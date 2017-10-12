/*  iEPROM.c: Intel EPROM simulator for 8-bit SBCs

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        ?? ??? 10 - Original file.
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated ROM devices on an iSBC-80/XX SBCs.
        This allows the attachment of the device to a binary file containing the EPROM
        code image.  Unit will support a single 2708, 2716, 2732, or 2764 type EPROM.
        These functions also support bit 1 of 8255 number 1, port B, to enable/
        disable the onboard ROM.
*/

#include "system_defs.h"

#define DEBUG   0

/* function prototypes */

t_stat EPROM_attach (UNIT *uptr, CONST char *cptr);
t_stat EPROM_reset (DEVICE *dptr, uint16 size);
uint8 EPROM_get_mbyte(uint16 addr);

/* external function prototypes */

//extern uint8 i8255_C[4];                    //port c byte I/O
extern uint8 xack;                          /* XACK signal */

/* SIMH EPROM Standard I/O Data Structures */

UNIT EPROM_unit = {
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+UNIT_BUFABLE+UNIT_MUSTBUF, 0), 0
};

DEBTAB EPROM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE EPROM_dev = {
    "EPROM",            //name
    &EPROM_unit,        //units
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
//    &EPROM_reset,       //reset
    NULL,                               //reset
    NULL,               //boot
    &EPROM_attach,      //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG,          //flags
    0,                  //dctrl
    EPROM_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* global variables */

/* EPROM functions */

/* EPROM reset */

t_stat EPROM_reset (DEVICE *dptr, uint16 size)
{
    sim_debug (DEBUG_flow, &EPROM_dev, "   EPROM_reset: base=0000 size=%04X\n", size);
    if ((EPROM_unit.flags & UNIT_ATT) == 0) { /* if unattached */
        EPROM_unit.capac = size;           /* set EPROM size to 0 */
        sim_printf("      EPROM: Configured, Not attached\n");
        sim_debug (DEBUG_flow, &EPROM_dev, "Done1\n");
    } else {
        sim_printf("      EPROM: Configured %d bytes, Attached to %s\n",
            EPROM_unit.capac, EPROM_unit.filename);
    }
    sim_debug (DEBUG_flow, &EPROM_dev, "Done2\n");
    return SCPE_OK;
}

/* EPROM attach  */

t_stat EPROM_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: Error\n");
        return r;
    }
    sim_debug (DEBUG_read, &EPROM_dev, "\tClose file\n");
    sim_printf("   EPROM: Configured %d bytes, Attached to %s\n",
        EPROM_unit.capac, EPROM_unit.filename);
    sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: Done\n");
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 EPROM_get_mbyte(uint16 addr)
{
    uint8 val;

    sim_debug (DEBUG_read, &EPROM_dev, "EPROM_get_mbyte: addr=%04X\n", addr);
    if (addr < EPROM_unit.capac) {
        SET_XACK(1);                /* good memory address */
        sim_debug (DEBUG_xack, &EPROM_dev, "EPROM_get_mbyte: Set XACK for %04X\n", addr); 
        val = *((uint8 *)EPROM_unit.filebuf + addr);
        sim_debug (DEBUG_read, &EPROM_dev, " val=%04X\n", val);
        return (val & 0xFF);
    } else {
        sim_debug (DEBUG_read, &EPROM_dev, " Out of range\n");
        return 0;
    }
}

/* end of iEPROM.c */
