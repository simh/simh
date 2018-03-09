/* pdp11_tu.c - PDP-11 TM02/TU16 TM03/TU45/TU77 Massbus magnetic tape controller

   Copyright (c) 1993-2017, Robert M Supnik

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

   tu           TM02/TM03 magtape

   28-Dec-17    RMS     Read tape mark must set Massbus EXC
   13-Mar-17    RMS     Annotated fall through in switch
   23-Oct-13    RMS     Revised for new boot setup routine
   18-Apr-11    MP      Fixed t_addr printouts for 64b big-endian systems
   17-May-07    RMS     CS1 DVA resides in device, not MBA
   29-Apr-07    RMS     Fixed bug in setting FCE on TMK Naoki Hamada)
   16-Feb-06    RMS     Added tape capacity checking
   12-Nov-05    RMS     Changed default formatter to TM03 (for VMS)
   31-Oct-05    RMS     Fixed address width for large files
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   31-Mar-05    RMS     Fixed inaccuracies in error reporting
   18-Mar-05    RMS     Added attached test to detach routine
   10-Sep-04    RMS     Cloned from pdp10_tu.c

   Magnetic tapes are represented as a series of variable 8b records
   of the form:

        32b record length in bytes - exact number, sign = error
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b record length in bytes - exact number, sign = error

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a single record length of 0.
   End of tape is two consecutive end of file marks.
*/

#if defined (VM_PDP10)
#error "PDP-10 uses pdp10_tu.c!"

#elif defined (VM_PDP11)
#include "pdp11_defs.h"
#define DEV_DIS_INIT    DEV_DIS

#elif defined (VM_VAX)
#include "vax_defs.h"
#define DEV_DIS_INIT    0
#if (!UNIBUS)
#error "Qbus not supported!"
#endif

#endif
#include "sim_tape.h"

#define TU_NUMFM        1                               /* #formatters */
#define TU_NUMDR        8                               /* #drives */
#define USTAT           u3                              /* unit status */
#define UDENS           u4                              /* unit density */
#define  UD_UNK         0                               /* unknown */
#define MT_MAXFR        (1 << 16)                       /* max data buf */
#define DEV_V_TM03      (DEV_V_FFUF + 0)                /* TM02/TM03 */
#define DEV_TM03        (1 << DEV_V_TM03)
#define UNIT_V_TYPE     (MTUF_V_UF + 0)
#define UNIT_M_TYPE     03
#define UNIT_TYPE       (UNIT_M_TYPE << UNIT_V_TYPE)
#define UNIT_TE16       (0 << UNIT_V_TYPE)
#define UNIT_TU45       (1 << UNIT_V_TYPE)
#define UNIT_TU77       (2 << UNIT_V_TYPE)
#define GET_TYPE(x)     (((x) >> UNIT_V_TYPE) & UNIT_M_TYPE)

/* CS1 - offset 0 */

#define CS1_OF          0
#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_N_FNC       (CS1_M_FNC + 1)
#define  FNC_NOP        000                             /* no operation */
#define  FNC_UNLOAD     001                             /* unload */
#define  FNC_REWIND     003                             /* rewind */
#define  FNC_FCLR       004                             /* formatter clear */
#define  FNC_RIP        010                             /* read in preset */
#define  FNC_ERASE      012                             /* erase tape */
#define  FNC_WREOF      013                             /* write tape mark */
#define  FNC_SPACEF     014                             /* space forward */
#define  FNC_SPACER     015                             /* space reverse */
#define FNC_XFER        024                             /* >=? data xfr */
#define  FNC_WCHKF      024                             /* write check */
#define  FNC_WCHKR      027                             /* write check rev */
#define  FNC_WRITE      030                             /* write */
#define  FNC_READF      034                             /* read forward */
#define  FNC_READR      037                             /* read reverse */
#define CS1_RW          077
#define CS1_DVA         04000                           /* drive avail */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* TUFS - formatter status - offset 1
   + indicates kept in drive status
   ^ indicates calculated on the fly
*/

#define FS_OF           1
#define FS_SAT          0000001                         /* slave attention */
#define FS_BOT          0000002                         /* ^beginning of tape */
#define FS_TMK          0000004                         /* end of file */
#define FS_ID           0000010                         /* ID burst detected */
#define FS_SLOW         0000020                         /* slowing down NI */
#define FS_PE           0000040                         /* ^PE status */
#define FS_SSC          0000100                         /* slave stat change */
#define FS_RDY          0000200                         /* ^formatter ready */
#define FS_FPR          0000400                         /* formatter present */
#define FS_EOT          0002000                         /* +end of tape */
#define FS_WRL          0004000                         /* ^write locked */
#define FS_MOL          0010000                         /* ^medium online */
#define FS_PIP          0020000                         /* +pos in progress */
#define FS_ERR          0040000                         /* ^error */
#define FS_ATA          0100000                         /* attention active */
#define FS_REW          0200000                         /* +rewinding */

#define FS_DYN          (FS_ERR | FS_PIP | FS_MOL | FS_WRL | FS_EOT | \
                         FS_RDY | FS_PE | FS_BOT)

/* TUER - error register - offset 2 */

