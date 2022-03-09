/* ks10_lp.c: PDP-10 LP20 printer.

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
   in this Software without prior written authorization from Richard Cornwell.

*/

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_LP20
#define NUM_DEVS_LP20 0
#endif

#if (NUM_DEVS_LP20 > 0)

#define UNIT_V_CT    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_CT)
#define UNIT_CT      (1 << UNIT_V_CT)
#define UNIT_V_VFU   (UNIT_V_CT + 1)
#define UNIT_OPT     (1 << UNIT_V_VFU)


#define LINE     u6

/* LPCSRA   (765400) */
#define CS1_GO     0000001    /* Go command */
#define CS1_PAR    0000002    /* Enable Parity interrupt */
#define CS1_V_FNC    2        /* Function shift */
#define CS1_M_FNC    03       /* Function mask */
#define FNC_PRINT    0        /* Print */
#define FNC_TEST     1        /* Test */
#define FNC_DVU      2        /* Load DAVFU */
#define FNC_RAM      3        /* Load translation RAM */
#define CS1_UBA    0000060    /* Upper Unibus address */
#define CS1_IE     0000100    /* Interrupt enable */
#define CS1_DONE   0000200    /* Done flag */
#define CS1_INIT   0000400    /* Init */
#define CS1_ECLR   0001000    /* Clear errors */
#define CS1_DHOLD  0002000    /* Delimiter hold */
#define CS1_ONL    0004000    /* Online */
#define CS1_DVON   0010000    /* DAVFU online */
#define CS1_UND    0020000    /* Undefined Character */
#define CS1_PZERO  0040000    /* Page counter zero */
#define CS1_ERR    0100000    /* Errors */
#define CS1_MOD    (CS1_DHOLD|CS1_IE|(CS1_M_FNC << CS1_V_FNC)|CS1_PAR|CS1_GO)

/* LPCSRB   (765402) */
#define CS2_GOE    0000001    /* Go error */
#define CS2_DTE    0000002    /* DEM timing error */
#define CS2_MTE    0000004    /* MSYN error */
#define CS2_RPE    0000010    /* RAM parity error */
#define CS2_MPE    0000020    /* Memory parity error */
#define CS2_LPE    0000040    /* LPT parity error */
#define CS2_DVOF   0000100    /* DAVFU not ready */
#define CS2_OFFL   0000200    /* Offline */
#define CS2_TEST   0003400    /* Test mode */
#define CS2_OVFU   0004000    /* Optical VFU */
#define CS2_PBIT   0010000    /* data parity bit */
#define CS2_NRDY   0020000    /* Printer error */
#define CS2_LA180  0040000    /* LA180 printer */
#define CS2_VLD    0100000    /* Valid data */
#define CS2_ECLR   (CS2_GOE|CS2_DTE|CS2_MTE|CS2_RPE|CS2_LPE)
#define CS2_ERR    (CS2_ECLR|CS2_OFFL|CS2_DVOF)

/* LPBA    (765404) */
/* Unibus address */

/* LPBC    (765406) */
/* byte count */

/* LPPAGC  (765410) */
/* Page count */

/* LPRDAT  (765412) */
/* RAM Data register */

/* LPCOLC/LPCBUF (765414) */
/* Column counter / Character buffer */

/* LPCSUM/LPPDAT (765416) */
/* Checksum / Printer data */


#define EOFFLG   001      /* Tops 20 wants EOF */
#define HDSFLG   002      /* Tell Tops 20 The current device status */
#define ACKFLG   004      /* Post an acknowwledge message */
#define INTFLG   010      /* Send interrupt */
#define DELFLG   020      /* Previous character was delimiter */

#define MARGIN   6



