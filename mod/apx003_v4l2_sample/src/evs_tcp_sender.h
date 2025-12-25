/**
 * @file evs_tcp_sender.h
 * @brief EVS事件数据TCP发送器
 * @description 通过TCP Socket发送EVS事件数据到远程服务器
 */

#ifndef EVS_TCP_SENDER_H
#define EVS_TCP_SENDER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "evs_event_extractor.h"
#include "packet_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 配置参数
// ============================================================================

#define TCP_DEFAULT_PORT        8888
#define TCP_SEND_TIMEOUT_MS     1000
#define TCP_RECONNECT_DELAY_MS  3000
#define TCP_KEEPALIVE_INTERVAL  5

// ============================================================================
// TCP发送器结构
// ============================================================================

typedef struct {
    int socket_fd;                      // Socket文件描述符
    char server_ip[64];                 // 服务器IP地址
    int server_port;                    // 服务器端口
    bool connected;                     // 连接状态
    uint32_t sequence_num;              // 数据包序列号
    uint32_t device_id;                 // 设备ID
    
    // 统计信息
    SenderStats_t stats;
    
    // 缓冲区
    uint8_t* send_buffer;               // 发送缓冲区
    size_t send_buffer_size;            // 缓冲区大小
} EVSTCPSender_t;

// ============================================================================
// 函数声明
// ============================================================================

/**
 * @brief 创建TCP发送器
 * @param server_ip 服务器IP地址
 * @param server_port 服务器端口
 * @param device_id 设备ID
 * @return TCP发送器指针，失败返回NULL
 */
EVSTCPSender_t* evs_tcp_sender_create(
    const char* server_ip,
    int server_port,
    uint32_t device_id);

/**
 * @brief 销毁TCP发送器
 * @param sender 发送器指针
 */
void evs_tcp_sender_destroy(EVSTCPSender_t* sender);

/**
 * @brief 连接到服务器
 * @param sender 发送器指针
 * @return 0=成功，-1=失败
 */
int evs_tcp_sender_connect(EVSTCPSender_t* sender);

/**
 * @brief 断开连接
 * @param sender 发送器指针
 */
void evs_tcp_sender_disconnect(EVSTCPSender_t* sender);

/**
 * @brief 发送事件数据包（原始格式）
 * @param sender 发送器指针
 * @param packet 事件数据包
 * @return 发送的字节数，失败返回-1
 */
int evs_tcp_sender_send_events(
    EVSTCPSender_t* sender,
    const EVSEventPacket_t* packet);

/**
 * @brief 发送EVT2编码数据
 * @param sender 发送器指针
 * @param evt2_data EVT2编码数据
 * @param data_size 数据大小（字节）
 * @param event_count 原始事件数量
 * @return 发送的字节数，失败返回-1
 */
int evs_tcp_sender_send_evt2_data(
    EVSTCPSender_t* sender,
    const uint8_t* evt2_data,
    size_t data_size,
    uint32_t event_count);

/**
 * @brief 发送心跳包
 * @param sender 发送器指针
 * @return 0=成功，-1=失败
 */
int evs_tcp_sender_send_heartbeat(EVSTCPSender_t* sender);

/**
 * @brief 检查连接状态
 * @param sender 发送器指针
 * @return true=已连接，false=未连接
 */
bool evs_tcp_sender_is_connected(const EVSTCPSender_t* sender);

/**
 * @brief 获取统计信息
 * @param sender 发送器指针
 * @return 统计信息指针
 */
const SenderStats_t* evs_tcp_sender_get_stats(const EVSTCPSender_t* sender);

/**
 * @brief 打印统计信息
 * @param sender 发送器指针
 */
void evs_tcp_sender_print_stats(const EVSTCPSender_t* sender);

/**
 * @brief 重置统计信息
 * @param sender 发送器指针
 */
void evs_tcp_sender_reset_stats(EVSTCPSender_t* sender);

#ifdef __cplusplus
}
#endif

#endif // EVS_TCP_SENDER_H
