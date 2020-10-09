/* pdp11_vh.c: DHQ11 asynchronous terminal multiplexor simulator

   Copyright (c) 2004-2012, John A. Dundas III
   Portions derived from work by Robert M Supnik

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the Author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the Author.

   vh           DHQ11 asynch multiplexor for SIMH

   02-Jun-11    MP      Added debugging support to trace register, interrupt 
                        and data traffic (SET VH DEBUG[=REG;INT;XMT;RCV])
                        Added SET LOG and SET NOLOG support for logging mux
                        traffic
                        Fixed SET VH LINES=n to correctly adjust the number
                        of lines available to be 8, 16, 24, or 32.
                        Fixed performance issue avoiding redundant polling
   03-Jan-10    JAD     Eliminate gcc warnings
   24-Nov-08    JDB     Removed tmxr_send_buffered_data declaration (now in sim_tmxr.h)
   19-Nov-08    RMS     Revised for common TMXR show routines
   18-Jun-07    RMS     Added UNIT_IDLE flag
   29-Oct-06    RMS     Synced poll and clock
   07-Jul-05    RMS     Removed extraneous externs
   15-Jun-05    RMS     Revised for new autoconfigure interface
                        Fixed bug in vector display routine
   12-Jun-04    RMS     Repair MS2SIMH macro to avoid divide by 0 bug
   08-Jun-04    JAD     Repair vh_dev initialization; remove unused
                        variables, cast to avoid conversion confusion
   07-Jun-04    JAD     Complete function prototypes of forward declarations.
                        Repair broken prototypes of vh_rd() and vh_wr()
                        Explicitly size integer declarations
   4-Jun-04     JAD     Preliminary code: If operating in a PDP-11 Unibus
                        environment, force DHU mode
   29-May-04    JAD     Make certain RX.TIMER is within allowable range
   25-May-04    JAD     All time-based operations are scaled by tmxr_poll units
   23-May-04    JAD     Change to fifo_get() and dq_tx_report() to avoid
                        gratuitous stack manipulation
   20-May-04    JAD     Made modem control and auto-hangup unit flags
   19-May-04    JAD     Fix problem with modem status where the line number
                        was not being included
   12-May-04    JAD     Revised for updated tmxr interfaces
   28-Jan-04    JAD     Original creation and testing

I/O Page Registers

CSR 17 760 440 (float)

Vector: 300 (float)

Priority:   BR4

Rank:       32

*/
/* MANY constants needed! */

#if defined (VM_VAX)
#include "vax_defs.h"
#endif

#if defined (VM_PDP11)
#include "pdp11_defs.h"
#endif

#include "sim_tmxr.h"

/* imports from pdp11_stddev.c: */
extern int32    tmxr_poll, clk_tps;
/* convert ms to SIMH time units based on tmxr_poll polls per second */
#define MS2SIMH(ms) (((ms) * clk_tps) / 1000)

#ifndef VH_MUXES
#define VH_MUXES    (4)
#endif
#define VH_MAXMUX  (vh_desc.lines/VH_LINES - 1)

#if defined(VM_VAX)
#if VEC_QBUS
#define VH_LINES    (8)
#else
#define VH_LINES    (16)
#endif
#else
#define VH_LINES    (UNIBUS?16:8)
#endif
#define VH_LINES_ALLOC 16

#define UNIT_V_MODEDHU  (UNIT_V_UF + 0)
#define UNIT_V_FASTDMA  (UNIT_V_UF + 1)
#define UNIT_V_MODEM    (UNIT_V_UF + 2)
#define UNIT_V_HANGUP   (UNIT_V_UF + 3)
#define UNIT_MODEDHU    (1 << UNIT_V_MODEDHU)
#define UNIT_FASTDMA    (1 << UNIT_V_FASTDMA)
#define UNIT_MODEM      (1 << UNIT_V_MODEM)
#define UNIT_HANGUP     (1 << UNIT_V_HANGUP)

/* VHCSR - 160440 - Control and Status Register */

#define CSR_M_IND_ADDR      (017)
#define CSR_SKIP        (1 << 4)
#define CSR_MASTER_RESET    (1 << 5)
#define CSR_RXIE        (1 << 6)
#define CSR_RX_DATA_AVAIL   (1 << 7)
#define CSR_M_TX_LINE       (017)
#define CSR_V_TX_LINE       (8)
#define CSR_TX_DMA_ERR      (1 << 12)
#define CSR_DIAG_FAIL       (1 << 13)
#define CSR_TXIE        (1 << 14)
#define CSR_TX_ACTION       (1 << 15)
#define CSR_GETCHAN(x)  ((x) & CSR_M_IND_ADDR)
#define CSR_RW          \
    (CSR_TXIE|CSR_RXIE|CSR_SKIP|CSR_M_IND_ADDR|CSR_MASTER_RESET)
#define RESET_ABORT     (052525)

BITFIELD vh_csr_bits[] = {
  BITF(IND_ADDR,4),                         /* indirect address */
  BIT(SKIP),                                /* Skip */
  BIT(MASTER_RESET),                        /* Master Reset */
  BIT(RXIE),                                /* Receive Interrupt Enable */
  BIT(RX_DATA_AVAIL),                       /* Receive Data Available */
  BITF(TX_LINE,4),                          /* Transmit Line */
  BIT(TX_DMA_ERR),                          /* Transmit DMA Error */
  BIT(DIAG_FAIL),                           /* Diagnostic Fail */
  BIT(TXIE),                                /* Transmit Interrupt Enable */
  BIT(TX_ACTION),                           /* Transmit Action */
  ENDBITS
};


/* Receive Buffer (RBUF) */

#define FIFO_SIZE       (256)
#define FIFO_ALARM      (191)
#define FIFO_HALF       (FIFO_SIZE / 2)
#define RBUF_M_RX_CHAR      (0377)
#define RBUF_M_RX_LINE      (07)
#define RBUF_V_RX_LINE      (8)
#define RBUF_PARITY_ERR     (1 << 12)
#define RBUF_FRAME_ERR      (1 << 13)
#define RBUF_OVERRUN_ERR    (1 << 14)
#define RBUF_DATA_VALID     (1 << 15)
#define RBUF_GETLINE(x)     (((x) >> RBUF_V_RX_LINE) & RBUF_M_RX_LINE)
#define RBUF_PUTLINE(x)     ((x) << RBUF_V_RX_LINE)
#define RBUF_DIAG       \
    (RBUF_PARITY_ERR|RBUF_FRAME_ERR|RBUF_OVERRUN_ERR)
#define XON         (021)
#define XOFF            (023)

BITFIELD vh_rbuf_bits[] = {
  BITF(RX_CHAR,8),                          /* Receive Character */
  BITF(RX_LINE,4),                          /* Receive Line */
  BIT(PARITY_ERR),                          /* Parity Error */
  BIT(FRAME_ERR),                           /* Frame Error */
  BIT(OVERRUN_ERR),                         /* Overrun Error */
  BIT(DATA_VALID),                          /* Data Valid */
  ENDBITS
};

/* Transmit Character Register (TXCHAR) */

#define TXCHAR_M_CHAR       (0377)
#define TXCHAR_TX_DATA_VALID    (1 << 15)

BITFIELD vh_txchar_bits[] = {
  BITF(TX_CHAR,8),                          /* Receive Timer */
  BITNCF(7),                                /* Not Used */
  BIT(DATA_VALID),                          /* Data Valid */
  ENDBITS
};

/* Receive Timer Register (RXTIMER) */

#define RXTIMER_M_RX_TIMER  (0377)

BITFIELD vh_rxtimer_bits[] = {
  BITF(RX_TIMER,8),                         /* Receive Timer */
  BITNCF(8),                                /* Not Used */
  ENDBITS
};


/* Line-Parameter Register (LPR) */

#define LPR_DISAB_XRPT      (1 << 0)    /* not impl. in real DHU */
#define LPR_V_DIAG      (1)
#define LPR_M_DIAG      (03)
#define LPR_V_CHAR_LGTH     (3)
#define LPR_M_CHAR_LGTH     (03)
#define LPR_V_PARITY_ENAB   (5)
#define LPR_PARITY_ENAB     (1 << LPR_V_PARITY_ENAB)
#define LPR_V_EVEN_PARITY   (6)
#define LPR_EVEN_PARITY     (1 << LPR_V_EVEN_PARITY)
#define LPR_V_STOP_CODE     (7)
#define LPR_STOP_CODE       (1 << LPR_V_STOP_CODE)
#define LPR_V_RX_SPEED      (8)
#define LPR_M_RX_SPEED      (017)
#define LPR_V_TX_SPEED      (12)
#define LPR_M_TX_SPEED      (017)

#define RATE_50         (0)
#define RATE_75         (1)
#define RATE_110        (2)
#define RATE_134        (3)
#define RATE_150        (4)
#define RATE_300        (5)
#define RATE_600        (6)
#define RATE_1200       (7)
#define RATE_1800       (8)
#define RATE_2000       (9)
#define RATE_2400       (10)
#define RATE_4800       (11)
#define RATE_7200       (12)
#define RATE_9600       (13)
#define RATE_19200      (14)
#define RATE_38400      (15)

static const char *vh_charsizes[] = {"5", "6", "7", "8"};
static const char *vh_baudrates[] = {"50", "75", "110", "134.5", "150", "300", "600", "1200", 
                        "1800", "2000", "2400", "4800", "7200", "9600", "19200", "38400"};
static const char *vh_parity[] = {"N", "N", "E", "O"};
static const char *vh_stopbits[] = {"1", "2", "1", "1.5"};

BITFIELD vh_lpr_bits[] = {
  BITNC,                                    /* Unused */
  BITF(DIAG,2),                             /* Diagnostic Code */
  BITFNAM(CHAR_LGTH,2,vh_charsizes),        /* Character Length */
  BIT(PARITY_ENAB),                         /* Parity Enable */
  BIT(EVEN_PARITY),                         /* Even Parity */
  BIT(STOP_CODE),                           /* Stop Bits Code */
  BITFNAM(RX_SPEED,4,vh_baudrates),         /* Receive Speed */
  BITFNAM(TX_SPEED,4,vh_baudrates),         /* Transmit Speed */
  ENDBITS
};

