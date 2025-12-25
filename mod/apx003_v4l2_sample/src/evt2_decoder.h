/**
 * @file evt2_decoder.h
 * @brief EVT2事件解码器 - 将EVT2格式解压为EVS事件
 */

#ifndef EVT2_DECODER_H
#define EVT2_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include "evs_event_extractor.h"
#include "evt2_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief EVT2解码器
 */
typedef struct {
    uint64_t current_time_high;         // 当前时间高位值
    
    // 统计
    uint64_t total_events_decoded;      // 解码的事件总数
    uint64_t total_time_events;         // 处理的时间高位事件数
    uint64_t total_bytes_input;         // 输入的字节总数
} EVT2Decoder_t;

/**
 * @brief 创建EVT2解码器
 */
EVT2Decoder_t* evt2_decoder_create(void);

/**
 * @brief 销毁EVT2解码器
 */
void evt2_decoder_destroy(EVT2Decoder_t* decoder);

/**
 * @brief 解码EVT2数据为事件数组
 * @param decoder 解码器
 * @param encoded_data EVT2编码数据
 * @param encoded_size 编码数据大小（字节）
 * @param events 输出事件数组（由调用者分配）
 * @param max_events 最大事件数量
 * @param event_count 输出：解码的事件数量
 * @return 0成功，-1失败
 */
int evt2_decoder_decode(
    EVT2Decoder_t* decoder,
    const uint8_t* encoded_data,
    size_t encoded_size,
    EVSEvent_t* events,
    uint32_t max_events,
    uint32_t* event_count);

/**
 * @brief 获取解码统计信息
 */
void evt2_decoder_get_stats(
    const EVT2Decoder_t* decoder,
    uint64_t* total_events_decoded,
    uint64_t* total_time_events,
    uint64_t* total_bytes_input);

/**
 * @brief 重置解码统计
 */
void evt2_decoder_reset_stats(EVT2Decoder_t* decoder);

/**
 * @brief 打印解码统计
 */
void evt2_decoder_print_stats(const EVT2Decoder_t* decoder);

#ifdef __cplusplus
}
#endif

#endif // EVT2_DECODER_H
