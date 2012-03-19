/* swtp_6800_cpu.c: SWTP 6800 Motorola 6800 CPU simulator

   Copyright (c) 2005, 2007, William Beech

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
   WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A. Beech shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A. Beech.
   
   Based on work by Charles E Owen (c) 1997 and Peter Schorn (c) 2002-2005

   cpu		6800 CPU

   The register state for the 6800 CPU is:

   A<0:7>		Accumulator A
   B<0:7>		Accumulator B
   IX<0:15>		Index Register
   H			half-carry flag
   I			interrupt flag
   N			negative flag
   Z			zero flag
   V			overflow flag
   C			carry flag
   PC<0:15>		program counter
   SP<0:15>		Stack Pointer

   The 6800 is an 8-bit CPU, which uses 16-bit registers to address
   up to 64KB of memory.

   The 72 basic instructions come in 1, 2, and 3-byte flavors.

   This routine is the instruction decode routine for the 6800.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	WAI instruction
	I/O error in I/O simulator
	Invalid OP code (if ITRAP is set on CPU)
	Invalid mamory address (if MTRAP is set on CPU)

   2. Interrupts.
      There are 4 types of interrupt, and in effect they do a 
      hardware CALL instruction to one of 4 possible high memory addresses.

   3. Non-existent memory.  On the SWTP 6800, reads to non-existent memory
      return 0FFH, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

	swtp_6800_cpu.c	add I/O service routines to dev_table
	swtp_sys.c	add pointer to data structures in sim_devices
*/

#include <stdio.h>

#include "swtp_defs.h"

//#include <windows.h>
//#include <mmsystem.h>

#define UNIT_V_OPSTOP	(UNIT_V_UF)			/* Stop on Invalid OP? */
#define UNIT_OPSTOP	(1 << UNIT_V_OPSTOP)
#define UNIT_V_MSTOP	(UNIT_V_UF+1)		/* Stop on Invalid memory? */
#define UNIT_MSTOP	(1 << UNIT_V_MSTOP)
#define UNIT_V_MSIZE	(UNIT_V_UF+2)		/* Memory Size */
#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#define UNIT_V_MA000	(UNIT_V_UF+2)		/* 128B or 8kB at 0xA000 */
#define UNIT_MA000	(1 << UNIT_V_MA000)

uint8 M[MAXMEMSIZE];				/* Memory */
int32 A = 0;						/* Accumulator A */
int32 B = 0;						/* Accumulator B */
int32 IX = 0;						/* Index register */
int32 SP = 0;						/* Stack pointer */
int32 H = 0;						/* Half-carry flag */
int32 I = 1;						/* Interrupt flag */
int32 N = 0;						/* Negative flag */
int32 Z = 0;						/* Zero flag */
int32 V = 0;						/* Overflow flag */
int32 C = 0;						/* Carry flag */
int32 saved_PC = 0;					/* Program counter */
int32 INTE = 0;						/* Interrupt Enable */
int32 int_req = 0;					/* Interrupt request */

int32 mem_fault = 0;				/* memory fault flag */

extern int32 sim_int_char;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ;/* breakpoint info */

/* function prototypes */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
void dump_regs();
void go_rel(int32 cond);
int32 get_rel_addr();
int32 get_dir_val();
int32 get_dir_addr();
int32 get_indir_val();
int32 get_indir_addr();
int32 get_ext_val();
int32 get_ext_addr();
int32 get_psw();
void set_psw(int32 psw);
void condevalH(int32 res);
void condevalN(int32 res);
void condevalZ(int32 res);
void condevalC(int32 res);
void condevalVa(int32 op1, int32 op2);
void condevalVs(int32 op1, int32 op2);
void mem_put_byte(int32 addr, int32 val);
void mem_put_word(int32 addr, int32 val);
int32 mem_get_byte(int32 addr);
int32 mem_get_word(int32 addr);
int32 nulldev(int32 io, int32 data);

/* external routines */

extern int32 sio0s(int32 io, int32 data);
extern int32 sio0d(int32 io, int32 data);
extern int32 sio1s(int32 io, int32 data);
extern int32 sio1d(int32 io, int32 data);
extern int32 fdcdrv(int32 io, int32 data);
extern int32 fdccmd(int32 io, int32 data);
extern int32 fdctrk(int32 io, int32 data);
extern int32 fdcsec(int32 io, int32 data);
extern int32 fdcdata(int32 io, int32 data);
extern int32 fprint_sym (FILE *of, int32 addr, uint32 *val,
	UNIT *uptr, int32 sw);


/* This is the I/O configuration table.  There are 32 possible
device addresses, if a device is plugged into a port it's routine
address is here, 'nulldev' means no device is available
*/

struct idev {
	int32 (*routine)(int32, int32);
};

struct idev dev_table[32] = {
	{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},	/*Port 0 8000-8003*/
	{&sio0s},   {&sio0d},   {&sio1s},   {&sio1d},	/*Port 1 8004-8007*/
/* sio1x routines just return the last value read on the matching
   sio0x routine.  SWTBUG tests for the MP-C with most port reads! */
	{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},	/*Port 2 8008-800B*/
	{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},	/*Port 3 800C-800F*/
	{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},	/*Port 4 8010-8013*/
	{&fdcdrv},  {&nulldev}, {&nulldev}, {&nulldev},	/*Port 5 8014-8017*/
	{&fdccmd},  {&fdctrk},  {&fdcsec},  {&fdcdata},	/*Port 6 8018-801B*/
	{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev}	/*Port 7 801C-801F*/
};

/*	SWTP 6800 SWTBUG BOOT EPROM, fits at 0E000-0E3FFH and replicated
	at 0FC000-0FFFF for the interrupt vectors */

#define BOOTLEN	1024

