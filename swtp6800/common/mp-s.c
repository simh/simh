/*  mp-s.c: SWTP MP-S serial I/O card simulator

    Copyright (c) 2005-2012, William Beech

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
        Willaim Beech BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not
        be used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated SWTP MP-S interface card.
        The card contains one M6850 ACIA.  The ACIA implements one complete
        serial port.  It provides 7 or 8-bit ASCII RS-232 interface to Terminals
        or 20 mA current loop interface to a model 33 or 37 Teletype.  It is not
        compatible with baudot Teletypes.  Baud rates from 110 to 1200 are
        switch selectable from S! on the MP-S. The ACIA ports appear at all 
        4 addresses.  This fact is used by SWTBUG to determine the presence of the 
        MP-S vice MP-C serial card.  The ACIA interrupt request line can be connected
        to the IRQ or NMI interrupt lines by a jumper on the MP-S.

        All I/O is via either programmed I/O or interrupt controlled I/O.
        It has a status port and a data port.  A write to the status port
        can select some options for the device (0x03 will reset the port).
        A read of the status port gets the port status:

        +---+---+---+---+---+---+---+---+
        | I | P | O | F |CTS|DCD|TXE|RXF|
        +---+---+---+---+---+---+---+---+

        RXF - A 1 in this bit position means a character has been received
              on the data port and is ready to be read.
        TXE - A 1 in this bit means the port is ready to receive a character
              on the data port and transmit it out over the serial line.
     
        A read to the data port gets the buffered character, a write
        to the data port writes the character to the device.
*/

#include    <stdio.h>
#include    <ctype.h>
#include    "swtp_defs.h"

#define UNIT_V_TTY  (UNIT_V_UF)         // TTY or ANSI mode
#define UNIT_TTY   (1 << UNIT_V_TTY)

/* local global variables */

int32 ptr_stopioe = 0;                  // stop on error
int32 ptp_stopioe = 0;                  // stop on error
int32 odata;
int32 status;

int32 ptp_flag = 0;
int32 ptr_flag = 0;

/* function prototypes */

t_stat sio_svc (UNIT *uptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat sio_reset (DEVICE *dptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
int32 sio0s(int32 io, int32 data);
int32 sio0d(int32 io, int32 data);
int32 sio1s(int32 io, int32 data);
int32 sio1d(int32 io, int32 data);

/* sio data structures

   sio_dev        SIO device descriptor
   sio_unit       SIO unit descriptor
   sio_reg        SIO register list
   sio_mod        SIO modifiers list */

UNIT sio_unit = { UDATA (&sio_svc, 0, 0), KBD_POLL_WAIT
};

REG sio_reg[] = {
    { ORDATA (DATA, sio_unit.buf, 8) },
    { ORDATA (STAT, sio_unit.u3, 8) },
    { NULL }
};

MTAB sio_mod[] = {
    { UNIT_TTY, UNIT_TTY, "TTY", "TTY", NULL },
    { UNIT_TTY, 0, "ANSI", "ANSI", NULL },
    { 0 }
};

DEVICE sio_dev = {
    "MP-S", &sio_unit, sio_reg, sio_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &sio_reset,
    NULL, NULL, NULL
};

/* paper tape reader data structures

   ptr_dev        PTR device descriptor
   ptr_unit       PTR unit descriptor
   ptr_reg        PTR register list
   ptr_mod        PTR modifiers list */

UNIT ptr_unit = { UDATA (&ptr_svc, UNIT_SEQ + UNIT_ATTABLE, 0), KBD_POLL_WAIT
};

DEVICE ptr_dev = {
    "PTR", &ptr_unit, NULL, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    NULL, NULL, NULL
};

/* paper tape punch data structures

   ptp_dev        PTP device descriptor
   ptp_unit       PTP unit descriptor
   ptp_reg        PTP register list
   ptp_mod        PTP modifiers list */

UNIT ptp_unit = { UDATA (&ptp_svc, UNIT_SEQ + UNIT_ATTABLE, 0), KBD_POLL_WAIT
};
DEVICE ptp_dev = {
    "PTP", &ptp_unit, NULL, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL
};

/* console input service routine */

t_stat sio_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&sio_unit, sio_unit.wait); // continue poll
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;                    // no char or error?
    sio_unit.buf = temp & 0xFF;         // Save char
    sio_unit.u3 |= 0x01;                // Set RXF flag
    /* Do any special character handling here */
    sio_unit.pos++;                     // step character count
    return SCPE_OK;
}

/* paper tape reader input service routine */

t_stat ptr_svc (UNIT *uptr)
{
    int32 temp;

    sim_activate (&ptr_unit, ptr_unit.wait); // continue poll
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;                    // no char or error?
    ptr_unit.buf = temp & 0xFF;         // Save char
    ptr_unit.u3 |= 0x01;                // Set RXF flag
    /* Do any special character handling here */
    ptr_unit.pos++;                     // step character count
    return SCPE_OK;
}

/* paper tape punch output service routine */

t_stat ptp_svc (UNIT *uptr)
{
    return SCPE_OK;
}

/* Reset console */

