/* h316_stddev.c: Honeywell 316/516 standard devices

   Copyright (c) 1999-2013, Robert M. Supnik

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

   ptr          316/516-50 paper tape reader
   ptp          316/516-52 paper tape punch
   tty          316/516-33 teleprinter
   clk/options  316/516-12 real time clocks/internal options

   10-Sep-13    RMS     Fixed several bugs in the TTY logic
                        Added SET file type commands to PTR/PTP
    3-Jul-13    RLA     compatibility changes for extended interrupts
   09-Jun-07    RMS     Fixed bug in clock increment (Theo Engel)
   30-Sep-06    RMS     Fixed handling of non-printable characters in KSR mode
   03-Apr-06    RMS     Fixed bugs in punch state handling (Theo Engel)
   22-Nov-05    RMS     Revised for new terminal processing routines
   05-Feb-05    RMS     Fixed bug in OCP '0001 (Philipp Hachtmann)
   31-Jan-05    RMS     Fixed bug in TTY print (Philipp Hachtmann)
   01-Dec-04    RMS     Fixed problem in SKS '104 (Philipp Hachtmann)
                        Fixed bug in SKS '504
                        Added PTR detach routine, stops motion
                        Added PTR/PTP ASCII file support
                        Added TTR/TTP support
   24-Oct-03    RMS     Added DMA/DMC support
   25-Apr-03    RMS     Revised for extended file support
   01-Mar-03    RMS     Added SET/SHOW CLK FREQ support
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Added 7b/8b support to terminal
   30-May-02    RMS     Widened POS to 32b
   03-Nov-01    RMS     Implemented upper case for console output
   29-Nov-01    RMS     Added read only unit support
   07-Sep-01    RMS     Moved function prototypes

   The ASR-33/35 reader/punch logic, and the ASCII file support for all paper tape
   devices, is taken, with grateful thanks, from Adrian Wise's H316 emulator.

   Teletype transitions:

   - An OCP '1 starts an output sequence, unconditionally. Ready and Busy are both
     set, and a dummy output sequence is started.
   - If OTA "overtakes" the dummy output sequence, the dummy sequence is stopped,
     and normal output takes place.
   - If OTA is not issued before the dummy sequence completes, Busy is cleared.
     Because Ready is set, an interrupt is requested.
   - An OCP '0 starts an input sequence, unconditionally. Ready and Busy are both
     cleared.
   - When a character is available (either from the keyboard or the reader), Busy
     is set.
   - At the end of a delay, Busy is cleared and Ready is set, and an interrupt is
     requested.
   - At all times, the interrupt flag reflects the equation Ready & ~Busy.

   Teletype reader transitions:

   - SET TTY2 START puts the reader in RUN.
   - XOFF from keyboard/reader stops the reader after 1-2 more characters are read.
   - XON from program starts the reader.
   - Detach, SET TTY2 STOP, or end of file stops the reader.

   Teletype punch transitions:

   - SET TTY3 START puts the punch in RUN.
   - XOFF from program stops the punch after 1 more character is punched.
   - TAPE from program starts the punch after 1 character delay.
   - Detach or SET TTY3 STOP stops the punch.
*/

#include "h316_defs.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define UNIT_V_ASC      (TTUF_V_UF + 0)                 /* ASCII */
#define UNIT_V_UASC     (TTUF_V_UF + 1)                 /* Unix ASCII */
#define UNIT_ASC        (1 << UNIT_V_ASC)
#define UNIT_UASC       (1 << UNIT_V_UASC)
#define STA             u3                              /* state bits */
#define LF_PEND         01                              /* lf pending */
#define RUNNING         02                              /* tape running */

#define XON             0021
#define TAPE            0022
#define XOFF            0023
#define RUBOUT          0377

extern uint16 M[];
extern int32 PC;
extern int32 stop_inst;
extern int32 C, dp, ext, extoff_pending, sc;
extern int32 dev_int, dev_enb;
extern UNIT cpu_unit;

