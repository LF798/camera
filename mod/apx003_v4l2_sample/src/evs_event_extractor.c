/**
 * @file evs_event_extractor.c
 * @brief EVS原始数据事件提取器实现
 */

#include "evs_event_extractor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 处理单个子帧数据
 * @param pixelBufferPtr 像素缓冲区指针
 * @param subframe 子帧编号 (0-3)
 * @param events 事件数组
 * @param event_count 当前事件计数指针
 * @param max_events 最大事件数
 * @return 本子帧提取的事件数
 */
static uint32_t process_subframe(
    const uint64_t* pixelBufferPtr,
    uint32_t subframe,
    EVSEvent_t* events,
    uint32_t* event_count,
    uint32_t max_events)
{
    uint32_t initial_count = *event_count;
    
    // 解析时间戳
    uint64_t buffer = pixelBufferPtr[0];
    uint64_t timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
    uint64_t header_vec = buffer & 0xFFFFFF;
    
    if (header_vec != 0xFFFF) {
        fprintf(stderr, "[EVS Extractor] Warning: Invalid header vector 0x%lx\n", header_vec);
    }
    
    // 验证子帧编号
    buffer = pixelBufferPtr[1];
    (void)(buffer >> 44);  // 子帧ID已通过参数传入，此处仅跳过
    timestamp /= 200;  // 转换为微秒
    
    // 计算子帧偏移量
    int x_offset = 0, y_offset = 0;
    switch (subframe) {
    case 0:
        x_offset = 0;
        y_offset = 0;
        break;
    case 1:
        x_offset = 1;
        y_offset = 0;
        break;
    case 2:
        x_offset = 0;
        y_offset = 1;
        break;
    case 3:
        x_offset = 1;
        y_offset = 1;
        break;
    }
    
    // 跳过头部，指向像素数据
    pixelBufferPtr += 2;
    
    int y = y_offset;
    
    // 遍历子帧像素
    for (int i = 0; i < EVS_SUB_HEIGHT; i++) {
        int x = x_offset;
        
        for (int j = 0; j < EVS_SUB_WIDTH / 32; j++) {
            buffer = pixelBufferPtr[j];
            
            // 每个64位包含32个2位像素
            for (int k = 0; k < 64; k += 2) {
                uint64_t pix = (buffer >> k) & 0x3;
                
                // 检查边界
                if (x >= EVS_OUTPUT_WIDTH || y >= EVS_OUTPUT_HEIGHT) {
                    x += 2;
                    continue;
                }
                
                // 如果有事件（pix > 0）
                if (pix > 0) {
                    if (*event_count >= max_events) {
                        fprintf(stderr, "[EVS Extractor] Warning: Max events reached\n");
                        return *event_count - initial_count;
                    }
                    
                    events[*event_count].x = (uint16_t)x;
                    events[*event_count].y = (uint16_t)y;
                    events[*event_count].polarity = (int8_t)(pix >> 1);
                    events[*event_count].reserved = 0;
                    events[*event_count].timestamp = timestamp;
                    
                    (*event_count)++;
                }
                
                x += 2;  // 2像素间隔
            }
        }
        
        pixelBufferPtr += EVS_SUB_WIDTH / 32;
        y += 2;  // 2像素间隔
    }
    
    return *event_count - initial_count;
}

// ============================================================================
// 公共函数实现
// ============================================================================

int evs_extract_events(
    const uint8_t* raw_data,
    size_t raw_data_size,
    EVSEventPacket_t* packet,
    uint32_t max_events)
{
    if (!raw_data || !packet || !packet->events) {
        fprintf(stderr, "[EVS Extractor] Error: Invalid parameters\n");
        return -1;
    }
    
    if (raw_data_size != EVS_RAW_DATA_SIZE) {
        fprintf(stderr, "[EVS Extractor] Error: Invalid data size %zu, expected %d\n",
                raw_data_size, EVS_RAW_DATA_SIZE);
        return -1;
    }
    
    // 重置事件计数
    packet->event_count = 0;
    
    const uint64_t* pixelBufferPtr = (const uint64_t*)raw_data;
    
    // 处理32个子帧
    for (int sub = 0; sub < 32; sub++) {
        process_subframe(
            pixelBufferPtr,
            sub,
            packet->events,
            &packet->event_count,
            max_events
        );
        
        // 移动到下一个子帧 (+32KB)
        pixelBufferPtr += HV_SUB_FULL_BYTE_SIZE / 8;
    }
    
    // 记录帧时间戳（使用第一个事件的时间戳）
    if (packet->event_count > 0) {
        packet->frame_timestamp = packet->events[0].timestamp;
    }
    
    return (int)packet->event_count;
}

