/* vax730_rb.c: RB730 disk simulator

   Copyright (c) 2010-2011, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1993-2008, Robert M Supnik

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

   rb           RB730 - RB02/RB80 disk controller

   29-Mar-2011  MB      First Version

   The RB730 is a four drive disk subsystem consisting of up to three RL02
   drives (known as RB02) and one optional RA80 drive (known as RB80).

   Unlike the RL11 controller seeks are not done relative to the current
   disk address.

   The RB730 has two regiter address spaces:
   
   - One dummy 16-bit register in unibus I/O space to allow the controller to
     be detected by SYSGEN autoconfigure (and others).
   - Eight 32-bit registers in the unibus controller space for the actual
     device control.
*/

#include "vax_defs.h"

/* Constants */

#define RB02_NUMWD      128                             /* words/sector */
#define RB02_NUMSC      40                              /* sectors/track */
#define RB02_NUMSF      2                               /* tracks/cylinder */
#define RB02_NUMCY      512                             /* cylinders/drive */
#define RB02_SIZE       (RB02_NUMCY * RB02_NUMSF * \
                         RB02_NUMSC * RB02_NUMWD)       /* words/drive */
#define RB80_NUMWD      256                             /* words/sector */
#define RB80_NUMSC      32                              /* sectors/track */
#define RB80_NUMSF      14                              /* tracks/cylinder */
#define RB80_NUMCY      559                             /* cylinders/drive */
#define RB80_SIZE       (RB80_NUMCY * RB80_NUMSF * \
                         RB80_NUMSC * RB80_NUMWD)       /* words/drive */

#define RB_NUMWD(u)     ((u->flags & UNIT_RB80) ? RB80_NUMWD : RB02_NUMWD)
#define RB_NUMSC(u)     ((u->flags & UNIT_RB80) ? RB80_NUMSC : RB02_NUMSC)
#define RB_NUMSF(u)     ((u->flags & UNIT_RB80) ? RB80_NUMSF : RB02_NUMSF)
#define RB_NUMCY(u)     ((u->flags & UNIT_RB80) ? RB80_NUMCY : RB02_NUMCY)
#define RB_SIZE(u)      ((u->flags & UNIT_RB80) ? RB80_SIZE : RB02_SIZE)

#define RB_NUMDR        4                               /* drives/controller */
#define RB_MAXFR        (1 << 16)                       /* max transfer */

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_RB80     (UNIT_V_UF + 1)                 /* RB02 vs RB80 */
#define UNIT_V_DUMMY    (UNIT_V_UF + 2)                 /* dummy flag */
#define UNIT_DUMMY      (1 << UNIT_V_DUMMY)
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_RB80       (1u << UNIT_V_RB80)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protected */

/* Parameters in the unit descriptor */

#define TRK             u3                              /* current track */
#define STAT            u4                              /* status */
#define SIP             u5                              /* seek in progress */

/* RBDS, NI = not implemented, * = kept in STAT, ^ = kept in TRK */

#define RB02DS_LOAD     0                               /* no cartridge */
#define RB02DS_LOCK     5                               /* lock on */
#define RB02DS_BHO      0000010                         /* brushes home NI */
#define RB02DS_HDO      0000020                         /* heads out NI */
#define RB02DS_CVO      0000040                         /* cover open NI */
#define RB02DS_HD       0000100                         /* head select ^ */
#define RB02DS_DSE      0000400                         /* drv sel err NI */
#define RB02DS_VCK      0001000                         /* vol check * */
#define RB02DS_WGE      0002000                         /* wr gate err * */
#define RB02DS_SPE      0004000                         /* spin err * */
#define RB02DS_STO      0010000                         /* seek time out NI */
#define RB02DS_WLK      0020000                         /* wr locked */
#define RB02DS_HCE      0040000                         /* hd curr err NI */
#define RB02DS_WDE      0100000                         /* wr data err NI */
#define RB02DS_ATT      (RB02DS_HDO+RB02DS_BHO+RB02DS_LOCK)   /* att status */
#define RB02DS_UNATT    (RB02DS_CVO+RB02DS_LOAD)            /* unatt status */
#define RB02DS_ERR      (RB02DS_WDE+RB02DS_HCE+RB02DS_STO+RB02DS_SPE+RB02DS_WGE+ \
                         RB02DS_VCK+RB02DS_DSE)             /* errors bits */

