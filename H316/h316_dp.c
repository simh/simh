/* h316_dp.c: Honeywell 4623, 4651, 4720 disk simulator

   Copyright (c) 2003-2017, Robert M. Supnik

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

   dp           4623 disk subsystem
                4651 disk subsystem
                4720 disk subsystem

   13-Mar-17    RMS     Annotated intentional fall through in switch
   03-Jul-13    RLA     compatibility changes for extended interrupts
   19-Mar-12    RMS     Fixed declaration of chan_req (Mark Pizzolato)
   04-Sep-05    RMS     Fixed missing return (Peter Schorn)
   15-Jul-05    RMS     Fixed bug in attach routine
   01-Dec-04    RMS     Fixed bug in skip on !seeking

   The Honeywell disks have the unique characteristic of supporting variable
   formatting, on a per track basis.  To accomodate this, each track is
   simulated as 2048 words, divided into records.  (2048 words accomodates
   the largest record of 1891 + 8 overhead words.)  A record is structured
   as follows:

   word 0       record length n (0 = end of track)
   word 1       record address (16b, uninterpreted by the simulator)
   word 2       record extension (0 to 4 words of permitted 'overwrite')
   word 3       first data word
        :
   word 3+n-1   last data word
   word 3+n     checksum word
   word 4+n     first extension word
        :
   word 7+n     fourth extension word
   word 8+n     start of next record

   Formatting is done in two ways.  The SET DPn FORMAT=k command formats
   unit n with k records per track, each record having the maximum allowable
   record size and a standard record address; or with k words per record.
   Alternately, the simulator allows programmatic formating.  When a track
   is formated, the program supplies record parameters as follows:

   word 0       record address
   words 1-n    data words
   word n+1     gap size in bits

   To make this work, the simulator tracks the consumption of bits in the
   track, against the track capacity in bits.  Bit consumption is:

   16.5 * 16    for overhead (including address and checksum)
   n * 16       for data
   'gap'        for gap, which must be at least 5% of the record length
*/

#include "h316_defs.h"
#include <math.h>

#define FNC             u3                              /* saved function */
#define CYL             u4                              /* actual cylinder */
#define DP_TRKLEN       2048                            /* track length, words */
#define DP_NUMDRV       8                               /* max # drives */
#define DP_NUMTYP       3                               /* # controller types */

/* Record format */

#define REC_LNT         0                               /* length (unextended) */
#define REC_ADDR        1                               /* address */
#define REC_EXT         2                               /* extension (0-4) */
#define REC_DATA        3                               /* start of data */
#define REC_OVHD        8                               /* overhead words */
#define REC_MAXEXT      4                               /* maximum extension */
#define REC_OVHD_WRDS   16.5                            /* 16.5 words */
#define REC_OVHD_BITS   ((16 * 16) + 8)

/* Status word, ^ = dynamic */

#define STA_BUSY        0100000                         /* busy */
#define STA_RDY         0040000                         /* ready */
#define STA_ADRER       0020000                         /* address error */
#define STA_FMTER       0010000                         /* format error */
#define STA_HNLER       0004000                         /* heads not loaded (NI) */
#define STA_OFLER       0002000                         /* offline */
#define STA_SEKER       0001000                         /* seek error */
#define STA_MBZ         0000700
#define STA_WPRER       0000040                         /* write prot error */
#define STA_UNSER       0000020                         /* unsafe */
#define STA_CSMER       0000010                         /* checksum error */
#define STA_DTRER       0000004                         /* transfer rate error */
#define STA_ANYER       0000002                         /* any error^ */
#define STA_EOR         0000001                         /* end of record */
#define STA_ALLERR      (STA_ADRER|STA_FMTER|STA_HNLER|STA_OFLER|STA_SEKER|\
                         STA_WPRER|STA_UNSER|STA_DTRER)

/* Functions */

#define FNC_SK0         0000                            /* recalibrate */
#define FNC_SEEK        0001                            /* seek */
#define FNC_RCA         0002                            /* read current */
#define FNC_UNL         0004                            /* unload */
#define FNC_FMT         0005                            /* format */
#define FNC_RW          0006                            /* read/write */
#define FNC_STOP        0010                            /* stop format */
#define FNC_RDS         0011                            /* read status */
#define FNC_DMA         0013                            /* DMA/DMC */
#define FNC_AKI         0014                            /* acknowledge intr */
#define FNC_IOBUS       0017                            /* IO bus */
#define FNC_2ND         0020                            /* second state */
#define FNC_3RD         0040                            /* third state */
#define FNC_4TH         0060                            /* fourth state */
#define FNC_5TH         0100                            /* fifth state */

/* Command word 1 */

#define CW1_RW          0100000                         /* read/write */
#define CW1_DIR         0000400                         /* seek direction */
#define CW1_V_UNIT      11                              /* unit select */
#define CW1_V_HEAD      6                               /* head select */
#define CW1_V_OFFS      0                               /* seek offset */
#define CW1_GETUNIT(x)  (((x) >> CW1_V_UNIT) & dp_tab[dp_ctype].umsk)
#define CW1_GETHEAD(x)  (((x) >> CW1_V_HEAD) & dp_tab[dp_ctype].hmsk)
#define CW1_GETOFFS(x)  (((x) >> CW1_V_OFFS) & dp_tab[dp_ctype].cmsk)

/* OTA states */