#define LPR_GETSPD(x)   vh_baudrates[((x) >> LPR_V_RX_SPEED) & LPR_M_RX_SPEED]
#define LPR_GETCHARSIZE(x) vh_charsizes[((x) >> LPR_V_CHAR_LGTH) & LPR_M_CHAR_LGTH]
#define LPR_GETPARITY(x) vh_parity[(((x) >> LPR_V_PARITY_ENAB) & 1) | (((x) >> (LPR_V_EVEN_PARITY-1)) & 2)]
#define LPR_GETSTOPBITS(x) vh_stopbits[(((x) >> LPR_V_STOP_CODE) & 1) + (((((x) >> LPR_V_CHAR_LGTH) & LPR_M_CHAR_LGTH) == 0) ? 2 : 0)]

/* Line-Status Register (STAT) */

#define STAT_DHUID      (1 << 8)    /* mode: 0=DHV, 1=DHU */
#define STAT_MDL        (1 << 9)    /* always 0, has modem support */
#define STAT_CTS        (1 << 11)   /* CTS from modem */
#define STAT_DCD        (1 << 12)   /* DCD from modem */
#define STAT_RI         (1 << 13)   /* RI from modem */
#define STAT_DSR        (1 << 15)   /* DSR from modem */

BITFIELD vh_stat_bits[] = {
  BITNCF(8),                                /* Not Used */
  BIT(DHUID),                               /* DHU/DHV Id */
  BIT(MDL),                                 /* Even Parity */
  BITNC,                                    /* Not Used */
  BIT(CTS),                                 /* CTS from modem */
  BIT(DCD),                                 /* DCD from modem */
  BIT(RI),                                  /* RI from modem */
  BITNC,                                    /* Not Used */
  BIT(DSR),                                 /* DSR from modem */
  ENDBITS
};

/* FIFO Size Register (FIFOSIZE) */

#define FIFOSIZE_M_SIZE     (0377)

BITFIELD vh_fifosize_bits[] = {
  BITF(FIFOSIZE,8),                         /* FIFO Size */
  BITNCF(8),                                /* Not Used */
  ENDBITS
};

/* FIFO Data Register (FIFODATA) */

#define FIFODATA_W0     (0377)
#define FIFODATA_V_W1       (8)
#define FIFODATA_M_W1       (0377)

BITFIELD vh_fifodata_bits[] = {
  BITF(W0,8),                               /* Low Byte Character */
  BITF(W1,8),                               /* High Byte Character */
  ENDBITS
};

/* Line-Control Register (LNCTRL) */

#define LNCTRL_TX_ABORT     (1 << 0)
#define LNCTRL_IAUTO        (1 << 1)
#define LNCTRL_RX_ENA       (1 << 2)
#define LNCTRL_BREAK        (1 << 3)
#define LNCTRL_OAUTO        (1 << 4)
#define LNCTRL_FORCE_XOFF   (1 << 5)
#define LNCTRL_V_MAINT      (6)
#define LNCTRL_M_MAINT      (03)
#define LNCTRL_LINK_TYPE    (1 << 8)    /* 0=data leads only, 1=modem */
#define LNCTRL_DTR          (1 << 9)    /* DTR to modem */
#define LNCTRL_RTS          (1 << 12)   /* RTS to modem */

BITFIELD vh_lnctrl_bits[] = {
  BIT(TX_ABORT),                            /* Transmitter Abort */
  BIT(IAUTO),                               /* Incoming Auto Flow Control */
  BIT(RX_ENA),                              /* Receiver Enable  */
  BIT(BREAK),                               /* Line BREAK (Space) */
  BIT(OAUTO),                               /* Output Auto Flow Control */
  BIT(FORCE_XOFF),                          /* Force XOFF */
  BITF(MAINT,2),                            /* Maintenance Mode */
  BIT(LINK_TYPE),                           /* modem/data leads only */
  BIT(DTR),                                 /* Data Terminal Ready */
  BITNCF(2),                                /* Unused */
  BIT(RTS),                                 /* Request To Send */
  BITNCF(3),                                /* Unused */
  ENDBITS
};

/* Transmit Buffer Address Register Number 1 (TBUFFAD1) */

BITFIELD vh_tbuffad1_bits[] = {
  BITF(TBUFFAD1,16),                        /* Transmit Buffer Address Register */
  ENDBITS
};

/* Transmit Buffer Address Register Number 2 (TBUFFAD2) */

#define TB2_M_TBUFFAD       (077)
#define TB2_TX_DMA_START    (1 << 7)
#define TB2_TX_ENA      (1 << 15)

BITFIELD vh_tbuffad2_bits[] = {
  BITF(TBUFFAD,6),                          /* Transmit DMA Address */
  BITNC,                                    /* Unused */
  BIT(TX_DMA_START),                        /* Transmit DMA Start */
  BITNCF(3),                                /* Unused */
  BIT(TX_ENA),                              /* Transmit Enable */
  ENDBITS
};

/* Transmit DMA Buffer Counter (TBUFFCT) */

BITFIELD vh_tbuffct_bits[] = {
  BITF(TBUFFCT,16),                         /* Transmit Character Count */
  ENDBITS
};

/* Self-Test Error Codes */

#define SELF_NULL       (0201)
#define SELF_SKIP       (0203)
#define SELF_OCT        (0211)
#define SELF_RAM        (0225)
#define SELF_RCD        (0231)
#define SELF_DRD        (0235)

#define BMP_OK          (0305)
#define BMP_BAD         (0307)

/* Loopback types */

#define LOOP_NONE       (0)
#define LOOP_H325       (1)
#define LOOP_H3101      (2) /* p.2-13 DHQ manual */
/* Local storage */

static uint16   vh_csr[VH_MUXES]    = { 0 };    /* CSRs */
static uint16   vh_timer[VH_MUXES]  = { 1 };    /* controller timeout */
static uint16   vh_mcount[VH_MUXES] = { 0 };
static int32    vh_timeo[VH_MUXES]  = { 0 };
static uint32   vh_ovrrun[VH_MUXES] = { 0 };    /* line overrun bits */
/* XOFF'd channels, one bit/channel */
static uint32   vh_stall[VH_MUXES]  = { 0 };
static uint16   vh_loop[VH_MUXES]   = { 0 };    /* loopback status */

/* One bit per controller: */
static uint32   vh_rxi = 0; /* rcv interrupts */
static uint32   vh_txi = 0; /* xmt interrupts */
static uint32   vh_crit = 0;/* FIFO.CRIT */

static uint32   vh_wait = 50;                   /* input polling adjustment */

static const int32 bitmask[4] = { 037, 077, 0177, 0377 };

/* RX FIFO state */

static int32    rbuf_idx[VH_MUXES]      = { 0 };/* index into vh_rbuf */
static uint32   vh_rbuf[VH_MUXES][FIFO_SIZE]    = { { 0 } };

/* TXQ state */

#define TXQ_SIZE    (16)
static int32    txq_idx[VH_MUXES]       = { 0 };
static uint32   vh_txq[VH_MUXES][TXQ_SIZE]  = { { 0 } };

/* Need to extend the TMLN structure */

typedef struct {
    TMLN    *tmln;
    uint16  lpr;        /* line parameters */
    uint16  lnctrl;     /* line control */
    uint16  lstat;      /* line modem status */
    uint16  tbuffct;    /* remaining character count */
    uint16  tbuf1;
    uint16  tbuf2;
    uint16  txchar;     /* single character I/O */
#define TX_FIFO_SIZE 64
    uint16  txfifo[TX_FIFO_SIZE];/* transmit FIFO - circular */

    uint16  txfifo_idx; /* Extraction index */
    uint16  txfifo_cnt; /* Count of FIFO entries  */
    uint16  txstate;    /* transmit state */
#define TXS_IDLE        0
#define TXS_PIO_START   1
#define TXS_PIO_PENDING 2
#define TXS_DMA_PENDING 3
} TMLX;

static TMLN vh_ldsc[VH_MUXES * VH_LINES_ALLOC] = { { 0 } };
static TMXR vh_desc = { VH_MUXES * VH_LINES_ALLOC, 0, 0, vh_ldsc };
static TMLX vh_parm[VH_MUXES * VH_LINES_ALLOC] = { { 0 } };

/* debugging bitmaps */
#define DBG_RREG    0x0001                              /* trace read registers */
#define DBG_WREG    0x0002                              /* trace write registers */
#define DBG_REG     DBG_RREG|DBG_WREG                   /* trace read/write registers */
#define DBG_INT     0x0004                              /* display interrupt activities */
#define DBG_TIM     0x0008                              /* display timing activities */
#define DBG_TIMTRC  0x0010                              /* display trace timing activities */
#define DBG_RCVSCH  0x0020                              /* display receive scheduling activities */
#define DBG_XMTSCH  0x0040                              /* display transmit scheduling activities */
#define DBG_XMT     TMXR_DBG_XMT                        /* display Transmitted Data */
#define DBG_RCV     TMXR_DBG_RCV                        /* display Received Data */
#define DBG_MDM     TMXR_DBG_MDM                        /* display Modem Signals */
#define DBG_CON     TMXR_DBG_CON                        /* display connection activities */
#define DBG_TRC     TMXR_DBG_TRC                        /* display trace routine calls */
#define DBG_ASY     TMXR_DBG_ASY                        /* display Asynchronous Activities */

DEBTAB vh_debug[] = {
  {"RREG",   DBG_RREG,   "read registers"},
  {"WREG",   DBG_WREG,   "write registers"},
  {"REG",    DBG_REG,    "read/write registers"},
  {"INT",    DBG_INT,    "interrupt activities"},
  {"TIM",    DBG_TIM,    "timing activities"},
  {"TIMTRC", DBG_TIMTRC, "trace timing activities"},
  {"RCVSCH", DBG_RCVSCH, "receive scheduling activities"},
  {"XMTSCH", DBG_XMTSCH, "transmit scheduling activities"},
  {"XMT",    DBG_XMT,    "Transmitted Data"},
  {"RCV",    DBG_RCV,    "Received Data"},
  {"MDM",    DBG_MDM,    "Modem Signals"},
  {"CON",    DBG_CON,    "connection activities"},
  {"TRC",    DBG_TRC,    "trace routine calls"},
  {"ASY",    DBG_ASY,    "Asynchronous Activities"},
  {0}
};

