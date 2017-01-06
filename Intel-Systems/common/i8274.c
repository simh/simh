/*  i8274.c: Intel i8274 MPSC adapter

    Copyright (c) 2011, William A. Beech

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

    These functions support a simulated i8274 interface device on an iSBC.
    The device had two physical I/O ports which could be connected
    to any serial I/O device that would connect to an RS232 interface.  

    All I/O is via programmed I/O.  The i8274 has a status port
    and a data port.    

    The simulated device does not support synchronous mode.  The simulated device 
    supports a select from I/O space and two address lines.  The data port is at the 
    lower address and the status/command port is at the higher address for each
    channel.

    Minimum simulation is provided for this device.  Channel A is used as a
    console port for the iSBC-88/45
    
    A write to the status port can select some options for the device:

    Asynchronous Mode Instruction
    +---+---+---+---+---+---+---+---+
    | S2  S1  EP PEN  L2  L1  B2  B1|
    +---+---+---+---+---+---+---+---+

        Baud Rate Factor
        B2  0       1       0       1
        B1  0       0       1       1
            sync    1X      16X     64X
            mode

        Character Length
        L2  0       1       0       1
        L1  0       0       1       1
            5       6       7       8  
            bits    bits    bits    bits

        EP - A 1 in this bit position selects even parity.
        PEN - A 1 in this bit position enables parity.

        Number of Stop Bits
        S2  0       1       0       1
        S1  0       0       1       1
            invalid 1       1.5     2
                    bit     bits    bits

    Command Instruction Format
    +---+---+---+---+---+---+---+---+
    | EH  IR RTS ER SBRK RxE DTR TxE|
    +---+---+---+---+---+---+---+---+

        TxE - A 1 in this bit position enables transmit.
        DTR - A 1 in this bit position forces *DTR to zero.
        RxE - A 1 in this bit position enables receive.
        SBRK - A 1 in this bit position forces TxD to zero.
        ER - A 1 in this bit position resets the error bits
        RTS - A 1 in this bit position forces *RTS to zero.
        IR - A 1 in this bit position returns the 8251 to Mode Instruction Format.
        EH - A 1 in this bit position enables search for sync characters.

    A read of the status port gets the port status:

    Status Read Format
    +---+---+---+---+---+---+---+---+
    |DSR  SD  FE  OE  PE TxE RxR TxR|
    +---+---+---+---+---+---+---+---+

        TxR - A 1 in this bit position signals transmit ready to receive a character.
        RxR - A 1 in this bit position signals receiver has a character.
        TxE - A 1 in this bit position signals transmitter has no more characters to transmit.
        PE - A 1 in this bit signals a parity error.
        OE - A 1 in this bit signals an transmit overrun error.
        FE - A 1 in this bit signals a framing error.
        SD - A 1 in this bit position returns the 8251 to Mode Instruction Format.
        DSR - A 1 in this bit position signals *DSR is at zero.

    A read to the data port gets the buffered character, a write
    to the data port writes the character to the device.  
    
*/

#include <stdio.h>

#include "multibus_defs.h"

#define UNIT_V_ANSI (UNIT_V_UF + 0)     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

/* register definitions */
/* channel A */
uint8   wr0a = 0,                       /* command register */
        wr1a = 0,                       /* enable register */
        wr2a = 0,                       /* mode register */
        wr3a = 0,                       /* configuration register 1 */
        wr4a = 0,                       /* configuration register 2 */
        wr5a = 0,                       /* configuration register 3 */
        wr6a = 0,                       /* sync low byte */
        wr7a = 0,                       /* sync high byte */
        rr0a = 0,                       /* status register */
        rr1a = 0,                       /* error register */
        rr2a = 0;                       /* read interrupt vector */
/* channel B */
uint8   wr0b = 0,                       /* command register */
        wr1b = 0,                       /* enable register */
        wr2b = 0,                       /* CH B interrups vector */
        wr3b = 0,                       /* configuration register 1 */
        wr4b = 0,                       /* configuration register 2 */
        wr5b = 0,                       /* configuration register 3 */
        wr6b = 0,                       /* sync low byte */
        wr7b = 0,                       /* sync high byte */
        rr0b = 0,                       /* status register */
        rr1b = 0,                       /* error register */
        rr2b = 0;                       /* read interrupt vector */

/* function prototypes */

t_stat i8274_svc (UNIT *uptr);
t_stat i8274_reset (DEVICE *dptr);
int32 i8274As(int32 io, int32 data);
int32 i8274Ad(int32 io, int32 data);
int32 i8274Bs(int32 io, int32 data);
int32 i8274Bd(int32 io, int32 data);

/* i8274 Standard I/O Data Structures */

UNIT i8274_unit = { UDATA (NULL, 0, 0), KBD_POLL_WAIT };

