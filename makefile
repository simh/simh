#
# This GNU make makefile has been tested on:
#   Linux (x86 & Sparc & PPC)
#   Android (Termux)
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
# In the unlikely event that someone wants to build network capable 
# simulators without networking support, invoking GNU make with 
# NONETWORK=1 on the command line will do the trick.
#
# By default, video support is enabled if the SDL2 development
# headers and libraries are available.  To force a build without video
# support, invoke GNU make with NOVIDEO=1 on the command line.
#
# The default build will build compiler optimized binaries.
# If debugging is desired, then GNU make can be invoked with
# DEBUG=1 on the command line.
#
# The default build will run per simulator tests if they are 
# available.  If building without running tests is desired, 
# then GNU make should be invoked with TESTS=0 on the command 
# line.
#
# Default test execution will produce summary output.  Detailed
# test output can be produced if GNU make is invoked with 
# TEST_ARG=-v on the command line.
#
# simh project support is provided for simulators that are built with 
# dependent packages provided with the or by the operating system 
# distribution OR for platforms where that isn't directly available 
# (OS X/macOS) by packages from specific package management systems (MacPorts 
# or Homebrew).  Users wanting to build simulators with locally built 
# dependent packages or packages provided by an unsupported package 
# management system may be able to override where this procedure looks 
# for include files and/or libraries.  Overrides can be specified by define 
# exported environment variables or GNU make command line arguments which 
# specify INCLUDES and/or LIBRARIES.  
# Each of these, if specified, must be the complete list include directories
# or library directories that should be used with each element separated by 
# colons. (i.e. INCLUDES=/usr/include/:/usr/local/include/:...)
# If this doesn't work for you and/or you're interested in using a different 
# ToolChain, you're free to solve this problem on your own.  Good Luck.
#
# Some environments may have the LLVM (clang) compiler installed as
# an alternate to gcc.  If you want to build with the clang compiler, 
# invoke make with GCC=clang.
#
# Internal ROM support can be disabled if GNU make is invoked with
# DONT_USE_ROMS=1 on the command line.
#
# For linting (or other code analyzers) make may be invoked similar to:
#
#   make GCC=cppcheck CC_OUTSPEC= LDFLAGS= CFLAGS_G="--enable=all --template=gcc" CC_STD=--std=c99
#
# CC Command (and platform available options).  (Poor man's autoconf)
#
ifneq (,${GREP_OPTIONS})
  $(info GREP_OPTIONS is defined in your environment.)
  $(info )
  $(info This variable interfers with the proper operation of this script.)
  $(info )
  $(info The GREP_OPTIONS environment variable feature of grep is deprecated)
  $(info for exactly this reason and will be removed from future versions of)
  $(info grep.  The grep man page suggests that you use an alias or a script)
  $(info to invoke grep with your preferred options.)
  $(info )
  $(info unset the GREP_OPTIONS environment variable to use this makefile)
  $(error 1)
