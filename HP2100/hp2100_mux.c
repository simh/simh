/* hp2100_mux.c: HP 2100 12920A terminal multiplexor simulator

   Copyright (c) 2002-2007, Robert M Supnik

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

   mux,muxl,muxc        12920A terminal multiplexor

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

   The 12920A consists of three separate devices

   mux                  scanner (upper data card)
   muxl                 lines (lower data card)
   muxm                 modem control (control card)

   The lower data card has no CMD flop; the control card has no CMD flop.
   The upper data card has none of the usual flops.

   Reference:
   - 12920A Asynchronous Multiplexer Interface Kits Operating and Service
            Manual (12920-90001, Oct-1972)
*/

#include "hp2100_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define MUX_LINES       16                              /* user lines */
#define MUX_ILINES      5                               /* diag rcv only */
#define UNIT_V_MDM      (TTUF_V_UF + 0)                 /* modem control */
#define UNIT_V_DIAG     (TTUF_V_UF + 1)                 /* loopback diagnostic */
#define UNIT_MDM        (1 << UNIT_V_MDM)
#define UNIT_DIAG       (1 << UNIT_V_DIAG)
#define MUXU_INIT_POLL  8000
#define MUXL_WAIT       500

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
#define RTS             OCT_C2                          /* C2 = rts */
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

/* Debug flags */

#define DEB_CMDS        (1 << 0)                        /* Command initiation and completion */
#define DEB_CPU         (1 << 1)                        /* CPU I/O */
#define DEB_XFER        (1 << 2)                        /* Socket receive and transmit */

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2], dev_srq[2];
extern FILE *sim_deb;

uint16 mux_sta[MUX_LINES + MUX_ILINES];                 /* line status */
uint16 mux_rpar[MUX_LINES + MUX_ILINES];                /* rcv param */
uint16 mux_xpar[MUX_LINES];                             /* xmt param */
uint16 mux_rbuf[MUX_LINES + MUX_ILINES];                /* rcv buf */
uint16 mux_xbuf[MUX_LINES];                             /* xmt buf */
uint8 mux_rchp[MUX_LINES + MUX_ILINES];                 /* rcv chr pend */
uint8 mux_xdon[MUX_LINES];                              /* xmt done */
uint8 muxc_ota[MUX_LINES];                              /* ctrl: Cn,ESn,SSn */
uint8 muxc_lia[MUX_LINES];                              /* ctrl: Sn */
uint32 mux_tps = 100;                                   /* polls/second */
uint32 muxl_ibuf = 0;                                   /* low in: rcv data */
uint32 muxl_obuf = 0;                                   /* low out: param */
uint32 muxu_ibuf = 0;                                   /* upr in: status */
uint32 muxu_obuf = 0;                                   /* upr out: chan */
uint32 muxc_chan = 0;                                   /* ctrl chan */
uint32 muxc_scan = 0;                                   /* ctrl scan */

TMLN mux_ldsc[MUX_LINES] = { { 0 } };                   /* line descriptors */
TMXR mux_desc = { MUX_LINES, 0, 0, mux_ldsc };          /* mux descriptor */

DEVICE muxl_dev, muxu_dev, muxc_dev;
int32 muxlio (int32 inst, int32 IR, int32 dat);
int32 muxuio (int32 inst, int32 IR, int32 dat);
int32 muxcio (int32 inst, int32 IR, int32 dat);
t_stat muxi_svc (UNIT *uptr);
t_stat muxo_svc (UNIT *uptr);
t_stat muxc_reset (DEVICE *dptr);
t_stat mux_attach (UNIT *uptr, char *cptr);
t_stat mux_detach (UNIT *uptr);
t_stat mux_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat mux_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat mux_show (FILE *st, UNIT *uptr, int32 val, void *desc);
void mux_data_int (void);
void mux_ctrl_int (void);
void mux_diag (int32 c);

