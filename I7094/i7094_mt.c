/* i7094_mt.c: IBM 7094 magnetic tape simulator

   Copyright (c) 2003-2012, Robert M Supnik

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

   mt           magtape simulator

   19-Mar-12    RMS     Fixed declaration of sel_name (Mark Pizzolato)
   16-Jul-10    RMS     Fixed handling of BSR, BSF (Dave Pitts)
*/

#include "i7094_defs.h"
#include "sim_tape.h"

#define UST             u3                              /* unit state */
#define UCH             u4                              /* channel number */
#define MTUF_V_LDN      (MTUF_V_UF + 0)
#define MTUF_LDN        (1 << MTUF_V_LDN)
#define MT_MAXFR        ((1 << 18) + 2)

#define QCHRONO(c,u)    ((cpu_model & I_CT) && \
                         ((c) == CHRONO_CH) && ((u) == CHRONO_UNIT))

uint8 mtxb[NUM_CHAN][MT_MAXFR + 6];                     /* xfer buffer */
uint32 mt_unit[NUM_CHAN];                               /* unit */
uint32 mt_bptr[NUM_CHAN];
uint32 mt_blnt[NUM_CHAN];
t_uint64 mt_chob[NUM_CHAN];
uint32 mt_chob_v[NUM_CHAN];
uint32 mt_tshort = 2;                                   /* "a few microseconds" */
uint32 mt_twef = 25000;                                 /* 50 msec */
uint32 mt_tstart = 29000;                               /* 58 msec */
uint32 mt_tstop = 10000;                                /* 20 msec */
uint32 mt_tword = 50;                                   /* 125 usec */

static const uint8 odd_par[64] = {
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1
    };

static const char *tape_stat[] = {
    "OK", "TMK", "UNATT", "IOERR", "INVRECLNT",
    "FMT", "BOT", "EOM", "RECERR", "WRPROT"
    };

extern uint32 PC;
extern uint32 cpu_model;
extern uint32 ind_ioc;
extern const char *sel_name[];

t_stat mt_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat mt_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_stat mt_rec_end (UNIT *uptr);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, CONST char *cptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_map_err (UNIT *uptr, t_stat st);

extern uint32 chrono_rd (uint8 *buf, uint32 bufsiz);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit list
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

DIB mt_dib = { &mt_chsel, &mt_chwr };

MTAB mt_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write ring in place" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "no Write ring in place" },
    { MTUF_LDN, 0, "high density", "HIGH", NULL },
    { MTUF_LDN, MTUF_LDN, "low density", "LOW", NULL },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { 0 }
    };

