/**
 * @file time_sync_client_test_main.c
 * @brief 时间同步客户端测试程序
 */

#include "time_sync_client.h"
#include "time_sync_client_standalone.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[Test] Stopping...\n");
    g_running = 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device_id> <server_ip> [server_port]\n", argv[0]);
        fprintf(stderr, "  device_id:   Device ID (1-255)\n");
        fprintf(stderr, "  server_ip:   Time sync server IP address\n");
        fprintf(stderr, "  server_port: Server port (default: 9999)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  %s 1 192.168.1.100\n", argv[0]);
        fprintf(stderr, "  %s 2 192.168.1.100 9999\n", argv[0]);
        return 1;
    }
    
    uint32_t device_id = (uint32_t)atoi(argv[1]);
    const char *server_ip = argv[2];
    int server_port = (argc > 3) ? atoi(argv[3]) : 0;
    
    printf("========================================\n");
    printf("  Time Sync Client Test\n");
    printf("========================================\n");
    printf("Device ID: %u\n", device_id);
    printf("Server: %s:%d\n", server_ip, server_port > 0 ? server_port : 9999);
    printf("========================================\n\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化基础时间同步客户端
    TimeSyncConfig_t config = {
        .device_id = device_id,
        .mode = TIME_SYNC_MODE_NTP_ONCE,
        .ntp_server = "192.168.1.1",  // NTP服务器（可选）
        .initial_offset_us = 0,
        .enable_auto_adjust = 1
    };
    
    // 初始化（这会执行一次NTP同步，如果配置了NTP服务器）
    if (time_sync_init(&config) < 0) {
        fprintf(stderr, "[Test] Failed to initialize time sync\n");
        // 继续运行，仅使用软件偏移
    }
    
    // 初始化独立时间同步客户端（与服务器通信）
    if (time_sync_client_standalone_init(device_id, server_ip, server_port) < 0) {
        fprintf(stderr, "[Test] Failed to initialize standalone client\n");
        return 1;
    }
    
    printf("\n[Test] Running... Press Ctrl+C to stop\n\n");
    
    // 主循环：周期性打印当前时间戳
    int counter = 0;
    while (g_running) {
        sleep(5);
        
        if (!g_running) break;
        
        counter++;
        
        // 获取校准后的时间戳
        uint64_t timestamp = time_sync_get_timestamp_us();
        uint64_t raw_timestamp = time_sync_get_raw_timestamp_us();
        int64_t offset = time_sync_get_offset_us();
        
        printf("[%d] Raw: %llu us, Offset: %lld us (%.3f ms), Calibrated: %llu us\n",
               counter,
               (unsigned long long)raw_timestamp,
               (long long)offset,
               offset / 1000.0,
               (unsigned long long)timestamp);
        
        // 获取同步状态
        TimeSyncStatus_t status;
        time_sync_get_status(&status);
        printf("     Quality: %u, Sync count: %u, Errors: %u\n",
               status.sync_quality, status.sync_count, status.sync_errors);
    }
    
    printf("\n[Test] Cleaning up...\n");
    time_sync_client_standalone_cleanup();
    
    printf("[Test] Stopped\n");
    return 0;
}
