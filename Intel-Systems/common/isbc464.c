/*  isbc464.c: Intel iSBC 464 32K Byte ROM Card

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

        29 Oct 17 - Original file.

    NOTES:

*/

#include "system_defs.h"

/* prototypes */

t_stat isbc064_cfg(uint16 base, uint16 size);
t_stat isbc464_reset (DEVICE *dptr);
t_stat isbc464_attach (UNIT *uptr, CONST char *cptr);
uint8 isbc464_get_mbyte(uint16 addr);
void isbc464_put_mbyte(uint16 addr, uint8 val);

/* external function prototypes */

/* external globals */

extern uint8 xack;                         /* XACK signal */

/* local globals */

/* isbc464 Standard I/O Data Structures */

UNIT isbc464_unit = {
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+UNIT_BUFABLE+UNIT_MUSTBUF, 0), 0
};

DEBTAB isbc464_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE isbc464_dev = {
    "SBC464",           //name
    &isbc464_unit,      //units
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
    NULL,               //reset
    NULL,               //boot
    isbc464_attach,     //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags
    0,                  //dctrl
    isbc464_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* isbc464 globals */

// configuration routine

t_stat isbc464_cfg(uint16 base, uint16 size)
{
    sim_printf("    sbc464: 0%04XH bytes at base 0%04XH\n",
        size, base);
    isbc464_unit.capac = size;          //set size
    isbc464_unit.u3 = base;             //and base
    return SCPE_OK;
}

/* Reset routine */

t_stat isbc464_reset (DEVICE *dptr)
{
    return SCPE_OK;
}

/* isbc464 attach  */

t_stat isbc464_attach (UNIT *uptr, CONST char *cptr) 
{
    t_stat r;

    isbc464_reset(NULL);                //odd fix, but it works
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_printf ("isbc464_attach: Error %d\n", r);
        return r;
    }
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 isbc464_get_mbyte(uint16 addr)
{
    uint32 val, org, len;
    uint8 *fbuf;

    if ((isbc464_dev.flags & DEV_DIS) == 0) {
        org = isbc464_unit.u3;
        len = isbc464_unit.capac;
        fbuf = (uint8 *) isbc464_unit.filebuf;
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(1);                /* good memory address */
            val = *(fbuf + (addr - org));
            return (val & 0xFF);
        } else {
            sim_printf("isbc464_get_mbyte: Out of range\n");
            SET_XACK(0);                /* bad memory address */
            return 0;                   /* multibus has active high pullups and inversion */
        }
    }
    sim_printf ("isbc464_put_mbyte: Write-Disabled addr=%04X\n", addr);
    SET_XACK(0);                /* bad memory address */
    return 0;                           /* multibus has active high pullups and inversion */
}

/*  put a byte into memory */

void isbc464_put_mbyte(uint16 addr, uint8 val)
{
    uint32 org, len;

    if ((isbc464_dev.flags & DEV_DIS) == 0) {
        org = isbc464_unit.u3;
        len = isbc464_unit.capac;
        if ((addr >= org) && (addr < (org + len))) {
            SET_XACK(0);                /* bad memory address */
            sim_printf ("isbc464_put_mbyte: Read-only Memory\n");
            return;
        } else {
            sim_printf ("isbc464_put_mbyte: Out of range\n");
            SET_XACK(0);                /* bad memory address */
            return;
        }
    }
    sim_printf ("isbc464_put_mbyte: Disabled\n");
    SET_XACK(0);                /* bad memory address */
}

/* end of isbc464.c */
