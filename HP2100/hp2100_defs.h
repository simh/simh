/* hp2100_defs.h: HP 2100 simulator definitions

   Copyright (c) 1993-2001, Robert M. Supnik

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

   30-Nov-01	RMS	Added extended SET/SHOW support
   15-Oct-00	RMS	Added dynamic device numbers
   14-Apr-99	RMS	Changed t_addr to unsigned

   The author gratefully acknowledges the help of Jeff Moffat in answering
   questions about the HP2100.
*/

#include "sim_defs.h"					/* simulator defns */

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_IODV	2				/* must be 2 */
#define STOP_HALT	3				/* HALT */
#define STOP_IBKPT	4				/* breakpoint */
#define STOP_IND	5				/* indirect loop */

/* Memory */

#define MAXMEMSIZE	32768				/* max memory size */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define AMASK		(MAXMEMSIZE - 1)		/* address mask */
#define MEM_ADDR_OK(x)	(((t_addr) (x)) < MEMSIZE)

/* Architectural constants */

#define SIGN		0100000				/* sign */
#define DMASK		0177777				/* data mask */
#define AR		M[0]				/* A = location 0 */
#define BR		M[1]				/* B = location 1 */
#define ABREG		M				/* register array */

/* Memory reference instructions */

#define IA		0100000				/* indirect address */
#define MROP		0070000				/* opcode */
#define AB		0004000				/* A/B select */
#define CP		0002000				/* current page */
#define DISP		0001777				/* page displacement */
#define PAGENO		0076000				/* page number */

/* Other instructions */

#define NMROP		0102000				/* non-mrf opcode */
#define SHFT		0000000				/* shift */
#define ASKP		0002000				/* alter/skip */
#define XTND		0100000				/* extend */
#define IOT		0102000				/* I/O */
#define HC		0001000				/* hold/clear */
#define DEVMASK		0000077				/* device mask */

/* DMA channels */

#define DMA1_STC	0100000				/* DMA - issue STC */
#define DMA1_CLC	0020000				/* DMA - issue CLC */
#define DMA2_OI		0100000				/* DMA - output/input */

struct DMA {						/* DMA channel */
	int32	cw1;					/* device select */
	int32	cw2;					/* direction, address */
	int32	cw3;					/* word count */
};

/* I/O sub-opcodes */

#define ioHLT		0				/* halt */
#define ioFLG		1				/* set/clear flag */
#define ioSFC		2				/* skip on flag clear */
#define ioSFS		3				/* skip on flag set */
#define ioMIX		4				/* merge into A/B */
#define ioLIX		5				/* load into A/B */
#define ioOTX		6				/* output from A/B */
#define ioCTL		7				/* set/clear control */

/* I/O devices - fixed assignments */

#define CPU		000				/* interrupt control */
#define OVF		001				/* overflow */
#define DMALT0		002				/* DMA 0 alternate */
#define DMALT1		003				/* DMA 1 alternate */
#define PWR		004				/* power fail */
#define PRO		005				/* parity/mem protect */
#define DMA0		006				/* DMA channel 0 */
#define DMA1		007				/* DMA channel 1 */
#define VARDEV		(DMA1 + 1)			/* start of var assign */
#define M_NXDEV		(INT_M (CPU) | INT_M (OVF) | \
			 INT_M (DMALT0) | INT_M (DMALT1))
#define M_FXDEV		(M_NXDEV | INT_M (PWR) | INT_M (PRO) | \
			 INT_M (DMA0) | INT_M (DMA1))

/* I/O devices - variable assignment defaults */

#define PTR		010				/* paper tape reader */
#define TTY		011				/* console */
#define PTP		012				/* paper tape punch */
#define CLK		013				/* clock */
#define LPT		014				/* line printer */
#define MTD		020				/* mag tape data */
#define MTC		021				/* mag tape control */
#define DPD		022				/* disk pack data */
#define DPC		023				/* disk pack control */
#define DPBD		024				/* second disk pack data */
#define DPBC		025				/* second disk pack control */

/* Dynamic device information table */

struct hpdev {
	int32	devno;					/* device number */
	int32	cmd;					/* saved command */
	int32	ctl;					/* saved control */
	int32	flg;					/* saved flag */
	int32	fbf;					/* saved flag buf */
	int32	(*iot)();				/* I/O routine */
};

/* Offsets in device information table */

#define inPTR		0				/* infotab ordinals */
#define inPTP		1
#define inTTY		2
#define inCLK		3
#define inLPT		4
#define inMTD		5
#define inMTC		6
#define inDPD		7
#define inDPC		8
#define inDPBD		9
#define inDPBC		10

#define UNIT_DEVNO	(1 << UNIT_V_UF)		/* dummy flag */

/* I/O macros */

#define INT_V(x)	((x) & 037)			/* device bit pos */
#define INT_M(x)	(1u << INT_V (x))		/* device bit mask */
#define setCMD(D)	dev_cmd[(D)/32] = dev_cmd[(D)/32] | INT_M ((D))
#define clrCMD(D)	dev_cmd[(D)/32] = dev_cmd[(D)/32] & ~INT_M (D)
#define setCTL(D)	dev_ctl[(D)/32] = dev_ctl[(D)/32] | INT_M ((D))
#define clrCTL(D)	dev_ctl[(D)/32] = dev_ctl[(D)/32] & ~INT_M (D)
#define setFBF(D)	dev_fbf[(D)/32] = dev_fbf[(D)/32] | INT_M (D)
#define clrFBF(D)	dev_fbf[(D)/32] = dev_fbf[(D)/32] & ~INT_M (D)
#define setFLG(D)	dev_flg[(D)/32] = dev_flg[(D)/32] | INT_M (D); \
			setFBF(D)
#define clrFLG(D)	dev_flg[(D)/32] = dev_flg[(D)/32] & ~INT_M (D); \
			clrFBF(D)
#define CMD(D)		((dev_cmd[(D)/32] >> INT_V (D)) & 1)
#define CTL(D)		((dev_ctl[(D)/32] >> INT_V (D)) & 1)
#define FLG(D)		((dev_flg[(D)/32] >> INT_V (D)) & 1)
#define FBF(D)		((dev_fbf[(D)/32] >> INT_V (D)) & 1)

#define IOT_V_REASON	16
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */

/* Function prototypes */

t_stat hp_setdev (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat hp_showdev (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat hp_setdev2 (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat hp_showdev2 (FILE *st, UNIT *uptr, int32 val, void *desc);
