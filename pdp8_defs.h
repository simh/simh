/* pdp8_defs.h: PDP-8 simulator definitions

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

   18-Mar-01	RMS	Added DF32 support
   15-Feb-01	RMS	Added DECtape support
   14-Apr-99	RMS	Changed t_addr to unsigned
   19-Mar-95	RMS	Added dynamic memory size
   02-May-94	RMS	Added non-existent memory handling

   The author gratefully acknowledges the help of Max Burnet, Richie Lary,
   and Bill Haygood in resolving questions about the PDP-8
*/

#include "sim_defs.h"					/* simulator defns */

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_HALT	2				/* HALT */
#define STOP_IBKPT	3				/* breakpoint */

/* Memory */

#define MAXMEMSIZE	32768				/* max memory size */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define ADDRMASK	(MAXMEMSIZE - 1)		/* address mask */
#define MEM_ADDR_OK(x)	(((t_addr) (x)) < MEMSIZE)

/* IOT subroutine return codes */

#define IOT_V_SKP	12				/* skip */
#define IOT_V_REASON	13				/* reason */
#define IOT_SKP		(1 << IOT_V_SKP)
#define IOT_REASON	(1 << IOT_V_REASON)
#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */

/* Interrupt flags

   The interrupt flags consist of three groups:

   1.	Devices with individual interrupt enables.  These record
	their interrupt requests in device_done and their enables
	in device_enable, and must occupy the low bit positions.

   2.	Devices without interrupt enables.  These record their
	interrupt requests directly in int_req, and must occupy
	the middle bit positions.

   3.	Overhead.  These exist only in int_req and must occupy the
	high bit positions.

   Because the PDP-8 does not have priority interrupts, the order
   of devices within groups does not matter.
*/

#define INT_V_START	0				/* enable start */
#define INT_V_LPT	(INT_V_START+0)			/* line printer */
#define INT_V_PTP	(INT_V_START+1)			/* tape punch */
#define INT_V_PTR	(INT_V_START+2)			/* tape reader */
#define INT_V_TTO	(INT_V_START+3)			/* terminal */
#define INT_V_TTI	(INT_V_START+4)			/* keyboard */
#define INT_V_CLK	(INT_V_START+5)			/* clock */
#define INT_V_DIRECT	(INT_V_START+6)			/* direct start */
#define INT_V_RX	(INT_V_DIRECT+0)		/* RX8E */
#define INT_V_RK	(INT_V_DIRECT+1)		/* RK8E */
#define INT_V_RF	(INT_V_DIRECT+2)		/* RF08 */
#define INT_V_DF	(INT_V_DIRECT+3)		/* DF32 */
#define INT_V_MT	(INT_V_DIRECT+4)		/* TM8E */
#define INT_V_DTA	(INT_V_DIRECT+5)		/* TC08 */
#define INT_V_PWR	(INT_V_DIRECT+6)		/* power int */
#define INT_V_UF	(INT_V_DIRECT+7)		/* user int */
#define INT_V_OVHD	(INT_V_DIRECT+8)		/* overhead start */
#define INT_V_NO_ION_PENDING (INT_V_OVHD+0)		/* ion pending */
#define INT_V_NO_CIF_PENDING (INT_V_OVHD+1)		/* cif pending */
#define INT_V_ION	(INT_V_OVHD+2)			/* interrupts on */

#define INT_LPT		(1 << INT_V_LPT)
#define INT_PTP		(1 << INT_V_PTP)
#define INT_PTR		(1 << INT_V_PTR)
#define INT_TTO		(1 << INT_V_TTO)
#define INT_TTI		(1 << INT_V_TTI)
#define INT_CLK		(1 << INT_V_CLK)
#define INT_RX		(1 << INT_V_RX)
#define INT_RK		(1 << INT_V_RK)
#define INT_RF		(1 << INT_V_RF)
#define INT_DF		(1 << INT_V_DF)
#define INT_MT		(1 << INT_V_MT)
#define INT_DTA		(1 << INT_V_DTA)
#define INT_PWR		(1 << INT_V_PWR)
#define INT_UF		(1 << INT_V_UF)
#define INT_NO_ION_PENDING (1 << INT_V_NO_ION_PENDING)
#define INT_NO_CIF_PENDING (1 << INT_V_NO_CIF_PENDING)
#define INT_ION		(1 << INT_V_ION)
#define INT_DEV_ENABLE	((1 << INT_V_DIRECT) - 1)	/* devices w/enables */
#define INT_ALL		((1 << INT_V_OVHD) - 1)		/* all interrupts */
#define INT_INIT_ENABLE	(INT_TTI+INT_TTO+INT_PTR+INT_PTP+INT_LPT)
#define INT_PENDING	(INT_ION+INT_NO_CIF_PENDING+INT_NO_ION_PENDING)
#define INT_UPDATE	((int_req & ~INT_DEV_ENABLE) | (dev_done & int_enable))
