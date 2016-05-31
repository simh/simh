/* hp2100_mux.c: HP 2100 12920A terminal multiplexor simulator

   Copyright (c) 2002-2016, Robert M Supnik

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

   MUX,MUXL,MUXM        12920A terminal multiplexor

   13-May-16    JDB     Modified for revised SCP API function parameter types
   29-Jun-15    JDB     Corrected typo in RTS macro definition
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Jan-13    MP      Added DEV_MUX and additional DEVICE field values
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Removed DEV_NET to allow restoration of listening port
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   25-Nov-08    JDB     Revised for new multiplexer library SHOW routines
   09-Oct-08    JDB     "muxl_unit" defined one too many units (17 instead of 16)
   10-Sep-08    JDB     SHOW MUX CONN/STAT with SET MUX DIAG is no longer disallowed
   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   27-Aug-08    JDB     Added LINEORDER support
   12-Aug-08    JDB     Added BREAK deferral to allow RTE break-mode to work
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   16-Apr-08    JDB     Sync mux poll with console poll for idle compatibility
   06-Mar-07    JDB     Corrected "mux_sta" size from 16 to 21 elements
                        Fixed "muxc_reset" to clear lines 16-20
   26-Feb-07    JDB     Added debug printouts
                        Fixed control card OTx to set current channel number
                        Fixed to set "muxl_ibuf" in response to a transmit interrupt
                        Changed "mux_xbuf", "mux_rbuf" declarations from 8 to 16 bits
                        Fixed to set "mux_rchp" when a line break is received
                        Fixed incorrect "odd_par" table values
                        Reversed test in "RCV_PAR" to return "LIL_PAR" on odd parity
                        Fixed mux reset (ioCRS) to clear port parameters
                        Fixed to use PUT_DCH instead of PUT_CCH for data channel status
   10-Feb-07    JDB     Added DIAG/TERM modifiers to implement diagnostic mode
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   02-Jun-06    JDB     Fixed compiler warning for mux_ldsc init
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Jun-05    RMS     Added SET MUXLn DISCONNECT
   07-Oct-04    JDB     Allow enable/disable from any device
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   05-Jan-04    RMS     Revised for tmxr library changes
   21-Dec-03    RMS     Added invalid character screening for TSB (from Mike Gemeny)
   09-May-03    RMS     Added network device flag
   01-Nov-02    RMS     Added 7B/8B support
   22-Aug-02    RMS     Updated for changes to sim_tmxr

   Reference:
   - 12920A Asynchronous Multiplexer Interface Kits Operating and Service Manual
        (12920-90001, Oct-1972)


   The 12920A was a 16-channel asynchronous terminal multiplexer.  It supported
   direct-connected terminals as well as modems at speeds up to 2400 baud.  It
   was the primary terminal multiplexer for the HP 2000 series of Time-Shared
   BASIC systems.

   The multiplexer was implemented as a three-card set consisting of a lower
   data card, an upper data card, and a modem control card.  Under simulation,
   these are implemented by three devices:

     MUXL   lower data card (lines)
     MUX    upper data card (scanner)
     MUXM   control card (modem control)

   The lower and upper data cards must be in adjacent I/O slots.  The control
   card may be placed in any slot, although in practice it was placed in the
   slot above the upper data card, so that all three cards were physically
   together.

   The 12920A supported one or two control cards (two cards were used with
   801-type automatic dialers).  Under simulation, only one control card is
   supported.

   Implementation notes:

    1. If a BREAK is detected during an input poll, and we are not in diagnostic
       mode, we defer recognition until either a character is output or a second
       successive input poll occurs.  This is necessary for RTE break-mode
       operation.  Without this deferral, a BREAK during output would be ignored
       by the RTE driver, making it impossible to stop a long listing.

       The problem is due to timing differences between simulated and real time.
       The RTE multiplexer driver is a privileged driver.  Privileged drivers
       bypass RTE to provide rapid interrupt handling.  To inform RTE that an
       operation is complete, e.g., that a line has been written, the interrupt
       section of the driver sets a device timeout of one clock tick (10
       milliseconds).  When that timeout occurs, RTE is entered normally to
       complete the I/O transaction.  While the completion timeout is pending,
       the driver ignores any further interrupts from the multiplexer line.

       The maximum communication rate for the multiplexer is 2400 baud, or
       approximately 4.2 milliseconds per character transferred.  A typical line
       of 20 characters would therefore take ~85 milliseconds, plus the 10
       millisecond completion timeout, or about 95 milliseconds total.  BREAK
       recognition would be ignored for roughly 10% of that time.  At lower baud
       rates, recognition would be ignored for a correspondingly smaller
       percentage of the time.

       However, SIMH uses an optimized timing of 500 instructions per character
       transfer, rather than the ~6600 instructions that a character transfer
       should take, and so a typical 20-character line will take about 11,000
       instructions.  On the other hand, the clock tick is calibrated to real
       time, and 10 milliseconds of real time takes about 420,000 instructions
       on a 2.0 GHz PC.  To be recognized, then, the BREAK key must be pressed
       in a window that is open for about 2.5% of the time.  Therefore, the
       BREAK key will be ignored about 97.5% of the time, and RTE break-mode
       effectively will not work.

       Deferring BREAK recognition until the next character is output ensures
       that the BREAK interrupt will be accepted (the simulator delivers input
       interrupts before output interrupts, so the BREAK interrupt arrives
       before the output character transmit interrupt).  If an output operation
       is not in progress, then the BREAK will be recognized at the next input
       poll.
*/


#include <ctype.h>

#include "hp2100_defs.h"
#include "sim_tmxr.h"


/* Unit references */

#define MUX_LINES       16                              /* number of user lines */
#define MUX_ILINES      5                               /* number of diag rcv only lines */


/* Service times */

#define MUXL_WAIT       500


/* Unit flags */

