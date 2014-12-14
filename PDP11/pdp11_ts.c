/* pdp11_ts.c: TS11/TSV05 magnetic tape simulator

   Copyright (c) 1993-2014, Robert M Supnik

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

   ts           TS11/TSV05 magtape

   27-Oct-14    RMS     Fixed bug in read forward with byte swap
   23-Oct-13    RMS     Revised for new boot setup routine
   19-Mar-12    RMS     Fixed declaration of cpu_opt (Mark Pizzolato)
   22-May-10    RMS     Fixed t_addr printouts for 64b big-endian systems
                        (Mark Pizzolato)
   16-Feb-06    RMS     Added tape capacity checking
   31-Oct-05    RMS     Fixed address width for large files
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Jul-05    RMS     Removed extraneous externs
   18-Mar-05    RMS     Added attached test to detach routine
   07-Dec-04    RMS     Added read-only file support
   30-Sep-04    RMS     Revised Unibus interface
   25-Jan-04    RMS     Revised for device debug support
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Revised to use magtape library
   30-Sep-02    RMS     Added variable address support to bootstrap
                        Added vector change/display support
                        Fixed CTL unload/clean decode
                        Implemented XS0_MOT in extended status
                        New data structures, revamped error recovery
   28-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length protection
   04-Apr-02    RMS     Fixed bug in residual frame count after space operation
   16-Feb-02    RMS     Fixed bug in message header logic
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   09-Nov-01    RMS     Added bus map, VAX support
   15-Oct-01    RMS     Integrated debug logging across simulator
   27-Sep-01    RMS     Implemented extended characteristics and status
                        Fixed bug in write characteristics status return
   19-Sep-01    RMS     Fixed bug in bootstrap
   15-Sep-01    RMS     Fixed bug in NXM test
   07-Sep-01    RMS     Revised device disable and interrupt mechanism
   13-Jul-01    RMS     Fixed bug in space reverse (Peter Schorn)

   Magnetic tapes are represented as a series of variable 8b records
   of the form:

        32b record length in bytes - exact number
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b record length in bytes - exact number

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a single record length of 0.
   End of tape is two consecutive end of file marks.

   The TS11 functions in three environments:

   - PDP-11 Q22 systems - the I/O map is one for one, so it's safe to
     go through the I/O map
   - PDP-11 Unibus 22b systems - the TS11 behaves as an 18b Unibus
     peripheral and must go through the I/O map
   - VAX Q22 systems - the TS11 must go through the I/O map
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "TS11 not supported on PDP10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define TS_DIS          0                               /* on by default */
#define DMASK           0xFFFF

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define TS_DIS          DEV_DIS                         /* off by default */
extern uint32 cpu_opt;
#endif

#include "sim_tape.h"
#define ADDRTEST        (UNIBUS? 0177774: 0177700)

/* TSBA/TSDB - 17772520: base address/data buffer register

   read:        most recent memory address
   write word:  initiate command
   write byte:  diagnostic use
*/

/* TSSR  - 17772522: subsystem status register
   TSDBX - 17772523: extended address register

   read:        return status
   write word:  initialize
   write byte:  if odd, set extended packet address register
*/

#define TSSR_SC         0100000                         /* special condition */
#define TSSR_RMR        0010000                         /* reg mod refused */
#define TSSR_NXM        0004000                         /* nxm */
#define TSSR_NBA        0002000                         /* need buf addr */
#define TSSR_V_EMA      8                               /* mem addr<17:16> */
#define TSSR_EMA        0001400
#define TSSR_SSR        0000200                         /* subsystem ready */
#define TSSR_OFL        0000100                         /* offline */
#define TSSR_V_TC       1                               /* term class */
#define TSSR_M_TC       07
#define TSSR_TC         (TSSR_M_TC << TSSR_V_TC)
#define  TC0            (0 << TSSR_V_TC)                /* ok */
#define  TC1            (1 << TSSR_V_TC)                /* attention */
#define  TC2            (2 << TSSR_V_TC)                /* status alert */
#define  TC3            (3 << TSSR_V_TC)                /* func reject */
#define  TC4            (4 << TSSR_V_TC)                /* retry, moved */
#define  TC5            (5 << TSSR_V_TC)                /* retry */
#define  TC6            (6 << TSSR_V_TC)                /* pos lost */
#define  TC7            (7 << TSSR_V_TC)                /* fatal err */
#define TSSR_MBZ        0060060
#define GET_TC(x)       (((x) >> TSSR_V_TC) & TSSR_M_TC)        

#define TSDBX_M_XA      017                             /* ext addr */
#define TSDBX_BOOT      0000200                         /* boot */

/* Command packet offsets */

#define CMD_PLNT        4                               /* cmd pkt length */
#define cmdhdr          tscmdp[0]                       /* header */
#define cmdadl          tscmdp[1]                       /* address low */
#define cmdadh          tscmdp[2]                       /* address high */
#define cmdlnt          tscmdp[3]                       /* length */

/* Command packet header */

#define CMD_ACK         0100000                         /* acknowledge */
#define CMD_CVC         0040000                         /* clear vol chk */
#define CMD_OPP         0020000                         /* opposite */
#define CMD_SWP         0010000                         /* swap bytes */
#define CMD_V_MODE      8                               /* mode */
#define CMD_M_MODE      017
#define CMD_IE          0000200                         /* int enable */
#define CMD_V_FNC       0                               /* function */
#define CMD_M_FNC       037                             /* function */
#define CMD_N_FNC       (CMD_M_FNC + 1)
#define  FNC_READ       001                             /* read */
#define  FNC_WCHR       004                             /* write char */
#define  FNC_WRIT       005                             /* write */
#define  FNC_WSSM       006                             /* write mem */
#define  FNC_POS        010                             /* position */
#define  FNC_FMT        011                             /* format */
#define  FNC_CTL        012                             /* control */
#define  FNC_INIT       013                             /* init */
#define  FNC_GSTA       017                             /* get status */
#define CMD_MBZ         0000140
#define GET_FNC(x)      (((x) >> CMD_V_FNC) & CMD_M_FNC)
#define GET_MOD(x)      (((x) >> CMD_V_MODE) & CMD_M_MODE)

