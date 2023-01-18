/* pdp11_dl.c: PDP-11 multiple terminal interface simulator

   Copyright (c) 1993-2013, Robert M Supnik

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

   dli,dlo      DL11 terminal input/output

   11-Oct-2013  RMS     Poll DLI immediately after attach to pick up connect
   18-Apr-2012  RMS     Modified to use clock coscheduling
   17-Aug-2011  RMS     Added AUTOCONFIGURE modifier
   19-Nov-2008  RMS     Revised for common TMXR show routines
                        Revised to autoconfigure vectors
   20-May-2008  RMS     Added modem control support
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "DL11 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#error "DL11 is not supported on the VAX!"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif
#include "sim_sock.h"
#include "sim_tmxr.h"

#define DLX_MAXMUX      (dlx_desc.lines - 1)
#define DLI_RCI         0                               /* rcv ints */
#define DLI_DSI         1                               /* dset ints */

/* Modem control */

#define DLX_V_MDM       (TTUF_V_UF + 0)
#define DLX_MDM         (1u << DLX_V_MDM)

/* registers */

#define DLICSR_DSI      0100000                         /* dataset int, RO */
#define DLICSR_RNG      0040000                         /* ring, RO */
#define DLICSR_CTS      0020000                         /* CTS, RO */
#define DLICSR_CDT      0010000                         /* CDT, RO */
#define DLICSR_SEC      0002000                         /* sec rcv, RONI */
#define DLICSR_DSIE     0000040                         /* DSI ie, RW */
#define DLICSR_SECX     0000010                         /* sec xmt, RWNI */
#define DLICSR_RTS      0000004                         /* RTS, RW */
#define DLICSR_DTR      0000002                         /* DTR, RW */
#define DLICSR_RD       (CSR_DONE|CSR_IE)               /* DL11C */
#define DLICSR_WR       (CSR_IE)
#define DLICSR_RD_M     (DLICSR_DSI|DLICSR_RNG|DLICSR_CTS|DLICSR_CDT|DLICSR_SEC| \
                         CSR_DONE|CSR_IE|DLICSR_DSIE|DLICSR_SECX|DLICSR_RTS|DLICSR_DTR)
#define DLICSR_WR_M     (CSR_IE|DLICSR_DSIE|DLICSR_SECX|DLICSR_RTS|DLICSR_DTR)
#define DLIBUF_ERR      0100000
#define DLIBUF_OVR      0040000
#define DLIBUF_RBRK     0020000
#define DLIBUF_RD       (DLIBUF_ERR|DLIBUF_OVR|DLIBUF_RBRK|0377)
#define DLOCSR_MNT      0000004                         /* maint, RWNI */
#define DLOCSR_XBR      0000001                         /* xmit brk, RWNI */
#define DLOCSR_RD       (CSR_DONE|CSR_IE|DLOCSR_MNT|DLOCSR_XBR)
#define DLOCSR_WR       (CSR_IE|DLOCSR_MNT|DLOCSR_XBR)

extern int32 tmxr_poll;

typedef struct dl_device_state {
    DEVICE  *idptr;                 /* Input device */
    DEVICE  *odptr;                 /* Output device */
    UNIT    *ouptr;
    uint16  i_csr;                  /* Input Control/Status */
    uint16  i_buf;                  /* Input Buffer */
    uint32  i_buftime;
    uint32  *pi_ireq;               /* Pointer to Input interrupt requests */
    uint16  o_csr;                  /* Output Control/Status */
    uint8   o_buf;                  /* Output Buffer */
    uint32  *po_ireq;               /* Pointer to Output interrupt requests */
    int32   ln;                     /* Line Number */
    TMXR    *tmxr;                  /* Mux pointer */
    TMLN    *lp;                    /* Mux Line pointer */
    } DL;

#define dl_ctx up8                  /* Field in Unit structure which points to the DL context */


DL dl_state[DLX_LINES];
DL dlcj_state[DLCJ_LINES];

uint32 dli_ireq[2] = { 0, 0};
uint32 dlcji_ireq[2] = { 0, 0};
uint32 dlo_ireq = 0;
uint32 dlcjo_ireq = 0;
TMLN dlx_ldsc[DLX_LINES] = { {0} };                     /* line descriptors */
TMXR dlx_desc = { DLX_LINES, 0, 0, dlx_ldsc };          /* mux descriptor */
TMLN dlcj_ldsc[DLCJ_LINES] = { {0} };                   /* line descriptors */
TMXR dlcj_desc = { DLCJ_LINES, 0, 0, dlcj_ldsc };       /* mux descriptor */

