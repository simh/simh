/* ka10_lp.c: PDP-10 line printer simulator

   Copyright (c) 2011-2017, Richard Cornwell

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
#include <ctype.h>

#ifndef NUM_DEVS_LP
#define NUM_DEVS_LP 0
#endif

#if (NUM_DEVS_LP > 0)

#define LP_DEVNUM 0124
#define STATUS   u3
#define COL      u4
#define POS      u5
#define LINE     u6

#define UNIT_V_CT    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_CT)
#define UNIT_UTF8    (2 << UNIT_V_CT)
#define UNIT_CT      (3 << UNIT_V_CT)

#define PI_DONE  000007
#define PI_ERROR 000070
#define DONE_FLG 000100
#define BUSY_FLG 000200
#define ERR_FLG  000400
#define CLR_LPT  002000
#define C96      002000
#define C128     004000
#define DEL_FLG  0100000



t_stat          lpt_devio(uint32 dev, uint64 *data);
t_stat          lpt_svc (UNIT *uptr);
t_stat          lpt_reset (DEVICE *dptr);
t_stat          lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat          lpt_detach (UNIT *uptr);
t_stat          lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
const char     *lpt_description (DEVICE *dptr);

char            lpt_buffer[134 * 3];
uint8           lpt_chbuf[5];             /* Read in Character buffers */

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { LP_DEVNUM, 1, &lpt_devio, NULL };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), 100
    };

REG lpt_reg[] = {
    { DRDATA (STATUS, lpt_unit.STATUS, 18), PV_LEFT | REG_UNIT },
    { DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT | REG_UNIT },
    { BRDATA(BUFF, lpt_buffer, 16, 8, sizeof(lpt_buffer)), REG_HRO},
    { BRDATA(CBUFF, lpt_chbuf, 16, 8, sizeof(lpt_chbuf)), REG_HRO},
    { NULL }
};

MTAB lpt_mod[] = {
    {UNIT_CT, 0, "Lower case", "LC", NULL},
    {UNIT_CT, UNIT_UC, "Upper case", "UC", NULL},
    {UNIT_CT, UNIT_UTF8, "UTF8 ouput", "UTF8", NULL},
    { 0 }
};

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    &lpt_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpt_help, NULL, NULL, &lpt_description
};

/* IOT routine */

t_stat lpt_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &lpt_unit;
    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS & (PI_DONE|PI_ERROR|DONE_FLG|BUSY_FLG|ERR_FLG);
         if ((uptr->flags & UNIT_UC) == 0)
             *data |= C96;
         if ((uptr->flags & UNIT_UTF8) == 0)
             *data |= C128;
         if ((uptr->flags & UNIT_ATT) == 0)
             *data |= ERR_FLG;
         sim_debug(DEBUG_CONI, &lpt_dev, "LP CONI %012llo PC=%06o\n", *data, PC);
         break;

    case CONO:
         clr_interrupt(dev);
         sim_debug(DEBUG_CONO, &lpt_dev, "LP CONO %012llo PC=%06o\n", *data, PC);
         uptr->STATUS &= ~0777;
         uptr->STATUS |= ((PI_DONE|PI_ERROR|DONE_FLG|BUSY_FLG|CLR_LPT) & *data);
         if (*data & CLR_LPT) {
             uptr->STATUS &= ~DONE_FLG;
             uptr->STATUS |= BUSY_FLG;
             sim_activate (&lpt_unit, lpt_unit.wait);
         }
         if ((uptr->flags & UNIT_ATT) == 0) {
             uptr->STATUS |= ERR_FLG;
             set_interrupt(dev, (uptr->STATUS >> 3));
         }
         if ((uptr->STATUS & DONE_FLG) != 0)
             set_interrupt(dev, uptr->STATUS);
         break;

    case DATAO:
         if ((uptr->STATUS & DONE_FLG) != 0) {
             int i, j;
             for (j = 0, i = 29; i > 0; i-=7)
                 lpt_chbuf[j++] = ((uint8)(*data >> i)) & 0x7f;
             uptr->STATUS &= ~DONE_FLG;
             uptr->STATUS |= BUSY_FLG;
             clr_interrupt(dev);
             sim_activate (&lpt_unit, lpt_unit.wait);
             sim_debug(DEBUG_DATAIO, &lpt_dev, "LP DATO %012llo PC=%06o\n", *data, PC);
         }
         break;
    case DATAI:
         *data = 0;
         break;
    }
    return SCPE_OK;
}