endif
ifeq (old,$(shell gmake --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ if ($$3 < "3.81") {print "old"} }'))
  GMAKE_VERSION = $(shell gmake --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ print $$3 }')
  $(warning *** Warning *** GNU Make Version $(GMAKE_VERSION) is too old to)
  $(warning *** Warning *** fully process this makefile)
endif
BUILD_SINGLE := ${MAKECMDGOALS} $(BLANK_SUFFIX)
BUILD_MULTIPLE_VERB = is
# building the pdp1, pdp11, tx-0, or any microvax simulator could use video support
ifneq (,$(or $(findstring XXpdp1XX,$(addsuffix XX,$(addprefix XX,${MAKECMDGOALS}))),$(findstring pdp11,${MAKECMDGOALS}),$(findstring tx-0,${MAKECMDGOALS}),$(findstring microvax1,${MAKECMDGOALS}),$(findstring microvax2,${MAKECMDGOALS}),$(findstring microvax3900,${MAKECMDGOALS}),$(findstring microvax2000,${MAKECMDGOALS}),$(findstring vaxstation3100,${MAKECMDGOALS}),$(findstring XXvaxXX,$(addsuffix XX,$(addprefix XX,${MAKECMDGOALS})))))
  VIDEO_USEFUL = true
endif
# building the besm6 needs both video support and fontfile support
ifneq (,$(findstring besm6,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
  BESM6_BUILD = true
endif
# building the Imlac needs video support
ifneq (,$(findstring imlac,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
endif
# building the TT2500 needs video support
ifneq (,$(findstring tt2500,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
endif
# building the PDP6, KA10 or KI10 needs video support
ifneq (,$(or $(findstring pdp6,${MAKECMDGOALS}),$(findstring pdp10-ka,${MAKECMDGOALS}),$(findstring pdp10-ki,${MAKECMDGOALS})))
  VIDEO_USEFUL = true
endif
# building the KA10, KI10 or KL10 networking can be used.
ifneq (,$(or $(findstring pdp10-ka,${MAKECMDGOALS}),$(findstring pdp10-ki,${MAKECMDGOALS},$(findstring pdp10-kl,${MAKECMDGOALS}))))
  NETWORK_USEFUL = true
endif
# building the PDP-7 needs video support
ifneq (,$(findstring pdp7,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
endif
# building the pdp11, pdp10, or any vax simulator could use networking support
ifneq (,$(or $(findstring pdp11,${MAKECMDGOALS}),$(findstring pdp10,${MAKECMDGOALS}),$(findstring vax,${MAKECMDGOALS}),$(findstring infoserver,${MAKECMDGOALS}),$(findstring 3b2,${MAKECMDGOALS})$(findstring all,${MAKECMDGOALS})))
  NETWORK_USEFUL = true
  ifneq (,$(findstring all,${MAKECMDGOALS}))
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
    VIDEO_USEFUL = true
    BESM6_BUILD = true
  endif
  ifneq (,$(word 2,${MAKECMDGOALS}))
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
  endif
else
  ifeq (${MAKECMDGOALS},)
    # default target is all
    NETWORK_USEFUL = true
    VIDEO_USEFUL = true
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
    BUILD_SINGLE := all $(BUILD_SINGLE)
    BESM6_BUILD = true
  endif
endif
# someone may want to explicitly build simulators without network support
ifneq ($(NONETWORK),)
  NETWORK_USEFUL =
endif
# ... or without video support
ifneq ($(NOVIDEO),)
  VIDEO_USEFUL =
endif
ifneq ($(findstring Windows,${OS}),)
  ifeq ($(findstring .exe,${SHELL}),.exe)
    # MinGW
    WIN32 := 1
    # Tests don't run under MinGW
    TESTS := 0
  else # Msys or cygwin
    ifeq (MINGW,$(findstring MINGW,$(shell uname)))
      $(info *** This makefile can not be used with the Msys bash shell)
      $(error Use build_mingw.bat ${MAKECMDGOALS} from a Windows command prompt)
    endif
  endif
endif

find_exe = $(abspath $(strip $(firstword $(foreach dir,$(strip $(subst :, ,${PATH})),$(wildcard $(dir)/$(1))))))
find_lib = $(abspath $(strip $(firstword $(foreach dir,$(strip ${LIBPATH}),$(wildcard $(dir)/lib$(1).${LIBEXT})))))
find_include = $(abspath $(strip $(firstword $(foreach dir,$(strip ${INCPATH}),$(wildcard $(dir)/$(1).h)))))
ifneq (0,$(TESTS))
  find_test = RegisterSanityCheck $(abspath $(wildcard $(1)/tests/$(2)_test.ini)) </dev/null
  TESTING_FEATURES = - Per simulator tests will be run
else
  TESTING_FEATURES = - Per simulator tests will be skipped
endif
ifeq (${WIN32},)  #*nix Environments (&& cygwin)
  ifeq (${GCC},)
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
  ifeq (Darwin,$(OSTYPE))
    ifeq (,$(shell which port)$(shell which brew))
      $(info *** Info *** simh dependent packages on macOS must be provided by either the)
      $(info *** Info *** MacPorts package system or by the HomeBrew package system.)
      $(info *** Info *** Neither of these seem to be installed on the local system.)
      $(info *** Info ***)
      ifeq (,$(INCLUDES)$(LIBRARIES))
        $(info *** Info *** Users wanting to build simulators with locally built dependent)
        $(info *** Info *** packages or packages provided by an unsupported package)
        $(info *** Info *** management system may be able to override where this procedure)
        $(info *** Info *** looks for include files and/or libraries.  Overrides can be)
        $(info *** Info *** specified by defining exported environment variables or GNU make)
        $(info *** Info *** command line arguments which specify INCLUDES and/or LIBRARIES.)
        $(info *** Info *** If this works, that's great, if it doesn't you are on your own!)
      else
        $(info *** Warning *** Attempting to build on macOS with:)
        $(info *** Warning *** INCLUDES defined as $(INCLUDES))
        $(info *** Warning *** and)
        $(info *** Warning *** LIBRARIES defined as $(LIBRARIES))
      endif
    endif
  endif
  ifeq (,$(shell ${GCC} -v /dev/null 2>&1 | grep 'clang'))
    GCC_VERSION = $(shell ${GCC} -v /dev/null 2>&1 | grep 'gcc version' | awk '{ print $$3 }')
    COMPILER_NAME = GCC Version: $(GCC_VERSION)
    ifeq (,$(GCC_VERSION))
      ifeq (SunOS,$(OSTYPE))
        ifneq (,$(shell ${GCC} -V 2>&1 | grep 'Sun C'))
          SUNC_VERSION = $(shell ${GCC} -V 2>&1 | grep 'Sun C')
          COMPILER_NAME = $(wordlist 2,10,$(SUNC_VERSION))
          CC_STD = -std=c99
        endif
      endif
      ifeq (HP-UX,$(OSTYPE))
        ifneq (,$(shell what `which $(firstword ${GCC}) 2>&1`| grep -i compiler))
          COMPILER_NAME = $(strip $(shell what `which $(firstword ${GCC}) 2>&1` | grep -i compiler))
          CC_STD = -std=gnu99
        endif
      endif
    else
      ifeq (,$(findstring ++,${GCC}))
        CC_STD = -std=gnu99
      else
        CPP_BUILD = 1
      endif
    endif
  else
    ifeq (Apple,$(shell ${GCC} -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 }'))
      COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
      CLANG_VERSION = $(word 4,$(COMPILER_NAME))
    else
      COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 }')
      CLANG_VERSION = $(word 3,$(COMPILER_NAME))
      ifeq (,$(findstring .,$(CLANG_VERSION)))
        COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
        CLANG_VERSION = $(word 4,$(COMPILER_NAME))
      endif
    endif
    ifeq (,$(findstring ++,${GCC}))
      CC_STD = -std=c99
    else
      CPP_BUILD = 1
      OS_CCDEFS += -Wno-deprecated
    endif
  endif
  ifeq (git-repo,$(shell if ${TEST} -d ./.git; then echo git-repo; fi))
    GIT_PATH=$(strip $(shell which git))
    ifeq (,$(GIT_PATH))
      $(error building using a git repository, but git is not available)
    endif
    ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
      CURRENT_GIT_COMMIT_ID=$(strip $(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }'))
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty="%H"))
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        NEED_COMMIT_ID = need-commit-id
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell rm -f .git/hooks/post-checkout .git/hooks/post-commit .git/hooks/post-merge)
      endif
    else
      NEED_COMMIT_ID = need-commit-id
    endif
    ifeq (need-commit-id,$(NEED_COMMIT_ID))
      ifneq (,$(shell git update-index --refresh --))
        GIT_EXTRA_FILES=+uncommitted-changes
      endif
      isodate=$(shell git log -1 --pretty="%ai"|sed -e 's/ /T/'|sed -e 's/ //')
      $(shell git log -1 --pretty="SIM_GIT_COMMIT_ID %H$(GIT_EXTRA_FILES)%nSIM_GIT_COMMIT_TIME $(isodate)" >.git-commit-id)
    endif
  endif
  LTO_EXCLUDE_VERSIONS = 
  PCAPLIB = pcap
  ifeq (agcc,$(findstring agcc,${GCC})) # Android target build?
    OS_CCDEFS = -D_GNU_SOURCE -DSIM_ASYNCH_IO 
    OS_LDFLAGS = -lm
  else # Non-Android (or Native Android) Builds
    ifeq (,$(INCLUDES)$(LIBRARIES))
      INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
      ifeq (,${INCPATH})
        INCPATH:=/usr/include
      endif
      LIBPATH:=/usr/lib
    else
      $(info *** Warning ***)
      ifeq (,$(INCLUDES))
        INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
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
    GCC_OPTIMIZERS_CMD = ${GCC} -v --help 2>&1
    GCC_WARNINGS_CMD = ${GCC} -v --help 2>&1
    LD_ELF = $(shell echo | ${GCC} -E -dM - | grep __ELF__)
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      ifneq (include,$(findstring include,$(UNSUPPORTED_BUILD)))
        INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | grep -v 'framework directory' | tr -d '\n')
      endif
      ifeq (incopt,$(shell if ${TEST} -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libopt,$(shell if ${TEST} -d /opt/local/lib; then echo libopt; fi))
        LIBPATH += /opt/local/lib
        OS_LDFLAGS += -L/opt/local/lib
      endif
      ifeq (HomeBrew,$(shell if ${TEST} -d /usr/local/Cellar; then echo HomeBrew; fi))
        INCPATH += $(foreach dir,$(wildcard /usr/local/Cellar/*/*),$(dir)/include)
        LIBPATH += $(foreach dir,$(wildcard /usr/local/Cellar/*/*),$(dir)/lib)
      endif
      ifeq (libXt,$(shell if ${TEST} -d /usr/X11/lib; then echo libXt; fi))
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
            ifneq (,$(shell if ${TEST} -d ${PREFIX}/lib; then echo prefixlib; fi))
              LIBPATH += ${PREFIX}/lib
            endif
            ifneq (,$(shell if ${TEST} -d /system/lib; then echo systemlib; fi))
              LIBPATH += /system/lib
            endif
            LIBPATH += $(LD_LIBRARY_PATH)
          endif
          ifeq (ldconfig,$(shell if ${TEST} -e /sbin/ldconfig; then echo ldconfig; fi))
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
          ifeq (incsfw,$(shell if ${TEST} -d /opt/sfw/include; then echo incsfw; fi))
            INCPATH += /opt/sfw/include
            OS_CCDEFS += -I/opt/sfw/include
          endif
          ifeq (libsfw,$(shell if ${TEST} -d /opt/sfw/lib; then echo libsfw; fi))
            LIBPATH += /opt/sfw/lib
            OS_LDFLAGS += -L/opt/sfw/lib -R/opt/sfw/lib
          endif
          OS_CCDEFS += -D_LARGEFILE_SOURCE
        else
          ifeq (cygwin,$(OSTYPE))
            # use 0readme_ethernet.txt documented Windows pcap build components
            INCPATH += ../windows-build/winpcap/WpdPack/Include
            LIBPATH += ../windows-build/winpcap/WpdPack/Lib
            PCAPLIB = wpcap
            LIBEXT = a
          else
            ifneq (,$(findstring AIX,$(OSTYPE)))
              OS_LDFLAGS += -lm -lrt
              ifeq (incopt,$(shell if ${TEST} -d /opt/freeware/include; then echo incopt; fi))
                INCPATH += /opt/freeware/include
                OS_CCDEFS += -I/opt/freeware/include
              endif
              ifeq (libopt,$(shell if ${TEST} -d /opt/freeware/lib; then echo libopt; fi))
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
                  OS_LDFLAGS += $(patsubst %,-L%,${LIBPATH})
                endif
              endif
            endif
            ifeq (usrpkglib,$(shell if ${TEST} -d /usr/pkg/lib; then echo usrpkglib; fi))
              LIBPATH += /usr/pkg/lib
              INCPATH += /usr/pkg/include
              OS_LDFLAGS += -L/usr/pkg/lib -R/usr/pkg/lib
              OS_CCDEFS += -I/usr/pkg/include
            endif
            ifeq (X11R7,$(shell if ${TEST} -d /usr/X11R7/lib; then echo X11R7; fi))
              LIBPATH += /usr/X11R7/lib
              INCPATH += /usr/X11R7/include
              OS_LDFLAGS += -L/usr/X11R7/lib -R/usr/X11R7/lib
              OS_CCDEFS += -I/usr/X11R7/include
            endif
            ifeq (/usr/local/lib,$(findstring /usr/local/lib,${LIBPATH}))
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
        ifeq (,$(shell ${GCC} -v /dev/null 2>&1 | grep '\-\-enable-lto'))
          LTO_EXCLUDE_VERSIONS += $(GCC_VERSION)
        endif
      endif
    endif
  endif
  $(info lib paths are: ${LIBPATH})
  $(info include paths are: ${INCPATH})
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
      OS_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
      OS_LDFLAGS += -lpthread
      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
    else
      LIBEXTSAVE := ${LIBEXT}
      LIBEXT = a
      ifneq (,$(call find_lib,pthread))
        OS_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
        OS_LDFLAGS += -lpthread
        $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
      else
        ifneq (,$(findstring Haiku,$(OSTYPE)))
          OS_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
          $(info using libpthread: $(call find_include,pthread))
        else
          ifeq (Darwin,$(OSTYPE))
            OS_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
            OS_LDFLAGS += -lpthread
            $(info using macOS libpthread: $(call find_include,pthread))
          endif
        endif
      endif
      LIBEXT = $(LIBEXTSAVE)
    endif
  endif
  # Find PCRE RegEx library.
  ifneq (,$(call find_include,pcre))
    ifneq (,$(call find_lib,pcre))
      OS_CCDEFS += -DHAVE_PCRE_H
      OS_LDFLAGS += -lpcre
      $(info using libpcre: $(call find_lib,pcre) $(call find_include,pcre))
      ifeq ($(LD_SEARCH_NEEDED),$(call need_search,pcre))
        OS_LDFLAGS += -L$(dir $(call find_lib,pcre))
      endif
    endif
  endif
  # Find available ncurses library.
  ifneq (,$(call find_include,ncurses))
    ifneq (,$(call find_lib,ncurses))
      OS_CURSES_DEFS += -DHAVE_NCURSES -lncurses
    endif
  endif
  ifneq (,$(call find_include,semaphore))
    ifneq (, $(shell grep sem_timedwait $(call find_include,semaphore)))
      OS_CCDEFS += -DHAVE_SEMAPHORE
      $(info using semaphore: $(call find_include,semaphore))
    endif
  endif
  ifneq (,$(call find_include,sys/ioctl))
    OS_CCDEFS += -DHAVE_SYS_IOCTL
  endif
  ifneq (,$(call find_include,linux/cdrom))
    OS_CCDEFS += -DHAVE_LINUX_CDROM
  endif
  ifneq (,$(call find_include,dlfcn))
    ifneq (,$(call find_lib,dl))
      OS_CCDEFS += -DHAVE_DLOPEN=${LIBEXT}
      OS_LDFLAGS += -ldl
      $(info using libdl: $(call find_lib,dl) $(call find_include,dlfcn))
    else
      ifneq (,$(findstring BSD,$(OSTYPE))$(findstring AIX,$(OSTYPE))$(findstring Haiku,$(OSTYPE)))
        OS_CCDEFS += -DHAVE_DLOPEN=so
        $(info using libdl: $(call find_include,dlfcn))
      else
        ifneq (,$(call find_lib,dld))
          OS_CCDEFS += -DHAVE_DLOPEN=${LIBEXT}
          OS_LDFLAGS += -ldld
          $(info using libdld: $(call find_lib,dld) $(call find_include,dlfcn))
        else
          ifeq (Darwin,$(OSTYPE))
            OS_CCDEFS += -DHAVE_DLOPEN=dylib
            $(info using macOS dlopen with .dylib)
          endif
        endif
      endif
    endif
  endif
  ifneq (,$(call find_include,utime))
    OS_CCDEFS += -DHAVE_UTIME
  endif
  ifneq (,$(call find_include,png))
    ifneq (,$(call find_lib,png))
      OS_CCDEFS += -DHAVE_LIBPNG
      OS_LDFLAGS += -lpng
      $(info using libpng: $(call find_lib,png) $(call find_include,png))
      ifneq (,$(call find_include,zlib))
        ifneq (,$(call find_lib,z))
          OS_CCDEFS += -DHAVE_ZLIB
          OS_LDFLAGS += -lz
          $(info using zlib: $(call find_lib,z) $(call find_include,zlib))
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
  ifneq (,$(call find_include,sys/mman))
    ifneq (,$(shell grep shm_open $(call find_include,sys/mman)))
      # some Linux installs have been known to have the include, but are
      # missing librt (where the shm_ APIs are implemented on Linux)
      # other OSes seem have these APIs implemented elsewhere
      ifneq (,$(if $(findstring Linux,$(OSTYPE)),$(call find_lib,rt),OK))
        OS_CCDEFS += -DHAVE_SHM_OPEN
        $(info using mman: $(call find_include,sys/mman))
      endif
    endif
  endif
  ifneq (,$(VIDEO_USEFUL))
    ifeq (cygwin,$(OSTYPE))
      LIBEXTSAVE := ${LIBEXT}
      LIBEXT = dll.a
    endif
    ifneq (,$(call find_include,SDL2/SDL))
      ifneq (,$(call find_lib,SDL2))
        ifneq (,$(findstring Haiku,$(OSTYPE)))
          ifneq (,$(shell which sdl2-config))
            SDLX_CONFIG = sdl2-config
          endif
        else
          SDLX_CONFIG = $(realpath $(dir $(call find_include,SDL2/SDL))../../bin/sdl2-config)
        endif
        ifneq (,$(SDLX_CONFIG))
          VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO `$(SDLX_CONFIG) --cflags`
          VIDEO_LDFLAGS += `$(SDLX_CONFIG) --libs`
          VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
          DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
          DISPLAYVT = ${DISPLAYD}/vt11.c
          DISPLAY340 = ${DISPLAYD}/type340.c
          DISPLAYNG = ${DISPLAYD}/ng.c
          DISPLAYIII = ${DISPLAYD}/iii.c
          DISPLAYIMLAC = ${DISPLAYD}/imlac.c
          DISPLAYTT2500 = ${DISPLAYD}/tt2500.c
          DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
          $(info using libSDL2: $(call find_include,SDL2/SDL))
          ifeq (Darwin,$(OSTYPE))
            VIDEO_CCDEFS += -DSDL_MAIN_AVAILABLE
          endif
        endif
      endif
    endif
    ifeq (cygwin,$(OSTYPE))
      LIBEXT = $(LIBEXTSAVE)
    endif
    ifeq (,$(findstring HAVE_LIBSDL,$(VIDEO_CCDEFS)))
      $(info *** Info ***)
      $(info *** Info *** The simulator$(BUILD_MULTIPLE) you are building could provide more functionality)
      $(info *** Info *** if video support was available on your system.)
      $(info *** Info *** To gain this functionality:)
      ifeq (Darwin,$(OSTYPE))
        ifeq (/opt/local/bin/port,$(shell which port))
          $(info *** Info *** Install the MacPorts libSDL2 package to provide this)
          $(info *** Info *** functionality for your OS X system:)
          $(info *** Info ***       # port install libsdl2 libpng zlib)
        endif
        ifeq (/usr/local/bin/brew,$(shell which brew))
          ifeq (/opt/local/bin/port,$(shell which port))
            $(info *** Info ***)
            $(info *** Info *** OR)
            $(info *** Info ***)
          endif
          $(info *** Info *** Install the HomeBrew libSDL2 package to provide this)
          $(info *** Info *** functionality for your OS X system:)
          $(info *** Info ***       $$ brew install sdl2 libpng zlib)
        else
          ifeq (,$(shell which port))
            $(info *** Info *** Install MacPorts or HomeBrew and rerun this make for)
            $(info *** Info *** specific advice)
          endif
        endif
      else
        ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
          $(info *** Info *** Install the development components of libSDL2 packaged for)
          $(info *** Info *** your operating system distribution for your Linux)
          $(info *** Info *** system:)
          $(info *** Info ***        $$ sudo apt-get install libsdl2-dev libpng-dev)
        else
          $(info *** Info *** Install the development components of libSDL2 packaged by your)
          $(info *** Info *** operating system distribution and rebuild your simulator to)
          $(info *** Info *** enable this extra functionality.)
        endif
      endif
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
        LIBEXTSAVE := ${LIBEXT}
        LIBEXT = a
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS += -DUSE_NETWORK
          NETWORK_LDFLAGS := -L$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
        endif
        LIBEXT = $(LIBEXTSAVE)
        ifeq (Darwin,$(OSTYPE)$(findstring USE_,$(NETWORK_CCDEFS)))
          NETWORK_CCDEFS += -DUSE_SHARED
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
          $(info using macOS dynamic libpcap: $(call find_include,pcap))
        endif
      endif
    else
      # On non-Linux platforms, we'll still try to provide deprecated support for libpcap in /usr/local
      INCPATHSAVE := ${INCPATH}
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
        LIBEXTSAVE := ${LIBEXT}
        # first check if binary - shared objects are available/installed in the linker known search paths
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
          NETWORK_FEATURES = - dynamic networking support using libpcap components from www.tcpdump.org and locally installed libpcap.${LIBEXT}
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
            $(error using libpcap: $(call find_include,pcap) missing $(PCAPLIB).${LIBEXT})
          endif
          NETWORK_LAN_FEATURES += PCAP
        endif
        LIBEXT = $(LIBEXTSAVE)
      else
        INCPATH = $(INCPATHSAVE)
        $(info *** Warning ***)
        $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built WITHOUT)
        $(info *** Warning *** libpcap networking support)
        $(info *** Warning ***)
        $(info *** Warning *** To build simulator(s) with libpcap networking support you)
        ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
          $(info *** Warning *** should install the libpcap development components for)
          $(info *** Warning *** for your Linux system:)
          $(info *** Warning ***        $$ sudo apt-get install libpcap-dev)
        else
          $(info *** Warning *** should read 0readme_ethernet.txt and follow the instructions)
          $(info *** Warning *** regarding the needed libpcap development components for your)
          $(info *** Warning *** $(OSTYPE) platform)
        endif
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
      ifneq (,$(findstring Linux,$(OSTYPE))$(findstring Darwin,$(OSTYPE)))
        ifneq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
          $(info *** Info ***)
          $(info *** Info *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built with)
          $(info *** Info *** minimal libpcap networking support)
          $(info *** Info ***)
        endif
        $(info *** Info ***)
        $(info *** Info *** Simulators on your $(OSNAME) platform can also be built with)
        $(info *** Info *** extended LAN Ethernet networking support by using VDE Ethernet.)
        $(info *** Info ***)
        $(info *** Info *** To build simulator(s) with extended networking support you)
        ifeq (Darwin,$(OSTYPE))
          ifeq (/opt/local/bin/port,$(shell which port))
            $(info *** Info *** should install the MacPorts vde2 package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       # port install vde2)
          endif
          ifeq (/usr/local/bin/brew,$(shell which brew))
            ifeq (/opt/local/bin/port,$(shell which port))
              $(info *** Info ***)
              $(info *** Info *** OR)
              $(info *** Info ***)
            endif
            $(info *** Info *** should install the HomeBrew vde package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       $$ brew install vde)
          else
            ifeq (,$(shell which port))
              $(info *** Info *** should install MacPorts or HomeBrew and rerun this make for)
              $(info *** Info *** specific advice)
            endif
          endif
        else
          ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
            $(info *** Info *** should install the vde2 package to provide this)
            $(info *** Info *** functionality for your $(OSNAME) system:)
            ifneq (,$(shell apt list 2>/dev/null| grep libvdeplug-dev))
              $(info *** Info ***        $$ sudo apt-get install libvdeplug-dev)
            else
              $(info *** Info ***        $$ sudo apt-get install vde2)
            endif
          else
            $(info *** Info *** should read 0readme_ethernet.txt and follow the instructions)
            $(info *** Info *** regarding the needed libvdeplug components for your $(OSNAME))
            $(info *** Info *** platform)
          endif
        endif
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
    ifeq (bsdtuntap,$(shell if ${TEST} -e /usr/include/net/if_tun.h -o -e /Library/Extensions/tap.kext -o -e /Applications/Tunnelblick.app/Contents/Resources/tap-notarized.kext; then echo bsdtuntap; fi))
      # Provide support for Tap networking on BSD platforms (including OS X)
      NETWORK_CCDEFS += -DHAVE_TAP_NETWORK -DHAVE_BSDTUNTAP
      NETWORK_LAN_FEATURES += TAP
      ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
        NETWORK_CCDEFS += -DUSE_NETWORK
      endif
    endif
    ifeq (slirp,$(shell if ${TEST} -e slirp_glue/sim_slirp.c; then echo slirp; fi))
      NETWORK_CCDEFS += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
    ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS))$(findstring HAVE_VDE_NETWORK,$(NETWORK_CCDEFS)))
      NETWORK_CCDEFS += -DUSE_NETWORK
      NETWORK_FEATURES = - WITHOUT Local LAN networking support
      $(info *** Warning ***)
      $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built WITHOUT LAN networking support)
      $(info *** Warning ***)
      $(info *** Warning *** To build simulator(s) with networking support you should read)
      $(info *** Warning *** 0readme_ethernet.txt and follow the instructions regarding the)
      $(info *** Warning *** needed libpcap components for your $(OSTYPE) platform)
      $(info *** Warning ***)
    endif
    NETWORK_OPT = $(NETWORK_CCDEFS)
  endif
  ifneq (binexists,$(shell if ${TEST} -e BIN/buildtools; then echo binexists; fi))
    MKDIRBIN = @mkdir -p BIN/buildtools
  endif
  ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
    GIT_COMMIT_ID=$(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }')
    GIT_COMMIT_TIME=$(shell grep 'SIM_GIT_COMMIT_TIME' .git-commit-id | awk '{ print $$2 }')
  else
    ifeq (,$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | grep 'Format:'))
      GIT_COMMIT_ID=$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | awk '{ print $$3 }')
      GIT_COMMIT_TIME=$(shell grep 'define SIM_GIT_COMMIT_TIME' sim_rev.h | awk '{ print $$3 }')
    else
      ifeq (git-submodule,$(if $(shell cd .. ; git rev-parse --git-dir 2>/dev/null),git-submodule))
        GIT_COMMIT_ID=$(shell cd .. ; git submodule status | grep "$(notdir $(realpath .))" | awk '{ print $$1 }')
        GIT_COMMIT_TIME=$(shell git --git-dir=$(realpath .)/.git log $(GIT_COMMIT_ID) -1 --pretty="%aI")
      else
        $(info *** Error ***)
        $(info *** Error *** The simh git commit id can not be determined.)
        $(info *** Error ***)
        $(info *** Error *** There are ONLY two supported ways to acquire and build)
        $(info *** Error *** the simh source code:)
        $(info *** Error ***   1: directly with git via:)
        $(info *** Error ***      $$ git clone https://github.com/simh/simh)
        $(info *** Error ***      $$ cd simh)
        $(info *** Error ***      $$ make {simulator-name})
        $(info *** Error *** OR)
        $(info *** Error ***   2: download the source code zip archive from:)
        $(info *** Error ***      $$ wget(or via browser) https://github.com/simh/simh/archive/master.zip)
        $(info *** Error ***      $$ unzip master.zip)
        $(info *** Error ***      $$ cd simh-master)
        $(info *** Error ***      $$ make {simulator-name})
        $(info *** Error ***)
        $(error get simh source either with zip download or git clone)
      endif
    endif
  endif
else
  #Win32 Environments (via MinGW32)
  GCC := gcc
  GCC_Path := $(abspath $(dir $(word 1,$(wildcard $(addsuffix /${GCC}.exe,$(subst ;, ,${PATH}))))))
  ifeq (rename-build-support,$(shell if exist ..\windows-build-windows-build echo rename-build-support))
    REMOVE_OLD_BUILD := $(shell if exist ..\windows-build rmdir/s/q ..\windows-build)
    FIXED_BUILD := $(shell move ..\windows-build-windows-build ..\windows-build >NUL)
  endif
  GCC_VERSION = $(word 3,$(shell ${GCC} --version))
  COMPILER_NAME = GCC Version: $(GCC_VERSION)
  ifeq (,$(findstring ++,${GCC}))
    CC_STD = -std=gnu99
  else
    CPP_BUILD = 1
  endif
  LTO_EXCLUDE_VERSIONS = 4.5.2
  ifeq (,$(PATH_SEPARATOR))
    PATH_SEPARATOR := ;
  endif
  INCPATH = $(abspath $(wildcard $(GCC_Path)\..\include $(subst $(PATH_SEPARATOR), ,$(CPATH))  $(subst $(PATH_SEPARATOR), ,$(C_INCLUDE_PATH))))
  LIBPATH = $(abspath $(wildcard $(GCC_Path)\..\lib $(subst :, ,$(LIBRARY_PATH))))
  $(info lib paths are: ${LIBPATH})
  $(info include paths are: ${INCPATH})
  # Give preference to any MinGW provided threading (if available)
  ifneq (,$(call find_include,pthread))
    PTHREADS_CCDEFS = -DUSE_READER_THREAD -DSIM_ASYNCH_IO
    PTHREADS_LDFLAGS = -lpthread
  else
    ifeq (pthreads,$(shell if exist ..\windows-build\pthreads\Pre-built.2\include\pthread.h echo pthreads))
      PTHREADS_CCDEFS = -DUSE_READER_THREAD -DPTW32_STATIC_LIB -D_POSIX_C_SOURCE -I../windows-build/pthreads/Pre-built.2/include -DSIM_ASYNCH_IO
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
      VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO -I$(abspath $(dir $(SDL_INCLUDE)))
      ifneq ($(DEBUG),)
        VIDEO_LDFLAGS  += $(abspath $(dir $(SDL_INCLUDE))\..\..\..\lib\lib-VC2008\Debug)/SDL2.lib
      else
        VIDEO_LDFLAGS  += $(abspath $(dir $(SDL_INCLUDE))\..\..\..\lib\lib-VC2008\Release)/SDL2.lib
      endif
      VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
      DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
      DISPLAYVT = ${DISPLAYD}/vt11.c
      DISPLAY340 = ${DISPLAYD}/type340.c
      DISPLAYNG = ${DISPLAYD}/ng.c
      DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
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
  ifneq (clean,${MAKECMDGOALS})
    ifneq (buildtoolsexists,$(shell if exist BIN\buildtools (echo buildtoolsexists) else (mkdir BIN\buildtools)))
      MKDIRBIN=
    endif
  endif
  ifneq ($(USE_NETWORK),)
    NETWORK_OPT += -DUSE_SHARED
  endif
  ifeq (git-repo,$(shell if exist .git echo git-repo))
    GIT_PATH := $(shell where git)
    ifeq (,$(GIT_PATH))
      $(error building using a git repository, but git is not available)
    endif
    ifeq (commit-id-exists,$(shell if exist .git-commit-id echo commit-id-exists))
      CURRENT_GIT_COMMIT_ID=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_ID" .git-commit-id)") do echo %%i)
      ifneq (, $(shell git update-index --refresh --))
        ACTUAL_GIT_COMMIT_EXTRAS=+uncommitted-changes
      endif
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty=%H))$(ACTUAL_GIT_COMMIT_EXTRAS)
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        NEED_COMMIT_ID = need-commit-id
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell if exist .git\hooks\post-checkout del .git\hooks\post-checkout)
        $(shell if exist .git\hooks\post-commit   del .git\hooks\post-commit)
        $(shell if exist .git\hooks\post-merge    del .git\hooks\post-merge)
      endif
    else
      NEED_COMMIT_ID = need-commit-id
    endif
    ifeq (need-commit-id,$(NEED_COMMIT_ID))
      ifneq (, $(shell git update-index --refresh --))
        ACTUAL_GIT_COMMIT_EXTRAS=+uncommitted-changes
      endif
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty=%H))$(ACTUAL_GIT_COMMIT_EXTRAS)
      isodate=$(shell git log -1 --pretty=%ai)
      commit_time=$(word 1,$(isodate))T$(word 2,$(isodate))$(word 3,$(isodate))
      $(shell echo SIM_GIT_COMMIT_ID $(ACTUAL_GIT_COMMIT_ID)>.git-commit-id)
      $(shell echo SIM_GIT_COMMIT_TIME $(commit_time)>>.git-commit-id)
    endif
  endif
  ifneq (,$(shell if exist .git-commit-id echo git-commit-id))
    GIT_COMMIT_ID=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_ID" .git-commit-id)") do echo %%i)
    GIT_COMMIT_TIME=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_TIME" .git-commit-id)") do echo %%i)
  else
    ifeq (,$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h | findstr Format))
      GIT_COMMIT_ID=$(shell for /F "tokens=3" %%i in ("$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h)") do echo %%i)
      GIT_COMMIT_TIME=$(shell for /F "tokens=3" %%i in ("$(shell findstr /C:"define SIM_GIT_COMMIT_TIME" sim_rev.h)") do echo %%i)
    endif
  endif
  ifneq (windows-build,$(shell if exist ..\windows-build\README.md echo windows-build))
    ifneq (,$(GIT_PATH))
      $(info Cloning the windows-build dependencies into $(abspath ..)/windows-build)
      $(shell git clone https://github.com/simh/windows-build ../windows-build)
    else
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
    endif
  else
    # Version check on windows-build
    WINDOWS_BUILD = $(word 2,$(shell findstr WINDOWS-BUILD ..\windows-build\Windows-Build_Versions.txt))
    ifeq (,$(WINDOWS_BUILD))
      WINDOWS_BUILD = 00000000
    endif
    ifneq (,$(or $(shell if 20190124 GTR $(WINDOWS_BUILD) echo old-windows-build),$(and $(shell if 20171112 GTR $(WINDOWS_BUILD) echo old-windows-build),$(findstring pthreadGC2,$(PTHREADS_LDFLAGS)))))
      $(info .)
      $(info windows-build components at: $(abspath ..\windows-build))
      $(info .)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This currently available windows-build components are out of     **)
      ifneq (,$(GIT_PATH))
        $(info **  date.  You need to update to the latest windows-build            **)
        $(info **  dependencies by executing these commands:                        **)
        $(info **                                                                   **)
        $(info **    > cd ..\windows-build                                          **)
        $(info **    > git pull                                                     **)
        $(info **                                                                   **)
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(error .)
      else
        $(info **  date.  For the most functional and stable features you shoud     **)
        $(info **  Download the file:                                               **)
        $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
        $(info **  Extract the windows-build-windows-build folder it contains to    **)
        $(info **  $(abspath ..\)                                                   **)
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(info .)
        $(error Update windows-build)
      endif
    endif
    ifeq (pcre,$(shell if exist ..\windows-build\PCRE\include\pcre.h echo pcre))
      OS_CCDEFS += -DHAVE_PCRE_H -DPCRE_STATIC -I$(abspath ../windows-build/PCRE/include)
      OS_LDFLAGS += -lpcre -L../windows-build/PCRE/lib/
      $(info using libpcre: $(abspath ../windows-build/PCRE/lib/pcre.a) $(abspath ../windows-build/PCRE/include/pcre.h))
    endif
    ifeq (slirp,slirp)
      NETWORK_OPT += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c -lIphlpapi
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
  endif
  ifneq (,$(call find_include,ddk/ntdddisk))
    CFLAGS_I = -DHAVE_NTDDDISK_H
  endif
