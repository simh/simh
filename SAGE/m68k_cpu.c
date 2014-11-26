/* m68k_cpu.c: 68k-CPU simulator

   Copyright (c) 2009-2010 Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   04-Oct-09    HV      Initial version
   25-Apr-10    HV      Fixed LSR.W and ROXR.B instructions
   26-Jun-10	HV		Incomplete decoding of BCHG d,d instruction
   15-Jul-10	HV		IRQ logic loses lower prio interrupts
   17-Jul-10	HV		Implement Call/Exit Tracing with symbol table lookup
   17-Jul-10	HV		Mustn't grant interrupt at level == IPL
   18-Jul-10	HV		Broken address calculation for AIDX and EA_W_RMW, wonder why this didn't pop up earlier.
   20-Jul-10	HV		Corrected ADDQ.W/SUBQ.W for EA_ADIR, EOR.[WL]
   23-Jul-10	HV		Broken C code sequence in lsl.l
   23-Jul-10	HV		RTE didn't set/reset S bit 
*/

#include "m68k_cpu.h"
#include <ctype.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/* status reg flags */
#define FLAG_C			0x0001
#define FLAG_V			0x0002
#define FLAG_Z			0x0004
#define FLAG_N			0x0008
#define FLAG_X			0x0010
#define FLAG_I0			0x0100
#define FLAG_I1			0x0200
#define FLAG_I2			0x0400
#define FLAG_IPL_MASK	(FLAG_I0|FLAG_I1|FLAG_I2)
#define FLAG_S			0x2000
#define FLAG_T			0x8000
#define FLAG_T1			FLAG_T
#define FLAG_T0			0x4000

#define BIT7			0x80
#define BIT8			0x100
#define BIT15			0x8000
#define BIT16			0x10000
#define BIT31			0x80000000
#define BIT32			0x100000000L

#define MASK_0(x)		((x) & 1)
#define MASK_8U(x)		((x) & 0xffffff00)
#define MASK_8L(x)		((x) & 0x000000ff)
#define MASK_8SGN(x)	((x) & BIT7)
#define MASK_9(x)		((x) & BIT8)
#define MASK_16U(x)		((x) & 0xffff0000)
#define MASK_16L(x)		((x) & 0x0000ffff)
#define MASK_16SGN(x)	((x) & BIT15)
#define MASK_17(x)		((x) & BIT16)
#define MASK_32U(x)		(0)
#define MASK_32L(x)		((x) & 0xffffffff)
#define MASK_32SGN(x)	((x) & BIT31)
#define MASK_33(x)		((x) & BIT32)

#define COMBINE8(tgt,src)  (MASK_8U(tgt) | MASK_8L(src))
#define COMBINE16(tgt,src) (MASK_16U(tgt) | MASK_16L(src))
#define COMBINE32(tgt,src) MASK_32L(src)

extern t_addr addrmask;

static t_addr addrmasks[] = {
	0x00ffffff,		/*68000*/
	0x000fffff,		/*68008*/
	0x00ffffff,		/*68010*/
	0xffffffff,		/*68020*/
	0xffffffff		/*68030*/
};

int16 cputype = CPU_TYPE_68000 >> UNIT_CPU_V_TYPE;

/* CPU data structures 
 * m68kcpu_dev		CPU device descriptor
 * m68kcpu_unit		CPU unit descriptor
 * m68kcpu_reg		CPU register list
 * m68kcpu_mod		CPU modifiers list
 */

UNIT *m68kcpu_unit;  /* must be set elsewhere */
DEVICE *m68kcpu_dev; /* must be set elsewhere */

void (*m68kcpu_trapcallback)(DEVICE* dptr,int trapnum) = 0;

/* register set */
int32 	DR[8];
#define D0	DR[0]
#define D1	DR[1]
#define D2	DR[2]
#define D3	DR[3]
#define D4	DR[4]
#define D5	DR[5]
#define D6	DR[6]
#define D7	DR[7]
t_addr 	AR[8];
#define A0	AR[0]
#define A1	AR[1]
#define A2	AR[2]
#define A3	AR[3]
#define A4	AR[4]
#define A5	AR[5]
#define A6	AR[6]
#define A7	AR[7]
t_addr 	USP;
t_addr  *cur_sp;

uint16	SR;
#define CCR_C	(SR & FLAG_C)
#define CCR_V	(SR & FLAG_V)
#define CCR_Z	(SR & FLAG_Z)
#define CCR_N	(SR & FLAG_N)
#define CCR_X	(SR & FLAG_X)
#define SR_IPL	((SR & FLAG_IPL_MASK)>>8)
#define SR_S	(SR & FLAG_S)
#define SR_T	(SR & FLAG_T)
#define SR_T0	(SR & FLAG_T0)
#define SR_T1	(SR & FLAG_T1)

#define ONEF(flag) SR |= (flag)
#define CLRF(flag) SR &= ~(flag)
#define SETF(cond,flag) if (cond) SR |= (flag); else SR &= ~(flag)
#define SETZ8(cond) if (MASK_8L(cond)) SR &= ~FLAG_Z; else SR |= FLAG_Z
#define SETZ16(cond) if (MASK_16L(cond)) SR &= ~FLAG_Z; else SR |= FLAG_Z
#define SETZ32(cond) if (MASK_32L(cond)) SR &= ~FLAG_Z; else SR |= FLAG_Z
#define SETNZ8(cond) SETZ8(cond); if (MASK_8SGN(cond)) SR |= FLAG_N; else SR &= ~FLAG_N
#define SETNZ16(cond) SETZ16(cond); if (MASK_16SGN(cond)) SR |= FLAG_N; else SR &= ~FLAG_N
#define SETNZ32(cond) SETZ32(cond); if (MASK_32SGN(cond)) SR |= FLAG_N; else SR &= ~FLAG_N
#define SETV_ADD8(a1,a2,r)	SETF(MASK_8SGN(((a1)^(r))&((a2)^(r))),FLAG_V);
#define SETV_ADD16(a1,a2,r)	SETF(MASK_16SGN(((a1)^(r))&((a2)^(r))),FLAG_V);
#define SETV_ADD32(a1,a2,r)	SETF(MASK_32SGN(((a1)^(r))&((a2)^(r))),FLAG_V);
#define SETV_SUB8(s,d,r)	SETF(MASK_8SGN(((s)^(d))&((r)^(d))),FLAG_V)
#define SETV_SUB16(s,d,r)	SETF(MASK_16SGN(((s)^(d))&((r)^(d))),FLAG_V)
#define SETV_SUB32(s,d,r)	SETF(MASK_32SGN(((s)^(d))&((r)^(d))),FLAG_V)

#define ASSERT_PRIV() if (!SR_S) { rc = STOP_PRVIO; break; }
#define ASSERT_OK(func) if ((rc=(func)) != SCPE_OK) break
#define ASSERT_OKRET(func) if ((rc=(func)) != SCPE_OK) return rc

#define AREG(r)	(r==7 ? cur_sp : &AR[r])

uint16	SFC;
uint16	DFC;
uint32	VBR;
t_addr	saved_PC;
static t_bool intpending;
static int m68k_sublevel;

REG m68kcpu_reg[] = {
	{ HRDATA (D0,		DR[0],		32)					},
	{ HRDATA (D1,		DR[1],		32)					},
	{ HRDATA (D2,		DR[2],		32)					},
	{ HRDATA (D3,		DR[3],		32)					},
	{ HRDATA (D4,		DR[4],		32)					},
	{ HRDATA (D5,		DR[5],		32)					},
	{ HRDATA (D6,		DR[6],		32)					},
	{ HRDATA (D7,		DR[7],		32)					},
	{ HRDATA (A0,		AR[0],		32)					},
	{ HRDATA (A1,		AR[1],		32)					},
	{ HRDATA (A2,		AR[2],		32)					},
	{ HRDATA (A3,		AR[3],		32)					},
	{ HRDATA (A4,		AR[4],		32)					},
	{ HRDATA (A5,		AR[5],		32)					},
	{ HRDATA (A6,		AR[6],		32)					},
	{ HRDATA (A7,		AR[7],		32)					},
	{ HRDATA (SSP,		AR[7],		32)					},
	{ HRDATA (USP,		USP,		32)					},
	{ HRDATA (PC,		saved_PC,	32)					},
	{ HRDATA (SR,		SR,			16)					},
	{ HRDATA (CCR,		SR,			8)					},
	{ FLDATA (C,		SR,			0)					},
	{ FLDATA (V,		SR,			1)					},
	{ FLDATA (Z,		SR,			2)					},
	{ FLDATA (N,		SR,			3)					},
	{ FLDATA (X,		SR,			4)					},
	{ GRDATA (IPL,		SR,			8,	3,  8)			},
	{ FLDATA (S,		SR,			13)					},
	{ FLDATA (T,		SR,			15)					},
	{ HRDATA (SFC,		SFC,		3), 	REG_HIDDEN	},
	{ HRDATA (DFC,		DFC,		3), 	REG_HIDDEN	},
	{ HRDATA (VBR,		VBR,		32),	REG_RO		},
	{ FLDATA (IRQPEN,   intpending, 0),     REG_HIDDEN  },
	{ NULL }
};

DEBTAB m68kcpu_dt[] = {
	{ "EXC",	DBG_CPU_EXC    },
	{ "PC",		DBG_CPU_PC     },
	{ "INT",	DBG_CPU_INT    },
	{ "CTRACE",	DBG_CPU_CTRACE },
	{ "BTRACE",	DBG_CPU_BTRACE },
	{ NULL,		0              }
};

static char *condnames[] = {
		"RA", "SR", "HI", "LS", "CC", "CS", "NE", "EQ", "VC", "VS", "PL", "MI", "GE", "LT", "GT", "LE"
};

#if 0
/* sample code */
static MTAB m68kcpu_mod[] = {
	M68KCPU_STDMOD,
	{ 0 }
};

DEVICE m68kcpu_dev = {
	"CPU", &m68kcpu_unit, m68kcpu_reg, m68kcpu_mod,
	1, 16, 32, 2, 16, 16,
	&m68kcpu_ex, &m68kcpu_dep, &m68kcpu_reset,
	&m68kcpu_boot, NULL, NULL,
	NULL, DEV_DEBUG, 0,
	m68kcpu_dt, NULL, NULL
};
#endif

static DEVICE* cpudev_self = 0;

t_stat m68kcpu_peripheral_reset()
{
	t_stat rc;
	DEVICE** devs = sim_devices;
	DEVICE* dptr;
	if (!devs) return SCPE_IERR;
	
	while ((dptr = *devs) != NULL) {
		if (dptr != cpudev_self) { 
			ASSERT_OKRET(dptr->reset(dptr));
		}
		devs++;
	}
	return SCPE_OK;
}

/* simple prefetch I cache */
#define CACHE_SIZE 16
#define CACHE_MASK 0x0f

static t_addr cache_pc;
static uint8 cache_line[CACHE_SIZE];

static t_stat ReadICache(t_addr tpc)
{
	int i;
	t_stat rc;
	uint8* mem;

	ASSERT_OKRET(Mem((tpc+CACHE_SIZE)&addrmask,&mem));
	
	/* 68000/08/10 do not like unaligned access */
	if (cputype < 3 && (tpc & 1)) return STOP_ERRADR;
	
	for (i=CACHE_SIZE-1; i>=0; i--) {
		cache_line[i] = *mem--; 
	}
//	for (i=0; i<16; i++) printf("icache[%d]=0x%08x\n",i,cache_line[i]);
	return SCPE_OK;
}

static t_stat ReadInstr(t_addr pc,uint32* inst)
{
	t_stat rc;
	t_addr tpc;
	IOHANDLER* ioh;
	
	if ((rc=TranslateAddr(pc & ~CACHE_MASK,&tpc,&ioh,MEM_READ,FALSE,FALSE)) != SCPE_OK)
		return rc==SIM_ISIO ? STOP_PCIO : rc;
	if (tpc != cache_pc) {
		ASSERT_OKRET(ReadICache(tpc));
	}
	pc &= CACHE_MASK;
	*inst = (cache_line[pc]<<8) | cache_line[pc+1];
	return SCPE_OK;
}

static t_stat ReadInstrInc(t_addr* pc,uint32* inst)
{
	t_stat rc;
	ASSERT_OKRET(ReadInstr(*pc,inst));
	*pc += 2;
	return SCPE_OK;
}

static t_stat ReadInstrLongInc(t_addr* pc,uint32* inst)
{
	t_stat rc;
	uint32 val1,val2;
	ASSERT_OKRET(ReadInstr(*pc,&val1));
	*pc += 2;
	ASSERT_OKRET(ReadInstr(*pc,&val2));
	*pc += 2;
	*inst = COMBINE16(val1<<16,val2);
	return SCPE_OK;
}


void m68k_set_s(t_bool tf)
{
	if (tf) {
		SR |= FLAG_S;
		cur_sp = &A7;
	} else {
		SR &= ~FLAG_S;
		cur_sp = &USP;
	}
}

void m68k_setipl(int ipl)
{
//	printf("set ipl to %d\n",ipl);
	SR &= ~FLAG_IPL_MASK;
	SR |= (ipl & 7) << 8;
}

/* interrupt logic */
static int intvectors[8];

static t_stat m68k_irqinit()
{
	int i;
	for (i=0; i<8; i++) intvectors[i] = 0;
	intpending = 0;
	return SCPE_OK;
}

t_stat m68k_raise_vectorint(int level,int vector)
{
	int mask = 1<<level;
	IFDEBUG(DBG_CPU_INT,fprintf(sim_deb,"CPU : [0x%08x] Interrupt: request level=%d, IPL=%d, vec=%d, pending=%x\n",
			saved_PC,level,SR_IPL,vector,intpending));
	if ((intpending & mask) == 0) {
		intvectors[level] = vector;
		intpending |= mask;
	}
	return SCPE_OK;
}

