/**
 * @file rtsp_evt2.c
 * @brief RTSP扩展实现：支持EVT2编码事件数据流
 */

#include "rtsp_demo_evt2.h"
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
#define RTP_PT_EVT2        99  // EVT2编码数据

// htonll辅助函数声明（如果系统没有提供）
static uint64_t htonll(uint64_t value);

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

// EVT2 RTP payload header（24字节，简化版，不含CRC32）
typedef struct {
    uint32_t frame_id;        ///< 帧序号
    uint32_t event_count;     ///< 事件数量
    uint64_t timestamp_us;    ///< 时间戳（微秒）
    uint32_t evt2_size;       ///< EVT2数据大小
    uint16_t frag_index;      ///< 分片索引
    uint16_t frag_count;      ///< 总分片数
    uint8_t  data[0];         ///< EVT2数据（变长）
} __attribute__((packed)) evt2_rtp_payload_t;

// 简化分片header（后续包，只包含分片信息）
typedef struct {
    uint16_t frag_index;      ///< 分片索引
    uint16_t frag_count;      ///< 总分片数
} __attribute__((packed)) evt2_rtp_frag_header_t;

/**
 * @brief 设置EVT2视频流
 */
int rtsp_set_video_evt2(rtsp_session_handle session, 
                        const rtsp_evt2_stream_info_t *stream_info) {
    struct rtsp_session *s = (struct rtsp_session*)session;
    if (!s || !stream_info) {
        return -1;
    }

    // 使用自定义codec_id
    s->vcodec_id = RTSP_CODEC_ID_VIDEO_EVT2;
    s->vrtpe.pt = RTP_PT_EVT2;
    s->vrtpe.seq = 0;
    s->vrtpe.ssrc = 0;
    s->vrtpe.sample_rate = 90000;  // 90kHz时钟

    // 存储流信息
    #ifdef ENABLE_RTSP_EVT2_SUPPORT
    if (stream_info) {
        memcpy(&s->evt2_stream_info, stream_info, sizeof(rtsp_evt2_stream_info_t));
        s->evt2_stream_info_valid = 1;
    } else {
        s->evt2_stream_info_valid = 0;
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
 * @brief RTP封装EVT2数据到队列缓冲区
 * @note 直接使用队列提供的缓冲区，不进行内存分配
 */
static int rtp_enc_evt2_to_queue(rtp_enc *enc,
                                  const uint8_t *evt2_data,
                                  size_t evt2_size,
                                  uint32_t frame_id,
                                  uint32_t event_count,
                                  uint64_t timestamp_us,
                                  uint8_t **packets,
                                  int *pktsizs,
                                  int max_packets) {
    if (!enc || !evt2_data || evt2_size == 0 || !packets || !pktsizs || max_packets <= 0) {
        return -1;
    }

    size_t max_payload = RTP_MAX_PKTSIZ - RTPHDR_SIZE;
    // payload_header_size不包含data[0]，只计算实际字段大小
    size_t payload_header_size = sizeof(evt2_rtp_payload_t) - sizeof(((evt2_rtp_payload_t *)0)->data);
    size_t frag_header_size = sizeof(evt2_rtp_frag_header_t);
    size_t total_payload_size = payload_header_size + evt2_size;
    uint32_t rtp_ts = (uint32_t)(timestamp_us * enc->sample_rate / 1000000);
    
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
        rtp_hdr->ts = htonl(rtp_ts);
        rtp_hdr->ssrc = htonl(enc->ssrc);

        // EVT2 payload header
        evt2_rtp_payload_t *payload = (evt2_rtp_payload_t *)(pkt + RTPHDR_SIZE);
        payload->frame_id = htonl(frame_id);
        payload->event_count = htonl(event_count);
        // timestamp_us需要转换为网络字节序（64位）
        uint64_t timestamp_us_net = htonll(timestamp_us);
        memcpy(&payload->timestamp_us, &timestamp_us_net, sizeof(uint64_t));
        payload->evt2_size = htonl(evt2_size);
        payload->frag_index = htons(0);
        payload->frag_count = htons(1);
        
        // EVT2数据
        memcpy(payload->data, evt2_data, evt2_size);

        pktsizs[0] = RTPHDR_SIZE + payload_header_size + evt2_size;
        return 1;  // 返回包数
    } else {
        // 需要分片
        int pkt_count = 0;
        const uint8_t *data_ptr = evt2_data;
        size_t remaining = evt2_size;
        
        // 计算分片数（考虑第一个包有完整header，后续包只有简化header）
        size_t first_pkt_data_size = max_payload - payload_header_size;  // 第一个包可用数据
        size_t frag_pkt_data_size = max_payload - frag_header_size;    // 后续包可用数据
        
        int frag_count;
        if (evt2_size <= first_pkt_data_size) {
            frag_count = 1;
        } else {
            // 第一个包 + 后续包
            size_t remaining_data = evt2_size - first_pkt_data_size;
            int frag_pkt_count = (remaining_data + frag_pkt_data_size - 1) / frag_pkt_data_size;
            frag_count = 1 + frag_pkt_count;
        }
        
        if (frag_count > max_packets) {
            err("EVT2 data too large: %zu bytes, need %d packets (max=%d, first_pkt_data=%zu, frag_pkt_data=%zu)\n", 
                evt2_size, frag_count, max_packets, first_pkt_data_size, frag_pkt_data_size);
            return -1;
        }
        
        // 第一个包：包含完整payload header + 部分数据
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
        
        // 第一个包的payload header
        evt2_rtp_payload_t *payload = (evt2_rtp_payload_t *)(pkt + RTPHDR_SIZE);
        payload->frame_id = htonl(frame_id);
        payload->event_count = htonl(event_count);
        uint64_t timestamp_us_net = htonll(timestamp_us);
        memcpy(&payload->timestamp_us, &timestamp_us_net, sizeof(uint64_t));
        payload->evt2_size = htonl(evt2_size);
        payload->frag_index = htons(0);
        payload->frag_count = htons(frag_count);
        
        // 计算第一个包实际能容纳的数据量（使用已计算的first_pkt_data_size，但不超过remaining）
        size_t actual_first_pkt_data_size = first_pkt_data_size;
        if (actual_first_pkt_data_size > remaining) {
            actual_first_pkt_data_size = remaining;
        }
        
        memcpy(payload->data, data_ptr, actual_first_pkt_data_size);
        pktsizs[pkt_count] = RTPHDR_SIZE + payload_header_size + actual_first_pkt_data_size;
        data_ptr += actual_first_pkt_data_size;
        remaining -= actual_first_pkt_data_size;
        pkt_count++;
         
        // 后续包：只包含简化header + 数据片段
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
            size_t fragment_size = max_payload - frag_header_size;
            if (fragment_size > remaining) {
                fragment_size = remaining;
                rtp_hdr->m = 1;  // 最后一包，设置 marker
            } else {
                rtp_hdr->m = 0;
            }
            
            // 简化header（只包含分片信息）
            evt2_rtp_frag_header_t *frag_header = (evt2_rtp_frag_header_t *)(pkt + RTPHDR_SIZE);
            frag_header->frag_index = htons(pkt_count);
            frag_header->frag_count = htons(frag_count);
            
            // 复制数据片段
            memcpy(pkt + RTPHDR_SIZE + frag_header_size, data_ptr, fragment_size);
            pktsizs[pkt_count] = RTPHDR_SIZE + frag_header_size + fragment_size;
            
            data_ptr += fragment_size;
            remaining -= fragment_size;
            pkt_count++;
        }
        
        if (remaining > 0) {
            err("EVT2 data too large, cannot fit in %d packets\n", max_packets);
            return -1;
        }
        
        return pkt_count;
    }
}

