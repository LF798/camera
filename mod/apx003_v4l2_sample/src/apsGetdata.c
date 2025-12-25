#include <stdint.h>
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
#include<semaphore.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>

#include "apsGetdata.h"

// #define SENSOR_APX003CC
#define BUF_NUM (5)

#define ERR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define FMT_NUM_PLANES 1

#define BUFFER_COUNT 4
#define SOF_EOF_DIFF_CNT   (30)


#define NSEC_PER_SEC    1000000000L
#define NSEC_PER_USEC   1000L

char * apsParamPath = "/oem/apsParam";
apsParam gApsParam;

static inline long long app_timeval_to_ns(const struct timeval *tv)
{
    return ((long long)tv->tv_sec * NSEC_PER_SEC) + tv->tv_usec * NSEC_PER_USEC;
}

// return sys tick in us
unsigned long GetTickCount()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}


static int xioctl(int fh, int request, void *arg)
{
        int r;
        do {
                r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);
        return r;
}



static void errno_exit(const char *s)
{
        ERR("%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}



struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;
static enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

void camera_deinit(void)
{
    unsigned int i;

    for (i = 0; i < n_buffers; ++i) {
        if (-1 == munmap(buffers[i].start, buffers[i].length))
                errno_exit("munmap");
    }

    free(buffers);
    buffers = NULL;
}
struct buffer  *mmap_buffer(int fd)
{
      struct v4l2_requestbuffers req;

    CLEAR(req);

    printf("%s start\n", __func__);

    req.count = BUFFER_COUNT;
    req.type = buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
            if (EINVAL == errno) {
                    ERR("%s does not support "
                             "memory mapping\n", __func__);
                    exit(EXIT_FAILURE);
            } else {
                    errno_exit("VIDIOC_REQBUFS");
            }
    }

    if (req.count < 2) {
            ERR("Insufficient buffer memory on %s\n",
                     __func__);
            exit(EXIT_FAILURE);
    }

    buffers = (struct buffer*)calloc(req.count, sizeof(*buffers));

    if (!buffers) {
            ERR("Out of memory\n");
            exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
            struct v4l2_buffer buf;
            struct v4l2_plane planes[FMT_NUM_PLANES];
            CLEAR(buf);
            CLEAR(planes);

            buf.type = buf_type;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = n_buffers;

            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
                buf.m.planes = planes;
                buf.length = FMT_NUM_PLANES;
            }

            if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                    errno_exit("VIDIOC_QUERYBUF");

            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
                buffers[n_buffers].length = buf.m.planes[0].length;
                buffers[n_buffers].start =
                    mmap(NULL /* start anywhere */,
                          buf.m.planes[0].length,
                          PROT_READ | PROT_WRITE /* required */,
                          MAP_SHARED /* recommended */,
                          fd, buf.m.planes[0].m.mem_offset);
            } else {
                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                    mmap(NULL /* start anywhere */,
                          buf.length,
                          PROT_READ | PROT_WRITE /* required */,
                          MAP_SHARED /* recommended */,
                          fd, buf.m.offset);
            }

            if (MAP_FAILED == buffers[n_buffers].start)
                    errno_exit("mmap");
    }
    buffers->fd = fd;
    printf("%s done\n", __func__);
    
    return buffers;
}
 int unmap_buffer(struct buffer *usr_buf)
{
    if (usr_buf == NULL) {
        printf("error param\n");
        return -1;
    }

    for(unsigned int i = 0; i < n_buffers; i++) {
        int ret = munmap(usr_buf[i].start, usr_buf[i].length);
        if (ret < 0) {
            printf("munmap failed\n");
            return -1;
        }
    }

    free(usr_buf);
    usr_buf = NULL;

    return 0;
}
static void init_device(int fd, int width, int height, int format)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
                ERR("%s is no V4L2 device\n",
                         __func__);
                exit(EXIT_FAILURE);
        } else {
                errno_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        ERR("%s is not a video capture device, capabilities: %x\n",
                     __func__, cap.capabilities);
            exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
            ERR("%s does not support streaming i/o\n",
                __func__);
            exit(EXIT_FAILURE);
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    CLEAR(fmt);
    fmt.type = buf_type;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = format;
    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        printf("%s: Current output format: fmt=0x%X,%dx%d, num_planes:%d\r\n", __func__,
               fmt.fmt.pix_mp.pixelformat,
               fmt.fmt.pix_mp.width,
               fmt.fmt.pix_mp.height,
               fmt.fmt.pix_mp.num_planes
        );
    } else {
        printf("VIDIOC_G_FMT: %s", strerror(errno));
    }
}

int camera_init(const char* dev, __u32 width, __u32 height, __u32 pixformat)
{
    int video_fd = -1;

	video_fd = open(dev, O_RDWR,0);
    if(video_fd < 0) {
        printf("open \"%s\" error\n", dev);
        return -1;
    }

    init_device(video_fd, width, height, pixformat);

    return video_fd;
}

int stream_on(int video_fd)
{
    unsigned int i;
    enum v4l2_buf_type type;

    printf("%s start, n_buffers:%d\n", __func__, n_buffers);

    for (i = 0; i < n_buffers; ++i) {
            struct v4l2_buffer buf;

            CLEAR(buf);
            buf.type = buf_type;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
                struct v4l2_plane planes[FMT_NUM_PLANES];

                buf.m.planes = planes;
                buf.length = FMT_NUM_PLANES;
            }
            if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
                    errno_exit("VIDIOC_QBUF");
    }

    type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");

    printf("%s done\n", __func__);

    return 0;
}

int stream_off(int video_fd)
{
    enum v4l2_buf_type type;

    type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMOFF, &type))
        errno_exit("VIDIOC_STREAMOFF");

    return 0;
}

void release_camera(int video_fd)
{
    close(video_fd);
}

static inline uint64_t get_exposure_start_timestamp(void)
{
    char const *expstart_ts_fn = "/sys/module/sc132gs/parameters/g_exposure_start_timestamp_ns";
    int dvs_ts_fd = -1;
    int readlen = 0;
    char readtmp[24] = {0};
    uint64_t ret = 0;

    if (access(expstart_ts_fn, R_OK) != 0) {
        printf("%s, no g_exposure_start_timestamp_ns export????\n", __func__);
    } else {
        dvs_ts_fd = open(expstart_ts_fn, O_RDONLY);
        if (dvs_ts_fd < 0) {
            printf("%s, open g_exposure_start_timestamp_ns file failed\n", __func__);
        } else {
            readlen = read(dvs_ts_fd, readtmp, 16);
            ret = strtoul(readtmp, NULL, 0);

            close(dvs_ts_fd);
        }
    }

    return ret;
}



#define USEC_PER_SEC    1000000L

static inline unsigned long app_timeval_to_us(const struct timeval *tv)
{
    return ((unsigned long)tv->tv_sec * USEC_PER_SEC) + tv->tv_usec;
}

typedef struct rkisp_frame_info_s
{
    int64_t frame_id;
    uint64_t frame_sof_ns;
    uint64_t exptime_ns;
}IspFrameInfo;

typedef struct frame_desc_t
{
    void* addr;
    uint32_t data_size;
    uint32_t index;
    uint64_t frame_end_ts;  //ns
    uint64_t exp_start_ts;
}FrameDesc;

static FrameDesc frame_desc_buffer[BUFFER_COUNT] = {0};

static IspFrameInfo cur_isp_info = {0};
static IspFrameInfo prev_isp_info = {0};

uint64_t dvs_ts_offset_ns = 0;
unsigned long curFrametimestampUS = 0;
unsigned long preFrametimestampUS = 0;
static uint64_t cur_desc_num = 0;

// static void get_frame_info(void)
// {
//     static uint64_t presoftime=0;
//     float exptime = 0.0;
//     int64_t frame_sof = 0; // in ns