#define ER_OF           2
#define ER_ILF          0000001                         /* illegal func */
#define ER_ILR          0000002                         /* illegal register */
#define ER_RMR          0000004                         /* reg mod refused */
#define ER_MCP          0000010                         /* Mbus cpar err NI */
#define ER_FER          0000020                         /* format sel err */
#define ER_MDP          0000040                         /* Mbus dpar err NI */
#define ER_VPE          0000100                         /* vert parity err */
#define ER_CRC          0000200                         /* CRC err NI */
#define ER_NSG          0000400                         /* non std gap err NI */
#define ER_FCE          0001000                         /* frame count err */
#define ER_ITM          0002000                         /* inv tape mark NI */
#define ER_NXF          0004000                         /* wlock or fnc err */
#define ER_DTE          0010000                         /* time err NI */
#define ER_OPI          0020000                         /* op incomplete */
#define ER_UNS          0040000                         /* drive unsafe */
#define ER_DCK          0100000                         /* data check NI */

/* TUMR - maintenance register - offset 03 */

#define MR_OF           3
#define MR_RW           0177637                         /* read/write */

/* TUAS - attention summary - offset 4 */

#define AS_OF           4
#define AS_U0           0000001                         /* unit 0 flag */

/* TUFC - offset 5 */

#define FC_OF           5

/* TUDT - drive type - offset 6 */

#define DT_OF           6
#define DT_NSA          0100000                         /* not sect addr */
#define DT_TAPE         0040000                         /* tape */
#define DT_PRES         0002000                         /* slave present */
#define DT_TM03         0000040                         /* TM03 formatter */
#define DT_OFF          0000010                         /* drive off */
#define DT_TU16         0000011                         /* TE16 */
#define DT_TU45         0000012                         /* TU45 */
#define DT_TU77         0000014                         /* TU77 */

/* TUCC - check character, read only - offset 7 */

#define CC_OF           7
#define CC_MBZ          0177000                         /* must be zero */

/* TUSN - serial number - offset 8 */

#define SN_OF           8

/* TUTC - tape control register - offset 9 */

#define TC_OF           9
#define TC_V_UNIT       0                               /* unit select */
#define TC_M_UNIT       07
#define TC_V_EVN        0000010                         /* even parity */
#define TC_V_FMT        4                               /* format select */
#define TC_M_FMT        017
#define  TC_STD          014                            /* standard */
#define  TC_CDUMP        015                            /* core dump */
#define TC_V_DEN        8                               /* density select */
#define TC_M_DEN        07
#define  TC_800          3                              /* 800 bpi */
#define  TC_1600         4                              /* 1600 bpi */
#define TC_AER          0010000                         /* abort on error */
#define TC_SAC          0020000                         /* slave addr change */
#define TC_FCS          0040000                         /* frame count status */
#define TC_ACC          0100000                         /* accelerating NI */
#define TC_RW           0013777
#define TC_MBZ          0004000
#define TC_RIP          ((TC_800 << TC_V_DEN) | (TC_STD << TC_V_FMT))
#define GET_DEN(x)      (((x) >> TC_V_DEN) & TC_M_DEN)
#define GET_FMT(x)      (((x) >> TC_V_FMT) & TC_M_FMT)
#define GET_DRV(x)      (((x) >> TC_V_UNIT) & TC_M_UNIT)

int32 tucs1 = 0;                                        /* control/status 1 */
int32 tufc = 0;                                         /* frame count */
int32 tufs = 0;                                         /* formatter status */
int32 tuer = 0;                                         /* error status */
int32 tucc = 0;                                         /* check character */
int32 tumr = 0;                                         /* maint register */
int32 tutc = 0;                                         /* tape control */
int32 tu_time = 10;                                     /* record latency */
int32 tu_stopioe = 1;                                   /* stop on error */
static uint8 *xbuf = NULL;                              /* xfer buffer */
static uint16 *wbuf = NULL;
static int32 fmt_test[16] = {                           /* fmt valid */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0
    };
static int32 dt_map[3] = { DT_TU16, DT_TU45, DT_TU77 };
static const char *tu_fname[CS1_N_FNC] = {
    "NOP", "UNLD", "2", "REW", "FCLR", "5", "6", "7",
    "RIP", "11", "ERASE", "WREOF", "SPCF", "SPCR", "16", "17",
    "20", "21", "22", "23", "WRCHKF", "25", "26", "WRCHKR",
    "WRITE", "31", "32", "33", "READF", "35", "36" "READR"
    };

t_stat tu_mbrd (int32 *data, int32 PA, int32 fmtr);
t_stat tu_mbwr (int32 data, int32 PA, int32 fmtr);
t_stat tu_svc (UNIT *uptr);
t_stat tu_reset (DEVICE *dptr);
t_stat tu_attach (UNIT *uptr, CONST char *cptr);
t_stat tu_detach (UNIT *uptr);
t_stat tu_boot (int32 unitno, DEVICE *dptr);
t_stat tu_set_fmtr (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tu_show_fmtr (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *tu_description (DEVICE *dptr);
t_stat tu_go (int32 drv);
int32 tu_abort (void);
void tu_set_er (int32 flg);
void tu_clr_as (int32 mask);
void tu_update_fs (int32 flg, int32 drv);
t_stat tu_map_err (int32 drv, t_stat st, t_bool qdt);

/* TU data structures

   tu_dev       TU device descriptor
   tu_unit      TU unit list
   tu_reg       TU register list
   tu_mod       TU modifier list
*/

#define IOLN_TU         040
DIB tu_dib = { MBA_AUTO, IOLN_TU, &tu_mbrd, &tu_mbwr,0, 0, 0, { &tu_abort } };

UNIT tu_unit[] = {
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&tu_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) }
    };

