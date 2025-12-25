#include "hv_evs_recorder.h"
#include "hv_usb_device.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <algorithm>

// 时间戳元数据结构
struct TimestampMetadata {
    size_t block_index;     // 数据块索引
    size_t sub_index;       // 子帧在块中的索引
    uint64_t subframe;      // 子帧编号
    uint64_t raw_timestamp; // 原始时间戳
    uint64_t timestamp;     // 处理后的时间戳
};

namespace hv {

// BufferPool 实现（复用原有实现）
class BufferPool {
public:
    BufferPool(size_t buffer_size, size_t pool_size) 
        : buffer_size_(buffer_size) {
        buffers_.reserve(pool_size);
        
        // 预分配所有缓冲区
        for (size_t i = 0; i < pool_size; ++i) {
            unsigned char* buffer = new unsigned char[buffer_size_];
            buffers_.push_back(buffer);
            available_buffers_.push(buffer);
        }
    }

    ~BufferPool() {
        for (auto* buffer : buffers_) {
            delete[] buffer;
        }
    }

    unsigned char* acquire() {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (available_buffers_.empty()) {
            // 如果池中没有可用缓冲区，创建新的（应该避免这种情况）
            return new unsigned char[buffer_size_];
        }
        
        unsigned char* buffer = available_buffers_.front();
        available_buffers_.pop();
        return buffer;
    }

    void release(unsigned char* buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        // 检查是否是池中的缓冲区
        bool is_pool_buffer = false;
        for (auto* pool_buffer : buffers_) {
            if (pool_buffer == buffer) {
                is_pool_buffer = true;
                break;
            }
        }
        
        if (is_pool_buffer) {
            available_buffers_.push(buffer);
        } else {
            // 如果不是池中的缓冲区，直接删除
            delete[] buffer;
        }
    }

