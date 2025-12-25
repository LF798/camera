/**
 * @file evs_event_extractor.h
 * @brief EVS原始数据事件提取器 - 从4096x512原始数据中提取事件
 * @description 解析EVS相机专有格式，提取4个子帧并合并为完整事件流
 */

#ifndef EVS_EVENT_EXTRACTOR_H
#define EVS_EVENT_EXTRACTOR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// EVS硬件参数
// ============================================================================

#define EVS_RAW_WIDTH           4096        // 原始数据宽度
#define EVS_RAW_HEIGHT          256         // 原始数据高度
#define EVS_RAW_DATA_SIZE       (EVS_RAW_WIDTH * EVS_RAW_HEIGHT)  // 1MB

#define EVS_OUTPUT_WIDTH        768         // 输出事件宽度
#define EVS_OUTPUT_HEIGHT       608         // 输出事件高度

#define EVS_SUB_WIDTH           384         // 子帧宽度
#define EVS_SUB_HEIGHT          304         // 子帧高度

#define HV_SUB_FULL_BYTE_SIZE   32768       // 子帧完整字节数 (32KB)
#define HV_SUB_VALID_BYTE_SIZE  29184       // 子帧有效字节数 (384×304÷4)

// ============================================================================
// 事件数据结构
// ============================================================================

/**
 * @brief 单个事件结构
 */
typedef struct __attribute__((packed)) {
    uint16_t x;              // X坐标 (0-767)
    uint16_t y;              // Y坐标 (0-607)
    int8_t   polarity;       // 极性 (0=负, 1=正)
    uint8_t  reserved;       // 对齐保留字节
    uint64_t timestamp;      // 时间戳 (微秒)
} EVSEvent_t;

/**
 * @brief 事件数据包
 */
typedef struct {
    uint32_t event_count;           // 事件数量
    uint64_t frame_timestamp;       // 帧时间戳
    EVSEvent_t* events;             // 事件数组指针
} EVSEventPacket_t;

/**
 * @brief 事件提取器统计
 */
typedef struct {
    uint64_t total_frames_processed;    // 处理的总帧数
    uint64_t total_events_extracted;    // 提取的总事件数
    uint32_t last_event_count;          // 上一帧事件数
    uint32_t max_events_per_frame;      // 单帧最大事件数
    uint32_t min_events_per_frame;      // 单帧最小事件数
} EVSExtractorStats_t;

// ============================================================================
// 函数声明
// ============================================================================

/**
 * @brief 从EVS原始数据中提取事件
 * @param raw_data 原始数据指针 (4096x512 = 2MB)
 * @param raw_data_size 原始数据大小
 * @param packet 输出事件数据包指针
 * @param max_events 最大事件数限制
 * @return 提取的事件数量，失败返回-1
 */
int evs_extract_events(
    const uint8_t* raw_data,
    size_t raw_data_size,
    EVSEventPacket_t* packet,
    uint32_t max_events);

/**
 * @brief 直接提取单个子帧的事件到目标缓冲区（零拷贝）
 * @param subframe_data 子帧原始数据指针
 * @param subframe_id 子帧ID（0-3）用于计算坐标偏移
 * @param events 目标事件数组
 * @param current_count 当前事件数（输入/输出）
 * @param max_events 最大事件数
 * @param dropped_count 丢失的事件数（输出，可选）
 * @return 提取的事件数，-1表示失败
 */
int evs_extract_subframe_direct(
    const uint8_t* subframe_data,
    int subframe_id,
    EVSEvent_t* events,
    uint32_t* current_count,
    uint32_t max_events,
    uint32_t* dropped_count);

/**
 * @brief 创建事件数据包
 * @param max_events 最大事件数
 * @return 事件数据包指针，失败返回NULL
 */
EVSEventPacket_t* evs_event_packet_create(uint32_t max_events);

/**
 * @brief 销毁事件数据包
 * @param packet 事件数据包指针
 */
void evs_event_packet_destroy(EVSEventPacket_t* packet);

/**
 * @brief 重置事件数据包（清空但保留内存）
 * @param packet 事件数据包指针
 */
void evs_event_packet_reset(EVSEventPacket_t* packet);

/**
 * @brief 初始化统计信息
 * @param stats 统计信息指针
 */
void evs_extractor_stats_init(EVSExtractorStats_t* stats);

/**
 * @brief 更新统计信息
 * @param stats 统计信息指针
 * @param event_count 本次提取的事件数
 */
void evs_extractor_stats_update(EVSExtractorStats_t* stats, uint32_t event_count);

/**
 * @brief 打印统计信息
 * @param stats 统计信息指针
 */
void evs_extractor_stats_print(const EVSExtractorStats_t* stats);

#ifdef __cplusplus
}
#endif

#endif // EVS_EVENT_EXTRACTOR_H
