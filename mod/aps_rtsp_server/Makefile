HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
CFLAGS := $(shell echo ${GSF_CFLAGS})
LDFLAGS := -g $(shell echo ${GSF_LDFLAGS}) $(shell echo ${GSF_EXECFLAGS})

TARGET_V1 = bin/aps_rtsp_server_v1.exe
TARGET_V2 = bin/aps_rtsp_server_v2.exe
INCS := -I$(HOME)/inc -Isrc -Iinc \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/rksdk \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/common \
		-I$(HOME)/mod/mpp/$(GSF_CPU_ARCH)/inc/deps \
		-I$(HOME)/common/rtsp/inc 

SRCS_V1 := src/aps_rtsp_server.c
SRCS_V2 := src/aps_rtsp_server_v2.c

OBJS_V1 := $(patsubst %.c, %.o, $(SRCS_V1))
OBJS_V2 := $(patsubst %.c, %.o, $(SRCS_V2))

LIBS := -L$(HOME)/lib/$(GSF_CPU_ARCH) \
		-lmpi -lm -lpthread -lrt \
		-lrockit -lasound -lavcodec -lavformat -lavutil -ldrm -lgraphic_lsf -lmali -lrga -lrockchip_mpp -lswresample -lrknnrt \
		-lrtsp

LIBS_RECEIVER_DISPLAY := -llz4 -lpthread

# 接收端程序的包含路径（x86环境，不需要MPI头文件）
INCS_RECEIVER_DISPLAY := -I$(HOME)/inc -Isrc -Iinc \
		-I$(HOME)/common/rtsp/inc
$(TARGET_V1): $(OBJS_V1)
	@echo "Linking $(TARGET_V1)..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS_V1) $(LIBS)
	@mkdir -p $(HOME)/bin/$(GSF_CPU_ARCH)
	@cp $(TARGET_V1) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v

DEPS_V1 := $(patsubst %.o, %.d, $(OBJS_V1))
-include $(DEPS_V1)

$(TARGET_V2): $(OBJS_V2)
	@echo "Linking $(TARGET_V2)..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS_V2) $(LIBS)
	@mkdir -p $(HOME)/bin/$(GSF_CPU_ARCH)
	@cp $(TARGET_V2) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v

DEPS_V2 := $(patsubst %.o, %.d, $(OBJS_V2))
-include $(DEPS_V2)
all: $(TARGET_V1) $(TARGET_V2)

# 通用编译规则
.c.o:
	$(CC) $(CFLAGS) -c -MMD $< $(INCS) -o $@


.PHONY: clean 
clean:
	-rm $(TARGET_V1) $(TARGET_V2) \
		$(OBJS_V1) $(OBJS_V2) \
		$(DEPS_V1) $(DEPS_V2) src/*.bak -rf