int             lp20_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int             lp20_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
void            lp20_printline(UNIT *uptr, int nl);
void            lp20_output(UNIT *uptr, uint8 c);
void            lp20_update_chkirq (UNIT *uptr, int done, int irq);
void            lp20_update_ready(UNIT *uptr, uint16 setrdy, uint16 clrrdy);
t_stat          lp20_svc (UNIT *uptr);
t_stat          lp20_init (UNIT *uptr);
t_stat          lp20_reset (DEVICE *dptr);
t_stat          lp20_attach (UNIT *uptr, CONST char *cptr);
t_stat          lp20_detach (UNIT *uptr);
t_stat          lp20_setlpp(UNIT *, int32, CONST char *, void *);
t_stat          lp20_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat          lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
const char     *lp20_description (DEVICE *dptr);

char            lp20_buffer[134 * 3];
uint16          lp20_cs1;
uint16          lp20_cs2;
uint16          lp20_pagcnt;
uint32          lp20_ba;
uint16          lp20_wcnt;
uint8           lp20_col;
uint8           lp20_chksum;
uint8           lp20_buf;
uint8           lp20_data;
int             lp20_odd = 0;
int             lp20_duvfa_state = 0;
int             lp20_index = 0;

#define LP20_RAM_RAP  010000     /* RAM Parity */
#define LP20_RAM_INT  04000      /* Interrrupt bit */
#define LP20_RAM_DEL  02000      /* Delimiter bit */
#define LP20_RAM_TRN  01000      /* Translation bite */
#define LP20_RAM_PI   00400      /* Paper Instruction */
#define LP20_RAM_CHR  00377      /* Character translation */

uint16          lp20_vfu[256];
uint16          lp20_ram[256];
uint16          lp20_dvfu[] = {   /* Default VFU */
    /* 66 line page with 6 line margin */
    00377,    /* Line   0     8  7  6  5  4  3  2  1 */
    00220,    /* Line   1     8        5             */
    00224,    /* Line   2     8        5     3       */
    00230,    /* Line   3     8        5  4          */
    00224,    /* Line   4     8        5     3       */
    00220,    /* Line   5     8        5             */
    00234,    /* Line   6     8        5  4  3       */
    00220,    /* Line   7     8        5             */
    00224,    /* Line   8     8        5     3       */
    00230,    /* Line   9     8        5  4          */
    00264,    /* Line  10     8     6  5     3       */
    00220,    /* Line  11     8        5             */
    00234,    /* Line  12     8        5  4  3       */
    00220,    /* Line  13     8        5             */
    00224,    /* Line  14     8        5     3       */
    00230,    /* Line  15     8        5  4          */
    00224,    /* Line  16     8        5     3       */
    00220,    /* Line  17     8        5             */
    00234,    /* Line  18     8        5  4  3       */
    00220,    /* Line  19     8        5             */
    00364,    /* Line  20     8  7  6  5     3       */
    00230,    /* Line  21     8        5  4          */
    00224,    /* Line  22     8        5     3       */
    00220,    /* Line  23     8        5             */
    00234,    /* Line  24     8        5  4  3       */
    00220,    /* Line  25     8        5             */
    00224,    /* Line  26     8        5     3       */
    00230,    /* Line  27     8        5  4          */
    00224,    /* Line  28     8        5     3       */
    00220,    /* Line  29     8        5             */
    00276,    /* Line  30     8     6  5  4  3  2    */
    00220,    /* Line  31     8        5             */
    00224,    /* Line  32     8        5     3       */
    00230,    /* Line  33     8        5  4          */
    00224,    /* Line  34     8        5     3       */
    00220,    /* Line  35     8        5             */
    00234,    /* Line  36     8        5  4  3       */
    00220,    /* Line  37     8        5             */
    00224,    /* Line  38     8        5     3       */
    00230,    /* Line  39     8        5  4          */
    00364,    /* Line  40     8  7  6  5     3       */
    00220,    /* Line  41     8        5             */
    00234,    /* Line  42     8        5  4  3       */
    00220,    /* Line  43     8        5             */
    00224,    /* Line  44     8        5     3       */
    00230,    /* Line  45     8        5  4          */
    00224,    /* Line  46     8        5     3       */
    00220,    /* Line  47     8        5             */
    00234,    /* Line  48     8        5  4  3       */
    00220,    /* Line  49     8        5             */
    00264,    /* Line  50     8     6  5     3       */
    00230,    /* Line  51     8        5  4          */
    00224,    /* Line  52     8        5     3       */
    00220,    /* Line  53     8        5             */
    00234,    /* Line  54     8        5  4  3       */
    00220,    /* Line  55     8        5             */
    00224,    /* Line  56     8        5     3       */
    00230,    /* Line  57     8        5  4          */
    00224,    /* Line  58     8        5     3       */
    00220,    /* Line  59     8        5             */
    00020,    /* Line  60              5             */
    00020,    /* Line  61              5             */
    00020,    /* Line  62              5             */
    00020,    /* Line  63              5             */
    00020,    /* Line  64              5             */
    04020,    /* Line  65 12           5             */
   010000,    /* End of form */
};


