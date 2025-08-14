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
#   Windows (MinGW & cygwin) - deprecated (maybe works, maybe not)
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
# When building compiler optimized binaries with the gcc or clang
# compilers, invoking GNU make with LTO=1 on the command line will
# cause the build to use Link Time Optimization to maximally optimize
# the results.  Link Time Optimization can report errors which aren't
# otherwise detected and will also take significantly longer to
# complete.  Additionally, non debug builds default to build with an
# optimization level of -O2.  This optimization level can be changed
# by invoking GNU make with OPTIMIZE=-O3 (or whatever optimize value
# you want) on the command line if desired.
#
# The default setup will fail simulator build(s) if the compile
# produces any warnings.  These should be cleaned up before new
# or changed code is accepted into the code base.  This option
# can be overridden if GNU make is invoked with WARNINGS=ALLOWED
# on the command line.
#
# The default build will run per simulator tests if they are
# available.  If building without running tests is desired,
# then GNU make should be invoked with TESTS=0 on the command
# line.
#
# The default build will compile all input source files with a
# single compile and link operation.  This is most optimal when
# building all simulators or just a single simulator which you
# merely plan to run.  If you're developing new code for a
# simulator, it is more efficient to compile each source module
# into it's own object and then to link all the objects into the
# simulator binary.  This allows only the changed modules to be
# compiled instead of all of the input files resulting in much
# quicker builds during active simulator development.  GNU make
# can be invoked with BUILD_SEPARATE=1 on the command line (or
# defined as an exported environment variable) and separate
# objects will be built and linked into the resulting simulator.
#
# The default make output will show the details of each compile
# and link command executed.  GNU make can be invoked with QUIET=1
# on the command line (or defined as an exported environment
# variable) a summary of the executed command will be displayed
# in the make output.
#
# Default test execution will produce summary output.  Detailed
# test output can be produced if GNU make is invoked with
# TEST_ARG=-v on the command line.
#
# simh project support is provided for simulators that are built with
# dependent packages provided with the or by the operating system
# distribution OR for platforms where that isn't directly available
# (OS X/macOS) by packages from specific package management systems
# (HomeBrew or MacPorts).  Users wanting to build simulators with locally
# built dependent packages or packages provided by an unsupported package
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
# an alternate to gcc.  If you want to specifically build with the
# clang compiler, you should invoke make with GCC=clang on the make command
# line.
#
# Internal ROM support can be disabled if GNU make is invoked with
# DONT_USE_ROMS=1 on the command line.
#
# For linting (or other code analyzers) make may be invoked similar to:
#
#   make GCC=cppcheck CC_OUTSPEC= LDFLAGS= CFLAGS_G="--enable=all --template=gcc" CC_STD=--std=c99
#
ifeq (0,$(MAKELEVEL))	# recursive individual target build logic is end of this makefile
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
ifneq ($(findstring Windows,${OS}),)
  $(info *** Warning *** Compiling simh simulators with MinGW or cygwin is deprecated and)
  $(info *** Warning *** may not complete successfully or produce working simulators.  If)
  $(info *** Warning *** building simulators completes, they may not be fully functional.)
  $(info *** Warning *** It is recommended to use one of the free Microsoft Visual Studio)
  $(info *** Warning *** compilers which provide fully functional simulator capabilities.)
  ifeq ($(findstring .exe,${SHELL}),.exe)
    # MinGW
    export WIN32 := 1
    # Tests don't run under MinGW
    export TESTS := 0
    export RM = del /f /q
    export MKDIR = mkdir
    export OSTYPE = MinGW
    ifneq (,$(strip $(shell $(MAKE) --version 2>NUL)))
      GNUMake=$(strip $(shell $(MAKE) --version | findstr /C:"GNU Make"))
      export GNUMakeVERSION=$(strip $(shell for /F "tokens=3" %%i in ("$(GNUMake)") do echo %%i))
    else
      # Nothing useful returned from MinGW GNU Make 3.82.90, make sure to set this version
      export GNUMakeVERSION=3.82.90
    endif
  else # Msys or cygwin
    ifeq (MINGW,$(findstring MINGW,$(shell uname)))
      $(info *** This makefile can not be used with the Msys bash shell)
      $(error Use build_mingw.bat ${MAKECMDGOALS} from a Windows command prompt)
    endif
  endif
else
  export GNUMakeVERSION = $(shell ($(MAKE) --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ print $$3 }'))
  ifeq (old,$(shell $(MAKE) --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ if ($$3 < "3.81") {print "old"} }'))
    $(warning *** Warning *** GNU Make Version $(GNUMakeVERSION) is too old to)
    $(warning *** Warning *** fully process this makefile)
  endif
  export MKDIR = mkdir -p
  export OSTYPE = $(shell uname)
endif
ifeq ($(WIN32),)
  SIM_MAJOR=$(shell grep SIM_MAJOR sim_rev.h | awk '{ print $$3 }')
else
  SIM_MAJOR=$(shell for /F "tokens=3" %%i in ('findstr /c:"SIM_MAJOR" sim_rev.h') do echo %%i)
endif
# Assure that only BUILD_SEPARATE=1 will cause separate compiles
ifeq (,$(BUILD_SEPARATE))
  override BUILD_SEPARATE=
endif
export BUILD_SEPARATE
ifeq (,$(QUIET))
  override QUIET=
endif
export QUIET
BUILD_SINGLE := ${MAKECMDGOALS} $(BLANK_SUFFIX)
BUILD_MULTIPLE_VERB = is
MAKECMDGOALS_DESCRIPTION = the $(MAKECMDGOALS) simulator
# building the pdp1, pdp11, tx-0, or any microvax simulator could use video support
ifneq (3,${SIM_MAJOR})
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
  # building the AltairZ80 could use video support
  ifneq (,$(findstring altairz80,${MAKECMDGOALS}))
    VIDEO_USEFUL = true
  endif
endif
# building the SEL32 networking can be used
ifneq (,$(findstring sel32,${MAKECMDGOALS}))
  NETWORK_USEFUL = true
endif
# building the PDP-7 needs video support
ifneq (,$(findstring pdp7,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
endif
# building the pdp11, any pdp10, any 3b2, or any vax simulator could use networking support
ifneq (,$(findstring pdp11,${MAKECMDGOALS})$(findstring pdp10,${MAKECMDGOALS})$(findstring vax,${MAKECMDGOALS})$(findstring frontpaneltest,${MAKECMDGOALS})$(findstring infoserver,${MAKECMDGOALS})$(findstring 3b2,${MAKECMDGOALS})$(findstring all,${MAKECMDGOALS}))
  NETWORK_USEFUL = true
  ifneq (,$(findstring all,${MAKECMDGOALS}))
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
    VIDEO_USEFUL = true
    BESM6_BUILD = true
    MAKECMDGOALS_DESCRIPTION = everything
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
    MAKECMDGOALS_DESCRIPTION = everything
  endif
endif
ifneq (,$(and $(word 1,${MAKECMDGOALS}),$(word 2,${MAKECMDGOALS})))
  BUILD_MULTIPLE = s
  BUILD_MULTIPLE_VERB = are
  MAKECMDGOALS_DESCRIPTION = the $(MAKECMDGOALS) simulators
endif
# someone may want to explicitly build simulators without network support
ifneq ($(NONETWORK),)
  NETWORK_USEFUL =
endif
# ... or without video support
ifneq ($(NOVIDEO),)
  VIDEO_USEFUL =
endif

find_exe = $(abspath $(strip $(firstword $(foreach dir,$(strip $(subst :, ,${PATH})),$(wildcard $(dir)/$(1))))))
find_lib = $(firstword $(abspath $(strip $(firstword $(foreach dir,$(strip ${LIBPATH}),$(foreach ext,$(strip ${LIBEXT}),$(wildcard $(dir)/lib$(1).$(ext))))))))
find_include = $(abspath $(strip $(firstword $(foreach dir,$(strip ${INCPATH}),$(wildcard $(dir)/$(1).h)))))
ifeq (Darwin,$(OSTYPE))
  eq = $(if $(or $(1),$(2)),$(and $(findstring $(1),$(2)),$(findstring $(2),$(1))),1)
  ifneq (,$(or $(call eq,/usr/local/bin/brew,$(call find_exe,brew)),$(call eq,/opt/homebrew/bin/brew,$(call find_exe,brew))))
    PKG_MGR = HOMEBREW
    PKG_CMD = brew install
  else
    ifeq (/opt/local/bin/port,$(call find_exe,port))
      PKG_MGR = MACPORTS
      PKG_CMD = port install
    endif
  endif
endif
ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
  ifneq (Android,$(shell uname -o))
    PKG_MGR = APT
    PKG_CMD = apt-get install
  else
    PKG_MGR = TERMUX
    PKG_CMD = pkg install
    PKG_NO_SUDO = YES
  endif
endif
ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,yum)))
  PKG_MGR = YUM
  PKG_CMD = yum install
endif
ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apk)))
  PKG_MGR = APK
  PKG_CMD = apk add
  PKG_NO_SUDO = YES
endif
ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,dnf)))
  PKG_MGR = DNF
  ifneq (,$(shell dnf repolist | grep crb))
    PKG_CMD = dnf --enablerepo=crb install
  else
    PKG_CMD = dnf install
  endif
endif
ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,zypper)))
  PKG_MGR = ZYPPER
  PKG_CMD = zypper install
endif
ifneq (,$(and $(findstring NetBSD,$(OSTYPE)),$(call find_exe,pkgin)))
  PKG_MGR = PKGSRC
  PKG_CMD = pkgin install
  PKG_NO_SUDO = YES
endif
ifneq (,$(and $(findstring FreeBSD,$(OSTYPE)),$(call find_exe,pkg)))
  PKG_MGR = PKGBSD
  PKG_CMD = pkg install
  PKG_NO_SUDO = YES
endif
ifneq (,$(and $(findstring OpenBSD,$(OSTYPE)),$(call find_exe,pkg_add)))
  PKG_MGR = PKGADD
  PKG_CMD = pkg_add
  PKG_NO_SUDO = YES
  PKG_SHELL_READ_CANT_PROMPT = YES
endif
# Dependent packages
DPKG_COMPILER  = 1
DPKG_PCAP      = 2
DPKG_VDE       = 3
DPKG_PCRE      = 4
DPKG_EDITLINE  = 5
DPKG_SDL       = 6
DPKG_PNG       = 7
DPKG_ZLIB      = 8
DPKG_SDL_TTF   = 9
DPKG_GMAKE     = 10
DPKG_CURL      = 11
ifneq (3,${SIM_MAJOR})
  # Platform Pkg Names  COMPILER PCAP          VDE            PCRE         EDITLINE      SDL               PNG            ZLIB       SDL_TTF           GMAKE CURL
  PKGS_SRC_HOMEBREW   = -        -             vde            pcre         libedit       sdl2              libpng         zlib       sdl2_ttf          make  -
  PKGS_SRC_MACPORTS   = -        -             vde2           pcre         libedit       libsdl2           libpng         zlib       libsdl2_ttf       gmake -
  PKGS_SRC_APT        = gcc      libpcap-dev   libvdeplug-dev libpcre3-dev libedit-dev   libsdl2-dev       libpng-dev     -          libsdl2-ttf-dev   -     curl
  PKGS_SRC_YUM        = gcc      libpcap-devel -              pcre-devel   libedit-devel SDL2-devel        libpng-devel   zlib-devel SDL2_ttf-devel    -     -
  PKGS_SRC_DNF        = gcc      libpcap-devel -              pcre-devel   libedit-devel SDL2-devel        libpng-devel   zlib-devel SDL2_ttf-devel    -     -
  PKGS_SRC_ZYPPER     = gcc      libpcap-devel -              -            libedit-devel sdl2-compat-devel libpng16-devel zlib-devel SDL2_ttf-devel    make  -
  PKGS_SRC_APK        = clang    libpcap-devel -              -            libedit-devel sdl2-compat-devel libpng-devel   -          sdl2_ttf-devel    gmake curl
  PKGS_SRC_PKGSRC     = -        -             -              pcre         editline      SDL2              png            zlib       SDL2_ttf          gmake -
  PKGS_SRC_PKGBSD     = -        -             -              pcre         libedit       sdl2              png            -          sdl2_ttf          gmake -
  PKGS_SRC_PKGADD     = -        -             -              pcre         -             sdl2              png            -          sdl2-ttf          gmake -
  PKGS_SRC_TERMUX     = clang    libpcap       -              pcre         -             -                 -              -          -                 -     curl
  ifneq (0,$(TESTS))
    ifneq (,${TEST_ARG})
      export TEST_ARG
      TESTING_FEATURES = - Per simulator tests will be run with argument: ${TEST_ARG}
    else
      TESTING_FEATURES = - Per simulator tests will be run
    endif
  else
    TESTING_FEATURES = - Per simulator tests will be skipped
  endif
