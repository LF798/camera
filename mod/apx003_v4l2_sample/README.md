

## 使用说明
目前是V4来驱动先采集，然后再进行传输，暂时是先接受端，后传输端，后续会修改
使用TCP传输，首先在PC上运行接收端，然后相机上运行发送端进行数据传输

运行接收端：
./evs_tcp_receiver.exe <PORT>
例如：./evs_tcp_receiver.exe 8888


运行相机发送端：
./evs_multithread_sender.exe <IP> <PORT> <WINDOW_MS> 
例如：./evs_multithread_sender.exe 127.0.0.1 8888 20





**核心文件**：
- **发送端**：`evsGetdata_multithread.c` (RK3588)
- **接收端**：`evs_tcp_receiver.c` (PC)

**系统架构**：多线程流水线架构，实现V4L2采集、事件提取、EVT2编码和TCP传输的解耦

---

## 一、系统架构总览

### 1.1 发送端多线程架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    EVS Multi-threaded Sender                     │
│                        (RK3588 - ARM)                            │
└─────────────────────────────────────────────────────────────────┘

Thread 1: V4L2 Acquisition (高优先级)
   ↓ 采集1MB原始MPP包 (4096x256)
   ├─ 每帧包含32个子帧 (8个完整帧/包， 4个子帧/完整帧)
   └─ memcpy拷贝到帧缓冲区
   
   ↓ [Queue 1: V4L2 Frame Queue = 8]
   
Thread 2: Event Extraction
   ↓ 解析32个子帧头部
   ├─ 按时间戳排序
   ├─ 判断时间窗口完成条件
   └─ 零拷贝提取事件到窗口缓冲区

   ↓ [Queue 2: Encoding Queue = 20]
   
Thread 3-4: EVT2 Encoding (2个并行编码线程)
   ↓ EVT2差分编码压缩
   ├─ 时间差分 (ΔT)
   ├─ 空间差分 (ΔX, ΔY)
   
   ↓ [Queue 3: Transmission Queue = 50]
   
Thread 5: TCP Transmission
   ↓ 发送EVT2编码数据
   ├─ 自动重连机制
   └─ 流量控制
   
   ↓ [TCP Socket]
   
接收端 (PC - x86_64)
```

## 二、发送端详细流程

### 2.1 配置参数

```c
// V4L2设备配置
#define DVS_DEV_NAME  "/dev/video1"
#define DVS_IMG_WIDTH  4096
#define DVS_IMG_HEIGHT 256
#define DVS_PIXEL_FMT  V4L2_PIX_FMT_SBGGR8

// 缓冲区配置
#define BUFFER_COUNT 4              // V4L2 mmap缓冲区数量

// 队列容量
#define V4L2_FRAME_QUEUE_SIZE 8     // V4L2帧队列
#define ENCODING_QUEUE_SIZE 20      // 编码队列
#define TRANSMISSION_QUEUE_SIZE 50  // 传输队列

// 线程配置
#define NUM_ENCODING_THREADS 2      // 编码线程数

// 时间窗口
#define DEFAULT_WINDOW_SIZE_MS 20   // 默认20ms窗口

// 子帧配置
#define MAX_EVENTS_PER_SUBFRAME (384 * 304)  // 每子帧最大事件数
```

### 2.2 数据结构

#### 2.2.1 原始帧缓冲区（V4L2队列）

```c
typedef struct {
    uint8_t* data;             // 帧数据拷贝 (1MB)
    size_t data_size;          // 数据大小
    uint32_t frame_index;      // 帧索引
    struct timeval timestamp;  // 采集时间戳
} RawFrameBuffer_t;
```

#### 2.2.2 子帧信息结构

```c
typedef struct {
    int physical_index;        // 物理位置（0-31）
    int subframe_id;           // 子帧ID（0-31）
    uint64_t timestamp;        // 时间戳（微秒）
    const uint8_t* data_ptr;   // 数据指针（指向V4L2 buffer）
} SubframeInfo_t;
```

**说明**：
- **1个V4L2包** = **32个子帧** = **1 MB**
- 每个子帧 = 32 KB = 4096×8字节
- 子帧按**时间戳排序**后处理  （实际是按照时间戳顺序进行传输的，用于测试检测）

#### 2.2.3 事件结构

```c
typedef struct {
    uint16_t x;          // X坐标 (0-4095)
    uint16_t y;          // Y坐标 (0-255)
    uint64_t timestamp;  // 时间戳（微秒）
    int8_t polarity;     // 极性 (+1/-1)
} EVSEvent_t;
```

### 2.3 线程1：V4L2采集线程

**流程图**：

```
┌────────────────────────────────────┐
│  V4L2 Acquisition Thread           │
└────────────────────────────────────┘
         │
         ↓
    [等待V4L2帧就绪]
         │
         ↓ VIDIOC_DQBUF (出队)
    [获取帧索引 frame_idx]
         │
         ↓
    [分配帧缓冲区]
    RawFrameBuffer_t* buffer
         │
         ↓
    [memcpy 1MB数据]
    V4L2 mmap → buffer->data
         │
         ↓ VIDIOC_QBUF (重新入队)
    [归还V4L2缓冲区]
         │
         ↓
    [非阻塞推送到队列]
    queue_try_push(V4L2_queue, buffer)
         │
         ├─成功→ 统计：frames_captured++
         └─失败→ 统计：frames_dropped++, 释放buffer