endif # Win32 (via MinGW)
ifneq (,$(GIT_COMMIT_ID))
  CFLAGS_GIT = -DSIM_GIT_COMMIT_ID=$(GIT_COMMIT_ID)
endif
ifneq (,$(GIT_COMMIT_TIME))
  CFLAGS_GIT += -DSIM_GIT_COMMIT_TIME=$(GIT_COMMIT_TIME)
endif
ifneq (,$(UNSUPPORTED_BUILD))
  CFLAGS_GIT += -DSIM_BUILD=Unsupported=$(UNSUPPORTED_BUILD)
endif
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
else
  ifneq (,$(findstring clang,$(COMPILER_NAME))$(findstring LLVM,$(COMPILER_NAME)))
    CFLAGS_O = -O2 -fno-strict-overflow
    GCC_OPTIMIZERS_CMD = ${GCC} --help
    NO_LTO = 1
  else
    NO_LTO = 1
    ifeq (Darwin,$(OSTYPE))
      CFLAGS_O += -O4 -flto -fwhole-program
    else
      CFLAGS_O := -O2
    endif
  endif
  LDFLAGS_O = 
  GCC_MAJOR_VERSION = $(firstword $(subst  ., ,$(GCC_VERSION)))
  ifneq (3,$(GCC_MAJOR_VERSION))
    ifeq (,$(GCC_OPTIMIZERS_CMD))
      GCC_OPTIMIZERS_CMD = ${GCC} --help=optimizers
      GCC_COMMON_CMD = ${GCC} --help=common
    endif
  endif
  ifneq (,$(GCC_OPTIMIZERS_CMD))
    GCC_OPTIMIZERS = $(shell $(GCC_OPTIMIZERS_CMD))
  endif
  ifneq (,$(GCC_COMMON_CMD))
    GCC_OPTIMIZERS += $(shell $(GCC_COMMON_CMD))
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
    GCC_WARNINGS_CMD = ${GCC} --help=warnings
  endif
endif
ifneq (clean,${MAKECMDGOALS})
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
  ifneq (,$(TESTING_FEATURES))
    $(info *** $(TESTING_FEATURES).)
  endif
  ifneq (,$(GIT_COMMIT_ID))
    $(info ***)
    $(info *** git commit id is $(GIT_COMMIT_ID).)
    $(info *** git commit time is $(GIT_COMMIT_TIME).)
  endif
  $(info ***)
endif
ifneq ($(DONT_USE_ROMS),)
  ROMS_OPT = -DDONT_USE_INTERNAL_ROM
else
  BUILD_ROMS = ${BIN}buildtools/BuildROMs${EXE}
endif
ifneq ($(DONT_USE_READER_THREAD),)
  NETWORK_OPT += -DDONT_USE_READER_THREAD
endif

CC_OUTSPEC = -o $@
CC := ${GCC} ${CC_STD} -U__STRICT_ANSI__ ${CFLAGS_G} ${CFLAGS_O} ${CFLAGS_GIT} ${CFLAGS_I} -DSIM_COMPILER="${COMPILER_NAME}" -DSIM_BUILD_TOOL=simh-makefile -I . ${OS_CCDEFS} ${ROMS_OPT}
LDFLAGS := ${OS_LDFLAGS} ${NETWORK_LDFLAGS} ${LDFLAGS_O}

#
# Common Libraries
#
BIN = BIN/
SIMHD = .
SIM = ${SIMHD}/scp.c ${SIMHD}/sim_console.c ${SIMHD}/sim_fio.c \
	${SIMHD}/sim_timer.c ${SIMHD}/sim_sock.c ${SIMHD}/sim_tmxr.c \
	${SIMHD}/sim_ether.c ${SIMHD}/sim_tape.c ${SIMHD}/sim_disk.c \
	${SIMHD}/sim_serial.c ${SIMHD}/sim_video.c ${SIMHD}/sim_imd.c \
	${SIMHD}/sim_card.c

DISPLAYD = ${SIMHD}/display

SCSI = ${SIMHD}/sim_scsi.c

#
# Emulator source files and compile time options
#
PDP1D = ${SIMHD}/PDP1
PDP1_DISPLAY_OPT = -DDISPLAY_TYPE=DIS_TYPE30 -DPIX_SCALE=RES_HALF
PDP1 = ${PDP1D}/pdp1_lp.c ${PDP1D}/pdp1_cpu.c ${PDP1D}/pdp1_stddev.c \
	${PDP1D}/pdp1_sys.c ${PDP1D}/pdp1_dt.c ${PDP1D}/pdp1_drm.c \
	${PDP1D}/pdp1_clk.c ${PDP1D}/pdp1_dcs.c ${PDP1D}/pdp1_dpy.c ${DISPLAYL}
PDP1_OPT = -I ${PDP1D} ${DISPLAY_OPT} $(PDP1_DISPLAY_OPT)


