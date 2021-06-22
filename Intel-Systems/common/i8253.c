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

#define i8253_NAME    "Intel i8253 PIT Chip"

/* external globals */

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint16, uint8);
extern uint8 unreg_dev(uint16);

/* globals */

static const char* i8253_desc(DEVICE *dptr) {
    return i8253_NAME;
}
int     i8253_num = 0;
int     i8253_baseport[] = { -1, -1, -1, -1 }; //base port
uint8   i8253_intnum[4] = { 0, 0, 0, 0 }; //interrupt number
uint8   i8253_verb[4] = { 0, 0, 0, 0 }; //verbose flag
/* function prototypes */

t_stat i8253_cfg(uint16 base, uint16 devnum, uint8 dummy);
t_stat i8253_clr(void);
t_stat i8253_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
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
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, i8253_show_param, NULL, 
        "show configured parametes for i8253" },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8253_dev = {
    "I8253",            //name
    i8253_unit,         //units
    i8253_reg,          //registers
    i8253_mod,          //modifiers
    I8253_NUM,         //numunits
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
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl
    i8253_debug,        //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &i8253_desc         //device description
};

/* Service routines to handle simulator functions */

// i8253 configuration

t_stat i8253_cfg(uint16 base, uint16 devnum, uint8 dummy)
{
    i8253_baseport[devnum] = base & 0xff;
    sim_printf("    i8253%d: installed at base port 0%02XH\n",
        devnum, i8253_baseport[devnum]);
    reg_dev(i8253t0, i8253_baseport[devnum], devnum, 0); 
    reg_dev(i8253t1, i8253_baseport[devnum] + 1, devnum, 0); 
    reg_dev(i8253t2, i8253_baseport[devnum] + 2, devnum, 0); 
    reg_dev(i8253c, i8253_baseport[devnum] + 3, devnum, 0);
    i8253_num++;
    return SCPE_OK;
}

t_stat i8253_clr(void)
{
    int i;
    
    for (i=0; i<i8253_num; i++) {
        unreg_dev(i8253_baseport[i]); 
        unreg_dev(i8253_baseport[i] + 1); 
        unreg_dev(i8253_baseport[i] + 2); 
        unreg_dev(i8253_baseport[i] + 3);
        i8253_baseport[i] = -1;
        i8253_intnum[i] = 0;
        i8253_verb[i] = 0;
    }
    i8253_num = 0; 
    return SCPE_OK;
}

// show configuration parameters

t_stat i8253_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int i;
    
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "Device %s\n", ((i8253_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled");
    for (i=0; i<i8253_num; i++) {
        fprintf(st, "Unit %d at Base port ", i);
        fprintf(st, "0%02X ", i8253_baseport[i]);
        fprintf(st, "Interrupt # is ");
        fprintf(st, "%d ", i8253_intnum[i]);
        fprintf(st, "Mode ");
        fprintf(st, "%s", i8253_verb[i] ? "Verbose" : "Quiet");
        if (i<i8253_num && i8253_num != 1) fprintf(st, "\n");
    }
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
    
    for (devnum=0; devnum<i8253_num+1; devnum++) {
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

/* end of i8253.c */