#define OTA_NOP         0                               /* normal */
#define OTA_CW1         1                               /* expecting CW1 */
#define OTA_CW2         2                               /* expecting CW2 */

/* Transfer state */

#define XIP_UMSK        007                             /* unit mask */
#define XIP_SCHED       010                             /* scheduled */
#define XIP_WRT         020                             /* write */
#define XIP_FMT         040                             /* format */

/* The H316/516 disk emulator supports three disk controllers:

   controller   units   cylinders       surfaces        data words per track

   4651         4       203             2               1908.25
   4623         8       203             10              1816.5
   4720         8       203             20              1908.25

   Disk types may not be intermixed on the same controller.
*/

#define TYPE_4651       0
#define UNIT_4651       4
#define CYL_4651        203
#define SURF_4651       2
#define WRDS_4651       1908.25
#define UMSK_4651       0003
#define HMSK_4651       0001
#define CMSK_4651       0377
#define CAP_4651        (CYL_4651*SURF_4651*DP_TRKLEN)

#define TYPE_4623       1
#define UNIT_4623       8
#define CYL_4623        203
#define SURF_4623       10
#define WRDS_4623       1816.5
#define UMSK_4623       0007
#define HMSK_4623       0017
#define CMSK_4623       0377
#define CAP_4623        (CYL_4623*SURF_4623*DP_TRKLEN)

#define TYPE_4720       2
#define UNIT_4720       8
#define CYL_4720        203
#define SURF_4720       20
#define WRDS_4720       1908.25
#define UMSK_4720       0007
#define HMSK_4720       0037
#define CMSK_4720       0377
#define CAP_4720        (CYL_4720*SURF_4720*DP_TRKLEN)

struct drvtyp {
    const char          *name;
    uint32              numu;
    uint32              cyl;
    uint32              surf;
    uint32              cap;
    uint32              umsk;
    uint32              hmsk;
    uint32              cmsk;
    float               wrds;
    };

#define DP_DRV(d) \
    #d, \
    UNIT_##d, CYL_##d, SURF_##d, CAP_##d, \
    UMSK_##d, HMSK_##d, CMSK_##d, WRDS_##d

static struct drvtyp dp_tab[] = {
    { DP_DRV (4651) },
    { DP_DRV (4623) },
    { DP_DRV (4720) }
    };

extern int32 dev_int, dev_enb;
extern uint32 chan_req;
extern int32 stop_inst;
extern uint32 dma_ad[DMA_MAX];

uint32 dp_cw1 = 0;                                      /* cmd word 1 */
uint32 dp_cw2 = 0;                                      /* cmd word 2 */
uint32 dp_fnc = 0;                                      /* saved function */
uint32 dp_buf = 0;                                      /* buffer */
uint32 dp_otas = 0;                                     /* state */
uint32 dp_sta = 0;                                      /* status */
uint32 dp_defint = 0;                                   /* deferred seek int */
uint32 dp_ctype = TYPE_4651;                            /* controller type */
uint32 dp_dma = 0;                                      /* DMA/DMC */
uint32 dp_eor = 0;                                      /* end of range */
uint32 dp_xip = 0;                                      /* transfer in prog */
uint32 dp_csum = 0;                                     /* parity checksum */
uint32 dp_rptr = 0;                                     /* start of record */
uint32 dp_wptr = 0;                                     /* word ptr in record */
uint32 dp_bctr = 0;                                     /* format bit cntr */
uint32 dp_gap = 0;                                      /* format gap size */
uint32 dp_stopioe = 1;                                  /* stop on error */
int32 dp_stime = 1000;                                  /* seek per cylinder */
int32 dp_xtime = 10;                                    /* xfer per word */
int32 dp_btime = 30;                                    /* busy time */
uint16 dpxb[DP_TRKLEN];                                 /* track buffer */

int32 dpio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat dp_svc (UNIT *uptr);
t_stat dp_reset (DEVICE *dptr);
t_stat dp_attach (UNIT *uptr, CONST char *cptr);
t_stat dp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dp_go (uint32 dma);
t_stat dp_go1 (uint32 dat);
t_stat dp_go2 (uint32 dat);
t_stat dp_rdtrk (UNIT *uptr, uint16 *buf, uint32 cyl, uint32 hd);
t_stat dp_wrtrk (UNIT *uptr, uint16 *buf, uint32 cyl, uint32 hd);
t_bool dp_findrec (uint32 addr);
t_stat dp_wrwd (UNIT *uptr, uint32 dat);
t_stat dp_wrdone (UNIT *uptr, uint32 flg);
t_stat dp_done (uint32 req, uint32 f);
t_stat dp_setformat (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dp_showformat (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* DP data structures

   dp_dev       DP device descriptor
   dp_unit      DP unit list
   dp_reg       DP register list
   dp_mod       DP modifier list
*/

DIB dp_dib = { DP, 1, DMC1, IOBUS, INT_V_DP, INT_V_NONE, &dpio, 0 };

UNIT dp_unit[] = {
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
            UNIT_ROABLE, CAP_4651) }
    };

