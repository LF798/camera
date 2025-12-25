/***************************************************************
 * 中断信号USB传输集成实现
 * 实现中断监听和USB传输功能
 ***************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usb/functionfs.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

#include "interrupt_integration.h"

// 全局变量定义
struct interrupt_event_usb latest_interrupt_data = {0};
pthread_mutex_t interrupt_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile bool interrupt_pending = false;

// 私有变量
static int interrupt_fd = -1;
static pthread_t interrupt_thread_id;
static bool interrupt_system_initialized = false;

/*
 * 将ktime_t格式转换为纳秒
 * ktime_t在内核中通常是64位纳秒值
 */
uint64_t ktime_to_ns(uint64_t ktime) {
    // 如果ktime已经是纳秒格式，直接返回
    // 否则需要根据具体的ktime_t实现进行转换
    return ktime;
}

/*
 * 获取当前时间戳（纳秒）
 */
uint64_t get_current_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * 中断监听线程
 * 监听中断驱动程序的事件并缓存最新数据
 */
void *interrupt_monitor_thread(void *arg) {
    struct key_event_data event_data;
    ssize_t ret;
    
    printf("[INTERRUPT] Monitor thread started\n");
    
    // 打开中断设备
    interrupt_fd = open("/dev/my_key", O_RDONLY);
    if (interrupt_fd < 0) {
        perror("[INTERRUPT] Failed to open interrupt device /dev/my_key");
        printf("[INTERRUPT] Please ensure the interrupt driver is loaded\n");
        return NULL;
    }
    
    printf("[INTERRUPT] Successfully opened /dev/my_key\n");
    
    while (1) {
        // 阻塞读取中断事件
        ret = read(interrupt_fd, &event_data, sizeof(event_data));
        if (ret == sizeof(event_data)) {
            // 转换时间戳格式并更新全局中断数据
            pthread_mutex_lock(&interrupt_mutex);
            
            latest_interrupt_data.event_type = event_data.event_type;
            latest_interrupt_data.mono_time_ns = ktime_to_ns(event_data.mono_time);
            latest_interrupt_data.real_time_ns = ktime_to_ns(event_data.real_time);
            latest_interrupt_data.usb_timestamp = get_current_timestamp_ns();
            interrupt_pending = true;
            
            pthread_mutex_unlock(&interrupt_mutex);
            
            printf("[INTERRUPT] Event detected: type=%d, mono_time=%llu ns, real_time=%llu ns\n",
                   event_data.event_type, 
                   latest_interrupt_data.mono_time_ns, 
                   latest_interrupt_data.real_time_ns);
        } else if (ret < 0) {
            if (errno == EINTR) {
                // 被信号中断，继续循环
                continue;
            }
            perror("[INTERRUPT] Error reading from interrupt device");
            break;
        } else {
            printf("[INTERRUPT] Warning: Partial read from interrupt device (got %zd bytes, expected %zu)\n", 
                   ret, sizeof(event_data));
        }
    }
    
    if (interrupt_fd >= 0) {
        close(interrupt_fd);
        interrupt_fd = -1;
    }
    printf("[INTERRUPT] Monitor thread exiting\n");
    return NULL;
}

/*
 * 处理中断相关的USB控制请求
 * 集成到现有的handle_setup_request函数中
 */
void handle_interrupt_setup_request(int ep0, struct usb_ctrlrequest *setup) {
    uint8_t response_buffer[64];
    int ret;
    
    switch (setup->bRequest) {
        case REQUEST_INTERRUPT_EVENT:
            // 检查是否有待处理的中断事件
            pthread_mutex_lock(&interrupt_mutex);
            if (interrupt_pending) {
                response_buffer[0] = 1;  // 有中断事件
                printf("[INTERRUPT] USB: Interrupt event query - event pending\n");
            } else {
                response_buffer[0] = 0;  // 无中断事件
            }
            pthread_mutex_unlock(&interrupt_mutex);
            
            // 发送响应
            ret = write(ep0, response_buffer, 1);
            if (ret < 0) {
                perror("[INTERRUPT] USB: Failed to send interrupt event status");
            }
            break;
            
        case REQUEST_GET_INTERRUPT_DATA:
            // 发送最新的中断数据
            pthread_mutex_lock(&interrupt_mutex);
            memcpy(response_buffer, &latest_interrupt_data, sizeof(latest_interrupt_data));
            if (interrupt_pending) {
                interrupt_pending = false;  // 清除标志
                printf("[INTERRUPT] USB: Sending interrupt data - type=%d, mono_time=%llu ns\n",
                       latest_interrupt_data.event_type, latest_interrupt_data.mono_time_ns);
            }
            pthread_mutex_unlock(&interrupt_mutex);
            
            ret = write(ep0, response_buffer, sizeof(latest_interrupt_data));
            if (ret < 0) {
                perror("[INTERRUPT] USB: Failed to send interrupt data");
            } else {
                printf("[INTERRUPT] USB: Successfully sent %d bytes of interrupt data\n", ret);
            }
            break;
            
        default:
            printf("[INTERRUPT] USB: Unknown interrupt request 0x%02x\n", setup->bRequest);
            break;
    }
}

/*
 * 初始化中断USB集成
 */
int init_interrupt_usb_integration(void) {
    int ret;
    
    if (interrupt_system_initialized) {
        printf("[INTERRUPT] System already initialized\n");
        return 0;
    }
    
    // 初始化互斥锁
    ret = pthread_mutex_init(&interrupt_mutex, NULL);
    if (ret != 0) {
        perror("[INTERRUPT] Failed to initialize mutex");
        return -1;
    }
    
    // 创建中断监听线程
    ret = pthread_create(&interrupt_thread_id, NULL, interrupt_monitor_thread, NULL);
    if (ret != 0) {
        perror("[INTERRUPT] Failed to create interrupt monitor thread");
        pthread_mutex_destroy(&interrupt_mutex);
        return -1;
    }
    
    // 设置线程为分离状态
    pthread_detach(interrupt_thread_id);
    
    interrupt_system_initialized = true;
    printf("[INTERRUPT] USB integration initialized successfully\n");
    return 0;
}

/*
 * 清理中断USB集成
 */
void cleanup_interrupt_usb_integration(void) {
    if (!interrupt_system_initialized) {
        return;
    }
    
    // 取消线程
    if (interrupt_thread_id != 0) {
        pthread_cancel(interrupt_thread_id);
        interrupt_thread_id = 0;
    }
    
    // 关闭设备文件
    if (interrupt_fd >= 0) {
        close(interrupt_fd);
        interrupt_fd = -1;
    }
    
    // 销毁互斥锁
    pthread_mutex_destroy(&interrupt_mutex);
    
    interrupt_system_initialized = false;
    printf("[INTERRUPT] USB integration cleaned up\n");
}