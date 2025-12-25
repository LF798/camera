/**
 * @file encoded_packet.c
 * @brief 编码数据包实现
 */

#include "encoded_packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EncodedWindowPacket_t* encoded_packet_create(
    uint32_t window_id,
    uint64_t window_start_timestamp,
    uint64_t window_end_timestamp,
    uint32_t original_event_count,
    const uint8_t* encoded_data,
    size_t encoded_data_size,
    uint32_t subframes_in_window,
    uint32_t frames_in_window)
{
    if (!encoded_data || encoded_data_size == 0) {
        return NULL;
    }
    
    EncodedWindowPacket_t* packet = (EncodedWindowPacket_t*)malloc(sizeof(EncodedWindowPacket_t));
    if (!packet) {
        fprintf(stderr, "[EncodedPacket] Failed to allocate packet\n");
        return NULL;
    }
    
    packet->encoded_data = (uint8_t*)malloc(encoded_data_size);
    if (!packet->encoded_data) {
        fprintf(stderr, "[EncodedPacket] Failed to allocate data buffer\n");
        free(packet);
        return NULL;
    }
    
    packet->window_id = window_id;
    packet->window_start_timestamp = window_start_timestamp;
    packet->window_end_timestamp = window_end_timestamp;
    packet->original_event_count = original_event_count;
    packet->encoded_data_size = encoded_data_size;
    packet->subframes_in_window = subframes_in_window;
    packet->frames_in_window = frames_in_window;
    
    memcpy(packet->encoded_data, encoded_data, encoded_data_size);
    
    return packet;
}

void encoded_packet_destroy(EncodedWindowPacket_t* packet)
{
    if (packet) {
        if (packet->encoded_data) {
            free(packet->encoded_data);
        }
        free(packet);
    }
}
