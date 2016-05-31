/* pdp18b_rb.c: RB09 fixed head disk simulator

   Copyright (c) 2003-2016, Robert M Supnik

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

   rb           RB09 fixed head disk

   07-Mar-16    RMS     Revised for dynamically allocated memory
   03-Sep-13    RMS     Added explicit void * cast
   14-Jan-04    RMS     Revised IO device call interface
   26-Oct-03    RMS     Cleaned up buffer copy code

   The RB09 is a head-per-track disk.  It uses the single cycle data break
   facility.  To minimize overhead, the entire RB09 is buffered in memory.

   Two timing parameters are provided:

   rb_time      Interword timing.  Must be non-zero.
   rb_burst     Burst mode.  If 0, DMA occurs cycle by cycle; otherwise,
                DMA occurs in a burst.
*/

#include "pdp18b_defs.h"
#include <math.h>

/* Constants */

#define RB_NUMWD        64                              /* words/sector */
#define RB_NUMSC        80                              /* sectors/track */
#define RB_NUMTR        200                             /* tracks/disk */
#define RB_WLKTR        10                              /* tracks/wlock switch */
#define RB_SIZE         (RB_NUMTR * RB_NUMSC * RB_NUMWD) /* words/drive */

/* Function/status register */

#define RBS_ERR         0400000                         /* error */
#define RBS_PAR         0200000                         /* parity error */
#define RBS_ILA         0100000                         /* ill addr error */
#define RBS_TIM         0040000                         /* timing transfer */
#define RBS_NRY         0020000                         /* not ready error */
#define RBS_DON         0010000                         /* done */
#define RBS_IE          0004000                         /* int enable */
#define RBS_BSY         0002000                         /* busy */
#define RBS_WR          0001000                         /* read/write */
#define RBS_XOR         (RBS_IE|RBS_BSY|RBS_WR)         /* set by XOR */
#define RBS_MBZ         0000777                         /* always clear */
#define RBS_EFLGS       (RBS_PAR|RBS_ILA|RBS_TIM|RBS_NRY)       /* error flags */

/* BCD disk address */

#define RBA_V_TR        8
#define RBA_M_TR        0x1FF
#define RBA_V_SC        0
#define RBA_M_SC        0xFF
#define RBA_GETTR(x)    (((x) >> RBA_V_TR) & RBA_M_TR)
#define RBA_GETSC(x)    (((x) >> RBA_V_SC) & RBA_M_SC)

#define GET_POS(x)      ((int) fmod (sim_gtime () / ((double) (x)), \
                        ((double) (RB_NUMSC * RB_NUMWD))))

extern int32 *M;
extern int32 int_hwre[API_HLVL+1];
extern UNIT cpu_unit;

int32 rb_sta = 0;                                       /* status register */
int32 rb_da = 0;                                        /* disk address */
int32 rb_ma = 0;                                        /* current addr */
int32 rb_wc = 0;                                        /* word count */
int32 rb_wlk = 0;                                       /* write lock */
int32 rb_time = 10;                                     /* inter-word time */
int32 rb_burst = 1;                                     /* burst mode flag */
int32 rb_stopioe = 1;                                   /* stop on error */

int32 rb71 (int32 dev, int32 pulse, int32 AC);
t_stat rb_svc (UNIT *uptr);
t_stat rb_reset (DEVICE *dptr);
int32 rb_updsta (int32 val);
int32 rb_make_da (int32 dat);
int32 rb_make_bcd (int32 dat);
int32 rb_set_da (int32 dat, int32 old);
int32 rb_set_bcd (int32 dat);

/* RB data structures

   rb_dev       RF device descriptor
   rb_unit      RF unit descriptor
   rb_reg       RF register list
*/

DIB rb_dib = { DEV_RB, 1, NULL, { &rb71 } };

UNIT rb_unit = {
    UDATA (&rb_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF,
           RB_SIZE)
    };

