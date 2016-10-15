/*  isbc202.c: Intel double density disk adapter adapter

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

        27 Jun 16 - Original file.

    NOTES:


*/

#include "system_defs.h"                /* system header in system dir */

/* function prototypes */

uint8 isbc202(t_bool io, uint8 data, uint8 devnum);    /* isbc202*/
t_stat isbc202_reset(DEVICE *dptr, uint16 base, uint8 devnum);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16 port, uint8 devnum);

/* globals */

UNIT isbc202_unit[] = {
    { UDATA (0, 0, 0) },                /* isbc202*/
};

REG isbc202_reg[] = {
    { HRDATA (CONTROL0, isbc202_unit[0].u3, 8) }, /* isbc202 */
    { NULL }
};

DEBTAB isbc202_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE isbc202_dev = {
    "ISBC202",             //name
    isbc202_unit,         //units
    isbc202_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &isbc202_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    isbc202_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* ISBC202 control port functions */

uint8 isbc202(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* always return 0 */
        sim_printf("   isbc202: read data=%02X port=%02X returned 0\n", data, devnum);
        return 0x00;
    } else {                            /* write control port */
        sim_printf("   isbc202: data=%02X port=%02X\n", data, devnum);
    }
}

/* Reset routine */

t_stat isbc202_reset(DEVICE *dptr, uint16 base, uint8 devnum)
{
    reg_dev(isbc202, base, devnum); 
    reg_dev(isbc202, base + 1, devnum); 
    reg_dev(isbc202, base + 2, devnum); 
    reg_dev(isbc202, base + 3, devnum); 
    reg_dev(isbc202, base + 4, devnum); 
    reg_dev(isbc202, base + 5, devnum); 
    reg_dev(isbc202, base + 6, devnum); 
    reg_dev(isbc202, base + 7, devnum); 
    isbc202_unit[devnum].u3 = 0x00; /* ipc reset */
    sim_printf("   isbc202-%d: Reset\n", devnum);
    sim_printf("   isbc202-%d: Registered at %04X\n", devnum, base);
    return SCPE_OK;
}

/* end of isbc202.c */
