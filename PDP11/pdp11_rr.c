/* pdp11_rr.c: RP11/-C/-E/RP02/RP03 disk pack device

   Copyright (c) 2022 Tony Lawrence

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

   Inspired by PDP-11's RK/RL implementations by Robert Supnik.
*/

#if defined(VM_PDP11)  &&  !defined(UC15)

#include "pdp11_defs.h"
#include "sim_disk.h"

/* Constants */

#define RPCONTR         uint16
#define RPWRDSZ         16
#define MAP_RDW(a,c,b)  (Map_ReadW (a, (c) << 1, b) >> 1)
#define MAP_WRW(a,c,b)  (Map_WriteW(a, (c) << 1, b) >> 1)

/* RP02 parameters; RP03 doubles # of cylinders (both total and spare) */
#define RP_NUMWD        256                             /* words/sector */
#define RP_NUMCY        203                             /* cylinders/drive */
#define RP_SPARE        3                               /* of those, spare */
#define RP_NUMSF        20                              /* surfaces/cylinder */
#define RP_NUMSC        10                              /* sectors/track */
#define RP_NUMTR        (RP_NUMCY * RP_NUMSF)           /* tracks/drive */
#define RP_NUMBL        (RP_NUMTR * RP_NUMSC)           /* blocks/drive */
#define RP_NUMDR        8                               /* drives/controller */
#define RP_MAXFR        (1 << 16)                       /* max transfer */
#define RP_SIZE(n)      (RP_NUMWD * (n))                /* words in n blocks */
#define RP_SIZE_RP02    RP_SIZE(RP_NUMBL)               /* RP02 capacity, words */
#define RP_SIZE_RP03    RP_SIZE(RP_NUMBL*2)             /* RP03 capacity, words */
#define RP_ROT_12       125                             /* Half rotation, 0.1ms */

/* Controller / drive types */
#define RP_RP11         "RP11-C"
#define RP_RP02         "RP02"
#define RP_RP03         "RP03"

/* Parameters in the unit descriptor */
#define CYL             u3                              /* current cylinder */
#define HEAD            u4                              /* current track */
#define FUNC            us9                             /* current func */
#define STATUS          u5                              /* RPDS bits for the unit */
#define SEEKING         u6                              /* seek cmd underway */

/* 4 dummy UNIBUS registers followed by... */
#define RP_IOFF         4
/* 12 real UNIBUS registers, so 16 registers total: 32 addresses */
#define RP_IOLN         040

/* RP02/RP03 particulars */

/* RP11 specific drive type parameters */
#define spare       uint32_01   /* spare (out of cyl) */
#define seek_1      uint32_02   /* one track move, 0.1ms */
#define seek_ave    uint32_03   /* average seek, 0.1ms */
#define seek_max    uint32_04   /* maximal seek, 0.1ms */

#define RP_DRV(d, factor, seek_L, seek_AVG, seek_MAX)                                \
    { RP_NUMSC, RP_NUMSF, RP_NUMCY*factor, RP_NUMBL*factor, #d, \
      RP_NUMWD*sizeof(uint16), 0, NULL, 0, 0, NULL, NULL,       \
      RP_SPARE*factor, seek_L, seek_AVG, seek_MAX}

static DRVTYP drv_typ[] = {
    RP_DRV(RP02, 1, 200, 500, 800),
    RP_DRV(RP03, 2,  75, 290, 550),
    { 0 }
    };


/* RPDS 776710, selected drive status, read-only except for the attention bits */
static BITFIELD rp_ds_bits[] = {
#define RPDS_ATTN       0000377                         /* attention (read/clear) */
    BITF(ATTN,8),
#define RPDS_WLK        0000400                         /* write locked */
    BIT(WLK),
#define RPDS_UNSAFE     0001000                         /* unsafe */
    BIT(UNSAFE),
#define RPDS_SEEK       0002000                         /* seek underway */
    BIT(SEEK),
#define RPDS_INC        0004000                         /* seek incomplete */
    BIT(INC),
#define RPDS_HNF        0010000                         /* header not found */
    BIT(HNF),
#define RPDS_RP03       0020000                         /* drive is RP03 */
    BIT(RP03=),
#define RPDS_ONLN       0040000                         /* unit online */
    BIT(ONLN),
#define RPDS_RDY        0100000                         /* unit ready */
    BIT(RDY),
    ENDBITS
};
#define RPDS_REAL       0017400                         /* bits stored */
#define RPDS_DKER       (RPDS_HNF | RPDS_INC)           /* drive error */
#define RPER_DKER(x)    ((x) & RPDS_DKER ? RPER_DRE : 0)/* DRE for RPER */

/* RPER 776712, error register, read-only */
static BITFIELD rp_er_bits[] = {
#define RPER_DRE        0000001                         /* drive error (HNF|INC) */
    BIT(DRE),
#define RPER_EOP        0000002                         /* end of pack (overrun) */
    BIT(EOP),
#define RPER_NXM        0000004                         /* nx memory */
    BIT(NXM),
#define RPER_WCE        0000010                         /* write check error */
    BIT(WCE),
#define RPER_TE         0000020                         /* timing error */
    BIT(TE),
#define RPER_CSE        0000040                         /* serial checksum error */
    BIT(CSE),
#define RPER_WPE        0000100                         /* word parity error */
    BIT(WPE),
#define RPER_LPE        0000200                         /* longitudinal parity error */
    BIT(LPE),
#define RPER_MODE       0000400                         /* mode error */
    BIT(MODE),
#define RPER_FMTE       0001000                         /* format error */
    BIT(FMTE),
#define RPER_PGE        0002000                         /* programming error */
    BIT(PGE), 
#define RPER_NXS        0004000                         /* nx sector */
    BIT(NXS),
#define RPER_NXT        0010000                         /* nx track */
    BIT(NXT),
#define RPER_NXC        0020000                         /* nx cylinder */
    BIT(NXC),
#define RPER_FUV        0040000                         /* unsafe violation */
    BIT(FUV),
#define RPER_WPV        0100000                         /* write lock violation */
    BIT(WPV),
    ENDBITS
};
#define RPER_REAL       0177776
/* hard errors: drawing 19 */
#define RPER_HARDERR    (RPER_WPV | RPER_FUV | RPER_NXC | RPER_NXT | \
                         RPER_NXS | RPER_PGE | RPER_NXM | RPER_DRE | RPER_MODE)
/* soft errors: drawing 19 */
#define RPER_SOFTERR    (RPER_LPE | RPER_WPE | RPER_CSE | RPER_WCE | \
                         RPER_EOP | RPER_TE  | RPER_FMTE)
#define RPER_HARD(x)    ((x) & RPER_HARDERR ? (RPCS_ERR | RPCS_HERR) : 0)
#define RPER_SOFT(x)    ((x) & RPER_SOFTERR ?  RPCS_ERR              : 0)

/* RPCS 776714, command/status register */
static const char* rp_funcs[] = {
    "RESET", "WRITE", "READ", "WCHK", "SEEK", "WRNOSEEK", "HOME", "RDNOSEEK"
};

