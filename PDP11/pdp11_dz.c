/* pdp11_dz.c: DZ11 terminal multiplexor simulator

   Copyright (c) 2001-2008, Robert M Supnik

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

   dz           DZ11 terminal multiplexor

   29-Dec-08    RMS     Added MTAB_NC to SET LOG command (Walter Mueller)
   19-Nov-08    RMS     Revised for common TMXR show routines
   18-Jun-07    RMS     Added UNIT_IDLE flag
   29-Oct-06    RMS     Synced poll and clock
   22-Nov-05    RMS     Revised for new terminal processing routines
   07-Jul-05    RMS     Removed extraneous externs
   15-Jun-05    RMS     Revised for new autoconfigure interface
   04-Apr-04    RMS     Added per-line logging
   05-Jan-04    RMS     Revised for tmxr library changes
   19-May-03    RMS     Revised for new conditional compilation scheme
   09-May-03    RMS     Added network device flag
   22-Dec-02    RMS     Added break (framing error) support
   31-Oct-02    RMS     Added 8b support
   12-Oct-02    RMS     Added autoconfigure support
   29-Sep-02    RMS     Fixed bug in set number of lines routine
                        Added variable vector support
                        New data structures
   22-Apr-02    RMS     Updated for changes in sim_tmxr
   28-Apr-02    RMS     Fixed interrupt acknowledge, fixed SHOW DZ ADDRESS
   14-Jan-02    RMS     Added multiboard support
   30-Dec-01    RMS     Added show statistics, set disconnect
                        Removed statistics registers
   03-Dec-01    RMS     Modified for extended SET/SHOW
   09-Nov-01    RMS     Added VAX support
   20-Oct-01    RMS     Moved getchar from sim_tmxr, changed interrupt
                        logic to use tmxr_rqln
   06-Oct-01    RMS     Fixed bug in carrier detect logic
   03-Oct-01    RMS     Added support for BSD-style "ringless" modems
   27-Sep-01    RMS     Fixed bug in xmte initialization
   17-Sep-01    RMS     Added separate autodisconnect switch
   16-Sep-01    RMS     Fixed modem control bit offsets
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"
#define RANK_DZ         0                               /* no autoconfig */
#define DZ_8B_DFLT      0

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define DZ_8B_DFLT      TT_MODE_8B

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define DZ_8B_DFLT      TT_MODE_8B
#endif

#include "sim_sock.h"
#include "sim_tmxr.h"

#if !defined (DZ_MUXES)
#define DZ_MUXES        1
#endif
#if !defined (MAX_DZ_MUXES)
#define MAX_DZ_MUXES    32
#endif
#define DZ_LINES        (UNIBUS ? 8 : 4)                /* lines per DZ mux */

#if DZ_MUXES > MAX_DZ_MUXES
#error "Too many DZ multiplexers"
#endif

#define DZ_MAXMUX       (dz_desc.lines/DZ_LINES - 1)    /* maximul mux no */
#define DZ_LNOMASK      (DZ_LINES - 1)                  /* mask for lineno */
#define DZ_LMASK        ((1 << DZ_LINES) - 1)           /* mask of lines */
#define DZ_SILO_ALM     16                              /* silo alarm level */

/* DZCSR - 160100 - control/status register */

#define CSR_MAINT       0000010                         /* maint - NI */
#define CSR_CLR         0000020                         /* clear */
#define CSR_MSE         0000040                         /* master scan enb */
#define CSR_RIE         0000100                         /* rcv int enb */
#define CSR_RDONE       0000200                         /* rcv done - RO */
#define CSR_V_TLINE     8                               /* xmit line - RO */
#define CSR_TLINE       (DZ_LNOMASK << CSR_V_TLINE)
#define CSR_SAE         0010000                         /* silo alm enb */
#define CSR_SA          0020000                         /* silo alm - RO */
#define CSR_TIE         0040000                         /* xmit int enb */
#define CSR_TRDY        0100000                         /* xmit rdy - RO */
#define CSR_RW          (CSR_MSE | CSR_RIE | CSR_SAE | CSR_TIE)
#define CSR_MBZ         (0004003 | CSR_CLR | CSR_MAINT)

#define CSR_GETTL(x)    (((x) >> CSR_V_TLINE) & DZ_LNOMASK)
#define CSR_PUTTL(x,y)  x = ((x) & ~CSR_TLINE) | (((y) & DZ_LNOMASK) << CSR_V_TLINE)

BITFIELD dz_csr_bits[] = {
  BITNCF(3),                                /* not used */
  BIT(MAINT),                               /* Maint */
  BIT(CLR),                                 /* clear */
  BIT(MSE),                                 /* naster scan enable */
  BIT(RIE),                                 /* receive interrupt enable */
  BIT(RDONE),                               /* receive done */
  BITF(TLINE,3),                            /* transmit line */
  BITNCF(1),                                /* not used */
  BIT(SAE),                                 /* silo alarm enable */
  BIT(SA),                                  /* silo alarm */
  BIT(TIE),                                 /* transmit interrupt enable */
  BIT(TRDY),                                /* transmit ready */
  ENDBITS
};

