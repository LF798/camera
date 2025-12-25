#ifndef __SHMFIFO_H__
#define __SHMFIFO_H__
 
#include <sys/ipc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>  // 引入 uintptr_t 的定义


// 错误处理宏
#define CHECK_ERROR(condition, message) \
    do { \
        if (condition) { \
            perror(message); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)


typedef struct shmhead {
    int rd_idx; // 读入数据索引
    int wr_idx; // 写数据索引
    int blocks; // 存数据块数量
    int blksz;  // 每个数据块大小
}shmhead_t;
 
typedef struct shmfifo {
    shmhead_t *p_head;    // 共享内存的起始地址
    char *     p_payload; // 有效数据的起始地址
    int shmid;            // 打开的共享内存id
    int sem_mutex;        // 互斥量
    int sem_empty;        // 还剩多少个可以消费     
    int sem_full;         // 剩余多少个地方可以生产
}shmfifo_t;

void __attribute__ ((noinline)) neonMemCopy(unsigned char* src, unsigned char* dst, int num_bytes);
// 初始化函数
shmfifo_t *shmfifo_init(key_t key, int blocks, int blksz);
// 放入数据
void shmfifo_put(shmfifo_t *fifo, const void *buf);
// 取得数据
void shmfifo_get(shmfifo_t *fifo, void *buf);
// 结构销毁
void shmfifo_destroy(shmfifo_t *fifo);
 
#endif //__SHMFIFO_H__