/* Function test flags */

#define FLG_MO          001                             /* motion */
#define FLG_WR          002                             /* write */
#define FLG_AD          004                             /* addr mem */

/* Message packet offsets */

#define MSG_PLNT        8                               /* packet length */
#define msghdr          tsmsgp[0]                       /* header */
#define msglnt          tsmsgp[1]                       /* length */
#define msgrfc          tsmsgp[2]                       /* residual frame */
#define msgxs0          tsmsgp[3]                       /* ext status 0 */
#define msgxs1          tsmsgp[4]                       /* ext status 1 */
#define msgxs2          tsmsgp[5]                       /* ext status 2 */
#define msgxs3          tsmsgp[6]                       /* ext status 3 */
#define msgxs4          tsmsgp[7]                       /* ext status 4 */

/* Message packet header */

#define MSG_ACK         0100000                         /* acknowledge */
#define MSG_MATN        0000000                         /* attention */
#define MSG_MILL        0000400                         /* illegal */
#define MSG_MNEF        0001000                         /* non exec fnc */
#define MSG_CEND        0000020                         /* end */
#define MSG_CFAIL       0000021                         /* fail */
#define MSG_CERR        0000022                         /* error */
#define MSG_CATN        0000023                         /* attention */

/* Extended status register 0 */

#define XS0_TMK         0100000                         /* tape mark */
#define XS0_RLS         0040000                         /* rec lnt short */
#define XS0_LET         0020000                         /* log end tape */
#define XS0_RLL         0010000                         /* rec lnt long */
#define XS0_WLE         0004000                         /* write lock err */
#define XS0_NEF         0002000                         /* non exec fnc */
#define XS0_ILC         0001000                         /* illegal cmd */
#define XS0_ILA         0000400                         /* illegal addr */
#define XS0_MOT         0000200                         /* tape has moved */
#define XS0_ONL         0000100                         /* online */
#define XS0_IE          0000040                         /* int enb */
#define XS0_VCK         0000020                         /* volume check */
#define XS0_PET         0000010                         /* 1600 bpi */
#define XS0_WLK         0000004                         /* write lock */
#define XS0_BOT         0000002                         /* BOT */
#define XS0_EOT         0000001                         /* EOT */
#define XS0_ALLCLR      0177600                         /* clear at start */

/* Extended status register 1 */

#define XS1_UCOR        0000002                         /* uncorrectable */

/* Extended status register 2 */

#define XS2_XTF         0000200                         /* ext features */

/* Extended status register 3 */

#define XS3_OPI         0000100                         /* op incomplete */
#define XS3_REV         0000040                         /* reverse */
#define XS3_RIB         0000001                         /* reverse to BOT */

/* Extended status register 4 */

#define XS4_HDS         0100000                         /* high density */

/* Write characteristics packet offsets */

#define WCH_PLNT        5                               /* packet length */
#define wchadl          tswchp[0]                       /* address low */
#define wchadh          tswchp[1]                       /* address high */
#define wchlnt          tswchp[2]                       /* length */
#define wchopt          tswchp[3]                       /* options */
#define wchxopt         tswchp[4]                       /* ext options */

/* Write characteristics options */

#define WCH_ESS         0000200                         /* stop dbl tmk */
#define WCH_ENB         0000100                         /* BOT = tmk */
#define WCH_EAI         0000040                         /* enb attn int */
#define WCH_ERI         0000020                         /* enb mrls int */

/* Write characteristics extended options */

#define WCHX_HDS        0000040                         /* high density */

#define MAX(a,b)        (((a) >= (b))? (a): (b))
#define MAX_PLNT        8                               /* max pkt length */

extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;

uint8 *tsxb = NULL;                                     /* xfer buffer */
int32 tssr = 0;                                         /* status register */
int32 tsba = 0;                                         /* mem addr */
int32 tsdbx = 0;                                        /* data buf ext */
int32 tscmdp[CMD_PLNT] = { 0 };                         /* command packet */
int32 tsmsgp[MSG_PLNT] = { 0 };                         /* message packet */
int32 tswchp[WCH_PLNT] = { 0 };                         /* wr char packet */
int32 ts_ownc = 0;                                      /* tape owns cmd */
int32 ts_ownm = 0;                                      /* tape owns msg */
int32 ts_qatn = 0;                                      /* queued attn */
int32 ts_bcmd = 0;                                      /* boot cmd */
int32 ts_time = 10;                                     /* record latency */
static uint16 cpy_buf[MAX_PLNT];                        /* copy buffer */

DEVICE ts_dev;
t_stat ts_rd (int32 *data, int32 PA, int32 access);
t_stat ts_wr (int32 data, int32 PA, int32 access);
t_stat ts_svc (UNIT *uptr);
t_stat ts_reset (DEVICE *dptr);
t_stat ts_attach (UNIT *uptr, char *cptr);
t_stat ts_detach (UNIT *uptr);
t_stat ts_boot (int32 unitno, DEVICE *dptr);
int32 ts_updtssr (int32 t);
int32 ts_updxs0 (int32 t);
void ts_cmpendcmd (int32 s0, int32 s1);
void ts_endcmd (int32 ssf, int32 xs0f, int32 msg);
int32 ts_map_status (t_stat st);
t_stat ts_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *ts_description (DEVICE *dptr);

/* TS data structures

   ts_dev       TS device descriptor
   ts_unit      TS unit list
   ts_reg       TS register list
   ts_mod       TS modifier list
*/

#define IOLN_TS         004

DIB ts_dib = {
    IOBA_AUTO, IOLN_TS, &ts_rd, &ts_wr,
    1, IVCL (TS), VEC_AUTO, { NULL }, IOLN_TS
    };

UNIT ts_unit = { UDATA (&ts_svc, UNIT_ATTABLE + UNIT_ROABLE + UNIT_DISABLE, 0) };

