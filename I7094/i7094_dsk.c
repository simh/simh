/* i7094_dsk.c: 7631 file control (disk/drum) simulator

   Copyright (c) 2005-2008, Robert M Supnik

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

   dsk          7631 file control

   The 7631 is a controller for multiple serial bit stream devices such as
   disks or drums.  It supports the

   1301         fixed disk
   1302         fixed disk
   2302         fixed disk
   7320         drum

   The 7631 supports variable record formatting, user-specified record numbering,
   and other complex features.  Each track has

        home address 1:         the track number, 4 BCD digits (implicit)
        home address 2:         user-specified track identifier, 6 BCD chars
        records 1..n:           variably formatted records, each consisting of
          record address:       user-specified record identifier, 4 BCD digits
                                 and 2 BCD characters
          record data:          36b words

   To deal with this, the simulator provides a container of 500 (7320/1301) or
   1000 (1302/2302) words per track.  Each track starts with home address 2
   and then contains a variable number of records.  Each record has a two-word
   header followed by data:

        word 0:                 record length without header
        word 1:                 record address
        word 2:                 start of data
        word 2+n-1:             end of data
        word 2+n+2:             start of next record

   A record length of 0 indicates end of valid data on the track.

   Orders to the 7631 are 10 BCD digits (60b), consisting of two words:

        word 0:                 op-op-access-module-d1-d2
        word 1:                 d3-d4-d5-d6-x-x

   Depending on the opcode, d1:d6 can be a track number plus home address 2,
   or a record number.

   Status from the 7631 is also 10 BCD digits (60b), with 36b in the first
   word, and 24b (plus 12b of zeroes) in the second.

   Because modules can have two access arms that seek independently, each
   module m is represented by two units: unit m for access 0 and unit m+10
   for access 1.  This requires tricky bookkeeping to be sure that the
   service routine is using the 'right' unit.

   Limitations of the simulation:

   - HA2 and record address must be exactly 6 characters (one word)
   - Record lengths must be exact multiples of 6 characters
   - Seek timing is fixed rather than based on seek length
*/

/* Definitions */

#include "i7094_defs.h"
#include <math.h>

#define DSK_NUMDR       10                              /* modules/controller */
#define DSK_SNS         (2 * DSK_NUMDR)                 /* dummy unit for sense */

/* Drive geometry */

#define DSK_WDSPT_7320  500                             /* words/track */
#define DSK_WDSPT_1301  500
#define DSK_WDSPT_1302  1000
#define DSK_WDSPT_2302  1000
#define DSK_TRKPC_7320  400                             /* tracks/cylinder */
#define DSK_TRKPC_1301  40
#define DSK_TRKPC_1302  40
#define DSK_TRKPC_2302  40
#define DSK_CYLPA_7320  1                               /* cylinders/access */
#define DSK_CYLPA_1301  250
#define DSK_CYLPA_1302  250
#define DSK_CYLPA_2302  250
#define DSK_TRKPA_7320  (DSK_TRKPC_7320*DSK_CYLPA_7320) /* tracks/access */
#define DSK_TRKPA_1301  (DSK_TRKPC_1301*DSK_CYLPA_1301)
#define DSK_TRKPA_1302  (DSK_TRKPC_1302*DSK_CYLPA_1302)
#define DSK_TRKPA_2302  (DSK_TRKPC_2302*DSK_CYLPA_2302)
#define DSK_ACCPM_7320  1                               /* access/module */
#define DSK_ACCPM_1301  1
#define DSK_ACCPM_1302  2
#define DSK_ACCPM_2302  2
#define DSK_FMCPT_7320  2868                            /* format chars/track */
#define DSK_FMCPT_1301  2868
#define DSK_FMCPT_1302  5942
#define DSK_FMCPT_2302  5942
#define SIZE_7320       (DSK_WDSPT_7320*DSK_TRKPA_7320*DSK_ACCPM_7320)
#define SIZE_1301       (DSK_WDSPT_1301*DSK_TRKPA_1301*DSK_ACCPM_1301)
#define SIZE_1302       (DSK_WDSPT_1302*DSK_TRKPA_1302*DSK_ACCPM_1302)
#define SIZE_2302       (DSK_WDSPT_2302*DSK_TRKPA_2302*DSK_ACCPM_2302)
#define DSK_BUFSIZ      (DSK_WDSPT_2302)
#define DSK_DA(a,t,d)   (((((a) * dsk_tab[d].trkpa) + (t)) * dsk_tab[d].wdspt) *\
                    sizeof (t_uint64))

/* Unit flags */

#define UNIT_V_INOP0    (UNIT_V_UF + 0)                 /* acc 0 inoperative */
#define UNIT_V_INOP1    (UNIT_V_UF + 1)                 /* acc 1 inoperative */
#define UNIT_V_FMTE     (UNIT_V_UF + 2)                 /* format enabled */
#define UNIT_V_TYPE     (UNIT_V_UF + 3)                 /* drive type */
#define UNIT_M_TYPE     03
#define UNIT_INOP0      (1 << UNIT_V_INOP0)
#define UNIT_INOP1      (1 << UNIT_V_INOP1)
#define UNIT_FMTE       (1 << UNIT_V_FMTE)
#define UNIT_TYPE       (UNIT_M_TYPE << UNIT_V_TYPE)
#define  TYPE_7320      (0 << UNIT_V_TYPE)
#define  TYPE_1301      (1 << UNIT_V_TYPE)
#define  TYPE_1302      (2 << UNIT_V_TYPE)
#define  TYPE_2302      (3 << UNIT_V_TYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_TYPE) & UNIT_M_TYPE)
#define TRK             u3                              /* track */
#define SKF             u4                              /* seek in progress */

/* Track/record structure */

#define THA2            0                               /* home address 2 */
#define HA2_MASK        INT64_C(0777700000000)          /* two chars checked */
#define T1STREC         1                               /* start of records */
#define RLNT            0                               /* record length */
#define RADDR           1                               /* record address */
#define RDATA           2                               /* start of data */
#define REC_MASK        INT64_C(0171717177777)          /* 4 digits, 2 chars */

/* Command word (60b) - 10 BCD digits */

#define OP1             0                               /* opcode */
#define OP2             1
#define ACC             2                               /* access */
#define MOD             3                               /* module */
#define T1              4                               /* track */
#define T2              5
#define T3              6
#define T4              7

/* Disk states */

#define DSK_IDLE        0

/* Status word (60b) */

