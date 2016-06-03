/*  i8273.c: Intel i8273 UART adapter

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

    These functions support a simulated i8273 interface device on an iSBC.
    The device had one physical I/O port which could be connected
    to any serial I/O device that would connect to a current loop,
    RS232, or TTY interface.  Available baud rates were jumper
    selectable for each port from 110 to 9600.

    All I/O is via programmed I/O.  The i8273 has a status port
    and a data port.    

    The simulated device does not support synchronous mode.  The simulated device 
    supports a select from I/O space and one address line.  The data port is at the 
    lower address and the status/command port is at the higher.
    
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

#define UNIT_V_ANSI (UNIT_V_UF + 0)                     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

uint8   
    wr0 = 0,                            /* command register */
    wr1 = 0,                            /* enable register */
    wr2 = 0,                            /* CH A mode register */
                                        /* CH B interrups vector */
    wr3 = 0,                            /* configuration register 1 */
    wr4 = 0,                            /* configuration register 2 */
    wr5 = 0,                            /* configuration register 3 */
    wr6 = 0,                            /* sync low byte */
    wr7 = 0,                            /* sync high byte */
    rr0 = 0,                            /* status register */
    rr1 = 0,                            /* error register */
    rr2 = 0;                            /* read interrupt vector */

/* function prototypes */

t_stat i8273_reset (DEVICE *dptr);

/* i8273 Standard I/O Data Structures */

UNIT i8273_unit = { UDATA (NULL, 0, 0), KBD_POLL_WAIT };

REG i8273_reg[] = {
    { HRDATA (WR0, wr0, 8) },
    { HRDATA (WR1, wr1, 8) },
    { HRDATA (WR2, wr2, 8) },
    { HRDATA (WR3, wr3, 8) },
    { HRDATA (WR4, wr4, 8) },
    { HRDATA (WR5, wr5, 8) },
    { HRDATA (WR6, wr6, 8) },
    { HRDATA (WR7, wr7, 8) },
    { HRDATA (RR0, rr0, 8) },
    { HRDATA (RR0, rr1, 8) },
    { HRDATA (RR0, rr2, 8) },
    { NULL }
};
MTAB i8273_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

DEBTAB i8273_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE i8273_dev = {
    "8273",             //name
    &i8273_unit,        //units
    i8273_reg,          //registers
    i8273_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix 
    32,                 //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth
    NULL,               //examine 
    NULL,               //deposit 
    i8273_reset,        //reset
    NULL,               //boot
    NULL,               //attach 
    NULL,               //detach
    NULL,               //ctxt                
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    i8273_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

/* Reset routine */

t_stat i8273_reset (DEVICE *dptr)
{
    wr0 = 0;                            /* command register */
    wr1 = 0;                            /* enable register */
    wr2 = 0;                            /* CH A mode register */
                                        /* CH B interrups vector */
    wr3 = 0;                            /* configuration register 1 */
    wr4 = 0;                            /* configuration register 2 */
    wr5 = 0;                            /* configuration register 3 */
    wr6 = 0;                            /* sync low byte */
    wr7 = 0;                            /* sync high byte */
    rr0 = 0;                            /* status register */
    rr1 = 0;                            /* error register */
    rr2 = 0;                            /* read interrupt vector */
    sim_printf("   8273 Reset\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/

int32 i8273s(int32 io, int32 data)
{
    if (io == 0) {                                      /* read status port */
        return i8273_unit.u3;
    } else {                                            /* write status port */
        if (data == 0x40) {                             /* reset port! */
            i8273_unit.u3 = 0x05;                       /* status */
            i8273_unit.u4 = 0;                          /* mode instruction */
            i8273_unit.u5 = 0;                          /* command instruction */
            i8273_unit.u6 = 0;
            i8273_unit.buf = 0;
            i8273_unit.pos = 0;
            sim_printf("8273 Reset\n");
        } else if (i8273_unit.u6) {
            i8273_unit.u5 = data;
            sim_printf("8273 Command Instruction=%02X\n", data);
        } else {
            i8273_unit.u4 = data;
            sim_printf("8273 Mode Instruction=%02X\n", data);
            i8273_unit.u6++;
        }
        return (0);
    }
}

int32 i8273d(int32 io, int32 data)
{
    if (io == 0) {                                      /* read data port */
        i8273_unit.u3 &= 0xFD;
        return (i8273_unit.buf);
    } else {                                            /* write data port */
        sim_putchar(data);
    }
    return 0;
}

