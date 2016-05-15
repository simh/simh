/*
 * besm6_printer.c: BESM-6 line printer device
 *
 * Copyright (c) 2009, Leonid Broukhis
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

t_stat printer_event (UNIT *u);
void offset_gost_write (int num, FILE *fout);

/*
 * Printer data structures
 *
 * printer_dev  PRINTER device descriptor
 * printer_unit PRINTER unit descriptor
 * printer_reg  PRINTER register list
 */
UNIT printer_unit [] = {
    { UDATA (printer_event, UNIT_ATTABLE+UNIT_SEQ, 0) },
    { UDATA (printer_event, UNIT_ATTABLE+UNIT_SEQ, 0) },
};


#define MAX_STRIKES 10
struct acpu_t {
    int curchar, feed, rampup;
    int strikes;
    int length;
    unsigned char line[128][MAX_STRIKES];
} acpu[2];

#define PRN1_NOT_READY  (1<<19)
#define PRN2_NOT_READY  (1<<18)

/* 1 = можно пользоваться молоточками, 0 - бумага в процессе протяжки */
#define PRN1_LINEFEED   (1<<23)
#define PRN2_LINEFEED   (1<<22)

#define SLOW_START      100*MSEC
#define FAST_START      1*MSEC
#define LINEFEED_SYNC   1       /* Чтобы быстрее печатало; в жизни 20-25 мс/1.4 мс ~= 17 */

REG printer_reg[] = {
    { REGDATA ( "Готов",  READY, 2,  2, 18, 1, NULL, NULL, 0, 0, 0) },
    { REGDATA ( "Прогон", READY, 2,  2, 22, 1, NULL, NULL, 0, 0, 0) },
    { 0 }
};

MTAB printer_mod[] = {
    { 0 }
};

t_stat printer_reset (DEVICE *dptr);
t_stat printer_attach (UNIT *uptr, CONST char *cptr);
t_stat printer_detach (UNIT *uptr);

DEVICE printer_dev = {
    "PRN", printer_unit, printer_reg, printer_mod,
    2, 8, 19, 1, 8, 50,
    NULL, NULL, &printer_reset, NULL, &printer_attach, &printer_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat printer_reset (DEVICE *dptr)
{
    memset(acpu, 0, sizeof (acpu));
    acpu[0].rampup = acpu[1].rampup = SLOW_START;
    sim_cancel (&printer_unit[0]);
    sim_cancel (&printer_unit[1]);
    READY |= PRN1_NOT_READY | PRN2_NOT_READY;
    if (printer_unit[0].flags & UNIT_ATT)
        READY &= ~PRN1_NOT_READY;
    if (printer_unit[1].flags & UNIT_ATT)
        READY &= ~PRN2_NOT_READY;
    return SCPE_OK;
}

t_stat printer_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int num = u - printer_unit;

    if (u->flags & UNIT_ATT) {
        /* Switching files cleanly */
        detach_unit (u);
    }
    s = attach_unit (u, cptr);
    if (s != SCPE_OK)
        return s;

    READY &= ~(PRN1_NOT_READY >> num);
    return SCPE_OK;
}

t_stat printer_detach (UNIT *u)
{
    int num = u - printer_unit;
    READY |= PRN1_NOT_READY >> num;
    return detach_unit (u);
}

/*
 * Управление двигателями, прогон
 */
void printer_control (int num, uint32 cmd)
{
    UNIT *u = &printer_unit[num];
    struct acpu_t * dev = acpu + num;

    if (printer_dev.dctrl)
        besm6_debug(">>> АЦПУ%d команда %o", num, cmd);
    if (READY & (PRN1_NOT_READY >> num)) {
        if (printer_dev.dctrl)
            besm6_debug(">>> АЦПУ%d не готово", num, cmd);
        return;
    }
    switch (cmd) {
    case 1:         /* linefeed */
        READY &= ~(PRN1_LINEFEED >> num);
        offset_gost_write (num, u->fileref);
        dev->feed = LINEFEED_SYNC;
        break;
    case 4:         /* start */
        /* стартуем из состояния прогона для надежности */
        dev->feed = LINEFEED_SYNC;
        READY &= ~(PRN1_LINEFEED >> num);
        if (dev->rampup)
            sim_activate (u, dev->rampup);
        dev->rampup = 0;
        break;
    case 10:        /* motor and ribbon off */
    case 8:         /* motor off? (undocumented) */
    case 2:         /* ribbon off */
        dev->rampup = cmd == 2 ? FAST_START : SLOW_START;
        sim_cancel (u);
        fflush (u->fileref);
        break;
    }
}

/*
 * Управление молоточками
 */
