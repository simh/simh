#
# DESCRIP.MMS
# Written By:  Robert Alan Byer
#              byer@mail.ourservers.net
#
# Modified By: Mark Pizzolato
#              mark@infocomm.com
#
# This MMS/MMK build script is used to compile the various simulators in
# the SIMH package for OpenVMS using DEC C v6.0-001.
#
# Notes:  On VAX, the PDP-10 and VAX simulator will not be built due to the
#         fact that INT64 is required for those simulators.
#
#         When using DEC's MMS on an Alpha you must use 
#         /MACRO=("__ALPHA__=1") to compile properly.
#
# This build script will accept the following build options.
#
#            ALL             Just Build "Everything".
#            ALTAIR          Just Build The MITS Altair.
#            ALTAIRZ80       Just Build The MITS Altair Z80.
#            ECLIPSE         Just Build The Data General Eclipse.
#            GRI             Just Build The GRI Corporation GRI-909.
#            H316            Just Build The Honewell 316/516.
#            HP2100          Just Build The Hewlett-Packard HP-2100. 
#            I1401           Just Build The IBM 1401.
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
#            VAX             Just Build The DEC VAX.
#            CLEAN           Will Clean Files Back To Base Kit.
#
# To build with debugging enabled (which will also enable traceback 
# information) use..
#
#        MMK/FORCE/MACRO=(DEBUG=1)
#
# This will produce an executable named {Simulator}-{VAX|AXP}-DBG.EXE
#

#
# Define The BIN Directory Where The Executables Will Go.
#
BIN_DIR = SYS$DISK:[.BIN]

#
# Define Our Library Directory.
#
LIB_DIR = SYS$DISK:[.LIB]

#
# Let's See If We Are Going To Build With DEBUG Enabled.
#
.IFDEF DEBUG
CC_DEBUG = /DEBUG=ALL
LINK_DEBUG = /DEBUG/TRACEBACK
CC_OPTIMIZE = /NOOPTIMIZE
.IFDEF __ALPHA__
CC_FLAGS = /PREFIX=ALL
ARCH = AXP-DBG
.ELSE
ARCH = VAX-DBG
CC_FLAGS = $(CC_FLAGS)
.ENDIF
.ELSE
CC_DEBUG = /NODEBUG
LINK_DEBUG = /NODEBUG/NOTRACEBACK
.IFDEF __ALPHA__
CC_OPTIMIZE = /OPTIMIZE=(LEVEL=5,TUNE=HOST)/ARCH=HOST
CC_FLAGS = /PREFIX=ALL
ARCH = AXP
.ELSE
CC_OPTIMIZE = /OPTIMIZE
ARCH = VAX
CC_FLAGS = $(CC_FLAGS)
.ENDIF
.ENDIF

#
# Define Our Compiler Flags
#
CC_FLAGS = $(CC_FLAGS)$(CC_DEBUG)$(CC_OPTIMIZE)/NEST=PRIMARY/NAME=(AS_IS,SHORTENED)

#
# Define The Compile Command.
#
CC = CC/DECC$(CC_FLAGS)

#
# First, Let's Check To Make Sure We Have A SYS$DISK:[.BIN] And 
# SYS$DISK:[.LIB] Directory.
#
.FIRST
  @ IF (F$SEARCH("SYS$DISK:[]BIN.DIR").EQS."") THEN CREATE/DIRECTORY $(BIN_DIR)
  @ IF (F$SEARCH("SYS$DISK:[]LIB.DIR").EQS."") THEN CREATE/DIRECTORY $(LIB_DIR)

#
# Core SIMH File Definitions.
#
SIMH_DIR = SYS$DISK:[]
SIMH_LIB = $(LIB_DIR)SIMH-$(ARCH).OLB
SIMH_SOURCE = $(SIMH_DIR)SCP_TTY.C,$(SIMH_DIR)SIM_SOCK.C,\
              $(SIMH_DIR)SIM_TMXR.C,$(SIMH_DIR)SIM_ETHER.C,\
              $(SIMH_DIR)SIM_TAPE.C
SIMH_OBJS = $(SIMH_DIR)SCP_TTY.OBJ,$(SIMH_DIR)SIM_SOCK.OBJ,\
            $(SIMH_DIR)SIM_TMXR.OBJ,$(SIMH_DIR)SIM_ETHER.OBJ,\
            $(SIMH_DIR)SIM_TAPE.OBJ

#
# MITS Altair Simulator Definitions.
#
ALTAIR_DIR = SYS$DISK:[.ALTAIR]
ALTAIR_LIB = $(LIB_DIR)ALTAIR-$(ARCH).OLB
ALTAIR_SOURCE = $(ALTAIR_DIR)ALTAIR_SIO.C,$(ALTAIR_DIR)ALTAIR_CPU.C,\
                $(ALTAIR_DIR)ALTAIR_DSK.C,$(ALTAIR_DIR)ALTAIR_SYS.C
ALTAIR_OBJS = $(ALTAIR_DIR)ALTAIR_SIO.OBJ,$(ALTAIR_DIR)ALTAIR_CPU.OBJ,\
              $(ALTAIR_DIR)ALTAIR_DSK.OBJ,$(ALTAIR_DIR)ALTAIR_SYS.OBJ
ALTAIR_OPTIONS = /INCLUDE=($(SIMH_DIR),$(ALTAIR_DIR))

#
# MITS Altair Z80 Simulator Definitions.
#
ALTAIRZ80_DIR = SYS$DISK:[.ALTAIRZ80]
ALTAIRZ80_LIB = $(LIB_DIR)ALTAIRZ80-$(ARCH).OLB
ALTAIRZ80_SOURCE = $(ALTAIRZ80_DIR)ALTAIRZ80_CPU.C,\
                   $(ALTAIRZ80_DIR)ALTAIRZ80_DSK.C,\
	           $(ALTAIRZ80_DIR)ALTAIRZ80_SIO.C,\
                   $(ALTAIRZ80_DIR)ALTAIRZ80_SYS.C,\
                   $(ALTAIRZ80_DIR)ALTAIRZ80_HDSK.C
ALTAIRZ80_OBJS = $(ALTAIRZ80_DIR)ALTAIRZ80_CPU.OBJ,\
                 $(ALTAIRZ80_DIR)ALTAIRZ80_DSK.OBJ,\
	         $(ALTAIRZ80_DIR)ALTAIRZ80_SIO.OBJ,\
                 $(ALTAIRZ80_DIR)ALTAIRZ80_SYS.OBJ,\
                 $(ALTAIRZ80_DIR)ALTAIRZ80_HDSK.OBJ
ALTAIRZ80_OPTIONS = /INCLUDE=($(SIMH_DIR),$(ALTAIRZ80_DIR))

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
              $(NOVA_DIR)NOVA_TT1.C
