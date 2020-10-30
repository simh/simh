/* vax860_stddev.c: VAX 8600 standard I/O devices

   Copyright (c) 2011-2012, Matt Burke
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
   cs           console RL02
   todr         TODR clock
   tmr          interval timer

   26-Dec-2012  MB      First Version
*/

#include "vax_defs.h"
#include "sim_tmxr.h"

/* Terminal definitions */

#define RXCS_V_DTR      16                              /* logical carrier */
#define RXCS_M_DTR      0xF
#define RXCS_DTR        (RXCS_M_DTR << RXCS_V_DTR)
#define RXCS_RD         (CSR_DONE + CSR_IE + RXCS_DTR)  /* terminal input */
#define RXCS_WR         (CSR_IE)
#define RXDB_V_LC       16                              /* logical carrier */
#define RXDB_V_IDC      8                               /* ID Code */
#define RXDB_M_IDC      0xF
#define RXDB_IDC        (TXCS_M_IDC << TXCS_V_IDC)
#define TXCS_V_IDC      8                               /* ID Code */
#define TXCS_M_IDC      0xF
#define TXCS_IDC        (TXCS_M_IDC << TXCS_V_IDC)
#define TXCS_WMN        0x8000                          /* Write mask now */
#define TXCS_V_TEN      16                              /* Transmitter en */
#define TXCS_M_TEN      0xF
#define TXCS_TEN        (TXCS_M_TEN << TXCS_V_TEN)
#define TXCS_RD         (CSR_DONE + CSR_IE + TXCS_TEN + TXCS_IDC + TXCS_WMN)  /* Readable bits */
#define TXCS_WR         (CSR_IE)                        /* Writeable bits */
#define ID_CT           0                               /* console terminal */
#define ID_RS           1                               /* remote services */
#define ID_EMM          2                               /* environmental monitoring module */
#define ID_LC           3                               /* logical console */
#define ID_M_CT         (1u << ID_CT)
#define ID_M_RS         (1u << ID_RS)
#define ID_M_EMM        (1u << ID_EMM)
#define ID_M_LC         (1u << ID_LC)
#define RDY             u3

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

/* Logical console definitions */

#define LC_NUMBY        128                             /* response buffer size */

#define LC_IDLE         0                               /* idle state */
#define LC_READDAT      1                               /* read data */

#define LC_V_FNC        0                               /* logical console function */
#define LC_M_FNC        0xFF
#define  LC_FNCBT       0x2                             /* boot cpu */
#define  LC_FNCCW       0x3                             /* clear warm start flag */
#define  LC_FNCCS       0x4                             /* clear cold start flag */
#define  LC_FNCMV       0x12                            /* microcode version */
#define  LC_FNCAC       0x13                            /* array configuration */
#define  LC_FNCSS       0x30                            /* snapshot file status */
#define  LC_FNCCA       0x70                            /* cancel all */
#define LC_GETFNC(x)    (((x) >> LC_V_FNC) & LC_M_FNC)

/* Console storage definitions */

#define STXCS_FNC       0xF
#define STXCS_V_DA      8
#define STXCS_M_DA      0xFFFF
#define STXCS_DA        (STXCS_M_DA << STXCS_V_DA)
#define STXCS_GETDA(x)  (((x) >> STXCS_V_DA) & STXCS_M_DA)
#define STXCS_V_STS     24
#define STXCS_M_STS     0xFFu
#define STXCS_STS       (STXCS_M_STS << STXCS_V_STS)
#define STXCS_WR        (STXCS_FNC | CSR_DONE | CSR_IE | STXCS_DA)

#define STXDB_DAT       0xFFFF

#define RL_NUMBY        256                             /* bytes/sector */
#define RL_NUMWD        128                             /* words/sector */
#define RL_NUMSC        40                              /* sectors/surface */
#define RL_NUMSF        2                               /* surfaces/cylinder */
#define RL_NUMCY        512                             /* cylinders/drive */
#define RL02_SIZE (RL_NUMCY * RL_NUMSF * RL_NUMSC * RL_NUMWD)  /* words/drive */

/* Parameters in the unit descriptor */

#define TRK             u3                              /* current track */
#define STAT            u4                              /* status */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_WLK        (1u << UNIT_V_WLK)

