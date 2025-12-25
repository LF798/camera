/*************************************************************************
	> File Name: rtsp_demo.h
	> Author: bxq
	> Mail: 544177215@qq.com 
	> Created Time: Monday, November 23, 2015 AM12:22:43 CST
 ************************************************************************/

#ifndef __RTSP_DEMO_H__
#define __RTSP_DEMO_H__
/*
 * a simple RTSP server demo
 * RTP over UDP/TCP H264/G711a 
 * */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rtsp_codec_id {
	RTSP_CODEC_ID_NONE = 0,
	RTSP_CODEC_ID_VIDEO_H264 = 0x0001,	/*codec_data is SPS + PPS frames*/
	RTSP_CODEC_ID_VIDEO_H265,			/*codec_data is VPS + SPS + PPS frames*/
	RTSP_CODEC_ID_VIDEO_MPEG4,			/*now not support*/
	RTSP_CODEC_ID_VIDEO_MJPEG,			/*now not support*/
	RTSP_CODEC_ID_AUDIO_G711A = 0x4001,	/*codec_data is NULL*/
	RTSP_CODEC_ID_AUDIO_G711U,			/*codec_data is NULL*/
	RTSP_CODEC_ID_AUDIO_G726,			/*codec_data is bitrate (int)*/
	RTSP_CODEC_ID_AUDIO_AAC,			/*codec_data is audio specific config (2bytes). frame type is ADTS*/
};

enum rtsp_auth_type {
	RTSP_AUTH_TYPE_NONE = 0,
	RTSP_AUTH_TYPE_BASIC,
	RTSP_AUTH_TYPE_DIGEST, /*now not support*/
};

typedef void * rtsp_demo_handle;
typedef void * rtsp_session_handle;

rtsp_demo_handle rtsp_new_demo (int port);
int rtsp_do_event (rtsp_demo_handle demo);
rtsp_session_handle rtsp_new_session (rtsp_demo_handle demo, const char *path);
int rtsp_set_auth (rtsp_session_handle session, int type, const char *user, const char *passwd);
int rtsp_set_video (rtsp_session_handle session, int codec_id, const uint8_t *codec_data, int data_len);
int rtsp_set_audio (rtsp_session_handle session, int codec_id, const uint8_t *codec_data, int data_len);
int rtsp_tx_video (rtsp_session_handle session, const uint8_t *frame, int len, uint64_t ts);
int rtsp_tx_audio (rtsp_session_handle session, const uint8_t *frame, int len, uint64_t ts);
void rtsp_del_session (rtsp_session_handle session);
void rtsp_del_demo (rtsp_demo_handle demo);

uint64_t rtsp_get_reltime (void);
uint64_t rtsp_get_ntptime (void);
int rtsp_sync_video_ts (rtsp_session_handle session, uint64_t ts, uint64_t ntptime);
int rtsp_sync_audio_ts (rtsp_session_handle session, uint64_t ts, uint64_t ntptime);

/* ==================== 时间戳查询接口 ==================== */

/**
 * 时间戳信息结构体
 * 用于查询 RTSP 会话中视频流的时间戳统计信息
 */
typedef struct {
	uint64_t first_pts;        // 首帧时间戳（微秒）
	uint64_t last_pts;         // 最新帧时间戳（微秒）
	uint64_t min_interval_us;  // 最小帧间隔（微秒）
	uint64_t max_interval_us;  // 最大帧间隔（微秒）
	uint64_t avg_interval_us;  // 平均帧间隔（微秒）
	uint64_t frame_count;      // 已发送帧数
	uint64_t total_bytes;      // 已发送总字节数
} rtsp_ts_info_t;

/**
 * 查询视频流的时间戳信息
 * @param session RTSP 会话句柄
 * @param info 输出参数，时间戳信息结构体指针
 * @return 0=成功, -1=失败（session无效或未发送视频帧）
 */
int rtsp_get_video_ts_info(rtsp_session_handle session, rtsp_ts_info_t *info);

/**
 * 重置视频流的时间戳统计信息
 * @param session RTSP 会话句柄
 * @return 0=成功, -1=失败
 */
int rtsp_reset_video_ts_info(rtsp_session_handle session);

void *test_rtsp(void *arg);
void *rtsp_process(void *arg);

#ifdef __cplusplus
}
#endif
#endif
