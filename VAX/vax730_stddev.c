/* vax730_stddev.c: VAX 11/730 standard I/O devices

   Copyright (c) 2010-2011, Matt Burke
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
   td           console TU58
   todr         TODR clock
   tmr          interval timer

   28-Sep-11    MP      Generalized setting TODR for all OSes.  
                        Unbound the TODR value from the 100hz clock tick 
                        interrupt.  TODR now behaves like the original 
                        battery backed-up clock and runs with the wall 
                        clock, not the simulated instruction clock.  
                        Two operational modes are available:
                        - Default VMS mode, which is similar to the previous 
                          behavior in that without initializing the TODR it 
                          would default to the value VMS would set it to if
                          VMS knew the correct time.  This would be correct
                          almost all the time unless a VMS disk hadn't been
                          booted from for more than a year.  This mode 
                          produces strange time results for non VMS OSes on 
                          each system boot.
                        - OS Agnostic mode.  This mode behaves precisely like
                          the VAX780 TODR and works correctly for all OSes.  
                          This mode is enabled by attaching the TODR to a 
                          battery backup state file for the TOY clock 
                          (i.e. sim> attach TODR TOY_CLOCK).  When operating 
                          in OS Agnostic mode, the TODR will initially start
                          counting from 0 and be adjusted differently when an
                          OS specifically writes to the TODR.  VMS will prompt
                          to set the time on each boot unless the SYSGEN 
                          parameter TIMEPROMPTWAIT is set to 0.
   29-Mar-2011  MB      First Version
*/

#include "vax_defs.h"
#include "sim_tmxr.h"

/* Terminal definitions */

#define RXCS_RD         (CSR_DONE + CSR_IE)             /* terminal input */
#define RXCS_WR         (CSR_IE)
#define RXDB_V_SEL      8                               /* unit select */
#define RXDB_M_SEL      0xF
#define  RXDB_TERM      0x0                             /* console terminal */
#define  RXDB_MISC      0xF                             /* console misc */
#define RXDB_ERR        0x8000                          /* error */
#define TXCS_RD         (CSR_DONE + CSR_IE)             /* terminal output */
#define TXCS_WR         (CSR_IE)
#define TXDB_V_SEL      8                               /* unit select */
#define TXDB_M_SEL      0xF
#define  TXDB_TERM      0x0                             /* console terminal */
#define  TXDB_MISC      0xF                             /* console misc */
#define MISC_MASK        0xFF                           /* console data mask */
#define  MISC_SWDN       0x1                            /* software done */
#define  MISC_BOOT       0x2                            /* reboot */
#define  MISC_CLWS       0x3                            /* clear warm start */
#define  MISC_CLCS       0x4                            /* clear cold start */
#define TXDB_SEL        (TXDB_M_SEL << TXDB_V_SEL)      /* non-terminal */
#define TXDB_GETSEL(x)  (((x) >> TXDB_V_SEL) & TXDB_M_SEL)
#define CSTS_BRK        0x1
#define CSTS_RD         (CSR_DONE + CSR_IE + CSTS_BRK)  /* terminal output */
#define CSTS_WR         (CSR_IE + CSTS_BRK)

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

/* TU58 definitions */

#define UNIT_V_WLK      (UNIT_V_UF)                     /* write locked */
#define UNIT_WLK        (1u << UNIT_V_UF)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

#define TD_NUMBLK       512                             /* blocks/tape */
#define TD_NUMBY        512                             /* bytes/block */
#define TD_SIZE         (TD_NUMBLK * TD_NUMBY)          /* bytes/tape */

#define TD_OPDAT        001                             /* Data */
#define TD_OPCMD        002                             /* Command */
#define TD_OPINI        004                             /* INIT */
#define TD_OPBOO        010                             /* Bootstrap */
#define TD_OPCNT        020                             /* Continue */
#define TD_OPXOF        023                             /* XOFF */

#define TD_CMDNOP       0000                            /* NOP */
#define TD_CMDINI       0001                            /* INIT */
#define TD_CMDRD        0002                            /* Read */
#define TD_CMDWR        0003                            /* Write */
#define TD_CMDPOS       0005                            /* Position */
#define TD_CMDDIA       0007                            /* Diagnose */
#define TD_CMDGST       0010                            /* Get Status */
#define TD_CMDSST       0011                            /* Set Status */
#define TD_CMDMRSP      0012                            /* MRSP Request */
#define TD_CMDEND       0100                            /* END */

#define TD_STSOK        0000                            /* Normal success */
#define TD_STSRTY       0001                            /* Success with retries */
#define TD_STSFAIL      0377                            /* Failed selftest */
#define TD_STSPO        0376                            /* Partial operation (end of medium) */
#define TD_STSBUN       0370                            /* Bad unit number */
#define TD_STSNC        0367                            /* No cartridge */
#define TD_STSWP        0365                            /* Write protected */
#define TD_STSDCE       0357                            /* Data check error */
#define TD_STSSE        0340                            /* Seek error (block not found) */
#define TD_STSMS        0337                            /* Motor stopped */
#define TD_STSBOP       0320                            /* Bad opcode */
#define TD_STSBBN       0311                            /* Bad block number (>511) */