#define DSKS_PCHK       INT64_C(004000000000000000000)  /* prog check */
#define DSKS_DCHK       INT64_C(002000000000000000000)  /* data check */
#define DSKS_EXCC       INT64_C(001000000000000000000)  /* exc cond */
#define DSKS_INVS       INT64_C(000200000000000000000)  /* invalid seq */
#define DSKS_INVC       INT64_C(000040000000000000000)  /* invalid opcode */
#define DSKS_FMTC       INT64_C(000020000000000000000)  /* format check */
#define DSKS_NRCF       INT64_C(000010000000000000000)  /* no record found */
#define DSKS_INVA       INT64_C(000002000000000000000)  /* invalid address */
#define DSKS_RSPC       INT64_C(000000400000000000000)  /* response check */
#define DSKS_CMPC       INT64_C(000000200000000000000)  /* compare check */
#define DSKS_PARC       INT64_C(000000100000000000000)  /* parity check */
#define DSKS_ACCI       INT64_C(000000020000000000000)  /* access inoperative */
#define DSKS_ACCN       INT64_C(000000004000000000000)  /* access not ready */
#define DSKS_DSKE       INT64_C(000000002000000000000)  /* disk error */
#define DSKS_FILE       INT64_C(000000001000000000000)  /* file error */
#define DSKS_6B         INT64_C(000000000040000000000)  /* six bit mode */
#define DSKS_ATN0       INT64_C(000000000002000000000)  /* attention start */
#define DSKS_PALL       INT64_C(000777000000000000000)
#define DSKS_DALL       INT64_C(000000740000000000000)
#define DSKS_EALL       INT64_C(000000037000000000000)
#define DSKS_ALLERR     INT64_C(007777777000000000000)

/* Commands - opcode 0 */

#define DSKC_NOP        0x00
#define DSKC_RLS        0x04
#define DSKC_8B         0x08
#define DSKC_6B         0x09

/* Commands - opcode 8 */

#define DSKC_SEEK       0x0                             /* seek */
#define DSKC_SREC       0x2                             /* single record */
#define DSKC_WFMT       0x3                             /* write format */
#define DSKC_TNOA       0x4                             /* track no addr */
#define DSKC_CYL        0x5                             /* cyl no addr */
#define DSKC_WCHK       0x6                             /* write check */
#define DSKC_ACCI       0x7                             /* set acc inoperative */
#define DSKC_TWIA       0x8                             /* track with addr */
#define DSKC_THA        0x9                             /* track home addr */

/* CTSS record structure */

#define CTSS_HA2        INT64_C(0676767676767)          /* =HXXXXXX */
#define CTSS_RLNT       435                             /* data record */
#define CTSS_D1LNT      31                              /* padding */
#define CTSS_D2LNT      14
#define CTSS_D3LNT      16
#define CTSS_DLLNT      1
#define CTSS_RA1        2
#define CTSS_RA2        8

/* Data and declarations */

typedef struct {
    const char          *name;
    uint32              accpm;                          /* acc/module: 1 or 2 */
    uint32              wdspt;                          /* wds/track: 500 or 1000 */
    uint32              trkpc;                          /* trks/cyl: 1 or 40 */
    uint32              trkpa;                          /* trks/acc: 400 or 10000 */
    uint32              fchpt;                          /* format ch/track */
    uint32              size;
    } DISK_TYPE;

const DISK_TYPE dsk_tab[4] = {
    { "7320", DSK_ACCPM_7320, DSK_WDSPT_7320,
      DSK_TRKPC_7320, DSK_TRKPA_7320, DSK_FMCPT_7320, SIZE_7320 },
    { "1301", DSK_ACCPM_1301, DSK_WDSPT_1301,
      DSK_TRKPC_1301, DSK_TRKPA_1301, DSK_FMCPT_1301, SIZE_1301 },
    { "1302", DSK_ACCPM_1302, DSK_WDSPT_1302,
      DSK_TRKPC_1302, DSK_TRKPA_1302, DSK_FMCPT_1302, SIZE_1302 },
    { "2302", DSK_ACCPM_2302, DSK_WDSPT_2302,
      DSK_TRKPC_2302, DSK_TRKPA_2302, DSK_FMCPT_2302, SIZE_2302 }
    };

/* 7320/1301 format track characters */

uint8 fmt_thdr_7320[] = {
    4, 4, 4,                                            /* gap 1 */
    3, 3, 3, 3, 3, 3, 3, 3, 3,                          /* ha1 */
    4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4,                 /* gap 2 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0                     /* ha2 */
    };
uint8 fmt_rhdr_7320[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,                 /* x gap */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,                       /* record addr */
    2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,                 /* y gap */
    1, 1, 1, 1, 0                                       /* record ovhd */
    };

/* 1302/2302 format track characters */

uint8 fmt_thdr_1302[] = {
    4, 4, 4, 4, 4, 4,                                   /* gap 1 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,                 /* ha1 */
    4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4,        /* gap 2 */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0            /* ha2 */
    };
uint8 fmt_rhdr_1302[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,        /* x gap */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,              /* record addr */
    2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,        /* y gap */
    1, 1, 1, 1, 1, 1, 1, 0                              /* record ovhd */
    };

/* CTSS 7320/1301 track format table */

uint32 ctss_fmt_7320[] = {
    CTSS_RLNT, CTSS_D3LNT, CTSS_DLLNT, 0
    };

/* CTSS 1302/2302 track format table */

uint32 ctss_fmt_1302[] = {
    CTSS_RLNT, CTSS_D1LNT, CTSS_D2LNT,
    CTSS_RLNT, CTSS_D3LNT, CTSS_DLLNT, 0
    };

uint32 dsk_ch = CH_C;                                   /* disk channel */
uint32 dsk_acc = 0;                                     /* access */
uint32 dsk_mod = 0;                                     /* module */
uint32 dsk_sta = 0;                                     /* disk state */
uint32 dsk_mode = 0;                                    /* I/O mode */
uint32 dsk_wchk = 0;                                    /* write check flag */
uint32 dsk_ctime = 10;                                  /* command time */
uint32 dsk_stime = 1000;                                /* seek time */
uint32 dsk_rtime = 100;                                 /* rotational latency */
uint32 dsk_wtime = 2;                                   /* word time */
uint32 dsk_gtime = 5;                                   /* gap time */
uint32 dsk_rbase = 0;                                   /* record tracking */
uint32 dsk_rptr = 0;
uint32 dsk_rlim = 0;
uint32 dsk_stop = 0;
uint32 dsk_fmt_cntr = 0;                                /* format counter */
t_uint64 dsk_rec = 0;                                   /* rec/home addr (36b) */
t_uint64 dsk_sns = 0;                                   /* sense data (60b) */
t_uint64 dsk_cmd = 0;                                   /* BCD command (60b) */
t_uint64 dsk_chob = 0;                                  /* chan output buffer */
uint32 dsk_chob_v = 0;                                  /* valid */
t_uint64 dsk_buf[DSK_BUFSIZ];                           /* transfer buffer */

