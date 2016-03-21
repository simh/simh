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
BUILD_SINGLE := $(MAKECMDGOALS) $(BLANK_SUFFIX)
# building the pdp1, pdp11, tx-0, or any microvax simulator could use video support
ifneq (,$(or $(findstring XXpdp1XX,$(addsuffix XX,$(addprefix XX,$(MAKECMDGOALS)))),$(findstring pdp11,$(MAKECMDGOALS)),$(findstring tx-0,$(MAKECMDGOALS)),$(findstring microvax1,$(MAKECMDGOALS)),$(findstring microvax2,$(MAKECMDGOALS)),$(findstring microvax3900,$(MAKECMDGOALS)),$(findstring XXvaxXX,$(addsuffix XX,$(addprefix XX,$(MAKECMDGOALS))))))
  VIDEO_USEFUL = true
endif
# building the besm6 needs both video support and fontfile support
ifneq (,$(findstring besm6,$(MAKECMDGOALS)))
  VIDEO_USEFUL = true
  BESM6_BUILD = true
endif
# building the pdp11, pdp10, or any vax simulator could use networking support
ifneq (,$(or $(findstring pdp11,$(MAKECMDGOALS)),$(findstring pdp10,$(MAKECMDGOALS)),$(findstring vax,$(MAKECMDGOALS)),$(findstring all,$(MAKECMDGOALS))))
  NETWORK_USEFUL = true
  ifneq (,$(findstring all,$(MAKECMDGOALS))$(word 2,$(MAKECMDGOALS)))
    BUILD_MULTIPLE = s
    VIDEO_USEFUL = true
    BESM6_BUILD = true
  endif
