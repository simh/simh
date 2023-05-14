/*
 * Copyright (c) 2023 Anders Magnusson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sim_defs.h"
#include "sim_tmxr.h"

#include "nd100_defs.h"

struct intr tti_int = { 0, 1 };
struct intr tto_int = { 0, 1 };

MTAB ttx_mod[];

int tti_status, tti_ctrl;
int tto_status, tto_ctrl;

t_stat tti_svc(UNIT *uptr);
t_stat tto_svc(UNIT *uptr);
t_stat tti_reset(DEVICE *dptr);
t_stat tto_reset(DEVICE *dptr);
t_stat tty_setpar(UNIT *uptr);

#define TT_ICTRL_EIRDY  0000001 /* enable int on dev ready */
#define TT_ICTRL_EIERR  0000002 /* enable int on error */
#define TT_ICTRL_ACT    0000004 /* set device active */

#define TT_ISTAT_IRDY   0000001 /* device ready gives interrupt */
#define TT_ISTAT_RDY    0000010 /* device ready for transfer */

#define TT_OCTRL_EIRDY  0000001 /* enable int on dev ready */
#define TT_OCTRL_EIERR  0000002 /* enable int on error */
#define TT_OCTRL_ACT    0000004 /* set device active */

#define TT_OSTAT_IRDY   0000001 /* device ready gives interrupt */
#define TT_OSTAT_EINT   0000002 /* error interrupt enabled */
#define TT_OSTAT_ACT    0000004 /* device active */
#define TT_OSTAT_RDY    0000010 /* device ready for transfer */

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
        { ORDATA(BUF, tti_unit.buf, 8) },
        { ORDATA(ISTATUS, tti_status, 16) },
        { ORDATA(ICTRL, tti_ctrl, 16) },
        { DRDATA (TIME, tti_unit.wait, 24), },
        { NULL }
};

