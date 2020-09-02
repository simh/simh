/*  mp-a_sys.c: SWTP 6800 system interface

    Copyright (c) 2005-2012, William Beech

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
    WILLIAM A BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of William A. Beech shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from William A. Beech.
*/

#include <ctype.h>
#include <string.h>
#include "swtp_defs.h"

/* externals */

extern DEVICE CPU_BD_dev;
extern DEVICE m6800_dev;
extern REG m6800_reg[];
extern DEVICE BOOTROM_dev;
extern DEVICE m6810_dev;

extern DEVICE MB_dev;
extern DEVICE sio_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE mp_8m_dev;
extern DEVICE dsk_dev;

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "SWTP 6800, V2, MP-A CPU Board";

REG *sim_PC = &m6800_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &CPU_BD_dev,
    &m6800_dev,
    &BOOTROM_dev,
    &m6810_dev,
    &MB_dev,
    &sio_dev,
    &ptr_dev,
    &ptp_dev,
    &mp_8m_dev,
    &dsk_dev,
    NULL
};

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Unknown I/O Instruction",
    "HALT instruction",
    "Breakpoint",
    "Invalid Opcode",
    "Invalid Memory" 
};

/* end of mp-a_sys.c */
