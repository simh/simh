/* pdp11_defs.h: PDP-11 simulator definitions

   Copyright (c) 1993-2002, Robert M Supnik

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

   The author gratefully acknowledges the help of Max Burnet, Megan Gentry,
   and John Wilson in resolving questions about the PDP-11

   28-Apr-02	RMS	Clarified PDF ACF mnemonics
   22-Apr-02	RMS	Added HTRAP, BPOK maint register flags, MT_MAXFR
   06-Mar-02	RMS	Changed system type to KDJ11A
   20-Jan-02	RMS	Added multiboard DZ11 support
   09-Nov-01	RMS	Added bus map support
   07-Nov-01	RMS	Added RQDX3 support
   26-Oct-01	RMS	Added symbolic definitions for IO page
   19-Oct-01	RMS	Added DZ definitions
   15-Oct-01	RMS	Added logging capabilities
   07-Sep-01	RMS	Revised for multilevel interrupts
   01-Jun-01	RMS	Added DZ11 support
   23-Apr-01	RMS	Added RK611 support
   05-Apr-01	RMS	Added TS11/TSV05 support
   10-Feb-01	RMS	Added DECtape support
*/

#include "sim_defs.h"					/* simulator defns */
#include <setjmp.h>

/* Architectural constants */

#define STKLIM		0400				/* stack limit */
#define VASIZE		0200000				/* 2**16 */
#define VAMASK		(VASIZE - 1)			/* 2**16 - 1 */
#define INIMEMSIZE 	001000000			/* 2**18 */
#define IOPAGEBASE	017760000			/* 2**22 - 2**13 */
#define MAXMEMSIZE	020000000			/* 2**22 */
#define PAMASK		(MAXMEMSIZE - 1)		/* 2**22 - 1 */
#define MEMSIZE		(cpu_unit.capac)
#define ADDR_IS_MEM(x)	(((t_addr) (x)) < MEMSIZE)
#define DMASK		0177777

/* Protection modes */

#define KERNEL		0
#define SUPER		1
#define UNUSED		2
#define USER		3

/* I/O access modes */

#define READ		0
#define READC		1				/* read console */
#define WRITE		2
#define WRITEC		3				/* write console */
#define WRITEB		4

/* PSW */

#define	PSW_V_C		0				/* condition codes */
#define PSW_V_V		1
#define PSW_V_Z		2
#define PSW_V_N 	3
#define PSW_V_TBIT 	4				/* trace trap */
#define PSW_V_IPL	5				/* int priority */
#define PSW_V_RS	11				/* register set */
#define PSW_V_PM	12				/* previous mode */
#define PSW_V_CM	14				/* current mode */
#define PSW_RW		0174357				/* read/write bits */

/* FPS */

#define	FPS_V_C		0				/* condition codes */
#define FPS_V_V		1
#define FPS_V_Z		2
#define FPS_V_N 	3
#define FPS_V_T		5				/* truncate */
#define FPS_V_L		6				/* long */
#define FPS_V_D		7				/* double */
#define FPS_V_IC	8				/* ic err int */
#define FPS_V_IV	9				/* overflo err int */
#define FPS_V_IU	10				/* underflo err int */
#define FPS_V_IUV	11				/* undef var err int */
#define FPS_V_ID	14				/* int disable */
#define FPS_V_ER	15				/* error */

/* PIRQ */

#define PIRQ_PIR1	0001000
#define PIRQ_PIR2	0002000
#define PIRQ_PIR3	0004000
#define PIRQ_PIR4	0010000
#define PIRQ_PIR5	0020000
#define PIRQ_PIR6	0040000
#define PIRQ_PIR7	0100000
#define PIRQ_IMP	0177356				/* implemented bits */
#define PIRQ_RW		0177000				/* read/write bits */

/* MMR0 */