else
  ifeq ($(MAKECMDGOALS),)
    # default target is all
    NETWORK_USEFUL = true
    VIDEO_USEFUL = true
    BUILD_MULTIPLE = s
    BUILD_SINGLE := all $(BUILD_SINGLE)
    BESM6_BUILD = true
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
      CC_STD = -std=gnu99
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
    CC_STD = -std=c99
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
    INCPATH:=$(shell LANG=C; $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      INCPATH:=$(shell LANG=C; $(GCC) -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | grep -v 'framework directory' | tr -d '\n')
      ifeq (incbrew,$(shell if $(TEST) -d /usr/local/include && $(TEST) -e /usr/local/bin/brew; then echo incbrew; fi))
        INCPATH += /usr/local/include
        OS_CCDEFS += -I/usr/local/include
      endif
      ifeq (incopt,$(shell if $(TEST) -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libbrew,$(shell if $(TEST) -d /usr/local/lib && $(TEST) -e /usr/local/bin/brew; then echo libbrew; fi))
        LIBPATH += /usr/local/lib
        OS_LDFLAGS += -L/usr/local/lib
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
          LIBPATH := $(shell LANG=C; crle | grep 'Default Library Path' | awk '{ print $$5 }' | sed 's/:/ /g')
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
  ifneq (,$(call find_include,png))
    ifneq (,$(call find_lib,png))
      OS_CCDEFS += -DHAVE_LIBPNG
      OS_LDFLAGS += -lpng
      $(info using libpng: $(call find_lib,png) $(call find_include,png))
    endif
  endif
  ifneq (,$(call find_include,glob))
    OS_CCDEFS += -DHAVE_GLOB
  else
    ifneq (,$(call find_include,fnmatch))
      OS_CCDEFS += -DHAVE_FNMATCH    
    endif
  endif
  ifneq (,$(call find_include,sys/mman))
    ifneq (,$(shell grep shm_open $(call find_include,sys/mman)))
      OS_CCDEFS += -DHAVE_SHM_OPEN
      $(info using mman: $(call find_include,sys/mman))
    endif
  endif
  ifneq (,$(VIDEO_USEFUL))
    ifeq (cygwin,$(OSTYPE))
      LIBEXTSAVE := $(LIBEXT)
      LIBEXT = dll.a
    endif
    ifneq (,$(call find_include,SDL2/SDL))
      ifneq (,$(call find_lib,SDL2))
        VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO -I$(dir $(call find_include,SDL2/SDL))
        VIDEO_LDFLAGS += -lSDL2
        VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
        DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
        DISPLAYVT = ${DISPLAYD}/vt11.c
        DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
        $(info using libSDL2: $(call find_lib,SDL2) $(call find_include,SDL2/SDL))
        ifeq (Darwin,$(OSTYPE))
          VIDEO_LDFLAGS += -lobjc -framework cocoa -DSDL_MAIN_AVAILABLE
        endif
      endif
    else
      ifneq (,$(call find_include,SDL/SDL))
        ifneq (,$(call find_lib,SDL))
          VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO -I$(dir $(call find_include,SDL/SDL))
          VIDEO_LDFLAGS += -lSDL
          VIDEO_FEATURES = - video capabilities provided by libSDL (Simple Directmedia Layer)
          DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
          DISPLAYVT = ${DISPLAYD}/vt11.c
          DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
          $(info using libSDL: $(call find_lib,SDL) $(call find_include,SDL/SDL))
          ifeq (Darwin,$(OSTYPE))
            VIDEO_LDFLAGS += -lobjc -framework cocoa -DSDL_MAIN_AVAILABLE
          endif
        endif
      endif
    endif
    ifeq (cygwin,$(OSTYPE))
      LIBEXT = $(LIBEXTSAVE)
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
    ifeq (slirp,$(shell if $(TEST) -e slirp_glue/sim_slirp.c; then echo slirp; fi))
      NETWORK_CCDEFS += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c
      NETWORK_LAN_FEATURES += NAT(SLiRP)
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
  CC_STD = -std=gnu99
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
    NETWORK_LAN_FEATURES += PCAP
  else
    ifneq (,$(call find_include,pcap))
      NETWORK_LDFLAGS =
      NETWORK_OPT = -DUSE_SHARED
      NETWORK_FEATURES = - dynamic networking support using libpcap components found in the MinGW directories
      NETWORK_LAN_FEATURES += PCAP
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
    ifeq (slirp,slirp)
      NETWORK_OPT += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c -lIphlpapi
      NETWORK_LAN_FEATURES += NAT(SLiRP)
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

CC_OUTSPEC = -o $@
CC := $(GCC) $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) $(CFLAGS_GIT) -DSIM_COMPILER="$(COMPILER_NAME)" -I . $(OS_CCDEFS) $(ROMS_OPT)
LDFLAGS := $(OS_LDFLAGS) $(NETWORK_LDFLAGS) $(LDFLAGS_O)

#
# Common Libraries
#
BIN = BIN/
SIM = scp.c sim_console.c sim_fio.c sim_timer.c sim_sock.c \
	sim_tmxr.c sim_ether.c sim_tape.c sim_disk.c sim_serial.c \
	sim_video.c sim_imd.c

DISPLAYD = display
  
#
# Emulator source files and compile time options
#
PDP1D = PDP1
ifneq (,$(DISPLAY_OPT))
  PDP1_DISPLAY_OPT = -DDISPLAY_TYPE=DIS_TYPE30 -DPIX_SCALE=RES_HALF
endif
PDP1 = ${PDP1D}/pdp1_lp.c ${PDP1D}/pdp1_cpu.c ${PDP1D}/pdp1_stddev.c \
	${PDP1D}/pdp1_sys.c ${PDP1D}/pdp1_dt.c ${PDP1D}/pdp1_drm.c \
	${PDP1D}/pdp1_clk.c ${PDP1D}/pdp1_dcs.c ${PDP1D}/pdp1_dpy.c ${DISPLAYL}
PDP1_OPT = -I ${PDP1D} $(DISPLAY_OPT) $(PDP1_DISPLAY_OPT)


NOVAD = NOVA
NOVA = ${NOVAD}/nova_sys.c ${NOVAD}/nova_cpu.c ${NOVAD}/nova_dkp.c \
	${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c ${NOVAD}/nova_mta.c \
	${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c ${NOVAD}/nova_clk.c \
	${NOVAD}/nova_tt.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
NOVA_OPT = -I ${NOVAD}


ECLIPSE = ${NOVAD}/eclipse_cpu.c ${NOVAD}/eclipse_tt.c ${NOVAD}/nova_sys.c \
	${NOVAD}/nova_dkp.c ${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c \
	${NOVAD}/nova_mta.c ${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c \
	${NOVAD}/nova_clk.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
ECLIPSE_OPT = -I ${NOVAD} -DECLIPSE


PDP18BD = PDP18B
PDP18B = ${PDP18BD}/pdp18b_dt.c ${PDP18BD}/pdp18b_drm.c ${PDP18BD}/pdp18b_cpu.c \
	${PDP18BD}/pdp18b_lp.c ${PDP18BD}/pdp18b_mt.c ${PDP18BD}/pdp18b_rf.c \
	${PDP18BD}/pdp18b_rp.c ${PDP18BD}/pdp18b_stddev.c ${PDP18BD}/pdp18b_sys.c \
	${PDP18BD}/pdp18b_rb.c ${PDP18BD}/pdp18b_tt1.c ${PDP18BD}/pdp18b_fpp.c
PDP4_OPT = -DPDP4 -I ${PDP18BD}
PDP7_OPT = -DPDP7 -I ${PDP18BD}
PDP9_OPT = -DPDP9 -I ${PDP18BD}
PDP15_OPT = -DPDP15 -I ${PDP18BD}


PDP11D = PDP11
PDP11 = ${PDP11D}/pdp11_fp.c ${PDP11D}/pdp11_cpu.c ${PDP11D}/pdp11_dz.c \
	${PDP11D}/pdp11_cis.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rx.c \
	${PDP11D}/pdp11_stddev.c ${PDP11D}/pdp11_sys.c ${PDP11D}/pdp11_tc.c \
	${PDP11D}/pdp11_tm.c ${PDP11D}/pdp11_ts.c ${PDP11D}/pdp11_io.c \
	${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_tq.c ${PDP11D}/pdp11_pclk.c \
	${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_vh.c \
	${PDP11D}/pdp11_rh.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_cpumod.c \
	${PDP11D}/pdp11_cr.c ${PDP11D}/pdp11_rf.c ${PDP11D}/pdp11_dl.c \
	${PDP11D}/pdp11_ta.c ${PDP11D}/pdp11_rc.c ${PDP11D}/pdp11_kg.c \
	${PDP11D}/pdp11_ke.c ${PDP11D}/pdp11_dc.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_kmc.c ${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_rs.c \
	${PDP11D}/pdp11_vt.c ${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c $(DISPLAYL) $(DISPLAYVT)
PDP11_OPT = -DVM_PDP11 -I ${PDP11D} ${NETWORK_OPT} $(DISPLAY_OPT)


VAXD = VAX
VAX = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c ${VAXD}/vax_io.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_stddev.c ${VAXD}/vax_sysdev.c \
	${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c ${VAXD}/vax_syslist.c \
	${VAXD}/vax_vc.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_2681.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX_OPT = -DVM_VAX -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}


VAX610 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax610_stddev.c ${VAXD}/vax610_sysdev.c ${VAXD}/vax610_io.c \
	${VAXD}/vax610_syslist.c ${VAXD}/vax610_mem.c ${VAXD}/vax_vc.c \
	${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_2681.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX610_OPT = -DVM_VAX -DVAX_610 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}

VAX630 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax630_stddev.c ${VAXD}/vax630_sysdev.c \
	${VAXD}/vax630_io.c ${VAXD}/vax630_syslist.c ${VAXD}/vax_vc.c \
	${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_2681.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX620_OPT = -DVM_VAX -DVAX_620 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}
VAX630_OPT = -DVM_VAX -DVAX_630 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}


VAX730 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax730_stddev.c ${VAXD}/vax730_sys.c \
	${VAXD}/vax730_mem.c ${VAXD}/vax730_uba.c ${VAXD}/vax730_rb.c \
	${VAXD}/vax730_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_io_lib.c
VAX730_OPT = -DVM_VAX -DVAX_730 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


VAX750 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax750_stddev.c ${VAXD}/vax750_cmi.c \
	${VAXD}/vax750_mem.c ${VAXD}/vax750_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax750_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_tu.c \
	${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_dup.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX750_OPT = -DVM_VAX -DVAX_750 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


VAX780 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax780_stddev.c ${VAXD}/vax780_sbi.c \
	${VAXD}/vax780_mem.c ${VAXD}/vax780_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax780_fload.c ${VAXD}/vax780_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_dup.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX780_OPT = -DVM_VAX -DVAX_780 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


VAX8600 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax860_stddev.c ${VAXD}/vax860_sbia.c \
	${VAXD}/vax860_abus.c ${VAXD}/vax780_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax860_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_dup.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c
VAX8600_OPT = -DVM_VAX -DVAX_860 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


PDP10D = PDP10
PDP10 = ${PDP10D}/pdp10_fe.c ${PDP11D}/pdp11_dz.c ${PDP10D}/pdp10_cpu.c \
	${PDP10D}/pdp10_ksio.c ${PDP10D}/pdp10_lp20.c ${PDP10D}/pdp10_mdfp.c \
	${PDP10D}/pdp10_pag.c ${PDP10D}/pdp10_rp.c ${PDP10D}/pdp10_sys.c \
	${PDP10D}/pdp10_tim.c ${PDP10D}/pdp10_tu.c ${PDP10D}/pdp10_xtnd.c \
	${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_kmc.c \
	${PDP11D}/pdp11_xu.c
PDP10_OPT = -DVM_PDP10 -DUSE_INT64 -I ${PDP10D} -I ${PDP11D} ${NETWORK_OPT}


PDP8D = PDP8
PDP8 = ${PDP8D}/pdp8_cpu.c ${PDP8D}/pdp8_clk.c ${PDP8D}/pdp8_df.c \
	${PDP8D}/pdp8_dt.c ${PDP8D}/pdp8_lp.c ${PDP8D}/pdp8_mt.c \
	${PDP8D}/pdp8_pt.c ${PDP8D}/pdp8_rf.c ${PDP8D}/pdp8_rk.c \
	${PDP8D}/pdp8_rx.c ${PDP8D}/pdp8_sys.c ${PDP8D}/pdp8_tt.c \
	${PDP8D}/pdp8_ttx.c ${PDP8D}/pdp8_rl.c ${PDP8D}/pdp8_tsc.c \
	${PDP8D}/pdp8_td.c ${PDP8D}/pdp8_ct.c ${PDP8D}/pdp8_fpp.c
PDP8_OPT = -I ${PDP8D}


H316D = H316
H316 = ${H316D}/h316_stddev.c ${H316D}/h316_lp.c ${H316D}/h316_cpu.c \
	${H316D}/h316_sys.c ${H316D}/h316_mt.c ${H316D}/h316_fhd.c \
	${H316D}/h316_dp.c ${H316D}/h316_rtc.c ${H316D}/h316_imp.c \
	${H316D}/h316_hi.c ${H316D}/h316_mi.c ${H316D}/h316_udp.c 
H316_OPT = -I ${H316D} -D VM_IMPTIP


HP2100D = HP2100
HP2100 = ${HP2100D}/hp2100_stddev.c ${HP2100D}/hp2100_dp.c ${HP2100D}/hp2100_dq.c \
	${HP2100D}/hp2100_dr.c ${HP2100D}/hp2100_lps.c ${HP2100D}/hp2100_ms.c \
	${HP2100D}/hp2100_mt.c ${HP2100D}/hp2100_mux.c ${HP2100D}/hp2100_cpu.c \
	${HP2100D}/hp2100_fp.c ${HP2100D}/hp2100_sys.c ${HP2100D}/hp2100_lpt.c \
	${HP2100D}/hp2100_ipl.c ${HP2100D}/hp2100_ds.c ${HP2100D}/hp2100_cpu0.c \
	${HP2100D}/hp2100_cpu1.c ${HP2100D}/hp2100_cpu2.c ${HP2100D}/hp2100_cpu3.c \
	${HP2100D}/hp2100_cpu4.c ${HP2100D}/hp2100_cpu5.c ${HP2100D}/hp2100_cpu6.c \
	${HP2100D}/hp2100_cpu7.c ${HP2100D}/hp2100_fp1.c ${HP2100D}/hp2100_baci.c \
	${HP2100D}/hp2100_mpx.c ${HP2100D}/hp2100_pif.c ${HP2100D}/hp2100_di.c \
	${HP2100D}/hp2100_di_da.c ${HP2100D}/hp2100_disclib.c
HP2100_OPT = -DHAVE_INT64 -I ${HP2100D}

HP3000D = HP3000
HP3000 = ${HP3000D}/hp_disclib.c ${HP3000D}/hp_tapelib.c ${HP3000D}/hp3000_atc.c \
	${HP3000D}/hp3000_clk.c ${HP3000D}/hp3000_cpu.c ${HP3000D}/hp3000_cpu_base.c \
	${HP3000D}/hp3000_cpu_fp.c ${HP3000D}/hp3000_ds.c ${HP3000D}/hp3000_iop.c \
	${HP3000D}/hp3000_mpx.c ${HP3000D}/hp3000_ms.c \
	${HP3000D}/hp3000_scmb.c ${HP3000D}/hp3000_sel.c ${HP3000D}/hp3000_sys.c
HP3000_OPT = -I ${HP3000D}


I1401D = I1401
I1401 = ${I1401D}/i1401_lp.c ${I1401D}/i1401_cpu.c ${I1401D}/i1401_iq.c \
	${I1401D}/i1401_cd.c ${I1401D}/i1401_mt.c ${I1401D}/i1401_dp.c \
	${I1401D}/i1401_sys.c
I1401_OPT = -I ${I1401D}


I1620D = I1620
I1620 = ${I1620D}/i1620_cd.c ${I1620D}/i1620_dp.c ${I1620D}/i1620_pt.c \
	${I1620D}/i1620_tty.c ${I1620D}/i1620_cpu.c ${I1620D}/i1620_lp.c \
	${I1620D}/i1620_fp.c ${I1620D}/i1620_sys.c
I1620_OPT = -I ${I1620D}


I7094D = I7094
I7094 = ${I7094D}/i7094_cpu.c ${I7094D}/i7094_cpu1.c ${I7094D}/i7094_io.c \
	${I7094D}/i7094_cd.c ${I7094D}/i7094_clk.c ${I7094D}/i7094_com.c \
	${I7094D}/i7094_drm.c ${I7094D}/i7094_dsk.c ${I7094D}/i7094_sys.c \
	${I7094D}/i7094_lp.c ${I7094D}/i7094_mt.c ${I7094D}/i7094_binloader.c
I7094_OPT = -DUSE_INT64 -I ${I7094D}


IBM1130D = Ibm1130
IBM1130 = ${IBM1130D}/ibm1130_cpu.c ${IBM1130D}/ibm1130_cr.c \
	${IBM1130D}/ibm1130_disk.c ${IBM1130D}/ibm1130_stddev.c \
	${IBM1130D}/ibm1130_sys.c ${IBM1130D}/ibm1130_gdu.c \
	${IBM1130D}/ibm1130_gui.c ${IBM1130D}/ibm1130_prt.c \
	${IBM1130D}/ibm1130_fmt.c ${IBM1130D}/ibm1130_ptrp.c \
	${IBM1130D}/ibm1130_plot.c ${IBM1130D}/ibm1130_sca.c \
	${IBM1130D}/ibm1130_t2741.c
IBM1130_OPT = -I ${IBM1130D}
ifneq ($(WIN32),)
IBM1130_OPT += -DGUI_SUPPORT -lgdi32
endif  


ID16D = Interdata
ID16 = ${ID16D}/id16_cpu.c ${ID16D}/id16_sys.c ${ID16D}/id_dp.c \
	${ID16D}/id_fd.c ${ID16D}/id_fp.c ${ID16D}/id_idc.c ${ID16D}/id_io.c \
	${ID16D}/id_lp.c ${ID16D}/id_mt.c ${ID16D}/id_pas.c ${ID16D}/id_pt.c \
	${ID16D}/id_tt.c ${ID16D}/id_uvc.c ${ID16D}/id16_dboot.c ${ID16D}/id_ttp.c
ID16_OPT = -I ${ID16D}


ID32D = Interdata
ID32 = ${ID32D}/id32_cpu.c ${ID32D}/id32_sys.c ${ID32D}/id_dp.c \
	${ID32D}/id_fd.c ${ID32D}/id_fp.c ${ID32D}/id_idc.c ${ID32D}/id_io.c \
	${ID32D}/id_lp.c ${ID32D}/id_mt.c ${ID32D}/id_pas.c ${ID32D}/id_pt.c \
	${ID32D}/id_tt.c ${ID32D}/id_uvc.c ${ID32D}/id32_dboot.c ${ID32D}/id_ttp.c
ID32_OPT = -I ${ID32D}


S3D = S3
S3 = ${S3D}/s3_cd.c ${S3D}/s3_cpu.c ${S3D}/s3_disk.c ${S3D}/s3_lp.c \
	${S3D}/s3_pkb.c ${S3D}/s3_sys.c
S3_OPT = -I ${S3D}


ALTAIRD = ALTAIR
ALTAIR = ${ALTAIRD}/altair_sio.c ${ALTAIRD}/altair_cpu.c ${ALTAIRD}/altair_dsk.c \
	${ALTAIRD}/altair_sys.c
ALTAIR_OPT = -I ${ALTAIRD}


ALTAIRZ80D = AltairZ80
ALTAIRZ80 = ${ALTAIRZ80D}/altairz80_cpu.c ${ALTAIRZ80D}/altairz80_cpu_nommu.c \
	${ALTAIRZ80D}/altairz80_dsk.c ${ALTAIRZ80D}/disasm.c \
	${ALTAIRZ80D}/altairz80_sio.c ${ALTAIRZ80D}/altairz80_sys.c \
	${ALTAIRZ80D}/altairz80_hdsk.c ${ALTAIRZ80D}/altairz80_net.c \
	${ALTAIRZ80D}/flashwriter2.c ${ALTAIRZ80D}/i86_decode.c \
	${ALTAIRZ80D}/i86_ops.c ${ALTAIRZ80D}/i86_prim_ops.c \
	${ALTAIRZ80D}/i8272.c ${ALTAIRZ80D}/insnsd.c ${ALTAIRZ80D}/altairz80_mhdsk.c \
	${ALTAIRZ80D}/mfdc.c ${ALTAIRZ80D}/n8vem.c ${ALTAIRZ80D}/vfdhd.c \
	${ALTAIRZ80D}/s100_disk1a.c ${ALTAIRZ80D}/s100_disk2.c ${ALTAIRZ80D}/s100_disk3.c \
	${ALTAIRZ80D}/s100_fif.c ${ALTAIRZ80D}/s100_mdriveh.c \
	${ALTAIRZ80D}/s100_mdsa.c \
	${ALTAIRZ80D}/s100_mdsad.c ${ALTAIRZ80D}/s100_selchan.c \
	${ALTAIRZ80D}/s100_ss1.c ${ALTAIRZ80D}/s100_64fdc.c \
	${ALTAIRZ80D}/s100_scp300f.c \
	${ALTAIRZ80D}/wd179x.c ${ALTAIRZ80D}/s100_hdc1001.c \
	${ALTAIRZ80D}/s100_if3.c ${ALTAIRZ80D}/s100_adcs6.c \
	${ALTAIRZ80D}/m68kcpu.c ${ALTAIRZ80D}/m68kdasm.c \
	${ALTAIRZ80D}/m68kopac.c ${ALTAIRZ80D}/m68kopdm.c \
	${ALTAIRZ80D}/m68kopnz.c ${ALTAIRZ80D}/m68kops.c ${ALTAIRZ80D}/m68ksim.c
ALTAIRZ80_OPT = -I ${ALTAIRZ80D} -DUSE_SIM_IMD


GRID = GRI
GRI = ${GRID}/gri_cpu.c ${GRID}/gri_stddev.c ${GRID}/gri_sys.c
GRI_OPT = -I ${GRID}


LGPD = LGP
LGP = ${LGPD}/lgp_cpu.c ${LGPD}/lgp_stddev.c ${LGPD}/lgp_sys.c
LGP_OPT = -I ${LGPD}


SDSD = SDS
SDS = ${SDSD}/sds_cpu.c ${SDSD}/sds_drm.c ${SDSD}/sds_dsk.c ${SDSD}/sds_io.c \
	${SDSD}/sds_lp.c ${SDSD}/sds_mt.c ${SDSD}/sds_mux.c ${SDSD}/sds_rad.c \
	${SDSD}/sds_stddev.c ${SDSD}/sds_sys.c
SDS_OPT = -I ${SDSD}


SWTP6800D = swtp6800/swtp6800
SWTP6800C = swtp6800/common
SWTP6800MP-A = ${SWTP6800C}/mp-a.c ${SWTP6800C}/m6800.c ${SWTP6800C}/m6810.c \
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800C}/mp-s.c ${SWTP6800D}/mp-a_sys.c \
	${SWTP6800C}/mp-b2.c ${SWTP6800C}/mp-8m.c
SWTP6800MP-A2 = ${SWTP6800C}/mp-a2.c ${SWTP6800C}/m6800.c ${SWTP6800C}/m6810.c \
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800C}/mp-s.c ${SWTP6800D}/mp-a2_sys.c \
	${SWTP6800C}/mp-b2.c ${SWTP6800C}/mp-8m.c ${SWTP6800C}/i2716.c
SWTP6800_OPT = -I ${SWTP6800D}


ISYS8010D = Intel-Systems/isys8010
ISYS8010C = Intel-Systems/common
ISYS8010 = ${ISYS8010C}/i8080.c ${ISYS8010D}/isys8010_sys.c \
	${ISYS8010C}/i8251.c ${ISYS8010C}/i8255.c \
	${ISYS8010C}/ieprom.c ${ISYS8010C}/iram8.c \
	${ISYS8010C}/multibus.c ${ISYS8010C}/isbc80-10.c	\
	${ISYS8010C}/isbc064.c ${ISYS8010C}/isbc208.c
ISYS8010_OPT = -I ${ISYS8010D}


ISYS8020D = Intel-Systems/isys8020
ISYS8020C = Intel-Systems/common
ISYS8020 = ${ISYS8020C}/i8080.c ${ISYS8020D}/isys8020_sys.c \
	${ISYS8020C}/i8251.c ${ISYS8020C}/i8255.c \
	${ISYS8020C}/ieprom.c ${ISYS8020C}/iram8.c \
	${ISYS8020C}/multibus.c ${ISYS8020C}/isbc80-20.c	\
	${ISYS8020C}/isbc064.c ${ISYS8020C}/isbc208.c \
	${ISYS8020C}/i8259.c
ISYS8020_OPT = -I ${ISYS8020D}


TX0D = TX-0
TX0 = ${TX0D}/tx0_cpu.c ${TX0D}/tx0_dpy.c ${TX0D}/tx0_stddev.c \
      ${TX0D}/tx0_sys.c ${TX0D}/tx0_sys_orig.c ${DISPLAYL}
TX0_OPT = -I ${TX0D} $(DISPLAY_OPT)


SSEMD = SSEM
SSEM = ${SSEMD}/ssem_cpu.c ${SSEMD}/ssem_sys.c
SSEM_OPT = -I ${SSEMD}

B5500D = B5500
B5500 = ${B5500D}/b5500_cpu.c ${B5500D}/b5500_io.c ${B5500D}/b5500_sys.c \
	${B5500D}/b5500_dk.c ${B5500D}/b5500_mt.c ${B5500D}/b5500_urec.c \
	${B5500D}/b5500_dr.c ${B5500D}/b5500_dtc.c ${B5500D}/sim_card.c
B5500_OPT = -I.. -DUSE_INT64 -DB5500

###
### Experimental simulators
###

BESM6D = BESM6
BESM6 = ${BESM6D}/besm6_cpu.c ${BESM6D}/besm6_sys.c ${BESM6D}/besm6_mmu.c \
        ${BESM6D}/besm6_arith.c ${BESM6D}/besm6_disk.c ${BESM6D}/besm6_drum.c \
        ${BESM6D}/besm6_tty.c ${BESM6D}/besm6_panel.c ${BESM6D}/besm6_printer.c \
        ${BESM6D}/besm6_punch.c

ifneq (,$(and ${VIDEO_LDFLAGS}, $(BESM6_BUILD)))
    ifeq (,${FONTFILE})
        FONTPATH += /usr/share/fonts /Library/Fonts /usr/lib/jvm /System/Library/Frameworks/JavaVM.framework/Versions C:/Windows/Fonts
        FONTPATH := $(dir $(foreach dir,$(strip $(FONTPATH)),$(wildcard $(dir)/.)))
        FONTNAME += DejaVuSans.ttf LucidaSansRegular.ttf FreeSans.ttf AppleGothic.ttf tahoma.ttf
        $(info font paths are: $(FONTPATH))
        $(info font names are: $(FONTNAME))
        find_fontfile = $(strip $(firstword $(foreach dir,$(strip $(FONTPATH)),$(wildcard $(dir)/$(1))$(wildcard $(dir)/*/$(1))$(wildcard $(dir)/*/*/$(1))$(wildcard $(dir)/*/*/*/$(1)))))
        find_font = $(abspath $(strip $(firstword $(foreach font,$(strip $(FONTNAME)),$(call find_fontfile,$(font))))))
        ifneq (,$(call find_font))
            FONTFILE=$(call find_font)
        else
            $(info ***)
            $(info *** No font file available, BESM-6 video panel disabled.)
            $(info ***)
            $(info *** To enable the panel display please specify one of:)
            $(info ***          a font path with FONTNAME=path)
            $(info ***          a font name with FONTNAME=fontname.ttf)
            $(info ***          a font file with FONTFILE=path/fontname.ttf)
            $(info ***)
        endif
    endif
endif
ifeq (,$(and ${VIDEO_LDFLAGS}, ${FONTFILE}))
    BESM6_OPT = -I ${BESM6D} -DUSE_INT64 
else ifneq (,$(and $(findstring SDL2,${VIDEO_LDFLAGS}),$(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf)))
    $(info using libSDL2_ttf: $(call find_lib,SDL2_ttf) $(call find_include,SDL2/SDL_ttf))
    $(info ***)
    BESM6_OPT = -I ${BESM6D} -DFONTFILE=${FONTFILE} -DUSE_INT64 ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS} -lSDL2_ttf
else ifneq (,$(and $(call find_include,SDL/SDL_ttf),$(call find_lib,SDL_ttf)))
    $(info using libSDL_ttf: $(call find_lib,SDL_ttf) $(call find_include,SDL/SDL_ttf))
    $(info ***)
    BESM6_OPT = -I ${BESM6D} -DFONTFILE=${FONTFILE} -DUSE_INT64 ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS} -lSDL_ttf
else
    BESM6_OPT = -I ${BESM6D} -DUSE_INT64 
endif

###
### Unsupported/Incomplete simulators
###

SIGMAD = sigma
SIGMA = ${SIGMAD}/sigma_cpu.c ${SIGMAD}/sigma_sys.c ${SIGMAD}/sigma_cis.c \
	${SIGMAD}/sigma_coc.c ${SIGMAD}/sigma_dk.c ${SIGMAD}/sigma_dp.c \
	${SIGMAD}/sigma_fp.c ${SIGMAD}/sigma_io.c ${SIGMAD}/sigma_lp.c \
	${SIGMAD}/sigma_map.c ${SIGMAD}/sigma_mt.c ${SIGMAD}/sigma_pt.c \
    ${SIGMAD}/sigma_rad.c ${SIGMAD}/sigma_rtc.c ${SIGMAD}/sigma_tt.c
SIGMA_OPT = -I ${SIGMAD}

ALPHAD = alpha
ALPHA = ${ALPHAD}/alpha_500au_syslist.c ${ALPHAD}/alpha_cpu.c \
    ${ALPHAD}/alpha_ev5_cons.c ${ALPHAD}/alpha_ev5_pal.c \
    ${ALPHAD}/alpha_ev5_tlb.c ${ALPHAD}/alpha_fpi.c \
    ${ALPHAD}/alpha_fpv.c ${ALPHAD}/alpha_io.c \
    ${ALPHAD}/alpha_mmu.c ${ALPHAD}/alpha_sys.c
ALPHA_OPT = -I ${ALPHAD} -DUSE_ADDR64 -DUSE_INT64

SAGED = SAGE
SAGE = ${SAGED}/sage_cpu.c ${SAGED}/sage_sys.c ${SAGED}/sage_stddev.c \
    ${SAGED}/sage_cons.c ${SAGED}/sage_fd.c ${SAGED}/sage_lp.c \
    ${SAGED}/m68k_cpu.c ${SAGED}/m68k_mem.c ${SAGED}/m68k_scp.c \
    ${SAGED}/m68k_parse.tab.c ${SAGED}/m68k_sys.c \
    ${SAGED}/i8251.c ${SAGED}/i8253.c ${SAGED}/i8255.c ${SAGED}/i8259.c ${SAGED}/i8272.c 
SAGE_OPT = -I ${SAGED} -DHAVE_INT64 -DUSE_SIM_IMD

PDQ3D = PDQ-3
PDQ3 = ${PDQ3D}/pdq3_cpu.c ${PDQ3D}/pdq3_sys.c ${PDQ3D}/pdq3_stddev.c \
    ${PDQ3D}/pdq3_mem.c ${PDQ3D}/pdq3_debug.c ${PDQ3D}/pdq3_fdc.c 
PDQ3_OPT = -I ${PDQ3D} -DUSE_SIM_IMD


#
# Build everything (not the unsupported/incomplete simulators)
#
ALL = pdp1 pdp4 pdp7 pdp8 pdp9 pdp15 pdp11 pdp10 \
	vax microvax3900 microvax1 rtvax1000 microvax2 vax730 vax750 vax780 vax8600 \
	nova eclipse hp2100 hp3000 i1401 i1620 s3 altair altairz80 gri \
	i7094 ibm1130 id16 id32 sds lgp h316 \
	swtp6800mp-a swtp6800mp-a2 tx-0 ssem isys8010 isys8020 \
	b5500

all : ${ALL}

clean :
ifeq ($(WIN32),)
	${RM} -r ${BIN}
else
	if exist BIN\*.exe del /q BIN\*.exe
	if exist BIN rmdir BIN
endif

${BIN}BuildROMs${EXE} :
	${MKDIRBIN}
ifeq (agcc,$(findstring agcc,$(firstword $(CC))))
	gcc $(wordlist 2,1000,${CC}) sim_BuildROMs.c $(CC_OUTSPEC)
else
	${CC} sim_BuildROMs.c $(CC_OUTSPEC)
endif
ifeq ($(WIN32),)
	$@
	${RM} $@
  ifeq (Darwin,$(OSTYPE)) # remove Xcode's debugging symbols folder too
	${RM} -rf $@.dSYM
  endif
else
	$(@D)\$(@F)
	del $(@D)\$(@F)
endif

#
# Individual builds
#
pdp1 : ${BIN}pdp1${EXE}

${BIN}pdp1${EXE} : ${PDP1} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP1} ${SIM} ${PDP1_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp4 : ${BIN}pdp4${EXE}

${BIN}pdp4${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP4_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp7 : ${BIN}pdp7${EXE}

${BIN}pdp7${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP7_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp8 : ${BIN}pdp8${EXE}

${BIN}pdp8${EXE} : ${PDP8} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP8} ${SIM} ${PDP8_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp9 : ${BIN}pdp9${EXE}

${BIN}pdp9${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP9_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp15 : ${BIN}pdp15${EXE}

${BIN}pdp15${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP15_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp10 : ${BIN}pdp10${EXE}

${BIN}pdp10${EXE} : ${PDP10} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP10} ${SIM} ${PDP10_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdp11 : ${BIN}BuildROMs${EXE} ${BIN}pdp11${EXE}

${BIN}pdp11${EXE} : ${PDP11} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP11} ${SIM} ${PDP11_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax : microvax3900

microvax3900 : ${BIN}BuildROMs${EXE} ${BIN}microvax3900${EXE}

${BIN}microvax3900${EXE} : ${VAX} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX} ${SIM} ${VAX_OPT} $(CC_OUTSPEC) ${LDFLAGS}
ifeq ($(WIN32),)
	cp ${BIN}microvax3900${EXE} ${BIN}vax${EXE}
else
	copy $(@D)\microvax3900${EXE} $(@D)\vax${EXE}
endif

microvax1 : ${BIN}BuildROMs${EXE} ${BIN}microvax1${EXE}

${BIN}microvax1${EXE} : ${VAX610} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX610} ${SIM} ${VAX610_OPT} -o $@ ${LDFLAGS}

rtvax1000 : ${BIN}BuildROMs${EXE} ${BIN}rtvax1000${EXE}

${BIN}rtvax1000${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX620_OPT} -o $@ ${LDFLAGS}

microvax2 : ${BIN}BuildROMs${EXE} ${BIN}microvax2${EXE}

${BIN}microvax2${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX630_OPT} -o $@ ${LDFLAGS}

vax730 : ${BIN}BuildROMs${EXE} ${BIN}vax730${EXE}

${BIN}vax730${EXE} : ${VAX730} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX730} ${SIM} ${VAX730_OPT} -o $@ ${LDFLAGS}

vax750 : ${BIN}BuildROMs${EXE} ${BIN}vax750${EXE}

${BIN}vax750${EXE} : ${VAX750} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX750} ${SIM} ${VAX750_OPT} -o $@ ${LDFLAGS}

vax780 : ${BIN}BuildROMs${EXE} ${BIN}vax780${EXE}

${BIN}vax780${EXE} : ${VAX780} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX780} ${SIM} ${VAX780_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax8600 : ${BIN}BuildROMs${EXE} ${BIN}vax8600${EXE}

${BIN}vax8600${EXE} : ${VAX8600} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX8600} ${SIM} ${VAX8600_OPT} $(CC_OUTSPEC) ${LDFLAGS}

nova : ${BIN}nova${EXE}

${BIN}nova${EXE} : ${NOVA} ${SIM}
	${MKDIRBIN}
	${CC} ${NOVA} ${SIM} ${NOVA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

eclipse : ${BIN}eclipse${EXE}

${BIN}eclipse${EXE} : ${ECLIPSE} ${SIM}
	${MKDIRBIN}
	${CC} ${ECLIPSE} ${SIM} ${ECLIPSE_OPT} $(CC_OUTSPEC) ${LDFLAGS}

h316 : ${BIN}h316${EXE}

${BIN}h316${EXE} : ${H316} ${SIM}
	${MKDIRBIN}
	${CC} ${H316} ${SIM} ${H316_OPT} $(CC_OUTSPEC) ${LDFLAGS}

hp2100 : ${BIN}hp2100${EXE}

${BIN}hp2100${EXE} : ${HP2100} ${SIM}
	${MKDIRBIN}
	${CC} ${HP2100} ${SIM} ${HP2100_OPT} $(CC_OUTSPEC) ${LDFLAGS}

hp3000 : ${BIN}hp3000${EXE}

${BIN}hp3000${EXE} : ${HP3000} ${SIM}
	${MKDIRBIN}
	${CC} ${HP3000} ${SIM} ${HP3000_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i1401 : ${BIN}i1401${EXE}

${BIN}i1401${EXE} : ${I1401} ${SIM}
	${MKDIRBIN}
	${CC} ${I1401} ${SIM} ${I1401_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i1620 : ${BIN}i1620${EXE}

${BIN}i1620${EXE} : ${I1620} ${SIM}
	${MKDIRBIN}
	${CC} ${I1620} ${SIM} ${I1620_OPT} $(CC_OUTSPEC) ${LDFLAGS}

i7094 : ${BIN}i7094${EXE}

${BIN}i7094${EXE} : ${I7094} ${SIM}
	${MKDIRBIN}
	${CC} ${I7094} ${SIM} ${I7094_OPT} $(CC_OUTSPEC) ${LDFLAGS}

ibm1130 : ${BIN}ibm1130${EXE}

${BIN}ibm1130${EXE} : ${IBM1130}
	${MKDIRBIN}
ifneq ($(WIN32),)
	windres ${IBM1130D}/ibm1130.rc $(BIN)ibm1130.o
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} $(BIN)ibm1130.o $(CC_OUTSPEC) ${LDFLAGS}
	del BIN\ibm1130.o
else
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} $(CC_OUTSPEC) ${LDFLAGS}
endif  

s3 : ${BIN}s3${EXE}

${BIN}s3${EXE} : ${S3} ${SIM}
	${MKDIRBIN}
	${CC} ${S3} ${SIM} ${S3_OPT} $(CC_OUTSPEC) ${LDFLAGS}

altair : ${BIN}altair${EXE}

${BIN}altair${EXE} : ${ALTAIR} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIR} ${SIM} ${ALTAIR_OPT} $(CC_OUTSPEC) ${LDFLAGS}

altairz80 : ${BIN}altairz80${EXE}

${BIN}altairz80${EXE} : ${ALTAIRZ80} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIRZ80} ${SIM} ${ALTAIRZ80_OPT} $(CC_OUTSPEC) ${LDFLAGS}

gri : ${BIN}gri${EXE}

${BIN}gri${EXE} : ${GRI} ${SIM}
	${MKDIRBIN}
	${CC} ${GRI} ${SIM} ${GRI_OPT} $(CC_OUTSPEC) ${LDFLAGS}

lgp : ${BIN}lgp${EXE}

${BIN}lgp${EXE} : ${LGP} ${SIM}
	${MKDIRBIN}
	${CC} ${LGP} ${SIM} ${LGP_OPT} $(CC_OUTSPEC) ${LDFLAGS}

id16 : ${BIN}id16${EXE}

${BIN}id16${EXE} : ${ID16} ${SIM}
	${MKDIRBIN}
	${CC} ${ID16} ${SIM} ${ID16_OPT} $(CC_OUTSPEC) ${LDFLAGS}

id32 : ${BIN}id32${EXE}

${BIN}id32${EXE} : ${ID32} ${SIM}
	${MKDIRBIN}
	${CC} ${ID32} ${SIM} ${ID32_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sds : ${BIN}sds${EXE}

${BIN}sds${EXE} : ${SDS} ${SIM}
	${MKDIRBIN}
	${CC} ${SDS} ${SIM} ${SDS_OPT} $(CC_OUTSPEC) ${LDFLAGS}

swtp6800mp-a : ${BIN}BuildROMs${EXE} ${BIN}swtp6800mp-a${EXE}

${BIN}swtp6800mp-a${EXE} : ${SWTP6800MP-A} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}

swtp6800mp-a2 : ${BIN}BuildROMs${EXE} ${BIN}swtp6800mp-a2${EXE}

${BIN}swtp6800mp-a2${EXE} : ${SWTP6800MP-A2} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A2} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}


isys8010: ${BIN}isys8010${EXE}

${BIN}isys8010${EXE} : ${ISYS8010} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8010} ${SIM} ${ISYS8010_OPT} $(CC_OUTSPEC) ${LDFLAGS}

isys8020: ${BIN}isys8020${EXE}

${BIN}isys8020${EXE} : ${ISYS8020} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8020} ${SIM} ${ISYS8020_OPT} $(CC_OUTSPEC) ${LDFLAGS}

