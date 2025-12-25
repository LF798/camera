#include "hv_evt2_codec.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace hv {
namespace evt2 {

// EventCDEncoder implementation
void EventCDEncoder::encode(RawEvent* raw_event) {
    RawEventCD* raw_cd_event = reinterpret_cast<RawEventCD*>(raw_event);
    raw_cd_event->x = x;
    raw_cd_event->y = y;
    raw_cd_event->timestamp = t & 0x3F;  // Lower 6 bits
    raw_cd_event->type = p ? static_cast<uint8_t>(EventTypes::CD_ON) : static_cast<uint8_t>(EventTypes::CD_OFF);
}

void EventCDEncoder::setEvent(unsigned short x_coord, unsigned short y_coord, short polarity, Timestamp timestamp) {
    x = x_coord;
    y = y_coord;
    p = polarity;
    t = timestamp;
}

// EventTriggerEncoder implementation
void EventTriggerEncoder::encode(RawEvent* raw_event) {
    RawEventExtTrigger* raw_trigger_event = reinterpret_cast<RawEventExtTrigger*>(raw_event);
    raw_trigger_event->timestamp = t & 0x3F;  // Lower 6 bits
    raw_trigger_event->id = id;
    raw_trigger_event->value = p;
    raw_trigger_event->type = static_cast<uint8_t>(EventTypes::EXT_TRIGGER);
    raw_trigger_event->unused1 = 0;
    raw_trigger_event->unused2 = 0;
}

void EventTriggerEncoder::setEvent(short polarity, short trigger_id, Timestamp timestamp) {
    p = polarity;
    id = trigger_id;
    t = timestamp;
}

// EventTimeEncoder implementation
EventTimeEncoder::EventTimeEncoder(Timestamp base) 
    : th((base / TH_NEXT_STEP) * TH_NEXT_STEP) {
}

void EventTimeEncoder::reset(Timestamp base) {
    th = (base / TH_NEXT_STEP) * TH_NEXT_STEP;
}

void EventTimeEncoder::encode(RawEvent* raw_event) {
    RawEventTime* ev_th = reinterpret_cast<RawEventTime*>(raw_event);
    ev_th->timestamp = th >> N_LOWER_BITS_TH;
    ev_th->type = static_cast<uint8_t>(EventTypes::EVT_TIME_HIGH);
    th += TH_NEXT_STEP;
}

// EVT2Decoder implementation
EVT2Decoder::EVT2Decoder() 
    : current_time_base_(0), first_time_base_set_(false), n_time_high_loop_(0) {
}

size_t EVT2Decoder::decode(const uint8_t* buffer, size_t buffer_size,
                          std::vector<Metavision::EventCD>& cd_events,
                          std::vector<std::tuple<short, short, Timestamp>>* trigger_events) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    
    cd_events.clear();
    if (trigger_events) {
        trigger_events->clear();
    }
    
    const RawEvent* current_word = reinterpret_cast<const RawEvent*>(buffer);
    const RawEvent* last_word = current_word + buffer_size / sizeof(RawEvent);
    
    size_t events_decoded = 0;
    
    // Skip events until we find the first time high if not set
    for (; !first_time_base_set_ && current_word != last_word; ++current_word) {
        EventTypes type = static_cast<EventTypes>(current_word->type);
        if (type == EventTypes::EVT_TIME_HIGH) {
            const RawEventTime* ev_time_high = reinterpret_cast<const RawEventTime*>(current_word);
            current_time_base_ = (Timestamp(ev_time_high->timestamp) << 6);
            first_time_base_set_ = true;
            break;
        }
    }
    
    // Process remaining events
    for (; current_word != last_word; ++current_word) {
        processEvent(current_word, cd_events, trigger_events);
        events_decoded++;
    }
    
    return events_decoded;
}

void EVT2Decoder::reset() {
    current_time_base_ = 0;
    first_time_base_set_ = false;
    n_time_high_loop_ = 0;
}

