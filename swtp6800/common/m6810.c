/*  m6810.c: Motorola m6810 RAM emulator

    Copyright (c) 2011-2012, William A. Beech

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated m6810 RAM device on a CPU board.  The
        byte get and put routines use an offset into the RAM image to locate the 
        proper byte.  This allows another device to set the base address for the
        M6810.
*/

#include <stdio.h>
#include "swtp_defs.h"

/* function prototypes */

t_stat m6810_reset (DEVICE *dptr);
int32 m6810_get_mbyte(int32 offset);
void m6810_put_mbyte(int32 offset, int32 val);
t_stat m6810_examine(t_value *eval_array, t_addr addr, UNIT *uptr, int32 switches);
t_stat m6810_deposit(t_value value, t_addr addr, UNIT *uptr, int32 switches);

/* SIMH RAM Standard I/O Data Structures */

UNIT m6810_unit = { UDATA (NULL, UNIT_BINK, 128),
                    0 };

MTAB m6810_mod[] = {
    { 0 }
};

DEBTAB m6810_debug[] = {
    { "ALL", DEBUG_all, "All debug bits" },
    { "FLOW", DEBUG_flow, "Flow control" },
    { "READ", DEBUG_read, "Read Command" },
    { "WRITE", DEBUG_write, "Write Command"},
    { NULL }
};

DEVICE m6810_dev = {
    "M6810",                        //name
    &m6810_unit,                    //units
    NULL,                           //registers
    m6810_mod,                      //modifiers
    1,                              //numunits
    16,                             //aradix
    32,                             //awidth
    1,                              //aincr
    16,                             //dradix
    8,                              //dwidth
    &m6810_examine,                 //examine
    &m6810_deposit,                 //deposit
    &m6810_reset,                   //reset
    NULL,                           //boot
    NULL,                           //attach
    NULL,                           //detach
    NULL,                           //ctxt
    DEV_DEBUG,                      //flags
    0,                              //dctrl
    m6810_debug,                    //debflags
    NULL,                           //msize
    NULL                            //lname
};

/* global variables */

/* m6810_reset */

t_stat m6810_reset (DEVICE *dptr)
{
    sim_debug (DEBUG_flow, &m6810_dev, "m6810_reset: \n");
    if (m6810_unit.filebuf == NULL) {
        m6810_unit.filebuf = malloc(128);
        if (m6810_unit.filebuf == NULL) {
            printf("m6810_reset: Malloc error\n");
            return SCPE_MEM;
        }
        m6810_unit.capac = 128;
    }
    sim_debug (DEBUG_flow, &m6810_dev, "m6810_reset: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    RAM memory read or write is issued.
*/

/*  get a byte from memory - from offset from start of RAM*/

int32 m6810_get_mbyte(int32 offset)
{
    int32 val;

    sim_debug (DEBUG_read, &m6810_dev, "m6810_get_mbyte: offset=%04X\n", offset);
    if (((t_addr)offset) < m6810_unit.capac) {
        val = *((uint8 *)(m6810_unit.filebuf) + offset) & 0xFF;
        sim_debug (DEBUG_read, &m6810_dev, "val=%04X\n", val);
        return val;
    } else {
        sim_debug (DEBUG_read, &m6810_dev, "m6810_get_mbyte: out of range\n");
        return 0xFF;
    }
}

/*  put a byte to memory */

void m6810_put_mbyte(int32 offset, int32 val)
{
    sim_debug (DEBUG_write, &m6810_dev, "m6810_put_mbyte: offset=%04X, val=%02X\n",
        offset, val);
    if ((t_addr)offset < m6810_unit.capac) {
        *((uint8 *)(m6810_unit.filebuf) + offset) = val & 0xFF;
        return;
    } else {
        sim_debug (DEBUG_write, &m6810_dev, "m6810_put_mbyte: out of range\n");
        return;
    }
}

/* end of m6810.c */

t_stat m6810_examine(t_value *eval_array, t_addr addr, UNIT *uptr, int32 switches)
{
    int32 i;

    for (i=0; i<sim_emax; ++i)
        *eval_array++ = m6810_get_mbyte(addr++);
    return SCPE_OK;
}

t_stat m6810_deposit(t_value value, t_addr addr, UNIT *uptr, int32 switches)
{
    m6810_put_mbyte(addr,value);
    return SCPE_OK;
}
