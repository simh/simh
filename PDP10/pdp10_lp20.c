/* pdp10_lp20.c: PDP-10 LP20 line printer simulator

   Copyright (c) 1993-2009, Robert M Supnik

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
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   lp20         line printer

   19-Jan-07    RMS     Added UNIT_TEXT flag
   04-Sep-05    RMS     Fixed missing return (found by Peter Schorn)
   07-Jul-05    RMS     Removed extraneous externs
   18-Mar-05    RMS     Added attached test to detach routine
   29-Dec-03    RMS     Fixed bug in scheduling
   25-Apr-03    RMS     Revised for extended file support
   29-Sep-02    RMS     Added variable vector support
                        Modified to use common Unibus routines
                        New data structures
   30-May-02    RMS     Widened POS to 32b
   06-Jan-02    RMS     Added enable/disable support
   30-Nov-01    RMS     Added extended SET/SHOW support
*/

#include "pdp10_defs.h"

#define UNIT_DUMMY      (1 << UNIT_V_UF)
#define LP_WIDTH        132                             /* printer width */

/* DAVFU RAM */

#define DV_SIZE         143                             /* DAVFU size */
#define DV_DMASK        077                             /* data mask per byte */
#define DV_TOF          0                               /* top of form channel */
#define DV_MAX          11                              /* max channel number */

/* Translation RAM */

#define TX_SIZE         256                             /* translation RAM */
#define TX_AMASK        (TX_SIZE - 1)
#define TX_DMASK        07777
#define TX_V_FL         8                               /* flags */
#define TX_M_FL         017
/* define TX_INTR       04000                           /* interrupt */
#define TX_DELH         02000                           /* delimiter */
/* define TX_XLAT       01000                           /* translate */
/* define TX_DVFU       00400                           /* DAVFU */
#define TX_SLEW         00020                           /* chan vs slew */
#define TX_VMASK        00017                           /* spacing mask */
#define TX_CHR          0                               /* states: pr char */
#define TX_RAM          1                               /* pr translation */
#define TX_DVU          2                               /* DAVFU action */
#define TX_INT          3                               /* interrupt */
#define TX_GETFL(x)     (((x) >> TX_V_FL) & TX_M_FL)

/* LPCSRA (765400) */

#define CSA_GO          0000001                         /* go */
#define CSA_PAR         0000002                         /* parity enable NI */
#define CSA_V_FNC       2                               /* function */
#define CSA_M_FNC       03
#define  FNC_PR          0                              /* print */
#define  FNC_TST         1                              /* test */
#define  FNC_DVU         2                              /* load DAVFU */
#define  FNC_RAM         3                              /* load translation RAM */
#define  FNC_INTERNAL   1                               /* internal function */
#define CSA_FNC         (CSA_M_FNC << CSA_V_FNC)
#define CSA_V_UAE       4                               /* Unibus addr extension */
#define CSA_UAE         (03 << CSA_V_UAE)
#define CSA_IE          0000100                         /* interrupt enable */
#define CSA_DONE        0000200                         /* done */
#define CSA_INIT        0000400                         /* init */
#define CSA_ECLR        0001000                         /* clear errors */
#define CSA_DELH        0002000                         /* delimiter hold */
#define CSA_ONL         0004000                         /* online */
#define CSA_DVON        0010000                         /* DAVFU online */
#define CSA_UNDF        0020000                         /* undefined char */
#define CSA_PZRO        0040000                         /* page counter zero */
#define CSA_ERR         0100000                         /* error */
#define CSA_RW          (CSA_DELH | CSA_IE | CSA_UAE | CSA_FNC | CSA_PAR | CSA_GO)
#define CSA_MBZ         (CSA_ECLR | CSA_INIT)
#define CSA_GETUAE(x)   (((x) & CSA_UAE) << (16 - CSA_V_UAE))
#define CSA_GETFNC(x)   (((x) >> CSA_V_FNC) & CSA_M_FNC)

/* LPCSRB (765402) */

