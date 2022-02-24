/* ks10_cty.c: KS-10 front end (console terminal) simulator

   Copyright (c) 2021, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#if KS
#define UNIT_DUMMY      (1 << UNIT_V_UF)

#define STATUS          031
#define CTY_IN          032
#define CTY_OUT         033
#define KLINK_IN        034
#define KLINK_OUT       035
#define BOOT_ADDR       036
#define BOOT_DRIVE      037
#define MAG_FMT         040

#define KA_FAIL       0000000000001LL      /* Keep Alive failed to change */
#define FORCE_RELOAD  0000000000002LL      /* Force reload */
#define PWR_FAIL1     0000000000004LL      /* Power failure */
#define BOOT_SW       0000000000010LL      /* Boot switch */
#define KEEP_ALIVE    0000000177400LL      /* Keep alive */
#define TRAPS_ENB     0000040000000LL      /* Traps enabled */
#define ONE_MS        0000100000000LL      /* 1ms enabled */
#define CACHE_ENB     0000200000000LL      /* Cache enable */
#define DP_PAR_ENB    0000400000000LL      /* DP parity error enable */
#define CRAM_PAR_ENB  0001000000000LL      /* CRAM parity error enable */
#define PAR_ENB       0002000000000LL      /* Parity error detect enable */
#define KLINK_ENB     0004000000000LL      /* Klink active */
#define EX_KEEP_ALV   0010000000000LL      /* Examine Keep Alive */
#define RELOAD        0020000000000LL      /* Reload */

#define CTY_CHAR      0000000000400LL      /* Character pending */
#define KLINK_CHAR    0000000000400LL      /* Character pending */
#define KLINK_ACT     0000000001000LL      /* KLINK ACTIVE */
#define KLINK_HANG    0000000001400LL      /* KLINK HANGUP */

extern int32 tmxr_poll;
t_stat ctyi_svc (UNIT *uptr);
t_stat ctyo_svc (UNIT *uptr);
t_stat ctyrtc_srv(UNIT * uptr);
t_stat cty_reset (DEVICE *dptr);
t_stat cty_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *cty_description (DEVICE *dptr);
uint64 keep_alive = 0;
int    keep_num = 0;

extern DEVICE *rh_boot_dev;
extern int     rh_boot_unit;

static int32   rtc_tps = 1;

MTAB cty_mod[] = {
    { UNIT_DUMMY, 0, NULL, "STOP", &cty_stop_os },
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &tty_set_mode },
    { 0 }
    };

UNIT cty_unit[] = {
    { UDATA (&ctyo_svc, TT_MODE_7B, 0), 20000},
    { UDATA (&ctyi_svc, TT_MODE_7B|UNIT_DIS, 0), 4000 },
    { UDATA (&ctyrtc_srv, UNIT_IDLE|UNIT_DIS, 0), 1000 }
    };

REG  cty_reg[] = {
    {HRDATAD(WRU, sim_int_char, 8, "interrupt character") },
    { 0 },
    };


DEVICE cty_dev = {
    "CTY", cty_unit, cty_reg, cty_mod,
    3, 10, 31, 1, 8, 8,
    NULL, NULL, &cty_reset,
    NULL, NULL, NULL, NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cty_help, NULL, NULL, &cty_description
    };

void
cty_wakeup()
{
    sim_debug(DEBUG_EXP, &cty_dev, "CTY wakeup\n");
    sim_activate(&cty_unit[0], cty_unit[0].wait);
}


/* Check for input from CTY and put on queue. */
t_stat ctyi_svc (UNIT *uptr)
{
    uint64   buffer;
    int32    ch;

    sim_clock_coschedule (uptr, tmxr_poll * 3);

    if (Mem_read_word(CTY_IN, &buffer, 0))
        return SCPE_OK;
    sim_debug(DEBUG_DETAIL, &cty_dev, "CTY Read %012llo\n", buffer);
    if (buffer & CTY_CHAR) {
        cty_interrupt();
        return SCPE_OK;
    }
    ch = sim_poll_kbd ();
    if (ch & SCPE_KFLAG) {
        ch = 0177 & sim_tt_inpcvt(ch, TT_GET_MODE (cty_unit[0].flags));
        sim_debug(DEBUG_DETAIL, &cty_dev, "CTY char %o '%c'\n", ch,
                             ((ch > 040 && ch < 0177)? ch: '.'));
        buffer = (uint64)(ch) | CTY_CHAR;
        if (Mem_write_word(CTY_IN, &buffer, 0) == 0) {
            cty_interrupt();
        } else {
            sim_debug(DEBUG_DETAIL, &cty_dev, "CTY write failed %o '%c'\n", ch,
                             ((ch > 040 && ch < 0177)? ch: '.'));
        }
    }
    return SCPE_OK;
}

