/**
 * @file time_window.c
 * @brief 时间窗口管理器实现
 */

#include "time_window.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EVS_OUTPUT_WIDTH  768
#define EVS_OUTPUT_HEIGHT 608

// ============================================================================
// 事件窗口缓冲区
// ============================================================================

EventWindowBuffer_t* event_window_buffer_create(uint32_t max_events)
{
    EventWindowBuffer_t* buffer = (EventWindowBuffer_t*)calloc(1, sizeof(EventWindowBuffer_t));
    if (!buffer) return NULL;
    
    buffer->width = EVS_OUTPUT_WIDTH;
    buffer->height = EVS_OUTPUT_HEIGHT;
    buffer->max_events = max_events;
    
    // 分配图像缓冲区
    buffer->frame_buffer = (uint8_t*)calloc(buffer->width * buffer->height, sizeof(uint8_t));
    if (!buffer->frame_buffer) {
        free(buffer);
        return NULL;
    }
    
    // 分配事件数组
    buffer->events = (EVSEvent_t*)calloc(max_events, sizeof(EVSEvent_t));
    if (!buffer->events) {
        free(buffer->frame_buffer);
        free(buffer);
        return NULL;
    }
    
    buffer->event_count = 0;
    buffer->frames_in_window = 0;
    buffer->subframes_in_window = 0;
    
    return buffer;
}

void event_window_buffer_destroy(EventWindowBuffer_t* buffer)
{
    if (!buffer) return;
    
    if (buffer->frame_buffer) {
        free(buffer->frame_buffer);
    }
    if (buffer->events) {
        free(buffer->events);
    }
    free(buffer);
}

void event_window_buffer_reset(EventWindowBuffer_t* buffer)
{
    if (!buffer) return;
    
    memset(buffer->frame_buffer, 0, buffer->width * buffer->height);
    buffer->event_count = 0;
    buffer->frames_in_window = 0;
    buffer->subframes_in_window = 0;
}

// ============================================================================
// 时间窗口累积器
// ============================================================================

TimeWindowAccumulator_t* time_window_accumulator_create(uint32_t window_size_ms)
{
    TimeWindowAccumulator_t* accum = (TimeWindowAccumulator_t*)calloc(1, sizeof(TimeWindowAccumulator_t));
    if (!accum) return NULL;
    
    accum->window_size_us = window_size_ms * 1000;  // 转换为微秒
    accum->window_initialized = false;
    accum->total_windows_generated = 0;
    accum->total_events_processed = 0;
    accum->total_subframes_processed = 0;
    
    // 创建第一个窗口缓冲区
    accum->current_window = event_window_buffer_create(5000000);  // 预分配500万事件
    if (!accum->current_window) {
        free(accum);
        return NULL;
    }
    
    printf("[TimeWindow] Created: window_size=%u ms (%lu us)\n",
           window_size_ms, accum->window_size_us);
    
    return accum;
}

void time_window_accumulator_destroy(TimeWindowAccumulator_t* accum)
{
    if (!accum) return;
    
    if (accum->current_window) {
        event_window_buffer_destroy(accum->current_window);
    }
    
    free(accum);
}

int time_window_set_size(TimeWindowAccumulator_t* accum, uint32_t window_size_ms)
{
    if (!accum) return -1;
    
    uint64_t new_size_us = window_size_ms * 1000;
    
    printf("[TimeWindow] Changing window size: %lu us -> %lu us\n",
           accum->window_size_us, new_size_us);
    
    accum->window_size_us = new_size_us;
    
    // 更新当前窗口的结束时间
    if (accum->window_initialized) {
        accum->window_end_timestamp = accum->window_start_timestamp + new_size_us;
    }
    
    return 0;
}

uint32_t time_window_get_size(TimeWindowAccumulator_t* accum)
{
    if (!accum) return 0;
    return (uint32_t)(accum->window_size_us / 1000);
}

bool time_window_will_complete(TimeWindowAccumulator_t* accum, 
                               uint64_t subframe_timestamp)
{
    if (!accum) return false;
    
    // 如果窗口未初始化，不会触发完成
    if (!accum->window_initialized) {
        return false;
    }
    
    // 判断子帧时间戳是否超出窗口
    return (subframe_timestamp >= accum->window_end_timestamp);
}

