# (This makefile is for operating systems other than Windows,
# or compilers other than Microsoft's. For MS builds, use the
# .mak files found in this directory and the utils directory).
#
# If you are building the emulator and utilities as part of
# the SIMH package, please:
#
#	Be sure that you there are NO copies of scp.c, scp_tty.c,
#	sim_sock.c, sim_tmxr.c, sim_rev.h, sim_defs.h, sim_sock.h and
#	sim_tmxr.h in the ibm1130 subdirectory. Delete them if there
#	are.
#
#	Do not use this makefile with "make all" or "make ibm1130".
#	Use the SIMH build files instead.
#
#	If and when you download updates for this simulator from
#	www.ibm1130.org, get ibm1130code.zip and ibm1130software.zip
#	separately.
#
# If you have downloaded the emulator independently of SIMH (e.g, from
# www.ibm1130.org), please:
#
#	Be sure that you DO have copies of scp.c, scp_tty.c, sim_sock.c,
#	sim_tmxr.c, sim_rev.h, sim_defs.h, sim_sock.h and sim_tmxr.h
#	in this folder.
#
#	Use this file to make the emulator.
#
#	If and when you download updates for this simulator from
#	www.ibm1130.org, get ibm1130.zip. When you expand it,
#	also expand ibm1130sofware.zip, which is inside.
#
# In either case, if you want to build DMS or work with assembly
# language programs outside of DMS, you'll want to make the utilities
# by cd'ing to the utils directory and running make there.

# CC Command
#
# Note: -O2 is sometimes broken in GCC when setjump/longjump is being
# used. Try -O2 only with released simulators.
#
CC = gcc -O0 -lm -I .
#CC = gcc -O2 -g -lm -I .


#
# Common Libraries
#
BIN = 
SIM = scp.c sim_console.c sim_fio.c sim_sock.c sim_timer.c sim_tmxr.c scp_tty.c 
SIM_INC = scp.h sim_console.h sim_defs.h sim_fio.h sim_rev.h sim_sock.h sim_timer.h sim_tmxr.h

#
# Emulator source files and compile time options
#

ibm1130D = ./
ibm1130  = ${ibm1130D}ibm1130_sys.c	${ibm1130D}ibm1130_cpu.c \
	   ${ibm1130D}ibm1130_cr.c	${ibm1130D}ibm1130_disk.c \
	   ${ibm1130D}ibm1130_stddev.c	${ibm1130D}ibm1130_gdu.c \
	   ${ibm1130D}ibm1130_gui.c	${ibm1130D}ibm1130_prt.c \
	   ${ibm1130D}ibm1130_ptrp.c    ${ibm1130D}ibm1130_fmt.c

ibm1130_INC = ibm1130res.h ibm1130_conin.h ibm1130_conout.h \
	      ibm1130_defs.h ibm1130_prtwheel.h ibm1130_fmt.h \
	      dmsr2v12phases.h dmsr2v12slet.h

#
# Build the emulator
#

${BIN}ibm1130 : ${ibm1130} ${SIM} ${ibm1130_INC} ${SIM_INC}
	${CC} ${ibm1130} ${SIM} -o $@

