/* scelbi_io.c: I/O for the SCELBI computer.

   Copyright (c) 2017, Hans-Ake Lund

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


    This interface simulates a "bitbanger" TTY interface as implemented
    on the SCELBI computer in the SCELBAL source code.
    Inport 2 bit 7 is used as input from the TTY and
    Outport 2 bit 0 is used as output to the TTY.
    In SCELBI documentation Inport 5 is used for input from the TTY
    and Outport 6 is used for output to the TTY.
    The I/O simulation routines are mapped to both port combinations.


    There are also functions that support simulated I/O for
    the Intel 8008 computer built for a master thesis in 1975.
    These functiona are however not mapped in the i/o configuration
    table as they conflict with the SCELBI TTY interface.
    Note that Inport 0 is read by the assembler code as INP 0
    Outport 0 is written by the AS Macro Assembler code as OUT 10 (octal)

    The following i/o ports were used in this computer:
    Outport 0: used to select device for reading from Inport 0
        and writing to Outport 3.
    Inport 0: used to read external data.
    Outport 3: used to write external data.

    Outport 1: used to save interupt state, connected to Inport 1.
    Outport 2: used to save interupt state, connected to Inport 2.

    Inport 3: used to input data from tape-reader
    Outport 4: used to output character to printer (implemented).
    Inport 5: used to input character from keyboard (implemented).

    Inport 4: used for status flags for the ports (Flagport).
      Flag 1 (bit 0): set to 1 when printer ready (implemented).
      Flag 2 (bit 1): set to 1 when input available from tape-reader.
      Flag 3 (bit 2): set to 1 when tape in tape-reader.
      Flag 5 (bit 4): set to 1 when character available from keyboard (implemented).
      Flag 7 (bit 6): set to 1 when the reset key on the computer is pressed.

    Inport 7: used to start the printer motor, just using an output pulse,
        no data is read.

    04-Sep-17    HAL     Working version of SCELBI simulator
    12-Sep-17    HAL     Modules restructured in "Intel-Systems" directory

*/

#include <stdio.h>
#include "system_defs.h"

/* This is the I/O configuration table. There are 8 possible
   input device addresses (octal 0 - 7) and 24 possible output 
   device addresses (octal 10 - 37).
   The port numbers are specified as for the 8008 AS Macro Assembler,
   in other 8008 assemblers outport 012 (octal) may be specified as 2.
   If a device is plugged to a port it's routine
   address is here, 'nulldev' means no device is available.
 */
int32 ttyout_d(int32 io, int32 data);
int32 ttyin_d(int32 io, int32 data);
int32 prt_d(int32 io, int32 data);
int32 kbd_d(int32 io, int32 data);
int32 iostat_s(int32 io, int32 data);
int32 nulldev(int32 io, int32 data);

struct idev dev_table[32] = {
{&nulldev}, {&nulldev}, {&ttyin_d}, {&nulldev},     /* 000 input 0 - 3 */
{&nulldev}, {&ttyin_d}, {&nulldev}, {&nulldev},     /* 004 input 4 - 7 */
{&nulldev}, {&nulldev}, {&ttyout_d}, {&nulldev},    /* 010 output 8 - 11 */
{&nulldev}, {&nulldev}, {&ttyout_d}, {&nulldev},    /* 014 output 12 - 15 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},     /* 020 output 16 - 19 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},     /* 024 output 20 - 23 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},     /* 030 output 24 - 27 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev}      /* 034 output 28 - 31 */
};

#define UNIT_V_ANSI (UNIT_V_UF + 0)                     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

t_stat tty_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);

/* I/O Data Structures */

/* TTY, TeleTYpewriter - console input/output
 */
UNIT tty_unit = { UDATA (&tty_svc, 0, 0), KBD_POLL_WAIT };

REG tty_reg[] = {
    { ORDATA (DATA, tty_unit.buf, 8) },
    { ORDATA (STAT, tty_unit.u3, 8) },
    { NULL }
};

MTAB tty_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

DEVICE tty_dev = {
    "TTY", &tty_unit, tty_reg, tty_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL
};

/* PTR, Paper Tape Reader - not implemented yet
 */
UNIT ptr_unit = { UDATA (&ptr_svc, UNIT_SEQ + UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG ptr_reg[] = {
    { ORDATA (DATA, ptr_unit.buf, 8) },
    { ORDATA (STAT, ptr_unit.u3, 8) },
    { ORDATA (POS, ptr_unit.pos, T_ADDR_W) },
    { NULL }
};

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    NULL, NULL, NULL
};

/* Service routines to handle simulator functions */

/* Service routine for TTY - actually gets char & places in buffer
 */
t_stat tty_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&tty_unit, tty_unit.wait);        /* continue poll */
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return (temp);                              /* no char or error? */
    tty_unit.buf = temp & 0377;                     /* Save char */
    tty_unit.u3 |= 0x10;                            /* Set status
                                                       Flag 5 (bit 3) == 1 */

    /* Do any special character handling here */

    tty_unit.pos++;
    return SCPE_OK;
}

/* Service routine for Paper Tape Reader - not implemented yet
 */
t_stat ptr_svc (UNIT *uptr)
{
    return SCPE_OK;
}