static BITFIELD rp_cs_bits[] = {
/* CSR_GO */                                            /* the GO! bit */
    BIT(GO),
#define RPCS_V_FUNC     1
#define RPCS_M_FUNC     7
#define RPCS_FUNC       (RPCS_M_FUNC << RPCS_V_FUNC)    /* function */
#define  RPCS_RESET     0
#define  RPCS_WRITE     1
#define  RPCS_READ      2
#define  RPCS_WCHK      3
#define  RPCS_SEEK      4
#define  RPCS_WR_NOSEEK 5
#define  RPCS_HOME      6
#define  RPCS_RD_NOSEEK 7
    BITFNAM(FUNC,3,rp_funcs),
#define RPCS_V_MEX      4
#define RPCS_M_MEX      3
#define RPCS_MEX        (RPCS_M_MEX << RPCS_V_MEX)      /* memory extension */
    BITF(MEX,2),
/* CSR_IE */                                            /* interrupt enable */
    BIT(IE),
/* CSR_DONE */                                          /* controller ready */
    BIT(DONE),
#define RPCS_V_DRV      8
#define RPCS_M_DRV      7
#define RPCS_DRV        (RPCS_M_DRV << RPCS_V_DRV)      /* drive id */
    BITFFMT(DRV,3,%u),
#define RPCS_HDR        0004000                         /* header operation */
    BIT(HDR),
#define RPCS_MODE       0010000                         /* 0=PDP-11; 1=PDP-10/-15 or format */
    BIT(MODE),
#define RPCS_AIE        0020000                         /* attention interrupt enable */
    BIT(AIE),
#define RPCS_HERR       0040000                         /* hard error */
    BIT(HERR),
#define RPCS_ERR        CSR_ERR                         /* error (hard or soft) */
    BIT(ERR),
#define RPCS_REAL       0037776                         /* bits kept here */
#define RPCS_RW         0037576                         /* read/write */
#define GET_FUNC(x)     (((x) & RPCS_FUNC) >> RPCS_V_FUNC)
#define GET_DRIVE(x)    (((x) & RPCS_DRV) >> RPCS_V_DRV)
    ENDBITS
};

/* RPWC 776716, two's complement word count */
/* For PDP-11 must be even for data, and in multiples of 3 for format */
static BITFIELD rp_wc_bits[] = {
#define RPWC_IMP        0177777                         /* implemented */
    BITFFMT(WC,16,%u),
    ENDBITS
};

/* RPBA 776720, bus address */
static BITFIELD rp_ba_bits[] = {
#define RPBA_IMP        0177776                         /* implemented */
    BITF(BA,16),
    ENDBITS
};

/* RPCA 776722, cylinder address */
static BITFIELD rp_ca_bits[] = {
#define RPCA_IMP        0000777                         /* implemented */
    BITFFMT(CYL,9,%u),
    ENDBITS
};

/* RPDA 776724, disk address (track/sector) */
static BITFIELD rp_da_bits[] = {
#define RPDA_IMPL       0017777                         /* implemented */
#define RPDA_RW         0017417                         /* bits here */
#define RPDA_M_SECT     017
#define RPDA_SECT       RPDA_M_SECT                     /* sector */
    BITFFMT(SECT,4,%u),
#define RPDA_V_SOT      4
#define RPDA_SOT        (RPDA_M_SECT << RPDA_V_SOT)     /* current sect on track */
    BITFFMT(SOT,4,%u),
#define RPDA_V_TRACK    8
#define RPDA_M_TRACK    037
#define RPDA_TRACK      (RPDA_M_TRACK << RPDA_V_TRACK)  /* track */
    BITFFMT(SURF,5,%u),
#define GET_SECT(x)     ((x) & RPDA_SECT)
#define GET_TRACK(x)    (((x) & RPDA_TRACK) >> RPDA_V_TRACK)
#define GET_DA(c,h,s)   (((c) * RP_NUMSF + (h)) * RP_NUMSC + (s))
    ENDBITS
};

/* RPM1 776726 maintenance 1, read-only, not implemented */

/* RPM2 776730 maintenance 2, read-only, not implemented */

/* RPM1 776732 maintenance 3, write-only, not implemented */

/* SUCA 776734 selected unit cylinder address, read-only */
static BITFIELD rp_suca_bits[] = {
    BITFFMT(CYL,9,%u),
    ENDBITS
};

/* SILO 776736 silo memory, not implemented */

/* Maintenance Write Lockout Address (LOA) (the switches on the maint. panel) */
static const char* offon[] = { "OFF", "ON" };
static BITFIELD rp_wloa_bits[] = {
#define RPWLOA_IMPL     03777
#define RPWLOA_CYL2     0377                            /* cyls locked (x2+1) */
    BITFFMT(CYL2,8,%u),
#define RPWLOA_V_DRV    8
#define RPWLOA_M_DRV    7
#define RPWLOA_DRV      (RPWLOA_M_DRV << RPWLOA_V_DRV)  /* drive(s) locked */
    BITFFMT(DRV,3,%u),
#define GET_WLOACYL(x)  (((((uint32)(x)) & RPWLOA_CYL2) << 1) | 1)
#define GET_WLOADRV(x)  ((((uint32)(x)) & RPWLOA_DRV) >> RPWLOA_V_DRV)
    BITNCF(4),
#define RPWLOA_ON       0100000
    BITFNAM(PROTECT,1,offon),
    ENDBITS
};

/* Data buffer and device registers */

static RPCONTR* rpxb = NULL;                            /* xfer buffer */
static int32 rpds = 0;                                  /* drive status */
static int32 rper = 0;                                  /* error status */
static int32 rpcs = 0;                                  /* control/status */
static int32 rpwc = 0;                                  /* word count */
static int32 rpba = 0;                                  /* memory address */
static int32 rpca = 0;                                  /* cylinder address */
static int32 rpda = 0;                                  /* disk address */
static int32 suca = 0;                                  /* current cylinder address */
static int32 wloa = 0;                                  /* write lockout address */
static int32 not_impl = 0;                              /* dummy register value */

/* Debug detail levels */

#define RRDEB_OPS       001                             /* transactions */
#define RRDEB_RRD       002                             /* reg reads */
#define RRDEB_RWR       004                             /* reg writes */
#define RRDEB_TRC       010                             /* trace */
#define RRDEB_INT       020                             /* interrupts */
#define RRDEB_DAT      0100                             /* transfer data */

static DEBTAB rr_deb[] = {
    { "OPS",       RRDEB_OPS, "transactions" },
    { "RRD",       RRDEB_RRD, "register reads" },
    { "RWR",       RRDEB_RWR, "register writes" },
    { "INTERRUPT", RRDEB_INT, "interrupts" },
    { "TRACE",     RRDEB_TRC, "trace" },
    { "DATA",      RRDEB_DAT, "transfer data" },
    { NULL, 0 }
};

static struct {
    const char* name;
    int32*      valp;
    BITFIELD*   bits;
} rr_regs[] = {
    { "RPDS", &rpds,     rp_ds_bits   },
    { "RPER", &rper,     rp_er_bits   },
    { "RPCS", &rpcs,     rp_cs_bits   },
    { "RPWC", &rpwc,     rp_wc_bits   },
    { "RPBA", &rpba,     rp_ba_bits   },
    { "RPCA", &rpca,     rp_ca_bits   },
    { "RPDA", &rpda,     rp_da_bits   },
    { "RPM1", &not_impl, NULL         },
    { "RPM2", &not_impl, NULL         },
    { "RPM3", &not_impl, NULL         },
    { "SUCA", &suca,     rp_suca_bits },
    { "SILO", &not_impl, NULL         }
};

/* Forward decls */

