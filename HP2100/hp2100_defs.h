/* hp2100_defs.h: HP 2100 simulator definitions

   Copyright (c) 1993-2004, Robert M. Supnik

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

   30-Dec-04	JDB	Added IBL_DS_HEAD head number mask
   19-Nov-04	JDB	Added STOP_OFFLINE, STOP_PWROFF stop codes
   25-Apr-04	RMS	Added additional IBL definitions
			Added DMA EDT I/O pseudo-opcode
   25-Apr-03	RMS	Revised for extended file support
   24-Oct-02	RMS	Added indirect address interrupt
   08-Feb-02	RMS	Added DMS definitions
   01-Feb-02	RMS	Added terminal multiplexor support
   16-Jan-02	RMS	Added additional device support
   30-Nov-01	RMS	Added extended SET/SHOW support
   15-Oct-00	RMS	Added dynamic device numbers
   14-Apr-99	RMS	Changed t_addr to unsigned

   The author gratefully acknowledges the help of Jeff Moffat in answering
   questions about the HP2100; and of Dave Bryan in adding features and
   correcting errors throughout the simulator.
*/

#include "sim_defs.h"					/* simulator defns */

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_IODV	2				/* must be 2 */
#define STOP_HALT	3				/* HALT */
#define STOP_IBKPT	4				/* breakpoint */
#define STOP_IND	5				/* indirect loop */
#define STOP_INDINT	6				/* indirect intr */
#define STOP_NOCONN	7				/* no connection */
#define STOP_OFFLINE	8				/* device offline */
#define STOP_PWROFF	9				/* device powered off */

#define ABORT_PRO	1				/* protection abort */

/* Memory */

#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define MEM_ADDR_OK(x)	(((uint32) (x)) < MEMSIZE)
#define VA_N_SIZE	15				/* virtual addr size */
#define VASIZE		(1 << VA_N_SIZE)
#define VAMASK		(VASIZE - 1)			/* virt addr mask */
#define PA_N_SIZE	20				/* phys addr size */
#define PASIZE		(1 << PA_N_SIZE)
#define PAMASK		(PASIZE - 1)			/* phys addr mask */

/* Architectural constants */

#define SIGN32		020000000000			/* 32b sign */
#define SIGN		0100000				/* 16b sign */
#define DMASK		0177777				/* 16b data mask */
#define AR		ABREG[0]			/* A = reg 0 */
#define BR		ABREG[1]			/* B = reg 1 */
#define SEXT(x)		((int32) (((x) & SIGN)? ((x) | ~DMASK): ((x) & DMASK)))

/* Memory reference instructions */

#define I_IA		0100000				/* indirect address */
#define I_AB		0004000				/* A/B select */
#define I_CP		0002000				/* current page */
#define I_DISP		0001777				/* page displacement */
#define I_PAGENO	0076000				/* page number */

/* Other instructions */

#define I_NMRMASK	0172000				/* non-mrf opcode */
#define I_SRG		0000000				/* shift */
#define I_ASKP		0002000				/* alter/skip */
#define I_EXTD		0100000				/* extend */
#define I_IO		0102000				/* I/O */
#define I_CTL		0004000				/* CTL on/off */
#define I_HC		0001000				/* hold/clear */
#define I_DEVMASK	0000077				/* device mask */
#define I_GETIOOP(x)	(((x) >> 6) & 07)		/* I/O sub op */

/* DMA channels */

#define DMA1_STC	0100000				/* DMA - issue STC */
#define DMA1_CLC	0020000				/* DMA - issue CLC */
#define DMA2_OI		0100000				/* DMA - output/input */

struct DMA {						/* DMA channel */
	uint32	cw1;					/* device select */
	uint32	cw2;					/* direction, address */
	uint32	cw3;					/* word count */
};

/* Memory management */

#define VA_N_OFF	10				/* offset width */
#define VA_M_OFF	((1 << VA_N_OFF) - 1)		/* offset mask */
#define VA_GETOFF(x)	((x) & VA_M_OFF)
#define VA_N_PAG	(VA_N_SIZE - VA_N_OFF)		/* page width */
#define VA_V_PAG	(VA_N_OFF)
#define VA_M_PAG	((1 << VA_N_PAG) - 1)
#define VA_GETPAG(x)	(((x) >> VA_V_PAG) & VA_M_PAG)

/* Maps */

#define MAP_NUM		4				/* num maps */
#define MAP_LNT		(1 << VA_N_PAG)			/* map length */
#define MAP_MASK	((MAP_NUM * MAP_LNT) - 1)
#define SMAP		0				/* system map */
#define UMAP		(SMAP + MAP_LNT)		/* user map */
#define PAMAP		(UMAP + MAP_LNT)		/* port A map */
#define PBMAP		(PAMAP + MAP_LNT)		/* port B map */

/* DMS map entries */

#define MAP_V_RPR	15				/* read prot */
#define MAP_V_WPR	14				/* write prot */
#define RD		(1 << MAP_V_RPR)
#define WR		(1 << MAP_V_WPR)
#define MAP_MBZ		0036000				/* must be zero */
#define MAP_N_PAG	(PA_N_SIZE - VA_N_OFF)		/* page width */
#define MAP_V_PAG	(VA_N_OFF)
#define MAP_M_PAG	((1 << MAP_N_PAG) - 1)
#define MAP_GETPAG(x)	(((x) & MAP_M_PAG) << MAP_V_PAG)

/* Map status register */

