/*
 * APS RTSP Server V2 - Shared Memory Based Architecture
 * 
 * 功能：从共享内存读取 APS VPSS 数据，编码为 H264，通过 RTSP 服务传输
 * 架构：apx003_mpi_sample (VI→VPSS→SharedMem) + aps_rtsp_server_v2 (SharedMem→VENC→RTSP)
 * 
 * 与V1的区别：
 * - V1: 独立创建 VI-VPSS-VENC 完整流程（资源冲突）
 * - V2: 从共享内存读取，只负责 VENC-RTSP（无资源冲突）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/time.h>

#include "rk_defines.h"
#include "rk_debug.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_cal.h"
#include "rk_common.h"
#include "rk_comm_venc.h"
#include "test_common.h"
#include "test_comm_utils.h"

#include "rtsp_demo_2.h"

// ==================== 配置参数 ====================

// YUV 数据规格（与 apx003_mpi_sample 的 VPSS RESIZE 输出一致）
#define YUV_WIDTH                   (768)
#define YUV_HEIGHT                  (608)

// ✅ 帧元数据结构（与 apxGetData.c 保持一致）
typedef struct {
    RK_U64 u64PTS;          // 时间戳（微秒）
    RK_U32 u32FrameSeq;     // 帧序号
    RK_U32 u32Width;        // 宽度
    RK_U32 u32Height;       // 高度
    RK_U32 u32Reserved[4];  // 预留
} FRAME_METADATA_S;

// ✅ 共享内存数据长度：元数据 + YUV数据
#define YUV_DATA_LEN                (sizeof(FRAME_METADATA_S) + YUV_WIDTH * YUV_HEIGHT * 3 / 2)
#define YUV_ONLY_LEN                (YUV_WIDTH * YUV_HEIGHT * 3 / 2)

// 共享内存配置（与 apx003_mpi_sample 一致）
#define SHM_NAME                    "/apcdatashm"
#define SEM_WAIT_NAME               "/wait_aps_sem"
#define SEM_DONE_NAME               "/send_done_sem"

// VENC 配置
#define VENC_CHN_ID                 (0)
#define VENC_BITRATE                (4000000)  // 4Mbps
#define VENC_FPS                    (30)
#define VENC_GOP                    (5)       // GOP=10，更快输出第一个I帧（原30）
#define VENC_STREAM_BUFCNT          (10)       // 增加缓冲：8→15 (必须 > GOP)

// RTSP 配置
#define RTSP_PORT                   (8554)
#define RTSP_PATH                   "/live"

// MB Pool 配置
#define MB_POOL_CNT                 (4)  // 4个缓冲区

// ==================== 数据结构 ====================

// 共享内存上下文
typedef struct {
    int shm_fd;                    // 共享内存文件描述符
    uint8_t *pYuvData;             // YUV数据映射地址
    size_t data_len;               // 数据长度
    sem_t *wait_sem;               // 等待信号量（生产者通知）
    sem_t *done_sem;               // 完成信号量（消费者确认）
} SHM_CTX_S;

// VENC 配置
typedef struct {
    RK_S32 s32ChnId;
    VENC_CHN_ATTR_S stChnAttr;
} VENC_CFG_S;

// RTSP 统计信息
typedef struct {
    RK_U64 u64FrameCount;
    RK_U64 u64ByteCount;
    RK_U64 u64ErrorCount;
    RK_U64 u64LastReportTime;
    RK_U64 u64StartTime;
} RTSP_STATS_S;

// ==================== 全局变量 ====================

// ✅ 全局时间戳跟踪（用于在 yuv_feed_thread 和 rtsp_send_thread 间传递时间戳）
static RK_U64 g_lastMetadataPTS = 0;        // 最新的硬件时间戳（微秒）
static RK_U32 g_lastMetadataSeq = 0;        // 最新的帧序号
static pthread_mutex_t g_ptsMutex = PTHREAD_MUTEX_INITIALIZER;

static RK_BOOL bquit = RK_FALSE;
static pthread_mutex_t g_rtspLock = PTHREAD_MUTEX_INITIALIZER;

static SHM_CTX_S g_shmCtx = {0};
static VENC_CFG_S g_vencCfg = {0};
static MB_POOL g_mbPool = MB_INVALID_POOLID;
static rtsp_demo_handle g_rtspDemo = NULL;
static rtsp_session_handle g_rtspSession = NULL;
static RTSP_STATS_S g_rtspStats = {0};

// 运行配置
static struct {
    int bitrate_override;
    int venc_stream_bufcnt;
    int shm_timeout_sec;
} g_runCfg = {
    .bitrate_override = 0,
    .venc_stream_bufcnt = VENC_STREAM_BUFCNT,
    .shm_timeout_sec = 2,  // 与 usb_app 完全一致：DIAG_TIMEOUT = 2 秒
};

// ==================== 信号处理 ====================

static void sigterm_handler(int sig) {
    RK_LOGI("Received signal %d, exiting...", sig);
    bquit = RK_TRUE;
}

// ==================== 参数解析 ====================

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--bitrate=", 10)) {
            g_runCfg.bitrate_override = atoi(argv[i] + 10);
        } else if (!strncmp(argv[i], "--venc-buf=", 11)) {
            g_runCfg.venc_stream_bufcnt = atoi(argv[i] + 11);
        } else if (!strncmp(argv[i], "--timeout=", 10)) {
            g_runCfg.shm_timeout_sec = atoi(argv[i] + 10);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("APS RTSP Server V2 - Shared Memory Based\n");
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --bitrate=N     Set VENC bitrate (default: %d)\n", VENC_BITRATE);
            printf("  --venc-buf=N    Set VENC stream buffer count (default: %d)\n", VENC_STREAM_BUFCNT);
            printf("  --timeout=N     Set shared memory wait timeout in seconds (default: 2, same as usb_app)\n");
            printf("  --help, -h      Show this help\n");
            printf("\n");
            printf("Note: apx003_mpi_sample must be running first to provide data\n");
            exit(0);
        }
    }
}

// ==================== 共享内存管理 ====================

static RK_S32 init_shared_memory(SHM_CTX_S *pCtx) {
    RK_LOGI("Initializing shared memory...");
    
    // 1. 打开共享内存（只读模式）
    pCtx->shm_fd = shm_open(SHM_NAME, O_RDONLY, 0777);
    if (pCtx->shm_fd < 0) {
        RK_LOGE("shm_open failed: %s", strerror(errno));
        RK_LOGE("Please ensure apx003_mpi_sample is running first");
        return RK_FAILURE;
    }
    
    // 2. 映射到进程地址空间
    pCtx->data_len = YUV_DATA_LEN;
    pCtx->pYuvData = mmap(NULL, pCtx->data_len, PROT_READ, MAP_SHARED, pCtx->shm_fd, 0);
    if (pCtx->pYuvData == MAP_FAILED) {
        RK_LOGE("mmap failed: %s", strerror(errno));
        close(pCtx->shm_fd);
        return RK_FAILURE;
    }
    
    // 3. 打开信号量
    pCtx->wait_sem = sem_open(SEM_WAIT_NAME, O_RDWR);
    if (pCtx->wait_sem == SEM_FAILED) {
        RK_LOGE("sem_open(%s) failed: %s", SEM_WAIT_NAME, strerror(errno));
        munmap(pCtx->pYuvData, pCtx->data_len);
        close(pCtx->shm_fd);
        return RK_FAILURE;
    }
    
    pCtx->done_sem = sem_open(SEM_DONE_NAME, O_RDWR);
    if (pCtx->done_sem == SEM_FAILED) {
        RK_LOGE("sem_open(%s) failed: %s", SEM_DONE_NAME, strerror(errno));
        sem_close(pCtx->wait_sem);
        munmap(pCtx->pYuvData, pCtx->data_len);
        close(pCtx->shm_fd);
        return RK_FAILURE;
    }
    
    RK_LOGI("Shared memory initialized:");
    RK_LOGI("  - Name: %s", SHM_NAME);
    RK_LOGI("  - Size: %zu bytes (%dx%d YUV420SP)", pCtx->data_len, YUV_WIDTH, YUV_HEIGHT);
    RK_LOGI("  - Wait semaphore: %s", SEM_WAIT_NAME);
    RK_LOGI("  - Done semaphore: %s", SEM_DONE_NAME);
    
    return RK_SUCCESS;
}

static void deinit_shared_memory(SHM_CTX_S *pCtx) {
    if (pCtx->done_sem != SEM_FAILED) {
        sem_close(pCtx->done_sem);
        pCtx->done_sem = SEM_FAILED;
    }
    
    if (pCtx->wait_sem != SEM_FAILED) {
        sem_close(pCtx->wait_sem);
        pCtx->wait_sem = SEM_FAILED;
    }
    
    if (pCtx->pYuvData != MAP_FAILED && pCtx->pYuvData != NULL) {
        munmap(pCtx->pYuvData, pCtx->data_len);
        pCtx->pYuvData = NULL;
    }
    
    if (pCtx->shm_fd >= 0) {
        close(pCtx->shm_fd);
        pCtx->shm_fd = -1;
    }
    
    RK_LOGI("Shared memory deinitialized");
}

// ==================== MB Pool ====================

static MB_POOL create_yuv_mb_pool(RK_U32 width, RK_U32 height, RK_U32 count) {
    PIC_BUF_ATTR_S stPicBufAttr;
    MB_PIC_CAL_S stMbPicCalResult;
    MB_POOL_CONFIG_S stMbPoolCfg;
    RK_S32 s32Ret;
    
    stPicBufAttr.u32Width = width;
    stPicBufAttr.u32Height = height;
    stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
    stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
    
    s32Ret = RK_MPI_CAL_COMM_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_CAL_COMM_GetPicBufferSize failed: 0x%x", s32Ret);
        return MB_INVALID_POOLID;
    }
    
    memset(&stMbPoolCfg, 0, sizeof(stMbPoolCfg));
    stMbPoolCfg.u64MBSize = stMbPicCalResult.u32MBSize;
    stMbPoolCfg.u32MBCnt = count;
    stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
    stMbPoolCfg.bPreAlloc = RK_TRUE;
    
    MB_POOL mbPool = RK_MPI_MB_CreatePool(&stMbPoolCfg);
    if (mbPool == MB_INVALID_POOLID) {
        RK_LOGE("RK_MPI_MB_CreatePool failed");
        return MB_INVALID_POOLID;
    }
    
    RK_LOGI("MB Pool created: %u buffers, size=%llu bytes each", count, stMbPoolCfg.u64MBSize);
    return mbPool;
}

// ==================== VENC 管理 ====================

static RK_S32 create_venc(VENC_CFG_S *pCfg) {
    RK_S32 s32Ret;
    
    memset(&pCfg->stChnAttr, 0, sizeof(VENC_CHN_ATTR_S));
    
    // 编码器属性
    pCfg->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_AVC;
    pCfg->stChnAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    pCfg->stChnAttr.stVencAttr.u32PicWidth = YUV_WIDTH;
    pCfg->stChnAttr.stVencAttr.u32PicHeight = YUV_HEIGHT;
    pCfg->stChnAttr.stVencAttr.u32VirWidth = YUV_WIDTH;
    pCfg->stChnAttr.stVencAttr.u32VirHeight = YUV_HEIGHT;
    pCfg->stChnAttr.stVencAttr.u32StreamBufCnt = g_runCfg.venc_stream_bufcnt;
    pCfg->stChnAttr.stVencAttr.u32BufSize = YUV_WIDTH * YUV_HEIGHT;  // ⚠️ 关键修复：必须设置！
    pCfg->stChnAttr.stVencAttr.u32Profile = H264E_PROFILE_MAIN;
    
    // 码率控制
    int bitrate = (g_runCfg.bitrate_override > 0) ? g_runCfg.bitrate_override : VENC_BITRATE;
    pCfg->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.u32Gop = VENC_GOP;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = VENC_FPS;
    
    // 创建VENC通道
    s32Ret = RK_MPI_VENC_CreateChn(pCfg->s32ChnId, &pCfg->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_CreateChn failed: 0x%x", s32Ret);
        return s32Ret;
    }
    
    RK_LOGI("VENC created: Chn=%d, %dx%d, BitRate=%d, FPS=%d, BufCnt=%d",
            pCfg->s32ChnId, YUV_WIDTH, YUV_HEIGHT, bitrate, VENC_FPS,
            g_runCfg.venc_stream_bufcnt);
    
    return RK_SUCCESS;
}

static RK_S32 start_venc_recv(VENC_CFG_S *pCfg) {
    VENC_RECV_PIC_PARAM_S stRecvParam;
    memset(&stRecvParam, 0, sizeof(stRecvParam));
    stRecvParam.s32RecvPicNum = -1;  // 无限接收
    
    RK_S32 s32Ret = RK_MPI_VENC_StartRecvFrame(pCfg->s32ChnId, &stRecvParam);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_StartRecvFrame failed: 0x%x", s32Ret);
        return s32Ret;
    }
    
    RK_LOGI("VENC started receiving frames");
    
    // ⚠️ 关键修复：立即请求一个 IDR 帧
    s32Ret = RK_MPI_VENC_RequestIDR(pCfg->s32ChnId, RK_TRUE);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGW("RK_MPI_VENC_RequestIDR failed: 0x%x", s32Ret);
    } else {
        RK_LOGI("✓ Requested IDR frame");
    }
    
    return RK_SUCCESS;
}

static void destroy_venc(VENC_CFG_S *pCfg) {
    RK_MPI_VENC_StopRecvFrame(pCfg->s32ChnId);
    RK_MPI_VENC_DestroyChn(pCfg->s32ChnId);
    RK_LOGI("VENC destroyed");
}

// ==================== RTSP 管理 ====================

static RK_S32 init_rtsp_server(void) {
    g_rtspDemo = rtsp_new_demo(RTSP_PORT);
    if (g_rtspDemo == NULL) {
        RK_LOGE("rtsp_new_demo failed");
        return RK_FAILURE;
    }
    
    g_rtspSession = rtsp_new_session(g_rtspDemo, RTSP_PATH);
    if (g_rtspSession == NULL) {
        RK_LOGE("rtsp_new_session failed");
        rtsp_del_demo(g_rtspDemo);
        g_rtspDemo = NULL;
        return RK_FAILURE;
    }
    
    RK_LOGI("RTSP server started:");
    RK_LOGI("  - Port: %d", RTSP_PORT);
    RK_LOGI("  - Path: %s", RTSP_PATH);
    RK_LOGI("  - URL: rtsp://<ip>:%d%s", RTSP_PORT, RTSP_PATH);
    
    return RK_SUCCESS;
}

static void deinit_rtsp_server(void) {
    if (g_rtspSession) {
        rtsp_del_session(g_rtspSession);
        g_rtspSession = NULL;
    }
    if (g_rtspDemo) {
        rtsp_del_demo(g_rtspDemo);
        g_rtspDemo = NULL;
    }
    RK_LOGI("RTSP server stopped");
}

// ==================== YUV 送帧线程 ====================

void *yuv_feed_thread(void *arg) {
    RK_S32 s32Ret;
    VIDEO_FRAME_INFO_S stFrame;
    MB_BLK mbBlk;
    RK_U64 frameSeq = 0;
    struct timespec timeout;
    int consecutive_timeouts = 0;
    int sem_value = 0;
    RK_BOOL timeout_occurred = RK_FALSE;  // 使用 RK_BOOL 代替 bool
    
    RK_LOGI("YUV feed thread started");
    RK_LOGI("Timeout handling: FULLY aligned with usb_app (always use old data on timeout)");
    RK_LOGI("Timeout: %d seconds (same as usb_app DIAG_TIMEOUT)", g_runCfg.shm_timeout_sec);
    
    while (!bquit) {
        timeout_occurred = RK_FALSE;  // 使用 RK_FALSE
        
        // 1. 等待数据 ready（与 usb_app 完全一致的超时处理）
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += g_runCfg.shm_timeout_sec;  // 2 秒，与 usb_app 一致
        
        // 【调试】立即打印循环状态
        if (frameSeq <= 10) {
            RK_LOGI("=== LOOP START: frame=%llu, bquit=%d ===", frameSeq, bquit);
        }
        
        // 检查信号量当前值（调试用）
        sem_getvalue(g_shmCtx.wait_sem, &sem_value);
        
        // 【调试】前100帧都打印，观察是否继续循环
        if (frameSeq < 100 || frameSeq % 30 == 0) {
            RK_LOGI("Before wait: frame=%llu, sem_value=%d, timeout=%ds", 
                    frameSeq, sem_value, g_runCfg.shm_timeout_sec);
        }
        
        s32Ret = sem_timedwait(g_shmCtx.wait_sem, &timeout);
        if (s32Ret < 0) {
            if (errno == ETIMEDOUT) {
                consecutive_timeouts++;
                timeout_occurred = RK_TRUE;  // 使用 RK_TRUE
                
                // ✅ 与 usb_app 完全一致：超时后打印日志，继续执行
                // usb_app 代码：if(sem_timedwait(...) <0 ) { LOG_PRINTF("timeout"); }
                // 然后直接继续执行，没有 continue！
                RK_LOGW("======timeout wait_aps_sem (count=%d, frame=%llu, errno=%d)", 
                        consecutive_timeouts, frameSeq, errno);
                // ⚠️ 关键：与 usb_app 完全一致，不 continue，总是继续处理！
            } else if (errno == EINTR) {
                continue;  // 被信号中断，继续
            } else {
                RK_LOGE("sem_timedwait failed: %s", strerror(errno));
                break;
            }
        } else {
            // sem_timedwait 成功
            consecutive_timeouts = 0;  // 重置超时计数
        }
        
        // 前100帧打印详细信息
        if (frameSeq < 100 || frameSeq % 30 == 0) {
            if (timeout_occurred) {
                RK_LOGI("After timeout: Will use old data from shm, frame=%llu", frameSeq);
            } else {
                RK_LOGI("After wait OK: Got new data from shm, frame=%llu", frameSeq);
            }
        }
        
        // 2. 从共享内存读取数据（与 usb_app 完全一致：即使超时也读取）
        // usb_app: 超时后直接 memcpy(iobuf[i].buf[i], global_aps_mmap + ..., ...)
        // 我们：超时后也继续分配 MB 并 memcpy
        
        // ✅ 2.1 读取元数据（包含时间戳）
        FRAME_METADATA_S metadata;
        memcpy(&metadata, g_shmCtx.pYuvData, sizeof(FRAME_METADATA_S));
        
        // 2.2 分配MB（与 usb_app 的本地缓冲区类似）
        if (frameSeq < 10) {
            RK_LOGI("Step 2.2: Allocating MB, frame=%llu", frameSeq);
        }
        
        mbBlk = RK_MPI_MB_GetMB(g_mbPool, YUV_ONLY_LEN, RK_TRUE);
        if (mbBlk == RK_NULL) {
            RK_LOGE("RK_MPI_MB_GetMB failed, frame=%llu", frameSeq);
            sem_post(g_shmCtx.done_sem);  // ⚠️ 与 usb_app 一致：总是 post done_sem
            usleep(10000);  // 等10ms再试
            continue;
        }
        
        if (frameSeq < 10) {
            RK_LOGI("Step 2.2: MB allocated OK, frame=%llu", frameSeq);
        }
        
        // 2.3 从共享内存复制YUV数据到MB（跳过元数据）
        // ✅ 与 usb_app 完全一致：memcpy(iobuf[i].buf[i], global_aps_mmap + ..., ...)
        uint8_t *pMbVirAddr = RK_MPI_MB_Handle2VirAddr(mbBlk);
        if (pMbVirAddr == NULL) {
            RK_LOGE("RK_MPI_MB_Handle2VirAddr failed, frame=%llu", frameSeq);
            RK_MPI_MB_ReleaseMB(mbBlk);
            sem_post(g_shmCtx.done_sem);  // ⚠️ 与 usb_app 一致：总是 post done_sem
            continue;
        }
        
        // 调试：打印时间戳（前10帧）
        if (frameSeq < 10) {
            RK_LOGI("Step 2.3: Metadata - PTS=%llu us (%.3f sec), Seq=%u, Size=%ux%u",
                    metadata.u64PTS, metadata.u64PTS / 1000000.0, 
                    metadata.u32FrameSeq, metadata.u32Width, metadata.u32Height);
        }
        
        // ✅ 从共享内存偏移位置复制YUV数据（跳过元数据）
        memcpy(pMbVirAddr, g_shmCtx.pYuvData + sizeof(FRAME_METADATA_S), YUV_ONLY_LEN);
        RK_MPI_SYS_MmzFlushCache(mbBlk, RK_FALSE);
        
        // ==================================================
        // 3. 输出部分：与 usb_app 不同（VENC+RTSP vs USB）
        // ==================================================
        // usb_app: io_prep_pwrite() + io_submit() -> USB
        // aps_rtsp_server_v2: RK_MPI_VENC_SendFrame() -> VENC -> RTSP
        
        // 3.1 填充 frame 信息
        memset(&stFrame, 0, sizeof(stFrame));
        stFrame.stVFrame.pMbBlk = mbBlk;
        stFrame.stVFrame.u32Width = YUV_WIDTH;
        stFrame.stVFrame.u32Height = YUV_HEIGHT;
        stFrame.stVFrame.u32VirWidth = YUV_WIDTH;
        stFrame.stVFrame.u32VirHeight = YUV_HEIGHT;
        stFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
        stFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;
        
        // ✅ 使用从共享内存读取的真实硬件时间戳
        stFrame.stVFrame.u64PTS = metadata.u64PTS;
        stFrame.stVFrame.u64PrivateData = metadata.u32FrameSeq;
        
        // ✅ 保存时间戳到全局变量，供 rtsp_send_thread 使用
        pthread_mutex_lock(&g_ptsMutex);
        g_lastMetadataPTS = metadata.u64PTS;
        g_lastMetadataSeq = metadata.u32FrameSeq;
        pthread_mutex_unlock(&g_ptsMutex);
        
        // 3.2 送入 VENC 编码（输出方式差异：VENC vs USB）
        s32Ret = RK_MPI_VENC_SendFrame(VENC_CHN_ID, &stFrame, 1000);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VENC_SendFrame failed: 0x%x, frame=%llu", s32Ret, frameSeq);
            // ⚠️ 诊断：查询 VENC 状态
            VENC_CHN_STATUS_S stVencStat;
            memset(&stVencStat, 0, sizeof(stVencStat));
            RK_MPI_VENC_QueryStatus(VENC_CHN_ID, &stVencStat);
            RK_LOGE("VENC status: left=%u, leftBytes=%u, leftPics=%u, curPacks=%u",
                    stVencStat.u32LeftStreamFrames, stVencStat.u32LeftStreamBytes,
                    stVencStat.u32LeftPics, stVencStat.u32CurPacks);
        } else {
            // 前100帧都打印，之后每30帧打印一次
            if (frameSeq < 100 || frameSeq % 30 == 0) {
                if (timeout_occurred) {
                    RK_LOGI("✓ Sent YUV frame %llu to VENC [timeout, using old data] (w=%d,h=%d)", 
                            frameSeq, YUV_WIDTH, YUV_HEIGHT);
                } else {
                    RK_LOGI("✓ Sent YUV frame %llu to VENC [new data] (w=%d,h=%d)", 
                            frameSeq, YUV_WIDTH, YUV_HEIGHT);
                }
            }
        }
        
        // 3.3 释放 MB
        if (frameSeq < 10) {
            RK_LOGI("Step 3.3: Releasing MB, frame=%llu", frameSeq);
        }
        RK_MPI_MB_ReleaseMB(mbBlk);
        
        // ==================================================
        // 4. 完成通知：与 usb_app 完全一致
        // ==================================================
        // usb_app: sem_post(send_done_sem);
        // aps_rtsp_server_v2: sem_post(g_shmCtx.done_sem);
        // ⚠️ 与 usb_app 一致：无论超时与否，总是 post done_sem
        if (frameSeq < 10) {
            RK_LOGI("Step 4: Posting done_sem, frame=%llu", frameSeq);
        }
        sem_post(g_shmCtx.done_sem);
        
        frameSeq++;
        
        // 【调试】前10帧都打印完成信息
        if (frameSeq <= 10) {
            RK_LOGI("=== Completed frame %llu, next frame will be %llu ===", 
                    frameSeq - 1, frameSeq);
        }
        
        // ✅ 每100帧打印一次时间戳信息
        if (frameSeq % 100 == 0) {
            RK_LOGI("APS Frame %llu: PTS=%llu us (%.3f sec)", 
                    frameSeq, metadata.u64PTS, metadata.u64PTS / 1000000.0);
        }
    }  // while (!bquit) 循环结束
    
    // 【调试】如果线程退出循环，打印原因
    RK_LOGE("YUV feed thread EXIT! bquit=%d, frameSeq=%llu", bquit, frameSeq);
    
    return NULL;
}

// ==================== RTSP 发送线程 ====================

void *rtsp_send_thread(void *arg) {
    RK_S32 s32Ret;
    VENC_STREAM_S stStream;
    VENC_PACK_S *pstPack = NULL;  // 修改：使用指针
    uint8_t sps_buf[256];
    uint8_t pps_buf[256];
    uint8_t sps_pps[512];
    int sps_len = 0;
    int pps_len = 0;
    int sps_pps_len = 0;
    RK_BOOL got_sps = RK_FALSE;
    RK_BOOL got_pps = RK_FALSE;
    RK_U64 u64Pts = 0;
    int empty_cnt = 0;
    
    RK_LOGI("RTSP send thread started");
    
    // ⚠️ 关键修复：为 pstPack 预分配内存（必须！否则 GetStream 会失败）
    pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    if (pstPack == NULL) {
        RK_LOGE("Failed to allocate memory for VENC_PACK_S");
        return NULL;
    }
    
    g_rtspStats.u64StartTime = TEST_COMM_GetNowUs();
    g_rtspStats.u64LastReportTime = g_rtspStats.u64StartTime;
    
    while (!bquit) {
        // 获取编码流
        memset(&stStream, 0, sizeof(VENC_STREAM_S));
        stStream.pstPack = pstPack;  // ⚠️ 关键：设置预分配的内存
        
        // ⚠️ 修复：减少超时时间到 100ms，避免在空缓冲时长时间阻塞
        s32Ret = RK_MPI_VENC_GetStream(VENC_CHN_ID, &stStream, 100);
        
        if (s32Ret != RK_SUCCESS) {
            if ((RK_U32)s32Ret == 0xA0048006 || s32Ret == RK_ERR_VENC_BUF_EMPTY) {
                empty_cnt++;
                if ((empty_cnt % 20) == 0) {
                    VENC_CHN_STATUS_S stStat;
                    memset(&stStat, 0, sizeof(stStat));
                    RK_MPI_VENC_QueryStatus(VENC_CHN_ID, &stStat);
                    RK_LOGD("VENC empty x%u, stat: left=%u curPacks=%u leftPics=%u",
                            empty_cnt, stStat.u32LeftStreamFrames, stStat.u32CurPacks, stStat.u32LeftPics);
                }
                // ⚠️ 诊断：如果 curPacks > 0 但是 GetStream 失败，说明有问题
                if (empty_cnt == 2000) {  // 2000 次后打印详细错误
                    VENC_CHN_STATUS_S stStat;
                    memset(&stStat, 0, sizeof(stStat));
                    RK_MPI_VENC_QueryStatus(VENC_CHN_ID, &stStat);
                    RK_LOGE("⚠️ VENC stuck! GetStream returns BUF_EMPTY but curPacks=%u, left=%u, leftPics=%u",
                            stStat.u32CurPacks, stStat.u32LeftStreamFrames, stStat.u32LeftPics);
                    RK_LOGE("⚠️ This suggests VENC internal error. Try restarting VENC or adjusting GOP/FPS.");
                    
                    // ⚠️ 尝试修复：请求 IDR 帧
                    RK_LOGW("Attempting recovery: requesting IDR frame...");
                    RK_MPI_VENC_RequestIDR(VENC_CHN_ID, RK_TRUE);
                    empty_cnt = 0;  // 重置计数，给它一次机会
                }
                continue;
            }
            RK_LOGE("RK_MPI_VENC_GetStream failed: 0x%x", s32Ret);
            continue;
        }
        
        empty_cnt = 0;  // 重置空计数
        
        if (stStream.u32PackCount == 0 || stStream.pstPack == NULL) {
            RK_LOGE("Invalid VENC stream");
            RK_MPI_VENC_ReleaseStream(VENC_CHN_ID, &stStream);
            continue;
        }
        
        // ⚠️ 调试：打印pack数量（前100帧）
        if (g_rtspStats.u64FrameCount < 100) {
            RK_LOGI("Stream u32PackCount=%u", stStream.u32PackCount);
        }
        
        // 处理每个包
        for (RK_U32 i = 0; i < stStream.u32PackCount; i++) {
            VENC_PACK_S *pPack = &stStream.pstPack[i];  // 修改：使用指针访问
            
            RK_U8 *pStreamData = (RK_U8 *)RK_MPI_MB_Handle2VirAddr(pPack->pMbBlk);
            if (pStreamData == NULL) {
                RK_LOGE("RK_MPI_MB_Handle2VirAddr failed");
                continue;
            }
            
            // ⚠️ 特殊处理：Rockchip VENC可能在IDR帧中内嵌SPS/PPS
            // 检查type=5(IDR)或type=7(SPS)的数据，尝试提取SPS/PPS
            if (!got_sps && (pPack->DataType.enH264EType == H264E_NALU_IDRSLICE || 
                             pPack->DataType.enH264EType == H264E_NALU_SPS)) {
                // 扫描数据中的NALU起始码，提取SPS/PPS
                RK_U8 *pData = pStreamData + pPack->u32Offset;
                RK_U32 dataLen = pPack->u32Len;
                
                // 扫描NAL单元（以 00 00 00 01 或 00 00 01 分隔）
                for (RK_U32 pos = 0; pos < dataLen - 4; pos++) {
                    // 查找起始码 00 00 00 01
                    if (pData[pos] == 0 && pData[pos+1] == 0 && pData[pos+2] == 0 && pData[pos+3] == 1) {
                        RK_U32 naluStart = pos + 4;
                        if (naluStart >= dataLen) continue;
                        
                        // 读取NALU类型（第一个字节的低5位）
                        RK_U8 naluType = pData[naluStart] & 0x1F;
                        
                        // 查找下一个起始码来确定NALU长度
                        RK_U32 naluEnd = dataLen;
                        for (RK_U32 nextPos = naluStart + 1; nextPos < dataLen - 3; nextPos++) {
                            if (pData[nextPos] == 0 && pData[nextPos+1] == 0 && 
                                (pData[nextPos+2] == 1 || (pData[nextPos+2] == 0 && pData[nextPos+3] == 1))) {
                                naluEnd = nextPos;
                                break;
                            }
                        }
                        
                        RK_U32 naluLen = naluEnd - naluStart;
                        
                        // 提取SPS (type=7) - 包含起始码
                        if (naluType == 7 && !got_sps) {
                            RK_U32 totalLen = 4 + naluLen;  // 起始码(4字节) + NALU数据
                            if (totalLen < sizeof(sps_buf)) {
                                // 复制起始码 + NALU数据
                                memcpy(sps_buf, &pData[pos], totalLen);
                                sps_len = totalLen;
                                got_sps = RK_TRUE;
                                RK_LOGI("✓ Got SPS from IDR frame, len=%d (with startcode)", sps_len);
                            }
                        }
                        // 提取PPS (type=8) - 包含起始码
                        else if (naluType == 8 && !got_pps) {
                            RK_U32 totalLen = 4 + naluLen;  // 起始码(4字节) + NALU数据
                            if (totalLen < sizeof(pps_buf)) {
                                // 复制起始码 + NALU数据
                                memcpy(pps_buf, &pData[pos], totalLen);
                                pps_len = totalLen;
                                got_pps = RK_TRUE;
                                RK_LOGI("✓ Got PPS from IDR frame, len=%d (with startcode)", pps_len);
                            }
                        }
                        
                        pos = naluEnd - 1;  // 跳到下一个NALU
                    }
                }
            }
            
            // 原有的SPS提取逻辑（作为fallback）
            if (!got_sps && pPack->DataType.enH264EType == H264E_NALU_SPS) {
                if (pPack->u32Len < sizeof(sps_buf)) {
                    memcpy(sps_buf, pStreamData + pPack->u32Offset, pPack->u32Len);
                    sps_len = pPack->u32Len;
                    got_sps = RK_TRUE;
                    RK_LOGI("✓ Got SPS, len=%d", sps_len);
                }
            }
            
            // 提取 PPS
            if (!got_pps && pPack->DataType.enH264EType == H264E_NALU_PPS) {
                if (pPack->u32Len < sizeof(pps_buf)) {
                    memcpy(pps_buf, pStreamData + pPack->u32Offset, pPack->u32Len);
                    pps_len = pPack->u32Len;
                    got_pps = RK_TRUE;
                    RK_LOGI("✓ Got PPS, len=%d", pps_len);
                }
            }
            
            // 调试：打印所有NALU类型（扩展到前100帧，以便找到SPS/PPS）
            if (g_rtspStats.u64FrameCount < 100) {
                RK_LOGI("VENC pack[%u]: type=%d, len=%u, got_sps=%d, got_pps=%d", 
                        i, pPack->DataType.enH264EType, pPack->u32Len, got_sps, got_pps);
            }
            
            // ⚠️ 特别注意：如果看到type=7或8，说明有SPS/PPS
            if (pPack->DataType.enH264EType == H264E_NALU_SPS || 
                pPack->DataType.enH264EType == H264E_NALU_PPS) {
                RK_LOGI("⚠️ FOUND: NALU type=%d (SPS=7, PPS=8), len=%u, pack[%u]",
                        pPack->DataType.enH264EType, pPack->u32Len, i);
            }
            
            // 设置 RTSP 视频参数
            if (got_sps && got_pps && sps_pps_len == 0) {
                if (sps_len + pps_len < sizeof(sps_pps)) {
                    memcpy(sps_pps, sps_buf, sps_len);
                    memcpy(sps_pps + sps_len, pps_buf, pps_len);
                    sps_pps_len = sps_len + pps_len;
                    
                    // 调试：打印SPS/PPS头部数据
                    RK_LOGI("SPS+PPS data header: [%02x %02x %02x %02x] [%02x %02x %02x %02x]",
                            sps_pps[0], sps_pps[1], sps_pps[2], sps_pps[3],
                            sps_pps[4], sps_pps[5], sps_pps[6], sps_pps[7]);
                    
                    pthread_mutex_lock(&g_rtspLock);
                    s32Ret = rtsp_set_video(g_rtspSession,
                                          RTSP_CODEC_ID_VIDEO_H264,
                                          sps_pps,
                                          sps_pps_len);
                    pthread_mutex_unlock(&g_rtspLock);
                    
                    if (s32Ret == 0) {
                        RK_LOGI("✓ RTSP video codec set successfully, SPS+PPS len=%d", sps_pps_len);
                    } else {
                        RK_LOGE("✗ rtsp_set_video FAILED with ret=%d", s32Ret);
                    }
                }
            }
            
            // 发送视频帧到 RTSP
            if (got_sps && got_pps) {
                // ✅ 使用硬件时间戳而非软件时间戳
                // 从 VENC 输出流获取 PTS（如果可用），否则使用全局变量中保存的时间戳
                pthread_mutex_lock(&g_ptsMutex);
                u64Pts = g_lastMetadataPTS;  // 使用 VI 层的硬件时间戳
                RK_U32 u32FrameSeq = g_lastMetadataSeq;
                pthread_mutex_unlock(&g_ptsMutex);
                
                // ⚠️ 验证：VENC 输出流是否包含 PTS
                if (stStream.pstPack[0].u64PTS != 0) {
                    // VENC 保留了输入 PTS，优先使用（更精确）
                    u64Pts = stStream.pstPack[0].u64PTS;
                    if (g_rtspStats.u64FrameCount < 10) {
                        RK_LOGI("✓ Using VENC output PTS: %llu us (%.3f sec)", 
                                u64Pts, u64Pts / 1000000.0);
                    }
                } else {
                    // VENC 未保留 PTS，使用全局变量
                    if (g_rtspStats.u64FrameCount < 10) {
                        RK_LOGI("⚠️ VENC PTS=0, using global metadata PTS: %llu us (%.3f sec)", 
                                u64Pts, u64Pts / 1000000.0);
                    }
                }
                
                pthread_mutex_lock(&g_rtspLock);
                s32Ret = rtsp_tx_video(g_rtspSession,
                                     pStreamData + pPack->u32Offset,
                                     pPack->u32Len,
                                     u64Pts);
                pthread_mutex_unlock(&g_rtspLock);
                
                // ⚠️ 修复：rtsp_tx_video 成功时返回发送的字节数（>0），失败时返回<=0
                if (s32Ret > 0) {
                    g_rtspStats.u64FrameCount++;
                    g_rtspStats.u64ByteCount += s32Ret;  // 使用实际发送的字节数
                } else {
                    g_rtspStats.u64ErrorCount++;
                    // 真正的失败才打印错误
                    if (g_rtspStats.u64ErrorCount <= 10) {
                        RK_LOGE("rtsp_tx_video REALLY failed: ret=%d, type=%d, len=%u, pts=%llu (error #%llu)",
                                s32Ret, pPack->DataType.enH264EType, pPack->u32Len, u64Pts, 
                                g_rtspStats.u64ErrorCount);
                    }
                }
            }
        }
        
        // 释放流
        RK_MPI_VENC_ReleaseStream(VENC_CHN_ID, &stStream);
        
        // 定期报告统计信息
        RK_U64 u64NowTime = TEST_COMM_GetNowUs();
        if (u64NowTime - g_rtspStats.u64LastReportTime > 5000000) {  // 5秒
            RK_U64 u64ElapsedTime = u64NowTime - g_rtspStats.u64StartTime;
            double dFps = g_rtspStats.u64FrameCount * 1000000.0 / u64ElapsedTime;
            double dBitrate = g_rtspStats.u64ByteCount * 8.0 / 1000000.0 / 
                            (u64ElapsedTime / 1000000.0);
            
            // ✅ 添加时间戳信息到统计报告
            pthread_mutex_lock(&g_ptsMutex);
            RK_U64 currentPTS = g_lastMetadataPTS;
            RK_U32 currentSeq = g_lastMetadataSeq;
            pthread_mutex_unlock(&g_ptsMutex);
            
            RK_LOGI("RTSP Stats: Frames=%llu, Bytes=%llu, Errors=%llu, FPS=%.1f, Bitrate=%.1f Mbps",
                    g_rtspStats.u64FrameCount, g_rtspStats.u64ByteCount,
                    g_rtspStats.u64ErrorCount, dFps, dBitrate);
            RK_LOGI("  Timestamp: PTS=%llu us (%.3f sec), FrameSeq=%u",
                    currentPTS, currentPTS / 1000000.0, currentSeq);
            
            g_rtspStats.u64LastReportTime = u64NowTime;
        }
    }
    
    // 释放内存
    if (pstPack) {
        free(pstPack);
    }
    
    RK_LOGI("RTSP send thread exited");
    return NULL;
}

// ==================== 主函数 ====================

int main(int argc, char **argv) {
    RK_S32 s32Ret;
    pthread_t yuv_thread, rtsp_thread;
    
    RK_LOGI("==================================================");
    RK_LOGI("  APS RTSP Server V2 - Shared Memory Based");
    RK_LOGI("==================================================");
    
    // 1. 解析参数
    parse_args(argc, argv);
    
    // 2. 注册信号处理
    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);
    
    // 3. 初始化 MPP 系统
    s32Ret = RK_MPI_SYS_Init();
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_SYS_Init failed: 0x%x", s32Ret);
        return -1;
    }
    RK_LOGI("MPP system initialized");
    
    // 4. 打开共享内存
    s32Ret = init_shared_memory(&g_shmCtx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("init_shared_memory failed");
        RK_LOGE("Please start apx003_mpi_sample first!");
        goto EXIT_SYS;
    }
    
    // 4.1 清空旧的信号量（防止堆积）
    int drained_wait_count = 0;
    int drained_done_count = 0;
    
    // 清空 wait_aps_sem
    while (sem_trywait(g_shmCtx.wait_sem) == 0) {
        drained_wait_count++;
    }
    
    // 清空 send_done_sem
    while (sem_trywait(g_shmCtx.done_sem) == 0) {
        drained_done_count++;
    }
    
    if (drained_wait_count > 0 || drained_done_count > 0) {
        RK_LOGI("Drained %d old wait_aps_sem, %d old send_done_sem", 
                drained_wait_count, drained_done_count);
    }
    
    // ⚠️ 关键修复：如果清空了 N 个 wait_aps_sem，需要 post N 个 send_done_sem
    // 这样可以"解救"可能被阻塞的 apxGetData
    if (drained_wait_count > 0) {
        RK_LOGI("Posting %d send_done_sem to unblock apxGetData...", drained_wait_count);
        for (int i = 0; i < drained_wait_count; i++) {
            sem_post(g_shmCtx.done_sem);
        }
        
        // 等待 apxGetData 恢复
        sleep(1);
        
        // 再次清空可能积压的 wait_aps_sem
        int extra_drained = 0;
        while (sem_trywait(g_shmCtx.wait_sem) == 0) {
            extra_drained++;
        }
        if (extra_drained > 0) {
            RK_LOGI("Drained %d more wait_aps_sem after unblocking apxGetData", extra_drained);
        }
    }
    
    // 检查最终状态
    int final_wait_val, final_done_val;
    sem_getvalue(g_shmCtx.wait_sem, &final_wait_val);
    sem_getvalue(g_shmCtx.done_sem, &final_done_val);
    RK_LOGI("After cleanup: wait_sem=%d, done_sem=%d", final_wait_val, final_done_val);
    
    // 4.2 等待 apxGetData 开始产生数据
    RK_LOGI("Waiting for apxGetData to start producing data...");
    RK_LOGI("(This may take 10-20 seconds for camera initialization)");
    int wait_val;
    for (int i = 0; i < 20 && !bquit; i++) {  // 最多等20秒
        sem_getvalue(g_shmCtx.wait_sem, &wait_val);
        if (wait_val > 0) {
            RK_LOGI("✓ Data ready after %d seconds (sem_value=%d)", i+1, wait_val);
            break;
        }
        if (i == 0 || i % 5 == 4) {
            RK_LOGI("  Still waiting... (%d/20s, sem_value=%d)", i+1, wait_val);
        }
        sleep(1);
    }
    
    if (bquit) {
        RK_LOGW("Interrupted by user during wait");
        goto EXIT_SHM;
    }
    
    sem_getvalue(g_shmCtx.wait_sem, &wait_val);
    if (wait_val == 0) {
        RK_LOGW("WARNING: No data ready after 20 seconds.");
        RK_LOGW("apxGetData may have issues. Check /tmp/apxGetData.log");
        RK_LOGW("Will continue waiting in thread (timeout=%ds per frame, same as usb_app)...", g_runCfg.shm_timeout_sec);
        RK_LOGW("Note: Unlike before, thread will ALWAYS continue on timeout (usb_app style)");
    }
    
    // 5. 创建 MB Pool
    g_mbPool = create_yuv_mb_pool(YUV_WIDTH, YUV_HEIGHT, MB_POOL_CNT);
    if (g_mbPool == MB_INVALID_POOLID) {
        RK_LOGE("create_yuv_mb_pool failed");
        goto EXIT_SHM;
    }
    
    // 6. 创建 VENC
    g_vencCfg.s32ChnId = VENC_CHN_ID;
    s32Ret = create_venc(&g_vencCfg);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create_venc failed");
        goto EXIT_MBPOOL;
    }
    
    // 7. 启动 VENC 接收
    s32Ret = start_venc_recv(&g_vencCfg);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("start_venc_recv failed");
        goto EXIT_VENC;
    }
    
    // 8. 初始化 RTSP
    s32Ret = init_rtsp_server();
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("init_rtsp_server failed");
        goto EXIT_VENC_RECV;
    }
    
    // 9. 启动 YUV 送帧线程
    s32Ret = pthread_create(&yuv_thread, NULL, yuv_feed_thread, NULL);
    if (s32Ret != 0) {
        RK_LOGE("pthread_create(yuv_feed_thread) failed");
        goto EXIT_RTSP;
    }
    
    // 10. 启动 RTSP 发送线程
    s32Ret = pthread_create(&rtsp_thread, NULL, rtsp_send_thread, NULL);
    if (s32Ret != 0) {
        RK_LOGE("pthread_create(rtsp_send_thread) failed");
        bquit = RK_TRUE;
        pthread_join(yuv_thread, NULL);
        goto EXIT_RTSP;
    }
    
    RK_LOGI("==================================================");
    RK_LOGI("  APS RTSP Server V2 Started Successfully!");
    RK_LOGI("  - Reading YUV from: %s", SHM_NAME);
    RK_LOGI("  - RTSP URL: rtsp://<ip>:%d%s", RTSP_PORT, RTSP_PATH);
    RK_LOGI("==================================================");
    
    // 11. 主循环
    while (!bquit) {
        if (g_rtspDemo) {
            pthread_mutex_lock(&g_rtspLock);
            rtsp_do_event(g_rtspDemo);
            pthread_mutex_unlock(&g_rtspLock);
        }
        usleep(10000);  // 10ms
    }
    
    // 12. 清理资源
    RK_LOGI("Cleaning up...");
    
    pthread_join(rtsp_thread, NULL);
    pthread_join(yuv_thread, NULL);
    
EXIT_RTSP:
    deinit_rtsp_server();
EXIT_VENC_RECV:
    RK_MPI_VENC_StopRecvFrame(VENC_CHN_ID);
EXIT_VENC:
    destroy_venc(&g_vencCfg);
EXIT_MBPOOL:
    if (g_mbPool != MB_INVALID_POOLID) {
        RK_MPI_MB_DestroyPool(g_mbPool);
    }
EXIT_SHM:
    deinit_shared_memory(&g_shmCtx);
EXIT_SYS:
    RK_MPI_SYS_Exit();
    
    RK_LOGI("APS RTSP Server V2 exited");
    return 0;
}