    void warmup() {
        // 预热：访问所有缓冲区的内存页面
        for (auto* buffer : buffers_) {
            // 写入一些数据来确保内存页面被分配
            std::memset(buffer, 0, std::min(buffer_size_, size_t(4096)));
            // 读取数据来预热缓存
            volatile char dummy = buffer[0];
            (void)dummy; // 避免编译器优化
        }
    }

private:
    size_t buffer_size_;
    std::vector<unsigned char*> buffers_;
    std::queue<unsigned char*> available_buffers_;
    std::mutex pool_mutex_;
};

HV_EVS_Recorder::HV_EVS_Recorder(uint16_t vendor_id, uint16_t product_id)
    : usb_device_(std::make_unique<USBDevice>(vendor_id, product_id)),
      event_endpoint_(0),
      recording_(false),
      writer_running_(false),
      timestamp_analysis_enabled_(false) {
    
    // 初始化USB缓冲池：预分配8个缓冲区用于高速数据传输
    usb_buffer_pool_ = std::make_unique<BufferPool>(HV_BUF_LEN, 8);
    
    // 预热内存池
    usb_buffer_pool_->warmup();
    
    // 初始化性能统计
    stats_.total_bytes.store(0);
    stats_.total_frames.store(0);
    stats_.total_transfer_time.store(0);
    stats_.max_transfer_time.store(0);
    stats_.min_transfer_time.store(UINT64_MAX);
}

HV_EVS_Recorder::~HV_EVS_Recorder() {
    stopRecording();
    close();
}

bool HV_EVS_Recorder::open() {
    // 使用USB设备类打开设备
    if (!usb_device_->open()) {
        std::cerr << "Failed to open USB device" << std::endl;
        return false;
    }
    
    // 获取事件数据端点地址
    event_endpoint_ = usb_device_->getEndpointAddress(1);
    
    std::cout << "EVS Recorder opened successfully, event endpoint: 0x" 
              << std::hex << static_cast<int>(event_endpoint_) << std::dec << std::endl;
    
    return true;
}

bool HV_EVS_Recorder::isOpen() const {
    return usb_device_->isOpen();
}

void HV_EVS_Recorder::close() {
    usb_device_->close();
}

bool HV_EVS_Recorder::startRecording(const std::string& filename, bool enable_timestamp_analysis) {
    if (!isOpen()) {
        std::cerr << "Device not opened" << std::endl;
        return false;
    }

    if (recording_) {
        std::cerr << "Recording already in progress" << std::endl;
        return false;
    }

    // 打开输出文件
    output_filename_ = filename;
    output_file_.open(output_filename_, std::ios::binary | std::ios::out);
    if (!output_file_.is_open()) {
        std::cerr << "Failed to open output file: " << output_filename_ << std::endl;
        return false;
    }

    // 重置统计信息
    stats_.total_bytes = 0;
    stats_.total_frames = 0;
    stats_.total_transfer_time = 0;
    stats_.max_transfer_time = 0;
    stats_.min_transfer_time = UINT64_MAX;

    // 初始化时间戳分析
    timestamp_analysis_enabled_ = enable_timestamp_analysis;
    if (timestamp_analysis_enabled_) {
        initTimestampFile();
    }

    // 启动写入线程
    writer_running_ = true;
    writer_thread_ = std::thread(&HV_EVS_Recorder::writerThreadFunc, this);
    std::cout << "[Main] 写入线程已启动" << std::endl;

    // 启动录制线程
    recording_ = true;
    recording_thread_ = std::thread(&HV_EVS_Recorder::recordingThreadFunc, this);
    std::cout << "[Main] 录制线程已启动" << std::endl;

    std::cout << "[Main] 开始录制到文件: " << output_filename_ << std::endl;
    std::cout << "[Main] 队列健康监控已启用，将实时显示调试信息" << std::endl;
    return true;
}

void HV_EVS_Recorder::stopRecording() {
    if (!recording_) {
        std::cout << "[Main] 录制未在进行中，无需停止" << std::endl;
        return;
    }

    std::cout << "[Main] 开始停止录制..." << std::endl;

    // 停止录制线程
    std::cout << "[Main] 正在停止录制线程..." << std::endl;
    recording_ = false;
    if (recording_thread_.joinable()) {
        recording_thread_.join();
        std::cout << "[Main] 录制线程已停止" << std::endl;
    }

    // 停止写入线程
    std::cout << "[Main] 正在停止写入线程..." << std::endl;
    writer_running_ = false;
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
        std::cout << "[Main] 写入线程已停止" << std::endl;
    }

    // 检查队列是否为空
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!write_queue_.empty()) {
            std::cout << "[Main] 警告: 停止时队列中仍有 " << write_queue_.size() << " 个未处理的数据块" << std::endl;
        } else {
            std::cout << "[Main] 队列已清空，所有数据已处理完毕" << std::endl;
        }
    }

    // 关闭文件
    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (output_file_.is_open()) {
            output_file_.close();
            std::cout << "[Main] 输出文件已关闭" << std::endl;
        }
    }
    
    // 关闭时间戳文件
    if (timestamp_analysis_enabled_) {
        closeTimestampFile();
    }

    // 输出最终统计信息
    double total_mb = (double)stats_.total_bytes / (1024 * 1024);
    std::cout << "[Main] 录制完成! 总字节数: " << stats_.total_bytes 
              << " (" << std::fixed << std::setprecision(2) << total_mb << " MB)" 
              << ", 总帧数: " << stats_.total_frames << std::endl;
    
    if (stats_.total_frames > 0) {
        uint64_t avg_transfer_time = stats_.total_transfer_time / stats_.total_frames;
        std::cout << "[Main] 平均传输时间: " << avg_transfer_time << "μs" 
                  << ", 最小: " << stats_.min_transfer_time << "μs" 
                  << ", 最大: " << stats_.max_transfer_time << "μs" << std::endl;
    }
}