uint32 ptr_motion = 0;                                  /* read motion */
uint32 ptr_stopioe = 0;                                 /* stop on error */
uint32 ptp_stopioe = 0;
uint32 ptp_power = 0;                                   /* punch power, time */
int32 ptp_ptime;
uint32 ttr_stopioe = 0;
uint32 tty_mode = 0;                                    /* input (0), output (1) */
uint32 tty_buf = 0;                                     /* tty buffer */
uint32 tty_ready = 1;                                   /* tty ready */
uint32 tty_busy = 0;                                    /* tty busy */
uint32 tty_2nd = 0;                                     /* tty input second state */
uint32 ttr_xoff_read = 0;
uint32 ttp_tape_rcvd = 0;
uint32 ttp_xoff_rcvd = 0;
int32 tty_busy_wait = SERIAL_IN_WAIT;                   /* busy state on input */
int32 clk_tps = 60;                                     /* ticks per second */

int32 ptrio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
int32 ptpio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
int32 ttyio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat ttio_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ttrp_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ttrp_set_start_stop (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 clkio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
t_stat clk_set_freq (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat pt_attach (UNIT *uptr, char *cptr);
t_stat pt_detach (UNIT *uptr);
t_stat tto_write (int32 c);
t_stat ttp_write (int32 c);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_mod      PTR modifiers
   ptr_reg      PTR register list
*/

DIB ptr_dib = { PTR, 1, IOBUS, IOBUS, INT_V_PTR, INT_V_NONE, &ptrio, 0 };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATA (BUF, ptr_unit.buf, 8) },
    { FLDATA (READY, dev_int, INT_V_PTR) },
    { FLDATA (ENABLE, dev_enb, INT_V_PTR) },
    { FLDATA (MOTION, ptr_motion, 0) },
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { ORDATA (RSTATE, ptr_unit.STA, 2), REG_HIDDEN },
    { FLDATA (STOP_IOE, ptr_stopioe, 0) },
    { NULL }
    };

MTAB pt_mod[] = {
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE, NULL, "BINARY",
      &ttrp_set_mode },
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE+UNIT_ASC, "ASCII", "ASCII",
      &ttrp_set_mode },
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, "Unix ASCII", "UASCII",
      &ttrp_set_mode },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, pt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, &pt_attach, &pt_detach,
    &ptr_dib, 0
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_mod      PTP modifiers
   ptp_reg      PTP register list
*/

DIB ptp_dib = { PTP, 1, IOBUS, IOBUS, INT_V_PTP, INT_V_NONE, &ptpio, 0 };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATA (BUF, ptp_unit.buf, 8) },
    { FLDATA (READY, dev_int, INT_V_PTP) },
    { FLDATA (ENABLE, dev_enb, INT_V_PTP) },
    { FLDATA (POWER, ptp_power, 0) },
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { ORDATA (PSTATE, ptp_unit.STA, 2), REG_HIDDEN },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { DRDATA (PWRTIME, ptp_ptime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ptp_stopioe, 0) },
    { NULL }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, pt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, &pt_attach, NULL,
    &ptp_dib, 0
    };

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
   tty_mod      TTy modifiers list
*/

#define TTI     0
#define TTO     1
#define TTR     2
#define TTP     3

DIB tty_dib = { TTY, 1, IOBUS, IOBUS, INT_V_TTY, INT_V_NONE, &ttyio, 0 };

UNIT tty_unit[] = {
    { UDATA (&tti_svc, TT_MODE_KSR, 0), KBD_POLL_WAIT },
    { UDATA (&tto_svc, TT_MODE_KSR, 0), SERIAL_OUT_WAIT },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) }
    };