REG ts_reg[] = {
    { GRDATAD (TSSR,         tssr, DEV_RDX, 16, 0, "status register") },
    { GRDATAD (TSBA,         tsba, DEV_RDX, 22, 0, "bus address register") },
    { GRDATAD (TSDBX,       tsdbx, DEV_RDX,  8, 0, "data buffer extension register") },
    { GRDATAD (CHDR,       cmdhdr, DEV_RDX, 16, 0, "command packet header") },
    { GRDATAD (CADL,       cmdadl, DEV_RDX, 16, 0, "command packet low address or count") },
    { GRDATAD (CADH,       cmdadh, DEV_RDX, 16, 0, "command packet high address") },
    { GRDATAD (CLNT,       cmdlnt, DEV_RDX, 16, 0, "command packet length") },
    { GRDATAD (MHDR,       msghdr, DEV_RDX, 16, 0, "message packet header") },
    { GRDATAD (MRFC,       msgrfc, DEV_RDX, 16, 0, "message packet residual frame count") },
    { GRDATAD (MXS0,       msgxs0, DEV_RDX, 16, 0, "message packet extended status 0") },
    { GRDATAD (MXS1,       msgxs1, DEV_RDX, 16, 0, "message packet extended status 1") },
    { GRDATAD (MXS2,       msgxs2, DEV_RDX, 16, 0, "message packet extended status 2") },
    { GRDATAD (MXS3,       msgxs3, DEV_RDX, 16, 0, "message packet extended status 3") },
    { GRDATAD (MSX4,       msgxs4, DEV_RDX, 16, 0, "message packet extended status 4") },
    { GRDATAD (WADL,       wchadl, DEV_RDX, 16, 0, "write char packet low address") },
    { GRDATAD (WADH,       wchadh, DEV_RDX, 16, 0, "write char packet high address") },
    { GRDATAD (WLNT,       wchlnt, DEV_RDX, 16, 0, "write char packet length") },
    { GRDATAD (WOPT,       wchopt, DEV_RDX, 16, 0, "write char packet options") },
    { GRDATAD (WXOPT,     wchxopt, DEV_RDX, 16, 0, "write char packet extended options") },
    { FLDATAD (INT, IREQ (TS), INT_V_TS,           "interrupt pending") },
    { FLDATAD (ATTN,      ts_qatn, 0,              "attention message pending") },
    { FLDATAD (BOOT,      ts_bcmd, 0,              "boot request pending") },
    { FLDATAD (OWNC,      ts_ownc, 0,              "if set, tape owns command buffer") },
    { FLDATAD (OWNM,      ts_ownm, 0,              "if set, tape owns message buffer") },
    { DRDATAD (TIME,      ts_time, 24,             "delay"), PV_LEFT + REG_NZ },
    { DRDATAD (POS,   ts_unit.pos, T_ADDR_W,       "position"), PV_LEFT + REG_RO },
    { GRDATA  (DEVADDR, ts_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC, ts_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB ts_mod[] = {
    { MTUF_WLK,         0, "write enabled",  "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable tape drive" },
    { MTUF_WLK,  MTUF_WLK, "write locked",   "LOCKED", 
        NULL, NULL, NULL, "Write lock tape drive"  },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0,       "FORMAT", "FORMAT",
        &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, "Set/Display tape format (SIMH, E11, TPC, P7B)" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0,       "CAPACITY", "CAPACITY",
        &sim_tape_set_capac, &sim_tape_show_capac, NULL, "Set/Display capacity" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004,     "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV, 0,                 "VECTOR", NULL,
        NULL, &show_vec, NULL, "Interrupt vector" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_REG  0x0001                                 /* display read/write register access */
#define DBG_REQ  0x0002                                 /* display transfer requests */
#define DBG_TAP  MTSE_DBG_STR                           /* display sim_tape and tape structure detail */
#define DBG_POS  MTSE_DBG_POS                           /* display position activities */
#define DBG_DAT  MTSE_DBG_DAT                           /* display transfer data */

DEBTAB ts_debug[] = {
  {"REG",    DBG_REG,   "display read/write register access"},
  {"REQ",    DBG_REQ,   "display transfer requests"},
  {"TAPE",   DBG_TAP,   "display sim_tape and tape structure detail"},
  {"POS",    DBG_POS,   "display position activities"},
  {"DATA",   DBG_DAT,   "display transfer data"},
  {0}
};

DEVICE ts_dev = {
    "TS", &ts_unit, ts_reg, ts_mod,
    1, 10, T_ADDR_W, 1, DEV_RDX, 8,
    NULL, NULL, &ts_reset,
    &ts_boot, &ts_attach, &ts_detach,
    &ts_dib, DEV_DISABLE | TS_DIS | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_TAPE, 0,
    ts_debug, NULL, NULL, &ts_help, NULL, NULL, 
    &ts_description
    };

/* I/O dispatch routines, I/O addresses 17772520 - 17772522

   17772520     TSBA    read/write
   17772522     TSSR    read/write
*/

t_stat ts_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* TSBA */
        *data = tsba & DMASK;                           /* low 16b of ba */
        break;
    case 1:                                             /* TSSR */
        *data = tssr = ts_updtssr (tssr);               /* update tssr */
        break;
        }

sim_debug(DBG_REG, &ts_dev, "ts_rd(PA=0x%08X [%s], access=%d): 0x%04X\n", PA, ((PA >> 1) & 01) ? "TSBA" : "TSSR", access, *data);

return SCPE_OK;
}

t_stat ts_wr (int32 data, int32 PA, int32 access)
{
int32 i, t;

sim_debug(DBG_REG, &ts_dev, "ts_wr(PA=0x%08X [%s], access=%d): 0x%04X\n", PA, ((PA >> 1) & 01) ? "TSDB" : "TSSR", access, data);

switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* TSDB */
        if ((tssr & TSSR_SSR) == 0) {                   /* ready? */
            tssr = tssr | TSSR_RMR;                     /* no, refuse */
            break;
            }
        tsba = ((tsdbx & TSDBX_M_XA) << 18) |           /* form pkt addr */
            ((data & 03) << 16) | (data & 0177774);
        tsdbx = 0;                                      /* clr tsdbx */
        tssr = ts_updtssr (tssr & TSSR_NBA);            /* clr ssr, err */
        msgxs0 = ts_updxs0 (msgxs0 & ~XS0_ALLCLR);      /* clr, upd xs0 */
        msgrfc = msgxs1 = msgxs2 = msgxs3 = msgxs4 = 0; /* clr status */
        CLR_INT (TS);                                   /* clr int req */
        t = Map_ReadW (tsba, CMD_PLNT << 1, cpy_buf);   /* read cmd pkt */
        tsba = tsba + ((CMD_PLNT << 1) - t);            /* incr tsba */
        if (t) {                                        /* nxm? */
            ts_endcmd (TSSR_NXM + TC5, 0, MSG_ACK|MSG_MNEF|MSG_CFAIL);
            return SCPE_OK;
            }
        for (i = 0; i < CMD_PLNT; i++)                  /* copy packet */
            tscmdp[i] = cpy_buf[i];
        ts_ownc = ts_ownm = 1;                          /* tape owns all */
        sim_activate (&ts_unit, ts_time);               /* activate */
        break;

    case 1:                                             /* TSSR */
        if (PA & 1) {                                   /* TSDBX */
            if (UNIBUS)                                 /* not in TS11 */
                return SCPE_OK;
            if (tssr & TSSR_SSR) {                      /* ready? */
                tsdbx = data;                           /* save */
                if (data & TSDBX_BOOT) {
                    ts_bcmd = 1;
                    sim_activate (&ts_unit, ts_time);
                    }
                }
            else tssr = tssr | TSSR_RMR;                /* no, err */
            }
        else if (access == WRITE)                       /* reset */
            ts_reset (&ts_dev);
        break;
        }

return SCPE_OK;
}

/* Tape motion routines */

#define XTC(x,t)        (((unsigned) (x) << 16) | (t))
#define GET_X(x)        (((x) >> 16) & 0177777)
#define GET_T(x)        ((x) & 0177777)

int32 ts_map_status (t_stat st)
{
switch (st) {

    case MTSE_OK:
        break;

    case MTSE_TMK:
        msgxs0 = msgxs0 | XS0_MOT;                      /* tape has moved */
        return (XTC (XS0_TMK | XS0_RLS, TC2));

    case MTSE_RECE:                                     /* record in error */
        msgxs0 = msgxs0 | XS0_MOT;                      /* tape has moved */
    case MTSE_INVRL:                                    /* invalid rec lnt */
    case MTSE_IOERR:                                    /* IO error */
        msgxs1 = msgxs1 | XS1_UCOR;                     /* uncorrectable */
        return (XTC (XS0_RLS, TC6));                    /* pos lost */

    case MTSE_FMT:
    case MTSE_UNATT:
    case MTSE_EOM:                                      /* end of medium */
        msgxs3 = msgxs3 | XS3_OPI;                      /* incomplete */
        return (XTC (XS0_RLS, TC6));                    /* pos lost */

    case MTSE_BOT:                                      /* reverse into BOT */
        msgxs3 = msgxs3 | XS3_RIB;                      /* set status */
        return (XTC (XS0_BOT | XS0_RLS, TC2));          /* tape alert */

    case MTSE_WRP:                                      /* write protect */
        msgxs0 = msgxs0 | XS0_WLE | XS0_NEF;            /* can't execute */
        return (XTC (XS0_WLE | XS0_NEF, TC3));
        }

return 0;
}

int32 ts_spacef (UNIT *uptr, int32 fc, t_bool upd)
{
t_stat st;
t_mtrlnt tbc;

do {
    fc = (fc - 1) & DMASK;                              /* decr wc */
    if (upd)
        msgrfc = fc;
    if ((st = sim_tape_sprecf (uptr, &tbc)))            /* space rec fwd, err? */
        return ts_map_status (st);                      /* map status */
    msgxs0 = msgxs0 | XS0_MOT;                          /* tape has moved */
    } while (fc != 0);
return 0;
}

int32 ts_skipf (UNIT *uptr, int32 fc)
{
t_stat st;
t_mtrlnt tbc;
t_bool tmkprv = FALSE;

msgrfc = fc;
if (sim_tape_bot (uptr) && (wchopt & WCH_ENB))
    tmkprv = TRUE;
do {
    st = sim_tape_sprecf (uptr, &tbc);                  /* space rec fwd */
    if (st == MTSE_TMK) {                               /* tape mark? */
        msgrfc = (msgrfc - 1) & DMASK;                  /* decr count */
        msgxs0 = msgxs0 | XS0_MOT;                      /* tape has moved */
        if (tmkprv && (wchopt & WCH_ESS))               /* 2nd tmk & ESS? */
            return (XTC ((msgrfc? XS0_RLS: 0) |
                XS0_TMK | XS0_LET, TC2));
        tmkprv = TRUE;                                  /* flag tmk */
        }
    else if (st != MTSE_OK)
        return ts_map_status (st);
    else tmkprv = FALSE;                                /* not a tmk */
    msgxs0 = msgxs0 | XS0_MOT;                          /* tape has moved */
    } while (msgrfc != 0);
return 0;
}

int32 ts_spacer (UNIT *uptr, int32 fc, t_bool upd)
{
int32 st;
t_mtrlnt tbc;

do {
    fc = (fc - 1) & DMASK;                              /* decr wc */
    if (upd)
        msgrfc = fc;
    if ((st = sim_tape_sprecr (uptr, &tbc)))            /* space rec rev, err? */
        return ts_map_status (st);                      /* map status */
    msgxs0 = msgxs0 | XS0_MOT;                          /* tape has moved */
    } while (fc != 0);
return 0;
}

int32 ts_skipr (UNIT *uptr, int32 fc)
{
t_stat st;
t_mtrlnt tbc;
t_bool tmkprv = FALSE;

msgrfc = fc;
do {
    st = sim_tape_sprecr (uptr, &tbc);                  /* space rec rev */
    if (st == MTSE_TMK) {                               /* tape mark? */
        msgrfc = (msgrfc - 1) & DMASK;                  /* decr count */
        msgxs0 = msgxs0 | XS0_MOT;                      /* tape has moved */
        if (tmkprv && (wchopt & WCH_ESS))               /* 2nd tmk & ESS? */
            return (XTC ((msgrfc? XS0_RLS: 0) |
                XS0_TMK | XS0_LET, TC2));
        tmkprv = TRUE;                                  /* flag tmk */
        }
    else if (st != MTSE_OK)
        return ts_map_status (st);
    else tmkprv = FALSE;                                /* not a tmk */
    msgxs0 = msgxs0 | XS0_MOT;                          /* tape has moved */
    } while (msgrfc != 0);
return 0;
}

int32 ts_readf (UNIT *uptr, uint32 fc)
{
t_stat st;
t_mtrlnt i, t, tbc, wbc;
int32 wa;

msgrfc = fc;
st = sim_tape_rdrecf (uptr, tsxb, &tbc, MT_MAXFR);      /* read rec fwd */
if (st != MTSE_OK)                                      /* error? */
    return ts_map_status (st);
if (fc == 0)                                            /* byte count */
    fc = 0200000;
tsba = (cmdadh << 16) | cmdadl;                         /* buf addr */
wbc = (tbc > fc)? fc: tbc;                              /* cap buf size */
msgxs0 = msgxs0 | XS0_MOT;                              /* tape has moved */
if (cmdhdr & CMD_SWP) {                                 /* swapped? */
    for (i = 0; i < wbc; i++) {                         /* copy buffer */
        wa = tsba ^ 1;                                  /* apply OPP */
        if (Map_WriteB (wa, 1, &tsxb[i])) {             /* store byte, nxm? */
            tssr = ts_updtssr (tssr | TSSR_NXM);        /* set error */
            return (XTC (XS0_RLS, TC4));
            }
        tsba = tsba + 1;
        msgrfc = (msgrfc - 1) & DMASK;
        }
    }
else {
    t = Map_WriteB (tsba, wbc, tsxb);                   /* store record */
    tsba = tsba + (wbc - t);                            /* update tsba */
    if (t) {                                            /* nxm? */
        tssr = ts_updtssr (tssr | TSSR_NXM);            /* set error */
        return (XTC (XS0_RLS, TC4));
        }
    msgrfc = (msgrfc - (wbc - t)) & DMASK;              /* update fc */
    }
if (msgrfc)                                             /* buf too big? */
    return (XTC (XS0_RLS, TC2));
if (tbc > wbc)                                          /* rec too big? */
    return (XTC (XS0_RLL, TC2));
return 0;
}

int32 ts_readr (UNIT *uptr, uint32 fc)
{
t_stat st;
t_mtrlnt i, tbc, wbc;
int32 wa;

msgrfc = fc;
st = sim_tape_rdrecr (uptr, tsxb, &tbc, MT_MAXFR);      /* read rec rev */
if (st != MTSE_OK)                                      /* error? */
    return ts_map_status (st);
if (fc == 0)                                            /* byte count */
    fc = 0200000;
tsba = ((cmdadh << 16) | cmdadl) + fc;                  /* buf addr */
wbc = (tbc > fc)? fc: tbc;                              /* cap buf size */
msgxs0 = msgxs0 | XS0_MOT;                              /* tape has moved */
for (i = wbc; i > 0; i--) {                             /* copy buffer */
    tsba = tsba - 1;
    wa = (cmdhdr & CMD_SWP)? tsba ^ 1: tsba;            /* apply OPP */
    if (Map_WriteB (wa, 1, &tsxb[i - 1])) {             /* store byte, nxm? */
        tssr = ts_updtssr (tssr | TSSR_NXM);
        return (XTC (XS0_RLS, TC4));
        }
    msgrfc = (msgrfc - 1) & DMASK;
    }
if (msgrfc)                                             /* buf too big? */
    return (XTC (XS0_RLS, TC2));
if (tbc > wbc)                                          /* rec too big? */
    return (XTC (XS0_RLL, TC2));
return 0;
}

int32 ts_write (UNIT *uptr, int32 fc)
{
int32 i, t;
uint32 wa;
t_stat st;

msgrfc = fc;
if (fc == 0)                                            /* byte count */
    fc = 0200000;
tsba = (cmdadh << 16) | cmdadl;                         /* buf addr */
if (cmdhdr & CMD_SWP) {                                 /* swapped? */
    for (i = 0; i < fc; i++) {                          /* copy mem to buf */
        wa = tsba ^ 1;                                  /* apply OPP */
        if (Map_ReadB (wa, 1, &tsxb[i])) {              /* fetch byte, nxm? */
            tssr = ts_updtssr (tssr | TSSR_NXM);
            return TC5;
            }
        tsba = tsba + 1;
        }
    }
else {
    t = Map_ReadB (tsba, fc, tsxb);                     /* fetch record */
    tsba = tsba + (fc - t);                             /* update tsba */
    if (t) {                                            /* nxm? */
        tssr = ts_updtssr (tssr | TSSR_NXM);
        return TC5;
        }
    }
if ((st = sim_tape_wrrecf (uptr, tsxb, fc)))            /* write rec, err? */
    return ts_map_status (st);                          /* return status */
msgxs0 = msgxs0 | XS0_MOT;                              /* tape has moved */
msgrfc = 0;
if (sim_tape_eot (&ts_unit))                            /* EOT on write? */
    return XTC (XS0_EOT, TC2);
return 0;
}

int32 ts_wtmk (UNIT *uptr)
{
t_stat st;

if ((st = sim_tape_wrtmk (uptr)))                       /* write tmk, err? */
    return ts_map_status (st);                          /* return status */
msgxs0 = msgxs0 | XS0_MOT;                              /* tape has moved */
if (sim_tape_eot (&ts_unit))                            /* EOT on write? */
    return XTC (XS0_EOT, TC2);
return XTC (XS0_TMK, TC0);
}

/* Unit service */

t_stat ts_svc (UNIT *uptr)
{
int32 i, t, bc, fnc, mod, st0, st1;

static const int32 fnc_mod[CMD_N_FNC] = {               /* max mod+1 0 ill */
 0, 4, 0, 0, 1, 2, 1, 0,                                /* 00 - 07 */
 5, 3, 5, 1, 0, 0, 0, 1,                                /* 10 - 17 */
 0, 0, 0, 0, 0, 0, 0, 0,                                /* 20 - 27 */
 0, 0, 0, 0, 0, 0, 0, 0                                 /* 30 - 37 */
 };
static const int32 fnc_flg[CMD_N_FNC] = {
 0, FLG_MO+FLG_AD, 0, 0, 0, FLG_MO+FLG_WR+FLG_AD, FLG_AD, 0,
 FLG_MO, FLG_MO+FLG_WR, FLG_MO, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0,                                /* 20 - 27 */
 0, 0, 0, 0, 0, 0, 0, 0                                 /* 30 - 37 */
 };
static const char *fnc_name[CMD_N_FNC] = {
 "0", "READ", "2", "3", "WCHR", "WRITE", "WSSM", "7",
 "POS", "FMT", "CTL", "INIT", "14", "15", "16", "GSTA",
 "20", "21", "22", "23", "24", "25", "26", "27",
 "30", "31", "32", "33", "34", "35", "36", "37"
 };

if (ts_bcmd) {                                          /* boot? */
    ts_bcmd = 0;                                        /* clear flag */
    sim_tape_rewind (uptr);                             /* rewind */
    if (uptr->flags & UNIT_ATT) {                       /* attached? */
        cmdlnt = cmdadh = cmdadl = 0;                   /* defang rd */
        ts_spacef (uptr, 1, FALSE);                     /* space fwd */
        ts_readf (uptr, 512);                           /* read blk */
        tssr = ts_updtssr (tssr | TSSR_SSR);
        }
    else tssr = ts_updtssr (tssr | TSSR_SSR | TC3);
    if (cmdhdr & CMD_IE)
        SET_INT (TS);
    return SCPE_OK;
    }

if (!(cmdhdr & CMD_ACK)) {                              /* no acknowledge? */
    tssr = ts_updtssr (tssr | TSSR_SSR);                /* set rdy, int */
    if (cmdhdr & CMD_IE)
        SET_INT (TS);
    ts_ownc = ts_ownm = 0;                              /* CPU owns all */
    return SCPE_OK;
    }
fnc = GET_FNC (cmdhdr);                                 /* get fnc+mode */
mod = GET_MOD (cmdhdr);
sim_debug (DBG_REQ, &ts_dev, ">>STRT: cmd=%s, mod=%o, buf=%o, lnt=%d, pos=%" T_ADDR_FMT "u\n",
        fnc_name[fnc], mod, cmdadl, cmdlnt, ts_unit.pos);
if ((fnc != FNC_WCHR) && (tssr & TSSR_NBA)) {           /* ~wr chr & nba? */
    ts_endcmd (TC3, 0, 0);                              /* error */
    return SCPE_OK;
    }
if (ts_qatn && (wchopt & WCH_EAI)) {                    /* attn pending? */
    ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);            /* send attn msg */
    SET_INT (TS);                                       /* set interrupt */
    ts_qatn = 0;                                        /* not pending */
    return SCPE_OK;
    }
if (cmdhdr & CMD_CVC)                                   /* cvc? clr vck */
    msgxs0 = msgxs0 & ~XS0_VCK;
if ((cmdhdr & CMD_MBZ) || (mod >= fnc_mod[fnc])) {      /* test mbz */
    ts_endcmd (TC3, XS0_ILC, MSG_ACK | MSG_MILL | MSG_CFAIL);
    return SCPE_OK;
    }
if ((fnc_flg[fnc] & FLG_MO) &&                          /* mot+(vck|!att)? */
    ((msgxs0 & XS0_VCK) || !(uptr->flags & UNIT_ATT))) {
    ts_endcmd (TC3, XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
    return SCPE_OK;
    }
if ((fnc_flg[fnc] & FLG_WR) &&                          /* write? */
    sim_tape_wrp (uptr)) {                              /* write lck? */
    ts_endcmd (TC3, XS0_WLE | XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
    return SCPE_OK;
    }
if ((((fnc == FNC_READ) && (mod == 1)) ||               /* read rev */
     ((fnc == FNC_POS) && (mod & 1))) &&                /* space rev */
     sim_tape_bot (uptr)) {                             /* BOT? */
    ts_endcmd (TC3, XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
    return SCPE_OK;
    }
if ((fnc_flg[fnc] & FLG_AD) && (cmdadh & ADDRTEST)) {   /* buf addr > 22b? */
    ts_endcmd (TC3, XS0_ILA, MSG_ACK | MSG_MILL | MSG_CFAIL);
    return SCPE_OK;
    }

st0 = st1 = 0;
switch (fnc) {                                          /* case on func */

    case FNC_INIT:                                      /* init */
        if (!sim_tape_bot (uptr))                       /* set if tape moves */
            msgxs0 = msgxs0 | XS0_MOT;
        sim_tape_rewind (uptr);                         /* rewind */
    case FNC_WSSM:                                      /* write mem */
    case FNC_GSTA:                                      /* get status */
        ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);         /* send end packet */
        return SCPE_OK;

    case FNC_WCHR:                                      /* write char */
        if ((cmdadh & ADDRTEST) || (cmdadl & 1) || (cmdlnt < 6)) {
            ts_endcmd (TSSR_NBA | TC3, XS0_ILA, 0);
            break;
            }
        tsba = (cmdadh << 16) | cmdadl;
        bc = ((WCH_PLNT << 1) > cmdlnt)? cmdlnt: WCH_PLNT << 1;
        t = Map_ReadW (tsba, bc, cpy_buf);              /* fetch packet */
        tsba = tsba + (bc - t);                         /* inc tsba */
        if (t) {                                        /* nxm? */
            ts_endcmd (TSSR_NBA | TSSR_NXM | TC5, 0, 0);
            return SCPE_OK;
            }
        for (i = 0; i < (bc / 2); i++)                  /* copy packet */
            tswchp[i] = cpy_buf[i];
        if ((wchlnt < ((MSG_PLNT - 1) * 2)) || (wchadh & 0177700) || (wchadl & 1))
            ts_endcmd (TSSR_NBA | TC3, 0, 0);
        else {
            msgxs2 = msgxs2 | XS2_XTF | 1;
            tssr = ts_updtssr (tssr & ~TSSR_NBA);
            ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);
            }
        return SCPE_OK;

    case FNC_CTL:                                       /* control */
        switch (mod) {                                  /* case mode */

        case 00:                                        /* msg buf rls */
            tssr = ts_updtssr (tssr | TSSR_SSR);        /* set SSR */
            if (wchopt & WCH_ERI)
                SET_INT (TS);
            ts_ownc = 0; ts_ownm = 1;                   /* keep msg */
            break;

        case 01:                                        /* rewind and unload */
            if (!sim_tape_bot (uptr))                   /* if tape moves */
                msgxs0 = msgxs0 | XS0_MOT;
            sim_tape_detach (uptr);                     /* unload */
            ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);
            break;

        case 02:                                        /* clean */
            ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);     /* nop */
            break;

        case 03:                                        /* undefined */
            ts_endcmd (TC3, XS0_ILC, MSG_ACK | MSG_MILL | MSG_CFAIL);
            return SCPE_OK;

        case 04:                                        /* rewind */
            if (!sim_tape_bot (uptr))                   /* if tape moves */
                msgxs0 = msgxs0 | XS0_MOT;
            sim_tape_rewind (uptr);
            ts_endcmd (TC0, XS0_BOT, MSG_ACK | MSG_CEND);
            break;
            }
        break;

    case FNC_READ:                                      /* read */
        switch (mod) {                                  /* case mode */

        case 00:                                        /* fwd */
            st0 = ts_readf (uptr, cmdlnt);              /* read */
            break; 

        case 01:                                        /* back */
            st0 = ts_readr (uptr, cmdlnt);              /* read */
            break;

        case 02:                                        /* reread fwd */
            if (cmdhdr & CMD_OPP) {                     /* opposite? */
                st0 = ts_readr (uptr, cmdlnt);
                st1 = ts_spacef (uptr, 1, FALSE);
                }
            else {
                st0 = ts_spacer (uptr, 1, FALSE);
                st1 = ts_readf (uptr, cmdlnt);
                }
            break;

        case 03:                                        /* reread back */
            if (cmdhdr & CMD_OPP) {                     /* opposite */
                st0 = ts_readf (uptr, cmdlnt);
                st1 = ts_spacer (uptr, 1, FALSE);
                }
            else {
                st0 = ts_spacef (uptr, 1, FALSE);
                st1 = ts_readr (uptr, cmdlnt);
                }
            break;
            }
        ts_cmpendcmd (st0, st1);
        break;

    case FNC_WRIT:                                      /* write */
        switch (mod) {                                  /* case mode */

        case 00:                                        /* write */
            st0 = ts_write (uptr, cmdlnt);
            break;

        case 01:                                        /* rewrite */
            st0 = ts_spacer (uptr, 1, FALSE);
            st1 = ts_write (uptr, cmdlnt);
            break;
            }
        ts_cmpendcmd (st0, st1);
        break;

    case FNC_FMT:                                       /* format */
        switch (mod) {                                  /* case mode */

        case 00:                                        /* write tmk */
            st0 = ts_wtmk (uptr);
            break;

        case 01:                                        /* erase */
            break;

        case 02:                                        /* retry tmk */
            st0 = ts_spacer (uptr, 1, FALSE);
            st1 = ts_wtmk (uptr);
            break;
            }
        ts_cmpendcmd (st0, st1);
        break;

    case FNC_POS:                                       /* position */
        switch (mod) {                                  /* case mode */

        case 00:                                        /* space fwd */
            st0 = ts_spacef (uptr, cmdadl, TRUE);
            break;

        case 01:                                        /* space rev */
            st0 = ts_spacer (uptr, cmdadl, TRUE);
            break;

        case 02:                                        /* space ffwd */
            st0 = ts_skipf (uptr, cmdadl);
            break;

        case 03:                                        /* space frev */
            st0 = ts_skipr (uptr, cmdadl);
            break;

        case 04:                                        /* rewind */
            if (!sim_tape_bot (uptr))                   /* if tape moves */
                msgxs0 = msgxs0 | XS0_MOT;
            sim_tape_rewind (uptr);
            break;
            }
        ts_cmpendcmd (st0, 0);
        break;
        }

