/* sim_rev.h: simulator revisions and current rev level

   Copyright (c) 1993-2012, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#ifndef SIM_REV_H_
#define SIM_REV_H_     0

#ifndef SIM_MAJOR
#define SIM_MAJOR       4
#endif
#ifndef SIM_MINOR
#define SIM_MINOR       0
#endif
#ifndef SIM_PATCH
#define SIM_PATCH       0
#endif
#ifndef SIM_DELTA
#define SIM_DELTA       0
#endif

#ifndef SIM_VERSION_MODE
#define SIM_VERSION_MODE "Current"
#endif

#if defined(SIM_NEED_GIT_COMMIT_ID)
#include ".git-commit-id.h"
#endif

/*
  Simh's git commit id would be undefined when working with an 
  extracted archive (zip file or tar ball).  To address this 
  problem and record the commit id that the archive was created 
  from, the archive creation process populates the below 
  information as a consequence of the "sim_rev.h export-subst" 
  line in the .gitattributes file.
 */
#if !defined(SIM_GIT_COMMIT_ID)
#define SIM_GIT_COMMIT_ID $Format:%H$
#define SIM_GIT_COMMIT_TIME $Format:%aI$
#endif

/*
  The comment section below reflects the manual editing process which was in place
  prior to the use of the git source control system on at https://gihub.com/simh/simh

  Details about all future fixes will be visible in the source control system's 
  history.

*/

