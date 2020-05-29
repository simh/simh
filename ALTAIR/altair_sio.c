/* altair_sio: MITS Altair serial I/O card

   Copyright (c) 1997-2005, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

    These functions support a simulated MITS 2SIO interface card.
    The card had two physical I/O ports which could be connected
    to any serial I/O device that would connect to a current loop,
    RS232, or TTY interface.  Available baud rates were jumper
    selectable for each port from 110 to 9600.

    All I/O is via programmed I/O.  Each each has a status port
    and a data port.  A write to the status port can select
    some options for the device (0x03 will reset the port).
    A read of the status port gets the port status:

    +---+---+---+---+---+---+---+---+
    | X   X   X   X   X   X   O   I |
    +---+---+---+---+---+---+---+---+

    I - A 1 in this bit position means a character has been received
        on the data port and is ready to be read.
    O - A 1 in this bit means the port is ready to receive a character
        on the data port and transmit it out over the serial line.

    A read to the data port gets the buffered character, a write
    to the data port writes the character to the device.
*/

#include <stdio.h>

#include "altair_defs.h"

#define UNIT_V_ANSI (UNIT_V_UF + 0)                     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

t_stat sio_svc (UNIT *uptr);
t_stat sio_reset (DEVICE *dptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);

int32 ptr_stopioe = 0, ptp_stopioe = 0;                 /* stop on error */

/* 2SIO Standard I/O Data Structures */

UNIT sio_unit = { UDATA (&sio_svc, 0, 0), KBD_POLL_WAIT };

REG sio_reg[] = {
    { ORDATA (DATA, sio_unit.buf, 8) },
    { ORDATA (STAT, sio_unit.u3, 8) },
    { NULL }
};

MTAB sio_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

DEVICE sio_dev = {
    "2SIO", &sio_unit, sio_reg, sio_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &sio_reset,
    NULL, NULL, NULL
};

UNIT ptr_unit = { UDATA (&ptr_svc, UNIT_SEQ + UNIT_ATTABLE + UNIT_ROABLE, 0), KBD_POLL_WAIT };

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

UNIT ptp_unit = { UDATA (&ptp_svc, UNIT_SEQ + UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG ptp_reg[] = {
    { ORDATA (DATA, ptp_unit.buf, 8) },
    { ORDATA (STAT, ptp_unit.u3, 8) },
    { ORDATA (POS, ptp_unit.pos, T_ADDR_W) },
    { NULL }
};

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL
};

/* Service routines to handle simulator functions */

/* service routine - actually gets char & places in buffer */

t_stat sio_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&sio_unit, sio_unit.wait);            /* continue poll */
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;                                    /* no char or error? */
    sio_unit.buf = temp & 0377;                         /* Save char */
    sio_unit.u3 |= 0x01;                                /* Set status */

    /* Do any special character handling here */

    sio_unit.pos++;
    return SCPE_OK;
}


t_stat ptr_svc (UNIT *uptr)
{
    return SCPE_OK;
}

t_stat ptp_svc (UNIT *uptr)
{
    return SCPE_OK;
}


/* Reset routine */

t_stat sio_reset (DEVICE *dptr)
{
    sio_unit.buf = 0;                                   /* Data */
    sio_unit.u3 = 0x02;                                 /* Status */
    sim_activate (&sio_unit, sio_unit.wait);            /* activate unit */
    return SCPE_OK;
}


t_stat ptr_reset (DEVICE *dptr)
{
    ptr_unit.buf = 0;
    ptr_unit.u3 = 0x02;
    sim_cancel (&ptr_unit);                             /* deactivate unit */
    return SCPE_OK;
}

t_stat ptp_reset (DEVICE *dptr)
{
    ptp_unit.buf = 0;
    ptp_unit.u3 = 0x02;
    sim_cancel (&ptp_unit);                             /* deactivate unit */
    return SCPE_OK;
}


/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/

int32 sio0s(int32 io, int32 data)
{
    if (io == 0) {
        return (sio_unit.u3);
    } else {
        if (data == 0x03) {                             /* reset port! */
            sio_unit.u3 = 0x02;
            sio_unit.buf = 0;
            sio_unit.pos = 0;
        }
        return (0);
    }
}

int32 sio0d(int32 io, int32 data)
{
    if (io == 0) {
        sio_unit.u3 = sio_unit.u3 & 0xFE;
        return (sio_unit.buf);
    } else {
        sim_putchar(data);
    }
    return 0;
}

/* Port 2 controls the PTR/PTP devices */

int32 sio1s(int32 io, int32 data)
{
    if (io == 0) {
        if ((ptr_unit.flags & UNIT_ATT) == 0)           /* attached? */
            return 0x02;
        if (ptr_unit.u3 != 0)                           /* No more data? */
            return 0x02;
        return (0x03);                                  /* ready to read/write */
    } else {
        if (data == 0x03) {
            ptr_unit.u3 = 0;
            ptr_unit.buf = 0;
            ptr_unit.pos = 0;
            ptp_unit.u3 = 0;
            ptp_unit.buf = 0;
            ptp_unit.pos = 0;
        }
        return (0);
    }
}

int32 sio1d(int32 io, int32 data)
{
    int32 temp;
    UNIT *uptr;

    if (io == 0) {
        if ((ptr_unit.flags & UNIT_ATT) == 0)           /* attached? */
            return 0;
        if (ptr_unit.u3 != 0)
            return 0;
        uptr = ptr_dev.units;
        if ((temp = getc(uptr -> fileref)) == EOF) {    /* end of file? */
            ptr_unit.u3 = 0x01;
            return 0;
        }
        ptr_unit.pos++;
        return (temp & 0xFF);
    } else {
        uptr = ptp_dev.units;
        putc(data, uptr -> fileref);
        ptp_unit.pos++;
    }
    return 0;
}

