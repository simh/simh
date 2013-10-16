# DESCRIP.MMS
# Written By:   Robert Alan Byer / byer@mail.ourservers.net
# Modified By:  Mark Pizzolato / mark@infocomm.com
#               Norman Lastovica / norman.lastovica@oracle.com
#               Camiel Vanderhoeven / camiel@camicom.com
#               Matt Burke / scope.matthew@btinternet.com
#
# This MMS/MMK build script is used to compile the various simulators in
# the SIMH package for OpenVMS using DEC C v6.0-001(AXP), v6.5-001(AXP),
# HP C V7.3-009-48GBT (AXP), HP C V7.2-001 (IA64) and v6.4-005(VAX).
#
# Notes:  On VAX, the PDP-10, Eclipse and IBM 7094 simulators will not be 
#         built due to the fact that INT64 is required for these simulators.
#
# This build script will accept the following build options.
#
#            ALL             Just Build "Everything".
#            ALTAIR          Just Build The MITS Altair.
#            ALTAIRZ80       Just Build The MITS Altair Z80.
#            ECLIPSE         Just Build The Data General Eclipse.
#            GRI             Just Build The GRI Corporation GRI-909.
#            LGP             Just Build The Royal-McBee LGP-30.
#            H316            Just Build The Honewell 316/516.
#            HP2100          Just Build The Hewlett-Packard HP-2100. 
#            I1401           Just Build The IBM 1401.
#            I1620           Just Build The IBM 1620.
#            I7094           Just Build The IBM 7094.
#            IBM1130         Just Build The IBM 1130.
#            ID16            Just Build The Interdata 16-bit CPU.
#            ID32            Just Build The Interdata 32-bit CPU.
#            NOVA            Just Build The Data General Nova.
#            PDP1            Just Build The DEC PDP-1.
#            PDP4            Just Build The DEC PDP-4.
#            PDP7            Just Build The DEC PDP-7.
#            PDP8            Just Build The DEC PDP-8.
#            PDP9            Just Build The DEC PDP-9.
#            PDP10           Just Build The DEC PDP-10.
#            PDP11           Just Build The DEC PDP-11.
#            PDP15           Just Build The DEC PDP-15.
#            S3              Just Build The IBM System 3.
#            SDS             Just Build The SDS 940.
#            SSEM            Just Build the Manchester University SSEM.
#            SWTP6800MP-A    Just Build The SWTP6800MP-A.
#            SWTP6800MP-A2   Just Build The SWTP6800MP-A2.
#            VAX             Just Build The DEC MicroVAX3900 (aka VAX).
#            MicroVAX3900    Just Build The DEC MicroVAX3900 (aka VAX).
#            MicroVAX1       Just Build The DEC MicroVAX1 (MicroVAX I).
#            rtVAX1000       Just Build The DEC rtVAX1000 (rtVAX 1000).
#            MicroVAX2       Just Build The DEC MicroVAX2 (MicroVAX II).
#            VAX730          Just Build The DEC VAX730.
#            VAX750          Just Build The DEC VAX750.
#            VAX780          Just Build The DEC VAX780.
#            VAX8600         Just Build The DEC VAX8600.
#            CLEAN           Will Clean Files Back To Base Kit.
#
# To build with debugging enabled (which will also enable traceback 
# information) use..
#
#        MMK/MACRO=(DEBUG=1)
#
# This will produce an executable named {Simulator}-{I64|VAX|AXP}-DBG.EXE
#
# To build on older Alpha VMS platforms, SIM_ASYNCH_IO must be disabled. 
# use..
#
#        MMK/MACRO=(NOASYNCH=1)
#
# On AXP and IA64 the VMS PCAP components are built and used to provide 
# network support for the VAX and PDP11 simulators.
#
# The AXP PCAP components can only be built using a version of the 
# DEC/Compaq/HP Compiler version V6.5-001 or later.  To build using an
# older compiler, networking support must be disabled.  Use...
#
#        MMK/MACRO=(NONETWORK=1)
#
# The PCAP-VMS components are presumed (by this procedure) to be located
# in a directory at the same level as the directory containing the
# simh source files.  For example, if these exist here:
#
#   []descrip.mms
#   []scp.c
#   etc.
#
# Then the following should exist:
#   [-.PCAP-VMS]BUILD_ALL.COM
#   [-.PCAP-VMS.PCAP-VCI]
#   [-.PCAP-VMS.PCAPVCM]
#   etc.

# Let's See If We Are Going To Build With DEBUG Enabled.  Always compile
# /DEBUG so that the traceback and debug information is always available
# in the object files.

CC_DEBUG = /DEBUG

.IFDEF DEBUG
CC_OPTIMIZE = /NOOPTIMIZE
NEST_DEBUG = ,DEBUG=1

.IFDEF MMSALPHA
ALPHA_OR_IA64 = 1
CC_FLAGS = /PREF=ALL
.IFDEF NOASYNCH
ARCH = AXP-NOASYNCH-DBG
CC_DEFS = "_LARGEFILE"
LINK_DEBUG = /DEBUG/TRACEBACK
.ELSE
ARCH = AXP-DBG
CC_DEFS = "_LARGEFILE","SIM_ASYNCH_IO=1"
LINK_DEBUG = /DEBUG/TRACEBACK/THREADS_ENABLE
.ENDIF
.ENDIF

.IFDEF MMSIA64
ALPHA_OR_IA64 = 1
CC_FLAGS = /PREF=ALL
.IFDEF NOASYNCH
ARCH = I64-NOASYNCH-DBG
CC_DEFS = "_LARGEFILE"
LINK_DEBUG = /DEBUG/TRACEBACK
.ELSE
ARCH = I64-DBG
CC_DEFS = "_LARGEFILE","SIM_ASYNCH_IO=1"
LINK_DEBUG = /DEBUG/TRACEBACK/THREADS_ENABLE
.ENDIF
.ENDIF

.IFDEF MMSVAX
CC_FLAGS = $(CC_FLAGS)
ARCH = VAX-DBG
CC_DEFS = "__VAX"
LINK_DEBUG = /DEBUG/TRACEBACK
.ENDIF

.ELSE
# !DEBUG

.IFDEF MMSALPHA
ALPHA_OR_IA64 = 1
CC_OPTIMIZE = /OPT=(LEV=5)/ARCH=HOST
CC_FLAGS = /PREF=ALL
.IFDEF NOASYNCH
ARCH = AXP-NOASYNCH
CC_DEFS = "_LARGEFILE"
LINK_DEBUG = /NODEBUG/NOTRACEBACK
.ELSE
ARCH = AXP
CC_DEFS = "_LARGEFILE","SIM_ASYNCH_IO=1"
LINK_DEBUG = /NODEBUG/NOTRACEBACK/THREADS_ENABLE
.ENDIF
LINK_SECTION_BINDING = /SECTION_BINDING
.ENDIF

.IFDEF MMSIA64
ALPHA_OR_IA64 = 1
CC_OPTIMIZE = /OPT=(LEV=5)
CC_FLAGS = /PREF=ALL
.IFDEF NOASYNCH
ARCH = I64-NOASYNCH
CC_DEFS = "_LARGEFILE"
LINK_DEBUG = /NODEBUG/NOTRACEBACK
.ELSE
ARCH = I64
CC_DEFS = "_LARGEFILE","SIM_ASYNCH_IO=1"
LINK_DEBUG = /NODEBUG/NOTRACEBACK/THREADS_ENABLE
.ENDIF
.ENDIF

.IFDEF MMSVAX
CC_OPTIMIZE = /OPTIMIZE
CC_FLAGS = $(CC_FLAGS)
ARCH = VAX
CC_DEFS = "__VAX"
LINK_DEBUG = /NODEBUG/NOTRACEBACK
.ENDIF

.ENDIF


# Define Our Compiler Flags & Define The Compile Command
OUR_CC_FLAGS = $(CC_FLAGS)$(CC_DEBUG)$(CC_OPTIMIZE) \
               /NEST=PRIMARY/NAME=(AS_IS,SHORT)
CC = CC/DECC$(OUR_CC_FLAGS)

# Define The BIN Directory Where The Executables Will Go.
# Define Our Library Directory.
# Define The platform specific Build Directory Where The Objects Will Go.
#
BIN_DIR = SYS$DISK:[.BIN]
LIB_DIR = SYS$DISK:[.BIN.VMS.LIB]
BLD_DIR = SYS$DISK:[.BIN.VMS.LIB.BLD-$(ARCH)]


# Core SIMH File Definitions.
#
SIMH_DIR = SYS$DISK:[]
SIMH_LIB = $(LIB_DIR)SIMH-$(ARCH).OLB
SIMH_NONET_LIB = $(LIB_DIR)SIMH-NONET-$(ARCH).OLB
SIMH_SOURCE = $(SIMH_DIR)SIM_CONSOLE.C,$(SIMH_DIR)SIM_SOCK.C,\
              $(SIMH_DIR)SIM_TMXR.C,$(SIMH_DIR)SIM_ETHER.C,\
              $(SIMH_DIR)SIM_TAPE.C,$(SIMH_DIR)SIM_FIO.C,\
              $(SIMH_DIR)SIM_TIMER.C,$(SIMH_DIR)SIM_DISK.C,\
              $(SIMH_DIR)SIM_SERIAL.C
SIMH_MAIN = SCP.C
.IFDEF ALPHA_OR_IA64
SIMH_LIB64 = $(LIB_DIR)SIMH64-$(ARCH).OLB
.ENDIF

# VMS PCAP File Definitions.
#
PCAP_DIR = SYS$DISK:[-.PCAP-VMS.PCAP-VCI]
PCAP_LIB = $(LIB_DIR)PCAP-$(ARCH).OLB
PCAP_SOURCE = $(PCAP_DIR)PCAPVCI.C,$(PCAP_DIR)VCMUTIL.C,\
              $(PCAP_DIR)BPF_DUMP.C,$(PCAP_DIR)BPF_FILTER.C,\
              $(PCAP_DIR)BPF_IMAGE.C,$(PCAP_DIR)ETHERENT.C,\
              $(PCAP_DIR)FAD-GIFC.C,$(PCAP_DIR)GENCODE.C,\
              $(PCAP_DIR)GRAMMAR.C,$(PCAP_DIR)INET.C,\
              $(PCAP_DIR)NAMETOADDR.C,$(PCAP_DIR)OPTIMIZE.C,\
              $(PCAP_DIR)PCAP.C,$(PCAP_DIR)SAVEFILE.C,\
              $(PCAP_DIR)SCANNER.C,$(PCAP_DIR)SNPRINTF.C,\
              $(PCAP_DIR)PCAP-VMS.C
PCAP_VCMDIR = SYS$DISK:[-.PCAP-VMS.PCAPVCM]
PCAP_VCM_SOURCES = $(PCAP_VCMDIR)PCAPVCM.C,$(PCAP_VCMDIR)PCAPVCM_INIT.MAR,\
                   $(PCAP_VCMDIR)VCI_JACKET.MAR,$(PCAP_VCMDIR)VCMUTIL.C
PCAP_VCI = SYS$COMMON:[SYS$LDR]PCAPVCM.EXE