NOVAD = ${SIMHD}/NOVA
NOVA = ${NOVAD}/nova_sys.c ${NOVAD}/nova_cpu.c ${NOVAD}/nova_dkp.c \
	${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c ${NOVAD}/nova_mta.c \
	${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c ${NOVAD}/nova_clk.c \
	${NOVAD}/nova_tt.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
NOVA_OPT = -I ${NOVAD}


ECLIPSE = ${NOVAD}/eclipse_cpu.c ${NOVAD}/eclipse_tt.c ${NOVAD}/nova_sys.c \
	${NOVAD}/nova_dkp.c ${NOVAD}/nova_dsk.c ${NOVAD}/nova_lp.c \
	${NOVAD}/nova_mta.c ${NOVAD}/nova_plt.c ${NOVAD}/nova_pt.c \
	${NOVAD}/nova_clk.c ${NOVAD}/nova_tt1.c ${NOVAD}/nova_qty.c
ECLIPSE_OPT = -I ${NOVAD} -DECLIPSE -DUSE_INT64


PDP18BD = ${SIMHD}/PDP18B
PDP18B = ${PDP18BD}/pdp18b_dt.c ${PDP18BD}/pdp18b_drm.c ${PDP18BD}/pdp18b_cpu.c \
	${PDP18BD}/pdp18b_lp.c ${PDP18BD}/pdp18b_mt.c ${PDP18BD}/pdp18b_rf.c \
	${PDP18BD}/pdp18b_rp.c ${PDP18BD}/pdp18b_stddev.c ${PDP18BD}/pdp18b_sys.c \
	${PDP18BD}/pdp18b_rb.c ${PDP18BD}/pdp18b_tt1.c ${PDP18BD}/pdp18b_fpp.c \
	${PDP18BD}/pdp18b_g2tty.c ${PDP18BD}/pdp18b_dr15.c

ifneq (,${DISPLAY_OPT})
  PDP7_DISPLAY_OPT = -DDISPLAY_TYPE=DIS_TYPE30 -DPIX_SCALE=RES_HALF
endif

PDP4_OPT = -DPDP4 -I ${PDP18BD}
PDP7_OPT = -DPDP7 -I ${PDP18BD} ${DISPLAY_OPT} $(PDP7_DISPLAY_OPT)
PDP9_OPT = -DPDP9 -I ${PDP18BD}
PDP15_OPT = -DPDP15 -I ${PDP18BD}


PDP11D = ${SIMHD}/PDP11
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
	${PDP11D}/pdp11_vt.c ${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c \
	${PDP11D}/pdp11_rom.c ${PDP11D}/pdp11_ch.c ${DISPLAYL} ${DISPLAYVT} \
	${PDP11D}/pdp11_ng.c ${PDP11D}/pdp11_daz.c ${DISPLAYNG}
PDP11_OPT = -DVM_PDP11 -I ${PDP11D} ${NETWORK_OPT} ${DISPLAY_OPT}


UC15D = ${SIMHD}/PDP11
UC15 = ${UC15D}/pdp11_cis.c ${UC15D}/pdp11_cpu.c \
	${UC15D}/pdp11_cpumod.c ${UC15D}/pdp11_cr.c \
	${UC15D}/pdp11_fp.c ${UC15D}/pdp11_io.c \
	${UC15D}/pdp11_io_lib.c ${UC15D}/pdp11_lp.c \
	${UC15D}/pdp11_rh.c ${UC15D}/pdp11_rk.c \
	${UC15D}/pdp11_stddev.c ${UC15D}/pdp11_sys.c \
	${UC15D}/pdp11_uc15.c
UC15_OPT = -DVM_PDP11 -DUC15 -I ${UC15D} -I ${PDP18BD}


VAXD = ${SIMHD}/VAX
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


VAX410 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax410_sysdev.c ${VAXD}/vax410_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax4xx_rd.c ${VAXD}/vax4xx_rz80.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_va.c ${VAXD}/vax4xx_vc.c ${VAXD}/vax_lk.c \
	${VAXD}/vax_vs.c ${VAXD}/vax_gpx.c
VAX410_OPT = -DVM_VAX -DVAX_410 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}


VAX420 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax420_sysdev.c ${VAXD}/vax420_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax4xx_rd.c ${VAXD}/vax4xx_rz80.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_va.c ${VAXD}/vax4xx_vc.c ${VAXD}/vax4xx_ve.c \
	${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_gpx.c
VAX420_OPT = -DVM_VAX -DVAX_420 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}
VAX411_OPT = ${VAX420_OPT} -DVAX_411
VAX412_OPT = ${VAX420_OPT} -DVAX_412
VAX41A_OPT = ${VAX420_OPT} -DVAX_41A
VAX41D_OPT = ${VAX420_OPT} -DVAX_41D
VAX42A_OPT = ${VAX420_OPT} -DVAX_42A
VAX42B_OPT = ${VAX420_OPT} -DVAX_42B


VAX43 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax43_sysdev.c ${VAXD}/vax43_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax4xx_rz80.c ${VAXD}/vax_xs.c ${VAXD}/vax4xx_vc.c \
	${VAXD}/vax4xx_ve.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c
VAX43_OPT = -DVM_VAX -DVAX_43 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} ${NETWORK_OPT} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS}


VAX440 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax440_sysdev.c ${VAXD}/vax440_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax_xs.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax4xx_rz94.c
VAX440_OPT = -DVM_VAX -DVAX_440 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} ${NETWORK_OPT}
VAX46_OPT = ${VAX440_OPT} -DVAX_46
VAX47_OPT = ${VAX440_OPT} -DVAX_47
VAX48_OPT = ${VAX440_OPT} -DVAX_48


IS1000 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_rz94.c ${VAXD}/vax4nn_stddev.c \
	${VAXD}/is1000_sysdev.c ${VAXD}/is1000_syslist.c
IS1000_OPT = -DVM_VAX -DIS_1000 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} ${NETWORK_OPT}


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
	${VAXD}/vax630_io.c ${VAXD}/vax630_syslist.c ${VAXD}/vax_va.c \
	${VAXD}/vax_vc.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c \
	${VAXD}/vax_2681.c ${VAXD}/vax_gpx.c \
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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c
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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c
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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c
VAX780_OPT = -DVM_VAX -DVAX_780 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


VAX8200 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax820_stddev.c ${VAXD}/vax820_bi.c \
	${VAXD}/vax820_mem.c ${VAXD}/vax820_uba.c ${VAXD}/vax820_ka.c \
	${VAXD}/vax820_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c
VAX8200_OPT = -DVM_VAX -DVAX_820 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c
VAX8600_OPT = -DVM_VAX -DVAX_860 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


