#define _BSD_SOURCE /* for endian.h */
#include <time.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <bits/endian.h>

#include "libaio.h"
#define IOCB_FLAG_RESFD         (1 << 0)

#include <linux/usb/functionfs.h>
#include <sys/mman.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <stdint.h>  // 引入 uintptr_t 的定义
#include <pthread.h>

#include "shmfifo.h"                                            

#define APS_DATA_LEN (768 * 608 * 3 / 2)
// #define APS_DATA_LEN (640 * 480 * 3 / 2)
#define BUFS_MAX 1
#define BUF_LEN (APS_DATA_LEN / BUFS_MAX)

#define EVS_DATA_LEN (4096*512)
#define EVS_BUFS_MAX 4
#define EVS_BUFF_LEN (EVS_DATA_LEN / EVS_BUFS_MAX)

#define AIO_MAX 10

// 定义日志开关
#define ENABLE_LOG 1  // 1 表示开启日志，0 表示关闭日志

// 定义日志宏
#if ENABLE_LOG
    #define LOG_PRINTF(...) printf(__VA_ARGS__)
#else
    #define LOG_PRINTF(...)
#endif

pthread_t apspid;
pthread_t evspid;
int ep0;
int ep[2];
bool ready;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool bquit = false;
static void sigterm_handler(int sig) {
    bquit = true;
}
static const struct {
	struct usb_functionfs_descs_head_v2 header;
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	__le32 os_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio bulk_sink;
		struct usb_endpoint_descriptor_no_audio bulk_source;
	} __attribute__ ((__packed__)) fs_descs, hs_descs;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio sink;
		struct usb_ss_ep_comp_descriptor sink_comp;
		struct usb_endpoint_descriptor_no_audio source;
		struct usb_ss_ep_comp_descriptor source_comp;
	} __attribute__ ((__packed__)) ss_descs;
	struct usb_os_desc_header os_header;
	struct usb_ext_compat_desc os_desc;

} __attribute__ ((__packed__)) descriptors = {
	.header = {
		.magic = (FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = (FUNCTIONFS_HAS_FS_DESC |
				 FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC |
				 FUNCTIONFS_HAS_MS_OS_DESC),
		.length = (sizeof(descriptors)),
	},
	.fs_count = (3),
	.fs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.fs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.fs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.fs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
	},
	.hs_count = (3),
	.hs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.hs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.hs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.hs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
	},
    .ss_count = (5),  // SuperSpeed 描述符数量为 5
    .ss_descs = {
        .intf = {
            .bLength = sizeof(descriptors.ss_descs.intf),          // 接口描述符长度
            .bDescriptorType = USB_DT_INTERFACE,                   // 描述符类型为接口
            .bInterfaceNumber = 0,                                 // 接口编号为0
            .bNumEndpoints = 2,                                    // 包含两个端点
            .bInterfaceClass = USB_CLASS_VENDOR_SPEC,              // 供应商特定的接口类
            .iInterface = 1,                                       // 接口字符串索引为1
        },
        .sink = {
            .bLength = sizeof(descriptors.ss_descs.sink),          // 端点描述符长度
            .bDescriptorType = USB_DT_ENDPOINT,                    // 描述符类型为端点
            .bEndpointAddress = 1 | USB_DIR_IN,                    // 端点地址 (1, IN方向)
            .bmAttributes = USB_ENDPOINT_XFER_BULK,                // 批量传输类型
            .wMaxPacketSize = (1024),                              // 最大数据包大小为1024字节（SuperSpeed标准最大为1024）
        },
        .sink_comp = {
            .bLength = sizeof(descriptors.ss_descs.sink_comp),     // 补充描述符长度
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,            // SuperSpeed 端点补充描述符类型
            .bMaxBurst = 4,                                        // 最大Burst数量为4（表示主机可以连续传输的包数量 - 增加吞吐量）
            .bmAttributes = 0,                                     // 通常为0（在批量传输情况下）
            .wBytesPerInterval = 0,                                // 对于批量端点，这个字段为0
        },
        .source = {
            .bLength = sizeof(descriptors.ss_descs.source),        // 端点描述符长度
            .bDescriptorType = USB_DT_ENDPOINT,                    // 描述符类型为端点
            .bEndpointAddress = 2 | USB_DIR_IN,                    // 端点地址 (2, IN方向)
            .bmAttributes = USB_ENDPOINT_XFER_BULK,                // 批量传输类型
            .wMaxPacketSize = (1024),                              // 最大数据包大小为1024字节
        },
        .source_comp = {
            .bLength = sizeof(descriptors.ss_descs.source_comp),   // 补充描述符长度
            .bDescriptorType = USB_DT_SS_ENDPOINT_COMP,            // SuperSpeed 端点补充描述符类型
            .bMaxBurst = 4,                                        // 最大Burst数量为4
            .bmAttributes = 0,                                     // 批量传输情况下为0
            .wBytesPerInterval = 0,                                // 对于批量端点为0
        },
    },
	.os_count = (1),
	.os_header = {
		.interface = (1),
		.dwLength = (sizeof(descriptors.os_header) +
			    sizeof(descriptors.os_desc)),
		.bcdVersion = (1),
		.wIndex = (4),
		.bCount = (1),
		.Reserved = (0),
	},
	.os_desc = {
		.bFirstInterfaceNumber = 0,
		.Reserved1 = (1),
		.CompatibleID = {0},
		.SubCompatibleID = {0},
		.Reserved2 = {0},
	},
};

