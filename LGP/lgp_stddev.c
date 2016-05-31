/* lgp_stddev.c: LGP-30 standard devices

   Copyright (c) 2004-2008, Robert M Supnik

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

   tti          typewriter input (keyboard and reader)
   tto          typewriter output (printer and punch)
   ptr          high speed reader
   ptpp         high speed punch

   26-Nov-2008  RMS     Changed encode character from # to !
*/

#include "lgp_defs.h"
#include <ctype.h>

uint32 tt_wait = WPS / 10;
uint32 tti_buf = 0;
uint32 tti_rdy = 0;
uint32 tto_uc = 0;
uint32 tto_buf = 0;
uint32 ttr_stopioe = 1;
uint32 ptr_rdy = 0;
uint32 ptr_stopioe = 1;
uint32 ptp_stopioe = 1;

extern uint32 A;
extern uint32 inp_strt, inp_done;
extern uint32 out_strt, out_done;
extern UNIT cpu_unit;

t_stat tti_svc (UNIT *uptr);
t_stat ttr_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *uptr);
t_stat tto_reset (DEVICE *uptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *uptr);
t_stat ptp_reset (DEVICE *uptr);
t_stat tap_attach (UNIT *uptr, CONST char *cptr);
t_stat tap_attable (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat read_reader (UNIT *uptr, int32 stop, int32 *c);
t_stat write_tto (int32 flex);
t_stat write_punch (UNIT *uptr, int32 flex);
t_stat tti_rdrss (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat punch_feed (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat send_start (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

extern uint32 shift_in (uint32 a, uint32 dat, uint32 sh4);

/* Conversion tables */

const int32 flex_to_ascii[128] = {
    -1  , 'z', '0', ' ', '>', 'b', '1', '-',
    '<' , 'y', '2', '+', '|', 'r', '3', ';',
    '\r', 'i', '4', '/','\\', 'd', '5', '.',
    '\t', 'n', '6', ',', -1 , 'm', '7', 'v',
    '\'', 'p', '8', 'o', -1 , 'e', '9', 'x',
    -1  , 'u', 'f', -1 , -1 , 't', 'g', -1 ,
    -1  , 'h', 'j', -1 , -1 , 'c', 'k', -1 ,
    -1  , 'a', 'q', -1 , -1 , 's', 'w', 0  ,

    -1  , 'Z', ')', ' ', -1 , 'B', 'L', '_',
    -1  , 'Y', '*', '=', '|', 'R', '"', ':',
    '\r', 'I', '^', '?','\\', 'D', '%', ']',
    '\t', 'N', '$', '[', -1 , 'M', '~', 'V',
    '\'', 'P', '#', 'O', -1 , 'E', '(', 'X',
    -1  , 'U', 'F', -1 , -1 , 'T', 'G', -1 ,
    -1  , 'H', 'J', -1 , -1 , 'C', 'K', -1 ,
    -1  , 'A', 'Q', -1 , -1 , 'S', 'W', 0
    };

const int32 ascii_to_flex[128] = {
    -1 , -1 , -1 , -1 , -1 , -1 , -1 , -1 ,
    024, 030, -1 , -1 , -1 , 020, -1 , -1 ,
    -1 , -1 , -1 , -1 , -1 , -1 , -1 , -1 ,
    -1 , -1 , -1 , -1 , -1 , -1 , -1 , -1 ,
    003, -1 , 016, 042, 032, 026, -1 , 040,
    046, 001, 012, 013, 033, 007, 027, 023,
    002, 006, 012, 016, 022, 026, 032, 036,
    042, 046, 017, 017, 004, 013, 010, 023,
    -1 , 071, 005, 065, 025, 045, 052, 056,
    061, 021, 062, 066, 006, 035, 031, 043,
    041, 072, 015, 075, 055, 051, 037, 076,
    047, 011, 001, 033, -1 , 027, 022, 007,
    -1,  071, 005, 065, 025, 045, 052, 056,
    061, 021, 062, 066, 006, 035, 031, 043,
    041, 072, 015, 075, 055, 051, 037, 076,
    047, 011, 001, -1 , 014, -1 , 036, 077
    };
    
static const uint8 flex_inp_valid[64] = {
    1, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    1, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1
    };

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_mod      TTI modifier list
   tti_reg      TTI register list
*/

UNIT tti_unit[] = {
    { UDATA (&tti_svc, 0, 0) },
    { UDATA (&ttr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0) }
    };

REG tti_reg[] = {
    { HRDATA (BUF, tti_buf, 6) },
    { FLDATA (RDY, tti_rdy, 0) },
    { DRDATA (KPOS, tti_unit[0].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (RPOS, tti_unit[1].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tt_wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, ttr_stopioe, 0) },
    { NULL }
    };

MTAB tti_mod[] = {
    { UNIT_FLEX_D, UNIT_FLEX_D, NULL, "FLEX", &tap_attable },
    { UNIT_FLEX_D, 0,           NULL, "ASCII", &tap_attable },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT+UNIT_FLEX,
      "file is Flex", NULL },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT,
      "file is ASCII", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE+UNIT_FLEX,
      "default is Flex", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE,
      "default is ASCII", NULL },
    { UNIT_ATTABLE+UNIT_NOCS, UNIT_ATTABLE+UNIT_NOCS,
      "ignore conditional stop", "NOCSTOP", &tap_attable },
    { UNIT_ATTABLE+UNIT_NOCS, UNIT_ATTABLE          ,
      NULL, "CSTOP", &tap_attable },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "START", &send_start },
    { MTAB_XTD|MTAB_VDV, 1, NULL, "RSTART", &tti_rdrss },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "RSTOP", &tti_rdrss },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", tti_unit, tti_reg, tti_mod,
    2, 10, 31, 1, 16, 7,
    NULL, NULL, &tti_reset,
    NULL, &tap_attach, NULL
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_mod      TTO modifier list
   tto_reg      TTO register list
*/

UNIT tto_unit[] = {
    { UDATA (&tto_svc, 0, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) }
    };

REG tto_reg[] = {
    { HRDATA (BUF, tto_buf, 6) },
    { FLDATA (UC, tto_uc, 0) },
    { DRDATA (TPOS, tto_unit[0].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (PPOS, tto_unit[1].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tt_wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { UNIT_FLEX_D, UNIT_FLEX_D, NULL, "FLEX", &tap_attable },
    { UNIT_FLEX_D, 0,           NULL, "ASCII", &tap_attable },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT+UNIT_FLEX,
      "file is Flex", NULL },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT,
      "file is ASCII", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE+UNIT_FLEX,
      "default is Flex", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE,
      "default is ASCII", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "FEED", &punch_feed },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", tto_unit, tto_reg, tto_mod,
    2, 10, 31, 1, 16, 7,
    NULL, NULL, &tto_reset, 
    NULL, &tap_attach, NULL
    };

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_mod      PTR modifier list
   ptr_reg      PTR register list
*/

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), WPS / 200
    };

REG ptr_reg[] = {
    { HRDATA (BUF, ptr_unit.buf, 6) },
    { FLDATA (RDY, ptr_rdy, 0) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB ptr_mod[] = {
    { UNIT_FLEX_D, UNIT_FLEX_D, NULL, "FLEX", &tap_attable },
    { UNIT_FLEX_D, 0,           NULL, "ASCII", &tap_attable },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT+UNIT_FLEX,
      "file is Flex", NULL },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT,
      "file is ASCII", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE+UNIT_FLEX,
      "default is Flex", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE,
      "default is ASCII", NULL },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 16, 7,
    NULL, NULL, &ptr_reset,
    NULL, &tap_attach, NULL
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_mod      PTP modifier list
   ptp_reg      PTP register list
*/

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), WPS / 20
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

MTAB ptp_mod[] = {
    { UNIT_FLEX_D, UNIT_FLEX_D, NULL, "FLEX", &tap_attable },
    { UNIT_FLEX_D, 0,           NULL, "ASCII", &tap_attable },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT+UNIT_FLEX,
      "file is Flex", NULL },
    { UNIT_ATT+UNIT_FLEX, UNIT_ATT,
      "file is ASCII", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE+UNIT_FLEX,
      "default is Flex", NULL },
    { UNIT_ATTABLE+UNIT_ATT+UNIT_FLEX, UNIT_ATTABLE,
      "default is ASCII", NULL },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 16, 7,
    NULL, NULL, &ptp_reset,
    NULL, &tap_attach, NULL
    };