int32 bootrom[BOOTLEN] = {
0xFE,0xA0,0x00,0x6E,0x00,0x8D,0x40,0x6E,
0x00,0x10,0x16,0x04,0xBD,0xE3,0x34,0x8D,
0x67,0x81,0x53,0x26,0xFA,0x8D,0x61,0x81,
0x39,0x27,0x29,0x81,0x31,0x26,0xF0,0x7F,
0xA0,0x0F,0x8D,0x31,0x80,0x02,0xB7,0xA0,
0x47,0x8D,0x1C,0x8D,0x28,0x7A,0xA0,0x47,
0x27,0x09,0xA7,0x00,0xA1,0x00,0x26,0x08,
0x08,0x20,0xF0,0x7C,0xA0,0x0F,0x27,0xCF,   
0x86,0x3F,0x8D,0x31,0x7E,0xE2,0xD4,0x8D,
0x0C,0xB7,0xA0,0x0D,0x8D,0x07,0xB7,0xA0,
0x0E,0xFE,0xA0,0x0D,0x39,0x8D,0x53,0x48,
0x48,0x48,0x48,0x16,0x8D,0x4C,0x1B,0x16,
0xFB,0xA0,0x0F,0xF7,0xA0,0x0F,0x39,0x44,
0x44,0x44,0x44,0x84,0x0F,0x8B,0x30,0x81,
0x39,0x23,0x02,0x8B,0x07,0x7E,0xE1,0xD1,
0x7E,0xE1,0xAC,0x8D,0xF8,0x08,0xA6,0x00,
0x81,0x04,0x26,0xF7,0x39,0x7E,0xE1,0x4A,
0x8D,0xBD,0xCE,0xE1,0x9D,0x8D,0xEF,0xCE,
0xA0,0x0D,0x8D,0x34,0xFE,0xA0,0x0D,0x8D,
0x31,0x8D,0x31,0x8D,0xDB,0x81,0x20,0x27,
0xFA,0x81,0x0D,0x27,0xE0,0x81,0x5E,0x20,
0x2C,0x01,0x8D,0xCC,0x80,0x30,0x2B,0x4C,
0x81,0x09,0x2F,0x0A,0x81,0x11,0x2B,0x44,
0x81,0x16,0x2E,0x40,0x80,0x07,0x39,0xA6,
0x00,0x8D,0xA4,0xA6,0x00,0x08,0x20,0xA3,
0x8D,0xF5,0x8D,0xF3,0x86,0x20,0x20,0xA5,
0x8E,0xA0,0x42,0x20,0x2C,0x26,0x07,0x09,
0x09,0xFF,0xA0,0x0D,0x20,0xAC,0xFF,0xA0,
0x0D,0x20,0x02,0x20,0x6D,0x81,0x30,0x25,
0xA1,0x81,0x46,0x22,0x9D,0x8D,0xBD,0xBD,
0xE0,0x57,0x09,0xA7,0x00,0xA1,0x00,0x27,
0x91,0x7E,0xE0,0x40,0xBE,0xA0,0x08,0x20,
0x49,0xBF,0xA0,0x08,0x86,0xFF,0xBD,0xE3,
0x08,0xCE,0x80,0x04,0xBD,0xE2,0x84,0xA6,
0x00,0xA1,0x02,0x20,0x02,0x20,0x19,0x26,
0x39,0x86,0x03,0xA7,0x00,0x86,0x11,0xA7,
0x00,0x20,0x2F,0x01,0xBF,0xA0,0x08,0x30,
0x6D,0x06,0x26,0x02,0x6A,0x05,0x6A,0x06,
0xCE,0xE1,0x9D,0xBD,0xE0,0x7E,0xFE,0xA0,
0x08,0x08,0x8D,0x8E,0x8D,0x8C,0x8D,0x8A,
0x8D,0x86,0x8D,0x84,0xCE,0xA0,0x08,0xBD,
0xE0,0xC8,0xFE,0xA0,0x12,0x8C,0xE1,0x23,
0x27,0x19,0x8E,0xA0,0x42,0xCE,0x80,0x04,
0xFF,0xA0,0x0A,0x7F,0xA0,0x0C,0x8D,0x73,
0x27,0x03,0xBD,0xE2,0x7D,0xBD,0xE3,0x53,
0xBD,0xE3,0x47,0xCE,0xE1,0x9C,0xBD,0xE0,
0x7E,0x8D,0x39,0xCE,0xE3,0xD1,0xA1,0x00,
0x26,0x07,0xBD,0xE0,0xCC,0xEE,0x01,0x6E,
0x00,0x08,0x08,0x08,0x8C,0xE3,0xF8,0x26,
0xED,0x20,0xBF,0xFE,0xA0,0x12,0x6E,0x00,
0x53,0x39,0x04,0x0D,0x0A,0x15,0x00,0x00,
0x00,0x53,0x31,0x04,0x13,0x0D,0x0A,0x15,
0x00,0x00,0x00,0x24,0x04,0x20,0x4C,0xFE,
0xA0,0x06,0x6E,0x00,0x20,0x40,0xBD,0xE0,
0x47,0xFF,0xA0,0x04,0xBD,0xE0,0x47,0xBD,
0xE0,0x55,0x16,0xA6,0x00,0xFF,0xA0,0x0D,
0x11,0x27,0x02,0x20,0x21,0xCE,0xE1,0x9D,
0xBD,0xE0,0x7E,0xCE,0xA0,0x0D,0x20,0x10,
0x3B,0x20,0x3A,0xFF,0xA0,0x10,0xFE,0xA0,
0x0A,0x37,0xE6,0x01,0xE1,0x03,0x33,0x39,
0xBD,0xE0,0xC8,0xFE,0xA0,0x0D,0xBC,0xA0,
0x04,0x27,0x9E,0x08,0x20,0xCD,0x8D,0x06,
0x84,0x7F,0x39,0x31,0x31,0x31,0x37,0x8D,
0xDA,0x26,0x28,0x86,0x15,0xA7,0x00,0xA6,
0x00,0x47,0x24,0xFB,0xA6,0x01,0xF6,0xA0,
0x0C,0x27,0x07,0x20,0x11,0x37,0x8D,0xC3,
0x26,0x2E,0xC6,0x11,0xE7,0x00,0xE6,0x00,
0x57,0x57,0x24,0xFA,0xA7,0x01,0x33,0xFE,
0xA0,0x10,0x39,0xA6,0x00,0x2B,0xFC,0x8D,
0x3A,0xC6,0x04,0xE7,0x02,0x58,0x8D,0x2A,
0x0D,0x69,0x00,0x46,0x5A,0x26,0xF7,0x8D,
0x21,0xF6,0xA0,0x0C,0x27,0x13,0x20,0xDE,
0x8D,0x23,0xC6,0x0A,0x6A,0x00,0x8D,0x16,
0x8D,0x10,0xA7,0x00,0x0D,0x46,0x5A,0x26,
0xF7,0xE6,0x02,0x58,0x2A,0xC8,0x8D,0x02,
0x20,0xC4,0x6D,0x02,0x2A,0xFC,0x6C,0x02,
0x6A,0x02,0x39,0x6F,0x02,0x8D,0xF7,0x20,
0xF1,0x8D,0x83,0x16,0x7F,0xA0,0x0B,0xFE,
0xA0,0x0A,0x8D,0x10,0x8D,0x07,0xCE,0xE3,
0xEF,0x17,0x7E,0xE1,0x76,0x86,0x34,0xA7,
0x03,0xA7,0x02,0x39,0x6C,0x00,0x86,0x07,
0xA7,0x01,0x6C,0x00,0xA7,0x02,0x39,0x7F,
0x80,0x14,0x8D,0x2E,0xC6,0x0B,0x8D,0x25,
0xE6,0x04,0xC5,0x01,0x26,0xFA,0x6F,0x06,
0x8D,0x1D,0xC6,0x9C,0x8D,0x17,0xCE,0x24,
0x00,0xC5,0x02,0x27,0x06,0xB6,0x80,0x1B,
0xA7,0x00,0x08,0xF6,0x80,0x18,0xC5,0x01,
0x26,0xEF,0x7E,0x24,0x00,0xE7,0x04,0x8D,
0x00,0x39,0xCE,0xFF,0xFF,0x09,0x8C,0x80,
0x14,0x26,0xFA,0x39,0xCE,0xE0,0x09,0xBD,
0xE0,0x7E,0x8D,0xF1,0xBD,0xE3,0x47,0x20,
0x58,0xCE,0xE1,0x23,0xBC,0xA0,0x12,0x27,
0x1A,0x08,0x8D,0x32,0xBD,0xE0,0x47,0xFF,
0xA0,0x14,0xA6,0x00,0xB7,0xA0,0x16,0x86,
0x3F,0xA7,0x00,0xCE,0xE1,0x23,0x8D,0x1E,
0x7E,0xE1,0x6B,0xFE,0xA0,0x14,0xB6,0xA0,
0x16,0xA7,0x00,0xCE,0xE1,0x24,0x20,0xDA,
0xB7,0xA0,0x43,0xFE,0xA0,0x12,0x8C,0xE1,
0x23,0x27,0x06,0xCE,0xE1,0x24,0xFF,0xA0,
0x12,0x39,0x8D,0x5A,0x20,0x0F,0xCE,0xA0,
0x49,0xFF,0xA0,0x04,0x09,0x8D,0x52,0xCE,
0xE1,0x90,0xBD,0xE0,0x7E,0x8D,0x24,0x8D,
0x91,0x7E,0xE1,0x52,0x73,0xA0,0x0C,0x86,
0x11,0xC6,0x20,0x8D,0x1A,0xBD,0xE1,0xD9,
0x27,0x04,0x86,0x3C,0xA7,0x03,0x39,0x86,
0x13,0xC6,0x10,0x20,0x0A,0x86,0x12,0xC6,
0x04,0x20,0x04,0x86,0x14,0xC6,0x08,0xBD,
0xE0,0x75,0xBD,0xE1,0xD6,0x27,0x16,0x86,
0x02,0xCA,0x01,0x8D,0x0C,0x8D,0x08,0x86,
0x02,0xC6,0x01,0xE7,0x00,0x8D,0x02,0x86,
0x06,0xA7,0x01,0xE7,0x00,0x39,0xFE,0xA0,
0x02,0xFF,0xA0,0x44,0x8D,0xCF,0xB6,0xA0,
0x05,0xB0,0xA0,0x45,0xF6,0xA0,0x04,0xF2,
0xA0,0x44,0x26,0x04,0x81,0x10,0x25,0x02,
0x86,0x0F,0x8B,0x04,0xB7,0xA0,0x47,0x80,
0x03,0xB7,0xA0,0x46,0xCE,0xE1,0x93,0xBD,
0xE0,0x7E,0x5F,0xCE,0xA0,0x47,0x8D,0x24,
0xCE,0xA0,0x44,0x8D,0x1F,0x8D,0x1D,0xFE,
0xA0,0x44,0x8D,0x18,0x7A,0xA0,0x46,0x26,
0xF9,0xFF,0xA0,0x44,0x53,0x37,0x30,0x8D,
0x0B,0x33,0xFE,0xA0,0x44,0x09,0xBC,0xA0,
0x04,0x26,0xB3,0x39,0xEB,0x00,0x7E,0xE0,
0xBF,0x47,0xE1,0xD0,0x5A,0xC0,0x00,0x4D,
0xE0,0x88,0x46,0xE1,0xAE,0x52,0xE1,0x30,
0x4A,0xE0,0x05,0x43,0xE2,0xCC,0x44,0xE2,
0x8F,0x42,0xE2,0xD9,0x4F,0xE2,0x69,0x50,
0xE3,0x1A,0x4C,0xE0,0x0C,0x45,0xE3,0x1E,
0xE0,0x00,0xE1,0x8B,0xE1,0xA7,0xE0,0xD0
};

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit descriptor
   cpu_reg	CPU register list
   cpu_mod	CPU modifiers list */

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK,
		32768) };