#define CSB_GOE         0000001                         /* go error */
#define CSB_DTE         0000002                         /* DEM timing error NI */
#define CSB_MTE         0000004                         /* MSYN error (Ubus timeout) */
#define CSB_RPE         0000010                         /* RAM parity error NI */
#define CSB_MPE         0000020                         /* MEM parity error NI */
#define CSB_LPE         0000040                         /* LPT parity error NI */
#define CSB_DVOF        0000100                         /* DAVFU not ready */
#define CSB_OFFL        0000200                         /* offline */
#define CSB_TEST        0003400                         /* test mode */
#define CSB_OVFU        0004000                         /* optical VFU NI */
#define CSB_PBIT        0010000                         /* data parity bit NI */
#define CSB_NRDY        0020000                         /* printer error NI */
#define CSB_LA180       0040000                         /* LA180 printer NI */
#define CSB_VLD         0100000                         /* valid data NI */
#define CSB_ECLR        (CSB_GOE | CSB_DTE | CSB_MTE | CSB_RPE | CSB_MPE | CSB_LPE)
#define CSB_ERR         (CSB_ECLR | CSB_DVOF | CSB_OFFL)
#define CSB_RW          CSB_TEST
#define CSB_MBZ         (CSB_DTE | CSB_RPE | CSB_MPE | CSB_LPE | CSB_OVFU |\
                         CSB_PBIT | CSB_NRDY | CSB_LA180 | CSB_VLD)

/* LPBA (765404) */

/* LPBC (765506) */

#define BC_MASK         0007777                         /* <15:12> MBZ */

/* LPPAGC (765510) */

#define PAGC_MASK       0007777                         /* <15:12> MBZ */

/* LPRDAT (765512) */

#define RDAT_MASK       0007777                         /* <15:12> MBZ */

/* LPCOLC/LPCBUF (765514) */

/* LPCSUM/LPPDAT (765516) */

extern d10 *M;                                          /* main memory */
extern int32 int_req;

int32 lpcsa = 0;                                        /* control/status A */
int32 lpcsb = 0;                                        /* control/status B */
int32 lpba = 0;                                         /* bus address */
int32 lpbc = 0;                                         /* byte count */
int32 lppagc = 0;                                       /* page count */
int32 lprdat = 0;                                       /* RAM data */
int32 lpcbuf = 0;                                       /* character buffer */
int32 lpcolc = 0;                                       /* column count */
int32 lppdat = 0;                                       /* printer data */
int32 lpcsum = 0;                                       /* checksum */
int32 dvptr = 0;                                        /* davfu pointer */
int32 dvlnt = 0;                                        /* davfu length */
int32 lp20_irq = 0;                                     /* int request */
int32 lp20_stopioe = 0;                                 /* stop on error */
int16 txram[TX_SIZE] = { 0 };                           /* translation RAM */
int16 davfu[DV_SIZE] = { 0 };                           /* DAVFU */