```


### 2.4 线程2：事件提取线程

**流程图**：

```
┌────────────────────────────────────┐
│  Extraction Thread                 │
└────────────────────────────────────┘
         │
         ↓
    [阻塞获取原始帧]
    queue_pop(V4L2_queue)
         │
         ↓
    [解析32个子帧头部]
    parse_subframe_headers()
    ├─ 提取时间戳
    ├─ 提取子帧ID
    └─ 按时间戳排序
         │
         ↓
    [for i=0 to 31: 处理子帧]
         │
         ├─→ [4a. 空间检查]
         │   available < MAX_EVENTS?
         │   └─YES→ 强制完成窗口
         │           time_window_force_complete()
         │
         ├─→ [4b. 时间检查]
         │   时间戳超出窗口?
         │   └─YES→ 完成窗口
         │           time_window_complete()
         │
         └─→ [5. 提取事件]
             extract_and_accumulate_subframe()
             └─ 零拷贝提取到时间窗口缓冲区
         │
         ↓
    [完成窗口 → Encoding Queue]
```

**关键代码**：

```c
void* extraction_thread_func(void* arg) {
    while (g_running) {
        // 1. 从队列获取原始帧
        RawFrameBuffer_t* frame_buffer = queue_pop(g_v4l2_frame_queue);
        
        // 2. 解析所有32个子帧头部并按时间戳排序
        SubframeInfo_t subframes[32];
        parse_subframe_headers(frame_buffer->data, subframes);
        
        pthread_mutex_lock(&g_window_mutex);
        
        // 3. 按时间戳顺序处理所有32个子帧
        for (int i = 0; i < 32; i++) {
            SubframeInfo_t* sf = &subframes[i];
            
            // 4a. 空间检查：缓冲区空间不足则强制完成
            if (available < MAX_EVENTS_PER_SUBFRAME) {
                EventWindowBuffer_t* completed = 
                    time_window_force_complete(g_time_window);
                queue_push(g_encoding_queue, completed);
            }
            
            // 4b. 时间检查：子帧时间戳超出窗口
            if (time_window_will_complete(g_time_window, sf->timestamp)) {
                EventWindowBuffer_t* completed = 
                    time_window_complete(g_time_window);
                queue_push(g_encoding_queue, completed);
            }
            
            // 5. 提取事件到当前窗口
            extract_and_accumulate_subframe(
                sf->data_ptr, sf->subframe_id, g_time_window);
        }
        
        pthread_mutex_unlock(&g_window_mutex);
        raw_frame_buffer_destroy(frame_buffer);
    }
}
```


#### 2.4.2 时间窗口机制

**窗口完成条件**：

1. **时间触发**：子帧时间戳 ≥ (窗口起始时间 + 窗口大小)
2. **空间触发**：窗口缓冲区剩余空间 < 116K事件

**时间窗口状态机**：
```
[空闲] 
  ↓ 收到第一个子帧
[累积中] start_time = subframe.timestamp
  ↓ 持续累积事件
  │
  ├─→ [时间到期] timestamp >= start + window_size
  │   └→ complete() → 生成窗口
  │
  └─→ [空间不足] available < MAX_EVENTS
      └→ force_complete() → 强制完成
  ↓
[新窗口] 继续累积...
```

### 2.5 线程3-4：编码线程（并行）

**流程图**：

```
┌────────────────────────────────────┐
│  Encoding Thread (x2)              │
└────────────────────────────────────┘
         │
         ↓
    [阻塞获取窗口]
    queue_pop(encoding_queue)
         │
         ↓
    [EVT2编码]
    evt2_encoder_encode()
    ├─ 时间差分：ΔT = T[i] - T[i-1]
    ├─ 空间差分：ΔX, ΔY
    ├─ 极性编码
    └─ 变长编码
         │
         ↓
    [压缩统计]
    原始：event_count × 12 bytes
    压缩：encoded_size bytes
    比例：70-90% 压缩
         │
         ↓
    [创建编码数据包]
    EncodedWindowPacket_t
         │
         ↓
    [推送到传输队列]
    queue_push(transmission_queue)