#define RLCS_DRDY       0000001                         /* drive ready */
#define RLCS_M_DRIVE    03
#define RLCS_V_DRIVE    8
#define RLCS_INCMP      0002000                         /* incomplete */
#define RLCS_CRC        0004000                         /* CRC error */
#define RLCS_HDE        0010000                         /* header error */
#define RLCS_NXM        0020000                         /* non-exist memory */
#define RLCS_DRE        0040000                         /* drive error */
#define RLCS_ERR        0100000                         /* error summary */
#define RLCS_ALLERR (RLCS_ERR+RLCS_DRE+RLCS_NXM+RLCS_HDE+RLCS_CRC+RLCS_INCMP)
#define RLCS_RW         0001776                         /* read/write */

/* RL Function Codes */

#define RLFC_NOP        0                               /* No Operation */
#define RLFC_CONT       2                               /* Continue Transaction */
#define RLFC_ABORT      3                               /* Abort Current Transfer */
#define RLFC_STS        4                               /* Read Device Status */
#define RLFC_WRITE      5                               /* Write Block Data */
#define RLFC_READ       6                               /* Read Block Data */

/* RL Status Codes */

#define RLST_COMP       1                               /* Transaction Complete */
#define RLST_CONT       2                               /* Continue Transaction */
#define RLST_ABORT      3                               /* Transaction Aborted */
#define RLST_STS        4                               /* Return Device Status */
#define RLST_HERR       80                              /* Handshake Error */
#define RLST_HDERR      81                              /* Hardware Error */

/* RL States */

#define RL_IDLE         0
#define RL_READ         1
#define RL_WRITE        2
#define RL_STATUS       3
#define RL_ABORT        4

#define RL_CSR          0                               /* CSR selected */
#define RL_MP           1                               /* MP selected */

/* RLDS, NI = not implemented, * = kept in STAT, ^ = kept in TRK */

#define RLDS_LOAD       0                               /* no cartridge */
#define RLDS_LOCK       5                               /* lock on */
#define RLDS_BHO        0000010                         /* brushes home NI */
#define RLDS_HDO        0000020                         /* heads out NI */
#define RLDS_CVO        0000040                         /* cover open NI */
#define RLDS_HD         0000100                         /* head select ^ */
#define RLDS_RL02       0000200                         /* RL02 */
#define RLDS_DSE        0000400                         /* drv sel err NI */
#define RLDS_VCK        0001000                         /* vol check * */
#define RLDS_WGE        0002000                         /* wr gate err * */
#define RLDS_SPE        0004000                         /* spin err * */
#define RLDS_STO        0010000                         /* seek time out NI */
#define RLDS_WLK        0020000                         /* wr locked */
#define RLDS_HCE        0040000                         /* hd curr err NI */
#define RLDS_WDE        0100000                         /* wr data err NI */
#define RLDS_ATT        (RLDS_HDO+RLDS_BHO+RLDS_LOCK)   /* att status */
#define RLDS_UNATT      (RLDS_CVO+RLDS_LOAD)            /* unatt status */
#define RLDS_ERR        (RLDS_WDE+RLDS_HCE+RLDS_STO+RLDS_SPE+RLDS_WGE+ \
                         RLDS_VCK+RLDS_DSE)             /* errors bits */

int32 tti_csr = 0;                                      /* control/status */
uint32 tti_buftime;                                     /* time input character arrived */
int32 tti_buf = 0;                                      /* buffer */
int32 tti_int = 0;                                      /* interrupt */
int32 tto_csr = 0;                                      /* control/status */
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

int32 lc_fnc = 0;                                       /* function */
int32 lc_cwait = 50;                                    /* command time */
int32 lc_xwait = 20;                                    /* tr set time */
uint8 lc_buf[LC_NUMBY] = { 0 };                         /* response buffer */
int32 lc_bptr = 0;                                      /* buffer pointer */
int32 lc_dlen = 0;                                      /* buffer data len */

int32 csi_int = 0;                                      /* interrupt */
int32 cso_csr = 0;                                      /* control/status */
int32 cso_buf = 0;                                      /* buffer */

