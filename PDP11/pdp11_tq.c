/* pdp11_tq.c: TMSCP tape controller simulator

   Copyright (c) 2002-2013, Robert M Supnik

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

   tq           TQK50 tape controller

   23-Oct-13    RMS     Revised for new boot setup routine
   23-Jan-12    MP      Added missing support for Logical EOT detection while
                        positioning.
   17-Aug-11    RMS     Added CAPACITY modifier
   05-Mar-11    MP      Added missing state for proper save/restore
   01-Mar-11    MP      - Migrated complex physical tape activities to sim_tape
                        - adopted use of asynch I/O interfaces from sim_tape
                        - Added differing detailed debug output via sim_debug
   14-Jan-11    MP      Various fixes discovered while exploring Ultrix issue:
                        - Set UNIT_SXC flag when a tape mark is encountered 
                          during forward motion read operations.
                        - Fixed logic which clears UNIT_SXC to check command 
                          modifier.
                        - Added CMF_WR flag to tq_cmf entry for OP_WTM.
                        - Made Non-immediate rewind positioning operations 
                          take 2 seconds.
                        - Added UNIT_IDLE flag to tq units.
                        - Fixed debug output of tape file positions when they 
                          are 64b.  Added more debug output after positioning
                          operations.  Also, added textual display of the 
                          command being performed (GUS,POS,RD,WR,etc@)
   18-Jun-07    RMS     Added UNIT_IDLE flag to timer thread
   16-Feb-06    RMS     Revised for new magtape capacity checking
   31-Oct-05    RMS     Fixed address width for large files
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   22-Jul-05    RMS     Fixed warning from Solaris C (Doug Gwyn)
   30-Sep-04    RMS     Revised Unibus interface
   12-Jun-04    RMS     Fixed bug in reporting write protect (Lyle Bickley)
   18-Apr-04    RMS     Fixed TQK70 media ID and model byte (Robert Schaffrath)
   26-Mar-04    RMS     Fixed warnings with -std=c99
   25-Jan-04    RMS     Revised for device debug support
   19-May-03    RMS     Revised for new conditional compilation scheme
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   28-Feb-03    RMS     Added variable controller, user-defined drive support
   26-Feb-03    RMS     Fixed bug in vector calculation for VAXen
   22-Feb-03    RMS     Fixed ordering bug in queue process
                        Fixed flags table to allow MD_CSE everywhere
   09-Jan-03    RMS     Fixed bug in transfer end packet status
   17-Oct-02    RMS     Fixed bug in read reverse (Hans Pufal)
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "TQK50 not supported on PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#if (UNIBUS)
#define INIT_TYPE       TQ8_TYPE
#define INIT_CAP        TQ8_CAP
#else
#define INIT_TYPE       TQ5_TYPE
#define INIT_CAP        TQ5_CAP
#endif

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define INIT_TYPE       TQ5_TYPE
#define INIT_CAP        TQ5_CAP
#endif

#include "pdp11_uqssp.h"
#include "pdp11_mscp.h"
#include "sim_tape.h"

#define UF_MSK          (UF_SCH|UF_VSS|UF_CMR|UF_CMW)   /* settable flags */

#define TQ_SH_MAX       24                              /* max display wds */
#define TQ_SH_PPL       8                               /* wds per line */
#define TQ_SH_DPL       4                               /* desc per line */
#define TQ_SH_RI        001                             /* show rings */
#define TQ_SH_FR        002                             /* show free q */
#define TQ_SH_RS        004                             /* show resp q */
#define TQ_SH_UN        010                             /* show unit q's */
#define TQ_SH_ALL       017                             /* show all */

#define TQ_CLASS        1                               /* TQK50 class */
#define TQ_DHTMO        0                               /* def host timeout */
#define TQ_DCTMO        120                             /* def ctrl timeout */
#define TQ_NUMDR        4                               /* # drives */
#define TQ_MAXFR        (1 << 16)                       /* max xfer */

#define UNIT_V_ONL      (MTUF_V_UF + 0)                 /* online */
#define UNIT_V_ATP      (MTUF_V_UF + 1)                 /* attn pending */
#define UNIT_V_SXC      (MTUF_V_UF + 2)                 /* serious exc */
#define UNIT_V_POL      (MTUF_V_UF + 3)                 /* position lost */
#define UNIT_V_TMK      (MTUF_V_UF + 4)                 /* tape mark seen */
#define UNIT_ONL        (1 << UNIT_V_ONL)
#define UNIT_ATP        (1 << UNIT_V_ATP)
#define UNIT_SXC        (1 << UNIT_V_SXC)
#define UNIT_POL        (1 << UNIT_V_POL)
#define UNIT_TMK        (1 << UNIT_V_TMK)
#define cpkt            us9                             /* current packet */
#define pktq            us10                            /* packet queue */
#define uf              buf                             /* settable unit flags */
#define objp            wait                            /* object position */
#define unit_plug       u4                              /* drive unit plug value */
#define io_status       u5                              /* io status from callback */
#define io_complete     u6                              /* io completion flag */
#define TQ_WPH(u)       ((sim_tape_wrp (u))? UF_WPH: 0)
#define results         up7                             /* xfer buffer & results */

#define CST_S1          0                               /* init stage 1 */
#define CST_S1_WR       1                               /* stage 1 wrap */
#define CST_S2          2                               /* init stage 2 */
#define CST_S3          3                               /* init stage 3 */
#define CST_S3_PPA      4                               /* stage 3 sa wait */
#define CST_S3_PPB      5                               /* stage 3 ip wait */
#define CST_S4          6                               /* stage 4 */
#define CST_UP          7                               /* online */
#define CST_DEAD        8                               /* fatal error */

#define tq_comm         tq_rq.ba

#define ERR             0                               /* must be SCPE_OK! */
#define OK              1

#define CMF_IMM         0x10000                         /* immediate */
#define CMF_SEQ         0x20000                         /* sequential */
#define CMF_WR          0x40000                         /* write */
#define CMF_RW          0x80000                         /* resp to GCS */

/* Internal packet management */

#define TQ_NPKTS        32                              /* # packets (pwr of 2) */
#define TQ_M_NPKTS      (TQ_NPKTS - 1)                  /* mask */
#define TQ_PKT_SIZE_W   32                              /* payload size (wds) */
#define TQ_PKT_SIZE     (TQ_PKT_SIZE_W * sizeof (int16))

struct tqpkt {
    int16       link;                                   /* link to next */
    uint16      d[TQ_PKT_SIZE_W];                       /* data */
    };

/* Packet payload extraction and insertion */

#define GETP(p,w,f)     ((tq_pkt[p].d[w] >> w##_V_##f) & w##_M_##f)
#define GETP32(p,w)     (((uint32) tq_pkt[p].d[w]) | \
                        (((uint32) tq_pkt[p].d[(w)+1]) << 16))
#define PUTP32(p,w,x)   tq_pkt[p].d[w] = (x) & 0xFFFF; \
                        tq_pkt[p].d[(w)+1] = ((x) >> 16) & 0xFFFF

/* Controller and device types - TQK50 must be swre rev 5 or later */

#define TQ5_TYPE        0                               /* TK50 */
#define TQ5_UQPM        3                               /* UQ port ID */
#define TQ5_CMOD        9                               /* ctrl ID */
#define TQ5_UMOD        3                               /* unit ID */
#define TQ5_MED         0x6D68B032                      /* media ID */
#define TQ5_CREV        ((1 << 8) | 5)                  /* ctrl revs */ 
#define TQ5_FREV        0                               /* formatter revs */
#define TQ5_UREV        0                               /* unit revs */
#define TQ5_CAP         (94 * (1 << 20))                /* capacity */
#define TQ5_FMT         (TF_CTP|TF_CTP_LO)              /* menu */

#define TQ7_TYPE        1                               /* TK70 */
#define TQ7_UQPM        14                              /* UQ port ID */
#define TQ7_CMOD        14                              /* ctrl ID */
#define TQ7_UMOD        14                              /* unit ID */
#define TQ7_MED         0x6D68B046                      /* media ID */
#define TQ7_CREV        ((1 << 8) | 5)                  /* ctrl revs */ 
#define TQ7_FREV        0                               /* formatter revs */
#define TQ7_UREV        0                               /* unit revs */
#define TQ7_CAP         (300 * (1 << 20))               /* capacity */
#define TQ7_FMT         (TF_CTP|TF_CTP_LO)              /* menu */

#define TQ8_TYPE        2                               /* TU81 */
#define TQ8_UQPM        5                               /* UQ port ID */
#define TQ8_CMOD        5                               /* ctrl ID */
#define TQ8_UMOD        2                               /* unit ID */
#define TQ8_MED         0x6D695051                      /* media ID */
#define TQ8_CREV        ((1 << 8) | 5)                  /* ctrl revs */ 
#define TQ8_FREV        0                               /* formatter revs */
#define TQ8_UREV        0                               /* unit revs */
#define TQ8_CAP         (180 * (1 << 20))               /* capacity */
#define TQ8_FMT         (TF_9TK|TF_9TK_GRP)             /* menu */

#define TQU_TYPE        3                               /* TKuser defined */
#define TQU_UQPM        3                               /* UQ port ID */
#define TQU_CMOD        9                               /* ctrl ID */
#define TQU_UMOD        3                               /* unit ID */
#define TQU_MED         0x6D68B032                      /* media ID */
#define TQU_CREV        ((1 << 8) | 5)                  /* ctrl revs */ 
#define TQU_FREV        0                               /* formatter revs */
#define TQU_UREV        0                               /* unit revs */
#define TQU_CAP         (94 * (1 << 20))                /* capacity */
#define TQU_FMT         (TF_CTP|TF_CTP_LO)              /* menu */
#define TQU_MINC        30                              /* min cap MB */
#define TQU_MAXC        2000                            /* max cap MB */
#define TQU_EMAXC       2000000000                      /* ext max cap MB */

#define TQ_DRV(d) \
    d##_UQPM, \
    d##_CMOD, d##_MED,  d##_FMT,  d##_CAP, \
    d##_UMOD, d##_CREV, d##_FREV, d##_UREV

#define TEST_EOT(u)     (sim_tape_eot (u))

struct drvtyp {
    uint32      uqpm;                                   /* UQ port model */
    uint16      cmod;                                   /* ctrl model */
    uint32      med;                                    /* MSCP media */
    uint16      fmt;                                    /* flags */
    t_addr      cap;                                    /* capacity */
    uint16      umod;                                   /* unit model */
    uint16      cver;
    uint16      fver;
    uint16      uver;
    const char  *name;
    };

static struct drvtyp drv_tab[] = {
    { TQ_DRV (TQ5), "TK50" },
    { TQ_DRV (TQ7), "TK70" },
    { TQ_DRV (TQ8), "TU81" },
    { TQ_DRV (TQU), "TKUSER" },
    };

/* Data */

uint32 tq_sa = 0;                                       /* status, addr */
uint32 tq_saw = 0;                                      /* written data */
uint32 tq_s1dat = 0;                                    /* S1 data */
uint32 tq_csta = 0;                                     /* ctrl state */
uint32 tq_perr = 0;                                     /* last error */
uint16 tq_cflgs = 0;                                    /* ctrl flags */
uint32 tq_prgi = 0;                                     /* purge int */
uint32 tq_pip = 0;                                      /* poll in progress */
struct uq_ring tq_cq = { 0 };                           /* cmd ring */
struct uq_ring tq_rq = { 0 };                           /* rsp ring */
struct tqpkt tq_pkt[TQ_NPKTS];                          /* packet queue */
uint16 tq_freq = 0;                                     /* free list */
uint16 tq_rspq = 0;                                     /* resp list */
uint16 tq_max_plug;                                     /* highest unit plug number */
uint32 tq_pbsy = 0;                                     /* #busy pkts */
uint32 tq_credits = 0;                                  /* credits */
uint32 tq_hat = 0;                                      /* host timer */
uint32 tq_htmo = TQ_DHTMO;                              /* host timeout */
int32 tq_itime = 200;                                   /* init time, except */
int32 tq_itime4 = 10;                                   /* stage 4 */
int32 tq_qtime = 200;                                   /* queue time */
int32 tq_xtime = 500;                                   /* transfer time */
int32 tq_rwtime = 2000000;                              /* rewind time 2 sec (adjusted later) */
int32 tq_typ = INIT_TYPE;                               /* device type */

/* Command table - legal modifiers (low 16b) and flags (high 16b) */