tx-0 : ${BIN}tx-0${EXE}

${BIN}tx-0${EXE} : ${TX0} ${SIM}
	${MKDIRBIN}
	${CC} ${TX0} ${SIM} ${TX0_OPT} $(CC_OUTSPEC) ${LDFLAGS}

ssem : ${BIN}ssem${EXE}

${BIN}ssem${EXE} : ${SSEM} ${SIM}
	${MKDIRBIN}
	${CC} ${SSEM} ${SIM} ${SSEM_OPT} $(CC_OUTSPEC) ${LDFLAGS}

besm6 : ${BIN}besm6${EXE}

${BIN}besm6${EXE} : ${BESM6} ${SIM}
	${MKDIRBIN}
	${CC} ${BESM6} ${SIM} ${BESM6_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sigma : ${BIN}sigma${EXE}

${BIN}sigma${EXE} : ${SIGMA} ${SIM}
	${MKDIRBIN}
	${CC} ${SIGMA} ${SIM} ${SIGMA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

alpha : ${BIN}alpha${EXE}

${BIN}alpha${EXE} : ${ALPHA} ${SIM}
	${MKDIRBIN}
	${CC} ${ALPHA} ${SIM} ${ALPHA_OPT} $(CC_OUTSPEC) ${LDFLAGS}

sage : ${BIN}sage${EXE}

${BIN}sage${EXE} : ${SAGE} ${SIM}
	${MKDIRBIN}
	${CC} ${SAGE} ${SIM} ${SAGE_OPT} $(CC_OUTSPEC) ${LDFLAGS}

pdq3 : ${BIN}pdq3${EXE}

${BIN}pdq3${EXE} : ${PDQ3} ${SIM}
	${MKDIRBIN}
	${CC} ${PDQ3} ${SIM} ${PDQ3_OPT} $(CC_OUTSPEC) ${LDFLAGS}

b5500 : $(BIN)b5500$(EXE)

${BIN}b5500${EXE} : ${B5500} ${SIM} 
	${MKDIRBIN}
	${CC} ${B5500} ${SIM} ${B5500_OPT} $(CC_OUTSPEC) ${LDFLAGS}

# Front Panel API Demo/Test program

frontpaneltest : ${BIN}frontpaneltest${EXE}

${BIN}frontpaneltest${EXE} : frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c
	${MKDIRBIN}
	${CC} frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c $(CC_OUTSPEC) ${LDFLAGS}