//     memcpy(&prev_isp_info, &cur_isp_info, sizeof(IspFrameInfo));

    // rkisp_getAeTime(exptime);
    // cur_isp_info.exptime_ns = (uint64_t)(exptime * 1000 * 1000 * 1000);

    // rkisp_get_meta_frame_id(cur_isp_info.frame_id);

    // rkisp_get_meta_frame_sof_ts(frame_sof);

    // cur_isp_info.frame_sof_ns = (uint64_t)frame_sof;

  //  printf("sof_time:%f,%ld,%ld\n",exptime,cur_isp_info.frame_sof_ns,cur_isp_info.frame_sof_ns - presoftime);
//     presoftime =cur_isp_info.frame_sof_ns;
//     return;
// }

int get_frame(int video_fd)
{
    struct v4l2_buffer buf;
    int bytesused;
    int ret = 0;
    int desc_idx = 0;
    unsigned long sys_timestamp_ns = 0;

    CLEAR(buf);

    buf.type = buf_type;

    buf.memory = V4L2_MEMORY_MMAP;

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
        struct v4l2_plane planes[FMT_NUM_PLANES];
        buf.m.planes = planes;
        buf.length = FMT_NUM_PLANES;
    }

    if (-1 == xioctl(video_fd, VIDIOC_DQBUF, &buf)) 
            errno_exit("VIDIOC_DQBUF");

     sys_timestamp_ns = (unsigned long)(app_timeval_to_ns(&buf.timestamp));
    if (cur_desc_num != 0) { // drop frame check
        desc_idx = (cur_desc_num-1)%BUFFER_COUNT;
        int last_index = frame_desc_buffer[desc_idx].index;
        if ((last_index + 1)%BUFFER_COUNT != buf.index) {
            printf("[error]: cur index:%d, last index:%d\n", buf.index, last_index);
            ret = -1;
        }

        int64_t ts_diff = sys_timestamp_ns - frame_desc_buffer[desc_idx].frame_end_ts;

         if (ts_diff > 27000000 || ts_diff < 17000000) {
            // printf("[error]: ts_diff:%ld, expect 22000000\n", ts_diff);
            // ret = -1;
        }
    }

    desc_idx = cur_desc_num % BUFFER_COUNT;
    frame_desc_buffer[desc_idx].addr = buffers[buf.index].start;
    frame_desc_buffer[desc_idx].data_size = bytesused;
    frame_desc_buffer[desc_idx].frame_end_ts = sys_timestamp_ns;
    frame_desc_buffer[desc_idx].index = buf.index;
    // frame_desc_buffer[desc_idx].exp_start_ts = get_exposure_start_timestamp();
    cur_desc_num++;
    if (cur_desc_num == 30) {
        unsigned long sync_ts_us = GetTickCount();
        // system("io -w -4 0xFF788000 0x1008000");
        printf("set sync pin high, sync_ts_us:%ld, sys_timestamp_ns:%ld\n", sync_ts_us, sys_timestamp_ns);
        dvs_ts_offset_ns = sync_ts_us*1000;
    }

    if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF"); 
    
    ret = buf.index;
    return ret;
}



static char * iq_file= "/etc/cam_iq.xml";
#define APS_DEV_NAME  ("/dev/video11")
#define APS_IMG_WIDTH  (640)
#define APS_IMG_HEIGHT (480)
#define APS_PIXEL_FMT  (V4L2_PIX_FMT_NV12)//(V4L2_PIX_FMT_SRGGB10)//(V4L2_PIX_FMT_YUYV)
#define APS_DATA_LEN APS_IMG_WIDTH*APS_IMG_HEIGHT*3/2
static unsigned char frame_info[32] = {0};

static struct buffer *aps_usr_buf = NULL;
int aps_fd = -1;


