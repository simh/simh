#
# This GNU make makefile has been tested on:
#   Linux (x86 & Sparc)
#   OS X
#   Solaris (x86 & Sparc)
#   OpenBSD
#   NetBSD
#   FreeBSD
#   Windows (MinGW & cygwin)
#   Linux x86 targeting Android (using agcc script)
#
# Android targeted builds should invoke GNU make with GCC=agcc on
# the command line.
#
# In general, the logic below will detect and build with the available
# features which the host build environment provides.
#
# Dynamic loading of libpcap is the default behavior if pcap.h is
# available at build time.  Direct calls to libpcap can be enabled
# if GNU make is invoked with USE_NETWORK=1 on the command line.
#
# The default build will build compiler optimized binaries.
# If debugging is desired, then GNU make can be invoked with
# DEBUG=1 on the command line.
#
# For linting (or other code analyzers) make may be invoked similar to:
#
#   make GCC=cppcheck CC_OUTSPEC= LDFLAGS= CFLAGS_G="--enable=all --template=gcc" CC_STD=--std=c99
#
# CC Command (and platform available options).  (Poor man's autoconf)
#
# building the pdp11, or any vax simulator could use networking support
# No Asynch I/O support for now.
NOASYNCH = 1
BUILD_SINGLE := $(MAKECMDGOALS) $(BLANK_PREFIX)
ifneq (,$(findstring all,$(MAKECMDGOALS)))
  BUILD_MULTIPLE = s
else
  ifeq ($(MAKECMDGOALS),)
    # default target is all
    BUILD_MULTIPLE = s
    BUILD_SINGLE := all $(BLANK_PREFIX)
  endif
endif
ifeq ($(findstring Windows,$(OS)),Windows)
  WIN32 = 1
