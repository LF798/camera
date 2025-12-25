#include "hv_event_reader.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace hv {

HVEventReader::HVEventReader() : is_open_(false), data_start_pos_(0) {
    read_buffer_.reserve(1000000);  // 1MB buffer
}

HVEventReader::~HVEventReader() {
    close();
}

bool HVEventReader::open(const std::string& filename) {
    close();
    
    file_.open(filename, std::ios::binary);
    if (!file_.is_open()) {
        return false;
    }
    
    if (!readHeader()) {
        close();
        return false;
    }
    
    data_start_pos_ = file_.tellg();
    is_open_ = true;
    return true;
}

void HVEventReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    is_open_ = false;
}

bool HVEventReader::isOpen() const {
    return is_open_;
}

const evt2::EVT2Header& HVEventReader::getHeader() const {
    return header_;
}

size_t HVEventReader::readEvents(size_t num_events, std::vector<Metavision::EventCD>& events) {
    if (!is_open_) {
        return 0;
    }
    
    events.clear();
    size_t total_decoded = 0;
    
    while (total_decoded < num_events && !file_.eof()) {
        size_t bytes_to_read = std::min(size_t(100000), (num_events - total_decoded) * 4);
        size_t bytes_read = readRawData(read_buffer_, bytes_to_read);
        
        if (bytes_read == 0) {
            break;
        }
        
        std::vector<Metavision::EventCD> batch_events;
        decoder_.decode(read_buffer_.data(), bytes_read, batch_events, nullptr);
        
        for (const auto& event : batch_events) {
            if (events.size() < num_events) {
                events.push_back(event);
                total_decoded++;
            } else {
                break;
            }
        }
    }
    
    return total_decoded;
}

size_t HVEventReader::readAllEvents(std::vector<Metavision::EventCD>& events) {
    if (!is_open_) {
        return 0;
    }
    
    reset();
    events.clear();
    
    std::vector<Metavision::EventCD> batch_events;
    size_t total_events = 0;
    
    while (!file_.eof()) {
        size_t batch_count = readEvents(10000, batch_events);
        if (batch_count == 0) {
            break;
        }
        
        events.insert(events.end(), batch_events.begin(), batch_events.end());
        total_events += batch_count;
    }
    
    return total_events;
}

size_t HVEventReader::streamEvents(size_t batch_size, EventCallback callback) {
    if (!is_open_ || !callback) {
        return 0;
    }
    
    reset();
    std::vector<Metavision::EventCD> batch_events;
    size_t total_processed = 0;
    
    while (true) {
        size_t read_count = readEvents(batch_size, batch_events);
        if (read_count == 0) break;
        
        callback(batch_events);
        total_processed += read_count;
    }
    
    return total_processed;
}

void HVEventReader::reset() {
    if (is_open_) {
        file_.clear();
        file_.seekg(data_start_pos_);
        decoder_.reset();
    }
}

std::pair<uint32_t, uint32_t> HVEventReader::getImageSize() const {
    return std::make_pair(header_.width, header_.height);
}

bool HVEventReader::readHeader() {
    file_.seekg(0, std::ios::beg);
    
    std::vector<std::string> header_lines;
    std::string line;
    
    // 读取头部行直到遇到 "% end" 或非 '%' 开头的行
    while (std::getline(file_, line)) {
        if (line.empty()) {
            continue;
        }
        
        if (line[0] == '%') {
            header_lines.push_back(line);
            if (line == "% end") {
                break;
            }
        } else {
            // 遇到非头部行，回退
            file_.seekg(-static_cast<std::streamoff>(line.length() + 1), std::ios::cur);
            break;
        }
    }
    
    // 解析头部
    return evt2::utils::parseEVT2Header(header_lines, header_);
}

size_t HVEventReader::readRawData(std::vector<uint8_t>& buffer, size_t max_bytes) {
    buffer.resize(max_bytes);
    file_.read(reinterpret_cast<char*>(buffer.data()), max_bytes);
    size_t bytes_read = file_.gcount();
    buffer.resize(bytes_read);
    return bytes_read;
}

} // namespace hv