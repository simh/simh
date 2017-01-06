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
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set baseport and size.
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

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

/* function prototypes */

t_stat i8255_reset (DEVICE *dptr, uint16 baseport);
uint8 i8255_get_dn(void);
uint8 i8255s(t_bool io, uint8 data);
uint8 i8255a(t_bool io, uint8 data);
uint8 i8255b(t_bool io, uint8 data);
uint8 i8255c(t_bool io, uint8 data);

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);

/* globals */

int32 i8255_devnum = 0;                 //actual number of 8255 instances + 1
uint16 i8255_port[4];                   //baseport port registered to each instance

/* these bytes represent the input and output to/from a port instance */

uint8 i8255_A[4];                       //port A byte I/O
uint8 i8255_B[4];                       //port B byte I/O
uint8 i8255_C[4];                       //port C byte I/O

/* i8255 Standard I/O Data Structures */
/* up to 4 i8255 devices */

UNIT i8255_unit[] = {
    { UDATA (0, 0, 0) },                /* i8255 0 */
    { UDATA (0, 0, 0) },                /* i8255 1 */
    { UDATA (0, 0, 0) },                /* i8255 2 */
    { UDATA (0, 0, 0) }                 /* i8255 3 */ 
};

REG i8255_reg[] = {
    { HRDATA (CS0, i8255_unit[0].u3, 8) }, /* i8255 0 */
    { HRDATA (A0, i8255_A[0], 8) },
    { HRDATA (B0, i8255_B[0], 8) },
    { HRDATA (C0, i8255_C[0], 8) },
    { HRDATA (CS1, i8255_unit[1].u3, 8) }, /* i8255 1 */
    { HRDATA (A1, i8255_A[1], 8) },
    { HRDATA (B1, i8255_B[1], 8) },
    { HRDATA (C1, i8255_C[1], 8) },
    { HRDATA (CS2, i8255_unit[2].u3, 8) }, /* i8255 2 */
    { HRDATA (A2, i8255_A[2], 8) },
    { HRDATA (B2, i8255_B[2], 8) },
    { HRDATA (C2, i8255_C[2], 8) },
    { HRDATA (CS3, i8255_unit[3].u3, 8) }, /* i8255 3 */
    { HRDATA (A3, i8255_A[3], 8) },
    { HRDATA (B3, i8255_B[3], 8) },
    { HRDATA (C3, i8255_C[3], 8) },
    { NULL }
};

DEBTAB i8255_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8255_dev = {
    "8255",             //name
    i8255_unit,         //units
    i8255_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
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

/* Reset routine */

t_stat i8255_reset (DEVICE *dptr, uint16 baseport)
{
    if (i8255_devnum > I8255_NUM) {
        sim_printf("i8255_reset: too many devices!\n");
        return SCPE_MEM;
    }
    sim_printf("   8255-%d: Reset\n", i8255_devnum);
    sim_printf("   8255-%d: Registered at %04X\n", i8255_devnum, baseport);
    i8255_port[i8255_devnum] = baseport;
    reg_dev(i8255a, baseport, i8255_devnum); 
    reg_dev(i8255b, baseport + 1, i8255_devnum); 
    reg_dev(i8255c, baseport + 2, i8255_devnum); 
    reg_dev(i8255s, baseport + 3, i8255_devnum); 
    i8255_unit[i8255_devnum].u3 = 0x9B; /* control */
    i8255_A[i8255_devnum] = 0xFF; /* Port A */
    i8255_B[i8255_devnum] = 0xFF; /* Port B */
    i8255_C[i8255_devnum] = 0xFF; /* Port C */
    i8255_devnum++;
    return SCPE_OK;
}

uint8 i8255_get_dn(void)
{
    int i;

    for (i=0; i<I8255_NUM; i++)
        if (port >=i8255_port[i] && port <= i8255_port[i] + 3)
            return i;
    sim_printf("i8255_get_dn: port %04X not in 8255 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* i8255 functions */

uint8 i8255s(t_bool io, uint8 data)
{
    uint8 bit;
    uint8 devnum;

    if ((devnum = i8255_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read status port */
            return i8255_unit[devnum].u3;
        } else {                            /* write status port */
            if (data & 0x80) {              /* mode instruction */
                i8255_unit[devnum].u3 = data;
                sim_printf("   8255-%d: Mode Instruction=%02X\n", devnum, data);
                if (data & 0x64)
                    sim_printf("   Mode 1 and 2 not yet implemented\n");
            } else {                        /* bit set */
                bit = (data & 0x0E) >> 1;   /* get bit number */
                if (data & 0x01) {          /* set bit */
                    i8255_C[devnum] |= (0x01 << bit);
                } else {                    /* reset bit */
                    i8255_C[devnum] &= ~(0x01 << bit);
                }
            }
        }
    }
    return 0;
}

uint8 i8255a(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8255_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            //return (i8255_unit[devnum].u4);
            return (i8255_A[devnum]);
        } else {                            /* write data port */
            i8255_A[devnum] = data;
            sim_printf("   8255-%d: Port A = %02X\n", devnum, data);
        }
    }
    return 0;
}

uint8 i8255b(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8255_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            return (i8255_B[devnum]);
        } else {                            /* write data port */
            i8255_B[devnum] = data;
            sim_printf("   8255-%d: Port B = %02X\n", devnum, data);
        }
    }
    return 0;
}

uint8 i8255c(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8255_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            return (i8255_C[devnum]);
        } else {                            /* write data port */
            i8255_C[devnum] = data;
            sim_printf("   8255-%d: Port C = %02X\n", devnum, data);
        }
    }
    return 0;
}

/* end of i8255.c */