# PCAP is not available on OpenVMS VAX
#
.IFDEF ALPHA_OR_IA64
.IFDEF NONETWORK
# Network Capabilities disabled
.ELSE
PCAP_EXECLET = $(PCAP_VCI)
PCAP_INC = ,$(PCAP_DIR)
PCAP_LIBD = $(PCAP_LIB)
PCAP_LIBR = ,$(PCAP_LIB)/LIB/SYSEXE
PCAP_DEFS = ,"USE_NETWORK=1"
PCAP_SIMH_INC = /INCL=($(PCAP_DIR))
.ENDIF
.ENDIF

# Check To Make Sure We Have SYS$DISK:[.BIN] & SYS$DISK:[.LIB] Directory.
#
.FIRST
  @ IF "".NES."''CC'" THEN DELETE/SYMBOL/GLOBAL CC
  @ EXIT_ON_ERROR := IF (ERROR_CONDITION) THEN EXIT %X10000004
  @ ERROR_CONDITION = ((F$GETSYI("ARCH_NAME").EQS."Alpha").AND.(F$GETSYI("VERSION").LTS."V8.0").AND.("$(NOASYNCH)".EQS.""))
  @ IF (ERROR_CONDITION) THEN WRITE SYS$OUTPUT "*** WARNING **** Build should be invoked with /MACRO=NOASYNCH=1 on this platform"
  @ 'EXIT_ON_ERROR
  @ DEFINE/USER SYS$ERROR NLA0:
  @ DEFINE/USER SYS$OUTPUT CC_VERSION.DAT
  @ CC/DECC/VERSION
  @ OPEN /READ VERSION CC_VERSION.DAT
  @ READ VERSION CC_VERSION
  @ CLOSE VERSION
  @ DELETE CC_VERSION.DAT;
  @ CC_VERSION = F$ELEMENT(2," ",CC_VERSION)
  @ BAD_CC_VERSION = ((F$GETSYI("ARCH_NAME").EQS."Alpha").AND.(CC_VERSION.LTS."V6.5-001").AND.("$(NONETWORK)".EQS.""))
  @ IF (BAD_CC_VERSION) THEN WRITE SYS$OUTPUT "*** WARNING *** C Compiler is: ''CC_VERSION'"
  @ IF (BAD_CC_VERSION.AND.(F$GETSYI("VERSION").GES."V8.0")) THEN -
     WRITE SYS$OUTPUT "*** WARNING *** Build should be invoked with /MACRO=NONETWORK=1 with this compiler"
  @ IF (BAD_CC_VERSION.AND.(F$GETSYI("VERSION").LTS."V8.0")) THEN -
     WRITE SYS$OUTPUT "*** WARNING *** Build should be invoked with /MACRO=(NONETWORK=1,NOASYNCH=1) with this compiler"
  @ ERROR_CONDITION = BAD_CC_VERSION
  @ 'EXIT_ON_ERROR
  @ MISSING_PCAP = (("$(PCAP_EXECLET)".NES."").AND.("$(NONETWORK)".EQS."").AND.(F$SEARCH("$(PCAP_DIR)PCAP-VMS.C").EQS.""))
  @ MISS_SAY := IF (MISSING_PCAP) THEN WRITE SYS$OUTPUT
  @ 'MISS_SAY' "*** Error *** Attempting a Network Build but the VMS-PCAP components are not"
  @ 'MISS_SAY' "*** Error *** available"
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error *** The vms-pcap.zip file can be downloaded from:"
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error ***     https://github.com/markpizz/simh/downloads"
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error *** Be sure to ""unzip -a vms-pcap"" to properly set the file attributes"
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error *** The PCAP-VMS components are presumed (by this procedure) to be"
  @ 'MISS_SAY' "*** Error *** located in a directory at the same level as the directory"
  @ 'MISS_SAY' "*** Error *** containing the simh source files."
  @ 'MISS_SAY' "*** Error *** For example, if these exist here:"
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error ***   []descrip.mms"
  @ 'MISS_SAY' "*** Error ***   []scp.c"
  @ 'MISS_SAY' "*** Error ***   etc."
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error *** Then the following should exist:"
  @ 'MISS_SAY' "*** Error ***   [-.PCAP-VMS]BUILD_ALL.COM"
  @ 'MISS_SAY' "*** Error ***   [-.PCAP-VMS.PCAP-VCI]"
  @ 'MISS_SAY' "*** Error ***   [-.PCAP-VMS.PCAPVCM]"
  @ 'MISS_SAY' "*** Error ***   etc."
  @ 'MISS_SAY' "*** Error *** "
  @ 'MISS_SAY' "*** Error *** Aborting Build"
  @ ERROR_CONDITION = MISSING_PCAP
  @ 'EXIT_ON_ERROR
  @ IF (F$SEARCH("SYS$DISK:[]BIN.DIR").EQS."") THEN CREATE/DIRECTORY $(BIN_DIR)
  @ IF (F$SEARCH("SYS$DISK:[.BIN]VMS.DIR").EQS."") THEN CREATE/DIRECTORY $(LIB_DIR)
  @ IF (F$SEARCH("SYS$DISK:[.BIN.VMS]LIB.DIR").EQS."") THEN CREATE/DIRECTORY $(LIB_DIR)
  @ IF (F$SEARCH("SYS$DISK:[.BIN.VMS.LIB]BLD-$(ARCH).DIR").EQS."") THEN CREATE/DIRECTORY $(BLD_DIR)
  @ IF (F$SEARCH("$(BLD_DIR)*.*").NES."") THEN DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.*;*
  @ IF (("$(BUILDING_ROMS)".EQS."").AND.(F$SEARCH("$(BIN_DIR)BuildROMs-$(ARCH).EXE").EQS."")) THEN $(MMS) BUILDROMS/MACRO=(BUILDING_ROMS=1$(NEST_DEBUG))


# MITS Altair Simulator Definitions.
#
ALTAIR_DIR = SYS$DISK:[.ALTAIR]
ALTAIR_LIB = $(LIB_DIR)ALTAIR-$(ARCH).OLB
ALTAIR_SOURCE = $(ALTAIR_DIR)ALTAIR_SIO.C,$(ALTAIR_DIR)ALTAIR_CPU.C,\
                $(ALTAIR_DIR)ALTAIR_DSK.C,$(ALTAIR_DIR)ALTAIR_SYS.C
ALTAIR_OPTIONS = /INCL=($(SIMH_DIR),$(ALTAIR_DIR))/DEF=($(CC_DEFS))

#
# MITS Altair Z80 Simulator Definitions.
#
ALTAIRZ80_DIR = SYS$DISK:[.ALTAIRZ80]
ALTAIRZ80_LIB1 = $(LIB_DIR)ALTAIRZ80L1-$(ARCH).OLB
ALTAIRZ80_SOURCE1 = $(ALTAIRZ80_DIR)ALTAIRZ80_CPU.C,$(ALTAIRZ80_DIR)ALTAIRZ80_CPU_NOMMU.C,\
                    $(ALTAIRZ80_DIR)ALTAIRZ80_DSK.C,$(ALTAIRZ80_DIR)DISASM.C,\
                    $(ALTAIRZ80_DIR)ALTAIRZ80_SIO.C,$(ALTAIRZ80_DIR)ALTAIRZ80_SYS.C,\
                    $(ALTAIRZ80_DIR)ALTAIRZ80_HDSK.C,$(ALTAIRZ80_DIR)ALTAIRZ80_NET.C,\
                    $(ALTAIRZ80_DIR)FLASHWRITER2.C,$(ALTAIRZ80_DIR)I86_DECODE.C,\
                    $(ALTAIRZ80_DIR)I86_OPS.C,$(ALTAIRZ80_DIR)I86_PRIM_OPS.C,\
                    $(ALTAIRZ80_DIR)I8272.C,$(ALTAIRZ80_DIR)INSNSD.C,\
                    $(ALTAIRZ80_DIR)MFDC.C,$(ALTAIRZ80_DIR)N8VEM.C,$(ALTAIRZ80_DIR)VFDHD.C
ALTAIRZ80_LIB2 = $(LIB_DIR)ALTAIRZ80L2-$(ARCH).OLB
ALTAIRZ80_SOURCE2 = $(ALTAIRZ80_DIR)S100_DISK1A.C,$(ALTAIRZ80_DIR)S100_DISK2.C,\
                    $(ALTAIRZ80_DIR)S100_FIF.C,$(ALTAIRZ80_DIR)S100_MDRIVEH.C,\
                    $(ALTAIRZ80_DIR)S100_MDSAD.C,$(ALTAIRZ80_DIR)S100_SELCHAN.C,\
                    $(ALTAIRZ80_DIR)S100_SS1.C,$(ALTAIRZ80_DIR)S100_64FDC.C,\
                    $(ALTAIRZ80_DIR)S100_SCP300F.C,$(ALTAIRZ80_DIR)SIM_IMD.C,\
                    $(ALTAIRZ80_DIR)WD179X.C,$(ALTAIRZ80_DIR)S100_DISK3.C,\
                    $(ALTAIRZ80_DIR)S100_ADCS6.C,$(ALTAIRZ80_DIR)S100_HDC1001.C,\
                    $(ALTAIRZ80_DIR)S100_IF3.C
ALTAIRZ80_OPTIONS = /INCL=($(SIMH_DIR),$(ALTAIRZ80_DIR))/DEF=($(CC_DEFS))

#
# Data General Nova Simulator Definitions.
#
NOVA_DIR = SYS$DISK:[.NOVA]
NOVA_LIB = $(LIB_DIR)NOVA-$(ARCH).OLB
NOVA_SOURCE = $(NOVA_DIR)NOVA_SYS.C,$(NOVA_DIR)NOVA_CPU.C,\
              $(NOVA_DIR)NOVA_DKP.C,$(NOVA_DIR)NOVA_DSK.C,\
              $(NOVA_DIR)NOVA_LP.C,$(NOVA_DIR)NOVA_MTA.C,\
              $(NOVA_DIR)NOVA_PLT.C,$(NOVA_DIR)NOVA_PT.C,\
              $(NOVA_DIR)NOVA_CLK.C,$(NOVA_DIR)NOVA_TT.C,\
              $(NOVA_DIR)NOVA_TT1.C,$(NOVA_DIR)NOVA_QTY.C
NOVA_OPTIONS = /INCL=($(SIMH_DIR),$(NOVA_DIR))/DEF=($(CC_DEFS))

#
# Data General Eclipse Simulator Definitions.
#
ECLIPSE_LIB = $(LIB_DIR)ECLIPSE-$(ARCH).OLB
ECLIPSE_SOURCE = $(NOVA_DIR)ECLIPSE_CPU.C,$(NOVA_DIR)ECLIPSE_TT.C,\
                 $(NOVA_DIR)NOVA_SYS.C,$(NOVA_DIR)NOVA_DKP.C,\
                 $(NOVA_DIR)NOVA_DSK.C,$(NOVA_DIR)NOVA_LP.C,\
                 $(NOVA_DIR)NOVA_MTA.C,$(NOVA_DIR)NOVA_PLT.C,\
                 $(NOVA_DIR)NOVA_PT.C,$(NOVA_DIR)NOVA_CLK.C,\
                 $(NOVA_DIR)NOVA_TT1.C,$(NOVA_DIR)NOVA_QTY.C
ECLIPSE_OPTIONS = /INCL=($(SIMH_DIR),$(NOVA_DIR))\
                    /DEF=($(CC_DEFS),"ECLIPSE=1")

