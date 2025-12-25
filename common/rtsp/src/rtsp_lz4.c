/**
 * @file rtsp_lz4.c
 * @brief RTSP扩展实现：支持LZ4压缩数据和原始帧数据
 */

#include "rtsp_demo_lz4.h"
#include "rtsp_demo_2.h"
#include "rtsp_internal.h"
#include "rtp_enc.h"
#include "utils.h"
#include "comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// RTP payload类型定义（动态类型96-127）
#define RTP_PT_LZ4        96  // LZ4压缩数据
#define RTP_PT_RAW_FRAME  97  // 原始帧数据

// RTP头部结构（从rtp_enc.c中复制）
struct rtphdr {
#ifdef __BIG_ENDIAN__
    uint16_t v:2;
    uint16_t p:1;
    uint16_t x:1;
    uint16_t cc:4;
    uint16_t m:1;
    uint16_t pt:7;
#else
    uint16_t cc:4;
    uint16_t x:1;
    uint16_t p:1;
    uint16_t v:2;
    uint16_t pt:7;
    uint16_t m:1;
#endif
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
};

#define RTPHDR_SIZE 12

// LZ4帧数据包结构（RTP payload）
typedef struct {
    uint32_t frame_id;        ///< 帧序号
    uint32_t original_size;   ///< 原始帧大小
    uint32_t compressed_size; ///< 压缩数据大小
    uint8_t  data[0];         ///< 压缩数据（变长）
} __attribute__((packed)) lz4_rtp_payload_t;

/**
 * @brief 设置LZ4视频流
 */
int rtsp_set_video_lz4(rtsp_session_handle session, const rtsp_lz4_frame_info_t *frame_info) {
    struct rtsp_session *s = (struct rtsp_session*)session;
    if (!s || !frame_info) {
        return -1;
    }

    // 使用自定义codec_id
    s->vcodec_id = RTSP_CODEC_ID_VIDEO_LZ4;
    s->vrtpe.pt = RTP_PT_LZ4;
    s->vrtpe.seq = 0;
    s->vrtpe.ssrc = 0;
    s->vrtpe.sample_rate = 90000;  // 90kHz时钟

    // 存储帧信息
    #ifdef __RTSP_DEMO_LZ4_H__
    if (frame_info) {
        memcpy(&s->lz4_frame_info, frame_info, sizeof(rtsp_lz4_frame_info_t));
        s->lz4_frame_info_valid = 1;
    } else {
        s->lz4_frame_info_valid = 0;
    }
    #endif

    if (!s->vstreamq) {
        s->vstreamq = streamq_alloc(RTP_MAX_PKTSIZ, VRTP_MAX_NBPKTS + 1);
        if (!s->vstreamq) {
            err("alloc memory for video rtp queue failed\n");
            s->vcodec_id = RTSP_CODEC_ID_NONE;
            return -1;
        }
    }

    return 0;
}

/**
 * @brief RTP封装LZ4数据
 */
/**
 * @brief RTP封装LZ4数据到队列缓冲区
 * @note 直接使用队列提供的缓冲区，不进行内存分配
 */
