HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
ENV_CFLAGS := $(shell echo ${GSF_CFLAGS})
ENV_LDFLAGS := $(shell echo ${GSF_LDFLAGS})
ENV_EXECFLAGS := $(shell echo ${GSF_EXECFLAGS})

# 如果环境变量未设置，使用默认值
ifeq ($(CC),)
	CC = gcc
endif

ifeq ($(HOME),)
	HOME = $(shell pwd)/../..
endif

# 设置编译选项
ifeq ($(ENV_CFLAGS),)
	CFLAGS = -Wall -O3 -pthread
else
	CFLAGS = $(ENV_CFLAGS)
endif

# 设置链接选项
ifeq ($(ENV_LDFLAGS),)
	LDFLAGS = -lm -lpthread -lrt
else
	LDFLAGS = $(ENV_LDFLAGS) $(ENV_EXECFLAGS) -lm -lpthread -lrt
endif


# 库文件
GSF_CPU_ARCH := $(shell echo ${GSF_CPU_ARCH})
ifneq ($(GSF_CPU_ARCH),)
	LIB_FILES := -L$(HOME)/lib/$(GSF_CPU_ARCH)
else
	LIB_FILES :=
endif

LIBS := 
LIBS += 

TARG := bin/evsGetdata.exe

# 多线程TCP传输系统目标
MULTITHREAD_SENDER := bin/evs_multithread_sender.exe
MULTITHREAD_RECEIVER := bin/evs_tcp_receiver.exe


# 独立时间同步系统目标
TIME_SYNC_SERVER := bin/time_sync_server.exe
TIME_SYNC_CLIENT_TEST := bin/time_sync_client_test.exe




#============================================================= 
# 头文件路径
#============================================================= 
INCS := -I$(HOME) -I$(HOME)/inc -Isrc -Iinc    
			
#============================================================= 
# 原有目标：evsGetdata.c 
#============================================================= 
SRCS := src/evsGetdata.c
# 备选：src/apsGetdata.c
OBJS := $(patsubst %.c, %.o, $(SRCS))
DEPS := $(patsubst %.o, %.d, $(OBJS))

#============================================================= 
# 多线程TCP传输系统源文件
#============================================================= 
# 事件提取器
EXTRACTOR_SRCS := src/evs_event_extractor.c
EXTRACTOR_OBJS := $(patsubst %.c, %.o, $(EXTRACTOR_SRCS))
EXTRACTOR_DEPS := $(patsubst %.o, %.d, $(EXTRACTOR_OBJS))

# 数据包协议
PROTOCOL_SRCS := src/packet_protocol.c
PROTOCOL_OBJS := $(patsubst %.c, %.o, $(PROTOCOL_SRCS))
PROTOCOL_DEPS := $(patsubst %.o, %.d, $(PROTOCOL_OBJS))

# TCP发送器
TCP_SENDER_MODULE_SRCS := src/evs_tcp_sender.c
TCP_SENDER_MODULE_OBJS := $(patsubst %.c, %.o, $(TCP_SENDER_MODULE_SRCS))
TCP_SENDER_MODULE_DEPS := $(patsubst %.o, %.d, $(TCP_SENDER_MODULE_OBJS))

# 时间窗口管理
TIME_WINDOW_SRCS := src/time_window.c
TIME_WINDOW_OBJS := $(patsubst %.c, %.o, $(TIME_WINDOW_SRCS))
TIME_WINDOW_DEPS := $(patsubst %.o, %.d, $(TIME_WINDOW_OBJS))

# 线程安全队列
QUEUE_SRCS := src/thread_safe_queue.c
QUEUE_OBJS := $(patsubst %.c, %.o, $(QUEUE_SRCS))
QUEUE_DEPS := $(patsubst %.o, %.d, $(QUEUE_OBJS))

# EVT2编码器
EVT2_ENCODER_SRCS := src/evt2_encoder.c
EVT2_ENCODER_OBJS := $(patsubst %.c, %.o, $(EVT2_ENCODER_SRCS))
EVT2_ENCODER_DEPS := $(patsubst %.o, %.d, $(EVT2_ENCODER_OBJS))

# EVT2解码器
EVT2_DECODER_SRCS := src/evt2_decoder.c
EVT2_DECODER_OBJS := $(patsubst %.c, %.o, $(EVT2_DECODER_SRCS))
EVT2_DECODER_DEPS := $(patsubst %.o, %.d, $(EVT2_DECODER_OBJS))

# 编码数据包
ENCODED_PACKET_SRCS := src/encoded_packet.c
ENCODED_PACKET_OBJS := $(patsubst %.c, %.o, $(ENCODED_PACKET_SRCS))
ENCODED_PACKET_DEPS := $(patsubst %.o, %.d, $(ENCODED_PACKET_OBJS))

# 多线程发送端主程序
MULTITHREAD_SENDER_SRCS := src/evsGetdata_multithread.c
MULTITHREAD_SENDER_OBJS := $(patsubst %.c, %.o, $(MULTITHREAD_SENDER_SRCS))
MULTITHREAD_SENDER_DEPS := $(patsubst %.o, %.d, $(MULTITHREAD_SENDER_OBJS))

