/* hp2100_cpu1.c: HP 2100 EAU and UIG simulator

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

   CPU1		Extended arithmetic and optional microcode instructions

   22-Feb-05	JDB	Fixed missing MPCK on JRS target
			Removed EXECUTE instruction (is NOP in actual microcode)
   18-Feb-05	JDB	Add 2100/21MX Fast FORTRAN Processor instructions
   21-Jan-05	JDB	Reorganized CPU option and operand processing flags
			Split code along microcode modules
   15-Jan-05	RMS	Cloned from hp2100_cpu.c

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
	(5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
	(92851-90001, Mar-1981)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.

   This source file contains the Extended Arithmetic Unit and various optional
   User Instruction Group (a.k.a. "Macro") instruction sets for the 2100 and
   21MX CPUs.  Unit flags indicate which options are present in the current
   system.

   The microcode address space of the 2100 encompassed four modules of 256 words
   each.  The 21MX M-series expanded that to sixteen modules, and the 21MX
   E-series expanded that still further to sixty-four modules.  Each CPU had its
   own microinstruction set, although the micromachines of the various 21MX
   models were similar internally.

   Regarding option instruction sets, there was some commonality across CPU
   types.  EAU instructions were identical across all models, and the floating
   point set was the same on the 2100 and 21MX.  Other options implemented
   proper instruction supersets (e.g., the Fast FORTRAN Processor from 2100 to
   21MX-M to 21MX-E to 21MX-F) or functional equivalence with differing code
   points (the 2000 I/O Processor from 2100 to 21MX).

   The 2100 decoded the EAU and UIG sets separately in hardware and supported
   only the UIG 0 code points.  Bits 7-4 of a UIG instruction decoded one of
   sixteen entry points in the lowest-numbered module after module 0.  Those
   entry points could be used directly (as for the floating-point instructions),
   or additional decoding based on bits 3-0 could be implemented.

   The 21MX generalized the instruction decoding to a series of microcoded
   jumps, based on the bits in the instruction.  Bits 15-8 indicated the group
   of the current instruction: EAU (200, 201, 202, 210, and 211), UIG 0 (212),
   or UIG 1 (203 and 213).  UIG 0, UIG 1, and some EAU instructions were decoded
   further by selecting one of sixteen modules within the group via bits 7-4.
   Finally, each UIG module decoded up to sixteen instruction entry points via
   bits 3-0.  Jump tables for all firmware options were contained in the base
   set, so modules needed only to be concerned with decoding their individual
   entry points within the module.

   While the 2100 and 21MX hardware decoded these instruction sets differently,
   the decoding mechanism of the simulation follows that of the 21MX E-series.
   Where needed, CPU type- or model-specific behavior is simulated.

   The design of the 21MX microinstruction set was such that executing an
   instruction for which no microcode was present (e.g., executing a FFP
   instruction when the FFP firmware was not installed) resulted in a NOP.
   Under simulation, such execution causes an undefined instruction stop.
*/

#include <setjmp.h>
#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_fp1.h"

/* Operand processing encoding */

#define OP_NUL		0				/* no operand */
#define OP_CON		1				/* operand is a constant */
#define OP_VAR		2				/* operand is a variable */
#define OP_ADR		3				/* operand is an address */
#define OP_ADK		4				/* op is addr of 1-word const */
#define OP_ADF		5				/* op is addr of 2-word const */
#define OP_ADX		6				/* op is addr of 3-word const */
#define OP_ADT		7				/* op is addr of 4-word const */

#define OP_N_FLAGS	3				/* number of flag bits */
#define OP_M_FLAGS	((1 << OP_N_FLAGS) - 1)		/* mask for flag bits */

#define	OP_N_F		4				/* number of op fields */

#define OP_V_F1		(0 * OP_N_FLAGS)		/* 1st operand field */
#define OP_V_F2		(1 * OP_N_FLAGS)		/* 2nd operand field */
#define OP_V_F3		(2 * OP_N_FLAGS)		/* 3rd operand field */
#define OP_V_F4		(3 * OP_N_FLAGS)		/* 4th operand field */

/* Operand patterns */

#define OP_N		(OP_NUL)
#define OP_C		(OP_CON << OP_V_F1)
#define OP_V		(OP_VAR << OP_V_F1)
#define OP_A		(OP_ADR << OP_V_F1)
#define OP_K		(OP_ADK << OP_V_F1)
#define OP_F		(OP_ADF << OP_V_F1)
#define OP_CV		((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_AC		((OP_ADR << OP_V_F1) | (OP_CON << OP_V_F2))
#define OP_AA		((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_AK		((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_AX		((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2))
#define OP_KV		((OP_ADK << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_KA		((OP_ADK << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_KK		((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_CVA		((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2) | \
			 (OP_ADR << OP_V_F3))
#define OP_AAF		((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
			 (OP_ADF << OP_V_F3))
#define OP_AAX		((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
			 (OP_ADX << OP_V_F3))
#define OP_AXX		((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2) | \
			 (OP_ADX << OP_V_F3))
#define OP_AAXX		((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
			 (OP_ADX << OP_V_F3) | (OP_ADX << OP_V_F4))
#define OP_KKKK		((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2) | \
			 (OP_ADK << OP_V_F3) | (OP_ADK << OP_V_F4))

typedef uint32 OP_PAT;					/* operand pattern */
typedef uint32 OPS[OP_N_F * 2];				/* operand array */

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
extern UNIT cpu_unit;

t_stat cpu_eau (uint32 IR, uint32 intrq);		/* EAU group handler */
t_stat cpu_uig_0 (uint32 IR, uint32 intrq);		/* UIG group 0 handler */
t_stat cpu_uig_1 (uint32 IR, uint32 intrq);		/* UIG group 1 handler */

static t_stat cpu_fp (uint32 IR, uint32 intrq);		/* Floating-point */
static t_stat cpu_ffp (uint32 IR, uint32 intrq);	/* Fast FORTRAN Processor */
static t_stat cpu_iop (uint32 IR, uint32 intrq);	/* 2000 I/O Processor */
static t_stat cpu_dms (uint32 IR, uint32 intrq);	/* Dynamic mapping system */
static t_stat cpu_eig (uint32 IR, uint32 intrq);	/* Extended instruction group */
static t_stat get_ops (OP_PAT pattern, OPS op, uint32 irq);	/* operand processor */

extern uint32 f_as (uint32 op, t_bool sub);		/* FAD/FSB */
extern uint32 f_mul (uint32 op);			/* FMP */
extern uint32 f_div (uint32 op);			/* FDV */
extern uint32 f_fix (void);				/* FIX */
extern uint32 f_flt (void);				/* FLT */
extern uint32 f_pack (int32 expon);			/* .PACK helper */
extern void f_unpack (void);				/* .FLUN helper */
extern void f_pwr2 (int32 n);				/* .PWR2 helper */

/* EAU

   The Extended Arithmetic Unit (EAU) adds ten instructions with double-word
   operands, including multiply, divide, shifts, and rotates.  Option
   implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
     12579A   std     std     std     std

   The instruction codes are mapped to routines as follows:

     Instr.    Bits
      Code   15-8 7-4	2116	2100   21MX-M  21MX-E  21MX-F  Note
     ------  ---- ---  ------  ------  ------  ------  ------  ---------------------
     100000   200  00				DIAG	DIAG   Unsupported
     100020   200  01	ASL	ASL	ASL	ASL	ASL    Bits 3-0 encode shift
     100040   200  02	LSL	LSL	LSL	LSL	LSL    Bits 3-0 encode shift
     100060   200  03			       TIMER   TIMER   Unsupported
     100100   200  04	RRL	RRL	RRL	RRL	RRL    Bits 3-0 encode shift
     100200   200  10	MPY	MPY	MPY	MPY	MPY
     100400   201  xx	DIV	DIV	DIV	DIV	DIV
     101020   202  01	ASR	ASR	ASR	ASR	ASR    Bits 3-0 encode shift
     101040   202  02	LSR	LSR	LSR	LSR	LSR    Bits 3-0 encode shift
     101100   202  04	RRR	RRR	RRR	RRR	RRR    Bits 3-0 encode shift
     104200   210  xx	DLD	DLD	DLD	DLD	DLD
     104400   211  xx	DST	DST	DST	DST	DST

   The remaining codes for bits 7-4 are undefined and will cause a simulator
   stop if enabled.  On a real 21MX-M, all undefined instructions in the 200
   group decode as MPY, and all in the 202 group decode as NOP.  On a real
   21MX-E, instruction patterns 200/05 through 200/07 and 202/03 decode as NOP;
   all others cause erroneous execution.

   EAU instruction decoding on the 21MX M-series is convoluted.  The JEAU
   microorder maps IR bits 11, 9-7 and 5-4 to bits 2-0 of the microcode jump
   address.  The map is detailed on page IC-84 of the ERD.
	
   The 21MX E/F-series add two undocumented instructions to the 200 group:
   TIMER and DIAG.  These are described in the ERD on page IA 5-5, paragraph
   5-7.  The M-series executes these as MPY and RRL, respectively.  A third
   instruction, EXECUTE (100120), is also described but was never implemented,
   and the E/F-series microcode execute a NOP for this instruction code.

   Under simulation, TIMER, DIAG, and EXECUTE cause undefined instruction stops
   if the CPU is set to 2100 or 2116.  DIAG and EXECUTE also cause stops on the
   21MX-M.  TIMER does not, because it is used by several HP programs to
   differentiate between M- and E/F-series machines.
*/