#define RB80DS_SCNT     0x0000000F
#define RB80DS_FLT      0x00000100
#define RB80DS_PLV      0x00000200
#define RB80DS_SKE      0x00000400
#define RB80DS_OCY      0x00000800
#define RB80DS_RDY      0x00001000
#define RB80DS_WLK      0x00002000

/* RBCS */

#define RBCS_DRDY       0x00000001                      /* drive ready */
#define RBCS_M_FUNC     0x7                             /* function */
#define  RBCS_NOP       0
#define  RBCS_WCHK      1
#define  RBCS_GSTA      2
#define  RBCS_SEEK      3
#define  RBCS_RHDR      4
#define  RBCS_WRITE     5
#define  RBCS_READ      6
#define  RBCS_RNOHDR    7
#define RBCS_V_FUNC     1
#define RBCS_M_DRIVE    0x3
#define RBCS_V_DRIVE    8
#define RBCS_INCMP      0x00000400                      /* incomplete */
#define RBCS_CRC        0x00000800                      /* CRC error */
#define RBCS_DLT        0x00001000                      /* data late */
#define RBCS_HDE        0x00001400                      /* header error */
#define RBCS_NXM        0x00002000                      /* non-exist memory */
#define RBCS_DRE        0x00004000                      /* drive error */
#define RBCS_ERR        0x00008000                      /* error summary */
#define RBCS_ALLERR (RBCS_ERR+RBCS_DRE+RBCS_NXM+RBCS_CRC+RBCS_INCMP)
#define RBCS_M_ATN      0xF
#define RBCS_V_ATN      16
#define RBCS_ATN        (RBCS_M_ATN << RBCS_V_ATN)
#define RBCS_M_ECC      0x2
#define RBCS_V_ECC      20
#define RBCS_SSI        0x00400000
#define RBCS_SSE        0x00800000
#define RBCS_IRQ        0x01000000
#define RBCS_MTN        0x02000000
#define RBCS_R80        0x04000000
#define RBCS_ASI        0x08000000
#define RBCS_TOI        0x10000000
#define RBCS_FMT        0x20000000
#define RBCS_MATN       0x80000000
//#define RBCS_RW         0001716                         /* read/write */
#define RBCS_RW         ((RBCS_M_FUNC << RBCS_V_FUNC) + \
                        CSR_IE + CSR_DONE + \
                        (RBCS_M_DRIVE << RBCS_V_DRIVE) + \
                        RBCS_SSI + RBCS_MTN + RBCS_ASI + \
                        RBCS_TOI + RBCS_FMT + RBCS_MATN)
#define RBCS_C0         RBCS_SSE
#define RBCS_C1         (rbcs & RBCS_MATN) ? RBCS_IRQ : \
                        ((RBCS_M_ATN << RBCS_V_ATN) + RBCS_IRQ)
#define GET_FUNC(x)     (((x) >> RBCS_V_FUNC) & RBCS_M_FUNC)
#define GET_DRIVE(x)    (((x) >> RBCS_V_DRIVE) & RBCS_M_DRIVE)

/* RBBA */

#define RBBA_RW         0x0003FFFF

/* RBBC */

/* RBMP */

#define RBMP_MRK        0x00000001
#define RBMP_GST        0x00000002
#define RBMP_RST        0x00000008

/* RBDA */

