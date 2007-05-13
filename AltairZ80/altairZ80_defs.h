/*  altairz80_defs.h: MITS Altair simulator definitions

    Copyright (c) 2002-2007, Peter Schorn

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.

    Based on work by Charles E Owen (c) 1997
*/

#include "sim_defs.h"                                   /* simulator definitions                        */

#define MAXMEMSIZE          65536                       /* maximum memory size                          */
#define ADDRMASK            (MAXMEMSIZE - 1)            /* address mask                                 */
#define BOOTROM_SIZE        256                         /* size of boot rom                             */
#define MAXBANKS            8                           /* max number of memory banks                   */
#define MAXBANKSLOG2        3                           /* log2 of MAXBANKS                             */
#define BANKMASK            (MAXBANKS-1)                /* bank mask                                    */
#define MEMSIZE             (cpu_unit.capac)            /* actual memory size                           */
#define KB                  1024                        /* kilo byte                                    */
#define DEFAULT_ROM_LOW     0xff00                      /* default for lowest address of ROM            */
#define DEFAULT_ROM_HIGH    0xffff                      /* default for highest address of ROM           */

#define NUM_OF_DSK          8                           /* NUM_OF_DSK must be power of two              */
#define LDA_INSTRUCTION     0x3e                        /* op-code for LD A,<8-bit value> instruction   */
#define UNIT_NO_OFFSET_1    0x37                        /* LD A,<unitno>                                */
#define UNIT_NO_OFFSET_2    0xb4                        /* LD a,80h | <unitno>                          */

#define UNIT_V_OPSTOP       (UNIT_V_UF+0)               /* stop on invalid operation                    */
#define UNIT_OPSTOP         (1 << UNIT_V_OPSTOP)
#define UNIT_V_CHIP         (UNIT_V_UF+1)               /* 8080 or Z80 CPU                              */
#define UNIT_CHIP           (1 << UNIT_V_CHIP)
#define UNIT_V_MSIZE        (UNIT_V_UF+2)               /* memory size                                  */
#define UNIT_MSIZE          (1 << UNIT_V_MSIZE)
#define UNIT_V_BANKED       (UNIT_V_UF+3)               /* banked memory is used                        */
#define UNIT_BANKED         (1 << UNIT_V_BANKED)
#define UNIT_V_ROM          (UNIT_V_UF+4)               /* ROM exists                                   */
#define UNIT_ROM            (1 << UNIT_V_ROM)
#define UNIT_V_ALTAIRROM    (UNIT_V_UF+5)               /* ALTAIR ROM exists                            */
#define UNIT_ALTAIRROM      (1 << UNIT_V_ALTAIRROM)
#define UNIT_V_WARNROM      (UNIT_V_UF+6)               /* warn if ROM is written to                    */
#define UNIT_WARNROM        (1 << UNIT_V_WARNROM)

#define UNIX_PLATFORM (defined (__linux) || defined(__NetBSD__) || defined (__OpenBSD__) || \
    defined (__FreeBSD__) || defined (__APPLE__))

#define ADDRESS_FORMAT      "[%04xh]"
#define PC_FORMAT           "\n" ADDRESS_FORMAT " "
#define MESSAGE_1(p1)                \
    sprintf(messageBuffer,PC_FORMAT p1,PCX);                 printMessage()
#define MESSAGE_2(p1,p2)             \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2);              printMessage()
#define MESSAGE_3(p1,p2,p3)          \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2,p3);           printMessage()
#define MESSAGE_4(p1,p2,p3,p4)       \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2,p3,p4);        printMessage()
#define MESSAGE_5(p1,p2,p3,p4,p5)    \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2,p3,p4,p5);     printMessage()
#define MESSAGE_6(p1,p2,p3,p4,p5,p6) \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2,p3,p4,p5,p6);  printMessage()
#define MESSAGE_7(p1,p2,p3,p4,p5,p6,p7) \
    sprintf(messageBuffer,PC_FORMAT p1,PCX,p2,p3,p4,p5,p6,p7);  printMessage()
