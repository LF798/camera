# RM := rm
# CP := cp
# HOME := $(shell echo ${GSF_HOME})
# CC := $(shell echo ${GSF_CC})
# AR := $(shell echo ${GSF_AR})
# CFLAGS := $(shell echo ${GSF_CFLAGS})
# LDFLAGS := -shared -g $(shell echo ${GSF_LDFLAGS})
# TARG := lib/libmpi.so


# INCS := -I$(HOME) -Isrc -Iinc -Iinc/rksdk -Iinc/common -Iinc/deps \
# 		-Iinc/deps/libavcodec   -Iinc/deps/libavformar  -Iinc/deps/avutil  -Iinc/deps/swresample

# #SRCS := $(shell ls src/*.c)
# SUBDIRS=$(shell find ./src/ -maxdepth 7 -type d)
# SRCS:=$(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.c))
# OBJS := $(patsubst %.c, %.o, $(SRCS))
# LIBS += 
# DEP_OBJS := OBJS


# $(TARG): $(OBJS) $(TARG_EX)
# 	$(RM) lib/$(DEP_OBJS) -rf
# 	$(foreach a, $(DEP_LIBS), cd lib; $(AR) -t $(a) | sed 's/^/lib\//' >> $(DEP_OBJS); $(AR) x $(a); cd ..;)
# 	${CC} ${LDFLAGS} -o $@ ${OBJS} `cat lib/$(DEP_OBJS)` ${LIBS}
# 	rm -rf `cat lib/$(DEP_OBJS)`; rm -rf lib/$(DEP_OBJS);
# 	# cp $(TARG) $(HOME)/lib/$(GSF_CPU_ARCH)/ -v

# $(TARG_EX): $(OBJS_EX)
# 	${AR} -rcs $@ ${OBJS_EX}

# .c.o:
# 	$(CC) $(CFLAGS) -c $< $(INCS) -o $@

# .Phony: clean
# clean:
# 	-rm $(TARG) $(OBJS) $(OBJS_EX) src/*.bak -rf


RM := rm
CP := cp
HOME := $(shell echo ${GSF_HOME})
CC := $(shell echo ${GSF_CC})
AR := $(shell echo ${GSF_AR})
CFLAGS := $(shell echo ${GSF_CFLAGS})
LDFLAGS := -shared -g $(shell echo ${GSF_LDFLAGS})
TARG := lib/libmpi.so

#============================================================= 

INCS := -I$(HOME) -Isrc -Iinc -Iinc/rksdk -Iinc/common -Iinc/deps \
		-Iinc/deps/libavcodec   -Iinc/deps/libavformar  -Iinc/deps/avutil  -Iinc/deps/swresample

# SRCS := $(shell ls src/*.c)
SUBDIRS=$(shell find ./src/ -maxdepth 7 -type d)
SRCS:=$(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.c))

OBJS := $(patsubst %.c, %.o, $(SRCS))
# LIBS := lib/rksdk/librockit.so
LIBS += 
DEP_OBJS := OBJS
DEP_LIBS += 

$(TARG): $(OBJS)
	$(RM) lib/$(DEP_OBJS) -rf
	$(foreach a, $(DEP_LIBS), cd lib; $(AR) -t $(a) | sed 's/^/lib\//' >> $(DEP_OBJS); $(AR) x $(a); cd ..;)
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}
	rm -rf `cat lib/$(DEP_OBJS)`; rm -rf lib/$(DEP_OBJS);
	cp $(TARG) $(HOME)/lib/$(GSF_CPU_ARCH)/ -v
	
.c.o:
	$(CC) $(CFLAGS) -c $< $(INCS) -o $@

.Phony: clean
clean:
	-rm $(TARG) $(OBJS) src/*.bak -rf