#define MMR0_MME	0000001				/* mem mgt enable */
#define MMR0_V_PAGE	1				/* offset to pageno */
#define MMR0_M_PAGE	077				/* mask for pageno */
#define MMR0_PAGE	(MMR0_M_PAGE << MMR0_V_PAGE)
#define MMR0_RO		0020000				/* read only error */
#define MMR0_PL		0040000				/* page lnt error */
#define MMR0_NR		0100000				/* no access error */
#define MMR0_FREEZE	0160000				/* if set, no update */
#define MMR0_IMP	0160177				/* implemented bits */
#define MMR0_RW		0160001				/* read/write bits */

/* MMR3 */

#define	MMR3_UDS	001				/* user dspace enbl */
#define MMR3_SDS	002				/* super dspace enbl */
#define MMR3_KDS	004				/* krnl dspace enbl */
#define MMR3_CSM	010				/* CSM enable */
#define MMR3_M22E	020				/* 22b mem mgt enbl */
#define MMR3_BME	040				/* DMA bus map enbl */
#define MMR3_IMP	077				/* implemented bits */
#define MMR3_RW		077				/* read/write bits */

/* PDR */

#define PDR_PRD		0000002				/* page readable */
#define PDR_PWR		0000004				/* page writeable */
#define PDR_ED		0000010				/* expansion dir */
#define PDR_W		0000100				/* written flag */
#define PDR_PLF		0077400				/* page lnt field */
#define PDR_IMP		0177516				/* implemented bits */
#define PDR_RW		0177416				/* read/write bits */

/* Virtual address */

#define VA_DF		0017777				/* displacement */
#define VA_BN		0017700				/* block number */
#define VA_V_APF	13				/* offset to APF */
#define VA_V_DS		16				/* offset to space */
#define VA_V_MODE	17				/* offset to mode */
#define VA_DS		(1u << VA_V_DS)			/* data space flag */

/* Unibus map (if present) */

#define UBM_LNT_LW	32				/* size in LW */
#define UBM_V_PN	13				/* page number */
#define UBM_M_PN	037
#define UBM_V_OFF	0				/* offset */
#define UBM_M_OFF	017777
#define UBM_GETPN(x)	(((x) >> UBM_V_PN) & UBM_M_PN)
#define UBM_GETOFF(x)	((x) & UBM_M_OFF)

/* CPUERR */

#define CPUE_RED	0004				/* red stack */
#define CPUE_YEL	0010				/* yellow stack */
#define CPUE_TMO	0020				/* IO page nxm */
#define CPUE_NXM	0040				/* memory nxm */
#define CPUE_ODD	0100				/* odd address */
#define CPUE_HALT	0200				/* HALT not kernel */
#define CPUE_IMP	0374				/* implemented bits */

/* Maintenance register */

#define MAINT_V_UQ	9				/* Q/U flag */
#define MAINT_Q		(0 << MAINT_V_UQ)		/* Qbus */
#define MAINT_U		(1 << MAINT_V_UQ)
#define MAINT_V_FPA	8				/* FPA flag */
#define MAINT_NOFPA	(0 << MAINT_V_FPA)
#define MAINT_FPA	(1 << MAINT_V_FPA)
#define MAINT_V_TYP	4				/* system type */
#define MAINT_KDJ	(1 << MAINT_V_TYP)		/* KDJ11A */
#define MAINT_V_HTRAP	3				/* trap 4 on HALT */
#define MAINT_HTRAP	(1 << MAINT_V_HTRAP)
#define MAINT_V_BPOK	0				/* power OK */
#define MAINT_BPOK	(1 << MAINT_V_BPOK)

/* Floating point accumulators */

struct fpac {
	unsigned int32	l;				/* low 32b */
	unsigned int32	h;				/* high 32b */
};
typedef struct fpac fpac_t;

/* Device CSRs */

#define CSR_V_GO	0				/* go */
#define CSR_V_IE	6				/* interrupt enable */
#define CSR_V_DONE	7				/* done */
#define CSR_V_BUSY	11				/* busy */
#define CSR_V_ERR	15				/* error */
#define CSR_GO		(1u << CSR_V_GO)
#define CSR_IE		(1u << CSR_V_IE)
#define CSR_DONE	(1u << CSR_V_DONE)
#define CSR_BUSY	(1u << CSR_V_BUSY)
#define CSR_ERR		(1u << CSR_V_ERR)