int32 rlcs_swait = 10;                                  /* command time */
int32 rlcs_state = RL_IDLE;                             /* protocol state */
int32 rlcs_sts_reg = RL_CSR;                            /* status register */
int32 rlcs_csr = 0;                                     /* control/status */
int32 rlcs_mp = 0;
int32 rlcs_bcnt = 0;                                    /* byte count */
uint16 *rlcs_buf = NULL;

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tmr_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat lc_svc (UNIT *uptr);
t_stat rlcs_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
const char *tti_description (DEVICE *dptr);
const char *tto_description (DEVICE *dptr);
const char *clk_description (DEVICE *dptr);
const char *tmr_description (DEVICE *dptr);
const char *rlcs_description (DEVICE *dptr);
t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat clk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat clk_attach (UNIT *uptr, CONST char *cptr);
t_stat clk_detach (UNIT *uptr);
t_stat tmr_reset (DEVICE *dptr);
t_stat rlcs_reset (DEVICE *dptr);
t_stat rlcs_attach (UNIT *uptr, CONST char *cptr);
int32 icr_rd (void);
void tmr_sched (uint32 incr);
t_stat todr_resync (void);
t_stat lc_wr_txdb (int32 data);

extern int32 con_halt (int32 code, int32 cc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

UNIT tti_unit[] = {
    { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), 0 },
    { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), 0 },
    { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), 0 },
    { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), 0 },
    };

REG tti_reg[] = {
    { HRDATAD (RXDB,          tti_buf,         16, "last data item processed") },
    { HRDATAD (RXCS,          tti_csr,         16, "control/status register") },
    { FLDATAD (INT,           tti_int,          0, "interrupt pending flag") },
    { FLDATAD (DONE,          tti_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,            tti_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { URDATAD (POS,   tti_unit[0].pos, 10, T_ADDR_W, 0, 4, PV_LEFT, "number of characters input") },
    { URDATAD (TIME, tti_unit[0].wait, 10, 24, 0, 4, PV_LEFT, "input polling interval") },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "Set 7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "Set 8 bit mode" },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", tti_unit, tti_reg, tti_mod,
    4, 10, 31, 1, 16, 8,
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
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT },
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT },
    { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT },
    };

