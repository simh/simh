/* pdp1_lp.c: PDP-1 line printer simulator

   Copyright (c) 1993-2008, Robert M. Supnik

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
   bused in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   lpt          Type 62 line printer for the PDP-1

   19-Jan-07    RMS     Added UNIT_TEXT flag
   21-Dec-06    RMS     Added 16-channel SBS support
   07-Sep-03    RMS     Changed ioc to ios
   23-Jul-03    RMS     Fixed bugs in instruction decoding, overprinting
                        Revised to detect I/O wait hang
   25-Apr-03    RMS     Revised for extended file support
   30-May-02    RMS     Widened POS to 32b
   13-Apr-01    RMS     Revised for register arrays
*/

#include "pdp1_defs.h"

#define BPTR_MAX        40                              /* pointer max */
#define LPT_BSIZE       (BPTR_MAX * 3)                  /* line size */
#define BPTR_MASK       077                             /* buf ptr mask */

int32 lpt_spc = 0;                                      /* print (0) vs spc */
int32 lpt_ovrpr = 0;                                    /* overprint */
int32 lpt_stopioe = 0;                                  /* stop on error */
int32 lpt_bptr = 0;                                     /* buffer ptr */
int32 lpt_sbs = 0;                                      /* SBS level */
char lpt_buf[LPT_BSIZE + 1] = { 0 };
static const unsigned char lpt_trans[64] = {
    ' ','1','2','3','4','5','6','7','8','9','\'','~','#','V','^','<',
    '0','/','S','T','U','V','W','X','Y','Z','"',',','>','^','-','?',
    '@','J','K','L','M','N','O','P','Q','R','$','=','-',')','-','(',
    '_','A','B','C','D','E','F','G','H','I','*','.','+',']','|','['
    };

extern int32 ios, cpls, iosta;
extern int32 stop_inst;

t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *ptr);
t_stat lpt_detach (UNIT *uptr);
t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *lpt_description (DEVICE *dptr);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit
   lpt_reg      LPT register list