NOVA_OBJS = $(NOVA_DIR)NOVA_SYS.OBJ,$(NOVA_DIR)NOVA_CPU.OBJ,\
            $(NOVA_DIR)NOVA_DKP.OBJ,$(NOVA_DIR)NOVA_DSK.OBJ,\
            $(NOVA_DIR)NOVA_LP.OBJ,$(NOVA_DIR)NOVA_MTA.OBJ,\
            $(NOVA_DIR)NOVA_PLT.OBJ,$(NOVA_DIR)NOVA_PT.OBJ,\
            $(NOVA_DIR)NOVA_CLK.OBJ,$(NOVA_DIR)NOVA_TT.OBJ,\
            $(NOVA_DIR)NOVA_TT1.OBJ
NOVA_OPTIONS = /INCLUDE=($(SIMH_DIR),$(NOVA_DIR))

#
# Data General Eclipse Simulator Definitions.
#
ECLIPSE_LIB = $(LIB_DIR)ECLIPSE-$(ARCH).OLB
ECLIPSE_SOURCE = $(NOVA_DIR)ECLIPSE_CPU.C,$(NOVA_DIR)ECLIPSE_TT.C,\
                 $(NOVA_DIR)NOVA_SYS.C,$(NOVA_DIR)NOVA_DKP.C,\
                 $(NOVA_DIR)NOVA_DSK.C,$(NOVA_DIR)NOVA_LP.C,\
                 $(NOVA_DIR)NOVA_MTA.C,$(NOVA_DIR)NOVA_PLT.C,\
                 $(NOVA_DIR)NOVA_PT.C,$(NOVA_DIR)NOVA_CLK.C,\
                 $(NOVA_DIR)NOVA_TT1.C
ECLIPSE_OBJS = $(NOVA_DIR)ECLIPSE_CPU.OBJ,$(NOVA_DIR)ECLIPSE_TT.OBJ,\
               $(NOVA_DIR)NOVA_SYS.OBJ,$(NOVA_DIR)NOVA_DKP.OBJ,\
               $(NOVA_DIR)NOVA_DSK.OBJ,$(NOVA_DIR)NOVA_LP.OBJ,\
               $(NOVA_DIR)NOVA_MTA.OBJ,$(NOVA_DIR)NOVA_PLT.OBJ,\
               $(NOVA_DIR)NOVA_PT.OBJ,$(NOVA_DIR)NOVA_CLK.OBJ,\
               $(NOVA_DIR)NOVA_TT1.OBJ
ECLIPSE_OPTIONS = /INCLUDE=($(SIMH_DIR),$(NOVA_DIR))/DEFINE=("ECLIPSE=1")

#
# GRI Corporation GRI-909 Simulator Definitions.
#
GRI_DIR = SYS$DISK:[.GRI]
GRI_LIB = $(LIB_DIR)GRI-$(ARCH).OLB
GRI_SOURCE = $(GRI_DIR)GRI_CPU.C,$(GRI_DIR)GRI_STDDEV.C,$(GRI_DIR)GRI_SYS.C
GRI_OBJS = $(GRI_DIR)GRI_CPU.OBJ,$(GRI_DIR)GRI_STDDEV.OBJ,\
           $(GRI_DIR)GRI_SYS.OBJ
GRI_OPTIONS = /INCLUDE=($(SIMH_DIR),$(GRI_DIR))

#
# Honeywell 316/516 Simulator Definitions.
#
H316_DIR = SYS$DISK:[.H316]
H316_LIB = $(LIB_DIR)H316-$(ARCH).OLB
H316_SOURCE = $(H316_DIR)H316_STDDEV.C,$(H316_DIR)H316_LP.C,\
              $(H316_DIR)H316_CPU.C,$(H316_DIR)H316_SYS.C
H316_OBJS = $(H316_DIR)H316_STDDEV.OBJ,$(H316_DIR)H316_LP.OBJ,\
            $(H316_DIR)H316_CPU.OBJ,$(H316_DIR)H316_SYS.OBJ
H316_OPTIONS = /INCLUDE=($(SIMH_DIR),$(H316_DIR))

#
# Hewlett-Packard HP-2100 Simulator Definitions.
#
HP2100_DIR = SYS$DISK:[.HP2100]
HP2100_LIB = $(LIB_DIR)HP2100-$(ARCH).OLB
HP2100_SOURCE = $(HP2100_DIR)HP2100_STDDEV.C,$(HP2100_DIR)HP2100_DP.C,\
                $(HP2100_DIR)HP2100_DQ.C,$(HP2100_DIR)HP2100_DR.C,\
                $(HP2100_DIR)HP2100_LPS.C,$(HP2100_DIR)HP2100_MS.C,\
                $(HP2100_DIR)HP2100_MT.C,$(HP2100_DIR)HP2100_MUX.C,\
                $(HP2100_DIR)HP2100_CPU.C,$(HP2100_DIR)HP2100_FP.C,\
                $(HP2100_DIR)HP2100_SYS.C,$(HP2100_DIR)HP2100_LPT.C,\
                $(HP2100_DIR)HP2100_IPL.C
HP2100_OBJS = $(HP2100_DIR)HP2100_STDDEV.OBJ,$(HP2100_DIR)HP2100_DP.OBJ,\
              $(HP2100_DIR)HP2100_DQ.OBJ,$(HP2100_DIR)HP2100_DR.OBJ,\
              $(HP2100_DIR)HP2100_LPS.OBJ,$(HP2100_DIR)HP2100_MS.OBJ,\
              $(HP2100_DIR)HP2100_MT.OBJ,$(HP2100_DIR)HP2100_MUX.OBJ,\
              $(HP2100_DIR)HP2100_CPU.OBJ,$(HP2100_DIR)HP2100_FP.OBJ,\
              $(HP2100_DIR)HP2100_SYS.OBJ,$(HP2100_DIR)HP2100_LPT.OBJ,\
              $(HP2100_DIR)HP2100_IPL.OBJ
HP2100_OPTIONS = /INCLUDE=($(SIMH_DIR),$(HP2100_DIR))

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
ID16_OBJS = $(ID16_DIR)ID16_CPU.OBJ,$(ID16_DIR)ID16_SYS.OBJ,\
            $(ID16_DIR)ID_DP.OBJ,$(ID16_DIR)ID_FD.OBJ,$(ID16_DIR)ID_FP.OBJ,\
            $(ID16_DIR)ID_IDC.OBJ,$(ID16_DIR)ID_IO.OBJ,$(ID16_DIR)ID_LP.OBJ,\
            $(ID16_DIR)ID_MT.OBJ,$(ID16_DIR)ID_PAS.OBJ,$(ID16_DIR)ID_PT.OBJ,\
            $(ID16_DIR)ID_TT.OBJ,$(ID16_DIR)ID_UVC.OBJ,\
            $(ID16_DIR)ID16_DBOOT.OBJ,$(ID16_DIR)ID_TTP.OBJ