REG tto_reg[] = {
    { URDATAD (TXDB, tto_unit[0].buf, 16, 32, 0, 4, 0, "last data item processed") },
    { HRDATAD (TXCS,          tto_csr,         16, "control/status register") },
    { FLDATAD (INT,           tto_int,          0, "interrupt pending flag") },
    { FLDATAD (DONE,          tto_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,            tto_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { URDATAD (POS,   tto_unit[0].pos, 10, T_ADDR_W, 0, 4, PV_LEFT, "number of characters output") },
    { URDATAD (TIME, tto_unit[0].wait, 10, 24, 0, 4, PV_LEFT + REG_NZ, "time from I/O initiation to interrupt") },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "Set 7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "Set 8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "Set 7 bit mode (suppress non printing)" },
    { 0 }
    };


DEVICE tto_dev = {
    "TTO", tto_unit, tto_reg, tto_mod,
    4, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &tto_help, NULL, NULL, 
    &tto_description
    };

/* TODR and TMR data structures */

UNIT clk_unit = { UDATA (&clk_svc, UNIT_FIX, sizeof(TOY))};

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

UNIT tmr_unit = { UDATA (&tmr_svc, 0, 0) };             /* timer */

REG tmr_reg[] = {
    { HRDATAD (ICCS,          tmr_iccs, 32, "interval timer control and status") },
    { HRDATAD (ICR,            tmr_icr, 32, "interval count register") },
    { HRDATAD (NICR,          tmr_nicr, 32, "next interval count register") },
    { FLDATAD (INT,            tmr_int,  0, "interrupt request") },
    { DRDATAD (TPS,            clk_tps,  8, "ticks per second"), REG_NZ + PV_LEFT },
    { HRDATA  (INCR,           tmr_inc, 32), REG_HIDDEN },
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

/* Console storage structures

   rlcs_dev       CS device descriptor
   rlcs_unit      CS unit list
   rlcs_reg       CS register list
   rlcs_mod       CS modifier list
*/

UNIT rlcs_unit = { UDATA (&rlcs_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, RL02_SIZE) };

REG rlcs_reg[] = {
    { HRDATAD (CSR,     rlcs_csr, 16, "control/status register") },
    { HRDATAD (MP,       rlcs_mp, 16, "") },
    { DRDATAD (BCNT,   rlcs_bcnt,  7, "byte count register")  },
    { DRDATAD (STIME, rlcs_swait, 24, "command time"), PV_LEFT },
    { NULL }
    };

MTAB rlcs_mod[] = {
    { UNIT_WLK,         0, "write enabled",  "WRITEENABLED", NULL, NULL, NULL, "Write enable console RL02 drive" },
    { UNIT_WLK,  UNIT_WLK, "write locked",   "LOCKED", NULL, NULL, NULL, "Write lock console RL02 drive"  },
    { 0 }
    };

DEVICE rlcs_dev = {
    "CS", &rlcs_unit, rlcs_reg, rlcs_mod,
    1, 10, 24, 1, 16, 16,
    NULL, NULL, &rlcs_reset,
    NULL, &rlcs_attach, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &rlcs_description
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
int32 t = tti_buf;

if (tti_csr & CSR_DONE) {                               /* Input pending ? */
    t = t | ((ID_M_LC | ID_M_EMM | ID_M_CT) << RXDB_V_LC);/* char + DTR for hard-wired lines */
    tti_csr = tti_csr & ~CSR_DONE;                      /* clr done */
    tti_int = 0;
    }
return t;
}

void tto_update_int (void)
{
int32 id = 0;

tto_csr = tto_csr & ~TXCS_IDC;
if ((tto_csr & (ID_M_LC << TXCS_V_TEN)) &&
    (tto_unit[ID_LC].RDY))                              /* logical console enabled and ready? */
    id = ID_LC;
else if ((tto_csr & (ID_M_EMM << TXCS_V_TEN)) &&
    (tto_unit[ID_EMM].RDY))                             /* EMM enabled and ready? */
    id = ID_EMM;
else if ((tto_csr & (ID_M_RS << TXCS_V_TEN)) &&
    (tto_unit[ID_RS].RDY))                              /* remote services enabled and ready? */
    id = ID_RS;
else if ((tto_csr & (ID_M_CT << TXCS_V_TEN)) &&
    (tto_unit[ID_CT].RDY))                              /* console terminal enabled and ready? */
    id = ID_CT;
else id = 0xF;                                          /* no lines enabled */
tto_csr = tto_csr | (id << TXCS_V_IDC);
tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE)
    tto_int = 1;
}

int32 txcs_rd (void)
{
return (tto_csr & TXCS_RD);
}

void txcs_wr (int32 data)
{
tto_csr = (tto_csr & ~TXCS_WR) | (data & TXCS_WR);          /* Write new bits. */
if (data & TXCS_WMN) {                                      /* Updating enable mask? */
    tto_csr = (tto_csr & ~TXCS_TEN) | (data & TXCS_TEN);    /* Yes. Modify enable mask. */
    tto_update_int ();                                      /* This can change interrupt requests... */
    }
if ((tto_csr & CSR_IE) == 0)
    tto_int = 0;
else {
    if ((tto_csr & CSR_DONE) == CSR_DONE)
        tto_int = 1;
    }
}

void txdb_wr (int32 data)
{
int32 dest = (tto_csr >> TXCS_V_IDC) & TXCS_M_IDC;
if ((dest >= ID_CT) && (dest <= ID_LC)) {               /* valid line? */
    tto_csr = tto_csr & ~CSR_DONE;                      /* clear flag */
    tto_int = 0;                                        /* clear int */
    tto_unit[dest].buf = data & WMASK;
    tto_unit[dest].RDY = 0;
    sim_activate (&tto_unit[dest], 
                  ((dest == ID_LC) && (data == LC_FNCBT)) ? 0 : tto_unit[dest].wait);/* activate unit */
    }
}

int32 stxcs_rd (void)
{
return cso_csr;
}

void stxcs_wr (int32 data)
{
int32 fnc = data & STXCS_FNC;
cso_csr = (cso_csr & ~STXCS_WR) | (data & STXCS_WR);
cso_csr = cso_csr & ~STXCS_STS;

switch (fnc) {
    case RLFC_NOP:                                      /* no operation */
        break;

    case RLFC_CONT:                                     /* request status with reset */
        rlcs_bcnt = 0;                                  /* clear byte counter */
                                                        /* fall through */
    case RLFC_STS:                                      /* request status */
        rlcs_state = RL_STATUS;
        cso_csr = cso_csr & ~CSR_DONE;                  /* clear done */
        sim_activate (&rlcs_unit, rlcs_swait);
        break;

    case RLFC_ABORT:                                    /* abort transfer */
        rlcs_state = RL_ABORT;
        cso_csr = cso_csr & ~CSR_DONE;                  /* clear done */
        sim_activate (&rlcs_unit, rlcs_swait);
        break;

    case RLFC_WRITE:                                    /* write block */
        rlcs_state = RL_WRITE;
        cso_csr = cso_csr & ~CSR_DONE;                  /* clear done */
        sim_activate (&rlcs_unit, rlcs_swait);
        break;

    case RLFC_READ:                                     /* read block */
        rlcs_state = RL_READ;
        cso_csr = cso_csr & ~CSR_DONE;                  /* clear done */
        sim_activate (&rlcs_unit, rlcs_swait);
        break;

    default:
        sim_printf ("CS: Unknown Command: %d\n", fnc);
    }
}

int32 stxdb_rd (void)
{
return cso_buf & STXDB_DAT;
}

void stxdb_wr (int32 data)
{
cso_buf = data & STXDB_DAT;

if (rlcs_state == RL_WRITE) {
    rlcs_buf[rlcs_bcnt] = cso_buf;
    rlcs_bcnt++;
    }
}

/* Terminal input service (poll for character) */

t_stat tti_svc (UNIT *uptr)
{
int32 c;
int32 line = uptr - tti_dev.units;

switch (line) {

    case ID_CT:                                         /* console terminal */
        sim_clock_coschedule (uptr, tmxr_poll);         /* continue poll */
        if ((tti_csr & CSR_DONE) &&                     /* input still pending and < 500ms? */
            ((sim_os_msec () - tti_buftime) < 500))
             return SCPE_OK;
        if ((c = sim_poll_kbd ()) < SCPE_KFLAG)         /* no char or error? */
            return c;
        if (c & SCPE_BREAK)                             /* break? */
            tti_buf = 0;
        else
            tti_buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
        tti_buftime = sim_os_msec ();
        break;

    case ID_LC:                                         /* logical console */
        if (lc_bptr > 0) {
            if ((tti_csr & CSR_DONE) == 0) {            /* prev data taken? */
                tti_buf = lc_buf[--lc_bptr];            /* get next byte */
                tti_buf |= (ID_LC << RXDB_V_IDC);       /* source = logical console */
                if (lc_bptr == 0)                       /* buffer empty? */
                    break;                              /* done */
                }
            sim_activate (uptr, lc_xwait);              /* schedule next */
            }
        break;
        }
uptr->pos = uptr->pos + 1;
tti_csr = tti_csr | CSR_DONE;
if (tti_csr & CSR_IE)
    tti_int = 1;
return SCPE_OK;
}

/* Terminal input reset */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (tti_unit, tto_unit);
tti_buf = 0;
tti_csr = 0;
tti_int = 0;
sim_activate (&tti_unit[ID_CT], tmr_poll);
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
int32 line = uptr - tto_dev.units;
t_stat r;

switch (line) {

    case ID_CT:                                         /* console terminal */
        c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags));
        if (c >= 0) {
            if ((r = sim_putchar_s (c)) != SCPE_OK) {   /* output; error? */
                sim_activate (uptr, uptr->wait);        /* retry */
                return ((r == SCPE_STALL)? SCPE_OK: r); /* !stall? report */
                }
            }
        break;

    case ID_LC:                                         /* logical console */
        lc_wr_txdb (uptr->buf);
        break;
        }