#define UNIT_V_MDM      (TTUF_V_UF + 0)                 /* modem control */
#define UNIT_V_DIAG     (TTUF_V_UF + 1)                 /* loopback diagnostic */
#define UNIT_MDM        (1 << UNIT_V_MDM)
#define UNIT_DIAG       (1 << UNIT_V_DIAG)

/* Debug flags */

#define DEB_CMDS        (1 << 0)                        /* Command initiation and completion */
#define DEB_CPU         (1 << 1)                        /* CPU I/O */
#define DEB_XFER        (1 << 2)                        /* Socket receive and transmit */

/* Channel number (OTA upper, LIA lower or upper) */

#define MUX_V_CHAN      10                              /* channel num */
#define MUX_M_CHAN      037
#define MUX_CHAN(x)     (((x) >> MUX_V_CHAN) & MUX_M_CHAN)

/* OTA, lower = parameters or data */

#define OTL_P           0100000                         /* parameter */
#define OTL_TX          0040000                         /* transmit */
#define OTL_ENB         0020000                         /* enable */
#define OTL_TPAR        0010000                         /* xmt parity */
#define OTL_ECHO        0010000                         /* rcv echo */
#define OTL_DIAG        0004000                         /* diagnose */
#define OTL_SYNC        0004000                         /* sync */
#define OTL_V_LNT       8                               /* char length */
#define OTL_M_LNT       07
#define OTL_LNT(x)      (((x) >> OTL_V_LNT) & OTL_M_LNT)
#define OTL_V_BAUD      0                               /* baud rate */
#define OTL_M_BAUD      0377
#define OTL_BAUD(x)     (((x) >> OTL_V_BAUD) & OTL_M_BAUD)
#define OTL_CHAR        03777                           /* char mask */
#define OTL_PAR         0200                            /* char parity */

/* LIA, lower = received data */

#define LIL_PAR         0100000                         /* parity */
#define PUT_DCH(x)      (((x) & MUX_M_CHAN) << MUX_V_CHAN)
#define LIL_CHAR        01777                           /* character */

/* LIA, upper = status */

#define LIU_SEEK        0100000                         /* seeking NI */
#define LIU_DG          0000010                         /* diagnose */
#define LIU_BRK         0000004                         /* break */
#define LIU_LOST        0000002                         /* char lost */
#define LIU_TR          0000001                         /* trans/rcv */

/* OTA, control */

#define OTC_SCAN        0100000                         /* scan */
#define OTC_UPD         0040000                         /* update */
#define OTC_V_CHAN      10                              /* channel */
#define OTC_M_CHAN      017
#define OTC_CHAN(x)     (((x) >> OTC_V_CHAN) & OTC_M_CHAN)
#define OTC_EC2         0000200                         /* enable Cn upd */
#define OTC_EC1         0000100
#define OTC_C2          0000040                         /* Cn flops */
#define OTC_C1          0000020
#define OTC_V_C         4                               /* S1 to C1 */
#define OTC_ES2         0000010                         /* enb comparison */
#define OTC_ES1         0000004
#define OTC_V_ES        2
#define OTC_SS2         0000002                         /* SSn flops */
#define OTC_SS1         0000001
#define OTC_RW          (OTC_ES2|OTC_ES1|OTC_SS2|OTC_SS1)
#define RTS             OTC_C2                          /* C2 = rts */
#define DTR             OTC_C1                          /* C1 = dtr */

/* LIA, control */

#define LIC_MBO         0140000                         /* always set */
#define LIC_V_CHAN      10                              /* channel */
#define LIC_M_CHAN      017
#define PUT_CCH(x)      (((x) & OTC_M_CHAN) << OTC_V_CHAN)
#define LIC_I2          0001000                         /* change flags */
#define LIC_I1          0000400
#define LIC_S2          0000002                         /* Sn flops */
#define LIC_S1          0000001
#define LIC_V_I         8                               /* S1 to I1 */
#define CDET            LIC_S2                          /* S2 = cdet */
#define DSR             LIC_S1                          /* S1 = dsr */

#define LIC_TSTI(ch)    (((muxc_lia[ch] ^ muxc_ota[ch]) & \
                          ((muxc_ota[ch] & (OTC_ES2|OTC_ES1)) >> OTC_V_ES)) \
                         << LIC_V_I)


/* Program constants */

static const uint8 odd_par [256] = {
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 000-017 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 020-037 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 040-067 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 060-077 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 100-117 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 120-137 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 140-157 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 160-177 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 200-217 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 220-237 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 240-267 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 260-277 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 300-317 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 320-337 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 340-357 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1         /* 360-377 */
 };

#define RCV_PAR(x)      (odd_par[(x) & 0377] ? 0 : LIL_PAR)
#define XMT_PAR(x)      (odd_par[(x) & 0377] ? 0 : OTL_PAR)


/* Multiplexer controller state variables */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } muxl = { CLEAR, CLEAR, CLEAR };

uint32 muxl_ibuf = 0;                                   /* low in: rcv data */
uint32 muxl_obuf = 0;                                   /* low out: param */

uint32 muxu_ibuf = 0;                                   /* upr in: status */
uint32 muxu_obuf = 0;                                   /* upr out: chan */

struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    } muxc = { CLEAR, CLEAR, CLEAR };

uint32 muxc_chan = 0;                                   /* ctrl chan */
uint32 muxc_scan = 0;                                   /* ctrl scan */


/* Multiplexer per-line state variables */

uint16 mux_sta   [MUX_LINES + MUX_ILINES];              /* line status */
uint16 mux_rpar  [MUX_LINES + MUX_ILINES];              /* rcv param */
uint16 mux_xpar  [MUX_LINES];                           /* xmt param */
uint8  mux_rchp  [MUX_LINES + MUX_ILINES];              /* rcv chr pend */
uint8  mux_xdon  [MUX_LINES];                           /* xmt done */
uint8  muxc_ota  [MUX_LINES];                           /* ctrl: Cn,ESn,SSn */
uint8  muxc_lia  [MUX_LINES];                           /* ctrl: Sn */
uint8  mux_defer [MUX_LINES];                           /* break deferred flags */