bool HV_EVS_Recorder::isRecording() const {
    return recording_;
}

void HV_EVS_Recorder::getRecordingStats(uint64_t& total_bytes, uint64_t& total_frames, uint64_t& avg_transfer_time) const {
    total_bytes = stats_.total_bytes;
    total_frames = stats_.total_frames;
    avg_transfer_time = (total_frames > 0) ? (stats_.total_transfer_time / total_frames) : 0;
}

void HV_EVS_Recorder::recordingThreadFunc() {
    int frame_drop_count = 0;
    uint64_t failed_transfers = 0;
    uint64_t successful_transfers = 0;
    uint64_t queue_full_warnings = 0;
    auto thread_start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "[Recording Thread] 录制线程已启动" << std::endl;
    
    // 缓存预热：进行几次空跑来预热缓存和分支预测器
    if (frame_drop_count == 0) {
        std::cout << "[Recording Thread] 正在进行缓存预热..." << std::endl;
        for (int warmup = 0; warmup < 3; ++warmup) {
            unsigned char* warmup_buffer = usb_buffer_pool_->acquire();
            // 模拟数据处理来预热缓存
            std::memset(warmup_buffer, 0x55, std::min(size_t(HV_BUF_LEN), size_t(65536)));
            usb_buffer_pool_->release(warmup_buffer);
        }
        std::cout << "[Recording Thread] 缓存预热完成" << std::endl;
    }
    
    while (recording_ && isOpen()) {
        // 从内存池获取缓冲区
        unsigned char* buffer = usb_buffer_pool_->acquire();
        int bytes;
        
        // 开始计时USB数据传输
        auto usb_start_time = std::chrono::high_resolution_clock::now();
        
        // 使用USB设备类进行数据传输
        bool success = usb_device_->bulkTransfer(event_endpoint_, buffer, HV_BUF_LEN, &bytes, 500);
        
        // 结束计时USB数据传输
        auto usb_end_time = std::chrono::high_resolution_clock::now();
        auto usb_duration = std::chrono::duration_cast<std::chrono::microseconds>(usb_end_time - usb_start_time);
        
        if (success && bytes > 0) {
            successful_transfers++;
            
            // 跳过前4帧以确保数据稳定（与hv_camera.cpp保持一致）
            if (frame_drop_count < 4) {
                ++frame_drop_count;
                std::cout << "[Recording Thread] 跳过第 " << frame_drop_count << " 帧 (数据稳定期)" << std::endl;
                usb_buffer_pool_->release(buffer);
                continue;
            }
            
            // 检查队列大小，防止内存积压
            size_t current_queue_size;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                current_queue_size = write_queue_.size();
            }
            
            // 如果队列过大，发出警告
            if (current_queue_size > 100) {
                queue_full_warnings++;
                std::cout << "[Recording Thread] 警告: 写入队列积压严重! 当前大小: " << current_queue_size 
                          << " (警告次数: " << queue_full_warnings << ")" << std::endl;
                
                // 如果队列过大，可以选择丢弃当前帧或等待
                if (current_queue_size > 200) {
                    std::cout << "[Recording Thread] 严重警告: 队列过载，丢弃当前帧!" << std::endl;
                    usb_buffer_pool_->release(buffer);
                    continue;
                }
            }
            
            // 时间戳分析（如果启用）
            if (timestamp_analysis_enabled_) {
                analyzeTimestamps(buffer, stats_.total_frames + 1);
            }
            
            // 创建数据副本用于异步写入
            unsigned char* data_copy = new unsigned char[bytes];
            std::memcpy(data_copy, buffer, bytes);
            
            // 将数据加入写入队列
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                write_queue_.emplace(data_copy, bytes);
            }
            queue_cv_.notify_one();
            
            // 更新性能统计
            stats_.total_frames++;
            stats_.total_bytes += bytes;
            stats_.total_transfer_time += usb_duration.count();
            
            uint64_t current_time = usb_duration.count();
            uint64_t expected_max = stats_.max_transfer_time;
            while (current_time > expected_max && 
                   !stats_.max_transfer_time.compare_exchange_weak(expected_max, current_time)) {
                expected_max = stats_.max_transfer_time;
            }
            
            uint64_t expected_min = stats_.min_transfer_time;
            while (current_time < expected_min && 
                   !stats_.min_transfer_time.compare_exchange_weak(expected_min, current_time)) {
                expected_min = stats_.min_transfer_time;
            }
            
            // 输出传输统计信息
            std::cout << "[Recording Thread] Frame " << stats_.total_frames << ": USB: " << usb_duration.count() 
                      << "μs, Bytes: " << bytes << ", Queue: " << current_queue_size;
            
            // 每100帧输出一次详细统计信息
            if (stats_.total_frames % 100 == 0) {
                uint64_t avg_transfer = stats_.total_transfer_time / stats_.total_frames;
                double success_rate = (double)successful_transfers / (successful_transfers + failed_transfers) * 100.0;
                std::cout << " | Avg: " << avg_transfer << "μs | Min: " << stats_.min_transfer_time 
                          << "μs | Max: " << stats_.max_transfer_time << "μs | Total MB: " 
                          << (stats_.total_bytes / 1024 / 1024) << " | Success Rate: " << std::fixed << std::setprecision(2) << success_rate << "%";
            }
            std::cout << std::endl;
        } else {
            failed_transfers++;
            std::cout << "[Recording Thread] USB Transfer FAILED: " << usb_duration.count() << "μs (失败次数: " << failed_transfers << ")" << std::endl;
            
            // 连续失败过多时发出严重警告
            if (failed_transfers % 10 == 0) {
                std::cout << "[Recording Thread] 严重警告: USB传输连续失败 " << failed_transfers << " 次!" << std::endl;
            }
            
            // 如果传输失败，等待一段时间再重试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 将缓冲区归还到内存池
        usb_buffer_pool_->release(buffer);
    }
    
    // 输出录制线程退出统计信息
    auto thread_end_time = std::chrono::high_resolution_clock::now();
    auto total_thread_duration = std::chrono::duration_cast<std::chrono::seconds>(thread_end_time - thread_start_time);
    double success_rate = (successful_transfers + failed_transfers > 0) ? 
                         (double)successful_transfers / (successful_transfers + failed_transfers) * 100.0 : 0.0;
    
    std::cout << "[Recording Thread] 线程退出 - 成功传输: " << successful_transfers 
              << ", 失败传输: " << failed_transfers 
              << ", 成功率: " << std::fixed << std::setprecision(2) << success_rate << "%" 
              << ", 队列警告次数: " << queue_full_warnings 
              << ", 总运行时间: " << total_thread_duration.count() << "s" << std::endl;
}

