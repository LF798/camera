/**
 * @file time_sync_server_standalone.c
 * @brief 独立时间同步服务器程序（UDP）
 * @details 接收多个设备的心跳时间戳，计算偏移，下发偏移值
 */

#include "time_sync_protocol.h"
#include "time_sync_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

// ============================================================================
// 全局变量
// ============================================================================

static int g_sockfd = -1;
static volatile int g_running = 1;

// 客户端地址表（用于回复）
typedef struct {
    uint32_t device_id;
    struct sockaddr_in addr;
    int valid;
} ClientAddress_t;

#define MAX_CLIENT_ADDRS 32
static ClientAddress_t g_client_addrs[MAX_CLIENT_ADDRS];
static pthread_mutex_t g_addr_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 信号处理
 */
static void signal_handler(int sig)
{
    (void)sig;
    printf("\n[TimeSyncServer] Shutting down...\n");
    g_running = 0;
}

/**
 * @brief 保存或更新客户端地址
 */
static void save_client_address(uint32_t device_id, const struct sockaddr_in *addr)
{
    pthread_mutex_lock(&g_addr_mutex);
    
    // 查找是否已存在
    int idx = -1;
    for (int i = 0; i < MAX_CLIENT_ADDRS; i++) {
        if (g_client_addrs[i].valid && g_client_addrs[i].device_id == device_id) {
            idx = i;
            break;
        }
    }
    
    // 如果不存在，找空位
    if (idx < 0) {
        for (int i = 0; i < MAX_CLIENT_ADDRS; i++) {
            if (!g_client_addrs[i].valid) {
                idx = i;
                break;
            }
        }
    }
    
    if (idx >= 0) {
        g_client_addrs[idx].device_id = device_id;
        memcpy(&g_client_addrs[idx].addr, addr, sizeof(struct sockaddr_in));
        g_client_addrs[idx].valid = 1;
    }
    
    pthread_mutex_unlock(&g_addr_mutex);
}

/**
 * @brief 获取客户端地址
 */
