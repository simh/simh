/*  i8259.c: Intel i8259 PIC adapter

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

    NOTES:

        This software was written by Bill Beech, 24 Jan 13, to allow emulation of 
        more complex Multibus Computer Systems.

        This program simulates up to 2 i8259 devices.  It handles 1 i8259 
        device on the iSBC 80/20 and iSBC 80/30 SBCs.  Other devices could be on 
        other multibus boards in the simulated system.
*/

#include "system_defs.h"                /* system header in system dir */

/* function prototypes */

uint8 i8259a(t_bool io, uint8 data, uint8 devnum);
uint8 i8259b(t_bool io, uint8 data, uint8 devnum);
void i8259_dump(uint8 devnum);
t_stat i8259_reset (DEVICE *dptr, uint16 base, uint8 devnum);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16 port, uint8 devnum);

/* globals */

uint8 i8259_base[I8259_NUM];
uint8 i8259_icw1[I8259_NUM];
uint8 i8259_icw2[I8259_NUM];
uint8 i8259_icw3[I8259_NUM];
uint8 i8259_icw4[I8259_NUM];
uint8 i8259_ocw1[I8259_NUM];
uint8 i8259_ocw2[I8259_NUM];
uint8 i8259_ocw3[I8259_NUM];
uint8 icw_num0 = 1, icw_num1 = 1;

/* i8255 Standard I/O Data Structures */
/* up to 2 i8259 devices */

UNIT i8259_unit[] = {
    { UDATA (0, 0, 0) },                /* i8259 0 */
    { UDATA (0, 0, 0) }                 /* i8259 1 */
};

REG i8259_reg[] = {
    { HRDATA (IRR0, i8259_unit[0].u3, 8) }, /* i8259 0 */
    { HRDATA (ISR0, i8259_unit[0].u4, 8) },
    { HRDATA (IMR0, i8259_unit[0].u5, 8) },
    { HRDATA (IRR1, i8259_unit[1].u3, 8) }, /* i8259 0 */
    { HRDATA (ISR1, i8259_unit[1].u4, 8) },
    { HRDATA (IMR1, i8259_unit[1].u5, 8) },
    { NULL }
};

DEBTAB i8259_debug[] = {
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

DEVICE i8259_dev = {
    "8259",             //name
    i8259_unit,         //units
    i8259_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &i8259_reset,       //reset
    NULL,               //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8259_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* i8259 functions */

uint8 i8259a(t_bool io, uint8 data, uint8 devnum)
{
    if (devnum >= I8259_NUM) {
        sim_printf("8259a: Illegal Device Number %d\n", devnum);
        return 0;
    }
    if (io == 0) {                      /* read data port */
        if ((i8259_ocw3[devnum] & 0x03) == 0x02)
            return (i8259_unit[devnum].u3);  /* IRR */
        if ((i8259_ocw3[devnum] & 0x03) == 0x03)
            return (i8259_unit[devnum].u4);  /* ISR */
    } else {                            /* write data port */
        if (data & 0x10) {
            icw_num0 = 1;
        }
        if (icw_num0 == 1) {
            i8259_icw1[devnum] = data;       /* ICW1 */
            i8259_unit[devnum].u5 = 0x00;    /* clear IMR */
            i8259_ocw3[devnum] = 0x02;       /* clear OCW3, Sel IRR */
        } else {
            switch (data & 0x18) {
            case 0:                     /* OCW2 */
                i8259_ocw2[devnum] = data;
                break;
            case 8:                     /* OCW3 */
                i8259_ocw3[devnum] = data;
                break;
            default:
                sim_printf("8259a-%d: OCW Error %02X\n", devnum, data);
                break;
            }
        }
        sim_printf("8259a-%d: data = %02X\n", devnum, data);
                icw_num0++;             /* step ICW number */
    }
    i8259_dump(devnum);
    return 0;
}

uint8 i8259b(t_bool io, uint8 data, uint8 devnum)
{
    if (devnum >= I8259_NUM) {
        sim_printf("8259b: Illegal Device Number %d\n", devnum);
        return 0;
    }
    if (io == 0) {                      /* read data port */
        if ((i8259_ocw3[devnum] & 0x03) == 0x02)
            return (i8259_unit[devnum].u3);  /* IRR */
        if ((i8259_ocw3[devnum] & 0x03) == 0x03)
            return (i8259_unit[devnum].u4);  /* ISR */
    } else {                            /* write data port */
        if (data & 0x10) {
            icw_num1 = 1;
        }
        if (icw_num1 == 1) {
            i8259_icw1[devnum] = data;       /* ICW1 */
            i8259_unit[devnum].u5 = 0x00;    /* clear IMR */
            i8259_ocw3[devnum] = 0x02;       /* clear OCW3, Sel IRR */
        } else {
            switch (data & 0x18) {
            case 0:                     /* OCW2 */
                i8259_ocw2[devnum] = data;
                break;
            case 8:                     /* OCW3 */
                i8259_ocw3[devnum] = data;
                break;
            default:
                sim_printf("8259b-%d: OCW Error %02X\n", devnum, data);
                break;
            }
        }
        sim_printf("8259b-%d: data = %02X\n", devnum, data);
        icw_num1++;                     /* step ICW number */
    }
    i8259_dump(devnum);
    return 0;
}

void i8259_dump(uint8 devnum)
{
    sim_printf("Device %d\n", devnum);
    sim_printf("   IRR = %02X\n", i8259_unit[devnum].u3);
    sim_printf("   ISR = %02X\n", i8259_unit[devnum].u4);
    sim_printf("   IMR = %02X\n", i8259_unit[devnum].u5);
    sim_printf("   ICW1 = %02X\n", i8259_icw1[devnum]);
    sim_printf("   ICW2 = %02X\n", i8259_icw2[devnum]);
    sim_printf("   ICW3 = %02X\n", i8259_icw3[devnum]);
    sim_printf("   ICW4 = %02X\n", i8259_icw4[devnum]);
    sim_printf("   OCW1 = %02X\n", i8259_ocw1[devnum]);
    sim_printf("   OCW2 = %02X\n", i8259_ocw2[devnum]);
    sim_printf("   OCW3 = %02X\n", i8259_ocw3[devnum]);
}

/* Reset routine */

t_stat i8259_reset (DEVICE *dptr, uint16 base, uint8 devnum)
{
    if (devnum >= I8259_NUM) {
        sim_printf("8259_reset: Illegal Device Number %d\n", devnum);
        return 0;
    }
    reg_dev(i8259a, base, devnum); 
    reg_dev(i8259b, base + 1, devnum); 
    i8259_unit[devnum].u3 = 0x00; /* IRR */
    i8259_unit[devnum].u4 = 0x00; /* ISR */
    i8259_unit[devnum].u5 = 0x00; /* IMR */
    sim_printf("   8259-%d: Reset\n", devnum);
    sim_printf("   8259-%d: Registered at %04X\n", devnum, base);
    return SCPE_OK;
}

/* end of i8259.c */