void HV_EVS_Recorder::writerThreadFunc() {
    uint64_t processed_buffers = 0;
    uint64_t total_write_time = 0;
    uint64_t max_queue_size = 0;
    auto thread_start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "[Writer Thread] 写入线程已启动" << std::endl;
    
    while (writer_running_ || !write_queue_.empty()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 记录等待开始时间
        auto wait_start_time = std::chrono::high_resolution_clock::now();
        
        // 等待数据或停止信号
        queue_cv_.wait(lock, [this] { return !write_queue_.empty() || !writer_running_; });
        
        // 记录等待结束时间
        auto wait_end_time = std::chrono::high_resolution_clock::now();
        auto wait_duration = std::chrono::duration_cast<std::chrono::microseconds>(wait_end_time - wait_start_time);
        
        // 记录当前队列大小
        size_t current_queue_size = write_queue_.size();
        if (current_queue_size > max_queue_size) {
            max_queue_size = current_queue_size;
        }
        
        // 如果队列为空且不再运行，输出调试信息并退出
        if (write_queue_.empty() && !writer_running_) {
            std::cout << "[Writer Thread] 队列为空且停止信号已收到，准备退出" << std::endl;
            break;
        }
        
        // 输出队列状态调试信息
        if (current_queue_size > 0) {
            std::cout << "[Writer Thread] 队列大小: " << current_queue_size 
                      << ", 等待时间: " << wait_duration.count() << "μs";
            
            // 队列过大时发出警告
            if (current_queue_size > 50) {
                std::cout << " [警告: 队列积压严重!]";
            }
            std::cout << std::endl;
        }
        
        // 处理队列中的所有数据
        uint32_t batch_count = 0;
        auto batch_start_time = std::chrono::high_resolution_clock::now();
        
        while (!write_queue_.empty()) {
            DataBuffer data_buffer = write_queue_.front();
            write_queue_.pop();
            lock.unlock();
            
            // 记录单次写入开始时间
            auto write_start_time = std::chrono::high_resolution_clock::now();
            
            // 写入文件
            {
                std::lock_guard<std::mutex> file_lock(file_mutex_);
                if (output_file_.is_open()) {
                    output_file_.write(reinterpret_cast<const char*>(data_buffer.data), data_buffer.size);
                    output_file_.flush(); // 确保数据立即写入磁盘
                } else {
                    std::cerr << "[Writer Thread] 错误: 输出文件未打开!" << std::endl;
                }
            }
            
            // 记录单次写入结束时间
            auto write_end_time = std::chrono::high_resolution_clock::now();
            auto write_duration = std::chrono::duration_cast<std::chrono::microseconds>(write_end_time - write_start_time);
            total_write_time += write_duration.count();
            
            // 释放数据副本
            delete[] data_buffer.data;
            
            processed_buffers++;
            batch_count++;
            
            // 如果单次写入时间过长，发出警告
            if (write_duration.count() > 10000) { // 超过10ms
                std::cout << "[Writer Thread] 警告: 写入耗时过长 " << write_duration.count() << "μs" << std::endl;
            }
            
            lock.lock();
        }
        
        // 输出批处理统计信息
        if (batch_count > 0) {
            auto batch_end_time = std::chrono::high_resolution_clock::now();
            auto batch_duration = std::chrono::duration_cast<std::chrono::microseconds>(batch_end_time - batch_start_time);
            
            std::cout << "[Writer Thread] 批处理完成: " << batch_count << " 个缓冲区, "
                      << "耗时: " << batch_duration.count() << "μs, "
                      << "平均: " << (batch_duration.count() / batch_count) << "μs/缓冲区" << std::endl;
        }
        
        // 每处理1000个缓冲区输出一次总体统计
        if (processed_buffers % 1000 == 0 && processed_buffers > 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - thread_start_time);
            uint64_t avg_write_time = total_write_time / processed_buffers;
            
            std::cout << "[Writer Thread] 统计信息 - 已处理: " << processed_buffers << " 个缓冲区, "
                      << "运行时间: " << total_duration.count() << "s, "
                      << "平均写入时间: " << avg_write_time << "μs, "
                      << "最大队列大小: " << max_queue_size << std::endl;
        }
    }
    
    // 输出线程退出统计信息
    auto thread_end_time = std::chrono::high_resolution_clock::now();
    auto total_thread_duration = std::chrono::duration_cast<std::chrono::seconds>(thread_end_time - thread_start_time);
    uint64_t avg_write_time = (processed_buffers > 0) ? (total_write_time / processed_buffers) : 0;
    
    std::cout << "[Writer Thread] 线程退出 - 总处理: " << processed_buffers << " 个缓冲区, "
              << "总运行时间: " << total_thread_duration.count() << "s, "
              << "平均写入时间: " << avg_write_time << "μs, "
              << "最大队列大小: " << max_queue_size << std::endl;
}