/* Forward references */
static t_stat vh_rd (int32 *data, int32 PA, int32 access);
static t_stat vh_wr (int32 data, int32 PA, int32 access);
static t_stat vh_svc (UNIT *uptr);
static t_stat vh_xmt_svc (UNIT *uptr);
static t_stat vh_timersvc (UNIT *uptr);
static int32 vh_rxinta (void);
static int32 vh_txinta (void);
static t_stat vh_clear (int32 vh, t_bool flag);
static t_stat vh_reset (DEVICE *dptr);
static t_stat vh_attach (UNIT *uptr, CONST char *cptr);
static t_stat vh_detach (UNIT *uptr);       
static t_stat vh_show_detail (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vh_show_rbuf (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vh_show_txq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vh_putc (int32 vh, TMLX *lp, int32 chan, int32 data);
static void vh_set_config (TMLX *lp);
static void doDMA (int32 vh, int32 chan);
static t_stat vh_setmode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vh_show_vec (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vh_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vh_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vh_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat vh_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat vh_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat vh_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *vh_description (DEVICE *dptr);

/* SIMH I/O Structures */

#define IOLN_VH         020

static DIB vh_dib = {
    IOBA_AUTO,
    IOLN_VH * VH_MUXES,
    &vh_rd,     /* read */
    &vh_wr,     /* write */
    2,          /* # of vectors */
    IVCL (VHRX),
    VEC_FLOAT,
    { &vh_rxinta, &vh_txinta },  /* int. ack. routines */
    IOLN_VH,    /* IO space per device */
};

static UNIT vh_unit[VH_MUXES+3] = {
    { UDATA (NULL, UNIT_ATTABLE, 0) },
};

static UNIT *vh_poll_unit;
static UNIT *vh_xmit_unit;
static UNIT *vh_timer_unit;

static const REG vh_reg[] = {
    { BRDATADF (CSR,         vh_csr, DEV_RDX, 16, VH_MUXES, "control/status register, boards 0 to 3", vh_csr_bits) },
    { BRDATAD  (TIMER,     vh_timer, DEV_RDX, 16, VH_MUXES, "controller timeout, boards 0 to 3") },
    { BRDATAD  (MCOUNT,   vh_mcount, DEV_RDX, 16, VH_MUXES, "count down timer, boards 0 to 3") },
    { BRDATAD  (TIMEO,     vh_timeo, DEV_RDX, 16, VH_MUXES, "receive interrupt count down timer, boards 0 to 3") },
    { BRDATAD  (OVRRUN,   vh_ovrrun, DEV_RDX, 16, VH_MUXES, "line overrun bits, boards 0 to 3") },
    { BRDATAD  (STALL,     vh_stall, DEV_RDX, 16, VH_MUXES, "XOFF'd channels 1 bit/channel, boards 0 to 3") },
    { BRDATAD  (LOOP,       vh_loop, DEV_RDX, 16, VH_MUXES, "loopback status, boards 0 to 3") },
    { GRDATAD  (RCVINT,      vh_rxi, DEV_RDX, 32, 0,        "rcv interrupts 1 bit/channel") },
    { GRDATAD  (TXINT,       vh_txi, DEV_RDX, 32, 0,        "xmt interrupts 1 bit/channel") },
    { GRDATAD  (FIFOCRIT,   vh_crit, DEV_RDX, 32, 0,        "FIFO.CRIT 1 bit/channel") },
    { DRDATAD  (TIME,       vh_wait, 24,                    "Slow DMA start delay"), PV_LEFT },
    { GRDATA   (DEVADDR,  vh_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA   (DEVVEC,  vh_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
};

static const MTAB vh_mod[] = {
#if !UNIBUS
    { UNIT_MODEDHU,            0, "DHV mode", "DHV", 
        &vh_setmode, NULL,  NULL, "Set DHV mode" },
#endif
    { UNIT_MODEDHU, UNIT_MODEDHU, "DHU mode", "DHU", 
        &vh_setmode, NULL,  NULL, "Set DHU mode" },
    { UNIT_FASTDMA,            0, NULL,       "NORMAL",
        NULL,        NULL,  NULL, "Set Normal DMA mode" },
    { UNIT_FASTDMA, UNIT_FASTDMA, "fast DMA", "FASTDMA", 
        NULL,        NULL,  NULL, "Enable Fast DMA mode" },
    { UNIT_MODEM,              0, NULL,       "NOMODEM",
        NULL,        NULL,  NULL, "Disable modem control" },
    { UNIT_MODEM,     UNIT_MODEM, "Modem",    "MODEM", 
        NULL,        NULL,  NULL, "Enable modem control" },
    { UNIT_HANGUP,             0, NULL,       "NOHANGUP",
        NULL,        NULL,  NULL, "Disable disconnect on DTR drop" },
    { UNIT_HANGUP,   UNIT_HANGUP, "Hangup",   "HANGUP",
        NULL,        NULL,  NULL, "Enable disconnect on DTR drop" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec, &vh_show_vec, (void *) &vh_desc, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
        &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &vh_setnl, &tmxr_show_lines, (void *) &vh_desc, "Display number of lines" },
    { UNIT_ATT,         UNIT_ATT, "summary", NULL,
        NULL, &tmxr_show_summ, (void *) &vh_desc, "Display a summary of line states"  },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &vh_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &vh_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &vh_desc, "Disconnect a specific line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "DETAIL", NULL,
        NULL, &vh_show_detail, NULL, "Display detailed multiplexer status" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "RBUF", NULL,
        NULL, &vh_show_rbuf, NULL, "Display recieve buffer" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "TXQ", NULL,
        NULL, &vh_show_txq, NULL, "Display transmit queue for each mux" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &vh_set_log, NULL, &vh_desc, "Display logging"  },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG=n",
        &vh_set_nolog, NULL, &vh_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &vh_show_log, &vh_desc, "Display logging for all lines" },
    { 0 }
};

DEVICE vh_dev = {
    "VH",           /* name */
    vh_unit,        /* units */
    (REG *)vh_reg,  /* registers */
    (MTAB *)vh_mod, /* modifiers */
    VH_MUXES,       /* # units */
    DEV_RDX,        /* address radix */
    8,              /* address width */
    1,              /* address increment */
    DEV_RDX,        /* data radix */
    8,              /* data width */
    NULL,           /* examine routine */
    NULL,           /* deposit routine */
    &vh_reset,      /* reset routine */
    NULL,           /* boot routine */
    &vh_attach,     /* attach routine */
    &vh_detach,     /* detach routine */
    (void *)&vh_dib,/* context */
    DEV_DISABLE | DEV_DIS | DEV_QBUS | DEV_UBUS | DEV_DEBUG | DEV_MUX,    /* flags */
    0, vh_debug,    /* debug control and debug flags */
    NULL,           /* memory size routine */
    NULL,           /* logical name */
    &vh_help,       /* help routine */
    &vh_help_attach,/* attach_help routines */
    (void *)&vh_desc,/* help context variable */
    &vh_description /* description */
};

/* Register names for Debug tracing */
static const char *vh_rd_dhv_regs[] =
    {"CSR   ", "RBUF  ", "LPR   ", "STAT  ", "LNCTRL", "TBFAD1", "TBFAD2", "TBFCNT" };
static const char *vh_wr_dhv_regs[] =
    {"CSR   ", "TXCHAR", "LPR   ", "STAT  ", "LNCTRL", "TBFAD1", "TBFAD2", "TBFCNT" };
static const char *vh_rd_dhu_regs[] =
    {"CSR   ", "RBUF  ", "LPR   ", "FIFOSZ", "LNCTRL", "TBFAD1", "TBFAD2", "TBFCNT" };
static const char *vh_wr_dhu_regs[] =
    {"CSR   ", "RXTIMR", "LPR   ", "FIFODT", "LNCTRL", "TBFAD1", "TBFAD2", "TBFCNT" };

/* Interrupt routines */

static void vh_clr_rxint (  int32   vh  )
{
    vh_rxi &= ~(1 << vh);
    if (vh_rxi == 0)
        CLR_INT (VHRX);
    else
        SET_INT (VHRX);
}

static void vh_set_rxint (  int32   vh  )
{
    vh_rxi |= (1 << vh);
    SET_INT (VHRX);
}

/* RX interrupt ack. (bus cycle) */

static int32 vh_rxinta (void)
{
    int32   vh;

    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++) {
        if (vh_rxi & (1 << vh)) {
            sim_debug(DBG_INT, &vh_dev, "vh_rxinta(vh=%d)\n", vh);
            vh_clr_rxint (vh);
            return (vh_dib.vec + (vh * 010));
        }
    }
    return (0);
}

static void vh_clr_txint (  int32   vh  )
{
    vh_txi &= ~(1 << vh);
    if (vh_txi == 0)
        CLR_INT (VHTX);
    else
        SET_INT (VHTX);
}

static void vh_set_txint (  int32   vh  )
{
    sim_debug(DBG_INT, &vh_dev, "vh_set_txint(vh=%d)\n", vh);
    vh_txi |= (1 << vh);
    SET_INT (VHTX);
}

/* TX interrupt ack. (bus cycle) */

static int32 vh_txinta (void)
{
    int32   vh;

    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++) {
        if (vh_txi & (1 << vh)) {
            sim_debug(DBG_INT, &vh_dev, "vh_txinta(vh=%d)\n", vh);
            vh_clr_txint (vh);
            return (vh_dib.vec + 4 + (vh * 010));
        }
    }
    return (0);
}
/* RX FIFO get/put routines */

/* return 0 on success, -1 on FIFO overflow */

static int32 fifo_put ( int32   vh,
            TMLX    *lp,
            int32   data    )
{
    int32   status = 0;

    if (lp == NULL)
        goto override;
    /* this might have to move to vh_getc() */
    if ((lp->lnctrl & LNCTRL_OAUTO) && ((data & RBUF_DIAG) == 0)) {
        TMLX    *l0p;
        /* implement transmitted data flow control */
        switch (data & 0377) {
        case XON:
            lp->tbuf2 |= TB2_TX_ENA;
            goto common;
        case XOFF:
            lp->tbuf2 &= ~TB2_TX_ENA;
        common:
            /* find line 0 for this controller */
            l0p = &vh_parm[vh * VH_LINES];
            if (l0p->lpr & LPR_DISAB_XRPT)
                return (0);
            break;
        default:
            break;
        }
    }
/* BUG: which of the following 2 is correct? */
    /* if ((data & RBUF_DIAG) == RBUF_DIAG) */
    if (data & RBUF_DIAG)
        goto override;
    if (((lp->lnctrl >> LNCTRL_V_MAINT) & LNCTRL_M_MAINT) == 2)
        goto override;
    if (!(lp->lnctrl & LNCTRL_RX_ENA))
        return (0); 
override:
    vh_csr[vh] |= CSR_RX_DATA_AVAIL;
    if (rbuf_idx[vh] < FIFO_SIZE) {
        vh_rbuf[vh][rbuf_idx[vh]] = data;
        rbuf_idx[vh] += 1;
    } else {
        vh_ovrrun[vh] |= (1 << RBUF_GETLINE (data));
        status = -1;
    }
    if (vh_csr[vh] & CSR_RXIE) {
        if (vh_unit[vh].flags & UNIT_MODEDHU) {
            /* was it a modem status change? */
            if ((data & RBUF_DIAG) == RBUF_DIAG)
                vh_set_rxint (vh);
            /* look for FIFO alarm @ 3/4 full */
            else if (rbuf_idx[vh] == FIFO_ALARM)
                vh_set_rxint (vh);
            else if (vh_timer[vh] == 0)
                ; /* nothing, infinite timeout */
            else if (vh_timer[vh] == 1)
                vh_set_rxint (vh);
            else if (vh_timeo[vh] == 0) {
                vh_timeo[vh] = MS2SIMH (vh_timer[vh]) + 1;
                sim_debug (DBG_TIM, &vh_dev, "Timeout set vh=%d, timeout=%d\n", vh, vh_timeo[vh]); 
                }
        } else {
            /* Interrupt on transition _from_ an empty FIFO */
            if (rbuf_idx[vh] == 1)
                vh_set_rxint (vh);
        }
    }
    if (rbuf_idx[vh] > FIFO_ALARM)
        vh_crit |= (1 << vh);
    /* Implement RX FIFO-level flow control */
    if (lp != NULL) {
        if ((lp->lnctrl & LNCTRL_FORCE_XOFF) || 
              ((vh_crit & (1 << vh)) && (lp->lnctrl & LNCTRL_IAUTO))) {
            int32   chan = RBUF_GETLINE(data);
            vh_stall[vh] ^= (1 << chan);
            /* send XOFF every other character received */
            if (vh_stall[vh] & (1 << chan))
                vh_putc (vh, lp, chan, XOFF);
        }
    }
    return (status);
}

static int32 fifo_get ( int32   vh  )
{
    int32   data, i;

    if (rbuf_idx[vh] == 0)
        return (0);

    /* pick off the first character, mark valid */
    data = vh_rbuf[vh][0] | RBUF_DATA_VALID;
    /* move the remainder up */
    rbuf_idx[vh] -= 1;
    for (i = 0; i < rbuf_idx[vh]; i++)
        vh_rbuf[vh][i] = vh_rbuf[vh][i + 1];
    /* rbuf_idx[vh] -= 1; */
    /* look for any previous overruns */
    if (vh_ovrrun[vh]) {
        for (i = 0; i < VH_LINES; i++) {
            if (vh_ovrrun[vh] & (1 << i)) {
                fifo_put (vh, NULL, RBUF_OVERRUN_ERR |
                      RBUF_PUTLINE (i));
                vh_ovrrun[vh] &= ~(1 << i);
                break;
            }
        }
    }
    /* recompute FIFO alarm condition */
    if ((rbuf_idx[vh] < FIFO_HALF) && (vh_crit & (1 << vh))) {
        vh_crit &= ~(1 << vh);
        /* send XON to all XOFF'd channels on this controller */
        for (i = 0; i < VH_LINES; i++) {
            TMLX    *lp = &vh_parm[(vh * VH_LINES) + i];
            if (lp->lnctrl & LNCTRL_FORCE_XOFF)
                continue;
            if (vh_stall[vh] & (1 << i)) {
                vh_putc (vh, NULL, i, XON);
                vh_stall[vh] &= ~(1 << i);
            }
        }
    }

    if (rbuf_idx[vh] == 0)                  /* FIFO just became empty? */
        vh_csr[vh] &= ~CSR_RX_DATA_AVAIL;   /* clear CSR bit ti indicate this */

    /* Reschedule the next poll preceisely so that the 
       programmed input speed is observed. */
    sim_clock_coschedule_abs (vh_poll_unit, tmxr_poll);
    return (data & 0177777);
}
/* TX FIFO get/put routines */

/* return 0 on success, -1 on FIFO overflow */

static int32 tx_fifo_free_count ( TMLX    *lp)
{
return TX_FIFO_SIZE - lp->txfifo_cnt;
}

static int32 tx_fifo_put ( TMLX    *lp,
            int32   data    )
{
    if (tx_fifo_free_count (lp) == 0)
        return -1;
    lp->txfifo[(lp->txfifo_idx + lp->txfifo_cnt) % TX_FIFO_SIZE] = data;
    ++lp->txfifo_cnt;
    return 0;
}

static int32 tx_fifo_get ( TMLX    *lp    )
{
    int32 data = lp->txfifo[lp->txfifo_idx];

    if (lp->txfifo_cnt == 0)
        return -1;
    lp->txfifo[lp->txfifo_idx] = 0;
    --lp->txfifo_cnt;
    ++lp->txfifo_idx;
    if (lp->txfifo_idx == TX_FIFO_SIZE)
        lp->txfifo_idx = 0;
    return (data & 0177777);
}

/* TX Q manipulation */

static int32 dq_tx_report ( int32   vh  )
{
    int32   data, i;

    if (txq_idx[vh] == 0)
        return (0);
    data = vh_txq[vh][0];
    txq_idx[vh] -= 1;
    for (i = 0; i < txq_idx[vh]; i++)
        vh_txq[vh][i] = vh_txq[vh][i + 1];
    if ((txq_idx[vh] > 0) && (vh_csr[vh] & CSR_TXIE))
        vh_set_txint (vh);
    /* txq_idx[vh] -= 1; */
    return (data & 0177777);
}

static void q_tx_report ( TMLX    *lp,
                          int32   extra_data )
{
    int32   vh = (lp - vh_parm) / VH_LINES;
    int32   data = (((lp - vh_parm) - (vh * VH_LINES)) << CSR_V_TX_LINE) | extra_data;

    if (vh_csr[vh] & CSR_TXIE)
        vh_set_txint (vh);
    if (txq_idx[vh] >= TXQ_SIZE) {
/* BUG: which of the following 2 is correct? */
        dq_tx_report (vh);
        /* return; */
    }
    vh_txq[vh][txq_idx[vh]] = CSR_TX_ACTION | data;
    txq_idx[vh] += 1;
}
/* Channel get/put routines */

/* TX a character on a line, regardless of the TX enable state */

static t_stat vh_putc ( int32   vh,
            TMLX    *lp,
            int32   chan,
            int32   data    )
{
    int32   val;
    t_stat  status = SCPE_OK;

    /* truncate to desired character length */
    data &= bitmask[(lp->lpr >> LPR_V_CHAR_LGTH) & LPR_M_CHAR_LGTH];
    switch ((lp->lnctrl >> LNCTRL_V_MAINT) & LNCTRL_M_MAINT) {
    case 0:     /* normal */
#if 0
        /* check for (external) loopback setting */
        switch (vh_loop[vh]) {
        default:
        case LOOP_NONE:
            break;
        }
#endif
        status = tmxr_putc_ln (lp->tmln, data);
        if (status == SCPE_STALL) {
            /* let's flush and try again */
            tmxr_send_buffered_data (lp->tmln);
            status = tmxr_putc_ln (lp->tmln, data);
        }
        break;
    case 1:     /* auto echo */
        break;
    case 2:     /* local loopback */
        if (lp->lnctrl & LNCTRL_BREAK)
            val = fifo_put (vh, lp,
                RBUF_FRAME_ERR | RBUF_PUTLINE (chan));
        else
            val = fifo_put (vh, lp,
                RBUF_PUTLINE (chan) | data);
        status = (val < 0) ? SCPE_TTMO : SCPE_OK;
        break;
    default:    /* remote loopback */
        break;
    }
    return (status);
}

/* Retrieve all stored input from TMXR and place in RX FIFO */

static void vh_getc (   int32   vh  )
{
    uint32  i, c;
    TMLX    *lp;
    int32   modem_incoming_bits;
    uint16  new_lstat;

    for (i = 0; i < (uint32)VH_LINES; i++) {
        if (rbuf_idx[vh] >= (FIFO_ALARM-1)) /* close to fifo capacity? */
            continue;                       /* don't bother checking for data */
        lp = &vh_parm[(vh * VH_LINES) + i];
        while ((c = tmxr_getc_ln (lp->tmln)) != 0) {
            if (c & SCPE_BREAK) {
                fifo_put (vh, lp,
                    RBUF_FRAME_ERR | RBUF_PUTLINE (i));
            } else {
                c &= bitmask[(lp->lpr >> LPR_V_CHAR_LGTH) &
                    LPR_M_CHAR_LGTH];
                fifo_put (vh, lp, RBUF_PUTLINE (i) | c);
            }
        }
        tmxr_set_get_modem_bits (lp->tmln, 0, 0, &modem_incoming_bits);
        new_lstat = lp->lstat & ~(STAT_DSR | STAT_DCD | STAT_CTS | STAT_RI);
        new_lstat |= (modem_incoming_bits & TMXR_MDM_DSR) ? STAT_DSR : 0;
        new_lstat |= (modem_incoming_bits & TMXR_MDM_DCD) ? STAT_DCD : 0;
        new_lstat |= (modem_incoming_bits & TMXR_MDM_CTS) ? STAT_CTS : 0;
        new_lstat |= (modem_incoming_bits & TMXR_MDM_RNG) ? STAT_RI  : 0;
        if (new_lstat != lp->lstat) {
            lp->lstat = new_lstat;
            if (lp->lnctrl & LNCTRL_LINK_TYPE)
                fifo_put (vh, lp, RBUF_DIAG | RBUF_PUTLINE (i) | ((lp->lstat >> 8) & 0376));
        }
    }
}

static void vh_set_config (     TMLX    *lp)
{
    char lineconfig[16];
    
    sprintf(lineconfig, "%s-%s%s%s", LPR_GETSPD(lp->lpr), LPR_GETCHARSIZE(lp->lpr), LPR_GETPARITY(lp->lpr), LPR_GETSTOPBITS(lp->lpr));
    if (!lp->tmln->serconfig || (0 != strcmp(lp->tmln->serconfig, lineconfig))) /* config changed? */
        tmxr_set_config_line (lp->tmln, lineconfig);      /* set it */
}

/* I/O dispatch routines */

static t_stat vh_rd (   int32   *data,
                        int32   PA,
                        int32   access  )
{
    int32   vh = ((PA - vh_dib.ba) >> 4), line;
    TMLX    *lp;
    static BITFIELD* bitdefs[] = {vh_csr_bits, vh_rbuf_bits, vh_lpr_bits, vh_stat_bits,
                                  vh_lnctrl_bits, vh_tbuffad1_bits, vh_tbuffad2_bits, vh_tbuffct_bits};


    if (vh > VH_MAXMUX)                         /* validate mux number */
        return SCPE_IERR;

    switch ((PA >> 1) & 7) {
    case 0:     /* CSR */
        *data = vh_csr[vh] | dq_tx_report (vh);
        vh_csr[vh] &= ~0117400; /* clear the read-once bits */
        break;
    case 1:     /* RBUF */
        *data = fifo_get (vh);
        break;
    case 2:     /* LPR */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = lp->lpr;
        break;
    case 3:     /* STAT/FIFOSIZE */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = (lp->lstat & ~0377) |       /* modem status */
                    tx_fifo_free_count (lp);
        break;
    case 4:     /* LNCTRL */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = lp->lnctrl;
        break;
    case 5:     /* TBUFFAD1 */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = lp->tbuf1;
        break;
    case 6:     /* TBUFFAD2 */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = lp->tbuf2;
        break;
    case 7:     /* TBUFFCT */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES) {
            *data = 0;
            break;
        }
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        *data = lp->tbuffct;
        break;
    default:
        /* can't happen */
        break;
    }

    sim_debug(DBG_RREG, &vh_dev, "vh_rd(vh=%d, PA=0x%08X [%s], access=%d, data=0x%X) ", vh, PA, 
              ((vh_unit[vh].flags & UNIT_MODEDHU) ? vh_rd_dhu_regs : vh_rd_dhv_regs)[(PA >> 1) & 07], access, *data);
    sim_debug_bits(DBG_RREG, &vh_dev, bitdefs[(PA >> 1) & 07], (uint32)(*data), (uint32)(*data), TRUE);

    return (SCPE_OK);
}

static t_stat vh_wr (   int32   ldata,
                        int32   PA,
                        int32   access  )
{
    int32   vh = ((PA - vh_dib.ba) >> 4), line;
    TMLX    *lp;
    uint16  data = (uint16)ldata;
    static BITFIELD* bitdefs[] = {vh_csr_bits, vh_rbuf_bits, vh_lpr_bits, vh_stat_bits,
                                  vh_lnctrl_bits, vh_tbuffad1_bits, vh_tbuffad1_bits, vh_tbuffct_bits};

    if (vh > VH_MAXMUX)                         /* validate mux number */
        return SCPE_IERR;

    sim_debug(DBG_WREG, &vh_dev, "vh_wr(vh=%d, PA=0x%08X [%s], access=%d, data=0x%X) ", vh, PA, 
              ((vh_unit[vh].flags & UNIT_MODEDHU) ? vh_wr_dhu_regs : vh_wr_dhv_regs)[(PA >> 1) & 07], access, data);
    sim_debug_bits(DBG_WREG, &vh_dev, bitdefs[(PA >> 1) & 07], (uint32)((PA & 1) ? data<<8 : data), (uint32)((PA & 1) ? data<<8 : data), TRUE);

    switch ((PA >> 1) & 7) {   
    case 0:     /* CSR, but no read-modify-write */
        if (access == WRITEB)
            data = (PA & 1) ?
                (vh_csr[vh] & 0377) | (data << 8) :
                (vh_csr[vh] & ~0377) | (data & 0377);
        if (data & CSR_MASTER_RESET) {
            sim_cancel (vh_poll_unit);
            vh_mcount[vh] = MS2SIMH (1200); /* 1.2 seconds */
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            sim_debug (DBG_TIM, &vh_dev, "vh_wr() - Master Reset Timeout set vh=%d, timeout=%d\n", vh, vh_mcount[vh]); 
        }
        if ((data & CSR_RXIE) == 0)
            vh_clr_rxint (vh);
        /* catch the RXIE transition if the FIFO is not empty */
        else if (((vh_csr[vh] & CSR_RXIE) == 0) &&
              (rbuf_idx[vh] != 0)) {
            if (vh_unit[vh].flags & UNIT_MODEDHU) {
                if (rbuf_idx[vh] > FIFO_ALARM)
                    vh_set_rxint (vh);
                else if (vh_timer[vh] == 0)
                    ;
                else if (vh_timer[vh] == 1)
                    vh_set_rxint (vh);
                else if (vh_timeo[vh] == 0) {
                    vh_timeo[vh] = MS2SIMH (vh_timer[vh]) + 1;
                    sim_debug (DBG_TIM, &vh_dev, "vh_wr() - Timeout set vh=%d, timeout=%d\n", vh, vh_timeo[vh]); 
                    }
            } else {
                vh_set_rxint (vh);
            }
        }
        if ((data & CSR_TXIE) == 0)
            vh_clr_txint (vh);
        else if (((vh_csr[vh] & CSR_TXIE) == 0) &&
              (txq_idx[vh] != 0))
            vh_set_txint (vh);
        vh_csr[vh] = (vh_csr[vh] & ~((uint16) CSR_RW)) | (data & (uint16) CSR_RW);
        break;
    case 1:     /* TXCHAR/RXTIMER */
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)
            break;
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (vh_unit[vh].flags & UNIT_MODEDHU) {
            if (CSR_GETCHAN (vh_csr[vh]) != 0)
                break;
            if (access == WRITEB)
                data = (PA & 1) ?
                    (vh_timer[vh] & 0377) | (data << 8) :
                    (vh_timer[vh] & ~0377) | (data & 0377);
            vh_timer[vh] = data & 0377;
#if 0
            if (vh_csr[vh] & CSR_RXIE) {
                if (rbuf_idx[vh] > FIFO_ALARM)
                    vh_set_rxint (vh);
                else if (vh_timer[vh] == 0)
                    ;
                else if (vh_timer[vh] == 1)
                    vh_set_rxint (vh);
                else if (vh_timeo[vh] == 0)
                    vh_timeo[vh] = MS2SIMH (vh_timer[vh]) + 1;
            }
#endif
        } else {
            line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
            lp = &vh_parm[line];
            if (access == WRITEB)
                data = (PA & 1) ?   
                    (lp->txchar & 0377) | (data << 8) :
                    (lp->txchar & ~0377) | (data & 0377);
            lp->txchar = data;  /* TXCHAR */
            if (lp->txchar & TXCHAR_TX_DATA_VALID) {
                if (lp->tbuf2 & TB2_TX_ENA) {
                    tx_fifo_put (lp, TXCHAR_TX_DATA_VALID | (data & 0377));
                    sim_debug (DBG_XMTSCH, &vh_dev, "VH-%d PIO Scheduling Start - 0x%X '%c'\n", (int)(lp - vh_parm), lp->txchar & 0xFF, isprint (lp->txchar & 0xFF) ? lp->txchar & 0xFF : '.');
                    sim_activate_abs (vh_xmit_unit, 0);
                }
            }
        }
        break;
    case 2:     /* LPR */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (access == WRITEB)
            data = (PA & 1) ?
                (lp->lpr & 0377) | (data << 8) :
                (lp->lpr & ~0377) | (data & 0377);
        /* Modify only if CSR<3:0> == 0 */
        if (CSR_GETCHAN (vh_csr[vh]) != 0)
            data &= ~LPR_DISAB_XRPT;
        lp->lpr = data;
        vh_set_config (lp);
        if (((lp->lpr >> LPR_V_DIAG) & LPR_M_DIAG) == 1) {
            fifo_put (vh, lp,
                RBUF_DIAG |
                RBUF_PUTLINE (CSR_GETCHAN (vh_csr[vh])) |
                BMP_OK);
            /* BUG: check for overflow above */
            lp->lpr &= ~(LPR_M_DIAG << LPR_V_DIAG);
        }
        break;
    case 3:     /* STAT/FIFODATA */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (vh_unit[vh].flags & UNIT_MODEDHU) {
            /* high byte writes not allowed */
            if (PA & 1)
                break;
            /* transmit 1 or 2 characters */
            if (!(lp->tbuf2 & TB2_TX_ENA))
                break;
            tx_fifo_put (lp, TXCHAR_TX_DATA_VALID | (data & 0377));
            if (lp->txfifo_cnt == 1) {
                sim_debug (DBG_XMTSCH, &vh_dev, "VH-%d PIO Scheduling Start - 0x%X '%c' %s 0x%X '%c'\n", (int)(lp - vh_parm), data & 0xFF, isprint (data & 0xFF) ? data & 0xFF : '.', 
                                                                                                         (access == WRITEB) ? "" : "Extra Byte", data >> 8, isprint (data >> 8) ? data >> 8 : '.' );
                sim_activate_abs (vh_xmit_unit, 0);
                }
            if (access != WRITEB)                       /* second character */
                tx_fifo_put (lp, TXCHAR_TX_DATA_VALID | ((data >> 8)& 0377));
        }
        break;
    case 4:     /* LNCTRL */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)   
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (access == WRITEB)
            data = (PA & 1) ?
                (lp->lnctrl & 0377) | (data << 8) :
                (lp->lnctrl & ~0377) | (data & 0377);
        /* catch the abort TX transition */
        if (!(lp->lnctrl & LNCTRL_TX_ABORT) &&
             (data & LNCTRL_TX_ABORT)) {
            if ((lp->tbuf2 & TB2_TX_ENA) &&
                (lp->txfifo_cnt != 0)) {
                lp->txfifo_idx = lp->txfifo_cnt = 0;
                q_tx_report (lp, 0);
            }
        }
        /* Implement program-initiated flow control */
        if ( (data & LNCTRL_FORCE_XOFF) &&
             !(lp->lnctrl & LNCTRL_FORCE_XOFF) ) {
            if (!(lp->lnctrl & LNCTRL_IAUTO))
                vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]), XOFF);
        } else if ( !(data & LNCTRL_FORCE_XOFF) &&
                (lp->lnctrl & LNCTRL_FORCE_XOFF) ) {
            if (!(lp->lnctrl & LNCTRL_IAUTO))
                vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]), XON);
            else if (!(vh_crit & (1 << vh)) &&
                 (vh_stall[vh] & (1 << CSR_GETCHAN (vh_csr[vh]))))
                vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]), XON);
        }
        if ( (data & LNCTRL_IAUTO) &&       /* IAUTO 0->1 */
             !(lp->lnctrl & LNCTRL_IAUTO) ) {
            if (!(lp->lnctrl & LNCTRL_FORCE_XOFF)) {
                if (vh_crit & (1 << vh)) {
                    vh_putc (vh, lp,
                        CSR_GETCHAN (vh_csr[vh]), XOFF);
                    vh_stall[vh] |= (1 << CSR_GETCHAN (vh_csr[vh]));
                }
            } else {
                /* vh_stall[vh] |= (1 << CSR_GETCHAN (vh_csr[vh])) */;
            }
        } else if ( !(data & LNCTRL_IAUTO) &&
                (lp->lnctrl & LNCTRL_IAUTO) ) {
            if (!(lp->lnctrl & LNCTRL_FORCE_XOFF))
                vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]), XON);
        }
        tmxr_set_line_modem_control (lp->tmln, (data & LNCTRL_LINK_TYPE) && (vh_unit[vh].flags & UNIT_MODEM));
        if (!(vh_unit[vh].flags & UNIT_MODEM))
            tmxr_set_get_modem_bits (lp->tmln, (TMXR_MDM_DTR | TMXR_MDM_RTS), 0, NULL);
        /* check modem control bits */
        if ( !(data & LNCTRL_DTR) &&        /* DTR 1->0 */
              (lp->lnctrl & LNCTRL_DTR)) {
            if (vh_unit[vh].flags & UNIT_HANGUP) {
                tmxr_linemsg (lp->tmln, "\r\nLine hangup\r\n");
                tmxr_set_get_modem_bits (lp->tmln, 0, TMXR_MDM_DTR, NULL);
            }
        }
        if (!(lp->lnctrl & LNCTRL_DTR) &&   /* DTR 0->1 */
            (data & LNCTRL_DTR)) {
            tmxr_set_get_modem_bits (lp->tmln, TMXR_MDM_DTR, 0, NULL);
            sim_activate_abs (vh_poll_unit, 0);
        }
        tmxr_set_get_modem_bits (lp->tmln, ((data & LNCTRL_RTS) ? TMXR_MDM_RTS : 0), ((data & LNCTRL_RTS) ? 0 : TMXR_MDM_RTS), NULL);
        lp->lnctrl = data;
        lp->tmln->rcve = (data & LNCTRL_RX_ENA) ? 1 : 0;
        tmxr_poll_rx (&vh_desc);
        vh_getc (vh);
        if (lp->lnctrl & LNCTRL_BREAK)
            vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]), 0);
        break;
    case 5:     /* TBUFFAD1 */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)   
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (access == WRITEB)
            data = (PA & 1) ?
                (lp->tbuf1 & 0377) | (data << 8) :
                (lp->tbuf1 & ~0377) | (data & 0377);
        lp->tbuf1 = data;
        break;
    case 6:     /* TBUFFAD2 */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (access == WRITEB)
            data = (PA & 1) ?
                (lp->tbuf2 & 0377) | (data << 8) :
                (lp->tbuf2 & ~0377) | (data & 0377);
        lp->tbuf2 = data;
        /* if starting a DMA, clear DMA_ERR */
        if ((data & TB2_TX_ENA) && (data & TB2_TX_DMA_START)) {
            sim_debug (DBG_XMTSCH, &vh_dev, "VH-%d DMA Starting: After %d\n", vh, (vh_unit[vh].flags & UNIT_FASTDMA) ? vh_wait/10 : vh_wait);
            sim_activate_after_abs (vh_xmit_unit, (vh_unit[vh].flags & UNIT_FASTDMA) ? vh_wait/10 : vh_wait);
        }
        break;
    case 7:     /* TBUFFCT */
        if ((data == RESET_ABORT) && (vh_csr[vh] & CSR_MASTER_RESET)) {
            vh_mcount[vh] = 1;
            sim_clock_coschedule (vh_timer_unit, tmxr_poll);
            break;
        }
        if (CSR_GETCHAN (vh_csr[vh]) >= VH_LINES)
            break;
        line = (vh * VH_LINES) + CSR_GETCHAN (vh_csr[vh]);
        lp = &vh_parm[line];
        if (access == WRITEB)
            data = (PA & 1) ?
                (lp->tbuffct & 0377) | (data << 8) :
                (lp->tbuffct & ~0377) | (data & 0377);
        lp->tbuffct = data;
        break;
    default:
        /* can't happen */
        break;
    }
    return (SCPE_OK);
}