t_stat dlx_rd (int32 *data, int32 PA, int32 access);
t_stat dlx_wr (int32 data, int32 PA, int32 access);
t_stat dlx_reset (DEVICE *dptr);
t_stat dli_svc (UNIT *uptr);
t_stat dlo_svc (UNIT *uptr);
t_stat dlx_attach (UNIT *uptr, CONST char *cptr);
t_stat dlx_detach (UNIT *uptr);
t_stat dlx_set_lines (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void dlx_enbdis (DEVICE *dptr);
void dli_clr_int (DEVICE *dptr, int32 ln, uint32 wd);
void dli_set_int (DEVICE *dptr, int32 ln, uint32 wd);
int32 dli_iack (void);
int32 dlcji_iack (void);
void dlo_clr_int (DEVICE *dptr, int32 ln);
void dlo_set_int (DEVICE *dptr, int32 ln);
int32 dlo_iack (void);
int32 dlcjo_iack (void);
void dlx_reset_ln (UNIT *uptr);
t_stat dl_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dl_show_mode (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dlx_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *dlx_description (DEVICE *dptr);

/* DLI data structures

   dli_dev      DLI device descriptor
   dli_unit     DLI unit descriptor
   dli_reg      DLI register list
*/

#define IOLN_DL         010

DIB dli_dib = {
    IOBA_AUTO, IOLN_DL * DLX_LINES, &dlx_rd, &dlx_wr,
    2, IVCL (DLI), VEC_AUTO, { &dli_iack, &dlo_iack }, IOLN_DL,
    };

DIB dlcji_dib = {
    IOBA_AUTO, IOLN_DL * DLCJ_LINES, &dlx_rd, &dlx_wr,
    2, IVCL (DLCJI), VEC_AUTO, { &dlcji_iack, &dlcjo_iack }, IOLN_DL,
    };

UNIT dli_unit;

UNIT dlcji_unit;

REG dli_reg[] = {
    { SRDATAD (CSR, dl_state, i_csr, DEV_RDX, 16, 0, DLX_LINES, "Input Control/Status") },
    { SRDATAD (BUF, dl_state, i_buf, DEV_RDX, 16, 0, DLX_LINES, "Input Buffer") },
    { DRDATAD (TIME, dli_unit.wait,  24, "input polling interval"), PV_LEFT },
    { GRDATA (IREQ, dli_ireq[DLI_RCI], DEV_RDX, DLX_LINES, 0) },
    { GRDATA (DSI, dli_ireq[DLI_DSI], DEV_RDX, DLX_LINES, 0) },
    { DRDATA (LINES, dlx_desc.lines, 6), REG_HRO },
    { GRDATA (DEVADDR, dli_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVIOLN, dli_dib.lnt, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, dli_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

REG dlcji_reg[] = {
    { SRDATAD (CSR, dlcj_state, i_csr, DEV_RDX, 16, 0, DLCJ_LINES, "Input Control/Status") },
    { SRDATAD (BUF, dlcj_state, i_buf, DEV_RDX, 16, 0, DLCJ_LINES, "Input Buffer") },
    { DRDATAD (TIME, dlcji_unit.wait,  24, "input polling interval"), PV_LEFT },
    { GRDATA (IREQ, dlcji_ireq[DLI_RCI], DEV_RDX, DLCJ_LINES, 0) },
    { GRDATA (DSI, dlcji_ireq[DLI_DSI], DEV_RDX, DLCJ_LINES, 0) },
    { DRDATA (LINES, dlcj_desc.lines, 6), REG_HRO },
    { GRDATA (DEVADDR, dlcji_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVIOLN, dlcji_dib.lnt, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA (DEVVEC, dlcji_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB dli_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, "VECTOR", "VECTOR",
      &set_vec, &show_vec_mux, (void *) &dlx_desc, "Interrupt vector"  },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dlx_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dlx_desc, "Display a summary of line states" },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlx_desc, "Display current connections" },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlx_desc, "Display multiplexer statistics"  },
    { MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "LINES", "LINES=n",
      &dlx_set_lines, &tmxr_show_lines, (void *) &dlx_desc, "Display number of lines" },
    { 0 }
    };

MTAB dlcji_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, "VECTOR", "VECTOR",
      &set_vec, &show_vec_mux, (void *) &dlcj_desc, "Interrupt vector"  },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &dlcj_desc },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dlcj_desc, "Display a summary of line states" },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlcj_desc, "Display current connections" },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dlcj_desc, "Display multiplexer statistics"  },
    { MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES",
      &dlx_set_lines, &tmxr_show_lines, (void *) &dlcj_desc, "Display number of lines" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* read/write registers */
#define DBG_INT  0x0002                                 /* interrupts */
#define DBG_TRC  TMXR_DBG_TRC                           /* routine calls */

DEBTAB dl_debug[] = {
    { "REG",       DBG_REG,   "Register Activities" },
    { "INT",       DBG_INT,   "Interrupt Activities" },
    { "XMT",  TMXR_DBG_XMT,   "Transmit Data" },
    { "RCV",  TMXR_DBG_RCV,   "Received Data" },
    { "RET",  TMXR_DBG_RET,   "Returned Received Data" },
    { "MDM",  TMXR_DBG_MDM,   "Modem Signals" },
    { "CON",  TMXR_DBG_CON,   "Connection Activities" },
    { "ASY",  TMXR_DBG_ASY,   "Asynchronous Activities" },
    { "TRC",       DBG_TRC,   "trace routine calls" },
    { "EXP",  TMXR_DBG_EXP,   "Expect Activities" },
    { "SEND", TMXR_DBG_SEND,  "Send Activities" },
    { 0 }
};

DEVICE dli_dev = {
    "DLI", &dli_unit, dli_reg, dli_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, &dlx_attach, &dlx_detach,
    &dli_dib, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS | DEV_MUX | DEV_DEBUG,
    0, dl_debug, NULL, NULL, &dlx_help, NULL, &dlx_desc, &dlx_description, NULL, &dlx_desc};

DEVICE dlcji_dev = {
    "DLCJI", &dlcji_unit, dlcji_reg, dlcji_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, &dlx_attach, &dlx_detach,
    &dlcji_dib, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS | DEV_MUX | DEV_DEBUG,
    0, dl_debug, NULL, NULL, &dlx_help, NULL, &dlcj_desc, &dlx_description, NULL, &dlcj_desc};

/* DLO data structures

   dlo_dev      DLO device descriptor
   dlo_unit     DLO unit descriptor
   dlo_reg      DLO register list
*/

UNIT dlo_unit[DLX_LINES];

UNIT dlcjo_unit[DLCJ_LINES];

REG dlo_reg[] = {
    { SRDATAD (CSR, dl_state, o_csr, DEV_RDX, 16, 0, DLX_LINES, "Output Control/Status") },
    { SRDATAD (BUF, dl_state, o_buf, DEV_RDX, 8, 0, DLX_LINES, "Output Buffer") },
    { GRDATA (IREQ, dlo_ireq, DEV_RDX, DLX_LINES, 0) },
    { URDATA (TIME, dlo_unit[0].wait, 10, 31, 0,
              DLX_LINES, PV_LEFT) },
    { NULL }
    };

REG dlcjo_reg[] = {
    { SRDATAD (CSR, dlcj_state, o_csr, DEV_RDX, 16, 0, DLX_LINES, "Output Control/Status") },
    { SRDATAD (BUF, dlcj_state, o_buf, DEV_RDX, 8, 0, DLX_LINES, "Output Buffer") },
    { GRDATA (IREQ, dlcjo_ireq, DEV_RDX, DLCJ_LINES, 0) },
    { URDATA (TIME, dlcjo_unit[0].wait, 10, 31, 0,
              DLCJ_LINES, PV_LEFT) },
    { NULL }
    };

MTAB dlo_mod[] = {
    { MTAB_XTD|MTAB_VUN, TT_MODE_UC, NULL, "UC", &dl_set_mode, NULL, NULL, "Set upper case mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_7B, NULL, "7B", &dl_set_mode, NULL, NULL, "Set 7 bit mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_8B, NULL, "8B", &dl_set_mode, NULL, NULL, "Set 8 bit mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_7P, NULL, "7P", &dl_set_mode, NULL, NULL, "Set 7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VUN, 0,          "MODE", NULL, NULL, &dl_show_mode, NULL, "Show character mode" },
    { DLX_MDM,           0,          "no dataset", "NODATASET", NULL, NULL, NULL, "Set modem signals disabled" },
    { DLX_MDM,           DLX_MDM,    "dataset",    "DATASET",   NULL, NULL, NULL, "Set modem signals enabled" },
    { MTAB_XTD|MTAB_VUN, 0,          NULL,    "DISCONNECT", &tmxr_dscln, NULL, &dlx_desc, "Disconnect line" },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0,  "LOG", "LOG=file", &tmxr_set_log, &tmxr_show_log, &dlx_desc, "Set Logging to file" },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0,  NULL, "NOLOG", &tmxr_set_nolog, NULL, &dlx_desc, "Disable logging on line n" },
    { 0 }
    };

