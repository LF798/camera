/**
 * @file evt2_decoder.c
 * @brief EVT2事件解码器实现
 */

#include "evt2_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EVT2Decoder_t* evt2_decoder_create(void)
{
    EVT2Decoder_t* decoder = (EVT2Decoder_t*)malloc(sizeof(EVT2Decoder_t));
    if (!decoder) {
        fprintf(stderr, "[EVT2 Decoder] Failed to allocate decoder\n");
        return NULL;
    }
    
    decoder->current_time_high = 0;
    decoder->total_events_decoded = 0;
    decoder->total_time_events = 0;
    decoder->total_bytes_input = 0;
    
    return decoder;
}

void evt2_decoder_destroy(EVT2Decoder_t* decoder)
{
    if (decoder) {
        free(decoder);
    }
}

int evt2_decoder_decode(
    EVT2Decoder_t* decoder,
    const uint8_t* encoded_data,
    size_t encoded_size,
    EVSEvent_t* events,
    uint32_t max_events,
    uint32_t* event_count)
{
    if (!decoder || !encoded_data || !events || !event_count || encoded_size == 0) {
        return -1;
    }
    
    *event_count = 0;
    decoder->total_bytes_input += encoded_size;
    
    // EVT2原始事件是4字节对齐的
    if (encoded_size % sizeof(EVT2RawEvent_t) != 0) {
        fprintf(stderr, "[EVT2 Decoder] Invalid data size: %zu (not aligned to 4 bytes)\n", 
               encoded_size);
        return -1;
    }
    
    const EVT2RawEvent_t* raw_events = (const EVT2RawEvent_t*)encoded_data;
    size_t num_raw_events = encoded_size / sizeof(EVT2RawEvent_t);
    
    for (size_t i = 0; i < num_raw_events; i++) {
        const EVT2RawEvent_t* raw_event = &raw_events[i];
        
        // 检查事件类型
        uint8_t event_type = (raw_event->raw >> 28) & 0x0F;
        
        if (event_type == EVT2_TYPE_TIME_HIGH) {
            // 时间高位事件
            const EVT2RawEventTime_t* time_event = (const EVT2RawEventTime_t*)raw_event;
            decoder->current_time_high = ((uint64_t)time_event->timestamp) << 6;
            decoder->total_time_events++;
        }
        else if (event_type == EVT2_TYPE_CD_ON || event_type == EVT2_TYPE_CD_OFF) {
            // CD事件
            if (*event_count >= max_events) {
                fprintf(stderr, "[EVT2 Decoder] Output buffer full (%u events)\n", max_events);
                return -1;
            }
            
            const EVT2RawEventCD_t* cd_event = (const EVT2RawEventCD_t*)raw_event;
            
            EVSEvent_t* event = &events[*event_count];
            event->x = cd_event->x;
            event->y = cd_event->y;
            event->timestamp = decoder->current_time_high | cd_event->timestamp;
            event->polarity = (event_type == EVT2_TYPE_CD_ON) ? 1 : 0;
            
            (*event_count)++;
            decoder->total_events_decoded++;
        }
        else if (event_type == EVT2_TYPE_EXT_TRIGGER) {
            // 外部触发事件（暂时忽略）
            continue;
        }
        else {
            // 未知事件类型
            fprintf(stderr, "[EVT2 Decoder] Unknown event type: 0x%02X\n", event_type);
        }
    }
    
    return 0;
}

void evt2_decoder_get_stats(
    const EVT2Decoder_t* decoder,
    uint64_t* total_events_decoded,
    uint64_t* total_time_events,
    uint64_t* total_bytes_input)
{
    if (!decoder) return;
    
    if (total_events_decoded) *total_events_decoded = decoder->total_events_decoded;
    if (total_time_events) *total_time_events = decoder->total_time_events;
    if (total_bytes_input) *total_bytes_input = decoder->total_bytes_input;
}

void evt2_decoder_reset_stats(EVT2Decoder_t* decoder)
{
    if (!decoder) return;
    
    decoder->total_events_decoded = 0;
    decoder->total_time_events = 0;
    decoder->total_bytes_input = 0;
}

void evt2_decoder_print_stats(const EVT2Decoder_t* decoder)
{
    if (!decoder) return;
    
    printf("[EVT2 Decoder Statistics]\n");
    printf("  Total Events Decoded: %lu\n", decoder->total_events_decoded);
    printf("  Total Time Events:    %lu\n", decoder->total_time_events);
    printf("  Total Bytes Input:    %lu\n", decoder->total_bytes_input);
    
    if (decoder->total_events_decoded > 0) {
        printf("  Bytes/Event (avg):    %.2f\n", 
               (double)decoder->total_bytes_input / decoder->total_events_decoded);
    }
}