#
# GRI Corporation GRI-909 Simulator Definitions.
#
GRI_DIR = SYS$DISK:[.GRI]
GRI_LIB = $(LIB_DIR)GRI-$(ARCH).OLB
GRI_SOURCE = $(GRI_DIR)GRI_CPU.C,$(GRI_DIR)GRI_STDDEV.C,$(GRI_DIR)GRI_SYS.C
GRI_OPTIONS = /INCL=($(SIMH_DIR),$(GRI_DIR))/DEF=($(CC_DEFS))

#
# Royal-McBee LGP-30 Simulator Definitions.
#
LGP_DIR = SYS$DISK:[.LGP]
LGP_LIB = $(LIB_DIR)LGP-$(ARCH).OLB
LGP_SOURCE = $(LGP_DIR)LGP_CPU.C,$(LGP_DIR)LGP_STDDEV.C,$(LGP_DIR)LGP_SYS.C
LGP_OPTIONS = /INCL=($(SIMH_DIR),$(LGP_DIR))/DEF=($(CC_DEFS))

#
# Honeywell 316/516 Simulator Definitions.
#
H316_DIR = SYS$DISK:[.H316]
H316_LIB = $(LIB_DIR)H316-$(ARCH).OLB
H316_SOURCE = $(H316_DIR)H316_STDDEV.C,$(H316_DIR)H316_LP.C,\
              $(H316_DIR)H316_CPU.C,$(H316_DIR)H316_SYS.C,\
              $(H316_DIR)H316_FHD.C,$(H316_DIR)H316_MT.C,\
              $(H316_DIR)H316_DP.C
H316_OPTIONS = /INCL=($(SIMH_DIR),$(H316_DIR))/DEF=($(CC_DEFS))

#
# Hewlett-Packard HP-2100 Simulator Definitions.
#
HP2100_DIR = SYS$DISK:[.HP2100]
HP2100_LIB1 = $(LIB_DIR)HP2100L1-$(ARCH).OLB
HP2100_SOURCE1 = $(HP2100_DIR)HP2100_STDDEV.C,$(HP2100_DIR)HP2100_DP.C,\
                 $(HP2100_DIR)HP2100_DQ.C,$(HP2100_DIR)HP2100_DR.C,\
                 $(HP2100_DIR)HP2100_LPS.C,$(HP2100_DIR)HP2100_MS.C,\
                 $(HP2100_DIR)HP2100_MT.C,$(HP2100_DIR)HP2100_MUX.C,\
                 $(HP2100_DIR)HP2100_CPU.C,$(HP2100_DIR)HP2100_FP.C,\
                 $(HP2100_DIR)HP2100_SYS.C,$(HP2100_DIR)HP2100_LPT.C,\
                 $(HP2100_DIR)HP2100_IPL.C,$(HP2100_DIR)HP2100_DS.C,\
                 $(HP2100_DIR)HP2100_CPU0.C,$(HP2100_DIR)HP2100_CPU1.C,\
                 $(HP2100_DIR)HP2100_CPU2.C,$(HP2100_DIR)HP2100_CPU3.C,\
                 $(HP2100_DIR)HP2100_CPU4.C,$(HP2100_DIR)HP2100_CPU5.C,\
                 $(HP2100_DIR)HP2100_CPU6.C,$(HP2100_DIR)HP2100_CPU7.C
HP2100_LIB2 = $(LIB_DIR)HP2100L2-$(ARCH).OLB
HP2100_SOURCE2 = $(HP2100_DIR)HP2100_FP1.C,$(HP2100_DIR)HP2100_BACI.C,\
                 $(HP2100_DIR)HP2100_MPX.C,$(HP2100_DIR)HP2100_PIF.C,\
                 $(HP2100_DIR)HP2100_DI.C,$(HP2100_DIR)HP2100_DI_DA.C,\
                 $(HP2100_DIR)HP_DISCLIB.C
.IFDEF ALPHA_OR_IA64
HP2100_OPTIONS = /INCL=($(SIMH_DIR),$(HP2100_DIR))\
                    /DEF=($(CC_DEFS),"HAVE_INT64=1")
.ELSE
HP2100_OPTIONS = /INCL=($(SIMH_DIR),$(HP2100_DIR))/DEF=($(CC_DEFS))
.ENDIF

#
# Interdata 16-bit CPU.
#
ID16_DIR = SYS$DISK:[.INTERDATA]
ID16_LIB = $(LIB_DIR)ID16-$(ARCH).OLB
ID16_SOURCE = $(ID16_DIR)ID16_CPU.C,$(ID16_DIR)ID16_SYS.C,$(ID16_DIR)ID_DP.C,\
              $(ID16_DIR)ID_FD.C,$(ID16_DIR)ID_FP.C,$(ID16_DIR)ID_IDC.C,\
              $(ID16_DIR)ID_IO.C,$(ID16_DIR)ID_LP.C,$(ID16_DIR)ID_MT.C,\
              $(ID16_DIR)ID_PAS.C,$(ID16_DIR)ID_PT.C,$(ID16_DIR)ID_TT.C,\
              $(ID16_DIR)ID_UVC.C,$(ID16_DIR)ID16_DBOOT.C,$(ID16_DIR)ID_TTP.C
ID16_OPTIONS = /INCL=($(SIMH_DIR),$(ID16_DIR))/DEF=($(CC_DEFS))

#
# Interdata 32-bit CPU.
#
ID32_DIR = SYS$DISK:[.INTERDATA]
ID32_LIB = $(LIB_DIR)ID32-$(ARCH).OLB
ID32_SOURCE = $(ID32_DIR)ID32_CPU.C,$(ID32_DIR)ID32_SYS.C,$(ID32_DIR)ID_DP.C,\
              $(ID32_DIR)ID_FD.C,$(ID32_DIR)ID_FP.C,$(ID32_DIR)ID_IDC.C,\
              $(ID32_DIR)ID_IO.C,$(ID32_DIR)ID_LP.C,$(ID32_DIR)ID_MT.C,\
              $(ID32_DIR)ID_PAS.C,$(ID32_DIR)ID_PT.C,$(ID32_DIR)ID_TT.C,\
              $(ID32_DIR)ID_UVC.C,$(ID32_DIR)ID32_DBOOT.C,$(ID32_DIR)ID_TTP.C
ID32_OPTIONS = /INCL=($(SIMH_DIR),$(ID32_DIR))/DEF=($(CC_DEFS))

#
# IBM 1130 Simulator Definitions.
#
IBM1130_DIR = SYS$DISK:[.IBM1130]
IBM1130_LIB = $(LIB_DIR)IBM1130-$(ARCH).OLB
IBM1130_SOURCE = $(IBM1130_DIR)IBM1130_CPU.C,$(IBM1130_DIR)IBM1130_CR.C,\
                 $(IBM1130_DIR)IBM1130_DISK.C,$(IBM1130_DIR)IBM1130_STDDEV.C,\
                 $(IBM1130_DIR)IBM1130_SYS.C,$(IBM1130_DIR)IBM1130_GDU.C,\
                 $(IBM1130_DIR)IBM1130_GUI.C,$(IBM1130_DIR)IBM1130_PRT.C,\
                 $(IBM1130_DIR)IBM1130_FMT.C,$(IBM1130_DIR)IBM1130_PTRP.C,\
                 $(IBM1130_DIR)IBM1130_PLOT.C,$(IBM1130_DIR)IBM1130_SCA.C,\
                 $(IBM1130_DIR)IBM1130_T2741.C
IBM1130_OPTIONS = /INCL=($(SIMH_DIR),$(IBM1130_DIR))/DEF=($(CC_DEFS))

#
# IBM 1401 Simulator Definitions.
#
I1401_DIR = SYS$DISK:[.I1401]
I1401_LIB = $(LIB_DIR)I1401-$(ARCH).OLB
I1401_SOURCE = $(I1401_DIR)I1401_LP.C,$(I1401_DIR)I1401_CPU.C,\
               $(I1401_DIR)I1401_IQ.C,$(I1401_DIR)I1401_CD.C,\
               $(I1401_DIR)I1401_MT.C,$(I1401_DIR)I1401_DP.C,\
               $(I1401_DIR)I1401_SYS.C
I1401_OPTIONS = /INCL=($(SIMH_DIR),$(I1401_DIR))/DEF=($(CC_DEFS))


#
# IBM 1620 Simulators Definitions.
#
I1620_DIR = SYS$DISK:[.I1620]
I1620_LIB = $(LIB_DIR)I1620-$(ARCH).OLB
I1620_SOURCE = $(I1620_DIR)I1620_CD.C,$(I1620_DIR)I1620_DP.C,\
               $(I1620_DIR)I1620_PT.C,$(I1620_DIR)I1620_TTY.C,\
               $(I1620_DIR)I1620_CPU.C,$(I1620_DIR)I1620_LP.C,\
               $(I1620_DIR)I1620_FP.C,$(I1620_DIR)I1620_SYS.C
I1620_OPTIONS = /INCL=($(SIMH_DIR),$(I1620_DIR))/DEF=($(CC_DEFS))

#
# PDP-1 Simulator Definitions.
#
PDP1_DIR = SYS$DISK:[.PDP1]
PDP1_LIB = $(LIB_DIR)PDP1-$(ARCH).OLB
PDP1_SOURCE = $(PDP1_DIR)PDP1_LP.C,$(PDP1_DIR)PDP1_CPU.C,\
              $(PDP1_DIR)PDP1_STDDEV.C,$(PDP1_DIR)PDP1_SYS.C,\
              $(PDP1_DIR)PDP1_DT.C,$(PDP1_DIR)PDP1_DRM.C,\
              $(PDP1_DIR)PDP1_CLK.C,$(PDP1_DIR)PDP1_DCS.C, \
              $(PDP1_DIR)PDP1_DPY.C
PDP1_OPTIONS = /INCL=($(SIMH_DIR),$(PDP1_DIR))/DEF=($(CC_DEFS))

#
# Digital Equipment PDP-8 Simulator Definitions.
#
PDP8_DIR = SYS$DISK:[.PDP8]
PDP8_LIB = $(LIB_DIR)PDP8-$(ARCH).OLB
PDP8_SOURCE = $(PDP8_DIR)PDP8_CPU.C,$(PDP8_DIR)PDP8_CLK.C,\
              $(PDP8_DIR)PDP8_DF.C,$(PDP8_DIR)PDP8_DT.C,\
              $(PDP8_DIR)PDP8_LP.C,$(PDP8_DIR)PDP8_MT.C,\
              $(PDP8_DIR)PDP8_PT.C,$(PDP8_DIR)PDP8_RF.C,\
              $(PDP8_DIR)PDP8_RK.C,$(PDP8_DIR)PDP8_RX.C,\
              $(PDP8_DIR)PDP8_SYS.C,$(PDP8_DIR)PDP8_TT.C,\
              $(PDP8_DIR)PDP8_TTX.C,$(PDP8_DIR)PDP8_RL.C,\
              $(PDP8_DIR)PDP8_TSC.C,$(PDP8_DIR)PDP8_TD.C,\
              $(PDP8_DIR)PDP8_CT.C,$(PDP8_DIR)PDP8_FPP.C
PDP8_OPTIONS = /INCL=($(SIMH_DIR),$(PDP8_DIR))/DEF=($(CC_DEFS))

