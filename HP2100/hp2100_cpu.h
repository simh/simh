/* hp2100_cpu.h: HP 2100 CPU definitions

   Copyright (c) 2005, Robert M. Supnik

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

   14-Jan-05	RMS	Cloned from hp2100_cpu.c
*/

#ifndef _HP2100_CPU_H_
#define _HP2100_CPU_H_	0

#define PCQ_SIZE	64				/* must be 2**n */
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = err_PC

#define UNIT_V_2100	(UNIT_V_UF + 0)			/* 2100 */
#define UNIT_V_21MX	(UNIT_V_UF + 1)			/* 21MX-E or 21MX-M */
#define UNIT_V_EAU	(UNIT_V_UF + 2)			/* EAU */
#define UNIT_V_FP	(UNIT_V_UF + 3)			/* FP */
#define UNIT_V_DMS	(UNIT_V_UF + 4)			/* DMS */
#define UNIT_V_IOP	(UNIT_V_UF + 5)			/* 2100 IOP */
#define UNIT_V_IOPX	(UNIT_V_UF + 6)			/* 21MX IOP */
#define UNIT_V_MSIZE	(UNIT_V_UF + 7)			/* dummy mask */
#define UNIT_V_MXM  	(UNIT_V_UF + 8)			/* 21MX is M-series */
#define UNIT_2116	(0)
#define UNIT_2100	(1 << UNIT_V_2100)
#define UNIT_21MX	(1 << UNIT_V_21MX)
#define UNIT_EAU	(1 << UNIT_V_EAU)
#define UNIT_FP		(1 << UNIT_V_FP)
#define UNIT_DMS	(1 << UNIT_V_DMS)
#define UNIT_IOP	(1 << UNIT_V_IOP)
#define UNIT_IOPX	(1 << UNIT_V_IOPX)
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define UNIT_MXM	(1 << UNIT_V_MXM)

t_stat Ea (uint32 IR, uint32 *addr, uint32 irq);
uint8 ReadB (uint32 addr);
uint8 ReadBA (uint32 addr);
uint16 ReadW (uint32 addr);
uint16 ReadWA (uint32 addr);
uint32 ReadF (uint32 addr);
void WriteB (uint32 addr, uint32 dat);
void WriteBA (uint32 addr, uint32 dat);
void WriteW (uint32 addr, uint32 dat);
void WriteWA (uint32 addr, uint32 dat);
t_stat iogrp (uint32 ir, uint32 iotrap);
void mp_dms_jmp (uint32 va);
uint16 dms_rmap (uint32 mapi);
void dms_wmap (uint32 mapi, uint32 dat);
void dms_viol (uint32 va, uint32 st);
uint32 dms_upd_sr (void);

#endif