/* DZRBUF - 160102 - receive buffer, read only */

#define RBUF_CHAR       0000377                         /* rcv char */
#define RBUF_V_RLINE    8                               /* rcv line */
#define RBUF_PARE       0010000                         /* parity err - NI */
#define RBUF_FRME       0020000                         /* frame err */
#define RBUF_OVRE       0040000                         /* overrun err - NI */
#define RBUF_VALID      0100000                         /* rcv valid */
#define RBUF_MBZ        0004000

BITFIELD dz_rbuf_bits[] = {
  BITFFMT(RBUF,8,"%02X"),                   /* Received Character */
  BITF(RLINE,3),                            /* receive line */
  BITNCF(1),                                /* not used */
  BIT(PARE),                                /* parity error */
  BIT(FRME),                                /* frame error */
  BIT(OVRE),                                /* overrun error */
  BIT(VALID),                               /* receive valid */
  ENDBITS
};

const char *dz_charsizes[] = {"5", "6", "7", "8"};
const char *dz_baudrates[] = {"50", "75", "110", "134.5", "150", "300", "600", "1200", 
                        "1800", "2000", "2400", "3600", "4800", "7200", "9600", "19200"};
const char *dz_parity[] = {"N", "E", "N", "O"};
const char *dz_stopbits[] = {"1", "2", "1", "1.5"};

/* DZLPR - 160102 - line parameter register, write only, word access only */

#define LPR_V_LINE      0                               /* line */
#define LPR_V_SPEED     8                               /* speed code */
#define LPR_M_SPEED     0007400                         /* speed code mask */
#define LPR_V_CHARSIZE  3                               /* char size code */
#define LPR_M_CHARSIZE  0000030                         /* char size code mask */
#define LPR_V_STOPBITS  5                               /* stop bits code */
#define LPR_V_PARENB    6                               /* parity enable */
#define LPR_V_PARODD    7                               /* parity odd */
#define LPR_GETSPD(x)   dz_baudrates[((x) & LPR_M_SPEED) >> LPR_V_SPEED]
#define LPR_GETCHARSIZE(x) dz_charsizes[((x) & LPR_M_CHARSIZE) >> LPR_V_CHARSIZE]
#define LPR_GETPARITY(x) dz_parity[(((x) >> LPR_V_PARENB) & 1) | (((x) >> (LPR_V_PARODD-1)) & 2)]
#define LPR_GETSTOPBITS(x) dz_stopbits[(((x) >> LPR_V_STOPBITS) & 1) + (((((x) & LPR_M_CHARSIZE) >> LPR_V_CHARSIZE) == 0) ? 2 : 0)]
#define LPR_LPAR        0007770                         /* line pars - NI */
#define LPR_RCVE        0010000                         /* receive enb */
#define LPR_GETLN(x)    (((x) >> LPR_V_LINE) & DZ_LNOMASK)

BITFIELD dz_lpr_bits[] = {
  BITF(LINE,3),                             /* line */
  BITFNAM(CHARSIZE,2,dz_charsizes),         /* character size */
  BIT(STOPBITS),                            /* stop bits code */
  BIT(PARENB),                              /* parity error */
  BIT(PARODD),                              /* frame error */
  BITFNAM(SPEED,4,dz_baudrates),            /* speed code */
  BITNCF(1),                                /* not used */
  BIT(RCVE),                                /* receive enable */
  ENDBITS
};

/* DZTCR - 160104 - transmission control register */

#define TCR_V_XMTE      0                               /* xmit enables */
#define TCR_V_DTR       8                               /* DTRs */

BITFIELD dz_tcr_bits[] = {
  BITFFMT(XMTE,8,%02X),                     /* Transmit enable */
  BITFFMT(DTR, 8,%02X),                     /* Data Terminal Ready */
  ENDBITS
};

/* DZMSR - 160106 - modem status register, read only */

#define MSR_V_RI        0                               /* ring indicators */
#define MSR_V_CD        8                               /* carrier detect */

BITFIELD dz_msr_bits[] = {
  BITFFMT(RI,8,%02X),                       /* ring indicators */
  BITFFMT(CD,8,%02X),                       /* carrier detects */
  ENDBITS
};

/* DZTDR - 160106 - transmit data, write only */

#define TDR_CHAR        0000377                         /* xmit char */
#define TDR_V_TBR       8                               /* xmit break - NI */

BITFIELD dz_tdr_bits[] = {
  BITFFMT(CHAR,8,%02X),                     /* xmit char */
  BITFFMT(TBR, 8,%02X),                     /* xmit break - NI */
  ENDBITS
};

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */

uint16 dz_csr[MAX_DZ_MUXES] = { 0 };                    /* csr */
uint16 dz_rbuf[MAX_DZ_MUXES] = { 0 };                   /* rcv buffer */
uint16 dz_lpr[MAX_DZ_MUXES] = { 0 };                    /* line param */
uint16 dz_tcr[MAX_DZ_MUXES] = { 0 };                    /* xmit control */
uint16 dz_msr[MAX_DZ_MUXES] = { 0 };                    /* modem status */
uint16 dz_tdr[MAX_DZ_MUXES] = { 0 };                    /* xmit data */
uint16 dz_silo[MAX_DZ_MUXES][DZ_SILO_ALM] = { 0 };      /* silo */
uint16 dz_scnt[MAX_DZ_MUXES] = { 0 };                   /* silo used */
uint8 dz_sae[MAX_DZ_MUXES] = { 0 };                     /* silo alarm enabled */
uint32 dz_rxi = 0;                                      /* rcv interrupts */
uint32 dz_txi = 0;                                      /* xmt interrupts */
int32 dz_mctl = 0;                                      /* modem ctrl enabled */
int32 dz_auto = 0;                                      /* autodiscon enabled */
TMLN *dz_ldsc = NULL;                                   /* line descriptors */
TMXR dz_desc = { 0, 0, 0, NULL };                       /* mux descriptor */

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
  {"REG",    DBG_REG, "read/write registers"},
  {"INT",    DBG_INT, "interrupt activities"},
  {"XMT",    DBG_XMT, "Transmitted Data"},
  {"RCV",    DBG_RCV, "Received Data"},
  {"RET",    DBG_RET, "Read Data"},
  {"MDM",    DBG_MDM, "Modem Signals"},
  {"CON",    DBG_CON, "connection activities"},
  {"TRC",    DBG_TRC, "trace routine calls"},
  {"ASY",    DBG_ASY, "Asynchronous Activities"},
  {0}
};

