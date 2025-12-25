/**
 * @file evs_tcp_sender.c
 * @brief EVS事件数据TCP发送器实现
 */

#include "evs_tcp_sender.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 设置Socket选项
 */
static int set_socket_options(int sockfd)
{
    int flag = 1;
    
    // 设置TCP_NODELAY，禁用Nagle算法，减少延迟
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        perror("setsockopt TCP_NODELAY");
        return -1;
    }
    
    // 设置SO_KEEPALIVE，保持连接活跃
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) < 0) {
        perror("setsockopt SO_KEEPALIVE");
        return -1;
    }
    
    // 设置发送超时
    struct timeval timeout;
    timeout.tv_sec = TCP_SEND_TIMEOUT_MS / 1000;
    timeout.tv_usec = (TCP_SEND_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_SNDTIMEO");
        return -1;
    }
    
    // 设置发送缓冲区大小（千兆网优化：4MB）
    int sendbuf_size = 4 * 1024 * 1024;  // 4MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, sizeof(sendbuf_size)) < 0) {
        perror("setsockopt SO_SNDBUF");
    }
    
    // 千兆网优化：设置TCP窗口缩放
    // 注意：需要在connect之前设置
    int window_scale = 7;  // 窗口缩放因子（2^7 = 128倍）
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &window_scale, sizeof(window_scale)) < 0) {
        // 某些系统可能不支持，忽略错误
    }
    
    // 千兆网优化：启用TCP快速打开（如果内核支持）
    #ifdef TCP_FASTOPEN
    int qlen = 5;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) < 0) {
        // 不是所有系统都支持，忽略错误
    }
    #endif
    
    // 千兆网优化：设置TCP_QUICKACK，快速确认
    #ifdef TCP_QUICKACK
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) < 0) {
        // 忽略错误
    }
    #endif
    
    return 0;
}

/**
 * @brief 发送完整数据（处理部分发送）
 */
static ssize_t send_full(int sockfd, const void* data, size_t len)
{
    size_t total_sent = 0;
    const uint8_t* ptr = (const uint8_t*)data;
    
    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;  // 被信号中断，重试
            }
            perror("send");
            return -1;
        } else if (sent == 0) {
            fprintf(stderr, "[TCP Sender] Connection closed by peer\n");
            return -1;
        }
        total_sent += sent;
    }
    
    return total_sent;
}

// ============================================================================
// 公共函数实现
// ============================================================================

EVSTCPSender_t* evs_tcp_sender_create(
    const char* server_ip,
    int server_port,
    uint32_t device_id)
{
    if (!server_ip) {
        fprintf(stderr, "[TCP Sender] Error: Invalid server IP\n");
        return NULL;
    }
    
    EVSTCPSender_t* sender = (EVSTCPSender_t*)malloc(sizeof(EVSTCPSender_t));
    if (!sender) {
        fprintf(stderr, "[TCP Sender] Error: Failed to allocate sender\n");
        return NULL;
    }
    
    memset(sender, 0, sizeof(EVSTCPSender_t));
    
    // 初始化参数
    strncpy(sender->server_ip, server_ip, sizeof(sender->server_ip) - 1);
    sender->server_port = server_port;
    sender->device_id = device_id;
    sender->socket_fd = -1;
    sender->connected = false;
    sender->sequence_num = 0;
    
    // 分配发送缓冲区（足够容纳头部+最大负载）
    sender->send_buffer_size = sizeof(PacketHeader_t) + MAX_PAYLOAD_SIZE;
    sender->send_buffer = (uint8_t*)malloc(sender->send_buffer_size);
    if (!sender->send_buffer) {
        fprintf(stderr, "[TCP Sender] Error: Failed to allocate send buffer\n");
        free(sender);
        return NULL;
    }
    
    printf("[TCP Sender] Created: server=%s:%d, device_id=%u\n",
           server_ip, server_port, device_id);
    
    return sender;
}

