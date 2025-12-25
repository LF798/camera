/**
 * @file rtsp_internal.h
 * @brief RTSP库内部结构定义
 * @description 供LZ4扩展等模块使用的内部结构定义
 */

#ifndef __RTSP_INTERNAL_H__
#define __RTSP_INTERNAL_H__

#include "rtsp_demo_2.h"
#include "rtp_enc.h"
#include "stream_queue.h"
#include "queue.h"
#include "utils.h"
#include "comm.h"
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// SOCKET类型定义（与rtsp.c中一致）
#ifdef __WIN32__
#include <winsock2.h>
#else
#define SOCKET_ERROR	(-1)
#define INVALID_SOCKET  (-1)
typedef int SOCKET;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
struct rtsp_demo;
struct rtsp_session;
struct rtsp_client_connection;

// 队列头定义
TAILQ_HEAD(rtsp_session_queue_head, rtsp_session);
TAILQ_HEAD(rtsp_client_connection_queue_head, rtsp_client_connection);

// RTSP会话结构（从rtsp.c中复制，确保一致性）
struct rtsp_session
{
	char path[64];
	int  vcodec_id;
	int  acodec_id;

	int  auth_type;
	char auth_user[16];
	char auth_passwd[16];

	union {
		struct codec_data_h264 h264;
		struct codec_data_h265 h265;
	} vcodec_data;

	union {
		struct codec_data_g726 g726;
		struct codec_data_aac aac;
	} acodec_data;

	rtp_enc vrtpe;
	rtp_enc artpe;
	struct stream_queue *vstreamq;
	struct stream_queue *astreamq;

	uint64_t video_ntptime_of_zero_ts;
	uint64_t audio_ntptime_of_zero_ts;
	
	// ✅ 时间戳统计信息（用于时间戳查询接口）
	struct {
		uint64_t first_pts;        // 首帧时间戳（微秒）
		uint64_t last_pts;         // 最新帧时间戳（微秒）
		uint64_t prev_pts;         // 上一帧时间戳（用于计算间隔）
		uint64_t min_interval_us;  // 最小帧间隔（微秒）
		uint64_t max_interval_us;  // 最大帧间隔（微秒）
		uint64_t total_interval_us;// 总间隔（用于计算平均值）
		uint64_t frame_count;      // 已发送帧数
		uint64_t total_bytes;      // 已发送总字节数
		int initialized;           // 是否已初始化
	} video_ts_stats;
	
	// LZ4扩展字段（条件编译）
	#ifdef ENABLE_RTSP_LZ4_SUPPORT
	#include "rtsp_demo_lz4.h"
	rtsp_lz4_frame_info_t lz4_frame_info;
	int lz4_frame_info_valid;
	#else
	// 占位符，保持结构大小一致（当LZ4未启用时）
	char lz4_reserved[sizeof(uint32_t) * 5 + sizeof(int)];  // 约等于rtsp_lz4_frame_info_t + int
	#endif
	
	// EVT2扩展字段（条件编译）
	#ifdef ENABLE_RTSP_EVT2_SUPPORT
	#include "rtsp_demo_evt2.h"
	rtsp_evt2_stream_info_t evt2_stream_info;
	int evt2_stream_info_valid;
	#else
	// 占位符，保持结构大小一致（当EVT2未启用时）
	char evt2_reserved[sizeof(uint32_t) * 6 + sizeof(int)];  // 约等于rtsp_evt2_stream_info_t + int
	#endif
	
	struct rtsp_demo *demo;
	struct rtsp_client_connection_queue_head connections_qhead;
	TAILQ_ENTRY(rtsp_session) demo_entry;
};

// 常量定义（必须在结构体定义之前，因为它们被用于结构体数组大小）
#ifndef RTP_MAX_PKTSIZ
#define RTP_MAX_PKTSIZ	((1500-42)/4*4)
#endif
#ifndef VRTP_MAX_NBPKTS
#define VRTP_MAX_NBPKTS	(400)
#endif
#ifndef ARTP_MAX_NBPKTS
#define ARTP_MAX_NBPKTS	(10)
#endif
#ifndef RTSP_REQBUF_MAX_SIZ
#define RTSP_REQBUF_MAX_SIZ  (1024)
#endif
#ifndef RTSP_RESBUF_MAX_SIZ
// RTSP_RESBUF_MAX_SIZ 依赖于 RTP_MAX_PKTSIZ，使用相同的计算逻辑
#define RTSP_RESBUF_MAX_SIZ  (RTP_MAX_PKTSIZ+4)
#endif

// 客户端连接状态定义
#define RTSP_CC_STATE_INIT		0
#define RTSP_CC_STATE_READY		1
#define RTSP_CC_STATE_PLAYING	2
#define RTSP_CC_STATE_RECORDING	3

// RTSP客户端连接结构（从rtsp.c中复制，必须与rtsp.c完全一致）
struct rtsp_client_connection
{
	int state;	//session state
	SOCKET sockfd;		//rtsp client socket
	struct in_addr peer_addr; //peer ipv4 addr
	unsigned int   peer_port; //peer ipv4 port
	unsigned long session_id;	//session id
	
	char reqbuf[RTSP_REQBUF_MAX_SIZ];
	int  reqlen;
	
	char resbuf[RTSP_RESBUF_MAX_SIZ];
	int  resoff;
	int  reslen;
	
	struct rtp_connection *vrtp;
	struct rtp_connection *artp;
	
	struct rtsp_demo *demo;
	struct rtsp_session *session;
	TAILQ_ENTRY(rtsp_client_connection) demo_entry;	
	TAILQ_ENTRY(rtsp_client_connection) session_entry;
};

// RTP连接结构（从rtsp.c中复制）
struct rtp_connection
{
	int is_over_tcp;
	int tcp_interleaved[2];
	SOCKET udp_sockfd[2];
	uint16_t udp_localport[2];
	uint16_t udp_peerport[2];
	struct in_addr peer_addr;
	int streamq_index;
	uint32_t ssrc;
	uint32_t rtcp_packet_count;
	uint32_t rtcp_octet_count;
	uint64_t rtcp_last_ts;
};

// ============================================================================
// 内部函数声明（供扩展模块使用）
// ============================================================================

/**
 * @brief 发送视频RTP包（内部函数，供扩展模块使用）
 * @param cc 客户端连接
 * @return 0成功，-1失败
 */
int rtsp_tx_video_packet(struct rtsp_client_connection *cc);

/**
 * @brief 尝试发送RTCP SR包（内部函数，供扩展模块使用）
 * @param cc 客户端连接
 * @param isaudio 是否为音频（1=音频，0=视频）
 * @param ts 时间戳（微秒）
 * @return 0成功，-1失败
 */
int rtsp_try_tx_rtcp_sr(struct rtsp_client_connection *cc, int isaudio, uint64_t ts);

#ifdef __cplusplus
}
#endif

#endif // __RTSP_INTERNAL_H__

