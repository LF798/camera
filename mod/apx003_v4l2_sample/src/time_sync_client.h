/**
 * @file time_sync_client.h
 * @brief 时间同步客户端 - 用于EVS设备端的时间同步
 * @details 支持NTP和PTP两种时间同步方式，确保多设备时间统一
 * @author Cursor AI
 * @date 2025-12-18
 */

#ifndef TIME_SYNC_CLIENT_H
#define TIME_SYNC_CLIENT_H

#include <stdint.h>

// ============================================================================
// 时间同步配置结构
// ============================================================================

/**
 * @brief 时间同步模式（简化版：仅保留NTP_ONCE）
 */
typedef enum {
    TIME_SYNC_MODE_NONE = 0,      ///< 不使用时间同步
    TIME_SYNC_MODE_NTP_ONCE = 1   ///< NTP初始同步一次 + 软件偏移（推荐）
} TimeSyncMode_t;

/**
 * @brief 时间同步配置
 */
typedef struct {
    uint32_t device_id;              ///< 设备ID（用于区分不同EVS设备）
    TimeSyncMode_t mode;             ///< 同步模式（推荐使用TIME_SYNC_MODE_NTP_ONCE）
    char ntp_server[64];             ///< NTP服务器地址（如 "192.168.1.100"）
    int64_t initial_offset_us;       ///< 初始时间偏移（微秒，通常为0）
    uint8_t enable_auto_adjust;      ///< 是否允许服务器自动调整偏移（1=允许，推荐）
} TimeSyncConfig_t;

/**
 * @brief 时间同步状态
 */
typedef struct {
    TimeSyncMode_t mode;             ///< 当前同步模式
    int64_t time_offset_us;          ///< 当前时间偏移（微秒）
    uint32_t sync_quality;           ///< 同步质量（0-100，100表示完美同步）
    uint32_t sync_count;             ///< 成功同步次数
    uint32_t sync_errors;            ///< 同步失败次数
    uint64_t last_sync_time_us;      ///< 上次同步时间（微秒）
    int64_t estimated_drift_us;      ///< 估计的时钟漂移（微秒/秒）
} TimeSyncStatus_t;

// ============================================================================
// API函数
// ============================================================================

/**
 * @brief 初始化时间同步客户端
 * @param config 同步配置
 * @return 0=成功，-1=失败
 */
int time_sync_init(const TimeSyncConfig_t *config);

/**
 * @brief 获取同步后的时间戳（微秒）
 * @return 校准后的时间戳（微秒，基于系统启动时间）
 * @note 这是最常用的函数，用于给事件打时间戳
 */
uint64_t time_sync_get_timestamp_us(void);

/**
 * @brief 获取当前时间偏移
 * @return 时间偏移（微秒）
 */
int64_t time_sync_get_offset_us(void);

/**
 * @brief 设置时间偏移（由服务器控制）
 * @param offset_us 时间偏移（微秒）
 * @note 只有在 enable_auto_adjust=1 时才生效
 */
void time_sync_set_offset_us(int64_t offset_us);

/**
 * @brief 获取同步质量
 * @return 0-100，100表示完美同步
 */
uint32_t time_sync_get_quality(void);

/**
 * @brief 获取时间同步状态
 * @param status 输出状态结构
 */
void time_sync_get_status(TimeSyncStatus_t *status);

/**
 * @brief 获取设备ID
 * @return 设备ID
 */
uint32_t time_sync_get_device_id(void);

/**
 * @brief 手动触发一次时间同步
 * @return 0=成功，-1=失败
 */
int time_sync_trigger_sync(void);

/**
 * @brief 清理时间同步
 */
void time_sync_cleanup(void);

/**
 * @brief 获取原始系统时间（未校准）
 * @return 系统时间戳（微秒）
 */
uint64_t time_sync_get_raw_timestamp_us(void);

/**
 * @brief 估计时钟漂移
 * @return 时钟漂移（微秒/秒）
 * @note 正值表示本地时钟快于参考时钟
 */
int64_t time_sync_estimate_drift(void);

#endif // TIME_SYNC_CLIENT_H

