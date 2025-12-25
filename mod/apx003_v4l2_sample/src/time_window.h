/**
 * @file time_window.h
 * @brief 时间窗口管理器（基于硬件时间戳）
 */

#ifndef TIME_WINDOW_H
#define TIME_WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include "evs_event_extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 事件窗口缓冲区
// ============================================================================

typedef struct {
    // 时间窗口信息（基于硬件时间戳）
    uint64_t window_start_timestamp;   // 窗口起始时间戳（微秒）
    uint64_t window_end_timestamp;     // 窗口结束时间戳（微秒）
    uint32_t window_id;                // 窗口序号
    
    // 图像缓冲区（累积模式）
    uint8_t* frame_buffer;             // 768×608图像
    uint32_t width;
    uint32_t height;
    
    // 事件数据（可选，用于EVT2编码）
    EVSEvent_t* events;                // 事件数组
    uint32_t event_count;              // 事件数量
    uint32_t max_events;               // 数组容量
    
    // 统计信息
    uint32_t frames_in_window;         // 窗口内处理的V4L2帧数
    uint32_t subframes_in_window;      // 窗口内处理的子帧数
    
} EventWindowBuffer_t;

// ============================================================================
// 时间窗口累积器
// ============================================================================

typedef struct {
    // 配置参数
    uint64_t window_size_us;           // 时间窗口大小（微秒）
    
    // 时间窗口状态
    uint64_t window_start_timestamp;   // 当前窗口起始时间戳
    uint64_t window_end_timestamp;     // 当前窗口结束时间戳
    bool window_initialized;
    
    // 当前窗口缓冲区
    EventWindowBuffer_t* current_window;
    
    // 统计信息
    uint32_t total_windows_generated;  // 已生成的窗口数
    uint64_t total_events_processed;   // 总处理事件数
    uint64_t total_subframes_processed;// 总处理子帧数
    
} TimeWindowAccumulator_t;

/**
 * @brief 创建事件窗口缓冲区
 */
EventWindowBuffer_t* event_window_buffer_create(uint32_t max_events);

/**
 * @brief 销毁事件窗口缓冲区
 */
void event_window_buffer_destroy(EventWindowBuffer_t* buffer);

/**
 * @brief 重置窗口缓冲区（清空数据）
 */
void event_window_buffer_reset(EventWindowBuffer_t* buffer);

/**
 * @brief 创建时间窗口累积器
 * @param window_size_ms 时间窗口大小（毫秒）
 */
TimeWindowAccumulator_t* time_window_accumulator_create(uint32_t window_size_ms);

/**
 * @brief 销毁时间窗口累积器
 */
void time_window_accumulator_destroy(TimeWindowAccumulator_t* accum);

/**
 * @brief 设置时间窗口大小
 * @param window_size_ms 时间窗口大小（毫秒）
 */
int time_window_set_size(TimeWindowAccumulator_t* accum, uint32_t window_size_ms);

/**
 * @brief 获取时间窗口大小
 * @return 时间窗口大小（毫秒）
 */
uint32_t time_window_get_size(TimeWindowAccumulator_t* accum);

/**
 * @brief 检查子帧是否会触发窗口完成
 * @param accum 累积器
 * @param subframe_timestamp 子帧时间戳（微秒）
 * @return true=会触发窗口完成, false=不会触发
 */
bool time_window_will_complete(TimeWindowAccumulator_t* accum, 
                               uint64_t subframe_timestamp);

/**
 * @brief 完成当前窗口并返回窗口缓冲区（时间触发）
 * @return 完成的窗口缓冲区（调用者负责释放）
 */
EventWindowBuffer_t* time_window_complete(TimeWindowAccumulator_t* accum);

/**
 * @brief 强制完成当前窗口（空间不足时触发）
 * @return 完成的窗口缓冲区（调用者负责释放）
 */
EventWindowBuffer_t* time_window_force_complete(TimeWindowAccumulator_t* accum);

/**
 * @brief 累积事件到当前窗口
 */
int time_window_accumulate_event(TimeWindowAccumulator_t* accum,
                                 const EVSEvent_t* event);

/**
 * @brief 打印统计信息
 */
void time_window_print_stats(const TimeWindowAccumulator_t* accum);

#ifdef __cplusplus
}
#endif

#endif // TIME_WINDOW_H
