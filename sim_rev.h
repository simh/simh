/* sim_rev.h: simulator revisions and current rev level

   Copyright (c) 1993-2001, Robert M Supnik

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
#define SIM_MINOR	7
#define SIM_PATCH	15

/* SIMH detailed revision list, starting with V2.7

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
			non-existent formatters, fixed non-data transfer commands
			clearing DONE

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