static uint32 tq_cmf[64] = {
    0,                                                  /* 0 */
    CMF_IMM,                                            /* abort */
    CMF_IMM|MD_CSE,                                     /* get cmd status */
    CMF_IMM|MD_CSE|MD_NXU,                              /* get unit status */
    CMF_IMM|MD_CSE,                                     /* set ctrl char */
    0, 0, 0,                                            /* 5-7 */
    CMF_SEQ|MD_ACL|MD_CDL|MD_CSE|MD_EXA|MD_UNL,         /* available */
    CMF_SEQ|MD_CDL|MD_CSE|MD_SWP|MD_EXA,                /* online */
    CMF_SEQ|MD_CDL|MD_CSE|MD_SWP|MD_EXA,                /* set unit char */
    CMF_IMM,                                            /* define acc paths */
    0, 0, 0, 0,                                         /* 12-15 */
    CMF_SEQ|CMF_RW|MD_CDL|MD_CSE|MD_REV|                /* access */
            MD_SCH|MD_SEC|MD_SER,
    0,                                                  /* 17 */
    CMF_SEQ|CMF_WR|MD_CDL|MD_CSE|MD_IMM,                /* erase */
    CMF_SEQ|CMF_WR|MD_CDL|MD_CSE,                       /* flush */
    0, 0,                                               /* 20-21 */
    CMF_SEQ|CMF_WR|MD_CDL|MD_CSE|MD_IMM,                /* erase gap */
    0, 0, 0, 0, 0, 0, 0, 0, 0,                          /* 22-31 */
    CMF_SEQ|CMF_RW|MD_CDL|MD_CSE|MD_REV|                /* compare */
            MD_SCH|MD_SEC|MD_SER,
    CMF_SEQ|CMF_RW|MD_CDL|MD_CSE|MD_REV|MD_CMP|         /* read */
            MD_SCH|MD_SEC|MD_SER,
    CMF_SEQ|CMF_RW|CMF_WR|MD_CDL|MD_CSE|MD_IMM|         /* write */
            MD_CMP|MD_ERW|MD_SEC|MD_SER,
    0,                                                  /* 35 */
    CMF_SEQ|CMF_WR|MD_CDL|MD_CSE|MD_IMM,                /* wr tape mark */
    CMF_SEQ|MD_CDL|MD_CSE|MD_IMM|MD_OBC|                /* reposition */
            MD_REV|MD_RWD|MD_DLE|
            MD_SCH|MD_SEC|MD_SER,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                       /* 38-47 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
    };  

static const char *tq_cmdname[] = {
    "",                                                 /*  0 */
    "ABO",                                              /*  1 b: abort */
    "GCS",                                              /*  2 b: get command status */
    "GUS",                                              /*  3 b: get unit status */
    "SCC",                                              /*  4 b: set controller char */
    "","","",                                           /*  5-7 */
    "AVL",                                              /*  8 b: available */
    "ONL",                                              /*  9 b: online */
    "SUC",                                              /* 10 b: set unit char */
    "DAP",                                              /* 11 b: det acc paths - nop */
    "","","","",                                        /* 12-15 */
    "ACC",                                              /* 16 b: access */
    "CCD",                                              /* 17 d: compare - nop */
    "ERS",                                              /* 18 b: erase */
    "FLU",                                              /* 19 d: flush - nop */
    "","",                                              /* 20-21 */
    "ERG",                                              /* 22 t: erase gap */
    "","","","","","","","","",                         /* 23-31 */
    "CMP",                                              /* 32 b: compare */
    "RD",                                               /* 33 b: read */
    "WR",                                               /* 34 b: write */
    "",                                                 /* 35 */
    "WTM",                                              /* 36 t: write tape mark */
    "POS",                                              /* 37 t: reposition */
    "","","","","","","","","",                         /* 38-46 */
    "FMT",                                              /* 47 d: format */
    "","","","","","","","","","","","","","","","",    /* 48-63 */
    "AVA",                                              /* 64 b: unit now avail */
    };

/* Forward references */

