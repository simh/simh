/* id4_defs.h: Interdata 4 simulator definitions 

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

   07-Oct-00	RMS	Overhauled I/O subsystem
   14-Apr-99	RMS	Changed t_addr to unsigned

   The author gratefully acknowledges the help of Carl Friend, who provided
   key documents about the Interdata 4.   Questions answered to date:

	1. Do device interrupt enables mask interrupt requests or prevent
	   interrupt requests?  A: Mask interrupt requests.
	2. Does SLHA set C from shift out of bit <0> or bit <1>?  A: From <1>.
	3. What is the limit on device numbers?  A: 256.  How big must the
	   interrupt request and enable arrays be?  A: 8 x 32b.
	4. Does BXH subtract or add the second argument?
	5. Do BXH and BXLE do a logical or arithmetic compare?  A: Logical.
	6. Do ACH and SCH produce normal GL codes, or do they take into account
	   prior GL codes?
*/

#include "sim_defs.h"					/* simulator defns */

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_HALT	2				/* HALT */
#define STOP_IBKPT	3				/* breakpoint */
#define STOP_WAIT	4				/* wait */

/* Memory */

#define MAXMEMSIZE	65536				/* max memory size */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define AMASK		(MAXMEMSIZE - 1)		/* address mask */
#define MEM_ADDR_OK(x)	(((t_addr) (x)) < MEMSIZE)
#define ReadW(x)	M[(x) >> 1]
#define WriteW(x,d)	if (MEM_ADDR_OK (x)) M[(x) >> 1] = d
#define ReadB(x)	((M[(x) >> 1] >> (((x) & 1)? 0: 8)) & 0xFF)
#define WriteB(x,d)	if (MEM_ADDR_OK (x)) M[(x) >> 1] = \
				(((x) & 1)? ((M[(x) >> 1] & ~0xFF) | (d)): \
				((M[(x) >> 1] & 0xFF) | ((d) << 8)))

/* Architectural constants */

#define SIGN		0x8000				/* sign bit */
#define DMASK		0xFFFF				/* data mask */
#define MAGMASK		0x7FFF				/* magnitude mask */

#define OP_4B		0x40				/* 2 byte vs 4 byte */

#define CC_C		0x8				/* carry */
#define CC_V		0x4				/* overflow */
#define CC_G		0x2				/* greater than */
#define CC_L		0x1				/* less than */
#define CC_MASK		(CC_C | CC_V | CC_G | CC_L)

#define PSW_WAIT	0x8000				/* wait */	
#define PSW_EXI		0x4000				/* ext intr enable */
#define PSW_MCI		0x2000				/* machine check enable */
#define PSW_DFI		0x1000				/* divide fault enable */
#define PSW_FDI		0x0400				/* flt divide fault enable */

#define FDOPSW		0x28				/* flt div fault old PSW */
#define FDNPSW		0x2C				/* flt div fault new PSW */
#define ILOPSW		0x30				/* illegal op old PSW */
#define ILNPSW		0x34				/* illegal op new PSW */
#define MCOPSW		0x38				/* machine check old PSW */
#define MCNPSW		0x3C				/* machine check new PSW */
#define EXOPSW		0x40				/* external intr old PSW */
#define EXNPSW		0x44				/* external intr new PSW */
#define IDOPSW		0x48				/* int div fault old PSW */
#define IDNPSW		0x4C				/* int div fault new PSW */

/* I/O operations */

#define IO_ADR		0x0				/* address select */
#define IO_RD		0x1				/* read */
#define IO_WD		0x2				/* write */
#define IO_OC		0x3				/* output command */
#define IO_SS		0x5				/* sense status */

/* Device return codes: data byte is <7:0> */

#define IOT_V_EXM	8				/* set V flag */
#define IOT_EXM		(1u << IOT_V_EXM)
#define IOT_V_REASON	9				/* set reason */

/* Device command byte */

#define CMD_V_INT	6				/* interrupt control */
#define CMD_M_INT	0x3
#define CMD_IENB	 1				/* enable */
#define CMD_IDIS	 2				/* disable */
#define CMD_ICOM	 3				/* complement */
#define CMD_GETINT(x)	(((x) >> CMD_V_INT) & CMD_M_INT)

/* Device status byte */

#define STA_BSY		0x8				/* busy */
#define STA_EX		0x4				/* examine status */
#define STA_EOM		0x2				/* end of medium */
#define STA_DU		0x1				/* device unavailable */

/* Device numbers */

#define DEV_LOW		0x01				/* lowest intr dev */
#define DEV_MAX		0xFF				/* highest intr dev */
#define DEVNO		(DEV_MAX + 1)			/* number of devices */
#define INTSZ		((DEVNO + 31) / 32)		/* number of interrupt words */
#define DS		0x01				/* display and switches */
#define TT		0x02				/* teletype */
#define PT		0x03				/* paper tape */
#define CD		0x04				/* card reader */

/* I/O macros */

#define INT_V(d)	(1u << ((d) & 0x1F))
#define SET_INT(d)	int_req[(d)/32] = int_req[(d)/32] | INT_V (d)
#define CLR_INT(d)	int_req[(d)/32] = int_req[(d)/32] & ~INT_V (d)
#define SET_ENB(d)	int_enb[(d)/32] = int_enb[(d)/32] | INT_V (d)
#define COM_ENB(d)	int_enb[(d)/32] = int_enb[(d)/32] ^ INT_V (d)
#define CLR_ENB(d)	int_enb[(d)/32] = int_enb[(d)/32] & ~INT_V (d)

#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */
