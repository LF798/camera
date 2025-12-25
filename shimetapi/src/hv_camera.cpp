#include "hv_camera.h"
#include "hv_usb_device.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event2d.h>

namespace hv {

HV_Camera::HV_Camera(uint16_t vendor_id, uint16_t product_id)
    : usb_device_(std::make_unique<USBDevice>(vendor_id, product_id)),
      event_endpoint_(0), image_endpoint_(0),
      event_running_(false), image_running_(false),
      event_processing_running_(false),
      latest_image_(HV_APS_HEIGHT, HV_APS_WIDTH, CV_8UC3, cv::Scalar(0, 0, 0)) {
    // 性能优化：预分配事件数组容量
    reusable_event_array_.reserve(ESTIMATED_EVENTS_PER_FRAME);
}

HV_Camera::~HV_Camera() {
    stopEventCapture();
    stopImageCapture();
    close();
}

bool HV_Camera::open() {
    // 使用USB设备类打开设备
    if (!usb_device_->open()) {
        return false;
    }
    
    // 获取端点地址
    image_endpoint_ = usb_device_->getEndpointAddress(0);
    event_endpoint_ = usb_device_->getEndpointAddress(1);
    
    return true;
}

bool HV_Camera::isOpen() const {
    return usb_device_->isOpen();
}

void HV_Camera::close() {
    usb_device_->close();
}

bool HV_Camera::startEventCapture(EventCallback callback) {
    std::cout << "Starting event capture" << std::endl;
    std::cout <<"请确保USB为3.0以上版本，使用USB2.0可能导致丢帧。"<< std::endl;
    if (!isOpen()) {
        std::cerr << "Device not opened" << std::endl;
        return false;
    }

    if (event_running_) {
        std::cerr << "Event capture already running" << std::endl;
        return false;
    }

    event_callback_ = callback;
    event_running_ = true;
    event_processing_running_ = true;

    // 清空队列
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        while (!event_data_queue_.empty()) {
            event_data_queue_.pop();
        }
        std::cout << "Event queue cleared" << std::endl;
    }

    int result = libusb_clear_halt(usb_device_->getHandle(), event_endpoint_);
    if (result != 0) {
        std::cerr << "Failed to clear halt on endpoint: " << result << " (" << libusb_error_name(result) << ")" << std::endl;
    } else {
        std::cout << "Successfully cleared halt on endpoint" << std::endl;
    }
   
    std::cout << "Sending clear device shared memory request" << std::endl;
    usb_device_->clearSharedMemory();

    // 启动USB数据接收线程
    std::thread event_thread(&HV_Camera::eventThreadFunc, this);
    event_thread.detach();
    
    // 启动事件数据处理线程
    std::thread processing_thread(&HV_Camera::eventProcessingThreadFunc, this);
    processing_thread.detach();

    return true;
}

void HV_Camera::stopEventCapture() {
    event_running_ = false;
    event_processing_running_ = false;
    
    // 通知处理线程退出
    event_queue_cv_.notify_all();
    
    // 给线程一些时间来退出
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 清空队列
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        while (!event_data_queue_.empty()) {
            event_data_queue_.pop();
        }
    }
}

bool HV_Camera::startImageCapture(ImageCallback callback) {
    std::cout << "Starting image capture" << std::endl;
    if (!isOpen()) {
        std::cerr << "Device not opened" << std::endl;
        return false;
    }

    if (image_running_) {
        std::cerr << "Image capture already running" << std::endl;
        return false;
    }

    image_callback_ = callback;
    image_running_ = true;


    // 启动图像数据采集线程
    std::thread image_thread(&HV_Camera::imageThreadFunc, this);
    image_thread.detach();

    return true;
}

