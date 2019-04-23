/* vax820_stddev.c: VAX 8200 standard I/O devices

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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

   tti          console input
   tto          console output
   cs           console floppy
   todr         TODR clock
   tmr          interval timer
*/

#include "vax_defs.h"
#include "sim_tmxr.h"

/* Terminal definitions */

#define RXCS_RD         (CSR_DONE + CSR_IE)             /* terminal input */
#define RXCS_WR         (CSR_IE)
#define RXDB_ERR        0x8000                          /* error */
#define RXDB_OVR        0x4000                          /* overrun */
#define RXDB_FRM        0x2000                          /* framing error */
#define TXCS_RD         (CSR_DONE + CSR_IE)             /* terminal output */
#define TXCS_WR         (CSR_IE)
#define TXDB_V_SEL      8                               /* unit select */
#define TXDB_M_SEL      0xF
#define  TXDB_FDAT      0x1                             /* floppy data */
#define  TXDB_COMM      0x3                             /* console mem read */
#define  TXDB_FCMD      0x9                             /* floppy cmd */
#define  TXDB_MISC      0xF                             /* console misc */
#define COMM_LNT        0200                            /* comm region lnt */
#define COMM_MASK       (COMM_LNT - 1)                  /* comm region mask */
#define  COMM_GH        0144                            /* GH flag */
#define  COMM_WRMS      0145                            /* warm start */
#define  COMM_CLDS      0146                            /* cold start */
#define  COMM_APTL      0147                            /* APT load */
#define  COMM_LAST      0150                            /* last position */
#define  COMM_AUTO      0151                            /* auto restart */
#define  COMM_PCSV      0152                            /* PCS version */
#define  COMM_WCSV      0153                            /* WCS version */
#define  COMM_WCSS      0154                            /* WCS secondary */
#define  COMM_FPLV      0155                            /* FPLA version */
#define  COMM_MTCH_785  0153                            /* 785 PCS/WCS version */
#define  COMM_WCSP_785  0154                            /* 785 WCS version */
#define  COMM_WCSS_785  0155                            /* 785 WCS secondary */
#define COMM_DATA       0x300                           /* comm data return */
#define MISC_MASK        0xFF                           /* console data mask */
#define  MISC_SWDN       0x1                            /* software done */
#define  MISC_BOOT       0x2                            /* reboot */
#define  MISC_CLWS       0x3                            /* clear warm start */
#define  MISC_CLCS       0x4                            /* clear cold start */
#define TXDB_SEL        (TXDB_M_SEL << TXDB_V_SEL)      /* non-terminal */
#define TXDB_GETSEL(x)  (((x) >> TXDB_V_SEL) & TXDB_M_SEL)

/* Clock definitions */

#define TMR_CSR_ERR     0x80000000                      /* error W1C */
#define TMR_CSR_DON     0x00000080                      /* done W1C */
#define TMR_CSR_IE      0x00000040                      /* int enb RW */
#define TMR_CSR_SGL     0x00000020                      /* single WO */
#define TMR_CSR_XFR     0x00000010                      /* xfer WO */
#define TMR_CSR_RUN     0x00000001                      /* run RW */
#define TMR_CSR_RD      (TMR_CSR_W1C | TMR_CSR_WR)
#define TMR_CSR_W1C     (TMR_CSR_ERR | TMR_CSR_DON)
#define TMR_CSR_WR      (TMR_CSR_IE | TMR_CSR_RUN)
#define TMR_INC         10000                           /* usec/interval */
#define CLK_DELAY       5000                            /* 100 Hz */
#define TMXR_MULT       1                               /* 100 Hz */

static BITFIELD tmr_iccs_bits [] = {
    BIT(RUN),                                   /* Run */
    BITNCF(3),                                  /* unused */
    BIT(XFR),                                   /* Transfer */
    BIT(SGL),                                   /* Single */
    BIT(IE),                                    /* Interrupt Enable */
    BIT(DON),                                   /* Done */
    BITNCF(23),                                 /* unused */
    BIT(ERR),                                   /* Error */
    ENDBITS
    };

/* Floppy definitions */

#define FL_CS0          2
#define FL_CS1          3
#define FL_CS2          4
#define FL_CS3          5
#define FL_CS4          6
#define FL_CS5          7
#define FL_EB           8
#define FL_CA           9
#define FL_GO           10
#define FL_FB           11

#define FLCS0_SS        0x01                            /* side select */
#define FLCS0_DKS       0x02                            /* disk select */
#define FLCS0_DS        0x04                            /* drive select */
#define FLCS0_EMT       0x08                            /* extended motor timeout (write only) */
#define FLCS0_DONE      0x08                            /* done (read only) */
#define FLCS0_V_FNC     4                               /* function code */
#define FLCS0_M_FNC     0x7
#define FLCS0_FNC       (FLCS0_M_FNC << FLCS0_V_FNC)
#define  FL_FNCST       0x0                             /* read status */
#define  FL_FNCMM       0x1                             /* maintenance mode */
#define  FL_FNCRD       0x2                             /* restore drive */
#define  FL_FNCIN       0x3                             /* initialise */
#define  FL_FNCRS       0x4                             /* read sector */
#define  FL_FNCEX       0x5                             /* extended function */
#define  FL_FNCRA       0x6                             /* read address */
#define  FL_FNCWS       0x7                             /* write sector */
#define FLCS0_ERR       0x80                            /* error (read only) */
#define FLCS0_WR        0x7F

const char *fl_fncnames[] = {
    "read status",
    "maintenance status",
    "restore drive",
    "initialise",
    "read sector",
    "extended function",
    "read address",
    "write sector"
    };

