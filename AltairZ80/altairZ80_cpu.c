/*	altairz80_cpu.c: MITS Altair CPU (8080 and Z80)
		Written by Peter Schorn, 2001-2002
		Based on work by Charles E Owen ((c) 1997 - Commercial use prohibited)
		Code for Z80 CPU from Frank D. Cringle ((c) 1995 under GNU license)
*/

#include <stdio.h>
#include "altairZ80_defs.h"

#define PCQ_SIZE	64													/* must be 2**n											*/
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY(PC)	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC

#define MEMSIZE					(cpu_unit.capac)			/* actual memory size								*/
#define KB							1024									/* kilo byte												*/
#define bootrom_origin	0xff00								/* start address of boot rom				*/

/* Simulator stop codes */
#define STOP_HALT				2											/* HALT															*/
#define STOP_IBKPT			3											/* breakpoint	(program counter)			*/
#define STOP_MEM				4											/* breakpoint	(memory access)				*/
#define STOP_OPCODE			5											/* unknown 8080 or Z80 instruction	*/

/*-------------------------------- definitions for memory space ------------------*/

uint8 M[MAXMEMSIZE][MAXBANKS];	/* RAM which is present */

/* two sets of accumulator / flags */
uint16 af[2];
int af_sel;

/* two sets of 16-bit registers */
struct ddregs {
	uint16 bc;
	uint16 de;
	uint16 hl;
} regs[2];
int regs_sel;

uint16 ir;
uint16 ix;
uint16 iy;
uint16 sp;
uint16 pc;
uint16 IFF;

#define FLAG_C	1
#define FLAG_N	2
#define FLAG_P	4
#define FLAG_H	16
#define FLAG_Z	64
#define FLAG_S	128

#define SETFLAG(f,c)	AF = (c) ? AF | FLAG_ ## f : AF & ~FLAG_ ## f
#define TSTFLAG(f)	((AF & FLAG_ ## f) != 0)

#define ldig(x)		((x) & 0xf)
#define hdig(x)		(((x)>>4)&0xf)
#define lreg(x)		((x)&0xff)
#define hreg(x)		(((x)>>8)&0xff)

#define Setlreg(x, v)	x = (((x)&0xff00) | ((v)&0xff))
#define Sethreg(x, v)	x = (((x)&0xff) | (((v)&0xff) << 8))

/*	SetPV and SetPV2 are used to provide correct parity flag semantics for the 8080 in cases
		where the Z80 uses the overflow flag
*/
#define SetPV ((cpu_unit.flags & UNIT_CHIP) ? (((cbits >> 6) ^ (cbits >> 5)) & 4) : (parity(sum)))
#define SetPV2(x) ((cpu_unit.flags & UNIT_CHIP) ? (((temp == (x)) << 2)) : (parity(temp)))

/* checkCPU8080 must be invoked whenever a Z80 only instruction is executed */
#define checkCPU8080																													\
	if ((cpu_unit.flags & UNIT_CHIP == 0) && (cpu_unit.flags & UNIT_OPSTOP)) {	\
		reason = STOP_OPCODE;																											\
		goto end_decode;																													\
	}

/* checkCPUZ80 must be invoked whenever a non Z80 instruction is executed */
#define checkCPUZ80																														\
	if (cpu_unit.flags & UNIT_OPSTOP) {																					\
		reason = STOP_OPCODE;																											\
		goto end_decode;																													\
	}

static const uint8 partab[256] = {
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
	4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
};

#define parity(x)	partab[(x)&0xff]

#define POP(x)	do {													\
	register uint32 y = RAM_pp(SP);							\
	x = y + (RAM_pp(SP) << 8);									\
} while (0)

#define JPC(cond) {														\
	if (cond) {																	\
		PCQ_ENTRY(PC - 1);												\
		PC = GetWORD(PC);													\
	}																						\
	else {																			\
		PC += 2;																	\
	}																						\
}

#define CALLC(cond) {													\
	if (cond) {																	\
		register uint32 adrr = GetWORD(PC);				\
		CheckBreakWord(SP - 2);										\
		PUSH(PC + 2);															\
		PCQ_ENTRY(PC - 1);												\
		PC = adrr;																\
	}																						\
	else {																			\
		sim_brk_pend = FALSE;											\
		PC += 2;																	\
	}																						\
}

int32 saved_PC = 0;			/* program counter														*/
int32 SR = 0;						/* switch register														*/
int32 PCX;							/* External view of PC												*/
int32 bankSelect = 0;		/* determines selected memory bank						*/
uint32 common = 0xc000;	/* addresses >= 'common' are in common memory	*/

extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */
extern int32 sio0s				(int32 port, int32 io, int32 data);
extern int32 sio0d				(int32 port, int32 io, int32 data);
extern int32 sio1s				(int32 port, int32 io, int32 data);
extern int32 sio1d				(int32 port, int32 io, int32 data);
extern int32 dsk10				(int32 port, int32 io, int32 data);
extern int32 dsk11				(int32 port, int32 io, int32 data);
extern int32 dsk12				(int32 port, int32 io, int32 data);
extern int32 nulldev			(int32 port, int32 io, int32 data);
extern int32 simh_dev			(int32 port, int32 io, int32 data);
extern int32 sr_dev				(int32 port, int32 io, int32 data);
extern int32 bootrom[bootrom_size];
extern char memoryAccessMessage[];

/* function prototypes */
t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset(DEVICE *dptr);
t_stat cpu_set_size(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_banked(UNIT *uptr, int32 value, char *cptr, void *desc);
t_stat cpu_set_rom(UNIT *uptr, int32 value, char *cptr, void *desc);
uint32 in(uint32 Port);
void out(uint32 Port, uint32 Value);
uint8 GetBYTE(register uint32 Addr);
void PutBYTE(register uint32 Addr, register uint32 Value);
void PutBYTEForced(register uint32 Addr, register uint32 Value);
uint16 GetWORD(register uint32 a);
void PutWORD(register uint32 a, register uint32 v);
int32 sim_instr (void);
void install_bootrom(void);
void clear_memory(int32 starting);
t_bool sim_brk_lookup (t_addr bloc, int32 btyp);
void prepareMemoryAccessMessage(t_addr loc);

/*	in case of using inline we need to ensure that the GetBYTE and PutBYTE
		are accessible externally */
#ifndef NO_INLINE
uint8 GetBYTEWrapper(register uint32 Addr);
void PutBYTEWrapper(register uint32 Addr, register uint32 Value);
#endif

/*	CPU data structures
		cpu_dev	CPU device descriptor
		cpu_unit	CPU unit descriptor
		cpu_reg	CPU register list
		cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK + UNIT_ROM, MAXMEMSIZE) };

int32 AF_S;
int32 BC_S;
int32 DE_S;
int32 HL_S;
int32 IX_S;
int32 IY_S;
int32 SP_S;
int32 AF1_S;
int32 BC1_S;
int32 DE1_S;
int32 HL1_S;
int32 IFF_S;
int32 INT_S;
uint16 pcq[PCQ_SIZE] = { 0 };	/* PC queue */
int32 pcq_p = 0;							/* PC queue ptr */
REG *pcq_r = NULL;						/* PC queue reg ptr */

REG cpu_reg[] = {
	{ HRDATA (PC, saved_PC, 16) },
	{ HRDATA (AF, AF_S, 16) },
	{ HRDATA (BC, BC_S, 16) },
	{ HRDATA (DE, DE_S, 16) },
	{ HRDATA (HL, HL_S, 16) },
	{ HRDATA (IX, IX_S, 16) },
	{ HRDATA (IY, IY_S, 16) },
	{ HRDATA (SP, SP_S, 16) },
	{ HRDATA (AF1, AF1_S, 16) },
	{ HRDATA (BC1, BC1_S, 16) },
	{ HRDATA (DE1, DE1_S, 16) },
	{ HRDATA (HL1, HL1_S, 16) },
	{ GRDATA (IFF, IFF_S, 2, 2, 0) },
	{ FLDATA (INT, INT_S, 8) },
	{ FLDATA (Z80, cpu_unit.flags, UNIT_V_CHIP), REG_HRO },
	{ FLDATA (OPSTOP, cpu_unit.flags, UNIT_V_OPSTOP), REG_HRO },
	{ HRDATA (SR, SR, 8) },
	{ HRDATA (BANK, bankSelect, MAXBANKSLOG2) },
	{ HRDATA (COMMON, common, 16) },
	{ BRDATA (PCQ, pcq, 16, 16, PCQ_SIZE), REG_RO + REG_CIRC },
	{ DRDATA (PCQP, pcq_p, 6), REG_HRO },
	{ HRDATA (WRU, sim_int_char, 8) },
	{ NULL }	};

MTAB cpu_mod[] = {
	{ UNIT_CHIP,		UNIT_CHIP,		"Z80",				"Z80",				NULL						},
	{ UNIT_CHIP,		0,						"8080",				"8080",				NULL						},
	{ UNIT_OPSTOP,	UNIT_OPSTOP,	"ITRAP",			"ITRAP",			NULL						},
	{ UNIT_OPSTOP,	0,						"NOITRAP",		"NOITRAP",		NULL						},
	{ UNIT_BANKED,	UNIT_BANKED,	"BANKED",			"BANKED",			&cpu_set_banked	},
	{ UNIT_BANKED,	0,						"NONBANKED",	"NONBANKED",	NULL						},
	{ UNIT_ROM,			UNIT_ROM,			"ROM",				"ROM",				&cpu_set_rom		},
	{ UNIT_ROM,			0,						"NOROM",			"NOROM",			NULL						},
	{ UNIT_MSIZE,		4 * KB,				NULL,					"4K",					&cpu_set_size		},
	{ UNIT_MSIZE,		8 * KB,				NULL,					"8K",					&cpu_set_size		},
	{ UNIT_MSIZE,		12 * KB,			NULL,					"12K",				&cpu_set_size		},
	{ UNIT_MSIZE,		16 * KB,			NULL,					"16K",				&cpu_set_size		},
	{ UNIT_MSIZE,		20 * KB,			NULL,					"20K",				&cpu_set_size		},
	{ UNIT_MSIZE,		24 * KB,			NULL,					"24K",				&cpu_set_size		},
	{ UNIT_MSIZE,		28 * KB,			NULL,					"28K",				&cpu_set_size		},
	{ UNIT_MSIZE,		32 * KB,			NULL,					"32K",				&cpu_set_size		},
	{ UNIT_MSIZE,		48 * KB,			NULL,					"48K",				&cpu_set_size		},
	{ UNIT_MSIZE,		64 * KB,			NULL,					"64K",				&cpu_set_size		},
	{ 0 }	};

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 1, 16, 8,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

/* data structure for IN/OUT instructions */
struct idev {
	int32 (*routine)(int32, int32, int32);
};

/*	This is the I/O configuration table. There are 255 possible
		device addresses, if a device is plugged to a port it's routine
		address is here, 'nulldev' means no device is available
*/
struct idev dev_table[256] = {
{&nulldev},	{&nulldev},	{&nulldev}, {&nulldev},					/* 00 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 04 */
{&dsk10},		{&dsk11},		{&dsk12},		{&nulldev},					/* 08 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 0C */
{&sio0s},		{&sio0d},		{&sio1s},		{&sio1d},						/* 10 */
{&sio0s},		{&sio0d},		{&sio0s},		{&sio0d},						/* 14 */
{&sio0s},		{&sio0d},		{&nulldev}, {&nulldev},					/* 18 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 1C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 20 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 24 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 28 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 2C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 30 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 34 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 38 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 3C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 40 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 44 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 48 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 4C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 50 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 54 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 58 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 5C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 60 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 64 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 68 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 6C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 70 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 74 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 78 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 7C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 80 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 84 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 88 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 8C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 90 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 94 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 98 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* 9C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* A0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* A4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* A8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* AC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* B0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* B4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* B8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* BC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* C0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* C4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* C8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* CC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* D0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* D4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* D8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* DC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* D0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* E4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* E8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* EC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* F0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* F4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},					/* F8 */
{&nulldev}, {&nulldev}, {&simh_dev}, {&sr_dev} };				/* FC */

INLINE void out(uint32 Port, uint32 Value) {
	dev_table[Port].routine(Port, 1, Value);
}

INLINE uint32 in(uint32 Port) {
	return dev_table[Port].routine(Port, 0, 0);
}

INLINE uint8 GetBYTE(register uint32 Addr) {
	Addr &= ADDRMASK;	/* registers are NOT guaranteed to be always 16-bit values */
	if (cpu_unit.flags & UNIT_BANKED) {
		return Addr < common ? M[Addr][bankSelect] : M[Addr][0];
	}
	else {
		return ((Addr < MEMSIZE) || (bootrom_origin <= Addr)) ? M[Addr][0] : 0xff;
	}
}

INLINE void PutBYTE(register uint32 Addr, register uint32 Value) {
	Addr &= ADDRMASK;	/* registers are NOT guaranteed to be always 16-bit values */
	if (cpu_unit.flags & UNIT_BANKED) {
		if (Addr < common) {
			M[Addr][bankSelect] = Value;
		}
		else if ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0)) {
			M[Addr][0] = Value;
		}
	}
	else {
		if ((Addr < MEMSIZE) && ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0))) {
			M[Addr][0] = Value;
		}
	}
}

