/* pdp11_defs.h: PDP-11 simulator definitions

   Copyright (c) 1993-2001, Robert M Supnik

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

#define PDR_NR		0000002				/* non-resident ACF */
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

/* CPUERR */

#define CPUE_RED	0004				/* red stack */
#define CPUE_YEL	0010				/* yellow stack */
#define CPUE_TMO	0020				/* IO page nxm */
#define CPUE_NXM	0040				/* memory nxm */
#define CPUE_ODD	0100				/* odd address */
#define CPUE_HALT	0200				/* HALT not kernel */
#define CPUE_IMP	0374				/* implemented bits */

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

#define STOP_HALT	TRAP_V_MAX + 1			/* HALT instruction */
#define STOP_IBKPT	TRAP_V_MAX + 2			/* instruction bkpt */
#define STOP_WAIT	TRAP_V_MAX + 3			/* wait, no events */
#define STOP_VECABORT	TRAP_V_MAX + 4			/* abort vector read */
#define STOP_SPABORT	TRAP_V_MAX + 5			/* abort trap push */
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* cond error return */

/* Interrupt assignments, priority is right to left

   <3:0> =	BR7, <3> = PIR7
   <7:4> =	BR6, <7> = PIR6
   <19:8> =	BR5, <15> = PIR5
   <28:20> =	BR4, <28> = PIR4
   <29> =	PIR3
   <30> =	PIR2
   <31> =	PIR1
*/

#define INT_V_PIR7	3
#define INT_V_CLK	4
#define INT_V_DTA	5
#define INT_V_PIR6	7
#define INT_V_RK	8
#define INT_V_RL	9
#define INT_V_RX	10
#define INT_V_TM	11
#define INT_V_RP	12
#define INT_V_TS	13
#define INT_V_HK	14
#define INT_V_PIR5	19
#define INT_V_TTI	20
#define INT_V_TTO	21
#define INT_V_PTR	22
#define INT_V_PTP	23
#define INT_V_LPT	24
#define INT_V_PIR4	28
#define INT_V_PIR3	29
#define INT_V_PIR2	30
#define INT_V_PIR1	31

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
#define INT_HK		(1u << INT_V_HK)
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

#define INT_IPL7	0x00000000			/* int level masks */
#define INT_IPL6	0x0000000F
#define INT_IPL5	0x000000FF
#define INT_IPL4	0x000FFFFF
#define INT_IPL3	0x1FFFFFFF
#define INT_IPL2	0x3FFFFFFF
#define INT_IPL1	0x7FFFFFFF
#define INT_IPL0	0xFFFFFFFF

#define VEC_PIRQ	0240				/* interrupt vectors */
#define VEC_TTI		0060
#define VEC_TTO		0064
#define VEC_PTR		0070
#define VEC_PTP		0074
#define VEC_CLK		0100
#define VEC_LPT		0200
#define VEC_HK		0210
#define VEC_RK		0220
#define VEC_RL		0160
#define VEC_DTA		0214
#define VEC_TM		0224
#define VEC_TS		0224
#define VEC_RP		0254
#define VEC_RX		0264

/* CPU and FPU macros */

#define update_MM ((MMR0 & (MMR0_FREEZE + MMR0_MME)) == MMR0_MME)
#define setTRAP(name) trap_req = trap_req | (name)
#define setCPUERR(name) CPUERR = CPUERR | (name)
#define ABORT(val) longjmp (save_env, (val))
#define SP R[6]
#define PC R[7]