MTAB dlcjo_mod[] = {
    { MTAB_XTD|MTAB_VUN, TT_MODE_UC, NULL, "UC", &dl_set_mode, NULL, NULL, "Set upper case mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_7B, NULL, "7B", &dl_set_mode, NULL, NULL, "Set 7 bit mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_8B, NULL, "8B", &dl_set_mode, NULL, NULL, "Set 8 bit mode" },
    { MTAB_XTD|MTAB_VUN, TT_MODE_7P, NULL, "7P", &dl_set_mode, NULL, NULL, "Set 7 bit mode - non printing suppressed" },
    { MTAB_XTD|MTAB_VUN, 0,          "MODE", NULL, NULL, &dl_show_mode, NULL, "Show character mode" },
    { DLX_MDM,           0,          "no dataset", "NODATASET", NULL, NULL, NULL, "Set modem signals disabled" },
    { DLX_MDM,           DLX_MDM,    "dataset",    "DATASET",   NULL, NULL, NULL, "Set modem signals enabled" },
    { MTAB_XTD|MTAB_VUN, 0,          NULL,    "DISCONNECT", &tmxr_dscln, NULL, &dlcj_desc, "Disconnect line" },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0,  "LOG", "LOG=file", &tmxr_set_log, &tmxr_show_log, &dlcj_desc, "Set Logging to file" },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0,  NULL, "NOLOG", &tmxr_set_nolog, NULL, &dlcj_desc, "Disable logging on line n" },
    { 0 }
    };

DEVICE dlo_dev = {
    "DLO", dlo_unit, dlo_reg, dlo_mod,
    DLX_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, NULL, NULL,
    NULL, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, dl_debug, NULL, NULL, &dlx_help, NULL, &dlx_desc, &dlx_description, NULL, &dlx_desc};

DEVICE dlcjo_dev = {
    "DLCJO", dlcjo_unit, dlcjo_reg, dlcjo_mod,
    DLCJ_LINES, 10, 31, 1, 8, 8,
    NULL, NULL, &dlx_reset,
    NULL, NULL, NULL,
    NULL, DEV_UBUS | DEV_QBUS | DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, dl_debug, NULL, NULL, &dlx_help, NULL, &dlcj_desc, &dlx_description, NULL, &dlcj_desc};

/* Register names for Debug tracing */
static const char *dl_regs[] =
    {"TTI CSR", "TTI BUF", "TTO CSR", "TTO BUF" };

