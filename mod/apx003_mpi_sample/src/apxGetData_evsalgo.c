#include <stdio.h>
#include <sys/poll.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <semaphore.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "rk_defines.h"
#include "rk_debug.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_vo.h"
#include "rk_mpi_pvs.h"
#include "rk_mpi_rgn.h"
#include "rk_common.h"
#include "rk_comm_rgn.h"
#include "rk_comm_vi.h"
#include "rk_comm_vo.h"
#include "rk_comm_pvs.h"
#include "test_common.h"
#include "test_comm_utils.h"
#include "test_comm_argparse.h"
#include "test_comm_app_vdec.h"
#include "rk_mpi_cal.h"
#include "rk_mpi_mmz.h"
#include "test_mod_vpss.h"

#include "arm_neon.h"
#include "alp_inference_api.h"
#include "alp_denoise_api.h"
#include "time.h"
#include "unistd.h"

#include "shmfifo.h"                                            


// Color Format ARGB8888
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_RED       0xFFFF0000
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_ORANGE    0xFFFF4500
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF



#define TEST_VENC_MAX 2
#define TEST_WITH_FD 0
#define TEST_WITH_FD_SWITCH 0

#undef DBG_MOD_ID
#define DBG_MOD_ID  RK_ID_VI

// for 356x vo
#define RK356X_VO_DEV_HD0 0
#define RK356X_VO_DEV_HD1 1
#define RK356X_VOP_LAYER_CLUSTER_0 0
#define RK356X_VOP_LAYER_CLUSTER_1 2
#define RK356X_VOP_LAYER_ESMART_0 4
#define RK356X_VOP_LAYER_ESMART_1 5
#define RK356X_VOP_LAYER_SMART_0 6
#define RK356X_VOP_LAYER_SMART_1 7


#define APX_APS_DEV_ID              (0)
#define APX_APS_CHANNEL_ID          (1)

#define APX_EVS_DEV_ID              (1)
#define APX_EVS_CHANNEL_ID          (1)



#define APX_K2_EVS_WIDTH            (768)
#define APX_K2_EVS_HEIGHT           (608)
#define APX_K2_EVS_SUB_WIDTH        (384)
#define APX_K2_EVS_SUB_HEIGHT       (304)
#define APX_K2_EVS_SUB_FRAME_NUM    (4)
#define APX_K2_EVS_MERGE_FRAME_NUM  (4) //合并大帧数量
#define APX_K2_EVS_DATA_HEAD        (0x0000FFFF)
#define APX_K2_EVS_DATA_HEAD_LEN    (16)
#define APX_K2_EVS_NO_EVENT_VALUE   (127)

#define APX_K2_EVS_MERGE_FRAME_NUM_Algo  (4) //合并大帧数量
#define APX_K2_EVS_NO_EVENT_VALUE_Algo   (0)

#define APX_K2_EVS_RAW_WIDTH       (4096)
#define APX_K2_EVS_RAW_HEIGHT      (256)
#define APX_K2_EVS_RAW_MERGE_NUM   (8)

#define APX_K2_EVS_ALGO_WIDTH       (768)
#define APX_K2_EVS_ALGO_HEIGHT      (608)


#define APX_EVS_VPSS_GROUP              (0)
#define APX_EVS_VPSS_GROUP_NUM          (1)
#define APX_EVS_VPSS_SHOW_CHANNEL       (0)
#define APX_EVS_VPSS_ALGO_CHANNEL       (1)


#define APX_APS_VPSS_GROUP              (1)
#define APX_APS_VPSS_GROUP_NUM          (1)
#define APX_APS_VPSS_RESIZE_CHANNEL     (0)


#define APX_APS_SRC_WIDTH               (1632)
#define APX_APS_SRC_HEIGHT              (1224)
#define APX_APS_DST_WIDTH               (768)
#define APX_APS_DST_HEIGHT              (608)

#define APX_APS_PVS_CHANNEL             (0)
#define APX_EVS_PVS_CHANNEL             (1)

#define APX_IMG_SHOW_WIDTH              (640)
#define APX_IMG_SHOW_HEIGHT             (480)

#define EVS_IMG_SHOW_WIDTH              (640)
#define EVS_IMG_SHOW_HEIGHT             (480)

#define USB
#define VO

// 定义日志开关
#define ENABLE_LOG 1  // 1 表示开启日志，0 表示关闭日志

// 定义日志宏
#if ENABLE_LOG
    #define LOG_PRINTF(...) printf(__VA_ARGS__)
#else
    #define LOG_PRINTF(...)
#endif

#define SHM_KEY 0x1234         /* Key for shared memory segment */
#define BLOCKS 3

u_int8_t denoise;//evs denoise set

typedef struct {
    u_int32_t flag;
    u_int32_t denoise;
    u_int32_t seq;
    u_int32_t size;
    uint8_t received_buff[64]; // 接收数据缓冲区
} __attribute__((aligned(16))) config;

typedef enum {
    IMAGE_FORMAT_GRAY8,
    IMAGE_FORMAT_RGB888,
    IMAGE_FORMAT_RGBA8888,
    IMAGE_FORMAT_YUV420SP_NV21,
    IMAGE_FORMAT_YUV420SP_NV12,
} image_format_t;



typedef struct rkVPSS_CFG_S {
    const char *dstFilePath;
    RK_S32 s32DevId;
    RK_S32 s32ChnId;
    RK_U32 u32VpssChnCnt;
    VPSS_GRP_ATTR_S stGrpVpssAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
} VPSS_CFG_S;

typedef struct rkRGN_CFG_S {
    RGN_ATTR_S stRgnAttr;
    RGN_CHN_ATTR_S stRgnChnAttr;
} RGN_CFG_S;

typedef enum rkTestVIMODE_E {
    TEST_VI_MODE_VI_FRAME_ONLY = 0,
    TEST_VI_MODE_BIND_VENC = 1,
    TEST_VI_MODE_BIND_VENC_MULTI = 2,
    TEST_VI_MODE_BIND_VPSS_BIND_VENC = 3,
    TEST_VI_MODE_BIND_VO = 4,
    TEST_VI_MODE_MUTI_VI = 5,
    TEST_VI_MODE_VI_STREAM_ONLY = 6,
    TEST_VI_MODE_BIND_VDEC_BIND_VO = 7,
} TEST_VI_MODE_E;

typedef struct _rkMpiVICtx {
    RK_S32 width;
    RK_S32 height;
    RK_S32 devId;
    RK_S32 pipeId;
    RK_S32 channelId;
    RK_S32 loopCountSet;
    RK_S32 selectFd;
    RK_BOOL bFreeze;
    RK_BOOL bEnRgn;
    RK_S32 s32RgnCnt;
    RK_S32 rgnType;
    RK_BOOL bUserPicEnabled;
    RK_BOOL bGetConnecInfo;
    RK_BOOL bGetEdid;
    RK_BOOL bSrcChange;
    RK_BOOL bSetEdid;
    COMPRESS_MODE_E enCompressMode;
    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    VI_CHN_ATTR_S stChnAttr;
    VI_SAVE_FILE_INFO_S stDebugFile;
    VIDEO_FRAME_INFO_S stViFrame;
    VI_CHN_STATUS_S stChnStatus;
    VI_USERPIC_ATTR_S stUsrPic;
    TEST_VI_MODE_E enMode;
    const char *aEntityName;
    // for vi
    RGN_CFG_S stViRgn;
    //for vpss
    MB_POOL MbPool;
    
    VENC_STREAM_S stFrame[TEST_VENC_MAX];
    VPSS_CFG_S stVpssCfg;
    // for vo
    VO_LAYER s32VoLayer;
    VO_DEV s32VoDev;
    // for stream
    RK_CODEC_ID_E enCodecId;

    pthread_t ImgHandleThread;
    pthread_t ImgShowThread;

    config evsConfig;

    // alp_box_api_t AlgoOutBox[ALP_MAX_OUTPUT_NUM];
    // RK_U32 AlgoOutPutNUm;
} TEST_VI_CTX_S;


typedef struct _rkMpiPvsTestCtx {
    RK_S32                s32DevId;
    RK_S32                s32ChnId;

    // source parameters
    RK_U32                u32SrcWidth;
    RK_U32                u32SrcHeight;
    RK_U32                u32SrcVirWidth;
    RK_U32                u32SrcVirHeight;
    RK_U32                u32SrcBufferSize;
    RK_S32                s32SrcFrameRate;
    RK_S32                s32RecvThreshold;
    PIXEL_FORMAT_E        enSrcPixelFormat;
    COMPRESS_MODE_E       enSrcCompressMode;

    // dest parameters
    RK_S32                s32StitchMode;
    RK_S32                s32StitchFrmCnt;
    PVS_DEV_ATTR_S        stDevAttr;
    VIDEO_PROC_DEV_TYPE_E enVProcDev;

    RK_U32                u32RCNum;
    RK_S32                s32LoopCount;
    RK_U32                u32TestMode;
    RK_U32                u32TotalChn;
    RK_S32               *s32RuningCnt;
    pthread_t            *pSendFrameThreads;
} TEST_PVS_CTX_S;




TEST_PVS_CTX_S ApxPvsCtx;
TEST_VI_CTX_S * pApsCtx = NULL;
TEST_VI_CTX_S * pEvsCtx = NULL;


RK_S32 ApxCreatePvs(RK_U32 Width, RK_U32 Height);
RK_S32 ApxPvsChannelStart(RK_U32 ChannelId, RK_U32 X, RK_U32 Y, RK_U32 Width, RK_U32 Height);
TEST_VI_CTX_S * ApxCreateVi(uint32_t DevId, uint32_t ChId, uint32_t W, uint32_t H, uint32_t Depth, char* pEntName, PIXEL_FORMAT_E PixelFormat);
RK_S32 ApxDeleteVi(TEST_VI_CTX_S * pCtx);



static RK_BOOL bquit = RK_FALSE;
static void sigterm_handler(int sig) {
    bquit = RK_TRUE;
}

typedef struct {
    time_t startTime;  // 开始时间
    int duration;      // 持续时间（秒）
    int isRunning;     // 标志位：计时器是否运行
} ApxTimer_s;


ApxTimer_s EvsShowTimer;
ApxTimer_s ApsShowTimer;
RK_U32 EvsShowEnable = 0;
RK_U32 ApsShowEnable = 0;


// 启动计时器
void startTimer(ApxTimer_s* timer, int duration) {
    timer->startTime = time(NULL);  // 记录当前时间为开始时间
    timer->duration = duration;     // 设置持续时间
    timer->isRunning = 1;           // 设置计时器为运行状态
    // RK_LOGI("Timer started!\n");
}

