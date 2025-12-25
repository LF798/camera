HOME := 
CC := gcc
AR := gcc-ar
CFLAGS = $(shell pkg-config --cflags libusb-1.0)
TARG := bin/usb_host.exe


PROJECT_SOURCE_DIR := $(HOME)/mod/codec/src

INCS := -Iinc
	
SRCS := src/host_app.c     

OBJS := $(patsubst %.c, %.o, $(SRCS))

LIB_FILES :=

LIBS := -lusb-1.0

$(TARG): $(OBJS)
	$(CC) $(LIB_FILES) -o $@ $(OBJS) $(LIBS)

DEPS := $(patsubst %.o, %.d, $(OBJS))
-include $(DEPS)
.c.o:
	$(CC) -c  $< $(INCS) -o $@
  
.Phony: clean
clean:
	-rm $(TARG) $(OBJS) $(DEPS) src/*.bak -rf