/* Multiplexer per-line buffer variables */

uint16 mux_rbuf[MUX_LINES + MUX_ILINES];                /* rcv buf */
uint16 mux_xbuf[MUX_LINES];                             /* xmt buf */


/* Multiplexer local routines */

void mux_receive (int32 ln, int32 c, t_bool diag);
void mux_data_int (void);
void mux_ctrl_int (void);
void mux_diag (int32 c);


/* Multiplexer global routines */

IOHANDLER muxlio;
IOHANDLER muxuio;
IOHANDLER muxcio;

t_stat muxi_svc (UNIT *uptr);
t_stat muxo_svc (UNIT *uptr);
t_stat muxc_reset (DEVICE *dptr);
t_stat mux_attach (UNIT *uptr, CONST char *cptr);
t_stat mux_detach (UNIT *uptr);
t_stat mux_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc);


/* MUXL/MUXU device information block.

   The DIBs of adjacent cards must be contained in an array, so they are defined
   here and referenced in the lower and upper card device structures.
*/

DIB mux_dib[] = {
    { &muxlio, MUXL },
    { &muxuio, MUXU }
    };

#define muxl_dib mux_dib[0]
#define muxu_dib mux_dib[1]


/* MUXL data structures.

   muxl_dib     MUXL device information block
   muxl_unit    MUXL unit list
   muxl_reg     MUXL register list
   muxl_mod     MUXL modifier list
   muxl_dev     MUXL device descriptor
*/

TMXR mux_desc;

DEVICE muxl_dev;

UNIT muxl_unit[] = {
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT }
    };

