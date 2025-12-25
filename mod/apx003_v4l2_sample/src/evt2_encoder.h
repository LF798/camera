/**
 * @file evt2_encoder.h
 * @brief EVT2事件编码器 - 将EVS事件压缩为EVT2格式
 * @description 基于Prophesee EVT2标准的事件压缩编码器
 */

#ifndef EVT2_ENCODER_H
#define EVT2_ENCODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "evs_event_extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// EVT2格式定义
// ============================================================================

/**
 * @brief EVT2事件类型
 */
typedef enum {
    EVT2_TYPE_CD_OFF        = 0x00,    // CD事件：极性为负
    EVT2_TYPE_CD_ON         = 0x01,    // CD事件：极性为正
    EVT2_TYPE_TIME_HIGH     = 0x08,    // 时间高位事件
    EVT2_TYPE_EXT_TRIGGER   = 0x0A,    // 外部触发事件
} EVT2EventType_t;

/**
 * @brief EVT2原始事件（4字节）
 */
typedef union {
    uint32_t raw;
    struct {
        uint32_t pad : 28;
        uint32_t type : 4;
    } __attribute__((packed));
} EVT2RawEvent_t;

/**
 * @brief EVT2时间高位事件
 */
typedef struct {
    uint32_t timestamp : 28;    // 时间戳高28位（右移6位后的值）
    uint32_t type : 4;          // 必须为EVT2_TYPE_TIME_HIGH (0x08)
} __attribute__((packed)) EVT2RawEventTime_t;

/**
 * @brief EVT2 CD事件
 */
typedef struct {
    uint32_t y : 11;            // Y坐标 (0-2047)
    uint32_t x : 11;            // X坐标 (0-2047)
    uint32_t timestamp : 6;     // 时间戳低6位
    uint32_t type : 4;          // EVT2_TYPE_CD_ON 或 EVT2_TYPE_CD_OFF
} __attribute__((packed)) EVT2RawEventCD_t;

// ============================================================================
// 时间编码器
// ============================================================================

/**
 * @brief EVT2时间编码器
 */
typedef struct {
    uint64_t th;                // 当前时间高位值
    uint64_t th_next_step;      // 时间步进值（微秒）
} EVT2TimeEncoder_t;

/**
 * @brief 创建时间编码器
 * @param base_timestamp 基准时间戳（微秒）
 * @return 时间编码器指针，失败返回NULL
 */
EVT2TimeEncoder_t* evt2_time_encoder_create(uint64_t base_timestamp);

/**
 * @brief 销毁时间编码器
 */
void evt2_time_encoder_destroy(EVT2TimeEncoder_t* encoder);

/**
 * @brief 重置时间编码器
 * @param encoder 时间编码器
 * @param base_timestamp 新的基准时间戳（微秒）
 */
void evt2_time_encoder_reset(EVT2TimeEncoder_t* encoder, uint64_t base_timestamp);

/**
 * @brief 获取下一个时间高位值
 * @return 下一个时间高位时间戳
 */
uint64_t evt2_time_encoder_get_next_th(const EVT2TimeEncoder_t* encoder);

/**
 * @brief 编码时间高位事件
 * @param encoder 时间编码器
 * @param raw_event 输出的原始事件
 */
void evt2_time_encoder_encode(EVT2TimeEncoder_t* encoder, EVT2RawEvent_t* raw_event);

// ============================================================================
// EVT2编码缓冲区
// ============================================================================

/**
 * @brief EVT2编码缓冲区
 */
typedef struct {
    uint8_t* data;              // 编码数据
    size_t size;                // 当前数据大小（字节）
    size_t capacity;            // 缓冲区容量（字节）
} EVT2Buffer_t;

/**
 * @brief 创建EVT2缓冲区
 * @param initial_capacity 初始容量（字节）
 * @return 缓冲区指针，失败返回NULL
 */
EVT2Buffer_t* evt2_buffer_create(size_t initial_capacity);

/**
 * @brief 销毁EVT2缓冲区
 */
void evt2_buffer_destroy(EVT2Buffer_t* buffer);

/**
 * @brief 清空缓冲区（保留容量）
 */
void evt2_buffer_clear(EVT2Buffer_t* buffer);

/**
 * @brief 确保缓冲区有足够容量
 * @param buffer 缓冲区
 * @param required_size 需要的大小
 * @return 0成功，-1失败
 */
int evt2_buffer_ensure_capacity(EVT2Buffer_t* buffer, size_t required_size);

// ============================================================================
// 事件编码器
// ============================================================================

/**
 * @brief EVT2事件编码器
 */
typedef struct {
    EVT2TimeEncoder_t* time_encoder;    // 时间编码器
    EVT2Buffer_t* buffer;               // 编码缓冲区
    
    // 统计
    uint64_t total_events_encoded;      // 编码的事件总数
    uint64_t total_time_events;         // 插入的时间高位事件数
    uint64_t total_bytes_output;        // 输出的字节总数
} EVT2Encoder_t;

/**
 * @brief 创建EVT2编码器
 * @param initial_buffer_size 初始缓冲区大小（字节）
 * @return 编码器指针，失败返回NULL
 */
EVT2Encoder_t* evt2_encoder_create(size_t initial_buffer_size);

/**
 * @brief 销毁EVT2编码器
 */
void evt2_encoder_destroy(EVT2Encoder_t* encoder);

/**
 * @brief 编码事件数组为EVT2格式
 * @param encoder 编码器
 * @param events 事件数组（必须按时间戳排序）
 * @param event_count 事件数量
 * @param base_timestamp 窗口起始时间戳（微秒）
 * @param output_data 输出：编码数据指针（不需要释放，由encoder管理）
 * @param output_size 输出：编码数据大小（字节）
 * @return 0成功，-1失败
 */
int evt2_encoder_encode(
    EVT2Encoder_t* encoder,
    const EVSEvent_t* events,
    uint32_t event_count,
    uint64_t base_timestamp,
    const uint8_t** output_data,
    size_t* output_size);

/**
 * @brief 获取编码统计信息
 */
void evt2_encoder_get_stats(
    const EVT2Encoder_t* encoder,
    uint64_t* total_events_encoded,
    uint64_t* total_time_events,
    uint64_t* total_bytes_output);

/**
 * @brief 重置编码统计
 */
void evt2_encoder_reset_stats(EVT2Encoder_t* encoder);

/**
 * @brief 打印编码统计
 */
void evt2_encoder_print_stats(const EVT2Encoder_t* encoder);

#ifdef __cplusplus
}
#endif

#endif // EVT2_ENCODER_H