static int32 dlx_unit_from_pa (uint32 PA, UNIT **pouptr)
{
int32 ln = -1;

*pouptr = NULL;
if ((PA >= dli_dib.ba) && (PA < (dli_dib.ba + dli_dib.lnt))) {
    ln = (PA - dli_dib.ba) >> 3;
    *pouptr = &dlo_dev.units[ln];
    return ln;
    }
if ((PA >= dlcji_dib.ba) && (PA < (dlcji_dib.ba + dlcji_dib.lnt))) {
    ln = (PA - dlcji_dib.ba) >> 3;
    *pouptr = &dlcjo_dev.units[ln];
    return ln;
    }
return ln;
}


/* Terminal input routines */

t_stat dlx_rd (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;
int32 ln = dlx_unit_from_pa ((uint32)PA, &uptr);
DL *dl = (DL *)uptr->dl_ctx;

if (ln < 0)                                             /* validate line number */
    return SCPE_NXM;

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        *data = dl->i_csr &
            ((dl->ouptr->flags & DLX_MDM)? DLICSR_RD_M: DLICSR_RD);
        dl->i_csr &= ~DLICSR_DSI;                       /* clr DSI flag */
        dli_clr_int (dl->idptr, ln, DLI_DSI);           /* clr dset int req */
        break;

    case 01:                                            /* tti buf */
        *data = dl->i_buf & DLIBUF_RD;
        dl->i_csr &= ~CSR_DONE;                         /* clr rcv done */
        dli_clr_int (dl->idptr, ln, DLI_RCI);           /* clr rcv int req */
        /* Reschedule the next poll preceisely so that 
           the programmed input speed is observed. */
        sim_clock_coschedule_abs (dl->idptr->units, tmxr_poll);
        break;

    case 02:                                            /* tto csr */
        *data = dl->o_csr & DLOCSR_RD;
        break;

    case 03:                                            /* tto buf */
        *data = dl->o_buf;
        break;

    default:
        return SCPE_NXM;
        }                                               /* end switch PA */

sim_debug(DBG_REG, dl->idptr, "dlx_rd(PA=0x%08X [%s], access=%d, data=0x%X)\n", PA, dl_regs[(PA >> 1) & 03], access, *data);

return SCPE_OK;
}