void stopTimer(ApxTimer_s* timer) {
    timer->isRunning = 0;  // 设置计时器为停止状态
    // RK_LOGI("Timer stopped!\n");
}


uint32_t updateTimer(ApxTimer_s* timer) {
    if (timer->isRunning) {
        time_t currentTime = time(NULL);  // 获取当前时间
        double elapsed = difftime(currentTime, timer->startTime);  // 计算经过的时间

        // 如果经过时间大于等于持续时间，停止计时器
        if (elapsed >= timer->duration) {
            stopTimer(timer);
            return 1;
        } else {
            // RK_LOGI("Time elapsed: %.0f seconds\n", elapsed);
            return 0;
        }
    } else {
        return 1;
    }
}



static RK_S32 create_vpss(VPSS_CFG_S *pstVpssCfg, RK_S32 s32Grp, RK_S32 s32OutChnNum) {
    RK_S32 s32Ret = RK_SUCCESS;
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };
    VPSS_CROP_INFO_S stCropInfo;

    s32Ret = RK_MPI_VPSS_CreateGrp(s32Grp, &pstVpssCfg->stGrpVpssAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    for (RK_S32 i = 0; i < s32OutChnNum; i++) {
        s32Ret = RK_MPI_VPSS_SetChnAttr(s32Grp, VpssChn[i], &pstVpssCfg->stVpssChnAttr[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
        s32Ret = RK_MPI_VPSS_EnableChn(s32Grp, VpssChn[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
    }

    s32Ret = RK_MPI_VPSS_EnableBackupFrame(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VPSS_StartGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    return  RK_SUCCESS;
}

static RK_S32 destory_vpss(RK_S32 s32Grp, RK_S32 s32OutChnNum) {
    RK_S32 s32Ret = RK_SUCCESS;
    VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = { VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3 };

    s32Ret = RK_MPI_VPSS_StopGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    for (RK_S32 i = 0; i < s32OutChnNum; i++) {
        s32Ret = RK_MPI_VPSS_DisableChn(s32Grp, VpssChn[i]);
        if (s32Ret != RK_SUCCESS) {
            return s32Ret;
        }
    }

    s32Ret = RK_MPI_VPSS_DisableBackupFrame(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VPSS_DestroyGrp(s32Grp);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    return  RK_SUCCESS;
}



MB_POOL MbPoolCreate(uint32_t u32Width, uint32_t u32Height, uint32_t u32Num, PIXEL_FORMAT_E enPixelFormat)
{
    RK_S32 s32Ret;
    PIC_BUF_ATTR_S stPicBufAttr;
    MB_PIC_CAL_S stMbPicCalResult;

    stPicBufAttr.u32Width = u32Width;
    stPicBufAttr.u32Height = u32Height;
    stPicBufAttr.enPixelFormat = enPixelFormat;
    stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
    s32Ret = RK_MPI_CAL_COMM_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("get picture buffer size failed. err 0x%x", s32Ret);
        return MB_INVALID_POOLID;
    }

    MB_POOL_CONFIG_S stMbPoolCfg;
    memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
    stMbPoolCfg.u64MBSize = stMbPicCalResult.u32MBSize;
    stMbPoolCfg.u32MBCnt = u32Num;
    stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
    stMbPoolCfg.bPreAlloc = RK_TRUE;

    return RK_MPI_MB_CreatePool(&stMbPoolCfg);
}



static uint32_t ApxEvs_2bitToByte(uint8_t* pSrc, uint8_t* pDst, uint32_t Width, uint32_t Height)
{
    uint32_t EvsSubFrameMemSize = Width*Height/4;
    uint8x16_t vMask = vdupq_n_u8(0x03);
    uint8x16x4_t vDst;
    uint8x16_t vSrc; 

    for(uint32_t i = 0; i < EvsSubFrameMemSize; i+=16) {
        vSrc = vld1q_u8(pSrc);

        vDst.val[0] = vandq_u8(vSrc, vMask);
        vDst.val[0] = vshlq_n_u8(vDst.val[0], 1);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[1] = vandq_u8(vSrc, vMask);
        vDst.val[1] = vshlq_n_u8(vDst.val[1], 1);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[2] = vandq_u8(vSrc, vMask);
        vDst.val[2] = vshlq_n_u8(vDst.val[2], 1);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[3] = vandq_u8(vSrc, vMask);
        vDst.val[3] = vshlq_n_u8(vDst.val[3], 1);

        vst4q_u8(pDst, vDst);

        pSrc += 16;
        pDst += 64;
    }

    return 0;
}

static uint32_t ApxEvs_2bitToByte_Algo(uint8_t* pSrc, uint8_t* pDst, uint32_t Width, uint32_t Height)
{
    uint32_t EvsSubFrameMemSize = Width*Height/4;
    uint8x16_t vMask = vdupq_n_u8(0x03);
    uint8x16x4_t vDst;
    uint8x16_t vSrc; 

    for(uint32_t i = 0; i < EvsSubFrameMemSize; i+=16) {
        vSrc = vld1q_u8(pSrc);

        vDst.val[0] = vandq_u8(vSrc, vMask);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[1] = vandq_u8(vSrc, vMask);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[2] = vandq_u8(vSrc, vMask);

        vSrc = vshrq_n_u8(vSrc, 2);
        vDst.val[3] = vandq_u8(vSrc, vMask);

        vst4q_u8(pDst, vDst);

        pSrc += 16;
        pDst += 64;
    }

    return 0;
}

static uint32_t ApxEvs_Merge(uint8_t* pEvsSubPixelData[], uint8_t* pEvsMergePixelData, uint32_t MergeCount)
{

    if(MergeCount < APX_K2_EVS_MERGE_FRAME_NUM) {
        uint8x16_t vSubPixel_0;
        uint8x16_t vSubPixel_1;
        uint8x16_t vSubPixel_2;
        uint8x16_t vSubPixel_3;
        uint8x16x2_t vMergePixel_0;
        uint8x16x2_t vMergePixel_1;
        uint32_t TempNeonSubWidth = APX_K2_EVS_SUB_WIDTH/16;


//        RK_LOGE("%s, %d height = %d", __FUNCTION__, __LINE__, APX_K2_EVS_SUB_HEIGHT);

        for(uint32_t y = 0; y < APX_K2_EVS_SUB_HEIGHT; y++) {

            uint32_t TempMergeWidth = 2*y*APX_K2_EVS_WIDTH;
            uint8_t* pEvsMergeLine0 = pEvsMergePixelData + TempMergeWidth;
            uint8_t* pEvsMergeLine1 = pEvsMergePixelData + (TempMergeWidth + APX_K2_EVS_WIDTH);

            uint32_t TempSubWidth = y*APX_K2_EVS_SUB_WIDTH;
            uint8_t* pEvsSubPixelLine_0 = pEvsSubPixelData[0] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_1 = pEvsSubPixelData[1] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_2 = pEvsSubPixelData[2] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_3 = pEvsSubPixelData[3] + TempSubWidth;

//            RK_LOGE("%s, %d, y=%d", __FUNCTION__, __LINE__, y);
#if 1
            for (uint32_t x = 0; x < TempNeonSubWidth; x++) { 
                //RK_LOGE("%s, %d", __FUNCTION__, __LINE__);
                vSubPixel_0 = vld1q_u8(pEvsSubPixelLine_0);
                vSubPixel_1 = vld1q_u8(pEvsSubPixelLine_1);
                vSubPixel_2 = vld1q_u8(pEvsSubPixelLine_2);
                vSubPixel_3 = vld1q_u8(pEvsSubPixelLine_3);

                vMergePixel_0 = vld2q_u8(pEvsMergeLine0);
                vMergePixel_1 = vld2q_u8(pEvsMergeLine1);

                vMergePixel_0.val[0] = vorrq_u8(vMergePixel_0.val[0], vSubPixel_0);  //or
                vMergePixel_0.val[1] = vorrq_u8(vMergePixel_0.val[1], vSubPixel_1);
                vMergePixel_1.val[0] = vorrq_u8(vMergePixel_1.val[0], vSubPixel_2);
                vMergePixel_1.val[1] = vorrq_u8(vMergePixel_1.val[1], vSubPixel_3);

                vst2q_u8(pEvsMergeLine0, vMergePixel_0);
                vst2q_u8(pEvsMergeLine1, vMergePixel_1);

                pEvsSubPixelLine_0 += 16;
                pEvsSubPixelLine_1 += 16;
                pEvsSubPixelLine_2 += 16;
                pEvsSubPixelLine_3 += 16;

                pEvsMergeLine0 += 32;
                pEvsMergeLine1 += 32;
            }
#endif
        }
    }else {
        uint8x16_t vSubPixel_0;
        uint8x16_t vSubPixel_1;
        uint8x16_t vSubPixel_2;
        uint8x16_t vSubPixel_3;
        uint8x16x2_t vMergePixel_0;
        uint8x16x2_t vMergePixel_1;
        uint8x16_t vMul = vdupq_n_u8(APX_K2_EVS_NO_EVENT_VALUE);
        uint8x16_t vEor = vdupq_n_u8(0x01);
        uint8x16_t vAdd = vdupq_n_u8(0x01);

        uint32_t TempNeonSubWidth = APX_K2_EVS_SUB_WIDTH/16;

        for(uint32_t y = 0; y < APX_K2_EVS_SUB_HEIGHT; y++) {

            uint32_t TempMergeWidth = 2*y*APX_K2_EVS_WIDTH;
            uint8_t* pEvsMergeLine0 = pEvsMergePixelData + TempMergeWidth;
            uint8_t* pEvsMergeLine1 = pEvsMergePixelData + (TempMergeWidth + APX_K2_EVS_WIDTH);

            uint32_t TempSubWidth = y*APX_K2_EVS_SUB_WIDTH;
            uint8_t* pEvsSubPixelLine_0 = pEvsSubPixelData[0] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_1 = pEvsSubPixelData[1] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_2 = pEvsSubPixelData[2] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_3 = pEvsSubPixelData[3] + TempSubWidth;

            for (uint32_t x = 0; x < TempNeonSubWidth; x++) { 
                vSubPixel_0 = vld1q_u8(pEvsSubPixelLine_0);
                vSubPixel_1 = vld1q_u8(pEvsSubPixelLine_1);
                vSubPixel_2 = vld1q_u8(pEvsSubPixelLine_2);
                vSubPixel_3 = vld1q_u8(pEvsSubPixelLine_3);

                vMergePixel_0 = vld2q_u8(pEvsMergeLine0);
                vMergePixel_1 = vld2q_u8(pEvsMergeLine1);

                vMergePixel_0.val[0] = vorrq_u8(vMergePixel_0.val[0], vSubPixel_0);  //or
                vMergePixel_0.val[0] = vshrq_n_u8(vMergePixel_0.val[0], 1);
                vMergePixel_0.val[0] = veorq_u8(vMergePixel_0.val[0], vEor);
                vMergePixel_0.val[0] = vaddq_u8(vMergePixel_0.val[0], vAdd);
                vMergePixel_0.val[0] = vshrq_n_u8(vMergePixel_0.val[0], 1);
                vMergePixel_0.val[0] = vmulq_u8(vMergePixel_0.val[0], vMul);

                vMergePixel_0.val[1] = vorrq_u8(vMergePixel_0.val[1], vSubPixel_1);
                vMergePixel_0.val[1] = vshrq_n_u8(vMergePixel_0.val[1], 1);
                vMergePixel_0.val[1] = veorq_u8(vMergePixel_0.val[1], vEor);
                vMergePixel_0.val[1] = vaddq_u8(vMergePixel_0.val[1], vAdd);
                vMergePixel_0.val[1] = vshrq_n_u8(vMergePixel_0.val[1], 1);
                vMergePixel_0.val[1] = vmulq_u8(vMergePixel_0.val[1], vMul);

                vMergePixel_1.val[0] = vorrq_u8(vMergePixel_1.val[0], vSubPixel_2);
                vMergePixel_1.val[0] = vshrq_n_u8(vMergePixel_1.val[0], 1);
                vMergePixel_1.val[0] = veorq_u8(vMergePixel_1.val[0], vEor);
                vMergePixel_1.val[0] = vaddq_u8(vMergePixel_1.val[0], vAdd);
                vMergePixel_1.val[0] = vshrq_n_u8(vMergePixel_1.val[0], 1);
                vMergePixel_1.val[0] = vmulq_u8(vMergePixel_1.val[0], vMul);

                vMergePixel_1.val[1] = vorrq_u8(vMergePixel_1.val[1], vSubPixel_3);
                vMergePixel_1.val[1] = vshrq_n_u8(vMergePixel_1.val[1], 1);
                vMergePixel_1.val[1] = veorq_u8(vMergePixel_1.val[1], vEor);
                vMergePixel_1.val[1] = vaddq_u8(vMergePixel_1.val[1], vAdd);
                vMergePixel_1.val[1] = vshrq_n_u8(vMergePixel_1.val[1], 1);
                vMergePixel_1.val[1] = vmulq_u8(vMergePixel_1.val[1], vMul);

                vst2q_u8(pEvsMergeLine0, vMergePixel_0);
                vst2q_u8(pEvsMergeLine1, vMergePixel_1);

                pEvsSubPixelLine_0 += 16;
                pEvsSubPixelLine_1 += 16;
                pEvsSubPixelLine_2 += 16;
                pEvsSubPixelLine_3 += 16;

                pEvsMergeLine0 += 32;
                pEvsMergeLine1 += 32;
            }
        }
    }

    return 0;
}

static uint32_t ApxEvs_Merge_Algo(uint8_t* pEvsSubPixelData[], uint8_t* pEvsMergePixelData, uint32_t MergeCount)
{

    if(MergeCount < APX_K2_EVS_MERGE_FRAME_NUM_Algo) {
        uint8x16_t vSubPixel_0;
        uint8x16_t vSubPixel_1;
        uint8x16_t vSubPixel_2;
        uint8x16_t vSubPixel_3;
        uint8x16x2_t vMergePixel_0;
        uint8x16x2_t vMergePixel_1;
        uint32_t TempNeonSubWidth = APX_K2_EVS_SUB_WIDTH/16;


    //    RK_LOGE("%s, %d height = %d", __FUNCTION__, __LINE__, APX_K2_EVS_SUB_HEIGHT);

        for(uint32_t y = 0; y < APX_K2_EVS_SUB_HEIGHT; y++) {

            uint32_t TempMergeWidth = 2*y*APX_K2_EVS_WIDTH;
            uint8_t* pEvsMergeLine0 = pEvsMergePixelData + TempMergeWidth;
            uint8_t* pEvsMergeLine1 = pEvsMergePixelData + (TempMergeWidth + APX_K2_EVS_WIDTH);

            uint32_t TempSubWidth = y*APX_K2_EVS_SUB_WIDTH;
            uint8_t* pEvsSubPixelLine_0 = pEvsSubPixelData[0] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_1 = pEvsSubPixelData[1] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_2 = pEvsSubPixelData[2] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_3 = pEvsSubPixelData[3] + TempSubWidth;

        //    RK_LOGE("%s, %d, y=%d", __FUNCTION__, __LINE__, y);
#if 1
            for (uint32_t x = 0; x < TempNeonSubWidth; x++) { 
                //RK_LOGE("%s, %d", __FUNCTION__, __LINE__);
                vSubPixel_0 = vld1q_u8(pEvsSubPixelLine_0);
                vSubPixel_1 = vld1q_u8(pEvsSubPixelLine_1);
                vSubPixel_2 = vld1q_u8(pEvsSubPixelLine_2);
                vSubPixel_3 = vld1q_u8(pEvsSubPixelLine_3);

                vMergePixel_0 = vld2q_u8(pEvsMergeLine0);
                vMergePixel_1 = vld2q_u8(pEvsMergeLine1);

                vMergePixel_0.val[0] = vorrq_u8(vMergePixel_0.val[0], vSubPixel_0);  //or
                vMergePixel_0.val[1] = vorrq_u8(vMergePixel_0.val[1], vSubPixel_1);
                vMergePixel_1.val[0] = vorrq_u8(vMergePixel_1.val[0], vSubPixel_2);
                vMergePixel_1.val[1] = vorrq_u8(vMergePixel_1.val[1], vSubPixel_3);

                vst2q_u8(pEvsMergeLine0, vMergePixel_0);
                vst2q_u8(pEvsMergeLine1, vMergePixel_1);

                pEvsSubPixelLine_0 += 16;
                pEvsSubPixelLine_1 += 16;
                pEvsSubPixelLine_2 += 16;
                pEvsSubPixelLine_3 += 16;

                pEvsMergeLine0 += 32;
                pEvsMergeLine1 += 32;
            }
#endif
        }
    }else {
        uint8x16_t vSubPixel_0;
        uint8x16_t vSubPixel_1;
        uint8x16_t vSubPixel_2;
        uint8x16_t vSubPixel_3;
        uint8x16x2_t vMergePixel_0;
        uint8x16x2_t vMergePixel_1;
        uint8x16_t vMul = vdupq_n_u8(APX_K2_EVS_NO_EVENT_VALUE_Algo);
        uint8x16_t vEor = vdupq_n_u8(0x01);
        uint8x16_t vAdd = vdupq_n_u8(0x01);

        uint32_t TempNeonSubWidth = APX_K2_EVS_SUB_WIDTH/16;

        for(uint32_t y = 0; y < APX_K2_EVS_SUB_HEIGHT; y++) {

            uint32_t TempMergeWidth = 2*y*APX_K2_EVS_WIDTH;
            uint8_t* pEvsMergeLine0 = pEvsMergePixelData + TempMergeWidth;
            uint8_t* pEvsMergeLine1 = pEvsMergePixelData + (TempMergeWidth + APX_K2_EVS_WIDTH);

            uint32_t TempSubWidth = y*APX_K2_EVS_SUB_WIDTH;
            uint8_t* pEvsSubPixelLine_0 = pEvsSubPixelData[0] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_1 = pEvsSubPixelData[1] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_2 = pEvsSubPixelData[2] + TempSubWidth;
            uint8_t* pEvsSubPixelLine_3 = pEvsSubPixelData[3] + TempSubWidth;

            for (uint32_t x = 0; x < TempNeonSubWidth; x++) { 
                vSubPixel_0 = vld1q_u8(pEvsSubPixelLine_0);
                vSubPixel_1 = vld1q_u8(pEvsSubPixelLine_1);
                vSubPixel_2 = vld1q_u8(pEvsSubPixelLine_2);
                vSubPixel_3 = vld1q_u8(pEvsSubPixelLine_3);

                vMergePixel_0 = vld2q_u8(pEvsMergeLine0);
                vMergePixel_1 = vld2q_u8(pEvsMergeLine1);

                vMergePixel_0.val[0] = vSubPixel_0; 
                vMergePixel_0.val[1] = vSubPixel_1;
                vMergePixel_1.val[0] = vSubPixel_2;
                vMergePixel_1.val[1] = vSubPixel_3;

                vst2q_u8(pEvsMergeLine0, vMergePixel_0);
                vst2q_u8(pEvsMergeLine1, vMergePixel_1);

                pEvsSubPixelLine_0 += 16;
                pEvsSubPixelLine_1 += 16;
                pEvsSubPixelLine_2 += 16;
                pEvsSubPixelLine_3 += 16;

                pEvsMergeLine0 += 32;
                pEvsMergeLine1 += 32;
            }
        }
    }

    return 0;
}

static int ConvertEVS2YUV(TEST_VI_CTX_S *pCtx, VIDEO_FRAME_INFO_S *pstViFrame)
{
    int ret = 0;
    uint32_t EvsSubFrameMemSize = APX_K2_EVS_SUB_WIDTH * APX_K2_EVS_SUB_HEIGHT / 4;
    uint32_t EvsSubFramePixelSize = APX_K2_EVS_SUB_WIDTH * APX_K2_EVS_SUB_HEIGHT;
    uint32_t EvsRawMemSize = APX_K2_EVS_RAW_WIDTH * APX_K2_EVS_RAW_HEIGHT;
    uint32_t EvsRawSubMemSize = APX_K2_EVS_RAW_WIDTH * APX_K2_EVS_RAW_HEIGHT / APX_K2_EVS_RAW_MERGE_NUM / APX_K2_EVS_SUB_FRAME_NUM;
    uint32_t DstEvsYuvSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT * 3 / 2;
    uint32_t DstEvsYSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT;
    uint32_t DstEvsUvSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT / 2;
    uint32_t evsSubFrameNums = APX_K2_EVS_RAW_MERGE_NUM * APX_K2_EVS_SUB_FRAME_NUM;
    uint8_t* pEvsSubPixelData[APX_K2_EVS_SUB_FRAME_NUM] = {NULL};


    MB_BLK pMbBlk = NULL;
    MB_BLK pMbBlkYuvFrame[2] = {NULL,};
    uint8_t* pDstEvsYuv[2] = {NULL, NULL};


    pMbBlk = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYuvSize, RK_TRUE);
    if (RK_NULL == pMbBlk) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        return -1;
    }

    uint8_t *pVirAddr = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlk);
    for (uint32_t i = 0; i < APX_K2_EVS_SUB_FRAME_NUM; i++) {
        pEvsSubPixelData[i] = pVirAddr + (i * EvsSubFramePixelSize);
    }

    
    pMbBlkYuvFrame[0] = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYuvSize, RK_TRUE);
    if (RK_NULL == pMbBlkYuvFrame[0]) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        RK_MPI_MB_ReleaseMB(pMbBlk);
        return -1;
    }

    pMbBlkYuvFrame[1] = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYuvSize, RK_TRUE);
    if (RK_NULL == pMbBlkYuvFrame[1]) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        RK_MPI_MB_ReleaseMB(pMbBlk);
        RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[0]);
        return -1;
    }

    pDstEvsYuv[0] = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlkYuvFrame[0]);
    pDstEvsYuv[1] = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlkYuvFrame[1]);
    uint8_t *pEvsData = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pstViFrame->stVFrame.pMbBlk);

    memset(pDstEvsYuv[0], 0, DstEvsYSize);
    memset(pDstEvsYuv[1], 0, DstEvsYSize);
    memset(pDstEvsYuv[0]+DstEvsYSize, APX_K2_EVS_NO_EVENT_VALUE, DstEvsUvSize);
    memset(pDstEvsYuv[1]+DstEvsYSize, APX_K2_EVS_NO_EVENT_VALUE, DstEvsUvSize);


    for (uint32_t i = 0; i < evsSubFrameNums; i++) {

        if (APX_K2_EVS_DATA_HEAD != (*((RK_U32*)pEvsData) & 0x00FFFFFF)) {
            RK_LOGE("apx evs frame head error");
            ret = -1;
            break;
        }

        ApxEvs_2bitToByte(pEvsData + APX_K2_EVS_DATA_HEAD_LEN, 
            pEvsSubPixelData[i % APX_K2_EVS_SUB_FRAME_NUM], APX_K2_EVS_SUB_WIDTH, APX_K2_EVS_SUB_HEIGHT);
        
        pEvsData += EvsRawSubMemSize;

        if ((i+1) % APX_K2_EVS_SUB_FRAME_NUM == 0) {
            uint32_t MergeCount = (i+1) / APX_K2_EVS_SUB_FRAME_NUM;
            uint32_t DstCount = 0;

            if (MergeCount > 4) {
                MergeCount -= 4;
                DstCount = 1;
            }
            
            //RK_LOGI("merge sub frame %d", MergeCount);
            uint8_t* pEvsMergePixelData = pDstEvsYuv[DstCount];
            
            ApxEvs_Merge(pEvsSubPixelData, pEvsMergePixelData, MergeCount);
            
            if (MergeCount == 4) {
                static uint32_t u32FrameSeq = 0;
                VIDEO_FRAME_INFO_S stVideoFrame;
                memset(&stVideoFrame, 0x0, sizeof(VIDEO_FRAME_INFO_S));
                stVideoFrame.stVFrame.pMbBlk = pMbBlkYuvFrame[DstCount];
                stVideoFrame.stVFrame.u32Width = APX_K2_EVS_WIDTH;
                stVideoFrame.stVFrame.u32Height = APX_K2_EVS_HEIGHT;
                stVideoFrame.stVFrame.u32VirWidth = APX_K2_EVS_WIDTH;
                stVideoFrame.stVFrame.u32VirHeight = APX_K2_EVS_HEIGHT;
                stVideoFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
                stVideoFrame.stVFrame.u32FrameFlag |= 0;
                stVideoFrame.stVFrame.u64PrivateData = u32FrameSeq++;
                stVideoFrame.stVFrame.u64PTS = TEST_COMM_GetNowUs();
                stVideoFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;

                RK_MPI_SYS_MmzFlushCache(pMbBlkYuvFrame[DstCount], RK_FALSE);
                
                // uint8_t *pEvsAlgoData = (uint8_t *)RK_MPI_MB_Handle2VirAddr(stVideoFrame.stVFrame.pMbBlk);
                // runSpatialDenoiseSimple(pEvsAlgoData, APX_K2_EVS_HEIGHT, APX_K2_EVS_WIDTH, 127, denoise);
                // RK_MPI_SYS_MmzFlushCache(stVideoFrame.stVFrame.pMbBlk, RK_FALSE);
                
                RK_MPI_SYS_MmzFlushCache(stVideoFrame.stVFrame.pMbBlk, RK_FALSE);

                #if 1
                RK_S32 s32Ret = RK_MPI_VPSS_SendFrame(APX_EVS_VPSS_GROUP, 0, &stVideoFrame, -1);
                if (s32Ret != RK_SUCCESS) {
                    printf("%s RK_MPI_VPSS_SendFrame with code 0x%x\n", __FUNCTION__, s32Ret);
                }
                #endif
            } 

        }
    }

    RK_MPI_MB_ReleaseMB(pMbBlk);
    RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[0]);
    RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[1]);

    return ret;
    
}

