/* i1401_cd.c: IBM 1402 card reader/punch

   Copyright (c) 1993-2017, Robert M. Supnik

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

   cdr          card reader
   cdp          card punch
   stack        stackers (5 units)
        0       normal
        1       1
        2       2/8
        3       unused
        4       4

   Cards are represented as ASCII text streams terminated by newlines.
   This allows cards to be created and edited as normal files.

   09-Mar-17    RMS     Protect character conversions from gargage files (COVERITY)
   05-May-16    RMS     Fixed calling sequence inconsistency (Mark Pizzolato)
   28-Feb-15    RMS     Added read from console
   24-Mar-09    RMS     Fixed read stacker operation in column binary mode
                        Fixed punch stacker operation (Van Snyder)
   28-Jun-07    RMS     Added support for SS overlap modifiers
   19-Jan-07    RMS     Added UNIT_TEXT flag
   20-Sep-05    RMS     Revised for new code tables, compatible colbinary treatment
   30-Aug-05    RMS     Fixed read, punch to ignore modifier on 1,4 char inst
                        (Van Snyder)
   14-Nov-04    WVS     Added column binary support
   25-Apr-03    RMS     Revised for extended file support
   30-May-02    RMS     Widened POS to 32b
   30-Jan-02    RMS     New zero footprint card bootstrap (Van Snyder)
   29-Nov-01    RMS     Added read only unit support
   13-Apr-01    RMS     Revised for register arrays
*/

/* Read from console was requested by the 1401 restoration team at the
   Computer History Museum. It allows small programs to be entered
   quickly, without creating card files. Unfortunately, if input is
   coming from the keyboard, then the card reader is not attached,
   and it won't boot.

   To deal with this problem, the card reader must keep various
   unit flags in a consistent state:

   ATTABLE?     ATT?        DFLT?           state

   0            0           0               impossible
   0            0           1               input from console
   0            1           0               impossible
   0            1           1               impossible
   1            0           0               waiting for file
   1            0           1               impossible
   1            1           0               input from file
   1            1           1               input from file,
                                            default to console
                                            after detach

   To maintain this state, starting from 100, means the
   following:

   SET CDR DFLT             set default flag
                            if !ATT, clear ATTABLE
   SET CDR NODFLT           clear default flag
   ATTACH CDR               set ATTABLE, attach
                            if error && DFLT, clear ATTABLE
   DETACH CDR               detach
                            if DFLT, clear ATTABLE
*/

#include "i1401_defs.h"
#include <ctype.h>

#define UNIT_V_PCH      (UNIT_V_UF + 0)                 /* output conv */
#define UNIT_PCH        (1 << UNIT_V_PCH)
#define UNIT_V_CONS     (UNIT_V_UF + 1)                 /* input from console */
#define UNIT_CONS       (1 << UNIT_V_CONS)

extern uint8 M[];
extern int32 ind[64], ssa, iochk;
extern int32 conv_old;

int32 s1sel, s2sel, s4sel, s8sel;
char cdr_buf[(2 * CBUFSIZE) + 1];                       /* > CDR_WIDTH */
char cdp_buf[(2 * CDP_WIDTH) + 1];                      /* + null */
int32 cdp_buf_full = 0;                                 /* punch buf full? */