return SCPE_OK;
}

/* Utility routines */

int32 ts_updtssr (int32 t)
{
t = (t & ~TSSR_EMA) | ((tsba >> (16 - TSSR_V_EMA)) & TSSR_EMA);
if (ts_unit.flags & UNIT_ATT)
    t = t & ~TSSR_OFL;
else t = t | TSSR_OFL;
return (t & ~TSSR_MBZ);
}

int32 ts_updxs0 (int32 t)
{
t = (t & ~(XS0_ONL | XS0_WLK | XS0_BOT | XS0_IE)) | XS0_PET;
if (ts_unit.flags & UNIT_ATT) {
    t = t | XS0_ONL;
    if (sim_tape_wrp (&ts_unit))
        t = t | XS0_WLK;
    if (sim_tape_bot (&ts_unit))
        t = (t | XS0_BOT) & ~XS0_EOT;
    if (sim_tape_eot (&ts_unit))
        t = (t | XS0_EOT) & ~XS0_BOT;
    }
else t = t & ~XS0_EOT;
if (cmdhdr & CMD_IE)
    t = t | XS0_IE;
return t;
}

void ts_cmpendcmd (int32 s0, int32 s1)
{
int32 xs0, ssr, tc;
static const int32 msg[8] = {
 MSG_ACK | MSG_CEND, MSG_ACK | MSG_MATN | MSG_CATN,
 MSG_ACK | MSG_CEND, MSG_ACK | MSG_CFAIL,
 MSG_ACK | MSG_CERR, MSG_ACK | MSG_CERR,
 MSG_ACK | MSG_CERR, MSG_ACK | MSG_CERR
 };

xs0 = GET_X (s0) | GET_X (s1);                          /* or XS0 errs */
s0 = GET_T (s0);                                        /* get SSR errs */
s1 = GET_T (s1);
ssr = (s0 | s1) & ~TSSR_TC;                             /* or SSR errs */
tc = MAX (GET_TC (s0), GET_TC (s1));                    /* max term code */
ts_endcmd (ssr | (tc << TSSR_V_TC), xs0, msg[tc]);      /* end cmd */
return;
}