static void doDMA ( int32   vh,
            int32   chan    )
{
    int32   line, status;
    uint32  pa;
    TMLX    *lp;

    line = (vh * VH_LINES) + chan;
    lp = &vh_parm[line];
    if ((lp->tbuf2 & TB2_TX_ENA) && (lp->tbuf2 & TB2_TX_DMA_START)) {
        int32 sent = 0;

        pa = lp->tbuf1;
        pa |= (lp->tbuf2 & TB2_M_TBUFFAD) << 16;
        status = 0;
        while (tmxr_txdone_ln (lp->tmln) && (lp->tbuffct > 0)) {
            uint8   buf;
            if (lp->lnctrl & LNCTRL_TX_ABORT) {
                lp->tbuf2 &= ~TB2_TX_DMA_START;
                q_tx_report (lp, 0);
                break;
            }
            if (Map_ReadB (pa, 1, &buf)) {
                status |= CSR_TX_DMA_ERR;
                lp->tbuffct = 0;
                break;
            }
            if (vh_putc (vh, lp, chan, buf) == SCPE_STALL)
                break;
            ++sent;
            /* pa = (pa + 1) & PAMASK; */
            pa = (pa + 1) & ((1 << 22) - 1);
            lp->tbuffct--;
            break;
        }
        lp->tbuf1 = pa & 0177777;
        lp->tbuf2 = (lp->tbuf2 & ~TB2_M_TBUFFAD) |
                ((pa >> 16) & TB2_M_TBUFFAD);
        sim_debug (DBG_XMTSCH, &vh_dev, "VH-%d DMAed: %d, remaining: %u - %d\n", (int)(lp - vh_parm), sent, lp->tbuffct, status);
        if (((tmxr_txdone_ln (lp->tmln)) &&
             (sent == 0) &&
             (lp->tbuffct == 0)) || 
            (!lp->tmln->conn)) {
            lp->tbuf2 &= ~TB2_TX_DMA_START;
            q_tx_report (lp, status);
            lp->txstate = TXS_IDLE;
        } else
            lp->txstate = TXS_DMA_PENDING;
    }
}

