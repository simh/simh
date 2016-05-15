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
#define i8259_DEV       2               /* number of devices */

/* function prototypes */

int32 i8259a0(int32 io, int32 data);
int32 i8259b0(int32 io, int32 data);
int32 i8259a1(int32 io, int32 data);
int32 i8259b1(int32 io, int32 data);
void i8259_dump(int32 dev);
t_stat i8259_reset (DEVICE *dptr, int32 base);

/* external function prototypes */

extern int32 reg_dev(int32 (*routine)(int32, int32), int32 port);

/* globals */

int32 i8259_cnt = 0;
uint8 i8259_base[i8259_DEV];
uint8 i8259_icw1[i8259_DEV];
uint8 i8259_icw2[i8259_DEV];
uint8 i8259_icw3[i8259_DEV];
uint8 i8259_icw4[i8259_DEV];
uint8 i8259_ocw1[i8259_DEV];
uint8 i8259_ocw2[i8259_DEV];
uint8 i8259_ocw3[i8259_DEV];
int32 icw_num0 = 1, icw_num1 = 1;

/* i8255 Standard I/O Data Structures */

UNIT i8259_unit[] = {
    { UDATA (0, 0, 0) },                /* i8259 0 */
    { UDATA (0, 0, 0) }                 /* i8259 1 */
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

REG i8259_reg[] = {
    { HRDATA (IRR0, i8259_unit[0].u3, 8) }, /* i8259 0 */
    { HRDATA (ISR0, i8259_unit[0].u4, 8) },
    { HRDATA (IMR0, i8259_unit[0].u5, 8) },
    { HRDATA (IRR1, i8259_unit[1].u3, 8) }, /* i8259 0 */
    { HRDATA (ISR1, i8259_unit[1].u4, 8) },
    { HRDATA (IMR1, i8259_unit[1].u5, 8) },
    { NULL }
};

DEVICE i8259_dev = {
    "8259",             //name
    i8259_unit,         //units
    i8259_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    32,                 //awidth
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

/* i8259 0 functions */

int32 i8259a0(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        if ((i8259_ocw3[0] & 0x03) == 0x02)
            return (i8259_unit[0].u3);  /* IRR */
        if ((i8259_ocw3[0] & 0x03) == 0x03)
            return (i8259_unit[0].u4);  /* ISR */
    } else {                            /* write data port */
        if (data & 0x10) {
            icw_num0 = 1;
        }
        if (icw_num0 == 1) {
            i8259_icw1[0] = data;       /* ICW1 */
            i8259_unit[0].u5 = 0x00;    /* clear IMR */
            i8259_ocw3[0] = 0x02;       /* clear OCW3, Sel IRR */
        } else {
            switch (data & 0x18) {
            case 0:                     /* OCW2 */
                i8259_ocw2[0] = data;
                break;
            case 8:                     /* OCW3 */
                i8259_ocw3[0] = data;
                break;
            default:
                sim_printf("8259b-0: OCW Error %02X\n", data);
                break;
            }
        }
        sim_printf("8259a-0: data = %02X\n", data);
                icw_num0++;             /* step ICW number */
    }
    i8259_dump(0);
    return 0;
}

int32 i8259a1(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        if ((i8259_ocw3[1] & 0x03) == 0x02)
            return (i8259_unit[1].u3);  /* IRR */
        if ((i8259_ocw3[1] & 0x03) == 0x03)
            return (i8259_unit[1].u4);  /* ISR */
    } else {                            /* write data port */
        if (data & 0x10) {
            icw_num1 = 1;
        }
        if (icw_num1 == 1) {
            i8259_icw1[1] = data;       /* ICW1 */
            i8259_unit[1].u5 = 0x00;    /* clear IMR */
            i8259_ocw3[1] = 0x02;       /* clear OCW3, Sel IRR */
        } else {
            switch (data & 0x18) {
            case 0:                     /* OCW2 */
                i8259_ocw2[1] = data;
                break;
            case 8:                     /* OCW3 */
                i8259_ocw3[1] = data;
                break;
            default:
                sim_printf("8259b-1: OCW Error %02X\n", data);
                break;
            }
        }
        sim_printf("8259a-1: data = %02X\n", data);
        icw_num1++;                     /* step ICW number */
    }
    i8259_dump(1);
    return 0;
}