void printer_hammer (int num, int pos, uint32 mask)
{
    struct acpu_t * dev = acpu + num;
    while (mask) {
        if (mask & 1) {
            int strike = 0;
            while (dev->line[pos][strike] && strike < MAX_STRIKES)
                ++strike;
            if (strike < MAX_STRIKES) {
                dev->line[pos][strike] = dev->curchar;
                if (pos + 1 > dev->length)
                    dev->length = pos + 1;
                if (strike + 1 > dev->strikes)
                    dev->strikes = strike + 1;
            }
        }
        mask >>= 1;
        pos += 8;
    }
}

/*
 * Событие: вращение барабана АЦПУ.
 * Устанавливаем флаг прерывания.
 */
t_stat printer_event (UNIT *u)
{
    int num = u - printer_unit;
    struct acpu_t * dev = acpu + num;

    if (dev->curchar < 0140) {
        GRP |= GRP_PRN1_SYNC >> num;
        ++dev->curchar;
        /* For next char */
        sim_activate (u, 1400*USEC);
        if (dev->feed && --dev->feed == 0) {
            READY |= PRN1_LINEFEED >> num;
        }
    } else {
        /* For "zero" */
        dev->curchar = 0;
        GRP |= GRP_PRN1_ZERO >> num;
        if (printer_dev.dctrl)
            besm6_debug(">>> АЦПУ%d 'ноль'", num);
        /* For first sync after "zero" */
        sim_activate (u, 1000*USEC);
    }
    return SCPE_OK;
}

int gost_latin = 0; /* default cyrillics */

/*
 * GOST-10859 encoding.
 * Documentation: http://en.wikipedia.org/wiki/GOST_10859
 */
static const unsigned short gost_to_unicode_cyr [256] = {
    /* 000-007 */   0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
    /* 010-017 */   0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
    /* 020-027 */   0x65,   0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
    /* 030-037 */   0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
    /* 040-047 */   0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    /* 050-057 */   0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
    /* 060-067 */   0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    /* 070-077 */   0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
    /* 100-107 */   0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
    /* 110-117 */   0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2264, 0x2265,
    /* 120-127 */   0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
    /* 130-137 */   0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};

static const unsigned short gost_to_unicode_lat [256] = {
    /* 000-007 */   0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
    /* 010-017 */   0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
    /* 020-027 */   0x65,   0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
    /* 030-037 */   0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
    /* 040-047 */   0x41,   0x0411, 0x42,   0x0413, 0x0414, 0x45,   0x0416, 0x0417,
    /* 050-057 */   0x0418, 0x0419, 0x4b,   0x041b, 0x4d,   0x48,   0x4f,   0x041f,
    /* 060-067 */   0x50,   0x43,   0x54,   0x59,   0x0424, 0x58,   0x0426, 0x0427,
    /* 070-077 */   0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
    /* 100-107 */   0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
    /* 110-117 */   0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2264, 0x2265,
    /* 120-127 */   0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
    /* 130-137 */   0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};
/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
static void
utf8_putc (unsigned short ch, FILE *fout)
{
    if (ch < 0x80) {
        putc (ch, fout);
        return;
    }
    if (ch < 0x800) {
        putc (ch >> 6 | 0xc0, fout);
        putc ((ch & 0x3f) | 0x80, fout);
        return;
    }
    putc (ch >> 12 | 0xe0, fout);
    putc (((ch >> 6) & 0x3f) | 0x80, fout);
    putc ((ch & 0x3f) | 0x80, fout);
}

unsigned short
gost_to_unicode (unsigned char ch)
{
    return gost_latin ? gost_to_unicode_lat [ch] :
        gost_to_unicode_cyr [ch];
}

/*
 * Write GOST-10859 symbol to file.
 * Convert to local encoding (UTF-8, KOI8-R, CP-1251, CP-866).
 */
void
gost_putc (unsigned char ch, FILE *fout)
{
    unsigned short u;

    u = gost_to_unicode (ch);
    if (! u)
        u = ' ';
    utf8_putc (u, fout);
}

/*
 * Write GOST-10859 string with overprint to file in UTF-8.
 */
void
offset_gost_write (int num, FILE *fout)
{
    struct acpu_t * dev = acpu + num;
    int s, p;
    for (s = 0; s < dev->strikes; ++s) {
        if (s)
            fputc ('\r', fout);
        for (p = 0; p < dev->length; ++p) {
            gost_putc (dev->line[p][s] - 1, fout);
        }
    }

    fputc ('\n', fout);
    memset(dev->line, 0, sizeof (dev->line));
    dev->length = dev->strikes = 0;
}