static t_stat rr_rd (int32 *data, int32 PA, int32 access);
static t_stat rr_wr (int32 data, int32 PA, int32 access);
static int32  rr_inta (void);
static t_stat rr_svc (UNIT *uptr);
static t_stat rr_reset (DEVICE *dptr);
static void   rr_go (int16 func);
static void   rr_set_done (int32 error);
static void   rr_clr_done (void);
static t_stat rr_boot (int32 unitno, DEVICE *dptr);
static t_stat rr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static t_stat rr_attach (UNIT *uptr, CONST char *cptr);
static t_stat rr_detach (UNIT *uptr);
static t_stat rr_set_wloa (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat rr_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static const char *rr_description (DEVICE *dptr);

/* RP11 data structures

   rr_dib       RR device context for PDP-11
   rr_reg       RR register list
   rr_unit      RR unit list
   rr_mod       RR modifier list
   rr_dev       RR device descriptor
*/

static DIB rr_dib = {
    IOBA_AUTO/*base address*/, RP_IOLN/*addresses used*/,
    rr_rd, rr_wr,
    1/*# of vectors*/, IVCL(RR)/*locator*/, VEC_AUTO,
    { rr_inta }, RP_IOLN/*addresses per device*/,
};

static REG rr_reg[] = {
    /* registers: CSR first, then in the bus address order */
    { ORDATADF(RPCS, rpcs, 16, "control/status",        rp_cs_bits) },
    { ORDATADF(RPDS, rpds, 16, "drive status",          rp_ds_bits) },
    { ORDATADF(RPER, rper, 16, "error status",          rp_er_bits) },
    { ORDATADF(RPWC, rpwc, 16, "word count",            rp_wc_bits) },
    { ORDATADF(RPBA, rpba, 16, "memory address",        rp_ba_bits) },
    { ORDATADF(RPCA, rpca, 16, "cylinder address",      rp_ca_bits) },
    { ORDATADF(RPDA, rpda, 16, "disk address",          rp_da_bits) },
    { ORDATADF(SUCA, suca, 16, "current cylinder",      rp_suca_bits) },
    { ORDATADF(WLOA, wloa, 16, "write lockout address", rp_wloa_bits) },

    /* standard stuff */
    { FLDATAD (INT,  IREQ(RR), INT_V_RR,   "interrupt pending flag") },
    { FLDATAD (ERR,  rpcs,     CSR_V_ERR,  "error flag (CSR<15>)") },
    { FLDATAD (DONE, rpcs,     CSR_V_DONE, "device done flag (CSR<7>)") },
    { FLDATAD (IE,   rpcs,     CSR_V_IE,   "interrupt enable flag (CSR<6>)") },
    { ORDATA  (DEVADDR, rr_dib.ba,  32), REG_HRO },
    { ORDATA  (DEVVEC,  rr_dib.vec, 16), REG_HRO },
    { NULL }
};

static UNIT rr_unit[RP_NUMDR] = { 0 };

static MTAB rr_mod[] = {
    { MTAB_VDV, 0,
        "TYPE",         NULL, 
        NULL,           rr_show_ctrl,   NULL,
        "Display controller type" },
    { MTAB_VDV | MTAB_VALR, 0,
        NULL,           "PROTECT",
        rr_set_wloa,    NULL,           NULL,
        "Set write lockout mode/address" },
    { MTAB_VUN, 0,
        "WRITEENABLED", "WRITEENABLED", 
        set_writelock,  show_writelock, NULL,
        "Write enable disk drive" },
    { MTAB_VUN, 1,
        NULL,           "LOCKED", 
        set_writelock,  NULL,           NULL,
        "Write lock disk drive" },
    { MTAB_VUN | MTAB_VALR, 0,
        "FORMAT",       "FORMAT={AUTO|SIMH|VHD|RAW}",
        sim_disk_set_fmt, sim_disk_show_fmt, NULL,
        "Set/Display disk format" },
    { MTAB_VDV | MTAB_VALR, 010,
        "ADDRESS",      "ADDRESS",
        set_addr,       show_addr,      NULL,
        "Bus address" },
    { MTAB_VDV | MTAB_VALR, 0,
        "VECTOR",       "VECTOR",
        set_vec,        show_vec,       NULL,
        "Interrupt vector" },
    { 0 }
};

DEVICE rr_dev = {
    "RR", rr_unit, rr_reg, rr_mod, RP_NUMDR,
    DEV_RDX/*address radix*/, 26/*address width*/, 1/*address increment*/,
    DEV_RDX/*data radix*/, RPWRDSZ/*data width*/,
    NULL/*examine()*/, NULL/*deposit()*/,
    rr_reset, rr_boot, rr_attach, rr_detach,
    &rr_dib,
    DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_Q18 | DEV_DEBUG | DEV_DISK,
    0/*debug control*/, rr_deb,
    NULL/*msize()*/, NULL/*logical name*/,
    rr_help, NULL/*attach_help()*/, NULL/*help_ctx*/,
    rr_description, NULL, &drv_typ
};

/* I/O dispatch routine, I/O addresses 17776710 - 17776736

   17776710     RPDS    read-only except for attention bits
   17776712     RPER    read-only
   17776714     RPCS    read/write
   17776716     RPWC    read/write
   17776720     RPBA    read/write
   17776722     RPCA    read/write
   17776724     RPDA    read/write
   17776726     RPM1    read-only,  unimplemented
   17776730     RPM2    read-only,  unimplemented
   17776732     RPM3    write-only, unimplemented
   17776734     SUCA    read-only
   17776736     SILO    read/write, unimplemented

RP11-C actually responds to the range 17776700 - 17776736 with the first 4 word
locations unused.

Some operating systems want you to specify the latter range (RSTS/E), but some
just want to know where the CSR is located, so they auto-calculate the range.

The original RP11 had the following differences:  it responded to the address
range 17776710 - 17776746:  RPM3 was followed by 3 buffer registers RPB1-RPB3,
then 3 locations (42-46) were unused.  RPCA was both the cylinder address in the
lower 8 bits <00:07> (read-write), and the Selected Unit current cylinder address
(a la SUCA in RP11-C) in the higher 8 bits <08:15> (read-only).  Since only the
RP02 disk drives were supported, it only required 8 bits for cylinder addresses.
There was no separate SUCA register (the location was occupied by RPB1).  The
RP03 bit in RPDS was always 0.  But programmatically it was mostly compatible
with the -C revision (except for the SU current cylinder address location, which
was not used by and/or important to the major part of software, anyways).

RP11-E was just a newer version of RP11-C and supported both RP02 and RP03 disk
drives on the same controller.

*/

static t_stat rr_rd (int32 *data, int32 PA, int32 access)
{
    /* offset by base then decode <4:1> */
    int32 rn = (((PA - rr_dib.ba) & (RP_IOLN - 1)) >> 1) - RP_IOFF;
    UNIT* uptr;

    switch (rn) {
    case 0:                                             /* RPDS */
    case 1:                                             /* RPER */
    case 2:                                             /* RPCS */
        /* RPDS */
        uptr = rr_dev.units + GET_DRIVE(rpcs);          /* selected unit */
        rpds &= RPDS_ATTN;                              /* attention bits */
        if (!(uptr->flags & UNIT_DIS)) {                /* not disabled? */
            rpds |= RPDS_ONLN;
            if (strcmp (uptr->drvtyp->name, RP_RP03) == 0)
                rpds |= RPDS_RP03;
            if (uptr->flags & UNIT_ATT) {               /* attached? */
                rpds |= uptr->STATUS & RPDS_REAL;
                if (uptr->flags & UNIT_WPRT)            /* write locked? */
                    rpds |= RPDS_WLK;
                if (uptr->SEEKING)                      /* still seeking? */
                    rpds |= RPDS_SEEK;
                else if (!uptr->FUNC  &&  !(rpds & (RPDS_INC | RPDS_UNSAFE)))
                    rpds |= RPDS_RDY;                   /* ready! */
            } else
                rpds |= uptr->STATUS & (RPDS_DKER | RPDS_UNSAFE);
        }

        /* RPER */
        rper &= RPER_REAL;
        rper |= RPER_DKER(rpds); 

        /* RPCS */
        rpcs &= RPCS_REAL;
        rpcs |= RPER_HARD(rper) | RPER_SOFT(rper);

        *data = *rr_regs[rn].valp;
        break;

    case 3:                                             /* RPWC */
        *data = rpwc;
        break;

    case 4:                                             /* RPBA */
        *data = rpba;
        break;

    case 5:                                             /* RPCA */
        *data = rpca;
        break;

    case 6:                                             /* RPDA */
        rpda &= RPDA_RW;
        rpda |= (rand() % RP_NUMSC) << RPDA_V_SOT;      /* inject a random sect */
        *data = rpda;                
        break;

    case 10:                                            /* SUCA */
        *data = suca;
        break;

    default:                                            /* not implemented */
        *data = 0;
        return SCPE_OK;
    }
    sim_debug(RRDEB_RRD, &rr_dev, ">>RR  read: %s=%#o\n", rr_regs[rn].name, *data);
    sim_debug_bits(RRDEB_RRD, &rr_dev, rr_regs[rn].bits, *data, *data, 1);
    return SCPE_OK;
}

#define RR_DATOB(r, d)  (PA & 1 ? ((d) << 8) | ((r) & 0377) : ((r) & ~0377) | (d))

static t_stat rr_wr (int32 data, int32 PA, int32 access)
{
    /* offset by base then decode <4:1> */
    int32 rn = (((PA - rr_dib.ba) & (RP_IOLN - 1)) >> 1) - RP_IOFF;
    int32 n, oval = rn < 0 ? 0 : *rr_regs[rn].valp;
    int16 func;

    if (access == WRITEB  &&  2 <= rn  &&  rn <= 6)
        data = RR_DATOB(oval, data);
    switch (rn) {
    case 0:                                             /* RPDS */
        if (access != WRITEB  ||  !(PA & 1)) {
            rpds &= ~(data & RPDS_ATTN);                /* clr attention bits */
            if (!(rpds & RPDS_ATTN)  &&  (rpcs & RPCS_AIE)
                &&  (!(rpcs & CSR_IE)  ||  !(rpcs & CSR_DONE))) {
                sim_debug(RRDEB_INT, &rr_dev, "rr_wr(ATTN:CLR_INT)\n");
                CLR_INT(RR);                            /* clr int request */
            }
        }
        break;

    case 1:                                             /* RPER: read-only */
        break;

    case 2:                                             /* RPCS */
        if (((data & CSR_IE)
             &&  (rpcs & (CSR_DONE | CSR_IE)) == CSR_DONE)  ||
            ((data & RPCS_AIE)
             &&  !(rpcs & RPCS_AIE)  &&  (rpds & RPDS_ATTN))) {
            sim_debug(RRDEB_INT, &rr_dev, "rr_wr(CSR:SET_INT)\n");
            SET_INT(RR);                                /* set int request */
        } else if (rpcs & (CSR_IE | RPCS_AIE)) {
            sim_debug(RRDEB_INT, &rr_dev, "rr_wr(CSR:CLR_INT)\n");
            CLR_INT(RR);                                /* clr int request */
        }
        rpcs &= ~RPCS_RW;
        rpcs |= data & RPCS_RW;
        n = GET_DRIVE(rpcs);                            /* get drive no */
        if (n != GET_DRIVE(oval)) {
            UNIT* uptr = rr_dev.units + n;              /* new selected unit */
            suca = uptr->CYL;
            n = 1;
        } else
            n = 0;                                      /* same old */
        func = GET_FUNC(rpcs);                          /* get function */
        if (((rpcs & CSR_DONE)  ||  func == RPCS_RESET) /* ready or reset? */
            &&   (data & CSR_GO)) {                     /* ...and GO? */
            rr_go(func);                                /* new function! */
        } else if (!(rpcs & CSR_DONE)                   /* not ready? */
            &&  ((data & CSR_GO) | n)) {                /* ...and: GO or desel? */
            rper |= RPER_PGE;                           /* flag error */
        }
        break;

    case 3:                                             /* RPWC */
        rpwc = data & RPWC_IMP;
        break;

    case 4:                                             /* RPBA */
        rpba = data & RPBA_IMP;
        break;

    case 5:                                             /* RPCA */
        rpca = data & RPCA_IMP;
        break;

    case 6:                                             /* RPDA */
        rpda &= ~RPDA_RW;
        rpda |= data & RPDA_RW;
        break;

    case 10:                                            /* SUCA: read-only */
        break;

    default:
        return SCPE_OK;
    }
    sim_debug(RRDEB_RWR, &rr_dev, ">>RR write: %s=%#o\n", rr_regs[rn].name, data);
    /* note that this is post-op; so e.g. it won't ever show the GO bit as 1 */
    sim_debug_bits(RRDEB_RWR, &rr_dev, rr_regs[rn].bits, oval, *rr_regs[rn].valp, 1);
    return SCPE_OK;
}

/* Complete seek initiation */

static t_stat rr_seek_init (UNIT *uptr)
{
    rr_set_done(0);                                     /* set done */
    ASSURE(uptr->SEEKING);
    uptr->action = rr_svc;
    sim_activate(uptr, uptr->SEEKING);                  /* seek ends then */
    return SCPE_OK;
}

/* Initiate new function */

static void rr_go (int16 func)
{
    int32 i, cyl, head;
    t_bool rd, wr;
    UNIT* uptr;

    ASSURE(func == GET_FUNC(rpcs));

    if (func == RPCS_RESET) {                           /* control reset? */
        rpds = 0;
        rper = 0;
        rpcs = CSR_DONE | (rpcs & CSR_IE);
        rpwc = 0;
        rpba = 0;
        rpca = 0;
        rpda = 0;
        suca = rr_dev.units[0].CYL;
        for (i = 0;  i < RP_NUMDR;  ++i) {
            uptr = rr_dev.units + i;
            sim_cancel(uptr);
            uptr->action = rr_svc;
            uptr->SEEKING = 0;
            uptr->STATUS = 0;
            uptr->FUNC = 0;
        }
        if (rpcs & CSR_IE) {
            sim_debug(RRDEB_INT, &rr_dev, "rr_go(RESET:SET_INT)\n");
            SET_INT(RR);                                /* set int request */
        } else {
            sim_debug(RRDEB_INT, &rr_dev, "rr_go(RESET:CLR_INT)\n");
            CLR_INT(RR);                                /* clr int request */
        }
        return;
    }

    ASSURE(rpcs & CSR_DONE);

    rr_clr_done();                                      /* clear done */
    rper  = 0;                                          /* clear errors */
    rpcs &= ~(CSR_ERR | RPCS_HERR);                     /* clear summary */
    i = GET_DRIVE(rpcs);                                /* get drive no */
    uptr = rr_dev.units + i;                            /* selected unit */
    ASSURE(uptr->action == rr_svc);
    ASSURE(uptr->SEEKING  ||  !uptr->FUNC);             /* SEEK underway or idle */
    uptr->STATUS &= ~(RPDS_DKER | RPDS_WLK);            /* clear drive errors */

    if (!(uptr->flags & UNIT_ATT)) {                    /* not attached? */
        rr_set_done(RPER_PGE);                          /* unit offline */
        return;
    }
    if (uptr->STATUS & RPDS_UNSAFE) {                   /* file unsafe? */
        rr_set_done(RPER_FUV);                          /* unsafe violation */
        return;
    }

    ASSURE(!uptr->FUNC  ||  uptr->FUNC == RPCS_HOME  ||  uptr->FUNC == RPCS_SEEK);
    if ((uptr->FUNC == RPCS_HOME  &&  func != RPCS_HOME)  ||
        (uptr->FUNC == RPCS_SEEK  &&  func == RPCS_SEEK)) {
        rr_set_done(RPER_PGE);                          /* no can't do */
        return;
    }
    /* RP11 allows to stack up an I/O command on top of an ongoing SEEK.  The
     * I/O gets performed once the SEEK has completed (with updated DA from the
     * I/O command).  XXDP checks that.
     * RPDS_SEEK in the unit's STATUS means that drive is performing a SEEK as
     * an initiation for an I/O (which can be a part of the topped SEEK).  OTOH,
     * SEEKING denotes that either a HOME/SEEK command is in progress (per FUNC)
     * or SEEK was in progress before the stacked I/O command (stored in FUNC). */
    ASSURE(!(uptr->STATUS & RPDS_SEEK));

    rd = func == RPCS_READ   ||  func == RPCS_RD_NOSEEK  ||  func == RPCS_WCHK;
    wr = func == RPCS_WRITE  ||  func == RPCS_WR_NOSEEK;

    if (rd | wr) {
        int32 sect = GET_SECT(rpda);                    /* get sect */
        if (sect >= RP_NUMSC)                           /* sect out of range? */
            rper |= RPER_NXS;
    }
    if (func == RPCS_HOME) {
        head = 0;
        cyl  = 0;
    } else if (func == RPCS_RD_NOSEEK  ||  func == RPCS_WR_NOSEEK) {
        head = uptr->HEAD;
        cyl  = uptr->CYL;
        ASSURE(uptr->CYL < (int32)uptr->drvtyp->cyl  &&  uptr->HEAD < RP_NUMSF);
    } else {
        head = GET_TRACK(rpda);
        cyl  = rpca;
        if (head >= RP_NUMSF)                           /* bad head? */
            rper |= RPER_NXT;
        if (cyl >= (int32)uptr->drvtyp->cyl)                   /* bad cyl? */
            rper |= RPER_NXC;
    }

    if (wr  &&  (uptr->flags & UNIT_WPRT))              /* write and locked? */
        rper |= RPER_WPV;

    if (rper) {                                         /* any errors? */
        rr_set_done(0);                                 /* set done (w/errors) */
        return;
    }

    /* seek time */
    if (func == RPCS_HOME)
        i  = uptr->drvtyp->seek_ave / 2;
    else if (!(i = abs(cyl - uptr->CYL)))
        i  = uptr->drvtyp->seek_1 / 2;
    else if (i <= 2)
        i *= uptr->drvtyp->seek_1;
    else if (i <= (3 * (int32)uptr->drvtyp->cyl) / 4)
        i  = uptr->drvtyp->seek_ave;
    else
        i  = uptr->drvtyp->seek_max;
    if (func == RPCS_HOME  ||  func == RPCS_SEEK) {     /* seek? */
        uptr->action = rr_seek_init;
        uptr->SEEKING = i;                              /* drive is seeking */
        i = 10;                                         /* enough for 16us */
        /* XXDP ZRPF-B, Test 0 (cylinder positioning) fails because, even though
         * it initializes INTFLG late (_after_ initiating the SEEK), the delayed
         * CS_DONE here helps avoid the race (and an error) but only for the
         * first iteration (cylinder 0).  The next and all subsequent iterations
         * begin with clearing attention bits in RDPS with a BIC instruction,
         * like so: "BIC @#RPDS, @#RPDS", resulting in bare 0 actually sent by
         * ALU to the UNIBUS in DATO cycle to RPDS, and causing no bits reset.
         * That, in turn, causes the interrupt to occur immediately when RPCS is
         * written with the new SEEK/AIE function (the attention bit still set)
         * for the next cylinder, and that causes INTFLG to get set by the ISR
         * just prior to when the test clears it.  Thus, the test is unable to
         * confirm the interrupt for all remaining cylinders.  ZRPB-E fixes both
         * bugs by initializing INTFLG before firing up SEEK/AIE, and also by
         * using the more correct "MOVB @#RPDS, @#RPDS", instead of BIC. */
    } else {
        if (cyl != uptr->CYL  ||  head != uptr->HEAD) {
            ASSURE(func != RPCS_RD_NOSEEK  &&  func != RPCS_WR_NOSEEK);
            uptr->STATUS |= RPDS_SEEK;                  /* drive is seeking */
        }
        i += RP_ROT_12;                                 /* I/O takes longer */
        /* XXDP ZRPB-E / ZRPF-B have two data race conditions in Test 5 (data
         * reliability), which in ZRPB-E can be worked around with the following
         * multiplier for all 15 steady patterns, but it does not help eliminate
         * the second race in the last (random) pattern test, despite showing no
         * actual data discrepancies. */
#if 0
        if (func == RPCS_READ)
            i *= 64;                                    /* to use w/XXDP tests */
#endif
    }
    sim_activate(uptr, i);                              /* schedule */

    uptr->FUNC = func;                                  /* save new func */
    uptr->HEAD = head;                                  /* save head too */
    uptr->CYL  = cyl;                                   /* put on cylinder */
    return;
}

/* Complete seek */

static void rr_seek_done (UNIT *uptr, t_bool cancel)
{
    int32 n = (int32)(uptr - rr_dev.units);             /* get unit number */

    if (n == GET_DRIVE(rpcs))
        suca = cancel ? 0 : uptr->CYL;                  /* update cyl shown */
    if (uptr->SEEKING) {                                /* was seek cmd pending? */
        ASSURE((1 << n) & RPDS_ATTN);
        rpds |= 1 << n;                                 /* set attention */
        if (rpcs & RPCS_AIE) {                          /* att ints enabled? */
            sim_debug(RRDEB_INT, &rr_dev, "rr_seek_done(SET_INT)\n");
            SET_INT(RR);
        }
        uptr->SEEKING = 0;                              /* seek completed */
    }
    uptr->STATUS &= ~RPDS_SEEK;                         /* no longer seeking */
    return;
}

/* Service a unit

   If seek in progress, complete seek command
   Then if I/O pending, complete data transfer command

   The unit control block contains the function and disk address for
   the current command.

   Some registers must be revalidated because they could have been
   modified in between go() and now.
*/

static t_stat rr_svc (UNIT *uptr)
{
    uint32 n, cyl, head, sect, da, wc;
    int16 func = uptr->FUNC;
    t_seccnt todo, done;
    t_stat ioerr;
    uint32 ma;
    t_bool wr;

    ASSURE(func);
    uptr->FUNC = 0;                                     /* idle */

    rr_seek_done(uptr, 0);                              /* complete seek, if any */
    ASSURE(!uptr->SEEKING  &&  !(uptr->STATUS & RPDS_SEEK));
    if (func == RPCS_HOME  ||  func == RPCS_SEEK)
        return SCPE_OK;                                 /* all done */

    ASSURE(~(rpcs & CSR_DONE));

    if (!(uptr->flags & UNIT_ATT)) {                    /* not attached? */
        rr_set_done(RPER_PGE);                          /* unit offline */
        return SCPE_UNATT;
    }
    if (uptr->STATUS & RPDS_UNSAFE) {                   /* file unsafe? */
        rr_set_done(RPER_FUV);                          /* unsafe violation */
        return SCPE_OK;
    }

    wr = func == RPCS_WRITE  ||  func == RPCS_WR_NOSEEK;

    n = (int32)(uptr - rr_dev.units);                   /* get drive no */

    sect = GET_SECT(rpda);                              /* get sect */
    if (sect >= RP_NUMSC)                               /* sect out of range? */
        rper |= RPER_NXS;
    head = uptr->HEAD;
    cyl  = uptr->CYL;

    if (wr) {
        if ((wloa & RPWLOA_ON)  &&  !rper/*valid DA*/
            &&  ((n <= GET_WLOADRV(wloa))  ||  (cyl <= GET_WLOACYL(wloa)))) {
            uptr->STATUS |= RPDS_WLK;                   /* DA write-locked */
            rper |= RPER_WPV;
        } else if (uptr->flags & UNIT_WPRT)             /* write and locked? */
            rper |= RPER_WPV;
    }

    if (rper) {                                         /* control in error? */
        rr_set_done(0);
        return SCPE_OK;
    }
    /* rper == 0: drive remained selected */
    ASSURE(n == GET_DRIVE(rpcs));

    wc = 0200000 - rpwc;                                /* get wd cnt */
    ASSURE(wc <= RP_MAXFR);
    ASSURE(cyl < uptr->drvtyp->cyl  &&  head < RP_NUMSF);
    da = GET_DA(cyl, head, sect);                       /* form full disk addr */
    ASSURE(da < uptr->drvtyp->size);
    n = uptr->drvtyp->size - da;                        /* sectors available */

    if (rpcs & RPCS_HDR) {                              /* header ops? */
        if (!(rpcs & RPCS_MODE))                        /* yes: 18b mode? */
            rper |= RPER_MODE;                          /* must be in 18b mode */
        else if ((!wr  &&  wc != 3)  ||  (wr  &&  wc % 3)) /* DEC-11-HRPCA-C-D 3.8 */
            rper |= RPER_PGE;
        else if (wr)
            n *= 3;                                     /* 3 wds per sector */
        else                                            /* a typo in doc??? */
            n  = 3;                                     /* can only read 3 wds */
    } else {                                            /* no: regular R/W */
        /* RP11 can actually handle the PDP-10/-15 (18b) mode on PDP-11 using 3
         * words per transfer, combined as two 18b words in a disk sector (for
         * the bit assignments in the triplet, see rr_svc() for format reading).
         * However, a sector written in this mode is marked as such and cannot be
         * read back in the PDP-11 (16b) mode (but only in the 18b mode).  Since
         * the disk containers do not have sector format information, this mode
         * cannot be supported (and for all intents and purposes it's not needed
         * for any real PDP-11 I/O).  XXDP has some test for this, though. */
        if (rpcs & RPCS_MODE)
            rper |= RPER_MODE;                          /* must be in PDP-11 mode */
#if 0   /* per doc, wc must be even; but DOS/Batch uses odd wc xfers (?!) */
        else if (wc & 1)                                /* must be even */
            rper |= RPER_PGE;
#endif
        else
            n = RP_SIZE(n);                             /* can do this many wrds */
    }

    if (rper) {                                         /* any new errors? */
        rr_set_done(0);                                 /* set done (w/errors) */
        return SCPE_OK;
    }
    if (wc > n)
        wc = n;                                         /* trim word count */
    ASSURE(wc);

    /* A note on error handling:
     * RP11 processes data words between drive and memory absolutely sequentially
     * (not by the sector like the SIMH API provides).  Therefore, controller
     * errors should be asserted to follow this scenario.
     *
     * 1.  When reading from disk, an I/O error must be deferred until all words
     * (read from the disk so far) have been verified not to cause NXM.  If any
     * word did, then NXM gets reported, and the I/O error gets discarded
     * (because the real controller would have stopped the operation right then
     * and there, and would not have encountered the (later) I/O condition).
     *
     * 2.  When writing, I/O errors take precedence, provided that words keep
     * passing the address check on extraction from memory.  But no NXM should
     * be reported for any words that reside after the completed I/O boundary in
     * case of a short write to disk.
     *
     * 3.  Disk pack overrun is strictly a run-off of an otherwise successful
     * completion, which has left a residual word counter non-zero, because had
     * an earlier error stopped the disk operation, the overrun situation could
     * not have been experienced (the end of pack would not have been reached).
     */

    ma = ((rpcs & RPCS_MEX) << (16 - RPCS_V_MEX)) | rpba; /* get mem addr */

    if (!wr) {                                          /* read: */
        if (rpcs & RPCS_HDR) {                          /* format? */
            /* Sector header is loaded in the 36-bit Buffer Register(BR):
               17 0-bits, 9-bit cyl, 5-bit track, a spare bit, 4-bit sect */
            rpxb[0] = 0;                                /* BR<35:20> */
            rpxb[1] = (cyl << 6) | (head << 1);         /* BR<19:04> */
            rpxb[2] = sect;                             /* BR<03:00> */
            ioerr = 0;
            done = 1;                                   /* 1 sector done */
        } else {                                        /* normal read: */
            DEVICE* dptr = find_dev_from_unit(uptr);
            todo = (wc + (RP_NUMWD - 1)) / RP_NUMWD;    /* sectors to read */
            ioerr = sim_disk_rdsect(uptr, da, (uint8*) rpxb, &done, todo);
            n = RP_SIZE(done);                          /* words read */
            sim_disk_data_trace(uptr, (uint8*) rpxb, da, n * sizeof(*rpxb), "rr_read",
                                RRDEB_DAT & (dptr->dctrl | uptr->dctrl), RRDEB_OPS);
            ASSURE(done <= todo);
            if (done >= todo)
                ioerr = 0;                              /* good stuff */
            else if (ioerr)
                wc = n;                                 /* short, adj wd cnt */
            else {
                todo -= done;                           /* to clear... */
                todo *= RP_SIZE(sizeof(*rpxb));         /* ...bytes */
                memset(rpxb + n, 0, todo);
            }
        }
        if (func == RPCS_WCHK) {
            uint32 a = ma;
            for (n = 0;  n < wc;  ++n) {                /* loop thru buf */
                RPCONTR data;
                if (MAP_RDW(a, 1, &data)) {             /* mem wd */
                    rper |= RPER_NXM;                   /* NXM? set flg */
                    wc = n;                             /* adj wd cnt */
                    break;
                }
                a += 2;
                if (rper | ioerr)
                    continue;
                if (data != rpxb[n])                    /* match to disk? */
                    rper |= RPER_WCE;                   /* no, err */
            }
        } else if ((n = MAP_WRW(ma, wc, rpxb))) {       /* store buf */
            rper |= RPER_NXM;                           /* NXM? set flag */
            wc -= n;                                    /* adj wd cnt */
        }
        if (!rper  &&  ioerr) {                         /* all wrds ok but I/O? */
            rper |= RPER_FMTE;                          /* report as FMTE */
            uptr->STATUS |= RPDS_HNF;                   /* sector not found */
            if (func == RPCS_WCHK)
                rper |= RPER_WCE;                       /* write-check err, too */
        }
        if (rper)                                       /* adj sector count */
            done = (wc + (RP_NUMWD - 1)) / RP_NUMWD;    /* NB: works for HDR, too */
    } else {                                            /* write: */
        if ((n = MAP_RDW(ma, wc, rpxb)))                /* get buf */
            wc -= n;                                    /* adj wd cnt */
        if (wc  &&  !(rpcs & RPCS_HDR)) {               /* regular write? */
            DEVICE* dptr = find_dev_from_unit(uptr);
            int32 m = (wc + (RP_NUMWD - 1)) & ~(RP_NUMWD - 1); /* clr to... */
            memset(rpxb + wc, 0, (m - wc) * sizeof(*rpxb)); /* ...end of sect */
            sim_disk_data_trace(uptr, (uint8*) rpxb, da, m * sizeof(*rpxb), "rr_write",
                                RRDEB_DAT & (dptr->dctrl | uptr->dctrl), RRDEB_OPS);
            todo = m / RP_NUMWD;                        /* sectors to write */
            ASSURE(!(m % RP_NUMWD));
            ioerr = sim_disk_wrsect(uptr, da, (uint8*) rpxb, &done, todo);
            ASSURE(done <= todo);
            if (done < todo) {                          /* short write? */
                wc = RP_SIZE(done);                     /* words written */
                rper |= RPER_FMTE;                      /* report as FMTE */
                uptr->STATUS |= RPDS_HNF;               /* sector not found */
                if (!ioerr)
                    ioerr = 1;                          /* must be reported! */
            } else if (n) {
                rper |= RPER_NXM;                       /* NXM? set flg */
                ioerr = 0;                              /* don't care */
            } else
                ioerr = 0;                              /* good stuff */
        } else {
            ioerr = 0;                                  /* good stuff */
            done = wc / 3;
            if (n)
                rper |= RPER_NXM;                       /* NXM? set flg */
        }
    }
    ASSURE(!ioerr  ||  rper);
    ASSURE(!wc  ||  done);
    ASSURE(wc  ||  rper);

    rpwc += wc;
    rpwc &= RPWC_IMP;
    ma   += wc << 1;
    rpba  = ma & RPBA_IMP;
    rpcs &= ~RPCS_MEX;
    rpcs |= (ma >> (16 - RPCS_V_MEX)) & RPCS_MEX;
    if (rpwc  &&  !rper)
        rper |= RPER_EOP;                               /* disk pack overrun */

    da += done ? done : 1;                              /* update DA */
    ASSURE(da <= uptr->drvtyp->size);
    sect = da % RP_NUMSC;                               /* new sector */
    head = da / RP_NUMSC;                               /* new head (w/cyl) */
    todo = head / RP_NUMSF;                             /* new cyl (tentative) */
    if (todo == uptr->drvtyp->cyl) {                    /* at the end? */
        cyl   = uptr->drvtyp->cyl - 1;                  /* keep on last cyl */
        todo  = 0;                                      /* wrap cyl for rpda */
        head  = 0;                                      /* ...and head, too */
        ASSURE(!sect);
    } else {
        cyl   = todo;                                   /* new cyl */
        head %= RP_NUMSF;                               /* isolate head */
    }
    uptr->HEAD = head;                                  /* update head */
    if ((func == RPCS_RD_NOSEEK  ||  func == RPCS_WR_NOSEEK) /* no SEEK I/O... */
        &&  (uptr->CYL != cyl                           /* ...and: arm moved or... */
             ||  (rper & RPER_EOP))) {                  /* ...boundary exceeded? */
        n = (int32)(uptr - rr_dev.units);               /* get unit number */
        ASSURE((1 << n) & RPDS_ATTN);
        rpds |= 1 << n;                                 /* set attention */
        if (rpcs & RPCS_AIE) {                          /* att ints enabled? */
            sim_debug(RRDEB_INT, &rr_dev, "rr_svc(SET_INT)\n");
            SET_INT(RR);                                /* request interrupt */
        }
    }
    uptr->CYL = cyl;                                    /* update cyl */
    rpda = (head << RPDA_V_TRACK) | sect;               /* updated head / sect */
    rpca = todo;                                        /* wrapped up cyl */
    suca = cyl;                                         /* updated real cyl */

    rr_set_done(0);                                     /* all done here */

    if (ioerr) {                                        /* I/O error? */
        const char* name = uptr->uname;
        const char* file = uptr->filename;
        const char* errstr = errno ? strerror(errno) : "";
        sim_printf("RR%u %s [%s:%s] FUNC=%o(%c) RPER=%06o I/O error (%s)%s%s",
                   (int) GET_DRIVE(rpcs), uptr->drvtyp->name,
                   name ? name : "???", file ? file : "<NULL>",
                   (int) func, "WR"[!wr], (int) rper, sim_error_text(ioerr),
                   errstr  &&  *errstr ? ": " : "", errstr ? errstr : "");
        return SCPE_IOERR;
    }
    return SCPE_OK;
}

/* Interrupt state change routines

   rr_clr_done          clear done
   rr_set_done          set done (and errors)
   rr_inta              interrupt acknowledge
*/

static void rr_clr_done (void)
{
    ASSURE(rpcs & CSR_DONE);
    rpcs &= ~CSR_DONE;                                  /* clear done */
    if ((rpcs & CSR_IE)  &&  (!(rpcs & RPCS_AIE)  ||  !(rpds & RPDS_ATTN))) {
        sim_debug(RRDEB_INT, &rr_dev, "rr_clr_done(CLR_INT)\n");
        CLR_INT(RR);                                    /* clear int req */
    }
    return;
}

static void rr_set_done (int32 err)
{
    ASSURE(~(rpcs & CSR_DONE));
    rper |= err;
    rpcs |= CSR_DONE;                                   /* set done */
    if (rpcs & CSR_IE) {                                /* int enable? */
        sim_debug(RRDEB_INT, &rr_dev, "rr_set_done(SET_INT)\n");
        SET_INT(RR);                                    /* request int */
    }
    return;
}

static int32 rr_inta (void)
{
    sim_debug(RRDEB_INT, &rr_dev, "rr_inta()\n");
    ASSURE(((rpcs & RPCS_AIE)  &&  (rpds & RPDS_ATTN))  ||
            ((rpcs & CSR_IE)  &&  (rpcs & CSR_DONE)));
    rpcs &= ~RPCS_AIE;                                  /* AIE is one-shot */
    return rr_dib.vec;                                  /* return vector */
}

/* Device reset */

static t_stat rr_reset (DEVICE *dptr)
{
    static t_bool inited = FALSE;
    int32 i;

    /* compile-time sanity check first */
    ASSURE(sizeof(rr_regs)/sizeof(rr_regs[0]) == RP_IOLN/2 - RP_IOFF);

    if (!inited) {
        inited = TRUE;
        for (i = 0;  i < RP_NUMDR;  ++i) {
            dptr->units[i].action = &rr_svc;
            dptr->units[i].flags = UNIT_FIX | UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE;
            sim_disk_set_drive_type_by_name (&dptr->units[i], "RP03");
        }
    }

    /* clear everything now */
    rpds = 0;
    rper = 0;
    rpcs = CSR_DONE;
    rpwc = 0;
    rpba = 0;
    rpca = 0;
    rpda = 0;
    suca = 0;
    for (i = 0;  i < RP_NUMDR;  ++i) {
        UNIT* uptr = rr_dev.units + i;
        sim_cancel(uptr);
        uptr->action = rr_svc;
        uptr->SEEKING = 0;
        uptr->STATUS = 0;
        uptr->FUNC = 0;
        uptr->HEAD = 0;
        uptr->CYL = 0;
    }
    ASSURE(dptr == &rr_dev);
    sim_debug(RRDEB_INT, dptr, "rr_reset(CLR_INT)\n");
    CLR_INT(RR);
    if (rpxb == NULL)
        rpxb  = (RPCONTR*) calloc(RP_MAXFR, sizeof (*rpxb));
    if (rpxb == NULL)
        return SCPE_MEM;
    return auto_config(NULL, 0);
}

/* Attach/detach routines */

static t_stat rr_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat err = sim_disk_attach(uptr, cptr,
                                 RP_SIZE(sizeof(*rpxb)), sizeof(*rpxb),
                                 TRUE, 0, uptr->drvtyp->name,
                                 0, 0);
    if (err == SCPE_OK  &&  !(uptr->STATUS & RPDS_DKER))
        uptr->STATUS &= ~RPDS_UNSAFE;
    return err;
}

