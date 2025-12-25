/***************************************************************
 * 中断信号USB传输集成头文件
 * 定义中断相关的数据结构和函数声明
 ***************************************************************/
#ifndef INTERRUPT_INTEGRATION_H
#define INTERRUPT_INTEGRATION_H

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

// 新增中断相关的USB请求码
#define REQUEST_INTERRUPT_EVENT    0x08  // 中断事件通知
#define REQUEST_GET_INTERRUPT_DATA 0x09  // 获取中断数据

// 中断事件数据结构（与驱动程序保持一致）
struct key_event_data {
    int event_type;         /* 事件类型 */
    uint64_t mono_time;     /* 单调时间戳(ktime_t格式) */
    uint64_t real_time;     /* 真实时间戳(ktime_t格式) */
};

// 转换后的中断事件数据结构（用于USB传输）
struct interrupt_event_usb {
    int event_type;         /* 事件类型 */
    uint64_t mono_time_ns;  /* 单调时间戳(纳秒) */
    uint64_t real_time_ns;  /* 真实时间戳(纳秒) */
    uint64_t usb_timestamp; /* USB传输时间戳 */
};

// 全局变量声明
extern struct interrupt_event_usb latest_interrupt_data;
extern pthread_mutex_t interrupt_mutex;
extern volatile bool interrupt_pending;

// 函数声明
int init_interrupt_usb_integration(void);
void handle_interrupt_setup_request(int ep0, struct usb_ctrlrequest *setup);
void *interrupt_monitor_thread(void *arg);
uint64_t ktime_to_ns(uint64_t ktime);
uint64_t get_current_timestamp_ns(void);

#endif // INTERRUPT_INTEGRATION_H