else
  # simh v3 has minimal external dependencies
  # Platform Pkg Names  COMPILER PCAP          VDE            PCRE         EDITLINE      SDL         PNG          ZLIB       SDL_TTF   GMAKE
  PKGS_SRC_HOMEBREW   = -        -             vde            -            libedit       -           -            -          -         -
  PKGS_SRC_MACPORTS   = -        -             vde2           -            libedit       -           -            -          -         -
  PKGS_SRC_APT        = gcc      libpcap-dev   libvdeplug-dev -            libedit-dev   -           -            -          -         -
  PKGS_SRC_YUM        = gcc      libpcap-devel -              -            libedit-devel -           -            -          -         -
  PKGS_SRC_DNF        = gcc      libpcap-devel -              -            libedit-devel -           -            -          -         -
  PKGS_SRC_PKGSRC     = -        -             -              -            editline      -           -            -          -         -
  PKGS_SRC_PKGBSD     = -        -             -              -            libedit       -           -            -          -         -
  PKGS_SRC_PKGADD     = -        -             -              -            -             -           -            -          -         -
endif
ifeq (${WIN32},)  #*nix Environments (&& cygwin)
  # OSNAME is used in messages to indicate the source of libpcap components
  OSNAME = $(OSTYPE)
  ifeq (SunOS,$(OSTYPE))
    export TEST = /bin/test
  else
    export TEST = test
  endif
  RUNNING_AS_ROOT:=$(if $(findstring Darwin,$(OSTYPE)),$(shell if [ `id -u` == '0' ]; then echo running_as_root; fi),$(shell if $(TEST) -r /dev/mem; then echo running_as_root; fi))
  CAN_AUTO_INSTALL_PACKAGES:=$(findstring HOMEBREW,$(PKG_MGR)),$(RUNNING_AS_ROOT)
  override AUTO_INSTALL_PACKAGES:=$(and $(AUTO_INSTALL_PACKAGES),$(or $(findstring HOMEBREW,$(PKG_MGR)),$(RUNNING_AS_ROOT)))
  ifeq (${GCC},)
    ifeq (,$(call find_exe,gcc))
      ifneq (clang,$(findstring clang,$(and $(call find_exe,cc),$(shell cc -v /dev/null 2>&1 | grep 'clang'))))
        $(info *** Warning *** Using local cc since gcc isn't available locally.)
        $(info *** Warning *** You may need to install gcc to build working simulators.)
        NEEDED_PKGS += DPKG_COMPILER
      endif
      GCC = cc
    else
      GCC = gcc
    endif
  endif
  ifeq (CYGWIN,$(findstring CYGWIN,$(OSTYPE))) # uname returns CYGWIN_NT-n.n-ver
    OSTYPE = cygwin
    OSNAME = windows-build
  endif
  ifeq (Darwin,$(OSTYPE))
    ifeq (,$(call find_exe,port)$(call find_exe,brew))
      $(info *** Info *** simh dependent packages on macOS must be provided by either the)
      $(info *** Info *** HomeBrew package system or by the MacPorts package system.)
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
  export LANG = en_US.UTF-8
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
      OS_CCDEFS += $(if $(findstring ALLOWED,$(WARNINGS)),,-Werror)
      ifeq (,$(findstring ++,${GCC}))
        CC_STD = -std=gnu99
      else
        export CPP_BUILD = 1
      endif
    endif
  else
    OS_CCDEFS += $(if $(findstring ALLOWED,$(WARNINGS)),,-Werror)
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
      export CPP_BUILD = 1
      OS_CCDEFS += -Wno-deprecated
    endif
  endif
  ifeq (git-repo,$(shell if ${TEST} -e ./.git; then echo git-repo; fi))
    GIT_REPO=1
    GIT_PATH=$(strip $(call find_exe,git))
    ifeq (,$(GIT_PATH))
      $(error building using a git repository, but git is not available)
    endif
  endif
  ifeq (got-repo,$(shell if ${TEST} -e ./.got; then echo got-repo; fi))
    GIT_PATH=$(strip $(call find_exe,git))
    ifeq (,$(GIT_PATH))
      $(error building using a got repository, but git is not available)
    endif
    ifeq (,$(file <.got/repository))
      $(error building using a got repository, but git repository is not available)
    endif
    REPO_PATH=-C $(file <.got/repository)
    GIT_REPO=1
  endif
  ifneq (,$(and $(GIT_REPO),$(GIT_PATH)))
    ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
      CURRENT_FULL_GIT_COMMIT_ID=$(strip $(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }'))
      CURRENT_GIT_COMMIT_ID=$(word 1,$(subst +, , $(CURRENT_FULL_GIT_COMMIT_ID)))
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git $(REPO_PATH) log -1 --pretty="%H"))
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        NEED_COMMIT_ID = need-commit-id$(shell touch scp.c)
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell $(RM) .git/hooks/post-checkout .git/hooks/post-commit .git/hooks/post-merge)
      endif
    else
      NEED_COMMIT_ID = need-commit-id$(shell touch scp.c)
    endif
    ifneq (,$(if $(REPO_PATH),$(shell got status -S ?),$(shell git update-index --refresh --)))
      ifeq (,$(findstring +uncommitted-changes,$(CURRENT_FULL_GIT_COMMIT_ID)))
        GIT_EXTRA_FILES=+uncommitted-changes$(shell touch scp.c)
      else
        GIT_EXTRA_FILES=+uncommitted-changes
      endif
    endif
    ifneq (,$(or $(NEED_COMMIT_ID),$(GIT_EXTRA_FILES)))
      isodate=$(shell git $(REPO_PATH) log -1 --pretty="%ai"|sed -e 's/ /T/'|sed -e 's/ //')
      $(shell git $(REPO_PATH) log -1 --pretty="SIM_GIT_COMMIT_ID %H$(GIT_EXTRA_FILES)%nSIM_GIT_COMMIT_TIME $(isodate)" >.git-commit-id)
    endif
  endif
  SIM_BUILD_OS_VERSION= -DSIM_BUILD_OS_VERSION="$(shell uname -a|sed 's/,//g')"
  LTO_EXCLUDE_VERSIONS =
  PCAPLIB = pcap
  ifeq (agcc,$(findstring agcc,${GCC})) # Android target build?
    OS_CCDEFS += -D_GNU_SOURCE -DSIM_ASYNCH_IO 
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
        INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | awk '{ print $$1 }' | tr '\n' ' ')
      endif
      ifeq (incopt,$(shell if ${TEST} -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libopt,$(shell if ${TEST} -d /opt/local/lib; then echo libopt; fi))
        LIBPATH += /opt/local/lib
        OS_LDFLAGS += -L/opt/local/lib
      endif
      ifeq (HomeBrew,$(or $(shell if ${TEST} -d /usr/local/Cellar; then echo HomeBrew; fi),$(shell if ${TEST} -d /opt/homebrew/Cellar; then echo HomeBrew; fi)))
        ifeq (local,$(shell if $(TEST) -d /usr/local/Cellar; then echo local; fi))
          HBPATH = /usr/local
        else
          HBPATH = /opt/homebrew
        endif
        INCPATH += $(foreach dir,$(wildcard $(HBPATH)/Cellar/*/*),$(realpath $(dir)/include))
        LIBPATH += $(foreach dir,$(wildcard $(HBPATH)/Cellar/*/*),$(realpath $(dir)/lib))
      endif
    else
      ifeq (Linux,$(OSTYPE))
        ifeq (Android,$(shell uname -o))
          ANDROID_API=$(shell getprop ro.build.version.sdk)
          ANDROID_VERSION=$(shell getprop ro.build.version.release)
          OS_CCDEFS += -DSIM_BUILD_OS=" On Android Version $(ANDROID_VERSION) sdk=$(ANDROID_API)"
          ifeq (,$(shell clang sim_BuildROMs.c -o /dev/null -D__ANDROID_API__=$(ANDROID_API) 2>&1))
            OS_CCDEFS += -D__ANDROID_API__=$(ANDROID_API)
          endif
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
        LIBSOEXT = so
        LIBEXT = $(LIBSOEXT) a
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
                      $(info *** Warning *** The library search path on your $(OSTYPE) platform can not be)
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
                override LTO =
              else
                LIBEXT = a
              endif
            endif
          endif
        endif
      endif
    endif
    ifeq (,$(and $(findstring -D_LARGEFILE64_SOURCE,$(OS_CCDEFS)),$(shell grep _LARGEFILE64_SOURCE $(call find_include,pthread))))
      OS_CCDEFS += -D_LARGEFILE64_SOURCE
    endif
    ifeq (,$(LIBSOEXT))
      LIBSOEXT = $(LIBEXT)
    endif
    ifeq (,$(filter /lib/,$(LIBPATH)))
      ifeq (existlib,$(shell if $(TEST) -d /lib/; then echo existlib; fi))
        LIBPATH += /lib/
      endif
    endif
    ifeq (,$(filter /usr/lib/,$(LIBPATH)))
      ifeq (existusrlib,$(shell if $(TEST) -d /usr/lib/; then echo existusrlib; fi))
        LIBPATH += /usr/lib/
      endif
    endif
    export CPATH = $(subst $() $(),:,$(INCPATH))
    export LIBRARY_PATH = $(subst $() $(),:,$(LIBPATH))
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
      PTHREAD_CCDEFS += -DSIM_ASYNCH_IO
      PTHREAD_LDFLAGS += -lpthread
      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
    else
      LIBEXTSAVE := ${LIBEXT}
      LIBEXT = a
      ifneq (,$(call find_lib,pthread))
        PTHREAD_CCDEFS += -DSIM_ASYNCH_IO
        PTHREAD_LDFLAGS += -lpthread
        $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
      else
        ifneq (,$(findstring Haiku,$(OSTYPE)))
          PTHREAD_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO
          $(info using libpthread: $(call find_include,pthread))
        else
          ifeq (Darwin,$(OSTYPE))
            PTHREAD_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO
            PTHREAD_LDFLAGS += -lpthread
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
      $(info using libpcre: $(call find_lib,pcre) $(call find_include,pcre))
      ifneq (,$(ALL_DEPENDENCIES))
        OS_CCDEFS += -DHAVE_PCRE_H
        OS_LDFLAGS += -lpcre
        ifeq ($(LD_SEARCH_NEEDED),$(call need_search,pcre))
          OS_LDFLAGS += -L$(dir $(call find_lib,pcre))
        endif
      endif
    else
      NEEDED_PKGS += DPKG_PCRE
    endif
  else
    NEEDED_PKGS += DPKG_PCRE
  endif
  # Find libedit BSD licensed library for readline support.
  ifneq (,$(call find_lib,edit))
    ifneq (,$(call find_include,editline/readline))
      $(info using libedit: $(call find_lib,edit) $(call find_include,editline/readline))
      ifneq (,$(ALL_DEPENDENCIES))
        OS_CCDEFS += -DHAVE_LIBEDIT
        OS_LDFLAGS += -ledit
        ifneq (,$(call find_lib,termcap))
          OS_LDFLAGS += -ltermcap
        endif
        ifeq ($(LD_SEARCH_NEEDED),$(call need_search,edit))
          OS_LDFLAGS += -L$(dir $(call find_lib,edit))
        endif
      endif
    else
      NEEDED_PKGS += DPKG_EDITLINE
    endif
  else
    NEEDED_PKGS += DPKG_EDITLINE
  endif
  # The recursive logic needs a GNU make at least v4 when building with 
  # separate compiles
  ifneq (,$(call find_exe,gmake))
    override MAKE = $(call find_exe,gmake)
  endif
  ifneq (,$(and $(findstring 3.,$(GNUMakeVERSION)),$(BUILD_SEPARATE)))
    NEEDED_PKGS += DPKG_GMAKE
  endif
  ifeq (,$(call find_exe,curl))
    $(info *** Info ***)
    $(info *** Info *** The SCP curl command needs the curl package installed.)
    $(info *** Info *** Normal simulator execution doesn't require curl, but user)
    $(info *** Info *** scripts may want it available.)
    $(info *** Info ***)
    OPTIONAL_PKGS += DPKG_CURL
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
      OS_CCDEFS += -DSIM_HAVE_DLOPEN=$(LIBSOEXT)
      OS_LDFLAGS += -ldl
      $(info using libdl: $(call find_lib,dl) $(call find_include,dlfcn))
    else
      ifneq (,$(findstring BSD,$(OSTYPE))$(findstring AIX,$(OSTYPE))$(findstring Haiku,$(OSTYPE)))
        OS_CCDEFS += -DSIM_HAVE_DLOPEN=so
        $(info using libdl: $(call find_include,dlfcn))
      else
        ifneq (,$(call find_lib,dld))
          OS_CCDEFS += -DSIM_HAVE_DLOPEN=$(LIBSOEXT)
          OS_LDFLAGS += -ldld
          $(info using libdld: $(call find_lib,dld) $(call find_include,dlfcn))
        else
          ifeq (Darwin,$(OSTYPE))
            OS_CCDEFS += -DSIM_HAVE_DLOPEN=dylib
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
      $(info using libpng: $(call find_lib,png) $(call find_include,png))
      PNG_CCDEFS += -DHAVE_LIBPNG
      ifneq (,$(ALL_DEPENDENCIES))
        PNG_LDFLAGS += -lpng
      endif
      ifneq (,$(call find_include,zlib))
        ifneq (,$(call find_lib,z))
          $(info using zlib: $(call find_lib,z) $(call find_include,zlib))
          PNG_CCDEFS += -DHAVE_ZLIB
          ifneq (,$(ALL_DEPENDENCIES))
            PNG_LDFLAGS += -lz
          endif
        else
          NEEDED_PKGS += DPKG_ZLIB
        endif
      else
        NEEDED_PKGS += DPKG_ZLIB
      endif
    else
      # some systems may name the png library libpng16
      ifneq (,$(call find_lib,png16))
        PNG_CCDEFS += -DHAVE_LIBPNG
        PNG_LDFLAGS += -lpng16
        $(info using libpng: $(call find_lib,png16) $(call find_include,png))
        ifneq (,$(call find_include,zlib))
          ifneq (,$(call find_lib,z))
            PNG_CCDEFS += -DHAVE_ZLIB
            PNG_LDFLAGS += -lz
            $(info using zlib: $(call find_lib,z) $(call find_include,zlib))
          else
            NEEDED_PKGS += DPKG_ZLIB
          endif
        endif
      else
        NEEDED_PKGS += DPKG_PNG
      endif
    endif
  else
    NEEDED_PKGS += DPKG_PNG
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
        ifneq (,$(call find_exe,sdl2-config))
          SDLX_CONFIG = sdl2-config
        endif
        ifneq (,$(SDLX_CONFIG))
          VIDEO_CCDEFS += -DHAVE_LIBSDL `$(SDLX_CONFIG) --cflags` $(PNG_CCDEFS)
          VIDEO_LDFLAGS += `$(SDLX_CONFIG) --libs` $(PNG_LDFLAGS)
          VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
          DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
          DISPLAYVT = ${DISPLAYD}/vt11.c
          DISPLAY340 = ${DISPLAYD}/type340.c
          DISPLAYNG = ${DISPLAYD}/ng.c
          DISPLAYIII = ${DISPLAYD}/iii.c
          DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) -DUSE_SIM_VIDEO
          $(info using libSDL2: $(call find_include,SDL2/SDL))
        endif
      else
        NEEDED_PKGS += DPKG_SDL
      endif
    else
      NEEDED_PKGS += DPKG_SDL
    endif
    ifneq (,$(BESM6_BUILD))
      ifneq (,$(and $(findstring sdl2,${VIDEO_LDFLAGS}),$(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf)))
        $(info using libSDL2_ttf: $(call find_lib,SDL2_ttf) $(call find_include,SDL2/SDL_ttf))
        $(info ***)
        VIDEO_TTF_OPT = $(VIDEO_CCDEFS) -DHAVE_LIBSDL_TTF
        VIDEO_TTF_LDFLAGS += -lSDL2_ttf
        VIDEO_FEATURES += with TrueType font support
        # Retain support for explicitly supplying a preferred fontfile
        ifneq (,$(FONTFILE))
          VIDEO_TTF_OPT +=  -DFONTFILE=${FONTFILE}
        endif
      else
        NEEDED_PKGS += DPKG_SDL_TTF
      endif
      ifneq (,$(and $(VIDEO_CCDEFS),$(PTHREAD_CCDEFS)))
        VIDEO_CCDEFS += $(PTHREAD_CCDEFS)
        VIDEO_LDFLAGS += $(PTHREAD_LDFLAGS)
      endif
    endif
  endif
  ifneq (,$(NETWORK_USEFUL))
    ifeq (Darwin,$(OSTYPE)) # the macOS vmnet framework is only useful when the OS is 10.15 or later
      ifeq (,$(findstring clang,$(COMPILER_NAME))) #only clang can use vmnet APIs
        DONT_USE_VMNET = DONT_USE_VMNET
      endif
      ifeq (,$(DONT_USE_VMNET))
        macOSMajor = $(strip $(shell sw_vers 2>/dev/null | grep 'ProductVersion:' 2>/dev/null | awk '{ print $$2 }' | awk -F . '{ print $$1 }'))
        macOSMinor = $(strip $(shell sw_vers 2>/dev/null | grep 'ProductVersion:' 2>/dev/null | awk '{ print $$2 }' | awk -F . '{ print $$2 }'))
        ifeq (10,$(macOSMajor))
          DONT_USE_VMNET = $(shell if ${TEST} $(macOSMinor) -lt 15; then echo DONT_USE_VMNET; fi)
        else
          DONT_USE_VMNET = $(shell if ${TEST} $(macOSMajor) -lt 10; then echo DONT_USE_VMNET; fi)
        endif
        ifeq (,$(DONT_USE_VMNET)$(DONT_USE_VMNET_HOST))
          DONT_USE_VMNET_HOST = $(shell if ${TEST} $(macOSMajor) -lt 11; then echo DONT_USE_VMNET_HOST; fi)
        endif
      endif
    endif
    ifneq (,$(if $(DONT_USE_VMNET),,$(call find_include,vmnet.framework/Headers/vmnet)))
      # sim_ether reduces network features to the appropriate minimal set
      NETWORK_LAN_FEATURES += VMNET
      NETWORK_CCDEFS += -DUSE_SHARED -I slirp -I slirp_glue -I slirp_glue/qemu -DHAVE_VMNET_NETWORK
      ifneq (,$(DONT_USE_VMNET_HOST))
        NETWORK_CCDEFS += -DDONT_USE_VMNET_HOST
      endif
      NETWORK_DEPS += slirp/*.c slirp_glue/*.c
      NETWORK_LDFLAGS += -framework vmnet
      $(info using vmnet: $(call find_include,vmnet.framework/Headers/vmnet))
    else
      # Consider other network connections
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
              $(info *** Warning *** Directly linking against libpcap is provides no measurable)
              $(info *** Warning *** benefits over dynamically linking libpcap.)
              $(info *** Warning ***)
              $(info *** Warning *** Support for linking this way is currently deprecated and may be removed)
              $(info *** Warning *** in the future.)
              $(info *** Warning ***)
            else
              $(info *** Error ***)
              $(info *** Error *** Directly linking against libpcap is provides no measurable)
              $(info *** Error *** benefits over dynamically linking libpcap.)
              $(info *** Error ***)
              $(info *** Error *** Support for linking directly has been removed on the $(OSTYPE))
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
            NETWORK_CCDEFS += 
            NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
            $(info using macOS dynamic libpcap: $(call find_include,pcap))
          endif
        endif
      else # pcap desired but pcap.h not found
        ifneq (,$(call find_lib,$(PCAPLIB)))
          PCAP_LIB_VERSION = $(shell strings $(call find_lib,$(PCAPLIB)) | grep 'libpcap version' | awk '{ print $$3}')
          PCAP_LIB_BASE_VERSION = $(firstword $(subst ., ,$(PCAP_LIB_VERSION)))
        endif
        NEEDED_PKGS += DPKG_PCAP
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
          ifeq (1,$(PCAP_LIB_BASE_VERSION))
            $(info using libpcap $(PCAP_LIB_VERSION) without an available pcap.h)
            NETWORK_CCDEFS = -DUSE_SHARED -DHAVE_PCAP_NETWORK -DPCAP_LIB_VERSION=$(PCAP_LIB_VERSION)
            NETWORK_FEATURES = - dynamic networking support using libpcap components from www.tcpdump.org and locally installed libpcap.${LIBEXT}
            NETWORK_LAN_FEATURES += PCAP
            NEEDED_PKGS := $(filter-out DPKG_PCAP,$(NEEDED_PKGS))
          else
            $(info *** Warning ***)
            $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built WITHOUT)
            $(info *** Warning *** libpcap networking support)
            $(info *** Warning ***)
            $(info *** Warning *** To build simulator(s) with libpcap networking support you)
            $(info *** Warning *** should install the libpcap development components for)
            $(info *** Warning *** for your $(OSNAME) system.)
            ifeq (,$(or $(findstring Linux,$(OSTYPE)),$(findstring OSX,$(OSNAME))))
              $(info *** Warning *** You should read 0readme_ethernet.txt and follow the instructions)
              $(info *** Warning *** regarding the needed libpcap development components for your)
              $(info *** Warning *** $(OSNAME) platform.)
            endif
            $(info *** Warning ***)
          endif
        endif
      endif
      ifneq (,$(call find_lib,vdeplug))
        # libvdeplug requires the use of the OS provided libpcap
        ifeq (,$(findstring usr/local,$(NETWORK_CCDEFS)))
          ifneq (,$(call find_include,libvdeplug))
            # Provide support for vde networking
            NETWORK_CCDEFS += -DHAVE_VDE_NETWORK
            NETWORK_LAN_FEATURES += VDE
            ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
              NETWORK_CCDEFS += -DUSE_SHARED
            endif
            ifeq (Darwin,$(OSTYPE))
              NETWORK_LDFLAGS += -lvdeplug -L$(dir $(call find_lib,vdeplug))
            else
              NETWORK_LDFLAGS += -lvdeplug -Wl,-R,$(dir $(call find_lib,vdeplug)) -L$(dir $(call find_lib,vdeplug))
            endif
            $(info using libvdeplug: $(call find_lib,vdeplug) $(call find_include,libvdeplug))
          endif
        else
          ifeq (,$(findstring PKG_PCAP, $(NEEDED_PKGS)))
            NEEDED_PKGS += DPKG_PCAP
          endif
        endif
      else
        NEEDED_PKGS += DPKG_VDE
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
    endif
    ifeq (slirp,$(shell if ${TEST} -e slirp_glue/sim_slirp.c; then echo slirp; fi))
      NETWORK_CCDEFS += -I slirp -I slirp_glue -I slirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG 
      NETWORK_DEPS += slirp/*.c slirp_glue/*.c
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
    ifneq (,$(and $(NETWORK_CCDEFS),$(PTHREAD_CCDEFS)))
      NETWORK_CCDEFS += -DUSE_READER_THREAD $(PTHREAD_CCDEFS)
      NETWORK_LDFLAGS += $(PTHREAD_LDFLAGS)
    endif
    NETWORK_OPT = $(NETWORK_CCDEFS)
  endif
  ifneq (binexists,$(shell if ${TEST} -e BIN/buildtools; then echo binexists; fi))
    export MKDIRBIN
    MKDIRBIN = @$(MKDIR) BIN/buildtools
  endif
  ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
    GIT_COMMIT_ID=$(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }')
    GIT_COMMIT_TIME=$(shell grep 'SIM_GIT_COMMIT_TIME' .git-commit-id | awk '{ print $$2 }')
  else
    ifeq (,$(shell grep 'define SIM_ARCHIVE_GIT_COMMIT_ID' sim_rev.h | grep 'Format:'))
      GIT_COMMIT_ID=$(shell grep 'define SIM_ARCHIVE_GIT_COMMIT_ID' sim_rev.h | awk '{ print $$3 }')
      GIT_COMMIT_TIME=$(shell grep 'define SIM_ARCHIVE_GIT_COMMIT_TIME' sim_rev.h | awk '{ print $$3 }')
      GIT_ARCHIVE_COMMIT_ID=$(empty) $(empty)archive
     else
      ifeq (git-submodule,$(if $(shell cd .. ; git rev-parse --git-dir 2>/dev/null),git-submodule))
        GIT_COMMIT_ID=$(shell cd .. ; git submodule status | grep " $(notdir $(realpath .)) " | awk '{ print $$1 }')
        GIT_COMMIT_TIME=$(shell git $(REPO_PATH) --git-dir=$(realpath .)/.git log $(GIT_COMMIT_ID) -1 --pretty="%aI")
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
    export CPP_BUILD = 1
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
      DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS)
    else
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This build could produce simulators with video capabilities.     **)
      $(info **  However, the required files to achieve this can not be found on  **)
      $(info **  on this system.  Download the file:                              **)
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
  ifeq (,$(findstring clean,${MAKECMDGOALS}))
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
      CURRENT_FULL_GIT_COMMIT_ID=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_ID" .git-commit-id)") do echo %%i)
      CURRENT_GIT_COMMIT_ID=$(word 1,$(subst +, , $(CURRENT_FULL_GIT_COMMIT_ID)))
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell $(REPO_PATH) git log -1 --pretty=%H))
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        ifeq (,$(strip $(findstring scp.c,$(shell git $(REPO_PATH) diff --name-only))))
          # scp.c hasn't changed, so we want to touch it to force it to recompile
          # but touch isn't part of MinGW, so we do some git monkey business
          NEED_COMMIT_ID = need-commit-id$(file >> scp.c,)$(shell git $(REPO_PATH) restore scp.c)
        else
          NEED_COMMIT_ID = need-commit-id
        endif
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell if exist .git\hooks\post-checkout del .git\hooks\post-checkout)
        $(shell if exist .git\hooks\post-commit   del .git\hooks\post-commit)
        $(shell if exist .git\hooks\post-merge    del .git\hooks\post-merge)
      endif
    else
      ifeq (,$(strip $(findstring scp.c,$(shell git $(REPO_PATH) diff --name-only))))
        NEED_COMMIT_ID = need-commit-id$(file >> scp.c,)$(shell git $(REPO_PATH) restore scp.c)
      else
        NEED_COMMIT_ID = need-commit-id
      endif
    endif
    ifneq (,$(shell git $(REPO_PATH) update-index --refresh --))
      ifeq (,$(findstring +uncommitted-changes,$(CURRENT_FULL_GIT_COMMIT_ID)))
        ifeq (,$(strip $(findstring scp.c,$(shell git $(REPO_PATH) diff --name-only))))
          GIT_EXTRA_FILES=+uncommitted-changes$(file >> scp.c,)$(shell git $(REPO_PATH) restore scp.c)
        else
          GIT_EXTRA_FILES=+uncommitted-changes
        endif
      else
        GIT_EXTRA_FILES=+uncommitted-changes
      endif
    endif
    ifneq (,$(or $(NEED_COMMIT_ID),$(GIT_EXTRA_FILES)))
      isodatetime=$(shell git $(REPO_PATH) log -1 --pretty=%ai)
      isodate=$(word 1,$(isodatetime))T$(word 2,$(isodatetime))$(word 3,$(isodatetime))
      $(shell echo SIM_GIT_COMMIT_ID $(ACTUAL_GIT_COMMIT_ID)$(GIT_EXTRA_FILES)>.git-commit-id)
      $(shell echo SIM_GIT_COMMIT_TIME $(isodate)>>.git-commit-id)
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
      ifneq (3,${SIM_MAJOR})
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
    endif
  else
    # Version check on windows-build
    WINDOWS_BUILD = $(word 2,$(shell findstr WINDOWS-BUILD ..\windows-build\Windows-Build_Versions.txt))
    ifeq (,$(WINDOWS_BUILD))
      WINDOWS_BUILD = 00000000
    endif
    ifneq (,$(or $(shell if 20191001 GTR $(WINDOWS_BUILD) echo old-windows-build),$(and $(shell if 20171112 GTR $(WINDOWS_BUILD) echo old-windows-build),$(findstring pthreadGC2,$(PTHREADS_LDFLAGS)))))
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
        $(info **  date.  For the most functional and stable features you should    **)
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
      NETWORK_OPT += -I slirp -I slirp_glue -I slirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c -lIphlpapi
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
  endif
  ifneq (,$(call find_include,ddk/ntdddisk))
    CFLAGS_I = -DHAVE_NTDDDISK_H
  endif
endif # Win32 (via MinGW)
ifeq (clean,$(strip ${MAKECMDGOALS}))
  # a simple clean has no dependencies 
  NEEDED_PKGS =
endif
USEFUL_PACKAGES = $(filter-out -,$(foreach word,$(NEEDED_PKGS),$(word $($(word)),$(PKGS_SRC_$(strip $(PKG_MGR))))))
OPTIONAL_PACKAGES = $(filter-out -,$(foreach word,$(OPTIONAL_PKGS),$(word $($(word)),$(PKGS_SRC_$(strip $(PKG_MGR))))))
USEFUL_PLURAL =  $(if $(word 2,$(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES)),s,)
USEFUL_MULTIPLE_HIST = $(if $(word 2,$(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES)),were,was)
USEFUL_MULTIPLE = $(if $(word 2,$(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES)),these,this)
ifneq (,$(USEFUL_PACKAGES))
  $(info )
  $(info *** Info ***)
  $(info *** Info *** The simulator$(BUILD_MULTIPLE) you are building could provide more)
  $(info *** Info *** functionality if the:)
  $(info *** Info ***     $(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES))
  $(info *** Info *** package$(USEFUL_PLURAL) $(USEFUL_MULTIPLE_HIST) available on your system.)
  $(info )
  ifeq (,$(AUTO_INSTALL_PACKAGES))
    $(info *** You have the option of building $(MAKECMDGOALS_DESCRIPTION) without the)
    $(info *** functionality $(USEFUL_MULTIPLE) package$(USEFUL_PLURAL) provide$(if $(USEFUL_PLURAL),,s), or stopping now to install)
    $(info *** $(USEFUL_MULTIPLE) package$(USEFUL_PLURAL).)
    $(info )
  endif
endif
ifneq (,$(BUILD_SEPARATE))
  EXTRAS:=BUILD_SEPARATE=$(BUILD_SEPARATE)
endif
ifneq (,$(QUIET))
  EXTRAS+= QUIET=$(QUIET)
endif
ifneq (,$(and $(AUTO_INSTALL_PACKAGES),$(PKG_CMD),$(USEFUL_PACKAGES)))
  ifneq (,$(AUTO_INSTALL_PACKAGES))
    $(info Running $(word 1,$(PKG_CMD)) now to install $(USEFUL_MULTIPLE) package$(USEFUL_PLURAL) before building $(MAKECMDGOALS_DESCRIPTION)?)
  else
    $(info Do you want to install $(USEFUL_MULTIPLE) package$(USEFUL_PLURAL) before building $(MAKECMDGOALS_DESCRIPTION)?)
  endif
  ifeq (,$(if $(AUTO_INSTALL_PACKAGES),,$(shell $(SHELL) -c 'read -p "[Enter Y or N, Default is Y] " answer; echo $$answer' | grep -i n)))
    INSTALLER_RESULT = $(shell $(PKG_CMD) $(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES) 1>&2)
    $(info $(INSTALLER_RESULT))
    $(info *** rerunning this make to perform your desired build...)
    MAKE_RESULT = $(shell $(MAKE) $(MAKECMDGOALS) $(EXTRAS) 1>&2)
    $(error Done: $(MAKE_RESULT))
  endif
else
  ifneq (,$(USEFUL_PACKAGES))
    $(info Do you want to install $(USEFUL_MULTIPLE) package$(USEFUL_PLURAL) before building $(MAKECMDGOALS_DESCRIPTION)?)
    ifeq (,$(PKG_SHELL_READ_CANT_PROMPT))
      ANSWER := $(shell $(SHELL) -c 'read -p "[Enter Y or N, Default is Y] " answer; echo $$answer' | grep -i n)
    else
      $(info [Enter Y or N, Default is Y])
      ANSWER := $(shell $(SHELL) -c 'read answer; echo $$answer' | grep -i n)
    endif
    ifeq (,$(ANSWER))
      ANSWER := Y
    else
      ifeq (y,$(ANSWER))
        ANSWER := Y
      endif
    endif
    ifeq (Y,$(ANSWER))
      ifneq (,$(CAN_AUTO_INSTALL_PACKAGES))
        INSTALLER_RESULT = $(shell $(PKG_CMD) $(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES) 1>&2)
        $(info $(INSTALLER_RESULT))
        $(info *** rerunning this make to perform your desired build...)
        MAKE_RESULT = $(shell $(MAKE) $(MAKECMDGOALS) $(EXTRAS) 1>&2)
        $(error Done: $(MAKE_RESULT))
      endif
      ifeq (,$(PKG_NO_SUDO))
        $(info Enter:    $$ sudo $(PKG_CMD) $(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES))
        $(info when that completes)
        $(info re-enter: $$ $(MAKE) $(MAKECMDGOALS) $(EXTRAS))
        $(error )
      else
        hash := \#
        $(info Enter:    $$ su)
        $(info Enter:    Password: <type-root-password>)
        $(info Enter:    $(hash) $(PKG_CMD) $(USEFUL_PACKAGES) $(OPTIONAL_PACKAGES))
        $(info when that completes)
        $(info Enter:    $(hash) exit)
        $(info re-enter: $$ $(MAKE) $(MAKECMDGOALS) $(EXTRAS))
        $(error )
      endif
    endif
  endif
endif
ifneq (,$(GIT_COMMIT_ID))
  CFLAGS_GIT = -DSIM_GIT_COMMIT_ID=$(GIT_COMMIT_ID)
endif
ifneq (,$(GIT_COMMIT_TIME))
  CFLAGS_GIT += -DSIM_GIT_COMMIT_TIME=$(GIT_COMMIT_TIME)
endif
ifneq (,$(UNSUPPORTED_BUILD))
  CFLAGS_GIT += -DSIM_BUILD=Unsupported=$(UNSUPPORTED_BUILD)
endif
OPTIMIZE ?= -O2
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
  LTO =
else
  ifneq (,$(findstring clang,$(COMPILER_NAME))$(findstring LLVM,$(COMPILER_NAME)))
    CFLAGS_O = $(OPTIMIZE) -fno-strict-overflow
    GCC_OPTIMIZERS_CMD = ${GCC} --help 2>&1
  else
    CFLAGS_O := $(OPTIMIZE)
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
  ifneq (,$(findstring $(GCC_VERSION),$(LTO_EXCLUDE_VERSIONS)))
    override LTO =
  endif
  ifneq (,$(LTO))
    ifneq (,$(findstring -flto,$(GCC_OPTIMIZERS)))
      CFLAGS_O += -flto
      LTO_FEATURE = , with Link Time Optimization,
    endif
  endif
  BUILD_FEATURES = - compiler optimizations$(LTO_FEATURE) and no debugging support
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
  $(info *** - $(if $(BUILD_SEPARATE),Each source module compiled separately,Building using a single compile and link).)
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
    $(info *** git$(GIT_ARCHIVE_COMMIT_ID) commit id is $(GIT_COMMIT_ID).)
    $(info *** git$(GIT_ARCHIVE_COMMIT_ID) commit time is $(GIT_COMMIT_TIME).)
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
export CC := ${GCC} ${CC_STD} -U__STRICT_ANSI__ ${CFLAGS_G} ${CFLAGS_O} ${CFLAGS_GIT} ${CFLAGS_I} -DSIM_COMPILER="${COMPILER_NAME}" $(SIM_BUILD_OS_VERSION) -DSIM_BUILD_TOOL=simh-makefile$(if $(BUILD_SEPARATE),-separate-compiles,-single-compile) -I . ${OS_CCDEFS} ${ROMS_OPT}
ifneq (,${SIM_VERSION_MODE})
  CC += -DSIM_VERSION_MODE="${SIM_VERSION_MODE}"
endif
ifneq (,$(and $(findstring -lpthread,$(NETWORK_LDFLAGS)),$(findstring -lpthread,$(VIDEO_LDFLAGS))))
  export LDFLAGS := ${OS_LDFLAGS} $(NETWORK_LDFLAGS:-lpthread=) ${VIDEO_LDFLAGS} ${VIDEO_TTF_LDFLAGS} ${LDFLAGS_O}
else
  export LDFLAGS := ${OS_LDFLAGS} ${NETWORK_LDFLAGS} ${VIDEO_LDFLAGS} ${VIDEO_TTF_LDFLAGS} ${LDFLAGS_O}
endif

#
# Common Libraries
#
SIMHD = .
SIM = ${SIMHD}/scp.c ${SIMHD}/sim_console.c ${SIMHD}/sim_fio.c \
	${SIMHD}/sim_timer.c ${SIMHD}/sim_sock.c ${SIMHD}/sim_tmxr.c \
	${SIMHD}/sim_ether.c ${SIMHD}/sim_tape.c ${SIMHD}/sim_disk.c \
	${SIMHD}/sim_serial.c ${SIMHD}/sim_video.c ${SIMHD}/sim_imd.c \
	${SIMHD}/sim_card.c

DISPLAYD = ${SIMHD}/display

SCSI = ${SIMHD}/sim_scsi.c

BIN = BIN/
# The recursive logic needs a GNU make at least v4 when building with 
# separate compiles
ifneq (,$(call find_exe,gmake))
  override MAKE = $(call find_exe,gmake)
endif
ifneq (,$(and $(findstring 3.,$(GNUMakeVERSION)),$(BUILD_SEPARATE)))
  ifeq (HOMEBREW,$(PKG_MGR))
    $(info *** You can't build with separate compiles using version $(GNUMakeVERSION))
    $(info *** of GNU make.  A GNU make version 4 or later is required.)
    $(info *** Installing the latest GNU make using HomeBrew...)
    BREW_RESULT = $(shell brew install make 1>&2)
    $(info $(BREW_RESULT))
    override MAKE = $(call find_exe,gmake)
  else
    $(info makefile:error *** You can't build with separate compiles using version $(GNUMakeVERSION))
    $(error of GNU make.  A GNU make version 4 or later is required.)
  endif
endif
MAKEIT = @+$(MAKE) -f $(MAKEFILE_LIST) TARGET="$@" DEPS="$^"

#
# Emulator source files and compile time options
#
PDP1D = ${SIMHD}/PDP1
PDP1_DISPLAY_OPT = -DDISPLAY_TYPE=DIS_TYPE30 -DPIX_SCALE=RES_HALF
PDP1 = ${PDP1D}/pdp1_lp.c ${PDP1D}/pdp1_cpu.c ${PDP1D}/pdp1_stddev.c \
	${PDP1D}/pdp1_sys.c ${PDP1D}/pdp1_dt.c ${PDP1D}/pdp1_drm.c \
	${PDP1D}/pdp1_clk.c ${PDP1D}/pdp1_dcs.c ${PDP1D}/pdp1_dpy.c \
	${DISPLAYL}
PDP1_OPT = -I ${PDP1D} ${DISPLAY_OPT} $(PDP1_DISPLAY_OPT)


ND100D = ${SIMHD}/ND100
ND100 = ${ND100D}/nd100_sys.c ${ND100D}/nd100_cpu.c ${ND100D}/nd100_floppy.c \
	${ND100D}/nd100_stddev.c ${ND100D}/nd100_mm.c
ND100_OPT = -I ${ND100D}


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
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rpb.c \
	${PDP11D}/pdp11_rx.c ${PDP11D}/pdp11_stddev.c ${PDP11D}/pdp11_sys.c \
	${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_tm.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_io.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_pclk.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_pt.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_xu.c \
	${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_rh.c ${PDP11D}/pdp11_tu.c \
	${PDP11D}/pdp11_cpumod.c ${PDP11D}/pdp11_cr.c ${PDP11D}/pdp11_rf.c \
	${PDP11D}/pdp11_dl.c ${PDP11D}/pdp11_ta.c ${PDP11D}/pdp11_rc.c \
	${PDP11D}/pdp11_kg.c ${PDP11D}/pdp11_ke.c ${PDP11D}/pdp11_dc.c \
	${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_kmc.c ${PDP11D}/pdp11_dup.c \
	${PDP11D}/pdp11_rs.c ${PDP11D}/pdp11_vt.c ${PDP11D}/pdp11_td.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_rom.c ${PDP11D}/pdp11_ch.c \
	${PDP11D}/pdp11_dh.c ${PDP11D}/pdp11_ng.c ${PDP11D}/pdp11_daz.c \
	${PDP11D}/pdp11_tv.c ${PDP11D}/pdp11_mb.c ${PDP11D}/pdp11_rr.c \
	${DISPLAYL} ${DISPLAYNG} ${DISPLAYVT} $(NETWORK_DEPS)
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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_dup.c \
	$(NETWORK_DEPS)
VAX_OPT = -DVM_VAX -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS}


VAX410 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax410_sysdev.c ${VAXD}/vax410_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax4xx_rd.c ${VAXD}/vax4xx_rz80.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_va.c ${VAXD}/vax4xx_vc.c ${VAXD}/vax_lk.c \
	${VAXD}/vax_vs.c ${VAXD}/vax_gpx.c \
	$(NETWORK_DEPS)
VAX410_OPT = -DVM_VAX -DVAX_410 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} ${NETWORK_OPT} ${VIDEO_CCDEFS}


VAX420 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax420_sysdev.c ${VAXD}/vax420_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax4xx_rd.c ${VAXD}/vax4xx_rz80.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_va.c ${VAXD}/vax4xx_vc.c ${VAXD}/vax4xx_ve.c \
	${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_gpx.c \
	$(NETWORK_DEPS)
VAX420_OPT = -DVM_VAX -DVAX_420 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS}
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
	${VAXD}/vax4xx_ve.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c \
	$(NETWORK_DEPS)
VAX43_OPT = -DVM_VAX -DVAX_43 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} ${NETWORK_OPT} ${VIDEO_CCDEFS}


VAX440 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax4xx_stddev.c \
	${VAXD}/vax440_sysdev.c ${VAXD}/vax440_syslist.c ${VAXD}/vax4xx_dz.c \
	${VAXD}/vax_xs.c ${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax4xx_rz94.c \
	$(NETWORK_DEPS)
VAX440_OPT = -DVM_VAX -DVAX_440 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} ${NETWORK_OPT}
VAX46_OPT = ${VAX440_OPT} -DVAX_46
VAX47_OPT = ${VAX440_OPT} -DVAX_47
VAX48_OPT = ${VAX440_OPT} -DVAX_48


IS1000 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax_nar.c ${VAXD}/vax_xs.c \
	${VAXD}/vax4xx_rz94.c ${VAXD}/vax4nn_stddev.c \
	${VAXD}/is1000_sysdev.c ${VAXD}/is1000_syslist.c \
	$(NETWORK_DEPS)
IS1000_OPT = -DVM_VAX -DIS_1000 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} ${NETWORK_OPT}


VAX610 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c ${VAXD}/vax_syscm.c \
	${VAXD}/vax610_stddev.c ${VAXD}/vax610_sysdev.c ${VAXD}/vax610_io.c \
	${VAXD}/vax610_syslist.c ${VAXD}/vax610_mem.c ${VAXD}/vax_vc.c \
	${VAXD}/vax_lk.c ${VAXD}/vax_vs.c ${VAXD}/vax_2681.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c \
	$(NETWORK_DEPS)
VAX610_OPT = -DVM_VAX -DVAX_610 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS}


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
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_dup.c \
	$(NETWORK_DEPS)
VAX620_OPT = -DVM_VAX -DVAX_620 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}
VAX630_OPT = -DVM_VAX -DVAX_630 -DUSE_INT64 -DUSE_ADDR64 -DUSE_SIM_VIDEO -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT} ${VIDEO_CCDEFS}


VAX730 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax730_stddev.c ${VAXD}/vax730_sys.c \
	${VAXD}/vax730_mem.c ${VAXD}/vax730_uba.c ${VAXD}/vax730_rb.c \
	${VAXD}/vax_uw.c ${VAXD}/vax730_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c ${PDP11D}/pdp11_dup.c \
	$(NETWORK_DEPS)
VAX730_OPT = -DVM_VAX -DVAX_730 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX750 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax750_stddev.c ${VAXD}/vax750_cmi.c \
	${VAXD}/vax750_mem.c ${VAXD}/vax750_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax_uw.c ${VAXD}/vax750_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rpb.c \
	${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c \
	${PDP11D}/pdp11_rk.c ${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c \
	$(NETWORK_DEPS)
VAX750_OPT = -DVM_VAX -DVAX_750 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX780 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax780_stddev.c ${VAXD}/vax780_sbi.c \
	${VAXD}/vax780_mem.c ${VAXD}/vax780_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax780_fload.c 	${VAXD}/vax_uw.c ${VAXD}/vax780_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rpb.c ${PDP11D}/pdp11_tu.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c \
	${PDP11D}/pdp11_rk.c ${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c \
	$(NETWORK_DEPS)
VAX780_OPT = -DVM_VAX -DVAX_780 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX8200 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax_watch.c ${VAXD}/vax820_stddev.c ${VAXD}/vax820_bi.c \
	${VAXD}/vax820_mem.c ${VAXD}/vax820_uba.c ${VAXD}/vax820_ka.c \
	${VAXD}/vax_uw.c ${VAXD}/vax820_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c ${PDP11D}/pdp11_rk.c \
	${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c ${PDP11D}/pdp11_dup.c \
	$(NETWORK_DEPS)
VAX8200_OPT = -DVM_VAX -DVAX_820 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX8600 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax860_stddev.c ${VAXD}/vax860_sbia.c \
	${VAXD}/vax860_abus.c ${VAXD}/vax780_uba.c ${VAXD}/vax7x0_mba.c \
	${VAXD}/vax_uw.c ${VAXD}/vax860_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_rpb.c ${PDP11D}/pdp11_tu.c \
	${PDP11D}/pdp11_hk.c ${PDP11D}/pdp11_vh.c ${PDP11D}/pdp11_dmc.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_td.c ${PDP11D}/pdp11_tc.c \
	${PDP11D}/pdp11_rk.c ${PDP11D}/pdp11_io_lib.c ${PDP11D}/pdp11_ch.c \
	$(NETWORK_DEPS)
VAX8600_OPT = -DVM_VAX -DVAX_860 -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


PDP10D = ${SIMHD}/PDP10
PDP10 = ${PDP10D}/pdp10_fe.c ${PDP11D}/pdp11_dz.c ${PDP10D}/pdp10_cpu.c \
	${PDP10D}/pdp10_ksio.c ${PDP10D}/pdp10_lp20.c ${PDP10D}/pdp10_mdfp.c \
	${PDP10D}/pdp10_pag.c ${PDP10D}/pdp10_rp.c ${PDP10D}/pdp10_sys.c \
	${PDP10D}/pdp10_tim.c ${PDP10D}/pdp10_tu.c ${PDP10D}/pdp10_xtnd.c \
	${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_dup.c ${PDP11D}/pdp11_dmc.c ${PDP11D}/pdp11_kmc.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ch.c \
	$(NETWORK_DEPS)
PDP10_OPT = -DVM_PDP10 -DUSE_INT64 -I ${PDP10D} -I ${PDP11D} ${NETWORK_OPT}


IMLACD = ${SIMHD}/imlac
IMLAC = ${IMLACD}/imlac_sys.c ${IMLACD}/imlac_cpu.c \
	${IMLACD}/imlac_dp.c ${IMLACD}/imlac_crt.c ${IMLACD}/imlac_kbd.c \
	${IMLACD}/imlac_tty.c ${IMLACD}/imlac_pt.c ${IMLACD}/imlac_bel.c \
	${DISPLAYL}
IMLAC_OPT = -I ${IMLACD} ${DISPLAY_OPT}


TT2500D = ${SIMHD}/tt2500
TT2500 = ${TT2500D}/tt2500_sys.c ${TT2500D}/tt2500_cpu.c \
	${TT2500D}/tt2500_dpy.c ${TT2500D}/tt2500_crt.c ${TT2500D}/tt2500_tv.c \
	${TT2500D}/tt2500_key.c ${TT2500D}/tt2500_uart.c ${TT2500D}/tt2500_rom.c \
	${DISPLAYL}
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
ID16_OPT = -DIFP_IN_MEM -I ${ID16D}


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
	${ALTAIRZ80D}/s100_dazzler.c \
	${ALTAIRZ80D}/s100_tuart.c \
	${ALTAIRZ80D}/s100_jair.c \
	${ALTAIRZ80D}/sol20.c \
	${ALTAIRZ80D}/s100_vdm1.c \
	${ALTAIRZ80D}/mmd.c \
	${ALTAIRZ80D}/s100_dj2d.c \
	${ALTAIRZ80D}/s100_djhdc.c \
	${ALTAIRZ80D}/altairz80_dsk.c ${ALTAIRZ80D}/disasm.c \
	${ALTAIRZ80D}/altairz80_sio.c ${ALTAIRZ80D}/altairz80_sys.c \
	${ALTAIRZ80D}/altairz80_hdsk.c ${ALTAIRZ80D}/altairz80_net.c \
	${ALTAIRZ80D}/s100_hayes.c ${ALTAIRZ80D}/s100_2sio.c ${ALTAIRZ80D}/s100_pmmi.c\
	${ALTAIRZ80D}/flashwriter2.c ${ALTAIRZ80D}/i86_decode.c \
	${ALTAIRZ80D}/i86_ops.c ${ALTAIRZ80D}/i86_prim_ops.c \
	${ALTAIRZ80D}/i8272.c ${ALTAIRZ80D}/insnsd.c ${ALTAIRZ80D}/altairz80_mhdsk.c \
	${ALTAIRZ80D}/ibc.c ${ALTAIRZ80D}/ibc_mcc_hdc.c ${ALTAIRZ80D}/ibc_smd_hdc.c \
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
	${ALTAIRZ80D}/s100_tdd.c \
	${ALTAIRZ80D}/wd179x.c ${ALTAIRZ80D}/s100_hdc1001.c \
	${ALTAIRZ80D}/s100_if3.c ${ALTAIRZ80D}/s100_adcs6.c \
	${ALTAIRZ80D}/m68k/m68kcpu.c ${ALTAIRZ80D}/m68k/m68kdasm.c ${ALTAIRZ80D}/m68k/m68kasm.c \
	${ALTAIRZ80D}/m68k/m68kopac.c ${ALTAIRZ80D}/m68k/m68kopdm.c \
	${ALTAIRZ80D}/m68k/softfloat/softfloat.c \
	${ALTAIRZ80D}/m68k/m68kopnz.c ${ALTAIRZ80D}/m68k/m68kops.c ${ALTAIRZ80D}/m68ksim.c
ALTAIRZ80_OPT = -I ${ALTAIRZ80D} -DUSE_SIM_VIDEO ${VIDEO_CCDEFS} $(VIDEO_LDFLAGS)


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
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800D}/mp-a_sys.c \
	${SWTP6800C}/mp-8m.c ${SWTP6800C}/fd400.c ${SWTP6800C}/mp-b2.c \
	${SWTP6800C}/mp-s.c
SWTP6800MP-A2 = ${SWTP6800C}/mp-a2.c ${SWTP6800C}/m6800.c ${SWTP6800C}/m6810.c \
	${SWTP6800C}/bootrom.c ${SWTP6800C}/dc-4.c ${SWTP6800D}/mp-a2_sys.c \
	${SWTP6800C}/mp-8m.c ${SWTP6800C}/i2716.c ${SWTP6800C}/fd400.c \
	${SWTP6800C}/mp-s.c ${SWTP6800C}/mp-b2.c
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
	${INTELSYSC}/isbc064.c \
	${INTELSYSC}/isbc202.c \
	${INTELSYSC}/isbc201.c \
	${INTELSYSC}/isbc206.c \
	${INTELSYSC}/isbc464.c \
	${INTELSYSC}/isbc208.c \
	${INTELSYSC}/port.c \
	${INTELSYSC}/irq.c \
	${INTELSYSC}/multibus.c \
	${INTELSYSC}/mem.c \
	${INTELSYSC}/sys.c \
	${INTELSYSC}/zx200a.c


INTEL_MDSD = ${INTELSYSD}/Intel-MDS
INTEL_MDS = ${INTELSYSC}/i8080.c ${INTEL_MDSD}/imds_sys.c \
	${INTEL_PARTS}
INTEL_MDS_OPT = -I ${INTEL_MDSD}


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
B5500_OPT = -I ${B5500D} -DUSE_INT64 -DB5500 -DUSE_SIM_CARD

BESM6D = ${SIMHD}/BESM6
BESM6 = ${BESM6D}/besm6_cpu.c ${BESM6D}/besm6_sys.c ${BESM6D}/besm6_mmu.c \
        ${BESM6D}/besm6_arith.c ${BESM6D}/besm6_disk.c ${BESM6D}/besm6_drum.c \
        ${BESM6D}/besm6_tty.c ${BESM6D}/besm6_panel.c ${BESM6D}/besm6_printer.c \
        ${BESM6D}/besm6_pl.c ${BESM6D}/besm6_mg.c \
        ${BESM6D}/besm6_punch.c ${BESM6D}/besm6_punchcard.c ${BESM6D}/besm6_vu.c
BESM6_OPT = -I ${BESM6D} -DUSE_INT64 $(VIDEO_TTF_OPT)

PDP6D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
  PDP6_DISPLAY_OPT =
endif
PDP6 = ${PDP6D}/kx10_cpu.c ${PDP6D}/kx10_sys.c ${PDP6D}/kx10_cty.c \
	${PDP6D}/kx10_lp.c ${PDP6D}/kx10_pt.c ${PDP6D}/kx10_cr.c \
	${PDP6D}/kx10_cp.c ${PDP6D}/pdp6_dct.c ${PDP6D}/pdp6_dtc.c \
	${PDP6D}/pdp6_mtc.c ${PDP6D}/pdp6_dsk.c ${PDP6D}/pdp6_dcs.c \
	${PDP6D}/kx10_dpy.c ${PDP6D}/pdp6_slave.c ${PDP6D}/pdp6_ge.c \
	${DISPLAYL} ${DISPLAY340}
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
	${KA10D}/ka10_pclk.c ${KA10D}/ka10_tv.c ${KA10D}/ka10_dd.c \
	${KA10D}/kx10_ddc.c ${DISPLAYL} ${DISPLAY340} ${DISPLAYIII} $(NETWORK_DEPS)
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
	${KI10D}/kx10_ddc.c ${KI10D}/kx10_tym.c ${DISPLAYL} ${DISPLAY340} $(NETWORK_DEPS)
KI10_OPT = -DKI=1 -DUSE_INT64 -I ${KI10D} -DUSE_SIM_CARD ${NETWORK_OPT} ${DISPLAY_OPT} ${KI10_DISPLAY_OPT}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
KI10_OPT += -DPANDA_LIGHTS
KI10 += ${KA10D}/ka10_lights.c
KI10_LDFLAGS = -lusb-1.0
endif

KL10D = ${SIMHD}/PDP10
KL10 =  ${KL10D}/kx10_cpu.c ${KL10D}/kx10_sys.c ${KL10D}/kx10_df.c \
    ${KA10D}/kx10_dp.c ${KA10D}/kx10_mt.c ${KA10D}/kx10_lp.c \
    ${KA10D}/kx10_pt.c ${KA10D}/kx10_dc.c ${KL10D}/kx10_rh.c \
    ${KA10D}/kx10_dt.c ${KA10D}/kx10_cr.c ${KA10D}/kx10_cp.c \
    ${KL10D}/kx10_rp.c ${KL10D}/kx10_tu.c ${KL10D}/kx10_rs.c \
    ${KL10D}/kx10_imp.c ${KL10D}/kl10_fe.c ${KL10D}/ka10_pd.c \
    ${KL10D}/ka10_ch10.c ${KL10D}/kl10_nia.c ${KL10D}/kx10_disk.c \
    $(NETWORK_DEPS)
KL10_OPT = -DKL=1 -DUSE_INT64 -I $(KL10D) -DUSE_SIM_CARD ${NETWORK_OPT} 

KS10D = ${SIMHD}/PDP10
KS10 = ${KS10D}/kx10_cpu.c ${KS10D}/kx10_sys.c ${KS10D}/kx10_disk.c \
	${KS10D}/ks10_cty.c ${KS10D}/ks10_uba.c ${KS10D}/kx10_rh.c \
	${KS10D}/kx10_rp.c ${KS10D}/kx10_tu.c ${KS10D}/ks10_dz.c \
	${KS10D}/ks10_tcu.c ${KS10D}/ks10_lp.c ${KS10D}/ks10_ch11.c \
	${KS10D}/ks10_kmc.c ${KS10D}/ks10_dup.c ${KS10D}/kx10_imp.c \
	$(NETWORK_DEPS)
KS10_OPT = -DKS=1 -DUSE_INT64 -I $(KS10D) -I $(PDP11D) ${NETWORK_OPT}

ATT3B2D = ${SIMHD}/3B2
ATT3B2M400 = ${ATT3B2D}/3b2_cpu.c ${ATT3B2D}/3b2_sys.c \
    ${ATT3B2D}/3b2_rev2_sys.c ${ATT3B2D}/3b2_rev2_mmu.c \
    ${ATT3B2D}/3b2_mau.c ${ATT3B2D}/3b2_rev2_csr.c \
    ${ATT3B2D}/3b2_timer.c ${ATT3B2D}/3b2_stddev.c \
    ${ATT3B2D}/3b2_mem.c ${ATT3B2D}/3b2_iu.c \
    ${ATT3B2D}/3b2_if.c ${ATT3B2D}/3b2_id.c \
    ${ATT3B2D}/3b2_dmac.c ${ATT3B2D}/3b2_io.c \
    ${ATT3B2D}/3b2_ports.c ${ATT3B2D}/3b2_ctc.c \
	${ATT3B2D}/3b2_ni.c \
	$(NETWORK_DEPS)
ATT3B2M400_OPT = -DUSE_INT64 -DUSE_ADDR64 -DREV2 -I ${ATT3B2D} ${NETWORK_OPT}

ATT3B2M700 = ${ATT3B2D}/3b2_cpu.c ${ATT3B2D}/3b2_sys.c \
    ${ATT3B2D}/3b2_rev3_sys.c ${ATT3B2D}/3b2_rev3_mmu.c \
    ${ATT3B2D}/3b2_mau.c ${ATT3B2D}/3b2_rev3_csr.c \
    ${ATT3B2D}/3b2_timer.c ${ATT3B2D}/3b2_stddev.c \
    ${ATT3B2D}/3b2_mem.c ${ATT3B2D}/3b2_iu.c \
    ${ATT3B2D}/3b2_if.c ${ATT3B2D}/3b2_dmac.c \
    ${ATT3B2D}/3b2_io.c ${ATT3B2D}/3b2_ports.c \
	${ATT3B2D}/3b2_scsi.c ${ATT3B2D}/3b2_ni.c \
	$(NETWORK_DEPS)
ATT3B2M700_OPT = -DUSE_INT64 -DUSE_ADDR64 -DREV3 -I ${ATT3B2D} ${NETWORK_OPT}

SIGMAD = ${SIMHD}/sigma
SIGMA = ${SIGMAD}/sigma_cpu.c ${SIGMAD}/sigma_sys.c ${SIGMAD}/sigma_cis.c \
	${SIGMAD}/sigma_coc.c ${SIGMAD}/sigma_dk.c ${SIGMAD}/sigma_dp.c \
	${SIGMAD}/sigma_fp.c ${SIGMAD}/sigma_io.c ${SIGMAD}/sigma_lp.c \
	${SIGMAD}/sigma_map.c ${SIGMAD}/sigma_mt.c ${SIGMAD}/sigma_pt.c \
	${SIGMAD}/sigma_rad.c ${SIGMAD}/sigma_rtc.c ${SIGMAD}/sigma_tt.c \
	${SIGMAD}/sigma_cr.c ${SIGMAD}/sigma_cp.c 
SIGMA_OPT = -I ${SIGMAD}

SEL32D = ${SIMHD}/SEL32
SEL32 = ${SEL32D}/sel32_cpu.c ${SEL32D}/sel32_sys.c ${SEL32D}/sel32_chan.c \
	${SEL32D}/sel32_iop.c ${SEL32D}/sel32_com.c ${SEL32D}/sel32_con.c \
	${SEL32D}/sel32_clk.c ${SEL32D}/sel32_mt.c ${SEL32D}/sel32_lpr.c \
	${SEL32D}/sel32_scfi.c ${SEL32D}/sel32_fltpt.c ${SEL32D}/sel32_disk.c \
	${SEL32D}/sel32_hsdp.c ${SEL32D}/sel32_mfp.c ${SEL32D}/sel32_scsi.c \
	${SEL32D}/sel32_ec.c \
	$(NETWORK_DEPS)
SEL32_OPT = -I ${SEL32D} -DUSE_INT32 -DSEL32  ${NETWORK_OPT}

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
SAGE_OPT = -I ${SAGED} -DHAVE_INT64

PDQ3D = ${SIMHD}/PDQ-3
PDQ3 = ${PDQ3D}/pdq3_cpu.c ${PDQ3D}/pdq3_sys.c ${PDQ3D}/pdq3_stddev.c \
    ${PDQ3D}/pdq3_mem.c ${PDQ3D}/pdq3_debug.c ${PDQ3D}/pdq3_fdc.c
PDQ3_OPT = -I ${PDQ3D}

#
# Build everything (not the unsupported/incomplete or experimental simulators)
#
ALL = pdp1 pdp4 pdp7 pdp8 pdp9 pdp15 pdp11 pdp10 \
	vax microvax3900 microvax1 rtvax1000 microvax2 vax730 vax750 vax780 \
	vax8200 vax8600 besm6 \
	microvax2000 infoserver100 infoserver150vxt microvax3100 microvax3100e \
	vaxstation3100m30 vaxstation3100m38 vaxstation3100m76 vaxstation4000m60 \
	microvax3100m80 vaxstation4000vlc infoserver1000 \
	nd100 nova eclipse hp2100 hp3000 i1401 i1620 s3 altair altairz80 gri \
	i7094 ibm1130 id16 id32 sds lgp h316 cdc1700 \
	swtp6800mp-a swtp6800mp-a2 tx-0 ssem b5500 intel-mds \
	scelbi 3b2 3b2-700 i701 i704 i7010 i7070 i7080 i7090 \
	sigma uc15 pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6 i650 \
	imlac tt2500 sel32

all : ${ALL}

EXPERIMENTAL = alpha pdq3 sage

experimental : ${EXPERIMENTAL}

clean :
ifeq (${WIN32},)
	-${RM} -rf ${BIN}
else
	-if exist $(BIN) rmdir /s /q BIN
endif

${BUILD_ROMS} :
	${MKDIRBIN}
ifeq (${WIN32},)
	@if ${TEST} \( ! -e $@ \) -o \( sim_BuildROMs.c -nt $@ \) ; then ${CC} sim_BuildROMs.c ${CC_OUTSPEC}; fi
else
	@if not exist $@ ${CC} sim_BuildROMs.c ${CC_OUTSPEC}
endif
	@$@

MAKEFLAGS += --no-print-directory

#
# Individual builds
#

pdp1 : $(BIN)pdp1$(EXE)

$(BIN)pdp1$(EXE) : $(PDP1) $(SIM)
	$(MAKEIT) OPTS="$(PDP1_OPT)"


pdp4 : $(BIN)pdp4$(EXE)

$(BIN)pdp4$(EXE) : $(PDP18B) $(SIM)
	$(MAKEIT) OPTS="$(PDP4_OPT)"


pdp7 : $(BIN)pdp7$(EXE)

$(BIN)pdp7$(EXE) : $(PDP18B) ${PDP18BD}/pdp18b_dpy.c ${DISPLAYL} ${DISPLAY340} $(SIM)
	$(MAKEIT) OPTS="$(PDP7_OPT)"


pdp8 : $(BIN)pdp8$(EXE)

$(BIN)pdp8$(EXE) : ${PDP8} ${SIM}
	$(MAKEIT) OPTS="$(PDP8_OPT)"


pdp9 : $(BIN)pdp9$(EXE)

$(BIN)pdp9$(EXE) : ${PDP18B} ${SIM}
	$(MAKEIT) OPTS="$(PDP9_OPT)"


pdp15 : $(BIN)pdp15$(EXE)

$(BIN)pdp15$(EXE) : ${PDP18B} ${SIM}
	$(MAKEIT) OPTS="$(PDP15_OPT)"


pdp10 : $(BIN)pdp10$(EXE)

$(BIN)pdp10$(EXE) : ${PDP10} ${SIM}
	$(MAKEIT) OPTS="$(PDP10_OPT)"


imlac : $(BIN)imlac$(EXE)

$(BIN)imlac$(EXE) : ${IMLAC} ${SIM}
	$(MAKEIT) OPTS="$(IMLAC_OPT)"


tt2500 : $(BIN)tt2500$(EXE)

$(BIN)tt2500$(EXE) : ${TT2500} ${SIM}
	$(MAKEIT) OPTS="$(TT2500_OPT)"


pdp11 : $(BIN)pdp11$(EXE)

$(BIN)pdp11$(EXE) : ${PDP11} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(PDP11_OPT)"


uc15 : $(BIN)uc15$(EXE)

$(BIN)uc15$(EXE) : ${UC15} ${SIM}
	$(MAKEIT) OPTS="$(UC15_OPT)"


microvax3900 : vax

vax : $(BIN)vax$(EXE)

$(BIN)vax$(EXE) : ${VAX} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX_OPT)" TEST_NAME=vax-diag ALTNAME=microvax3900


microvax2000 : $(BIN)microvax2000$(EXE)

$(BIN)microvax2000$(EXE) : ${VAX410} ${SIM} ${SCSI} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX410_OPT)" TEST_NAME=vax-diag


infoserver100 : $(BIN)infoserver100$(EXE)

$(BIN)infoserver100$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX411_OPT)" TEST_NAME=vax-diag


infoserver150vxt : $(BIN)infoserver150vxt$(EXE)

$(BIN)infoserver150vxt$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX412_OPT)" TEST_NAME=vax-diag


microvax3100 : $(BIN)microvax3100$(EXE)

$(BIN)microvax3100$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX41A_OPT)" TEST_NAME=vax-diag


microvax3100e : $(BIN)microvax3100e$(EXE)

$(BIN)microvax3100e$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX41D_OPT)" TEST_NAME=vax-diag


vaxstation3100m30 : $(BIN)vaxstation3100m30$(EXE)

$(BIN)vaxstation3100m30$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX42A_OPT)" TEST_NAME=vax-diag


vaxstation3100m38 : $(BIN)vaxstation3100m38$(EXE)

$(BIN)vaxstation3100m38$(EXE) : ${VAX420} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX42B_OPT)" TEST_NAME=vax-diag


vaxstation3100m76 : $(BIN)vaxstation3100m76$(EXE)

$(BIN)vaxstation3100m76$(EXE) : ${VAX43} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX43_OPT)" TEST_NAME=vax-diag


vaxstation4000m60 : $(BIN)vaxstation4000m60$(EXE)

$(BIN)vaxstation4000m60$(EXE) : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX46_OPT)" TEST_NAME=vax-diag


microvax3100m80 : $(BIN)microvax3100m80$(EXE)

$(BIN)microvax3100m80$(EXE) : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX47_OPT)" TEST_NAME=vax-diag


vaxstation4000vlc : $(BIN)vaxstation4000vlc$(EXE)

$(BIN)vaxstation4000vlc$(EXE) : ${VAX440} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX48_OPT)" TEST_NAME=vax-diag


infoserver1000 : $(BIN)infoserver1000$(EXE)

$(BIN)infoserver1000$(EXE) : ${IS1000} ${SCSI} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(IS1000_OPT)" TEST_NAME=vax-diag


microvax1 : $(BIN)microvax1$(EXE)

$(BIN)microvax1$(EXE) : ${VAX610} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX610_OPT)" TEST_NAME=vax-diag


rtvax1000 : $(BIN)rtvax1000$(EXE)

$(BIN)rtvax1000$(EXE) : ${VAX630} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX620_OPT)" TEST_NAME=vax-diag


microvax2 : $(BIN)microvax2$(EXE)

$(BIN)microvax2$(EXE) : ${VAX630} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX630_OPT)" TEST_NAME=vax-diag


vax730 : $(BIN)vax730$(EXE)

$(BIN)vax730$(EXE) : ${VAX730} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX730_OPT)" TEST_NAME=vax-diag


vax750 : $(BIN)vax750$(EXE)

$(BIN)vax750$(EXE) : ${VAX750} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX750_OPT)" TEST_NAME=vax-diag


vax780 : $(BIN)vax780$(EXE)

$(BIN)vax780$(EXE) : ${VAX780} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX780_OPT)" TEST_NAME=vax-diag


vax8200 : $(BIN)vax8200$(EXE)

$(BIN)vax8200$(EXE) : ${VAX8200} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX8200_OPT)" TEST_NAME=vax-diag


vax8600 : $(BIN)vax8600$(EXE)

$(BIN)vax8600$(EXE) : ${VAX8600} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(VAX8600_OPT)" TEST_NAME=vax-diag


nd100 : $(BIN)nd100$(EXE)

$(BIN)nd100$(EXE) : ${ND100} ${SIM}
	$(MAKEIT) OPTS="$(ND100_OPT)"


nova : $(BIN)nova$(EXE)

$(BIN)nova$(EXE) : ${NOVA} ${SIM}
	$(MAKEIT) OPTS="$(NOVA_OPT)"


eclipse : $(BIN)eclipse$(EXE)

$(BIN)eclipse$(EXE) : ${ECLIPSE} ${SIM}
	$(MAKEIT) OPTS="$(ECLIPSE_OPT)"


h316 : $(BIN)h316$(EXE)

$(BIN)h316$(EXE) : ${H316} ${SIM}
	$(MAKEIT) OPTS="$(H316_OPT)"


hp2100 : $(BIN)hp2100$(EXE)

$(BIN)hp2100$(EXE) : ${HP2100} ${SIM}
	$(MAKEIT) OPTS="$(HP2100_OPT)" NOCPP=1


hp3000 : $(BIN)hp3000$(EXE)

$(BIN)hp3000$(EXE) : ${HP3000} ${SIM}
	$(MAKEIT) OPTS="$(HP3000_OPT)" NOCPP=1


i1401 : $(BIN)i1401$(EXE)

$(BIN)i1401$(EXE) : ${I1401} ${SIM}
	$(MAKEIT) OPTS="$(I1401_OPT)"


i1620 : $(BIN)i1620$(EXE)

$(BIN)i1620$(EXE) : ${I1620} ${SIM}
	$(MAKEIT) OPTS="$(I1620_OPT)"


i7094 : $(BIN)i7094$(EXE)

$(BIN)i7094$(EXE) : ${I7094} ${SIM}
	$(MAKEIT) OPTS="$(I7094_OPT)"


ibm1130 : $(BIN)ibm1130$(EXE)

#$(BIN)ibm1130$(EXE) : ${IBM1130} $(SIM)
#	$(MAKEIT) OPTS="$(IBM1130_OPT)" NOCPP=1

${BIN}ibm1130${EXE} : ${IBM1130}
ifneq (1,${CPP_BUILD}${CPP_FORCE})
	${MKDIRBIN}
ifneq (${WIN32},)
	windres ${IBM1130D}/ibm1130.rc ${BIN}ibm1130.o
endif
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (${WIN32},)
	del $(BIN)\ibm1130.o
endif
ifneq (,$(call find_test,Ibm1130))
	$@ $(call find_test,Ibm1130) ${TEST_ARG}
endif
else
	$(info ibm1130 can not be built using C++)
endif

s3 : $(BIN)s3$(EXE)

$(BIN)s3$(EXE) : ${S3} ${SIM}
	$(MAKEIT) OPTS="$(S3_OPT)"


sel32 : $(BIN)sel32$(EXE)

$(BIN)sel32$(EXE) : ${SEL32} ${SIM}
	$(MAKEIT) OPTS="$(SEL32_OPT)"


altair : $(BIN)altair$(EXE)

$(BIN)altair$(EXE) : ${ALTAIR} ${SIM}
	$(MAKEIT) OPTS="$(ALTAIR_OPT)"


altairz80 : $(BIN)altairz80$(EXE)

$(BIN)altairz80$(EXE) : ${ALTAIRZ80} ${SIM}
	$(MAKEIT) OPTS="$(ALTAIRZ80_OPT)"


gri : $(BIN)gri$(EXE)

$(BIN)gri$(EXE) : ${GRI} ${SIM}
	$(MAKEIT) OPTS="$(GRI_OPT)"


lgp : $(BIN)lgp$(EXE)

$(BIN)lgp$(EXE) : ${LGP} ${SIM}
	$(MAKEIT) OPTS="$(LGP_OPT)"


id16 : $(BIN)id16$(EXE)

$(BIN)id16$(EXE) : ${ID16} ${SIM}
	$(MAKEIT) OPTS="$(ID16_OPT)"


id32 : $(BIN)id32$(EXE)

$(BIN)id32$(EXE) : ${ID32} ${SIM}
	$(MAKEIT) OPTS="$(ID32_OPT)"


sds : $(BIN)sds$(EXE)

$(BIN)sds$(EXE) : ${SDS} ${SIM}
	$(MAKEIT) OPTS="$(SDS_OPT)"


swtp6800mp-a : $(BIN)swtp6800mp-a$(EXE)

$(BIN)swtp6800mp-a$(EXE) : ${SWTP6800MP-A} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(SWTP6800_OPT)"


swtp6800mp-a2 : $(BIN)swtp6800mp-a2$(EXE)

$(BIN)swtp6800mp-a2$(EXE) : ${SWTP6800MP-A2} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(SWTP6800_OPT)"


intel-mds : $(BIN)intel-mds$(EXE)

$(BIN)intel-mds$(EXE) : ${INTEL_MDS} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(INTEL_MDS_OPT)"


ibmpc : $(BIN)ibmpc$(EXE)

$(BIN)ibmpc$(EXE) : ${IBMPC} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(IBMPC_OPT)"


ibmpcxt : $(BIN)ibmpcxt$(EXE)

$(BIN)ibmpcxt$(EXE) : ${IBMPCXT} ${SIM} ${BUILD_ROMS}
	$(MAKEIT) OPTS="$(IBMPCXT_OPT)"


scelbi : $(BIN)scelbi$(EXE)

$(BIN)scelbi$(EXE) : ${SCELBI} ${SIM}
	$(MAKEIT) OPTS="$(SCELBI_OPT)"


tx-0 : $(BIN)tx-0$(EXE)

$(BIN)tx-0$(EXE) : ${TX0} ${SIM}
	$(MAKEIT) OPTS="$(TX0_OPT)"


ssem : $(BIN)ssem$(EXE)

$(BIN)ssem$(EXE) : ${SSEM} ${SIM}
	$(MAKEIT) OPTS="$(SSEM_OPT)"


cdc1700 : $(BIN)cdc1700$(EXE)

$(BIN)cdc1700$(EXE) : ${CDC1700} ${SIM}
	$(MAKEIT) OPTS="$(CDC1700_OPT)"


besm6 : $(BIN)besm6$(EXE)

$(BIN)besm6$(EXE) : ${BESM6} ${SIM}
	$(MAKEIT) OPTS="$(BESM6_OPT)" NOCPP=1


sigma : $(BIN)sigma$(EXE)

$(BIN)sigma$(EXE) : ${SIGMA} ${SIM}
	$(MAKEIT) OPTS="$(SIGMA_OPT)"


alpha : $(BIN)alpha$(EXE)

$(BIN)alpha$(EXE) : ${ALPHA} ${SIM}
	$(MAKEIT) OPTS="$(ALPHA_OPT)"


sage : $(BIN)sage$(EXE)

$(BIN)sage$(EXE) : ${SAGE} ${SIM}
	$(MAKEIT) OPTS="$(SAGE_OPT)"


pdq3 : $(BIN)pdq3$(EXE)

$(BIN)pdq3$(EXE) : ${PDQ3} ${SIM}
	$(MAKEIT) OPTS="$(PDQ3_OPT)"


b5500 : $(BIN)b5500$(EXE)

$(BIN)b5500$(EXE) : ${B5500} ${SIM}
	$(MAKEIT) OPTS="$(B5500_OPT)"


3b2 : $(BIN)3b2$(EXE)

$(BIN)3b2$(EXE) : ${ATT3B2M400} ${SIM}
	$(MAKEIT) OPTS="$(ATT3B2M400_OPT)" ALTNAME=3b2-400
 

3b2-700 : $(BIN)3b2-700$(EXE)

$(BIN)3b2-700$(EXE) : ${ATT3B2M700} ${SCSI} ${SIM}
	$(MAKEIT) OPTS="$(ATT3B2M700_OPT)"


i7090 : $(BIN)i7090$(EXE)

$(BIN)i7090$(EXE) : ${I7090} ${SIM}
	$(MAKEIT) OPTS="$(I7090_OPT)"


i7080 : $(BIN)i7080$(EXE)

$(BIN)i7080$(EXE) : ${I7080} ${SIM}
	$(MAKEIT) OPTS="$(I7080_OPT)"


i7070 : $(BIN)i7070$(EXE)

$(BIN)i7070$(EXE) : ${I7070} ${SIM}
	$(MAKEIT) OPTS="$(I7070_OPT)"


i7010 : $(BIN)i7010$(EXE)

$(BIN)i7010$(EXE) : ${I7010} ${SIM}
	$(MAKEIT) OPTS="$(I7010_OPT)"


i704 : $(BIN)i704$(EXE)

$(BIN)i704$(EXE) : ${I704} ${SIM}
	$(MAKEIT) OPTS="$(I704_OPT)"


i701 : $(BIN)i701$(EXE)

$(BIN)i701$(EXE) : ${I701} ${SIM}
	$(MAKEIT) OPTS="$(I701_OPT)"


i650 : $(BIN)i650$(EXE)

$(BIN)i650$(EXE) : ${I650} ${SIM}
	$(MAKEIT) OPTS="$(I650_OPT)"


pdp6 : $(BIN)pdp6$(EXE)

$(BIN)pdp6$(EXE) : ${PDP6} ${SIM}
	$(MAKEIT) OPTS="$(PDP6_OPT)"


pdp10-ka : $(BIN)pdp10-ka$(EXE)

$(BIN)pdp10-ka$(EXE) : ${KA10} ${SIM}
	$(MAKEIT) OPTS="$(KA10_OPT)"


pdp10-ki : $(BIN)pdp10-ki$(EXE)

$(BIN)pdp10-ki$(EXE) : ${KI10} ${SIM}
	$(MAKEIT) OPTS="$(KI10_OPT)"


pdp10-kl : $(BIN)pdp10-kl$(EXE)

$(BIN)pdp10-kl$(EXE) : ${KL10} ${SIM}
	$(MAKEIT) OPTS="$(KL10_OPT)"


pdp10-ks : $(BIN)pdp10-ks$(EXE)

$(BIN)pdp10-ks$(EXE) : ${KS10} ${SIM}
	$(MAKEIT) OPTS="$(KS10_OPT)"



# Front Panel API Demo/Test program

frontpaneltest : ${BIN}frontpaneltest${EXE} ${BIN}vax${EXE} 

${BIN}frontpaneltest${EXE} : frontpanel/FrontPanelTest.c sim_sock.c sim_frontpanel.c
	#cmake:ignore-target
	$(MAKEIT) OPTS="$(OS_CURSES_DEFS)" TESTS=0

else # end of primary make recipies

  # Recursion support to build simulator objects and/or binaries
  # This section exists for make recursion to achieve individual target 
  # builds.

  # potential specified input parameters
  #    OPTS      - the compile options (required)
  #    LNK_OPTS  - optional platform specific linker options
  #    TEST_NAME - the name of the simulator test script (when not simply named <simulator-name>_test.ini)
  #    ALTNAME   - an optional alternate name for the current simulator target
  
  override DEPS := $(filter %.c,$(DEPS))  # only worry about building C source modules

  ifeq (,$(OPTS))
    $(error ERROR ***  Missing build options.)
  endif

  ifneq (,$(QUIET))
    CC := @$(CC)
  endif

  # Extract source directories from the dependencies
  D0 = $(foreach dir,$(DEPS),$(dir $(dir)))
  # Isolate the directory of the first dependency
  PRIMARY_SRC = $(word 1, $(D0))
  D1 = $(sort $(D0))

  # Extract potential source code directories from the -I specifiers in the options

  space = $(empty) $(empty)
  # Combine all options separated with ^
  D2=$(subst $(space),^,^$(OPTS))
  # split the options with -I at the beginning of each element
  D3=$(subst ^-I,$(space)^-I,$(D2))
  # strip out includes for known support directories (system/dependenty includes 
  # starting with /, slirp, slirp_glue, display, etc - with or without spaces between the 
  # -I and the directory)
  D4=$(filter-out ^-I/%,$(filter-out ^-I^/%,$(filter-out ^-Islirp%,$(filter-out ^-I^slirp%,$(filter-out ^-Islirp_glue%,$(filter-out ^-I^slirp_glue%,$(filter-out ^-Idisplay%,$(filter-out ^-I^display%,$(D3)))))))))
  # remove leading element if it isn't an include
  D5=$(filter ^-I%,$(D4))
  # strip off the leading -I include specifier
  D6=$(foreach include,$(D5),$(patsubst ^-I%,%,$(include)))
  # chop off any extra options beyond the include directory
  D7=$(foreach include,$(D6),$(word 1,$(subst ^,$(space),$(include))))
  PRIMARY_INC = $(word 1, $(D7))
  DIRS = $(strip $(D7) $(D1))
  ifneq ($(WIN32),)
    pathfix = $(subst /,\,$(1))
  else
    pathfix = $(1)
  endif

  find_test = $(if $(findstring 0,$(TESTS)),, RegisterSanityCheck $(if $(abspath $(wildcard $(PRIMARY_SRC)/tests/$(1)_test.ini)),$(abspath $(wildcard $(PRIMARY_SRC)/tests/$(1)_test.ini)),$(abspath $(wildcard $(PRIMARY_INC)/tests/$(1)_test.ini))) </dev/null)

  TARGETNAME = $(basename $(notdir $(TARGET)))
  BIN = $(dir $(TARGET))
  EXE = $(suffix $(TARGET))
  BLDDIR = $(BIN)$(OSTYPE)-build/$(TARGETNAME)
  OBJS = $(addsuffix .o,$(addprefix $(BLDDIR)/,$(basename $(notdir $(DEPS)))))
  $(shell $(MKDIR) $(call pathfix,$(BLDDIR)))
  ifeq (,$(findstring 3.,$(GNUMakeVERSION)))
    define NEWLINE
$(empty)
$(empty)
endef
    MAKE_INFO = $(foreach VAR,CC OPTS LNK_OPTS DEPS LDFLAGS DIRS BUILD_SEPARATE,$(VAR)=$($(VAR))$(NEWLINE))
    PRIOR_MAKE_INFO = $(shell if ${TEST} -e $(call pathfix,$(BLDDIR)/Make.info); then cat $(call pathfix,$(BLDDIR)/Make.info); fi)
    ifneq ($(strip $(subst $(NEWLINE), ,$(MAKE_INFO))),$(strip $(PRIOR_MAKE_INFO)))
      # Different or no prior options, so start from scratch
      $(shell $(RM) $(call pathfix,$(BLDDIR)/*) $(call pathfix,$(wildcard $(TARGET))))
      $(file >$(BLDDIR)/Make.info,$(MAKE_INFO))
    endif
  endif

  ifneq (,$(and $(CPP_BUILD),$(NOCPP)))
    $(warning the $(TARGETNAME) simulator can not be built using C++)
  else

$(BLDDIR)/%.o : $(word 1,$(DIRS))/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 1,$(DIRS))/*/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 1,$(DIRS))/*/*/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : display/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : slirp/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : slirp_glue/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : %.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

