#include "shmfifo.h"                                            
#include <time.h>


#define SHM_KEY 0x1234         /* Key for shared memory segment */

#define WHDTH 1280
#define HEIGHT 960

#define BLOCKS 3
#define BLKSZ WHDTH*HEIGHT

typedef struct {
    u_int32_t magic;
    u_int32_t check;
    u_int32_t seq;
    u_int32_t size;
    u_int8_t data[BLKSZ]; 
} __attribute__((aligned(16))) gsf_frm_t;


#define SLEEPTIME (50000)

int main() {
    // Initialize shared memory FIFO
    shmfifo_t *fifo = shmfifo_init(SHM_KEY, BLOCKS, sizeof(gsf_frm_t));
    if (!fifo) {
        fprintf(stderr, "Failed to initialize shared memory FIFO.\n");
        exit(EXIT_FAILURE);
    }

    gsf_frm_t head ;
    u_int32_t seq = 0;
    while(1){
        usleep(SLEEPTIME);
        struct timespec ts1, ts2;  
        clock_gettime(CLOCK_MONOTONIC, &ts1);
 
        head.magic = 0x30383938;
        head.size = BLKSZ;
        head.seq = seq++;
        head.check = 0;
        for (int i = 0; i < head.size; i++){
            head.data[i] = rand();
            head.check += (u_int32_t)head.data[i]; 
        }

        shmfifo_put(fifo, &head);
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        int cost = (ts2.tv_sec*1000 + ts2.tv_nsec/1000000) - (ts1.tv_sec*1000 + ts1.tv_nsec/1000000);
        // if(cost > 10)
            printf("cfifo_putt frame %u, frame size:%d put cost:%d ms\n", head.seq, head.size, cost);

    }
    // Destroy shared memory FIFO
    shmfifo_destroy(fifo);

    return 0;
}