```


### 2.6 线程5：TCP传输线程

**流程图**：

```
┌────────────────────────────────────┐
│  Transmission Thread               │
└────────────────────────────────────┘
         │
         ↓
    [初始连接TCP服务器]
    evs_tcp_sender_connect()
    最多重试5次
         │
         ↓
    [主循环]
         │
         ↓
    [阻塞获取编码数据包]
    queue_pop(transmission_queue)
         │
         ↓
    [发送EVT2数据]
    evs_tcp_sender_send_evt2_data()
         │
         ├─成功→ 统计：events_sent++
         │
         └─失败→ [重试机制]
                 ├─ 断开连接
                 ├─ 等待1秒
                 ├─ 重新连接
                 └─ 最多重试3次
```

**关键代码**：

```c
void* transmission_thread_func(void* arg) {
    // 初始连接
    while (g_running && attempts < 5) {
        if (evs_tcp_sender_connect(g_tcp_sender) == 0) break;
        sleep(3);
    }
    
    // 传输循环
    while (true) {
        EncodedWindowPacket_t* packet = 
            queue_pop(g_transmission_queue);
        if (!packet) break;
        
        // 发送EVT2数据（带重试）
        for (int retry = 0; retry < 3; retry++) {
            int sent = evs_tcp_sender_send_evt2_data(
                g_tcp_sender,
                packet->encoded_data,
                packet->encoded_data_size,
                packet->original_event_count
            );
            
            if (sent >= 0) {
                g_stats.total_events_sent += packet->original_event_count;
                break;
            }
            
            // 失败：断开重连
            evs_tcp_sender_disconnect(g_tcp_sender);
            sleep(1);
            evs_tcp_sender_connect(g_tcp_sender);
        }
        
        encoded_packet_destroy(packet);
    }
}
```

---

## 三、接收端详细流程

### 3.1 接收端架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    EVS TCP Receiver                              │
│                        (PC - x86_64)                             │
└─────────────────────────────────────────────────────────────────┘

[监听端口 8888]
         │
         ↓
    [accept客户端]
         │
         ↓
    [设置TCP选项]
    ├─ SO_RCVBUF = 4MB
    ├─ TCP_NODELAY
    └─ SO_RCVTIMEO = 10s
         │
         ↓
    [创建EVT2解码器]
    evt2_decoder_create()
         │
         ↓
    [接收循环]
         │
         ├─→ [接收数据包头]
         │   recv_full(header, sizeof(header))
         │
         ├─→ [验证数据包头]
         │   packet_header_validate()
         │
         ├─→ [检查序列号]
         │   检测丢包
         │
         ├─→ [接收负载数据]
         │   recv_full(payload, payload_size)
         │
         ├─→ [验证校验和]
         │   packet_calculate_checksum()
         │
         └─→ [处理数据包]
             ├─ PACKET_TYPE_RAW_EVENTS → 原始事件
             ├─ PACKET_TYPE_EVT2_DATA → EVT2编码数据
             │   └─ evt2_decoder_decode() → 解码
             └─ PACKET_TYPE_HEARTBEAT → 心跳
```

### 3.2 数据包协议

**数据包结构**：

```
┌──────────────────────────────────────┐
│  PacketHeader (固定大小)              │
│  ├─ magic: 0xEVS1 (4 bytes)         │
│  ├─ packet_type (1 byte)            │
│  ├─ sequence_num (4 bytes)          │
│  ├─ device_id (4 bytes)             │
│  ├─ payload_size (4 bytes)          │
│  ├─ event_count (4 bytes)           │
│  ├─ timestamp_sec (4 bytes)         │
│  ├─ timestamp_usec (4 bytes)        │
│  └─ checksum (4 bytes)              │
├──────────────────────────────────────┤
│  Payload (变长)                      │
│  └─ EVT2编码数据 / 原始事件          │
└──────────────────────────────────────┘
```

**数据包类型**：

```c
typedef enum {
    PACKET_TYPE_RAW_EVENTS = 0x01,  // 原始事件数据
    PACKET_TYPE_EVT2_DATA = 0x02,   // EVT2编码数据
    PACKET_TYPE_HEARTBEAT = 0x03    // 心跳包
} PacketType_t;
```

### 3.3 接收端处理流程

**关键代码**：