/* i8259 1 functions */

int32 i8259b0(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8259_unit[0].u5);      /* IMR */
    } else {                            /* write data port */
        if (icw_num0 >= 2 && icw_num0 < 5) { /* ICW mode */                  
            switch (icw_num0) {
            case 2:                     /* ICW2 */
                i8259_icw2[0] = data;
                break;
            case 3:                     /* ICW3 */
                i8259_icw3[0] = data;
                break;
            case 4:                     /* ICW4 */
                if (i8259_icw1[0] & 0x01)
                    i8259_icw4[0] = data;
                else
                    sim_printf("8259b-0: ICW4 not enabled - data=%02X\n", data);
                break;
            default:
                sim_printf("8259b-0: ICW Error %02X\n", data);
                break;
            }
            icw_num0++;
        } else {
            i8259_ocw1[0] = data;       /* OCW0 */
        }
    }
    i8259_dump(0);
    return 0;
}

int32 i8259b1(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8259_unit[1].u5);              /* IMR */
    } else {                            /* write data port */
        if (icw_num1 >= 2 && icw_num1 < 5) { /* ICW mode */                  
            switch (icw_num1) {
            case 2:                     /* ICW2 */
                i8259_icw2[1] = data;
                break;
            case 3:                     /* ICW3 */
                i8259_icw3[1] = data;
                break;
            case 4:                     /* ICW4 */
                if (i8259_icw1[1] & 0x01)
                    i8259_icw4[1] = data;
                else
                    sim_printf("8259b-1: ICW4 not enabled - data=%02X\n", data);
                break;
            default:
                sim_printf("8259b-1: ICW Error %02X\n", data);
                break;
            }
            icw_num1++;
        } else {
            i8259_ocw1[1] = data;       /* OCW0 */
        }
    }
    i8259_dump(1);
    return 0;
}

void i8259_dump(int32 dev)
{
    sim_printf("Device %d\n", dev);
    sim_printf("   IRR = %02X\n", i8259_unit[dev].u3);
    sim_printf("   ISR = %02X\n", i8259_unit[dev].u4);
    sim_printf("   IMR = %02X\n", i8259_unit[dev].u5);
    sim_printf("   ICW1 = %02X\n", i8259_icw1[dev]);
    sim_printf("   ICW2 = %02X\n", i8259_icw2[dev]);
    sim_printf("   ICW3 = %02X\n", i8259_icw3[dev]);
    sim_printf("   ICW4 = %02X\n", i8259_icw4[dev]);
    sim_printf("   OCW1 = %02X\n", i8259_ocw1[dev]);
    sim_printf("   OCW2 = %02X\n", i8259_ocw2[dev]);
    sim_printf("   OCW3 = %02X\n", i8259_ocw3[dev]);
}

/* Reset routine */

t_stat i8259_reset (DEVICE *dptr, int32 base)
{
    switch (i8259_cnt) {
    case 0:
        reg_dev(i8259a0, base); 
        reg_dev(i8259b0, base + 1); 
        reg_dev(i8259a0, base + 2); 
        reg_dev(i8259b0, base + 3); 
        i8259_unit[0].u3 = 0x00; /* IRR */
        i8259_unit[0].u4 = 0x00; /* ISR */
        i8259_unit[0].u5 = 0x00; /* IMR */
        sim_printf("   8259-0: Reset\n");
        break;
    case 1:
        reg_dev(i8259a1, base); 
        reg_dev(i8259b1, base + 1); 
        reg_dev(i8259a1, base + 2); 
        reg_dev(i8259b1, base + 3); 
        i8259_unit[1].u3 = 0x00; /* IRR */
        i8259_unit[1].u4 = 0x00; /* ISR */
        i8259_unit[1].u5 = 0x00; /* IMR */
        sim_printf("   8259-1: Reset\n");
        break;
    default:
        sim_printf("   8259: Bad device\n");
        break;
    }
    sim_printf("   8259-%d: Registered at %02X\n", i8259_cnt, base);
    i8259_cnt++;
    return SCPE_OK;
}

/* end of i8259.c */
