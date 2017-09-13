
/* scelbi_sys.c: Intel 8008 CPU system interface for the SCELBI computer.

   Copyright (c) 2017, Hans-Ake Lund

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

   Except as contained in this notice, the name of Hans-Ake Lund shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Hans-Ake Lund.

   04-Sep-17    HAL     Working version of CPU simulator for SCELBI computer
   12-Sep-17    HAL     Modules restructured in "Intel-Systems" directory

*/

#include "system_defs.h"

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern DEVICE tty_dev;
extern DEVICE ptr_dev;
extern unsigned char Mem[];
extern int32 saved_PCreg;

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "SCELBI";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &tty_dev,
    &ptr_dev,
    NULL
};

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unknown I/O Instruction",
    "HALT instruction",
    "Breakpoint",
    "Invalid Opcode"
};

/* This is the binary loader.  The input file is considered to be
   a string of literal bytes with no format special format. The
   load starts at the current value of the PC.
*/
t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 i, addr = 0, cnt = 0;

if (*cptr != 0)
    return SCPE_ARG;
if (flag != 0) {
    sim_printf ("DUMP command not supported.\n");
    return SCPE_ARG;
    }
addr = saved_PCreg;
while ((i = getc (fileref)) != EOF) {
    if (addr >= MAXMEMSIZE)
        return (SCPE_NXM);
    Mem[addr] = i;
    addr++;
    cnt++;
}                                                       /* end while */
sim_printf ("%d Bytes loaded.\n", cnt);
return (SCPE_OK);
}
