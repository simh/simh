/* hp2100_cpu.c: HP 2100 EAU and MAC simulator

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

   CPU		extended arithmetic and microcode instructions

   22-Feb-05    JDB     Fixed missing MPCK on JRS target
			Removed EXECUTE instruction (is NOP in actual microcode)
   15-Jan-05	RMS	Cloned from hp2100_cpu.c
*/

#include "hp2100_defs.h"
#include <setjmp.h>
#include "hp2100_cpu.h"

extern uint16 ABREG[2];
extern uint32 PC;
extern uint32 err_PC;
extern uint32 XR;
extern uint32 YR;
extern uint32 E;
extern uint32 O;
extern uint32 dms_enb;
extern uint32 dms_ump;
extern uint32 dms_sr;
extern uint32 dms_vr;
extern uint32 mp_fence;
extern uint32 iop_sp;
extern uint32 ion_defer;
extern uint16 pcq[PCQ_SIZE];
extern uint32 pcq_p;
extern uint32 stop_inst;
extern jmp_buf save_env;
extern UNIT cpu_unit;

extern t_stat Ea1 (uint32 *addr, uint32 irq);
extern uint32 f_as (uint32 op, t_bool sub);
extern uint32 f_mul (uint32 op);
extern uint32 f_div (uint32 op);
extern uint32 f_fix (void);
extern uint32 f_flt (void);

/* Extended instruction decode tables */

#define E_V_FL		0				/* flags */
#define E_M_FL		0xFF
#define E_FP		(UNIT_FP >> (UNIT_V_UF - E_V_FL))
#define E_21MX		(UNIT_21MX >> (UNIT_V_UF - E_V_FL))
#define E_DMS		(UNIT_DMS >> (UNIT_V_UF - E_V_FL))
#define E_IOP		(UNIT_IOP >> (UNIT_V_UF - E_V_FL))
#define E_IOPX		(UNIT_IOPX >> (UNIT_V_UF - E_V_FL))
#define E_V_TY		8				/* type */
#define E_M_TY		0xF
#define  E_NO		0				/* no operands */
#define  E_CN		1				/* PC+1: count */
#define  E_AD		2				/* PC+1: addr */
#define  E_AA		3				/* PC+1,2: addr */
#define  E_AC		4				/* PC+1: addr, +2: count */
#define  E_AZ		5				/* PC+1: addr, +2: zero */
#define ET_NO		(E_NO << E_V_TY)
#define ET_AD		(E_AD << E_V_TY)
#define ET_AA		(E_AA << E_V_TY)
#define ET_CN		(E_CN << E_V_TY)
#define ET_AC		(E_AC << E_V_TY)
#define ET_AZ		(E_AZ << E_V_TY)
#define E_V_TYI		12				/* type if 2100 IOP */
#define E_GETFL(x)	(((x) >> E_V_FL) & E_M_FL)
#define E_GETTY(f,x)	(((x) >> \
			    ((((f) & E_IOP) && (cpu_unit.flags & UNIT_IOP))? \
				E_V_TYI: E_V_TY)) & E_M_TY)
#define F_NO		E_FP | ET_NO
#define F_MR		E_FP | ET_AD
#define X_NO		E_21MX | ET_NO
#define X_MR		E_21MX | ET_AD
#define X_AA		E_21MX | ET_AA
#define X_AZ		E_21MX | ET_AZ
#define D_NO		E_DMS | ET_NO
#define D_MR		E_DMS | ET_AD
#define D_AA		E_DMS | ET_AA
#define M_NO		E_IOPX | ET_NO
#define M_CN		E_IOPX | ET_CN
#define M_AC		E_IOPX | ET_AC
#define I_NO		E_IOP | (ET_NO << (E_V_TYI - E_V_TY))
#define I_CN		E_IOP | (ET_CN << (E_V_TYI - E_V_TY))
#define I_AC		E_IOP | (ET_AC << (E_V_TYI - E_V_TY))
#define I_AZ		E_IOP | (ET_AZ << (E_V_TYI - E_V_TY))

