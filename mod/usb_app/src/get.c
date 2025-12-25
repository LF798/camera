#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#define APX_IMG_SHOW_WIDTH              (4096)
#define APX_IMG_SHOW_HEIGHT             (256)


int main() {
	
    int DVS_DATA_LEN = APX_IMG_SHOW_WIDTH * APX_IMG_SHOW_HEIGHT ;
    FILE *fp = NULL;
    fp = fopen("/tmp/cap.raw", "wb");
    if (fp == NULL) {
        printf("open  failed, error: %s", strerror(errno));
        exit(-1);
    }

    // 打开共享内存对象
    int shm_fd = shm_open("/dvsdatashm", O_CREAT | O_RDWR, 0777);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    
	sem_t *wait_aps_sem = sem_open("/wait_dvs_sem", O_CREAT|O_RDWR, 0666, 1);
    sem_t *send_done_sem = sem_open("/send_donedvs_sem", O_CREAT|O_RDWR, 0666, 0); 

    // 将共享内存映射到进程地址空间
    char* papsdata = mmap(0, DVS_DATA_LEN, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (papsdata == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // 从共享内存中读取数据
    for(int i = 0;i < 30;i++)
	{

		#define DIAG_TIMEOUT 3
		struct timeval now;
		struct timespec out_time;
		int seq = 0;
    	struct timespec ts1, ts2;  

		/*开始进行超时等待*/
        clock_gettime(CLOCK_MONOTONIC, &ts1);

		gettimeofday(&now, NULL);
		out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
		out_time.tv_nsec = now.tv_usec * 1000;
		if(sem_timedwait(wait_aps_sem,&out_time) <0 )
		{
			printf("======timeout wait_dvs_sem\n");
		}

        fwrite(papsdata,1, DVS_DATA_LEN, fp);
        fflush(fp);

        fprintf(stdout, "[%d] len:%d, [%02x%02x%02x%02x%02x%02x]\r\n", seq, DVS_DATA_LEN, \
            papsdata[0], papsdata[1], papsdata[2], papsdata[3], papsdata[4], papsdata[5]);
      clock_gettime(CLOCK_MONOTONIC, &ts2);
        int cost = (ts2.tv_sec*1000 + ts2.tv_nsec/1000000) - (ts1.tv_sec*1000 + ts1.tv_nsec/1000000);
        // if(cost > 10)
            printf("get frame %d, frame size:%d put cost:%d ms\n", seq, DVS_DATA_LEN, cost);

		seq++;

        sem_post(send_done_sem);

	}

    return 0;
}
