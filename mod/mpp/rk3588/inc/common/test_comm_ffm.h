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

#ifndef SRC_TESTS_RT_MPI_COMMON_TEST_COMM_FFM_H_
#define SRC_TESTS_RT_MPI_COMMON_TEST_COMM_FFM_H_

#include "rk_debug.h"
#include "rk_comm_vdec.h"

#ifndef FFM_INT64_MIN
#define FFM_INT64_MIN               (-0x7fffffffffffffffLL-1)
#endif

#ifndef FFM_INT64_MAX
#define FFM_INT64_MAX               INT64_C(9223372036854775807)
#endif

// extern "C" {
    #include "libavformat/avformat.h"
    #include "libavformat/version.h"
    #include "libavutil/avutil.h"
    #include "libavutil/opt.h"
// }

typedef struct _rkStreamInfo {
    VDEC_CHN VdecChn;
    void *pFFmCtx;
    void *pExtraData;
    RK_U32 u32ExtraDataSize;
    RK_U32 u32StreamIndex;
    RK_BOOL bFindKeyFrame;
    RK_CODEC_ID_E enCodecId;
    VIDEO_MODE_E enMode;
    RK_U32 u32PicWidth;
    RK_U32 u32PicHeight;
    RK_BOOL bUseSeqAsPts;
} STREAM_INFO_S;

typedef RK_S32 (*RK_FREE_CB)(void *);

typedef struct _rkStreamData_S {
    RK_U8              *pu8VirAddr;
    RK_U64              u64Size;
    RK_FREE_CB          pFreeCB;
    RK_VOID            *pOpaque;
    RK_BOOL             bEndOfStream;
    RK_BOOL             bEndOfFrame;
    RK_U64              u64PTS;
} STREAM_DATA_S;

RK_S32 TEST_COMM_CodecIDFfmpegToRK(RK_S32 s32Id);
RK_S32 TEST_COMM_FFmParserOpen(const char *uri, STREAM_INFO_S *pstStreamInfo);
RK_S32 TEST_COMM_FFmParserRead(STREAM_INFO_S *pstStreamInfo, STREAM_DATA_S *pstStreamData);
RK_S32 TEST_COMM_FFmParserSeek(STREAM_INFO_S *pstStreamInfo, RK_S64 s64Pos, RK_U32 flag);
RK_S32 TEST_COMM_FFmParserClose(STREAM_INFO_S *pstStreamInfo);

#endif  // SRC_TESTS_RT_MPI_COMMON_TEST_COMM_FFM_H_