void ts_endcmd (int32 tc, int32 xs0, int32 msg)
{
int32 i, t;

msgxs0 = ts_updxs0 (msgxs0 | xs0);                      /* update XS0 */
if (wchxopt & WCHX_HDS)                                 /* update XS4 */
    msgxs4 = msgxs4 | XS4_HDS;
if (msg && !(tssr & TSSR_NBA)) {                        /* send end pkt */
    msghdr = msg;
    msglnt = wchlnt - 4;                                /* exclude hdr, bc */
    tsba = (wchadh << 16) | wchadl;
    for (i = 0; (i < MSG_PLNT) && (i < (wchlnt / 2)); i++)
        cpy_buf[i] = (uint16) tsmsgp[i];                /* copy buffer */
    t = Map_WriteW (tsba, i << 1, cpy_buf);             /* write to mem */
    tsba = tsba + ((i << 1) - t);                       /* incr tsba */
    if (t) {                                            /* nxm? */
        tssr = tssr | TSSR_NXM;
        tc = (tc & ~TSSR_TC) | TC4;
        }
    }
tssr = ts_updtssr (tssr | tc | TSSR_SSR | (tc? TSSR_SC: 0));
if (cmdhdr & CMD_IE)
    SET_INT (TS);
ts_ownm = 0; ts_ownc = 0;
sim_debug (DBG_REQ, &ts_dev, ">>CMPL: sta=%o, tc=%o, rfc=%d, pos=%" T_ADDR_FMT "u\n",
                        msgxs0, GET_TC (tssr), msgrfc, ts_unit.pos);
return;
}