uptr->pos = uptr->pos + 1;
uptr->RDY = 1;
tto_update_int ();
return SCPE_OK;
}

/* Terminal output reset */

t_stat tto_reset (DEVICE *dptr)
{
tto_csr = (ID_M_CT << TXCS_V_TEN) | CSR_DONE;           /* console enabled + done */
tto_int = 0;
tto_unit[ID_CT].RDY = 1;                                /* all lines ready */
tto_unit[ID_RS].RDY = 1;
tto_unit[ID_EMM].RDY = 1;
tto_unit[ID_LC].RDY = 1;
sim_cancel (&tto_unit[ID_CT]);                          /* deactivate units */
sim_cancel (&tto_unit[ID_RS]);
sim_cancel (&tto_unit[ID_EMM]);
sim_cancel (&tto_unit[ID_LC]);
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
        tmr_icr = icr_rd ();                            /* update icr */
        sim_debug (TMR_DB_REG, &tmr_dev, "iccs_wr() - stopping clock remaining ICR=0x%08X\n", tmr_icr);
        }
    sim_cancel (&tmr_unit);                             /* cancel timer */
    }
if ((tmr_iccs & CSR_DONE) && (val & CSR_DONE) &&        /* Interrupt Acked? */
    (10000 == (tmr_nicr ? (~tmr_nicr + 1) : 0xFFFFFFFF)))/* of 10ms tick */
    sim_rtcn_tick_ack (20, TMR_CLK);                    /* Let timers know */
