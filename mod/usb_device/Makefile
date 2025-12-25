HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
CFLAGS := $(shell echo ${GSF_CFLAGS})
LDFLAGS := -g $(shell echo ${GSF_LDFLAGS}) $(shell echo ${GSF_EXECFLAGS})
# TARG := bin/apsData.exe
TARG := bin/usb_device.exe


PROJECT_SOURCE_DIR := $(HOME)/mod/codec/src

INCS := -I$(HOME) -Isrc -Iinc \
		-I$(HOME)/common/libaio/inc
	
SRCS := src/aio_multibuff.c  

OBJS := $(patsubst %.c, %.o, $(SRCS))

LIB_FILES := -L$(HOME)/lib/$(GSF_CPU_ARCH)  \

LIBS := -lm -lpthread -lrt -laio

$(TARG): $(OBJS)
	$(CC) $(LDFLAGS) $(LIB_FILES) -o $@ $(OBJS) $(LIBS)
	cp $(TARG) $(HOME)/bin/$(GSF_CPU_ARCH)/ -v

DEPS := $(patsubst %.o, %.d, $(OBJS))
-include $(DEPS)
.c.o:
	$(CC) $(CFLAGS) -c -MMD $< $(INCS) -o $@
  
.Phony: clean
clean:
	-rm $(TARG) $(OBJS) $(DEPS) src/*.bak -rf