ifneq (,$(word 2,$(DIRS)))
$(BLDDIR)/%.o : $(word 2,$(DIRS))/%.c
	-@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 2,$(DIRS))/*/%.c
	@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 2,$(DIRS))/*/*/%.c
	@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}
ifneq (,$(word 3,$(DIRS)))
$(BLDDIR)/%.o : $(word 3,$(DIRS))/%.c
	@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 3,$(DIRS))/*/%.c
	@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}

$(BLDDIR)/%.o : $(word 3,$(DIRS))/*/*/%.c
	@$(MKDIR) $(call pathfix,$(dir $@))
  ifneq (,$(QUIET))
	@echo Compiling $< into $@
  endif
	$(CC) -c $< -o $@ ${OPTS}
endif
endif


    ifeq (,$(TEST_NAME))
      override TEST_NAME = $(TARGETNAME)
    endif

    ifneq (,$(BUILD_SEPARATE))
# Multiple Separate compiles for each input
$(TARGET): $(OBJS)
	$(MKDIRBIN)
    ifneq (,$(QUIET))
	  @echo Linking $(TARGET)
    endif
	  ${CC} $(OBJS) ${OPTS} ${LNK_OPTS} -o $@ ${LDFLAGS}
    else
# Single Compile and Link of all inputs
$(TARGET): $(DEPS)
	$(MKDIRBIN)
    ifneq (,$(QUIET))
	  @echo Compile and Linking $(DEPS) into $(TARGET)
    endif
	${CC} $(DEPS) ${OPTS} ${LNK_OPTS} -o $@ ${LDFLAGS}
    endif
    ifneq (,$(ALTNAME))
      ifeq (${WIN32},)
	cp $(TARGET) $(@D)/$(ALTNAME)${EXE}
      else
	copy $(TARGET) $(@D)\$(ALTNAME)${EXE}
      endif
    endif
    ifneq (,$(call find_test,$(TEST_NAME)))
    # invoke the just built simulator to engage its test activities
	$@ $(call find_test,$(TEST_NAME)) ${TEST_ARG}
    endif
    ifneq (,$(SOURCE_CHECK))
	  $@ $(SOURCE_CHECK_SWITCHES) CheckSourceCode $(DEPS)
    endif

  endif  # CPP_BUILD
endif # makefile recursion build support