/* Device reset */

t_stat ts_reset (DEVICE *dptr)
{
int32 i;

sim_tape_rewind (&ts_unit);
tsba = tsdbx = 0;
ts_ownc = ts_ownm = 0;
ts_bcmd = 0;
ts_qatn = 0;
tssr = ts_updtssr (TSSR_NBA | TSSR_SSR);
for (i = 0; i < CMD_PLNT; i++)
    tscmdp[i] = 0;
for (i = 0; i < WCH_PLNT; i++)
    tswchp[i] = 0;
for (i = 0; i < MSG_PLNT; i++)
    tsmsgp[i] = 0;
msgxs0 = ts_updxs0 (XS0_VCK);
CLR_INT (TS);
if (tsxb == NULL)
    tsxb = (uint8 *) calloc (MT_MAXFR, sizeof (uint8));
if (tsxb == NULL)
    return SCPE_MEM;
return auto_config (0, 0);
}

/* Attach */

t_stat ts_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);                       /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
tssr = tssr & ~TSSR_OFL;                                /* clr offline */
if ((tssr & TSSR_NBA) || !(wchopt & WCH_EAI))           /* attn msg? */
    return r;
if (ts_ownm) {                                          /* own msg buf? */
    ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);            /* send attn */
    SET_INT (TS);                                       /* set interrupt */
    ts_qatn = 0;                                        /* don't queue */
    }
