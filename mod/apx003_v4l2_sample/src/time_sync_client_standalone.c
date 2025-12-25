/**
 * @file time_sync_client_standalone.c
 * @brief 独立时间同步客户端（与独立服务器通信）
 * @details 通过UDP与独立时间同步服务器通信，发送心跳，接收偏移
 */

#include "time_sync_protocol.h"
#include "time_sync_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>

// ============================================================================
// 全局变量
// ============================================================================

static int g_sockfd = -1;
static volatile int g_running = 0;
static pthread_t g_heartbeat_thread = 0;
static pthread_t g_receive_thread = 0;
static uint32_t g_sequence = 0;

static char g_server_ip[64] = {0};
static int g_server_port = TIME_SYNC_DEFAULT_PORT;
static uint32_t g_device_id = 0;

static pthread_mutex_t g_seq_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// 内部函数
// ============================================================================

/**
 * @brief 获取并递增序列号
 */
static uint32_t get_next_sequence(void)
{
    pthread_mutex_lock(&g_seq_mutex);
    uint32_t seq = g_sequence++;
    pthread_mutex_unlock(&g_seq_mutex);
    return seq;
}

/**
 * @brief 发送心跳消息
 */
static int send_heartbeat(void)
{
    TimeSyncHeartbeatMsg_t msg;
    time_sync_init_header(&msg.header, TIME_SYNC_MSG_HEARTBEAT, g_device_id);
    
    // 获取当前时间戳
    msg.timestamp_us = time_sync_get_raw_timestamp_us();
    msg.sequence = get_next_sequence();
    
    // 发送UDP消息
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_server_port);
    
    if (inet_pton(AF_INET, g_server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[TimeSync] Invalid server IP: %s\n", g_server_ip);
        return -1;
    }
    
    ssize_t sent = sendto(g_sockfd, &msg, sizeof(msg), 0,
                          (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (sent < 0) {
        perror("[TimeSync] sendto");
        return -1;
    }
    
    return 0;
}

/**
 * @brief 处理偏移应答
 */
static void handle_offset_reply(const TimeSyncOffsetReplyMsg_t *reply)
{
    int64_t offset_us = reply->offset_us;
    uint32_t ref_device = reply->reference_device_id;
    uint32_t quality = reply->sync_quality;
    uint32_t seq = reply->sequence;
    
    printf("[Offset] Received: offset=%lld us (%.3f ms), ref_device=%u, quality=%u, seq=%u\n",
           (long long)offset_us, offset_us / 1000.0, ref_device, quality, seq);
    
    // 应用偏移
    time_sync_set_offset_us(offset_us);
}

/**
 * @brief 接收线程
 */
static void* receive_thread(void* arg)
{
    (void)arg;
    uint8_t buffer[1024];
    
    printf("[TimeSync] Receive thread started\n");
    
    while (g_running) {
        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);
        
        ssize_t recv_len = recvfrom(g_sockfd, buffer, sizeof(buffer), 0,
                                     (struct sockaddr*)&from_addr, &addr_len);
        
        if (recv_len < 0) {
            if (g_running && errno != EINTR) {
                perror("[TimeSync] recvfrom");
            }
            continue;
        }
        
        if (recv_len < (ssize_t)sizeof(TimeSyncMsgHeader_t)) {
            continue;
        }
        
        TimeSyncMsgHeader_t *header = (TimeSyncMsgHeader_t*)buffer;
        
        // 验证消息头
        if (time_sync_validate_header(header) < 0) {
            continue;
        }
        
        // 处理消息
        switch (header->msg_type) {
            case TIME_SYNC_MSG_OFFSET_REPLY:
                if (recv_len >= (ssize_t)sizeof(TimeSyncOffsetReplyMsg_t)) {
                    handle_offset_reply((TimeSyncOffsetReplyMsg_t*)buffer);
                }
                break;
                
            default:
                break;
        }
    }
    
    printf("[TimeSync] Receive thread stopped\n");
    return NULL;
}

/**
 * @brief 心跳线程
 */
static void* heartbeat_thread(void* arg)
{
    (void)arg;
    
    printf("[TimeSync] Heartbeat thread started (interval: %d ms)\n", 
           TIME_SYNC_HEARTBEAT_INTERVAL_MS);
    
    while (g_running) {
        // 发送心跳
        if (send_heartbeat() < 0) {
            fprintf(stderr, "[TimeSync] Failed to send heartbeat\n");
        }
        
        // 等待
        usleep(TIME_SYNC_HEARTBEAT_INTERVAL_MS * 1000);
    }
    
    printf("[TimeSync] Heartbeat thread stopped\n");
    return NULL;
}

// ============================================================================
// API函数
// ============================================================================

/**
 * @brief 初始化独立时间同步客户端
 */
int time_sync_client_standalone_init(uint32_t device_id, 
                                      const char *server_ip, 
                                      int server_port)
{
    if (!server_ip || strlen(server_ip) == 0) {
        fprintf(stderr, "[TimeSync] Invalid server IP\n");
        return -1;
    }
    
    g_device_id = device_id;
    strncpy(g_server_ip, server_ip, sizeof(g_server_ip) - 1);
    g_server_port = (server_port > 0) ? server_port : TIME_SYNC_DEFAULT_PORT;
    
    printf("[TimeSync] Initializing standalone client\n");
    printf("  Device ID: %u\n", g_device_id);
    printf("  Server: %s:%d\n", g_server_ip, g_server_port);
    
    // 创建UDP socket
    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // 设置接收超时（避免阻塞关闭）
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(g_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // 启动线程
    g_running = 1;
    
    if (pthread_create(&g_receive_thread, NULL, receive_thread, NULL) != 0) {
        perror("pthread_create (receive)");
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }
    
    if (pthread_create(&g_heartbeat_thread, NULL, heartbeat_thread, NULL) != 0) {
        perror("pthread_create (heartbeat)");
        g_running = 0;
        pthread_join(g_receive_thread, NULL);
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }
    
    printf("[TimeSync] Standalone client initialized successfully\n");
    return 0;
}

/**
 * @brief 清理独立时间同步客户端
 */
void time_sync_client_standalone_cleanup(void)
{
    printf("[TimeSync] Cleaning up standalone client...\n");
    
    g_running = 0;
    
    if (g_heartbeat_thread) {
        pthread_join(g_heartbeat_thread, NULL);
        g_heartbeat_thread = 0;
    }
    
    if (g_receive_thread) {
        pthread_join(g_receive_thread, NULL);
        g_receive_thread = 0;
    }
    
    if (g_sockfd >= 0) {
        close(g_sockfd);
        g_sockfd = -1;
    }
    
    printf("[TimeSync] Cleanup complete\n");
}
