#include "hv_event_writer.h"
#include <algorithm>
#include <iostream>
#include <cstring>

namespace hv {

HVEventWriter::HVEventWriter() 
    : is_open_(false), event_count_(0) {
    write_buffer_.reserve(1000000);  // 1MB buffer
}

HVEventWriter::~HVEventWriter() {
    close();
}

bool HVEventWriter::open(const std::string& filename, uint32_t width, uint32_t height, uint64_t start_timestamp) {
    if (is_open_) {
        return false;
    }
    
    file_.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return false;
    }
    
    // 初始化头部
    header_.width = width;
    header_.height = height;
    header_.start_timestamp = start_timestamp;
    
    // 初始化时间编码器
    time_encoder_ = std::make_unique<evt2::EventTimeEncoder>(start_timestamp);
    
    // 写入EVT2头部
    writeHeader();
    
    is_open_ = true;
    event_count_ = 0;
    
    return true;
}

void HVEventWriter::close() {
    if (!is_open_) {
        return;
    }
    
    // 刷新缓冲区
    flushBuffer();
    
    file_.close();
    is_open_ = false;
    event_count_ = 0;
}

bool HVEventWriter::isOpen() const {
    return is_open_;
}

size_t HVEventWriter::writeEvents(const std::vector<Metavision::EventCD>& events) {
    if (!is_open_ || events.empty() || !time_encoder_) {
        return 0;
    }
    
    // 转换为EVT2格式
    std::vector<uint8_t> evt2_data;
    size_t converted_count = evt2::utils::convertToEVT2(events, evt2_data, *time_encoder_);
    
    if (evt2_data.empty()) {
        return 0;
    }
    
    // 写入原始数据
    writeRawData(evt2_data);
    
    event_count_ += converted_count;
    return converted_count;
}

void HVEventWriter::flush() {
    if (is_open_) {
        flushBuffer();
    }
}

uint64_t HVEventWriter::getWrittenEventCount() const {
    return event_count_;
}

size_t HVEventWriter::getFileSize() const {
    if (!file_.is_open()) {
        return 0;
    }
    
    // 获取当前文件位置作为文件大小
    std::streampos current_pos = const_cast<std::ofstream&>(file_).tellp();
    return static_cast<size_t>(current_pos) + write_buffer_.size();
}

void HVEventWriter::writeHeader() {
    if (!file_.is_open()) {
        return;
    }
    
    std::vector<std::string> header_lines = evt2::utils::generateEVT2Header(header_.width, header_.height, "Shimeta");
    
    for (const auto& line : header_lines) {
        file_ << line << "\n";
    }
}

void HVEventWriter::writeRawData(const std::vector<uint8_t>& data) {
    // 添加到缓冲区
    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    
    // 如果缓冲区太大，刷新到文件
    if (write_buffer_.size() > 500000) {  // 500KB
        flushBuffer();
    }
}

void HVEventWriter::flushBuffer() {
    if (!write_buffer_.empty() && file_.is_open()) {
        file_.write(reinterpret_cast<const char*>(write_buffer_.data()), write_buffer_.size());
        write_buffer_.clear();
    }
}

} // namespace hv