/* Perform many of the functions of PROC2 */

static t_stat vh_timersvc (  UNIT    *uptr   )
{
    int32   vh;

    sim_debug(DBG_TIMTRC, &vh_dev, "vh_timersvc()\n");

    /* scan all DHU-mode muxes for RX FIFO timeout */
    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++) {
        if (vh_unit[vh].flags & UNIT_MODEDHU) {
            if ((vh_csr[vh] & (CSR_SKIP | CSR_MASTER_RESET)) == (CSR_SKIP | CSR_MASTER_RESET)) {
                sim_activate (vh_poll_unit, tmxr_poll);
                vh_csr[vh] &= ~CSR_MASTER_RESET;
                }
            if (vh_timeo[vh] && (vh_csr[vh] & CSR_RXIE)) {
                vh_timeo[vh] -= 1;
                if ((vh_timeo[vh] == 0) && rbuf_idx[vh]) {
                    vh_set_rxint (vh);
                    sim_debug (DBG_TIM, &vh_dev, "vh_timersvc() - vh=%d, RX FIFO Timeout\n", vh); 
                }
            }
        }
    }
    /* scan all muxes for countdown reset */
    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++) {
        if (vh_csr[vh] & CSR_MASTER_RESET) {
            if (vh_mcount[vh] != 0)
                vh_mcount[vh] -= 1;
            else {
                vh_clear (vh, FALSE);
                sim_debug (DBG_TIM, &vh_dev, "vh_timersvc() - vh=%d, Master Reset Complete\n", vh); 
            }
        }
    }
    sim_clock_coschedule (uptr, tmxr_poll); /* requeue ourselves */
    return (SCPE_OK);
}

