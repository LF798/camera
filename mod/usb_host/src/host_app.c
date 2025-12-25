#if 0
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VENDOR 0x1d6b
#define PRODUCT 0x0105
#define BUF_LEN  (8192)

struct test_state {
    libusb_device *found;
    libusb_context *ctx;
    libusb_device_handle *handle;
    int attached;
};

int test_init(struct test_state *state) {
    int ret;
    libusb_device **list;
    ssize_t cnt;

    state->found = NULL;
    state->ctx = NULL;
    state->handle = NULL;
    state->attached = 0;

    // 初始化libusb
    ret = libusb_init(&state->ctx);
    if (ret) {
        fprintf(stderr, "Cannot initialize libusb: %s\n", libusb_error_name(ret));
        return 1;
    }

    // 获取设备列表
    cnt = libusb_get_device_list(state->ctx, &list);
    if (cnt < 0) {
        fprintf(stderr, "No devices found\n");
        goto error_exit;
    }

    // 查找目标设备
    for (ssize_t i = 0; i < cnt; ++i) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(list[i], &desc);
        if (ret) {
            fprintf(stderr, "Unable to get device descriptor: %s\n", libusb_error_name(ret));
            goto error_free_list;
        }
        if (desc.idVendor == VENDOR && desc.idProduct == PRODUCT) {
            state->found = list[i];
            break;
        }
    }

    if (!state->found) {
        fprintf(stderr, "No matching devices found\n");
        goto error_free_list;
    }

    // 打开设备
    ret = libusb_open(state->found, &state->handle);
    if (ret) {
        fprintf(stderr, "Cannot open device: %s\n", libusb_error_name(ret));
        goto error_free_list;
    }

    // 如果设备被内核驱动控制，则分离
    if (libusb_kernel_driver_active(state->handle, 0)) {
        ret = libusb_detach_kernel_driver(state->handle, 0);
        if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
            printf("Kernel driver not attached, proceeding without detaching.\n");
        } else if (ret) {
            fprintf(stderr, "Unable to detach kernel driver: %s\n", libusb_error_name(ret));
            goto error_close_device;
        }
        state->attached = 1;
    }

    // 声明接口
    ret = libusb_claim_interface(state->handle, 0);
    if (ret) {
        fprintf(stderr, "Cannot claim interface: %s\n", libusb_error_name(ret));
        goto error_attach_driver;
    }

    libusb_free_device_list(list, 1);
    return 0;

error_attach_driver:
    if (state->attached) {
        libusb_attach_kernel_driver(state->handle, 0);
    }
error_close_device:
    libusb_close(state->handle);
error_free_list:
    libusb_free_device_list(list, 1);
error_exit:
    libusb_exit(state->ctx);
    return 1;
}

void test_exit(struct test_state *state) {
    if (state->handle) {
        libusb_release_interface(state->handle, 0);
        if (state->attached) {
            libusb_attach_kernel_driver(state->handle, 0);
        }
        libusb_close(state->handle);
    }
    libusb_exit(state->ctx);
}