REG tty_reg[] = {
    { ORDATA (BUF, tty_buf, 8) },
    { ORDATA (IN2ND, tty_2nd, 9) },
    { FLDATA (MODE, tty_mode, 0) },
    { FLDATA (READY, tty_ready, 0) },
    { FLDATA (BUSY, tty_busy, 0) },
    { FLDATA (INT, dev_int, INT_V_TTY) },
    { FLDATA (ENABLE, dev_enb, INT_V_TTY) },
    { DRDATA (KPOS, tty_unit[TTI].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (KTIME, tty_unit[TTI].wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (KBTIME, tty_busy_wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPOS, tty_unit[TTO].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TTIME, tty_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
    { ORDATA (RXOFF, ttr_xoff_read, 2), REG_HIDDEN },
    { ORDATA (RSTATE, tty_unit[TTR].STA, 2), REG_HIDDEN },
    { DRDATA (RPOS, tty_unit[TTR].pos, T_ADDR_W), PV_LEFT },
    { ORDATA (PTAPE, ttp_tape_rcvd, 2), REG_HIDDEN },
    { ORDATA (PXOFF, ttp_xoff_rcvd, 2), REG_HIDDEN },
    { ORDATA (PSTATE, tty_unit[TTP].STA, 2), REG_HIDDEN },
    { DRDATA (PPOS, tty_unit[TTP].pos, T_ADDR_W), PV_LEFT },
    { FLDATA (STOP_IOE, ttr_stopioe, 0) },
    { NULL }
    };

MTAB tty_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &ttio_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &ttio_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &ttio_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &ttio_set_mode },
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE, NULL, "BINARY",
      &ttrp_set_mode },
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE+UNIT_ASC, "ASCII", "ASCII",
      &ttrp_set_mode },
    { UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, UNIT_ATTABLE+UNIT_ASC+UNIT_UASC, "Unix ASCII", "UASCII",
      &ttrp_set_mode },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 1, NULL, "START", &ttrp_set_start_stop },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, NULL, "STOP", &ttrp_set_start_stop },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    4, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, &pt_attach, &pt_detach,
    &tty_dib, 0
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_mod      CLK modifiers
   clk_reg      CLK register list
*/

DIB clk_dib = { CLK_KEYS, 1, IOBUS, IOBUS, INT_V_CLK, INT_V_NONE, &clkio, 0 };

UNIT clk_unit = { UDATA (&clk_svc, 0, 0), 16000 };

REG clk_reg[] = {
    { FLDATA (READY, dev_int, INT_V_CLK) },
    { FLDATA (ENABLE, dev_enb, INT_V_CLK) },
    { DRDATA (TIME, clk_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, clk_tps, 8), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB clk_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &clk_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &clk_show_freq, NULL },
    { 0 }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, clk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    &clk_dib, 0
    };

/* Paper tape reader: IO routine */

int32 ptrio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        if ((fnc & 016) != 0)                           /* only fnc 0,1 */
            return IOBADFNC (dat);
        ptr_motion = fnc ^ 1;
        if (fnc != 0)                                   /* fnc 1? stop */
            sim_cancel (&ptr_unit);
        else sim_activate (&ptr_unit, ptr_unit.wait);   /* fnc 0? start */
        break;

    case ioSKS:                                         /* SKS */
        if ((fnc & 013) != 0)                           /* only fnc 0,4 */
            return IOBADFNC (dat);
        if (((fnc == 000) && TST_INT (INT_PTR)) ||      /* fnc 0? skip rdy */
            ((fnc == 004) && !TST_INTREQ (INT_PTR)))    /* fnc 4? skip !int */
            return IOSKIP (dat);
        break;

    case ioINA:                                         /* INA */
        if (fnc != 0)                                   /* only fnc 0 */
            return IOBADFNC (dat);
        if (TST_INT (INT_PTR)) {                        /* ready? */
            CLR_INT (INT_PTR);                          /* clear ready */
            if (ptr_motion)                             /* if motion, restart */
                sim_activate (&ptr_unit, ptr_unit.wait);
            return IOSKIP (ptr_unit.buf | dat);         /* ret buf, skip */
            }
        break;
        }                                               /* end case op */

return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 c;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (ptr_stopioe, SCPE_UNATT);
if (uptr->STA & LF_PEND) {                              /* lf pending? */
    uptr->STA &= ~LF_PEND;                              /* clear flag */
    c = 0212;                                           /* insert LF */
    }
