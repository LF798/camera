#!/usr/bin/env python3
"""
EVS RTSP Client - Python示例
接收EVT2事件流并可视化

依赖：
    pip install opencv-python numpy

使用：
    python3 rtsp_client_python.py rtsp://192.168.1.100:554/evs_stream
"""

import cv2
import numpy as np
import struct
import sys
import time
from collections import deque

class EVT2Decoder:
    """EVT2事件解码器"""
    
    # EVT2事件类型
    EVT2_TYPE_CD_OFF = 0x00
    EVT2_TYPE_CD_ON = 0x01
    EVT2_TYPE_TIME_HIGH = 0x08
    
    def __init__(self):
        self.time_high = 0
    
    def decode_event(self, raw_event):
        """解码单个EVT2事件（32位）"""
        event_type = (raw_event >> 28) & 0xF
        
        if event_type in [self.EVT2_TYPE_CD_OFF, self.EVT2_TYPE_CD_ON]:
            # CD事件：坐标 + 时间戳低位 + 极性
            y = (raw_event >> 0) & 0x7FF
            x = (raw_event >> 11) & 0x7FF
            t_low = (raw_event >> 22) & 0x3F
            polarity = event_type
            
            timestamp = (self.time_high << 6) | t_low
            
            return {
                'type': 'CD',
                'x': x,
                'y': y,
                'timestamp': timestamp,
                'polarity': polarity
            }
        
        elif event_type == self.EVT2_TYPE_TIME_HIGH:
            # 时间高位事件
            self.time_high = raw_event & 0x0FFFFFFF
            return {
                'type': 'TIME_HIGH',
                'timestamp': self.time_high
            }
        
        return None
    
    def decode_stream(self, data):
        """解码EVT2数据流"""
        events = []
        
        # 每4字节一个事件
        for i in range(0, len(data), 4):
            if i + 4 > len(data):
                break
            
            raw_event = struct.unpack('<I', data[i:i+4])[0]
            evt = self.decode_event(raw_event)
            
            if evt and evt['type'] == 'CD':
                events.append(evt)
        
        return events


class EventVisualizer:
    """事件流可视化器"""
    
    def __init__(self, width=384, height=304, decay_rate=0.95):
        self.width = width
        self.height = height
        self.decay_rate = decay_rate
        
        # 创建累积图像
        self.image = np.zeros((height, width, 3), dtype=np.uint8)
        self.event_count = 0
        self.frame_count = 0
        
        # 统计信息
        self.fps_deque = deque(maxlen=30)
        self.last_time = time.time()
    
    def add_events(self, events):
        """添加事件到图像"""
        for evt in events:
            x, y = evt['x'], evt['y']
            
            # 边界检查
            if 0 <= x < self.width and 0 <= y < self.height:
                # 根据极性设置颜色
                if evt['polarity'] == 1:  # ON事件
                    color = (0, 255, 0)  # 绿色
                else:  # OFF事件
                    color = (255, 0, 0)  # 蓝色
                
                # 在图像上绘制点
                cv2.circle(self.image, (x, y), 1, color, -1)
                self.event_count += 1
    
    def get_frame(self):
        """获取当前可视化帧"""
        # 计算FPS
        current_time = time.time()
        fps = 1.0 / (current_time - self.last_time + 1e-6)
        self.fps_deque.append(fps)
        self.last_time = current_time
        avg_fps = np.mean(self.fps_deque)
        
        # 复制图像用于显示
        display_image = self.image.copy()
        
        # 添加统计信息
        info_text = [
            f"Frame: {self.frame_count}",
            f"Events: {self.event_count}",
            f"FPS: {avg_fps:.1f}"
        ]
        
        y_offset = 20
        for text in info_text:
            cv2.putText(display_image, text, (10, y_offset),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)
            y_offset += 20
        
        # 衰减图像
        self.image = (self.image * self.decay_rate).astype(np.uint8)
        
        self.frame_count += 1
        self.event_count = 0
        
        return display_image
    
    def reset(self):
        """重置图像"""
        self.image = np.zeros((self.height, self.width, 3), dtype=np.uint8)
        self.event_count = 0


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 rtsp_client_python.py <rtsp_url>")
        print("Example: python3 rtsp_client_python.py rtsp://192.168.1.100:554/evs_stream")
        sys.exit(1)
    
    rtsp_url = sys.argv[1]
    
    print("=" * 60)
    print("  EVS RTSP Client - Python")
    print("=" * 60)
    print(f"Connecting to: {rtsp_url}")
    print("Press 'q' to quit, 'r' to reset image")
    print()
    
    # 创建解码器和可视化器
    decoder = EVT2Decoder()
    visualizer = EventVisualizer()
    
    # 打开RTSP流
    cap = cv2.VideoCapture(rtsp_url)
    
    if not cap.isOpened():
        print(f"ERROR: Cannot open RTSP stream: {rtsp_url}")
        sys.exit(1)
    
    print("Connected! Receiving events...")
    print()
    
    try:
        while True:
            # 读取帧（实际包含RTP payload）
            ret, frame = cap.read()
            
            if not ret:
                print("WARNING: Failed to read frame, reconnecting...")
                time.sleep(1)
                cap = cv2.VideoCapture(rtsp_url)
                continue
            
            # 注意：OpenCV读取的是解码后的帧
            # 实际EVT2数据需要自定义RTP解析器
            # 这里简化处理，假设frame包含EVT2数据
            
            # TODO: 实现自定义RTP解析器
            # 目前OpenCV无法直接解析EVT2 RTP payload
            # 建议使用GStreamer或自定义UDP接收器
            
            print(f"Received frame: {frame.shape if frame is not None else 'None'}")
            
            # 临时方案：显示空白图像（需要配合自定义RTP解析器）
            vis_frame = visualizer.get_frame()
            cv2.imshow('EVS Event Stream', vis_frame)
            
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('r'):
                visualizer.reset()
                print("Image reset")
    
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    
    finally:
        cap.release()
        cv2.destroyAllWindows()
        print("Client stopped")


if __name__ == '__main__':
    main()