DEVICE lp20_dev;
t_stat lp20_rd (int32 *data, int32 pa, int32 access);
t_stat lp20_wr (int32 data, int32 pa, int32 access);
int32 lp20_inta (void);
t_stat lp20_svc (UNIT *uptr);
t_stat lp20_reset (DEVICE *dptr);
t_stat lp20_attach (UNIT *uptr, char *ptr);
t_stat lp20_detach (UNIT *uptr);
t_stat lp20_clear_vfu (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool lp20_print (int32 c);
t_bool lp20_adv (int32 c, t_bool advdvu);
t_bool lp20_davfu (int32 c);
void update_lpcs (int32 flg);

/* LP data structures

   lp20_dev     LPT device descriptor
   lp20_unit    LPT unit descriptor
   lp20_reg     LPT register list
*/

DIB lp20_dib = {
    IOBA_LP20, IOLN_LP20, &lp20_rd, &lp20_wr,
    1, IVCL (LP20), VEC_LP20, { &lp20_inta }
    };

UNIT lp20_unit = {
    UDATA (&lp20_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG lp20_reg[] = {
    { ORDATA (LPCSA, lpcsa, 16) },
    { ORDATA (LPCSB, lpcsb, 16) },
    { ORDATA (LPBA, lpba, 16) },
    { ORDATA (LPBC, lpbc, 12) },
    { ORDATA (LPPAGC, lppagc, 12) },
    { ORDATA (LPRDAT, lprdat, 12) },
    { ORDATA (LPCBUF, lpcbuf, 8) },
    { ORDATA (LPCOLC, lpcolc, 8) },
    { ORDATA (LPPDAT, lppdat, 8) },
    { ORDATA (LPCSUM, lpcsum, 8) },
    { ORDATA (DVPTR, dvptr, 7) },
    { ORDATA (DVLNT, dvlnt, 7), REG_RO + REG_NZ },
    { FLDATA (INT, int_req, INT_V_LP20) },
    { FLDATA (IRQ, lp20_irq, 0) },
    { FLDATA (ERR, lpcsa, CSR_V_ERR) },
    { FLDATA (DONE, lpcsa, CSR_V_DONE) },
    { FLDATA (IE, lpcsa, CSR_V_IE) },
    { DRDATA (POS, lp20_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, lp20_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lp20_stopioe, 0) },
    { BRDATA (TXRAM, txram, 8, 12, TX_SIZE) },
    { BRDATA (DAVFU, davfu, 8, 12, DV_SIZE) },
    { ORDATA (DEVADDR, lp20_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, lp20_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB lp20_mod[] = {
    { UNIT_DUMMY, 0, NULL, "VFUCLEAR", &lp20_clear_vfu },
    { MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

DEVICE lp20_dev = {
    "LP20", &lp20_unit, lp20_reg, lp20_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lp20_reset,
    NULL, &lp20_attach, &lp20_detach,
    &lp20_dib, DEV_DISABLE | DEV_UBUS
    };

/* Line printer routines

   lp20_rd      I/O page read
   lp20_wr      I/O page write
   lp20_svc     process event (printer ready)
   lp20_reset   process reset
   lp20_attach  process attach
   lp20_detach  process detach
*/

t_stat lp20_rd (int32 *data, int32 pa, int32 access)
{
update_lpcs (0);                                        /* update csr's */
switch ((pa >> 1) & 07) {                               /* case on PA<3:1> */

    case 00:                                            /* LPCSA */
        *data = lpcsa = lpcsa & ~CSA_MBZ;
        break;

    case 01:                                            /* LPCSB */
        *data = lpcsb = lpcsb & ~CSB_MBZ;
        break;

    case 02:                                            /* LPBA */
        *data = lpba;
        break;

    case 03:                                            /* LPBC */
        *data = lpbc = lpbc & BC_MASK;
        break;

    case 04:                                            /* LPPAGC */
        *data = lppagc = lppagc & PAGC_MASK;
        break;

    case 05:                                            /* LPRDAT */
        *data = lprdat = lprdat & RDAT_MASK;
        break;

    case 06:                                            /* LPCOLC/LPCBUF */
        *data = (lpcolc << 8) | lpcbuf;
        break;

    case 07:                                            /* LPCSUM/LPPDAT */
        *data = (lpcsum << 8) | lppdat;
        break;
        }                                               /* end case PA */

return SCPE_OK;
}

t_stat lp20_wr (int32 data, int32 pa, int32 access)
{
update_lpcs (0);                                        /* update csr's */
switch ((pa >> 1) & 07) {                               /* case on PA<3:1> */

    case 00:                                            /* LPCSA */
        if (access == WRITEB)
            data = (pa & 1)? (lpcsa & 0377) | (data << 8): (lpcsa & ~0377) | data;
        if (data & CSA_ECLR) {                          /* error clear? */
            lpcsa = (lpcsa | CSA_DONE) & ~CSA_GO;       /* set done, clr go */
            lpcsb = lpcsb & ~CSB_ECLR;                  /* clear err */
            sim_cancel (&lp20_unit);                    /* cancel I/O */
            }
        if (data & CSA_INIT)                            /* init? */
            lp20_reset (&lp20_dev);
        if (data & CSA_GO) {                            /* go set? */
            if ((lpcsa & CSA_GO) == 0) {                /* not set before? */
                if (lpcsb & CSB_ERR)
                    lpcsb = lpcsb | CSB_GOE;
                lpcsum = 0;                             /* clear checksum */
                sim_activate (&lp20_unit, lp20_unit.wait);
                }
            }
        else sim_cancel (&lp20_unit);                   /* go clr, stop DMA */
        lpcsa = (lpcsa & ~CSA_RW) | (data & CSA_RW);
        break;

    case 01:                                            /* LPCSB */
        break;                                          /* ignore writes to TEST */

    case 02:                                            /* LPBA */
        if (access == WRITEB)
            data = (pa & 1)? (lpba & 0377) | (data << 8): (lpba & ~0377) | data;
        lpba = data;
        break;

    case 03:                                            /* LPBC */
        if (access == WRITEB)
            data = (pa & 1)? (lpbc & 0377) | (data << 8): (lpbc & ~0377) | data;
        lpbc = data & BC_MASK;
        lpcsa = lpcsa & ~CSA_DONE;
        break;

    case 04:                                            /* LPPAGC */
        if (access == WRITEB)
            data = (pa & 1)? (lppagc & 0377) | (data << 8): (lppagc & ~0377) | data;
        lppagc = data & PAGC_MASK;
        break;

    case 05:                                            /* LPRDAT */
        if (access == WRITEB)
            data = (pa & 1)? (lprdat & 0377) | (data << 8): (lprdat & ~0377) | data;
        lprdat = data & RDAT_MASK;
        txram[lpcbuf & TX_AMASK] = lprdat;              /* load RAM */
        break;

    case 06:                                            /* LPCOLC/LPCBUF */
        if ((access == WRITEB) && (pa & 1))             /* odd byte */
            lpcolc = data & 0377;
        else {
            lpcbuf = data & 0377;                       /* even byte, word */
            if (access == WRITE)
                lpcolc = (data >> 8) & 0377;
            }
        break;

    case 07:                                            /* LPCSUM/LPPDAT */
        break;                                          /* read only */
        }                                               /* end case PA */

update_lpcs (0);
return SCPE_OK;
}

/* Line printer service

   The translation RAM case table is derived from the LP20 spec and
   verified against the LP20 RAM simulator in TOPS10 7.04 LPTSPL.
   The equations are:

   flags := inter, delim, xlate, paper, delim_hold (from CSRA)
   actions : = print_input, print_xlate, davfu_action, interrupt

   if (inter) {
        if (!xlate || delim || delim_hold)
            interrupt;
        else if (paper)
            davfu_action;
        else print_xlate;
		}
   else if (paper) {
        if (xlate || delim || delim_hold)
            davfu_action;
        else print_input;
		}
   else {
        if (xlate || delim || delim_hold)
            print_xlate;
        else print_input;
		}
*/

t_stat lp20_svc (UNIT *uptr)
{
int32 fnc, i, tbc, temp, txst;
int32 dvld = -2;                                        /* must be even */
uint16 wd10;
t_bool cont;
a10 ba;

static const uint32 txcase[32] = {
    TX_CHR, TX_RAM, TX_CHR, TX_DVU, TX_RAM, TX_RAM, TX_DVU, TX_DVU,
    TX_RAM, TX_RAM, TX_DVU, TX_DVU, TX_RAM, TX_RAM, TX_DVU, TX_DVU,
    TX_INT, TX_INT, TX_INT, TX_INT, TX_RAM, TX_INT, TX_DVU, TX_INT,
    TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT, TX_INT
    };

lpcsa = lpcsa & ~CSA_GO;
ba = CSA_GETUAE (lpcsa) | lpba;
fnc = CSA_GETFNC (lpcsa);
tbc = 010000 - lpbc;
if (((fnc & FNC_INTERNAL) == 0) && ((lp20_unit.flags & UNIT_ATT) == 0)) {
    update_lpcs (CSA_ERR);
    return IORETURN (lp20_stopioe, SCPE_UNATT);
    }
if ((fnc == FNC_PR) && (dvlnt == 0)) {
    update_lpcs (CSA_ERR);
    return SCPE_OK;
    }

for (i = 0, cont = TRUE; (i < tbc) && cont; ba++, i++) {
    if (Map_ReadW (ba, 2, &wd10)) {                     /* get word, err? */
        lpcsb = lpcsb | CSB_MTE;                        /* set NXM error */
        update_lpcs (CSA_ERR);                          /* set done */
        break;
        }
    lpcbuf = (wd10 >> ((ba & 1)? 8: 0)) & 0377;         /* get character */
    lpcsum = (lpcsum + lpcbuf) & 0377;                  /* add into checksum */
    switch (fnc) {                                      /* switch on function */

/* Translation RAM load */

    case FNC_RAM:                                       /* RAM load */
        txram[(i >> 1) & TX_AMASK] = wd10 & TX_DMASK;
        break;

/* DAVFU RAM load.  The DAVFU RAM is actually loaded in bytes, delimited by
   a start (354 to 356) and stop (357) byte pair.  If the number of bytes 
   loaded is odd, or no bytes are loaded, the DAVFU is invalid.
*/

    case FNC_DVU:                                       /* DVU load */
        if ((lpcbuf >= 0354) && (lpcbuf <= 0356))       /* start DVU load? */
            dvld = dvlnt = 0;                           /* reset lnt */
        else if (lpcbuf == 0357) {                      /* stop DVU load? */
            dvptr = 0;                                  /* reset ptr */
            if (dvld & 1)                               /* if odd, invalid */
                dvlnt = 0;
            }
        else if (dvld == 0) {                           /* even state? */
            temp = lpcbuf & DV_DMASK;
            dvld = 1;
            }
        else if (dvld == 1) {                           /* odd state? */
            if (dvlnt < DV_SIZE)
                davfu[dvlnt++] = temp | ((lpcbuf & DV_DMASK) << 6);
            dvld = 0;
            }
        break;

/* Print characters */

    case FNC_PR:                                        /* print */
        lprdat = txram[lpcbuf];                         /* get RAM char */
        txst = (TX_GETFL (lprdat) << 1) |               /* get state */
            ((lpcsa & CSA_DELH)? 1: 0);                 /* plus delim hold */ 
        if (lprdat & TX_DELH)
            lpcsa = lpcsa | CSA_DELH;
        else lpcsa = lpcsa & ~CSA_DELH;
        lpcsa = lpcsa & ~CSA_UNDF;                      /* assume char ok */
        switch (txcase[txst]) {                         /* case on state */

        case TX_CHR:                                    /* take char */
            cont = lp20_print (lpcbuf);
            break;

        case TX_RAM:                                    /* take translation */
            cont = lp20_print (lprdat);
            break;

        case TX_DVU:                                    /* DAVFU action */
            if (lprdat & TX_SLEW)
                cont = lp20_adv (lprdat & TX_VMASK, TRUE);
            else cont = lp20_davfu (lprdat & TX_VMASK);
            break;

        case TX_INT:                                    /* interrupt */
            lpcsa = lpcsa | CSA_UNDF;                   /* set flag */
            cont = FALSE;                               /* force stop */
            break;
            }                                           /* end case char state */
        break;

    case FNC_TST:                                       /* test */
        break;
        }                                               /* end case function */
    }                                                   /* end for */
lpba = ba & 0177777;
lpcsa = (lpcsa & ~CSA_UAE) | ((ba >> (16 - CSA_V_UAE)) & CSA_UAE);
lpbc = (lpbc + i) & BC_MASK;
if (lpbc)                                               /* intr, but not done */
    update_lpcs (CSA_MBZ);
else update_lpcs (CSA_DONE);                            /* intr and done */
if ((fnc == FNC_PR) && ferror (lp20_unit.fileref)) {
    perror ("LP I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Print routines

   lp20_print           print a character
   lp20_adv             advance n lines
   lp20_davfu           advance to channel on VFU

   Return TRUE to continue printing, FALSE to stop
*/

t_bool lp20_print (int32 c)
{
t_bool r = TRUE;
int32 i, rpt = 1;

lppdat = c & 0177;                                      /* mask char to 7b */
if (lppdat == 000)                                      /* NUL? no op */
    return TRUE;
if (lppdat == 012)                                      /* LF? adv carriage */
    return lp20_adv (1, TRUE);
if (lppdat == 014)                                      /* FF? top of form */
    return lp20_davfu (DV_TOF);
if (lppdat == 015)                                      /* CR? reset col cntr */
    lpcolc = 0;
else if (lppdat == 011) {                               /* TAB? simulate */
    lppdat = ' ';                                       /* with spaces */
    if (lpcolc >= 128) {
        r = lp20_adv (1, TRUE);                         /* eol? adv carriage */
        rpt = 8;                                        /* adv to col 9 */
        }
    else rpt = 8 - (lpcolc & 07);                       /* else adv 1 to 8 */
    }           
else {
    if (lppdat < 040)                                   /* cvt non-prnt to spc */
        lppdat = ' ';
    if (lpcolc >= LP_WIDTH)                             /* line full? */
        r = lp20_adv (1, TRUE);                         /* adv carriage */
    }
for (i = 0; i < rpt; i++)
    fputc (lppdat, lp20_unit.fileref); 
lp20_unit.pos = ftell (lp20_unit.fileref);
lpcolc = lpcolc + rpt;
return r;
}

t_bool lp20_adv (int32 cnt, t_bool dvuadv)
{
int32 i;

if (cnt == 0)
    return TRUE;
lpcolc = 0;                                             /* reset col cntr */
for (i = 0; i < cnt; i++)
    fputc ('\n', lp20_unit.fileref);
lp20_unit.pos = ftell (lp20_unit.fileref);              /* print 'n' newlines */
if (dvuadv)                                             /* update DAVFU ptr */
    dvptr = (dvptr + cnt) % dvlnt;
if (davfu[dvptr] & (1 << DV_TOF)) {                     /* at top of form? */
    if (lppagc = (lppagc - 1) & PAGC_MASK) {            /* decr page cntr */
        lpcsa = lpcsa & ~CSA_PZRO;                      /* update status */
        return TRUE;
        }
    else {
        lpcsa = lpcsa | CSA_PZRO;                       /* stop if zero */
        return FALSE;
        }
    }
return TRUE;
}

t_bool lp20_davfu (int32 cnt)
{
int i;

if (cnt > DV_MAX)                                       /* inval chan? */
    cnt = 7;
for (i = 0; i < dvlnt; i++) {                           /* search DAVFU */
    dvptr = dvptr + 1;                                  /* adv DAVFU ptr */
    if (dvptr >= dvlnt)                                 /* wrap at end */
        dvptr = 0;
    if (davfu[dvptr] & (1 << cnt)) {                    /* channel stop set? */
        if (cnt)                                        /* ~TOF, adv */
            return lp20_adv (i + 1, FALSE);
        if (lpcolc)                                     /* TOF, need newline? */
            lp20_adv (1, FALSE);
        fputc ('\f', lp20_unit.fileref);                /* print form feed */
        lp20_unit.pos = ftell (lp20_unit.fileref); 
        if (lppagc = (lppagc - 1) & PAGC_MASK) {        /* decr page cntr */
            lpcsa = lpcsa & ~CSA_PZRO;                  /* update status */
            return TRUE;
            }
        else {
            lpcsa = lpcsa | CSA_PZRO;                   /* stop if zero */
            return FALSE;
            }
        }
    }                                                   /* end for */
dvlnt = 0;                                              /* DAVFU error */
return FALSE;
}

/* Update LPCSA, optionally request interrupt */

void update_lpcs (int32 flg)
{
if (flg)                                                /* set int req */
    lp20_irq = 1;
lpcsa = (lpcsa | flg) & ~(CSA_MBZ | CSA_ERR | CSA_ONL | CSA_DVON);
lpcsb = (lpcsb | CSB_OFFL | CSB_DVOF) & ~CSB_MBZ;
if (lp20_unit.flags & UNIT_ATT) {
    lpcsa = lpcsa | CSA_ONL;
    lpcsb = lpcsb & ~CSB_OFFL;
    }
else lpcsa = lpcsa & ~CSA_DONE;
if (dvlnt) {
    lpcsa = lpcsa | CSA_DVON;
    lpcsb = lpcsb & ~CSB_DVOF;
    }
if (lpcsb & CSB_ERR)
    lpcsa = lpcsa | CSA_ERR;
if ((lpcsa & CSA_IE) && lp20_irq)
    int_req = int_req | INT_LP20;
else int_req = int_req & ~INT_LP20;
return;
}

/* Acknowledge interrupt (clear internal request) */

int32 lp20_inta (void)
{
lp20_irq = 0;                                           /* clear int req */
return lp20_dib.vec;
}

t_stat lp20_reset (DEVICE *dptr)
{
lpcsa =  CSA_DONE;
lpcsb = 0;
lpba = lpbc = lppagc = lpcolc = 0;                      /* clear registers */
lprdat = lppdat = lpcbuf = lpcsum = 0;
lp20_irq = 0;                                           /* clear int req */
dvptr = 0;                                              /* reset davfu ptr */
sim_cancel (&lp20_unit);                                /* deactivate unit */
update_lpcs (0);                                        /* update status */
return SCPE_OK;
}

t_stat lp20_attach (UNIT *uptr, char *cptr)
{
t_stat reason;
    
reason = attach_unit (uptr, cptr);                      /* attach file */
if (lpcsa & CSA_ONL)                                    /* just file chg? */
    return reason;
if (sim_is_active (&lp20_unit))                         /* busy? no int */
    update_lpcs (0);
else update_lpcs (CSA_MBZ);                             /* interrupt */
return reason;
}

t_stat lp20_detach (UNIT *uptr)
{
t_stat reason;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
reason = detach_unit (uptr);
sim_cancel (&lp20_unit);
lpcsa = lpcsa & ~CSA_GO;
update_lpcs (CSA_MBZ);
return reason;
}

t_stat lp20_clear_vfu (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int i;

if (!get_yn ("Clear DAVFU? [N]", FALSE))
    return SCPE_OK;
for (i = 0; i < DV_SIZE; i++)
    davfu[i] = 0;
dvlnt = dvptr = 0;
update_lpcs (0);
return SCPE_OK;
}