t_stat m68k_raise_autoint(int level)
{
	return m68k_raise_vectorint(level,level+24);
}

static void m68k_nocallback(DEVICE* dev,int trapnum)
{
	/* do nothing */
}

/* reset and boot */
t_stat m68kcpu_reset(DEVICE* dptr) 
{
	t_stat rc;
	uint32 dummy;
	
	cpudev_self = dptr;

	sim_brk_types = SWMASK('E')|SWMASK('R')|SWMASK('W');
	sim_brk_dflt = SWMASK('E');
	
	addrmask = addrmasks[cputype];

	ASSERT_OKRET(m68k_alloc_mem());
	ASSERT_OKRET(m68k_ioinit());

	m68kcpu_trapcallback = &m68k_nocallback;
	
	m68k_sublevel = 0;
	
	/* TODO: 68010 VBR */
	ReadPL(0,&A7);
	ReadPL(4,&saved_PC);
	ReadInstr(saved_PC,&dummy); /* fill prefetch cache */
	m68k_irqinit();				/* reset interrupt flags */
	m68k_set_s(TRUE);			/* reset to supervisor mode */
	return SCPE_OK;
}

t_stat m68kcpu_boot(int32 unitno,DEVICE* dptr)
{
	return dptr->reset(dptr);
}

/* for instruction decoder */
#define IR_1512		(IR&0170000)
#define IR_1109		(IR&0007000)
#define IR_1108		(IR&0007400)
#define IR_1106		(IR&0007700)
#define IR_1103		(IR&0007770)
#define IR_08		(IR&0000400)
#define IR_0806		(IR&0000700)
#define IR_0803		(IR&0000770)
#define IR_0706		(IR&0000300)
#define IR_0703		(IR&0000370)
#define IR_0503		(IR&0000070)
#define IR_080403	(IR&0000430)
#define IR_08060403	(IR&0000730)
#define IR_0200		(IR&0000007)
#define IR_EAMOD	(IR&0000070)
#define IR_0503		(IR&0000070)
#define IR_COND		(IR&0007400)
#define IR_EA		(IR&0000077)
#define IR_EAM12	(IR&0000060)
#define IR_EAREG	(IR&0000007)
#define IR_DISP		(IR&0000377)
#define IR_EATGT   ((IR&0000700)>>3)
#define IR_REGX	   ((IR&0007000)>>9)
#define IR_REGY		(IR&0000007)
#define IR_TRAP		(IR&0000017)
#define IR_SIZE	   ((IR&0000300)>>6)
#define IR_DATA		(IR&0000377)
#define IRE_DA		(IRE&0100000)
#define IRE_REG	   ((IRE&0070000)>>12)
#define IRE_WL      (IRE&0004000)
#define IRE_DISP	(IRE&0000377)

/* EA modes */
#define EA_DDIR		0000
#define EA_ADIR		0010
#define EA_AIND		0020
#define EA_API		0030
#define EA_APD		0040
#define EA_AIDX		0050
#define EA_AXIDX	0060
#define EA_EXT		0070
#define EA_IMM		0074
#define EAX_AW		000
#define EAX_AL		001
#define EAX_PCIDX	002
#define EAX_PCXIDX	003
#define EAX_IMM		004

#define EXTB(x)		((int32)((int8)((x)&0xff)))
#define EXTW(x)		((int32)((int16)((x)&0xffff)))

#define DRX 		DR[IR_REGX]
#define DRY 		DR[IR_REGY]

static uint32 quickarg[] = { 8,1,2,3,4,5,6,7 };
static int32  shmask8[]  = { 0x00,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff };
static int32  shmask16[] = { 0x0000,
							 0x8000,0xc000,0xe000,0xf000,0xf800,0xfc00,0xfe00,0xff00,
							 0xff80,0xffc0,0xffe0,0xfff0,0xff80,0xffc0,0xffe0,0xffff,
							 0xffff };
static int32  shmask32[] = { 0x00000000,
							 0x80000000,0xc0000000,0xe0000000,0xf0000000,
							 0xf8000000,0xfc000000,0xfe000000,0xff000000,
							 0xff800000,0xffc00000,0xffe00000,0xfff00000,
							 0xfff80000,0xfffc0000,0xfffe0000,0xffff0000,
							 0xffff8000,0xffffc000,0xffffe000,0xfffff000,
							 0xfffff800,0xfffffc00,0xfffffe00,0xffffff00,
							 0xffffff80,0xffffffc0,0xffffffe0,0xfffffff0,
							 0xfffffff8,0xfffffffc,0xfffffffe,0xffffffff,
							 0xffffffff };
static int32  bitmask[]  = { 0x00000000,
							 0x00000001,0x00000002,0x00000004,0x00000008,
							 0x00000010,0x00000020,0x00000040,0x00000080,
							 0x00000100,0x00000200,0x00000400,0x00000800,
							 0x00001000,0x00002000,0x00004000,0x00000800,
							 0x00010000,0x00020000,0x00040000,0x00008000,
							 0x00100000,0x00200000,0x00400000,0x00080000,
							 0x01000000,0x02000000,0x04000000,0x00800000,
							 0x10000000,0x20000000,0x40000000,0x80000000,
							 0x00000000 };

static t_addr saved_ea;