#define FLCS1_TRK       0x7F                            /* track number */

#define FLCS2_SECT      0x0F                            /* sector number */
#define FLCS2_TRK       0x7F                            /* track number */

#define FLCS3_SECT      0x0F                            /* sector number */

#define FLCS4_D0AV      0x01                            /* disk 0 available */
#define FLCS4_D0DS      0x02                            /* disk 0 double sided */
#define FLCS4_D1AV      0x04                            /* disk 1 available */
#define FLCS4_D1DS      0x08                            /* disk 1 double sided */
#define FLCS4_D2AV      0x10                            /* disk 2 available */
#define FLCS4_D2DS      0x20                            /* disk 2 double sided */
#define FLCS4_D3AV      0x40                            /* disk 3 available */
#define FLCS4_D3DS      0x80                            /* disk 3 double sided */

#define FLCS5_FUNC      0xFF                            /* extended function code */

#define FL_NUMTR        80                              /* tracks/disk */
#define FL_NUMSC        10                              /* sectors/track */
#define FL_NUMBY        512                             /* bytes/sector */
#define FL_INTL         5                               /* interleave */
#define FL_SIZE         (FL_NUMTR * FL_NUMSC * FL_NUMBY)/* bytes/disk */
#define UNIT_V_WLK      (UNIT_V_UF)                     /* write locked */
#define UNIT_WLK        (1u << UNIT_V_UF)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

#define TRACK u3                                        /* current track */
#define CALC_SC(t,s)    (fl_intl[((t) - 1) % FL_INTL][((s) - 1)])
#define CALC_DA(t,s)    ((((t) - 1) * FL_NUMSC) + CALC_SC(t,s)) * FL_NUMBY

int32 tti_csr = 0;                                      /* control/status */
uint32 tti_buftime;                                     /* time input character arrived */
int32 tti_buf = 0;                                      /* buffer */
int32 tti_int = 0;                                      /* interrupt */
int32 tto_csr[KA_NUM] = { 0 };                          /* control/status */
int32 tto_buf = 0;                                      /* buffer */
int32 tto_int = 0;                                      /* interrupt */

int32 tmr_iccs = 0;                                     /* interval timer csr */
uint32 tmr_icr = 0;                                     /* curr interval */
uint32 tmr_nicr = 0;                                    /* next interval */
uint32 tmr_inc = 0;                                     /* timer increment */
int32 tmr_int = 0;                                      /* interrupt */
int32 clk_tps = 100;                                    /* ticks/second */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;                /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* pgm timer poll */
struct todr_battery_info {
    uint32 toy_gmtbase;                                 /* GMT base of set value */
    uint32 toy_gmtbasemsec;                             /* The milliseconds of the set value */
    uint32 toy_endian_plus2;                            /* 2 -> Big Endian, 3 -> Little Endian, invalid otherwise */
    };
typedef struct todr_battery_info TOY;

int32 fl_cs0 = 0;
int32 fl_cs1 = 0;
int32 fl_cs2 = 0;
int32 fl_cs3 = 0;
int32 fl_cs4 = 0;
int32 fl_cs5 = 0;

int32 fl_int = 0;
int32 fl_fnc = 0;                                       /* function */
int32 fl_ecode = 0;                                     /* error code */
int32 fl_track = 0;                                     /* desired track */
int32 fl_sector = 0;                                    /* desired sector */
int32 fl_stopioe = 1;                                   /* stop on error */
int32 fl_swait = 100;                                   /* seek, per track */
int32 fl_cwait = 50;                                    /* command time */
int32 fl_xwait = 20;                                    /* tr set time */
uint8 fl_buf[FL_NUMBY] = { 0 };                         /* sector buffer */
int32 fl_bptr = 0;                                      /* buffer pointer */
static int32 fl_intl[FL_INTL][FL_NUMSC] = {
    { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9 },
    { 4, 9, 0, 5, 1, 6, 2, 7, 3, 8 },
    { 3, 8, 4, 9, 0, 5, 1, 6, 2, 7 },
    { 2, 7, 3, 8, 4, 9, 0, 5, 1, 6 },
    { 1, 6, 2, 7, 3, 8, 4, 9, 0, 5 }
    };


extern int32 cur_cpu;

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tmr_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
const char *tti_description (DEVICE *dptr);
const char *tto_description (DEVICE *dptr);
const char *clk_description (DEVICE *dptr);
const char *tmr_description (DEVICE *dptr);
const char *fl_description (DEVICE *dptr);
t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat clk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat clk_attach (UNIT *uptr, CONST char *cptr);
t_stat clk_detach (UNIT *uptr);
t_stat tmr_reset (DEVICE *dptr);
t_stat fl_svc (UNIT *uptr);
t_stat fl_reset (DEVICE *dptr);
int32 icr_rd (void);
void tmr_sched (uint32 incr);
t_stat todr_resync (void);
t_bool fl_test_xfr (UNIT *uptr, t_bool wr);
void fl_protocol_error (void);

extern int32 con_halt (int32 code, int32 cc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), TMLN_SPD_9600_BPS };

REG tti_reg[] = {
    { HRDATAD (RXDB,       tti_buf,         16, "last data item processed") },
    { HRDATAD (RXCS,       tti_csr,         16, "control/status register") },
    { FLDATAD (INT,        tti_int,          0, "interrupt pending flag") },
    { FLDATAD (DONE,       tti_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,         tti_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,   tti_unit.pos,   T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME, tti_unit.wait,         24, "input polling interval"), PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE,  TT_MODE_7B, "7b", "7B",     NULL, NULL,      NULL, "Set 7 bit mode" },
    { TT_MODE,  TT_MODE_8B, "8b", "8B",     NULL, NULL,      NULL, "Set 8 bit mode" },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &tti_help, NULL, NULL,
    &tti_description
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

UNIT tto_unit[] = {
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT },
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT }
    };