/* Input instruction */

void op_i_strt (uint32 dev)
{
switch (dev) {                                          /* case on device */

    case DEV_PT:                                        /* ptr */
        sim_activate (&ptr_unit, ptr_unit.wait);        /* activate */
        break;

    case DEV_TT:                                        /* tti/ttr */
        if (Q_MANI)                                     /* manual input? */
            sim_putchar ('`');
        else sim_activate (&tti_unit[1], tt_wait);      /* no, must be ttr */
        break;
        }
return;
}           

t_stat op_i (uint32 dev, uint32 ch, uint32 sh4)
{
if (Q_LGP21 && out_strt)                                /* LGP-21? must be idle */
    return STOP_STALL;
if (!inp_strt) {                                        /* input started? */
    inp_strt = 1;                                       /* no, set start */
    inp_done = 0;                                       /* clear done */
    A = shift_in (A, ch, sh4);
    tti_rdy = ptr_rdy = 0;                              /* no input */
    if (Q_LGP21 || Q_INPT)                              /* LGP-21 or PTR? start */
        op_i_strt (dev);
    }
switch (dev) {                                          /* case on device */

    case DEV_PT:                                        /* ptr */
        if (ptr_rdy) {                                  /* char ready? */
            ptr_rdy = 0;                                /* reset ready */
            if ((ptr_unit.buf != FLEX_DEL) &&           /* ignore delete and */
                (!Q_LGP21 || ((ptr_unit.buf & 3) == 2))) /* LGP-21 4b? zone != 2 */
                A = shift_in (A, ptr_unit.buf, sh4);    /* shift data in */
            }
        break;

    case DEV_TT:                                        /* tti/ttr */
        if (tti_rdy) {                                  /* char ready? */
            tti_rdy = 0;                                /* reset ready */
            if ((tti_buf != FLEX_DEL) &&                /* ignore delete and */
                (!Q_LGP21 || ((tti_buf & 3) != 0)))     /* LGP-21 4b? zone == 0 */
                A = shift_in (A, tti_buf, sh4);         /* shift data in */
            }
        break;

    default:                                            /* nx device */
        return STOP_NXDEV;                              /* return error */
        }

if (inp_done) {                                         /* done? */
    inp_strt = inp_done = 0;                            /* clear start, done */
    return SCPE_OK;                                     /* no stall */
    }
return STOP_STALL;                                      /* stall */
}