void EVT2Decoder::processEvent(const RawEvent* raw_event,
                              std::vector<Metavision::EventCD>& cd_events,
                              std::vector<std::tuple<short, short, Timestamp>>* trigger_events) {
    EventTypes type = static_cast<EventTypes>(raw_event->type);
    
    switch (type) {
        case EventTypes::CD_OFF: {
            const RawEventCD* ev_cd = reinterpret_cast<const RawEventCD*>(raw_event);
            Timestamp t = current_time_base_ + ev_cd->timestamp;
            
            Metavision::EventCD event;
            event.x = ev_cd->x;
            event.y = ev_cd->y;
            event.p = 0;  // OFF event
            event.t = t;
            cd_events.push_back(event);
            break;
        }
        
        case EventTypes::CD_ON: {
            const RawEventCD* ev_cd = reinterpret_cast<const RawEventCD*>(raw_event);
            Timestamp t = current_time_base_ + ev_cd->timestamp;
            
            Metavision::EventCD event;
            event.x = ev_cd->x;
            event.y = ev_cd->y;
            event.p = 1;  // ON event
            event.t = t;
            cd_events.push_back(event);
            break;
        }
        
        case EventTypes::EVT_TIME_HIGH: {
            // Time high loop detection constants
            static constexpr Timestamp MaxTimestampBase = ((Timestamp(1) << 28) - 1) << 6;  // 17179869120us
            static constexpr Timestamp TimeLoop = MaxTimestampBase + (1 << 6);  // 17179869184us
            static constexpr Timestamp LoopThreshold = (10 << 6);
            
            const RawEventTime* ev_time_high = reinterpret_cast<const RawEventTime*>(raw_event);
            Timestamp new_time_base = (Timestamp(ev_time_high->timestamp) << 6);
            new_time_base += n_time_high_loop_ * TimeLoop;
            
            if ((current_time_base_ > new_time_base) &&
                (current_time_base_ - new_time_base >= MaxTimestampBase - LoopThreshold)) {
                // Time High loop detected
                new_time_base += TimeLoop;
                ++n_time_high_loop_;
            }
            
            current_time_base_ = new_time_base;
            break;
        }
        
        case EventTypes::EXT_TRIGGER: {
            if (trigger_events) {
                const RawEventExtTrigger* ev_trigg = reinterpret_cast<const RawEventExtTrigger*>(raw_event);
                Timestamp t = current_time_base_ + ev_trigg->timestamp;
                
                trigger_events->emplace_back(ev_trigg->value, ev_trigg->id, t);
            }
            break;
        }
        
        default:
            // Unknown event type, skip
            break;
    }
}

