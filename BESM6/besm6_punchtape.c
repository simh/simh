/*
 * besm6_punch.c: BESM-6 punchcard/punchtape devices
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

t_stat fs_event (UNIT *u);
t_stat uvvk_event (UNIT *u);

UNIT fs_unit [] = {
    { UDATA (fs_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (fs_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
};

int curchar[2], feed[2], isfifo[2];
char line[2][128];

#define FS1_READY (1<<15)
#define FS2_READY (1<<14)

/* #define NEGATIVE_RDY */

#ifndef NEGATIVE_RDY
#   define ENB_RDY      SET_RDY
#   define DIS_RDY      CLR_RDY
#   define IS_RDY       ISSET_RDY
#else
#   define ENB_RDY      CLR_RDY
#   define DIS_RDY      SET_RDY
#   define IS_RDY       ISCLR_RDY
#endif

#define SET_RDY(x)      do READY |= x; while (0)
#define CLR_RDY(x)      do READY &= ~(x); while (0)
#define ISSET_RDY(x)    ((READY & (x)) != 0)
#define ISCLR_RDY(x)    ((READY & (x)) == 0)

#define FS_RATE         1000*MSEC/1500

unsigned char FS[2];

REG fs_reg[] = {
    { REGDATA ( "Готов", READY, 2,  2, 14, 1, NULL, NULL, 0, 0, 0) },
    { ORDATA  ( "ФС1500-1", FS[0], 8) },
    { ORDATA  ( "ФС1500-2", FS[2], 8) },
    { 0 }
};

MTAB fs_mod[] = {
    { 0 }
};

t_stat fs_reset (DEVICE *dptr);
t_stat fs_attach (UNIT *uptr, CONST char *cptr);
t_stat fs_detach (UNIT *uptr);

DEVICE fs_dev = {
    "FS", fs_unit, fs_reg, fs_mod,
    2, 8, 19, 1, 8, 50,
    NULL, NULL, &fs_reset, NULL, &fs_attach, &fs_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};

#define CARD_LEN 120
enum {
    FS_IDLE,
    FS_STARTING,
    FS_BINARY,
    FS_RUNNING,
    FS_IMAGE,
    FS_IMAGE_LAST = FS_IMAGE + CARD_LEN - 1,
    FS_TOOLONG,
    FS_FILLUP,
    FS_FILLUP_LAST = FS_FILLUP + CARD_LEN - 1,
    FS_ENDA3,
    FS_ENDA3_LAST = FS_ENDA3 + CARD_LEN - 1,
    FS_TAIL,
} fs_state[2];

int fs_textmode[2];

/*
 * Reset routine
 */
t_stat fs_reset (DEVICE *dptr)
{
    sim_cancel (&fs_unit[0]);
    sim_cancel (&fs_unit[1]);
    fs_state[0] = fs_state[1] = FS_IDLE;
    DIS_RDY(FS1_READY | FS2_READY);
    if (fs_unit[0].flags & UNIT_ATT)
        ENB_RDY(FS1_READY);
    if (fs_unit[1].flags & UNIT_ATT)
        ENB_RDY(FS2_READY);
    return SCPE_OK;
}

/*
 * Attaches a raw binary file by default,
 * with a -t switch attaches a prepared text file in UTF-8.
 */
t_stat fs_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int num = u - fs_unit;
    fs_textmode[num] = sim_switches & SWMASK('T');
    sim_switches &= ~SWMASK('T');
    s = attach_unit (u, cptr);
    if (s != SCPE_OK)
        return s;
    isfifo[num] = (0 == sim_set_fifo_nonblock (u->fileref));
    ENB_RDY(FS1_READY >> num);
    return SCPE_OK;
}

t_stat fs_detach (UNIT *u)
{
    int num = u - fs_unit;
    DIS_RDY(FS1_READY >> num);
    return detach_unit (u);
}

/*
 * Управление двигателем, лампой, протяжкой
 */