else ts_qatn = 1;                                       /* else queue */
return r;
}

/* Detach routine */

t_stat ts_detach (UNIT* uptr)
{
t_stat r;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
r = sim_tape_detach (uptr);                             /* detach unit */
if (r != SCPE_OK)
    return r;                                           /* error? */
tssr = tssr | TSSR_OFL;                                 /* set offline */
if ((tssr & TSSR_NBA) || !(wchopt & WCH_EAI))           /* attn msg? */
    return r;
if (ts_ownm) {                                          /* own msg buf? */
    ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);            /* send attn */
    SET_INT (TS);                                       /* set interrupt */
    ts_qatn = 0;                                        /* don't queue */
    }
else ts_qatn = 1;                                       /* else queue */
return r;
}

/* Boot */

#if defined (VM_PDP11)
#define BOOT_START      01000
#define BOOT_CSR0       (BOOT_START + 006)
#define BOOT_CSR1       (BOOT_START + 012)
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    0012706, 0001000,               /* mov #boot_start, sp */
    0012700, 0172520,               /* mov #tsba, r0 */
    0012701, 0172522,               /* mov #tssr, r1 */
    0005011,                        /* clr (r1)             ; init, rew */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0012710, 0001070,               /* mov #pkt1, (r0)      ; set char */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0012710, 0001110,               /* mov #pkt2, (r0)      ; read, skip */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0012710, 0001110,               /* mov #pkt2, (r0)      ; read */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0005711,                        /* tst (r1)             ; err? */
    0100421,                        /* bmi hlt */
    0005000,                        /* clr r0 */
    0012704, 0001066+020,           /* mov #sgnt+20, r4 */
    0005007,                        /* clr r7 */
    0046523,                        /* sgnt: "SM" */
    0140004,                        /* pkt1: 140004, wcpk, 0, 8. */
    0001100,
    0000000,
    0000010,
    0001122,                        /* wcpk: msg, 0, 14., 0 */
    0000000,
    0000016,
    0000000,
    0140001,                        /* pkt2: 140001, 0, 0, 512. */
    0000000,
    0000000,
    0001000,
    0000000                         /* hlt:  halt */
                                    /* msg: .blk 4 */
    };

t_stat ts_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint16 *M;

sim_tape_rewind (&ts_unit);
for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_CSR0 >> 1] = ts_dib.ba & DMASK;
M[BOOT_CSR1 >> 1] = (ts_dib.ba & DMASK) + 02;
cpu_set_boot (BOOT_START);
return SCPE_OK;
}
 
#else

t_stat ts_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}
#endif

t_stat ts_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "TS11 Magnetic Tape (TS)\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.  The\n");
fprintf (st, "bad block option can be used only when a unit is attached to a file.\n");
fprintf (st, "The TS11 does not support the BOOT command.\n");
#if defined (VM_PDP11)
fprintf (st, "The TS11 device supports the BOOT command.\n");
#endif
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         processed as\n");
fprintf (st, "    not attached  tape not ready\n\n");
fprintf (st, "    end of file   bad tape\n");
fprintf (st, "    OS I/O error  fatal tape error\n\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

char *ts_description (DEVICE *dptr)
{
return (UNIBUS) ? "TS11 magnetic tape controller" :
                  "TSV11/TSV05 magnetic tape controller ";
}