static int ConvertEVS_ALgo(TEST_VI_CTX_S *pCtx, VIDEO_FRAME_INFO_S *pstViFrame)
{
    int ret = 0;
    uint32_t EvsSubFrameMemSize = APX_K2_EVS_SUB_WIDTH * APX_K2_EVS_SUB_HEIGHT / 4;
    uint32_t EvsSubFramePixelSize = APX_K2_EVS_SUB_WIDTH * APX_K2_EVS_SUB_HEIGHT;
    uint32_t EvsRawMemSize = APX_K2_EVS_RAW_WIDTH * APX_K2_EVS_RAW_HEIGHT;
    uint32_t EvsRawSubMemSize = APX_K2_EVS_RAW_WIDTH * APX_K2_EVS_RAW_HEIGHT / APX_K2_EVS_RAW_MERGE_NUM / APX_K2_EVS_SUB_FRAME_NUM;
    uint32_t DstEvsYuvSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT * 3 / 2;
    uint32_t DstEvsYSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT;
    uint32_t DstEvsUvSize = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT / 2;
    uint32_t evsSubFrameNums = APX_K2_EVS_RAW_MERGE_NUM * APX_K2_EVS_SUB_FRAME_NUM;
    uint8_t* pEvsSubPixelData[APX_K2_EVS_SUB_FRAME_NUM] = {NULL};


    MB_BLK pMbBlk = NULL;
    MB_BLK pMbBlkYuvFrame[2] = {NULL,};
    uint8_t* pDstEvsYuv[2] = {NULL, NULL};


    pMbBlk = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYSize, RK_TRUE);
    if (RK_NULL == pMbBlk) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        return -1;
    }

    uint8_t *pVirAddr = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlk);
    for (uint32_t i = 0; i < APX_K2_EVS_SUB_FRAME_NUM; i++) {
        pEvsSubPixelData[i] = pVirAddr + (i * EvsSubFramePixelSize);
    }

    
    pMbBlkYuvFrame[0] = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYSize, RK_TRUE);
    if (RK_NULL == pMbBlkYuvFrame[0]) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        RK_MPI_MB_ReleaseMB(pMbBlk);
        return -1;
    }

    pMbBlkYuvFrame[1] = RK_MPI_MB_GetMB(pCtx->MbPool, DstEvsYSize, RK_TRUE);
    if (RK_NULL == pMbBlkYuvFrame[1]) {
        RK_LOGE("RK_MPI_MB_GetMB fail");
        RK_MPI_MB_ReleaseMB(pMbBlk);
        RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[0]);
        return -1;
    }

    pDstEvsYuv[0] = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlkYuvFrame[0]);
    pDstEvsYuv[1] = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pMbBlkYuvFrame[1]);
    uint8_t *pEvsData = (uint8_t *)RK_MPI_MB_Handle2VirAddr(pstViFrame->stVFrame.pMbBlk);

    memset(pDstEvsYuv[0], 0, DstEvsYSize);
    memset(pDstEvsYuv[1], 0, DstEvsYSize);
    // memset(pDstEvsYuv[0]+DstEvsYSize, APX_K2_EVS_NO_EVENT_VALUE_Algo, DstEvsUvSize);
    // memset(pDstEvsYuv[1]+DstEvsYSize, APX_K2_EVS_NO_EVENT_VALUE_Algo, DstEvsUvSize);


    for (uint32_t i = 0; i < evsSubFrameNums; i++) {

        if (APX_K2_EVS_DATA_HEAD != (*((RK_U32*)pEvsData) & 0x00FFFFFF)) {
            RK_LOGE("apx evs frame head error");
            ret = -1;
            break;
        }

        ApxEvs_2bitToByte_Algo(pEvsData + APX_K2_EVS_DATA_HEAD_LEN, 
            pEvsSubPixelData[i % APX_K2_EVS_SUB_FRAME_NUM], APX_K2_EVS_SUB_WIDTH, APX_K2_EVS_SUB_HEIGHT);
        
        pEvsData += EvsRawSubMemSize;

        if ((i+1) % APX_K2_EVS_SUB_FRAME_NUM == 0) {
            uint32_t MergeCount = (i+1) / APX_K2_EVS_SUB_FRAME_NUM;
            uint32_t DstCount = 0;

            if (MergeCount > 4) {
                MergeCount -= 4;
                DstCount = 1;
            }
            
            // RK_LOGI("merge sub frame %d", MergeCount);
            uint8_t* pEvsMergePixelData = pDstEvsYuv[DstCount];
            
            ApxEvs_Merge_Algo(pEvsSubPixelData, pEvsMergePixelData, MergeCount);
            
            if (MergeCount == 4) {
                static uint32_t u32FrameSeq = 0;
                VIDEO_FRAME_INFO_S stVideoFrame;
                memset(&stVideoFrame, 0x0, sizeof(VIDEO_FRAME_INFO_S));
                stVideoFrame.stVFrame.pMbBlk = pMbBlkYuvFrame[DstCount];
                stVideoFrame.stVFrame.u32Width = APX_K2_EVS_WIDTH;
                stVideoFrame.stVFrame.u32Height = APX_K2_EVS_HEIGHT;
                stVideoFrame.stVFrame.u32VirWidth = APX_K2_EVS_WIDTH;
                stVideoFrame.stVFrame.u32VirHeight = APX_K2_EVS_HEIGHT;
                stVideoFrame.stVFrame.enPixelFormat = RK_FMT_YUV400SP;//RK_FMT_YUV400SP;RK_FMT_YUV420SP
                stVideoFrame.stVFrame.u32FrameFlag |= 0;
                stVideoFrame.stVFrame.u64PrivateData = u32FrameSeq++;
                stVideoFrame.stVFrame.u64PTS = TEST_COMM_GetNowUs();
                stVideoFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;

                uint8_t *pEvsAlgoData = (uint8_t *)RK_MPI_MB_Handle2VirAddr(stVideoFrame.stVFrame.pMbBlk);
                // runSpatialDenoiseSimple(pEvsAlgoData, APX_K2_EVS_HEIGHT, APX_K2_EVS_WIDTH, 127, denoise);
                runSpatialDenoiseSimple(pEvsAlgoData, APX_K2_EVS_HEIGHT, APX_K2_EVS_WIDTH, 0, denoise);

                RK_MPI_SYS_MmzFlushCache(pMbBlkYuvFrame[DstCount], RK_FALSE);
                
 
                #if 1
                RK_S32 s32Ret = RK_MPI_VPSS_SendFrame(APX_EVS_VPSS_GROUP, 0, &stVideoFrame, -1);
                if (s32Ret != RK_SUCCESS) {
                    printf("%s RK_MPI_VPSS_SendFrame with code 0x%x\n", __FUNCTION__, s32Ret);
                }
                #endif
            } 

        }
    }

    RK_MPI_MB_ReleaseMB(pMbBlk);
    RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[0]);
    RK_MPI_MB_ReleaseMB(pMbBlkYuvFrame[1]);

    return ret;
    
}

