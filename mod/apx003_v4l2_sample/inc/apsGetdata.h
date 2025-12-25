#ifndef __APSGETDATA_H
#define __APSGETDATA_H

struct buffer {
        void *start;
        size_t length;
        struct v4l2_buffer v4l2_buf;
        int fd;
};
typedef enum aemode {
    AEMODE_AUTO = 1,
    AEMODE_MANUAL = 2
} aemode;

typedef struct __apsParam {
    aemode aeMode;
    float aeGain;
    float aeExtime;
} apsParam;


extern apsParam gApsParam;
void writeApsPmram(apsParam * pParam);
void readApsPmram(apsParam * pParam);
#endif