static t_stat ea_src_b(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	t_stat rc = SCPE_OK;
	uint32 reg, regno, IRE;
	t_addr *areg;
//	printf("src eamod=%x eareg=%x\n",eamod,eareg);
	switch (eamod) {
	case EA_DDIR:
		*val = MASK_8L(DR[eareg]);
		return SCPE_OK;
	case EA_ADIR:
		*val = MASK_8L(*AREG(eareg));
		return SCPE_OK;
	case EA_AIND:
		return ReadVB(saved_ea = *AREG(eareg),val);
	case EA_API:
		areg = AREG(eareg);
		rc = ReadVB(saved_ea = *areg,val);
		*areg += (eareg==7 ? 2 : 1);
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= (eareg==7 ? 2 : 1);
		return ReadVB(saved_ea = *areg,val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return ReadVB(saved_ea = *AREG(eareg)+EXTW(IRE),val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return ReadVB(saved_ea = *AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			saved_ea = EXTW(IRE);
			rc = ReadVB(saved_ea, val);
			return rc;
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return ReadVB(saved_ea = IRE, val);
		case EAX_PCIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return ReadVB(saved_ea = *pc-2 + EXTW(IRE), val);
		case EAX_PCXIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			regno = IRE_REG;
			reg = (IRE_DA) ? *AREG(regno) : DR[regno];
			if (!IRE_WL) reg = EXTW(reg);
			return ReadVB(saved_ea = *pc-2 + EXTW(IRE_DISP) + reg, val);
		case EAX_IMM:
			ASSERT_OKRET(ReadInstrInc(pc,val));
			*val = MASK_8L(*val);
			return SCPE_OK;
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_src_bs(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	if (eamod==EA_EXT && eareg==EAX_IMM) {
		*val = MASK_8L(SR);
		return SCPE_OK;
	}
	return ea_src_b(eamod,eareg,val,pc);
}

static t_stat ea_src_w(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	t_stat rc = SCPE_OK;
	uint32 reg, regno, IRE;
	t_addr *areg;
	
	switch (eamod) {
	case EA_DDIR:
		*val = MASK_16L(DR[eareg]);
		return SCPE_OK;
	case EA_ADIR:
		*val = MASK_16L(*AREG(eareg));
		return SCPE_OK;
	case EA_AIND:
		return ReadVW(saved_ea = *AREG(eareg), val);
	case EA_API:
		areg = AREG(eareg);
		rc = ReadVW(saved_ea = *areg, val);
		*areg += 2;
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= 2;
		return ReadVW(saved_ea = *areg, val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return ReadVW(saved_ea = *AREG(eareg) + EXTW(IRE), val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return ReadVW(saved_ea = *AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return ReadVW(saved_ea = EXTW(IRE), val);
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return ReadVW(saved_ea = IRE, val);
		case EAX_PCIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return ReadVW(saved_ea = *pc-2 + EXTW(IRE), val);
		case EAX_PCXIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			regno = IRE_REG;
			reg = (IRE_DA) ? *AREG(regno) : DR[regno];
			if (!IRE_WL) reg = EXTW(reg);
			return ReadVW(saved_ea = *pc-2 + EXTW(IRE_DISP) + reg, val);
		case EAX_IMM:
			return ReadInstrInc(pc,val);
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_src_ws(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	if (eamod==EA_EXT && eareg==EAX_IMM) {
		*val = SR;
		return SCPE_OK;
	}
	return ea_src_w(eamod,eareg,val,pc);
}

/* non dereferencing version of ea_src_l, only accepts ea category control */
static t_stat ea_src_l_nd(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	t_stat rc = SCPE_OK;
	uint32 reg, regno, IRE;
	
	switch (eamod) {
	case EA_AIND:
		*val = *AREG(eareg);
		return SCPE_OK;
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		*val = *AREG(eareg) + EXTW(IRE);
		return SCPE_OK;
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		*val = *AREG(eareg) + EXTW(IRE_DISP) + reg;
		return SCPE_OK;
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			*val = EXTW(IRE);
			return SCPE_OK;
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,val));
			*pc += 4;
			return SCPE_OK;
		case EAX_PCIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			*val = *pc-2 + EXTW(IRE);
			return SCPE_OK;
		case EAX_PCXIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			regno = IRE_REG;
			reg = (IRE_DA) ? *AREG(regno) : DR[regno];
			if (!IRE_WL) reg = EXTW(reg);
			*val = *pc-2 + EXTW(IRE_DISP) + reg;
			return SCPE_OK;
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_src_l(uint32 eamod,uint32 eareg,uint32* val,t_addr* pc)
{
	t_stat rc = SCPE_OK;
	uint32 reg, regno, IRE;
	t_addr *areg;
	
	switch (eamod) {
	case EA_DDIR:
		*val = DR[eareg];
		return SCPE_OK;
	case EA_ADIR:
		*val = *AREG(eareg);
		return SCPE_OK;
	case EA_AIND:
		return ReadVL(saved_ea = *AREG(eareg), val);
	case EA_API:
		areg = AREG(eareg);
		rc = ReadVL(saved_ea = *areg, val);
		*areg += 4;
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= 4;
		return ReadVL(saved_ea = *areg, val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return ReadVL(saved_ea = *AREG(eareg) + EXTW(IRE), val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return ReadVL(saved_ea = *AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return ReadVL(saved_ea = EXTW(IRE), val);
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return ReadVL(saved_ea = IRE, val);
		case EAX_PCIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return ReadVL(saved_ea = *pc-2 + EXTW(IRE), val);
		case EAX_PCXIDX:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			regno = IRE_REG;
			reg = (IRE_DA) ? *AREG(regno) : DR[regno];
			if (!IRE_WL) reg = EXTW(reg);
			return ReadVL(saved_ea = *pc-2 + EXTW(IRE_DISP) + reg, val);
		case EAX_IMM:
			ASSERT_OKRET(ReadVL(*pc,val));
			*pc += 4;
			return SCPE_OK;
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_src_l64(uint32 eamod,uint32 eareg,t_uint64* val64,t_addr* pc)
{
	uint32 val32;
	t_stat rc = ea_src_l(eamod,eareg,&val32,pc);
	*val64 = (t_uint64)val32;
	return rc;
}

t_stat ea_src(uint32 eamod,uint32 eareg,uint32* val,int sz,t_addr* pc)
{
	switch (sz) {
	case SZ_BYTE:
		return ea_src_b(eamod,eareg,val,pc);
	case SZ_WORD:
		return ea_src_w(eamod,eareg,val,pc);
	case SZ_LONG:
		return ea_src_l(eamod,eareg,val,pc);
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_dst_b(uint32 eamod,uint32 eareg,uint32 val,t_addr* pc)
{
	t_stat rc;
	uint32 IRE,reg,regno;
	t_addr *areg;

//	printf("dst: eamod=%x eareg=%x\n",eamod,eareg);
//	printf("val=%x\n",val);
	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = COMBINE8(DR[eareg],val);
		return SCPE_OK;
	case EA_AIND:
		return WriteVB(*AREG(eareg), val);
	case EA_API:
		areg = AREG(eareg);
		rc = WriteVB(*areg, val);
		*areg += (eareg==7 ? 2 : 1);
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= (eareg==7 ? 2 : 1);
		return WriteVB(*areg, val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return WriteVB(*AREG(eareg) + EXTW(IRE), val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return WriteVB(*AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return WriteVB(EXTW(IRE), val);
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return WriteVB(IRE, val);
		default:
			return STOP_ERROP;
		}
	case EA_ADIR:
	default:
		return STOP_ERROP;
	}
}

t_stat ea_dst_b_rmw(uint32 eamod,uint32 eareg,uint32 val)
{
	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = COMBINE8(DR[eareg],val);
		return SCPE_OK;
	case EA_AIND:
	case EA_API:
	case EA_APD:
	case EA_AIDX:
	case EA_AXIDX:
		return WriteVB(saved_ea, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
		case EAX_AL:
			return WriteVB(saved_ea, val);
		case EAX_IMM:
			SR = COMBINE8(SR,val);
			return SCPE_OK;
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_dst_w(uint32 eamod,uint32 eareg,uint32 val,t_addr* pc)
{
	t_stat rc;
	uint32 IRE,reg,regno;
	t_addr *areg;

	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = COMBINE16(DR[eareg],val);
		return SCPE_OK;
	case EA_ADIR:
		*AREG(eareg) = COMBINE16(*AREG(eareg),val);
//		*AREG(eareg) = EXTW(val);
		return SCPE_OK;
	case EA_AIND:
		return WriteVW(*AREG(eareg), val);
	case EA_API:
		areg = AREG(eareg);
		rc = WriteVW(*areg, val);
		*areg += 2;
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= 2;
		return WriteVW(*areg, val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return WriteVW(*AREG(eareg) + EXTW(IRE), val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return WriteVW(*AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return WriteVW(EXTW(IRE), val);
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return WriteVW(IRE, val);
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_dst_w_rmw(uint32 eamod,uint32 eareg,uint32 val)
{
	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = COMBINE16(DR[eareg],val);
		return SCPE_OK;
	case EA_ADIR:
		printf("ea_dst_w_rmw EA_ADIR: pc=%x\n",saved_PC);
		*AREG(eareg) = val; /* use full 32 bits even for word operand */
		return SCPE_OK;
	case EA_AIND:
	case EA_API:
	case EA_APD:
	case EA_AIDX:
	case EA_AXIDX:
		return WriteVW(saved_ea, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
		case EAX_AL:
			return WriteVW(saved_ea, val);
		case EAX_IMM:
			SR = val;
			return SCPE_OK;
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

static t_stat ea_dst_l(uint32 eamod,uint32 eareg,uint32 val,t_addr* pc)
{
	t_stat rc;
	uint32 IRE,reg,regno;
	t_addr *areg;

	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = val;
		return SCPE_OK;
	case EA_ADIR:
		*AREG(eareg) = val;
		return SCPE_OK;
	case EA_AIND:
		return WriteVL(*AREG(eareg), val);
	case EA_API:
		areg = AREG(eareg);
		rc = WriteVL(*areg, val);
		*areg += 4;
		return rc;
	case EA_APD:
		areg = AREG(eareg);
		*areg -= 4;
		return WriteVL(*areg, val);
	case EA_AIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		return WriteVL(*AREG(eareg) + EXTW(IRE), val);
	case EA_AXIDX:
		ASSERT_OKRET(ReadInstrInc(pc,&IRE));
		regno = IRE_REG;
		reg = IRE_DA ? *AREG(regno) : DR[regno];
		if (!IRE_WL) reg = EXTW(reg);
		return WriteVL(*AREG(eareg) + EXTW(IRE_DISP) + reg, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
			ASSERT_OKRET(ReadInstrInc(pc,&IRE));
			return WriteVL(EXTW(IRE), val);
		case EAX_AL:
			ASSERT_OKRET(ReadPL(*pc,&IRE));
			*pc += 4;
			return WriteVL(IRE, val);
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

t_stat ea_dst_l_rmw(uint32 eamod,uint32 eareg,uint32 val)
{

	switch (eamod) {
	case EA_DDIR:
		DR[eareg] = val;
		return SCPE_OK;
	case EA_ADIR:
		*AREG(eareg) = val;
		return SCPE_OK;
	case EA_AIND:
	case EA_API:
	case EA_APD:
	case EA_AIDX:
	case EA_AXIDX:
		return WriteVL(saved_ea, val);
	case EA_EXT:
		switch (eareg) {
		case EAX_AW:
		case EAX_AL:
			return WriteVL(saved_ea, val);
		default:
			return STOP_ERROP;
		}
	default:
		return STOP_ERROP;
	}
}

t_stat ea_dst(uint32 eamod,uint32 eareg,uint32 val,int sz,t_addr* pc)
{
	switch (sz) {
	case SZ_BYTE:
		return ea_dst_b(eamod,eareg,val,pc);
	case SZ_WORD:
		return ea_dst_w(eamod,eareg,val,pc);
	case SZ_LONG:
		return ea_dst_l(eamod,eareg,val,pc);
	default:
		return STOP_ERROP;
	}
}

static t_bool testcond(uint32 c)
{
	int n,v;
	
	switch (c) {
	case 0x0000: /*T*/
		return TRUE;
	case 0x0100: /*F*/
		return FALSE;
	case 0x0200: /*HI*/
		return !(CCR_C || CCR_Z);
	case 0x0300: /*LS*/
		return CCR_C || CCR_Z;
	case 0x0400: /*CC*/
		return !CCR_C;
	case 0x0500: /*CS*/
		return CCR_C;
	case 0x0600: /*NE*/
		return !CCR_Z;
	case 0x0700: /*EQ*/
		return CCR_Z;
	case 0x0800: /*VC*/
		return !CCR_V;
	case 0x0900: /*VS*/
		return CCR_V;
	case 0x0a00: /*PL*/
		return !CCR_N;
	case 0x0b00: /*MI*/
		return CCR_N;
	case 0x0c00: /*GE*/
		n = CCR_N; v = CCR_V;
		return (n && v) || !(n || v); 
	case 0x0d00: /*LT*/
		n = CCR_N; v = CCR_V;
		return (n && !v) || (!n && v); 
	case 0x0e00: /*GT*/
		n = CCR_N; v = CCR_V;
		return !CCR_Z && (n || !v) && (!n || v);
	case 0x0f00: /*LE*/
		n = CCR_N; v = CCR_V;
		return CCR_Z || (!n && v) || (n && !v);
	default: /*notreached*/
		return FALSE;
	}
}

/* push/pop on supervisor sp */
static t_stat m68k_push16(uint32 data)
{
	A7 -= 2;
	return WriteVW(A7,data);
}

static t_stat m68k_push32(uint32 data)
{
	A7 -= 4;
	return WriteVL(A7,data);
}

static t_stat m68k_pop16(uint32* data)
{
	A7 += 2;
	return ReadVW(A7-2,data);
}

static t_stat m68k_pop32(uint32* data)
{
	A7 += 4;
	return ReadVL(A7-4,data);
}

/* push/pop on current sp */
t_stat m68k_cpush16(uint32 data)
{
	*cur_sp -= 2;
	return WriteVW(*cur_sp,data);
}

static t_stat m68k_cpush32(uint32 data)
{
	*cur_sp -= 4;
	return WriteVL(*cur_sp,data);
}

static t_stat m68k_cpop16(uint32* data)
{
	*cur_sp += 2;
	return ReadVW(*cur_sp-2,data);
}

static t_stat m68k_cpop32(uint32* data)
{
	*cur_sp += 4;
	return ReadVL(*cur_sp-4,data);
}

t_stat m68k_gen_exception(int vecno,t_addr* pc)
{
	t_stat rc;
	uint32 dummy;
	t_addr oldpc = *pc;
	char out[20];
	
	/* @TODO VBR! */
	if (cputype<2) {
		ASSERT_OKRET(m68k_push32(*pc));
		ASSERT_OKRET(m68k_push16(SR));
		m68k_set_s(TRUE);
		CLRF(FLAG_T0|FLAG_T1);
	} else {
		/* no support for 68010 and above yet */
		return STOP_IMPL;
	}

	/* set the new PC */
	ASSERT_OKRET(ReadPL(vecno<<2,pc));
	IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: vec=%d to %s\n",oldpc,vecno,m68k_getsym(*pc,XFMT,out)));
	return ReadInstr(*pc,&dummy); /* fill prefetch cache */
}

static uint32 m68k_add8(uint32 src1,uint32 src2,uint32 x)
{
	uint32 res = MASK_8L(src1) + MASK_8L(src2) + x;
	SETNZ8(res);
	SETF(MASK_9(res),FLAG_C|FLAG_X);
	SETV_ADD8(src1,src2,res);
	return res;
}

static uint32 m68k_add16(uint32 src1,uint32 src2,uint32 x,t_bool chgflags)
{
	uint32 res = MASK_16L(src1) + MASK_16L(src2) + x;
	if (chgflags) {
		SETNZ16(res);
		SETF(MASK_17(res),FLAG_C|FLAG_X);
		SETV_ADD16(src1,src2,res);
	}
	return res;
}

static uint32 m68k_add32(t_uint64 src1,t_uint64 src2,t_uint64 x,t_bool chgflags)
{
	t_uint64 resx = MASK_32L(src1) + MASK_32L(src2) + x;
	if (chgflags) {
		SETNZ32(resx);
		SETF(MASK_33(resx),FLAG_C|FLAG_X);
		SETV_ADD32(src1,src2,resx);
	}
	return (uint32)resx;
}

static uint32 m68k_sub8(uint32 dst,uint32 src,uint32 x)
{
	uint32 res = MASK_8L(dst) - MASK_8L(src) - x;
	SETNZ8(res);
	SETF(MASK_9(res),FLAG_C|FLAG_X);
	SETV_SUB8(src,dst,res);
	return res;
}

static uint32 m68k_sub16(uint32 dst,uint32 src,uint32 x,t_bool chgflags)
{
	uint32 res = MASK_16L(dst) - MASK_16L(src) - x;
	if (chgflags) {
		SETNZ16(res);
		SETF(MASK_17(res),FLAG_C|FLAG_X);
		SETV_SUB16(src,dst,res);
	}
	return res;
}

static uint32 m68k_sub32(t_uint64 dst,t_uint64 src, t_uint64 x,t_bool chgflags)
{
	t_uint64 resx = MASK_32L(dst) - MASK_32L(src) - x;
	if (chgflags) {
		SETNZ32(resx);
		SETF(MASK_33(resx),FLAG_C|FLAG_X);
		SETV_SUB32(src,dst,resx);
	}
	return (uint32)resx;
}

static uint32* movem_regs[] = {
		(uint32*)&D0, (uint32*)&D1, (uint32*)&D2, (uint32*)&D3, (uint32*)&D4, (uint32*)&D5, (uint32*)&D6, (uint32*)&D7,
		(uint32*)&A0, (uint32*)&A1, (uint32*)&A2, (uint32*)&A3, (uint32*)&A4, (uint32*)&A5, (uint32*)&A6, 0
};

static t_stat m68k_movem_r_pd(t_addr* areg,uint32 regs,t_bool sz)
{
	int i;
	t_stat rc;
	t_addr ea = *areg;
	movem_regs[15] = cur_sp;
	for (i=0; i<16; i++) {
		if (regs & (1<<i)) {
			if (sz) {
				ea -= 4;
				ASSERT_OK(WriteVL(ea, *movem_regs[15-i]));
			} else {
				ea -= 2;
				ASSERT_OK(WriteVW(ea, *movem_regs[15-i]));
			}
		}
	}
	*areg = ea;
	return SCPE_OK;
}

static t_stat m68k_movem_r_ea(t_addr ea,uint32 regs,t_bool sz)
{
	int i;
	t_stat rc;
	movem_regs[15] = cur_sp;
	for (i=0; i<16; i++) {
		if (regs & (1<<i)) {
			if (sz) {
				ASSERT_OK(WriteVL(ea, *movem_regs[i]));
				ea += 4;
			} else {
				ASSERT_OK(WriteVW(ea, *movem_regs[i]));
				ea += 2;
			}
		}
	}
	return SCPE_OK;
}

static t_stat m68k_movem_pi_r(t_addr* areg,uint32 regs,t_bool sz)
{
	int i;
	t_addr ea = *areg;
	uint32 src;
	t_stat rc;
	movem_regs[15] = cur_sp;
	for (i=0; i<16; i++) {
		if (regs & (1<<i)) {
			if (sz) {
				ASSERT_OK(ReadVL(ea, movem_regs[i]));
				ea += 4;
			} else {
				ASSERT_OK(ReadVW(ea, &src));
				*movem_regs[i] = EXTW(src);
				ea += 2;
			}
		}
	}
	*areg = ea;
	return SCPE_OK;
}

static t_stat m68k_movem_ea_r(t_addr ea,uint32 regs,t_bool sz)
{
	int i;
	uint32 src;
	t_stat rc;
	movem_regs[15] = cur_sp;
	for (i=0; i<16; i++) {
		if (regs & (1<<i)) {
			if (sz) {
				ASSERT_OK(ReadVL(ea, movem_regs[i]));
				ea += 4;
			} else {
				ASSERT_OK(ReadVW(ea, &src));
				*movem_regs[i] = EXTW(src);
				ea += 2;
			}
		}
	}
	return SCPE_OK;
}

static t_stat m68k_divu_w(uint32 divdr,int32* reg, t_addr* pc)
{
	uint32 quo,rem,*dst;

	dst = (uint32*)reg;
	divdr = MASK_16L(divdr);
	if (divdr==0) return m68k_gen_exception(5,pc);

	quo = *dst / divdr;
	rem = (*dst % divdr)<<16;
	if (MASK_16U(quo)) ONEF(FLAG_V);
	else {
		SETNZ16(quo);
		CLRF(FLAG_V|FLAG_C);
		*dst = COMBINE16(rem,quo);
	}
	return SCPE_OK;	
}

static t_stat m68k_divs_w(uint32 divdr,int32* reg, t_addr* pc)
{
	int32 quo,rem,div;
	
	div = EXTW(divdr);
	if (div==0) return m68k_gen_exception(5,pc);
	if (*reg==0x80000000 && div==0xffffffff) {
		CLRF(FLAG_Z|FLAG_N|FLAG_V|FLAG_C);
		*reg = 0;
		return SCPE_OK;
	}
	
	quo = *reg / divdr;
	rem = (*reg % divdr)<<16;
	if (EXTW(quo) == quo) {
		SETNZ16(quo);
		CLRF(FLAG_V|FLAG_C);
		*reg = COMBINE16(rem,quo);
	} else ONEF(FLAG_V);
	return SCPE_OK;
}

static t_bool m68k_checkints(t_addr* pc)
{
	int i;
	if (intpending) {
		for (i=7; i>=1; i--) {
			if (intpending & (1<<i) && (i==7 || i > SR_IPL)) {
				/* found a pending irq at level i, that must be serviced now  */
				IFDEBUG(DBG_CPU_INT,fprintf(sim_deb,"CPU : [0x%08x] Interrupt: granting level=%d, IPL=%d, pending=%x\n",
						*pc,i,SR_IPL,intpending));
				m68k_gen_exception(intvectors[i],pc); /* generate an exception */
				intpending &= ~(1<<i);
				intvectors[i] = 0; /* mark it as handled */
				m68k_setipl(i); /* set new interrupt prio, to leave out lower prio interrupts */
				return TRUE;
			}
		}
	}
	return intpending != 0;
}

/* handle stop instruction */
static t_stat m68k_stop(t_addr* pc)
{
	t_stat rc = SCPE_OK;
	t_addr oldpc = *pc;
	IFDEBUG(DBG_CPU_INT,fprintf(sim_deb,"CPU : [0x%08x] STOP: SR=0x%04x\n",oldpc-4,SR));
	for (;;) {
		/* is there an interrupt above IPL (checked in raise_vectorint) ? */
		if (m68k_checkints(pc)) break;
		
		/* loop until something is happening */
		if (sim_interval <= 0 && (rc=sim_process_event())) break;
		sim_interval--;
	}
	IFDEBUG(DBG_CPU_INT,fprintf(sim_deb,"CPU : [0x%08x] STOP: will continue at 0x%08x intpending=%x rc=%d\n",*pc,oldpc,intpending,rc));
	return rc;
}

t_stat sim_instr()
{
	t_stat rc;
	uint32 IR, IRE, src1, src2, res, ea;
	int32 sres, *reg, cnt;
	t_uint64 resx, srcx1, srcx2;
	t_addr PC, srca, *areg, oldpc;
	t_bool isbsr,iscond;
	uint16 tracet0;
	char out[20];
	
	/* restore state */
	PC = saved_PC;
	rc = 0;
	tracet0 = 0;

	/* the big main loop */
	while (rc == SCPE_OK) {
		saved_PC = PC;
		
		/* expired? */
		if (sim_interval <= 0) {
            if ((rc = sim_process_event())) break;
		}
        /* process breakpoints */
		if (sim_brk_summ && sim_brk_test(PC, E_BKPT_SPC|SWMASK('E'))) {
			rc = STOP_IBKPT;
			break;
		}
		
		/* opcode fetch */
		ASSERT_OK(ReadInstrInc(&PC,&IR));
		IFDEBUG(DBG_CPU_PC,fprintf(sim_deb,"DEBUG(PC): PC=%x IR=%x\n",PC-2,IR));
		
		sim_interval--;
		
		/* now decode instruction */
		switch (IR_1512) {
		/*   15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0   
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   0   0   0 | Opcode    | 0 |Length | effective address     | addi,andi,cmpi,eori,ori,subi
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   0   0   0 | Register  | 1 |Opcode | effective address<>001| bchg,bclr,bset,btst
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   0   0   0 | Register  | 1 |Opcode |  0  0   1 | Register  | movep
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x0000:
			switch (IR_1103) {
			case 0000400: case 0001400: case 0002400: case 0003400:
			case 0004400: case 0005400: case 0006400: case 0007400: /* btst d,d */
				cnt = DRX & 0x1f;
				goto do_btstd;
			case 0004000: /* btst #,d */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				cnt = IRE & 0x1f;
do_btstd:		SETZ32(DRY & bitmask[cnt+1]);
				rc = SCPE_OK; break;
			case 0000420: case 0000430: case 0000440: case 0000450:
			case 0000460: case 0000470:	case 0001420: case 0001430: 
			case 0001440: case 0001450:	case 0001460: case 0001470: 
			case 0002420: case 0002430: case 0002440: case 0002450:
			case 0002460: case 0002470:	case 0003420: case 0003430:
			case 0003440: case 0003450:	case 0003460: case 0003470: 
			case 0004420: case 0004430: case 0004440: case 0004450:
			case 0004460: case 0004470:	case 0005420: case 0005430:
			case 0005440: case 0005450:	case 0005460: case 0005470: 
			case 0006420: case 0006430: case 0006440: case 0006450:
			case 0006460: case 0006470:	case 0007420: case 0007430:
			case 0007440: case 0007450:	case 0007460: case 0007470: /* btst d,ea */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				cnt = DRX & 7;
				goto do_btst8;
			case 0004020: case 0004030: case 0004040: case 0004050:
			case 0004060: case 0004070: /* btst #,ea */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				cnt = IRE & 7;
do_btst8:		SETZ8(src1 & bitmask[cnt+1]);
				rc = SCPE_OK;
				break;

			case 0000700: case 0001700: case 0002700: case 0003700:
			case 0004700: case 0005700: case 0006700: case 0007700: /* bset d,d */
				cnt = DRX & 0x1f;
				src1 = bitmask[cnt+1];
				goto do_bsetd;
			case 0004300: /* bset #,d */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				src1 = bitmask[(IRE & 0x1f)+1];
do_bsetd:		reg = &DRY;
				SETZ32(*reg & src1);
				*reg |= src1;
				rc = SCPE_OK; break;
			case 0000720: case 0000730: case 0000740: case 0000750:
			case 0000760: case 0000770:	case 0001720: case 0001730:
			case 0001740: case 0001750:	case 0001760: case 0001770: 
			case 0002720: case 0002730: case 0002740: case 0002750:
			case 0002760: case 0002770:	case 0003720: case 0003730:
			case 0003740: case 0003750:	case 0003760: case 0003770: 
			case 0004720: case 0004730: case 0004740: case 0004750:
			case 0004760: case 0004770:	case 0005720: case 0005730:
			case 0005740: case 0005750:	case 0005760: case 0005770: 
			case 0006720: case 0006730: case 0006740: case 0006750:
			case 0006760: case 0006770:	case 0007720: case 0007730:
			case 0007740: case 0007750:	case 0007760: case 0007770:	/* bset d,ea */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				cnt = DRY & 7;
				src1 = bitmask[cnt+1];
				goto do_bset8;
			case 0004320: case 0004330: case 0004340: case 0004350:
			case 0004360: case 0004370: /* bset # */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				src1 = bitmask[(IRE&7)+1];
do_bset8:		SETZ8(res & src1);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res | src1); break;
				
			case 0000500: case 0001500: case 0002500: case 0003500:
			case 0004500: case 0005500: case 0006500: case 0007500: /* bchg d,d */
				cnt = DRX & 0x1f;
				src1 = bitmask[cnt+1];
				goto do_bchgd;
			case 0004100: /* bchg #,d */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				src1 = bitmask[(IRE & 0x1f)+1];
do_bchgd:		reg = &DRY;
				SETZ32(*reg & src1);
				*reg ^= src1;
				rc = SCPE_OK; break;
			case 0000520: case 0000530: case 0000540: case 0000550:
			case 0000560: case 0000570: case 0001520: case 0001530:
			case 0001540: case 0001550:	case 0001560: case 0001570: 
			case 0002520: case 0002530: case 0002540: case 0002550:
			case 0002560: case 0002570:	case 0003520: case 0003530:
			case 0003540: case 0003550:	case 0003560: case 0003570: 
			case 0004520: case 0004530: case 0004540: case 0004550:
			case 0004560: case 0004570:	case 0005520: case 0005530:
			case 0005540: case 0005550:	case 0005560: case 0005570: 
			case 0006520: case 0006530: case 0006540: case 0006550:
			case 0006560: case 0006570: case 0007520: case 0007530:
			case 0007540: case 0007550:	case 0007560: case 0007570: /* bchg d,ea */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				cnt = DRX & 7;
				src1 = bitmask[cnt+1];
				goto do_bchg8;
			case 0004120: case 0004130: case 0004140: case 0004150:
			case 0004160: case 0004170: /* bchg #,ea */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				src1 = bitmask[(IRE&7)+1];
do_bchg8:		SETZ8(res & src1);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res ^ src1); break;

			case 0000600: case 0001600: case 0002600: case 0003600:
			case 0004600: case 0005600: case 0006600: case 0007600: /* bclr d,d */ 
				cnt = DRX & 0x1f;
				src1 = bitmask[cnt+1];
				goto do_bclrd;
			case 0004200: /* bclr #,d */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				src1 = bitmask[(IRE & 0x1f)+1];
do_bclrd:		reg = &DRY;
				SETZ32(*reg & src1);
				*reg &= ~src1;
				rc = SCPE_OK; break;
			case 0000620: case 0000630: case 0000640: case 0000650:
			case 0000660: case 0000670:	case 0001620: case 0001630:
			case 0001640: case 0001650:	case 0001660: case 0001670:
			case 0002620: case 0002630: case 0002640: case 0002650:
			case 0002660: case 0002670:	case 0003620: case 0003630:
			case 0003640: case 0003650:	case 0003660: case 0003670:
			case 0004620: case 0004630: case 0004640: case 0004650:
			case 0004660: case 0004670:	case 0005620: case 0005630:
			case 0005640: case 0005650:	case 0005660: case 0005670:
			case 0006620: case 0006630: case 0006640: case 0006650:
			case 0006660: case 0006670:	case 0007620: case 0007630:
			case 0007640: case 0007650:	case 0007660: case 0007670:	/* bclr d,ea */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				cnt = DRX & 7;
				src1 = bitmask[cnt+1];
				goto do_bclr8;
			case 0004220: case 0004230: case 0004240: case 0004250:
			case 0004260: case 0004270: /* bclr #,ea */
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&res,&PC));
				src1 = bitmask[(IRE&7)+1];
do_bclr8:		SETZ8(res & src1);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res & ~src1); break;
				
			case 0000410: case 0001410: case 0002410: case 0003410:
			case 0004410: case 0005410: case 0006410: case 0007410: /*movep.w m,r*/
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				ASSERT_OK(ReadVB(srca,&src1));
				reg = &DRX;
				*reg = src1<<8;
				rc = ReadVB(srca+2,&src1);
				*reg = COMBINE8(*reg,src1);
				break;
			case 0000510: case 0001510: case 0002510: case 0003510:
			case 0004510: case 0005510: case 0006510: case 0007510: /*movep.l m,r*/
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				ASSERT_OK(ReadVB(srca,&src1));
				reg = &DRX;
				*reg = src1<<8;
				ASSERT_OK(ReadVB(srca+2,&src1));
				*reg = (COMBINE8(*reg,src1))<<8;
				ASSERT_OK(ReadVB(srca+4,&src1));
				*reg = (COMBINE8(*reg,src1))<<8;
				rc = ReadVB(srca+6,&src1);
				*reg = (COMBINE8(*reg,src1))<<8;
				break;
			case 0000610: case 0001610: case 0002610: case 0003610:
			case 0004610: case 0005610: case 0006610: case 0007610: /*movep.w r,m*/
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				src1 = DRX;
				ASSERT_OK(WriteVB(srca,src1>>8));
				rc = WriteVB(srca+2,src1); break;
			case 0000710: case 0001710: case 0002710: case 0003710:
			case 0004710: case 0005710: case 0006710: case 0007710: /*movep.l r,m*/
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				ASSERT_OK(WriteVB(srca,src1>>24));
				ASSERT_OK(WriteVB(srca+2,src1>>16));
				ASSERT_OK(WriteVB(srca+4,src1>>8));
				rc = WriteVB(srca+6,src1); break;
			
			case 0000000: case 0000020: case 0000030: case 0000040:
			case 0000050: case 0000060: case 0000070: /*ori.b*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_bs(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | src2;
				if (IR_EA != EA_IMM) {
					SETNZ8(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0000100: case 0000120: case 0000130: case 0000140:
			case 0000150: case 0000160: case 0000170: /*ori.w*/
				if (IR_EA == EA_IMM) ASSERT_PRIV();
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_ws(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | src2;
				if (IR_EA != EA_IMM) {
					SETNZ16(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0000200: case 0000220: case 0000230: case 0000240:
			case 0000250: case 0000260: case 0000270: /*ori.l*/
				ASSERT_OK(ReadInstrLongInc(&PC,&src2));
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | src2;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res); break;

			case 0001000: case 0001020: case 0001030: case 0001040:
			case 0001050: case 0001060: case 0001070: /*andi.b*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_bs(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & src2;
				if (IR_EA != EA_IMM) {
					SETNZ8(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0001100: case 0001120: case 0001130: case 0001140:
			case 0001150: case 0001160: case 0001170: /*andi.w*/
				if (IR_EA==EA_IMM) ASSERT_PRIV();
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_ws(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & src2;
				if (IR_EA != EA_IMM) {
					SETNZ16(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0001200: case 0001220: case 0001230: case 0001240:
			case 0001250: case 0001260: case 0001270: /*andi.l*/
				ASSERT_OK(ReadInstrLongInc(&PC,&src2));
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & src2;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;

			case 0006000: case 0006020: case 0006030: case 0006040:
			case 0006050: case 0006060: case 0006070: /*cmpi.b*/
			case 0002000: case 0002020: case 0002030: case 0002040:
			case 0002050: case 0002060: case 0002070: /*subi.b*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub8(src1,src2,0);
				rc = IR_1103 < 0006000 ? ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res) : SCPE_OK; 
				break;
			case 0006100: case 0006120: case 0006130: case 0006140:
			case 0006150: case 0006160: case 0006170: /*cmpi.w*/
			case 0002100: case 0002120: case 0002130: case 0002140:
			case 0002150: case 0002160: case 0002170: /*subi.w*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub16(src1,src2,0,TRUE);
				rc = IR_1103 < 0006000 ? ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res) : SCPE_OK; 
				break;
			case 0006200: case 0006220: case 0006230: case 0006240:
			case 0006250: case 0006260: case 0006270: /*cmpi.l*/
			case 0002200: case 0002220: case 0002230: case 0002240:
			case 0002250: case 0002260: case 0002270: /*subi.l*/
				ASSERT_OK(ReadInstrLongInc(&PC,&src2));
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_sub32(srcx1,(t_uint64)src2,0,TRUE);
				rc = IR_1103 < 0006000 ? ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res) : SCPE_OK; 
				break;

			case 0003000: case 0003020: case 0003030: case 0003040:
			case 0003050: case 0003060: case 0003070: /*addi.b*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add8(src1,src2,0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res); break;
			case 0003100: case 0003120: case 0003130: case 0003140:
			case 0003150: case 0003160: case 0003170: /*addi.w*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add16(src1,src2,0,TRUE);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res); break;
			case 0003200: case 0003220: case 0003230: case 0003240:
			case 0003250: case 0003260: case 0003270: /*addi.l*/
				ASSERT_OK(ReadInstrLongInc(&PC,&src2));
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_add32(srcx1,(t_uint64)src2,0,TRUE);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0005000: case 0005020: case 0005030: case 0005040:
			case 0005050: case 0005060: case 0005070: /*eori.b*/
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_bs(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ src2;
				if (IR_EA != EA_IMM) {
					SETNZ8(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0005100: case 0005120: case 0005130: case 0005140:
			case 0005150: case 0005160: case 0005170: /*eori.w*/
				if (IR_EA==EA_IMM) ASSERT_PRIV();
				ASSERT_OK(ReadInstrInc(&PC,&src2));
				ASSERT_OK(ea_src_ws(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ src2;
				if (IR_EA != EA_IMM) {
					SETNZ16(res);
					CLRF(FLAG_C|FLAG_V);
				}
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				tracet0 = SR_T0; break;
			case 0005200: case 0005220: case 0005230: case 0005240:
			case 0005250: case 0005260: case 0005270: /*eori.l*/
				ASSERT_OK(ReadInstrLongInc(&PC,&src2));
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ src2;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res); break;

			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   0 |Length2| TargetReg | TargetMode| SourceMode| SourceReg | move
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   0 |Length2| TargetReg | 0   0   1 | SourceMode| SourceReg | movea
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x1000:
			ea = IR_EATGT;
			ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
			if  (ea == EA_ADIR)
				rc = STOP_ERROP; /* movea.b */
			else {
				ASSERT_OK(ea_dst_b(ea,IR_REGX,src1,&PC));
				SETNZ8(src1);
			}
			break;
		case 0x2000:
			ea = IR_EATGT;
			ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
			if (ea==EA_ADIR) { /* movea.l */
				*AREG(IR_REGX) = src1;
				rc = SCPE_OK;
			} else {
				rc = ea_dst_l(ea,IR_REGX,src1,&PC);
				SETNZ32(src1);
			}
			break;
		case 0x3000:
			ea = IR_EATGT;
			ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
			if (ea==EA_ADIR) { /* movea.w */
				*AREG(IR_REGX) = EXTW(src1);
				rc = SCPE_OK;
			} else {
				rc = ea_dst_w(ea,IR_REGX,src1,&PC);
				SETNZ16(src1);
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Length | effective address     | clr,neg,negx,not,tst
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 | 1   1 | effective address     | moveccr,movesr
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Mode   | 0   0   0 | Register  | ext
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Opcode | effective address     | jmp,jsr,movem,nbcd,pea,tas
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Opcode         |   Vector      | trap
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Opcode             | Register  | link,moveusp,swap,unlink
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Opcode    | 0 |Opcode                         | illegal,nop,reset,rte,rtr,rts,stop,trapv
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   0 | Register  | 1 |Opcode | effective address     | chk,lea
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x4000:
			switch (IR_1106) {
			case 000600: case 001600: case 002600: case 003600:
			case 004600: case 005600: case 006600: case 007600: /*chk*/
				src1 = DRX;
				SETF(src1 < 0,FLAG_N);
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&res,&PC));
				rc = CCR_N || src1 > res ? m68k_gen_exception(6,&PC) : SCPE_OK;
				break;
			case 000700: case 001700: case 002700: case 003700:
			case 004700: case 005700: case 006700: case 007700: /*lea*/
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				*AREG(IR_REGX) = srca;
				rc = SCPE_OK;
				break;
				
			case 000300: /*move from sr*/
				rc = ea_dst_w(IR_EAMOD,IR_EAREG,SR,&PC);
				break;

			case 001000: /*clr.b*/
				ONEF(FLAG_Z);
				CLRF(FLAG_N|FLAG_C|FLAG_V);
				rc = ea_dst_b(IR_EAMOD,IR_EAREG,0,&PC);
				break;
			case 001100: /*clr.w*/
				ONEF(FLAG_Z);
				CLRF(FLAG_N|FLAG_C|FLAG_V);
				rc = ea_dst_w(IR_EAMOD,IR_EAREG,0,&PC);
				break;
			case 001200: /*clr.l*/
				ONEF(FLAG_Z);
				CLRF(FLAG_N|FLAG_C|FLAG_V);
				rc = ea_dst_l(IR_EAMOD,IR_EAREG,0,&PC);
				break;

			case 000000: /*negx.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				src1 += (CCR_X ? 1 : 0);
				goto do_neg8;
			case 002000: /*neg.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
do_neg8:		res = m68k_sub8(0,src1,0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;

			case 000100: /*negx.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				src1 += (CCR_X ? 1 : 0);
				goto do_neg16;
			case 002100: /*neg.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
do_neg16:		res = m68k_sub16(0,src1,0,TRUE);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				break;

			case 000200: /*negx.l*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				srcx1 = (t_uint64)src1 + (CCR_X ? 1 : 0);
				goto do_neg32;
			case 002200: /*neg.l*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
do_neg32:		res = m68k_sub32(0,srcx1,0,TRUE);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;

			case 002300: /*move to ccr*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				SR = COMBINE8(SR,src1);
				break;						

			case 003000: /*not.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = ~src1;
				SETNZ8(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 003100: /*not.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = ~src1;
				SETNZ16(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				break;
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
			case 003200: /*not.l*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = ~src1;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;

			case 003300: /*move to sr*/
				ASSERT_PRIV();
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				SR = src1;
				tracet0 = SR_T0;
				break;

			case 004000:  /*nbcd*/
				rc = STOP_IMPL;
				break;
				
			case 004100: /*pea or swap*/
				if (IR_0503==000) { /*swap*/
					reg = &DRY;
					src1 = *reg << 16;
					res = *reg >> 16;
					*reg = COMBINE16(src1,res);
					SETNZ32(*reg);
					CLRF(FLAG_C|FLAG_V);
					rc = SCPE_OK;
				} else { /*pea*/
					ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
					ASSERT_OK(m68k_cpush32(srca));
				}
				break;
			case 004200: /*movem.w or ext*/
				if (IR_0503==000) { /*ext.w*/
					reg = &DRY;
					res = EXTB(*reg);
					*reg = COMBINE16(*reg,res);
					SETNZ16(res);
					CLRF(FLAG_C|FLAG_V);
					rc = SCPE_OK;
				} else { /*movem.w regs,ea*/
					ASSERT_OK(ReadInstrInc(&PC,&IRE));
					if (IR_EAMOD==EA_APD)
						rc = m68k_movem_r_pd(AREG(IR_REGY),IRE,FALSE);
					else {
						ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
						rc = m68k_movem_r_ea(srca,IRE,FALSE);
					}
				}
				break;
			case 004300: /*movem or ext*/
				if (IR_0503==000) { /*ext.l*/
					reg = &DRY;
					*reg = EXTW(*reg);
					SETNZ32(*reg);
					CLRF(FLAG_C|FLAG_V);
					rc = SCPE_OK;
				} else { /*movem.l regs,ea */
					ASSERT_OK(ReadInstrInc(&PC,&IRE));
					if (IR_EAMOD==EA_APD)
						rc = m68k_movem_r_pd(AREG(IR_REGY),IRE,TRUE);
					else {
						ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
						rc = m68k_movem_r_ea(srca,IRE,TRUE);
					}
				}
				break;
			case 005000: /*tst.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				SETNZ8(src1);
				CLRF(FLAG_V|FLAG_C);
				break;
			case 005100: /*tst.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				SETNZ16(src1);
				CLRF(FLAG_V|FLAG_C);
				break;
			case 005200: /*tst.l*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				SETNZ32(src1);
				CLRF(FLAG_V|FLAG_C);
				break;
				
			case 005300: /*tas or illegal*/
				if (IR==045374) { /*illegal*/
					rc = STOP_ERROP;
				} else { /*tas*/
					rc = STOP_IMPL;
				}
				break;
			case 006200: /*movem.w ea,regs*/
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				if (IR_EAMOD==EA_API)
					rc = m68k_movem_pi_r(AREG(IR_REGY),IRE,FALSE);
				else {
					ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
					rc = m68k_movem_ea_r(srca,IRE,FALSE);
				}
				break;
			case 006300: /*movem.l ea,regs*/
				ASSERT_OK(ReadInstrInc(&PC,&IRE));
				if (IR_EAMOD==EA_API)
					rc = m68k_movem_pi_r(AREG(IR_REGY),IRE,TRUE);
				else {
					ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
					rc = m68k_movem_ea_r(srca,IRE,TRUE);
				}
				break;
			case 007100:
				switch(IR_0503) {
				case 000000:
				case 000010: /*trap*/
					(*m68kcpu_trapcallback)(m68kcpu_dev,IR_TRAP);
					rc = m68k_gen_exception(32+IR_TRAP,&PC);
					break;
				case 000020: /*link*/
					ASSERT_OK(ReadInstrInc(&PC,&IRE));
					if (IR_REGY==7) {
						*cur_sp -= 4;
						ASSERT_OK(WriteVL(*cur_sp,*cur_sp));
					} else {
						areg = AREG(IR_REGY);
						ASSERT_OK(m68k_cpush32(*areg));
						*areg = *cur_sp;
					}
					*cur_sp += EXTW(IRE);
					break;
				case 000030: /*unlk*/
					if (IR_REGY==7) {
						ASSERT_OK(ReadVL(*cur_sp,&srca));
						*cur_sp = srca;
					} else {
						areg = AREG(IR_REGY);
						*cur_sp = *areg;
						ASSERT_OK(m68k_cpop32(areg));
					}
					break;
				case 000040: /*move to usp*/
					ASSERT_PRIV();
					USP = AR[IR_REGY];
					tracet0 = SR_T0;
					rc = SCPE_OK;
					break;
				case 000050: /*move from usp*/
					ASSERT_PRIV();
					AR[IR_REGY] = USP;
					rc = SCPE_OK;
					break;
				case 000060:
					switch(IR_0200)	{
					case 000000: /*reset*/
						ASSERT_PRIV();
						rc = m68kcpu_peripheral_reset();
						break;
					case 000001: /*nop*/
						rc = SCPE_OK; 
						tracet0 = SR_T0;
						break;
					case 000002: /*stop*/
						ASSERT_PRIV();
						ASSERT_OKRET(ReadInstrInc(&PC,&IRE));
						SR = (uint16)IRE;
						rc = STOP_HALT;
						tracet0 = SR_T0;
						break;
					case 000003: /*rte*/
						ASSERT_PRIV();
						ASSERT_OK(m68k_pop16(&src1));
						SR = src1;
						m68k_set_s(SR_S != 0);
						oldpc = PC;
						rc = m68k_pop32(&PC);
						tracet0 = SR_T0;
						IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] RTE to 0x%08x, IPL=%d S=%d\n",
								oldpc-2,PC,SR_IPL,SR_S?1:0));
						break;
					case 000005: /*rts*/
						oldpc = PC;
						rc = m68k_cpop32(&PC);
						m68k_sublevel--;
						IFDEBUG(DBG_CPU_CTRACE,fprintf(sim_deb,"CPU : [0x%08x] <<< RTS to 0x%08x (level=%d)\n",
								oldpc-2,PC,m68k_sublevel));
						tracet0 = SR_T0;
						break;
					case 000006: /*trapv*/
						rc = CCR_V ? m68k_gen_exception(7,&PC) : SCPE_OK;
						break;
					case 000007: /*rtr*/
						ASSERT_OK(m68k_cpop16(&src1));
						SR = COMBINE8(SR,src1);
						oldpc = PC;
						rc = m68k_cpop32(&PC);
						tracet0 = SR_T0;
						IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] RTR to 0x%08x\n",oldpc-2,PC));
						break;
					default:
						rc = STOP_ERROP;
					}
					break;
				default:
					rc = STOP_ERROP;
				}
				break;
			case 007200: /*jsr*/
				oldpc = PC;
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				ASSERT_OK(m68k_cpush32(PC));
				IFDEBUG(DBG_CPU_CTRACE,fprintf(sim_deb,"CPU : [0x%08x] >>> JSR %s (level=%d)\n",
						oldpc-2,m68k_getsym(srca,XFMT,out),m68k_sublevel));
				PC = srca;
				m68k_sublevel++;
				tracet0 = SR_T0;
				break;
			case 007300: /*jmp*/
				oldpc = PC;
				ASSERT_OK(ea_src_l_nd(IR_EAMOD,IR_EAREG,&srca,&PC));
				IFDEBUG(DBG_CPU_BTRACE,fprintf(sim_deb,"CPU : [0x%08x] ||| JMP %s\n",
						oldpc-2,m68k_getsym(srca,XFMT,out)));
				PC = srca;
				tracet0 = SR_T0;
				break;
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   1 | Quickdata |Opc|Length | effective address<>001| addq,subq
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   1 | Condition     | 1   1   0   0   1 | Register  | dbcc
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   0   1 | Condition     | 1   1 | effective address<>001| scc
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x5000:
			switch (IR_0806) {
			case 0000300:
			case 0000700:
				if (IR_0503==010) { /*dbcc*/
					if (!IR_COND || !testcond(IR_COND)) { /* dbt is a NOP */
						reg = &DRY;
						src1 = MASK_16L((*reg-1));
						*reg = MASK_16U(*reg) | src1;
						if (src1 != 0xffff) {
							ASSERT_OK(ReadInstr(PC,&IRE));
							PC += (EXTW(IRE));
							rc = SCPE_OK;
							tracet0 = SR_T0;
							break;
						} /* else loop terminated */
					}
					/* loop cond not met or dbt */
					PC += 2;
					rc = SCPE_OK;
				} else { /*scc*/
					src1 = testcond(IR_COND) ? 0xff : 0x00;
					rc = ea_dst_b(IR_EAMOD,IR_EAREG,src1,&PC);
				}
				break;
			case 0000000: /*addq.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add8(src1,quickarg[IR_REGX],0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000100: /*addq.w*/
				if (IR_EAMOD == EA_ADIR) {
					*AREG(IR_REGY) += EXTW(quickarg[IR_REGX]);
					rc = SCPE_OK;
				} else {
					ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
					res = m68k_add16(src1,quickarg[IR_REGX],0,TRUE);
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				}
				break;
			case 0000200: /*addq.l*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_add32(srcx1,(t_uint64)quickarg[IR_REGX],0,IR_EAMOD!=EA_ADIR);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000400: /*subq.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub8(src1,quickarg[IR_REGX],0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000500: /*subq.w*/
				if (IR_EAMOD == EA_ADIR) {
					*AREG(IR_REGY) -= EXTW(quickarg[IR_REGX]);
					rc = SCPE_OK;
				} else {
					ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
					res = m68k_sub16(src1,quickarg[IR_REGX],0,TRUE);
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				}
				break;
			case 0000600: /*subq.l*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_sub32(srcx1,(t_uint64)quickarg[IR_REGX],0,IR_EAMOD!=EA_ADIR);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   1   0 | Condition     |   Displacement                | Bcc,bra,bsr
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x6000:
			isbsr = IR_COND==0x100; /* is bsr */
			iscond = isbsr || testcond(IR_COND); /* condition matched */
			if (IR_DISP) {
				if (iscond) {
					if (isbsr) {
						IFDEBUG(DBG_CPU_CTRACE,fprintf(sim_deb,"CPU : [0x%08x] >>> BSR %s (level=%d\n",
								PC-2,m68k_getsym(PC+EXTB(IR_DISP),XFMT,out),m68k_sublevel));
						ASSERT_OK(m68k_cpush32(PC)); /* save PC for BSR */
						m68k_sublevel++;
					} else {
						IFDEBUG(DBG_CPU_BTRACE,fprintf(sim_deb,"CPU : [0x%08x] ||| B%s %s\n",
								PC-2,condnames[IR_COND>>8],m68k_getsym(PC+EXTB(IR_DISP),XFMT,out)));
					}
					PC += EXTB(IR_DISP); /* go to new location */
				} /* else condition not matched */
			} else { /* 16 bit ext word */
				if (iscond) {
					ASSERT_OK(ReadInstr(PC,&IRE)); /* get extension word */
					if (isbsr) {
						IFDEBUG(DBG_CPU_CTRACE,fprintf(sim_deb,"CPU : [0x%08x] >>> BSR %s (level=%d)\n",
								PC-2,m68k_getsym(PC+EXTW(IRE),XFMT,out),m68k_sublevel));
						ASSERT_OK(m68k_cpush32(PC+2)); /* save PC for BSR */
						m68k_sublevel++;
					} else {
						IFDEBUG(DBG_CPU_BTRACE,fprintf(sim_deb,"CPU : [0x%08x] ||| B%s %s\n",
								PC-2,condnames[IR_COND>>8],m68k_getsym(PC+EXTW(IRE),XFMT,out)));
					}
					PC += EXTW(IRE); /* go to new location */
				} else {
					PC += 2; /* condition not matched */
				}
			}
			tracet0 = SR_T0;
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 0   1   1   1 | Register  | 0 |   Data                        | moveq
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x7000:
			src1 = DRX = EXTB(IR_DATA);
			SETNZ32(src1);
			CLRF(FLAG_C|FLAG_V);
			rc = SCPE_OK; break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   0 | Register  |Opc|Length | effective address<>00x| or
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   0 |   Reg X   | 1   0   0 | 0   0 |R/M|  Reg Y    | sbcd
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   0 | Register  |Opc| 1   1 | effective address     | divs,divu
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x8000:
			switch(IR_0803) {
			case 0000300: case 0000320: case 0000330: case 0000340:
			case 0000350: case 0000360: case 0000370: /*divu.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				rc = m68k_divu_w(src1,&DR[IR_REGX], &PC);
				break;
			case 0000700: case 0000720: case 0000730: case 0000740:
			case 0000750: case 0000760: case 0000770: /*divs.w*/
				rc = m68k_divs_w(src1,&DR[IR_REGX], &PC);
				break;
			case 0000400: /*sbcd d*/
				rc = STOP_IMPL; break;
			case 0000410: /*sbcd a*/
				rc = STOP_IMPL; break;
			case 0000000: case 0000020: case 0000030: case 0000040:
			case 0000050: case 0000060: case 0000070: /*or.b ->d*/	
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = MASK_8L(src1 | DRX);
				SETNZ8(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000100: case 0000120: case 0000130: case 0000140:
			case 0000150: case 0000160: case 0000170: /*or.w ->d*/	
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = MASK_16L(src1 | DRX);
				SETNZ16(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000200: case 0000220: case 0000230: case 0000240:
			case 0000250: case 0000260: case 0000270: /*or.l ->d*/	
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & DRX;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000420: case 0000430: case 0000440: case 0000450:
			case 0000460: case 0000470: /*or.b ->ea*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | DRX;
				SETNZ8(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			case 0000520: case 0000530: case 0000540: case 0000550:
			case 0000560: case 0000570: /*or.w ->ea*/	
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | DRX;
				SETNZ16(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			case 0000620: case 0000630: case 0000640: case 0000650:
			case 0000660: case 0000670: /*or.l ->ea*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 | DRX;
				SETNZ32(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			default:
				rc = STOP_ERROP; break;		
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   1 | Register  |Opc|Length | effective address<>00x| sub
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   1 | Register  |Opc| 1   1 | effective address     | suba
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   0   1 |   Reg X   | 1 |Length | 0   0 |R/M|  Reg Y    | subx
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0x9000:
			switch (IR_0803) {
			case 0000300: case 0000310: case 0000320: case 0000330:
			case 0000340: case 0000350: case 0000360: case 0000370: /* suba.w */
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&srca,&PC));
				*AREG(IR_REGX) -= EXTW(srca); /* note: no flag changes! */
				break;
			case 0000700: case 0000710: case 0000720: case 0000730:
			case 0000740: case 0000750: case 0000760: case 0000770: /* suba.l */
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&srca,&PC));
				*AREG(IR_REGX) -= srca; /* note: no flag changes! */
				break;
			case 0000400: /*subx.b d*/
				res = m68k_sub8(MASK_8L(DRY),DRX,CCR_X?1:0);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000410: /*subx.b -a*/
				ASSERT_OK(ea_src_b(EA_APD,IR_REGY,&src1,&PC));
				ASSERT_OK(ea_src_b(EA_APD,IR_REGX,&src2,&PC));
				res = m68k_sub8(src1,src2,CCR_X?1:0);
				rc = ea_dst_b_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000500: /*subx.w d*/
				res = m68k_sub16(MASK_16L(DRY),DRX,CCR_X?1:0,TRUE);
				rc = ea_dst_w(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000510: /*subx.w -a*/
				ASSERT_OK(ea_src_w(EA_APD,IR_REGY,&src1,&PC));
				ASSERT_OK(ea_src_w(EA_APD,IR_REGX,&src2,&PC));
				res = m68k_sub16(src1,src2,CCR_X?1:0,TRUE);
				rc = ea_dst_w_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000600: /*subx.l d*/
				res = m68k_sub32((t_uint64)DRY,(t_uint64)DRX,CCR_X?1:0,TRUE);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000610: /*subx.l -a*/
				ASSERT_OK(ea_src_l64(EA_APD,IR_REGY,&srcx1,&PC));
				ASSERT_OK(ea_src_l64(EA_APD,IR_REGX,&srcx2,&PC));
				res = m68k_sub32(srcx1,srcx2,CCR_X?1:0,TRUE);
				rc = ea_dst_l_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000000: case 0000020: case 0000030: case 0000040:
			case 0000050: case 0000060: case 0000070: 				/* sub.b ->d */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub8(DRX,src1,0);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000100: case 0000110: case 0000120: case 0000130:
			case 0000140: case 0000150: case 0000160: case 0000170: /* sub.w ->d */
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub16(DRX,src1,0,TRUE);
				rc = ea_dst_w(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000200: case 0000210: case 0000220: case 0000230:
			case 0000240: case 0000250: case 0000260: case 0000270: /* sub.l ->d */
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_sub32((t_uint64)DRX,srcx1,0,TRUE);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000420: case 0000430: case 0000440: case 0000450:
			case 0000460: case 0000470: 							/* sub.b ->ea */
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub8(src1,DRX,0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000520: case 0000530: case 0000540: case 0000550:
			case 0000560: case 0000570: 							/* sub.w ->ea */
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_sub16(src1,DRX,0,TRUE);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000620: case 0000630: case 0000640: case 0000650:
			case 0000660: case 0000670: 							/* sub.l ->ea */
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_sub32(srcx1,(t_uint64)DRX,0,TRUE);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   1   0 | Opcode                                        | trapa
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xa000:
			rc = m68k_gen_exception(10,&PC);
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   1   1 | Register  | 0 |Length | effective address     | cmp,cmpa
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   1   1 | Register  | 1 |Length | effective address<>001| eor
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   0   1   1 |   Reg X   | 1 |Length | 0   0   1 |  Reg Y    | cmpm
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xb000:
			switch (IR_0803) {
			case 0000410: /*cmpm.b*/
				rc = STOP_IMPL; break;
			case 0000510: /*cmpm.w*/
				rc = STOP_IMPL; break;
			case 0000610: /*cmpm.l*/
				rc = STOP_IMPL; break;
			case 0000400: case 0000420: case 0000430: case 0000440:
			case 0000450: case 0000460: case 0000470: /*eor.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ DRX;
				SETNZ8(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			case 0000500: case 0000520: case 0000530: case 0000540:
			case 0000550: case 0000560: case 0000570: /*eor.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ DRX;
				SETNZ16(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			case 0000600: case 0000620: case 0000630: case 0000640:
			case 0000650: case 0000660: case 0000670: /*eor.l*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 ^ DRX;
				SETNZ32(res);
				CLRF(FLAG_V|FLAG_C);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);				
				break;
			case 0000000: case 0000020: case 0000030: case 0000040:
			case 0000050: case 0000060: case 0000070: /*cmp.b*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				(void)m68k_sub8(DRX,src1,0);
				break;
			case 0000100: case 0000110: case 0000120: case 0000130:
			case 0000140: case 0000150: case 0000160: case 0000170: /*cmp.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				(void)m68k_sub16(DRX,src1,0,TRUE);
				break;
			case 0000200: case 0000210: case 0000220: case 0000230:
			case 0000240: case 0000250: case 0000260: case 0000270: /*cmp.l*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				(void)m68k_sub32((t_uint64)DRX,srcx1,0,TRUE);
				break;
			case 0000300: case 0000310: case 0000320: case 0000330:
			case 0000340: case 0000350: case 0000360: case 0000370: /*cmpa.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				areg = AREG(IR_REGX);
				(void)m68k_sub32((t_uint64)EXTW(*areg),(t_uint64)src1,0,TRUE);
				break;

			case 0000700: case 0000710: case 0000720: case 0000730:
			case 0000740: case 0000750: case 0000760: case 0000770: /*cmpa.l*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				(void)m68k_sub32((t_uint64)*AREG(IR_REGX),srcx1,0,TRUE);
				break;
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   0 | Register  |Opc|Length | effective address<>00x| and
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   0 |   Reg X   | 1   0   0 | 0   0 |R/M|  Reg Y    | abcd
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   0 |   Reg X   | 1 |Opcode | 0   0 |Opc|  Reg Y    | exg
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   0 | Register  |Opc| 1   1 | effective address     | muls,mulu
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xc000:
			switch(IR_0803) {
			case 0000300: case 0000310: case 0000320: case 0000330: 
			case 0000340: case 0000350: case 0000360: case 0000370:	/*mulu*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = (uint16)MASK_16L(src1) * (uint16)MASK_16L(DRX);
				DRX = res;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				break;
			case 0000700: case 0000710: case 0000720: case 0000730: 
			case 0000740: case 0000750: case 0000760: case 0000770:	/*muls*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				sres = (int16)MASK_16L(src1) * (int16)MASK_16L(DRX);
				DRX = (uint32)sres;
				SETNZ32(sres);
				CLRF(FLAG_C|FLAG_V);
				break;
			case 0000500: /* exg d,d */
				res = DRX; DRX = DRY; DRY = res;
				rc = SCPE_OK; break;
			case 0000510: /* exg a,a */
				srca = *AREG(IR_REGX); *AREG(IR_REGX) = *AREG(IR_REGY); *AREG(IR_REGY) = srca;
				rc = SCPE_OK; break;
			case 0000610: /* exg a,d */
				res = DRX; DRX = (uint32)*AREG(IR_REGY); *AREG(IR_REGY) = (t_addr)res;
				rc = SCPE_OK; break;
			case 0000400: /* abcd d */				
				rc = STOP_IMPL; break;
			case 0000410: /* abcd a */				
				rc = STOP_IMPL; break;
			case 0000000: case 00000020: case 0000030: case 0000040:
			case 0000050: case 00000060: case 0000070: /* and.b -> d*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & DRX;
				SETNZ8(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000100: case 00000120: case 0000130: case 0000140:
			case 0000150: case 00000160: case 0000170: /* and.w -> d*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & DRX;
				SETNZ16(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_w(EA_DDIR,IR_REGX,res,&PC);
				break;
				rc = STOP_IMPL; break;
			case 0000200: case 00000220: case 0000230: case 0000240:
			case 0000250: case 00000260: case 0000270: /* and.l -> d*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1 & DRX;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000420: case 00000430: case 0000440: case 0000450:
			case 0000460: case 00000470: /* and.b -> ea*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = DRX & src1;
				SETNZ8(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000520: case 00000530: case 0000540: case 0000550:
			case 0000560: case 00000570: /* and.w -> ea*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = DRX & src1;
				SETNZ16(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000620: case 00000630: case 0000640: case 0000650:
			case 0000660: case 00000670: /* and.l -> ea*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = DRX & src1;
				SETNZ32(res);
				CLRF(FLAG_C|FLAG_V);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   1 | Register  |Opc| 1   1 | effective address<>00x| add,adda
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   0   1 |   Reg X   | 1 |Length | 0   0 |R/M|  Reg Y    | addx
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xd000:
			switch (IR_0803) {
			case 0000300: case 0000310: case 0000320: case 0000330:
			case 0000340: case 0000350: case 0000360: case 0000370: /*adda.w*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&srca,&PC));
				*AREG(IR_REGX) += EXTW(srca); /* note: no flag changes! */
				break;
			case 0000700: case 0000710: case 0000720: case 0000730:
			case 0000740: case 0000750: case 0000760: case 0000770: /*adda.l*/
				ASSERT_OK(ea_src_l(IR_EAMOD,IR_EAREG,&srca,&PC));
				*AREG(IR_REGX) += srca; /* note: no flag changes! */
				break;
			case 0000400: /* addx.b d*/
				res = m68k_add8(MASK_8L(DRY),DRX,CCR_X?1:0);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000410: /* addx.b -a*/
				ASSERT_OK(ea_src_b(EA_APD,IR_REGY,&src1,&PC));
				ASSERT_OK(ea_src_b(EA_APD,IR_REGX,&src2,&PC));
				res = m68k_add8(src1,src2,CCR_X?1:0);
				rc = ea_dst_b_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000500: /* addx.w d*/
				res = m68k_add16(MASK_16L(DRY),DRX,CCR_X?1:0,TRUE);
				rc = ea_dst_w(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000510: /* addx.w -a*/
				ASSERT_OK(ea_src_w(EA_APD,IR_REGY,&src1,&PC));
				ASSERT_OK(ea_src_w(EA_APD,IR_REGX,&src2,&PC));
				res = m68k_add16(src1,src2,CCR_X?1:0,TRUE);
				rc = ea_dst_w_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000600: /* addx.l d*/
				res = m68k_add32((t_uint64)DRY,(t_uint64)DRX,CCR_X?1:0,TRUE);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000610: /* addx.l -a*/
				ASSERT_OK(ea_src_l64(EA_APD,IR_REGY,&srcx1,&PC));
				ASSERT_OK(ea_src_l64(EA_APD,IR_REGX,&srcx2,&PC));
				res = m68k_add32(srcx1,srcx2,CCR_X?1:0,TRUE);
				rc = ea_dst_l_rmw(EA_APD,IR_REGX,res);
				break;
			case 0000000: case 0000010: case 0000020: case 0000030:
			case 0000040: case 0000050: case 0000060: case 0000070: /*add.b ->d*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add8(src1,DRX,0);
				rc = ea_dst_b(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000100: case 0000110: case 0000120: case 0000130:
			case 0000140: case 0000150: case 0000160: case 0000170: /*add.w ->d*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add16(src1,DRX,0,TRUE);
				rc = ea_dst_w(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000200: case 0000210: case 0000220: case 0000230:
			case 0000240: case 0000250: case 0000260: case 0000270: /*add.l ->d*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_add32(srcx1,(t_uint64)DRX,0,TRUE);
				rc = ea_dst_l(EA_DDIR,IR_REGX,res,&PC);
				break;
			case 0000420: case 0000430: case 0000440: case 0000450:
			case 0000460: case 0000470: /*add.b ->ea*/
				ASSERT_OK(ea_src_b(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add8(src1,DRX,0);
				rc = ea_dst_b_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000520: case 0000530: case 0000540: case 0000550:
			case 0000560: case 0000570: /*add.w ->ea*/
				ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = m68k_add16(src1,DRX,0,TRUE);
				rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			case 0000620: case 0000630: case 0000640: case 0000650:
			case 0000660: case 0000670: /*add.l ->ea*/
				ASSERT_OK(ea_src_l64(IR_EAMOD,IR_EAREG,&srcx1,&PC));
				res = m68k_add32(srcx1,(t_uint64)DRX,0,TRUE);
				rc = ea_dst_l_rmw(IR_EAMOD,IR_EAREG,res);
				break;
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   1   0 |Size/Reg X |dir|Length |i/r|Opcode2|  Reg Y    | asl,asr,lsl,lsr,rol,ror,roxl,roxr
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   1   0 | Opcode    |dir| 1   1 | effective address     | asl,asr,lsl,lsr,rol,ror,roxl,roxr
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xe000:
			switch (IR_1103) {
			case 000040: case 001040: case 002040: case 003040: 
			case 004040: case 005040: case 006040: case 007040: /*asr.b r*/
				cnt = DRX & 077;
				goto do_asr8;
			case 000000: case 001000: case 002000: case 003000:
			case 004000: case 005000: case 006000: case 007000: /*asr.b #*/
				cnt = quickarg[IR_REGX];
do_asr8:		reg = DR+IR_REGY;
				res = src1 = MASK_8L(*reg);
				if (cnt) {
					if (cnt<8) {
						res >>= cnt; 
						if (MASK_8SGN(src1)) res |= shmask8[cnt];
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = MASK_8SGN(src1) ? 0xff : 0x00;
						SETF(res,FLAG_C|FLAG_X);
					}
					*reg = COMBINE8(*reg,res);
				} else
					CLRF(FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc =SCPE_OK; break;

			case 000320: case 000330: case 000340: case 000350:
			case 000360: case 000370: /*asr*/
				cnt = 1;
				goto do_asr16;
			case 000140: case 001140: case 002140: case 003140: 
			case 004140: case 005140: case 006140: case 007140: /*asr.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_asr16;
			case 000100: case 001100: case 002100: case 003100: 
			case 004100: case 005100: case 006100: case 007100: /*asr.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_asr16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				if (cnt) {
					if (cnt<16) {
						res = src1 >> cnt; 
						if (MASK_16SGN(src1)) res |= shmask16[cnt];
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = MASK_16SGN(src1) ? 0xffff : 0x0000;
						SETF(res,FLAG_C|FLAG_X);
					}
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res); 	
				} else {
					CLRF(FLAG_C);
					res = src1;
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;

			case 000240: case 001240: case 002240: case 003240: 
			case 004240: case 005240: case 006240: case 007240: /*asr.l r*/
				cnt = DRX & 077;
				goto do_asr32;
			case 000200: case 001200: case 002200: case 003200: 
			case 004200: case 005200: case 006200: case 007200: /*asr.l #*/
				cnt = quickarg[IR_REGX];
do_asr32:		reg = DR+IR_REGY;
				res = src1 = *reg;
				if (cnt) {
					if (cnt < 32) {
						res >>= cnt; 
						if (MASK_32SGN(src1)) res |= shmask32[cnt];
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = MASK_32SGN(src1) ? 0xffffffff : 0x00000000;
						SETF(res,FLAG_C|FLAG_X);
					}
					*reg = res;
				} else CLRF(FLAG_C);
				SETNZ32(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;

			case 000440: case 001440: case 002440: case 003440: 
			case 004440: case 005440: case 006440: case 007440: /*asl.b r*/
				cnt = DRX & 077;
				goto do_asl8;
			case 000400: case 001400: case 002400: case 003400: 
			case 004400: case 005400: case 006400: case 007400: /*asl.b #*/
				cnt = quickarg[IR_REGX];
do_asl8:		reg = DR+IR_REGY;
				res = src1 = MASK_8L(*reg);
				if (cnt) {
					if (cnt<8) {
						res = src1 << cnt;
						SETF(MASK_9(res),FLAG_C|FLAG_X);
						src1 &= shmask8[cnt+1];
						SETF(src1 && src1 != shmask8[cnt+1],FLAG_V);						
					} else {
						res = 0;
						SETF(cnt==8?(src1 & 1):0,FLAG_C|FLAG_X);
						SETF(src1,FLAG_V);
					}
					*reg = COMBINE8(*reg,res);
				} else CLRF(FLAG_C|FLAG_V);
				SETNZ8(res);
				rc = SCPE_OK; break;
				
			case 000720: case 000730: case 000740: case 000750:
			case 000760: case 000770: /*asl*/
				cnt = 1;
				goto do_asl16;
			case 000540: case 001540: case 002540: case 003540:
			case 004540: case 005540: case 006540: case 007540: /*asl.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_asl16;
			case 000500: case 001500: case 002500: case 003500:
			case 004500: case 005500: case 006500: case 007500: /*asl.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_asl16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				if (cnt) {
					if (cnt<16) {
						res = src1 << cnt;
						SETF(MASK_17(res),FLAG_C|FLAG_X);
						src1 &= shmask16[cnt+1];
						SETF(src1 && src1 != shmask16[cnt+1],FLAG_V);
					} else {
						res = 0;
						SETF(cnt==16?(src1 & 1):0,FLAG_C|FLAG_X);
						SETF(src1,FLAG_V);
					}
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				} else {
					CLRF(FLAG_C|FLAG_V);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				break;

			case 000640: case 001640: case 002640: case 003640:
			case 004640: case 005640: case 006640: case 007640: /*asl.l r*/
				cnt = DRX & 077;
				goto do_asl32;
			case 000600: case 001600: case 002600: case 003600: 
			case 004600: case 005600: case 006600: case 007600: /*asl.l #*/
				cnt = quickarg[IR_REGX];
do_asl32:		reg = DR+IR_REGY;
				res = src1 = *reg;
				if (cnt) {
					if (cnt<32) {
						res <<= cnt;
						SETF(src1 & bitmask[32-cnt],FLAG_C|FLAG_X);
						src1 &= shmask32[cnt+1];
						SETF(src1 && src1 != shmask32[cnt+1],FLAG_V);
					} else {
						res = 0;
						SETF(cnt==16?(src1 & 1):0,FLAG_C|FLAG_X);
						SETF(src1,FLAG_V);
					}
					*reg = res;
				} else CLRF(FLAG_C|FLAG_V);
				SETNZ32(res);
				rc = SCPE_OK; break;
			
			case 000050: case 001050: case 002050: case 003050:
			case 004050: case 005050: case 006050: case 007050: /*lsr.b r*/
				cnt = DRX & 077;
				goto do_lsr8;
			case 000010: case 001010: case 002010: case 003010:
			case 004010: case 005010: case 006010: case 007010: /*lsr.b #*/
				cnt = quickarg[IR_REGX];
do_lsr8:		reg = DR+IR_REGY;
				res = src1 = MASK_8L(*reg);
				if (cnt) {
					if (cnt <= 8) {
						res = src1 >> cnt;
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					*reg = COMBINE8(*reg,res);
				} else CLRF(FLAG_C);
				CLRF(FLAG_V);
				SETNZ8(res);
				rc = SCPE_OK; break;
				
			case 001320: case 001330: case 001340: case 001350:
			case 001360: case 001370: /*lsr*/
				cnt = 1;
				goto do_lsr16;
			case 000150: case 001150: case 002150: case 003150:
			case 004150: case 005150: case 006150: case 007150: /*lsr.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_lsr16;
			case 000110: case 001110: case 002110: case 003110:
			case 004110: case 005110: case 006110: case 007110: /*lsr.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_lsr16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				if (cnt) {
					if (cnt <= 16) {
						res = src1 >> cnt;
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				CLRF(FLAG_V);
				SETNZ16(res);
				break;

			case 000250: case 001250: case 002250: case 003250:
			case 004250: case 005250: case 006250: case 007250: /*lsr.l r*/
				cnt = DRX & 077;
				goto do_lsr32;
			case 000210: case 001210: case 002210: case 003210:
			case 004210: case 005210: case 006210: case 007210: /*lsr.l #*/
				cnt = quickarg[IR_REGX];
do_lsr32:		reg = DR+IR_REGY;
				res = src1 = *reg;
				if (cnt) {
					if (cnt <= 32) {
						res = src1 >> cnt;
						SETF(src1&bitmask[cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					*reg = res;
				} else CLRF(FLAG_C);
				CLRF(FLAG_V);
				SETNZ32(res);
				rc = SCPE_OK;
				break;

			case 000450: case 001450: case 002450: case 003450:
			case 004450: case 005450: case 006450: case 007450: /*lsl.b r*/
				cnt = DRX & 077;
				goto do_lsl8;
			case 000410: case 001410: case 002410: case 003410: 
			case 004410: case 005410: case 006410: case 007410: /*lsl.b #*/
				cnt = quickarg[IR_REGX];
do_lsl8:		reg = DR+IR_REGY;
				res = src1 = MASK_8L(*reg);
				if (cnt) {
					if (cnt <= 8) {
						res = src1 << cnt;
						SETF(src1&bitmask[9-cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					*reg = COMBINE8(*reg,res);
				} else CLRF(FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
			
			case 001720: case 001730: case 001740: case 001750:
			case 001760: case 001770: /*lsl*/
				cnt = 1;
				goto do_lsl16;
			case 000550: case 001550: case 002550: case 003550:
			case 004550: case 005550: case 006550: case 007550: /*lsl.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_lsl16;
			case 000510: case 001510: case 002510: case 003510:
			case 004510: case 005510: case 006510: case 007510: /*lsl.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_lsl16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&src1,&PC));
				res = src1;
				if (cnt) {
					if (cnt <= 16) {
						res = src1 << cnt;
						SETF(src1&bitmask[17-cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;

			case 000650: case 001650: case 002650: case 003650:
			case 004650: case 005650: case 006650: case 007650: /*lsl.l r*/
				cnt = DRX & 077;
				goto do_lsl32;
			case 000610: case 001610: case 002610: case 003610:
			case 004610: case 005610: case 006610: case 007610: /*lsl.l #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_lsl32:		reg = DR+IR_REGY;
				res = src1 = *reg;
				if (cnt) {
					if (cnt <= 32) {
						res = src1 << cnt;
						SETF(src1&bitmask[33-cnt],FLAG_C|FLAG_X);
					} else {
						res = 0;
						CLRF(FLAG_X|FLAG_C);
					}
					*reg = res;
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ32(res);
				CLRF(FLAG_V);
				break;
			
			case 000060: case 001060: case 002060: case 003060:
			case 004060: case 005060: case 006060: case 007060: /*roxr.b r*/
				cnt = DRX & 077;
				goto do_roxr8;
			case 000020: case 001020: case 002020: case 003020:
			case 004020: case 005020: case 006020: case 007020: /*roxr.b #*/
				cnt = quickarg[IR_REGX];
do_roxr8:		reg = DR+IR_REGY;
				res = MASK_8L(*reg);
				if (cnt) {
					cnt %= 9;
					if (CCR_X) res |= BIT8;
					res = (res>>cnt) | (res<<(9-cnt));
					*reg = COMBINE8(*reg,res);
					SETF(MASK_9(res),FLAG_X|FLAG_C);
				} else SETF(CCR_X,FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
				
			case 002320: case 002330: case 002340: case 002350:
			case 002360: case 002370: /*roxr*/
				cnt = 1;
				goto do_roxr16;
			case 000160: case 001160: case 002160: case 003160:
			case 004160: case 005160: case 006160: case 007160: /*roxr.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_roxr16;
			case 000120: case 001120: case 002120: case 003120:
			case 004120: case 005120: case 006120: case 007120: /*roxr.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_roxr16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&res,&PC));
				if (cnt) {
					cnt %= 17;
					if (CCR_X) res |= BIT16;
					res = (res>>cnt) | (res<<(17-cnt));
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
					SETF(MASK_17(res),FLAG_X|FLAG_C);
				} else {
					SETF(CCR_X,FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;

			case 000260: case 001260: case 002260: case 003260:
			case 004260: case 005260: case 006260: case 007260: /*roxr.l r*/
				cnt = DRX & 077;
				goto do_roxr32;
			case 000220: case 001220: case 002220: case 003220:
			case 004220: case 005220: case 006220: case 007220: /*roxr.l #*/
				cnt = quickarg[IR_REGX];
do_roxr32:		reg = DR+IR_REGY;
				resx = *reg;
				if (cnt) {
					cnt %= 33;
					if (CCR_X) resx |= BIT32;
					resx = (resx>>cnt) | (resx<<(33-cnt));
					*reg = MASK_32L(resx);
					SETF(MASK_33(res),FLAG_X|FLAG_C);
				} else SETF(CCR_X,FLAG_C);
				SETNZ32(resx);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
			
			case 000460: case 001460: case 002460: case 003460:
			case 004460: case 005460: case 006460: case 007460: /*roxl.b r*/
				cnt = DRX & 077;
				goto do_roxl8;
			case 000420: case 001420: case 002420: case 003420:
			case 004420: case 005420: case 006420: case 007420: /*roxl.b #*/
				cnt = quickarg[IR_REGX];
do_roxl8:		reg = DR+IR_REGY;
				res = MASK_8L(*reg);
				if (cnt) {
					cnt %= 9;
					if (CCR_X) res |= BIT8;
					res = (res<<cnt) | (res>>(9-cnt));
					*reg = COMBINE8(*reg,res);
					SETF(MASK_9(res),FLAG_X|FLAG_C);
				} else SETF(CCR_X,FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;

			case 002720: case 002730: case 002740: case 002750:
			case 002760: case 002770: /*roxl*/
				cnt = 1;
				goto do_roxl16;
			case 000560: case 001560: case 002560: case 003560:
			case 004560: case 005560: case 006560: case 007560: /*roxl.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_roxl16;
			case 000520: case 001520: case 002520: case 003520:
			case 004520: case 005520: case 006520: case 007520: /*roxl.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_roxl16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&res,&PC));
				if (cnt) {
					cnt %= 17;
					if (CCR_X) res |= BIT16;
					res = (res<<cnt) | (res>>(17-cnt));
					SETF(MASK_17(res),FLAG_X|FLAG_C);
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res); 
				} else {
					SETF(CCR_X,FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;
			
			case 000660: case 001660: case 002660: case 003660:
			case 004660: case 005660: case 006660: case 007660: /*roxl.l r*/
				cnt = DRX & 077;
				goto do_roxl32;
			case 000620: case 001620: case 002620: case 003620:
			case 004620: case 005620: case 006620: case 007620: /*roxl.l #*/
				cnt = quickarg[IR_REGX];
do_roxl32:		reg = DR+IR_REGY;
				resx = *reg;
				if (cnt) {
					cnt %= 33;
					if (CCR_X) resx |= BIT32;
					resx = (resx<<cnt) | (resx>>(33-cnt));
					SETF(MASK_33(resx),FLAG_X|FLAG_C);
					*reg = MASK_32L(resx);
				} else SETF(CCR_X,FLAG_C);
				SETNZ32(resx);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
			
			case 000070: case 001070: case 002070: case 003070:
			case 004070: case 005070: case 006070: case 007070: /*ror.b r*/
				cnt = DRX & 077;
				goto do_ror8;
			case 000030: case 001030: case 002030: case 003030:
			case 004030: case 005030: case 006030: case 007030: /*ror.b #*/
				cnt = quickarg[IR_REGX];
do_ror8:		reg = DR+IR_REGY;
				res = MASK_8L(*reg);
				if (cnt) {
					cnt &= 7;
					res = (res>>cnt) | (res<<(8-cnt));
					SETF(MASK_9(res),FLAG_C);
					*reg = COMBINE8(*reg,res);
				} else CLRF(FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;

			case 003320: case 003330: case 003340: case 003350:
			case 003360: case 003370: /*ror*/
				cnt = 1;
				goto do_ror16;
			case 000170: case 001170: case 002170: case 003170:
			case 004170: case 005170: case 006170: case 007170: /*ror.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_ror16;
			case 000130: case 001130: case 002130: case 003130:
			case 004130: case 005130: case 006130: case 007130: /*ror.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_ror16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&res,&PC));
				if (cnt) {
					cnt &= 15;
					res = (res>>cnt) | (res<<(16-cnt));
					SETF(MASK_17(res),FLAG_C);
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;

			case 000270: case 001270: case 002270: case 003270:
			case 004270: case 005270: case 006270: case 007270: /*ror.l r*/
				cnt = DRX & 077;
				goto do_ror32;
			case 000230: case 001230: case 002230: case 003230:
			case 004230: case 005230: case 006230: case 007230: /*ror.l #*/
				cnt = quickarg[IR_REGX];
do_ror32:		reg = DR+IR_REGY;
				resx = *reg;
				if (cnt) {
					cnt &= 31;
					resx = (resx>>cnt) | (resx<<(32-cnt));
					SETF(MASK_33(res),FLAG_C);
					*reg = (int32)resx;
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ32(resx);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;

			case 000470: case 001470: case 002470: case 003470:
			case 004470: case 005470: case 006470: case 007470: /*rol.b r*/
				cnt = DRX & 077;
				goto do_rol8;
			case 000430: case 001430: case 002430: case 003430:
			case 004430: case 005430: case 006430: case 007430: /*rol.b #*/
				cnt = quickarg[IR_REGX];
do_rol8:		reg = DR+IR_REGY;
				res = MASK_8L(*reg);
				if (cnt) {
					cnt &= 7;
					res = (res<<cnt) | (res>>(8-cnt));
					SETF(MASK_9(res),FLAG_C);
					*reg = COMBINE8(*reg,res);
				} else CLRF(FLAG_C);
				SETNZ8(res);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
				
			case 003720: case 003730: case 003740: case 003750:
			case 003760: case 003770: /*rol*/
				cnt = 1;
				goto do_rol16;
			case 000570: case 001570: case 002570: case 003570:
			case 004570: case 005570: case 006570: case 007570: /*rol.w r*/
				cnt = DRX & 077;
				IR = EA_DDIR | IR_REGY;
				goto do_rol16;
			case 000530: case 001530: case 002530: case 003530:
			case 004530: case 005530: case 006530: case 007530: /*rol.w #*/
				cnt = quickarg[IR_REGX];
				IR = EA_DDIR | IR_REGY;
do_rol16:		ASSERT_OK(ea_src_w(IR_EAMOD,IR_EAREG,&res,&PC));
				if (cnt) {
					cnt &= 15;
					res = (res<<cnt) | (res>>(16-cnt));
					SETF(MASK_17(res),FLAG_C);
					rc = ea_dst_w_rmw(IR_EAMOD,IR_EAREG,res);
				} else {
					CLRF(FLAG_C);
					rc = SCPE_OK;
				}
				SETNZ16(res);
				CLRF(FLAG_V);
				break;

			case 000670: case 001670: case 002670: case 003670:
			case 004670: case 005670: case 006670: case 007670: /*rol.l r*/
				cnt = DRX & 077;
				goto do_rol32;
			case 000630: case 001630: case 002630: case 003630:
			case 004630: case 005630: case 006630: case 007630: /*rol.l #*/
				cnt = quickarg[IR_REGX];
do_rol32:		reg = DR+IR_REGY;
				resx = (uint32)*reg;
				if (cnt) {
					cnt &= 31;
					resx = (resx<<cnt) | (resx>>(32-cnt));
					SETF(MASK_32L(resx),FLAG_C);
					*reg = MASK_32L(resx);
				} else CLRF(FLAG_C);
				SETNZ32(resx);
				CLRF(FLAG_V);
				rc = SCPE_OK; break;
			
			default:
				rc = STOP_ERROP;
			}
			break;

		/* +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
		 * | 1   1   1   1 | Opcode                                        | trapf
		 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---*/
		case 0xf000:
			rc = m68k_gen_exception(11,&PC); break;

		/* unreachable */
		default:
			rc = STOP_ERROP; break;
		}
		
		/* handle tracing */
		if (tracet0 || SR_T1) {
			if (m68kcpu_unit->flags & UNIT_CPU_TRACE) {
				/* leave loop */
				sim_interval = -1;
				rc = STOP_TRACE;
				break;
			}
			IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: Tracebit set\n",PC));
			ASSERT_OK(m68k_gen_exception(9,&PC));
			/* remain in loop */
		}
		tracet0 = 0;

		/* handle interrupts (sets/resets intpending) */
		m68k_checkints(&PC);
		
		/* handle STOP instr */
		if (rc==STOP_HALT) {
			if (m68kcpu_unit->flags & UNIT_CPU_STOP) {
				PC -= 4; /* correct PC to point to STOP instr */
				break;
			}
			if ((rc = m68k_stop(&PC)) != SCPE_OK) 
				break; /* does not return until interrupt occurs, will react to CTRL-E */
		}
	}

	/* handle various exit codes */
	switch (rc) {
	case STOP_ERRADR: /* address error */
		if ((m68kcpu_unit->flags & UNIT_CPU_EXC)==0) {
			IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: Address error\n",PC));
			if ((rc = m68k_gen_exception(3,&PC)) != SCPE_OK) {
				/* double bus fault */
				rc = STOP_DBF; /* cannot be masked, will stop simulator */
			}
		}
		return rc;
	case STOP_PCIO: /* cannot be masked, will stop simulator */
		return rc;
	case STOP_ERRIO: /* bus error */
		if ((m68kcpu_unit->flags & UNIT_CPU_EXC)==0) {
			IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: Bus error\n",PC));
			if ((rc = m68k_gen_exception(2,&PC)) != SCPE_OK) {
				/* double bus fault */
				rc = STOP_DBF; /* cannot be masked, will stop simulator */
			}
		}
		return rc;
	case STOP_ERROP: /* illegal opcode */
		if (!(m68kcpu_unit->flags & UNIT_CPU_EXC)) {
			IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: Illegal opcode\n",PC));
			rc = m68k_gen_exception(4,&PC);
		}
		return rc;
	case STOP_PRVIO: /* privilege violation */
		if (!(m68kcpu_unit->flags & UNIT_CPU_PRVIO)) {
			IFDEBUG(DBG_CPU_EXC,fprintf(sim_deb,"CPU : [0x%08x] Exception: Privilege violation\n",PC));
			rc = m68k_gen_exception(8,&PC);
		}
		break;
	case STOP_IMPL:
		return rc; /* leave sim_instr */
	default:
		return rc; /* leave sim_instr */
	}

	/* save state */
	saved_PC = PC;

	return rc;
}
