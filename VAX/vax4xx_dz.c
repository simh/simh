/* vax4xx_dz.c: Built-in DZ terminal multiplexor simulator

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 2001-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   dz           DZ terminal multiplexor
*/

#include "vax_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include "vax_lk.h"
#include "vax_vs.h"

#define DZ_LINES        4
#define DZ_LNOMASK      (DZ_LINES - 1)                  /* mask for lineno */
#define DZ_LMASK        ((1 << DZ_LINES) - 1)           /* mask of lines */
#define DZ_SILO_ALM     16                              /* silo alarm level */

/* line functions */

#define DZ_TMXR         0
#define DZ_CONSOLE      1
#define DZ_KEYBOARD     2
#define DZ_MOUSE        3

/* DZCSR - 200A0000 - control/status register */

#define CSR_MAINT       0x0008                          /* maint - NI */
#define CSR_CLR         0x0010                          /* clear */
#define CSR_MSE         0x0020                          /* master scan enb */
#define CSR_RDONE       0x0080                          /* rcv done - RO */
#define CSR_V_TLINE     8                               /* xmit line - RO */
#define CSR_TLINE       (DZ_LNOMASK << CSR_V_TLINE)
#define CSR_SAE         0x1000                          /* silo alm enb */
#define CSR_SA          0x2000                          /* silo alm - RO */
#define CSR_TRDY        0x8000                          /* xmit rdy - RO */
#define CSR_RW          (CSR_MAINT | CSR_MSE | CSR_SAE)
#define CSR_MBZ         (0xC07 | CSR_CLR)

#define CSR_GETTL(x)    (((x) >> CSR_V_TLINE) & DZ_LNOMASK)
#define CSR_PUTTL(x,y)  x = ((x) & ~CSR_TLINE) | (((y) & DZ_LNOMASK) << CSR_V_TLINE)

BITFIELD dz_csr_bits[] = {
    BITNCF(3),                                          /* not used */
    BIT(MAINT),                                         /* Maint */
    BIT(CLR),                                           /* clear */
    BIT(MSE),                                           /* naster scan enable */
    BITNCF(1),                                          /* not used */
    BIT(RDONE),                                         /* receive done */
    BITF(TLINE,2),                                      /* transmit line */
    BITNCF(2),                                          /* not used */
    BIT(SAE),                                           /* silo alarm enable */
    BIT(SA),                                            /* silo alarm */
    BITNCF(1),                                          /* not used */
    BIT(TRDY),                                          /* transmit ready */
    ENDBITS
    };

/* DZRBUF - 200A0004 - receive buffer, read only */

#define RBUF_CHAR       0x00FF                          /* rcv char */
#define RBUF_V_RLINE    8                               /* rcv line */
#define RBUF_RLINE      (DZ_LNOMASK << RBUF_V_RLINE)
#define RBUF_PARE       0x1000                          /* parity err - NI */
#define RBUF_FRME       0x2000                          /* frame err */
#define RBUF_OVRE       0x4000                          /* overrun err - NI */
#define RBUF_VALID      0x8000                          /* rcv valid */
#define RBUF_MBZ        0x0C00

#define RBUF_GETRL(x)    (((x) >> RBUF_V_RLINE) & DZ_LNOMASK)
#define RBUF_PUTRL(x,y)  x = ((x) & ~RBUF_RLINE) | (((y) & DZ_LNOMASK) << RBUF_V_RLINE)

BITFIELD dz_rbuf_bits[] = {
    BITFFMT(RBUF,8,"%02X"),                             /* Received Character */
    BITF(RLINE,2),                                      /* receive line */
    BITNCF(2),                                          /* not used */
    BIT(PARE),                                          /* parity error */
    BIT(FRME),                                          /* frame error */
    BIT(OVRE),                                          /* overrun error */
    BIT(VALID),                                         /* receive valid */
    ENDBITS
    };

const char *dz_charsizes[] = {"5", "6", "7", "8"};
const char *dz_baudrates[] = {"50", "75", "110", "134.5", "150", "300", "600", "1200", 
                        "1800", "2000", "2400", "3600", "4800", "7200", "9600", "19200"};
const char *dz_parity[] = {"N", "E", "N", "O"};
const char *dz_stopbits[] = {"1", "2", "1", "1.5"};

/* DZLPR - 200A0004 - line parameter register, write only, word access only */

