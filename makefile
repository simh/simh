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
ifneq (,$(or $(findstring pdp11,$(MAKECMDGOALS)),$(findstring vax,$(MAKECMDGOALS)),$(findstring all,$(MAKECMDGOALS))))
  NETWORK_USEFUL = true
  ifneq (,$(findstring all,$(MAKECMDGOALS))$(word 2,$(MAKECMDGOALS)))
    BUILD_MULTIPLE = s
  endif
else
  ifeq ($(MAKECMDGOALS),)
    # default target is all
    NETWORK_USEFUL = true
    BUILD_MULTIPLE = s
    BUILD_SINGLE := all $(BLANK_PREFIX)
  endif
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
  GCC_VERSION = $(shell $(GCC) -v /dev/null 2>&1 | grep 'gcc version' | awk '{ print $$3 }')
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
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      # OSX's XCode gcc doesn't support LTO, but gcc built to explicitly enable it will work
      ifeq (,$(shell $(GCC) -v /dev/null 2>&1 | grep '\-\-enable-lto'))
        LTO_EXCLUDE_VERSIONS += $(GCC_VERSION)
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
        else
          ifeq (cygwin,$(OSTYPE))
            # use 0readme_ethernet.txt documented Windows pcap build components
            INCPATH += ../windows-build/winpcap/WpdPack/include
            LIBPATH += ../windows-build/winpcap/WpdPack/lib
            PCAPLIB = wpcap
            LIBEXT = a
          else
            LDSEARCH :=$(shell ldconfig -r | grep 'search directories' | awk '{print $$3}' | sed 's/:/ /g')
            ifneq (,$(LDSEARCH))
              LIBPATH := $(LDSEARCH)
            endif
            ifeq (usrpkglib,$(shell if $(TEST) -d /usr/pkg/lib; then echo usrpkglib; fi))
              LIBPATH += /usr/pkg/lib
              OS_LDFLAGS += -L/usr/pkg/lib -R/usr/pkg/lib
            endif
            ifneq (,$(findstring NetBSD,$(OSTYPE))$(findstring FreeBSD,$(OSTYPE)))
              LIBEXT = so
            else
              LIBEXT = a
            endif
          endif
        endif
      endif
    endif
  endif
  $(info lib paths are: $(LIBPATH))
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
      OS_CCDEFS += -DUSE_READER_THREAD
      ifeq (,$(NOASYNCH))
        OS_CCDEFS += -DSIM_ASYNCH_IO 
      endif
      OS_LDFLAGS += -lpthread
      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
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
      ifneq (,$(call find_lib,$(PCAPLIB)))
        ifneq ($(USE_NETWORK),) # Network support specified on the GNU make command line
          NETWORK_CCDEFS = -DUSE_NETWORK -I$(dir $(call find_include,pcap))
          ifeq (cygwin,$(OSTYPE))
            # cygwin has no ldconfig so explicitly specify pcap object library
            NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          else
            NETWORK_LDFLAGS = -l$(PCAPLIB)
          endif
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
        else # default build uses dynamic libpcap
          NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap))
          $(info using libpcap: $(call find_include,pcap))
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
        endif
      else
        NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap))
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
  ifneq (binexists,$(shell if $(TEST) -e BIN; then echo binexists; fi))
    MKDIRBIN = if $(TEST) ! -e BIN; then mkdir BIN; fi
  endif