REG tto_reg[] = {
    { HRDATAD (TXDB,       tto_buf,         16, "last data item processed") },
    { HRDATAD (TXCS,       tto_csr,         16, "control/status register") },
    { FLDATAD (INT,        tto_int,          0, "interrupt pending flag") },
    { FLDATAD (DONE,       tto_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,         tto_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,  tto_unit[0].pos, T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME, tto_unit[0].wait,      24, "time from I/O initiation to interrupt"), PV_LEFT + REG_NZ },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE,  TT_MODE_7B, "7b", "7B",     NULL, NULL,      NULL, "Set 7 bit mode" },
    { TT_MODE,  TT_MODE_8B, "8b", "8B",     NULL, NULL,      NULL, "Set 8 bit mode" },
    { TT_MODE,  TT_MODE_7P, "7p", "7P",     NULL, NULL,      NULL, "Set 7 bit mode (suppress non printing output)" },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", tto_unit, tto_reg, tto_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &tto_help, NULL, NULL,
    &tto_description
    };

/* TODR and TMR data structures */

UNIT clk_unit = { UDATA (NULL, UNIT_FIX, sizeof(TOY))};

REG clk_reg[] = {
    { DRDATAD (TIME,                   clk_unit.wait,  24, "initial poll interval"), REG_NZ + PV_LEFT },
    { DRDATAD (POLL,                        tmr_poll,  24, "calibrated poll interval"), REG_NZ + PV_LEFT + REG_HRO },
#if defined (SIM_ASYNCH_IO)
    { DRDATAD (ASYNCH,            sim_asynch_enabled,   1, "asynch I/O enabled flag"), PV_LEFT },
    { DRDATAD (LATENCY,           sim_asynch_latency,  32, "desired asynch interrupt latency"), PV_LEFT },
    { DRDATAD (INST_LATENCY, sim_asynch_inst_latency,  32, "calibrated instruction latency"), PV_LEFT },
#endif
    { NULL }
    };

#define TMR_DB_TODR     0x10    /* TODR */

DEBTAB todr_deb[] = {
    { "TODR",  TMR_DB_TODR,     "TODR activities"},
    { NULL, 0 }
    };

DEVICE clk_dev = {
    "TODR", &clk_unit, clk_reg, NULL,
    1, 0, 8, 4, 0, 32,
    NULL, NULL, &clk_reset,
    NULL, &clk_attach, &clk_detach,
    NULL, DEV_DEBUG, 0, todr_deb, NULL, NULL, &clk_help, NULL, NULL,
    &clk_description
    };

UNIT tmr_unit = { UDATA (&tmr_svc, 0, 0) };                     /* timer */

REG tmr_reg[] = {
    { HRDATADF (ICCS,         tmr_iccs, 32, "interval timer control and status", tmr_iccs_bits) },
    { HRDATAD  (ICR,           tmr_icr, 32, "interval count register") },
    { HRDATAD  (NICR,         tmr_nicr, 32, "next interval count register") },
    { FLDATAD  (INT,           tmr_int,  0, "interrupt request") },
    { DRDATAD  (TPS,           clk_tps,  8, "ticks per second"), REG_NZ + PV_LEFT },
    { HRDATA   (INCR,          tmr_inc, 32), REG_HIDDEN },
    { NULL }
    };

#define TMR_DB_REG      0x01    /* Register Access */
#define TMR_DB_TICK     0x02    /* Ticks */
#define TMR_DB_SCHED    0x04    /* Scheduling */
#define TMR_DB_INT      0x08    /* Interrupts */

DEBTAB tmr_deb[] = {
    { "REG",   TMR_DB_REG,      "Register Access"},
    { "TICK",  TMR_DB_TICK,     "Ticks"},
    { "SCHED", TMR_DB_SCHED,    "Scheduling"},
    { "INT",   TMR_DB_INT,      "Interrupts"},
    { NULL, 0 }
    };

DEVICE tmr_dev = {
    "TMR", &tmr_unit, tmr_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &tmr_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0,
    tmr_deb, NULL, NULL, NULL, NULL, NULL,
    &tmr_description
    };

/* RX01 data structures

   fl_dev       RX device descriptor
   fl_unit      RX unit list
   fl_reg       RX register list
   fl_mod       RX modifier list
*/

UNIT fl_unit[] = {
    { UDATA (&fl_svc,
      UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, FL_SIZE) },
    { UDATA (&fl_svc,
      UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, FL_SIZE) }
    };

REG fl_reg[] = {
    { HRDATAD (FNC,          fl_fnc,  8, "function select") },
    { HRDATAD (ECODE,      fl_ecode,  8, "error code") },
    { HRDATAD (TA,         fl_track,  8, "track address") },
    { HRDATAD (SA,        fl_sector,  8, "sector address") },
    { DRDATAD (BPTR,        fl_bptr,  7, "data buffer pointer")  },
    { DRDATAD (CTIME,      fl_cwait, 24, "command initiation delay"), PV_LEFT },
    { DRDATAD (STIME,      fl_swait, 24, "seek time delay, per track"), PV_LEFT },
    { DRDATAD (XTIME,      fl_xwait, 24, "transfer time delay, per byte"), PV_LEFT },
    { FLDATAD (STOP_IOE, fl_stopioe,  0, "stop on I/O error") },
    { BRDATAD (DBUF,         fl_buf, 16, 8, FL_NUMBY, "data buffer") },
    { NULL }
    };