extern uint32 ch_req;

t_stat dsk_svc (UNIT *uptr);
t_stat dsk_svc_sns (UNIT *uptr);
t_stat dsk_reset (DEVICE *dptr);
t_stat dsk_attach (UNIT *uptr, CONST char *cptr);
t_stat dsk_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dsk_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat dsk_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_stat dsk_new_cmd (uint32 ch, t_uint64 cmd);
t_stat dsk_uend (uint32 ch, t_uint64 stat);
t_stat dsk_recad (uint32 trk, uint32 rec, uint32 acc, uint32 mod, t_uint64 *res);
t_uint64 dsk_acc_atn (uint32 u);
t_stat dsk_init_trk (UNIT *udptr, uint32 trk);
t_stat dsk_xfer_done (UNIT *uaptr, uint32 dtyp);
t_stat dsk_wr_trk (UNIT *uptr, uint32 trk);
t_bool dsk_get_fmtc (uint32 dtyp, uint8 *fc);
t_bool dsk_qdone (uint32 ch);
t_stat dsk_show_format (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* DSK data structures

   dsk_dev      DSK device descriptor
   dsk_unit     DSK unit descriptor
   dsk_reg      DSK register list
*/

DIB dsk_dib = { &dsk_chsel, &dsk_chwr };

UNIT dsk_unit[] = {
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             TYPE_7320, SIZE_7320) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_DIS+TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_DIS+TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_DIS+TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_DIS+TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_DIS+TYPE_2302, SIZE_2302) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc, UNIT_DIS, 0) },
    { UDATA (&dsk_svc_sns, UNIT_DIS, 0) }
    };