REG dp_reg[] = {
    { ORDATA (STA, dp_sta, 16) },
    { ORDATA (BUF, dp_buf, 16) },
    { ORDATA (FNC, dp_fnc, 4) },
    { ORDATA (CW1, dp_cw1, 16) },
    { ORDATA (CW2, dp_cw2, 16) },
    { ORDATA (CSUM, dp_csum, 16) },
    { FLDATA (BUSY, dp_sta, 15) },
    { FLDATA (RDY, dp_sta, 14) },
    { FLDATA (EOR, dp_eor, 0) },
    { FLDATA (DEFINT, dp_defint, 0) },
    { FLDATA (INTREQ, dev_int, INT_V_DP) },
    { FLDATA (ENABLE, dev_enb, INT_V_DP) },
    { BRDATA (TBUF, dpxb, 8, 16, DP_TRKLEN) },
    { ORDATA (RPTR, dp_rptr, 11), REG_RO },
    { ORDATA (WPTR, dp_wptr, 11), REG_RO },
    { ORDATA (BCTR, dp_bctr, 15), REG_RO },
    { ORDATA (GAP, dp_gap, 16), REG_RO },
    { DRDATA (STIME, dp_stime, 24), REG_NZ + PV_LEFT },
    { DRDATA (XTIME, dp_xtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (BTIME, dp_btime, 24), REG_NZ + PV_LEFT },
    { FLDATA (CTYPE, dp_ctype, 0), REG_HRO },
    { URDATA (UCYL, dp_unit[0].CYL, 10, 8, 0,
              DP_NUMDRV, PV_LEFT | REG_HRO) },
    { URDATA (UFNC, dp_unit[0].FNC, 8, 7, 0,
              DP_NUMDRV, REG_HRO) },
    { URDATA (CAPAC, dp_unit[0].capac, 10, T_ADDR_W, 0,
              DP_NUMDRV, PV_LEFT | REG_HRO) },
    { ORDATA (OTAS, dp_otas, 2), REG_HRO },
    { ORDATA (XIP, dp_xip, 6), REG_HRO },
    { ORDATA (CHAN, dp_dib.chan, 5), REG_HRO },
    { FLDATA (STOP_IOE, dp_stopioe, 0) },
    { NULL }
    };

MTAB dp_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { MTAB_XTD | MTAB_VDV, TYPE_4623, NULL, "4623",
      &dp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, TYPE_4651, NULL, "4651",
      &dp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, TYPE_4720, NULL, "4720",
      &dp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &dp_showtype, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMC",
      &io_set_dmc, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "DMA",
      &io_set_dma, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", NULL,
      NULL, &io_show_chan, NULL },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "FORMAT", "FORMAT",
      &dp_setformat, &dp_showformat, NULL },
    { 0 }
    };

DEVICE dp_dev = {
    "DP", dp_unit, dp_reg, dp_mod,
    DP_NUMDRV, 8, 24, 1, 8, 16,
    NULL, NULL, &dp_reset,
    NULL, &dp_attach, NULL,
    &dp_dib, DEV_DISABLE
    };

/* IOT routines */

int32 dpio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
int32 ch = dp_dib.chan - 1;                             /* DMA/DMC chan */
int32 u;
UNIT *uptr;

switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        switch (fnc) {                                  /* case on function */

        case FNC_SK0: case FNC_SEEK: case FNC_RCA:      /* data transfer */
        case FNC_UNL: case FNC_FMT: case FNC_RW:
            dp_go (fnc);                                /* if !busy, start */
            break;

        case FNC_STOP:                                  /* stop transfer */
            if (dp_xip) {                               /* transfer in prog? */
                uptr = dp_dev.units + (dp_xip & XIP_UMSK);      /* get unit */
                sim_cancel (uptr);                      /* stop operation */
                if (dp_xip & (XIP_WRT|XIP_FMT))         /* write or format? */
                    dp_wrdone (uptr,                    /* write track */
                        ((dp_xip & XIP_FMT) &&          /* check fmt state */
                        (uptr->FNC != (FNC_FMT|FNC_2ND)))?
                        STA_DTRER: 0);
                else dp_done (1, dp_csum? STA_CSMER: 0);/* no, just clr busy */
                dp_xip = 0;                             /* clear flag */
                }
            dp_otas = OTA_NOP;                          /* clear state */
            dp_sta = dp_sta & ~STA_BUSY;                /* clear busy */
            break;      

        case FNC_RDS:                                   /* read status */
            if (dp_sta & STA_BUSY)                      /* ignore if busy */
                return dat;
            dp_sta = (dp_sta | STA_RDY) & ~(STA_MBZ | STA_ANYER);
            if (dp_sta & STA_ALLERR) dp_sta = dp_sta | STA_ANYER;
            dp_buf = dp_sta;
            if (dp_dma && Q_DMA (ch))                   /* DMA? set chan req */
                SET_CH_REQ (ch);
            break;

        case FNC_DMA:                                   /* set DMA/DMC */
            dp_dma = 1;
            break;

        case FNC_IOBUS:                                 /* set IO bus */
            dp_dma = 0;
            break;

        case FNC_AKI:                                   /* ack intr */
            CLR_INT (INT_DP);
            break;

        default:                                        /* undefined */
            return IOBADFNC (dat);
            }
        break;

    case ioINA:                                         /* INA */
        if (fnc)                                        /* fnc 0 only */
            return IOBADFNC (dat);
        if (dp_sta & STA_RDY) {                         /* ready? */
            dp_sta = dp_sta & ~STA_RDY;                 /* clear ready */
            return IOSKIP (dat | dp_buf);               /* ret buf, skip */
            }
        break;

    case ioOTA:                                         /* OTA */
        if (fnc)                                        /* fnc 0 only */
            return IOBADFNC (dat);
        if (dp_sta & STA_RDY) {                         /* ready? */
            dp_sta = dp_sta & ~STA_RDY;                 /* clear ready */
            dp_buf = dat;                               /* store buf */
            if (dp_otas == OTA_CW1)                     /* expecting CW1? */
                dp_go1 (dat);
            else if (dp_otas == OTA_CW2)                /* expecting CW2? */
                dp_go2 (dat);
            return IOSKIP (dat);
            }
        break;

    case ioSKS:                                         /* SKS */
        u = 7;                                          /* assume unit 7 */
        switch (fnc) {

        case 000:                                       /* ready */
            if (dp_sta & STA_RDY)
                return IOSKIP (dat);
            break;

        case 001:                                       /* !interrupting */
            if (!TST_INTREQ (INT_DP))
                return IOSKIP (dat);
            break;

        case 002:                                       /* operational */
            if (!(dp_sta & (STA_BUSY | STA_ALLERR)))
                return IOSKIP (dat);
            break;

        case 003:                                       /* !error */
            if (!(dp_sta & STA_ALLERR))
                return IOSKIP (dat);
            break;

        case 004:                                       /* !busy */
            if (!(dp_sta & STA_BUSY))
                return IOSKIP (dat);
            break;

        case 011: case 012: case 013:                   /* !not seeking 0-6 */
        case 014: case 015: case 016: case 017:
            u = fnc - 011;                              /* set u */
            /* fall through */
        case 007:                                       /* !not seeking 7 */
            if (!sim_is_active (&dp_unit[u]) ||         /* quiescent? */
                (dp_unit[u].FNC != (FNC_SEEK | FNC_2ND)))
                return IOSKIP (dat);                    /* seeking sets late */
            break;
            }
        break;

    case ioEND:                                         /* end of range */
        dp_eor = 1;                                     /* transfer done */
        break;
        }

