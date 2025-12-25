/**
 * @file evsGetdata_multithread.c
 * @brief EVS多线程采集、编码和传输（先判断再提取，可配置时间窗口）
 */

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
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

#include "apsGetdata.h"
#include "evs_event_extractor.h"
#include "evs_tcp_sender.h"
#include "time_window.h"
#include "thread_safe_queue.h"
#include "evt2_encoder.h"
#include "encoded_packet.h"

// ============================================================================
// 配置参数
// ============================================================================

#define DVS_DEV_NAME  "/dev/video1"
#define DVS_IMG_WIDTH  4096
#define DVS_IMG_HEIGHT 256
#define DVS_PIXEL_FMT  V4L2_PIX_FMT_SBGGR8

#define BUFFER_COUNT 4
#define MAX_EVENTS_PER_SUBFRAME (384 * 304)

// TCP服务器配置
#define TCP_SERVER_IP   "192.168.1.100"
#define TCP_SERVER_PORT 8888
#define DEVICE_ID       1

// 默认时间窗口大小（毫秒）
#define DEFAULT_WINDOW_SIZE_MS 20

// 队列容量
#define V4L2_FRAME_QUEUE_SIZE 8
#define ENCODING_QUEUE_SIZE 20
#define TRANSMISSION_QUEUE_SIZE 50

// 编码线程数
#define NUM_ENCODING_THREADS 2

// 队列模式配置：0=非阻塞(高吞吐,可能丢帧) 1=阻塞(无丢失,可能影响V4L2)
#define V4L2_QUEUE_MODE_BLOCKING 0
#define ENCODING_QUEUE_MODE_BLOCKING 1
#define TRANSMISSION_QUEUE_MODE_BLOCKING 1

// 阻塞模式超时（毫秒）
#define QUEUE_PUSH_TIMEOUT_MS 100

// 统计输出间隔
#define STATS_PRINT_INTERVAL 100

// ============================================================================
// 原始帧缓冲区结构（用于V4L2采集队列）
// ============================================================================

typedef struct {
    uint8_t* data;             // 帧数据拷贝
    size_t data_size;          // 数据大小
    uint32_t frame_index;      // 帧索引
    struct timeval timestamp;  // 采集时间戳
} RawFrameBuffer_t;

// ============================================================================
// 子帧信息结构
// ============================================================================

typedef struct {
    int physical_index;        // 物理位置（0-3）
    int subframe_id;           // 子帧ID（0-3）
    uint64_t timestamp;        // 时间戳（微秒）
    const uint8_t* data_ptr;   // 数据指针
} SubframeInfo_t;

// ============================================================================
// 统计信息结构
// ============================================================================

typedef struct {
    // 帧统计
    uint64_t total_frames_captured;
    uint64_t total_subframes_processed;
    
    // 窗口统计
    uint64_t windows_generated;
    uint64_t windows_time_completed;           // 时间触发完成的窗口数
    uint64_t windows_force_completed;          // 空间不足强制完成的窗口数
    uint64_t windows_dropped_encoding_full;
    uint64_t windows_dropped_transmission_full;
    
    // 事件统计
    uint64_t total_events_extracted;
    uint64_t total_events_sent;
    uint64_t events_dropped_buffer_full;       // 缓冲区满导致的事件丢失
    
    // 编码统计
    uint64_t total_events_encoded;             // 编码的事件总数
    uint64_t total_bytes_before_encoding;      // 编码前字节数
    uint64_t total_bytes_after_encoding;       // 编码后字节数
    
    // 传输统计
    uint64_t tcp_send_failures;
    uint64_t tcp_reconnections;
    
    // 性能统计
    uint64_t max_encoding_queue_size;
    uint64_t max_transmission_queue_size;
    
    // 完整性验证
    uint64_t total_subframes_seen;             // 看到的子帧总数
    uint64_t total_subframes_extracted;        // 成功提取的子帧总数
    
    // V4L2采集统计
    uint64_t v4l2_frames_captured;             // V4L2实际采集的帧数（连续序号）
    uint64_t v4l2_frames_dropped;              // V4L2队列满丢弃的帧数
    uint64_t max_v4l2_queue_size;              // V4L2队列峰值大小
    uint64_t v4l2_queue_push_timeouts;         // V4L2队列推送超时次数
    
    pthread_mutex_t mutex;
} GlobalStats_t;

// 内存池已移除：改用直接提取到时间窗口缓冲区（零拷贝）

// ============================================================================
// 全局变量
// ============================================================================

static struct buffer *dvs_usr_buf = NULL;
static int dvs_fd = -1;
static unsigned int n_buffers = 0;
static enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
static volatile bool g_running = true;

// 时间窗口累积器
static TimeWindowAccumulator_t* g_time_window = NULL;
static pthread_mutex_t g_window_mutex = PTHREAD_MUTEX_INITIALIZER;

// 队列
static ThreadSafeQueue_t* g_v4l2_frame_queue = NULL;   // V4L2原始帧队列
static ThreadSafeQueue_t* g_encoding_queue = NULL;
static ThreadSafeQueue_t* g_transmission_queue = NULL;

// TCP发送器
static EVSTCPSender_t* g_tcp_sender = NULL;