static uint8 odd_par[256] = {
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

/* Debug flags table */

DEBTAB mux_deb[] = {
    { "CMDS", DEB_CMDS },
    { "CPU",  DEB_CPU },
    { "XFER", DEB_XFER },
    { NULL, 0 }  };

DIB mux_dib[] = {
    { MUXL, 0, 0, 0, 0, 0, &muxlio },
    { MUXU, 0, 0, 0, 0, 0, &muxuio }
    };

#define muxl_dib mux_dib[0]
#define muxu_dib mux_dib[1]

/* MUX data structures

   muxu_dev     MUX device descriptor
   muxu_unit    MUX unit descriptor
   muxu_reg     MUX register list
   muxu_mod     MUX modifiers list
*/

UNIT muxu_unit = { UDATA (&muxi_svc, UNIT_ATTABLE, 0), MUXU_INIT_POLL };

REG muxu_reg[] = {
    { ORDATA (IBUF, muxu_ibuf, 16) },
    { ORDATA (OBUF, muxu_obuf, 16) },
    { FLDATA (CMD, muxu_dib.cmd, 0), REG_HRO },
    { FLDATA (CTL, muxu_dib.ctl, 0), REG_HRO },
    { FLDATA (FLG, muxu_dib.flg, 0), REG_HRO },
    { FLDATA (FBF, muxu_dib.fbf, 0), REG_HRO },
    { FLDATA (SRQ, muxu_dib.srq, 0), REG_HRO },
    { ORDATA (DEVNO, muxu_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB muxu_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", &mux_setdiag },
    { UNIT_DIAG, 0, "terminal mode", "TERM", &mux_setdiag },
    { UNIT_ATT, UNIT_ATT, "connections", NULL, NULL, &mux_summ },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &mux_show, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &mux_show, NULL },
    { MTAB_XTD|MTAB_VDV, 1, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &muxl_dev },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &mux_desc },
    { 0 }
    };

DEVICE muxu_dev = {
    "MUX", &muxu_unit, muxu_reg, muxu_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &muxc_reset,
    NULL, &mux_attach, &mux_detach,
    &muxu_dib, DEV_NET | DEV_DISABLE | DEV_DEBUG,
    0, mux_deb, NULL, NULL
    };

/* MUXL data structures

   muxl_dev     MUXL device descriptor
   muxl_unit    MUXL unit descriptor
   muxl_reg     MUXL register list
   muxl_mod     MUXL modifiers list
*/

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
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT },
    { UDATA (&muxo_svc, TT_MODE_UC, 0), MUXL_WAIT }
    };

MTAB muxl_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { UNIT_MDM, 0, "no dataset", "NODATASET", NULL },
    { UNIT_MDM, UNIT_MDM, "dataset", "DATASET", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &mux_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &mux_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &mux_desc },
    { MTAB_XTD|MTAB_VDV, 1, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &muxl_dev },
    { 0 }
    };