/* LPT data structures

   lp20_dev      LPT device descriptor
   lp20_unit     LPT unit descriptor
   lp20_reg      LPT register list
*/

DIB lp20_dib = { 0775400, 017, 0754, 5, 3, &lp20_read, &lp20_write, NULL, 0, 0 };


UNIT lp20_unit = {
    UDATA (&lp20_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 66), 100
    };

REG lp20_reg[] = {
   {BRDATA(BUFFER, lp20_buffer, 16, 8, sizeof(lp20_buffer)), REG_HRO},
   {BRDATA(VFU, lp20_vfu, 16, 16, (sizeof(lp20_vfu)/sizeof(uint16))), REG_HRO},
   {BRDATA(RAM, lp20_ram, 16, 16, (sizeof(lp20_ram)/sizeof(uint16))), REG_HRO},
   {ORDATA(CS1, lp20_cs1, 16)},
   {ORDATA(CS2, lp20_cs2, 16)},
   {ORDATA(PAGCNT, lp20_pagcnt, 12)},
   {ORDATA(BA, lp20_ba, 18)},
   {ORDATA(BC, lp20_wcnt, 16)},
   {ORDATA(COL, lp20_col, 8)},
   {ORDATA(CHKSUM, lp20_chksum, 8)},
   {ORDATA(BUF, lp20_buf, 8)},
   { NULL }
};

MTAB lp20_mod[] = {
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of LP20" },
    {UNIT_CT, 0, "Lower case", "LC", NULL},
    {UNIT_CT, UNIT_UC, "Upper case", "UC", NULL},
    {UNIT_OPT, 0, "Normal VFU", "NORMAL", NULL},
    {UNIT_OPT, UNIT_OPT, "Optical VFU", "OPTICAL", NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lp20_setlpp, &lp20_getlpp, NULL, "Number of lines per page"},
    { 0 }
};

DEVICE lp20_dev = {
    "LP20", &lp20_unit, lp20_reg, lp20_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lp20_reset,
    NULL, &lp20_attach, &lp20_detach,
    &lp20_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lp20_help, NULL, NULL, &lp20_description
};

