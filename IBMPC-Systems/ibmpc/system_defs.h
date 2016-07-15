/*  system_defs.h: IBM PC simulator definitions

    Copyright (c) 2010, William A. Beech

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
        William A. Beech BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

        11 Jul 16 - Original file.
*/

#include <stdio.h>
#include <ctype.h>
#include "sim_defs.h"		        /* simulator defns */

/* set the base I/O address and device count for the 8237 */
#define I8237_BASE_0    0x000
#define I8237_NUM       1

/* set the base I/O address and device count for the 8253 */
#define I8253_BASE_0    0x040
#define I8253_BASE_1    0x100
#define I8253_NUM       2

/* set the base I/O address and device count for the 8255 */
#define I8255_BASE_0    0x060
#define I8255_NUM       1

/* set the base I/O address and device count for the 8259 */
#define I8259_BASE_0    0x020
#define I8259_NUM       1

/* set the base I/O address for the NMI mask */
#define NMI_BASE        0x0A0

/* set the base I/O address and device count for the DMA page registers */
#define DMAPAG_BASE_0   0x080
#define DMAPAG_BASE_1   0x081
#define DMAPAG_BASE_2   0x082
#define DMAPAG_BASE_3   0x083
#define DMAPAG_NUM      4

/* set the base and size for the EPROM on the IBM PC */
#define ROM_BASE        0xFE000
#define ROM_SIZE        0x02000

/* set the base and size for the RAM on the IBM PC */
#define RAM_BASE        0x00000
#define RAM_SIZE        0x40000

/* set INTR for CPU on the 8088 */
#define INT_R           INT_1  

/* xtbus interrupt definitions */

#define INT_0   0x01
#define INT_1   0x02
#define INT_2   0x04
#define INT_3   0x08
#define INT_4   0x10
#define INT_5   0x20
#define INT_6   0x40
#define INT_7   0x80

/* Memory */

#define ADDRMASK16          0xFFFF
#define ADDRMASK20          0xFFFFF
#define MAXMEMSIZE20        0xFFFFF	        /* 8080 max memory size */

#define MEMSIZE		    (i8088_unit.capac)  /* 8088 actual memory size */
#define ADDRMASK	    (MAXMEMSIZE - 1)    /* 8088 address mask */
#define MEM_ADDR_OK(x)	    (((uint32) (x)) < MEMSIZE)

/* debug definitions */

#define DEBUG_flow      0x0001
#define DEBUG_read      0x0002
#define DEBUG_write     0x0004
#define DEBUG_level1    0x0008
#define DEBUG_level2    0x0010
#define DEBUG_reg       0x0020
#define DEBUG_asm       0x0040
#define DEBUG_all       0xFFFF

/* Simulator stop codes */

#define STOP_RSRV	1			    /* must be 1 */
#define STOP_HALT	2			    /* HALT */
#define STOP_IBKPT	3		            /* breakpoint */
#define STOP_OPCODE	4                           /* Invalid Opcode */
#define STOP_IO 	5                           /* I/O error */
#define STOP_MEM 	6                           /* Memory error */