#
# Digital Equipment PDP-4, PDP-7, PDP-9 And PDP-15 Simulator Definitions.
#
PDP18B_DIR = SYS$DISK:[.PDP18B]
PDP4_LIB = $(LIB_DIR)PDP4-$(ARCH).OLB
PDP7_LIB = $(LIB_DIR)PDP7-$(ARCH).OLB
PDP9_LIB = $(LIB_DIR)PDP9-$(ARCH).OLB
PDP15_LIB = $(LIB_DIR)PDP15-$(ARCH).OLB
PDP18B_SOURCE = $(PDP18B_DIR)PDP18B_DT.C,$(PDP18B_DIR)PDP18B_DRM.C,\
                $(PDP18B_DIR)PDP18B_CPU.C,$(PDP18B_DIR)PDP18B_LP.C,\
                $(PDP18B_DIR)PDP18B_MT.C,$(PDP18B_DIR)PDP18B_RF.C,\
                $(PDP18B_DIR)PDP18B_RP.C,$(PDP18B_DIR)PDP18B_STDDEV.C,\
                $(PDP18B_DIR)PDP18B_SYS.C,$(PDP18B_DIR)PDP18B_TT1.C,\
                $(PDP18B_DIR)PDP18B_RB.C,$(PDP18B_DIR)PDP18B_FPP.C
PDP4_OPTIONS = /INCL=($(SIMH_DIR),$(PDP18B_DIR))/DEF=($(CC_DEFS),"PDP4=1")
PDP7_OPTIONS = /INCL=($(SIMH_DIR),$(PDP18B_DIR))/DEF=($(CC_DEFS),"PDP7=1")
PDP9_OPTIONS = /INCL=($(SIMH_DIR),$(PDP18B_DIR))/DEF=($(CC_DEFS),"PDP9=1")
PDP15_OPTIONS = /INCL=($(SIMH_DIR),$(PDP18B_DIR))/DEF=($(CC_DEFS),"PDP15=1")

#
# Digital Equipment PDP-11 Simulator Definitions.
#
PDP11_DIR = SYS$DISK:[.PDP11]
PDP11_LIB1 = $(LIB_DIR)PDP11L1-$(ARCH).OLB
PDP11_SOURCE1 = $(PDP11_DIR)PDP11_FP.C,$(PDP11_DIR)PDP11_CPU.C,\
               $(PDP11_DIR)PDP11_DZ.C,$(PDP11_DIR)PDP11_CIS.C,\
               $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_RK.C,\
               $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RP.C,\
               $(PDP11_DIR)PDP11_RX.C,$(PDP11_DIR)PDP11_STDDEV.C,\
               $(PDP11_DIR)PDP11_SYS.C,$(PDP11_DIR)PDP11_TC.C, \
               $(PDP11_DIR)PDP11_CPUMOD.C,$(PDP11_DIR)PDP11_CR.C,\
               $(PDP11_DIR)PDP11_TA.C,$(PDP11_DIR)PDP11_DMC.C,\
               $(PDP11_DIR)PDP11_DUP.C,$(PDP11_DIR)PDP11_RS.C,\
               $(PDP11_DIR)PDP11_VT.C,$(PDP11_DIR)PDP11_IO_LIB.C
PDP11_LIB2 = $(LIB_DIR)PDP11L2-$(ARCH).OLB
PDP11_SOURCE2 = $(PDP11_DIR)PDP11_TM.C,$(PDP11_DIR)PDP11_TS.C,\
               $(PDP11_DIR)PDP11_IO.C,$(PDP11_DIR)PDP11_RQ.C,\
               $(PDP11_DIR)PDP11_TQ.C,$(PDP11_DIR)PDP11_PCLK.C,\
               $(PDP11_DIR)PDP11_RY.C,$(PDP11_DIR)PDP11_PT.C,\
               $(PDP11_DIR)PDP11_HK.C,$(PDP11_DIR)PDP11_XQ.C,\
               $(PDP11_DIR)PDP11_VH.C,$(PDP11_DIR)PDP11_RH.C,\
               $(PDP11_DIR)PDP11_XU.C,$(PDP11_DIR)PDP11_TU.C,\
               $(PDP11_DIR)PDP11_DL.C,$(PDP11_DIR)PDP11_RF.C, \
               $(PDP11_DIR)PDP11_RC.C,$(PDP11_DIR)PDP11_KG.C,\
               $(PDP11_DIR)PDP11_KE.C,$(PDP11_DIR)PDP11_DC.C
PDP11_OPTIONS = /INCL=($(SIMH_DIR),$(PDP11_DIR)$(PCAP_INC))\
                /DEF=($(CC_DEFS),"VM_PDP11=1"$(PCAP_DEFS))

#
# Digital Equipment PDP-10 Simulator Definitions.
#
PDP10_DIR = SYS$DISK:[.PDP10]
PDP10_LIB = $(LIB_DIR)PDP10-$(ARCH).OLB
PDP10_SOURCE = $(PDP10_DIR)PDP10_FE.C,\
               $(PDP10_DIR)PDP10_CPU.C,$(PDP10_DIR)PDP10_KSIO.C,\
               $(PDP10_DIR)PDP10_LP20.C,$(PDP10_DIR)PDP10_MDFP.C,\
               $(PDP10_DIR)PDP10_PAG.C,$(PDP10_DIR)PDP10_XTND.C,\
               $(PDP10_DIR)PDP10_RP.C,$(PDP10_DIR)PDP10_SYS.C,\
               $(PDP10_DIR)PDP10_TIM.C,$(PDP10_DIR)PDP10_TU.C,\
               $(PDP11_DIR)PDP11_PT.C,$(PDP11_DIR)PDP11_DZ.C,\
               $(PDP11_DIR)PDP11_RY.C,$(PDP11_DIR)PDP11_CR.C,\
               $(PDP11_DIR)PDP11_DUP.C
PDP10_OPTIONS = /INCL=($(SIMH_DIR),$(PDP10_DIR),$(PDP11_DIR))\
                /DEF=($(CC_DEFS),"USE_INT64=1","VM_PDP10=1"$(PCAP_DEFS))

#
# IBM System 3 Simulator Definitions.
#
S3_DIR = SYS$DISK:[.S3]
S3_LIB = $(LIB_DIR)S3-$(ARCH).OLB
S3_SOURCE = $(S3_DIR)S3_CD.C,$(S3_DIR)S3_CPU.C,$(S3_DIR)S3_DISK.C,\
            $(S3_DIR)S3_LP.C,$(S3_DIR)S3_PKB.C,$(S3_DIR)S3_SYS.C
S3_OPTIONS = /INCL=($(SIMH_DIR),$(S3_DIR))/DEF=($(CC_DEFS))

#
# SDS 940
#
SDS_DIR = SYS$DISK:[.SDS]
SDS_LIB = $(LIB_DIR)SDS-$(ARCH).OLB
SDS_SOURCE = $(SDS_DIR)SDS_CPU.C,$(SDS_DIR)SDS_DRM.C,$(SDS_DIR)SDS_DSK.C,\ 
             $(SDS_DIR)SDS_IO.C,$(SDS_DIR)SDS_LP.C,$(SDS_DIR)SDS_MT.C,\
             $(SDS_DIR)SDS_MUX.C,$(SDS_DIR)SDS_RAD.C,$(SDS_DIR)SDS_STDDEV.C,\
             $(SDS_DIR)SDS_SYS.C
SDS_OPTIONS = /INCL=($(SIMH_DIR),$(SDS_DIR))/DEF=($(CC_DEFS))

#
# SSEM
#
SSEM_DIR = SYS$DISK:[.SSEM]
SSEM_LIB = $(LIB_DIR)SSEM-$(ARCH).OLB
SSEM_SOURCE = $(SSEM_DIR)SSEM_CPU.C,$(SSEM_DIR)SSEM_SYS.C
SSEM_OPTIONS = /INCL=($(SIMH_DIR),$(SSEM_DIR))/DEF=($(CC_DEFS))

#
# SWTP 6800MP A
#
SWTP6800MP_A_DIR = SYS$DISK:[.SWTP6800.SWTP6800]
SWTP6800MP_A_COMMON = SYS$DISK:[.SWTP6800.COMMON]
SWTP6800MP_A_LIB = $(LIB_DIR)SWTP6800MP-A-$(ARCH).OLB
SWTP6800MP_A_SOURCE = $(SWTP6800MP_A_COMMON)mp-a.c,$(SWTP6800MP_A_COMMON)m6800.c,\
	$(SWTP6800MP_A_COMMON)m6810.c,$(SWTP6800MP_A_COMMON)bootrom.c,$(SWTP6800MP_A_COMMON)dc-4.c,\
	$(SWTP6800MP_A_COMMON)mp-s.c,$(SWTP6800MP_A_DIR)mp-a_sys.c,$(SWTP6800MP_A_COMMON)mp-b2.c,\
	$(SWTP6800MP_A_COMMON)mp-8m.c
SWTP6800MP_A_OPTIONS = /INCL=($(SIMH_DIR),$(SWTP6800MP_A_DIR))/DEF=($(CC_DEFS))

#
# SWTP 6800MP A2
#
SWTP6800MP_A2_DIR = SYS$DISK:[.SWTP6800.SWTP6800]
SWTP6800MP_A2_COMMON = SYS$DISK:[.SWTP6800.COMMON]
SWTP6800MP_A2_LIB = $(LIB_DIR)SWTP6800MP-A2-$(ARCH).OLB
SWTP6800MP_A2_SOURCE = $(SWTP6800MP_A2_COMMON)mp-a2.c,$(SWTP6800MP_A2_COMMON)m6800.c,\
	$(SWTP6800MP_A2_COMMON)m6810.c,$(SWTP6800MP_A2_COMMON)bootrom.c,$(SWTP6800MP_A2_COMMON)dc-4.c,\
	$(SWTP6800MP_A2_COMMON)mp-s.c,$(SWTP6800MP_A2_DIR)mp-a2_sys.c,$(SWTP6800MP_A2_COMMON)mp-b2.c,\
	$(SWTP6800MP_A2_COMMON)mp-8m.c,$(SWTP6800MP_A2_COMMON)i2716.c
SWTP6800MP_A2_OPTIONS = /INCL=($(SIMH_DIR),$(SWTP6800MP_A2_DIR))/DEF=($(CC_DEFS))

#
# Digital Equipment VAX 3900 Simulator Definitions.
#
VAX_DIR = SYS$DISK:[.VAX]
VAX_LIB1 = $(LIB_DIR)VAXL1-$(ARCH).OLB
VAX_LIB2 = $(LIB_DIR)VAXL2-$(ARCH).OLB
VAX_SOURCE1 = $(VAX_DIR)VAX_CIS.C,$(VAX_DIR)VAX_CMODE.C,\
              $(VAX_DIR)VAX_CPU.C,$(VAX_DIR)VAX_CPU1.C,\
              $(VAX_DIR)VAX_FPA.C,$(VAX_DIR)VAX_MMU.C,\
              $(VAX_DIR)VAX_OCTA.C,$(VAX_DIR)VAX_SYS.C,\
              $(VAX_DIR)VAX_SYSCM.C,$(VAX_DIR)VAX_SYSDEV.C,\
              $(VAX_DIR)VAX_SYSLIST.C,$(VAX_DIR)VAX_IO.C,\
              $(VAX_DIR)VAX_STDDEV.C
