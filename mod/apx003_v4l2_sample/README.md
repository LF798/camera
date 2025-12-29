

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
- **接收端**：`evs_tcp_receiver.c` (PC)


---


---

## 一、接收端详细流程

### 1.1 接收端架构

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

### 1.2 数据包协议

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

### 1.3 接收端处理流程

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

#### 1.3.1 EVT2解码流程

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


