/**
 * @file time_sync_client.c
 * @brief 时间同步客户端实现
 */

#include "time_sync_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

// ============================================================================
// 全局变量
// ============================================================================

static TimeSyncConfig_t g_config = {0};
static TimeSyncStatus_t g_status = {0};
static int64_t g_time_offset_us = 0;
static pthread_mutex_t g_offset_mutex = PTHREAD_MUTEX_INITIALIZER;
// NTP_ONCE模式不需要后台线程和漂移估计

// ============================================================================
// 内部函数
// ============================================================================

/**
 * @brief 获取原始系统时间（微秒）
 */
uint64_t time_sync_get_raw_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000UL + tv.tv_usec;
}

/**
 * @brief NTP同步（使用ntpd -gq）
 */
static int ntp_sync_once(const char *server)
{
    if (!server || strlen(server) == 0) {
        return -1;
    }
    
    // ====== 打印NTP同步前的系统时间 ======
    struct timeval tv_before;
    gettimeofday(&tv_before, NULL);
    uint64_t timestamp_before_us = (uint64_t)tv_before.tv_sec * 1000000UL + tv_before.tv_usec;
    
    struct tm *tm_before = localtime(&tv_before.tv_sec);
    printf("\n========== NTP Sync Start ==========\n");
    printf("[NTP BEFORE] System Time: %04d-%02d-%02d %02d:%02d:%02d.%06ld\n",
           tm_before->tm_year + 1900, tm_before->tm_mon + 1, tm_before->tm_mday,
           tm_before->tm_hour, tm_before->tm_min, tm_before->tm_sec,
           tv_before.tv_usec);
    printf("[NTP BEFORE] Timestamp: %lu us\n", timestamp_before_us);
    printf("[NTP BEFORE] Software offset: %ld us\n", (long)g_time_offset_us);
    
    // 检查是否已有ntpd守护进程在运行
    int check_ret = system("pgrep -x ntpd > /dev/null 2>&1");
    if (check_ret == 0) {
        printf("[NTP] Detected running ntpd daemon\n");
        printf("[NTP] Skipping one-time sync (system time already managed by ntpd)\n");
        printf("[NTP] Checking ntpd sync status...\n");
        
        // 使用ntpq查询同步状态
        int ntpq_ret = system("ntpq -p 2>/dev/null | grep -E '^\\*|^\\+' > /dev/null");
        
        struct timeval tv_after;
        gettimeofday(&tv_after, NULL);
        uint64_t timestamp_after_us = (uint64_t)tv_after.tv_sec * 1000000UL + tv_after.tv_usec;
        
        if (ntpq_ret == 0) {
            printf("[NTP] ntpd is synchronized with NTP server\n");
            g_status.sync_count++;
            g_status.last_sync_time_us = timestamp_after_us;
            printf("[NTP RESULT] Sync Status: SUCCESS (using existing ntpd)\n");
            printf("====================================\n\n");
            return 0;
        } else {
            printf("[NTP] ntpd is running but not yet synchronized\n");
            printf("[NTP RESULT] Sync Status: PARTIAL (ntpd daemon active)\n");
            printf("====================================\n\n");
            return 0;
        }
    }
    
    // 执行NTP同步 - 使用ntpd -gq替代ntpdate
    // -g: 允许大幅度时间调整
    // -q: 查询模式，同步后退出（不作为守护进程运行）
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ntpd -gq -p %s 2>&1", server);
    printf("[NTP] Executing: %s\n", cmd);
    
    int ret = system(cmd);
    
    // ====== 打印NTP同步后的系统时间 ======
    struct timeval tv_after;
    gettimeofday(&tv_after, NULL);
    uint64_t timestamp_after_us = (uint64_t)tv_after.tv_sec * 1000000UL + tv_after.tv_usec;
    
    struct tm *tm_after = localtime(&tv_after.tv_sec);
    printf("\n[NTP AFTER]  System Time: %04d-%02d-%02d %02d:%02d:%02d.%06ld\n",
           tm_after->tm_year + 1900, tm_after->tm_mon + 1, tm_after->tm_mday,
           tm_after->tm_hour, tm_after->tm_min, tm_after->tm_sec,
           tv_after.tv_usec);
    printf("[NTP AFTER]  Timestamp: %lu us\n", timestamp_after_us);
    
    // 计算时间跳变
    int64_t time_jump_us = (int64_t)timestamp_after_us - (int64_t)timestamp_before_us;
    printf("\n[NTP RESULT] Time Jump: %+ld us (%+.6f seconds)\n",
           (long)time_jump_us, time_jump_us / 1000000.0);
    
    if (ret == 0) {
        g_status.sync_count++;
        g_status.last_sync_time_us = timestamp_after_us;
        printf("[NTP RESULT] Sync Status: SUCCESS\n");
        printf("====================================\n\n");
        return 0;
    } else {
        g_status.sync_errors++;
        fprintf(stderr, "[NTP RESULT] Sync Status: FAILED (ret=%d)\n", ret);
        printf("====================================\n\n");
        return -1;
    }
}

