/*  ipc-cont.c: Intel IPC control port PIO adapter

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

        07 Jun 16 - Original file.

    NOTES:

*/

#include "system_defs.h"                /* system header in system dir */

/* function prototypes */

t_stat ipc_cont_cfg(uint8 base, uint8 devnum);
uint8 ipc_cont(t_bool io, uint8 data, uint8 devnum);    /* ipc_cont*/
t_stat ipc_cont_reset (DEVICE *dptr);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);

/* globals */

UNIT ipc_cont_unit =
    { UDATA (0, 0, 0) };                /* ipc_cont*/


REG ipc_cont_reg[] = {
    { HRDATA (CONTROL0, ipc_cont_unit.u3, 8) }, /* ipc_cont */
    { NULL }
};

DEBTAB ipc_cont_debug[] = {
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

DEVICE ipc_cont_dev = {
    "IPC-CONT",         //name
    &ipc_cont_unit,     //units
    ipc_cont_reg,       //registers
    NULL,               //modifiers
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
    0,                  //flags
    0,                  //dctrl
    ipc_cont_debug,     //debflags
    NULL,               //msize
    NULL                //lname
};

// ipc_cont configuration

t_stat ipc_cont_cfg(uint8 base, uint8 devnum)
{
    sim_printf("    ipc-cont[%d]: at base 0%02XH\n",
        devnum, base & 0xFF);
    reg_dev(ipc_cont, base, devnum); 
    return SCPE_OK;
}

/* Reset routine */

t_stat ipc_cont_reset(DEVICE *dptr)
{
    ipc_cont_unit.u3 = 0x00;                    /* ipc reset */
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* IPC control port functions */

uint8 ipc_cont(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read status port */
        return ipc_cont_unit.u3;
    } else {                            /* write control port */
        //this simulates an 74LS259 register 
        //d0-d2 address the reg(in reverse order!), d3 is the data to be latched (inverted)
        switch(data & 0x07) {
            case 5:                     //interrupt enable 8085 INTR
                if(data & 0x08)         //bit low
                    ipc_cont_unit.u3 &= 0xBF;
                else                    //bit high
                    ipc_cont_unit.u3 |= 0x20;
                break;
            case 4:                     //*selboot ROM @ 0E800h
                if(data & 0x08)         //bit low
                    ipc_cont_unit.u3 &= 0xEF;
                else                    //bit high
                    ipc_cont_unit.u3 |= 0x10;
                break;
            case 2:                     //*startup ROM @ 00000h
                if(data & 0x08)         //bit low
                    ipc_cont_unit.u3 &= 0xFB;
                else                    //bit high
                    ipc_cont_unit.u3 |= 0x04;
                break;
            case 1:                     //override inhibit other multibus users
                if(data & 0x08)         //bit low
                    ipc_cont_unit.u3 &= 0xFD;
                else                    //bit high
                    ipc_cont_unit.u3 |= 0x02;
                break;
            case 0:                     //aux prom enable
                if(data & 0x08)         //bit low
                    ipc_cont_unit.u3 &= 0xFE;
                else                    //bit high
                    ipc_cont_unit.u3 |= 0x01;
                break;
            default:
                break;
        }
    }
    return 0;
}

/* end of ipc-cont.c */