static int get_client_address(uint32_t device_id, struct sockaddr_in *addr)
{
    pthread_mutex_lock(&g_addr_mutex);
    
    for (int i = 0; i < MAX_CLIENT_ADDRS; i++) {
        if (g_client_addrs[i].valid && g_client_addrs[i].device_id == device_id) {
            memcpy(addr, &g_client_addrs[i].addr, sizeof(struct sockaddr_in));
            pthread_mutex_unlock(&g_addr_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_addr_mutex);
    return -1;
}

/**
 * @brief 发送偏移应答给指定设备
 */
static void send_offset_reply(uint32_t device_id, int64_t offset_us, uint32_t sequence)
{
    struct sockaddr_in client_addr;
    if (get_client_address(device_id, &client_addr) < 0) {
        return;
    }
    
    TimeSyncOffsetReplyMsg_t reply;
    time_sync_init_header(&reply.header, TIME_SYNC_MSG_OFFSET_REPLY, 0);
    reply.offset_us = offset_us;
    reply.reference_device_id = 0;  // 将在下面更新
    reply.sync_quality = 85;
    reply.sequence = sequence;
    
    // 获取参考设备ID
    TimeSyncServerStats_t stats;
    time_sync_server_get_stats(&stats);
    reply.reference_device_id = stats.reference_device_id;
    
    // 发送UDP回复
    sendto(g_sockfd, &reply, sizeof(reply), 0,
           (struct sockaddr*)&client_addr, sizeof(client_addr));
    
    printf("[Send] Device %u: offset=%lld us (%.3f ms), seq=%u\n",
           device_id, (long long)offset_us, offset_us / 1000.0, sequence);
}

/**
 * @brief 处理心跳消息
 */
static void handle_heartbeat(const TimeSyncHeartbeatMsg_t *msg, 
                             const struct sockaddr_in *client_addr)
{
    uint32_t device_id = msg->header.device_id;
    uint64_t timestamp_us = msg->timestamp_us;
    uint32_t sequence = msg->sequence;
    
    printf("[Heartbeat] Device %u: timestamp=%llu us, seq=%u\n",
           device_id, (unsigned long long)timestamp_us, sequence);
    
    // 保存客户端地址
    save_client_address(device_id, client_addr);
    
    // 更新设备时间戳
    time_sync_server_update_device(device_id, timestamp_us);
    
    // 计算偏移
    time_sync_server_calculate_offsets();
    
    // 获取该设备的偏移
    int64_t offset = time_sync_server_get_device_offset(device_id);
    
    // 发送偏移应答
    send_offset_reply(device_id, offset, sequence);
}

/**
 * @brief 处理状态查询请求
 */
static void handle_status_request(const TimeSyncMsgHeader_t *header,
                                   const struct sockaddr_in *client_addr)
{
    TimeSyncStatusReplyMsg_t reply;
    time_sync_init_header(&reply.header, TIME_SYNC_MSG_STATUS_REPLY, 0);
    
    TimeSyncServerStats_t stats;
    time_sync_server_get_stats(&stats);
    
    reply.total_devices = stats.total_devices;
    reply.active_devices = stats.active_devices;
    reply.reference_device_id = stats.reference_device_id;
    reply.max_offset_us = stats.max_offset_us;
    reply.min_offset_us = stats.min_offset_us;
    reply.avg_offset_us = stats.avg_offset_us;
    
    sendto(g_sockfd, &reply, sizeof(reply), 0,
           (struct sockaddr*)client_addr, sizeof(*client_addr));
}

/**
 * @brief 接收线程
 */
static void* receive_thread(void* arg)
{
    (void)arg;
    uint8_t buffer[1024];
    
    printf("[TimeSyncServer] Receive thread started\n");
    
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        ssize_t recv_len = recvfrom(g_sockfd, buffer, sizeof(buffer), 0,
                                     (struct sockaddr*)&client_addr, &addr_len);
        
        if (recv_len < 0) {
            if (g_running) {
                perror("recvfrom");
            }
            break;
        }
        
        if (recv_len < (ssize_t)sizeof(TimeSyncMsgHeader_t)) {
            fprintf(stderr, "[TimeSyncServer] Message too short: %zd bytes\n", recv_len);
            continue;
        }
        
        TimeSyncMsgHeader_t *header = (TimeSyncMsgHeader_t*)buffer;
        
        // 验证消息头
        if (time_sync_validate_header(header) < 0) {
            fprintf(stderr, "[TimeSyncServer] Invalid message header\n");
            continue;
        }
        
        // 处理不同类型的消息
        switch (header->msg_type) {
            case TIME_SYNC_MSG_HEARTBEAT:
                if (recv_len >= (ssize_t)sizeof(TimeSyncHeartbeatMsg_t)) {
                    handle_heartbeat((TimeSyncHeartbeatMsg_t*)buffer, &client_addr);
                }
                break;
                
            case TIME_SYNC_MSG_STATUS_REQ:
                handle_status_request(header, &client_addr);
                break;
                
            default:
                fprintf(stderr, "[TimeSyncServer] Unknown message type: %u\n", 
                        header->msg_type);
                break;
        }
    }
    
    printf("[TimeSyncServer] Receive thread stopped\n");
    return NULL;
}

/**
 * @brief 监控线程（周期性打印状态）
 */
static void* monitor_thread(void* arg)
{
    (void)arg;
    
    printf("[TimeSyncServer] Monitor thread started\n");
    
    while (g_running) {
        sleep(5);
        
        if (!g_running) {
            break;
        }
        
        // 检查超时设备
        int timeout_count = time_sync_server_check_timeouts();
        if (timeout_count > 0) {
            printf("[Monitor] %d device(s) timeout\n", timeout_count);
        }
        
        // 打印状态
        time_sync_server_print_status();
    }
    
    printf("[TimeSyncServer] Monitor thread stopped\n");
    return NULL;
}

// ============================================================================
// 主程序
// ============================================================================

int main(int argc, char *argv[])
{
    int port = TIME_SYNC_DEFAULT_PORT;
    
    // 解析命令行参数
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            fprintf(stderr, "  port: UDP port (default: %d)\n", TIME_SYNC_DEFAULT_PORT);
            return 1;
        }
    }
    
    printf("========================================\n");
    printf("  Time Sync Server (Standalone)\n");
    printf("========================================\n");
    printf("Port: %d\n", port);
    printf("========================================\n\n");
    
    // 初始化时间同步服务器
    if (time_sync_server_init() < 0) {
        fprintf(stderr, "Failed to initialize time sync server\n");
        return 1;
    }
    
    // 创建UDP socket
    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockfd < 0) {
        perror("socket");
        return 1;
    }
    
    // 设置socket选项
    int reuse = 1;
    setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(g_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(g_sockfd);
        return 1;
    }
    
    printf("[TimeSyncServer] Listening on UDP port %d\n\n", port);
    
    // 初始化客户端地址表
    memset(g_client_addrs, 0, sizeof(g_client_addrs));
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 启动接收线程
    pthread_t recv_tid, monitor_tid;
    if (pthread_create(&recv_tid, NULL, receive_thread, NULL) != 0) {
        perror("pthread_create (receive)");
        close(g_sockfd);
        return 1;
    }
    
    // 启动监控线程
    if (pthread_create(&monitor_tid, NULL, monitor_thread, NULL) != 0) {
        perror("pthread_create (monitor)");
        g_running = 0;
        pthread_join(recv_tid, NULL);
        close(g_sockfd);
        return 1;
    }
    
    // 等待线程结束
    pthread_join(recv_tid, NULL);
    pthread_join(monitor_tid, NULL);
    
    // 清理
    close(g_sockfd);
    time_sync_server_cleanup();
    
    printf("\n[TimeSyncServer] Shutdown complete\n");
    return 0;
}