static t_stat rr_detach (UNIT *uptr)
{
    int16 func = uptr->FUNC;
    rr_seek_done(uptr, 1/*cancel*/);
    if (func) {
        uptr->FUNC = 0;                                 /* idle now */
        sim_cancel(uptr);
        if (func == RPCS_SEEK)
            uptr->STATUS |= RPDS_INC;                   /* seek incomplete */
        else if (func != RPCS_HOME) {
            uptr->STATUS |= RPDS_HNF;                   /* sector not found */
            rr_set_done(RPER_TE);
        }
    }
    uptr->STATUS |= RPDS_UNSAFE;                        /* must reset before use */
    ASSURE(!sim_is_active(uptr));
    ASSURE(!uptr->SEEKING);
    uptr->action = rr_svc;
    uptr->HEAD = 0;
    uptr->CYL = 0;
    return sim_disk_detach(uptr);
}

/* Set WLOA */

static t_stat rr_set_wloa (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE* dptr = find_dev_from_unit(uptr);
    if (!cptr  ||  !*cptr)
        return SCPE_2FARG;
    if (strncasecmp(cptr, "OFF", 3) == 0) {
        cptr += 3;
        if (*cptr == ';')
            return SCPE_2MARG;
        if (*cptr)
            return SCPE_ARG;
        wloa &= ~RPWLOA_ON;
        return SCPE_OK;
    }
    if (strncasecmp(cptr, "ON", 2) != 0)
        return SCPE_ARG;
    cptr += 2;
    if (*cptr == ';') {
        char* end;
        long val;
        if (!*++cptr)
            return SCPE_MISVAL;
        errno = 0;
        val = strtol(cptr, &end, 0);
        if (errno  ||  !end  ||  *end  ||  end == cptr  ||  (val & ~RPWLOA_IMPL))
            return SCPE_ARG;
        wloa &= ~RPWLOA_IMPL;
        wloa |= val;
    } else if (*cptr)
        return SCPE_ARG;
    wloa |= RPWLOA_ON;
    return SCPE_OK;
}