int evs_extract_subframe_direct(
    const uint8_t* subframe_data,
    int subframe_id,
    EVSEvent_t* events,
    uint32_t* current_count,
    uint32_t max_events,
    uint32_t* dropped_count)
{
    if (!subframe_data || !events || !current_count) {
        fprintf(stderr, "[EVS Extractor] Error: Invalid parameters\n");
        return -1;
    }
    
    const uint64_t* pixelBufferPtr = (const uint64_t*)subframe_data;
    uint32_t event_idx = *current_count;
    uint32_t extracted = 0;
    uint32_t dropped = 0;
    
    // 初始化输出参数
    if (dropped_count) {
        *dropped_count = 0;
    }
    
    // 解析时间戳（前2个64位字）
    uint64_t buffer = pixelBufferPtr[0];
    uint64_t timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
    uint64_t header_vec = buffer & 0xFFFFFF;
    
    if (header_vec != 0xFFFF) {
        fprintf(stderr, "[EVS Extractor] Warning: Invalid header vector 0x%lx\n", header_vec);
    }
    
    // 验证子帧编号
    buffer = pixelBufferPtr[1];
    (void)(buffer >> 44);  // 子帧ID已通过参数传入
    timestamp /= 200;  // 转换为微秒
    
    // 跳过头部
    pixelBufferPtr += 2;
    
    // 计算子帧偏移量（基于子帧ID）
    int x_offset = 0, y_offset = 0;
    switch (subframe_id) {
    case 0:
        x_offset = 0;
        y_offset = 0;
        break;
    case 1:
        x_offset = 1;
        y_offset = 0;
        break;
    case 2:
        x_offset = 0;
        y_offset = 1;
        break;
    case 3:
        x_offset = 1;
        y_offset = 1;
        break;
    default:
        fprintf(stderr, "[EVS Extractor] Error: Invalid subframe_id %d\n", subframe_id);
        return -1;
    }
    
    int y = y_offset;
    
    // 处理像素数据
    for (int i = 0; i < EVS_SUB_HEIGHT; i++) {
        int x = x_offset;
        for (int j = 0; j < EVS_SUB_WIDTH / 32; j++) {
            buffer = pixelBufferPtr[j];
            for (int k = 0; k < 64; k += 2) {
                uint64_t pix = (buffer >> k) & 0x3;
                
                if (x >= EVS_OUTPUT_WIDTH || y >= EVS_OUTPUT_HEIGHT) {
                    x += 2;
                    continue;
                }
                
                if (pix > 0) {
                    // 检查缓冲区是否还有空间
                    if (event_idx >= max_events) {
                        // 缓冲区满，只统计不写入
                        dropped++;
                    } else {
                        // 直接写入目标缓冲区（零拷贝）
                        events[event_idx].x = x;
                        events[event_idx].y = y;
                        events[event_idx].polarity = (pix >> 1);
                        events[event_idx].timestamp = timestamp;
                        
                        event_idx++;
                        extracted++;
                    }
                }
                x += 2;
            }
        }
        pixelBufferPtr += EVS_SUB_WIDTH / 32;
        y += 2;
    }
    
    *current_count = event_idx;
    
    // 输出丢失数量
    if (dropped_count) {
        *dropped_count = dropped;
    }
    
    // 如果有事件丢失，打印警告
    if (dropped > 0) {
        fprintf(stderr, "[EVS Extractor] Warning: Buffer full, dropped %u events (extracted %u)\n",
               dropped, extracted);
    }
    
    return extracted;
}

EVSEventPacket_t* evs_event_packet_create(uint32_t max_events)
{
    EVSEventPacket_t* packet = (EVSEventPacket_t*)malloc(sizeof(EVSEventPacket_t));
    if (!packet) {
        fprintf(stderr, "[EVS Extractor] Error: Failed to allocate packet\n");
        return NULL;
    }
    
    packet->events = (EVSEvent_t*)malloc(max_events * sizeof(EVSEvent_t));
    if (!packet->events) {
        fprintf(stderr, "[EVS Extractor] Error: Failed to allocate events array\n");
        free(packet);
        return NULL;
    }
    
    packet->event_count = 0;
    packet->frame_timestamp = 0;
    
    return packet;
}

void evs_event_packet_destroy(EVSEventPacket_t* packet)
{
    if (packet) {
        if (packet->events) {
            free(packet->events);
        }
        free(packet);
    }
}

void evs_event_packet_reset(EVSEventPacket_t* packet)
{
    if (packet) {
        packet->event_count = 0;
        packet->frame_timestamp = 0;
    }
}

void evs_extractor_stats_init(EVSExtractorStats_t* stats)
{
    if (stats) {
        memset(stats, 0, sizeof(EVSExtractorStats_t));
        stats->min_events_per_frame = UINT32_MAX;
    }
}

void evs_extractor_stats_update(EVSExtractorStats_t* stats, uint32_t event_count)
{
    if (!stats) return;
    
    stats->total_frames_processed++;
    stats->total_events_extracted += event_count;
    stats->last_event_count = event_count;
    
    if (event_count > stats->max_events_per_frame) {
        stats->max_events_per_frame = event_count;
    }
    
    if (event_count < stats->min_events_per_frame) {
        stats->min_events_per_frame = event_count;
    }
}

void evs_extractor_stats_print(const EVSExtractorStats_t* stats)
{
    if (!stats) return;
    
    printf("\n========== EVS Extractor Statistics ==========\n");
    printf("Total Frames Processed: %lu\n", stats->total_frames_processed);
    printf("Total Events Extracted: %lu\n", stats->total_events_extracted);
    printf("Last Event Count: %u\n", stats->last_event_count);
    printf("Max Events/Frame: %u\n", stats->max_events_per_frame);
    printf("Min Events/Frame: %u\n", stats->min_events_per_frame);
    
    if (stats->total_frames_processed > 0) {
        double avg = (double)stats->total_events_extracted / stats->total_frames_processed;
        printf("Avg Events/Frame: %.2f\n", avg);
    }
    
    printf("==============================================\n\n");
}