#define RBDA_V_SECT     0                               /* sector */
#define RBDA_M_SECT     0xFF
#define RBDA_V_TRACK    8                               /* track */
#define RBDA_M_TRACK    0xFF
#define RBDA_V_CYL      16                              /* cylinder */
#define RBDA_M_CYL      0xFFFFu
#define RBDA_TRACK      (RBDA_M_TRACK << RBDA_V_TRACK)
#define RBDA_CYL        (RBDA_M_CYL << RBDA_V_CYL)
#define GET_SECT(x)     (((x) >> RBDA_V_SECT) & RBDA_M_SECT)
#define GET_CYL(x)      (((x) >> RBDA_V_CYL) & RBDA_M_CYL)
#define GET_TRACK(x)    (((x) >> RBDA_V_TRACK) & RBDA_M_TRACK)
//#define GET_DA(x)       ((GET_CYL(x) * RB02_NUMSF * GET_TRACK (x) * RB02_NUMSC) + GET_SECT (x))
#define GET_DA(x,u)       ((GET_TRACK (x) * RB_NUMCY(u) * RB_NUMSC(u) * RB_NUMWD(u)) + \
                        (GET_CYL(x) * RB_NUMSC(u) * RB_NUMWD(u)) + \
                        (GET_SECT (x) * RB_NUMWD(u)))

#define DBG_REG         0x0001                          /* registers */
#define DBG_CMD         0x0002                          /* commands */
#define DBG_RD          0x0004                          /* disk reads */
#define DBG_WR          0x0008                          /* disk writes */

uint16 *rbxb = NULL;                                     /* xfer buffer */
int32 rbcs = 0;                                         /* control/status */
int32 rbba = 0;                                         /* memory address */
int32 rbbc = 0;                                         /* bytes count */
int32 rbda = 0;                                         /* disk addr */
int32 rbmp = 0, rbmp1 = 0, rbmp2 = 0;                   /* mp register queue */
int32 rb_swait = 150;                                   /* seek wait */
int32 rb_mwait = 300;                                   /* seek wait */
int32 rb_cwait = 50;                                    /* seek wait */

t_stat rb_rd16 (int32 *data, int32 PA, int32 access);
t_stat rb_wr16 (int32 data, int32 PA, int32 access);
t_stat rb_rd32 (int32 *data, int32 PA, int32 access);
t_stat rb_wr32 (int32 data, int32 PA, int32 access);
t_stat rb_svc (UNIT *uptr);
t_stat rb_reset (DEVICE *dptr);
const char *rb_description (DEVICE *dptr);
void rb_set_done (int32 error);
t_stat rb_attach (UNIT *uptr, CONST char *cptr);
t_stat rb_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rb_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* RB730 data structures

   rb_dev       RB device descriptor
   rb_unit      RB unit list
   rb_reg       RB register list
   rb_mod       RB modifier list
*/

#define IOLN_RB         002

DIB rb_dib = {
    IOBA_AUTO, IOLN_RB, &rb_rd16, &rb_wr16,
    1, IVCL (RB), VEC_AUTO, { NULL }, IOLN_RB };

UNIT rb_unit[] = {
    { UDATA (&rb_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_RB80, RB80_SIZE) },
    { UDATA (&rb_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RB02_SIZE) },
    { UDATA (&rb_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RB02_SIZE) },
    { UDATA (&rb_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, RB02_SIZE) },
    };

REG rb_reg[] = {
    { NULL }
    };

DEBTAB rb_debug[] = {
    {"REG", DBG_REG},
    {"CMD", DBG_CMD},
    {"RD",  DBG_RD},
    {"WR",  DBG_WR},
    {0}
};