/* Device bootstrap */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR + 12 */
#define BOOT_LEN        (sizeof (rr_boot_rom) / sizeof (rr_boot_rom[0]))

static const uint16 rr_boot_rom[] = {
/* EXPECTED M9312 REGISTER USE FOR BOOT PROMS (IN THE BOOTED SOFTWARE):                                *
 * R0     = UNIT NUMBER                                                                                *
 * R1     = CONTROLLER CSR                                                                             *
 * R2, R3 = TEMPORARIES                                                                                *
 * R4     = ALWAYS POINTS TO PROM BASE + 20 (HELPS LOCATE THE BOOTED DEVICE DESIGNATION)               *
 * R5     = LAST COMMAND DATA (E.G. LOAD ADDR, EXAM DATA; OTHERWISE, JUNK)                             *
 * R6(SP) = PC OF THE COMMAND START (CONTAINS THE ADDRESS WHERE THE BOOT COMMAND ORIGINATED FROM)      */
/*                                              .TITLE RP11 BOOT M9312 STYLE - TONY LAWRENCE (C) 2023  */
/*                                              .ASECT                                                 */
/* 002000                                       .=2000                                                 */
/* 002000 */ 0042120,                 /* START: .WORD   "PD             ; "DP" (RP DEVICE DESIGNATION) */
/* 002002 */ 0012706, BOOT_ENTRY,     /* BOOT:  MOV     #BOOT, SP       ; ENTRY POINT PC               */
/* 002006 */ 0112700, 0000000,        /*        MOVB    #0, R0          ; UNIT NUMBER                  */
/* 002012 */ 0012701, 0176726,        /*        MOV     #176726, R1     ; RPCS + 12                    */
/* 002016 */ 0012704, BOOT_START+020, /*        MOV     #<START+20>, R4 ; BACKLINK TO PROM W/OFFSET 20 */
/* 002022 */ 0005041,                 /*        CLR     -(R1)           ; DISK ADDRESS                 */
/* 002024 */ 0005041,                 /*        CLR     -(R1)           ; CYLINDER ADDRESS             */
/* 002026 */ 0005041,                 /*        CLR     -(R1)           ; MEMORY ADDRESS               */
/* 002030 */ 0012741, 0177000,        /*        MOV     #-512., -(R1)   ; WORD CT (ALWAYS 2 FULL SECTS)*/
/* 002034 */ 0010003,                 /*        MOV     R0, R3                                         */
/* 002036 */ 0000303,                 /*        SWAB    R3              ; MOVE UNIT# INTO POSITION     */
/* 002040 */ 0052703, 0000005,        /*        BIS     #5, R3          ; COMBINE READ+GO FUNCTION     */
/* 002044 */ 0010341,                 /*        MOV     R3, -(R1)       ; DO IT!                       */
/* 002046 */ 0005005,                 /*        CLR     R5              ; M9312 USES FOR DISPLAY       */
/* 002050 */ 0105711,                 /* 1$:    TSTB    (R1)            ; READY?                       */
/* 002052 */ 0100376,                 /*        BPL     1$              ; BR IF NOT                    */
/* 002054 */ 0005711,                 /*        TST     (R1)            ; ERROR?                       */
/* 002056 */ 0100002,                 /*        BPL     2$              ; BR IF NOT                    */
/* 002060 */ 0000005,                 /*        RESET                                                  */
/* 002062 */ 0000747,                 /*        BR      BOOT            ; START OVER                   */
/* 002064 */ 0105011,                 /* 2$:    CLRB    (R1)            ; CLEAR CONTROLLER             */
/* 002066 */ 0005007                  /*        CLR     PC              ; JUMP TO BOOTSTRAP            */
/* 002070                                       .END                                                   */
};

