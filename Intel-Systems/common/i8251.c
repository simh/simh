/*  i8251.c: Intel i8251 UART adapter

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

        These functions support a simulated i8251 interface device on an iSBC.
        The device had one physical I/O port which could be connected
        to any serial I/O device that would connect to a current loop,
        RS232, or TTY interface.  Available baud rates were jumper
        selectable for each port from 110 to 9600.

        All I/O is via programmed I/O.  The i8251 has a status port
        and a data port.    

        The simulated device does not support synchronous mode.  The simulated device 
        supports a select from I/O space and one address line.  The data port is at the 
        lower address and the status/command port is at the higher.
        
        A write to the status port can select some options for the device:

        Asynchronous Mode Instruction
          7   6   5   4   3   2   1   0
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
          7   6   5   4   3   2   1   0
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
          7   6   5   4   3   2   1   0
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

        A read from the data port gets the typed character, a write
        to the data port writes the character to the device.  
*/

#include "system_defs.h"

#define  DEBUG   0

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

#define UNIT_V_ANSI (UNIT_V_UF + 0)     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

#define TXR         0x01
#define RXR         0x02
#define TXE         0x04
#define SD          0x40

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);

/* globals */

int32 i8251_devnum = 0;                 //actual number of 8251 instances + 1
uint16 i8251_port[4];                   //baseport port registered to each instance

/* function prototypes */

t_stat i8251_svc (UNIT *uptr);
t_stat i8251_reset (DEVICE *dptr, uint16 baseport);
uint8 i8251_get_dn(void);
uint8 i8251s(t_bool io, uint8 data);
uint8 i8251d(t_bool io, uint8 data);
void i8251_reset1(uint8 devnum);

/* i8251 Standard I/O Data Structures */
/* up to 1 i8251 devices */

UNIT i8251_unit = { 
    UDATA (&i8251_svc, 0, 0), KBD_POLL_WAIT 
};

REG i8251_reg[] = {
    { HRDATA (DATA, i8251_unit.buf, 8) },
    { HRDATA (STAT, i8251_unit.u3, 8) },
    { HRDATA (MODE, i8251_unit.u4, 8) },
    { HRDATA (CMD, i8251_unit.u5, 8) },
    { NULL }
};

DEBTAB i8251_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB i8251_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8251_dev = {
    "I8251",            //name
    &i8251_unit,        //units
    i8251_reg,          //registers
    i8251_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &i8251_reset,       //reset
    NULL,               //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8251_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

/* i8251_svc - actually gets char & places in buffer */

t_stat i8251_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&i8251_unit, i8251_unit.wait); /* continue poll */
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;                    /* no char or error? */
    i8251_unit.buf = temp & 0xFF;       /* Save char */
    i8251_unit.u3 |= RXR;               /* Set status */

    /* Do any special character handling here */

    i8251_unit.pos++;
    return SCPE_OK;
}

/* Reset routine */

t_stat i8251_reset (DEVICE *dptr, uint16 baseport)
{
    if (i8251_devnum >= I8251_NUM) {
        sim_printf("i8251_reset: too many devices!\n");
        return SCPE_MEM;
    }
    sim_printf("      8251-%d: Hardware Reset\n", i8251_devnum);
    sim_printf("      8251-%d: Registered at %04X\n", i8251_devnum, baseport);
    i8251_port[i8251_devnum] = baseport;
    reg_dev(i8251d, baseport, i8251_devnum); 
    reg_dev(i8251s, baseport + 1, i8251_devnum); 
    i8251_reset1(i8251_devnum);
    sim_activate (&i8251_unit, i8251_unit.wait); /* activate unit */
    i8251_devnum++;
    return SCPE_OK;
}

void i8251_reset1(uint8 devnum)
{
    i8251_unit.u3 = TXR + TXE;          /* status */
    i8251_unit.u4 = 0;                  /* mode instruction */
    i8251_unit.u5 = 0;                  /* command instruction */
    i8251_unit.u6 = 0;
    i8251_unit.buf = 0;
    i8251_unit.pos = 0;
    sim_printf("      8251-%d: Software Reset\n", devnum);
}

uint8 i8251_get_dn(void)
{
    int i;

    for (i=0; i<I8251_NUM; i++)
        if (port >= i8251_port[i] && port <= i8251_port[i] + 1)
            return i;
    sim_printf("i8251_get_dn: port %04X not in 8251 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8251s(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8251_get_dn()) != 0xFF) {
    //    sim_printf("\nio=%d data=%04X\n", io, data);
        if (io == 0) {                      /* read status port */
            return i8251_unit.u3;
        } else {                            /* write status port */
            if (i8251_unit.u6) {            /* if mode, set cmd */
                i8251_unit.u5 = data;
                if (DEBUG)
                    sim_printf("   8251-%d: Command Instruction=%02X\n", devnum, data);
                if (data & SD)              /* reset port! */
                    i8251_reset1(devnum);
            } else {                        /* set mode */
                i8251_unit.u4 = data;
                if (DEBUG)
                    sim_printf("   8251-%d: Mode Instruction=%02X\n", devnum, data);
                i8251_unit.u6 = 1;          /* set cmd received */
            }
        }
    }
    return 0;
}

uint8 i8251d(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8251_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            i8251_unit.u3 &= ~RXR;
            return (i8251_unit.buf);
        } else {                            /* write data port */
            sim_putchar(data);
        }
    }
    return 0;
}

/* end of i8251.c */
