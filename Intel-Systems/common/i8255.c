/*  i8255.c: Intel i8255 PIO adapter

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

        These functions support a simulated i8255 interface device on an iSBC.
        The device has threee physical 8-bit I/O ports which could be connected
        to any parallel I/O device.

        All I/O is via programmed I/O.  The i8255 has a control port (PIOS)
        and three data ports (PIOA, PIOB, and PIOC).    

        The simulated device supports a select from I/O space and two address lines. 
        The data ports are at the lower addresses and the control port is at
        the highest.
        
        A write to the control port can configure the device:

        Control Word
        +---+---+---+---+---+---+---+---+
        | D7  D6  D5  D4  D3  D2  D1  D0|
        +---+---+---+---+---+---+---+---+

            Group B
            D0  Port C (lower) 1-Input, 0-Output
            D1  Port B 1-Input, 0-Output
            D2  Mode Selection  0-Mode 0, 1-Mode 1
                                
            Group A
            D3  Port C (upper) 1-Input, 0-Output
            D4  Port A 1-Input, 0-Output
            D5-6  Mode Selection  00-Mode 0, 01-Mode 1, 1X-Mode 2

            D7  Mode Set Flag 1=Active, 0=Bit Set

            Mode 0 - Basic Input/Output
            Mode 1 - Strobed Input/Output
            Mode 2 - Bidirectional Bus

            Bit Set - D7=0, D3:1 select port C bit, D0 1=set, 0=reset

        A read to the data ports gets the current port value, a write
        to the data ports writes the character to the device.  

        This program simulates up to 4 i8255 devices.  It handles 2 i8255 
        devices on the iSBC 80/10 SBC.  Other devices could be on other 
        multibus boards in the simulated system.
*/

#include "system_defs.h"                /* system header in system dir */
#define i8255_DEV    4                  /* number of devices */

/* function prototypes */

int32 i8255s0(int32 io, int32 data);    /* i8255 0 */
int32 i8255a0(int32 io, int32 data);
int32 i8255b0(int32 io, int32 data);
int32 i8255c0(int32 io, int32 data);
int32 i8255s1(int32 io, int32 data);    /* i8255 1 */
int32 i8255a1(int32 io, int32 data);
int32 i8255b1(int32 io, int32 data);
int32 i8255c1(int32 io, int32 data);
int32 i8255s2(int32 io, int32 data);    /* i8255 2 */
int32 i8255a2(int32 io, int32 data);
int32 i8255b2(int32 io, int32 data);
int32 i8255c2(int32 io, int32 data);
int32 i8255s3(int32 io, int32 data);    /* i8255 3 */
int32 i8255a3(int32 io, int32 data);
int32 i8255b3(int32 io, int32 data);
int32 i8255c3(int32 io, int32 data);
t_stat i8255_reset (DEVICE *dptr, int32 base);

/* external function prototypes */

extern int32 reg_dev(int32 (*routine)(int32, int32), int32 port);

/* globals */

int32 i8255_cnt = 0;
uint8 i8255_base[i8255_DEV];

/* i8255 Standard I/O Data Structures */

UNIT i8255_unit[] = {
    { UDATA (0, 0, 0) },                /* i8255 0 */
    { UDATA (0, 0, 0) },                /* i8255 1 */
    { UDATA (0, 0, 0) },                /* i8255 2 */
    { UDATA (0, 0, 0) }                 /* i8255 3 */ 
};

DEBTAB i8255_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

REG i8255_reg[] = {
    { HRDATA (CONTROL0, i8255_unit[0].u3, 8) }, /* i8255 0 */
    { HRDATA (PORTA0, i8255_unit[0].u4, 8) },
    { HRDATA (PORTB0, i8255_unit[0].u5, 8) },
    { HRDATA (PORTC0, i8255_unit[0].u6, 8) },
    { HRDATA (CONTROL1, i8255_unit[1].u3, 8) }, /* i8255 1 */
    { HRDATA (PORTA1, i8255_unit[1].u4, 8) },
    { HRDATA (PORTB1, i8255_unit[1].u5, 8) },
    { HRDATA (PORTC1, i8255_unit[1].u6, 8) },
    { HRDATA (CONTROL1, i8255_unit[2].u3, 8) }, /* i8255 2 */
    { HRDATA (PORTA1, i8255_unit[2].u4, 8) },
    { HRDATA (PORTB1, i8255_unit[2].u5, 8) },
    { HRDATA (PORTC1, i8255_unit[2].u6, 8) },
    { HRDATA (CONTROL1, i8255_unit[3].u3, 8) }, /* i8255 3 */
    { HRDATA (PORTA1, i8255_unit[3].u4, 8) },
    { HRDATA (PORTB1, i8255_unit[3].u5, 8) },
    { HRDATA (PORTC1, i8255_unit[3].u6, 8) },
    { NULL }
};