else {
    if ((c = getc (uptr->fileref)) == EOF) {            /* read byte */
        if (feof (uptr->fileref)) {
            if (ptr_stopioe)
                printf ("PTR end of file\n");
            else return SCPE_OK;
            }
        else perror ("PTR I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    if ((uptr->flags & UNIT_UASC) && (c == '\n')) {     /* Unix newline? */
        c = 0215;                                       /* insert CR */
        uptr->STA |= LF_PEND;                           /* lf pending */
        }
    else if ((uptr->flags & UNIT_ASC) && (c != 0))      /* ASCII? */
        c = c | 0200;
    uptr->pos = ftell (uptr->fileref);                  /* update pos */
    }
SET_INT (INT_PTR);                                      /* set ready flag */
uptr->buf = c & 0377;                                   /* get byte */
return SCPE_OK;
}

/* Paper tape attach routine - set or clear ASC/UASC flags if specified
   Can be called for TTY units at well, hence, check for attachability */

t_stat pt_attach (UNIT *uptr, char *cptr)
{
t_stat r;

if (!(uptr->flags & UNIT_ATTABLE))                      /* not tti,tto */
    return SCPE_NOFNC;
if ((r = attach_unit (uptr, cptr)))
    return r;
if (sim_switches & SWMASK ('A'))                        /* -a? ASCII */
    uptr->flags |= UNIT_ASC;
else if (sim_switches & SWMASK ('U'))                   /* -u? Unix ASCII */
    uptr->flags |= (UNIT_ASC|UNIT_UASC);
else if (sim_switches & SWMASK ('B'))                   /* -b? binary */
    uptr->flags &= ~(UNIT_ASC|UNIT_UASC);
uptr->STA = 0;
return r;
}

/* Detach routine - stop motion if not restore */

t_stat pt_detach (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATTABLE))                      /* not tti,tto */
    return SCPE_NOFNC;
if (!(sim_switches & SIM_SW_REST))                      /* stop motion */
     sim_cancel (uptr);
uptr->STA = 0;
return detach_unit (uptr);
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
CLR_INT (INT_PTR);                                      /* clear ready, enb */
CLR_ENB (INT_PTR);
ptr_unit.buf = 0;                                       /* clear buffer */
ptr_unit.STA = 0;
ptr_motion = 0;                                         /* unit stopped */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Paper tape reader bootstrap routine */

#define PBOOT_START     1
#define PBOOT_SIZE      (sizeof (pboot) / sizeof (int32))

static const int32 pboot[] = {
    0010057,                                            /*        STA 57 */
    0030001,                                            /*        OCP 1 */
    0131001,                                            /* READ,  INA 1001 */
    0002003,                                            /*        JMP READ */
    0101040,                                            /*        SNZ */
    0002003,                                            /*        JMP READ */
    0010000,                                            /*        STA 0 */
    0131001,                                            /* READ1, INA 1001 */
    0002010,                                            /*        JMP READ1 */
    0041470,                                            /*        LGL 8 */
    0130001,                                            /* READ2, INA 1 */
    0002013,                                            /*        JMP READ2 */
    0110000,                                            /*        STA* 0 */
    0024000,                                            /*        IRS 0 */
    0100040                                             /*        SZE */
    };

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < PBOOT_SIZE; i++)                        /* copy bootstrap */
    M[PBOOT_START + i] = pboot[i];
PC = PBOOT_START;       
return SCPE_OK;
}

/* Paper tape punch: IO routine */