static int rtp_enc_lz4_to_queue(rtp_enc *enc, 
                                 const uint8_t *compressed_data,
                                 size_t compressed_size,
                                 uint32_t frame_id,
                                 uint32_t original_size,
                                 uint64_t ts,
                                 uint8_t **packets,
                                 int *pktsizs,
                                 int max_packets) {
    if (!enc || !compressed_data || compressed_size == 0 || !packets || !pktsizs || max_packets <= 0) {
        return -1;
    }

    size_t max_payload = RTP_MAX_PKTSIZ - RTPHDR_SIZE;
    size_t payload_header_size = sizeof(lz4_rtp_payload_t) - sizeof(((lz4_rtp_payload_t *)0)->data);
    size_t total_payload_size = payload_header_size + compressed_size;
    uint32_t rtp_ts = (uint32_t)(ts * enc->sample_rate / 1000000);
    
    if (total_payload_size <= max_payload) {
        // 单包发送
        if (!packets[0] || pktsizs[0] < RTPHDR_SIZE) {
            return -1;
        }
        
        uint8_t *pkt = packets[0];
        
        // RTP头部
        struct rtphdr *rtp_hdr = (struct rtphdr *)pkt;
        memset(rtp_hdr, 0, RTPHDR_SIZE);
        rtp_hdr->v = 2;
        rtp_hdr->p = 0;
        rtp_hdr->x = 0;
        rtp_hdr->cc = 0;
        rtp_hdr->m = 1;  // marker = 1（最后一包）
        rtp_hdr->pt = enc->pt;
        rtp_hdr->seq = htons(enc->seq++);
        rtp_hdr->ts = htonl((uint32_t)(ts * enc->sample_rate / 1000000));  // 90kHz
        rtp_hdr->ssrc = htonl(enc->ssrc);

        // LZ4 payload
        lz4_rtp_payload_t *payload = (lz4_rtp_payload_t *)(pkt + RTPHDR_SIZE);
        payload->frame_id = htonl(frame_id);
        payload->original_size = htonl(original_size);
        payload->compressed_size = htonl(compressed_size);
        memcpy(payload->data, compressed_data, compressed_size);

        pktsizs[0] = RTPHDR_SIZE + total_payload_size;
        return 1;  // 返回包数
    } else {
        // 需要分片
        // 🔥 分片发送
        int pkt_count = 0;
        const uint8_t *data_ptr = compressed_data;
        size_t remaining = compressed_size;
        
        // 第一个包：包含 payload header + 部分数据
        if (pkt_count >= max_packets || !packets[pkt_count] || pktsizs[pkt_count] < RTPHDR_SIZE) {
            return -1;
        }
        
        uint8_t *pkt = packets[pkt_count];
        struct rtphdr *rtp_hdr = (struct rtphdr *)pkt;
        memset(rtp_hdr, 0, RTPHDR_SIZE);
        rtp_hdr->v = 2;
        rtp_hdr->p = 0;
        rtp_hdr->x = 0;
        rtp_hdr->cc = 0;
        rtp_hdr->m = 0;  // 不是最后一包
        rtp_hdr->pt = enc->pt;
        rtp_hdr->seq = htons(enc->seq++);
        rtp_hdr->ts = htonl(rtp_ts);
        rtp_hdr->ssrc = htonl(enc->ssrc);
        
        // 第一个包的 payload：header + 数据
        lz4_rtp_payload_t *payload = (lz4_rtp_payload_t *)(pkt + RTPHDR_SIZE);
        payload->frame_id = htonl(frame_id);
        payload->original_size = htonl(original_size);
        payload->compressed_size = htonl(compressed_size);
        
        // 计算第一个包能容纳的数据量
        size_t first_pkt_data_size = max_payload - payload_header_size;
        if (first_pkt_data_size > remaining) {
            first_pkt_data_size = remaining;
        }
        
        memcpy(payload->data, data_ptr, first_pkt_data_size);
        pktsizs[pkt_count] = RTPHDR_SIZE + payload_header_size + first_pkt_data_size;
        data_ptr += first_pkt_data_size;
        remaining -= first_pkt_data_size;
        pkt_count++;
        
        // 后续包：只包含数据片段
        while (remaining > 0 && pkt_count < max_packets && packets[pkt_count] && pktsizs[pkt_count] > RTPHDR_SIZE) {
            pkt = packets[pkt_count];
            rtp_hdr = (struct rtphdr *)pkt;
            memset(rtp_hdr, 0, RTPHDR_SIZE);
            rtp_hdr->v = 2;
            rtp_hdr->p = 0;
            rtp_hdr->x = 0;
            rtp_hdr->cc = 0;
            rtp_hdr->pt = enc->pt;
            rtp_hdr->seq = htons(enc->seq++);
            rtp_hdr->ts = htonl(rtp_ts);  // 相同时间戳
            rtp_hdr->ssrc = htonl(enc->ssrc);
            
            // 计算这个包能容纳的数据量
            size_t fragment_size = max_payload;
            if (fragment_size > remaining) {
                fragment_size = remaining;
                rtp_hdr->m = 1;  // 最后一包，设置 marker
            } else {
                rtp_hdr->m = 0;
            }
            
            // 复制数据片段
            memcpy(pkt + RTPHDR_SIZE, data_ptr, fragment_size);
            pktsizs[pkt_count] = RTPHDR_SIZE + fragment_size;
            
            data_ptr += fragment_size;
            remaining -= fragment_size;
            pkt_count++;
        }
        
        if (remaining > 0) {
            err("LZ4 data too large, cannot fit in %d packets\n", max_packets);
            return -1;
        }
        
        return pkt_count;
    }
}

/**
 * @brief 发送LZ4压缩的视频帧
 * @note 参考rtsp_tx_video的实现，使用队列缓冲区
 */