static RK_S32 create_vi(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;

    // 0. get dev config status
    s32Ret = RK_MPI_VI_GetDevAttr(ctx->devId, &ctx->stDevAttr);
    if (s32Ret == RK_ERR_VI_NOT_CONFIG) {
        // 0-1.config dev
        s32Ret = RK_MPI_VI_SetDevAttr(ctx->devId, &ctx->stDevAttr);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevAttr %x", s32Ret);
            goto __FAILED;
        }
    } else {
        RK_LOGE("RK_MPI_VI_SetDevAttr already");
    }
    // 1.get  dev enable status
    s32Ret = RK_MPI_VI_GetDevIsEnable(ctx->devId);
    if (s32Ret != RK_SUCCESS) {
        // 1-2.enable dev
        s32Ret = RK_MPI_VI_EnableDev(ctx->devId);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_EnableDev %x", s32Ret);
            goto __FAILED;
        }
        // 1-3.bind dev/pipe
        ctx->stBindPipe.u32Num = ctx->pipeId;
        ctx->stBindPipe.PipeId[0] = ctx->pipeId;
        s32Ret = RK_MPI_VI_SetDevBindPipe(ctx->devId, &ctx->stBindPipe);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_SetDevBindPipe %x", s32Ret);
            goto __FAILED;
        }
    } else {
        RK_LOGE("RK_MPI_VI_EnableDev already");
    }
    // 2.config channel
    s32Ret = RK_MPI_VI_SetChnAttr(ctx->pipeId, ctx->channelId, &ctx->stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_SetChnAttr %x", s32Ret);
        goto __FAILED;
    }
    // 3.enable channel
    RK_LOGD("RK_MPI_VI_EnableChn %x %d %d", ctx->devId, ctx->pipeId, ctx->channelId);
    s32Ret = RK_MPI_VI_EnableChn(ctx->pipeId, ctx->channelId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VI_EnableChn %x", s32Ret);
        goto __FAILED;
    }
    // 4.save debug file
    if (ctx->stDebugFile.bCfg) {
        s32Ret = RK_MPI_VI_ChnSaveFile(ctx->pipeId, ctx->channelId, &ctx->stDebugFile);
        RK_LOGD("RK_MPI_VI_ChnSaveFile %x", s32Ret);
    }