void
lpt_printline(UNIT *uptr, int nl) {
    int   trim = 0;
    /* Trim off trailing blanks */
    while (uptr->COL >= 0 && lpt_buffer[uptr->POS - 1] == ' ') {
         uptr->COL--;
         uptr->POS--;
         trim = 1;
    }
    lpt_buffer[uptr->POS] = '\0';
    sim_debug(DEBUG_DETAIL, &lpt_dev, "LP output %d %d [%s]\n", uptr->COL, nl, lpt_buffer);
    /* Stick a carraige return and linefeed as needed */
    if (uptr->COL != 0 || trim)
        lpt_buffer[uptr->POS++] = '\r';
    if (nl) {
        lpt_buffer[uptr->POS++] = '\n';
        uptr->LINE++;
    }
    sim_fwrite(&lpt_buffer, 1, uptr->POS, uptr->fileref);
    uptr->COL = 0;
    uptr->POS = 0;
    if (ferror (uptr->fileref)) {                           /* error? */
        perror ("LPT I/O error");
        clearerr (uptr->fileref);
        uptr->STATUS |= ERR_FLG;
        set_interrupt(LP_DEVNUM, (uptr->STATUS >> 3));
        return;
    }
    return;
}

uint16 utf_code[32] = {
      0x0000,           /* Dot */
      0x2193,           /* Down arrow */
      0x237a,           /* APL Alpha */
      0x03b2,           /* Beta */
      0x039b,           /* Lambda */
      0x2510,           /* Box light down and left */
      0x03b5,           /* Epsilon */
      0x03d6,           /* Pi */
      0x03bb,           /* Lambda */
      0x221d,           /* proportional */
      0x222b,           /* Integral */
      0x00b1,           /* Plus minus */
      0x2295,           /* Circle plus */
      0x221e,           /* Infinity */
      0x2202,           /* Partial derivitive */
      0x2282,           /* Subset of */
      0x2283,           /* Superset of */
      0x2229,           /* Intersection */
      0x222a,           /* union */
      0x2200,           /* For all */
      0x2203,           /* Exists */
      0x2295,           /* Circle plus */
      0x2194,           /* Left right arrow */
      0x2227,           /* Logical and */
      0x2192,           /* Rightwards arror */
      0x2014,           /* Em dash */
      0x2260,           /* Not equal */
      0x2264,           /* Less than or equal */
      0x2265,           /* Greater than or equal */
      0x2261,           /* Identical too */
      0x2228            /* Logical or */
 };

/* Unit service */
void
lpt_output(UNIT *uptr, char c) {

    if (c == 0)
       return;
    if ((uptr->flags & UNIT_UC) && (c & 0140) == 0140)
        c &= 0137;
    if ((uptr->flags & UNIT_UTF8) && c < 040) {
        uint16 u = utf_code[c & 0x1f];
        if (u > 0x7ff) {
            lpt_buffer[uptr->POS++] = 0xe0 + ((u >> 12) & 0xf);
            lpt_buffer[uptr->POS++] = 0x80 + ((u >> 6) & 0x3f);
            lpt_buffer[uptr->POS++] = 0x80 + (u & 0x3f);
        } else if (u > 0x7f) {
            lpt_buffer[uptr->POS++] = 0xc0 + ((u >> 6) & 0x3f);
            lpt_buffer[uptr->POS++] = 0x80 + (u & 0x3f);
        } else {
            lpt_buffer[uptr->POS++] = u & 0x7f;
        }
        uptr->COL++;
    } else if (c >= 040) {
        lpt_buffer[uptr->POS++] = c;
        uptr->COL++;
    }
    if (uptr->COL == 132)
        lpt_printline(uptr, 1);
    return;
}