#define LPR_V_LINE      0                               /* line */
#define LPR_V_SPEED     8                               /* speed code */
#define LPR_M_SPEED     0x0F00                          /* speed code mask */
#define LPR_V_CHARSIZE  3                               /* char size code */
#define LPR_M_CHARSIZE  0x0018                          /* char size code mask */
#define LPR_V_STOPBITS  5                               /* stop bits code */
#define LPR_V_PARENB    6                               /* parity enable */
#define LPR_V_PARODD    7                               /* parity odd */
#define LPR_GETSPD(x)   dz_baudrates[((x) & LPR_M_SPEED) >> LPR_V_SPEED]
#define LPR_GETCHARSIZE(x) dz_charsizes[((x) & LPR_M_CHARSIZE) >> LPR_V_CHARSIZE]
#define LPR_GETPARITY(x) dz_parity[(((x) >> LPR_V_PARENB) & 1) | (((x) >> (LPR_V_PARODD-1)) & 2)]
#define LPR_GETSTOPBITS(x) dz_stopbits[(((x) >> LPR_V_STOPBITS) & 1) + (((((x) & LPR_M_CHARSIZE) >> LPR_V_CHARSIZE) == 0) ? 2 : 0)]
#define LPR_LPAR        0x0FF8                          /* line pars - NI */
#define LPR_RCVE        0x1000                          /* receive enb */
#define LPR_GETLN(x)    (((x) >> LPR_V_LINE) & DZ_LNOMASK)

BITFIELD dz_lpr_bits[] = {
    BITF(LINE,2),                                       /* line */
    BITFNAM(CHARSIZE,2,dz_charsizes),                   /* character size */
    BIT(STOPBITS),                                      /* stop bits code */
    BIT(PARENB),                                        /* parity error */
    BIT(PARODD),                                        /* frame error */
    BITFNAM(SPEED,4,dz_baudrates),                      /* speed code */
    BIT(RCVE),                                          /* receive enable */
    BITNCF(3),                                          /* not used */
    ENDBITS
    };

/* DZTCR - 200A0008 - transmission control register */

#define TCR_V_XMTE      0                               /* xmit enables */
#define TCR_V_RTS2      8                               /* RTS (line 2) */
#define TCR_V_DSRS2     9                               /* DSRS (line 2) */
#define TCR_V_DTR2      10                              /* DTR (line 2) */
#define TCR_V_LLBK2     11                              /* local loopback (line 2) */
#define TCR_MBZ         0xF0F0

BITFIELD dz_tcr_bits[] = {
    BITFFMT(XMTE,8,%02X),                               /* Transmit enable */
    BIT(RTS2),                                          /* RTS (line 2) */
    BIT(DSRS2),                                         /* DSRS (line 2) */
    BIT(DTR2),                                          /* DTR (line 2) */
    BIT(LLBK2),                                         /* local loopback (line 2) */
    BITNCF(4),                                          /* not used */
    ENDBITS
    };

/* DZMSR - 200A000C - modem status register, read only */

#define MSR_V_TMI2      0                               /* test mode indicate (line2) */
#define MSR_V_RI2       2                               /* ring indicator (line 2) */
#define MSR_V_CTS2      8                               /* CTS (line 2) */
#define MSR_V_DSR2      9                               /* DSR (line 2) */
#define MSR_V_CD2       10                              /* carrier detect (line 2) */
#define MSR_V_SPDI2     11                              /* speed mode indicate (line 2) */

BITFIELD dz_msr_bits[] = {
    BIT(TMI2),                                          /* test mode indicate (line2) */
    BIT(RI2),                                           /* ring indicator (line 2) */
    BIT(CTS2),                                          /* CTS (line 2) */
    BIT(DSR2),                                          /* DSR (line 2) */
    BIT(CD2),                                           /* carrier detect (line 2) */
    BIT(SPDI2),                                         /* speed mode indicate (line 2) */
    BITNCF(4),
    ENDBITS
    };

/* DZTDR - 200A000C - transmit data, write only */

#define TDR_CHAR        0x00FF                          /* xmit char */
#define TDR_V_TBR       8                               /* xmit break - NI */

BITFIELD dz_tdr_bits[] = {
    BITFFMT(CHAR,8,%02X),                               /* xmit char */
    BITFFMT(TBR, 4,%02X),                               /* xmit break - NI */
    BITNCF(4),
    ENDBITS
    };

extern int32 tmxr_poll;                                 /* calibrated delay */