ID16_OPTIONS = /INCLUDE=($(SIMH_DIR),$(ID16_DIR))

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
ID32_OBJS = $(ID32_DIR)ID32_CPU.OBJ,$(ID32_DIR)ID32_SYS.OBJ,\
            $(ID32_DIR)ID_DP.OBJ,$(ID32_DIR)ID_FD.OBJ,\
            $(ID32_DIR)ID_FP.OBJ,$(ID32_DIR)ID_IDC.OBJ,\
            $(ID32_DIR)ID_IO.OBJ,$(ID32_DIR)ID_LP.OBJ,$(ID32_DIR)ID_MT.OBJ,\
            $(ID32_DIR)ID_PAS.OBJ,$(ID32_DIR)ID_PT.OBJ,$(ID32_DIR)ID_TT.OBJ,\
            $(ID32_DIR)ID_UVC.OBJ,$(ID32_DIR)ID32_DBOOT.OBJ,\
            $(ID32_DIR)ID_TTP.OBJ
ID32_OPTIONS = /INCLUDE=($(SIMH_DIR),$(ID32_DIR))

#
# IBM 1130 Simulator Definitions.
#
IBM1130_DIR = SYS$DISK:[.IBM1130]
IBM1130_LIB = $(LIB_DIR)IBM1130-$(ARCH).OLB
IBM1130_SOURCE = $(IBM1130_DIR)IBM1130_CPU.C,$(IBM1130_DIR)IBM1130_CR.C,\
                 $(IBM1130_DIR)IBM1130_DISK.C,$(IBM1130_DIR)IBM1130_STDDEV.C,\
                 $(IBM1130_DIR)IBM1130_SYS.C,$(IBM1130_DIR)IBM1130_GDU.C,\
                 $(IBM1130_DIR)IBM1130_GUI.C,$(IBM1130_DIR)IBM1130_PRT.C,\
                 $(IBM1130_DIR)IBM1130_FMT.C
IBM1130_OBJS = $(IBM1130_DIR)IBM1130_CPU.OBJ,$(IBM1130_DIR)IBM1130_CR.OBJ,\
               $(IBM1130_DIR)IBM1130_DISK.OBJ,$(IBM1130_DIR)IBM1130_STDDEV.OBJ,\
               $(IBM1130_DIR)IBM1130_SYS.OBJ,$(IBM1130_DIR)IBM1130_GDU.OBJ,\
               $(IBM1130_DIR)IBM1130_GUI.OBJ,$(IBM1130_DIR)IBM1130_PRT.OBJ,\
               $(IBM1130_DIR)IBM1130_FMT.OBJ
IBM1130_OPTIONS = /INCLUDE=($(SIMH_DIR),$(IBM1130_DIR))

#
# IBM 1401 Simulator Definitions.
#
I1401_DIR = SYS$DISK:[.I1401]
I1401_LIB = $(LIB_DIR)I1401-$(ARCH).OLB
I1401_SOURCE = $(I1401_DIR)I1401_LP.C,$(I1401_DIR)I1401_CPU.C,\
               $(I1401_DIR)I1401_IQ.C,$(I1401_DIR)I1401_CD.C,\
               $(I1401_DIR)I1401_MT.C,$(I1401_DIR)I1401_DP.C,\
               $(I1401_DIR)I1401_SYS.C
I1401_OBJS = $(I1401_DIR)I1401_LP.OBJ,$(I1401_DIR)I1401_CPU.OBJ,\
             $(I1401_DIR)I1401_IQ.OBJ,$(I1401_DIR)I1401_CD.OBJ,\
             $(I1401_DIR)I1401_MT.OBJ,$(I1401_DIR)I1401_DP.OBJ,\
             $(I1401_DIR)I1401_SYS.OBJ
I1401_OPTIONS = /INCLUDE=($(SIMH_DIR),$(I1401_DIR))


#
# IBM 1620 Simulators Definitions.
#
I1620_DIR = SYS$DISK:[.I1620]
I1620_LIB = $(LIB_DIR)I1620-$(ARCH).OLB
I1620_SOURCE = $(I1620_DIR)I1620_CD.C,$(I1620_DIR)I1620_DP.C,\
               $(I1620_DIR)I1620_PT.C,$(I1620_DIR)I1620_TTY.C,\
               $(I1620_DIR)I1620_CPU.C,$(I1620_DIR)I1620_LP.C,\
               $(I1620_DIR)I1620_FP.C,$(I1620_DIR)I1620_SYS.C
I1620_OBJS = $(I1620_DIR)I1620_CD.OBJ,$(I1620_DIR)I1620_DP.OBJ,\
             $(I1620_DIR)I1620_PT.OBJ,$(I1620_DIR)I1620_TTY.OBJ,\
             $(I1620_DIR)I1620_CPU.OBJ,$(I1620_DIR)I1620_LP.OBJ,\
             $(I1620_DIR)I1620_FP.OBJ,$(I1620_DIR)I1620_SYS.OBJ
I1620_OPTIONS = /INCLUDE=($(SIMH_DIR),$(I1620_DIR))

#
# PDP-1 Simulator Definitions.
#
PDP1_DIR = SYS$DISK:[.PDP1]
PDP1_LIB = $(LIB_DIR)PDP1-$(ARCH).OLB
PDP1_SOURCE = $(PDP1_DIR)PDP1_LP.C,$(PDP1_DIR)PDP1_CPU.C,\
              $(PDP1_DIR)PDP1_STDDEV.C,$(PDP1_DIR)PDP1_SYS.C,\
              $(PDP1_DIR)PDP1_DT.C,$(PDP1_DIR)PDP1_DRM.C
PDP1_OBJS = $(PDP1_DIR)PDP1_LP.OBJ,$(PDP1_DIR)PDP1_CPU.OBJ,\
            $(PDP1_DIR)PDP1_STDDEV.OBJ,$(PDP1_DIR)PDP1_SYS.OBJ,\
            $(PDP1_DIR)PDP1_DT.OBJ,$(PDP1_DIR)PDP1_DRM.OBJ
PDP1_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP1_DIR))

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
	      $(PDP8_DIR)PDP8_TTX.C,$(PDP8_DIR)PDP8_RL.C
PDP8_OBJS = $(PDP8_DIR)PDP8_CPU.OBJ,$(PDP8_DIR)PDP8_CLK.OBJ,\
            $(PDP8_DIR)PDP8_DF.OBJ,$(PDP8_DIR)PDP8_DT.OBJ,\
            $(PDP8_DIR)PDP8_LP.OBJ,$(PDP8_DIR)PDP8_MT.OBJ,\
	    $(PDP8_DIR)PDP8_PT.OBJ,$(PDP8_DIR)PDP8_RF.OBJ,\
            $(PDP8_DIR)PDP8_RK.OBJ,$(PDP8_DIR)PDP8_RX.OBJ,\
            $(PDP8_DIR)PDP8_SYS.OBJ,$(PDP8_DIR)PDP8_TT.OBJ,\
	    $(PDP8_DIR)PDP8_TTX.OBJ,$(PDP8_DIR)PDP8_RL.OBJ