else
  #Win32 Environments (via MinGW32)
  GCC = gcc
  GCC_Path := $(dir $(shell where gcc.exe))
  GCC_VERSION = $(word 3,$(shell $(GCC) --version))
  LTO_EXCLUDE_VERSIONS = 4.5.2
  ifeq (pthreads,$(shell if exist ..\windows-build\pthreads\Pre-built.2\include\pthread.h echo pthreads))
    PTHREADS_CCDEFS = -DUSE_READER_THREAD -DPTW32_STATIC_LIB -I../windows-build/pthreads/Pre-built.2/include
    ifeq (,$(NOASYNCH))
      PTHREADS_CCDEFS += -DSIM_ASYNCH_IO 
    endif
    PTHREADS_LDFLAGS = -lpthreadGC2 -L..\windows-build\pthreads\Pre-built.2\lib
  else
    ifeq (pthreads,$(shell if exist $(dir $(GCC_Path))..\include\pthread.h echo pthreads))
      PTHREADS_CCDEFS = -DUSE_READER_THREAD
      ifeq (,$(NOASYNCH))
        PTHREADS_CCDEFS += -DSIM_ASYNCH_IO 
      endif
      PTHREADS_LDFLAGS = -lpthread
    endif
  endif
  ifeq (pcap,$(shell if exist ..\windows-build\winpcap\Wpdpack\include\pcap.h echo pcap))
    PCAP_CCDEFS = -I../windows-build/winpcap/Wpdpack/include -I$(GCC_Path)..\include\ddk -DUSE_SHARED
    NETWORK_LDFLAGS =
    NETWORK_OPT = -DUSE_SHARED
    NETWORK_FEATURES = - dynamic networking support using windows-build provided libpcap components
  else
    ifeq (pcap,$(shell if exist $(dir $(GCC_Path))..\include\pcap.h echo pcap))
      PCAP_CCDEFS = -DUSE_SHARED -I$(GCC_Path)..\include\ddk
      NETWORK_LDFLAGS =
      NETWORK_OPT = -DUSE_SHARED
      NETWORK_FEATURES = - dynamic networking support using libpcap components found in the MinGW directories
    endif
  endif
  OS_CCDEFS =  -fms-extensions $(PTHREADS_CCDEFS) $(PCAP_CCDEFS)
  OS_LDFLAGS = -lm -lwsock32 -lwinmm $(PTHREADS_LDFLAGS)
  EXE = .exe
  ifneq (binexists,$(shell if exist BIN echo binexists))
    MKDIRBIN = if not exist BIN mkdir BIN
  endif
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
  BUILD_FEATURES := $(BUILD_FEATURES). GCC Version: $(GCC_VERSION)
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

CC_STD = -std=c99
CC_OUTSPEC = -o $@
CC = $(GCC) $(CC_STD) -U__STRICT_ANSI__ $(CFLAGS_G) $(CFLAGS_O) -I . $(OS_CCDEFS) $(ROMS_OPT)
LDFLAGS = $(OS_LDFLAGS) $(NETWORK_LDFLAGS) $(LDFLAGS_O)

#
# Common Libraries
#
BIN = BIN/
SIM = scp.c sim_console.c sim_fio.c sim_timer.c sim_sock.c \
	sim_tmxr.c sim_ether.c sim_tape.c


#
# Emulator source files and compile time options
#
PDP1D = PDP1
PDP1 = ${PDP1D}/pdp1_lp.c ${PDP1D}/pdp1_cpu.c ${PDP1D}/pdp1_stddev.c \
	${PDP1D}/pdp1_sys.c ${PDP1D}/pdp1_dt.c ${PDP1D}/pdp1_drm.c \
	${PDP1D}/pdp1_clk.c ${PDP1D}/pdp1_dcs.c
PDP1_OPT = -I ${PDP1D}


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
	${PDP11D}/pdp11_ke.c ${PDP11D}/pdp11_dc.c ${PDP11D}/pdp11_io_lib.c
PDP11_OPT = -DVM_PDP11 -I ${PDP11D} ${NETWORK_OPT}