REG i8274_reg[] = {
    { HRDATA (WR0A, wr0a, 8) },
    { HRDATA (WR1A, wr1a, 8) },
    { HRDATA (WR2A, wr2a, 8) },
    { HRDATA (WR3A, wr3a, 8) },
    { HRDATA (WR4A, wr4a, 8) },
    { HRDATA (WR5A, wr5a, 8) },
    { HRDATA (WR6A, wr6a, 8) },
    { HRDATA (WR7A, wr7a, 8) },
    { HRDATA (RR0A, rr0a, 8) },
    { HRDATA (RR0A, rr1a, 8) },
    { HRDATA (RR0A, rr2a, 8) },
    { HRDATA (WR0B, wr0b, 8) },
    { HRDATA (WR1B, wr1b, 8) },
    { HRDATA (WR2B, wr2b, 8) },
    { HRDATA (WR3B, wr3b, 8) },
    { HRDATA (WR4B, wr4b, 8) },
    { HRDATA (WR5B, wr5b, 8) },
    { HRDATA (WR6B, wr6b, 8) },
    { HRDATA (WR7B, wr7b, 8) },
    { HRDATA (RR0B, rr0b, 8) },
    { HRDATA (RR0B, rr1b, 8) },
    { HRDATA (RR0B, rr2b, 8) },
    { NULL }
};
MTAB i8274_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

DEBTAB i8274_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE i8274_dev = {
    "I8274",             //name
    &i8274_unit,        //units
    i8274_reg,          //registers
    i8274_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix 
    32,                 //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth
    NULL,               //examine 
    NULL,               //deposit 
    i8274_reset,        //reset
    NULL,               //boot
    NULL,               //attach 
    NULL,               //detach
    NULL,               //ctxt                
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    i8274_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

/* service routine - actually gets char & places in buffer in CH A*/

t_stat i8274_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&i8274_unit, i8274_unit.wait); /* continue poll */
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;                    /* no char or error? */
    i8274_unit.buf = temp & 0xFF;       /* Save char */
    rr0a |= 0x01;                       /* Set rx char ready */

    /* Do any special character handling here */

    i8274_unit.pos++;
    return SCPE_OK;
}

/* Reset routine */

t_stat i8274_reset (DEVICE *dptr)
{
    wr0a = wr1a = wr2a = wr3a = wr4a = wr5a = wr6a = wr7a = rr0a = rr1a = rr2a = 0;
    wr0b = wr1b = wr2b = wr3b = wr4b = wr5b = wr6b = wr7b = rr0b = rr1b = rr2b = 0;
    sim_printf("   8274 Reset\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.

    The 8274 contains 2 separate channels, A and B.
*/

/* channel A command/status */
int32 i8274As(int32 io, int32 data)
{
    if (io == 0) {                      /* read status port */
        switch(wr0a & 0x7) {
            case 0:                     /* rr0a */
                return rr0a;
            case 1:                     /* rr1a */
                return rr1a;
            case 2:                     /* rr1a */
                return rr2a;
        }
        return 0;                       /* bad register select */
    } else {                            /* write status port */
        switch(wr0a & 0x7) {
            case 0:                     /* wr0a */
                wr0a = data;
                if ((wr0a & 0x38) == 0x18) { /* channel reset */
                    wr0a = wr1a = wr2a = wr3a = wr4a = wr5a = 0;
                    wr6a = wr7a = rr0a = rr1a = rr2a = 0;
                    sim_printf("8274 Channel A reset\n");
                }
                break;
            case 1:                     /* wr1a */
                wr1a = data;
                break;
            case 2:                     /* wr2a */
                wr2a = data;
                break;
            case 3:                     /* wr3a */
                wr3a = data;
                 break;
            case 4:                     /* wr4a */
                wr4a = data;
                 break;
            case 5:                     /* wr5a */
                wr5a = data;
                break;
            case 6:                     /* wr6a */
                wr6a = data;
                break;
            case 7:                     /* wr7a */
                wr7a = data;
                break;
        }
        sim_printf("8274 Command WR%dA=%02X\n", wr0a & 0x7, data);
        return 0;
    }
}

/* channel A data */
int32 i8274Ad(int32 io, int32 data)
{
    if (io == 0) {                          /* read data port */
        rr0a &= 0xFE;
        return (i8274_unit.buf);
    } else {                                /* write data port */
        sim_putchar(data);
    }
    return 0;
}

/* channel B command/status */
int32 i8274Bs(int32 io, int32 data)
{
    if (io == 0) {                          /* read status port */
        return i8274_unit.u3;
    } else {                                /* write status port */
        if (data == 0x40) {                 /* reset port! */
            sim_printf("8274 Reset\n");
        } else if (i8274_unit.u6) {
            i8274_unit.u5 = data;
            sim_printf("8274 Command Instruction=%02X\n", data);
        } else {
            i8274_unit.u4 = data;
            sim_printf("8274 Mode Instruction=%02X\n", data);
            i8274_unit.u6++;
        }
        return (0);
    }
}

/* channel B data */
int32 i8274Bd(int32 io, int32 data)
{
    if (io == 0) {                          /* read data port */
        i8274_unit.u3 &= 0xFD;
        return (i8274_unit.buf);
    } else {                                /* write data port */
        sim_putchar(data);
    }
    return 0;
}

/* end of i8274.c */