// 线程
static pthread_t g_v4l2_acquisition_thread;  // V4L2采集线程
static pthread_t g_extraction_thread;         // 事件提取线程
static pthread_t g_encoding_threads[NUM_ENCODING_THREADS];
static pthread_t g_transmission_thread;

// 统计信息
static GlobalStats_t g_stats;

// ============================================================================
// 统计信息函数
// ============================================================================

static void stats_init(GlobalStats_t* stats)
{
    memset(stats, 0, sizeof(GlobalStats_t));
    pthread_mutex_init(&stats->mutex, NULL);
}

static void stats_print(const GlobalStats_t* stats)
{
    pthread_mutex_lock((pthread_mutex_t*)&stats->mutex);
    
    printf("========================================\n");
    printf("V4L2 Acquisition:\n");
    printf("  V4L2 Frames Captured: %lu\n", stats->v4l2_frames_captured);
    printf("  Total Frames Processed: %lu\n", stats->total_frames_captured);
    printf("  Frames Dropped (Queue Full): %lu\n", stats->v4l2_frames_dropped);
    printf("  Queue Push Timeouts: %lu\n", stats->v4l2_queue_push_timeouts);
    printf("  V4L2 Queue Peak: %lu/%d\n", stats->max_v4l2_queue_size, V4L2_FRAME_QUEUE_SIZE);
    
    // 数据完整性检查
    uint64_t total_v4l2_frames = stats->v4l2_frames_captured;
    uint64_t processed_frames = stats->total_frames_captured;
    uint64_t lost_frames = stats->v4l2_frames_dropped + stats->v4l2_queue_push_timeouts;
    if (total_v4l2_frames == processed_frames + lost_frames) {
        printf("  Integrity: ✓ VERIFIED (%lu captured = %lu processed + %lu lost)\n",
               total_v4l2_frames, processed_frames, lost_frames);
    } else {
        printf("  Integrity: ✗ MISMATCH (captured %lu != processed %lu + lost %lu)\n",
               total_v4l2_frames, processed_frames, lost_frames);
    }
    
    printf("\nFrames & Subframes:\n");
    printf("  Total Subframes: %lu\n", stats->total_subframes_processed);
    
    printf("\nWindows:\n");
    printf("  Total Generated: %lu\n", stats->windows_generated);
    printf("  Time-Completed: %lu (%.1f%%)\n",
           stats->windows_time_completed,
           100.0 * stats->windows_time_completed / (stats->windows_generated + 1));
    printf("  Force-Completed: %lu (%.1f%%)\n",
           stats->windows_force_completed,
           100.0 * stats->windows_force_completed / (stats->windows_generated + 1));
    printf("  Dropped (Encoding): %lu\n", stats->windows_dropped_encoding_full);
    printf("  Dropped (Transmission): %lu\n", stats->windows_dropped_transmission_full);
    
    printf("\nEvents:\n");
    printf("  Extracted: %lu\n", stats->total_events_extracted);
    printf("  Dropped (Buffer Full): %lu\n", stats->events_dropped_buffer_full);
    printf("  Encoded: %lu\n", stats->total_events_encoded);
    printf("  Sent: %lu\n", stats->total_events_sent);
    if (stats->events_dropped_buffer_full > 0) {
        printf("  ⚠ WARNING: %lu events dropped due to buffer overflow!\n", 
               stats->events_dropped_buffer_full);
    }
    
    printf("\nEncoding Compression:\n");
    if (stats->total_events_encoded > 0) {
        printf("  Before: %lu bytes (%.2f MB)\n", 
               stats->total_bytes_before_encoding,
               stats->total_bytes_before_encoding / (1024.0 * 1024.0));
        printf("  After:  %lu bytes (%.2f MB)\n",
               stats->total_bytes_after_encoding,
               stats->total_bytes_after_encoding / (1024.0 * 1024.0));
        double ratio = 100.0 * (1.0 - (double)stats->total_bytes_after_encoding / stats->total_bytes_before_encoding);
        printf("  Ratio:  %.1f%% compression\n", ratio);
        printf("  Avg:    %.2f bytes/event\n",
               (double)stats->total_bytes_after_encoding / stats->total_events_encoded);
    } else {
        printf("  No encoding data yet\n");
    }
    
    printf("\nData Integrity:\n");
    printf("  Subframes Seen: %lu\n", stats->total_subframes_seen);
    printf("  Subframes Extracted: %lu\n", stats->total_subframes_extracted);
    if (stats->total_subframes_seen == stats->total_subframes_extracted) {
        printf("  Status: ✓ NO DATA LOSS\n");
    } else {
        printf("  Status: ✗ LOSS DETECTED (%lu subframes)\n",
               stats->total_subframes_seen - stats->total_subframes_extracted);
    }
    
    printf("\nTransmission:\n");
    printf("  TCP Failures: %lu\n", stats->tcp_send_failures);
    printf("  TCP Reconnections: %lu\n", stats->tcp_reconnections);
    
    printf("\nQueue Peak Usage:\n");
    printf("  Encoding: %lu\n", stats->max_encoding_queue_size);
    printf("  Transmission: %lu\n", stats->max_transmission_queue_size);
    printf("========================================\n");
    
    pthread_mutex_unlock((pthread_mutex_t*)&stats->mutex);
}