REG tu_reg[] = {
    { GRDATAD (CS1,            tucs1, DEV_RDX,  6, 0, "current operation") },
    { GRDATAD (FC,              tufc, DEV_RDX, 16, 0, "frame count") },
    { GRDATAD (FS,              tufs, DEV_RDX, 16, 0, "formatter status") },
    { GRDATAD (ER,              tuer, DEV_RDX, 16, 0, "formatter errors") },
    { GRDATAD (CC,              tucc, DEV_RDX, 16, 0, "check character") },
    { GRDATAD (MR,              tumr, DEV_RDX, 16, 0, "maintenance register") },
    { GRDATAD (TC,              tutc, DEV_RDX, 16, 0, "tape control register") },
    { FLDATAD (STOP_IOE,  tu_stopioe,  0,             "stop on I/O error flag") },
    { DRDATAD (TIME,         tu_time, 24,             "operation execution time"), PV_LEFT },
    { URDATAD (UST, tu_unit[0].USTAT, DEV_RDX, 17, 0, TU_NUMDR, 0, "unit status") },
    { URDATAD (POS,   tu_unit[0].pos, 10, T_ADDR_W, 0,
              TU_NUMDR, PV_LEFT | REG_RO, "position") },
    { NULL }
    };

MTAB tu_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "MASSBUS", NULL, 
        NULL, &mba_show_num, NULL, "Display Massbus number" },
#if defined (VM_PDP11)
    { MTAB_XTD|MTAB_VDV, 0, "FORMATTER", "TM02",
      &tu_set_fmtr, NULL         , NULL, "Set formatter/controller type to TM02" },
    { MTAB_XTD|MTAB_VDV, 1, NULL,        "TM03",
      &tu_set_fmtr, NULL,          NULL, "Set formatter/controller type to TM03" },
#endif
    { MTAB_XTD|MTAB_VDV, 0, "FORMATTER", NULL,
      NULL, &tu_show_fmtr, NULL, "Display formatter/controller type" },
    { MTUF_WLK,         0, "write enabled",  "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable tape drive" },
    { MTUF_WLK,  MTUF_WLK, "write locked",   "LOCKED", 
        NULL, NULL, NULL, "Write lock tape drive"  },
    { UNIT_TYPE, UNIT_TE16, "TE16", "TE16", 
        NULL, NULL, NULL, "Set drive type to TE16" },
    { UNIT_TYPE, UNIT_TU45, "TU45", "TU45", 
        NULL, NULL, NULL, "Set drive type to TU45" },
    { UNIT_TYPE, UNIT_TU77, "TU77", "TU77", 
        NULL, NULL, NULL, "Set drive type to TU77" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0,       "FORMAT", "FORMAT",
        &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, "Set/Display tape format (SIMH, E11, TPC, P7B)" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0,       "CAPACITY", "CAPACITY",
        &sim_tape_set_capac, &sim_tape_show_capac, NULL, "Set unit n capacity to arg MB (0 = unlimited)" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0,        "CAPACITY", NULL,
        NULL,                &sim_tape_show_capac, NULL, "Set/Display capacity" },
    { 0 }
    };

DEVICE tu_dev = {
    "TU", tu_unit, tu_reg, tu_mod,
    TU_NUMDR, 10, T_ADDR_W, 1, DEV_RDX, 8,
    NULL, NULL, &tu_reset,
    &tu_boot, &tu_attach, &tu_detach,
    &tu_dib, DEV_MBUS|DEV_UBUS|DEV_QBUS|DEV_DEBUG|DEV_DISABLE|DEV_DIS_INIT|DEV_TM03|DEV_TAPE,
    0, NULL, NULL, NULL, &tu_help, NULL, NULL,
    &tu_description
    };

/* Massbus register read */