PDP8_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP8_DIR))

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
PDP18B_OBJS = $(PDP18B_DIR)PDP18B_DT.OBJ,$(PDP18B_DIR)PDP18B_DRM.OBJ,\
              $(PDP18B_DIR)PDP18B_CPU.OBJ,$(PDP18B_DIR)PDP18B_LP.OBJ,\
              $(PDP18B_DIR)PDP18B_MT.OBJ,$(PDP18B_DIR)PDP18B_RF.OBJ,\
              $(PDP18B_DIR)PDP18B_RP.OBJ,$(PDP18B_DIR)PDP18B_STDDEV.OBJ,\
              $(PDP18B_DIR)PDP18B_SYS.OBJ,$(PDP18B_DIR)PDP18B_TT1.OBJ,\
              $(PDP18B_DIR)PDP18B_RB.OBJ,$(PDP18B_DIR)PDP18B_FPP.OBJ
PDP4_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP18B_DIR))/DEFINE=("PDP4=1")
PDP7_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP18B_DIR))/DEFINE=("PDP7=1")
PDP9_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP18B_DIR))/DEFINE=("PDP9=1")
PDP15_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP18B_DIR))/DEFINE=("PDP15=1")

#
# Digital Equipment PDP-11 Simulator Definitions.
#
PDP11_DIR = SYS$DISK:[.PDP11]
PDP11_LIB = $(LIB_DIR)PDP11-$(ARCH).OLB
PDP11_SOURCE = $(PDP11_DIR)PDP11_FP.C,$(PDP11_DIR)PDP11_CPU.C,\
               $(PDP11_DIR)PDP11_DZ.C,$(PDP11_DIR)PDP11_CIS.C,\
               $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_RK.C,\
	       $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RP.C,\
               $(PDP11_DIR)PDP11_RX.C,$(PDP11_DIR)PDP11_STDDEV.C,\
               $(PDP11_DIR)PDP11_SYS.C,$(PDP11_DIR)PDP11_TC.C,\
	       $(PDP11_DIR)PDP11_TM.C,$(PDP11_DIR)PDP11_TS.C,\
               $(PDP11_DIR)PDP11_IO.C,$(PDP11_DIR)PDP11_RQ.C,\
               $(PDP11_DIR)PDP11_TQ.C,$(PDP11_DIR)PDP11_PCLK.C,\
               $(PDP11_DIR)PDP11_RY.C,$(PDP11_DIR)PDP11_PT.C,\
               $(PDP11_DIR)PDP11_HK.C,$(PDP11_DIR)PDP11_XQ.C,\
               $(PDP11_DIR)PDP11_XU.C
PDP11_OBJS = $(PDP11_DIR)PDP11_FP.OBJ,$(PDP11_DIR)PDP11_CPU.OBJ,\
             $(PDP11_DIR)PDP11_DZ.OBJ,$(PDP11_DIR)PDP11_CIS.OBJ,\
             $(PDP11_DIR)PDP11_LP.OBJ,$(PDP11_DIR)PDP11_RK.OBJ,\
             $(PDP11_DIR)PDP11_RL.OBJ,$(PDP11_DIR)PDP11_RP.OBJ,\
             $(PDP11_DIR)PDP11_RX.OBJ,$(PDP11_DIR)PDP11_STDDEV.OBJ,\
             $(PDP11_DIR)PDP11_SYS.OBJ,$(PDP11_DIR)PDP11_TC.OBJ,\
	     $(PDP11_DIR)PDP11_TM.OBJ,$(PDP11_DIR)PDP11_TS.OBJ,\
             $(PDP11_DIR)PDP11_IO.OBJ,$(PDP11_DIR)PDP11_RQ.OBJ,\
             $(PDP11_DIR)PDP11_TQ.OBJ,$(PDP11_DIR)PDP11_PCLK.OBJ,\
             $(PDP11_DIR)PDP11_RY.OBJ,$(PDP11_DIR)PDP11_PT.OBJ,\
             $(PDP11_DIR)PDP11_HK.OBJ,$(PDP11_DIR)PDP11_XQ.OBJ,\
             $(PDP11_DIR)PDP11_XU.OBJ
PDP11_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP11_DIR))/DEFINE=("VM_PDP11=1")

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
               $(PDP11_DIR)PDP11_RY.C,$(PDP11_DIR)PDP11_XU.C
PDP10_OBJS = $(PDP10_DIR)PDP10_FE.OBJ,\
             $(PDP10_DIR)PDP10_CPU.OBJ,$(PDP10_DIR)PDP10_KSIO.OBJ,\
             $(PDP10_DIR)PDP10_LP20.OBJ,$(PDP10_DIR)PDP10_MDFP.OBJ,\
             $(PDP10_DIR)PDP10_PAG.OBJ,$(PDP10_DIR)PDP10_XTND.OBJ,\
             $(PDP10_DIR)PDP10_RP.OBJ,$(PDP10_DIR)PDP10_SYS.OBJ,\
             $(PDP10_DIR)PDP10_TIM.OBJ,$(PDP10_DIR)PDP10_TU.OBJ,\
	       $(PDP10_DIR)PDP11_PT.OBJ,$(PDP10_DIR)PDP11_DZ.OBJ,\
             $(PDP10_DIR)PDP11_RY.OBJ,$(PDP10_DIR)PDP11_XU.OBJ
PDP10_OPTIONS = /INCLUDE=($(SIMH_DIR),$(PDP10_DIR),$(PDP11_DIR))/DEFINE=("USE_INT64=1","VM_PDP10=1")

#
# IBM System 3 Simulator Definitions.
#
S3_DIR = SYS$DISK:[.S3]
S3_LIB = $(LIB_DIR)S3-$(ARCH).OLB
S3_SOURCE = $(S3_DIR)S3_CD.C,$(S3_DIR)S3_CPU.C,$(S3_DIR)S3_DISK.C,\
            $(S3_DIR)S3_LP.C,$(S3_DIR)S3_PKB.C,$(S3_DIR)S3_SYS.C
S3_OBJS = $(S3_DIR)S3_CD.OBJ,$(S3_DIR)S3_CPU.OBJ,$(S3_DIR)S3_DISK.OBJ,\
          $(S3_DIR)S3_LP.OBJ,$(S3_DIR)S3_PKB.OBJ,$(S3_DIR)S3_SYS.OBJ
S3_OPTIONS = /INCLUDE=($(SIMH_DIR),$(S3_DIR))

#
# SDS 940
#
SDS_DIR = SYS$DISK:[.SDS]
SDS_LIB = $(LIB_DIR)SDS-$(ARCH).OLB
SDS_SOURCE = $(SDS_DIR)SDS_CPU.C,$(SDS_DIR)SDS_DRM.C,$(SDS_DIR)SDS_DSK.C,\ 
             $(SDS_DIR)SDS_IO.C,$(SDS_DIR)SDS_LP.C,$(SDS_DIR)SDS_MT.C,\
             $(SDS_DIR)SDS_MUX.C,$(SDS_DIR)SDS_RAD.C,$(SDS_DIR)SDS_STDDEV.C,\
             $(SDS_DIR)SDS_SYS.C