void fs_control (int num, uint32 cmd)
{
    UNIT *u = &fs_unit[num];

    static int bytecnt = 0;
    if (fs_dev.dctrl)
        besm6_debug("<<< ФС1500-%d команда %o", num, cmd);
    if (! IS_RDY(FS1_READY >> num)) {
        if (fs_dev.dctrl)
            besm6_debug("<<< ФС1500-%d не готово", num, cmd);
        return;
    }
    switch (cmd) {
    case 0:         /* полное выключение */
        sim_cancel (u);
        fs_state[num] = FS_IDLE;
        if (fs_dev.dctrl)
            besm6_debug("<<<ФС1500-%d ВЫКЛ..", num);
        bytecnt = 0;
        break;
    case 4:         /* двигатель без протяжки */
        fs_state[num] = FS_STARTING;
        if (fs_dev.dctrl)
            besm6_debug("<<<ФС1500-%d ВКЛ.", num);
        sim_cancel (u);
        break;
    case 5:         /* протяжка */
        if (fs_state[num] == FS_IDLE)
            besm6_debug("<<< ФС1500-%d протяжка без мотора", num);
        else if (fs_state[num] != FS_TAIL) {
            sim_activate (u, FS_RATE);
            bytecnt++;
        } else {
            if (! isfifo[num]) {
                fs_detach(u);
                fs_state[num] = FS_IDLE;
            }
        }
        break;
    default:
        besm6_debug ("<<< ФС1500-%d неизвестная команда %o", num, cmd);
    }
    if (cmd && fs_dev.dctrl) {
        besm6_debug("<<<ФС1500-%d: %d симв.", num, bytecnt);
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

static int utf8_getc (FILE *fin);

/*
 * Событие: читаем очередной символ с перфоленты в регистр.
 * Устанавливаем флаг прерывания.
 */
t_stat fs_event (UNIT *u)
{
    int num = u - fs_unit;
  again:
    if (fs_state[num] == FS_STARTING) {
        /* The first interrupt after starting the motor is dummy,
         * no need to read anything from the attached file.
         */
        FS[num] = 0;
        fs_state[num] = fs_textmode[num] ? FS_RUNNING : FS_BINARY;
    } else if (fs_state[num] == FS_BINARY) {
        int ch = getc (u->fileref);
        if (ch < 0) {
            FS[num] = 0;
            fs_state[num] = FS_TAIL;
        } else {
            FS[num] = ch;
        }
    } else if (fs_state[num] == FS_RUNNING) {
        int ch;
        /* Line separators are ignored in running text mode */
        do ch = utf8_getc (u->fileref); while (ch == '\n' || ch == '\r');
        if (ch < 0) {
            /* the tail end of the tape has no holes */
            FS[num] = 0;
            fs_state[num] = FS_TAIL;
        } else if (ch == (']' & 037)) {
            /* Switching from running text mode to "virtual punchcard" mode and back
             * is done with an ASCII GS (group separator) symbol ctrl-].
             */
            fs_state[num] = FS_IMAGE;
            goto again;
        } else {
            FS[num] = unicode_to_upp (ch);
        }
    } else if (FS_IMAGE <= fs_state[num] && fs_state[num] <= FS_IMAGE_LAST) {
        int ch = utf8_getc (u->fileref);
        if (ch < 0) {
            /* premature end of tape */
            FS[num] = 0;
            fs_state[num] = FS_TAIL;
        } else if (ch == '\r') {
            /* always ignored */
            goto again;
        } else if (ch == '\n') {
            /* Start returning zero bytes up to the end of the current "virtual punchard" */
            fs_state[num] = FS_FILLUP + (fs_state[num] - FS_IMAGE);
            goto again;
        } else if (ch == (']' & 037)) {
            if (fs_state[num] != FS_IMAGE)
                besm6_debug("<<< ENDA3 requested mid-card?");
            fs_state[num] = FS_ENDA3;
            goto again;
        } else {
            FS[num] = unicode_to_upp (ch);
            if (++fs_state[num] == FS_TOOLONG) {
                /* If a line is too long (> 120 chars), start the next "virtual punchcard" */
                fs_state[num] = FS_IMAGE;
            }
        }
    } else if (FS_FILLUP <= fs_state[num] && fs_state[num] <= FS_FILLUP_LAST) {
        FS[num] = 0;
        if (++fs_state[num] == FS_ENDA3) {
            fs_state[num] = FS_IMAGE;
        }
    } else if (FS_ENDA3 <= fs_state[num] && fs_state[num] <= FS_ENDA3_LAST) {
        if ((fs_state[num] - FS_ENDA3) % 5 == 0)
            FS[num] = 0200;
        else
            FS[num] = 0;
        if (++fs_state[num] == FS_TAIL) {
            fs_state[num] = FS_RUNNING;
        }
    } else if (fs_state[num] == FS_IDLE || fs_state[num] == FS_TAIL) {
        FS[num] = 0;
    }
    GRP |= GRP_FS1_SYNC >> num;
    return SCPE_OK;
}

int fs_read(int num) {
    if (fs_dev.dctrl)
        besm6_debug("<<< ФС1500-%d: байт %03o", num, FS[num]);

    return FS[num];
}

/*
 * Unlike the OS which uses GOST overline (approximated by ^) as a line separator
 * in running text mode, the BESM-ALGOL programming system used a nonprintable
 * character (0174) from the unused part of the codetable to allow compressing multiple
 * source lines on a punchcard. To specify that character,
 * we use ASCII RS (record separator) symbol ctrl-^.
 */
unsigned char
unicode_to_gost (unsigned short val)
{
    static const unsigned char tab0 [256] = {
        /* 00 - 07 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* 08 - 0f */   017,    017,    0214,   017,    017,    017,    017,    017,
        /* 10 - 17 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* 18 - 1f */   017,    017,    017,    017,    017,    017,    0174,   017,
        /*  !"#$%&' */  0017,   0133,   0134,   0034,   0127,   0126,   0121,   0033,
        /* ()*+,-./ */  0022,   0023,   0031,   0012,   0015,   0013,   0016,   0014,
        /* 01234567 */  0000,   0001,   0002,   0003,   0004,   0005,   0006,   0007,
        /* 89:;<=>? */  0010,   0011,   0037,   0026,   0035,   0025,   0036,   0136,
        /* @ABCDEFG */  0021,   0040,   0042,   0061,   0077,   0045,   0100,   0101,
        /* HIJKLMNO */  0055,   0102,   0103,   0052,   0104,   0054,   0105,   0056,
        /* PQRSTUVW */  0060,   0106,   0107,   0110,   0062,   0111,   0112,   0113,
        /* XYZ[\]^_ */  0065,   0063,   0114,   0027,   017,    0030,   0115,   0132,
        /* `abcdefg */  0032,   0040,   0042,   0061,   0077,   0045,   0100,   0101,
        /* hijklmno */  0055,   0102,   0103,   0052,   0104,   0054,   0105,   0056,
        /* pqrstuvw */  0060,   0106,   0107,   0110,   0062,   0111,   0112,   0113,
        /* xyz{|}~  */  0065,   0063,   0114,   017,    0130,   017,    0123,   017,
        /* 80 - 87 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* 88 - 8f */   017,    017,    017,    017,    017,    017,    017,    017,
        /* 90 - 97 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* 98 - 9f */   017,    017,    017,    017,    017,    017,    017,    017,
        /* a0 - a7 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* a8 - af */   017,    017,    017,    017,    0123,   017,    017,    017,
        /* b0 - b7 */   0136,   017,    017,    017,    017,    017,    017,    017,
        /* b8 - bf */   017,    017,    017,    017,    017,    017,    017,    017,
        /* c0 - c7 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* c8 - cf */   017,    017,    017,    017,    017,    017,    017,    017,
        /* d0 - d7 */   017,    017,    017,    017,    017,    017,    017,    0024,
        /* d8 - df */   017,    017,    017,    017,    017,    017,    017,    017,
        /* e0 - e7 */   017,    017,    017,    017,    017,    017,    017,    017,
        /* e8 - ef */   017,    017,    017,    017,    017,    017,    017,    017,
        /* f0 - f7 */   017,    017,    017,    017,    017,    017,    017,    0124,
        /* f8 - ff */   017,    017,    017,    017,    017,    017,    017,    017,
    };
    switch (val >> 8) {
    case 0x00:
        return tab0 [val];
    case 0x04:
        switch ((unsigned char) val) {
        case 0x10: return 0040;
        case 0x11: return 0041;
        case 0x12: return 0042;
        case 0x13: return 0043;
        case 0x14: return 0044;
        case 0x15: return 0045;
        case 0x16: return 0046;
        case 0x17: return 0047;
        case 0x18: return 0050;
        case 0x19: return 0051;
        case 0x1a: return 0052;
        case 0x1b: return 0053;
        case 0x1c: return 0054;
        case 0x1d: return 0055;
        case 0x1e: return 0056;
        case 0x1f: return 0057;
        case 0x20: return 0060;
        case 0x21: return 0061;
        case 0x22: return 0062;
        case 0x23: return 0063;
        case 0x24: return 0064;
        case 0x25: return 0065;
        case 0x26: return 0066;
        case 0x27: return 0067;
        case 0x28: return 0070;
        case 0x29: return 0071;
        case 0x2a: return 0135;
        case 0x2b: return 0072;
        case 0x2c: return 0073;
        case 0x2d: return 0074;
        case 0x2e: return 0075;
        case 0x2f: return 0076;
        case 0x30: return 0040;
        case 0x31: return 0041;
        case 0x32: return 0042;
        case 0x33: return 0043;
        case 0x34: return 0044;
        case 0x35: return 0045;
        case 0x36: return 0046;
        case 0x37: return 0047;
        case 0x38: return 0050;
        case 0x39: return 0051;
        case 0x3a: return 0052;
        case 0x3b: return 0053;
        case 0x3c: return 0054;
        case 0x3d: return 0055;
        case 0x3e: return 0056;
        case 0x3f: return 0057;
        case 0x40: return 0060;
        case 0x41: return 0061;
        case 0x42: return 0062;
        case 0x43: return 0063;
        case 0x44: return 0064;
        case 0x45: return 0065;
        case 0x46: return 0066;
        case 0x47: return 0067;
        case 0x48: return 0070;
        case 0x49: return 0071;
        case 0x4a: return 0135;
        case 0x4b: return 0072;
        case 0x4c: return 0073;
        case 0x4d: return 0074;
        case 0x4e: return 0075;
        case 0x4f: return 0076;
        }
        break;
    case 0x20:
        switch ((unsigned char) val) {
        case 0x15: return 0131;
        case 0x18: return 0032;
        case 0x19: return 0033;
        case 0x32: return 0137;
        case 0x3e: return 0115;
        }
        break;
    case 0x21:
        switch ((unsigned char) val) {
        case 0x2f: return 0020;
        case 0x91: return 0021;
        }
        break;
    case 0x22:
        switch ((unsigned char) val) {
        case 0x27: return 0121;
        case 0x28: return 0120;
        case 0x60: return 0034;
        case 0x61: return 0125;
        case 0x64: return 0116;
        case 0x65: return 0117;
        case 0x83: return 0122;
        }
        break;
    case 0x23:
        switch ((unsigned char) val) {
        case 0xe8: return 0020;
        }
        break;
    case 0x25:
        switch ((unsigned char) val) {
        case 0xc7: return 0127;
        case 0xca: return 0127;
        }
        break;
    }
    return 017;
}

/*
 * Read Unicode symbol from file.
 * Convert from UTF-8 encoding.
 */
static int
utf8_getc (FILE *fin)
{
    int c1, c2, c3;
  again:
    c1 = getc (fin);
    if (c1 < 0 || ! (c1 & 0x80))
        return c1;
    c2 = getc (fin);
    if (! (c1 & 0x20))
        return (c1 & 0x1f) << 6 | (c2 & 0x3f);
    c3 = getc (fin);
    if (c1 == 0xEF && c2 == 0xBB && c3 == 0xBF) {
        /* Skip zero width no-break space. */
        goto again;
    }
    return (c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 | (c3 & 0x3f);
}
