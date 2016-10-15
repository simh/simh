/*  ibmpcxt_sys.c: IBM 5160 simulator

    Copyright (c) 2016, William A. Beech

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

#include "system_defs.h"

extern DEVICE i8088_dev;
extern REG i8088_reg[];
extern DEVICE i8237_dev;
extern DEVICE i8253_dev;
extern DEVICE i8255_dev;
extern DEVICE i8259_dev;
extern DEVICE EPROM_dev;
extern DEVICE RAM_dev;
extern DEVICE xtbus_dev;

/* bit patterns to manipulate 8-bit ports */

#define i82XX_bit_0     0x01            //bit 0 of a port
#define i82XX_bit_1     0x02            //bit 1 of a port
#define i82XX_bit_2     0x04            //bit 2 of a port
#define i82XX_bit_3     0x08            //bit 3 of a port
#define i82XX_bit_4     0x10            //bit 4 of a port
#define i82XX_bit_5     0x20            //bit 5 of a port
#define i82XX_bit_6     0x40            //bit 6 of a port
#define i82XX_bit_7     0x80            //bit 7 of a port

#define i82XX_nbit_0    ~i8255_bit_0     //bit 0 of a port
#define i82XX_nbit_1    ~i8255_bit_1     //bit 1 of a port
#define i82XX_nbit_2    ~i8255_bit_3     //bit 2 of a port
#define i82XX_nbit_3    ~i8255_bit_3     //bit 3 of a port
#define i82XX_nbit_4    ~i8255_bit_4     //bit 4 of a port
#define i82XX_nbit_5    ~i8255_bit_5     //bit 5 of a port
#define i82XX_nbit_6    ~i8255_bit_6     //bit 6 of a port
#define i82XX_nbit_7    ~i8255_bit_7     //bit 7 of a port

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
*/

char sim_name[] = "IBM PC";

REG *sim_PC = &i8088_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &i8088_dev,
    &EPROM_dev,
    &RAM_dev,
    &i8237_dev,
    &i8253_dev,
    &i8255_dev,
    &i8259_dev,
    &xtbus_dev,
    NULL
};

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unknown I/O Instruction",
    "HALT instruction",
    "Breakpoint",
    "Invalid Opcode",
    "Invalid Memory",
    "XACK Error"
};