__FAILED:
    return s32Ret;
}

static RK_S32 destroy_vi(TEST_VI_CTX_S *ctx) {
    RK_S32 s32Ret = RK_FAILURE;
    s32Ret = RK_MPI_VI_DisableChn(ctx->pipeId, ctx->channelId);
    RK_LOGE("RK_MPI_VI_DisableChn pipe=%d ret:%x", ctx->pipeId, s32Ret);

    s32Ret = RK_MPI_VI_DisableDev(ctx->devId);
    RK_LOGE("RK_MPI_VI_DisableDev pipe=%d ret:%x", ctx->pipeId, s32Ret);
    
    return s32Ret;
}


RK_S32 ApxCreatePvs(RK_U32 Width, RK_U32 Height)
{
    RK_S32 s32Ret = RK_SUCCESS;

    memset(&ApxPvsCtx, 0, sizeof(TEST_PVS_CTX_S));
    ApxPvsCtx.s32DevId = 0;
    ApxPvsCtx.s32ChnId = 0;
    ApxPvsCtx.enVProcDev = VIDEO_PROC_DEV_GPU; //0: GPU, 1: RGA
    ApxPvsCtx.s32SrcFrameRate = 30;
    ApxPvsCtx.s32StitchFrmCnt = 30;
    ApxPvsCtx.s32RecvThreshold = 2;
    ApxPvsCtx.stDevAttr.s32StitchFrmRt = 30;
    ApxPvsCtx.stDevAttr.stSize.u32Width = Width;
    ApxPvsCtx.stDevAttr.stSize.u32Height = Height;
    // ApxPvsCtx.enSrcPixelFormat = ;

    s32Ret = RK_MPI_PVS_SetVProcDev(ApxPvsCtx.s32DevId, ApxPvsCtx.enVProcDev);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("set proc dev %d failed", ApxPvsCtx.s32DevId);
    }
    s32Ret = RK_MPI_PVS_SetDevAttr(ApxPvsCtx.s32DevId, &ApxPvsCtx.stDevAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("set dev %d attr failed", ApxPvsCtx.s32DevId);
        return s32Ret;
    }
    s32Ret = RK_MPI_PVS_EnableDev(ApxPvsCtx.s32DevId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("enable dev %d failed", ApxPvsCtx.s32DevId);
        return s32Ret;
    }
    
    return s32Ret;
}


RK_S32 ApxPvsStop()
{
    RK_S32 s32Ret = RK_SUCCESS;
    
    RK_MPI_PVS_DisableChn(ApxPvsCtx.s32DevId, APX_APS_PVS_CHANNEL);
    RK_MPI_PVS_DisableChn(ApxPvsCtx.s32DevId, APX_EVS_PVS_CHANNEL);

    RK_MPI_PVS_DisableDev(ApxPvsCtx.s32DevId);
    
    return s32Ret;
}


RK_S32 ApxPvsChannelStart(RK_U32 ChannelId, RK_U32 X, RK_U32 Y, RK_U32 Width, RK_U32 Height)
{
    RK_S32 s32Ret = RK_SUCCESS;
    PVS_CHN_ATTR_S stChnAttr;
    PVS_CHN_PARAM_S stChnParam;

    memset(&stChnParam, 0, sizeof(PVS_CHN_PARAM_S));
    stChnParam.enStitchMod = (PVS_STITCH_MODE_E)ApxPvsCtx.s32StitchMode;
    stChnParam.s32ChnFrmRate = ApxPvsCtx.s32SrcFrameRate;
    stChnParam.s32RecvThreshold = ApxPvsCtx.s32RecvThreshold;

    memset(&stChnAttr, 0, sizeof(PVS_CHN_ATTR_S));
    stChnAttr.stRect.s32X = X;
    stChnAttr.stRect.s32Y = Y;
    stChnAttr.stRect.u32Width = Width;
    stChnAttr.stRect.u32Height = Height;

    s32Ret = RK_MPI_PVS_SetChnAttr(ApxPvsCtx.s32DevId, ChannelId, &stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed to set chn %d attr", ChannelId);
    }
    s32Ret = RK_MPI_PVS_SetChnParam(ApxPvsCtx.s32DevId, ChannelId, &stChnParam);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed to set chn %d param", ChannelId);
    }
    s32Ret = RK_MPI_PVS_EnableChn(ApxPvsCtx.s32DevId, ChannelId);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("enable channel %d failed", ChannelId);
        return s32Ret;
    }

    return s32Ret;
}

static RK_S32 ApxCreateVo(VO_LAYER VoLayer, VO_DEV VoDev, RK_U32 u32Ch, RK_U32 X, RK_U32 Y, RK_U32 Width, RK_U32 Height) 
{
    /* Enable VO */
    VO_PUB_ATTR_S VoPubAttr;
    VO_VIDEO_LAYER_ATTR_S stLayerAttr;
    RK_S32 s32Ret = RK_SUCCESS;
    VO_CHN_ATTR_S stChnAttr;

    RK_MPI_VO_DisableLayer(VoLayer);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_ESMART_0);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_ESMART_1);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_SMART_0);
    RK_MPI_VO_DisableLayer(RK356X_VOP_LAYER_SMART_1);
    RK_MPI_VO_Disable(VoDev);

    memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
    memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));

    stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
    stLayerAttr.stDispRect.s32X = X;
    stLayerAttr.stDispRect.s32Y = Y;
    stLayerAttr.u32DispFrmRt = 30;
    stLayerAttr.stDispRect.u32Width = Width;
    stLayerAttr.stDispRect.u32Height = Height;
    stLayerAttr.stImageSize.u32Width = Width;
    stLayerAttr.stImageSize.u32Height = Height;

    s32Ret = RK_MPI_VO_GetPubAttr(VoDev, &VoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    VoPubAttr.enIntfType = VO_INTF_HDMI;
    VoPubAttr.enIntfSync = VO_OUTPUT_1080P60;

    s32Ret = RK_MPI_VO_SetPubAttr(VoDev, &VoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }
    s32Ret = RK_MPI_VO_Enable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        return s32Ret;
    }

    s32Ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_SetLayerAttr failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    s32Ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_BindLayer failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }


    s32Ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("RK_MPI_VO_EnableLayer failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    stChnAttr.stRect.s32X = 0;
    stChnAttr.stRect.s32Y = 0;
    stChnAttr.stRect.u32Width = stLayerAttr.stImageSize.u32Width;
    stChnAttr.stRect.u32Height = stLayerAttr.stImageSize.u32Height;
    stChnAttr.u32Priority = 0;
    stChnAttr.u32FgAlpha = 128;
    stChnAttr.u32BgAlpha = 0;

    s32Ret = RK_MPI_VO_SetChnAttr(VoLayer, u32Ch, &stChnAttr);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("set chn Attr failed,s32Ret:%d\n", s32Ret);
        return RK_FAILURE;
    }

    return s32Ret;
}