static const uint32 e_inst[512] = {
 F_MR | I_AC,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,		/* FAD/ILIST */
 F_MR | I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,	/* FSB/LAI- */
 I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,
 F_MR | I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,	/* FMP/LAI+ */
 I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,
 F_MR | I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,	/* FDV/SAI- */
 I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,
 F_NO | I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,	/* FIX/SAI+ */
 I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,I_NO,
 F_NO | I_AZ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,		/* FLT/MBYTE */
 0,0,0,0,0,0,0,0,I_CN,0,0,0,0,0,0,0,			/* CRC */
 I_CN,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* TRSLT */
 I_AZ,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* WMOVE */
 I_NO,I_NO,I_NO,I_NO,0,0,0,0,0,0,0,0,0,0,0,0,		/* READF,PFRIO,PFREI,PFREX */
 I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,0,I_NO,			/* ENQ,PENQ */
 I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* DEQ */
 I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* SBYTE */
 I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* LBYTE */
 I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* REST */
 0,0,I_NO,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* SAVE */
 M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,		/* LAI-/SAI- */
 M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,
 M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,		/* LAI+/SAI+ */
 M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0440 */
 M_CN,M_NO,M_NO,M_NO,M_NO,M_NO,M_NO,M_CN,		/* CRC,REST,READF,INS,ENQ,PENQ,DEQ,TR */
 M_AC,M_NO,M_NO,M_NO,M_NO,0,0,0,			/* ILIST,PFREI,PFREX,PFRIO,SAVE */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0500 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0520 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0540 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0560 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0600 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0620 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0640 */
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,			/* 0660 */
 D_NO,D_NO,D_NO,D_NO,D_NO,D_NO,D_NO,D_NO,		/* XMM,test,MBI,MBF,MBW,MWI,MWF,MWW */
 D_NO,D_NO,D_NO,D_NO,D_MR,D_AA,D_NO,D_NO,		/* SY*,US*,PA*,PB*,SSM,JRS,nop,nop */
 D_NO,D_NO,D_NO,D_NO,D_MR,D_MR,D_MR,D_NO,		/* XMM,XMS,XM*,nop,XL*,XS*,XC*,LF* */
 D_NO,D_NO,D_MR,D_MR,D_MR,D_MR,D_MR,D_MR,		/* RS*,RV*,DJP,DJS,SJP,SJS,UJP,UJS */
 X_MR,X_NO,X_MR,X_MR,X_NO,X_MR,X_MR,X_NO,		/* S*X,C*X,L*X,STX,CX*,LDX,ADX,X*X */
 X_MR,X_NO,X_MR,X_MR,X_NO,X_MR,X_MR,X_NO,		/* S*Y,C*Y,L*Y,STY,CY*,LDY,ADY,X*Y */
 X_NO,X_NO,X_MR,X_NO,X_NO,X_AZ,X_AZ,X_NO,		/* ISX,DSX,JLY,LBT,SBT,MBT,CBT,SFB */
 X_NO,X_NO,X_NO,X_AA,X_AA,X_AA,X_AZ,X_AZ };		/* ISY,DSY,JPY,SBS,CBS,TBS,CMW,MVW */

/* Extended arithmetic

   The 21MX-E adds three "special instructions" that do not exist in earlier
   CPUs, including the 21MX-M.  They are: TIMER (100060), EXECUTE (100120), and
   DIAG (100000).  On the 21MX-M, these instruction codes map to the
   microroutines for MPY, ASL, and RRL, respectively.

   Under simulation, these cause undefined instruction stops if the CPU is set
   to 2100 or 2116.  They do not cause stops on the 21MX-M, as TIMER in
   particular is used by several HP programs to differentiate between M- and
   E-series machines. */

t_stat cpu_eau (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
uint32 MA, v1, v2, t;
uint32 rs, qs, sc;
int32 sop1, sop2;

if ((cpu_unit.flags & UNIT_EAU) == 0) return stop_inst;	/* implemented? */

switch ((IR >> 8) & 017) {				/* case on IR<11:8> */

    case 000:						/* EAU group 0 */
	switch ((IR >> 4) & 017) {			/* decode IR<7:4> */
	case 001:					/* ASL */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    O = 0;					/* clear ovflo */
	    while (sc-- != 0) {				/* bit by bit */
		t = BR << 1;				/* shift B */
		BR = (BR & SIGN) | (t & 077777) | (AR >> 15);
		AR = (AR << 1) & DMASK;
		if ((BR ^ t) & SIGN) O = 1;  }
	    break;
	case 002:					/* LSL */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
	    AR = (AR << sc) & DMASK;			/* BR'AR lsh left */
	    break;
	case 000:					/* DIAG */
	    if (!(cpu_unit.flags & UNIT_21MX))		/* must be 21MX */
		return stop_inst;			/* trap if not */
	    if (!(cpu_unit.flags & UNIT_MXM))		/* E-series? */
		break;					/* is NOP unless halted */
	case 004:					/* RRL (+ DIAG on 21MX-M) */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    t = BR;					/* BR'AR rot left */
	    BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
	    AR = ((AR << sc) | (t >> (16 - sc))) & DMASK;
	    break;
	case 003:					/* TIMER */
	    if (!(cpu_unit.flags & UNIT_21MX))		/* must be 21MX */
		return stop_inst;			/* trap if not */
	    else if (!(cpu_unit.flags & UNIT_MXM)) {	/* E-series? */
		BR = (BR + 1) & DMASK;  		/* increment B */
		if (BR) PC = err_PC;			/* if !=0, repeat */
		break;  }
	case 010:					/* MPY (+ TIMER on 21MX-M) */
	    if (reason = Ea1 (&MA, intrq)) break;	/* get opnd addr */
	    sop1 = SEXT (AR);				/* sext AR */
	    sop2 = SEXT (ReadW (MA));			/* sext mem */
	    sop1 = sop1 * sop2;				/* signed mpy */
	    BR = (sop1 >> 16) & DMASK;			/* to BR'AR */
	    AR = sop1 & DMASK;
	    O = 0;					/* no overflow */
	    break;
	default:
	    return stop_inst;
	    }
	break;

    case 001:						/* divide */
	if (reason = Ea1 (&MA, intrq)) break;		/* get opnd addr */
	if (rs = qs = BR & SIGN) {			/* save divd sign, neg? */
	    AR = (~AR + 1) & DMASK;			/* make B'A pos */
	    BR = (~BR + (AR == 0)) & DMASK;  }		/* make divd pos */
	v2 = ReadW (MA);				/* divr = mem */
	if (v2 & SIGN) {				/* neg? */
	    v2 = (~v2 + 1) & DMASK;			/* make divr pos */
	    qs = qs ^ SIGN;  }				/* sign of quotient */
	if (BR >= v2) O = 1;				/* divide work? */
	else {						/* maybe... */
	    O = 0;					/* assume ok */
	    v1 = (BR << 16) | AR;			/* 32b divd */
	    AR = (v1 / v2) & DMASK;			/* quotient */
	    BR = (v1 % v2) & DMASK;			/* remainder */
	    if (AR) {					/* quotient > 0? */
		if (qs) AR = (~AR + 1) & DMASK;		/* apply quo sign */
		if ((AR ^ qs) & SIGN) O = 1;  }		/* still wrong? ovflo */
	    if (rs) BR = (~BR + 1) & DMASK;  }		/* apply rem sign */
	break;

    case 002:						/* EAU group 2 */
	switch ((IR >> 4) & 017) {			/* decode IR<7:4> */
	case 001:					/* ASR */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
	    BR = (SEXT (BR) >> sc) & DMASK;		/* BR'AR ash right */
	    O = 0;
	    break;
	case 002:					/* LSR */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
	    BR = BR >> sc;				/* BR'AR log right */
	    break;
	case 004:					/* RRR */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    t = AR;					/* BR'AR rot right */
	    AR = ((AR >> sc) | (BR << (16 - sc))) & DMASK;
	    BR = ((BR >> sc) | (t << (16 - sc))) & DMASK;
	    break;
	default:
	    return stop_inst;
	    }
	break;

    case 010:						/* DLD */
	if (reason = Ea1 (&MA, intrq)) break;		/* get opnd addr */
	AR = ReadW (MA);				/* load AR */
	MA = (MA + 1) & VAMASK;
	BR = ReadW (MA);				/* load BR */
	break;

    case 011:						/* DST */
	if (reason = Ea1 (&MA, intrq)) break;		/* get opnd addr */
	WriteW (MA, AR);				/* store AR */
	MA = (MA + 1) & VAMASK;
	WriteW (MA, BR);				/* store BR */
	break;

    default:						/* should never get here */
	return SCPE_IERR;
	}
return reason;
}