MTAB rb_mod[] = {
    { UNIT_WLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK, UNIT_WLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock disk drive"  },
    { UNIT_DUMMY,      0, NULL,            "BADBLOCK", 
        &rb_set_bad, NULL, NULL, "write bad block table on last track" },
    { (UNIT_RB80+UNIT_ATT),             UNIT_ATT, "RB02", NULL, NULL },
    { (UNIT_RB80+UNIT_ATT), (UNIT_RB80+UNIT_ATT), "RB80", NULL, NULL },
    { (UNIT_RB80+UNIT_ATT),                    0, "RB02", NULL, NULL },
    { (UNIT_RB80+UNIT_ATT),            UNIT_RB80, "RB80", NULL, NULL },
    { (UNIT_RB80),                             0, NULL, "RB02", 
        &rb_set_size, NULL, NULL, "Set type to RB02" },
    { (UNIT_RB80),                     UNIT_RB80, NULL, "RB80", 
        &rb_set_size, NULL, NULL, "Set type to RB80" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0010, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,    0, "VECTOR", "VECTOR",
      &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE rb_dev = {
    "RB", rb_unit, rb_reg, rb_mod,
    RB_NUMDR, DEV_RDX, T_ADDR_W, 1, DEV_RDX, 16,
    NULL, NULL, &rb_reset,
    NULL, &rb_attach, NULL,
    &rb_dib, DEV_DISABLE | DEV_UBUS | DEV_DEBUG, 0,
    rb_debug, NULL, NULL, NULL, NULL, NULL, 
    &rb_description
    };

/* I/O dispatch routines

   17775606     RBDCS    dummy csr to trigger sysgen
*/

t_stat rb_rd16 (int32 *data, int32 PA, int32 access)
{
*data = 0;
return SCPE_OK;
}

t_stat rb_wr16 (int32 data, int32 PA, int32 access)
{
return SCPE_OK;
}

t_stat rb_rd32 (int32 *data, int32 PA, int32 access)
{
UNIT *uptr;

switch ((PA >> 2) & 07) {

    case 0:                                             /* RBCS */
        if (rbcs & RBCS_ALLERR)
            rbcs = rbcs | RBCS_ERR;
        uptr = rb_dev.units + GET_DRIVE (rbcs);
        if ((sim_is_active (uptr)) || (uptr->flags & UNIT_DIS))
            rbcs = rbcs & ~RBCS_DRDY;
        else rbcs = rbcs | RBCS_DRDY;                   /* see if ready */
        if (uptr->flags & UNIT_RB80)
            rbcs = rbcs | RBCS_R80;
        else rbcs = rbcs & ~RBCS_R80;
        *data = rbcs;
        break;

    case 1:                                             /* RBBA */
        *data = rbba & RBBA_RW;
        break;

    case 2:                                             /* RBBC */
        *data = rbbc;
        break;

    case 3:                                             /* RBDA */
        *data = rbda;
        break;

    case 4:                                             /* RBMP */
        *data = rbmp;
        rbmp = rbmp1;                                   /* ripple data */
        rbmp1 = rbmp2;
        break;

    case 5:                                             /* ECCPS */
    case 6:                                             /* ECCPT */
    case 7:                                             /* INIT */
        *data = 0;
        break;
    }

sim_debug(DBG_REG, &rb_dev, "reg %d read, value = %X\n", (PA >> 2) & 07, *data);

return SCPE_OK;
}

t_stat rb_wr32 (int32 data, int32 PA, int32 access)
{
UNIT *uptr;

sim_debug(DBG_REG, &rb_dev, "reg %d write, value = %X\n", (PA >> 2) & 07, data);

switch ((PA >> 2) & 07) {

    case 0:                                             /* CSR */
        if (rbcs & RBCS_ALLERR)
            rbcs = rbcs | RBCS_ERR;
        uptr = rb_dev.units + GET_DRIVE (data);
        if ((sim_is_active (uptr)) || (uptr->flags & UNIT_DIS))
            rbcs = rbcs & ~RBCS_DRDY;
        else rbcs = rbcs | RBCS_DRDY;                   /* see if ready */
        if (uptr->flags & UNIT_RB80)
            rbcs = rbcs | RBCS_R80;
        else rbcs = rbcs & ~RBCS_R80;

        rbcs = rbcs & ~(data & RBCS_C1);
        rbcs = rbcs & ~(~data & RBCS_C0);
        rbcs = (rbcs & ~RBCS_RW) | (data & RBCS_RW);
        if (data & RBCS_ATN) CLR_INT (RB);
        
        if ((data & CSR_DONE) || (sim_is_active (uptr))) /* ready set? */
            return SCPE_OK;

        CLR_INT (RB);                                   /* clear interrupt */
        rbcs = rbcs & ~RBCS_ALLERR;                     /* clear errors */
        uptr->SIP = 0;
        if (uptr->flags & UNIT_DIS) {
            rbcs = rbcs | (1u << (RBCS_V_ATN + GET_DRIVE (rbcs)));
            rb_set_done (RBCS_ERR | RBCS_INCMP);
            break;
            }
        switch (GET_FUNC (rbcs)) {                      /* case on RBCS<3:1> */
        case RBCS_NOP:                                  /* nop */
            rb_set_done (0);
            break;
        case RBCS_SEEK:                                 /* seek */
            sim_activate (uptr, rb_swait);
            break;
        default:                                        /* data transfer */
            sim_activate (uptr, rb_cwait);              /* activate unit */
            break;
            }                                           /* end switch func */
        break;

    case 1:                                             /* BAR */
        rbba = data & RBBA_RW;
        break;

    case 2:                                             /* BCR */
        rbbc = data;
        break;

    case 3:                                             /* DAR */
        rbda = data;
        break;

    case 4:                                             /* MPR */
        rbmp = rbmp1 = rbmp2 = data;
        break;

    case 5:                                             /* ECCPS */
    case 6:                                             /* ECCPT */
        break;

    case 7:                                             /* INIT */
        return rb_reset(&rb_dev);
    }

return SCPE_OK;
}

/* Service unit timeout

   If seek in progress, complete seek command
   Else complete data transfer command

   The unit control block contains the function and cylinder for
   the current command.
*/

t_stat rb_svc (UNIT *uptr)
{
int32 curr, newc, swait;
int32 err, wc, maxwc, t;
int32 i, func, da, awc;
uint32 ma;
uint16 comp;

func = GET_FUNC (rbcs);                                 /* get function */
if (func == RBCS_GSTA) {                                /* get status */
    sim_debug(DBG_CMD, &rb_dev, "Get Status\n");
    if (uptr->flags & UNIT_RB80) {
        rbmp = uptr->STAT | RB80DS_PLV;
        if (uptr->flags & UNIT_ATT)
            rbmp = rbmp | RB80DS_RDY | RB80DS_OCY;
        if (uptr->flags & UNIT_WPRT)
            rbmp = rbmp | RB80DS_WLK;
        }
    else {
        if (rbmp & RBMP_RST)
            uptr->STAT = uptr->STAT & ~RB02DS_ERR;
        rbmp = uptr->STAT | (uptr->flags & UNIT_ATT)? RB02DS_ATT: RB02DS_UNATT;
        if (uptr->flags & UNIT_WPRT)
            rbmp = rbmp | RB02DS_WLK;
        }
    rbmp2 = rbmp1 = rbmp;
    rb_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if (func == RBCS_RHDR) {                                /* read header? */
    sim_debug(DBG_CMD, &rb_dev, "Read Header\n");
    rbmp = (uptr->TRK & RBDA_TRACK) | GET_SECT (rbda);
    rbmp1 = rbmp2 = 0;
    rbcs = rbcs | (1 << (RBCS_V_ATN + GET_DRIVE (rbcs)));
    rb_set_done (0);                                    /* done */
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    rbcs = rbcs & ~RBCS_DRDY;                           /* clear drive ready */
    rbcs = rbcs | (1u << (RBCS_V_ATN + GET_DRIVE (rbcs)));
    if ((uptr->flags & UNIT_RB80) == 0)
        uptr->STAT = uptr->STAT | RB02DS_SPE;           /* spin error */
    rb_set_done (RBCS_ERR | RBCS_INCMP);                /* flag error */
    //return IORETURN (rl_stopioe, SCPE_UNATT);
    return SCPE_OK;
    }

if ((func == RBCS_WRITE) && (uptr->flags & UNIT_WPRT)) {
    if ((uptr->flags & UNIT_RB80) == 0)
        uptr->STAT = uptr->STAT | RB02DS_WGE;           /* write and locked */
    rb_set_done (RBCS_ERR | RBCS_DRE);
    return SCPE_OK;
    }

if (func == RBCS_SEEK) {                                /* seek? */
    if (uptr->SIP == 0) {
        sim_debug(DBG_CMD, &rb_dev, "Seek, CYL=%d, TRK=%d, SECT=%d\n", GET_CYL(rbda), GET_TRACK(rbda), GET_SECT(rbda));
        uptr->SIP = 1;
        if ((uint32)rbda == 0xFFFFFFFF) swait = rb_swait;
        else {
            curr = GET_CYL (uptr->TRK);                     /* current cylinder */
            newc = GET_CYL (rbda);                          /* offset */
            uptr->TRK = (newc << RBDA_V_CYL);               /* put on track */
            swait = rb_cwait * abs (newc - curr);
            if (swait < rb_mwait) swait = rb_mwait;
            }
        sim_activate (uptr, swait);
        rbcs = rbcs | (1 << (RBCS_V_ATN + GET_DRIVE (rbcs)));
        rbcs = rbcs | RBCS_IRQ;
        rb_set_done(0);
        return SCPE_OK;
        }
    else {
        sim_debug(DBG_CMD, &rb_dev, "Seek done\n");
        rbcs = rbcs | (1 << (RBCS_V_ATN + GET_DRIVE (rbcs)));
        uptr->SIP = 0;
        rb_set_done (0);                                /* done */
        return SCPE_OK;
        }
    }

if (((func != RBCS_RNOHDR) && ((uptr->TRK & RBDA_CYL) != (rbda & RBDA_CYL)))
   || (GET_SECT (rbda) >= RB_NUMSC(uptr))) {                /* bad cyl or sector? */
    sim_debug(DBG_CMD, &rb_dev, "Invalid cylinder or sector, CYL=%d, TRK=%d, SECT=%d\n", GET_CYL(rbda), GET_TRACK(rbda), GET_SECT(rbda));
    rb_set_done (RBCS_ERR | RBCS_HDE | RBCS_INCMP);     /* wrong cylinder? */
    return SCPE_OK;
    }

ma = rbba;                                              /* get mem addr */
da = GET_DA (rbda, uptr);                               /* get disk addr */
wc = ((rbbc * -1) >> 1);                                /* get true wc */

maxwc = (RB_NUMSC(uptr) - GET_SECT (rbda)) * RB_NUMWD(uptr);    /* max transfer */
if (wc > maxwc)                                         /* track overrun? */
    wc = maxwc;
err = sim_fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);

if ((func >= RBCS_READ) && (err == 0)) {                /* read (no hdr)? */
    sim_debug(DBG_CMD, &rb_dev, "Read, CYL=%d, TRK=%d, SECT=%d, WC=%d, DA=%d\n", GET_CYL(rbda), GET_TRACK(rbda), GET_SECT(rbda), wc, da);
    i = sim_fread (rbxb, sizeof (uint16), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    for ( ; i < wc; i++)                                /* fill buffer */
        rbxb[i] = 0;
    if ((t = Map_WriteW (ma, wc << 1, rbxb))) {         /* store buffer */
        rbcs = rbcs | RBCS_ERR | RBCS_NXM;              /* nxm */
        wc = wc - t;                                    /* adjust wc */
        }
    }                                                   /* end read */

if ((func == RBCS_WRITE) && (err == 0)) {               /* write? */
    sim_debug(DBG_CMD, &rb_dev, "Write, CYL=%d, TRK=%d, SECT=%d, WC=%d, DA=%d\n", GET_CYL(rbda), GET_TRACK(rbda), GET_SECT(rbda), wc, da);
    if ((t = Map_ReadW (ma, wc << 1, rbxb))) {          /* fetch buffer */
        rbcs = rbcs | RBCS_ERR | RBCS_NXM;              /* nxm */
        wc = wc - t;                                    /* adj xfer lnt */
        }
    if (wc) {                                           /* any xfer? */
        awc = (wc + (RB_NUMWD(uptr) - 1)) & ~(RB_NUMWD(uptr) - 1);  /* clr to */
        for (i = wc; i < awc; i++)                      /* end of blk */
            rbxb[i] = 0;
        sim_fwrite (rbxb, sizeof (uint16), awc, uptr->fileref);
        err = ferror (uptr->fileref);
        }
    }                                                   /* end write */

if ((func == RBCS_WCHK) && (err == 0)) {                /* write check? */
    sim_debug(DBG_CMD, &rb_dev, "WCheck, CYL=%d, TRK=%d, SECT=%d, WC=%d, DA=%d\n", GET_CYL(rbda), GET_TRACK(rbda), GET_SECT(rbda), wc, da);
    i = sim_fread (rbxb, sizeof (uint16), wc, uptr->fileref);
    err = ferror (uptr->fileref);
    for ( ; i < wc; i++)                                /* fill buffer */
        rbxb[i] = 0;
    awc = wc;                                           /* save wc */
    for (wc = 0; (err == 0) && (wc < awc); wc++)  {     /* loop thru buf */
        if (Map_ReadW (ma + (wc << 1), 2, &comp)) {     /* mem wd */
            rbcs = rbcs | RBCS_ERR | RBCS_NXM;          /* nxm */
            break;
            }
        if (comp != rbxb[wc])                           /* check to buf */
            rbcs = rbcs | RBCS_ERR | RBCS_CRC;
        }                                               /* end for */
    }                                                   /* end wcheck */

rbbc = (rbbc + (wc << 1));                              /* final byte count */
if (rbbc != 0) {                                        /* completed? */
    rbcs = rbcs | RBCS_ERR | RBCS_INCMP;
    }
ma = ma + (wc << 1);                                    /* final byte addr */
rbba = ma & RBBA_RW;
rbda = rbda + ((wc + (RB_NUMWD(uptr) - 1)) / RB_NUMWD(uptr));
rb_set_done (0);

if (err != 0) {                                         /* error? */
    sim_perror ("RB I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Set done and possibly errors */

void rb_set_done (int32 status)
{
rbcs = rbcs | status | CSR_DONE;                        /* set done */
rbcs = rbcs | RBCS_IRQ;
if (rbcs & CSR_IE) {
    sim_debug(DBG_CMD, &rb_dev, "Done, INT\n");
    SET_INT (RB);
    }
else {
    sim_debug(DBG_CMD, &rb_dev, "Done, no INT\n");
    CLR_INT (RB);
    }
return;
}

/* Device reset */

t_stat rb_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

rbcs = CSR_DONE;
rbda = rbba = rbbc = rbmp = 0;
CLR_INT (RB);
for (i = 0; i < RB_NUMDR; i++) {
    uptr = rb_dev.units + i;
    sim_cancel (uptr);
    uptr->STAT = 0;
    uptr->SIP = 0;
    }
if (rbxb == NULL)
    rbxb = (uint16 *) calloc (RB_MAXFR, sizeof (uint16));
if (rbxb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

const char *rb_description (DEVICE *dptr)
{
return "RB730 disk controller";
}

/* Attach routine */

t_stat rb_attach (UNIT *uptr, CONST char *cptr)
{
uint32 p;
t_stat r;

uptr->capac = (uptr->flags & UNIT_RB80)? RB80_SIZE: RB02_SIZE;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
uptr->TRK = 0;                                          /* cylinder 0 */
if ((uptr->flags & UNIT_RB80) == 0)
    uptr->STAT = RB02DS_VCK;                            /* new volume */
if ((p = sim_fsize (uptr->fileref)) == 0) {             /* new disk image? */
    if (uptr->flags & UNIT_RO)                          /* if ro, done */
        return SCPE_OK;
    return pdp11_bad_block (uptr, RB_NUMSC(uptr), RB_NUMWD(uptr));
    }
return SCPE_OK;
}

/* Set size routine */

t_stat rb_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = (val & UNIT_RB80)? RB80_SIZE: RB02_SIZE;
return SCPE_OK;
}

/* Set bad block routine */

t_stat rb_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return pdp11_bad_block (uptr, RB_NUMSC(uptr), RB_NUMWD(uptr));
}