uint16 dz_csr = 0;                                      /* csr */
uint16 dz_rbuf = 0;                                     /* rcv buffer */
uint16 dz_lpr = 0;                                      /* line param */
uint16 dz_tcr = 0;                                      /* xmit control */
uint16 dz_msr = 0;                                      /* modem status */
uint16 dz_tdr = 0;                                      /* xmit data */
uint16 dz_silo[DZ_SILO_ALM] = { 0 };                    /* silo */
uint16 dz_scnt = 0;                                     /* silo used */
uint8 dz_sae = 0;                                       /* silo alarm enabled */
int32 dz_mctl = 0;                                      /* modem ctrl enabled */
int32 dz_auto = 0;                                      /* autodiscon enabled */
uint32 dz_func[DZ_LINES] = { DZ_TMXR };                 /* line function */
uint32 dz_char[DZ_LINES] = { 0 };                       /* character buffer */
int32 dz_lnorder[DZ_LINES] = { 0 };                     /* line order */
TMLN *dz_ldsc = NULL;                                   /* line descriptors */
TMXR dz_desc = { DZ_LINES, 0, 0, NULL, dz_lnorder };    /* mux descriptor */

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* trace read/write registers */
#define DBG_INT  0x0002                                 /* display interrupt activities */
#define DBG_XMT  TMXR_DBG_XMT                           /* display Transmitted Data */
#define DBG_RCV  TMXR_DBG_RCV                           /* display Received Data */
#define DBG_RET  TMXR_DBG_RET                           /* display Read Data */
#define DBG_MDM  TMXR_DBG_MDM                           /* display Modem Signals */
#define DBG_CON  TMXR_DBG_CON                           /* display connection activities */
#define DBG_TRC  TMXR_DBG_TRC                           /* display trace routine calls */
#define DBG_ASY  TMXR_DBG_ASY                           /* display Asynchronous Activities */

DEBTAB dz_debug[] = {
    { "REG",    DBG_REG, "read/write registers" },
    { "INT",    DBG_INT, "interrupt activities" },
    { "XMT",    DBG_XMT, "Transmitted Data" },
    { "RCV",    DBG_RCV, "Received Data" },
    { "RET",    DBG_RET, "Read Data" },
    { "MDM",    DBG_MDM, "Modem Signals" },
    { "CON",    DBG_CON, "connection activities" },
    { "TRC",    DBG_TRC, "trace routine calls" },
    { "ASY",    DBG_ASY, "Asynchronous Activities" },
    { 0 }
    };

