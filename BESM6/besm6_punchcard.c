/*
 * besm6_punchcard.c: BESM-6 punchcard devices
 *
 * Copyright (c) 2017, Leonid Broukhis
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

t_stat pi_event (UNIT *u);      /* punched card writer */
UNIT pi_unit [] = {
    { UDATA (pi_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (pi_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
};

/*
 * Each line has 3 phases:
 * - striking: the "PUNCH" interrupt line goes high,
 *   puncher solenoids can be activated
 * - moving: solenoids are turned off, the "PUNCH" interrupt line goes low
 * - checking: the "CHECK" interrupt goes high,
 *   querying of the check brushes can be done.
 */
typedef enum {
    PI_STRIKE,
    PI_MOVE,
    PI_CHECK,
    PI_LAST = PI_STRIKE + 3*12 - 1,
    PI_PAUSE,
    PI_IDLE
} pi_state_t;

typedef struct {
    /* 
     * A 3-card long tract, with 12 lines per card,
     * represented as 4 20-bit registers each.
     */
    uint32 image[3][12][4];
    int cur;                    /* FIFO position */
    int running;                /* continue with the next card */
    pi_state_t state;
    void (*punch_fn)(int, int);
} pi_t;
    
/*
 * There are 2 card punchers */
pi_t PI[2];

#define PI1_READY (1<<15)
#define PI2_READY (1<<13)

#define PI1_START (1<<14)
#define PI2_START (1<<12)

// #define NEGATIVE_RDY

#ifndef NEGATIVE_RDY
#   define ENB_RDY2     SET_RDY2
#   define DIS_RDY2     CLR_RDY2
#   define IS_RDY2      ISSET_RDY2
#else
#   define ENB_RDY2     CLR_RDY2
#   define DIS_RDY2     SET_RDY2
#   define IS_RDY2      ISCLR_RDY2
#endif

#define SET_RDY2(x)      do READY2 |= x; while (0)
#define CLR_RDY2(x)      do READY2 &= ~(x); while (0)
#define ISSET_RDY2(x)    ((READY2 & (x)) != 0)
#define ISCLR_RDY2(x)    ((READY2 & (x)) == 0)

/*
 * Per one line of a punched card.
 */
#define PI_RATE         (20*MSEC)

const uint32 pi_punch_mask[2] = { PRP_PCARD1_PUNCH, PRP_PCARD2_PUNCH };
const uint32 pi_check_mask[2] = { PRP_PCARD1_CHECK, PRP_PCARD2_CHECK };
const uint32 pi_ready_mask[2] = { PI1_READY, PI2_READY };
const uint32 pi_start_mask[2] = { PI1_START, PI2_START };

REG pi_reg[] = {
    { REGDATA ( "READY", READY2, 2, 4, 12, 1, NULL, NULL, 0, 0, 0) },
    { 0 }
};

MTAB pi_mod[] = {
    { 0 }
};

t_stat pi_reset (DEVICE *dptr);
t_stat pi_attach (UNIT *uptr, CONST char *cptr);
t_stat pi_detach (UNIT *uptr);

DEVICE pi_dev = {
    "PI", pi_unit, pi_reg, pi_mod,
    2, 8, 19, 1, 8, 50,
    NULL, NULL, &pi_reset, NULL, &pi_attach, &pi_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};


/*
 * Outputs 12 lines of 80 characters plus an empty line.
 */
static void pi_punch_dots(int unit, int card) {
    UNIT *u = &pi_unit[unit];
    FILE * f = u->fileref;
    int l, p, c;
    for (l = 0; l < 12; ++l) {
        for (p = 0; p < 4; ++p)
            for (c = 19; c >= 0; --c)
                putc((PI[unit].image[card][l][p] >> c) & 1 ? 'O' : '.', f);
        putc('\n', f);
    }
    putc('\n', f);
}

static void pi_to_bytes(int unit, int card, unsigned char buf[120]) {
    int byte = 0;
    int cnt = 0;
    int l, p, c;
    for (l = 0; l < 12; ++l) {
        for (p = 0; p < 4; ++p) {
            for (c = 19; c >= 0; --c) {
                int bit = (PI[unit].image[card][l][p] >> c) & 1 ? 1 : 0;
                buf[byte] <<= 1;
                buf[byte] |= bit;
                if (++cnt == 8) {
                    cnt = 0;
                    ++byte;
                }
            }
        }
    }
}

/*
 * Outputs 120 bytes, read linewise.
 */
static void pi_punch_binary(int unit, int card) {
    UNIT *u = &pi_unit[unit];
    FILE * f = u->fileref;
    static unsigned char buf[120];
    pi_to_bytes(unit, card, buf);
    fwrite(buf, 120, 1, f);
}

/*
 * Outputs a visual representation of the card
 * using 3 lines of 40 Braille patterns, plus an empty line.
 */
static void pi_punch_visual(int unit, int card) {
    UNIT *u = &pi_unit[unit];
    FILE * f = u->fileref;
    // Print 3 lines of 40 Braille characters per line representing a punchcard.
    unsigned char bytes[3][40];
    int line, col, p, c;
    memset(bytes, 0, 120);
    for (line = 0; line < 12; ++line) {
        for (p = 0; p < 4; ++p)
            for (c = 19; c >= 0; --c) {
                int bit = (PI[unit].image[card][line][p] >> c) & 1 ? 1 : 0;
                int col = p*20 + 19-c;
                if (bit) {
                    /*
                     * Braille Unicode codepoints are U+2800 plus
                     * an 8-bit mask of punches according to the map
                     * 0 3
                     * 1 4
                     * 2 5
                     * 6 7
                     */
                    bytes[line/4][col/2] |=
                        "\x01\x08\x02\x10\x04\x20\x40\x80"[line%4*2+col%2];
                }
            }
    }
    for (line = 0; line < 3; ++line) {
        for (col = 0; col < 40; ++col) {
            fprintf(f, "\342%c%c",
                    0240+(bytes[line][col] >> 6),
                    0200 + (bytes[line][col] & 077));
        }
        putc('\n', f);
    }
    putc('\n', f);
}

/*
 * Attempts to interpret a card as GOST-10859 with odd parity;
 * if fails, dumps visual.
 */
static void pi_punch_gost(int unit, int card) {
    UNIT *u = &pi_unit[unit];
    FILE * f = u->fileref;
    static unsigned char buf[120];
    int len;
    int cur;
    int zero_expected = 0;
    pi_to_bytes(unit, card, buf);
    /*
     * Bytes in the buffer must have odd parity, with the exception
     * of optional zero bytes at the end of lines and at the end of a card.
     * Trailing zeros are trimmed, intermediate zeros become blanks.
     * The first character in each line must be valid.
     */
    for (len = 120; len && !buf[len-1]; --len);
    for (cur = 0; cur < len; ++cur) {
        if (cur % 10 == 0) {
            /* A new line */
            zero_expected = 0;
        }
        if (zero_expected) {
            if (buf[cur])
                break;
        } else if (!buf[cur]) {
            if (cur % 10 == 0) {
                /* The first char in a line is zero */
                break;
            }
            zero_expected = 1;
        } else if (!odd_parity(buf[cur]) || (buf[cur] & 0177) >= 0140) {
            break;
        }
    }
    if (cur != len) {
        /* Bad parity or invalid codepoint detected */
        pi_punch_visual(unit, card);
    } else {
        for (cur = 0; cur < len; ++cur) {
            if (buf[cur]) {
                gost_putc(buf[cur] & 0177, f);
            } else {
                putc(' ', f);
            }
        }
        putc('\n', f);
    }
}

/*
 * Dumps the last card in the FIFO and advances the FIFO pointer
 * (this is equivalent to advancing the FIFO pointer and dumping
 * the "current" card).
 */
static void pi_output (int unit, int cull) {
    int card;

    PI[unit].cur = card = (PI[unit].cur + 1) % 3;

    if (cull) {
        besm6_debug("<<< PI-%d: Culling bad card", unit);
    } else {
        (*PI[unit].punch_fn)(unit, card);
    }
    pi_unit[unit].pos = ftell(pi_unit[unit].fileref);
    memset(PI[unit].image[card], 0, sizeof(PI[unit].image[card]));
}

/*
 * Reset routine
 */
t_stat pi_reset (DEVICE *dptr)
{
    sim_cancel (&pi_unit[0]);
    sim_cancel (&pi_unit[1]);
    PI[0].state = PI[1].state = PI_IDLE;
    DIS_RDY2(PI1_READY | PI2_READY);
    if (pi_unit[0].flags & UNIT_ATT)
        ENB_RDY2(PI1_READY|PI1_START);
    if (pi_unit[1].flags & UNIT_ATT)
        ENB_RDY2(PI2_READY|PI2_START);
    return SCPE_OK;
}

/*
 * Punching mode switches: 
 * -b - raw binary, line-wise, 120 bytes per p/c;
 * -v - a visual form using Unicode Braille patterns;
 * -d - a visual form using dots and Os;
 * -g or -u - attempts to interpret the card as GOST/UPP text.
 * The default is -v.
 */
t_stat pi_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int unit = u - pi_unit;
    PI[unit].punch_fn = NULL;
    while (sim_switches &
           (SWMASK('B')|SWMASK('V')|SWMASK('D')|SWMASK('G')|SWMASK('U'))) {
        if (PI[unit].punch_fn) {
            return SCPE_ARG;
        }
        if (sim_switches & SWMASK('B')) {
            PI[unit].punch_fn = pi_punch_binary;
            sim_switches &= ~SWMASK('B');
        } else if (sim_switches & SWMASK('V')) {
            PI[unit].punch_fn = pi_punch_visual;
            sim_switches &= ~SWMASK('V');
        } else if (sim_switches & SWMASK('D')) {
            PI[unit].punch_fn = pi_punch_dots;
            sim_switches &= ~SWMASK('D');
        } else if (sim_switches & SWMASK('G')) {
            PI[unit].punch_fn = pi_punch_gost;
            sim_switches &= ~SWMASK('G');
        } else if (sim_switches & SWMASK('U')) {
            PI[unit].punch_fn = pi_punch_gost;
            sim_switches &= ~SWMASK('U');
        }
    }
    if (PI[unit].punch_fn == NULL) {
        PI[unit].punch_fn = pi_punch_visual;
    }
    s = attach_unit (u, cptr);
    if (s != SCPE_OK)
        return s;
    ENB_RDY2(pi_ready_mask[unit]);
    return SCPE_OK;
}

t_stat pi_detach (UNIT *u)
{
    int unit = u - pi_unit;
    DIS_RDY2(pi_ready_mask[unit]);
    return detach_unit (u);
}

void pi_control (int num, uint32 cmd)
{
    UNIT *u = &pi_unit[num];
    if (pi_dev.dctrl)
        besm6_debug("<<<PI-%d cmd %o, state %d", num, cmd, PI[num].state);
    cmd &= 011;
    if (! IS_RDY2(pi_ready_mask[num])) {
        if (pi_dev.dctrl)
            besm6_debug("<<< PI-%d not ready", num, cmd);
        return;
    }
    switch (cmd) {
    case 000:         /* stop */
    case 010:         /* stop with culling (doesn't make much sense) */
        if (PI[num].state == PI_LAST) {
            pi_output(num, cmd & 010);
        }
        sim_cancel (u);
        PI[num].state = PI_IDLE;
        ENB_RDY2(pi_start_mask[num]);
        break;
    case 001:         /* start without culling */
    case 011:         /* start with culling */
        switch (PI[num].state) {
        case PI_IDLE:
            sim_activate (u, PI_RATE);
            break;
        case PI_PAUSE:
            /* Switching on during pause ignored */
            besm6_debug("<<< PI-%d switching on during pause ignored", num);
            break;
        case PI_LAST:
            PI[num].running = 1;
            /* This is the only state when the cull bit is honored */
            pi_output(num, cmd & 010);
            break;
        default:
            PI[num].running = 1;
            break;
        } break;
    }
}

t_stat pi_event (UNIT *u)
{
    int unit = u - pi_unit;
    if (++PI[unit].state > PI_IDLE) {
        /* Starting a new card */
        PI[unit].state = PI_STRIKE;
    }
    switch (PI[unit].state) {
    case PI_LAST:
        /*
         * At the last check interrupt,
         * the "permission to start" flag is cleared.
         */
        DIS_RDY2(pi_start_mask[unit]);
        break;
    case PI_PAUSE:
        /*
         * The permission to start is re-enabled.
         */
        ENB_RDY2(pi_start_mask[unit]);
        PI[unit].state = PI_IDLE;
        if (PI[unit].running) {
            if (pi_dev.dctrl)
                besm6_debug ("<<< PI-%d re-enabled", unit);
            sim_activate(u, PI_RATE);
            PI[unit].running = 0;
        } else {
            /*
             * The unit is going idle without an explicit "stop" command:
             * The last card (the separator) falls into the "good" bin.
             */
            pi_output(unit, 0);
        }
        break;
    default:
        break;
    }
    if (pi_dev.dctrl)
        besm6_debug ("<<< PI-%d event, state %d", unit, PI[unit].state);
    if (PI[unit].state <= PI_LAST) {
        switch (PI[unit].state % 3) {
        case PI_STRIKE:
            /* Punch interrupt */
            PRP |= pi_punch_mask[unit];
            sim_activate(u, PI_RATE/3);
            break;
        case PI_MOVE:
            /* Punchers off */
            PRP &= ~pi_punch_mask[unit];
            sim_activate(u, 2*PI_RATE/3);
            break;
        case PI_CHECK:
            /* Check interrupt */
            PRP |= pi_check_mask[unit];
            sim_activate(u, PI_RATE);
        }
    }
    return SCPE_OK;
}

/*
 * Writing to the register punches the current card.
 */
void pi_write (int num, uint32 val)
{
    int unit = num >> 2;
    int card = PI[unit].cur;
    int pos = (num & 3) ^ 3;
    int line = PI[unit].state / 3;
    if (line > 11 || PI[unit].state % 3 != PI_STRIKE) {
        besm6_debug("<<< PI-%d, writing out of turn, useless", num);
        return;
    }
    if (pi_dev.dctrl) { 
        besm6_debug("Card %d line %d pos %d <- val %05x",
                    card, line, pos, val);
    }
    PI[unit].image[card][line][pos] = val;
}

/*
 * Reading from the register reads the previous card
 * and returns the inverted value.
 */
int pi_read (int num)
{
    int unit = num >> 2;
    int pos = (num & 3) ^ 3;
    int line = PI[unit].state / 3;
    int card = (PI[unit].cur + 2) % 3;
    if (line > 11 || PI[unit].state % 3 != PI_CHECK) {
        /* Reading out of turn */
        return 0xFFFFF;
    } else {
        if (pi_dev.dctrl) {
            besm6_debug("Card %d line %d pos %d -> val %05x",
                        card, line, pos, PI[unit].image[card][line][pos]);
        }
        return PI[unit].image[card][line][pos] ^ 0xFFFFF;
    }
}