EventWindowBuffer_t* time_window_complete(TimeWindowAccumulator_t* accum)
{
    if (!accum || !accum->current_window) return NULL;
    
    EventWindowBuffer_t* completed_window = accum->current_window;
    
    printf("[TimeWindow #%u] TIME-Completed: [%lu, %lu] us, %u events, %u subframes\n",
           completed_window->window_id,
           completed_window->window_start_timestamp,
           completed_window->window_end_timestamp,
           completed_window->event_count,
           completed_window->subframes_in_window);
    
    // 更新统计
    accum->total_windows_generated++;
    
    // 创建新窗口（与初始窗口大小保持一致）
    accum->current_window = event_window_buffer_create(5000000);
    if (!accum->current_window) {
        fprintf(stderr, "[TimeWindow] Error: Failed to create new window buffer\n");
        return completed_window;
    }
    
    // 更新窗口时间
    accum->window_start_timestamp = accum->window_end_timestamp;
    accum->window_end_timestamp += accum->window_size_us;
    
    // 设置新窗口的ID和时间戳
    accum->current_window->window_id = accum->total_windows_generated;
    accum->current_window->window_start_timestamp = accum->window_start_timestamp;
    accum->current_window->window_end_timestamp = accum->window_end_timestamp;
    
    printf("[TimeWindow #%u] Started: [%lu, %lu] us\n",
           accum->current_window->window_id,
           accum->window_start_timestamp,
           accum->window_end_timestamp);
    
    return completed_window;
}

EventWindowBuffer_t* time_window_force_complete(TimeWindowAccumulator_t* accum)
{
    if (!accum || !accum->current_window) return NULL;
    
    EventWindowBuffer_t* completed_window = accum->current_window;
    
    // 使用当前时间作为结束时间戳（窗口可能未到时间）
    if (completed_window->event_count > 0) {
        // 使用最后一个事件的时间戳作为结束时间
        uint64_t last_event_ts = completed_window->events[completed_window->event_count - 1].timestamp;
        completed_window->window_end_timestamp = last_event_ts;
    }
    
    printf("[TimeWindow #%u] FORCE-Completed: [%lu, %lu] us, %u events (%.1f%% full), %u subframes\n",
           completed_window->window_id,
           completed_window->window_start_timestamp,
           completed_window->window_end_timestamp,
           completed_window->event_count,
           100.0 * completed_window->event_count / completed_window->max_events,
           completed_window->subframes_in_window);
    
    // 更新统计
    accum->total_windows_generated++;
    
    // 创建新窗口
    accum->current_window = event_window_buffer_create(5000000);
    if (!accum->current_window) {
        fprintf(stderr, "[TimeWindow] Error: Failed to create new window buffer\n");
        return completed_window;
    }
    
    // 设置新窗口的时间戳（确保时间连续性）
    accum->window_start_timestamp = completed_window->window_end_timestamp;
    accum->window_end_timestamp = accum->window_start_timestamp + accum->window_size_us;
    accum->window_initialized = true;
    
    // 设置新窗口的时间戳和ID
    accum->current_window->window_id = accum->total_windows_generated;
    accum->current_window->window_start_timestamp = accum->window_start_timestamp;
    accum->current_window->window_end_timestamp = accum->window_end_timestamp;
    
    printf("[TimeWindow #%u] Started (continuous): [%lu, %lu] us\n",
           accum->current_window->window_id,
           accum->window_start_timestamp,
           accum->window_end_timestamp);
    
    return completed_window;
}

int time_window_accumulate_event(TimeWindowAccumulator_t* accum,
                                 const EVSEvent_t* event)
{
    if (!accum || !event || !accum->current_window) return -1;
    
    EventWindowBuffer_t* window = accum->current_window;
    
    // 初始化窗口（使用第一个事件的时间戳）
    if (!accum->window_initialized) {
        accum->window_start_timestamp = event->timestamp;
        accum->window_end_timestamp = event->timestamp + accum->window_size_us;
        accum->window_initialized = true;
        
        window->window_start_timestamp = accum->window_start_timestamp;
        window->window_end_timestamp = accum->window_end_timestamp;
        window->window_id = 0;
        
        printf("[TimeWindow #0] Started: [%lu, %lu] us\n",
               accum->window_start_timestamp,
               accum->window_end_timestamp);
    }
    
    // 累积到图像缓冲区
    if (event->x < window->width && event->y < window->height) {
        int idx = event->y * window->width + event->x;
        
        // 极性模式：正事件=白色，负事件=灰色
        if (event->polarity > 0) {
            window->frame_buffer[idx] = 255;
        } else {
            window->frame_buffer[idx] = 128;
        }
    }
    
    // 累积到事件数组
    if (window->event_count < window->max_events) {
        window->events[window->event_count++] = *event;
    }
    
    accum->total_events_processed++;
    
    return 0;
}

void time_window_print_stats(const TimeWindowAccumulator_t* accum)
{
    if (!accum) return;
    
    printf("\n========== Time Window Statistics ==========\n");
    printf("Window Size: %u ms (%lu us)\n",
           (uint32_t)(accum->window_size_us / 1000),
           accum->window_size_us);
    printf("Total Windows Generated: %u\n", accum->total_windows_generated);
    printf("Total Events Processed: %lu\n", accum->total_events_processed);
    printf("Total Subframes Processed: %lu\n", accum->total_subframes_processed);
    
    if (accum->current_window) {
        printf("Current Window #%u:\n", accum->current_window->window_id);
        printf("  Events: %u\n", accum->current_window->event_count);
        printf("  Subframes: %u\n", accum->current_window->subframes_in_window);
    }
    
    printf("============================================\n\n");
}