return dat;
}

/* Start new operation - recal, seek, read address, format, read/write */

t_stat dp_go (uint32 fnc)
{
int32 ch = dp_dib.chan - 1;                             /* DMA/DMC chan */

if (dp_sta & STA_BUSY)                                  /* ignore if busy */
    return SCPE_OK;
dp_fnc = fnc;                                           /* save function */
dp_xip = 0;                                             /* transfer not started */
dp_eor = 0;                                             /* not end of range */
dp_csum = 0;                                            /* init checksum */
dp_otas = OTA_CW1;                                      /* expect CW1 */
dp_sta = (dp_sta | STA_BUSY | STA_RDY) & ~(STA_ALLERR | STA_EOR);
if (dp_dma && Q_DMA (ch)) {                             /* DMA and DMA channel? */
    SET_CH_REQ (ch);                                    /* set channel request */
    dma_ad[ch] = dma_ad[ch] & ~DMA_IN;                  /* force output */
    }
return SCPE_OK;
}

/* Process command word 1 - recal, seek, read address, format, read/write */

t_stat dp_go1 (uint32 dat)
{
int32 ch = dp_dib.chan - 1;                             /* DMA/DMC chan */
uint32 u = CW1_GETUNIT (dat);
UNIT *uptr = dp_dev.units + u;

dp_cw1 = dat;                                           /* store CW1 */
dp_otas = OTA_NOP;                                      /* assume no CW2 */
uptr->FNC = dp_fnc;
if (sim_is_active (uptr))                               /* still seeking? */
    return dp_done (1, STA_UNSER);                      /* unsafe */
if (!(uptr->flags & UNIT_ATT))                          /* not attached? */
    return dp_done (1, STA_OFLER);                      /* offline */

switch (dp_fnc) {                                       /* case on function */

    case FNC_SEEK:                                      /* seek */
    case FNC_SK0:                                       /* recalibrate */
    case FNC_UNL:                                       /* unload */
        sim_activate (uptr, dp_btime);                  /* quick timeout */
        break;

    case FNC_FMT:                                       /* format */
        if (uptr->flags & UNIT_WPRT)                    /* write protect? */
            return dp_done (1, STA_WPRER);              /* stop now */
    case FNC_RCA:                                       /* read current addr */
        dp_xip = u | XIP_SCHED;                         /* operation started */
        sim_activate (uptr, dp_xtime * 10);             /* rotation timeout */
        break;

    case FNC_RW:                                        /* read/write */
        dp_otas = OTA_CW2;                              /* expect CW2 */
        dp_sta = dp_sta | STA_RDY;                      /* set ready */
        if (dp_dma && Q_DMA (ch))                       /* DMA? set chan request */
            SET_CH_REQ (ch);
        break;
        }

return SCPE_OK;
}

/* Process command word 2 - read/write only */

t_stat dp_go2 (uint32 dat)
{
uint32 u = CW1_GETUNIT (dp_cw1);
UNIT *uptr = dp_dev.units + u;

dp_cw2 = dat;                                           /* store CW2 */
dp_otas = OTA_NOP;                                      /* normal state */
sim_activate (uptr, dp_xtime * 10);                     /* rotation timeout */
dp_xip = u | XIP_SCHED;                                 /* operation started */
return SCPE_OK;
}

/* Unit service */