static t_stat rr_boot (int32 unitno, DEVICE *dptr)
{
    size_t i;
    ASSURE(dptr == &rr_dev);
    for (i = 0;  i < BOOT_LEN;  ++i)
        WrMemW(BOOT_START + (2 * i), rr_boot_rom[i]);
    WrMemW(BOOT_UNIT, unitno & (RP_NUMDR - 1));
    WrMemW(BOOT_CSR, (rr_dib.ba & DMASK) + (014/*CSR*/ + 012));
    cpu_set_boot(BOOT_ENTRY);
    return SCPE_OK;
}

/* Misc */

#define RP_DESCRIPTION  RP_RP11 "/" RP_RP02 "/" RP_RP03 " disk pack device"

static t_stat rr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    size_t i;
    fputs(
    /*567901234567890123456789012345678901234567890123456789012345678901234567890*/
    RP_DESCRIPTION "\n\n"
    "A detailed description of this device can be found in the\n"
    "\"PDP-11 Peripherals Handbook\" (1973 - 1976) and in the technical manual\n"
    "\"RP11-C Disk Pack Drive Controller Maintenance Manual\" (1974)\n"
    "(DEC-11-HRPCA-C-D).\n\n"
    "In default configuration " RP_RP11 " responds to the range 17776700 - 17776736\n"
    "with the first 4 word locations not occupied by any device registers (and\n"
    "so 17776710 is the first used location).  Some operating systems want you\n"
    "to specify the extended range (e.g. RSTS/E), but some -- the relevant range\n"
    "(17776710 - 17776736), yet some just want to know where the CSR is located\n"
    "(17776714 by default), so they can auto-calculate the range on their own.\n\n"
    "Disk drive parameters (all decimal):\n\n"
    "        Cylinders    Heads  Sects/Trk     Capacity    Average access\n"
    "      Total   Spare                   Nominal  Usable    time, ms\n", st);
    for (i = 0;  i < sizeof(drv_typ)/sizeof(drv_typ[0]);  ++i) {
        uint32 spare = GET_DA(drv_typ[i].spare, RP_NUMSF, RP_NUMSC);
        uint32 total = drv_typ[i].size;
        if (drv_typ[i].name == NULL)
            continue;
        fprintf(st, "%.6s: %5u   %5u  %5u  %5u"
                "    %5.1fMB  %5.1fMB   %5u.%1u\n", drv_typ[i].name,
                drv_typ[i].cyl, drv_typ[i].spare, RP_NUMSF, RP_NUMSC,
                RP_SIZE(total - spare) / .5e6, RP_SIZE(total) / .5e6,
                (drv_typ[i].seek_ave + RP_ROT_12)/10,
                (drv_typ[i].seek_ave + RP_ROT_12)%10);
    }
    fputs("\n"
    "The implementation does not include any maintenance registers or disk/sector\n"
    "formatting operations yet supports the Write Lockout Address (LOA) register,\n"
    "which can be set with a PROTECT command:\n\n"
    "    sim> set RR PROTECT=ON;0407\n\n"
    "to turn the protection on (in this case, the entire units 0 and 1, and\n"
    "7 x 2 + 1 = 15(10) first cylinders of unit 2 will become write-locked).\n"
    "The current setting can be obtained by examining the WLOA register in\n"
    "the device (the sign bit not present in hardware controls the feature):\n\n"
    "    sim> examine RR WLOA\n"
    "    WLOA:   100407  PROTECT=ON DRV=1 CYL2=7\n\n"
    "To remove the lockout:\n\n"
    "    sim> set RR PROTECT=OFF\n"
    "    sim> examine RR WLOA\n"
    "    WLOA:   000407  PROTECT=OFF DRV=1 CYL2=7\n\n"
    "Note that it does not clear the address but turns the feature off.  Also,\n"
    "the WLOA register is unaffected by the device RESET.\n", st);
    fprint_set_help (st, dptr);
    fprint_show_help(st, dptr);
    fprintf(st,
    "\nThe " RP_RP11 " is disabled in a Qbus system with more than 256KB of memory.\n");
    fprint_reg_help (st, dptr);
    return SCPE_OK;
}

static const char *rr_description (DEVICE *dptr)
{
    return RP_DESCRIPTION;
}

/* Show / switch controller type */

static t_stat rr_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fputs(RP_RP11, st);
    return SCPE_OK;
}

#elif !defined(UC15)
#error "RP11/-C/-E can only be used in PDP-11 configuration"
#endif /*VM_PDP11 && !UC15*/