```c
static void handle_client(int client_fd, struct sockaddr_in* addr) {
    // 创建EVT2解码器
    EVT2Decoder_t* decoder = evt2_decoder_create();
    
    while (g_running) {
        // 1. 接收数据包头
        PacketHeader_t header;
        if (recv_full(client_fd, &header, sizeof(header)) < 0) break;
        
        // 2. 验证数据包
        if (packet_header_validate(&header) < 0) continue;
        
        // 3. 检查序列号
        if (sequence_num != expected_sequence) {
            fprintf(stderr, "Sequence error: expected %u, got %u\n",
                   expected_sequence, sequence_num);
            g_stats.packets_dropped += (sequence_num - expected_sequence);
        }
        expected_sequence++;
        
        // 4. 接收负载
        uint8_t* payload = malloc(payload_size);
        if (recv_full(client_fd, payload, payload_size) < 0) break;
        
        // 5. 验证校验和
        uint32_t checksum = packet_calculate_checksum(&header, payload);
        if (checksum != received_checksum) {
            g_stats.checksum_errors++;
            continue;
        }
        
        // 6. 处理数据包
        switch (packet_type) {
        case PACKET_TYPE_EVT2_DATA:
            process_evt2_packet(&header, payload, decoder);
            break;
        case PACKET_TYPE_RAW_EVENTS:
            process_event_packet(&header, payload);
            break;
        }
        
        free(payload);
        g_stats.total_packets_received++;
    }
    
    evt2_decoder_destroy(decoder);
}
```

#### 3.3.1 EVT2解码流程

```c
static int process_evt2_packet(const PacketHeader_t* header,
                               const uint8_t* payload,
                               EVT2Decoder_t* decoder) {
    uint32_t event_count = ntohl(header->event_count);
    uint32_t payload_size = ntohl(header->payload_size);
    
    // 分配解码缓冲区
    EVSEvent_t* decoded_events = malloc(event_count * sizeof(EVSEvent_t));
    
    // 解码EVT2数据
    uint32_t actual_count = 0;
    evt2_decoder_decode(
        decoder,
        payload,
        payload_size,
        decoded_events,
        event_count,
        &actual_count
    );
    
    // 统计事件
    uint32_t positive = 0, negative = 0;
    for (uint32_t i = 0; i < actual_count; i++) {
        if (decoded_events[i].polarity > 0) positive++;
        else negative++;
    }
    
    double compression = 100.0 * (1.0 - payload_size / 
                        (event_count * sizeof(EVSEvent_t)));
    
    printf("[EVT2 #%u] Events=%u (Pos=%u, Neg=%u), "
           "Size=%u bytes (%.1f%% compression)\n",
           sequence_num, actual_count, positive, negative,
           payload_size, compression);
    
    free(decoded_events);
    return 0;
}
```

---

## 四、队列和缓冲区设计

### 4.1 队列容量配置

| 队列 | 容量 | 阻塞模式 | 说明 |
|------|------|----------|------|
| V4L2 Frame Queue | 8 | 非阻塞 | 保护V4L2采集，队列满丢帧 |
| Encoding Queue | 20 | 阻塞 | 等待编码线程处理 |
| Transmission Queue | 50 | 阻塞 | 缓冲待发送数据 |

### 4.2 内存使用估算

```
V4L2 Frame Queue:
  8 × 1 MB = 8 MB

Encoding Queue (时间窗口):
  20 × ~2 MB = 40 MB
  (每窗口最大 3.7M 事件 × 12 bytes)

Transmission Queue (编码数据):
  50 × ~30 KB = 1.5 MB

V4L2 mmap buffers:
  4 × 1 MB = 4 MB

总计：~54 MB
```

---

## 五、使用说明

### 5.1 编译

**发送端（RK3588）**：
```bash
cd /path/to/apx003_v4l2_sample
make evs_sender_mt
# 生成: bin/evs_multithread_sender.exe
```

**接收端（PC）**：
```bash
cd /path/to/apx003_v4l2_sample
make evs_receiver
# 生成: bin/evs_tcp_receiver.exe
```

### 5.2 运行

**步骤1：在PC上启动接收端**
```bash
./bin/evs_tcp_receiver.exe [port]
# 默认监听端口 8888
# 示例：./bin/evs_tcp_receiver.exe 8888
```

**步骤2：在RK3588上启动发送端**
```bash
./bin/evs_multithread_sender.exe <server_ip> <port> [window_ms]
# 示例：
./bin/evs_multithread_sender.exe 192.168.1.100 8888 30
#                                  ↑PC IP      ↑端口 ↑30ms窗口
```

### 5.3 参数说明