int
lp20_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    uint16            temp;

    addr &= dibp->uba_mask;
    sim_debug(DEBUG_DETAIL, dptr, "LP20 write %06o %06o %o\n",
             addr, data, access);

    switch (addr & 016) {
    case 000:  /* LPCSA */
            if (access == BYTE) {
               if (addr & 1)
                   data = data | (lp20_cs1 & 0377);
               else
                   data = (lp20_cs1 & 0177400) | data;
            }
            lp20_ba = ((data & CS1_UBA) << 9) | (lp20_ba & 0177777);
            if (data & CS1_INIT) {
               /* Reset controller */
               lp20_init (&lp20_unit);
            }
            if (data & CS1_ECLR) {
               /* Clear errors */
               lp20_cs2 &= ~(CS2_ECLR);
               lp20_cs1 &= ~(CS1_GO);
               lp20_cs1 |= CS1_DONE;
            }
            if (data & CS1_GO) {
                if ((lp20_cs1 & CS1_GO) == 0) {
                    lp20_chksum = 0;
                    lp20_odd = 0;
                    lp20_duvfa_state = 0;
                    lp20_index = 0;
                    sim_activate(&lp20_unit, 100);
                    lp20_cs1 |= CS1_GO;
                }
            } else {
                lp20_cs1 &= ~CS1_GO;
                sim_cancel(&lp20_unit);
            }
            lp20_cs1 &= ~(CS1_MOD);
            lp20_cs1 |= data & CS1_MOD;
            if (lp20_duvfa_state && ((lp20_cs1 >> CS1_V_FNC) & CS1_M_FNC) != FNC_DVU) {
                lp20_update_ready(&lp20_unit, 0, CS1_DVON);
                lp20_duvfa_state = 0;
            }
            break;

    case 002:  /* LPCSB */
            if (access == BYTE) {
               if (addr & 1) {
                   lp20_cs2 &= ~(CS2_TEST);
                   lp20_cs2 |= data & CS2_TEST;
               } else {
                   lp20_cs2 &= ~(CS2_GOE);
                   lp20_cs2 |= data & CS2_GOE;
               }
            } else {
               lp20_cs2 &= ~(CS2_TEST|CS2_GOE);
               lp20_cs2 |= data & (CS2_TEST|CS2_GOE);
            }
            break;

    case 004:  /* LPBA */
            lp20_ba = (lp20_ba & 060000) | (data & 0177777);
            break;

    case 006:  /* LPBC */
            lp20_wcnt = (data & 0177777);
            lp20_cs1 &= ~CS1_DONE;
            break;

    case 010: /* LPPAGC */
            if (access == BYTE) {
               if (addr & 1)
                   data = data | (lp20_pagcnt & 0377);
               else
                   data = (lp20_pagcnt & 0177400) | data;
            }
            lp20_pagcnt = (data & 0177777);
            lp20_cs1 &= ~CS1_PZERO;
            break;

    case 012: /* LPRDAT */
            break;

    case 014: /* LPCOL/LPCBUF */
            if (access == BYTE) {
               if (addr & 1)
                   lp20_col = (data >> 8) & 0377;
               else
                   lp20_buf = data & 0377;
            } else {
                lp20_buf = data & 0377;
                lp20_col = (data >> 8) & 0377;
            }
            break;

    case 016: /* LPCSUM/LPPDAT */
            if (access == BYTE) {
               temp = lp20_ram[(int)lp20_buf];
               if (addr & 1)
                   data = data | (temp & 0377);
               else
                   data = (temp & 0177400) | data;
            }
            lp20_ram[(int)lp20_buf] = data & 07777;
            break;
    }
    lp20_update_chkirq(&lp20_unit, 0, 0);
    return 0;
}

int
lp20_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    uint16            temp;
    int               par;

    addr &= dibp->uba_mask;
    switch (addr & 016) {
    case 000:  /* LPCSA */
            *data = lp20_cs1;
            *data &= ~CS1_UBA;
            *data |= (lp20_ba >> 9) & CS1_UBA;
            if ((lp20_cs2 & CS2_ERR) != 0)
               *data |= CS1_ERR;
            break;

    case 002:  /* LPCSB */
            *data = lp20_cs2;
            break;

    case 004:  /* LPBA */
            *data = lp20_ba & 0177777;
            break;

     case 006:  /* LPBC */
            *data = lp20_wcnt;
            break;

     case 010: /* LPPAGC */
            *data = lp20_pagcnt;
            break;

     case 012: /* LPRDAT */
            temp = lp20_ram[(int)lp20_buf] & 07777;
            par = (temp >> 8) ^ (temp >> 4) ^ temp;
            par = (par >> 2) ^ par;
            par ^= par >> 1;
            if ((par & 1) != 0)
               temp |= LP20_RAM_RAP;
            break;

     case 014: /* LPCOL/LPCBUF */
            *data = ((uint16)lp20_col) << 8;
            *data |= (uint16)lp20_buf;
            break;

     case 016: /* LPCSUM/LPPDAT */
            *data = ((uint16)lp20_chksum) << 8;
            *data |= (uint16)lp20_data;
            break;
     }
     sim_debug(DEBUG_DETAIL, dptr, "LP20 read %06o %06o %o\n",
             addr, *data, access);
     return 0;
}