t_stat dp_svc (UNIT *uptr)
{
int32 dcyl = 0;                                         /* assume recalibrate */
int32 ch = dp_dib.chan - 1;                             /* DMA/DMC chan */
uint32 h = CW1_GETHEAD (dp_cw1);                        /* head */
int32 st;
uint32 i, offs, lnt, ming, tpos;
t_stat r;

if (!(uptr->flags & UNIT_ATT)) {                        /* not attached? */
    dp_done (1, STA_OFLER);                             /* offline */
    return IORETURN (dp_stopioe, SCPE_UNATT);
    }

switch (uptr->FNC) {                                    /* case on function */

    case FNC_SEEK:                                      /* seek, need cyl */
        offs = CW1_GETOFFS (dp_cw1);                    /* get offset */
        if (dp_cw1 & CW1_DIR)                           /* get desired cyl */
            dcyl = uptr->CYL - offs;
        else dcyl = uptr->CYL + offs;
        if ((offs == 0) ||
            (dcyl < 0) ||
            (dcyl >= (int32) dp_tab[dp_ctype].cyl))     
            return dp_done (1, STA_SEKER);              /* bad seek? */

    case FNC_SK0:                                       /* recalibrate */
        dp_sta = dp_sta & ~STA_BUSY;                    /* clear busy */
        uptr->FNC = FNC_SEEK | FNC_2ND;                 /* next state */
        st = (abs (dcyl - uptr->CYL)) * dp_stime;       /* schedule seek */
        if (st == 0)
            st = dp_stime;
        uptr->CYL = dcyl;                               /* put on cylinder */
        sim_activate (uptr, st);
        return SCPE_OK;

    case FNC_SEEK | FNC_2ND:                            /* seek, 2nd state */
        if (dp_sta & STA_BUSY)                          /* busy? queue intr */
            dp_defint = 1;
        else SET_INT (INT_DP);                          /* no, req intr */
        return SCPE_OK;

    case FNC_UNL:                                       /* unload */
        detach_unit (uptr);                             /* detach unit */
        return dp_done (0, 0);                          /* clear busy, no intr */

    case FNC_RCA:                                       /* read current addr */
        if (h >= dp_tab[dp_ctype].surf)                 /* invalid head? */
            return dp_done (1, STA_ADRER);              /* error */
        if ((r = dp_rdtrk (uptr, dpxb, uptr->CYL, h)))  /* get track; error? */
            return r;
        dp_rptr = 0;                                    /* init rec ptr */
        if (dpxb[dp_rptr + REC_LNT] == 0)               /* unformated? */
            return dp_done (1, STA_ADRER);              /* error */
        tpos = (uint32) (fmod (sim_gtime () / (double) dp_xtime, DP_TRKLEN));
        do {                                            /* scan down track */
            dp_buf = dpxb[dp_rptr + REC_ADDR];          /* get rec addr */
            dp_rptr = dp_rptr + dpxb[dp_rptr + REC_LNT] + REC_OVHD;
            } while ((dp_rptr < tpos) && (dpxb[dp_rptr + REC_LNT] != 0));
        if (dp_dma) {                                   /* DMA/DMC? */
            if (Q_DMA (ch))                             /* DMA? */
                dma_ad[ch] = dma_ad[ch] | DMA_IN;       /* force input */
            SET_CH_REQ (ch);                            /* request chan */
            }
        return dp_done (1, STA_RDY);                    /* clr busy, set rdy */

/* Formating takes place in five states:

   init - clear track buffer, start at first record
   address - store address word
   data - store data word(s) until end of range
   pause - wait for gap word or stop command
   gap  - validate gap word, advance to next record

   Note that formating is stopped externally by an OCP command; the
   track buffer is flushed at that point.  If the stop does not occur
   in the proper state (gap word received), a format error occurs.
*/

    case FNC_FMT:                                       /* format */
        for (i = 0; i < DP_TRKLEN; i++)                 /* clear track */
            dpxb[i] = 0;
        dp_xip = dp_xip | XIP_FMT;                      /* format in progress */
        dp_rptr = 0;                                    /* init record ptr */
        dp_gap = 0;                                     /* no gap before first */
        dp_bctr = (uint32) (16.0 * dp_tab[dp_ctype].wrds); /* init bit cntr */
        uptr->FNC = uptr->FNC | FNC_2ND;                /* address state */
        break;                                          /* set up next word */

    case FNC_FMT | FNC_2ND:                             /* format, address word */
        dp_wptr = 0;                                    /* clear word ptr */
        if (dp_bctr < (dp_gap + REC_OVHD_BITS + 16))    /* room for gap, record? */
            return dp_wrdone (uptr, STA_FMTER);         /* no, format error */
        dp_bctr = dp_bctr - dp_gap - REC_OVHD_BITS;     /* charge for gap, ovhd */
        dpxb[dp_rptr + REC_ADDR] = dp_buf;              /* store address */
        uptr->FNC = FNC_FMT | FNC_3RD;                  /* data state */
        if (dp_eor) {                                   /* record done? */
            dp_eor = 0;                                 /* clear for restart */
            if (dp_dma)                                 /* DMA/DMC? intr */
                SET_INT (INT_DP);
            }
        break;                                          /* set up next word */

    case FNC_FMT | FNC_3RD:                             /* format, data word */
        if (dp_sta & STA_RDY)                           /* timing failure? */
            return dp_wrdone (uptr, STA_DTRER);         /* write trk, err */
        else {                                          /* no, have word */
            if (dp_bctr < 16)                           /* room for it? */
                return dp_wrdone (uptr, STA_FMTER);     /* no, error */
            dp_bctr = dp_bctr - 16;                     /* charge for word */
            dp_csum = dp_csum ^ dp_buf;                 /* update checksum */
            dpxb[dp_rptr + REC_DATA + dp_wptr] = dp_buf;/* store word */
            dpxb[dp_rptr + REC_LNT]++;                  /* incr rec lnt */
            dp_wptr++;                                  /* incr word ptr */
            }
        if (dp_eor) {                                   /* record done? */
            dp_eor = 0;                                 /* clear for restart */
            if (dp_dma)                                 /* DMA/DMC? intr */
                SET_INT (INT_DP);
            dpxb[dp_rptr + REC_DATA + dp_wptr] = dp_csum; /* store checksum */
            uptr->FNC = uptr->FNC | FNC_4TH;            /* pause state */
            sim_activate (uptr, 5 * dp_xtime);          /* schedule pause */
            return SCPE_OK;                             /* don't request word */
            }
        break;                                          /* set up next word */

    case FNC_FMT | FNC_4TH:                             /* format, pause */
        uptr->FNC = FNC_FMT | FNC_5TH;                  /* gap state */
        break;                                          /* request word */

    case FNC_FMT | FNC_5TH:                             /* format, gap word */
        ming = ((16 * dp_wptr) + REC_OVHD_BITS) / 20;   /* min 5% gap */
        if (dp_buf < ming)                              /* too small? */
            return dp_wrdone (uptr, STA_FMTER);         /* yes, format error */
        dp_rptr = dp_rptr + dp_wptr + REC_OVHD;         /* next record */
        uptr->FNC = FNC_FMT | FNC_2ND;                  /* address state */
        if (dp_eor) {                                   /* record done? */
            dp_eor = 0;                                 /* clear for restart */
            if (dp_dma) SET_INT (INT_DP);               /* DMA/DMC? intr */
            }
        dp_gap = dp_buf;                                /* save gap */
        dp_csum = 0;                                    /* clear checksum */
        break;                                          /* set up next word */

/* Read and write take place in two states:

   init - read track into buffer, find record, validate parameters
   data - (read) fetch data from buffer, stop on end of range
        - (write) write data into buffer, flush on end of range
*/

    case FNC_RW:                                        /* read/write */
        if (h >= dp_tab[dp_ctype].surf)                 /* invalid head? */
            return dp_done (1, STA_ADRER);              /* error */
        if ((r = dp_rdtrk (uptr, dpxb, uptr->CYL, h)))  /* get track; error? */
            return r;
        if (!dp_findrec (dp_cw2))                       /* find rec; error? */
            return dp_done (1, STA_ADRER);              /* address error */
        if ((dpxb[dp_rptr + REC_LNT] >= (DP_TRKLEN - dp_rptr - REC_OVHD)) ||
            (dpxb[dp_rptr + REC_EXT] >= REC_MAXEXT)) {  /* bad lnt or ext? */
            dp_done (1, STA_UNSER);                     /* stop simulation */
            return STOP_DPFMT;                          /* bad format */
            }
        uptr->FNC = uptr->FNC | FNC_2ND;                /* next state */
        if (dp_cw1 & CW1_RW) {                          /* write? */
            if (uptr->flags & UNIT_WPRT)                /* write protect? */
                return dp_done (1, STA_WPRER);          /* error */
            dp_xip = dp_xip | XIP_WRT;                  /* write in progress */
            dp_sta = dp_sta | STA_RDY;                  /* set ready */
            if (dp_dma)                                 /* if DMA/DMC, req chan */
                SET_CH_REQ (ch);
            }
        else if (Q_DMA (ch))                            /* read; DMA? */
            dma_ad[ch] = dma_ad[ch] | DMA_IN;           /* force input */
        sim_activate (uptr, dp_xtime);                  /* schedule word */
        dp_wptr = 0;                                    /* init word pointer */
        return SCPE_OK;

    case FNC_RW | FNC_2ND:                              /* read/write, word */
        if (dp_cw1 & CW1_RW) {                          /* write? */
            if (dp_sta & STA_RDY)                       /* timing failure? */
                return dp_wrdone (uptr, STA_DTRER);     /* yes, error */
            if ((r = dp_wrwd (uptr, dp_buf)))           /* wr word, error? */
                return r;
            if (dp_eor) {                               /* transfer done? */
                dpxb[dp_rptr + REC_DATA + dp_wptr] = dp_csum;
                return dp_wrdone (uptr, 0);             /* clear busy, intr req */
                }
            }
        else {                                          /* read? */
            lnt = dpxb[dp_rptr + REC_LNT] + dpxb[dp_rptr + REC_EXT];
            dp_buf = dpxb[dp_rptr + REC_DATA + dp_wptr];/* current word */
            dp_csum = dp_csum ^ dp_buf;                 /* xor to csum */
            if ((dp_wptr > lnt) || dp_eor)              /* transfer done? */
                return dp_done (1,
                    (dp_csum? STA_CSMER: 0) | ((dp_wptr >= lnt)? STA_EOR: 0));
            if (dp_sta & STA_RDY)                       /* data buf full? */
                return dp_done (1, STA_DTRER);          /* no, underrun */
            dp_wptr++;                                  /* next word */
            }
        break;

    default:
        return SCPE_IERR;
        }                                               /* end case */

dp_sta = dp_sta | STA_RDY;                              /* set ready */
if (dp_dma)                                             /* if DMA/DMC, req chan */
    SET_CH_REQ (ch);
sim_activate (uptr, dp_xtime);                          /* schedule word */
return SCPE_OK;
}