/* Trap masks, descending priority order, following J-11
   An interrupt summary bit is kept with traps, to minimize overhead
*/

#define TRAP_V_RED	0				/* red stk abort  4 */
#define TRAP_V_ODD	1				/* odd address	  4 */
#define TRAP_V_MME	2				/* mem mgt	250 */
#define TRAP_V_NXM	3				/* nx memory	  4 */
#define TRAP_V_PAR	4				/* parity err	114 */
#define TRAP_V_PRV	5				/* priv inst	  4 */
#define TRAP_V_ILL	6				/* illegal inst	 10 */
#define TRAP_V_BPT	7				/* BPT		 14 */
#define TRAP_V_IOT	8				/* IOT		 20 */
#define TRAP_V_EMT	9				/* EMT		 30 */
#define TRAP_V_TRAP	10				/* TRAP		 34 */
#define TRAP_V_TRC	11				/* T bit	 14 */
#define TRAP_V_YEL	12				/* stack	  4 */
#define TRAP_V_PWRFL	13				/* power fail	 24 */
#define TRAP_V_FPE	14				/* fpe		244 */
#define TRAP_V_MAX	15				/* intr = max trp # */
#define TRAP_RED	(1u << TRAP_V_RED)
#define TRAP_ODD	(1u << TRAP_V_ODD)
#define TRAP_MME	(1u << TRAP_V_MME)
#define TRAP_NXM	(1u << TRAP_V_NXM)
#define TRAP_PAR	(1u << TRAP_V_PAR)
#define TRAP_PRV	(1u << TRAP_V_PRV)
#define TRAP_ILL	(1u << TRAP_V_ILL)
#define TRAP_BPT	(1u << TRAP_V_BPT)
#define TRAP_IOT	(1u << TRAP_V_IOT)
#define TRAP_EMT	(1u << TRAP_V_EMT)
#define TRAP_TRAP	(1u << TRAP_V_TRAP)
#define TRAP_TRC	(1u << TRAP_V_TRC)
#define TRAP_YEL	(1u << TRAP_V_YEL)
#define TRAP_PWRFL	(1u << TRAP_V_PWRFL)
#define TRAP_FPE	(1u << TRAP_V_FPE)
#define TRAP_INT	(1u << TRAP_V_MAX)
#define TRAP_ALL	((1u << TRAP_V_MAX) - 1)	/* all traps */

#define VEC_RED		0004				/* trap vectors */
#define VEC_ODD		0004
#define VEC_MME		0250
#define VEC_NXM		0004
#define VEC_PAR		0114
#define VEC_PRV		0004
#define VEC_ILL		0010
#define VEC_BPT		0014
#define VEC_IOT		0020
#define VEC_EMT		0030
#define VEC_TRAP	0034
#define VEC_TRC		0014
#define VEC_YEL		0004
#define VEC_PWRFL	0024
#define VEC_FPE		0244

/* Simulator stop codes; codes 1:TRAP_V_MAX correspond to traps 0:TRAPMAX-1 */

#define STOP_HALT	(TRAP_V_MAX + 1)		/* HALT instruction */
#define STOP_IBKPT	(TRAP_V_MAX + 2)		/* instruction bkpt */
#define STOP_WAIT	(TRAP_V_MAX + 3)		/* wait, no events */
#define STOP_VECABORT	(TRAP_V_MAX + 4)		/* abort vector read */
#define STOP_SPABORT	(TRAP_V_MAX + 5)		/* abort trap push */
#define STOP_RQ		(TRAP_V_MAX + 6)		/* RQDX3 panic */
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* cond error return */

/* Timers */

#define TMR_CLK		0				/* line clock */
#define TMR_KWP		1				/* KW11P */

/* IO parameters */

#define DZ_MUXES	4				/* max # of muxes */
#define DZ_LINES	8				/* lines per mux */
#define MT_MAXFR	(1 << 16)			/* magtape max rec */

/* Device information block */