*/

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG lpt_reg[] = {
    { ORDATAD (BUF, lpt_unit.buf, 8, "last data item processed") },
    { FLDATAD (PNT, iosta, IOS_V_PNT, "printing done flag") },
    { FLDATAD (SPC, iosta, IOS_V_SPC, "spacing done flag") },
    { FLDATAD (RPLS, cpls, CPLS_V_LPT, "return restart pulse flag") },
    { DRDATAD (BPTR, lpt_bptr, 6, "print buffer pointer") },
    { ORDATA (LPT_STATE, lpt_spc, 6), REG_HRO },
    { FLDATA (LPT_OVRPR, lpt_ovrpr, 0), REG_HRO },
    { DRDATAD (POS, lpt_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { DRDATAD (TIME, lpt_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { FLDATAD (STOP_IOE, lpt_stopioe, 0, "stop on I/O error") },
    { BRDATAD (LBUF, lpt_buf, 8, 8, LPT_BSIZE, "line buffer") },
    { DRDATA (SBSLVL, lpt_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB lpt_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &lpt_sbs },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    NULL, DEV_DISABLE, 0,
    NULL, NULL, NULL, &lpt_help, NULL, NULL, 
    &lpt_description
    };

/* Line printer IOT routine */

int32 lpt (int32 inst, int32 dev, int32 dat)
{
int32 i;

if (lpt_dev.flags & DEV_DIS)                            /* disabled? */
    return (stop_inst << IOT_V_REASON) | dat;           /* stop if requested */
if ((inst & 07000) == 01000) {                          /* fill buf */
    if (lpt_bptr < BPTR_MAX) {                          /* limit test ptr */
        i = lpt_bptr * 3;                               /* cvt to chr ptr */
        lpt_buf[i] = lpt_trans[(dat >> 12) & 077];
        lpt_buf[i + 1] = lpt_trans[(dat >> 6) & 077];
        lpt_buf[i + 2] = lpt_trans[dat & 077];
        }
    lpt_bptr = (lpt_bptr + 1) & BPTR_MASK;
    return dat;
    }
if ((inst & 07000) == 02000) {                          /* space */
    iosta = iosta & ~IOS_SPC;                           /* space, clear flag */
    lpt_spc = (inst >> 6) & 077;                        /* state = space n */
    }
else if ((inst & 07000) == 00000) {                     /* print */
    iosta = iosta & ~IOS_PNT;                           /* clear flag */
    lpt_spc = 0;                                        /* state = print */
    }
else return (stop_inst << IOT_V_REASON) | dat;          /* not implemented */
if (GEN_CPLS (inst)) {                                  /* comp pulse? */
    ios = 0;                                            /* clear flop */
    cpls = cpls | CPLS_LPT;                             /* request completion */
    }
else cpls = cpls & ~CPLS_LPT;
sim_activate (&lpt_unit, lpt_unit.wait);                /* activate */
return dat;
}

/* Unit service, printer is in one of three states

   lpt_spc = 000                write buffer to file, set overprint
   lpt_iot = 02x                space command x, clear overprint
*/

t_stat lpt_svc (UNIT *uptr)
{
int32 i;
static const char *lpt_cc[] = {
    "\n",
    "\n\n",
    "\n\n\n",
    "\n\n\n\n\n\n",
    "\n\n\n\n\n\n\n\n\n\n\n",
    "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
    "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",
    "\f"
    };

if (cpls & CPLS_LPT) {                                  /* completion pulse? */
    ios = 1;                                            /* restart */
    cpls = cpls & ~CPLS_LPT;                            /* clr pulse pending */
    }
dev_req_int (lpt_sbs);                                  /* req interrupt */
if (lpt_spc) {                                          /* space? */
    iosta = iosta | IOS_SPC;                            /* set flag */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return IORETURN (lpt_stopioe, SCPE_UNATT);
    fputs (lpt_cc[lpt_spc & 07], uptr->fileref);        /* print cctl */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (uptr->fileref)) {                       /* error? */
        sim_perror ("LPT I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    lpt_ovrpr = 0;                                      /* dont overprint */
    }
else {
    iosta = iosta | IOS_PNT;                            /* print */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return IORETURN (lpt_stopioe, SCPE_UNATT);
    if (lpt_ovrpr)                                      /* overprint? */
        fputc ('\r', uptr->fileref);
    fputs (lpt_buf, uptr->fileref);                     /* print buffer */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (uptr->fileref)) {                       /* test error */
        sim_perror ("LPT I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    lpt_bptr = 0;
    for (i = 0; i <= LPT_BSIZE; i++)                    /* clear buffer */
        lpt_buf[i] = 0;
    lpt_ovrpr = 1;                                      /* set overprint */
    }
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
int32 i;

lpt_bptr = 0;                                           /* clear buffer ptr */
for (i = 0; i <= LPT_BSIZE; i++)                        /* clear buffer */
    lpt_buf[i] = 0;
lpt_spc = 0;                                            /* clear state */
lpt_ovrpr = 0;                                          /* clear overprint */
cpls = cpls & ~CPLS_LPT;
iosta = iosta & ~(IOS_PNT | IOS_SPC);                   /* clear flags */
sim_cancel (&lpt_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

sim_switches |= SWMASK('A');            /* position to EOF */
reason = attach_unit (uptr, cptr);
return reason;
}

t_stat lpt_detach (UNIT *uptr)
{
return detach_unit (uptr);
}

t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n\n");
fprintf (st, "The default position after ATTACH is to position at the end of an existing file.\n");
fprintf (st, "A new file can be created if you attach with the -N switch.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         STOP_IOE   processed as\n");
fprintf (st, "    not attached  1          out of paper\n");
fprintf (st, "                  0          disk not ready\n\n");
fprintf (st, "    OS I/O error  x          report error and stop\n");
return SCPE_OK;
}

const char *lpt_description (DEVICE *dptr)
{
return "Type 62 Line Printer";
}
