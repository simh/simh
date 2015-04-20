#
# This GNU make makefile has been tested on:
#   Linux (x86 & Sparc & PPC)
#   OS X
#   Solaris (x86 & Sparc) (gcc and Sun C)
#   OpenBSD
#   NetBSD
#   FreeBSD
#   HP-UX
#   AIX
#   Windows (MinGW & cygwin)
#   Linux x86 targeting Android (using agcc script)
#   Haiku x86 (with gcc4)
#
# Android targeted builds should invoke GNU make with GCC=agcc on
# the command line.
#
# In general, the logic below will detect and build with the available
# features which the host build environment provides.
#
# Dynamic loading of libpcap is the preferred default behavior if pcap.h 
# is available at build time.  Support to statically linking against libpcap
# is deprecated and may be removed in the future.  Static linking against 
# libpcap can be enabled if GNU make is invoked with USE_NETWORK=1 on the 
# command line.
#
# Some platforms may not have vendor supplied libpcap available.  HP-UX is 
# one such example.  The packages which are available for this platform
# install include files and libraries in user specified directories.  In 
# order for this makefile to locate where these components may have been 
# installed, gmake should be invoked with LPATH=/usr/lib:/usr/local/lib 
# defined (adjusted as needed depending on where they may be installed).
#
# The default build will build compiler optimized binaries.
# If debugging is desired, then GNU make can be invoked with
# DEBUG=1 on the command line.
#
# OSX and other environments may have the LLVM (clang) compiler 
# installed.  If you want to build with the clang compiler, invoke
# make with GCC=clang.
#
# Internal ROM support can be disabled if GNU make is invoked with
# DONT_USE_ROMS=1 on the command line.
#
# Asynchronous I/O support can be disabled if GNU make is invoked with
# NOASYNCH=1 on the command line.
#
# For linting (or other code analyzers) make may be invoked similar to:
#
#   make GCC=cppcheck CC_OUTSPEC= LDFLAGS= CFLAGS_G="--enable=all --template=gcc" CC_STD=--std=c99
#
# CC Command (and platform available options).  (Poor man's autoconf)
#
ifeq (old,$(shell gmake --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ if ($$3 < "3.81") {print "old"} }'))
  GMAKE_VERSION = $(shell gmake --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ print $$3 }')
  $(warning *** Warning *** GNU Make Version $(GMAKE_VERSION) is too old to)
  $(warning *** Warning *** fully process this makefile)
endif
# building the pdp11, or any vax simulator could use networking support
BUILD_SINGLE := $(MAKECMDGOALS) $(BLANK_SUFFIX)
ifneq (,$(or $(findstring pdp11,$(MAKECMDGOALS)),$(findstring vax,$(MAKECMDGOALS)),$(findstring all,$(MAKECMDGOALS))))
  NETWORK_USEFUL = true
  ifneq (,$(or $(findstring microvax1,$(MAKECMDGOALS)),$(findstring microvax2,$(MAKECMDGOALS)),$(findstring microvax3900,$(MAKECMDGOALS)),$(findstring XXvaxXX,$(addsuffix XX,$(addprefix XX,$(MAKECMDGOALS))))))
    VIDEO_USEFUL = true
  endif
  ifneq (,$(findstring all,$(MAKECMDGOALS))$(word 2,$(MAKECMDGOALS)))
    BUILD_MULTIPLE = s
    VIDEO_USEFUL = true
    DISPLAY_USEFUL = true
  endif
else ifneq (,$(findstring besm6,$(MAKECMDGOALS)))
  VIDEO_USEFUL = true
  BESM6_BUILD = true
else
  ifeq ($(MAKECMDGOALS),)
    # default target is all
    NETWORK_USEFUL = true
    VIDEO_USEFUL = true
    DISPLAY_USEFUL = true
    BUILD_MULTIPLE = s
    BUILD_SINGLE := all $(BUILD_SINGLE)
  else
    ifneq (,$(or $(or $(findstring pdp1,$(MAKECMDGOALS)),$(findstring pdp11,$(MAKECMDGOALS))),$(findstring tx-0,$(MAKECMDGOALS))))
      DISPLAY_USEFUL = true
    endif
  endif
endif
find_lib = $(abspath $(strip $(firstword $(foreach dir,$(strip $(LIBPATH)),$(wildcard $(dir)/lib$(1).$(LIBEXT))))))
find_include = $(abspath $(strip $(firstword $(foreach dir,$(strip $(INCPATH)),$(wildcard $(dir)/$(1).h)))))
ifneq ($(findstring Windows,$(OS)),)
  ifeq ($(findstring .exe,$(SHELL)),.exe)
    # MinGW
    WIN32 := 1
  else # Msys or cygwin
    ifeq (MINGW,$(findstring MINGW,$(shell uname)))
      $(info *** This makefile can not be used with the Msys bash shell)
      $(error *** Use build_mingw.bat $(MAKECMDGOALS) from a Windows command prompt)
    endif
  endif
endif
ifeq ($(WIN32),)  #*nix Environments (&& cygwin)
  ifeq ($(GCC),)
    ifeq (,$(shell which gcc 2>/dev/null))
      $(info *** Warning *** Using local cc since gcc isn't available locally.)
      $(info *** Warning *** You may need to install gcc to build working simulators.)
      GCC = cc
    else
      GCC = gcc
    endif
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
        endif
      endif
      ifeq (HP-UX,$(OSTYPE))
        ifneq (,$(shell what `which $(firstword $(GCC)) 2>&1`| grep -i compiler))
          COMPILER_NAME = $(strip $(shell what `which $(firstword $(GCC)) 2>&1` | grep -i compiler))
        endif
      endif
    endif
  else
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
  endif
  ifeq (git-repo,$(shell if $(TEST) -d ./.git; then echo git-repo; fi))
    ifeq (need-hooks,$(shell if $(TEST) ! -e ./.git/hooks/post-checkout; then echo need-hooks; fi))
      $(info *** Installing git hooks in local repository ***)
      GIT_HOOKS += $(shell /bin/cp './Visual Studio Projects/git-hooks/post-commit' ./.git/hooks/)
      GIT_HOOKS += $(shell /bin/cp './Visual Studio Projects/git-hooks/post-checkout' ./.git/hooks/)
      GIT_HOOKS += $(shell /bin/cp './Visual Studio Projects/git-hooks/post-merge' ./.git/hooks/)
      GIT_HOOKS += $(shell ./.git/hooks/post-checkout)
      ifneq (,$(strip $(GIT_HOOKS)))
        $(info *** Warning - Error installing git hooks *** $(GIT_HOOKS))
      endif
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
  else # Non-Android Builds
    INCPATH:=/usr/include
    LIBPATH:=/usr/lib
    OS_CCDEFS = -D_GNU_SOURCE
    GCC_OPTIMIZERS_CMD = $(GCC) -v --help 2>&1
    GCC_WARNINGS_CMD = $(GCC) -v --help 2>&1
    LD_ELF = $(shell echo | $(GCC) -E -dM - | grep __ELF__)
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      INCPATH:=$(shell $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | grep -v 'framework directory' | tr -d '\n')
      ifeq (incopt,$(shell if $(TEST) -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libopt,$(shell if $(TEST) -d /opt/local/lib; then echo libopt; fi))
        LIBPATH += /opt/local/lib
        OS_LDFLAGS += -L/opt/local/lib
      endif
      ifeq (libXt,$(shell if $(TEST) -d /usr/X11/lib; then echo libXt; fi))
        LIBPATH += /usr/X11/lib
        OS_LDFLAGS += -L/usr/X11/lib
      endif
    else
      ifeq (Linux,$(OSTYPE))
        LIBPATH := $(sort $(foreach lib,$(shell /sbin/ldconfig -p | grep ' => /' | sed 's/^.* => //'),$(dir $(lib))))
        LIBEXT = so
      else
        ifeq (SunOS,$(OSTYPE))
          OSNAME = Solaris
          LIBPATH := $(shell crle | grep 'Default Library Path' | awk '{ print $$5 }' | sed 's/:/ /g')
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
            INCPATH += ../windows-build/winpcap/WpdPack/include
            LIBPATH += ../windows-build/winpcap/WpdPack/lib
            PCAPLIB = wpcap
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
                    LDSEARCH :=$(shell ldconfig -r | grep 'search directories' | awk '{print $$3}' | sed 's/:/ /g')
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
                      OS_LDFLAGS += $(patsubst %,-L%,$(LIBPATH))
                    endif
                  endif
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
              OS_LDFLAGS += -L/X11R7/pkg/lib -R/usr/X11R7/lib
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
  need_search = $(strip $(shell ld -l$(1) /dev/null 2>&1 | grep $(1) | sed s/$(1)//))
  LD_SEARCH_NEEDED := $(call need_search,ZzzzzzzZ)
  ifneq (,$(call find_lib,m))
    OS_LDFLAGS += -lm
    $(info using libm: $(call find_lib,m))
  endif
  ifneq (,$(call find_lib,rt))
    OS_LDFLAGS += -lrt
    $(info using librt: $(call find_lib,rt))
  endif
  ifneq (,$(call find_include,pthread))
    ifneq (,$(call find_lib,pthread))
      OS_CCDEFS += -DUSE_READER_THREAD
      ifeq (,$(NOASYNCH))
        OS_CCDEFS += -DSIM_ASYNCH_IO 
      endif
      OS_LDFLAGS += -lpthread
      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
    else
      LIBEXTSAVE := $(LIBEXT)
      LIBEXT = a
      ifneq (,$(call find_lib,pthread))
        OS_CCDEFS += -DUSE_READER_THREAD
        ifeq (,$(NOASYNCH))
          OS_CCDEFS += -DSIM_ASYNCH_IO 
        endif
        OS_LDFLAGS += -lpthread
        $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
      else
        ifneq (,$(findstring Haiku,$(OSTYPE)))
          OS_CCDEFS += -DUSE_READER_THREAD
          ifeq (,$(NOASYNCH))
            OS_CCDEFS += -DSIM_ASYNCH_IO 
          endif
          $(info using libpthread: $(call find_include,pthread))
        endif
      endif
      LIBEXT = $(LIBEXTSAVE)        
    endif
  endif
  # Find available RegEx library.  Prefer libpcreposix.
  ifneq (,$(call find_include,pcreposix))
    ifneq (,$(call find_lib,pcreposix))
      OS_CCDEFS += -DHAVE_PCREPOSIX_H
      OS_LDFLAGS += -lpcreposix
      $(info using libpcreposix: $(call find_lib,pcreposix) $(call find_include,pcreposix))
      ifeq ($(LD_SEARCH_NEEDED),$(call need_search,pcreposix))
        OS_LDFLAGS += -L$(dir $(call find_lib,pcreposix))
      endif
    endif
  else
    # If libpcreposix isn't available, fall back to the local regex.h 
    # Presume that the local regex support is available in the C runtime 
    # without a specific reference to a library.  This may not be true on
    # some platforms.
    ifneq (,$(call find_include,regex))
      OS_CCDEFS += -DHAVE_REGEX_H
      $(info using regex: $(call find_include,regex))
    endif
  endif
  ifneq (,$(call find_include,dlfcn))
    ifneq (,$(call find_lib,dl))
      OS_CCDEFS += -DHAVE_DLOPEN=$(LIBEXT)
      OS_LDFLAGS += -ldl
      $(info using libdl: $(call find_lib,dl) $(call find_include,dlfcn))
    else
      ifneq (,$(findstring BSD,$(OSTYPE))$(findstring AIX,$(OSTYPE))$(findstring Haiku,$(OSTYPE)))
        OS_CCDEFS += -DHAVE_DLOPEN=so
        $(info using libdl: $(call find_include,dlfcn))
      else
        ifneq (,$(call find_lib,dld))
          OS_CCDEFS += -DHAVE_DLOPEN=$(LIBEXT)
          OS_LDFLAGS += -ldld
          $(info using libdld: $(call find_lib,dld) $(call find_include,dlfcn))
        endif
      endif
    endif
  endif
  ifneq (,$(call find_include,glob))
    OS_CCDEFS += -DHAVE_GLOB
  else
    ifneq (,$(call find_include,fnmatch))
      OS_CCDEFS += -DHAVE_FNMATCH    
    endif
  endif
  ifneq (,$(VIDEO_USEFUL))
    ifneq (,$(call find_include,SDL2/SDL))
      ifneq (,$(call find_lib,SDL2))
        VIDEO_CCDEFS += -DHAVE_LIBSDL -I$(dir $(call find_include,SDL2/SDL))
        VIDEO_LDFLAGS += -lSDL2
        VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
        $(info using libSDL2: $(call find_lib,SDL2) $(call find_include,SDL2/SDL))
        ifeq (Darwin,$(OSTYPE))
          VIDEO_LDFLAGS += -lobjc -framework cocoa -DSDL_MAIN_AVAILABLE
        endif
      endif
    else
      ifneq (,$(call find_include,SDL/SDL))
        ifneq (,$(call find_lib,SDL))
          VIDEO_CCDEFS += -DHAVE_LIBSDL -I$(dir $(call find_include,SDL/SDL))
          VIDEO_LDFLAGS += -lSDL
          VIDEO_FEATURES = - video capabilities provided by libSDL (Simple Directmedia Layer)
          $(info using libSDL: $(call find_lib,SDL) $(call find_include,SDL/SDL))
          ifeq (Darwin,$(OSTYPE))
            VIDEO_LDFLAGS += -lobjc -framework cocoa -DSDL_MAIN_AVAILABLE
          endif
        endif
      endif
    endif
    ifeq (,$(findstring HAVE_LIBSDL,$(VIDEO_CCDEFS)))
      $(info *** Info ***)
      $(info *** Info *** The simulator$(BUILD_MULTIPLE) you are building could provide more)
      $(info *** Info *** functionality if video support were available on your system.)
      $(info *** Info *** Install the development components of libSDL packaged by your)
      $(info *** Info *** operating system distribution and rebuild your simulator to)
      $(info *** Info *** enable this extra functionality.)
      $(info *** Info ***)
    endif
  endif
  ifneq (,$(DISPLAY_USEFUL))
    ifeq (,$(WIN32))
      ifneq (,$(call find_include,X11/Intrinsic))
        ifneq (,$(call find_lib,Xt))
          DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/x11.c
          DISPLAYVT = ${DISPLAYD}/vt11.c
          DISPLAY_OPT += -DUSE_DISPLAY -I$(dir $(call find_include,X11/Intrinsic))/include -lXt -lX11 -lm
          $(info using display: $(call find_lib,Xt) $(call find_include,X11/Intrinsic))
        endif
        ifneq (,$(GCC_WARNINGS_CMD)$(CLANG_VERSION))
          ifneq (,$(CLANG_VERSION)$(findstring -Wdeprecated-declarations,$(shell $(GCC_WARNINGS_CMD))))
            DISPLAY_OPT += -Wno-deprecated-declarations
          endif
        endif
      endif
    else
      DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/win32.c
      DISPLAYVT = ${DISPLAYD}/vt11.c
      DISPLAY_OPT = -DUSE_DISPLAY -lgdi32
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
      NETWORK_CCDEFS += -DHAVE_PCAP_NETWORK -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
      NETWORK_LAN_FEATURES += PCAP
      ifneq (,$(call find_lib,$(PCAPLIB)))
        ifneq ($(USE_NETWORK),) # Network support specified on the GNU make command line
          NETWORK_CCDEFS += -DUSE_NETWORK
          ifeq (,$(findstring Linux,$(OSTYPE))$(findstring Darwin,$(OSTYPE)))
            $(info *** Warning ***)
            $(info *** Warning *** Statically linking against libpcap is provides no measurable)
            $(info *** Warning *** benefits over dynamically linking libpcap.)
            $(info *** Warning ***)
            $(info *** Warning *** Support for linking this way is currently deprecated and may be removed)
            $(info *** Warning *** in the future.)
            $(info *** Warning ***)
          else
            $(info *** Error ***)
            $(info *** Error *** Statically linking against libpcap is provides no measurable)
            $(info *** Error *** benefits over dynamically linking libpcap.)
            $(info *** Error ***)
            $(info *** Error *** Support for linking statically has been removed on the $(OSTYPE))
            $(info *** Error *** platform.)
            $(info *** Error ***)
            $(error Retry your build without specifying USE_NETWORK=1)
          endif
          ifeq (cygwin,$(OSTYPE))
            # cygwin has no ldconfig so explicitly specify pcap object library
            NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          else
            NETWORK_LDFLAGS = -l$(PCAPLIB)
          endif
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
        else # default build uses dynamic libpcap
          NETWORK_CCDEFS += -DUSE_SHARED
          $(info using libpcap: $(call find_include,pcap))
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
        endif
      else
        LIBEXTSAVE := $(LIBEXT)
        LIBEXT = a
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS += -DUSE_NETWORK
          NETWORK_LDFLAGS := -L$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
        endif
        LIBEXT = $(LIBEXTSAVE)        
      endif
    else
      # On non-Linux platforms, we'll still try to provide deprecated support for libpcap in /usr/local
      INCPATHSAVE := $(INCPATH)
      ifeq (,$(findstring Linux,$(OSTYPE)))
        # Look for package built from tcpdump.org sources with default install target (or cygwin winpcap)
        INCPATH += /usr/local/include
        PCAP_H_FOUND = $(call find_include,pcap)
      endif
      ifneq (,$(strip $(PCAP_H_FOUND)))
        ifneq (,$(shell grep 'pcap/pcap.h' $(call find_include,pcap) | grep include))
          PCAP_H_PATH = $(dir $(call find_include,pcap))pcap/pcap.h
        else
          PCAP_H_PATH = $(call find_include,pcap)
        endif
        ifneq (,$(shell grep pcap_compile $(PCAP_H_PATH) | grep const))
          BPF_CONST_STRING = -DBPF_CONST_STRING
        endif
        LIBEXTSAVE := $(LIBEXT)
        # first check if binary - shared objects are available/installed in the linker known search paths
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
          NETWORK_FEATURES = - dynamic networking support using libpcap components from www.tcpdump.org and locally installed libpcap.$(LIBEXT)
          $(info using libpcap: $(call find_include,pcap))
        else
          LIBPATH += /usr/local/lib
          LIBEXT = a
          ifneq (,$(call find_lib,$(PCAPLIB)))
            $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
            ifeq (cygwin,$(OSTYPE))
              NETWORK_CCDEFS = -DUSE_NETWORK -DHAVE_PCAP_NETWORK -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
              NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
              NETWORK_FEATURES = - static networking support using libpcap components located in the cygwin directories
            else
              NETWORK_CCDEFS := -DUSE_NETWORK -DHAVE_PCAP_NETWORK -isystem -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING) $(call find_lib,$(PCAPLIB))
              NETWORK_FEATURES = - networking support using libpcap components from www.tcpdump.org
              $(info *** Warning ***)
              $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) being built with networking support using)
              $(info *** Warning *** libpcap components from www.tcpdump.org.)
              $(info *** Warning *** Some users have had problems using the www.tcpdump.org libpcap)
              $(info *** Warning *** components for simh networking.  For best results, with)
              $(info *** Warning *** simh networking, it is recommended that you install the)
              $(info *** Warning *** libpcap-dev (or libpcap-devel) package from your $(OSNAME) distribution)
              $(info *** Warning ***)
              $(info *** Warning *** Building with the components manually installed from www.tcpdump.org)
              $(info *** Warning *** is officially deprecated.  Attempting to do so is unsupported.)
              $(info *** Warning ***)
            endif
          else
            $(error using libpcap: $(call find_include,pcap) missing $(PCAPLIB).$(LIBEXT))
          endif
          NETWORK_LAN_FEATURES += PCAP
        endif
        LIBEXT = $(LIBEXTSAVE)
      else
        INCPATH = $(INCPATHSAVE)
        $(info *** Warning ***)
        $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) are being built WITHOUT)
        $(info *** Warning *** libpcap networking support)
        $(info *** Warning ***)
        $(info *** Warning *** To build simulator(s) with libpcap networking support you)
        $(info *** Warning *** should read 0readme_ethernet.txt and follow the instructions)
        $(info *** Warning *** regarding the needed libpcap development components for your)
        $(info *** Warning *** $(OSTYPE) platform)
        $(info *** Warning ***)
      endif
    endif
    # Consider other network connections
    ifneq (,$(call find_lib,vdeplug))
      # libvdeplug requires the use of the OS provided libpcap
      ifeq (,$(findstring usr/local,$(NETWORK_CCDEFS)))
        ifneq (,$(call find_include,libvdeplug))
          # Provide support for vde networking
          NETWORK_CCDEFS += -DHAVE_VDE_NETWORK
          NETWORK_LAN_FEATURES += VDE
          ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
            NETWORK_CCDEFS += -DUSE_NETWORK
          endif
          ifeq (Darwin,$(OSTYPE))
            NETWORK_LDFLAGS += -lvdeplug -L$(dir $(call find_lib,vdeplug))
          else
            NETWORK_LDFLAGS += -lvdeplug -Wl,-R,$(dir $(call find_lib,vdeplug)) -L$(dir $(call find_lib,vdeplug))
          endif
          $(info using libvdeplug: $(call find_lib,vdeplug) $(call find_include,libvdeplug))
        endif
      endif
    endif
    ifeq (,$(findstring HAVE_VDE_NETWORK,$(NETWORK_CCDEFS)))
      # Support is available on Linux for libvdeplug.  Advise on its usage
      ifneq (,$(findstring Linux,$(OSTYPE)))
        ifneq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
          $(info *** Info ***)
          $(info *** Info *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) are being built with)
          $(info *** Info *** minimal libpcap networking support)
          $(info *** Info ***)
        endif
        $(info *** Info *** Simulators on your $(OSNAME) platform can also be built with)
        $(info *** Info *** extended LAN Ethernet networking support by using VDE Ethernet.)
        $(info *** Info ***)
        $(info *** Info *** To build simulator(s) with extended networking support you)
        $(info *** Info *** should read 0readme_ethernet.txt and follow the instructions)
        $(info *** Info *** regarding the needed libvdeplug components for your $(OSNAME))
        $(info *** Info *** platform)
        $(info *** Info ***)
      endif
    endif
    ifneq (,$(call find_include,linux/if_tun))
      # Provide support for Tap networking on Linux
      NETWORK_CCDEFS += -DHAVE_TAP_NETWORK
      NETWORK_LAN_FEATURES += TAP
      ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
        NETWORK_CCDEFS += -DUSE_NETWORK
      endif
    endif
    ifeq (bsdtuntap,$(shell if $(TEST) -e /usr/include/net/if_tun.h -o -e /Library/Extensions/tap.kext; then echo bsdtuntap; fi))
      # Provide support for Tap networking on BSD platforms (including OS X)
      NETWORK_CCDEFS += -DHAVE_TAP_NETWORK -DHAVE_BSDTUNTAP
      NETWORK_LAN_FEATURES += TAP
      ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
        NETWORK_CCDEFS += -DUSE_NETWORK
      endif
    endif
    ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS))$(findstring HAVE_VDE_NETWORK,$(NETWORK_CCDEFS)))
      NETWORK_CCDEFS += -DUSE_NETWORK
      NETWORK_FEATURES = - WITHOUT Local LAN networking support
      $(info *** Warning ***)
      $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) are being built WITHOUT LAN networking support)
      $(info *** Warning ***)
      $(info *** Warning *** To build simulator(s) with networking support you should read)
      $(info *** Warning *** 0readme_ethernet.txt and follow the instructions regarding the)
      $(info *** Warning *** needed libpcap components for your $(OSTYPE) platform)
      $(info *** Warning ***)
    endif
    NETWORK_OPT = $(NETWORK_CCDEFS)
  endif
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
else
  #Win32 Environments (via MinGW32)
  GCC := gcc
  GCC_Path := $(abspath $(dir $(word 1,$(wildcard $(addsuffix /$(GCC).exe,$(subst ;, ,$(PATH)))))))
  ifeq (rename-build-support,$(shell if exist ..\windows-build-windows-build echo rename-build-support))
    REMOVE_OLD_BUILD := $(shell if exist ..\windows-build rmdir/s/q ..\windows-build)
    FIXED_BUILD := $(shell move ..\windows-build-windows-build ..\windows-build >NUL)
  endif
  GCC_VERSION = $(word 3,$(shell $(GCC) --version))
  COMPILER_NAME = GCC Version: $(GCC_VERSION)
  LTO_EXCLUDE_VERSIONS = 4.5.2
  ifeq (,$(PATH_SEPARATOR))
    PATH_SEPARATOR := ;
  endif
  INCPATH = $(abspath $(wildcard $(GCC_Path)\..\include $(subst $(PATH_SEPARATOR), ,$(CPATH))  $(subst $(PATH_SEPARATOR), ,$(C_INCLUDE_PATH))))
  LIBPATH = $(abspath $(wildcard $(GCC_Path)\..\lib $(subst :, ,$(LIBRARY_PATH))))
  $(info lib paths are: $(LIBPATH))
  $(info include paths are: $(INCPATH))
  # Give preference to any MinGW provided threading (if available)
  ifneq (,$(call find_include,pthread))
    PTHREADS_CCDEFS = -DUSE_READER_THREAD
    ifeq (,$(NOASYNCH))
      PTHREADS_CCDEFS += -DSIM_ASYNCH_IO 
    endif
    PTHREADS_LDFLAGS = -lpthread
  else
    ifeq (pthreads,$(shell if exist ..\windows-build\pthreads\Pre-built.2\include\pthread.h echo pthreads))
      PTHREADS_CCDEFS = -DUSE_READER_THREAD -DPTW32_STATIC_LIB -D_POSIX_C_SOURCE -I../windows-build/pthreads/Pre-built.2/include
      ifeq (,$(NOASYNCH))
        PTHREADS_CCDEFS += -DSIM_ASYNCH_IO 
      endif
      PTHREADS_LDFLAGS = -lpthreadGC2 -L..\windows-build\pthreads\Pre-built.2\lib
    endif
  endif
  ifeq (pcap,$(shell if exist ..\windows-build\winpcap\Wpdpack\include\pcap.h echo pcap))
    NETWORK_LDFLAGS =
    NETWORK_OPT = -DUSE_SHARED -I../windows-build/winpcap/Wpdpack/include
    NETWORK_FEATURES = - dynamic networking support using windows-build provided libpcap components
  else
    ifneq (,$(call find_include,pcap))
      NETWORK_LDFLAGS =
      NETWORK_OPT = -DUSE_SHARED
      NETWORK_FEATURES = - dynamic networking support using libpcap components found in the MinGW directories
    endif
  endif
  ifneq (,$(VIDEO_USEFUL))
    SDL_INCLUDE = $(word 1,$(shell dir /b /s ..\windows-build\libSDL\SDL.h))
    ifeq (SDL.h,$(findstring SDL.h,$(SDL_INCLUDE)))
      VIDEO_CCDEFS += -DHAVE_LIBSDL -I$(abspath $(dir $(SDL_INCLUDE)))
      VIDEO_LDFLAGS  += -lSDL2 -L$(abspath $(dir $(SDL_INCLUDE))\..\lib)
      VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
    else
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This build could produce simulators with video capabilities.     **)
      $(info **  However, the required files to achieve this can't be found on    **)
      $(info **  this system.  Download the file:                                 **)
      $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
      $(info **  Extract the windows-build-windows-build folder it contains to    **)
      $(info **  $(abspath ..\)                                                   **)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info .)
    endif
  endif
  OS_CCDEFS += -fms-extensions $(PTHREADS_CCDEFS)
  OS_LDFLAGS += -lm -lwsock32 -lwinmm $(PTHREADS_LDFLAGS)
  EXE = .exe
  ifneq (binexists,$(shell if exist BIN echo binexists))
    MKDIRBIN = if not exist BIN mkdir BIN
  endif
  ifneq ($(USE_NETWORK),)
    NETWORK_OPT += -DUSE_SHARED
  endif
  ifneq (,$(shell if exist .git-commit-id type .git-commit-id))
    GIT_COMMIT_ID=$(shell if exist .git-commit-id type .git-commit-id)
  else
    ifeq (,$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h | findstr Format))
      GIT_COMMIT_ID=$(shell for /F "tokens=3" %%i in ("$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h)") do echo %%i)
    endif
  endif
  ifneq (windows-build,$(shell if exist ..\windows-build\README.md echo windows-build))
    $(info ***********************************************************************)
    $(info ***********************************************************************)
    $(info **  This build is operating without the required windows-build       **)
    $(info **  components and therefore will produce less than optimal          **)
    $(info **  simulator operation and features.                                **)
    $(info **  Download the file:                                               **)
    $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
    $(info **  Extract the windows-build-windows-build folder it contains to    **)
    $(info **  $(abspath ..\)                                                   **)
    $(info ***********************************************************************)
    $(info ***********************************************************************)
    $(info .)
  else
    # Version check on windows-build
    WINDOWS_BUILD = $(shell findstr WINDOWS-BUILD ..\windows-build\Windows-Build_Versions.txt)
    ifeq (,$(WINDOWS_BUILD))
      WINDOWS_BUILD = 00000000
    endif
    ifneq (,$(shell if 20150412 GTR $(WINDOWS_BUILD) echo old-windows-build))
      $(info .)
      $(info windows-build components at: $(abspath ..\windows-build))
      $(info .)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This currently available windows-build components are out of     **)
      $(info **  date.  For the most functional and stable features you shoud     **)
      $(info **  Download the file:                                               **)
      $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
      $(info **  Extract the windows-build-windows-build folder it contains to    **)
      $(info **  $(abspath ..\)                                                   **)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info .)
    endif
    ifeq (pcre,$(shell if exist ..\windows-build\PCRE\include\pcre.h echo pcre))
      OS_CCDEFS += -DHAVE_PCREPOSIX_H -DPCRE_STATIC -I$(abspath ../windows-build/PCRE/include)
      OS_LDFLAGS += -lpcreposix -lpcre -L../windows-build/PCRE/lib/
      $(info using libpcreposix: $(abspath ../windows-build/PCRE/lib/pcreposix.a) $(abspath ../windows-build/PCRE/include/pcreposix.h))
    endif
  endif
