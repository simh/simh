/* h316_defs.h: Honeywell 316/516 simulator definitions

   Copyright (c) 1999-2002, Robert M. Supnik

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
#define X_AMASK		(MAXMEMSIZE - 1)		/* ext address mask */
#define NX_AMASK	((MAXMEMSIZE / 2) - 1)		/* nx address mask */
#define MEM_ADDR_OK(x)	(((t_addr) (x)) < MEMSIZE)

/* Architectural constants */

#define SIGN		0100000				/* sign */
#define DMASK		0177777				/* data mask */
#define MMASK		(DMASK & ~SIGN)			/* magnitude mask */
#define XR		M[0]
#define M_CLK		061				/* clock location */
#define M_RSTINT	062				/* restrict int */
#define M_INT		063				/* int location */

/* CPU options */

#define UNIT_V_EXT	(UNIT_V_UF + 1)			/* extended mem */
#define UNIT_V_HSA	(UNIT_V_UF + 2)			/* high speed arith */
#define UNIT_EXT	(1 << UNIT_V_EXT)
#define UNIT_HSA	(1 << UNIT_V_HSA)

/* Instruction format */

#define I_M_OP		077				/* opcode */
#define I_V_OP		10
#define I_GETOP(x)	(((x) >> I_V_OP) & I_M_OP)
#define I_M_FNC		017				/* function */
#define I_V_FNC		6
#define I_GETFNC(x)	(((x) >> I_V_FNC) & I_M_FNC)
#define IA		0100000				/* indirect address */
#define IDX		0040000				/* indexed */
#define SC		0001000				/* sector */
#define DISP		0000777				/* page displacement */
#define PAGENO		0077000				/* page number */
#define INCLRA		(010 << I_V_FNC)		/* INA clear A */
#define DEVMASK		0000077				/* device mask */
#define SHFMASK		0000077				/* shift mask */

/* I/O opcodes */

#define ioOCP		0				/* output control */
#define ioSKS		1				/* skip if set */
#define ioINA		2				/* input to A */
#define ioOTA		3				/* output from A */

/* I/O devices */

#define PTR		001				/* paper tape reader */
#define PTP		002				/* paper tape punch */
#define LPT		003				/* line printer */
#define TTY		004				/* console */
#define CDR		005				/* card reader */
#define MT		010				/* mag tape data */
#define KEYS		020				/* keys (CPU) */
#define FHD		022				/* fixed head disk */
#define DMA		024				/* DMA control */
#define DP		025				/* moving head disk */
#define OPT		034				/* SKS/OCP option */

/* Interrupt flags, definitions correspond to SMK bits */

#define INT_V_CLK	0				/* clock */
#define INT_V_MPE	1				/* parity error */
#define INT_V_LPT	2				/* line printer */
#define INT_V_CDR	4				/* card reader */
#define INT_V_TTY	5				/* teletype */
#define INT_V_PTP	6				/* paper tape punch */
#define INT_V_PTR	7				/* paper tape reader */
#define INT_V_FHD	8				/* fixed head disk */
#define INT_V_DP	12				/* moving head disk */
#define INT_V_MT	15				/* mag tape */
#define INT_V_NODEF	16				/* int not deferred */
#define INT_V_ON	17				/* int on */

/* I/O macros */

#define IOT_V_REASON	17
#define IOT_V_SKIP	16
#define IOT_SKIP	(1u << IOT_V_SKIP)
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */
#define IOBADFNC(x)	(((stop_inst) << IOT_V_REASON) | (x))
#define IOSKIP(x)	(IOT_SKIP | (x))

#define INT_CLK		(1u << INT_V_CLK)
#define INT_MPE		(1u << INT_V_MPE)
#define INT_LPT		(1u << INT_V_LPT)
#define INT_CDR		(1u << INT_V_CDR)
#define INT_TTY		(1u << INT_V_TTY)
#define INT_PTP		(1u << INT_V_PTP)
#define INT_PTR		(1u << INT_V_PTR)
#define INT_FHD		(1u << INT_V_FHD)
#define INT_DP		(1u << INT_V_DP)
#define INT_MT		(1u << INT_V_MT)
#define INT_NODEF	(1u << INT_V_NODEF)
#define INT_ON		(1u << INT_V_ON)
#define INT_PENDING	(INT_ON | INT_NODEF)

#define SET_READY(x)	dev_ready = dev_ready | (x)
#define CLR_READY(x)	dev_ready = dev_ready & ~(x)
#define TST_READY(x)	((dev_ready & (x)) != 0)
#define CLR_ENABLE(x)	dev_enable = dev_enable & ~(x)
#define TST_INTREQ(x)	((dev_ready & dev_enable & (x)) != 0)
