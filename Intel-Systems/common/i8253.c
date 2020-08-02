/*  i8253.c: Intel i8253 PIT adapter

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
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set baseport and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

*/

#include "system_defs.h"

#if defined (I8253_NUM) && (I8253_NUM > 0)

/* external globals */

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);

/* globals */

/* function prototypes */

t_stat i8253_cfg(uint8 base, uint8 devnum);
t_stat i8253_svc (UNIT *uptr);
t_stat i8253_reset (DEVICE *dptr);
uint8 i8253t0(t_bool io, uint8 data, uint8 devnum);
uint8 i8253t1(t_bool io, uint8 data, uint8 devnum);
uint8 i8253t2(t_bool io, uint8 data, uint8 devnum);
uint8 i8253c(t_bool io, uint8 data, uint8 devnum);

/* i8253 Standard I/O Data Structures */
/* up to 4 i8253 devices */

UNIT i8253_unit[] = { 
    { UDATA (&i8253_svc, 0, 0), 20 }, 
    { UDATA (&i8253_svc, 0, 0), 20 }, 
    { UDATA (&i8253_svc, 0, 0), 20 }, 
    { UDATA (&i8253_svc, 0, 0), 20 } 
};

REG i8253_reg[] = {
    { HRDATA (T0, i8253_unit[0].u3, 8) },
    { HRDATA (T1, i8253_unit[0].u4, 8) },
    { HRDATA (T2, i8253_unit[0].u5, 8) },
    { HRDATA (CMD, i8253_unit[0].u6, 8) },
    { HRDATA (T0, i8253_unit[1].u3, 8) },
    { HRDATA (T1, i8253_unit[1].u4, 8) },
    { HRDATA (T2, i8253_unit[1].u5, 8) },
    { HRDATA (CMD, i8253_unit[1].u6, 8) },
    { NULL }
};

DEBTAB i8253_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB i8253_mod[] = {
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8253_dev = {
    "I8253",            //name
    i8253_unit,         //units
    i8253_reg,          //registers
    i8253_mod,          //modifiers
    I8253_NUM,          //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    i8253_reset,        //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8253_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

// i8253 configuration

t_stat i8253_cfg(uint8 base, uint8 devnum)
{
    sim_printf("    i8253[%d]: at base port 0%02XH\n",
        devnum, base & 0xFF);
    reg_dev(i8253t0, base, devnum); 
    reg_dev(i8253t1, base + 1, devnum); 
    reg_dev(i8253t2, base + 2, devnum); 
    reg_dev(i8253c, base + 3, devnum); 
    return SCPE_OK;
}

/* i8253_svc - actually gets char & places in buffer */

t_stat i8253_svc (UNIT *uptr)
{
    sim_activate (&i8253_unit[0], i8253_unit[0].wait); /* continue poll */
    return SCPE_OK;
}

/* Reset routine */

t_stat i8253_reset (DEVICE *dptr)
{
    uint8 devnum;
    
    for (devnum=0; devnum<I8253_NUM; devnum++) {
        i8253_unit[devnum].u3 = 0;        /* status */
        i8253_unit[devnum].u4 = 0;        /* mode instruction */
        i8253_unit[devnum].u5 = 0;        /* command instruction */
        i8253_unit[devnum].u6 = 0;
    }
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8253t0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        return i8253_unit[devnum].u3;
    } else {                        /* write data port */
        i8253_unit[devnum].u3 = data;
        //sim_activate_after (&i8253_unit[devnum], );
        return 0;
    }
    return 0;
}

//read routine:
//sim_activate_time(&i8253_unit[devnum])/sim_inst_per_second()

uint8 i8253t1(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        return i8253_unit[devnum].u4;
    } else {                        /* write data port */
        i8253_unit[devnum].u4 = data;
        return 0;
    }
    return 0;
}

uint8 i8253t2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        return i8253_unit[devnum].u5;
    } else {                        /* write data port */
        i8253_unit[devnum].u5 = data;
        return 0;
    }
    return 0;
}

uint8 i8253c(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read status port */
        return i8253_unit[devnum].u6;
    } else {                        /* write data port */
        i8253_unit[devnum].u6 = data;
        return 0;
    }
    return 0;
}

#endif /* I8253_NUM > 0 */

/* end of i8253.c */