struct pdp_dib {
	uint32		enb;				/* enabled */
	uint32		ba;				/* base addr */
	uint32		lnt;				/* length */
	t_stat		(*rd)(int32 *dat, int32 ad, int32 md);
	t_stat		(*wr)(int32 dat, int32 ad, int32 md);  };

typedef struct pdp_dib DIB;

/* I/O page layout */

#define IOBA_DZ		(IOPAGEBASE + 000100)		/* DZ11 */
#define IOLN_DZ		010
#define IOBA_UBM	(IOPAGEBASE + 010200)		/* Unibus map */
#define IOLN_UBM	(UBM_LNT_LW * sizeof (int32))
#define IOBA_RQ		(IOPAGEBASE + 012150)		/* RQDX3 */
#define IOLN_RQ		004
#define IOBA_APR	(IOPAGEBASE + 012200)		/* APRs */
#define IOLN_APR	0200
#define IOBA_MMR3	(IOPAGEBASE + 012516)		/* MMR3 */
#define IOLN_MMR3	002
#define IOBA_TM		(IOPAGEBASE + 012520)		/* TM11 */
#define IOLN_TM		014
#define IOBA_TS		(IOPAGEBASE + 012520)		/* TS11 */
#define IOLN_TS		004
#define IOBA_RL		(IOPAGEBASE + 014400)		/* RL11 */
#define IOLN_RL		012
#define IOBA_RP		(IOPAGEBASE + 016700)		/* RP/RM */
#define IOLN_RP		054
#define IOBA_RX		(IOPAGEBASE + 017170)		/* RX11 */
#define IOLN_RX		004
#define IOBA_TC		(IOPAGEBASE + 017340)		/* TC11 */
#define IOLN_TC		012
#define IOBA_RK		(IOPAGEBASE + 017400)		/* RK11 */
#define IOLN_RK		020
#define IOBA_RK6	(IOPAGEBASE + 017440)		/* RK611 */
#define IOLN_RK6	040
#define IOBA_LPT	(IOPAGEBASE + 017514)		/* LP11 */
#define IOLN_LPT	004
#define IOBA_CLK	(IOPAGEBASE + 017546)		/* KW11L */
#define IOLN_CLK	002
#define IOBA_PT		(IOPAGEBASE + 017550)		/* PC11 */
#define IOLN_PT		010
#define IOBA_TT		(IOPAGEBASE + 017560)		/* DL11 */
#define IOLN_TT		010
#define IOBA_SRMM	(IOPAGEBASE + 017570)		/* SR, MMR0-2 */
#define IOLN_SRMM	010
#define IOBA_APR1	(IOPAGEBASE + 017600)		/* APRs */
#define IOLN_APR1	0100
#define IOBA_CPU	(IOPAGEBASE + 017740)		/* CPU reg */
#define IOLN_CPU	040

/* Interrupt assignments; within each level, priority is right to left */

#define IPL_HLVL	8				/* # int levels */

#define INT_V_PIR7	0				/* BR7 */

#define INT_V_CLK	0				/* BR6 */
#define INT_V_DTA	1
#define INT_V_PIR6	2

#define INT_V_RK	0				/* BR5 */
#define INT_V_RL	1
#define INT_V_RX	2
#define INT_V_TM	3
#define INT_V_RP	4
#define INT_V_TS	5
#define INT_V_HK	6
#define INT_V_RQ	7
#define INT_V_DZRX	8
#define INT_V_DZTX	9
#define INT_V_PIR5	10

#define INT_V_TTI	0				/* BR4 */
#define INT_V_TTO	1
#define INT_V_PTR	2
#define INT_V_PTP	3
#define INT_V_LPT	4
#define INT_V_PIR4	5

#define INT_V_PIR3	0				/* BR3 */
#define INT_V_PIR2	0				/* BR2 */
#define INT_V_PIR1	0				/* BR1 */

