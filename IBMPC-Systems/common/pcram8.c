/*  pcram8.c: Intel RAM simulator for 8-bit SBCs

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        11 Jul 16 - Original file.

    NOTES:

        These functions support a simulated RAM devices on a PC XT SBC.
*/

#include "system_defs.h"

/* function prototypes */

t_stat RAM_svc (UNIT *uptr);
t_stat RAM_reset (DEVICE *dptr, uint32 base, uint32 size);
uint8 RAM_get_mbyte(uint32 addr);
void RAM_put_mbyte(uint32 addr, uint8 val);

/* external function prototypes */

extern UNIT i8255_unit[];

/* SIMH RAM Standard I/O Data Structures */

UNIT RAM_unit = { UDATA (NULL, UNIT_BINK, 0), KBD_POLL_WAIT };

DEBTAB RAM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE RAM_dev = {
    "RAM",              //name
    &RAM_unit,          //units
    NULL,               //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &RAM_reset,         //reset
    NULL,               //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG,          //flags
    0,                  //dctrl
    RAM_debug,          //debflags
    NULL,               //msize
    NULL                //lname
};

/* global variables */

/* RAM functions */

/* RAM reset */

t_stat RAM_reset (DEVICE *dptr, uint32 base, uint32 size)
{
    sim_debug (DEBUG_flow, &RAM_dev, "   RAM_reset: base=%05X size=%05X\n", base, size-1);
    if (RAM_unit.capac == 0) {          /* if undefined */
        RAM_unit.capac = size;
        RAM_unit.u3 = base;
    }
    if (RAM_unit.filebuf == NULL) {     /* no buffer allocated */
        RAM_unit.filebuf = malloc(RAM_unit.capac);
        if (RAM_unit.filebuf == NULL) {
            sim_debug (DEBUG_flow, &RAM_dev, "RAM_set_size: Malloc error\n");
            return SCPE_MEM;
        }
    }
    sim_printf("   RAM: Available [%05X-%05XH]\n", 
        RAM_unit.u3,
        RAM_unit.u3 + RAM_unit.capac - 1);
    sim_debug (DEBUG_flow, &RAM_dev, "RAM_reset: Done\n");
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 RAM_get_mbyte(uint32 addr)
{
    uint8 val;

    if (i8255_unit[0].u5 & 0x02) {         /* enable RAM */
        sim_debug (DEBUG_read, &RAM_dev, "RAM_get_mbyte: addr=%04X\n", addr);
        if ((addr >= RAM_unit.u3) && ((uint32) addr < (RAM_unit.u3 + RAM_unit.capac))) {
            val = *((uint8 *)RAM_unit.filebuf + (addr - RAM_unit.u3));
            sim_debug (DEBUG_read, &RAM_dev, " val=%04X\n", val); 
            return (val & 0xFF);
        }
        sim_debug (DEBUG_read, &RAM_dev, " Out of range\n");
        return 0xFF;
    }
    sim_debug (DEBUG_read, &RAM_dev, " RAM disabled\n");
    return 0xFF;
}

/*  put a byte to memory */

void RAM_put_mbyte(uint32 addr, uint8 val)
{
    if (i8255_unit[0].u5 & 0x02) {         /* enable RAM */
        sim_debug (DEBUG_write, &RAM_dev, "RAM_put_mbyte: addr=%04X, val=%02X\n", addr, val);
        if ((addr >= RAM_unit.u3) && ((uint32)addr < RAM_unit.u3 + RAM_unit.capac)) {
            *((uint8 *)RAM_unit.filebuf + (addr - RAM_unit.u3)) = val & 0xFF;
            sim_debug (DEBUG_write, &RAM_dev, "\n");
            return;
        }
        sim_debug (DEBUG_write, &RAM_dev, " Out of range\n");
        return;
    }
    sim_debug (DEBUG_write, &RAM_dev, " RAM disabled\n");
}

/* end of pcram8.c */
