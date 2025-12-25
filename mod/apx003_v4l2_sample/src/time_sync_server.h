/**
 * @file time_sync_server.h
 * @brief 时间同步服务器 - 用于接收端管理多设备时间同步
 * @details 监控各设备时间偏移，计算校准值，并下发给客户端
 * @author Cursor AI
 * @date 2025-12-18
 */

#ifndef TIME_SYNC_SERVER_H
#define TIME_SYNC_SERVER_H

#include <stdint.h>
#include <sys/time.h>

// ============================================================================
// 常量定义
// ============================================================================

#define MAX_SYNC_DEVICES 32            ///< 最大支持的设备数量
#define DEVICE_TIMEOUT_SEC 10          ///< 设备超时时间（秒）

// ============================================================================
// 数据结构
// ============================================================================

/**
 * @brief 设备时间信息
 */
typedef struct {
    uint32_t device_id;              ///< 设备ID
    uint64_t last_timestamp_us;      ///< 最后一次收到的时间戳（微秒）
    int64_t  calculated_offset_us;   ///< 计算出的时间偏移（微秒）
    uint32_t packet_count;           ///< 收到的数据包总数
    uint32_t sync_request_count;     ///< 发送的同步请求总数
    struct timeval last_seen;        ///< 最后一次收到数据的时间
    uint8_t is_active;               ///< 设备是否活跃（1=活跃，0=超时）
    uint8_t is_reference;            ///< 是否为参考设备（1=是，0=否）
} DeviceTimeInfo_t;

/**
 * @brief 时间同步服务器统计信息
 */
typedef struct {
    uint32_t total_devices;          ///< 设备总数
    uint32_t active_devices;         ///< 活跃设备数
    uint32_t reference_device_id;    ///< 参考设备ID（0=未设置）
    uint64_t server_start_time;      ///< 服务器启动时间（微秒）
    uint32_t sync_cycles;            ///< 同步周期计数
    int64_t max_offset_us;           ///< 最大时间偏移（微秒）
    int64_t min_offset_us;           ///< 最小时间偏移（微秒）
    int64_t avg_offset_us;           ///< 平均时间偏移（微秒）
} TimeSyncServerStats_t;

// ============================================================================
// API函数
// ============================================================================

/**
 * @brief 初始化时间同步服务器
 * @return 0=成功，-1=失败
 */
int time_sync_server_init(void);

/**
 * @brief 注册或更新设备时间信息
 * @param device_id 设备ID
 * @param timestamp_us 设备时间戳（微秒）
 * @note 每次收到设备数据包时调用此函数
 */
void time_sync_server_update_device(uint32_t device_id, uint64_t timestamp_us);

/**
 * @brief 设置参考设备
 * @param device_id 参考设备ID（其他设备将对齐到此设备）
 * @return 0=成功，-1=失败（设备不存在）
 * @note 如果不设置，将自动选择第一个设备作为参考
 */
int time_sync_server_set_reference_device(uint32_t device_id);

/**
 * @brief 计算各设备的时间偏移
 * @return 0=成功，-1=失败（无参考设备）
 * @note 建议周期性调用（如每秒一次）
 */
int time_sync_server_calculate_offsets(void);

/**
 * @brief 获取设备的推荐时间偏移
 * @param device_id 设备ID
 * @return 时间偏移（微秒），如果设备不存在返回0
 */
int64_t time_sync_server_get_device_offset(uint32_t device_id);

/**
 * @brief 获取设备信息
 * @param device_id 设备ID
 * @param info 输出设备信息
 * @return 0=成功，-1=失败（设备不存在）
 */
int time_sync_server_get_device_info(uint32_t device_id, DeviceTimeInfo_t *info);

/**
 * @brief 获取所有设备列表
 * @param devices 输出设备ID数组
 * @param max_count 数组最大容量
 * @return 实际设备数量
 */
int time_sync_server_get_device_list(uint32_t *devices, int max_count);

/**
 * @brief 检查并标记超时设备
 * @return 超时设备数量
 */
int time_sync_server_check_timeouts(void);

/**
 * @brief 获取服务器统计信息
 * @param stats 输出统计信息
 */
void time_sync_server_get_stats(TimeSyncServerStats_t *stats);

/**
 * @brief 打印所有设备的时间信息（调试用）
 */
void time_sync_server_print_status(void);

/**
 * @brief 清理时间同步服务器
 */
void time_sync_server_cleanup(void);

#endif // TIME_SYNC_SERVER_H

