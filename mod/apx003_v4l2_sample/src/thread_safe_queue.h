/**
 * @file thread_safe_queue.h
 * @brief 线程安全队列
 */

#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void** items;
    uint32_t capacity;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    
    bool shutdown;
} ThreadSafeQueue_t;

/**
 * @brief 创建线程安全队列
 */
ThreadSafeQueue_t* queue_create(uint32_t capacity);

/**
 * @brief 销毁队列
 */
void queue_destroy(ThreadSafeQueue_t* queue);

/**
 * @brief 入队（阻塞）
 */
int queue_push(ThreadSafeQueue_t* queue, void* item);

/**
 * @brief 出队（阻塞）
 */
void* queue_pop(ThreadSafeQueue_t* queue);

/**
 * @brief 尝试入队（非阻塞，带超时）
 */
int queue_try_push(ThreadSafeQueue_t* queue, void* item, uint32_t timeout_ms);

/**
 * @brief 尝试出队（非阻塞，带超时）
 */
void* queue_try_pop(ThreadSafeQueue_t* queue, uint32_t timeout_ms);

/**
 * @brief 获取队列大小
 */
uint32_t queue_size(ThreadSafeQueue_t* queue);

/**
 * @brief 获取队列大小（线程安全）
 */
uint32_t queue_size_get(ThreadSafeQueue_t* queue);

/**
 * @brief 检查队列是否为空
 */
bool queue_is_empty(ThreadSafeQueue_t* queue);

/**
 * @brief 检查队列是否已满
 */
bool queue_is_full(ThreadSafeQueue_t* queue);

/**
 * @brief 关闭队列
 */
void queue_shutdown(ThreadSafeQueue_t* queue);

#ifdef __cplusplus
}
#endif

#endif // THREAD_SAFE_QUEUE_H