TEST_VI_CTX_S * ApxCreateVi(uint32_t DevId, uint32_t ChId, uint32_t W, uint32_t H, uint32_t Depth, char* pEntName, PIXEL_FORMAT_E PixelFormat)
{

    TEST_VI_CTX_S *pTempCtx;
    pTempCtx = (TEST_VI_CTX_S *)(malloc(sizeof(TEST_VI_CTX_S)));
    memset(pTempCtx, 0, sizeof(TEST_VI_CTX_S));

    pTempCtx->devId = DevId;
    pTempCtx->pipeId = pTempCtx->devId;
    pTempCtx->channelId = ChId;

    pTempCtx->width = W;
    pTempCtx->height = H;

    pTempCtx->stChnAttr.stSize.u32Width = pTempCtx->width;
    pTempCtx->stChnAttr.stSize.u32Height = pTempCtx->height;

    pTempCtx->stChnAttr.stIspOpt.u32BufCount = 8;
    pTempCtx->stChnAttr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    pTempCtx->stChnAttr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
    pTempCtx->stChnAttr.u32Depth = Depth;//8;
    pTempCtx->aEntityName = pEntName;
    
  //  memcpy(pTempCtx->aEntityName, pEntName, strlen(pEntName));
    if(RK_NULL != pEntName) {
        memcpy(pTempCtx->stChnAttr.stIspOpt.aEntityName, pTempCtx->aEntityName, strlen(pTempCtx->aEntityName)); 
    }

    RK_LOGE("%s pTempCtx->aEntityName = %s", __FUNCTION__, pTempCtx->aEntityName);

    pTempCtx->stChnAttr.enPixelFormat = PixelFormat;
    pTempCtx->stChnAttr.stFrameRate.s32SrcFrameRate = -1;
    pTempCtx->stChnAttr.stFrameRate.s32DstFrameRate = -1; 
    pTempCtx->bEnRgn = RK_FALSE;
    pTempCtx->s32RgnCnt = 1;
    pTempCtx->rgnType = RGN_BUTT;

    create_vi(pTempCtx);
   // test_vi_init(pTempCtx);

    return pTempCtx;
}


 RK_S32 ApxDeleteVi(TEST_VI_CTX_S * pCtx)
{
    destroy_vi(pCtx);
    free(pCtx);
    pCtx = NULL;
    return RK_SUCCESS;
}



void * EvsRawHandleThreadEntry(void *arg)
{ 
    RK_S32 s32Ret = RK_SUCCESS; 
    uint32_t EvsRawMemSize = APX_K2_EVS_RAW_WIDTH*APX_K2_EVS_RAW_HEIGHT;
    TEST_VI_CTX_S *pCtx = (TEST_VI_CTX_S*)arg;
    

    pCtx->MbPool = MbPoolCreate(APX_K2_EVS_WIDTH, APX_K2_EVS_HEIGHT, 6, RK_FMT_YUV420SP);//RK_FMT_YUV420SP;RK_FMT_YUV400SP
    if(MB_INVALID_POOLID == pCtx->MbPool) {
        RK_LOGE("RK_MPI_MB_CreatePool fail");
        return NULL;
    }

    while (!bquit) {
        s32Ret = RK_MPI_VI_GetChnFrame(pCtx->pipeId, pCtx->channelId, &pCtx->stViFrame, -1);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_GetChnFrame error with code 0x%x\n", s32Ret);
            usleep(100);
            continue;
        }

        if(RK_MPI_MB_GetLength(pCtx->stViFrame.stVFrame.pMbBlk) != EvsRawMemSize) {
            RK_LOGE("EVS Frame size error.\n");
            continue;
        }

        
        RK_U64 nowUs = TEST_COMM_GetNowUs();
        // ConvertEVS2YUV(pCtx, &pCtx->stViFrame);            
        ConvertEVS_ALgo(pCtx, &pCtx->stViFrame);
        RK_U64 endUs = TEST_COMM_GetNowUs();
        // RK_LOGI("time used %lld us\r\n", (endUs - nowUs));

        
        s32Ret = RK_MPI_VI_ReleaseChnFrame(pCtx->pipeId, pCtx->channelId, &pCtx->stViFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("RK_MPI_VI_ReleaseChnFrame fail, code:0x%x", s32Ret);
        }
    
    } 

    RK_MPI_MB_DestroyPool(pCtx->MbPool);
    return NULL;
}



static unsigned int convert_color(unsigned int src_color, image_format_t dst_fmt)
{
    unsigned int dst_color = 0x0;
    unsigned char* p_src_color = (unsigned char*)&src_color;
    unsigned char* p_dst_color = (unsigned char*)&dst_color;
    char r = p_src_color[2];
    char g = p_src_color[1];
    char b = p_src_color[0];
    char a = p_src_color[3];

    switch (dst_fmt)
    {
        case IMAGE_FORMAT_GRAY8:
            p_dst_color[0] = a;
            break;
        case IMAGE_FORMAT_RGB888:
            p_dst_color[0] = r;
            p_dst_color[1] = g;
            p_dst_color[2] = b;
            break;
        case IMAGE_FORMAT_RGBA8888:
            p_dst_color[0] = r;
            p_dst_color[1] = g;
            p_dst_color[2] = b;
            p_dst_color[3] = a;
            break;
        case IMAGE_FORMAT_YUV420SP_NV12:
            p_dst_color[0] = 0.299 * r + 0.587 * g + 0.114 * b;
            p_dst_color[1] = 0.492 * (b - p_dst_color[0]);
            p_dst_color[2] = 0.877 * (r - p_dst_color[0]);
            break;
        case IMAGE_FORMAT_YUV420SP_NV21:
            p_dst_color[0] = 0.299 * r + 0.587 * g + 0.114 * b;
            p_dst_color[1] = 0.877 * (r - p_dst_color[0]);
            p_dst_color[2] = 0.492 * (b - p_dst_color[0]);
            break;
        default:
            break;
    }
    return dst_color;
}






static void draw_rectangle_c1(unsigned char* pixels, int w, int h, int rx, int ry, int rw, int rh, unsigned int color,
                              int thickness)
{
    const unsigned char* pen_color = (const unsigned char*)&color;
    int stride = w;

    if (thickness == -1) {
        // filled
        for (int y = ry; y < ry + rh; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx; x < rx + rw; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x] = pen_color[0];
            }
        }

        return;
    }

    const int t0 = thickness / 2;
    const int t1 = thickness - t0;

    // draw top
    {
        for (int y = ry - t0; y < ry + t1; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx - t0; x < rx + rw + t1; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x] = pen_color[0];
            }
        }
    }

    // draw bottom
    {
        for (int y = ry + rh - t0; y < ry + rh + t1; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx - t0; x < rx + rw + t1; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x] = pen_color[0];
            }
        }
    }

    // draw left
    for (int x = rx - t0; x < rx + t1; x++) {
        if (x < 0)
            continue;

        if (x >= w)
            break;

        for (int y = ry + t1; y < ry + rh - t0; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            p[x] = pen_color[0];
        }
    }

    // draw right
    for (int x = rx + rw - t0; x < rx + rw + t1; x++) {
        if (x < 0)
            continue;

        if (x >= w)
            break;

        for (int y = ry + t1; y < ry + rh - t0; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            p[x] = pen_color[0];
        }
    }
}

static void draw_rectangle_c2(unsigned char* pixels, int w, int h, int rx, int ry, int rw, int rh, unsigned int color,
                              int thickness)
{
    const unsigned char* pen_color = (const unsigned char*)&color;
    int stride = w * 2;

    if (thickness == -1) {
        // filled
        for (int y = ry; y < ry + rh; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx; x < rx + rw; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x * 2 + 0] = pen_color[0];
                p[x * 2 + 1] = pen_color[1];
            }
        }

        return;
    }

    const int t0 = thickness / 2;
    const int t1 = thickness - t0;

    // draw top
    {
        for (int y = ry - t0; y < ry + t1; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx - t0; x < rx + rw + t1; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x * 2 + 0] = pen_color[0];
                p[x * 2 + 1] = pen_color[1];
            }
        }
    }

    // draw bottom
    {
        for (int y = ry + rh - t0; y < ry + rh + t1; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            for (int x = rx - t0; x < rx + rw + t1; x++) {
                if (x < 0)
                    continue;

                if (x >= w)
                    break;

                p[x * 2 + 0] = pen_color[0];
                p[x * 2 + 1] = pen_color[1];
            }
        }
    }

    // draw left
    for (int x = rx - t0; x < rx + t1; x++) {
        if (x < 0)
            continue;

        if (x >= w)
            break;

        for (int y = ry + t1; y < ry + rh - t0; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            p[x * 2 + 0] = pen_color[0];
            p[x * 2 + 1] = pen_color[1];
        }
    }

    // draw right
    for (int x = rx + rw - t0; x < rx + rw + t1; x++) {
        if (x < 0)
            continue;

        if (x >= w)
            break;

        for (int y = ry + t1; y < ry + rh - t0; y++) {
            if (y < 0)
                continue;

            if (y >= h)
                break;

            unsigned char* p = pixels + stride * y;

            p[x * 2 + 0] = pen_color[0];
            p[x * 2 + 1] = pen_color[1];
        }
    }
}



void ApxDrawRectangle_Yuv420sp(uint8_t * pData, uint32_t ImgW, uint32_t ImgH, 
                              uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color, uint32_t thickness)
{
    const unsigned char* pen_color = (const unsigned char*)&color;
    unsigned int v_y;
    unsigned int v_uv;
    unsigned char* pen_color_y = (unsigned char*)&v_y;
    unsigned char* pen_color_uv = (unsigned char*)&v_uv;

    pen_color_y[0] = pen_color[0];
    pen_color_uv[0] = pen_color[1];
    pen_color_uv[1] = pen_color[2];


    unsigned char* Y = pData;  
    draw_rectangle_c1(Y, ImgW, ImgH, rx, ry, rw, rh, v_y, thickness);


    unsigned char* UV = pData + ImgW*ImgH;
    
    //int thickness_uv = thickness == -1 ? thickness : max(thickness / 2, 1);
    int thickness_uv  = thickness/2;
    draw_rectangle_c2(UV, ImgW / 2, ImgH / 2, rx / 2, ry / 2, rw / 2, rh / 2, v_uv, thickness_uv);

    
}