/* Read track */

t_stat dp_rdtrk (UNIT *uptr, uint16 *buf, uint32 c, uint32 h)
{
uint32 da = ((c * dp_tab[dp_ctype].surf) + h) * DP_TRKLEN;
int32 l;

(void)fseek (uptr->fileref, da * sizeof (uint16), SEEK_SET);
l = fxread (buf, sizeof (uint16), DP_TRKLEN, uptr->fileref);
for ( ; l < DP_TRKLEN; l++)
    buf[l] = 0;
if (ferror (uptr->fileref)) {
    sim_perror ("DP I/O error");
    clearerr (uptr->fileref);
    dp_done (1, STA_UNSER);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Write track */

t_stat dp_wrtrk (UNIT *uptr, uint16 *buf, uint32 c, uint32 h)
{
uint32 da = ((c * dp_tab[dp_ctype].surf) + h) * DP_TRKLEN;

(void)fseek (uptr->fileref, da * sizeof (uint16), SEEK_SET);
fxwrite (buf, sizeof (uint16), DP_TRKLEN, uptr->fileref);
if (ferror (uptr->fileref)) {
    sim_perror ("DP I/O error");
    clearerr (uptr->fileref);
    dp_done (1, STA_UNSER);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Find record; true if found, false if not found */

t_bool dp_findrec (uint32 addr)
{
dp_rptr = 0;

do {
    if (dpxb[dp_rptr + REC_LNT] == 0)
        return FALSE;
    if (dpxb[dp_rptr + REC_LNT] >= DP_TRKLEN)
        return TRUE;
    if (dpxb[dp_rptr + REC_ADDR] == addr)
        return TRUE;
    dp_rptr = dp_rptr + dpxb[dp_rptr + REC_LNT] + REC_OVHD;
    } while (dp_rptr < DP_TRKLEN);
return FALSE;
}

/* Write next word to track buffer; return TRUE if ok, FALSE if next record trashed */

t_stat dp_wrwd (UNIT *uptr, uint32 dat)
{
uint32 lnt = dpxb[dp_rptr + REC_LNT];
t_stat r;

dp_csum = dp_csum ^ dat;
if (dp_wptr < lnt) {
    dpxb[dp_rptr + REC_DATA + dp_wptr++] = dat;
    return SCPE_OK;
    }
if (dp_wptr < (lnt + REC_MAXEXT)) {
    dpxb[dp_rptr + REC_EXT]++;
    dpxb[dp_rptr + REC_DATA + dp_wptr++] = dat;
    return SCPE_OK;
    }
dpxb[dp_rptr + REC_DATA + dp_wptr] = dp_csum;           /* write csum */
dpxb[dp_rptr + lnt + REC_OVHD] = 0;                     /* zap rest of track */
if ((r = dp_wrdone (uptr, STA_UNSER)))                  /* dump track */
    return r;
return STOP_DPOVR;
}       

/* Write done, dump track, clear busy */

t_stat dp_wrdone (UNIT *uptr, uint32 flg)
{
dp_done (1, flg);
return dp_wrtrk (uptr, dpxb, uptr->CYL, CW1_GETHEAD (dp_cw1));
}

/* Clear busy, set errors, request interrupt if required */

t_stat dp_done (uint32 req, uint32 flg)
{
dp_xip = 0;                                             /* clear xfr in prog */
dp_sta = (dp_sta | flg) & ~(STA_BUSY | STA_MBZ);        /* clear busy */
if (req || dp_defint)                                   /* if req, set intr */
    SET_INT (INT_DP);
dp_defint = 0;                                          /* clr def intr */
return SCPE_OK;
}

/* Reset routine */

t_stat dp_reset (DEVICE *dptr)
{
int32 i;

dp_fnc = 0;
dp_cw1 = 0;
dp_cw2 = 0;
dp_sta = 0;
dp_buf = 0;
dp_xip = 0;
dp_eor = 0;
dp_dma = 0;
dp_csum = 0;
dp_rptr = 0;
dp_wptr = 0;
dp_bctr = 0;
dp_gap = 0;
dp_defint = 0;
for (i = 0; i < DP_NUMDRV; i++) {                       /* loop thru drives */
    sim_cancel (&dp_unit[i]);                           /* cancel activity */
    dp_unit[i].FNC = 0;                                 /* clear function */
    dp_unit[i].CYL = 0;
    }
CLR_INT (INT_DP);                                       /* clear int, enb */
CLR_ENB (INT_DP);
return SCPE_OK;
}

/* Attach routine, test formating */

t_stat dp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    return r;
return dp_showformat (stdout, uptr, 0, NULL);
}

/* Set controller type */

t_stat dp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val >= DP_NUMTYP) || (cptr != NULL))
    return SCPE_ARG;
for (i = 0; i < DP_NUMDRV; i++) {
    if (dp_unit[i].flags & UNIT_ATT) return SCPE_ALATT;
    }
for (i = 0; i < DP_NUMDRV; i++)
    dp_unit[i].capac = dp_tab[val].cap;
dp_ctype = val;
return SCPE_OK;
}