t_stat tu_mbrd (int32 *data, int32 ofs, int32 fmtr)
{
int32 drv;

if (fmtr != 0) {                                        /* only one fmtr */
    *data = 0;
    return MBE_NXD;
    }
drv = GET_DRV (tutc);                                   /* get current unit */
tu_update_fs (0, drv);                                  /* update status */

switch (ofs) {                                          /* decode offset */

    case CS1_OF:                                        /* MTCS1 */
        *data = (tucs1 & CS1_RW) | CS1_DVA;             /* DVA always set */
        break;

    case FC_OF:                                         /* MTFC */
        *data = tufc;
        break;

    case FS_OF:                                         /* MTFS */
        *data = tufs & 0177777;                         /* mask off rewind */
        break;

    case ER_OF:                                         /* MTER */
        *data = tuer;
        break;

    case AS_OF:                                         /* MTAS */
        *data = (tufs & FS_ATA)? AS_U0: 0;
        break;

    case CC_OF:                                         /* MTCC */
        *data = tucc = tucc & ~CC_MBZ;
        break;

    case MR_OF:                                         /* MTMR */
        *data = tumr;
        break;

    case DT_OF:                                         /* MTDT */
        *data = DT_NSA | DT_TAPE |                      /* fmtr flags */
            ((tu_dev.flags & DEV_TM03)? DT_TM03: 0);
        if (tu_unit[drv].flags & UNIT_DIS)
            *data |= DT_OFF;
        else *data |= DT_PRES | dt_map[GET_TYPE (tu_unit[drv].flags)];
        break;

    case SN_OF:                                         /* MTSN */
        *data = (tu_unit[drv].flags & UNIT_DIS)? 0: 040 | (drv + 1);
        break;

    case TC_OF:                                         /* MTTC */
        *data = tutc = tutc & ~TC_MBZ;
        break;

    default:                                            /* all others */
        return MBE_NXR;
        }

return SCPE_OK;
}

/* Massbus register write */

t_stat tu_mbwr (int32 data, int32 ofs, int32 fmtr)
{
int32 drv;

if (fmtr != 0)                                          /* only one fmtr */
    return MBE_NXD;
drv = GET_DRV (tutc);                                   /* get current unit */

switch (ofs) {                                          /* decode PA<4:1> */

    case CS1_OF:                                        /* MTCS1 */
        if (tucs1 & CS1_GO)
            tu_set_er (ER_RMR);
        else {
            tucs1 = data & CS1_RW;
            if (tucs1 & CS1_GO)
                return tu_go (drv);
            }
        break;  

    case FC_OF:                                         /* MTFC */
        if (tucs1 & CS1_GO)
            tu_set_er (ER_RMR);
        else {
            tufc = data;
            tutc = tutc | TC_FCS;                       /* set fc flag */
            }
        break;

    case AS_OF:                                         /* MTAS */
        tu_clr_as (data);
        break;

    case MR_OF:                                         /* MTMR */
        tumr = (tumr & ~MR_RW) | (data & MR_RW);
        break;

    case TC_OF:                                         /* MTTC */
        if (tucs1 & CS1_GO)
            tu_set_er (ER_RMR);
        else {
            tutc = (tutc & ~TC_RW) | (data & TC_RW) | TC_SAC;
            drv = GET_DRV (tutc);
            }
        break;

    case FS_OF:                                         /* MTFS */
    case ER_OF:                                         /* MTER */
    case CC_OF:                                         /* MTCC */
    case DT_OF:                                         /* MTDT */
    case SN_OF:                                         /* MTSN */
        if (tucs1 & CS1_GO)
            tu_set_er (ER_RMR);
        break;                                          /* read only */

    default:                                            /* all others */
        return MBE_NXR;
        }                                               /* end switch */

tu_update_fs (0, drv);
return SCPE_OK;
}

/* New magtape command */