REG cpu_reg[] = {
	{ HRDATA (PC, saved_PC, 16) },
	{ HRDATA (A, A, 8) },
	{ HRDATA (B, B, 8) },
	{ HRDATA (IX, IX, 16) },
	{ HRDATA (SP, SP, 16) },
	{ FLDATA (H, H, 16) },
	{ FLDATA (I, I, 16) },
	{ FLDATA (N, N, 16) },
	{ FLDATA (Z, Z, 16) },
	{ FLDATA (V, V, 16) },
	{ FLDATA (C, C, 16) },
	{ FLDATA (INTE, INTE, 16) },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
	{ UNIT_OPSTOP, UNIT_OPSTOP, "ITRAP", "ITRAP", NULL },
	{ UNIT_OPSTOP, 0, "NOITRAP", "NOITRAP", NULL },
	{ UNIT_MSTOP, UNIT_MSTOP, "MTRAP", "MTRAP", NULL },
	{ UNIT_MSTOP, 0, "NOMTRAP", "NOMTRAP", NULL },
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ UNIT_MA000, UNIT_MA000, "MA000", "MA000", NULL },
	{ UNIT_MA000, 0, "NOMA000", "NOMA000", NULL },
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 16, 16, 1, 16, 8,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

int32 PC;							/* global for the helper routines */

