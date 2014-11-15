# detect the host system, fallback to mingw
# avoid $(or/and ... ) to support living fossils (3.80 and earlier)
# and avoid >1 else sequences (3.79 and earlier)

OSTYPE = $(shell uname -s | tr A-Z a-z)
ifneq (,$(findstring linux,$(OSTYPE)))
	LFSC = $(shell getconf LFS_CFLAGS)
	LFSD = $(shell getconf LFS_LDFLAGS)
	OST = -Dh_linux -Dh_thr -Dh_affi
endif
ifneq (,$(findstring mingw,$(OSTYPE)))
	OST = -Dh_mingw
endif
ifneq (,$(findstring bsd,$(OSTYPE)))
	OST = -Dh_bsd
endif
ifneq (,$(findstring freebsd,$(OSTYPE)))
	LFSC = $(shell getconf LFS_CFLAGS)
	LFSD = $(shell getconf LFS_LDFLAGS)
	OST = -Dh_freebsd -Dh_thr
endif
# and fallback to mingw
ifeq (,$(strip $(OSTYPE)))
	OST = -Dh_mingw
endif
# large file support fallback
ifeq (,$(strip $(LFSC)))
	# last resort, rather dumb fallback ...
	LFSC = -D_LARGEFILE_SOURCE -D_LARGEFILE_SOURCE64 -D_FILE_OFFSET_BITS=64
endif

YANVER = $(shell git describe --tags --abbrev=8 --always HEAD)
ifneq (,$(YANVER))
	YANVER += $(shell git show -s --date=short --pretty=format:'// %ad')
else
	# no git command at all ?
	YANVER = git
endif

MAKEDEPS = -MT $@ -MMD -MF $(dir $@).$(notdir $@).d
CFLAGS += -std=gnu99 -pipe -fno-common -fstrict-aliasing -fstrict-overflow -mtune=native -march=native
CWFLAGS+= -Wall -Wextra -Wstrict-prototypes -Wstrict-aliasing=1
CFLAGS += $(CWFLAGS)
-include Makefile.devel
LIBS =
DEBUG ?= 0

ifeq (1,$(PROFILE))
	CFLAGS += -pg -fprofile-generate --coverage -O0
	LDFLAGS += -pg -fprofile-generate --coverage -O0
endif
ifeq (2,$(PROFILE))
	CFLAGS += -fprofile-use
	LDFLAGS += -fprofile-use
endif

ifeq (1,$(DEBUG))
	CFLAGS += -O0 -ggdb -fno-omit-frame-pointer
else ifneq (1,$(PROFILE))
	CFLAGS += -O3 -g1   -fomit-frame-pointer
endif

ifeq (-Dh_mingw,$(OST))
	LIBS += -lws2_32
	EXE = .exe
endif
ifeq (-Dh_thr,$(findstring -Dh_thr,$(OST)))
	CFLAGS += -pthread
	LDFLAGS += -pthread
	LIBS += -lrt
endif

CC = gcc
PREFIX ?= /usr/local/bin
CFLAGS += $(OST) $(LFSC) -DDEBUG=$(DEBUG)
LDFLAGS += $(LFS_LDFLAGS)

OBJS =  yancat.o buffer.o fdpack.o options.o parse.o crc.o common.o \
	mtxw_posix.o \
	semw_posix.o semw_sysv.o \
	semw_posixu.o shmw_posix.o shmw_sysv.o shmw_malloc.o

SRCS = $(OBJS:.o=.c)
DEPS = $(OBJS:%.o=.%.o.d)

.PHONY: all clean distclean

all: yancat

options.c: version.h

version.h:
	echo "#define YANVER \""$(YANVER)"\"" >$@

yancat: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

strip: yancat
	strip -s $<$(EXE)

clean:
	rm -f *.o yancat$(EXE) .*.d version.h *~ .*~

distclean: clean
	rm -f *.gc{da,no,ov}

install: yancat
	install -D $< $(DESTDIR)$(PREFIX)/$<

%.o: %.c
	$(CC) -c -o $@ $(MAKEDEPS) $(CFLAGS) $<

# from older explicit depends target
#.%.o.d: %.c
#	$(CC) $(OST) -MM -MF $@ -MT $@ -MT $(<:.c=.o) $<

-include .*.d