static inline int get_dvs_ts_offset(void)
{
    char const *dvs_ts_fn = "/sys/module/alpsen01/parameters/dvs_timestamp_ns";
    int dvs_ts_fd = -1;
    int readlen = 0;
    char readtmp[24] = {0};
    int ret = -1;

    if (access(dvs_ts_fn, R_OK) != 0) {
        printf("%s, no dvs_timestamp_ns export????\n", __func__);
    } else {
        dvs_ts_fd = open(dvs_ts_fn, O_RDONLY);
        if (dvs_ts_fd < 0) {
            printf("%s, open dvs ts file failed\n", __func__);
        } else {
            readlen = read(dvs_ts_fd, readtmp, 16);
            dvs_ts_offset_ns = strtoul(readtmp, NULL, 0);
            printf("readtmp:%s, dvs_ts_offset_ns:%lu\n", readtmp, dvs_ts_offset_ns);
            ret = 0;
        }
    }

    return ret;
}
static inline void pack_frame_info(uint64_t exptime_ns, uint64_t frame_sof_ns, uint64_t frame_eof_ns, unsigned char* frame_info)
{
    uint64_t verify_code = 0x0123456789abcdef;

    frame_info[0] = (verify_code & 0xff00000000000000) >> 56;
    frame_info[1] = (verify_code & 0x00ff000000000000) >> 48;
    frame_info[2] = (verify_code & 0x0000ff0000000000) >> 40;
    frame_info[3] = (verify_code & 0x000000ff00000000) >> 32;
    frame_info[4] = (verify_code & 0x00000000ff000000) >> 24;
    frame_info[5] = (verify_code & 0x0000000000ff0000) >> 16;
    frame_info[6] = (verify_code & 0x000000000000ff00) >> 8;
    frame_info[7] = verify_code & 0x00000000000000ff;

    frame_info[8] = (frame_sof_ns & 0xff00000000000000) >> 56;
    frame_info[9] = (frame_sof_ns & 0x00ff000000000000) >> 48;
    frame_info[10] = (frame_sof_ns & 0x0000ff0000000000) >> 40;
    frame_info[11] = (frame_sof_ns & 0x000000ff00000000) >> 32;
    frame_info[12] = (frame_sof_ns & 0x00000000ff000000) >> 24;
    frame_info[13] = (frame_sof_ns & 0x0000000000ff0000) >> 16;
    frame_info[14] = (frame_sof_ns & 0x000000000000ff00) >> 8;
    frame_info[15] = (frame_sof_ns & 0x00000000000000ff);

    frame_info[16] = (exptime_ns & 0xff00000000000000) >> 56;
    frame_info[17] = (exptime_ns & 0x00ff000000000000) >> 48;
    frame_info[18] = (exptime_ns & 0x0000ff0000000000) >> 40;
    frame_info[19] = (exptime_ns & 0x000000ff00000000) >> 32;
    frame_info[20] = (exptime_ns & 0x00000000ff000000) >> 24;
    frame_info[21] = (exptime_ns & 0x0000000000ff0000) >> 16;
    frame_info[22] = (exptime_ns & 0x000000000000ff00) >> 8;
    frame_info[23] = (exptime_ns & 0x00000000000000ff);

    frame_info[24] = (frame_eof_ns & 0xff00000000000000) >> 56;
    frame_info[25] = (frame_eof_ns & 0x00ff000000000000) >> 48;
    frame_info[26] = (frame_eof_ns & 0x0000ff0000000000) >> 40;
    frame_info[27] = (frame_eof_ns & 0x000000ff00000000) >> 32;
    frame_info[28] = (frame_eof_ns & 0x00000000ff000000) >> 24;
    frame_info[29] = (frame_eof_ns & 0x0000000000ff0000) >> 16;
    frame_info[30] = (frame_eof_ns & 0x000000000000ff00) >> 8;
    frame_info[31] = (frame_eof_ns & 0x00000000000000ff);

    return;
}