#define TD_GETOPC       0                               /* get opcode state */
#define TD_GETLEN       1                               /* get length state */
#define TD_GETDATA      2                               /* get data state */

#define TD_IDLE         0                               /* idle state */
#define TD_READ         1                               /* read */
#define TD_READ1        2                               /* fill buffer */
#define TD_READ2        3                               /* empty buffer */
#define TD_WRITE        4                               /* write */
#define TD_WRITE1       5                               /* write */
#define TD_WRITE2       6                               /* write */
#define TD_END          7                               /* empty buffer */
#define TD_END1         8                               /* empty buffer */
#define TD_INIT         9                               /* empty buffer */

int32 tti_csr = 0;                                      /* control/status */
uint32 tti_buftime;                                     /* time input character arrived */
int32 tti_buf = 0;                                      /* buffer */
int32 tti_int = 0;                                      /* interrupt */
int32 tto_csr = 0;                                      /* control/status */
int32 tto_buf = 0;                                      /* buffer */
int32 tto_int = 0;                                      /* interrupt */

int32 csi_csr = 0;                                      /* control/status */
int32 csi_buf = 0;                                      /* buffer */
int32 csi_int = 0;                                      /* interrupt */
int32 cso_csr = 0;                                      /* control/status */
int32 cso_buf = 0;                                      /* buffer */
int32 cso_int = 0;                                      /* interrupt */
int32 cso_state = 0;                                    /* state */

int32 tmr_iccs = 0;                                     /* interval timer csr */
uint32 tmr_icr = 0;                                     /* curr interval */
uint32 tmr_nicr = 0;                                    /* next interval */
uint32 tmr_inc = 0;                                     /* timer increment */
int32 tmr_sav = 0;                                      /* timer save */
int32 tmr_int = 0;                                      /* interrupt */
int32 tmr_use_100hz = 1;                                /* use 100Hz for timer */
int32 clk_tps = 100;                                    /* ticks/second */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;                /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* pgm timer poll */
struct todr_battery_info {
    uint32 toy_gmtbase;                                 /* GMT base of set value */
    uint32 toy_gmtbasemsec;                             /* The milliseconds of the set value */
    };
typedef struct todr_battery_info TOY;

int32 td_swait = 100;                                   /* seek, per block */
int32 td_cwait = 150;                                   /* command time */
int32 td_xwait = 180;                                   /* tr set time */
int32 td_iwait = 180;                                   /* init time */
uint8 td_ibuf[TD_NUMBY] = { 0 };                        /* input buffer */
int32 td_ibptr = 0;                                     /* input buffer pointer */
int32 td_ilen = 0;                                      /* input length */
uint8 td_obuf[TD_NUMBY] = { 0 };                        /* output buffer */
int32 td_obptr = 0;                                     /* output buffer pointer */
int32 td_olen = 0;                                      /* output length */
int32 td_block = 0;                                     /* current block number */
int32 td_txsize = 0;                                    /* remaining transfer size */
int32 td_offset = 0;                                    /* offset into current transfer */
int32 td_state = TD_IDLE;
int32 td_unitno = 0;                                    /* active unit number */
int32 td_ecode = 0;                                     /* end packet success code */

extern jmp_buf save_env;

t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat clk_svc (UNIT *uptr);
t_stat tmr_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat clk_reset (DEVICE *dptr);
char *tti_description (DEVICE *dptr);
char *tto_description (DEVICE *dptr);
char *clk_description (DEVICE *dptr);
char *tmr_description (DEVICE *dptr);
char *td_description (DEVICE *dptr);
t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat clk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat clk_attach (UNIT *uptr, char *cptr);
t_stat clk_detach (UNIT *uptr);
t_stat tmr_reset (DEVICE *dptr);
t_stat td_svc (UNIT *uptr);
t_stat td_reset (DEVICE *dptr);
int32 icr_rd (t_bool interp);
void tmr_incr (uint32 inc);
void tmr_sched (void);
t_stat todr_resync (void);
t_stat txdb_misc_wr (int32 data);
void td_process_packet();
t_bool td_test_xfr (UNIT *uptr, int32 state);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, UNIT_IDLE|TT_MODE_8B, 0), SERIAL_IN_WAIT };

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
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "Set 7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "Set 8 bit mode" },
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

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_8B, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { HRDATAD (TXDB,       tto_buf,         16, "last data item processed") },
    { HRDATAD (TXCS,       tto_csr,         16, "control/status register") },
    { FLDATAD (INT,        tto_int,          0, "interrupt pending flag") },
    { FLDATAD (DONE,       tto_csr, CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,         tto_csr,   CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (POS,   tto_unit.pos,   T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME, tto_unit.wait,         24, "time from I/O initiation to interrupt"), PV_LEFT + REG_NZ },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL, NULL, NULL, "Set 7 bit mode" },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL, NULL, NULL, "Set 8 bit mode" },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL, NULL, NULL, "Set 7 bit mode (suppress non printing)" },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 16, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &tto_help, NULL, NULL, 
    &tto_description
    };