t_stat cdr_svc (UNIT *uptr);
t_stat cdr_boot (int32 unitno, DEVICE *dptr);
t_stat cdr_attach (UNIT *uptr, CONST char *cptr);
t_stat cdr_detach (UNIT *uptr);
t_stat cdp_attach (UNIT *uptr, CONST char *cptr);
t_stat cdp_detach (UNIT *uptr);
t_stat cdp_npr (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cd_reset (DEVICE *dptr);
t_stat cdr_read_file (char *buf, int32 sz);
t_stat cdr_read_cons (char *buf, int32 sz);
t_stat cdr_chg_cons (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
int32 bcd2asc (int32 c, UNIT *uptr);
char colbin_to_bcd (uint32 cb);

extern void inq_puts (const char *cptr);

/* Card reader data structures

   cdr_dev      CDR descriptor
   cdr_unit     CDR unit descriptor
   cdr_reg      CDR register list
*/

UNIT cdr_unit = {
    UDATA (&cdr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE+UNIT_TEXT, 0), 100
    };

REG cdr_reg[] = {
    { FLDATA (LAST, ind[IN_LST], 0) },
    { FLDATA (ERR, ind[IN_READ], 0) },
    { FLDATA (S1, s1sel, 0) },
    { FLDATA (S2, s2sel, 0) },
    { DRDATA (POS, cdr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, cdr_unit.wait, 24), PV_LEFT },
    { BRDATA (BUF, cdr_buf, 8, 8, sizeof (cdr_buf)) },
    { NULL }
    };

MTAB cdr_mod[] = {
    { UNIT_CONS, UNIT_CONS, "default to console", "DEFAULT", &cdr_chg_cons },
    { UNIT_CONS, 0        , "no default device", "NODEFAULT", &cdr_chg_cons },
    { 0 }
    };

DEVICE cdr_dev = {
    "CDR", &cdr_unit, cdr_reg, cdr_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cd_reset,
    &cdr_boot, &cdr_attach, &cdr_detach
    };

/* CDP data structures

   cdp_dev      CDP device descriptor
   cdp_unit     CDP unit descriptor
   cdp_reg      CDP register list
*/

UNIT cdp_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0)
    };

REG cdp_reg[] = {
    { FLDATA (ERR, ind[IN_PNCH], 0) },
    { FLDATA (S4, s4sel, 0) },
    { FLDATA (S8, s8sel, 0) },
    { DRDATA (POS, cdp_unit.pos, T_ADDR_W), PV_LEFT },
    { BRDATA (BUF, cdp_buf, 8, 8, CDP_WIDTH * 2) },
    { FLDATA (FULL, cdp_buf_full, 0) },
    { NULL }
    };

MTAB cdp_mod[] = {
    { UNIT_PCH, 0,        "business set", "BUSINESS" },
    { UNIT_PCH, UNIT_PCH, "Fortran set", "FORTRAN" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "NPR",
      &cdp_npr, NULL },
    { 0 }
    };

DEVICE cdp_dev = {
    "CDP", &cdp_unit, cdp_reg, cdp_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cd_reset,
    NULL, &cdp_attach, &cdp_detach
    };

/* Stacker data structures

   stack_dev    STACK device descriptor
   stack_unit   STACK unit descriptors
   stack_reg    STACK register list
*/

UNIT stack_unit[] = {
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0) }
    };