t_stat tq_rd (int32 *data, int32 PA, int32 access);
t_stat tq_wr (int32 data, int32 PA, int32 access);
int32 tq_inta (void);
t_stat tq_svc (UNIT *uptr);
t_stat tq_tmrsvc (UNIT *uptr);
t_stat tq_quesvc (UNIT *uptr);
t_stat tq_reset (DEVICE *dptr);
t_stat tq_attach (UNIT *uptr, CONST char *cptr);
t_stat tq_detach (UNIT *uptr);
t_stat tq_boot (int32 unitno, DEVICE *dptr);
t_stat tq_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tq_show_unitq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tq_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tq_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tq_set_plug (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tq_show_plug (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat tq_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *tq_description (DEVICE *dptr);

t_bool tq_step4 (void);
t_bool tq_mscp (uint16 pkt, t_bool q);
t_bool tq_abo (uint16 pkt);
t_bool tq_avl (uint16 pkt);
t_bool tq_erase (uint16 pkt);
t_bool tq_flu (uint16 pkt);
t_bool tq_gcs (uint16 pkt);
t_bool tq_gus (uint16 pkt);
t_bool tq_onl (uint16 pkt);
t_bool tq_pos (uint16 pkt);
t_bool tq_rw (uint16 pkt);
t_bool tq_scc (uint16 pkt);
t_bool tq_suc (uint16 pkt);
t_bool tq_wtm (uint16 pkt);
t_bool tq_plf (uint32 err);
t_bool tq_dte (UNIT *uptr, uint16 err);
t_bool tq_hbe (UNIT *uptr, uint32 ba);
t_bool tq_una (UNIT *uptr);
uint32 tq_map_status (UNIT *uptr, t_stat st);
void tq_rdbuff_top (UNIT *uptr, t_mtrlnt *tbc);
uint32 tq_rdbuff_bottom (UNIT *uptr, t_mtrlnt *tbc);
void tq_rdbufr_top (UNIT *uptr, t_mtrlnt *tbc);
uint32 tq_rdbufr_bottom (UNIT *uptr, t_mtrlnt *tbc);
t_bool tq_deqf (uint16 *pkt);
uint16 tq_deqh (uint16 *lh);
void tq_enqh (uint16 *lh, int16 pkt);
void tq_enqt (uint16 *lh, int16 pkt);
t_bool tq_getpkt (uint16 *pkt);
t_bool tq_putpkt (uint16 pkt, t_bool qt);
t_bool tq_getdesc (struct uq_ring *ring, uint32 *desc);
t_bool tq_putdesc (struct uq_ring *ring, uint32 desc);
uint16 tq_mot_valid (UNIT *uptr, uint32 cmd);
t_stat tq_mot_err (UNIT *uptr, uint32 rsiz);
t_bool tq_mot_end (UNIT *uptr, uint32 flg, uint16 sts, uint32 rsiz);
void tq_putr (int32 pkt, uint32 cmd, uint32 flg, uint16 sts, uint16 lnt, uint16 typ);
void tq_putr_unit (int16 pkt, UNIT *uptr, uint16 lu, t_bool all);
void tq_setf_unit (int16 pkt, UNIT *uptr);
uint32 tq_efl (UNIT *uptr);
void tq_init_int (void);
void tq_ring_int (struct uq_ring *ring);
t_bool tq_fatal (uint32 err);
UNIT *tq_getucb (uint16 lu);

/* TQ data structures

   tq_dev       TQ device descriptor
   tq_unit      TQ unit list
   tq_reg       TQ register list
   tq_mod       TQ modifier list
*/

#define IOLN_TQ         004

DIB tq_dib = {
    IOBA_AUTO, IOLN_TQ, &tq_rd, &tq_wr,
    1, IVCL (TQ), 0, { &tq_inta }, IOLN_TQ,
    };

UNIT tq_unit[] = {
    { UDATA (&tq_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE, INIT_CAP) },
    { UDATA (&tq_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE, INIT_CAP) },
    { UDATA (&tq_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE, INIT_CAP) },
    { UDATA (&tq_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE, INIT_CAP) },
    { UDATA (&tq_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA (&tq_quesvc, UNIT_IDLE|UNIT_DIS, 0) }
    };

#define TQ_TIMER        (TQ_NUMDR)
#define TQ_QUEUE        (TQ_TIMER + 1)

REG tq_reg[] = {
    { GRDATAD (SA,                     tq_sa, DEV_RDX, 16, 0, "status/address register") },
    { GRDATAD (SAW,                   tq_saw, DEV_RDX, 16, 0, "written data") },
    { GRDATAD (S1DAT,               tq_s1dat, DEV_RDX, 16, 0, "step 1 init host data") },
    { GRDATAD (CQIOFF,            tq_cq.ioff, DEV_RDX, 32, 0, "command queue intr offset") },
    { GRDATAD (CQBA,                tq_cq.ba, DEV_RDX, 22, 0, "command queue base address") },
    { GRDATAD (CQLNT,              tq_cq.lnt, DEV_RDX,  8, 2, "command queue length"), REG_NZ },
    { GRDATAD (CQIDX,              tq_cq.idx, DEV_RDX,  8, 2, "command queue index") },
    { GRDATAD (TQIOFF,            tq_rq.ioff, DEV_RDX, 32, 0, "request queue intr offset") },
    { GRDATAD (TQBA,                tq_rq.ba, DEV_RDX, 22, 0, "request queue base address") },
    { GRDATAD (TQLNT,              tq_rq.lnt, DEV_RDX,  8, 2, "request queue length"), REG_NZ },
    { GRDATAD (TQIDX,              tq_rq.idx, DEV_RDX,  8, 2, "request queue index") },
    { DRDATAD (FREE,                 tq_freq, 5,              "head of free packet list") },
    { DRDATAD (RESP,                 tq_rspq, 5,              "head of response packet list") },
    { DRDATAD (PBSY,                 tq_pbsy, 5,              "number of busy packets") },
    { GRDATAD (CFLGS,               tq_cflgs, DEV_RDX, 16, 0, "controller flags") },
    { GRDATAD (CSTA,                 tq_csta, DEV_RDX,  4, 0, "controller state") },
    { GRDATAD (PERR,                 tq_perr, DEV_RDX,  9, 0, "port error number") },
    { DRDATAD (CRED,              tq_credits, 5,              "host credits") },
    { DRDATAD (HAT,                   tq_hat, 17,             "host available timer") },
    { DRDATAD (HTMO,                 tq_htmo, 17,             "host timeout value") },
    { URDATAD (CPKT,         tq_unit[0].cpkt, 10, 5, 0, TQ_NUMDR, 0, "current packet, units 0 to 3") },
    { URDATAD (PKTQ,         tq_unit[0].pktq, 10, 5, 0, TQ_NUMDR, 0, "packet queue, units 0 to 3") },
    { URDATAD (UFLG,           tq_unit[0].uf, DEV_RDX, 16, 0, TQ_NUMDR, 0, "unit flags, units 0 to 3") },
    { URDATAD (POS,           tq_unit[0].pos, 10, T_ADDR_W, 0, TQ_NUMDR, 0, "position, units 0 to 3") },
    { URDATAD (OBJP,         tq_unit[0].objp, 10, 32, 0, TQ_NUMDR, 0, "object position, units 0 to 3") },
    { FLDATA  (PRGI,                 tq_prgi, 0), REG_HIDDEN },
    { FLDATA  (PIP,                   tq_pip, 0), REG_HIDDEN },
    { FLDATAD (INT,                IREQ (TQ), INT_V_TQ,       "interrupt pending flag") },
    { DRDATAD (ITIME,               tq_itime, 24,             "init time delay, except stage 4"), PV_LEFT + REG_NZ },
    { DRDATAD (I4TIME,             tq_itime4, 24,             "init stage 4 delay"), PV_LEFT + REG_NZ },
    { DRDATAD (QTIME,               tq_qtime, 24,             "response time for 'immediate' packets"), PV_LEFT + REG_NZ },
    { DRDATAD (XTIME,               tq_xtime, 24,             "response time for data transfers"), PV_LEFT + REG_NZ },
    { DRDATAD (RWTIME,             tq_rwtime, 32,             "rewind time 2 sec (adjusted later)"), PV_LEFT + REG_NZ },
    { BRDATAD (PKTS,                 tq_pkt, DEV_RDX, 16, TQ_NPKTS * (TQ_PKT_SIZE_W + 1), "packet buffers, 33W each, 32 entries") },
    { URDATAD (PLUG,    tq_unit[0].unit_plug, 10, 32, 0, TQ_NUMDR, PV_LEFT | REG_RO, "unit plug value, units 0 to 3") },
    { DRDATA  (DEVTYPE,               tq_typ, 2), REG_HRO },
    { DRDATA  (DEVCAP, drv_tab[TQU_TYPE].cap, T_ADDR_W), PV_LEFT | REG_HRO },
    { GRDATA  (DEVADDR,            tq_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC,            tq_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB tq_mod[] = {
    { MTUF_WLK,         0, "write enabled",  "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable tape drive" },
    { MTUF_WLK,  MTUF_WLK, "write locked",   "LOCKED", 
        NULL, NULL, NULL, "Write lock tape drive"  },
    { MTAB_XTD|MTAB_VDV,  TQ5_TYPE,           NULL,   "TK50",
        &tq_set_type, NULL, NULL, "Set TK50 Device Type" },
    { MTAB_XTD|MTAB_VDV,  TQ7_TYPE,           NULL,   "TK70",
        &tq_set_type, NULL, NULL, "Set TK70 Device Type"  },
    { MTAB_XTD|MTAB_VDV,  TQ8_TYPE,           NULL,  "TU81",
        &tq_set_type, NULL, NULL, "Set TU81 Device Type"  },
    { MTAB_XTD|MTAB_VDV,  TQU_TYPE,           NULL,  "TKUSER",
        &tq_set_type, NULL, NULL, "Set TKUSER=size Device Type"  },
    { MTAB_XTD|MTAB_VDV,         0,         "TYPE",  NULL,
        NULL, &tq_show_type, NULL, "Display device type" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "UNIT", "UNIT=val (0-65534)",
      &tq_set_plug, &tq_show_plug, NULL, "Set/Display Unit plug value" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TQ_SH_RI, "RINGS", NULL,
        NULL, &tq_show_ctrl, NULL, "Display command and response rings" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TQ_SH_FR, "FREEQ", NULL,
        NULL, &tq_show_ctrl, NULL, "Display free queue" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TQ_SH_RS, "RESPQ", NULL,
        NULL, &tq_show_ctrl, NULL, "Display response queue" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TQ_SH_UN, "UNITQ", NULL,
        NULL, &tq_show_ctrl, NULL, "Display all unit queues" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TQ_SH_ALL, "ALL", NULL,
        NULL, &tq_show_ctrl, NULL, "Display complete controller state" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0,        "UNITQ", NULL,
        NULL, &tq_show_unitq, NULL, "Display unit queue" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "FORMAT", "FORMAT",
        &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, "Set/Display tape format (SIMH, E11, TPC, P7B, AWS, TAR)" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0,       "CAPACITY", "CAPACITY",
        &sim_tape_set_capac, &sim_tape_show_capac, NULL, "Set/Display capacity" },
#if defined (VM_PDP11)
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 004,     "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "AUTOCONFIGURE",
        &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
#else
    { MTAB_XTD|MTAB_VDV, 004,               "ADDRESS", NULL,
        NULL, &show_addr, NULL, "Bus address" },
#endif
    { MTAB_XTD|MTAB_VDV, 0,                 "VECTOR", NULL,
        NULL, &show_vec, NULL, "Interrupt vector" },
    { 0 }
    };

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_INI  0x0002                                 /* display setup/init sequence info */
#define DBG_REG  0x0004                                 /* trace read/write registers */
#define DBG_REQ  0x0008                                 /* display transfer requests */
#define DBG_TAP  0x0010                                 /* display sim_tape activities */
#define DBG_STR  MTSE_DBG_STR                           /* display tape structure detail */
#define DBG_POS  MTSE_DBG_POS                           /* display position activities */
#define DBG_DAT  MTSE_DBG_DAT                           /* display transfer data */

DEBTAB tq_debug[] = {
  {"TRACE",  DBG_TRC,   "trace routine calls"},
  {"INIT",   DBG_INI,   "display setup/init sequence info"},
  {"REG",    DBG_REG,   "trace read/write registers"},
  {"REQ",    DBG_REQ,   "display transfer requests"},
  {"TAPE",   DBG_TAP,   "display sim_tape activities"},
  {"STR",    DBG_STR,   "display tape structure detail"},
  {"POS",    DBG_POS,   "display position activities"},
  {"DATA",   DBG_DAT,   "display transfer data"},
  {0}
};

DEVICE tq_dev = {
    "TQ", tq_unit, tq_reg, tq_mod,
    TQ_NUMDR + 2, 10, T_ADDR_W, 1, DEV_RDX, 8,
    NULL, NULL, &tq_reset,
    &tq_boot, &tq_attach, &tq_detach,
    &tq_dib, DEV_DISABLE | DEV_UBUS | DEV_QBUS | DEV_DEBUG | DEV_TAPE,
    0, tq_debug,
    NULL, NULL, &tq_help, NULL, NULL, 
    &tq_description
    };


struct tq_req_results {           /* intermediate State during tape motion commands */
    t_stat io_status;
    int32 io_complete;
    int rewind_done;
    uint32 sts;
    uint32 sktmk;
    uint32 skrec;
    t_mtrlnt tbc;
    int32 objupd;
    uint8 tqxb[TQ_MAXFR];
    };

/* I/O dispatch routines, I/O addresses 17774500 - 17774502

   17774500     IP      read/write
   17774502     SA      read/write
*/

t_stat tq_rd (int32 *data, int32 PA, int32 access)
{
sim_debug(DBG_REG, &tq_dev, "tq_rd(PA=0x%08X [%s], access=%d)=0x%04X\n", PA, ((PA >> 1) & 01) ? "SA" : "IP", access, ((PA >> 1) & 01) ? tq_sa : 0);

switch ((PA >> 1) & 01) {                               /* decode PA<1> */
    case 0:                                             /* IP */
        *data = 0;                                      /* reads zero */
        if (tq_csta == CST_S3_PPB)                      /* waiting for poll? */
            tq_step4 ();
        else if (tq_csta == CST_UP) {                   /* if up */
            tq_pip = 1;                                 /* poll host */
            sim_activate (&tq_unit[TQ_QUEUE], tq_qtime);
            }
        break;

    case 1:                                             /* SA */
        *data = tq_sa;
        break;
        }

return SCPE_OK;
}

t_stat tq_wr (int32 data, int32 PA, int32 access)
{
sim_debug(DBG_REG, &tq_dev, "tq_wr(PA=0x%08X [%s], access=%d, data=0x%04X)\n", PA, ((PA >> 1) & 01) ? "SA" : "IP", access, data);

switch ((PA >> 1) & 01) {                               /* decode PA<1> */

    case 0:                                             /* IP */
        tq_reset (&tq_dev);                             /* init device */
        sim_debug (DBG_REQ, &tq_dev, "initialization started\n");
        break;

    case 1:                                             /* SA */
        tq_saw = data;
        if (tq_csta < CST_S4)                           /* stages 1-3 */
            sim_activate (&tq_unit[TQ_QUEUE], tq_itime);
        else if (tq_csta == CST_S4)                     /* stage 4 (fast) */
            sim_activate (&tq_unit[TQ_QUEUE], tq_itime4);
        break;
        }

return SCPE_OK;
}

/* Transition to step 4 - init communications region */

t_bool tq_step4 (void)
{
int32 i, lnt;
uint32 base;
uint16 zero[SA_COMM_MAX >> 1];

tq_rq.ioff = SA_COMM_RI;                                /* set intr offset */
tq_rq.ba = tq_comm;                                     /* set rsp q base */
tq_rq.lnt = SA_S1H_RQ (tq_s1dat) << 2;                  /* get resp q len */
tq_cq.ioff = SA_COMM_CI;                                /* set intr offset */
tq_cq.ba = tq_comm + tq_rq.lnt;                         /* set cmd q base */
tq_cq.lnt = SA_S1H_CQ (tq_s1dat) << 2;                  /* get cmd q len */
tq_cq.idx = tq_rq.idx = 0;                              /* clear q idx's */
if (tq_prgi)
    base = tq_comm + SA_COMM_QQ;
else base = tq_comm + SA_COMM_CI;
lnt = tq_comm + tq_cq.lnt + tq_rq.lnt - base;           /* comm lnt */
if (lnt > SA_COMM_MAX)                                  /* paranoia */
    lnt = SA_COMM_MAX;
for (i = 0; i < (lnt >> 1); i++)                        /* clr buffer */
    zero[i] = 0;
if (Map_WriteW (base, lnt, zero))                       /* zero comm area */
    return tq_fatal (PE_QWE);                           /* error? */
tq_sa = SA_S4 | (drv_tab[tq_typ].uqpm << SA_S4C_V_MOD) |/* send step 4 */
    ((drv_tab[tq_typ].cver & 0xFF) << SA_S4C_V_VER);
tq_csta = CST_S4;                                       /* set step 4 */
tq_init_int ();                                         /* poke host */
return OK;
}

/* Queue service - invoked when any of the queues (host queue, unit
   queues, response queue) require servicing.  Also invoked during
   initialization to provide some delay to the next step.

   Process at most one item off each unit queue
   If the unit queues were empty, process at most one item off the host queue
   Process at most one item off the response queue

   If all queues are idle, terminate thread
*/

t_stat tq_quesvc (UNIT *uptr)
{
int32 i, cnid;
uint16 pkt = 0;
UNIT *nuptr;

sim_debug(DBG_TRC, &tq_dev, "tq_quesvc\n");

if (tq_csta < CST_UP) {                                 /* still init? */
    sim_debug(DBG_INI, &tq_dev, "CSTA=%d, SAW=0x%X\n", tq_csta, tq_saw);

    switch (tq_csta) {                                  /* controller state? */
    case CST_S1:                                        /* need S1 reply */
        if (tq_saw & SA_S1H_VL) {                       /* valid? */
            if (tq_saw & SA_S1H_WR) {                   /* wrap? */
                tq_sa = tq_saw;                         /* echo data */
                tq_csta = CST_S1_WR;                    /* endless loop */
                }
            else {
                tq_s1dat = tq_saw;                      /* save data */
                tq_dib.vec = (tq_s1dat & SA_S1H_VEC) << 2; /* get vector */
                tq_sa = SA_S2 | SA_S2C_PT | SA_S2C_EC (tq_s1dat);
                tq_csta = CST_S2;                       /* now in step 2 */
                tq_init_int ();                         /* intr if req */
                }
            }                                           /* end if valid */
        break;

    case CST_S1_WR:                                     /* wrap mode */
        tq_sa = tq_saw;                                 /* echo data */
        break;

    case CST_S2:                                        /* need S2 reply */
        tq_comm = tq_saw & SA_S2H_CLO;                  /* get low addr */
        tq_prgi = tq_saw & SA_S2H_PI;                   /* get purge int */
        tq_sa = SA_S3 | SA_S3C_EC (tq_s1dat);
        tq_csta = CST_S3;                               /* now in step 3 */
        tq_init_int ();                                 /* intr if req */
        break;

    case CST_S3:                                        /* need S3 reply */
        tq_comm = ((tq_saw & SA_S3H_CHI) << 16) | tq_comm;
        if (tq_saw & SA_S3H_PP) {                       /* purge/poll test? */
            tq_sa = 0;                                  /* put 0 */
            tq_csta = CST_S3_PPA;                       /* wait for 0 write */
            }
        else tq_step4 ();                               /* send step 4 */
        break;

    case CST_S3_PPA:                                    /* need purge test */
        if (tq_saw)                                     /* data not zero? */
            tq_fatal (PE_PPF);
        else tq_csta = CST_S3_PPB;                      /* wait for poll */
        break;

    case CST_S4:                                        /* need S4 reply */
        if (tq_saw & SA_S4H_GO) {                       /* go set? */
            sim_debug (DBG_REQ, &tq_dev, "initialization complete\n");
            tq_csta = CST_UP;                           /* we're up */
            tq_sa = 0;                                  /* clear SA */
            sim_activate_after (&tq_unit[TQ_TIMER], 1000000);
            if ((tq_saw & SA_S4H_LF) && tq_perr)
                tq_plf (tq_perr);
            tq_perr = 0;
            }
        break;
        }                                               /* end switch */                        
    return SCPE_OK;
    }                                                   /* end if */

for (i = 0; i < TQ_NUMDR; i++) {                        /* chk unit q's */
    uint16 tpkt;

    nuptr = tq_dev.units + i;                           /* ptr to unit */
    if (nuptr->cpkt || (nuptr->pktq == 0))
        continue;
    tpkt = nuptr->pktq;
    pkt = tq_deqh (&tpkt);                              /* get top of q */
    nuptr->pktq = tpkt;
    if (!tq_mscp (pkt, FALSE))                          /* process */
        return SCPE_OK;
    }
if ((pkt == 0) && tq_pip) {                             /* polling? */
    if (!tq_getpkt (&pkt))                              /* get host pkt */
        return SCPE_OK;
    if (pkt) {                                          /* got one? */
        UNIT *up = tq_getucb (tq_pkt[pkt].d[CMD_UN]);

        if (up)
            sim_debug (DBG_REQ, &tq_dev, "cmd=%04X(%3s), mod=%04X, unit=%d, bc=%04X%04X, ma=%04X%04X, obj=%d, pos=0x%" T_ADDR_FMT "X\n", 
                    tq_pkt[pkt].d[CMD_OPC], tq_cmdname[tq_pkt[pkt].d[CMD_OPC]&0x3f],
                    tq_pkt[pkt].d[CMD_MOD], tq_pkt[pkt].d[CMD_UN],
                    tq_pkt[pkt].d[RW_BCH], tq_pkt[pkt].d[RW_BCL],
                    tq_pkt[pkt].d[RW_BAH], tq_pkt[pkt].d[RW_BAL],
                    up->objp, up->pos);
        else
            sim_debug (DBG_REQ, &tq_dev, "cmd=%04X(%3s), mod=%04X, unit=%d, bc=%04X%04X, ma=%04X%04X\n", 
                    tq_pkt[pkt].d[CMD_OPC], tq_cmdname[tq_pkt[pkt].d[CMD_OPC]&0x3f],
                    tq_pkt[pkt].d[CMD_MOD], tq_pkt[pkt].d[CMD_UN],
                    tq_pkt[pkt].d[RW_BCH], tq_pkt[pkt].d[RW_BCL],
                    tq_pkt[pkt].d[RW_BAH], tq_pkt[pkt].d[RW_BAL]);

        if (GETP (pkt, UQ_HCTC, TYP) != UQ_TYP_SEQ)     /* seq packet? */
            return tq_fatal (PE_PIE);                   /* no, term thread */
        cnid = GETP (pkt, UQ_HCTC, CID);                /* get conn ID */
        if (cnid == UQ_CID_TMSCP) {                     /* TMSCP packet? */
            if (!tq_mscp (pkt, TRUE))                   /* proc, q non-seq */
                return SCPE_OK;
            }
        else if (cnid == UQ_CID_DUP) {                  /* DUP packet? */
            tq_putr (pkt, OP_END, 0, ST_CMD | I_OPCD, RSP_LNT, UQ_TYP_SEQ);
            if (!tq_putpkt (pkt, TRUE))                 /* ill cmd */
                return SCPE_OK;
            }
        else return tq_fatal (PE_ICI);                  /* no, term thread */
        }                                               /* end if pkt */
    else tq_pip = 0;                                    /* discontinue poll */
    }                                                   /* end if pip */
if (tq_rspq) {                                          /* resp q? */
    pkt = tq_deqh ((uint16 *)&tq_rspq);                 /* get top of q */
    if (!tq_putpkt (pkt, FALSE))                        /* send to host */
        return SCPE_OK;
    }                                                   /* end if resp q */
if (pkt)                                                /* more to do? */
    sim_activate (&tq_unit[TQ_QUEUE], tq_qtime);
return SCPE_OK;                                         /* done */
}

/* Clock service (roughly once per second) */

t_stat tq_tmrsvc (UNIT *uptr)
{
int32 i;
UNIT *nuptr;

sim_debug(DBG_TRC, &tq_dev, "tq_tmrsvc\n");

sim_activate_after (uptr, 1000000);                     /* reactivate */
for (i = 0; i < TQ_NUMDR; i++) {                        /* poll */
    nuptr = tq_dev.units + i;
    if ((nuptr->flags & UNIT_ATP) &&                    /* ATN pending? */
        (nuptr->flags & UNIT_ATT) &&                    /* still online? */
        (tq_cflgs & CF_ATN)) {                          /* wanted? */
        if (!tq_una (nuptr))
            return SCPE_OK;
        }
    nuptr->flags = nuptr->flags & ~UNIT_ATP;
    }
if ((tq_hat > 0) && (--tq_hat == 0))                    /* host timeout? */
    tq_fatal (PE_HAT);                                  /* fatal err */ 
return SCPE_OK;
}

/* MSCP packet handling */

t_bool tq_mscp (uint16 pkt, t_bool q)
{
uint16 sts;
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* command */
uint32 flg = GETP (pkt, CMD_OPC, FLG);                  /* flags */
uint32 mdf = tq_pkt[pkt].d[CMD_MOD];                    /* modifier */
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_mscp\n");

if ((cmd >= 64) || (tq_cmf[cmd] == 0)) {                /* invalid cmd? */
    cmd = OP_END;                                       /* set end op */
    sts = ST_CMD | I_OPCD;                              /* ill op */
    }
else if (flg) {                                         /* flags? */
    cmd = cmd | OP_END;                                 /* set end flag */
    sts = ST_CMD | I_FLAG;                              /* ill flags */
    }
else if (mdf & ~tq_cmf[cmd]) {                          /* invalid mod? */
    cmd = cmd | OP_END;                                 /* set end flag */
    sts = ST_CMD | I_MODF;                              /* ill mods */
    }
else {                                                  /* valid cmd */
    if ((uptr = tq_getucb (lu))) {                      /* valid unit? */
        if (q && (tq_cmf[cmd] & CMF_SEQ) &&             /* queueing, seq, */
            (uptr->cpkt || uptr->pktq)) {               /* and active? */
            tq_enqt (&uptr->pktq, pkt);                 /* do later */
            return OK;
            }
//      if (tq_cmf[cmd] & MD_CDL)                       /* clr cch lost? */
//          uptr->flags = uptr->flags & ~UNIT_CDL;
        if ((mdf & MD_CSE) && (uptr->flags & UNIT_SXC)) /* clr ser exc? */
            uptr->flags = uptr->flags & ~UNIT_SXC;
        memset (uptr->results, 0, sizeof (struct tq_req_results)); /* init request state */
        }
    switch (cmd) {

    case OP_ABO:                                        /* abort */
        return tq_abo (pkt);

    case OP_AVL:                                        /* avail */
        return tq_avl (pkt);

    case OP_GCS:                                        /* get cmd status */
        return tq_gcs (pkt);

    case OP_GUS:                                        /* get unit status */
        return tq_gus (pkt);

    case OP_ONL:                                        /* online */
        return tq_onl (pkt);

    case OP_SCC:                                        /* set ctrl char */
        return tq_scc (pkt);

    case OP_SUC:                                        /* set unit char */
        return tq_suc (pkt);

    case OP_ERS:                                        /* erase */
    case OP_ERG:                                        /* erase gap */
        return tq_erase (pkt);

    case OP_FLU:                                        /* flush */
        return tq_flu (pkt);

    case OP_POS:                                        /* position */
        return tq_pos (pkt);

    case OP_WTM:                                        /* write tape mark */
        return tq_wtm (pkt);

    case OP_ACC:                                        /* access */
    case OP_CMP:                                        /* compare */
    case OP_RD:                                         /* read */
    case OP_WR:                                         /* write */
        return tq_rw (pkt);

    case OP_DAP:
        cmd = cmd | OP_END;                             /* set end flag */
        sts = ST_SUC;                                   /* success */
        break;

    default:
        cmd = OP_END;                                   /* set end op */
        sts = ST_CMD | I_OPCD;                          /* ill op */
        break;
        }                                               /* end switch */
    }                                                   /* end else */
tq_putr (pkt, cmd, 0, sts, RSP_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Abort a command - 1st parameter is ref # of cmd to abort */

t_bool tq_abo (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint32 ref = GETP32 (pkt, ABO_REFL);                    /* cmd ref # */
uint16 tpkt, prv;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_abo\n");

tpkt = 0;                                               /* set no mtch */
if ((uptr = tq_getucb (lu))) {                          /* get unit */
    if (uptr->cpkt &&                                   /* curr pkt? */
        (GETP32 (uptr->cpkt, CMD_REFL) == ref)) {       /* match ref? */
        tpkt = uptr->cpkt;                              /* save match */
        uptr->cpkt = 0;                                 /* gonzo */
        sim_cancel (uptr);                              /* cancel unit */
        sim_activate (&tq_unit[TQ_QUEUE], tq_qtime);
        }
    else if (uptr->pktq &&                              /* head of q? */
        (GETP32 (uptr->pktq, CMD_REFL) == ref)) {       /* match ref? */
        tpkt = uptr->pktq;                              /* save match */
        uptr->pktq = tq_pkt[tpkt].link;                 /* unlink */
        }
    else if ((prv = uptr->pktq)) {                      /* srch pkt q */
        while ((tpkt = tq_pkt[prv].link)) {             /* walk list */
            if (GETP32 (tpkt, RSP_REFL) == ref) {       /* match ref? */
                tq_pkt[prv].link = tq_pkt[tpkt].link;   /* unlink */
                    break;
                }
            prv = tpkt;                                 /* no match, next */
            }
        }
    if (tpkt) {                                         /* found target? */
        uint32 tcmd = GETP (tpkt, CMD_OPC, OPC);        /* get opcode */
        tq_putr (tpkt, tcmd | OP_END, 0, ST_ABO, RSP_LNT, UQ_TYP_SEQ);
        if (!tq_putpkt (tpkt, TRUE))
            return ERR;
        }
    }                                                   /* end if unit */
tq_putr (pkt, OP_ABO | OP_END, 0, ST_SUC, ABO_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Unit available - set unit status to available
   Deferred if q'd cmds, bypassed if ser exc */

t_bool tq_avl (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint32 mdf = tq_pkt[pkt].d[CMD_MOD];                    /* modifiers */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_avl\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    if (uptr->flags & UNIT_SXC)                         /* ser exc pending? */
        sts = ST_SXC;
    else {
        uptr->flags = uptr->flags & ~(UNIT_ONL | UNIT_TMK | UNIT_POL);
        sim_tape_rewind (uptr);                         /* rewind */
        uptr->uf = uptr->objp = 0;                      /* clr flags */
        if (uptr->flags & UNIT_ATT) {                   /* attached? */
            sts = ST_SUC;                               /* success */
            if (mdf & MD_UNL)                           /* unload? */
                tq_detach (uptr);
            }
        else sts = ST_OFL | SB_OFL_NV;                  /* no, offline */
        }
    }
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, OP_AVL | OP_END, tq_efl (uptr), sts, AVL_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Get command status - only interested in active xfr cmd */

t_bool tq_gcs (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint32 ref = GETP32 (pkt, GCS_REFL);                    /* ref # */
int32 tpkt;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_gcs\n");

if ((uptr = tq_getucb (lu)) &&                          /* valid lu? */
    (tpkt = uptr->cpkt) &&                              /* queued pkt? */
    (GETP32 (tpkt, CMD_REFL) == ref) &&                 /* match ref? */
    (tq_cmf[GETP (tpkt, CMD_OPC, OPC)] & CMF_RW)) {     /* rd/wr cmd? */
    tq_pkt[pkt].d[GCS_STSL] = tq_pkt[tpkt].d[RW_BCL];
    tq_pkt[pkt].d[GCS_STSH] = tq_pkt[tpkt].d[RW_BCH];
    }
else tq_pkt[pkt].d[GCS_STSL] = tq_pkt[pkt].d[GCS_STSH] = 0;
tq_putr (pkt, OP_GCS | OP_END, 0, ST_SUC, GCS_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Get unit status */

t_bool tq_gus (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_gus\n");

if (tq_pkt[pkt].d[CMD_MOD] & MD_NXU) {                  /* next unit? */
    if (lu > tq_max_plug) {                             /* beyond last unit plug? */
        lu = 0;                                         /* reset to 0 */
        tq_pkt[pkt].d[RSP_UN] = (uint16)lu;
        }
    }
if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
            sts = ST_OFL | SB_OFL_NV;                   /* offl no vol */
    else if (uptr->flags & UNIT_ONL)                    /* online */
        sts = ST_SUC;
    else sts = ST_AVL;                                  /* avail */
    tq_putr_unit (pkt, uptr, lu, FALSE);                /* fill unit fields */
    tq_pkt[pkt].d[GUS_MENU] = drv_tab[tq_typ].fmt;      /* format menu */
    tq_pkt[pkt].d[GUS_CAP] = 0;                         /* free capacity */
    tq_pkt[pkt].d[GUS_FVER] = drv_tab[tq_typ].fver;     /* formatter version */
    tq_pkt[pkt].d[GUS_UVER] = drv_tab[tq_typ].uver;     /* unit version */
    }
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, OP_GUS | OP_END, tq_efl (uptr), sts, GUS_LNT_T, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Unit online - deferred if q'd commands */

t_bool tq_onl (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_onl\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    else if (uptr->flags & UNIT_ONL)                    /* already online? */
        sts = ST_SUC | SB_SUC_ON;
    else {
        sts = ST_SUC;                                   /* mark online */
        sim_tape_rewind (uptr);                         /* rewind */
        uptr->objp = 0;                                 /* clear flags */
        uptr->flags = (uptr->flags | UNIT_ONL) &
            ~(UNIT_TMK | UNIT_POL);                     /* onl, pos ok */
        tq_setf_unit (pkt, uptr);                       /* hack flags */
        }
    tq_putr_unit (pkt, uptr, lu, TRUE);                 /* set fields */
    }
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, OP_ONL | OP_END, tq_efl (uptr), sts, ONL_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Set controller characteristics */

t_bool tq_scc (uint16 pkt)
{
sim_debug(DBG_TRC, &tq_dev, "tq_scc\n");

if (tq_pkt[pkt].d[SCC_MSV])                             /* MSCP ver = 0? */
    tq_putr (pkt, 0, 0, ST_CMD | I_VRSN, SCC_LNT, UQ_TYP_SEQ);
else {
    tq_cflgs = (tq_cflgs & CF_RPL) |                    /* hack ctrl flgs */
        tq_pkt[pkt].d[SCC_CFL];
    if ((tq_htmo = tq_pkt[pkt].d[SCC_TMO]))             /* set timeout */
        tq_htmo = tq_htmo + 2;                          /* if nz, round up */
    tq_pkt[pkt].d[SCC_CFL] = tq_cflgs;                  /* return flags */
    tq_pkt[pkt].d[SCC_TMO] = TQ_DCTMO;                  /* ctrl timeout */
    tq_pkt[pkt].d[SCC_VER] = drv_tab[tq_typ].cver;      /* ctrl version */
    tq_pkt[pkt].d[SCC_CIDA] = 0;                        /* ctrl ID */
    tq_pkt[pkt].d[SCC_CIDB] = 0;
    tq_pkt[pkt].d[SCC_CIDC] = 0;
    tq_pkt[pkt].d[SCC_CIDD] = (TQ_CLASS << SCC_CIDD_V_CLS) |
        (drv_tab[tq_typ].cmod << SCC_CIDD_V_MOD);
    PUTP32 (pkt, SCC_MBCL, TQ_MAXFR);                   /* max bc */
    tq_putr (pkt, OP_SCC | OP_END, 0, ST_SUC, SCC_LNT, UQ_TYP_SEQ);
    }
return tq_putpkt (pkt, TRUE);
}
    
/* Set unit characteristics - defer if q'd commands */

t_bool tq_suc (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_suc\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    if ((uptr->flags & UNIT_ATT) == 0)                  /* not attached? */
        sts = ST_OFL | SB_OFL_NV;                       /* offl no vol */
    else {
        sts = ST_SUC;                                   /* avail or onl */
        tq_setf_unit (pkt, uptr);                       /* hack flags */
        }
    tq_putr_unit (pkt, uptr, lu, TRUE);                 /* set fields */
    }
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, OP_SUC | OP_END, 0, sts, SUC_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Flush - sequential nop - deferred if q'd cmds, bypassed if ser exc */

t_bool tq_flu (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_flu\n");

if ((uptr = tq_getucb (lu)))                            /* unit exist? */
    sts = tq_mot_valid (uptr, OP_FLU);                  /* validate req */
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, OP_FLU | OP_END, tq_efl (uptr), sts, FLU_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Erase, erase gap - deferred if q'd cmds, bypassed if ser exc */

t_bool tq_erase (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint16 sts;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_erase\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    sts = tq_mot_valid (uptr, cmd);                     /* validity checks */
    if (sts == ST_SUC) {                                /* ok? */
        uptr->cpkt = pkt;                               /* op in progress */
        uptr->iostarttime = sim_grtime();
        sim_activate (uptr, 0);                         /* activate */
        return OK;                                      /* done */
        }
    }
else sts = ST_OFL;                                      /* offline */
tq_putr (pkt, cmd | OP_END, tq_efl (uptr), sts, ERS_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Write tape mark - deferred if q'd cmds, bypassed if ser exc */

t_bool tq_wtm (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
uint32 objp = 0;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_wtm\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    objp = uptr->objp;                                  /* position op */
    sts = tq_mot_valid (uptr, OP_WTM);                  /* validity checks */
    if (sts == ST_SUC) {                                /* ok? */
        uptr->cpkt = pkt;                               /* op in progress */
        uptr->iostarttime = sim_grtime();
        sim_activate (uptr, 0);                         /* activate */
        return OK;                                      /* done */
        }
    }
else sts = ST_OFL;                                      /* offline */
PUTP32 (pkt, WTM_POSL, objp);                           /* set obj pos */
tq_putr (pkt, OP_WTM | OP_END, tq_efl (uptr), sts, WTM_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Position - deferred if q'd cmds, bypassed if ser exc */

t_bool tq_pos (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint16 sts;
uint32 objp = 0;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_pos\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    objp = uptr->objp;                                  /* position op */
    sts = tq_mot_valid (uptr, OP_POS);                  /* validity checks */
    if (sts == ST_SUC) {                                /* ok? */
        uptr->cpkt = pkt;                               /* op in progress */
        if ((tq_pkt[pkt].d[CMD_MOD] & MD_RWD) &&        /* rewind? */
            (!(tq_pkt[pkt].d[CMD_MOD] & MD_IMM))) {     /* !immediate? */
            double walltime = (tq_rwtime - 100);

            if (uptr->hwmark)
                walltime *= ((double)uptr->pos)/uptr->hwmark;
            sim_activate_after_d (uptr, 100 + walltime);/* use scaled 2 sec rewind execute time */
            }
        else {                                          /* otherwise */
            uptr->iostarttime = sim_grtime();
            sim_activate (uptr, 0);                     /* use normal execute time */
            }
        return OK;                                      /* done */
        }
    }
else sts = ST_OFL;                                      /* offline */
PUTP32 (pkt, POS_RCL, 0);                               /* clear #skipped */
PUTP32 (pkt, POS_TMCL, 0);
PUTP32 (pkt, POS_POSL, objp);                           /* set obj pos */
tq_putr (pkt, OP_POS | OP_END, tq_efl (uptr), sts, POS_LNT, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Data transfer commands - deferred if q'd commands, bypassed if ser exc */

t_bool tq_rw (uint16 pkt)
{
uint16 lu = tq_pkt[pkt].d[CMD_UN];                      /* unit # */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* opcode */
uint32 bc = GETP32 (pkt, RW_BCL);                       /* byte count */
uint16 sts;
uint32 objp = 0;
UNIT *uptr;

sim_debug(DBG_TRC, &tq_dev, "tq_rw\n");

if ((uptr = tq_getucb (lu))) {                          /* unit exist? */
    objp = uptr->objp;                                  /* position op */
    sts = tq_mot_valid (uptr, cmd);                     /* validity checks */
    if (sts == ST_SUC) {                                /* ok? */
        if ((bc == 0) || (bc > TQ_MAXFR)) {             /* invalid? */
            uptr->flags = uptr->flags | UNIT_SXC;       /* set ser exc */
            sts = ST_CMD | I_BCNT;
            }
        else {
            uptr->cpkt = pkt;                           /* op in progress */
            uptr->iostarttime = sim_grtime();
            sim_activate (uptr, 0);                     /* activate */
            return OK;                                  /* done */
            }
        }
    }
else sts = ST_OFL;                                      /* offline */
PUTP32 (pkt, RW_BCL, 0);                                /* no bytes processed */
PUTP32 (pkt, RW_POSL, objp);                            /* set obj pos */
PUTP32 (pkt, RW_RSZL, 0);                               /* clr rec size */
tq_putr (pkt, cmd | OP_END, tq_efl (uptr), sts, RW_LNT_T, UQ_TYP_SEQ);
return tq_putpkt (pkt, TRUE);
}

/* Validity checks */

uint16 tq_mot_valid (UNIT *uptr, uint32 cmd)
{
sim_debug(DBG_TRC, &tq_dev, "tq_mot_valid\n");

if (uptr->flags & UNIT_SXC)                             /* ser exc pend? */
    return ST_SXC;
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return (ST_OFL | SB_OFL_NV);                        /* offl no vol */
if ((uptr->flags & UNIT_ONL) == 0)                      /* not online? */
    return ST_AVL;                                      /* only avail */
if (tq_cmf[cmd] & CMF_WR) {                             /* write op? */
    if (uptr->uf & UF_WPS) {                            /* swre wlk? */
        uptr->flags = uptr->flags | UNIT_SXC;           /* set ser exc */
        return (ST_WPR | SB_WPR_SW);
        }
    if (TQ_WPH (uptr)) {                                /* hwre wlk? */
        uptr->flags = uptr->flags | UNIT_SXC;           /* set ser exc */
        return (ST_WPR | SB_WPR_HW);
        }
    }
return ST_SUC;                                          /* success! */
}

/* Unit service for motion commands */

/* I/O completion callback */

void tq_io_complete (UNIT *uptr, t_stat status)
{
struct tq_req_results *res = (struct tq_req_results *)uptr->results;

sim_debug(DBG_TRC, &tq_dev, "tq_io_complete(status=%d)\n", status);

res->io_status = status;
res->io_complete = 1;
/* Reschedule for the appropriate delay */
sim_activate_notbefore (uptr, uptr->iostarttime+tq_xtime);
}


t_stat tq_svc (UNIT *uptr)
{
uint32 t;
t_mtrlnt wbc;
int32 pkt = uptr->cpkt;                                 /* get packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* get cmd */
uint32 mdf = tq_pkt[pkt].d[CMD_MOD];                    /* modifier */
uint32 ba = GETP32 (pkt, RW_BAL);                       /* buf addr */
t_mtrlnt bc = GETP32 (pkt, RW_BCL);                     /* byte count */
uint32 nrec = GETP32 (pkt, POS_RCL);                    /* #rec to skip */
uint32 ntmk = GETP32 (pkt, POS_TMCL);                   /* #tmk to skp */
struct tq_req_results *res = (struct tq_req_results *)uptr->results;
int32 io_complete = res->io_complete;

sim_debug (DBG_TRC, &tq_dev, "tq_svc(unit=%d, pkt=%d, cmd=%s, mdf=0x%0X, bc=0x%0x, phase=%s)\n",
           (int)(uptr-tq_dev.units), pkt, tq_cmdname[tq_pkt[pkt].d[CMD_OPC]&0x3f], mdf, bc,
           uptr->io_complete ? "bottom" : "top");

res->io_complete = 0;
if (pkt == 0)                                           /* what??? */
    return SCPE_IERR;
if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    tq_mot_end (uptr, 0, ST_OFL | SB_OFL_NV, 0);        /* offl no vol */
    return SCPE_OK;
    }

if (tq_cmf[cmd] & CMF_WR) {                             /* write op? */
    if (TQ_WPH (uptr)) {                                /* hwre write prot? */
        uptr->flags = uptr->flags | UNIT_SXC;           /* set ser exc */
        tq_mot_end (uptr, 0, ST_WPR | SB_WPR_HW, 0);
        return SCPE_OK;
        }
    if (uptr->uf & UF_WPS) {                            /* swre write prot? */
        uptr->flags = uptr->flags | UNIT_SXC;           /* set ser exc */
        tq_mot_end (uptr, 0, ST_WPR | SB_WPR_SW, 0);
        return SCPE_OK;
        }
    }
if (!io_complete) {
    res->sts = ST_SUC;                                  /* assume success */
    res->tbc = 0;                                       /* assume zero rec */
    }
switch (cmd) {                                          /* case on command */

    case OP_RD:case OP_ACC:case OP_CMP:                 /* read-like op */
        if (!io_complete) {
            if (mdf & MD_REV)                           /* read record */
                tq_rdbufr_top (uptr, &res->tbc);
            else
                tq_rdbuff_top (uptr, &res->tbc);
            return SCPE_OK;
            }
        if (mdf & MD_REV)                           /* read record */
            res->sts = tq_rdbufr_bottom (uptr, &res->tbc);
        else
            res->sts = tq_rdbuff_bottom (uptr, &res->tbc);
        if (res->sts == ST_DRV) {                       /* read error? */
            PUTP32 (pkt, RW_BCL, 0);                    /* no bytes processed */
            return tq_mot_err (uptr, res->tbc);         /* log, done */
            }
        if ((res->sts != ST_SUC) || (cmd == OP_ACC)) {  /* error or access? */
            if (res->sts == ST_TMK)
                uptr->flags = uptr->flags | UNIT_SXC;   /* set ser exc */
            PUTP32 (pkt, RW_BCL, 0);                    /* no bytes processed */
            break;
            }
        if (res->tbc > bc) {                            /* tape rec > buf? */
            uptr->flags = uptr->flags | UNIT_SXC;       /* serious exc */
            res->sts = ST_RDT;                          /* data truncated */
            wbc = bc;                                   /* set working bc */
            }
        else wbc = res->tbc;
        if (cmd == OP_RD) {                             /* read? */
            if ((t = Map_WriteB (ba, wbc, res->tqxb))) {/* store, nxm? */
                PUTP32 (pkt, RW_BCL, wbc - t);          /* adj bc */
                if (tq_hbe (uptr, ba + wbc - t))        /* post err log */
                    tq_mot_end (uptr, EF_LOG, ST_HST | SB_HST_NXM, res->tbc);        
                return SCPE_OK;                         /* end if nxm */
                }
            }                                           /* end if read */
        else {                                          /* compare */
            uint8 mby, dby;
            uint32 mba;
            t_mtrlnt i;
            for (i = 0; i < wbc; i++) {                 /* loop */
                if (mdf & MD_REV) {                     /* reverse? */
                    mba = ba + bc - 1 - i;              /* mem addr */
                    dby = ((uint8 *)res->tqxb)[res->tbc - 1 - i]; /* byte */
                    }
                else {
                    mba = ba + i;
                    dby = ((uint8 *)res->tqxb)[i];
                    }
                if (Map_ReadB (mba, 1, &mby)) {         /* fetch, nxm? */
                    PUTP32 (pkt, RW_BCL, i);            /* adj bc */
                    if (tq_hbe (uptr, mba))             /* post err log */
                        tq_mot_end (uptr, EF_LOG, ST_HST | SB_HST_NXM, res->tbc);
                    return SCPE_OK;
                    }
                if (mby != dby) {                       /* cmp err? */
                    uptr->flags = uptr->flags | UNIT_SXC; /* ser exc */
                    PUTP32 (pkt, RW_BCL, i);            /* adj bc */
                    tq_mot_end (uptr, 0, ST_CMP, res->tbc);
                    return SCPE_OK;                     /* exit */
                    }
                }                                       /* end for */
            }                                           /* end if compare */
        PUTP32 (pkt, RW_BCL, wbc);                      /* bytes read/cmp'd */
        break;

    case OP_WR:                                         /* write */
        if (!io_complete) { /* Top half processing */
            if ((t = Map_ReadB (ba, bc, res->tqxb))) {  /* fetch buf, nxm? */
                PUTP32 (pkt, RW_BCL, 0);                /* no bytes xfer'd */
                if (tq_hbe (uptr, ba + bc - t))         /* post err log */
                    tq_mot_end (uptr, EF_LOG, ST_HST | SB_HST_NXM, bc);     
                return SCPE_OK;                         /* end else wr */
                }
            sim_tape_wrrecf_a (uptr, res->tqxb, bc, tq_io_complete); /* write rec fwd */
            return SCPE_OK;
            } 
        if (res->io_status)
            return tq_mot_err (uptr, bc);               /* log, end */
        uptr->objp = uptr->objp + 1;                    /* upd obj pos */
        if (TEST_EOT (uptr))                            /* EOT on write? */
            uptr->flags = uptr->flags | UNIT_SXC;
        uptr->flags = uptr->flags & ~UNIT_TMK;          /* disable LEOT */
        res->tbc = bc;                                  /* RW_BC is ok */
        break;

    case OP_WTM:                                        /* write tape mark */
        if (!io_complete) { /* Top half processing */
            sim_tape_wrtmk_a (uptr, tq_io_complete);    /* write tmk, err? */
            return SCPE_OK;
            }
        if (res->io_status)
            return tq_mot_err (uptr, 0);                /* log, end */
        uptr->objp = uptr->objp + 1;                    /* incr obj cnt */
    case OP_ERG:                                        /* erase gap */
        if (TEST_EOT (uptr))                            /* EOT on write? */
            uptr->flags = uptr->flags | UNIT_SXC;
        uptr->flags = uptr->flags & ~UNIT_TMK;          /* disable LEOT */
        break;

    case OP_ERS:                                        /* erase */
        if (!io_complete) { /* Top half processing */
            sim_tape_wreomrw_a (uptr, tq_io_complete);    /* write eom, err? */
            return SCPE_OK;
            }
        if (res->io_status)
            return tq_mot_err (uptr, 0);                /* log, end */
        uptr->objp = 0;
        uptr->flags = uptr->flags & ~(UNIT_TMK | UNIT_POL);
        break;

    case OP_POS:                                        /* position */
        if (!io_complete) { /* Top half processing */
            res->sktmk = res->skrec = 0;                /* clr skipped */
            if (mdf & MD_RWD) {                         /* rewind? */
                uptr->objp = 0;                         /* clr flags */
                uptr->flags = uptr->flags & ~(UNIT_TMK | UNIT_POL);
                }
            sim_tape_position_a (uptr,
                                 ((mdf & MD_RWD) ? MTPOS_M_REW : 0) | 
                                 ((mdf & MD_REV) ? MTPOS_M_REV : 0) |
                                 ((mdf & MD_OBC) ? MTPOS_M_OBJ : 0) |
                                 (((mdf & MD_DLE) && !(mdf & MD_REV)) ? MTPOS_M_DLE : 0),
                                 nrec, &res->skrec, ntmk, &res->sktmk, (uint32 *)&res->objupd, tq_io_complete);
            return SCPE_OK;
            }
        res->sts = tq_map_status (uptr, res->io_status);
        if ((res->io_status != MTSE_OK) && (res->io_status != MTSE_TMK) && (res->io_status != MTSE_BOT) && (res->io_status != MTSE_LEOT))
            return tq_mot_err (uptr, 0);                /* log, end */
        sim_debug (DBG_REQ, &tq_dev, "Position Done: mdf=0x%04X, nrec=%d, ntmk=%d, skrec=%d, sktmk=%d, skobj=%d\n", 
                            mdf, nrec, ntmk, res->skrec, res->sktmk, res->objupd);
        if (mdf & MD_REV)
            uptr->objp = uptr->objp - res->objupd;
        else
            uptr->objp = uptr->objp + res->objupd;
        PUTP32 (pkt, POS_RCL, res->skrec);              /* #rec skipped */
        PUTP32 (pkt, POS_TMCL, res->sktmk);             /* #tmk skipped */
        break;

    default:
        return SCPE_IERR;
        }

tq_mot_end (uptr, 0, (uint16)res->sts, res->tbc);       /* done */
return SCPE_OK;
}

/* Motion command drive error */

t_stat tq_mot_err (UNIT *uptr, uint32 rsiz)
{
uptr->flags = (uptr->flags | UNIT_SXC) & ~UNIT_TMK;     /* serious exception */
if (tq_dte (uptr, ST_DRV))                              /* post err log */
    tq_mot_end (uptr, EF_LOG, ST_DRV, rsiz);            /* if ok, report err */
return SCPE_IOERR;
}

/* Motion command complete */

t_bool tq_mot_end (UNIT *uptr, uint32 flg, uint16 sts, uint32 rsiz)
{
uint16 pkt = uptr->cpkt;                                /* packet */
uint32 cmd = GETP (pkt, CMD_OPC, OPC);                  /* get cmd */
uint16 lnt = RW_LNT_T;                                  /* assume rw */

if (cmd == OP_ERG)                                      /* set pkt lnt */
    lnt = ERG_LNT;
else if (cmd == OP_ERS)
    lnt = ERS_LNT;
else if (cmd == OP_WTM)
    lnt = WTM_LNT;
else if (cmd == OP_POS)
    lnt = POS_LNT;

uptr->cpkt = 0;                                         /* done */
if (lnt > ERG_LNT) {                                    /* xfer cmd? */
    PUTP32 (pkt, RW_POSL, uptr->objp);                  /* position */
    PUTP32 (pkt, RW_RSZL, rsiz);                        /* record size */
    }
tq_putr (pkt, cmd | OP_END, flg | tq_efl (uptr), sts, lnt, UQ_TYP_SEQ);
if (!tq_putpkt (pkt, TRUE))                             /* send pkt */
    return ERR;
if (uptr->pktq)                                         /* more to do? */
    sim_activate (&tq_unit[TQ_QUEUE], tq_qtime);        /* activate thread */
return OK;
}

/* Tape motion routines */

uint32 tq_map_status (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_OK:
        break;

    case MTSE_UNATT:
        uptr->flags = uptr->flags | UNIT_SXC;
        return (ST_OFL | SB_OFL_NV);

    case MTSE_FMT:
        uptr->flags = uptr->flags | UNIT_SXC;
        return ST_MFE;

    case MTSE_TMK:
        uptr->flags = uptr->flags | UNIT_SXC;
        return ST_TMK;

    case MTSE_INVRL:
        uptr->flags = uptr->flags | UNIT_SXC | UNIT_POL;
        return ST_FMT;

    case MTSE_RECE:
    case MTSE_IOERR:
        uptr->flags = uptr->flags | UNIT_SXC | UNIT_POL;
        return ST_DRV;

    case MTSE_EOM:
        uptr->flags = uptr->flags | UNIT_SXC | UNIT_POL;
        return ST_DAT;

    case MTSE_BOT:
        uptr->flags = (uptr->flags | UNIT_SXC) & ~UNIT_POL;
        return ST_BOT;

    case MTSE_WRP:
        uptr->flags = uptr->flags | UNIT_SXC;
        return ST_WPR;

    case MTSE_LEOT:
        return ST_LED;
        }

return ST_SUC;
}

/* Read buffer - can return ST_TMK, ST_FMT, or ST_DRV */

void tq_rdbuff_top (UNIT *uptr, t_mtrlnt *tbc)
{
struct tq_req_results *res = (struct tq_req_results *)uptr->results;

sim_tape_rdrecf_a (uptr, res->tqxb, tbc, MT_MAXFR, tq_io_complete);/* read rec fwd */
}

uint32 tq_rdbuff_bottom (UNIT *uptr, t_mtrlnt *tbc)
{
t_stat st;
struct tq_req_results *res = (struct tq_req_results *)uptr->results;

st = res->io_status;                                    /* read rec fwd io status */
if (st == MTSE_TMK) {                                   /* tape mark? */
    uptr->flags = uptr->flags | UNIT_SXC | UNIT_TMK;    /* serious exc */
    uptr->objp = uptr->objp + 1;                        /* update obj cnt */
    return ST_TMK;
    }
if (st != MTSE_OK)                                      /* other error? */
    return tq_map_status (uptr, st);
uptr->flags = uptr->flags & ~UNIT_TMK;                  /* clr tape mark */
uptr->objp = uptr->objp + 1;                            /* upd obj cnt */
return ST_SUC;
}

void tq_rdbufr_top (UNIT *uptr, t_mtrlnt *tbc)
{
struct tq_req_results *res = (struct tq_req_results *)uptr->results;

sim_tape_rdrecr_a (uptr, res->tqxb, tbc, MT_MAXFR, tq_io_complete);        /* read rec rev */
}

uint32 tq_rdbufr_bottom (UNIT *uptr, t_mtrlnt *tbc)
{
t_stat st;
struct tq_req_results *res = (struct tq_req_results *)uptr->results;

st = res->io_status;                                    /* read rec rev io status */
if (st == MTSE_TMK) {                                   /* tape mark? */
    uptr->flags = uptr->flags | UNIT_SXC;               /* serious exc */
    uptr->objp = uptr->objp - 1;                        /* update obj cnt */
    return ST_TMK;
    }
if (st != MTSE_OK)                                      /* other error? */
    return tq_map_status (uptr, st);
uptr->objp = uptr->objp - 1;                            /* upd obj cnt */
return ST_SUC;
}

/* Data transfer error log packet */

t_bool tq_dte (UNIT *uptr, uint16 err)
{
uint16 pkt, tpkt;
uint16 lu;

if ((tq_cflgs & CF_THS) == 0)                           /* logging? */
    return OK;
if (!tq_deqf (&pkt))                                    /* get log pkt */
    return ERR;
tpkt = uptr->cpkt;                                      /* rw pkt */
lu = tq_pkt[tpkt].d[CMD_UN];                            /* unit # */

tq_pkt[pkt].d[ELP_REFL] = tq_pkt[tpkt].d[CMD_REFL];     /* copy cmd ref */
tq_pkt[pkt].d[ELP_REFH] = tq_pkt[tpkt].d[CMD_REFH];     /* copy cmd ref */
tq_pkt[pkt].d[ELP_UN] = lu;                             /* copy unit */
tq_pkt[pkt].d[ELP_SEQ] = 0;                             /* clr seq # */
tq_pkt[pkt].d[DTE_CIDA] = 0;                            /* ctrl ID */
tq_pkt[pkt].d[DTE_CIDB] = 0;
tq_pkt[pkt].d[DTE_CIDC] = 0;
tq_pkt[pkt].d[DTE_CIDD] = (TQ_CLASS << DTE_CIDD_V_CLS) |
    (drv_tab[tq_typ].cmod << DTE_CIDD_V_MOD);
tq_pkt[pkt].d[DTE_VER] = drv_tab[tq_typ].cver;          /* ctrl ver */
tq_pkt[pkt].d[DTE_MLUN] = lu;                           /* MLUN */
tq_pkt[pkt].d[DTE_UIDA] = lu;                           /* unit ID */
tq_pkt[pkt].d[DTE_UIDB] = 0;
tq_pkt[pkt].d[DTE_UIDC] = 0;
tq_pkt[pkt].d[DTE_UIDD] = (UID_TAPE << DTE_UIDD_V_CLS) |
    (drv_tab[tq_typ].umod << DTE_UIDD_V_MOD);
tq_pkt[pkt].d[DTE_UVER] = drv_tab[tq_typ].uver;         /* unit ver */
PUTP32 (pkt, DTE_POSL, uptr->objp);                     /* position */
tq_pkt[pkt].d[DTE_FVER] = drv_tab[tq_typ].fver;         /* fmtr ver */
tq_putr (pkt, FM_TAP, LF_SNR, err, DTE_LNT, UQ_TYP_DAT);
return tq_putpkt (pkt, TRUE);
}

/* Host bus error log packet */

t_bool tq_hbe (UNIT *uptr, uint32 ba)
{
uint16 pkt, tpkt;

if ((tq_cflgs & CF_THS) == 0)                           /* logging? */
    return OK;
if (!tq_deqf (&pkt))                                    /* get log pkt */
    return ERR;
tpkt = uptr->cpkt;                                      /* rw pkt */
tq_pkt[pkt].d[ELP_REFL] = tq_pkt[tpkt].d[CMD_REFL];     /* copy cmd ref */
tq_pkt[pkt].d[ELP_REFH] = tq_pkt[tpkt].d[CMD_REFH];     /* copy cmd ref */
tq_pkt[pkt].d[ELP_UN] = tq_pkt[tpkt].d[CMD_UN];         /* copy unit */
tq_pkt[pkt].d[ELP_SEQ] = 0;                             /* clr seq # */
tq_pkt[pkt].d[HBE_CIDA] = 0;                            /* ctrl ID */
tq_pkt[pkt].d[HBE_CIDB] = 0;
tq_pkt[pkt].d[HBE_CIDC] = 0;
tq_pkt[pkt].d[DTE_CIDD] = (TQ_CLASS << DTE_CIDD_V_CLS) |
    (drv_tab[tq_typ].cmod << DTE_CIDD_V_MOD);
tq_pkt[pkt].d[HBE_VER] = drv_tab[tq_typ].cver;          /* ctrl ver */
tq_pkt[pkt].d[HBE_RSV] = 0;
PUTP32 (pkt, HBE_BADL, ba);                             /* bad addr */
tq_putr (pkt, FM_BAD, LF_SNR, ST_HST | SB_HST_NXM, HBE_LNT, UQ_TYP_DAT);
return tq_putpkt (pkt, TRUE);
}

/* Port last failure error log packet */

t_bool tq_plf (uint32 err)
{
uint16 pkt = 0;

if (!tq_deqf (&pkt))                                    /* get log pkt */
    return ERR;
tq_pkt[pkt].d[ELP_REFL] = tq_pkt[pkt].d[ELP_REFH] = 0;  /* ref = 0 */
tq_pkt[pkt].d[ELP_UN] = tq_pkt[pkt].d[ELP_SEQ] = 0;     /* no unit, seq */
tq_pkt[pkt].d[PLF_CIDA] = 0;                            /* cntl ID */
tq_pkt[pkt].d[PLF_CIDB] = 0;
tq_pkt[pkt].d[PLF_CIDC] = 0;
tq_pkt[pkt].d[PLF_CIDD] = (TQ_CLASS << PLF_CIDD_V_CLS) |
    (drv_tab[tq_typ].cmod << PLF_CIDD_V_MOD);
tq_pkt[pkt].d[PLF_VER] = drv_tab[tq_typ].cver;
tq_pkt[pkt].d[PLF_ERR] = (uint16)err;
tq_putr (pkt, FM_CNT, LF_SNR, ST_CNT, PLF_LNT, UQ_TYP_DAT);
tq_pkt[pkt].d[UQ_HCTC] |= (UQ_CID_DIAG << UQ_HCTC_V_CID);
return tq_putpkt (pkt, TRUE);
}

/* Unit now available attention packet */

t_bool tq_una (UNIT *uptr)
{
uint16 pkt;
uint16 lu;

if (!tq_deqf (&pkt))                                    /* get log pkt */
    return ERR;
lu = (uint16) (uptr->unit_plug);                        /* get unit */
tq_pkt[pkt].d[RSP_REFL] = tq_pkt[pkt].d[RSP_REFH] = 0;  /* ref = 0 */
tq_pkt[pkt].d[RSP_UN] = lu;
tq_pkt[pkt].d[RSP_RSV] = 0;
tq_putr_unit (pkt, uptr, lu, FALSE);                    /* fill unit fields */
tq_putr (pkt, OP_AVA, 0, 0, UNA_LNT, UQ_TYP_SEQ);       /* fill std fields */
return tq_putpkt (pkt, TRUE);
}

/* List handling

   tq_deqf      -       dequeue head of free list (fatal err if none)
   tq_deqh      -       dequeue head of list
   tq_enqh      -       enqueue at head of list
   tq_enqt      -       enqueue at tail of list
*/

t_bool tq_deqf (uint16 *pkt)
{
if (tq_freq == 0)                                       /* no free pkts?? */
    return tq_fatal (PE_NSR);
tq_pbsy = tq_pbsy + 1;                                  /* cnt busy pkts */
*pkt = tq_freq;                                         /* head of list */
tq_freq = tq_pkt[tq_freq].link;                         /* next */
return OK;
}

uint16 tq_deqh (uint16 *lh)
{
int16 ptr = *lh;                                        /* head of list */

if (ptr)                                                /* next */
    *lh = tq_pkt[ptr].link;
return ptr;
}

void tq_enqh (uint16 *lh, int16 pkt)
{
if (pkt == 0)                                           /* any pkt? */
    return;
tq_pkt[pkt].link = *lh;                                 /* link is old lh */
*lh = pkt;                                              /* pkt is new lh */
return;
}

void tq_enqt (uint16 *lh, int16 pkt)
{
if (pkt == 0)                                           /* any pkt? */
    return;
tq_pkt[pkt].link = 0;                                   /* it will be tail */
if (*lh == 0)                                           /* if empty, enqh */
    *lh = pkt;
else {
    uint16 ptr = *lh;                                   /* chase to end */
    while (tq_pkt[ptr].link)
        ptr = tq_pkt[ptr].link;
    tq_pkt[ptr].link = pkt;                             /* enq at tail */
    }
return;
}

/* Packet and descriptor handling */

/* Get packet from command ring */

t_bool tq_getpkt (uint16 *pkt)
{
uint32 addr, desc;

if (!tq_getdesc (&tq_cq, &desc))                        /* get cmd desc */
    return ERR;
if ((desc & UQ_DESC_OWN) == 0) {                        /* none */
    *pkt = 0;                                           /* pkt = 0 */
    return OK;                                          /* no error */
    }
if (!tq_deqf (pkt))                                     /* get cmd pkt */
    return ERR;
tq_hat = 0;                                             /* dsbl hst timer */
addr = desc & UQ_ADDR;                                  /* get Q22 addr */
if (Map_ReadW (addr + UQ_HDR_OFF, TQ_PKT_SIZE, tq_pkt[*pkt].d))
    return tq_fatal (PE_PRE);                           /* read pkt */
return tq_putdesc (&tq_cq, desc);                       /* release desc */
}

/* Put packet to response ring - note the clever hack about credits.
   The controller sends all its credits to the host.  Thereafter, it
   supplies one credit for every response packet sent over.  Simple!
*/

t_bool tq_putpkt (uint16 pkt, t_bool qt)
{
uint32 addr, desc, lnt, cr;
UNIT *up = tq_getucb (tq_pkt[pkt].d[CMD_UN]);

if (pkt == 0)                                           /* any packet? */
    return OK;
if (up)
    sim_debug (DBG_REQ, &tq_dev, "rsp=%04X, sts=%04X, rszl=%04X, obj=%d, pos=%" T_ADDR_FMT "d\n", 
                               tq_pkt[pkt].d[RSP_OPF], tq_pkt[pkt].d[RSP_STS], tq_pkt[pkt].d[RW_RSZL],
                               up->objp, up->pos);
else
    sim_debug (DBG_REQ, &tq_dev, "rsp=%04X, sts=%04X\n", 
                               tq_pkt[pkt].d[RSP_OPF], tq_pkt[pkt].d[RSP_STS]);
if (!tq_getdesc (&tq_rq, &desc))                        /* get rsp desc */
    return ERR;
if ((desc & UQ_DESC_OWN) == 0) {                        /* not valid? */
    if (qt)                                             /* normal? q tail */
        tq_enqt (&tq_rspq, pkt);
    else tq_enqh (&tq_rspq, pkt);                       /* resp q call */
    sim_activate (&tq_unit[TQ_QUEUE], tq_qtime);        /* activate q thrd */
    return OK;
    }
addr = desc & UQ_ADDR;                                  /* get Q22 addr */
lnt = tq_pkt[pkt].d[UQ_HLNT] - UQ_HDR_OFF;              /* size, with hdr */
if ((GETP (pkt, UQ_HCTC, TYP) == UQ_TYP_SEQ) &&         /* seq packet? */
    (GETP (pkt, CMD_OPC, OPC) & OP_END)) {              /* end packet? */
    cr = (tq_credits >= 14)? 14: tq_credits;            /* max 14 credits */
    tq_credits = tq_credits - cr;                       /* decr credits */
    tq_pkt[pkt].d[UQ_HCTC] |= ((cr + 1) << UQ_HCTC_V_CR);
    }
if (Map_WriteW (addr + UQ_HDR_OFF, lnt, tq_pkt[pkt].d))
    return tq_fatal (PE_PWE);                           /* write pkt */
tq_enqh (&tq_freq, pkt);                                /* pkt is free */
tq_pbsy = tq_pbsy - 1;                                  /* decr busy cnt */
if (tq_pbsy == 0)                                       /* idle? strt hst tmr */
    tq_hat = tq_htmo;
return tq_putdesc (&tq_rq, desc);                       /* release desc */
}

/* Get a descriptor from the host */

t_bool tq_getdesc (struct uq_ring *ring, uint32 *desc)
{
uint32 addr = ring->ba + ring->idx;
uint16 d[2];

if (Map_ReadW (addr, 4, d))                             /* fetch desc */
    return tq_fatal (PE_QRE);                           /* err? dead */
*desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
return OK;
}

/* Return a descriptor to the host, clearing owner bit
   If rings transitions from "empty" to "not empty" or "full" to
   "not full", and interrupt bit was set, interrupt the host.
   Actually, test whether previous ring entry was owned by host.
*/

t_bool tq_putdesc (struct uq_ring *ring, uint32 desc)
{
uint32 prvd, newd = (desc & ~UQ_DESC_OWN) | UQ_DESC_F;
uint32 prva, addr = ring->ba + ring->idx;
uint16 d[2];

d[0] = newd & 0xFFFF;                                   /* 32b to 16b */
d[1] = (newd >> 16) & 0xFFFF;
if (Map_WriteW (addr, 4, d))                            /* store desc */
    return tq_fatal (PE_QWE);                           /* err? dead */
if (desc & UQ_DESC_F) {                                 /* was F set? */
    if (ring->lnt <= 4)                                 /* lnt = 1? intr */
        tq_ring_int (ring);
    else {
        prva = ring->ba +                               /* prv desc */
            ((ring->idx - 4) & (ring->lnt - 1));
        if (Map_ReadW (prva, 4, d))                     /* read prv */
            return tq_fatal (PE_QRE);
        prvd = ((uint32) d[0]) | (((uint32) d[1]) << 16);
        if (prvd & UQ_DESC_OWN)
            tq_ring_int (ring);
        }
    }
ring->idx = (ring->idx + 4) & (ring->lnt - 1);
return OK;
}

/* Get unit descriptor for logical unit - trivial now,
   but eventually, hide multiboard complexities here */

UNIT *tq_getucb (uint16 lu)
{
uint32 i;
UNIT *uptr;

for (i = 0; i < tq_dev.numunits - 2; i++) {
    uptr = &tq_dev.units[i];
    if ((lu == uptr->unit_plug) &&
        !(uptr->flags & UNIT_DIS))
        return uptr;
    }
return NULL;
}

/* Hack unit flags */

void tq_setf_unit (int16 pkt, UNIT *uptr)
{
uptr->uf = tq_pkt[pkt].d[ONL_UFL] & UF_MSK;             /* settable flags */
if ((tq_pkt[pkt].d[CMD_MOD] & MD_SWP) &&                /* swre wrp enb? */
    (tq_pkt[pkt].d[ONL_UFL] & UF_WPS))                  /* swre wrp on? */
    uptr->uf = uptr->uf | UF_WPS;                       /* simon says... */
return;
}

/* Hack end flags */

uint32 tq_efl (UNIT *uptr)
{
uint32 t = 0;

if (uptr) {                                             /* any unit? */
    if (uptr->flags & UNIT_POL)                         /* note pos lost */
        t = t | EF_PLS;
    if (uptr->flags & UNIT_SXC)                         /* note ser exc */
        t = t | EF_SXC;
    if (TEST_EOT (uptr))                                /* note EOT */
        t = t | EF_EOT;
    }
return t;
}

/* Unit response fields */

void tq_putr_unit (int16 pkt, UNIT *uptr, uint16 lu, t_bool all)
{
tq_pkt[pkt].d[ONL_MLUN] = (uint16)lu;                   /* multi-unit */
tq_pkt[pkt].d[ONL_UFL] = (uint16)(uptr->uf | TQ_WPH (uptr));/* unit flags */
tq_pkt[pkt].d[ONL_UFL] |= tq_efl (uptr);                /* end flags accordingly */
tq_pkt[pkt].d[ONL_RSVL] = tq_pkt[pkt].d[ONL_RSVH] = 0;  /* reserved */
tq_pkt[pkt].d[ONL_UIDA] = (uint16)lu;                           /* UID low */
tq_pkt[pkt].d[ONL_UIDB] = 0;
tq_pkt[pkt].d[ONL_UIDC] = 0;
tq_pkt[pkt].d[ONL_UIDD] = (UID_TAPE << ONL_UIDD_V_CLS) |
    (drv_tab[tq_typ].umod << ONL_UIDD_V_MOD);           /* UID hi */
PUTP32 (pkt, ONL_MEDL, drv_tab[tq_typ].med);            /* media type */
if (all) {                                              /* if long form */
    tq_pkt[pkt].d[ONL_FMT] = drv_tab[tq_typ].fmt;       /* format */
    tq_pkt[pkt].d[ONL_SPD] = 0;                         /* speed */
    PUTP32 (pkt, ONL_MAXL, TQ_MAXFR);                   /* max xfr */
    tq_pkt[pkt].d[ONL_NREC] = 0;                        /* noise rec */
    tq_pkt[pkt].d[ONL_RSVE] = 0;                        /* reserved */
    }
return;
}

/* UQ_HDR and RSP_OP fields */

void tq_putr (int32 pkt, uint32 cmd, uint32 flg, uint16 sts, uint16 lnt, uint16 typ)
{
tq_pkt[pkt].d[RSP_OPF] = (uint16)((cmd << RSP_OPF_V_OPC) |/* set cmd, flg */
                                  (flg << RSP_OPF_V_FLG));
tq_pkt[pkt].d[RSP_STS] = sts;
tq_pkt[pkt].d[UQ_HLNT] = lnt;                           /* length */
tq_pkt[pkt].d[UQ_HCTC] = (typ << UQ_HCTC_V_TYP) |       /* type, cid */
    (UQ_CID_TMSCP << UQ_HCTC_V_CID);                    /* clr credits */
return;
}

/* Post interrupt during init */

void tq_init_int (void)
{
if ((tq_s1dat & SA_S1H_IE) && tq_dib.vec)
    SET_INT (TQ);
return;
}

/* Post interrupt during putpkt - note that NXMs are ignored! */

void tq_ring_int (struct uq_ring *ring)
{
uint32 iadr = tq_comm + ring->ioff;                     /* addr intr wd */
uint16 flag = 1;

(void)Map_WriteW (iadr, 2, &flag);                      /* write flag */
if (tq_dib.vec)                                         /* if enb, intr */
    SET_INT (TQ);
return;
}

/* Return interrupt vector */

int32 tq_inta (void)
{
return tq_dib.vec;                                      /* prog vector */
}

/* Fatal error */

t_bool tq_fatal (uint32 err)
{
sim_debug (DBG_TRC, &tq_dev, "tq_fatal\n");

sim_debug (DBG_REQ, &tq_dev, "fatal err=%X\n", err);
tq_reset (&tq_dev);                                     /* reset device */
tq_sa = SA_ER | err;                                    /* SA = dead code */
tq_csta = CST_DEAD;                                     /* state = dead */
tq_perr = err;                                          /* save error */
return ERR;
}

/* Device attach */

t_stat tq_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach_ex (uptr, cptr, DBG_TAP, 0);
if (r != SCPE_OK)
    return r;
if (tq_csta == CST_UP)
    uptr->flags = (uptr->flags | UNIT_ATP) & ~(UNIT_SXC | UNIT_POL | UNIT_TMK);
return SCPE_OK;
}

/* Device detach */

t_stat tq_detach (UNIT *uptr)
{
t_stat r;

r = sim_tape_detach (uptr);                             /* detach unit */
if (r != SCPE_OK)
    return r;
uptr->flags = uptr->flags & ~(UNIT_ONL | UNIT_ATP | UNIT_SXC | UNIT_POL | UNIT_TMK);
uptr->uf = 0;                                           /* clr unit flgs */
return SCPE_OK;
} 

/* Device reset */

t_stat tq_reset (DEVICE *dptr)
{
int32 i, j;
UNIT *uptr;
static t_bool plugs_inited = FALSE;

for (i=tq_max_plug=0; i<TQ_NUMDR; i++)
    if (dptr->units[i].unit_plug > tq_max_plug)
        tq_max_plug = (uint16)dptr->units[i].unit_plug;
if (!plugs_inited ) {
    uint32 d;
    char uname[16];

    sprintf (uname, "%s-TIMER", dptr->name);
    sim_set_uname (&dptr->units[4], uname);
    sprintf (uname, "%s-QUESVC", dptr->name);
    sim_set_uname (&dptr->units[5], uname);
    plugs_inited  = TRUE;
    for (d = 0; d < tq_dev.numunits - 2; d++)
        tq_unit[d].unit_plug = d;
    }

tq_csta = CST_S1;                                       /* init stage 1 */
tq_s1dat = 0;                                           /* no S1 data */
tq_dib.vec = 0;                                         /* no vector */
if (UNIBUS)                                             /* Unibus? */
    tq_sa = SA_S1 | SA_S1C_DI | SA_S1C_MP;
else tq_sa = SA_S1 | SA_S1C_Q22 | SA_S1C_DI | SA_S1C_MP; /* init SA val */
tq_cflgs = CF_RPL;                                      /* ctrl flgs off */
tq_htmo = TQ_DHTMO;                                     /* default timeout */
tq_hat = tq_htmo;                                       /* default timer */
tq_cq.ba = tq_cq.lnt = tq_cq.idx = 0;                   /* clr cmd ring */
tq_rq.ba = tq_rq.lnt = tq_rq.idx = 0;                   /* clr rsp ring */
tq_credits = (TQ_NPKTS / 2) - 1;                        /* init credits */
tq_freq = 1;                                            /* init free list */
for (i = 0; i < TQ_NPKTS; i++) {                        /* all pkts free */
    if (i)
        tq_pkt[i].link = (i + 1) & TQ_M_NPKTS;
    else tq_pkt[i].link = 0;
    for (j = 0; j < TQ_PKT_SIZE_W; j++)
        tq_pkt[i].d[j] = 0;
    }
tq_rspq = 0;                                            /* no q'd rsp pkts */
tq_pbsy = 0;                                            /* all pkts free */
tq_pip = 0;                                             /* not polling */
CLR_INT (TQ);                                           /* clr intr req */
for (i = 0; i < TQ_NUMDR + 2; i++) {                    /* init units */
    uptr = tq_dev.units + i;
    sim_cancel (uptr);                                  /* clr activity */
    sim_tape_reset (uptr);
    uptr->flags = uptr->flags &                         /* not online */
        ~(UNIT_ONL|UNIT_ATP|UNIT_SXC|UNIT_POL|UNIT_TMK);
    uptr->uf = 0;                                       /* clr unit flags */
    uptr->cpkt = uptr->pktq = 0;                        /* clr pkt q's */
    if (uptr->results == NULL)
        uptr->results = calloc (1, sizeof (struct tq_req_results));
    if (uptr->results == NULL)
        return SCPE_MEM;
    }
return SCPE_OK;
}

/* Device bootstrap */

#if defined (VM_PDP11)

#define BOOT_START      016000                          /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

/* Data structure definitions */

#define B_CMDINT        (BOOT_START - 01000)            /* cmd int */
#define B_RSPINT        (B_CMDINT + 002)                /* rsp int */
#define B_RING          (B_RSPINT + 002)                /* ring base */
#define B_RSPH          (B_RING + 010)                  /* resp pkt hdr */
#define B_TKRSP         (B_RSPH + 004)                  /* resp pkt */
#define B_CMDH          (B_TKRSP + 060)                 /* cmd pkt hdr */
#define B_TKCMD         (B_CMDH + 004)                  /* cmd pkt */
#define B_UNIT          (B_TKCMD + 004)                 /* unit # */

static const uint16 boot_rom[] = {

    0046525,                        /* ST: "UM" */

    0012706, 0016000,               /*   mov  #st,sp */
    0012700, 0000000,               /*   mov  #unitno,r0 */
    0012701, 0174500,               /*   mov  #174500,r1    ; ip addr */
    0005021,                        /*   clr  (r1)+         ; init */
    0012704, 0004000,               /*   mov  #4000,r4      ; s1 mask */
    0005002,                        /*   clr  r2 */
    0005022,                        /* 10$: clr (r2)+       ; clr up to boot */
    0020237, BOOT_START - 2,        /*   cmp  r2,#st-2 */
    0103774,                        /*   blo  10$ */
    0012705, BOOT_START+0312,       /*   mov  #cmdtbl,r5    ; addr of tbl */

                                    /* Four step init process */

    0005711,                        /* 20$: tst (r1)        ; err? */
    0100001,                        /*   bpl  30$ */
    0000000,                        /*   halt */
    0030411,                        /* 30$: bit r4,(r1)     ; step set? */
    0001773,                        /*   beq  20$           ; wait */
    0012511,                        /*   mov  (r5)+,(r1)    ; send next */
    0006304,                        /*   asl  r4            ; next mask */
    0100370,                        /*   bpl  20$           ; s4 done? */

                                    /* Set up rings, issue ONLINE, REWIND, READ */

    0012737, 0000400, B_CMDH + 2,   /*   mov  #400,cmdh+2   ; VCID = 1 */
    0012737, 0000044, B_CMDH,       /*   mov  #36.,cmdh     ; cmd pkt lnt */
    0010037, B_UNIT,                /*   mov  r0,unit       ; unit # */
    0012737, 0000011, B_TKCMD + 8,  /*   mov  #11,tkcmd+8.  ; online op */
    0012737, 0020000, B_TKCMD + 10, /*   mov  #20000,tkcmd+10. ; clr ser ex */
    0012702, B_RING,                /*   mov  #ring,r2      ; init rings */
    0012722, B_TKRSP,               /*   mov  #tkrsp,(r2)+  ; rsp pkt addr */
    0010203,                        /*   mov  r2,r3         ; save ring+2 */
    0010423,                        /*   mov  r4,(r3)+      ; set TK own */
    0012723, B_TKCMD,               /*   mov  #tkcmd,(r3)+  ; cmd pkt addr */
    0010423,                        /*   mov  r4,(r3)+      ; set TK own */
    0005741,                        /*   tst  -(r1)         ; start poll */
    0005712,                        /* 40$: tst (r2)        ; wait for resp */
    0100776,                        /*   bmi  40$ */
    0105737, B_TKRSP + 10,          /*   tstb tkrsp+10.     ; check stat */
    0001401,                        /*   beq  50$ */
    0000000,                        /*   halt */
    0012703, B_TKCMD + 8,           /* 50$: mov #tkcmd+8.,r3 */
    0012723, 0000045,               /*   mov  #45,(r3)+     ; reposition */
    0012723, 0020002,               /*   mov  #20002,(r3)+  ; rew, clr exc */
    0012723, 0000001,               /*   mov  #1,(r3)+      ; lo rec skp */
    0005023,                        /*   clr  (r3)+         ; hi rec skp */
    0005023,                        /*   clr  (r3)+         ; lo tmk skp */
    0005023,                        /*   clr  (r3)+         ; hi tmk skp */
    0010412,                        /*   mov  r4,(r2)       ; TK own rsp */
    0010437, B_RING + 6,            /*   mov  r4,ring+6     ; TK own cmd */
    0005711,                        /*   tst  (r1)          ; start poll */
    0005712,                        /* 60$: tst (r2)        ; wait for resp */
    0100776,                        /*   bmi  60$ */
    0105737, B_TKRSP + 10,          /*   tstb tkrsp+10.     ; check stat */
    0001401,                        /*   beq  70$ */
    0000000,                        /*   halt */
    0012703, B_TKCMD + 8,           /* 70$: mov #tkcmd+8.,r3 */
    0012723, 0000041,               /*   mov  #41,(r3)+     ; read */
    0012723, 0020000,               /*   mov  #20000,(r3)+  ; clr exc */
    0012723, 0001000,               /*   mov  #512.,(r3)+   ; bc = 512 */
    0005023,                        /*   clr  (r3)+         ; clr args */
    0005023,                        /*   clr  (r3)+         ; ba = 0 */
    0010412,                        /*   mov  r4,(r2)       ; TK own rsp */
    0010437, B_RING + 6,            /*   mov  r4,ring+6     ; TK own cmd */
    0005711,                        /*   tst  (r1)          ; start poll */
    0005712,                        /* 80$: tst (r2)        ; wait for resp */
    0100776,                        /*   bmi  80$ */
    0105737, B_TKRSP + 10,          /*   tstb tkrsp+10.     ; check stat */
    0001401,                        /*   beq  90$ */
    0000000,                        /*   halt */

                                    /* Boot block read in, jump to 0 - leave controller init'd */

    0005003,                        /*   clr  r3 */
    0012704, BOOT_START+020,        /*   mov  #st+020,r4 */
    0005005,                        /*   clr  r5 */
    0005007,                        /*   clr  pc */

    0100000,                        /* cmdtbl: init step 1 */
    B_RING,                         /* ring base */
    0000000,                        /* high ring base */
    0000001                         /* go */
    };

t_stat tq_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
UNIT *uptr = &dptr->units[unitno];

for (i = 0; i < BOOT_LEN; i++)
    WrMemW (BOOT_START + (2 * i), boot_rom[i]);
WrMemW (BOOT_UNIT, (uint16)uptr->unit_plug);
WrMemW (BOOT_CSR, tq_dib.ba & DMASK);
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

t_stat tq_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif

/* Special show commands */

void tq_show_ring (FILE *st, struct uq_ring *rp)
{
uint32 i, desc;
uint16 d[2];

#if defined (VM_PDP11)
fprintf (st, "ring, base = %o, index = %d, length = %d\n",
     rp->ba, rp->idx >> 2, rp->lnt >> 2);
#else
fprintf (st, "ring, base = %x, index = %d, length = %d\n",
     rp->ba, rp->idx >> 2, rp->lnt >> 2);
#endif
for (i = 0; i < (rp->lnt >> 2); i++) {
    if (Map_ReadW (rp->ba + (i << 2), 4, d)) {
        fprintf (st, " %3d: non-existent memory\n", i);
        break;
        }
    desc = ((uint32) d[0]) | (((uint32) d[1]) << 16);
#if defined (VM_PDP11)
    fprintf (st, " %3d: %011o\n", i, desc);
#else
    fprintf (st, " %3d: %08x\n", i, desc);
#endif
    }
return;
}

void tq_show_pkt (FILE *st, int32 pkt)
{
int32 i, j;
uint32 cr = GETP (pkt, UQ_HCTC, CR);
uint32 typ = GETP (pkt, UQ_HCTC, TYP);
uint32 cid = GETP (pkt, UQ_HCTC, CID);

fprintf (st, "packet %d, credits = %d, type = %d, cid = %d\n",
    pkt, cr, typ, cid);
for (i = 0; i < TQ_SH_MAX; i = i + TQ_SH_PPL) {
    fprintf (st, " %2d:", i);
    for (j = i; j < (i + TQ_SH_PPL); j++)
#if defined (VM_PDP11)
    fprintf (st, " %06o", tq_pkt[pkt].d[j]);
#else
    fprintf (st, " %04x", tq_pkt[pkt].d[j]);
#endif
    fprintf (st, "\n");
    }
return;
}

t_stat tq_show_unitq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 pkt, u = uptr - tq_dev.units;

if (tq_csta != CST_UP) {
    fprintf (st, "Controller is not initialized\n");
    return SCPE_OK;
    }
if ((uptr->flags & UNIT_ONL) == 0) {
    if (uptr->flags & UNIT_ATT)
        fprintf (st, "Unit %d is available\n", u);
    else fprintf (st, "Unit %d is offline\n", u);
    return SCPE_OK;
    }
if (uptr->cpkt) {
    fprintf (st, "Unit %d current ", u);
    tq_show_pkt (st, uptr->cpkt);
    if ((pkt = uptr->pktq)) {
        do {
            fprintf (st, "Unit %d queued ", u);
            tq_show_pkt (st, pkt);
            } while ((pkt = tq_pkt[pkt].link));
        }
    }
else fprintf (st, "Unit %d queues are empty\n", u);
return SCPE_OK;
}

t_stat tq_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i, pkt;

if (tq_csta != CST_UP) {
    fprintf (st, "Controller is not initialized\n");
    return SCPE_OK;
    }
if (val & TQ_SH_RI) {
    if (tq_pip)
        fprintf (st, "Polling in progress, host timer = %d\n", tq_hat);
    else fprintf (st, "Host timer = %d\n", tq_hat);
    fprintf (st, "Command ");
    tq_show_ring (st, &tq_cq);
    fprintf (st, "Response ");
    tq_show_ring (st, &tq_rq);
    }
if (val & TQ_SH_FR) {
    if ((pkt = tq_freq)) {
        for (i = 0; pkt != 0; i++, pkt = tq_pkt[pkt].link) {
            if (i == 0)
                fprintf (st, "Free queue = %d", pkt);
            else if ((i % 16) == 0)
                fprintf (st, ",\n %d", pkt);
            else fprintf (st, ", %d", pkt);
            }
        fprintf (st, "\n");
        }
    else fprintf (st, "Free queue is empty\n");
    }
if (val & TQ_SH_RS) {
    if ((pkt = tq_rspq)) {
        do {
            fprintf (st, "Response ");
            tq_show_pkt (st, pkt);
            } while ((pkt = tq_pkt[pkt].link));
        }
    else fprintf (st, "Response queue is empty\n");
    }
if (val & TQ_SH_UN) {
    for (i = 0; i < TQ_NUMDR; i++)
        tq_show_unitq (st, &tq_unit[i], 0, NULL);
    }
return SCPE_OK;
}

/* Set controller type (and capacity for user-defined type) */

t_stat tq_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i, cap;
uint32 max = sim_taddr_64? TQU_EMAXC: TQU_MAXC;
t_stat r;

if ((val < 0) || (val > TQU_TYPE) || ((val != TQU_TYPE) && cptr))
    return SCPE_ARG;
for (i = 0; i < TQ_NUMDR; i++) {
    if (tq_unit[i].flags & UNIT_ATT)
        return SCPE_ALATT;
    }
if (cptr) {
    cap = (uint32) get_uint (cptr, 10, max, &r);
    if ((r != SCPE_OK) || (cap < TQU_MINC))
        return SCPE_ARG;
    drv_tab[TQU_TYPE].cap = ((t_addr) cap) << 20;
    }
tq_typ = val;
for (i = 0; i < TQ_NUMDR; i++)
    tq_unit[i].capac = drv_tab[tq_typ].cap;
return SCPE_OK;
}

/* Show controller type and capacity */

t_stat tq_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s (%dMB)", drv_tab[tq_typ].name, (uint32) (drv_tab[tq_typ].cap >> 20));
return SCPE_OK;
}

/* Show unit plug */

t_stat tq_show_plug (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "UNIT=%d", uptr->unit_plug);
return SCPE_OK;
}

/* Set unit plug */

t_stat tq_set_plug (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 plug;
uint32 i;
t_stat r;
DEVICE *dptr = find_dev_from_unit (uptr);

if (cptr == NULL)
    return sim_messagef (SCPE_ARG, "Must specify UNIT=value\n");
plug = (int32) get_uint (cptr, 10, 0xFFFFFFFF, &r);
if ((r != SCPE_OK) || (plug > 65534))
    return sim_messagef (SCPE_ARG, "Invalid Unit Plug Number: %s\n", cptr);
if (uptr->unit_plug == plug)
    return SCPE_OK;
for (i=0; i < dptr->numunits - 2; i++)
    if (dptr->units[i].unit_plug == plug)
        return sim_messagef (SCPE_ARG, "Unit Plug %d Already In Use on %s\n", plug, sim_uname (&dptr->units[i]));
uptr->unit_plug = plug;
return SCPE_OK;
}

static t_stat tq_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char *devtype = UNIBUS ? "TUK50" : "TQK50";

fprintf (st, "%s (TQ)\n\n", tq_description (dptr));
fprintf (st, "The TQ controller simulates the %s TMSCP disk controller.  TQ options\n", devtype);
fprintf (st, "include the ability to set units write enabled or write locked, and to\n");
fprintf (st, "specify the controller type and tape length:\n");
fprint_set_help (st, dptr);
fprintf (st, "\nThe %s device supports the BOOT command.\n", devtype);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         processed as\n");
fprintf (st, "    not attached  tape not ready\n\n");
fprintf (st, "    end of file   end of medium\n");
fprintf (st, "    OS I/O error  fatal tape error\n\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *tq_description (DEVICE *dptr)
{
return (UNIBUS) ? "TUK50 TMSCP magnetic tape controller" :
                  "TQK50 TMSCP magnetic tape controller";
}