t_stat cpu_eau (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 rs, qs, sc, v1, v2, t;
int32 sop1, sop2;

if ((cpu_unit.flags & UNIT_EAU) == 0) return stop_inst;	/* implemented? */

switch ((IR >> 8) & 0377) {				/* decode IR<15:8> */

case 0200:						/* EAU group 0 */
	switch ((IR >> 4) & 017) {			/* decode IR<7:4> */

	case 000:					/* DIAG 100000 */
	    if (UNIT_CPU_MODEL != UNIT_21MX_E)		/* must be 21MX-E */
		return stop_inst;			/* trap if not */
	    break;					/* DIAG is NOP unless halted */

	case 001:					/* ASL 100020-100037 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    O = 0;					/* clear ovflo */
	    while (sc-- != 0) {				/* bit by bit */
		t = BR << 1;				/* shift B */
		BR = (BR & SIGN) | (t & 077777) | (AR >> 15);
		AR = (AR << 1) & DMASK;
		if ((BR ^ t) & SIGN) O = 1;  }
	    break;

	case 002:					/* LSL 100040-100057 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
	    AR = (AR << sc) & DMASK;			/* BR'AR lsh left */
	    break;

	case 003:					/* TIMER 100060 */
	    if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)	/* must be 21MX */
		return stop_inst;			/* trap if not */
	    if (UNIT_CPU_MODEL == UNIT_21MX_M)		/* 21MX M-series? */
		goto MPY;				/* decode as MPY */
	    BR = (BR + 1) & DMASK;			/* increment B */
	    if (BR) PC = err_PC;			/* if !=0, repeat */
	    break;

	case 004:					/* RRL 100100-100117 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    t = BR;					/* BR'AR rot left */
	    BR = ((BR << sc) | (AR >> (16 - sc))) & DMASK;
	    AR = ((AR << sc) | (t >> (16 - sc))) & DMASK;
	    break;

	case 010:					/* MPY 100200 */
	MPY:
	    if (reason = get_ops (OP_K, op, intrq))	/* get operand */
		break;
	    sop1 = SEXT (AR);				/* sext AR */
	    sop2 = SEXT (op[0]);			/* sext mem */
	    sop1 = sop1 * sop2;				/* signed mpy */
	    BR = (sop1 >> 16) & DMASK;			/* to BR'AR */
	    AR = sop1 & DMASK;
	    O = 0;					/* no overflow */
	    break;

	default:					/* others undefined */
	    return stop_inst;
	    }
	
	break;

case 0201:						/* DIV 100400 */
	if (reason = get_ops (OP_K, op, intrq))		/* get operand */
	    break;
	if (rs = qs = BR & SIGN) {			/* save divd sign, neg? */
	    AR = (~AR + 1) & DMASK;			/* make B'A pos */
	    BR = (~BR + (AR == 0)) & DMASK;  }		/* make divd pos */
	v2 = op[0];					/* divr = mem */
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

case 0202:						/* EAU group 2 */
	switch ((IR >> 4) & 017) {			/* decode IR<7:4> */

	case 001:					/* ASR 101020-101037 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
	    BR = (SEXT (BR) >> sc) & DMASK;		/* BR'AR ash right */
	    O = 0;
	    break;
	
	case 002:					/* LSR 101040-101057 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    AR = ((BR << (16 - sc)) | (AR >> sc)) & DMASK;
	    BR = BR >> sc;				/* BR'AR log right */
	    break;
	
	case 004:					/* RRR 101100-101117 */
	    sc = (IR & 017)? (IR & 017): 16;		/* get sc */
	    t = AR;					/* BR'AR rot right */
	    AR = ((AR >> sc) | (BR << (16 - sc))) & DMASK;
	    BR = ((BR >> sc) | (t << (16 - sc))) & DMASK;
	    break;
	
	default:					/* others undefined */
	    return stop_inst;
	    }
	
	break;

case 0210:						/* DLD 104200 */
	if (reason = get_ops (OP_F, op, intrq))		/* get operand */
	    break;
	AR = (op[0] >> 16) & DMASK;			/* load AR */
	BR = op[0] & DMASK;				/* load BR */
	break;

case 0211:						/* DST 104400 */
	if (reason = get_ops (OP_A, op, intrq))		/* get operand */
	    break;
	WriteW (op[0], AR);				/* store AR */
	op[0] = (op[0] + 1) & VAMASK;
	WriteW (op[0], BR);				/* store BR */
	break;

default:						/* should never get here */
	return SCPE_IERR;
	}

return reason;
}

/* UIG 0

   The first User Instruction Group (UIG) encodes firmware options for the 2100
   and 21MX.  Instruction codes 105000-105377 are assigned to microcode options
   as follows:

     Instructions   Option Name			2100   21MX-M  21MX-E  21MX-F
     -------------  -------------------------  ------  ------  ------  ------
     105000-105362  2000 I/O Processor		opt	 -	 -	 -
     105000-105120  Floating Point		opt	std	std	std
     105200-105237  Fast FORTRAN Processor	opt	opt	opt	std
     105240-105257  RTE-IVA/B EMA		 -	 -	opt	opt
     105240-105257  RTE-6/VMA			 -	 -	opt	opt
     105300-105317  Distributed System		 -	 -	opt	opt
     105340-105357  RTE-6/VM Operating System	 -	 -	opt	opt

   Because the 2100 IOP microcode uses the same instruction range as the 2100 FP
   and FFP options, it cannot coexist with them.  To simplify simulation, the
   2100 IOP instructions are remapped to the equivalent 21MX instructions and
   dispatched to the UIG 1 module.

   Note that if the 2100 IOP is installed, the only valid UIG instructions are
   IOP instructions, as the IOP used the full 2100 microcode addressing space.
*/

t_stat cpu_uig_0 (uint32 IR, uint32 intrq)
{
if ((cpu_unit.flags & UNIT_IOP) && (UNIT_CPU_TYPE == UNIT_TYPE_2100)) {
	if ((IR >= 0105020) && (IR <= 0105057))		/* remap LAI */
	    IR = 0105400 | (IR - 0105020);
	else if ((IR >= 0105060) && (IR <= 0105117))	/* remap SAI */
	    IR = 0101400 | (IR - 0105060);
	else
	    switch (IR) {				/* remap others */
	    case 0105000:  IR = 0105470;  break;	/* ILIST */
	    case 0105120:  IR = 0105765;  break;	/* MBYTE (maps to MBT) */
	    case 0105150:  IR = 0105460;  break;	/* CRC   */
	    case 0105160:  IR = 0105467;  break;	/* TRSLT */
	    case 0105200:  IR = 0105777;  break;	/* MWORD (maps to MVW) */
	    case 0105220:  IR = 0105462;  break;	/* READF */
	    case 0105221:  IR = 0105473;  break;	/* PRFIO */
	    case 0105222:  IR = 0105471;  break;	/* PRFEI */
	    case 0105223:  IR = 0105472;  break;	/* PRFEX */
	    case 0105240:  IR = 0105464;  break;	/* ENQ   */
	    case 0105257:  IR = 0105465;  break;	/* PENQ  */
	    case 0105260:  IR = 0105466;  break;	/* DEQ   */
	    case 0105300:  IR = 0105764;  break;	/* SBYTE (maps to SBT) */
	    case 0105320:  IR = 0105763;  break;	/* LBYTE (maps to LBT) */
	    case 0105340:  IR = 0105461;  break;	/* REST  */
	    case 0105362:  IR = 0105474;  break;	/* SAVE  */

	    default:					/* all others invalid */
		return stop_inst;
	    }
	if (IR >= 0105700) return cpu_eig (IR, intrq);	/* dispatch to 21MX EIG */
	else return cpu_iop (IR, intrq);  }		/* or to 21MX IOP */

switch ((IR >> 4) & 017) {				/* decode IR<7:4> */

case 000:						/* 105000-105017 */
case 001:						/* 105020-105037 */
case 002:						/* 105040-105057 */
case 003:						/* 105060-105077 */
case 004:						/* 105100-105117 */
case 005:						/* 105120-105137 */
	return cpu_fp (IR, intrq);			/* Floating Point */

case 010:						/* 105200-105217 */
case 011:						/* 105220-105237 */
	return cpu_ffp (IR, intrq);			/* Fast FORTRAN Processor */
	}

return stop_inst;					/* others undefined */
}

/* UIG 1

   The second User Instruction Group (UIG) encodes firmware options for the
   21MX.  Instruction codes 101400-101777 and 105400-105777 are assigned to
   microcode options as follows ("x" is "1" or "5" below):

     Instructions   Option Name			21MX-M  21MX-E	21MX-F
     -------------  --------------------------  ------  ------	------
     10x400-10x437  2000 IOP			 opt	 opt	  -
     10x460-10x477  2000 IOP			 opt	 opt	  -
     10x700-10x737  Dynamic Mapping System	 opt	 opt	 std
     10x740-10x777  Extended Instruction Group	 std	 std	 std

   Only 21MX systems execute these instructions.
*/

t_stat cpu_uig_1 (uint32 IR, uint32 intrq)
{
if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)			/* 21MX execution? */
	return stop_inst;				/* no, so trap */

