/* s3_pkb.c: System/3 5471 console terminal simulator

   Copyright (c) 2001-2005, Charles E. Owen

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

   pkb          5471 printer/keyboard

   25-Apr-03    RMS     Revised for extended file support
   08-Oct-02    RMS     Added impossible function catcher
*/

#include "s3_defs.h"
#include <ctype.h>

extern int32 int_req, dev_busy, dev_done, dev_disable;
t_stat pkb_svc (UNIT *uptr);
t_stat pkb_reset (DEVICE *dptr);
extern int32 IAR[], level;
extern int32 debug_reg;

/* 5471 data structures

   pkb_dev      TTI device descriptor
   pkb_unit     TTI unit descriptor
   pkb_reg      TTI register list
   pkb_mod      TTI/TTO modifiers list
*/

/* Flag bits : (kept in pkb_unit.u3) */

#define PRT_INTREQ 0x800                                /* Printer interrupt pending */
#define KBD_INTREQ 0x400                                /* Request key interrupt pending */
#define KBD_INTEND 0x200                                /* End or cancel key interrupt pending */
#define KBD_INTKEY 0x100                                /* Return or other key interrupt pending */
#define KBD_REQLIGHT 0x20                               /* Request Pending Indicator (light on/off) */
#define KBD_PROLIGHT 0x10                               /* Proceed indicator (light on/off) */
#define KBD_REQINT 0x04                                 /* Req key interrupts enabled */
#define KBD_KEYINT 0x02                                 /* Other key interrupts enabled */
#define PRT_PRTINT 0x01                                 /* Printer interrupts enabled */

/* Keys mapped to 5471 functions */

int32 key_req = 0x01;                                   /* Request key: ^A */
int32 key_rtn = 0x12;                                   /* Return key: ^R */
int32 key_can = 0x1B;                                   /* Cancel key: ESC */
int32 key_end = 0x0d;                                   /* End key - CR */

UNIT pkb_unit = { UDATA (&pkb_svc, 0, 0), KBD_POLL_WAIT };