DEVICE i8255_dev = {
    "8255",             //name
    i8255_unit,         //units
    i8255_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    32,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &i8255_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8255_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* i8255 0 functions */

int32 i8255s0(int32 io, int32 data)
{
    int32 bit;

    if (io == 0) {                      /* read status port */
        return i8255_unit[0].u3;
    } else {                            /* write status port */
        if (data & 0x80) {              /* mode instruction */
            i8255_unit[0].u3 = data;
            sim_printf("8255-0: Mode Instruction=%02X\n", data);
            if (data & 0x64)
                sim_printf("   Mode 1 and 2 not yet implemented\n");
        } else {                        /* bit set */
            bit = (data & 0x0E) >> 1;   /* get bit number */
            if (data & 0x01) {          /* set bit */
                i8255_unit[0].u6 |= (0x01 << bit);
            } else {                    /* reset bit */
                i8255_unit[0].u6 &= ~(0x01 << bit);
            }
        }
    }
    return 0;
}

int32 i8255a0(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[0].u4);
    } else {                            /* write data port */
        i8255_unit[0].u4 = data;
        sim_printf("8255-0: Port A = %02X\n", data);
    }
    return 0;
}

int32 i8255b0(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[0].u5);
    } else {                            /* write data port */
        i8255_unit[0].u5 = data;
        sim_printf("8255-0: Port B = %02X\n", data);
    }
    return 0;
}

int32 i8255c0(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[0].u6);
    } else {                            /* write data port */
        i8255_unit[0].u6 = data;
        sim_printf("8255-0: Port C = %02X\n", data);
    }
    return 0;
}

/* i8255 1 functions */

int32 i8255s1(int32 io, int32 data)
{
    int32 bit;

    if (io == 0) {                      /* read status port */
        return i8255_unit[1].u3;
    } else {                            /* write status port */
        if (data & 0x80) {              /* mode instruction */
            i8255_unit[1].u3 = data;
            sim_printf("8255-1: Mode Instruction=%02X\n", data);
            if (data & 0x64)
                sim_printf("   Mode 1 and 2 not yet implemented\n");
        } else {                        /* bit set */
            bit = (data & 0x0E) >> 1;   /* get bit number */
            if (data & 0x01) {          /* set bit */
                i8255_unit[1].u6 |= (0x01 << bit);
            } else {                    /* reset bit */
                i8255_unit[1].u6 &= ~(0x01 << bit);
            }
        }
    }
    return 0;
}

int32 i8255a1(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[1].u4);
    } else {                            /* write data port */
        i8255_unit[1].u4 = data;
        sim_printf("8255-1: Port A = %02X\n", data);
    }
    return 0;
}

int32 i8255b1(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[1].u5);
    } else {                            /* write data port */
        i8255_unit[1].u5 = data;
        sim_printf("8255-1: Port B = %02X\n", data);
    }
    return 0;
}

int32 i8255c1(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[1].u6);
    } else {                            /* write data port */
        i8255_unit[1].u6 = data;
        sim_printf("8255-1: Port C = %02X\n", data);
    }
    return 0;
}

/* i8255 2 functions */

int32 i8255s2(int32 io, int32 data)
{
    int32 bit;

    if (io == 0) {                      /* read status port */
        return i8255_unit[2].u3;
    } else {                            /* write status port */
        if (data & 0x80) {              /* mode instruction */
            i8255_unit[2].u3 = data;
            sim_printf("8255-2: Mode Instruction=%02X\n", data);
            if (data & 0x64)
                sim_printf("   Mode 1 and 2 not yet implemented\n");
        } else {                        /* bit set */
            bit = (data & 0x0E) >> 1;   /* get bit number */
            if (data & 0x01) {          /* set bit */
                i8255_unit[2].u6 |= (0x01 << bit);
            } else {                    /* reset bit */
                i8255_unit[2].u6 &= ~(0x01 << bit);
            }
        }
    }
    return 0;
}

int32 i8255a2(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[2].u4);
    } else {                            /* write data port */
        i8255_unit[2].u4 = data;
        sim_printf("8255-2: Port A = %02X\n", data);
    }
    return 0;
}