t_stat dz_svc (UNIT *uptr);
t_stat dz_xmt_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat dz_attach (UNIT *uptr, CONST char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_clear (t_bool flag);
uint16 dz_getc (void);
void dz_putc (int32 line, uint16 data);
void dz_update_rcvi (void);
void dz_update_xmti (void);
t_stat dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat dz_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dz_description (DEVICE *dptr);

/* DZ data structures

   dz_dev       DZ device descriptor
   dz_unit      DZ unit list
   dz_reg       DZ register list
*/

UNIT dz_unit[2] = {
    { UDATA (&dz_svc, UNIT_IDLE|UNIT_ATTABLE|TT_MODE_8B, 0) },
    { UDATA (&dz_xmt_svc, UNIT_DIS, 0), SERIAL_OUT_WAIT } };

REG dz_reg[] = {
    { HRDATADF (CSR,   dz_csr,         16, "control/status register", dz_csr_bits) },
    { HRDATADF (RBUF,  dz_rbuf,        16, "receive buffer",          dz_rbuf_bits) },
    { HRDATADF (LPR,   dz_lpr,         16, "line parameter register", dz_lpr_bits) },
    { HRDATADF (TCR,   dz_tcr,         16, "transmission control register", dz_tcr_bits) },
    { HRDATADF (MSR,   dz_msr,         16, "modem status register",    dz_msr_bits) },
    { HRDATADF (TDR,   dz_tdr,         16, "transmit data register",   dz_tdr_bits) },
    { HRDATAD  (SAENB, dz_sae,          1, "silo alarm enabled") },
    { DRDATAD  (TIME, dz_unit[1].wait, 24, "output character delay"), PV_LEFT },
    { FLDATAD  (MDMCTL, dz_mctl, 0,        "modem control enabled") },
    { FLDATAD  (AUTODS, dz_auto, 0,        "autodisconnect enabled") },
    { FLDATAD  (TXINT,  int_req[IPL_DZTX], INT_V_DZTX, "transmit interrupt pending flag") },
    { FLDATAD  (RXINT,  int_req[IPL_DZRX], INT_V_DZRX, "receive interrupt pending flag") },
    { NULL }
    };

MTAB dz_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dz_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
        NULL, &tmxr_show_summ, (void *) &dz_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display multiplexer statistics" },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", NULL,
      NULL, &tmxr_show_lines, (void *) &dz_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", NULL,
        NULL, &tmxr_show_lines, (void *) &dz_desc, "Display number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dz_set_log, NULL, &dz_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG=n",
        &dz_set_nolog, NULL, &dz_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dz_show_log, &dz_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE dz_dev = {
    "DZ", dz_unit, dz_reg, dz_mod,
    2, DEV_RDX, 8, 1, DEV_RDX, 8,
    &tmxr_ex, &tmxr_dep, &dz_reset,
    NULL, &dz_attach, &dz_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_MUX,
    0, dz_debug, NULL, NULL,
    &dz_help, &dz_help_attach,          /* help and attach_help routines */
    (void *)&dz_desc,                   /* help context variable */
    &dz_description                     /* description routine */
    };

/* Register names for Debug tracing */
static const char *dz_rd_regs[] =
    {"CSR ", "RBUF", "TCR ", "MSR " };
static const char *dz_wr_regs[] = 
    {"CSR ", "LPR ", "TCR ", "TDR "};

/* IO dispatch routines */

int32 dz_rd (int32 pa)
{
int32 data = 0;

switch ((pa >> 2) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        data = dz_csr = dz_csr & ~CSR_MBZ;
        break;

    case 01:                                            /* RBUF */
        dz_csr = dz_csr & ~CSR_SA;                      /* clr silo alarm */
        if (dz_csr & CSR_MSE) {                         /* scanner on? */
            dz_rbuf = dz_getc ();                       /* get top of silo */
            if (!dz_rbuf)                               /* empty? re-enable */
                dz_sae = 1;
            tmxr_poll_rx (&dz_desc);                    /* poll input */
            dz_update_rcvi ();                          /* update rx intr */
            if (dz_rbuf) {
                /* Reschedule the next poll preceisely so that the
                   the programmed input speed is observed. */
                sim_clock_coschedule_abs (dz_unit, tmxr_poll);
                }
            }
        else {
            dz_rbuf = 0;                                /* no data */
            dz_update_rcvi ();                          /* no rx intr */
            }
        data = dz_rbuf;
        
        break;

    case 02:                                            /* TCR */
        data = dz_tcr = dz_tcr & ~TCR_MBZ;
        break;

    case 03:                                            /* MSR */
        if (dz_mctl) {
            int32 modem_bits;
            TMLN *lp;
            
            lp = &dz_ldsc[2];                           /* get line desc */
            tmxr_set_get_modem_bits (lp, 0, 0, &modem_bits);

            dz_msr &= ~(MSR_V_RI2 | MSR_V_CD2);
            dz_msr |= ((modem_bits&TMXR_MDM_RNG) ? MSR_V_RI2 : 0) |
                          ((modem_bits&TMXR_MDM_DCD) ? MSR_V_CD2 : 0);
            }
        data = dz_msr;
        break;
        }

sim_debug(DBG_REG, &dz_dev, "dz_rd(PA=0x%08X [%s], data=0x%X)\n", pa, dz_rd_regs[(pa >> 2) & 03], data);

SET_IRQL;
return data;
}

void dz_wr (int32 pa, int32 data, int32 access)
{
int32 line;
char lineconfig[16];
TMLN *lp;

sim_debug(DBG_REG, &dz_dev, "dz_wr(PA=0x%08X [%s], access=%d, data=0x%X)\n", pa, dz_wr_regs[(pa >> 2) & 03], access, data);

switch ((pa >> 2) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        if (access == L_BYTE) data = (pa & 1)?          /* byte? merge */
            (dz_csr & BMASK) | (data << 8):
            (dz_csr & ~BMASK) | data;
        if (data & CSR_CLR)                             /* clr? reset */
            dz_clear (FALSE);
        if (data & CSR_MSE)                             /* MSE? start poll */
            sim_clock_coschedule (&dz_unit[0], tmxr_poll);
        else
            dz_csr &= ~(CSR_SA | CSR_RDONE | CSR_TRDY);
        dz_csr = (dz_csr & ~CSR_RW) | (data & CSR_RW);
        break;

    case 01:                                            /* LPR */
        dz_lpr = data;
        line = LPR_GETLN (data);                        /* get line num */
        lp = &dz_ldsc[line];                            /* get line desc */
        if (dz_lpr & LPR_RCVE)                          /* rcv enb? on */
            lp->rcve = 1;
        else
            lp->rcve = 0;                               /* else line off */
        sprintf(lineconfig, "%s-%s%s%s", LPR_GETSPD(data), LPR_GETCHARSIZE(data), LPR_GETPARITY(data), LPR_GETSTOPBITS(data));
        if (!lp->serconfig || (0 != strcmp(lp->serconfig, lineconfig))) /* config changed? */
            tmxr_set_config_line (lp, lineconfig);      /* set it */
        tmxr_poll_rx (&dz_desc);                        /* poll input */
        dz_update_rcvi ();                              /* update rx intr */
        break;

    case 02:                                            /* TCR */
        if (access == L_BYTE) data = (pa & 1)?          /* byte? merge */
            (dz_tcr & BMASK) | (data << 8):
            (dz_tcr & ~BMASK) | data;
        if (dz_mctl) {                                  /* modem ctl? */
            int32 changed = data ^ dz_tcr;

            for (line = 0; line < DZ_LINES; line++) {
                if (0 == (changed & (1 << (TCR_V_DTR2 + line))))
                    continue;                           /* line unchanged skip */
                lp = &dz_ldsc[line];                    /* get line desc */
                if (data & (1 << (TCR_V_DTR2 + line)))
                    tmxr_set_get_modem_bits (lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                else
                    if (dz_auto)
                        tmxr_set_get_modem_bits (lp, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);
                }
            }
        dz_tcr = data;
        tmxr_poll_tx (&dz_desc);                        /* poll output */
        dz_update_xmti ();                              /* update int */
        break;

    case 03:                                            /* TDR */
        if (pa & 1) {                                   /* odd byte? */
            dz_tdr = (dz_tdr & BMASK) | (data << 8);    /* just save */
            break;
            }
        dz_tdr = data;
        if (dz_csr & CSR_MSE) {                         /* enabled? */
            line = CSR_GETTL (dz_csr);
            if (dz_csr & CSR_MAINT) {                   /* test mode? */
                dz_char[line] = (dz_tdr & BMASK) | RBUF_VALID;/* loop data back */
                dz_char[line] |= (line << RBUF_V_RLINE);
                if (dz_tdr & (1u << (TDR_V_TBR + line)))
                    dz_char[line] = dz_char[line] | RBUF_FRME;
                dz_csr &= ~CSR_TRDY;
                sim_debug(DBG_REG, &dz_dev, "maint char for line %d : %X\n", line, dz_char[line]);
                break;
                }
            dz_putc (line, dz_tdr);
            sim_activate (&dz_unit[1], dz_unit[1].wait);
            }
        break;
        }

SET_IRQL;
}

/* Unit input service routine

   The DZ polls to see if asynchronous activity has occurred and now
   needs to be processed.  The polling interval is controlled by the clock
   simulator, so for most environments, it is calibrated to real time.
   Typical polling intervals are 50-60 times per second.

   The simulator assumes that software enables all of the multiplexors,
   or none of them.
*/

t_stat dz_svc (UNIT *uptr)
{
int32 newln, muxln;

if (dz_csr & CSR_MSE) {                                 /* enabled? */
    newln = tmxr_poll_conn (&dz_desc);                  /* poll connect */
    if ((newln >= 0) && dz_mctl) {                      /* got a live one? */
        muxln = newln % DZ_LINES;                       /* get line in mux */
        if (muxln == 2) {
            if (dz_tcr & (1 << TCR_V_DTR2))             /* DTR set? */
                dz_msr |= (1 << MSR_V_CD2);             /* set cdet */
            else dz_msr |= (1 << MSR_V_RI2);            /* set ring */
            }
        }
    tmxr_poll_rx (&dz_desc);                            /* poll input */
    dz_update_rcvi ();                                  /* upd rcv intr */
    tmxr_poll_tx (&dz_desc);                            /* poll output */
    dz_update_xmti ();                                  /* upd xmt intr */
    if ((dz_csr & CSR_RDONE) == 0)
        sim_clock_coschedule (uptr, tmxr_poll);         /* reactivate */
    }
return SCPE_OK;
}

t_stat dz_xmt_svc (UNIT *uptr)
{
tmxr_poll_tx (&dz_desc);                                /* poll output */
dz_update_xmti ();                                      /* update int */
return SCPE_OK;
}

/* Put a character to the specified line */

void dz_putc (int32 line, uint16 data)
{
int32 c;
TMLN *lp;

switch (dz_func[line]) {
    case DZ_TMXR:
        lp = &dz_ldsc[line];                            /* get line desc */
        c = sim_tt_outcvt (data, TT_GET_MODE (dz_unit[0].flags));
        if (c >= 0)                                     /* store char */
            tmxr_putc_ln (lp, c);
        break;

    case DZ_CONSOLE:
        c = sim_tt_outcvt (data, TT_GET_MODE (dz_unit[0].flags));
        if (c >= 0)
            sim_putchar_s (c);                          /* send to console */
        break;

    case DZ_KEYBOARD:
        lk_wr ((uint8)data);                            /* send to keyboard */
        break;

    case DZ_MOUSE:
        vs_wr ((uint8)data);                            /* send to mouse */
        break;
        }
return;
}

/* Get first available character for mux, if any */

uint16 dz_getc (void)
{
uint16 ret;
uint32 i;

if (!dz_scnt)
    return 0;
ret = dz_silo[0];                                       /* first fifo element */
for (i = 1; i < dz_scnt; i++)                           /* slide down remaining entries */
    dz_silo[i-1] = dz_silo[i];
--dz_scnt;                                              /* adjust count */
sim_debug (DBG_RCV, &dz_dev, "DZ Line %d - Received: 0x%X - '%c'\n", i, ret, sim_isprint(ret&0xFF) ? ret & 0xFF : '.');
return ret;
}

/* Update receive interrupts */

void dz_update_rcvi (void)
{
int32 line, c;
TMLN *lp;

if (dz_csr & CSR_MSE) {                                 /* enabled? */
    for (line = 0; line < DZ_LINES; line++) {           /* poll lines */
        if (dz_scnt >= DZ_SILO_ALM)
            break;
        c = 0;
        if ((dz_func[line] == DZ_TMXR) && ((dz_csr & CSR_MAINT) == 0)) {
            lp = &dz_ldsc[line];                        /* get line desc */
            c = tmxr_getc_ln (lp);                      /* test for input */
            if (c & SCPE_BREAK)                         /* break? frame err */
                c = RBUF_FRME;
            if (line == 2) {
                if (dz_mctl && !lp->conn)               /* if disconn */
                    dz_msr &= ~(1 << MSR_V_CD2);        /* reset car det */
                }
            }
        else {
            switch (dz_func[line]) {
                case DZ_KEYBOARD:
                    if (lk_rd ((uint8*)&c) == SCPE_OK)  /* test for input */
                        c |= RBUF_VALID;
                    break;

                case DZ_MOUSE:
                    if (vs_rd ((uint8*)&c) == SCPE_OK)  /* test for input */
                        c |= RBUF_VALID;
                    break;

                case DZ_CONSOLE:
                    if ((c = sim_poll_kbd ()) < SCPE_KFLAG) {
                        if (SCPE_BARE_STATUS(c) == SCPE_OK) /* no char */
                            continue;
                        else
                            ABORT (c);                  /* error */
                        }
                    if (c & SCPE_BREAK) {               /* break? frame err */
                        hlt_pin = 1;
                        c = RBUF_FRME;
                        }
                    else
                        c = sim_tt_inpcvt (c, TT_GET_MODE (dz_unit[0].flags));
                    break;

                default:                                /* no action for other lines */
                    continue;
                    }
            }
        if (c) {                                        /* save in silo */
            c = (c & (RBUF_CHAR | RBUF_FRME)) | RBUF_VALID;;
            RBUF_PUTRL (c, line);                       /* add line # */
            dz_silo[dz_scnt] = (uint16)c;
            ++dz_scnt;
            }
        
        }
    }
if (dz_scnt && (dz_csr & CSR_MSE)) {                    /* input & enabled? */
    dz_csr |= CSR_RDONE;                                /* set done */
    if (dz_sae && (dz_scnt >= DZ_SILO_ALM)) {           /* alm enb & cnt hi? */
        dz_csr |= CSR_SA;                               /* set status */
        dz_sae = 0;                                     /* disable alarm */
        }
    }
else
    dz_csr &= ~CSR_RDONE;                               /* no, clear done */
if (((dz_csr & CSR_SAE)?
     (dz_csr & CSR_SA): (dz_csr & CSR_RDONE)))
    SET_INT (DZRX);                                     /* alm/done? */
else
    CLR_INT (DZRX);                                     /* no, clear int */
return;
}

/* Update transmit interrupts */

void dz_update_xmti (void)
{
int32 linemask, i, line;

linemask = dz_tcr & DZ_LMASK;                           /* enabled lines */
dz_csr &= ~CSR_TRDY;                                    /* assume not rdy */
line = CSR_GETTL (dz_csr);                              /* start at current */
for (i = 0; i < DZ_LINES; i++) {                        /* loop thru lines */
    line = (line + 1) & DZ_LNOMASK;                     /* next line */
    if ((linemask & (1 << line)) && dz_ldsc[line].xmte) {
        CSR_PUTTL (dz_csr, line);                       /* put ln in csr */
        dz_csr |= CSR_TRDY;                             /* set xmt rdy */
        break;
        }
    }
if (dz_csr & CSR_TRDY)                                  /* ready? */
    SET_INT (DZTX);
else
    CLR_INT (DZTX);                                     /* no int req */
return;
}

/* Device reset */

t_stat dz_clear (t_bool flag)
{
int32 i;

dz_csr = 0;                                             /* clear CSR */
dz_rbuf = 0;                                            /* silo empty */
dz_lpr = 0;                                             /* no params */
if (flag)                                               /* INIT? clr all */
    dz_tcr = 0;
else dz_tcr &= ~0377;                                   /* else save dtr */
dz_tdr = 0;
dz_sae = 1;                                             /* alarm on */
dz_scnt = 0;
CLR_INT (DZRX);                                         /* clear int */
CLR_INT (DZTX);
for (i = 0; i < DZ_LINES; i++) {                        /* loop thru lines */
    if (!dz_ldsc[i].conn)                               /* set xmt enb */
        dz_ldsc[i].xmte = 1;
    dz_ldsc[i].rcve = 0;                                /* clr rcv enb */
    }
return SCPE_OK;
}

t_stat dz_reset (DEVICE *dptr)
{
int32 i;

if (sys_model) {                                        /* VAXstation? */
    dz_func[0] = DZ_KEYBOARD;
    dz_func[1] = DZ_MOUSE;
    dz_func[2] = DZ_TMXR;
    dz_func[3] = DZ_TMXR;
    dz_lnorder[0] = 2;
    dz_lnorder[1] = 3;
    dz_lnorder[2] = 2;                                  /* only 2 connections */
    dz_lnorder[3] = 3;
    }
else if (DZ_L3C) {                                      /* no, MicroVAX */
    dz_func[0] = DZ_TMXR;
    dz_func[1] = DZ_TMXR;
    dz_func[2] = DZ_TMXR;
    dz_func[3] = DZ_CONSOLE;
    dz_lnorder[0] = 0;
    dz_lnorder[1] = 1;
    dz_lnorder[2] = 2;
    dz_lnorder[3] = 0;                                  /* only 3 connections */
    }
else {                                                  /* InfoServer */
    dz_func[0] = DZ_CONSOLE;
    dz_func[1] = DZ_TMXR;
    dz_func[2] = DZ_TMXR;
    dz_func[3] = DZ_TMXR;
    dz_lnorder[0] = 1;
    dz_lnorder[1] = 2;
    dz_lnorder[2] = 3;
    dz_lnorder[3] = 1;                                  /* only 3 connections */
    }
if (dz_ldsc != NULL) {
    for (i = 0; i < DZ_LINES; i++) {
        if (dz_func[i] != DZ_TMXR) {
            if (dz_ldsc[i].conn) {
                tmxr_linemsg (&dz_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dz_ldsc[i]);
                }
            tmxr_detach_ln (&dz_ldsc[i]);               /* completely reset line */
            }
        }
    }
else
    dz_desc.ldsc = dz_ldsc = (TMLN *)calloc (DZ_LINES, sizeof(*dz_ldsc));
dz_clear (TRUE);                                        /* init mux */
CLR_INT (DZRX);
CLR_INT (DZTX);
sim_cancel (&dz_unit[0]);                               /* stop poll */
for (i = 0; i < DZ_LINES; i++)
    dz_char[i] = 0;
return SCPE_OK;
}

/* Attach */

t_stat dz_attach (UNIT *uptr, CONST char *cptr)
{
int32 muxln;
t_stat r;

if (sim_switches & SWMASK ('M'))                        /* modem control? */
    tmxr_set_modem_control_passthru (&dz_desc);
r = tmxr_attach (&dz_desc, uptr, cptr);                 /* attach mux */
if (r != SCPE_OK) {                                     /* error? */
    tmxr_clear_modem_control_passthru (&dz_desc);
    return r;
    }
if (sim_switches & SWMASK ('M')) {                      /* modem control? */
    dz_mctl = 1;
    sim_printf ("Modem control activated\n");
    if (sim_switches & SWMASK ('A')) {                  /* autodisconnect? */
        dz_auto = 1;
        sim_printf ("Auto disconnect activated\n");
        }
    }

if (!dz_mctl || (0 == (dz_csr & CSR_MSE)))              /* enabled? */
    return SCPE_OK;
for (muxln = 0; muxln < DZ_LINES; muxln++) {
    if (dz_tcr & (1 << (muxln + TCR_V_DTR2))) {
        TMLN *lp = &dz_ldsc[muxln];

        tmxr_set_get_modem_bits (lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
        }
    }
return SCPE_OK;
}

/* Detach */

t_stat dz_detach (UNIT *uptr)
{
dz_mctl = dz_auto = 0;                                  /* modem ctl off */
return tmxr_detach (&dz_desc, uptr);
}

/* SET LOG processor */

t_stat dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
char gbuf[CBUFSIZE];
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, '=');
if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
    return SCPE_ARG;
ln = (int32) get_uint (gbuf, 10, dz_desc.lines, &r);
if ((r != SCPE_OK) || (ln >= dz_desc.lines))
    return SCPE_ARG;
return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
ln = (int32) get_uint (cptr, 10, dz_desc.lines, &r);
if ((r != SCPE_OK) || (ln >= dz_desc.lines))
    return SCPE_ARG;
return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i;

for (i = 0; i < dz_desc.lines; i++) {
    fprintf (st, "line %d: ", i);
    tmxr_show_log (st, NULL, i, desc);
    fprintf (st, "\n");
    }
return SCPE_OK;
}

t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DZ Terminal Multiplexer (DZ)\n\n");
fprintf (st, "The DZ is a %d line terminal multiplexor.\n", DZ_LINES);
fprintf (st, "For the MicroVAX, one of these lines is dedicated to the console and\n");
fprintf (st, "cannot be used with the Telnet multiplexer. For the VAXstation, two\n");
fprintf (st, "ports are dedicated to the keyboard and mouse.\n");
fprintf (st, "The DZ supports three character processing modes, 7P, 7B, and 8B:\n\n");
fprintf (st, "        mode    input characters        output characters\n");
fprintf (st, "        =============================================\n");
fprintf (st, "        7P      high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                        non-printing characters suppressed\n");
fprintf (st, "        7B      high-order bit cleared  high-order bit cleared\n");
fprintf (st, "        8B      no changes              no changes\n\n");
fprintf (st, "The default is 8B.\n\n");
fprintf (st, "The DZ supports logging on a per-line basis.  The command\n\n");
fprintf (st, "   sim> SET %s LOG=n=filename\n\n", dptr->name);
fprintf (st, "enables logging for the specified line(n) to the indicated file.  The command\n\n");
fprintf (st, "   sim> SET %s NOLOG=line\n\n", dptr->name);
fprintf (st, "disables logging for the specified line and closes any open log file.  Finally,\n");
fprintf (st, "the command:\n\n");
fprintf (st, "   sim> SHOW %s LOG\n\n", dptr->name);
fprintf (st, "displays logging information for all %s lines.\n\n", dptr->name);
fprintf (st, "Once the DZ is attached and the simulator is running, the DZ will listen for\n");
fprintf (st, "connections on the specified port.  It assumes that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connection remains open until disconnected by the\n");
fprintf (st, "simulated program, the Telnet client, a SET %s DISCONNECT command, or a\n", dptr->name);
fprintf (st, "DETACH %s command.\n\n", dptr->name);
fprintf (st, "Other special %s commands:\n\n", dptr->name);
fprintf (st, "   sim> SHOW %s CONNECTIONS           show current connections\n", dptr->name);
fprintf (st, "   sim> SHOW %s STATISTICS            show statistics for active connections\n", dptr->name);
fprintf (st, "   sim> SET %s DISCONNECT=linenumber  disconnects the specified line.\n\n\n", dptr->name);
fprintf (st, "All open connections are lost when the simulator shuts down or the %s is\n", dptr->name);
fprintf (st, "detached.\n");
return SCPE_OK;
}

t_stat dz_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The terminal lines perform input and output through Telnet sessions connected\n");
fprintf (st, "to a user-specified port.  The ATTACH command specifies the port to be used:\n\n");
fprintf (st, "   sim> ATTACH {-am} %s {interface:}port      set up listening port\n\n", dptr->name);
fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
fprintf (st, "other TCP/IP activities.  The optional switch -m turns on the DZ's modem\n");
fprintf (st, "controls; the optional switch -a turns on active disconnects (disconnect\n");
fprintf (st, "session if computer clears Data Terminal Ready).  Without modem control, the\n");
fprintf (st, "DZ behaves as though terminals were directly connected; disconnecting the\n");
fprintf (st, "Telnet session does not cause any operating system-visible change in line\n");
fprintf (st, "status.\n\n");
return SCPE_OK;
}

const char *dz_description (DEVICE *dptr)
{
return "DZ 4-line terminal multiplexer";
}