t_stat tu_go (int32 drv)
{
int32 fnc, den;
UNIT *uptr;

fnc = GET_FNC (tucs1);                                  /* get function */
den = GET_DEN (tutc);                                   /* get density */
uptr = tu_dev.units + drv;                              /* get unit */
if (DEBUG_PRS (tu_dev)) {
    fprintf (sim_deb, ">>TU%d STRT: fnc=%s, fc=%06o, fs=%06o, er=%06o, pos=",
             drv, tu_fname[fnc], tufc, tufs, tuer);
    fprint_val (sim_deb, uptr->pos, 10, T_ADDR_W, PV_LEFT);
    fprintf (sim_deb, "\n");
    }
if ((fnc != FNC_FCLR) &&                                /* not clear & err */
    ((tufs & FS_ERR) || sim_is_active (uptr))) {        /* or in motion? */
    tu_set_er (ER_ILF);                                 /* set err */
    tucs1 = tucs1 & ~CS1_GO;                            /* clear go */
    tu_update_fs (FS_ATA, drv);                         /* set attn */
    return MBE_GOE;
    }
tu_clr_as (AS_U0);                                      /* clear ATA */
tutc = tutc & ~TC_SAC;                                  /* clear addr change */

switch (fnc) {                                          /* case on function */

    case FNC_FCLR:                                      /* drive clear */
        tuer = 0;                                       /* clear errors */
        tutc = tutc & ~TC_FCS;                          /* clear fc status */
        tufs = tufs & ~(FS_SAT | FS_SSC | FS_ID | FS_ERR);
        sim_cancel (uptr);                              /* reset drive */
        uptr->USTAT = 0;
        /* fall through */
    case FNC_NOP:
        tucs1 = tucs1 & ~CS1_GO;                        /* no operation */
        return SCPE_OK;

    case FNC_RIP:                                       /* read-in preset */
        tutc = TC_RIP;                                  /* set tutc */
        sim_tape_rewind (&tu_unit[0]);                  /* rewind unit 0 */
        tu_unit[0].USTAT = 0;
        tucs1 = tucs1 & ~CS1_GO;
        tufs = tufs & ~FS_TMK;
        return SCPE_OK;

    case FNC_UNLOAD:                                    /* unload */
        if ((uptr->flags & UNIT_ATT) == 0) {            /* unattached? */
            tu_set_er (ER_UNS);
            break;
            }
        detach_unit (uptr);
        uptr->USTAT = FS_REW;
        sim_activate (uptr, tu_time);
        tucs1 = tucs1 & ~CS1_GO;
        tufs = tufs & ~FS_TMK;
        return SCPE_OK; 

    case FNC_REWIND:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* unattached? */
            tu_set_er (ER_UNS);
            break;
            }
        uptr->USTAT = FS_PIP | FS_REW;
        sim_activate (uptr, tu_time);
        tucs1 = tucs1 & ~CS1_GO;
        tufs = tufs & ~FS_TMK;
        return SCPE_OK;

    case FNC_SPACEF:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* unattached? */
            tu_set_er (ER_UNS);
            break;
            }
        if (sim_tape_eot (uptr) || ((tutc & TC_FCS) == 0)) {
            tu_set_er (ER_NXF);
            break;
            }
        uptr->USTAT = FS_PIP;
        goto GO_XFER;

    case FNC_SPACER:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* unattached? */
            tu_set_er (ER_UNS);
            break;
            }
        if (sim_tape_bot (uptr) || ((tutc & TC_FCS) == 0)) {
            tu_set_er (ER_NXF);
            break;
            }
        uptr->USTAT = FS_PIP;
        goto GO_XFER;

    case FNC_WCHKR:                                     /* wchk = read */
    case FNC_READR:                                     /* read rev */
        if (tufs & FS_BOT) {                            /* beginning of tape? */
            tu_set_er (ER_NXF);
            break;
            }
        goto DATA_XFER;

    case FNC_WRITE:                                     /* write */
        if (((tutc & TC_FCS) == 0) ||                   /* frame cnt = 0? */
            ((den == TC_800) && (tufc > 0777765))) {    /* NRZI, fc < 13? */
            tu_set_er (ER_NXF);
            break;
            }
    case FNC_WREOF:                                     /* write tape mark */
    case FNC_ERASE:                                     /* erase */
        if (sim_tape_wrp (uptr)) {                      /* write locked? */
            tu_set_er (ER_NXF);
            break;
            }
    case FNC_WCHKF:                                     /* wchk = read */
    case FNC_READF:                                     /* read */
    DATA_XFER:
        if ((uptr->flags & UNIT_ATT) == 0) {            /* unattached? */
            tu_set_er (ER_UNS);
            break;
            }
        if (fmt_test[GET_FMT (tutc)] == 0) {            /* invalid format? */
            tu_set_er (ER_FER);
            break;
            }
        if (uptr->UDENS == UD_UNK)                      /* set dens */
            uptr->UDENS = den;
        uptr->USTAT = 0;
    GO_XFER:
        tufs = tufs & ~(FS_TMK | FS_ID);                /* clear eof, id */
        sim_activate (uptr, tu_time);
        return SCPE_OK;

    default:                                            /* all others */
        tu_set_er (ER_ILF);                             /* not supported */
        break;
        }                                               /* end case function */

tucs1 = tucs1 & ~CS1_GO;                                /* clear go */
tu_update_fs (FS_ATA, drv);                             /* set attn */
return MBE_GOE;
}

/* Abort transfer */

int32 tu_abort (void)
{
return tu_reset (&tu_dev);
}

/* Unit service

   Complete movement or data transfer command
   Unit must exist - can't remove an active unit
   Unit must be attached - detach cancels in progress operations
*/