#define INT_PIR7	(1u << INT_V_PIR7)
#define INT_CLK		(1u << INT_V_CLK)
#define INT_DTA		(1u << INT_V_DTA)
#define INT_PIR6	(1u << INT_V_PIR6)
#define INT_RK		(1u << INT_V_RK)
#define INT_RL		(1u << INT_V_RL)
#define INT_RX		(1u << INT_V_RX)
#define INT_TM		(1u << INT_V_TM)
#define INT_RP		(1u << INT_V_RP)
#define INT_TS		(1u << INT_V_TS)
#define INT_RK6		(1u << INT_V_HK)
#define INT_RQ		(1u << INT_V_RQ)
#define INT_DZRX	(1u << INT_V_DZRX)
#define INT_DZTX	(1u << INT_V_DZTX)
#define INT_PIR5	(1u << INT_V_PIR5)
#define INT_PTR		(1u << INT_V_PTR)
#define INT_PTP		(1u << INT_V_PTP)
#define INT_TTI		(1u << INT_V_TTI)
#define INT_TTO		(1u << INT_V_TTO)
#define INT_LPT		(1u << INT_V_LPT)
#define INT_PIR4	(1u << INT_V_PIR4)
#define INT_PIR3	(1u << INT_V_PIR3)
#define INT_PIR2	(1u << INT_V_PIR2)
#define INT_PIR1	(1u << INT_V_PIR1)

#define IPL_CLK		6				/* int pri levels */
#define IPL_DTA		6
#define IPL_RK		5
#define IPL_RL		5
#define IPL_RX		5
#define IPL_TM		5
#define IPL_RP		5
#define IPL_TS		5
#define IPL_RK6		5
#define IPL_RQ		5
#define IPL_DZRX	5
#define IPL_DZTX	5
#define IPL_PTR		4
#define IPL_PTP		4
#define IPL_TTI		4
#define IPL_TTO		4
#define IPL_LPT		4

#define IPL_PIR7	7
#define IPL_PIR6	6
#define IPL_PIR5	5
#define IPL_PIR4	4
#define IPL_PIR3	3
#define IPL_PIR2	2
#define IPL_PIR1	1

#define VEC_Q		0000				/* vector base */
#define VEC_PIRQ	0240
#define VEC_TTI		0060
#define VEC_TTO		0064
#define VEC_PTR		0070
#define VEC_PTP		0074
#define VEC_CLK		0100
#define VEC_RQ		0154
#define VEC_RL		0160
#define VEC_LPT		0200
#define VEC_RK6		0210
#define VEC_RK		0220
#define VEC_DTA		0214
#define VEC_TM		0224
#define VEC_TS		0224
#define VEC_RP		0254
#define VEC_RX		0264
#define VEC_DZRX	0310
#define VEC_DZTX	0314

/* Interrupt macros */

#define IREQ(dv)	int_req[IPL_##dv]
#define SET_INT(dv)	int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)	int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)

/* CPU and FPU macros */

#define update_MM	((MMR0 & (MMR0_FREEZE + MMR0_MME)) == MMR0_MME)
#define setTRAP(name)	trap_req = trap_req | (name)
#define setCPUERR(name)	CPUERR = CPUERR | (name)
#define ABORT(val)	longjmp (save_env, (val))
#define SP R[6]
#define PC R[7]

/* Logging */

#define LOG_CPU_I	00000001
#define LOG_RP		00000010
#define LOG_TS		00000020
#define LOG_RQ		00000040
#define LOG_TC_MS	00000100
#define LOG_TC_RW	00000200
#define LOG_TC_RA	00000400
#define LOG_TC_BL	00001000

#define DBG_LOG(x)	(sim_log && (cpu_log & (x)))

/* Function prototypes */

#define QB		0				/* Q22 native */
#define UB		1				/* Unibus */

t_bool Map_Addr (t_addr qa, t_addr *ma);
int32 Map_ReadB (t_addr ba, int32 bc, uint8 *buf, t_bool ub);
int32 Map_ReadW (t_addr ba, int32 bc, uint16 *buf, t_bool ub);
int32 Map_WriteB (t_addr ba, int32 bc, uint8 *buf, t_bool ub);
int32 Map_WriteW (t_addr ba, int32 bc, uint16 *buf, t_bool ub);
t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat set_enbdis (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool dev_conflict (uint32 nba, DIB *curr);