PDP10D = ${SIMHD}/PDP10
PDP10 = ${PDP10D}/pdp10_fe.c ${PDP11D}/pdp11_dz.c ${PDP10D}/pdp10_cpu.c \
	${PDP10D}/pdp10_ksio.c ${PDP10D}/pdp10_lp20.c ${PDP10D}/pdp10_mdfp.c \
	${PDP10D}/pdp10_pag.c ${PDP10D}/pdp10_rp.c ${PDP10D}/pdp10_sys.c \
	${PDP10D}/pdp10_tim.c ${PDP10D}/pdp10_tu.c ${PDP10D}/pdp10_xtnd.c \
	${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_kmc.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ch.c
PDP10_OPT = -DVM_PDP10 -DUSE_INT64 -I ${PDP10D} -I ${PDP11D} ${NETWORK_OPT}


IMLACD = ${SIMHD}/imlac
IMLAC = ${IMLACD}/imlac_sys.c ${IMLACD}/imlac_cpu.c \
	${IMLACD}/imlac_dp.c ${IMLACD}/imlac_crt.c ${IMLACD}/imlac_kbd.c \
	${IMLACD}/imlac_tty.c ${IMLACD}/imlac_pt.c ${IMLACD}/imlac_bel.c \
	${DISPLAYL} ${DISPLAYIMLAC}
IMLAC_OPT = -I ${IMLACD} ${DISPLAY_OPT}


TT2500D = ${SIMHD}/tt2500
TT2500 = ${TT2500D}/tt2500_sys.c ${TT2500D}/tt2500_cpu.c \
	${TT2500D}/tt2500_dpy.c ${TT2500D}/tt2500_crt.c ${TT2500D}/tt2500_tv.c \
	${TT2500D}/tt2500_key.c ${TT2500D}/tt2500_uart.c ${TT2500D}/tt2500_rom.c \
	${DISPLAYL} ${DISPLAYTT2500}
TT2500_OPT = -I ${TT2500D} ${DISPLAY_OPT}


PDP8D = ${SIMHD}/PDP8
PDP8 = ${PDP8D}/pdp8_cpu.c ${PDP8D}/pdp8_clk.c ${PDP8D}/pdp8_df.c \
	${PDP8D}/pdp8_dt.c ${PDP8D}/pdp8_lp.c ${PDP8D}/pdp8_mt.c \
	${PDP8D}/pdp8_pt.c ${PDP8D}/pdp8_rf.c ${PDP8D}/pdp8_rk.c \
	${PDP8D}/pdp8_rx.c ${PDP8D}/pdp8_sys.c ${PDP8D}/pdp8_tt.c \
	${PDP8D}/pdp8_ttx.c ${PDP8D}/pdp8_rl.c ${PDP8D}/pdp8_tsc.c \
	${PDP8D}/pdp8_td.c ${PDP8D}/pdp8_ct.c ${PDP8D}/pdp8_fpp.c
PDP8_OPT = -I ${PDP8D}


H316D = ${SIMHD}/H316
H316 = ${H316D}/h316_stddev.c ${H316D}/h316_lp.c ${H316D}/h316_cpu.c \
	${H316D}/h316_sys.c ${H316D}/h316_mt.c ${H316D}/h316_fhd.c \
	${H316D}/h316_dp.c ${H316D}/h316_rtc.c ${H316D}/h316_imp.c \
	${H316D}/h316_hi.c ${H316D}/h316_mi.c ${H316D}/h316_udp.c 
H316_OPT = -I ${H316D} -D VM_IMPTIP


HP2100D = ${SIMHD}/HP2100
HP2100 = ${HP2100D}/hp2100_baci.c ${HP2100D}/hp2100_cpu.c \
        ${HP2100D}/hp2100_cpu_fp.c ${HP2100D}/hp2100_cpu_fpp.c \
        ${HP2100D}/hp2100_cpu0.c ${HP2100D}/hp2100_cpu1.c \
        ${HP2100D}/hp2100_cpu2.c ${HP2100D}/hp2100_cpu3.c \
        ${HP2100D}/hp2100_cpu4.c ${HP2100D}/hp2100_cpu5.c \
        ${HP2100D}/hp2100_cpu6.c ${HP2100D}/hp2100_cpu7.c \
        ${HP2100D}/hp2100_di.c ${HP2100D}/hp2100_di_da.c \
        ${HP2100D}/hp2100_disclib.c ${HP2100D}/hp2100_dma.c \
        ${HP2100D}/hp2100_dp.c ${HP2100D}/hp2100_dq.c \
        ${HP2100D}/hp2100_dr.c ${HP2100D}/hp2100_ds.c \
        ${HP2100D}/hp2100_ipl.c ${HP2100D}/hp2100_lps.c \
        ${HP2100D}/hp2100_lpt.c ${HP2100D}/hp2100_mc.c \
        ${HP2100D}/hp2100_mem.c ${HP2100D}/hp2100_mpx.c \
        ${HP2100D}/hp2100_ms.c ${HP2100D}/hp2100_mt.c \
        ${HP2100D}/hp2100_mux.c ${HP2100D}/hp2100_pif.c \
        ${HP2100D}/hp2100_pt.c ${HP2100D}/hp2100_sys.c \
        ${HP2100D}/hp2100_tbg.c ${HP2100D}/hp2100_tty.c
HP2100_OPT = -DHAVE_INT64 -I ${HP2100D}

HP3000D = ${SIMHD}/HP3000
HP3000 = ${HP3000D}/hp_disclib.c ${HP3000D}/hp_tapelib.c ${HP3000D}/hp3000_atc.c \
	${HP3000D}/hp3000_clk.c ${HP3000D}/hp3000_cpu.c ${HP3000D}/hp3000_cpu_base.c \
	${HP3000D}/hp3000_cpu_fp.c ${HP3000D}/hp3000_cpu_cis.c ${HP3000D}/hp3000_ds.c \
	${HP3000D}/hp3000_iop.c ${HP3000D}/hp3000_lp.c ${HP3000D}/hp3000_mem.c \
	${HP3000D}/hp3000_mpx.c ${HP3000D}/hp3000_ms.c ${HP3000D}/hp3000_scmb.c \
	${HP3000D}/hp3000_sel.c ${HP3000D}/hp3000_sys.c
HP3000_OPT = -I ${HP3000D}


I1401D = ${SIMHD}/I1401
I1401 = ${I1401D}/i1401_lp.c ${I1401D}/i1401_cpu.c ${I1401D}/i1401_iq.c \
	${I1401D}/i1401_cd.c ${I1401D}/i1401_mt.c ${I1401D}/i1401_dp.c \
	${I1401D}/i1401_sys.c
I1401_OPT = -I ${I1401D}


I1620D = ${SIMHD}/I1620
I1620 = ${I1620D}/i1620_cd.c ${I1620D}/i1620_dp.c ${I1620D}/i1620_pt.c \
	${I1620D}/i1620_tty.c ${I1620D}/i1620_cpu.c ${I1620D}/i1620_lp.c \
	${I1620D}/i1620_fp.c ${I1620D}/i1620_sys.c
I1620_OPT = -I ${I1620D}

I7000D = ${SIMHD}/I7000
I7090 = ${I7000D}/i7090_cpu.c ${I7000D}/i7090_sys.c ${I7000D}/i7090_chan.c \
	${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c \
	${I7000D}/i7090_hdrum.c ${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c \
	${I7000D}/i7000_com.c ${I7000D}/i7000_ht.c 
I7090_OPT = -I $(I7000D) -DUSE_INT64 -DI7090 -DUSE_SIM_CARD

I7080D = ${SIMHD}/I7000
I7080 = ${I7000D}/i7080_cpu.c ${I7000D}/i7080_sys.c ${I7000D}/i7080_chan.c \
	${I7000D}/i7080_drum.c ${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c \
	${I7000D}/i7000_con.c ${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c \
	${I7000D}/i7000_mt.c ${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c \
	${I7000D}/i7000_com.c ${I7000D}/i7000_ht.c 
I7080_OPT = -I $(I7000D) -DI7080 -DUSE_SIM_CARD

I7070D = ${SIMHD}/I7000
I7070 = ${I7000D}/i7070_cpu.c ${I7000D}/i7070_sys.c ${I7000D}/i7070_chan.c \
	${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c ${I7000D}/i7000_con.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c ${I7000D}/i7000_mt.c \
	${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c ${I7000D}/i7000_com.c \
	${I7000D}/i7000_ht.c 
I7070_OPT = -I $(I7000D) -DUSE_INT64 -DI7070 -DUSE_SIM_CARD

I7010D = ${SIMHD}/I7000
I7010 = ${I7000D}/i7010_cpu.c ${I7000D}/i7010_sys.c ${I7000D}/i7010_chan.c \
	${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c ${I7000D}/i7000_con.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c ${I7000D}/i7000_mt.c \
	${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c ${I7000D}/i7000_com.c \
	${I7000D}/i7000_ht.c 
I7010_OPT = -I $(I7010D) -DI7010 -DUSE_SIM_CARD

I704D  = ${SIMHD}/I7000
I704   = ${I7000D}/i7090_cpu.c ${I7000D}/i7090_sys.c ${I7000D}/i7090_chan.c \
	 ${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	 ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c ${I7000D}/i7000_chan.c 
I704_OPT = -I $(I7000D) -DUSE_INT64 -DI704 -DUSE_SIM_CARD


I701D  = ${SIMHD}/I7000
I701   = ${I7000D}/i701_cpu.c ${I7000D}/i701_sys.c ${I7000D}/i701_chan.c \
	 ${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	 ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c ${I7000D}/i7000_chan.c 
I701_OPT = -I $(I7000D) -DUSE_INT64 -DI701 -DUSE_SIM_CARD


I7094D = ${SIMHD}/I7094
I7094 = ${I7094D}/i7094_cpu.c ${I7094D}/i7094_cpu1.c ${I7094D}/i7094_io.c \
	${I7094D}/i7094_cd.c ${I7094D}/i7094_clk.c ${I7094D}/i7094_com.c \
	${I7094D}/i7094_drm.c ${I7094D}/i7094_dsk.c ${I7094D}/i7094_sys.c \
	${I7094D}/i7094_lp.c ${I7094D}/i7094_mt.c ${I7094D}/i7094_binloader.c
I7094_OPT = -DUSE_INT64 -I ${I7094D}


I650D = ${SIMHD}/I650
I650 = ${I650D}/i650_cpu.c ${I650D}/i650_cdr.c ${I650D}/i650_cdp.c \
	${I650D}/i650_dsk.c ${I650D}/i650_mt.c ${I650D}/i650_sys.c
I650_OPT = -I ${I650D} -DUSE_INT64 -DUSE_SIM_CARD


IBM1130D = ${SIMHD}/Ibm1130
IBM1130 = ${IBM1130D}/ibm1130_cpu.c ${IBM1130D}/ibm1130_cr.c \
	${IBM1130D}/ibm1130_disk.c ${IBM1130D}/ibm1130_stddev.c \
	${IBM1130D}/ibm1130_sys.c ${IBM1130D}/ibm1130_gdu.c \
	${IBM1130D}/ibm1130_gui.c ${IBM1130D}/ibm1130_prt.c \
	${IBM1130D}/ibm1130_fmt.c ${IBM1130D}/ibm1130_ptrp.c \
	${IBM1130D}/ibm1130_plot.c ${IBM1130D}/ibm1130_sca.c \
	${IBM1130D}/ibm1130_t2741.c
IBM1130_OPT = -I ${IBM1130D}
ifneq (${WIN32},)
IBM1130_OPT += -DGUI_SUPPORT -lgdi32 ${BIN}ibm1130.o 
endif  


ID16D = ${SIMHD}/Interdata
ID16 = ${ID16D}/id16_cpu.c ${ID16D}/id16_sys.c ${ID16D}/id_dp.c \
	${ID16D}/id_fd.c ${ID16D}/id_fp.c ${ID16D}/id_idc.c ${ID16D}/id_io.c \
	${ID16D}/id_lp.c ${ID16D}/id_mt.c ${ID16D}/id_pas.c ${ID16D}/id_pt.c \
	${ID16D}/id_tt.c ${ID16D}/id_uvc.c ${ID16D}/id16_dboot.c ${ID16D}/id_ttp.c
ID16_OPT = -I ${ID16D}


ID32D = ${SIMHD}/Interdata
ID32 = ${ID32D}/id32_cpu.c ${ID32D}/id32_sys.c ${ID32D}/id_dp.c \
	${ID32D}/id_fd.c ${ID32D}/id_fp.c ${ID32D}/id_idc.c ${ID32D}/id_io.c \
	${ID32D}/id_lp.c ${ID32D}/id_mt.c ${ID32D}/id_pas.c ${ID32D}/id_pt.c \
	${ID32D}/id_tt.c ${ID32D}/id_uvc.c ${ID32D}/id32_dboot.c ${ID32D}/id_ttp.c
ID32_OPT = -I ${ID32D}


S3D = ${SIMHD}/S3
S3 = ${S3D}/s3_cd.c ${S3D}/s3_cpu.c ${S3D}/s3_disk.c ${S3D}/s3_lp.c \
	${S3D}/s3_pkb.c ${S3D}/s3_sys.c
S3_OPT = -I ${S3D}


ALTAIRD = ${SIMHD}/ALTAIR
ALTAIR = ${ALTAIRD}/altair_sio.c ${ALTAIRD}/altair_cpu.c ${ALTAIRD}/altair_dsk.c \
	${ALTAIRD}/altair_sys.c
ALTAIR_OPT = -I ${ALTAIRD}


ALTAIRZ80D = ${SIMHD}/AltairZ80
ALTAIRZ80 = ${ALTAIRZ80D}/altairz80_cpu.c ${ALTAIRZ80D}/altairz80_cpu_nommu.c \
	${ALTAIRZ80D}/altairz80_dsk.c ${ALTAIRZ80D}/disasm.c \
	${ALTAIRZ80D}/altairz80_sio.c ${ALTAIRZ80D}/altairz80_sys.c \
	${ALTAIRZ80D}/altairz80_hdsk.c ${ALTAIRZ80D}/altairz80_net.c \
	${ALTAIRZ80D}/s100_hayes.c ${ALTAIRZ80D}/s100_2sio.c ${ALTAIRZ80D}/s100_pmmi.c\
	${ALTAIRZ80D}/flashwriter2.c ${ALTAIRZ80D}/i86_decode.c \
	${ALTAIRZ80D}/i86_ops.c ${ALTAIRZ80D}/i86_prim_ops.c \
	${ALTAIRZ80D}/i8272.c ${ALTAIRZ80D}/insnsd.c ${ALTAIRZ80D}/altairz80_mhdsk.c \
	${ALTAIRZ80D}/mfdc.c ${ALTAIRZ80D}/n8vem.c ${ALTAIRZ80D}/vfdhd.c \
	${ALTAIRZ80D}/s100_disk1a.c ${ALTAIRZ80D}/s100_disk2.c ${ALTAIRZ80D}/s100_disk3.c \
	${ALTAIRZ80D}/s100_fif.c ${ALTAIRZ80D}/s100_mdriveh.c \
	${ALTAIRZ80D}/s100_icom.c \
	${ALTAIRZ80D}/s100_jadedd.c \
	${ALTAIRZ80D}/s100_mdsa.c \
	${ALTAIRZ80D}/s100_mdsad.c ${ALTAIRZ80D}/s100_selchan.c \
	${ALTAIRZ80D}/s100_ss1.c ${ALTAIRZ80D}/s100_64fdc.c \
	${ALTAIRZ80D}/s100_scp300f.c \
	${ALTAIRZ80D}/s100_tarbell.c \
	${ALTAIRZ80D}/wd179x.c ${ALTAIRZ80D}/s100_hdc1001.c \
	${ALTAIRZ80D}/s100_if3.c ${ALTAIRZ80D}/s100_adcs6.c \
	${ALTAIRZ80D}/m68kcpu.c ${ALTAIRZ80D}/m68kdasm.c ${ALTAIRZ80D}/m68kasm.c \
	${ALTAIRZ80D}/m68kopac.c ${ALTAIRZ80D}/m68kopdm.c \
	${ALTAIRZ80D}/m68kopnz.c ${ALTAIRZ80D}/m68kops.c ${ALTAIRZ80D}/m68ksim.c
ALTAIRZ80_OPT = -I ${ALTAIRZ80D} -DUSE_SIM_IMD


GRID = ${SIMHD}/GRI
GRI = ${GRID}/gri_cpu.c ${GRID}/gri_stddev.c ${GRID}/gri_sys.c
GRI_OPT = -I ${GRID}


LGPD = ${SIMHD}/LGP
LGP = ${LGPD}/lgp_cpu.c ${LGPD}/lgp_stddev.c ${LGPD}/lgp_sys.c
LGP_OPT = -I ${LGPD}


SDSD = ${SIMHD}/SDS
SDS = ${SDSD}/sds_cpu.c ${SDSD}/sds_drm.c ${SDSD}/sds_dsk.c ${SDSD}/sds_io.c \
	${SDSD}/sds_lp.c ${SDSD}/sds_mt.c ${SDSD}/sds_mux.c ${SDSD}/sds_rad.c \
	${SDSD}/sds_stddev.c ${SDSD}/sds_sys.c ${SDSD}/sds_cp.c ${SDSD}/sds_cr.c
SDS_OPT = -I ${SDSD} -DUSE_SIM_CARD


SWTP6800D = ${SIMHD}/swtp6800/swtp6800
SWTP6800C = ${SIMHD}/swtp6800/common
SWTP6800MP-A = ${SWTP6800C}/mp-a.c ${SWTP6800C}/m6800.c ${SWTP6800C}/m6810.c \
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800C}/mp-s.c ${SWTP6800D}/mp-a_sys.c \
	${SWTP6800C}/mp-b2.c ${SWTP6800C}/mp-8m.c
SWTP6800MP-A2 = ${SWTP6800C}/mp-a2.c ${SWTP6800C}/m6800.c ${SWTP6800C}/m6810.c \
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800C}/mp-s.c ${SWTP6800D}/mp-a2_sys.c \
	${SWTP6800C}/mp-b2.c ${SWTP6800C}/mp-8m.c ${SWTP6800C}/i2716.c
SWTP6800_OPT = -I ${SWTP6800D}

INTELSYSD = ${SIMHD}/Intel-Systems
INTELSYSC = ${SIMHD}/Intel-Systems/common

INTEL_PARTS = \
	${INTELSYSC}/i3214.c \
	${INTELSYSC}/i8251.c \
	${INTELSYSC}/i8253.c \
	${INTELSYSC}/i8255.c \
	${INTELSYSC}/i8259.c \
	${INTELSYSC}/ieprom.c \
	${INTELSYSC}/ioc-cont.c \
	${INTELSYSC}/ipc-cont.c \
	${INTELSYSC}/iram8.c \
	${INTELSYSC}/io.c \
	${INTELSYSC}/isbc064.c \
	${INTELSYSC}/isbc202.c \
	${INTELSYSC}/isbc201.c \
	${INTELSYSC}/isbc206.c \
	${INTELSYSC}/isbc464.c \
	${INTELSYSC}/isbc208.c \
	${INTELSYSC}/multibus.c \
	${INTELSYSC}/zx200a.c 


ISDK80D = ${INTELSYSD}/isdk80
ISDK80 = ${INTELSYSC}/i8080.c ${ISDK80D}/isdk80_sys.c \
	${ISDK80D}/isdk80.c ${INTEL_PARTS}
ISDK80_OPT = -I ${ISDK80D}


IDS880D = ${INTELSYSD}/ids880
IDS880 = ${INTELSYSC}/i8080.c ${IDS880D}/ids880_sys.c \
	${IDS880D}/ids880.c ${INTEL_PARTS}
IDS880_OPT = -I ${IDS880D}


ISYS8010D = ${INTELSYSD}/isys8010
ISYS8010 = ${INTELSYSC}/i8080.c ${ISYS8010D}/isys8010_sys.c \
	${ISYS8010D}/isbc8010.c ${INTEL_PARTS}
ISYS8010_OPT = -I ${ISYS8010D}


ISYS8020D = ${INTELSYSD}/isys8020
ISYS8020 = ${INTELSYSC}/i8080.c ${ISYS8020D}/isys8020_sys.c \
	${ISYS8020D}/isbc8020.c ${INTEL_PARTS}
ISYS8020_OPT = -I ${ISYS8020D}


ISYS8024D = ${INTELSYSD}/isys8024
ISYS8024 = ${INTELSYSC}/i8080.c ${ISYS8024D}/isys8024_sys.c \
	${ISYS8024D}/isbc8024.c ${INTEL_PARTS}
ISYS8024_OPT = -I ${ISYS8024D}


ISYS8030D = ${INTELSYSD}/isys8030
ISYS8030 = ${INTELSYSC}/i8080.c ${ISYS8030D}/isys8030_sys.c \
	${ISYS8030D}/isbc8030.c ${INTEL_PARTS}
ISYS8030_OPT = -I ${ISYS8030D}


IMDS210D = ${INTELSYSD}/imds-210
IMDS210 = ${INTELSYSC}/i8080.c ${IMDS210D}/imds-210_sys.c \
	${INTELSYSC}/ipb.c ${INTEL_PARTS}
IMDS210_OPT = -I ${IMDS210D}


IMDS220D = ${INTELSYSD}/imds-220
IMDS220 = ${INTELSYSC}/i8080.c ${IMDS220D}/imds-220_sys.c \
	${INTELSYSC}/ipb.c ${INTEL_PARTS}
IMDS220_OPT = -I ${IMDS220D}


IMDS225D = ${INTELSYSD}/imds-225
IMDS225 = ${INTELSYSC}/i8080.c ${IMDS225D}/imds-225_sys.c \
	${INTELSYSC}/ipc.c ${INTEL_PARTS}
IMDS225_OPT = -I ${IMDS225D}


IMDS230D = ${INTELSYSD}/imds-230
IMDS230 = ${INTELSYSC}/i8080.c ${IMDS230D}/imds-230_sys.c \
	${INTELSYSC}/ipb.c ${INTEL_PARTS}
IMDS230_OPT = -I ${IMDS230D}


IMDS800D = ${INTELSYSD}/imds-800
IMDS800 = ${INTELSYSC}/i8080.c ${IMDS800D}/imds-800_sys.c \
        ${IMDS800D}/cpu.c ${IMDS800D}/front_panel.c \
        ${IMDS800D}/monitor.c ${INTEL_PARTS}
IMDS800_OPT = -I ${IMDS800D}


IMDS810D = ${INTELSYSD}/imds-810
IMDS810 = ${INTELSYSC}/i8080.c ${IMDS810D}/imds-810_sys.c \
        ${IMDS810D}/cpu.c ${IMDS810D}/front_panel.c \
        ${IMDS810D}/monitor.c ${INTEL_PARTS}
IMDS810_OPT = -I ${IMDS810D}


IBMPCD = ${INTELSYSD}/ibmpc
IBMPCC = ${INTELSYSD}/common
IBMPC =	${IBMPCC}/i8255.c ${IBMPCD}/ibmpc.c \
	${IBMPCC}/i8088.c ${IBMPCD}/ibmpc_sys.c \
	${IBMPCC}/i8253.c ${IBMPCC}/i8259.c \
	${IBMPCC}/pceprom.c ${IBMPCC}/pcram8.c \
	${IBMPCC}/i8237.c ${IBMPCC}/pcbus.c
IBMPC_OPT = -I ${IBMPCD}


IBMPCXTD = ${INTELSYSD}/ibmpcxt
IBMPCXTC = ${INTELSYSD}/common
IBMPCXT = ${IBMPCXTC}/i8088.c ${IBMPCXTD}/ibmpcxt_sys.c \
	${IBMPCXTC}/i8253.c ${IBMPCXTC}/i8259.c \
	${IBMPCXTC}/i8255.c ${IBMPCXTD}/ibmpcxt.c \
	${IBMPCXTC}/pceprom.c ${IBMPCXTC}/pcram8.c \
	${IBMPCXTC}/pcbus.c ${IBMPCXTC}/i8237.c 
IBMPCXT_OPT = -I ${IBMPCXTD}


SCELBID = ${INTELSYSD}/scelbi
SCELBIC = ${INTELSYSD}/common
SCELBI = ${SCELBIC}/i8008.c ${SCELBID}/scelbi_sys.c ${SCELBID}/scelbi_io.c
SCELBI_OPT = -I ${SCELBID}


TX0D = ${SIMHD}/TX-0
TX0 = ${TX0D}/tx0_cpu.c ${TX0D}/tx0_dpy.c ${TX0D}/tx0_stddev.c \
	${TX0D}/tx0_sys.c ${TX0D}/tx0_sys_orig.c ${DISPLAYL}
TX0_OPT = -I ${TX0D} ${DISPLAY_OPT}


SSEMD = ${SIMHD}/SSEM
SSEM = ${SSEMD}/ssem_cpu.c ${SSEMD}/ssem_sys.c
SSEM_OPT = -I ${SSEMD}

B5500D = ${SIMHD}/B5500
B5500 = ${B5500D}/b5500_cpu.c ${B5500D}/b5500_io.c ${B5500D}/b5500_sys.c \
	${B5500D}/b5500_dk.c ${B5500D}/b5500_mt.c ${B5500D}/b5500_urec.c \
	${B5500D}/b5500_dr.c ${B5500D}/b5500_dtc.c
B5500_OPT = -I.. -DUSE_INT64 -DB5500 -DUSE_SIM_CARD

BESM6D = ${SIMHD}/BESM6
BESM6 = ${BESM6D}/besm6_cpu.c ${BESM6D}/besm6_sys.c ${BESM6D}/besm6_mmu.c \
        ${BESM6D}/besm6_arith.c ${BESM6D}/besm6_disk.c ${BESM6D}/besm6_drum.c \
        ${BESM6D}/besm6_tty.c ${BESM6D}/besm6_panel.c ${BESM6D}/besm6_printer.c \
        ${BESM6D}/besm6_punch.c ${BESM6D}/besm6_punchcard.c

ifneq (,$(BESM6_BUILD))
    BESM6_OPT = -I ${BESM6D} -DUSE_INT64 $(BESM6_PANEL_OPT)
    ifneq (,$(and ${SDLX_CONFIG},${VIDEO_LDFLAGS}, $(or $(and $(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf)), $(and $(call find_include,SDL/SDL_ttf),$(call find_lib,SDL_ttf)))))
        FONTPATH += /usr/share/fonts /Library/Fonts /usr/lib/jvm /System/Library/Frameworks/JavaVM.framework/Versions C:/Windows/Fonts
        FONTPATH := $(dir $(foreach dir,$(strip $(FONTPATH)),$(wildcard $(dir)/.)))
        FONTNAME += DejaVuSans.ttf LucidaSansRegular.ttf FreeSans.ttf AppleGothic.ttf tahoma.ttf
#cmake-insert:set(BESM6_FONT)
#cmake-insert:foreach (fdir IN ITEMS
#cmake-insert:            "/usr/share/fonts" "/Library/Fonts" "/usr/lib/jvm"
#cmake-insert:            "/System/Library/Frameworks/JavaVM.framework/Versions"
#cmake-insert:            "$ENV{WINDIR}/Fonts")
#cmake-insert:    foreach (font IN ITEMS
#cmake-insert:                "DejaVuSans.ttf" "LucidaSansRegular.ttf" "FreeSans.ttf" "AppleGothic.ttf" "tahoma.ttf")
#cmake-insert:        if (EXISTS ${fdir})
#cmake-insert:            file(GLOB_RECURSE found_font ${fdir}/${font})
#cmake-insert:            if (found_font)
#cmake-insert:                get_filename_component(fontfile ${found_font} ABSOLUTE)
#cmake-insert:                list(APPEND BESM6_FONT ${fontfile})
#cmake-insert:            endif ()
#cmake-insert:        endif ()
#cmake-insert:    endforeach()
#cmake-insert:endforeach()
#cmake-insert:
#cmake-insert:if (NOT BESM6_FONT)
#cmake-insert:    message("No font file available, BESM-6 video panel disabled")
#cmake-insert:    set(BESM6_PANEL_OPT)
#cmake-insert:endif ()
#cmake-insert:
#cmake-insert:if (BESM6_FONT AND WITH_VIDEO)
#cmake-insert:    list(GET BESM6_FONT 0 BESM6_FONT)
#cmake-insert:endif ()
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
            $(info ***          a font path with FONTPATH=path)
            $(info ***          a font name with FONTNAME=fontname.ttf)
            $(info ***          a font file with FONTFILE=path/fontname.ttf)
            $(info ***)
        endif
    endif
    ifeq (,$(and ${VIDEO_LDFLAGS}, ${FONTFILE}, $(BESM6_BUILD)))
        $(info *** No SDL ttf support available.  BESM-6 video panel disabled.)
        $(info ***)
        ifeq (Darwin,$(OSTYPE))
          ifeq (/opt/local/bin/port,$(shell which port))
            $(info *** Info *** Install the MacPorts libSDL2-ttf development package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       # port install libsdl2-ttf-dev)
          endif
          ifeq (/usr/local/bin/brew,$(shell which brew))
            ifeq (/opt/local/bin/port,$(shell which port))
              $(info *** Info ***)
              $(info *** Info *** OR)
              $(info *** Info ***)
            endif
            $(info *** Info *** Install the HomeBrew sdl2_ttf package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       $$ brew install sdl2_ttf)
          endif
        else
          ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
            $(info *** Info *** Install the development components of libSDL2-ttf)
            $(info *** Info *** packaged for your Linux operating system distribution:)
            $(info *** Info ***        $$ sudo apt-get install libsdl2-ttf-dev)
          else
            $(info *** Info *** Install the development components of libSDL2-ttf packaged by your)
            $(info *** Info *** operating system distribution and rebuild your simulator to)
            $(info *** Info *** enable this extra functionality.)
          endif
        endif
        BESM6_OPT = -I ${BESM6D} -DUSE_INT64 
    else ifneq (,$(and $(findstring sdl2,${VIDEO_LDFLAGS}),$(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf)))
        $(info using libSDL2_ttf: $(call find_lib,SDL2_ttf) $(call find_include,SDL2/SDL_ttf))
        $(info ***)
        BESM6_PANEL_OPT = -DFONTFILE=${FONTFILE} ${VIDEO_CCDEFS} ${VIDEO_LDFLAGS} -lSDL2_ttf
    endif
endif

PDP6D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
  PDP6_DISPLAY_OPT = 
endif
PDP6 = ${PDP6D}/kx10_cpu.c ${PDP6D}/kx10_sys.c ${PDP6D}/kx10_cty.c \
	${PDP6D}/kx10_lp.c ${PDP6D}/kx10_pt.c ${PDP6D}/kx10_cr.c \
	${PDP6D}/kx10_cp.c ${PDP6D}/pdp6_dct.c ${PDP6D}/pdp6_dtc.c \
	${PDP6D}/pdp6_mtc.c ${PDP6D}/pdp6_dsk.c ${PDP6D}/pdp6_dcs.c \
	${PDP6D}/kx10_dpy.c ${PDP6D}/pdp6_slave.c ${DISPLAYL} ${DISPLAY340}
PDP6_OPT = -DPDP6=1 -DUSE_INT64 -I ${PDP6D} -DUSE_SIM_CARD ${DISPLAY_OPT} ${PDP6_DISPLAY_OPT}

KA10D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
  KA10_DISPLAY_OPT = 
endif
KA10 = ${KA10D}/kx10_cpu.c ${KA10D}/kx10_sys.c ${KA10D}/kx10_df.c \
	${KA10D}/kx10_dp.c ${KA10D}/kx10_mt.c ${KA10D}/kx10_cty.c \
	${KA10D}/kx10_lp.c ${KA10D}/kx10_pt.c ${KA10D}/kx10_dc.c \
	${KA10D}/kx10_rp.c ${KA10D}/kx10_rc.c ${KA10D}/kx10_dt.c \
	${KA10D}/kx10_dk.c ${KA10D}/kx10_cr.c ${KA10D}/kx10_cp.c \
	${KA10D}/kx10_tu.c ${KA10D}/kx10_rs.c ${KA10D}/ka10_pd.c \
	${KA10D}/kx10_rh.c ${KA10D}/kx10_imp.c ${KA10D}/ka10_tk10.c \
	${KA10D}/ka10_mty.c ${KA10D}/ka10_imx.c ${KA10D}/ka10_ch10.c \
	${KA10D}/ka10_stk.c ${KA10D}/ka10_ten11.c ${KA10D}/ka10_auxcpu.c \
	$(KA10D)/ka10_pmp.c ${KA10D}/ka10_dkb.c ${KA10D}/pdp6_dct.c \
	${KA10D}/pdp6_dtc.c ${KA10D}/pdp6_mtc.c ${KA10D}/pdp6_dsk.c \
	${KA10D}/pdp6_dcs.c ${KA10D}/ka10_dpk.c ${KA10D}/kx10_dpy.c \
	${KA10D}/ka10_ai.c ${KA10D}/ka10_iii.c ${KA10D}/kx10_disk.c \
	${KA10D}//ka10_pclk.c ${DISPLAYL} ${DISPLAY340} ${DISPLAYIII}
KA10_OPT = -DKA=1 -DUSE_INT64 -I ${KA10D} -DUSE_SIM_CARD ${NETWORK_OPT} ${DISPLAY_OPT} ${KA10_DISPLAY_OPT}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
KA10_OPT += -DPANDA_LIGHTS
KA10 += ${KA10D}/ka10_lights.c
KA10_LDFLAGS += -lusb-1.0
endif

KI10D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
KI10_DISPLAY_OPT = 
endif
KI10 = ${KI10D}/kx10_cpu.c ${KI10D}/kx10_sys.c ${KI10D}/kx10_df.c \
	${KI10D}/kx10_dp.c ${KI10D}/kx10_mt.c ${KI10D}/kx10_cty.c \
	${KI10D}/kx10_lp.c ${KI10D}/kx10_pt.c ${KI10D}/kx10_dc.c  \
	${KI10D}/kx10_rh.c ${KI10D}/kx10_rp.c ${KI10D}/kx10_rc.c \
	${KI10D}/kx10_dt.c ${KI10D}/kx10_dk.c ${KI10D}/kx10_cr.c \
	${KI10D}/kx10_cp.c ${KI10D}/kx10_tu.c ${KI10D}/kx10_rs.c \
	${KI10D}/kx10_imp.c ${KI10D}/kx10_dpy.c ${KI10D}/kx10_disk.c \
	${DISPLAYL} ${DISPLAY340}
KI10_OPT = -DKI=1 -DUSE_INT64 -I ${KI10D} -DUSE_SIM_CARD ${NETWORK_OPT} ${DISPLAY_OPT} ${KI10_DISPLAY_OPT}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
KI10_OPT += -DPANDA_LIGHTS
KI10 += ${KA10D}/ka10_lights.c
KI10_LDFLAGS = -lusb-1.0
endif

KL10D = ${SIMHD}/PDP10
KL10 = ${KL10D}/kx10_cpu.c ${KL10D}/kx10_sys.c ${KL10D}/kx10_df.c \
	${KL10D}/kx10_mt.c ${KL10D}/kx10_dc.c ${KL10D}/kx10_rh.c \
	${KL10D}/kx10_rp.c ${KL10D}/kx10_tu.c ${KL10D}/kx10_rs.c \
	${KL10D}/kx10_imp.c ${KL10D}/kl10_fe.c ${KL10D}/ka10_pd.c \
	${KL10D}/ka10_ch10.c ${KL10D}/kx10_lp.c ${KL10D}/kl10_nia.c \
	${KL10D}/kx10_disk.c
KL10_OPT = -DKL=1 -DUSE_INT64 -I $(KL10D) -DUSE_SIM_CARD ${NETWORK_OPT} 

ATT3B2D = ${SIMHD}/3B2
ATT3B2M400 = ${ATT3B2D}/3b2_400_cpu.c ${ATT3B2D}/3b2_400_sys.c \
	${ATT3B2D}/3b2_400_stddev.c ${ATT3B2D}/3b2_400_mmu.c \
	${ATT3B2D}/3b2_400_mau.c ${ATT3B2D}/3b2_iu.c \
	${ATT3B2D}/3b2_if.c ${ATT3B2D}/3b2_id.c \
	${ATT3B2D}/3b2_dmac.c ${ATT3B2D}/3b2_io.c \
	${ATT3B2D}/3b2_ports.c ${ATT3B2D}/3b2_ctc.c \
	${ATT3B2D}/3b2_ni.c
ATT3B2_OPT = -DUSE_INT64 -DUSE_ADDR64 -I ${ATT3B2D} ${NETWORK_OPT}

SIGMAD = ${SIMHD}/sigma
SIGMA = ${SIGMAD}/sigma_cpu.c ${SIGMAD}/sigma_sys.c ${SIGMAD}/sigma_cis.c \
	${SIGMAD}/sigma_coc.c ${SIGMAD}/sigma_dk.c ${SIGMAD}/sigma_dp.c \
	${SIGMAD}/sigma_fp.c ${SIGMAD}/sigma_io.c ${SIGMAD}/sigma_lp.c \
	${SIGMAD}/sigma_map.c ${SIGMAD}/sigma_mt.c ${SIGMAD}/sigma_pt.c \
    ${SIGMAD}/sigma_rad.c ${SIGMAD}/sigma_rtc.c ${SIGMAD}/sigma_tt.c
SIGMA_OPT = -I ${SIGMAD}

###
### Experimental simulators
###

CDC1700D = ${SIMHD}/CDC1700
CDC1700 = ${CDC1700D}/cdc1700_cpu.c ${CDC1700D}/cdc1700_dis.c \
        ${CDC1700D}/cdc1700_io.c ${CDC1700D}/cdc1700_sys.c \
        ${CDC1700D}/cdc1700_dev1.c ${CDC1700D}/cdc1700_mt.c \
        ${CDC1700D}/cdc1700_dc.c ${CDC1700D}/cdc1700_iofw.c \
        ${CDC1700D}/cdc1700_lp.c ${CDC1700D}/cdc1700_dp.c \
        ${CDC1700D}/cdc1700_cd.c ${CDC1700D}/cdc1700_sym.c \
        ${CDC1700D}/cdc1700_rtc.c ${CDC1700D}/cdc1700_drm.c \
        ${CDC1700D}/cdc1700_msos5.c
CDC1700_OPT = -I ${CDC1700D}

###
### Unsupported/Incomplete simulators
###

ALPHAD = ${SIMHD}/alpha
ALPHA = ${ALPHAD}/alpha_500au_syslist.c ${ALPHAD}/alpha_cpu.c \
    ${ALPHAD}/alpha_ev5_cons.c ${ALPHAD}/alpha_ev5_pal.c \
    ${ALPHAD}/alpha_ev5_tlb.c ${ALPHAD}/alpha_fpi.c \
    ${ALPHAD}/alpha_fpv.c ${ALPHAD}/alpha_io.c \
    ${ALPHAD}/alpha_mmu.c ${ALPHAD}/alpha_sys.c
ALPHA_OPT = -I ${ALPHAD} -DUSE_ADDR64 -DUSE_INT64

SAGED = ${SIMHD}/SAGE
SAGE = ${SAGED}/sage_cpu.c ${SAGED}/sage_sys.c ${SAGED}/sage_stddev.c \
    ${SAGED}/sage_cons.c ${SAGED}/sage_fd.c ${SAGED}/sage_lp.c \
    ${SAGED}/m68k_cpu.c ${SAGED}/m68k_mem.c ${SAGED}/m68k_scp.c \
    ${SAGED}/m68k_parse.tab.c ${SAGED}/m68k_sys.c \
    ${SAGED}/i8251.c ${SAGED}/i8253.c ${SAGED}/i8255.c ${SAGED}/i8259.c ${SAGED}/i8272.c 
SAGE_OPT = -I ${SAGED} -DHAVE_INT64 -DUSE_SIM_IMD

PDQ3D = ${SIMHD}/PDQ-3
PDQ3 = ${PDQ3D}/pdq3_cpu.c ${PDQ3D}/pdq3_sys.c ${PDQ3D}/pdq3_stddev.c \
    ${PDQ3D}/pdq3_mem.c ${PDQ3D}/pdq3_debug.c ${PDQ3D}/pdq3_fdc.c 
PDQ3_OPT = -I ${PDQ3D} -DUSE_SIM_IMD

#
# Build everything (not the unsupported/incomplete or experimental simulators)
#
ALL = pdp1 pdp4 pdp7 pdp8 pdp9 pdp15 pdp11 pdp10 \
	vax microvax3900 microvax1 rtvax1000 microvax2 vax730 vax750 vax780 \
	vax8200 vax8600 besm6 \
	microvax2000 infoserver100 infoserver150vxt microvax3100 microvax3100e \
	vaxstation3100m30 vaxstation3100m38 vaxstation3100m76 vaxstation4000m60 \
	microvax3100m80 vaxstation4000vlc infoserver1000 \
	nova eclipse hp2100 hp3000 i1401 i1620 s3 altair altairz80 gri \
	i7094 ibm1130 id16 id32 sds lgp h316 cdc1700 \
	swtp6800mp-a swtp6800mp-a2 tx-0 ssem b5500 isdk80 ids880 isys8010 isys8020 \
	isys8030 isys8024 imds-210 imds-220 imds-225 imds-230 imds-800 imds-810 \
	scelbi 3b2 i701 i704 i7010 i7070 i7080 i7090 \
	sigma uc15 pdp10-ka pdp10-ki pdp10-kl pdp6 i650

all : ${ALL}

EXPERIMENTAL = cdc1700 

experimental : ${EXPERIMENTAL}

clean :
ifeq (${WIN32},)
	${RM} -rf ${BIN}
else
	if exist BIN rmdir /s /q BIN
endif

${BUILD_ROMS} : 
	${MKDIRBIN}
ifeq (${WIN32},)
	@if ${TEST} \( ! -e $@ \) -o \( sim_BuildROMs.c -nt $@ \) ; then ${CC} sim_BuildROMs.c ${CC_OUTSPEC}; fi
else
	@if not exist $@ ${CC} sim_BuildROMs.c ${CC_OUTSPEC}
endif
	@$@

#
# Individual builds
#
pdp1 : ${BIN}pdp1${EXE}

${BIN}pdp1${EXE} : ${PDP1} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP1} ${SIM} ${PDP1_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP1D},pdp1))
	$@ $(call find_test,${PDP1D},pdp1) ${TEST_ARG}
endif

pdp4 : ${BIN}pdp4${EXE}

${BIN}pdp4${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP4_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP18BD},pdp4))
	$@ $(call find_test,${PDP18BD},pdp4) ${TEST_ARG}
endif

pdp7 : ${BIN}pdp7${EXE}

${BIN}pdp7${EXE} : ${PDP18B} ${PDP18BD}/pdp18b_dpy.c ${DISPLAYL} ${DISPLAY340} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${PDP18BD}/pdp18b_dpy.c ${DISPLAYL} ${DISPLAY340} ${SIM} ${PDP7_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP18BD},pdp7))
	$@ $(call find_test,${PDP18BD},pdp7) ${TEST_ARG}
endif

pdp8 : ${BIN}pdp8${EXE}

${BIN}pdp8${EXE} : ${PDP8} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP8} ${SIM} ${PDP8_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP8D},pdp8))
	$@ $(call find_test,${PDP8D},pdp8) ${TEST_ARG}
endif

pdp9 : ${BIN}pdp9${EXE}

${BIN}pdp9${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP9_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP18BD},pdp9))
	$@ $(call find_test,${PDP18BD},pdp9) ${TEST_ARG}
endif

pdp15 : ${BIN}pdp15${EXE}

${BIN}pdp15${EXE} : ${PDP18B} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP18B} ${SIM} ${PDP15_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP18BD},pdp15))
	$@ $(call find_test,${PDP18BD},pdp15) ${TEST_ARG}
endif

pdp10 : ${BIN}pdp10${EXE}

${BIN}pdp10${EXE} : ${PDP10} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP10} ${SIM} ${PDP10_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP10D},pdp10))
	$@ $(call find_test,${PDP10D},pdp10) ${TEST_ARG}
endif

imlac : ${BIN}imlac${EXE}

${BIN}imlac${EXE} : ${IMLAC} ${SIM}
	${MKDIRBIN}
	${CC} ${IMLAC} ${SIM} ${IMLAC_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMLAC},imlac))
	$@ $(call find_test,${IMLACD},imlac) ${TEST_ARG}
endif

tt2500 : ${BIN}tt2500${EXE}

${BIN}tt2500${EXE} : ${TT2500} ${SIM}
	${MKDIRBIN}
	${CC} ${TT2500} ${SIM} ${TT2500_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${TT2500},tt2500))
	$@ $(call find_test,${TT2500D},tt2500) ${TEST_ARG}
endif

pdp11 : ${BIN}pdp11${EXE}