MTAB fl_mod[] = {
    { UNIT_WLK,         0, "write enabled",  "WRITEENABLED", NULL, NULL, NULL, "Write enable floppy drive" },
    { UNIT_WLK,  UNIT_WLK, "write locked",   "LOCKED", NULL, NULL, NULL, "Write lock floppy drive"  },
    { 0 }
    };

#define FL_DB_REG       0x01                            /* Register Access */
#define FL_DB_FNC       0x02                            /* Functions */
#define FL_DB_INT       0x04                            /* Interrupts */

DEBTAB fl_deb[] = {
    { "REG", FL_DB_REG, "Register Access"},
    { "FNC", FL_DB_FNC, "Functions"},
    { "INT", FL_DB_INT, "Interrupts"},
    { NULL, 0 }
    };

DEVICE fl_dev = {
    "CS", fl_unit, fl_reg, fl_mod,
    2, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &fl_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0,
    fl_deb, NULL, NULL, NULL, NULL, NULL,
    &fl_description
    };

const char *fl_regnames[] = {
    "",             /* 0 spare */
    "",             /* 1 spare */
    "CS0",          /* 2 */
    "CS1",          /* 3 */
    "CS2",          /* 4 */
    "CS3",          /* 5 */
    "CS4",          /* 6 */
    "CS5",          /* 7 */
    "EB",           /* 8 */
    "CA",           /* 9 */
    "GO",           /* 10 */
    "FB"            /* 11 */
    };

/* Terminal MxPR routines

   rxcs_rd/wr   input control/status
   rxdb_rd      input buffer
   txcs_rd/wr   output control/status
   txdb_wr      output buffer
*/

int32 rxcs_rd (void)
{
return (tti_csr & RXCS_RD);
}

void rxcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tti_int = 0;
else {
    if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
        tti_int = 1;
    }
tti_csr = (tti_csr & ~RXCS_WR) | (data & RXCS_WR);
}

int32 rxdb_rd (void)
{
int32 t = tti_buf;                                      /* char + error */

if (tti_csr & CSR_DONE) {                               /* Input pending ? */
    tti_csr = tti_csr & ~CSR_DONE;                      /* clr done */
    tti_buf = tti_buf & BMASK;                          /* clr errors */
    tti_int = 0;
    sim_activate_after_abs (&tti_unit, tti_unit.wait);  /* check soon for more input */
    }
return t;
}

int32 txcs_rd (void)
{
return (tto_csr[cur_cpu] & TXCS_RD);
}

void txcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tto_int &= ~(1u << cur_cpu);
else {
    if ((tto_csr[cur_cpu] & (CSR_DONE + CSR_IE)) == CSR_DONE)
        tto_int |= (1u << cur_cpu);
    }
tto_csr[cur_cpu] = (tto_csr[cur_cpu] & ~TXCS_WR) | (data & TXCS_WR);
}

void txdb_wr (int32 data)
{
if (cur_cpu == 0)
    tto_buf = data & WMASK;                             /* save data */
tto_csr[cur_cpu] = tto_csr[cur_cpu] & ~CSR_DONE;        /* clear flag */
tto_int &= ~(1u << cur_cpu);                            /* clear int */
sim_activate (&tto_unit[cur_cpu], tto_unit[cur_cpu].wait);
}

/* Terminal input service (poll for character) */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */

if ((tti_csr & CSR_DONE) &&                             /* input still pending and < 500ms? */
    ((sim_os_msec () - tti_buftime) < 500))
     return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    tti_buf = RXDB_ERR | RXDB_FRM;
else
    tti_buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
tti_buftime = sim_os_msec ();
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    tti_int = 1;
return SCPE_OK;
}

/* Terminal input reset */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit[0]);
tti_buf = 0;
tti_csr = 0;
tti_int = 0;
sim_activate (&tti_unit, tmr_poll);
return SCPE_OK;
}

t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Console Terminal Input (TTI)\n\n");
fprintf (st, "The terminal input (TTI) polls the console keyboard for input.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *tti_description (DEVICE *dptr)
{
return "console terminal input";
}

/* Terminal output service (output character) */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;
int32 cpu = uptr->u3;

if (cpu == 0) {
    if ((tto_buf & TXDB_SEL) == 0) {                        /* for console? */
        c = sim_tt_outcvt (tto_buf, TT_GET_MODE (uptr->flags));
        if (c >= 0) {
            if ((r = sim_putchar_s (c)) != SCPE_OK) {       /* output; error? */
                sim_activate (uptr, uptr->wait);            /* retry */
                return ((r == SCPE_STALL)? SCPE_OK: r);     /* !stall? report */
                }
            }
        uptr->pos = uptr->pos + 1;
        }
    }
tto_csr[cpu] = tto_csr[cpu] | CSR_DONE;
if (tto_csr[cpu] & CSR_IE)
    tto_int |= (1u << cpu);
return SCPE_OK;
}

/* Terminal output reset */

t_stat tto_reset (DEVICE *dptr)
{
int32 i;

tto_buf = 0;
tto_int = 0;
for (i = 0; i < KA_NUM; i++) {
    tto_csr[i] = CSR_DONE;
    tto_unit[i].u3 = i;
    sim_cancel (&tto_unit[i]);                          /* deactivate unit */
    }
return SCPE_OK;
}

