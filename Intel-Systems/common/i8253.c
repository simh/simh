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

uint8   i8253_T0_control_word[4];
uint8   i8253_T0_flag[4];
uint16  i8253_T0_load[4];
uint16  i8253_T0_latch[4];
uint16  i8253_T0_count[4];
int     i8253_T0_gate[4];
int     i8253_T0_out[4];
uint8   i8253_T1_control_word[4];
uint8   i8253_T1_flag[4];
uint16  i8253_T1_load[4];
uint16  i8253_T1_latch[4];
uint16  i8253_T1_count[4];
int     i8253_T1_gate[4];
int     i8253_T1_out[4];
uint8   i8253_T2_control_word[4];
uint8   i8253_T2_flag[4];
uint16  i8253_T2_load[4];
uint16  i8253_T2_latch[4];
uint16  i8253_T2_count[4];
int     i8253_T2_gate[4];
int     i8253_T2_out[4];

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
    { URDATAD(T0,i8253_unit[0].u3,16,8,0,4,0,"Timer 0") },
    { URDATAD(T1,i8253_unit[0].u4,16,8,0,4,0,"Timer 1") },
    { URDATAD(T2,i8253_unit[0].u5,16,8,0,4,0,"Timer 2") },
    { URDATAD(CMD,i8253_unit[0].u6,16,8,0,4,0,"Command") },
    { NULL }
};

DEBTAB i8253_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { NULL }
};

MTAB i8253_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, i8253_show_param, NULL, 
        "show configured parameters for i8253" },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8253_dev = {
    "I8253",            //name
    i8253_unit,         //units
    i8253_reg,          //registers
    i8253_mod,          //modifiers
    4,                  //numunits
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
    UNIT *uptr;

    uptr = i8253_dev.units;
    i8253_baseport[devnum] = base & BYTEMASK;
    sim_printf("    i8253%d: installed at base port 0%02XH\n",
        devnum, i8253_baseport[devnum]);
    reg_dev(i8253t0, i8253_baseport[devnum], devnum, 0); 
    reg_dev(i8253t1, i8253_baseport[devnum] + 1, devnum, 0); 
    reg_dev(i8253t2, i8253_baseport[devnum] + 2, devnum, 0); 
    reg_dev(i8253c, i8253_baseport[devnum] + 3, devnum, 0);
    uptr->u6 = i8253_num;
    i8253_num++;
    sim_activate (uptr, uptr->wait);    /* start poll */
    return SCPE_OK;
}

// i8253 unconfiguration

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

/* i8253_svc - actually does timing */