static t_stat vh_svc (  UNIT    *uptr   )
{
    int32   vh, newln;

    sim_debug(DBG_TRC, find_dev_from_unit(uptr), "vh_svc()\n");
    sim_debug(DBG_RCVSCH, find_dev_from_unit(uptr), "vh_svc()\n");

    /* sample for modem changes (new connections) */
    newln = tmxr_poll_conn (&vh_desc);
    if (newln >= 0) {
        TMLX    *lp;
        int32   line;

        vh = newln / VH_LINES;  /* determine which mux */
        line = newln - (vh * VH_LINES);
        lp = &vh_parm[newln];
        lp->lstat |= STAT_DSR | STAT_DCD | STAT_CTS;
        lp->lstat &= ~STAT_RI;
        if (lp->lnctrl & LNCTRL_LINK_TYPE)
            fifo_put (vh, lp, RBUF_DIAG |
                      RBUF_PUTLINE (line) |
                      ((lp->lstat >> 8) & 0376));
            /* BUG: should check for overflow above */
    }
    if (newln == -2) {      /* Ringing */
        for (newln = 0; newln < vh_desc.lines; newln++) {
            TMLX    *lp;
            int32   line;

            vh = newln/VH_LINES;
            line = newln - (vh * VH_LINES);
            lp = &vh_parm[newln];
            if (lp->lnctrl & LNCTRL_LINK_TYPE) {
                if (lp->tmln->modembits & TMXR_MDM_RNG) {
                    lp->lstat |= STAT_DSR | STAT_RI;
                    fifo_put (vh, lp, RBUF_DIAG |
                              RBUF_PUTLINE (line) |
                              ((lp->lstat >> 8) & 0376));
                }
            }
        }
    }
    /* interrupt driven in a real DHQ */
    tmxr_poll_rx (&vh_desc);
    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++)
        vh_getc (vh);
    sim_clock_coschedule (uptr, tmxr_poll); /* requeue ourselves */
    return SCPE_OK;
}