REG muxl_reg[] = {
    { FLDATA (CMD, muxl_dib.cmd, 0), REG_HRO },
    { FLDATA (CTL, muxl_dib.ctl, 0) },
    { FLDATA (FLG, muxl_dib.flg, 0) },
    { FLDATA (FBF, muxl_dib.fbf, 0) },
    { FLDATA (SRQ, muxl_dib.srq, 0) },
    { BRDATA (STA, mux_sta, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (RPAR, mux_rpar, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (XPAR, mux_xpar, 8, 16, MUX_LINES) },
    { BRDATA (RBUF, mux_rbuf, 8, 16, MUX_LINES + MUX_ILINES) },
    { BRDATA (XBUF, mux_xbuf, 8, 16, MUX_LINES) },
    { BRDATA (RCHP, mux_rchp, 8, 1, MUX_LINES + MUX_ILINES) },
    { BRDATA (XDON, mux_xdon, 8, 1, MUX_LINES) },
    { URDATA (TIME, muxl_unit[0].wait, 10, 24, 0,
              MUX_LINES, REG_NZ + PV_LEFT) },
    { ORDATA (DEVNO, muxl_dib.devno, 6), REG_HRO },
    { NULL }
    };

DEVICE muxl_dev = {
    "MUXL", muxl_unit, muxl_reg, muxl_mod,
    MUX_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &muxc_reset,
    NULL, NULL, NULL,
    &muxl_dib, DEV_DISABLE
    };

/* MUXM data structures

   muxc_dev     MUXM device descriptor
   muxc_unit    MUXM unit descriptor
   muxc_reg     MUXM register list
   muxc_mod     MUXM modifiers list
*/

DIB muxc_dib = { MUXC, 0, 0, 0, 0, 0, &muxcio };

UNIT muxc_unit = { UDATA (NULL, 0, 0) };

REG muxc_reg[] = {
    { FLDATA (CMD, muxc_dib.cmd, 0), REG_HRO },
    { FLDATA (CTL, muxc_dib.ctl, 0) },
    { FLDATA (FLG, muxc_dib.flg, 0) },
    { FLDATA (FBF, muxc_dib.fbf, 0) },
    { FLDATA (SRQ, muxc_dib.srq, 0) },
    { FLDATA (SCAN, muxc_scan, 0) },
    { ORDATA (CHAN, muxc_chan, 4) },
    { BRDATA (DSO, muxc_ota, 8, 6, MUX_LINES) },
    { BRDATA (DSI, muxc_lia, 8, 2, MUX_LINES) },
    { ORDATA (DEVNO, muxc_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB muxc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &muxc_dev },
    { 0 }
    };

DEVICE muxc_dev = {
    "MUXM", &muxc_unit, muxc_reg, muxc_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &muxc_reset,
    NULL, NULL, NULL,
    &muxc_dib, DEV_DISABLE
    };

/* IO instructions: data cards

   Implementation note: the operating manual says that "at least 100
   milliseconds of CLC 0s must be programmed" by systems employing the
   multiplexer to ensure that the multiplexer resets.  In practice, such systems
   issue 128K CLC 0 instructions.  As we provide debug logging of multiplexer
   resets, a CRS counter is used to ensure that only one debug line is printed
   in response to these 128K CRS invocations.
*/

int32 muxlio (int32 inst, int32 IR, int32 dat)
{
int32 dev, ln;
t_bool is_crs;
static uint32 crs_count = 0;                            /* cntr for crs repeat */

dev = IR & I_DEVMASK;                                   /* get device no */
is_crs = FALSE;

switch (inst) {                                         /* case on opcode */

    case ioFLG:                                         /* flag clear/set */
        if ((IR & I_HC) == 0) { setFSR (dev); }         /* STF */
        break;

    case ioSFC:                                         /* skip flag clear */
        if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
        break;

    case ioSFS:                                         /* skip flag set */
        if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
        break;

    case ioOTX:                                         /* output */
        muxl_obuf = dat;                                /* store data */

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            if (dat & OTL_P)
                fprintf (sim_deb, ">>MUXl OTx: Parameter = %06o\n", dat);
            else
                fprintf (sim_deb, ">>MUXl OTx: Data = %06o\n", dat);
        break;

    case ioLIX:                                         /* load */
        dat = 0;

    case ioMIX:                                         /* merge */
        dat = dat | muxl_ibuf;

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            fprintf (sim_deb, ">>MUXl LIx: Data = %06o\n", dat);
        break;

    case ioCRS:                                         /* control reset */
        is_crs = TRUE;

        if (crs_count)                                  /* already reset? */
            break;                                      /* skip redundant clear */

        for (ln = 0; ln < MUX_LINES; ln++) {            /* clear transmit info */
            mux_xbuf[ln] = mux_xpar[ln] = 0;
            muxc_ota[ln] = muxc_lia[ln] = mux_xdon[ln] = 0;
            }

        for (ln = 0; ln < (MUX_LINES + MUX_ILINES); ln++) {
            mux_rbuf[ln] = mux_rpar[ln] = 0;            /* clear receive info */
            mux_sta[ln] = mux_rchp[ln] = 0;
            }                                           /* fall into CLC SC */

    case ioCTL:                                         /* control clear/set */
        if (IR & I_CTL) {                               /* CLC */
            clrCTL (dev);

            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                fprintf (sim_deb, ">>MUXl CLC: Data interrupt inhibited\n");
            }
        else {                                          /* STC */
            setCTL (dev);                               /* set ctl */
            ln = MUX_CHAN (muxu_obuf);                  /* get chan # */

            if (muxl_obuf & OTL_TX) {                   /* transmit? */
                if (ln < MUX_LINES) {                   /* line valid? */
                    if (muxl_obuf & OTL_P) {            /* parameter? */
                        mux_xpar[ln] = muxl_obuf;       /* store param value */
                        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                            fprintf (sim_deb,
                                ">>MUXl STC: Transmit channel %d parameter %06o stored\n",
                                ln, muxl_obuf);
    					}

                    else {                              /* data */
                        if (mux_xpar[ln] & OTL_TPAR)    /* parity requested? */
                            muxl_obuf =                 /* add parity bit */
                                muxl_obuf & ~OTL_PAR |
                                XMT_PAR(muxl_obuf);
                        mux_xbuf[ln] = muxl_obuf;       /* load buffer */

                        if (sim_is_active (&muxl_unit[ln])) {       /* still working? */
                            mux_sta[ln] = mux_sta[ln] | LIU_LOST;   /* char lost */
                            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                                fprintf (sim_deb,
                                    ">>MUXl STC: Transmit channel %d data overrun\n", ln);
    					    }
                        else {
                            if (muxu_unit.flags & UNIT_DIAG)        /* loopback? */
                                mux_ldsc[ln].conn = 1;              /* connect this line */
                            sim_activate (&muxl_unit[ln], muxl_unit[ln].wait);
                            if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                                fprintf (sim_deb,
                                    ">>MUXl STC: Transmit channel %d data %06o scheduled\n",
                                    ln, muxl_obuf);
    					    }
                        }
                    }
                else if (DEBUG_PRI (muxu_dev, DEB_CMDS))    /* line invalid */
                    fprintf (sim_deb, ">>MUXl STC: Transmit channel %d invalid\n", ln);
                }

            else                                        /* receive */
                if (ln < (MUX_LINES + MUX_ILINES)) {    /* line valid? */
                    if (muxl_obuf & OTL_P) {            /* parameter? */
                        mux_rpar[ln] = muxl_obuf;       /* store param value */
                        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
                            fprintf (sim_deb,
                                ">>MUXl STC: Receive channel %d parameter %06o stored\n",
                                ln, muxl_obuf);
                        }

                    else if (DEBUG_PRI (muxu_dev, DEB_CMDS))    /* data (invalid action) */
                        fprintf (sim_deb,
                            ">>MUXl STC: Receive channel %d parameter %06o invalid action\n",
                            ln, muxl_obuf);
                    }

                else if (DEBUG_PRI (muxu_dev, DEB_CMDS))    /* line invalid */
                    fprintf (sim_deb, ">>MUXl STC: Receive channel %d invalid\n", ln);
            }                                           /* end STC */
        break;

    default:
        break;
        }

if (is_crs)                                             /* control reset? */
    crs_count = crs_count + 1;                          /* increment count */

else if (crs_count) {                                   /* something else */
    if (DEBUG_PRI (muxu_dev, DEB_CMDS))                 /* report reset count */
        fprintf (sim_deb,
            ">>MUXl CRS: Multiplexer reset %d times\n", crs_count);
    crs_count = 0;                                      /* clear counter */
    }

if (IR & I_HC) {                                        /* H/C option */
    clrFSR (dev);                                       /* clear flag */
    mux_data_int ();                                    /* look for new int */
    }
return dat;
}

int32 muxuio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {                                         /* case on opcode */

    case ioOTX:                                         /* output */
        muxu_obuf = dat;                                /* store data */

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            fprintf (sim_deb, ">>MUXu OTx: Data channel = %d\n", MUX_CHAN(dat));
        break;

    case ioLIX:                                         /* load */
        dat = 0;

    case ioMIX:                                         /* merge */
        dat = dat | muxu_ibuf;

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            fprintf (sim_deb, ">>MUXu LIx: Status = %06o, channel = %d\n", dat, MUX_CHAN(dat));
        break;

    default:
        break;
    }

return dat;
}

/* IO instructions: control card

   In diagnostic mode, the control signals C1 and C2 are looped back to status
   signals S1 and S2.  Changing the control signals may cause an interrupt, so a
   test is performed after OTx processing.
 */

int32 muxcio (int32 inst, int32 IR, int32 dat)
{
int32 dev, ln, t, old;

dev = IR & I_DEVMASK;                                   /* get device no */
switch (inst) {                                         /* case on opcode */

    case ioFLG:                                         /* flag clear/set */
        if ((IR & I_HC) == 0) { setFSR (dev); }         /* STF */
        break;

    case ioSFC:                                         /* skip flag clear */
        if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
        break;

    case ioSFS:                                         /* skip flag set */
        if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
        break;

    case ioOTX:                                         /* output */
        ln = muxc_chan = OTC_CHAN (dat);                /* set channel */

        if (dat & OTC_SCAN) muxc_scan = 1;              /* set scan flag */
        else muxc_scan = 0;

        if (dat & OTC_UPD) {                            /* update? */
            old = muxc_ota[ln];                         /* save prior val */
            muxc_ota[ln] =                              /* save ESn,SSn */
                (muxc_ota[ln] & ~OTC_RW) | (dat & OTC_RW);

            if (dat & OTC_EC2)                          /* if EC2, upd C2 */
                muxc_ota[ln] =
                    (muxc_ota[ln] & ~OTC_C2) | (dat & OTC_C2);

            if (dat & OTC_EC1)                          /* if EC1, upd C1 */
                muxc_ota[ln] =
                    (muxc_ota[ln] & ~OTC_C1) | (dat & OTC_C1);

            if (muxu_unit.flags & UNIT_DIAG)            /* loopback? */
                muxc_lia[ln ^ 1] =                      /* set S1, S2 to C1, C2 */
                    (muxc_lia[ln ^ 1] & ~(LIC_S2 | LIC_S1)) |
                    (muxc_ota[ln] & (OTC_C1 | OTC_C2)) >> OTC_V_C;

            else if ((muxl_unit[ln].flags & UNIT_MDM) &&    /* modem ctrl? */
                (old & DTR) &&                              /* DTR drop? */
                !(muxc_ota[ln] & DTR)) {
                tmxr_linemsg (&mux_ldsc[ln], "\r\nLine hangup\r\n");
                tmxr_reset_ln (&mux_ldsc[ln]);          /* reset line */
                muxc_lia[ln] = 0;                       /* dataset off */
                }
            }                                           /* end update */

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            fprintf (sim_deb, ">>MUXc OTx: Parameter = %06o, channel = %d\n",
                dat, ln);

        if ((muxu_unit.flags & UNIT_DIAG) &&            /* loopback? */
            (!FLG(muxc_dib.devno)))                     /* flag clear? */
            mux_ctrl_int ();                            /* status chg may interrupt */
        break;

    case ioLIX:                                         /* load */
        dat = 0;

    case ioMIX:                                         /* merge */
        t = LIC_MBO | PUT_CCH (muxc_chan) |             /* mbo, chan num */
            LIC_TSTI (muxc_chan) |                      /* I2, I1 */
            (muxc_ota[muxc_chan] & (OTC_ES2 | OTC_ES1)) | /* ES2, ES1 */
            (muxc_lia[muxc_chan] & (LIC_S2 | LIC_S1));  /* S2, S1 */
        dat = dat | t;                                  /* return status */

        if (DEBUG_PRI (muxu_dev, DEB_CPU))
            fprintf (sim_deb, ">>MUXc LIx: Status = %06o, channel = %d\n",
                dat, muxc_chan);

        muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;       /* incr channel */
        break;

    case ioCRS:                                         /* control reset */
    case ioCTL:                                         /* ctrl clear/set */
        if (IR & I_CTL) { clrCTL (dev); }               /* CLC */
        else { setCTL (dev); }                          /* STC */
        break;

    default:
        break;
	}

if (IR & I_HC) {                                        /* H/C option */
    clrFSR (dev);                                       /* clear flag */
    mux_ctrl_int ();                                    /* look for new int */
    }
return dat;
}

/* Unit service - receive side

   Poll for new connections
   Poll all active lines for input
*/

t_stat muxi_svc (UNIT *uptr)
{
int32 ln, c, t;
t_bool loopback;

loopback = ((muxu_unit.flags & UNIT_DIAG) != 0);        /* diagnostic mode? */

if (!loopback) {                                        /* terminal mode? */
    if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;  /* attached? */
    t = sim_rtcn_calb (mux_tps, TMR_MUX);               /* calibrate */
    sim_activate (uptr, t);                             /* continue poll */
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

        else
            c = tmxr_getc_ln (&mux_ldsc[ln]);           /* get char from Telnet */

        if (c) {                                        /* valid char? */
            if (c & SCPE_BREAK) {                       /* break? */
                mux_sta[ln] = mux_sta[ln] | LIU_BRK;
                mux_rbuf[ln] = 0;                       /* no char */
                }
            else {                                      /* normal */
                if (mux_rchp[ln])                       /* char already pending? */
                    mux_sta[ln] = mux_sta[ln] | LIU_LOST;

                if (!loopback) {                        /* terminal mode? */
                    c = sim_tt_inpcvt (c, TT_GET_MODE (muxl_unit[ln].flags));
                    if (mux_rpar[ln] & OTL_ECHO) {      /* echo? */
                        TMLN *lp = &mux_ldsc[ln];       /* get line */
                        tmxr_putc_ln (lp, c);           /* output char */
                        tmxr_poll_tx (&mux_desc);       /* poll xmt */
                        }
                    }
                mux_rbuf[ln] = c;                       /* save char */
				}

            mux_rchp[ln] = 1;                           /* char pending */

            if (DEBUG_PRI (muxu_dev, DEB_XFER))
                fprintf (sim_deb, ">>MUXi svc: Line %d character %06o received\n",
                    ln, c);

            if (mux_rpar[ln] & OTL_DIAG) mux_diag (c);  /* rcv diag? */
            }                                           /* end if char */
        }                                               /* end if connected */

    else                                                /* not connected */
        if (!loopback)                                  /* terminal mode? */
            muxc_lia[ln] = 0;                           /* line disconnected */
    }                                                   /* end for */
if (!FLG (muxl_dib.devno)) mux_data_int ();             /* scan for data int */
if (!FLG (muxc_dib.devno)) mux_ctrl_int ();             /* scan modem */
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
            fprintf (sim_deb, ">>MUXo svc: Line %d character %06o sent\n",
                ln, (loopback ? fc : c));
        }

    else {                                              /* buf full */
        tmxr_poll_tx (&mux_desc);                       /* poll xmt */
        sim_activate (uptr, muxl_unit[ln].wait);        /* wait */
        return SCPE_OK;
        }
    }