t_stat tu_svc (UNIT *uptr)
{
int32 fnc, fmt, j, xbc;
int32 fc, drv;
t_mtrlnt i, tbc;
t_stat st, r = SCPE_OK;

drv = (int32) (uptr - tu_dev.units);                    /* get drive # */
if (uptr->USTAT & FS_REW) {                             /* rewind or unload? */
    sim_tape_rewind (uptr);                             /* rewind tape */
    uptr->USTAT = 0;                                    /* clear status */
    tu_update_fs (FS_ATA | FS_SSC, drv);
    return SCPE_OK;
    }

fnc = GET_FNC (tucs1);                                  /* get command */
fmt = GET_FMT (tutc);                                   /* get format */
fc = 0200000 - tufc;                                    /* get frame count */
uptr->USTAT = 0;                                        /* clear status */

if ((uptr->flags & UNIT_ATT) == 0) {
    tu_set_er (ER_UNS);                                 /* set formatter error */
    if (fnc >= FNC_XFER)
        mba_set_don (tu_dib.ba);
    tu_update_fs (FS_ATA, drv);
    return (tu_stopioe? SCPE_UNATT: SCPE_OK);
    }
switch (fnc) {                                          /* case on function */

/* Non-data transfer commands - set ATA when done */

    case FNC_SPACEF:                                    /* space forward */
        do {
            tufc = (tufc + 1) & 0177777;                /* incr fc */
            if ((st = sim_tape_sprecf (uptr, &tbc))) {  /* space rec fwd, err? */
                r = tu_map_err (drv, st, 0);            /* map error */
                break;
                }
            } while ((tufc != 0) && !sim_tape_eot (uptr));
        if (tufc != 0)
            tu_set_er (ER_FCE);
        else tutc = tutc & ~TC_FCS;
        break;

    case FNC_SPACER:                                    /* space reverse */
        do {
            tufc = (tufc + 1) & 0177777;                /* incr wc */
            if ((st = sim_tape_sprecr (uptr, &tbc))) {  /* space rec rev, err? */
                r = tu_map_err (drv, st, 0);            /* map error */
                break;
                }
            } while (tufc != 0);
        if (tufc != 0)
            tu_set_er (ER_FCE);
        else tutc = tutc & ~TC_FCS;
        break;

    case FNC_WREOF:                                     /* write end of file */
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = tu_map_err (drv, st, 0);                /* map error */
        break;

    case FNC_ERASE:
        if (sim_tape_wrp (uptr))                        /* write protected? */
            r = tu_map_err (drv, MTSE_WRP, 0);          /* map error */
        break;

/* Unit service - data transfer commands */

    case FNC_READF:                                     /* read */
    case FNC_WCHKF:                                     /* wcheck = read */
        tufc = 0;                                       /* clear frame count */
        if ((uptr->UDENS == TC_1600) && sim_tape_bot (uptr))
            tufs = tufs | FS_ID;                        /* PE BOT? ID burst */
        if ((st = sim_tape_rdrecf (uptr, xbuf, &tbc, MT_MAXFR))) {/* read fwd */
            r = tu_map_err (drv, st, 1);                /* map error */
            break;                                      /* done */
            }
        for (i = tbc; i < tbc + 4; i++)                 /* pad with 0's */
            xbuf[i] = 0;
        if (fmt == TC_CDUMP) {                          /* core dump? */
            for (i = j = 0; i < tbc; i = i + 4) {
                wbuf[j++] = ((uint16) xbuf[i] & 0xF) |
                    (((uint16) (xbuf[i + 1] & 0xF)) << 4) |
                    (((uint16) (xbuf[i + 2] & 0xF)) << 8) |
                    (((uint16) (xbuf[i + 3] & 0xf)) << 12);
                }
            xbc = (tbc + 1) >> 1;
            }
        else {                                          /* standard */
            for (i = j = 0; i < tbc; i = i + 2) {
                wbuf[j++] = ((uint16) xbuf[i]) |
                    (((uint16) xbuf[i + 1]) << 8);
                }
            xbc = tbc;
            }
        if (mba_get_bc (tu_dib.ba) > xbc)               /* record short? */
            tu_set_er (ER_FCE);                         /* set FCE, ATN */
        if (fnc == FNC_WCHKF)
            mba_chbufW (tu_dib.ba, xbc, wbuf);
        else mba_wrbufW (tu_dib.ba, xbc, wbuf);
        tufc = tbc & 0177777;
        break;

    case FNC_WRITE:                                     /* write */
        xbc = mba_rdbufW (tu_dib.ba, fc, wbuf);         /* read buffer */
        if (xbc == 0)                                   /* anything?? */
            break;
        if (fmt == TC_CDUMP) {                          /* core dump? */
            for (i = j = 0; j < xbc; j = j + 1) {
                xbuf[i++] = wbuf[j] & 0xF;
                xbuf[i++] = (wbuf[j] >> 4) & 0xF;
                xbuf[i++] = (wbuf[j] >> 8) & 0xF;
                xbuf[i++] = (wbuf[j] >> 12) & 0xF;
                }
            tbc = (xbc + 1) >> 1;
            }
        else {                                          /* standard */
            for (i = j = 0; j < xbc; j = j + 1) {
                xbuf[i++] = wbuf[j] & 0377;
                xbuf[i++] = (wbuf[j] >> 8) & 0377;
                }
            tbc = xbc;
            }
        if ((st = sim_tape_wrrecf (uptr, xbuf, tbc)))   /* write rec, err? */
            r = tu_map_err (drv, st, 1);                /* map error */
        else {
            tufc = (tufc + tbc) & 0177777;
            if (tufc == 0)
                tutc = tutc & ~TC_FCS;
            }
        break;

    case FNC_READR:                                     /* read reverse */
    case FNC_WCHKR:                                     /* wcheck = read */
        tufc = 0;                                       /* clear frame count */
        if ((st = sim_tape_rdrecr (uptr, xbuf + 4, &tbc, MT_MAXFR))) {/* read rev */
            r = tu_map_err (drv, st, 1);                /* map error */
            break;                                      /* done */
            }
        for (i = 0; i < 4; i++) xbuf[i] = 0;            /* pad with 0's */
        if (fmt == TC_CDUMP) {                          /* core dump? */
            for (i = tbc + 3, j = 0; i > 3; i = i - 4) {
                wbuf[j++] = ((uint16) xbuf[i] & 0xF) |
                    (((uint16) (xbuf[i - 1] & 0xF)) << 4) |
                    (((uint16) (xbuf[i - 2] & 0xF)) << 8) |
                    (((uint16) (xbuf[i - 3] & 0xf)) << 12);
                }
            xbc = (tbc + 1) >> 1;
            }
        else {                                          /* standard */
            for (i = tbc + 3, j = 0; i > 3; i = i - 2) {
                wbuf[j++] = ((uint16) xbuf[i]) |
                    (((uint16) xbuf[i - 1]) << 8);
                }
            xbc = tbc;
            }
        if (mba_get_bc (tu_dib.ba) > xbc)               /* record short? */
            tu_set_er (ER_FCE);                         /* set FCE, ATN */
        if (fnc == FNC_WCHKR)
            mba_chbufW (tu_dib.ba, xbc, wbuf);
        else mba_wrbufW (tu_dib.ba, xbc, wbuf);
        tufc = tbc & 0177777;
        break;
        }                                               /* end case */

tucs1 = tucs1 & ~CS1_GO;                                /* clear go */
if (fnc >= FNC_XFER) {                                  /* data xfer? */
    mba_set_don (tu_dib.ba);                            /* set done */
    tu_update_fs (0, drv);                              /* update fs */
    }
else tu_update_fs (FS_ATA, drv);                        /* no, set attn */
if (DEBUG_PRS (tu_dev)) {
    fprintf (sim_deb, ">>TU%d DONE: fnc=%s, fc=%06o, fs=%06o, er=%06o, pos=",
             drv, tu_fname[fnc], tufc, tufs, tuer);
    fprint_val (sim_deb, uptr->pos, 10, T_ADDR_W, PV_LEFT);
    fprintf (sim_deb, ", r=%d\n", r);
    }
return SCPE_OK;
}