/* TODR and TMR data structures */

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE+UNIT_FIX, sizeof(TOY)), CLK_DELAY };/* 100Hz */

REG clk_reg[] = {
    { DRDATAD (TIME,                   clk_unit.wait,  24, "initial poll interval"), REG_NZ + PV_LEFT },
    { DRDATAD (POLL,                        tmr_poll,  24, "calibrated poll interval"), REG_NZ + PV_LEFT + REG_HRO },
    { DRDATAD (TPS,                          clk_tps,   8, "ticks per second (100)"), REG_NZ + PV_LEFT },
#if defined (SIM_ASYNCH_IO)
    { DRDATAD (ASYNCH,            sim_asynch_enabled,   1, "asynch I/O enabled flag"), PV_LEFT },
    { DRDATAD (LATENCY,           sim_asynch_latency,  32, "desired asynch interrupt latency"), PV_LEFT },
    { DRDATAD (INST_LATENCY, sim_asynch_inst_latency,  32, "calibrated instruction latency"), PV_LEFT },
#endif
    { NULL }
    };

DEVICE clk_dev = {
    "TODR", &clk_unit, clk_reg, NULL,
    1, 0, 8, 4, 0, 32,
    NULL, NULL, &clk_reset,
    NULL, &clk_attach, &clk_detach,
    NULL, 0, 0, NULL, NULL, NULL, &clk_help, NULL, NULL, 
    &clk_description
    };

UNIT tmr_unit = { UDATA (&tmr_svc, 0, 0) };                     /* timer */

REG tmr_reg[] = {
    { HRDATAD (ICCS,          tmr_iccs, 32, "interval timer control and status") },
    { HRDATAD (ICR,            tmr_icr, 32, "interval count register") },
    { HRDATAD (NICR,          tmr_nicr, 32, "next interval count register") },
    { FLDATAD (INT,            tmr_int,  0, "interrupt request") },
    { HRDATA  (INCR,           tmr_inc, 32), REG_HIDDEN },
    { HRDATA  (SAVE,           tmr_sav, 32), REG_HIDDEN },
    { FLDATA  (USE100HZ, tmr_use_100hz,  0), REG_HIDDEN },
    { NULL }
    };

DEVICE tmr_dev = {
    "TMR", &tmr_unit, tmr_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &tmr_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &tmr_description
    };

/* TU58 data structures

   td_dev       RX device descriptor
   td_unit      RX unit list
   td_reg       RX register list
   td_mod       RX modifier list
*/

UNIT td_unit[] = {
    { UDATA (&td_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, TD_SIZE) },
    { UDATA (&td_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, TD_SIZE) }
    };

REG td_reg[] = {
    { HRDATAD (ECODE,  td_ecode,  8, "end packet success code") },
    { HRDATAD (BLK,    td_block,  8, "current block number") },
    { DRDATAD (PSTATE, td_state,  4, "state"), REG_RO },
    { DRDATAD (BPTR,   td_obptr,  7, "output buffer pointer")  },
    { DRDATAD (CTIME,  td_cwait, 24, "command time"), PV_LEFT },
    { DRDATAD (STIME,  td_swait, 24, "seek, per block"), PV_LEFT },
    { DRDATAD (XTIME,  td_xwait, 24, "tr set time"), PV_LEFT },
    { NULL }
    };

MTAB td_mod[] = {
    { UNIT_WLK,         0, "write enabled",  "WRITEENABLED", NULL, NULL, NULL, "Write enable TU58 drive" },
    { UNIT_WLK,  UNIT_WLK, "write locked",   "LOCKED", NULL, NULL, NULL, "Write lock TU58 drive"  },
    { 0 }
    };

DEVICE td_dev = {
    "TD", td_unit, td_reg, td_mod,
    2, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &td_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &td_description
    };

/* Console storage MxPR routines

   csrs_rd/wr   input control/status
   csrd_rd      input buffer
   csts_rd/wr   output control/status
   cstd_wr      output buffer
*/

int32 csrs_rd (void)
{
return (csi_csr & RXCS_RD);
}