REG muxl_reg[] = {
    { FLDATA (CTL, muxl.control, 0) },
    { FLDATA (FLG, muxl.flag,    0) },
    { FLDATA (FBF, muxl.flagbuf, 0) },
    { BRDATA (STA, mux_sta, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (RPAR, mux_rpar, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (XPAR, mux_xpar, 8, 16, MUX_LINES) },
    { BRDATA (RBUF, mux_rbuf, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (XBUF, mux_xbuf, 8, 16, MUX_LINES) },
    { BRDATA (RCHP, mux_rchp, 8, 1, MUX_LINES + MUX_ILINES) },
    { BRDATA (XDON, mux_xdon, 8, 1, MUX_LINES) },
    { BRDATA (BDFR, mux_defer, 8, 1, MUX_LINES) },
    { URDATA (TIME, muxl_unit[0].wait, 10, 24, 0,
              MUX_LINES, REG_NZ + PV_LEFT) },
    { ORDATA (SC, muxl_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, muxl_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB muxl_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL },

    { UNIT_MDM, UNIT_MDM, "dataset",    "DATASET",   NULL, NULL, NULL },
    { UNIT_MDM,        0, "no dataset", "NODATASET", NULL, NULL, NULL },

    { MTAB_XTD | MTAB_VUN | MTAB_NC, 0, "LOG", "LOG",   &tmxr_set_log,   &tmxr_show_log, &mux_desc },
    { MTAB_XTD | MTAB_VUN | MTAB_NC, 0, NULL,  "NOLOG", &tmxr_set_nolog, NULL,           &mux_desc },

    { MTAB_XTD | MTAB_VUN,            0, NULL,    "DISCONNECT", &tmxr_dscln, NULL,        &mux_desc },
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",         &hp_setsc,   &hp_showsc,  &muxl_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO",      &hp_setdev,  &hp_showdev, &muxl_dev },

    { 0 }
    };

DEVICE muxl_dev = {
    "MUXL",                                 /* device name */
    muxl_unit,                              /* unit array */
    muxl_reg,                               /* register array */
    muxl_mod,                               /* modifier array */
    MUX_LINES,                              /* number of units */
    10,                                     /* address radix */
    31,                                     /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    8,                                      /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &muxc_reset,                            /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &muxl_dib,                              /* device information block */
    DEV_DISABLE,                            /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL,                                   /* logical device name */
    NULL,                                   /* help routine */
    NULL,                                   /* help attach routine*/
    NULL                                    /* help context */
    };


/* MUXU data structures

   mux_order    MUX line connection order table
   mux_ldsc     MUX terminal multiplexer line descriptors
   mux_desc     MUX terminal multiplexer device descriptor

   muxu_dib     MUXU device information block
   muxu_unit    MUXU unit list
   muxu_reg     MUXU register list
   muxu_mod     MUXU modifier list
   muxu_deb     MUXU debug list
   muxu_dev     MUXU device descriptor
*/

DEVICE muxu_dev;

int32 mux_order [MUX_LINES] = { -1 };                       /* connection order */
TMLN  mux_ldsc  [MUX_LINES] = { { 0 } };                    /* line descriptors */
TMXR  mux_desc = { MUX_LINES, 0, 0, mux_ldsc, mux_order };  /* device descriptor */

UNIT muxu_unit = { UDATA (&muxi_svc, UNIT_ATTABLE, 0), POLL_FIRST };

REG muxu_reg[] = {
    { ORDATA (IBUF, muxu_ibuf, 16) },
    { ORDATA (OBUF, muxu_obuf, 16) },
    { ORDATA (SC, muxu_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, muxu_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB muxu_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", &mux_setdiag, NULL,            NULL },
    { UNIT_DIAG, 0,         "terminal mode",   "TERM", &mux_setdiag, NULL,            NULL },
    { UNIT_ATT,  UNIT_ATT,  "",                NULL,   NULL,         &tmxr_show_summ, &mux_desc },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "LINEORDER", "LINEORDER", &tmxr_set_lnorder, &tmxr_show_lnorder, &mux_desc },

    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,         NULL,        &tmxr_show_cstat, &mux_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS",  NULL,         NULL,        &tmxr_show_cstat, &mux_desc },
    { MTAB_XTD | MTAB_VDV,            1, NULL,          "DISCONNECT", &tmxr_dscln, NULL,             &mux_desc },
    { MTAB_XTD | MTAB_VDV,            1, "SC",          "SC",         &hp_setsc,   &hp_showsc,       &muxl_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO",       "DEVNO",      &hp_setdev,  &hp_showdev,      &muxl_dev },

    { 0 }
    };

DEBTAB muxu_deb [] = {
    { "CMDS", DEB_CMDS },
    { "CPU",  DEB_CPU },
    { "XFER", DEB_XFER },
    { NULL,   0 }
    };

DEVICE muxu_dev = {
    "MUX",                                  /* device name */
    &muxu_unit,                             /* unit array */
    muxu_reg,                               /* register array */
    muxu_mod,                               /* modifier array */
    1,                                      /* number of units */
    10,                                     /* address radix */
    31,                                     /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    8,                                      /* data width */
    &tmxr_ex,                               /* examine routine */
    &tmxr_dep,                              /* deposit routine */
    &muxc_reset,                            /* reset routine */
    NULL,                                   /* boot routine */
    &mux_attach,                            /* attach routine */
    &mux_detach,                            /* detach routine */
    &muxu_dib,                              /* device information block */
    DEV_DISABLE | DEV_DEBUG  | DEV_MUX,     /* device flags */
    0,                                      /* debug control flags */
    muxu_deb,                               /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL,                                   /* logical device name */
    NULL,                                   /* help routine */
    NULL,                                   /* help attach routine*/
    (void *) &mux_desc                      /* help context */
    };


/* MUXC data structures.

   muxc_dib     MUXC device information block
   muxc_unit    MUXC unit list
   muxc_reg     MUXC register list
   muxc_mod     MUXC modifier list
   muxc_dev     MUXC device descriptor
*/

DEVICE muxc_dev;

DIB muxc_dib = { &muxcio, MUXC };

UNIT muxc_unit = { UDATA (NULL, 0, 0) };

REG muxc_reg[] = {
    { FLDATA (CTL, muxc.control, 0) },
    { FLDATA (FLG, muxc.flag,    0) },
    { FLDATA (FBF, muxc.flagbuf, 0) },
    { FLDATA (SCAN, muxc_scan, 0) },
    { ORDATA (CHAN, muxc_chan, 4) },
    { BRDATA (DSO, muxc_ota, 8, 6, MUX_LINES) },
    { BRDATA (DSI, muxc_lia, 8, 2, MUX_LINES) },
    { ORDATA (SC, muxc_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, muxc_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB muxc_mod[] = {
    { MTAB_XTD | MTAB_VDV,            0, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &muxc_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &muxc_dev },
    { 0 }
    };

DEVICE muxc_dev = {
    "MUXM",                                 /* device name */
    &muxc_unit,                             /* unit array */
    muxc_reg,                               /* register array */
    muxc_mod,                               /* modifier array */
    1,                                      /* number of units */
    10,                                     /* address radix */
    31,                                     /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    8,                                      /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &muxc_reset,                            /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &muxc_dib,                              /* device information block */
    DEV_DISABLE,                            /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL,                                   /* logical device name */
    NULL,                                   /* help routine */
    NULL,                                   /* help attach routine*/
    NULL                                    /* help context */
    };


/* Lower data card I/O signal handler.

   Implementation notes:

    1. The operating manual says that "at least 100 milliseconds of CLC 0s must
       be programmed" by systems employing the multiplexer to ensure that the
       multiplexer resets.  In practice, such systems issue 128K CLC 0
       instructions.  As we provide debug logging of multiplexer resets, a CRS
       counter is used to ensure that only one debug line is printed in response
       to these 128K CRS invocations.
*/

uint32 muxlio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
int32 ln;
const char *hold_or_clear = (signal_set & ioCLF ? ",C" : "");
static uint32 crs_count = 0;                            /* cntr for ioCRS repeat */
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

if (crs_count && !(signal_set & ioCRS)) {               /* counting CRSes and not present? */
    if (DEBUG_PRI (muxu_dev, DEB_CMDS))                 /* report reset count */
        fprintf (sim_deb, ">>MUXl cmds: [CRS] Multiplexer reset %d times\n",
                          crs_count);

    crs_count = 0;                                      /* clear counter */
    }

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            muxl.flag = muxl.flagbuf = CLEAR;

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fputs (">>MUXl cmds: [CLF] Flag cleared\n", sim_deb);

            mux_data_int ();                            /* look for new int */
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            muxl.flag = muxl.flagbuf = SET;

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fputs (">>MUXl cmds: [STF] Flag set\n", sim_deb);
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (muxl);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (muxl);
            break;


        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, muxl_ibuf);  /* merge in return status */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                fprintf (sim_deb, ">>MUXl cpu:  [LIx%s] Data = %06o\n", hold_or_clear, muxl_ibuf);
            break;


        case ioIOO:                                     /* I/O data output */
            muxl_obuf = IODATA (stat_data);             /* store data */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                if (muxl_obuf & OTL_P)
                    fprintf (sim_deb, ">>MUXl cpu:  [OTx%s] Parameter = %06o\n", hold_or_clear, muxl_obuf);
                else
                    fprintf (sim_deb, ">>MUXl cpu:  [OTx%s] Data = %06o\n", hold_or_clear, muxl_obuf);
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            muxl.flag = muxl.flagbuf = SET;             /* set flag andflag buffer */
            break;


        case ioCRS:                                     /* control reset */
            if (crs_count == 0) {                       /* first reset? */
                muxl.control = CLEAR;                   /* clear control flip-flop */

                for (ln = 0; ln < MUX_LINES; ln++) {    /* clear transmit info */
                    mux_xbuf[ln] = mux_xpar[ln] = 0;
                    muxc_ota[ln] = muxc_lia[ln] = mux_xdon[ln] = 0;
                    }

                for (ln = 0; ln < (MUX_LINES + MUX_ILINES); ln++) {
                    mux_rbuf[ln] = mux_rpar[ln] = 0;    /* clear receive info */
                    mux_sta[ln] = mux_rchp[ln] = 0;
                    }
                }

            crs_count = crs_count + 1;                  /* increment count */
            break;


        case ioCLC:                                     /* clear control flip-flop */
            muxl.control = CLEAR;

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fprintf (sim_deb, ">>MUXl cmds: [CLC%s] Data interrupt inhibited\n", hold_or_clear);
            break;


        case ioSTC:                                                 /* set control flip-flop */
            muxl.control = SET;                                     /* set control */

            ln = MUX_CHAN (muxu_obuf);                              /* get chan # */

            if (muxl_obuf & OTL_TX) {                               /* transmit? */
                if (ln < MUX_LINES) {                               /* line valid? */
                    if (muxl_obuf & OTL_P) {                        /* parameter? */
                        mux_xpar[ln] = (uint16) muxl_obuf;          /* store param value */
                        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                            fprintf (sim_deb,
                                ">>MUXl cmds: [STC%s] Transmit channel %d parameter %06o stored\n",
                                hold_or_clear, ln, muxl_obuf);
                        }

                    else {                                          /* data */
                        if (mux_xpar[ln] & OTL_TPAR)                /* parity requested? */
                            muxl_obuf =                             /* add parity bit */
                                muxl_obuf & ~OTL_PAR |
                                XMT_PAR(muxl_obuf);
                        mux_xbuf[ln] = (uint16) muxl_obuf;          /* load buffer */

                        if (sim_is_active (&muxl_unit[ln])) {       /* still working? */
                            mux_sta[ln] = mux_sta[ln] | LIU_LOST;   /* char lost */
                            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                                fprintf (sim_deb, ">>MUXl cmds: [STC%s] Transmit channel %d data overrun\n",
                                                  hold_or_clear, ln);
                            }
                        else {
                            if (muxu_unit.flags & UNIT_DIAG)        /* loopback? */
                                mux_ldsc[ln].conn = 1;              /* connect this line */
                            sim_activate (&muxl_unit[ln], muxl_unit[ln].wait);
                            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                                fprintf (sim_deb, ">>MUXl cmds: [STC%s] Transmit channel %d data %06o scheduled\n",
                                                  hold_or_clear, ln, muxl_obuf);
                            }
                        }
                    }
                else if (DEBUG_PRI (muxu_dev, DEB_CMDS))            /* line invalid */
                    fprintf (sim_deb, ">>MUXl cmds: [STC%s] Transmit channel %d invalid\n", hold_or_clear, ln);
                }

            else                                                    /* receive */
                if (ln < (MUX_LINES + MUX_ILINES)) {                /* line valid? */
                    if (muxl_obuf & OTL_P) {                        /* parameter? */
                        mux_rpar[ln] = (uint16) muxl_obuf;          /* store param value */
                        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                            fprintf (sim_deb,
                                ">>MUXl cmds: [STC%s] Receive channel %d parameter %06o stored\n",
                                hold_or_clear, ln, muxl_obuf);
                        }

                    else if (DEBUG_PRI (muxu_dev, DEB_CMDS))        /* data (invalid action) */
                        fprintf (sim_deb,
                            ">>MUXl cmds: [STC%s] Receive channel %d parameter %06o invalid action\n",
                            hold_or_clear, ln, muxl_obuf);
                    }

                else if (DEBUG_PRI (muxu_dev, DEB_CMDS))            /* line invalid */
                    fprintf (sim_deb, ">>MUXl cmds: [STC%s] Receive channel %d invalid\n", hold_or_clear, ln);
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (muxl);                           /* set standard PRL signal */
            setstdIRQ (muxl);                           /* set standard IRQ signal */
            setstdSRQ (muxl);                           /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            muxl.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Upper data card I/O signal handler.

   The upper data card does not have a control, flag, or flag buffer flip-flop.
   It does not drive the IRQ or SRQ lines, so the I/O dispatcher does not handle
   the ioSIR signal.

   Implementation notes:

    1. The upper and lower data card hardware takes a number of actions in
       response to the CRS signal.  Under simulation, these actions are taken by
       the lower data card CRS handler.
*/

uint32 muxuio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, muxu_ibuf);  /* merge in return status */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                fprintf (sim_deb, ">>MUXu cpu:  [LIx] Status = %06o, channel = %d\n",
                                  muxu_ibuf, MUX_CHAN(muxu_ibuf));
            break;


        case ioIOO:                                     /* I/O data output */
            muxu_obuf = IODATA (stat_data);             /* store data */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                fprintf (sim_deb, ">>MUXu cpu:  [OTx] Data channel = %d\n", MUX_CHAN(muxu_obuf));
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Control card I/O signal handler.

   In diagnostic mode, the control signals C1 and C2 are looped back to status
   signals S1 and S2.  Changing the control signals may cause an interrupt, so a
   test is performed after IOO processing.
*/

uint32 muxcio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
const char *hold_or_clear = (signal_set & ioCLF ? ",C" : "");
uint16 data;
int32 ln, old;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            muxc.flag = muxc.flagbuf = CLEAR;

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fputs (">>MUXc cmds: [CLF] Flag cleared\n", sim_deb);

            mux_ctrl_int ();                            /* look for new int */
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            muxc.flag = muxc.flagbuf = SET;

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fputs (">>MUXc cmds: [STF] Flag set\n", sim_deb);
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (muxc);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (muxc);
            break;


        case ioIOI:                                                         /* I/O data input */
            data = (uint16) (LIC_MBO | PUT_CCH (muxc_chan) |                /* mbo, chan num */
                             LIC_TSTI (muxc_chan) |                         /* I2, I1 */
                             (muxc_ota[muxc_chan] & (OTC_ES2 | OTC_ES1)) |  /* ES2, ES1 */
                             (muxc_lia[muxc_chan] & (LIC_S2 | LIC_S1)));    /* S2, S1 */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                fprintf (sim_deb, ">>MUXc cpu:  [LIx%s] Status = %06o, channel = %d\n",
                                  hold_or_clear, data, muxc_chan);

            muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;               /* incr channel */
            stat_data = IORETURN (SCPE_OK, data);                   /* merge in return status */
            break;


        case ioIOO:                                             /* I/O data output */
            data = IODATA (stat_data);                          /* clear supplied status */
            ln = muxc_chan = OTC_CHAN (data);                   /* set channel */

            if (data & OTC_SCAN) muxc_scan = 1;                 /* set scan flag */
            else muxc_scan = 0;

            if (data & OTC_UPD) {                               /* update? */
                old = muxc_ota[ln];                             /* save prior val */
                muxc_ota[ln] =                                  /* save ESn,SSn */
                    (muxc_ota[ln] & ~OTC_RW) | (data & OTC_RW);

                if (data & OTC_EC2)                             /* if EC2, upd C2 */
                    muxc_ota[ln] =
                        (muxc_ota[ln] & ~OTC_C2) | (data & OTC_C2);

                if (data & OTC_EC1)                             /* if EC1, upd C1 */
                    muxc_ota[ln] =
                        (muxc_ota[ln] & ~OTC_C1) | (data & OTC_C1);

                if (muxu_unit.flags & UNIT_DIAG)                /* loopback? */
                    muxc_lia[ln ^ 1] =                          /* set S1, S2 to C1, C2 */
                        (muxc_lia[ln ^ 1] & ~(LIC_S2 | LIC_S1)) |
                        (muxc_ota[ln] & (OTC_C1 | OTC_C2)) >> OTC_V_C;

                else if ((muxl_unit[ln].flags & UNIT_MDM) &&    /* modem ctrl? */
                    (old & DTR) &&                              /* DTR drop? */
                    !(muxc_ota[ln] & DTR)) {
                    tmxr_linemsg (&mux_ldsc[ln], "\r\nLine hangup\r\n");
                    tmxr_reset_ln (&mux_ldsc[ln]);              /* reset line */
                    muxc_lia[ln] = 0;                           /* dataset off */
                    }
                }                                               /* end update */

            if (DEBUG_PRI (muxu_dev, DEB_CPU))
                fprintf (sim_deb, ">>MUXc cpu:  [OTx%s] Parameter = %06o, channel = %d\n",
                                  hold_or_clear, data, ln);

            if ((muxu_unit.flags & UNIT_DIAG) && (!muxc.flag))  /* loopback and flag clear? */
                mux_ctrl_int ();                                /* status chg may interrupt */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            muxc.flag = muxc.flagbuf = SET;             /* set flag and flag buffer */
            break;


        case ioCRS:                                     /* control reset */
        case ioCLC:                                     /* clear control flip-flop */
            muxc.control = CLEAR;
            break;


        case ioSTC:                                     /* set control flip-flop */
            muxc.control = SET;
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (muxc);                           /* set standard PRL signal */
            setstdIRQ (muxc);                           /* set standard IRQ signal */
            setstdSRQ (muxc);                           /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            muxc.flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service - receive side

   Poll for new connections
   Poll all active lines for input
*/

t_stat muxi_svc (UNIT *uptr)
{
int32 ln, c;
t_bool loopback;

loopback = ((muxu_unit.flags & UNIT_DIAG) != 0);        /* diagnostic mode? */

if (!loopback) {                                        /* terminal mode? */
    if (uptr->wait == POLL_FIRST)                       /* first poll? */
        uptr->wait = sync_poll (INITIAL);               /* initial synchronization */
    else                                                /* not first */
        uptr->wait = sync_poll (SERVICE);               /* continue synchronization */

    sim_activate (uptr, uptr->wait);                    /* continue polling */

    ln = tmxr_poll_conn (&mux_desc);                    /* look for connect */

    if (ln >= 0) {                                      /* got one? */
        if ((muxl_unit[ln].flags & UNIT_MDM) &&         /* modem ctrl? */
            (muxc_ota[ln] & DTR))                       /* DTR? */
            muxc_lia[ln] = muxc_lia[ln] | CDET;         /* set cdet */
        muxc_lia[ln] = muxc_lia[ln] | DSR;              /* set dsr */
        mux_ldsc[ln].rcve = 1;                          /* rcv enabled */
        }
    tmxr_poll_rx (&mux_desc);                           /* poll for input */
    }

for (ln = 0; ln < MUX_LINES; ln++) {                    /* loop thru lines */
    if (mux_ldsc[ln].conn) {                            /* connected? */
        if (loopback) {                                 /* diagnostic mode? */
            c = mux_xbuf[ln ^ 1] & OTL_CHAR;            /* get char from xmit line */
            if (c == 0)                                 /* all char bits = 0? */
                c = c | SCPE_BREAK;                     /* set break flag */
            mux_ldsc[ln].conn = 0;                      /* clear connection */
            }

        else if (mux_defer[ln])                         /* break deferred? */
            c = SCPE_BREAK;                             /* supply it now */

        else
            c = tmxr_getc_ln (&mux_ldsc[ln]);           /* get char from Telnet */

        if (c)                                          /* valid char? */
            mux_receive (ln, c, loopback);              /* process it */
        }

    else                                                /* not connected */
        if (!loopback)                                  /* terminal mode? */
            muxc_lia[ln] = 0;                           /* line disconnected */
    }

if (!muxl.flag) mux_data_int ();                        /* scan for data int */
if (!muxc.flag) mux_ctrl_int ();                        /* scan modem */
return SCPE_OK;
}


/* Unit service - transmit side */

t_stat muxo_svc (UNIT *uptr)
{
int32 c, fc, ln, altln;
t_bool loopback;

ln = uptr - muxl_unit;                                  /* line # */
altln = ln ^ 1;                                         /* alt. line for diag mode */

fc = mux_xbuf[ln] & OTL_CHAR;                           /* full character data */
c = fc & 0377;                                          /* Telnet character data */

loopback = ((muxu_unit.flags & UNIT_DIAG) != 0);        /* diagnostic mode? */

if (mux_ldsc[ln].conn) {                                /* connected? */
    if (mux_ldsc[ln].xmte) {                            /* xmt enabled? */
        if (loopback)                                   /* diagnostic mode? */
            mux_ldsc[ln].conn = 0;                      /* clear connection */

        else if (mux_defer[ln])                         /* break deferred? */
            mux_receive (ln, SCPE_BREAK, loopback);     /* process it now */

        if ((mux_xbuf[ln] & OTL_SYNC) == 0) {           /* start bit 0? */
            TMLN *lp = &mux_ldsc[ln];                   /* get line */
            c = sim_tt_outcvt (c, TT_GET_MODE (muxl_unit[ln].flags));

            if (mux_xpar[ln] & OTL_DIAG)                /* xmt diagnose? */
                mux_diag (fc);                          /* before munge */

            if (loopback) {                             /* diagnostic mode? */
                mux_ldsc[altln].conn = 1;               /* set recv connection */
                sim_activate (&muxu_unit, 1);           /* schedule receive */
                }

            else {                                      /* no loopback */
                if (c >= 0)                             /* valid? */
                    tmxr_putc_ln (lp, c);               /* output char */
                tmxr_poll_tx (&mux_desc);               /* poll xmt */
                }
            }

        mux_xdon[ln] = 1;                               /* set for xmit irq */

        if (DEBUG_PRI (muxu_dev, DEB_XFER) && (loopback | (c >= 0)))
            fprintf (sim_deb, ">>MUXl xfer: Line %d character %s sent\n",
                ln, fmt_char ((uint8) (loopback ? fc : c)));
        }

    else {                                              /* buf full */
        tmxr_poll_tx (&mux_desc);                       /* poll xmt */
        sim_activate (uptr, muxl_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }

if (!muxl.flag) mux_data_int ();                        /* scan for int */
return SCPE_OK;
}


/* Process a character received from a multiplexer port */

void mux_receive (int32 ln, int32 c, t_bool diag)
{
if (c & SCPE_BREAK) {                                   /* break? */
    if (mux_defer[ln] || diag) {                        /* break deferred or diagnostic mode? */
        mux_defer[ln] = 0;                              /* process now */
        mux_rbuf[ln] = 0;                               /* break returns NUL */
        mux_sta[ln] = mux_sta[ln] | LIU_BRK;            /* set break status */

        if (DEBUG_PRI (muxu_dev, DEB_XFER))
            if (diag)
                fputs (">>MUXl xfer: Break detected\n", sim_deb);
            else
                fputs (">>MUXl xfer: Deferred break processed\n", sim_deb);
        }

    else {
        mux_defer[ln] = 1;                              /* defer break */

        if (DEBUG_PRI (muxu_dev, DEB_XFER))
            fputs (">>MUXl xfer: Break detected and deferred\n", sim_deb);

        return;
        }
    }
else {                                                  /* normal */
    if (mux_rchp[ln])                                   /* char already pending? */
        mux_sta[ln] = mux_sta[ln] | LIU_LOST;

    if (!diag) {                                        /* terminal mode? */
        c = sim_tt_inpcvt (c, TT_GET_MODE (muxl_unit[ln].flags));
        if (mux_rpar[ln] & OTL_ECHO) {                  /* echo? */
            TMLN *lp = &mux_ldsc[ln];                   /* get line */
            tmxr_putc_ln (lp, c);                       /* output char */
            tmxr_poll_tx (&mux_desc);                   /* poll xmt */
            }
        }
    mux_rbuf[ln] = (uint16) c;                          /* save char */
    }

mux_rchp[ln] = 1;                                       /* char pending */

if (DEBUG_PRI (muxu_dev, DEB_XFER))
    fprintf (sim_deb, ">>MUXl xfer: Line %d character %s received\n",
                      ln, fmt_char ((uint8) c));

if (mux_rpar[ln] & OTL_DIAG)                            /* diagnose this line? */
    mux_diag (c);                                       /* do diagnosis */

return;
}


/* Look for data interrupt */

void mux_data_int (void)
{
int32 i;

for (i = 0; i < MUX_LINES; i++) {                       /* rcv lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {       /* enabled, char? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i];           /* hi buf = stat */
        mux_rchp[i] = 0;                                /* clr char, stat */
        mux_sta[i] = 0;

        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MUXl cmds: Receive channel %d interrupt requested\n", i);

        muxlio (&muxl_dib, ioENF, 0);                   /* interrupt */
        return;
        }
    }
for (i = 0; i < MUX_LINES; i++) {                       /* xmt lines */
    if ((mux_xpar[i] & OTL_ENB) && mux_xdon[i]) {       /* enabled, done? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = last rcv char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i] | LIU_TR;  /* hi buf = stat */
        mux_xdon[i] = 0;                                /* clr done, stat */
        mux_sta[i] = 0;

        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MUXl cmds: Transmit channel %d interrupt requested\n", i);

        muxlio (&muxl_dib, ioENF, 0);                   /* interrupt */
        return;
        }
    }
for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++) {    /* diag lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {           /* enabled, char? */
        muxl_ibuf = PUT_DCH (i) |                           /* lo buf = char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i] | LIU_DG;      /* hi buf = stat */
        mux_rchp[i] = 0;                                    /* clr char, stat */
        mux_sta[i] = 0;

        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MUXl cmds: Receive channel %d interrupt requested\n", i);

        muxlio (&muxl_dib, ioENF, 0);                       /* interrupt */
        return;
        }
    }
return;
}


/* Look for control interrupt

   If either of the incoming status bits does not match the stored status, and
   the corresponding mismatch is enabled, a control interrupt request is
   generated.  Depending on the scan flag, we check either all 16 lines or just
   the current line.  If an interrupt is requested, the channel counter
   indicates the interrupting channel.
*/

void mux_ctrl_int (void)
{
int32 i, line_count;

line_count = (muxc_scan ? MUX_LINES : 1);               /* check one or all lines */

for (i = 0; i < line_count; i++) {
    if (muxc_scan)                                      /* scanning? */
        muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;       /* step channel */
    if (LIC_TSTI (muxc_chan)) {                         /* status change? */

        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
            fprintf (sim_deb,
                ">>MUXc cmds: Control channel %d interrupt requested (poll = %d)\n",
                muxc_chan, i + 1);

        muxcio (&muxc_dib, ioENF, 0);                   /* set flag */
        break;
        }
    }
return;
}


/* Set diagnostic lines for given character */

void mux_diag (int32 c)
{
int32 i;

for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++) {
    if (c & SCPE_BREAK) {                               /* break? */
        mux_sta[i] = mux_sta[i] | LIU_BRK;
        mux_rbuf[i] = 0;                                /* no char */
        }
    else {
        if (mux_rchp[i]) mux_sta[i] = mux_sta[i] | LIU_LOST;
        mux_rchp[i] = 1;
        mux_rbuf[i] = (uint16) c;
        }
    }
return;
}


/* Reset an individual line */

static void mux_reset_ln (int32 i)
{
mux_rbuf[i] = mux_xbuf[i] = 0;                          /* clear state */
mux_rpar[i] = mux_xpar[i] = 0;
mux_rchp[i] = mux_xdon[i] = 0;
mux_sta[i] = mux_defer[i] = 0;
muxc_ota[i] = muxc_lia[i] = 0;                          /* clear modem */
if (mux_ldsc[i].conn &&                                 /* connected? */
    ((muxu_unit.flags & UNIT_DIAG) == 0))               /* term mode? */
    muxc_lia[i] = muxc_lia[i] | DSR |                   /* cdet, dsr */
    (muxl_unit[i].flags & UNIT_MDM? CDET: 0);
sim_cancel (&muxl_unit[i]);
return;
}


/* Reset routine for lower data, upper data, and control cards */

t_stat muxc_reset (DEVICE *dptr)
{
int32 i;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */

if (dptr == &muxc_dev) {                                /* make all consistent */
    hp_enbdis_pair (dptr, &muxl_dev);
    hp_enbdis_pair (dptr, &muxu_dev);
    }
else if (dptr == &muxl_dev) {
    hp_enbdis_pair (dptr, &muxc_dev);
    hp_enbdis_pair (dptr, &muxu_dev);
    }
else {
    hp_enbdis_pair (dptr, &muxc_dev);
    hp_enbdis_pair (dptr, &muxl_dev);
    }

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

muxc_chan = muxc_scan = 0;                              /* init modem scan */

if (muxu_unit.flags & UNIT_ATT) {                       /* master att? */
    muxu_unit.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&muxu_unit, muxu_unit.wait);          /* start Telnet poll immediately */
    }
else
    sim_cancel (&muxu_unit);                            /* else stop */

for (i = 0; i < MUX_LINES; i++)
    mux_reset_ln (i);                                   /* reset lines 0-15 */

for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++)  /* reset lines 16-20 */
    mux_rbuf[i] = mux_rpar[i] = mux_sta[i] = mux_rchp[i] = 0;

return SCPE_OK;
}


/* Attach master unit */

t_stat mux_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status = SCPE_OK;

if (muxu_unit.flags & UNIT_DIAG)                        /* diag mode? */
    return SCPE_NOFNC;                                  /* command not allowed */

status = tmxr_attach (&mux_desc, uptr, cptr);           /* attach */

if (status == SCPE_OK) {
    muxu_unit.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&muxu_unit, muxu_unit.wait);          /* start Telnet poll immediately */
    }

return status;
}