REG stack_reg[] = {
    { DRDATA (POS0, stack_unit[0].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (POS1, stack_unit[1].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (POS28, stack_unit[2].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (POS4, stack_unit[4].pos, T_ADDR_W), PV_LEFT },
    { NULL }
    };

DEVICE stack_dev = {
    "STKR", stack_unit, stack_reg, NULL,
    5, 10, 31, 1, 8, 7,
    NULL, NULL, &cd_reset,
    NULL, NULL, NULL
    };

/* Card read routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/

t_stat read_card (int32 ilnt, int32 mod)
{
int32 i, cbn, c1, c2, cbufsz;
t_stat r;

if (sim_is_active (&cdr_unit)) {                        /* busy? */
    sim_cancel (&cdr_unit);                             /* cancel */
    if ((r = cdr_svc (&cdr_unit)))                      /* process */
        return r;
    }
ind[IN_READ] = ind[IN_LST] = s1sel = s2sel = 0;         /* default stacker */
cbn = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_C);   /* col binary? */
cbufsz = (cbn)? 2 * CBUFSIZE: CBUFSIZE;                 /* buffer size */
for (i = 0; i < (2 * CBUFSIZE) + 1; i++)                /* clear extended buf */
    cdr_buf[i] = 0;
if ((cdr_unit.flags & UNIT_ATT) != 0)                   /* attached? */
    r = cdr_read_file (cdr_buf, cbufsz);                /* read from file */
else if ((cdr_unit.flags & UNIT_CONS) != 0)             /* default to console? */
    r = cdr_read_cons (cdr_buf, cbufsz);                /* read from console */
else return SCPE_UNATT;                                 /* else can't read */
if (r != SCPE_OK)                                       /* read error? */
    return r;                                           /* can't read */
if (cbn) {                                              /* column binary? */
    for (i = 0; i < CDR_WIDTH; i++) {
        if (conv_old) {
            c1 = ascii2bcd (cdr_buf[i] & 0177);
            c2 = ascii2bcd (cdr_buf[CDR_WIDTH + i] & 0177);
            }
        else {
            c1 = ascii2bcd (cdr_buf[2 * i] & 0177);
            c2 = ascii2bcd (cdr_buf[(2 * i) + 1] & 0177);
            }
        M[CD_CBUF1 + i] = (M[CD_CBUF1 + i] & WM) | c1;
        M[CD_CBUF2 + i] = (M[CD_CBUF2 + i] & WM) | c2;
        M[CDR_BUF + i] = colbin_to_bcd ((c1 << 6) | c2);
        }
    }                                                   /* end if col bin */
else {                                                  /* normal read */
    for (i = 0; i < CDR_WIDTH; i++) {                   /* cvt to BCD */
        c1 = ascii2bcd (cdr_buf[i]);
        M[CDR_BUF + i] = (M[CDR_BUF + i] & WM) | c1;
        }
    }
M[CDR_BUF - 1] = 060;                                   /* mem mark */
sim_activate (&cdr_unit, cdr_unit.wait);                /* activate */
return SCPE_OK;
}

/* Card reader service.  If a stacker select is active, copy to the
   selected stacker.  Otherwise, copy to the normal stacker.  If the
   unit is unattached, simply exit.

   The original card buffer (cdr_buf) has not been changed from its input
   format (ASCII text), with its newline attached. There is a guaranteed
   null at the end, because the buffer was zeroed prior to the read, and
   is one character longer than the maximum string length.
*/

t_stat cdr_svc (UNIT *uptr)
{
if (s1sel)                                              /* stacker 1? */
    uptr = &stack_unit[1];
else if (s2sel)                                         /* stacker 2? */
    uptr = &stack_unit[2];
else uptr = &stack_unit[0];                             /* then default */
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
fputs (cdr_buf, uptr->fileref);                         /* write card */
uptr->pos = ftell (uptr->fileref);                      /* update position */
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("Card stacker I/O error");
    clearerr (uptr->fileref);
    if (iochk)
        return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)

   - Run out any previously buffered card
   - Clear stacker select
   - Copy card from memory buffer to punch buffer
*/

t_stat punch_card (int32 ilnt, int32 mod)
{
int32 i, cbn, c1, c2;
t_bool use_h;
t_stat r;

r = cdp_npr (NULL, 0, NULL, NULL);                      /* write card */
if (r != SCPE_OK)
    return r;
use_h = cdp_unit.flags & UNIT_PCH;
ind[IN_PNCH] = s4sel = s8sel = 0;                       /* clear flags */
cbn = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_C);   /* col binary? */

M[CDP_BUF - 1] = 012;                                   /* set prev loc */
if (cbn) {                                              /* column binary */
    for (i = 0; i < CDP_WIDTH; i++) {
        c1 = bcd2ascii (M[CD_CBUF1 + i] & CHAR, use_h);
        c2 = bcd2ascii (M[CD_CBUF2 + i] & CHAR, use_h);
        if (conv_old) {
            cdp_buf[i] = c1;
            cdp_buf[i + CDP_WIDTH] = c2;
            }
        else {
            cdp_buf[2 * i] = c1;
            cdp_buf[(2 * i) + 1] = c2;
            }
        }
    for (i = (2 * CDP_WIDTH) - 1; (i >= 0) && (cdp_buf[i] == ' '); i--)
         cdp_buf[i] = 0;
    cdp_buf[2 * CDP_WIDTH] = 0;                         /* trailing null */
    }
