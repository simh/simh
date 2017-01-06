/* pdp8_rk.c: RK8E cartridge disk simulator

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

   rk           RK8E/RK05 cartridge disk

   17-Sep-13    RMS     Changed to use central set_bootpc routine
   18-Mar-13    RMS     Raised RK_MIN so that RKLFMT will work (Mark Pizzolato)
   25-Apr-03    RMS     Revised for extended file support
   04-Oct-02    RMS     Added DIB, device number support
   06-Jan-02    RMS     Changed enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Converted FLG to array, made register names consistent
   25-Apr-01    RMS     Added device enable/disable support
   29-Jun-96    RMS     Added unit enable/disable support
*/

#include "pdp8_defs.h"

/* Constants */

#define RK_NUMSC        16                              /* sectors/surface */
#define RK_NUMSF        2                               /* surfaces/cylinder */
#define RK_NUMCY        203                             /* cylinders/drive */
#define RK_NUMWD        256                             /* words/sector */
#define RK_SIZE         (RK_NUMCY * RK_NUMSF * RK_NUMSC * RK_NUMWD)
                                                        /* words/drive */
#define RK_NUMDR        4                               /* drives/controller */
#define RK_M_NUMDR      03

/* Flags in the unit flags word */

#define UNIT_V_HWLK     (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_SWLK     (UNIT_V_UF + 1)                 /* swre write lock */
#define UNIT_HWLK       (1 << UNIT_V_HWLK)
#define UNIT_SWLK       (1 << UNIT_V_SWLK)
#define UNIT_WPRT       (UNIT_HWLK|UNIT_SWLK|UNIT_RO)   /* write protect */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */
#define FUNC            u4                              /* function */

/* Status register */

#define RKS_DONE        04000                           /* transfer done */
#define RKS_HMOV        02000                           /* heads moving */
#define RKS_SKFL        00400                           /* drive seek fail */
#define RKS_NRDY        00200                           /* drive not ready */
#define RKS_BUSY        00100                           /* control busy error */
#define RKS_TMO         00040                           /* timeout error */
#define RKS_WLK         00020                           /* write lock error */
#define RKS_CRC         00010                           /* CRC error */
#define RKS_DLT         00004                           /* data late error */
#define RKS_STAT        00002                           /* drive status error */
#define RKS_CYL         00001                           /* cyl address error */
#define RKS_ERR         (RKS_BUSY+RKS_TMO+RKS_WLK+RKS_CRC+RKS_DLT+RKS_STAT+RKS_CYL)

/* Command register */

#define RKC_M_FUNC      07                              /* function */
#define  RKC_READ       0
#define  RKC_RALL       1
#define  RKC_WLK        2
#define  RKC_SEEK       3
#define  RKC_WRITE      4
#define  RKC_WALL       5
#define RKC_V_FUNC      9
#define RKC_IE          00400                           /* interrupt enable */
#define RKC_SKDN        00200                           /* set done on seek done */
#define RKC_HALF        00100                           /* 128W sector */
#define RKC_MEX         00070                           /* memory extension */
#define RKC_V_MEX       3
#define RKC_M_DRV       03                              /* drive select */
#define RKC_V_DRV       1
#define RKC_CYHI        00001                           /* high cylinder addr */

#define GET_FUNC(x)     (((x) >> RKC_V_FUNC) & RKC_M_FUNC)
#define GET_DRIVE(x)    (((x) >> RKC_V_DRV) & RKC_M_DRV)
#define GET_MEX(x)      (((x) & RKC_MEX) << (12 - RKC_V_MEX))

/* Disk address */

#define RKD_V_SECT      0                               /* sector */
#define RKD_M_SECT      017
#define RKD_V_SUR       4                               /* surface */
#define RKD_M_SUR       01
#define RKD_V_CYL       5                               /* cylinder */
#define RKD_M_CYL       0177
#define GET_CYL(x,y)    ((((x) & RKC_CYHI) << (12-RKD_V_CYL)) | \
                        (((y) >> RKD_V_CYL) & RKD_M_CYL))
#define GET_DA(x,y)     ((((x) & RKC_CYHI) << 12) | y)

/* Reset commands */