t_stat dz_rd (int32 *data, int32 PA, int32 access);
t_stat dz_wr (int32 data, int32 PA, int32 access);
int32 dz_rxinta (void);
int32 dz_txinta (void);
t_stat dz_svc (UNIT *uptr);
t_stat dz_xmt_svc (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat dz_attach (UNIT *uptr, CONST char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_clear (int32 dz, t_bool flag);
uint16 dz_getc (int32 dz);
void dz_update_rcvi (void);
void dz_update_xmti (void);
void dz_clr_rxint (int32 dz);
void dz_set_rxint (int32 dz);
void dz_clr_txint (int32 dz);
void dz_set_txint (int32 dz);
t_stat dz_show_vec (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
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

#define IOLN_DZ         010

DIB dz_dib = {
    IOBA_AUTO, IOLN_DZ * DZ_MUXES, &dz_rd, &dz_wr,
    2, IVCL (DZRX), VEC_AUTO, { &dz_rxinta, &dz_txinta },
    IOLN_DZ,
    };

UNIT dz_unit[2] = {
        { UDATA (&dz_svc, UNIT_IDLE|UNIT_ATTABLE|DZ_8B_DFLT, 0) },
        { UDATA (&dz_xmt_svc, UNIT_DIS, 0) }
    };

REG dz_reg[] = {
    { BRDATADF (CSR,   dz_csr,   DEV_RDX, 16, MAX_DZ_MUXES, "control/status register", dz_csr_bits) },
    { BRDATADF (RBUF,  dz_rbuf,  DEV_RDX, 16, MAX_DZ_MUXES, "receive buffer",          dz_rbuf_bits) },
    { BRDATADF (LPR,   dz_lpr,   DEV_RDX, 16, MAX_DZ_MUXES, "line parameter register", dz_lpr_bits) },
    { BRDATADF (TCR,   dz_tcr,   DEV_RDX, 16, MAX_DZ_MUXES, "transmission control register", dz_tcr_bits) },
    { BRDATADF (MSR,   dz_msr,   DEV_RDX, 16, MAX_DZ_MUXES, "modem status register",    dz_msr_bits) },
    { BRDATADF (TDR,   dz_tdr,   DEV_RDX, 16, MAX_DZ_MUXES, "transmit data register",   dz_tdr_bits) },
    { BRDATAD  (SAENB, dz_sae,   DEV_RDX,  1, MAX_DZ_MUXES, "silo alarm enabled") },
    { GRDATAD  (RXINT, dz_rxi,   DEV_RDX, MAX_DZ_MUXES,  0, "receive interrupts") },
    { GRDATAD  (TXINT, dz_txi,   DEV_RDX, MAX_DZ_MUXES,  0, "transmit interrupts") },
    { DRDATAD  (TIME, dz_unit[1].wait,   24,                "output character delay"), PV_LEFT },
    { FLDATAD  (MDMCTL, dz_mctl, 0,                         "modem control enabled") },
    { FLDATAD  (AUTODS, dz_auto, 0,                         "autodisconnect enabled") },
    { GRDATA   (DEVADDR, dz_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA   (DEVVEC, dz_dib.vec, DEV_RDX, 16, 0), REG_HRO },
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
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec, &dz_show_vec, (void *) &dz_desc, "Interrupt vector" },
#if !defined (VM_PDP10)
    { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
        &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dz_setnl, &tmxr_show_lines, (void *) &dz_desc, "Display number of lines" },
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
    &dz_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_MUX,
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

/* IO dispatch routines, I/O addresses 177601x0 - 177601x7 */

t_stat dz_rd (int32 *data, int32 PA, int32 access)
{
int i;
static BITFIELD* bitdefs[] = {dz_csr_bits, dz_rbuf_bits, dz_tcr_bits, dz_msr_bits};
int32 dz = ((PA - dz_dib.ba) >> 3);                     /* get mux num */

if (dz > DZ_MAXMUX)
    return SCPE_IERR;
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        *data = dz_csr[dz] = dz_csr[dz] & ~CSR_MBZ;
        break;

    case 01:                                            /* RBUF */
        dz_csr[dz] = dz_csr[dz] & ~CSR_SA;              /* clr silo alarm */
        if (dz_csr[dz] & CSR_MSE) {                     /* scanner on? */
            dz_rbuf[dz] = dz_getc (dz);                 /* get top of silo */
            if (!dz_rbuf[dz])                           /* empty? re-enable */
                dz_sae[dz] = 1;
            tmxr_poll_rx (&dz_desc);                    /* poll input */
            dz_update_rcvi ();                          /* update rx intr */
            if (dz_rbuf[dz]) {
                /* Reschedule the next poll preceisely so that the 
                   the programmed input speed is observed. */
                sim_clock_coschedule_abs (dz_unit, tmxr_poll);
                }
            }
        else {
            dz_rbuf[dz] = 0;                            /* no data */
            dz_update_rcvi ();                          /* no rx intr */
            }
        *data = dz_rbuf[dz];
        break;

    case 02:                                            /* TCR */
        *data = dz_tcr[dz];
        break;

    case 03:                                            /* MSR */
        for (i=0; i<DZ_LINES; ++i) {                    /* Gather line status bits for each line */
            int line;
            int32 modem_bits;
            TMLN *lp;
            
            line = (dz * DZ_LINES) + i;
            lp = &dz_ldsc[line];                    /* get line desc */
            tmxr_set_get_modem_bits (lp, 0, 0, &modem_bits);

            dz_msr[dz] &= ~((1 << (MSR_V_RI + i)) | (1 << (MSR_V_CD + i)));
            dz_msr[dz] |= (dz_tcr[dz] & (1 << (i + TCR_V_DTR))) ?
                          ((modem_bits&TMXR_MDM_DCD) ? (1 << (MSR_V_CD + i)) : 0) :
                          ((modem_bits&TMXR_MDM_RNG) ? (1 << (MSR_V_RI + i)) : 0);
            }
        *data = dz_msr[dz];
        break;
        }

sim_debug(DBG_REG, &dz_dev, "dz_rd(PA=0x%08X [%s], access=%d, data=0x%X) ", PA, dz_rd_regs[(PA >> 1) & 03], access, *data);
sim_debug_bits(DBG_REG, &dz_dev, bitdefs[(PA >> 1) & 03], (uint32)(*data), (uint32)(*data), TRUE);

return SCPE_OK;
}

t_stat dz_wr (int32 ldata, int32 PA, int32 access)
{
int32 dz = ((PA - dz_dib.ba) >> 3);                     /* get mux num */
static BITFIELD* bitdefs[] = {dz_csr_bits, dz_lpr_bits, dz_tcr_bits, dz_tdr_bits};
int32 i, c, line;
char lineconfig[16];
TMLN *lp;
uint16 data = (uint16)ldata;

if (dz > DZ_MAXMUX)
    return SCPE_IERR;

sim_debug(DBG_REG, &dz_dev, "dz_wr(PA=0x%08X [%s], access=%d, data=0x%X) ", PA, dz_wr_regs[(PA >> 1) & 03], access, data);
sim_debug_bits(DBG_REG, &dz_dev, bitdefs[(PA >> 1) & 03], (uint32)((PA & 1) ? data<<8 : data), (uint32)((PA & 1) ? data<<8 : data), TRUE);

switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

    case 00:                                            /* CSR */
        if (access == WRITEB)
            data = (PA & 1)?                            /* byte? merge */
                    (dz_csr[dz] & 0377) | (data << 8):
                    (dz_csr[dz] & ~0377) | data;
        if (data & CSR_CLR)                             /* clr? reset */
            dz_clear (dz, FALSE);
        if (data & CSR_MSE)                             /* MSE? start poll */
            sim_clock_coschedule (dz_unit, tmxr_poll);
        else
            dz_csr[dz] &= ~(CSR_SA | CSR_RDONE | CSR_TRDY);
        if ((data & CSR_RIE) == 0)                      /* RIE = 0? */
            dz_clr_rxint (dz);
        else
            if (((dz_csr[dz] & CSR_IE) == 0) &&        /* RIE 0->1? */
                ((dz_csr[dz] & CSR_SAE)?
                (dz_csr[dz] & CSR_SA): (dz_csr[dz] & CSR_RDONE)))
                dz_set_rxint (dz);
        if ((data & CSR_TIE) == 0)                      /* TIE = 0? */
            dz_clr_txint (dz);
        else if (((dz_csr[dz] & CSR_TIE) == 0) && (dz_csr[dz] & CSR_TRDY))
            dz_set_txint (dz);
        dz_csr[dz] = (dz_csr[dz] & ~CSR_RW) | (data & CSR_RW);
        break;

    case 01:                                            /* LPR */
        dz_lpr[dz] = data;
        line = (dz * DZ_LINES) + LPR_GETLN (data);      /* get line num */
        lp = &dz_ldsc[line];                            /* get line desc */
        if (dz_lpr[dz] & LPR_RCVE)                      /* rcv enb? on */
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
        if (access == WRITEB)
            data = (PA & 1)?                            /* byte? merge */
                    (dz_tcr[dz] & 0377) | (data << 8):
                    (dz_tcr[dz] & ~0377) | data;
        if (dz_mctl && 
            ((access != WRITEB) || (PA & 1))) {         /* modem ctl (DTR)? */
            int32 changed = data ^ dz_tcr[dz];

            for (i = 0; i < DZ_LINES; i++) {
                if (0 == (changed & (1 << (TCR_V_DTR + i))))
                    continue;                           /* line unchanged skip */
                line = (dz * DZ_LINES) + i;             /* get line num */
                lp = &dz_ldsc[line];                    /* get line desc */
                if (data & (1 << (TCR_V_DTR + i))) {    /* just asserted, so turn on */
                    tmxr_set_get_modem_bits (lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
                    }
                else                                    /* just deasserted, so turn off */
                    if (dz_auto)
                        tmxr_set_get_modem_bits (lp, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);
                }
            }
        dz_tcr[dz] = data;
        tmxr_poll_tx (&dz_desc);                        /* poll output */
        dz_update_xmti ();                              /* update int */
        break;

    case 03:                                            /* TDR */
        if (PA & 1) {                                   /* odd byte? */
            dz_tdr[dz] = (dz_tdr[dz] & 0377) | (data << 8);     /* just save */
            break;
            }
        dz_tdr[dz] = data;
        if (dz_csr[dz] & CSR_MSE) {                     /* enabled? */
            line = (dz * DZ_LINES) + CSR_GETTL (dz_csr[dz]);
            lp = &dz_ldsc[line];                        /* get line desc */
            c = sim_tt_outcvt (dz_tdr[dz], TT_GET_MODE (dz_unit[0].flags));
            if (c >= 0) {                               /* store char */
                tmxr_putc_ln (lp, c);
                dz_update_xmti ();
                sim_activate_after_abs (&dz_unit[1], lp->txdeltausecs);
                }
            }
        break;
        }

return SCPE_OK;
}

/* Unit Input service routine

   The DZ11 polls to see if asynchronous activity has occurred and now
   needs to be processed.  The polling interval is controlled by the clock
   simulator, so for most environments, it is calibrated to real time.
   Typical polling intervals are 50-60 times per second.

   The simulator assumes that software enables all of the multiplexors,
   or none of them.
*/

t_stat dz_svc (UNIT *uptr)
{
int32 dz, t, newln, muxln;

sim_debug(DBG_TRC, find_dev_from_unit(uptr), "dz_svc()\n");
for (dz = t = 0; dz < dz_desc.lines/DZ_LINES; dz++)     /* check enabled */
    t = t | (dz_csr[dz] & CSR_MSE);
if (t) {                                                /* any enabled? */
    newln = tmxr_poll_conn (&dz_desc);                  /* poll connect */
    if ((newln >= 0) && dz_mctl) {                      /* got a live one? */
        dz = newln / DZ_LINES;                          /* get mux num */
        muxln = newln % DZ_LINES;                       /* get line in mux */
        if (dz_tcr[dz] & (1 << (muxln + TCR_V_DTR)))    /* DTR set? */
            dz_msr[dz] |= (1 << (muxln + MSR_V_CD));    /* set cdet */
        else dz_msr[dz] |= (1 << (muxln + MSR_V_RI));   /* set ring */
        }
    tmxr_poll_rx (&dz_desc);                            /* poll input */
    dz_update_rcvi ();                                  /* upd rcv intr */
    tmxr_poll_tx (&dz_desc);                            /* poll output */
    dz_update_xmti ();                                  /* upd xmt intr */
    for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {
        if (dz_csr[dz] & CSR_RDONE)
            break;
        }
    if (dz == dz_desc.lines/DZ_LINES)                   /* All idle? */
        sim_clock_coschedule (uptr, tmxr_poll);         /* reactivate */
    }
return SCPE_OK;
}

t_stat dz_xmt_svc (UNIT *uptr)
{
tmxr_poll_tx (&dz_desc);                                /* poll output */
dz_update_xmti ();                                      /* update int */
sim_activate_after (uptr, 500000);                      /* reactivate occasionally */
return SCPE_OK;
}

/* Get first available character for mux, if any */

uint16 dz_getc (int32 dz)
{
uint16 ret;
uint32 i;

if (!dz_scnt[dz])
    return 0;
ret = dz_silo[dz][0];                       /* first fifo element */
for (i=1; i < dz_scnt[dz]; i++)             /* slide down remaining entries */
    dz_silo[dz][i-1] = dz_silo[dz][i];
--dz_scnt[dz];                              /* adjust count */
sim_debug(DBG_RCV, &dz_dev, "DZ Device %d - Received: 0x%X - '%c'\n", dz, ret, sim_isprint(ret&0xFF) ? ret & 0xFF : '.');
return ret;
}

/* Update receive interrupts */

void dz_update_rcvi (void)
{
int32 i, dz, c;
TMLN *lp;

for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {       /* loop thru muxes */
    if (dz_csr[dz] & CSR_MSE) {                         /* enabled? */
        for (i = 0; i < DZ_LINES; i++) {                /* poll lines */
            if (dz_scnt[dz] >= DZ_SILO_ALM)
                break;
            lp = &dz_ldsc[(dz * DZ_LINES) + i];         /* get line desc */
            c = tmxr_getc_ln (lp);                      /* test for input */
            if (c & SCPE_BREAK)                         /* break? frame err */
                c = RBUF_FRME;
            if (c) {                                    /* save in silo */
                c = (c & (RBUF_CHAR | RBUF_FRME)) | RBUF_VALID | (i << RBUF_V_RLINE);
                dz_silo[dz][dz_scnt[dz]] = (uint16)c;
                ++dz_scnt[dz];
                }
            if (dz_mctl && !lp->conn)                   /* if disconn */
                dz_msr[dz] &= ~(1 << (i + MSR_V_CD));   /* reset car det */
            }
        }
    if (dz_scnt[dz] && (dz_csr[dz] & CSR_MSE)) {        /* input & enabled? */
        dz_csr[dz] |= CSR_RDONE;                        /* set done */
        if (dz_sae[dz] && (dz_scnt[dz] >= DZ_SILO_ALM)) {/* alm enb & cnt hi? */
            dz_csr[dz] |= CSR_SA;                       /* set status */
            dz_sae[dz] = 0;                             /* disable alarm */
            }
        }
    else
        dz_csr[dz] &= ~CSR_RDONE;                       /* no, clear done */
    if ((dz_csr[dz] & CSR_RIE) &&                       /* int enable */
        ((dz_csr[dz] & CSR_SAE)?
         (dz_csr[dz] & CSR_SA): (dz_csr[dz] & CSR_RDONE)))
        dz_set_rxint (dz);                              /* and alm/done? */
    else
        dz_clr_rxint (dz);                              /* no, clear int */
    }
return;
}

/* Update transmit interrupts */

void dz_update_xmti (void)
{
int32 dz, linemask, i, j, line;

for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {       /* loop thru muxes */
    linemask = dz_tcr[dz] & DZ_LMASK;                   /* enabled lines */
    dz_csr[dz] &= ~CSR_TRDY;                            /* assume not rdy */
    j = CSR_GETTL (dz_csr[dz]);                         /* start at current */
    for (i = 0; i < DZ_LINES; i++) {                    /* loop thru lines */
        j = (j + 1) & DZ_LNOMASK;                       /* next line */
        line = (dz * DZ_LINES) + j;                     /* get line num */
        if ((linemask & (1 << j)) && tmxr_txdone_ln (&dz_ldsc[line])) {
            CSR_PUTTL (dz_csr[dz], j);                  /* put ln in csr */
            dz_csr[dz] |= CSR_TRDY;                     /* set xmt rdy */
            break;
            }
        }
    if ((dz_csr[dz] & CSR_TIE) && (dz_csr[dz] & CSR_TRDY)) /* ready plus int? */
        dz_set_txint (dz);
    else
        dz_clr_txint (dz);                              /* no int req */
    }
return;
}

/* Interrupt routines */

void dz_clr_rxint (int32 dz)
{
if (dz_rxi & (1 << dz))
    sim_debug(DBG_INT, &dz_dev, "dz_clr_rxint(dz=%d, rxi=0x%X)\n", dz, dz_rxi);
dz_rxi = dz_rxi & ~(1 << dz);                           /* clr mux rcv int */
if (dz_rxi == 0)                                        /* all clr? */
    CLR_INT (DZRX);
else SET_INT (DZRX);                                    /* no, set intr */
return;
}

void dz_set_rxint (int32 dz)
{
dz_rxi = dz_rxi | (1 << dz);                            /* set mux rcv int */
SET_INT (DZRX);                                         /* set master intr */
sim_debug(DBG_INT, &dz_dev, "dz_set_rxint(dz=%d)\n", dz);
return;
}

int32 dz_rxinta (void)
{
int32 dz;

for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {       /* find 1st mux */
    if (dz_rxi & (1 << dz)) {
        sim_debug(DBG_INT, &dz_dev, "dz_rzinta(dz=%d)\n", dz);
        dz_clr_rxint (dz);                              /* clear intr */
        return (dz_dib.vec + (dz * 010));               /* return vector */
        }
    }
return 0;
}

void dz_clr_txint (int32 dz)
{
dz_txi = dz_txi & ~(1 << dz);                           /* clr mux xmt int */
if (dz_txi == 0)                                        /* all clr? */
    CLR_INT (DZTX);
else SET_INT (DZTX);                                    /* no, set intr */
return;
}

void dz_set_txint (int32 dz)
{
dz_txi = dz_txi | (1 << dz);                            /* set mux xmt int */
SET_INT (DZTX);                                         /* set master intr */
sim_debug(DBG_INT, &dz_dev, "dz_set_txint(dz=%d)\n", dz);
return;
}

int32 dz_txinta (void)
{
int32 dz;

for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {       /* find 1st mux */
    if (dz_txi & (1 << dz)) {
        sim_debug(DBG_INT, &dz_dev, "dz_txinta(dz=%d)\n", dz);
        dz_clr_txint (dz);                              /* clear intr */
        return (dz_dib.vec + 4 + (dz * 010));           /* return vector */
        }
    }
return 0;
}

/* Device reset */

t_stat dz_clear (int32 dz, t_bool flag)
{
int32 i, line;

sim_debug(DBG_TRC, &dz_dev, "dz_clear(dz=%d,flag=%d)\n", dz, flag);

dz_csr[dz] = 0;                                         /* clear CSR */
dz_rbuf[dz] = 0;                                        /* silo empty */
dz_scnt[dz] = 0;
dz_lpr[dz] = 0;                                         /* no params */
if (flag)                                               /* INIT? clr all */
    dz_tcr[dz] = 0;
else dz_tcr[dz] &= ~0377;                               /* else save dtr */
dz_tdr[dz] = 0;
dz_sae[dz] = 1;                                         /* alarm on */
dz_clr_rxint (dz);                                      /* clear int */
dz_clr_txint (dz);
for (i = 0; i < DZ_LINES; i++) {                        /* loop thru lines */
    line = (dz * DZ_LINES) + i;
    if (!dz_ldsc[line].conn)                            /* set xmt enb */
        dz_ldsc[line].xmte = 1;
    dz_ldsc[line].rcve = 0;                             /* clr rcv enb */
    }
return SCPE_OK;
}

t_stat dz_reset (DEVICE *dptr)
{
int32 i, ndev;

sim_debug(DBG_TRC, dptr, "dz_reset()\n");

if (dz_ldsc == NULL) {
    dz_desc.lines = DZ_MUXES * DZ_LINES;
    dz_desc.ldsc = dz_ldsc = (TMLN *)calloc (dz_desc.lines, sizeof(*dz_ldsc));
    sim_set_uname (&dz_unit[0], "DZ-RCV-CON");
    sim_set_uname (&dz_unit[1], "DZ-XMT");
    }
if ((dz_desc.lines % DZ_LINES) != 0) {      /* Transition from Qbus to Unibus device */
    int32 newln = DZ_LINES * (1 + (dz_desc.lines / DZ_LINES));

    dz_desc.ldsc = dz_ldsc = (TMLN *)realloc(dz_ldsc, newln*sizeof(*dz_ldsc));
    memset (dz_ldsc + dz_desc.lines, 0, sizeof(*dz_ldsc)*(newln-dz_desc.lines));
    dz_desc.lines = newln;
    }
tmxr_set_port_speed_control (&dz_desc);
for (i = 0; i < dz_desc.lines/DZ_LINES; i++)            /* init muxes */
    dz_clear (i, TRUE);
dz_rxi = dz_txi = 0;                                    /* clr master int */
CLR_INT (DZRX);
CLR_INT (DZTX);
sim_cancel (dz_unit);                                   /* stop poll */
ndev = ((dptr->flags & DEV_DIS)? 0: (dz_desc.lines / DZ_LINES));
dz_dib.lnt = ndev * IOLN_DZ;                            /* set length */
return auto_config (dptr->name, ndev);                  /* auto config */
}

/* Attach */

t_stat dz_attach (UNIT *uptr, CONST char *cptr)
{
int32 dz, muxln, ln;
t_stat r;

if ((sim_switches & SWMASK ('M')) || dz_mctl)           /* modem control? */
    tmxr_set_modem_control_passthru (&dz_desc);
for (ln = 0; ln < dz_desc.lines; ln++)
    tmxr_set_line_output_unit (&dz_desc, ln, &dz_unit[1]);
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

for (dz = 0; dz < dz_desc.lines/DZ_LINES; dz++) {
    if (!dz_mctl || (0 == (dz_csr[dz] & CSR_MSE)))      /* enabled? */
        continue;
    for (muxln = 0; muxln < DZ_LINES; muxln++) {
        if (dz_tcr[dz] & (1 << (muxln + TCR_V_DTR))) {
            TMLN *lp = &dz_ldsc[(dz * DZ_LINES) + muxln];

            tmxr_set_get_modem_bits (lp, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
            }
        }
    }
return SCPE_OK;
}

/* Detach */

t_stat dz_detach (UNIT *uptr)
{
t_stat r = tmxr_detach (&dz_desc, uptr);

dz_mctl = dz_auto = 0;                                  /* modem ctl off */
tmxr_clear_modem_control_passthru (&dz_desc);
return r;
}

t_stat dz_show_vec (FILE *st, UNIT *uptr, int32 arg, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;

return show_vec (st, uptr, ((mp->lines * 2) / DZ_LINES), desc);
}


/* SET LINES processor */

t_stat dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = (int32) get_uint (cptr, 10, (MAX_DZ_MUXES * DZ_LINES), &r);
if ((r != SCPE_OK) || (newln == dz_desc.lines))
    return r;
if ((newln == 0) || (newln % DZ_LINES))
    return SCPE_ARG;
if (newln < dz_desc.lines) {
    for (i = newln, t = 0; i < dz_desc.lines; i++)
        t = t | dz_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < dz_desc.lines; i++) {
        if (dz_ldsc[i].conn) {
            tmxr_linemsg (&dz_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_send_buffered_data (&dz_ldsc[i]);
            }
        tmxr_detach_ln (&dz_ldsc[i]);                   /* completely reset line */
        if ((i % DZ_LINES) == (DZ_LINES - 1))
            dz_clear (i / DZ_LINES, TRUE);              /* reset mux */
        }
    }
dz_dib.lnt = (newln / DZ_LINES) * IOLN_DZ;              /* set length */
dz_desc.ldsc = dz_ldsc = (TMLN *)realloc(dz_ldsc, newln*sizeof(*dz_ldsc));
if (dz_desc.lines < newln)
    memset (dz_ldsc + dz_desc.lines, 0, sizeof(*dz_ldsc)*(newln-dz_desc.lines));
dz_desc.lines = newln;
return dz_reset (&dz_dev);                              /* setup lines and auto config */
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
const char *devtype = (UNIBUS) ? "DZ11" : "DZV11";

fprintf (st, "%s Terminal Multiplexer (DZ)\n\n", devtype);
fprintf (st, "The %s is a %d line terminal multiplexor.  Up to %d %s's (%d lines) are\n", devtype, DZ_LINES, MAX_DZ_MUXES, devtype, DZ_LINES*MAX_DZ_MUXES);
fprintf (st, "supported.  The default number of lines is %d.  The number of lines can\n", DZ_LINES*DZ_MUXES);
fprintf (st, "be changed with the command\n\n");
fprintf (st, "   sim> SET %s LINES=n            set line count to n\n\n", dptr->name);
fprintf (st, "The line count must be a multiple of %d, with a maximum of %d.\n\n", DZ_LINES, DZ_LINES*MAX_DZ_MUXES);
fprintf (st, "The %s supports three character processing modes, 7P, 7B, and 8B:\n\n", devtype);
fprintf (st, "  mode    input characters    output characters\n");
fprintf (st, "  =============================================\n");
fprintf (st, "  7P  high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                  non-printing characters suppressed\n");
fprintf (st, "  7B  high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B  no changes      no changes\n\n");
fprintf (st, "The default is 8B.\n\n");
fprintf (st, "The %s supports logging on a per-line basis.  The command\n\n", devtype);
fprintf (st, "   sim> SET %s LOG=n=filename\n\n", dptr->name);
fprintf (st, "enables logging for the specified line(n) to the indicated file.  The command\n\n");
fprintf (st, "   sim> SET %s NOLOG=line\n\n", dptr->name);
fprintf (st, "disables logging for the specified line and closes any open log file.  Finally,\n");
fprintf (st, "the command:\n\n");
fprintf (st, "   sim> SHOW %s LOG\n\n", dptr->name);
fprintf (st, "displays logging information for all %s lines.\n\n", dptr->name);
fprintf (st, "Once the %s is attached and the simulator is running, the %s will listen for\n", devtype, devtype);
fprintf (st, "connections on the specified port.  It assumes that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connection remains open until disconnected by the\n");
fprintf (st, "simulated program, the Telnet client, a SET %s DISCONNECT command, or a\n", dptr->name);
fprintf (st, "DETACH %s command.\n\n", dptr->name);
fprintf (st, "Other special %s commands:\n\n", dptr->name);
fprintf (st, "   sim> SHOW %s CONNECTIONS           show current connections\n", dptr->name);
fprintf (st, "   sim> SHOW %s STATISTICS            show statistics for active connections\n", dptr->name);
fprintf (st, "   sim> SET %s DISCONNECT=linenumber  disconnects the specified line.\n\n\n", dptr->name);
fprintf (st, "All open connections are lost when the simulator shuts down or the %s is\n", dptr->name);
fprintf (st, "detached.\n\n");
dz_help_attach (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

t_stat dz_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char *devtype = (UNIBUS) ? "DZ11" : "DZV11";

tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The terminal lines perform input and output through Telnet sessions connected\n");
fprintf (st, "to a user-specified port.  The ATTACH command specifies the port to be used:\n\n");
fprintf (st, "   sim> ATTACH {-am} %s {interface:}port      set up listening port\n\n", dptr->name);
fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
fprintf (st, "other TCP/IP activities.  The optional switch -m turns on the %s's modem\n", devtype);
fprintf (st, "controls; the optional switch -a turns on active disconnects (disconnect\n");
fprintf (st, "session if computer clears Data Terminal Ready).  Without modem control, the\n");
fprintf (st, "%s behaves as though terminals were directly connected; disconnecting the\n", devtype);
fprintf (st, "Telnet session does not cause any operating system-visible change in line\n");
fprintf (st, "status.\n\n");
return SCPE_OK;
}

const char *dz_description (DEVICE *dptr)
{
return (UNIBUS) ? "DZ11 8-line terminal multiplexer" : "DZV11 4-line terminal multiplexer";
}