// Utility functions implementation
namespace utils {

bool parseEVT2Header(const std::vector<std::string>& header_lines, EVT2Header& header) {
    header.width = 0;
    header.height = 0;
    header.integrator = "Shimeta";
    header.date = "Unknown";
    header.format_line = "";
    
    for (const auto& line : header_lines) {
        if (line.empty() || line[0] != '%') {
            continue;
        }
        
        // 处理 "% Date" 格式（首字母大写）
        if (line.substr(0, 6) == "% date" || line.substr(0, 6) == "% Date") {
            header.date = line.substr(7);
        } else if (line.substr(0, 9) == "% format ") {
            header.format_line = line.substr(9);
            
            // Parse format line for width and height
            std::istringstream sf(header.format_line);
            std::string format_name;
            std::getline(sf, format_name, ';');
            
            if (format_name == "EVT2") {
                while (!sf.eof()) {
                    std::string option;
                    std::getline(sf, option, ';');
                    
                    if (option.empty()) continue;
                    
                    std::istringstream so(option);
                    std::string name, value;
                    std::getline(so, name, '=');
                    std::getline(so, value, '=');
                    
                    if (name == "width") {
                        try {
                            header.width = std::stoul(value);
                        } catch (...) {
                            // 忽略解析错误
                        }
                    } else if (name == "height") {
                        try {
                            header.height = std::stoul(value);
                        } catch (...) {
                            // 忽略解析错误
                        }
                    }
                }
            }
        } else if (line.substr(0, 18) == "% integrator_name") {
            header.integrator = line.substr(19);
        } else if (line.substr(0, 11) == "% geometry ") {
            // Alternative geometry format
            try {
                std::istringstream sg(line.substr(11));
                std::string sw, sh;
                std::getline(sg, sw, 'x');
                std::getline(sg, sh);
                header.width = std::stoul(sw);
                header.height = std::stoul(sh);
            } catch (...) {
                // 忽略解析错误
            }
        } else if (line.substr(0, 7) == "% evt ") {
            // 处理 "% evt 2.0" 格式，设置默认格式
            header.format_line = "EVT2";
        }
    }
    
    // 如果无法解析到宽高，使用默认值 640x512
    if (header.width == 0 || header.height == 0) {
        header.width = 640;
        header.height = 512;
        std::cout << "Warning: Could not parse image dimensions from header, using default 640x512" << std::endl;
    }
    
    // 总是返回true，因为我们现在有默认值
    return true;
}

std::vector<std::string> generateEVT2Header(const EVT2Header& header) {
    std::vector<std::string> header_lines;
    
    // Date
    if (!header.date.empty()) {
        header_lines.push_back("% date " + header.date);
    } else {
        const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        const struct std::tm* ptm = std::localtime(&tt);
        
        std::ostringstream date_stream;
        date_stream << "% date " << std::put_time(ptm, "%Y-%m-%d %H:%M:%S");
        header_lines.push_back(date_stream.str());
    }
    
    // Format line
    std::ostringstream format_stream;
    format_stream << "% format EVT2;width=" << header.width << ";height=" << header.height;
    header_lines.push_back(format_stream.str());
    
    // Integrator
    header_lines.push_back("% integrator_name " + header.integrator);
    
    // End marker
    header_lines.push_back("% end");
    
    return header_lines;
}

std::vector<std::string> generateEVT2Header(uint32_t width, uint32_t height, const std::string& integrator) {
    std::vector<std::string> header_lines;
    
    // Generate date
    const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const struct std::tm* ptm = std::localtime(&tt);
    
    std::ostringstream date_stream;
    date_stream << "% date " << std::put_time(ptm, "%Y-%m-%d %H:%M:%S");
    header_lines.push_back(date_stream.str());
    
    // Format line
    std::ostringstream format_stream;
    format_stream << "% format EVT2;width=" << width << ";height=" << height;
    header_lines.push_back(format_stream.str());
    
    // Integrator
    header_lines.push_back("% integrator_name " + integrator);
    
    // End marker
    header_lines.push_back("% end");
    
    return header_lines;
}

size_t convertToEVT2(const std::vector<Metavision::EventCD>& events,
                     std::vector<uint8_t>& raw_data,
                     EventTimeEncoder& time_encoder) {
    if (events.empty()) {
        raw_data.clear();
        return 0;
    }
    
    std::vector<RawEvent> raw_events;
    raw_events.reserve(events.size() + events.size() / 1000);  // Reserve extra space for time high events
    
    EventCDEncoder cd_encoder;
    
    // Add initial time high event
    RawEvent time_event;
    time_encoder.encode(&time_event);
    raw_events.push_back(time_event);
    
    for (const auto& event : events) {
        // Check if we need to insert time high events
        while (event.t >= time_encoder.getNextTimeHigh()) {
            RawEvent th_event;
            time_encoder.encode(&th_event);
            raw_events.push_back(th_event);
        }
        
        // Encode CD event
        cd_encoder.setEvent(event.x, event.y, event.p, event.t);
        RawEvent cd_event;
        cd_encoder.encode(&cd_event);
        raw_events.push_back(cd_event);
    }
    
    // Convert RawEvent vector to byte vector
    raw_data.resize(raw_events.size() * sizeof(RawEvent));
    std::memcpy(raw_data.data(), raw_events.data(), raw_data.size());
    
    return events.size();  // Return number of original events converted
}

} // namespace utils

} // namespace evt2
} // namespace hv