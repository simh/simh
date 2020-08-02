/*  system_defs.h: Intel iSBC simulator definitions

    Copyright (c) 2017, William A. Beech

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

    28 Oct 17 - Original file.
*/

#include <stdio.h>
#include <ctype.h>
#include "sim_defs.h"                   /* simulator defns */

#define IPC     0
#define SET_XACK(VAL)   (xack = VAL)

#define I3214_NUM       0

/* set the base for the DBB ports */
#define DBB_BASE        0xC0

/* set the base I/O address for the 8255 */
#define I8255_BASE_0    0xE4
#define I8255_BASE_1    0xE8
#define I8255_NUM       2

/* set the base I/O address for the 8253 */
#define I8253_BASE      0xF0
#define I8253_NUM       1

/* set the base I/O address for the 8251 */
#define I8251_BASE_0    0xF4
#define I8251_BASE_1    0xF6
#define I8251_NUM       2

/* set the base I/O address for the 8259 */
#define I8259_BASE_0    0xFA
#define I8259_BASE_1    0xFC
#define I8259_NUM       2

/* set the base I/O address for the IPC control port */
#define ICONT_BASE      0xFF

/* set the base and size for the EPROM on the MDS 210 */
#define ROM_BASE        0x0000
#define ROM_SIZE        0x0FFF
#define ROM_DISABLE     1
#define EPROM_NUM       1

/* set the base and size for the RAM on the MDS 210 */
#define RAM_BASE        0x0000
#define RAM_SIZE        0x7FFF

//board definitions for the multibus
/* set the base I/O address for the iSBC 201 */
#define SBC201_BASE     0x78
#define SBC201_INT      INT_1
#define SBC201_NUM      0

/* set the base I/O address for the iSBC 202 */
#define SBC202_BASE     0x78
#define SBC202_INT      INT_1
#define SBC202_NUM      0

/* set the base I/O address for the iSBC 206 */
#define SBC206_BASE     0x68
#define SBC206_INT      INT_1
#define SBC206_NUM      0

/* set the base I/O address for the iSBC 208 */
#define SBC208_BASE     0x40
#define SBC208_INT      INT_1
#define SBC208_NUM      0

/* set the base for the zx-200a disk controller */
#define ZX200A_BASE     0x78
#define ZX200A_INT      INT_1
#define ZX200A_NUM      0

/* set the base and size for the iSBC 464 ROM */
#define SBC464_BASE     0xA800
#define SBC464_SIZE     0x47FF
#define SBC464_NUM      1

/* set the base and size for the iSBC 064 RAM */
#define SBC064_BASE     0x8000
#define SBC064_SIZE     0x7FFF
#define SBC064_NUM      0

/* set INTR for CPU */
#define INTR            INT_1

/* multibus interrupt definitions */

#define INT_0   0x01
#define INT_1   0x02
#define INT_2   0x04
#define INT_3   0x08
#define INT_4   0x10
#define INT_5   0x20
#define INT_6   0x40
#define INT_7   0x80

/* CPU interrupts definitions */

#define INT_R   0x200
#define I75     0x40
#define I65     0x20
#define I55     0x10

/* Memory */

#define MAXMEMSIZE          0x0FFFF             /* 8080 max memory size */
#define MEMSIZE             (i8080_unit.capac)  /* 8080 actual memory size */
#define ADDRMASK            (MAXMEMSIZE)        /* 8080 address mask */
#define MEM_ADDR_OK(x)      (((uint16) (x)) <= MEMSIZE)

/* debug definitions */

#define DEBUG_flow      0x0001
#define DEBUG_read      0x0002
#define DEBUG_write     0x0004
#define DEBUG_level1    0x0008
#define DEBUG_level2    0x0010
#define DEBUG_reg       0x0020
#define DEBUG_asm       0x0040
#define DEBUG_xack      0x0080
#define DEBUG_all       0xFFFF

/* Simulator stop codes */

#define STOP_RSRV       1                           /* must be 1 */
#define STOP_HALT       2                           /* HALT */
#define STOP_IBKPT      3                           /* breakpoint */
#define STOP_OPCODE     4                           /* Invalid Opcode */
#define STOP_IO         5                           /* I/O error */
#define STOP_MEM        6                           /* Memory error */
#define STOP_XACK       7                           /* XACK error */