switch ((IR >> 4) & 017) {				/* decode IR<7:4> */

case 000:						/* 105400-105417 */
case 001:						/* 105420-105437 */
case 003:						/* 105460-105477 */
	return cpu_iop (IR, intrq);			/* 2000 I/O Processor */

case 014:						/* 105700-105717 */
case 015:						/* 105720-105737 */
	return cpu_dms (IR, intrq);			/* Dynamic Mapping System */

case 016:						/* 105740-105737 */
case 017:						/* 105760-105777 */
	return cpu_eig (IR, intrq);			/* Extended Instruction Group */
	}

return stop_inst;					/* others undefined */
}

/* Floating Point

   The 2100 and 21MX CPUs share the single-precision (two word) floating point
   instruction codes.  Option implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
      N/A    12901A   std     std     std

   The instruction codes are mapped to routines as follows:

     Instr.  2100/21MX-M/E/F
     ------  ---------------
     105000	  FAD
     105020	  FSB
     105040	  FMP
     105060	  FDV
     105100	  FIX
     105120	  FLT

   Bits 3-0 are not decoded by these instructions, so FAD (e.g.) would be
   executed by any instruction in the range 105000-105017.
*/

static const OP_PAT op_fp[6] = {
  OP_F,    OP_F,    OP_F,    OP_F,			/*  FAD    FSB    FMP    FDV  */
  OP_N,    OP_N	};					/*  FIX    FLT    ---    ---  */

static t_stat cpu_fp (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_FP) == 0)			/* FP option installed? */
	return stop_inst;

entry = (IR >> 4) & 017;				/* mask to entry point */

if (op_fp[entry] != OP_N)
	if (reason = get_ops (op_fp[entry], op, intrq))	/* get instruction operands */
	    return reason;

switch (entry) {					/* decode IR<7:4> */

case 000:						/* FMP 105000 */
	O = f_as (op[0], 0);				/* add, upd ovflo */
	break;

case 001:						/* FMP 105020 */
	O = f_as (op[0], 1);				/* sub, upd ovflo */
	break;

case 002:						/* FMP 105040 */
	O = f_mul (op[0]);				/* mul, upd ovflo */
	break;

case 003:						/* FDV 105060 */
	O = f_div (op[0]);				/* div, upd ovflo */
	break;

case 004:						/* FIX 105100 */
	O = f_fix ();					/* fix, upd ovflo */
	break;

case 005:						/* FLT 105120 */
	O = f_flt ();					/* float, upd ovflo */
	break;

default:						/* should be impossible */
	return SCPE_IERR;
	}

return reason;
}

/* Fast FORTRAN Processor

   The Fast FORTRAN Processor (FFP) is a set of FORTRAN language accelerators
   and extended-precision (three-word) floating point routines.  Although the
   FFP is an option for the 2100 and later CPUs, each implements the FFP in a
   slightly different form.

   Option implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
      N/A    12907A  12977B  13306B   std

   The instruction codes are mapped to routines as follows:

     Instr.   2100  21MX-M 21MX-E 21MX-F    Instr.   2100  21MX-M 21MX-E 21MX-F
     ------  ------ ------ ------ ------    ------  ------ ------ ------ ------
     105200    --     --     --   [test]    105220  .XFER  .XFER  .XFER  .XFER
     105201   DBLE   DBLE   DBLE   DBLE     105221  .GOTO  .GOTO  .GOTO  .GOTO
     105202   SNGL   SNGL   SNGL   SNGL     105222  ..MAP  ..MAP  ..MAP  ..MAP
     105203  .XMPY  .XMPY  .XMPY    --      105223  .ENTR  .ENTR  .ENTR  .ENTR
     105204  .XDIV  .XDIV  .XDIV    --      105224  .ENTP  .ENTP  .ENTP  .ENTP
     105205  .DFER  .DFER  .DFER  .DFER     105225    --   .PWR2  .PWR2  .PWR2
     105206    --   .XPAK  .XPAK  .XPAK     105226    --   .FLUN  .FLUN  .FLUN
     105207    --    XADD   XADD  .BLE      105227  $SETP  $SETP  $SETP  $SETP

     105210    --    XSUB   XSUB    --      105230    --   .PACK  .PACK  .PACK
     105211    --    XMPY   XMPY    --      105231    --     --   .CFER  .CFER
     105212    --    XDIV   XDIV    --      105232    --     --     --   ..FCM
     105213  .XADD  .XADD  .XADD    --      105233    --     --     --   ..TCM
     105214  .XSUB  .XSUB  .XSUB  .NGL      105234    --     --     --     --
     105215    --   .XCOM  .XCOM  .XCOM     105235    --     --     --     --
     105216    --   ..DCM  ..DCM  ..DCM     105236    --     --     --     --
     105217    --   DDINT  DDINT  DDINT     105237    --     --     --     --

   Notes:

     1. The "$SETP" instruction is sometimes listed as ".SETP" in the
	documentation.

     2. Extended-precision arithmetic routines (e.g., .XMPY) exist on the
	21MX-F, but they are assigned instruction codes in the single-precision
	floating-point module.

     3. The software implementation of ..MAP supports 1-, 2-, or 3-dimensional
	arrays, designated by setting A = -1, 0, and +1, respectively.  The
	firmware implementation supports only 2- and 3-dimensional access.
	
     4. The documentation for ..MAP for the 2100 FFP shows A = 0 or -1 for two
	or three dimensions, respectively, but the 21MX FFP shows A = 0 or +1.
	The firmware actually only checks the LSB of A.

     5. The .DFER and .XFER implementations for the 2100 FFP return X+4 and Y+4
	in the A and B registers, whereas the 21MX FFP returns X+3 and Y+3.

     6. The .XFER implementation for the 2100 FFP returns to P+2, whereas the
	21MX implementation returns to P+1.

   Additional references:
    - DOS/RTE Relocatable Library Reference Manual (24998-90001, Oct-1981)
    - Implementing the HP 2100 Fast FORTRAN Processor (12907-90010, Nov-1974)
*/

