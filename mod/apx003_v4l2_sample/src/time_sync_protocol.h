/**
 * @file time_sync_protocol.h
 * @brief 独立时间同步协议定义
 * @details 定义客户端与服务器之间的时间同步通信协议（UDP）
 */

#ifndef TIME_SYNC_PROTOCOL_H
#define TIME_SYNC_PROTOCOL_H

#include <stdint.h>

// ============================================================================
// 协议常量
// ============================================================================

#define TIME_SYNC_PROTOCOL_VERSION  1
#define TIME_SYNC_DEFAULT_PORT      9999      ///< 默认UDP端口
#define TIME_SYNC_MAGIC             0x54535943UL  ///< "TSYNC"的16进制
#define TIME_SYNC_HEARTBEAT_INTERVAL_MS  1000  ///< 心跳间隔（毫秒）

// ============================================================================
// 消息类型
// ============================================================================

typedef enum {
    TIME_SYNC_MSG_HEARTBEAT = 1,      ///< 客户端心跳（含时间戳）
    TIME_SYNC_MSG_OFFSET_REPLY = 2,   ///< 服务器偏移应答
    TIME_SYNC_MSG_STATUS_REQ = 3,     ///< 客户端状态查询请求
    TIME_SYNC_MSG_STATUS_REPLY = 4    ///< 服务器状态应答
} TimeSyncMsgType_t;

// ============================================================================
// 消息结构
// ============================================================================

/**
 * @brief 通用消息头
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                   ///< 魔数（0x54535943）
    uint8_t  version;                 ///< 协议版本
    uint8_t  msg_type;                ///< 消息类型
    uint16_t reserved;                ///< 保留字段
    uint32_t device_id;               ///< 设备ID
} TimeSyncMsgHeader_t;

/**
 * @brief 客户端心跳消息（发送当前时间戳）
 */
typedef struct __attribute__((packed)) {
    TimeSyncMsgHeader_t header;
    uint64_t timestamp_us;            ///< 设备当前时间戳（微秒）
    uint32_t sequence;                ///< 序列号
} TimeSyncHeartbeatMsg_t;

/**
 * @brief 服务器偏移应答消息
 */
typedef struct __attribute__((packed)) {
    TimeSyncMsgHeader_t header;
    int64_t  offset_us;               ///< 推荐的时间偏移（微秒）
    uint32_t reference_device_id;     ///< 参考设备ID
    uint32_t sync_quality;            ///< 同步质量（0-100）
    uint32_t sequence;                ///< 对应的心跳序列号
} TimeSyncOffsetReplyMsg_t;

/**
 * @brief 服务器状态应答消息
 */
typedef struct __attribute__((packed)) {
    TimeSyncMsgHeader_t header;
    uint32_t total_devices;           ///< 总设备数
    uint32_t active_devices;          ///< 活跃设备数
    uint32_t reference_device_id;     ///< 参考设备ID
    int64_t  max_offset_us;           ///< 最大偏移
    int64_t  min_offset_us;           ///< 最小偏移
    int64_t  avg_offset_us;           ///< 平均偏移
} TimeSyncStatusReplyMsg_t;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 初始化消息头
 */
static inline void time_sync_init_header(TimeSyncMsgHeader_t *header, 
                                         TimeSyncMsgType_t msg_type,
                                         uint32_t device_id)
{
    header->magic = TIME_SYNC_MAGIC;
    header->version = TIME_SYNC_PROTOCOL_VERSION;
    header->msg_type = (uint8_t)msg_type;
    header->reserved = 0;
    header->device_id = device_id;
}

/**
 * @brief 验证消息头
 */
static inline int time_sync_validate_header(const TimeSyncMsgHeader_t *header)
{
    if (header->magic != TIME_SYNC_MAGIC) {
        return -1;  // 魔数错误
    }
    if (header->version != TIME_SYNC_PROTOCOL_VERSION) {
        return -2;  // 版本不匹配
    }
    return 0;
}

#endif // TIME_SYNC_PROTOCOL_H
