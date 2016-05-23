/*  iPATA.c: Intel i8255 PIO adapter for PATA HD

    Copyright (c) 2015, William A. Beech

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

        These functions support a simulated i8255 interface device on an iSBC.
        The device has threee physical 8-bit I/O ports which could be connected
        to any parallel I/O device. This is an extension of the i8255.c file to support
        an emulated PATA IDE Hard Disk Drive.

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

        The Second 8255 on the iSBC 80/10 is used to connect to the IDE PATA
        Hard Disk Drive.  Pins are defined as shown below:
        
            PA[0..7] High data byte
            PB[0..7] Low data byte
            PC[0..2] Register select
            PC[3..4] CSFX select
            PC[5]    Read register
            PC[6]    Write register
*/

#include "system_defs.h"

extern int32 reg_dev(int32 (*routine)(), int32 port);

/* function prototypes */

t_stat pata_reset (DEVICE *dptr, int32 base);

/* i8255 Standard I/O Data Structures */

UNIT pata_unit[] = {
    { UDATA (0, 0, 0) }
};

REG pata_reg[] = {
    { HRDATA (CONTROL0, pata_unit[0].u3, 8) },
    { HRDATA (PORTA0, pata_unit[0].u4, 8) },
    { HRDATA (PORTB0, pata_unit[0].u5, 8) },
    { HRDATA (PORTC0, pata_unit[0].u6, 8) },
    { NULL }
};

DEVICE pata_dev = {
    "PATA",             //name
    pata_unit,          //units
    pata_reg,           //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    32,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &pata_reset,       //reset
    NULL,               //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    NULL,               //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

int32 patas(int32 io, int32 data)
{
    int32 bit;

    if (io == 0) {                      /* read status port */
        return pata_unit[0].u3;
    } else {                            /* write status port */
        if (data & 0x80) {              /* mode instruction */
            pata_unit[0].u3 = data;
            sim_printf("PATA: 8255 Mode Instruction=%02X\n", data);
            if (data & 0x64)
                sim_printf("   Mode 1 and 2 not yet implemented\n");
        } else {                        /* bit set */
            bit = (data & 0x0E) >> 1;   /* get bit number */
            if (data & 0x01) {          /* set bit */
                pata_unit[0].u6 |= (0x01 << bit);
            } else {                    /* reset bit */
                pata_unit[0].u6 &= ~(0x01 << bit);
            }
        }
    }
    return 0;
}

int32 pataa(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        sim_printf("PATA: 8255 Read Port A = %02X\n", pata_unit[0].u4);
        return (pata_unit[0].u4);
    } else {                            /* write data port */
        pata_unit[0].u4 = data;
        sim_printf("PATA: 8255 Write Port A = %02X\n", data);
    }
    return 0;
}

int32 patab(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        sim_printf("PATA: 8255 Read Port B = %02X\n", pata_unit[0].u5);
        return (pata_unit[0].u5);
    } else {                            /* write data port */
        pata_unit[0].u5 = data;
        sim_printf("PATA: 8255 Write Port B = %02X\n", data);
    }
    return 0;
}

int32 patac(int32 io, int32 data)
{
    if (io == 0) {                      /* read data port */
        sim_printf("PATA: 8255 Read Port C = %02X\n", pata_unit[0].u6);
        return (pata_unit[0].u6);
    } else {                            /* write data port */
        pata_unit[0].u6 = data;
        sim_printf("PATA: 8255 Write Port C = %02X\n", data);
    }
    return 0;
}

/* Reset routine */

t_stat pata_reset (DEVICE *dptr, int32 base)
{
    pata_unit[0].u3 = 0x9B;         /* control */
    pata_unit[0].u4 = 0xFF;         /* Port A */
    pata_unit[0].u5 = 0xFF;         /* Port B */
    pata_unit[0].u6 = 0xFF;         /* Port C */
    reg_dev(pataa, base); 
    reg_dev(patab, base + 1); 
    reg_dev(patac, base + 2); 
    reg_dev(patas, base + 3); 
    sim_printf("   PATA: Reset\n");
    return SCPE_OK;
}