static const OP_PAT op_ffp[32] = {
  OP_N,    OP_AAF,  OP_AX,   OP_AXX,			/*  ---   DBLE   SNGL   .XMPY */
  OP_AXX,  OP_AA,   OP_A,    OP_AAXX,			/* .XDIV  .DFER  .XPAK  XADD  */
  OP_AAXX, OP_AAXX, OP_AAXX, OP_AXX,			/* XSUB   XMPY   XDIV   .XADD */
  OP_AXX,  OP_A,    OP_A,    OP_AAX,			/* .XSUB  .XCOM  ..DCM  DDINT */
  OP_N,    OP_AK,   OP_KKKK, OP_A,			/* .XFER  .GOTO  ..MAP  .ENTR */
  OP_A,    OP_K,    OP_N,    OP_K,			/* .ENTP  .PWR2  .FLUN  $SETP */
  OP_C,    OP_AA,   OP_N,    OP_N,			/* .PACK  .CFER   ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N };			/*  ---    ---    ---    ---  */

static t_stat cpu_ffp (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op, op2;
uint32 entry;
uint32 j, sa, sb, sc, da, dc, ra, MA;
int32 i;
XPN xop;

if ((cpu_unit.flags & UNIT_FFP) == 0)			/* FFP option installed? */
	return stop_inst;

entry = IR & 037;					/* mask to entry point */

if (op_ffp[entry] != OP_N)
	if (reason = get_ops (op_ffp[entry], op, intrq))/* get instruction operands */
	    return reason;

switch (entry) {					/* decode IR<3:0> */

/* FFP module 1 */

case 001:						/* DBLE 105201 (OP_AAF) */
	WriteW (op[1]++, (op[2] >> 16) & DMASK);	/* transfer high mantissa */
	WriteW (op[1]++, op[2] & 0177400);		/* convert low mantissa */
	WriteW (op[1], op[2] & 0377);			/* convert exponent */
	break;

case 002:						/* SNGL 105202 (OP_AX) */
	BR = op[2] >> 16;				/* move LSB and expon to B */
	f_unpack ();					/* unpack B into A/B */
	sa = AR;					/* save exponent */
	AR = (op[1] >> 16) & DMASK;			/* move MSB to A */
	BR = (op[1] & DMASK) | (BR != 0);		/* move mid to B with carry */
	O = f_pack (SEXT (sa));				/* pack into A/B */
	break;

#if defined (HAVE_INT64)

case 003:						/* .XMPY 105203 (OP_AXX) */
	i = 0;						/* params start at op[0] */
	goto XMPY;					/* process as XMPY */

case 004:						/* .XDIV 105204 (OP_AXX) */
	i = 0;						/* params start at op[0] */
	goto XDIV;					/* process as XDIV */

#endif

case 005:						/* .DFER 105205 (OP_AA) */
	BR = op[0];					/* get destination address */
	AR = op[1];					/* get source address */
	goto XFER;					/* do transfer */

#if defined (HAVE_INT64)

case 006:						/* .XPAK 105206 (OP_A) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	xop = ReadX (op[0]);				/* read unpacked */
	O = x_pak (&xop, xop, SEXT (AR));		/* pack mantissa, exponent */
	WriteX (op[0], xop);				/* write back */
	break;

case 007:						/* XADD 105207 (OP_AAXX) */
	i = 1;						/* params start at op[1] */
XADD:							/* enter here from .XADD */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	O = x_add (&xop, AS_XPN (op [i + 1]), AS_XPN (op [i + 3]));  /* add ops */
	WriteX (op[i], xop);				/* write sum */
	break;

case 010:						/* XSUB 105210 (OP_AAXX) */
	i = 1;						/* params start at op[1] */
XSUB:							/* enter here from .XSUB */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	O = x_sub (&xop, AS_XPN (op [i + 1]), AS_XPN (op [i + 3]));  /* subtract */
	WriteX (op[i], xop);				/* write difference */
	break;

case 011:						/* XMPY 105211 (OP_AAXX) */
	i = 1;						/* params start at op[1] */
XMPY:							/* enter here from .XMPY */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	O = x_mpy (&xop, AS_XPN (op [i + 1]), AS_XPN (op [i + 3]));  /* multiply */
	WriteX (op[i], xop);				/* write product */
	break;

case 012:						/* XDIV 105212 (OP_AAXX) */
	i = 1;						/* params start at op[1] */
XDIV:							/* enter here from .XDIV */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	O = x_div (&xop, AS_XPN (op [i + 1]), AS_XPN (op [i + 3]));  /* divide */
	WriteX (op[i], xop);				/* write quotient */
	break;

case 013:						/* .XADD 105213 (OP_AXX) */
	i = 0;						/* params start at op[0] */
	goto XADD;					/* process as XADD */

case 014:						/* .XSUB 105214 (OP_AXX) */
	i = 0;						/* params start at op[0] */
	goto XSUB;					/* process as XSUB */

case 015:						/* .XCOM 105215 (OP_A) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	xop = ReadX (op[0]);				/* read operand */
	AR = x_com (&xop);				/* neg and rtn exp adj */
	WriteX (op[0], xop);				/* write result */
	break;

case 016:						/* ..DCM 105216 (OP_A) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	xop = ReadX (op[0]);				/* read operand */
	O = x_dcm (&xop);				/* negate */
	WriteX (op[0], xop);				/* write result */
	break;

case 017:						/* DDINT 105217 (OP_AAX) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	if (intrq) {					/* interrupt pending? */
	    PC = err_PC;				/* restart instruction */
	    break;  }
	x_trun (&xop, AS_XPN (op [2]));			/* truncate operand */
	WriteX (op[1], xop);				/* write result */
	break;

#endif

/* FFP module 2 */

case 020:						/* .XFER 105220 (OP_N) */
	if (UNIT_CPU_TYPE == UNIT_TYPE_2100)
	    PC = (PC + 1) & VAMASK;			/* 2100 .XFER returns to P+2 */
XFER:							/* enter here from .DFER */
	sc = 3;						/* set count for 3-wd xfer */
	goto CFER;					/* do transfer */

case 021:						/* .GOTO 105221 (OP_AK) */
	if ((op[1] == 0) || (op[1] & SIGN))		/* index < 1? */
	    op[1] = 1;					/* reset min */
	sa = PC + op[1] - 1;				/* point to jump target */
	if (sa >= op[0])				/* must be <= last target */
	    sa = op[0] - 1;
	da = ReadW (sa);				/* get jump target */
	if (reason = resolve (da, &MA, intrq)) {	/* resolve indirects */
	    PC = err_PC;				/* irq restarts instruction */
	    break;  }
	mp_dms_jmp (MA);				/* validate jump addr */
	PCQ_ENTRY;					/* record last PC */
	PC = MA;					/* jump */
	BR = op[0];					/* (for 2100 FFP compat) */
	break;

case 022:						/* ..MAP 105222 (OP_KKKK) */
	op[1] = op[1] - 1;				/* decrement 1st subscr */
	if ((AR & 1) == 0)				/* 2-dim access? */
	    op[1] = op[1] + (op[2] - 1) * op[3];	/* compute element offset */
	else {						/* 3-dim access */
	    if (reason = get_ops (OP_KK, op2, intrq)) {	/* get 1st, 2nd ranges */
		PC = err_PC;				/* irq restarts instruction */
		break;  }
	    op[1] = op[1] + ((op[3] - 1) * op2[1] + op[2] - 1) * op2[0];  }  /* offset */
	AR = (op[0] + op[1] * BR) & DMASK;		/* return element address */
	break;

case 023:						/* .ENTR 105223 (OP_A) */
	MA = PC - 3;					/* get addr of entry point */
ENTR:							/* enter here from .ENTP */
	da = op[0];					/* get addr of 1st formal */
	dc = MA - da;					/* get count of formals */
	sa = ReadW (MA);				/* get addr of return point */
	ra = ReadW (sa++);				/* get rtn, ptr to 1st actual */
	WriteW (MA, ra);				/* stuff rtn into caller's ent */
	sc = ra - sa;					/* get count of actuals */
	if (sc > dc) sc = dc;				/* use min (actuals, formals) */
	for (j = 0; j < sc; j++) {
	    MA = ReadW (sa++);				/* get addr of actual */
	    if (reason = resolve (MA, &MA, intrq)) {	/* resolve indirect */
		PC = err_PC;				/* irq restarts instruction */
		break;  }
	    WriteW (da++, MA);  }			/* put addr into formal */
	AR = ra;					/* return address */
	BR = da;					/* addr of 1st unused formal */
	break;

case 024:						/* .ENTP 105224 (OP_A) */
	MA = PC - 5;					/* get addr of entry point */
	goto ENTR;

case 025:						/* .PWR2 105225 (OP_K) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	f_pwr2 (SEXT (op[0]));				/* calc result into A/B */
	break;

case 026:						/* .FLUN 105226 (OP_N) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	f_unpack ();					/* unpack into A/B */
	break;

case 027:						/* $SETP 105227 (OP_K) */
	j = sa = AR;					/* save initial value */
	sb = BR;					/* save initial address */
	AR = 0;						/* AR will return = 0 */
	BR = BR & VAMASK;				/* addr must be direct */
	do {
	    WriteW (BR, j);				/* write value to address */
	    j = (j + 1) & DMASK;			/* incr value */
	    BR = (BR + 1) & VAMASK;			/* incr address */
	    op[0] = op[0] - 1;				/* decr count */
	    if (op[0] && intrq) {			/* more and intr? */
		AR = sa;				/* restore A */
		BR = sb;				/* restore B */
		PC = err_PC;				/* restart instruction */
		break;  }  }
	while (op[0] != 0);				/* loop until count exhausted */
	break;

case 030:						/* .PACK 105230 (OP_C) */
	if (UNIT_CPU_TYPE != UNIT_TYPE_21MX)		/* must be 21MX */
	    return stop_inst;				/* trap if not */
	O = f_pack (SEXT (op[0]));			/* calc A/B and overflow */
	break;

case 031:						/* .CFER 105231 (OP_AA) */
	if (UNIT_CPU_MODEL != UNIT_21MX_E)		/* must be 21MX E-series */
	    return stop_inst;				/* trap if not */
	BR = op[0];					/* get destination address */
	AR = op[1];					/* get source address */
	sc = 4;						/* set for 4-wd xfer */
CFER:							/* enter here from .XFER */
	for (j = 0; j < sc; j++) {			/* xfer loop */
	    WriteW (BR, ReadW (AR));			/* transfer word */
	    AR = (AR + 1) & VAMASK;			/* bump source addr */
	    BR = (BR + 1) & VAMASK;  }			/* bump destination addr */
	E = 0;						/* routine clears E */
	if (UNIT_CPU_TYPE == UNIT_TYPE_2100) {		/* 2100 (and .DFER/.XFER)? */
	    AR = (AR + 1) & VAMASK;			/* 2100 FFP returns X+4, Y+4 */
	    BR = (BR + 1) & VAMASK;  }
	break;

default:						/* others undefined */
	reason = stop_inst;  }

return reason;
}

/* 2000 I/O Processor

   The IOP accelerates certain operations of the HP 2000 Time-Share BASIC system
   I/O processor.  Most 2000 systems were delivered with 2100 CPUs, although IOP
   microcode was developed for the 21MX-M and 21MX-E.  As the I/O processors
   were specific to the 2000 system, general compatibility with other CPU
   microcode options was unnecessary, and indeed no other options were possible
   for the 2100.

   Option implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
      N/A    13206A  13207A  22702A   N/A

   The routines are mapped to instruction codes as follows:

     Instr.	2100	  21MX-M/E   Description
     ------  ----------  ----------  --------------------------------------------
     SAI     105060-117  101400-037  Store A indexed by B (+/- offset in IR<4:0>)
     LAI     105020-057  105400-037  Load A indexed by B (+/- offset in IR<4:0>)
     CRC     105150	 105460	     Generate CRC
     REST    105340	 105461	     Restore registers from stack
     READF   105220	 105462	     Read F register (stack pointer)
     INS       --	 105463	     Initialize F register (stack pointer)
     ENQ     105240	 105464	     Enqueue
     PENQ    105257	 105465	     Priority enqueue
     DEQ     105260	 105466	     Dequeue
     TRSLT   105160	 105467	     Translate character
     ILIST   105000	 105470	     Indirect address list (similar to $SETP)
     PRFEI   105222	 105471	     Power fail exit with I/O
     PRFEX   105223	 105472	     Power fail exit
     PRFIO   105221	 105473	     Power fail I/O
     SAVE    105362	 105474	     Save registers to stack

     MBYTE   105120	 105765	     Move bytes (MBT)
     MWORD   105200	 105777	     Move words (MVW)
     SBYTE   105300	 105764	     Store byte (SBT)
     LBYTE   105320	 105763	     Load byte (LBT)

   The INS instruction was not required in the 2100 implementation because the
   stack pointer was actually the memory protect fence register and so could be
   loaded directly with an OTA/B 05.  Also, the 21MX implementation did not
   offer the MBYTE, MWORD, SBYTE, and LBYTE instructions because the equivalent
   instructions from the standard Extended Instruction Group were used instead.

   Additional reference:
   - HP 2000 Computer System Sources and Listings Documentation
	(22687-90020, undated), section 3, pages 2-74 through 2-91.
*/

static const OP_PAT op_iop[16] = {
  OP_V,    OP_N,    OP_N,    OP_N,			/* CRC    RESTR  READF  INS   */
  OP_N,    OP_N,    OP_N,    OP_V,			/* ENQ    PENQ   DEQ    TRSLT */
  OP_AC,   OP_CVA,  OP_A,    OP_CV,			/* ILIST  PRFEI  PRFEX  PRFIO */
  OP_N,    OP_N,    OP_N,    OP_N  };			/* SAVE	   ---    ---    ---  */

static t_stat cpu_iop (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;
uint32 hp, tp, i, t, wc, MA;

if ((cpu_unit.flags & UNIT_IOP) == 0)			/* IOP option installed? */
	return stop_inst;

entry = IR & 077;					/* mask to entry point */

if (entry <= 037) {					/* LAI/SAI 10x400-437 */
	MA = ((entry - 020) + BR) & VAMASK;		/* +/- offset */
	if (IR & I_AB) AR = ReadW (MA);			/* AB = 1 -> LAI */
	else WriteW (MA, AR);				/* AB = 0 -> SAI */
	return reason;  }
else if (entry <= 057)					/* IR = 10x440-457? */
	return stop_inst;				/* not part of IOP */

entry = entry - 060;					/* offset 10x460-477 */

if (op_iop[entry] != OP_N)
	if (reason = get_ops (op_iop[entry], op, intrq))/* get instruction operands */
	    return reason;

switch (entry) {					/* decode IR<5:0> */

case 000:						/* CRC 105460 (OP_V) */
	t = ReadW (op[0]) ^ (AR & 0377);		/* xor prev CRC and char */
	for (i = 0; i < 8; i++) {			/* apply polynomial */
	    t = (t >> 1) | ((t & 1) << 15);		/* rotate right */
	    if (t & SIGN) t = t ^ 020001;  }		/* old t<0>? xor */
	WriteW (op[0], t);				/* rewrite CRC */
	break;

case 001:						/* RESTR 105461 (OP_N) */
	iop_sp = (iop_sp - 1) & VAMASK;			/* decr stack ptr */
	t = ReadW (iop_sp);				/* get E and O */
	O = ((t >> 1) ^ 1) & 1;				/* restore O */
	E = t & 1;					/* restore E */
	iop_sp = (iop_sp - 1) & VAMASK;			/* decr sp */
	BR = ReadW (iop_sp);				/* restore B */
	iop_sp = (iop_sp - 1) & VAMASK;			/* decr sp */
	AR = ReadW (iop_sp);				/* restore A */
	if (UNIT_CPU_MODEL == UNIT_2100)
	    mp_fence = iop_sp;				/* 2100 keeps sp in MP FR */
	break;

case 002:						/* READF 105462 (OP_N) */
	AR = iop_sp;					/* copy stk ptr */
	break;

case 003:						/* INS 105463 (OP_N) */
	iop_sp = AR;					/* init stk ptr */
	break;

case 004:						/* ENQ 105464 (OP_N) */
	hp = ReadW (AR & VAMASK);			/* addr of head */
	tp = ReadW ((AR + 1) & VAMASK);			/* addr of tail */
	WriteW ((BR - 1) & VAMASK, 0);			/* entry link */
	WriteW ((tp - 1) & VAMASK, BR);			/* tail link */
	WriteW ((AR + 1) & VAMASK, BR);			/* queue tail */
	if (hp != 0) PC = (PC + 1) & VAMASK;		/* q not empty? skip */
	break;

case 005:						/* PENQ 105465 (OP_N) */
	hp = ReadW (AR & VAMASK);			/* addr of head */
	WriteW ((BR - 1) & VAMASK, hp);			/* becomes entry link */
	WriteW (AR & VAMASK, BR);			/* queue head */
	if (hp == 0)					/* q empty? */
	    WriteW ((AR + 1) & VAMASK, BR);		/* queue tail */
	else PC = (PC + 1) & VAMASK;			/* skip */
	break;

case 006:						/* DEQ 105466 (OP_N) */
	BR = ReadW (AR & VAMASK);			/* addr of head */
	if (BR) {					/* queue not empty? */
	    hp = ReadW ((BR - 1) & VAMASK);		/* read hd entry link */
	    WriteW (AR & VAMASK, hp);			/* becomes queue head */
	    if (hp == 0)				/* q now empty? */
		WriteW ((AR + 1) & VAMASK, (AR + 1) & DMASK);
		PC = (PC + 1) & VAMASK;  }		/* skip */
	break;

case 007:						/* TRSLT 105467 (OP_V) */
	wc = ReadW (op[0]);				/* get count */
	if (wc & SIGN) break;				/* cnt < 0? */
	while (wc != 0) {				/* loop */
	    MA = (AR + AR + ReadB (BR)) & VAMASK;
	    t = ReadB (MA);				/* xlate */
	    WriteB (BR, t);				/* store char */
	    BR = (BR + 1) & DMASK;			/* incr ptr */
	    wc = (wc - 1) & DMASK;			/* decr cnt */
	    if (wc && intrq) {				/* more and intr? */
		WriteW (op[0], wc);			/* save count */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 010:						/* ILIST 105470 (OP_AC) */
	do {						/* for count */
	    WriteW (op[0], AR);				/* write AR to mem */
	    AR = (AR + 1) & DMASK;			/* incr AR */
	    op[0] = (op[0] + 1) & VAMASK;		/* incr MA */
	    op[1] = (op[1] - 1) & DMASK;  }		/* decr count */
	while (op[1] != 0);
	break;

case 011:						/* PRFEI 105471 (OP_CVA) */
	WriteW (op[1], 1);				/* set flag */
	reason = iogrp (op[0], 0);			/* execute I/O instr */
	op[0] = op[2];					/* set rtn and fall through */

case 012:						/* PRFEX 105472 (OP_A) */
	PCQ_ENTRY;
	PC = ReadW (op[0]) & VAMASK;			/* jump indirect */
	WriteW (op[0], 0);				/* clear exit */
	break;

case 013:						/* PRFIO 105473 (OP_CV) */
	WriteW (op[1], 1);				/* set flag */
	reason = iogrp (op[0], 0);			/* execute instr */
	break;

case 014:						/* SAVE 105474 (OP_N) */
	WriteW (iop_sp, AR);				/* save A */
	iop_sp = (iop_sp + 1) & VAMASK;			/* incr stack ptr */
	WriteW (iop_sp, BR);				/* save B */
	iop_sp = (iop_sp + 1) & VAMASK;			/* incr stack ptr */
	t = ((O ^ 1) << 1) | E;				/* merge E and O */
	WriteW (iop_sp, t);				/* save E and O */
	iop_sp = (iop_sp + 1) & VAMASK;			/* incr stack ptr */
	if (UNIT_CPU_TYPE == UNIT_TYPE_2100)
	    mp_fence = iop_sp;				/* 2100 keeps sp in MP FR */
	break;

default:						/* instruction undefined */
	return stop_inst;
	}

return reason;
}

/* Dynamic Mapping System

   The 21MX Dynamic Mapping System (DMS) consisted of the 12731A Memory
   Expansion Module (MEM) card and 38 instructions to expand the basic 32K
   logical address space to a 1024K physical space.  The MEM provided four maps
   of 32 mapping registers each: a system map, a user map, and two DCPC maps.
   DMS worked in conjunction with memory protect to provide a "protected mode"
   in which memory read and write violations could be trapped, and that
   inhibited "privileged" instruction execution that attempted to alter the
   memory mapping.

   Option implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
      N/A     N/A    12976B  13307B   std

   The instruction codes are mapped to routines as follows:

     Instr.  21MX-M  21MX-E/F   Instr.   21MX-M  21MX-E/F
     ------  ------  --------   ------   ------  --------
     10x700  [xmm]    [xmm]	10x720	  XMM	   XMM
     10x701  [nop]    [test]	10x721	  XMS	   XMS
     10x702   MBI      MBI	10x722	  XM*	   XM*
     10x703   MBF      MBF	10x723	 [nop]	  [nop]
     10x704   MBW      MBW	10x724	  XL*	   XL*
     10x705   MWI      MWI	10x725	  XS*	   XS*
     10x706   MWF      MWF	10x726	  XC*	   XC*
     10x707   MWW      MWW	10x727	  LF*	   LF*
     10x710   SY*      SY*	10x730	  RS*	   RS*

     10x711   US*      US*	10x731	  RV*	   RV*
     10x712   PA*      PA*	10x732	  DJP	   DJP
     10x713   PB*      PB*	10x733	  DJS	   DJS
     10x714   SSM      SSM	10x734	  SJP	   SJP
     10x715   JRS      JRS	10x735	  SJS	   SJS
     10x716  [nop]    [nop]	10x736	  UJP	   UJP
     10x717  [nop]    [nop]	10x737	  UJS	   UJS

   Instructions that use IR bit 9 to select the A or B register are designated
   with a * above (e.g., 101710 is SYA, and 105710 is SYB).  For those that do
   not use this feature, either the 101xxx or 105xxx code will execute the
   corresponding instruction, although the 105xxx form is the documented
   instruction code.

   Notes:

     1. Instruction code 10x700 will execute the XMM instruction, although
	10x720 is the documented instruction value.

     2. The DMS privilege violation rules are:
	- load map and CTL5 set (XMM, XMS, XM*, SY*, US*, PA*, PB*)
	- load state or fence and UMAP set (JRS, DJP, DJS, SJP, SJS, UJP, UJS, LF*)

     3. The 21MX manual is incorrect in stating that M*I, M*W, XS* are
	privileged.
*/

static const OP_PAT op_dms[32] = {
  OP_N,    OP_N,    OP_N,    OP_N,			/* xmm    test   MBI    MBF   */
  OP_N,    OP_N,    OP_N,    OP_N,			/* MBW    MWI    MWF    MWW   */
  OP_N,    OP_N,    OP_N,    OP_N,			/* SYA/B  USA/B  PAA/B  PBA/B */
  OP_A,    OP_KA,   OP_N,    OP_N,			/* SSM    JRS    nop    nop   */
  OP_N,    OP_N,    OP_N,    OP_N,			/* XMM    XMS    XMA/B  nop   */
  OP_A,    OP_A,    OP_A,    OP_N,			/* XLA/B  XSA/B  XCA/B  LFA/B */
  OP_N,    OP_N,    OP_A,    OP_A,			/* RSA/B  RVA/B  DJP    DJS   */
  OP_A,    OP_A,    OP_A,    OP_A  };			/* SJP    SJS    UJP    UJS   */

static t_stat cpu_dms (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry, absel;
uint32 i, t, mapi, mapj;

if ((cpu_unit.flags & UNIT_DMS) == 0)			/* DMS option installed? */
	return stop_inst;

absel = (IR & I_AB)? 1: 0;				/* get A/B select */
entry = IR & 037;					/* mask to entry point */

if (op_dms[entry] != OP_N)
	if (reason = get_ops (op_dms[entry], op, intrq))/* get instruction operands */
	    return reason;

switch (entry) {					/* decode IR<3:0> */

/* DMS module 1 */

case 000:						/* [undefined] 105700 (OP_N) */
	goto XMM;					/* decodes as XMM */

case 001:						/* [self test] 105701 (OP_N) */
	ABREG[absel] = ABREG[absel] ^ DMASK;		/* CMA or CMB */
	break;

case 002:						/* MBI 105702 (OP_N) */
	AR = AR & ~1;					/* force A, B even */
	BR = BR & ~1;
	while (XR != 0) {				/* loop */
	    t = ReadB (AR);				/* read curr */
	    WriteBA (BR, t);				/* write alt */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 003:						/* MBF 105703 (OP_N) */
	AR = AR & ~1;					/* force A, B even */
	BR = BR & ~1;
	while (XR != 0) {				/* loop */
	    t = ReadBA (AR);				/* read alt */
	    WriteB (BR, t);				/* write curr */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 004:						/* MBW 105704 (OP_N) */
	AR = AR & ~1;					/* force A, B even */
	BR = BR & ~1;
	while (XR != 0) {				/* loop */
	    t = ReadBA (AR);				/* read alt */
	    WriteBA (BR, t);				/* write alt */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq && !(AR & 1)) {		/* more, int, even? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 005:						/* MWI 105705 (OP_N) */
	while (XR != 0) {				/* loop */
	    t = ReadW (AR & VAMASK);			/* read curr */
	    WriteWA (BR & VAMASK, t);			/* write alt */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq) {				/* more and intr? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 006:						/* MWF 105706 (OP_N) */
	while (XR != 0) {				/* loop */
	    t = ReadWA (AR & VAMASK);			/* read alt */
	    WriteW (BR & VAMASK, t);			/* write curr */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq) {				/* more and intr? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 007:						/* MWW 105707 (OP_N) */
	while (XR != 0) {				/* loop */
	    t = ReadWA (AR & VAMASK);			/* read alt */
	    WriteWA (BR & VAMASK, t);			/* write alt */
	    AR = (AR + 1) & DMASK;			/* incr ptrs */
	    BR = (BR + 1) & DMASK;
	    XR = (XR - 1) & DMASK;
	    if (XR && intrq) {				/* more and intr? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 010:						/* SYA, SYB 10x710 (OP_N) */
case 011:						/* USA, USB 10x711 (OP_N) */
case 012:						/* PAA, PAB 10x712 (OP_N) */
case 013:						/* PBA, PBB 10x713 (OP_N) */
	mapi = (IR & 03) << VA_N_PAG;			/* map base */
	if (ABREG[absel] & SIGN) {			/* store? */
	    for (i = 0; i < MAP_LNT; i++) {
		t = dms_rmap (mapi + i);		/* map to memory */
		WriteW ((ABREG[absel] + i) & VAMASK, t);  }  }
	else {						/* load */
	    dms_viol (err_PC, MVI_PRV);			/* priv if PRO */
	    for (i = 0; i < MAP_LNT; i++) {
		t = ReadW ((ABREG[absel] + i) & VAMASK);
		dms_wmap (mapi + i, t);   }  }		/* mem to map */
	ABREG[absel] = (ABREG[absel] + MAP_LNT) & DMASK;
	break;

case 014:						/* SSM 105714 (OP_A) */
	WriteW (op[0], dms_upd_sr ());			/* store stat */
	break;

case 015:						/* JRS 105715 (OP_KA) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	dms_enb = 0;					/* assume off */
	dms_ump = SMAP;
	if (op[0] & 0100000) {				/* set enable? */
	    dms_enb = 1;
	    if (op[0] & 0040000) dms_ump = UMAP;  }	/* set/clr usr */
	mp_dms_jmp (op[1]);				/* mpck jmp target */
	PCQ_ENTRY;					/* save old PC */
	PC = op[1];					/* jump */
	ion_defer = 1;					/* defer intr */
	break;

/* DMS module 2 */

case 020:						/* XMM 105720 (OP_N) */
XMM:
	if (XR == 0) break;				/* nop? */
	while (XR != 0) {				/* loop */
	    if (XR & SIGN) {				/* store? */
		t = dms_rmap (AR);			/* map to mem */
		WriteW (BR & VAMASK, t);
		XR = (XR + 1) & DMASK;  }
	    else {					/* load */
		dms_viol (err_PC, MVI_PRV);		/* priv viol if prot */
		t = ReadW (BR & VAMASK);		/* mem to map */
		dms_wmap (AR, t);
		XR = (XR - 1) & DMASK;  }
	    AR = (AR + 1) & DMASK;
	    BR = (BR + 1) & DMASK;
	    if (intrq && ((XR & 017) == 017)) {		/* intr, grp of 16? */
		PC = err_PC;				/* stop for now */
		break;  }  }
	break;

case 021:						/* XMS 105721 (OP_N) */
	if ((XR & SIGN) || (XR == 0)) break;		/* nop? */
	dms_viol (err_PC, MVI_PRV);			/* priv viol if prot */
	while (XR != 0) {
	    dms_wmap (AR, BR);				/* AR to map */
	    XR = (XR - 1) & DMASK;
	    AR = (AR + 1) & DMASK;
	    BR = (BR + 1) & DMASK;
	    if (intrq && ((XR & 017) == 017)) {		/* intr, grp of 16? */
		PC = err_PC;
		break;  }  }
	break;

case 022:						/* XMA, XMB 10x722 (OP_N) */
	dms_viol (err_PC, MVI_PRV);			/* priv viol if prot */
	if (ABREG[absel] & 0100000) mapi = UMAP;
	else mapi = SMAP;
	if (ABREG[absel] & 0000001) mapj = PBMAP;
	else mapj = PAMAP;
	for (i = 0; i < MAP_LNT; i++) {
	    t = dms_rmap (mapi + i);			/* read map */
	    dms_wmap (mapj + i, t);  }			/* write map */
	break;

case 024:						/* XLA, XLB 10x724 (OP_A) */
	ABREG[absel] = ReadWA (op[0]);			/* load alt */
	break;

case 025:						/* XSA, XSB 10x725 (OP_A) */
	WriteWA (op[0], ABREG[absel]);			/* store alt */
	break;

case 026:						/* XCA, XCB 10x726 (OP_A) */
	if (ABREG[absel] != ReadWA (op[0]))		/* compare alt */
	    PC = (PC + 1) & VAMASK;
	break;

case 027:						/* LFA, LFB 10x727 (OP_N) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	dms_sr = (dms_sr & ~(MST_FLT | MST_FENCE)) |
	    (ABREG[absel] & (MST_FLT | MST_FENCE));
	break;

case 030:						/* RSA, RSB 10x730 (OP_N) */
	ABREG[absel] = dms_upd_sr ();			/* save stat */
	break;

case 031:						/* RVA, RVB 10x731 (OP_N) */
	ABREG[absel] = dms_vr;				/* save viol */
	break;

case 032:						/* DJP 105732 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	mp_dms_jmp (op[0]);				/* validate jump addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = op[0];					/* new PC */
	dms_enb = 0;					/* disable map */
	dms_ump = SMAP;
	ion_defer = 1;
	break;

case 033:						/* DJS 105733 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	WriteW (op[0], PC);				/* store ret addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = (op[0] + 1) & VAMASK;			/* new PC */
	dms_enb = 0;					/* disable map */
	dms_ump = SMAP;
	ion_defer = 1;					/* defer intr */
	break;

case 034:						/* SJP 105734 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	mp_dms_jmp (op[0]);				/* validate jump addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = op[0];					/* jump */
	dms_enb = 1;					/* enable system */
	dms_ump = SMAP;
	ion_defer = 1;					/* defer intr */
	break;

case 035:						/* SJS 105735 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	t = PC;						/* save retn addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = (op[0] + 1) & VAMASK;			/* new PC */
	dms_enb = 1;					/* enable system */
	dms_ump = SMAP;
	WriteW (op[0], t);				/* store ret addr */
	ion_defer = 1;					/* defer intr */
	break;

case 036:						/* UJP 105736 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	mp_dms_jmp (op[0]);				/* validate jump addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = op[0];					/* jump */
	dms_enb = 1;					/* enable user */
	dms_ump = UMAP;
	ion_defer = 1;					/* defer intr */
	break;

case 037:						/* UJS 105737 (OP_A) */
	if (dms_ump) dms_viol (err_PC, MVI_PRV);	/* priv viol if prot */
	t = PC;						/* save retn addr */
	PCQ_ENTRY;					/* save curr PC */
	PC = (op[0] + 1) & VAMASK;			/* new PC */
	dms_enb = 1;					/* enable user */
	dms_ump = UMAP;
	WriteW (op[0], t);				/* store ret addr */
	ion_defer = 1;					/* defer intr */
	break;

default:						/* others NOP */
	break;  }

return reason;
}

/* Extended Instruction Group

   The Extended Instruction Group (EIG) adds 32 index and 10 bit/byte/word
   manipulation instructions to the 21MX base set.  These instructions
   use the new X and Y index registers that were added to the 21MX.

   Option implementation by CPU was as follows:

      2116    2100   21MX-M  21MX-E  21MX-F
     ------  ------  ------  ------  ------
      N/A     N/A     std     std     std

   The instruction codes are mapped to routines as follows:

     Instr.  21MX-M/E/F   Instr.   21MX-M/E/F
     ------  ----------   ------   ----------
     10x740	S*X	  10x760      ISX
     10x741	C*X	  10x761      DSX
     10x742	L*X	  10x762      JLY
     10x743	STX	  10x763      LBT
     10x744	CX*	  10x764      SBT
     10x745	LDX	  10x765      MBT
     10x746	ADX	  10x766      CBT
     10x747	X*X	  10x767      SFB

     10x750	S*Y	  10x770      ISY
     10x751	C*Y	  10x771      DSY
     10x752	L*Y	  10x772      JPY
     10x753	STY	  10x773      SBS
     10x754	CY*	  10x774      CBS
     10x755	LDY	  10x775      TBS
     10x756	ADY	  10x776      CMW
     10x757	X*Y	  10x777      MVW

   Instructions that use IR bit 9 to select the A or B register are designated
   with a * above (e.g., 101740 is SAX, and 105740 is SBX).  For those that do
   not use this feature, either the 101xxx or 105xxx code will execute the
   corresponding instruction, although the 105xxx form is the documented
   instruction code.

   Notes:

     1. The LBT, SBT, MBT, and MVW instructions are used as part of the 2100 IOP
	implementation.  When so called, the MBT and MVW instructions have the
	additional restriction that the count must be positive.
*/

static const OP_PAT op_eig[32] = {
  OP_A,    OP_N,    OP_A,    OP_A,			/* S*X    C*X    L*X    STX   */
  OP_N,    OP_K,    OP_K,    OP_N,			/* CX*    LDX    ADX    X*X   */
  OP_A,    OP_N,    OP_A,    OP_A,			/* S*Y    C*Y    L*Y    STY   */
  OP_N,    OP_K,    OP_K,    OP_N,			/* CY*    LDY    ADY    X*Y   */
  OP_N,    OP_N,    OP_A,    OP_N,			/* ISX    DSX    JLY    LBT   */
  OP_N,    OP_KV,   OP_KV,   OP_N,			/* SBT    MBT    CBT    SFB   */
  OP_N,    OP_N,    OP_C,    OP_KA,			/* ISY    DSY    JPY    SBS   */
  OP_KA,   OP_KK,   OP_KV,   OP_KV  };			/* CBS    TBS    CMW    MVW   */

static t_stat cpu_eig (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry, absel;
uint32 t, v1, v2, wc;
int32 sop1, sop2;

absel = (IR & I_AB)? 1: 0;				/* get A/B select */
entry = IR & 037;					/* mask to entry point */

if (op_eig[entry] != OP_N)
	if (reason = get_ops (op_eig[entry], op, intrq))/* get instruction operands */
	    return reason;

switch (entry) {					/* decode IR<4:0> */

/* EIG module 1 */

case 000:						/* SAX, SBX 10x740 (OP_A) */
	op[0] = (op[0] + XR) & VAMASK;			/* indexed addr */
	WriteW (op[0], ABREG[absel]);			/* store */
	break;

case 001:						/* CAX, CBX 10x741 (OP_N) */
	XR = ABREG[absel];				/* copy to XR */
	break;

case 002:						/* LAX, LBX 10x742 (OP_A) */
	op[0] = (op[0] + XR) & VAMASK;			/* indexed addr */
	ABREG[absel] = ReadW (op[0]);			/* load */
	break;

case 003:						/* STX 105743 (OP_A) */
	WriteW (op[0], XR);				/* store XR */
	break;

case 004:						/* CXA, CXB 10x744 (OP_N) */
	ABREG[absel] = XR;				/* copy from XR */
	break;

case 005:						/* LDX 105745 (OP_K)*/
	XR = op[0];					/* load XR */
	break;

case 006:						/* ADX 105746 (OP_K) */
	t = XR + op[0];					/* add to XR */
	if (t > DMASK) E = 1;				/* set E, O */
	if (((~XR ^ op[0]) & (XR ^ t)) & SIGN) O = 1;
	XR = t & DMASK;
	break;

case 007:						/* XAX, XBX 10x747 (OP_N) */
	t = XR;						/* exchange XR */
	XR = ABREG[absel];
	ABREG[absel] = t;
	break;

case 010:						/* SAY, SBY 10x750 (OP_A) */
	op[0] = (op[0] + YR) & VAMASK;			/* indexed addr */
	WriteW (op[0], ABREG[absel]);			/* store */
	break;

case 011:						/* CAY, CBY 10x751 (OP_N) */
	YR = ABREG[absel];				/* copy to YR */
	break;

case 012:						/* LAY, LBY 10x752 (OP_A) */
	op[0] = (op[0] + YR) & VAMASK;			/* indexed addr */
	ABREG[absel] = ReadW (op[0]);			/* load */
	break;

case 013:						/* STY 105753 (OP_A) */
	WriteW (op[0], YR);				/* store YR */
	break;

case 014:						/* CYA, CYB 10x754 (OP_N) */
	ABREG[absel] = YR;				/* copy from YR */
	break;

case 015:						/* LDY 105755 (OP_K) */
	YR = op[0];					/* load YR */
	break;

case 016:						/* ADY 105756 (OP_K) */
	t = YR + op[0];					/* add to YR */
	if (t > DMASK) E = 1;				/* set E, O */
	if (((~YR ^ op[0]) & (YR ^ t)) & SIGN) O = 1;
	YR = t & DMASK;
	break;

case 017:						/* XAY, XBY 10x757 (OP_N) */
	t = YR;						/* exchange YR */
	YR = ABREG[absel];
	ABREG[absel] = t;
	break;

/* EIG module 2 */

case 020:						/* ISX 105760 (OP_N) */
	XR = (XR + 1) & DMASK;				/* incr XR */
	if (XR == 0) PC = (PC + 1) & VAMASK;		/* skip if zero */
	break;

case 021:						/* DSX 105761 (OP_N) */
	XR = (XR - 1) & DMASK;				/* decr XR */
	if (XR == 0) PC = (PC + 1) & VAMASK;		/* skip if zero */
	break;

case 022:						/* JLY 105762 (OP_A) */
	mp_dms_jmp (op[0]);				/* validate jump addr */
	PCQ_ENTRY;
	YR = PC;					/* ret addr to YR */
	PC = op[0];					/* jump */
	break;

case 023:						/* LBT 105763 (OP_N) */
	AR = ReadB (BR);				/* load byte */
	BR = (BR + 1) & DMASK;				/* incr ptr */
	break;

case 024:						/* SBT 105764 (OP_N) */
	WriteB (BR, AR);				/* store byte */
	BR = (BR + 1) & DMASK;				/* incr ptr */
	break;

case 025:						/* MBT 105765 (OP_KV) */
	wc = ReadW (op[1]);				/* get continuation count */
	if (wc == 0) wc = op[0];			/* none? get initiation count */
	if ((wc & SIGN) && (UNIT_CPU_TYPE == UNIT_TYPE_2100))
	    break;					/* < 0 is NOP for 2100 IOP */
	while (wc != 0) {				/* while count */
	    WriteW (op[1], wc);				/* for MP abort */
	    t = ReadB (AR);				/* move byte */
	    WriteB (BR, t);
	    AR = (AR + 1) & DMASK;			/* incr src */
	    BR = (BR + 1) & DMASK;			/* incr dst */
	    wc = (wc - 1) & DMASK;			/* decr cnt */
	    if (intrq && wc) {				/* intr, more to do? */
		PC = err_PC;				/* back up PC */
		break;  }  }				/* take intr */
	WriteW (op[1], wc);				/* clean up inline */
	break;

case 026:						/* CBT 105766 (OP_KV) */
	wc = ReadW (op[1]);				/* get continuation count */
	if (wc == 0) wc = op[0];			/* none? get initiation count */
	while (wc != 0) {				/* while count */
	    WriteW (op[1], wc);				/* for MP abort */
	    v1 = ReadB (AR);				/* get src1 */
	    v2 = ReadB (BR);				/* get src2 */
	    if (v1 != v2) {				/* compare */
		PC = (PC + 1 + (v1 > v2)) & VAMASK;
		BR = (BR + wc) & DMASK;			/* update BR */
		wc = 0;					/* clr interim */
		break;  }
	    AR = (AR + 1) & DMASK;			/* incr src1 */
	    BR = (BR + 1) & DMASK;			/* incr src2 */
	    wc = (wc - 1) & DMASK;			/* decr cnt */
	    if (intrq && wc) {				/* intr, more to do? */
		PC = err_PC;				/* back up PC */
		break;  }  }				/* take intr */
	WriteW (op[1], wc);				/* clean up inline */
	break;

case 027:						/* SFB 105767 (OP_N) */
	v1 = AR & 0377;					/* test byte */
	v2 = (AR >> 8) & 0377;				/* term byte */
	for (;;) {					/* scan */
	    t = ReadB (BR);				/* read byte */
	    if (t == v1) break;				/* test match? */
	    BR = (BR + 1) & DMASK;
	    if (t == v2) {				/* term match? */
		PC = (PC + 1) & VAMASK;
		break;  }
	    if (intrq) {				/* int pending? */
		PC = err_PC;				/* back up PC */
		break;  }  }				/* take intr */
	break;

case 030:						/* ISY 105770 (OP_N) */
	YR = (YR + 1) & DMASK;				/* incr YR */
	if (YR == 0) PC = (PC + 1) & VAMASK;		/* skip if zero */
	break;

case 031:						/* DSY 105771 (OP_N) */
	YR = (YR - 1) & DMASK;				/* decr YR */
	if (YR == 0) PC = (PC + 1) & VAMASK;		/* skip if zero */
	break;

case 032:						/* JPY 105772 (OP_C) */
	op[0] = (op[0] + YR) & VAMASK;			/* index, no indir */
	mp_dms_jmp (op[0]);				/* validate jump addr */
	PCQ_ENTRY;
	PC = op[0];					/* jump */
	break;

case 033:						/* SBS 105773 (OP_KA) */
	WriteW (op[1], ReadW (op[1]) | op[0]);		/* set bits */
	break;

case 034:						/* CBS 105774 (OP_KA) */
	WriteW (op[1], ReadW (op[1]) & ~op[0]);		/* clear bits */
	break;

case 035:						/* TBS 105775 (OP_KK) */
	if ((op[1] & op[0]) != op[0])			/* test bits */
	    PC = (PC + 1) & VAMASK;
	break;

case 036:						/* CMW 105776 (OP_KV) */
	wc = ReadW (op[1]);				/* get continuation count */
	if (wc == 0) wc = op[0];			/* none? get initiation count */
	while (wc != 0) {				/* while count */
	    WriteW (op[1], wc);				/* for abort */
	    v1 = ReadW (AR & VAMASK);			/* first op */
	    v2 = ReadW (BR & VAMASK);			/* second op */
	    sop1 = (int32) SEXT (v1);			/* signed */
	    sop2 = (int32) SEXT (v2);
	    if (sop1 != sop2) {				/* compare */
		PC = (PC + 1 + (sop1 > sop2)) & VAMASK;
		BR = (BR + wc) & DMASK;			/* update BR */
		wc = 0;					/* clr interim */
		break;  }
	    AR = (AR + 1) & DMASK;			/* incr src1 */
	    BR = (BR + 1) & DMASK;			/* incr src2 */
	    wc = (wc - 1) & DMASK;			/* decr cnt */
	    if (intrq && wc) {				/* intr, more to do? */
		PC = err_PC;				/* back up PC */
		break;  }  }				/* take intr */
	WriteW (op[1], wc);				/* clean up inline */
	break;

case 037:						/* MVW 105777 (OP_KV) */
	wc = ReadW (op[1]);				/* get continuation count */
	if (wc == 0) wc = op[0];			/* none? get initiation count */
	if ((wc & SIGN) && (UNIT_CPU_TYPE == UNIT_TYPE_2100))
	    break;					/* < 0 is NOP for 2100 IOP */
	while (wc != 0) {				/* while count */
	    WriteW (op[1], wc);				/* for abort */
	    t = ReadW (AR & VAMASK);			/* move word */
	    WriteW (BR & VAMASK, t);
	    AR = (AR + 1) & DMASK;			/* incr src */
	    BR = (BR + 1) & DMASK;			/* incr dst */
	    wc = (wc - 1) & DMASK;			/* decr cnt */
	    if (intrq && wc) {				/* intr, more to do? */
		PC = err_PC;				/* back up PC */
		break;  }  }				/* take intr */
	WriteW (op[1], wc);				/* clean up inline */
	break;

default:						/* all others NOP */
	break;  }

return reason;
}

/* Get instruction operands

   Operands for a given instruction are specifed by an "operand pattern"
   consisting of flags indicating the types and storage methods.  The pattern
   directs how each operand is to be retrieved and whether the operand value or
   address is returned in the operand array.

   Eight operand encodings are defined:

      Code   Operand Description	      Example	 Return
     ------  -----------------------------  -----------  ------------
     OP_NUL  No operand present			[inst]	 None

     OP_CON  Inline constant			[inst]	 Value of C
					     C	DEC 0

     OP_VAR  Inline variable			[inst]	 Address of V
					     V	BSS 1

     OP_ADR  Address				[inst]	 Address of A
						DEF A
						...
					     A  EQU *
	
     OP_ADK  Address of a 1-word constant	[instr]	 Value of K
						DEF K
						...
					     K	DEC 0

     OP_ADF  Address of a 2-word constant	[inst]	 Value of F
						DEF F
						...
					     F	DEC 0.0

     OP_ADX  Address of a 3-word constant	[inst]	 Value of X
						DEF X
						...
					     X	DEX 0.0

     OP_ADT  Address of a 4-word constant	[inst]	 Value of T
						DEF T
						...
					     T	DEY 0.0

   Address operands, i.e., those having a DEF to the operand, will be resolved
   to direct addresses.  If an interrupt is pending and more than three levels
   of indirection are used, the routine returns without completing operand
   retrieval (the instruction will be retried after interrupt servicing).
   Addresses are always resolved in the current DMS map.

   An operand pattern consists of one or more operand encodings, corresponding
   to the operands required by a given instruction.  Values are returned in
   sequence to the operand array.  Addresses and one-word values are returned in
   the lower half of the 32-bit array element.  Two-word values are packed into
   one array element, with the first word in the upper 16 bits.  Three- and
   four-word values are packed into two consecutive elements, with the last word
   of a three-word value in the upper 16 bits of of the second element.

   IMPLEMENTATION NOTE: OP_ADT is not currently supported.
*/

static t_stat get_ops (OP_PAT pattern, OPS op, uint32 irq)
{
t_stat reason = SCPE_OK;
OP_PAT flags;
uint32 i, MA;
XPN xop;

for (i = 0; i < OP_N_F; i++) {
	flags = pattern & OP_M_FLAGS;			/* get operand pattern */

	if (flags >= OP_ADR)				/* address operand? */
	    if (reason = resolve (ReadW (PC), &MA, irq))/* resolve indirects */
		return reason;

	switch (flags) {
	    case OP_NUL:				/* null operand */
		return reason;				/* no more, so quit */

	    case OP_CON:				/* constant operand */
		*op++ = ReadW (PC);			/* get value */
		break;

	    case OP_VAR:				/* variable operand */
		*op++ = PC;				/* get pointer to variable */
		break;

	    case OP_ADR:				/* address operand */
		*op++ = MA;				/* get address */
		break;

	    case OP_ADK:				/* address of 1-word constant */
		*op++ = ReadW (MA);			/* get value */
		break;

	    case OP_ADF:				/* address of 2-word constant */
		*op++ = ReadF (MA);			/* get value */
		break;

	    case OP_ADX:				/* address of 3-word constant */
		xop = ReadX (MA);
		*op++ = xop.high;			/* get first two words of value */
		*op++ = xop.low;			/* get third word of value */
		break;

	    case OP_ADT:				/* address of 4-word constant */

	    default:
		return SCPE_IERR;			/* not implemented */
	    }

	PC = (PC + 1) & VAMASK;
	pattern = pattern >> OP_N_FLAGS;  }		/* move next pattern into place */
return reason;
}