REG rb_reg[] = {
    { ORDATA (STA, rb_sta, 18) },
    { ORDATA (DA, rb_da, 20) },
    { ORDATA (WC, rb_wc, 16) },
    { ORDATA (MA, rb_ma, ADDRSIZE) },
    { FLDATA (INT, int_hwre[API_RB], INT_V_RB) },
    { ORDATA (WLK, rb_wlk, RB_NUMTR / RB_WLKTR) },
    { DRDATA (TIME, rb_time, 24), PV_LEFT + REG_NZ },
    { FLDATA (BURST, rb_burst, 0) },
    { FLDATA (STOP_IOE, rb_stopioe, 0) },
    { ORDATA (DEVNO, rb_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rb_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
    { 0 }
    };

DEVICE rb_dev = {
    "RB", &rb_unit, rb_reg, rb_mod,
    1, 8, 21, 1, 8, 18,
    NULL, NULL, &rb_reset,
    NULL, NULL, NULL,
    &rb_dib, DEV_DIS | DEV_DISABLE
    };

/* IOT routines */

int32 rb71 (int32 dev, int32 pulse, int32 AC)
{
int32 tow, t, sb = pulse & 060;

if (pulse & 001) {
    if (sb == 000)                                      /* DBCF */
        rb_sta = rb_sta & ~(RBS_ERR | RBS_EFLGS | RBS_DON);
    if ((sb == 020) && (rb_sta & (RBS_ERR | RBS_DON)))
        AC = AC | IOT_SKP;                              /* DBSF */
    if (sb == 040)                                      /* DBCS */
        rb_sta = 0;
    }
if (pulse & 002) {
    if (sb == 000)                                      /* DBRD */
        AC = AC | rb_make_da (rb_da);
    if (sb == 020)                                      /* DBRS */
        AC = AC | rb_sta;
    if (sb == 040)                                      /* DBLM */
        rb_ma = AC & AMASK;
    }
if (pulse & 004) {
    if (sb == 000)                                      /* DBLD */
        rb_da = rb_set_da (AC, rb_da);
    if (sb == 020)                                      /* DBLW */
        rb_wc = AC & 0177777;
    if (sb == 040) {                                    /* DBLS */
        rb_sta = (rb_sta & RBS_XOR) ^ (AC & ~RBS_MBZ);
        if (rb_sta & RBS_BSY) {                         /* busy set? */
            if (!sim_is_active (&rb_unit)) {            /* schedule */
                tow = rb_da % (RB_NUMSC * RB_NUMWD);
                t = tow - GET_POS (rb_time);
                if (t < 0)
                    t = t + (RB_NUMSC * RB_NUMWD);
                sim_activate (&rb_unit, t * rb_time);
                }
            }
        else sim_cancel (&rb_unit);                     /* no, stop */
        }
    }
rb_updsta (0);                                          /* update status */
return AC;
}

int32 rb_make_da (int32 da)
{
int32 t = da / (RB_NUMSC * RB_NUMWD);                   /* bin track */
int32 s = (da % (RB_NUMSC * RB_NUMWD)) / RB_NUMWD;      /* bin sector */
int32 bcd_t = rb_make_bcd (t);                          /* bcd track */
int32 bcd_s = rb_make_bcd (s);                          /* bcd sector */
return (bcd_t << RBA_V_TR) | (bcd_s << RBA_V_SC);
}

int32 rb_set_da (int32 bcda, int32 old_da)
{
int32 bcd_t = RBA_GETTR (bcda);                         /* bcd track */
int32 bcd_s = RBA_GETSC (bcda);                         /* bcd sector */
int32 t = rb_set_bcd (bcd_t);                           /* bin track */
int32 s = rb_set_bcd (bcd_s);                           /* bin sector */

if ((t >= RB_NUMTR) || (t < 0) ||                       /* invalid? */
    (s >= RB_NUMSC) || (s < 0)) {
    rb_updsta (RBS_ILA);                                /* error */
    return old_da;                                      /* don't change */
    }
else return (((t * RB_NUMSC) + s) * RB_NUMWD);          /* new da */
}

int32 rb_make_bcd (int32 bin)
{
int32 d, i, r;

for (r = i = 0; bin != 0; bin = bin / 10) {             /* while nz */
    d = bin % 10;                                       /* dec digit */
    r = r | (d << i);                                   /* insert bcd */
    i = i + 4;
    }
return r;
}

int32 rb_set_bcd (int32 bcd)
{
int32 d, i, r;

for (r = 0, i = 1; bcd != 0; bcd = bcd >> 4) {          /* while nz */
    d = bcd & 0xF;                                      /* bcd digit */
    if (d >= 10)                                        /* invalid? */
        return -1;
    r = r + (d * i);                                    /* insert bin */
    i = i * 10;
    }
return r;
}       

/* Unit service - disk is buffered in memory */

t_stat rb_svc (UNIT *uptr)
{
int32 t, sw;
int32 *fbuf = (int32 *) uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    rb_updsta (RBS_NRY | RBS_DON);                      /* set nxd, done */
    return IORETURN (rb_stopioe, SCPE_UNATT);
    }

do {
    if (rb_sta & RBS_WR) {                              /* write? */
        t = rb_da / (RB_NUMSC * RB_NUMWD);              /* track */
        sw = t / RB_WLKTR;                              /* switch */
        if ((rb_wlk >> sw) & 1) {                       /* write locked? */
            rb_updsta (RBS_ILA | RBS_DON);
            break;
            }
        else {                                          /* not locked */
            fbuf[rb_da] = M[rb_ma];                     /* write word */
            if (((t_addr) rb_da) >= uptr->hwmark)
                uptr->hwmark = rb_da + 1;
            }
        }
    else if (MEM_ADDR_OK (rb_ma))                       /* read, valid addr? */
        M[rb_ma] = fbuf[rb_da];                         /* read word */
    rb_wc = (rb_wc + 1) & 0177777;                      /* incr word count */
        rb_ma = (rb_ma + 1) & AMASK;                    /* incr mem addr */
    rb_da = rb_da + 1;                                  /* incr disk addr */
    if (rb_da > RB_SIZE)                                /* disk wraparound? */
        rb_da = 0;
    } while ((rb_wc != 0) && (rb_burst != 0));          /* brk if wc, no brst */

if ((rb_wc != 0) && ((rb_sta & RBS_ERR) == 0))          /* more to do? */
    sim_activate (&rb_unit, rb_time);                   /* sched next */
else rb_updsta (RBS_DON);                               /* set done */
return SCPE_OK;
}

/* Update status */

int32 rb_updsta (int32 val)
{
rb_sta = (rb_sta | val) & ~(RBS_ERR | RBS_MBZ);         /* clear err, mbz */
if (rb_sta & RBS_EFLGS)                                 /* error? */
    rb_sta = rb_sta | RBS_ERR;
if (rb_sta & RBS_DON)                                   /* done? clear busy */
    rb_sta = rb_sta & ~RBS_BSY;
if ((rb_sta & (RBS_ERR | RBS_DON)) && (rb_sta & RBS_IE))
     SET_INT (RB);                                      /* set or clr intr */
else CLR_INT (RB);
return rb_sta;
}

/* Reset routine */

t_stat rb_reset (DEVICE *dptr)
{
rb_sta = rb_da = 0;
rb_wc = rb_ma = 0;
rb_updsta (0);
sim_cancel (&rb_unit);
return SCPE_OK;
}