void evs_tcp_sender_destroy(EVSTCPSender_t* sender)
{
    if (!sender) return;
    
    evs_tcp_sender_disconnect(sender);
    
    if (sender->send_buffer) {
        free(sender->send_buffer);
    }
    
    free(sender);
    printf("[TCP Sender] Destroyed\n");
}

int evs_tcp_sender_connect(EVSTCPSender_t* sender)
{
    if (!sender) return -1;
    
    if (sender->connected) {
        printf("[TCP Sender] Already connected\n");
        return 0;
    }
    
    // 创建Socket
    sender->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sender->socket_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // 设置Socket选项
    if (set_socket_options(sender->socket_fd) < 0) {
        close(sender->socket_fd);
        sender->socket_fd = -1;
        return -1;
    }
    
    // 连接到服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sender->server_port);
    
    if (inet_pton(AF_INET, sender->server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[TCP Sender] Error: Invalid IP address %s\n", sender->server_ip);
        close(sender->socket_fd);
        sender->socket_fd = -1;
        return -1;
    }
    
    printf("[TCP Sender] Connecting to %s:%d...\n", sender->server_ip, sender->server_port);
    
    if (connect(sender->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sender->socket_fd);
        sender->socket_fd = -1;
        return -1;
    }
    
    sender->connected = true;
    printf("[TCP Sender] Connected successfully\n");
    
    return 0;
}

void evs_tcp_sender_disconnect(EVSTCPSender_t* sender)
{
    if (!sender) return;
    
    if (sender->socket_fd >= 0) {
        close(sender->socket_fd);
        sender->socket_fd = -1;
    }
    
    sender->connected = false;
    printf("[TCP Sender] Disconnected\n");
}

int evs_tcp_sender_send_events(
    EVSTCPSender_t* sender,
    const EVSEventPacket_t* packet)
{
    if (!sender || !packet || !sender->connected) {
        return -1;
    }
    
    if (packet->event_count == 0) {
        return 0;  // 没有事件，不发送
    }
    
    // 计算负载大小
    uint32_t payload_size = packet->event_count * sizeof(EVSEvent_t);
    
    if (payload_size > MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "[TCP Sender] Error: Payload too large %u > %u\n",
                payload_size, MAX_PAYLOAD_SIZE);
        sender->stats.dropped_events += packet->event_count;
        return -1;
    }
    
    // 构建数据包头
    PacketHeader_t* header = (PacketHeader_t*)sender->send_buffer;
    packet_header_init(header, PACKET_TYPE_RAW_EVENTS,
                      sender->sequence_num++, sender->device_id,
                      payload_size, packet->event_count);
    
    // 设置时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    header->timestamp_sec = tv.tv_sec;
    header->timestamp_usec = tv.tv_usec;
    
    // 复制事件数据到发送缓冲区
    uint8_t* payload_ptr = sender->send_buffer + sizeof(PacketHeader_t);
    memcpy(payload_ptr, packet->events, payload_size);
    
    // 计算校验和并转换为网络字节序
    uint32_t checksum = packet_calculate_checksum(header, payload_ptr);
    header->checksum = htonl(checksum);
    
    // 发送数据包（头部+负载）
    size_t total_size = sizeof(PacketHeader_t) + payload_size;
    ssize_t sent = send_full(sender->socket_fd, sender->send_buffer, total_size);
    
    if (sent < 0) {
        fprintf(stderr, "[TCP Sender] Error: Failed to send packet\n");
        sender->stats.send_errors++;
        sender->connected = false;
        return -1;
    }
    
    // 更新统计
    sender->stats.total_packets_sent++;
    sender->stats.total_events_sent += packet->event_count;
    sender->stats.total_bytes_sent += sent;
    
    return sent;
}

