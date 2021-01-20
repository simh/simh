# Simple NMAKE command file for MSVC++
#
# Targets:
#
#   hp2100 - make the HP 2100 simulator
#   hp3000 - make the HP 3000 simulator
#   clean  - remove all simulator object and executable files
#
# This file is placed in the SCP subdirectory under the simulator root
# directory.  It places the executable in the parent directory (i.e., the
# simulator root) and object files in the Release subdirectory of the root.


# This makefile is ONLY for MSVC / NMAKE.  The following command will cause an
# error if the file is accidentally invoked with GNU make.

!MESSAGE


SCPD = SCP
OBJDIR = Release

CC = cl /nologo /Ox /ISCP

CC_OUTSPEC = /Fo$(OBJDIR)\ /Fe$@
LDFLAGS = advapi32.lib wsock32.lib winmm.lib

SIM = $(SCPD)\scp.c $(SCPD)\sim_console.c $(SCPD)\sim_fio.c $(SCPD)\sim_timer.c \
	$(SCPD)\sim_sock.c $(SCPD)\sim_tmxr.c $(SCPD)\sim_tape.c $(SCPD)\sim_shmem.c \
	$(SCPD)\sim_extension.c $(SCPD)\sim_serial.c

#
# Emulator source files and compile time options
#
HP2100D = $(SCPD)\HP2100
HP2100 = $(HP2100D)/hp2100_baci.c $(HP2100D)/hp2100_cpu.c $(HP2100D)/hp2100_cpu_fp.c \
	$(HP2100D)/hp2100_cpu_fpp.c $(HP2100D)/hp2100_cpu0.c $(HP2100D)/hp2100_cpu1.c \
	$(HP2100D)/hp2100_cpu2.c $(HP2100D)/hp2100_cpu3.c $(HP2100D)/hp2100_cpu4.c \
	$(HP2100D)/hp2100_cpu5.c $(HP2100D)/hp2100_cpu6.c $(HP2100D)/hp2100_cpu7.c \
	$(HP2100D)/hp2100_di.c $(HP2100D)/hp2100_di_da.c $(HP2100D)/hp2100_disclib.c \
	$(HP2100D)/hp2100_dma.c $(HP2100D)/hp2100_dp.c $(HP2100D)/hp2100_dq.c \
	$(HP2100D)/hp2100_dr.c $(HP2100D)/hp2100_ds.c $(HP2100D)/hp2100_ipl.c \
	$(HP2100D)/hp2100_lps.c $(HP2100D)/hp2100_lpt.c $(HP2100D)/hp2100_mc.c \
	$(HP2100D)/hp2100_mem.c $(HP2100D)/hp2100_mpx.c $(HP2100D)/hp2100_ms.c \
	$(HP2100D)/hp2100_mt.c $(HP2100D)/hp2100_mux.c $(HP2100D)/hp2100_pif.c \
	$(HP2100D)/hp2100_pt.c $(HP2100D)/hp2100_sys.c $(HP2100D)/hp2100_tbg.c \
	$(HP2100D)/hp2100_tty.c
HP2100_OPT = /D USE_VM_INIT /DHAVE_INT64 /I$(HP2100D)

HP3000D = $(SCPD)\HP3000
HP3000 = $(HP3000D)/hp_disclib.c $(HP3000D)/hp_tapelib.c $(HP3000D)/hp3000_atc.c \
	$(HP3000D)/hp3000_clk.c $(HP3000D)/hp3000_cpu.c $(HP3000D)/hp3000_cpu_base.c \
	$(HP3000D)/hp3000_cpu_fp.c $(HP3000D)/hp3000_cpu_eis.c \
	$(HP3000D)/hp3000_cpu_cis.c $(HP3000D)/hp3000_ds.c \
	$(HP3000D)/hp3000_iop.c $(HP3000D)/hp3000_lp.c $(HP3000D)/hp3000_mem.c \
	$(HP3000D)/hp3000_mpx.c $(HP3000D)/hp3000_ms.c $(HP3000D)/hp3000_scmb.c \
	$(HP3000D)/hp3000_sel.c $(HP3000D)/hp3000_sys.c
HP3000_OPT = /D USE_VM_INIT /I$(HP3000D)


clean :
	if exist $(OBJDIR)\nul rmdir /s /q $(OBJDIR)
	if exist hp2100.exe del hp2100.exe
	if exist hp3000.exe del hp3000.exe


hp3000 : makedir hp3000.exe

makedir :
	if not exist $(OBJDIR)\nul mkdir $(OBJDIR)

hp3000.exe : $(HP3000) $(SIM)
	$(CC) $(HP3000) $(SIM) $(HP3000_OPT) $(CC_OUTSPEC) $(LDFLAGS)


hp2100 : makedir hp2100.exe

hp2100.exe : $(HP2100) $(SIM)
	$(CC) $(HP2100) $(SIM) $(HP2100_OPT) $(CC_OUTSPEC) $(LDFLAGS)