void
lp20_printline(UNIT *uptr, int nl) {
    int     trim = 0;

    /* Trim off trailing blanks */
    while (lp20_col != 0 && lp20_buffer[lp20_col - 1] == ' ') {
         lp20_col--;
         trim = 1;
    }
    lp20_buffer[lp20_col] = '\0';
    sim_debug(DEBUG_DETAIL, &lp20_dev, "LP output %d %d [%s]\n", lp20_col, nl,
              lp20_buffer);
    /* Stick a carraige return and linefeed as needed */
    if (lp20_col != 0 || trim)
        lp20_buffer[lp20_col++] = '\r';
    if (nl != 0) {
        lp20_buffer[lp20_col++] = '\n';
        uptr->LINE++;
    }
    if (nl > 0 && lp20_vfu[uptr->LINE] == 010000) {
        lp20_buffer[lp20_col++] = '\f';
        uptr->LINE = 0;
        lp20_pagcnt = (lp20_pagcnt - 1) & 07777;
        if (lp20_pagcnt == 0)
           lp20_cs1 |= CS1_PZERO;
    } else if (nl < 0 && uptr->LINE >= (int32)uptr->capac) {
        uptr->LINE = 0;
        lp20_pagcnt = (lp20_pagcnt - 1) & 07777;
        if (lp20_pagcnt == 0)
           lp20_cs1 |= CS1_PZERO;
    }

    sim_fwrite(&lp20_buffer, 1, lp20_col, uptr->fileref);
    uptr->pos += lp20_col;
    lp20_col = 0;
    return;
}


/* Unit service */
void
lp20_output(UNIT *uptr, uint8 c) {

    if (c == 0)
       return;
    lp20_data = c & 0377;
    if (lp20_col == 132)
        lp20_printline(uptr, 1);
    /* Map lower to upper case if uppercase only */
    if ((uptr->flags & UNIT_UC) && (c & 0140) == 0140)
        c &= 0137;
    if (c >= 040 && c < 0177) { /* If printable */
        lp20_buffer[lp20_col++] = c;
        return;
    }
    if (c == 012) {  /* Line feed? */
       lp20_printline(uptr, 1);
    } else if (c == 014) { /* Form feed, advance to top of form */
       if (lp20_col != 0)
           lp20_printline(uptr, 1);
       sim_fwrite("\f", 1, 1, uptr->fileref);
       uptr->pos += 1;
       lp20_col = 0;
       uptr->LINE = 0;
    } else if (c == 015) { /* Carrage return */
       lp20_col = 0;
    }
    if (c == 011) { /* Tab */
        lp20_buffer[lp20_col++] = ' ';
        while ((lp20_col & 07) != 0)
            lp20_buffer[lp20_col++] = ' ';
    }
    return;
}

/*
 * Check if interrupt should be sent.
 *  Done set CS1_DONE.
 *  Irq set interrupt
 */
void
lp20_update_chkirq (UNIT *uptr, int done, int irq)
{
    DEVICE         *dptr = find_dev_from_unit (uptr);
    struct pdp_dib *dibp = (DIB *)dptr->ctxt;
    if (done)
       lp20_cs1 |= CS1_DONE;

    if (uptr->flags & UNIT_ATT) {
       lp20_cs1 |= CS1_ONL;
       lp20_cs2 &= ~(CS2_OFFL|CS2_NRDY);
    } else {
       lp20_cs1 &= ~(CS1_ONL|CS1_DONE);
       lp20_cs2 |= CS2_NRDY|CS2_OFFL;
    }
    if ((lp20_cs1 & CS1_IE) && (irq || (lp20_cs1 & CS1_DONE)))
        uba_set_irq(dibp, dibp->uba_vect);
    else
        uba_clr_irq(dibp, dibp->uba_vect);
}

/*
 * Update ready status of printer.
 */