/* Terminal keyboard unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 c, flex;

sim_activate (uptr, tt_wait);                           /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
flex = ascii_to_flex[c & 0x1FF];                        /* cvt to flex */
if (flex > 0) {                                         /* it's a typewriter... */
    write_tto (flex);                                   /* always echos */
    if (tto_unit[1].flags & UNIT_ATT)                   /* ttp attached? */
        write_punch (&tto_unit[1], tto_buf);            /* punch to ttp */
    }
else write_tto ('\a');                                  /* don't echo bad */
if (Q_MANI && (flex > 0) && flex_inp_valid[flex]) {     /* wanted, valid? */
    if (flex == FLEX_CSTOP)                             /* conditional stop? */
        inp_done = 1;
    else tti_rdy = 1;                                   /* no, set ready */
    tti_buf = flex;                                     /* save char */
    uptr->pos = uptr->pos + 1;
    }
return SCPE_OK;
}

/* Terminal reader unit service */

t_stat ttr_svc (UNIT *uptr)
{
t_stat r;

if ((r = read_reader (uptr, ttr_stopioe, (int32 *) &tti_buf)))
    return r;
if (!(uptr->flags & UNIT_NOCS) &&                       /* cstop enable? */
    (tti_buf == FLEX_CSTOP))                            /* cond stop? */
    inp_done = 1;
else {
    tti_rdy = 1;                                        /* no, set ready */
    sim_activate (uptr, tt_wait);                       /* cont reading */
    }
write_tto (tti_buf);                                    /* echo to tto */
if (tto_unit[1].flags & UNIT_ATT)                       /* ttp attached? */
    return write_punch (&tto_unit[1], tti_buf);         /* punch to ttp */
return SCPE_OK;
}