// ============================================================================
// 原始帧缓冲区管理函数
// ============================================================================

static RawFrameBuffer_t* raw_frame_buffer_create(size_t data_size)
{
    RawFrameBuffer_t* buffer = (RawFrameBuffer_t*)malloc(sizeof(RawFrameBuffer_t));
    if (!buffer) {
        return NULL;
    }
    
    buffer->data = (uint8_t*)malloc(data_size);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }
    
    buffer->data_size = data_size;
    buffer->frame_index = 0;
    memset(&buffer->timestamp, 0, sizeof(buffer->timestamp));
    
    return buffer;
}

static void raw_frame_buffer_destroy(RawFrameBuffer_t* buffer)
{
    if (!buffer) return;
    
    if (buffer->data) {
        free(buffer->data);
    }
    free(buffer);
}

// ============================================================================
// V4L2辅助函数
// ============================================================================

#define ERR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define FMT_NUM_PLANES 1

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

static void init_device(int fd, int width, int height, int format)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            ERR("%s is no V4L2 device\n", __func__);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        ERR("%s is not a video capture device\n", __func__);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        ERR("%s does not support streaming i/o\n", __func__);
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

    printf("[V4L2] Format set: %dx%d, fmt=0x%X\n", width, height, format);
}

static struct buffer* mmap_buffer(int fd)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.count = BUFFER_COUNT;
    req.type = buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            ERR("Device does not support memory mapping\n");
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        ERR("Insufficient buffer memory\n");
        exit(EXIT_FAILURE);
    }

    struct buffer* buffers = (struct buffer*)calloc(req.count, sizeof(*buffers));
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
            buffers[n_buffers].start = mmap(NULL, buf.m.planes[0].length,
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           fd, buf.m.planes[0].m.mem_offset);
        } else {
            buffers[n_buffers].length = buf.length;
            buffers[n_buffers].start = mmap(NULL, buf.length,
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           fd, buf.m.offset);
        }

        if (MAP_FAILED == buffers[n_buffers].start)
            errno_exit("mmap");
    }
    
    buffers->fd = fd;
    printf("[V4L2] Mapped %d buffers\n", n_buffers);
    
    return buffers;
}

static int stream_on(int video_fd)
{
    for (unsigned int i = 0; i < n_buffers; ++i) {
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

    enum v4l2_buf_type type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");

    printf("[V4L2] Stream started\n");
    return 0;
}

static int stream_off(int video_fd)
{
    enum v4l2_buf_type type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMOFF, &type))
        errno_exit("VIDIOC_STREAMOFF");
    return 0;
}

static int get_frame(int video_fd)
{
    struct v4l2_buffer buf;
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

    int index = buf.index;

    if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF");

    return index;
}

// ============================================================================
// 子帧解析函数
// ============================================================================

/**
 * @brief qsort比较函数：按时间戳排序
 */
static int compare_subframe_timestamp(const void* a, const void* b)
{
    const SubframeInfo_t* sf_a = (const SubframeInfo_t*)a;
    const SubframeInfo_t* sf_b = (const SubframeInfo_t*)b;
    
    if (sf_a->timestamp < sf_b->timestamp) return -1;
    if (sf_a->timestamp > sf_b->timestamp) return 1;
    return 0;
}

/**
 * @brief 解析所有子帧头部（不提取事件）
 * 
 * 物理位置说明：
 * - physical_index: 子帧在V4L2缓冲区中的物理位置（0-31）
 * - subframe_id: 子帧在传感器输出中的逻辑ID
 * - 传感器按顺序输出32个子帧
 * - V4L2缓冲区按物理位置存储
 * - 按timestamp排序后处理以确保时间窗口正确
 */
static void parse_subframe_headers(const uint8_t* raw_data, SubframeInfo_t subframes[32])
{
    const uint8_t* ptr = raw_data;
    
    // 解析所有32个子帧的头部
    for (int i = 0; i < 32; i++) {
        const uint64_t* pixel_ptr = (const uint64_t*)ptr;
        
        // 解析时间戳
        uint64_t buffer = pixel_ptr[0];
        uint64_t timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
        timestamp /= 200;  // 转换为微秒
        
        // 解析子帧ID
        buffer = pixel_ptr[1];
        uint64_t subframe_id = (buffer >> 44) & 0xF;
        
        subframes[i].physical_index = i;
        subframes[i].subframe_id = subframe_id;
        subframes[i].timestamp = timestamp;
        subframes[i].data_ptr = ptr;
        
        ptr += HV_SUB_FULL_BYTE_SIZE;  // +32KB
    }
    
    // 按时间戳排序（传感器输出顺序）
    // 使用qsort，因为有32个元素
    qsort(subframes, 32, sizeof(SubframeInfo_t), compare_subframe_timestamp);
}

/**
 * @brief 提取单个子帧的事件并直接写入时间窗口缓冲区（零拷贝）
 */