t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Console Terminal Output (TTO)\n\n");
fprintf (st, "The terminal output (TTO) writes to the simulator console.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *tto_description (DEVICE *dptr)
{
return "console terminal output";
}

/* Programmable timer

   The architected VAX timer, which increments at 1Mhz, cannot be
   accurately simulated due to the overhead that would be required
   for 1M clock events per second.  Instead 1Mhz intervals are
   derived from the calibrated instruction execution rate.

   If the interval register is read, then its value between events
   is interpolated relative to the elapsed instruction count.
*/

int32 iccs_rd (void)
{
sim_debug_bits_hdr (TMR_DB_REG, &tmr_dev, "iccs_rd()", tmr_iccs_bits, tmr_iccs, tmr_iccs, TRUE);
return tmr_iccs & TMR_CSR_RD;
}

void iccs_wr (int32 val)
{
sim_debug_bits_hdr (TMR_DB_REG, &tmr_dev, "iccs_wr()", tmr_iccs_bits, tmr_iccs, val, TRUE);
if ((val & TMR_CSR_RUN) == 0) {                         /* clearing run? */
    if (tmr_iccs & TMR_CSR_RUN) {                       /* run 1 -> 0? */
        tmr_icr = icr_rd ();                            /* update itr */
        sim_rtcn_calb (0, TMR_CLK);                     /* stop timer */
        }
    sim_cancel (&tmr_unit);                             /* cancel timer */
    }
if (val & CSR_DONE)                                     /* Interrupt Acked? */
    sim_rtcn_tick_ack (20, TMR_CLK);                    /* Let timers know */
tmr_iccs = tmr_iccs & ~(val & TMR_CSR_W1C);             /* W1C csr */
tmr_iccs = (tmr_iccs & ~TMR_CSR_WR) |                   /* new r/w */
    (val & TMR_CSR_WR);
if (val & TMR_CSR_XFR)                                  /* xfr set? */
    tmr_icr = tmr_nicr;
if (val & TMR_CSR_RUN)  {                               /* run? */
    if (val & TMR_CSR_XFR)                              /* new tir? */
        sim_cancel (&tmr_unit);                         /* stop prev */
    if (!sim_is_active (&tmr_unit)) {                   /* not running? */
        sim_rtcn_init_unit (&tmr_unit, CLK_DELAY, TMR_CLK);  /* init timer */
        tmr_sched (tmr_icr);                            /* activate */
        }
    }
else {
    if (val & TMR_CSR_XFR)                              /* xfr set? */
        tmr_icr = tmr_nicr;
    if (val & TMR_CSR_SGL) {                            /* single step? */
        tmr_icr = tmr_icr + 1;                          /* incr tmr */
        if (tmr_icr == 0) {                             /* if ovflo, */
            if (tmr_iccs & TMR_CSR_DON)                 /* done? set err */
                tmr_iccs = tmr_iccs | TMR_CSR_ERR;
            else
                tmr_iccs = tmr_iccs | TMR_CSR_DON;      /* set done */
            if (tmr_iccs & TMR_CSR_IE) {                /* ie? */
                tmr_int = 3;                            /* set int req */
                sim_debug (TMR_DB_INT, &tmr_dev, "tmr_incr() - INT=1\n");
                }
            tmr_icr = tmr_nicr;                         /* reload tir */
            }
        }
    }
if ((tmr_iccs & (TMR_CSR_DON | TMR_CSR_IE)) !=          /* update int */
    (TMR_CSR_DON | TMR_CSR_IE)) {
    if (tmr_int) {
        tmr_int = 0;
        sim_debug (TMR_DB_INT, &tmr_dev, "iccs_wr() - INT=0\n");
        }
    }
}

int32 icr_rd (void)
{
int32 result;

if (tmr_iccs & TMR_CSR_RUN) {                           /* running? */
    uint32 usecs_remaining = (uint32)sim_activate_time_usecs (&tmr_unit);

    result = (int32)(~usecs_remaining + 1);
    }
else
    result = (int32)tmr_icr;
sim_debug (TMR_DB_REG, &tmr_dev, "icr_rd() = 0x%08X%s\n", result, (tmr_iccs & TMR_CSR_RUN) ? " - interpolated" : "");
return result;
}

int32 nicr_rd (void)
{
sim_debug (TMR_DB_REG, &tmr_dev, "nicr_rd() = 0x%08X\n", tmr_nicr);
return tmr_nicr;
}

void nicr_wr (int32 val)
{
sim_debug (TMR_DB_REG, &tmr_dev, "nicr_wr(0x%08X)\n", val);
tmr_nicr = val;
}

/* Interval timer unit service */

t_stat tmr_svc (UNIT *uptr)
{
sim_debug (TMR_DB_TICK, &tmr_dev, "tmr_svc()\n");
tmxr_poll = tmr_poll * TMXR_MULT;                   /* set mux poll */
if (tmr_iccs & TMR_CSR_DON)                         /* done? set err */
    tmr_iccs = tmr_iccs | TMR_CSR_ERR;
else
    tmr_iccs = tmr_iccs | TMR_CSR_DON;              /* set done */
if (tmr_iccs & TMR_CSR_RUN)                         /* run? */
    tmr_sched (tmr_nicr);                           /* reactivate */
if (tmr_iccs & TMR_CSR_IE) {                        /* ie? set int req */
    tmr_int = 1;
    sim_debug (TMR_DB_INT, &tmr_dev, "tmr_svc() - INT=1\n");
    }
else
    tmr_int = 0;
AIO_SET_INTERRUPT_LATENCY(tmr_poll*clk_tps);            /* set interrrupt latency */
return SCPE_OK;
}

/* Timer scheduling */

void tmr_sched (uint32 nicr)
{
uint32 usecs = (nicr) ? (~nicr + 1) : 0xFFFFFFFF;

clk_tps = (int32)((1000000.0 / usecs) + 0.5);

sim_debug (TMR_DB_SCHED, &tmr_dev, "tmr_sched(nicr=0x%08X-usecs=0x%08X) - tps=%d\n", nicr, usecs, clk_tps);
tmr_poll = sim_rtcn_calb (clk_tps, TMR_CLK);
sim_activate_after (&tmr_unit, usecs);
}

/* 100Hz TODR reset */

t_stat clk_reset (DEVICE *dptr)
{
if (clk_unit.filebuf == NULL) {                         /* make sure the TODR is initialized */
    clk_unit.filebuf = calloc(sizeof(TOY), 1);
    if (clk_unit.filebuf == NULL)
        return SCPE_MEM;
    }
todr_resync ();
if (clk_unit.flags & UNIT_ATT)              /* battery backup hooked up? */
    wtc_set_valid ();
else
    wtc_set_invalid ();
return SCPE_OK;
}

t_stat clk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Real-Time Clock (%s)\n\n", dptr->name);
fprintf (st, "The real-time clock autocalibrates; the clock interval is adjusted up or down\n");
fprintf (st, "so that the clock tracks actual elapsed time.\n\n");
fprintf (st, "There are two modes of TODR operation:\n\n");
fprintf (st, "   Default VMS mode.  Without initializing the TODR it returns the current\n");
fprintf (st, "                      time of year offset which VMS would set the clock to\n");
fprintf (st, "                      if VMS knew the correct time (i.e. by manual input).\n");
fprintf (st, "                      This is correct almost all the time unless a VMS disk\n");
fprintf (st, "                      hadn't been booted from in the current year.  This mode\n");
fprintf (st, "                      produces strange time results for non VMS OSes on each\n");
fprintf (st, "                      system boot.\n");
fprintf (st, "   OS Agnostic mode.  This mode behaves precisely like the VAX780 TODR and\n");
fprintf (st, "                      works correctly for all OSes.  This mode is enabled by\n");
fprintf (st, "                      attaching the %s to a battery backup state file for the\n", dptr->name);
fprintf (st, "                      TOY clock (i.e. sim> attach %s TOY_CLOCK).  When\n", dptr->name);
fprintf (st, "                      operating in OS Agnostic mode, the TODR will initially\n");
fprintf (st, "                      start counting from 0 and be adjusted differently when\n");
fprintf (st, "                      an OS specifically writes to the TODR.  VMS determines\n");
fprintf (st, "                      if the TODR currently contains a valid time if the value\n");
fprintf (st, "                      it sees is less than about 1 month.  If the time isn't\n");
fprintf (st, "                      valid VMS will prompt to set the time during the system\n");
fprintf (st, "                      boot.  While prompting for the time it will wait for an\n");
fprintf (st, "                      answer to the prompt for up to the SYSGEN parameter\n");
fprintf (st, "                      TIMEPROMPTWAIT seconds.  A value of 0 for TIMEPROMPTWAIT\n");
fprintf (st, "                      will disable the clock setting prompt.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *clk_description (DEVICE *dptr)
{
return "time of year clock";
}

static uint32 sim_byteswap32 (uint32 data)
{
uint8 *bdata = (uint8 *)&data;
uint8 tmp;

tmp = bdata[0];
bdata[0] = bdata[3];
bdata[3] = tmp;
tmp = bdata[1];
bdata[1] = bdata[2];
bdata[2] = tmp;
return data;
}

/* CLK attach */

t_stat clk_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);
memset (uptr->filebuf, 0, (size_t)uptr->capac);
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
else {
    TOY *toy = (TOY *)uptr->filebuf;

    wtc_set_valid ();
    wtc_set (NULL, 0, "STD", NULL);
    uptr->hwmark = (uint32) uptr->capac;
    if ((toy->toy_endian_plus2 < 2) || (toy->toy_endian_plus2 > 3))
        memset (uptr->filebuf, 0, (size_t)uptr->capac);
    else {
        if (toy->toy_endian_plus2 != sim_end + 2) {     /* wrong endian? */
            toy->toy_gmtbase = sim_byteswap32 (toy->toy_gmtbase);
            toy->toy_gmtbasemsec = sim_byteswap32 (toy->toy_gmtbasemsec);
            }
        }
    toy->toy_endian_plus2 = sim_end + 2;
    todr_resync ();
    }
return r;
}

