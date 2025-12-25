/*
 * Copyright 2020 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test_comm_ffm.h"

#include "rk_common.h"

#define SIZE_ARRAY_ELEMS(a)          (sizeof(a) / sizeof((a)[0]))

typedef struct _rkCodecInfo {
    RK_CODEC_ID_E enRkCodecId;
    enum AVCodecID enAvCodecId;
    char mine[16];
} CODEC_INFO;

static CODEC_INFO gCodecMapList[] = {
    { RK_VIDEO_ID_MPEG1VIDEO,    AV_CODEC_ID_MPEG1VIDEO,  "mpeg1" },
    { RK_VIDEO_ID_MPEG2VIDEO,    AV_CODEC_ID_MPEG2VIDEO,  "mpeg2" },
    { RK_VIDEO_ID_H263,          AV_CODEC_ID_H263,  "h263"  },
    { RK_VIDEO_ID_MPEG4,         AV_CODEC_ID_MPEG4, "mpeg4"  },
    { RK_VIDEO_ID_WMV,           AV_CODEC_ID_WMV3,  "wmv3" },
    { RK_VIDEO_ID_AVC,           AV_CODEC_ID_H264,  "h264" },
    { RK_VIDEO_ID_MJPEG,         AV_CODEC_ID_MJPEG, "mjpeg" },
    { RK_VIDEO_ID_VP8,           AV_CODEC_ID_VP8,   "vp8" },
    { RK_VIDEO_ID_VP9,           AV_CODEC_ID_VP9,   "vp9" },
    { RK_VIDEO_ID_HEVC,          AV_CODEC_ID_HEVC,  "hevc" },
    { RK_VIDEO_ID_VC1,           AV_CODEC_ID_VC1,   "vc1" },
    { RK_VIDEO_ID_AVS,           AV_CODEC_ID_AVS,   "avs" },
    { RK_VIDEO_ID_AVS,           AV_CODEC_ID_CAVS,  "cavs" },
    { RK_VIDEO_ID_AVSPLUS,       AV_CODEC_ID_CAVS,  "avs+" },
    { RK_VIDEO_ID_AVS2,          AV_CODEC_ID_AVS2,  "avs2" },
    { RK_VIDEO_ID_FLV1,          AV_CODEC_ID_FLV1,  "flv1" },
    { RK_VIDEO_ID_AV1,           AV_CODEC_ID_AV1,   "av1" },
};

RK_S32 TEST_COMM_CodecIDFfmpegToRK(RK_S32 s32Id) {
    RK_BOOL bFound = RK_FALSE;
    RK_U32 i = 0;
    for (i = 0; i < SIZE_ARRAY_ELEMS(gCodecMapList); i++) {
        if (s32Id == gCodecMapList[i].enAvCodecId) {
            bFound = RK_TRUE;
            break;
        }
    }

    if (bFound)
        return gCodecMapList[i].enRkCodecId;
    else
        return RK_VIDEO_ID_Unused;
}

static RK_S32 mpi_ffmpeg_free(void *opaque) {
    AVPacket* avPkt = (AVPacket*)(opaque);
    if (RK_NULL != avPkt) {
        av_packet_unref(avPkt);
        av_packet_free(&avPkt);
    }
    avPkt = RK_NULL;
    return 0;
}

RK_S32 TEST_COMM_FFmParserOpen(const char *uri, STREAM_INFO_S *pstStreamInfo) {
    RK_S32 s32Ret = 0;
    RK_U32 u32RetryNum = 5;
    RK_BOOL bFindStream = RK_FALSE;
    const AVStream *pStream = RK_NULL;
    AVFormatContext *pAvfc = RK_NULL;

    avformat_network_init();

    pAvfc = avformat_alloc_context();

__RETRY:
    s32Ret = avformat_open_input(&pAvfc, uri, NULL, NULL);
    if (s32Ret < 0) {
        if (s32Ret == -110 && u32RetryNum >= 0) {
            RK_LOGE("AGAIN");
            u32RetryNum--;
            goto __RETRY;
        } else {
            RK_LOGE("open input %s failed!", uri);
            goto __FAILED;
        }
    }

    if (avformat_find_stream_info(pAvfc, NULL) < 0) {
        goto __FAILED;
    }

    RK_LOGV("found stream num: %d", pAvfc->nb_streams);
    for (RK_S32 i = 0; i < pAvfc->nb_streams; i++) {
        pStream = pAvfc->streams[i];

        if (RK_NULL != pStream && pStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            pstStreamInfo->enCodecId = (RK_CODEC_ID_E)TEST_COMM_CodecIDFfmpegToRK(pStream->codecpar->codec_id);
            pstStreamInfo->enMode = VIDEO_MODE_FRAME;
            pstStreamInfo->u32PicWidth = pStream->codecpar->width;
            pstStreamInfo->u32PicHeight = pStream->codecpar->height;

            pstStreamInfo->pExtraData = pStream->codecpar->extradata;
            pstStreamInfo->u32ExtraDataSize = pStream->codecpar->extradata_size;
            pstStreamInfo->u32StreamIndex = pStream->index;
            pstStreamInfo->pFFmCtx = pAvfc;

            RK_LOGV("found video stream width %d height %d",
                pstStreamInfo->u32PicWidth, pstStreamInfo->u32PicHeight);
            bFindStream = RK_TRUE;
            break;
        }
    }

    if (!bFindStream)
        goto __FAILED;

    RK_LOGI("open avformat success");
    return RK_SUCCESS;

__FAILED:
    if (pAvfc) {
        avformat_close_input(&pAvfc);
        avformat_free_context(pAvfc);
    }

    avformat_network_deinit();
    return RK_FAILURE;
}

RK_S32 TEST_COMM_FFmParserRead(STREAM_INFO_S *pstStreamInfo, STREAM_DATA_S *pstStreamData) {
    RK_S32 s32Ret = 0;
    RK_U64 u64PTS = 0;

    VDEC_STREAM_S stStream;
    AVStream *vStream = RK_NULL;
    MB_BLK buffer = RK_NULL;
    MB_EXT_CONFIG_S stMbExtConfig;
    AVPacket *avPacket = RK_NULL;
    RK_BOOL bEos = RK_FALSE;
    RK_BOOL bFindKeyFrame = RK_FALSE;
    AVFormatContext *pAvfc = (AVFormatContext *)pstStreamInfo->pFFmCtx;

    memset(&stMbExtConfig, 0, sizeof(MB_EXT_CONFIG_S));

_RETY:
    avPacket = av_packet_alloc();
    av_init_packet(avPacket);
    if (pstStreamInfo->u32ExtraDataSize > 0) {
        avPacket->data = (uint8_t *)(pstStreamInfo->pExtraData);
        avPacket->size = pstStreamInfo->u32ExtraDataSize;
        avPacket->stream_index = pstStreamInfo->u32StreamIndex;
        avPacket->flags = AV_PKT_FLAG_KEY;
        pstStreamInfo->u32ExtraDataSize = 0;
    } else {
        s32Ret = av_read_frame(pAvfc, avPacket);
        if (s32Ret == AVERROR_EOF || s32Ret == AVERROR_EXIT) {
            mpi_ffmpeg_free(avPacket);

            avformat_seek_file(pAvfc, pstStreamInfo->u32StreamIndex,
                INT64_MIN, 0, INT64_MAX, AVSEEK_FLAG_BYTE);

            pstStreamData->bEndOfStream = RK_TRUE;
            return RK_SUCCESS;
        }
    }

    if (pstStreamInfo->u32StreamIndex == avPacket->stream_index) {
        if (!pstStreamInfo->bFindKeyFrame) {
            if (avPacket->flags & AV_PKT_FLAG_KEY == AV_PKT_FLAG_KEY) {
                pstStreamInfo->bFindKeyFrame = RK_TRUE;
            } else {
                mpi_ffmpeg_free(avPacket);
                goto _RETY;
            }
        }

        vStream = pAvfc->streams[avPacket->stream_index];
        u64PTS = av_q2d(vStream->time_base)*(avPacket->pts)*1000000ll;

        pstStreamData->pFreeCB = mpi_ffmpeg_free;
        pstStreamData->pOpaque = avPacket;
        pstStreamData->pu8VirAddr = avPacket->data;
        pstStreamData->u64Size = avPacket->size;
        pstStreamData->u64PTS = u64PTS;
        pstStreamData->bEndOfStream = RK_FALSE;
        pstStreamData->bEndOfFrame = RK_FALSE;
        return RK_SUCCESS;
    } else {
        mpi_ffmpeg_free(avPacket);
    }

    return RK_FAILURE;
}

RK_S32 TEST_COMM_FFmParserSeek(STREAM_INFO_S *pstStreamInfo, RK_S64 s64Pos, RK_U32 flag) {
    avformat_seek_file((AVFormatContext *)pstStreamInfo->pFFmCtx, pstStreamInfo->u32StreamIndex,
                        FFM_INT64_MIN, s64Pos, FFM_INT64_MAX, flag);
    return RK_SUCCESS;
}

RK_S32 TEST_COMM_FFmParserClose(STREAM_INFO_S *pstStreamInfo) {
    if (pstStreamInfo->pFFmCtx != RK_NULL) {
        avformat_close_input((AVFormatContext **)&(pstStreamInfo->pFFmCtx));
        avformat_free_context((AVFormatContext *)pstStreamInfo->pFFmCtx);
        pstStreamInfo->pFFmCtx = RK_NULL;
    }

    avformat_network_deinit();
    return RK_SUCCESS;
}

