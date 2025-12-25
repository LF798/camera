/*
 * EVS Subframe Processor Header
 * 
 * 功能：通过V4L2缓冲池直接获取数据，并按照子帧处理流程解析事件数据
 */

#ifndef __EVS_SUBFRAME_PROCESSOR_H
#define __EVS_SUBFRAME_PROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 事件数据结构
typedef struct {
    unsigned short x;        // X坐标
    unsigned short y;        // Y坐标
    short polarity;          // 极性：0=off, 1=on
    uint64_t timestamp;      // 时间戳
} EventData;

// 事件回调函数类型
// events: 事件数组指针
// event_count: 事件数量
typedef void (*EventCallback)(const EventData *events, size_t event_count);

/**
 * 初始化EVS子帧处理器
 * 
 * @param dev_name: V4L2设备路径，如果为NULL则使用默认路径 "/dev/video1"
 * @param callback: 事件回调函数，处理完一帧数据后调用
 * @return: 0成功，-1失败
 */
int evs_subframe_init(const char* dev_name, EventCallback callback);

/**
 * 反初始化EVS子帧处理器
 * 释放所有资源
 */
void evs_subframe_deinit(void);

/**
 * 处理一帧数据
 * 从缓冲池获取数据并进行子帧处理，处理完成后会调用回调函数
 * 
 * @return: 0成功，-1失败（包括没有数据可用的情况）
 */
int evs_subframe_process_frame(void);

/**
 * 打印时间统计信息
 * 包括：
 *   - 子帧间隔（设备传输间隔）
 *   - 完整帧时间（从第一个子帧到最后一个子帧）
 *   - 单个子帧处理时间
 *   - 获取buffer时间
 *   - 完整帧处理时间
 */
void evs_subframe_print_statistics(void);

/**
 * 重置时间统计信息
 */
void evs_subframe_reset_statistics(void);

/**
 * 启用/禁用时间统计
 * @param enable: true启用，false禁用
 */
void evs_subframe_enable_statistics(bool enable);

#endif /* __EVS_SUBFRAME_PROCESSOR_H */