REG pkb_reg[] = {
    { HRDATA (FLAG, pkb_unit.u3, 16) },
    { HRDATA (IBUF, pkb_unit.buf, 8) },
    { HRDATA (OBUF, pkb_unit.u4, 8) },
    { HRDATA (REQKEY, key_req, 8) },
    { HRDATA (RTNKEY, key_rtn, 8) },
    { HRDATA (CANKEY, key_can, 8) },
    { HRDATA (ENDKEY, key_end, 8) },
    { DRDATA (POS, pkb_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, pkb_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
};

MTAB pkb_mod[] = {
    { 0 }
};

DEVICE pkb_dev = {
    "PKB", &pkb_unit, pkb_reg, pkb_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &pkb_reset,
    NULL, NULL, NULL
};


/*-------------------------------------------------------------------*/
/* EBCDIC to ASCII translate table                                   */
/*-------------------------------------------------------------------*/
unsigned char ebcdic_to_ascii[] = {
"\x00\x01\x02\x03\xA6\x09\xA7\x7F\xA9\xB0\xB1\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\xB2\xB4\x08\xB7\x18\x19\x1A\xB8\xBA\x1D\xBB\x1F"
"\xBD\xC0\x1C\xC1\xC2\x0A\x17\x1B\xC3\xC4\xC5\xC6\xC7\x05\x06\x07"
"\xC8\xC9\x16\xCB\xCC\x1E\xCD\x04\xCE\xD0\xD1\xD2\x14\x15\xD3\xFC"
"\x20\xD4\x83\x84\x85\xA0\xD5\x86\x87\xA4\xD6\x2E\x3C\x28\x2B\xD7"
"\x26\x82\x88\x89\x8A\xA1\x8C\x8B\x8D\xD8\x21\x24\x2A\x29\x3B\x5E"
"\x2D\x2F\xD9\x8E\xDB\xDC\xDD\x8F\x80\xA5\x7C\x2C\x25\x5F\x3E\x3F"
"\xDE\x90\xDF\xE0\xE2\xE3\xE4\xE5\xE6\x60\x3A\x23\x40\x27\x3D\x22"
"\xE7\x61\x62\x63\x64\x65\x66\x67\x68\x69\xAE\xAF\xE8\xE9\xEA\xEC"
"\xF0\x6A\x6B\x6C\x6D\x6E\x6F\x70\x71\x72\xF1\xF2\x91\xF3\x92\xF4"
"\xF5\x7E\x73\x74\x75\x76\x77\x78\x79\x7A\xAD\xA8\xF6\x5B\xF7\xF8"
"\x9B\x9C\x9D\x9E\x9F\xB5\xB6\xAC\xAB\xB9\xAA\xB3\xBC\x5D\xBE\xBF"
"\x7B\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\x93\x94\x95\xA2\xCF"
"\x7D\x4A\x4B\x4C\x4D\x4E\x4F\x50\x51\x52\xDA\x96\x81\x97\xA3\x98"
"\x5C\xE1\x53\x54\x55\x56\x57\x58\x59\x5A\xFD\xEB\x99\xED\xEE\xEF"
"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\xFE\xFB\x9A\xF9\xFA\xFF"
};

/*-------------------------------------------------------------------*/
/* ASCII to EBCDIC translate table                                   */
/*-------------------------------------------------------------------*/
unsigned char ascii_to_ebcdic[] = {
"\x00\x01\x02\x03\x37\x2D\x2E\x2F\x16\x05\x25\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\x3C\x3D\x32\x26\x18\x19\x1A\x27\x22\x1D\x35\x1F"
"\x40\x5A\x7F\x7B\x5B\x6C\x50\x7D\x4D\x5D\x5C\x4E\x6B\x60\x4B\x61"
"\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\x7A\x5E\x4C\x7E\x6E\x6F"
"\x7C\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xD1\xD2\xD3\xD4\xD5\xD6"
"\xD7\xD8\xD9\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xAD\xE0\xBD\x5F\x6D"
"\x79\x81\x82\x83\x84\x85\x86\x87\x88\x89\x91\x92\x93\x94\x95\x96"
"\x97\x98\x99\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xC0\x6A\xD0\xA1\x07"
"\x68\xDC\x51\x42\x43\x44\x47\x48\x52\x53\x54\x57\x56\x58\x63\x67"
"\x71\x9C\x9E\xCB\xCC\xCD\xDB\xDD\xDF\xEC\xFC\xB0\xB1\xB2\xB3\xB4"
"\x45\x55\xCE\xDE\x49\x69\x04\x06\xAB\x08\xBA\xB8\xB7\xAA\x8A\x8B"
"\x09\x0A\x14\xBB\x15\xB5\xB6\x17\x1B\xB9\x1C\x1E\xBC\x20\xBE\xBF"
"\x21\x23\x24\x28\x29\x2A\x2B\x2C\x30\x31\xCA\x33\x34\x36\x38\xCF"
"\x39\x3A\x3B\x3E\x41\x46\x4A\x4F\x59\x62\xDA\x64\x65\x66\x70\x72"
"\x73\xE1\x74\x75\x76\x77\x78\x80\x8C\x8D\x8E\xEB\x8F\xED\xEE\xEF"
"\x90\x9A\x9B\x9D\x9F\xA0\xAC\xAE\xAF\xFD\xFE\xFB\x3F\xEA\xFA\xFF"
};

/* -------------------------------------------------------------------- */

/* Console Input: master routine */

int32 pkb (int32 op, int32 m, int32 n, int32 data)
{
    int32 iodata= 0, ec, ac;
    switch (op) {
        case 0:                                         /* SIO 5471 */
            if (n != 0)
                return STOP_INVDEV;
            /*printf("%04X SIO %d,%d,%02X\n\r", IAR[level]-4, m, n, data);*/
            if (m == 0) {                               /* Keyboard */
                pkb_unit.u3 &= 0xFC1;
                pkb_unit.u3 |= data;
                if (data & 0x01) {
                    pkb_unit.u3 &= ~KBD_INTREQ;
                    pkb_unit.u3 &= ~KBD_INTKEY;
                    pkb_unit.u3 &= ~KBD_INTEND;
                    return RESET_INTERRUPT;
                }   
            } else {                                    /* Printer */
                if (data & 0x80) {                      /* start print bit */
                    if (debug_reg & 0x80)
                        return STOP_IBKPT;
                    ec = pkb_unit.u4 & 0xff;
                    ac = ebcdic_to_ascii[ec];
                    sim_putchar(ac);
                    pkb_unit.u3 |= PRT_INTREQ;
                }
                if (data & 0x40) {                      /* Carr. Return */
                    sim_putchar('\n');
                    sim_putchar('\r');
                    pkb_unit.u3 |= PRT_INTREQ;
                }   
                pkb_unit.u3 &= 0xFFe;
                if (data & 0x04)                        /* Print interrupt flag */
                    pkb_unit.u3 |= PRT_PRTINT;
                if (data & 0x01) {                      /* Reset Interrupt */
                    if (level < 8) {
                        if (!(data & 0x80))
                            pkb_unit.u3 &= ~PRT_INTREQ;
                        return RESET_INTERRUPT;
                    }
                }   
            }
            return SCPE_OK;
        case 1:                                         /* LIO 5471 */
            if (n != 0)
                return STOP_INVDEV;
            if (m != 1)
                return STOP_INVDEV;
            pkb_unit.u4 = (data >> 8) & 0xff;
            return SCPE_OK;
            break;
        case 2:                                         /* TIO 5471 */
            return STOP_INVDEV;
        case 3:                                         /* SNS 5471 */
            if (n != 1 && n != 3)
                return (STOP_INVDEV << 16);
            if (m == 0) {                               /* Keyboard data */
                if (n == 1) {                           /* Sense bytes 0 & 1 */
                    iodata = (pkb_unit.buf << 8) & 0xff00;
                    if (pkb_unit.u3 & KBD_INTREQ)
                        iodata |= 0x80;
                    if (pkb_unit.u3 & KBD_INTEND)
                        iodata |= 0x40;
                    if (pkb_unit.u3 & KBD_INTKEY)
                        iodata |= 0x08;
                    if (pkb_unit.buf == 0x12)           /* Return key */
                        iodata |= 0x04;
                    if (pkb_unit.buf == 0x03)           /* Cancel key */
                        iodata |= 0x20;
                    if (pkb_unit.buf == 0x0d)           /* End key */
                        iodata |= 0x10;             
                    iodata |= ((SCPE_OK << 16) & 0xffff0000);           
                } else {                                /* Sense bytes 2 & 3 */
                    iodata = 0;                         /* Manual says CE use only */   
                }   
            } else {                                    /* Printer Data */
                if (n == 1) {                           /* Sense bytes 0 & 1 */
                    iodata = 0;
                    if (pkb_unit.u3 & PRT_INTREQ)
                        iodata |= 0x80;
                } else {
                    iodata = 0;                         /* CE use only */
                }   
            }
            iodata |= ((SCPE_OK << 16) & 0xffff0000);
            return (iodata);            
        case 4:                                         /* APL 5471 */
            return STOP_INVDEV;
        default:
            break;
    }                       
    sim_printf (">>PKB non-existent function %d\n", op);
    return SCPE_OK;                     
}

/* Unit service */

t_stat pkb_svc (UNIT *uptr)
{
int32 temp, ac, ec;

sim_activate (&pkb_unit, pkb_unit.wait);                /* continue poll */

if (pkb_unit.u3 & PRT_INTREQ) {                         /* Printer Interrupt */
    int_req |= 2;
    return SCPE_OK;
}   

/* Keyboard : handle input */

if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp; /* no char or error? */

ac = temp & 0x7f;                                       /* placed type ASCII char in ac */
if (pkb_unit.u3 & KBD_REQINT) {
    if (ac == key_req) {                                /* Request Key */
        pkb_unit.u3 |= KBD_INTREQ;
        int_req |= 2;
        return SCPE_OK;
    }
}
if (islower(ac))
    ac = toupper(ac);                
ec = ascii_to_ebcdic[ac];                               /* Translate */
pkb_unit.buf = ec;                                      /* put in buf */
pkb_unit.pos = pkb_unit.pos + 1;
if (ac == key_end) {                                    /* End key */
    if (pkb_unit.u3 & KBD_KEYINT) {     
        pkb_unit.u3 |= KBD_INTEND;
        pkb_unit.buf = 0x0d;
        int_req |= 2;
    }   
    return SCPE_OK;
}
if (ac == key_can) {                                    /* Cancel key */
    if (pkb_unit.u3 & KBD_KEYINT) {     
        pkb_unit.u3 |= KBD_INTEND;
        pkb_unit.buf = 0x03;
        int_req |= 2;
    }   
    return SCPE_OK;
}
if (ac == key_rtn) {                                    /* Return key */
    if (pkb_unit.u3 & KBD_KEYINT) {     
        pkb_unit.u3 |= KBD_INTKEY;
        pkb_unit.buf = 0x12;
        int_req |= 2;
    }   
    return SCPE_OK;
}
if (pkb_unit.u3 & KBD_KEYINT) {                         /* Key interupts enabled ? */
    int_req |= 2;                                       /* Device 1 Interrupt! */
    pkb_unit.u3 |= KBD_INTKEY;                          /* Set pending flag */
}   
return SCPE_OK;
}

/* Reset routine */

t_stat pkb_reset (DEVICE *dptr)
{
pkb_unit.buf = 0;
int_req = int_req & ~0x02;                              /* reset interrupt */   
sim_activate (&pkb_unit, pkb_unit.wait);                /* activate unit */
return SCPE_OK;
}

t_stat pkb_setmod (UNIT *uptr, int32 value)
{
return SCPE_OK;
}