int32 sim_instr (void)
{
	extern int32 sim_interval;
	int32 IR, OP, DAR, reason, hi, lo, op1;
//	uint32 val1[3];
	
	PC = saved_PC & ADDRMASK;		/* load local PC */
	reason = 0;

	/* Main instruction fetch/decode loop */

	while (reason == 0) {			/* loop until halted */
    	if (sim_interval <= 0) 		/* check clock queue */
			if (reason = sim_process_event ()) 
				break;
		if (mem_fault) {			/* memory fault? */
			mem_fault = 0;			/* reset fault flag */
			reason = STOP_MEMORY;
			break;
		}
    	if (int_req > 0) {			/* interrupt? */
    /* 6800 interrupts not implemented yet.  None were used,
       on a standard SWTP 6800. All I/O is programmed. */
    	}							/* end interrupt */
		if (sim_brk_summ &&
		    sim_brk_test (PC, SWMASK ('E'))) {	/* breakpoint? */
			reason = STOP_IBKPT;	/* stop simulation */
			break;
		}
	/* transient routine area - trace */
		/*
		if (PC >= 0xa100 && PC < 0xa400) { 
			dump_regs();
			printf("\n\r%04X:	", PC);
			val1[0] = M[PC];
			val1[1] = M[PC+1];
			val1[2] = M[PC+2];
			fprint_sym(stdout, PC, val1, NULL, SWMASK ('M'));
		}
*/
	  	IR = OP = mem_get_byte(PC);		/* fetch instruction */
	   	PC = (PC + 1) & ADDRMASK;  		/* increment PC */
	   	sim_interval--;

    /* The Big Instruction Decode Switch */

		switch (IR) {

			case 0x01:				/* NOP */
				break;
			case 0x06:				/* TAP */
				set_psw(A);
				break;
			case 0x07:				/* TPA */
				A = get_psw();
				break;
			case 0x08:				/* INX */
				IX = (IX + 1) & ADDRMASK;
				condevalZ(IX);
				break;
			case 0x09:				/* DEX */
				IX = (IX + 1) & ADDRMASK;
				condevalZ(IX);
				break;
			case 0x0A:				/* CLV */
				V = 0;
				break;
			case 0x0B:				/* SEV */
				V = 0x10000;
				break;
			case 0x0C:				/* CLC */
				C = 0;
				break;
			case 0x0D:				/* SEC */
				C = 0x10000;
				break;
			case 0x0E:				/* CLI */
				I = 0;
				break;
			case 0x0F:				/* SEI */
				I = 0x10000;
				break;
			case 0x10:				/* SBA */
				op1 = A;
				A = A - B;
				condevalN(A);
				condevalZ(A);
				condevalC(A);
				condevalVs(B, op1);
				A &= 0xFF;
				break;
			case 0x11:				/* CBA */
				lo = A - B;
				condevalN(lo);
				condevalZ(lo);
				condevalC(lo);
				condevalVs(B, A);
				break;
			case 0x16:				/* TAB */
				B = A;
				condevalN(B);
				condevalZ(B);
				V = 0;
				break;
			case 0x17:				/* TBA */
				A = B;
				condevalN(B);
				condevalZ(B);
				V = 0;
				break;
			case 0x19:				/* DAA */
    			DAR = A & 0x0F;
				op1 = C;
				if (DAR > 9 || C) {
            		DAR += 6;
					A &= 0xF0;
            		A |= DAR & 0x0F;
					C = 0;
					if (DAR & 0x10)
						C = 0x10000;
				}
				DAR = (A >> 4) & 0x0F;
				if (DAR > 9 || C) {
					DAR += 6;
				  if (C) 
					  DAR++;
				  A &= 0x0F;
				  A |= (DAR << 4);
				}
				C = op1;
				if ((DAR << 4) & 0x100)
					C = 0x10000;
				condevalN(A);
				condevalZ(A);
				A &= 0xFF;
				break;
			case 0x1B:				/* ABA */
				A += B;
				condevalH(A);
				condevalN(A);
				condevalZ(A);
				condevalC(A);
				condevalVa(A, B);
				A &= 0xFF;
				break;
			case 0x20:				/* BRA rel */
				go_rel(1);
				break;
			case 0x22:				/* BHI rel */
				go_rel(!(C | Z));
				break;
			case 0x23:				/* BLS rel */
				go_rel(C | Z);
				break;
			case 0x24:				/* BCC rel */
				go_rel(!C);
				break;
			case 0x25:				/* BCS rel */
				go_rel(C);
				break;
			case 0x26:				/* BNE rel */
				go_rel(!Z);
				break;
			case 0x27:				/* BEQ rel */
				go_rel(Z);
				break;
			case 0x28:				/* BVC rel */
				go_rel(!V);
				break;
			case 0x29:				/* BVS rel */
				go_rel(V);
				break;
			case 0x2A:				/* BPL rel */
				go_rel(!N);
				break;
			case 0x2B:				/* BMI rel */
				go_rel(N);
				break;
			case 0x2C:				/* BGE rel */
				go_rel(!(N ^ V));
				break;
			case 0x2D:				/* BLT rel */
				go_rel(N ^ V);
				break;
			case 0x2E:				/* BGT rel */
				go_rel(!(Z | (N ^ V)));
				break;
			case 0x2F:				/* BLE rel */
				go_rel(Z | (N ^ V));
				break;
			case 0x30:				/* TSX */
				IX = (SP + 1) & ADDRMASK;
				break;
			case 0x31:				/* INS */
				SP = (SP + 1) & ADDRMASK;
				break;
			case 0x32:				/* PUL A */
				SP = (SP + 1) & ADDRMASK;
		     	A = mem_get_byte(SP);
				break;
			case 0x33:				/* PUL B */
				SP = (SP + 1) & ADDRMASK;
		     	B = mem_get_byte(SP);
				break;
			case 0x34:				/* DES */
				SP = (SP - 1) & ADDRMASK;
				break;
			case 0x35:				/* TXS */
				SP = (IX - 1) & ADDRMASK;
				break;
			case 0x36:				/* PSH A */
		        mem_put_byte(SP, A);
				SP = (SP - 1) & ADDRMASK;
				break;
			case 0x37:				/* PSH B */
		        mem_put_byte(SP, B);
				SP = (SP - 1) & ADDRMASK;
 				break;
			case 0x39:				/* RTS */
				SP = (SP + 1) & ADDRMASK;
				PC = mem_get_word(SP) & ADDRMASK;
				SP = (SP + 1) & ADDRMASK;
				break;
			case 0x3B:				/* RTI */
				SP = (SP + 1) & ADDRMASK;
				set_psw(mem_get_byte(SP));
				SP = (SP + 1) & ADDRMASK;
				B = mem_get_byte(SP);
				SP = (SP + 1) & ADDRMASK;
				A = mem_get_byte(SP);
				SP = (SP + 1) & ADDRMASK;
				IX = mem_get_word(SP);
				SP = (SP + 2) & ADDRMASK;
				PC = mem_get_word(SP) & ADDRMASK;
				SP = (SP + 1) & ADDRMASK;
				break;
			case 0x3E:				/* WAI */
				SP = (SP - 1) & ADDRMASK;
				mem_put_word(SP, PC);
				SP = (SP - 2) & ADDRMASK;
				mem_put_word(SP, IX);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, A);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, B);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, get_psw());
				SP = (SP - 1) & ADDRMASK;
				if (I) {
	   				reason = STOP_HALT;
	       			continue;
				} else {
					I = 0x10000;
					PC = mem_get_word(0xFFFE) & ADDRMASK;
				}
				break;
			case 0x3F:				/* SWI */
				SP = (SP - 1) & ADDRMASK;
				mem_put_word(SP, PC);
				SP = (SP - 2) & ADDRMASK;
				mem_put_word(SP, IX);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, A);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, B);
				SP = (SP - 1) & ADDRMASK;
		        mem_put_byte(SP, get_psw());
				SP = (SP - 1) & ADDRMASK;
				I = 0x10000;
				PC = mem_get_word(0xFFFB) & ADDRMASK;
				break;
			case 0x40:				/* NEG A */
				A = (0 - A) & 0xFF;
				V = 0;
				if (A & 0x80)
					V = 0x10000;
				C = 0;
				if (A)
					C = 0x10000;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x43:				/* COM A */
				A = ~A & 0xFF;
				V = 0;
				C = 0x10000;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x44:				/* LSR A */
				C = 0;
				if (A & 0x01)
					C = 0x10000;
				A = (A >> 1) & 0xFF;
				N = 0;
				condevalZ(A);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x46:				/* ROR A */
				hi = C;
				C = 0;
				if (A & 0x01)
					C = 0x10000;
				A = (A >> 1) & 0xFF;
				if (hi)
					A |= 0x80;
				condevalN(A);
				condevalZ(A);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x47:				/* ASR A */
				C = 0;
				if (A & 0x01)
					C = 0x10000;
				lo = A & 0x8000;
				A = (A >> 1) & 0xFF;
				A |= lo; 
				condevalN(A);
				condevalZ(A);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x48:				/* ASL A */
				C = 0;
				if (A & 0x80)
					C = 0x10000;
				A = (A << 1) & 0xFF;
				condevalN(A);
				condevalZ(A);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x49:				/* ROL A */
				hi = C;
				C = 0;
				if (A & 0x80)
					C = 0x10000;
				A = (A << 1) & 0xFF;
				if (hi)
					A |= 0x01;
				condevalN(A);
				condevalZ(A);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x4A:				/* DEC A */
				V = 0;
				if (A == 0x80)
					V = 0x10000;
				A = (A - 1) & 0xFF;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x4C:				/* INC A */
				V = 0;
				if (A == 0x7F)
					V = 0x10000;
				A = (A + 1) & 0xFF;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x4D:				/* TST A */
				lo = (A - 0) & 0xFF;
				V = 0;
				C = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x4F:				/* CLR A */
				A = 0;
				N = V = C = 0;
				Z = 0x10000;
				break;
			case 0x50:				/* NEG B */
				B = (0 - V) & 0xFF;
				V = 0;
				if (B & 0x8000)
					V = 0x10000;
				C = 0;
				if (B)
					C = 0x10000;
				condevalN(B);
				condevalZ(B);
				break;
			case 0x53:				/* COM B */
				B = ~B & 0xFF;
				V = 0;
				C = 0x10000;
				condevalN(B);
				condevalZ(B);
				break;
			case 0x54:				/* LSR B */
				C = 0;
				if (B & 0x01)
					C = 0x10000;
				B = (B >> 1) & 0xFF;
				N = 0;
				condevalZ(B);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x56:				/* ROR B */
				hi = C;
				C = 0;
				if (B & 0x01)
					C = 0x10000;
				B = (B >> 1) & 0xFF;
				if (hi)
					B |= 0x80;
				condevalN(B);
				condevalZ(B);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x57:				/* ASR B */
				C = 0;
				if (B & 0x01)
					C = 0x10000;
				lo = B & 0x8000;
				B = (B >> 1) & 0xFF;
				B |= lo; 
				condevalN(B);
				condevalZ(B);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x58:				/* ASL B */
				C = 0;
				if (B & 0x80)
					C = 0x10000;
				B = (B << 1) & 0xFF;
				condevalN(B);
				condevalZ(B);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x59:				/* ROL B */
				hi = C;
				C = 0;
				if (B & 0x80)
					C = 0x10000;
				B = (B << 1) & 0xFF;
				if (hi)
					B |= 0x01;
				condevalN(B);
				condevalZ(B);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x5A:				/* DEC B */
				V = 0;
				if (B == 0x80)
					V = 0x10000;
				B = (B - 1) & 0xFF;
				condevalN(B);
				condevalZ(B);
				break;
			case 0x5C:				/* INC B */
				V = 0;
				if (B == 0x7F)
					V = 0x10000;
				B = (B + 1) & 0xFF;
				condevalN(B);
				condevalZ(B);
				break;
			case 0x5D:				/* TST B */
				lo = (B - 0) & 0xFF;
				V = 0;
				C = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x5F:				/* CLR B */
				B = 0;
				N = V = C = 0;
				Z = 0x10000;
				break;
			case 0x60:				/* NEG ind */
				DAR = get_indir_addr();
				lo = (0 - mem_get_byte(DAR)) & 0xFF;
				mem_put_byte(DAR, lo);
				V = 0;
				if (lo & 0x80)
					V = 0x10000;
				C = 0;
				if (lo)
					C = 0x10000;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x63:				/* COM ind */
				DAR = get_indir_addr();
				lo = ~mem_get_byte(DAR) & 0xFF;
				mem_put_byte(DAR, lo);
				V = 0;
				C = 0x10000;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x64:				/* LSR ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				lo >>= 1;
				mem_put_byte(DAR, lo);
				N = 0;
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x66:				/* ROR ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				hi = C;
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				lo >>= 1;
				if (hi)
					lo |= 0x80;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x67:				/* ASR ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				lo = (lo & 0x80) | (lo >> 1);
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x68:				/* ASL ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x80)
					C = 0x10000;
				lo <<= 1;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x69:				/* ROL ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				hi = C;
				C = 0;
				if (lo & 0x80)
					C = 0x10000;
				lo <<= 1;
				if (hi)
					lo |= 0x01;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x6A:				/* DEC ind */
				DAR = get_indir_addr();
				lo = mem_get_byte(DAR);
				V = 0;
				if (lo == 0x80)
					V = 0x10000;
				lo = (lo - 1) & 0xFF;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x6C:				/* INC ind */
				DAR= get_indir_addr();
				lo = mem_get_byte(DAR);
				V = 0;
				if (lo == 0x7F)
					V = 0x10000;
				lo = (lo + 1) & 0xFF;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x6D:				/* TST ind */
				lo = (get_indir_val() - 0) & 0xFF;
				V = 0;
				C = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x6E:				/* JMP ind */
				PC = get_indir_addr();
				break;
			case 0x6F:				/* CLR ind */
				mem_put_byte(get_indir_addr(), 0);
				N = V = C = 0;
				Z = 0x10000;
				break;
			case 0x70:				/* NEG ext */
				DAR = get_ext_addr(PC);
				lo = (0 - mem_get_byte(DAR)) & 0xFF;
				mem_put_byte(DAR, lo);
				V = 0;
				if (lo & 0x80)
					V = 0x10000;
				C = 0;
				if (lo)
					C = 0x10000;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x73:				/* COM ext */
				DAR = get_ext_addr();
				lo = ~mem_get_byte(DAR) & 0xFF;
				mem_put_byte(DAR, lo);
				V = 0;
				C = 0x10000;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x74:				/* LSR ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				lo >>= 1;
				mem_put_byte(DAR, lo);
				N = 0;
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x76:				/* ROR ext */
				DAR = get_ext_addr();
				hi = C;
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				lo >>= 1;
				if (hi)
					lo |= 0x80;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x77:				/* ASR ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x01)
					C = 0x10000;
				hi = lo & 0x80;
				lo >>= 1;
				lo |= hi;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x78:				/* ASL ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				C = 0;
				if (lo & 0x80)
					C = 0x10000;
				lo <<= 1;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x79:				/* ROL ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				hi = C;
				C = 0;
				if (lo & 0x80)
					C = 0x10000;
				lo <<= 1;
				if (hi)
					lo |= 0x01;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				V = 0;
				if (N ^ C)
					V = 0x10000;
				break;
			case 0x7A:				/* DEC ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				V = 0;
				if (lo == 0x80)
					V = 0x10000;
				lo = (lo - 1) & 0xFF;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x7C:				/* INC ext */
				DAR = get_ext_addr();
				lo = mem_get_byte(DAR);
				V = 0;
				if (lo == 0x7F)
					V = 0x10000;
				lo = (lo + 1) & 0xFF;
				mem_put_byte(DAR, lo);
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x7D:				/* TST ext */
				lo = mem_get_byte(get_ext_addr()) - 0;
				V = 0;
				C = 0;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				break;
			case 0x7E:				/* JMP ext */
				PC = get_ext_addr() & ADDRMASK;
				break;
			case 0x7F:				/* CLR ext */
				mem_put_byte(get_ext_addr(), 0);
				N = V = C = 0;
				Z = 0x10000;
				break;
			case 0x80:				/* SUB A imm */
				op1 = get_dir_addr();
				A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x81:				/* CMP A imm */
				op1 = get_dir_addr();
				lo = A - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(lo, op1);
				break;
			case 0x82:				/* SBC A imm */
				op1 = get_dir_addr();
				if (C)
					A = A - op1 - 1;
				else
					A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x84:				/* AND A imm */
				A = (A & get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x85:				/* BIT A imm */
				lo = (A & get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x86:				/* LDA A imm */
				A = get_dir_addr();
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x88:				/* EOR A imm */
				A = (A ^ get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x89:				/* ADC A imm */
				op1 = get_dir_addr();
				if (C)
					A = A + op1 + 1;
				else
					A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x8A:				/* ORA A imm */
				A = (A | get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x8B:				/* ADD A imm */
				op1 = get_dir_addr();
				A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x8C:				/* CPX imm */
				op1 = IX - get_ext_addr();
				condevalZ(op1);
				condevalN(op1 >> 8);
				V = op1 & 0x10000;
				break;
			case 0x8D:				/* BSR rel */
				lo = get_rel_addr();
				SP = (SP - 1) & ADDRMASK;
				mem_put_word(SP, PC);
				SP = (SP - 1) & ADDRMASK;
				PC = PC + lo;
				PC &= ADDRMASK;
				break;
			case 0x8E:				/* LDS imm */
				SP = get_ext_addr();
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0x90:				/* SUB A dir */
				op1 = get_dir_val();
				A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x91:				/* CMP A dir */
				op1 = get_dir_val();
				lo = A - op1;
				condevalN(lo);
				condevalZ(lo & 0xff);
				condevalC(lo);
				condevalVs(A, op1);
				break;
			case 0x92:				/* SBC A dir */
				op1 = get_dir_val();
				if (C)
					A = A - op1 - 1;
				else
					A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x94:				/* AND A dir */
				A = (A & get_dir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x95:				/* BIT A dir */
				lo = (A & get_dir_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0x96:				/* LDA A dir */
				A = get_dir_val();
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x97:				/* STA A dir */
				mem_put_byte(get_dir_addr(), A);
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x98:				/* EOR A dir */
				A = (A ^ get_dir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x99:				/* ADC A dir */
				op1 = get_dir_val();
				if (C)
					A = A + op1 + 1;
				else
					A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x9A:				/* ORA A dir */
				A = (A | get_dir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0x9B:				/* ADD A dir */
				op1 = get_dir_val();
				A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0x9C:				/* CPX dir */
				op1 = IX - mem_get_word(get_dir_addr());
				condevalZ(op1);
				condevalN(op1 >> 8);
				V = op1 & 0x10000;
				break;
			case 0x9E:				/* LDS dir */
				SP = mem_get_word(get_dir_addr());
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0x9F:				/* STS dir */
				mem_put_word(get_dir_addr(), SP);
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0xA0:				/* SUB A ind */
				op1 = get_indir_val();
				A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xA1:				/* CMP A ind */
				op1 = get_indir_val();
				lo = A - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(A, op1);
				break;
			case 0xA2:				/* SBC A ind */
				op1 = get_indir_val();
				if (C)
					A = A - op1 - 1;
				else
					A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xA4:				/* AND A ind */
				A = (A & get_indir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xA5:				/* BIT A ind */
				lo = (A & get_indir_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xA6:				/* LDA A ind */
				A = get_indir_val();
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xA7:				/* STA A ind */
				mem_put_byte(get_indir_addr(), A);		
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xA8:				/* EOR A ind */
				A = (A ^ get_indir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xA9:				/* ADC A ind */
				op1 = get_indir_val();
				if (C)
					A = A + op1 + 1;
				else
					A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xAA:				/* ORA A ind */
				A = (A | get_indir_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xAB:				/* ADD A ind */
				op1 = get_indir_val();
				A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xAC:				/* CPX ind */
				op1 = (IX - get_indir_addr()) & ADDRMASK;
				condevalZ(op1);
				condevalN(op1 >> 8);
				V = op1 & 0x10000;
				break;
			case 0xAD:				/* JSR ind */
				DAR = get_indir_addr();
				SP = (SP - 1) & ADDRMASK;
				mem_put_word(SP, PC);
				SP = (SP - 1) & ADDRMASK;
				PC = DAR;
				break;
			case 0xAE:				/* LDS ind */
				SP = mem_get_word(get_indir_addr());
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0xAF:				/* STS ind */
				mem_put_word(get_indir_addr(), SP);
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0xB0:				/* SUB A ext */
				op1 = get_ext_val();
				A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xB1:				/* CMP A ext */
				op1 = get_ext_val();
				lo = A - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(A, op1);
				break;
			case 0xB2:				/* SBC A ext */
				op1 = get_ext_val();
				if (C)
					A = A - op1 - 1;
				else
					A = A - op1;
				condevalN(A);
				condevalC(A);
				condevalVs(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xB4:				/* AND A ext */
				A = (A & get_ext_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xB5:				/* BIT A ext */
				lo = (A & get_ext_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xB6:				/* LDA A ext */
				A = get_ext_val();
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xB7:				/* STA A ext */
				mem_put_byte(get_ext_addr(), A);		
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xB8:				/* EOR A ext */
				A = (A ^ get_ext_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xB9:				/* ADC A ext */
				op1 = get_ext_val();
				if (C)
					A = A + op1 + 1;
				else
					A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xBA:				/* ORA A ext */
				A = (A | get_ext_val()) & 0xFF;
				V = 0;
				condevalN(A);
				condevalZ(A);
				break;
			case 0xBB:				/* ADD A ext */
				op1 = get_ext_val();
				A = A + op1;
				condevalH(A);
				condevalN(A);
				condevalC(A);
				condevalVa(A, op1);
				A &= 0xFF;
				condevalZ(A);
				break;
			case 0xBC:				/* CPX ext */
				op1 = (IX - mem_get_word(get_ext_addr())) & ADDRMASK;
				condevalZ(op1);
				condevalN(op1 >> 8);
				V = op1 & 0x10000;
				break;
			case 0xBD:				/* JSR ext */
				DAR = get_ext_addr();
				SP = (SP - 1) & ADDRMASK;
				mem_put_word(SP, PC);
				SP = (SP - 1) & ADDRMASK;
				PC = DAR;
				break;
			case 0xBE:				/* LDS ext */
				SP = mem_get_word(get_ext_addr());
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0xBF:				/* STS ext */
				mem_put_word(get_ext_addr(), SP);
				condevalN(SP >> 8);
				condevalZ(SP);
				V = 0;
				break;
			case 0xC0:				/* SUB B imm */
				op1 = get_dir_addr();
				B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xC1:				/* CMP B imm */
				op1 = get_dir_addr();
				lo = B - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(B, op1);
				break;
			case 0xC2:				/* SBC B imm */
				op1 = get_dir_addr();
				if (C)
					B = B - op1 - 1;
				else
					B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xC4:				/* AND B imm */
				B = (B & get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xC5:				/* BIT B imm */
				lo = (B & get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xC6:				/* LDA B imm */
				B = get_dir_addr();
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xC8:				/* EOR B imm */
				B = (B ^ get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xC9:				/* ADC B imm */
				op1 = get_dir_addr();
				if (C)
					B = B + op1 + 1;
				else
					B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xCA:				/* ORA B imm */
				B = (B | get_dir_addr()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xCB:				/* ADD B imm */
				op1 = get_dir_addr();
				B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xCE:				/* LDX imm */
				IX = get_ext_addr();
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xD0:				/* SUB B dir */
				op1 = get_dir_val();
				B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xD1:				/* CMP B dir */
				op1 = get_dir_val();
				lo = B - op1;
				condevalN(lo);
				condevalZ(lo);
				condevalC(lo);
				condevalVs(B, op1);
				break;
			case 0xD2:				/* SBC B dir */
				op1 = get_dir_val();
				if (C)
					B = B - op1 - 1;
				else
					B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xD4:				/* AND B dir */
				B = (B & get_dir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xD5:				/* BIT B dir */
				lo = (B & get_dir_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xD6:				/* LDA B dir */
				B = get_dir_val();
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xD7:				/* STA B dir */
				mem_put_byte(get_dir_addr(), B);
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xD8:				/* EOR B dir */
				B = (B ^ get_dir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xD9:				/* ADC B dir */
				op1 = get_dir_val();
				if (C)
					B = B + op1 + 1;
				else
					B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xDA:				/* ORA B dir */
				B = (B | get_dir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xDB:				/* ADD B dir */
				op1 = get_dir_val();
				B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xDE:				/* LDX dir */
				IX = mem_get_word(get_dir_addr());
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xDF:				/* STX dir */
				mem_put_word(get_dir_addr(), IX);
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xE0:				/* SUB B ind */
				op1 = get_indir_val();
				B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xE1:				/* CMP B ind */
				op1 = get_indir_val();
				lo = B - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(B, op1);
				break;
			case 0xE2:				/* SBC B ind */
				op1 = get_indir_val();
				if (C)
					B = B - op1 - 1;
				else
					B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xE4:				/* AND B ind */
				B = (B & get_indir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xE5:				/* BIT B ind */
				lo = (B & get_indir_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xE6:				/* LDA B ind */
				B = get_indir_val();
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xE7:				/* STA B ind */
				mem_put_byte(get_indir_addr(), B);
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xE8:				/* EOR B ind */
				B = (B ^ get_indir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xE9:				/* ADC B ind */
				op1 = get_indir_val();
				if (C)
					B = B + op1 + 1;
				else
					B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xEA:				/* ORA B ind */
				B = (B | get_indir_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xEB:				/* ADD B ind */
				op1 = get_indir_val();
				B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xEE:				/* LDX ind */
				IX = mem_get_word(get_indir_addr());
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xEF:				/* STX ind */
				mem_put_word(get_indir_addr(), IX);
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xF0:				/* SUB B ext */
				op1 = get_ext_val();
				B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xF1:				/* CMP B ext */
				op1 = get_ext_val();
				lo = B - op1;
				condevalN(lo);
				condevalZ(lo & 0xFF);
				condevalC(lo);
				condevalVs(B, op1);
				break;
			case 0xF2:				/* SBC B ext */
				op1 = get_ext_val();
				if (C)
					B = B - op1 - 1;
				else
					B = B - op1;
				condevalN(B);
				condevalC(B);
				condevalVs(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xF4:				/* AND B ext */
				B = (B & get_ext_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xF5:				/* BIT B ext */
				lo = (B & get_ext_val()) & 0xFF;
				V = 0;
				condevalN(lo);
				condevalZ(lo);
				break;
			case 0xF6:				/* LDA B ext */
				B = get_ext_val();
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xF7:				/* STA B ext */
				mem_put_byte(get_ext_addr(), B);
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xF8:				/* EOR B ext */
				B = (B ^ get_ext_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xF9:				/* ADC B ext */
				op1 = get_ext_val();
				if (C)
					B = B + op1 + 1;
				else
					B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xFA:				/* ORA B ext */
				B = (B | get_ext_val()) & 0xFF;
				V = 0;
				condevalN(B);
				condevalZ(B);
				break;
			case 0xFB:				/* ADD B ext */
				op1 = get_ext_val();
				B = B + op1;
				condevalH(B);
				condevalN(B);
				condevalC(B);
				condevalVa(B, op1);
				B &= 0xFF;
				condevalZ(B);
				break;
			case 0xFE:				/* LDX ext */
				IX = mem_get_word(get_ext_addr());
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;
			case 0xFF:				/* STX ext */
				mem_put_word(get_ext_addr(), IX);
				condevalN(IX >> 8);
				condevalZ(IX);
				V = 0;
				break;

	    	default: {				/* Unassigned */
	        	if (cpu_unit.flags & UNIT_OPSTOP) {
		    		reason = STOP_OPCODE;
		            PC--;
				}
    			break;
	    	}
	    }
	}
	/* Simulation halted - lets dump all the registers! */
	dump_regs();
	saved_PC = PC;
	return reason;
}

/* dump the working registers */

void dump_regs()
{
	printf("\r\nPC=%04X SP=%04X IX=%04X ", PC, SP, IX);
	printf("A=%02X B=%02X PSW=%02X", A, B, get_psw());
}

/*	this routine does the jump to relative offset if the condition is
	met.  Otherwise, execution continues at the current PC. */

void go_rel(int32 cond)
{
	int32 temp;

	temp = get_rel_addr();
	if (cond)
		PC += temp;
	PC &= ADDRMASK;
}

/* returns the relative offset sign-extended */

int32 get_rel_addr()
{
	int32 temp;

	temp = mem_get_byte(PC++);
	if (temp & 0x80)
		temp |= 0xFF00;
	return temp & ADDRMASK;
}

/* returns the value at the direct address pointed to by PC */

int32 get_dir_val()
{
	return mem_get_byte(get_dir_addr());
}

/* returns the direct address pointed to by PC */

int32 get_dir_addr()
{
	int32 temp;

	temp = mem_get_byte(PC);
	PC = (PC + 1) & ADDRMASK;
	return temp & 0xFF;
}

/* returns the value at the indirect address pointed to by PC */

int32 get_indir_val()
{
	return mem_get_byte(get_indir_addr());
}

/* returns the indirect address pointed to by PC or immediate byte */

int32 get_indir_addr()
{
	int32 temp;
	
	temp = (mem_get_byte(PC++) + IX) & ADDRMASK;
	PC &= ADDRMASK;
	return temp;
}

/* returns the value at the extended address pointed to by PC */

int32 get_ext_val()
{
	return mem_get_byte(get_ext_addr());
}

/* returns the extended address pointed to by PC or immediate word */

int32 get_ext_addr()
{
	int32 temp;

	temp = (mem_get_byte(PC) << 8) | mem_get_byte(PC+1);
	PC = (PC +2) & ADDRMASK;
	return temp;
}

/* return a PSW from the current flags */

int32 get_psw()
{
	int32 psw;

	psw = 0xC0;
	if (H)
		psw |= 0x20;
	if (I)
		psw |= 0x10;
	if (N)
		psw |= 0x08;
	if (Z)
		psw |= 0x04;
	if (V)
		psw |= 0x02;
	if (C)
		psw |= 0x01;
	return psw;
}

/* set the current flags from a PSW */

void set_psw(int32 psw)
{
	H = 0;
	if (psw & 0x20)
		H = 0x10000;
	I = 0;
	if (psw & 0x10)
		I = 0x10000;
	N = 0;
	if (psw & 0x08)
		N = 0x10000;
	Z = 0;
	if (psw & 0x04)
		Z = 0x10000;
	V = 0;
	if (psw & 0x02)
		V = 0x10000;
	C = 0;
	if (psw & 0x01)
		C = 0x10000;
}

/* test and set H */

void condevalH(int32 res)
{
	H = (res & 0x10) << 12;
}

/* test and set N */

void condevalN(int32 res)
{
	N = 0;
	if (res & 0x80)
		N = 0x10000;
}

/* test and set Z */

void condevalZ(int32 res)
{
	Z = 0;
	if (res == 0)
		Z = 0x10000;
}

/* test and set V for addition */

void condevalVa(int32 op1, int32 op2)
{
	if (C) {
		V = 0;
		if (((op1 & 0x80) && (op2 & 0x80)) ||
			(((op1 & 0x80) == 0) && ((op2 & 0x80) == 0)))
			V = 0x10000;
	}
}

/* test and set V for subtraction */

void condevalVs(int32 op1, int32 op2)
{
	if (C) {
		V = 0;
		if (((op1 & 0x80) && ((op2 & 0x80) == 0)) ||
			(((op1 & 0x80) == 0) && (op2 & 0x80)))
			V = 0x10000;
	}
}

/* test and set C */

void condevalC(int32 res)
{
	C = (res & 0x100) << 8;
}

/* memory write operations */

/* put word */

void mem_put_word(int32 addr, int32 val)
{
	mem_put_byte(addr,val >> 8);
	mem_put_byte(addr + 1, val);
}

/* put byte */

void mem_put_byte(int32 addr, int32 val)
{
	if (addr >= 0x0000 && addr < (int32) MEMSIZE)  	/* memory cards */
		M[addr] = val & 0xFF;
	else if (addr >= 0x8000 && addr < 0x8020) 		/* memory mapped I/O */
		dev_table[addr - 0x8000].routine(1, val);
	else  if (addr >= 0xA000 && addr < 0xA080)  	/* CPU memory */
		M[addr] = val & 0xFF;
	else  if ((addr >= 0xA080 && addr < 0xC000) &&	/* extended CPU memory */
		cpu_unit.flags & UNIT_MA000)		
		M[addr] = val & 0xFF;
	else {
		if (cpu_unit.flags & UNIT_MSTOP)
			mem_fault = 1;
		printf("Invalid write to %04X\n\r", addr);
	}
}

/* memory read operations */

/* get word */

int32 mem_get_word(int32 addr)
{
	int32 temp;

	temp = (mem_get_byte(addr) << 8) | mem_get_byte(addr+1);
	return temp;
}

/* get byte */

int32 mem_get_byte(int32 addr)
{
	int32 val;

	if (addr >= 0x0000 && addr < (int32) MEMSIZE)  	/* memory cards */
		val = M[addr];
	else if (addr >= 0x8000 && addr < 0x8020) 		/* memory mapped I/O */
		val = dev_table[addr - 0x8000].routine(0, 0);
	else  if (addr >= 0xA000 && addr < 0xA080)  	/* CPU memory */
		val = M[addr];
	else  if ((addr >= 0xA080 && addr < 0xC000) &&	/* extended CPU memory */
		cpu_unit.flags & UNIT_MA000)		
		val = M[addr];
	else  if (addr >= 0xE000 && addr < 0x10000)  	/* ROM memory */
		val = M[addr];
	else {
		if (cpu_unit.flags & UNIT_MSTOP)
			mem_fault = 1;
		val = 0xFF;				/* default for no memory at address */
		printf("Invalid read of %04X\n\r", addr);
	}
	return val & 0xFF;
}

/* calls from the simulator */

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
	int i;

	I = 0x10000;
	saved_PC = (M[0xFFFE] << 8) | M[0xFFFF];
	int_req = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	/* copy in rom image at E000 */
	for (i = 0; i < BOOTLEN; i++) {
	     M[i + 0xE000] = bootrom[i] & 0xFF;
	}
	/* copy in rom image at FC00 for vectors! */
	for (i = 0; i < BOOTLEN; i++) {
	     M[i + 0xFC00] = bootrom[i] & 0xFF;
	}
	return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MAXMEMSIZE)
		return SCPE_NXM;
	if (vptr != NULL)
		*vptr = mem_get_byte(addr);
	return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MAXMEMSIZE)
		return SCPE_NXM;
    mem_put_byte(addr, val);
//	printf("Deposit to %04X of %02X\n\r", addr, val);
	return SCPE_OK;
}

/* adjust the memory size for the emulator 4k to 32k in 4k steps */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
	int32 mc = 0;
	uint32 i;

	if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 0x0FFF) != 0))
		return SCPE_ARG;
	for (i = val; i < MEMSIZE; i++)
		mc = mc | M[i];
	if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
		return SCPE_OK;
	MEMSIZE = val;
	return SCPE_OK;
}

/* dummy i/o device */

int32 nulldev(int32 io, int32 data)
{
	if (io == 0)
		return (0xFF);
	return 0;
}

