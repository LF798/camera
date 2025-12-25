/**
 * @file evt2_encoder.c
 * @brief EVT2事件编码器实现
 */

#include "evt2_encoder.h"
#include "evs_event_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// 常量定义
// ============================================================================

#define N_LOWER_BITS_TH         6           // 时间戳低位比特数
#define REDUNDANCY_FACTOR       4           // 冗余因子
#define TH_STEP                 (1ul << N_LOWER_BITS_TH)        // 64us
#define TH_NEXT_STEP            (TH_STEP / REDUNDANCY_FACTOR)   // 16us

// ============================================================================
// 时间编码器实现
// ============================================================================

EVT2TimeEncoder_t* evt2_time_encoder_create(uint64_t base_timestamp)
{
    EVT2TimeEncoder_t* encoder = (EVT2TimeEncoder_t*)malloc(sizeof(EVT2TimeEncoder_t));
    if (!encoder) {
        fprintf(stderr, "[EVT2] Failed to allocate time encoder\n");
        return NULL;
    }
    
    encoder->th_next_step = TH_NEXT_STEP;
    encoder->th = (base_timestamp / TH_NEXT_STEP) * TH_NEXT_STEP;
    
    return encoder;
}

void evt2_time_encoder_destroy(EVT2TimeEncoder_t* encoder)
{
    if (encoder) {
        free(encoder);
    }
}

void evt2_time_encoder_reset(EVT2TimeEncoder_t* encoder, uint64_t base_timestamp)
{
    if (!encoder) return;
    
    encoder->th = (base_timestamp / TH_NEXT_STEP) * TH_NEXT_STEP;
}

uint64_t evt2_time_encoder_get_next_th(const EVT2TimeEncoder_t* encoder)
{
    if (!encoder) return 0;
    return encoder->th;
}

void evt2_time_encoder_encode(EVT2TimeEncoder_t* encoder, EVT2RawEvent_t* raw_event)
{
    if (!encoder || !raw_event) return;
    
    EVT2RawEventTime_t* ev_th = (EVT2RawEventTime_t*)raw_event;
    ev_th->timestamp = encoder->th >> N_LOWER_BITS_TH;
    ev_th->type = EVT2_TYPE_TIME_HIGH;
    encoder->th += TH_NEXT_STEP;
}

// ============================================================================
// 缓冲区实现
// ============================================================================

EVT2Buffer_t* evt2_buffer_create(size_t initial_capacity)
{
    EVT2Buffer_t* buffer = (EVT2Buffer_t*)malloc(sizeof(EVT2Buffer_t));
    if (!buffer) {
        fprintf(stderr, "[EVT2] Failed to allocate buffer\n");
        return NULL;
    }
    
    buffer->data = (uint8_t*)malloc(initial_capacity);
    if (!buffer->data) {
        fprintf(stderr, "[EVT2] Failed to allocate buffer data\n");
        free(buffer);
        return NULL;
    }
    
    buffer->size = 0;
    buffer->capacity = initial_capacity;
    
    return buffer;
}

void evt2_buffer_destroy(EVT2Buffer_t* buffer)
{
    if (buffer) {
        if (buffer->data) {
            free(buffer->data);
        }
        free(buffer);
    }
}

void evt2_buffer_clear(EVT2Buffer_t* buffer)
{
    if (buffer) {
        buffer->size = 0;
    }
}

int evt2_buffer_ensure_capacity(EVT2Buffer_t* buffer, size_t required_size)
{
    if (!buffer) return -1;
    
    if (buffer->capacity >= required_size) {
        return 0;
    }
    
    size_t new_capacity = buffer->capacity * 2;
    while (new_capacity < required_size) {
        new_capacity *= 2;
    }
    
    uint8_t* new_data = (uint8_t*)realloc(buffer->data, new_capacity);
    if (!new_data) {
        fprintf(stderr, "[EVT2] Failed to expand buffer: %zu -> %zu bytes\n",
               buffer->capacity, new_capacity);
        return -1;
    }
    
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    
    return 0;
}

// ============================================================================
// 事件编码器实现
// ============================================================================

