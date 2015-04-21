# This makefile is for corss-compiling simh to Windows on POSIX platforms
# If you are running Windows now, run build_mingw.bat

ifndef GCC
GCC = i586-mingw32msvc-gcc
else
ifeq ($(findstring mingw32ce,$(GCC)),mingw32ce)
WCE = true
endif
endif

export CONSOLE=1

ifneq (,$(DISPLAY_USEFUL))
DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/win32.c
DISPLAYVT = ${DISPLAYD}/vt11.c
DISPLAY_OPT = -DUSE_DISPLAY
ifndef WCE
DISPLAY_OPT += -lgdi32
endif
endif

GCC_VERSION = $(word 3,$(shell $(CC) --version))
COMPILER_NAME = GCC Version: $(GCC_VERSION)

TEST = test

ifeq ($(USE_BUILDIN_PTHERADS),1)
PTHREADS_CCDEFS = -DUSE_READER_THREAD -DPTW32_STATIC_LIB -D_POSIX_C_SOURCE -I../windows-build/pthreads/Pre-built.2/include
PTHREADS_LDFLAGS = -lpthreadGC2 -L../windows-build/pthreads/Pre-built.2/lib
else
PTHREADS_CCDEFS = -DUSE_READER_THREAD
#PTHREADS_LDFLAGS = -lpthreadGC2
PTHREADS_LDFLAGS = -lpthread
endif

ifeq (,$(NOASYNCH))
PTHREADS_CCDEFS += -DSIM_ASYNCH_IO
endif


ifdef PCAP
 ifeq ($(PCAP),BUILDIN)
  NETWORK_LDFLAGS =
  NETWORK_OPT = -DUSE_SHARED -I../windows-build/winpcap/Wpdpack/include
  NETWORK_FEATURES = - dynamic networking support using windows-build provided libpcap components
 else
  NETWORK_LDFLAGS =
  NETWORK_OPT = -DUSE_SHARED
  NETWORK_FEATURES = - dynamic networking support using libpcap components from the MinGW directories
 endif
endif


ifneq (,$(VIDEO_USEFUL))
 VIDEO_CCDEFS += -DHAVE_LIBSDL
 VIDEO_LDFLAGS  += -lSDL
 VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
endif


OS_CCDEFS += -fms-extensions $(PTHREADS_CCDEFS)
OS_LDFLAGS += -lm -lwsock32 -lwinmm $(PTHREADS_LDFLAGS)

EXE = .exe


ifneq (binexists,$(shell if $(TEST) -e BIN; then echo binexists; fi))
 MKDIRBIN = mkdir -p BIN
endif
ifeq (commit-id-exists,$(shell if $(TEST) -e .git-commit-id; then echo commit-id-exists; fi))
 GIT_COMMIT_ID=$(shell cat .git-commit-id)
else
 ifeq (,$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | grep 'Format:'))
  GIT_COMMIT_ID=$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | awk '{ print $$3 }')
 endif
endif


ifneq (,$(GIT_COMMIT_ID))
  CFLAGS_GIT = -DSIM_GIT_COMMIT_ID=$(GIT_COMMIT_ID)
endif
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
else
  CFLAGS_O = -O1
endif


ifneq ($(DONT_USE_ROMS),)
  ROMS_OPT = -DDONT_USE_INTERNAL_ROM
else
  BUILD_ROMS = ${BIN}BuildROMs
endif
ifneq ($(DONT_USE_READER_THREAD),)
  NETWORK_OPT += -DDONT_USE_READER_THREAD
endif

CC_OUTSPEC = -o $@
CC := $(GCC) $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) $(CFLAGS_GIT) -DSIM_COMPILER="$(COMPILER_NAME)" -I . $(OS_CCDEFS) $(ROMS_OPT)
HOST_CC := gcc $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) $(CFLAGS_GIT) -I . $(OS_CCDEFS) $(ROMS_OPT)
LDFLAGS := $(OS_LDFLAGS) $(NETWORK_LDFLAGS) $(LDFLAGS_O)

include sources.mk
include rules.mk

