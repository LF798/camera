/**
 * @file thread_safe_queue.c
 * @brief 线程安全队列实现
 */

#include "thread_safe_queue.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

ThreadSafeQueue_t* queue_create(uint32_t capacity)
{
    ThreadSafeQueue_t* queue = (ThreadSafeQueue_t*)malloc(sizeof(ThreadSafeQueue_t));
    if (!queue) return NULL;
    
    queue->items = (void**)calloc(capacity, sizeof(void*));
    if (!queue->items) {
        free(queue);
        return NULL;
    }
    
    queue->capacity = capacity;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    
    return queue;
}

void queue_destroy(ThreadSafeQueue_t* queue)
{
    if (!queue) return;
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    
    free(queue->items);
    free(queue);
}

int queue_push(ThreadSafeQueue_t* queue, void* item)
{
    if (!queue || !item) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size >= queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

void* queue_pop(ThreadSafeQueue_t* queue)
{
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    void* item = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return item;
}

int queue_try_push(ThreadSafeQueue_t* queue, void* item, uint32_t timeout_ms)
{
    if (!queue || !item) return -1;
    
    struct timespec ts;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    ts.tv_sec = now.tv_sec + timeout_ms / 1000;
    ts.tv_nsec = (now.tv_usec + (timeout_ms % 1000) * 1000) * 1000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size >= queue->capacity && !queue->shutdown) {
        int ret = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &ts);
        if (ret != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

void* queue_try_pop(ThreadSafeQueue_t* queue, uint32_t timeout_ms)
{
    if (!queue) return NULL;
    
    struct timespec ts;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    ts.tv_sec = now.tv_sec + timeout_ms / 1000;
    ts.tv_nsec = (now.tv_usec + (timeout_ms % 1000) * 1000) * 1000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->size == 0 && !queue->shutdown) {
        int ret = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
        if (ret != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
    }
    
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    void* item = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return item;
}

uint32_t queue_size(ThreadSafeQueue_t* queue)
{
    if (!queue) return 0;
    return queue->size;
}

uint32_t queue_size_get(ThreadSafeQueue_t* queue)
{
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    uint32_t size = queue->size;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

bool queue_is_empty(ThreadSafeQueue_t* queue)
{
    return queue_size(queue) == 0;
}

bool queue_is_full(ThreadSafeQueue_t* queue)
{
    if (!queue) return false;
    
    pthread_mutex_lock(&queue->mutex);
    bool full = (queue->size >= queue->capacity);
    pthread_mutex_unlock(&queue->mutex);
    
    return full;
}

void queue_shutdown(ThreadSafeQueue_t* queue)
{
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}
