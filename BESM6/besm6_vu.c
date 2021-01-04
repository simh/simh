/*
 * besm6_vu.c: BESM-6 punchcard reader
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

t_stat vu_event (UNIT *u);    /* punched card reader */
UNIT vu_unit [] = {
    { UDATA (vu_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (vu_event, UNIT_SEQ+UNIT_ATTABLE, 0) },
};

/* Dispak seems to only care about the NOTREADY flag,
 * the proper behavior of FEED and MAYSTART may vary.
 */
#define VU1_NOTREADY (1<<23)
#define VU1_FEED     (1<<22)
#define VU1_MAYSTART (1<<21)
#define VU2_NOTREADY (1<<19)
#define VU2_FEED     (1<<18)
#define VU2_MAYSTART (1<<17)

#define SET_RDY2(x)      do READY2 |= x; while (0)
#define CLR_RDY2(x)      do READY2 &= ~(x); while (0)
#define ISSET_RDY2(x)    ((READY2 & (x)) != 0)
#define ISCLR_RDY2(x)    ((READY2 & (x)) == 0)

#define VU_RATE_CPM     600

/* Interrupts are every 2 columns */
#define CARD_LEN (80/2)
#define DFLT_DELAY (60*1000*MSEC/VU_RATE_CPM/CARD_LEN)

/*
 * The lines are first converted to GOST 10859; some GOST codes need to be known to the emulator.
 */
#define GOST_DOT 016            /* unpunched position */
#define GOST_O   056            /* punched position */

/* 6 "open quote" characters and an end-of-card indicator, can be entered as `````` */
#define DISP_END "\032\032\032\032\032\032\377"

unsigned int vu_col_dly = DFLT_DELAY;
unsigned int vu_end_dly = DFLT_DELAY/20; /* that seems to work */
unsigned int vu_card_dly = 10*DFLT_DELAY;
unsigned int vu_updkstart[2], vu_updkend[2];

unsigned int VU[2];

REG vu_reg[] = {
    { REGDATA ( Готов, READY2, 2,  8, 16, 1, NULL, NULL, 0, 0, 0) },
    { ORDATA  ( ВУ-0, VU[0], 24) },
    { ORDATA  ( ВУ-1, VU[1], 24) },
    { 0 }
};

t_stat vu_set_coldly (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (cptr && atoi(cptr) > 0) {
        vu_col_dly = atoi(cptr);
        return SCPE_OK;
    } else 
        sim_printf("Integer value required\n");
    return SCPE_ARG;
}

t_stat vu_set_enddly (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (cptr && atoi(cptr) > 0) {
        vu_end_dly = atoi(cptr);
        return SCPE_OK;
    } else 
        sim_printf("Integer value required\n");
    return SCPE_ARG;
}

t_stat vu_set_carddly (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (cptr && atoi(cptr) > 0) {
        vu_card_dly = atoi(cptr);
        return SCPE_OK;
    } else 
        sim_printf("Integer value required\n");
    return SCPE_ARG;
}

t_stat vu_show_coldly (FILE *st, UNIT *u, int32 v, CONST void *dp)
{
    fprintf(st, "Column delay is %d", vu_col_dly);
    return SCPE_OK;
}

t_stat vu_show_enddly (FILE *st, UNIT *u, int32 v, CONST void *dp)
{
    fprintf(st, "Delay before the end of card is %d", vu_end_dly);
    return SCPE_OK;
}

t_stat vu_show_carddly (FILE *st, UNIT *u, int32 v, CONST void *dp)
{
    fprintf(st, "Card delay is %d", vu_card_dly);
    return SCPE_OK;
}

t_stat vu_set_updk (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    unsigned start, end;
    int num = u - vu_unit;
    if (!cptr) {
        sim_printf("Range set to MAX\n");
        vu_updkstart[num] = 1;
        vu_updkend[num] = 0;
        return SCPE_OK;
    }
    if (sscanf(cptr, "%u-%u", &start, &end) != 2 ||
        (start == 0 && end != 0) || (end != 0 && end < start)) {
        sim_printf("Range required, e.g. 10-100, or 0-0 to disable.\n");
        return SCPE_ARG;
    }
    vu_updkstart[num] = start;
    vu_updkend[num] = end;
    return SCPE_OK;
}

t_stat vu_show_updk (FILE *st, UNIT *u, int32 v, CONST void *dp)
{
    int num = u - vu_unit;
    if (vu_updkstart[num] == 0 && vu_updkend[num] == 0)
        fprintf(st, "UPDK disabled");
    else if (vu_updkend[num] == 0)
        fprintf(st, "UPDK card %d to EOF", vu_updkstart[num]);
    else
        fprintf(st, "UPDK cards %d-%d", vu_updkstart[num], vu_updkend[num]);
    return SCPE_OK;
}

MTAB vu_mod[] = {
    { MTAB_XTD|MTAB_VDV,
        0, "COLDLY",  "COLDLY",  &vu_set_coldly,      &vu_show_coldly,     NULL,
      "Delay between pair-of-columns interrupts,\n"
      "and between the last column interrupt and posedge of the end-of-card signal." },
    { MTAB_XTD|MTAB_VDV,
        0, "ENDDLY",  "ENDDLY",  &vu_set_enddly,      &vu_show_enddly,     NULL,
      "Duration of the end-of-card signal." },
    { MTAB_XTD|MTAB_VDV,
        0, "CARDDLY",  "CARDDLY",  &vu_set_carddly,      &vu_show_carddly,     NULL,
      "Delay between the negedge of the end-of-card signal and the next card interrupt." },
    { MTAB_XTD|MTAB_VUN,
        0, "UPDK",  "UPDK",  &vu_set_updk,      &vu_show_updk,     NULL,
      "Range of cards to be converted to UPDK, e.g. SET UPDK 10-100. Use 0-0 to disable." },
    { 0 }
};


t_stat vu_reset (DEVICE *dptr);
t_stat vu_attach (UNIT *uptr, CONST char *cptr);
t_stat vu_detach (UNIT *uptr);

DEVICE vu_dev = {
    "VU", vu_unit, vu_reg, vu_mod,
    2, 8, 19, 1, 8, 50,
    NULL, NULL, &vu_reset, NULL, &vu_attach, &vu_detach,
    NULL, DEV_DISABLE | DEV_DEBUG
};


typedef enum {
    VU_IDLE,
    VU_STARTING,
    VU_COL,
    VU_COL_LAST = VU_COL + CARD_LEN - 1,
    VU_TAIL, VU_TAIL2
} VU_state;

VU_state vu_state[2], vu_next[2];

int vu_isfifo[2];

// Each card can hold up to 120 bytes; potentially valid GOST chars, expressible in UPDK, are 0-0177.
// True spaces are 017; bytes past the end of line (empty columns) are 0377. 

unsigned char vu_gost[2][120];
unsigned short vu_image[2][80];
unsigned int vu_cardcnt[2];

/*
 * Reset routine
 */
t_stat vu_reset (DEVICE *dptr)
{
    sim_cancel (&vu_unit[0]);
    sim_cancel (&vu_unit[1]);
    vu_state[0] = vu_state[1] = VU_IDLE;
    SET_RDY2(VU1_NOTREADY | VU2_NOTREADY);
    if (vu_unit[0].flags & UNIT_ATT) {
        CLR_RDY2(VU1_NOTREADY);
    }
    if (vu_unit[1].flags & UNIT_ATT) {
        CLR_RDY2(VU2_NOTREADY);
    }
    return SCPE_OK;
}

/*
 * Attaches a text file in UTF-8. By default the lines are converted to the linewise GOST/UPP
 * code as it allows each card to contain up to 120 characters. The columnwise GOST/UPDK code
 * is not supported yet.
 */
t_stat vu_attach (UNIT *u, CONST char *cptr)
{
    t_stat s;
    int num = u - vu_unit;

    s = attach_unit (u, cptr);
    if (s != SCPE_OK)
        return s;
    vu_isfifo[num] = (0 == sim_set_fifo_nonblock (u->fileref));
    CLR_RDY2(VU1_NOTREADY >> (num*4));
    vu_cardcnt[num] = 0;
    return SCPE_OK;
}

t_stat vu_detach (UNIT *u)
{
    int num = u - vu_unit;
    SET_RDY2(VU1_NOTREADY >>(num*4));
    return detach_unit (u);
}

/*
 * Controlling the card reader.
 */
void vu_control (int num, uint32 cmd)
{
    UNIT *u = &vu_unit[num];
    if (vu_dev.dctrl)
        besm6_debug("<<< VU-%d cmd %o", num, cmd);
    if (ISSET_RDY2(VU1_NOTREADY >> (num*4))) {
        if (vu_dev.dctrl)
            besm6_debug("<<< VU-%d not ready", num, cmd);
        return;
    }
    if (cmd & 010) {
        // Resetting the column buffer.
        if (vu_dev.dctrl)
            besm6_debug("<<< VU-%d buffer reset", num);
        VU[num] = 0;
        cmd &= ~010;
    }
    switch (cmd) {
    case 2:         /* stop */
        sim_cancel (u);
        vu_state[num] = VU_IDLE;
        SET_RDY2(VU1_MAYSTART >> (num*4));
        if (vu_dev.dctrl)
            besm6_debug("<<< VU-%d OFF", num);
        if (vu_state[num] == VU_TAIL) {
            if (! vu_isfifo[num]) {
                vu_detach(u);
                return;
            }
        }
        break;
    case 4:         /* read card */
    case 1:         /* read deck */
        vu_state[num] = VU_STARTING;
        CLR_RDY2(VU1_MAYSTART >> (num*4));
        vu_next[num] = cmd == 1 ? VU_STARTING : VU_IDLE;
        if (vu_dev.dctrl)
            besm6_debug("<<< VU-%d %s read.", num, cmd == 1 ? "DECK" : "CARD");
        sim_activate (u, vu_col_dly);
        break;
    case 0:
        break;
    default:
        besm6_debug ("<<< VU-%d unknown cmd %o", num, cmd);
    }
}

extern unsigned char unicode_to_gost(unsigned short);
extern unsigned short gost_to_unicode(unsigned char);

void uni2utf8(unsigned short ch, char buf[5]) {
    int i = 0;
    if (ch < 0x80) {
        buf[i++] = ch & 0x7F;
    } else if (ch < 0x800) {
        buf[i++] = (ch >> 6 | 0xc0);
        buf[i++] = ((ch & 0x3f) | 0x80);
    } else {
        buf[i++] = (ch >> 12 | 0xe0);
        buf[i++] = (((ch >> 6) & 0x3f) | 0x80);
        buf[i++] = ((ch & 0x3f) | 0x80);
    }
    buf[i] = '\0';
}

/*
 * Converts a string consisting of 0-9+- digits, plus, or minus to a 12-bit map of punches.
 */
static int punch(const char * s) {
    int r = 0;
    while (*s) {
        r |= *s >= '0' ? 4 << (*s - '0') : *s == '+' ? 1 : *s == '-' ? 2 : 0;
        ++s;
    }
    return r;
}

/* 
 * The UPDK code is a modified
 * [GOST 10859-CARD](https://ub.fnwi.uva.nl/computermuseum//DWcodes.html#A056)
 * for better distinctiveness wrt other column codes.
 * The UPDK codes are taken from Maznyj, "Programming in the Dubna system".
 */
static unsigned short gost_to_updk (unsigned char ch) {
    unsigned short ret;
    // Assuming that bits in the card are 9876543210-+
    // Bits from the upper and lower halves are XORed
    static char * upper[4] = { "", "+0", "-0", "+-" };
    static char * lower[2][16] = {
        { "0", "1", "2",   "3",   "4",   "5",   "6",   "7",
          "8", "9", "082", "083", "084", "085", "086", "087" },
        { "390", "391",   "392",   "39210", "394",   "395",   "396",   "397",
          "398", "39801", "39802", "39821", "39804", "39805", "39806", "39807" }
    };
    if (ch == 0377 /* filler */ || ch == 017 /* space */) {
        ret = 0;
    } else {
        ret = punch(upper[(ch>>4)&3]) ^ punch(lower[ch>=0100][ch&0xF]);
    }
    return ret;
}

/*
 * The UPP code is the GOST 10859 code with odd parity.
 * UPP stood for "unit for preparation of punchards".
 */
static unsigned char gost_to_upp (unsigned char ch) {
    unsigned char ret = ch;
    ch = (ch & 0x55) + ((ch >> 1) & 0x55);
    ch = (ch & 0x33) + ((ch >> 2) & 0x33);
    ch = (ch & 0x0F) + ((ch >> 4) & 0x0F);
    return (ch & 1) ? ret : ret | 0x80;
}

static void display_card(int num) {
    if (vu_updkstart[num] != 0 || vu_updkend[num] != 0) {
        char buf[80];
        int i, j;
        for (i = 0; i < 12; ++i) {
            for (j = 0; j < 80; ++j)
                buf[j] = (vu_image[num][j] >> i) & 1 ? 'O' : '.';
            besm6_debug("<<< VU-%d: %.80s", num, buf);
        }
        besm6_debug("<<< VU-%d: ###", num);
    }
}

static void reverse_card(int num, int raw) {
    char content[500];
    int i, j;
    memset(vu_image[num], 0, 160);
    content[0] = 0;
    for (i = 0; i < 120; ++i) {
        unsigned char ch = vu_gost[num][i];
        int mask = 1 << (i / 10);
        int pos = 8 * (i % 10);
        if (!raw) {
            if (ch == 0377)
                break;
            ch = gost_to_upp(ch);
        }
        for (j = 7; j >= 0; --j) {
            if (ch & 1)
                vu_image[num][pos+j] |= mask;
            ch >>= 1;
        }
    }
}

extern int utf8_getc(FILE*);

static int
is_prettycard (unsigned char *s)
{
        int i;
        for (i=0; i<80; ++i)
            if (s[i] != GOST_DOT && s[i] != GOST_O) {
                return 0;
            }
        for (i = 80; i < 120; ++i)
            if (s[i] != 0377)
                return 0;
        return 1;
}


static int chad (int num, int bit, char val)
{
    int index = bit / 8;

    switch (val) {
    case GOST_O:
        vu_gost[num][index] <<= 1;
        vu_gost[num][index] |= 1;
        return 0;
    case GOST_DOT:
        vu_gost[num][index] <<= 1;
        return 0;
    default:
        return -1;
    }
}

int
prettycard (UNIT *u)
{
    int bit, ch;
    int num = u - vu_unit;
        for (bit = 0; bit < 80; bit++) {
            /* The first line is good, no need to check */
            chad(num, bit, vu_gost[num][bit]);
        }
        for (bit = 80; bit < 12*80; bit++) {
            ch = utf8_getc(u->fileref);
            if (ch == '\n' && bit % 80 == 0)
                ch = utf8_getc(u->fileref);
            ch = unicode_to_gost(ch);
            if (chad(num, bit, ch))
                return -1;
            if (bit % 80 == 79) {
                do ch = utf8_getc(u->fileref); while (ch == '\r');
                if (ch != '\n')
                    return -1;
            }
        }
        /* there may be an empty line after a card */
        ch = getc(u->fileref);
        if (ch != '\n')
            ungetc(ch, u->fileref);
        return 0;
}


/*
 * Event: reading two characters (two columns) into the register, sending an interrupt.
 */
t_stat vu_event (UNIT *u)
{
    int num = u - vu_unit;
    if (vu_state[num] == VU_STARTING) {
        // Reading a line and forming the GOST array.
        int ch;
        do 
            ch = utf8_getc(u->fileref);
        while (ch == '\r');
        if (ch == EOF) {
            if (vu_dev.dctrl) {
                besm6_debug("<<< VU-%d: EOF, detaching", num);
            }
            vu_state[num] = VU_IDLE;
            vu_detach(u);
        } else {
            int endline = 0, i;
            ++vu_cardcnt[num];
            for (i = 0; i < 120; ++i) {
                if (endline) {
                    vu_gost[num][i] = 0377;
                } else {
                    int gost;
                    if (ch == EOF || ch == '\n') {
                        endline = 1;
                        gost = 0377;
                    } else {
                        gost = unicode_to_gost(ch);
                    }
                    vu_gost[num][i] = gost;
                    if (!endline && i != 119) 
                        do 
                            ch = utf8_getc(u->fileref);
                        while (ch == '\r');
                }
            }
            if (!endline) {
                int ch;
                do
                    ch = utf8_getc(u->fileref);
                while (ch == '\n' || ch == EOF);
                
            }
            if (0 == strncmp(vu_gost[num], DISP_END, 7)) {
                // The "dispatcher's end" card, end of card image mode.
                memset(vu_image[num], 0, 160);
                vu_image[num][0] = vu_image[num][40] = 0xFFF;
            } else if (is_prettycard(vu_gost[num])) {
                if (prettycard(u) < 0) {
                    sim_printf("VU-%d: A badly formatted card image at card %d, garbage will follow",
                               num, vu_cardcnt[num]);
                }
                reverse_card(num, 1); /* raw */
            } else if (vu_updkstart[num] != 0 && vu_cardcnt[num] >= vu_updkstart[num] &&
                       (vu_updkend[num] == 0 || vu_cardcnt[num] <= vu_updkend[num])) {
                int i;
                for (i = 0; i < 80; ++i)
                    vu_image[num][i] = gost_to_updk(vu_gost[num][i]);
            } else {
                reverse_card(num, 0); /* add parity */
            }
            
            if (vu_dev.dctrl) {
                display_card(num);
                besm6_debug("<<< VU-%d: card start", num);
            }

            GRP |= GRP_VU1_SYNC >> num;
            sim_activate(u, vu_col_dly);
            vu_state[num] = VU_COL;
            VU[num] = 0;
        }
    } else if (VU_COL <= vu_state[num] && vu_state[num] <= VU_COL_LAST) {
        int pos = (vu_state[num]++ - VU_COL) * 2;
        VU[num] = (vu_image[num][pos] << 12) | vu_image[num][pos+1];
        if (vu_dev.dctrl) {
            besm6_debug("<<< VU-%d: cols %d-%d: reg %06x", num, pos+1, pos+2, VU[num]);
        }
        GRP |= GRP_VU1_SYNC >> num;
        sim_activate (u, vu_col_dly);
    } else if (vu_state[num] == VU_TAIL) {
        PRP |= num == 0 ? PRP_VU1_END : PRP_VU2_END;
        vu_state[num] = VU_TAIL2;
        sim_activate(u, vu_end_dly);
        if (vu_dev.dctrl) {
            besm6_debug("<<< VU-%d: ------", num);
        }
    } else if (vu_state[num] == VU_TAIL2) {
        PRP &= ~(num == 0 ? PRP_VU1_END : PRP_VU2_END);
        SET_RDY2(VU1_FEED >> (num*4));
        if (vu_next[num] == VU_STARTING) {
            sim_activate (u, vu_card_dly);
        }
        vu_state[num] = vu_next[num];        
        if (vu_dev.dctrl) {
            besm6_debug("<<< VU-%d: ======", num);
        }
    } else {
        besm6_debug("<<< VU-%d: spurious event", num);        
    }

    return SCPE_OK;
}

int vu_read(int num) {
    if (vu_dev.dctrl)
        besm6_debug("<<< VU-%d: reg %06x", num, VU[num]);

    return VU[num];
}