/* CLK detach */

t_stat clk_detach (UNIT *uptr)
{
t_stat r;

r = detach_unit (uptr);
if ((uptr->flags & UNIT_ATT) == 0)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
return r;
}


/* Interval timer reset */

t_stat tmr_reset (DEVICE *dptr)
{
tmr_poll = sim_rtcn_init_unit (&tmr_unit, CLK_DELAY, TMR_CLK);  /* init timer */
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
tmr_iccs = 0;
tmr_nicr = 0;
tmr_int = 0;
sim_cancel (&tmr_unit);                                 /* cancel timer */
return SCPE_OK;
}

const char *tmr_description (DEVICE *dptr)
{
return "interval timer";
}

/* TODR routines */

static const char *todr_fmt_vms_todr (int32 val)
{
static char buf[32];
uint32 uval = (uint32)val;

if (val < 0x10000000)
    sprintf (buf, "Not VMS Time: 0x%08X", uval);
else {
    int yday, hr, min, sec, msecs;

    uval -= 0x10000000;
    msecs = (uval % 100) * 10;
    uval /= 100;
    sec = uval % 60;
    uval /= 60;
    min = uval % 60;
    uval /= 60;
    hr = uval % 24;
    uval /= 24;
    yday = uval;
    sprintf (buf, "yday:%d %02d:%02d:%02d.%03d", yday, hr, min, sec, msecs);
    }
return buf;
}

