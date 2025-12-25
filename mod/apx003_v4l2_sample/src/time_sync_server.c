/**
 * @file time_sync_server.c
 * @brief 时间同步服务器实现
 */

#include "time_sync_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>

// ============================================================================
// 全局变量
// ============================================================================

static DeviceTimeInfo_t g_devices[MAX_SYNC_DEVICES];
static int g_device_count = 0;
static uint32_t g_reference_device_id = 0;
static pthread_mutex_t g_devices_mutex = PTHREAD_MUTEX_INITIALIZER;
static TimeSyncServerStats_t g_stats = {0};
static int g_initialized = 0;

// ============================================================================
// 内部函数
// ============================================================================

/**
 * @brief 查找设备索引
 */
static int find_device_index(uint32_t device_id)
{
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].device_id == device_id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 获取当前时间（微秒）
 */
static uint64_t get_current_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000UL + tv.tv_usec;
}

// ============================================================================
// API函数实现
// ============================================================================

int time_sync_server_init(void)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    if (g_initialized) {
        pthread_mutex_unlock(&g_devices_mutex);
        return 0;
    }
    
    // 初始化设备列表
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_reference_device_id = 0;
    
    // 初始化统计信息
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.server_start_time = get_current_time_us();
    
    g_initialized = 1;
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    printf("[TimeSyncServer] Initialized\n");
    return 0;
}

void time_sync_server_update_device(uint32_t device_id, uint64_t timestamp_us)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    // 查找设备
    int idx = find_device_index(device_id);
    
    // 新设备
    if (idx < 0) {
        if (g_device_count >= MAX_SYNC_DEVICES) {
            fprintf(stderr, "[TimeSyncServer] Device limit reached (%d)\n", MAX_SYNC_DEVICES);
            pthread_mutex_unlock(&g_devices_mutex);
            return;
        }
        
        idx = g_device_count++;
        g_devices[idx].device_id = device_id;
        g_devices[idx].calculated_offset_us = 0;
        g_devices[idx].packet_count = 0;
        g_devices[idx].sync_request_count = 0;
        g_devices[idx].is_active = 1;
        g_devices[idx].is_reference = 0;
        
        printf("[TimeSyncServer] New device registered: ID=%u\n", device_id);
        
        // 如果是第一个设备，自动设为参考
        if (g_reference_device_id == 0) {
            g_reference_device_id = device_id;
            g_stats.reference_device_id = device_id;  // 同步更新统计信息
            g_devices[idx].is_reference = 1;
            printf("[TimeSyncServer] Device %u set as reference (first device)\n", device_id);
        }
        
        g_stats.total_devices = g_device_count;
    }
    
    // 更新设备信息
    g_devices[idx].last_timestamp_us = timestamp_us;
    g_devices[idx].packet_count++;
    gettimeofday(&g_devices[idx].last_seen, NULL);
    g_devices[idx].is_active = 1;
    
    pthread_mutex_unlock(&g_devices_mutex);
}

int time_sync_server_set_reference_device(uint32_t device_id)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    int idx = find_device_index(device_id);
    if (idx < 0) {
        fprintf(stderr, "[TimeSyncServer] Device %u not found\n", device_id);
        pthread_mutex_unlock(&g_devices_mutex);
        return -1;
    }
    
    // 清除旧的参考标记
    for (int i = 0; i < g_device_count; i++) {
        g_devices[i].is_reference = 0;
    }
    
    // 设置新的参考
    g_devices[idx].is_reference = 1;
    g_reference_device_id = device_id;
    g_stats.reference_device_id = device_id;
    
    printf("[TimeSyncServer] Device %u set as reference\n", device_id);
    
    pthread_mutex_unlock(&g_devices_mutex);
    return 0;
}

int time_sync_server_calculate_offsets(void)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    // 检查是否有参考设备
    if (g_reference_device_id == 0) {
        pthread_mutex_unlock(&g_devices_mutex);
        return -1;
    }
    
    // 找到参考设备
    int ref_idx = find_device_index(g_reference_device_id);
    if (ref_idx < 0 || !g_devices[ref_idx].is_active) {
        fprintf(stderr, "[TimeSyncServer] Reference device %u not active\n", g_reference_device_id);
        pthread_mutex_unlock(&g_devices_mutex);
        return -1;
    }
    
    // 参考设备的时间戳
    uint64_t ref_timestamp = g_devices[ref_idx].last_timestamp_us;
    
    // 注意：各设备的last_timestamp可能来自不同时刻的数据包
    // 这会引入一定误差，但由于EVS数据连续，误差通常在1-2ms内可接受
    
    // 计算统计值
    int64_t max_offset = INT64_MIN;
    int64_t min_offset = INT64_MAX;
    int64_t sum_offset = 0;
    int active_count = 0;
    int non_ref_count = 0;  // 非参考设备数量
    
    // 计算其他设备的偏移
    for (int i = 0; i < g_device_count; i++) {
        if (!g_devices[i].is_active) {
            continue;
        }
        
        if (g_devices[i].device_id == g_reference_device_id) {
            g_devices[i].calculated_offset_us = 0;  // 参考设备偏移为0
            active_count++;
            continue;
        }
        
        // 计算偏移：需要加上多少才能对齐到参考设备
        int64_t offset = (int64_t)ref_timestamp - (int64_t)g_devices[i].last_timestamp_us;
        g_devices[i].calculated_offset_us = offset;
        
        // 更新统计
        if (offset > max_offset) max_offset = offset;
        if (offset < min_offset) min_offset = offset;
        sum_offset += offset;
        active_count++;
        non_ref_count++;
    }
    
    // 更新服务器统计
    g_stats.active_devices = active_count;
    
    // 如果没有非参考设备，统计值设为0（只有参考设备）
    if (non_ref_count == 0) {
        g_stats.max_offset_us = 0;
        g_stats.min_offset_us = 0;
        g_stats.avg_offset_us = 0;
    } else {
        g_stats.max_offset_us = max_offset;
        g_stats.min_offset_us = min_offset;
        g_stats.avg_offset_us = sum_offset / non_ref_count;
    }
    
    g_stats.sync_cycles++;
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    return 0;
}