t_stat sio_reset (DEVICE *dptr)
{
    sio_unit.buf = 0;                   // Data buffer
    sio_unit.u3 = 0x02;                 // Status buffer
    sio_unit.wait = 10000;
    sim_activate (&sio_unit, sio_unit.wait); // activate unit
    return SCPE_OK;
}

/* Reset paper tape reader */

t_stat ptr_reset (DEVICE *dptr)
{
    ptr_unit.buf = 0;
    ptr_unit.u3 = 0x02;
//    sim_activate (&ptr_unit, ptr_unit.wait); // activate unit
    sim_cancel (&ptr_unit);             // deactivate unit
    return SCPE_OK;
}

/* Reset paper tape punch */

t_stat ptp_reset (DEVICE *dptr)
{
    ptp_unit.buf = 0;
    ptp_unit.u3 = 0x02;
//    sim_activate (&ptp_unit, ptp_unit.wait); // activate unit
    sim_cancel (&ptp_unit);             // deactivate unit
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the MP-B2 module when a
   read or write occur to addresses 0x8004-0x8007. */

int32 sio0s(int32 io, int32 data)
{
    if (io == 0) {                      // control register read
        if (ptr_flag) {                 // reader enabled?
            if ((ptr_unit.flags & UNIT_ATT) == 0) { // attached?
                ptr_unit.u3 &= 0xFE;    // no, clear RXF flag 
                ptr_flag = 0;           // clear reader flag
                printf("Reader not attached to file\n");
            } else {                    // attached
                if (feof(ptr_unit.fileref)) { // EOF
                    ptr_unit.u3 &= 0xFE; // clear RXF flag
                    ptr_flag = 0;       // clear reader flag
                } else                  // not EOF
                    ptr_unit.u3 |= 0x01; // set ready
            }
            return (status = ptr_unit.u3); // return ptr status
        } else {
            return (status = sio_unit.u3); // return console status
        }
    } else {                            // control register write
        if (data == 0x03) {             // reset port!
            sio_unit.u3 = 0x02;         // reset console
            sio_unit.buf = 0;
            sio_unit.pos = 0;
            ptr_unit.u3 = 0x02;         // reset reader
            ptr_unit.buf = 0;
            ptr_unit.pos = 0;
            ptp_unit.u3 = 0x02;         // reset punch
            ptp_unit.buf = 0;
            ptp_unit.pos = 0;
        }
    return (status = 0);                // invalid io
    }
}

int32 sio0d(int32 io, int32 data)
{
    if (io == 0) {                      // data register read
        if (ptr_flag) {                 // RDR enabled?
            if ((ptr_unit.flags & UNIT_ATT) == 0) // attached?
                return 0;               // no, done
//          printf("ptr_unit.u3=%02X\n", ptr_unit.u3);
            if ((ptr_unit.u3 & 0x01) == 0) { // yes, more data?
//              printf("Returning old %02X\n", odata); // no, return previous byte
                return (odata & 0xFF);
            }
            if ((odata = getc(ptr_unit.fileref)) == EOF) { // end of file?
//              printf("Got EOF\n");
                ptr_unit.u3 &= 0xFE;    // clear RXF flag
                return (odata = 0);     // no data
            }
//          printf("Returning new %02X\n", odata);
            ptr_unit.pos++;             // step character count
            ptr_unit.u3 &= 0xFE;        // clear RXF flag
            return (odata & 0xFF);      // return character
        } else {
            sio_unit.u3 &= 0xFE;        // clear RXF flag
            return (odata = sio_unit.buf); // return next char
        }
    } else {                            // data register write
        if (isprint(data) || data == '\r' || data == '\n') { // printable?
            sim_putchar(data);          // print character on console
            if (ptp_flag && ptp_unit.flags & UNIT_ATT) { // PTP enabled & attached?
                putc(data, ptp_unit.fileref);
                ptp_unit.pos++;         // step character counter
            }
        } else {                        // DC1-DC4 control Reader/Punch
            switch (data) {
                case 0x11:              // PTR on
                    ptr_flag = 1;
                    ptr_unit.u3 |= 0x01;
//                    printf("Reader on\n");
                    break;
                case 0x12:              // PTP on
                    ptp_flag = 1;
                    ptp_unit.u3 |= 0x02;
//                    printf("Punch on\n");
                    break;
                case 0x13:              // PTR off
                    ptr_flag = 0;
//                    printf("Reader off-%d bytes read\n", ptr_unit.pos);
                    break;
                case 0x14:              // PTP off
                    ptp_flag = 0;
//                    printf("Punch off-%d bytes written\n", ptp_unit.pos);
                    break;
                default:                // ignore all other characters
                    break;
            }
        }
    }
    return (odata = 0);
}

/*  because each port appears at 2 addresses and this fact is used
    to determine if it is a MP-C or MP-S repeatedly in the SWTBUG
    monitor, this code assures that reads of the high ports return
    the same data as was read the last time on the low ports.
*/

int32 sio1s(int32 io, int32 data)
{
    return status;
}

int32 sio1d(int32 io, int32 data)
{
    return odata;
}

/* end of mp-s.c */