#define RKX_CLS         0                               /* clear status */
#define RKX_CLC         1                               /* clear control */
#define RKX_CLD         2                               /* clear drive */
#define RKX_CLSA        3                               /* clear status alt */

#define RK_INT_UPDATE   if (((rk_sta & (RKS_DONE + RKS_ERR)) != 0) && \
                            ((rk_cmd & RKC_IE) != 0)) \
                            int_req = int_req | INT_RK; \
                        else int_req = int_req & ~INT_RK
#define RK_MIN          50
#define MAX(x,y)        (((x) > (y))? (x): (y))

extern uint16 M[];
extern int32 int_req, stop_inst;
extern UNIT cpu_unit;

int32 rk_busy = 0;                                      /* controller busy */
int32 rk_sta = 0;                                       /* status register */
int32 rk_cmd = 0;                                       /* command register */
int32 rk_da = 0;                                        /* disk address */
int32 rk_ma = 0;                                        /* memory address */
int32 rk_swait = 10, rk_rwait = 10;                     /* seek, rotate wait */
int32 rk_stopioe = 1;                                   /* stop on error */

int32 rk (int32 IR, int32 AC);
t_stat rk_svc (UNIT *uptr);
t_stat rk_reset (DEVICE *dptr);
t_stat rk_boot (int32 unitno, DEVICE *dptr);
void rk_go (int32 function, int32 cylinder);

/* RK-8E data structures

   rk_dev       RK device descriptor
   rk_unit      RK unit list
   rk_reg       RK register list
   rk_mod       RK modifiers list
*/

DIB rk_dib = { DEV_RK, 1, { &rk } };

UNIT rk_unit[] = {
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) },
    { UDATA (&rk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RK_SIZE) }
    };