if (!FLG (muxl_dib.devno)) mux_data_int ();             /* scan for int */
return SCPE_OK;
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
            fprintf (sim_deb, ">>MUXd irq: Receive channel %d interrupt requested\n", i);

        setFSR (muxl_dib.devno);                        /* interrupt */
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
            fprintf (sim_deb, ">>MUXd irq: Transmit channel %d interrupt requested\n", i);

        setFSR (muxl_dib.devno);                        /* interrupt */
        return;
        }
    }
for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++) {        /* diag lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {       /* enabled, char? */
        muxl_ibuf = PUT_DCH (i) |                       /* lo buf = char */
            mux_rbuf[i] & LIL_CHAR |
            RCV_PAR (mux_rbuf[i]);
        muxu_ibuf = PUT_DCH (i) | mux_sta[i] | LIU_DG;  /* hi buf = stat */
        mux_rchp[i] = 0;                                /* clr char, stat */
        mux_sta[i] = 0;

        if (DEBUG_PRI (muxu_dev, DEB_CMDS))
            fprintf (sim_deb, ">>MUXd irq: Receive channel %d interrupt requested\n", i);

        setFSR (muxl_dib.devno);
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
                ">>MUXc irq: Control channel %d interrupt requested (poll = %d)\n",
                muxc_chan, i + 1);

        setFSR (muxc_dib.devno);                        /* set flag */
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
        mux_rbuf[i] = c;
        }
    }
