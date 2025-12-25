/**
 * @file packet_protocol.h
 * @brief 数据包协议定义 - 用于TCP传输和丢帧检测
 */

#ifndef PACKET_PROTOCOL_H
#define PACKET_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 协议常量
// ============================================================================

#define PACKET_MAGIC_NUMBER 0x44565331  // "DVS1"
#define PACKET_VERSION      0x01
#define MAX_PAYLOAD_SIZE    (8 * 1024 * 1024)  // 8MB最大负载（支持1M事件，每个事件4字节EVT2编码）

// ============================================================================
// 数据包类型
// ============================================================================

typedef enum {
    PACKET_TYPE_EVT2_DATA = 0x01,      // EVT2编码数据
    PACKET_TYPE_RAW_EVENTS = 0x02,     // 原始事件数据
    PACKET_TYPE_HEARTBEAT = 0x03,      // 心跳包
    PACKET_TYPE_ACK = 0x04,            // 确认包
    PACKET_TYPE_STATS = 0x05,          // 统计信息
    PACKET_TYPE_RAW_FRAME = 0x06,      // 原始相机帧数据（2MB全帧）
    PACKET_TYPE_RAW_SUBFRAME = 0x07,   // 原始相机子帧数据（512KB）
    PACKET_TYPE_TIME_SYNC_REQ = 0x10,  // 时间同步请求（服务器->设备）
    PACKET_TYPE_TIME_SYNC_RESP = 0x11, // 时间同步响应（设备->服务器）
    PACKET_TYPE_TIME_OFFSET = 0x12     // 时间偏移校准（服务器->设备）
} PacketType_t;

// ============================================================================
// 数据包头结构（32字节）
// ============================================================================

typedef struct __attribute__((packed)) {
    uint32_t magic;              // 魔数：0x44565331 ("DVS1")
    uint8_t  version;            // 协议版本
    uint8_t  packet_type;        // 数据包类型
    uint16_t flags;              // 标志位
    
    uint32_t sequence_num;       // 序列号（用于丢帧检测）
    uint32_t device_id;          // 设备ID（用于多设备时间同步）
    uint32_t timestamp_sec;      // 时间戳（秒）
    uint32_t timestamp_usec;     // 时间戳（微秒）
    
    uint32_t payload_size;       // 负载大小（字节）
    uint32_t event_count;        // 事件数量
    
    uint32_t checksum;           // CRC32校验和
    uint32_t reserved;           // 保留字段
} PacketHeader_t;

// ============================================================================
// 统计信息结构
// ============================================================================

typedef struct {
    uint32_t total_packets_sent;      // 总发送包数
    uint32_t total_events_sent;       // 总事件数
    uint64_t total_bytes_sent;        // 总字节数
    uint32_t window_duration_ms;      // 时间窗口长度
    uint32_t send_errors;             // 发送错误数
    uint32_t dropped_events;          // 丢弃的事件数（缓冲区溢出）
} SenderStats_t;

typedef struct {
    uint32_t total_packets_received;  // 总接收包数
    uint32_t total_events_received;   // 总事件数
    uint64_t total_bytes_received;    // 总字节数
    uint32_t packets_dropped;         // 丢失包数
    uint32_t sequence_errors;         // 序列错误数
    uint32_t checksum_errors;         // 校验错误数
} ReceiverStats_t;

// ============================================================================
// 函数声明
// ============================================================================

/**
 * @brief 计算CRC32校验和
 * @param data 数据指针
 * @param length 数据长度
 * @return CRC32值
 */
uint32_t calculate_crc32(const uint8_t *data, uint32_t length);

/**
 * @brief 初始化数据包头
 * @param header 头结构指针
 * @param packet_type 数据包类型
 * @param sequence_num 序列号
 * @param device_id 设备ID
 * @param payload_size 负载大小
 * @param event_count 事件数量
 */
void packet_header_init(PacketHeader_t *header, PacketType_t packet_type,
                       uint32_t sequence_num, uint32_t device_id,
                       uint32_t payload_size, uint32_t event_count);

/**
 * @brief 验证数据包头
 * @param header 头结构指针
 * @return 0=有效，-1=无效
 */
int packet_header_validate(const PacketHeader_t *header);

/**
 * @brief 计算数据包校验和
 * @param header 头结构指针
 * @param payload 负载数据指针
 * @return 校验和
 */
uint32_t packet_calculate_checksum(const PacketHeader_t *header, const uint8_t *payload);

// ============================================================================
// 时间同步数据结构
// ============================================================================

/**
 * @brief 时间同步请求/响应负载
 */
typedef struct __attribute__((packed)) {
    uint32_t device_id;              // 设备ID
    uint64_t request_timestamp_us;   // 请求时间戳（微秒）
    uint64_t response_timestamp_us;  // 响应时间戳（微秒）
    int64_t  server_offset_us;       // 服务器计算的偏移值（微秒）
} TimeSyncPayload_t;

#ifdef __cplusplus
}
#endif

#endif // PACKET_PROTOCOL_H