int main(void) {
    struct test_state state;
    struct libusb_config_descriptor *conf;
    const struct libusb_interface_descriptor *iface;
    //unsigned char in_addr, out_addr;
    unsigned char ep1_addr;  // 用于存储 ep1 地址的变量

    if (test_init(&state)) {
        return 1;
    }

    // 获取配置描述符
    libusb_get_config_descriptor(state.found, 0, &conf);
    iface = &conf->interface[0].altsetting[0];
    //in_addr = iface->endpoint[0].bEndpointAddress;
    //out_addr = iface->endpoint[1].bEndpointAddress;
    // 确保使用正确的端点
    ep1_addr = iface->endpoint[0].bEndpointAddress; 

	while (1) {
		static unsigned char buffer[BUF_LEN];
		int bytes;
		libusb_bulk_transfer(state.handle, ep1_addr, buffer, BUF_LEN,
				     &bytes, 500);
        // printf("rev:%s \r\n",&(buffer[0]));
        fprintf(stdout, "Received : [%02x%02x%02x%02x%02x%02x]\r\n",
                     buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
        fprintf(stdout, "buff_addr:%p\r\n", buffer);
	}
	test_exit(&state);

}
#endif

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VENDOR 0x1d6b
#define PRODUCT 0x0105
#define BUF_LEN  (8192)

struct test_state {
    libusb_device *found;
    libusb_context *ctx;
    libusb_device_handle *handle;
    int attached;
};

int test_init(struct test_state *state) {
    int ret;
    libusb_device **list;
    ssize_t cnt;

    state->found = NULL;
    state->ctx = NULL;
    state->handle = NULL;
    state->attached = 0;

    // 初始化libusb
    ret = libusb_init(&state->ctx);
    if (ret) {
        fprintf(stderr, "Cannot initialize libusb: %s\n", libusb_error_name(ret));
        return 1;
    }

    // 获取设备列表
    cnt = libusb_get_device_list(state->ctx, &list);
    if (cnt < 0) {
        fprintf(stderr, "No devices found\n");
        goto error_exit;
    }

    // 查找目标设备
    for (ssize_t i = 0; i < cnt; ++i) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(list[i], &desc);
        if (ret) {
            fprintf(stderr, "Unable to get device descriptor: %s\n", libusb_error_name(ret));
            goto error_free_list;
        }
        if (desc.idVendor == VENDOR && desc.idProduct == PRODUCT) {
            state->found = list[i];
            break;
        }
    }

    if (!state->found) {
        fprintf(stderr, "No matching devices found\n");
        goto error_free_list;
    }

    // 打开设备
    ret = libusb_open(state->found, &state->handle);
    if (ret) {
        fprintf(stderr, "Cannot open device: %s\n", libusb_error_name(ret));
        goto error_free_list;
    }

    // 如果设备被内核驱动控制，则分离
    if (libusb_kernel_driver_active(state->handle, 0)) {
        ret = libusb_detach_kernel_driver(state->handle, 0);
        if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
            printf("Kernel driver not attached, proceeding without detaching.\n");
        } else if (ret) {
            fprintf(stderr, "Unable to detach kernel driver: %s\n", libusb_error_name(ret));
            goto error_close_device;
        }
        state->attached = 1;
    }

    // 声明接口
    ret = libusb_claim_interface(state->handle, 0);
    if (ret) {
        fprintf(stderr, "Cannot claim interface: %s\n", libusb_error_name(ret));
        goto error_attach_driver;
    }

    libusb_free_device_list(list, 1);
    return 0;

error_attach_driver:
    if (state->attached) {
        libusb_attach_kernel_driver(state->handle, 0);
    }
error_close_device:
    libusb_close(state->handle);
error_free_list:
    libusb_free_device_list(list, 1);
error_exit:
    libusb_exit(state->ctx);
    return 1;
}

void test_exit(struct test_state *state) {
    if (state->handle) {
        libusb_release_interface(state->handle, 0);
        if (state->attached) {
            libusb_attach_kernel_driver(state->handle, 0);
        }
        libusb_close(state->handle);
    }
    libusb_exit(state->ctx);
}

int main(void) {
    struct test_state state;
    struct libusb_config_descriptor *conf;
    const struct libusb_interface_descriptor *iface;
    unsigned char ep1_addr;  // 用于存储 ep1 地址的变量

    if (test_init(&state)) {
        return 1;
    }

    // 获取配置描述符
    libusb_get_config_descriptor(state.found, 0, &conf);
    iface = &conf->interface[0].altsetting[0];
    ep1_addr = iface->endpoint[0].bEndpointAddress;

    // 打开文件以保存接收到的数据
    FILE *file = fopen("received_data.bin", "wb");
    if (!file) {
        perror("Unable to open file for writing");
        test_exit(&state);
        return 1;
    }

    while (1) {
        static unsigned char buffer[BUF_LEN];
        int bytes;
        int ret = libusb_bulk_transfer(state.handle, ep1_addr, buffer, BUF_LEN, &bytes, 500);

        if (ret == LIBUSB_SUCCESS) {
            // 将接收到的数据写入文件
            fwrite(buffer, 1, bytes, file);
            printf("Received %d bytes, written to file.\n", bytes);
        } else {
            fprintf(stderr, "Error in bulk transfer: %s\n", libusb_error_name(ret));
            break; // 发生错误时退出循环
        }
    }

    fclose(file); // 关闭文件
    test_exit(&state);
    return 0;
}