return;
}

/* Reset an individual line */

void mux_reset_ln (int32 i)
{
mux_rbuf[i] = mux_xbuf[i] = 0;                          /* clear state */
mux_rpar[i] = mux_xpar[i] = 0;
mux_rchp[i] = mux_xdon[i] = 0;
mux_sta[i] = 0;
muxc_ota[i] = muxc_lia[i] = 0;                          /* clear modem */
if (mux_ldsc[i].conn &&                                 /* connected? */
    ((muxu_unit.flags & UNIT_DIAG) == 0))               /* term mode? */
    muxc_lia[i] = muxc_lia[i] | DSR |                   /* cdet, dsr */
    (muxl_unit[i].flags & UNIT_MDM? CDET: 0);
sim_cancel (&muxl_unit[i]);
return;
}

/* Reset routine */

t_stat muxc_reset (DEVICE *dptr)
{
int32 i, t;

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
muxl_dib.cmd = muxl_dib.ctl = 0;                        /* init lower */
muxl_dib.flg = muxl_dib.fbf = muxl_dib.srq = 1;
muxu_dib.cmd = muxu_dib.ctl = 0;                        /* upper not */
muxu_dib.flg = muxu_dib.fbf = muxu_dib.srq = 0;         /* implemented */
muxc_dib.cmd = muxc_dib.ctl = 0;                        /* init ctrl */
muxc_dib.flg = muxc_dib.fbf = muxc_dib.srq = 1;
muxc_chan = muxc_scan = 0;                              /* init modem scan */
if (muxu_unit.flags & UNIT_ATT) {                       /* master att? */
    if (!sim_is_active (&muxu_unit)) {
        t = sim_rtcn_init (muxu_unit.wait, TMR_MUX);
        sim_activate (&muxu_unit, t);                   /* activate */
        }
    }
else sim_cancel (&muxu_unit);                           /* else stop */
for (i = 0; i < MUX_LINES; i++) mux_reset_ln (i);       /* reset lines 0-15 */
for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++)  /* reset lines 16-20 */
    mux_rbuf[i] = mux_rpar[i] = mux_sta[i] = mux_rchp[i] = 0;