void HV_EVS_Recorder::analyzeTimestamps(const unsigned char* buffer, size_t block_index) {
    if (!timestamp_analysis_enabled_ || !timestamp_file_.is_open()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(timestamp_mutex_);
    
    // 按照hv_camera.cpp的方式处理数据块：每个偏移量为HV_SUB_FULL_BYTE_SIZE * 4
    for (size_t offset = 0; offset < HV_BUF_LEN; offset += HV_SUB_FULL_BYTE_SIZE * 4) {
        const uint64_t* pixelBufferPtr = reinterpret_cast<const uint64_t*>(buffer + offset);
        
        // 处理4个子帧
        for (int sub = 0; sub < 4; sub++) {
            const uint64_t* subPtr = pixelBufferPtr + (sub * HV_SUB_FULL_BYTE_SIZE / 8);
            
            uint64_t timestamp = 0;
            uint64_t subframe = 0;
            
            // 提取时间戳（与hv_camera.cpp中的processEventData保持一致）
            uint64_t buffer_data = subPtr[0];
            uint64_t raw_timestamp = (buffer_data >> 24) & 0xFFFFFFFFFF;
            uint64_t header_vec = buffer_data & 0xFFFFFF;
            
            if (header_vec == 0xFFFF) {
                buffer_data = subPtr[1];
                subframe = (buffer_data >> 44) & 0xF;
                timestamp = raw_timestamp / 200;  // 与hv_camera.cpp保持一致
                
                // 创建时间戳元数据
                TimestampMetadata ts_meta;
                ts_meta.block_index = block_index;
                ts_meta.sub_index = offset / (HV_SUB_FULL_BYTE_SIZE * 4);
                ts_meta.subframe = subframe;
                ts_meta.raw_timestamp = raw_timestamp;
                ts_meta.timestamp = timestamp;
                
                // 计算与前一个时间戳的差值（微秒）
                static uint64_t prev_timestamp = 0;
                uint64_t timestamp_diff = (prev_timestamp > 0) ? (timestamp - prev_timestamp) : 0;
                
                // 写入CSV格式
                timestamp_file_ << ts_meta.block_index << ","
                               << ts_meta.sub_index << ","
                               << ts_meta.subframe << ","
                               << ts_meta.raw_timestamp << ","
                               << ts_meta.timestamp << ","
                               << timestamp_diff << "\n";
                
                prev_timestamp = timestamp;
            }
        }
    }
    
    // 定期刷新文件
    if (block_index % 100 == 0) {
        timestamp_file_.flush();
    }
}

void HV_EVS_Recorder::initTimestampFile() {
    // 生成时间戳文件名
    size_t dot_pos = output_filename_.find_last_of('.');
    if (dot_pos != std::string::npos) {
        timestamp_filename_ = output_filename_.substr(0, dot_pos) + "_timestamps.csv";
    } else {
        timestamp_filename_ = output_filename_ + "_timestamps.csv";
    }
    
    // 打开时间戳文件
    timestamp_file_.open(timestamp_filename_);
    if (!timestamp_file_.is_open()) {
        std::cerr << "[Timestamp] 无法创建时间戳文件: " << timestamp_filename_ << std::endl;
        timestamp_analysis_enabled_ = false;
        return;
    }
    
    // 写入CSV头部
    timestamp_file_ << "block_index,sub_index,subframe,raw_timestamp,processed_timestamp,timestamp_diff_us\n";
    
    std::cout << "[Timestamp] 时间戳分析已启用，输出文件: " << timestamp_filename_ << std::endl;
}

void HV_EVS_Recorder::closeTimestampFile() {
    std::lock_guard<std::mutex> lock(timestamp_mutex_);
    
    if (timestamp_file_.is_open()) {
        timestamp_file_.close();
        std::cout << "[Timestamp] 时间戳文件已关闭: " << timestamp_filename_ << std::endl;
    }
}

} // namespace hv