int32 todr_rd (void)
{
TOY *toy = (TOY *)clk_unit.filebuf;
struct timespec base, now, val;

sim_rtcn_get_time(&now, TMR_CLK);                       /* get curr time */
base.tv_sec = toy->toy_gmtbase;
base.tv_nsec = toy->toy_gmtbasemsec * 1000000;
sim_timespec_diff (&val, &now, &base);
sim_debug (TMR_DB_TODR, &clk_dev, "todr_rd() - TODR=0x%X - %s\n", (int32)(val.tv_sec*100 + val.tv_nsec/10000000), todr_fmt_vms_todr ((int32)(val.tv_sec*100 + val.tv_nsec/10000000)));
return (int32)(val.tv_sec*100 + val.tv_nsec/10000000);  /* 100hz Clock Ticks */
}

void todr_wr (int32 data)
{
TOY *toy = (TOY *)clk_unit.filebuf;
struct timespec now, val, base;
time_t tbase;

/* Save the GMT time when set value was 0 to record the base for
   future read operations in "battery backed-up" state */

sim_rtcn_get_time(&now, TMR_CLK);                       /* get curr time */
val.tv_sec = ((uint32)data) / 100;
val.tv_nsec = (((uint32)data) % 100) * 10000000;
sim_timespec_diff (&base, &now, &val);                  /* base = now - data */
toy->toy_gmtbase = (uint32)base.tv_sec;
tbase = (time_t)base.tv_sec;
toy->toy_gmtbasemsec = base.tv_nsec/1000000;
sim_debug (TMR_DB_TODR, &clk_dev, "todr_wr(0x%X) - %s - GMTBASE=%8.8s.%03d\n", data, todr_fmt_vms_todr (data), 11+ctime(&tbase), (int)(base.tv_nsec/1000000));
}

t_stat todr_resync (void)
{
TOY *toy = (TOY *)clk_unit.filebuf;

if (clk_unit.flags & UNIT_ATT) {                        /* Attached means behave like real VAX TODR */
    if (!toy->toy_gmtbase)                              /* Never set? */
        todr_wr (0);                                    /* Start ticking from 0 */
    }
else {                                                  /* Not-Attached means */
    uint32 base;                                        /* behave like simh VMS default */
    time_t curr;
    struct tm *ctm;
    struct timespec now;

    sim_rtcn_get_time(&now, TMR_CLK);                   /* get curr time */
    curr = (time_t)now.tv_sec;
    if (curr == (time_t) -1)                            /* error? */
        return SCPE_NOFNC;
    ctm = localtime (&curr);                            /* decompose */
    if (ctm == NULL)                                    /* error? */
        return SCPE_NOFNC;
    base = (((((ctm->tm_yday * 24) +                    /* sec since 1-Jan */
            ctm->tm_hour) * 60) +
            ctm->tm_min) * 60) +
            ctm->tm_sec;
    todr_wr ((base * 100) + 0x10000000 +                /* use VMS form */
             (int32)(now.tv_nsec / 10000000));
    }
return SCPE_OK;
}

int32 fl_rd (int32 pa)
{
int32 rg = (pa >> 1) & 0xF;
int32 val;

switch (rg) {

    case FL_CS0:
        val = fl_cs0;
        break;

    case FL_CS1:
        val = fl_cs1;
        break;

    case FL_CS2:
        val = fl_cs2;
        break;

    case FL_CS3:
        val = fl_cs3;
        break;

    case FL_CS4:
        val = fl_cs4;
        break;

    case FL_CS5:
        val = fl_cs5;
        break;

    case FL_EB:
        val = fl_buf[fl_bptr];
        if (fl_bptr < (FL_NUMBY - 1))
            fl_bptr++;
        break;

    case FL_CA:
        fl_bptr = 0;
        break;

    case FL_GO:
        if (fl_cs0 & FLCS0_DKS)                         /* disk 1? */
            sim_activate (&fl_unit[1], fl_cwait);       /* start operation */
        else                                            /* no, disk 0 */
            sim_activate (&fl_unit[0], fl_cwait);       /* start operation */
        break;

    default:
        val = 0;
        break;
        }
sim_debug (FL_DB_REG, &fl_dev, "fl_rd(%s) data=0x%02X\n", fl_regnames[rg], val);
return val;
}

void fl_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa >> 1) & 0xF;

sim_debug (FL_DB_REG, &fl_dev, "fl_wr(%s) data=0x%02X\n", fl_regnames[rg], val);
switch (rg) {

    case FL_CS0:
        fl_cs0 = (fl_cs0 & ~FLCS0_WR) | (val & FLCS0_WR);
        break;

    case FL_CS1:
        fl_cs1 = (val & FLCS1_TRK);
        break;

    case FL_CS2:
        fl_cs2 = (val & FLCS2_SECT);
        break;

    case FL_CS5:
        fl_cs5 = (val & FLCS5_FUNC);
        break;

    case FL_CA:
        fl_bptr = 0;
        break;

    case FL_GO:
        if (fl_cs0 & FLCS0_DKS)                         /* disk 1? */
            sim_activate (&fl_unit[1], fl_cwait);       /* start operation */
        else                                            /* no, disk 0 */
            sim_activate (&fl_unit[0], fl_cwait);       /* start operation */
        break;

    case FL_FB:
        fl_buf[fl_bptr] = val;
        if (fl_bptr < (FL_NUMBY - 1))
            fl_bptr++;
        break;

    default:
        break;
        }
}