t_stat cpu_mac (uint32 IR, uint32 intrq)
{
t_stat reason;
uint32 MA, M1, absel, v1, v2, t;
uint32 fop, eop, etype, eflag;
uint32 mapi, mapj;
uint32 awc, wc, hp, tp;
int32 i, sop1, sop2;

absel = (IR & I_AB)? 1: 0;				/* get A/B select */
eop = IR & 0777;					/* extended opcode */
eflag = E_GETFL (e_inst[eop]);				/* get flags */
if ((eflag & (cpu_unit.flags >> UNIT_V_UF)) == 0)	/* invalid? error */
	return stop_inst;
etype = E_GETTY (eflag, e_inst[eop]);			/* get format */
if (etype > E_CN) {					/* at least 1 addr? */
	if (reason = Ea1 (&MA, intrq))			/* get first address */
	     return reason;  }
if ((etype == E_AC) || (etype == E_CN)) {		/* addr + cnt, cnt */
	wc = ReadW (PC);				/* get count */
	awc = PC;					/* addr of count */
	PC = (PC + 1) & VAMASK;  }
else if (etype == E_AZ) {				/* addr + zero */
	wc = ReadW (MA);				/* get wc */
	awc = PC;					/* addr of interim */
	if (wc) {					/* wc > 0? */
	    if (t = ReadW (PC)) wc = t;  }		/* use interim if nz */
	WriteW (awc, 0);				/* clear interim */
	PC = (PC + 1) & VAMASK;  }
else if (etype == E_AA) {				/* second addr */
	if (reason = Ea1 (&M1, intrq))			/* get second address */
	    return reason;  }

switch (eop) {						/* decode IR<8:0> */

/* Floating point instructions */

	case 0000:					/* IOP ILIST/FAD */
	    if (cpu_unit.flags & UNIT_IOP)		/* ILIST (E_AC) */
		goto IOP_ILIST;
	    fop = ReadF (MA);				/* get fop */
	    O = f_as (fop, 0);				/* add, upd ovflo */
	    break;
	case 0020:					/* IOP LAI-/FSB */
	    if (cpu_unit.flags & UNIT_IOP)		/* LAI -20 (I_NO) */
		goto IOP_LAIM;
	    fop = ReadF (MA);				/* get fop */
	    O = f_as (fop, 1);				/* sub, upd ovflo */
	    break;
	case 0040:					/* IOP LAI+/FMP */
	    if (cpu_unit.flags & UNIT_IOP)		/* LAI 0 (I_NO) */
		goto IOP_LAIP;
	    fop = ReadF (MA);				/* get fop */
	    O = f_mul (fop);				/* mul, upd ovflo */
	    break;
	case 0060:					/* IOP SAI-/FDV */
	    if (cpu_unit.flags & UNIT_IOP)		/* SAI -20 (I_NO) */
		goto IOP_SAIM;		
	    fop = ReadF (MA);				/* get fop */
	    O = f_div (fop);				/* div, upd ovflo */
	    break;
	case 0100:					/* IOP SAI+/FIX */
	    if (cpu_unit.flags & UNIT_IOP)		/* SAI 0 (I_NO) */
		goto IOP_SAIP;
	    O = f_fix ();				/* FIX (E_NO) */
	    break;
	case 0120:					/* IOP MBYTE/FLT */
	    if (cpu_unit.flags & UNIT_IOP)		/* MBYTE (I_AZ) */
		goto IOP_MBYTE;
	    O = f_flt ();				/* FLT (E_NO) */
	    break;

/* 2100 (and 21MX) IOP instructions */

	IOP_LAIM:  case 0021: case 0022: case 0023:	/* IOP LAI- (I_NO) */
	case 0024: case 0025: case 0026: case 0027:
	case 0030: case 0031: case 0032: case 0033:
	case 0034: case 0035: case 0036: case 0037:
	    MA = ((IR | 0177760) + BR) & VAMASK;	/* IR<3:0> = -offset */
	    AR = ReadW (MA);				/* load AR */
	    break;
	IOP_LAIP:  case 0041: case 0042: case 0043:	/* IOP LAI+ (I_NO) */
	case 0044: case 0045: case 0046: case 0047:
	case 0050: case 0051: case 0052: case 0053:
	case 0054: case 0055: case 0056: case 0057:
	    MA = ((IR & 017) + BR) & VAMASK;		/* IR<3:0> = +offset */
	    AR = ReadW (MA);				/* load AR */
	    break;
	IOP_SAIM:  case 0061: case 0062: case 0063:	/* IOP SAI- (I_NO) */
	case 0064: case 0065: case 0066: case 0067:
	case 0070: case 0071: case 0072: case 0073:
	case 0074: case 0075: case 0076: case 0077:
	    MA = ((IR | 0177760) + BR) & VAMASK;	/* IR<3:0> = -offset */
	    WriteW (MA, AR);				/* store AR */
	    break;
	IOP_SAIP:  case 0101: case 0102: case 0103:	/* IOP SAI+ (I_NO) */
	case 0104: case 0105: case 0106: case 0107:
	case 0110: case 0111: case 0112: case 0113:
	case 0114: case 0115: case 0116: case 0117:
	    MA = ((IR & 017) + BR) & VAMASK;		/* IR<3:0> = +offset */
	    WriteW (MA, AR);				/* store AR */
	    break;
	case 0150:					/* IOP CRC (I_CN) */
	case 0460:					/* IOPX CRC (I_CN) */
	    t = (AR & 0xFF) ^ wc;			/* start CRC */
	    for (i = 0; i < 8; i++) {			/* apply polynomial */
		t = (t >> 1) | ((t & 1) << 15);		/* rotate right */
	    if (t & SIGN) t = t ^ 020001;  }		/* old t<0>? xor */
	    WriteW (awc, t);				/* rewrite CRC */
	    break;
	case 0160:					/* IOP TRSLT (I_CN) */
	case 0467:					/* IOPX TRSLT (I_CN) */
	    if (wc & SIGN) break;			/* cnt < 0? */
	    while (wc != 0) {				/* loop */
		MA = (AR + AR + ReadB (BR)) & VAMASK;
		t = ReadB (MA);				/* xlate */
		WriteB (BR, t);				/* store char */
		BR = (BR + 1) & DMASK;			/* incr ptr */
		wc = (wc - 1) & DMASK;			/* decr cnt */
		if (wc && intrq) {			/* more and intr? */
		    WriteW (awc, wc);			/* rewrite wc */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0220:					/* IOP READF (I_NO) */
	case 0462:					/* IOPX READF (I_NO) */
	    AR = iop_sp;				/* copy stk ptr */
	    break;
	case 0221:					/* IOP PRFIO (I_NO) */
	case 0473:					/* IOPX PFRIO (I_NO) */
	    t = ReadW (PC);				/* get IO instr */
	    PC = (PC + 1) & VAMASK;
	    WriteW (PC, 1);				/* set flag */
	    PC = (PC + 1) & VAMASK;
	    reason = iogrp (t, 0);			/* execute instr */
	    break;
	case 0222:					/* IOP PRFEI (I_NO) */
	case 0471:					/* IOPX PFREI (I_NO) */
	    t = ReadW (PC);				/* get IO instr */
	    PC = (PC + 1) & VAMASK;
	    WriteW (PC, 1);				/* set flag */
	    PC = (PC + 1) & VAMASK;
	    reason = iogrp (t, 0);			/* execute instr */
							/* fall through */
	case 0223:					/* IOP PRFEX (I_NO) */
	case 0472:					/* IOPX PFREX (I_NO) */
	    MA = ReadW (PC);				/* exit addr */
	    PCQ_ENTRY;
	    PC = ReadW (MA) & VAMASK;			/* jump indirect */
	    WriteW (MA, 0);				/* clear exit */
	    break;
	case 0240:					/* IOP ENQ (I_NO) */
	case 0464:					/* IOPX ENQ (I_NO) */
	    hp = ReadW (AR & VAMASK);			/* addr of head */
	    tp = ReadW ((AR + 1) & VAMASK);		/* addr of tail */
	    WriteW ((BR - 1) & VAMASK, 0);		/* entry link */
	    WriteW ((tp - 1) & VAMASK, BR);		/* tail link */
	    WriteW ((AR + 1) & VAMASK, BR);		/* queue tail */
	    if (hp != 0) PC = (PC + 1) & VAMASK;	/* q not empty? skip */
	    break;
	case 0257:					/* IOP PENQ (I_NO) */
	case 0465:					/* IOPX PENQ (I_NO) */
	    hp = ReadW (AR & VAMASK);			/* addr of head */
	    WriteW ((BR - 1) & VAMASK, hp);		/* becomes entry link */
	    WriteW (AR & VAMASK, BR);			/* queue head */
	    if (hp == 0)				/* q empty? */
		WriteW ((AR + 1) & VAMASK, BR);		/* queue tail */
	    else PC = (PC + 1) & VAMASK;		/* skip */
	    break;
	case 0260:					/* IOP DEQ (I_NO) */
	case 0466:					/* IOPX DEQ (I_NO) */
	    BR = ReadW (AR & VAMASK);			/* addr of head */
	    if (BR) {					/* queue not empty? */
		hp = ReadW ((BR - 1) & VAMASK);		/* read hd entry link */
		WriteW (AR & VAMASK, hp);		/* becomes queue head */
		if (hp == 0)				/* q now empty? */
		    WriteW ((AR + 1) & VAMASK, (AR + 1) & DMASK);
		PC = (PC + 1) & VAMASK;  }		/* skip */
	    break;
	case 0300:					/* IOP SBYTE (I_NO) */
	    WriteB (BR, AR);				/* store byte */
	    BR = (BR + 1) & DMASK;			/* incr ptr */
	    break;
	case 0320:					/* IOP LBYTE (I_NO) */
	    AR = ReadB (BR);				/* load byte */
	    BR = (BR + 1) & DMASK;			/* incr ptr */
	    break;
	case 0340:					/* IOP REST (I_NO) */
	case 0461:					/* IOPX REST (I_NO) */
	    iop_sp = (iop_sp - 1) & VAMASK;		/* pop E/~O,BR,AR */
	    t = ReadW (iop_sp);
	    O = ((t >> 1) ^ 1) & 1;
	    E = t & 1;
	    iop_sp = (iop_sp - 1) & VAMASK;
	    BR = ReadW (iop_sp);
	    iop_sp = (iop_sp - 1) & VAMASK;
	    AR = ReadW (iop_sp);
	    if (cpu_unit.flags & UNIT_2100) mp_fence = iop_sp;
	    break;
	case 0362:					/* IOP SAVE (I_NO) */
	case 0474:					/* IOPX SAVE (I_NO) */
	    WriteW (iop_sp, AR);			/* push AR,BR,E/~O */
	    iop_sp = (iop_sp + 1) & VAMASK;
	    WriteW (iop_sp, BR);
	    iop_sp = (iop_sp + 1) & VAMASK;
	    t = ((O ^ 1) << 1) | E;
	    WriteW (iop_sp, t);
	    iop_sp = (iop_sp + 1) & VAMASK;
	    if (cpu_unit.flags & UNIT_2100) mp_fence = iop_sp;
	    break;

	case 0400: case 0401: case 0402: case 0403:	/* IOPX LAI-/SAI- (I_NO) */
	case 0404: case 0405: case 0406: case 0407:
	case 0410: case 0411: case 0412: case 0413:
	case 0414: case 0415: case 0416: case 0417:
	    MA = ((IR | 0177760) + BR) & VAMASK;	/* IR<3:0> = -offset */
	    if (IR & I_AB) AR = ReadW (MA);		/* AB = 1? LAI */
	    else WriteW (MA, AR);			/* AB = 0? SAI */
	    break;
	case 0420: case 0421: case 0422: case 0423:	/* IOPX LAI+/SAI+ (I_NO) */
	case 0424: case 0425: case 0426: case 0427:
	case 0430: case 0431: case 0432: case 0433:
	case 0434: case 0435: case 0436: case 0437:
	    MA = ((IR & 017) + BR) & VAMASK;		/* IR<3:0> = +offset */
	    if (IR & I_AB) AR = ReadW (MA);		/* AB = 1? LAI */
	    else WriteW (MA, AR);			/* AB = 0? SAI */
	    break;
	case 0463:					/* IOPX INS (I_NO) */
	    iop_sp = AR;				/* init stk ptr */
	    break;
	case 0470:					/* IOPX ILIST (I_CN) */
	IOP_ILIST:
	    do {					/* for count */
		WriteW (MA, AR);			/* write AR to mem */
		AR = (AR + 1) & DMASK;			/* incr AR */
		MA = (MA + 1) & VAMASK;			/* incr MA */
		wc = (wc - 1) & DMASK;  }		/* decr count */
	    while (wc != 0);
	    break;

/* DMS instructions, move alternate - interruptible

   DMS privilege violation rules are
   - load map and CTL set (XMM, XMS, XM*, SY*, US*, PA*, PB*)
   - load state or fence and UMAP set (JRS, DJP, DJS, SJP, SJS, UJP, UJS, LF*)
   
   The 21MX manual is incorrect in stating that M*I, M*W, XS* are privileged */

	case 0701:					/* self test */
	    ABREG[absel] = ABREG[absel] ^ DMASK;	/* CMA or CMB */
	    break;
	case 0702:					/* MBI (E_NO) */
	    AR = AR & ~1;				/* force A, B even */
	    BR = BR & ~1;
	    while (XR != 0) {				/* loop */
		t = ReadB (AR);				/* read curr */
		WriteBA (BR, t);			/* write alt */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0703:					/* MBF (E_NO) */
	    AR = AR & ~1;				/* force A, B even */
	    BR = BR & ~1;
	    while (XR != 0) {				/* loop */
		t = ReadBA (AR);			/* read alt */
		WriteB (BR, t);				/* write curr */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0704:					/* MBW (E_NO) */
	    AR = AR & ~1;				/* force A, B even */
	    BR = BR & ~1;
	    while (XR != 0) {				/* loop */
		t = ReadBA (AR);			/* read alt */
		WriteBA (BR, t);			/* write alt */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0705:					/* MWI (E_NO) */
	    while (XR != 0) {				/* loop */
		t = ReadW (AR & VAMASK);		/* read curr */
		WriteWA (BR & VAMASK, t);		/* write alt */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq) {			/* more and intr? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0706:					/* MWF (E_NO) */
	    while (XR != 0) {				/* loop */
		t = ReadWA (AR & VAMASK);		/* read alt */
		WriteW (BR & VAMASK, t);		/* write curr */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq) {			/* more and intr? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0707:					/* MWW (E_NO) */
	    while (XR != 0) {				/* loop */
		t = ReadWA (AR & VAMASK);		/* read alt */
		WriteWA (BR & VAMASK, t);		/* write alt */
		AR = (AR + 1) & DMASK;			/* incr ptrs */
		BR = (BR + 1) & DMASK;
		XR = (XR - 1) & DMASK;
		if (XR && intrq) {			/* more and intr? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;

/* DMS, continued */

	case 0710:					/* SYA, SYB (E_NO) */
	case 0711:					/* USA, USB (E_NO) */
	case 0712:					/* PAA, PAB (E_NO) */
	case 0713:					/* PBA, PBB (E_NO) */
	    mapi = (IR & 03) << VA_N_PAG;		/* map base */
	    if (ABREG[absel] & SIGN) {			/* store? */
		for (i = 0; i < MAP_LNT; i++) {
		    t = dms_rmap (mapi + i);		/* map to memory */
		    WriteW ((ABREG[absel] + i) & VAMASK, t);  }  }
	    else {					/* load */
		dms_viol (err_PC, MVI_PRV);		/* priv if PRO */
		for (i = 0; i < MAP_LNT; i++) {
		    t = ReadW ((ABREG[absel] + i) & VAMASK);
		    dms_wmap (mapi + i, t);   }  }	/* mem to map */
	    ABREG[absel] = (ABREG[absel] + MAP_LNT) & DMASK;
	    break;
	case 0714:					/* SSM (E_AD) */
	    WriteW (MA, dms_upd_sr ());			/* store stat */
	    break;
	case 0715:					/* JRS (E_AA) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    t = ReadW (MA);				/* get status */
	    dms_enb = 0;				/* assume off */
	    dms_ump = SMAP;
	    if (t & 0100000) {				/* set enable? */
		dms_enb = 1;
		if (t & 0040000) dms_ump = UMAP;  }	/* set/clr usr */
	    mp_dms_jmp (M1);				/* mpck jmp target */
	    PCQ_ENTRY;					/* save old PC */
	    PC = M1;					/* jump */
	    ion_defer = 1;				/* defer intr */
	    break;

/* DMS, continued */

	case 0700: case 0720:				/* XMM (E_NO) */
	    if (XR == 0) break;				/* nop? */
	    while (XR != 0) {				/* loop */
		if (XR & SIGN) {			/* store? */
		    t = dms_rmap (AR);			/* map to mem */
		    WriteW (BR & VAMASK, t);
		    XR = (XR + 1) & DMASK;  }
		else {					/* load */
		    dms_viol (err_PC, MVI_PRV);		/* priv if PRO */
		    t = ReadW (BR & VAMASK);		/* mem to map */
		    dms_wmap (AR, t);
		    XR = (XR - 1) & DMASK;  }
		AR = (AR + 1) & DMASK;
		BR = (BR + 1) & DMASK;
		if (intrq && ((XR & 0xF) == 0xF)) {	/* intr, cnt4 = F? */
		    PC = err_PC;			/* stop for now */
		    break;  }  }
	    break;
	case 0721:					/* XMS (E_NO) */
	    if ((XR & SIGN) || (XR == 0)) break;	/* nop? */
	    dms_viol (err_PC, MVI_PRV);			/* priv if PRO */
	    while (XR != 0) {
		dms_wmap (AR, BR);			/* AR to map */
		XR = (XR - 1) & DMASK;
		AR = (AR + 1) & DMASK;
		BR = (BR + 1) & DMASK;
		if (intrq && ((XR & 0xF) == 0xF)) {	/* intr, cnt4 = F? */
		    PC = err_PC;
		    break;  }  }
	    break;
	case 0722:					/* XMA, XMB (E_NO) */
	    dms_viol (err_PC, MVI_PRV);			/* priv if PRO */
	    if (ABREG[absel] & 0100000) mapi = UMAP;
	    else mapi = SMAP;
	    if (ABREG[absel] & 0000001) mapj = PBMAP;
	    else mapj = PAMAP;
	    for (i = 0; i < MAP_LNT; i++) {
		t = dms_rmap (mapi + i);		/* read map */
		dms_wmap (mapj + i, t);  }		/* write map */
	    break;
	case 0724:					/* XLA, XLB (E_AD) */
	    ABREG[absel] = ReadWA (MA);			/* load alt */
	    break;
	case 0725:					/* XSA, XSB (E_AD) */
	    WriteWA (MA, ABREG[absel]);			/* store alt */
	    break;
	case 0726:					/* XCA, XCB (E_AD) */
	    if (ABREG[absel] != ReadWA (MA))		/* compare alt */
		PC = (PC + 1) & VAMASK;
	    break;
	case 0727:					/* LFA, LFB (E_NO) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    dms_sr = (dms_sr & ~(MST_FLT | MST_FENCE)) |
		(ABREG[absel] & (MST_FLT | MST_FENCE));
	    break;

/* DMS, continued */

	case 0730:					/* RSA, RSB (E_NO) */
	    ABREG[absel] = dms_upd_sr ();		/* save stat */
	    break;
	case 0731:					/* RVA, RVB (E_NO) */
	    ABREG[absel] = dms_vr;			/* save viol */
	    break;
	case 0732:					/* DJP (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    mp_dms_jmp (MA);				/* validate jump addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = MA;					/* new PC */
	    dms_enb = 0;				/* disable map */
	    dms_ump = SMAP;
	    ion_defer = 1;
	    break;
	case 0733:					/* DJS (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    WriteW (MA, PC);				/* store ret addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = (MA + 1) & VAMASK;			/* new PC */
	    dms_enb = 0;				/* disable map */
	    dms_ump = SMAP;
	    ion_defer = 1;				/* defer intr */
	    break;
	case 0734:					/* SJP (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    mp_dms_jmp (MA);				/* validate jump addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = MA;					/* jump */
	    dms_enb = 1;				/* enable system */
	    dms_ump = SMAP;
	    ion_defer = 1;				/* defer intr */
	    break;
	case 0735:					/* SJS (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    t = PC;					/* save retn addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = (MA + 1) & VAMASK;			/* new PC */
	    dms_enb = 1;				/* enable system */
	    dms_ump = SMAP;
	    WriteW (MA, t);				/* store ret addr */
	    ion_defer = 1;				/* defer intr */
	    break;
	case 0736:					/* UJP (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    mp_dms_jmp (MA);				/* validate jump addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = MA;					/* jump */
	    dms_enb = 1;				/* enable user */
	    dms_ump = UMAP;
	    ion_defer = 1;				/* defer intr */
	    break;
	case 0737:					/* UJS (E_AD) */
	    if (dms_ump) dms_viol (err_PC, MVI_PRV);
	    t = PC;					/* save retn addr */
	    PCQ_ENTRY;					/* save curr PC */
	    PC = (MA + 1) & VAMASK;			/* new PC */
	    dms_enb = 1;				/* enable user */
	    dms_ump = UMAP;
	    WriteW (MA, t);				/* store ret addr */
	    ion_defer = 1;				/* defer intr */
	    break;

/* Index register instructions */

	case 0740:					/* SAX, SBX (E_AD) */
	    MA = (MA + XR) & VAMASK;			/* indexed addr */
	    WriteW (MA, ABREG[absel]);			/* store */
	    break;
	case 0741:					/* CAX, CBX (E_NO) */
	    XR = ABREG[absel];				/* copy to XR */
	    break;
	case 0742:					/* LAX, LBX (E_AD) */
	    MA = (MA + XR) & VAMASK;			/* indexed addr */
	    ABREG[absel] = ReadW (MA);			/* load */
	    break;
	case 0743:					/* STX (E_AD) */
	    WriteW (MA, XR);				/* store XR */
	    break;
	case 0744:					/* CXA, CXB (E_NO) */
	    ABREG[absel] = XR;				/* copy from XR */
	    break;
	case 0745:					/* LDX  (E_AD)*/
	    XR = ReadW (MA);				/* load XR */
	    break;
	case 0746:					/* ADX (E_AD) */
	    v1 = ReadW (MA);				/* add to XR */
	    t = XR + v1;
	    if (t > DMASK) E = 1;			/* set E, O */
	    if (((~XR ^ v1) & (XR ^ t)) & SIGN) O = 1;
	    XR = t & DMASK;
	    break;
	case 0747:					/* XAX, XBX (E_NO) */
	    t = XR;					/* exchange XR */
	    XR = ABREG[absel];
	    ABREG[absel] = t;
	    break;
	case 0750:					/* SAY, SBY (E_AD) */
	    MA = (MA + YR) & VAMASK;			/* indexed addr */
	    WriteW (MA, ABREG[absel]);			/* store */
	    break;
	case 0751:					/* CAY, CBY (E_NO) */
	    YR = ABREG[absel];				/* copy to YR */
	    break;
	case 0752:					/* LAY, LBY (E_AD) */
	    MA = (MA + YR) & VAMASK;			/* indexed addr */
	    ABREG[absel] = ReadW (MA);			/* load */
	    break;
	case 0753:					/* STY (E_AD) */
	    WriteW (MA, YR);				/* store YR */
	    break;
	case 0754:					/* CYA, CYB (E_NO) */
	    ABREG[absel] = YR;				/* copy from YR */
	    break;
	case 0755:					/* LDY (E_AD) */
	    YR = ReadW (MA);				/* load YR */
	    break;
	case 0756:					/* ADY (E_AD) */
	    v1 = ReadW (MA);				/* add to YR */
	    t = YR + v1;
	    if (t > DMASK) E = 1;			/* set E, O */
	    if (((~YR ^ v1) & (YR ^ t)) & SIGN) O = 1;
	    YR = t & DMASK;
	    break;
	case 0757:					/* XAY, XBY (E_NO) */
	    t = YR;					/* exchange YR */
	    YR = ABREG[absel];
	    ABREG[absel] = t;
	    break;
	case 0760:					/* ISX (E_NO) */
	    XR = (XR + 1) & DMASK;			/* incr XR */
	    if (XR == 0) PC = (PC + 1) & VAMASK;	/* skip if zero */
	    break;
	case 0761:					/* DSX (E_NO) */
	    XR = (XR - 1) & DMASK;			/* decr XR */
	    if (XR == 0) PC = (PC + 1) & VAMASK;	/* skip if zero */
	    break;
	case 0762:					/* JLY (E_AD) */
	    mp_dms_jmp (MA);				/* validate jump addr */
	    PCQ_ENTRY;
	    YR = PC;					/* ret addr to YR */
	    PC = MA;					/* jump */
	    break;
	case 0770:					/* ISY (E_NO) */
	    YR = (YR + 1) & DMASK;			/* incr YR */
	    if (YR == 0) PC = (PC + 1) & VAMASK;	/* skip if zero */
	    break;
	case 0771:					/* DSY (E_NO) */
	    YR = (YR - 1) & DMASK;			/* decr YR */
	    if (YR == 0) PC = (PC + 1) & VAMASK;	/* skip if zero */
	    break;
	case 0772:					/* JPY (E_NO) */
	    MA = (ReadW (PC) + YR) & VAMASK; 		/* index, no indir */
	    PC = (PC + 1) & VAMASK;
	    mp_dms_jmp (MA);				/* validate jump addr */
	    PCQ_ENTRY;
	    PC = MA;					/* jump */
	    break;

/* Byte instructions */

	case 0763:					/* LBT (E_NO) */
	    AR = ReadB (BR);				/* load byte */
	    BR = (BR + 1) & DMASK;			/* incr ptr */
	    break;
	case 0764:					/* SBT (E_NO) */
	    WriteB (BR, AR);				/* store byte */
	    BR = (BR + 1) & DMASK;			/* incr ptr */
	    break;
	IOP_MBYTE:					/* IOP MBYTE (I_AZ) */
	    if (wc & SIGN) break;			/* must be positive */
	case 0765:					/* MBT (E_AZ) */
	    while (wc != 0) {				/* while count */
		WriteW (awc, wc);			/* for abort */
		t = ReadB (AR);				/* move byte */
		WriteB (BR, t);
		AR = (AR + 1) & DMASK;			/* incr src */
		BR = (BR + 1) & DMASK;			/* incr dst */
		wc = (wc - 1) & DMASK;			/* decr cnt */
		if (intrq && wc) {			/* intr, more to do? */
		    PC = err_PC;			/* back up PC */
		    break;  }  }			/* take intr */
	    WriteW (awc, wc);				/* clean up inline */
	    break;
	case 0766:					/* CBT (E_AZ) */
	    while (wc != 0) {				/* while count */
		WriteW (awc, wc);			/* for abort */
		v1 = ReadB (AR);			/* get src1 */
		v2 = ReadB (BR);			/* get src2 */
		if (v1 != v2) {				/* compare */
		    PC = (PC + 1 + (v1 > v2)) & VAMASK;
		    BR = (BR + wc) & DMASK;		/* update BR */
		    wc = 0;				/* clr interim */
		    break;  }
		AR = (AR + 1) & DMASK;			/* incr src1 */
		BR = (BR + 1) & DMASK;			/* incr src2 */
		wc = (wc - 1) & DMASK;			/* decr cnt */
		if (intrq && wc) {			/* intr, more to do? */
		    PC = err_PC;			/* back up PC */
		    break;  }  }			/* take intr */
	    WriteW (awc, wc);				/* clean up inline */
	    break;
	case 0767:					/* SFB (E_NO) */
	    v1 = AR & 0377;				/* test byte */
	    v2 = (AR >> 8) & 0377;			/* term byte */
	    for (;;) {					/* scan */
		t = ReadB (BR);				/* read byte */
		if (t == v1) break;			/* test match? */
		BR = (BR + 1) & DMASK;
		if (t == v2) {				/* term match? */
		    PC = (PC + 1) & VAMASK;
		    break;  }
		if (intrq) {				/* int pending? */
		    PC = err_PC;			/* back up PC */
		    break;  }  }			/* take intr */
	    break;

/* Bit, word instructions */

	case 0773:					/* SBS (E_AA) */
	    v1 = ReadW (MA);
	    v2 = ReadW (M1);
	    WriteW (M1, v2 | v1);			/* set bit */
	    break;
	case 0774:					/* CBS (E_AA) */
	    v1 = ReadW (MA);
	    v2 = ReadW (M1);
	    WriteW (M1, v2 & ~v1);			/* clear bit */
	    break;
	case 0775:					/* TBS (E_AA) */
	    v1 = ReadW (MA);
	    v2 = ReadW (M1);
	    if ((v2 & v1) != v1)			/* test bits */
	        PC = (PC + 1) & VAMASK;
	    break;
	case 0776:					/* CMW (E_AZ) */
	    while (wc != 0) {				/* while count */
		WriteW (awc, wc);			/* for abort */
		v1 = ReadW (AR & VAMASK);		/* first op */
		v2 = ReadW (BR & VAMASK);		/* second op */
		sop1 = (int32) SEXT (v1);		/* signed */
		sop2 = (int32) SEXT (v2);
		if (sop1 != sop2) {			/* compare */
		    PC = (PC + 1 + (sop1 > sop2)) & VAMASK;
		    BR = (BR + wc) & DMASK;		/* update BR */
		    wc = 0;				/* clr interim */
		    break;  }
		AR = (AR + 1) & DMASK;			/* incr src1 */
		BR = (BR + 1) & DMASK;			/* incr src2 */
		wc = (wc - 1) & DMASK;			/* decr cnt */
		if (intrq && wc) {			/* intr, more to do? */
		    PC = err_PC;			/* back up PC */
		    break;  }  }			/* take intr */
	    WriteW (awc, wc);				/* clean up inline */
	    break;
	case 0200:					/* IOP WMOVE (I_AZ) */
	    if (wc & SIGN) break;			/* must be positive */
	case 0777:					/* MVW (E_AZ) */
	    while (wc != 0) {				/* while count */
		WriteW (awc, wc);			/* for abort */
		t = ReadW (AR & VAMASK);		/* move word */
		WriteW (BR & VAMASK, t);
		AR = (AR + 1) & DMASK;			/* incr src */
		BR = (BR + 1) & DMASK;			/* incr dst */
		wc = (wc - 1) & DMASK;			/* decr cnt */
		if (intrq && wc) {			/* intr, more to do? */
		    PC = err_PC;			/* back up PC */
		    break;  }  }			/* take intr */
	    WriteW (awc, wc);				/* clean up inline */
	    break;
	default:					/* all others NOP */
	    break;  }					/* end case ext */
return reason;
}