EVT2Encoder_t* evt2_encoder_create(size_t initial_buffer_size)
{
    EVT2Encoder_t* encoder = (EVT2Encoder_t*)malloc(sizeof(EVT2Encoder_t));
    if (!encoder) {
        fprintf(stderr, "[EVT2] Failed to allocate encoder\n");
        return NULL;
    }
    
    encoder->time_encoder = evt2_time_encoder_create(0);
    if (!encoder->time_encoder) {
        free(encoder);
        return NULL;
    }
    
    encoder->buffer = evt2_buffer_create(initial_buffer_size);
    if (!encoder->buffer) {
        evt2_time_encoder_destroy(encoder->time_encoder);
        free(encoder);
        return NULL;
    }
    
    encoder->total_events_encoded = 0;
    encoder->total_time_events = 0;
    encoder->total_bytes_output = 0;
    
    return encoder;
}

void evt2_encoder_destroy(EVT2Encoder_t* encoder)
{
    if (encoder) {
        evt2_time_encoder_destroy(encoder->time_encoder);
        evt2_buffer_destroy(encoder->buffer);
        free(encoder);
    }
}

int evt2_encoder_encode(
    EVT2Encoder_t* encoder,
    const EVSEvent_t* events,
    uint32_t event_count,
    uint64_t base_timestamp,
    const uint8_t** output_data,
    size_t* output_size)
{
    if (!encoder || !events || event_count == 0 || !output_data || !output_size) {
        return -1;
    }
    
    evt2_buffer_clear(encoder->buffer);
    
    size_t estimated_size = (event_count + event_count / 1000 + 10) * sizeof(EVT2RawEvent_t);
    if (evt2_buffer_ensure_capacity(encoder->buffer, estimated_size) < 0) {
        return -1;
    }
    
    evt2_time_encoder_reset(encoder->time_encoder, base_timestamp);
    
    EVT2RawEvent_t* raw_events = (EVT2RawEvent_t*)encoder->buffer->data;
    size_t raw_event_count = 0;
    
    evt2_time_encoder_encode(encoder->time_encoder, &raw_events[raw_event_count++]);
    encoder->total_time_events++;
    
    for (uint32_t i = 0; i < event_count; i++) {
        const EVSEvent_t* event = &events[i];
        
        while (event->timestamp >= evt2_time_encoder_get_next_th(encoder->time_encoder)) {
            evt2_time_encoder_encode(encoder->time_encoder, &raw_events[raw_event_count++]);
            encoder->total_time_events++;
        }
        
        EVT2RawEventCD_t* cd_event = (EVT2RawEventCD_t*)&raw_events[raw_event_count++];
        cd_event->x = event->x;
        cd_event->y = event->y;
        cd_event->timestamp = event->timestamp & 0x3F;
        cd_event->type = (event->polarity > 0) ? EVT2_TYPE_CD_ON : EVT2_TYPE_CD_OFF;
    }
    
    encoder->buffer->size = raw_event_count * sizeof(EVT2RawEvent_t);
    
    encoder->total_events_encoded += event_count;
    encoder->total_bytes_output += encoder->buffer->size;
    
    *output_data = encoder->buffer->data;
    *output_size = encoder->buffer->size;
    
    return 0;
}

void evt2_encoder_get_stats(
    const EVT2Encoder_t* encoder,
    uint64_t* total_events_encoded,
    uint64_t* total_time_events,
    uint64_t* total_bytes_output)
{
    if (!encoder) return;
    
    if (total_events_encoded) *total_events_encoded = encoder->total_events_encoded;
    if (total_time_events) *total_time_events = encoder->total_time_events;
    if (total_bytes_output) *total_bytes_output = encoder->total_bytes_output;
}

void evt2_encoder_reset_stats(EVT2Encoder_t* encoder)
{
    if (!encoder) return;
    
    encoder->total_events_encoded = 0;
    encoder->total_time_events = 0;
    encoder->total_bytes_output = 0;
}

void evt2_encoder_print_stats(const EVT2Encoder_t* encoder)
{
    if (!encoder) return;
    
    printf("[EVT2 Encoder Statistics]\n");
    printf("  Total Events Encoded: %lu\n", encoder->total_events_encoded);
    printf("  Total Time Events:    %lu\n", encoder->total_time_events);
    printf("  Total Bytes Output:   %lu\n", encoder->total_bytes_output);
    
    if (encoder->total_events_encoded > 0) {
        uint64_t original_size = encoder->total_events_encoded * sizeof(EVSEvent_t);
        double compression_ratio = 100.0 * (1.0 - (double)encoder->total_bytes_output / original_size);
        printf("  Original Size:        %lu bytes\n", original_size);
        printf("  Compression Ratio:    %.1f%%\n", compression_ratio);
        printf("  Bytes/Event (avg):    %.2f\n", 
               (double)encoder->total_bytes_output / encoder->total_events_encoded);
    }
}