void * EvsImgShowThreadEntry(void *arg)
{ 
    RK_S32 s32Ret = RK_SUCCESS; 
    TEST_VI_CTX_S *pCtx = (TEST_VI_CTX_S*)arg;
    VIDEO_FRAME_INFO_S stViFrame;
    RK_U8 *papsdata = NULL;
    uint8_t * PsrcEvsYuv = NULL;
    // int DVS_DATA_LEN = EVS_IMG_SHOW_WIDTH * EVS_IMG_SHOW_HEIGHT * 3 / 2;
    // int DVS_DATA_LEN = EVS_IMG_SHOW_WIDTH * EVS_IMG_SHOW_HEIGHT ;
    int DVS_DATA_LEN = APX_K2_EVS_WIDTH * APX_K2_EVS_HEIGHT ;

    int fd = shm_open("/dvsdatashm", O_CREAT | O_RDWR, 0777);
    if (fd < 0) {
        perror("open");
    }
    ftruncate(fd,DVS_DATA_LEN);

    sem_t *wait_dvs_sem     = sem_open("/wait_dvs_sem", O_CREAT|O_RDWR, 0666, 1); //信号量值为 1
    sem_t *send_donedvs_sem = sem_open("/send_donedvs_sem", O_CREAT|O_RDWR, 0666, 0); //信号量值为 0
    papsdata = (char *)mmap(NULL, DVS_DATA_LEN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (papsdata== MAP_FAILED)
	{
		perror("mmap");
	}
    while (!bquit) {
        s32Ret = RK_MPI_VPSS_GetChnFrame(APX_EVS_VPSS_GROUP, APX_EVS_VPSS_SHOW_CHANNEL, &stViFrame, 1000);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("error with code 0x%x\n", s32Ret);
            continue;
        }

        PsrcEvsYuv = (uint8_t*)RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
        memcpy(papsdata,PsrcEvsYuv,DVS_DATA_LEN);
        sem_post(wait_dvs_sem);
        RK_MPI_SYS_MmzFlushCache(stViFrame.stVFrame.pMbBlk,false);
        struct timeval now;
        struct timespec out_time;

        /*开始进行超时等待*/
        gettimeofday(&now, NULL);
        out_time.tv_sec = now.tv_sec + 2;
        out_time.tv_nsec = now.tv_usec * 1000;
        if (sem_timedwait(send_donedvs_sem, &out_time) < 0) {
            LOG_PRINTF("====== Timeout Send_DoneDvs_sem for EVS data ======\n");
        }

        s32Ret = RK_MPI_VPSS_ReleaseChnFrame(APX_EVS_VPSS_GROUP, APX_EVS_VPSS_SHOW_CHANNEL, &stViFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("fail, code:0x%x", s32Ret);
        }
    
    } 

    return NULL;
}



RK_S32 ApxEvsRawDataPullCreate()
{
    RK_S32 s32Ret = RK_SUCCESS; 

    pEvsCtx = ApxCreateVi(APX_EVS_DEV_ID, APX_EVS_CHANNEL_ID, APX_K2_EVS_RAW_WIDTH, APX_K2_EVS_RAW_HEIGHT, 5, "/dev/video1", RK_FMT_RGB_BAYER_SRGGB_8BPP);

#if 1
    pEvsCtx->stVpssCfg.u32VpssChnCnt = APX_EVS_VPSS_GROUP_NUM;
    pEvsCtx->stVpssCfg.stGrpVpssAttr.u32MaxW = 4096;
    pEvsCtx->stVpssCfg.stGrpVpssAttr.u32MaxH = 4096;
    pEvsCtx->stVpssCfg.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV400SP;//RK_FMT_YUV400SP;RK_FMT_YUV420SP
    pEvsCtx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
    pEvsCtx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
    pEvsCtx->stVpssCfg.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;

    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].enChnMode = VPSS_CHN_MODE_USER;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].enDynamicRange = DYNAMIC_RANGE_SDR8;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].enPixelFormat = RK_FMT_YUV400SP;//RK_FMT_YUV400SP;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].stFrameRate.s32SrcFrameRate = -1;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].stFrameRate.s32DstFrameRate = -1;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].u32Width = APX_K2_EVS_WIDTH;//EVS_IMG_SHOW_WIDTH;APX_K2_EVS_WIDTH;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].u32Height = APX_K2_EVS_HEIGHT;//EVS_IMG_SHOW_HEIGHT;APX_K2_EVS_HEIGHT
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].enCompressMode = COMPRESS_MODE_NONE;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].u32FrameBufCnt = 8;
    pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_SHOW_CHANNEL].u32Depth = 5;

    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].enChnMode = VPSS_CHN_MODE_USER;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].enDynamicRange = DYNAMIC_RANGE_SDR8;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].enPixelFormat = RK_FMT_YUV420SP;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].stFrameRate.s32SrcFrameRate = -1;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].stFrameRate.s32DstFrameRate = -1;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].u32Width = APX_K2_EVS_ALGO_WIDTH;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].u32Height = APX_K2_EVS_ALGO_HEIGHT;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].enCompressMode = COMPRESS_MODE_NONE;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].u32FrameBufCnt = 8;
    // pEvsCtx->stVpssCfg.stVpssChnAttr[APX_EVS_VPSS_ALGO_CHANNEL].u32Depth = 5;

    
    // init vpss
    s32Ret = create_vpss(&pEvsCtx->stVpssCfg, APX_EVS_VPSS_GROUP, pEvsCtx->stVpssCfg.u32VpssChnCnt);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("creat grp vpss failed!");
        goto END;
    }
#endif

    pthread_create(&pEvsCtx->ImgHandleThread, NULL, EvsRawHandleThreadEntry, (void *)pEvsCtx);
    #ifdef USB
    pthread_create(&pEvsCtx->ImgShowThread, NULL, EvsImgShowThreadEntry, (void *)pEvsCtx);
    #endif // USB


END:
    return s32Ret;
}


RK_S32 ApxEvsRawDataPullDelete()
{
    RK_S32 s32Ret = RK_SUCCESS; 
    destory_vpss(APX_EVS_VPSS_GROUP, APX_EVS_VPSS_GROUP_NUM);
    destroy_vi(pEvsCtx);
    return s32Ret;
}


void * ApsImgShowThreadEntry(void *arg)
{ 
    RK_S32 s32Ret = RK_SUCCESS; 
    TEST_VI_CTX_S *pCtx = (TEST_VI_CTX_S*)arg;
    VIDEO_FRAME_INFO_S stViFrame;

    char *papsdata = NULL;
    uint8_t * PsrcEvsYuv = NULL;
    int APS_DATA_LEN = APX_IMG_SHOW_WIDTH * APX_IMG_SHOW_HEIGHT * 3 / 2;

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
    while (!bquit) {
        s32Ret = RK_MPI_VPSS_GetChnFrame(APX_APS_VPSS_GROUP, APX_APS_VPSS_RESIZE_CHANNEL, &stViFrame, 1000);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("error with code 0x%x\n", s32Ret);
            continue;
        }
        // RK_MPI_PVS_SendFrame(0, APX_APS_PVS_CHANNEL, &stViFrame);

        PsrcEvsYuv = (uint8_t*)RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
        memcpy(papsdata,PsrcEvsYuv,APS_DATA_LEN);

        sem_post(wait_aps_sem);

        // struct timespec out_time = {2, 0};
        struct timeval now;
        struct timespec out_time;

        /*开始进行超时等待*/
        gettimeofday(&now, NULL);
        out_time.tv_sec = now.tv_sec + 2;
        out_time.tv_nsec = now.tv_usec * 1000;
        if (sem_timedwait(send_done_sem, &out_time) < 0) {
            LOG_PRINTF("====== Timeout Send_done_sem for APS data ======\n");
        }

        s32Ret = RK_MPI_VPSS_ReleaseChnFrame(APX_APS_VPSS_GROUP, APX_APS_VPSS_RESIZE_CHANNEL, &stViFrame);
        if (s32Ret != RK_SUCCESS) {
            RK_LOGE("fail, code:0x%x", s32Ret);
        }
    
    } 

    return NULL;
}


RK_S32 ApxApsYuvDataPullCreate()
{
    RK_S32 s32Ret = RK_SUCCESS; 
    MPP_CHN_S stViChn;
    MPP_CHN_S stVpssChn;

    pApsCtx = ApxCreateVi(APX_APS_DEV_ID, APX_APS_CHANNEL_ID, APX_APS_SRC_WIDTH, APX_APS_SRC_HEIGHT, 0, RK_NULL, RK_FMT_YUV420SP); //"dev/video12"

    /* vpss */
    pApsCtx->stVpssCfg.u32VpssChnCnt = APX_APS_VPSS_GROUP_NUM;
    pApsCtx->stVpssCfg.stGrpVpssAttr.u32MaxW = 4096;
    pApsCtx->stVpssCfg.stGrpVpssAttr.u32MaxH = 4096;
    pApsCtx->stVpssCfg.stGrpVpssAttr.enPixelFormat = RK_FMT_YUV420SP;
    pApsCtx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32SrcFrameRate = -1;
    pApsCtx->stVpssCfg.stGrpVpssAttr.stFrameRate.s32DstFrameRate = -1;
    pApsCtx->stVpssCfg.stGrpVpssAttr.enCompressMode = COMPRESS_MODE_NONE;

    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enChnMode = VPSS_CHN_MODE_USER;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enDynamicRange = DYNAMIC_RANGE_SDR8;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enPixelFormat = RK_FMT_YUV420SP;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].stFrameRate.s32SrcFrameRate = -1;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].stFrameRate.s32DstFrameRate = -1;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Width = APX_IMG_SHOW_WIDTH;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Height = APX_IMG_SHOW_HEIGHT;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].enCompressMode = COMPRESS_MODE_NONE;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32FrameBufCnt = 8;
    pApsCtx->stVpssCfg.stVpssChnAttr[APX_APS_VPSS_RESIZE_CHANNEL].u32Depth = 5;

    // init vpss
    s32Ret = create_vpss(&pApsCtx->stVpssCfg, APX_APS_VPSS_GROUP, pApsCtx->stVpssCfg.u32VpssChnCnt);
    if (s32Ret != RK_SUCCESS) {
      RK_LOGE("creat 0 grp vpss failed!");
    }


    pApsCtx->MbPool = MbPoolCreate(APX_IMG_SHOW_WIDTH, APX_IMG_SHOW_HEIGHT, 4, RK_FMT_YUV420SP);
    if(MB_INVALID_POOLID == pApsCtx->MbPool) {
        RK_LOGE("RK_MPI_MB_CreatePool fail");
    }

    #ifdef USB
    pthread_create(&pApsCtx->ImgShowThread, NULL, ApsImgShowThreadEntry, (void *)pApsCtx);
    #endif 


#if 1
    // bind vi to vpss
    stViChn.enModId    = RK_ID_VI;
    stViChn.s32DevId   = pApsCtx->devId;
    stViChn.s32ChnId   = pApsCtx->channelId;
    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = APX_APS_VPSS_GROUP;
    stVpssChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;

    RK_LOGD("vi to vpss ch %d vpss group %d", stVpssChn.s32ChnId , stVpssChn.s32DevId);
    s32Ret = RK_MPI_SYS_Bind(&stViChn, &stVpssChn);
    if (s32Ret != RK_SUCCESS) {
      RK_LOGE("vi and vpss bind error ");
    }
#endif
    
END:
    return s32Ret;

}