UNIT mta_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mta_reg[] = {
    { ORDATA (UNIT, mt_unit[0], 5) },
    { ORDATA (CHOB, mt_chob[0], 36) },
    { FLDATA (CHOBV, mt_chob_v[0], 0) },
    { DRDATA (BPTR, mt_bptr[0], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[0], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[0], 8, 7, sizeof (mtxb[0])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mta_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mta_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mtb_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mtb_reg[] = {
    { ORDATA (UNIT, mt_unit[1], 5) },
    { ORDATA (CHOB, mt_chob[1], 36) },
    { FLDATA (CHOBV, mt_chob_v[1], 0) },
    { DRDATA (BPTR, mt_bptr[1], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[1], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[1], 8, 7, sizeof (mtxb[1])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mtb_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mtb_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mtc_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mtc_reg[] = {
    { ORDATA (UNIT, mt_unit[2], 5) },
    { ORDATA (CHOB, mt_chob[2], 36) },
    { FLDATA (CHOBV, mt_chob_v[2], 0) },
    { DRDATA (BPTR, mt_bptr[2], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[2], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[2], 8, 7, sizeof (mtxb[2])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mtc_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mtc_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mtd_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mtd_reg[] = {
    { ORDATA (UNIT, mt_unit[3], 5) },
    { ORDATA (CHOB, mt_chob[3], 36) },
    { FLDATA (CHOBV, mt_chob_v[3], 0) },
    { DRDATA (BPTR, mt_bptr[3], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[3], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[3], 8, 7, sizeof (mtxb[3])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mtd_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mtd_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mte_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mte_reg[] = {
    { ORDATA (UNIT, mt_unit[4], 5) },
    { ORDATA (CHOB, mt_chob[4], 36) },
    { FLDATA (CHOBV, mt_chob_v[4], 0) },
    { DRDATA (BPTR, mt_bptr[4], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[4], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[4], 8, 7, sizeof (mtxb[4])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mte_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mte_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mtf_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mtf_reg[] = {
    { ORDATA (UNIT, mt_unit[5], 5) },
    { ORDATA (CHOB, mt_chob[5], 36) },
    { FLDATA (CHOBV, mt_chob_v[5], 0) },
    { DRDATA (BPTR, mt_bptr[5], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[5], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[5], 8, 7, sizeof (mtxb[5])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mtf_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mtf_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mtg_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mtg_reg[] = {
    { ORDATA (UNIT, mt_unit[6], 5) },
    { ORDATA (CHOB, mt_chob[6], 36) },
    { FLDATA (CHOBV, mt_chob_v[6], 0) },
    { DRDATA (BPTR, mt_bptr[6], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[6], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[6], 8, 7, sizeof (mtxb[6])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mtg_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mtg_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

UNIT mth_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_ROABLE+UNIT_DISABLE, 0) }
    };

REG mth_reg[] = {
    { ORDATA (UNIT, mt_unit[7], 5) },
    { ORDATA (CHOB, mt_chob[7], 36) },
    { FLDATA (CHOBV, mt_chob_v[7], 0) },
    { DRDATA (BPTR, mt_bptr[7], 16), PV_LEFT },
    { DRDATA (BLNT, mt_blnt[7], 16), PV_LEFT },
    { BRDATA (BUF, mtxb[7], 8, 7, sizeof (mtxb[7])) },
    { DRDATA (TWEF, mt_twef, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSHORT, mt_tshort, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTART, mt_tstart, 24), REG_NZ + PV_LEFT },
    { DRDATA (TSTOP, mt_tstop, 24), REG_NZ + PV_LEFT },
    { DRDATA (TWORD, mt_tword, 24), REG_NZ + PV_LEFT },
    { URDATA (UST, mth_unit[0].UST, 8, 5, 0, MT_NUMDR + 1, 0) },
    { URDATA (POS, mth_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR + 1, PV_LEFT | REG_RO) },
    { NULL }
    };

DEVICE mt_dev[NUM_CHAN] = {
    {
    "MTA", mta_unit, mta_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    &mt_boot, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DEBUG | DEV_TAPE
    },
    {
    "MTB", mtb_unit, mtb_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTC", mtc_unit, mtc_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTD", mtd_unit, mtd_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTE", mte_unit, mte_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTF", mtf_unit, mtf_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTG", mtg_unit, mtg_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    },
    {
    "MTH", mth_unit, mth_reg, mt_mod,
    MT_NUMDR + 1, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &sim_tape_detach,
    &mt_dib, DEV_DIS|DEV_DEBUG
    }
    };

/* Select controller

   Inputs:
        ch      =       channel
        cmd     =       select command
        unit    =       unit
   Outputs:
        status  =       SCPE_OK if ok
                        STOP_STALL if busy
                        error code if error
*/

static const int mt_must_att[CHSL_NUM] = {
    0, 1, 1, 0, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1, 0, 0
    };

static const int mt_will_wrt[CHSL_NUM] = {
    0, 0, 1, 0, 0, 1, 0, 0,
    1, 1, 0, 0, 0, 0, 0, 0
    };

t_stat mt_chsel (uint32 ch, uint32 cmd, uint32 unit)
{
UNIT *uptr;
uint32 u = unit & 017;

if ((ch >= NUM_CHAN) || (cmd == 0) || (cmd >= CHSL_NUM))
    return SCPE_IERR;                                   /* invalid arg? */
if (mt_dev[ch].flags & DEV_DIS)                         /* disabled? */
    return STOP_NXDEV;
if ((u == 0) || (u > MT_NUMDR))                         /* valid unit? */
    return STOP_NXDEV;
uptr = mt_dev[ch].units + u;                            /* get unit ptr */
if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return STOP_NXDEV;
if (mt_unit[ch] || sim_is_active (uptr))                /* ctrl or unit busy? */
    return ERR_STALL;                                   /* stall */
if (QCHRONO (ch, u)) {                                  /* Chronolog clock? */
    if (cmd != CHSL_RDS)                                /* only reads */
        return STOP_ILLIOP;
    sim_activate (uptr, mt_tword);                      /* responds quickly */
    }
else {                                                  /* real tape */
    if (!(uptr->flags & UNIT_ATT) && mt_must_att[cmd])  /* unit unatt? */
        return SCPE_UNATT;
    if (sim_tape_wrp (uptr) && mt_will_wrt[cmd])        /* unit wrp && write? */
        return STOP_WRP;
    if (DEBUG_PRS (mt_dev[ch]))
        fprintf (sim_deb, ">>%s%d %s, pos = %d\n",
                 mt_dev[ch].name, u, sel_name[cmd], uptr->pos);

    switch (cmd) {                                      /* case on cmd */

    case CHSL_RDS:
    case CHSL_WRS:
        sim_activate (uptr, mt_tstart);                 /* schedule op */
        break;

    case CHSL_WEF:                                      /* write eof? */
        sim_activate (uptr, mt_twef);                   /* schedule op */
        break;

    case CHSL_BSR:
    case CHSL_BSF:                                      /* backspace */
    case CHSL_REW:
    case CHSL_RUN:
    case CHSL_SDN:                                      /* rew, rew/unl, set det */
        sim_activate (uptr, mt_tshort);                 /* schedule quick event */
        break;

    default:
        return SCPE_IERR;
        }                                               /* end switch */
    }                                                   /* end else */

uptr->UST = cmd;                                        /* set cmd */
mt_unit[ch] = unit & 0777;                              /* save unit */
return SCPE_OK;
}

/* Channel write routine */

t_stat mt_chwr (uint32 ch, t_uint64 val, uint32 eorfl)
{
int32 k, u;
uint8 by, *xb;
UNIT *uptr;

if (ch >= NUM_CHAN)                                     /* invalid chan? */
    return SCPE_IERR;
xb = mtxb[ch];                                          /* get xfer buf */
u = mt_unit[ch] & 017;
if ((xb == NULL) || (u > MT_NUMDR))                     /* invalid args? */
    return SCPE_IERR;
uptr = mt_dev[ch].units + u;                            /* get unit */
mt_chob[ch] = val & DMASK;                              /* save word from chan */
mt_chob_v[ch] = 1;                                      /* set valid */

if (uptr->UST == (CHSL_WRS|CHSL_2ND)) {                 /* data write? */
    for (k = 30;                                        /* proc 6 bytes */
        (k >= 0) && (mt_bptr[ch] < MT_MAXFR);
         k = k - 6) {
        by = (uint8) ((val >> k) & 077);                /* get byte */
        if ((mt_unit[ch] & 020) == 0) {                 /* BCD? */
            if (by == 0)                                /* cvt bin 0 */
                by = BCD_ZERO;
            else if (by & 020)                          /* invert zones */
                by = by ^ 040;
            if (!odd_par[by])                           /* even parity */
                by = by | 0100;
            }
        else if (odd_par[by])                           /* bin, odd par */
            by = by | 0100;
        xb[mt_bptr[ch]++] = by;                         /* put in buffer */
        }
    if (eorfl)
        return mt_rec_end (uptr);                       /* EOR? write rec */
    return SCPE_OK;
    }
return SCPE_IERR;
}

/* Unit timeout */

t_stat mt_svc (UNIT *uptr)
{
uint32 i, u, ch = uptr->UCH;                            /* get channel number */
uint8 by, *xb = mtxb[ch];                               /* get xfer buffer */
t_uint64 dat;
t_mtrlnt bc;
t_stat r;

if (xb == NULL)                                         /* valid buffer? */
    return SCPE_IERR;
u = uptr - mt_dev[ch].units;
switch (uptr->UST) {                                    /* case on state */

    case CHSL_RDS:                                      /* read start */
        if (QCHRONO (ch, mt_unit[ch] & 017))            /* Chronolog clock? */
            bc = chrono_rd (xb, MT_MAXFR);              /* read clock */
        else {                                          /* real tape */
            r = sim_tape_rdrecf (uptr, xb, &bc, MT_MAXFR); /* read record */
            if ((r = mt_map_err (uptr, r)))             /* map status */
                return r;
            if (mt_unit[ch] == 0)                       /* disconnected? */
                return SCPE_OK;
            }                                           /* end else Chrono */
        if (!ch6_qconn (ch, mt_unit[ch])) {             /* chan disconnected? */
            mt_unit[ch] = 0;                            /* clr ctrl busy */
            return SCPE_OK;
            }
        for (i = bc; i < (bc + 6); i++)                 /* extra 0's */
            xb[i] = 0;
        mt_bptr[ch] = 0;                                /* set ptr, lnt */
        mt_blnt[ch] = bc;
        uptr->UST = CHSL_RDS|CHSL_2ND;                  /* next state */
        sim_activate (uptr, mt_tword);
        break;

    case CHSL_RDS|CHSL_2ND:                             /* read word */
        for (i = 0, dat = 0; i < 6; i++) {              /* proc 6 bytes */
            by = xb[mt_bptr[ch]++] & 077;               /* get next byte */
            if ((mt_unit[ch] & 020) == 0) {             /* BCD? */
                if (by == BCD_ZERO)                     /* cvt BCD 0 */
                    by = 0;
                else if (by & 020)                      /* invert zones */
                    by = by ^ 040;
                }
            dat = (dat << 6) | ((t_uint64) by);
            }
        if (mt_bptr[ch] >= mt_blnt[ch]) {               /* end of record? */
            ch6_req_rd (ch, mt_unit[ch], dat, CH6DF_EOR);
            uptr->UST = CHSL_RDS|CHSL_3RD;              /* next state */
            sim_activate (uptr, mt_tstop);              /* long timing */
            }
        else {
            ch6_req_rd (ch, mt_unit[ch], dat, 0);       /* send to channel */
            sim_activate (uptr, mt_tword);              /* next word */
            }
        break;

    case CHSL_RDS|CHSL_3RD:                             /* end record */
        if (ch6_qconn (ch, mt_unit[ch])) {              /* ch still conn? */
            uptr->UST = CHSL_RDS;                       /* initial state */
            sim_activate (uptr, mt_tshort);             /* sched next record */
            }
        else mt_unit[ch] = 0;                           /* clr ctrl busy */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d RDS complete, pos = %d, %s\n",
                     mt_dev[ch].name, u, uptr->pos,
                     mt_unit[ch]? "continuing": "disconnecting");
        return SCPE_OK;

    case CHSL_WRS:                                      /* write start */
        if (!ch6_qconn (ch, mt_unit[ch])) {             /* chan disconnected? */
            mt_unit[ch] = 0;                            /* clr ctrl busy */
            return SCPE_OK;                             /* (writes blank tape) */
            }
        mt_bptr[ch] = 0;                                /* init buffer */
        uptr->UST = CHSL_WRS|CHSL_2ND;                  /* next state */
        ch6_req_wr (ch, mt_unit[ch]);                   /* request channel */
        mt_chob[ch] = 0;                                /* clr, inval buffer */
        mt_chob_v[ch] = 0;
        sim_activate (uptr, mt_tword);                  /* wait for word */
        break;

    case CHSL_WRS|CHSL_2ND:                             /* write word */
        if (!ch6_qconn (ch, mt_unit[ch]))               /* disconnected? */
            return mt_rec_end (uptr);                   /* write record */
        if (mt_chob_v[ch])                              /* valid? clear */
            mt_chob_v[ch] = 0;
        else ind_ioc = 1;                               /* no, io check */
        ch6_req_wr (ch, mt_unit[ch]);                   /* request channel */
        sim_activate (uptr, mt_tword);                  /* next word */
        break;

    case CHSL_WRS|CHSL_3RD:                             /* write stop */
        if (ch6_qconn (ch, mt_unit[ch])) {              /* chan active? */
            uptr->UST = CHSL_WRS;                       /* initial state */
            sim_activate (uptr, mt_tshort);             /* sched next record */
            }
        else mt_unit[ch] = 0;                           /* clr ctrl busy */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d WRS complete, pos = %d, %s\n",
                     mt_dev[ch].name, u, uptr->pos,
                     mt_unit[ch]? "continuing": "disconnecting");
        return SCPE_OK;

    case CHSL_BSR: case CHSL_BSF:                       /* backspace */
        uptr->UST = uptr->UST | CHSL_2ND;               /* set 2nd state */
        sim_activate (uptr, mt_tstart);                 /* reactivate */
        ch6_end_nds (ch);                               /* disconnect */
        return SCPE_OK;

    case CHSL_BSR|CHSL_2ND:                             /* backspace rec */
        r = sim_tape_sprecr (uptr, &bc);                /* space backwards */
        mt_unit[ch] = 0;                                /* clr ctrl busy */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d BSR complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        if (r == MTSE_TMK)                              /* allow tape mark */
            return SCPE_OK;
        return mt_map_err (uptr, r);

    case CHSL_BSF|CHSL_2ND:                             /* backspace file */
        while ((r = sim_tape_sprecr (uptr, &bc)) == MTSE_OK) ;
        mt_unit[ch] = 0;                                /* clr ctrl busy */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d BSF complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        if (r == MTSE_TMK)                              /* allow tape mark */
            return SCPE_OK;
        return mt_map_err (uptr, r);                    /* map others */

    case CHSL_WEF:                                      /* write eof */
        r = sim_tape_wrtmk (uptr);                      /* write tape mark */
        mt_unit[ch] = 0;                                /* clr ctrl busy */
        ch6_end_nds (ch);                               /* disconnect */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d WEF complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        return mt_map_err (uptr, r);

    case CHSL_REW: case CHSL_RUN:                       /* rewind, unload */
        uptr->UST = uptr->UST | CHSL_2ND;               /* set 2nd state */
        sim_activate (uptr, mt_tstart);                 /* reactivate */
        mt_unit[ch] = 0;                                /* clr ctrl busy */
        ch6_end_nds (ch);                               /* disconnect */
        return SCPE_OK;

    case CHSL_REW | CHSL_2ND:
        sim_tape_rewind (uptr);
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d REW complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        return SCPE_OK;

    case CHSL_RUN | CHSL_2ND:
        sim_tape_detach (uptr);
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d RUN complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        return SCPE_OK;

    case CHSL_SDN:
        if (mt_unit[ch] & 020)                          /* set density flag */
            uptr->flags = uptr-> flags & ~MTUF_LDN;
        else uptr->flags = uptr->flags | MTUF_LDN;
        mt_unit[ch] = 0;                                /* clr ctrl busy */
        ch6_end_nds (ch);                               /* disconnect */
        if (DEBUG_PRS (mt_dev[ch]))
            fprintf (sim_deb, ">>%s%d SDN complete, pos = %d\n",
                     mt_dev[ch].name, u, uptr->pos);
        return SCPE_OK;

    default:
        return SCPE_IERR;
        }

return SCPE_OK;
}

/* End record routine */

t_stat mt_rec_end (UNIT *uptr)
{
uint32 ch = uptr->UCH;
uint8 *xb = mtxb[ch];
t_stat r;

if (mt_bptr[ch]) {                                      /* any data? */
    if (xb == NULL)
        return SCPE_IERR;
    r = sim_tape_wrrecf (uptr, xb, mt_bptr[ch]);        /* write record */
    if ((r = mt_map_err (uptr, r)))                     /* map error */
        return r;
    }
uptr->UST = CHSL_WRS|CHSL_3RD;                          /* next state */
sim_cancel (uptr);                                      /* cancel current */
sim_activate (uptr, mt_tstop);                          /* long timing */
return SCPE_OK;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
uint32 ch = uptr->UCH;
uint32 u = mt_unit[ch];
uint32 up = uptr - mt_dev[ch].units;

if ((st != MTSE_OK) && DEBUG_PRS (mt_dev[ch]))
    fprintf (sim_deb, ">>%s%d status = %s, pos = %d\n",
             mt_dev[ch].name, up, tape_stat[st], uptr->pos);

switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
        ch6_err_disc (ch, u, CHF_TRC);
        mt_unit[ch] = 0;                                /* disconnect */
        return SCPE_IERR;

    case MTSE_IOERR:                                    /* IO error */
        ch6_err_disc (ch, u, CHF_TRC);
        mt_unit[ch] = 0;                                /* disconnect */
        return SCPE_IOERR;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        ch6_err_disc (ch, u, CHF_TRC);
        mt_unit[ch] = 0;                                /* disconnect */
        return SCPE_MTRLNT;

    case MTSE_WRP:                                      /* write protect */
        ch6_err_disc (ch, u, 0);
        mt_unit[ch] = 0;                                /* disconnect */
        return STOP_WRP;

    case MTSE_EOM:                                      /* end of medium */
    case MTSE_TMK:                                      /* tape mark */
        ch6_err_disc (ch, u, CHF_EOF);
        mt_unit[ch] = 0;                                /* disconnect */
        break;

    case MTSE_RECE:                                     /* record in error */
        ch6_set_flags (ch, u, CHF_TRC);
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        ch6_set_flags (ch, u, CHF_BOT);
        break;

    case MTSE_OK:                                       /* no error */
        break;
        }

return SCPE_OK;
}

/* Magtape reset */

t_stat mt_reset (DEVICE *dptr)
{
uint32 ch = dptr - &mt_dev[0];
uint32 j;
UNIT *uptr;

mt_unit[ch] = 0;                                        /* clear busy */
mt_bptr[ch] = 0;                                        /* clear buf ptrs */
mt_blnt[ch] = 0;
mt_chob[ch] = 0;
mt_chob_v[ch] = 0;
for (j = 1; j <= MT_NUMDR; j++) {                       /* for all units */
    uptr = dptr->units + j;
    uptr->UST = 0;                                      /* clear state */
    uptr->UCH = ch;
    sim_cancel (uptr);                                  /* stop activity */
    }                                                   /* end for */
return SCPE_OK;                                         /* done */
}

/* Magtape attach */

t_stat mt_attach (UNIT *uptr, CONST char *cptr)
{
uptr->flags = uptr->flags & ~MTUF_LDN;                  /* start as hi den */
return sim_tape_attach (uptr, cptr);
}

/* Magtape boot */

#define BOOT_START      01000

static const t_uint64 boot_rom[5] = {
    INT64_C(0076200000000) + U_MTBIN - 1,               /* RDS MT_binary */
    INT64_C(0054000000000) + BOOT_START + 4,            /* RCHA *+3 */
    INT64_C(0054400000000),                             /* LCHA 0 */
    INT64_C(0002100000001),                             /* TTR 1 */
    INT64_C(0500003000000),                             /* IOCT 0,,3 */
    };

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
uint32 i, chan;
extern t_uint64 *M;

chan = dptr - &mt_dev[0] + 1;
WriteP (BOOT_START, boot_rom[0] + unitno + (chan << 9));
for (i = 1; i < 5; i++)
    WriteP (BOOT_START + i, boot_rom[i]);
PC = BOOT_START;
return SCPE_OK;
}
