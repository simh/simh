/*  i3214.c: Intel i3214 PICU adapter

    Copyright (c) 2019, William A. Beech

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

        12 OCT 19 - Original file.

    NOTES:

        These functions support a simulated i3214 interface device on an iSBC.
        The device had three physical I/O port which could be connected
        as needed.  This device was replaced by the 8259.

        All I/O is via programmed I/O.  The i3214 has a status port
        and two data port.    

*/

#include "system_defs.h"
#define IPC     0

#define i3214_NAME    "Intel i3214 Perpherial Interrupt Contrlooer Chip"

/* external globals */

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint16, uint8);
extern uint8 unreg_dev(uint16);

/* globals */

uint8 i3214_mask = 0;
uint8 i3214_cnt = 0;
uint8 i3214_ram[16];
uint8 EPROM_enable = 1;
uint8 BUS_OVERRIDE = 0;
uint8 monitor_boot = 0x00;
static const char* i3214_desc(DEVICE *dptr) {
    return i3214_NAME;
}
int   i3214_baseport = -1;              //base port
uint8   i3214_intnum = 0;               //interrupt number
uint8   i3214_verb = 0;                 //verbose flag

/* function prototypes */

t_stat i3214_cfg(uint16 base, uint16 devnum, uint8 dummy);
t_stat i3214_clr(void);
t_stat i3214_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat i3214_reset (DEVICE *dptr);
t_stat i3214_reset_dev (uint8 devnum);
t_stat i3214_svc (UNIT *uptr);
uint8 i3214_do_mask(t_bool io, uint8 data, uint8 devnum);
uint8 i3214_do_status(t_bool io, uint8 data, uint8 devnum);
uint8 i3214_cpu_bus_override(t_bool io, uint8 data, uint8 devnum);
uint8 i3214_monitor_do_boot(t_bool io, uint8 data, uint8 devnum);

/* i3214 Standard I/O Data Structures */
/* 1 i3214 device */

UNIT i3214_unit[] = { 
    { UDATA (&i3214_svc, 0, 0), KBD_POLL_WAIT }
};

REG i3214_reg[] = {
    { HRDATA (MASK0, i3214_mask, 8) },
    { HRDATA (CNT0, i3214_cnt, 8) },
    { HRDATA (RAM0, i3214_ram, 8) },
    { NULL }
};

DEBTAB i3214_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB i3214_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, i3214_show_param, NULL, 
        "show configured parametes for i3214" },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i3214_dev = {
    "I3214",            //name
    i3214_unit,         //units
    i3214_reg,          //registers
    i3214_mod,          //modifiers
    I3214_NUM,          //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    i3214_reset,        //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl
    i3214_debug,        //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &i3214_desc         //device description
};

// i3214 configuration

t_stat i3214_cfg(uint16 base, uint16 devnum, uint8 dummy)
{
    i3214_baseport = base & 0xff;
    sim_printf("    i3214: at base port 0%02XH\n",
        i3214_baseport);
    reg_dev(i3214_do_mask, i3214_baseport, 0, 0);
    reg_dev(i3214_do_status, i3214_baseport + 1, 0, 0);
    reg_dev(i3214_cpu_bus_override, i3214_baseport + 2, 0, 0);
    reg_dev(i3214_monitor_do_boot, i3214_baseport + 3, 0, 0);
    return SCPE_OK;
}

t_stat i3214_clr(void)
{
    unreg_dev(i3214_baseport);
    unreg_dev(i3214_baseport + 1);
    unreg_dev(i3214_baseport + 2);
    unreg_dev(i3214_baseport + 3);
    i3214_baseport = -1;
    i3214_intnum = 0;
    i3214_verb = 0;
    return SCPE_OK;
}

// show configuration parameters

t_stat i3214_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "%s, Base port 0%04XH, Interrupt # %d, %s", 
        ((i3214_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        i3214_baseport, i3214_intnum, i3214_verb ? "Verbose" : "Quiet");
    return SCPE_OK;
}

/* Service routines to handle simulator functions */

/* i3214_svc - actually gets char & places in buffer */

t_stat i3214_svc (UNIT *uptr)
{
    return SCPE_OK;
}

/* Reset routine */

t_stat i3214_reset (DEVICE *dptr)
{
    uint8 devnum;
    
    for (devnum=0; devnum<1; devnum++) {
        i3214_reset_dev(devnum);
        sim_activate (&i3214_unit[devnum], i3214_unit[devnum].wait); /* activate unit */
    }
    return SCPE_OK;
}

t_stat i3214_reset_dev (uint8 devnum)
{
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

// 3214 device routines

uint8 i3214_do_mask(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0)                        /* read status port */
        return i3214_mask;
    else
        i3214_mask = data;
    return 0;
}

uint8 i3214_do_status(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0)
        return 0;
    else {
        i3214_cnt--;
    }
    return 0;
}

uint8 i3214_cpu_bus_override(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0)                        /* read status port */
        return 0;
    else
        BUS_OVERRIDE = data & 0x01;
    return 0;
}

uint8 i3214_monitor_do_boot(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0)                        /* read status port */
        return monitor_boot;
    else
        monitor_boot = data;
    return 0;
}

/* end of i3214.c */
