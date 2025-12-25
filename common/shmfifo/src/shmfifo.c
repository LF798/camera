#include "shmfifo.h"
 
typedef union semun{
    int val;
}semun;


void __attribute__ ((noinline)) neonMemCopy(unsigned char* src, unsigned char* dst, int num_bytes)
{
    // 检查内存对齐
    CHECK_ERROR(((uintptr_t)src % 16) != 0, "Source address is not 16-byte aligned");
    CHECK_ERROR(((uintptr_t)dst % 16) != 0, "Destination address is not 16-byte aligned");

    // 处理 64 字节倍数部分的数据
    int aligned_bytes = (num_bytes / 64) * 64;  // 对齐到 64 字节的部分

    if (aligned_bytes > 0) {
        asm volatile (
            "neoncopypld:\n"
            "       ld1         {v0.16b-v3.16b}, [%[src]], #64\n"  // 从 src 加载 64 字节到 v0-v3
            "       st1         {v0.16b-v3.16b}, [%[dst]], #64\n"  // 将 v0-v3 存储到 dst
            "       subs        %[aligned_bytes], %[aligned_bytes], #64\n"  // 更新剩余字节数
            "       bgt         neoncopypld\n"  // 如果还有字节需要复制，继续循环
            : [src] "+r" (src), [dst] "+r" (dst), [aligned_bytes] "+r" (aligned_bytes)
            :
            : "memory", "cc", "v0", "v1", "v2", "v3"
        );
    }

    // 处理剩余的不满 64 字节的数据
    int remaining_bytes = num_bytes % 64;
    if (remaining_bytes > 0) {
        memcpy(dst, src, remaining_bytes);
    }
}


static void P(int id)
{
    struct sembuf sb[1] = {0,-1, 0};
    semop(id, sb, 1);
}
 
static void V(int id)
{
    struct sembuf sb[1] = {0, 1, 0};
    semop(id, sb, 1);
}
 
shmfifo_t* shmfifo_init(key_t key, int blocks, int blksz)
{
    shmfifo_t *p = malloc(sizeof(shmfifo_t));
    CHECK_ERROR(p == NULL, "malloc failed");

    int shmid = shmget(key, 0, 0);
    int len = sizeof(shmhead_t) + blocks * blksz;

    if (shmid == -1) {  
        shmid = shmget(key, len, IPC_CREAT | 0644);
        CHECK_ERROR(shmid == -1, "shmget failed");

        p->p_head = shmat(shmid, NULL, 0);
        CHECK_ERROR(p->p_head == (void*)-1, "shmat failed");

        p->p_head->rd_idx = 0;
        p->p_head->wr_idx = 0;
        p->p_head->blocks = blocks;
        p->p_head->blksz = blksz;

        p->p_payload = (char*)(p->p_head + 1);
        p->shmid = shmid;

        p->sem_mutex = semget(key, 1, IPC_CREAT | 0644);
        CHECK_ERROR(p->sem_mutex == -1, "semget mutex failed");

        p->sem_empty = semget(key + 1, 1, IPC_CREAT | 0644);
        CHECK_ERROR(p->sem_empty == -1, "semget empty failed");

        p->sem_full = semget(key + 2, 1, IPC_CREAT | 0644);
        CHECK_ERROR(p->sem_full == -1, "semget full failed");

        semun su = {1}; 
        semctl(p->sem_mutex, 0, SETVAL, su);

        su.val = blocks; 
        semctl(p->sem_empty, 0, SETVAL, su);

        su.val = 0; 
        semctl(p->sem_full, 0, SETVAL, su);
    } else {
        p->p_head = shmat(shmid, NULL, 0);
        CHECK_ERROR(p->p_head == (void*)-1, "shmat failed");

        p->p_payload = (char*)(p->p_head + 1);
        p->shmid = shmid;

        p->sem_mutex = semget(key, 0, 0);
        CHECK_ERROR(p->sem_mutex == -1, "semget mutex failed");

        p->sem_empty = semget(key + 1, 0, 0);
        CHECK_ERROR(p->sem_empty == -1, "semget empty failed");

        p->sem_full = semget(key + 2, 0, 0);
        CHECK_ERROR(p->sem_full == -1, "semget full failed");
    }

    return p;
}
 
void shmfifo_put(shmfifo_t *fifo, const void *buf)
{
    P(fifo->sem_empty);
    P(fifo->sem_mutex);

    neonMemCopy(buf, fifo->p_payload + fifo->p_head->wr_idx * fifo->p_head->blksz, fifo->p_head->blksz);

    fifo->p_head->wr_idx = (fifo->p_head->wr_idx + 1) % fifo->p_head->blocks;

    V(fifo->sem_full);
    V(fifo->sem_mutex);
}

void shmfifo_get(shmfifo_t* fifo, void *buf)
{
    P(fifo->sem_full);
    P(fifo->sem_mutex);

    void* src = fifo->p_payload + fifo->p_head->rd_idx * fifo->p_head->blksz;
    CHECK_ERROR(((uintptr_t)src % 16) != 0, "Memory alignment issue for NEON");

    neonMemCopy(src, buf, fifo->p_head->blksz);
    // memcpy(buf, src, fifo->p_head->blksz);  // Use memcpy for testing

    fifo->p_head->rd_idx = (fifo->p_head->rd_idx + 1) % fifo->p_head->blocks;

    V(fifo->sem_empty);
    V(fifo->sem_mutex);
}


// 销毁
void shmfifo_destroy(shmfifo_t* pFifo)
{
    shmdt(pFifo->p_head);  //取消内存段挂载
    shmctl(pFifo->shmid, IPC_RMID, 0); //释放掉该内存段
    //删除信号量
    semctl(pFifo->sem_mutex, 0, IPC_RMID, 0);
    semctl(pFifo->sem_empty, 0, IPC_RMID, 0);
    semctl(pFifo->sem_full, 0, IPC_RMID, 0);
 
    free(pFifo);
}  