int32 i8255b2(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[2].u5);
    } else {                            /* write data port */
        i8255_unit[2].u5 = data;
        sim_printf("8255-2: Port B = %02X\n", data);
    }
    return 0;
}

int32 i8255c2(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[2].u6);
    } else {                            /* write data port */
        i8255_unit[2].u6 = data;
        sim_printf("8255-2: Port C = %02X\n", data);
    }
    return 0;
}

/* i8255 3 functions */

int32 i8255s3(int32 io, int32 data)
{
    int32 bit;

    if (io == 0) {                      /* read status port */
        return i8255_unit[3].u3;
    } else {                            /* write status port */
        if (data & 0x80) {              /* mode instruction */
            i8255_unit[3].u3 = data;
            sim_printf("8255-3: Mode Instruction=%02X\n", data);
            if (data & 0x64)
                sim_printf("\n   Mode 1 and 2 not yet implemented\n");
        } else {                        /* bit set */
            bit = (data & 0x0E) >> 1;   /* get bit number */
            if (data & 0x01) {          /* set bit */
                i8255_unit[3].u6 |= (0x01 << bit);
            } else {                    /* reset bit */
                i8255_unit[3].u6 &= ~(0x01 << bit);
            }
        }
    }
    return 0;
}

int32 i8255a3(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[3].u4);
    } else {                            /* write data port */
        i8255_unit[3].u4 = data;
        sim_printf("8255-3: Port A = %02X\n", data);
    }
    return 0;
}

int32 i8255b3(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[3].u5);
    } else {                            /* write data port */
        i8255_unit[3].u5 = data;
        sim_printf("8255-3: Port B = %02X\n", data);
    }
    return 0;
}

int32 i8255c3(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        return (i8255_unit[3].u6);
    } else {                            /* write data port */
        i8255_unit[3].u6 = data;
        sim_printf("8255-3: Port C = %02X\n", data);
    }
    return 0;
}

/* Reset routine */

t_stat i8255_reset (DEVICE *dptr, int32 base)
{
    switch (i8255_cnt) {
    case 0:
        reg_dev(i8255a0, base); 
        reg_dev(i8255b0, base + 1); 
        reg_dev(i8255c0, base + 2); 
        reg_dev(i8255s0, base + 3); 
        i8255_unit[0].u3 = 0x9B; /* control */
        i8255_unit[0].u4 = 0xFF; /* Port A */
        i8255_unit[0].u5 = 0xFF; /* Port B */
        i8255_unit[0].u6 = 0xFF; /* Port C */
        sim_printf("   8255-0: Reset\n");
        break;
    case 1:
        reg_dev(i8255a1, base); 
        reg_dev(i8255b1, base + 1); 
        reg_dev(i8255c1, base + 2); 
        reg_dev(i8255s1, base + 3); 
        i8255_unit[1].u3 = 0x9B; /* control */
        i8255_unit[1].u4 = 0xFF; /* Port A */
        i8255_unit[1].u5 = 0xFF; /* Port B */
        i8255_unit[1].u6 = 0xFF; /* Port C */
        sim_printf("   8255-1: Reset\n");
        break;
    case 2:
        reg_dev(i8255a2, base); 
        reg_dev(i8255b2, base + 1); 
        reg_dev(i8255c2, base + 2); 
        reg_dev(i8255s2, base + 3); 
        i8255_unit[2].u3 = 0x9B; /* control */
        i8255_unit[2].u4 = 0xFF; /* Port A */
        i8255_unit[2].u5 = 0xFF; /* Port B */
        i8255_unit[2].u6 = 0xFF; /* Port C */
        sim_printf("   8255-2: Reset\n");
        break;
    case 3:
        reg_dev(i8255a3, base); 
        reg_dev(i8255b3, base + 1); 
        reg_dev(i8255c3, base + 2); 
        reg_dev(i8255s3, base + 3); 
        i8255_unit[3].u3 = 0x9B; /* control */
        i8255_unit[3].u4 = 0xFF; /* Port A */
        i8255_unit[3].u5 = 0xFF; /* Port B */
        i8255_unit[3].u6 = 0xFF; /* Port C */
        sim_printf("   8255-3: Reset\n");
        break;
    default:
        sim_printf("   8255: Bad device\n");
    }
    sim_printf("   8255-%d: Registered at %02X\n", i8255_cnt, base);
    i8255_cnt++;
    return SCPE_OK;
}

/* end of i8255.c */