VAXD = VAX
VAX = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c ${VAXD}/vax_io.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_stddev.c ${VAXD}/vax_sysdev.c \
	${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c ${VAXD}/vax_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xq.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_vh.c \
	${PDP11D}/pdp11_cr.c ${PDP11D}/pdp11_io_lib.c
VAX_OPT = -DVM_VAX -DUSE_INT64 -DUSE_ADDR64 -I ${VAXD} -I ${PDP11D} ${NETWORK_OPT}


VAX780 = ${VAXD}/vax_cpu.c ${VAXD}/vax_cpu1.c ${VAXD}/vax_fpa.c \
	${VAXD}/vax_cis.c ${VAXD}/vax_octa.c  ${VAXD}/vax_cmode.c \
	${VAXD}/vax_mmu.c ${VAXD}/vax_sys.c  ${VAXD}/vax_syscm.c \
	${VAXD}/vax780_stddev.c ${VAXD}/vax780_sbi.c \
	${VAXD}/vax780_mem.c ${VAXD}/vax780_uba.c ${VAXD}/vax780_mba.c \
	${VAXD}/vax780_fload.c ${VAXD}/vax780_syslist.c \
	${PDP11D}/pdp11_rl.c ${PDP11D}/pdp11_rq.c ${PDP11D}/pdp11_ts.c \
	${PDP11D}/pdp11_dz.c ${PDP11D}/pdp11_lp.c ${PDP11D}/pdp11_tq.c \
	${PDP11D}/pdp11_xu.c ${PDP11D}/pdp11_ry.c ${PDP11D}/pdp11_cr.c \
	${PDP11D}/pdp11_rp.c ${PDP11D}/pdp11_tu.c ${PDP11D}/pdp11_hk.c \
	${PDP11D}/pdp11_io_lib.c
VAX780_OPT = -DVM_VAX -DVAX_780 -DUSE_INT64 -DUSE_ADDR64 -I VAX -I ${PDP11D} ${NETWORK_OPT}


PDP10D = PDP10
PDP10 = ${PDP10D}/pdp10_fe.c ${PDP11D}/pdp11_dz.c ${PDP10D}/pdp10_cpu.c \
	${PDP10D}/pdp10_ksio.c ${PDP10D}/pdp10_lp20.c ${PDP10D}/pdp10_mdfp.c \
	${PDP10D}/pdp10_pag.c ${PDP10D}/pdp10_rp.c ${PDP10D}/pdp10_sys.c \
	${PDP10D}/pdp10_tim.c ${PDP10D}/pdp10_tu.c ${PDP10D}/pdp10_xtnd.c \
	${PDP11D}/pdp11_pt.c ${PDP11D}/pdp11_ry.c \
	${PDP11D}/pdp11_cr.c
PDP10_OPT = -DVM_PDP10 -DUSE_INT64 -I ${PDP10D} -I ${PDP11D}



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
	${H316D}/h316_dp.c
H316_OPT = -I ${H316D}


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
	${HP2100D}/hp2100_di_da.c ${HP2100D}/hp_disclib.c
HP2100_OPT = -DHAVE_INT64 -I ${HP2100D}


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
	${ALTAIRZ80D}/i8272.c ${ALTAIRZ80D}/insnsd.c \
	${ALTAIRZ80D}/mfdc.c ${ALTAIRZ80D}/n8vem.c ${ALTAIRZ80D}/vfdhd.c \
	${ALTAIRZ80D}/s100_disk1a.c ${ALTAIRZ80D}/s100_disk2.c ${ALTAIRZ80D}/s100_disk3.c\
	${ALTAIRZ80D}/s100_fif.c ${ALTAIRZ80D}/s100_mdriveh.c \
	${ALTAIRZ80D}/s100_mdsad.c ${ALTAIRZ80D}/s100_selchan.c \
	${ALTAIRZ80D}/s100_ss1.c ${ALTAIRZ80D}/s100_64fdc.c \
	${ALTAIRZ80D}/s100_scp300f.c ${ALTAIRZ80D}/sim_imd.c \
	${ALTAIRZ80D}/wd179x.c ${ALTAIRZ80D}/s100_hdc1001.c \
	${ALTAIRZ80D}/s100_if3.c ${ALTAIRZ80D}/s100_adcs6.c
ALTAIRZ80_OPT = -I ${ALTAIRZ80D}


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


#
# Build everything
#
ALL = pdp1 pdp4 pdp7 pdp8 pdp9 pdp15 pdp11 pdp10 \
	vax vax780 nova eclipse hp2100 i1401 i1620 s3 \
	altair altairz80 gri i7094 ibm1130 id16 \
	id32 sds lgp h316 swtp6800mp-a swtp6800mp-a2

all : ${ALL}

clean :
ifeq ($(WIN32),)
	${RM} -r ${BIN}
else
	if exist BIN\*.exe del /q BIN\*.exe
	if exist BIN rmdir BIN
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

pdp11 : ${BIN}pdp11${EXE}

${BIN}pdp11${EXE} : ${PDP11} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP11} ${SIM} ${PDP11_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax : ${BIN}vax${EXE}

${BIN}vax${EXE} : ${VAX} ${SIM}
	${MKDIRBIN}
	${CC} ${VAX} ${SIM} ${VAX_OPT} $(CC_OUTSPEC) ${LDFLAGS}

vax780 : ${BIN}vax780${EXE}

${BIN}vax780${EXE} : ${VAX780} ${SIM}
	${MKDIRBIN}
	${CC} ${VAX780} ${SIM} ${VAX780_OPT} $(CC_OUTSPEC) ${LDFLAGS}

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
	${CC} ${IBM1130} ${SIM} ${IBM1130_OPT} $(CC_OUTSPEC) ${LDFLAGS}

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

swtp6800mp-a : ${BIN}swtp6800mp-a${EXE}

${BIN}swtp6800mp-a${EXE} : ${SWTP6800MP-A} ${SIM}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}

swtp6800mp-a2 : ${BIN}swtp6800mp-a2${EXE}

${BIN}swtp6800mp-a2${EXE} : ${SWTP6800MP-A2} ${SIM}
	${MKDIRBIN}
	${CC} ${SWTP6800MP-A2} ${SIM} ${SWTP6800_OPT} $(CC_OUTSPEC) ${LDFLAGS}