# TCP接收端主程序
MULTITHREAD_RECEIVER_SRCS := src/evs_tcp_receiver.c
MULTITHREAD_RECEIVER_OBJS := $(patsubst %.c, %.o, $(MULTITHREAD_RECEIVER_SRCS))
MULTITHREAD_RECEIVER_DEPS := $(patsubst %.o, %.d, $(MULTITHREAD_RECEIVER_OBJS))


# 编译规则

# 默认目标：编译原有程序
all: $(TARG)

# 编译多线程TCP传输系统
multithread: multithread_sender multithread_receiver
# 编译多线程发送端
multithread_sender: $(MULTITHREAD_SENDER)
# 编译多线程接收端
multithread_receiver: $(MULTITHREAD_RECEIVER)

# 编译原有目标（evsGetdata）
$(TARG): $(OBJS)
	@mkdir -p bin
	$(CC) $(LDFLAGS) $(LIB_FILES) -lm -lpthread -lrt -o $@ $(OBJS) $(LIBS)
	@if [ -n "$(GSF_CPU_ARCH)" ]; then \
		mkdir -p $(HOME)/bin/$(GSF_CPU_ARCH); \
		cp $(TARG) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v; \
	fi


# 多线程发送端
$(MULTITHREAD_SENDER): $(EXTRACTOR_OBJS) $(PROTOCOL_OBJS) $(TCP_SENDER_MODULE_OBJS) $(TIME_WINDOW_OBJS) $(QUEUE_OBJS) $(EVT2_ENCODER_OBJS) $(ENCODED_PACKET_OBJS) $(MULTITHREAD_SENDER_OBJS)
	@mkdir -p bin
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "✓ Multi-threaded sender built successfully: $(MULTITHREAD_SENDER)"
	@if [ -n "$(GSF_CPU_ARCH)" ]; then \
		mkdir -p $(HOME)/bin/$(GSF_CPU_ARCH); \
		cp $(MULTITHREAD_SENDER) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v; \
	fi


# 多线程接收端(x86架构，用于PC端接收）
$(MULTITHREAD_RECEIVER):
	@echo "=================================="
	@echo "Building EVT2 Receiver (x86_64)..."
	@echo "Compiler: gcc (forced x86, ignoring environment)"
	@echo "=================================="
	@mkdir -p bin
	# 强制删除旧的对象文件（避免使用ARM编译的.o文件）
	@if [ -f src/packet_protocol.o ]; then rm src/packet_protocol.o; fi
	@if [ -f src/evs_event_extractor.o ]; then rm src/evs_event_extractor.o; fi
	@if [ -f src/evt2_decoder.o ]; then rm src/evt2_decoder.o; fi
	@if [ -f src/evs_tcp_receiver.o ]; then rm src/evs_tcp_receiver.o; fi
	# 使用gcc强制编译为x86（不使用环境变量CC）
	gcc -Wall -O3 -pthread $(INCS) -c src/packet_protocol.c -o src/packet_protocol.o
	gcc -Wall -O3 -pthread $(INCS) -c src/evs_event_extractor.c -o src/evs_event_extractor.o
	gcc -Wall -O3 -pthread $(INCS) -c src/evt2_decoder.c -o src/evt2_decoder.o
	gcc -Wall -O3 -pthread $(INCS) -c src/evs_tcp_receiver.c -o src/evs_tcp_receiver.o
	# 链接为x86可执行文件
	gcc -Wall -O3 -pthread -o $@ \
		src/packet_protocol.o \
		src/evs_event_extractor.o \
		src/evt2_decoder.o \
		src/evs_tcp_receiver.o \
		-lm -lpthread -lrt
	@echo "✓ Multi-threaded receiver built successfully: $(MULTITHREAD_RECEIVER)"
	@file $@ || echo "⚠️  Verify architecture: file $@"




# 独立时间同步系统编译规则

# 时间同步服务器源文件
TIME_SYNC_SERVER_SRCS := \
	src/time_sync_server.c \
	src/time_sync_server_standalone.c

TIME_SYNC_SERVER_OBJS := $(TIME_SYNC_SERVER_SRCS:.c=.o)
TIME_SYNC_SERVER_DEPS := $(TIME_SYNC_SERVER_SRCS:.c=.d)

# 时间同步客户端测试源文件
TIME_SYNC_CLIENT_TEST_SRCS := \
	src/time_sync_client.c \
	src/time_sync_client_standalone.c

TIME_SYNC_CLIENT_TEST_OBJS := $(TIME_SYNC_CLIENT_TEST_SRCS:.c=.o)
TIME_SYNC_CLIENT_TEST_DEPS := $(TIME_SYNC_CLIENT_TEST_SRCS:.c=.d)

# 时间同步系统目标
timesync: $(TIME_SYNC_SERVER) $(TIME_SYNC_CLIENT_TEST)
	@echo "✓ Time sync system built successfully"