void PutBYTEForced(register uint32 Addr, register uint32 Value) {
	Addr &= ADDRMASK;	/* registers are NOT guaranteed to be always 16-bit values */
	if (cpu_unit.flags & UNIT_BANKED) {
		if (Addr < common) {
			M[Addr][bankSelect] = Value;
		}
		else {
			M[Addr][0] = Value;
		}
	}
	else {
		M[Addr][0] = Value;
	}
}

INLINE void PutWORD(register uint32 Addr, register uint32 Value) {
	Addr &= ADDRMASK;	/* registers are NOT guaranteed to be always 16-bit values */
	if (cpu_unit.flags & UNIT_BANKED) {
		if (Addr < common) {
			M[Addr][bankSelect] = Value;
		}
		else if ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0)) {
			M[Addr][0] = Value;
		}
		Addr = (Addr + 1) & ADDRMASK;
		if (Addr < common) {
			M[Addr][bankSelect] = Value >> 8;
		}
		else if ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0)) {
			M[Addr][0] = Value >> 8;
		}
	}
	else {
		if ((Addr < MEMSIZE) && ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0))) {
			M[Addr][0] = Value;
		}
		Addr = (Addr + 1) & ADDRMASK;
		if ((Addr < MEMSIZE) && ((Addr < bootrom_origin) || ((cpu_unit.flags & UNIT_ROM) == 0))) {
			M[Addr][0] = Value >> 8;
		}
	}
}

#ifndef NO_INLINE
uint8 GetBYTEWrapper(register uint32 Addr) { /* make sure that non-inlined version exists */
	return GetBYTE(Addr);
}

void PutBYTEWrapper(register uint32 Addr, register uint32 Value) {
	PutBYTE(Addr, Value);
}
#endif

#define RAM_mm(a)	GetBYTE(a--)
#define RAM_pp(a)	GetBYTE(a++)

#define PutBYTE_pp(a,v)	PutBYTE(a++, v)
#define PutBYTE_mm(a,v)	PutBYTE(a--, v)
#define mm_PutBYTE(a,v)	PutBYTE(--a, v)

INLINE uint16 GetWORD(register uint32 a)	{
	return GetBYTE(a) | (GetBYTE(a + 1) << 8);
}

#define MASK_BRK (TRUE+1)

/* repeated from scp.c */
struct brktab {
	t_addr	addr;
	int32		typ;
	int32		cnt;
	char		*act;
};
typedef struct brktab BRKTAB;

/* this is a modified version of sim_brk_test with two differences:
	1) is does not set sim_brk_pend to FALSE (this if left to the instruction decode)
	2) it returns MASK_BRK if a breakpoint is found but should be ignored
*/
int32 sim_brk_lookup (t_addr loc, int32 btyp) {
	extern BRKTAB *sim_brk_fnd (t_addr loc);
	extern t_bool sim_brk_pend;
	extern t_addr sim_brk_ploc;
	BRKTAB *bp;
	if ((bp = sim_brk_fnd (loc)) &&
			(btyp & bp -> typ) &&
			(!sim_brk_pend || (loc != sim_brk_ploc)) &&
			(--(bp -> cnt) <= 0)) {
		bp -> cnt = 0;
		sim_brk_ploc = loc;
		sim_brk_pend = TRUE;
		return TRUE;
	}
	return (sim_brk_pend && (loc == sim_brk_ploc)) ? MASK_BRK : FALSE;
}

void prepareMemoryAccessMessage(t_addr loc) {
	sprintf(memoryAccessMessage, "Memory access breakpoint [%04xh]", loc);
}

#define PUSH(x) do {																			\
	mm_PutBYTE(SP, (x) >> 8);																\
	mm_PutBYTE(SP, x);																			\
} while (0)

#define CheckBreakByte(a)																	\
	if (sim_brk_summ && sim_brk_test(a, SWMASK('M'))) {			\
		reason = STOP_MEM;																		\
		prepareMemoryAccessMessage(a);												\
		goto end_decode;																			\
	}

#define CheckBreakTwoBytesExtended(a1, a2, iCode)					\
	if (sim_brk_summ) {																			\
		br1 = sim_brk_lookup(a1, SWMASK('M'));								\
		br2 = br1 ? FALSE : sim_brk_lookup(a2, SWMASK('M'));	\
		if ((br1 == MASK_BRK) || (br2 == MASK_BRK)) {					\
			sim_brk_pend = FALSE;																\
		}																											\
		else if (br1 || br2) {																\
			reason = STOP_MEM;																	\
			if (br1) {																					\
				prepareMemoryAccessMessage(a1);										\
			}																										\
			else {																							\
				prepareMemoryAccessMessage(a2);										\
			}																										\
			iCode;																							\
			goto end_decode;																		\
		}																											\
		else {																								\
			sim_brk_pend = FALSE;																\
		}																											\
	}

#define CheckBreakTwoBytes(a1, a2) CheckBreakTwoBytesExtended(a1, a2,;)

#define CheckBreakWord(a) CheckBreakTwoBytes(a, (a+1))