// PTP功能已删除（不需要）

// 漂移估计功能已删除（服务器端负责计算偏移）

// NTP周期同步线程已删除（仅使用NTP_ONCE模式）

// ============================================================================
// API函数实现
// ============================================================================

int time_sync_init(const TimeSyncConfig_t *config)
{
    if (!config) {
        fprintf(stderr, "[TimeSync] Invalid config\n");
        return -1;
    }
    
    // 复制配置
    memcpy(&g_config, config, sizeof(TimeSyncConfig_t));
    
    // 初始化状态
    memset(&g_status, 0, sizeof(TimeSyncStatus_t));
    g_status.mode = config->mode;
    
    // 设置初始偏移
    pthread_mutex_lock(&g_offset_mutex);
    g_time_offset_us = config->initial_offset_us;
    g_status.time_offset_us = g_time_offset_us;
    pthread_mutex_unlock(&g_offset_mutex);
    
    printf("[TimeSync] Initializing (Device ID: %u, Mode: %d)\n", 
           config->device_id, config->mode);
    
    switch (config->mode) {
        case TIME_SYNC_MODE_NONE:
            printf("[TimeSync] Time sync disabled\n");
            g_status.sync_quality = 0;
            return 0;
            
        case TIME_SYNC_MODE_NTP_ONCE:
            // NTP初始同步一次 + 后续软件偏移模式（推荐）
            printf("[TimeSync] NTP initial sync mode (server: %s)\n", 
                   config->ntp_server);
            printf("[TimeSync] Performing one-time NTP sync...\n");
            
            // 执行一次NTP同步
            if (ntp_sync_once(config->ntp_server) == 0) {
                printf("[TimeSync] Initial NTP sync successful\n");
                printf("[TimeSync] Switching to software offset mode\n");
                g_status.sync_quality = 85;  // NTP初始同步 + 软件偏移质量较高
                g_status.sync_count = 1;
            } else {
                fprintf(stderr, "[TimeSync] Initial NTP sync failed\n");
                fprintf(stderr, "[TimeSync] Continuing with software offset only\n");
                g_status.sync_quality = 50;  // 降级为纯软件偏移
            }
            
            printf("[TimeSync] Software offset enabled (auto-adjust: %s)\n",
                   config->enable_auto_adjust ? "YES" : "NO");
            return 0;
            
        default:
            fprintf(stderr, "[TimeSync] Unknown sync mode: %d\n", config->mode);
            fprintf(stderr, "[TimeSync] Please use TIME_SYNC_MODE_NTP_ONCE\n");
            return -1;
    }
}

uint64_t time_sync_get_timestamp_us(void)
{
    uint64_t raw_time = time_sync_get_raw_timestamp_us();
    
    pthread_mutex_lock(&g_offset_mutex);
    int64_t offset = g_time_offset_us;
    pthread_mutex_unlock(&g_offset_mutex);
    
    // 应用偏移
    return raw_time + offset;
}

int64_t time_sync_get_offset_us(void)
{
    pthread_mutex_lock(&g_offset_mutex);
    int64_t offset = g_time_offset_us;
    pthread_mutex_unlock(&g_offset_mutex);
    return offset;
}

void time_sync_set_offset_us(int64_t offset_us)
{
    // 检查是否允许自动调整
    if (!g_config.enable_auto_adjust) {
        fprintf(stderr, "[TimeSync] Auto adjust disabled, ignoring offset change\n");
        return;
    }
    
    pthread_mutex_lock(&g_offset_mutex);
    int64_t old_offset = g_time_offset_us;
    g_time_offset_us = offset_us;
    g_status.time_offset_us = offset_us;
    pthread_mutex_unlock(&g_offset_mutex);
    
    printf("[TimeSync] Time offset updated: %lld us -> %lld us (delta: %lld us = %.3f ms)\n",
           (long long)old_offset, 
           (long long)offset_us,
           (long long)(offset_us - old_offset),
           (offset_us - old_offset) / 1000.0);
}

uint32_t time_sync_get_quality(void)
{
    return g_status.sync_quality;
}

void time_sync_get_status(TimeSyncStatus_t *status)
{
    if (!status) {
        return;
    }
    
    pthread_mutex_lock(&g_offset_mutex);
    memcpy(status, &g_status, sizeof(TimeSyncStatus_t));
    status->time_offset_us = g_time_offset_us;
    pthread_mutex_unlock(&g_offset_mutex);
}

uint32_t time_sync_get_device_id(void)
{
    return g_config.device_id;
}

int time_sync_trigger_sync(void)
{
    if (g_status.mode == TIME_SYNC_MODE_NTP_ONCE) {
        return ntp_sync_once(g_config.ntp_server);
    }
    
    printf("[TimeSync] No sync method active\n");
    return -1;
}

void time_sync_cleanup(void)
{
    printf("[TimeSync] Cleaning up...\n");
    // NTP_ONCE模式无需清理（无后台线程）
    printf("[TimeSync] Cleanup complete\n");
}

int64_t time_sync_estimate_drift(void)
{
    return g_status.estimated_drift_us;
}

