/**
 * @file evs_tcp_receiver.c
 * @brief EVS事件数据TCP接收端示例
 * @description 接收来自EVS设备的事件数据，进行解析和统计
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "packet_protocol.h"
#include "evs_event_extractor.h"
#include "evt2_decoder.h"

// ============================================================================
// 配置参数
// ============================================================================

#define LISTEN_PORT         8888
#define MAX_CLIENTS         4
#define RECV_TIMEOUT_SEC    10
#define STATS_PRINT_INTERVAL 100

// ============================================================================
// 全局变量
// ============================================================================

static volatile bool g_running = true;
static ReceiverStats_t g_stats;

// ============================================================================
// 信号处理
// ============================================================================

static void signal_handler(int sig)
{
    printf("\n[Receiver] Received signal %d, shutting down...\n", sig);
    g_running = false;
}

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 接收完整数据（处理部分接收）
 */
static ssize_t recv_full(int sockfd, void* buffer, size_t len)
{
    size_t total_received = 0;
    uint8_t* ptr = (uint8_t*)buffer;
    
    while (total_received < len) {
        ssize_t received = recv(sockfd, ptr + total_received, 
                               len - total_received, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;  // 被信号中断，重试
            }
            perror("recv");
            return -1;
        } else if (received == 0) {
            fprintf(stderr, "[Receiver] Connection closed by peer\n");
            return -1;
        }
        total_received += received;
    }
    
    return total_received;
}

/**
 * @brief 处理接收到的事件数据包（原始格式）
 */
static int process_event_packet(const PacketHeader_t* header, const uint8_t* payload)
{
    uint32_t event_count = ntohl(header->event_count);
    uint32_t payload_size = ntohl(header->payload_size);
    uint32_t sequence_num = ntohl(header->sequence_num);
    uint32_t device_id = ntohl(header->device_id);
    
    // 验证数据大小
    if (payload_size != event_count * sizeof(EVSEvent_t)) {
        fprintf(stderr, "[Receiver] Warning: Payload size mismatch\n");
    }
    
    // 解析事件数据
    const EVSEvent_t* events = (const EVSEvent_t*)payload;
    
    // 简单统计：计算事件分布
    uint32_t positive_events = 0;
    uint32_t negative_events = 0;
    
    for (uint32_t i = 0; i < event_count; i++) {
        if (events[i].polarity > 0) {
            positive_events++;
        } else {
            negative_events++;
        }
    }
    
    printf("[RAW Packet #%u] Device=%u, Events=%u (Pos=%u, Neg=%u), Timestamp=%u.%06u\n",
           sequence_num, device_id, event_count,
           positive_events, negative_events,
           ntohl(header->timestamp_sec), ntohl(header->timestamp_usec));
    
    return 0;
}

/**
 * @brief 处理接收到的EVT2编码数据包
 */
static int process_evt2_packet(const PacketHeader_t* header, const uint8_t* payload,
                               EVT2Decoder_t* decoder)
{
    uint32_t event_count = ntohl(header->event_count);
    uint32_t payload_size = ntohl(header->payload_size);
    uint32_t sequence_num = ntohl(header->sequence_num);
    uint32_t device_id = ntohl(header->device_id);
    
    // 分配解码后的事件缓冲区
    EVSEvent_t* decoded_events = (EVSEvent_t*)malloc(event_count * sizeof(EVSEvent_t));
    if (!decoded_events) {
        fprintf(stderr, "[Receiver] Failed to allocate event buffer\n");
        return -1;
    }
    
    // 解码EVT2数据
    uint32_t actual_event_count = 0;
    int ret = evt2_decoder_decode(
        decoder,
        payload,
        payload_size,
        decoded_events,
        event_count,
        &actual_event_count
    );
    
    if (ret < 0) {
        fprintf(stderr, "[Receiver] Failed to decode EVT2 data\n");
        free(decoded_events);
        return -1;
    }
    
    // 统计解码后的事件
    uint32_t positive_events = 0;
    uint32_t negative_events = 0;
    
    for (uint32_t i = 0; i < actual_event_count; i++) {
        if (decoded_events[i].polarity > 0) {
            positive_events++;
        } else {
            negative_events++;
        }
    }
    
    double compression_ratio = 100.0 * (1.0 - (double)payload_size / (event_count * sizeof(EVSEvent_t)));
    
    printf("[EVT2 Packet #%u] Device=%u, Events=%u (Pos=%u, Neg=%u), EVT2=%u bytes (%.1f%% compression), Timestamp=%u.%06u\n",
           sequence_num, device_id, actual_event_count,
           positive_events, negative_events,
           payload_size, compression_ratio,
           ntohl(header->timestamp_sec), ntohl(header->timestamp_usec));
    
    free(decoded_events);
    return 0;
}

/**
 * @brief 处理客户端连接
 */