else {                                                  /* normal */
    for (i = 0; i < CDP_WIDTH; i++)
        cdp_buf[i] = bcd2ascii (M[CDP_BUF + i] & CHAR, use_h);
    for (i = CDP_WIDTH - 1; (i >= 0) && (cdp_buf[i] == ' '); i--)
        cdp_buf[i] = 0;
    cdp_buf[CDP_WIDTH] = 0;                             /* trailing null */
    }
cdp_buf_full = 1;                                       /* card buffer full */
return SCPE_OK;
}

/* Punch buffered card (also handles non-process runout button) */

t_stat cdp_npr (UNIT *notused, int32 val, CONST char *cptr, void *desc)
{
UNIT *uptr;

if (cdp_buf_full == 0)                                  /* any card? */
    return SCPE_OK;                                     /* no, done */
cdp_buf_full = 0;                                       /* buf empty */
if (s8sel)                                              /* stack 8? */
    uptr = &stack_unit[2];
else if (s4sel)                                         /* stack 4? */
    uptr = &stack_unit[4];
else uptr = &cdp_unit;                                  /* normal output */
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_UNATT;
fputs (cdp_buf, uptr->fileref);                         /* output card */
fputc ('\n', uptr->fileref);                            /* plus new line */
uptr->pos = ftell (uptr->fileref);                      /* update position */
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("Card punch I/O error");
    clearerr (uptr->fileref);
    if (iochk)
        return SCPE_IOERR;
    ind[IN_PNCH] = 1;
    }
return SCPE_OK;
}

/* Select stack routine

   Modifiers have been checked by the caller
   Modifiers are 1, 2, 4, 8 for the respective stack,
   or $, ., square for overlap control (ignored).
*/

t_stat select_stack (int32 mod)
{
if (mod == BCD_ONE)
    s1sel = 1;
else if (mod == BCD_TWO)
    s2sel = 1;
else if (mod == BCD_FOUR)
    s4sel = 1;
else if (mod == BCD_EIGHT)
    s8sel = 1;
return SCPE_OK;
}

/* Read card from file */

t_stat cdr_read_file (char *buf, int32 sz)
{
if (fgets (buf, sz, cdr_unit.fileref)) {};              /* rd bin/char card */
if (feof (cdr_unit.fileref))                            /* eof? */
    return STOP_NOCD;
if (ferror (cdr_unit.fileref)) {                        /* error? */
    ind[IN_READ] = 1;  
    sim_perror ("Card reader I/O error");
    clearerr (cdr_unit.fileref);
    if (iochk)
        return SCPE_IOERR;
    return SCPE_OK;
    }
cdr_unit.pos = ftell (cdr_unit.fileref);                /* update position */
if (ssa) {                                              /* if last cd on */
    getc (cdr_unit.fileref);                            /* see if more */
    if (feof (cdr_unit.fileref))                        /* eof? set flag */
        ind[IN_LST] = 1;
    fseek (cdr_unit.fileref, cdr_unit.pos, SEEK_SET);
    }
return SCPE_OK;
}

/* Read card from console */

t_stat cdr_read_cons (char *buf, int32 sz)
{
int32 i, t;

inq_puts ("[Enter card]\r\n");
for (i = 0; i < sz; ) {
    while (((t = sim_poll_kbd ()) == SCPE_OK) ||        /* wait for char */
        (t & SCPE_BREAK)) {
        if (stop_cpu)                                   /* stop? */
            return t;
        }
    if (t < SCPE_KFLAG)                                 /* error? */
        return t;
    t = t & 0177;
    if ((t == '\r') || (t == '\n'))                     /* eol? */
        break;
    if (t == 0177) {                                    /* rubout? */
        if (i != 0) {                                   /* anything? */
            buf[--i] = 0;
            sim_putchar ('\\');
            }
        }
    else {
        sim_putchar (t);
        buf[i++] = t;
        }
    }
inq_puts ("\r\n");
return SCPE_OK;
}