/* Set formatter error */

void tu_set_er (int32 flg)
{
tuer = tuer | flg;
tufs = tufs | FS_ATA;
mba_upd_ata (tu_dib.ba, 1);
return;
}

/* Clear attention */

void tu_clr_as (int32 mask)
{
if (mask & AS_U0)
    tufs = tufs & ~FS_ATA;
mba_upd_ata (tu_dib.ba, tufs & FS_ATA);
return;
}

/* Formatter update status */

void tu_update_fs (int32 flg, int32 drv)
{
int32 act = sim_activate_time (&tu_unit[drv]);

tufs = (tufs & ~FS_DYN) | FS_FPR | flg;
if (tu_unit[drv].flags & UNIT_ATT) {
    tufs = tufs | FS_MOL | tu_unit[drv].USTAT;
    if (tu_unit[drv].UDENS == TC_1600)
        tufs = tufs | FS_PE;
    if (sim_tape_wrp (&tu_unit[drv]))
        tufs = tufs | FS_WRL;
    if (!act) {
        if (sim_tape_bot (&tu_unit[drv]))
            tufs = tufs | FS_BOT;
        if (sim_tape_eot (&tu_unit[drv]))
            tufs = tufs | FS_EOT;
        }
    }
if (tuer)
    tufs = tufs | FS_ERR;
if (tufs && !act)
    tufs = tufs | FS_RDY;
if (flg & FS_ATA)
    mba_upd_ata (tu_dib.ba, 1);
return;
}

/* Map tape error status

   Note that tape mark on a data transfer sets FCE and Massbus EXC */

t_stat tu_map_err (int32 drv, t_stat st, t_bool qdt)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
        tu_set_er (ER_NXF);                             /* can't execute */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        break;

    case MTSE_TMK:                                      /* end of file */
        tufs = tufs | FS_TMK;                           /* set TMK status */
        if (qdt) {                                      /* data transfer? */
            tu_set_er (ER_FCE);                         /* set FCE */
            mba_set_exc (tu_dib.ba);                    /* set exception*/
            }
        break;

    case MTSE_IOERR:                                    /* IO error */
        tu_set_er (ER_VPE);                             /* flag error */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        return (tu_stopioe? SCPE_IOERR: SCPE_OK);

    case MTSE_INVRL:                                    /* invalid rec lnt */
        tu_set_er (ER_VPE);                             /* flag error */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        tu_set_er (ER_CRC);                             /* set crc err */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        break;

    case MTSE_EOM:                                      /* end of medium */
        tu_set_er (ER_OPI);                             /* incomplete */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        return SCPE_OK;

    case MTSE_WRP:                                      /* write protect */
        tu_set_er (ER_NXF);                             /* can't execute */
        if (qdt)                                        /* set exception */
            mba_set_exc (tu_dib.ba);
        break;

    default:                                            /* unknown error */
        return SCPE_IERR;
        }

return SCPE_OK;
}

/* Reset routine */

t_stat tu_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

mba_set_enbdis (dptr);
tucs1 = 0;
tufc = 0;
tuer = 0;
tufs = FS_FPR | FS_RDY;
if (sim_switches & SWMASK ('P'))                        /* powerup? clr TC */
    tutc = 0;
else tutc = tutc & ~TC_FCS;                             /* no, clr <fcs> */
for (u = 0; u < TU_NUMDR; u++) {                        /* loop thru units */
    uptr = tu_dev.units + u;
    sim_tape_reset (uptr);                              /* clear pos flag */
    sim_cancel (uptr);                                  /* cancel activity */
    uptr->USTAT = 0;
    }