SDS_OBJS = $(SDS_DIR)SDS_CPU.OBJ,$(SDS_DIR)SDS_DRM.OBJ,$(SDS_DIR)SDS_DSK.OBJ,\ 
           $(SDS_DIR)SDS_IO.OBJ,$(SDS_DIR)SDS_LP.OBJ,$(SDS_DIR)SDS_MT.OBJ,\
           $(SDS_DIR)SDS_MUX.OBJ,$(SDS_DIR)SDS_RAD.OBJ,\
           $(SDS_DIR)SDS_STDDEV.OBJ,$(SDS_DIR)SDS_SYS.OBJ
SDS_OPTIONS = /INCLUDE=($(SIMH_DIR),$(SDS_DIR))

#
# Digital Equipment VAX Simulator Definitions.
#
VAX_DIR = SYS$DISK:[.VAX]
VAX_LIB = $(LIB_DIR)VAX-$(ARCH).OLB
VAX_SOURCE = $(VAX_DIR)VAX_CPU1.C,$(VAX_DIR)VAX_CPU.C,\
             $(VAX_DIR)VAX_FPA.C,$(VAX_DIR)VAX_IO.C,\
             $(VAX_DIR)VAX_MMU.C,$(VAX_DIR)VAX_STDDEV.C,\
             $(VAX_DIR)VAX_SYS.C,$(VAX_DIR)VAX_SYSDEV.C,\
	     $(PDP11_DIR)PDP11_RL.C,$(PDP11_DIR)PDP11_RQ.C,\
             $(PDP11_DIR)PDP11_TS.C,$(PDP11_DIR)PDP11_DZ.C,\
             $(PDP11_DIR)PDP11_LP.C,$(PDP11_DIR)PDP11_TQ.C,\
             $(PDP11_DIR)PDP11_PT.C,$(PDP11_DIR)PDP11_XQ.C
VAX_OBJS = $(VAX_DIR)VAX_CPU1.OBJ,$(VAX_DIR)VAX_CPU.OBJ,\
           $(VAX_DIR)VAX_FPA.OBJ,$(VAX_DIR)VAX_IO.OBJ,\
           $(VAX_DIR)VAX_MMU.OBJ,$(VAX_DIR)VAX_STDDEV.OBJ,\
           $(VAX_DIR)VAX_SYS.OBJ,$(VAX_DIR)VAX_SYSDEV.OBJ,\
           $(VAX_DIR)PDP11_RL.OBJ,$(VAX_DIR)PDP11_RQ.OBJ,\
           $(VAX_DIR)PDP11_TS.OBJ,$(VAX_DIR)PDP11_DZ.OBJ,\
           $(VAX_DIR)PDP11_LP.OBJ,$(VAX_DIR)PDP11_TQ.OBJ,\
           $(VAX_DIR)PDP11_PT.OBJ,$(VAX_DIR)PDP11_XQ.OBJ
#
# If On Alpha, Define "USE_INT64" As We Have INT64.
#
.IFDEF __ALPHA__
VAX_OPTIONS = /INCLUDE=($(SIMH_DIR),$(VAX_DIR),$(PDP11_DIR))/DEFINE=("USE_INT64=1","VM_VAX=1")
.ELSE
#
# We Are On A VAX Platform So Don't Define "USE_INT64" As We Don't Have
# INT64.
#
VAX_OPTIONS = /INCLUDE=($(SIMH_DIR),$(VAX_DIR),$(PDP11_DIR))/DEFINE=("VM_VAX=1")
.ENDIF

#
# If On Alpha, Build Everything.
#
.IFDEF __ALPHA__
ALL : ALTAIR ALTAIRZ80 ECLIPSE GRI H316 HP2100 I1401 I1620 IBM1130 ID16 ID32 \
      NOVA PDP1 PDP4 PDP7 PDP8 PDP9 PDP10 PDP11 PDP15 S3 VAX SDS
.ELSE
#
# Else We Are On VAX And Build Everything EXCEPT The PDP-10 Since VAX
# Dosen't Have INT64
#
ALL : ALTAIR ALTAIRZ80 ECLIPSE GRI H316 HP2100 I1401 I1620 IBM1130 ID16 ID32 \
      NOVA PDP1 PDP4 PDP7 PDP8 PDP9 PDP11 PDP15 S3 VAX SDS
.ENDIF

CLEAN : 
	$!
	$! Clean out all targets and building Remnants
	$!
	$ IF (F$SEARCH("$(BIN_DIR)*.EXE;*").NES."") THEN -
	     DELETE/NOLOG/NOCONFIRM $(BIN_DIR)*.EXE;*
	$ IF (F$SEARCH("$(LIB_DIR)*.EXE;*").NES."") THEN -
	     DELETE/NOLOG/NOCONFIRM $(LIB_DIR)*.OLB;*
	$ IF (F$SEARCH("SYS$DISK:[...]*.OBJ;*").NES."") THEN -
	     DELETE/NOLOG/NOCONFIRM SYS$DISK:[...]*.OBJ;*