/* Show controller type */

t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (dp_ctype >= DP_NUMTYP)
    return SCPE_IERR;
fprintf (st, "%s", dp_tab[dp_ctype].name);
return SCPE_OK;
}

/* Set drive format

   There is no standard format for record addresses.  This routine
   provides two schemes:

   -S           sequential addressing (starting from 0)
   default      geometric addressing (8b: cylinder, 5b: head, 3b: sector)

   This routine also supports formatting by record count or word count:

   -R           argument is records per track
   default      argument is words per record

   The relationship between words per record (W), bits per track (B),
   and records per track (R), is as follows:

   W = (B / (R + ((R - 1) / 20))) - 16.5

   where (R - 1) / 20 is the "5% gap" and 16.5 is the overhead, in words,
   per record.
*/

t_stat dp_setformat (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 h, c, cntr, rptr;
int32 i, nr, nw, inp;
uint16 tbuf[DP_TRKLEN];
float finp;
t_stat r;

if (uptr == NULL)
    return SCPE_IERR;
if (cptr == NULL)
    return SCPE_ARG;
if (!(uptr->flags & UNIT_ATT))
    return SCPE_UNATT;
inp = (int32) get_uint (cptr, 10, 2048, &r);
if (r != SCPE_OK)
    return r;
if (inp == 0)
    return SCPE_ARG;
finp = (float) inp;
if (sim_switches & SWMASK ('R')) {                      /* format records? */
    nr = inp;
    nw = (int32) ((dp_tab[dp_ctype].wrds / (finp + ((finp - 1.0) / 20.0))) - REC_OVHD_WRDS);
    if (nw <= 0)
        return SCPE_ARG;
    }
else {
    nw = inp;                                           /* format words */
    nr = (int32) ((((20.0 * dp_tab[dp_ctype].wrds) / (finp + REC_OVHD_WRDS)) + 1.0) / 21.0);
    if (nr <= 0)
        return SCPE_ARG;
    }
sim_printf ("Proposed format: records/track = %d, record size = %d\n", nr, nw);
if (!get_yn ("Formatting will destroy all data on this disk; proceed? [N]", FALSE))
    return SCPE_OK;
for (c = cntr = 0; c < dp_tab[dp_ctype].cyl; c++) {
    for (h = 0; h < dp_tab[dp_ctype].surf; h++) {
        for (i = 0; i < DP_TRKLEN; i++)
            tbuf[i] = 0;
        rptr = 0;
        for (i = 0; i < nr; i++) {
            tbuf[rptr + REC_LNT] = nw & DMASK;
            if (sim_switches & SWMASK ('S'))
                tbuf[rptr + REC_ADDR] = cntr++;
            else tbuf[rptr + REC_ADDR] = (c << 8) + (h << 3) + i;
            rptr = rptr + nw + REC_OVHD;
            }
        if ((r = dp_wrtrk (uptr, tbuf, c, h)))
            return r;
        }
    }
sim_printf ("Formatting complete\n");
return SCPE_OK;
}

