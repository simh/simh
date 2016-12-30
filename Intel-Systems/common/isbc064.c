/*  isbc064.c: Intel iSBC064 64K Byte Memory Card

    Copyright (c) 2011, William A. Beech

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

        ?? ??? 11 - Original file.
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated isbc016, isbc032, isbc048 and isbc064
        memory card on an Intel multibus system.
*/

#include "system_defs.h"

#define SET_XACK(VAL)       (xack = VAL)

/* prototypes */

t_stat isbc064_reset (DEVICE *dptr);
uint8 isbc064_get_mbyte(uint16 addr);
void isbc064_put_mbyte(uint16 addr, uint8 val);

extern uint8 xack;                         /* XACK signal */

/* isbc064 Standard I/O Data Structures */

UNIT isbc064_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_DISABLE+UNIT_BINK, 65536), KBD_POLL_WAIT
};

DEBTAB isbc064_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE isbc064_dev = {
    "SBC064",           //name
    &isbc064_unit,      //units
    NULL,               //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposite
//    &isbc064_reset,     //reset
    NULL,               //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags
    0,                  //dctrl
    isbc064_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* iSBC064 globals */

/* Reset routine */

t_stat isbc064_reset (DEVICE *dptr)
{
    sim_debug (DEBUG_flow, &isbc064_dev, "isbc064_reset: ");
    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        isbc064_unit.capac = SBC064_SIZE;
        isbc064_unit.u3 = SBC064_BASE;
        sim_printf("Initializing iSBC-064 RAM Board\n");
        sim_printf("   Available[%04X-%04XH]\n", 
            isbc064_unit.u3,
            isbc064_unit.u3 + isbc064_unit.capac - 1);
    }
    if (isbc064_unit.filebuf == NULL) {
        isbc064_unit.filebuf = (uint8 *)malloc(isbc064_unit.capac);
        if (isbc064_unit.filebuf == NULL) {
            sim_debug (DEBUG_flow, &isbc064_dev, "isbc064_reset: Malloc error\n");
            return SCPE_MEM;
        }
    }
    sim_debug (DEBUG_flow, &isbc064_dev, "isbc064_reset: Done\n");
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 isbc064_get_mbyte(uint16 addr)
{
    uint32 val, org, len;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac;
        sim_debug (DEBUG_read, &isbc064_dev, "isbc064_get_mbyte: addr=%04X", addr);
        sim_debug (DEBUG_read, &isbc064_dev, "isbc064_get_mbyte: org=%04X, len=%04X\n", org, len);
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(1);                /* good memory address */
            sim_debug (DEBUG_xack, &isbc064_dev, "isbc064_get_mbyte: Set XACK for %04X\n", addr); 
            val = *((uint8 *)isbc064_unit.filebuf + (addr - org));
            sim_debug (DEBUG_read, &isbc064_dev, " val=%04X\n", val);
//            sim_printf ("isbc064_get_mbyte: addr=%04X, val=%02X\n", addr, val);
            return (val & 0xFF);
        } else {
            sim_debug (DEBUG_read, &isbc064_dev, "isbc064_get_mbyte: Out of range\n");
            return 0;                   /* multibus has active high pullups and inversion */
        }
    }
    sim_debug (DEBUG_read, &isbc064_dev, "isbc064_get_mbyte: Disabled\n");
//    sim_printf ("isbc064_get_mbyte: Disabled\n");
    return 0;                           /* multibus has active high pullups and inversion */
}

/*  put a byte into memory */

void isbc064_put_mbyte(uint16 addr, uint8 val)
{
    uint32 org, len;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac;
//        sim_printf ("isbc064_put_mbyte: addr=%04X, val=%02X\n", addr, val);
        sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: addr=%04X, val=%02X\n", addr, val);
        sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: org=%04X, len=%04X\n", org, len);
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(1);                /* good memory address */
            sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: Set XACK for %04X\n", addr); 
            *((uint8 *)isbc064_unit.filebuf + (addr - org)) = val & 0xFF;
            sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: Return\n"); 
            return;
        } else {
            sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: Out of range\n");
            return;
        }
    }
    sim_debug (DEBUG_write, &isbc064_dev, "isbc064_put_mbyte: Disabled\n");
//    sim_printf ("isbc064_put_mbyte: Disabled\n");
}

/* end of isbc064.c */
