/*
 * APS 和 EVS 时间戳对齐测试程序
 * 功能：
 *   1. 读取 APS 设备（ISP输出）的时间戳
 *   2. 读取 EVS 设备（DVS输出）的时间戳
 *   3. 进行时间戳对齐和比较分析
 * 
 * 设备：
 *   - APS: /dev/video11 (ISP输出，NV12格式)
 *   - EVS: /dev/video1 (DVS输出，SBGGR8格式)
 * 
 * 使用方法：
 *   ./aps_timestamp_test [aps_device] [evs_device] [test_frames] [选项]
 * 
 * 选项：
 *   --no-evs          : 禁用 EVS 测试，仅测试 APS
 *   --frames=N        : 设置测试帧数（默认 200）
 *   --verbose 或 -v   : 详细模式，打印每一帧的时间戳
 * 
 * 示例：
 *   ./aps_timestamp_test /dev/video11 /dev/video1 200
 *   ./aps_timestamp_test /dev/video11 /dev/video1 200 --verbose
 *   ./aps_timestamp_test --frames=100 --verbose
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <time.h>
#include <sys/select.h>

#define APS_DEVICE_DEFAULT "/dev/video11"
#define EVS_DEVICE_DEFAULT "/dev/video1"
#define APS_FRAME_WIDTH 640
#define APS_FRAME_HEIGHT 480
#define EVS_FRAME_WIDTH 4096
#define EVS_FRAME_HEIGHT 512
#define BUFFER_COUNT 4
#define TEST_FRAMES_DEFAULT 200
#define FMT_NUM_PLANES 1

struct buffer {
    void *start;
    size_t length;
};

// APS 相关
static struct buffer *aps_buffers = NULL;
static unsigned int aps_n_buffers = 0;
static int aps_fd = -1;
static enum v4l2_buf_type aps_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

// EVS 相关
static struct buffer *evs_buffers = NULL;
static unsigned int evs_n_buffers = 0;
static int evs_fd = -1;
static enum v4l2_buf_type evs_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

// 配置参数
static int test_frames = TEST_FRAMES_DEFAULT;
static int enable_evs = 1;  // 默认启用 EVS
static int verbose_mode = 0;  // 详细模式：打印每一帧的时间戳

// APS 统计信息
static uint64_t aps_prev_timestamp_us = 0;
static uint64_t aps_min_interval_us = UINT64_MAX;
static uint64_t aps_max_interval_us = 0;
static uint64_t aps_total_interval_us = 0;
static int aps_interval_count = 0;
static uint64_t aps_first_timestamp_us = 0;  // 第一帧时间戳（用于计算总时长）
static uint64_t aps_last_timestamp_us = 0;   // 最后一帧时间戳

// EVS 统计信息
static uint64_t evs_prev_timestamp_us = 0;
static uint64_t evs_min_interval_us = UINT64_MAX;
static uint64_t evs_max_interval_us = 0;
static uint64_t evs_total_interval_us = 0;
static int evs_interval_count = 0;
static uint64_t evs_first_timestamp_us = 0;  // 第一帧时间戳（用于计算总时长）
static uint64_t evs_last_timestamp_us = 0;   // 最后一帧时间戳

// 对齐统计信息
static uint64_t first_aps_timestamp_us = 0;
static uint64_t first_evs_timestamp_us = 0;
static int64_t total_timestamp_diff_us = 0;  // EVS - APS
static int timestamp_comparison_count = 0;
static int64_t min_timestamp_diff_us = INT64_MAX;
static int64_t max_timestamp_diff_us = INT64_MIN;

// 错误处理
static void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

// 执行 ioctl，带重试
static int xioctl(int fh, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);
    return r;
}

// 初始化 mmap 缓冲区（通用函数）
static void init_mmap(int video_fd, struct buffer **buffers_ptr, 
                      unsigned int *n_buffers_ptr, enum v4l2_buf_type *buf_type_ptr,
                      const char *device_name) {
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = *buf_type_ptr;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(video_fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", device_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", device_name);
        exit(EXIT_FAILURE);
    }

    *buffers_ptr = calloc(req.count, sizeof(**buffers_ptr));
    if (!*buffers_ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (*n_buffers_ptr = 0; *n_buffers_ptr < req.count; ++(*n_buffers_ptr)) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[FMT_NUM_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type = *buf_type_ptr;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = *n_buffers_ptr;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == *buf_type_ptr) {
            buf.m.planes = planes;
            buf.length = FMT_NUM_PLANES;
        }

        if (-1 == xioctl(video_fd, VIDIOC_QUERYBUF, &buf))
            errno_exit("VIDIOC_QUERYBUF");

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == *buf_type_ptr) {
            (*buffers_ptr)[*n_buffers_ptr].length = buf.m.planes[0].length;
            (*buffers_ptr)[*n_buffers_ptr].start = mmap(NULL, buf.m.planes[0].length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            video_fd, buf.m.planes[0].m.mem_offset);
        } else {
            (*buffers_ptr)[*n_buffers_ptr].length = buf.length;
            (*buffers_ptr)[*n_buffers_ptr].start = mmap(NULL, buf.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            video_fd, buf.m.offset);
        }

        if (MAP_FAILED == (*buffers_ptr)[*n_buffers_ptr].start)
            errno_exit("mmap");
    }

    printf("✓ 成功申请 %d 个缓冲区 (type=%s)\n", *n_buffers_ptr,
           *buf_type_ptr == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "MPLANE" : "SINGLE");
}

// 初始化设备（通用函数）
static void init_device(int video_fd, enum v4l2_buf_type *buf_type_ptr,
                        uint32_t width, uint32_t height, uint32_t pixelformat,
                        const char *device_name) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    // 查询设备能力
    if (-1 == xioctl(video_fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", device_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }

    printf("[%s] 设备信息:\n", device_name);
    printf("  驱动: %s\n", cap.driver);
    printf("  卡名: %s\n", cap.card);

    // 检查设备能力
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        fprintf(stderr, "%s is no video capture device\n", device_name);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", device_name);
        exit(EXIT_FAILURE);
    }

    // 确定缓冲区类型
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        *buf_type_ptr = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        *buf_type_ptr = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    // 设置格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = *buf_type_ptr;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = pixelformat;

    if (-1 == xioctl(video_fd, VIDIOC_S_FMT, &fmt))
        errno_exit("VIDIOC_S_FMT");

    printf("[%s] ✓ 设置格式: %dx%d, 格式=0x%X\n", 
           device_name, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.pixelformat);
}

// 开始采集（通用函数）
static void start_capturing(int video_fd, struct buffer *buffers, 
                            unsigned int n_buffers, enum v4l2_buf_type buf_type,
                            const char *device_name) {
    unsigned int i;
    enum v4l2_buf_type type;

    // 将所有缓冲区加入队列
    for (i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[FMT_NUM_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
            buf.m.planes = planes;
            buf.length = FMT_NUM_PLANES;
        }

        if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");
    }

    // 启动流
    type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMON, &type))
        errno_exit("VIDIOC_STREAMON");

    printf("[%s] ✓ 开始采集...\n", device_name);
}

// 停止采集（通用函数）
static void stop_capturing(int video_fd, enum v4l2_buf_type buf_type,
                           const char *device_name) {
    enum v4l2_buf_type type = buf_type;
    if (-1 == xioctl(video_fd, VIDIOC_STREAMOFF, &type))
        errno_exit("VIDIOC_STREAMOFF");
    printf("[%s] ✓ 停止采集\n", device_name);
}

// 读取一帧并返回时间戳（通用函数）
static int read_frame_timestamp(int video_fd, struct buffer *buffers,
                                 unsigned int n_buffers, enum v4l2_buf_type buf_type,
                                 uint64_t *timestamp_us, uint32_t *sequence,
                                 const char *device_name) {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[FMT_NUM_PLANES];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type = buf_type;
    buf.memory = V4L2_MEMORY_MMAP;

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
        buf.m.planes = planes;
        buf.length = FMT_NUM_PLANES;
    }

    if (-1 == xioctl(video_fd, VIDIOC_DQBUF, &buf)) {
        if (errno == EAGAIN) {
            // 没有数据可读，这是正常情况
            return 0;
        }
        // 其他错误，记录详细信息
        static int error_count = 0;
        if (error_count < 5) {
            fprintf(stderr, "⚠ [%s] VIDIOC_DQBUF 失败: %s (errno=%d)\n", 
                   device_name, strerror(errno), errno);
            error_count++;
        }
        return -1;
    }

    if (buf.index >= n_buffers) {
        fprintf(stderr, "⚠ [%s] 缓冲区索引无效: %d >= %u\n", 
               device_name, buf.index, n_buffers);
        xioctl(video_fd, VIDIOC_QBUF, &buf);
        return 0;
    }

    // 获取时间戳（微秒）
    uint64_t ts_us = (uint64_t)buf.timestamp.tv_sec * 1000000ULL + 
                     (uint64_t)buf.timestamp.tv_usec;
    
    if (timestamp_us) *timestamp_us = ts_us;
    if (sequence) *sequence = buf.sequence;
    
    // 重新加入队列
    if (-1 == xioctl(video_fd, VIDIOC_QBUF, &buf)) {
        fprintf(stderr, "⚠ [%s] VIDIOC_QBUF 失败: %s\n", 
               device_name, strerror(errno));
        return -1;
    }

    return 1;
}

// 主循环：同时读取 APS 和 EVS 并进行时间戳对齐比较
static void mainloop(void) {
    uint64_t aps_timestamp_us = 0;
    uint64_t evs_timestamp_us = 0;
    uint32_t aps_sequence = 0;
    uint32_t evs_sequence = 0;
    int aps_frame_count = 0;
    int evs_frame_count = 0;
    int timeout_count = 0;
    int max_timeout_before_warning = 50;  // 5秒后警告（50 * 100ms）

    printf("========================================\n");
    if (enable_evs) {
        printf("开始测试 APS 和 EVS 时间戳对齐（共 %d 帧）\n", test_frames);
    } else {
        printf("开始测试 APS 时间戳（共 %d 帧）\n", test_frames);
    }
    printf("========================================\n\n");

    while (aps_frame_count < test_frames || (enable_evs && evs_frame_count < test_frames)) {
        fd_set fds;
        struct timeval tv;
        int max_fd = aps_fd;
        int ready_count = 0;

        FD_ZERO(&fds);
        FD_SET(aps_fd, &fds);
        if (enable_evs && evs_fd >= 0) {
            FD_SET(evs_fd, &fds);
            if (evs_fd > max_fd) max_fd = evs_fd;
        }

        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms 超时

        int r = select(max_fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0 && errno != EINTR) {
            fprintf(stderr, "⚠ select() 错误: %s\n", strerror(errno));
            break;
        }
        
        // 超时处理
        if (r == 0) {
            timeout_count++;
            if (timeout_count >= max_timeout_before_warning) {
                if (timeout_count == max_timeout_before_warning) {
                    fprintf(stderr, "\n⚠ 警告：设备没有数据产生（已等待 %.1f 秒）\n", 
                           timeout_count * 0.1);
                    if (aps_frame_count == 0) {
                        fprintf(stderr, "   [APS] 未收到任何帧，请检查 /dev/video11 是否正常工作\n");
                    } else {
                        fprintf(stderr, "   [APS] 已收到 %d 帧 ✓\n", aps_frame_count);
                    }
                    if (enable_evs && evs_frame_count == 0) {
                        fprintf(stderr, "   [EVS] 未收到任何帧\n");
                        fprintf(stderr, "   ⚠ EVS (DVS事件相机) 特性说明：\n");
                        fprintf(stderr, "      - 只有场景发生变化时才会产生帧\n");
                        fprintf(stderr, "      - 如果场景静止，不会产生任何数据（这是正常的）\n");
                        fprintf(stderr, "      - 请尝试在相机前移动物体或改变光照\n");
                        fprintf(stderr, "      - 或者使用 --no-evs 参数只测试 APS\n");
                    } else if (enable_evs) {
                        fprintf(stderr, "   [EVS] 已收到 %d 帧 ✓\n", evs_frame_count);
                    }
                    fprintf(stderr, "   程序将继续等待...\n\n");
                } else if (timeout_count % 50 == 0) {
                    // 每5秒打印一次状态
                    fprintf(stderr, "   等待中... APS=%d/%d, EVS=%d/%d (已等待 %.1f 秒)\n",
                           aps_frame_count, test_frames, 
                           enable_evs ? evs_frame_count : 0, test_frames,
                           timeout_count * 0.1);
                }
            }
            // 即使超时，也尝试直接读取（某些情况下 select 可能不可靠）
            ready_count = 0;  // 继续尝试直接读取
        } else {
            timeout_count = 0;  // 重置超时计数
        }

        // 标记本次循环是否读取到新帧
        int aps_new_frame = 0;
        int evs_new_frame = 0;

        // 读取 APS 帧（即使 select 超时也尝试读取，因为 select 对 V4L2 可能不可靠）
        if (aps_frame_count < test_frames) {
            int ret = read_frame_timestamp(aps_fd, aps_buffers, aps_n_buffers, 
                                          aps_buf_type, &aps_timestamp_us, &aps_sequence,
                                          "APS");
            if (ret > 0) {
                aps_frame_count++;
                aps_new_frame = 1;  // 标记读取到新帧
                
                // 计算 APS 帧间隔
                uint64_t aps_interval = 0;
                if (aps_prev_timestamp_us > 0) {
                    aps_interval = aps_timestamp_us - aps_prev_timestamp_us;
                    if (aps_interval < aps_min_interval_us) aps_min_interval_us = aps_interval;
                    if (aps_interval > aps_max_interval_us) aps_max_interval_us = aps_interval;
                    aps_total_interval_us += aps_interval;
                    aps_interval_count++;
                } else {
                    first_aps_timestamp_us = aps_timestamp_us;
                    aps_first_timestamp_us = aps_timestamp_us;
                    printf("✓ 收到第一帧 APS 数据 (seq=%u, ts=%llu us)\n", 
                           aps_sequence, aps_timestamp_us);
                }
                aps_last_timestamp_us = aps_timestamp_us;
                ready_count++;
                
                // 详细模式：打印每一帧的时间戳
                if (verbose_mode) {
                    if (aps_interval_count > 0 && aps_interval > 0) {
                        double fps = 1000000.0 / aps_interval;
                        printf("[APS] 帧 #%d (seq=%u): ts=%llu us, 间隔=%.3f ms (%.2f fps)\n",
                               aps_frame_count, aps_sequence, aps_timestamp_us,
                               aps_interval / 1000.0, fps);
                    } else {
                        printf("[APS] 帧 #%d (seq=%u): ts=%llu us\n",
                               aps_frame_count, aps_sequence, aps_timestamp_us);
                    }
                }
                
                aps_prev_timestamp_us = aps_timestamp_us;
                
                // 每10帧打印一次进度
                if (aps_frame_count % 10 == 0 && !verbose_mode) {
                    printf("[APS] 已读取 %d/%d 帧\n", aps_frame_count, test_frames);
                }
            } else if (ret < 0) {
                // 读取失败（不是 EAGAIN）
                static int aps_error_print = 0;
                if (aps_error_print < 3) {
                    fprintf(stderr, "⚠ [APS] 读取失败: %s\n", strerror(errno));
                    aps_error_print++;
                }
            }
        }

        // 读取 EVS 帧（即使 select 超时也尝试读取）
        if (enable_evs && evs_fd >= 0 && evs_frame_count < test_frames) {
            int ret = read_frame_timestamp(evs_fd, evs_buffers, evs_n_buffers, 
                                          evs_buf_type, &evs_timestamp_us, &evs_sequence,
                                          "EVS");
            if (ret > 0) {
                evs_frame_count++;
                evs_new_frame = 1;  // 标记读取到新帧
                
                // 计算 EVS 帧间隔
                uint64_t evs_interval = 0;
                if (evs_prev_timestamp_us > 0) {
                    evs_interval = evs_timestamp_us - evs_prev_timestamp_us;
                    if (evs_interval < evs_min_interval_us) evs_min_interval_us = evs_interval;
                    if (evs_interval > evs_max_interval_us) evs_max_interval_us = evs_interval;
                    evs_total_interval_us += evs_interval;
                    evs_interval_count++;
                } else {
                    first_evs_timestamp_us = evs_timestamp_us;
                    evs_first_timestamp_us = evs_timestamp_us;
                    printf("✓ 收到第一帧 EVS 数据 (seq=%u, ts=%llu us)\n", 
                           evs_sequence, evs_timestamp_us);
                }
                evs_last_timestamp_us = evs_timestamp_us;
                ready_count++;
                
                // 详细模式：打印每一帧的时间戳
                if (verbose_mode) {
                    if (evs_interval_count > 0 && evs_interval > 0) {
                        double fps = 1000000.0 / evs_interval;
                        printf("[EVS] 帧 #%d (seq=%u): ts=%llu us, 间隔=%.3f ms (%.2f fps)\n",
                               evs_frame_count, evs_sequence, evs_timestamp_us,
                               evs_interval / 1000.0, fps);
                    } else {
                        printf("[EVS] 帧 #%d (seq=%u): ts=%llu us\n",
                               evs_frame_count, evs_sequence, evs_timestamp_us);
                    }
                }
                
                evs_prev_timestamp_us = evs_timestamp_us;
                
                // 每10帧打印一次进度
                if (evs_frame_count % 10 == 0 && !verbose_mode) {
                    printf("[EVS] 已读取 %d/%d 帧\n", evs_frame_count, test_frames);
                }
            } else if (ret < 0) {
                // 读取失败（不是 EAGAIN）
                static int evs_error_print = 0;
                if (evs_error_print < 3) {
                    fprintf(stderr, "⚠ [EVS] 读取失败: %s\n", strerror(errno));
                    evs_error_print++;
                }
            }
        }

        // 只有当两个设备都在本次循环中读取到新帧时，才进行比较
        // 这样可以确保比较的是同一时间窗口内的帧，而不是不同帧数的帧
        if (enable_evs && aps_new_frame && evs_new_frame && 
            aps_frame_count > 0 && evs_frame_count > 0) {
            int64_t diff_us = (int64_t)evs_timestamp_us - (int64_t)aps_timestamp_us;
            total_timestamp_diff_us += diff_us;
            timestamp_comparison_count++;
            
            if (diff_us < min_timestamp_diff_us) min_timestamp_diff_us = diff_us;
            if (diff_us > max_timestamp_diff_us) max_timestamp_diff_us = diff_us;

            // 每10次比较输出一次详细信息
            if (timestamp_comparison_count % 10 == 0 || timestamp_comparison_count <= 5) {
                printf("========== 对齐比较 #%d ==========\n", timestamp_comparison_count);
                printf("[APS] 帧 %d (seq=%u): 时间戳 = %llu us\n", 
                       aps_frame_count, aps_sequence, aps_timestamp_us);
                printf("[EVS] 帧 %d (seq=%u): 时间戳 = %llu us\n", 
                       evs_frame_count, evs_sequence, evs_timestamp_us);
                printf("[对齐] 差值 = %lld us (%.3f ms)\n", diff_us, diff_us / 1000.0);
                
                // 计算帧间隔（使用保存的上一个时间戳）
                static uint64_t prev_aps_ts_for_interval = 0;
                static uint64_t prev_evs_ts_for_interval = 0;
                if (prev_aps_ts_for_interval > 0 && prev_evs_ts_for_interval > 0) {
                    uint64_t aps_interval = aps_timestamp_us - prev_aps_ts_for_interval;
                    uint64_t evs_interval = evs_timestamp_us - prev_evs_ts_for_interval;
                    printf("[间隔] APS=%.2fms, EVS=%.2fms\n", 
                           aps_interval / 1000.0, evs_interval / 1000.0);
                }
                prev_aps_ts_for_interval = aps_timestamp_us;
                prev_evs_ts_for_interval = evs_timestamp_us;
                
                printf("\n");
            }
        }

        // 如果没有数据，短暂休眠避免 CPU 占用过高
        if (ready_count == 0) {
            usleep(50000);  // 50ms 延迟（降低 CPU 占用）
        }
    }

    // 输出统计信息
    printf("\n========================================\n");
    printf("测试完成\n");
    printf("========================================\n");
    printf("APS 帧数: %d\n", aps_frame_count);
    if (enable_evs) {
        printf("EVS 帧数: %d\n", evs_frame_count);
        printf("对齐比较次数: %d\n", timestamp_comparison_count);
    }

    // APS 统计
    printf("\n========== [APS] 帧率统计 ==========\n");
    printf("总帧数: %d 帧\n", aps_frame_count);
    if (aps_interval_count > 0) {
        uint64_t avg_interval = aps_total_interval_us / aps_interval_count;
        double avg_fps = 1000000.0 / avg_interval;
        
        // 基于总时长计算帧率（更准确）
        double total_duration_sec = 0.0;
        double total_fps = 0.0;
        if (aps_first_timestamp_us > 0 && aps_last_timestamp_us > 0 && 
            aps_frame_count > 1) {
            uint64_t total_duration_us = aps_last_timestamp_us - aps_first_timestamp_us;
            total_duration_sec = total_duration_us / 1000000.0;
            total_fps = (aps_frame_count - 1) / total_duration_sec;
        }
        
        printf("\n基于帧间隔的统计:\n");
        printf("  平均帧间隔: %llu us (%.3f ms)\n", avg_interval, avg_interval / 1000.0);
        printf("  平均帧率: %.2f fps (每秒 %.2f 帧)\n", avg_fps, avg_fps);
        printf("  最小帧间隔: %llu us (%.3f ms, 最高 %.2f fps)\n", 
               aps_min_interval_us, aps_min_interval_us / 1000.0,
               1000000.0 / aps_min_interval_us);
        printf("  最大帧间隔: %llu us (%.3f ms, 最低 %.2f fps)\n", 
               aps_max_interval_us, aps_max_interval_us / 1000.0,
               1000000.0 / aps_max_interval_us);
        
        if (total_duration_sec > 0) {
            printf("\n基于总时长的统计:\n");
            printf("  总时长: %.3f 秒\n", total_duration_sec);
            printf("  总帧率: %.2f fps (每秒 %.2f 帧)\n", total_fps, total_fps);
        }
    } else if (aps_frame_count > 0) {
        printf("  (只有 1 帧，无法计算帧率)\n");
    }

    // EVS 统计
    if (enable_evs) {
        printf("\n========== [EVS] 帧率统计 ==========\n");
        printf("总帧数: %d 帧\n", evs_frame_count);
        if (evs_interval_count > 0) {
            uint64_t avg_interval = evs_total_interval_us / evs_interval_count;
            double avg_fps = 1000000.0 / avg_interval;
            
            // 基于总时长计算帧率（更准确）
            double total_duration_sec = 0.0;
            double total_fps = 0.0;
            if (evs_first_timestamp_us > 0 && evs_last_timestamp_us > 0 && 
                evs_frame_count > 1) {
                uint64_t total_duration_us = evs_last_timestamp_us - evs_first_timestamp_us;
                total_duration_sec = total_duration_us / 1000000.0;
                total_fps = (evs_frame_count - 1) / total_duration_sec;
            }
            
            printf("\n基于帧间隔的统计:\n");
            printf("  平均帧间隔: %llu us (%.3f ms)\n", avg_interval, avg_interval / 1000.0);
            printf("  平均帧率: %.2f fps (每秒 %.2f 帧)\n", avg_fps, avg_fps);
            printf("  最小帧间隔: %llu us (%.3f ms, 最高 %.2f fps)\n", 
                   evs_min_interval_us, evs_min_interval_us / 1000.0,
                   1000000.0 / evs_min_interval_us);
            printf("  最大帧间隔: %llu us (%.3f ms, 最低 %.2f fps)\n", 
                   evs_max_interval_us, evs_max_interval_us / 1000.0,
                   1000000.0 / evs_max_interval_us);
            
            if (total_duration_sec > 0) {
                printf("\n基于总时长的统计:\n");
                printf("  总时长: %.3f 秒\n", total_duration_sec);
                printf("  总帧率: %.2f fps (每秒 %.2f 帧)\n", total_fps, total_fps);
            }
        } else if (evs_frame_count > 0) {
            printf("  (只有 1 帧，无法计算帧率)\n");
        }
    }

    // 对齐统计
    if (enable_evs && timestamp_comparison_count > 0) {
        int64_t avg_diff = total_timestamp_diff_us / timestamp_comparison_count;
        printf("\n[对齐] 时间戳差值统计:\n");
        printf("  初始偏移: APS=%llu us, EVS=%llu us, 差值=%lld us\n",
               first_aps_timestamp_us, first_evs_timestamp_us,
               (int64_t)first_evs_timestamp_us - (int64_t)first_aps_timestamp_us);
        printf("  平均差值: %lld us (%.3f ms)\n", avg_diff, avg_diff / 1000.0);
        printf("  最小差值: %lld us (%.3f ms)\n", min_timestamp_diff_us, min_timestamp_diff_us / 1000.0);
        printf("  最大差值: %lld us (%.3f ms)\n", max_timestamp_diff_us, max_timestamp_diff_us / 1000.0);
        
        if (avg_diff < 1000 && avg_diff > -1000) {
            printf("  ✓ 时间戳已对齐（平均差值 < 1ms）\n");
        } else {
            printf("  ⚠ 时间戳存在偏移（平均差值 = %.3f ms）\n", avg_diff / 1000.0);
            printf("  提示：如果差值较大，可能需要使用 DVS 时间戳偏移校正\n");
        }
    }
    
    printf("\n说明：\n");
    printf("  - APS 时间戳来自 V4L2 buf.timestamp（系统时间）\n");
    if (enable_evs) {
        printf("  - EVS 时间戳来自 V4L2 buf.timestamp（系统时间）\n");
        printf("  - 两者都使用系统时间，理论上应该对齐\n");
    }
    printf("  - 30fps 应该约 33.3ms 间隔\n");
    printf("========================================\n");
}

// 清理资源
static void cleanup_resources(void) {
    // 清理 APS 资源
    if (aps_buffers) {
        for (unsigned int i = 0; i < aps_n_buffers; ++i) {
            if (aps_buffers[i].start != MAP_FAILED)
                munmap(aps_buffers[i].start, aps_buffers[i].length);
        }
        free(aps_buffers);
        aps_buffers = NULL;
    }

    // 清理 EVS 资源
    if (evs_buffers) {
        for (unsigned int i = 0; i < evs_n_buffers; ++i) {
            if (evs_buffers[i].start != MAP_FAILED)
                munmap(evs_buffers[i].start, evs_buffers[i].length);
        }
        free(evs_buffers);
        evs_buffers = NULL;
    }

    if (aps_fd >= 0) close(aps_fd);
    if (evs_fd >= 0) close(evs_fd);
}

int main(int argc, char **argv) {
    const char *aps_device = APS_DEVICE_DEFAULT;
    const char *evs_device = EVS_DEVICE_DEFAULT;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-evs") == 0) {
            enable_evs = 0;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            test_frames = atoi(argv[i] + 9);
        } else if (argv[i][0] != '-') {
            if (aps_fd < 0) {
                aps_device = argv[i];
            } else {
                evs_device = argv[i];
            }
        }
    }

    printf("========================================\n");
    if (enable_evs) {
        printf("APS 和 EVS 时间戳对齐测试程序\n");
    } else {
        printf("APS 时间戳测试程序\n");
    }
    printf("========================================\n");
    printf("APS 设备: %s\n", aps_device);
    if (enable_evs) {
        printf("EVS 设备: %s\n", evs_device);
    }
    printf("测试帧数: %d\n", test_frames);
    printf("详细模式: %s\n", verbose_mode ? "启用 (打印每一帧时间戳)" : "禁用");
    printf("========================================\n\n");

    // 1. 初始化 APS 设备
    printf("[1/%d] 初始化 APS 设备...\n", enable_evs ? 3 : 2);
    aps_fd = open(aps_device, O_RDWR | O_NONBLOCK, 0);
    if (-1 == aps_fd) {
        fprintf(stderr, "❌ 无法打开 APS 设备 %s: %s\n", aps_device, strerror(errno));
        exit(EXIT_FAILURE);
    }

    init_device(aps_fd, &aps_buf_type, APS_FRAME_WIDTH, APS_FRAME_HEIGHT,
                V4L2_PIX_FMT_NV12, aps_device);
    init_mmap(aps_fd, &aps_buffers, &aps_n_buffers, &aps_buf_type, aps_device);
    start_capturing(aps_fd, aps_buffers, aps_n_buffers, aps_buf_type, aps_device);

    // 2. 初始化 EVS 设备（如果启用）
    if (enable_evs) {
        printf("[2/3] 初始化 EVS 设备...\n");
        evs_fd = open(evs_device, O_RDWR | O_NONBLOCK, 0);
        if (-1 == evs_fd) {
            fprintf(stderr, "⚠ 无法打开 EVS 设备 %s: %s\n", evs_device, strerror(errno));
            fprintf(stderr, "   将仅测试 APS 时间戳\n");
            enable_evs = 0;
        } else {
            init_device(evs_fd, &evs_buf_type, EVS_FRAME_WIDTH, EVS_FRAME_HEIGHT,
                        V4L2_PIX_FMT_SBGGR8, evs_device);
            init_mmap(evs_fd, &evs_buffers, &evs_n_buffers, &evs_buf_type, evs_device);
            start_capturing(evs_fd, evs_buffers, evs_n_buffers, evs_buf_type, evs_device);
        }
    }

    printf("[%d/%d] 开始测试...\n\n", enable_evs ? 3 : 2, enable_evs ? 3 : 2);

    // 3. 主循环
    mainloop();

    // 4. 清理
    if (aps_fd >= 0) {
        stop_capturing(aps_fd, aps_buf_type, aps_device);
    }
    if (enable_evs && evs_fd >= 0) {
        stop_capturing(evs_fd, evs_buf_type, evs_device);
    }

    cleanup_resources();
    printf("✓ 程序退出\n");

    return 0;
}