void fl_maint_status (void)
{
fl_cs0 = fl_cs0 & (FLCS0_FNC | FLCS0_DS | FLCS0_DKS | FLCS0_SS);
fl_cs0 = fl_cs0 | FLCS0_DONE;
fl_cs1 = fl_ecode;
fl_cs2 = fl_track;
fl_cs3 = 0;                                             /* FIXME */
fl_cs4 = 0;                                             /* FIXME */
}

void fl_xfer_status (void)
{
fl_cs0 = fl_cs0 & (FLCS0_FNC | FLCS0_DS | FLCS0_DKS | FLCS0_SS);
fl_cs0 = fl_cs0 | FLCS0_DONE;
fl_cs1 = fl_ecode;
fl_cs2 = fl_track;
fl_cs3 = 0;                                             /* FIXME */
fl_cs4 = 0;                                             /* FIXME */
}

/* Unit service; the action to be taken depends on the transfer state:

   FL_IDLE              Should never get here
   FL_RWDS              Set TXCS<done> (driver sends sector, sets FL_RWDT)
   FL_RWDT              Set TXCS<done> (driver sends track, sets FL_READ/FL_FILL)
   FL_READ              Set TXCS<done>, schedule FL_READ1
   FL_READ1             Read sector, schedule FL_EMPTY
   FL_EMPTY             Copy data to RXDB, set RXCS<done>
                        if fl_bptr >= max, schedule completion, else continue
   FL_FILL              Set TXCS<done> (driver sends next byte, sets FL_WRITE)
   FL_WRITE             Set TXCS<done>, schedule FL_WRITE1
   FL_WRITE1            Write sector, schedule FL_DONE
   FL_DONE              Copy requested data to TXDB, set FL_IDLE
*/

t_stat fl_svc (UNIT *uptr)
{
int32 fnc = (fl_cs0 >> FLCS0_V_FNC) & FLCS0_M_FNC;
int8 *fbuf = (int8 *)uptr->filebuf;
uint32 da;
int32 i;
int32 unit = (int32)(uptr - &fl_unit[0]);

sim_debug (FL_DB_FNC, &fl_dev, "fl_svc(%d) - %s\n", unit, fl_fncnames[fnc]);
switch (fnc) {

    case FL_FNCST:                                      /* read status */
    case FL_FNCMM:                                      /* maintenance mode */
        fl_maint_status ();
        break;

    case FL_FNCRD:                                      /* restore drive */
    case FL_FNCIN:                                      /* initialise */
        fl_track = 0;
        fl_sector = 0;
        fl_maint_status ();
        break;

    case FL_FNCRS:                                      /* read sector */
        fl_track = fl_cs1 & FLCS1_TRK;
        fl_sector = fl_cs2 & FLCS2_SECT;
        if (fl_test_xfr (uptr, FALSE)) {                /* transfer ok? */
            da = CALC_DA (fl_track, fl_sector);         /* get disk address */
            for (i = 0; i < FL_NUMBY; i++)              /* copy sector to buf */
                fl_buf[i] = fbuf[da + i];
            }
        sim_debug (FL_DB_INT, &fl_dev, "fl_int");
        fl_int = 1;
        fl_xfer_status ();
        break;

    case FL_FNCEX:                                      /* extended function */
        break;

    case FL_FNCRA:                                      /* read address */
        fl_xfer_status ();                              /* FIXME: maybe? */
        break;

    case FL_FNCWS:                                      /* write sector */
        fl_track = fl_cs1 & FLCS1_TRK;
        fl_sector = fl_cs2 & FLCS2_SECT;
        if (fl_test_xfr (uptr, TRUE)) {                 /* transfer ok? */
            da = CALC_DA (fl_track, fl_sector);         /* get disk address */
            for (i = 0; i < FL_NUMBY; i++)              /* copy buf to sector */
                fbuf[da + i] = fl_buf[i];
            da = da + FL_NUMBY;
            if (da > uptr->hwmark)                      /* update hwmark */
                uptr->hwmark = da;
            }
        sim_debug (FL_DB_INT, &fl_dev, "fl_int");
        fl_int = 1;
        fl_xfer_status ();                              /* FIXME: maybe? */
        break;
        }

return SCPE_OK;
}

/* Test for data transfer okay */

t_bool fl_test_xfr (UNIT *uptr, t_bool wr)
{
if ((uptr->flags & UNIT_BUF) == 0)                      /* not buffered? */
    fl_ecode = 0x50;                                    /* selected unit not ready */
else if (fl_track >= FL_NUMTR)                          /* bad track? */
    fl_ecode = 0x20;                                    /* tried to access a track > 79 */
else if ((fl_sector == 0) || (fl_sector > FL_NUMSC))    /* bad sect? */
    fl_ecode = 0xB8;                                    /* done, error */
else if (wr && (uptr->flags & UNIT_WPRT))               /* write and locked? */
    fl_ecode = 0xB0;                                    /* done, error */
else
    return TRUE;
return FALSE;
}

/* Reset */

t_stat fl_reset (DEVICE *dptr)
{
extern int32 sys_model;

fl_ecode = 0;                                           /* clear error */
fl_sector = 0;                                          /* clear addr */
fl_track = 0;
fl_bptr = 0;
sim_cancel (&fl_unit[0]);                               /* cancel drive */
sim_cancel (&fl_unit[1]);                               /* cancel drive */
return SCPE_OK;
}

const char *fl_description (DEVICE *dptr)
{
return "console floppy";
}