static int extract_and_accumulate_subframe(const uint8_t* subframe_data,
                                           int subframe_id,
                                           TimeWindowAccumulator_t* accum)
{
    EventWindowBuffer_t* window = accum->current_window;
    if (!window) {
        fprintf(stderr, "[Extraction] Error: No current window\n");
        return -1;
    }
    
    // 检查窗口缓冲区是否还有空间
    if (window->event_count >= window->max_events) {
        fprintf(stderr, "[Extraction] Warning: Window buffer full (%u/%u events)\n",
               window->event_count, window->max_events);
        return 0;
    }
    
    // 直接提取到时间窗口缓冲区（零拷贝）
    uint32_t dropped = 0;
    int extracted = evs_extract_subframe_direct(
        subframe_data,
        subframe_id,
        window->events,
        &window->event_count,
        window->max_events,
        &dropped
    );
    
    if (extracted > 0 || dropped > 0) {
        // 更新统计
        pthread_mutex_lock(&g_stats.mutex);
        g_stats.total_events_extracted += extracted;
        if (dropped > 0) {
            g_stats.events_dropped_buffer_full += dropped;
        }
        pthread_mutex_unlock(&g_stats.mutex);
        
        // 更新图像缓冲区（如果需要可视化）
        for (uint32_t i = window->event_count - extracted; i < window->event_count; i++) {
            EVSEvent_t* event = &window->events[i];
            if (event->x < window->width && event->y < window->height) {
                int idx = event->y * window->width + event->x;
                window->frame_buffer[idx] = (event->polarity > 0) ? 255 : 128;
            }
        }
        
        // 初始化时间窗口（使用第一个事件的时间戳）
        if (!accum->window_initialized && window->event_count > 0) {
            uint64_t first_timestamp = window->events[0].timestamp;
            accum->window_start_timestamp = first_timestamp;
            accum->window_end_timestamp = first_timestamp + accum->window_size_us;
            accum->window_initialized = true;
            
            window->window_start_timestamp = accum->window_start_timestamp;
            window->window_end_timestamp = accum->window_end_timestamp;
            window->window_id = 0;
            
            printf("[TimeWindow #0] Initialized: [%lu, %lu] us\n",
                   accum->window_start_timestamp,
                   accum->window_end_timestamp);
        }
    }
    
    return extracted;
}

// ============================================================================
// 信号处理
// ============================================================================

static void signal_handler(int sig)
{
    printf("\n[Main] Received signal %d, shutting down...\n", sig);
    g_running = false;
}

// ============================================================================
// 线程1：V4L2采集线程（高速采集，最小化延迟）
// ============================================================================

void* v4l2_acquisition_thread_func(void* arg)
{
    printf("[V4L2 Acquisition Thread] Started\n");
    
    uint32_t frame_count = 0;
    const size_t frame_size = DVS_IMG_WIDTH * DVS_IMG_HEIGHT;
    
    while (g_running) {
        // 1. 快速获取V4L2帧
        int frame_idx = get_frame(dvs_fd);
        if (frame_idx < 0 || frame_idx >= (int)n_buffers) {
            continue;
        }
        
        const uint8_t* raw_data = (const uint8_t*)dvs_usr_buf[frame_idx].start;
        
        // 2. 创建帧缓冲区并拷贝数据（快速拷贝后立即释放V4L2缓冲区）
        RawFrameBuffer_t* frame_buffer = raw_frame_buffer_create(frame_size);
        if (!frame_buffer) {
            fprintf(stderr, "[V4L2] Failed to allocate frame buffer\n");
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.v4l2_frames_dropped++;
            pthread_mutex_unlock(&g_stats.mutex);
            continue;
        }
        
        memcpy(frame_buffer->data, raw_data, frame_size);
        frame_buffer->frame_index = frame_count;
        gettimeofday(&frame_buffer->timestamp, NULL);
        
        // 记录V4L2采集帧数（用于完整性验证）
        pthread_mutex_lock(&g_stats.mutex);
        g_stats.v4l2_frames_captured++;
        pthread_mutex_unlock(&g_stats.mutex);
        
        // 3. 推送到队列（根据配置选择阻塞/非阻塞模式）
        int push_result = -1;
        
#if V4L2_QUEUE_MODE_BLOCKING
        // 阻塞模式：等待队列有空间，确保不丢数据
        push_result = queue_try_push(g_v4l2_frame_queue, frame_buffer, QUEUE_PUSH_TIMEOUT_MS);
        if (push_result < 0) {
            fprintf(stderr, "[V4L2] Queue push timeout after %dms, frame %u\n", 
                   QUEUE_PUSH_TIMEOUT_MS, frame_count);
            raw_frame_buffer_destroy(frame_buffer);
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.v4l2_queue_push_timeouts++;
            pthread_mutex_unlock(&g_stats.mutex);
        }
#else
        // 非阻塞模式：立即返回，队列满则丢弃
        push_result = queue_try_push(g_v4l2_frame_queue, frame_buffer, 0);
        if (push_result < 0) {
            fprintf(stderr, "[V4L2] Queue full, dropping frame %u\n", frame_count);
            raw_frame_buffer_destroy(frame_buffer);
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.v4l2_frames_dropped++;
            pthread_mutex_unlock(&g_stats.mutex);
        }
#endif
        
        if (push_result >= 0) {
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_frames_captured++;
            uint32_t queue_size = queue_size_get(g_v4l2_frame_queue);
            if (queue_size > g_stats.max_v4l2_queue_size) {
                g_stats.max_v4l2_queue_size = queue_size;
            }
            pthread_mutex_unlock(&g_stats.mutex);
        }
        
        frame_count++;
    }
    
    printf("[V4L2 Acquisition Thread] Exiting, total frames: %u\n", frame_count);
    return NULL;
}

