/*  mds210_sys.c: multibus system interface

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
    
    18 May 19 - Equipment Emulated:
    Model 210 chassis.
    Integrated processor board (IPB).
    Parallel I/O board (PIO).
    ROM-resident system monitor.
    Auxiliary ROM board with MCS-80/MCS-85 assembler
    and text editor.
    
*/

#include "system_defs.h"

extern DEVICE i8080_dev;
extern REG i8080_reg[];
extern DEVICE i8251_dev;
extern DEVICE i8253_dev;
extern DEVICE i8255_dev;
extern DEVICE i8259_dev;
extern DEVICE EPROM_dev;
extern DEVICE RAM_dev;
extern DEVICE ipc_cont_dev;
extern DEVICE multibus_dev;
//extern DEVICE isbc201_dev;
//extern DEVICE isbc202_dev;
//extern DEVICE isbc206_dev;
//extern DEVICE zx200a_dev;
extern DEVICE isbc464_dev;

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
*/

char sim_name[] = "Intel MDS-210";

REG *sim_PC = &i8080_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &i8080_dev,
    &EPROM_dev,
    &RAM_dev,
    &i8251_dev,
    &i8253_dev,
    &i8255_dev,
    &i8259_dev,
    &ipc_cont_dev,
    &multibus_dev,
//    &isbc201_dev,
//    &isbc202_dev,
//    &isbc206_dev,
//    &zx200a_dev,
    &isbc464_dev,
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

