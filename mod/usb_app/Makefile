HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
CFLAGS := $(shell echo ${GSF_CFLAGS})
LDFLAGS := -g $(shell echo ${GSF_LDFLAGS}) $(shell echo ${GSF_EXECFLAGS})
# TARG := bin/evs_test
TARG := bin/usb-ffs_sendData.exe


INCS := -I$(HOME) -Isrc -Iinc \
		-I$(HOME)/common/libaio/inc \
		-I$(HOME)/common/shmfifo/inc
	
# SRCS := src/get.c
SRCS := src/usb-ffs_sendData.c     
# SRCS := src/usb_app.c     

OBJS := $(patsubst %.c, %.o, $(SRCS))

LIB_FILES := -L$(HOME)/lib/$(GSF_CPU_ARCH)

LIBS := -lcfifo -laio -lm -lpthread -lrt

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