static void handle_client(int client_fd, struct sockaddr_in* client_addr)
{
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    printf("[Receiver] Client connected: %s:%d\n", 
           client_ip, ntohs(client_addr->sin_port));
    
    // 设置接收超时
    struct timeval timeout;
    timeout.tv_sec = RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 千兆网优化：设置接收缓冲区大小（4MB）
    int recvbuf_size = 4 * 1024 * 1024;
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &recvbuf_size, sizeof(recvbuf_size)) < 0) {
        perror("setsockopt SO_RCVBUF");
    }
    
    // 千兆网优化：设置TCP_NODELAY，减少延迟
    int flag = 1;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        perror("setsockopt TCP_NODELAY");
    }
    
    // 千兆网优化：设置TCP_QUICKACK，快速确认
    #ifdef TCP_QUICKACK
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) < 0) {
        // 忽略错误
    }
    #endif
    
    // 创建EVT2解码器
    EVT2Decoder_t* decoder = evt2_decoder_create();
    if (!decoder) {
        fprintf(stderr, "[Receiver] Failed to create EVT2 decoder\n");
        return;
    }
    
    uint32_t packet_count = 0;
    uint32_t expected_sequence = 0;
    
    while (g_running) {
        // 接收数据包头
        PacketHeader_t header;
        ssize_t received = recv_full(client_fd, &header, sizeof(header));
        if (received < 0) {
            break;
        }
        
        // 验证数据包头
        if (packet_header_validate(&header) < 0) {
            fprintf(stderr, "[Receiver] Invalid packet header\n");
            g_stats.checksum_errors++;
            continue;
        }
        
        uint32_t payload_size = ntohl(header.payload_size);
        uint32_t sequence_num = ntohl(header.sequence_num);
        PacketType_t packet_type = (PacketType_t)header.packet_type;
        
        // 检查序列号
        if (sequence_num != expected_sequence) {
            fprintf(stderr, "[Receiver] Sequence error: expected %u, got %u (lost %u packets)\n",
                   expected_sequence, sequence_num, sequence_num - expected_sequence);
            g_stats.sequence_errors++;
            g_stats.packets_dropped += (sequence_num - expected_sequence);
            expected_sequence = sequence_num;
        }
        expected_sequence++;
        
        // 接收负载数据
        uint8_t* payload = NULL;
        if (payload_size > 0) {
            payload = (uint8_t*)malloc(payload_size);
            if (!payload) {
                fprintf(stderr, "[Receiver] Failed to allocate payload buffer\n");
                break;
            }
            
            received = recv_full(client_fd, payload, payload_size);
            if (received < 0) {
                free(payload);
                break;
            }
        }
        
        // 验证校验和
        uint32_t calculated_checksum = packet_calculate_checksum(&header, payload);
        uint32_t received_checksum = ntohl(header.checksum);
        
        if (calculated_checksum != received_checksum) {
            fprintf(stderr, "[Receiver] Checksum error: expected 0x%08X, got 0x%08X\n",
                   calculated_checksum, received_checksum);
            g_stats.checksum_errors++;
            if (payload) free(payload);
            continue;
        }
        
        // 处理不同类型的数据包
        switch (packet_type) {
        case PACKET_TYPE_RAW_EVENTS:
            process_event_packet(&header, payload);
            g_stats.total_events_received += ntohl(header.event_count);
            break;
            
        case PACKET_TYPE_EVT2_DATA:
            process_evt2_packet(&header, payload, decoder);
            g_stats.total_events_received += ntohl(header.event_count);
            break;
            
        case PACKET_TYPE_HEARTBEAT:
            printf("[Receiver] Heartbeat received\n");
            break;
            
        default:
            printf("[Receiver] Unknown packet type: %d\n", packet_type);
            break;
        }
        
        if (payload) {
            free(payload);
        }
        
        // 更新统计
        g_stats.total_packets_received++;
        g_stats.total_bytes_received += sizeof(header) + payload_size;
        packet_count++;
        
        // 定期打印统计
        if (packet_count % STATS_PRINT_INTERVAL == 0) {
            printf("\n========== Receiver Statistics ==========\n");
            printf("Total Packets: %u\n", g_stats.total_packets_received);
            printf("Total Events: %u\n", g_stats.total_events_received);
            printf("Total Bytes: %lu (%.2f MB)\n",
                   g_stats.total_bytes_received,
                   g_stats.total_bytes_received / (1024.0 * 1024.0));
            printf("Packets Dropped: %u\n", g_stats.packets_dropped);
            printf("Sequence Errors: %u\n", g_stats.sequence_errors);
            printf("Checksum Errors: %u\n", g_stats.checksum_errors);
            printf("=========================================\n\n");
        }
    }
    
    // 打印解码器统计
    printf("\n[Receiver] EVT2 Decoder Statistics:\n");
    evt2_decoder_print_stats(decoder);
    
    // 清理解码器
    evt2_decoder_destroy(decoder);
    
    printf("[Receiver] Client disconnected: %s:%d\n", 
           client_ip, ntohs(client_addr->sin_port));
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[])
{
    int listen_port = LISTEN_PORT;
    
    // 解析命令行参数
    if (argc >= 2) {
        listen_port = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("EVS TCP Receiver\n");
    printf("Listening on port: %d\n", listen_port);
    printf("========================================\n\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化统计
    memset(&g_stats, 0, sizeof(g_stats));
    
    // 创建监听Socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    
    // 设置SO_REUSEADDR，允许快速重启
    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(listen_port);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    
    // 开始监听
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }
    
    printf("[Receiver] Listening on port %d...\n", listen_port);
    
    // 主循环：接受客户端连接
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;  // 被信号中断
            }
            perror("accept");
            break;
        }
        
        // 处理客户端（单线程版本，一次只处理一个客户端）
        handle_client(client_fd, &client_addr);
        close(client_fd);
    }
    
    // 清理
    close(listen_fd);
    
    // 打印最终统计
    printf("\n========== Final Statistics ==========\n");
    printf("Total Packets Received: %u\n", g_stats.total_packets_received);
    printf("Total Events Received: %u\n", g_stats.total_events_received);
    printf("Total Bytes Received: %lu (%.2f MB)\n",
           g_stats.total_bytes_received,
           g_stats.total_bytes_received / (1024.0 * 1024.0));
    printf("Packets Dropped: %u\n", g_stats.packets_dropped);
    printf("Sequence Errors: %u\n", g_stats.sequence_errors);
    printf("Checksum Errors: %u\n", g_stats.checksum_errors);
    printf("======================================\n");
    
    printf("[Receiver] Exit\n");
    return 0;
}