t_stat i8253_svc (UNIT *uptr)
{
    int devnum;
    
    if (uptr == NULL)
        return SCPE_ARG;
    devnum = uptr->u6;              //get devnum for unit
    switch (i8253_T0_control_word[devnum]) {
        case 0:                 //mode 0    
            break;
        case 1:                 //mode 1
            break;
        case 2:                 //mode 2 - rate generator
            if (i8253_T0_gate[devnum]) {
                i8253_T0_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x10) {
                    i8253_T0_count[devnum]--; //decrement counter
                    if (i8253_T0_count[devnum] == 0) { //if 0, do something
                        i8253_T0_out[devnum] = 1;
                        i8253_T0_count[devnum] = i8253_T0_load[devnum];
                    } else {
                        i8253_T0_out[devnum] = 1;
                    }
                }
            }
            break;
        case 3:                 //mode 3 - square wave rate generator
            if (i8253_T0_gate[devnum]) {
                i8253_T0_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x10) {
                    i8253_T0_count[devnum]--; //decrement counter
                    if (i8253_T0_count[devnum] == 0) { //if 0, do something
                        i8253_T0_out[devnum] = ~i8253_T0_out[devnum];
                        i8253_T0_count[devnum] = i8253_T0_load[devnum];
                    } else {
                        i8253_T0_out[devnum] = 1;
                    }
                }
            }
            break;
        case 4:                 //mode 4
            break;
        case 5:                 //mode 5              
            break;
    }
    switch (i8253_T1_control_word[devnum]) {
        case 0:                 //mode 0    
            break;
        case 1:                 //mode 1
            break;
        case 2:                 //mode 2 - rate generator
            if (i8253_T1_gate[devnum]) {
                i8253_T1_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x20) {
                    i8253_T1_count[devnum]--; //decrement counter
                    if (i8253_T1_count[devnum] == 0) { //if 0, do something
                        i8253_T1_out[devnum] = 1;
                        i8253_T1_count[devnum] = i8253_T1_load[devnum];
                    } else {
                        i8253_T1_out[devnum] = 1;
                    }
                }
            }
            break;
        case 3:                 //mode 3 - square wave rate generator
            if (i8253_T1_gate[devnum]) {
                i8253_T1_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x20) {
                    i8253_T1_count[devnum]--; //decrement counter
                    if (i8253_T1_count[devnum] == 0) { //if 0, do something
                        i8253_T1_out[devnum] = ~i8253_T1_out[devnum];
                        i8253_T1_count[devnum] = i8253_T1_load[devnum];
                    } else {
                        i8253_T1_out[devnum] = 1;
                    }
                }
            }
            break;
        case 4:                 //mode 4
            break;
        case 5:                 //mode 5              
            break;
    }
    switch (i8253_T2_control_word[devnum]) {
        case 0:                 //mode 0    
            break;
        case 1:                 //mode 1
            break;
        case 2:                 //mode 2 - rate generator
            if (i8253_T2_gate[devnum]) {
                i8253_T2_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x40) {
                    i8253_T2_count[devnum]--; //decrement counter
                    if (i8253_T2_count[devnum] == 0) { //if 0, do something
                        i8253_T2_out[devnum] = 1;
                        i8253_T2_count[devnum] = i8253_T2_load[devnum];
                    } else {
                        i8253_T2_out[devnum] = 1;
                    }
                }
            }
            break;
        case 3:                 //mode 3 - square wave rate generator
            if (i8253_T2_gate[devnum]) {
                i8253_T2_out[devnum] = 0;
                if (i8253_T0_flag[devnum] == 0x40) {
                    i8253_T2_count[devnum]--; //decrement counter
                    if (i8253_T2_count[devnum] == 0) { //if 0, do something
                        i8253_T2_out[devnum] = ~i8253_T2_out[devnum];
                        i8253_T2_count[devnum] = i8253_T2_load[devnum];
                    } else {
                        i8253_T2_out[devnum] = 1;
                    }
                }
            }
            break;
        case 4:                 //mode 4
            break;
        case 5:                 //mode 5              
            break;
    }
    sim_activate (uptr, uptr->wait);    /* continue poll */
    return SCPE_OK;
}

/* Reset routine */