t_stat dlx_wr (int32 data, int32 PA, int32 access)
{
UNIT *uptr;
int32 ln = dlx_unit_from_pa ((uint32)PA, &uptr);
DL *dl = (DL *)uptr->dl_ctx;

if (ln < 0)                                             /* validate line number */
    return SCPE_NXM;

sim_debug(DBG_REG, dl->idptr, "dlx_wr(PA=0x%08X [%s], access=%d, data=0x%X)\n", PA, dl_regs[(PA >> 1) & 03], access, data);

switch ((PA >> 1) & 03) {                               /* decode PA<2:1> */

    case 00:                                            /* tti csr */
        if (PA & 1)                                     /* odd byte RO */
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            dli_clr_int (dl->idptr, ln, DLI_RCI);
        else if ((dl->i_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
            dli_set_int (dl->idptr, ln, DLI_RCI);
        if (uptr->flags & DLX_MDM) {                   /* modem control */
            if ((data & DLICSR_DSIE) == 0)
                dli_clr_int (dl->idptr, ln, DLI_DSI);
            else
                if ((dl->i_csr & (DLICSR_DSI|DLICSR_DSIE)) == DLICSR_DSI)
                dli_set_int (dl->idptr, ln, DLI_DSI);
            if ((data ^ dl->i_csr) & DLICSR_DTR) {      /* DTR change? */
                if ((data & DLICSR_DTR) && dl->lp->conn) {/* setting DTR? */
                    dl->i_csr = (dl->i_csr & ~DLICSR_RNG) |
                        (DLICSR_CDT|DLICSR_CTS|DLICSR_DSI);
                    if (data & DLICSR_DSIE)             /* if ie, req int */
                        dli_set_int (dl->idptr, ln, DLI_DSI);
                    }                                   /* end DTR 0->1 + ring */
                else {                                  /* clearing DTR */
                    if (dl->lp->conn) {                 /* connected? */
                        tmxr_linemsg (dl->lp, "\r\nLine hangup\r\n");
                        tmxr_reset_ln (dl->lp);         /* reset line */
                        if (dl->i_csr & DLICSR_CDT) {   /* carrier det? */
                            dl->i_csr |= DLICSR_DSI;
                            if (data & DLICSR_DSIE)     /* if ie, req int */
                                dli_set_int (dl->idptr, ln, DLI_DSI);
                            }
                        }
                    dl->i_csr &= ~(DLICSR_CDT|DLICSR_RNG|DLICSR_CTS);
                                                        /* clr CDT,RNG,CTS */
                    }                                   /* end DTR 1->0 */
                }                                       /* end DTR chg */
            dl->i_csr = (uint16) ((dl->i_csr & ~DLICSR_WR_M) | (data & DLICSR_WR_M));
            }                                           /* end modem */
        dl->i_csr = (uint16) ((dl->i_csr & ~DLICSR_WR) | (data & DLICSR_WR));
        return SCPE_OK;

    case 01:                                            /* tti buf */
        return SCPE_OK;

    case 02:                                            /* tto csr */
        if (PA & 1)
            return SCPE_OK;
        if ((data & CSR_IE) == 0)
            dlo_clr_int (dl->odptr, ln);
        else {
            if ((dl->o_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
                dlo_set_int (dl->odptr, ln);
            }
        dl->o_csr = (uint16) ((dl->o_csr & ~DLOCSR_WR) | (data & DLOCSR_WR));
        return SCPE_OK;

    case 03:                                            /* tto buf */
        if ((PA & 1) == 0)
            dl->o_buf = data & 0377;
        dl->o_csr &= ~CSR_DONE;
        dlo_clr_int (dl->odptr, ln);
        sim_activate (uptr, uptr->wait);
        return SCPE_OK;
        }                                               /* end switch PA */

return SCPE_NXM;
}

/* Terminal input service */

t_stat dli_svc (UNIT *uptr)
{
DL *dl = (DL *)((find_dev_from_unit (uptr))->units->dl_ctx);
DL *odls = (DL *)(dl->odptr->units->dl_ctx);
int32 ln, c, temp;

sim_debug(DBG_TRC, dl->idptr, "dli_svc()\n");

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
ln = tmxr_poll_conn (dl->tmxr);                         /* look for connect */
if (ln >= 0) {                                          /* got one? rcv enb */
    dl->tmxr->ldsc[ln].rcve = 1;
    if (dl->odptr->units[ln].flags & DLX_MDM) {         /* modem control? */
        if (odls[ln].i_csr & DLICSR_DTR)                /* DTR already set? */
            odls[ln].i_csr |= (DLICSR_CDT|DLICSR_CTS|DLICSR_DSI);
        else
            odls[ln].i_csr |= (DLICSR_RNG|DLICSR_DSI);  /* no, ring */
        if (odls[ln].i_csr & DLICSR_DSIE)               /* if ie, */
            dli_set_int (dl->idptr, ln, DLI_DSI);       /* req int */
        }                                               /* end modem */
    }                                                   /* end new conn */
tmxr_poll_rx (dl->tmxr);                                /* poll for input */
for (ln = 0; ln < dl->tmxr->lines; ln++) {              /* loop thru lines */
    if (dl->tmxr->ldsc[ln].conn) {                      /* connected? */
        if ((odls[ln].i_csr & CSR_DONE) &&              /* input still pending and < 500ms? */
            ((sim_os_msec () - odls[ln].i_buftime) < 500))
            continue;
        if ((temp = tmxr_getc_ln (&dl->tmxr->ldsc[ln]))) {/* get char */
            if (temp & SCPE_BREAK)                      /* break? */
                c = DLIBUF_ERR|DLIBUF_RBRK;
            else
                c = sim_tt_inpcvt (temp, TT_GET_MODE (dl->odptr->units[ln].flags));
            if (odls[ln].i_csr & CSR_DONE)
                c |= DLIBUF_ERR|DLIBUF_OVR;
            else
                odls[ln].i_csr |= CSR_DONE;
            if (odls[ln].i_csr & CSR_IE)
                dli_set_int (dl->idptr, ln, DLI_RCI);
            odls[ln].i_buf = (uint16)c;
            odls[ln].i_buftime = sim_os_msec ();
            }
        }
    else {
        if (dl->odptr->units[ln].flags & DLX_MDM) {     /* discpnn & modem? */
            if (odls[ln].i_csr & DLICSR_CDT) {          /* carrier detect? */
                odls[ln].i_csr |= DLICSR_DSI;           /* dataset change */
                if (odls[ln].i_csr & DLICSR_DSIE)       /* if ie, */
                    dli_set_int (dl->idptr, ln, DLI_DSI);/* req int */
                }
            odls[ln].i_csr &= ~(DLICSR_CDT|DLICSR_RNG|DLICSR_CTS);
                                                        /* clr CDT,RNG,CTS */
            }
        }
    }
return sim_clock_coschedule (uptr, tmxr_poll);          /* continue poll */
}

/* Terminal output service */

t_stat dlo_svc (UNIT *uptr)
{
int32 c;
DL *dl = (DL *)(uptr->dl_ctx);

sim_debug(DBG_TRC, dl->odptr, "dlo_svc()\n");

if (dl->lp->conn) {                                     /* connected? */
    if (dl->lp->xmte) {                                 /* tx enabled? */
        c = sim_tt_outcvt (dl->o_buf, TT_GET_MODE (uptr->flags));
        if (c >= 0)                                     /* output char */
            tmxr_putc_ln (dl->lp, c);
        tmxr_poll_tx (dl->tmxr);                        /* poll xmt */
        }
    else {
        tmxr_poll_tx (dl->tmxr);                        /* poll xmt */
        sim_activate (uptr, uptr->wait);                /* wait until later */
        return SCPE_OK;
        }
    }
dl->o_csr |= CSR_DONE;                                  /* set done */
if (dl->o_csr & CSR_IE)
    dlo_set_int (dl->odptr, dl->ln);
return SCPE_OK;
}

/* Interrupt routines */

void dlx_SET_INT (DEVICE *dptr)
{
if (dptr == &dli_dev)
    SET_INT (DLI);
else {
    if (dptr == &dlo_dev)
        SET_INT (DLO);
    else {
        if (dptr == &dlcji_dev)
            SET_INT (DLCJI);
        else
            SET_INT (DLCJO);
        }
    }
}

void dlx_CLR_INT (DEVICE *dptr)
{
if (dptr == &dli_dev)
    CLR_INT (DLI);
else {
    if (dptr == &dlo_dev)
        CLR_INT (DLO);
    else {
        if (dptr == &dlcji_dev)
            CLR_INT (DLCJI);
        else
            CLR_INT (DLCJO);
        }
    }
}

void dli_clr_int (DEVICE *dptr, int32 ln, uint32 wd)
{
DL *dl = (DL *)(dptr->units->dl_ctx);

sim_debug(DBG_INT, dptr, "dli_clr_int(dl=%d, wd=%d)\n", ln, wd);

dl->pi_ireq[wd] &= ~(1 << ln);                          /* clr rcv/dset int */
if ((dl->pi_ireq[DLI_RCI] | dl->pi_ireq[DLI_DSI]) == 0) /* all clr? */
    dlx_CLR_INT (dptr);                                 /* all clr? */
else
    dlx_SET_INT (dptr);                                 /* no, set intr */
}

void dli_set_int (DEVICE *dptr, int32 ln, uint32 wd)
{
DL *dl = (DL *)(dptr->units->dl_ctx);

sim_debug(DBG_INT, dptr, "dli_set_int(dl=%d, wd=%d)\n", ln, wd);

dl->pi_ireq[wd] |= (1 << ln);                          /* set rcv/dset int */
dlx_SET_INT (dptr);                                   /* set master intr */
}

int32 dlxi_iack (DEVICE *dptr)
{
int32 ln;
DL *dl = (DL *)(dptr->units->dl_ctx);
DIB *dib = (DIB *)(dl->idptr->ctxt);

for (ln = 0; ln < dib->numc; ln++) {                    /* find 1st line */
    if ((dl->pi_ireq[DLI_RCI] | dl->pi_ireq[DLI_DSI]) & (1 << ln)) {
        dli_clr_int (dptr, ln, DLI_RCI);                /* clr both req */
        dli_clr_int (dptr, ln, DLI_DSI);
        sim_debug(DBG_INT, dptr, "dli_iack(ln=%d)\n", ln);
        return (dib->vec + (ln * 010));                 /* return vector */
        }
    }
return 0;
}

int32 dli_iack (void)
{
return dlxi_iack (&dli_dev);
}

int32 dlcji_iack (void)
{
return dlxi_iack (&dlcji_dev);
}


void dlo_clr_int (DEVICE *dptr, int32 ln)
{
DL *dl = (DL *)(dptr->units->dl_ctx);

sim_debug(DBG_INT, dptr, "dlo_clr_int(dl=%d)\n", ln);

*(dl->po_ireq) &= ~(1 << ln);                            /* clr xmit int */
if (*(dl->po_ireq) == 0)                                 /* all clr? */
    dlx_SET_INT (dptr);
else
    dlx_SET_INT (dptr);                                 /* no, set intr */
}

void dlo_set_int (DEVICE *dptr, int32 ln)
{
DL *dl = (DL *)(dptr->units->dl_ctx);

sim_debug(DBG_INT, dptr, "dlo_set_int(dl=%d)\n", ln);

*(dl->po_ireq) |= (1 << ln);                             /* set xmit int */
dlx_SET_INT (dptr);                                     /* set master intr */
}

int32 dlxo_iack (DEVICE *dptr)
{
DL *dl = (DL *)(dptr->units->dl_ctx);
DIB *dib = (DIB *)(dl->idptr->ctxt);
int32 ln;

for (ln = 0; ln < dib->numc; ln++) {            /* find 1st line with a pending interrupt */
    if (*(dl->po_ireq) & (1 << ln)) {
        dlo_clr_int (dptr, ln);                 /* clear intr */
        sim_debug(DBG_INT, dl->odptr, "dlo_iack(ln=%d)\n", ln);
        return (dib->vec + (ln * 010) + 4);     /* return vector */
        }
    }
return 0;
}

int32 dlo_iack (void)
{
return dlxo_iack (&dlo_dev);
}

int32 dlcjo_iack (void)
{
return dlxo_iack (&dlcjo_dev);
}

/* Reset */

t_stat dlx_reset (DEVICE *dptr)
{
uint32 ln;
DEVICE *idptr = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? &dli_dev : &dlcji_dev;
DEVICE *odptr = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? &dlo_dev : &dlcjo_dev;
TMXR *tmxr = (TMXR *)idptr->type_ctx;
DL *dl = (idptr == &dli_dev) ? dl_state : dlcj_state;

sim_debug(DBG_TRC, dptr, "dlx_reset()\n");

if (sim_switches & SWMASK ('P')) {
    idptr->units->action = dli_svc;
    idptr->units->wait = TMLN_SPD_9600_BPS;
    idptr->units->dl_ctx = &dl[0];
    for (ln = 0; ln < odptr->numunits; ln++) {
        odptr->units[ln].action = &dlo_svc;
        odptr->units[ln].flags = TT_MODE_UC;
        odptr->units[ln].wait = SERIAL_OUT_WAIT;
        odptr->units[ln].dl_ctx = &dl[ln];
        dl[ln].idptr = idptr;
        dl[ln].odptr = odptr;
        dl[ln].ouptr = &odptr->units[ln];
        dl[ln].ln = ln;
        dl[ln].tmxr = tmxr;
        dl[ln].lp = &tmxr->ldsc[ln];
        dl[ln].pi_ireq = (idptr == &dli_dev) ? dli_ireq : dlcji_ireq;
        dl[ln].po_ireq = (idptr == &dli_dev) ? &dlo_ireq : &dlcjo_ireq;
        tmxr_set_line_output_unit (tmxr, ln, &odptr->units[ln]);
        tmxr_set_line_speed (&tmxr->ldsc[ln], "9600");
        }
    }
dlx_enbdis (dptr);                                      /* sync enables */
sim_cancel (idptr->units);                              /* assume stop */
if (idptr->units->flags & UNIT_ATT)                     /* if attached, */
    sim_clock_coschedule (idptr->units, tmxr_poll);     /* activate */
for (ln = 0; ln < (uint32)tmxr->lines; ln++)            /* for all lines */
    dlx_reset_ln (&odptr->units[ln]);
return auto_config (idptr->name, tmxr->lines);          /* auto config */
}

/* Reset individual line */

void dlx_reset_ln (UNIT *uptr)
{
DL *dl = (DL *)uptr->dl_ctx;

sim_debug(DBG_TRC, dl->odptr, "dlx_reset_ln(ln=%d)\n", dl->ln);

dl->i_buf = 0;                                          /* clear buf */
if (uptr->flags & DLX_MDM)                              /* modem */
    dl->i_csr &= DLICSR_DTR;                            /* dont clr DTR */
else
    dl->i_csr = 0;
dl->o_buf = 0;                                          /* clear buf */
dl->o_csr = CSR_DONE;
sim_cancel (uptr);                                      /* deactivate */
dli_clr_int (dl->idptr, dl->ln, DLI_RCI);
dli_clr_int (dl->idptr, dl->ln, DLI_DSI);
dlo_clr_int (dl->odptr, dl->ln);
}

/* Attach master unit */

t_stat dlx_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
DEVICE *dptr = find_dev_from_unit (uptr);
TMXR *tmxr = (TMXR *)dptr->type_ctx;

sim_debug(DBG_TRC, dptr, "dlx_attach()\n");

r = tmxr_attach (tmxr, uptr, cptr);                     /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_activate (uptr, 0);                                 /* start poll at once */
return SCPE_OK;
}

/* Detach master unit */

t_stat dlx_detach (UNIT *uptr)
{
int32 i;
DEVICE *dptr = find_dev_from_unit (uptr);
TMXR *tmxr = (TMXR *)dptr->type_ctx;
t_stat r;

sim_debug(DBG_TRC, dptr, "dlx_detach()\n");

r = tmxr_detach (tmxr, uptr);                           /* detach */
for (i = 0; i < tmxr->lines; i++)                       /* all lines, */
    tmxr->ldsc[i].rcve = 0;                             /* disable rcv */
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Number of DL devices used by TU58 */

t_value dlx_tu58_count (DEVICE *dptr)
{
DEVICE *td_dptr = find_dev ("TDC");

if ((td_dptr == NULL) ||
    (td_dptr->flags & DEV_DIS) ||
    ((dptr != &dli_dev) && (dptr != &dlo_dev)))
    return 0;
return (t_value)((DIB *)td_dptr->ctxt)->numc;
}

/* Enable/disable device */

void dlx_enbdis (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS) {
    if ((dptr == &dli_dev) || (dptr == &dlo_dev)) {
        dli_dev.flags |= DEV_DIS;
        dlo_dev.flags |= DEV_DIS;
        }
    else {
        dlcji_dev.flags |= DEV_DIS;
        dlcjo_dev.flags |= DEV_DIS;
        }
    }
else {      /* Enabled */
    if ((dptr == &dli_dev) || (dptr == &dlo_dev)) {
        if (dlx_tu58_count (dptr) < DCX_LINES) {
            if ((dlx_desc.lines + dlx_tu58_count (dptr)) > 16) {
                char lines[16];
                int32 saved_switches = sim_switches;
                
                sprintf (lines, "%d", DCX_LINES - dlx_tu58_count (dptr));
                sim_switches |= SWMASK('Y');
                dlx_set_lines (dli_dev.units, 0, lines, NULL);
                sim_switches = saved_switches;
                }
            dli_dev.flags &= ~DEV_DIS;
            dlo_dev.flags &= ~DEV_DIS;
            }
        else {
            dli_dev.flags |= DEV_DIS;
            dlo_dev.flags |= DEV_DIS;
            }
        }
    else {
        dlcji_dev.flags &= ~DEV_DIS;
        dlcjo_dev.flags &= ~DEV_DIS;
        }
    }
}

/* Change number of lines */

t_stat dlx_set_lines (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 newln, i, t;
t_stat r;
DEVICE *dptr = find_dev_from_unit (uptr);
DEVICE *odptr = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? &dlo_dev : &dlcjo_dev;
TMXR *tmxr = (TMXR *)dptr->type_ctx;
DIB *dib = (DIB *)dptr->ctxt;

if (cptr == NULL)
    return SCPE_ARG;
newln = get_uint (cptr, 10, DLX_LINES - dlx_tu58_count (dptr), &r);
if ((r != SCPE_OK) || (newln == tmxr->lines) || (newln == 0))
    return sim_messagef (r, "%s is an invalid number of lines for the %s device\n", cptr, dptr->name);
if (newln < tmxr->lines) {
    for (i = newln, t = 0; i < tmxr->lines; i++)
        t = t | tmxr->ldsc[i].conn;
    if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
        return SCPE_OK;
    for (i = newln; i < tmxr->lines; i++) {
        if (tmxr->ldsc[i].conn) {
            tmxr_linemsg (&tmxr->ldsc[i], "\r\nOperator disconnected line\r\n");
            tmxr_reset_ln (&tmxr->ldsc[i]);             /* reset line */
            }
        odptr->units[i].flags |= UNIT_DIS;
        dlx_reset_ln (&odptr->units[i]);
        }
    }
else {
    for (i = tmxr->lines; i < newln; i++) {
        dptr->units[i].flags &= ~UNIT_DIS;
        dlx_reset_ln (&dptr->units[i]);
        }
    }
tmxr->lines = newln;
dib->lnt = newln * 010;                                 /* upd IO page lnt */
return auto_config (dptr->name, newln);                 /* auto config */
}

/* SET character MODE processor */

t_stat dl_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uptr->flags &= ~TT_MODE;
uptr->flags |= val;
return SCPE_OK;
}

/* SHOW character MODE processor */

t_stat dl_show_mode (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
static const char *modes[] = {"7B", "8B", "UC", "7P"};

fprintf (st, "%s", modes[(uptr->flags & TT_MODE) >> TTUF_V_MODE]);
return SCPE_OK;
}

static const char *dlx_devices (DEVICE *dptr)
{
static const char *devices[] = {
    "KL11/DL11-A/DL11-B",
    "DL11-C/DL11-D/DL11-E",
    "DLV11-E/DLV11-F",
    "DLV11-J/DLV11-E/DLV11-F",
    };
return devices[((UNIBUS == 0) << 1) + ((dptr != &dli_dev) & (dptr != &dlo_dev))];
}

t_stat dlx_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char *dli = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? "DLI" : "DLCJI";
const char *dlo = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? "DLO" : "DLCJO";
const char *device_type = dlx_devices (dptr);
int lines = ((dptr == &dli_dev) || (dptr == &dlo_dev)) ? DLX_LINES : DLCJ_LINES;

fprintf (st, "%s/%s Terminal Multiplexer (%s)\n\n", dli, dlo, device_type);
fprintf (st, "The %s/%s implements up to %d %s terminal lines.\n", dli, dlo, lines, device_type);
fprintf (st, "The default number of lines is %d.  The number of lines can\n", lines);
fprintf (st, "be changed with the command\n\n");
fprintf (st, "   sim> SET %s LINES=n            set line count to n\n\n", dli);
fprintf (st, "The %s/%s supports four character processing modes, UC, 7P, 7B, and 8B:\n\n", dli, dlo);
fprintf (st, "  mode    input characters     output characters\n");
fprintf (st, "  ===========================================================\n");
fprintf (st, "  UC  lower case converted to  lower case converted to upper case,\n");
fprintf (st, "      upper case, high-order   case, high-order bit cleared,\n");
fprintf (st, "      bit cleared\n");
fprintf (st, "  7P  high-order bit cleared   high-order bit cleared,\n");
fprintf (st, "                               non-printing characters suppressed\n");
fprintf (st, "  7B  high-order bit cleared   high-order bit cleared\n");
fprintf (st, "  8B  no changes               no changes\n\n");
fprintf (st, "The default is UC.  To change the character processing mode, use\n");
fprintf (st, "the command:\n\n");
fprintf (st, "   sim> SET %sn {UC|7P|7B|8B}\n\n", dlo);
fprintf (st, "The %s supports logging on a per-line basis.  The command\n\n", dlo);
fprintf (st, "   sim> SET %sn LOG=filename\n\n", dlo);
fprintf (st, "enables logging for the specified line(n) to the indicated file.\n");
fprintf (st, "The command:\n\n");
fprintf (st, "   sim> SET %sn NOLOG=line\n\n", dlo);
fprintf (st, "disables logging for the specified line and closes any open log file.\n");
fprintf (st, "Finally, the command:\n\n");
fprintf (st, "   sim> SHOW %sn LOG\n\n", dlo);
fprintf (st, "displays logging information for line n.\n\n");
fprintf (st, "Once the %s is attached and the simulator is running, the %s will listen\n", dli, dli);
fprintf (st, "for connections on the specified port.  It assumes that the incoming\n");
fprintf (st, "connections are Telnet connections.  The connection remains open until\n");
fprintf (st, "disconnected by the simulated program, the Telnet client, a\n");
fprintf (st, "SET %sn DISCONNECT command, or a DETACH %s command.\n\n", dlo, dli);
fprintf (st, "Other special %s/%s commands:\n\n", dli, dlo);
fprintf (st, "   sim> SHOW %s CONNECTIONS           show current connections\n", dli);
fprintf (st, "   sim> SHOW %s STATISTICS            show statistics for active connections\n", dli);
fprintf (st, "   sim> SET %s DISCONNECT=linenumber  disconnects the specified line.\n\n", dli);
fprintf (st, "All open connections are lost when the simulator shuts down or the %s is\n", dli);
fprintf (st, "detached.\n\n");
return SCPE_OK;
}

const char *dlx_description (DEVICE *dptr)
{
static char desc[128];

snprintf (desc, sizeof (desc), "%s asynchronous line interface - %s", dlx_devices (dptr), ((dptr == &dli_dev) || (dptr == &dlcji_dev)) ? "receiver" : "transmitter");
return desc;
}