tmr_iccs = tmr_iccs & ~(val & TMR_CSR_W1C);             /* W1C csr */
tmr_iccs = (tmr_iccs & ~TMR_CSR_WR) |                   /* new r/w */
    (val & TMR_CSR_WR);
if (val & TMR_CSR_XFR)                                  /* xfr set? */
    tmr_icr = tmr_nicr;
if (val & TMR_CSR_RUN)  {                               /* run? */
    if (val & TMR_CSR_XFR)                              /* new tir? */
        sim_cancel (&tmr_unit);                         /* stop prev */
    if (!sim_is_active (&tmr_unit))                     /* not running? */
        tmr_sched (tmr_icr);                            /* activate */
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
                tmr_int = 1;                            /* set int req */
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
AIO_SET_INTERRUPT_LATENCY(tmr_poll*clk_tps);        /* set interrrupt latency */
return SCPE_OK;
}

/* Timer scheduling */

void tmr_sched (uint32 nicr)
{
uint32 usecs = (nicr) ? (~nicr + 1) : 0xFFFFFFFF;

sim_debug (TMR_DB_SCHED, &tmr_dev, "tmr_sched(nicr=0x%08X-usecs=0x%08X) - tps=%d\n", nicr, usecs, clk_tps);
if (usecs == 10000)
    sim_clock_coschedule_tmr (&tmr_unit, TMR_CLK, 1);
else
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
sim_activate_after (&clk_unit, 10000);
tmr_poll = sim_rtcn_init_unit (&clk_unit, CLK_DELAY, TMR_CLK);  /* init timer */
return SCPE_OK;
}

t_stat clk_svc (UNIT *uptr)
{
sim_activate_after (uptr, 10000);
tmr_poll = sim_rtcn_calb (100, TMR_CLK);
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
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
base.tv_sec = (time_t)toy->toy_gmtbase;
base.tv_nsec = toy->toy_gmtbasemsec * 1000000;
sim_timespec_diff (&val, &now, &base);                  /* val = now - base */
sim_debug (TMR_DB_TODR, &clk_dev, "todr_rd() - TODR=0x%X - %s\n", (int32)(val.tv_sec*100 + val.tv_nsec/10000000), todr_fmt_vms_todr ((int32)(val.tv_sec*100 + val.tv_nsec/10000000)));
return (int32)(val.tv_sec*100 + (val.tv_nsec + 5000000)/10000000);  /* 100hz Clock rounded Ticks */
}

void todr_wr (int32 data)
{
TOY *toy = (TOY *)clk_unit.filebuf;
struct timespec now, val, base;
time_t tbase;

/* Save the GMT time when set value was 0 to record the base for 
   future read operations in "battery backed-up" state */

sim_rtcn_get_time(&now, TMR_CLK);                       /* get curr time */
val.tv_sec = (time_t)((uint32)data) / 100;
val.tv_nsec = (((uint32)data) % 100) * 10000000;
sim_timespec_diff (&base, &now, &val);                  /* base = now - data */
toy->toy_gmtbase = (uint32)base.tv_sec;
tbase = (time_t)base.tv_sec;
toy->toy_gmtbasemsec = (base.tv_nsec + 500000)/1000000;
if (clk_unit.flags & UNIT_ATT) {                        /* OS Agnostic mode? */
    rewind (clk_unit.fileref);
    fwrite (toy, sizeof (*toy), 1, clk_unit.fileref);   /* Save sync time info */
    fflush (clk_unit.fileref);
    }
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
             (int32)((now.tv_nsec + 5000000)/ 10000000));
    }
return SCPE_OK;
}

/* Logical console write */