/* Reset routines */

/* Reset routine for TTY
 */
t_stat tty_reset (DEVICE *dptr)
{
    tty_unit.buf = 0;                              /* Data */
    tty_unit.u3 = 0x01;                            /* Status
                                                      Flag 1 (bit 0) == 1
                                                      printer always ready */
    sim_activate (&tty_unit, tty_unit.wait);       /* activate unit */
    return SCPE_OK;
}

/* Reset routine for Paper Tape Reader - not implemented yet
 */
t_stat ptr_reset (DEVICE *dptr)
{
    ptr_unit.buf = 0;
    ptr_unit.u3 = 0;
    sim_cancel (&ptr_unit);                        /* deactivate unit */
    return SCPE_OK;
}

/*  I/O instruction handlers for the 8008 simulator.
    Called from the CPU module when an IN or OUT instruction is issued.
    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input (io == 0) is passed as the return value,
    on output (io != 0), 'data' is written to the device.
 */

/*  I/O instruction handlers for the SCELBI bitbanger serial interface
 */
int32 ttyin_bitcntr = 0;
int32 ttyin_charin = 0;

/* TTY input routine, assumes 1 start bit, 8 databits and 2 stop bits.
   the assumed number of INP instructions for each character are 9
 */
int32 ttyin_d(int32 io, int32 data)
{
    int32 newbit;

    /* if (ttyin_bitcntr != 0) {
        sim_printf("io: %d, bitcntr: %d, charin: 0%o\n",
           io, ttyin_bitcntr, ttyin_charin);
    }
    */
    if (io != 0) { /* not an INP instruction */
        return 0;
    }
    if (ttyin_bitcntr == 0) {
        if (tty_unit.u3 & 0x10) {
            /* Character available if Flag 5 (bit 4) set */
            ttyin_charin = tty_unit.buf | 0x80; /* bit 7 always set in SCELBAL */
            tty_unit.u3 = tty_unit.u3 & 0xEF;  /* Reset Flag 5 (bit 4) */
            ttyin_bitcntr = 1;
            return (0); /* start bit */
        }
        else {
            return (0x80); /* no start bit */
        }
    }
    if (ttyin_bitcntr > 7) { /* last data bit */
        if (ttyin_charin & 1)
            newbit = 0x80;
        else
            newbit = 0x00;
        ttyin_bitcntr = 0;
        return (newbit);
    }
    if (ttyin_charin & 1)
        newbit = 0x80;
    else
        newbit = 0x00;
    ttyin_bitcntr++;
    ttyin_charin = ttyin_charin >> 1;
    return (newbit);
}

int32 ttyout_bitcntr = 0;
int32 ttyout_charout = 0;

/* TTY output routine, assumes 1 start bit, 8 databits and 2 stop bits.
   the assumed number of OUT instructions for each character are 10
 */
int32 ttyout_d(int32 io, int32 data)
{
    int32 newbit;

    /* sim_printf("io: %d, data: 0%o, bit0: %d, bitcntr: %d, charout: 0%o\n",
        io, data, (data & 1), ttyout_bitcntr, ttyout_charout);
    */

    if (io == 0) { /* not an OUT instruction */
        return 0;
    }
    if ((ttyout_bitcntr == 0) && ((data & 1) == 0)) { /* start bit */
        ttyout_bitcntr = 1;
        return 0;
    }
    if (ttyout_bitcntr == 8) { /* last bit in character */
        if (data & 1)
            newbit = 0x80;
        else
            newbit = 0x00;
        ttyout_charout = ttyout_charout >> 1;
        ttyout_charout = ttyout_charout | newbit;
        if (ttyout_charout != 0224) /* avoid printing CTRL-T */
            sim_putchar(ttyout_charout & 0x7f); /* bit 7 always set in SCELBAL */
        ttyout_bitcntr++;
        return 0;
    }
    if (ttyout_bitcntr > 8) { /* stop bit */
        ttyout_charout = 0;
        ttyout_bitcntr = 0;
        return 0;
    }
    if (data & 1)
        newbit = 0x80;
    else
        newbit = 0x00;
    ttyout_charout = ttyout_charout >> 1;
    ttyout_charout = ttyout_charout | newbit;
    ttyout_bitcntr++;
    return 0;
}

/*  I/O instruction handlers for the master thesis computer hardware.
 */

/* Get status byte from Flagport
 */
int32 iostat_s(int32 io, int32 data)
{
    if (io == 0)
        return (tty_unit.u3);
      else
        return (0);
}

/* Get character from keyboard
 */
int32 kbd_d(int32 io, int32 data)
{
    if (io == 0) {
        tty_unit.u3 = tty_unit.u3 & 0xEF;  /* Reset Flag 5 (bit 4) */
        return (tty_unit.buf | 0x80); /* bit 7 always set in SCELBAL */
    }
    return 0;
}

/* Put character to printer
 */
int32 prt_d(int32 io, int32 data)
{
    if (io != 0) {
        sim_putchar(data & 0x7f); /* bit 7 always set in SCELBAL */
    }
    return 0;
}

/* I/O instruction handler for unused ports
 */
int32 nulldev(int32 flag, int32 data)
{
    if (flag == 0)
        return (0377);
    return 0;
}
