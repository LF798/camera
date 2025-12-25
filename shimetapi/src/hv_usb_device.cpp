#include "hv_usb_device.h"

namespace hv {

USBDevice::USBDevice(uint16_t vendor_id, uint16_t product_id)
    : vendor_id_(vendor_id), product_id_(product_id),
      ctx_(nullptr), device_(nullptr), handle_(nullptr), attached_(false) {
    // 初始化端点地址数组
    for (int i = 0; i < 8; ++i) {
        endpoints_[i] = 0;
    }
}

USBDevice::~USBDevice() {
    close();
}

bool USBDevice::open() {
    int ret;

    // 初始化 libusb
    ret = libusb_init(&ctx_);
    if (ret) {
        std::cerr << "Cannot initialize libusb: " << libusb_error_name(ret) << std::endl;
        return false;
    }

    // 获取设备列表
    libusb_device** list;
    ssize_t cnt = libusb_get_device_list(ctx_, &list);
    if (cnt < 0) {
        std::cerr << "No devices found" << std::endl;
        libusb_exit(ctx_);
        return false;
    }

    // 查找目标设备
    for (ssize_t i = 0; i < cnt; ++i) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(list[i], &desc);
        if (ret) {
            std::cerr << "Unable to get device descriptor: " << libusb_error_name(ret) << std::endl;
            libusb_free_device_list(list, 1);
            return false;
        }
        if (desc.idVendor == vendor_id_ && desc.idProduct == product_id_) {
            device_ = list[i];
            break;
        }
    }

    if (!device_) {
        std::cerr << "No matching devices found" << std::endl;
        libusb_free_device_list(list, 1);
        return false;
    }

    // 打开设备
    ret = libusb_open(device_, &handle_);
    if (ret) {
        std::cerr << "Cannot open device: " << libusb_error_name(ret) << std::endl;
        libusb_free_device_list(list, 1);
        libusb_exit(ctx_);
        return false;
    }

    // 如果设备被内核驱动控制，则分离
    if (libusb_kernel_driver_active(handle_, 0)) {
        ret = libusb_detach_kernel_driver(handle_, 0);
        if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
            std::cout << "Kernel driver not attached, proceeding without detaching." << std::endl;
        } else if (ret) {
            std::cerr << "Unable to detach kernel driver: " << libusb_error_name(ret) << std::endl;
            libusb_close(handle_);
            libusb_free_device_list(list, 1);
            libusb_exit(ctx_);
            return false;
        }
        attached_ = true;
    }

    // 声明接口
    ret = libusb_claim_interface(handle_, 0);
    if (ret) {
        std::cerr << "Cannot claim interface: " << libusb_error_name(ret) << std::endl;
        if (attached_) {
            libusb_attach_kernel_driver(handle_, 0);
        }
        libusb_close(handle_);
        libusb_free_device_list(list, 1);
        libusb_exit(ctx_);
        return false;
    }

    // 获取配置描述符
    libusb_config_descriptor* conf;
    libusb_get_config_descriptor(device_, 0, &conf);
    if (conf->interface[0].num_altsetting > 0) {
        // 存储端点地址
        for (int i = 0; i < conf->interface[0].altsetting[0].bNumEndpoints && i < 8; ++i) {
            endpoints_[i] = conf->interface[0].altsetting[0].endpoint[i].bEndpointAddress;
        }
    }
    libusb_free_config_descriptor(conf);

    return true;
}

bool USBDevice::isOpen() const {
    return handle_ != nullptr;
}

void USBDevice::close() {
    if (handle_) {
        libusb_release_interface(handle_, 0);
        if (attached_) {
            libusb_attach_kernel_driver(handle_, 0);
        }
        libusb_close(handle_);
        handle_ = nullptr;
    }
    if (ctx_) {
        libusb_exit(ctx_);
        ctx_ = nullptr;
    }
}

uint8_t USBDevice::getEndpointAddress(int index) const {
    if (index >= 0 && index < 8) {
        return endpoints_[index];
    }
    return 0;
}

bool USBDevice::bulkTransfer(uint8_t endpoint, unsigned char* data, int length, int* transferred, unsigned int timeout) {
    if (!isOpen()) {
        return false;
    }
    
    int ret = libusb_bulk_transfer(handle_, endpoint, data, length, transferred, timeout);
    return (ret == LIBUSB_SUCCESS);
}

bool USBDevice::clearSharedMemory() {
    if (!isOpen()) {
        return false;
    }
    //无需返回值
    int ret = libusb_control_transfer(handle_, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT, 0x07, 0, 0, nullptr, 0, 1000);
    return true;
}

libusb_device_handle* USBDevice::getHandle() const {
    return handle_;
}

} // namespace hv
