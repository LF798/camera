/**
 * @file encoded_packet.h
 * @brief 编码数据包结构 - 用于传输EVT2编码数据
 */

#ifndef ENCODED_PACKET_H
#define ENCODED_PACKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 编码窗口数据包
 */
typedef struct {
    uint32_t window_id;                 // 窗口ID
    uint64_t window_start_timestamp;    // 窗口起始时间戳（微秒）
    uint64_t window_end_timestamp;      // 窗口结束时间戳（微秒）
    uint32_t original_event_count;      // 原始事件数量
    
    size_t encoded_data_size;           // 编码数据大小（字节）
    uint8_t* encoded_data;              // EVT2编码数据
    
    // 元数据
    uint32_t subframes_in_window;       // 窗口中的子帧数
    uint32_t frames_in_window;          // 窗口中的帧数
} EncodedWindowPacket_t;

/**
 * @brief 创建编码数据包
 */
EncodedWindowPacket_t* encoded_packet_create(
    uint32_t window_id,
    uint64_t window_start_timestamp,
    uint64_t window_end_timestamp,
    uint32_t original_event_count,
    const uint8_t* encoded_data,
    size_t encoded_data_size,
    uint32_t subframes_in_window,
    uint32_t frames_in_window);

/**
 * @brief 销毁编码数据包
 */
void encoded_packet_destroy(EncodedWindowPacket_t* packet);

#ifdef __cplusplus
}
#endif

#endif // ENCODED_PACKET_H