int32 ptpio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        if ((fnc & 016) != 0)                           /* only fnc 0,1 */
            return IOBADFNC (dat);
        if (fnc != 0) {                                 /* fnc 1? pwr off */
            CLR_INT (INT_PTP);                          /* not ready */
            ptp_power = 0;                              /* turn off power */
            sim_cancel (&ptp_unit);                     /* stop punch */
            }
        else if (ptp_power == 0)                        /* fnc 0? start */
            sim_activate (&ptp_unit, ptp_ptime);
        break;

    case ioSKS:                                         /* SKS */
        if (((fnc & 012) !=0) || (fnc == 005))          /* only 0, 1, 4 */
            return IOBADFNC (dat);
        if (((fnc == 000) && TST_INT (INT_PTP)) ||      /* fnc 0? skip rdy */
            ((fnc == 001) &&                            /* fnc 1? skip ptp on */
                (ptp_power || sim_is_active (&ptp_unit))) ||
            ((fnc == 004) && !TST_INTREQ (INT_PTP)))    /* fnc 4? skip !int */
            return IOSKIP (dat);
        break;

    case ioOTA:                                         /* OTA */
        if (fnc != 0)                                   /* only fnc 0 */
            return IOBADFNC (dat);
        if (TST_INT (INT_PTP)) {                        /* if ptp ready */
            CLR_INT (INT_PTP);                          /* clear ready */
            ptp_unit.buf = dat & 0377;                  /* store byte */
            sim_activate (&ptp_unit, ptp_unit.wait);
            return IOSKIP (dat);                        /* skip return */
            }
        break;
        }

return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int32 c;

SET_INT (INT_PTP);                                      /* set flag */
if (ptp_power == 0) {                                   /* power on? */
    ptp_power = 1;                                      /* ptp is ready */
    return SCPE_OK;
    }
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if (uptr->flags & UNIT_ASC) {                           /* ASCII? */
    c = uptr->buf & 0177;                               /* mask to 7b */
    if ((uptr->flags & UNIT_UASC) && (c == 015))        /* cr? drop if Unix */
        return SCPE_OK;
    else if (c == 012)                                  /* lf? cvt to nl */
        c = '\n';
    }
else c = uptr->buf & 0377;                              /* no, binary */
if (putc (c, uptr->fileref) == EOF) {                   /* output byte */
    perror ("PTP I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
uptr->pos = ftell (uptr->fileref);                      /* update pos */
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tty_unit[TTR], &tty_unit[TTO]);
CLR_INT (INT_PTP);                                      /* clear ready, enb */
CLR_ENB (INT_PTP);
ptp_power = 0;                                          /* power off */
ptp_unit.buf = 0;                                       /* clear buffer */
ptp_unit.STA = 0;
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Terminal: IO routine */

int32 ttyio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        if ((fnc & 016) != 0)                           /* only fnc 0,1 */
            return IOBADFNC (dat);
        if (fnc != 0) {                                 /* output */
            tty_ready = 1;                              /* set rdy, busy */
            tty_busy = 1;                               /* start dummy out */
            tty_mode = 1;                               /* mode is output */
            sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);
            }
        else {                                          /* input? */
            tty_ready = 0;                              /* clr rdy, busy */
            tty_busy = 0;
            tty_mode = 0;                               /* mode is input */
            tty_2nd = 0;
/*          sim_cancel (&tty_unit[TTO]);              *//* cancel output */
            }
        CLR_INT (INT_TTY);                              /* clear intr */
        break;

    case ioSKS:                                         /* SKS */
        if ((fnc & 012) != 0)                           /* fnc 0,1,4,5 */
            return IOBADFNC (dat);
        if (((fnc == 000) && tty_ready) ||              /* fnc 0? skip rdy */
            ((fnc == 001) && !tty_busy) ||              /* fnc 1? skip !busy */
            ((fnc == 004) && !TST_INTREQ (INT_TTY)) ||  /* fnc 4? skip !int */
            ((fnc == 005) && (tty_mode ||               /* fnc 5? skip !xoff */
                ((tty_buf & 0177) != XOFF))))           /* input & XOFF char */
            return IOSKIP (dat);
        break;

    case ioINA:                                         /* INA */
        if ((fnc & 005) != 0)                           /* only 0,2 */
            return IOBADFNC (dat);
        if (tty_ready) {                                /* ready? */
            tty_ready = 0;                              /* clear rdy */
            CLR_INT (INT_TTY);                          /* no interrupt */
            return IOSKIP (dat |
                (tty_buf & ((fnc & 002)? 077: 0377)));
            }
        break;

    case ioOTA:
        if ((fnc & 015) != 0)                           /* only 0,2 */
            return IOBADFNC (dat);
        if (tty_ready) {                                /* ready? */
            tty_buf = dat & 0377;                       /* store char */
            if (fnc & 002) {                            /* binary mode? */
                tty_buf = tty_buf | 0100;               /* set ch 7 */
                if (tty_buf & 040)
                    tty_buf = tty_buf & 0277;
                }
            tty_ready = 0;                              /* clear ready */
            tty_busy = 1;                               /* set busy */
            CLR_INT (INT_TTY);                          /* clr int */
            sim_activate (&tty_unit[TTO], tty_unit[TTO].wait);
            return IOSKIP (dat);
            }
        break;
        }                                               /* end case op */

