/*	altairz80_cpu.c: MITS Altair CPU (8080 and Z80)
		Written by Peter Schorn, 2001
		Based on work by Charles E Owen ((c) 1997 - Commercial use prohibited)
		Code for Z80 CPU from Frank D. Cringle ((c) 1995 under GNU license)
*/

#include <stdio.h>
#include "altairZ80_defs.h"

/*-------------------------------- definitions for memory space --------*/

uint8 M[MAXMEMSIZE];	/* RAM which is present */

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

/* checkCPU must be invoked whenever a Z80 only instruction is executed */
#define checkCPU if ((cpu_unit.flags & UNIT_CHIP) == 0) { Bad8080OpOccured = 1; break; }

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

#define JPC(cond) PC = cond ? GetWORD(PC) : PC+2

#define CALLC(cond) {													\
	if (cond) {																	\
		register uint32 adrr = GetWORD(PC);				\
		PUSH(PC+2);																\
		PC = adrr;																\
	}																						\
	else																				\
		PC += 2;																	\
}

int32 saved_PC = 0;													/* program counter			*/
int32 SR = 0;																/* switch register			*/
int32 PCX;																	/* External view of PC	*/

extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */

/* function prototypes */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_svc (UNIT *uptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
void clear_memory(int32 starting);

extern int32 sio0s(int32 io, int32 data);
extern int32 sio0d(int32 io, int32 data);
extern int32 sio1s(int32 io, int32 data);
extern int32 sio1d(int32 io, int32 data);
extern int32 dsk10(int32 io, int32 data);
extern int32 dsk11(int32 io, int32 data);
extern int32 dsk12(int32 io, int32 data);
extern int32 nulldev(int32 io, int32 data);
extern int32 simh_dev(int32 io, int32 data);
extern int32 markTimeSP;

/*	This is the I/O configuration table. There are 255 possible
		device addresses, if a device is plugged to a port it's routine
		address is here, 'nulldev' means no device is available
*/
struct idev {
	int32 (*routine)();
};
struct idev dev_table[256] = {
{&nulldev},	{&nulldev},	{&nulldev}, {&nulldev},			/* 00 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 04 */
{&dsk10},		{&dsk11},		{&dsk12},		{&nulldev},			/* 08 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 0C */
{&sio0s},		{&sio0d},		{&sio1s},		{&sio1d},				/* 10 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 14 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 18 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 1C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 20 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 24 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 28 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 2C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 30 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 34 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 38 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 3C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 40 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 44 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 48 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 4C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 50 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 54 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 58 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 5C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 60 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 64 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 68 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 6C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 70 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 74 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 78 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 7C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 80 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 84 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 88 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 8C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 90 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 94 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 98 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* 9C */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* A0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* A4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* A8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* AC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* B0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* B4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* B8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* BC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* C0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* C4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* C8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* CC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* D0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* D4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* D8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* DC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* D0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* E4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* E8 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* EC */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* F0 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* F4 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},			/* F8 */
{&nulldev}, {&nulldev}, {&simh_dev}, {&nulldev} };	/* FC */

/* Altair MITS modified BOOT EPROM, fits in upper 256 byte of memory */

int32 bootrom[bootrom_size] = {
	0x21, 0x00, 0x5c, 0x11, 0x13, 0xff, 0x0e, 0xb9, /* ff00-ff07 */
	0x1a, 0x77, 0x13, 0x23, 0x0d, 0xc2, 0x08, 0xff, /* ff08-ff0f */
	0xc3, 0x00, 0x5c, 0x31, 0xa6, 0x5d, 0xaf, 0xd3, /* ff10-ff17 */
	0x08, 0x3e, 0x04, 0xd3, 0x09, 0xc3, 0x18, 0x5c, /* ff18-ff1f */
	0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x0d, 0x5c, 0x3e, /* ff20-ff27 */
	0x02, 0xd3, 0x09, 0xdb, 0x08, 0xe6, 0x40, 0xc2, /* ff28-ff2f */
	0x0d, 0x5c, 0x11, 0x00, 0x00, 0x06, 0x08, 0xc3, /* ff30-ff37 */
	0x29, 0x5c, 0x06, 0x00, 0x3e, 0x10, 0xf5, 0xd5, /* ff38-ff3f */
	0xc5, 0xd5, 0x11, 0x86, 0x80, 0x21, 0xb9, 0x5c, /* ff40-ff47 */
	0xdb, 0x09, 0x1f, 0xda, 0x35, 0x5c, 0xe6, 0x1f, /* ff48-ff4f */
	0xb8, 0xc2, 0x35, 0x5c, 0xdb, 0x08, 0xb7, 0xfa, /* ff50-ff57 */
	0x41, 0x5c, 0xdb, 0x0a, 0x77, 0x23, 0x1d, 0xca, /* ff58-ff5f */
	0x57, 0x5c, 0x1d, 0xdb, 0x0a, 0x77, 0x23, 0xc2, /* ff60-ff67 */
	0x41, 0x5c, 0xe1, 0x11, 0xbc, 0x5c, 0x01, 0x80, /* ff68-ff6f */
	0x00, 0x1a, 0x77, 0xbe, 0x80, 0x47, 0x13, 0x23, /* ff70-ff77 */
	0x0d, 0xc2, 0x5e, 0x5c, 0x1a, 0xfe, 0xff, 0xc2, /* ff78-ff7f */
	0x72, 0x5c, 0x13, 0x1a, 0xb8, 0xc1, 0xeb, 0xc2, /* ff80-ff87 */
	0xac, 0x5c, 0xf1, 0xf1, 0x2a, 0xba, 0x5c, 0xd5, /* ff88-ff8f */
	0x11, 0x00, 0x5c, 0xcd, 0xb3, 0x5c, 0xd1, 0xcd, /* ff90-ff97 */
	0xb3, 0x5c, 0xd2, 0xa5, 0x5c, 0x04, 0x04, 0x78, /* ff98-ff9f */
	0xfe, 0x20, 0xda, 0x29, 0x5c, 0x06, 0x01, 0xca, /* ffa0-ffa7 */
	0x29, 0x5c, 0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x97, /* ffa8-ffaf */
	0x5c, 0x3e, 0x01, 0xd3, 0x09, 0xc3, 0x27, 0x5c, /* ffb0-ffb7 */
	0x3e, 0x80, 0xd3, 0x08, 0xc3, 0x00, 0x00, 0xd1, /* ffb8-ffbf */
	0xf1, 0x3d, 0xc2, 0x2b, 0x5c, 0x76, 0x7a, 0xbc, /* ffc0-ffc7 */
	0xc0, 0x7b, 0xbd, 0xc9, 0x00, 0x00, 0x00, 0x00, /* ffc8-ffcf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffd0-ffd7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffd8-ffdf */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffe0-ffe7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffe8-ffef */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fff0-fff7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	/* fff8-ffff */
};

/* CPU data structures
	 cpu_dev	CPU device descriptor
	 cpu_unit	CPU unit descriptor
	 cpu_reg	CPU register list
	 cpu_mod	CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

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
	{ FLDATA (IFF, IFF_S, 8) },
	{ FLDATA (INT, INT_S, 8) },
	{ FLDATA (Z80, cpu_unit.flags, UNIT_V_CHIP), REG_HRO },
	{ FLDATA (OPSTOP, cpu_unit.flags, UNIT_V_OPSTOP), REG_HRO },
	{ HRDATA (SR, SR, 8) },
	{ HRDATA (WRU, sim_int_char, 8) },
	{ DRDATA (MARK, markTimeSP, 3), REG_RO },
	{ NULL }	};

MTAB cpu_mod[] = {
	{ UNIT_CHIP,		UNIT_CHIP,		"Z80",			"Z80",			NULL					},
	{ UNIT_CHIP,		0,						"8080",			"8080",			NULL					},
	{ UNIT_OPSTOP,	UNIT_OPSTOP,	"ITRAP",		"ITRAP",		NULL					},
	{ UNIT_OPSTOP,	0,						"NOITRAP",	"NOITRAP",	NULL					},
	{ UNIT_MSIZE,		4*KB,					NULL,				"4K",				&cpu_set_size	},
	{ UNIT_MSIZE,		8*KB,					NULL,				"8K",				&cpu_set_size	},
	{ UNIT_MSIZE,		12*KB,				NULL,				"12K",			&cpu_set_size	},
	{ UNIT_MSIZE,		16*KB,				NULL,				"16K",			&cpu_set_size	},
	{ UNIT_MSIZE,		20*KB,				NULL,				"20K",			&cpu_set_size	},
	{ UNIT_MSIZE,		24*KB,				NULL,				"24K",			&cpu_set_size	},
	{ UNIT_MSIZE,		28*KB,				NULL,				"28K",			&cpu_set_size	},
	{ UNIT_MSIZE,		32*KB,				NULL,				"32K",			&cpu_set_size	},
	{ UNIT_MSIZE,		48*KB,				NULL,				"48K",			&cpu_set_size	},
	{ UNIT_MSIZE,		64*KB,				NULL,				"64K",			&cpu_set_size	},
	{ 0 }	};

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 1, 16, 8,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

void out(uint32 Port, uint8 Value) {
	dev_table[Port].routine(1, Value);
}

int in(uint32 Port) {
	return Port == 0xFF ? SR & 0xFF : dev_table[Port].routine(0, 0);
}

inline uint8 GetBYTE(register uint16 a) {
	return a < MEMSIZE ? M[a] : 0xff;
}

#define RAM_mm(a)	 GetBYTE(a--)
#define RAM_pp(a)	 GetBYTE(a++)

inline void PutBYTE(register uint16 Addr, register uint8 Value) {
	if ((Addr < MEMSIZE) && (Addr < bootrom_origin)) {
		M[Addr] = Value;
	}
/*
	else {
		printf("R/O M[%x]:=%x\n", Addr, Value);
	}
*/
}

