/* Copyright 2022 Rockchip Electronics Co. LTD
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
 *
 */
#ifndef INCLUDE_RT_MPI_RK_MPI_PVS_H__
#define INCLUDE_RT_MPI_RK_MPI_PVS_H__

#include "rk_common.h"
#include "rk_comm_video.h"
#include "rk_comm_pvs.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

RK_S32 RK_MPI_PVS_EnableDev(PVS_DEV PvsDevId);
RK_S32 RK_MPI_PVS_DisableDev(PVS_DEV PvsDevId);

RK_S32 RK_MPI_PVS_EnableChn(PVS_DEV PvsDevId, PVS_CHN PvsChnId);
RK_S32 RK_MPI_PVS_DisableChn(PVS_DEV PvsDevId, PVS_CHN PvsChnId);

RK_S32 RK_MPI_PVS_SetDevAttr(PVS_DEV PvsDevId, const PVS_DEV_ATTR_S *pstDevAttr);
RK_S32 RK_MPI_PVS_GetDevAttr(PVS_DEV PvsDevId, PVS_DEV_ATTR_S *pstDevAttr);

RK_S32 RK_MPI_PVS_SetChnAttr(PVS_DEV PvsDevId, PVS_CHN PvsChnId, PVS_CHN_ATTR_S *pstChnAttr);
RK_S32 RK_MPI_PVS_GetChnAttr(PVS_DEV PvsDevId, PVS_CHN PvsChnId, PVS_CHN_ATTR_S *pstChnAttr);

RK_S32 RK_MPI_PVS_SetChnParam(PVS_DEV PvsDevId, PVS_CHN PvsChnId, PVS_CHN_PARAM_S *pstChnParam);
RK_S32 RK_MPI_PVS_GetChnParam(PVS_DEV PvsDevId, PVS_CHN PvsChnId, PVS_CHN_PARAM_S *pstChnParam);

RK_S32 RK_MPI_PVS_SendFrame(PVS_DEV PvsDevId, PVS_CHN PvsChnId, const VIDEO_FRAME_INFO_S *pstFrameInfo);
RK_S32 RK_MPI_PVS_GetFrame(PVS_DEV PvsDevId, VIDEO_FRAME_INFO_S *pstFrameInfo, RK_S32 s32MilliSec);
RK_S32 RK_MPI_PVS_ReleaseFrame(const VIDEO_FRAME_INFO_S *pstFrameInfo);

RK_S32 RK_MPI_PVS_SetVProcDev(PVS_DEV PvsDevId, VIDEO_PROC_DEV_TYPE_E enVProcDev);
RK_S32 RK_MPI_PVS_GetVProcDev(PVS_DEV PvsDevId, VIDEO_PROC_DEV_TYPE_E *enVProcDev);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* INCLUDE_RT_MPI_RK_MPI_PVS_H__ */
