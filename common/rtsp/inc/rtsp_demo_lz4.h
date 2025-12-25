/**
 * @file rtsp_demo_lz4.h
 * @brief RTSP扩展：支持LZ4压缩数据和原始帧数据
 * @description 扩展RTSP库以支持自定义payload类型（LZ4压缩数据、原始帧数据）
 */

#ifndef __RTSP_DEMO_LZ4_H__
#define __RTSP_DEMO_LZ4_H__

#include "rtsp_demo_2.h"
#include "rtsp_codec_id_ext.h"  // 扩展Codec ID定义
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LZ4帧数据结构
// ============================================================================

/**
 * @brief LZ4压缩帧元数据（用于codec_data）
 */
typedef struct {
    uint32_t width;           ///< 帧宽度（如768）
    uint32_t height;          ///< 帧高度（如608）
    uint32_t pixel_format;    ///< 像素格式（0=8位灰度）
    uint32_t original_size;   ///< 原始帧大小（字节）
    uint32_t reserved[4];     ///< 预留
} rtsp_lz4_frame_info_t;

// ============================================================================
// 扩展的RTSP函数
// ============================================================================

/**
 * @brief 设置LZ4视频流
 * @param session RTSP会话句柄
 * @param frame_info 帧信息（宽、高、格式等）
 * @return 0成功，-1失败
 */
int rtsp_set_video_lz4(rtsp_session_handle session, const rtsp_lz4_frame_info_t *frame_info);

/**
 * @brief 发送LZ4压缩的视频帧
 * @param session RTSP会话句柄
 * @param compressed_data LZ4压缩的数据
 * @param compressed_size 压缩数据大小
 * @param timestamp_us 时间戳（微秒）
 * @return 发送的字节数（>0成功），<=0失败
 */
int rtsp_tx_video_lz4(rtsp_session_handle session, 
                       const uint8_t *compressed_data, 
                       size_t compressed_size,
                       uint64_t timestamp_us);

/**
 * @brief 发送原始视频帧（未压缩）
 * @param session RTSP会话句柄
 * @param frame_data 原始帧数据
 * @param frame_size 帧数据大小
 * @param timestamp_us 时间戳（微秒）
 * @return 发送的字节数（>0成功），<=0失败
 */
int rtsp_tx_video_raw(rtsp_session_handle session,
                      const uint8_t *frame_data,
                      size_t frame_size,
                      uint64_t timestamp_us);

/**
 * @brief 发送压缩的视频帧（通用接口，自动检测压缩类型）
 * @param session RTSP会话句柄
 * @param compressed_data 压缩数据
 * @param compressed_size 压缩数据大小
 * @param compression_type 压缩类型（0=LZ4，其他保留）
 * @param timestamp_us 时间戳（微秒）
 * @return 发送的字节数（>0成功），<=0失败
 */
int rtsp_tx_video_compressed(rtsp_session_handle session,
                             const uint8_t *compressed_data,
                             size_t compressed_size,
                             int compression_type,
                             uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif // __RTSP_DEMO_LZ4_H__