VAX_SOURCE2 = $(PDP11_DIR)PDP11_IO_LIB.C,\
              $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
              $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
              $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
              $(PDP11_DIR)PDP11_XQ.C,$(PDP11_DIR)PDP11_VH.C,\
              $(PDP11_DIR)PDP11_CR.C
.IFDEF ALPHA_OR_IA64
VAX_OPTIONS = /INCL=($(SIMH_DIR),$(VAX_DIR),$(PDP11_DIR)$(PCAP_INC))\
                /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS))
VAX_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX_OPTIONS = /INCL=($(SIMH_DIR),$(VAX_DIR),$(PDP11_DIR)$(PCAP_INC))\
                /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS))
VAX_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX610 (MicroVAX I) Simulator Definitions.
#
VAX610_DIR = SYS$DISK:[.VAX]
VAX610_LIB1 = $(LIB_DIR)VAX610L1-$(ARCH).OLB
VAX610_SOURCE1 = $(VAX610_DIR)VAX_CPU.C,$(VAX610_DIR)VAX_CPU1.C,\
                 $(VAX610_DIR)VAX_FPA.C,$(VAX610_DIR)VAX_CIS.C,\
                 $(VAX610_DIR)VAX_OCTA.C,$(VAX610_DIR)VAX_CMODE.C,\
                 $(VAX610_DIR)VAX_MMU.C,$(VAX610_DIR)VAX_SYS.C,\
                 $(VAX610_DIR)VAX_SYSCM.C,$(VAX610_DIR)VAX610_STDDEV.C,\
                 $(VAX610_DIR)VAX610_MEM.C,$(VAX610_DIR)VAX610_SYSDEV.C,\
                 $(VAX610_DIR)VAX610_IO.C,$(VAX610_DIR)VAX610_SYSLIST.C
VAX610_LIB2 = $(LIB_DIR)VAX610L2-$(ARCH).OLB
VAX610_SOURCE2 = $(PDP11_DIR)PDP11_IO_LIB.C,\
                 $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XQ.C,$(PDP11_DIR)PDP11_VH.C,\
                 $(PDP11_DIR)PDP11_CR.C