static t_stat vh_xmt_svc (  UNIT    *uptr   )
{
    int32   vh, i;

    sim_debug(DBG_TRC, find_dev_from_unit(uptr), "vh_xmt_svc()\n");
    sim_debug(DBG_XMTSCH, find_dev_from_unit(uptr), "vh_xmt_svc()\n");

    /* scan all muxes lines */
    for (vh = 0; vh < vh_desc.lines/VH_LINES; vh++) {
        for (i = 0; i < VH_LINES; i++) {
            int32   line = (vh * VH_LINES) + i;
            TMLX    *lp = &vh_parm[line];

            /* process any pending programmed output */
            if (lp->txfifo_cnt) {
                if (lp->tbuf2 & TB2_TX_ENA) {
                    sim_debug (DBG_XMTSCH, &vh_dev, "VH-%d PIO: %s - 0x%X '%c'\n", (int)(lp - vh_parm), 
                                                                              (lp->txstate == TXS_PIO_PENDING) ? "Pending" : ((lp->txstate == TXS_PIO_START) ? "Starting" : ((lp->txstate == TXS_IDLE) ? "Idle" : "Unknown")), 
                                                                              lp->txfifo[lp->txfifo_idx] & 0xFF, isprint (lp->txfifo[lp->txfifo_idx] & 0xFF) ? lp->txfifo[lp->txfifo_idx] & 0xFF : '.');
                    switch (lp->txstate) {
                        case TXS_PIO_PENDING:
                            if (0 == tmxr_txdone_ln (lp->tmln))     /* actually done? */
                                break;
                            tx_fifo_get (lp);
                            q_tx_report (lp, 0);
                            lp->txstate = TXS_IDLE;
                            if (lp->txfifo_cnt == 0) 
                                break;
                            /* fall through */
                        case TXS_IDLE:
                            lp->txstate = TXS_PIO_START;
                            /* fall through */
                        case TXS_PIO_START:
                            if (vh_putc (vh, lp, CSR_GETCHAN (vh_csr[vh]),
                                         lp->txfifo[lp->txfifo_idx]) != SCPE_STALL)
                                lp->txstate = TXS_PIO_PENDING;
                            break;
                    }
                }
            }
            /* process pending DMA */
            doDMA (vh, i);
        }
    }
    tmxr_poll_tx (&vh_desc);
    sim_activate_after (uptr, 250000); /* requeue ourselves */
    return SCPE_OK;
}

/* init a channel on a controller */

/* set for:
send/receive 9600
8 data bits
1 stop bit
no parity
parity odd
auto-flow off
RX disabled
TX enabled
no break on line
no loopback
link type set to data-leads only
DTR & RTS off
DMA character counter 0
DMA start address registers 0
TX_DMA_START 0
TX_ABORT 0
auto-flow reports enabled
FIFO size set to 64
*/

static void vh_init_chan (  int32   vh,
                int32   chan    )
{
    int32   line;
    TMLX    *lp;

    line = (vh * VH_LINES) + chan;
    lp = &vh_parm[line];
    lp->lpr = (RATE_9600 << LPR_V_TX_SPEED) |
          (RATE_9600 << LPR_V_RX_SPEED) |
          (03 << LPR_V_CHAR_LGTH);
    vh_set_config (lp);
    lp->lnctrl = 0;
    lp->lstat &= ~(STAT_MDL | STAT_DHUID | STAT_RI);
    if (vh_unit[vh].flags & UNIT_MODEDHU)
        lp->lstat |= STAT_DHUID | 64;
    if (!(vh_unit[vh].flags & UNIT_MODEM)) {
        lp->lstat |= STAT_DSR;
        if (vh_unit[vh].flags & UNIT_HANGUP)
            tmxr_set_get_modem_bits (lp->tmln, 0, TMXR_MDM_DTR | TMXR_MDM_RTS, NULL);
        }
    lp->tmln->xmte = 1;
    lp->tmln->rcve = 0;
    lp->tbuffct = 0;
    lp->tbuf1 = 0;
    lp->tbuf2 = TB2_TX_ENA;
    lp->txchar = 0;
}

/* init a controller; flag true if BINIT, false if master.reset */

static t_stat vh_clear (    int32   vh,
                t_bool  flag    )
{
    int32   i;

    txq_idx[vh] = 0;
    rbuf_idx[vh] = 0;
    /* put 8 diag bytes in FIFO: 6 SELF_x, 2 circuit revision codes */
    if (vh_csr[vh] & CSR_SKIP) {
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(0) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(1) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(2) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(3) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(4) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(5) | SELF_SKIP);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(6) | 0107);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(7) | 0105);
    } else {
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(0) | SELF_NULL);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(1) | SELF_NULL);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(2) | SELF_NULL);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(3) | SELF_NULL);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(4) | SELF_NULL);
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(5) | SELF_NULL);
        /* PROC2 ver. 1 */
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(6) | 0107);
        /* PROC1 ver. 1 */
        fifo_put (vh, NULL, RBUF_DIAG | RBUF_PUTLINE(7) | 0105);
    }
    vh_csr[vh] &= ~(CSR_TX_ACTION|CSR_DIAG_FAIL|CSR_MASTER_RESET);
    if (flag)
        vh_csr[vh] &= ~(CSR_TXIE|CSR_RXIE|CSR_SKIP);
    vh_csr[vh] |= CSR_TX_DMA_ERR | (CSR_M_TX_LINE << CSR_V_TX_LINE);
    vh_clr_rxint (vh);
    vh_clr_txint (vh);
    vh_timer[vh] = 1;
    vh_timeo[vh] = 0;
    vh_ovrrun[vh] = 0;
    for (i = 0; i < VH_LINES; i++)
        vh_init_chan (vh, i);
    vh_crit &= ~(1 << vh);
    vh_stall[vh] = 0;
    vh_loop[vh] = LOOP_NONE;
    return (SCPE_OK);
}

/* Reset all controllers.  Used by BINIT and RESET. */

static t_stat vh_reset (    DEVICE  *dptr   )
{
    int32   i;

    tmxr_set_port_speed_control (&vh_desc);
    tmxr_set_modem_control_passthru (&vh_desc);
    if (vh_desc.lines > VH_MUXES*VH_LINES)
        vh_desc.lines = VH_MUXES*VH_LINES;
    vh_dev.numunits = (vh_desc.lines / VH_LINES) + 3;
    if (vh_poll_unit)
        sim_cancel (vh_poll_unit);
    vh_poll_unit =  &vh_unit[(vh_desc.lines / VH_LINES) + 0];
    vh_poll_unit->action = &vh_svc;
    vh_poll_unit->flags = UNIT_DIS | UNIT_IDLE;
    sim_set_uname (vh_poll_unit, "VH-RCV-CON");
    vh_desc.uptr = vh_poll_unit;
    if (vh_xmit_unit)
        sim_cancel (vh_xmit_unit);
    vh_xmit_unit =  &vh_unit[(vh_desc.lines / VH_LINES) + 1];
    vh_xmit_unit->action = &vh_xmt_svc;
    vh_xmit_unit->flags = UNIT_DIS;
    sim_set_uname (vh_xmit_unit, "VH-XMIT");
    if (vh_timer_unit)
        sim_cancel (vh_timer_unit);
    vh_timer_unit = &vh_unit[(vh_desc.lines / VH_LINES) + 2];
    vh_timer_unit->action = &vh_timersvc;
    vh_timer_unit->flags = UNIT_DIS | UNIT_IDLE;
    sim_set_uname (vh_timer_unit, "VH-TIMER");
    for (i = 0; i < vh_desc.lines; i++) {
        vh_parm[i].tmln = &vh_ldsc[i];
        tmxr_set_line_unit (&vh_desc, i, vh_poll_unit);
        tmxr_set_line_output_unit (&vh_desc, i, vh_xmit_unit);
        }
    for (i = 0; i < VH_MUXES; i++) {
        if (i < vh_desc.lines/VH_LINES) {
            /* if Unibus, force DHU mode */
            if (UNIBUS)
                vh_unit[i].flags |= UNIT_MODEDHU;
            vh_unit[i].flags &= ~UNIT_DIS;
            vh_clear (i, TRUE);
        } else {
            vh_unit[i].flags |= UNIT_DIS;
        }
    }
    vh_rxi = vh_txi = 0;
    CLR_INT (VHRX);
    CLR_INT (VHTX);
    vh_dib.lnt = (vh_desc.lines / VH_LINES) * IOLN_VH;      /* set length */
    return (auto_config (dptr->name, (dptr->flags & DEV_DIS) ? 0 : vh_desc.lines/VH_LINES));
}