/* Paper tape reader unit service */

t_stat ptr_svc (UNIT *uptr)
{
t_stat r;

if ((r = read_reader (uptr, ptr_stopioe, &uptr->buf)))
    return r;
if (uptr->buf == FLEX_CSTOP)                            /* cond stop? */
    inp_done = 1;
else {
    ptr_rdy = 1;                                        /* no, set ready */
    sim_activate (uptr, uptr->wait);                    /* cont reading */
    }
return SCPE_OK;
}

/* Output instruction */

t_stat op_p (uint32 dev, uint32 ch)
{
switch (dev) {                                          /* case on device */

    case DEV_PT:                                        /* paper tape punch */
        if (sim_is_active (&ptp_unit))                  /* busy? */
            return (Q_LGP21? STOP_STALL: SCPE_OK);      /* LGP-21: stall */
        ptp_unit.buf = ch;                              /* save char */
        sim_activate (&ptp_unit, ptp_unit.wait);        /* activate ptp */
        break;

    case DEV_TT:                                        /* typewriter */
        if (ch == 0) {                                  /* start input? */
            if (!Q_LGP21 && !Q_INPT)                    /* ignore if LGP-21, ptr */
                op_i_strt (DEV_TT);                     /* start tti */
            return SCPE_OK;                             /* no stall */
            }
        if (sim_is_active (&tto_unit[0]))               /* busy? */
            return (Q_LGP21? STOP_STALL: SCPE_OK);      /* LGP-21: stall */
        tto_buf = ch;                                   /* save char */
        sim_activate (&tto_unit[0], tt_wait);           /* activate tto */
        break;

    default:                                            /* unknown */
        return STOP_NXDEV;                              /* return error */
        }

if (out_strt == 0) {                                    /* output started? */
    out_strt = 1;                                       /* flag start */
    out_done = 0;                                       /* clear done */
    }
return SCPE_OK;                                         /* no stall */
}

/* Terminal printer unit service */

t_stat tto_svc (UNIT *uptr)
{
t_stat r;

if ((r = write_tto (tto_buf)) != SCPE_OK) {             /* output; error? */
    sim_activate (uptr, tt_wait);                       /* try again */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }
out_strt = 0;
out_done = 1;
if (tto_unit[1].flags & UNIT_ATT)                       /* ttp attached? */
    return write_punch (&tto_unit[1], tto_buf);         /* punch to ttp */
return SCPE_OK;
}

/* Paper tape punch unit service */

t_stat ptp_svc (UNIT *uptr)
{
out_strt = 0;
out_done = 1;
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);          /* error */
return write_punch (uptr, uptr->buf);                   /* write to ptp */
}

/* Utility routines */