#
# Build The Libraries.
#
$(LIB_DIR)SIMH-$(ARCH).OLB : $(SIMH_SOURCE)
                             $!
			     $! Building The $(SIMH_LIB) Library.
                             $!
                             $ $(CC)/OBJECT=$(SIMH_DIR) $(SIMH_SOURCE)
                             $ IF (F$SEARCH("$(SIMH_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(SIMH_LIB)
                             $ LIBRARY/REPLACE $(SIMH_LIB) $(SIMH_OBJS)
	                     $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

$(LIB_DIR)ALTAIR-$(ARCH).OLB : $(ALTAIR_SOURCE)
                               $!
			       $! Building The $(ALTAIR_LIB) Library.
                               $!
                               $ $(CC)$(ALTAIR_OPTIONS)/OBJECT=$(ALTAIR_DIR) -
                                      $(ALTAIR_SOURCE)
                               $ IF (F$SEARCH("$(ALTAIR_LIB)").EQS."") THEN -
                                    LIBRARY/CREATE $(ALTAIR_LIB)
                               $ LIBRARY/REPLACE $(ALTAIR_LIB) -
                                                 $(ALTAIR_OBJS)
                               $ DELETE/NOLOG/NOCONFIRM $(ALTAIR_DIR)*.OBJ;*

$(LIB_DIR)ALTAIRZ80-$(ARCH).OLB : $(ALTAIRZ80_SOURCE)
                                  $!
			          $! Building The $(ALTAIRZ80_LIB) Library.
                                  $!
                                  $ $(CC)$(ALTAIRZ80_OPTIONS) -
                                         /OBJECT=$(ALTAIRZ80_DIR) -
                                         $(ALTAIRZ80_SOURCE)
                                  $ IF (F$SEARCH("$(ALTAIRZ80_LIB)").EQS."") -
                                       THEN LIBRARY/CREATE $(ALTAIRZ80_LIB)
                                  $ LIBRARY/REPLACE $(ALTAIRZ80_LIB) -
                                                    $(ALTAIRZ80_OBJS)
                                  $ DELETE/NOLOG/NOCONFIRM -
                                          $(ALTAIRZ80_DIR)*.OBJ;*

$(LIB_DIR)ECLIPSE-$(ARCH).OLB : $(ECLIPSE_SOURCE)
                                $!
			        $! Building The $(ECLIPSE_LIB) Library.
                                $!
                                $ $(CC)$(ECLIPSE_OPTIONS)/OBJECT=$(NOVA_DIR) -
                                       $(ECLIPSE_SOURCE)
                                $ IF (F$SEARCH("$(ECLIPSE_LIB)").EQS."") THEN -
                                     LIBRARY/CREATE $(ECLIPSE_LIB)
                                $ LIBRARY/REPLACE $(ECLIPSE_LIB) -
                                                  $(ECLIPSE_OBJS)
                                $ DELETE/NOLOG/NOCONFIRM $(NOVA_DIR)*.OBJ;*

$(LIB_DIR)GRI-$(ARCH).OLB : $(GRI_SOURCE)
                            $!
			    $! Building The $(GRI_LIB) Library.
                            $!
                            $ $(CC)$(GRI_OPTIONS)/OBJECT=$(GRI_DIR) -
                                   $(GRI_SOURCE)
                            $ IF (F$SEARCH("$(GRI_LIB)").EQS."") THEN -
                                 LIBRARY/CREATE $(GRI_LIB)
                            $ LIBRARY/REPLACE $(GRI_LIB) $(GRI_OBJS)
                            $ DELETE/NOLOG/NOCONFIRM $(GRI_DIR)*.OBJ;*

$(LIB_DIR)H316-$(ARCH).OLB : $(H316_SOURCE)
                             $!
			     $! Building The $(H316_LIB) Library.
                             $!
                             $ $(CC)$(H316_OPTIONS)/OBJECT=$(H316_DIR) -
                                    $(H316_SOURCE)
                             $ IF (F$SEARCH("$(H316_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(H316_LIB)
                             $ LIBRARY/REPLACE $(H316_LIB) $(H316_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(H316_DIR)*.OBJ;*

$(LIB_DIR)HP2100-$(ARCH).OLB : $(HP2100_SOURCE)
                               $!
			       $! Building The $(HP2100_LIB) Library.
                               $!
                               $ $(CC)$(HP2100_OPTIONS)/OBJECT=$(HP2100_DIR) -
                                      $(HP2100_SOURCE)
                               $ IF (F$SEARCH("$(HP2100_LIB)").EQS."") THEN -
                                    LIBRARY/CREATE $(HP2100_LIB)
                               $ LIBRARY/REPLACE $(HP2100_LIB) $(HP2100_OBJS)
                               $ DELETE/NOLOG/NOCONFIRM $(HP2100_DIR)*.OBJ;*

$(LIB_DIR)I1401-$(ARCH).OLB  : $(I1401_SOURCE)
                               $!
			       $! Building The $(I1401_LIB) Library.
                               $!
	                       $ $(CC)$(I1401_OPTIONS)/OBJECT=$(I1401_DIR) -
                                      $(I1401_SOURCE)
                               $ IF (F$SEARCH("$(I1401_LIB)").EQS."") THEN -
                                    LIBRARY/CREATE $(I1401_LIB)
                               $ LIBRARY/REPLACE $(I1401_LIB) $(I1401_OBJS)
                               $ DELETE/NOLOG/NOCONFIRM $(I1401_DIR)*.OBJ;*

$(LIB_DIR)I1620-$(ARCH).OLB : $(I1620_SOURCE)
                              $!
			      $! Building The $(I1620_LIB) Library.
                              $!
	                      $ $(CC)$(I1620_OPTIONS)/OBJECT=$(I1620_DIR) -
                                     $(I1620_SOURCE)
                              $ IF (F$SEARCH("$(I1620_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(I1620_LIB)
                              $ LIBRARY/REPLACE $(I1620_LIB) $(I1620_OBJS)
                              $ DELETE/NOLOG/NOCONFIRM $(I1620_DIR)*.OBJ;*

$(LIB_DIR)IBM1130-$(ARCH).OLB : $(IBM1130_SOURCE)
                                $!
			        $! Building The $(IBM1130_LIB) Library.
                                $!
	                        $ $(CC)$(IBM1130_OPTIONS) -
                                       /OBJECT=$(IBM1130_DIR) -
                                       $(IBM1130_SOURCE)
                                $ IF (F$SEARCH("$(IBM1130_LIB)").EQS."") THEN -
                                     LIBRARY/CREATE $(IBM1130_LIB)
                                $ LIBRARY/REPLACE $(IBM1130_LIB) $(IBM1130_OBJS)
                                $ DELETE/NOLOG/NOCONFIRM $(IBM1130_DIR)*.OBJ;*

$(LIB_DIR)ID16-$(ARCH).OLB : $(ID16_SOURCE)
                             $!
			     $! Building The $(ID16_LIB) Library.
                             $!
	                     $ $(CC)$(ID16_OPTIONS)/OBJECT=$(ID16_DIR) -
                                    $(ID16_SOURCE)
                             $ IF (F$SEARCH("$(ID16_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(ID16_LIB)
                             $ LIBRARY/REPLACE $(ID16_LIB) $(ID16_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(ID16_DIR)*.OBJ;*

$(LIB_DIR)ID32-$(ARCH).OLB : $(ID32_SOURCE)
                             $!
			     $! Building The $(ID32_LIB) Library.
                             $!
	                     $ $(CC)$(ID32_OPTIONS)/OBJECT=$(ID32_DIR) -
                                    $(ID32_SOURCE)
                             $ IF (F$SEARCH("$(ID32_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(ID32_LIB)
                             $ LIBRARY/REPLACE $(ID32_LIB) $(ID32_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(ID32_DIR)*.OBJ;*

$(LIB_DIR)NOVA-$(ARCH).OLB : $(NOVA_SOURCE)
                             $!
			     $! Building The $(NOVA_LIB) Library.
                             $!
                             $ $(CC)$(NOVA_OPTIONS)/OBJECT=$(NOVA_DIR) -
                                    $(NOVA_SOURCE)
                             $ IF (F$SEARCH("$(NOVA_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(NOVA_LIB)
                             $ LIBRARY/REPLACE $(NOVA_LIB) $(NOVA_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(NOVA_DIR)*.OBJ;*

$(LIB_DIR)PDP1-$(ARCH).OLB : $(PDP1_SOURCE)
                             $!
			     $! Building The $(PDP1_LIB) Library.
                             $!
                             $ $(CC)$(PDP1_OPTIONS)/OBJECT=$(PDP1_DIR) -
                                    $(PDP1_SOURCE)
                             $ IF (F$SEARCH("$(PDP1_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(PDP1_LIB)
                             $ LIBRARY/REPLACE $(PDP1_LIB) $(PDP1_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(PDP1_DIR)*.OBJ;*

$(LIB_DIR)PDP4-$(ARCH).OLB : $(PDP18B_SOURCE)
                             $!
			     $! Building The $(PDP4_LIB) Library.
                             $!
                             $ $(CC)$(PDP4_OPTIONS)/OBJECT=$(PDP18B_DIR) -
                                    $(PDP18B_SOURCE)
                             $ IF (F$SEARCH("$(PDP4_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(PDP4_LIB)
                             $ LIBRARY/REPLACE $(PDP4_LIB) $(PDP18B_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(PDP18B_DIR)*.OBJ;*

$(LIB_DIR)PDP7-$(ARCH).OLB : $(PDP18B_SOURCE)
                             $!
			     $! Building The $(PDP7_LIB) Library.
                             $!
                             $ $(CC)$(PDP7_OPTIONS)/OBJECT=$(PDP18B_DIR) -
                                    $(PDP18B_SOURCE)
                             $ IF (F$SEARCH("$(PDP7_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(PDP7_LIB)
                             $ LIBRARY/REPLACE $(PDP7_LIB) $(PDP18B_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(PDP18B_DIR)*.OBJ;*

$(LIB_DIR)PDP8-$(ARCH).OLB : $(PDP8_SOURCE)
                             $!
			     $! Building The $(PDP8_LIB) Library.
                             $!
                             $ $(CC)$(PDP8_OPTIONS)/OBJECT=$(PDP8_DIR) -
                                    $(PDP8_SOURCE)
                             $ IF (F$SEARCH("$(PDP8_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(PDP8_LIB)
                             $ LIBRARY/REPLACE $(PDP8_LIB) $(PDP8_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(PDP8_DIR)*.OBJ;*

$(LIB_DIR)PDP9-$(ARCH).OLB : $(PDP18B_SOURCE)
                             $!
			     $! Building The $(PDP9_LIB) Library.
                             $!
                             $ $(CC)$(PDP9_OPTIONS)/OBJECT=$(PDP18B_DIR) -
                                    $(PDP18B_SOURCE)
                             $ IF (F$SEARCH("$(PDP9_LIB)").EQS."") THEN -
                                  LIBRARY/CREATE $(PDP9_LIB)
                             $ LIBRARY/REPLACE $(PDP9_LIB) $(PDP18B_OBJS)
                             $ DELETE/NOLOG/NOCONFIRM $(PDP18B_DIR)*.OBJ;*

#
# If On Alpha, Build The PDP-10 Library.
#
.IFDEF __ALPHA__
$(LIB_DIR)PDP10-$(ARCH).OLB : $(PDP10_SOURCE)
                              $!
			      $! Building The $(PDP10_LIB) Library.
                              $!
                              $ $(CC)$(PDP10_OPTIONS)/OBJECT=$(PDP10_DIR) -
                                     $(PDP10_SOURCE)
                              $ IF (F$SEARCH("$(PDP10_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(PDP10_LIB)
                              $ LIBRARY/REPLACE $(PDP10_LIB) $(PDP10_OBJS)
                              DELETE/NOLOG/NOCONFIRM $(PDP10_DIR)*.OBJ;*
.ELSE
#
# We Are On VAX And Due To The Use of INT64 We Can't Build It.
#
$(LIB_DIR)PDP10-$(ARCH).OLB : 
                              $!
			      $! Due To The Use Of INT64 We Can't Build The
                              $! $(LIB_DIR)PDP10-$(ARCH).OLB Library On VAX.
                              $!
.ENDIF

$(LIB_DIR)PDP11-$(ARCH).OLB : $(PDP11_SOURCE)
                              $!
			      $! Building The $(PDP11_LIB) Library.
                              $!
                              $(CC)$(PDP11_OPTIONS)/OBJECT=$(PDP11_DIR) -
                                   $(PDP11_SOURCE)
                              $ IF (F$SEARCH("$(PDP11_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(PDP11_LIB)
                              $ LIBRARY/REPLACE $(PDP11_LIB) $(PDP11_OBJS)
                              $ DELETE/NOLOG/NOCONFIRM $(PDP11_DIR)*.OBJ;*

$(LIB_DIR)PDP15-$(ARCH).OLB : $(PDP18B_SOURCE)
                              $!
			      $! Building The $(PDP15_LIB) Library.
                              $!
                              $ $(CC)$(PDP15_OPTIONS)/OBJECT=$(PDP18B_DIR) -
                                     $(PDP18B_SOURCE)
                              $ IF (F$SEARCH("$(PDP15_LIB)").EQS."") THEN -
                                   LIBRARY/CREATE $(PDP15_LIB)
                              $ LIBRARY/REPLACE $(PDP15_LIB) $(PDP18B_OBJS)
                              $ DELETE/NOLOG/NOCONFIRM $(PDP18B_DIR)*.OBJ;*

$(LIB_DIR)S3-$(ARCH).OLB : $(S3_SOURCE)
                           $!
			   $! Building The $(S3_LIB) Library.
                           $!
                           $ $(CC)$(S3_OPTIONS)/OBJECT=$(S3_DIR) $(S3_SOURCE)
                           $ IF (F$SEARCH("$(S3_LIB)").EQS."") THEN -
                                LIBRARY/CREATE $(S3_LIB)
                           $ LIBRARY/REPLACE $(S3_LIB) $(S3_OBJS)
                           $ DELETE/NOLOG/NOCONFIRM $(S3_DIR)*.OBJ;*

$(LIB_DIR)SDS-$(ARCH).OLB : $(SDS_SOURCE)
                            $!
			    $! Building The $(SDS_LIB) Library.
                            $!
                            $ $(CC)$(SDS_OPTIONS)/OBJECT=$(SDS_DIR) -
                                   $(SDS_SOURCE)
                            $ IF (F$SEARCH("$(SDS_LIB)").EQS."") THEN -
                                 LIBRARY/CREATE $(SDS_LIB)
                            $ LIBRARY/REPLACE $(SDS_LIB) $(SDS_OBJS)
                            $ DELETE/NOLOG/NOCONFIRM $(SDS_DIR)*.OBJ;*

#
# If On Alpha, Build The VAX Library.
#
$(LIB_DIR)VAX-$(ARCH).OLB : $(VAX_SOURCE)
                           $!
			   $! Building The $(VAX_LIB) Library.
                           $!
                           $ $(CC)$(VAX_OPTIONS)/OBJECT=$(VAX_DIR) -
                                  $(VAX_SOURCE)
                           $ IF (F$SEARCH("$(VAX_LIB)").EQS."") THEN -
                                LIBRARY/CREATE $(VAX_LIB)
                           $ LIBRARY/REPLACE $(VAX_LIB) $(VAX_OBJS)
                           $ DELETE/NOLOG/NOCONFIRM $(VAX_DIR)*.OBJ;*

#
# Individual Simulator Builds.
#
ALTAIR : $(SIMH_LIB) $(ALTAIR_LIB)
         $!
         $! Building The $(BIN_DIR)ALTAIR-$(ARCH).EXE Simulator.
         $!
         $ $(CC)$(ALTAIR_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
         $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ALTAIR-$(ARCH).EXE -
                SCP.OBJ,$(ALTAIR_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
         $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

ALTAIRZ80 : $(SIMH_LIB) $(ALTAIRZ80_LIB)
            $!
            $! Building The $(BIN_DIR)ALTAIRZ80-$(ARCH).EXE Simulator.
            $!
            $ $(CC)$(ALTAIRZ80_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
            $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ALTAIRZ80-$(ARCH).EXE -
                   SCP.OBJ,$(ALTAIRZ80_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
            $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

ECLIPSE : $(SIMH_LIB) $(ECLIPSE_LIB)
          $!
          $! Building The $(BIN_DIR)ECLPISE-$(ARCH).EXE Simulator.
          $!
          $ $(CC)$(ECLIPSE_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
          $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ECLIPSE-$(ARCH).EXE -
                 SCP.OBJ,$(ECLIPSE_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
          $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

GRI : $(SIMH_LIB) $(GRI_LIB)
      $!
      $! Building The $(BIN_DIR)GRI-$(ARCH).EXE Simulator.
      $!
      $ $(CC)$(GRI_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
      $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)GRI-$(ARCH).EXE -
             SCP.OBJ,$(GRI_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
      $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

H316 : $(SIMH_LIB) $(H316_LIB)
       $!
       $! Building The $(BIN_DIR)H316-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(H316_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)H316-$(ARCH).EXE -
              SCP.OBJ,$(H316_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

HP2100 : $(SIMH_LIB) $(HP2100_LIB)
         $!
         $! Building The $(BIN_DIR)HP2100-$(ARCH).EXE Simulator.
         $!
         $ $(CC)$(HP2100_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
         $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)HP2100-$(ARCH).EXE -
                SCP.OBJ,$(HP2100_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
         $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

I1401 : $(SIMH_LIB) $(I1401_LIB)
        $!
        $! Building The $(BIN_DIR)I1401-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(I1401_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)I1401-$(ARCH).EXE -
               SCP.OBJ,$(I1401_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

I1620 : $(SIMH_LIB) $(I1620_LIB)
        $!
        $! Building The $(BIN_DIR)I1620-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(I1620_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)I1620-$(ARCH).EXE -
               SCP.OBJ,$(I1620_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

IBM1130 : $(SIMH_LIB) $(IBM1130_LIB)
          $!
          $! Building The $(BIN_DIR)IBM1130-$(ARCH).EXE Simulator.
          $!
          $ $(CC)$(IBM1130_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
          $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)IBM1130-$(ARCH).EXE -
                 SCP.OBJ,$(IBM1130_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
          $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

ID16 : $(SIMH_LIB) $(ID16_LIB)
       $!
       $! Building The $(BIN_DIR)ID16-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(ID16_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ID16-$(ARCH).EXE -
              SCP.OBJ,$(ID16_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

ID32 : $(SIMH_LIB) $(ID32_LIB)
       $!
       $! Building The $(BIN_DIR)ID32-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(ID32_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)ID32-$(ARCH).EXE -
              SCP.OBJ,$(ID32_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

NOVA : $(SIMH_LIB) $(NOVA_LIB)
       $!
       $! Building The $(BIN_DIR)NOVA-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(NOVA_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)NOVA-$(ARCH).EXE -
              SCP.OBJ,$(NOVA_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP1 : $(SIMH_LIB) $(PDP1_LIB)
       $!
       $! Building The $(BIN_DIR)PDP1-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(PDP1_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP1-$(ARCH).EXE -
              SCP.OBJ,$(PDP1_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP4 : $(SIMH_LIB) $(PDP4_LIB)
       $!
       $! Building The $(BIN_DIR)PDP4-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(PDP4_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP4-$(ARCH).EXE -
              SCP.OBJ,$(PDP4_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP7 : $(SIMH_LIB) $(PDP7_LIB)
       $!
       $! Building The $(BIN_DIR)PDP7-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(PDP7_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP7-$(ARCH).EXE -
              SCP.OBJ,$(PDP7_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP8 : $(SIMH_LIB) $(PDP8_LIB)
       $!
       $! Building The $(BIN_DIR)PDP8-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(PDP8_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP8-$(ARCH).EXE -
              SCP.OBJ,$(PDP8_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP9 : $(SIMH_LIB) $(PDP9_LIB)
       $!
       $! Building The $(BIN_DIR)PDP9-$(ARCH).EXE Simulator.
       $!
       $ $(CC)$(PDP9_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
       $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP9-$(ARCH).EXE -
              SCP.OBJ,$(PDP9_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
       $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

#
# If On Alpha, Build The PDP-10 Simulator.
#
.IFDEF __ALPHA__
PDP10 : $(SIMH_LIB) $(PDP10_LIB)
        $!
        $! Building The $(BIN_DIR)PDP10-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP10_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP10-$(ARCH).EXE -
               SCP.OBJ,$(PDP10_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*
.ELSE
#
# Else We Are On VAX And Tell The User We Can't Build On VAX
# Due To The Use Of INT64.
#
PDP10 : 
        $!
        $! Sorry, Can't Build $(BIN_DIR)PDP10-$(ARCH).EXE Simulator
        $! Because It Requires The Use Of INT64.
        $!
.ENDIF

PDP11 : $(SIMH_LIB) $(PDP11_LIB)
        $!
        $! Building The $(BIN_DIR)PDP11-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP11_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP11-$(ARCH).EXE -
               SCP.OBJ,$(PDP11_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

PDP15 : $(SIMH_LIB) $(PDP15_LIB)
        $!
        $! Building The $(BIN_DIR)PDP15-$(ARCH).EXE Simulator.
        $!
        $ $(CC)$(PDP15_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
        $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)PDP15-$(ARCH).EXE -
               SCP.OBJ,$(PDP15_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
        $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

S3 : $(SIMH_LIB) $(S3_LIB)
     $!
     $! Building The $(BIN_DIR)S3-$(ARCH).EXE Simulator.
     $!
     $ $(CC)$(S3_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
     $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)S3-$(ARCH).EXE -
            SCP.OBJ,$(S3_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
     $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

SDS : $(SIMH_LIB) $(SDS_LIB)
      $!
      $! Building The $(BIN_DIR)SDS-$(ARCH).EXE Simulator.
      $!
      $ $(CC)$(SDS_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
      $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)SDS-$(ARCH).EXE -
             SCP.OBJ,$(SDS_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
      $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*

VAX : $(SIMH_LIB) $(VAX_LIB)
      $!
      $! Building The $(BIN_DIR)VAX-$(ARCH).EXE Simulator.
      $!
      $ $(CC)$(VAX_OPTIONS)/OBJECT=$(SIMH_DIR) SCP.C
      $ LINK $(LINK_DEBUG)/EXE=$(BIN_DIR)VAX-$(ARCH).EXE -
             SCP.OBJ,$(VAX_LIB)/LIBRARY,$(SIMH_LIB)/LIBRARY
      $ DELETE/NOLOG/NOCONFIRM $(SIMH_DIR)*.OBJ;*