void csrs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    cso_int = 0;
else if ((csi_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    csi_int = 1;
csi_csr = (csi_csr & ~RXCS_WR) | (data & RXCS_WR);
return;
}

int32 csrd_rd (void)
{
int32 t = csi_buf;                                      /* char + error */

csi_csr = csi_csr & ~CSR_DONE;                          /* clr done */
csi_buf = csi_buf & BMASK;                              /* clr errors */
csi_int = 0;
return t;
}

int32 csts_rd (void)
{
return (cso_csr & TXCS_RD);
}

void csts_wr (int32 data)
{
if ((cso_csr & CSTS_BRK) && !(data & CSTS_BRK)) {
    td_ibptr = 0;
    td_ibuf[td_ibptr++] = TD_OPINI;
    td_process_packet();                                /* check packet */
    }
if ((data & CSR_IE) == 0)
    cso_int = 0;
else if ((cso_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    cso_int = 1;
cso_csr = (cso_csr & ~CSTS_WR) | (data & CSTS_WR);
return;
}

void cstd_wr (int32 data)
{
cso_buf = data & WMASK;                                 /* save data */
cso_csr = cso_csr & ~CSR_DONE;                          /* clear flag */
cso_int = 0;                                            /* clear int */

switch (cso_state) {

    case TD_GETOPC:
        td_ibptr = 0;
        td_ibuf[td_ibptr++] = cso_buf;
        td_process_packet();                            /* check packet */
        break;

    case TD_GETLEN:
        td_ibuf[td_ibptr++] = cso_buf;
        td_ilen = cso_buf + 4;                          /* packet length + header + checksum */
        cso_state = TD_GETDATA;
        break;

    case TD_GETDATA:
        td_ibuf[td_ibptr++] = cso_buf;
        if (td_ibptr >= td_ilen) {
            cso_state = TD_GETOPC;
            td_process_packet();
            }
        break;
    }

cso_csr = cso_csr | CSR_DONE;                           /* set input flag */
if (cso_csr & CSR_IE)
    cso_int = 1;
return;
}

void td_process_packet()
{
int32 opcode = td_ibuf[0];

switch (opcode) {

    case TD_OPDAT:
        if (td_state != TD_WRITE1) {                     /* expecting data? */
            printf("TU58 protocol error 1\n");
            return;
            }
        if (td_ibptr < 2) {                             /* whole packet read? */
            cso_state = TD_GETLEN;                      /* get rest of packet */
            return;
            }
        td_state = TD_WRITE2;
        sim_activate (&td_dev.units[td_unitno], td_cwait);  /* sched command */
        break;

    case TD_OPCMD:
        if (td_state != TD_IDLE) {                      /* expecting command? */
            printf("TU58 protocol error 2\n");
            return;
            }
        if (td_ibptr < 2) {                             /* whole packet read? */
            cso_state = TD_GETLEN;                      /* get rest of packet */
            return;
            }
        switch (td_ibuf[2]) {
            case TD_CMDNOP:                             /* NOP */
            case TD_CMDGST:                             /* Get status */
            case TD_CMDSST:                             /* Set status */
                td_unitno = td_ibuf[4];
                td_state = TD_END;                      /* All treated as NOP */
                td_ecode = TD_STSOK;
                td_offset = 0;
                sim_activate (&td_dev.units[td_unitno], td_cwait);  /* sched command */
                break;
               
            case TD_CMDINI:
                printf("Warning: TU58 command 'INIT' not implemented\n");
                break;
               
            case TD_CMDRD:
                td_unitno = td_ibuf[4];
                td_block = ((td_ibuf[11] << 8) | td_ibuf[10]);
                td_txsize = ((td_ibuf[9] << 8) | td_ibuf[8]);
                td_state = TD_READ;
                td_offset = 0;
                sim_activate (&td_dev.units[td_unitno], td_cwait);  /* sched command */
                break;
               
            case TD_CMDWR:
                td_unitno = td_ibuf[4];
                td_block = ((td_ibuf[11] << 8) | td_ibuf[10]);
                td_txsize = ((td_ibuf[9] << 8) | td_ibuf[8]);
                td_state = TD_WRITE;
                td_offset = 0;
                sim_activate (&td_dev.units[td_unitno], td_cwait);  /* sched command */
                break;
               
            case TD_CMDPOS:
                printf("Warning: TU58 command 'Position' not implemented\n");
                break;
               
            case TD_CMDDIA:
                printf("Warning: TU58 command 'Diagnose' not implemented\n");
                break;
               
            case TD_CMDMRSP:                            /* MRSP supported? */
                csi_buf = TD_OPDAT;                     /* TP_OPCMD = yes, TP_OPDAT = no */
                csi_csr = csi_csr | CSR_DONE;           /* set input flag */
                if (csi_csr & CSR_IE)
                    csi_int = 1;
                break;
            }
        break;

    case TD_OPINI:
        sim_cancel (&td_dev.units[0]);
        sim_cancel (&td_dev.units[1]);
        td_ibptr = 0;
        td_obptr = 0;
        td_olen = 0;
        td_offset = 0;
        td_txsize = 0;
        cso_state = TD_GETOPC;
        td_state = TD_INIT;
        sim_activate (&td_dev.units[0], td_iwait);      /* sched command */
        break;

    case TD_OPBOO:
        if (td_state != TD_IDLE) {
            printf("TU58 protocol error 3\n");
            return;
            }
        if (td_ibptr < 2) {                             /* whole packet read? */
            td_ilen = 2;
            cso_state = TD_GETDATA;                     /* get rest of packet */
            return;
            }
        td_unitno = td_ibuf[1];
        td_block = 0;
        td_txsize = 512;
        td_state = TD_READ;
        td_offset = 0;
        sim_activate (&td_dev.units[td_unitno], td_cwait);  /* sched command */
        break;

    case TD_OPCNT:
        break;

    default:
        //printf("TU58: Unknown opcode %d\n", opcode);
        break;
    }
}

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
else if ((tti_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    tti_int = 1;
tti_csr = (tti_csr & ~RXCS_WR) | (data & RXCS_WR);
return;
}

int32 rxdb_rd (void)
{
int32 t = tti_buf;                                      /* char + error */

if (tti_csr & CSR_DONE) {                               /* Input pending ? */
    tti_csr = tti_csr & ~CSR_DONE;                      /* clr done */
    tti_buf = tti_buf & BMASK;                          /* clr errors */
    tti_int = 0;
    sim_activate_abs (&tti_unit, tti_unit.wait);        /* check soon for more input */
    }
return t;
}

int32 txcs_rd (void)
{
return (tto_csr & TXCS_RD);
}

void txcs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tto_int = 0;
else if ((tto_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
    tto_int = 1;
tto_csr = (tto_csr & ~TXCS_WR) | (data & TXCS_WR);
return;
}

void txdb_wr (int32 data)
{
tto_buf = data & WMASK;                                 /* save data */
tto_csr = tto_csr & ~CSR_DONE;                          /* clear flag */
tto_int = 0;                                            /* clear int */
if (tto_buf & TXDB_SEL)                                 /* console mailbox? */
    txdb_misc_wr (tto_buf);
sim_activate (&tto_unit, tto_unit.wait);                /* no, console */
return;
}

/* Terminal input service (poll for character) */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_clock_coschedule (uptr, KBD_WAIT (uptr->wait, tmr_poll));
                                                        /* continue poll */
if ((tti_csr & CSR_DONE) &&                             /* input still pending and < 500ms? */
    ((sim_os_msec () - tti_buftime) < 500))
     return SCPE_OK;
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    tti_buf = RXDB_ERR;
else tti_buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
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
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_buf = 0;
tti_csr = 0;
tti_int = 0;
sim_activate_abs (&tti_unit, KBD_WAIT (tti_unit.wait, tmr_poll));
return SCPE_OK;
}

t_stat tti_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Console Terminal Input (TTI)\n\n");
fprintf (st, "The terminal input (TTI) polls the console keyboard for input.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *tti_description (DEVICE *dptr)
{
return "console terminal input";
}
/* Terminal output service (output character) */

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

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
tto_csr = tto_csr | CSR_DONE;
if (tto_csr & CSR_IE)
    tto_int = 1;
return SCPE_OK;
}

/* Terminal output reset */

t_stat tto_reset (DEVICE *dptr)
{
tto_buf = 0;
tto_csr = CSR_DONE;
tto_int = 0;
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat tto_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
fprintf (st, "Console Terminal Output (TTO)\n\n");
fprintf (st, "The terminal output (TTO) writes to the simulator console.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

char *tto_description (DEVICE *dptr)
{
return "console terminal output";
}

/* Programmable timer

   The architected VAX timer, which increments at 1Mhz, cannot be
   accurately simulated due to the overhead that would be required
   for 1M clock events per second.  Instead, a hidden calibrated
   100Hz timer is run (because that's what VMS expects), and a
   hack is used for the interval timer.

   When the timer is started, the timer interval is inspected.

   if the interval is >= 10msec, then the 100Hz timer drives the
        next interval
   if the interval is < 10mec, then count instructions

   If the interval register is read, then its value between events
   is interpolated using the current instruction count versus the
   count when the most recent event started, the result is scaled
   to the calibrated system clock, unless the interval being timed
   is less than a calibrated system clock tick (or the calibrated 
   clock is running very slowly) at which time the result will be 
   the elapsed instruction count.
*/

int32 iccs_rd (void)
{
return tmr_iccs & TMR_CSR_RD;
}

void iccs_wr (int32 val)
{
if ((val & TMR_CSR_RUN) == 0) {                         /* clearing run? */
    sim_cancel (&tmr_unit);                             /* cancel timer */
    tmr_use_100hz = 0;
    if (tmr_iccs & TMR_CSR_RUN)                         /* run 1 -> 0? */
        tmr_icr = icr_rd (TRUE);                        /* update itr */
    }
tmr_iccs = tmr_iccs & ~(val & TMR_CSR_W1C);             /* W1C csr */
tmr_iccs = (tmr_iccs & ~TMR_CSR_WR) |                   /* new r/w */
    (val & TMR_CSR_WR);
if (val & TMR_CSR_XFR) tmr_icr = tmr_nicr;              /* xfr set? */
if (val & TMR_CSR_RUN)  {                               /* run? */
    if (val & TMR_CSR_XFR)                              /* new tir? */
        sim_cancel (&tmr_unit);                         /* stop prev */
    if (!sim_is_active (&tmr_unit))                     /* not running? */
        tmr_sched ();                                   /* activate */
    }
else if (val & TMR_CSR_SGL) {                           /* single step? */
    tmr_incr (1);                                       /* incr tmr */
    if (tmr_icr == 0)                                   /* if ovflo, */
        tmr_icr = tmr_nicr;                             /* reload tir */
    }
if ((tmr_iccs & (TMR_CSR_DON | TMR_CSR_IE)) !=          /* update int */
    (TMR_CSR_DON | TMR_CSR_IE))
    tmr_int = 0;
return;
}

int32 icr_rd (t_bool interp)
{
uint32 delta;

if (interp || (tmr_iccs & TMR_CSR_RUN)) {               /* interp, running? */
    delta = sim_grtime () - tmr_sav;                    /* delta inst */
    if (tmr_use_100hz && (tmr_poll > TMR_INC))          /* scale large int */
        delta = (uint32) ((((double) delta) * TMR_INC) / tmr_poll);
    if (delta >= tmr_inc)
        delta = tmr_inc - 1;
    return tmr_icr + delta;
    }
return tmr_icr;
}

int32 nicr_rd (void)
{
return tmr_nicr;
}

void nicr_wr (int32 val)
{
tmr_nicr = val;
}

/* 100Hz base clock unit service */

t_stat clk_svc (UNIT *uptr)
{
tmr_poll = sim_rtcn_calb (clk_tps, TMR_CLK);            /* calibrate clock */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
AIO_SET_INTERRUPT_LATENCY(tmr_poll*clk_tps);            /* set interrrupt latency */
if ((tmr_iccs & TMR_CSR_RUN) && tmr_use_100hz)          /* timer on, std intvl? */
    tmr_incr (TMR_INC);                                 /* do timer service */
return SCPE_OK;
}

/* Interval timer unit service */

t_stat tmr_svc (UNIT *uptr)
{
tmr_incr (tmr_inc);                                     /* incr timer */
return SCPE_OK;
}

/* Timer increment */

void tmr_incr (uint32 inc)
{
uint32 new_icr = (tmr_icr + inc) & LMASK;               /* add incr */

if (new_icr < tmr_icr) {                                /* ovflo? */
    tmr_icr = 0;                                        /* now 0 */
    if (tmr_iccs & TMR_CSR_DON)                         /* done? set err */
        tmr_iccs = tmr_iccs | TMR_CSR_ERR;
    else tmr_iccs = tmr_iccs | TMR_CSR_DON;             /* set done */
    if (tmr_iccs & TMR_CSR_RUN) {                       /* run? */
        tmr_icr = tmr_nicr;                             /* reload */
        tmr_sched ();                                   /* reactivate */
        }
    if (tmr_iccs & TMR_CSR_IE)                          /* ie? set int req */
        tmr_int = 1;
    else tmr_int = 0;
    }
else {
    tmr_icr = new_icr;                                  /* no, update icr */
    if (tmr_iccs & TMR_CSR_RUN)                         /* still running? */
        tmr_sched ();                                   /* reactivate */
    }
return;
}

/* Timer scheduling */

void tmr_sched (void)
{
tmr_sav = sim_grtime ();                                /* save intvl base */
tmr_inc = (~tmr_icr + 1);                               /* inc = interval */
if (tmr_inc == 0) tmr_inc = 1;
if (tmr_inc < TMR_INC) {                                /* 100Hz multiple? */
    sim_activate (&tmr_unit, tmr_inc);                  /* schedule timer */
    tmr_use_100hz = 0;
    }
else tmr_use_100hz = 1;                                 /* let clk handle */
return;
}

/* 100Hz clock reset */

t_stat clk_reset (DEVICE *dptr)
{
sim_register_clock_unit (&clk_unit);                    /* declare clock unit */
tmr_poll = sim_rtcn_init (clk_unit.wait, TMR_CLK);      /* init 100Hz timer */
sim_activate_abs (&clk_unit, tmr_poll);                 /* activate 100Hz unit */
tmxr_poll = tmr_poll * TMXR_MULT;                       /* set mux poll */
if (clk_unit.filebuf == NULL) {                         /* make sure the TODR is initialized */
    clk_unit.filebuf = calloc(sizeof(TOY), 1);
    if (clk_unit.filebuf == NULL)
        return SCPE_MEM;
    todr_resync ();
    }
return SCPE_OK;
}

t_stat clk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
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

char *clk_description (DEVICE *dptr)
{
return "time of year clock";
}

/* CLK attach */

t_stat clk_attach (UNIT *uptr, char *cptr)
{
t_stat r;

uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);
memset (uptr->filebuf, 0, (size_t)uptr->capac);
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
else
    uptr->hwmark = (uint32) uptr->capac;
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
tmr_icr = 0;
tmr_nicr = 0;
tmr_int = 0;
tmr_use_100hz = 1;
sim_cancel (&tmr_unit);                                 /* cancel timer */
todr_resync ();                                         /* resync TODR */
return SCPE_OK;
}

char *tmr_description (DEVICE *dptr)
{
return "interval timer";
}

/* TODR routines */

int32 todr_rd (void)
{
TOY *toy = (TOY *)clk_unit.filebuf;
struct timespec base, now, val;

clock_gettime(CLOCK_REALTIME, &now);                    /* get curr time */
base.tv_sec = toy->toy_gmtbase;
base.tv_nsec = toy->toy_gmtbasemsec * 1000000;
sim_timespec_diff (&val, &now, &base);
return (int32)(val.tv_sec*100 + val.tv_nsec/10000000);  /* 100hz Clock Ticks */
}

void todr_wr (int32 data)
{
TOY *toy = (TOY *)clk_unit.filebuf;
struct timespec now, val, base;

/* Save the GMT time when set value was 0 to record the base for future 
   read operations in "battery backed-up" state */

if (-1 == clock_gettime(CLOCK_REALTIME, &now))          /* get curr time */
    return;                                             /* error? */
val.tv_sec = ((uint32)data) / 100;
val.tv_nsec = (((uint32)data) % 100) * 10000000;
sim_timespec_diff (&base, &now, &val);                  /* base = now - data */
toy->toy_gmtbase = (uint32)base.tv_sec;
toy->toy_gmtbasemsec = base.tv_nsec/1000000;
}

t_stat todr_resync (void)
{
TOY *toy = (TOY *)clk_unit.filebuf;

if (clk_unit.flags & UNIT_ATT) {                        /* Attached means behave like real VAX780 */
    if (!toy->toy_gmtbase)                              /* Never set? */
        todr_wr (0);                                    /* Start ticking from 0 */
    }
else {                                                  /* Not-Attached means */
    uint32 base;                                        /* behave like simh VMS default */
    time_t curr;
    struct tm *ctm;

    curr = time (NULL);                                 /* get curr time */
    if (curr == (time_t) -1)                            /* error? */
        return SCPE_NOFNC;
    ctm = localtime (&curr);                            /* decompose */
    if (ctm == NULL)                                    /* error? */
        return SCPE_NOFNC;
    base = (((((ctm->tm_yday * 24) +                    /* sec since 1-Jan */
            ctm->tm_hour) * 60) +
            ctm->tm_min) * 60) +
            ctm->tm_sec;
    todr_wr ((base * 100) + 0x10000000);                /* use VMS form */
    }
return SCPE_OK;
}

/* Console write, txdb<11:8> != 0 (console unit) */

t_stat txdb_misc_wr (int32 data)
{
int32 sel = TXDB_GETSEL (data);                         /* get selection */

if (sel == TXDB_MISC) {                                 /* misc function? */
    switch (data & MISC_MASK) {                         /* case on function */

    case MISC_CLWS:
    case MISC_CLCS:
        break;

    case MISC_SWDN:
        ABORT (STOP_SWDN);
        break;
    
    case MISC_BOOT:
        ABORT (STOP_BOOT);
        break;
        }
    }
return SCPE_OK;
}

t_stat td_svc (UNIT *uptr)
{
int32 i, t, data_size;
uint16 c, w;
uint32 da;
int8 *fbuf = uptr->filebuf;

switch (td_state) {                                     /* case on state */

    case TD_IDLE:                                       /* idle */
        return SCPE_IERR;                               /* done */

    case TD_READ: case TD_WRITE:                        /* read, write */
        if (td_test_xfr (uptr, td_state)) {             /* transfer ok? */
            t = abs (td_block - 0);                     /* # blocks to seek */
            if (t == 0)                                 /* minimum 1 */
                t = 1;
            td_state++;                                 /* set next state */
            sim_activate (uptr, td_swait * t);          /* schedule seek */
            break;
            }
        else td_state = TD_END;
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_READ1:                                      /* build data packet */
//        da = CALC_DA (td_block);                      /* get tape address */
        da = (td_block * 512) + td_offset;              /* get tape address */
        if (td_txsize > 128)                            /* Packet length */
            data_size = 128;
        else data_size = td_txsize;
        td_txsize = td_txsize - data_size;
        td_offset = td_offset + data_size;
        
        td_obptr = 0;
        td_obuf[td_obptr++] = TD_OPDAT;                 /* Data packet */
        td_obuf[td_obptr++] = data_size;                /* Data length */
        for (i = 0; i < data_size; i++)                 /* copy sector to buf */
            td_obuf[td_obptr++] = fbuf[da + i];
        c = 0;
        for (i = 0; i < (data_size + 2); i++) {         /* Calculate checksum */
            w = (td_obuf[i] << ((i & 0x1) ? 8 : 0));
            c = c + w + ( (uint32)((uint32)c + (uint32)w) > 0xFFFF ? 1 : 0);
            }
        td_obuf[td_obptr++] = (c & 0xFF);               /* Checksum L */
        td_obuf[td_obptr++] = ((c >> 8) & 0xFF);        /* Checksum H */
        td_olen = td_obptr;
        td_obptr = 0;
        td_state = TD_READ2;                            /* go empty */
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_READ2:                                      /* send data packet to host */
        if ((csi_csr & CSR_DONE) == 0) {                /* prev data taken? */
            csi_buf = td_obuf[td_obptr++];              /* get next byte */
            csi_csr = csi_csr | CSR_DONE;               /* set input flag */
            if (csi_csr & CSR_IE)
                csi_int = 1;
            if (td_obptr >= td_olen) {                  /* buffer empty? */
                if (td_txsize > 0)
                    td_state = TD_READ1;
                else
                    td_state = TD_END;
                }
            }
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_WRITE1:                                     /* send continue */
        if ((csi_csr & CSR_DONE) == 0) {                /* prev data taken? */
            csi_buf = TD_OPCNT;
            csi_csr = csi_csr | CSR_DONE;               /* set input flag */
            if (csi_csr & CSR_IE)
                csi_int = 1;
            break;
            }
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_WRITE2:                                     /* write data to buffer */
        da = (td_block * 512) + td_offset;              /* get tape address */
        td_olen = td_ibuf[1];
        for (i = 0; i < td_olen; i++)                   /* write data to buffer */
            fbuf[da + i] = td_ibuf[i + 2];
        td_offset += td_olen;
        td_txsize -= td_olen;
        da = da + td_olen;
        if (da > uptr->hwmark)                          /* update hwmark */
            uptr->hwmark = da;
        if (td_txsize > 0)
            td_state = TD_WRITE1;
        else {                                          /* check whole number of blocks written */
            if ((td_olen = (512 - (td_offset % 512)) != 512)) {
                for (i = 0; i < td_olen; i++)
                    fbuf[da + i] = 0;                   /* zero fill */
                da = da + td_olen;
                if (da > uptr->hwmark)                  /* update hwmark */
                    uptr->hwmark = da;
                }
            td_state = TD_END;
            }
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;
        
    case TD_END:                                        /* build end packet */
        td_obptr = 0;
        td_obuf[td_obptr++] = TD_OPCMD;                 /* Command packet */
        td_obuf[td_obptr++] = 0xA;                      /* ** Need definition ** */
        td_obuf[td_obptr++] = TD_CMDEND;
        td_obuf[td_obptr++] = td_ecode;                 /* Success code */
        td_obuf[td_obptr++] = td_unitno;                /* Unit number */
        td_obuf[td_obptr++] = 0;                        /* Not used */
        td_obuf[td_obptr++] = 0;                        /* Sequence L (not used) */
        td_obuf[td_obptr++] = 0;                        /* Sequence H (not used) */
        td_obuf[td_obptr++] = (td_offset & 0xFF);       /* Byte count L */
        td_obuf[td_obptr++] = ((td_offset >> 8) & 0xFF);/* Byte count H */
        td_obuf[td_obptr++] = 0;                        /* Summary status L */
        td_obuf[td_obptr++] = 0;                        /* Summary status H */
        c = 0;
        for (i = 0; i < (0xA + 2); i++) {               /* Calculate checksum */
            w = (td_obuf[i] << ((i & 0x1) ? 8 : 0));
            c = c + w + ( (uint32)((uint32)c + (uint32)w) > 0xFFFF ? 1 : 0);
            }
        td_obuf[td_obptr++] = c & 0xFF;                 /* Checksum L */
        td_obuf[td_obptr++] = (c >> 8) & 0xFF;          /* Checksum H */
        td_olen = td_obptr;
        td_obptr = 0;
        td_state = TD_END1;                             /* go empty */
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_END1:                                       /* send end packet to host */
        if ((csi_csr & CSR_DONE) == 0) {                /* prev data taken? */
            csi_buf = td_obuf[td_obptr++];              /* get next byte */
            csi_csr = csi_csr | CSR_DONE;               /* set input flag */
            if (csi_csr & CSR_IE)
                csi_int = 1;
            if (td_obptr >= td_olen) {                  /* buffer empty? */
                td_state = TD_IDLE;
                break;
                }
            }
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;

    case TD_INIT:
        if ((csi_csr & CSR_DONE) == 0) {                /* prev data taken? */
            csi_buf = TD_OPCNT;
            csi_csr = csi_csr | CSR_DONE;               /* set input flag */
            if (csi_csr & CSR_IE)
                csi_int = 1;
            td_state = TD_IDLE;
            break;
            }
        sim_activate (uptr, td_xwait);                  /* schedule next */
        break;
    }
return SCPE_OK;
}

/* Test for data transfer okay */

t_bool td_test_xfr (UNIT *uptr, int32 state)
{
if ((uptr->flags & UNIT_BUF) == 0)                      /* not buffered? */
    td_ecode = TD_STSNC;
else if (td_block >= TD_NUMBLK)                         /* bad block? */
    td_ecode = TD_STSBBN;
else if ((state == TD_WRITE) && (uptr->flags & UNIT_WPRT)) /* write and locked? */
    td_ecode = TD_STSWP;
else {
    td_ecode = TD_STSOK;
    return TRUE;
    }
return FALSE;
}

/* Reset */

t_stat td_reset (DEVICE *dptr)
{
cso_buf = 0;
cso_csr = CSR_DONE;
cso_int = 0;
cso_state = TD_GETOPC;
td_ibptr = 0;
td_obptr = 0;
td_olen = 0;
td_offset = 0;
td_txsize = 0;
sim_cancel (&td_dev.units[0]);
sim_cancel (&td_dev.units[1]);
return SCPE_OK;
}

char *td_description (DEVICE *dptr)
{
return "Console TU58 cartridge";
}