return SCPE_OK;
}

/* Attach master unit */

t_stat mux_attach (UNIT *uptr, char *cptr)
{
t_stat r;
int32 t;

if (muxu_unit.flags & UNIT_DIAG)                        /* diag mode? */
    return SCPE_NOFNC;                                  /* command not allowed */

r = tmxr_attach (&mux_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK) return r;                             /* error */
t = sim_rtcn_init (muxu_unit.wait, TMR_MUX);
sim_activate (uptr, t);                                 /* start poll */
return SCPE_OK;
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

/* Diagnostic/normal mode routine

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

t_stat mux_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc)
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

/* Show summary processor */

t_stat mux_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

if (muxu_unit.flags & UNIT_DIAG)                        /* diag mode? */
    return SCPE_NOFNC;                                  /* command not allowed */

for (i = t = 0; i < MUX_LINES; i++) t = t + (mux_ldsc[i].conn != 0);
if (t == 1) fprintf (st, "1 connection");
else fprintf (st, "%d connections", t);
return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat mux_show (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

if (muxu_unit.flags & UNIT_DIAG)                        /* diag mode? */
    return SCPE_NOFNC;                                  /* command not allowed */

for (i = t = 0; i < MUX_LINES; i++) t = t + (mux_ldsc[i].conn != 0);
if (t) {
    for (i = 0; i < MUX_LINES; i++) {
        if (mux_ldsc[i].conn) {
            if (val) tmxr_fconns (st, &mux_ldsc[i], i);
            else tmxr_fstats (st, &mux_ldsc[i], i);
            }
        }
    }
else fprintf (st, "all disconnected\n");
return SCPE_OK;
}