int32 sim_instr (void) {
	extern int32 sim_interval;
	extern t_bool sim_brk_pend;
	extern int32 timerInterrupt;
	extern int32 timerInterruptHandler;
	int32 reason = 0;
	register uint32 AF;
	register uint32 BC;
	register uint32 DE;
	register uint32 HL;
	register uint32 PC;
	register uint32 SP;
	register uint32 IX;
	register uint32 IY;
	register uint32 temp, acu, sum, cbits;
	register uint32 op, adr;
	int32 br1, br2;

	pc = saved_PC & ADDRMASK;					/* load local PC */
	af[af_sel] = AF_S;
	regs[regs_sel].bc = BC_S;
	regs[regs_sel].de = DE_S;
	regs[regs_sel].hl = HL_S;
	ix = IX_S;
	iy = IY_S;
	sp = SP_S;
	af[1 - af_sel] = AF1_S;
	regs[1 - regs_sel].bc = BC1_S;
	regs[1 - regs_sel].de = DE1_S;
	regs[1 - regs_sel].hl = HL1_S;
	IFF = IFF_S;
	ir = INT_S;

	AF = af[af_sel];
	BC = regs[regs_sel].bc;
	DE = regs[regs_sel].de;
	HL = regs[regs_sel].hl;
	PC = pc;
	SP = sp;
	IX = ix;
	IY = iy;

	/* Main instruction fetch/decode loop */
	while (TRUE) {														/* loop until halted	*/
		if (sim_interval <= 0) {								/* check clock queue	*/
			if (reason = sim_process_event()) {
				break;
			}
		}

		if (timerInterrupt && (IFF & 1)) {
			timerInterrupt = FALSE;
			IFF = 0; /* disable interrupts */
			CheckBreakTwoBytesExtended(SP - 2, SP - 1, (timerInterrupt = TRUE, IFF |= 1));
			PUSH(PC);
			PCQ_ENTRY(PC - 1);
			PC = timerInterruptHandler & ADDRMASK;
		}

		if (sim_brk_summ && (sim_brk_lookup(PC, SWMASK('E')) == TRUE)) {	/* breakpoint?				*/
			reason = STOP_IBKPT;																						/* stop simulation		*/
			break;
		}

		PCX = PC;
		sim_interval--;

		 /* make sure that each instructions properly sets sim_brk_pend:
		 		1) Either directly to FALSE if no memory access takes place or
		 		2) through a call to a Check... routine
		 */
		switch(RAM_pp(PC)) { 
			case 0x00:			/* NOP */
				sim_brk_pend = FALSE;
				break;
			case 0x01:			/* LD BC,nnnn */
				sim_brk_pend = FALSE;
				BC = GetWORD(PC);
				PC += 2;
				break;
			case 0x02:			/* LD (BC),A */
				CheckBreakByte(BC)
				PutBYTE(BC, hreg(AF));
				break;
			case 0x03:			/* INC BC */
				sim_brk_pend = FALSE;
				++BC;
				break;
			case 0x04:			/* INC B */
				sim_brk_pend = FALSE;
				BC += 0x100;
				temp = hreg(BC);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x05:			/* DEC B */
				sim_brk_pend = FALSE;
				BC -= 0x100;
				temp = hreg(BC);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x06:			/* LD B,nn */
				sim_brk_pend = FALSE;
				Sethreg(BC, RAM_pp(PC));
				break;
			case 0x07:			/* RLCA */
				sim_brk_pend = FALSE;
				AF = ((AF >> 7) & 0x0128) | ((AF << 1) & ~0x1ff) |
					(AF & 0xc4) | ((AF >> 15) & 1);
				break;
			case 0x08:			/* EX AF,AF' */
				sim_brk_pend = FALSE;
				checkCPU8080;
				af[af_sel] = AF;
				af_sel = 1 - af_sel;
				AF = af[af_sel];
				break;
			case 0x09:			/* ADD HL,BC */
				sim_brk_pend = FALSE;
				HL &= ADDRMASK;
				BC &= ADDRMASK;
				sum = HL + BC;
				cbits = (HL ^ BC ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x0a:			/* LD A,(BC) */
				CheckBreakByte(BC)
				Sethreg(AF, GetBYTE(BC));
				break;
			case 0x0b:			/* DEC BC */
				sim_brk_pend = FALSE;
				--BC;
				break;
			case 0x0c:			/* INC C */
				sim_brk_pend = FALSE;
				temp = lreg(BC) + 1;
				Setlreg(BC, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x0d:			/* DEC C */
				sim_brk_pend = FALSE;
				temp = lreg(BC) - 1;
				Setlreg(BC, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x0e:			/* LD C,nn */
				sim_brk_pend = FALSE;
				Setlreg(BC, RAM_pp(PC));
				break;
			case 0x0f:			/* RRCA */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				sum = temp >> 1;
				AF = ((temp & 1) << 15) | (sum << 8) |
					(sum & 0x28) | (AF & 0xc4) | (temp & 1);
				break;
			case 0x10:			/* DJNZ dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				if ((BC -= 0x100) & 0xff00) {
					PCQ_ENTRY(PC - 1);
					PC += (signed char) GetBYTE(PC) + 1;
				}
				else {
					PC++;
				}
				break;
			case 0x11:			/* LD DE,nnnn */
				sim_brk_pend = FALSE;
				DE = GetWORD(PC);
				PC += 2;
				break;
			case 0x12:			/* LD (DE),A */
				CheckBreakByte(DE)
				PutBYTE(DE, hreg(AF));
				break;
			case 0x13:			/* INC DE */
				sim_brk_pend = FALSE;
				++DE;
				break;
			case 0x14:			/* INC D */
				sim_brk_pend = FALSE;
				DE += 0x100;
				temp = hreg(DE);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x15:			/* DEC D */
				sim_brk_pend = FALSE;
				DE -= 0x100;
				temp = hreg(DE);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x16:			/* LD D,nn */
				sim_brk_pend = FALSE;
				Sethreg(DE, RAM_pp(PC));
				break;
			case 0x17:			/* RLA */
				sim_brk_pend = FALSE;
				AF = ((AF << 8) & 0x0100) | ((AF >> 7) & 0x28) | ((AF << 1) & ~0x01ff) |
					(AF & 0xc4) | ((AF >> 15) & 1);
				break;
			case 0x18:			/* JR dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				PCQ_ENTRY(PC - 1);
				PC += (signed char) GetBYTE(PC) + 1;
				break;
			case 0x19:			/* ADD HL,DE */
				sim_brk_pend = FALSE;
				HL &= ADDRMASK;
				DE &= ADDRMASK;
				sum = HL + DE;
				cbits = (HL ^ DE ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x1a:			/* LD A,(DE) */
				CheckBreakByte(DE)
				Sethreg(AF, GetBYTE(DE));
				break;
			case 0x1b:			/* DEC DE */
				sim_brk_pend = FALSE;
				--DE;
				break;
			case 0x1c:			/* INC E */
				sim_brk_pend = FALSE;
				temp = lreg(DE) + 1;
				Setlreg(DE, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x1d:			/* DEC E */
				sim_brk_pend = FALSE;
				temp = lreg(DE) - 1;
				Setlreg(DE, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x1e:			/* LD E,nn */
				sim_brk_pend = FALSE;
				Setlreg(DE, RAM_pp(PC));
				break;
			case 0x1f:			/* RRA */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				sum = temp >> 1;
				AF = ((AF & 1) << 15) | (sum << 8) |
					(sum & 0x28) | (AF & 0xc4) | (temp & 1);
				break;
			case 0x20:			/* JR NZ,dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				if (TSTFLAG(Z)) {
					PC++;
				}
				else {
					PCQ_ENTRY(PC - 1);
					PC += (signed char) GetBYTE(PC) + 1;
				}
				break;
			case 0x21:			/* LD HL,nnnn */
				sim_brk_pend = FALSE;
				HL = GetWORD(PC);
				PC += 2;
				break;
			case 0x22:			/* LD (nnnn),HL */
				temp = GetWORD(PC);
				CheckBreakWord(temp);
				PutWORD(temp, HL);
				PC += 2;
				break;
			case 0x23:			/* INC HL */
				sim_brk_pend = FALSE;
				++HL;
				break;
			case 0x24:			/* INC H */
				sim_brk_pend = FALSE;
				HL += 0x100;
				temp = hreg(HL);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x25:			/* DEC H */
				sim_brk_pend = FALSE;
				HL -= 0x100;
				temp = hreg(HL);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x26:			/* LD H,nn */
				sim_brk_pend = FALSE;
				Sethreg(HL, RAM_pp(PC));
				break;
			case 0x27:			/* DAA */
				sim_brk_pend = FALSE;
				acu = hreg(AF);
				temp = ldig(acu);
				cbits = TSTFLAG(C);
				if (TSTFLAG(N)) {	/* last operation was a subtract */
					int hd = cbits || acu > 0x99;
					if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
						if (temp > 5) {
							SETFLAG(H, 0);
						}
						acu -= 6;
						acu &= 0xff;
					}
					if (hd) {	/* adjust high digit */
						acu -= 0x160;
					}
				}
				else {			/* last operation was an add */
					if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
						SETFLAG(H, (temp > 9));
						acu += 6;
					}
					if (cbits || ((acu & 0x1f0) > 0x90)) {	/* adjust high digit */
						acu += 0x60;
					}
				}
				cbits |= (acu >> 8) & 1;
				acu &= 0xff;
				AF = (acu << 8) | (acu & 0xa8) | ((acu == 0) << 6) |
					(AF & 0x12) | partab[acu] | cbits;
				break;
			case 0x28:			/* JR Z,dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				if (TSTFLAG(Z)) {
					PCQ_ENTRY(PC - 1);
					PC += (signed char) GetBYTE(PC) + 1;
				}
				else {
					PC++;
				}
				break;
			case 0x29:			/* ADD HL,HL */
				sim_brk_pend = FALSE;
				HL &= ADDRMASK;
				sum = HL + HL;
				cbits = (HL ^ HL ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x2a:			/* LD HL,(nnnn) */
				temp = GetWORD(PC);
				CheckBreakWord(temp);
				HL = GetWORD(temp);
				PC += 2;
				break;
			case 0x2b:			/* DEC HL */
				sim_brk_pend = FALSE;
				--HL;
				break;
			case 0x2c:			/* INC L */
				sim_brk_pend = FALSE;
				temp = lreg(HL) + 1;
				Setlreg(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x2d:			/* DEC L */
				sim_brk_pend = FALSE;
				temp = lreg(HL) - 1;
				Setlreg(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x2e:			/* LD L,nn */
				sim_brk_pend = FALSE;
				Setlreg(HL, RAM_pp(PC));
				break;
			case 0x2f:			/* CPL */
				sim_brk_pend = FALSE;
				AF = (~AF & ~0xff) | (AF & 0xc5) | ((~AF >> 8) & 0x28) | 0x12;
				break;
			case 0x30:			/* JR NC,dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				if (TSTFLAG(C)) {
					PC++;
				}
				else {
					PCQ_ENTRY(PC - 1);
					PC += (signed char) GetBYTE(PC) + 1;
				}
				break;
			case 0x31:			/* LD SP,nnnn */
				sim_brk_pend = FALSE;
				SP = GetWORD(PC);
				PC += 2;
				break;
			case 0x32:			/* LD (nnnn),A */
				temp = GetWORD(PC);
				CheckBreakByte(temp);
				PutBYTE(temp, hreg(AF));
				PC += 2;
				break;
			case 0x33:			/* INC SP */
				sim_brk_pend = FALSE;
				++SP;
				break;
			case 0x34:			/* INC (HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL) + 1;
				PutBYTE(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x35:			/* DEC (HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL) - 1;
				PutBYTE(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x36:			/* LD (HL),nn */
				CheckBreakByte(HL);
				PutBYTE(HL, RAM_pp(PC));
				break;
			case 0x37:			/* SCF */
				sim_brk_pend = FALSE;
				AF = (AF&~0x3b)|((AF>>8)&0x28)|1;
				break;
			case 0x38:			/* JR C,dd */
				sim_brk_pend = FALSE;
				checkCPU8080;
				if (TSTFLAG(C)) {
					PCQ_ENTRY(PC - 1);
					PC += (signed char) GetBYTE(PC) + 1;
				}
				else {
					PC++;
				}
				break;
			case 0x39:			/* ADD HL,SP */
				sim_brk_pend = FALSE;
				HL &= ADDRMASK;
				SP &= ADDRMASK;
				sum = HL + SP;
				cbits = (HL ^ SP ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x3a:			/* LD A,(nnnn) */
				temp = GetWORD(PC);
				CheckBreakByte(temp);
				Sethreg(AF, GetBYTE(temp));
				PC += 2;
				break;
			case 0x3b:			/* DEC SP */
				sim_brk_pend = FALSE;
				--SP;
				break;
			case 0x3c:			/* INC A */
				sim_brk_pend = FALSE;
				AF += 0x100;
				temp = hreg(AF);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x3d:			/* DEC A */
				sim_brk_pend = FALSE;
				AF -= 0x100;
				temp = hreg(AF);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x3e:			/* LD A,nn */
				sim_brk_pend = FALSE;
				Sethreg(AF, RAM_pp(PC));
				break;
			case 0x3f:			/* CCF */
				sim_brk_pend = FALSE;
				AF = (AF&~0x3b)|((AF>>8)&0x28)|((AF&1)<<4)|(~AF&1);
				break;
			case 0x40:			/* LD B,B */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x41:			/* LD B,C */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | ((BC & 255) << 8);
				break;
			case 0x42:			/* LD B,D */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | (DE & ~255);
				break;
			case 0x43:			/* LD B,E */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | ((DE & 255) << 8);
				break;
			case 0x44:			/* LD B,H */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | (HL & ~255);
				break;
			case 0x45:			/* LD B,L */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | ((HL & 255) << 8);
				break;
			case 0x46:			/* LD B,(HL) */
				CheckBreakByte(HL);
				Sethreg(BC, GetBYTE(HL));
				break;
			case 0x47:			/* LD B,A */
				sim_brk_pend = FALSE;
				BC = (BC & 255) | (AF & ~255);
				break;
			case 0x48:			/* LD C,B */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | ((BC >> 8) & 255);
				break;
			case 0x49:			/* LD C,C */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x4a:			/* LD C,D */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | ((DE >> 8) & 255);
				break;
			case 0x4b:			/* LD C,E */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | (DE & 255);
				break;
			case 0x4c:			/* LD C,H */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | ((HL >> 8) & 255);
				break;
			case 0x4d:			/* LD C,L */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | (HL & 255);
				break;
			case 0x4e:			/* LD C,(HL) */
				CheckBreakByte(HL);
				Setlreg(BC, GetBYTE(HL));
				break;
			case 0x4f:			/* LD C,A */
				sim_brk_pend = FALSE;
				BC = (BC & ~255) | ((AF >> 8) & 255);
				break;
			case 0x50:			/* LD D,B */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | (BC & ~255);
				break;
			case 0x51:			/* LD D,C */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | ((BC & 255) << 8);
				break;
			case 0x52:			/* LD D,D */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x53:			/* LD D,E */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | ((DE & 255) << 8);
				break;
			case 0x54:			/* LD D,H */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | (HL & ~255);
				break;
			case 0x55:			/* LD D,L */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | ((HL & 255) << 8);
				break;
			case 0x56:			/* LD D,(HL) */
				CheckBreakByte(HL);
				Sethreg(DE, GetBYTE(HL));
				break;
			case 0x57:			/* LD D,A */
				sim_brk_pend = FALSE;
				DE = (DE & 255) | (AF & ~255);
				break;
			case 0x58:			/* LD E,B */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | ((BC >> 8) & 255);
				break;
			case 0x59:			/* LD E,C */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | (BC & 255);
				break;
			case 0x5a:			/* LD E,D */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | ((DE >> 8) & 255);
				break;
			case 0x5b:			/* LD E,E */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x5c:			/* LD E,H */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | ((HL >> 8) & 255);
				break;
			case 0x5d:			/* LD E,L */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | (HL & 255);
				break;
			case 0x5e:			/* LD E,(HL) */
				CheckBreakByte(HL);
				Setlreg(DE, GetBYTE(HL));
				break;
			case 0x5f:			/* LD E,A */
				sim_brk_pend = FALSE;
				DE = (DE & ~255) | ((AF >> 8) & 255);
				break;
			case 0x60:			/* LD H,B */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | (BC & ~255);
				break;
			case 0x61:			/* LD H,C */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | ((BC & 255) << 8);
				break;
			case 0x62:			/* LD H,D */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | (DE & ~255);
				break;
			case 0x63:			/* LD H,E */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | ((DE & 255) << 8);
				break;
			case 0x64:			/* LD H,H */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x65:			/* LD H,L */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | ((HL & 255) << 8);
				break;
			case 0x66:			/* LD H,(HL) */
				CheckBreakByte(HL);
				Sethreg(HL, GetBYTE(HL));
				break;
			case 0x67:			/* LD H,A */
				sim_brk_pend = FALSE;
				HL = (HL & 255) | (AF & ~255);
				break;
			case 0x68:			/* LD L,B */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | ((BC >> 8) & 255);
				break;
			case 0x69:			/* LD L,C */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | (BC & 255);
				break;
			case 0x6a:			/* LD L,D */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | ((DE >> 8) & 255);
				break;
			case 0x6b:			/* LD L,E */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | (DE & 255);
				break;
			case 0x6c:			/* LD L,H */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | ((HL >> 8) & 255);
				break;
			case 0x6d:			/* LD L,L */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x6e:			/* LD L,(HL) */
				CheckBreakByte(HL);
				Setlreg(HL, GetBYTE(HL));
				break;
			case 0x6f:			/* LD L,A */
				sim_brk_pend = FALSE;
				HL = (HL & ~255) | ((AF >> 8) & 255);
				break;
			case 0x70:			/* LD (HL),B */
				CheckBreakByte(HL);
				PutBYTE(HL, hreg(BC));
				break;
			case 0x71:			/* LD (HL),C */
				CheckBreakByte(HL);
				PutBYTE(HL, lreg(BC));
				break;
			case 0x72:			/* LD (HL),D */
				CheckBreakByte(HL);
				PutBYTE(HL, hreg(DE));
				break;
			case 0x73:			/* LD (HL),E */
				CheckBreakByte(HL);
				PutBYTE(HL, lreg(DE));
				break;
			case 0x74:			/* LD (HL),H */
				CheckBreakByte(HL);
				PutBYTE(HL, hreg(HL));
				break;
			case 0x75:			/* LD (HL),L */
				CheckBreakByte(HL);
				PutBYTE(HL, lreg(HL));
				break;
			case 0x76:			/* HALT */
				sim_brk_pend = FALSE;
				reason = STOP_HALT;
				PC--;
				goto end_decode;
			case 0x77:			/* LD (HL),A */
				CheckBreakByte(HL);
				PutBYTE(HL, hreg(AF));
				break;
			case 0x78:			/* LD A,B */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | (BC & ~255);
				break;
			case 0x79:			/* LD A,C */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | ((BC & 255) << 8);
				break;
			case 0x7a:			/* LD A,D */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | (DE & ~255);
				break;
			case 0x7b:			/* LD A,E */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | ((DE & 255) << 8);
				break;
			case 0x7c:			/* LD A,H */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | (HL & ~255);
				break;
			case 0x7d:			/* LD A,L */
				sim_brk_pend = FALSE;
				AF = (AF & 255) | ((HL & 255) << 8);
				break;
			case 0x7e:			/* LD A,(HL) */
				CheckBreakByte(HL);
				Sethreg(AF, GetBYTE(HL));
				break;
			case 0x7f:			/* LD A,A */
				sim_brk_pend = FALSE;
				/* nop */
				break;
			case 0x80:			/* ADD A,B */
				sim_brk_pend = FALSE;
				temp = hreg(BC);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x81:			/* ADD A,C */
				sim_brk_pend = FALSE;
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x82:			/* ADD A,D */
				sim_brk_pend = FALSE;
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x83:			/* ADD A,E */
				sim_brk_pend = FALSE;
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x84:			/* ADD A,H */
				sim_brk_pend = FALSE;
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x85:			/* ADD A,L */
				sim_brk_pend = FALSE;
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x86:			/* ADD A,(HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x87:			/* ADD A,A */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x88:			/* ADC A,B */
				sim_brk_pend = FALSE;
				temp = hreg(BC);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x89:			/* ADC A,C */
				sim_brk_pend = FALSE;
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8a:			/* ADC A,D */
				sim_brk_pend = FALSE;
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8b:			/* ADC A,E */
				sim_brk_pend = FALSE;
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8c:			/* ADC A,H */
				sim_brk_pend = FALSE;
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8d:			/* ADC A,L */
				sim_brk_pend = FALSE;
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8e:			/* ADC A,(HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8f:			/* ADC A,A */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x90:			/* SUB B */
				sim_brk_pend = FALSE;
				temp = hreg(BC);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x91:			/* SUB C */
				sim_brk_pend = FALSE;
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x92:			/* SUB D */
				sim_brk_pend = FALSE;
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x93:			/* SUB E */
				sim_brk_pend = FALSE;
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x94:			/* SUB H */
				sim_brk_pend = FALSE;
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x95:			/* SUB L */
				sim_brk_pend = FALSE;
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x96:			/* SUB (HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x97:			/* SUB A */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x98:			/* SBC A,B */
				sim_brk_pend = FALSE;
				temp = hreg(BC);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x99:			/* SBC A,C */
				sim_brk_pend = FALSE;
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9a:			/* SBC A,D */
				sim_brk_pend = FALSE;
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9b:			/* SBC A,E */
				sim_brk_pend = FALSE;
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9c:			/* SBC A,H */
				sim_brk_pend = FALSE;
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9d:			/* SBC A,L */
				sim_brk_pend = FALSE;
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9e:			/* SBC A,(HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9f:			/* SBC A,A */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xa0:			/* AND B */
				sim_brk_pend = FALSE;
				sum = ((AF & (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xa1:			/* AND C */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) & BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xa2:			/* AND D */
				sim_brk_pend = FALSE;
				sum = ((AF & (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xa3:			/* AND E */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) & DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xa4:			/* AND H */
				sim_brk_pend = FALSE;
				sum = ((AF & (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xa5:			/* AND L */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) & HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xa6:			/* AND (HL) */
				CheckBreakByte(HL);
				sum = ((AF >> 8) & GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xa7:			/* AND A */
				sim_brk_pend = FALSE;
				sum = ((AF & (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xa8:			/* XOR B */
				sim_brk_pend = FALSE;
				sum = ((AF ^ (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xa9:			/* XOR C */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) ^ BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xaa:			/* XOR D */
				sim_brk_pend = FALSE;
				sum = ((AF ^ (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xab:			/* XOR E */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) ^ DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xac:			/* XOR H */
				sim_brk_pend = FALSE;
				sum = ((AF ^ (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xad:			/* XOR L */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) ^ HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xae:			/* XOR (HL) */
				CheckBreakByte(HL);
				sum = ((AF >> 8) ^ GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xaf:			/* XOR A */
				sim_brk_pend = FALSE;
				sum = ((AF ^ (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb0:			/* OR B */
				sim_brk_pend = FALSE;
				sum = ((AF | (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb1:			/* OR C */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) | BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb2:			/* OR D */
				sim_brk_pend = FALSE;
				sum = ((AF | (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb3:			/* OR E */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) | DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb4:			/* OR H */
				sim_brk_pend = FALSE;
				sum = ((AF | (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb5:			/* OR L */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) | HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb6:			/* OR (HL) */
				CheckBreakByte(HL);
				sum = ((AF >> 8) | GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb7:			/* OR A */
				sim_brk_pend = FALSE;
				sum = ((AF | (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xb8:			/* CP B */
				sim_brk_pend = FALSE;
				temp = hreg(BC);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xb9:			/* CP C */
				sim_brk_pend = FALSE;
				temp = lreg(BC);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xba:			/* CP D */
				sim_brk_pend = FALSE;
				temp = hreg(DE);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xbb:			/* CP E */
				sim_brk_pend = FALSE;
				temp = lreg(DE);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xbc:			/* CP H */
				sim_brk_pend = FALSE;
				temp = hreg(HL);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xbd:			/* CP L */
				sim_brk_pend = FALSE;
				temp = lreg(HL);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xbe:			/* CP (HL) */
				CheckBreakByte(HL);
				temp = GetBYTE(HL);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xbf:			/* CP A */
				sim_brk_pend = FALSE;
				temp = hreg(AF);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xc0:			/* RET NZ */
				if (TSTFLAG(Z)) {
					sim_brk_pend = FALSE;
				}
				else {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				break;
			case 0xc1:			/* POP BC */
				CheckBreakWord(SP);
				POP(BC);
				break;
			case 0xc2:			/* JP NZ,nnnn */
				sim_brk_pend = FALSE;
				JPC(!TSTFLAG(Z));
				break;
			case 0xc3:			/* JP nnnn */
				sim_brk_pend = FALSE;
				JPC(1);
				break;
			case 0xc4:			/* CALL NZ,nnnn */
				CALLC(!TSTFLAG(Z));
				break;
			case 0xc5:			/* PUSH BC */
				CheckBreakWord(SP - 2);
				PUSH(BC);
				break;
			case 0xc6:			/* ADD A,nn */
				sim_brk_pend = FALSE;
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0xc7:			/* RST 0 */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0;
				break;
			case 0xc8:			/* RET Z */
				if (TSTFLAG(Z)) {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				else {
					sim_brk_pend = FALSE;
				}
				break;
			case 0xc9:			/* RET */
				CheckBreakWord(SP);
				PCQ_ENTRY(PC - 1);
				POP(PC);
				break;
			case 0xca:			/* JP Z,nnnn */
				sim_brk_pend = FALSE;
				JPC(TSTFLAG(Z));
				break;
			case 0xcb:			/* CB prefix */
				checkCPU8080;
				adr = HL;
				switch ((op = GetBYTE(PC)) & 7) {
				case 0: sim_brk_pend = FALSE; ++PC; acu = hreg(BC); break;
				case 1: sim_brk_pend = FALSE; ++PC; acu = lreg(BC); break;
				case 2: sim_brk_pend = FALSE; ++PC; acu = hreg(DE); break;
				case 3: sim_brk_pend = FALSE; ++PC; acu = lreg(DE); break;
				case 4: sim_brk_pend = FALSE; ++PC; acu = hreg(HL); break;
				case 5: sim_brk_pend = FALSE; ++PC; acu = lreg(HL); break;
				case 6: CheckBreakByte(adr); ++PC; acu = GetBYTE(adr); break;
				case 7: sim_brk_pend = FALSE; ++PC; acu = hreg(AF); break;
				}
				switch (op & 0xc0) {
				case 0x00:		/* shift/rotate */
					switch (op & 0x38) {
					case 0x00:	/* RLC */
						temp = (acu << 1) | (acu >> 7);
						cbits = temp & 1;
						goto cbshflg1;
					case 0x08:	/* RRC */
						temp = (acu >> 1) | (acu << 7);
						cbits = temp & 0x80;
						goto cbshflg1;
					case 0x10:	/* RL */
						temp = (acu << 1) | TSTFLAG(C);
						cbits = acu & 0x80;
						goto cbshflg1;
					case 0x18:	/* RR */
						temp = (acu >> 1) | (TSTFLAG(C) << 7);
						cbits = acu & 1;
						goto cbshflg1;
					case 0x20:	/* SLA */
						temp = acu << 1;
						cbits = acu & 0x80;
						goto cbshflg1;
					case 0x28:	/* SRA */
						temp = (acu >> 1) | (acu & 0x80);
						cbits = acu & 1;
						goto cbshflg1;
					case 0x30:	/* SLIA */
						temp = (acu << 1) | 1;
						cbits = acu & 0x80;
						goto cbshflg1;
					case 0x38:	/* SRL */
						temp = acu >> 1;
						cbits = acu & 1;
					cbshflg1:
						AF = (AF & ~0xff) | (temp & 0xa8) |
							(((temp & 0xff) == 0) << 6) |
							parity(temp) | !!cbits;
					}
					break;
				case 0x40:		/* BIT */
					if (acu & (1 << ((op >> 3) & 7))) {
						AF = (AF & ~0xfe) | 0x10 |
						(((op & 0x38) == 0x38) << 7);
					}
					else {
						AF = (AF & ~0xfe) | 0x54;
					}
					if ((op&7) != 6) {
						AF |= (acu & 0x28);
					}
					temp = acu;
					break;
				case 0x80:		/* RES */
					temp = acu & ~(1 << ((op >> 3) & 7));
					break;
				case 0xc0:		/* SET */
					temp = acu | (1 << ((op >> 3) & 7));
					break;
				}
				switch (op & 7) {
				case 0: Sethreg(BC, temp); break;
				case 1: Setlreg(BC, temp); break;
				case 2: Sethreg(DE, temp); break;
				case 3: Setlreg(DE, temp); break;
				case 4: Sethreg(HL, temp); break;
				case 5: Setlreg(HL, temp); break;
				case 6: PutBYTE(adr, temp); break;
				case 7: Sethreg(AF, temp); break;
				}
				break;
			case 0xcc:			/* CALL Z,nnnn */
				CALLC(TSTFLAG(Z));
				break;
			case 0xcd:			/* CALL nnnn */
				CALLC(1);
				break;
			case 0xce:			/* ADC A,nn */
				sim_brk_pend = FALSE;
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0xcf:			/* RST 8 */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 8;
				break;
			case 0xd0:			/* RET NC */
				if (TSTFLAG(C)) {
					sim_brk_pend = FALSE;
				}
				else {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				break;
			case 0xd1:			/* POP DE */
				CheckBreakWord(SP);
				POP(DE);
				break;
			case 0xd2:			/* JP NC,nnnn */
				sim_brk_pend = FALSE;
				JPC(!TSTFLAG(C));
				break;
			case 0xd3:			/* OUT (nn),A */
				sim_brk_pend = FALSE;
				out(RAM_pp(PC), hreg(AF));
				break;
			case 0xd4:			/* CALL NC,nnnn */
				CALLC(!TSTFLAG(C));
				break;
			case 0xd5:			/* PUSH DE */
				CheckBreakWord(SP - 2);
				PUSH(DE);
				break;
			case 0xd6:			/* SUB nn */
				sim_brk_pend = FALSE;
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xd7:			/* RST 10H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x10;
				break;
			case 0xd8:			/* RET C */
				if (TSTFLAG(C)) {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				else {
					sim_brk_pend = FALSE;
				}
				break;
			case 0xd9:			/* EXX */
				sim_brk_pend = FALSE;
				checkCPU8080;
				regs[regs_sel].bc = BC;
				regs[regs_sel].de = DE;
				regs[regs_sel].hl = HL;
				regs_sel = 1 - regs_sel;
				BC = regs[regs_sel].bc;
				DE = regs[regs_sel].de;
				HL = regs[regs_sel].hl;
				break;
			case 0xda:			/* JP C,nnnn */
				sim_brk_pend = FALSE;
				JPC(TSTFLAG(C));
				break;
			case 0xdb:			/* IN A,(nn) */
				sim_brk_pend = FALSE;
				Sethreg(AF, in(RAM_pp(PC)));
				break;
			case 0xdc:			/* CALL C,nnnn */
				CALLC(TSTFLAG(C));
				break;
			case 0xdd:			/* DD prefix */
				checkCPU8080;
				switch (op = RAM_pp(PC)) {
				case 0x09:			/* ADD IX,BC */
					sim_brk_pend = FALSE;
					IX &= ADDRMASK;
					BC &= ADDRMASK;
					sum = IX + BC;
					cbits = (IX ^ BC ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x19:			/* ADD IX,DE */
					sim_brk_pend = FALSE;
					IX &= ADDRMASK;
					DE &= ADDRMASK;
					sum = IX + DE;
					cbits = (IX ^ DE ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x21:			/* LD IX,nnnn */
					sim_brk_pend = FALSE;
					IX = GetWORD(PC);
					PC += 2;
					break;
				case 0x22:			/* LD (nnnn),IX */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, IX);
					PC += 2;
					break;
				case 0x23:			/* INC IX */
					sim_brk_pend = FALSE;
					++IX;
					break;
				case 0x24:			/* INC IXH */
					sim_brk_pend = FALSE;
					IX += 0x100;
					temp = hreg(IX);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x25:			/* DEC IXH */
					sim_brk_pend = FALSE;
					IX -= 0x100;
					temp = hreg(IX);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x26:			/* LD IXH,nn */
					sim_brk_pend = FALSE;
					Sethreg(IX, RAM_pp(PC));
					break;
				case 0x29:			/* ADD IX,IX */
					sim_brk_pend = FALSE;
					IX &= ADDRMASK;
					sum = IX + IX;
					cbits = (IX ^ IX ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x2a:			/* LD IX,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					IX = GetWORD(temp);
					PC += 2;
					break;
				case 0x2b:			/* DEC IX */
					sim_brk_pend = FALSE;
					--IX;
					break;
				case 0x2c:			/* INC IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX) + 1;
					Setlreg(IX, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x2d:			/* DEC IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX) - 1;
					Setlreg(IX, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x2e:			/* LD IXL,nn */
					sim_brk_pend = FALSE;
					Setlreg(IX, RAM_pp(PC));
					break;
				case 0x34:			/* INC (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr) + 1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x35:			/* DEC (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr) - 1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x36:			/* LD (IX+dd),nn */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, RAM_pp(PC));
					break;
				case 0x39:			/* ADD IX,SP */
					sim_brk_pend = FALSE;
					IX &= ADDRMASK;
					SP &= ADDRMASK;
					sum = IX + SP;
					cbits = (IX ^ SP ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x44:			/* LD B,IXH */
					sim_brk_pend = FALSE;
					Sethreg(BC, hreg(IX));
					break;
				case 0x45:			/* LD B,IXL */
					sim_brk_pend = FALSE;
					Sethreg(BC, lreg(IX));
					break;
				case 0x46:			/* LD B,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(BC, GetBYTE(adr));
					break;
				case 0x4c:			/* LD C,IXH */
					sim_brk_pend = FALSE;
					Setlreg(BC, hreg(IX));
					break;
				case 0x4d:			/* LD C,IXL */
					sim_brk_pend = FALSE;
					Setlreg(BC, lreg(IX));
					break;
				case 0x4e:			/* LD C,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(BC, GetBYTE(adr));
					break;
				case 0x54:			/* LD D,IXH */
					sim_brk_pend = FALSE;
					Sethreg(DE, hreg(IX));
					break;
				case 0x55:			/* LD D,IXL */
					sim_brk_pend = FALSE;
					Sethreg(DE, lreg(IX));
					break;
				case 0x56:			/* LD D,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(DE, GetBYTE(adr));
					break;
				case 0x5c:			/* LD E,H */
					sim_brk_pend = FALSE;
					Setlreg(DE, hreg(IX));
					break;
				case 0x5d:			/* LD E,L */
					sim_brk_pend = FALSE;
					Setlreg(DE, lreg(IX));
					break;
				case 0x5e:			/* LD E,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(DE, GetBYTE(adr));
					break;
				case 0x60:			/* LD IXH,B */
					sim_brk_pend = FALSE;
					Sethreg(IX, hreg(BC));
					break;
				case 0x61:			/* LD IXH,C */
					sim_brk_pend = FALSE;
					Sethreg(IX, lreg(BC));
					break;
				case 0x62:			/* LD IXH,D */
					sim_brk_pend = FALSE;
					Sethreg(IX, hreg(DE));
					break;
				case 0x63:			/* LD IXH,E */
					sim_brk_pend = FALSE;
					Sethreg(IX, lreg(DE));
					break;
				case 0x64:			/* LD IXH,IXH */
					sim_brk_pend = FALSE;
					/* nop */
					break;
				case 0x65:			/* LD IXH,IXL */
					sim_brk_pend = FALSE;
					Sethreg(IX, lreg(IX));
					break;
				case 0x66:			/* LD H,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(HL, GetBYTE(adr));
					break;
				case 0x67:			/* LD IXH,A */
					sim_brk_pend = FALSE;
					Sethreg(IX, hreg(AF));
					break;
				case 0x68:			/* LD IXL,B */
					sim_brk_pend = FALSE;
					Setlreg(IX, hreg(BC));
					break;
				case 0x69:			/* LD IXL,C */
					sim_brk_pend = FALSE;
					Setlreg(IX, lreg(BC));
					break;
				case 0x6a:			/* LD IXL,D */
					sim_brk_pend = FALSE;
					Setlreg(IX, hreg(DE));
					break;
				case 0x6b:			/* LD IXL,E */
					sim_brk_pend = FALSE;
					Setlreg(IX, lreg(DE));
					break;
				case 0x6c:			/* LD IXL,IXH */
					sim_brk_pend = FALSE;
					Setlreg(IX, hreg(IX));
					break;
				case 0x6d:			/* LD IXL,IXL */
					sim_brk_pend = FALSE;
					/* nop */
					break;
				case 0x6e:			/* LD L,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(HL, GetBYTE(adr));
					break;
				case 0x6f:			/* LD IXL,A */
					sim_brk_pend = FALSE;
					Setlreg(IX, hreg(AF));
					break;
				case 0x70:			/* LD (IX+dd),B */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(BC));
					break;
				case 0x71:			/* LD (IX+dd),C */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(BC));
					break;
				case 0x72:			/* LD (IX+dd),D */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(DE));
					break;
				case 0x73:			/* LD (IX+dd),E */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(DE));
					break;
				case 0x74:			/* LD (IX+dd),H */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(HL));
					break;
				case 0x75:			/* LD (IX+dd),L */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(HL));
					break;
				case 0x77:			/* LD (IX+dd),A */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(AF));
					break;
				case 0x7c:			/* LD A,IXH */
					sim_brk_pend = FALSE;
					Sethreg(AF, hreg(IX));
					break;
				case 0x7d:			/* LD A,IXL */
					sim_brk_pend = FALSE;
					Sethreg(AF, lreg(IX));
					break;
				case 0x7e:			/* LD A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(AF, GetBYTE(adr));
					break;
				case 0x84:			/* ADD A,IXH */
					sim_brk_pend = FALSE;
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x85:			/* ADD A,IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x86:			/* ADD A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8c:			/* ADC A,IXH */
					sim_brk_pend = FALSE;
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8d:			/* ADC A,IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8e:			/* ADC A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x94:			/* SUB IXH */
					sim_brk_pend = FALSE;
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x95:			/* SUB IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x96:			/* SUB (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9c:			/* SBC A,IXH */
					sim_brk_pend = FALSE;
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9d:			/* SBC A,IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9e:			/* SBC A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0xa4:			/* AND IXH */
					sim_brk_pend = FALSE;
					sum = ((AF & (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) |
						((sum == 0) << 6) | 0x10 | partab[sum];
					break;
				case 0xa5:			/* AND IXL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) & IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xa6:			/* AND (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) & GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xac:			/* XOR IXH */
					sim_brk_pend = FALSE;
					sum = ((AF ^ (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xad:			/* XOR IXL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) ^ IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xae:			/* XOR (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) ^ GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb4:			/* OR IXH */
					sim_brk_pend = FALSE;
					sum = ((AF | (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb5:			/* OR IXL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) | IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb6:			/* OR (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) | GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xbc:			/* CP IXH */
					sim_brk_pend = FALSE;
					temp = hreg(IX);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xbd:			/* CP IXL */
					sim_brk_pend = FALSE;
					temp = lreg(IX);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xbe:			/* CP (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |

						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xcb:			/* CB prefix */
					adr = IX + (signed char) RAM_pp(PC);
					switch ((op = GetBYTE(PC)) & 7) {
					case 0: sim_brk_pend = FALSE; ++PC; acu = hreg(BC); break;
					case 1: sim_brk_pend = FALSE; ++PC; acu = lreg(BC); break;
					case 2: sim_brk_pend = FALSE; ++PC; acu = hreg(DE); break;
					case 3: sim_brk_pend = FALSE; ++PC; acu = lreg(DE); break;
					case 4: sim_brk_pend = FALSE; ++PC; acu = hreg(HL); break;
					case 5: sim_brk_pend = FALSE; ++PC; acu = lreg(HL); break;
					case 6: CheckBreakByte(adr); ++PC; acu = GetBYTE(adr); break;
					case 7: sim_brk_pend = FALSE; ++PC; acu = hreg(AF); break;
					}
					switch (op & 0xc0) {
					case 0x00:		/* shift/rotate */
						switch (op & 0x38) {
						case 0x00:	/* RLC */
							temp = (acu << 1) | (acu >> 7);
							cbits = temp & 1;
							goto cbshflg2;
						case 0x08:	/* RRC */
							temp = (acu >> 1) | (acu << 7);
							cbits = temp & 0x80;
							goto cbshflg2;
						case 0x10:	/* RL */
							temp = (acu << 1) | TSTFLAG(C);
							cbits = acu & 0x80;
							goto cbshflg2;
						case 0x18:	/* RR */
							temp = (acu >> 1) | (TSTFLAG(C) << 7);
							cbits = acu & 1;
							goto cbshflg2;
						case 0x20:	/* SLA */
							temp = acu << 1;
							cbits = acu & 0x80;
							goto cbshflg2;
						case 0x28:	/* SRA */
							temp = (acu >> 1) | (acu & 0x80);
							cbits = acu & 1;
							goto cbshflg2;
						case 0x30:	/* SLIA */
							temp = (acu << 1) | 1;
							cbits = acu & 0x80;
							goto cbshflg2;
						case 0x38:	/* SRL */
							temp = acu >> 1;
							cbits = acu & 1;
						cbshflg2:
							AF = (AF & ~0xff) | (temp & 0xa8) |
								(((temp & 0xff) == 0) << 6) |
								parity(temp) | !!cbits;
						}
						break;
					case 0x40:		/* BIT */
						if (acu & (1 << ((op >> 3) & 7))) {
							AF = (AF & ~0xfe) | 0x10 |
							(((op & 0x38) == 0x38) << 7);
						}
						else {
							AF = (AF & ~0xfe) | 0x54;
						}
						if ((op&7) != 6) {
							AF |= (acu & 0x28);
						}
						temp = acu;
						break;
					case 0x80:		/* RES */
						temp = acu & ~(1 << ((op >> 3) & 7));
						break;
					case 0xc0:		/* SET */
						temp = acu | (1 << ((op >> 3) & 7));
						break;
					}
					switch (op & 7) {
					case 0: Sethreg(BC, temp); break;
					case 1: Setlreg(BC, temp); break;
					case 2: Sethreg(DE, temp); break;
					case 3: Setlreg(DE, temp); break;
					case 4: Sethreg(HL, temp); break;
					case 5: Setlreg(HL, temp); break;
					case 6: PutBYTE(adr, temp); break;
					case 7: Sethreg(AF, temp); break;
					}
					break;
				case 0xe1:			/* POP IX */
					CheckBreakWord(SP);
					POP(IX);
					break;
				case 0xe3:			/* EX (SP),IX */
					CheckBreakWord(SP);
					temp = IX; POP(IX); PUSH(temp);
					break;
				case 0xe5:			/* PUSH IX */
					CheckBreakWord(SP - 2);
					PUSH(IX);
					break;
				case 0xe9:			/* JP (IX) */
					sim_brk_pend = FALSE;
					PCQ_ENTRY(PC - 2);
					PC = IX;
					break;
				case 0xf9:			/* LD SP,IX */
					sim_brk_pend = FALSE;
					SP = IX;
					break;
				default:				/* ignore DD */
					sim_brk_pend = FALSE;
					checkCPUZ80;
					PC--;
				}
				break;
			case 0xde:				/* SBC A,nn */
				sim_brk_pend = FALSE;
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xdf:			/* RST 18H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x18;
				break;
			case 0xe0:			/* RET PO */
				if (TSTFLAG(P)) {
					sim_brk_pend = FALSE;
				}
				else {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				break;
			case 0xe1:			/* POP HL */
				CheckBreakWord(SP);
				POP(HL);
				break;
			case 0xe2:			/* JP PO,nnnn */
				sim_brk_pend = FALSE;
				JPC(!TSTFLAG(P));
				break;
			case 0xe3:			/* EX (SP),HL */
				CheckBreakWord(SP);
				temp = HL; POP(HL); PUSH(temp);
				break;
			case 0xe4:			/* CALL PO,nnnn */
				CALLC(!TSTFLAG(P));
				break;
			case 0xe5:			/* PUSH HL */
				CheckBreakWord(SP - 2);
				PUSH(HL);
				break;
			case 0xe6:			/* AND nn */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) & RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xe7:			/* RST 20H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x20;
				break;
			case 0xe8:			/* RET PE */
				if (TSTFLAG(P)) {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				else {
					sim_brk_pend = FALSE;
				}
				break;
			case 0xe9:			/* JP (HL) */
				sim_brk_pend = FALSE;
				PCQ_ENTRY(PC - 1);
				PC = HL;
				break;
			case 0xea:			/* JP PE,nnnn */
				sim_brk_pend = FALSE;
				JPC(TSTFLAG(P));
				break;
			case 0xeb:			/* EX DE,HL */
				sim_brk_pend = FALSE;
				temp = HL; HL = DE; DE = temp;
				break;
			case 0xec:			/* CALL PE,nnnn */
				CALLC(TSTFLAG(P));
				break;
			case 0xed:			/* ED prefix */
				checkCPU8080;
				switch (op = RAM_pp(PC)) {
				case 0x40:			/* IN B,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Sethreg(BC, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x41:			/* OUT (C),B */
					sim_brk_pend = FALSE;
					out(lreg(BC), hreg(BC));
					break;
				case 0x42:			/* SBC HL,BC */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					BC &= ADDRMASK;
					sum = HL - BC - TSTFLAG(C);
					cbits = (HL ^ BC ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x43:			/* LD (nnnn),BC */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, BC);
					PC += 2;
					break;
				case 0x44:			/* NEG */
					sim_brk_pend = FALSE;
					temp = hreg(AF);
					AF = ((~(AF & 0xff00) + 1) & 0xff00); /* AF = (-(AF & 0xff00) & 0xff00); */
					AF |= ((AF >> 8) & 0xa8) | (((AF & 0xff00) == 0) << 6) |
						(((temp & 0x0f) != 0) << 4) |
						((temp == 0x80) << 2) |
						2 | (temp != 0);
					break;
				case 0x45:			/* RETN */
					IFF |= IFF >> 1;
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 2);
					POP(PC);
					break;
				case 0x46:			/* IM 0 */
					sim_brk_pend = FALSE;
					/* interrupt mode 0 */
					break;
				case 0x47:			/* LD I,A */
					sim_brk_pend = FALSE;
					ir = (ir & 255) | (AF & ~255);
					break;
				case 0x48:			/* IN C,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Setlreg(BC, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x49:			/* OUT (C),C */
					sim_brk_pend = FALSE;
					out(lreg(BC), lreg(BC));
					break;
				case 0x4a:			/* ADC HL,BC */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					BC &= ADDRMASK;
					sum = HL + BC + TSTFLAG(C);
					cbits = (HL ^ BC ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x4b:			/* LD BC,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					BC = GetWORD(temp);
					PC += 2;
					break;
				case 0x4d:			/* RETI */
					IFF |= IFF >> 1;
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 2);
					POP(PC);
					break;
				case 0x4f:			/* LD R,A */
					sim_brk_pend = FALSE;
					ir = (ir & ~255) | ((AF >> 8) & 255);
					break;
				case 0x50:			/* IN D,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Sethreg(DE, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x51:			/* OUT (C),D */
					sim_brk_pend = FALSE;
					out(lreg(BC), hreg(DE));
					break;
				case 0x52:			/* SBC HL,DE */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					DE &= ADDRMASK;
					sum = HL - DE - TSTFLAG(C);
					cbits = (HL ^ DE ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x53:			/* LD (nnnn),DE */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, DE);
					PC += 2;
					break;
				case 0x56:			/* IM 1 */
					sim_brk_pend = FALSE;
					/* interrupt mode 1 */
					break;
				case 0x57:			/* LD A,I */
					sim_brk_pend = FALSE;
					AF = (AF & 0x29) | (ir & ~255) | ((ir >> 8) & 0x80) | (((ir & ~255) == 0) << 6) | ((IFF & 2) << 1);
					break;
				case 0x58:			/* IN E,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Setlreg(DE, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x59:			/* OUT (C),E */
					sim_brk_pend = FALSE;
					out(lreg(BC), lreg(DE));
					break;
				case 0x5a:			/* ADC HL,DE */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					DE &= ADDRMASK;
					sum = HL + DE + TSTFLAG(C);
					cbits = (HL ^ DE ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x5b:			/* LD DE,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					DE = GetWORD(temp);
					PC += 2;
					break;
				case 0x5e:			/* IM 2 */
					sim_brk_pend = FALSE;
					/* interrupt mode 2 */
					break;
				case 0x5f:			/* LD A,R */
					sim_brk_pend = FALSE;
					AF = (AF & 0x29) | ((ir & 255) << 8) | (ir & 0x80) | (((ir & 255) == 0) << 6) | ((IFF & 2) << 1);
					break;
				case 0x60:			/* IN H,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Sethreg(HL, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x61:			/* OUT (C),H */
					sim_brk_pend = FALSE;
					out(lreg(BC), hreg(HL));
					break;
				case 0x62:			/* SBC HL,HL */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					sum = HL - HL - TSTFLAG(C);
					cbits = (HL ^ HL ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x63:			/* LD (nnnn),HL */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, HL);
					PC += 2;
					break;
				case 0x67:			/* RRD */
					sim_brk_pend = FALSE;
					temp = GetBYTE(HL);
					acu = hreg(AF);
					PutBYTE(HL, hdig(temp) | (ldig(acu) << 4));
					acu = (acu & 0xf0) | ldig(temp);
					AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
						partab[acu] | (AF & 1);
					break;
				case 0x68:			/* IN L,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Setlreg(HL, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x69:			/* OUT (C),L */
					sim_brk_pend = FALSE;
					out(lreg(BC), lreg(HL));
					break;
				case 0x6a:			/* ADC HL,HL */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					sum = HL + HL + TSTFLAG(C);
					cbits = (HL ^ HL ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x6b:			/* LD HL,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					HL = GetWORD(temp);
					PC += 2;
					break;
				case 0x6f:			/* RLD */
					sim_brk_pend = FALSE;
					temp = GetBYTE(HL);
					acu = hreg(AF);
					PutBYTE(HL, (ldig(temp) << 4) | ldig(acu));
					acu = (acu & 0xf0) | hdig(temp);
					AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
						partab[acu] | (AF & 1);
					break;
				case 0x70:			/* IN (C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Setlreg(temp, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x71:			/* OUT (C),0 */
					sim_brk_pend = FALSE;
					out(lreg(BC), 0);
					break;
				case 0x72:			/* SBC HL,SP */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					SP &= ADDRMASK;
					sum = HL - SP - TSTFLAG(C);
					cbits = (HL ^ SP ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x73:			/* LD (nnnn),SP */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, SP);
					PC += 2;
					break;
				case 0x78:			/* IN A,(C) */
					sim_brk_pend = FALSE;
					temp = in(lreg(BC));
					Sethreg(AF, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x79:			/* OUT (C),A */
					sim_brk_pend = FALSE;
					out(lreg(BC), hreg(AF));
					break;
				case 0x7a:			/* ADC HL,SP */
					sim_brk_pend = FALSE;
					HL &= ADDRMASK;
					SP &= ADDRMASK;
					sum = HL + SP + TSTFLAG(C);
					cbits = (HL ^ SP ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & ADDRMASK) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x7b:			/* LD SP,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					SP = GetWORD(temp);
					PC += 2;
					break;
				case 0xa0:			/* LDI */
					CheckBreakTwoBytes(HL, DE);
					acu = RAM_pp(HL);
					PutBYTE_pp(DE, acu);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
						(((--BC & ADDRMASK) != 0) << 2);
					break;
				case 0xa1:			/* CPI */
					CheckBreakByte(HL);
					acu = hreg(AF);
					temp = RAM_pp(HL);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
						((sum - ((cbits >> 4) & 1)) & 8) |
						((--BC & ADDRMASK) != 0) << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0) {
						AF &= ~8;
					}
					break;
				case 0xa2:			/* INI */
					CheckBreakByte(HL);
					PutBYTE(HL, in(lreg(BC))); ++HL;
					SETFLAG(N, 1);
					SETFLAG(P, (--BC & ADDRMASK) != 0);
					break;
				case 0xa3:			/* OUTI */
					CheckBreakByte(HL);
					out(lreg(BC), GetBYTE(HL)); ++HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xa8:			/* LDD */
					CheckBreakTwoBytes(HL, DE);
					acu = RAM_mm(HL);
					PutBYTE_mm(DE, acu);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
						(((--BC & ADDRMASK) != 0) << 2);
					break;
				case 0xa9:			/* CPD */
					CheckBreakByte(HL);
					acu = hreg(AF);
					temp = RAM_mm(HL);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
						((sum - ((cbits >> 4) & 1)) & 8) |
						((--BC & ADDRMASK) != 0) << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0) {
						AF &= ~8;
					}
					break;
				case 0xaa:			/* IND */
					CheckBreakByte(HL);
					PutBYTE(HL, in(lreg(BC))); --HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xab:			/* OUTD */
					CheckBreakByte(HL);
					out(lreg(BC), GetBYTE(HL)); --HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xb0:			/* LDIR */
					acu = hreg(AF);
					BC &= ADDRMASK;
					do {
						CheckBreakTwoBytes(HL, DE);
						acu = RAM_pp(HL);
						PutBYTE_pp(DE, acu);
					} while (--BC);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
					break;
				case 0xb1:			/* CPIR */
					acu = hreg(AF);
					BC &= ADDRMASK;
					do {
						CheckBreakByte(HL);
						temp = RAM_pp(HL);
						op = --BC != 0;
						sum = acu - temp;
					} while (op && sum != 0);
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) |
						(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
						op << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0) {
						AF &= ~8;
					}
					break;
				case 0xb2:			/* INIR */
					temp = hreg(BC);
					do {
						CheckBreakByte(HL);
						PutBYTE(HL, in(lreg(BC))); ++HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xb3:			/* OTIR */
					temp = hreg(BC);
					do {
						CheckBreakByte(HL);
						out(lreg(BC), GetBYTE(HL)); ++HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xb8:			/* LDDR */
					BC &= ADDRMASK;
					do {
						CheckBreakTwoBytes(HL, DE);
						acu = RAM_mm(HL);
						PutBYTE_mm(DE, acu);
					} while (--BC);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
					break;
				case 0xb9:			/* CPDR */
					acu = hreg(AF);
					BC &= ADDRMASK;
					do {
						CheckBreakByte(HL);
						temp = RAM_mm(HL);
						op = --BC != 0;
						sum = acu - temp;
					} while (op && sum != 0);
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) |
						(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
						op << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0) {
						AF &= ~8;
					}
					break;
				case 0xba:			/* INDR */
					temp = hreg(BC);
					do {
						CheckBreakByte(HL);
						PutBYTE(HL, in(lreg(BC))); --HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xbb:			/* OTDR */
					temp = hreg(BC);
					do {
						CheckBreakByte(HL);
						out(lreg(BC), GetBYTE(HL)); --HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				default:	/* ignore ED and following byte */
					sim_brk_pend = FALSE;
					checkCPUZ80;
				}
				break;
			case 0xee:			/* XOR nn */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) ^ RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xef:			/* RST 28H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x28;
				break;
			case 0xf0:			/* RET P */
				if (TSTFLAG(S)) {
					sim_brk_pend = FALSE;
				}
				else {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				break;
			case 0xf1:			/* POP AF */
				CheckBreakWord(SP);
				POP(AF);
				break;
			case 0xf2:			/* JP P,nnnn */
				sim_brk_pend = FALSE;
				JPC(!TSTFLAG(S));
				break;

			case 0xf3:			/* DI */
				sim_brk_pend = FALSE;
				IFF = 0;
				break;
			case 0xf4:			/* CALL P,nnnn */
				CALLC(!TSTFLAG(S));
				break;
			case 0xf5:			/* PUSH AF */
				CheckBreakWord(SP - 2);
				PUSH(AF);
				break;
			case 0xf6:			/* OR nn */
				sim_brk_pend = FALSE;
				sum = ((AF >> 8) | RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xf7:			/* RST 30H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x30;
				break;
			case 0xf8:			/* RET M */
				if (TSTFLAG(S)) {
					CheckBreakWord(SP);
					PCQ_ENTRY(PC - 1);
					POP(PC);
				}
				else {
					sim_brk_pend = FALSE;
				}
				break;
			case 0xf9:			/* LD SP,HL */
				sim_brk_pend = FALSE;
				SP = HL;
				break;
			case 0xfa:			/* JP M,nnnn */
				sim_brk_pend = FALSE;
				JPC(TSTFLAG(S));
				break;
			case 0xfb:			/* EI */
				sim_brk_pend = FALSE;
				IFF = 3;
				break;
			case 0xfc:			/* CALL M,nnnn */
				CALLC(TSTFLAG(S));
				break;
			case 0xfd:			/* FD prefix */
				checkCPU8080;
				switch (op = RAM_pp(PC)) {
				case 0x09:			/* ADD IY,BC */
					sim_brk_pend = FALSE;
					IY &= ADDRMASK;
					BC &= ADDRMASK;
					sum = IY + BC;
					cbits = (IY ^ BC ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x19:			/* ADD IY,DE */
					sim_brk_pend = FALSE;
					IY &= ADDRMASK;
					DE &= ADDRMASK;
					sum = IY + DE;
					cbits = (IY ^ DE ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x21:			/* LD IY,nnnn */
					sim_brk_pend = FALSE;
					IY = GetWORD(PC);
					PC += 2;
					break;
				case 0x22:			/* LD (nnnn),IY */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					PutWORD(temp, IY);
					PC += 2;
					break;
				case 0x23:			/* INC IY */
					sim_brk_pend = FALSE;
					++IY;
					break;
				case 0x24:			/* INC IYH */
					sim_brk_pend = FALSE;
					IY += 0x100;
					temp = hreg(IY);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x25:			/* DEC IYH */
					sim_brk_pend = FALSE;
					IY -= 0x100;
					temp = hreg(IY);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x26:			/* LD IYH,nn */
					sim_brk_pend = FALSE;
					Sethreg(IY, RAM_pp(PC));
					break;
				case 0x29:			/* ADD IY,IY */
					sim_brk_pend = FALSE;
					IY &= ADDRMASK;
					sum = IY + IY;
					cbits = (IY ^ IY ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x2a:			/* LD IY,(nnnn) */
					temp = GetWORD(PC);
					CheckBreakWord(temp);
					IY = GetWORD(temp);
					PC += 2;
					break;
				case 0x2b:			/* DEC IY */
					sim_brk_pend = FALSE;
					--IY;
					break;
				case 0x2c:			/* INC IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY) + 1;
					Setlreg(IY, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x2d:			/* DEC IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY) - 1;
					Setlreg(IY, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x2e:			/* LD IYL,nn */
					sim_brk_pend = FALSE;
					Setlreg(IY, RAM_pp(PC));
					break;
				case 0x34:			/* INC (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr) + 1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x35:			/* DEC (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr) - 1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x36:			/* LD (IY+dd),nn */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, RAM_pp(PC));
					break;
				case 0x39:			/* ADD IY,SP */
					sim_brk_pend = FALSE;
					IY &= ADDRMASK;
					SP &= ADDRMASK;
					sum = IY + SP;
					cbits = (IY ^ SP ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x44:			/* LD B,IYH */
					sim_brk_pend = FALSE;
					Sethreg(BC, hreg(IY));
					break;
				case 0x45:			/* LD B,IYL */
					sim_brk_pend = FALSE;
					Sethreg(BC, lreg(IY));
					break;
				case 0x46:			/* LD B,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(BC, GetBYTE(adr));
					break;
				case 0x4c:			/* LD C,IYH */
					sim_brk_pend = FALSE;
					Setlreg(BC, hreg(IY));
					break;
				case 0x4d:			/* LD C,IYL */
					sim_brk_pend = FALSE;
					Setlreg(BC, lreg(IY));
					break;
				case 0x4e:			/* LD C,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(BC, GetBYTE(adr));
					break;
				case 0x54:			/* LD D,IYH */
					sim_brk_pend = FALSE;
					Sethreg(DE, hreg(IY));
					break;
				case 0x55:			/* LD D,IYL */
					sim_brk_pend = FALSE;
					Sethreg(DE, lreg(IY));
					break;
				case 0x56:			/* LD D,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(DE, GetBYTE(adr));
					break;
				case 0x5c:			/* LD E,H */
					sim_brk_pend = FALSE;
					Setlreg(DE, hreg(IY));
					break;
				case 0x5d:			/* LD E,L */
					sim_brk_pend = FALSE;
					Setlreg(DE, lreg(IY));
					break;
				case 0x5e:			/* LD E,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(DE, GetBYTE(adr));
					break;
				case 0x60:			/* LD IYH,B */
					sim_brk_pend = FALSE;
					Sethreg(IY, hreg(BC));
					break;
				case 0x61:			/* LD IYH,C */
					sim_brk_pend = FALSE;
					Sethreg(IY, lreg(BC));
					break;
				case 0x62:			/* LD IYH,D */
					sim_brk_pend = FALSE;
					Sethreg(IY, hreg(DE));
					break;
				case 0x63:			/* LD IYH,E */
					sim_brk_pend = FALSE;
					Sethreg(IY, lreg(DE));
					break;
				case 0x64:			/* LD IYH,IYH */
					sim_brk_pend = FALSE;
					/* nop */
					break;
				case 0x65:			/* LD IYH,IYL */
					sim_brk_pend = FALSE;
					Sethreg(IY, lreg(IY));
					break;
				case 0x66:			/* LD H,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(HL, GetBYTE(adr));
					break;
				case 0x67:			/* LD IYH,A */
					sim_brk_pend = FALSE;
					Sethreg(IY, hreg(AF));
					break;
				case 0x68:			/* LD IYL,B */
					sim_brk_pend = FALSE;
					Setlreg(IY, hreg(BC));
					break;
				case 0x69:			/* LD IYL,C */
					sim_brk_pend = FALSE;
					Setlreg(IY, lreg(BC));
					break;
				case 0x6a:			/* LD IYL,D */
					sim_brk_pend = FALSE;
					Setlreg(IY, hreg(DE));
					break;
				case 0x6b:			/* LD IYL,E */
					sim_brk_pend = FALSE;
					Setlreg(IY, lreg(DE));
					break;
				case 0x6c:			/* LD IYL,IYH */
					sim_brk_pend = FALSE;
					Setlreg(IY, hreg(IY));
					break;
				case 0x6d:			/* LD IYL,IYL */
					sim_brk_pend = FALSE;
					/* nop */
					break;
				case 0x6e:			/* LD L,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Setlreg(HL, GetBYTE(adr));
					break;
				case 0x6f:			/* LD IYL,A */
					sim_brk_pend = FALSE;
					Setlreg(IY, hreg(AF));
					break;
				case 0x70:			/* LD (IY+dd),B */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(BC));
					break;
				case 0x71:			/* LD (IY+dd),C */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(BC));
					break;
				case 0x72:			/* LD (IY+dd),D */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(DE));
					break;
				case 0x73:			/* LD (IY+dd),E */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(DE));
					break;
				case 0x74:			/* LD (IY+dd),H */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(HL));
					break;
				case 0x75:			/* LD (IY+dd),L */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, lreg(HL));
					break;
				case 0x77:			/* LD (IY+dd),A */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					PutBYTE(adr, hreg(AF));
					break;
				case 0x7c:			/* LD A,IYH */
					sim_brk_pend = FALSE;
					Sethreg(AF, hreg(IY));
					break;
				case 0x7d:			/* LD A,IYL */
					sim_brk_pend = FALSE;
					Sethreg(AF, lreg(IY));
					break;
				case 0x7e:			/* LD A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					Sethreg(AF, GetBYTE(adr));
					break;
				case 0x84:			/* ADD A,IYH */
					sim_brk_pend = FALSE;
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x85:			/* ADD A,IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x86:			/* ADD A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8c:			/* ADC A,IYH */
					sim_brk_pend = FALSE;
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8d:			/* ADC A,IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8e:			/* ADC A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x94:			/* SUB IYH */
					sim_brk_pend = FALSE;
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x95:			/* SUB IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x96:			/* SUB (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9c:			/* SBC A,IYH */
					sim_brk_pend = FALSE;
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9d:			/* SBC A,IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9e:			/* SBC A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0xa4:			/* AND IYH */
					sim_brk_pend = FALSE;
					sum = ((AF & (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) |
						((sum == 0) << 6) | 0x10 | partab[sum];
					break;
				case 0xa5:			/* AND IYL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) & IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xa6:			/* AND (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) & GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xac:			/* XOR IYH */
					sim_brk_pend = FALSE;
					sum = ((AF ^ (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xad:			/* XOR IYL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) ^ IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xae:			/* XOR (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) ^ GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb4:			/* OR IYH */
					sim_brk_pend = FALSE;
					sum = ((AF | (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb5:			/* OR IYL */
					sim_brk_pend = FALSE;
					sum = ((AF >> 8) | IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xb6:			/* OR (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					sum = ((AF >> 8) | GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xbc:			/* CP IYH */
					sim_brk_pend = FALSE;
					temp = hreg(IY);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xbd:			/* CP IYL */
					sim_brk_pend = FALSE;
					temp = lreg(IY);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xbe:			/* CP (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					CheckBreakByte(adr);
					temp = GetBYTE(adr);
					AF = (AF & ~0x28) | (temp & 0x28);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xff) | (sum & 0x80) |
						(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0xcb:			/* CB prefix */
					adr = IY + (signed char) RAM_pp(PC);
					switch ((op = GetBYTE(PC)) & 7) {
					case 0: sim_brk_pend = FALSE; ++PC; acu = hreg(BC); break;
					case 1: sim_brk_pend = FALSE; ++PC; acu = lreg(BC); break;
					case 2: sim_brk_pend = FALSE; ++PC; acu = hreg(DE); break;
					case 3: sim_brk_pend = FALSE; ++PC; acu = lreg(DE); break;
					case 4: sim_brk_pend = FALSE; ++PC; acu = hreg(HL); break;
					case 5: sim_brk_pend = FALSE; ++PC; acu = lreg(HL); break;
					case 6: CheckBreakByte(adr); ++PC; acu = GetBYTE(adr); break;
					case 7: sim_brk_pend = FALSE; ++PC; acu = hreg(AF); break;
					}
					switch (op & 0xc0) {
					case 0x00:		/* shift/rotate */
						switch (op & 0x38) {
						case 0x00:	/* RLC */
							temp = (acu << 1) | (acu >> 7);
							cbits = temp & 1;
							goto cbshflg3;
						case 0x08:	/* RRC */
							temp = (acu >> 1) | (acu << 7);
							cbits = temp & 0x80;
							goto cbshflg3;
						case 0x10:	/* RL */
							temp = (acu << 1) | TSTFLAG(C);
							cbits = acu & 0x80;
							goto cbshflg3;
						case 0x18:	/* RR */
							temp = (acu >> 1) | (TSTFLAG(C) << 7);
							cbits = acu & 1;
							goto cbshflg3;
						case 0x20:	/* SLA */
							temp = acu << 1;
							cbits = acu & 0x80;
							goto cbshflg3;
						case 0x28:	/* SRA */
							temp = (acu >> 1) | (acu & 0x80);
							cbits = acu & 1;
							goto cbshflg3;
						case 0x30:	/* SLIA */
							temp = (acu << 1) | 1;
							cbits = acu & 0x80;
							goto cbshflg3;
						case 0x38:	/* SRL */
							temp = acu >> 1;
							cbits = acu & 1;
						cbshflg3:
							AF = (AF & ~0xff) | (temp & 0xa8) |
								(((temp & 0xff) == 0) << 6) |
								parity(temp) | !!cbits;
						}
						break;
					case 0x40:		/* BIT */
						if (acu & (1 << ((op >> 3) & 7))) {
							AF = (AF & ~0xfe) | 0x10 |
							(((op & 0x38) == 0x38) << 7);
						}
						else {
							AF = (AF & ~0xfe) | 0x54;
						}
						if ((op&7) != 6) {
							AF |= (acu & 0x28);
						}
						temp = acu;
						break;
					case 0x80:		/* RES */
						temp = acu & ~(1 << ((op >> 3) & 7));
						break;
					case 0xc0:		/* SET */
						temp = acu | (1 << ((op >> 3) & 7));
						break;
					}
					switch (op & 7) {
					case 0: Sethreg(BC, temp); break;
					case 1: Setlreg(BC, temp); break;
					case 2: Sethreg(DE, temp); break;
					case 3: Setlreg(DE, temp); break;
					case 4: Sethreg(HL, temp); break;
					case 5: Setlreg(HL, temp); break;
					case 6: PutBYTE(adr, temp); break;
					case 7: Sethreg(AF, temp); break;
					}
					break;
				case 0xe1:			/* POP IY */
					CheckBreakWord(SP);
					POP(IY);
					break;
				case 0xe3:			/* EX (SP),IY */
					CheckBreakWord(SP);
					temp = IY; POP(IY); PUSH(temp);
					break;
				case 0xe5:			/* PUSH IY */
					CheckBreakWord(SP - 2);
					PUSH(IY);
					break;
				case 0xe9:			/* JP (IY) */
					sim_brk_pend = FALSE;
					PCQ_ENTRY(PC - 2);
					PC = IY;
					break;
				case 0xf9:			/* LD SP,IY */
					sim_brk_pend = FALSE;
					SP = IY;
					break;
				default:				/* ignore FD */
					sim_brk_pend = FALSE;
					checkCPUZ80;
					PC--;
				}
				break;
			case 0xfe:			/* CP nn */
				sim_brk_pend = FALSE;
				temp = RAM_pp(PC);
				AF = (AF & ~0x28) | (temp & 0x28);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = (AF & ~0xff) | (sum & 0x80) |
					(((sum & 0xff) == 0) << 6) | (temp & 0x28) |
					(SetPV) | 2 |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0xff:			/* RST 38H */
				CheckBreakWord(SP - 2);
				PUSH(PC); PCQ_ENTRY(PC - 1); PC = 0x38;
		}
	}
	end_decode:
	pc = PC;
	af[af_sel] = AF;
	regs[regs_sel].bc = BC;
	regs[regs_sel].de = DE;
	regs[regs_sel].hl = HL;
	ix = IX;
	iy = IY;
	sp = SP;

	/* Simulation halted */
	saved_PC = ((reason == STOP_OPCODE) || (reason == STOP_MEM)) ? PCX : pc;
	pcq_r -> qptr = pcq_p;	/* update pc q ptr */
	AF_S = af[af_sel];
	BC_S = regs[regs_sel].bc;
	DE_S = regs[regs_sel].de;
	HL_S = regs[regs_sel].hl;
	IX_S = ix;
	IY_S = iy;
	SP_S = sp;
	AF1_S = af[1 - af_sel];
	BC1_S = regs[1 - regs_sel].bc;
	DE1_S = regs[1 - regs_sel].de;
	HL1_S = regs[1 - regs_sel].hl;
	IFF_S = IFF;
	INT_S = ir;
	return reason;
}

void install_bootrom(void) {
	int32 i;
	for (i = 0; i < bootrom_size; i++) {
		M[i + bootrom_origin][0] = bootrom[i] & 0xff;
	}
}

void clear_memory(int32 starting) {
	int32 i, j;
	if (cpu_unit.flags & UNIT_BANKED) {
		for (i = 0; i < MAXMEMSIZE; i++) {
			for (j = 0; j < MAXBANKS; j++) {
				M[i][j] = 0;
			}
		}
	}
	else {
		for (i = starting; i < MAXMEMSIZE; i++) {
			M[i][0] = 0;
		}
	}
	install_bootrom();
}

/* Reset routine */

t_stat cpu_reset(DEVICE *dptr) {
	int32 i;
	AF_S = AF1_S = 0;
	af_sel = 0;
	BC_S = DE_S = HL_S = 0;
	regs_sel = 0;
	BC1_S = DE1_S = HL1_S = 0;
	INT_S = IX_S = IY_S = SP_S = 0;
	IFF_S = 3;
	bankSelect = 0;
	saved_PC = 0;
	clear_memory(0);
	sim_brk_types = (SWMASK('E') | SWMASK('M'));
	sim_brk_dflt = SWMASK('E');
	for (i = 0; i < PCQ_SIZE; i++) {
		pcq[i] = 0;
	}
	pcq_p = 0;
	pcq_r = find_reg("PCQ", NULL, dptr);
	if (pcq_r) {
		pcq_r -> qptr = 0;
	}
	else {
		return SCPE_IERR;
	}
	return SCPE_OK;
}

/* Memory examine */
t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw) {
	if (cpu_unit.flags & UNIT_BANKED) {
		if (addr >= MAXMEMSIZE) {
			return SCPE_NXM;
		}
	}
	else {
		if ((addr >= MEMSIZE) && (addr < bootrom_origin)) {
			return SCPE_NXM;
		}
	}
	if (vptr != NULL) {
		*vptr = GetBYTE(addr) & 0xff;
	}
	return SCPE_OK;
}

/* Memory deposit */
t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw) {
	if (cpu_unit.flags & UNIT_BANKED) {
		if ((addr >= bootrom_origin) && (cpu_unit.flags & UNIT_ROM)) {
			return SCPE_NXM;
		}
	}
	else {
		if ((addr >= MEMSIZE) || ((addr >= bootrom_origin) && (cpu_unit.flags & UNIT_ROM))) {
			return SCPE_NXM;
		}
	}
	PutBYTE(addr, val & 0xff);
	return SCPE_OK;
}

t_stat cpu_set_rom(UNIT *uptr, int32 value, char *cptr, void *desc) {
	install_bootrom();
	return SCPE_OK;
}

t_stat cpu_set_banked(UNIT *uptr, int32 value, char *cptr, void *desc) {
	return MEMSIZE < MAXMEMSIZE ? SCPE_ARG : SCPE_OK;
}

t_stat cpu_set_size(UNIT *uptr, int32 value, char *cptr, void *desc) {
	int32 i, limit, mc = 0;

	if ((cpu_unit.flags & UNIT_BANKED) ||
		(value <= 0) || (value > MAXMEMSIZE) || ((value & 0xfff) != 0)) {
			return SCPE_ARG;
	}
	limit = (bootrom_origin < MEMSIZE) ? bootrom_origin : MEMSIZE;
	for (i = value; i < limit; i++) {
		mc |= GetBYTE(i);
	}
	if (mc && (!get_yn("Really truncate memory [N]?", FALSE))) {
		return SCPE_OK;
	}
	MEMSIZE = value;
	clear_memory(value);
	return SCPE_OK;
}
