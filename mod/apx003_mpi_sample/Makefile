HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
CFLAGS := $(shell echo ${GSF_CFLAGS})
LDFLAGS := -g $(shell echo ${GSF_LDFLAGS}) $(shell echo ${GSF_EXECFLAGS})
TARG := bin/apxGetdata.exe

# aps_timestamp_test 目标
TARG_TIMESTAMP := bin/aps_timestamp_test.exe

INCS := -I$(HOME)/inc  -Isrc -Iinc \
		-I$(HOME)/common/shmfifo/inc \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/rksdk  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/common \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/rksdk  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps/libavcodec  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps/libavformat  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps/libavutil  \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps/libswresample \
		-Isrc 
	
SRCS := src/apxGetData.c #v1
# SRCS := src/apxGetData_evsalgo.c #v2

OBJS := $(patsubst %.c, %.o, $(SRCS))

LIBS := -L$(HOME)/lib/$(GSF_CPU_ARCH) -lmpi -lm -lpthread -lrt \
		-lrockit -lasound -lavcodec -lavformat -lavutil -ldrm -lgraphic_lsf -lmali -lrga  -lrockchip_mpp -lrockit -lswresample -lrknnrt\
		-lcfifo

#LIBS += -L./lib -lalp_denoise_sdk -lalp_inference_sdk 

# aps_timestamp_test 源文件和对象文件
SRCS_TIMESTAMP := src/aps_timestamp_test.c
OBJS_TIMESTAMP := $(patsubst %.c, %.o, $(SRCS_TIMESTAMP))

# aps_timestamp_test 只需要标准系统库，不需要 MPI
LIBS_TIMESTAMP := -lm -lpthread -lrt

$(TARG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	cp $(TARG) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v

$(TARG_TIMESTAMP): $(OBJS_TIMESTAMP)
	@echo "Building aps_timestamp_test..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS_TIMESTAMP) $(LIBS_TIMESTAMP)
	@mkdir -p $(HOME)/bin/$(GSF_CPU_ARCH)
	cp $(TARG_TIMESTAMP) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v
	@echo "✓ aps_timestamp_test built successfully"

DEPS := $(patsubst %.o, %.d, $(OBJS))
DEPS_TIMESTAMP := $(patsubst %.o, %.d, $(OBJS_TIMESTAMP))
-include $(DEPS)
-include $(DEPS_TIMESTAMP)

.c.o:
	$(CC) $(CFLAGS) -c -MMD $< $(INCS) -o $@
  
# aps_timestamp_test 的编译规则（使用简化的头文件路径）
$(OBJS_TIMESTAMP): src/%.o: src/%.c
	@echo "Compiling $< for aps_timestamp_test..."
	$(CC) $(CFLAGS) -c -MMD -Isrc -Iinc $< -o $@
  
.Phony: clean clean_timestamp clean_sender clean_receiver clean_receiver_viz_opencv clean_sender_udp clean_receiver_udp clean_sender_rtsp clean_receiver_rtsp clean_receiver_rtsp_display clean_sender_direct_tcp clean_raw_viewer all sender sender_udp sender_rtsp sender_direct_tcp receiver receiver_x86 receiver_viz_opencv receiver_viz_opencv_x86 receiver_udp receiver_udp_x86 receiver_rtsp receiver_rtsp_x86 receiver_rtsp_display receiver_rtsp_display_x86 raw_viewer raw_viewer_x86 evt2_all evt2_udp_all evt2_rtsp_all
all: $(TARG) $(TARG_TIMESTAMP)

clean:
	-rm $(TARG) $(OBJS) $(DEPS) src/*.bak -rf

clean_timestamp:
	-rm $(TARG_TIMESTAMP) $(OBJS_TIMESTAMP) $(DEPS_TIMESTAMP) -rf

clean_all: clean clean_timestamp
