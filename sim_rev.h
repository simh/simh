/* sim_rev.h: simulator revisions and current rev level

   Copyright (c) 1993-2002, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#define	SIM_MAJOR	2
#define SIM_MINOR	10
#define SIM_PATCH	0

/* V2.10 revision history 

patch	date		module(s) and fix(es)

  1	21-Nov-02	pdp1_stddev.c: changed typewriter to half duplex
			(found by Derek Peschel)

			pdp10_tu.c:
			-- fixed bug in bootstrap (reported by Michael Thompson)
			-- fixed bug in read (reported by Harris Newman)

  0	15-Nov-02	SCP and libraries
			scp.c:
			-- added Telnet console support
			-- removed VT emulation support
			-- added support for statically buffered devices
			-- added HELP <command>
			-- fixed bugs in set_logon, ssh_break (found by David Hittner)
			-- added VMS file optimization (from Robert Alan Byer)
			-- added quiet mode, DO with parameters, GUI interface,
			   extensible commands (from Brian Knittel)
			-- added DEVICE context and flags
			-- added central device enable/disable support
			-- modified SAVE/GET to save and restore flags
			-- modified boot routine calling sequence
			scp_tty.c:
			-- removed VT emulation support
			-- added sim_os_sleep, renamed sim_poll_kbd, sim_putchar
			sim_tmxr.c:
			-- modified for Telnet console support
			-- fixed bug in binary (8b) support
			sim_sock.c: modified for Telnet console support
			sim_ether.c: new library for Ethernet (from David Hittner)

			all magtapes:
			-- added support for end of medium
			-- cleaned up BOT handling

			all DECtapes: added support for RT11 image file format

			most terminals and multiplexors:
			-- added support for 7b vs 8b character processing

			PDP-1
			pdp1_cpu.c, pdp1_sys.c, pdp1_dt.c: added PDP-1 DECtape support

			PDP-8
			pdp8_cpu.c, all peripherals:
			-- added variable device number support
			-- added new device enabled/disable support
			pdp8_rx.c: added RX28/RX02 support

			PDP-11
			pdp11_defs.h, pdp11_io.c, pdp11_sys.c, all peripherals:
			-- added variable vector support
			-- added new device enable/disable support
			-- added autoconfiguration support
			all bootstraps: modified to support variable addresses
			dec_mscp.h, pdp11_tq.c: added TK50 support
			pdp11_rq.c:
			-- added multicontroller support
			-- fixed bug in HBE error log packet
			-- fixed bug in ATP processing
			pdp11_ry.c: added RX211/RX02 support
			pdp11_hk.c: added RK611/RK06/RK07 support
			pdp11_tq.c: added TMSCP support
			pdp11_xq.c: added DEQNA/DELQA support (from David Hittner)
			pdp11_pclk.c: added KW11P support
			pdp11_ts.c:
			-- fixed bug in CTL decoding
			-- fixed bug in extended status XS0_MOT
			pdp11_stddev.c: removed paper tape to its own module

			PDP-18b
			pdp18b_cpu.c, all peripherals:
			-- added variable device number support
			-- added new device enabled/disabled support

			VAX
			dec_dz.h: fixed bug in number of boards calculation
			vax_moddefs.h, vax_io.c, vax_sys.c, all peripherals:
			-- added variable vector support
			-- added new device enable/disable support
			-- added autoconfiguration support
			vax_sys.c:
			-- generalized examine/deposit
			-- added TMSCP, multiple RQDX3, DEQNA/DELQA support
			vax_stddev.c: removed paper tape, now uses PDP-11 version
			vax_sysdev.c:
			-- allowed NVR to be attached to file
			-- removed unused variables (found by David Hittner)

			PDP-10
			pdp10_defs.h, pdp10_ksio.c, all peripherals:
			-- added variable vector support
			-- added new device enable/disable support
			pdp10_defs.h, pdp10_ksio.c: added support for standard PDP-11
			   peripherals, added RX211 support
			pdp10_pt.c: rewritten to reference common implementation

			Nova, Eclipse:
			nova_cpu.c, eclipse_cpu.c, all peripherals:
			-- added new device enable/disable support

			HP2100
			hp2100_cpu:
			-- fixed bugs in the EAU, 21MX, DMS, and IOP instructions
			-- fixed bugs in the memory protect and DMS functions
			-- created new options to enable/disable EAU, MPR, DMS
			-- added new device enable/disable support
			hp2100_fp.c:
			-- recoded to conform to 21MX microcode algorithms
			hp2100_stddev.c:
			-- fixed bugs in TTY reset, OTA, time base generator
			-- revised BOOT support to conform to RBL loader
			-- added clock calibration
			hp2100_dp.c:
			-- changed default to 13210A
			-- added BOOT support
			hp2100_dq.c:
			-- finished incomplete functions, fixed head switching
			-- added BOOT support
			hp2100_ms.c:
			-- fixed bugs found by diagnostics
			-- added 13183 support
			-- added BOOT support
			hp2100_mt.c:
			-- fixed bugs found by diagnostics
			-- disabled by default
			hp2100_lpt.c: implemented 12845A controller
			hp2100_lps.c:
			-- renamed 12653A controller
			-- added diagnostic mode for MPR, DCPC diagnostics
			-- disabled by default

			IBM 1620: first release

/* V2.9 revision history

  11	20-Jul-02	i1401_mt.c: on read, end of record stores group mark
			   without word mark (found by Van Snyder)

			i1401_dp.c: reworked address generation and checking

			vax_cpu.c: added infinite loop detection and halt to
			   boot ROM option (from Mark Pizzolato)

			vax_fpa.c: changed function names to prevent conflict
			   with C math library

			pdp11_cpu.c: fixed bug in MMR0 update logic (from
			   John Dundas)

			pdp18b_stddev.c: added "ASCII mode" for reader and
			   punch (from Hans Pufal)

			gri_*.c: added GRI-909 simulator

			scp.c: added DO echo, DO exit (from Brian Knittel)

			scp_tty.c: added Windows priority hacking (from
			   Mark Pizzolato)

  10	15-Jun-02	scp.c: fixed error checking on calls to fxread/fxwrite
			   (found by Norm Lastovic)

			scp_tty.c, sim_vt.h, sim_vt.c: added VTxxx emulation
			   support for Windows (from Fischer Franz)

			sim_sock.c: added OS/2 support (from Holger Veit)

			pdp11_cpu.c: fixed bugs (from John Dundas)
			-- added special case for PS<15:12> = 1111 to MFPI
			-- removed special case from MTPI
			-- added masking of relocation adds 

			i1401_cpu.c:
			-- added multiply/divide
			-- fixed bugs (found by Van Snyder)
			   o 5 and 7 character H, 7 character doesn't branch
			   o 8 character NOP
			   o 1401-like memory dump

			i1401_dp.c: added 1311 disk

  9	04-May-02	pdp11_rq: fixed bug in polling routine

  8	03-May-02	scp.c:
			-- changed LOG/NOLOG to SET LOG/NOLOG
			-- added SHOW LOG
			-- added SET VT/NOVT and SHOW VT for VT emulation
  
			sim_sock.h: changed VMS stropt.h include to ioctl.h

			vax_cpu.c
			-- added TODR powerup routine to set date, time on boot
			-- fixed exception flows to clear trap request
			-- fixed register logging in autoincrement indexed

			vax_stddev.c: added TODR powerup routine
			
			vax_cpu1.c: fixed exception flows to clear trap request

  7	30-Apr-02	scp.c: fixed bug in clock calibration when (real) clock
			   jumps forward due too far (found by Jonathan Engdahl)
  
			pdp11_cpu.c: fixed bugs, added features (from John Dundas
			   and Wolfgang Helbig)
			-- added HTRAP and BPOK to maintenance register
			-- added trap on kernel HALT if MAINT<HTRAP> set
			-- fixed red zone trap, clear odd address and nxm traps
			-- fixed RTS SP, don't increment restored SP
			-- fixed TSTSET, write dst | 1 rather than prev R0 | 1
			-- fixed DIV, set N=0,Z=1 on div by zero (J11, 11/70)
			-- fixed DIV, set set N=Z=0 on overfow (J11, 11/70)
			-- fixed ASH, ASHC, count = -32 used implementation-
			   dependent 32 bit right shift
			-- fixed illegal instruction test to detect 000010
			-- fixed write-only page test

			pdp11_rp.c: fixed SHOW ADDRESS command

			vaxmod_defs.h: fixed DZ vector base and number of lines

			dec_dz.h:
			-- fixed interrupt acknowledge routines
			-- fixed SHOW ADDRESS command

			all magtape routines: added test for badly formed
			   record length (suggested by Jonathan Engdahl)

  6	18-Apr-02	vax_cpu.c: fixed CASEL condition codes

			vax_cpu1.c: fixed vfield pos > 31 test to be unsigned

			vax_fpu.c: fixed EDIV overflow test for 0 quotient

  5	14-Apr-02	vax_cpu1.c:
			-- fixed interrupt, prv_mode set to 0 (found by Tim Stark)
			-- fixed PROBEx to mask mode to 2b (found by Kevin Handy)

  4	1-Apr-02	pdp11_rq.c: fixed bug, reset cleared write protect status

			pdp11_ts.c: fixed bug in residual frame count after space

  3	15-Mar-02	pdp11_defs.h: changed default model to KDJ11A (11/73)

			pdp11_rq.c: adjusted delays for M+ timing bugs

			hp2100_cpu.c, pdp10_cpu.c, pdp11_cpu.c: tweaked abort
			   code for ANSI setjmp/longjmp compliance

			hp2100_cpu.c, hp2100_fp.c, hp2100_stddev.c, hp2100_sys.c:
			   revised to allocate memory dynamically

  2	01-Mar-02	pdp11_cpu.c:
			-- fixed bugs in CPU registers
			-- fixed double operand evaluation order for M+

			pdp11_rq.c: added delays to initialization for
			   RSX11M+ prior to V4.5

  1	20-Feb-02	scp.c: fixed bug in clock calibration when (real)
			time runs backwards

			pdp11_rq.c: fixed bug in host timeout logic

			pdp11_ts.c: fixed bug in message header logic

			pdp18b_defs.h, pdp18b_dt.c, pdp18b_sys.c: added
			   PDP-7 DECtape support

			hp2100_cpu.c:
			-- added floating point and DMS
			-- fixed bugs in DIV, ASL, ASR, LBT, SBT, CBT, CMW

			hp2100_sys.c: added floating point, DMS

			hp2100_fp.c: added floating point

			ibm1130: added Brian Knittel's IBM 1130 simulator

  0	30-Jan-02	scp.c:
			-- generalized timer package for multiple timers
			-- added circular register arrays
			-- fixed bugs, line spacing in modifier display
			-- added -e switch to attach
			-- moved device enable/disable to simulators

			scp_tty.c: VAX specific fix (from Robert Alan Byer)

			sim_tmxr.c, sim_tmxr.h:
			-- added tmxr_fstats, tmxr_dscln
			-- renamed tmxr_fstatus to tmxr_fconns

			sim_sock.c, sim_sock.h: added VMS support (from
			Robert Alan Byer)

			pdp_dz.h, pdp18b_tt1.c, nova_tt1.c:
			-- added SET DISCONNECT
			-- added SHOW STATISTICS

			pdp8_defs.h: fixed bug in interrupt enable initialization

			pdp8_ttx.c: rewrote as unified multiplexor

			pdp11_cpu.c: fixed calc_MMR1 macro (found by Robert Alan Byer)

			pdp11_stddev.c: fixed bugs in KW11L (found by John Dundas)

			pdp11_rp.c: fixed bug in 18b mode boot

			pdp11 bootable I/O devices: fixed register setup at boot
			   exit (found by Doug Carman)

			hp2100_cpu.c:
			-- fixed DMA register tables (found by Bill McDermith)
			-- fixed SZx,SLx,RSS bug (found by Bill McDermith)
			-- fixed flop restore logic (found by Bill McDermith)

			hp2100_mt.c: fixed bug on write of last character

			hp2100_dq,dr,ms,mux.c: added new disk, magtape, and terminal
			   multiplexor controllers

			i1401_cd.c, i1401_mt.c: new zero footprint bootstraps
			   (from Van Snyder)

			i1401_sys.c: fixed symbolic display of H, NOP with no trailing
			   word mark (found by Van Snyder)

			most CPUs:
			-- replaced OLDPC with PC queue
			-- implemented device enable/disable locally

   V2.8 revision history

5	25-Dec-01	scp.c: fixed bug in DO command (found by John Dundas)

			pdp10_cpu.c:
			-- moved trap-in-progress to separate variable
			-- cleaned up declarations
			-- cleaned up volatile state for GNU C longjmp

			pdp11_cpu.c: cleaned up declarations
  
			pdp11_rq.c: added RA-class disks

4	17-Dec-01	pdp11_rq.c: added delayed processing of packets

3	16-Dec-01	pdp8_cpu.c:
			-- mode A EAE instructions didn't clear GTF
			-- ASR shift count > 24 mis-set GTF
			-- effective shift count == 32 didn't work

2	07-Dec-01	scp.c: added breakpoint package

			all CPU's: revised to use new breakpoint package

1	05-Dec-01	scp.c: fixed bug in universal register name logic

0	30-Nov-01	Reorganized simh source and documentation tree

			scp: Added DO command, universal registers, extended
			   SET/SHOW logic

			pdp11: overhauled PDP-11 for DMA map support, shared
			   sources with VAX, dynamic buffer allocation

			18b pdp: overhauled interrupt structure

			pdp8: added RL8A

			pdp10: fixed two ITS-related bugs (found by Dave Conroy)

   V2.7 revision history

patch	date		module(s) and fix(es)

15	23-Oct-01	pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: fixed bugs
			   error interrupt handling

			pdp10_defs.h, pdp10_ksio.c, pdp10_fe.c, pdp10_fe.c,
			pdp10_rp.c, pdp10_tu.c: reworked I/O page interface
			   to use symbolic base addresses and lengths

14	20-Oct-01	dec_dz.h, sim_tmxr_h, sim_tmxr.c: fixed bug in Telnet
			   state handling (found by Thord Nilson), removed
			   tmxr_getchar, added tmxr_rqln and tmxr_tqln

13	18-Oct-01	pdp11_tm.c: added stub diagnostic register clock
			   for RSTS/E (found by Thord Nilson)

12	15-Oct-01	pdp11_defs.h, pdp11_cpu.c, pdp11_tc.c, pdp11_ts.c,
			   pdp11_rp.c: added operations logging

11	8-Oct-01	scp.c: added sim_rev.h include and version print

			pdp11_cpu.c: fixed bug in interrupt acknowledge,
			   multiple outstanding interrupts caused the lowest
			   rather than the highest to be acknowledged

10	7-Oct-01	pdp11_stddev.c: added monitor bits (CSR<7>) for full
			   KW11L compatibility, needed for RSTS/E autoconfiguration

9	6-Oct-01	pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: rewrote interrupt
			   logic from RH11/RH70 schematics, to mimic hardware quirks

			dec_dz.c: fixed bug in carrier detect logic, carrier
			   detect was being cleared on next modem poll

8	4-Oct-01	pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: undid edit of
			   28-Sep-01; real problem was level-sensitive nature of
	   		   CS1_SC, but CS1_SC can only trigger an interrupt if
	   		   DONE is set

7	2-Oct-01	pdp11_rp.c, pdp10_rp.c: CS1_SC is evaluated as a level-
			   sensitive, rather than an edge-sensitive, input to
			   interrupt request

6	30-Sep-01	pdp11_rp.c, pdp10_rp.c: separated out CS1<5:0> to per-
			   drive registers

			pdp10_tu.c: based on above, cleaned up handling of
			   non-existent formatters, fixed non-data transfer
			   commands clearing DONE

5	28-Sep-01	pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: controller should
			   interrupt if ATA or SC sets when IE is set, was
			   interrupting only if DON = 1 as well

4	27-Sep-01	pdp11_ts.c:
			-- NXM errors should return TC4 or TC5; were returning TC3
			-- extended features is part of XS2; was returned in XS3
			-- extended characteristics (fifth) word needed for RSTS/E

			pdp11_tc.c: stop, stop all do cause an interrupt

			dec_dz.h: scanner should find a ready output line, even
			   if there are no connections; needed for RSTS/E autoconfigure

			scp.c:
			-- added routine sim_qcount for 1130
			-- added "simulator exit" detach routine for 1130

			sim_defs.h: added header for sim_qcount

3	20-Sep-01	pdp11_ts.c: boot code binary was incorrect

2	19-Sep-01	pdp18b_cpu.c: EAE should interpret initial count of 00
			   as 100

			scp.c: modified Macintosh support

1	17-Sep-01	pdp8_ttx.c: new module for PDP-8 multi-terminal support

			pdp18b_tt1.c: modified to use sim_tmxr library

			nova_tt1.c: modified to use sim_tmxr library

			dec_dz.h: added autodisconnect support

			scp.c: removed old multiconsole support

			sim_tmxr.c: modified calling sequence for sim_putchar_ln

			sim_sock.c: added Macintosh sockets support
*/