// htonll辅助函数（如果系统没有提供）
static uint64_t htonll(uint64_t value) {
    static const int num = 42;
    if (*(char *)&num == 42) {
        // 小端系统
        uint32_t high_part = htonl((uint32_t)(value >> 32));
        uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFF));
        return (((uint64_t)low_part) << 32) | high_part;
    } else {
        // 大端系统
        return value;
    }
}

/**
 * @brief 发送EVT2编码的视频帧
 * @note 参考rtsp_tx_video的实现，使用队列缓冲区
 */
int rtsp_tx_video_evt2(rtsp_session_handle session,
                       const uint8_t *evt2_data,
                       size_t evt2_size,
                       uint32_t frame_id,
                       uint32_t event_count,
                       uint64_t timestamp_us) {
    struct rtsp_session *s = (struct rtsp_session*)session;
    struct stream_queue *q = NULL;
    struct rtsp_client_connection *cc = NULL;
    uint8_t *packets[VRTP_MAX_NBPKTS+1] = {NULL};
    int  pktsizs[VRTP_MAX_NBPKTS+1] = {0};
    int *pktlens[VRTP_MAX_NBPKTS] = {NULL};
    int i, index, count;
    
    if (!s || !evt2_data || evt2_size == 0) {
        return -1;
    }

    if (s->vcodec_id != RTSP_CODEC_ID_VIDEO_EVT2) {
        err("video codec is not EVT2\n");
        return -1;
    }

    // 获取队列
    q = s->vstreamq;
    if (!q) {
        return -1;
    }

    // 估算需要的RTP包数量
    // payload_header_size不包含data[0]，只计算实际字段大小
    size_t payload_header_size = sizeof(evt2_rtp_payload_t) - sizeof(((evt2_rtp_payload_t *)0)->data);
    size_t frag_header_size = sizeof(evt2_rtp_frag_header_t);
    size_t max_payload = RTP_MAX_PKTSIZ - RTPHDR_SIZE;
    
    // 计算分片数（考虑第一个包有完整header，后续包只有简化header）
    size_t first_pkt_data_size = max_payload - payload_header_size;
    if (evt2_size <= first_pkt_data_size) {
        // 单包
        count = 1;
    } else {
        // 多包：第一个包 + 后续包
        size_t remaining_data = evt2_size - first_pkt_data_size;
        size_t frag_pkt_data_size = max_payload - frag_header_size;
        int frag_pkt_count = (remaining_data + frag_pkt_data_size - 1) / frag_pkt_data_size;
        count = 1 + frag_pkt_count;
    }
    
    if (count == 0) count = 1;
    if (count > VRTP_MAX_NBPKTS) {
        err("EVT2 data too large: %zu bytes, need %d packets (max=%d)\n", 
            evt2_size, count, VRTP_MAX_NBPKTS);
        return -1;
    }

    // 获取队列缓冲区（确保有足够的缓冲区）
    index = streamq_tail(q);
    for (i = 0; i < count && i < VRTP_MAX_NBPKTS; i++) {
        if (streamq_next(q, index) == streamq_head(q))
            streamq_pop(q);
        streamq_query(q, index, (char**)&packets[i], &pktlens[i]);
        pktsizs[i] = RTP_MAX_PKTSIZ;
        index = streamq_next(q, index);
    }
    packets[i] = NULL;
    pktsizs[i] = 0;
    
    if (i < count) {
        err("Not enough queue buffers: need %d packets, got %d (max=%d)\n", 
            count, i, VRTP_MAX_NBPKTS);
        return -1;
    }
    
    // RTP封装EVT2数据（使用队列缓冲区）
    int encoded_count = rtp_enc_evt2_to_queue(&s->vrtpe,
                                              evt2_data, evt2_size,
                                              frame_id, event_count, timestamp_us,
                                              packets, pktsizs, count);
    
    if (encoded_count <= 0) {
        err("rtp_enc_evt2_to_queue failed\n");
        return -1;
    }
    
    // 推送到队列
    for (i = 0; i < encoded_count; i++) {
        if (pktlens[i]) {
            *pktlens[i] = pktsizs[i];
            streamq_push(q);
        }
    }
    
    // 发送给所有客户端
    TAILQ_FOREACH(cc, &s->connections_qhead, session_entry) {
        struct rtp_connection *rtp = cc->vrtp;
        if (cc->state != RTSP_CC_STATE_PLAYING || !rtp)
            continue;
        rtsp_try_tx_rtcp_sr(cc, 0, timestamp_us);
        rtsp_tx_video_packet(cc);
    }
    
    return evt2_size;
}