return dat;
}

/* Input service - keyboard and reader */

t_stat tti_svc (UNIT *uptr)
{
int32 out, c;
UNIT *ruptr = &tty_unit[TTR];

sim_activate (uptr, uptr->wait);                        /* continue poll */
if (tty_2nd) {                                          /* char pending? */
    tty_buf = tty_2nd & 0377;
    tty_2nd = 0;
    tty_busy = 0;                                       /* clr busy */
    tty_ready = 1;                                      /* set ready */
    SET_INT (INT_TTY);                                  /* set int */
    return SCPE_OK;
    }
if ((c = sim_poll_kbd ()) >= SCPE_KFLAG) {              /* character? */
    out = c & 0177;                                     /* mask echo to 7b */
    if (c & SCPE_BREAK)                                 /* break? */
        c = 0;
    else c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags) | TTUF_KSR);
    uptr->pos = uptr->pos + 1;
    }
else if (c != SCPE_OK)                                  /* error? */
    return c;
else if ((ruptr->flags & UNIT_ATT) &&                   /* TTR attached */
    (ruptr->STA & RUNNING)) {                           /* and running? */
    if (ruptr->STA & LF_PEND) {                         /* lf pending? */
        c = 0212;                                       /* char is lf */
        ruptr->STA &= ~LF_PEND;                         /* clear flag */
        }
    else {                                              /* normal read */
        if ((c = getc (ruptr->fileref)) == EOF) {       /* read byte */
            if (feof (ruptr->fileref)) {                /* EOF? */
                ruptr->STA &= ~RUNNING;                 /* stop reader */
                if (ttr_stopioe)
                    printf ("TTR end of file\n");
                else return SCPE_OK;
                }
            else perror ("TTR I/O error");
            clearerr (ruptr->fileref);
            return SCPE_IOERR;
            }
        if ((ruptr->flags & UNIT_UASC) && (c == '\n')) {
            c = 0215;                                   /* Unix ASCII NL? */
            ruptr->STA |= LF_PEND;                      /* LF pending */
            }
        else if ((ruptr->flags & UNIT_ASC) && (c != 0))
            c = c | 0200;                               /* ASCII nz? cvt */
        ruptr->pos = ftell (ruptr->fileref);
        }
    if (ttr_xoff_read != 0) {                           /* reader stopping? */
        if (c == RUBOUT)                                /* rubout? stop */
            ttr_xoff_read = 0;
        else ttr_xoff_read--;                           /* else decr state */
        if (ttr_xoff_read == 0)                         /* delay done? */
            ruptr->STA &= ~RUNNING;                     /* stop reader */
        }
    else if ((c & 0177) == XOFF)                        /* XOFF read? */
        ttr_xoff_read = 2;
    out = c;                                            /* echo char */
    }
else return SCPE_OK;                                    /* no char */
tto_write (out);                                        /* echo to printer */
ttp_write (out);                                        /* and punch */
if (tty_mode == 0) {                                    /* input mode? */
    tty_2nd = (c & 0377) | 0400;                        /* flag 2nd state */
    tty_busy = 1;                                       /* set busy */
    CLR_INT (INT_TTY);                                  /* clear interrupt */
    sim_activate_abs (uptr, tty_busy_wait);             /* sched busy period */
    }
return SCPE_OK;
}

/* Output service - printer and punch */