timesync_server: $(TIME_SYNC_SERVER)

timesync_client: $(TIME_SYNC_CLIENT_TEST)


# 编译时间同步服务器（x86）
$(TIME_SYNC_SERVER): 
	@mkdir -p bin
	@echo "Building time sync server (x86)..."
	@# 强制删除旧的对象文件（可能是ARM架构）
	-rm -f src/time_sync_server.o src/time_sync_server_standalone.o
	@# 使用x86 gcc编译
	@echo "Compiling src/time_sync_server.c (x86)..."
	gcc -Wall -O3 -pthread $(INCS) -c src/time_sync_server.c -o src/time_sync_server.o
	@echo "Compiling src/time_sync_server_standalone.c (x86)..."
	gcc -Wall -O3 -pthread $(INCS) -c src/time_sync_server_standalone.c -o src/time_sync_server_standalone.o
	@# 链接为x86可执行文件
	@echo "Linking time sync server (x86)..."
	gcc -Wall -O3 -pthread -o $@ \
		src/time_sync_server.o \
		src/time_sync_server_standalone.o \
		-lm -lpthread -lrt
	@echo "✓ Time sync server built: $@"
	@file $@ || echo "⚠️  Verify architecture: file $@"

# 编译时间同步客户端测试程序
$(TIME_SYNC_CLIENT_TEST): $(TIME_SYNC_CLIENT_TEST_OBJS) src/time_sync_client_test_main.o
	@mkdir -p bin
	@echo "Linking time sync client test..."
	$(CC) $(CFLAGS) -o $@ $(TIME_SYNC_CLIENT_TEST_OBJS) src/time_sync_client_test_main.o $(LDFLAGS)
	@echo "✓ Time sync client test built: $@"


# 编译规则（支持依赖跟踪）

# 包含依赖文件
-include $(DEPS)
-include $(EXTRACTOR_DEPS)
-include $(PROTOCOL_DEPS)
-include $(TCP_SENDER_MODULE_DEPS)
-include $(TIME_WINDOW_DEPS)
-include $(QUEUE_DEPS)
-include $(EVT2_ENCODER_DEPS)
-include $(EVT2_DECODER_DEPS)
-include $(ENCODED_PACKET_DEPS)
-include $(MULTITHREAD_SENDER_DEPS)
-include $(MULTITHREAD_RECEIVER_DEPS)
-include $(RAW_SENDER_MODULE_DEPS)
-include $(RAW_SENDER_TEST_DEPS)
-include $(RAW_RECEIVER_DEPS)

# C 文件编译规则
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -MMD $< $(INCS) -o $@

# src目录下的C文件编译规则（确保能匹配src/*.c）
src/%.o: src/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -MMD $< $(INCS) -o $@
  


# 清理原有目标
clean:
	@echo "Cleaning original targets..."
	-rm -f $(TARG) $(OBJS) $(DEPS) src/*.bak
	-rm -f bin/apsGetdata.exe

# 清理多线程TCP传输系统
clean_multithread:
	@echo "Cleaning multi-threaded TCP transmission system..."
	-rm -f $(MULTITHREAD_SENDER) $(MULTITHREAD_RECEIVER)
	-rm -f $(EXTRACTOR_OBJS) $(PROTOCOL_OBJS) $(TCP_SENDER_MODULE_OBJS)
	-rm -f $(TIME_WINDOW_OBJS) $(QUEUE_OBJS) $(EVT2_ENCODER_OBJS) $(EVT2_DECODER_OBJS) $(ENCODED_PACKET_OBJS)
	-rm -f $(MULTITHREAD_SENDER_OBJS) $(MULTITHREAD_RECEIVER_OBJS)
	-rm -f $(EXTRACTOR_DEPS) $(PROTOCOL_DEPS) $(TCP_SENDER_MODULE_DEPS)

# 清理独立时间同步系统
clean_timesync:
	@echo "Cleaning time sync system..."
	-rm -f $(TIME_SYNC_SERVER) $(TIME_SYNC_CLIENT_TEST)
	-rm -f $(TIME_SYNC_SERVER_OBJS) $(TIME_SYNC_CLIENT_TEST_OBJS)
	-rm -f $(TIME_SYNC_SERVER_DEPS) $(TIME_SYNC_CLIENT_TEST_DEPS)
	-rm -f src/time_sync_client_test_main.o src/time_sync_client_test_main.d
	-rm -f $(TIME_WINDOW_DEPS) $(QUEUE_DEPS) $(EVT2_ENCODER_DEPS) $(EVT2_DECODER_DEPS) $(ENCODED_PACKET_DEPS)
	-rm -f $(MULTITHREAD_SENDER_DEPS) $(MULTITHREAD_RECEIVER_DEPS)

# 清理所有目标
clean_all: clean clean_multithread clean_timesync
	@echo "All targets cleaned"



.PHONY: all multithread multithread_sender multithread_receiver  timesync timesync_server timesync_client clean clean_multithread  clean_timesync  clean_all