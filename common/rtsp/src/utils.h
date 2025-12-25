/*************************************************************************
	> File Name: utils.h
	> Author: bxq
	> Mail: 544177215@qq.com 
	> Created Time: Sunday, May 22, 2016 PM09:35:22 CST
 ************************************************************************/

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct codec_data_h264 {
	uint8_t sps[64]; //no nal leader code 001
	uint8_t pps[64];
	uint32_t sps_len;
	uint32_t pps_len;
};

struct codec_data_h265 {
	uint8_t vps[64];
	uint8_t sps[64];
	uint8_t pps[64];
	uint32_t vps_len;
	uint32_t sps_len;
	uint32_t pps_len;
};

struct codec_data_g726 {
	uint32_t bit_rate;
};

struct codec_data_aac {
	uint8_t audio_specific_config[64];
	uint32_t audio_specific_config_len;
	uint32_t sample_rate;
	uint32_t channels;
};

char *base64_encode (char *out, int out_size, const uint8_t *in, int in_size);
const uint8_t *rtsp_find_h264_h265_nalu (const uint8_t *buff, int len, int *size);

int rtsp_codec_data_parse_from_user_h264 (const uint8_t *codec_data, int data_len, struct codec_data_h264 *pst_codec_data);
int rtsp_codec_data_parse_from_user_h265 (const uint8_t *codec_data, int data_len, struct codec_data_h265 *pst_codec_data);
int rtsp_codec_data_parse_from_user_g726 (const uint8_t *codec_data, int data_len, struct codec_data_g726 *pst_codec_data);
int rtsp_codec_data_parse_from_user_aac (const uint8_t *codec_data, int data_len, struct codec_data_aac *pst_codec_data);

int rtsp_codec_data_parse_from_frame_h264 (const uint8_t *frame, int len, struct codec_data_h264 *pst_codec_data);
int rtsp_codec_data_parse_from_frame_h265 (const uint8_t *frame, int len, struct codec_data_h265 *pst_codec_data);
int rtsp_codec_data_parse_from_frame_aac (const uint8_t *frame, int len, struct codec_data_aac *pst_codec_data);

int rtsp_build_sdp_media_attr_h264 (int pt, int sample_rate, const struct codec_data_h264 *pst_codec_data, char *sdpbuf, int maxlen);
int rtsp_build_sdp_media_attr_h265 (int pt, int sample_rate, const struct codec_data_h265 *pst_codec_data, char *sdpbuf, int maxlen);
int rtsp_build_sdp_media_attr_g711a (int pt, int sample_rate, char *sdpbuf, int maxlen);
int rtsp_build_sdp_media_attr_g711u (int pt, int sample_rate, char *sdpbuf, int maxlen);
int rtsp_build_sdp_media_attr_g726 (int pt, int sample_rate, const struct codec_data_g726 *pst_codec_data, char *sdpbuf, int maxlen);
int rtsp_build_sdp_media_attr_aac (int pt, int sample_rate, const struct codec_data_aac *pst_codec_data, char *sdpbuf, int maxlen);

// LZ4扩展（条件编译）

#ifdef ENABLE_RTSP_LZ4_SUPPORT
#include "rtsp_demo_lz4.h"
int rtsp_build_sdp_media_attr_lz4 (int pt, int sample_rate, const rtsp_lz4_frame_info_t *frame_info, char *sdpbuf, int maxlen);
#endif

// EVT2扩展（条件编译）

#ifdef ENABLE_RTSP_EVT2_SUPPORT
#include "rtsp_demo_evt2.h"
int rtsp_build_sdp_media_attr_evt2 (int pt, int sample_rate, const rtsp_evt2_stream_info_t *stream_info, char *sdpbuf, int maxlen);
#endif

#ifdef __cplusplus
}
#endif
#endif
