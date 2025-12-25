CC="aarch64-linux-gcc"
AR="aarch64-linux-ar"
includedir=inc
libdir=lib
INCS := -Isrc -Iinc
HOME := $(shell echo ${GSF_HOME})

CFLAGS ?= -g -fomit-frame-pointer -O2
CFLAGS += -Wall -I. -fPIC
SO_CFLAGS=-shared $(CFLAGS)
L_CFLAGS=$(CFLAGS)
LINK_FLAGS=
LINK_FLAGS+=$(LDFLAGS)
ENABLE_SHARED ?= 1

soname=lib/libaio.so
minor=0
micro=2
# libname=$(soname).$(minor).$(micro)
libname=$(soname)
# all_targets += libaio.a

ifeq ($(ENABLE_SHARED),1)
all_targets += $(libname)
endif

all: $(all_targets)

libaio_srcs += $(shell ls src/*.c)

# libaio_objs := $(patsubst %.c,%.ol,$(libaio_srcs))
libaio_sobjs := $(patsubst %.c,%.os,$(libaio_srcs))

# $(libaio_objs) $(libaio_sobjs): 
$(libaio_sobjs): 

%.os: %.c
	$(CC) $(SO_CFLAGS) -c $(INCS) -o $@ $<

# %.ol: %.c
# 	$(CC) $(L_CFLAGS) -c $(INCS) -o $@ $<

# AR ?= ar
# RANLIB ?= ranlib
# libaio.a: $(libaio_objs)
# 	rm -f libaio.a
# 	$(AR) r libaio.a $^
# 	$(RANLIB) libaio.a

$(libname): $(libaio_sobjs) libaio.map
	# $(CC) $(CFLAGS) -c $(INCS) src/struct_offsets.c
	$(CC) $(SO_CFLAGS) -Wl,--version-script=libaio.map -Wl,-soname=$(soname) -o $@ $(libaio_sobjs) $(LINK_FLAGS)
	cp $(libname) $(HOME)/lib/$(GSF_CPU_ARCH)/libaio.so -v

install: $(all_targets)
	# install -D -m 644 libaio.h $(includedir)/libaio.h
	# install -D -m 644 libaio.a $(libdir)/libaio.a

# ifeq ($(ENABLE_SHARED),1)
# 	install -D -m 755 $(libname) $(libdir)/$(libname)
# 	ln -sf $(libname) $(libdir)/$(soname)
# 	ln -sf $(libname) $(libdir)/libaio.so
# endif


clean:
	rm -f $(all_targets) $(libaio_objs) $(libaio_sobjs) $(soname).new
	rm -f *.so* *.a *.o