void
lp20_update_ready(UNIT *uptr, uint16 setrdy, uint16 clrrdy)
{
    DEVICE         *dptr = find_dev_from_unit (uptr);
    struct pdp_dib *dibp = (DIB *)dptr->ctxt;
    uint16  new_cs1 = (lp20_cs1 | setrdy) & ~clrrdy;

    if ((new_cs1 ^ lp20_cs1) & (CS1_ONL|CS1_DVON) && !sim_is_active(uptr)) {
        if (new_cs1 & CS1_IE)
            uba_set_irq(dibp, dibp->uba_vect);
    }
    if (new_cs1 & CS1_DVON)
        lp20_cs2 &= ~CS2_DVOF;
    if (new_cs1 & CS1_ONL)
        lp20_cs2 &= ~CS2_OFFL;
    else
        lp20_cs2 |= CS2_OFFL;
    lp20_cs1 = new_cs1;
}

t_stat
lp20_svc (UNIT *uptr)
{
    DEVICE         *dptr = find_dev_from_unit (uptr);
    struct pdp_dib *dibp = (DIB *)dptr->ctxt;
    char            ch;
    int             fnc = (lp20_cs1 >> CS1_V_FNC) & CS1_M_FNC;
    uint16          ram_ch;
    uint8           data;

    if (fnc == FNC_PRINT && (uptr->flags & UNIT_ATT) == 0) {
        lp20_cs1 |= CS1_ERR;
        /* Set error */
        lp20_cs1 &= ~CS1_GO;
        lp20_update_chkirq (uptr, 0, 1);
        return SCPE_OK;
    }

    if (uba_read_npr_byte(lp20_ba, dibp->uba_ctl, &data) == 0) {
        lp20_cs2 |= CS2_MTE;
        lp20_cs1 &= ~CS1_GO;
        lp20_update_chkirq (uptr, 0, 1);
    sim_debug(DEBUG_DETAIL, &lp20_dev, "LP npr failed\n");
        return SCPE_OK;
    }

    lp20_buf = data;
    lp20_ba = (lp20_ba + 1) & 0777777;
    lp20_wcnt = (lp20_wcnt + 1) & 07777;
    if (lp20_wcnt == 0) {
        lp20_cs1 &= ~CS1_GO;
    }
    lp20_chksum += lp20_buf;
    sim_debug(DEBUG_DETAIL, &lp20_dev, "LP npr %08o %06o %03o %d\n", lp20_ba, lp20_wcnt,
              lp20_buf, fnc);
    switch(fnc) {
    case FNC_PRINT:
        ram_ch = lp20_ram[(int)lp20_buf];
        /* If previous was delimiter or translation do it */
        if (lp20_cs1 & CS1_DHOLD || (ram_ch &(LP20_RAM_DEL|LP20_RAM_TRN)) != 0) {
            ch = ram_ch & LP20_RAM_CHR;
            lp20_cs1 &= ~CS1_DHOLD;
            if (ram_ch & LP20_RAM_DEL)
               lp20_cs1 |= CS1_DHOLD;
        }
        /* Flag if interrupt set */
        if (ram_ch & LP20_RAM_INT) {
            lp20_cs1 &= ~CS1_GO;
            lp20_cs1 |= CS1_UND;
        }
        /* Check if translate flag set */
        if (ram_ch & LP20_RAM_TRN) {
            lp20_buf = (uint8)(ram_ch & 0377);
        }
        /* Check if paper motion */
        if (ram_ch & LP20_RAM_PI) {
            int   lines = 0;  /* Number of new lines to output */
            /* Print any buffered line */
            if (lp20_col != 0)
                lp20_printline(uptr, 1);
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP Page Index %02x %04x\n",
                                 lp20_buf, ram_ch);
            if ((ram_ch & 020) == 0) { /* Find channel mark in output */
               while ((lp20_vfu[uptr->LINE] & (1 << (ram_ch & 017))) == 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                 "LP skip chan %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 0;
                      lp20_pagcnt = (lp20_pagcnt - 1) & 07777;
                      if (lp20_pagcnt == 0)
                         lp20_cs1 |= CS1_PZERO;
                      break;
                   }
                   lines++;
                   uptr->LINE++;
               }
            } else {
               while ((ram_ch & 017) != 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                "LP skip line %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 0;
                      lp20_pagcnt = (lp20_pagcnt - 1) & 07777;
                      if (lp20_pagcnt == 0)
                         lp20_cs1 |= CS1_PZERO;
                   }
                   lines++;
                   uptr->LINE++;
                   ram_ch--;
               }
            }
            for(;lines > 0; lines--) {
               sim_fwrite("\r\n", 1, 2, uptr->fileref);
               uptr->pos+=2;
            }
        } else if (lp20_buf != 0) {
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP print %03o '%c' %04o\n",
                                  lp20_buf, lp20_buf, ram_ch);
            lp20_output(uptr, lp20_buf);
        }
        if (lp20_cs1 & CS1_GO)
            sim_activate(uptr, 600);
        else
            lp20_update_chkirq (uptr, lp20_wcnt == 0, 1);
        return SCPE_OK;

    case FNC_TEST:
        break;

    case FNC_DVU:
        if ((uptr->flags & UNIT_OPT) != 0) {
            lp20_output(uptr, lp20_buf);
            break;
        }
        if (lp20_buf >= 0354 && lp20_buf <= 0356) {  /* Start of DVU load */
            lp20_duvfa_state = 1;
            lp20_index = 0;
            lp20_odd = 0;
            lp20_cs2 &= ~CS2_DVOF;
        } else if (lp20_buf == 0357) {               /* Stop DVU load */
            lp20_duvfa_state = 0;
            lp20_vfu[lp20_index] = 010000;
            if (lp20_odd || lp20_index < 12) {
                lp20_cs1 &= ~CS1_DVON;
            } else {
                lp20_cs1 |= CS1_DVON;
            }
        } else if (lp20_duvfa_state) {
           if (lp20_odd) {
             lp20_vfu[lp20_index] = (lp20_vfu[lp20_index] & 077) | ((lp20_buf & 077) << 6);
             sim_debug(DEBUG_DETAIL, &lp20_dev,
                                 "LP load DFU %d %04x\n", lp20_index, lp20_vfu[lp20_index]);
             lp20_index++;
           } else {
             lp20_vfu[lp20_index] = (lp20_vfu[lp20_index] & 0007700) | (lp20_buf & 077);
           }
           lp20_odd = !lp20_odd;
        }
        break;

    case FNC_RAM:
        if (lp20_odd) {
          lp20_ram[lp20_index] = (lp20_ram[lp20_index] & 0377) | ((lp20_buf & 017) << 8);
          lp20_index++;
        } else {
          lp20_ram[lp20_index] = (lp20_ram[lp20_index] & 07400) | (lp20_buf & 0377);
        }
        lp20_odd = !lp20_odd;
        break;
    }
    if (lp20_cs1 & CS1_GO) {
        sim_activate(uptr, 10);
    } else {
        lp20_update_chkirq (uptr, 1, 1);
    }
    return SCPE_OK;
}