int evs_tcp_sender_send_evt2_data(
    EVSTCPSender_t* sender,
    const uint8_t* evt2_data,
    size_t data_size,
    uint32_t event_count)
{
    if (!sender || !evt2_data || !sender->connected) {
        return -1;
    }
    
    if (data_size == 0) {
        return 0;
    }
    
    if (data_size > MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "[TCP Sender] Error: EVT2 payload too large %zu > %u\n",
                data_size, MAX_PAYLOAD_SIZE);
        sender->stats.dropped_events += event_count;
        return -1;
    }
    
    // 构建数据包头（使用EVT2数据类型）
    PacketHeader_t* header = (PacketHeader_t*)sender->send_buffer;
    packet_header_init(header, PACKET_TYPE_EVT2_DATA,
                      sender->sequence_num++, sender->device_id,
                      data_size, event_count);
    
    // 设置时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    header->timestamp_sec = tv.tv_sec;
    header->timestamp_usec = tv.tv_usec;
    
    // 复制EVT2数据到发送缓冲区
    uint8_t* payload_ptr = sender->send_buffer + sizeof(PacketHeader_t);
    memcpy(payload_ptr, evt2_data, data_size);
    
    // 计算校验和并转换为网络字节序
    uint32_t checksum = packet_calculate_checksum(header, payload_ptr);
    header->checksum = htonl(checksum);
    
    // 发送数据包（头部+负载）
    size_t total_size = sizeof(PacketHeader_t) + data_size;
    ssize_t sent = send_full(sender->socket_fd, sender->send_buffer, total_size);
    
    if (sent < 0) {
        fprintf(stderr, "[TCP Sender] Error: Failed to send EVT2 packet\n");
        sender->stats.send_errors++;
        sender->connected = false;
        return -1;
    }
    
    // 更新统计
    sender->stats.total_packets_sent++;
    sender->stats.total_events_sent += event_count;
    sender->stats.total_bytes_sent += sent;
    
    return sent;
}

int evs_tcp_sender_send_heartbeat(EVSTCPSender_t* sender)
{
    if (!sender || !sender->connected) {
        return -1;
    }
    
    // 构建心跳包
    PacketHeader_t header;
    packet_header_init(&header, PACKET_TYPE_HEARTBEAT,
                      sender->sequence_num++, sender->device_id,
                      0, 0);
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    header.timestamp_sec = tv.tv_sec;
    header.timestamp_usec = tv.tv_usec;
    header.checksum = packet_calculate_checksum(&header, NULL);
    
    ssize_t sent = send_full(sender->socket_fd, &header, sizeof(header));
    if (sent < 0) {
        sender->connected = false;
        return -1;
    }
    
    return 0;
}

bool evs_tcp_sender_is_connected(const EVSTCPSender_t* sender)
{
    return sender ? sender->connected : false;
}

const SenderStats_t* evs_tcp_sender_get_stats(const EVSTCPSender_t* sender)
{
    return sender ? &sender->stats : NULL;
}

void evs_tcp_sender_print_stats(const EVSTCPSender_t* sender)
{
    if (!sender) return;
    
    const SenderStats_t* stats = &sender->stats;
    
    printf("\n========== TCP Sender Statistics ==========\n");
    printf("Total Packets Sent: %u\n", stats->total_packets_sent);
    printf("Total Events Sent: %u\n", stats->total_events_sent);
    printf("Total Bytes Sent: %lu (%.2f MB)\n",
           stats->total_bytes_sent,
           stats->total_bytes_sent / (1024.0 * 1024.0));
    printf("Send Errors: %u\n", stats->send_errors);
    printf("Dropped Events: %u\n", stats->dropped_events);
    
    if (stats->total_packets_sent > 0) {
        double avg_events = (double)stats->total_events_sent / stats->total_packets_sent;
        double avg_bytes = (double)stats->total_bytes_sent / stats->total_packets_sent;
        printf("Avg Events/Packet: %.2f\n", avg_events);
        printf("Avg Bytes/Packet: %.2f\n", avg_bytes);
    }
    
    printf("===========================================\n\n");
}

void evs_tcp_sender_reset_stats(EVSTCPSender_t* sender)
{
    if (sender) {
        memset(&sender->stats, 0, sizeof(SenderStats_t));
    }
}