void HV_Camera::stopImageCapture() {
    image_running_ = false;
    // 给线程一些时间来退出
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

cv::Mat HV_Camera::getLatestImage() const {
    std::lock_guard<std::mutex> lock(image_mutex_);
    return latest_image_.clone();
}

void HV_Camera::clearEventQueue() {
    std::lock_guard<std::mutex> lock(event_queue_mutex_);
    while (!event_data_queue_.empty()) {
        event_data_queue_.pop();
    }
    std::cout << "Event queue cleared" << std::endl;
}

void HV_Camera::eventThreadFunc() {
    int usb_transfer_count = 0;  // USB传输计数器
    const size_t CLEAR_THRESHOLD = MAX_QUEUE_SIZE / 2;  // 清空阈值，例如队列一半满时清空
    
    while (event_running_ && isOpen()) {
        unsigned char* buffer = new unsigned char[HV_BUF_LEN];
        int bytes;
        
        // 记录USB传输开始时间
        auto usb_start_time = std::chrono::high_resolution_clock::now();
        
        // 使用USB设备类进行数据传输
        bool success = usb_device_->bulkTransfer(event_endpoint_, buffer, HV_BUF_LEN, &bytes, 500);
        
        // 记录USB传输结束时间并计算耗时
        auto usb_end_time = std::chrono::high_resolution_clock::now();
        auto usb_duration = std::chrono::duration_cast<std::chrono::microseconds>(usb_end_time - usb_start_time);
        
        usb_transfer_count++;
        
        // 每100次USB传输输出一次平均耗时
        if (usb_transfer_count % 100 == 0) {
            std::cout << "[USB Event] Transfer #" << usb_transfer_count 
                     << ", last transfer time: " << usb_duration.count() << " μs" 
                     << ", success: " << (success ? "true" : "false") << std::endl;
        }
        if (success) {
            if (bytes < HV_SUB_FULL_BYTE_SIZE * 4) {
                std::cerr << "Incomplete data received: " << bytes << " bytes" << std::endl;
                delete[] buffer;
                continue;
            }
            
            // 将数据放入队列中，而不是直接处理
            {
                std::unique_lock<std::mutex> lock(event_queue_mutex_);
                
                // 如果队列已满，丢弃最老的数据
                if (event_data_queue_.size() >= MAX_QUEUE_SIZE) {
                    event_data_queue_.pop();
                    std::cerr << "Event queue full, dropping oldest data" << std::endl;
                }
                
                // 创建缓存对象并复制数据
                auto data_buffer = std::make_unique<EventDataBuffer>(HV_BUF_LEN);
                std::memcpy(data_buffer->data.get(), buffer, HV_BUF_LEN);
                event_data_queue_.push(std::move(data_buffer));
            }
            
            // 通知处理线程有新数据
            event_queue_cv_.notify_one();
            
        } else {
            // 如果传输失败，等待一段时间再重试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        delete[] buffer;
    }
}

void HV_Camera::imageThreadFunc() {
    while (image_running_ && isOpen()) {
        unsigned char* image_buffer = new unsigned char[HV_APS_DATA_LEN];
        int bytes;
        
        // 使用USB设备类进行数据传输
        bool success = usb_device_->bulkTransfer(image_endpoint_, image_buffer, HV_APS_DATA_LEN, &bytes, 500);  

        if (success) {
            // 将YUV数据转换为BGR
            cv::Mat yuv(HV_APS_HEIGHT * 3 / 2, HV_APS_WIDTH, CV_8UC1, image_buffer);
            cv::Mat bgr;
            cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV12);
            
            // 更新最新的图像
            {
                std::lock_guard<std::mutex> lock(image_mutex_);
                bgr.copyTo(latest_image_);
            }
            
            // 调用回调函数
            if (image_callback_) {
                image_callback_(bgr);
            }
            
            delete[] image_buffer;
        } else {
            delete[] image_buffer;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void HV_Camera::eventProcessingThreadFunc() {
    static int process_count = 0;  // 处理计数器
    
    while (event_processing_running_) {
        std::vector<std::unique_ptr<EventDataBuffer>> batch_buffers;
        size_t current_queue_size = 0;
        
        // 批量获取多个缓冲区
        {
            std::unique_lock<std::mutex> lock(event_queue_mutex_);
            
            // 等待数据或退出信号
            event_queue_cv_.wait(lock, [this] {
                return !event_data_queue_.empty() || !event_processing_running_;
            });
            
            // 如果收到退出信号且队列为空，则退出
            if (!event_processing_running_ && event_data_queue_.empty()) {
                break;
            }
            
            // 一次性取出多个缓冲区（最多5个）进行批量处理
            while (!event_data_queue_.empty() && batch_buffers.size() < 5) {
                batch_buffers.push_back(std::move(event_data_queue_.front()));
                event_data_queue_.pop();
            }
            current_queue_size = event_data_queue_.size();
        }
        
        // 批量处理数据（在锁外进行，避免阻塞USB接收线程）
        for (auto& data_buffer : batch_buffers) {
            if (data_buffer) {
                for (size_t offset = 0; offset < HV_BUF_LEN; offset += HV_SUB_FULL_BYTE_SIZE * 4) {
                    processEventData(data_buffer->data.get() + offset);
                }
                process_count++;
            }
        }
        
        // 性能优化：降低调试输出频率，从每100次改为每1000次
        if (process_count % 1000 == 0 && process_count > 0) {
            std::cout << "[HV_Camera] Processed " << process_count 
                     << " buffers, current queue size: " << current_queue_size 
                     << ", batch size: " << batch_buffers.size() << std::endl;
        }
    }
}

void HV_Camera::processEventData(uint8_t* dataPtr) {
    uint64_t* pixelBufferPtr = reinterpret_cast<uint64_t*>(dataPtr);
    
    // 性能优化：重用预分配的事件数组，避免频繁内存分配
    reusable_event_array_.clear(); // 清空但保留容量
    
    for (int sub = 0; sub < 4; sub++) {
        uint64_t timestamp = 0;
        uint64_t subframe = 0;
        
        uint64_t buffer = pixelBufferPtr[0];
        timestamp = (buffer >> 24) & 0xFFFFFFFFFF;
        uint64_t header_vec = buffer & 0xFFFFFF;
        if (header_vec != 0xFFFF) {
            std::cerr << "bits process error" << std::endl;
        }
        
        buffer = pixelBufferPtr[1];
        subframe = (buffer >> 44) & 0xF;
        timestamp /= 200;

        pixelBufferPtr += 2;
        int x = 0, y = 0, x_offset = 0, y_offset = 0;
        
        switch (subframe) {
        case 0:
            x_offset = 0;
            y_offset = 0;
            break;
        case 1:
            x_offset = 1;
            y_offset = 0;
            break;
        case 2:
            x_offset = 0;
            y_offset = 1;
            break;
        case 3:
            x_offset = 1;
            y_offset = 1;
            break;
        }
        
        y = y_offset;

        for (int i = 0; i < HV_EVS_SUB_HEIGHT; i++) {
            x = x_offset;
            for (int j = 0; j < HV_EVS_SUB_WIDTH / 32; j++) {
                buffer = pixelBufferPtr[j];
                for (int k = 0; k < 64; k += 2) {
                    uint64_t pix = (buffer >> k) & 0x3;
                    if (x >= HV_EVS_WIDTH || y >= HV_EVS_HEIGHT) {
                        x += 2;
                        continue;
                    }
                    
                    if (pix > 0) {
                        // 性能优化：使用emplace_back直接构造，避免临时对象
                        reusable_event_array_.emplace_back(
                            static_cast<unsigned short>(x), 
                            static_cast<unsigned short>(y), 
                            static_cast<short>(pix >> 1), 
                            static_cast<Metavision::timestamp>(timestamp)
                        );
                    }
                    x += 2;
                }
            }
            pixelBufferPtr += HV_EVS_SUB_WIDTH / 32;
            y += 2;
        }
        pixelBufferPtr += (HV_SUB_FULL_BYTE_SIZE - HV_SUB_VALID_BYTE_SIZE) / 8;
    }
    
    // 处理完所有子帧后，一次性发送所有事件
    if (event_callback_ && !reusable_event_array_.empty()) {
        event_callback_(reusable_event_array_);
    }
}

} // namespace hv
