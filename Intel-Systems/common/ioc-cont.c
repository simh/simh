/*  ioc-cont.c: Intel IPC DBB adapter

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

uint8 ioc_cont(t_bool io, uint8 data);    /* ioc_cont*/
t_stat ioc_cont_reset (DEVICE *dptr, uint16 baseport);

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);
extern uint32 saved_PC;                    /* program counter */

/* globals */

UNIT ioc_cont_unit[] = {
    { UDATA (0, 0, 0) },                /* ioc_cont*/
};

REG ioc_cont_reg[] = {
    { HRDATA (CONTROL0, ioc_cont_unit[0].u3, 8) }, /* ioc_cont */
    { NULL }
};

DEBTAB ioc_cont_debug[] = {
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

DEVICE ioc_cont_dev = {
    "IOC-CONT",             //name
    ioc_cont_unit,         //units
    ioc_cont_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &ioc_cont_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    ioc_cont_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* Reset routine */

t_stat ioc_cont_reset(DEVICE *dptr, uint16 baseport)
{
    sim_printf("   ioc_cont[%d]: Reset\n", 0);
    sim_printf("   ioc_cont[%d]: Registered at %04X\n", 0, baseport);
    reg_dev(ioc_cont, baseport, 0); 
    reg_dev(ioc_cont, baseport + 1, 0); 
    ioc_cont_unit[0].u3 = 0x00; /* ipc reset */
    return SCPE_OK;
}

/* IOC control port functions */

uint8 ioc_cont(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read status port */
        sim_printf("   ioc_cont: read data=%02X port=%02X returned 0x00 from PC=%04X\n", data, 0, saved_PC);
        return 0x00;
    } else {                            /* write control port */
        sim_printf("   ioc_cont: data=%02X port=%02X\n", data, 0);
    }
}

/* end of ioc-cont.c */