// ============================================================================
// 线程2：事件提取线程（从队列读取并处理）
// ============================================================================

void* extraction_thread_func(void* arg)
{
    printf("[Extraction Thread] Started\n");
    
    int processed_count = 0;
    
    while (g_running) {
        // 1. 从队列获取原始帧（阻塞等待）
        RawFrameBuffer_t* frame_buffer = (RawFrameBuffer_t*)queue_pop(g_v4l2_frame_queue);
        if (!frame_buffer) {
            // 队列已shutdown且为空
            break;
        }
        
        const uint8_t* raw_data = frame_buffer->data;
        
        // 2. 解析所有32个子帧头部并按时间戳排序
        SubframeInfo_t subframes[32];
        parse_subframe_headers(raw_data, subframes);
        
        pthread_mutex_lock(&g_window_mutex);
        
        // 3. 按时间戳顺序处理所有32个子帧（确保时间窗口判断正确）
        for (int i = 0; i < 32; i++) {
            SubframeInfo_t* sf = &subframes[i];
            
            // 统计：记录看到的子帧
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_subframes_seen++;
            pthread_mutex_unlock(&g_stats.mutex);
            
            // 4a. 空间检查：如果缓冲区空间不足，强制完成窗口
            EventWindowBuffer_t* window = g_time_window->current_window;
            uint32_t available = window->max_events - window->event_count;
            uint32_t estimated = MAX_EVENTS_PER_SUBFRAME;  // 116K events
            
            if (available < estimated) {
                fprintf(stderr, "[ForceComplete] Space insufficient: available=%u < estimated=%u\n",
                       available, estimated);
                
                EventWindowBuffer_t* completed_window = time_window_force_complete(g_time_window);
                
                if (completed_window) {
                    pthread_mutex_lock(&g_stats.mutex);
                    g_stats.windows_generated++;
                    g_stats.windows_force_completed++;  // 强制完成统计
                    uint32_t queue_size = queue_size_get(g_encoding_queue);
                    if (queue_size > g_stats.max_encoding_queue_size) {
                        g_stats.max_encoding_queue_size = queue_size;
                    }
                    pthread_mutex_unlock(&g_stats.mutex);
                    
                    // 推送到编码队列
#if ENCODING_QUEUE_MODE_BLOCKING
                    if (queue_push(g_encoding_queue, completed_window) < 0) {
#else
                    if (queue_try_push(g_encoding_queue, completed_window, 0) < 0) {
#endif
                        fprintf(stderr, "[Extraction] Queue shutdown/full, dropping force-completed window #%u\n",
                               completed_window->window_id);
                        event_window_buffer_destroy(completed_window);
                        
                        pthread_mutex_lock(&g_stats.mutex);
                        g_stats.windows_dropped_encoding_full++;
                        pthread_mutex_unlock(&g_stats.mutex);
                    }
                }
            }
            
            // 4b. 时间检查：子帧是否会触发窗口完成
            // 注意：time_window_will_complete内部会处理窗口初始化
            if (time_window_will_complete(g_time_window, sf->timestamp)) {
                // 窗口将完成，先完成当前窗口
                EventWindowBuffer_t* completed_window = time_window_complete(g_time_window);
                
                if (completed_window) {
                    // 更新统计
                    pthread_mutex_lock(&g_stats.mutex);
                    g_stats.windows_generated++;
                    g_stats.windows_time_completed++;  // 时间触发完成统计
                    uint32_t queue_size = queue_size_get(g_encoding_queue);
                    if (queue_size > g_stats.max_encoding_queue_size) {
                        g_stats.max_encoding_queue_size = queue_size;
                    }
                    pthread_mutex_unlock(&g_stats.mutex);
                    
                    // 尝试放入编码队列
#if ENCODING_QUEUE_MODE_BLOCKING
                    if (queue_push(g_encoding_queue, completed_window) < 0) {
#else
                    if (queue_try_push(g_encoding_queue, completed_window, 0) < 0) {
#endif
                        fprintf(stderr, "[Extraction] Queue shutdown/full, dropping window #%u\n",
                               completed_window->window_id);
                        event_window_buffer_destroy(completed_window);
                        
                        pthread_mutex_lock(&g_stats.mutex);
                        g_stats.windows_dropped_encoding_full++;
                        pthread_mutex_unlock(&g_stats.mutex);
                    }
                }
            }
            
            // 5. 提取并累积这个子帧的事件到当前窗口
            // 关键：无论是否强制完成，当前子帧都会被提取，不会丢失！
            int extracted = extract_and_accumulate_subframe(sf->data_ptr, sf->subframe_id, g_time_window);
            
            // 更新统计
            if (extracted >= 0) {
                g_time_window->current_window->subframes_in_window++;
                g_time_window->total_subframes_processed++;
                
                // 完整性验证：记录成功提取的子帧
                pthread_mutex_lock(&g_stats.mutex);
                g_stats.total_subframes_extracted++;
                g_stats.total_subframes_processed++;
                pthread_mutex_unlock(&g_stats.mutex);
            } else {
                fprintf(stderr, "[Extraction] WARNING: Failed to extract subframe %d\n", i);
            }
        }
        
        g_time_window->current_window->frames_in_window++;
        
        pthread_mutex_unlock(&g_window_mutex);
        
        // 释放帧缓冲区
        raw_frame_buffer_destroy(frame_buffer);
        
        processed_count++;
        
        // 定期打印统计
        if (processed_count % STATS_PRINT_INTERVAL == 0) {
            printf("\n========== Processed %d frames ==========\n", processed_count);
            pthread_mutex_lock(&g_window_mutex);
            time_window_print_stats(g_time_window);
            pthread_mutex_unlock(&g_window_mutex);
            stats_print(&g_stats);
        }
    }
    
    printf("[Extraction Thread] Exiting, total processed: %d\n", processed_count);
    return NULL;
}

// ============================================================================
// 线程2：编码线程（多个）
// ============================================================================

void* encoding_thread_func(void* arg)
{
    int thread_id = *(int*)arg;
    printf("[Encoding Thread %d] Started\n", thread_id);
    
    // 创建EVT2编码器（初始缓冲区 5MB）
    EVT2Encoder_t* encoder = evt2_encoder_create(5 * 1024 * 1024);
    if (!encoder) {
        fprintf(stderr, "[Encoding Thread %d] Failed to create EVT2 encoder\n", thread_id);
        return NULL;
    }
    
    while (true) {
        // 从队列取出窗口数据（阻塞等待）
        EventWindowBuffer_t* window = (EventWindowBuffer_t*)queue_pop(g_encoding_queue);
        if (!window) {
            // 队列已shutdown且为空，退出
            break;
        }
        
        printf("[Encoding Thread %d] Processing window #%u (%u events)\n",
               thread_id, window->window_id, window->event_count);
        
        // ===== EVT2编码 =====
        const uint8_t* encoded_data = NULL;
        size_t encoded_size = 0;
        
        int ret = evt2_encoder_encode(
            encoder,
            window->events,
            window->event_count,
            window->window_start_timestamp,
            &encoded_data,
            &encoded_size
        );
        
        if (ret == 0 && encoded_size > 0) {
            // 计算原始大小和压缩率
            size_t original_size = window->event_count * sizeof(EVSEvent_t);
            double compression = 100.0 * (1.0 - (double)encoded_size / original_size);
            
            printf("[Encoding Thread %d] Encoded window #%u: %u events → %zu bytes (%.1f%% compression)\n",
                   thread_id, window->window_id, window->event_count, 
                   encoded_size, compression);
            
            // 更新编码统计
            pthread_mutex_lock(&g_stats.mutex);
            g_stats.total_events_encoded += window->event_count;
            g_stats.total_bytes_before_encoding += original_size;
            g_stats.total_bytes_after_encoding += encoded_size;
            pthread_mutex_unlock(&g_stats.mutex);
            
            // 创建编码数据包
            EncodedWindowPacket_t* encoded_packet = encoded_packet_create(
                window->window_id,
                window->window_start_timestamp,
                window->window_end_timestamp,
                window->event_count,
                encoded_data,
                encoded_size,
                window->subframes_in_window,
                window->frames_in_window
            );
            
            if (encoded_packet) {
                // 更新传输队列统计
                pthread_mutex_lock(&g_stats.mutex);
                uint32_t queue_size = queue_size_get(g_transmission_queue);
                if (queue_size > g_stats.max_transmission_queue_size) {
                    g_stats.max_transmission_queue_size = queue_size;
                }
                pthread_mutex_unlock(&g_stats.mutex);
                
                // 推送编码数据包到传输队列
#if TRANSMISSION_QUEUE_MODE_BLOCKING
                if (queue_push(g_transmission_queue, encoded_packet) < 0) {
#else
                if (queue_try_push(g_transmission_queue, encoded_packet, 0) < 0) {
#endif
                    fprintf(stderr, "[Encoding Thread %d] Transmission queue shutdown/full, dropping encoded window #%u\n",
                           thread_id, window->window_id);
                    encoded_packet_destroy(encoded_packet);
                    
                    pthread_mutex_lock(&g_stats.mutex);
                    g_stats.windows_dropped_transmission_full++;
                    pthread_mutex_unlock(&g_stats.mutex);
                }
            } else {
                fprintf(stderr, "[Encoding Thread %d] Failed to create encoded packet for window #%u\n",
                       thread_id, window->window_id);
            }
            
            // 释放原始窗口
            event_window_buffer_destroy(window);
        } else {
            fprintf(stderr, "[Encoding Thread %d] Failed to encode window #%u\n",
                   thread_id, window->window_id);
            event_window_buffer_destroy(window);
        }
    }
    
    // 打印编码器统计
    printf("[Encoding Thread %d] Final statistics:\n", thread_id);
    evt2_encoder_print_stats(encoder);
    
    // 清理
    evt2_encoder_destroy(encoder);
    
    printf("[Encoding Thread %d] Exiting\n", thread_id);
    return NULL;
}

// ============================================================================
// 线程3：传输线程
// ============================================================================

void* transmission_thread_func(void* arg)
{
    printf("[Transmission Thread] Started\n");
    
    // 初始连接TCP服务器
    int initial_reconnect_attempts = 0;
    const int MAX_INITIAL_RECONNECT_ATTEMPTS = 5;
    
    while (g_running && initial_reconnect_attempts < MAX_INITIAL_RECONNECT_ATTEMPTS) {
        if (evs_tcp_sender_connect(g_tcp_sender) == 0) {
            printf("[Transmission Thread] Connected to server\n");
            break;
        }
        
        initial_reconnect_attempts++;
        printf("[Transmission Thread] Initial connection failed, retry %d/%d...\n",
               initial_reconnect_attempts, MAX_INITIAL_RECONNECT_ATTEMPTS);
        
        pthread_mutex_lock(&g_stats.mutex);
        g_stats.tcp_reconnections++;
        pthread_mutex_unlock(&g_stats.mutex);
        
        sleep(3);
    }
    
    if (!evs_tcp_sender_is_connected(g_tcp_sender)) {
        ERR("[Transmission Thread] Failed to connect after %d attempts\n", MAX_INITIAL_RECONNECT_ATTEMPTS);
        return NULL;
    }
    
    // 简化退出逻辑
    while (true) {
        // 从队列取出编码数据包（阻塞等待）
        EncodedWindowPacket_t* encoded_packet = (EncodedWindowPacket_t*)queue_pop(g_transmission_queue);
        if (!encoded_packet) {
            // 队列已shutdown且为空，退出
            break;
        }
        
        printf("[Transmission] Sending encoded window #%u (%u events, %zu bytes EVT2)\n",
               encoded_packet->window_id, encoded_packet->original_event_count,
               encoded_packet->encoded_data_size);
        
        // 发送EVT2编码数据（带重试机制）
        bool sent_success = false;
        const int MAX_SEND_RETRIES = 3;
        
        if (encoded_packet->encoded_data_size > 0) {
            for (int retry = 0; retry < MAX_SEND_RETRIES; retry++) {
                int sent = evs_tcp_sender_send_evt2_data(
                    g_tcp_sender,
                    encoded_packet->encoded_data,
                    encoded_packet->encoded_data_size,
                    encoded_packet->original_event_count
                );
                
                if (sent >= 0) {
                    sent_success = true;
                    
                    // 更新统计
                    pthread_mutex_lock(&g_stats.mutex);
                    g_stats.total_events_sent += encoded_packet->original_event_count;
                    pthread_mutex_unlock(&g_stats.mutex);
                    
                    break;
                }
                
                // 发送失败，尝试重连
                fprintf(stderr, "[Transmission] Send failed for window #%u, retry %d/%d\n",
                       encoded_packet->window_id, retry + 1, MAX_SEND_RETRIES);
                
                pthread_mutex_lock(&g_stats.mutex);
                g_stats.tcp_send_failures++;
                pthread_mutex_unlock(&g_stats.mutex);
                
                if (retry < MAX_SEND_RETRIES - 1) {
                    // 断开并重连
                    evs_tcp_sender_disconnect(g_tcp_sender);
                    sleep(1);
                    
                    if (evs_tcp_sender_connect(g_tcp_sender) == 0) {
                        printf("[Transmission] Reconnected successfully\n");
                        
                        pthread_mutex_lock(&g_stats.mutex);
                        g_stats.tcp_reconnections++;
                        pthread_mutex_unlock(&g_stats.mutex);
                    } else {
                        fprintf(stderr, "[Transmission] Reconnection failed\n");
                    }
                }
            }
            
            if (!sent_success) {
                fprintf(stderr, "[Transmission] Failed to send window #%u after %d retries, data lost\n",
                       encoded_packet->window_id, MAX_SEND_RETRIES);
            }
        }
        
        // 释放编码数据包
        encoded_packet_destroy(encoded_packet);
    }
    
    printf("[Transmission Thread] Exiting\n");
    return NULL;
}

// ============================================================================
// 初始化和清理
// ============================================================================

static int dvsInit(void)
{
    dvs_fd = open(DVS_DEV_NAME, O_RDWR, 0);
    if (dvs_fd < 0) {
        perror("open V4L2 device");
        return -1;
    }

    init_device(dvs_fd, DVS_IMG_WIDTH, DVS_IMG_HEIGHT, DVS_PIXEL_FMT);
    dvs_usr_buf = mmap_buffer(dvs_fd);
    if (dvs_usr_buf == NULL) {
        ERR("DVS mmap failed\n");
        close(dvs_fd);
        return -1;
    }

    stream_on(dvs_fd);
    printf("[DVS Init] Initialized successfully\n");
    return 0;
}

static void dvsDeinit(void)
{
    if (dvs_fd >= 0) {
        stream_off(dvs_fd);
        
        for (unsigned int i = 0; i < n_buffers; ++i) {
            if (dvs_usr_buf && dvs_usr_buf[i].start) {
                munmap(dvs_usr_buf[i].start, dvs_usr_buf[i].length);
            }
        }
        
        if (dvs_usr_buf) {
            free(dvs_usr_buf);
            dvs_usr_buf = NULL;
        }
        
        close(dvs_fd);
        dvs_fd = -1;
    }
    
    printf("[DVS Deinit] Cleaned up\n");
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[])
{
    const char* server_ip = TCP_SERVER_IP;
    int server_port = TCP_SERVER_PORT;
    uint32_t window_size_ms = DEFAULT_WINDOW_SIZE_MS;
    
    // 解析命令行参数
    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = atoi(argv[2]);
    }
    if (argc >= 4) {
        window_size_ms = atoi(argv[3]);
    }
    
    printf("========================================\n");
    printf("EVS Multi-threaded Sender\n");
    printf("Server: %s:%d\n", server_ip, server_port);
    printf("Device: %s\n", DVS_DEV_NAME);
    printf("Time Window: %u ms\n", window_size_ms);
    printf("Encoding Threads: %d\n", NUM_ENCODING_THREADS);
    printf("========================================\n\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化统计信息
    stats_init(&g_stats);
    printf("[Init] Statistics initialized\n");
    printf("[Init] Direct extraction mode: Zero-copy to time window buffer\n");
    
    // 创建时间窗口累积器
    g_time_window = time_window_accumulator_create(window_size_ms);
    if (!g_time_window) {
        ERR("Failed to create time window accumulator\n");
        return 1;
    }
    
    // 创建队列
    g_v4l2_frame_queue = queue_create(V4L2_FRAME_QUEUE_SIZE);
    g_encoding_queue = queue_create(ENCODING_QUEUE_SIZE);
    g_transmission_queue = queue_create(TRANSMISSION_QUEUE_SIZE);
    if (!g_v4l2_frame_queue || !g_encoding_queue || !g_transmission_queue) {
        ERR("Failed to create queues\n");
        return 1;
    }
    printf("[Init] Queues created: V4L2=%d, Encoding=%d, Transmission=%d\n",
           V4L2_FRAME_QUEUE_SIZE, ENCODING_QUEUE_SIZE, TRANSMISSION_QUEUE_SIZE);
    
    // 创建TCP发送器
    g_tcp_sender = evs_tcp_sender_create(server_ip, server_port, DEVICE_ID);
    if (!g_tcp_sender) {
        ERR("Failed to create TCP sender\n");
        return 1;
    }
    
    // 初始化DVS设备
    if (dvsInit() != 0) {
        ERR("DVS initialization failed\n");
        return 1;
    }
    
    // 创建线程
    printf("[Main] Starting threads...\n");
    printf("[Main] Architecture: V4L2 Acquisition -> Frame Queue -> Extraction -> Encoding -> Transmission\n");
    
    // V4L2采集线程（高优先级，快速采集）
    if (pthread_create(&g_v4l2_acquisition_thread, NULL, v4l2_acquisition_thread_func, NULL) != 0) {
        ERR("Failed to create V4L2 acquisition thread\n");
        return 1;
    }
    
    // 事件提取线程
    if (pthread_create(&g_extraction_thread, NULL, extraction_thread_func, NULL) != 0) {
        ERR("Failed to create extraction thread\n");
        return 1;
    }
    
    // 编码线程（多个）
    int thread_ids[NUM_ENCODING_THREADS];
    for (int i = 0; i < NUM_ENCODING_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&g_encoding_threads[i], NULL, encoding_thread_func, &thread_ids[i]) != 0) {
            ERR("Failed to create encoding thread %d\n", i);
            return 1;
        }
    }
    
    // 传输线程
    if (pthread_create(&g_transmission_thread, NULL, transmission_thread_func, NULL) != 0) {
        ERR("Failed to create transmission thread\n");
        return 1;
    }
    
    printf("[Main] All threads started\n");
    
    // 等待V4L2采集线程结束
    pthread_join(g_v4l2_acquisition_thread, NULL);
    
    // 关闭V4L2队列，让提取线程退出
    queue_shutdown(g_v4l2_frame_queue);
    pthread_join(g_extraction_thread, NULL);
    
    // 关闭其他队列，让编码和传输线程退出
    queue_shutdown(g_encoding_queue);
    queue_shutdown(g_transmission_queue);
    
    for (int i = 0; i < NUM_ENCODING_THREADS; i++) {
        pthread_join(g_encoding_threads[i], NULL);
    }
    pthread_join(g_transmission_thread, NULL);
    
    // 清理资源
    printf("\n[Main] Cleaning up...\n");
    dvsDeinit();
    
    evs_tcp_sender_disconnect(g_tcp_sender);
    evs_tcp_sender_print_stats(g_tcp_sender);
    evs_tcp_sender_destroy(g_tcp_sender);
    
    time_window_accumulator_destroy(g_time_window);
    queue_destroy(g_v4l2_frame_queue);
    queue_destroy(g_encoding_queue);
    queue_destroy(g_transmission_queue);
    
    // 打印最终统计
    printf("\n========== Final Statistics ==========\n");
    stats_print(&g_stats);
    pthread_mutex_destroy(&g_stats.mutex);
    
    printf("[Main] Exit\n");
    return 0;
}