int rtsp_tx_video_lz4(rtsp_session_handle session, 
                       const uint8_t *compressed_data, 
                       size_t compressed_size,
                       uint64_t timestamp_us) {
    struct rtsp_session *s = (struct rtsp_session*)session;
    struct stream_queue *q = NULL;
    struct rtsp_client_connection *cc = NULL;
    uint8_t *packets[VRTP_MAX_NBPKTS+1] = {NULL};
    int  pktsizs[VRTP_MAX_NBPKTS+1] = {0};
    int *pktlens[VRTP_MAX_NBPKTS] = {NULL};
    int i, index, count;
    
    if (!s || !compressed_data || compressed_size == 0) {
        return -1;
    }

    if (s->vcodec_id != RTSP_CODEC_ID_VIDEO_LZ4) {
        err("video codec is not LZ4\n");
        return -1;
    }

    // 获取队列
    q = s->vstreamq;
    if (!q) {
        return -1;
    }

    // 估算需要的RTP包数量
    size_t payload_header_size = sizeof(lz4_rtp_payload_t) - sizeof(((lz4_rtp_payload_t *)0)->data);
    size_t total_payload = payload_header_size + compressed_size;
    count = (total_payload + RTP_MAX_PKTSIZ - RTPHDR_SIZE - 1) / (RTP_MAX_PKTSIZ - RTPHDR_SIZE);
    if (count == 0) count = 1;
    if (count > VRTP_MAX_NBPKTS) count = VRTP_MAX_NBPKTS;

    // 获取队列缓冲区
    index = streamq_tail(q);
    for (i = 0; i < VRTP_MAX_NBPKTS && i < count; i++) {
        if (streamq_next(q, index) == streamq_head(q))
            streamq_pop(q);
        streamq_query(q, index, (char**)&packets[i], &pktlens[i]);
        pktsizs[i] = RTP_MAX_PKTSIZ;
        index = streamq_next(q, index);
    }
    packets[i] = NULL;
    pktsizs[i] = 0;

    // 移动慢速客户端到队列尾部
    TAILQ_FOREACH(cc, &s->connections_qhead, session_entry) {
        struct rtp_connection *rtp = cc->vrtp;
        if (cc->state != RTSP_CC_STATE_PLAYING || !rtp)
            continue;
        if (!streamq_inused(q, rtp->streamq_index) && rtp->streamq_index != streamq_tail(q)) {
            rtp->streamq_index = streamq_head(q);
            warn("client lost video packet [peer %s:%u]\n", inet_ntoa(cc->peer_addr), cc->peer_port);
        }
    }

    // RTP封装LZ4数据
    static uint32_t frame_id = 0;
    uint32_t original_size = 768 * 608;  // 默认值
    
    // 从session中获取原始大小
    #ifdef __RTSP_DEMO_LZ4_H__
    if (s->lz4_frame_info_valid && s->lz4_frame_info.original_size > 0) {
        original_size = s->lz4_frame_info.original_size;
    }
    #endif
    
    // RTP封装LZ4数据（使用队列缓冲区）
    int encoded_count = rtp_enc_lz4_to_queue(&s->vrtpe, 
                                             compressed_data, compressed_size,
                                             frame_id++, original_size, timestamp_us,
                                             packets, pktsizs, count);
    
    if (encoded_count <= 0) {
        return -1;
    }

    // 设置包长度并推送
    for (i = 0; i < encoded_count; i++) {
        if (!pktlens[i]) break;
        *pktlens[i] = pktsizs[i];
        streamq_push(q);
    }

    // 发送到所有客户端
    TAILQ_FOREACH(cc, &s->connections_qhead, session_entry) {
        struct rtp_connection *rtp = cc->vrtp;
        if (cc->state != RTSP_CC_STATE_PLAYING || !rtp)
            continue;
        // 实际发送在rtsp_tx_video_packet中处理
        // 这里只需要确保数据已入队
    }

    return compressed_size;
}

/**
 * @brief 发送原始视频帧
 */
int rtsp_tx_video_raw(rtsp_session_handle session,
                      const uint8_t *frame_data,
                      size_t frame_size,
                      uint64_t timestamp_us) {
    // 实现类似于rtsp_tx_video_lz4，但发送未压缩数据
    // TODO: 实现
    return -1;
}

/**
 * @brief 发送压缩的视频帧（通用接口）
 */
int rtsp_tx_video_compressed(rtsp_session_handle session,
                             const uint8_t *compressed_data,
                             size_t compressed_size,
                             int compression_type,
                             uint64_t timestamp_us) {
    if (compression_type == 0) {  // LZ4
        return rtsp_tx_video_lz4(session, compressed_data, compressed_size, timestamp_us);
    }
    // 其他压缩类型...
    return -1;
}