/* Card reader/punch reset */

t_stat cd_reset (DEVICE *dptr)
{
ind[IN_LST] = ind[IN_READ] = ind[IN_PNCH] = 0;          /* clear indicators */
s1sel = s2sel = s4sel = s8sel = 0;                      /* clear stacker sel */
sim_cancel (&cdr_unit);                                 /* clear reader event */
return SCPE_OK;
}

/* Set/clear default to console flag

   Caller will do actual bit field update on successful return */

t_stat cdr_chg_cons (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val == 0)                                           /* clear? */
    cdr_unit.flags |= UNIT_ATTABLE;                     /* attachable on */
else if ((cdr_unit.flags & UNIT_ATT) == 0)              /* set, unattached? */
    cdr_unit.flags &= ~UNIT_ATTABLE;                    /* attachable off */
return SCPE_OK;
}

/* Card reader attach */

t_stat cdr_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

ind[IN_LST] = ind[IN_READ] = 0;                         /* clear last card */
cdr_unit.flags |= UNIT_ATTABLE;                         /* must be attachable */
r = attach_unit (uptr, cptr);                           /* do attach */
if ((r != SCPE_OK) && ((cdr_unit.flags & UNIT_CONS) != 0)) /* failed, default? */
    cdr_unit.flags &= ~UNIT_ATTABLE;                    /* clear attachable */
return r;
}

/* Card reader detach */

t_stat cdr_detach (UNIT *uptr)
{
t_stat r;

cdr_unit.flags |= UNIT_ATTABLE;                         /* must be attachable */
r = detach_unit (uptr);                                 /* detach */
if (((cdr_unit.flags & UNIT_ATT) == 0) &&               /* attached clear? */
    ((cdr_unit.flags & UNIT_CONS) != 0))                /* default on? */
    cdr_unit.flags &= ~UNIT_ATTABLE;                    /* clear attachable */
return r;
}

/* Bootstrap routine */

#define BOOT_START 0
#define BOOT_LEN (sizeof (boot_rom) / sizeof (unsigned char))

static const unsigned char boot_rom[] = {
    OP_R + WM, OP_NOP + WM                              /* R, NOP */
    };

t_stat cdr_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_IS;

for (i = 0; i < CDR_WIDTH; i++)                         /* clear buffer */
    M[CDR_BUF + i] = 0;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
saved_IS = BOOT_START;
return SCPE_OK;
}

/* Card punch attach */

t_stat cdp_attach (UNIT *uptr, CONST char *cptr)
{
cdp_buf_full = 0;
return attach_unit (uptr, cptr);
}

/* Card punch detach */

t_stat cdp_detach (UNIT *uptr)
{
t_stat r;

r = cdp_npr (NULL, 0, NULL, NULL);
if (r != SCPE_OK)
    return r;
return detach_unit (uptr);
}

/* Column binary to BCD

   This is based on documentation in the IBM 1620 manual and may not be
   accurate for the 7094.  Each row (12,11,0,1..9) is interpreted as a bit
   pattern, and the appropriate bits are set.  (Double punches inclusive
   OR, eg, 1,8,9 is 9.)  On the 1620, double punch errors are detected;
   since the 7094 only reads column binary, double punches are ignored.

   Bit order, left to right, is 12, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9.
   The for loop works right to left, so the table is reversed. */

static const char row_val[12] = {
    011, 010, 007, 006, 005, 004,
    003, 002, 001, 020, 040, 060
    };

char colbin_to_bcd (uint32 cb)
{
uint32 i;
char bcd;

for (i = 0, bcd = 0; i < 12; i++) {                     /* 'sum' rows */
    if (cb & (1 << i))
        bcd |= row_val[i];
    }
return bcd;
}
