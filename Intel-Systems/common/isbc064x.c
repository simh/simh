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

    These functions support a simulated isbc016, isbc032, isbc048 and isbc064
    memory card on an Intel multibus system.
    
    ?? ??? 11 - Original file.
    16 Dec 12 - Modified to use system_80_10.cfg file to set base and size.
    
*/

#include <stdio.h>
#include "multibus_defs.h"

#define SET_XACK(VAL)       (XACK = VAL)

/* prototypes */

t_stat isbc064_reset (DEVICE *dptr);
int32 isbc064_get_mbyte(int32 addr);
int32 isbc064_get_mword(int32 addr);
void isbc064_put_mbyte(int32 addr, int32 val);
void isbc064_put_mword(int32 addr, int32 val);

extern uint8 XACK;                         /* XACK signal */

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
    8,                  //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposite
    &isbc064_reset,     //reset
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
    if (isbc064_dev.dctrl & DEBUG_flow)
        sim_printf("isbc064_reset: \n");
    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        if (isbc064_dev.dctrl & DEBUG_flow)
            sim_printf("isbc064_reset: Size=%04X\n", isbc064_unit.capac - 1);
        if (isbc064_dev.dctrl & DEBUG_flow)
            sim_printf("isbc064_reset: Base address=%04X\n", isbc064_unit.u3);
        sim_printf("iSBC 064: Available[%04X-%04XH]\n", 
            isbc064_unit.u3,
            isbc064_unit.u3 + isbc064_unit.capac - 1);
    }
    if (isbc064_unit.filebuf == NULL) {
        isbc064_unit.filebuf = malloc(isbc064_unit.capac);
        if (isbc064_unit.filebuf == NULL) {
            if (isbc064_dev.dctrl & DEBUG_flow)
                sim_printf("isbc064_reset: Malloc error\n");
            return SCPE_MEM;
        }
    }
    if (isbc064_dev.dctrl & DEBUG_flow)
        sim_printf("isbc064_reset: Done\n");
    return SCPE_OK;
}

/*  get a byte from memory */

int32 isbc064_get_mbyte(int32 addr)
{
    int32 val, org, len;
    int i = 0;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac;
        if (isbc064_dev.dctrl & DEBUG_read)
            sim_printf("isbc064_get_mbyte: addr=%04X", addr);
        if (isbc064_dev.dctrl & DEBUG_read)
            sim_printf("isbc064_put_mbyte: org=%04X, len=%04X\n", org, len);
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(1);                /* good memory address */
            if (isbc064_dev.dctrl & DEBUG_xack)
                sim_printf("isbc064_get_mbyte: Set XACK for %04X\n", addr); 
            val = *(uint8 *)(isbc064_unit.filebuf + (addr - org));
            if (isbc064_dev.dctrl & DEBUG_read)
                sim_printf(" val=%04X\n", val);
            return (val & 0xFF);
        } else {
            if (isbc064_dev.dctrl & DEBUG_read)
                sim_printf(" Out of range\n");
            return 0xFF;    /* multibus has active high pullups */
        }
    }
    if (isbc064_dev.dctrl & DEBUG_read)
        sim_printf(" Disabled\n");
    return 0xFF;        /* multibus has active high pullups */
}

/*  get a word from memory */

int32 isbc064_get_mword(int32 addr)
{
    int32 val;

    val = isbc064_get_mbyte(addr);
    val |= (isbc064_get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte into memory */

void isbc064_put_mbyte(int32 addr, int32 val)
{
    int32 org, len;
    int i = 0;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac;
        if (isbc064_dev.dctrl & DEBUG_write)
            sim_printf("isbc064_put_mbyte: addr=%04X, val=%02X\n", addr, val);
        if (isbc064_dev.dctrl & DEBUG_write)
            sim_printf("isbc064_put_mbyte: org=%04X, len=%04X\n", org, len);
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(1);                /* good memory address */
            if (isbc064_dev.dctrl & DEBUG_xack)
                sim_printf("isbc064_put_mbyte: Set XACK for %04X\n", addr); 
            *(uint8 *)(isbc064_unit.filebuf + (addr - org)) = val & 0xFF;
            if (isbc064_dev.dctrl & DEBUG_xack)
                sim_printf("isbc064_put_mbyte: Return\n"); 
            return;
        } else {
            if (isbc064_dev.dctrl & DEBUG_write)
                sim_printf(" Out of range\n");
            return;
        }
    }
    if (isbc064_dev.dctrl & DEBUG_write)
        sim_printf("isbc064_put_mbyte: Disabled\n");
}

/*  put a word into memory */

void isbc064_put_mword(int32 addr, int32 val)
{
    isbc064_put_mbyte(addr, val);
    isbc064_put_mbyte(addr+1, val << 8);
}

/* end of isbc064.c */