t_stat tto_svc (UNIT *uptr)
{
uint32 c7b;
UNIT *ruptr = &tty_unit[TTR];
UNIT *puptr = &tty_unit[TTP];
t_stat r;

if ((tty_ready != 0) && (tty_busy != 0)) {              /* dummy cycle? */
    tty_busy = 0;                                       /* clr busy */
    SET_INT (INT_TTY);                                  /* set intr */
    return SCPE_OK;
    }
c7b = tty_buf & 0177;
if (ttp_tape_rcvd != 0) {                               /* prev = tape? */
    ttp_tape_rcvd--;                                    /* decrement state */
    if ((ttp_tape_rcvd == 0) && (puptr->flags & UNIT_ATT))
        puptr->STA |= RUNNING;                          /* start after delay */
    }
else if (c7b == TAPE)                                   /* char = TAPE? */
    ttp_tape_rcvd = 2;
if (ttp_xoff_rcvd != 0) {                               /* prev = XOFF? */
    ttp_xoff_rcvd--;                                    /* decrement state */
    if (ttp_xoff_rcvd == 0)                             /* stop after delay */
        puptr->STA &= ~RUNNING;
    }
else if (c7b == XOFF)                                   /* char = XOFF? */
    ttp_xoff_rcvd = 2;
if ((c7b == XON) && (ruptr->flags & UNIT_ATT)) {        /* char = XON? */
    ruptr->STA |= RUNNING;                              /* start reader */
    ttr_xoff_read = 0;                                  /* cancel stop */
    }
if ((r = tto_write (tty_buf)) != SCPE_OK) {             /* print; error? */
    sim_activate (uptr, uptr->wait);                    /* try again */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }
if ((r = ttp_write (tty_buf)) != SCPE_OK)               /* punch; error? */
    return r;
tty_busy = 0;                                           /* clr busy, set rdy */
tty_ready = 1;
SET_INT (INT_TTY);                                      /* set intr */
return SCPE_OK;
}

/* Output to printer */

t_stat tto_write (int32 c)
{
UNIT *tuptr = &tty_unit[TTO];

c = sim_tt_outcvt (c, TT_GET_MODE (tuptr->flags) | TTUF_KSR);
tuptr->pos = tuptr->pos + 1;
if (c >= 0)
    return sim_putchar_s (c);
else return SCPE_OK;
}

/* Output to punch */

t_stat ttp_write (int32 c)
{
uint32 p, c7b;
UNIT *puptr = &tty_unit[TTP];

if ((puptr->flags & UNIT_ATT) &&                        /* TTP attached */
    (puptr->STA & RUNNING)) {                           /* and running? */
    c7b = c & 0177;
    if (!(puptr->flags & UNIT_UASC) || (c7b != 015)) {
        if (puptr->flags & UNIT_ASC) {                  /* ASCII? */
            if (c7b == 012) p = '\n';                   /* cvt LF */
            else p = c7b;                               /* else 7b */
            }
        else p = c;                                     /* untouched */
        if (putc (p, puptr->fileref) == EOF) {          /* output byte */
            perror ("TTP I/O error");
            clearerr (puptr->fileref);
            return SCPE_IOERR;
            }
        puptr->pos = ftell (puptr->fileref);            /* update pos */
        }
    }
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
CLR_INT (INT_TTY);                                      /* clear ready, enb */
CLR_ENB (INT_TTY);
tty_mode = 0;                                           /* mode = input */
tty_buf = 0;
tty_2nd = 0;
tty_ready = 1;
tty_busy = 0;
ttr_xoff_read = 0;                                      /* clr TTR, TTP flags */
ttp_tape_rcvd = 0;
ttp_xoff_rcvd = 0;
tty_unit[TTR].STA = 0;
tty_unit[TTP].STA = 0;
sim_activate (&tty_unit[TTI], tty_unit[TTI].wait);      /* activate poll */
sim_cancel (&tty_unit[TTO]);                            /* cancel output */
return SCPE_OK;
}

/* Set keyboard/printer mode - make sure flags agree */

t_stat ttio_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATTABLE)                         /* not TTR, TTP */
    return SCPE_NOFNC;