t_stat lpt_svc (UNIT *uptr)
{
    char    c;
    int     pos;
    int     cpos;

    if ((uptr->flags & DONE_FLG) != 0) {
        set_interrupt(LP_DEVNUM, uptr->STATUS);
        return SCPE_OK;
    }
    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->STATUS |= ERR_FLG;
        set_interrupt(LP_DEVNUM, (uptr->STATUS >> 3));
        return SCPE_OK;
    }

    if (uptr->STATUS & CLR_LPT) {
        for (pos = 0; pos < uptr->COL; lpt_buffer[pos++] = ' ');
        uptr->POS = uptr->COL;
        lpt_printline(uptr, 0);
        uptr->STATUS &= ~(DEL_FLG|ERR_FLG|BUSY_FLG|CLR_LPT);
        uptr->STATUS |= DONE_FLG;
        set_interrupt(LP_DEVNUM, uptr->STATUS);
        return SCPE_OK;
    }

    for (cpos = 0; cpos < 5; cpos++) {
        c = lpt_chbuf[cpos];
        if (uptr->STATUS & DEL_FLG) {
            lpt_output(uptr, c);
            uptr->STATUS &= ~DEL_FLG;
        } else if (c == 0177) {  /* Check for DEL Character */
            uptr->STATUS |= DEL_FLG;
        } else if (c < 040) { /* Control character */
            switch(c) {
            case 011:     /* Horizontal tab, space to 8'th column */
                      lpt_output(uptr, ' ');
                      while ((uptr->COL & 07) != 0)
                         lpt_output(uptr, ' ');
                      break;
            case 015:     /* Carriage return, print line */
                      lpt_printline(uptr, 0);
                      break;
            case 012:     /* Line feed, print line, space one line */
                      lpt_printline(uptr, 1);
                      uptr->LINE++;
                      break;
            case 014:     /* Form feed, skip to top of page */
                      lpt_printline(uptr, 0);
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->LINE = 0;
                      break;
            case 013:     /* Vertical tab, Skip mod 20 */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 20) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->LINE++;
                      }
                      break;
            case 020:     /* Skip even lines */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 2) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->LINE++;
                      }
                      break;
            case 021:     /* Skip third lines */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 3) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->LINE++;
                      }
                      break;
            case 022:     /* Skip one line */
                      lpt_printline(uptr, 1);
                      sim_fwrite("\r\n", 1, 2, uptr->fileref);
                      uptr->LINE+=2;
                      break;
            case 023:     /* Skip every 10 lines */
                      lpt_printline(uptr, 1);
                      while((uptr->LINE % 10) != 0) {
                          sim_fwrite("\r\n", 1, 2, uptr->fileref);
                          uptr->LINE++;
                      }
                      break;
            default:      /* Ignore */
                      break;
            }
        } else {
            lpt_output(uptr, c);
        }
    }
    uptr->STATUS &= ~BUSY_FLG;
    uptr->STATUS |= DONE_FLG;
    set_interrupt(LP_DEVNUM, uptr->STATUS);
    return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
    UNIT *uptr = &lpt_unit;
    uptr->POS = 0;
    uptr->COL = 0;
    uptr->LINE = 1;
    uptr->STATUS = DONE_FLG;
    clr_interrupt(LP_DEVNUM);
    sim_cancel (&lpt_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    reason = attach_unit (uptr, cptr);
    uptr->STATUS &= ~ERR_FLG;
    clr_interrupt(LP_DEVNUM);
    return reason;
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
    uptr->STATUS |= ERR_FLG;
    set_interrupt(LP_DEVNUM, uptr->STATUS >> 3);
    return detach_unit (uptr);
}

t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lpt_description (DEVICE *dptr)
{
    return "LP10 line printer" ;
}

#endif