DEVICE tti_dev = {
        "TTI", &tti_unit, tti_reg, ttx_mod,
        1, 10, 31, 1, 8, 8,
        NULL, NULL, &tti_reset,
        NULL, NULL, NULL,
        NULL, 0
};

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
        { ORDATA(OSTATUS, tto_status, 16) },
        { ORDATA(OCTRL, tto_ctrl, 16) },
        { DRDATA(POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
        { DRDATA(TIME, tto_unit.wait, 24), REG_NZ + PV_LEFT },
        { NULL }
};

MTAB ttx_mod[] = {
#if 0
        { TT_PAR, TT_PAR_EVEN,  "even parity", "EVEN",  &tty_setpar },
        { TT_PAR, TT_PAR_ODD,   "odd parity",  "ODD",   &tty_setpar },
        { TT_PAR, TT_PAR_MARK,  "mark parity", "MARK",  &tty_setpar },
        { TT_PAR, TT_PAR_SPACE, "no parity",   "NONE",  &tty_setpar },
#endif
        { 0 }
};

DEVICE tto_dev = {
        "TTO", &tto_unit, tto_reg, ttx_mod,
        1, 10, 31, 1, 8, 8,
        NULL, NULL, &tto_reset,
        NULL, NULL, NULL,
        NULL, 0
};

t_stat
tti_reset(DEVICE *dptr)
{
        sim_cancel(&tti_unit);
        return SCPE_OK;
}

t_stat
tto_reset(DEVICE *dptr)
{
        sim_cancel(&tto_unit);
        tto_status |= TT_OSTAT_RDY;
        return SCPE_OK;
}

t_stat
tti_svc(UNIT *uptr)
{
        int temp;

        sim_activate (&tti_unit, tti_unit.wait);
        if ((temp = sim_poll_kbd()) < SCPE_KFLAG)
                return temp;

        tti_unit.buf = temp & 0177;
        if (tti_ctrl & TT_ICTRL_ACT) {
                tti_status |= TT_ISTAT_RDY;
                if (tti_ctrl & TT_ICTRL_EIRDY)
                        extint(12, &tti_int);
        }
        
        return SCPE_OK;
}

t_stat
tto_svc(UNIT *uptr)
{
        int32   c;
        t_stat  r;

        c = tto_unit.buf & 0177;
        if ((r = sim_putchar_s (c)) != SCPE_OK) {       /* output; error? */
                sim_activate (uptr, uptr->wait);        /* try again */
                return ((r == SCPE_STALL)? SCPE_OK : r);/* !stall? report */
        }
        tto_status &= ~TT_OSTAT_ACT;
        tto_status |= TT_OSTAT_RDY;
        if (tto_ctrl & TT_OCTRL_EIRDY)
                extint(10, &tto_int);
        return SCPE_OK;
}


int
iox_tty(int addr)
{
        int rv = 0;

        /*
         * First four addresses are input, following four are output.
         */
        switch (addr & 07) {
        case 0: /* read data */
                regA = tti_unit.buf;
                tti_status &= ~TT_ISTAT_RDY;
                break;

        case 1: /* Ignored */
                break;

        case 2: /* read input status reg */
                regA = tti_status;
                break;

        case 3: /* Write control reg */
                if ((tti_ctrl & TT_ICTRL_ACT) == 0 && (regA & TT_ICTRL_ACT))
                        sim_activate (&tti_unit, tti_unit.wait);
                if ((regA & TT_ICTRL_ACT) == 0)
                        sim_cancel(&tti_unit);
                tti_ctrl = regA;
                if (tti_ctrl & TT_ICTRL_EIRDY)
                        tti_status |= TT_ISTAT_IRDY;
                else
                        tti_status &= ~TT_ISTAT_IRDY;
                break;

        case 4: /* Ignored */
                break;

        case 5: /* Write data */
                tto_unit.buf = regA & 0177;
                tto_status &= ~TT_OSTAT_RDY;
                tto_status |= TT_OSTAT_ACT;
                sim_activate (&tto_unit, tto_unit.wait);
                break;

        case 6: /* Read output status */
                regA = tto_status;
                break;

        case 7: /* Write output control reg */
                /* Only interrupts are controlled */
                tto_status = (tto_status & ~03) | (regA & 03);
                break;
        }

        return rv;
}

/*
 * Real-time clock.
 */
#define CLK_PER_SEC     50

int int_enabled, dev_ready;

struct intr rtc_int = { 0, 1 };

t_stat clk_reset(DEVICE *dptr);

t_stat clk_svc(UNIT *uptr);

UNIT clk_unit = { UDATA (&clk_svc, 0, 0) };

REG clk_reg[] = {
        { FLDATA (INTENB, int_enabled, 0) },
        { FLDATA (DEVRDY, dev_ready, 0) },
        { NULL }
};

MTAB clk_mod[] = {
        { 0 }
};

DEVICE clk_dev = {
        "RTC", &clk_unit, clk_reg, clk_mod,
        1, 0, 0, 0, 0, 0,
        NULL, NULL, &clk_reset,
        NULL, NULL, NULL,
        0, 0
};

int
iox_clk(int addr)
{
        int rv = 0;

        switch (addr & 3) {
        case 0: /* return 0 in A */
                regA = 0;
                break;
        case 1: /* Reset counter */
                sim_activate_after_abs(&clk_unit, 1000000/CLK_PER_SEC);
                break;
        case 2: /* read status */
                regA = (dev_ready << 3) | int_enabled;
                break;

        case 3: /* set status */
                sim_activate_after_abs(&clk_unit, 1000000/CLK_PER_SEC);
                int_enabled = regA & 1;
                if (BIT13(regA))
                        dev_ready = 0;
                break;

        default:
                rv = STOP_UNHIOX;
        }
        return rv;
}

t_stat
clk_reset (DEVICE *dptr)
{
        sim_rtc_init(1000000/CLK_PER_SEC);
        return SCPE_OK;
}

t_stat
clk_svc(UNIT *uptr)
{
        sim_rtc_calb(CLK_PER_SEC);
        sim_activate_after(&clk_unit, 1000000/CLK_PER_SEC);
        dev_ready = 1;
        if (int_enabled)
                extint(13, &rtc_int);
        return SCPE_OK;
}
