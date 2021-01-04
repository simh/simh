/*
 * besm6_punch.c: BESM-6 punchtape output
 *
 * Copyright (c) 2020, Leonid Broukhis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.

 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */
#include "besm6_defs.h"

t_stat pl_event (UNIT *u);

UNIT pl_unit [] = {
    { UDATA (pl_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (pl_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
};

#define PL1_READY 04000
#define PL2_READY 02000

#define SET_RDY2(x)      do READY2 |= x; while (0)
#define CLR_RDY2(x)      do READY2 &= ~(x); while (0)
#define ISSET_RDY2(x)    ((READY2 & (x)) != 0)
#define ISCLR_RDY2(x)    ((READY2 & (x)) == 0)

#define PL_RATE         (int)(12.5*MSEC)

unsigned char PL[2];

t_stat pl_reset (DEVICE *dptr);
t_stat pl_attach (UNIT *uptr, CONST char *cptr);
t_stat pl_detach (UNIT *uptr);

DEVICE pl_dev = {
    "PL", pl_unit, NULL, NULL,
    2, 8, 19, 1, 8, 50,
    NULL, NULL, &pl_reset, NULL, &pl_attach, &pl_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};

t_stat pl_reset (DEVICE *dptr)
{
    sim_cancel (&pl_unit[0]);
    sim_cancel (&pl_unit[1]);
    CLR_RDY2(PL1_READY | PL2_READY);
    if (pl_unit[0].flags & UNIT_ATT) {
        SET_RDY2(PL1_READY);
    }
    if (pl_unit[1].flags & UNIT_ATT) {
        SET_RDY2(PL2_READY);
    }
    if (pl_dev.dctrl)
        besm6_debug("reset READY2 := %08o", READY2);
    return SCPE_OK;
}

t_stat pl_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int num = u - pl_unit;
    s = attach_unit (u, cptr);
    if (s != SCPE_OK)
        return s;
    SET_RDY2(PL1_READY >> num);
    if (pl_dev.dctrl)
        besm6_debug("attach READY2 := %08o", READY2);
    return SCPE_OK;
}

t_stat pl_detach (UNIT *u)
{
    int num = u - pl_unit;
    CLR_RDY2(PL1_READY >> num);
    if (pl_dev.dctrl)
        besm6_debug("detach READY2 := %08o", READY2);
    return detach_unit (u);
}

void pl_control (int num, uint32 cmd)
{
    UNIT *u = &pl_unit[num];
    FILE *f = u->fileref;

    if (! ISSET_RDY2(PL1_READY >> num)) {
        if (pl_dev.dctrl)
            besm6_debug("<<< PL80-%d not ready", num);
        return;
    }
    putc(cmd & 0xff, f);
    PRP &= ~(PRP_PTAPE1_PUNCH >> num);
    CLR_RDY2(PL1_READY >> num);
    sim_activate_after(u, PL_RATE);
    if (pl_dev.dctrl) {
        besm6_debug("PL%d: punching %03o", num, cmd & 0xff);
        besm6_debug("punch READY2 := %08o", READY2);
    }
}

unsigned char unicode_to_gost (unsigned short val);

/*
 * The UPP code is the GOST 10859 code with odd parity.
 * UPP stood for "unit for preparation of punchards".
 */
static unsigned char unicode_to_upp (unsigned short ch) {
    unsigned char ret;
    ch = ret = unicode_to_gost (ch);
    ch = (ch & 0x55) + ((ch >> 1) & 0x55);
    ch = (ch & 0x33) + ((ch >> 2) & 0x33);
    ch = (ch & 0x0F) + ((ch >> 4) & 0x0F);
    return (ch & 1) ? ret : ret | 0x80;
}

t_stat pl_event (UNIT *u)
{
    int num = u - pl_unit;
    PRP |= PRP_PTAPE1_PUNCH >> num;
    SET_RDY2(PL1_READY >> num);
    if (pl_dev.dctrl) {
        besm6_debug("PL%d event, READY2 := %08o", num, READY2);
    }
    return SCPE_OK;
}
