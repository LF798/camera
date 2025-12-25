/**
 * @file rtsp_codec_id_ext.h
 * @brief RTSP扩展Codec ID定义
 * @description 统一管理所有RTSP扩展codec ID，避免重复定义
 */

#ifndef __RTSP_CODEC_ID_EXT_H__
#define __RTSP_CODEC_ID_EXT_H__

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 扩展的Codec ID
// ============================================================================

/**
 * @brief RTSP扩展Codec ID枚举
 * @note 所有扩展codec ID统一在此定义，避免重复定义
 */
enum rtsp_codec_id_ext {
    RTSP_CODEC_ID_VIDEO_LZ4 = 0x0100,      ///< LZ4压缩的帧数据
    RTSP_CODEC_ID_VIDEO_RAW_FRAME,         ///< 原始帧数据（768×608，8位灰度）
    RTSP_CODEC_ID_VIDEO_COMPRESSED_FRAME,  ///< 压缩帧数据（通用，包含压缩类型）
    RTSP_CODEC_ID_VIDEO_EVT2 = 0x0102,     ///< EVT2编码的事件数据
};

#ifdef __cplusplus
}
#endif

#endif // __RTSP_CODEC_ID_EXT_H__