REG rk_reg[] = {
    { ORDATAD (RKSTA, rk_sta, 12, "status") },
    { ORDATAD (RKCMD, rk_cmd, 12, "disk command") },
    { ORDATAD (RKDA, rk_da, 12, "disk address") },
    { ORDATAD (RKMA, rk_ma, 12, "current memory address") },
    { FLDATAD (BUSY, rk_busy, 0, "control busy flag") },
    { FLDATAD (INT, int_req, INT_V_RK, "interrupt pending flag") },
    { DRDATAD (STIME, rk_swait, 24, "seek time, per cylinder"), PV_LEFT },
    { DRDATAD (RTIME, rk_rwait, 24, "rotational delay"), PV_LEFT },
    { FLDATAD (STOP_IOE, rk_stopioe, 0, "stop on I/O error") },
    { ORDATA (DEVNUM, rk_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB rk_mod[] = {
    { UNIT_HWLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_HWLK, UNIT_HWLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE rk_dev = {
    "RK", rk_unit, rk_reg, rk_mod,
    RK_NUMDR, 8, 24, 1, 8, 12,
    NULL, NULL, &rk_reset,
    &rk_boot, NULL, NULL,
    &rk_dib, DEV_DISABLE
    };

/* IOT routine */

int32 rk (int32 IR, int32 AC)
{
int32 i;
UNIT *uptr;

switch (IR & 07) {                                      /* decode IR<9:11> */

    case 0:                                             /* unused */
        return (stop_inst << IOT_V_REASON) + AC;

    case 1:                                             /* DSKP */
        return (rk_sta & (RKS_DONE + RKS_ERR))?         /* skip on done, err */
            IOT_SKP + AC: AC;

    case 2:                                             /* DCLR */
        rk_sta = 0;                                     /* clear status */
        switch (AC & 03) {                              /* decode AC<10:11> */

        case RKX_CLS:                                   /* clear status */
            if (rk_busy != 0) rk_sta = rk_sta | RKS_BUSY;
        case RKX_CLSA:                                  /* clear status alt */
            break;

        case RKX_CLC:                                   /* clear control */
            rk_cmd = rk_busy = 0;                       /* clear registers */
            rk_ma = rk_da = 0;
            for (i = 0; i < RK_NUMDR; i++)
                sim_cancel (&rk_unit[i]);
            break;

        case RKX_CLD:                                   /* reset drive */
            if (rk_busy != 0)
                rk_sta = rk_sta | RKS_BUSY;
            else rk_go (RKC_SEEK, 0);                   /* seek to 0 */
            break;
            }                                           /* end switch AC */
        break;

    case 3:                                             /* DLAG */
        if (rk_busy != 0)
            rk_sta = rk_sta | RKS_BUSY;
        else {
            rk_da = AC;                                 /* load disk addr */
            rk_go (GET_FUNC (rk_cmd), GET_CYL (rk_cmd, rk_da));
            }
        break;

    case 4:                                             /* DLCA */
        if (rk_busy != 0)
            rk_sta = rk_sta | RKS_BUSY;
        else rk_ma = AC;                                /* load curr addr */
        break;

    case 5:                                             /* DRST */
        uptr = rk_dev.units + GET_DRIVE (rk_cmd);       /* selected unit */
        rk_sta = rk_sta & ~(RKS_HMOV + RKS_NRDY);       /* clear dynamic */
        if ((uptr->flags & UNIT_ATT) == 0)
            rk_sta = rk_sta | RKS_NRDY;
        if (sim_is_active (uptr))
            rk_sta = rk_sta | RKS_HMOV;
        return rk_sta;

    case 6:                                             /* DLDC */
        if (rk_busy != 0)
            rk_sta = rk_sta | RKS_BUSY;
        else {
            rk_cmd = AC;                                /* load command */
            rk_sta = 0;                                 /* clear status */
            }
        break;

    case 7:                                             /* DMAN */
        break;
        }                                               /* end case pulse */

RK_INT_UPDATE;                                          /* update int req */
return 0;                                               /* clear AC */
}

/* Initiate new function

   Called with function, cylinder, to allow recalibrate as well as
   load and go to be processed by this routine.

   Assumes that the controller is idle, and that updating of interrupt
   request will be done by the caller.
*/

void rk_go (int32 func, int32 cyl)
{
int32 t;
UNIT *uptr;

if (func == RKC_RALL)                                   /* all? use standard */
    func = RKC_READ;
if (func == RKC_WALL)
func = RKC_WRITE;
uptr = rk_dev.units + GET_DRIVE (rk_cmd);               /* selected unit */
if ((uptr->flags & UNIT_ATT) == 0) {                    /* not attached? */
    rk_sta = rk_sta | RKS_DONE | RKS_NRDY | RKS_STAT;
    return;
    }
if (sim_is_active (uptr) || (cyl >= RK_NUMCY)) {        /* busy or bad cyl? */
    rk_sta = rk_sta | RKS_DONE | RKS_STAT;
    return;
    }
if ((func == RKC_WRITE) && (uptr->flags & UNIT_WPRT)) {
    rk_sta = rk_sta | RKS_DONE | RKS_WLK;               /* write and locked? */
    return;
    }
if (func == RKC_WLK) {                                  /* write lock? */
    uptr->flags = uptr->flags | UNIT_SWLK;
    rk_sta = rk_sta | RKS_DONE;
    return;
    }
t = abs (cyl - uptr->CYL) * rk_swait;                   /* seek time */
if (func == RKC_SEEK) {                                 /* seek? */
    sim_activate (uptr, MAX (RK_MIN, t));               /* schedule */
    rk_sta = rk_sta | RKS_DONE;                         /* set done */
    }
else {
    sim_activate (uptr, t + rk_rwait);                  /* schedule */
    rk_busy = 1;                                        /* set busy */
    }
uptr->FUNC = func;                                      /* save func */
uptr->CYL = cyl;                                        /* put on cylinder */
return;
}

/* Unit service

   If seek, complete seek command
   Else complete data transfer command

   The unit control block contains the function and cylinder address for
   the current command.

   Note that memory addresses wrap around in the current field.
*/

static uint16 fill[RK_NUMWD/2] = { 0 };
t_stat rk_svc (UNIT *uptr)
{
int32 err, wc, wc1, awc, swc, pa, da;
UNIT *seluptr;

if (uptr->FUNC == RKC_SEEK) {                           /* seek? */
    seluptr = rk_dev.units + GET_DRIVE (rk_cmd);        /* see if selected */
    if ((uptr == seluptr) && ((rk_cmd & RKC_SKDN) != 0)) {
        rk_sta = rk_sta | RKS_DONE;
        RK_INT_UPDATE;
        }
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* not att? abort */
    rk_sta = rk_sta | RKS_DONE | RKS_NRDY | RKS_STAT;
    rk_busy = 0;
    RK_INT_UPDATE;
    return IORETURN (rk_stopioe, SCPE_UNATT);
    }

if ((uptr->FUNC == RKC_WRITE) && (uptr->flags & UNIT_WPRT)) {
    rk_sta = rk_sta | RKS_DONE | RKS_WLK;               /* write and locked? */
    rk_busy = 0;
    RK_INT_UPDATE;
    return SCPE_OK;
    }

pa = GET_MEX (rk_cmd) | rk_ma;                          /* phys address */
da = GET_DA (rk_cmd, rk_da) * RK_NUMWD * sizeof (int16);/* disk address */
swc = wc = (rk_cmd & RKC_HALF)? RK_NUMWD / 2: RK_NUMWD; /* get transfer size */
if ((wc1 = ((rk_ma + wc) - 010000)) > 0)                /* if wrap, limit */
    wc = wc - wc1;
err = fseek (uptr->fileref, da, SEEK_SET);              /* locate sector */

if ((uptr->FUNC == RKC_READ) && (err == 0) && MEM_ADDR_OK (pa)) { /* read? */
    awc = fxread (&M[pa], sizeof (int16), wc, uptr->fileref);
    for ( ; awc < wc; awc++)                            /* fill if eof */
        M[pa + awc] = 0;
    err = ferror (uptr->fileref);
    if ((wc1 > 0) && (err == 0))  {                     /* field wraparound? */
        pa = pa & 070000;                               /* wrap phys addr */
        awc = fxread (&M[pa], sizeof (int16), wc1, uptr->fileref);
        for ( ; awc < wc1; awc++)                       /* fill if eof */
            M[pa + awc] = 0;
        err = ferror (uptr->fileref);
        }
    }

if ((uptr->FUNC == RKC_WRITE) && (err == 0)) {          /* write? */
    fxwrite (&M[pa], sizeof (int16), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    if ((wc1 > 0) && (err == 0)) {                      /* field wraparound? */
        pa = pa & 070000;                               /* wrap phys addr */
        fxwrite (&M[pa], sizeof (int16), wc1, uptr->fileref);
        err = ferror (uptr->fileref);
        }
    if ((rk_cmd & RKC_HALF) && (err == 0)) {            /* fill half sector */
        fxwrite (fill, sizeof (int16), RK_NUMWD/2, uptr->fileref);
        err = ferror (uptr->fileref);
        }
    }

rk_ma = (rk_ma + swc) & 07777;                          /* incr mem addr reg */
rk_sta = rk_sta | RKS_DONE;                             /* set done */
rk_busy = 0;
RK_INT_UPDATE;

if (err != 0) {
    sim_perror ("RK I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat rk_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rk_cmd = rk_ma = rk_da = rk_sta = rk_busy = 0;
int_req = int_req & ~INT_RK;                            /* clear interrupt */
for (i = 0; i < RK_NUMDR; i++) {                        /* stop all units */
    uptr = rk_dev.units + i;
    sim_cancel (uptr);
    uptr->flags = uptr->flags & ~UNIT_SWLK;
    uptr->CYL = uptr->FUNC = 0;
    }
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 023
#define BOOT_UNIT 032
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    06007,                      /* 23, CAF */
    06744,                      /* 24, DLCA             ; addr = 0 */
    01032,                      /* 25, TAD UNIT         ; unit no */
    06746,                      /* 26, DLDC             ; command, unit */
    06743,                      /* 27, DLAG             ; disk addr, go */
    01032,                      /* 30, TAD UNIT         ; unit no, for OS */
    05031,                      /* 31, JMP . */
    00000                       /* UNIT, 0              ; in bits <9:10> */
    };

t_stat rk_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

if (rk_dib.dev != DEV_RK)                               /* only std devno */
    return STOP_NOTSTD;
for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
M[BOOT_UNIT] = (unitno & RK_M_NUMDR) << 1;
cpu_set_bootpc (BOOT_START);
return SCPE_OK;
}