RK_S32 ApxApsYuvDataPullDelete()
{
    RK_S32 s32Ret = RK_SUCCESS; 
    MPP_CHN_S stViChn;
    MPP_CHN_S stVpssChn;

#if 1
    stViChn.enModId    = RK_ID_VI;
    stViChn.s32DevId   = pApsCtx->devId;
    stViChn.s32ChnId   = pApsCtx->channelId;
    stVpssChn.enModId = RK_ID_VPSS;
    stVpssChn.s32DevId = APX_APS_VPSS_GROUP;
    stVpssChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;
    RK_MPI_SYS_UnBind(&stViChn, &stVpssChn);
#endif

    destory_vpss(APX_APS_VPSS_GROUP, APX_APS_VPSS_GROUP_NUM);
    destroy_vi(pApsCtx);
    RK_MPI_MB_DestroyPool(pApsCtx->MbPool);


    return s32Ret;
}

void * ShowControlThreadEntry(void *arg)
{ 
    while (!bquit) {
        if(1 == updateTimer(&EvsShowTimer)) {
            EvsShowEnable = 0;
            ApsShowEnable = 0;
        }

        if(1 == updateTimer(&ApsShowTimer)) {
            ApsShowEnable = 0;
        }

        
        sleep(1);
    }

    return NULL;
}


int main(int argc, const char **argv) {
    RK_S32 i;
    RK_S32 s32Ret = RK_FAILURE;
    VO_LAYER s32VoLayer = 0;
    VO_DEV s32VoDev =0;
    RK_U32 MdStatus = 0;
    RK_U32 PersonStatus = 0;

    config p ;
    memset(&p, 0x00, sizeof(p));

    pEvsCtx = (TEST_VI_CTX_S *)malloc(sizeof(TEST_VI_CTX_S));
    if (pEvsCtx == NULL) {
        // 处理内存分配失败的情况
        perror("Memory allocation failed");
        return -1;
    }
    pEvsCtx->evsConfig = p;

    // Initialize shared memory FIFO
    shmfifo_t *fifo = shmfifo_init(SHM_KEY, BLOCKS, sizeof(config));
    if (!fifo) {
        fprintf(stderr, "Failed to initialize shared memory FIFO.\n");
        exit(EXIT_FAILURE);
    }

    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        RK_LOGE("%s rk mpi sys init fail!", __FUNCTION__);
        return RK_FAILURE;
    }
    

    ApxEvsRawDataPullCreate();
    ApxApsYuvDataPullCreate();
#ifdef VO

#if 1 
    s32VoLayer = RK356X_VOP_LAYER_CLUSTER_0;
    s32VoDev = RK356X_VO_DEV_HD0;
    #if 0
    ApxCreateVo(s32VoLayer, s32VoDev, 0, 0, 0, APX_IMG_SHOW_WIDTH, APX_IMG_SHOW_HEIGHT);
    ApxCreateVo(s32VoLayer, s32VoDev, 0, 0, 160, APX_IMG_SHOW_WIDTH*2, APX_IMG_SHOW_HEIGHT);
    ApxCreatePvs(APX_IMG_SHOW_WIDTH*2, APX_IMG_SHOW_HEIGHT);
    ApxPvsChannelStart(APX_APS_PVS_CHANNEL, 0, 0, APX_IMG_SHOW_WIDTH, APX_IMG_SHOW_HEIGHT);
    ApxPvsChannelStart(APX_EVS_PVS_CHANNEL, APX_IMG_SHOW_WIDTH, 0, EVS_IMG_SHOW_WIDTH, EVS_IMG_SHOW_HEIGHT);
    #endif

    #if 0 //aps+evs
    ApxCreateVo(s32VoLayer, s32VoDev, 0, 0, 160, 960*2, 760);
    ApxCreatePvs(960*2, 760);
    ApxPvsChannelStart(APX_APS_PVS_CHANNEL, 0, 0, 960, 760);
    ApxPvsChannelStart(APX_EVS_PVS_CHANNEL, 960, 0, 960, 760);
    #endif
    
    //aps
    ApxCreateVo(s32VoLayer, s32VoDev, 0, 0, 0, 1920, 1080);
    ApxCreatePvs(1920,1080);
    ApxPvsChannelStart(APX_APS_PVS_CHANNEL, 0, 0, 1920, 1080);
#endif

// --------------- APS  VPSS  Bind to PVS ---------------------------
#if 1
    MPP_CHN_S stApsVpssSrcChn;
    MPP_CHN_S stApsPvsDestChn;
    stApsVpssSrcChn.enModId = RK_ID_VPSS;
    stApsVpssSrcChn.s32DevId = APX_APS_VPSS_GROUP;
    stApsVpssSrcChn.s32ChnId = APX_APS_VPSS_RESIZE_CHANNEL;

    stApsPvsDestChn.enModId = RK_ID_PVS;
    stApsPvsDestChn.s32DevId = ApxPvsCtx.s32DevId;
    stApsPvsDestChn.s32ChnId = APX_APS_PVS_CHANNEL;
    s32Ret = RK_MPI_SYS_Bind(&stApsVpssSrcChn, &stApsPvsDestChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed with %#x!", s32Ret);
        return RK_FAILURE;
    }
#endif
// --------------- EVS  VPSS  Bind to PVS ---------------------------
#if 0
    MPP_CHN_S stEvsVpssSrcChn;
    MPP_CHN_S stEvsPvsDestChn;
    stEvsVpssSrcChn.enModId = RK_ID_VPSS;
    stEvsVpssSrcChn.s32DevId = APX_EVS_VPSS_GROUP;
    stEvsVpssSrcChn.s32ChnId = APX_EVS_VPSS_SHOW_CHANNEL;

    stEvsPvsDestChn.enModId = RK_ID_PVS;
    stEvsPvsDestChn.s32DevId = ApxPvsCtx.s32DevId;
    stEvsPvsDestChn.s32ChnId = APX_EVS_PVS_CHANNEL;
    s32Ret = RK_MPI_SYS_Bind(&stEvsVpssSrcChn, &stEvsPvsDestChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed with %#x!", s32Ret);
        return RK_FAILURE;
    }
#endif
    
// --------------- PVS Bind to VO ---------------------------
#if 1
    MPP_CHN_S stPvsSrcChn;
    MPP_CHN_S stVoDestChn;
    stPvsSrcChn.enModId = RK_ID_PVS;
    stPvsSrcChn.s32DevId = ApxPvsCtx.s32DevId;
    stPvsSrcChn.s32ChnId = 0;

    stVoDestChn.enModId = RK_ID_VO;
    stVoDestChn.s32DevId = s32VoLayer;
    stVoDestChn.s32ChnId = 0;
    s32Ret = RK_MPI_SYS_Bind(&stPvsSrcChn, &stVoDestChn);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("failed with %#x!", s32Ret);
        return RK_FAILURE;
    }
#endif

#if 1 
    // enable vo
    s32Ret = RK_MPI_VO_EnableChn(s32VoLayer, 0);
    if (s32Ret != RK_SUCCESS) {
        RK_LOGE("Enalbe vo chn failed, s32Ret = %d\n", s32Ret);
        return RK_FAILURE;
    }
#endif

#endif // VO

#if 0
    pthread_t ShowControlThread;
    // pthread_create(&ShowControlThread, NULL, ShowControlThreadEntry, NULL);



    uint32_t count = 0;
    VIDEO_FRAME_INFO_S stViFrame;
    VIDEO_FRAME_INFO_S stViFramePerson;
    alp_input_api_t input_data;
    alp_handle_api_t handle = NULL;
    alp_handle_api_t motion_handle = NULL;

    alp_input_api_t input_person_data;

    // Init_Alp_Detector_Api(handle, "/root/", RK3588_DVS_768x608_INT8);
    // Init_Alp_Motion_Api(motion_handle, APX_IMG_SHOW_HEIGHT, APX_IMG_SHOW_WIDTH, 5, 13, 13, 13, 13, 127);

    memset(&input_data, 0, sizeof(alp_input_api_t));
    memset(&input_person_data, 0, sizeof(alp_input_api_t));

    //input_data.virt_addr = input_buff;
    input_data.width = APX_IMG_SHOW_WIDTH;
    input_data.height = APX_IMG_SHOW_HEIGHT;
    input_data.width_stride = APX_IMG_SHOW_WIDTH;
    input_data.height_stride = APX_IMG_SHOW_HEIGHT;
    input_data.format = ALP_IMAGE_FORMAT_YUV420SP_NV21;

    input_person_data.width = APX_K2_EVS_ALGO_WIDTH;
    input_person_data.height = APX_K2_EVS_ALGO_HEIGHT;
    input_person_data.width_stride = APX_K2_EVS_ALGO_WIDTH;
    input_person_data.height_stride = APX_K2_EVS_ALGO_HEIGHT;
    input_person_data.format = ALP_IMAGE_FORMAT_YUV420SP_NV21;

    alp_box_api_t *person_box;
    // alp_rect_api_t *motion_box;
    int person_num = 0;
    int motion_num = 0;
    
    uint32_t EvsImageY = APX_IMG_SHOW_WIDTH*APX_IMG_SHOW_HEIGHT;
    uint32_t EvsImageUV = APX_IMG_SHOW_WIDTH*APX_IMG_SHOW_HEIGHT/2;
#endif
    signal(SIGINT, sigterm_handler);

    
    while (!bquit) {
        sleep(1);

        shmfifo_get(fifo,&p);
        denoise = p.denoise;
        printf("seq:%d Received config data: ",p.seq);
        for (int i = 0; i < p.flag; i++) {
            printf("%02x ", p.received_buff[i]);
        }
        printf("denoise%d\n",p.denoise);
    }

    
    
    pthread_join(pEvsCtx->ImgHandleThread, RK_NULL);
    #ifdef USB
    pthread_join(pEvsCtx->ImgShowThread, RK_NULL);
    pthread_join(pApsCtx->ImgShowThread, RK_NULL);
    #endif 
    RK_LOGE("---- exit ImgHandleThread!");
    
    ApxPvsStop();

#ifdef VO

    RK_MPI_SYS_UnBind(&stPvsSrcChn, &stVoDestChn);
#if 0
    RK_MPI_SYS_UnBind(&stEvsVpssSrcChn, &stEvsPvsDestChn);
#endif

#if 1
    RK_MPI_SYS_UnBind(&stApsVpssSrcChn, &stApsPvsDestChn);
#endif

#endif // VO
    ApxApsYuvDataPullDelete();
    ApxEvsRawDataPullDelete();

    RK_MPI_SYS_Exit();

    RK_SAFE_FREE(pApsCtx);
    RK_SAFE_FREE(pEvsCtx);
   
    return 0;

}
