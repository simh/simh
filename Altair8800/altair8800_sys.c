/* altair8800_sys.c: MITS Altair 8800 SIMH System Interface

   Copyright (c) 2025 Patrick A. Linstruth

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

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   ----------------------------------------------------------

   This module of the simulator contains the glue between the
   Altair8800 simulator and SIMH.
 
   To add a device, these modules must be modified:

   altair8800_sys.h    add external DEVICE declaration
   altair8800_sys.c    add DEVICE to sim_devices

   ----------------------------------------------------------

   History:
   11/07/25 Initial version

*/

#include "sim_defs.h"
#include "altair8800_sys.h"

#if 0
static t_bool fprint_stopped(FILE *st, t_stat reason);
#endif

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "Altair 8800 (BUS)";

int32 sim_emax = SIM_EMAX;

DEVICE *sim_devices[] = {
    &bus_dev,
    &cpu_dev,
    &ssw_dev,
    &simh_dev,
    &ram_dev,
    &bram_dev,
    &rom_dev,
    &po_dev,
    &mdsk_dev,
    &m2sio0_dev,
    &m2sio1_dev,
    &sio_dev,
    &sbc200_dev,
    &tarbell_dev,
    &vfii_dev,
    NULL
};

char memoryAccessMessage[256];
char instructionMessage[256];

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",            /* 0 is reserved/unknown */
    "Breakpoint",
    memoryAccessMessage,
    instructionMessage,
    "Invalid Opcode",
    "HALT instruction"
};


/* find_unit_index   find index of a unit

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result  =       index of device
*/
int32 sys_find_unit_index(UNIT* uptr)
{
    DEVICE *dptr = find_dev_from_unit(uptr);

    if (dptr == NULL) {
        return -1;
    }

    return (uptr - dptr->units);
}

#if 0

// Need to figure out how to initize in altair8800_sys.c
//
static t_bool fprint_stopped(FILE *st, t_stat reason)
{
    fprintf(st, "Hey, it stopped!\n");

    return FALSE;
}

#endif

char *sys_strupr(const char *str)
{
    static char s[128];
    int i;

    for (i = 0; i < sizeof(s) && str[i] != '\0'; i++) {
        s[i] = sim_toupper(str[i]);
    }

    s[i] = '\0';

    return s;
}


uint8 sys_floorlog2(unsigned int n)
{
    /* Compute log2(n) */
    uint8 r = 0;
    if (n >= 1<<16) {
        n >>=16;
        r += 16;
    }
    if (n >= 1<< 8) {
        n >>= 8;
        r +=  8;
    }
    if (n >= 1<< 4) {
        n >>= 4;
        r +=  4;
    }
    if (n >= 1<< 2) {
        n >>= 2;
        r +=  2;
    }
    if (n >= 1<< 1) {
        r +=  1;
    }
    return ((n == 0) ? (0xFF) : r); /* 0xFF is error return value */
}

