/* i1620_tty.c: IBM 1620 typewriter

   Copyright (c) 2002-2008, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   tty          console typewriter

   21-Sep-05    RMS     Revised translation tables for 7094/1401 compatibility
   22-Dec-02    RMS     Added break test
*/

#include "i1620_defs.h"

#define TTO_COLMAX      80

int32 tto_col = 0;

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;

void tti_unlock (void);
t_stat tti_rnum (int8 *c);
t_stat tti_ralp (int8 *c);
t_stat tti_read (int8 *c);
t_stat tto_num (uint32 pa, uint32 len);
t_stat tto_write (uint32 c);
t_stat tty_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
*/

UNIT tty_unit = { UDATA (&tty_svc, 0, 0), KBD_POLL_WAIT };

REG tty_reg[] = {
    { DRDATA (COL, tto_col, 7) },
    { DRDATA (TIME, tty_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

DEVICE tty_dev = {
    "TTY", &tty_unit, tty_reg, NULL,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL
    };

/* Data tables */

/* Keyboard to numeric */

const char *tti_to_num = "0123456789|=@:;}";

/* Keyboard to alphameric (digit pair) - translates LC to UC */

const int8 tti_to_alp[128] = {
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 00 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x00, 0x02,   -1, 0x33, 0x13, 0x24, 0x10, 0x34,        /*  !"#$%&' */
 0x24, 0x04, 0x14, 0x10, 0x23, 0x20, 0x03, 0x21,        /* ()*+,-./ */
 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* 01234567 */
 0x78, 0x79,   -1,   -1,   -1, 0x33,   -1,   -1,        /* 89:;<=>? */
 0x34, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* @ABCDEFG */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* HIJKLMNO */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* PQRSTUVW */
 0x67, 0x68, 0x69,   -1,   -1,   -1,   -1,   -1,        /* XYZ[\]^_ */
   -1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* `abcdefg */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* hijklmno */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* pqrstuvw */
 0x67, 0x68, 0x69,   -1,   -1, 0x0F,   -1,   -1         /* xyz{|}~  */
 };

/* Numeric (digit) to typewriter */

const char num_to_tto[16] = {
 '0', '1', '2', '3', '4', '5', '6', '7',
 '8', '9', '|', '=', '@', ':', ';', '}'
 };

/* Alphameric (digit pair) to typewriter */

const char alp_to_tto[256] = {
 ' ',  -1, '?', '.', ')',  -1,  -1,  -1,                /* 00 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '+',  -1, '!', '$', '*', ' ',  -1,  -1,                /* 10 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '-', '/', '|', ',', '(',  -1,  -1,  -1,                /* 20 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1, '0', '=', '@', ':',  -1,  -1,                /* 30 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1, 'A', 'B', 'C', 'D', 'E', 'F', 'G',                /* 40 */
 'H', 'I',  -1,  -1,  -1,  -1,  -1,  -1,
 '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                /* 50 */
 'Q', 'R',  -1,  -1,  -1,  -1,  -1,  -1,
  -1, '/', 'S', 'T', 'U', 'V', 'W', 'X',                /* 60 */
 'Y', 'Z',  -1,  -1,  -1,  -1,  -1,  -1,
 '0', '1', '2', '3', '4', '5', '6', '7',                /* 70 */
 '8', '9',  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 80 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 90 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* A0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* B0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* C0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* D0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* E0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* F0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
 };

/* Terminal IO
 
   - On input, parity errors cannot occur.
   - On input, release-start does NOT cause a record mark to be stored.
   - On output, invalid characters type an invalid character and set WRCHK.
     If IO stop is set, the system halts at the end of the operation.
*/

t_stat tty (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
t_addr i;
uint8 d;
int8 ttc;
t_stat r, sta;

sta = SCPE_OK;
switch (op) {                                           /* case on op */

    case OP_K:                                          /* control */
        switch (f1) {                                   /* case on control */
        case 1:                                         /* space */
            tto_write (' ');
            break;
        case 2:                                         /* return */
            tto_write ('\r');
            break;
        case 3:                                         /* backspace */
            if ((cpu_unit.flags & IF_MII) == 0)
                return STOP_INVFNC;
            tto_write ('\b');
            break;
        case 4:                                         /* index */
            if ((cpu_unit.flags & IF_MII) == 0)
                return STOP_INVFNC;
            tto_write ('\n');
            break;
        case 8:                                         /* tab */
            tto_write ('\t');
            break;
        default:
            return STOP_INVFNC;
            }
        return SCPE_OK;

    case OP_RN:                                         /* read numeric */
        tti_unlock ();                                  /* unlock keyboard */
        for (i = 0; i < MEMSIZE; i++) {                 /* (stop runaway) */
            r = tti_rnum (&ttc);                        /* read char */
            if (r != SCPE_OK)                           /* error? */
                return r;
            if (ttc == 0x7F)                            /* end record? */
                return SCPE_OK;
            M[pa] = ttc & (FLAG | DIGIT);               /* store char */
            PP (pa);                                    /* incr mem addr */
            }
        break;

    case OP_RA:                                         /* read alphameric */
        tti_unlock ();
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* (stop runaway) */
            r = tti_ralp (&ttc);                        /* read char */
            if (r != SCPE_OK)                           /* error? */
                return r;
            if (ttc == 0x7F)                            /* end record? */
                return SCPE_OK;
            M[pa] = (M[pa] & FLAG) | (ttc & DIGIT);     /* store 2 digits */
            M[pa - 1] = (M[pa - 1] & FLAG) | ((ttc >> 4) & DIGIT);
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;  

    case OP_DN:
        return tto_num (pa, 20000 - (pa % 20000));      /* dump numeric */

    case OP_WN:
        return tto_num (pa, 0);                         /* type numeric */

    case OP_WA:
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* stop runaway */
            d = M[pa] & DIGIT;                          /* get digit */
            if ((d & 0xA) == REC_MARK)                  /* 8-2 char? done */
                return sta;
            d = ((M[pa - 1] & DIGIT) << 4) | d;         /* get digit pair */
            ttc = alp_to_tto[d];                        /* translate */
            if (ttc < 0) {                              /* bad char? */
                ind[IN_WRCHK] = 1;                      /* set write check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                }
            tto_write (ttc & 0x7F);                     /* write */
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;          

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return STOP_RWRAP;
}

/* Read numerically - cannot generate parity errors */

t_stat tti_rnum (int8 *c)
{
int8 raw, flg = 0;
char *cp;
t_stat r;

*c = -1;                                                /* no char yet */
do {
    r = tti_read (&raw);                                /* get char */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    if (raw == '\r')                                    /* return? mark */
        *c = 0x7F;
    else if ((raw == '~') || (raw == '`'))              /* flag? mark */
        flg = FLAG;
    else if (cp = strchr (tti_to_num, raw))             /* legal? */
        *c = ((int8) (cp - tti_to_num)) | flg;          /* assemble char */
    else raw = 007;                                     /* beep! */
    tto_write (raw);                                    /* echo */
    } while (*c == -1);
return SCPE_OK;
}

/* Read alphamerically - cannot generate parity errors */

t_stat tti_ralp (int8 *c)
{
int8 raw;
t_stat r;

*c = -1;                                                /* no char yet */
do {
    r = tti_read (&raw);                                /* get char */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    if (raw == '\r')                                    /* return? mark */
        *c = 0x7F;
    else if (tti_to_alp[raw] >= 0)                      /* legal char? */
        *c = tti_to_alp[raw];                           /* xlate */
    else raw = 007;                                     /* beep! */
    tto_write (raw);                                    /* echo */
    } while (*c == -1);
return SCPE_OK;
}

/* Read from keyboard */

t_stat tti_read (int8 *c)
{
int32 t;

do {
    t = sim_poll_kbd ();                                /* get character */
	} while ((t == SCPE_OK) || (t & SCPE_BREAK));       /* ignore break */
if (t < SCPE_KFLAG)                                     /* error? */
    return t;
*c = t & 0177;                                          /* store character */
return SCPE_OK;
}

/* Write numerically - cannot generate parity errors */

t_stat tto_num (uint32 pa, uint32 len)
{
t_stat r;
uint8 d;
uint32 i, end;

end = pa + len;
for (i = 0; i < MEMSIZE; i++) {                         /* (stop runaway) */
    d = M[pa];                                          /* get char */
    if (len? (pa >= end):                               /* dump: end reached? */
       ((d & REC_MARK) == REC_MARK))                    /* write: rec mark? */
        return SCPE_OK;                                 /* end operation */
    if (d & FLAG)                                       /* flag? */
        tto_write ('`');
    r = tto_write (num_to_tto[d & DIGIT]);              /* write */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    PP (pa);                                            /* incr mem addr */
    }
return STOP_RWRAP;
}

/* Write, maintaining position */

t_stat tto_write (uint32 c)
{
int32 rpt;

if (c == '\t') {                                        /* tab? */
    rpt = 8 - (tto_col % 8);                            /* distance to next */
    tto_col = tto_col + rpt;                            /* tab over */
    while (rpt-- > 0)                                   /* use spaces */
        sim_putchar (' ');
    return SCPE_OK;
    }
if (c == '\r') {                                        /* return? */
    sim_putchar ('\r');                                 /* crlf */
    sim_putchar ('\n');
    tto_col = 0;                                        /* clear colcnt */
    return SCPE_OK;
    }
if ((c == '\n') || (c == 007)) {                        /* non-spacing? */
    sim_putchar (c);
    return SCPE_OK;
    }
if (c == '\b')                                          /* backspace? */
    tto_col = tto_col? tto_col - 1: 0;
else tto_col++;                                         /* normal */
if (tto_col > TTO_COLMAX) {                             /* line wrap? */
    sim_putchar ('\r');
    sim_putchar ('\n');
    tto_col = 0;
    }
sim_putchar (c);
return SCPE_OK;
}

/* Unit service - polls for WRU */

t_stat tty_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tty_unit, tty_unit.wait);                /* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)              /* no char or error? */
    return temp;
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
sim_activate (&tty_unit, tty_unit.wait);                /* activate poll */
tto_col = 0;
return SCPE_OK;
}

/* TTI unlock - signals that we are ready for keyboard input */

void tti_unlock (void)
{
tto_write ('>');
return;
}
