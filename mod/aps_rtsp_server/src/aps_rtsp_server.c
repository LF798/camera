/*
 * APS RTSP Server
 * 功能：从 APS 摄像头获取数据，编码为 H264，通过 RTSP 服务传输
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "rk_defines.h"
#include "rk_debug.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_cal.h"
#include "rk_common.h"
#include "rk_comm_vi.h"
#include "rk_comm_vpss.h"
#include "rk_comm_venc.h"
#include "test_common.h"
#include "test_comm_utils.h"

#include "rtsp_demo_2.h"

// 运行配置（用于分步排错）
typedef struct {
    int stop_after_step;      // 0:不停止；1~7：在对应步骤完成后停止初始化
    int vi_channel_override;  // -1:使用默认宏；>=0：覆盖 VI 通道
    int disable_vpss_backup;  // 1:禁用VPSS备份帧；0:启用
    int venc_stream_bufcnt;   // 0:默认；>0：覆盖VENC码流缓存数量
    int bitrate_override;     // 0:默认；>0：覆盖VENC码率
    int no_rtsp;              // 1:不初始化RTSP（仅验证管线）
    int vi_bind_use_pipe;     // 1: VI->VPSS 绑定使用 pipeId 作为 s32DevId；0: 使用 devId
    int probe_vpss_once;      // 1: 绑定后探测一次 VPSS RESIZE 是否有帧
} RUN_CFG_S;

static RUN_CFG_S g_run = {0};

static void parse_args(int argc, char **argv) {
    g_run.stop_after_step = 0;
    g_run.vi_channel_override = -1;
    g_run.disable_vpss_backup = 1;      // 默认禁用，便于定位
    g_run.venc_stream_bufcnt = 6;       // 默认提高到6，减轻背压
    g_run.bitrate_override = 0;
    g_run.no_rtsp = 0;
    g_run.vi_bind_use_pipe = 0;         // 默认与 apx003_mpi_sample 一致：使用 devId 绑定
    g_run.probe_vpss_once = 1;          // 默认探测一次 VPSS RESIZE 是否有帧

    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--phase=", 8)) {
            g_run.stop_after_step = atoi(argv[i] + 8);
        } else if (!strncmp(argv[i], "--vi-chn=", 9)) {
            g_run.vi_channel_override = atoi(argv[i] + 9);
        } else if (!strcmp(argv[i], "--enable-backup")) {
            g_run.disable_vpss_backup = 0;
        } else if (!strncmp(argv[i], "--venc-buf=", 11)) {
            g_run.venc_stream_bufcnt = atoi(argv[i] + 11);
        } else if (!strncmp(argv[i], "--bitrate=", 10)) {
            g_run.bitrate_override = atoi(argv[i] + 10);
        } else if (!strcmp(argv[i], "--no-rtsp")) {
            g_run.no_rtsp = 1;
        } else if (!strcmp(argv[i], "--vi-bind-pipe")) {
            g_run.vi_bind_use_pipe = 1;
        } else if (!strcmp(argv[i], "--vi-bind-dev")) {
            g_run.vi_bind_use_pipe = 0;
        } else if (!strcmp(argv[i], "--probe-vpss")) {
            g_run.probe_vpss_once = 1;
        } else if (!strcmp(argv[i], "--no-probe")) {
            g_run.probe_vpss_once = 0;
        }
    }
}

// APS 配置参数
#define APX_APS_DEV_ID              (0)
#define APX_APS_CHANNEL_ID          (1)
#define APX_APS_SRC_WIDTH           (1632)
#define APX_APS_SRC_HEIGHT          (1224)
#define APX_APS_DST_WIDTH           (768)
#define APX_APS_DST_HEIGHT          (608)
#define APX_APS_VPSS_GROUP          (0)
#define APX_APS_VPSS_RESIZE_CHANNEL (0)
#define APX_APS_VPSS_SHOW_CHANNEL   (1)

// VENC 配置参数
#define VENC_CHN_ID                 (0)
#define VENC_BITRATE                (4000000)  // 改为 4Mbps，提供更好的质量
#define VENC_FPS                    (30)
#define VENC_GOP                    (30)

// RTSP 配置参数
#define RTSP_PORT                   (8554)
#define RTSP_PATH                   "/live"

// 性能统计结构
typedef struct {
    RK_U64 u64FrameCount;        // 总帧数
    RK_U64 u64ByteCount;         // 总字节数
    RK_U64 u64ErrorCount;        // 错误计数
    RK_U64 u64LastReportTime;    // 上次报告时间
    RK_U64 u64StartTime;         // 启动时间
} RTSP_STATS_S;

static RK_BOOL bquit = RK_FALSE;
static pthread_mutex_t g_rtspLock = PTHREAD_MUTEX_INITIALIZER;
static RTSP_STATS_S g_rtspStats = {0};

// 信号处理
static void sigterm_handler(int sig) {
    bquit = RK_TRUE;
}

// VI 上下文结构
typedef struct {
    RK_S32 devId;
    RK_S32 pipeId;
    RK_S32 channelId;
    RK_U32 width;
    RK_U32 height;
    VI_DEV_ATTR_S stDevAttr;
    VI_CHN_ATTR_S stChnAttr;
} VI_CTX_S;

// VPSS 配置结构
typedef struct {
    RK_S32 s32GrpId;
    RK_U32 u32ChnCnt;
    VPSS_GRP_ATTR_S stGrpAttr;
    VPSS_CHN_ATTR_S stChnAttr[VPSS_MAX_CHN_NUM];
} VPSS_CFG_S;

// VENC 配置结构
typedef struct {
    RK_S32 s32ChnId;
    VENC_CHN_ATTR_S stChnAttr;
} VENC_CFG_S;

// 全局变量
static VI_CTX_S g_viCtx;
static VPSS_CFG_S g_vpssCfg;
static VENC_CFG_S g_vencCfg;
static rtsp_demo_handle g_rtspDemo = NULL;
static rtsp_session_handle g_rtspSession = NULL;

// 创建内存池（未使用，保留以备将来需要）
#if 0
static MB_POOL create_mb_pool(RK_U32 width, RK_U32 height, RK_U32 count, PIXEL_FORMAT_E format)
{
    PIC_BUF_ATTR_S stPicBufAttr;
    MB_PIC_CAL_S stMbPicCalResult;
    MB_POOL_CONFIG_S stMbPoolCfg;
    RK_S32 s32Ret;

    stPicBufAttr.u32Width = width;
    stPicBufAttr.u32Height = height;
    stPicBufAttr.enPixelFormat = format;
    stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;

    s32Ret = RK_MPI_CAL_COMM_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("get picture buffer size failed. err 0x%x", s32Ret);
        return MB_INVALID_POOLID;
    }

    memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
    stMbPoolCfg.u64MBSize = stMbPicCalResult.u32MBSize;
    stMbPoolCfg.u32MBCnt = count;
    stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
    stMbPoolCfg.bPreAlloc = RK_TRUE;

    return RK_MPI_MB_CreatePool(&stMbPoolCfg);
}
#endif

// 创建 VI
static RK_S32 create_vi(VI_CTX_S *pCtx)
{
    RK_S32 s32Ret = RK_FAILURE;

   // 配置通道属性（对齐 apx003_mpi_sample 的 APS VI 配置）
    memset(&pCtx->stChnAttr, 0, sizeof(VI_CHN_ATTR_S));
    pCtx->stChnAttr.stSize.u32Width  = pCtx->width;
    pCtx->stChnAttr.stSize.u32Height = pCtx->height;
    pCtx->stChnAttr.enPixelFormat    = RK_FMT_YUV420SP;
    pCtx->stChnAttr.stIspOpt.u32BufCount    = 8;
    pCtx->stChnAttr.stIspOpt.enMemoryType   = VI_V4L2_MEMORY_TYPE_DMABUF;
    pCtx->stChnAttr.stIspOpt.enCaptureType  = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
    pCtx->stChnAttr.u32Depth                = 0;
    pCtx->stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    pCtx->stChnAttr.stFrameRate.s32DstFrameRate = -1;

    // 关键：为 APS 通道设置 entityName，与 sample 保持一致
    // ch0 → rkisp_mainpath, ch1 → rkisp_selfpath
    if (pCtx->channelId == 0) {
        strncpy((char *)pCtx->stChnAttr.stIspOpt.aEntityName,
                "rkisp_mainpath",
                sizeof(pCtx->stChnAttr.stIspOpt.aEntityName) - 1);
    } else {
        strncpy((char *)pCtx->stChnAttr.stIspOpt.aEntityName,
                "rkisp_selfpath",
                sizeof(pCtx->stChnAttr.stIspOpt.aEntityName) - 1);
    }

    // 获取设备属性，如果不存在则设置
    s32Ret = RK_MPI_VI_GetDevAttr(pCtx->devId, &pCtx->stDevAttr);
    if (s32Ret == RK_ERR_VI_NOT_CONFIG) {
        // 设备未配置，需要设置
        memset(&pCtx->stDevAttr, 0, sizeof(VI_DEV_ATTR_S));
        s32Ret = RK_MPI_VI_SetDevAttr(pCtx->devId, &pCtx->stDevAttr);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevAttr failed, ret: 0x%x", s32Ret);
            return s32Ret;
        }
    }

    // 启用设备
    s32Ret = RK_MPI_VI_EnableDev(pCtx->devId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_EnableDev failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    // 绑定管道
    VI_DEV_BIND_PIPE_S stBindPipe;
    memset(&stBindPipe, 0, sizeof(VI_DEV_BIND_PIPE_S));
    stBindPipe.u32Num = 1;
    stBindPipe.PipeId[0] = pCtx->pipeId;
    s32Ret = RK_MPI_VI_SetDevBindPipe(pCtx->devId, &stBindPipe);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_SetDevBindPipe failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    // 设置通道属性
    s32Ret = RK_MPI_VI_SetChnAttr(pCtx->pipeId, pCtx->channelId, &pCtx->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_SetChnAttr failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    // 启用通道
    s32Ret = RK_MPI_VI_EnableChn(pCtx->pipeId, pCtx->channelId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_EnableChn failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    RK_LOGI("VI created successfully: Dev=%d, Pipe=%d, Chn=%d, %dx%d",
            pCtx->devId, pCtx->pipeId, pCtx->channelId, pCtx->width, pCtx->height);

    return RK_SUCCESS;
}

// 销毁 VI
static RK_S32 destroy_vi(VI_CTX_S *pCtx)
{
    RK_MPI_VI_DisableChn(pCtx->pipeId, pCtx->channelId);
    RK_MPI_VI_DisableDev(pCtx->devId);
    return RK_SUCCESS;
}

// 创建 VPSS
static RK_S32 create_vpss(VPSS_CFG_S *pCfg)
{
    RK_S32 s32Ret = RK_SUCCESS;
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};

    // 创建 VPSS 组
    s32Ret = RK_MPI_VPSS_CreateGrp(pCfg->s32GrpId, &pCfg->stGrpAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VPSS_CreateGrp failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    // 配置并启用通道
    for (RK_U32 i = 0; i < pCfg->u32ChnCnt; i++) {
        s32Ret = RK_MPI_VPSS_SetChnAttr(pCfg->s32GrpId, VpssChn[i], &pCfg->stChnAttr[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VPSS_SetChnAttr failed, chn=%d, ret: 0x%x", i, s32Ret);
            return s32Ret;
        }

        s32Ret = RK_MPI_VPSS_EnableChn(pCfg->s32GrpId, VpssChn[i]);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VPSS_EnableChn failed, chn=%d, ret: 0x%x", i, s32Ret);
            return s32Ret;
        }
    }

    // 备份帧根据运行配置开关
    if (!g_run.disable_vpss_backup) {
        s32Ret = RK_MPI_VPSS_EnableBackupFrame(pCfg->s32GrpId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VPSS_EnableBackupFrame failed, ret: 0x%x", s32Ret);
            return s32Ret;
        }
    } else {
        RK_LOGI("VPSS backup frame DISABLED by run config");
    }

    // 启动 VPSS 组
    s32Ret = RK_MPI_VPSS_StartGrp(pCfg->s32GrpId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VPSS_StartGrp failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    RK_LOGI("VPSS created successfully: Grp=%d, ChnCnt=%d", pCfg->s32GrpId, pCfg->u32ChnCnt);
    return RK_SUCCESS;
}

// 销毁 VPSS
static RK_S32 destroy_vpss(VPSS_CFG_S *pCfg)
{
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};

    RK_MPI_VPSS_StopGrp(pCfg->s32GrpId);

    for (RK_U32 i = 0; i < pCfg->u32ChnCnt; i++) {
        RK_MPI_VPSS_DisableChn(pCfg->s32GrpId, VpssChn[i]);
    }

    RK_MPI_VPSS_DisableBackupFrame(pCfg->s32GrpId);
    RK_MPI_VPSS_DestroyGrp(pCfg->s32GrpId);

    return RK_SUCCESS;
}

// 创建 VENC
static RK_S32 create_venc(VENC_CFG_S *pCfg)
{
    RK_S32 s32Ret = RK_SUCCESS;

    // 配置编码通道属性
    memset(&pCfg->stChnAttr, 0, sizeof(VENC_CHN_ATTR_S));

    // 编码器属性
    pCfg->stChnAttr.stVencAttr.enType = RK_VIDEO_ID_AVC;  // H264
    pCfg->stChnAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    pCfg->stChnAttr.stVencAttr.u32PicWidth = APX_APS_DST_WIDTH;
    pCfg->stChnAttr.stVencAttr.u32PicHeight = APX_APS_DST_HEIGHT;
    pCfg->stChnAttr.stVencAttr.u32VirWidth = APX_APS_DST_WIDTH;
    pCfg->stChnAttr.stVencAttr.u32VirHeight = APX_APS_DST_HEIGHT;
    pCfg->stChnAttr.stVencAttr.u32StreamBufCnt = (g_run.venc_stream_bufcnt > 0) ? g_run.venc_stream_bufcnt : 3;
    // 显式设置 H.264 Profile，避免默认路径触发固件兼容问题
    pCfg->stChnAttr.stVencAttr.u32Profile = H264E_PROFILE_MAIN;

    // 码率控制属性 (CBR 模式，使用更高的码率 4Mbps)
    pCfg->stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.u32Gop = VENC_GOP;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.u32BitRate = (g_run.bitrate_override > 0) ? g_run.bitrate_override : VENC_BITRATE;  // 可覆盖
    pCfg->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
    pCfg->stChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = VENC_FPS;

    // 创建编码通道
    s32Ret = RK_MPI_VENC_CreateChn(pCfg->s32ChnId, &pCfg->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_CreateChn failed, ret: 0x%x", s32Ret);
        return s32Ret;
    }

    // 注意：不在此时启动接收帧。应在 VPSS→VENC 绑定完成后再启动，以避免驱动内部空源导致异常。

    RK_LOGI("VENC created (not started yet): Chn=%d, %dx%d, BitRate=%d, FPS=%d",
            pCfg->s32ChnId, APX_APS_DST_WIDTH, APX_APS_DST_HEIGHT,
            VENC_BITRATE, VENC_FPS);

    return RK_SUCCESS;
}

// 销毁 VENC
static RK_S32 destroy_venc(VENC_CFG_S *pCfg)
{
    RK_MPI_VENC_StopRecvFrame(pCfg->s32ChnId);
    RK_MPI_VENC_DestroyChn(pCfg->s32ChnId);
    return RK_SUCCESS;
}

// RTSP 发送线程
void *rtsp_send_thread(void *arg)
{
    RK_S32 s32Ret = RK_SUCCESS;
    VENC_STREAM_S stStream;
    VENC_PACK_S *pstPack = NULL;  // ⚠️ 修复：使用指针而不是结构体
    uint8_t sps_buf[256];
    uint8_t pps_buf[256];
    uint8_t sps_pps[512];
    int sps_len = 0;
    int pps_len = 0;
    int sps_pps_len = 0;
    RK_BOOL got_sps = RK_FALSE;
    RK_BOOL got_pps = RK_FALSE;
    RK_U64 u64Pts = 0;
    int empty_cnt = 0;  // 连续空流计数

    RK_LOGI("RTSP send thread started");

    // ⚠️ 关键修复：为 pstPack 预分配内存（必须！否则 GetStream 会失败）
    pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    if (pstPack == NULL) {
        RK_LOGE("Failed to allocate memory for VENC_PACK_S");
        return NULL;
    }

    // 基本防御：确保 RTSP 句柄有效
    if (g_rtspDemo == NULL || g_rtspSession == NULL) {
        RK_LOGE("RTSP handles not ready in send thread, demo=%p, session=%p",
                g_rtspDemo, g_rtspSession);
        free(pstPack);
        return NULL;
    }

    g_rtspStats.u64StartTime = TEST_COMM_GetNowUs();
    g_rtspStats.u64LastReportTime = g_rtspStats.u64StartTime;

    while (!bquit) {
        // ⚠️ 修复：减少超时时间到 100ms，避免在空缓冲时长时间阻塞
        memset(&stStream, 0, sizeof(VENC_STREAM_S));
        stStream.pstPack = pstPack;  // ⚠️ 关键：设置预分配的内存
        s32Ret = RK_MPI_VENC_GetStream(VENC_CHN_ID, &stStream, 100);
        if (s32Ret != RK_SUCCESS) {
            // 某些固件返回 0xA0048006 表示暂无码流/超时，或标准空缓冲码，视为正常
            if ((RK_U32)s32Ret == 0xA0048006 || s32Ret == RK_ERR_VENC_BUF_EMPTY) {
                empty_cnt++;
                if ((empty_cnt % 20) == 0) {
                    VENC_CHN_STATUS_S stStat; memset(&stStat, 0, sizeof(stStat));
                    RK_S32 qret = RK_MPI_VENC_QueryStatus(VENC_CHN_ID, &stStat);
                    if (qret == RK_SUCCESS) {
                        RK_LOGI("VENC empty x%u, stat: left=%u curPacks=%u, leftRecv=%u, leftEnc=%u",
                                empty_cnt, stStat.u32LeftStreamFrames, stStat.u32CurPacks,
                                stStat.u32LeftRecvPics, stStat.u32LeftEncPics);
                    }
                }
                continue;
            }
            // 其他错误再打印
            RK_LOGE("RK_MPI_VENC_GetStream failed, ret: 0x%x", s32Ret);
            continue;
        }

        empty_cnt = 0;  // 重置空计数

        // 防御性检查：有些异常情况下可能返回成功但 pack 指针为空
        if (stStream.u32PackCount == 0 || stStream.pstPack == NULL) {
            RK_LOGE("VENC stream pack is invalid, count=%u, pstPack=%p",
                    stStream.u32PackCount, stStream.pstPack);
            RK_MPI_VENC_ReleaseStream(VENC_CHN_ID, &stStream);
            continue;
        }

        empty_cnt = 0;  // 重置空计数

        // 处理每个包
        for (RK_U32 i = 0; i < stStream.u32PackCount; i++) {
            VENC_PACK_S *pPack = &stStream.pstPack[i];  // ⚠️ 修复：使用指针访问

            // 获取虚拟地址
            RK_U8 *pStreamData = (RK_U8 *)RK_MPI_MB_Handle2VirAddr(pPack->pMbBlk);
            if (pStreamData == NULL) {
                RK_LOGE("RK_MPI_MB_Handle2VirAddr failed");
                continue;
            }

            // ⚠️ 增强：从IDR帧中提取SPS/PPS（某些编码器会将SPS/PPS内嵌在IDR帧中）
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
                                memcpy(sps_buf, &pData[pos], totalLen);
                                sps_len = totalLen;
                    got_sps = RK_TRUE;
                                RK_LOGI("✓ Got SPS from IDR frame, len=%d (with startcode)", sps_len);
                }
            }
                        // 提取PPS (type=8) - 包含起始码
                        else if (naluType == 8 && !got_pps) {
                            RK_U32 totalLen = 4 + naluLen;
                            if (totalLen < sizeof(pps_buf)) {
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

            // 原有的SPS/PPS提取逻辑（作为fallback）
            if (!got_sps && pPack->DataType.enH264EType == H264E_NALU_SPS) {
                if (pPack->u32Len < sizeof(sps_buf)) {
                    memcpy(sps_buf, pStreamData + pPack->u32Offset, pPack->u32Len);
                    sps_len = pPack->u32Len;
                    got_sps = RK_TRUE;
                    RK_LOGI("✓ Got SPS, len=%d", sps_len);
                }
            }

            if (!got_pps && pPack->DataType.enH264EType == H264E_NALU_PPS) {
                if (pPack->u32Len < sizeof(pps_buf)) {
                    memcpy(pps_buf, pStreamData + pPack->u32Offset, pPack->u32Len);
                    pps_len = pPack->u32Len;
                    got_pps = RK_TRUE;
                    RK_LOGI("✓ Got PPS, len=%d", pps_len);
                }
            }

            // 当同时获得 SPS 和 PPS 时，组合并设置 RTSP 参数
            if (got_sps && got_pps && sps_pps_len == 0) {
                if (sps_len + pps_len < sizeof(sps_pps)) {
                    memcpy(sps_pps, sps_buf, sps_len);
                    memcpy(sps_pps + sps_len, pps_buf, pps_len);
                    sps_pps_len = sps_len + pps_len;

                    // 调试：打印SPS/PPS头部数据
                    RK_LOGI("SPS+PPS data header: [%02x %02x %02x %02x] [%02x %02x %02x %02x]",
                            sps_pps[0], sps_pps[1], sps_pps[2], sps_pps[3],
                            sps_pps[4], sps_pps[5], sps_pps[6], sps_pps[7]);

                    // 设置 RTSP 视频参数 (添加线程同步)
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

            // 发送视频帧到 RTSP（所有类型的数据，包括 SPS/PPS）
            if (got_sps && got_pps) {
                u64Pts = rtsp_get_reltime();
                    pthread_mutex_lock(&g_rtspLock);
                    s32Ret = rtsp_tx_video(g_rtspSession,
                                     pStreamData + pPack->u32Offset,
                                     pPack->u32Len,
                                         u64Pts);
                    pthread_mutex_unlock(&g_rtspLock);
                    
                // ⚠️ 关键修复：rtsp_tx_video 成功时返回发送的字节数（>0），失败时返回<=0
                if (s32Ret > 0) {
                        g_rtspStats.u64FrameCount++;
                    g_rtspStats.u64ByteCount += s32Ret;  // 使用实际发送的字节数
                } else {
                    g_rtspStats.u64ErrorCount++;
                    // 真正的失败才打印错误（限制日志数量）
                    if (g_rtspStats.u64ErrorCount <= 10) {
                        RK_LOGE("rtsp_tx_video REALLY failed: ret=%d, type=%d, len=%u, pts=%llu (error #%llu)",
                                s32Ret, pPack->DataType.enH264EType, pPack->u32Len, u64Pts, 
                                g_rtspStats.u64ErrorCount);
                    }
                }
            }
        }

        // 释放流
        s32Ret = RK_MPI_VENC_ReleaseStream(VENC_CHN_ID, &stStream);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VENC_ReleaseStream failed, ret: 0x%x", s32Ret);
        }

        // 定期报告性能统计信息
        RK_U64 u64NowTime = TEST_COMM_GetNowUs();
        if (u64NowTime - g_rtspStats.u64LastReportTime > 5000000) {  // 5秒
            RK_U64 u64ElapsedTime = u64NowTime - g_rtspStats.u64StartTime;
            double dFps = g_rtspStats.u64FrameCount * 1000000.0 / u64ElapsedTime;
            double dBitrate = g_rtspStats.u64ByteCount * 8.0 / 1000000.0 / 
                            (u64ElapsedTime / 1000000.0);
            
            RK_LOGI("RTSP Stats: Frames=%llu, Bytes=%llu, Errors=%llu, "
                    "FPS=%.1f, Bitrate=%.1f Mbps",
                    g_rtspStats.u64FrameCount,
                    g_rtspStats.u64ByteCount,
                    g_rtspStats.u64ErrorCount,
                    dFps,
                    dBitrate);
            
            g_rtspStats.u64LastReportTime = u64NowTime;
        }
    }

    // 清理预分配的内存
    if (pstPack != NULL) {
        free(pstPack);
        pstPack = NULL;
    }

    RK_LOGI("RTSP send thread exited");
    return NULL;
}

// 初始化 APS RTSP 服务
RK_S32 aps_rtsp_init(void)
{
    RK_S32 s32Ret = RK_SUCCESS;
    MPP_CHN_S stViChn, stVpssChn, stVencChn;

    RK_LOGI(">>>> aps_rtsp_init start <<<<");

    // 1. 初始化 VI
    memset(&g_viCtx, 0, sizeof(VI_CTX_S));
    g_viCtx.devId = APX_APS_DEV_ID;
    g_viCtx.pipeId = APX_APS_DEV_ID;
    g_viCtx.channelId = (g_run.vi_channel_override >= 0) ? g_run.vi_channel_override : APX_APS_CHANNEL_ID;
    g_viCtx.width = APX_APS_SRC_WIDTH;
    g_viCtx.height = APX_APS_SRC_HEIGHT;

    RK_LOGI("[STEP 1] Creating VI...");
    s32Ret = create_vi(&g_viCtx);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create_vi failed");
        return s32Ret;
    }
    RK_LOGI("[STEP 1] VI created successfully.");
    if (g_run.stop_after_step == 1) {
        RK_LOGW("Stop after STEP 1 (VI) by --phase=1");
        return RK_SUCCESS;
    }

    // 2. 初始化 VPSS
    memset(&g_vpssCfg, 0, sizeof(VPSS_CFG_S));
    g_vpssCfg.s32GrpId = APX_APS_VPSS_GROUP;
    // 参考 apx003_mpi_sample 的 VPSS 设计：同时配置缩放通道和显示通道，保持与
    // 既有数据流一致（RESIZE channel 供编码，SHOW channel 可保留以便后续扩展）
    g_vpssCfg.u32ChnCnt = 2;

    // VPSS 组属性
    g_vpssCfg.stGrpAttr.u32MaxW = 4096;
    g_vpssCfg.stGrpAttr.u32MaxH = 4096;
    g_vpssCfg.stGrpAttr.enPixelFormat = RK_FMT_YUV420SP;
    g_vpssCfg.stGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    g_vpssCfg.stGrpAttr.stFrameRate.s32DstFrameRate = -1;
    g_vpssCfg.stGrpAttr.enCompressMode = COMPRESS_MODE_NONE;

    // VPSS 通道属性 (RESIZE_CHANNEL)
    // RESIZE 通道（供编码使用）
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enChnMode = VPSS_CHN_MODE_USER;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enDynamicRange = DYNAMIC_RANGE_SDR8;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enPixelFormat = RK_FMT_YUV420SP;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].stFrameRate.s32SrcFrameRate = -1;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].stFrameRate.s32DstFrameRate = -1;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Width = APX_APS_DST_WIDTH;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Height = APX_APS_DST_HEIGHT;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enCompressMode = COMPRESS_MODE_NONE;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32FrameBufCnt = 8;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Depth = 5;

    // SHOW 通道（与 sample 一致，保留 1632x1224 输出，便于联调 VO/调试）
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].enChnMode = VPSS_CHN_MODE_USER;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].enDynamicRange = DYNAMIC_RANGE_SDR8;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].enPixelFormat = RK_FMT_YUV420SP;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].stFrameRate.s32SrcFrameRate = -1;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].stFrameRate.s32DstFrameRate = -1;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].u32Width = APX_APS_SRC_WIDTH;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].u32Height = APX_APS_SRC_HEIGHT;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].enCompressMode = COMPRESS_MODE_NONE;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].u32FrameBufCnt = 8;
    g_vpssCfg.stChnAttr[APX_APS_VPSS_SHOW_CHANNEL].u32Depth = 5;

    RK_LOGI("[STEP 2] Creating VPSS...");
    s32Ret = create_vpss(&g_vpssCfg);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create_vpss failed");
        destroy_vi(&g_viCtx);
        return s32Ret;
    }
    RK_LOGI("[STEP 2] VPSS created successfully.");
    if (g_run.stop_after_step == 2) {
        RK_LOGW("Stop after STEP 2 (VPSS) by --phase=2");
        return RK_SUCCESS;
    }

    // 3. 初始化 VENC
    memset(&g_vencCfg, 0, sizeof(VENC_CFG_S));
    g_vencCfg.s32ChnId = VENC_CHN_ID;

    RK_LOGI("[STEP 3] Creating VENC...");
    s32Ret = create_venc(&g_vencCfg);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("create_venc failed");
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return s32Ret;
    }
    RK_LOGI("[STEP 3] VENC created successfully.");
    if (g_run.stop_after_step == 3) {
        RK_LOGW("Stop after STEP 3 (VENC create) by --phase=3");
        return RK_SUCCESS;
    }

    // 4. 绑定 VI → VPSS（可切换使用 devId 或 pipeId 作为 VI 端 s32DevId）
    stViChn.enModId = RK_ID_VI;
    stViChn.s32DevId = g_viCtx.devId;
    stViChn.s32ChnId = g_viCtx.channelId;

    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = g_vpssCfg.s32GrpId;
    stVpssChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;

    RK_LOGI("[STEP 4] Binding VI(dev=%d, ch=%d) to VPSS(grp=%d, ch=%d)...",
        stViChn.s32DevId, stViChn.s32ChnId,
        stVpssChn.s32DevId, stVpssChn.s32ChnId);
    s32Ret = RK_MPI_SYS_Bind(&stViChn, &stVpssChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("VI to VPSS bind failed, ret: 0x%x", s32Ret);
        destroy_venc(&g_vencCfg);
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return s32Ret;
    }
    RK_LOGI("[STEP 4] VI to VPSS bind OK.");

    // 可选：绑定后探测一次 VPSS RESIZE 是否有帧
    if (g_run.probe_vpss_once) {
        VIDEO_FRAME_INFO_S stDbgFrm;
        memset(&stDbgFrm, 0, sizeof(stDbgFrm));
        RK_S32 pr = RK_MPI_VPSS_GetChnFrame(g_vpssCfg.s32GrpId, APX_APS_VPSS_RESIZE_CHANNEL, &stDbgFrm, 500);
        if (pr == RK_SUCCESS) {
            RK_LOGI("[PROBE] VPSS RESIZE got one frame: %ux%u pts=%llu", stDbgFrm.stVFrame.u32Width,
                    stDbgFrm.stVFrame.u32Height, stDbgFrm.stVFrame.u64PTS);
            RK_MPI_VPSS_ReleaseChnFrame(g_vpssCfg.s32GrpId, APX_APS_VPSS_RESIZE_CHANNEL, &stDbgFrm);
        } else {
            RK_LOGW("[PROBE] VPSS RESIZE no frame within 500ms, ret=0x%x", pr);
        }
    }
    if (g_run.stop_after_step == 4) {
        RK_LOGW("Stop after STEP 4 (Bind VI->VPSS) by --phase=4");
        return RK_SUCCESS;
    }

    // 5. 绑定 VPSS → VENC
    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = g_vpssCfg.s32GrpId;
    stVpssChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;

    stVencChn.enModId = RK_ID_VENC;
    stVencChn.s32DevId = 0;
    stVencChn.s32ChnId = VENC_CHN_ID;

    RK_LOGI("[STEP 5] Binding VPSS to VENC...");
    s32Ret = RK_MPI_SYS_Bind(&stVpssChn, &stVencChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("VPSS to VENC bind failed, ret: 0x%x", s32Ret);
        RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
        destroy_venc(&g_vencCfg);
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return s32Ret;
    }
    RK_LOGI("[STEP 5] VPSS to VENC bind OK.");
    if (g_run.stop_after_step == 5) {
        RK_LOGW("Stop after STEP 5 (Bind VPSS->VENC) by --phase=5");
        return RK_SUCCESS;
    }

    // 5.1 绑定完成后，先显式设置 H.264 Profile，再启动接收帧，避免默认路径触发兼容问题
    do {
        VENC_CHN_ATTR_S stAttr;
        memset(&stAttr, 0, sizeof(stAttr));
        RK_S32 r = RK_MPI_VENC_GetChnAttr(g_vencCfg.s32ChnId, &stAttr);
        if (r == RK_SUCCESS) {
            if (stAttr.stVencAttr.enType == RK_VIDEO_ID_AVC) {
                stAttr.stVencAttr.u32Profile = H264E_PROFILE_MAIN; // 可改为 H264E_PROFILE_BASELINE 尝试
            }
            r = RK_MPI_VENC_SetChnAttr(g_vencCfg.s32ChnId, &stAttr);
            if (r != RK_SUCCESS) {
                RK_LOGE("RK_MPI_VENC_SetChnAttr (profile) failed: 0x%x", r);
            } else {
                RK_LOGI("VENC H264 profile set to MAIN successfully");
            }
        } else {
            RK_LOGE("RK_MPI_VENC_GetChnAttr failed: 0x%x", r);
        }
    } while (0);

    RK_LOGI("[STEP 5.1] Start VENC receiving frames...");
    VENC_RECV_PIC_PARAM_S stRecvParam;
    memset(&stRecvParam, 0, sizeof(stRecvParam));
    stRecvParam.s32RecvPicNum = -1; // 无限接收
    s32Ret = RK_MPI_VENC_StartRecvFrame(g_vencCfg.s32ChnId, &stRecvParam);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VENC_StartRecvFrame failed after bind, ret: 0x%x", s32Ret);
        RK_MPI_SYS_UnBind(&stVpssChn, &stVencChn);
        RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
        destroy_venc(&g_vencCfg);
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return s32Ret;
    }

    // 给管线一点时间稳定
    usleep(100000); // 100ms

    // 启动后强制一次 IDR，便于尽快产出关键帧
    RK_S32 r_idr = RK_MPI_VENC_RequestIDR(g_vencCfg.s32ChnId, RK_TRUE);
    if (r_idr != RK_SUCCESS) {
        RK_LOGW("RK_MPI_VENC_RequestIDR failed: 0x%x", r_idr);
    }

    // 给管线更多时间产生首帧
    usleep(200000); // 200ms

    // 6. 初始化 RTSP
    RK_LOGI("[STEP 6] Initializing RTSP server...");
    g_rtspDemo = rtsp_new_demo(RTSP_PORT);
    if (g_rtspDemo == NULL) {
        RK_LOGE("rtsp_new_demo failed");
        RK_MPI_SYS_UnBind(&stVpssChn, &stVencChn);
        RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
        destroy_venc(&g_vencCfg);
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return RK_FAILURE;
    }

    g_rtspSession = rtsp_new_session(g_rtspDemo, RTSP_PATH);
    if (g_rtspSession == NULL) {
        RK_LOGE("rtsp_new_session failed");
        rtsp_del_demo(g_rtspDemo);
        RK_MPI_SYS_UnBind(&stVpssChn, &stVencChn);
        RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
        destroy_venc(&g_vencCfg);
        destroy_vpss(&g_vpssCfg);
        destroy_vi(&g_viCtx);
        return RK_FAILURE;
    }

    RK_LOGI("RTSP server started on port %d, path: %s", RTSP_PORT, RTSP_PATH);
    RK_LOGI(">>>> aps_rtsp_init success <<<<");

    return RK_SUCCESS;
}

// 清理 APS RTSP 服务
RK_S32 aps_rtsp_deinit(void)
{
    MPP_CHN_S stViChn, stVpssChn, stVencChn;

    // 解绑
    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = g_vpssCfg.s32GrpId;
    stVpssChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;

    stVencChn.enModId = RK_ID_VENC;
    stVencChn.s32DevId = 0;
    stVencChn.s32ChnId = VENC_CHN_ID;
    RK_MPI_SYS_UnBind(&stVpssChn, &stVencChn);

    stViChn.enModId = RK_ID_VI;
    stViChn.s32DevId = g_viCtx.devId;
    stViChn.s32ChnId = g_viCtx.channelId;
    RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);

    // 销毁 RTSP
    if (g_rtspSession) {
        rtsp_del_session(g_rtspSession);
        g_rtspSession = NULL;
    }
    if (g_rtspDemo) {
        rtsp_del_demo(g_rtspDemo);
        g_rtspDemo = NULL;
    }

    // 销毁模块
    destroy_venc(&g_vencCfg);
    destroy_vpss(&g_vpssCfg);
    destroy_vi(&g_viCtx);

    return RK_SUCCESS;
}

int main(int argc, char **argv)
{
    RK_S32 s32Ret = RK_SUCCESS;
    pthread_t rtsp_thread;

    // 解析调试参数
    parse_args(argc, argv);
    RK_LOGI("RunCfg: phase=%d, vi-chn=%d, disable_backup=%d, venc_buf=%d, bitrate=%d, no_rtsp=%d",
            g_run.stop_after_step, g_run.vi_channel_override, g_run.disable_vpss_backup,
            g_run.venc_stream_bufcnt, g_run.bitrate_override, g_run.no_rtsp);

    // 注册信号处理
    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);

    // 初始化 MPP 系统
    s32Ret = RK_MPI_SYS_Init();
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_SYS_Init failed, ret: 0x%x", s32Ret);
        return -1;
    }

    // 初始化 APS RTSP 服务
    s32Ret = aps_rtsp_init();
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("aps_rtsp_init failed");
        RK_MPI_SYS_Exit();
        return -1;
    }

    if (!g_run.no_rtsp) {
        // 创建 RTSP 发送线程
        s32Ret = pthread_create(&rtsp_thread, NULL, rtsp_send_thread, NULL);
        if (s32Ret != 0) {
            RK_LOGE("pthread_create failed");
            aps_rtsp_deinit();
            RK_MPI_SYS_Exit();
            return -1;
        }

        RK_LOGI("APS RTSP Server started successfully!");
        RK_LOGI("RTSP URL: rtsp://<ip>:%d%s", RTSP_PORT, RTSP_PATH);

        // 主循环
        while (!bquit) {
            // 处理 RTSP 事件 (添加线程同步)
            if (g_rtspDemo) {
                pthread_mutex_lock(&g_rtspLock);
                rtsp_do_event(g_rtspDemo);
                pthread_mutex_unlock(&g_rtspLock);
            }
            usleep(10000);  // 10ms
        }

        // 等待线程结束
        pthread_join(rtsp_thread, NULL);
    } else {
        RK_LOGW("RTSP disabled by --no-rtsp; entering idle loop (Ctrl+C to exit)");
        while (!bquit) usleep(100000);
    }

    // 清理
    aps_rtsp_deinit();
    RK_MPI_SYS_Exit();

    RK_LOGI("APS RTSP Server exited");
    return 0;
}