#define MST_ENBI	0100000				/* mem enb @ int */
#define MST_UMPI	0040000				/* usr map @ int */
#define MST_ENB		0020000				/* mem enb */
#define MST_UMP		0010000				/* usr map */
#define MST_PRO		0004000				/* protection */
#define MST_FLT		0002000				/* fence comp */
#define MST_FENCE	0001777				/* base page fence */

/* Map violation register */

#define MVI_V_RPR	15				/* must be same as */
#define MVI_V_WPR	14				/* MAP_V_xPR */
#define MVI_RPR		(1 << MVI_V_RPR)		/* rd viol */
#define MVI_WPR		(1 << MVI_V_WPR)		/* wr viol */
#define MVI_BPG		0020000				/* base page viol */
#define MVI_PRV		0010000				/* priv viol */
#define MVI_MEB		0000200				/* me bus enb @ viol */
#define MVI_MEM		0000100				/* mem enb @ viol */
#define MVI_UMP		0000040				/* usr map @ viol */
#define MVI_PAG		0000037				/* pag sel */

/* Timers */

#define TMR_CLK		0				/* clock */
#define TMR_MUX		1				/* multiplexor */

/* I/O sub-opcodes */

#define ioHLT		0				/* halt */
#define ioFLG		1				/* set/clear flag */
#define ioSFC		2				/* skip on flag clear */
#define ioSFS		3				/* skip on flag set */
#define ioMIX		4				/* merge into A/B */
#define ioLIX		5				/* load into A/B */
#define ioOTX		6				/* output from A/B */
#define ioCTL		7				/* set/clear control */
#define ioEDT		8				/* DMA: end data transfer */

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

#define PTR		010				/* 12597A-002 paper tape reader */
#define TTY		011				/* 12531C teleprinter */
#define PTP		012				/* 12597A-005 paper tape punch */
#define CLK		013				/* 12539C time-base generator */
#define LPS		014				/* 12653A line printer */
#define LPT		015				/* 12845A line printer */
#define MTD		020				/* 12559A data */
#define MTC		021				/* 12559A control */
#define DPD		022				/* 12557A data */
#define DPC		023				/* 12557A control */
#define DQD		024				/* 12565A data */
#define DQC		025				/* 12565A control */
#define DRD		026				/* 12610A data */
#define DRC		027				/* 12610A control */
#define MSD		030				/* 13181A data */
#define MSC		031				/* 13181A control */
#define IPLI		032				/* 12566B link in */
#define IPLO		033				/* 12566B link out */
#define DS		034				/* 13037A control */
#define MUXL		040				/* 12920A lower data */
#define MUXU		041				/* 12920A upper data */
#define MUXC		042				/* 12920A control */

/* IBL assignments */

#define IBL_V_SEL	14				/* ROM select */
#define IBL_M_SEL	03
#define IBL_PTR		0000000				/* PTR */
#define IBL_DP		0040000				/* disk: DP */
#define IBL_DQ		0060000				/* disk: DQ */
#define IBL_MS		0100000				/* option 0: MS */
#define IBL_DS		0140000				/* option 1: DS */
#define IBL_MAN		0010000				/* RPL/man boot */
#define IBL_V_DEV	6				/* dev in <11:6> */
#define IBL_OPT		0000070				/* options in <5:3> */
#define IBL_DP_REM	0000001				/* DP removable */
#define IBL_DS_HEAD	0000003				/* DS head number */
#define IBL_LNT		64				/* boot length */
#define IBL_MASK	(IBL_LNT - 1)			/* boot length mask */
#define IBL_DPC		(IBL_LNT - 2)			/* DMA ctrl word */
#define IBL_END		(IBL_LNT - 1)			/* last location */

/* Dynamic device information table */

struct hp_dib {
	uint32	devno;					/* device number */
	uint32	cmd;					/* saved command */
	uint32	ctl;					/* saved control */
	uint32	flg;					/* saved flag */
	uint32	fbf;					/* saved flag buf */
	uint32	srq;					/* saved svc req */
	int32	(*iot)(int32 op, int32 ir, int32 dat);	/* I/O routine */
};

typedef struct hp_dib DIB;

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
#define setFSR(D)	dev_flg[(D)/32] = dev_flg[(D)/32] | INT_M (D); \
			setFBF(D); setSRQ(D)
#define clrFSR(D)	dev_flg[(D)/32] = dev_flg[(D)/32] & ~INT_M (D); \
			clrFBF(D); clrSRQ(D)
#define setSRQ(D)	dev_srq[(D)/32] = dev_srq[(D)/32] | INT_M ((D))
#define clrSRQ(D)	dev_srq[(D)/32] = dev_srq[(D)/32] & ~INT_M (D)
#define CMD(D)		((dev_cmd[(D)/32] >> INT_V (D)) & 1)
#define CTL(D)		((dev_ctl[(D)/32] >> INT_V (D)) & 1)
#define FLG(D)		((dev_flg[(D)/32] >> INT_V (D)) & 1)
#define FBF(D)		((dev_fbf[(D)/32] >> INT_V (D)) & 1)
#define SRQ(D)		((dev_srq[(D)/32] >> INT_V (D)) & 1)

#define IOT_V_REASON	16
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */

/* Function prototypes */

t_stat ibl_copy (const uint16 pboot[IBL_LNT], int32 dev);
t_stat hp_setdev (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat hp_showdev (FILE *st, UNIT *uptr, int32 val, void *desc);
void hp_enbdis_pair (DEVICE *ccp, DEVICE *dcp);