.IFDEF ALPHA_OR_IA64
VAX610_OPTIONS = /INCL=($(SIMH_DIR),$(VAX610_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_610=1")
VAX610_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX610_OPTIONS = /INCL=($(SIMH_DIR),$(VAX610_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_610=1")
VAX610_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX630 (MicroVAX II) Simulator Definitions.
#
VAX630_DIR = SYS$DISK:[.VAX]
VAX630_LIB1 = $(LIB_DIR)VAX630L1-$(ARCH).OLB
VAX630_SOURCE1 = $(VAX630_DIR)VAX_CPU.C,$(VAX630_DIR)VAX_CPU1.C,\
                 $(VAX630_DIR)VAX_FPA.C,$(VAX630_DIR)VAX_CIS.C,\
                 $(VAX630_DIR)VAX_OCTA.C,$(VAX630_DIR)VAX_CMODE.C,\
                 $(VAX630_DIR)VAX_MMU.C,$(VAX630_DIR)VAX_SYS.C,\
                 $(VAX630_DIR)VAX_SYSCM.C,$(VAX630_DIR)VAX_WATCH.C,\
                 $(VAX630_DIR)VAX630_STDDEV.C,$(VAX630_DIR)VAX630_SYSDEV.C,\
                 $(VAX630_DIR)VAX630_IO.C,$(VAX630_DIR)VAX630_SYSLIST.C
VAX630_LIB2 = $(LIB_DIR)VAX630L2-$(ARCH).OLB
VAX630_SOURCE2 = $(PDP11_DIR)PDP11_IO_LIB.C,\
                 $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XQ.C,$(PDP11_DIR)PDP11_VH.C,\
                 $(PDP11_DIR)PDP11_CR.C
.IFDEF ALPHA_OR_IA64
VAX630_OPTIONS = /INCL=($(SIMH_DIR),$(VAX630_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_630=1")
VAX630_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX630_OPTIONS = /INCL=($(SIMH_DIR),$(VAX630_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_630=1")
VAX630_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment rtVAX1000 (rtVAX 1000) Simulator Definitions.
#
VAX620_DIR = SYS$DISK:[.VAX]
VAX620_LIB1 = $(LIB_DIR)VAX620L1-$(ARCH).OLB
VAX620_SOURCE1 = $(VAX620_DIR)VAX_CPU.C,$(VAX620_DIR)VAX_CPU1.C,\
                 $(VAX620_DIR)VAX_FPA.C,$(VAX620_DIR)VAX_CIS.C,\
                 $(VAX620_DIR)VAX_OCTA.C,$(VAX620_DIR)VAX_CMODE.C,\
                 $(VAX620_DIR)VAX_MMU.C,$(VAX620_DIR)VAX_SYS.C,\
                 $(VAX620_DIR)VAX_SYSCM.C,$(VAX630_DIR)VAX_WATCH.C,\
                 $(VAX620_DIR)VAX630_STDDEV.C,$(VAX620_DIR)VAX630_SYSDEV.C,\
                 $(VAX620_DIR)VAX630_IO.C,$(VAX620_DIR)VAX630_SYSLIST.C
VAX620_LIB2 = $(LIB_DIR)VAX620L2-$(ARCH).OLB
VAX620_SOURCE2 = $(PDP11_DIR)PDP11_IO_LIB.C,\
                 $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XQ.C,$(PDP11_DIR)PDP11_VH.C,\
                 $(PDP11_DIR)PDP11_CR.C
.IFDEF ALPHA_OR_IA64
VAX620_OPTIONS = /INCL=($(SIMH_DIR),$(VAX620_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_620=1")
VAX620_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX620_OPTIONS = /INCL=($(SIMH_DIR),$(VAX620_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_620=1")
VAX620_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX730 Simulator Definitions.
#
VAX730_DIR = SYS$DISK:[.VAX]
VAX730_LIB1 = $(LIB_DIR)VAX730L1-$(ARCH).OLB
VAX730_SOURCE1 = $(VAX730_DIR)VAX_CPU.C,$(VAX730_DIR)VAX_CPU1.C,\
                 $(VAX730_DIR)VAX_FPA.C,$(VAX730_DIR)VAX_CIS.C,\
                 $(VAX730_DIR)VAX_OCTA.C,$(VAX730_DIR)VAX_CMODE.C,\
                 $(VAX730_DIR)VAX_MMU.C,$(VAX730_DIR)VAX_SYS.C,\
                 $(VAX730_DIR)VAX_SYSCM.C,$(VAX730_DIR)VAX730_STDDEV.C,\
                 $(VAX730_DIR)VAX730_SYS.C,$(VAX730_DIR)VAX730_MEM.C,\
                 $(VAX730_DIR)VAX730_UBA.C,$(VAX730_DIR)VAX730_RB.C,\
                 $(VAX730_DIR)VAX730_SYSLIST.C
VAX730_LIB2 = $(LIB_DIR)VAX730L2-$(ARCH).OLB
VAX730_SOURCE2 = $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XU.C,$(PDP11_DIR)PDP11_RY.C,\
                 $(PDP11_DIR)PDP11_CR.C,$(PDP11_DIR)PDP11_HK.C,\
                 $(PDP11_DIR)PDP11_VH.C,$(PDP11_DIR)PDP11_DMC.C,\
                 $(PDP11_DIR)PDP11_DUP.C,$(PDP11_DIR)PDP11_IO_LIB.C
.IFDEF ALPHA_OR_IA64
VAX730_OPTIONS = /INCL=($(SIMH_DIR),$(VAX730_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_730=1")
VAX730_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX730_OPTIONS = /INCL=($(SIMH_DIR),$(VAX730_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_730=1")
VAX730_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX750 Simulator Definitions.
#
VAX750_DIR = SYS$DISK:[.VAX]
VAX750_LIB1 = $(LIB_DIR)VAX750L1-$(ARCH).OLB
VAX750_SOURCE1 = $(VAX750_DIR)VAX_CPU.C,$(VAX750_DIR)VAX_CPU1.C,\
                 $(VAX750_DIR)VAX_FPA.C,$(VAX750_DIR)VAX_CIS.C,\
                 $(VAX750_DIR)VAX_OCTA.C,$(VAX750_DIR)VAX_CMODE.C,\
                 $(VAX750_DIR)VAX_MMU.C,$(VAX750_DIR)VAX_SYS.C,\
                 $(VAX750_DIR)VAX_SYSCM.C,$(VAX750_DIR)VAX750_STDDEV.C,\
                 $(VAX750_DIR)VAX750_CMI.C,$(VAX750_DIR)VAX750_MEM.C,\
                 $(VAX750_DIR)VAX750_UBA.C,$(VAX750_DIR)VAX7X0_MBA.C,\
                 $(VAX750_DIR)VAX750_SYSLIST.C
VAX750_LIB2 = $(LIB_DIR)VAX750L2-$(ARCH).OLB
VAX750_SOURCE2 = $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XU.C,$(PDP11_DIR)PDP11_RY.C,\
                 $(PDP11_DIR)PDP11_CR.C,$(PDP11_DIR)PDP11_HK.C,\
                 $(PDP11_DIR)PDP11_RP.C,$(PDP11_DIR)PDP11_TU.C,\
                 $(PDP11_DIR)PDP11_VH.C,$(PDP11_DIR)PDP11_DMC.C,\
                 $(PDP11_DIR)PDP11_DUP.C,$(PDP11_DIR)PDP11_IO_LIB.C
.IFDEF ALPHA_OR_IA64
VAX750_OPTIONS = /INCL=($(SIMH_DIR),$(VAX750_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_750=1")
VAX750_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX750_OPTIONS = /INCL=($(SIMH_DIR),$(VAX750_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_750=1")
VAX750_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX780 Simulator Definitions.
#
VAX780_DIR = SYS$DISK:[.VAX]
VAX780_LIB1 = $(LIB_DIR)VAX780L1-$(ARCH).OLB
VAX780_SOURCE1 = $(VAX780_DIR)VAX_CPU.C,$(VAX780_DIR)VAX_CPU1.C,\
                 $(VAX780_DIR)VAX_FPA.C,$(VAX780_DIR)VAX_CIS.C,\
                 $(VAX780_DIR)VAX_OCTA.C,$(VAX780_DIR)VAX_CMODE.C,\
                 $(VAX780_DIR)VAX_MMU.C,$(VAX780_DIR)VAX_SYS.C,\
                 $(VAX780_DIR)VAX_SYSCM.C,$(VAX780_DIR)VAX780_STDDEV.C,\
                 $(VAX780_DIR)VAX780_SBI.C,$(VAX780_DIR)VAX780_MEM.C,\
                 $(VAX780_DIR)VAX780_UBA.C,$(VAX780_DIR)VAX7X0_MBA.C,\
                 $(VAX780_DIR)VAX780_FLOAD.C,$(VAX780_DIR)VAX780_SYSLIST.C
VAX780_LIB2 = $(LIB_DIR)VAX780L2-$(ARCH).OLB
VAX780_SOURCE2 = $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XU.C,$(PDP11_DIR)PDP11_RY.C,\
                 $(PDP11_DIR)PDP11_CR.C,$(PDP11_DIR)PDP11_RP.C,\
                 $(PDP11_DIR)PDP11_TU.C,$(PDP11_DIR)PDP11_HK.C,\
                 $(PDP11_DIR)PDP11_VH.C,$(PDP11_DIR)PDP11_DMC.C,\
                 $(PDP11_DIR)PDP11_DUP.C,$(PDP11_DIR)PDP11_IO_LIB.C
.IFDEF ALPHA_OR_IA64
VAX780_OPTIONS = /INCL=($(SIMH_DIR),$(VAX780_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_780=1")
VAX780_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX780_OPTIONS = /INCL=($(SIMH_DIR),$(VAX780_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_780=1")
VAX780_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# Digital Equipment VAX8600 Simulator Definitions.
#
VAX8600_DIR = SYS$DISK:[.VAX]
VAX8600_LIB1 = $(LIB_DIR)VAX860L1-$(ARCH).OLB
VAX8600_SOURCE1 = $(VAX8600_DIR)VAX_CPU.C,$(VAX8600_DIR)VAX_CPU1.C,\
                 $(VAX8600_DIR)VAX_FPA.C,$(VAX8600_DIR)VAX_CIS.C,\
                 $(VAX8600_DIR)VAX_OCTA.C,$(VAX8600_DIR)VAX_CMODE.C,\
                 $(VAX8600_DIR)VAX_MMU.C,$(VAX8600_DIR)VAX_SYS.C,\
                 $(VAX8600_DIR)VAX_SYSCM.C,$(VAX8600_DIR)VAX860_STDDEV.C,\
                 $(VAX8600_DIR)VAX860_SBIA.C,$(VAX8600_DIR)VAX860_ABUS.C,\
                 $(VAX8600_DIR)VAX780_UBA.C,$(VAX8600_DIR)VAX7X0_MBA.C,\
                 $(VAX8600_DIR)VAX860_SYSLIST.C
VAX8600_LIB2 = $(LIB_DIR)VAX860L2-$(ARCH).OLB
VAX8600_SOURCE2 = $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
                 $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
                 $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
                 $(PDP11_DIR)PDP11_XU.C,$(PDP11_DIR)PDP11_RY.C,\
                 $(PDP11_DIR)PDP11_CR.C,$(PDP11_DIR)PDP11_RP.C,\
                 $(PDP11_DIR)PDP11_TU.C,$(PDP11_DIR)PDP11_HK.C,\
                 $(PDP11_DIR)PDP11_VH.C,$(PDP11_DIR)PDP11_DMC.C,\
                 $(PDP11_DIR)PDP11_DUP.C,$(PDP11_DIR)PDP11_IO_LIB.C
.IFDEF ALPHA_OR_IA64
VAX8600_OPTIONS = /INCL=($(SIMH_DIR),$(VAX8600_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1","USE_ADDR64=1","USE_INT64=1"$(PCAP_DEFS),"VAX_860=1")
VAX8600_SIMH_LIB = $(SIMH_LIB64)
.ELSE
VAX8600_OPTIONS = /INCL=($(SIMH_DIR),$(VAX8600_DIR),$(PDP11_DIR)$(PCAP_INC))\
                 /DEF=($(CC_DEFS),"VM_VAX=1"$(PCAP_DEFS),"VAX_860=1")
VAX8600_SIMH_LIB = $(SIMH_LIB)
.ENDIF

# IBM 7094 Simulator Definitions.
#
I7094_DIR = SYS$DISK:[.I7094]
I7094_LIB = $(LIB_DIR)I7094-$(ARCH).OLB
I7094_SOURCE = $(I7094_DIR)I7094_CPU.C,$(I7094_DIR)I7094_CPU1.C,\
               $(I7094_DIR)I7094_IO.C,$(I7094_DIR)I7094_CD.C,\
               $(I7094_DIR)I7094_CLK.C,$(I7094_DIR)I7094_COM.C,\
               $(I7094_DIR)I7094_DRM.C,$(I7094_DIR)I7094_DSK.C,\
               $(I7094_DIR)I7094_SYS.C,$(I7094_DIR)I7094_LP.C,\
               $(I7094_DIR)I7094_MT.C,$(I7094_DIR)I7094_BINLOADER.C
I7094_OPTIONS = /INCL=($(SIMH_DIR),$(I7094_DIR))/DEF=($(CC_DEFS))

# If we're not a VAX, Build Everything
#
.IFDEF ALPHA_OR_IA64
ALL : ALTAIR ALTAIRZ80 ECLIPSE GRI LGP H316 HP2100 I1401 I1620 IBM1130 ID16 \
      ID32 NOVA PDP1 PDP4 PDP7 PDP8 PDP9 PDP10 PDP11 PDP15 S3 \
      VAX MICROVAX3900 MICROVAX1 RTVAX1000 MICROVAX2 VAX730 VAX750 VAX780 VAX8600 \
      SDS I7094 SWTP6800MP-A SWTP6800MP-A2 SSEM
        $! No further actions necessary
.ELSE
#
# Else We Are On VAX And Build Everything EXCEPT the 64b simulators
#
ALL : ALTAIR ALTAIRZ80 GRI H316 HP2100 I1401 I1620 IBM1130 ID16 ID32 \
      NOVA PDP1 PDP4 PDP7 PDP8 PDP9 PDP11 PDP15 S3 \
      VAX MICROVAX3900 MICROVAX1 RTVAX1000 MICROVAX2 VAX730 VAX750 VAX780 VAX8600 \
      SDS SWTP6800MP-A SWTP6800MP-A2 SSEM
        $! No further actions necessary
.ENDIF

CLEAN : 
        $!
        $! Clean out all targets and building Remnants
        $!
        $ IF (F$SEARCH("$(BIN_DIR)*.EXE;*").NES."") THEN -
             DELETE/NOLOG/NOCONFIRM $(BIN_DIR)*.EXE;*
        $ IF (F$SEARCH("$(LIB_DIR)*.OLB;*").NES."") THEN -
             DELETE/NOLOG/NOCONFIRM $(LIB_DIR)*.OLB;*
        $ IF (F$SEARCH("SYS$DISK:[...]*.OBJ;*").NES."") THEN -
             DELETE/NOLOG/NOCONFIRM SYS$DISK:[...]*.OBJ;*
        $ IF (F$SEARCH("SYS$DISK:[...]*.LIS;*").NES."") THEN -
             DELETE/NOLOG/NOCONFIRM SYS$DISK:[...]*.LIS;*
        $ IF (F$SEARCH("SYS$DISK:[...]*.MAP;*").NES."") THEN -
             DELETE/NOLOG/NOCONFIRM SYS$DISK:[...]*.MAP;*

#
# ROM support
#
BUILDROMS : $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $! BuildROMs done

$(BIN_DIR)BuildROMs-$(ARCH).EXE : sim_BuildROMs.c
        $!
        $! Building The $(BIN_DIR)BuildROMs-$(ARCH).EXE Tool.
        $!
        $ $(CC)/OBJ=$(BLD_DIR) SIM_BUILDROMS.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)BUILDROMS-$(ARCH).EXE -
               $(BLD_DIR)SIM_BUILDROMS.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE

#
# Build The Libraries.
#
$(SIMH_LIB) : $(SIMH_SOURCE)
        $!
        $! Building The $(SIMH_LIB) Library.
        $!
        $ $(CC)/DEF=($(CC_DEFS)$(PCAP_DEFS))$(PCAP_SIMH_INC) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(SIMH_NONET_LIB) : $(SIMH_SOURCE)
        $!
        $! Building The $(SIMH_NONET_LIB) Library.
        $!
        $ $(CC)/DEF=($(CC_DEFS)) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

.IFDEF ALPHA_OR_IA64
$(SIMH_LIB64) : $(SIMH_SOURCE)
        $!
        $! Building The $(SIMH_LIB64) Library.
        $!
        $ $(CC)/DEF=($(CC_DEFS)$(PCAP_DEFS),"USE_ADDR64=1","USE_INT64=1")$(PCAP_SIMH_INC) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ENDIF

$(ALTAIR_LIB) : $(ALTAIR_SOURCE)
        $!
        $! Building The $(ALTAIR_LIB) Library.
        $!
        $ $(CC)$(ALTAIR_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(ALTAIRZ80_LIB1) : $(ALTAIRZ80_SOURCE1)
        $!
        $! Building The $(ALTAIRZ80_LIB1) Library.
        $!
        $ $(CC)$(ALTAIRZ80_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(ALTAIRZ80_LIB2) : $(ALTAIRZ80_SOURCE2)
        $!
        $! Building The $(ALTAIRZ80_LIB2) Library.
        $!
        $ $(CC)$(ALTAIRZ80_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

#
# If Not On VAX, Build The Eclipse Library.
#
.IFDEF ALPHA_OR_IA64
$(ECLIPSE_LIB) : $(ECLIPSE_SOURCE)
        $!
        $! Building The $(ECLIPSE_LIB) Library.
        $!
        $ $(CC)$(ECLIPSE_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ELSE
#
# We Are On VAX And Due To The Use of INT64 We Can't Build It.
#
$(ECLIPSE_LIB) : 
        $! Due To The Use Of INT64 We Can't Build The
        $! $(MMS$TARGET) Library On VAX.
.ENDIF

$(GRI_LIB) : $(GRI_SOURCE)
        $!
        $! Building The $(GRI_LIB) Library.
        $!
        $ $(CC)$(GRI_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(LGP_LIB) : $(LGP_SOURCE)
        $!
        $! Building The $(LGP_LIB) Library.
        $!
        $ $(CC)$(LGP_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(H316_LIB) : $(H316_SOURCE)
        $!
        $! Building The $(H316_LIB) Library.
        $!
        $ $(CC)$(H316_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(HP2100_LIB1) : $(HP2100_SOURCE1)
        $!
        $! Building The $(HP2100_LIB1) Library.
        $!
        $ $(CC)$(HP2100_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(HP2100_LIB2) : $(HP2100_SOURCE2)
        $!
        $! Building The $(HP2100_LIB2) Library.
        $!
        $ $(CC)$(HP2100_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(I1401_LIB) : $(I1401_SOURCE)
        $!
        $! Building The $(I1401_LIB) Library.
        $!
        $ $(CC)$(I1401_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(I1620_LIB) : $(I1620_SOURCE)
        $!
        $! Building The $(I1620_LIB) Library.
        $!
        $ $(CC)$(I1620_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(IBM1130_LIB) : $(IBM1130_SOURCE)
        $!
        $! Building The $(IBM1130_LIB) Library.
        $!
        $ $(CC)$(IBM1130_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(ID16_LIB) : $(ID16_SOURCE)
        $!
        $! Building The $(ID16_LIB) Library.
        $!
        $ $(CC)$(ID16_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(ID32_LIB) : $(ID32_SOURCE)
        $!
        $! Building The $(ID32_LIB) Library.
        $!
        $ $(CC)$(ID32_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(NOVA_LIB) : $(NOVA_SOURCE)
        $!
        $! Building The $(NOVA_LIB) Library.
        $!
        $ $(CC)$(NOVA_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP1_LIB) : $(PDP1_SOURCE)
        $!
        $! Building The $(PDP1_LIB) Library.
        $!
        $ $(CC)$(PDP1_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP4_LIB) : $(PDP18B_SOURCE)
        $!
        $! Building The $(PDP4_LIB) Library.
        $!
        $ $(CC)$(PDP4_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP7_LIB) : $(PDP18B_SOURCE)
        $!
        $! Building The $(PDP7_LIB) Library.
        $!
        $ $(CC)$(PDP7_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP8_LIB) : $(PDP8_SOURCE)
        $!
        $! Building The $(PDP8_LIB) Library.
        $!
        $ $(CC)$(PDP8_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP9_LIB) : $(PDP18B_SOURCE)
        $!
        $! Building The $(PDP9_LIB) Library.
        $!
        $ $(CC)$(PDP9_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

#
# If Not On VAX, Build The PDP-10 Library.
#
.IFDEF ALPHA_OR_IA64
$(PDP10_LIB) : $(PDP10_SOURCE)
        $!
        $! Building The $(PDP10_LIB) Library.
        $!
        $ $(CC)$(PDP10_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ELSE
#
# We Are On VAX And Due To The Use of INT64 We Can't Build It.
#
$(PDP10_LIB) : 
        $! Due To The Use Of INT64 We Can't Build The
        $! $(MMS$TARGET) Library On VAX.
.ENDIF

$(PDP11_LIB1) : $(PDP11_SOURCE1)
        $!
        $! Building The $(PDP11_LIB1) Library.
        $!
        $ $(CC)$(PDP11_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP11_LIB2) : $(PDP11_SOURCE2)
        $!
        $! Building The $(PDP11_LIB2) Library.
        $!
        $ $(CC)$(PDP11_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PDP15_LIB) : $(PDP18B_SOURCE)
        $!
        $! Building The $(PDP15_LIB) Library.
        $!
        $ $(CC)$(PDP15_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(S3_LIB) : $(S3_SOURCE)
        $!
        $! Building The $(S3_LIB) Library.
        $!
        $ $(CC)$(S3_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(SDS_LIB) : $(SDS_SOURCE)
        $!
        $! Building The $(SDS_LIB) Library.
        $!
        $ $(CC)$(SDS_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(SSEM_LIB) : $(SSEM_SOURCE)
        $!
        $! Building The $(SSEM_LIB) Library.
        $!
        $ $(CC)$(SSEM_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(SWTP6800MP_A_LIB) : $(SWTP6800MP_A_SOURCE)
        $!
        $! Building The $(SWTP_LIB) Library.
        $!
        $ $(CC)$(SWTP6800MP_A_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(SWTP6800MP_A2_LIB) : $(SWTP6800MP_A2_SOURCE)
        $!
        $! Building The $(SWTP_LIB) Library.
        $!
        $ $(CC)$(SWTP6800MP_A2_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX_LIB1) : $(VAX_SOURCE1)
        $!
        $! Building The $(VAX_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX_OPTIONS)/OBJ=$(VAX_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX_LIB2) : $(VAX_SOURCE2)
        $!
        $! Building The $(VAX_LIB2) Library.
        $!
        $ $(CC)$(VAX_OPTIONS)/OBJ=$(VAX_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX610_LIB1) : $(VAX610_SOURCE1)
        $!
        $! Building The $(VAX610_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX610_OPTIONS)/OBJ=$(VAX610_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX610_LIB2) : $(VAX610_SOURCE2)
        $!
        $! Building The $(VAX610_LIB2) Library.
        $!
        $ $(CC)$(VAX610_OPTIONS)/OBJ=$(VAX610_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX630_LIB1) : $(VAX630_SOURCE1)
        $!
        $! Building The $(VAX630_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX630_OPTIONS)/OBJ=$(VAX630_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX630_LIB2) : $(VAX630_SOURCE2)
        $!
        $! Building The $(VAX630_LIB2) Library.
        $!
        $ $(CC)$(VAX630_OPTIONS)/OBJ=$(VAX630_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX620_LIB1) : $(VAX620_SOURCE1)
        $!
        $! Building The $(VAX620_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX620_OPTIONS)/OBJ=$(VAX620_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX620_LIB2) : $(VAX620_SOURCE2)
        $!
        $! Building The $(VAX620_LIB2) Library.
        $!
        $ $(CC)$(VAX620_OPTIONS)/OBJ=$(VAX620_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX730_LIB1) : $(VAX730_SOURCE1)
        $!
        $! Building The $(VAX730_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX730_OPTIONS)/OBJ=$(VAX730_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX730_LIB2) : $(VAX730_SOURCE2)
        $!
        $! Building The $(VAX730_LIB2) Library.
        $!
        $ $(CC)$(VAX730_OPTIONS)/OBJ=$(VAX730_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX750_LIB1) : $(VAX750_SOURCE1)
        $!
        $! Building The $(VAX750_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX750_OPTIONS)/OBJ=$(VAX750_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX750_LIB2) : $(VAX750_SOURCE2)
        $!
        $! Building The $(VAX750_LIB2) Library.
        $!
        $ $(CC)$(VAX750_OPTIONS)/OBJ=$(VAX750_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX780_LIB1) : $(VAX780_SOURCE1)
        $!
        $! Building The $(VAX780_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX780_OPTIONS)/OBJ=$(VAX780_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX780_LIB2) : $(VAX780_SOURCE2)
        $!
        $! Building The $(VAX780_LIB2) Library.
        $!
        $ $(CC)$(VAX780_OPTIONS)/OBJ=$(VAX780_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX8600_LIB1) : $(VAX8600_SOURCE1)
        $!
        $! Building The $(VAX8600_LIB1) Library.
        $!
        $ RUN/NODEBUG $(BIN_DIR)BuildROMs-$(ARCH).EXE
        $ $(CC)$(VAX8600_OPTIONS)/OBJ=$(VAX8600_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(VAX8600_LIB2) : $(VAX8600_SOURCE2)
        $!
        $! Building The $(VAX8600_LIB2) Library.
        $!
        $ $(CC)$(VAX8600_OPTIONS)/OBJ=$(VAX8600_DIR) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

$(PCAP_LIB) : $(PCAP_SOURCE)
        $!
        $! Building The $(PCAP_LIB) Library.
        $!
        $ Saved_Default = F$Environment("DEFAULT")
        $ SET DEFAULT $(PCAP_DIR)
        $ @VMS_PCAP $(DEBUG)
        $ SET DEFAULT 'Saved_Default
        $ IF (F$SEARCH("$(PCAP_LIB)").NES."") THEN -
             DELETE $(PCAP_LIB);
        $ COPY $(PCAP_DIR)PCAP.OLB $(PCAP_LIB)
        $ DELETE/NOLOG/NOCONFIRM $(PCAP_DIR)*.OBJ;*,$(PCAP_DIR)*.OLB;*

#
# If Not On VAX, Build The IBM 7094 Library.
#
.IFDEF ALPHA_OR_IA64
$(I7094_LIB) : $(I7094_SOURCE)
        $!
        $! Building The $(I7094_LIB) Library.
        $!
        $ $(CC)$(I7094_OPTIONS) -
               /OBJ=$(BLD_DIR) $(MMS$CHANGED_LIST)
        $ IF (F$SEARCH("$(MMS$TARGET)").EQS."") THEN -
             LIBRARY/CREATE $(MMS$TARGET)
        $ LIBRARY/REPLACE $(MMS$TARGET) $(BLD_DIR)*.OBJ
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ELSE
#
# We Are On VAX And Due To The Use of INT64 We Can't Build It.
#
$(I7094_LIB) : 
        $! Due To The Use Of INT64 We Can't Build The
        $! $(MMS$TARGET) Library On VAX.
.ENDIF

#
# Individual Simulator Builds.
#
ALTAIR : $(BIN_DIR)ALTAIR-$(ARCH).EXE
        $! ALTAIR done

$(BIN_DIR)ALTAIR-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(ALTAIR_LIB)
        $!
        $! Building The $(BIN_DIR)ALTAIR-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(ALTAIR_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ALTAIR-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(ALTAIR_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

ALTAIRZ80 : $(BIN_DIR)ALTAIRZ80-$(ARCH).EXE
        $! ALTAIRZ80 done

$(BIN_DIR)ALTAIRZ80-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(ALTAIRZ80_LIB1) $(ALTAIRZ80_LIB2)
        $!
        $! Building The $(BIN_DIR)ALTAIRZ80-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(ALTAIRZ80_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ALTAIRZ80-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(ALTAIRZ80_LIB1)/LIBRARY, -
               $(ALTAIRZ80_LIB2)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
#
# If Not On VAX, Build The Eclipse Simulator.
#
.IFDEF ALPHA_OR_IA64
ECLIPSE : $(BIN_DIR)ECLIPSE-$(ARCH).EXE
        $! ECLIPSE done
.ELSE
#
# Else We Are On VAX And Tell The User We Can't Build On VAX
# Due To The Use Of INT64.
#
ECLIPSE : 
        $! Sorry, Can't Build $(BIN_DIR)ECLIPSE-$(ARCH).EXE Simulator
        $! Because It Requires The Use Of INT64.
.ENDIF

$(BIN_DIR)ECLIPSE-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(ECLIPSE_LIB)
        $!
        $! Building The $(BIN_DIR)ECLIPSE-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(ECLIPSE_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ECLIPSE-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(ECLIPSE_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

GRI : $(BIN_DIR)GRI-$(ARCH).EXE
        $! GRI done

$(BIN_DIR)GRI-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(GRI_LIB)
        $!
        $! Building The $(BIN_DIR)GRI-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(GRI_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)GRI-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(GRI_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

LGP : $(BIN_DIR)LGP-$(ARCH).EXE
        $! LGP done

$(BIN_DIR)LGP-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(LGP_LIB)
        $!
        $! Building The $(BIN_DIR)LGP-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(LGP_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)LGP-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(LGP_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

H316 : $(BIN_DIR)H316-$(ARCH).EXE
        $! H316 done

$(BIN_DIR)H316-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(H316_LIB)
        $!
        $! Building The $(BIN_DIR)H316-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(H316_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)H316-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(H316_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

HP2100 : $(BIN_DIR)HP2100-$(ARCH).EXE
        $! HP2100 done

$(BIN_DIR)HP2100-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(HP2100_LIB1) $(HP2100_LIB2)
        $!
        $! Building The $(BIN_DIR)HP2100-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(HP2100_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)HP2100-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(HP2100_LIB1)/LIBRARY, -
               $(HP2100_LIB2)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

I1401 : $(BIN_DIR)I1401-$(ARCH).EXE
        $! I1401 done

$(BIN_DIR)I1401-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(I1401_LIB)
        $!
        $! Building The $(BIN_DIR)I1401-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(I1401_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)I1401-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(I1401_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

I1620 : $(BIN_DIR)I1620-$(ARCH).EXE
        $! I1620 done

$(BIN_DIR)I1620-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(I1620_LIB)
        $!
        $! Building The $(BIN_DIR)I1620-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(I1620_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)I1620-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(I1620_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

IBM1130 : $(BIN_DIR)IBM1130-$(ARCH).EXE
        $! IBM1130 done

$(BIN_DIR)IBM1130-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(IBM1130_LIB)
        $!
        $! Building The $(BIN_DIR)IBM1130-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(IBM1130_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)IBM1130-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(IBM1130_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

ID16 : $(BIN_DIR)ID16-$(ARCH).EXE
        $! ID16 done

$(BIN_DIR)ID16-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(ID16_LIB)
        $!
        $! Building The $(BIN_DIR)ID16-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(ID16_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ID16-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(ID16_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

ID32 : $(BIN_DIR)ID32-$(ARCH).EXE
        $! ID32 done

$(BIN_DIR)ID32-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(ID32_LIB)
        $!
        $! Building The $(BIN_DIR)ID32-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(ID32_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ID32-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(ID32_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

NOVA : $(BIN_DIR)NOVA-$(ARCH).EXE
        $! NOVA done

$(BIN_DIR)NOVA-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(NOVA_LIB)
        $!
        $! Building The $(BIN_DIR)NOVA-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(NOVA_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)NOVA-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(NOVA_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP1 : $(BIN_DIR)PDP1-$(ARCH).EXE
        $! PDP1 done

$(BIN_DIR)PDP1-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP1_LIB)
        $!
        $! Building The $(BIN_DIR)PDP1-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP1_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP1-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP1_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP4 : $(BIN_DIR)PDP4-$(ARCH).EXE
        $! PDP4 done

$(BIN_DIR)PDP4-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP4_LIB)
        $!
        $! Building The $(BIN_DIR)PDP4-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP4_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP4-$(ARCH).EXE -
              $(BLD_DIR)SCP.OBJ,$(PDP4_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP7 : $(BIN_DIR)PDP7-$(ARCH).EXE
        $! PDP7 done

$(BIN_DIR)PDP7-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP7_LIB)
        $!
        $! Building The $(BIN_DIR)PDP7-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP7_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP7-$(ARCH).EXE -
              $(BLD_DIR)SCP.OBJ,$(PDP7_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP8 : $(BIN_DIR)PDP8-$(ARCH).EXE
        $! PDP8 done

$(BIN_DIR)PDP8-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP8_LIB)
        $!
        $! Building The $(BIN_DIR)PDP8-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP8_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP8-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP8_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP9 : $(BIN_DIR)PDP9-$(ARCH).EXE
        $! PDP9 done

$(BIN_DIR)PDP9-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP9_LIB)
        $!
        $! Building The $(BIN_DIR)PDP9-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP9_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP9-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP9_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

#
# If Not On VAX, Build The PDP-10 Simulator.
#
.IFDEF ALPHA_OR_IA64
PDP10 : $(BIN_DIR)PDP10-$(ARCH).EXE
        $! PDP10 done

$(BIN_DIR)PDP10-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PCAP_LIBD) $(PDP10_LIB) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)PDP10-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP10_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP10-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP10_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ELSE
#
# Else We Are On VAX And Tell The User We Can't Build On VAX
# Due To The Use Of INT64.
#
PDP10 : 
        $! Sorry, Can't Build $(BIN_DIR)PDP10-$(ARCH).EXE Simulator
        $! Because It Requires The Use Of INT64.
.ENDIF

PDP11 : $(BIN_DIR)PDP11-$(ARCH).EXE
        $! PDP11 done

$(BIN_DIR)PDP11-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_LIB) $(PCAP_LIBD) $(PDP11_LIB1) $(PDP11_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)PDP11-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP11_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP11-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP11_LIB1)/LIBRARY,$(PDP11_LIB2)/LIBRARY,$(SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

PDP15 : $(BIN_DIR)PDP15-$(ARCH).EXE
        $! PDP15 done

$(BIN_DIR)PDP15-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(PDP15_LIB)
        $!
        $! Building The $(BIN_DIR)PDP15-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP15_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP15-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(PDP15_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

S3 : $(BIN_DIR)S3-$(ARCH).EXE
        $! S3 done

$(BIN_DIR)S3-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(S3_LIB)
        $!
        $! Building The $(BIN_DIR)S3-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(S3_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)S3-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(S3_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

SDS : $(BIN_DIR)SDS-$(ARCH).EXE
        $! SDS done

$(BIN_DIR)SDS-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(SDS_LIB)
        $!
        $! Building The $(BIN_DIR)SDS-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(SDS_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)SDS-$(ARCH).EXE -
                 $(BLD_DIR)SCP.OBJ,$(SDS_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

SSEM : $(BIN_DIR)SSEM-$(ARCH).EXE
        $! SSEM done

$(BIN_DIR)SSEM-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(SSEM_LIB)
        $!
        $! Building The $(BIN_DIR)SSEM-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(SSEM_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)SDS-$(ARCH).EXE -
                 $(BLD_DIR)SCP.OBJ,$(SDS_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

SWTP6800MP-A : $(BIN_DIR)SWTP6800MP-A-$(ARCH).EXE
        $! SWTP6800MP-A done

$(BIN_DIR)SWTP6800MP-A-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(SWTP6800MP_A_LIB)
        $!
        $! Building The $(BIN_DIR)SWTP6800MP-A-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(SWTP6800MP_A_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)SWTP6800MP-A-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(SWTP6800MP_A_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

SWTP6800MP-A2 : $(BIN_DIR)SWTP6800MP-A2-$(ARCH).EXE
        $! SWTP6800MP-A2 done

$(BIN_DIR)SWTP6800MP-A2-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(SWTP6800MP_A2_LIB)
        $!
        $! Building The $(BIN_DIR)SWTP6800MP-A2-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(SWTP6800MP_A2_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)SWTP6800MP-A2-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(SWTP6800MP_A2_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

VAX : MICROVAX3900
        $! MICROVAX3900 aka VAX done

MICROVAX3900 : $(BIN_DIR)MICROVAX3900-$(ARCH).EXE
        $! MICROVAX3900 done

$(BIN_DIR)MICROVAX3900-$(ARCH).EXE : $(SIMH_MAIN) $(VAX_SIMH_LIB) $(PCAP_LIBD) $(VAX_LIB1) $(VAX_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)MICROVAX3900-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(VAX_LIB1)/LIBRARY,$(VAX_LIB2)/LIBRARY,-
               $(VAX_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
        $ COPY $(BIN_DIR)MICROVAX3900-$(ARCH).EXE $(BIN_DIR)VAX-$(ARCH).EXE

MICROVAX1 : $(BIN_DIR)MICROVAX1-$(ARCH).EXE
        $! MICROVAX1 done

$(BIN_DIR)MICROVAX1-$(ARCH).EXE : $(SIMH_MAIN) $(VAX610_SIMH_LIB) $(PCAP_LIBD) $(VAX610_LIB1) $(VAX610_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX610-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX610_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)MICROVAX1-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX610_LIB1)/LIBRARY,$(VAX610_LIB2)/LIBRARY,-
               $(VAX610_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

MICROVAX2 : $(BIN_DIR)MICROVAX2-$(ARCH).EXE
        $! MICROVAX2 done

$(BIN_DIR)MICROVAX2-$(ARCH).EXE : $(SIMH_MAIN) $(VAX630_SIMH_LIB) $(PCAP_LIBD) $(VAX630_LIB1) $(VAX630_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX630-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX630_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)MICROVAX2-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX630_LIB1)/LIBRARY,$(VAX630_LIB2)/LIBRARY,-
               $(VAX630_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

RTVAX1000 : $(BIN_DIR)RTVAX1000-$(ARCH).EXE
        $! RTVAX1000 done

$(BIN_DIR)RTVAX1000-$(ARCH).EXE : $(SIMH_MAIN) $(VAX620_SIMH_LIB) $(PCAP_LIBD) $(VAX620_LIB1) $(VAX620_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX620-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX620_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)RTVAX1000-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX620_LIB1)/LIBRARY,$(VAX620_LIB2)/LIBRARY,-
               $(VAX620_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

VAX730 : $(BIN_DIR)VAX730-$(ARCH).EXE
        $! VAX730 done

$(BIN_DIR)VAX730-$(ARCH).EXE : $(SIMH_MAIN) $(VAX730_SIMH_LIB) $(PCAP_LIBD) $(VAX730_LIB1) $(VAX730_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX730-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX730_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)VAX730-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX730_LIB1)/LIBRARY,$(VAX730_LIB2)/LIBRARY,-
               $(VAX730_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

VAX750 : $(BIN_DIR)VAX750-$(ARCH).EXE
        $! VAX750 done

$(BIN_DIR)VAX750-$(ARCH).EXE : $(SIMH_MAIN) $(VAX750_SIMH_LIB) $(PCAP_LIBD) $(VAX750_LIB1) $(VAX750_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX750-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX750_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)VAX750-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX750_LIB1)/LIBRARY,$(VAX750_LIB2)/LIBRARY,-
               $(VAX750_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

VAX780 : $(BIN_DIR)VAX780-$(ARCH).EXE
        $! VAX780 done

$(BIN_DIR)VAX780-$(ARCH).EXE : $(SIMH_MAIN) $(VAX780_SIMH_LIB) $(PCAP_LIBD) $(VAX780_LIB1) $(VAX780_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX780-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX780_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)VAX780-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX780_LIB1)/LIBRARY,$(VAX780_LIB2)/LIBRARY,-
               $(VAX780_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

VAX8600 : $(BIN_DIR)VAX8600-$(ARCH).EXE
        $! VAX8600 done

$(BIN_DIR)VAX8600-$(ARCH).EXE : $(SIMH_MAIN) $(VAX8600_SIMH_LIB) $(PCAP_LIBD) $(VAX8600_LIB1) $(VAX8600_LIB2) $(PCAP_EXECLET)
        $!
        $! Building The $(BIN_DIR)VAX8600-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(VAX8600_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)$(LINK_SECTION_BINDING)-
               /EXE=$(BIN_DIR)VAX8600-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,-
               $(VAX8600_LIB1)/LIBRARY,$(VAX8600_LIB2)/LIBRARY,-
               $(VAX8600_SIMH_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*

#
# If Not On VAX, Build The IBM 7094 Simulator.
#
.IFDEF ALPHA_OR_IA64
I7094 : $(BIN_DIR)I7094-$(ARCH).EXE
        $! I7094 done

$(BIN_DIR)I7094-$(ARCH).EXE : $(SIMH_MAIN) $(SIMH_NONET_LIB) $(I7094_LIB)
        $!
        $! Building The $(BIN_DIR)I7094-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(I7094_OPTIONS)/OBJ=$(BLD_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)I7094-$(ARCH).EXE -
               $(BLD_DIR)SCP.OBJ,$(I7094_LIB)/LIBRARY,$(SIMH_NONET_LIB)/LIBRARY$(PCAP_LIBR)
        $ DELETE/NOLOG/NOCONFIRM $(BLD_DIR)*.OBJ;*
.ELSE
#
# Else We Are On VAX And Tell The User We Can't Build On VAX
# Due To The Use Of INT64.
#
I7094 : 
        $! Sorry, Can't Build $(BIN_DIR)I7094-$(ARCH).EXE Simulator
        $! Because It Requires The Use Of INT64.
.ENDIF

#
# PCAP VCI Components
#
$(PCAP_VCI) : $(PCAP_VCMDIR)PCAPVCM.EXE
        $!
        $! Installing the PCAP VCI Execlet in SYS$LOADABLE_IMAGES
        $!
        $ COPY $(PCAP_VCMDIR)PCAPVCM.EXE SYS$COMMON:[SYS$LDR]PCAPVCM.EXE 

$(PCAP_VCMDIR)PCAPVCM.EXE : $(PCAP_VCM_SOURCES) 
        $!
        $! Building The PCAP VCI Execlet
        $!
        $ @SYS$DISK:[-.PCAP-VMS.PCAPVCM]BUILD_PCAPVCM
        $ DELETE/NOLOG/NOCONFIRM $(PCAP_VCMDIR)*.OBJ;*,$(PCAP_VCMDIR)*.MAP;*