t_stat i8253_reset (DEVICE *dptr)
{
    int i;

    for (i = 0; i < 4; i++)
        if (i < i8253_num)
            i8253_unit[i].flags = 0;
        else {
            sim_cancel (&i8253_unit[i]);
            i8253_unit[i].flags = UNIT_DIS;
        }
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8253t0(t_bool io, uint8 data, uint8 devnum)
{
    uint8 rl;

    rl = (i8253_T1_control_word[devnum] >> 4) & 0x03;
    if (io == 0) {                  /* read data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                return (i8253_T1_latch[devnum] >> 8);
                break;
            case 2:                 //read/load lsb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                return (i8253_T1_latch[devnum] & BYTEMASK);
                break;
            case 3:                 //read/load lsb then msb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
        }
        if ((i8253_T1_flag[devnum] & 0x01) == 0) {
            i8253_T1_flag[devnum] |= 0x01;
            return (i8253_T1_latch[devnum] & BYTEMASK);
        } else {
            i8253_T1_flag[devnum] &= 0xfe;
            return (i8253_T1_latch[devnum] >> 8);
        }
    } else {                        /* write data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T1_load[devnum] = (data << 8);
                i8253_T1_flag[devnum] |= 0x10;
                break;
            case 2:                 //read/load lsb
                i8253_T1_load[devnum] = data;
                i8253_T1_flag[devnum] |= 0x10;
                break;
            case 3:                 //read/load lsb then msb
                if ((i8253_T1_flag[devnum] & 0x01) == 0) {
                    i8253_T1_load[devnum] = data;
                    i8253_T1_flag[devnum] |= 0x01;
                } else {
                    i8253_T1_load[devnum] |= (data << 8);
                    i8253_T1_flag[devnum] &= 0xfe;
                    i8253_T1_flag[devnum] |= 0x10;
                }
                break;
        }
    }
    return 0;
}

uint8 i8253t1(t_bool io, uint8 data, uint8 devnum)
{
    uint8 rl;

    rl = (i8253_T1_control_word[devnum] >> 4) & 0x03;
    if (io == 0) {                  /* read data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                return (i8253_T1_latch[devnum] >> 8);
                break;
            case 2:                 //read/load lsb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                return (i8253_T1_latch[devnum] & BYTEMASK);
                break;
            case 3:                 //read/load lsb then msb
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
        }
        if ((i8253_T1_flag[devnum] & 0x02) == 0) {
            i8253_T1_flag[devnum] |= 0x02;
            return (i8253_T1_latch[devnum] & BYTEMASK);
        } else {
            i8253_T1_flag[devnum] &= 0xfd;
            return (i8253_T1_latch[devnum] >> 8);
        }
    } else {                        /* write data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T1_latch[devnum] = i8253_T1_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T1_load[devnum] = (data << 8);
                i8253_T1_flag[devnum] |= 0x20;
                break;
            case 2:                 //read/load lsb
                i8253_T1_load[devnum] = data;
                i8253_T1_flag[devnum] |= 0x20;
                break;
            case 3:                 //read/load lsb then msb
                if ((i8253_T1_flag[devnum] & 0x02) == 0) {
                    i8253_T1_load[devnum] = data;
                    i8253_T1_flag[devnum] |= 0x02;
                } else {
                    i8253_T1_load[devnum] |= (data << 8);
                    i8253_T1_flag[devnum] &= 0xfd;
                    i8253_T1_flag[devnum] |= 0x20;
                }
                break;
        }
    }
    return 0;
}

uint8 i8253t2(t_bool io, uint8 data, uint8 devnum)
{
    uint8 rl;

    rl = (i8253_T2_control_word[devnum] >> 4) & 0x03;
    if (io == 0) {                  /* read data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T2_latch[devnum] = i8253_T2_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T2_latch[devnum] = i8253_T2_count[devnum];
                return (i8253_T2_latch[devnum] >> 8);
                break;
            case 2:                 //read/load lsb
                i8253_T2_latch[devnum] = i8253_T2_count[devnum];
                return (i8253_T2_latch[devnum] & BYTEMASK);
                break;
            case 3:                 //read/load lsb then msb
                i8253_T2_latch[devnum] = i8253_T2_count[devnum];
                break;
        }
        if ((i8253_T2_flag[devnum] & 0x04) == 0) {
            i8253_T2_flag[devnum] |= 0x04;
            return (i8253_T2_latch[devnum] & BYTEMASK);
        }
        else {
            i8253_T2_flag[devnum] &= 0xfb;
            return (i8253_T2_latch[devnum] >> 8);
        }
    } else {                        /* write data port */
        switch (rl) {
            case 0:                 //counter latching
                i8253_T2_latch[devnum] = i8253_T2_count[devnum];
                break;
            case 1:                 //read/load msb
                i8253_T2_load[devnum] = (data << 8);
                i8253_T2_flag[devnum] |= 0x40;
                break;
            case 2:                 //read/load lsb
                i8253_T2_load[devnum] = data;
                i8253_T2_flag[devnum] |= 0x40;
                break;
            case 3:                 //read/load lsb then msb
                if ((i8253_T2_flag[devnum] & 0x04) == 0) {
                    i8253_T2_load[devnum] = data;
                    i8253_T2_flag[devnum] |= 0x04;
                } else {
                    i8253_T2_load[devnum] |= (data << 8);
                    i8253_T2_flag[devnum] &= 0xfb;
                    i8253_T2_flag[devnum] |= 0x40;
                }
                break;
        }
    }
    return 0;
}

uint8 i8253c(t_bool io, uint8 data, uint8 devnum)
{
    uint8 sc;

    if (io == 0) {                  /* read status port */
        return 0xff;
    } else {                        /* write data port */
        sc = (data >> 6) & 0x03;
        switch (sc) {
            case 0:
                i8253_T0_control_word[devnum] = data;
                i8253_T0_flag[devnum] = 0;
                break;
            case 1:
                i8253_T1_control_word[devnum] = data;
                i8253_T1_flag[devnum] = 0;
                break;
            case 2:
                i8253_T2_control_word[devnum] = data;
                i8253_T2_flag[devnum] = 0;
                break;
        }
    }
    return 0;
}

/* end of i8253.c */