/* Detach master unit */

t_stat mux_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&mux_desc, uptr);                      /* detach */
for (i = 0; i < MUX_LINES; i++) mux_ldsc[i].rcve = 0;   /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}


/* Diagnostic/normal mode routine,

   Diagnostic testing wants to exercise as much of the regular simulation code
   as possible to ensure good test coverage.  Normally, input polling and output
   transmission only occurs on connected lines.  In diagnostic mode, line
   connection flags are set selectively to enable processing on the lines under
   test.  The alternative to this would require duplicating the send/receive
   code; the diagnostic would then test the copy but not the actual code used
   for normal character transfers, which is undesirable.

   Therefore, to enable diagnostic mode, we must force a disconnect of the
   master socket and any connected Telnet lines, which clears the connection
   flags on all lines.  Then we set the "transmission enabled" flags on all
   lines to enable output character processing for the diagnostic.  (Normally,
   all of the flags are set when the multiplexer is first attached.  Until then,
   the enable flags default to "not enabled," so we enable them explicitly
   here.)
*/

t_stat mux_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 ln;

if (val) {                                              /* set diag? */
    mux_detach (uptr);                                  /* detach lines */
    for (ln = 0; ln < MUX_LINES; ln++)                  /* enable transmission */
        mux_ldsc[ln].xmte = 1;                          /* on all lines */
    }
else {                                                  /* set term */
    for (ln = 0; ln < MUX_LINES; ln++)                  /* clear connections */
        mux_ldsc[ln].conn = 0;                          /* on all lines */
    }
return SCPE_OK;
}