t_stat read_reader (UNIT *uptr, int32 stop, int32 *fl)
{
int32 ch, flex;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (stop, SCPE_UNATT);
do {
    if ((ch = getc (uptr->fileref)) == EOF) {           /* read char */
        if (feof (uptr->fileref)) {                     /* err or eof? */
            if (stop)
                sim_printf ("Reader end of file\n");
            else return SCPE_OK;
            }
        else sim_perror ("Reader I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    if (uptr->flags & UNIT_FLEX)                        /* transposed flex? */
        flex = ((ch << 1) | (ch >> 5)) & 0x3F;          /* undo 612345 */
    else if (ch == '!') {                               /* encoded? */
        int32 d1 = getc (uptr->fileref);                /* get 2 digits */
        int32 d2 = getc (uptr->fileref);
        if ((d1 == EOF) || (d2 == EOF)) {               /* error? */
            if (feof (uptr->fileref)) {                 /* eof? */
                if (stop)
                    sim_printf ("Reader end of file\n");
                else return SCPE_OK;
                }
            else sim_perror ("Reader I/O error");
            clearerr (uptr->fileref);
            return SCPE_IOERR;
            }
        flex = (((d1 - '0') * 10) + (d2 - '0')) & 0x3F;
        uptr->pos = uptr->pos + 2;
        }
    else flex = ascii_to_flex[ch & 0x7F];               /* convert */
    uptr->pos = uptr->pos + 1;
    } while (flex < 0);                                 /* until valid */
*fl = flex;                                             /* return char */
return SCPE_OK;
}

t_stat write_tto (int32 flex)
{
int32 ch;
t_stat r;

if (flex == FLEX_UC)                                    /* UC? set state */
    tto_uc = 1;
else if (flex == FLEX_LC)                               /* LC? set state */
    tto_uc = 0;
else {
    if (flex == FLEX_BS)                                /* backspace? */
        ch = '\b';
    else ch = flex_to_ascii[flex | (tto_uc << 6)];      /* cvt flex to ascii */
    if (ch > 0) {                                       /* legit? */
        if ((r = sim_putchar_s (ch)))                   /* write char */
            return r;
        tto_unit[0].pos = tto_unit[0].pos + 1;
        if (flex == FLEX_CR) {                          /* cr? */
            sim_putchar ('\n');                         /* add lf */
            tto_unit[0].pos = tto_unit[0].pos + 1;
            }
        }
    }
return SCPE_OK;
}

t_stat write_punch (UNIT *uptr, int32 flex)
{
int32 c, sta;

if (uptr->flags & UNIT_FLEX)                            /* transposed flex? */
    c = ((flex >> 1) | (flex << 5)) & 0x3F;             /* reorder to 612345 */
else c = flex_to_ascii[flex];                           /* convert to ASCII */
if (c >= 0)                                             /* valid? */
    sta = fputc (c, uptr->fileref);
else sta = fprintf (uptr->fileref, "!%02d", flex);      /* no, encode */
if (sta == EOF) {                                       /* error? */
    sim_perror ("Punch I/O error");                         /* error? */
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
uptr->pos = uptr->pos + ((c >= 0)? 1: 3);               /* incr position */
return SCPE_OK;
}

/* Reset routines */

t_stat tti_reset (DEVICE *dptr)
{
sim_activate (&tti_unit[0], tt_wait);
sim_cancel (&tti_unit[1]);
tti_buf = 0;
tti_rdy = 0;
return SCPE_OK;
}

t_stat tto_reset (DEVICE *dptr)
{
sim_cancel (&tto_unit[0]);
tto_buf = 0;
tto_uc = 0;
return SCPE_OK;
}

t_stat ptr_reset (DEVICE *dptr)
{
sim_cancel (&ptr_unit);
ptr_unit.buf = 0;
ptr_rdy = 0;
return SCPE_OK;
}

t_stat ptp_reset (DEVICE *dptr)
{
sim_cancel (&ptp_unit);
ptp_unit.buf = 0;
return SCPE_OK;
}

/* Attach paper tape unit */

t_stat tap_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

if ((r = attach_unit (uptr,cptr)) != SCPE_OK)
    return r;
if ((sim_switches & SWMASK ('F')) ||
    ((uptr->flags & UNIT_FLEX_D) && !(sim_switches & SWMASK ('A'))))
        uptr->flags = uptr->flags | UNIT_FLEX;
else uptr->flags = uptr->flags & ~UNIT_FLEX;
return SCPE_OK;
}

/* Validate unit is attachable */

t_stat tap_attable (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATTABLE)
    return SCPE_OK;
return SCPE_NOFNC;
}

/* Typewriter reader start/stop */

t_stat tti_rdrss (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val) {
    if ((tti_unit[1].flags & UNIT_ATT) == 0)
        return SCPE_UNATT;
    sim_activate (&tti_unit[1], tt_wait);
    }
else sim_cancel (&tti_unit[1]);
return SCPE_OK;
}

/* Punch feed routine */

t_stat punch_feed (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 cnt;
t_stat r;

if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
if (cptr) {
    cnt = (int32) get_uint (cptr, 10, 512, &r);
    if ((r != SCPE_OK) || (cnt == 0))
        return SCPE_ARG;
    }
else cnt = 10;
while (cnt-- > 0) {
    r = write_punch (uptr, 0);
    if (r != SCPE_OK)
        return r;
    }
return SCPE_OK;
}

/* Send start signal */

t_stat send_start (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (inp_strt)
    inp_done = 1;
else if (out_strt)
    out_done = 1;
return SCPE_OK;
}
