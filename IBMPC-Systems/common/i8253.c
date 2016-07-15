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
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

*/

#include "system_defs.h"

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16);

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

/* globals */

int32 i8253_devnum = 0;                 //actual number of 8253 instances + 1
uint16 i8253_port[4];                   //base port registered to each instance

/* function prototypes */

t_stat i8253_svc (UNIT *uptr);
t_stat i8253_reset (DEVICE *dptr, uint16 base);
uint8 i8253_get_dn(void);
uint8 i8253t0(t_bool io, uint8 data);
uint8 i8253t1(t_bool io, uint8 data);
uint8 i8253t2(t_bool io, uint8 data);
uint8 i8253c(t_bool io, uint8 data);

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
    "8251",             //name
    i8253_unit,        //units
    i8253_reg,          //registers
    i8253_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                  //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &i8253_reset,       //reset
    NULL,       //reset
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

/* i8253_svc - actually gets char & places in buffer */

t_stat i8253_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&i8253_unit[0], i8253_unit[0].wait); /* continue poll */
    return SCPE_OK;
}

/* Reset routine */

t_stat i8253_reset (DEVICE *dptr, uint16 port)
{
    if (i8253_devnum > I8253_NUM) {
        sim_printf("i8253_reset: too many devices!\n");
        return SCPE_MEM;
    }
    i8253_port[i8253_devnum] = reg_dev(i8253t0, port); 
    reg_dev(i8253t1, port + 1); 
    reg_dev(i8253t2, port + 2); 
    reg_dev(i8253c, port + 3); 
    i8253_unit[i8253_devnum].u3 = 0;          /* status */
    i8253_unit[i8253_devnum].u4 = 0;                  /* mode instruction */
    i8253_unit[i8253_devnum].u5 = 0;                  /* command instruction */
    i8253_unit[i8253_devnum].u6 = 0;
    sim_printf("   8253-%d: Reset\n", i8253_devnum);
    sim_printf("   8253-%d: Registered at %03X\n", i8253_devnum, port);
    sim_activate (&i8253_unit[i8253_devnum], i8253_unit[i8253_devnum].wait); /* activate unit */
    i8253_devnum++;
    return SCPE_OK;
}

uint8 i8253_get_dn(void)
{
    int i;

    for (i=0; i<I8253_NUM; i++)
        if (port >=i8253_port[i] && port <= i8253_port[i] + 3)
            return i;
    sim_printf("i8253_get_dn: port %03X not in 8253 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8253t0(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8253_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            i8253_unit[devnum].u3 = data;
            return 0;
        } else {                            /* write data port */
            return i8253_unit[devnum].u3;
        }
    }
    return 0;
}

uint8 i8253t1(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8253_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            i8253_unit[devnum].u4 = data;
            return 0;
        } else {                            /* write data port */
            return i8253_unit[devnum].u4;
        }
    }
    return 0;
}

uint8 i8253t2(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8253_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            i8253_unit[devnum].u5 = data;
            return 0;
        } else {                            /* write data port */
            return i8253_unit[devnum].u5;
        }
    }
    return 0;
}

uint8 i8253c(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8253_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read status port */
            i8253_unit[devnum].u6 = data;
            return 0;
        } else {                            /* write data port */
            return i8253_unit[devnum].u6;
        }
    }
    return 0;
}

/* end of i8253.c */