/* Init routine */
t_stat lp20_init (UNIT *uptr)
{
    lp20_cs1 = (lp20_cs1 & CS1_DVON) | CS1_DONE;
    lp20_cs2 = lp20_cs2 & (CS2_OFFL|CS2_DVOF);
    lp20_col = 0;
    lp20_ba = 0;
    lp20_wcnt = 0;
    lp20_chksum = 0;
    sim_cancel (uptr);                                 /* deactivate unit */
    lp20_update_chkirq (uptr, 1, 0);
    return SCPE_OK;
}


/* Reset routine */
t_stat lp20_reset (DEVICE *dptr)
{
    UNIT *uptr = &lp20_unit;
    int   i;
    int   par;
    lp20_col = 0;
    uptr->LINE = 0;
    lp20_cs1 = 0;
    lp20_cs2 = CS2_OFFL|CS2_DVOF;
    lp20_ba = 0;
    lp20_wcnt = 0;
    /* Clear RAM & VFU */
    for (i = 0; i < 256; i++) {
       lp20_ram[i] = 0;
       lp20_vfu[i] = 0;
    }

    if ((uptr->flags & UNIT_OPT) != 0) {
        /* Load default VFU into VFU */
        memcpy(&lp20_vfu, lp20_dvfu, sizeof(lp20_dvfu));
        lp20_cs2 |= CS2_OVFU;
        lp20_cs2 &= CS2_DVOF;
        lp20_cs1 |= CS1_DVON;
    }
    lp20_ram[012] = LP20_RAM_TRN|LP20_RAM_PI|7;   /* Line feed, print line, space one line */
    lp20_ram[013] = LP20_RAM_TRN|LP20_RAM_PI|6;   /* Vertical tab, Skip mod 20 */
    lp20_ram[014] = LP20_RAM_TRN|LP20_RAM_PI|0;   /* Form feed, skip to top of page */
    lp20_ram[015] = LP20_RAM_TRN|LP20_RAM_PI|020; /* Carrage return */
    lp20_ram[020] = LP20_RAM_TRN|LP20_RAM_PI|1;   /* Skip half page */
    lp20_ram[021] = LP20_RAM_TRN|LP20_RAM_PI|2;   /* Skip even lines */
    lp20_ram[022] = LP20_RAM_TRN|LP20_RAM_PI|3;   /* Skip triple lines */
    lp20_ram[023] = LP20_RAM_TRN|LP20_RAM_PI|4;   /* Skip one line */
    lp20_ram[024] = LP20_RAM_TRN|LP20_RAM_PI|5;
    /* Set parity of default RAM */
    for (i = 0; i < 256; i++) {
        lp20_ram[i] &= ~(LP20_RAM_RAP);
        par = (lp20_ram[i] >> 8) ^ (lp20_ram[i] >> 4) ^ (lp20_ram[i]);
        par = (par >> 2) ^ par;
        par ^= par >> 1;
        if ((par & 1) == 0)
           lp20_ram[i] |= LP20_RAM_RAP;
    }
    sim_cancel (uptr);                                 /* deactivate unit */
    if (uptr->flags & UNIT_ATT) {
       lp20_update_ready(uptr, CS1_ONL, 0);
       lp20_update_chkirq (uptr, 1, 0);
    }
    return SCPE_OK;
}