endif # Win32 (via MinGW)
ifneq (,$(GIT_COMMIT_ID))
  CFLAGS_GIT = -DSIM_GIT_COMMIT_ID=$(GIT_COMMIT_ID)
endif
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
else
  ifneq (clang,$(findstring clang,$(COMPILER_NAME)))
    CFLAGS_O = -O2
  else
    ifeq (Darwin,$(OSTYPE))
      CFLAGS_O += -O4 -fno-strict-overflow -flto -fwhole-program
    else
      CFLAGS_O := -O2 -fno-strict-overflow 
    endif
  endif
  LDFLAGS_O = 
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
  ifneq (,$(NETWORK_LAN_FEATURES))
    $(info *** - Local LAN packet transports: $(NETWORK_LAN_FEATURES))
  endif
  ifneq (,$(VIDEO_FEATURES))
    $(info *** $(VIDEO_FEATURES).)
  endif
  ifneq (,$(GIT_COMMIT_ID))
    $(info ***)
    $(info *** git commit id is $(GIT_COMMIT_ID).)
  endif
  $(info ***)
endif
ifneq ($(DONT_USE_ROMS),)
  ROMS_OPT = -DDONT_USE_INTERNAL_ROM
else
  BUILD_ROMS = ${BIN}BuildROMs${EXE}
endif
ifneq ($(DONT_USE_READER_THREAD),)
  NETWORK_OPT += -DDONT_USE_READER_THREAD
endif

ifeq (HP-UX,$(OSTYPE))
  CC_STD = -std=gnu99
else
  ifeq (,$(SUNC_VERSION))
    CC_STD = -std=c99
  endif
endif
CC_OUTSPEC = -o $@
CC := $(GCC) $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) $(CFLAGS_GIT) -DSIM_COMPILER="$(COMPILER_NAME)" -I . $(OS_CCDEFS) $(ROMS_OPT)
HOST_CC = $(CC)
LDFLAGS := $(OS_LDFLAGS) $(NETWORK_LDFLAGS) $(LDFLAGS_O)

include sources.mk
include rules.mk