tty_unit[TTO].flags = (tty_unit[TTO].flags & ~TT_MODE) | val;
if (val == TT_MODE_7P)
    val = TT_MODE_7B;
tty_unit[TTI].flags = (tty_unit[TTI].flags & ~TT_MODE) | val;
return SCPE_OK;
}

/* Set reader/punch mode */

t_stat ttrp_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (!(uptr->flags & UNIT_ATTABLE))                      /* PTR, PTP, TTR, TTP only */
    return SCPE_NOFNC;
if (!(val & UNIT_UASC))
    uptr->STA &= ~LF_PEND;
return SCPE_OK;
}

/* Set reader/punch start/stop */

t_stat ttrp_set_start_stop (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (!(uptr->flags & UNIT_ATTABLE))                      /* TTR, TTP only */
    return SCPE_NOFNC;
if (!(uptr->flags & UNIT_ATT))                          /* must be attached */
    return SCPE_UNATT;
if (val != 0)                                           /* start? set running */
    uptr->STA |= RUNNING;
else uptr->STA &= ~RUNNING;                             /* stop? clr running */
if ((uptr->flags & UNIT_ROABLE) != 0)                   /* TTR? cancel stop */
    ttr_xoff_read = 0;
else ttp_tape_rcvd = ttp_xoff_rcvd = 0;                 /* TTP? cancel all */
return SCPE_OK;
}

/* Clock/options: IO routine */

int32 clkio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        if ((fnc & 015) != 0)                           /* only fnc 0,2 */
            return IOBADFNC (dat);
        CLR_INT (INT_CLK);                              /* reset ready */
        if (fnc != 0)                                   /* fnc = 2? stop */
            sim_cancel (&clk_unit);
        else {                                          /* fnc = 0? */
            if (!sim_is_active (&clk_unit))
                sim_activate (&clk_unit,                /* activate */
            sim_rtc_init (clk_unit.wait));              /* init calibr */
            }
        break;

    case ioSKS:                                         /* SKS */
        if (fnc == 000) {                               /* clock skip !int */
            if (!TST_INTREQ (INT_CLK))
                return IOSKIP (dat);
            }
        else if ((fnc & 007) == 002) {                  /* mem parity? */
            if (((fnc == 002) && !TST_INT (INT_MPE)) ||
                ((fnc == 012) && TST_INT (INT_MPE)))
                return IOSKIP (dat);
            }
        else return IOBADFNC (dat);                     /* invalid fnc */
        break;

    case ioOTA:                                         /* OTA */
        if (fnc == 000)                                 /* SMK */
            dev_enb = dat;
        else if (fnc == 010) {                          /* OTK */
            C = (dat >> 15) & 1;                        /* set C */
            if (cpu_unit.flags & UNIT_HSA)              /* HSA included? */
                dp = (dat >> 14) & 1;                   /* set dp */
            if (cpu_unit.flags & UNIT_EXT) {            /* ext opt? */
                if (dat & 020000) {                     /* ext set? */
                    ext = 1;                            /* yes, set */
                    extoff_pending = 0;
                    }
                else extoff_pending = 1;                /* no, clr later */
                }
            sc = dat & 037;                             /* set sc */
            }
        else return IOBADFNC (dat);
        break;
        }

return dat;
}

/* Unit service */

t_stat clk_svc (UNIT *uptr)
{

M[M_CLK] = (M[M_CLK] + 1) & DMASK;                      /* increment mem ctr */
if (M[M_CLK] == 0)                                      /* = 0? set flag */
    SET_INT (INT_CLK);
sim_rtc_calb (clk_tps);                                 /* recalibrate */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
CLR_INT (INT_CLK);                                      /* clear ready, enb */
CLR_ENB (INT_CLK);
sim_cancel (&clk_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Set frequency */

t_stat clk_set_freq (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 50) && (val != 60))
    return SCPE_IERR;
clk_tps = val;
return SCPE_OK;
}

/* Show frequency */

t_stat clk_show_freq (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, (clk_tps == 50)? "50Hz": "60Hz");
return SCPE_OK;
}