/* Attach routine */

t_stat lp20_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat  r;
    sim_switches |= SWMASK ('A');   /* Position to EOF */
    r = attach_unit (uptr, cptr);
    if (r == SCPE_OK) {
       lp20_update_ready(uptr, CS1_ONL, 0);
       lp20_update_chkirq (uptr, 1, 1);
    }
    return r;
}

/* Detach routine */

t_stat lp20_detach (UNIT *uptr)
{
    sim_cancel (uptr);                                 /* deactivate unit */
    lp20_update_ready(uptr, 0, CS1_ONL);
    lp20_update_chkirq (uptr, 1, 1);
    return detach_unit (uptr);
}

/*
 * Line printer routines
 */

t_stat
lp20_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value   i;
    t_stat    r;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = get_uint (cptr, 10, 100, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    uptr->capac = (t_addr)i;
    uptr->LINE = 0;
    return SCPE_OK;
}

t_stat
lp20_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file. \n");
fprintf (st, "The Line printer can be configured to any number of lines per page with the:\n");
fprintf (st, "        sim> SET %s LINESPERPAGE=n\n\n", dptr->name);
fprintf (st, "The default is 66 lines per page.\n\n");
fprintf (st, "The LP20 is a unibus device, various parameters can be changed on these devices\n");
fprintf (st, "\n The address of the device can be set with: \n");
fprintf (st, "      sim> SET LP20 ADDR=octal   default address= 775400\n");
fprintf (st, "\n The interrupt vector can be set with: \n");
fprintf (st, "      sim> SET LP20 VECT=octal   default 754\n");
fprintf (st, "\n The interrupt level can be set with: \n");
fprintf (st, "      sim> SET LP20 BR=#     # should be between 4 and 7.\n");
fprintf (st, "\n The unibus addaptor that the DZ is on can be set with:\n");
fprintf (st, "      sim> SET LP20 CTL=#    # can be either 1 or 3\n");

fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lp20_description (DEVICE *dptr)
{
    return "LP20 line printer" ;
}

#endif