if (xbuf == NULL)
    xbuf = (uint8 *) calloc (MT_MAXFR + 4, sizeof (uint8));
if (xbuf == NULL)
    return SCPE_MEM;
if (wbuf == NULL)
    wbuf = (uint16 *) calloc ((MT_MAXFR + 4) >> 1, sizeof (uint16));
if (wbuf == NULL)
    return SCPE_MEM;
return auto_config(0, 0);
}

/* Attach routine */

t_stat tu_attach (UNIT *uptr, CONST char *cptr)
{
int32 drv = uptr - tu_dev.units, flg;
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->USTAT = 0;                                        /* clear unit status */
uptr->UDENS = UD_UNK;                                   /* unknown density */
flg = FS_ATA | FS_SSC;                                  /* set attention */
if (GET_DRV (tutc) == drv)                              /* sel drv? set SAT */
    flg = flg | FS_SAT;
tu_update_fs (flg, drv);                                /* update status */
return r;
}

/* Detach routine */

t_stat tu_detach (UNIT* uptr)
{
int32 drv = uptr - tu_dev.units;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
uptr->USTAT = 0;                                        /* clear status flags */
tu_update_fs (FS_ATA | FS_SSC, drv);                    /* update status */
return sim_tape_detach (uptr);
}

/* Set/show formatter type */

t_stat tu_set_fmtr (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
DEVICE *dptr = find_dev_from_unit (uptr);

if (cptr != NULL)
    return SCPE_ARG;
if (dptr == NULL)
    return SCPE_IERR;
if (val)
    dptr->flags = dptr->flags | DEV_TM03;
else dptr->flags = dptr->flags & ~DEV_TM03;
return SCPE_OK;
}

t_stat tu_show_fmtr (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
DEVICE *dptr = find_dev_from_unit (uptr);

if (dptr == NULL)
    return SCPE_IERR;
fprintf (st, "TM0%d", (dptr->flags & DEV_TM03? 3: 2));
return SCPE_OK;
}

/* Device bootstrap */

#if defined (PDP11)

#elif defined (VM_PDP11)

#define BOOT_START      016000                          /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint16))

static const uint16 boot_rom[] = {
    0046515,                        /* "MM" */
    0012706, BOOT_START,            /* mov #boot_start, sp */
    0012700, 0000000,               /* mov #unit, r0 */
    0012701, 0172440,               /* mov #TUCS1, r1 */
    0012761, 0000040, 0000010,      /* mov #CS2_CLR, 10(r1) ; reset */
    0012711, 0000021,               /* mov #RIP+GO, (r1)    ; rip */
    0010004,                        /* mov r0, r4 */
    0052704, 0002300,               /* bis #2300, r4        ; set den */
    0010461, 0000032,               /* mov r4, 32(r1)       ; set unit */
    0012761, 0177777, 0000006,      /* mov #-1, 6(r1)       ; set fc */
    0012711, 0000031,               /* mov #SPCF+GO, (r1)   ; skip rec */
    0105761, 0000012,               /* tstb 12 (r1)         ; fmtr rdy? */
    0100375,                        /* bpl .-4 */
    0012761, 0177000, 0000002,      /* mov #-1000, 2(r1)    ; set wc */
    0005061, 0000004,               /* clr 4(r1)            ; clr ba */
    0005061, 0000006,               /* clr 6(r1)            ; clr fc */
    0012711, 0000071,               /* mov #READ+GO, (r1)   ; read  */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0005002,                        /* clr R2 */
    0005003,                        /* clr R3 */
    0012704, BOOT_START+020,        /* mov #start+020, r4 */
    0005005,                        /* clr R5 */
    0105011,                        /* clrb (r1) */
    0005007                         /* clr PC */
    };

t_stat tu_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & (TU_NUMDR - 1);
M[BOOT_CSR >> 1] = mba_get_csr (tu_dib.ba) & DMASK;
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

t_stat tu_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif

t_stat tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "TM02/TM03/TE16/TU45/TU77 Magnetic Tapes\n\n");
fprintf (st, "The TU controller implements the Massbus family of 800/1600bpi magnetic tape\n");
fprintf (st, "drives.  TU options include the ability to set the drive type to one of three\n");
fprintf (st, "drives (TE16, TU45, or TU77), and to set the drives write enabled or write\n");
fprintf (st, "locked.  When configured on a PDP11 simulator, the TU formatter type can be\n");
fprintf (st, "selected as either TM02 or TM03),\n\n");
fprint_set_help (st, dptr);
fprintf (st, "\nMagnetic tape units can be set to a specific reel capacity in MB, or to\n");
fprintf (st, "unlimited capacity:\n\n");
#if defined (VM_PDP11)
fprintf (st, "The TU controller supports the BOOT command.\n");
#endif
fprintf (st, "\nThe TU controller implements the following registers:\n\n");
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error           processed as\n");
fprintf (st, "    not attached    tape not ready; if STOP_IOE, stop\n");
fprintf (st, "    end of file     bad tape\n");
fprintf (st, "    OS I/O error    parity error; if STOP_IOE, stop\n");
return SCPE_OK;
}

const char *tu_description (DEVICE *dptr)
{
return "TM03 tape formatter";
}