REG dsk_reg[] = {
    { ORDATA (STATE, dsk_sta, 6) },
    { ORDATA (ACCESS, dsk_acc, 1) },
    { ORDATA (MODULE, dsk_mod, 4) },
    { ORDATA (RECORD, dsk_rec, 36) },
    { ORDATA (MODE, dsk_mode, 4) },
    { ORDATA (SENSE, dsk_sns, 60) },
    { ORDATA (BCDCMD, dsk_cmd, 60) },
    { ORDATA (CHOB, dsk_chob, 36) },
    { FLDATA (CHOBV, dsk_chob_v, 0) },
    { FLDATA (STOP, dsk_stop, 0) },
    { DRDATA (FCNTR, dsk_fmt_cntr, 13) },
    { BRDATA (BUF, dsk_buf, 8, 36, DSK_BUFSIZ) },
    { DRDATA (RBASE, dsk_rbase, 10), REG_RO },
    { DRDATA (RPTR, dsk_rptr, 10), REG_RO },
    { DRDATA (RLIM, dsk_rlim, 10), REG_RO },
    { DRDATA (CHAN, dsk_ch, 3), REG_HRO },
    { DRDATA (STIME, dsk_stime, 24), REG_NZ + PV_LEFT },
    { DRDATA (RTIME, dsk_rtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (WTIME, dsk_wtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (GTIME, dsk_gtime, 24), REG_NZ + PV_LEFT },
    { DRDATA (CTIME, dsk_ctime, 24), REG_NZ + PV_LEFT },
    { URDATA (TRACK, dsk_unit[0].TRK, 10, 14, 0,
              2 * DSK_NUMDR, PV_LEFT) },
    { URDATA (SEEKF, dsk_unit[0].SKF, 10, 1, 0,
              2 * DSK_NUMDR, PV_LEFT | REG_HRO) },
    { URDATA (CAPAC, dsk_unit[0].capac, 10, T_ADDR_W, 0,
              DSK_NUMDR, PV_LEFT | REG_HRO) },
    { NULL }
    };

MTAB dsk_mtab[] = {
    { UNIT_INOP0 + UNIT_INOP1, 0, "operational", "OPERATIONAL" },
    { UNIT_INOP0 + UNIT_INOP1, UNIT_INOP0, "access 0 inoperative", NULL },
    { UNIT_INOP0 + UNIT_INOP1, UNIT_INOP1, "access 1 inoperative", NULL },
    { UNIT_FMTE, UNIT_FMTE, "formating enabled", "FORMAT" },
    { UNIT_FMTE, 0, "formating disabled", "NOFORMAT" },
    { UNIT_TYPE, TYPE_7320, "7320", "7320", &dsk_set_size },
    { UNIT_TYPE, TYPE_1301, "1301", "1301", &dsk_set_size },
    { UNIT_TYPE, TYPE_1302, "1302", "1302", &dsk_set_size },
    { UNIT_TYPE, TYPE_2302, "2302", "2302", &dsk_set_size },
    { MTAB_XTD|MTAB_VDV, 0, "CHANNEL", NULL,
      NULL, &ch_show_chan, NULL },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 1, "FORMAT", NULL,
      NULL, &dsk_show_format, NULL },
    { 0 }
    };

DEVICE dsk_dev = {
    "DSK", dsk_unit, dsk_reg, dsk_mtab,
    (DSK_NUMDR * 2) + 1, 10, 24, 1, 8, 36,
    NULL, NULL, &dsk_reset,
    NULL, &dsk_attach, NULL,
    &dsk_dib, DEV_DIS
    };

/* Disk channel select, from 7909 channel program */

t_stat dsk_chsel (uint32 ch, uint32 sel, uint32 unit)
{
uint32 u;

dsk_ch = ch;
if (dsk_sta != DSK_IDLE)                                /* not idle? seq check */
    dsk_uend (ch, DSKS_INVS);

switch (sel) {

    case CHSL_CTL:                                      /* control */
        ch_req |= REQ_CH (ch);                          /* request channel */
        break;

    case CHSL_SNS:                                      /* sense */
        if (sim_is_active (&dsk_unit[DSK_SNS]))         /* already sensing? */
            return dsk_uend (ch, DSKS_INVS);            /* sequence error */
        sim_activate (&dsk_unit[DSK_SNS], dsk_ctime);   /* set timer */
        dsk_stop = 0;
        break;

    case CHSL_RDS:                                      /* read */
        if (dsk_mode == DSKC_WFMT)                      /* write format? */
            return dsk_uend (ch, DSKS_INVS);            /* sequence error */
    case CHSL_WRS:                                      /* write */
        if (dsk_mode == 0)                              /* no mode? seq check */
            dsk_uend (ch, DSKS_INVS);
        if (dsk_mode == DSKC_WFMT)                      /* format? fake sel */
            sel = CHSL_FMT;
        u = (dsk_acc * DSK_NUMDR) + dsk_mod;            /* access unit number */
        if (sim_is_active (&dsk_unit[u]))               /* access in use? */
            return dsk_uend (ch, DSKS_ACCN);            /* access not ready */
        sim_activate (&dsk_unit[u], dsk_rtime);         /* rotational time */
        break;

    default:                                            /* other */
        return STOP_ILLIOP;
        }

dsk_sta = sel;                                          /* set new state */
return SCPE_OK;
}

/* Disk channel write, from 7909 channel program */

t_stat dsk_chwr (uint32 ch, t_uint64 val, uint32 stopf)
{
if (stopf)                                              /* stop? */
    dsk_stop = 1;

else {
    val = val & DMASK;
    switch (dsk_sta) {                                 /* case on state */

    case CHSL_CTL:                                      /* control */
        dsk_cmd = val << 24;
        if (val & INT64_C(0100000000000)) {             /* need 2nd word? */
            ch_req |= REQ_CH (ch);                      /* req ch for 2nd */
            dsk_sta = CHSL_CTL|CHSL_2ND;                /* next state */
            return SCPE_OK;
            }
        return dsk_new_cmd (ch, dsk_cmd);               /* no, do cmd */

    case CHSL_CTL|CHSL_2ND:                             /* 2nd control */
        dsk_cmd |= (val >> 12);
        return dsk_new_cmd (ch, dsk_cmd);               /* do cmd */

    default:
        dsk_chob = val;                                 /* store data */
        dsk_chob_v = 1;                                 /* set valid */
        }
    }

return SCPE_OK;
}

/* New command - end of CTL sequence */

t_stat dsk_new_cmd (uint32 ch, t_uint64 cmd)
{
uint32 i, d, a, m, u, trk, dtyp, bcd[8];

ch_req |= REQ_CH (ch);                                  /* req ch for end */
ch9_set_end (ch, 0);                                    /* set end flag */
dsk_sta = DSK_IDLE;                                     /* ctrl is idle */

for (i = 0; i < 8; i++) {                               /* get chars from cmd */
    d = (uint32) (cmd >> (6 * (9 - i))) & BCD_MASK;
    if (d == BCD_ZERO)
        d = 0;
    else if (d == 0)                                    /* BCD zero cvt */
        d = BCD_ZERO;
    bcd[i] = d;
    }

if (bcd[OP1] == 0) {                                    /* cmd = 0x? */

    switch (bcd[OP2]) {                                 /* case on x */

    case DSKC_NOP:                                      /* nop */
    case DSKC_RLS:                                      /* release */
        break;

    case DSKC_8B:                                       /* 8b mode */
        dsk_sns &= ~DSKS_6B;
        break;

    case DSKC_6B:                                       /* 6b mode */
        dsk_sns |= DSKS_6B;
        break;

    default:                                            /* unknown */
        return dsk_uend (ch, DSKS_INVC);                /* invalid opcode */
        }                                               /* end case op2 */
    return SCPE_OK;
    }                                                   /* end if */

else if (bcd[OP1] == 8) {                               /* cmd = 8x? */

    a = bcd[ACC];                                       /* get access, */
    m = bcd[MOD];                                       /* module */
    u = (a * DSK_NUMDR) + m;                            /* unit for access */
    if ((m > DSK_NUMDR) ||                              /* invalid module? */
        (dsk_unit[m].flags & UNIT_DIS))                 /* disabled module? */
        return dsk_uend (ch, DSKS_ACCI);
    dtyp = GET_DTYPE (dsk_unit[m].flags);               /* get drive type */
    if ((a >= dsk_tab[dtyp].accpm) ||                   /* invalid access? */
        (dsk_unit[m].flags & (UNIT_INOP0 << a)))        /* access inop? */
        return dsk_uend (ch, DSKS_ACCI);
    if ((bcd[T1] > 9) || (bcd[T2] > 9) || (bcd[T3] > 9) || (bcd[T4] > 9))
        trk = dsk_tab[dtyp].trkpa + 1;                  /* bad track */
    else trk = (((((bcd[T1] * 10) + bcd[T2]) * 10) + bcd[T3]) * 10) + bcd[T4];

    if (bcd[OP2] == DSKC_WCHK) {                        /* write check */
        if (dsk_mode == 0)                              /* no prior operation? */
            return dsk_uend (ch, DSKS_INVS);
        bcd[OP2] = dsk_mode;                            /* use prior mode */
        dsk_wchk = 1;                                   /* set write check */
        }
    else dsk_wchk = 0;
    dsk_sns &= ~(DSKS_ALLERR | dsk_acc_atn (u));        /* clear err, atn */
    dsk_stop = 0;                                       /* clear stop */

    switch (bcd[OP2]) {

    case DSKC_SEEK:                                     /* seek */
        if ((trk >= dsk_tab[dtyp].trkpa) &&             /* inv track? */
            ((dtyp == TYPE_7320) ||                     /* drum or not CE? */
             (bcd[T1] > 9) || (bcd[T2] != BCD_AT) ||
             (bcd[T3] > 9) || (bcd[T4] > 9)))
            return dsk_uend (ch, DSKS_INVA);
        if (sim_is_active (&dsk_unit[u]))               /* selected acc busy? */
            return dsk_uend (ch, DSKS_ACCN);
        dsk_unit[u].SKF = 1;                            /* set seeking flag */
        dsk_unit[u].TRK = trk;                          /* sel acc on cyl */
        sim_activate (&dsk_unit[u], dsk_stime);         /* seek */
        dsk_mode = 0;                                   /* clear I/O mode */
        return SCPE_OK;

    case DSKC_ACCI:                                     /* access inoperative */
        dsk_unit[m].flags |= (UNIT_INOP0 << a);         /* set correct flag */
        dsk_mode = 0;                                   /* clear I/O mode */
        return SCPE_OK;

    case DSKC_SREC:                                     /* single record */
        break;                                          /* no verification */

    case DSKC_WFMT:                                     /* format */
        if (!(dsk_unit[m].flags & UNIT_FMTE))           /* format enabled? */
            return dsk_uend (ch, DSKS_FMTC);            /* no, error */
    case DSKC_TNOA:                                     /* track no addr */
    case DSKC_CYL:                                      /* cyl no addr */
    case DSKC_TWIA:                                     /* track with addr */
    case DSKC_THA:                                      /* track home addr */
        if (trk != (uint32) dsk_unit[u].TRK)            /* on track? */
            return dsk_uend (ch, DSKS_NRCF);
        break;    

    default:
        return dsk_uend (ch, DSKS_INVC);                /* invalid opcode */
        }

    dsk_acc = a;                                        /* save access */
    dsk_mod = m;                                        /* save module */
    dsk_rec = cmd & DMASK;                              /* save rec/home addr */
    dsk_mode = bcd[OP2];                                /* save mode */
    return SCPE_OK;
    }

return dsk_uend (ch, DSKS_INVC);                        /* invalid opcode */
}

/* Sense unit service */

t_stat dsk_svc_sns (UNIT *uptr)
{
t_uint64 dat;

switch (dsk_sta) {                                      /* case on state */

    case CHSL_SNS:                                      /* prepare data */
        dsk_buf[0] = (dsk_sns >> 24) & DMASK;           /* buffer is 2 words */
        dsk_buf[1] = (dsk_sns << 12) & DMASK;
        dsk_rptr = 0;
        dsk_rlim = 2;
        dsk_sta = CHSL_SNS|CHSL_2ND;                    /* 2nd state */
        break;

    case CHSL_SNS|CHSL_2ND:                             /* second state */
        if (dsk_rptr >= dsk_rlim) {                     /* end of buffer? */
            ch9_set_end (dsk_ch, 0);                    /* set end */
            ch_req |= REQ_CH (dsk_ch);                  /* request channel */
            dsk_sta = CHSL_SNS|CHSL_3RD;                /* 3rd state */
            sim_activate (uptr, dsk_ctime);             /* longer wait */
            return SCPE_OK;
            }
        dat = dsk_buf[dsk_rptr++];                      /* get word */
        if (!dsk_stop)                                  /* send wd to chan */
            ch9_req_rd (dsk_ch, dat);
        break;

    case CHSL_SNS|CHSL_3RD:                             /* 3rd state */
        if (dsk_qdone (dsk_ch))                         /* done? exit */
            return SCPE_OK;
        dsk_sta = CHSL_SNS;                             /* repeat sequence */
        break;
        }

sim_activate (uptr, dsk_wtime);                         /* sched next */
return SCPE_OK;
}

/* Seek, read, write unit service */

t_stat dsk_svc (UNIT *uaptr)
{
uint32 i, dtyp, trk;
uint8 fc, *format;
t_uint64 rdat;
UNIT *udptr;
t_stat r;

if (uaptr->SKF) {                                       /* seeking? */
    uint32 u = uaptr - dsk_dev.units;                   /* get unit */
    uaptr->SKF = 0;                                     /* seek done */
    dsk_sns |= dsk_acc_atn (u);                         /* set atn bit */
    ch9_set_atn (dsk_ch);                               /* set atn flag */
    return SCPE_OK;
    }

udptr = dsk_dev.units + dsk_mod;                        /* data unit */
if (udptr->flags & (UNIT_INOP0 << dsk_acc))             /* acc inoperative? */
    return dsk_uend (dsk_ch, DSKS_ACCI);                /* error */
if ((udptr->flags & UNIT_ATT) == 0) {                   /* not attached? */
    dsk_uend (dsk_ch, DSKS_ACCI);                       /* error */
    return SCPE_UNATT;
    }

dtyp = GET_DTYPE (udptr->flags);                        /* get data drive type */
trk = uaptr->TRK;                                       /* get access track */

switch (dsk_sta) {                                      /* case on state */

    case CHSL_RDS:                                      /* read start */
        if ((r = dsk_init_trk (udptr, trk))) {          /* read track, err? */
            return ((r == ERR_NRCF)? SCPE_OK: r);       /* rec not fnd ok */
            }
        dsk_sta = CHSL_RDS|CHSL_2ND;                    /* next state */
        break;

    case CHSL_RDS|CHSL_2ND:                             /* read data transmit */
        if ((r = dsk_xfer_done (uaptr, dtyp))) {        /* transfer done? */
            if (r != ERR_ENDRC)                         /* error? */
                return r;
            dsk_sta = CHSL_RDS|CHSL_3RD;                /* next state */
            sim_activate (uaptr, dsk_gtime);            /* gap time */
            return SCPE_OK;
            }
        rdat = dsk_buf[dsk_rptr++];                     /* get word */
        if (dsk_rptr == T1STREC)                        /* if THA, skip after HA */
            dsk_rptr++;
        if (!dsk_stop)                                  /* give to channel */
            ch9_req_rd (dsk_ch, rdat);
        break;

    case CHSL_RDS|CHSL_3RD:                             /* read end rec/trk */
        if (dsk_qdone (dsk_ch))                         /* done? exit */
            return SCPE_OK;
        dsk_sta = CHSL_RDS;                             /* repeat sequence */
        break;

    case CHSL_WRS:                                      /* write start */
        if ((r = dsk_init_trk (udptr, trk))) {          /* read track, err? */
            return ((r == ERR_NRCF)? SCPE_OK: r);       /* rec not fnd ok */
            }
        ch_req |= REQ_CH (dsk_ch);                      /* first request */
        dsk_sta = CHSL_WRS|CHSL_2ND;                    /* next state */
        dsk_chob = 0;                                   /* clr, inval buffer */
        dsk_chob_v = 0;
        break;

    case CHSL_WRS|CHSL_2ND:                             /* write data transmit */
        if (dsk_chob_v)                                 /* valid? clear */
            dsk_chob_v = 0;
        else if (!dsk_stop)                             /* no, no stop? io chk */
            ch9_set_ioc (dsk_ch);
        if (dsk_wchk) {                                 /* write check? */
            if (dsk_buf[dsk_rptr++] != dsk_chob)        /* data mismatch? */
                return dsk_uend (dsk_ch, DSKS_CMPC);    /* error */
            }
        else dsk_buf[dsk_rptr++] = dsk_chob;            /* write, store word */
        if (dsk_rptr == T1STREC)                        /* if THA, skip after HA */
            dsk_rptr++;
        if ((r = dsk_xfer_done (uaptr, dtyp))) {        /* transfer done? */
            if (r != ERR_ENDRC)                         /* error? */
                return r;
            dsk_sta = CHSL_WRS|CHSL_3RD;                /* next state */
            sim_activate (uaptr, dsk_gtime);            /* gap time */
            return SCPE_OK;
            }
        if (!dsk_stop)                                  /* more to do */
            ch_req |= REQ_CH (dsk_ch);
        break;

    case CHSL_WRS|CHSL_3RD:                             /* write done */
        if (!dsk_wchk) {                                /* if write */
            if ((r = dsk_wr_trk (udptr, trk)))          /* write track; err? */
                return r;
            }
        if (dsk_qdone (dsk_ch))                         /* done? exit */
            return SCPE_OK;
        dsk_sta = CHSL_WRS;                             /* repeat sequence */
        break;

/* Formatting takes place in five stages

   1. Clear the track buffer, request the first word from the channel
   2. Match characters against the fixed overhead (HA1, HA2, and gaps)
   3. Match characters against the per-record overhead (RA and gaps)
   4. Count the characters defining the record length
   5. See if the next character is end or gap; if gap, return to stage 3

   This formating routine is not exact.  It checks whether the format
   will fit in the container, not whether the format would fit on a
   real 7320, 1301, 1302, or 2302. */

    case CHSL_FMT:                                      /* initialization */
        for (i = 0; i < DSK_BUFSIZ; i++)                /* clear track buf */
            dsk_buf[i] = 0;
        dsk_rbase = T1STREC;                            /* init record ptr */
        dsk_rptr = 0;                                   /* init format ptr */
        dsk_fmt_cntr = 0;                               /* init counter */
        ch_req |= REQ_CH (dsk_ch);                      /* request channel */
        dsk_sta = CHSL_FMT|CHSL_2ND;                    /* next state */
        dsk_chob = 0;                                   /* clr, inval buffer */
        dsk_chob_v = 0;
        break;

    case CHSL_FMT|CHSL_2ND:                             /* match track header */
        if ((dtyp == TYPE_7320) || (dtyp == TYPE_1301))
            format = fmt_thdr_7320;
        else format = fmt_thdr_1302;
        if (!dsk_get_fmtc (dtyp, &fc))                  /* get fmt char; err? */
            return SCPE_OK;
        if (fc != format[dsk_rptr++])                   /* mismatch? */
            return dsk_uend (dsk_ch, DSKS_FMTC);        /* format check */
        if (format[dsk_rptr] == 0) {                    /* end format? */
            dsk_sta = CHSL_FMT|CHSL_3RD;                /* next state */
            dsk_rptr = 0;                               /* reset format ptr */
            }
        break;

    case CHSL_FMT|CHSL_3RD:                             /* match record header */
        if ((dtyp == TYPE_7320) || (dtyp == TYPE_1301))
            format = fmt_rhdr_7320;
        else format = fmt_rhdr_1302;
        if (!dsk_get_fmtc (dtyp, &fc))                  /* get fmt char; err? */
            return SCPE_OK;
        if (fc != format[dsk_rptr++])                   /* mismatch? */
            return dsk_uend (dsk_ch, DSKS_FMTC);        /* format check */
        if (format[dsk_rptr] == 0) {                    /* end format? */
            dsk_sta = CHSL_FMT|CHSL_4TH;                /* next state */
            dsk_rlim = 0;                               /* reset record ctr */
            }
        break;

    case CHSL_FMT|CHSL_4TH:                             /* count record size */
        if (!dsk_get_fmtc (dtyp, &fc))                  /* get fmt char; err? */
            return SCPE_OK;
        if (fc == BCD_ONE)                              /* more record? */
            dsk_rlim++;
        else {
            uint32 rsiz = dsk_rlim / 6;                 /* rec size words */
            if ((fc != BCD_TWO) ||                      /* improper end? */
                (rsiz == 0) ||                          /* smaller than min? */
                ((dsk_rlim % 6) != 0) ||                /* not multiple of 6? */
                ((dsk_rbase + rsiz + RDATA) >= dsk_tab[dtyp].wdspt))
                return dsk_uend (dsk_ch, DSKS_FMTC);    /* format check */
            dsk_buf[dsk_rbase + RLNT] = rsiz;           /* record rec lnt */
            dsk_rbase = dsk_rbase + rsiz + RDATA;       /* new rec start */
            dsk_sta = CHSL_FMT|CHSL_5TH;                /* next state */
            }
        break;

    case CHSL_FMT|CHSL_5TH:                             /* record or track end */
        if (!dsk_get_fmtc (dtyp, &fc))                  /* get fmt char; err? */
            return SCPE_OK;
        if (fc == BCD_TWO) {                            /* back to record header? */
            dsk_rptr = 2;                               /* already done 2 chars */
            dsk_sta = CHSL_FMT|CHSL_3RD;                /* record header state */
            }
        else if (fc != BCD_ONE)                         /* format check */
            dsk_uend (dsk_ch, DSKS_FMTC);
        else {
            if (!dsk_wchk) {                            /* actual write? */
                trk = trk - (trk % dsk_tab[dtyp].trkpc);        /* cyl start */
                for (i = 0; i < dsk_tab[dtyp].trkpc; i++) {     /* do all tracks */
                    if ((r = dsk_wr_trk (udptr,  trk + i)))     /* wr track; err? */
                        return r;
                    }
                }
            ch9_set_end (dsk_ch, 0);                    /* set end */
            ch_req |= REQ_CH (dsk_ch);                  /* request channel */
            dsk_sta = DSK_IDLE;                         /* disk is idle */
            return SCPE_OK;                             /* done */
            }
        break;

    default:
        return SCPE_IERR;
        }

sim_activate (uaptr, dsk_wtime);
return SCPE_OK;
}

/* Initialize data transfer

   Inputs:
        udptr   =       pointer to data unit
        trk     =       track to read
   Outputs:
        dsk_buf contains track specified by trk
        dsk_rbase, dsk_rptr, dsk_rlim are initialized
   Errors:
        SCPE_IOERR =    I/O error (fatal, uend)
        ERR_NRCF =      no record found (HA2 or record number mismatch, uend)
        STOP_INVFMT =   invalid format (fatal, uend)
*/

t_stat dsk_init_trk (UNIT *udptr, uint32 trk)
{
uint32 k, da, dtyp, rlnt;

dtyp = GET_DTYPE (udptr->flags);                        /* get drive type */
da = DSK_DA (dsk_acc, trk, dtyp);                       /* get disk address */
sim_fseek (udptr->fileref, da, SEEK_SET);               /* read track */
k = sim_fread (dsk_buf, sizeof (t_uint64), dsk_tab[dtyp].wdspt, udptr->fileref);
if (ferror (udptr->fileref)) {                          /* error? */
    sim_perror ("DSK I/O error");
    clearerr (udptr->fileref);
    dsk_uend (dsk_ch, DSKS_DSKE);
    return SCPE_IOERR;
    }
for ( ; k < dsk_tab[dtyp].wdspt; k++)                   /* zero fill */
    dsk_buf[k] = 0;
dsk_rbase = T1STREC;                                    /* record base */
rlnt = (uint32) dsk_buf[dsk_rbase + RLNT];              /* length */
dsk_rlim = dsk_rbase + rlnt + RDATA;                    /* end */
if ((rlnt == 0) || (dsk_rlim >= dsk_tab[dtyp].wdspt)) { /* invalid record? */
    dsk_uend (dsk_ch, DSKS_FMTC);
    return STOP_INVFMT;
    }
if (dsk_mode != DSKC_SREC) {                            /* not single record? */
    if (dsk_mode == DSKC_THA)                           /* trk home addr? */
        dsk_rptr = 0;
    else {
        if (((dsk_rec << 24) ^ dsk_buf[THA2]) & HA2_MASK) {
            dsk_uend (dsk_ch, DSKS_NRCF);               /* invalid HA2 */
            return ERR_NRCF;
            }
        if (dsk_mode == DSKC_TWIA)                      /* track with addr? */
            dsk_rptr = dsk_rbase + RADDR;               /* start at addr */
        else dsk_rptr = dsk_rbase + RDATA;              /* else, at data */
        }
    return SCPE_OK;
    }
while (rlnt != 0) {                                     /* until end track */
    dsk_rptr = dsk_rbase + RDATA;
    if (((dsk_rec ^ dsk_buf[dsk_rbase + RADDR]) & REC_MASK) == 0)
        return SCPE_OK;                                 /* rec found? done */
    dsk_rbase = dsk_rlim;                               /* next record */
    rlnt = (uint32) dsk_buf[dsk_rbase + RLNT];          /* length */
    dsk_rlim = dsk_rbase + rlnt + RDATA;                /* limit */
    if (dsk_rlim >= dsk_tab[dtyp].wdspt) {              /* invalid format? */
        dsk_uend (dsk_ch, DSKS_FMTC);
        return STOP_INVFMT;
        }
    }
dsk_uend (dsk_ch, DSKS_NRCF);                           /* not found */
return ERR_NRCF;
}

/* Check end of transfer

   Inputs:
        uptr    =       pointer to access unit
        dtyp    =       drive type
   Outputs:
        ERR_ENDRC =     end of record/track/cylinder, end sent, ch req if required
        SCPE_OK =       more to do, dsk_rbase, dsk_rptr, dsk_rlim may be updated
        STOP_INVFMT =   invalid format (fatal, uend sent)
*/

t_stat dsk_xfer_done (UNIT *uaptr, uint32 dtyp)
{
uint32 rlnt;

if (dsk_rptr < dsk_rlim)                                /* record done? */
    return SCPE_OK;
if (dsk_stop || !ch9_qconn (dsk_ch) ||                  /* stop or err disc or */
    (dsk_mode == DSKC_SREC)) {                          /* single record? */
    ch9_set_end (dsk_ch, 0);                            /* set end */
    ch_req |= REQ_CH (dsk_ch);                          /* request channel */
    return ERR_ENDRC;
    }
dsk_rbase = dsk_rlim;                                   /* next record */
rlnt = (uint32) dsk_buf[dsk_rbase + RLNT];              /* length */
dsk_rlim = dsk_rbase + rlnt + RDATA;                    /* end */
if ((dsk_rbase >= dsk_tab[dtyp].wdspt) ||               /* invalid format? */
    (dsk_rlim >= dsk_tab[dtyp].wdspt)) {
    dsk_uend (dsk_ch, DSKS_FMTC);
    return STOP_INVFMT;
    }
if (rlnt) {                                             /* more on track? */
    if ((dsk_mode == DSKC_THA) || (dsk_mode == DSKC_TWIA))
        dsk_rptr = dsk_rbase + RADDR;                   /* start with addr */
    else dsk_rptr = dsk_rbase + RDATA;                  /* or data */
    return SCPE_OK;
    }
if (dsk_mode == DSKC_CYL) {                             /* cylinder mode? */
    uaptr->TRK = (uaptr->TRK + 1) % dsk_tab[dtyp].trkpa;        /* incr track */
    if (uaptr->TRK % dsk_tab[dtyp].trkpc)               /* not cyl end? */
        return ERR_ENDRC;                               /* don't set end */
    }
ch9_set_end (dsk_ch, 0);                                /* set end */
ch_req |= REQ_CH (dsk_ch);                              /* request channel */
return ERR_ENDRC;
}

/* Write track back to file */

t_stat dsk_wr_trk (UNIT *udptr, uint32 trk)
{
uint32 dtyp = GET_DTYPE (udptr->flags);
uint32 da = DSK_DA (dsk_acc, trk, dtyp);

sim_fseek (udptr->fileref, da, SEEK_SET);
sim_fwrite (dsk_buf, sizeof (t_uint64), dsk_tab[dtyp].wdspt, udptr->fileref);
if (ferror (udptr->fileref)) {
    sim_perror ("DSK I/O error");
    clearerr (udptr->fileref);
    dsk_uend (dsk_ch, DSKS_DSKE);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Synthesize right attention bit from (access * 10 + module) */

t_uint64 dsk_acc_atn (uint32 u)
{
uint32 g, b;

g = u / 4;                                              /* bit group */
b = u % 4;                                              /* bit within group */
return (DSKS_ATN0 >> ((g * 6) + (b? b + 1: 0)));
}

/* Get next format character */

t_bool dsk_get_fmtc (uint32 dtyp, uint8 *fc)
{
uint32 cc = dsk_fmt_cntr % 6;

if (cc == 0) {                                          /* start of word? */
    if (dsk_chob_v)                                     /* valid? clear */
        dsk_chob_v = 0;
    else if (!dsk_stop)                                 /* no, no stop? io chk */
        ch9_set_ioc (dsk_ch);
    }
*fc = ((uint8) (dsk_chob >> ((5 - cc) * 6))) & 077;     /* get character */
if ((cc == 5) && !dsk_stop)                             /* end of word? */
    ch_req |= REQ_CH (dsk_ch);
if (dsk_fmt_cntr++ >= dsk_tab[dtyp].fchpt) {            /* track overflow? */
    dsk_uend (dsk_ch, DSKS_FMTC);                       /* format check */
    return FALSE;
    }
return TRUE;
}

/* Unusual end (set status and stop) */

t_stat dsk_uend (uint32 ch, t_uint64 stat)
{
dsk_sns |= stat;
dsk_sns &= ~(DSKS_PCHK|DSKS_DCHK|DSKS_EXCC);
if (dsk_sns & DSKS_PALL)
    dsk_sns |= DSKS_PCHK;
if (dsk_sns & DSKS_DALL)
    dsk_sns |= DSKS_DCHK;
if (dsk_sns & DSKS_EALL)
    dsk_sns |= DSKS_EXCC;
ch9_set_end (ch, CHINT_UEND);
ch_req |= REQ_CH (ch);
dsk_sta = DSK_IDLE;
return SCPE_OK;
}

/* Test for done */

t_bool dsk_qdone (uint32 ch)
{
if (dsk_stop || !ch9_qconn (ch)) {                      /* stop or err disc? */
    dsk_sta = DSK_IDLE;                                 /* disk is idle */
    return TRUE;
    }
return FALSE;
}

/* Reset */

t_stat dsk_reset (DEVICE *dptr)
{
uint32 i;
UNIT *uptr;

dsk_acc = 0;
dsk_mod = 0;
dsk_rec = 0;
dsk_mode = 0;
dsk_wchk = 0;
dsk_sns = 0;
dsk_cmd = 0;
dsk_sta = DSK_IDLE;
dsk_rbase = 0;
dsk_rptr = 0;
dsk_rlim = 0;
dsk_stop = 0;
dsk_fmt_cntr = 0;
dsk_chob = 0;
dsk_chob_v = 0;
for (i = 0; i < DSK_BUFSIZ; i++)
    dsk_buf[i] = 0;
for (i = 0; i <= (2 * DSK_NUMDR); i++) {
    uptr = dsk_dev.units + i;
    sim_cancel (uptr);
    uptr->TRK = 0;
    uptr->SKF = 0;
    }
return SCPE_OK;
}

/* Attach routine, test formating */

t_stat dsk_attach (UNIT *uptr, CONST char *cptr)
{
uint32 dtyp = GET_DTYPE (uptr->flags);
t_stat r;

uptr->capac = dsk_tab[dtyp].size;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->TRK = 0;
uptr->SKF = 0;
uptr->flags &= ~(UNIT_INOP0|UNIT_INOP1);
return dsk_show_format (stdout, uptr, 0, NULL);
}

/* Set disk size */

t_stat dsk_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 dtyp = GET_DTYPE (val);
uint32 u = uptr - dsk_dev.units;
UNIT *u1;

if (u & 1)
    return SCPE_ARG;
u1 = dsk_dev.units + u + 1;
if ((uptr->flags & UNIT_ATT) || (u1->flags & UNIT_ATT))
    return SCPE_ALATT;
if (val == TYPE_7320)
    u1->flags = (u1->flags & ~UNIT_DISABLE) | UNIT_DIS;
else {
    u1->flags = (u1->flags & ~UNIT_TYPE) | val | UNIT_DISABLE;
    u1->capac = dsk_tab[dtyp].size;
    }
uptr->capac = dsk_tab[dtyp].size;
return SCPE_OK;
}

/* Show format */

t_stat dsk_show_format (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 a, t, k, u, tlim, dtyp, da;
uint32 rptr, rlnt, rlim, rec, ctptr, *format;
uint32 minrsz = DSK_BUFSIZ;
uint32 maxrsz = 0;
uint32 minrno = DSK_BUFSIZ;
uint32 maxrno = 0;
t_bool ctss;
t_uint64 dbuf[DSK_BUFSIZ];
DEVICE *dptr;

if (uptr == NULL)
    return SCPE_IERR;
if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
u = uptr - dptr->units;

dtyp = GET_DTYPE (uptr->flags);
if ((dtyp == TYPE_7320) || (dtyp == TYPE_1301))
    format = ctss_fmt_7320;
else format = ctss_fmt_1302;
for (a = 0, ctss = TRUE; a < dsk_tab[dtyp].accpm; a++) {
    if (val)
        tlim = dsk_tab[dtyp].trkpa;
    else tlim = 1;
    for (t = 0; t < tlim; t++) {
        da = DSK_DA (a, t, dtyp);                       /* get disk address */
        sim_fseek (uptr->fileref, da, SEEK_SET);        /* read track */
        k = sim_fread (dbuf, sizeof (t_uint64), dsk_tab[dtyp].wdspt, uptr->fileref);
        if (ferror (uptr->fileref))                     /* error? */
            return SCPE_IOERR;
        for ( ; k < dsk_tab[dtyp].wdspt; k++)
            dbuf[k] = 0;
        rptr = T1STREC;
        rlnt = (uint32) dbuf[rptr + RLNT];
        if (dbuf[THA2] != CTSS_HA2)
            ctss = FALSE;
        if (rlnt == 0) {
            if (a || t)
                fprintf (st, "Unformatted track, unit = %d, access = %d, track = %d\n", u, a, t);
            else fprintf (st, "Unit %d is unformatted\n", u);
            return SCPE_OK;
            }
        for (rec = 0, ctptr = 0; rlnt != 0; rec++) {
            if ((format[ctptr] == 0) || format[ctptr++] != rlnt)
                ctss = FALSE;
            rlim = rptr + rlnt + RDATA;
            if (rlim >= dsk_tab[dtyp].wdspt) {
                fprintf (st, "Invalid record length %d, unit = %d, access = %d, track = %d, record = %d\n",
                    rlnt, u, a, t, rec);
                return SCPE_OK;
                }
            if (rlnt > maxrsz)
                maxrsz = rlnt;
            if (rlnt < minrsz)
                minrsz = rlnt;
            rptr = rlim;
            rlnt = (uint32) dbuf[rptr + RLNT];
            }
        if (format[ctptr] != 0)
            ctss = FALSE;
        if (rec > maxrno)
            maxrno = rec;
        if (rec < minrno)
            minrno = rec;
        }
    }
if (val == 0)
    return SCPE_OK;
if (ctss)
    fprintf (st, "CTSS format\n");
else if ((minrno == maxrno) && (minrsz == maxrsz))
    fprintf (st, "Valid fixed format, records/track = %d, record size = %d\n",
             minrno, minrsz);
else if (minrsz == maxrsz)
    fprintf (st, "Valid variable format, records/track = %d-%d, record size = %d\n",
             minrno, maxrno, minrsz);
else if (minrno == maxrno)
    fprintf (st, "Valid variable format, records/track = %d, record sizes = %d-%d\n",
             minrno, minrsz, maxrsz);
else fprintf (st, "Valid variable format, records/track = %d-%d, record sizes = %d-%d\n",
              minrno, maxrno, minrsz, maxrsz);
return SCPE_OK;
}
