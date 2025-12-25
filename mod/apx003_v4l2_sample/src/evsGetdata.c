#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <semaphore.h>
#include <stdint.h>
#include "apsGetdata.h"
#include <pthread.h>

// #define PRINT_TIME_INTERVAL

#define DVS_DEV_NAME  ("/dev/video1")
#define DVS_IMG_WIDTH  (4096)
#define DVS_IMG_HEIGHT (256)
#define DVS_PIXEL_FMT  (V4L2_PIX_FMT_SBGGR8)

static struct buffer *dvs_usr_buf = NULL;
int dvs_fd = -1;

#define BUF_NUM (2)

#define ERR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define FMT_NUM_PLANES 1

#define BUFFER_COUNT 4

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

#define USEC_PER_SEC    1000000L

static inline unsigned long app_timeval_to_us(const struct timeval *tv)
{
    return ((unsigned long)tv->tv_sec * USEC_PER_SEC) + tv->tv_usec;
}

/*
** dvs frame rate is not 30fps, so don't define FRAME_RATE_CHECK
*/
int get_frame(int video_fd)
{
    struct v4l2_buffer buf;
    int i, bytesused;

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

    i = buf.index;

#ifdef PRINT_TIME_INTERVAL
    static unsigned long prev_sys_timestamp_us = 0;
    unsigned long sys_timestamp_us = 0;

    sys_timestamp_us = (unsigned long)(app_timeval_to_us(&buf.timestamp));
    unsigned long ts_diff = sys_timestamp_us - prev_sys_timestamp_us;
    printf("ts_diff:%ld us\n", ts_diff);

    prev_sys_timestamp_us = sys_timestamp_us;
#endif

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type)
        bytesused = buf.m.planes[0].bytesused;
    else
        bytesused = buf.bytesused;


    if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF"); 

    return buf.index;
}

int dvsInit(void)
{
    dvs_fd = camera_init(DVS_DEV_NAME, DVS_IMG_WIDTH, DVS_IMG_HEIGHT, DVS_PIXEL_FMT);
    if (dvs_fd < 0) {
        perror("camera_init");
        return -1;
    }
	dvs_usr_buf = mmap_buffer(dvs_fd);
    if (dvs_usr_buf == NULL) {
        printf("dvs mmap failed\n");
        return 1;
    }
	stream_on(dvs_fd);
    return 0;
}
void dvsDeinit(void)
{
    stream_off(dvs_fd);
	unmap_buffer(dvs_usr_buf);
	release_camera(dvs_fd);
}

void *dvsPthread(void *arg)
{
    int dvs_frame_idx,ret;
    char *papsdata = NULL;
    unsigned int DVS_DATA_LEN = DVS_IMG_WIDTH*DVS_IMG_HEIGHT ;

    int fd = shm_open("/dvsdatashm", O_CREAT | O_RDWR, 0777);
    if (fd < 0) {
        perror("open");
    }
    ftruncate(fd,DVS_DATA_LEN);

    #ifdef PRINT_TIME_INTERVAL
    struct timeval t1, t2, t3;
    unsigned long timeuse_us;
    #endif

    sem_t *wait_dvs_sem     = sem_open("/wait_dvs_sem", O_CREAT|O_RDWR, 0666, 1); //信号量值为 1
    sem_t *send_donedvs_sem = sem_open("/send_donedvs_sem", O_CREAT|O_RDWR, 0666, 0); //信号量值为 0
    papsdata = (char *)mmap(NULL, DVS_DATA_LEN + 64, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (papsdata== MAP_FAILED)
	{
		perror("mmap");
        return NULL;            // 直接退出线程
	}
    
    while(1)
    {
        #ifdef PRINT_TIME_INTERVAL
        gettimeofday(&t1, NULL);  // 初始化 t1
        gettimeofday(&t2, NULL);
        timeuse_us = 1000000 * (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec);
        printf("select:%ld us\n", timeuse_us);
        #endif

        dvs_frame_idx = get_frame(dvs_fd);
        if (dvs_frame_idx < 0 || dvs_frame_idx >= n_buffers) {
            ERR("Invalid frame index %d, n_buffers=%u\n", dvs_frame_idx, n_buffers);
            continue;
        }
        if (!dvs_usr_buf || !dvs_usr_buf[dvs_frame_idx].start) {
            ERR("Buffer not ready\n");
            continue;
        }
        #ifdef PRINT_TIME_INTERVAL
        gettimeofday(&t3, NULL);
        timeuse_us = 1000000 * (t3.tv_sec - t2.tv_sec) + (t3.tv_usec - t2.tv_usec);
        printf("get_frame:%ld us\n", timeuse_us);
        #endif
         
         memcpy(papsdata,dvs_usr_buf[dvs_frame_idx].start,dvs_usr_buf[dvs_frame_idx].length);
         sem_post(wait_dvs_sem);
        
        /*超时时间宏: s*/
        #define DIAG_TIMEOUT 3
        
        struct timespec out_time;
        struct timeval now;

        // 开始进行超时等待
        gettimeofday(&now, NULL);
        out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
        out_time.tv_nsec = now.tv_usec * 1000;
        if(sem_timedwait(send_donedvs_sem,&out_time) < 0)
        {
           printf("fun:%s dvs timeout send_donedvs_sem\n",__func__);
        }
       
    }
    dvsDeinit();

    sem_close(wait_dvs_sem);
    sem_close(send_donedvs_sem);

    sem_unlink("/wait_dvs_sem"); 
    sem_unlink("/send_donedvs_sem"); 
}

void dvsStart(void)
{

    pthread_t pid;
    pthread_create(&pid, NULL, dvsPthread,  NULL);

}

int main(int argc, char *argv[])
{
    int  ret = dvsInit();
    if(ret == 0 )
        printf("===================dvsInit ok \n");
    else
    {
         printf("===================dvsInit failed \n");
         return 0;
    }

    dvsStart();
    while(1)
    {
        sleep(1);
    }
}