/* Show format */

t_stat dp_showformat (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 c, h, rptr, rlnt, sec;
uint32 minrec = DP_TRKLEN;
uint32 maxrec = 0;
uint32 minsec = DP_TRKLEN;
uint32 maxsec = 0;
uint16 tbuf[DP_TRKLEN];
t_stat r;

if (uptr == NULL)
    return SCPE_IERR;
if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
for (c = 0; c < dp_tab[dp_ctype].cyl; c++) {
    for (h = 0; h < dp_tab[dp_ctype].surf; h++) {
        if ((r = dp_rdtrk (uptr, tbuf, c, h)))
            return r;
        rptr = 0;
        rlnt = tbuf[rptr + REC_LNT];
        if (rlnt == 0) {
            if (c || h)
                fprintf (st, "Unformatted track, cyl = %d, head = %d\n", c, h);
            else fprintf (st, "Disk is unformatted\n");
            return SCPE_OK;
            }
        for (sec = 0; rlnt != 0; sec++) {
            if ((rptr + rlnt + REC_OVHD) >= DP_TRKLEN) {
                fprintf (st, "Invalid record length %d, cyl = %d, head = %d, sect = %d\n",
                    rlnt, c, h, sec);
                return SCPE_OK;
                }
            if (tbuf[rptr + REC_EXT] >= REC_MAXEXT) {
                fprintf (st, "Invalid record extension %d, cyl = %d, head = %d, sect = %d\n",
                    tbuf[rptr + REC_EXT], c, h, sec);
                return SCPE_OK;
                }
            if (rlnt > maxrec)
                maxrec = rlnt;
            if (rlnt < minrec)
                minrec = rlnt;
            rptr = rptr + rlnt + REC_OVHD;
            rlnt = tbuf[rptr + REC_LNT];
            }
        if (sec > maxsec)
            maxsec = sec;
        if (sec < minsec)
            minsec = sec;
        }
    }
if ((minrec == maxrec) && (minsec == maxsec))
    fprintf (st, "Valid fixed format, records/track = %d, record size = %d\n",
             minsec, minrec);
else if (minrec == maxrec)
    fprintf (st, "Valid variable format, records/track = %d-%d, record size = %d\n",
             minsec, maxsec, minrec);
else if (minsec == maxsec)
    fprintf (st, "Valid variable format, records/track = %d, record sizes = %d-%d\n",
             minsec, minrec, maxrec);
else fprintf (st, "Valid variable format, records/track = %d-%d, record sizes = %d-%d\n",
              minsec, maxsec, minrec, maxrec);
return SCPE_OK;
}