t_stat lc_wr_txdb (int32 data)
{
int32 i;
int32 mask = 0;

lc_fnc = LC_GETFNC (data);                              /* get function */
if (lc_bptr > 0)                                        /* cmd in prog? */
    switch (lc_fnc) {

    case LC_FNCCA:                                      /* cancel */
       sim_cancel (&tti_unit[ID_LC]);
       lc_bptr = 0;
       break;

    default:                                            /* all others */
        return SCPE_OK;
        }

else switch (lc_fnc) {                                  /* idle, case */

    case LC_FNCBT:                                      /* boot cpu */
        con_halt (0, 0);                                /* set up reboot */
        break;
        
    case LC_FNCCW:                                      /* clear warm start flag */
        break;
        
    case LC_FNCCS:                                      /* clear cold start flag */
        break;
        
    case LC_FNCMV:                                      /* microcode version */
        lc_buf[2] = LC_FNCMV;
        lc_buf[1] = VER_UCODE & 0xFF;                   /* low byte */
        lc_buf[0] = (VER_UCODE >> 8) & 0xFF;            /* high byte */
        lc_bptr = 3;
        sim_activate (&tti_unit[ID_LC], lc_cwait);      /* sched command */
        break;

    case LC_FNCAC:                                      /* array configuration */
        lc_buf[3] = LC_FNCAC;
        if (MEMSIZE < MAXMEMSIZE) {                     /* 4MB Boards */
            lc_buf[2] = (uint8)(MEMSIZE >> 22);         /* slots in use */
            for (i = 0; i < lc_buf[2]; i++) {
                mask |= (2 << (i * 2));                 /* build array mask */
                }
            }
        else {
            lc_buf[2] = (uint8)(MEMSIZE >> 24);         /* 16MB Boards */
            for (i = 0; i < lc_buf[2]; i++) {
                mask |= (1 << (i * 2));                 /* build array mask */
                }
            }
        lc_buf[1] = mask & 0xFF;                        /* slots 1 - 4 */
        lc_buf[0] = (mask >> 8) & 0xFF;                 /* slots 5 - 8 */
        lc_bptr = 4;
        sim_activate (&tti_unit[ID_LC], lc_cwait);      /* sched command */
        break;

    case LC_FNCSS:                                      /* snapshot file status */
        lc_buf[1] = LC_FNCSS;
        lc_buf[0] = 0x0;                                /* both invalid */
        lc_bptr = 2;
        sim_activate (&tti_unit[ID_LC], lc_cwait);      /* sched command */
        break;

    default:                                            /* all others */
        sim_printf ("TTO3: Unknown console command: %X\n", lc_fnc);
        break;
        }
return SCPE_OK;
}

/* Unit service; the action to be taken depends on the transfer state:

   RL_IDLE              Should never get here
   RL_READ              Read byte, Set STXCS<done>
   RL_WRITE             Write byte, Set STXCS<done>
   RL_ABORT             Set STXCS<done>
   RL_STATUS            Copy requested data to STXDB, Set STXCS<done>
*/