${BIN}pdp11${EXE} : ${PDP11} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP11} ${SIM} ${PDP11_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP11D},pdp11))
	$@ $(call find_test,${PDP11D},pdp11) ${TEST_ARG}
endif

uc15 : ${BIN}uc15${EXE}

${BIN}uc15${EXE} : ${UC15} ${SIM}
	${MKDIRBIN}
	${CC} ${UC15} ${SIM} ${UC15_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP11D},uc15))
	$@ $(call find_test,${PDP11D},uc15) ${TEST_ARG}
endif

microvax3900 : vax

vax : ${BIN}vax${EXE}

${BIN}vax${EXE} : ${VAX} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX} ${SIM} ${VAX_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifeq (${WIN32},)
	cp ${BIN}vax${EXE} ${BIN}microvax3900${EXE}
else
	copy $(@D)\vax${EXE} $(@D)\microvax3900${EXE}
endif
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax2000 : ${BIN}microvax2000${EXE}

${BIN}microvax2000${EXE} : ${VAX410} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX410} ${SCSI} ${SIM} ${VAX410_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

infoserver100 : ${BIN}infoserver100${EXE}

${BIN}infoserver100${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX411_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

infoserver150vxt : ${BIN}infoserver150vxt${EXE}

${BIN}infoserver150vxt${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX412_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax3100 : ${BIN}microvax3100${EXE}

${BIN}microvax3100${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX41A_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax3100e : ${BIN}microvax3100e${EXE}

${BIN}microvax3100e${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX41D_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vaxstation3100m30 : ${BIN}vaxstation3100m30${EXE}

${BIN}vaxstation3100m30${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX42A_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vaxstation3100m38 : ${BIN}vaxstation3100m38${EXE}

${BIN}vaxstation3100m38${EXE} : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX420} ${SCSI} ${SIM} ${VAX42B_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vaxstation3100m76 : ${BIN}vaxstation3100m76${EXE}

${BIN}vaxstation3100m76${EXE} : ${VAX43} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX43} ${SCSI} ${SIM} ${VAX43_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vaxstation4000m60 : ${BIN}vaxstation4000m60${EXE}

${BIN}vaxstation4000m60${EXE} : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX440} ${SCSI} ${SIM} ${VAX46_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax3100m80 : ${BIN}microvax3100m80${EXE}

${BIN}microvax3100m80${EXE} : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX440} ${SCSI} ${SIM} ${VAX47_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vaxstation4000vlc : ${BIN}vaxstation4000vlc${EXE}

${BIN}vaxstation4000vlc${EXE} : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX440} ${SCSI} ${SIM} ${VAX48_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

infoserver1000 : ${BIN}infoserver1000${EXE}

${BIN}infoserver1000${EXE} : ${IS1000} ${SCSI} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IS1000} ${SCSI} ${SIM} ${IS1000_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax1 : ${BIN}microvax1${EXE}

${BIN}microvax1${EXE} : ${VAX610} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX610} ${SIM} ${VAX610_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

rtvax1000 : ${BIN}rtvax1000${EXE}

${BIN}rtvax1000${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX620_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

microvax2 : ${BIN}microvax2${EXE}

${BIN}microvax2${EXE} : ${VAX630} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX630} ${SIM} ${VAX630_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vax730 : ${BIN}vax730${EXE}

${BIN}vax730${EXE} : ${VAX730} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX730} ${SIM} ${VAX730_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vax750 : ${BIN}vax750${EXE}

${BIN}vax750${EXE} : ${VAX750} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX750} ${SIM} ${VAX750_OPT} -o $@ ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vax780 : ${BIN}vax780${EXE}

${BIN}vax780${EXE} : ${VAX780} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX780} ${SIM} ${VAX780_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vax8200 : ${BIN}vax8200${EXE}

${BIN}vax8200${EXE} : ${VAX8200} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX8200} ${SIM} ${VAX8200_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

vax8600 : ${BIN}vax8600${EXE}

${BIN}vax8600${EXE} : ${VAX8600} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${VAX8600} ${SIM} ${VAX8600_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${VAXD},vax-diag))
	$@ $(call find_test,${VAXD},vax-diag) ${TEST_ARG}
endif

nova : ${BIN}nova${EXE}

${BIN}nova${EXE} : ${NOVA} ${SIM}
	${MKDIRBIN}
	${CC} ${NOVA} ${SIM} ${NOVA_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${NOVAD},nova))
	$@ $(call find_test,${NOVAD},nova) ${TEST_ARG}
endif

eclipse : ${BIN}eclipse${EXE}

${BIN}eclipse${EXE} : ${ECLIPSE} ${SIM}
	${MKDIRBIN}
	${CC} ${ECLIPSE} ${SIM} ${ECLIPSE_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${NOVAD},eclipse))
	$@ $(call find_test,${NOVAD},eclipse) ${TEST_ARG}
endif

h316 : ${BIN}h316${EXE}

${BIN}h316${EXE} : ${H316} ${SIM}
	${MKDIRBIN}
	${CC} ${H316} ${SIM} ${H316_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${H316D},h316))
	$@ $(call find_test,${H316D},h316) ${TEST_ARG}
endif

hp2100 : ${BIN}hp2100${EXE}

${BIN}hp2100${EXE} : ${HP2100} ${SIM}
ifneq (1,${CPP_BUILD}${CPP_FORCE})
	${MKDIRBIN}
	${CC} ${HP2100} ${SIM} ${HP2100_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${HP2100D},hp2100))
	$@ $(call find_test,${HP2100D},hp2100) ${TEST_ARG}
endif
else
	$(info hp2100 can't be built using C++)
endif

hp3000 : ${BIN}hp3000${EXE}

${BIN}hp3000${EXE} : ${HP3000} ${SIM}
ifneq (1,${CPP_BUILD}${CPP_FORCE})
	${MKDIRBIN}
	${CC} ${HP3000} ${SIM} ${HP3000_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${HP3000D},hp3000))
	$@ $(call find_test,${HP3000D},hp3000) ${TEST_ARG}
endif
else
	$(info hp3000 can't be built using C++)
endif

i1401 : ${BIN}i1401${EXE}

${BIN}i1401${EXE} : ${I1401} ${SIM}
	${MKDIRBIN}
	${CC} ${I1401} ${SIM} ${I1401_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I1401D},i1401))
	$@ $(call find_test,${I1401D},i1401) ${TEST_ARG}
endif

i1620 : ${BIN}i1620${EXE}

${BIN}i1620${EXE} : ${I1620} ${SIM}
	${MKDIRBIN}
	${CC} ${I1620} ${SIM} ${I1620_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I1620D},i1620))
	$@ $(call find_test,${I1620D},i1620) ${TEST_ARG}
endif

i7094 : ${BIN}i7094${EXE}

${BIN}i7094${EXE} : ${I7094} ${SIM}
	${MKDIRBIN}
	${CC} ${I7094} ${SIM} ${I7094_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7094D},i7094))
	$@ $(call find_test,${I7094D},i7094) ${TEST_ARG}
endif

ibm1130 : ${BIN}ibm1130${EXE}

${BIN}ibm1130${EXE} : ${IBM1130}
ifneq (1,${CPP_BUILD}${CPP_FORCE})
	${MKDIRBIN}
ifneq (${WIN32},)
	windres ${IBM1130D}/ibm1130.rc ${BIN}ibm1130.o
endif
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (${WIN32},)
	del BIN\ibm1130.o
endif
ifneq (,$(call find_test,${IBM1130D},ibm1130))
	$@ $(call find_test,${IBM1130D},ibm1130) ${TEST_ARG}
endif
else
	$(info ibm1130 can't be built using C++)
endif

s3 : ${BIN}s3${EXE}

${BIN}s3${EXE} : ${S3} ${SIM}
	${MKDIRBIN}
	${CC} ${S3} ${SIM} ${S3_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${S3D},s3))
	$@ $(call find_test,${S3D},s3) ${TEST_ARG}