/* Handle output of characters to CTY. Started whenever there is output pending */
t_stat ctyo_svc (UNIT *uptr)
{
    uint64   buffer;
    /* Check if any input pending? */
    if (Mem_read_word(CTY_OUT, &buffer, 0))
        return SCPE_OK;
    sim_debug(DEBUG_DETAIL, &cty_dev, "CTY Write %012llo\n", buffer);
    if (buffer & CTY_CHAR) {
        int32    ch;
        ch = buffer & 0377;
        ch = sim_tt_outcvt ( ch, TT_GET_MODE (uptr->flags));
        if (sim_putchar_s(ch) != SCPE_OK) {
            sim_activate(uptr, 2000);
            return SCPE_OK;
        }
        sim_debug(DEBUG_DETAIL, &cty_dev, "CTY write %o '%c'\n", ch,
                             ((ch > 040 && ch < 0177)? ch: '.'));

        buffer = 0;
        if (Mem_write_word(CTY_OUT, &buffer, 0) == 0) {
            cty_interrupt();
        } else {
            sim_debug(DEBUG_DETAIL, &cty_dev, "CTY write failed %o '%c'\n", ch,
                             ((ch > 040 && ch < 0177)? ch: '.'));
        }
    }

    if (Mem_read_word(KLINK_OUT, &buffer, 0))
        return SCPE_OK;
    if (buffer != 0) {
        buffer = 0;
        if (Mem_write_word(CTY_OUT, &buffer, 0) == 0) {
            cty_interrupt();
        }
    }

    return SCPE_OK;
}


/* Handle FE timer interrupts. And keepalive counts */
t_stat
ctyrtc_srv(UNIT * uptr)
{
    uint64   buffer;

    sim_activate_after(uptr, 1000000/rtc_tps);
    if (Mem_read_word(STATUS, &buffer, 0))
        return SCPE_OK;
    if (buffer & ONE_MS) {
       fprintf(stderr, "1MS\n\r");
    }
    if (buffer & RELOAD && rh_boot_dev != NULL) {
        reset_all(1);   /* Reset everybody */
        if (rh_boot_dev->boot(rh_boot_unit, rh_boot_dev) != SCPE_OK)
            return SCPE_STOP;
    }
    /* Check if clock requested */
    if (buffer & EX_KEEP_ALV) {
        if (keep_alive != (buffer & KEEP_ALIVE)) {
            keep_alive = buffer;
            keep_num = 0;
        } else {
            if (++keep_num >= 15) {
               keep_num = 0;
               buffer &= ~0377LL;
               buffer |= 1;
               cty_execute(071);
               M[STATUS] = buffer;
               M[CTY_IN] = 0;
               M[CTY_OUT] = 0;
               M[KLINK_IN] = 0;
               M[KLINK_OUT] = 0;
            }
        }
    }
    return SCPE_OK;
}


t_stat cty_reset (DEVICE *dptr)
{
    sim_activate(&cty_unit[1], cty_unit[1].wait);
    sim_activate(&cty_unit[2], cty_unit[2].wait);
    M[STATUS] = 0;
    M[CTY_IN] = 0;
    M[CTY_OUT] = 0;
    M[KLINK_IN] = 0;
    M[KLINK_OUT] = 0;
    M[CTY_SWITCH] = 0;
    return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    cty_unit[0].flags = (cty_unit[0].flags & ~TT_MODE) | val;
    return SCPE_OK;
}

/* Stop operating system */

t_stat cty_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    M[CTY_SWITCH] = 1;                                 /* tell OS to stop */
    return SCPE_OK;
}


t_stat cty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "To stop the cpu use the command:\n\n");
fprintf (st, "    sim> SET CTY STOP\n\n");
fprintf (st, "This will write a 1 to location %03o, causing TOPS10 to stop\n\n", CTY_SWITCH);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprint_reg_help (st, &cty_dev);
return SCPE_OK;
}

const char *cty_description (DEVICE *dptr)
{
    return "Console TTY Line";
}

#endif
