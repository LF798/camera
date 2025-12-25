/**
 * @file rtsp_demo_evt2.h
 * @brief RTSP扩展：支持EVT2编码事件数据流
 * @description 扩展RTSP库以支持EVT2格式的事件数据传输
 */

#ifndef __RTSP_DEMO_EVT2_H__
#define __RTSP_DEMO_EVT2_H__

#include "rtsp_demo_2.h"
#include "rtsp_codec_id_ext.h"  // 扩展Codec ID定义
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// EVT2流信息结构
// ============================================================================

/**
 * @brief EVT2流信息结构（用于codec_data）
 */
typedef struct {
    uint32_t max_events_per_frame;  ///< 最大事件数/帧（如50000）
    uint32_t width;                  ///< 图像宽度（用于坐标范围，如768）
    uint32_t height;                 ///< 图像高度（用于坐标范围，如608）
    uint32_t reserved[4];           ///< 预留
} rtsp_evt2_stream_info_t;

// ============================================================================
// 扩展的RTSP函数
// ============================================================================

/**
 * @brief 设置EVT2视频流
 * @param session RTSP会话句柄
 * @param stream_info 流信息（最大事件数、图像尺寸等）
 * @return 0成功，-1失败
 */
int rtsp_set_video_evt2(rtsp_session_handle session, 
                        const rtsp_evt2_stream_info_t *stream_info);

/**
 * @brief 发送EVT2编码的视频帧
 * @param session RTSP会话句柄
 * @param evt2_data EVT2格式数据
 * @param evt2_size EVT2数据大小（字节）
 * @param frame_id 帧序号
 * @param event_count 事件数量
 * @param timestamp_us 时间戳（微秒）
 * @return 发送的字节数（>0成功），<=0失败
 */
int rtsp_tx_video_evt2(rtsp_session_handle session,
                       const uint8_t *evt2_data,
                       size_t evt2_size,
                       uint32_t frame_id,
                       uint32_t event_count,
                       uint64_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif // __RTSP_DEMO_EVT2_H__