t_stat rlcs_svc (UNIT *uptr)
{
int32 bcnt;
uint32 da;

switch (rlcs_state) {

    case RL_IDLE:
        return SCPE_IERR;

    case RL_READ:
        if ((cso_csr & CSR_DONE) == 0) {                /* buf ready? */
            if (rlcs_bcnt == 0) {                       /* read in whole block */
                if ((uptr->flags & UNIT_ATT) == 0) {    /* Attached? */
                    cso_csr = cso_csr | CSR_DONE |      /* error */
                        (RLST_HDERR << STXCS_V_STS);
                    rlcs_state = RL_IDLE;               /* now idle */
                    break;
                    }
                da = STXCS_GETDA(cso_csr) * 512;        /* get byte offset */
                if (sim_fseek (uptr->fileref, da, SEEK_SET))
                    return SCPE_IOERR;
                bcnt = sim_fread (rlcs_buf, sizeof (int16), RL_NUMBY, uptr->fileref);
                for ( ; bcnt < RL_NUMBY; bcnt++)                            /* fill buffer */
                    rlcs_buf[bcnt] = 0;
                }
            if (rlcs_bcnt < RL_NUMBY) {                 /* more data in buffer? */
                cso_buf = rlcs_buf[rlcs_bcnt++];        /* return next word */
                cso_csr = cso_csr | CSR_DONE |          /* continue */
                    (RLST_CONT << STXCS_V_STS);
                }
            else {
                cso_csr = cso_csr | CSR_DONE |          /* complete */
                    (RLST_COMP << STXCS_V_STS);
                rlcs_state = RL_IDLE;                   /* now idle */
                rlcs_bcnt = 0;
                }
            if (cso_csr & CSR_IE)
                csi_int = 1;
            break;
            }
        sim_activate (uptr, rlcs_swait);                /* schedule next */
        break;

    case RL_WRITE:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* Attached? */
            cso_csr = cso_csr | CSR_DONE |              /* error */
                (RLST_HDERR << STXCS_V_STS);
            rlcs_state = RL_IDLE;                       /* now idle */
            break;
            }
        if (rlcs_bcnt < RL_NUMBY) {                     /* more data to buffer? */
            cso_csr = cso_csr | CSR_DONE |              /* continue */
                (RLST_CONT << STXCS_V_STS);
            }
        else {
            da = STXCS_GETDA(cso_csr) * 512;            /* get byte offset */
            if (sim_fseek (uptr->fileref, da, SEEK_SET))
                return SCPE_IOERR;
            bcnt = sim_fwrite (rlcs_buf, sizeof (int16), RL_NUMBY, uptr->fileref);
            if (bcnt != RL_NUMBY)
                return SCPE_IOERR;
            rlcs_state = RL_IDLE;                       /* now idle */
            rlcs_bcnt = 0;
            cso_csr = cso_csr | CSR_DONE |              /* complete */
                (RLST_COMP << STXCS_V_STS);
            }
        if (cso_csr & CSR_IE)
            csi_int = 1;
        break;

    case RL_ABORT:
        if ((cso_csr & CSR_DONE) == 0) {                /* buf ready? */
            cso_csr = cso_csr | CSR_DONE |              /* aborted */
                (RLST_ABORT << STXCS_V_STS);
            cso_buf = 0;
            rlcs_bcnt = 0;
            rlcs_state = RL_IDLE;
            if (cso_csr & CSR_IE)
                csi_int = 1;
            break;
            }
        sim_activate (uptr, rlcs_swait);                /* schedule next */
        break;

    case RL_STATUS:
        if ((cso_csr & CSR_DONE) == 0) {                /* buf ready? */
            switch (rlcs_sts_reg) {                     /* which register? */

                case RL_CSR:
                    if (rlcs_csr & RLCS_ALLERR)         /* any errors? */
                        rlcs_csr = rlcs_csr | RLCS_ERR; /* set master error bit */
                    if (rlcs_bcnt > 0)                  /* transfer in progress? */
                        rlcs_csr = rlcs_csr & ~RLCS_DRDY;
                    else
                        rlcs_csr = rlcs_csr | RLCS_DRDY;
                    cso_buf = rlcs_csr;
                    rlcs_sts_reg = RL_MP;               /* MP on next read */
                    break;

                case RL_MP:
                    if ((uptr->flags & UNIT_ATT) == 0)  /* update status */
                        rlcs_mp = RLDS_UNATT;
                    else
                        rlcs_mp = RLDS_ATT;
                    cso_buf = rlcs_mp;
                    rlcs_sts_reg = RL_CSR;              /* MP on next read */
                    break;
                }
            cso_csr = cso_csr | CSR_DONE |              /* returning status */
                (RLST_STS << STXCS_V_STS);
            rlcs_state = RL_IDLE;
            if (cso_csr & CSR_IE)
                csi_int = 1;
            break;
            }
        sim_activate (uptr, rlcs_swait);                /* schedule next */
        break;
    }
return SCPE_OK;
}

/* Reset */

t_stat rlcs_reset (DEVICE *dptr)
{
cso_buf = 0;
cso_csr = CSR_DONE;
csi_int = 0;
rlcs_state = RL_IDLE;
rlcs_csr = 0;
rlcs_sts_reg = RL_CSR;
rlcs_bcnt = 0;
if (rlcs_buf == NULL)
    rlcs_buf = (uint16 *) calloc (RL_NUMBY, sizeof (uint16));
if (rlcs_buf == NULL)
    return SCPE_MEM;
sim_cancel (&rlcs_unit);                                /* deactivate unit */
return SCPE_OK;
}

const char *rlcs_description (DEVICE *dptr)
{
return "Console RL02 disk";
}

t_stat rlcs_attach (UNIT *uptr, CONST char *cptr)
{
uint32 p;
t_stat r;

uptr->capac = RL02_SIZE;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->TRK = 0;                                          /* cylinder 0 */
uptr->STAT = RLDS_VCK;                                  /* new volume */
if ((p = sim_fsize (uptr->fileref)) == 0) {             /* new disk image? */
    if (uptr->flags & UNIT_RO)                          /* if ro, done */
        return SCPE_OK;
    return pdp11_bad_block (uptr, RL_NUMSC, RL_NUMWD);
    }
return SCPE_OK;
}