int apsInit(void)
{
    apsParam apsGetParam;
    aps_fd = camera_init(APS_DEV_NAME, APS_IMG_WIDTH, APS_IMG_HEIGHT, APS_PIXEL_FMT);
    if (aps_fd < 0) {
        perror("camera_init");
        return -1;
    }
	aps_usr_buf = mmap_buffer(aps_fd);
    if (aps_usr_buf == NULL) {
        printf("aps mmap failed\n");
        return 1;
    }

	stream_on(aps_fd);
    // get_dvs_ts_offset();
    return 0;
}
void apsDeinit(void)
{
    stream_off(aps_fd);
	unmap_buffer(aps_usr_buf);
	release_camera(aps_fd);
}
void *apsPthread(void *arg)
{
    uint8_t cur_desc_idx = 0;
    int64_t ts_diff = 0;
    int64_t eof_sof_diff_sum_us = 0;
    int64_t eof_sof_diff_us = 0;
    uint32_t avg_cnt = 0;
    uint8_t find_idx = 0;
    uint8_t is_find = 0;
    uint64_t isp_sof_ns = 0;
    uint32_t cnt = 0;

    uint64_t prevTimestamp = 0;
    int aps_frame_idx=0;
    char *papsdata = NULL;

    int frame_info_size = sizeof(frame_info);

    // int APS_DATA_LEN = APS_IMG_WIDTH*APS_IMG_HEIGHT*2+frame_info_size;
    // int APS_DATA_LEN = APS_IMG_WIDTH*APS_IMG_HEIGHT*2;

    int fd = shm_open("/apcdatashm", O_CREAT | O_RDWR, 0777);
    if (fd < 0) {
        perror("open apcdatashm");
    }
    ftruncate(fd,APS_DATA_LEN);

    sem_t *wait_aps_sem = sem_open("/wait_aps_sem", O_CREAT|O_RDWR, 0666, 1);
    sem_t *send_done_sem = sem_open("/send_done_sem", O_CREAT|O_RDWR, 0666, 0); 
    papsdata = (char *)mmap(NULL, APS_DATA_LEN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (papsdata== MAP_FAILED) {
		perror("mmap");
	}

    while(1)
    {
#ifdef SENSOR_APX003CC
        if(eof_sof_diff_us == 0)
        {
            get_frame_info();
            aps_frame_idx = get_frame(aps_fd);
            if (aps_frame_idx < 0 || cur_desc_num < 2) {
                continue;
            }
            find_idx = 0;
            is_find = 0;
            isp_sof_ns = cur_isp_info.frame_sof_ns;
            while(find_idx < 4 && find_idx < (cur_desc_num-1)) {
                cur_desc_idx = (cur_desc_num-1-find_idx)%BUFFER_COUNT;
                ts_diff = frame_desc_buffer[cur_desc_idx].frame_end_ts - isp_sof_ns;
                if (ts_diff > 38000000) {
                    find_idx++;
                } else if (ts_diff < 0) {
                    if ((find_idx == 0) && (isp_sof_ns != prev_isp_info.frame_sof_ns)) {
                        isp_sof_ns = prev_isp_info.frame_sof_ns;
                    } else {
                        printf("[error]:isp_sof_ns:%ld, buffer[0]:%ld, buffer[1]:%ld, buffer[2]:%ld, buffer[3]:%ld, cur_desc_idx:%d\n", 
                           isp_sof_ns, frame_desc_buffer[0].frame_end_ts, frame_desc_buffer[1].frame_end_ts,
                           frame_desc_buffer[2].frame_end_ts, frame_desc_buffer[3].frame_end_ts, cur_desc_idx);
                        break;
                    }
                    
                } else {
                    is_find = 1;
                    break;
                }
            }

            if (is_find == 0) {
                printf("not found idx, find_idx:%d, ts_diff:%ld\n", find_idx, ts_diff);
                printf("isp_frame_id:%ld, cur_desc_num:%ld\n", 
                        cur_isp_info.frame_id, cur_desc_num);
                continue;
            }

            if (avg_cnt < SOF_EOF_DIFF_CNT) {
                eof_sof_diff_sum_us += (ts_diff/1000);
                avg_cnt++;
            } else if (avg_cnt == SOF_EOF_DIFF_CNT) {
                eof_sof_diff_us = eof_sof_diff_sum_us / SOF_EOF_DIFF_CNT;
            }
        }
        else
        {
             get_frame_info();
             aps_frame_idx = get_frame(aps_fd);
                if (aps_frame_idx < 0) {
                continue;
            }

            cur_desc_idx = (cur_desc_num-1)%BUFFER_COUNT;
            isp_sof_ns = frame_desc_buffer[cur_desc_idx].frame_end_ts - eof_sof_diff_us*1000; 
        }
         pack_frame_info(cur_isp_info.exptime_ns, isp_sof_ns-dvs_ts_offset_ns, frame_desc_buffer[cur_desc_idx].frame_end_ts-dvs_ts_offset_ns, frame_info);
         if (cnt++ > 1000) 
         {
            cnt = 0;
            printf("find_idx:%d, ts_diff:%ld, eof_sof_diff_us:%ld\n", find_idx, ts_diff, eof_sof_diff_us);
        }
        //printf("time: %ld \n",cur_isp_info.exptime_ns);
#else
        aps_frame_idx = get_frame(aps_fd);
        // pack_frame_info(15000000, prevTimestamp -dvs_ts_offset_ns, frame_desc_buffer[aps_frame_idx].exp_start_ts-dvs_ts_offset_ns, frame_info);
       // printf("t:%ld,%ld,%ld\n",prevTimestamp,frame_desc_buffer[aps_frame_idx].exp_start_ts, frame_desc_buffer[aps_frame_idx].exp_start_ts - prevTimestamp);
#endif

         memcpy(papsdata,aps_usr_buf[aps_frame_idx].start,aps_usr_buf[aps_frame_idx].length);
        //  memcpy(papsdata+APS_DATA_LEN-frame_info_size, frame_info, frame_info_size);
         sem_post(wait_aps_sem);
        //  prevTimestamp = frame_desc_buffer[aps_frame_idx].exp_start_ts;
        struct timeval now;
        struct timespec out_time;

        /*开始进行超时等待*/
        gettimeofday(&now, NULL);
        out_time.tv_sec = now.tv_sec + 3;
        out_time.tv_nsec = now.tv_usec * 1000;
        if(sem_timedwait(send_done_sem,&out_time) < 0)
        {
           printf("dvs timeout send_donedvs_sem\n");
        }
       
        // ret = cbWriteBuffer(pRingbuf,dvs_usr_buf[dvs_frame_idx].start, dvs_usr_buf[dvs_frame_idx].length);
        // printf("=================%ld,%d\n",aps_usr_buf[aps_frame_idx].length,ret);
    }
    apsDeinit();

    sem_close(wait_aps_sem);
    sem_close(send_done_sem);

    sem_unlink("/wait_aps_sem"); 
    sem_unlink("/send_done_sem"); 
}

void apsStart(void)
{
     /* 创建ringbuf 作为生产者 */
   // CircularBuffer*  prb = cbCreate("DVSProduct","DVSdata",2*1024*1024,CRB_PERSONALITY_WRITER);

    pthread_t pid;
    pthread_create(&pid, NULL, apsPthread, /*(CircularBuffer *)prb*/ NULL);

}
void writeApsPmram(apsParam * pParam)
{
    FILE *fp = NULL;
    fp = fopen(apsParamPath, "w+");
    if (NULL == fp)
    {
        printf("open %s fail, errno: %d \n",apsParamPath,errno);
        return;
    }
    fwrite(pParam,sizeof(apsParam),1,fp);
    fclose(fp);
}

void readApsPmram(apsParam * pParam)
{
	FILE *fp = fopen(apsParamPath, "r");
	if (NULL == fp)
	{
		printf("open %s fail, errno: %d \n",apsParamPath,errno);
		return;
	}
    fread(pParam,sizeof(apsParam),1,fp);
    fclose(fp);
}
int main(int argc, char *argv[])
{
    int  ret = apsInit();
    if(ret == 0 )
        printf("===================apsInit ok \n");
    else
    {
         printf("===================apsInit failed \n");
         return 0;
    }

    apsStart();
    // apsRegisterMsg();
    while(1)
    {
        // gApstMsg.waitMsg();
        sleep(1);
    }
//    apsUnRegisterMsg();
}