static t_stat vh_attach (   UNIT    *uptr,
                CONST char    *cptr   )
{
    if (uptr == vh_unit)
        return tmxr_attach (&vh_desc, uptr, cptr);
    return SCPE_NOATT;
}

static t_stat vh_detach (   UNIT    *uptr   )
{
    return (tmxr_detach (&vh_desc, uptr));
}

static t_stat vh_show_vec (FILE *st, UNIT *uptr, int32 arg, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;

return show_vec (st, uptr, ((mp->lines * 2) / VH_LINES), desc);
}

static void vh_detail_line (    FILE    *st,
                int32   vh,
                int32   chan    )
{
    int32   line;
    TMLX    *lp;

    line = (vh * VH_LINES) + chan;
    lp = &vh_parm[line];
    fprintf (st, "\tline %d\tlpr %06o, lnctrl %06o, lstat %06o\n",
        chan, lp->lpr, lp->lnctrl, lp->lstat);
    fprintf (st, "\t\ttbuffct %06o, tbuf1 %06o, tbuf2 %06o, txchar %06o\n",
        lp->tbuffct, lp->tbuf1, lp->tbuf2, lp->txchar);
    fprintf (st, "\t\ttmln rcve %d xmte %d\n",
        lp->tmln->rcve, lp->tmln->xmte);
}

static t_stat vh_show_detail (   FILE    *st,
                UNIT    *uptr,
                int32   val,
                CONST void    *desc   )
{
    int32   i, j;

    fprintf (st, "VH:\trxi %d, txi %d\n", vh_rxi, vh_txi);
    for (i = 0; i < vh_desc.lines/VH_LINES; i++) {
        fprintf (st, "VH%d:\tmode %s, crit %d\n", i,
            vh_unit[i].flags & UNIT_MODEDHU ? "DHU" : "DHV",
            vh_crit & (1 << i));
        fprintf (st, "\tCSR %06o, mcount %d, rbuf_idx %d, txq_idx %d\n",
            vh_csr[i], vh_mcount[i], rbuf_idx[i], txq_idx[i]);
        for (j = 0; j < VH_LINES; j++)
            vh_detail_line (st, i, j);
    }
    return (SCPE_OK);
}

static t_stat vh_show_rbuf (    FILE    *st,
                UNIT    *uptr,
                int32   val,
                CONST void    *desc   )
{
    int32   i;

    for (i = 0; i < rbuf_idx[0]; i++)
        fprintf (st, "%03d: %06o\n", i, vh_rbuf[0][i]);
    return (SCPE_OK);
}

static t_stat vh_show_txq ( FILE    *st,
                UNIT    *uptr,
                int32   val,
                CONST void    *desc   )
{
    int32   i;

    for (i = 0; i < txq_idx[0]; i++)
        fprintf (st, "%02d: %06o\n", i, vh_txq[0][i]);
    return (SCPE_OK);
}

/* SET LINES processor */

static t_stat vh_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newln = (int32) get_uint (cptr, 10, (VH_MUXES * VH_LINES), &r);
if ((r != SCPE_OK) || (newln == vh_desc.lines))
    return r;
if ((newln == 0) || (newln % VH_LINES))
    return sim_messagef (SCPE_ARG, "VH line count must be a multiple of %d\n", VH_LINES);
if (newln < vh_desc.lines) {
    for (i = newln, t = 0; i < vh_desc.lines; i++)
        t = t | vh_ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < vh_desc.lines; i++) {
        if (vh_ldsc[i].conn) {
            tmxr_linemsg (&vh_ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&vh_ldsc[i]);                /* reset line */
            }
        if ((i % VH_LINES) == (VH_LINES - 1))
            vh_clear (i / VH_LINES, TRUE);              /* reset mux */
        }
    }
vh_desc.lines = newln;
return vh_reset (&vh_dev);
}

/* SET DHU/DHV mode processor */

static t_stat vh_setmode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((UNIBUS) && (val != UNIT_MODEDHU))
    return SCPE_ARG;
return SCPE_OK;
}

/* SET LOG processor */

static t_stat vh_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
char gbuf[CBUFSIZE];
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, '=');
if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
    return SCPE_ARG;
ln = (int32) get_uint (gbuf, 10, vh_desc.lines, &r);
if ((r != SCPE_OK) || (ln >= vh_desc.lines))
    return SCPE_ARG;
return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

static t_stat vh_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
int32 ln;

if (cptr == NULL)
    return SCPE_ARG;
ln = (int32) get_uint (cptr, 10, (VH_MUXES * VH_LINES), &r);
if ((r != SCPE_OK) || (ln >= vh_desc.lines))
    return SCPE_ARG;
return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

static t_stat vh_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i;

for (i = 0; i < vh_desc.lines; i++) {
    fprintf (st, "line %d: ", i);
    tmxr_show_log (st, NULL, i, desc);
    fprintf (st, "\n");
    }
return SCPE_OK;
}

static t_stat vh_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char *devtype = (UNIBUS) ? "DH11" : "DHQ11";

fprintf (st, "%s Terminal Multiplexer (%s)\n\n", devtype, dptr->name);
fprintf (st, "The %s is an %d-line terminal multiplexer for %s systems.  Up to %d %s's\n", devtype, VH_LINES, (UNIBUS) ? "Unibus" : "Qbus", VH_MUXES, devtype);
fprintf (st, "are supported.\n\n");
fprintf (st, "The %s is a programmable asynchronous terminal multiplexer.  It has two\n", devtype);
fprintf (st, "programming modes: DHV11 and DHU11.  The register sets are compatible with\n");
fprintf (st, "these devices.  For transmission, the %s can be used in either DMA or\n", devtype);
fprintf (st, "programmed I/O mode.  For reception, there is a 256-entry FIFO for received\n");
fprintf (st, "characters, dataset status changes, and diagnostic information, and a\n");
fprintf (st, "programmable input interrupt timer (in DHU mode).  The device supports\n");
fprintf (st, "16-, 18-, and 22-bit addressing.  The %s can be programmed to filter\n", devtype);
fprintf (st, "and/or handle XON/XOFF characters independently of the processor.\n");
fprintf (st, "The %s supports programmable bit width (between 5 and 8) for the input\n", devtype);
fprintf (st, "and output of characters.\n\n");
fprintf (st, "By default, the DHV11 mode is selected, though DHU11 mode is recommended\n");
fprintf (st, "for applications that can support it.  The %s controller may be adjusted\n", dptr->name);
fprintf (st, "on a per controller basis as follows:\n\n");
fprintf (st, "   sim> SET %sn DHU               use the DHU programming mode\n", dptr->name);
fprintf (st, "   sim> SET %sn DHV               use the DHV programming mode\n\n", dptr->name);
fprintf (st, "DMA output is supported.  In a real %s, DMA is not initiated immediately\n", devtype);
fprintf (st, "upon receipt of TX.DMA.START but is dependent upon some internal processes.\n");
fprintf (st, "The %s controller mimics this behavior by default.  It may be desirable to\n", dptr->name);
fprintf (st, "alter this and start immediately, though this may not be compatible with all\n");
fprintf (st, "operating systems and diagnostics.  You can change the behavior of the %s\n", dptr->name);
fprintf (st, "controller as follows:\n\n");
fprintf (st, "   sim> SET %sn NORMAL            use normal DMA procedures\n", dptr->name);
fprintf (st, "   sim> SET %sn FASTDMA           set DMA to initiate immediately\n\n", dptr->name);
fprintf (st, "The number of lines (and therefore the number of %s devices\n", devtype);
fprintf (st, "simulated) can be changed with the command:\n\n");
fprintf (st, "   sim> SET %s LINES=n            set line count to n\n\n", dptr->name);
fprintf (st, "The line count must be a multiple of %d, with a maximum of %d.\n\n", VH_LINES, VH_LINES*VH_MUXES);
fprintf (st, "Modem and auto-disconnect support may be set on an individual controller\n");
fprintf (st, "basis.  The SET MODEM command directs the controller to report modem status\n");
fprintf (st, "changes to the computer.  The SET HANGUP command turns on active disconnects\n");
fprintf (st, "(disconnect session if computer clears Data Terminal Ready).\n\n");
fprintf (st, "   sim> SET %sn [NO]MODEM         disable/enable modem control\n", dptr->name);
fprintf (st, "   sim> SET %sn [NO]HANGUP        disable/enable disconnect on DTR drop\n\n", dptr->name);
fprintf (st, "Once the %s devuce is attached and the simulator is running, the %s will\n", dptr->name, dptr->name);
fprintf (st, "listen for connections on the specified port.  It assumes that the incoming\n");
fprintf (st, "connections are Telnet connections.  The connection remains open until\n");
fprintf (st, "disconnected by the simulated program, the Telnet client, a SET %s DISCONNECT\n", dptr->name);
fprintf (st, "command, or a DETACH %s command.\n\n", dptr->name);
fprintf (st, "Other special %s commands:\n\n", dptr->name);
fprintf (st, "   sim> SHOW %s CONNECTIONS       show current connections\n", dptr->name);
fprintf (st, "   sim> SHOW %s STATISTICS        show statistics for active connections\n", dptr->name);
fprintf (st, "   sim> SET %s DISCONNECT=linenumber  disconnects the specified line.\n\n", dptr->name);
fprintf (st, "The %s does not support save and restore.  All open connections are lost\n", devtype);
fprintf (st, "when the simulator shuts down or the %s is detached.\n\n", dptr->name);
vh_help_attach (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

static t_stat vh_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char *devtype = (UNIBUS) ? "DH11" : "DHQ11";

fprintf (st, "%s %s Terminal Multiplexer Attach Help\n\n", devtype, dptr->name);
return tmxr_attach_help (st, dptr, uptr, 1, cptr);
}

static const char *vh_description (DEVICE *dptr)
{
return (UNIBUS) ? "DH11 16-line Terminal Multiplexer" : "DHQ11 8-line Terminal Multiplexer";
}