/*
   V3.9 revision history

patch   date            module(s) and fix(es)

  0     01-May-2012     scp.c:
                        - added *nix READLINE support (Mark Pizzolato)
                        - fixed handling of DO with no arguments (Dave Bryan)
                        - fixed "SHOW DEVICE" with only one enabled unit (Dave Bryan)
                        - clarified some help messages (Mark Pizzolato)
                        - added "SHOW SHOW" and "SHOW <dev> SHOW" commands (Mark Pizzolato)
                        - fixed bug in deposit stride for numeric input (John Dundas)

                        sim_console.c
                        - added support for BREAK key on Windows (Mark Pizzolato)

                        sim_ether.c
                        - major revision (Dave Hittner and Mark Pizzolato)
                        - fixed array overrun which caused SEGFAULT on hosts with many
                          devices which libpcap can access.
                        - fixed duplicate MAC address detection to work reliably on switch
                          connected LANs

                        sim_tmxr.c:
                        - made telnet option negotiation more reliable, VAX simulator now 
                          works with PuTTY as console (Mark Pizzolato)

                        h316_cpu.c:
                        - fixed bugs in MPY, DIV introduced in 3.8-1 (from Theo Engel)
                        - fixed bugs in double precision, normalization, SC (from Adrian Wise)
                        - fixed XR behavior (from Adrian Wise)

                        hp2100 all peripherals (Dave Bryan):
                        - Changed I/O signal handlers for newly revised signal model
                        - Deprecated DEVNO modifier in favor of SC

                        hp2100_cpu.c (Dave Bryan):
                        - Minor speedup in "is_mapped"
                        - Added casts to cpu_mod, dmasio, dmapio, cpu_reset, dma_reset
                        - Fixed I/O return status bug for DMA cycles
                        - Failed I/O cycles now stop on failing instruction
                        - Revised DMA for new multi-card paradigm
                        - Consolidated DMA reset routines
                        - DMA channels renamed from 0,1 to 1,2 to match documentation
                        - Changed I/O instructions, handlers, and DMA for revised signal model
                        - Changed I/O dispatch table to use DIB pointers
                        - Removed DMA latency counter
                        - Fixed DMA requests to enable stealing every cycle
                        - Fixed DMA priority for channel 1 over channel 2
                        - Corrected comments for "cpu_set_idle"

                        hp2100_cpu.h:
                        - Changed declarations for VMS compiler

                        hp2100_cpu0.c (Dave Bryan):
                        - Removed DS note regarding PIF card (is now implemented)

                        hp2100_cpu4.c (Dave Bryan):
                        - Added OPSIZE casts to fp_accum calls in .FPWR/.TPWR

                        hp2100_cpu5.c (Dave Bryan):
                        - Added sign extension for dim count in "cpu_ema_resolve"
                        - Eliminated unused variable in "cpu_ema_vset"

                        hp2100_cpu6.c (Dave Bryan):
                        - DMA channels renamed from 0,1 to 1,2 to match documentation

                        hp2100_cpu7.c (Dave Bryan):
                        - Corrected "opsize" parameter type in vis_abs

                        hp2100_defs.h (Dave Bryan):
                        - Added hp_setsc, hp_showsc functions to support SC modifier
                        - DMA channels renamed from 0,1 to 1,2 to match documentation
                        - Revised I/O signal enum values for concurrent signals
                        - Revised I/O macros for new signal handling
                        - Added DA and DC device select code assignments

                        hp2100_di.c, hp2100_di.h (Dave Bryan):
                        - Implemented 12821A HP-IB Disc Interface

                        hp2100_di_da.c (Dave Bryan):
                        - Implemented 7906H/20H/25H ICD disc drives

                        hp2100_dp.c (Dave Bryan):
                        - Added CNTLR_TYPE cast to dp_settype
 
                        hp2100_ds.c (Dave Bryan):
                        - Rewritten to use the MAC/ICD disc controller library
                        - ioIOO now notifies controller service of parameter output
                        - Corrected SRQ generation and FIFO under/overrun detection
                        - Corrected Clear command to conform to the hardware
                        - Fixed Request Status to return Unit Unavailable if illegal
                        - Seek and Cold Load Read now Seek Check if seek in progress
                        - Remodeled command wait for seek completion
                        - Corrected status returns for disabled drive, auto-seek
                          beyond drive limits, Request Sector Address and Wakeup
                          with invalid or offline unit
                        - Address verification reenabled if auto-seek during
                          Read Without Verify

                        hp2100_fp1.c (Dave Bryan):
                        - Added missing precision on constant "one" in fp_trun
                        - Completed the comments for divide; no code changes

                        hp2100_ipl.c (Dave Bryan):
                        - Added CARD_INDEX casts to dib.card_index
                        - A failed STC may now be retried
                        - Consolidated reporting of consecutive CRS signals
                        - Revised for new multi-card paradigm

                        hp2100_lps.c (Dave Bryan):
                        - Revised detection of CLC at last DMA cycle
                        - Corrected 12566B (DIAG mode) jumper settings

                        hp2100_ms.c (Dave Bryan):
                        - Added CNTLR_TYPE cast to ms_settype
 
                        hp2100_mt.c (Dave Bryan):
                        - Removed redundant MTAB_VUN from "format" MTAB entry
                        - Fixed command scanning error in mtcio ioIOO handler

                        hp2100_stddev.c (Dave Bryan):
                        - Add TBG as a logical name for the CLK device
 
                        hp2100_sys.c (Dave Bryan):
                        - Deprecated DEVNO in favor of SC
                        - Added hp_setsc, hp_showsc functions to support SC modifier
                        - Added DA and dummy DC devices
                        - DMA channels renamed from 0,1 to 1,2 to match documentation
                        - Changed DIB access for revised signal model
 
                        hp_disclib.c, hp_disclib.h (Dave Bryan)
                        - Created MAC/ICD disc controller library

                        i1401_cd.c:
                        - fixed read stacker operation in column binary mode
                        - fixed punch stacker operation (Van Snyder)

                        id_pas.c:
                        - fixed TT_GET_MODE test to use TTUF_MODE_x (Michael Bloom)
                        - revised to use clock coscheduling

                        id_tt.c, id_ttc.p:
                        - revised to use clock coscheduling

                        id_uvc.c:
                        - added clock coscheduling routine

                        1401_cpu.c:
                        - reverted multiple tape indicator implementation
                        - fixed EOT indicator test not to clear indicator (Van Snyder)
                        - fixed divide not to clear word marks in quotient (Van Snyder)
                        - revised divide algorithm (Van Snyder)

                        i1401_mt.c:
                        - reverted multiple tape indicator implementation
                        - fixed END indicator test not to clear indicator (Van Snyder)
                        - fixed backspace over tapemark not to set EOR (Van Snyder)
                        - added no rewind option (Van Snyder)

                        i1401_sys.c:
                        - fixed misuse of & instead of && in decode (Peter Schorn)

                        pdp1_cpu.c:
                        - fixed misuse of & instead of && in Ea_ch (Michael Bloom)

                        pdp1_stddev.c:
                        - fixed unitialized variable in tty output service (Michael Bloom)

                        pdp10_fe.c:
                        - revised to use clock coscheduling

                        pdp11_defs.h:
                        - fixed priority of PIRQ vs IO; added INT_INTERNALn

                        pdp11_io.c:
                        - fixed Qbus interrupts to treat all IO devices (except clock) as BR4
                        - fixed order of int_internal (Jordi Guillaumes i Pons)

                        ppd11_rf.c
                        - fixed bug in updating mem addr extension (Peter Schorn)

                        pdp11_rk.c:
                        - fixed bug in read header (Walter F Mueller)

                        pdp11_rl.c:
                        - added debug support

                        pdp11_rq.c:
                        - added RD32 support

                        pdp11_tq.c: (Mark Pizzolato)
                        - set UNIT_SXC flag when a tape mark is encountered 
                          during forward motion read operations
                        - fixed logic which clears UNIT_SXC to check command modifier
                        - added CMF_WR flag to tq_cmf entry for OP_WTM
                        - made non-immediate rewind positioning operations take 2 seconds
                        - added UNIT_IDLE flag to tq units.
                        - fixed debug output of tape file positions when they are 64b
                        - added more debug output after positioning operations
                        - added textual display of the command being performed
                        - fixed comments about register addresses
                        
                        pdp11_ts.c:
                        - fixed t_addr printouts for 64b big-endian systems (Mark Pizzolato)

                        pdp11_tu.c:
                        - fixed t_addr printouts for 64b big-endian systems (Mark Pizzolato)

                        pdp11_vh.c: (Mark Pizzolato)
                        - fixed SET VH LINES=n to correctly adjust the number
                          of lines available to be 8, 16, 24, or 32.
                        - fixed performance issue avoiding redundant polling

                        pdp11_xq.c: (Mark Pizzolato)
                        - Fixed missing information from save/restore which
                          caused operations to not complete correctly after 
                          a restore until the OS reset the controller.
                        - Added address conflict check during attach.
                        - Fixed loopback processing to correctly handle forward packets.
                        - Fixed interrupt dispatch issue which caused delivered packets 
                          (in and out) to sometimes not interrupt the CPU after processing.
                        - Fixed the SCP visibile SA registers to always display the 
                          ROM mac address, even after it is changed by SET XQ MAC=.
                        - Added changes so that the Console DELQA diagnostic (>>>TEST 82) 
                          will succeed.
                        - Added DELQA-T (aka DELQA Plus) device emulation support.
                        - Added dropped frame statistics to record when the receiver discards
                          received packets due to the receiver being disabled, or due to the
                          XQ device's packet receive queue being full.
                        - Fixed bug in receive processing when we're not polling.  This could
                          cause receive processing to never be activated again if we don't 
                          read all available packets via eth_read each time we get the 
                          opportunity.
                        - Added the ability to Coalesce received packet interrupts.  This
                          is enabled by SET XQ POLL=DELAY=nnn where nnn is a number of 
                          microseconds to delay the triggering of an interrupt when a packet
                          is received.
                        - Added SET XQ POLL=DISABLE (aka SET XQ POLL=0) to operate without 
                          polling for packet read completion.
                        - Changed the sanity and id timer mechanisms to use a separate timer
                          unit so that transmit and recieve activities can be dealt with
                          by the normal xq_svc routine.
                          Dynamically determine the timer polling rate based on the 
                          calibrated tmr_poll and clk_tps values of the simulator.
                        - Enabled the SET XQ POLL to be meaningful if the simulator currently
                          doesn't support idling.
                        - Changed xq_debug_setup to use sim_debug instead of printf so that
                          all debug output goes to the same place.
                        - Restored the call to xq_svc after all successful calls to eth_write
                          to allow receive processing to happen before the next event
                          service time.  This must have been inadvertently commented out 
                          while other things were being tested.

                        pdp11_xu.c: (Mark Pizzolato)
                        - Added SHOW XU FILTERS modifier (Dave Hittner)
                        - Corrected SELFTEST command, enabling use by VMS 3.7, VMS 4.7, and Ultrix 1.1 (Dave Hittner)
                        - Added address conflict check during attach.
                        - Added loopback processing support
                        - Fixed the fact that no broadcast packets were received by the DEUNA
                        - Fixed transmitted packets to have the correct source MAC address.
                        - Fixed incorrect address filter setting calling eth_filter().

                        pdp18b_stddev.c:
                        - added clock coscheduling
                        - revised TTI to use clock coscheduling and to fix perpetual CAF bug
                        
                        pdp18b_ttx.c:
                        - revised to use clock coscheduling

                        pdp8_clk.c:
                        - added clock coscheduling

                        pdp8_fpp.c: (Rick Murphy)
                        - many bug fixes; now functional

                        pdp8_tt.c:
                        - revised to use clock coscheduling and to fix perpetual CAF bug

                        pdp8_ttx.c:
                        - revised to use clock cosheduling

                        pdp8_sys.c:
                        - added link to FPP

                        pdp8_td.c:
                        - fixed SDLC to clear AC (Dave Gesswein)

                        sds_mt.c:
                        - fixed bug in scan function decode (Peter Schorn)

                        vax_cpu.c:
                        - revised idle design (Mark Pizzolato)
                        - fixed bug in SET CPU IDLE
                        - fixed failure to clear PSL<tp> in BPT, XFC

                        vax_cpu1.c:
                        - revised idle design Mark Pizzolato)
                        - added VEC_QMODE test in interrupt handler

                        vax_fpa.c:
                        - fixed integer overflow bug in EMODx (Camiel Vanderhoeven)
                        - fixed POLYx normalizing before add mask bug (Camiel Vanderhoeven)
                        - fixed missing arguments in 32b floating add (Mark Pizzolato)

                        vax_octa.c (Camiel Vanderhoeven)
                        - fixed integer overflow bug in EMODH
                        - fixed POLYH normalizing before add mask bug

                        vax_stddev.c:
                        - revised to use clock coscheduling

                        vax_syscm.c:
                        - fixed t_addr printouts for 64b big-endian systems (Mark Pizzolato)

                        vax_sysdev.c:
                        - added power clear call to boot routine (Mark Pizzolato)

                        vax780_sbi.c:
                        - added AUTORESTART switch support (Mark Pizzolato)

                        vax780_stddev.c
                        - added REBOOT support (Mark Pizzolato)
                        - revised to use clock coscheduling

                        vaxmod_def.h
                        - moved all Qbus devices to BR4; deleted RP definitions


   V3.8 revision history

  1     08-Feb-09       scp.c:
                        - revised RESTORE unit logic for consistency
                        - "detach_all" ignores error status returns if shutting down (Dave Bryan)
                        - DO cmd missing params now default to null string (Dave Bryan)
                        - DO cmd sub_args now allows "\\" to specify literal backslash (Dave Bryan)
                        - decommitted MTAB_VAL
                        - fixed implementation of MTAB_NC
                        - fixed warnings in help printouts
                        - fixed "SHOW DEVICE" with only one enabled unit (Dave Bryan)  

                        sim_tape.c:
                        - fixed signed/unsigned warning in sim_tape_set_fmt (Dave Bryan)

                        sim_tmxr.c, sim_tmxr.h:
                        - added line connection order to tmxr_poll_conn,
                          added tmxr_set_lnorder and tmxr_show_lnorder (Dave Bryan)
                        - print device and line to which connection was made (Dave Bryan)
                        - added three new standardized SHOW routines

                        all terminal multiplexers:
                        - revised for new common SHOW routines in TMXR library
                        - rewrote set size routines not to use MTAB_VAL

                        hp2100_cpu.c (Dave Bryan):
                        - VIS and IOP are now mutually exclusive on 1000-F
                        - Removed A/B shadow register variables
                        - Moved hp_setdev, hp_showdev to hp2100_sys.c
                        - Moved non-existent memory checks to WritePW
                        - Fixed mp_dms_jmp to accept lower bound, check write protection
                        - Corrected DMS violation register set conditions
                        - Refefined ABORT to pass address, moved def to hp2100_cpu.h
                        - Combined dms and dms_io routines
                        - JSB to 0/1 with W5 out and fence = 0 erroneously causes MP abort
                        - Unified I/O slot dispatch by adding DIBs for CPU, MP, and DMA
                        - Rewrote device I/O to model backplane signals
                        - EDT no longer passes DMA channel
                        - Added SET CPU IDLE/NOIDLE, idle detection for DOS/RTE
                        - Breakpoints on interrupt trap cells now work

                        hp2100_cpu0.c (Dave Bryan):
                        - .FLUN and self-tests for VIS and SIGNAL are NOP if not present
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)
                        - Added "user microcode" dispatcher for unclaimed instructions

                        hp2100_cpu1.c (Dave Bryan):
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Moved option-present tests to UIG dispatchers
                        - Call "user microcode" dispatcher for unclaimed UIG instructions

                        hp2100_cpu2.c (Dave Bryan):
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)
                        - Updated mp_dms_jmp calling sequence
                        - Fixed DJP, SJP, and UJP jump target validation
                        - RVA/B conditionally updates dms_vr before returning value
                        
                        hp2100_cpu3.c (Dave Bryan):
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)
                        - Updated mp_dms_jmp calling sequence

                        hp2100_cpu4.c, hp2100_cpu7.c (Dave Bryan):
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)

                        hp2100_cpu5.c (Dave Bryan):
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)
                        - Redefined ABORT to pass address, moved def to hp2100_cpu.h
                        - Rewrote device I/O to model backplane signals

                        hp2100_cpu6.c (Dave Bryan):
                        - Corrected .SIP debug formatting
                        - Moved microcode function prototypes to hp2100_cpu1.h
                        - Removed option-present tests (now in UIG dispatchers)
                        - Rewrote device I/O to model backplane signals

                        hp2100 all peripherals (Dave Bryan):
                        - Rewrote device I/O to model backplane signals

                        hp2100_baci.c (Dave Bryan):
                        - Fixed STC,C losing interrupt request on BREAK
                        - Changed Telnet poll to connect immediately after reset or attach
                        - Added REG_FIT to register variables < 32-bit size
                        - Moved fmt_char() function to hp2100_sys.c

                        hp2100_dp.c, hp2100_dq.c (Dave Bryan):
                        - Added REG_FIT to register variables < 32-bit size

                        hp2100_dr.c (Dave Bryan):
                        - Revised drc_boot to use ibl_copy

                        hp2100_fp1.c (Dave Bryan):
                        - Quieted bogus gcc warning in fp_exec

                        hp2100_ipl.c (Dave Bryan):
                        - Changed socket poll to connect immediately after reset or attach
                        - Revised EDT handler to refine completion delay conditions
                        - Revised ipl_boot to use ibl_copy

                        hp2100_lpt.c (Dave Bryan):
                        - Changed CTIME register width to match documentation

                        hp2100_mpx.c (Dave Bryan):
                        - Implemented 12792C eight-channel terminal multiplexer

                        hp2100_ms.c (Dave Bryan):
                        - Revised to use AR instead of saved_AR in boot

                        hp2100_mt.c (Dave Bryan):
                        - Fixed missing flag after CLR command
                        - Moved write enable and format commands from MTD to MTC
                        
                        hp2100_mux.c (Dave Bryan):
                        - SHOW MUX CONN/STAT with SET MUX DIAG is no longer disallowed
                        - Changed Telnet poll to connect immediately after reset or attach
                        - Added LINEORDER support
                        - Added BREAK deferral to allow RTE break-mode to work

                        hp2100_pif.c (Dave Bryan):
                        - Implemented 12620A/12936A Privileged Interrupt Fences

                        hp2100_sys.c (Dave Bryan):
                        - Fixed IAK instruction dual-use mnemonic display
                        - Moved hp_setdev, hp_showdev from hp2100_cpu.c
                        - Changed sim_load to use WritePW instead of direct M[] access
                        - Added PIF device
                        - Moved fmt_char() function from hp2100_baci.c
                        - Added MPX device

                        hp2100_cpu.h (Dave Bryan):
                        - Rearranged declarations with hp2100_cpu.c and hp2100_defs.h
                        - Added mp_control to CPU state externals

                        hp2100_cpu1.h (Dave Bryan):
                        - Moved microcode function prototypes here

                        hp2100_defs.h (Dave Bryan):
                        - Added POLL_FIRST to indicate immediate connection attempt
                        - Rearranged declarations with hp2100_cpu.h
                        - Added PIF device
                        - Declared fmt_char() function
                        - Added MPX device

                        i1401_cpu.c:
                        - fixed bug in ZA and ZS (Bob Abeles)
                        - fixed tape indicator implementation (Bob Abeles)
                        - added missing magtape modifier A (Van Snyder)

                        i1401_mt.c:
                        - added -n (no rewind) option to BOOT (Van Snyder)
                        - fixed bug to mask input to 6b on read (Bob Abeles)

                        lgp_stddev.c:
                        - changed encode character from # to !, due to overlap

                        pdp11_cpu.c:
                        - fixed failure to clear cpu_bme on RESET (Walter Mueller)

                        pdp11_dz.c:
                        - added MTAB_NC modifier on SET LOG command (Walter Mueller)

                        pdp11_io.c, vax_io.c, vax780_uba.c:
                        - revised to use PDP-11 I/O library

                        pdp11_io_lib.c:
                        - created common library for Unibus/Qbus support routines

                        pdp11_cis.c, vax_cis.c:
                        - fixed bug in ASHP left overflow calc (Word/NibbleLShift)
                        - fixed bug in DIVx (LntDstr calculation)

                        sds_lp.c:
                        - fixed loss of carriage control position on space op

                        vax_stddev.c, vax780_stddev.c
                        - modified to resync TODR on any clock reset

  0     15-Jun-08       scp.c:
                        - fixed bug in local/global register search (Mark Pizzolato)
                        - fixed bug in restore of RO units (Mark Pizzolato)
                        - added SET/SHO/NO BR with default argument (Dave Bryan)

                        sim_tmxr.c
                        - worked around Telnet negotiation problem with QCTerm (Dave Bryan)

                        gri_defs.h, gri_cpu.c, gri_sys.c:
                        - added GRI-99 support

                        hp2100_baci.c (Dave Bryan):
                        - Implemented 12966A Buffered Asynchronous Communications Interface simulator

                        hp2100_cpu.c (Dave Bryan):
                        - Memory ex/dep and bkpt type default to current map mode
                        - Added SET CPU DEBUG and OS/VMA flags, enabled OS/VMA
                        - Corrected MP W5 (JSB) jumper action, SET/SHOW reversal,
                          mp_mevff clear on interrupt with I/O instruction in trap cell
                        - Removed DBI support from 1000-M (was temporary for RTE-6/VM)
                        - Enabled EMA and VIS, added EMA, VIS, and SIGNAL debug flags
                        - Enabled SIGNAL instructions, SIG debug flag
                        - Fixed single stepping through interrupts

                        hp2100_cpu0.c (Dave Bryan and Holger Veit):
                        - Removed and implemented "cpu_rte_vma" and "cpu_rte_os"
                        - Removed and implemented "cpu_vis" and "cpu_signal"
                        - Removed and implemented "cpu_rte_ema"

                        hp2100_cpu1.c (Dave Bryan):
                        - Added fprint_ops, fprint_regs for debug printouts
                        - Enabled DIAG as NOP on 1000 F-Series
                        - Fixed VIS and SIGNAL to depend on the FPP and HAVE_INT64

                        hp2100_cpu3.c (Dave Bryan):
                        - Fixed unsigned divide bug in .DDI
                        - Fixed unsigned multiply bug in .DMP
                        - Added implementation of DBI self-test

                        hp2100_cpu4.c (Dave Bryan):
                        - Fixed B register return bug in /CMRT

                        hp2100_cpu5.c (Holger Veit):
                        - Implemented RTE-6/VM Virtual Memory Area firmware
                        - Implemented RTE-IV Extended Memory Area firmware

                        hp2100_cpu6.c (Dave Bryan):
                        - Implemented RTE-6/VM OS accelerator firmware

                        hp2100_cpu7.c (Holger Veit):
                        - Implemented Vector Instruction Set and SIGNAL/1000 firmware

                        hp2100_ds.c (Dave Bryan):
                        - Corrected and verified ioCRS action
                        - Corrected DPTR register definition from FLDATA to DRDATA

                        hp2100_fp.c (Mark Pizzolato)
                        - Corrected fp_unpack mantissa high-word return

                        hp2100_fp1.c (Dave Bryan):
                        - Reworked "complement" to avoid inlining bug in gcc-4.x
                        - Fixed uninitialized return in fp_accum when setting

                        hp2100_mux.c (Dave Bryan):
                        - Sync mux poll with console poll for idle compatibility

                        hp2100_stddev.c (Dave Bryan):
                        - Fixed PTR trailing null counter for tape re-read
                        - Added IPTICK register to CLK to display CPU instr/tick
                        - Corrected and verified ioCRS actions
                        - Changed TTY console poll to 10 msec. real time
                        - Synchronized CLK with TTY if set for 10 msec.
                        - Added UNIT_IDLE to TTY and CLK
                        - Removed redundant control char handling definitions
                        - Changed TTY output wait from 100 to 200 for MSU BASIC

                        hp2100_sys.c (Dave Bryan):
                        - Added BACI device
                        - Added RTE OS/VMA/EMA mnemonics
                        - Changed fprint_sym to handle step with irq pending

                        hp2100_cpu.h (Dave Bryan):
                        - Added calc_defer() prototype
                        - Added extern sim_deb, cpu_dev, DEB flags for debug printouts
                        - Added extern intaddr, mp_viol, mp_mevff, calc_int, dev_ctl,
                          ReadIO, WriteIO for RTE-6/VM microcode support

                        hp2100_cpu1.h (Dave Bryan):
                        - Corrected OP_AFF to OP_AAFF for SIGNAL/1000
                        - Removed unused operand patterns
                        - Added fprint_ops, fprint_regs for debug printouts
                        - Revised OP_KKKAKK operand profile to OP_CCCACC for $LOC

                        hp2100_defs.h (Dave Bryan):
                        - Added BACI device
                        - Added 16/32-bit unsigned-to-signed conversions
                        - Changed TMR_MUX to TMR_POLL for idle support
                        - Added POLLMODE, sync_poll() declaration
                        - Added I_MRG, I_ISZ, I_IOG, I_STF, and I_SFS instruction masks
                        - Added I_MRG_I, I_JSB, I_JSB_I, and I_JMP instruction masks

                        nova_defs.h (Bruce Ray):
                        - added support for third-party 64KW memory

                        nova_clk.c (Bruce Ray):
                        - renamed to RTC, to match DG literature

                        nova_cpu.c (Bruce Ray):
                        - added support for third-party 64KW memory
                        - added Nova 3 "secret" instructions
                        - added CPU history support

                        nova_dkp.c (Bruce Ray):
                        - renamed to DKP, to match DG literature
                        - fixed numerous bugs in both documented and undocumented behavior
                        - changed bootstrap code to DG official sequence

                        nova_dsk.c (Bruce Ray):
                        - renamed to DSK, to match DG literature
                        - added support for undocumented behavior
                        - changed bootstrap code to DG official sequence

                        nova_mta.c (Bruce Ray):
                        - renamed to MTA, to match DG literature
                        - changed bootstrap code to DG official sequence

                        nova_plt.c, nova_pt.c (Bruce Ray):
                        - added 7B/8B support

                        nova_sys.c (Bruce Ray):
                        - fixed mistaken instruction mnemonics

                        pdp11_cpu.c, pdp11_io.c, pdp11_rh.c:
                        - fixed DMA memory address limit test (John Dundas)
                        - fixed MMR0 treatment in RESET (Walter Mueller)

                        pdp11_cpumod.h, pdp11_cpumod.c:
                        - fixed write behavior of 11/70 MBRK, LOSIZE, HISIZE (Walter Mueller)
                        - added support to set default state of KDJ11B,E clock control register

                        pdp11_dc.c:
                        - added support for DC11

                        pdp11_defs.h:
                        - added KE, KG, RC, DC support
                        - renamed DL11 devices

                        pdp11_dl.c:
                        - renamed devices to DLI/DLO, to match DC11
                        - added modem control

                        pdp11_io.c:
                        - added autoconfigure support for DC11

                        pdp11_ke.c:
                        - added support for KE11A

                        pdp11_kg.c (John Dundas):
                        - added support for KG11A

                        pdp11_rc.c (John Dundas):
                        - added support for RC11

                        pdp11_sys.c:
                        - modified to allow -A, -B use with 8b devices
                        - added KE, KG, RC, DC support
                        - renamed DL11 devices

                        vax_cmode.c, vax_io.c, vax780_uba.c:
                        - fixed declarations (Mark Pizzolato)


   V3.7 revision history 

  3     02-Sep-07       scp.c:
                        - fixed bug in SET THROTTLE command

                        pdp10_cpu.c:
                        - fixed non-portable usage in SHOW HISTORY routine

                        pdp11_ta.c:
                        - forward op at BOT skips initial file gap

                        pdp8_ct.c:
                        - forward op at BOT skips initial file gap
                        - fixed handling of BEOT

                        vax_cpu.c:
                        - fixed bug in read access g-format indexed specifiers

  2     12-Jul-07       sim_ether.c (Dave Hittner):
                        - fixed non-ethernet device removal loop (Naoki Hamada)
                        - added dynamic loading of wpcap.dll;
                        - corrected exceed max index bug in ethX lookup
                        - corrected failure to look up ethernet device names in
                          the registry on Windows XP x64

                        sim_timer.c:
                        - fixed idle timer event selection algorithm
  
                        h316_lp.c:
                        - fixed loss of last print line (Theo Engel)

                        h316_mt.c:
                        - fixed bug in write without stop (Theo Engel)

                        h316_stddev.c:
                        - fixed bug in clock increment (Theo Engel)

                        i1401_cpu.c:
                        - added recognition of overlapped operation modifiers
                        - remove restriction on load-mode binary tape operations

                        i1401_mt.c:
                        - fixed read tape mark operation (Van Snyder)
                        - remove restriction on load-mode binary tape operations

                        pdp1_cpu.c:
                        - fixed typo in SBS clear (Norm Lastovica)

                        pdp11_rh.c, pdp11_rp.c, pdp11_tu.c:
                        - CS1 DVA is in the device, not the MBA

                        pdp8_ct.c:
                        - fixed typo (Norm Lastovica)

                        vax_cpu.c:
                        - revised idle detector

  1     14-May-07       scp.c:
                        - modified sim_instr invocation to call sim_rtcn_init_all
                        - fixed bug in get_sim_opt (reported by Don North)
                        - fixed bug in RESTORE with changed memory size
                        - added global 'RESTORE in progress' flag
                        - fixed breakpoint actions in DO command file processing
                          (Dave Bryan)

                        all CPU's with clocks:
                        - removed clock initialization (now done in SCP)

                        hp2100_cpu.c (Dave Bryan):
                        - EDT passes input flag and DMA channel in dat parameter

                        hp2100_ipl.c (Dave Bryan):
                        - IPLI EDT delays DMA completion interrupt for TSB

                        hp2100_mux.c (Dave Bryan):
                        - corrected "mux_sta" size from 16 to 21 elements
                        - fixed "muxc_reset" to clear lines 16-20
                        - fixed control card OTx to set current channel number
                        - fixed to set "muxl_ibuf" in response to a transmit interrupt
                        - changed "mux_xbuf", "mux_rbuf" declarations from 8 to 16 bits
                        - fixed to set "mux_rchp" when a line break is received
                        - fixed incorrect "odd_par" table values
                        - reversed test in "RCV_PAR" to return "LIL_PAR" on odd parity
                        - rixed mux reset (ioCRS) to clear port parameters
                        - fixed to use PUT_DCH instead of PUT_CCH for data channel status
                        - added DIAG/TERM modifiers to implement diagnostic mode

                        pdp11_cpumod.c:
                        - changed memory size routine to work with RESTORE

                        pdp11_hk.c:
                        - NOP and DCLR (at least) do not check drive type
                        - MR2 and MR3 only updated on NOP

                        pdp10_tu.c, pdp11_tu.c:
                        - TMK sets FCE only on read (Naoki Hamada)

                        pdp11_xu.c:
                        - added missing FC_RMAL command
                        - cleared multicast on write

                        vax_moddefs.h, vax_cpu1.c:
                        - separated PxBR and SBR mbz checks

                        vax780_defs.h
                        - separated PxBR and SBR mbz checks
                        - modified mbz checks to reflect 780 microcode patches
                          (Naoki Hamada)

                        vax_mmu.c:
                        - added address masking to all SBR-based memory reads

  0     30-Jan-07       scp.c:
                        - implemented throttle commands
                        - added -e to control error processing in DO command files
                          (Dave Bryan)

                        sim_console.c:
                        - fixed handling of non-printable characters in KSR mode

                        sim_tape.c:
                        - fixed bug in reverse operations for P7B-format tapes
                        - fixed bug in reverse operations across erase gaps

                        sim_timer.c:
                        - added throttle support
                        - added idle support (based on work by Mark Pizzolato)

                        gri_stddev.c, h316_stddev.c, pdp18b_tt1.c
                        - fixed handling of non-printable characters in KSR mode

                        hp2100_cpu.c, hp2100_cpu0.c, hp2100_cpu1.c, hp2100_cpu2.c,
                        hp2100_cpu3.c, hp2100_cpu4.c (Dave Bryan):
                        - reorganized CPU modules for easier addition of new instructions
                        - added Double Integer instructions, 1000-F CPU, 2114 and
                          2115 CPUs, 12K and 24K memory sizes, 12607B and 12578A DMA
                          controllers, and 21xx binary loader protection
                        - fixed DMS self-test instruction execution on 1000-M
                        - fixed indirect interrupt holdoff logic

                        hp2100_ds.c:
                        - fixed REQUEST STATUS to clear status-1 (Dave Bryan)

                        hp2100_fp1.c:
                        - Added Floating Point Processor (Dave Bryan)

                        hp2100_lps.c:
                        - fixed diag-mode CLC response

                        i7094_cpu.c:
                        - fixed new bug in halt IO wait loop
                        - added IFT, EFT expanded core test instructions

                        id16_cpu.c, id32_cpu.c:
                        - removed separate multiplexor clock
                        - added idle support

                        id_pas.c:
                        - synced multiplexor poll to real-time clock

                        id_tt.c, id_ttp.c:
                        - fixed handling of non-printable characters in KSR mode
                        - synced keyboard poll to real-time clock

                        id_uvc.c:
                        - changed line-time clock to be free-running

                        pdp1_cpu.c:
                        - added 16-channel sequence break system (API) support
                        - added PDP-1D support

                        pdp1_clk.c:
                        - first release

                        pdp1_dcs.c:
                        - first release

                        pdp1_stddev.c:
                        - separated TTI, TTO for API support

                        pdp1_sys.c:
                        - added PDP-1D, 16-channel SBS, clock, DCS support
                        - fixed bugs in character input, block loader

                        pdp10_cpu.c:
                        - added idle support

                        pdp10_defs.h, pdp10_sys.c:
                        - added CR support

                        pdp10_fe.c, pdp10_tim.c:
                        - synced keyboard poll to real-time clock

                        pdp11_cr.c:
                        - revised for PDP-10 compatibility (CD11 only)

                        pdp11_cpu.c:
                        - added idle support
                        - fixed bug in ASH -32 C value

                        pdp11_rf.c:
                        - fixed unit mask (John Dundas)

                        pdp11_stddev.c, vax_stddev.c, vax780_stddev.c:
                        - synced keyboard poll to real-time clock
                        - added clock coscheduling support

                        pdp11_ta.c:
                        - first release

                        pdp11_vh.c:
                        - synced service poll to real-time clock
                        - changed device to be off by default

                        pdp11_dz.c, pdp11_xq.c, pdp11_xu.c:
                        - synced service poll to real-time clock

                        pdp11_sys.c:
                        - fixed operand order in EIS instructions (W.F.J. Mueller)
                        - added TA11 support

                        pdp18b_cpu.c:
                        - fixed incorrect value of PC on instruction fetch mem mmgt error
                        - fixed PDP-15 handling of mem mmgt traps (sets API 3)
                        - fixed PDP-15 handling of CAL API 4 (sets only if 0-3 inactive)
                        - fixed PDP-15 CAF to clear memory management mode register
                        - fixed boundary test in KT15/XVM (reported by Andrew Warkentin)
                        - added XVM RDCLK instruction
                        - added idle support and infinite loop detection

                        pdp18b_rf.c:
                        - fixed bug, DSCD does not clear function register

                        pdp18b_stddev.c:
                        - added PDP-15 program-selectable duplex handling instruction
                        - fixed PDP-15 handling of reader out-of-tape
                        - fixed handling of non-printable characters in KSR mode
                        - added XVM RDCLK instruction
                        - changed real-time clock to be free running
                        - synced keyboard poll to real-time clock

                        pdp18b_tt1.c
                        - fixed handling of non-printable characters in KSR mode

                        pdp18b_sys.c:
                        - added XVM RDCLK instruction

                        pdp8_cpu.c:
                        - fixed SC value after DVI overflow (Don North)
                        - added idle support and infinite loop detection

                        pdp8_ct.c:
                        - first release

                        pdp8_clk.c:
                        - changed real-time clock to be free running

                        pdp8_sys.c:
                        - added TA8E support
                        - added ability to disambiguate overlapping IOT definitions

                        pdp8_tt.c:
                        - fixed handling of non-printable characters in KSR mode
                        - synced keyboard poll to real-time clock

                        vax_cpu.c, vax_cpu1.c:
                        - added idle support

                        vax_syscm.c:
                        - fixed operand order in EIS instructions (W.F.J. Mueller)


   V3.6 revision history 

  1     25-Jul-06       sim_console.c:
                        - implemented SET/SHOW PCHAR

                        all DECtapes:
                        - fixed conflict in ATTACH switches

                        hp2100_ms.c (Dave Bryan):
                        - added CAPACITY as alternate for REEL
                        - fixed EOT test for unlimited reel size

                        i1620_cd.c (Tom McBride):
                        - fixed card reader fgets call
                        - fixed card reader boot sequence

                        i7094_cd.c:
                        - fixed problem with 80 column full cards

                        i7094_cpu.c:
                        - fixed bug in halt IO wait loop

                        i7094_sys.c:
                        - added binary loader (courtesy of Dave Pitt)

                        pdp1_cpu.c:
                        - fixed bugs in MUS and DIV

                        pdp11_cis.c:
                        - added interrupt tests to character instructions
                        - added 11/44 stack probe test to MOVCx (only)

                        pdp11_dl.c:
                        - first release

                        pdp11_rf.c:
                        - first release

                        pdp11_stddev.c:
                        - added UC support to TTI, TTO

                        pdp18b_cpu.c:
                        - fixed RESET to clear AC, L, and MQ

                        pdp18b_dt.c:
                        - fixed checksum calculation bug for Type 550

                        pdp18b_fpp.c:
                        - fixed bugs in left shift, multiply

                        pdp18b_stddev.c:
                        - fixed Baudot letters/figures inversion for PDP-4
                        - fixed letters/figures tracking for PDP-4
                        - fixed PDP-4/PDP-7 default terminal to be local echo

                        pdp18b_sys.c:
                        - added Fiodec, Baudot display
                        - generalized LOAD to handle HRI, RIM, and BIN files

                        pdp8_ttx.c:
                        - fixed bug in DETACH routine

  0     15-May-06       scp.c:
                        - revised save file format to save options, unit capacity

                        sim_tape.c, sim_tape.h:
                        - added support for finite reel size
                        - fixed bug in P7B write record

                        most magtapes:
                        - added support for finite reel size

                        h316_cpu.c: fixed bugs in LLL, LRL (Theo Engel)

                        h316_lp.c: fixed bug in blanks backscanning (Theo Engel)

                        h316_stddev.c: fixed bugs in punch state handling (Theo Engel)

                        i1401_cpu.c: fixed bug in divide (reported by Van Snyder)

                        i16_cpu.c: fixed bug in DH (Mark Hittinger)

                        i32_cpu.c:
                        - fixed bug in DH (Mark Hittinger)
                        - added support for 8 register banks in 8/32

                        i7094: first release

                        id_io.c: fixed bug, GO preserves EXA and SSTA (Davis Johnson)

                        id_idc.c:
                        - fixed WD/WH handling (Davis Johnson)
                        - fixed bug, nop command should be ignored (Davis Johnson)

                        nova_cpu.c: fixed bug in DIVS (Mark Hittinger)

                        pdp11_cis.c: (all reported by John Dundas)
                        - fixed bug in decode table
                        - fixed bug in ASHP
                        - fixed bug in write decimal string with mmgt enabled
                        - fixed bug in 0-length strings in multiply/divide

                        pdp11_cpu.c: fixed order of operand fetching in XOR for SDSD models

                        pdp11_cr.c: added CR11/CD11 support

                        pdp11_tc.c:
                        - fixed READ to set extended data bits in TCST (Alan Frisbie)

                        vax780_fload.c: added FLOAD command

                        vax780_sbi.c: fixed writes to ACCS

                        vax780_stddev.c: revised timer logic for EVKAE (reported by Tim Stark)

                        vax_cis.c: (all reported by Tim Stark)
                        - fixed MOVTC, MOVTUC to preserve cc's through page faults
                        - fixed MOVTUC to stop on translated == escape
                        - fixed CVTPL to set registers before destination reg write
                        - fixed CVTPL to set correct cc bit on overflow
                        - fixed EDITPC to preserve cc's through page faults
                        - fixed EDITPC EO$BLANK_ZERO count, cc test
                        - fixed EDITPC EO$INSERT to insert fill instead of blank
                        - fixed EDITPC EO$LOAD_PLUS/MINUS to skip character

                        vax_cpu.c:
                        - added KESU capability to virtual examine
                        - fixed bugs in virtual examine
                        - rewrote CPU history function for improved usability
                        (bugs below reported by Tim Stark)
                        - fixed fault cleanup to clear PSL<tp>
                        - fixed ADAWI r-mode to preserve dst<31:16>
                        - fixed ACBD/G to test correct operand
                        - fixed access checking on modify-class specifiers
                        - fixed branch address calculation in CPU history
                        - fixed bug in reported VA on faulting cross-page write

                        vax_cpu1.c: (all reported by Tim Stark)
                        - added access check on system PTE for 11/780
                        - added mbz check in LDPCTX for 11/780

                        vax_cmode.c: (all reported by Tim Stark)
                        - fixed omission of SXT
                        - fixed order of operand fetching in XOR

                        vax_fpa.c: (all reported by Tim Stark)
                        - fixed POLYD, POLYG to clear R4, R5
                        - fixed POLYD, POLYG to set R3 correctly
                        - fixed POLYD, POLYG to not exit prematurely if arg = 0
                        - fixed POLYD, POLYG to do full 64b multiply
                        - fixed POLYF, POLYD, POLYG to remove truncation on add
                        - fixed POLYF, POLYD, POLYG to mask mul reslt to 31b/63b/63b
                        - fixed fp add routine to test for zero via fraction
                          to support "denormal" argument from POLYF, POLYD, POLYG
                        - fixed bug in 32b floating multiply routine
                        - fixed bug in 64b extended modulus routine

                        vax_mmu.c:
                        - added access check on system PTE for 11/780

                        vax_octa.c: (all reported by Tim Stark)
                        - fixed MNEGH to test negated sign, clear C
                        - fixed carry propagation in qp_inc, qp_neg, qp_add
                        - fixed pack routines to test for zero via fraction
                        - fixed ACBH to set cc's on result
                        - fixed POLYH to set R3 correctly
                        - fixed POLYH to not exit prematurely if arg = 0
                        - fixed POLYH to mask mul reslt to 127b
                        - fixed fp add routine to test for zero via fraction
                          to support "denormal" argument from POLYH
                        - fixed EMODH to concatenate 15b of 16b extension
                        - fixed bug in reported VA on faulting cross-page write


   V3.5 revision history 

patch   date            module(s) and fix(es)

  2     07-Jan-06       scp.c:
                        - added breakpoint spaces
                        - added REG_FIT support

                        sim_console.c: added ASCII character processing routines

                        sim_tape.c, sim_tape.h:
                        - added write support for P7B format
                        - fixed bug in write forward (Dave Bryan)

                        h316_stddev.c, hp2100_stddev.c, hp2100_mux.c, id_tt.c,
                        id_ttp.c, id_pas.c, pdp8_tt.c, pdp8_ttx.c, pdp11_stddev.c,
                        pdp11_dz.c, pdp18b_stddev.c, pdp18b_tt1.c, vax_stddev,
                        gri_stddev.c:
                        - revised to support new character handling routines

                        pdp10_rp.c: fixed DCLR not to clear disk address

                        pdp11_hk.c: fixed overlapped seek interaction with NOP, etc

                        pdp11_rh.c: added enable/disable routine

                        pdp11_rq.c, pdp11_tm.c, pdp11_tq.c, pdp11_ts.c
                        - widened address display to 64b when USE_ADDR64

                        pdp11_rp.c:
                        - fixed DCLR not to clear disk address
                        - fixed device enable/disable logic to include Massbus adapter
                        - widened address display to 64b when USE_ADDR64

                        pdp11_tu.c:
                        - fixed device enable/disable logic to include Massbus adapter
                        - widened address display to 64b when USE_ADDR64
                        - changed default adapter to TM03 (for VMS)

                        pdp8_df.c, pdp8_dt.c, pdp8_rf.c:
                        - fixed unaligned access bug (Doug Carman)

                        pdp8_rl.c: fixed IOT 61 decoding bug (David Gesswein)

                        vax_cpu.c:
                        - fixed breakpoint detection when USE_ADDR64 option is active
                        - fixed CVTfi to trap on integer overflow if PSW<iv> set

  1     15-Oct-05       All CPU's, other sources: fixed declaration inconsistencies
                        (Sterling Garwood)

                        i1401_cpu.c: added control for old/new character encodings

                        i1401_cd.c, i1401_lpt.c, i1401_tty.c:
                        - changed character encodings to be consistent with 7094
                        - changed column binary format to be consistent with 7094
                        - added choice of business or Fortran set for output encoding

                        i1401_sys.c: changed WM character to ` under new encodings

                        i1620_cd.c, i1620_lpt.c, i1620_tty.c:
                        - changed character encodings to be consistent with 7094

                        pdp10_cpu.c: changed MOVNI to eliminate gcc warning

                        pdp11_io.c: fixed bug in autoconfiguration (missing XU)

                        vax_io.c: fixed bug in autoconfiguration (missing XU)

                        vax_fpa.c: fixed bug in 32b structure definitions (Jason Stevens)

  0     1-Sep-05        Note: most source modules have been edited to improve
                        readability and to fix declaration and cast problems in C++

                        all instruction histories: fixed reversed arguments to calloc

                        scp.c: revised to trim trailing spaces on file inputs

                        sim_sock.c: fixed SIGPIPE error on Unix

                        sim_ether.c: added Windows user-defined adapter names (Timothe Litt)

                        sim_tape.c: fixed misallocation of TPC map array

                        sim_tmxr.c: added support for SET <unit> DISCONNECT

                        hp2100_mux.c: added SET MUXLn DISCONNECT

                        i1401_cpu.c:
                        - fixed SSB-SSG clearing on RESET (reported by Ralph Reinke)
                        - removed error stops in MCE

                        i1401_cd.c: fixed read, punch to ignore modifier on 1, 4 char inst
                        (reported by Van Snyder)

                        id_pas.c:
                        - fixed bug in SHOW CONN/STATS
                        - added SET PASLn DISCONNECT

                        pdp10_ksio.c: revised for new autoconfiguration interface

                        pdp11_cpu.c: replaced WAIT clock queue check with API call

                        pdp11_cpumod.c: added additional 11/60 registers

                        pdp11_io.c: revised autoconfiguration algorithm and interface

                        pdp11_dz.c: revised for new autoconfiguration interface

                        pdp11_vh.c:
                        - revised for new autoconfiguration interface
                        - fixed bug in vector display routine

                        pdp11_xu.c: fixed runt packet processing (Tim Chapman)

                        pdp18b_cpu.c, pdp18b_sys.c:
                        - removed spurious AAS instruction

                        pdp18b_tt1.c:
                        - fixed bug in SHOW CONN/STATS
                        - fixed bug in SET LOG/NOLOG
                        - added SET TTOXn DISCONNECT

                        pdp8_ttx.c:
                        - fixed bug in SHOW CONN/STATS
                        - fixed bug in SET LOG/NOLOG
                        - added SET TTOXn DISCONNECT

                        sds_mux.c:
                        - fixed bug in SHOW CONN/STATS
                        - added SET MUXLn DISCONNECT

                        vaxmod_defs.h: added QDSS support

                        vax_io.c: revised autoconfiguration algorithm and interface

   V3.4 revision history 

  0     01-May-04       scp.c:
                        - fixed ASSERT code
                        - revised syntax for SET DEBUG (Dave Bryan)
                        - revised interpretation of fprint_sym, fparse_sym returns
                        - moved DETACH sanity tests into detach_unit

                        sim_sock.h and sim_sock.c:
                        - added test for WSAEINPROGRESS (Tim Riker)

                        many: revised detach routines to test for attached state

                        hp2100_cpu.c: reorganized CPU options (Dave Bryan)

                        hp2100_cpu1.c: reorganized EIG routines (Dave Bryan)

                        hp2100_fp1.c: added FFP support (Dave Bryan)

                        id16_cpu.c:
                        - fixed bug in show history routine (Mark Hittinger)
                        - revised examine/deposit to do words rather than bytes

                        id32_cpu.c:
                        - fixed bug in initial memory allocation
                        - fixed bug in show history routine (Mark Hittinger)
                        - revised examine/deposit to do words rather than bytes

                        id16_sys.c, id32_sys:
                        - revised examine/deposit to do words rather than bytes

                        pdp10_tu.c:
                        - fixed bug, ERASE and WREOF should not clear done (reported
                          by Rich Alderson)
                        - fixed error reporting

                        pdp11_tu.c: fixed error reporting

   V3.3 revision history 

  2     08-Mar-05       scp.c: added ASSERT command (Dave Bryan)

                        h316_defs.h: fixed IORETURN macro

                        h316_mt.c: fixed error reporting from OCP (Philipp Hachtmann)

                        h316_stddev.c: fixed bug in OCP '0001 (Philipp Hachtmann)

                        hp2100_cpu.c: split out EAU and MAC instructions

                        hp2100_cpu1.c: (Dave Bryan)
                        - fixed missing MPCK on JRS target
                        - removed EXECUTE instruction (is NOP in actual microcode)

                        hp2100_fp: (Dave Bryan)
                        - fixed missing negative overflow renorm in StoreFP

                        i1401_lp.c: fixed bug in write_line (reported by Van Snyder)

                        id32_cpu.c: fixed branches to mask new PC (Greg Johnson)

                        pdp11_cpu.c: fixed bugs in RESET for 11/70 (reported by Tim Chapman)

                        pdp11_cpumod.c:
                        - fixed bug in SHOW MODEL (Sergey Okhapkin)
                        - made SYSID variable for 11/70 (Tim Chapman)
                        - added MBRK write case for 11/70 (Tim Chapman)

                        pdp11_rq: added RA60, RA71, RA81 disks

                        pdp11_ry: fixed bug in boot code (reported by Graham Toal)

                        vax_cpu.c: fixed initial state of cpu_extmem

  1     05-Jan-05       h316_cpu.c: fixed bug in DIV

                        h316_stddev.c:
                        - fixed bug in SKS '104 (reported by Philipp Hachtmann)
                        - fixed bug in SKS '504
                        - adder reader/punch ASCII file support
                        - added Teletype reader/punch support

                        h316_dp.c: fixed bug in skip on !seeking

                        h316_mt.c: fixed bug in DMA/DMC support

                        h316_lp.c: fixed bug in DMA/DMC support

                        hp2100_cpu.c:
                        - fixed DMA reset to clear alternate CTL flop (Dave Bryan)
                        - fixed DMA reset to not clear control words (Dave Bryan)
                        - fixed SBS, CBS, TBS to do virtual reads
                        - separated A/B from M[0/1], for DMA IO (Dave Bryan)
                        - added SET CPU 21MX-M, 21MX-E (Dave Brian)
                        - disabled TIMER/EXECUTE/DIAG instructions for 21MX-M (Dave Bryan)
                        - added post-processor to maintain T/M consistency (Dave Bryan)

                        hp2100_ds.c: first release

                        hp2100_lps.c (all changes from Dave Bryan)
                        - added restart when set online, etc.
                        - fixed col count for non-printing chars

                        hp2100_lpt.c (all changes from Dave Bryan)
                        - added restart when set online, etc.

                        hp2100_sys.c (all changes from Dave Bryan):
                        - added STOP_OFFLINE, STOP_PWROFF messages

                        i1401_sys.c: added address argument support (Van Snyder)

                        id_mt.c: added read-only file support

                        lgp_cpu.c, lgp_sys.c: modified VM pointer setup

                        pdp11_cpu.c: fixed WAIT to work in all modes (John Dundas)

                        pdp11_tm.c, pdp11_ts.c: added read-only file support

                        sds_mt.c: added read-only file support

  0     23-Nov-04       scp.c:
                        - added reset_all_p (powerup)
                        - fixed comma-separated SET options (Dave Bryan)
                        - changed ONLINE/OFFLINE to ENABLED/DISABLED (Dave Bryan)
                        - modified to flush device buffers on stop (Dave Bryan)
                        - changed HELP to suppress duplicate command displays

                        sim_console.c:
                        - moved SET/SHOW DEBUG under CONSOLE hierarchy

                        hp2100_cpu.c: (all fixes by Dave Bryan)
                        - moved MP into its own device; added MP option jumpers
                        - modified DMA to allow disabling
                        - modified SET CPU 2100/2116 to truncate memory > 32K
                        - added -F switch to SET CPU to force memory truncation
                        - fixed S-register behavior on 2116
                        - fixed LIx/MIx behavior for DMA on 2116 and 2100
                        - fixed LIx/MIx behavior for empty I/O card slots
                        - modified WRU to be REG_HRO
                        - added BRK and DEL to save console settings
                        - fixed use of "unsigned int16" in cpu_reset

                        hp2100_dp.c: (all fixes by Dave Bryan)
                        - fixed enable/disable from either device
                        - fixed ANY ERROR status for 12557A interface
                        - fixed unattached drive status for 12557A interface
                        - status cmd without prior STC DC now completes (12557A)
                        - OTA/OTB CC on 13210A interface also does CLC CC
                        - fixed RAR model
                        - fixed seek check on 13210 if sector out of range

                        hp2100_dq.c: (all fixes by Dave Bryan)
                        - fixed enable/disable from either device
                        - shortened xtime from 5 to 3 (drive avg 156KW/second)
                        - fixed not ready/any error status
                        - fixed RAR model

                        hp2100_dr.c: (all fixes by Dave Bryan)
                        - fixed enable/disable from either device
                        - fixed sector return in status word
                        - provided protected tracks and "Writing Enabled" status bit
                        - fixed DMA last word write, incomplete sector fill value
                        - added "parity error" status return on writes for 12606
                        - added track origin test for 12606
                        - added SCP test for 12606
                        - fixed 12610 SFC operation
                        - added "Sector Flag" status bit
                        - added "Read Inhibit" status bit for 12606
                        - fixed current-sector determination
                        - added TRACKPROT modifier

                        hp2100_ipl.c, hp2100_ms.c: (all fixes by Dave Bryan)
                        - fixed enable/disable from either device

                        hp2100_lps.c: (all fixes by Dave Bryan)
                        - added SET OFFLINE/ONLINE, POWEROFF/POWERON
                        - fixed status returns for error conditions
                        - fixed handling of non-printing characters
                        - fixed handling of characters after column 80
                        - improved timing model accuracy for RTE
                        - added fast/realistic timing
                        - added debug printouts

                        hp2100_lpt.c: (all fixes by Dave Bryan)
                        - added SET OFFLINE/ONLINE, POWEROFF/POWERON
                        - fixed status returns for error conditions
                        - fixed TOF handling so form remains on line 0

                        hp2100_stddev.c (all fixes by Dave Bryan)
                        - added paper tape loop mode, DIAG/READER modifiers to PTR
                        - added PV_LEFT to PTR TRLLIM register
                        - modified CLK to permit disable

                        hp2100_sys.c: (all fixes by Dave Bryan)
                        - added memory protect device
                        - fixed display of CCA/CCB/CCE instructions

                        i1401_cpu.c: added =n to SHOW HISTORY

                        id16_cpu.c: added instruction history

                        id32_cpu.c: added =n to SHOW HISTORY

                        pdp10_defs.h: revised Unibus DMA API's

                        pdp10_ksio.c: revised Unibus DMA API's

                        pdp10_lp20.c: revised Unibus DMA API's

                        pdp10_rp.c: replicated register state per drive

                        pdp10_tu.c:
                        - fixed to set FCE on short record
                        - fixed to return bit<15> in drive type
                        - fixed format specification, 1:0 are don't cares
                        - implemented write check
                        - TMK is cleared by new motion command, not DCLR
                        - DONE is set on data transfers, ATA on non data transfers

                        pdp11_defs.h: 
                        - revised Unibus/Qbus DMA API's
                        - added CPU type and options flags

                        pdp11_cpumod.h, pdp11_cpumod.c:
                        - new routines for setting CPU type and options

                        pdp11_io.c: revised Unibus/Qbus DMA API's

                        all PDP-11 DMA peripherals:
                        - revised Unibus/Qbus DMA API's

                        pdp11_hk.c: CS2 OR must be zero for M+

                        pdp11_rh.c, pdp11_rp.c, pdp11_tu.c:
                        - split Massbus adapter from controllers
                        - replicated RP register state per drive
                        - added TM02/TM03 with TE16/TU45/TU77 drives

                        pdp11_rq.c, pdp11_tq.c:
                        - provided different default timing for PDP-11, VAX
                        - revised to report CPU bus type in stage 1
                        - revised to report controller type reflecting bus type
                        - added -L switch (LBNs) to RAUSER size specification

                        pdp15_cpu.c: added =n to SHOW HISTORY

                        pdp15_fpp.c:
                        - fixed URFST to mask low 9b of fraction
                        - fixed exception PC setting

                        pdp8_cpu.c: added =n to SHOW HISTORY

                        vax_defs.h:
                        - added octaword, compatibility mode support

                        vax_moddefs.h: 
                        - revised Unibus/Qbus DMA API's

                        vax_cpu.c:
                        - moved processor-specific code to vax_sysdev.c
                        - added =n to SHOW HISTORY

                        vax_cpu1.c:
                        - moved processor-specific IPR's to vax_sysdev.c
                        - moved emulation trap to vax_cis.c
                        - added support for compatibility mode

                        vax_cis.c: new full VAX CIS instruction emulator

                        vax_octa.c: new full VAX octaword and h_floating instruction emulator

                        vax_cmode.c: new full VAX compatibility mode instruction emulator

                        vax_io.c:
                        - revised Unibus/Qbus DMA API's

                        vax_io.c, vax_stddev.c, vax_sysdev.c:
                        - integrated powerup into RESET (with -p)

                        vax_sys.c:
                        - fixed bugs in parsing indirect displacement modes
                        - fixed bugs in displaying and parsing character data

                        vax_syscm.c: added display and parse for compatibility mode

                        vax_syslist.c:
                        - split from vax_sys.c
                        - removed PTR, PTP

   V3.2 revision history 

  3     03-Sep-04       scp.c:
                        - added ECHO command (Dave Bryan)
                        - qualified RESTORE detach with SIM_SW_REST

                        sim_console: added OS/2 EMX fixes (Holger Veit)

                        sim_sock.h: added missing definition for OS/2 (Holger Veit)

                        hp2100_cpu.c: changed error stops to report PC not PC + 1
                        (Dave Bryan)

                        hp2100_dp.c: functional and timing fixes (Dave Bryan)
                        - controller sets ATN for all commands except read status
                        - controller resumes polling for ATN interrupts after read status
                        - check status on unattached drive set busy and not ready
                        - check status tests wrong unit for write protect status
                        - drive on line sets ATN, will set FLG if polling

                        hp2100_dr.c: fixed CLC to stop operation (Dave Bryan)

                        hp2100_ms.c: functional and timing fixes (Dave Bryan)
                        - fixed erroneous execution of rejected command
                        - fixed erroneous execution of select-only command
                        - fixed erroneous execution of clear command
                        - fixed odd byte handling for read
                        - fixed spurious odd byte status on 13183A EOF
                        - modified handling of end of medium
                        - added detailed timing, with fast and realistic modes
                        - added reel sizes to simulate end of tape
                        - added debug printouts

                        hp2100_mt.c: modified handling of end of medium (Dave Bryan)

                        hp2100_stddev.c: added tab to control char set (Dave Bryan)

                        pdp11_rq.c: VAX controllers luns start at 0 (Andreas Cejna)

                        vax_cpu.c: fixed bug in EMODD/G, second word of quad dst not probed

  2     17-Jul-04       scp.c: fixed problem ATTACHing to read only files
                        (John Dundas)

                        sim_console.c: revised Windows console code (Dave Bryan)

                        sim_fio.c: fixed problem in big-endian read
                        (reported by Scott Bailey)

                        gri_cpu.c: updated MSR, EAO functions

                        hp_stddev.c: generalized handling of control char echoing
                        (Dave Bryan)

                        vax_sys.c: fixed bad block initialization routine

  1     10-Jul-04       scp.c: added SET/SHOW CONSOLE subhierarchy

                        hp2100_cpu.c: fixes and added features (Dave Bryan)
                        - SBT increments B after store
                        - DMS console map must check dms_enb
                        - SFS x,C and SFC x,C work
                        - MP violation clears automatically on interrupt
                        - SFS/SFC 5 is not gated by protection enabled
                        - DMS enable does not disable mem prot checks
                        - DMS status inconsistent at simulator halt
                        - Examine/deposit are checking wrong addresses
                        - Physical addresses are 20b not 15b
                        - Revised DMS to use memory rather than internal format
                        - Added instruction printout to HALT message
                        - Added M and T internal registers
                        - Added N, S, and U breakpoints
                        Revised IBL facility to conform to microcode
                        Added DMA EDT I/O pseudo-opcode
                        Separated DMA SRQ (service request) from FLG

                        all HP2100 peripherals:
                        - revised to make SFS x,C and SFC x,C work
                        - revised to separate SRQ from FLG

                        all HP2100 IBL bootable peripherals:
                        - revised boot ROMs to use IBL facility
                        - revised SR values to preserve SR<5:3>

                        hp2100_lps.c, hp2100_lpt.c: fixed timing
                        
                        hp2100_dp.c: fixed interpretation of SR<0>

                        hp2100_dr.c: revised boot code to use IBL algorithm

                        hp2100_mt.c, hp2100_ms.c: fixed spurious timing error after CLC
                         (Dave Bryan)

                        hp2100_stddev.c:
                        - fixed input behavior during typeout for RTE-IV
                        - suppressed nulls on TTY output for RTE-IV

                        hp2100_sys.c: added SFS x,C and SFC x,C to print/parse routines

                        pdp10_fe.c, pdp11_stddev.c, pdp18b_stddev.c, pdp8_tt.c, vax_stddev.c:
                        - removed SET TTI CTRL-C option

                        pdp11_tq.c:
                        - fixed bug in reporting write protect (reported by Lyle Bickley)
                        - fixed TK70 model number and media ID (Robert Schaffrath)

                        pdp11_vh.c: added DHQ11 support (John Dundas)

                        pdp11_io.c, vax_io.c: fixed DHQ11 autoconfigure (John Dundas)

                        pdp11_sys.c, vax_sys.c: added DHQ11 support (John Dundas)

                        vax_cpu.c: fixed bug in DIVBx, DIVWx (reported by Peter Trimmel)

  0     04-Apr-04       scp.c:
                        - added sim_vm_parse_addr and sim_vm_fprint_addr
                        - added REG_VMAD
                        - moved console logging to SCP
                        - changed sim_fsize to use descriptor rather than name
                        - added global device/unit show modifiers
                        - added device debug support (Dave Hittner)
                        - moved device and unit flags, updated save format

                        sim_ether.c:
                        - further generalizations (Dave Hittner, Mark Pizzolato)

                        sim_tmxr.h, sim_tmxr.c:
                        - added tmxr_linemsg
                        - changed TMXR definition to support variable number of lines

                        sim_libraries:
                        - new console library (sim_console.h, sim_console.c)
                        - new file I/O library (sim_fio.h, sim_fio.c)
                        - new timer library (sim_timer.h, sim_timer.c)

                        all terminal multiplexors: revised for tmxr library changes

                        all DECtapes:
                        - added STOP_EOR to enable end-of-reel stop
                        - revised for device debug support

                        all variable-sized devices: revised for sim_fsize change

                        eclipse_cpu.c, nova_cpu.c: fixed device enable/disable support
                           (Bruce Ray)

                        nova_defs.h, nova_sys.c, nova_qty.c:
                        - added QTY and ALM support (Bruce Ray)

                        id32_cpu.c, id_dp.c: revised for device debug support

                        lgp: added LGP-30 [LGP-21] simulator

                        pdp1_sys.c: fixed bug in LOAD (Mark Crispin)

                        pdp10_mdfp.c:
                        - fixed bug in floating unpack
                        - fixed bug in FIXR (Philip Stone, fixed by Chris Smith)

                        pdp11_dz.c: added per-line logging

                        pdp11_rk.c:
                        - added formatting support
                        - added address increment inhibit support
                        - added transfer overrun detection

                        pdp11_hk.c, pdp11_rp.c: revised for device debug support

                        pdp11_rq.c: fixed bug in interrupt control (Tom Evans)

                        pdp11_ry.c: added VAX support

                        pdp11_tm.c, pdp11_tq.c, pdp11_ts.c: revised for device debug support

                        pdp11_xu.c: replaced stub with real implementation (Dave Hittner)

                        pdp18b_cpu.c:
                        - fixed bug in XVM g_mode implementation
                        - fixed bug in PDP-15 indexed address calculation
                        - fixed bug in PDP-15 autoindexed address calculation

                        pdp18b_fpp.c: fixed bugs in instruction decode

                        pdp18b_stddev.c:
                        - fixed clock response to CAF
                        - fixed bug in hardware read-in mode bootstrap

                        pdp18b_sys.c: fixed XVM instruction decoding errors

                        pdp18b_tt1.c: added support for 1-16 additional terminals

                        vax_moddef.h, vax_cpu.c, vax_sysdev.c:
                        - added extended physical memory support (Mark Pizzolato)
                        - added RXV21 support

                        vax_cpu1.c:
                        - added PC read fault in EXTxV
                        - fixed PC write fault in INSV

   V3.1 revision history

  0     29-Dec-03       sim_defs.h, scp.c: added output stall status

                        all console emulators: added output stall support

                        sim_ether.c (Dave Hittner, Mark Pizzolato, Anders Ahgren):
                        - added Alpha/VMS support
                        - added FreeBSD, Mac OS/X support
                        - added TUN/TAP support
                        - added DECnet duplicate address detection

                        all memory buffered devices (fixed head disks, floppy disks):
                        - cleaned up buffer copy code

                        all DECtapes:
                        - fixed reverse checksum in read all
                        - added DECtape off reel message
                        - simplified timing

                        eclipse_cpu.c (Charles Owen):
                        - added floating point support
                        - added programmable interval timer support
                        - bug fixes

                        h316_cpu.c:
                        - added instruction history
                        - added DMA/DMC support
                        - added device ENABLE/DISABLE support
                        - change default to HSA option included

                        h316_dp.c: added moving head disk support

                        h316_fhd.c: added fixed head disk support

                        h316_mt.c: added magtape support

                        h316_sys.c: added new device support

                        nova_dkp.c (Charles Owen):
                        - fixed bug in flag clear sequence
                        - added diagnostic mode support for disk sizing

`                       nova_mt.c (Charles Owen):
                        - fixed bug, space operations return record count
                        - fixed bug, reset doesn't cancel rewind

                        nova_sys.c: added floating point, timer support (Charles Owen)

                        i1620_cpu.c: fixed bug in branch digit (Dave Babcock)

                        pdp1_drm.c:
                        - added parallel drum support
                        - fixed bug in serial drum instructin decoding

                        pdp1_sys.c: added parallel drum support, mnemonics

                        pdp11_cpu.c:
                        - added autoconfiguration controls
                        - added support for 18b-only Qbus devices
                        - cleaned up addressing/bus definitions

                        pdp11_rk.c, pdp11_ry.c, pdp11_tm.c, pdp11_hk.c:
                        - added Q18 attribute

                        pdp11_io.c:
                        - added autoconfiguration controls
                        - fixed bug in I/O configuration (Dave Hittner)

                        pdp11_rq.c:
                        - revised MB->LBN conversion for greater accuracy
                        - fixed bug with multiple RAUSER drives

                        pdp11_tc.c: changed to be off by default (base config is Qbus)

                        pdp11_xq.c (Dave Hittner, Mark Pizzolato):
                        - fixed second controller interrupts
                        - fixed bugs in multicast and promiscuous setup
  
                        pdp18b_cpu.c:
                        - added instruction history
                        - fixed PDP-4,-7,-9 autoincrement bug
                        - change PDP-7,-9 default to API option included

                        pdp8_defs.h, pdp8_sys.c:
                        - added DECtape off reel message
                        - added support for TSC8-75 (ETOS) option
                        - added support for TD8E controller

                        pdp8_cpu.c: added instruction history

                        pdp8_rx.c:
                        - fixed bug in RX28 read status (Charles Dickman)
                        - fixed double density write

                        pdp8_td.c: added TD8E controller

                        pdp8_tsc.c: added TSC8-75 option

                        vax_cpu.c:
                        - revised instruction history for dynamic sizing
                        - added autoconfiguration controls

                        vax_io.c:
                        - added autoconfiguration controls
                        - fixed bug in I/O configuration (Dave Hittner)

                        id16_cpu.c: revised instruction decoding

                        id32_cpu.c:
                        - revised instruction decoding
                        - added instruction history

   V3.0 revision history 

  2     15-Sep-03       scp.c:
                        - fixed end-of-file problem in dep, idep
                        - fixed error on trailing spaces in dep, idep

                        pdp1_stddev.c
                        - fixed system hang if continue after PTR error
                        - added PTR start/stop functionality
                        - added address switch functionality to PTR BOOT

                        pdp1_sys.c: added multibank capability to LOAD
  
                        pdp18b_cpu.c:
                        - fixed priorities in PDP-15 API (PI between 3 and 4)
                        - fixed sign handling in PDP-15 unsigned mul/div
                        - fixed bug in CAF, must clear API subsystem

                        i1401_mt.c:
                        - fixed tape read end-of-record handling based on real 1401
                        - added diagnostic read (space forward)

                        i1620_cpu.c
                        - fixed bug in immediate index add (Michael Short)

  1     27-Jul-03       pdp1_cpu.c: updated to detect indefinite I/O wait

                        pdp1_drm.c: fixed incorrect logical, missing activate, break

                        pdp1_lp.c:
                        - fixed bugs in instruction decoding, overprinting
                        - updated to detect indefinite I/O wait

                        pdp1_stddev.c:
                        - changed RIM loader to be "hardware"
                        - updated to detect indefinite I/O wait

                        pdp1_sys.c: added block loader format support to LOAD

                        pdp10_rp.c: fixed bug in read header

                        pdp11_rq: fixed bug in user disk size (Chaskiel M Grundman)

                        pdp18b_cpu.c:
                        - added FP15 support
                        - added XVM support
                        - added EAE support to the PDP-4
                        - added PDP-15 "re-entrancy ECO"
                        - fixed memory protect/skip interaction
                        - fixed CAF to only reset peripherals

                        pdp18b_fpp.c: added FP15

                        pdp18b_lp.c: fixed bug in Type 62 overprinting

                        pdp18b_rf.c: fixed bug in set size routine

                        pdp18b_stddev.c:
                        - increased PTP TIME for PDP-15 operating systems
                        - added hardware RIM loader for PDP-7, PDP-9, PDP-15

                        pdp18b_sys.c: added FP15, KT15, XVM instructions

                        pdp8b_df.c, pdp8_rf.c: fixed bug in set size routine

                        hp2100_dr.c:
                        - fixed drum sizes
                        - fixed variable capacity interaction with SAVE/RESTORE

                        i1401_cpu.c: revised fetch to model hardware more closely

                        ibm1130: fixed bugs found by APL 1130

                        nova_dsk.c: fixed bug in set size routine

                        altairz80: fixed bug in real-time clock on Windows host

  0     15-Jun-03       scp.c:
                        - added ASSIGN/DEASSIGN
                        - changed RESTORE to detach files
                        - added u5, u6 unit fields
                        - added USE_ADDR64 support
                        - changed some structure fields to unsigned

                        scp_tty.c: added extended file seek

                        sim_sock.c: fixed calling sequence in stubs

                        sim_tape.c:
                        - added E11 and TPC format support
                        - added extended file support

                        sim_tmxr.c: fixed bug in SHOW CONNECTIONS

                        all magtapes:
                        - added multiformat support
                        - added extended file support

                        i1401_cpu.c:
                        - fixed mnemonic, instruction lengths, and reverse
                           scan length check bug for MCS
                        - fixed MCE bug, BS off by 1 if zero suppress
                        - fixed chaining bug, D lost if return to SCP
                        - fixed H branch, branch occurs after continue
                        - added check for invalid 8 character MCW, LCA

                        i1401_mt.c: fixed load-mode end of record response

                        nova_dsk.c: fixed variable size interaction with restore

                        pdp1_dt.c: fixed variable size interaction with restore

                        pdp10_rp.c: fixed ordering bug in attach

                        pdp11_cpu.c:
                        - fixed bug in MMR1 update (Tim Stark)
                        - fixed bug in memory size table

                        pdp11_lp.c, pdp11_rq.c: added extended file support

                        pdp11_rl.c, pdp11_rp.c, pdp11_ry.c: fixed ordering bug in attach

                        pdp11_tc.c: fixed variable size interaction with restore

                        pdp11_xq.c:
                        - corrected interrupts on IE state transition (code by Tom Evans)
                        - added interrupt clear on soft reset (first noted by Bob Supnik)
                        - removed interrupt when setting XL or RL (multiple people)
                        - added SET/SHOW XQ STATS
                        - added SHOW XQ FILTERS
                        - added ability to split received packet into multiple buffers
                        - added explicit runt & giant packet processing

                        vax_fpa.c:
                        - fixed integer overflow bug in CVTfi
                        - fixed multiple bugs in EMODf

                        vax_io.c: optimized byte and word DMA routines

                        vax_sysdev.c:
                        - added calibrated delay to ROM reads (Mark Pizzolato)
                        - fixed calibration problems in interval timer (Mark Pizzolato)

                        pdp1_dt.c: fixed variable size interaction with restore

                        pdp18b_dt.c: fixed variable size interaction with restore

                        pdp18b_mt.c: fixed bug in MTTR

                        pdp18b_rf.c: fixed variable size interaction with restore

                        pdp8_df.c, pdp8_rf.c: fixed variable size interaction
                        with restore

                        pdp8_dt.c: fixed variable size interaction with restore

                        pdp8_mt.c: fixed bug in SKTR

                        hp2100_dp.c,hp2100_dq.c:
                        - fixed bug in read status (13210A controller)
                        - fixed bug in seek completion

                        id_pt.c: fixed type declaration (Mark Pizzolato)

                        gri_cpu.c: fixed bug in SC queue pointer management

   V2.10 revision history

  4     03-Mar-03       scp.c
                        - added .ini startup file capability
                        - added multiple breakpoint actions
                        - added multiple switch evaluation points
                        - fixed bug in multiword deposits to file

                        sim_tape.c: magtape simulation library

                        h316_stddev.c: added set line frequency command

                        hp2100_mt.c, hp2100_ms.c: revised to use magtape library

                        i1401_mt.c: revised to use magtape library

                        id_dp.c, id_idc.c: fixed cylinder overflow on writes

                        id_mt.c:
                        - fixed error handling to stop selector channel
                        - revised to use magtape library

                        id16_sys.c, id32_sys.c: added relative addressing support

                        id_uvc.c:
                        - added set frequency command to line frequency clock
                        - improved calibration algorithm for precision clock

                        nova_clk.c: added set line frequency command

                        nova_dsk.c: fixed autosizing algorithm

                        nova_mt.c: revised to use magtape library

                        pdp10_tu.c: revised to use magtape library

                        pdp11_cpu.c: fixed bug in MMR1 update (Tim Stark)

                        pdp11_stddev.c
                        - added set line frequency command
                        - added set ctrl-c command

                        pdp11_rq.c:
                        - fixed ordering problem in queue process
                        - fixed bug in vector calculation for VAXen
                        - added user defined drive support

                        pdp11_ry.c: fixed autosizing algorithm

                        pdp11_tm.c, pdp11_ts.c: revised to use magtape library

                        pdp11_tq.c:
                        - fixed ordering problem in queue process
                        - fixed overly restrictive test for bad modifiers
                        - fixed bug in vector calculation for VAXen
                        - added variable controller, user defined drive support
                        - revised to use magtape library

                        pdp18b_cpu.c: fixed three EAE bugs (Hans Pufal)

                        pdp18b_mt.c:
                        - fixed bugs in BOT error handling, interrupt handling
                        - revised to use magtape library

                        pdp18b_rf.c:
                        - removed 22nd bit from disk address
                        - fixed autosizing algorithm

                        pdp18b_stddev.c:
                        - added set line frequency command
                        - added set ctrl-c command

                        pdp18b_sys.c: fixed FMTASC printouts (Hans Pufal)

                        pdp8_clk.c: added set line frequency command

                        pdp8_df.c, pdp8_rf.c, pdp8_rx.c: fixed autosizing algorithm

                        pdp8_mt.c:
                        - fixed bug in BOT error handling
                        - revised to use magtape library

                        pdp8_tt.c: added set ctrl-c command

                        sds_cpu.c: added set line frequency command

                        sds_mt.c: revised to use magtape library

                        vax_stddev.c: added set ctrl-c command

  3     06-Feb-03       scp.c:
                        - added dynamic extension of the breakpoint table
                        - added breakpoint actions

                        hp2100_cpu.c: fixed last cycle bug in DMA output (found by
                        Mike Gemeny)

                        hp2100_ipl.c: individual links are full duplex (found by
                        Mike Gemeny)

                        pdp11_cpu.c: changed R, SP to track PSW<rs,cm> respectively

                        pdp18b_defs.h, pdp18b_sys.c: added RB09 fixed head disk,
                        LP09 printer

                        pdp18b_rf.c:
                        - fixed IOT decoding (Hans Pufal)
                        - fixed address overrun logic
                        - added variable number of platters and autosizing

                        pdp18b_rf.c:
                        - fixed IOT decoding
                        - fixed bug in command initiation

                        pdp18b_rb.c: new RB09 fixed head disk

                        pdp18b_lp.c: new LP09 line printer

                        pdp8_df.c: added variable number of platters and autosizing

                        pdp8_rf.c: added variable number of platters and autosizing

                        nova_dsk.c: added variable number of platters and autosizing

                        id16_cpu.c: fixed bug in SETM, SETMR (Mark Pizzolato)

  2     15-Jan-03       scp.c:
                        - added dynamic memory size flag and RESTORE support
                        - added EValuate command
                        - added get_ipaddr routine
                        - added ! (OS command) feature (Mark Pizzolato)
                        - added BREAK support to sim_poll_kbd (Mark Pizzolato)

                        sim_tmxr.c:
                        - fixed bugs in IAC+IAC handling (Mark Pizzolato)
                        - added IAC+BRK handling (Mark Pizzolato)

                        sim_sock.c:
                        - added use count for Windows start/stop
                        - added sim_connect_sock

                        pdp1_defs.h, pdp1_cpu.c, pdp1_sys.c, pdp1_drm.c:
                        added Type 24 serial drum

                        pdp18_defs.h: added PDP-4 drum support

                        hp2100_cpu.c: added 21MX IOP support

                        hp2100_ipl.c: added HP interprocessor link support

                        pdp11_tq.c: fixed bug in transfer end packet length

                        pdp11_xq.c:
                        - added VMScluster support (thanks to Mark Pizzolato)
                        - added major performance enhancements (thanks to Mark Pizzolato)
                        - added local packet processing
                        - added system id broadcast

                        pdp11_stddev.c: changed default to 7b (for early UNIX)

                        vax_cpu.c, vax_io.c, vax_stddev.c, vax_sysdev.c:
                        added console halt capability (Mark Pizzolato)

                        all terminals and multiplexors: added BREAK support

  1     21-Nov-02       pdp1_stddev.c: changed typewriter to half duplex
                        (Derek Peschel)

                        pdp10_tu.c:
                        - fixed bug in bootstrap (reported by Michael Thompson)
                        - fixed bug in read (reported by Harris Newman)

  0     15-Nov-02       SCP and libraries
                        scp.c:
                        - added Telnet console support
                        - removed VT emulation support
                        - added support for statically buffered devices
                        - added HELP <command>
                        - fixed bugs in set_logon, ssh_break (David Hittner)
                        - added VMS file optimization (Robert Alan Byer)
                        - added quiet mode, DO with parameters, GUI interface,
                           extensible commands (Brian Knittel)
                        - added DEVICE context and flags
                        - added central device enable/disable support
                        - modified SAVE/GET to save and restore flags
                        - modified boot routine calling sequence
                        scp_tty.c:
                        - removed VT emulation support
                        - added sim_os_sleep, renamed sim_poll_kbd, sim_putchar
                        sim_tmxr.c:
                        - modified for Telnet console support
                        - fixed bug in binary (8b) support
                        sim_sock.c: modified for Telnet console support
                        sim_ether.c: new library for Ethernet (David Hittner)

                        all magtapes:
                        - added support for end of medium
                        - cleaned up BOT handling

                        all DECtapes: added support for RT11 image file format

                        most terminals and multiplexors:
                        - added support for 7b vs 8b character processing

                        PDP-1
                        pdp1_cpu.c, pdp1_sys.c, pdp1_dt.c: added PDP-1 DECtape support

                        PDP-8
                        pdp8_cpu.c, all peripherals:
                        - added variable device number support
                        - added new device enabled/disable support
                        pdp8_rx.c: added RX28/RX02 support

                        PDP-11
                        pdp11_defs.h, pdp11_io.c, pdp11_sys.c, all peripherals:
                        - added variable vector support
                        - added new device enable/disable support
                        - added autoconfiguration support
                        all bootstraps: modified to support variable addresses
                        dec_mscp.h, pdp11_tq.c: added TK50 support
                        pdp11_rq.c:
                        - added multicontroller support
                        - fixed bug in HBE error log packet
                        - fixed bug in ATP processing
                        pdp11_ry.c: added RX211/RX02 support
                        pdp11_hk.c: added RK611/RK06/RK07 support
                        pdp11_tq.c: added TMSCP support
                        pdp11_xq.c: added DEQNA/DELQA support (David Hittner)
                        pdp11_pclk.c: added KW11P support
                        pdp11_ts.c:
                        - fixed bug in CTL decoding
                        - fixed bug in extended status XS0_MOT
                        pdp11_stddev.c: removed paper tape to its own module

                        PDP-18b
                        pdp18b_cpu.c, all peripherals:
                        - added variable device number support
                        - added new device enabled/disabled support

                        VAX
                        dec_dz.h: fixed bug in number of boards calculation
                        vax_moddefs.h, vax_io.c, vax_sys.c, all peripherals:
                        - added variable vector support
                        - added new device enable/disable support
                        - added autoconfiguration support
                        vax_sys.c:
                        - generalized examine/deposit
                        - added TMSCP, multiple RQDX3, DEQNA/DELQA support
                        vax_stddev.c: removed paper tape, now uses PDP-11 version
                        vax_sysdev.c:
                        - allowed NVR to be attached to file
                        - removed unused variables (David Hittner)

                        PDP-10
                        pdp10_defs.h, pdp10_ksio.c, all peripherals:
                        - added variable vector support
                        - added new device enable/disable support
                        pdp10_defs.h, pdp10_ksio.c: added support for standard PDP-11
                           peripherals, added RX211 support
                        pdp10_pt.c: rewritten to reference common implementation

                        Nova, Eclipse:
                        nova_cpu.c, eclipse_cpu.c, all peripherals:
                        - added new device enable/disable support

                        HP2100
                        hp2100_cpu:
                        - fixed bugs in the EAU, 21MX, DMS, and IOP instructions
                        - fixed bugs in the memory protect and DMS functions
                        - created new options to enable/disable EAU, MPR, DMS
                        - added new device enable/disable support
                        hp2100_fp.c:
                        - recoded to conform to 21MX microcode algorithms
                        hp2100_stddev.c:
                        - fixed bugs in TTY reset, OTA, time base generator
                        - revised BOOT support to conform to RBL loader
                        - added clock calibration
                        hp2100_dp.c:
                        - changed default to 13210A
                        - added BOOT support
                        hp2100_dq.c:
                        - finished incomplete functions, fixed head switching
                        - added BOOT support
                        hp2100_ms.c:
                        - fixed bugs found by diagnostics
                        - added 13183 support
                        - added BOOT support
                        hp2100_mt.c:
                        - fixed bugs found by diagnostics
                        - disabled by default
                        hp2100_lpt.c: implemented 12845A controller
                        hp2100_lps.c:
                        - renamed 12653A controller
                        - added diagnostic mode for MPR, DCPC diagnostics
                        - disabled by default

                        IBM 1620: first release

   V2.9 revision history

  11    20-Jul-02       i1401_mt.c: on read, end of record stores group mark
                           without word mark (Van Snyder)

                        i1401_dp.c: reworked address generation and checking

                        vax_cpu.c: added infinite loop detection and halt to
                           boot ROM option (Mark Pizzolato)

                        vax_fpa.c: changed function names to prevent conflict
                           with C math library

                        pdp11_cpu.c: fixed bug in MMR0 update logic (from
                           John Dundas)

                        pdp18b_stddev.c: added "ASCII mode" for reader and
                           punch (Hans Pufal)

                        gri_*.c: added GRI-909 simulator

                        scp.c: added DO echo, DO exit (Brian Knittel)

                        scp_tty.c: added Windows priority hacking (from
                           Mark Pizzolato)

  10    15-Jun-02       scp.c: fixed error checking on calls to fxread/fxwrite
                           (Norm Lastovic)

                        scp_tty.c, sim_vt.h, sim_vt.c: added VTxxx emulation
                           support for Windows (Fischer Franz)

                        sim_sock.c: added OS/2 support (Holger Veit)

                        pdp11_cpu.c: fixed bugs (John Dundas)
                        - added special case for PS<15:12> = 1111 to MFPI
                        - removed special case from MTPI
                        - added masking of relocation adds 

                        i1401_cpu.c:
                        - added multiply/divide
                        - fixed bugs (Van Snyder)
                           o 5 and 7 character H, 7 character doesn't branch
                           o 8 character NOP
                           o 1401-like memory dump

                        i1401_dp.c: added 1311 disk

  9     04-May-02       pdp11_rq: fixed bug in polling routine

  8     03-May-02       scp.c:
                        - changed LOG/NOLOG to SET LOG/NOLOG
                        - added SHOW LOG
                        - added SET VT/NOVT and SHOW VT for VT emulation
  
                        sim_sock.h: changed VMS stropt.h include to ioctl.h

                        vax_cpu.c
                        - added TODR powerup routine to set date, time on boot
                        - fixed exception flows to clear trap request
                        - fixed register logging in autoincrement indexed

                        vax_stddev.c: added TODR powerup routine
                        
                        vax_cpu1.c: fixed exception flows to clear trap request

  7     30-Apr-02       scp.c: fixed bug in clock calibration when (real) clock
                           jumps forward due too far (Jonathan Engdahl)
  
                        pdp11_cpu.c: fixed bugs, added features (John Dundas
                           and Wolfgang Helbig)
                        - added HTRAP and BPOK to maintenance register
                        - added trap on kernel HALT if MAINT<HTRAP> set
                        - fixed red zone trap, clear odd address and nxm traps
                        - fixed RTS SP, don't increment restored SP
                        - fixed TSTSET, write dst | 1 rather than prev R0 | 1
                        - fixed DIV, set N=0,Z=1 on div by zero (J11, 11/70)
                        - fixed DIV, set set N=Z=0 on overfow (J11, 11/70)
                        - fixed ASH, ASHC, count = -32 used implementation-
                           dependent 32 bit right shift
                        - fixed illegal instruction test to detect 000010
                        - fixed write-only page test

                        pdp11_rp.c: fixed SHOW ADDRESS command

                        vaxmod_defs.h: fixed DZ vector base and number of lines

                        dec_dz.h:
                        - fixed interrupt acknowledge routines
                        - fixed SHOW ADDRESS command

                        all magtape routines: added test for badly formed
                           record length (suggested by Jonathan Engdahl)

  6     18-Apr-02       vax_cpu.c: fixed CASEL condition codes

                        vax_cpu1.c: fixed vfield pos > 31 test to be unsigned

                        vax_fpu.c: fixed EDIV overflow test for 0 quotient

  5     14-Apr-02       vax_cpu1.c:
                        - fixed interrupt, prv_mode set to 0 (Tim Stark)
                        - fixed PROBEx to mask mode to 2b (Kevin Handy)

  4     1-Apr-02        pdp11_rq.c: fixed bug, reset cleared write protect status

                        pdp11_ts.c: fixed bug in residual frame count after space

  3     15-Mar-02       pdp11_defs.h: changed default model to KDJ11A (11/73)

                        pdp11_rq.c: adjusted delays for M+ timing bugs

                        hp2100_cpu.c, pdp10_cpu.c, pdp11_cpu.c: tweaked abort
                           code for ANSI setjmp/longjmp compliance

                        hp2100_cpu.c, hp2100_fp.c, hp2100_stddev.c, hp2100_sys.c:
                           revised to allocate memory dynamically

  2     01-Mar-02       pdp11_cpu.c:
                        - fixed bugs in CPU registers
                        - fixed double operand evaluation order for M+

                        pdp11_rq.c: added delays to initialization for
                           RSX11M+ prior to V4.5

  1     20-Feb-02       scp.c: fixed bug in clock calibration when (real)
                        time runs backwards

                        pdp11_rq.c: fixed bug in host timeout logic

                        pdp11_ts.c: fixed bug in message header logic

                        pdp18b_defs.h, pdp18b_dt.c, pdp18b_sys.c: added
                           PDP-7 DECtape support

                        hp2100_cpu.c:
                        - added floating point and DMS
                        - fixed bugs in DIV, ASL, ASR, LBT, SBT, CBT, CMW

                        hp2100_sys.c: added floating point, DMS

                        hp2100_fp.c: added floating point

                        ibm1130: added Brian Knittel's IBM 1130 simulator

  0     30-Jan-02       scp.c:
                        - generalized timer package for multiple timers
                        - added circular register arrays
                        - fixed bugs, line spacing in modifier display
                        - added -e switch to attach
                        - moved device enable/disable to simulators

                        scp_tty.c: VAX specific fix (Robert Alan Byer)

                        sim_tmxr.c, sim_tmxr.h:
                        - added tmxr_fstats, tmxr_dscln
                        - renamed tmxr_fstatus to tmxr_fconns

                        sim_sock.c, sim_sock.h: added VMS support (from
                        Robert Alan Byer)

                        pdp_dz.h, pdp18b_tt1.c, nova_tt1.c:
                        - added SET DISCONNECT
                        - added SHOW STATISTICS

                        pdp8_defs.h: fixed bug in interrupt enable initialization

                        pdp8_ttx.c: rewrote as unified multiplexor

                        pdp11_cpu.c: fixed calc_MMR1 macro (Robert Alan Byer)

                        pdp11_stddev.c: fixed bugs in KW11L (John Dundas)

                        pdp11_rp.c: fixed bug in 18b mode boot

                        pdp11 bootable I/O devices: fixed register setup at boot
                           exit (Doug Carman)

                        hp2100_cpu.c:
                        - fixed DMA register tables (Bill McDermith)
                        - fixed SZx,SLx,RSS bug (Bill McDermith)
                        - fixed flop restore logic (Bill McDermith)

                        hp2100_mt.c: fixed bug on write of last character

                        hp2100_dq,dr,ms,mux.c: added new disk, magtape, and terminal
                           multiplexor controllers

                        i1401_cd.c, i1401_mt.c: new zero footprint bootstraps
                           (Van Snyder)

                        i1401_sys.c: fixed symbolic display of H, NOP with no trailing
                           word mark (Van Snyder)

                        most CPUs:
                        - replaced OLDPC with PC queue
                        - implemented device enable/disable locally

   V2.8 revision history

5       25-Dec-01       scp.c: fixed bug in DO command (John Dundas)

                        pdp10_cpu.c:
                        - moved trap-in-progress to separate variable
                        - cleaned up declarations
                        - cleaned up volatile state for GNU C longjmp

                        pdp11_cpu.c: cleaned up declarations
  
                        pdp11_rq.c: added RA-class disks

4       17-Dec-01       pdp11_rq.c: added delayed processing of packets

3       16-Dec-01       pdp8_cpu.c:
                        - mode A EAE instructions didn't clear GTF
                        - ASR shift count > 24 mis-set GTF
                        - effective shift count == 32 didn't work

2       07-Dec-01       scp.c: added breakpoint package

                        all CPU's: revised to use new breakpoint package

1       05-Dec-01       scp.c: fixed bug in universal register name logic

0       30-Nov-01       Reorganized simh source and documentation tree

                        scp: Added DO command, universal registers, extended
                           SET/SHOW logic

                        pdp11: overhauled PDP-11 for DMA map support, shared
                           sources with VAX, dynamic buffer allocation

                        18b pdp: overhauled interrupt structure

                        pdp8: added RL8A

                        pdp10: fixed two ITS-related bugs (Dave Conroy)

   V2.7 revision history

patch   date            module(s) and fix(es)

15      23-Oct-01       pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: fixed bugs
                           error interrupt handling

                        pdp10_defs.h, pdp10_ksio.c, pdp10_fe.c, pdp10_fe.c,
                        pdp10_rp.c, pdp10_tu.c: reworked I/O page interface
                           to use symbolic base addresses and lengths

14      20-Oct-01       dec_dz.h, sim_tmxr_h, sim_tmxr.c: fixed bug in Telnet
                           state handling (Thord Nilson), removed
                           tmxr_getchar, added tmxr_rqln and tmxr_tqln

13      18-Oct-01       pdp11_tm.c: added stub diagnostic register clock
                           for RSTS/E (Thord Nilson)

12      15-Oct-01       pdp11_defs.h, pdp11_cpu.c, pdp11_tc.c, pdp11_ts.c,
                           pdp11_rp.c: added operations logging

11      8-Oct-01        scp.c: added sim_rev.h include and version print

                        pdp11_cpu.c: fixed bug in interrupt acknowledge,
                           multiple outstanding interrupts caused the lowest
                           rather than the highest to be acknowledged

10      7-Oct-01        pdp11_stddev.c: added monitor bits (CSR<7>) for full
                           KW11L compatibility, needed for RSTS/E autoconfiguration

9       6-Oct-01        pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: rewrote interrupt
                           logic from RH11/RH70 schematics, to mimic hardware quirks

                        dec_dz.c: fixed bug in carrier detect logic, carrier
                           detect was being cleared on next modem poll

8       4-Oct-01        pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: undid edit of
                           28-Sep-01; real problem was level-sensitive nature of
                           CS1_SC, but CS1_SC can only trigger an interrupt if
                           DONE is set

7       2-Oct-01        pdp11_rp.c, pdp10_rp.c: CS1_SC is evaluated as a level-
                           sensitive, rather than an edge-sensitive, input to
                           interrupt request

6       30-Sep-01       pdp11_rp.c, pdp10_rp.c: separated out CS1<5:0> to per-
                           drive registers

                        pdp10_tu.c: based on above, cleaned up handling of
                           non-existent formatters, fixed non-data transfer
                           commands clearing DONE

5       28-Sep-01       pdp11_rp.c, pdp10_rp.c, pdp10_tu.c: controller should
                           interrupt if ATA or SC sets when IE is set, was
                           interrupting only if DON = 1 as well

4       27-Sep-01       pdp11_ts.c:
                        - NXM errors should return TC4 or TC5; were returning TC3
                        - extended features is part of XS2; was returned in XS3
                        - extended characteristics (fifth) word needed for RSTS/E

                        pdp11_tc.c: stop, stop all do cause an interrupt

                        dec_dz.h: scanner should find a ready output line, even
                           if there are no connections; needed for RSTS/E autoconfigure

                        scp.c:
                        - added routine sim_qcount for 1130
                        - added "simulator exit" detach routine for 1130

                        sim_defs.h: added header for sim_qcount

3       20-Sep-01       pdp11_ts.c: boot code binary was incorrect

2       19-Sep-01       pdp18b_cpu.c: EAE should interpret initial count of 00
                           as 100

                        scp.c: modified Macintosh support

1       17-Sep-01       pdp8_ttx.c: new module for PDP-8 multi-terminal support

                        pdp18b_tt1.c: modified to use sim_tmxr library

                        nova_tt1.c: modified to use sim_tmxr library

                        dec_dz.h: added autodisconnect support

                        scp.c: removed old multiconsole support

                        sim_tmxr.c: modified calling sequence for sim_putchar_ln

                        sim_sock.c: added Macintosh sockets support
*/

#endif