int64_t time_sync_server_get_device_offset(uint32_t device_id)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    int idx = find_device_index(device_id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_devices_mutex);
        return 0;
    }
    
    int64_t offset = g_devices[idx].calculated_offset_us;
    g_devices[idx].sync_request_count++;
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    return offset;
}

int time_sync_server_get_device_info(uint32_t device_id, DeviceTimeInfo_t *info)
{
    if (!info) {
        return -1;
    }
    
    pthread_mutex_lock(&g_devices_mutex);
    
    int idx = find_device_index(device_id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_devices_mutex);
        return -1;
    }
    
    memcpy(info, &g_devices[idx], sizeof(DeviceTimeInfo_t));
    
    pthread_mutex_unlock(&g_devices_mutex);
    return 0;
}

int time_sync_server_get_device_list(uint32_t *devices, int max_count)
{
    if (!devices || max_count <= 0) {
        return 0;
    }
    
    pthread_mutex_lock(&g_devices_mutex);
    
    int count = (g_device_count < max_count) ? g_device_count : max_count;
    for (int i = 0; i < count; i++) {
        devices[i] = g_devices[i].device_id;
    }
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    return count;
}

int time_sync_server_check_timeouts(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    
    pthread_mutex_lock(&g_devices_mutex);
    
    int timeout_count = 0;
    
    for (int i = 0; i < g_device_count; i++) {
        if (!g_devices[i].is_active) {
            continue;
        }
        
        // 计算时间差（秒）
        long diff_sec = now.tv_sec - g_devices[i].last_seen.tv_sec;
        
        if (diff_sec > DEVICE_TIMEOUT_SEC) {
            g_devices[i].is_active = 0;
            timeout_count++;
            printf("[TimeSyncServer] Device %u timeout (last seen %ld sec ago)\n",
                   g_devices[i].device_id, diff_sec);
        }
    }
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    return timeout_count;
}

void time_sync_server_get_stats(TimeSyncServerStats_t *stats)
{
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&g_devices_mutex);
    memcpy(stats, &g_stats, sizeof(TimeSyncServerStats_t));
    pthread_mutex_unlock(&g_devices_mutex);
}

void time_sync_server_print_status(void)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    printf("\n");
    printf("========================================================\n");
    printf("          Time Sync Server Status\n");
    printf("========================================================\n");
    printf("Total Devices:     %u\n", g_stats.total_devices);
    printf("Active Devices:    %u\n", g_stats.active_devices);
    printf("Reference Device:  %u\n", g_stats.reference_device_id);
    printf("Sync Cycles:       %u\n", g_stats.sync_cycles);
    printf("Max Offset:        %lld us (%.3f ms)\n", 
           (long long)g_stats.max_offset_us, 
           g_stats.max_offset_us / 1000.0);
    printf("Min Offset:        %lld us (%.3f ms)\n",
           (long long)g_stats.min_offset_us,
           g_stats.min_offset_us / 1000.0);
    printf("Avg Offset:        %lld us (%.3f ms)\n",
           (long long)g_stats.avg_offset_us,
           g_stats.avg_offset_us / 1000.0);
    printf("========================================================\n");
    
    if (g_device_count > 0) {
        printf("\nDevice Details:\n");
        printf("--------------------------------------------------------\n");
        printf("%-8s %-12s %-18s %-15s %-10s %s\n",
               "ID", "Status", "Last Timestamp", "Offset (us)", "Packets", "Ref");
        printf("--------------------------------------------------------\n");
        
        for (int i = 0; i < g_device_count; i++) {
            printf("%-8u %-12s %-18llu %-15lld %-10u %s\n",
                   g_devices[i].device_id,
                   g_devices[i].is_active ? "Active" : "Timeout",
                   (unsigned long long)g_devices[i].last_timestamp_us,
                   (long long)g_devices[i].calculated_offset_us,
                   g_devices[i].packet_count,
                   g_devices[i].is_reference ? "YES" : "");
        }
        printf("--------------------------------------------------------\n");
    }
    
    printf("\n");
    
    pthread_mutex_unlock(&g_devices_mutex);
}

void time_sync_server_cleanup(void)
{
    pthread_mutex_lock(&g_devices_mutex);
    
    printf("[TimeSyncServer] Cleaning up...\n");
    
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_reference_device_id = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 0;
    
    pthread_mutex_unlock(&g_devices_mutex);
    
    printf("[TimeSyncServer] Cleanup complete\n");
}

