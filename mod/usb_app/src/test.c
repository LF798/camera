#if 1 
/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#define _BSD_SOURCE /* for endian.h */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/eventfd.h>

#include "libaio.h"
#define IOCB_FLAG_RESFD         (1 << 0)

#include <linux/usb/functionfs.h>

#define BUF_LEN		8192

/******************** Descriptors and Strings *******************************/

static const struct {
	struct usb_functionfs_descs_head_v2 header;
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	__le32 os_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio bulk_sink;
		struct usb_endpoint_descriptor_no_audio bulk_source;
	} __attribute__ ((__packed__)) fs_descs, hs_descs;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio sink;
		struct usb_ss_ep_comp_descriptor sink_comp;
		struct usb_endpoint_descriptor_no_audio source;
		struct usb_ss_ep_comp_descriptor source_comp;
	} __attribute__ ((__packed__)) ss_descs;
	struct usb_os_desc_header os_header;
	struct usb_ext_compat_desc os_desc;

} __attribute__ ((__packed__)) descriptors = {
	.header = {
		.magic = (FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = (FUNCTIONFS_HAS_FS_DESC |
				 FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC |
				 FUNCTIONFS_HAS_MS_OS_DESC),
		.length = (sizeof(descriptors)),
	},
	.fs_count = (3),
	.fs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.fs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.fs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_OUT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.fs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
	},
	.hs_count = (3),
	.hs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.hs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.hs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_OUT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.hs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
	},
	.ss_count = (5),
	.ss_descs = {
		.intf = {
			.bLength = sizeof(descriptors.ss_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bInterfaceNumber = 0,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.sink = {
			.bLength = sizeof(descriptors.ss_descs.sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_OUT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (1024),
		},
		.sink_comp = {
			.bLength = sizeof(descriptors.ss_descs.sink_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
		.source = {
			.bLength = sizeof(descriptors.ss_descs.source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (1024),
		},
		.source_comp = {
			.bLength = sizeof(descriptors.ss_descs.source_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
	},
	.os_count = (1),
	.os_header = {
		.interface = (1),
		.dwLength = (sizeof(descriptors.os_header) +
			    sizeof(descriptors.os_desc)),
		.bcdVersion = (1),
		.wIndex = (4),
		.bCount = (1),
		.Reserved = (0),
	},
	.os_desc = {
		.bFirstInterfaceNumber = 0,
		.Reserved1 = (1),
		.CompatibleID = {0},
		.SubCompatibleID = {0},
		.Reserved2 = {0},
	},
};

#define STR_INTERFACE "AIO Test"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE)];
	} __attribute__ ((__packed__)) lang0;
} __attribute__ ((__packed__)) strings = {
	.header = {
		.magic = (FUNCTIONFS_STRINGS_MAGIC),
		.length = (sizeof(strings)),
		.str_count = (1),
		.lang_count = (1),
	},
	.lang0 = {
		(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

/******************** Endpoints handling *******************************/

static void display_event(struct usb_functionfs_event *event)
{
	static const char *const names[] = {
		[FUNCTIONFS_BIND] = "BIND",
		[FUNCTIONFS_UNBIND] = "UNBIND",
		[FUNCTIONFS_ENABLE] = "ENABLE",
		[FUNCTIONFS_DISABLE] = "DISABLE",
		[FUNCTIONFS_SETUP] = "SETUP",
		[FUNCTIONFS_SUSPEND] = "SUSPEND",
		[FUNCTIONFS_RESUME] = "RESUME",
	};
	switch (event->type) {
	case FUNCTIONFS_BIND:
	case FUNCTIONFS_UNBIND:
	case FUNCTIONFS_ENABLE:
	case FUNCTIONFS_DISABLE:
	case FUNCTIONFS_SETUP:
	case FUNCTIONFS_SUSPEND:
	case FUNCTIONFS_RESUME:
		printf("Event %s\n", names[event->type]);
	}
}

static void handle_ep0(int ep0, bool *ready)
{
	struct usb_functionfs_event event;
	int ret;

	struct pollfd pfds[1];
	pfds[0].fd = ep0;
	pfds[0].events = POLLIN;

	ret = poll(pfds, 1, 0);

	if (ret && (pfds[0].revents & POLLIN)) {
		ret = read(ep0, &event, sizeof(event));
		if (!ret) {
			perror("unable to read event from ep0");
			return;
		}
		display_event(&event);
		switch (event.type) {
		case FUNCTIONFS_SETUP:
			if (event.u.setup.bRequestType & USB_DIR_IN)
				write(ep0, NULL, 0);
			else
				read(ep0, NULL, 0);
			break;

		case FUNCTIONFS_ENABLE:
			*ready = true;
			break;

		case FUNCTIONFS_DISABLE:
			*ready = false;
			break;

		default:
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int i, ret;
	char *ep_path;

	int ep0;
	int ep[2];

	io_context_t ctx;

	int evfd;
	fd_set rfds;

	char *buf_in, *buf_out;
	struct iocb *iocb_in, *iocb_out;
	int req_in = 0, req_out = 0;
	bool ready;

	if (argc != 2) {
		printf("ffs directory not specified!\n");
		return 1;
	}

	ep_path = malloc(strlen(argv[1]) + 4 /* "/ep#" */ + 1 /* '\0' */);
	if (!ep_path) {
		perror("malloc");
		return 1;
	}

	/* open endpoint files */
	sprintf(ep_path, "%s/ep0", argv[1]);
	ep0 = open(ep_path, O_RDWR);
	if (ep0 < 0) {
		perror("unable to open ep0");
		return 1;
	}
	if (write(ep0, &descriptors, sizeof(descriptors)) < 0) {
		perror("unable do write descriptors");
		return 1;
	}
	if (write(ep0, &strings, sizeof(strings)) < 0) {
		perror("unable to write strings");
		return 1;
	}
	for (i = 0; i < 2; ++i) {
		sprintf(ep_path, "%s/ep%d", argv[1], i+1);
		ep[i] = open(ep_path, O_RDWR);
		if (ep[i] < 0) {
			printf("unable to open ep%d: %s\n", i+1,
			       strerror(errno));
			return 1;
		}
	}

	free(ep_path);

	memset(&ctx, 0, sizeof(ctx));
	/* setup aio context to handle up to 2 requests */
	if (io_setup(2, &ctx) < 0) {
		perror("unable to setup aio");
		return 1;
	}

	evfd = eventfd(0, 0);
	if (evfd < 0) {
		perror("unable to open eventfd");
		return 1;
	}

	/* alloc buffers and requests */
	buf_in = malloc(BUF_LEN);
	buf_out = malloc(BUF_LEN);
	iocb_in = malloc(sizeof(*iocb_in));
	iocb_out = malloc(sizeof(*iocb_out));

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(ep0, &rfds);
		FD_SET(evfd, &rfds);

		ret = select(((ep0 > evfd) ? ep0 : evfd)+1,
			     &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (FD_ISSET(ep0, &rfds))
			handle_ep0(ep0, &ready);

		/* we are waiting for function ENABLE */
		if (!ready)
			continue;

		/* if something was submitted we wait for event */
		if (FD_ISSET(evfd, &rfds)) {
			uint64_t ev_cnt;
			ret = read(evfd, &ev_cnt, sizeof(ev_cnt));
			if (ret < 0) {
				perror("unable to read eventfd");
				break;
			}

			struct io_event e[2];
			/* we wait for one event */
			ret = io_getevents(ctx, 1, 2, e, NULL);
			/* if we got event */
			for (i = 0; i < ret; ++i) {
				if (e[i].obj->aio_fildes == ep[1]) {
					printf("ev=in; ret=%lu\n", e[i].res);
					req_in = 0;
				} else if (e[i].obj->aio_fildes == ep[0]) {
					printf("ev=out; ret=%lu\n", e[i].res);
					req_out = 0;
				}
			}
		}

		if (!req_in) { /* if IN transfer not requested*/
			/* prepare write request */
			io_prep_pwrite(iocb_in, ep[1], buf_in, BUF_LEN, 0);
			/* enable eventfd notification */
			iocb_in->u.c.flags |= IOCB_FLAG_RESFD;
			iocb_in->u.c.resfd = evfd;
			/* submit table of requests */
			ret = io_submit(ctx, 1, &iocb_in);
			if (ret >= 0) { /* if ret > 0 request is queued */
				req_in = 1;
				printf("submit: in\n");
			} else
				perror("unable to submit request");
		}
		if (!req_out) { /* if OUT transfer not requested */
			/* prepare read request */
			io_prep_pread(iocb_out, ep[0], buf_out, BUF_LEN, 0);
			/* enable eventfs notification */
			iocb_out->u.c.flags |= IOCB_FLAG_RESFD;
			iocb_out->u.c.resfd = evfd;
			/* submit table of requests */
			ret = io_submit(ctx, 1, &iocb_out);
			if (ret >= 0) { /* if ret > 0 request is queued */
				req_out = 1;
				printf("submit: out\n");
			} else
				perror("unable to submit request");
		}
	}

	/* free resources */

	io_destroy(ctx);

	free(buf_in);   
	free(buf_out);
	free(iocb_in);
	free(iocb_out);

	for (i = 0; i < 2; ++i)
		close(ep[i]);
	close(ep0);

	return 0;
}
#else
#define _BSD_SOURCE /* for endian.h */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/eventfd.h>

#include "libaio.h"
#define IOCB_FLAG_RESFD         (1 << 0)

#include <linux/usb/functionfs.h>
#include "shmfifo.h"                                        
#include <sys/mman.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <time.h>

/******************** Descriptors and Strings *******************************/

static const struct {
	struct usb_functionfs_descs_head_v2 header;
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	__le32 os_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio bulk_sink;
		struct usb_endpoint_descriptor_no_audio bulk_source;
	} __attribute__ ((__packed__)) fs_descs, hs_descs;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio sink;
		struct usb_ss_ep_comp_descriptor sink_comp;
		struct usb_endpoint_descriptor_no_audio source;
		struct usb_ss_ep_comp_descriptor source_comp;
	} __attribute__ ((__packed__)) ss_descs;
	struct usb_os_desc_header os_header;
	struct usb_ext_compat_desc os_desc;

} __attribute__ ((__packed__)) descriptors = {
	.header = {
		.magic = (FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = (FUNCTIONFS_HAS_FS_DESC |
				 FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC |
				 FUNCTIONFS_HAS_MS_OS_DESC),
		.length = (sizeof(descriptors)),
	},
	.fs_count = (3),
	.fs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.fs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.fs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.fs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
	},
	.hs_count = (3),
	.hs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.hs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.hs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.hs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (512),
		},
	},
	.ss_count = (5),
	.ss_descs = {
		.intf = {
			.bLength = sizeof(descriptors.ss_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bInterfaceNumber = 0,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.sink = {
			.bLength = sizeof(descriptors.ss_descs.sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 1 | USB_DIR_IN,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (1024),
		},
		.sink_comp = {
			.bLength = sizeof(descriptors.ss_descs.sink_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
		.source = {
			.bLength = sizeof(descriptors.ss_descs.source),
			.bDescriptorType = USB_DT_ENDPOINT,
			//.bEndpointAddress = 2 | USB_DIR_OUT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = (1024),
		},
		.source_comp = {
			.bLength = sizeof(descriptors.ss_descs.source_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
	},
	.os_count = (1),
	.os_header = {
		.interface = (1),
		.dwLength = (sizeof(descriptors.os_header) +
			    sizeof(descriptors.os_desc)),
		.bcdVersion = (1),
		.wIndex = (4),
		.bCount = (1),
		.Reserved = (0),
	},
	.os_desc = {
		.bFirstInterfaceNumber = 0,
		.Reserved1 = (1),
		.CompatibleID = {0},
		.SubCompatibleID = {0},
		.Reserved2 = {0},
	},
};

#define STR_INTERFACE "AIO Test"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE)];
	} __attribute__ ((__packed__)) lang0;
} __attribute__ ((__packed__)) strings = {
	.header = {
		.magic = (FUNCTIONFS_STRINGS_MAGIC),
		.length = (sizeof(strings)),
		.str_count = (1),
		.lang_count = (1),
	},
	.lang0 = {
		(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

/******************** Endpoints handling *******************************/

static void display_event(struct usb_functionfs_event *event)
{
	static const char *const names[] = {
		[FUNCTIONFS_BIND] = "BIND",
		[FUNCTIONFS_UNBIND] = "UNBIND",
		[FUNCTIONFS_ENABLE] = "ENABLE",
		[FUNCTIONFS_DISABLE] = "DISABLE",
		[FUNCTIONFS_SETUP] = "SETUP",
		[FUNCTIONFS_SUSPEND] = "SUSPEND",
		[FUNCTIONFS_RESUME] = "RESUME",
	};
	switch (event->type) {
	case FUNCTIONFS_BIND:
	case FUNCTIONFS_UNBIND:
	case FUNCTIONFS_ENABLE:
	case FUNCTIONFS_DISABLE:
	case FUNCTIONFS_SETUP:
	case FUNCTIONFS_SUSPEND:
	case FUNCTIONFS_RESUME:
		printf("Event %s\n", names[event->type]);
	}
}

static void handle_ep0(int ep0, bool *ready)
{
	struct usb_functionfs_event event;
	int ret;

	struct pollfd pfds[1];
	pfds[0].fd = ep0;
	pfds[0].events = POLLIN;

	ret = poll(pfds, 1, 0);

	if (ret && (pfds[0].revents & POLLIN)) {
		ret = read(ep0, &event, sizeof(event));
		if (!ret) {
			perror("unable to read event from ep0");
			return;
		}
		display_event(&event);
		switch (event.type) {
		case FUNCTIONFS_SETUP:
			if (event.u.setup.bRequestType & USB_DIR_IN)
				write(ep0, NULL, 0);
			else
				read(ep0, NULL, 0);
			break;

		case FUNCTIONFS_ENABLE:
			*ready = true;
			break;

		case FUNCTIONFS_DISABLE:
			*ready = false;
			break;

		default:
			break;
		}
	}
}


//=========================================================
int ep[2];
bool ready;
void* sendApsDataPthread(void *arg)
{
	int ret;
	int req_aps = 0;
	struct iocb *iocb_aps;
	arg =NULL;
	/* alloc buffers and requests */
	iocb_aps = (struct iocb *)malloc(sizeof(*iocb_aps));

	int APS_DATA_LEN = 640*480*3/2;

	int efd = eventfd(0, 0);
	if (efd < 0) {
		perror("unable to open eventfd");
		return NULL;
	}
	io_context_t apsctx;
	memset(&apsctx, 0, sizeof(apsctx));
	/* setup aio context to handle up to 1 requests */
	if (io_setup(1, &apsctx) < 0) {
		perror("unable to setup aio");
		return NULL;
	}
	printf("=========start usb aps data pthread \n");
	//CircularBuffer * pRingbuf = (CircularBuffer *)arg;

	  /* 初始化获取APS数据的共享内存 */
	int mmapfd = shm_open("/apcdatashm", O_RDONLY, 0777);
	ftruncate(mmapfd,APS_DATA_LEN);
	sem_t * wait_aps_sem    = sem_open("/wait_aps_sem", O_CREAT|O_RDWR, 0666, 0); //信号量值为 0  
    sem_t * send_done_sem  = sem_open("/send_done_sem", O_CREAT|O_RDWR, 0666, 1); //信号量值为 1
	char *papsmmapbuf = (char *)mmap(NULL, APS_DATA_LEN, PROT_READ, MAP_SHARED, mmapfd, 0);
	if (papsmmapbuf == MAP_FAILED)
	{
		sem_post(send_done_sem);
		perror("mmap");
	}
	while(1)
	{
		//unsigned char * p = (uint8_t *)cbReadBuffer(pRingbuf, APS_DATA_LEN);
		/* we are waiting for function ENABLE */
		if (!ready /*|| p==NULL*/)   
		{
			usleep(10*1000);
			continue;
		}
		
		/* if something was submitted we wait for event */
		if(req_aps==1)
		{
			struct timespec timeout;  
			timeout.tv_sec = 0;
            timeout.tv_nsec = 30*1000000;
			struct io_event e[1];
			/* we wait for one event */
			ret = io_getevents(apsctx, 1, 1, e, &timeout);
			/* if we got event */
			if(ret == 1)
			{
				req_aps = 0;
			}
			// if (e[0].obj->aio_fildes == ep[0]) {
			// 		printf("ev=usbcam; ret=%lu\n", e[0].res);
			// 		req_aps = 0;
			// 	} 
		}
		if(!req_aps) { 

			/*超时时间宏: s*/
			#define DIAG_TIMEOUT 3
			struct timeval now;
			struct timespec out_time;

			/*开始进行超时等待*/
			gettimeofday(&now, NULL);
			out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
			out_time.tv_nsec = now.tv_usec * 1000;
			if(sem_timedwait(wait_aps_sem,&out_time) <0 )
			{
				printf("======timeout wait_aps_sem\n");
			}
			/* prepare write request */
			io_prep_pwrite(iocb_aps, ep[0], papsmmapbuf,APS_DATA_LEN, 0);
			/* enable eventfd notification */
			iocb_aps->u.c.flags |= IOCB_FLAG_RESFD;
			iocb_aps->u.c.resfd = efd;
			/* submit table of requests */
			ret = io_submit(apsctx, 1, &iocb_aps);
			if (ret >= 0) { /* if ret > 0 request is queued */
				req_aps = 1;
				sem_post(send_done_sem);
			//	printf("submit: usbcam, index:%d\n", efd);
			} else {
				perror("unable to submit usbcam request");
			}
		}
	}
	printf("================exit sendApsDataPthread \n");
	free(iocb_aps);
	//cbFree(pRingbuf);
}

static void sendApsDataInit(void *arg)
{
	// CircularBuffer * pBbuf = cbCreate("usbComsume","APSdata",10*1024*1024,CRB_PERSONALITY_READER);
	// if(pBbuf != NULL)
	// 	printf("\n ===========sendApsDataInit success \n");
	pthread_t apspid;
	pthread_create(&apspid, NULL, sendApsDataPthread, /*(CircularBuffer *)pBbuf*/ arg);
}

int main(int argc, char *argv[]) {
    int i, ret;
	char *ep_path;

	int ep0;
	int ep[2];

	io_context_t ctx;

	int evfd;
	fd_set rfds;

	char *buf_in, *buf_out;
	struct iocb *iocb_in, *iocb_out;
	int req_in = 0, req_out = 0;
	//bool ready;

    
    u_int32_t old_seq;
    struct timespec ts1, ts2;

	if (argc != 2) {
		printf("ffs directory not specified!\n");
		return 1;
	}

	ep_path = malloc(strlen(argv[1]) + 4 /* "/ep#" */ + 1 /* '\0' */);
	if (!ep_path) {
		perror("malloc");
		return 1;
	}

	/* open endpoint files */
	sprintf(ep_path, "%s/ep0", argv[1]);
	ep0 = open(ep_path, O_RDWR);
	if (ep0 < 0) {
		perror("unable to open ep0");
		return 1;
	}
	if (write(ep0, &descriptors, sizeof(descriptors)) < 0) {
		perror("unable do write descriptors");
		return 1;
	}
	if (write(ep0, &strings, sizeof(strings)) < 0) {
		perror("unable to write strings");
		return 1;
	}
	for (i = 0; i < 2; ++i) {
		sprintf(ep_path, "%s/ep%d", argv[1], i+1);
		ep[i] = open(ep_path, O_RDWR);
		if (ep[i] < 0) {
			("unable to open ep%d: %s\n", i+1,
			       strerror(errno));
			return 1;
		}
	}

	free(ep_path);

	memset(&ctx, 0, sizeof(ctx));
	/* setup aio context to handle up to 2 requests */
	if (io_setup(2, &ctx) < 0) {
		perror("unable to setup aio");
		return 1;
	}

	evfd = eventfd(0, 0);
	if (evfd < 0) {
		perror("unable to open eventfd");
		return 1;
	}

	
    //int EVS_DATA_LEN = 640*480*3/2;
	int EVS_DATA_LEN =4080*160;
    /* alloc buffers and requests */
	buf_in = malloc(EVS_DATA_LEN);
	buf_out = malloc(EVS_DATA_LEN);
	iocb_in = malloc(sizeof(*iocb_in));
	iocb_out = malloc(sizeof(*iocb_out));
	//sendApsDataInit(&evfd);

    /*
    FILE *fp = NULL;
    fp = fopen("/tmp/cap.raw", "wb");
    if (fp == NULL) {
        printf("open  failed, error: %s", strerror(errno));
        exit(-1);
    }
	*/
    // 打开共享内存对象
    int shm_fd = shm_open("/dvsdatashm", O_CREAT | O_RDWR, 0777);
	ftruncate(shm_fd,EVS_DATA_LEN);    
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    
	sem_t *wait_dvs_sem = sem_open("/wait_dvs_sem", O_CREAT|O_RDWR, 0666, 1);
    sem_t *send_donedvs_sem = sem_open("/send_donedvs_sem", O_CREAT|O_RDWR, 0666, 0); 

    // 将共享内存映射到进程地址空间
    char* papsdata = mmap(0, EVS_DATA_LEN, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (papsdata == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

	
    while (1) {
		FD_ZERO(&rfds);
		FD_SET(ep0, &rfds);
		FD_SET(evfd, &rfds);

		ret = select(((ep0 > evfd) ? ep0 : evfd)+1,
			     &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (FD_ISSET(ep0, &rfds))
			handle_ep0(ep0, &ready);

		/* we are waiting for function ENABLE */
		if (!ready)
			continue;

		/* if something was submitted we wait for event */
		if (FD_ISSET(evfd, &rfds)) {
			uint64_t ev_cnt;
			ret = read(evfd, &ev_cnt, sizeof(ev_cnt));
			if (ret < 0) {
				perror("unable to read eventfd");
				break;
			}

			struct io_event e[2];
			/* we wait for one event */
			ret = io_getevents(ctx, 1, 2, e, NULL);
			/* if we got event */
			for (i = 0; i < ret; ++i) {
				if (e[i].obj->aio_fildes == ep[1]) {
                    printf("ev=in; ret=%lu, expected size =  %u\n", e[i].res, EVS_DATA_LEN);
					req_in = 0;
					if (e[i].res < 0) {
						// 处理 I/O 失败情况
						perror("Async write operation failed");
					} else if (e[i].res != EVS_DATA_LEN) {
						fprintf(stderr, "Warning: Submitted size (%u) does not match returned size (%lu)\n", EVS_DATA_LEN, e[i].res);
                    }				
				} else if (e[i].obj->aio_fildes == ep[0]) {
					printf("ev=out; ret=%lu\n", e[i].res);
					//req_aps = 0;
					req_out = 0;
				}
			}
		}
		printf("Generated random data (first 6 bytes): [%02x %02x %02x %02x %02x %02x]\n",
               papsdata[0], papsdata[1], papsdata[2], papsdata[3], papsdata[4], papsdata[5]);
	
 #if 0
		/*开始进行超时等待*/
        clock_gettime(CLOCK_MONOTONIC, &ts1);

		gettimeofday(&now, NULL);
		out_time.tv_sec = now.tv_sec + DIAG_TIMEOUT;
		out_time.tv_nsec = now.tv_usec * 1000;
		if(sem_timedwait(wait_aps_sem,&out_time) <0 )
		{
			printf("======timeout wait_dvs_sem\n");
		}

        fwrite(papsdata,1, APS_DATA_LEN, fp);
        fflush(fp);

        fprintf(stdout, "[%d] len:%d, [%02x%02x%02x%02x%02x%02x]\r\n", seq, APS_DATA_LEN, \
            papsdata[0], papsdata[1], papsdata[2], papsdata[3], papsdata[4], papsdata[5]);
      clock_gettime(CLOCK_MONOTONIC, &ts2);
        int cost = (ts2.tv_sec*1000 + ts2.tv_nsec/1000000) - (ts1.tv_sec*1000 + ts1.tv_nsec/1000000);
        if(cost > 10)
            printf("get frame %d, frame size:%d put cost:%d ms\n", seq, APS_DATA_LEN, cost);

		seq++;
		#endif
		/*     
		for (size_t i = 0; i < 256; i++) {
					if (i < 256) {
						buf_in[i] = rand() % 256;  // 生成 0~255 的随机字节
					}
				}
          
				// 打印生成的随机数据
        printf("Generated random data (first 6 bytes): [%02x %02x %02x %02x %02x %02x]\n",
               buf_in[0], buf_in[1], buf_in[2], buf_in[3], buf_in[4], buf_in[5]);
			*/
		// memcpy(aps_usr_buf.start,papsdata,APS_DATA_LEN);
        
		if (!req_in) { /* if IN transfer not requested*/
		
			/* prepare write request */
			//io_prep_pwrite(iocb_in, ep[0], papsdata, EVS_DATA_LEN, 0);
            io_prep_pwrite(iocb_in, ep[1], papsdata, EVS_DATA_LEN, 0);
			/* enable eventfd notification */
			iocb_in->u.c.flags |= IOCB_FLAG_RESFD;
			iocb_in->u.c.resfd = evfd;
			/* submit table of requests */
			ret = io_submit(ctx, 1, &iocb_in);
			if (ret >= 0) { /* if ret > 0 request is queued */
				req_in = 1;
                sem_post(send_donedvs_sem);
				printf("submit: in\n");
			} else
				perror("unable to submit request");
				}
		#if 1
		if (!req_out) { /* if OUT transfer not requested */
			/* prepare read request */
			io_prep_pwrite(iocb_out, ep[0], papsdata, EVS_DATA_LEN, 0);
			//io_prep_pwrite(iocb_out, ep[1], buf_out, EVS_DATA_LEN, 0);
			/* enable eventfs notification */
			iocb_out->u.c.flags |= IOCB_FLAG_RESFD;
			iocb_out->u.c.resfd = evfd;
			/* submit table of requests */
			ret = io_submit(ctx, 1, &iocb_out);
			if (ret >= 0) { /* if ret > 0 request is queued */
				req_out = 1;
				sem_post(send_donedvs_sem);
				printf("submit: out\n");
			} else
				perror("unable to submit request");
		}
		#endif
	}


	/* free resources */

	io_destroy(ctx);

	free(buf_in);
	free(buf_out);
	free(iocb_in);
	free(iocb_out);

	for (i = 0; i < 2; ++i)
		close(ep[i]);
	close(ep0);

    //fclose(fp);

    return 0;


}  
#endif