endif
ifeq ($(WIN32),)  #*nix Environments (&& cygwin)
  ifeq ($(GCC),)
    GCC = gcc
  endif
  OSTYPE = $(shell uname)
  # OSNAME is used in messages to indicate the source of libpcap components
  OSNAME = $(OSTYPE)
  ifeq (SunOS,$(OSTYPE))
    TEST = /bin/test
  else
    TEST = test
  endif
  ifeq (CYGWIN,$(findstring CYGWIN,$(OSTYPE))) # uname returns CYGWIN_NT-n.n-ver
    OSTYPE = cygwin
    OSNAME = windows-build
  endif
  ifeq (,$(shell $(GCC) -v /dev/null 2>&1 | grep 'clang'))
    GCC_VERSION = $(shell $(GCC) -v /dev/null 2>&1 | grep 'gcc version' | awk '{ print $$3 }')
    COMPILER_NAME = GCC Version: $(GCC_VERSION)
    ifeq (,$(GCC_VERSION))
      ifeq (SunOS,$(OSTYPE))
        ifneq (,$(shell $(GCC) -V 2>&1 | grep 'Sun C'))
          SUNC_VERSION = $(shell $(GCC) -V 2>&1 | grep 'Sun C')
          COMPILER_NAME = $(wordlist 2,10,$(SUNC_VERSION))
          CC_STD = -std=c99
        endif
      endif
      ifeq (HP-UX,$(OSTYPE))
        ifneq (,$(shell what `which $(firstword $(GCC)) 2>&1`| grep -i compiler))
          COMPILER_NAME = $(strip $(shell what `which $(firstword $(GCC)) 2>&1` | grep -i compiler))
          CC_STD = -std=gnu99
        endif
      endif
    else
      ifeq (,$(findstring ++,$(GCC)))
        CC_STD = -std=gnu99
      else
        CPP_BUILD = 1
      endif
    endif
  else
    NO_LTO = 1
    OS_CCDEFS += -Wno-parentheses
    ifeq (Apple,$(shell $(GCC) -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 }'))
      COMPILER_NAME = $(shell $(GCC) -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
      CLANG_VERSION = $(word 4,$(COMPILER_NAME))
    else
      COMPILER_NAME = $(shell $(GCC) -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 }')
      CLANG_VERSION = $(word 3,$(COMPILER_NAME))
      ifeq (,$(findstring .,$(CLANG_VERSION)))
        COMPILER_NAME = $(shell $(GCC) -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
        CLANG_VERSION = $(word 4,$(COMPILER_NAME))
      endif
    endif
    ifeq (,$(findstring ++,$(GCC)))
      CC_STD = -std=c99
    else
      CPP_BUILD = 1
    endif
  endif
  LTO_EXCLUDE_VERSIONS = 
  PCAPLIB = pcap
  ifeq (agcc,$(findstring agcc,$(GCC))) # Android target build?
    OS_CCDEFS = -D_GNU_SOURCE
    ifeq (,$(NOASYNCH))
      OS_CCDEFS += -DSIM_ASYNCH_IO 
    endif
    OS_LDFLAGS = -lm
  else # Non-Android (or Native Android) Builds
    ifeq (,$(INCLUDES)$(LIBRARIES))
      INCPATH:=$(shell LANG=C; $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
      ifeq (,$(INCPATH))
        INCPATH:=/usr/include
      endif
      LIBPATH:=/usr/lib
    else
      $(info *** Warning ***)
      ifeq (,$(INCLUDES))
        INCPATH:=$(shell LANG=C; $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
      else
        $(info *** Warning *** Unsupported build with INCLUDES defined as: $(INCLUDES))
        INCPATH:=$(strip $(subst :, ,$(INCLUDES)))
        UNSUPPORTED_BUILD := include
      endif
      ifeq (,$(LIBRARIES))
        LIBPATH:=/usr/lib
      else
        $(info *** Warning *** Unsupported build with LIBRARIES defined as: $(LIBRARIES))
        LIBPATH:=$(strip $(subst :, ,$(LIBRARIES)))
        ifeq (include,$(UNSUPPORTED_BUILD))
          UNSUPPORTED_BUILD := include+lib
        else
          UNSUPPORTED_BUILD := lib
        endif
      endif
      $(info *** Warning ***)
    endif
    OS_CCDEFS += -D_GNU_SOURCE
    GCC_OPTIMIZERS_CMD = $(GCC) -v --help 2>&1
    GCC_WARNINGS_CMD = $(GCC) -v --help 2>&1
    LD_ELF = $(shell echo | $(GCC) -E -dM - | grep __ELF__)
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      ifneq (include,$(findstring include,$(UNSUPPORTED_BUILD)))
        INCPATH:=$(shell LANG=C; $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | grep -v 'framework directory' | tr -d '\n')
      endif
      ifeq (incopt,$(shell if $(TEST) -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libopt,$(shell if $(TEST) -d /opt/local/lib; then echo libopt; fi))
        LIBPATH += /opt/local/lib
        OS_LDFLAGS += -L/opt/local/lib
      endif
      ifeq (HomeBrew,$(shell if $(TEST) -d /usr/local/Cellar; then echo HomeBrew; fi))
        INCPATH += $(foreach dir,$(wildcard /usr/local/Cellar/*/*),$(dir)/include)
        LIBPATH += $(foreach dir,$(wildcard /usr/local/Cellar/*/*),$(dir)/lib)
      endif
      ifeq (libXt,$(shell if $(TEST) -d /usr/X11/lib; then echo libXt; fi))
        LIBPATH += /usr/X11/lib
        OS_LDFLAGS += -L/usr/X11/lib
      endif
    else
      ifeq (Linux,$(OSTYPE))
        ifeq (Android,$(shell uname -o))
          OS_CCDEFS += -D__ANDROID_API__=$(shell getprop ro.build.version.sdk) -DSIM_BUILD_OS=" On Android Version $(shell getprop ro.build.version.release)"
        endif
        ifneq (lib,$(findstring lib,$(UNSUPPORTED_BUILD)))
          ifeq (Android,$(shell uname -o))
            ifneq (,$(shell if $(TEST) -d /system/lib; then echo systemlib; fi))
              LIBPATH += /system/lib
            endif
            LIBPATH += $(LD_LIBRARY_PATH)
          endif
          ifeq (ldconfig,$(shell if $(TEST) -e /sbin/ldconfig; then echo ldconfig; fi))
            LIBPATH := $(sort $(foreach lib,$(shell /sbin/ldconfig -p | grep ' => /' | sed 's/^.* => //'),$(dir $(lib))))
          endif
        endif
        LIBEXT = so
      else
        ifeq (SunOS,$(OSTYPE))
          OSNAME = Solaris
          ifneq (lib,$(findstring lib,$(UNSUPPORTED_BUILD)))
            LIBPATH := $(shell LANG=C; crle | grep 'Default Library Path' | awk '{ print $$5 }' | sed 's/:/ /g')
          endif
          LIBEXT = so
          OS_LDFLAGS += -lsocket -lnsl
          ifeq (incsfw,$(shell if $(TEST) -d /opt/sfw/include; then echo incsfw; fi))
            INCPATH += /opt/sfw/include
            OS_CCDEFS += -I/opt/sfw/include
          endif
          ifeq (libsfw,$(shell if $(TEST) -d /opt/sfw/lib; then echo libsfw; fi))
            LIBPATH += /opt/sfw/lib
            OS_LDFLAGS += -L/opt/sfw/lib -R/opt/sfw/lib
          endif
          OS_CCDEFS += -D_LARGEFILE_SOURCE
        else
          ifeq (cygwin,$(OSTYPE))
            # use 0readme_ethernet.txt documented Windows pcap build components
#            INCPATH += ../windows-build/winpcap/WpdPack/include
#            LIBPATH += ../windows-build/winpcap/WpdPack/lib
#            PCAPLIB = wpcap
            LIBEXT = a
          else
            ifneq (,$(findstring AIX,$(OSTYPE)))
              OS_LDFLAGS += -lm -lrt
              ifeq (incopt,$(shell if $(TEST) -d /opt/freeware/include; then echo incopt; fi))
                INCPATH += /opt/freeware/include
                OS_CCDEFS += -I/opt/freeware/include
              endif
              ifeq (libopt,$(shell if $(TEST) -d /opt/freeware/lib; then echo libopt; fi))
                LIBPATH += /opt/freeware/lib
                OS_LDFLAGS += -L/opt/freeware/lib
              endif
            else
              ifneq (,$(findstring Haiku,$(OSTYPE)))
                HAIKU_ARCH=$(shell getarch)
                ifeq ($(HAIKU_ARCH),)
                  $(error Missing getarch command, your Haiku release is probably too old)
                endif
                ifeq ($(HAIKU_ARCH),x86_gcc2)
                  $(error Unsupported arch x86_gcc2. Run setarch x86 and retry)
                endif
                INCPATH := $(shell findpaths -e -a $(HAIKU_ARCH) B_FIND_PATH_HEADERS_DIRECTORY)
                INCPATH += $(shell findpaths -e B_FIND_PATH_HEADERS_DIRECTORY posix)
                LIBPATH := $(shell findpaths -e -a $(HAIKU_ARCH) B_FIND_PATH_DEVELOP_LIB_DIRECTORY)
                OS_LDFLAGS += -lnetwork
              else
                ifeq (,$(findstring NetBSD,$(OSTYPE)))
                  ifneq (no ldconfig,$(findstring no ldconfig,$(shell which ldconfig 2>&1)))
                    LDSEARCH :=$(shell LANG=C; ldconfig -r | grep 'search directories' | awk '{print $$3}' | sed 's/:/ /g')
                  endif
                  ifneq (,$(LDSEARCH))
                    LIBPATH := $(LDSEARCH)
                  else
                    ifeq (,$(strip $(LPATH)))
                      $(info *** Warning ***)
                      $(info *** Warning *** The library search path on your $(OSTYPE) platform can't be)
                      $(info *** Warning *** determined.  This should be resolved before you can expect)
                      $(info *** Warning *** to have fully working simulators.)
                      $(info *** Warning ***)
                      $(info *** Warning *** You can specify your library paths via the LPATH environment)
                      $(info *** Warning *** variable.)
                      $(info *** Warning ***)
                    else
                      LIBPATH = $(subst :, ,$(LPATH))
                    endif
                  endif
                  OS_LDFLAGS += $(patsubst %,-L%,$(LIBPATH))
                endif
              endif
            endif
            ifeq (usrpkglib,$(shell if $(TEST) -d /usr/pkg/lib; then echo usrpkglib; fi))
              LIBPATH += /usr/pkg/lib
              INCPATH += /usr/pkg/include
              OS_LDFLAGS += -L/usr/pkg/lib -R/usr/pkg/lib
              OS_CCDEFS += -I/usr/pkg/include
            endif
            ifeq (X11R7,$(shell if $(TEST) -d /usr/X11R7/lib; then echo X11R7; fi))
              LIBPATH += /usr/X11R7/lib
              INCPATH += /usr/X11R7/include
              OS_LDFLAGS += -L/usr/X11R7/lib -R/usr/X11R7/lib
              OS_CCDEFS += -I/usr/X11R7/include
            endif
            ifeq (/usr/local/lib,$(findstring /usr/local/lib,$(LIBPATH)))
              INCPATH += /usr/local/include
              OS_CCDEFS += -I/usr/local/include
            endif
            ifneq (,$(findstring NetBSD,$(OSTYPE))$(findstring FreeBSD,$(OSTYPE))$(findstring AIX,$(OSTYPE)))
              LIBEXT = so
            else
              ifeq (HP-UX,$(OSTYPE))
                ifeq (ia64,$(shell uname -m))
                  LIBEXT = so
                else
                  LIBEXT = sl
                endif
                OS_CCDEFS += -D_HPUX_SOURCE -D_LARGEFILE64_SOURCE
                OS_LDFLAGS += -Wl,+b:
                NO_LTO = 1
              else
                LIBEXT = a
              endif
            endif
          endif
        endif
      endif
    endif
    # Some gcc versions don't support LTO, so only use LTO when the compiler is known to support it
    ifeq (,$(NO_LTO))
      ifneq (,$(GCC_VERSION))
        ifeq (,$(shell $(GCC) -v /dev/null 2>&1 | grep '\-\-enable-lto'))
          LTO_EXCLUDE_VERSIONS += $(GCC_VERSION)
        endif
      endif
    endif
  endif
  $(info lib paths are: $(LIBPATH))
  $(info include paths are: $(INCPATH))
  find_lib = $(strip $(firstword $(foreach dir,$(strip $(LIBPATH)),$(wildcard $(dir)/lib$(1).$(LIBEXT)))))
  find_include = $(strip $(firstword $(foreach dir,$(strip $(INCPATH)),$(wildcard $(dir)/$(1).h))))
  ifneq (,$(call find_lib,m))
    OS_LDFLAGS += -lm
    $(info using libm: $(call find_lib,m))
  endif
  ifneq (,$(call find_lib,rt))
    OS_LDFLAGS += -lrt
    $(info using librt: $(call find_lib,rt))
  endif
  ifneq (,$(call find_lib,pthread))
    ifneq (,$(call find_include,pthread))
#      OS_CCDEFS += -DUSE_READER_THREAD
      ifeq (,$(NOASYNCH))
        OS_CCDEFS += -DSIM_ASYNCH_IO 
      endif
#      OS_LDFLAGS += -lpthread
#      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
    endif
  endif
  ifneq (,$(call find_include,semaphore))
    ifneq (, $(shell grep sem_timedwait $(call find_include,semaphore)))
      OS_CCDEFS += -DHAVE_SEMAPHORE
      $(info using semaphore: $(call find_include,semaphore))
    endif
  endif
  ifneq (,$(call find_include,sys/mman))
    ifneq (,$(shell grep shm_open $(call find_include,sys/mman)))
      OS_CCDEFS += -DHAVE_SHM_OPEN
      $(info using mman: $(call find_include,sys/mman))
    endif
  endif
  ifneq (,$(call find_include,dlfcn))
    ifneq (,$(call find_lib,dl))
      OS_CCDEFS += -DHAVE_DLOPEN=$(LIBEXT)
      OS_LDFLAGS += -ldl
      $(info using libdl: $(call find_lib,dl) $(call find_include,dlfcn))
    else
      ifeq (BSD,$(findstring BSD,$(OSTYPE)))
        OS_CCDEFS += -DHAVE_DLOPEN=so
        $(info using libdl: $(call find_include,dlfcn))
      endif
    endif
  endif
  ifneq (,$(NETWORK_USEFUL))
    ifneq (,$(call find_include,pcap))
      ifneq (,$(shell grep 'pcap/pcap.h' $(call find_include,pcap) | grep include))
        PCAP_H_PATH = $(dir $(call find_include,pcap))pcap/pcap.h
      else
        PCAP_H_PATH = $(call find_include,pcap)
      endif
      ifneq (,$(shell grep pcap_compile $(PCAP_H_PATH) | grep const))
        BPF_CONST_STRING = -DBPF_CONST_STRING
      endif
      ifneq (,$(call find_lib,$(PCAPLIB)))
        ifneq ($(USE_NETWORK),) # Network support specified on the GNU make command line
          NETWORK_CCDEFS = -DUSE_NETWORK -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
          ifeq (cygwin,$(OSTYPE))
            # cygwin has no ldconfig so explicitly specify pcap object library
            NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          else
            NETWORK_LDFLAGS = -l$(PCAPLIB)
          endif
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
        else # default build uses dynamic libpcap
          NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
          $(info using libpcap: $(call find_include,pcap))
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
        endif
      else
        NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
        NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
        $(info using libpcap: $(call find_include,pcap))
      endif
    else
      # Look for package built from tcpdump.org sources with default install target (or cygwin winpcap)
      LIBPATH += /usr/local/lib
      INCPATH += /usr/local/include
      LIBEXTSAVE := $(LIBEXT)
      LIBEXT = a
      ifneq (,$(call find_lib,$(PCAPLIB)))
        ifneq (,$(call find_include,pcap))
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
          ifeq (cygwin,$(OSTYPE))
            NETWORK_CCDEFS = -DUSE_NETWORK -I$(dir $(call find_include,pcap))
            NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
            NETWORK_FEATURES = - static networking support using libpcap components located in the cygwin directories
          else
            NETWORK_CCDEFS := -DUSE_NETWORK -isystem $(dir $(call find_include,pcap)) $(call find_lib,$(PCAPLIB))
            NETWORK_FEATURES = - networking support using libpcap components from www.tcpdump.org
            $(info *** Warning ***)
            $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) being built with networking support using)
            $(info *** Warning *** libpcap components from www.tcpdump.org.)
            $(info *** Warning *** Some users have had problems using the www.tcpdump.org libpcap)
            $(info *** Warning *** components for simh networking.  For best results, with)
            $(info *** Warning *** simh networking, it is recommended that you install the)
            $(info *** Warning *** libpcap-dev package from your $(OSTYPE) distribution)
            $(info *** Warning ***)
          endif
        else
          $(error using libpcap: $(call find_lib,$(PCAPLIB)) missing pcap.h)
        endif
      endif
      LIBEXT = $(LIBEXTSAVE)
    endif
    ifneq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
      # Given we have libpcap components, consider other network connections as well
      ifneq (,$(call find_lib,vdeplug))
        # libvdeplug requires the use of the OS provided libpcap
        ifeq (,$(findstring usr/local,$(NETWORK_CCDEFS)))
          ifneq (,$(call find_include,libvdeplug))
            # Provide support for vde networking
            NETWORK_CCDEFS += -DUSE_VDE_NETWORK
            NETWORK_LDFLAGS += -lvdeplug
            $(info using libvdeplug: $(call find_lib,vdeplug) $(call find_include,libvdeplug))
          endif
        endif
      endif
      ifneq (,$(call find_include,linux/if_tun))
        # Provide support for Tap networking on Linux
        NETWORK_CCDEFS += -DUSE_TAP_NETWORK
      endif
      ifeq (bsdtuntap,$(shell if $(TEST) -e /usr/include/net/if_tun.h -o -e /Library/Extensions/tap.kext; then echo bsdtuntap; fi))
        # Provide support for Tap networking on BSD platforms (including OS X)
        NETWORK_CCDEFS += -DUSE_TAP_NETWORK -DUSE_BSDTUNTAP
      endif
    else
      NETWORK_FEATURES = - WITHOUT networking support
      $(info *** Warning ***)
      $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) are being built WITHOUT networking support)
      $(info *** Warning ***)
      $(info *** Warning *** To build simulator(s) with networking support you should read)
      $(info *** Warning *** 0readme_ethernet.txt and follow the instructions regarding the)
      $(info *** Warning *** needed libpcap components for your $(OSTYPE) platform)
      $(info *** Warning ***)
    endif
    NETWORK_OPT = $(NETWORK_CCDEFS)
  endif
#  ifneq (binexists,$(shell if $(TEST) -e BIN; then echo binexists; fi))
#    MKDIRBIN = if $(TEST) ! -e BIN; then mkdir BIN; fi
#  endif
else
  #Win32 Environments (via MinGW32)
  GCC = gcc
#  GCC_Path := $(dir $(shell where gcc.exe))
  GCC_VERSION = $(word 3,$(shell $(GCC) --version))
  LTO_EXCLUDE_VERSIONS = 4.5.2
  OS_CCDEFS =  -fms-extensions $(PTHREADS_CCDEFS) $(PCAP_CCDEFS)
  OS_LDFLAGS = -lm -lwsock32 -lwinmm $(PTHREADS_LDFLAGS)
  EXE = .exe
#  ifneq (binexists,$(shell if exist BIN echo binexists))
#    MKDIRBIN = if not exist BIN mkdir BIN
#  endif
  ifneq ($(USE_NETWORK),)
    NETWORK_OPT = -DUSE_SHARED
  endif
endif
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
else
  CFLAGS_O = -O2
  LDFLAGS_O = 
  ifeq (Darwin,$(OSTYPE))
    NO_LTO = 1
  endif
  GCC_MAJOR_VERSION = $(firstword $(subst  ., ,$(GCC_VERSION)))
  ifneq (3,$(GCC_MAJOR_VERSION))
    ifeq (,$(GCC_OPTIMIZERS_CMD))
      GCC_OPTIMIZERS_CMD = $(GCC) --help=optimizers
    endif
    GCC_OPTIMIZERS = $(shell $(GCC_OPTIMIZERS_CMD))
  endif
  ifneq (,$(findstring $(GCC_VERSION),$(LTO_EXCLUDE_VERSIONS)))
    NO_LTO = 1
  endif
  ifneq (,$(findstring -finline-functions,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -finline-functions
  endif
  ifneq (,$(findstring -fgcse-after-reload,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fgcse-after-reload
  endif
  ifneq (,$(findstring -fpredictive-commoning,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fpredictive-commoning
  endif
  ifneq (,$(findstring -fipa-cp-clone,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fipa-cp-clone
  endif
  ifneq (,$(findstring -funsafe-loop-optimizations,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fno-unsafe-loop-optimizations
  endif
  ifneq (,$(findstring -fstrict-overflow,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fno-strict-overflow
  endif
  ifeq (,$(NO_LTO))
    ifneq (,$(findstring -flto,$(GCC_OPTIMIZERS)))
      CFLAGS_O += -flto -fwhole-program
      LDFLAGS_O += -flto -fwhole-program
    endif
  endif
  BUILD_FEATURES = - compiler optimizations and no debugging support
endif
ifneq (3,$(GCC_MAJOR_VERSION))
  ifeq (,$(GCC_WARNINGS_CMD))
    GCC_WARNINGS_CMD = $(GCC) --help=warnings
  endif
  ifneq (,$(findstring -Wunused-result,$(shell $(GCC_WARNINGS_CMD))))
    CFLAGS_O += -Wno-unused-result
  endif
endif
ifneq (clean,$(MAKECMDGOALS))
  BUILD_FEATURES := $(BUILD_FEATURES). $(COMPILER_NAME)
  $(info ***)
  $(info *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) being built with:)
  $(info *** $(BUILD_FEATURES).)
  ifneq (,$(NETWORK_FEATURES))
    $(info *** $(NETWORK_FEATURES).)
  endif
  $(info ***)
endif
ifneq ($(DONT_USE_READER_THREAD),)
  NETWORK_OPT += -DDONT_USE_READER_THREAD
endif


# Shut up annoying clang default warnings.

ifeq ($(GCC),clang)
  OS_CCDEFS += -Wno-parentheses -Wno-bitwise-op-parentheses -Wno-dangling-else
endif


CC_STD = -std=c99
CC_OUTSPEC = -o $@
CC = $(GCC) $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) -I . $(OS_CCDEFS) $(ROMS_OPT)
LDFLAGS = $(OS_LDFLAGS) $(NETWORK_LDFLAGS) $(LDFLAGS_O)

#
# Common Libraries
#
BIN = ..
SIM = scp.c sim_console.c sim_fio.c sim_timer.c sim_sock.c \
	sim_tmxr.c sim_tape.c sim_shmem.c sim_extension.c sim_serial.c


#
# Emulator source files and compile time options
#
HP2100D = HP2100
HP2100 = ${HP2100D}/hp2100_baci.c ${HP2100D}/hp2100_cpu.c ${HP2100D}/hp2100_cpu_fp.c \
	${HP2100D}/hp2100_cpu_fpp.c ${HP2100D}/hp2100_cpu0.c ${HP2100D}/hp2100_cpu1.c \
	${HP2100D}/hp2100_cpu2.c ${HP2100D}/hp2100_cpu3.c ${HP2100D}/hp2100_cpu4.c \
	${HP2100D}/hp2100_cpu5.c ${HP2100D}/hp2100_cpu6.c ${HP2100D}/hp2100_cpu7.c \
	${HP2100D}/hp2100_di.c ${HP2100D}/hp2100_di_da.c ${HP2100D}/hp2100_disclib.c \
	${HP2100D}/hp2100_dma.c ${HP2100D}/hp2100_dp.c ${HP2100D}/hp2100_dq.c \
	${HP2100D}/hp2100_dr.c ${HP2100D}/hp2100_ds.c ${HP2100D}/hp2100_ipl.c \
	${HP2100D}/hp2100_lps.c ${HP2100D}/hp2100_lpt.c ${HP2100D}/hp2100_mc.c \
	${HP2100D}/hp2100_mem.c ${HP2100D}/hp2100_mpx.c ${HP2100D}/hp2100_ms.c \
	${HP2100D}/hp2100_mt.c ${HP2100D}/hp2100_mux.c ${HP2100D}/hp2100_pif.c \
	${HP2100D}/hp2100_pt.c ${HP2100D}/hp2100_sys.c ${HP2100D}/hp2100_tbg.c \
	${HP2100D}/hp2100_tty.c
HP2100_OPT = -D HAVE_INT64 -I ${HP2100D}

HP3000D = HP3000
HP3000 = ${HP3000D}/hp_disclib.c ${HP3000D}/hp_tapelib.c ${HP3000D}/hp3000_atc.c \
	${HP3000D}/hp3000_clk.c ${HP3000D}/hp3000_cpu.c ${HP3000D}/hp3000_cpu_base.c \
	${HP3000D}/hp3000_cpu_fp.c ${HP3000D}/hp3000_cpu_cis.c ${HP3000D}/hp3000_ds.c \
	${HP3000D}/hp3000_iop.c ${HP3000D}/hp3000_lp.c ${HP3000D}/hp3000_mem.c \
	${HP3000D}/hp3000_mpx.c ${HP3000D}/hp3000_ms.c ${HP3000D}/hp3000_scmb.c \
	${HP3000D}/hp3000_sel.c ${HP3000D}/hp3000_sys.c
HP3000_OPT = -I ${HP3000D}


#
# Build everything
#
ALL = hp2100 hp3000

all : ${ALL}

clean :
ifeq ($(WIN32),)
	${RM} $(addprefix ${BIN}/,$(addsuffix ${EXE},${ALL}))
else
	if exist ${BIN}\hp*.exe del /q ${BIN}\hp*.exe
#	if exist BIN rmdir BIN
endif

#
# Individual builds
#

hp2100 : ${BIN}/hp2100${EXE}

${BIN}/hp2100${EXE} : ${HP2100} ${SIM}
ifneq (1,$(CPP_BUILD)$(CPP_FORCE))
#	${MKDIRBIN}
	${CC} ${HP2100} ${SIM} ${HP2100_OPT} $(CC_OUTSPEC) ${LDFLAGS}
else
	$(info hp2100 can't be built using C++)
endif


hp3000 : ${BIN}/hp3000${EXE}

${BIN}/hp3000${EXE} : ${HP3000} ${SIM}
ifneq (1,$(CPP_BUILD)$(CPP_FORCE))
#	${MKDIRBIN}
	${CC} ${HP3000} ${SIM} ${HP3000_OPT} $(CC_OUTSPEC) ${LDFLAGS}
else
	$(info hp3000 can't be built using C++)
endif
