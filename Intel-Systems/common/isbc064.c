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

        ?? ??? 11 -- Original file.
        16 Dec 12 -- Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated isbc016, isbc032, isbc048 and isbc064
        memory card on an Intel multibus system.
*/

#include "system_defs.h"

#define UNIT_V_MSIZE    (UNIT_V_UF+2)                   /* Memory Size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

#define SET_XACK(VAL)       (xack = VAL)

/* prototypes */

t_stat isbc064_cfg(uint16 base, uint16 size);
t_stat isbc064_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc064_reset(DEVICE *dptr);
uint8 isbc064_get_mbyte(uint16 addr);
void isbc064_put_mbyte(uint16 addr, uint8 val);

/* external function prototypes */

/* local globals */

/* external globals */

extern uint16 PCX;                    /* program counter */
extern uint8 xack;

/* isbc064 Standard SIMH Device Data Structures */

UNIT isbc064_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_DISABLE+UNIT_BINK, 65536), KBD_POLL_WAIT
};

MTAB isbc064_mod[] = {
    { UNIT_MSIZE, 16384, "16K", "16K", &isbc064_set_size },
    { UNIT_MSIZE, 32768, "32K", "32K", &isbc064_set_size },
    { UNIT_MSIZE, 49152, "48K", "48K", &isbc064_set_size },
    { UNIT_MSIZE, 65536, "64K", "64K", &isbc064_set_size },
    { 0 }
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
    isbc064_mod,        //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
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

/* Service routines to handle simulator functions */

// configuration routine

t_stat isbc064_cfg(uint16 base, uint16 size)
{
    sim_printf("    sbc064: 0%04XH bytes at base 0%04XH\n",
        size, base);
    isbc064_unit.capac = size;          //set size
    isbc064_unit.u3 = base;             //and base
    isbc064_unit.filebuf = (uint8 *)calloc(size, sizeof(uint8));
    if (isbc064_unit.filebuf == NULL) {
        sim_printf ("    sbc064: Malloc error\n");
        return SCPE_MEM;
    }
    return SCPE_OK;
}

t_stat isbc064_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if ((val <= 0) || (val > MAXMEMSIZE)) {
        sim_printf("Memory size error - val=%d\n", val);
        return SCPE_ARG;
    }
    isbc064_reset(&isbc064_dev);
    isbc064_unit.capac = val;
    sim_printf("SBC064: Size set to %04X\n", val);
    return SCPE_OK;
}

/* Reset routine */

t_stat isbc064_reset (DEVICE *dptr)
{
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 isbc064_get_mbyte(uint16 addr)
{
    uint32 val;

    if ((isbc064_dev.flags & DEV_DIS) == 0) { //device is enabled
        if ((addr >= isbc064_unit.u3) && (addr <= (isbc064_unit.u3 + isbc064_unit.capac))) {
            SET_XACK(1);                /* good memory address */
            val = *((uint8 *)isbc064_unit.filebuf + (addr - isbc064_unit.u3));
                  return (val & 0xFF);
        } else {
            sim_printf("isbc064_get_mbyte: Read-Enabled Out of range addr=%04X PC=%04X\n", addr, PCX);
            SET_XACK(0);                /* bad memory address */
            return 0xff;                /* multibus has active high pullups and inversion */
        }
    } //device is disabled/not installed
    sim_printf ("isbc064_get_mbyte: Read-Disabled addr=%04X PC=%04X\n", addr, PCX);
    SET_XACK(0);                        /* bad memory address */
    return 0xff;                        /* multibus has active high pullups and inversion */
}

/*  put a byte into memory */

void isbc064_put_mbyte(uint16 addr, uint8 val)
{
       if ((isbc064_dev.flags & DEV_DIS) == 0) { //device is enabled
        if ((addr >= isbc064_unit.u3) && (addr <= (isbc064_unit.u3 + isbc064_unit.capac))) {
            SET_XACK(1);                /* good memory address */
            *((uint8 *)isbc064_unit.filebuf + (addr - isbc064_unit.u3)) = val & 0xFF;
            return;
        } else {
            sim_printf("isbc064_put_mbyte: Write Out of range addr=%04X PC=%04X\n", addr, PCX);
            SET_XACK(0);                /* bad memory address */
            return;
        }
    } //device is disabled/not installed
    sim_printf ("isbc064_put_mbyte: Write-Disabled addr=%04X PC=%04X\n", addr, PCX);
    SET_XACK(0);                        /* bad memory address */
}

/* end of isbc064.c */