**发送端参数**：
- `server_ip`：PC接收端IP地址（必需）
- `port`：TCP端口（必需，默认8888）
- `window_ms`：时间窗口大小（可选，默认30ms）

**接收端参数**：
- `port`：监听端口（可选，默认8888）

### 5.4 输出示例

**发送端输出**：
```
========================================
EVS Multi-threaded Sender
Server: 192.168.1.100:8888
Device: /dev/video1
Time Window: 30 ms
Encoding Threads: 2
========================================

[Init] Statistics initialized
[Init] Queues created: V4L2=8, Encoding=20, Transmission=50
[TCP Sender] Created
[DVS Init] Initialized successfully
[Main] Starting threads...
[V4L2 Acquisition Thread] Started
[Extraction Thread] Started
[Encoding Thread 0] Started
[Encoding Thread 1] Started
[Transmission Thread] Started

[Encoding Thread 0] Encoded window #1: 15234 events → 3421 bytes (81.2% compression)
[Transmission] Sending encoded window #1 (15234 events, 3421 bytes EVT2)
```

**接收端输出**：
```
========================================
EVS TCP Receiver
Listening on port: 8888
========================================

[Receiver] Client connected: 192.168.49.110:45678
[EVT2 Packet #1] Device=1, Events=15234 (Pos=8123, Neg=7111), EVT2=3421 bytes (81.2% compression), Timestamp=1234567.890123
[EVT2 Packet #2] Device=1, Events=18456 (Pos=9801, Neg=8655), EVT2=4102 bytes (79.8% compression), Timestamp=1234567.920145

========== Receiver Statistics ==========
Total Packets: 100
Total Events: 1523400
Total Bytes: 102.34 MB
Packets Dropped: 0
Sequence Errors: 0
Checksum Errors: 0
=========================================
```

---

## 六、性能指标

### 6.1 典型性能数据

| 指标 | 数值 | 说明 |
|------|------|------|
| V4L2采集速率 | ~30 FPS | 4096×256帧 |
| 原始数据速率 | ~30 MB/s | 1 MB/帧 × 30 FPS |
| 事件提取速率 | ~100K-500K events/s | 取决于场景 |
| EVT2压缩率 | 70-90% | 取决于事件密度 |
| 编码后速率 | ~3-10 MB/s | 压缩后 |
| TCP传输速率 | ~5-15 MB/s | 取决于网络 |
| 端到端延迟 | 50-100 ms | 窗口大小+处理延迟 |

### 6.2 优化建议

**CPU密集型场景**：
- 增加编码线程数：`NUM_ENCODING_THREADS = 4`
- 减小时间窗口：`window_ms = 20`

**网络受限场景**：
- 增加传输队列：`TRANSMISSION_QUEUE_SIZE = 100`
- 启用系统TCP优化（见TCP_BOTTLENECK_ANALYSIS.md）

**低延迟场景**：
- 减小时间窗口：`window_ms = 10`
- 减小队列容量
- 使用阻塞模式确保无丢失

---

## 七、关键数据流总结

### 7.1 完整数据流

```
[V4L2设备] /dev/video1
    ↓ 1 MB/帧 (4096×256)
    ↓ 包含32个子帧
    
[Thread 1] V4L2 Acquisition
    ↓ memcpy到帧缓冲区
    
[Queue 1] V4L2 Frame (8帧缓冲)
    ↓ 1 MB × 8 = 8 MB
    
[Thread 2] Event Extraction
    ↓ 解析32子帧
    ↓ 按时间戳排序
    ↓ 累积到30ms时间窗口
    ↓ 提取~50K-200K events
    
[Queue 2] Encoding (20窗口缓冲)
    ↓ ~2 MB × 20 = 40 MB
    
[Thread 3-4] EVT2 Encoding (2线程)
    ↓ 时间差分+空间差分
    ↓ 变长编码
    ↓ 压缩至5-30 KB
    
[Queue 3] Transmission (50数据包缓冲)
    ↓ ~30 KB × 50 = 1.5 MB
    
[Thread 5] TCP Transmission
    ↓ 发送EVT2编码数据
    
[网络] TCP Socket
    ↓
    
[PC接收端]
    ↓ 接收EVT2数据包
    ↓ 验证序列号+校验和
    ↓ EVT2解码
    ↓ 还原事件数据
    
[应用处理]
```

### 7.2 帧、子帧、窗口关系

```
1个V4L2帧（包） (1 MB)
  = 32个子帧 (32 KB/子帧)
  ≈ 1个时间窗口 (30ms)
  ≈ 50K-200K 事件
  → 1个编码数据包 (5-30 KB EVT2)
```

---
