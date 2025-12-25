#include "shmfifo.h"                                        
#include <unistd.h>
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


int main()
{
    gsf_frm_t head ;
    u_int32_t old_seq;

    // Initialize shared memory FIFO
    shmfifo_t *fifo = shmfifo_init(SHM_KEY, BLOCKS, sizeof(gsf_frm_t));
    if (!fifo) {
        fprintf(stderr, "Failed to initialize shared memory FIFO.\n");
        exit(EXIT_FAILURE);
    }

    // do{

    //         u_int32_t check = 0;
    //         memset(&head, 0x00, sizeof(head));
    //         shmfifo_get(fifo,&head);

    //         // for (int i = 0; i < head.size - YUV_OFFSET; i++){
    //         //     check += (u_int32_t)head.data[YUV_OFFSET + i];
    //         // }
    //         // if ((check != head.check) || ((old_seq+1) != head.seq)){
    //             fprintf(stdout, "[%d]check:%d,%d, seq:%d, len:%d, [%02x%02x%02x%02x%02x%02x]\r\n", old_seq, head.check, check, head.seq++, head.size, \
    //                 head.data[0], head.data[1], head.data[2], head.data[3], head.data[4], head.data[5]);

    //         // }
    //         old_seq = head.seq;

    // }while(1);

    while (1) {
        shmfifo_get(fifo, &head);
        printf("Data received: seq=%u, magic=%u\n", head.seq, head.magic);
        old_seq = head.seq;
    }

    shmfifo_destroy(fifo);
}