#define PutBYTE_pp(a,v)	PutBYTE(a++, v)
#define PutBYTE_mm(a,v)	PutBYTE(a--, v)
#define mm_PutBYTE(a,v)	PutBYTE(--a, v)

inline uint16 GetWORD(register uint16 a)	{return (GetBYTE(a) | (GetBYTE((a)+1) << 8));}
inline void PutWORD(register uint16 a, register uint16 v) {
	PutBYTE(a, (uint8)(v));
	PutBYTE(a+1, v>>8);
}

#define PUSH(x) do {				\
	mm_PutBYTE(SP, (x) >> 8);	\
	mm_PutBYTE(SP, x);				\
} while (0)

int32 sim_instr (void) {
	extern int32 sim_interval;
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
	int32 BadZ80OpOccured = 0;
	int32 Bad8080OpOccured = 0;

	pc = saved_PC & ADDRMASK;					/* load local PC */
	af[af_sel] = AF_S;
	regs[regs_sel].bc = BC_S;
	regs[regs_sel].de = DE_S;
	regs[regs_sel].hl = HL_S;
	ix = IX_S;
	iy = IY_S;
	sp = SP_S;
	af[1-af_sel] = AF1_S;
	regs[1-regs_sel].bc = BC1_S;
	regs[1-regs_sel].de = DE1_S;
	regs[1-regs_sel].hl = HL1_S;
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
	while (reason == 0) {												/* loop until halted	*/
		if (sim_interval <= 0) {									/* check clock queue	*/
			if (reason = sim_process_event ()) {
				break;
			}
		}

		if (sim_brk_summ &&
		    sim_brk_test (PC, SWMASK ('E'))) {		/* breakpoint?				*/
			reason = STOP_IBKPT;										/* stop simulation		*/
			break;
		}

		PCX = PC;
		sim_interval--;
		switch(RAM_pp(PC)) {
			case 0x00:			/* NOP */
				break;
			case 0x01:			/* LD BC,nnnn */
				BC = GetWORD(PC);
				PC += 2;
				break;
			case 0x02:			/* LD (BC),A */
				PutBYTE(BC, hreg(AF));
				break;
			case 0x03:			/* INC BC */
				++BC;
				break;
			case 0x04:			/* INC B */
				BC += 0x100;
				temp = hreg(BC);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x05:			/* DEC B */
				BC -= 0x100;
				temp = hreg(BC);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x06:			/* LD B,nn */
				Sethreg(BC, RAM_pp(PC));
				break;
			case 0x07:			/* RLCA */
				AF = ((AF >> 7) & 0x0128) | ((AF << 1) & ~0x1ff) |
					(AF & 0xc4) | ((AF >> 15) & 1);
				break;
			case 0x08:			/* EX AF,AF' */
				checkCPU
				af[af_sel] = AF;
				af_sel = 1 - af_sel;
				AF = af[af_sel];
				break;
			case 0x09:			/* ADD HL,BC */
				HL &= 0xffff;
				BC &= 0xffff;
				sum = HL + BC;
				cbits = (HL ^ BC ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x0A:			/* LD A,(BC) */
				Sethreg(AF, GetBYTE(BC));
				break;
			case 0x0B:			/* DEC BC */
				--BC;
				break;
			case 0x0C:			/* INC C */
				temp = lreg(BC)+1;
				Setlreg(BC, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x0D:			/* DEC C */
				temp = lreg(BC)-1;
				Setlreg(BC, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x0E:			/* LD C,nn */
				Setlreg(BC, RAM_pp(PC));
				break;
			case 0x0F:			/* RRCA */
				temp = hreg(AF);
				sum = temp >> 1;
				AF = ((temp & 1) << 15) | (sum << 8) |
					(sum & 0x28) | (AF & 0xc4) | (temp & 1);
				break;
			case 0x10:			/* DJNZ dd */
				checkCPU
				PC += ((BC -= 0x100) & 0xff00) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x11:			/* LD DE,nnnn */
				DE = GetWORD(PC);
				PC += 2;
				break;
			case 0x12:			/* LD (DE),A */
				PutBYTE(DE, hreg(AF));
				break;
			case 0x13:			/* INC DE */
				++DE;
				break;
			case 0x14:			/* INC D */
				DE += 0x100;
				temp = hreg(DE);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x15:			/* DEC D */
				DE -= 0x100;
				temp = hreg(DE);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x16:			/* LD D,nn */
				Sethreg(DE, RAM_pp(PC));
				break;
			case 0x17:			/* RLA */
				AF = ((AF << 8) & 0x0100) | ((AF >> 7) & 0x28) | ((AF << 1) & ~0x01ff) |
					(AF & 0xc4) | ((AF >> 15) & 1);
				break;
			case 0x18:			/* JR dd */
				checkCPU
				PC += (1) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x19:			/* ADD HL,DE */
				HL &= 0xffff;
				DE &= 0xffff;
				sum = HL + DE;
				cbits = (HL ^ DE ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x1A:			/* LD A,(DE) */
				Sethreg(AF, GetBYTE(DE));
				break;
			case 0x1B:			/* DEC DE */
				--DE;
				break;
			case 0x1C:			/* INC E */
				temp = lreg(DE)+1;
				Setlreg(DE, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x1D:			/* DEC E */
				temp = lreg(DE)-1;
				Setlreg(DE, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x1E:			/* LD E,nn */
				Setlreg(DE, RAM_pp(PC));
				break;
			case 0x1F:			/* RRA */
				temp = hreg(AF);
				sum = temp >> 1;
				AF = ((AF & 1) << 15) | (sum << 8) |
					(sum & 0x28) | (AF & 0xc4) | (temp & 1);
				break;
			case 0x20:			/* JR NZ,dd */
				checkCPU
				PC += (!TSTFLAG(Z)) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x21:			/* LD HL,nnnn */
				HL = GetWORD(PC);
				PC += 2;
				break;
			case 0x22:			/* LD (nnnn),HL */
				temp = GetWORD(PC);
				PutWORD(temp, HL);
				PC += 2;
				break;
			case 0x23:			/* INC HL */
				++HL;
				break;
			case 0x24:			/* INC H */
				HL += 0x100;
				temp = hreg(HL);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x25:			/* DEC H */
				HL -= 0x100;
				temp = hreg(HL);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x26:			/* LD H,nn */
				Sethreg(HL, RAM_pp(PC));
				break;
			case 0x27:			/* DAA */
				acu = hreg(AF);
				temp = ldig(acu);
				cbits = TSTFLAG(C);
				if (TSTFLAG(N)) {	/* last operation was a subtract */
					int hd = cbits || acu > 0x99;
					if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
						if (temp > 5)
							SETFLAG(H, 0);
						acu -= 6;
						acu &= 0xff;
					}
					if (hd)		/* adjust high digit */
						acu -= 0x160;
				}
				else {			/* last operation was an add */
					if (TSTFLAG(H) || (temp > 9)) { /* adjust low digit */
						SETFLAG(H, (temp > 9));
						acu += 6;
					}
					if (cbits || ((acu & 0x1f0) > 0x90)) /* adjust high digit */
						acu += 0x60;
				}
				cbits |= (acu >> 8) & 1;
				acu &= 0xff;
				AF = (acu << 8) | (acu & 0xa8) | ((acu == 0) << 6) |
					(AF & 0x12) | partab[acu] | cbits;
				break;
			case 0x28:			/* JR Z,dd */
				checkCPU
				PC += (TSTFLAG(Z)) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x29:			/* ADD HL,HL */
				HL &= 0xffff;
				sum = HL + HL;
				cbits = (HL ^ HL ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x2A:			/* LD HL,(nnnn) */
				temp = GetWORD(PC);
				HL = GetWORD(temp);
				PC += 2;
				break;
			case 0x2B:			/* DEC HL */
				--HL;
				break;
			case 0x2C:			/* INC L */
				temp = lreg(HL)+1;
				Setlreg(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x2D:			/* DEC L */
				temp = lreg(HL)-1;
				Setlreg(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x2E:			/* LD L,nn */
				Setlreg(HL, RAM_pp(PC));
				break;
			case 0x2F:			/* CPL */
				AF = (~AF & ~0xff) | (AF & 0xc5) | ((~AF >> 8) & 0x28) | 0x12;
				break;
			case 0x30:			/* JR NC,dd */
				checkCPU
				PC += (!TSTFLAG(C)) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x31:			/* LD SP,nnnn */
				SP = GetWORD(PC);
				PC += 2;
				break;
			case 0x32:			/* LD (nnnn),A */
				temp = GetWORD(PC);
				PutBYTE(temp, hreg(AF));
				PC += 2;
				break;
			case 0x33:			/* INC SP */
				++SP;
				break;
			case 0x34:			/* INC (HL) */
				temp = GetBYTE(HL)+1;
				PutBYTE(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x35:			/* DEC (HL) */
				temp = GetBYTE(HL)-1;
				PutBYTE(HL, temp);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x36:			/* LD (HL),nn */
				PutBYTE(HL, RAM_pp(PC));
				break;
			case 0x37:			/* SCF */
				AF = (AF&~0x3b)|((AF>>8)&0x28)|1;
				break;
			case 0x38:			/* JR C,dd */
				checkCPU
				PC += (TSTFLAG(C)) ? (signed char) GetBYTE(PC) + 1 : 1;
				break;
			case 0x39:			/* ADD HL,SP */
				HL &= 0xffff;
				SP &= 0xffff;
				sum = HL + SP;
				cbits = (HL ^ SP ^ sum) >> 8;
				HL = sum;
				AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
					(cbits & 0x10) | ((cbits >> 8) & 1);
				break;
			case 0x3A:			/* LD A,(nnnn) */
				temp = GetWORD(PC);
				Sethreg(AF, GetBYTE(temp));
				PC += 2;
				break;
			case 0x3B:			/* DEC SP */
				--SP;
				break;
			case 0x3C:			/* INC A */
				AF += 0x100;
				temp = hreg(AF);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0) << 4) |
					SetPV2(0x80);
				break;
			case 0x3D:			/* DEC A */
				AF -= 0x100;
				temp = hreg(AF);
				AF = (AF & ~0xfe) | (temp & 0xa8) |
					(((temp & 0xff) == 0) << 6) |
					(((temp & 0xf) == 0xf) << 4) |
					SetPV2(0x7f) | 2;
				break;
			case 0x3E:			/* LD A,nn */
				Sethreg(AF, RAM_pp(PC));
				break;
			case 0x3F:			/* CCF */
				AF = (AF&~0x3b)|((AF>>8)&0x28)|((AF&1)<<4)|(~AF&1);
				break;
			case 0x40:			/* LD B,B */
				/* nop */
				break;
			case 0x41:			/* LD B,C */
				BC = (BC & 255) | ((BC & 255) << 8);
				break;
			case 0x42:			/* LD B,D */
				BC = (BC & 255) | (DE & ~255);
				break;
			case 0x43:			/* LD B,E */
				BC = (BC & 255) | ((DE & 255) << 8);
				break;
			case 0x44:			/* LD B,H */
				BC = (BC & 255) | (HL & ~255);
				break;
			case 0x45:			/* LD B,L */
				BC = (BC & 255) | ((HL & 255) << 8);
				break;
			case 0x46:			/* LD B,(HL) */
				Sethreg(BC, GetBYTE(HL));
				break;
			case 0x47:			/* LD B,A */
				BC = (BC & 255) | (AF & ~255);
				break;
			case 0x48:			/* LD C,B */
				BC = (BC & ~255) | ((BC >> 8) & 255);
				break;
			case 0x49:			/* LD C,C */
				/* nop */
				break;
			case 0x4A:			/* LD C,D */
				BC = (BC & ~255) | ((DE >> 8) & 255);
				break;
			case 0x4B:			/* LD C,E */
				BC = (BC & ~255) | (DE & 255);
				break;
			case 0x4C:			/* LD C,H */
				BC = (BC & ~255) | ((HL >> 8) & 255);
				break;
			case 0x4D:			/* LD C,L */
				BC = (BC & ~255) | (HL & 255);
				break;
			case 0x4E:			/* LD C,(HL) */
				Setlreg(BC, GetBYTE(HL));
				break;
			case 0x4F:			/* LD C,A */
				BC = (BC & ~255) | ((AF >> 8) & 255);
				break;
			case 0x50:			/* LD D,B */
				DE = (DE & 255) | (BC & ~255);
				break;
			case 0x51:			/* LD D,C */
				DE = (DE & 255) | ((BC & 255) << 8);
				break;
			case 0x52:			/* LD D,D */
				/* nop */
				break;
			case 0x53:			/* LD D,E */
				DE = (DE & 255) | ((DE & 255) << 8);
				break;
			case 0x54:			/* LD D,H */
				DE = (DE & 255) | (HL & ~255);
				break;
			case 0x55:			/* LD D,L */
				DE = (DE & 255) | ((HL & 255) << 8);
				break;
			case 0x56:			/* LD D,(HL) */
				Sethreg(DE, GetBYTE(HL));
				break;
			case 0x57:			/* LD D,A */
				DE = (DE & 255) | (AF & ~255);
				break;
			case 0x58:			/* LD E,B */
				DE = (DE & ~255) | ((BC >> 8) & 255);
				break;
			case 0x59:			/* LD E,C */
				DE = (DE & ~255) | (BC & 255);
				break;
			case 0x5A:			/* LD E,D */
				DE = (DE & ~255) | ((DE >> 8) & 255);
				break;
			case 0x5B:			/* LD E,E */
				/* nop */
				break;
			case 0x5C:			/* LD E,H */
				DE = (DE & ~255) | ((HL >> 8) & 255);
				break;
			case 0x5D:			/* LD E,L */
				DE = (DE & ~255) | (HL & 255);
				break;
			case 0x5E:			/* LD E,(HL) */
				Setlreg(DE, GetBYTE(HL));
				break;
			case 0x5F:			/* LD E,A */
				DE = (DE & ~255) | ((AF >> 8) & 255);
				break;
			case 0x60:			/* LD H,B */
				HL = (HL & 255) | (BC & ~255);
				break;
			case 0x61:			/* LD H,C */
				HL = (HL & 255) | ((BC & 255) << 8);
				break;
			case 0x62:			/* LD H,D */
				HL = (HL & 255) | (DE & ~255);
				break;
			case 0x63:			/* LD H,E */
				HL = (HL & 255) | ((DE & 255) << 8);
				break;
			case 0x64:			/* LD H,H */
				/* nop */
				break;
			case 0x65:			/* LD H,L */
				HL = (HL & 255) | ((HL & 255) << 8);
				break;
			case 0x66:			/* LD H,(HL) */
				Sethreg(HL, GetBYTE(HL));
				break;
			case 0x67:			/* LD H,A */
				HL = (HL & 255) | (AF & ~255);
				break;
			case 0x68:			/* LD L,B */
				HL = (HL & ~255) | ((BC >> 8) & 255);
				break;
			case 0x69:			/* LD L,C */
				HL = (HL & ~255) | (BC & 255);
				break;
			case 0x6A:			/* LD L,D */
				HL = (HL & ~255) | ((DE >> 8) & 255);
				break;
			case 0x6B:			/* LD L,E */
				HL = (HL & ~255) | (DE & 255);
				break;
			case 0x6C:			/* LD L,H */
				HL = (HL & ~255) | ((HL >> 8) & 255);
				break;
			case 0x6D:			/* LD L,L */
				/* nop */
				break;
			case 0x6E:			/* LD L,(HL) */
				Setlreg(HL, GetBYTE(HL));
				break;
			case 0x6F:			/* LD L,A */
				HL = (HL & ~255) | ((AF >> 8) & 255);
				break;
			case 0x70:			/* LD (HL),B */
				PutBYTE(HL, hreg(BC));
				break;
			case 0x71:			/* LD (HL),C */
				PutBYTE(HL, lreg(BC));
				break;
			case 0x72:			/* LD (HL),D */
				PutBYTE(HL, hreg(DE));
				break;
			case 0x73:			/* LD (HL),E */
				PutBYTE(HL, lreg(DE));
				break;
			case 0x74:			/* LD (HL),H */
				PutBYTE(HL, hreg(HL));
				break;
			case 0x75:			/* LD (HL),L */
				PutBYTE(HL, lreg(HL));
				break;
			case 0x76:			/* HALT */
				reason = STOP_HALT;
				PC--;
				continue;
			case 0x77:			/* LD (HL),A */
				PutBYTE(HL, hreg(AF));
				break;
			case 0x78:			/* LD A,B */
				AF = (AF & 255) | (BC & ~255);
				break;
			case 0x79:			/* LD A,C */
				AF = (AF & 255) | ((BC & 255) << 8);
				break;
			case 0x7A:			/* LD A,D */
				AF = (AF & 255) | (DE & ~255);
				break;
			case 0x7B:			/* LD A,E */
				AF = (AF & 255) | ((DE & 255) << 8);
				break;
			case 0x7C:			/* LD A,H */
				AF = (AF & 255) | (HL & ~255);
				break;
			case 0x7D:			/* LD A,L */
				AF = (AF & 255) | ((HL & 255) << 8);
				break;
			case 0x7E:			/* LD A,(HL) */
				Sethreg(AF, GetBYTE(HL));
				break;
			case 0x7F:			/* LD A,A */
				/* nop */
				break;
			case 0x80:			/* ADD A,B */
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
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8A:			/* ADC A,D */
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8B:			/* ADC A,E */
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8C:			/* ADC A,H */
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8D:			/* ADC A,L */
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8E:			/* ADC A,(HL) */
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0x8F:			/* ADC A,A */
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
				temp = lreg(BC);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9A:			/* SBC A,D */
				temp = hreg(DE);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9B:			/* SBC A,E */
				temp = lreg(DE);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9C:			/* SBC A,H */
				temp = hreg(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9D:			/* SBC A,L */
				temp = lreg(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9E:			/* SBC A,(HL) */
				temp = GetBYTE(HL);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0x9F:			/* SBC A,A */
				temp = hreg(AF);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xA0:			/* AND B */
				sum = ((AF & (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xA1:			/* AND C */
				sum = ((AF >> 8) & BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xA2:			/* AND D */
				sum = ((AF & (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xA3:			/* AND E */
				sum = ((AF >> 8) & DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xA4:			/* AND H */
				sum = ((AF & (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xA5:			/* AND L */
				sum = ((AF >> 8) & HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xA6:			/* AND (HL) */
				sum = ((AF >> 8) & GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xA7:			/* AND A */
				sum = ((AF & (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) |
					((sum == 0) << 6) | 0x10 | partab[sum];
				break;
			case 0xA8:			/* XOR B */
				sum = ((AF ^ (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xA9:			/* XOR C */
				sum = ((AF >> 8) ^ BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAA:			/* XOR D */
				sum = ((AF ^ (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAB:			/* XOR E */
				sum = ((AF >> 8) ^ DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAC:			/* XOR H */
				sum = ((AF ^ (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAD:			/* XOR L */
				sum = ((AF >> 8) ^ HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAE:			/* XOR (HL) */
				sum = ((AF >> 8) ^ GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xAF:			/* XOR A */
				sum = ((AF ^ (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB0:			/* OR B */
				sum = ((AF | (BC)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB1:			/* OR C */
				sum = ((AF >> 8) | BC) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB2:			/* OR D */
				sum = ((AF | (DE)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB3:			/* OR E */
				sum = ((AF >> 8) | DE) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB4:			/* OR H */
				sum = ((AF | (HL)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB5:			/* OR L */
				sum = ((AF >> 8) | HL) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB6:			/* OR (HL) */
				sum = ((AF >> 8) | GetBYTE(HL)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB7:			/* OR A */
				sum = ((AF | (AF)) >> 8) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xB8:			/* CP B */
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
			case 0xB9:			/* CP C */
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
			case 0xBA:			/* CP D */
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
			case 0xBB:			/* CP E */
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
			case 0xBC:			/* CP H */
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
			case 0xBD:			/* CP L */
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
			case 0xBE:			/* CP (HL) */
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
			case 0xBF:			/* CP A */
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
			case 0xC0:			/* RET NZ */
				if (!TSTFLAG(Z)) POP(PC);
				break;
			case 0xC1:			/* POP BC */
				POP(BC);
				break;
			case 0xC2:			/* JP NZ,nnnn */
				JPC(!TSTFLAG(Z));
				break;
			case 0xC3:			/* JP nnnn */
				JPC(1);
				break;
			case 0xC4:			/* CALL NZ,nnnn */
				CALLC(!TSTFLAG(Z));
				break;
			case 0xC5:			/* PUSH BC */
				PUSH(BC);
				break;
			case 0xC6:			/* ADD A,nn */
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu + temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0xC7:			/* RST 0 */
				PUSH(PC); PC = 0;
				break;
			case 0xC8:			/* RET Z */
				if (TSTFLAG(Z)) POP(PC);
				break;
			case 0xC9:			/* RET */
				POP(PC);
				break;
			case 0xCA:			/* JP Z,nnnn */
				JPC(TSTFLAG(Z));
				break;
			case 0xCB:			/* CB prefix */
				checkCPU
				adr = HL;
				switch ((op = GetBYTE(PC)) & 7) {
				case 0: ++PC; acu = hreg(BC); break;
				case 1: ++PC; acu = lreg(BC); break;
				case 2: ++PC; acu = hreg(DE); break;
				case 3: ++PC; acu = lreg(DE); break;
				case 4: ++PC; acu = hreg(HL); break;
				case 5: ++PC; acu = lreg(HL); break;
				case 6: ++PC; acu = GetBYTE(adr); break;
				case 7: ++PC; acu = hreg(AF); break;
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
					if (acu & (1 << ((op >> 3) & 7)))
						AF = (AF & ~0xfe) | 0x10 |
						(((op & 0x38) == 0x38) << 7);
					else
						AF = (AF & ~0xfe) | 0x54;
					if ((op&7) != 6)
						AF |= (acu & 0x28);
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
			case 0xCC:			/* CALL Z,nnnn */
				CALLC(TSTFLAG(Z));
				break;
			case 0xCD:			/* CALL nnnn */
				CALLC(1);
				break;
			case 0xCE:			/* ADC A,nn */
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu + temp + TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) |
					((cbits >> 8) & 1);
				break;
			case 0xCF:			/* RST 8 */
				PUSH(PC); PC = 8;
				break;
			case 0xD0:			/* RET NC */
				if (!TSTFLAG(C)) POP(PC);
				break;
			case 0xD1:			/* POP DE */
				POP(DE);
				break;
			case 0xD2:			/* JP NC,nnnn */
				JPC(!TSTFLAG(C));
				break;
			case 0xD3:			/* OUT (nn),A */
				out(RAM_pp(PC), hreg(AF));
				break;
			case 0xD4:			/* CALL NC,nnnn */
				CALLC(!TSTFLAG(C));
				break;
			case 0xD5:			/* PUSH DE */
				PUSH(DE);
				break;
			case 0xD6:			/* SUB nn */
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu - temp;
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xD7:			/* RST 10H */
				PUSH(PC); PC = 0x10;
				break;
			case 0xD8:			/* RET C */
				if (TSTFLAG(C)) POP(PC);
				break;
			case 0xD9:			/* EXX */
				checkCPU
				regs[regs_sel].bc = BC;
				regs[regs_sel].de = DE;
				regs[regs_sel].hl = HL;
				regs_sel = 1 - regs_sel;
				BC = regs[regs_sel].bc;
				DE = regs[regs_sel].de;
				HL = regs[regs_sel].hl;
				break;
			case 0xDA:			/* JP C,nnnn */
				JPC(TSTFLAG(C));
				break;
			case 0xDB:			/* IN A,(nn) */
				Sethreg(AF, in(RAM_pp(PC)));
				break;
			case 0xDC:			/* CALL C,nnnn */
				CALLC(TSTFLAG(C));
				break;
			case 0xDD:			/* DD prefix */
				checkCPU
				switch (op = RAM_pp(PC)) {
				case 0x09:			/* ADD IX,BC */
					IX &= 0xffff;
					BC &= 0xffff;
					sum = IX + BC;
					cbits = (IX ^ BC ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x19:			/* ADD IX,DE */
					IX &= 0xffff;
					DE &= 0xffff;
					sum = IX + DE;
					cbits = (IX ^ DE ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x21:			/* LD IX,nnnn */
					IX = GetWORD(PC);
					PC += 2;
					break;
				case 0x22:			/* LD (nnnn),IX */
					temp = GetWORD(PC);
					PutWORD(temp, IX);
					PC += 2;
					break;
				case 0x23:			/* INC IX */
					++IX;
					break;
				case 0x24:			/* INC IXH */
					IX += 0x100;
					temp = hreg(IX);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x25:			/* DEC IXH */
					IX -= 0x100;
					temp = hreg(IX);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x26:			/* LD IXH,nn */
					Sethreg(IX, RAM_pp(PC));
					break;
				case 0x29:			/* ADD IX,IX */
					IX &= 0xffff;
					sum = IX + IX;
					cbits = (IX ^ IX ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x2A:			/* LD IX,(nnnn) */
					temp = GetWORD(PC);
					IX = GetWORD(temp);
					PC += 2;
					break;
				case 0x2B:			/* DEC IX */
					--IX;
					break;
				case 0x2C:			/* INC IXL */
					temp = lreg(IX)+1;
					Setlreg(IX, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x2D:			/* DEC IXL */
					temp = lreg(IX)-1;
					Setlreg(IX, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x2E:			/* LD IXL,nn */
					Setlreg(IX, RAM_pp(PC));
					break;
				case 0x34:			/* INC (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr)+1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x35:			/* DEC (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr)-1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x36:			/* LD (IX+dd),nn */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, RAM_pp(PC));
					break;
				case 0x39:			/* ADD IX,SP */
					IX &= 0xffff;
					SP &= 0xffff;
					sum = IX + SP;
					cbits = (IX ^ SP ^ sum) >> 8;
					IX = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x44:			/* LD B,IXH */
					Sethreg(BC, hreg(IX));
					break;
				case 0x45:			/* LD B,IXL */
					Sethreg(BC, lreg(IX));
					break;
				case 0x46:			/* LD B,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Sethreg(BC, GetBYTE(adr));
					break;
				case 0x4C:			/* LD C,IXH */
					Setlreg(BC, hreg(IX));
					break;
				case 0x4D:			/* LD C,IXL */
					Setlreg(BC, lreg(IX));
					break;
				case 0x4E:			/* LD C,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Setlreg(BC, GetBYTE(adr));
					break;
				case 0x54:			/* LD D,IXH */
					Sethreg(DE, hreg(IX));
					break;
				case 0x55:			/* LD D,IXL */
					Sethreg(DE, lreg(IX));
					break;
				case 0x56:			/* LD D,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Sethreg(DE, GetBYTE(adr));
					break;
				case 0x5C:			/* LD E,H */
					Setlreg(DE, hreg(IX));
					break;
				case 0x5D:			/* LD E,L */
					Setlreg(DE, lreg(IX));
					break;
				case 0x5E:			/* LD E,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Setlreg(DE, GetBYTE(adr));
					break;
				case 0x60:			/* LD IXH,B */
					Sethreg(IX, hreg(BC));
					break;
				case 0x61:			/* LD IXH,C */
					Sethreg(IX, lreg(BC));
					break;
				case 0x62:			/* LD IXH,D */
					Sethreg(IX, hreg(DE));
					break;
				case 0x63:			/* LD IXH,E */
					Sethreg(IX, lreg(DE));
					break;
				case 0x64:			/* LD IXH,IXH */
					/* nop */
					break;
				case 0x65:			/* LD IXH,IXL */
					Sethreg(IX, lreg(IX));
					break;
				case 0x66:			/* LD H,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Sethreg(HL, GetBYTE(adr));
					break;
				case 0x67:			/* LD IXH,A */
					Sethreg(IX, hreg(AF));
					break;
				case 0x68:			/* LD IXL,B */
					Setlreg(IX, hreg(BC));
					break;
				case 0x69:			/* LD IXL,C */
					Setlreg(IX, lreg(BC));
					break;
				case 0x6A:			/* LD IXL,D */
					Setlreg(IX, hreg(DE));
					break;
				case 0x6B:			/* LD IXL,E */
					Setlreg(IX, lreg(DE));
					break;
				case 0x6C:			/* LD IXL,IXH */
					Setlreg(IX, hreg(IX));
					break;
				case 0x6D:			/* LD IXL,IXL */
					/* nop */
					break;
				case 0x6E:			/* LD L,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Setlreg(HL, GetBYTE(adr));
					break;
				case 0x6F:			/* LD IXL,A */
					Setlreg(IX, hreg(AF));
					break;
				case 0x70:			/* LD (IX+dd),B */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(BC));
					break;
				case 0x71:			/* LD (IX+dd),C */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(BC));
					break;
				case 0x72:			/* LD (IX+dd),D */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(DE));
					break;
				case 0x73:			/* LD (IX+dd),E */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(DE));
					break;
				case 0x74:			/* LD (IX+dd),H */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(HL));
					break;
				case 0x75:			/* LD (IX+dd),L */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(HL));
					break;
				case 0x77:			/* LD (IX+dd),A */
					adr = IX + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(AF));
					break;
				case 0x7C:			/* LD A,IXH */
					Sethreg(AF, hreg(IX));
					break;
				case 0x7D:			/* LD A,IXL */
					Sethreg(AF, lreg(IX));
					break;
				case 0x7E:			/* LD A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					Sethreg(AF, GetBYTE(adr));
					break;
				case 0x84:			/* ADD A,IXH */
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
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8C:			/* ADC A,IXH */
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8D:			/* ADC A,IXL */
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8E:			/* ADC A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
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
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9C:			/* SBC A,IXH */
					temp = hreg(IX);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9D:			/* SBC A,IXL */
					temp = lreg(IX);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9E:			/* SBC A,(IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0xA4:			/* AND IXH */
					sum = ((AF & (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) |
						((sum == 0) << 6) | 0x10 | partab[sum];
					break;
				case 0xA5:			/* AND IXL */
					sum = ((AF >> 8) & IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xA6:			/* AND (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) & GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xAC:			/* XOR IXH */
					sum = ((AF ^ (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xAD:			/* XOR IXL */
					sum = ((AF >> 8) ^ IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xAE:			/* XOR (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) ^ GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB4:			/* OR IXH */
					sum = ((AF | (IX)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB5:			/* OR IXL */
					sum = ((AF >> 8) | IX) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB6:			/* OR (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) | GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xBC:			/* CP IXH */
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
				case 0xBD:			/* CP IXL */
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
				case 0xBE:			/* CP (IX+dd) */
					adr = IX + (signed char) RAM_pp(PC);
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
				case 0xCB:			/* CB prefix */
					adr = IX + (signed char) RAM_pp(PC);
					adr = adr;
					switch ((op = GetBYTE(PC)) & 7) {
					case 0: ++PC; acu = hreg(BC); break;
					case 1: ++PC; acu = lreg(BC); break;
					case 2: ++PC; acu = hreg(DE); break;
					case 3: ++PC; acu = lreg(DE); break;
					case 4: ++PC; acu = hreg(HL); break;
					case 5: ++PC; acu = lreg(HL); break;
					case 6: ++PC; acu = GetBYTE(adr); break;
					case 7: ++PC; acu = hreg(AF); break;
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
						if (acu & (1 << ((op >> 3) & 7)))
							AF = (AF & ~0xfe) | 0x10 |
							(((op & 0x38) == 0x38) << 7);
						else
							AF = (AF & ~0xfe) | 0x54;
						if ((op&7) != 6)
							AF |= (acu & 0x28);
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
				case 0xE1:			/* POP IX */
					POP(IX);
					break;
				case 0xE3:			/* EX (SP),IX */
					temp = IX; POP(IX); PUSH(temp);
					break;
				case 0xE5:			/* PUSH IX */
					PUSH(IX);
					break;
				case 0xE9:			/* JP (IX) */
					PC = IX;
					break;
				case 0xF9:			/* LD SP,IX */
					SP = IX;
					break;
				default:				/* ignore DD */
					BadZ80OpOccured = 1;
					PC--;
				}
				break;
			case 0xDE:				/* SBC A,nn */
				temp = RAM_pp(PC);
				acu = hreg(AF);
				sum = acu - temp - TSTFLAG(C);
				cbits = acu ^ temp ^ sum;
				AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
					(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
					(SetPV) | 2 |
					((cbits >> 8) & 1);
				break;
			case 0xDF:			/* RST 18H */
				PUSH(PC); PC = 0x18;
				break;
			case 0xE0:			/* RET PO */
				if (!TSTFLAG(P)) POP(PC);
				break;
			case 0xE1:			/* POP HL */
				POP(HL);
				break;
			case 0xE2:			/* JP PO,nnnn */
				JPC(!TSTFLAG(P));
				break;
			case 0xE3:			/* EX (SP),HL */
				temp = HL; POP(HL); PUSH(temp);
				break;
			case 0xE4:			/* CALL PO,nnnn */
				CALLC(!TSTFLAG(P));
				break;
			case 0xE5:			/* PUSH HL */
				PUSH(HL);
				break;
			case 0xE6:			/* AND nn */
				sum = ((AF >> 8) & RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | 0x10 |
					((sum == 0) << 6) | partab[sum];
				break;
			case 0xE7:			/* RST 20H */
				PUSH(PC); PC = 0x20;
				break;
			case 0xE8:			/* RET PE */
				if (TSTFLAG(P)) POP(PC);
				break;
			case 0xE9:			/* JP (HL) */
				PC = HL;
				break;
			case 0xEA:			/* JP PE,nnnn */
				JPC(TSTFLAG(P));
				break;
			case 0xEB:			/* EX DE,HL */
				temp = HL; HL = DE; DE = temp;
				break;
			case 0xEC:			/* CALL PE,nnnn */
				CALLC(TSTFLAG(P));
				break;
			case 0xED:			/* ED prefix */
				checkCPU
				switch (op = RAM_pp(PC)) {
				case 0x40:			/* IN B,(C) */
					temp = in(lreg(BC));
					Sethreg(BC, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x41:			/* OUT (C),B */
					out(lreg(BC), hreg(BC));
					break;
				case 0x42:			/* SBC HL,BC */
					HL &= 0xffff;
					BC &= 0xffff;
					sum = HL - BC - TSTFLAG(C);
					cbits = (HL ^ BC ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x43:			/* LD (nnnn),BC */
					temp = GetWORD(PC);
					PutWORD(temp, BC);
					PC += 2;
					break;
				case 0x44:			/* NEG */
					temp = hreg(AF);
					AF = (-(AF & 0xff00) & 0xff00);
					AF |= ((AF >> 8) & 0xa8) | (((AF & 0xff00) == 0) << 6) |
						(((temp & 0x0f) != 0) << 4) |
						((temp == 0x80) << 2) |
						2 | (temp != 0);
					break;
				case 0x45:			/* RETN */
					IFF |= IFF >> 1;
					POP(PC);
					break;
				case 0x46:			/* IM 0 */
					/* interrupt mode 0 */
					break;
				case 0x47:			/* LD I,A */
					ir = (ir & 255) | (AF & ~255);
					break;
				case 0x48:			/* IN C,(C) */
					temp = in(lreg(BC));
					Setlreg(BC, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x49:			/* OUT (C),C */
					out(lreg(BC), lreg(BC));
					break;
				case 0x4A:			/* ADC HL,BC */
					HL &= 0xffff;
					BC &= 0xffff;
					sum = HL + BC + TSTFLAG(C);
					cbits = (HL ^ BC ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x4B:			/* LD BC,(nnnn) */
					temp = GetWORD(PC);
					BC = GetWORD(temp);
					PC += 2;
					break;
				case 0x4D:			/* RETI */
					IFF |= IFF >> 1;
					POP(PC);
					break;
				case 0x4F:			/* LD R,A */
					ir = (ir & ~255) | ((AF >> 8) & 255);
					break;
				case 0x50:			/* IN D,(C) */
					temp = in(lreg(BC));
					Sethreg(DE, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x51:			/* OUT (C),D */
					out(lreg(BC), hreg(DE));
					break;
				case 0x52:			/* SBC HL,DE */
					HL &= 0xffff;
					DE &= 0xffff;
					sum = HL - DE - TSTFLAG(C);
					cbits = (HL ^ DE ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x53:			/* LD (nnnn),DE */
					temp = GetWORD(PC);
					PutWORD(temp, DE);
					PC += 2;
					break;
				case 0x56:			/* IM 1 */
					/* interrupt mode 1 */
					break;
				case 0x57:			/* LD A,I */
					AF = (AF & 0x29) | (ir & ~255) | ((ir >> 8) & 0x80) | (((ir & ~255) == 0) << 6) | ((IFF & 2) << 1);
					break;
				case 0x58:			/* IN E,(C) */
					temp = in(lreg(BC));
					Setlreg(DE, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x59:			/* OUT (C),E */
					out(lreg(BC), lreg(DE));
					break;
				case 0x5A:			/* ADC HL,DE */
					HL &= 0xffff;
					DE &= 0xffff;
					sum = HL + DE + TSTFLAG(C);
					cbits = (HL ^ DE ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x5B:			/* LD DE,(nnnn) */
					temp = GetWORD(PC);
					DE = GetWORD(temp);
					PC += 2;
					break;
				case 0x5E:			/* IM 2 */
					/* interrupt mode 2 */
					break;
				case 0x5F:			/* LD A,R */
					AF = (AF & 0x29) | ((ir & 255) << 8) | (ir & 0x80) | (((ir & 255) == 0) << 6) | ((IFF & 2) << 1);
					break;
				case 0x60:			/* IN H,(C) */
					temp = in(lreg(BC));
					Sethreg(HL, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x61:			/* OUT (C),H */
					out(lreg(BC), hreg(HL));
					break;
				case 0x62:			/* SBC HL,HL */
					HL &= 0xffff;
					sum = HL - HL - TSTFLAG(C);
					cbits = (HL ^ HL ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x63:			/* LD (nnnn),HL */
					temp = GetWORD(PC);
					PutWORD(temp, HL);
					PC += 2;
					break;
				case 0x67:			/* RRD */
					temp = GetBYTE(HL);
					acu = hreg(AF);
					PutBYTE(HL, hdig(temp) | (ldig(acu) << 4));
					acu = (acu & 0xf0) | ldig(temp);
					AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
						partab[acu] | (AF & 1);
					break;
				case 0x68:			/* IN L,(C) */
					temp = in(lreg(BC));
					Setlreg(HL, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x69:			/* OUT (C),L */
					out(lreg(BC), lreg(HL));
					break;
				case 0x6A:			/* ADC HL,HL */
					HL &= 0xffff;
					sum = HL + HL + TSTFLAG(C);
					cbits = (HL ^ HL ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x6B:			/* LD HL,(nnnn) */
					temp = GetWORD(PC);
					HL = GetWORD(temp);
					PC += 2;
					break;
				case 0x6F:			/* RLD */
					temp = GetBYTE(HL);
					acu = hreg(AF);
					PutBYTE(HL, (ldig(temp) << 4) | ldig(acu));
					acu = (acu & 0xf0) | hdig(temp);
					AF = (acu << 8) | (acu & 0xa8) | (((acu & 0xff) == 0) << 6) |
						partab[acu] | (AF & 1);
					break;
				case 0x70:			/* IN (C) */
					temp = in(lreg(BC));
					Setlreg(temp, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x71:			/* OUT (C),0 */
					out(lreg(BC), 0);
					break;
				case 0x72:			/* SBC HL,SP */
					HL &= 0xffff;
					SP &= 0xffff;
					sum = HL - SP - TSTFLAG(C);
					cbits = (HL ^ SP ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | 2 | ((cbits >> 8) & 1);
					break;
				case 0x73:			/* LD (nnnn),SP */
					temp = GetWORD(PC);
					PutWORD(temp, SP);
					PC += 2;
					break;
				case 0x78:			/* IN A,(C) */
					temp = in(lreg(BC));
					Sethreg(AF, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						parity(temp);
					break;
				case 0x79:			/* OUT (C),A */
					out(lreg(BC), hreg(AF));
					break;
				case 0x7A:			/* ADC HL,SP */
					HL &= 0xffff;
					SP &= 0xffff;
					sum = HL + SP + TSTFLAG(C);
					cbits = (HL ^ SP ^ sum) >> 8;
					HL = sum;
					AF = (AF & ~0xff) | ((sum >> 8) & 0xa8) |
						(((sum & 0xffff) == 0) << 6) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x7B:			/* LD SP,(nnnn) */
					temp = GetWORD(PC);
					SP = GetWORD(temp);
					PC += 2;
					break;
				case 0xA0:			/* LDI */
					acu = RAM_pp(HL);
					PutBYTE_pp(DE, acu);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
						(((--BC & 0xffff) != 0) << 2);
					break;
				case 0xA1:			/* CPI */
					acu = hreg(AF);
					temp = RAM_pp(HL);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
						((sum - ((cbits >> 4) & 1)) & 8) |
						((--BC & 0xffff) != 0) << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0)
						AF &= ~8;
					break;
				case 0xA2:			/* INI */
					PutBYTE(HL, in(lreg(BC))); ++HL;
					SETFLAG(N, 1);
					SETFLAG(P, (--BC & 0xffff) != 0);
					break;
				case 0xA3:			/* OUTI */
					out(lreg(BC), GetBYTE(HL)); ++HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xA8:			/* LDD */
					acu = RAM_mm(HL);
					PutBYTE_mm(DE, acu);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4) |
						(((--BC & 0xffff) != 0) << 2);
					break;
				case 0xA9:			/* CPD */
					acu = hreg(AF);
					temp = RAM_mm(HL);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) | (cbits & 16) |
						((sum - ((cbits >> 4) & 1)) & 8) |
						((--BC & 0xffff) != 0) << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0)
						AF &= ~8;
					break;
				case 0xAA:			/* IND */
					PutBYTE(HL, in(lreg(BC))); --HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xAB:			/* OUTD */
					out(lreg(BC), GetBYTE(HL)); --HL;
					SETFLAG(N, 1);
					Sethreg(BC, lreg(BC) - 1);
					SETFLAG(Z, lreg(BC) == 0);
					break;
				case 0xB0:			/* LDIR */
					acu = hreg(AF);
					BC &= 0xffff;
					do {
						acu = RAM_pp(HL);
						PutBYTE_pp(DE, acu);
					} while (--BC);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
					break;
				case 0xB1:			/* CPIR */
					acu = hreg(AF);
					BC &= 0xffff;
					do {
						temp = RAM_pp(HL);
						op = --BC != 0;
						sum = acu - temp;
					} while (op && sum != 0);
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) |
						(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
						op << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0)
						AF &= ~8;
					break;
				case 0xB2:			/* INIR */
					temp = hreg(BC);
					do {
						PutBYTE(HL, in(lreg(BC))); ++HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xB3:			/* OTIR */
					temp = hreg(BC);
					do {
						out(lreg(BC), GetBYTE(HL)); ++HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xB8:			/* LDDR */
					BC &= 0xffff;
					do {
						acu = RAM_mm(HL);
						PutBYTE_mm(DE, acu);
					} while (--BC);
					acu += hreg(AF);
					AF = (AF & ~0x3e) | (acu & 8) | ((acu & 2) << 4);
					break;
				case 0xB9:			/* CPDR */
					acu = hreg(AF);
					BC &= 0xffff;
					do {
						temp = RAM_mm(HL);
						op = --BC != 0;
						sum = acu - temp;
					} while (op && sum != 0);
					cbits = acu ^ temp ^ sum;
					AF = (AF & ~0xfe) | (sum & 0x80) | (!(sum & 0xff) << 6) |
						(((sum - ((cbits&16)>>4))&2) << 4) |
						(cbits & 16) | ((sum - ((cbits >> 4) & 1)) & 8) |
						op << 2 | 2;
					if ((sum & 15) == 8 && (cbits & 16) != 0)
						AF &= ~8;
					break;
				case 0xBA:			/* INDR */
					temp = hreg(BC);
					do {
						PutBYTE(HL, in(lreg(BC))); --HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				case 0xBB:			/* OTDR */
					temp = hreg(BC);
					do {
						out(lreg(BC), GetBYTE(HL)); --HL;
					} while (--temp);
					Sethreg(BC, 0);
					SETFLAG(N, 1);
					SETFLAG(Z, 1);
					break;
				default:	/* ignore ED and following byte */
					BadZ80OpOccured = 1;
				}
				break;
			case 0xEE:			/* XOR nn */
				sum = ((AF >> 8) ^ RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xEF:			/* RST 28H */
				PUSH(PC); PC = 0x28;
				break;
			case 0xF0:			/* RET P */
				if (!TSTFLAG(S)) POP(PC);
				break;
			case 0xF1:			/* POP AF */
				POP(AF);
				break;
			case 0xF2:			/* JP P,nnnn */
				JPC(!TSTFLAG(S));
				break;
			case 0xF3:			/* DI */
				IFF = 0;
				break;
			case 0xF4:			/* CALL P,nnnn */
				CALLC(!TSTFLAG(S));
				break;
			case 0xF5:			/* PUSH AF */
				PUSH(AF);
				break;
			case 0xF6:			/* OR nn */
				sum = ((AF >> 8) | RAM_pp(PC)) & 0xff;
				AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
				break;
			case 0xF7:			/* RST 30H */
				PUSH(PC); PC = 0x30;
				break;
			case 0xF8:			/* RET M */
				if (TSTFLAG(S)) POP(PC);
				break;
			case 0xF9:			/* LD SP,HL */
				SP = HL;
				break;
			case 0xFA:			/* JP M,nnnn */
				JPC(TSTFLAG(S));
				break;
			case 0xFB:			/* EI */
				IFF = 3;
				break;
			case 0xFC:			/* CALL M,nnnn */
				CALLC(TSTFLAG(S));
				break;
			case 0xFD:			/* FD prefix */
				checkCPU
				switch (op = RAM_pp(PC)) {
				case 0x09:			/* ADD IY,BC */
					IY &= 0xffff;
					BC &= 0xffff;
					sum = IY + BC;
					cbits = (IY ^ BC ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x19:			/* ADD IY,DE */
					IY &= 0xffff;
					DE &= 0xffff;
					sum = IY + DE;
					cbits = (IY ^ DE ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x21:			/* LD IY,nnnn */
					IY = GetWORD(PC);
					PC += 2;
					break;
				case 0x22:			/* LD (nnnn),IY */
					temp = GetWORD(PC);
					PutWORD(temp, IY);
					PC += 2;
					break;
				case 0x23:			/* INC IY */
					++IY;
					break;
				case 0x24:			/* INC IYH */
					IY += 0x100;
					temp = hreg(IY);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x25:			/* DEC IYH */
					IY -= 0x100;
					temp = hreg(IY);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x26:			/* LD IYH,nn */
					Sethreg(IY, RAM_pp(PC));
					break;
				case 0x29:			/* ADD IY,IY */
					IY &= 0xffff;
					sum = IY + IY;
					cbits = (IY ^ IY ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x2A:			/* LD IY,(nnnn) */
					temp = GetWORD(PC);
					IY = GetWORD(temp);
					PC += 2;
					break;
				case 0x2B:			/* DEC IY */
					--IY;
					break;
				case 0x2C:			/* INC IYL */
					temp = lreg(IY)+1;
					Setlreg(IY, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x2D:			/* DEC IYL */
					temp = lreg(IY)-1;
					Setlreg(IY, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x2E:			/* LD IYL,nn */
					Setlreg(IY, RAM_pp(PC));
					break;
				case 0x34:			/* INC (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr)+1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0) << 4) |
						((temp == 0x80) << 2);
					break;
				case 0x35:			/* DEC (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr)-1;
					PutBYTE(adr, temp);
					AF = (AF & ~0xfe) | (temp & 0xa8) |
						(((temp & 0xff) == 0) << 6) |
						(((temp & 0xf) == 0xf) << 4) |
						((temp == 0x7f) << 2) | 2;
					break;
				case 0x36:			/* LD (IY+dd),nn */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, RAM_pp(PC));
					break;
				case 0x39:			/* ADD IY,SP */
					IY &= 0xffff;
					SP &= 0xffff;
					sum = IY + SP;
					cbits = (IY ^ SP ^ sum) >> 8;
					IY = sum;
					AF = (AF & ~0x3b) | ((sum >> 8) & 0x28) |
						(cbits & 0x10) | ((cbits >> 8) & 1);
					break;
				case 0x44:			/* LD B,IYH */
					Sethreg(BC, hreg(IY));
					break;
				case 0x45:			/* LD B,IYL */
					Sethreg(BC, lreg(IY));
					break;
				case 0x46:			/* LD B,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Sethreg(BC, GetBYTE(adr));
					break;
				case 0x4C:			/* LD C,IYH */
					Setlreg(BC, hreg(IY));
					break;
				case 0x4D:			/* LD C,IYL */
					Setlreg(BC, lreg(IY));
					break;
				case 0x4E:			/* LD C,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Setlreg(BC, GetBYTE(adr));
					break;
				case 0x54:			/* LD D,IYH */
					Sethreg(DE, hreg(IY));
					break;
				case 0x55:			/* LD D,IYL */
					Sethreg(DE, lreg(IY));
					break;
				case 0x56:			/* LD D,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Sethreg(DE, GetBYTE(adr));
					break;
				case 0x5C:			/* LD E,H */
					Setlreg(DE, hreg(IY));
					break;
				case 0x5D:			/* LD E,L */
					Setlreg(DE, lreg(IY));
					break;
				case 0x5E:			/* LD E,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Setlreg(DE, GetBYTE(adr));
					break;
				case 0x60:			/* LD IYH,B */
					Sethreg(IY, hreg(BC));
					break;
				case 0x61:			/* LD IYH,C */
					Sethreg(IY, lreg(BC));
					break;
				case 0x62:			/* LD IYH,D */
					Sethreg(IY, hreg(DE));
					break;
				case 0x63:			/* LD IYH,E */
					Sethreg(IY, lreg(DE));
					break;
				case 0x64:			/* LD IYH,IYH */
					/* nop */
					break;
				case 0x65:			/* LD IYH,IYL */
					Sethreg(IY, lreg(IY));
					break;
				case 0x66:			/* LD H,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Sethreg(HL, GetBYTE(adr));
					break;
				case 0x67:			/* LD IYH,A */
					Sethreg(IY, hreg(AF));
					break;
				case 0x68:			/* LD IYL,B */
					Setlreg(IY, hreg(BC));
					break;
				case 0x69:			/* LD IYL,C */
					Setlreg(IY, lreg(BC));
					break;
				case 0x6A:			/* LD IYL,D */
					Setlreg(IY, hreg(DE));
					break;
				case 0x6B:			/* LD IYL,E */
					Setlreg(IY, lreg(DE));
					break;
				case 0x6C:			/* LD IYL,IYH */
					Setlreg(IY, hreg(IY));
					break;
				case 0x6D:			/* LD IYL,IYL */
					/* nop */
					break;
				case 0x6E:			/* LD L,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Setlreg(HL, GetBYTE(adr));
					break;
				case 0x6F:			/* LD IYL,A */
					Setlreg(IY, hreg(AF));
					break;
				case 0x70:			/* LD (IY+dd),B */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(BC));
					break;
				case 0x71:			/* LD (IY+dd),C */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(BC));
					break;
				case 0x72:			/* LD (IY+dd),D */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(DE));
					break;
				case 0x73:			/* LD (IY+dd),E */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(DE));
					break;
				case 0x74:			/* LD (IY+dd),H */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(HL));
					break;
				case 0x75:			/* LD (IY+dd),L */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, lreg(HL));
					break;
				case 0x77:			/* LD (IY+dd),A */
					adr = IY + (signed char) RAM_pp(PC);
					PutBYTE(adr, hreg(AF));
					break;
				case 0x7C:			/* LD A,IYH */
					Sethreg(AF, hreg(IY));
					break;
				case 0x7D:			/* LD A,IYL */
					Sethreg(AF, lreg(IY));
					break;
				case 0x7E:			/* LD A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					Sethreg(AF, GetBYTE(adr));
					break;
				case 0x84:			/* ADD A,IYH */
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
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu + temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8C:			/* ADC A,IYH */
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8D:			/* ADC A,IYL */
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu + temp + TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) |
						((cbits >> 8) & 1);
					break;
				case 0x8E:			/* ADC A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
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
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp;
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9C:			/* SBC A,IYH */
					temp = hreg(IY);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9D:			/* SBC A,IYL */
					temp = lreg(IY);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0x9E:			/* SBC A,(IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					temp = GetBYTE(adr);
					acu = hreg(AF);
					sum = acu - temp - TSTFLAG(C);
					cbits = acu ^ temp ^ sum;
					AF = ((sum & 0xff) << 8) | (sum & 0xa8) |
						(((sum & 0xff) == 0) << 6) | (cbits & 0x10) |
						(((cbits >> 6) ^ (cbits >> 5)) & 4) | 2 |
						((cbits >> 8) & 1);
					break;
				case 0xA4:			/* AND IYH */
					sum = ((AF & (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) |
						((sum == 0) << 6) | 0x10 | partab[sum];
					break;
				case 0xA5:			/* AND IYL */
					sum = ((AF >> 8) & IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xA6:			/* AND (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) & GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | 0x10 |
						((sum == 0) << 6) | partab[sum];
					break;
				case 0xAC:			/* XOR IYH */
					sum = ((AF ^ (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xAD:			/* XOR IYL */
					sum = ((AF >> 8) ^ IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xAE:			/* XOR (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) ^ GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB4:			/* OR IYH */
					sum = ((AF | (IY)) >> 8) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB5:			/* OR IYL */
					sum = ((AF >> 8) | IY) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xB6:			/* OR (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
					sum = ((AF >> 8) | GetBYTE(adr)) & 0xff;
					AF = (sum << 8) | (sum & 0xa8) | ((sum == 0) << 6) | partab[sum];
					break;
				case 0xBC:			/* CP IYH */
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
				case 0xBD:			/* CP IYL */
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
				case 0xBE:			/* CP (IY+dd) */
					adr = IY + (signed char) RAM_pp(PC);
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
				case 0xCB:			/* CB prefix */
					adr = IY + (signed char) RAM_pp(PC);
					adr = adr;
					switch ((op = GetBYTE(PC)) & 7) {
					case 0: ++PC; acu = hreg(BC); break;
					case 1: ++PC; acu = lreg(BC); break;
					case 2: ++PC; acu = hreg(DE); break;
					case 3: ++PC; acu = lreg(DE); break;
					case 4: ++PC; acu = hreg(HL); break;
					case 5: ++PC; acu = lreg(HL); break;
					case 6: ++PC; acu = GetBYTE(adr); break;
					case 7: ++PC; acu = hreg(AF); break;
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
						if (acu & (1 << ((op >> 3) & 7)))
							AF = (AF & ~0xfe) | 0x10 |
							(((op & 0x38) == 0x38) << 7);
						else
							AF = (AF & ~0xfe) | 0x54;
						if ((op&7) != 6)
							AF |= (acu & 0x28);
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
				case 0xE1:			/* POP IY */
					POP(IY);
					break;
				case 0xE3:			/* EX (SP),IY */
					temp = IY; POP(IY); PUSH(temp);
					break;
				case 0xE5:			/* PUSH IY */
					PUSH(IY);
					break;
				case 0xE9:			/* JP (IY) */
					PC = IY;
					break;
				case 0xF9:			/* LD SP,IY */
					SP = IY;
					break;
				default:				/* ignore FD */
					BadZ80OpOccured = 1;
					PC--;
				}
				break;
			case 0xFE:			/* CP nn */
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
			case 0xFF:			/* RST 38H */
				PUSH(PC); PC = 0x38;
		}
		if ((BadZ80OpOccured || Bad8080OpOccured) && (cpu_unit.flags & UNIT_OPSTOP)) {
			reason = STOP_OPCODE;
		}
	}
	pc = PC;
	af[af_sel] = AF;
	regs[regs_sel].bc = BC;
	regs[regs_sel].de = DE;
	regs[regs_sel].hl = HL;
	ix = IX;
	iy = IY;
	sp = SP;

	/* Simulation halted */
	saved_PC = (reason == STOP_OPCODE) ? PCX : pc;
	AF_S = af[af_sel];
	BC_S = regs[regs_sel].bc;
	DE_S = regs[regs_sel].de;
	HL_S = regs[regs_sel].hl;
	IX_S = ix;
	IY_S = iy;
	SP_S = sp;
	AF1_S = af[1-af_sel];
	BC1_S = regs[1-regs_sel].bc;
	DE1_S = regs[1-regs_sel].de;
	HL1_S = regs[1-regs_sel].hl;
	IFF_S = IFF;
	INT_S = ir;
	return reason;
}

void clear_memory(int32 starting) {
	int32 i;
	for (i = starting; i < MAXMEMSIZE; i++) {
		M[i] = 0;
	}
	for (i = 0; i < bootrom_size; i++) {
		M[i + bootrom_origin] = bootrom[i] & 0xFF;
	}
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)

{
	af[0] = af[1] = 0;
	af_sel = 0;
	regs[0].bc = regs[0].de = regs[0].hl = 0;
	regs_sel = 0;
	regs[1].bc = regs[1].de = regs[1].hl = 0;
	ir = ix = iy = sp = pc = IFF = 0;
	saved_PC = 0;
	clear_memory(0);
	markTimeSP = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if ((addr >= MEMSIZE) && (addr < bootrom_origin)) {
		return SCPE_NXM;
	}
	if (vptr != NULL) {
		*vptr = M[addr] & 0xff;
	}
	return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if ((addr >= MEMSIZE) || (addr >= bootrom_origin)) {
		return SCPE_NXM;
	}
	M[addr] = val & 0xff;
	return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 value, char *cptr, void *desc)
{
	int32 mc = 0;
	t_addr i;
	int32 limit;
	
	if ((value <= 0) || (value > MAXMEMSIZE) || ((value & 0xFFF) != 0)) {
		return SCPE_ARG;
	}
	limit = (bootrom_origin < MEMSIZE) ? bootrom_origin : MEMSIZE;
	for (i = value; i < limit; i++) {
		mc |= M[i];
	}
	if (mc && (!get_yn ("Really truncate memory [N]?", FALSE))) {
		return SCPE_OK;
	}
	MEMSIZE = value;
	clear_memory(value);
	return SCPE_OK;
}