endif

altair : ${BIN}altair${EXE}

${BIN}altair${EXE} : ${ALTAIR} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIR} ${SIM} ${ALTAIR_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ALTAIRD},altair))
	$@ $(call find_test,${ALTAIRD},altair) ${TEST_ARG}
endif

altairz80 : ${BIN}altairz80${EXE}

${BIN}altairz80${EXE} : ${ALTAIRZ80} ${SIM}
	${MKDIRBIN}
	${CC} ${ALTAIRZ80} ${SIM} ${ALTAIRZ80_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ALTAIRZ80D},altairz80))
	$@ $(call find_test,${ALTAIRZ80D},altairz80) ${TEST_ARG}
endif

gri : ${BIN}gri${EXE}

${BIN}gri${EXE} : ${GRI} ${SIM}
	${MKDIRBIN}
	${CC} ${GRI} ${SIM} ${GRI_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${GRID},gri))
	$@ $(call find_test,${GRID},gri) ${TEST_ARG}
endif

lgp : ${BIN}lgp${EXE}

${BIN}lgp${EXE} : ${LGP} ${SIM}
	${MKDIRBIN}
	${CC} ${LGP} ${SIM} ${LGP_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${LGPD},lgp))
	$@ $(call find_test,${LGPD},lgp) ${TEST_ARG}
endif

id16 : ${BIN}id16${EXE}

${BIN}id16${EXE} : ${ID16} ${SIM}
	${MKDIRBIN}
	${CC} ${ID16} ${SIM} ${ID16_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ID32D},id16))
	$@ $(call find_test,${ID32D},id16) ${TEST_ARG}
endif

id32 : ${BIN}id32${EXE}

${BIN}id32${EXE} : ${ID32} ${SIM}
	${MKDIRBIN}
	${CC} ${ID32} ${SIM} ${ID32_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ID32D},id32))
	$@ $(call find_test,${ID32D},id32) ${TEST_ARG}
endif

sds : ${BIN}sds${EXE}

${BIN}sds${EXE} : ${SDS} ${SIM}
	${MKDIRBIN}
	${CC} ${SDS} ${SIM} ${SDS_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SDSD},sds))
	$@ $(call find_test,${SDSD},sds) ${TEST_ARG}
endif

swtp6800mp-a : ${BIN}swtp6800mp-a${EXE}

${BIN}swtp6800mp-a${EXE} : ${SWTP6800MP-A} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A} ${SIM} ${SWTP6800_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SWTP6800D},swtp6800mp-a))
	$@ $(call find_test,${SWTP6800D},swtp6800mp-a) ${TEST_ARG}
endif

swtp6800mp-a2 : ${BIN}swtp6800mp-a2${EXE}

${BIN}swtp6800mp-a2${EXE} : ${SWTP6800MP-A2} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A2} ${SIM} ${SWTP6800_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SWTP6800D},swtp6800mp-a2))
	$@ $(call find_test,${SWTP6800D},swtp6800mp-a2) ${TEST_ARG}
endif

isdk80: ${BIN}isdk80${EXE}

${BIN}isdk80${EXE} : ${ISDK80} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISDK80} ${SIM} ${ISDK80_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ISDK80D},isdk80))
	$@ $(call find_test,${ISDK80D},isdk80) ${TEST_ARG}
endif

ids880: ${BIN}ids880${EXE}

${BIN}ids880${EXE} : ${IDS880} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IDS880} ${SIM} ${IDS880_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IDS880D},ids880))
	$@ $(call find_test,${IDS880D},ids880) ${TEST_ARG}
endif

isys8010: ${BIN}isys8010${EXE}

${BIN}isys8010${EXE} : ${ISYS8010} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8010} ${SIM} ${ISYS8010_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ISYS8010D},isys8010))
	$@ $(call find_test,${ISYS8010D},isys8010) ${TEST_ARG}
endif

isys8020: ${BIN}isys8020${EXE}

${BIN}isys8020${EXE} : ${ISYS8020} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8020} ${SIM} ${ISYS8020_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ISYS8020D},isys8020))
	$@ $(call find_test,${ISYS8020D},isys8020) ${TEST_ARG}
endif

isys8024: ${BIN}isys8024${EXE}

${BIN}isys8024${EXE} : ${ISYS8024} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8024} ${SIM} ${ISYS8024_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ISYS8024D},isys8024))
	$@ $(call find_test,${ISYS8024D},isys8024) ${TEST_ARG}
endif

isys8030: ${BIN}isys8030${EXE}

${BIN}isys8030${EXE} : ${ISYS8030} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ISYS8030} ${SIM} ${ISYS8030_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ISYS8030D},isys8030))
	$@ $(call find_test,${ISYS8030D},isys8030) ${TEST_ARG}
endif

imds-210: ${BIN}imds-210${EXE}

${BIN}imds-210${EXE} : ${IMDS210} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS210} ${SIM} ${IMDS210_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS210D},imds-210))
	$@ $(call find_test,${IMDS210D},imds-210) ${TEST_ARG}
endif

imds-220: ${BIN}imds-220${EXE}

${BIN}imds-220${EXE} : ${IMDS220} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS220} ${SIM} ${IMDS220_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS220D},imds-220))
	$@ $(call find_test,${IMDS220D},imds-220) ${TEST_ARG}
endif

imds-225: ${BIN}imds-225${EXE}

${BIN}imds-225${EXE} : ${IMDS225} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS225} ${SIM} ${IMDS225_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS225D},imds-225))
	$@ $(call find_test,${IMDS225D},imds-225) ${TEST_ARG}
endif

imds-230: ${BIN}imds-230${EXE}

${BIN}imds-230${EXE} : ${IMDS230} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS230} ${SIM} ${IMDS230_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS230D},imds-230))
	$@ $(call find_test,${IMDS230D},imds-230) ${TEST_ARG}
endif

imds-800: ${BIN}imds-800${EXE}

${BIN}imds-800${EXE} : ${IMDS800} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS800} ${SIM} ${IMDS800_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS800D},imds-800))
	$@ $(call find_test,${IMDS800D},imds-800) ${TEST_ARG}
endif

imds-810: ${BIN}imds-810${EXE}

${BIN}imds-810${EXE} : ${IMDS810} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${IMDS810} ${SIM} ${IMDS810_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IMDS810D},imds-810))
	$@ $(call find_test,${IMDS810D},imds-810) ${TEST_ARG}
endif

ibmpc: ${BIN}ibmpc${EXE}

${BIN}ibmpc${EXE} : ${IBMPC} ${SIM} ${BUILD_ROMS}
	#cmake:ignore-target
	${MKDIRBIN}
	${CC} ${IBMPC} ${SIM} ${IBMPC_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IBMPCD},ibmpc))
	$@ $(call find_test,${IBMPCD},ibmpc) ${TEST_ARG}
endif

ibmpcxt: ${BIN}ibmpcxt${EXE}

${BIN}ibmpcxt${EXE} : ${IBMPCXT} ${SIM} ${BUILD_ROMS}
	#cmake:ignore-target
	${MKDIRBIN}
	${CC} ${IBMPCXT} ${SIM} ${IBMPCXT_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IBMPCXTD},ibmpcxt))
	$@ $(call find_test,${IBMPCXTD},ibmpcxt) ${TEST_ARG}
endif

scelbi: ${BIN}scelbi${EXE}

${BIN}scelbi${EXE} : ${SCELBI} ${SIM}
	${MKDIRBIN}
	${CC} ${SCELBI} ${SIM} ${SCELBI_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SCELBID},scelbi))
	$@ $(call find_test,${SCELBID},scelbi) ${TEST_ARG}
endif

tx-0 : ${BIN}tx-0${EXE}

${BIN}tx-0${EXE} : ${TX0} ${SIM}
	${MKDIRBIN}
	${CC} ${TX0} ${SIM} ${TX0_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${TX0D},tx-0))
	$@ $(call find_test,${TX0D},tx-0) ${TEST_ARG}
endif

ssem : ${BIN}ssem${EXE}

${BIN}ssem${EXE} : ${SSEM} ${SIM}
	${MKDIRBIN}
	${CC} ${SSEM} ${SIM} ${SSEM_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SSEMD},ssem))
	$@ $(call find_test,${SSEMD},ssem) ${TEST_ARG}
endif

cdc1700 : ${BIN}cdc1700${EXE}

${BIN}cdc1700${EXE} : ${CDC1700} ${SIM}
	${MKDIRBIN}
	${CC} ${CDC1700} ${SIM} ${CDC1700_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${CDC1700D},cdc1700))
	$@ $(call find_test,${CDC1700D},cdc1700) ${TEST_ARG}
endif

besm6 : ${BIN}besm6${EXE}

${BIN}besm6${EXE} : ${BESM6} ${SIM}
ifneq (1,${CPP_BUILD}${CPP_FORCE})
	${MKDIRBIN}
	${CC} ${BESM6} ${SIM} ${BESM6_OPT} ${BESM6_PANEL_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${BESM6D},besm6))
	$@ $(call find_test,${BESM6D},besm6) ${TEST_ARG}
endif
else
	$(info besm6 can't be built using C++)
endif

sigma : ${BIN}sigma${EXE}

${BIN}sigma${EXE} : ${SIGMA} ${SIM}
	${MKDIRBIN}
	${CC} ${SIGMA} ${SIM} ${SIGMA_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SIGMAD},sigma))
	$@ $(call find_test,${SIGMAD},sigma) ${TEST_ARG}
endif

alpha : ${BIN}alpha${EXE}

${BIN}alpha${EXE} : ${ALPHA} ${SIM}
	${MKDIRBIN}
	${CC} ${ALPHA} ${SIM} ${ALPHA_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ALPHAD},alpha))
	$@ $(call find_test,${ALPHAD},alpha) ${TEST_ARG}
endif

sage : ${BIN}sage${EXE}

${BIN}sage${EXE} : ${SAGE} ${SIM}
	${MKDIRBIN}
	${CC} ${SAGE} ${SIM} ${SAGE_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SAGED},sage))
	$@ $(call find_test,${SAGED},sage) ${TEST_ARG}
endif

pdq3 : ${BIN}pdq3${EXE}

${BIN}pdq3${EXE} : ${PDQ3} ${SIM}
	${MKDIRBIN}
	${CC} ${PDQ3} ${SIM} ${PDQ3_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDQ3D},pdq3))
	$@ $(call find_test,${PDQ3D},pdq3) ${TEST_ARG}
endif

b5500 : ${BIN}b5500${EXE}

${BIN}b5500${EXE} : ${B5500} ${SIM} 
	${MKDIRBIN}
	${CC} ${B5500} ${SIM} ${B5500_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${B5500D},b5500))
	$@ $(call find_test,${B5500D},b5500) ${TEST_ARG}
endif

3b2 : ${BIN}3b2${EXE}
 
${BIN}3b2${EXE} : ${ATT3B2M400} ${SIM} ${BUILD_ROMS}
	${MKDIRBIN}
	${CC} ${ATT3B2M400} ${SIM} ${ATT3B2_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ATT3B2D},3b2))
	$@ $(call find_test,${ATT3B2D},3b2) ${TEST_ARG}
endif

i7090 : ${BIN}i7090${EXE}

${BIN}i7090${EXE} : ${I7090} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7090} ${SIM} ${I7090_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7000D},i7090))
	$@ $(call find_test,${I7000D},i7090) ${TEST_ARG}
endif

i7080 : ${BIN}i7080${EXE}

${BIN}i7080${EXE} : ${I7080} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7080} ${SIM} ${I7080_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7080D},i7080))
	$@ $(call find_test,${I7080D},i7080) ${TEST_ARG}
endif

i7070 : ${BIN}i7070${EXE}

${BIN}i7070${EXE} : ${I7070} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7070} ${SIM} ${I7070_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7070D},i7070))
	$@ $(call find_test,${I7070D},i7070) ${TEST_ARG}
endif

i7010 : ${BIN}i7010${EXE}

${BIN}i7010${EXE} : ${I7010} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7010} ${SIM} ${I7010_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7010D},i7010))
	$@ $(call find_test,${I7010D},i7010) ${TEST_ARG}
endif

i704 : ${BIN}i704${EXE}

${BIN}i704${EXE} : ${I704} ${SIM} 
	${MKDIRBIN}
	${CC} ${I704} ${SIM} ${I704_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I704D},i704))
	$@ $(call find_test,${I704D},i704) ${TEST_ARG}
endif

i701 : ${BIN}i701${EXE}

${BIN}i701${EXE} : ${I701} ${SIM} 
	${MKDIRBIN}
	${CC} ${I701} ${SIM} ${I701_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I701D},i701))
	$@ $(call find_test,${I701D},i701) ${TEST_ARG}
endif

i650 : ${BIN}i650${EXE}

${BIN}i650${EXE} : ${I650} ${SIM} 
	${MKDIRBIN}
	${CC} ${I650} ${SIM} ${I650_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I650D},i650))
	$@ $(call find_test,${I650D},i650) ${TEST_ARG}
endif

pdp6 : ${BIN}pdp6${EXE}

${BIN}pdp6${EXE} : ${PDP6} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP6} ${PDP6_DPY} ${SIM} ${PDP6_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${PDP6_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},pdp6))
	$@ $(call find_test,${PDP10D},pdp6) ${TEST_ARG}
endif

pdp10-ka : ${BIN}pdp10-ka${EXE}

${BIN}pdp10-ka${EXE} : ${KA10} ${SIM}
	${MKDIRBIN}
	${CC} ${KA10} ${KA10_DPY} ${SIM} ${KA10_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${KA10_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},ka10))
	$@ $(call find_test,${PDP10D},ka10) ${TEST_ARG}
endif

pdp10-ki : ${BIN}pdp10-ki${EXE}

${BIN}pdp10-ki${EXE} : ${KI10} ${SIM}
	${MKDIRBIN}
	${CC} ${KI10} ${KI10_DPY} ${SIM} ${KI10_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${KI10_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},ki10))
	$@ $(call find_test,${PDP10D},ki10) ${TEST_ARG}
endif

pdp10-kl : ${BIN}pdp10-kl${EXE}

${BIN}pdp10-kl${EXE} : ${KL10} ${SIM}
	${MKDIRBIN}
	${CC} ${KL10} ${SIM} ${KL10_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP10D},kl10))
	$@ $(call find_test,${PDP10D},kl10) ${TEST_ARG}
endif

# Front Panel API Demo/Test program

frontpaneltest : ${BIN}frontpaneltest${EXE}

${BIN}frontpaneltest${EXE} : frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c
	#cmake:ignore-target
	${MKDIRBIN}
	${CC} frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c ${CC_OUTSPEC} ${LDFLAGS} ${OS_CURSES_DEFS}