#define STR_INTERFACE "AIO Test"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE)];
	} __attribute__ ((__packed__)) lang0;
} __attribute__ ((__packed__)) strings = {
	.header = {
		.magic = (FUNCTIONFS_STRINGS_MAGIC),
		.length = (sizeof(strings)),
		.str_count = (1),
		.lang_count = (1),
	},
	.lang0 = {
		(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

/********************** Buffer structure *******************************/

struct io_buffer {
    char **buf; // 存储指向缓冲区的指针
    struct iocb **iocb; // 存储I/O控制块
    unsigned cnt; // 缓冲区数量
    unsigned len; // 每个缓冲区的长度
    unsigned requested; // 请求计数
};

/******************** Function Prototypes *******************************/

int setup_aio_context(io_context_t *ctx, int max_requests);
int setup_shared_memory(const char *name, int len, char **mapped_data);
void init_bufs(struct io_buffer *iobuf, unsigned n, unsigned len);
void delete_bufs(struct io_buffer *iobuf);
void process_io(struct iocb *iocb, io_context_t ctx, int fd, char *buffer, size_t len);
static void display_event(struct usb_functionfs_event *event);
static void handle_ep0(int ep0, bool *ready);
void *sendApsDataPthread(void *arg);
static void sendApsDataInit(void *arg);
void *sendEvsDataPthread(void *arg);
static void sendEvsDataInit(void *arg);

/******************** Function Definitions *******************************/

int setup_aio_context(io_context_t *ctx, int max_requests) {
    memset(ctx, 0, sizeof(*ctx));
    if (io_setup(max_requests, ctx) < 0) {
        perror("unable to setup aio");
        return -1;
    }
    return 0;
}

int setup_shared_memory(const char *name, int len, char **mapped_data) {
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0777);
    if (shm_fd < 0) {
        perror("shm_open");
        return -1;
    }
    ftruncate(shm_fd, len);
    *mapped_data = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (*mapped_data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        *mapped_data = MAP_FAILED; // Explicitly set to MAP_FAILED
        return -1;
    }
    LOG_PRINTF("Shared memory '%s' mapped at %p with length %d\n", name, *mapped_data, len);
    return shm_fd;
}

void init_bufs(struct io_buffer *iobuf, unsigned n, unsigned len) {
    iobuf->buf = malloc(n * sizeof(*iobuf->buf));
    iobuf->iocb = malloc(n * sizeof(*iobuf->iocb));
    iobuf->cnt = n;
    iobuf->len = len;
    iobuf->requested = 0;
    for (unsigned i = 0; i < n; ++i) {
        iobuf->buf[i] = malloc(len * sizeof(**iobuf->buf));
        iobuf->iocb[i] = malloc(sizeof(**iobuf->iocb));
    }
}

void delete_bufs(struct io_buffer *iobuf) {
    for (unsigned i = 0; i < iobuf->cnt; ++i) {
        free(iobuf->buf[i]);
        free(iobuf->iocb[i]);
    }
    free(iobuf->buf);
    free(iobuf->iocb);
}

void process_io(struct iocb *iocb, io_context_t ctx, int fd, char *buffer, size_t len) {
    io_prep_pwrite(iocb, fd, buffer, len, 0);
    iocb->u.c.flags |= IOCB_FLAG_RESFD;
    if (io_submit(ctx, 1, &iocb) < 0) {
        perror("unable to submit request");
    }
}

/******************** Endpoints routines ************************************/

static void handle_setup(const struct usb_ctrlrequest *setup)
{
	printf("bRequestType = 0x%02x\n", setup->bRequestType);
	printf("bRequest     = 0x%02x\n", setup->bRequest);
	printf("wValue       = 0x%02x\n", setup->wValue);
	printf("wIndex       = 0x%02x\n", setup->wIndex);
	printf("wLength      = 0x%02x\n", setup->wLength);
}

static void display_event(struct usb_functionfs_event *event) {
    static const char *const names[] = {
        [FUNCTIONFS_BIND] = "BIND",
        [FUNCTIONFS_UNBIND] = "UNBIND",
        [FUNCTIONFS_ENABLE] = "ENABLE",
        [FUNCTIONFS_DISABLE] = "DISABLE",
        [FUNCTIONFS_SETUP] = "SETUP",
        [FUNCTIONFS_SUSPEND] = "SUSPEND",
        [FUNCTIONFS_RESUME] = "RESUME",
    };
    switch (event->type) {
    case FUNCTIONFS_BIND:
    case FUNCTIONFS_UNBIND:
    case FUNCTIONFS_ENABLE:
    case FUNCTIONFS_DISABLE:
    case FUNCTIONFS_SETUP:
    case FUNCTIONFS_SUSPEND:
    case FUNCTIONFS_RESUME:
        printf("Event %s\n", names[event->type]);
    }
}

// 定义请求码
#define REQUEST_GET_STATUS  0x00 // 示例: 获取状态
#define REQUEST_SET_CONFIG  0x01 // 示例: 设置配置
#define REQUEST_CUSTOM_CMD  0x02 // 示例: 自定义命令
#define REQUEST_UPDATE_FILE  0x03 // 示例: update
#define REQUEST_REBOOT_CMD   0x04

#define REQUEST_CLEAR_SHM    0x07



#define SHM_KEY 0x1234         /* Key for shared memory segment */
#define BLOCKS 3
#define FILE_PATH "/opt/update.img" // 文件保存路径
#define MAX_BUFFER_SIZE 512  // 假设每次接收的最大数据量（512字节）

typedef struct {
    u_int32_t flag;
    u_int32_t denoise;
    u_int32_t seq;
    u_int32_t size;
    uint8_t received_buff[64]; // 接收数据缓冲区
} __attribute__((aligned(16))) config;

// 全局共享内存指针
char *global_aps_mmap = NULL;
char *global_evs_mmap = NULL;
// 新增全局状态标记
static volatile bool aps_mmap_ready = false;
static volatile bool evs_mmap_ready = false;

pthread_mutex_t shm_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_setup_request(int ep0, struct usb_ctrlrequest *setup) {
    int ret;
    uint8_t response_buffer[64]; // 响应数据缓冲区
    uint8_t received_buffer[64]; // 接收数据缓冲区

    // Initialize shared memory FIFO
    shmfifo_t *fifo = shmfifo_init(SHM_KEY, BLOCKS, sizeof(config));
    if (!fifo) {
        fprintf(stderr, "Failed to initialize shared memory FIFO.\n");
        exit(EXIT_FAILURE);
    }
    config p;
    memset(&p, 0x00, sizeof(p));
    static int seq = 0;

    size_t total_received = 0;  // 已接收的字节数
    size_t file_size = setup->wLength;  // 文件总大小
    char buffer[MAX_BUFFER_SIZE];    // 用于接收数据的缓冲区
    FILE *outFile = fopen("/tmp/update.bin", "ab+");  // 打开文件保存接收的数据
    if (outFile == NULL) {
        perror("Failed to open file for writing");
        return;
    }

    // 打印接收到的请求信息
    handle_setup(setup);
    // 根据请求码处理
    switch (setup->bRequest) {
        case REQUEST_GET_STATUS:
            if (setup->bRequestType & USB_DIR_IN) {
                // 设备向主机发送状态信息
                response_buffer[0] = 0x01; // 示例状态: OK
                ret = write(ep0, response_buffer, 1);
                if (ret < 0) perror("Error sending status");
            } else {
                // 主机向设备发送无效方向
                perror("Invalid direction for GET_STATUS");
            }
            break;

        case REQUEST_SET_CONFIG:
            if (!(setup->bRequestType & USB_DIR_IN)) {
                // 主机向设备发送配置数据
                ret = read(ep0, p.received_buff, setup->wLength);
                if (ret < 0) {
                    perror("Error reading configuration data");
                } else {
                    switch (p.received_buff[0])
                    {
                    case 0x01:      //denoise
                        printf("Received config data: ");
                        for (int i = 0; i < ret; i++) {
                            printf("%02x ", p.received_buff[i]);
                        }
                        p.flag = ret;
                        p.seq = seq++;
                        p.denoise = (u_int32_t)p.received_buff[1];
                        printf("denoise:%d\n",p.denoise);
                        shmfifo_put(fifo,&p);
                    break;
                    
                    case 0x02:     //update
                        /* code */
                        break;
                    default:
                        break;
                    }

                }
            } else {
                perror("Invalid direction for SET_CONFIG");
            }
            break;

        case REQUEST_CUSTOM_CMD:
            if (setup->bRequestType & USB_DIR_IN) {
                // 自定义命令，设备发送数据到主机
                response_buffer[0] = 0xDE; // 示例数据
                response_buffer[1] = 0xAD;
                response_buffer[2] = 0xBE;
                response_buffer[3] = 0xEF;
                ret = write(ep0, response_buffer, 4);
                if (ret < 0) perror("Error sending custom command response");
            } else {
                // 自定义命令，主机发送数据到设备
                ret = read(ep0, received_buffer, setup->wLength);
                if (ret < 0) {
                    perror("Error reading custom command data");
                } else {
                    printf("Received custom command data: ");
                    for (int i = 0; i < ret; i++) {
                        printf("%02x ", received_buffer[i]);
                    }
                    printf("\n");
                }
            }
            break;

        case REQUEST_UPDATE_FILE:  // 主机向设备发送文件数据
            if (!(setup->bRequestType & USB_DIR_IN)) {  // 确保是从主机向设备传输数据

                // 分块接收数据，直到接收到完整文件
                while (total_received < file_size) {
                    // 每次读取的大小为最大缓冲区大小，或者剩余的文件大小
                    size_t chunk_size = (file_size - total_received > MAX_BUFFER_SIZE) ? MAX_BUFFER_SIZE : (file_size - total_received);

                    ret = read(ep0, buffer, chunk_size);  // 从端点读取数据
                    if (ret < 0) {
                        perror("Error reading data from ep0");
                        fclose(outFile);  // 关闭文件
                        return;
                    }

                    // 将接收到的数据写入文件
                    fwrite(buffer, 1, ret, outFile);
                    total_received += ret;  // 更新已接收的总字节数

                    printf("Received %zu bytes, Total received: %zu/%zu bytes\n", ret, total_received, file_size);
                }

                // 文件接收完成
                printf("File received successfully, total size: %zu bytes\n", total_received);
                fclose(outFile);  // 关闭文件
                // system("/usr/bin/updateEngine --image_url=/opt/update.img --update --partition=0x0100 --reboot");
                // system("reboot");
            } else {
                perror("Invalid direction for REQUEST_UPDATE_FILE_CMD");
            }
            break;

        case REQUEST_REBOOT_CMD:

            system("/usr/bin/updateEngine --image_url=/tmp/update.bin --update --partition=0x0100 --reboot");
            break;
        
        case REQUEST_CLEAR_SHM:
            LOG_PRINTF("REQUEST_CLEAR_SHM received. global_aps_mmap = %p, aps_mmap_ready = %d, global_evs_mmap = %p, evs_mmap_ready = %d\n",
                       global_aps_mmap, aps_mmap_ready, global_evs_mmap, evs_mmap_ready);
            pthread_mutex_lock(&shm_mutex);
            if (!(setup->bRequestType & USB_DIR_IN)) {
                // 双重检查指针有效性
                if (global_aps_mmap && global_aps_mmap != MAP_FAILED && aps_mmap_ready) {
                    memset(global_aps_mmap, 0, APS_DATA_LEN);
                    LOG_PRINTF("Cleared APS shared memory.\n");
                } else {
                    LOG_PRINTF("APS shared memory not ready or invalid (mmap: %p, ready: %d).\n", global_aps_mmap, aps_mmap_ready);
                }
                if (global_evs_mmap && global_evs_mmap != MAP_FAILED && evs_mmap_ready) {
                    memset(global_evs_mmap, 0, EVS_DATA_LEN);
                    LOG_PRINTF("Cleared EVS shared memory.\n");
                } else {
                    LOG_PRINTF("EVS shared memory not ready or invalid (mmap: %p, ready: %d).\n", global_evs_mmap, evs_mmap_ready);
                }
            } else {
                perror("Invalid direction for REQUEST_CLEAR_SHM");
            }
            pthread_mutex_unlock(&shm_mutex);
            break;
        default:
            // 未知请求
            printf("Unknown request");
            break;
    }
    
    // 完成状态阶段
    write(ep0, NULL, 0);
}
static void handle_ep0(int ep0, bool *ready) {
    struct usb_functionfs_event event;
    struct usb_ctrlrequest setup;
    int ret;
    struct pollfd pfds[1];
    pfds[0].fd = ep0;
    pfds[0].events = POLLIN;

    ret = poll(pfds, 1, 0);

    printf("\n\nenent.type:%02x\n",event.type);

    if (ret && (pfds[0].revents & POLLIN)) {
        ret = read(ep0, &event, sizeof(event));
        if (!ret) {
            perror("unable to read event from ep0");
            return;
        }
        display_event(&event);
        switch (event.type) {
        // case FUNCTIONFS_SETUP:
        //     if (event.u.setup.bRequestType & USB_DIR_IN)
        //         write(ep0, NULL, 0);
        //     else
        //         read(ep0, NULL, 0);
        //     break;
       case FUNCTIONFS_SETUP:
            // 读取 SETUP 数据包
            memcpy(&setup, &event.u.setup, sizeof(setup));
            handle_setup_request(ep0, &setup);
            break;

        case FUNCTIONFS_ENABLE:
            pthread_mutex_lock(&lock);
            *ready = true;
            pthread_mutex_unlock(&lock);
            break;

        case FUNCTIONFS_DISABLE:
            pthread_mutex_lock(&lock);
            *ready = false;
            pthread_mutex_unlock(&lock);
            break;

        default:
            break;
        }
    }
}

void *sendApsDataPthread(void *arg) {
    struct io_buffer iobuf[BUFS_MAX];
    io_context_t apsctx;
    int efd = eventfd(0, 0);
	int ret;

	fd_set rfds;

	int actual = 0;
	bool ready;

    char *aps_local_mmap_buf;
    int mmapfd = setup_shared_memory("/apcdatashm", APS_DATA_LEN, &aps_local_mmap_buf);
    global_aps_mmap = aps_local_mmap_buf;
    LOG_PRINTF("APS Thread: global_aps_mmap = %p, APS_DATA_LEN = %d\n", global_aps_mmap, APS_DATA_LEN);

    sem_t *wait_aps_sem = sem_open("/wait_aps_sem", O_CREAT | O_RDWR, 0666, 0);
    sem_t *send_done_sem = sem_open("/send_done_sem", O_CREAT | O_RDWR, 0666, 1);

    if (efd < 0 || mmapfd < 0 || global_aps_mmap == MAP_FAILED) {
        perror("Initialization failed");
        return NULL;
    }

    if (setup_aio_context(&apsctx, AIO_MAX) < 0) {
        return NULL;
    }

    for (int i = 0; i < BUFS_MAX; ++i) {
        init_bufs(&iobuf[i], BUFS_MAX, BUF_LEN);
    }

    printf("========= Start USB APS data pthread =========\n");
    pthread_mutex_lock(&shm_mutex);
    aps_mmap_ready = true;
    pthread_mutex_unlock(&shm_mutex);

    while (1) {
        

		/*超时时间宏: s*/
		#define DIAG_TIMEOUT 2
		struct timeval now;
		struct timespec out_time;

		/*开始进行超时等待*/
		gettimeofday(&now, NULL);
		out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
		out_time.tv_nsec = now.tv_usec * 1000;
		if(sem_timedwait(wait_aps_sem,&out_time) <0 )
		{
			LOG_PRINTF("======timeout wait_aps_sem\n");
		}
        size_t total_bytes_read = 0;
        while (total_bytes_read < APS_DATA_LEN) {
            for (int i = 0; i < BUFS_MAX; ++i) {
                if (iobuf[i].requested) continue;

                size_t bytes_to_read = (APS_DATA_LEN - total_bytes_read < BUF_LEN) ?
                                      (APS_DATA_LEN - total_bytes_read) : BUF_LEN;

                memcpy(iobuf[i].buf[i], global_aps_mmap + total_bytes_read, bytes_to_read);
                total_bytes_read += bytes_to_read;

                io_prep_pwrite(iobuf[i].iocb[i], ep[0], iobuf[i].buf[i], bytes_to_read, 0);
                iobuf[i].iocb[i]->u.c.flags |= IOCB_FLAG_RESFD;
                iobuf[i].iocb[i]->u.c.resfd = efd;

                if (io_submit(apsctx, 1, &iobuf[i].iocb[i]) >= 0) {
                    iobuf[i].requested++;
                    // printf("Submitted APS: Buffer %d, Bytes: %zu\n", i, bytes_to_read);
                } else {
                    perror("Unable to submit request");
                }
            }

            struct io_event e[BUFS_MAX];
            int ret = io_getevents(apsctx, 1, BUFS_MAX, e, NULL);

            if (ret > 0) {
                iobuf[actual].requested -= ret;
                actual = (actual + 1) % BUFS_MAX;
            }
        }
        sem_post(send_done_sem);
    }

    for (int i = 0; i < BUFS_MAX; ++i) {
        delete_bufs(&iobuf[i]);
    }
    io_destroy(apsctx);
    printf("================ Exit sendApsDataPthread ================\n");
    return NULL;
}

void *sendEvsDataPthread(void *arg) {
    struct io_buffer iobuf[EVS_BUFS_MAX];
    io_context_t evsctx;

	int actual = 0;
    int efd = eventfd(0, 0);// EFD_NONBLOCK
	// int ret;
	// fd_set rfds;
	// bool ready;


    char *evs_local_mmap_buf;
    int mmapfd = setup_shared_memory("/dvsdatashm", EVS_DATA_LEN, &evs_local_mmap_buf);
    global_evs_mmap = evs_local_mmap_buf;
    LOG_PRINTF("EVS Thread: After setup_shared_memory for /dvsdatashm:\n");
    LOG_PRINTF("  mmapfd = %d\n", mmapfd);
    LOG_PRINTF("  global_evs_mmap = %p\n", global_evs_mmap);
    LOG_PRINTF("  EVS_DATA_LEN = %d\n", EVS_DATA_LEN); // Print the actual value of EVS_DATA_LEN

    sem_t *wait_dvs_sem     = sem_open("/wait_dvs_sem", O_CREAT|O_RDWR, 0666, 0); //信号量值为 0
    sem_t *send_donedvs_sem = sem_open("/send_donedvs_sem", O_CREAT|O_RDWR, 0666, 1); //信号量值为 1

    if (efd < 0 || mmapfd < 0 || global_evs_mmap == MAP_FAILED) {
        perror("Initialization failed");
        return NULL;
    }

    if (setup_aio_context(&evsctx, AIO_MAX) < 0) {
        return NULL;
    }

    for (int i = 0; i < EVS_BUFS_MAX; ++i) {
        init_bufs(&iobuf[i], EVS_BUFS_MAX, EVS_BUFF_LEN);
    }

    printf("========= Start USB EVS data pthread =========\n");
    pthread_mutex_lock(&shm_mutex);
    evs_mmap_ready = true;
    pthread_mutex_unlock(&shm_mutex);

    while (1) {
        
		#define DIAG_TIMEOUT 3
		struct timeval now;
		struct timespec out_time;

		/*开始进行超时等待*/
		gettimeofday(&now, NULL);
		out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
		out_time.tv_nsec = now.tv_usec * 1000;
		if(sem_timedwait(wait_dvs_sem,&out_time) <0 )
		{
			printf("======timeout wait_dvs_sem\n");
		}

        size_t total_bytes_read = 0;
        while (total_bytes_read < EVS_DATA_LEN) {
            for (int i = 0; i < EVS_BUFS_MAX; ++i) {
                if (iobuf[i].requested) continue;

                size_t bytes_to_read = (EVS_DATA_LEN - total_bytes_read < EVS_BUFF_LEN) ?
                                      (EVS_DATA_LEN - total_bytes_read) : EVS_BUFF_LEN;

                memcpy(iobuf[i].buf[i], global_evs_mmap + total_bytes_read, bytes_to_read);
                total_bytes_read += bytes_to_read;

                io_prep_pwrite(iobuf[i].iocb[i], ep[1], iobuf[i].buf[i], bytes_to_read, 0);
                iobuf[i].iocb[i]->u.c.flags |= IOCB_FLAG_RESFD;
                iobuf[i].iocb[i]->u.c.resfd = efd;

                if (io_submit(evsctx, 1, &iobuf[i].iocb[i]) >= 0) {
                    iobuf[i].requested++;
                    // printf("Submitted EVS------: Buffer %d, Bytes: %zu\n", i, bytes_to_read);
                } else {
                    perror("Unable to submit request");
                }
            }

            struct io_event e[BUFS_MAX];
            int ret = io_getevents(evsctx, 1, BUFS_MAX, e, NULL);

            if (ret > 0) {
                iobuf[actual].requested -= ret;
                actual = (actual + 1) % BUFS_MAX;
            }
        }
        sem_post(send_donedvs_sem);

    }

    for (int i = 0; i < EVS_BUFS_MAX; ++i) {
        delete_bufs(&iobuf[i]);
    }
    io_destroy(evsctx);
    printf("================ Exit sendApsDataPthread ================\n");
    return NULL;
}

static void sendApsDataInit(void *arg) {
    pthread_create(&apspid, NULL, sendApsDataPthread, arg);
}

static void sendEvsDataInit(void *arg) {
    pthread_create(&evspid, NULL, sendEvsDataPthread, arg);
}


void usb_bulk_receive_callback(uint8_t *data, int length) {
    if (length <= 0) {
        return;
    }


}


int main(int argc, char *argv[]) {
    int i,ret;
    char *ep_path;
    // int ep0;
    // struct usb_ctrlrequest ctrl;
    // int efd = eventfd(0, 0);
	fd_set rfds;
    if (argc != 2) {
        printf("ffs directory not specified!\n");
        return 1;
    }

    ep_path = malloc(strlen(argv[1]) + 4 /* "/ep#" */ + 1 /* '\0' */);
    if (!ep_path) {
        perror("malloc");
        return 1;
    }

    /* open endpoint files */
    sprintf(ep_path, "%s/ep0", argv[1]);
    ep0 = open(ep_path, O_RDWR);
    if (ep0 < 0) {
        perror("unable to open ep0");
        return 1;
    }
    if (write(ep0, &descriptors, sizeof(descriptors)) < 0) {
        perror("unable do write descriptors");
        return 1;
    }
    if (write(ep0, &strings, sizeof(strings)) < 0) {
        perror("unable to write strings");
        return 1;
    }
    for (i = 0; i < 2; ++i) {
        sprintf(ep_path, "%s/ep%d", argv[1], i + 1);//ep1 ep2
        ep[i] = open(ep_path, O_RDWR);
        if (ep[i] < 0) {
            perror("unable to open ep");
            free(ep_path);
            return 1;
        }
    }

    free(ep_path);

    /* Initialize and create threads for APS and EVS */
    sendEvsDataInit(NULL);
    sendApsDataInit(NULL);

    // Wait for shared memory to be ready
    printf("Waiting for shared memory to be ready...\n");
    while (true) {
        pthread_mutex_lock(&shm_mutex);
        if (aps_mmap_ready && evs_mmap_ready) {
            pthread_mutex_unlock(&shm_mutex);
            break;
        }
        pthread_mutex_unlock(&shm_mutex);
        usleep(10000); // Wait for 10ms before checking again
    }
    printf("Shared memory is ready.\n");
    
    signal(SIGINT, sigterm_handler);

    while (!bquit) {   
		FD_ZERO(&rfds);
		FD_SET(ep0, &rfds);

		ret = select(ep0+1,&rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (FD_ISSET(ep0, &rfds))
			handle_ep0(ep0, &ready);

    }

    /* Main thread waits for completion */
    pthread_join(apspid, NULL);
    pthread_join(evspid, NULL);


    /* free resources */
    for (i = 0; i < 2; ++i)
        close(ep[i]);
    close(ep0);

    return 0;
}
