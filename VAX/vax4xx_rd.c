/* vax4xx_rd.c: HDC9224 hard disk simulator

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   rd            HDC9224 Hard Disk Controller
*/

#include "vax_defs.h"
#include "sim_disk.h"

#if defined(VAX_420)
#include "vax_ka420_rdrz_bin.h"
#else
#define BOOT_CODE_ARRAY NULL
#define BOOT_CODE_SIZE  0
#endif

#define RD_NUMDR        3
#define RD_RMV(u)       ((drv_tab[GET_DTYPE (u->flags)].flgs & RDDF_RMV)? \
                          UF_RMV: 0)
#define RDDF_RMV        01                              /* removable */
#define RD_NUMBY        512                             /* bytes/sector */

#define RD_MAXFR        (1 << 14)                       /* max transfer */

/* HDC commands */

#define CMD_RESET       1
#define CMD_SETREG      2
#define CMD_DESELECT    3
#define CMD_DRVSEL      4
#define CMD_RESTORE     5
#define CMD_STEP        6
#define CMD_POLL        7
#define CMD_RDID        8
#define CMD_FORMAT      9
#define CMD_RDTRK       10
#define CMD_RDPHY       11
#define CMD_RDLOG       12
#define CMD_WRPHY       13
#define CMD_WRLOG       14
#define CMD_UNKNOWN     15

/* drive status */

#define DST_WRF         0x01                            /* write fault */
#define DST_RDY         0x02                            /* ready */
#define DST_WPT         0x04                            /* write protect */
#define DST_DS3         0x08                            /* drive status 3 */
#define DST_TRK0        0x10                            /* track 0 */
#define DST_SCOM        0x20                            /* seek complete */
#define DST_IDX         0x40                            /* index */
#define DST_SELA        0x80                            /* sel ack */

/* chip status */

#define CST_SDRV        0x03                            /* selected drive */
#define CST_CMPE        0x04                            /* compare error */
#define CST_SYNCE       0x08                            /* sync error */
#define CST_DELD        0x10                            /* deleted data mark */
#define CST_ECCE        0x20                            /* ECC error */
#define CST_ECCC        0x40                            /* ECC correction attepted */
#define CST_RETR        0x80                            /* retry required */

/* interrupt status port */

#define STAT_V_BAD      0                               /* bad sect */
#define STAT_V_OVR      1                               /* overrun */
#define STAT_V_RDYC     2                               /* ready chng */
#define STAT_V_TRMC     3                               /* term code */
#define STAT_M_TRMC     0x3
#define STAT_V_DONE     5                               /* done */
#define STAT_V_DMARQ    6                               /* dmareq */
#define STAT_V_INT      7                               /* intpend */
#define STAT_BAD        (1u << STAT_V_BAD)
#define STAT_OVR        (1u << STAT_V_OVR)
#define STAT_RDYC       (1u << STAT_V_RDYC)
#define STAT_TRMC       (STAT_M_TRMC << STAT_V_TRMC)
#define STAT_DONE       (1u << STAT_V_DONE)
#define STAT_DMARQ      (1u << STAT_V_DMARQ)
#define STAT_INT        (1u << STAT_V_INT)

/* termination codes */

#define TRM_OK          0                               /* Success completion */
#define TRM_ERR_RD      1                               /* Error in READ ID sequence */
#define TRM_ERR_VER     2                               /* Error in VERIFY sequence */
#define TRM_ERR_TRAN    3                               /* Error in DATA TRANSFER sequence */

#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

#define DBG_REG         0x0001                          /* registers */
#define DBG_CMD         0x0002                          /* commands */
#define DBG_RD          0x0004                          /* disk reads */
#define DBG_WR          0x0008                          /* disk writes */
#define DBG_REQ         0x0010                          /* display transfer requests */
#define DBG_DSK         0x0020                          /* display sim_disk activities */
#define DBG_DAT         0x0040                          /* display transfer data */

#define CYL             u3                              /* current cylinder */
#define HEAD            u4                              /* current head */
#define STAT            u5                              /* drive status */
#define CMD             u6                              /* current command */

#define CUR_DRV         (rd_cstat & CST_SDRV)           /* currently selected drive */

#define GET_SPT(u)      (drv_tab[GET_DTYPE (u->flags)].sect)
#define GET_SURF(u)     (drv_tab[GET_DTYPE (u->flags)].surf)
#define GET_DA(u,c,h,s) ((c * (GET_SPT(u) * GET_SURF(u))) \
                         + (h * GET_SPT(u)) + s)

/* The HDC9224 supports multiple disk drive types:

   type sec     surf    cyl     tpg     gpc     RCT     LBNs

   RX33 15      2       80      2       1       -       2400
   RD31 17      4       615     4       1       3*8     41584
   RD32 17      6       820     6       1       ?       83236
   RD53 17      7       1024    7       1       5*8     138712
   RD54 17      15      1225    15      1       7*8     311256
*/

#define RX33_DTYPE      0
#define RX33_SECT       15
#define RX33_SURF       2
#define RX33_CYL        80
#define RX33_TPG        2
#define RX33_XBN        0
#define RX33_DBN        0
#define RX33_LBN        2400
#define RX33_RCTS       0
#define RX33_RCTC       0
#define RX33_RBN        0
#define RX33_CYLP       0
#define RX33_CYLR       0
#define RX33_CCS        0
#define RX33_MED        0x25658021
#define RX33_FLGS       RDDF_RMV

#define RD31_DTYPE      1
#define RD31_SECT       17
#define RD31_SURF       4
#define RD31_CYL        616                             /* last unused */
#define RD31_TPG        RD31_SURF
#define RD31_XBN        54
#define RD31_DBN        14
#define RD31_LBN        41584
#define RD31_RCTS       3
#define RD31_RCTC       8
#define RD31_RBN        100
#define RD31_CYLP       256
#define RD31_CYLR       615
#define RD31_CCS        9
#define RD31_MED        0x2564401F
#define RD31_FLGS       0

#define RD32_DTYPE      2
#define RD32_SECT       17
#define RD32_SURF       6
#define RD32_CYL        821                             /* last unused */
#define RD32_TPG        RD32_SURF
#define RD32_XBN        54
#define RD32_DBN        48
#define RD32_LBN        83236
#define RD32_RCTS       4
#define RD32_RCTC       8
#define RD32_RBN        200
#define RD32_CYLP       821
#define RD32_CYLR       821
#define RD32_CCS        14
#define RD32_MED        0x25644020
#define RD32_FLGS       0

#define RD53_DTYPE      3
#define RD53_SECT       17
#define RD53_SURF       8
#define RD53_CYL        1024                            /* last unused */
#define RD53_TPG        RD53_SURF
#define RD53_XBN        54
#define RD53_DBN        82
#define RD53_LBN        138712
#define RD53_RCTS       5
#define RD53_RCTC       8
#define RD53_RBN        280
#define RD53_CYLP       1024
#define RD53_CYLR       1024
#define RD53_CCS        13
#define RD53_MED        0x25644035
#define RD53_FLGS       0

#define RD54_DTYPE      4
#define RD54_SECT       17
#define RD54_SURF       15
#define RD54_CYL        1225                            /* last unused */
#define RD54_TPG        RD54_SURF
#define RD54_XBN        54
#define RD54_DBN        201
#define RD54_LBN        311256
#define RD54_RCTS       7
#define RD54_RCTC       8
#define RD54_RBN        609
#define RD54_CYLP       1225
#define RD54_CYLR       1225
#define RD54_CCS        14
#define RD54_MED        0x25644036
#define RD54_FLGS       0

#define UNIT_V_WLK      (DKUF_V_UF + 0)                 /* hwre write lock */
#define UNIT_V_DTYPE    (DKUF_V_UF + 1)                 /* drive type */
#define UNIT_M_DTYPE    0xF
#define UNIT_WLK        (1u << UNIT_V_WLK)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protected */
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       tpg;                                    /* trk/grp */
    int32       xbn;                                    /* XBN size */
    int32       dbn;                                    /* DBN size */
    uint32      lbn;                                    /* LBN size */
    int32       rcts;                                   /* RCT size */
    int32       rctc;                                   /* RCT copies */
    int32       rbn;                                    /* RBNs */
    int32       cylp;                                   /* first cyl for write precomp */
    int32       cylr;                                   /* first cyl for reduced write current */
    int32       ccs;                                    /* cyl/cyl skew */
    int32       med;                                    /* MSCP media */
    int32       flgs;                                   /* flags */
    const char  *name;                                  /* name */
    };

#define RD_DRV(d) \
    d##_SECT, d##_SURF, d##_CYL,  d##_TPG, \
    d##_XBN,  d##_DBN,  d##_LBN,  d##_RCTS, \
    d##_RCTC, d##_RBN,  d##_CYLP, d##_CYLR, \
    d##_CCS,  d##_MED,  d##_FLGS
#define RD_SIZE(d)      (d##_LBN * RD_NUMBY)

static struct drvtyp drv_tab[] = {
    { RD_DRV (RX33), "RX33" },{ RD_DRV (RD31), "RD31" },
    { RD_DRV (RD32), "RD32" },{ RD_DRV (RD53), "RD53" },
    { RD_DRV (RD54), "RD54" },
    { 0 }
    };

int32 rd_cwait = 20;                                    /* command wait time */
int32 rd_dwait = 20;                                    /* data trasfer wait time */

int32 rd_rg_p = 0;                                      /* register pointer */
int32 rd_stat = 0;                                      /* interrupt status port */

int32 rd_dma = 0;                                       /* DMA address */
int32 rd_dsect = 0;                                     /* desired sector */
int32 rd_dhead = 0;                                     /* desired head */
int32 rd_dcyl = 0;                                      /* desired cylinder */
int32 rd_scnt = 0;                                      /* sector count */
int32 rd_rtcnt = 0;                                     /* retry count */
int32 rd_mode = 0;                                      /* operating mode */
int32 rd_cstat = 0;                                     /* chip status */
int32 rd_term = 0;                                      /* termination conditions */
int32 rd_data = 0;

uint16 *rd_xb = NULL;                                   /* xfer buffer */

t_stat rd_svc (UNIT *uptr);
t_stat rd_reset (DEVICE *dptr);
void rd_set_dstat (UNIT *uptr);
void rd_done (int32 term_code, t_bool setint);
void rd_cmd (int32 data);
int32 rd_decode_cmd (int32 data);
t_stat rd_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rd_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat rd_attach (UNIT *uptr, CONST char *cptr);
t_stat rd_detach (UNIT *uptr);
t_stat rd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rd_description (DEVICE *dptr);

/* RD data structures

   rd_dev       RD device descriptor
   rd_unit      RD unit list
   rd_reg       RD register list
   rd_mod       RD modifier list
*/

DIB rd_dib = {
    RD_ROM_INDEX, BOOT_CODE_ARRAY, BOOT_CODE_SIZE
    };

UNIT rd_unit[] = {
    { UDATA (&rd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RD_SIZE (RD54)) },
    { UDATA (&rd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RD54_DTYPE << UNIT_V_DTYPE), RD_SIZE (RD54)) },
    { UDATA (&rd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RX33_DTYPE << UNIT_V_DTYPE), RD_SIZE (RX33)) }
    };

REG rd_reg[] = {
    { DRDATA  (RPTR,  rd_rg_p,   4), REG_HRO },
    { HRDATAD (STAT,  rd_stat,   8, "Interrupt Status") },
    { HRDATAD (DMA,   rd_dma,   24, "DMA Address") },
    { DRDATAD (DSECT, rd_dsect,  8, "Desired Sector") },
    { DRDATAD (DHEAD, rd_dhead,  4, "Desired Head") },
    { DRDATAD (DCYL,  rd_dcyl,  11, "Desired Cylinder") },
    { URDATAD (CHEAD, rd_unit[0].HEAD, 10,  4, 0, RD_NUMDR, 0, "Current Head") },
    { URDATAD (CCYL,  rd_unit[0].CYL,  10, 11, 0, RD_NUMDR, 0, "Current Cylinder") },
    { URDATAD (DSTAT, rd_unit[0].STAT, 16,  8, 0, RD_NUMDR, 0, "Drive Status") },
    { URDATAD (CMD,   rd_unit[0].CMD,  10,  8, 0, RD_NUMDR, 0, "Current Command") },
    { DRDATAD (SCNT,  rd_scnt,   8, "Sector Count") },
    { DRDATAD (RCNT,  rd_rtcnt,  8, "Retry Count") },
    { HRDATAD (MODE,  rd_mode,   8, "Operating Mode") },
    { HRDATAD (CSTAT, rd_cstat,  8, "Chip Status") },
    { HRDATAD (TCON,  rd_term,   8, "Termination Conditions") },
    { DRDATAD (CWAIT, rd_cwait, 24, "Command wait time"), PV_LEFT + REG_NZ },
    { DRDATAD (DWAIT, rd_dwait, 24, "Data wait time"), PV_LEFT + REG_NZ },
    { URDATA  (CAPAC, rd_unit[0].capac, 10, T_ADDR_W, 0, RD_NUMDR, REG_HRO | PV_LEFT) },
    { FLDATAD (INT,   int_req[IPL_SCA], INT_V_SCA, "Interrupt pending flag") },
    { NULL }
    };

DEBTAB rd_debug[] = {
    { "REG",  DBG_REG, "trace read/write registers" },
    { "CMD",  DBG_CMD, "display commands" },
    { "RD",   DBG_RD,  "display disk reads" },
    { "WR",   DBG_WR,  "display disk writes" },
    { "REQ",  DBG_REQ, "display transfer requests" },
    { "DISK", DBG_DSK, "display sim_disk activities" },
    { "DATA", DBG_DAT, "display transfer data" },
    { 0 }
    };

MTAB rd_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", 
      NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK, UNIT_WLK, "write locked",  "LOCKED", 
      NULL, NULL, NULL, "Write lock disk drive" },
    { MTAB_XTD|MTAB_VUN, RX33_DTYPE, NULL, "RX33",
      &rd_set_type, NULL, NULL, "Set RX33 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD31_DTYPE, NULL, "RD31",
      &rd_set_type, NULL, NULL, "Set RD31 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD32_DTYPE, NULL, "RD32",
      &rd_set_type, NULL, NULL, "Set RD32 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD53_DTYPE, NULL, "RD53",
      &rd_set_type, NULL, NULL, "Set RD53 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RD54_DTYPE, NULL, "RD54",
      &rd_set_type, NULL, NULL, "Set RD54 Disk Type" },
    { MTAB_XTD|MTAB_VUN, 0, "TYPE", NULL,
      NULL, &rd_show_type, NULL, "Display device type" },
    { MTAB_XTD|MTAB_VUN | MTAB_VALR, 0, "FORMAT", "FORMAT={SIMH|VHD|RAW}",
      &sim_disk_set_fmt, &sim_disk_show_fmt, NULL, "Display disk format" },
    { 0 }
    };

DEVICE rd_dev = {
    "RD", rd_unit, rd_reg, rd_mod,
    RD_NUMDR, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &rd_reset,
    NULL, &rd_attach, &rd_detach,
    &rd_dib, DEV_DEBUG | RD_FLAGS, 0,
    rd_debug, NULL, NULL, &rd_help, NULL, NULL,
    &rd_description
    };

/* RD read

   200C0000             register data access
   200C0004             interrupt status
*/

int32 rd_rd (int32 pa)
{
int32 rg = (pa >> 2) & 3;
int32 data = 0;
UNIT *uptr = &rd_unit[CUR_DRV];

if (rd_dev.flags & DEV_DIS)                             /* disabled? */
    return 0;

switch (rg) {

    case 0:                                             /* DKC_REG */
        switch (rd_rg_p) {

            case 0:                                     /* UDC_DMA7 */
                data = rd_dma & BMASK;
                break;

            case 1:                                     /* UDC_DMA15 */
                data = (rd_dma >> 8) & BMASK;
                break;

            case 2:                                     /* UDC_DMA23 */
                data = (rd_dma >> 16) & BMASK;
                break;

            case 3:                                     /* UDC_DSECT */
                data = rd_dsect & BMASK;
                break;

            case 4:                                     /* UDC_CHEAD */
                data = ((uptr->CYL & 0x700) >> 4) | \
                    (uptr->HEAD & 0xF);
                break;

            case 5:                                     /* UDC_CCYL */
                data = uptr->CYL & BMASK;
                break;

            case 6:                                     /* temporary store */
                data = 0;
                break;

            case 7:                                     /* temporary store */
                data = 0;
                break;

            case 8:                                     /* UDC_CSTAT */
                data = rd_cstat & BMASK;
                break;

            case 9:                                     /* UDC_DSTAT */
                data = uptr->STAT & BMASK;
                break;

            case 10:                                    /* UDC_DATA */
                data = rd_data & BMASK;
                break;
                }
        sim_debug (DBG_REG, &rd_dev, "reg %d read, value = %X\n", rd_rg_p, data);
        if (rd_rg_p < 10)
            rd_rg_p++;                                  /* Advance to next register */
        break;

    case 1:                                             /* DKC_STAT */
        data = rd_stat & BMASK;
        rd_stat = rd_stat & ~(STAT_INT | STAT_RDYC);
        sim_debug (DBG_REG, &rd_dev, "int status read, value = %X\n", data);
        break;
        }
return data;
}

/* RD write

   200C0000             register data access
   200C0004             controller command
*/

void rd_wr (int32 pa, int32 data, int32 access)
{
int32 rg = (pa >> 2) & 3;

if (rd_dev.flags & DEV_DIS)                             /* disabled? */
    return;

switch (rg) {

    case 0:                                             /* DKC_REG */
        switch (rd_rg_p) {

            case 0:                                     /* UDC_DMA7 */
                rd_dma = (rd_dma & ~BMASK) | (data & BMASK);
                break;

            case 1:                                     /* UDC_DMA15 */
                rd_dma = (rd_dma & ~(BMASK << 8)) | ((data & BMASK) << 8);
                break;

            case 2:                                     /* UDC_DMA23 */
                rd_dma = (rd_dma & ~(BMASK << 16)) | ((data & BMASK) << 16);
                break;

            case 3:                                     /* UDC_DSECT */
                rd_dsect = data & BMASK;
                break;

            case 4:                                     /* UDC_DHEAD */
                rd_dhead = data & 0xF;                  /* head # in data <04:00> */
                rd_dcyl = (rd_dcyl & ~(0x700)) |        /* cyl <10:08> in data <06:04> */
                    ((data & 0x70) << 4);
                break;

            case 5:                                     /* UDC_DCYL */
                rd_dcyl = (rd_dcyl & ~BMASK) |          /* cyl <07:00> */
                    (data & BMASK);
                break;

            case 6:                                     /* UDC_SCNT */
                rd_scnt = data & BMASK;
                break;

            case 7:                                     /* UDC_RTCNT */
                rd_rtcnt = data & BMASK;
                break;

            case 8:                                     /* UDC_MODE */
                rd_mode = data & BMASK;
                break;

            case 9:                                     /* UDC_TERM */
                rd_term = data & BMASK;
                break;

            case 10:                                    /* UDC_DATA */
                rd_data = data & BMASK;
                break;
                }
        sim_debug (DBG_REG, &rd_dev, "reg %d write, value = %X\n", rd_rg_p, data);
        if (rd_rg_p < 10)
            rd_rg_p++;                                  /* Advance to next register */
        break;

    case 1:                                             /* DKC_CMD */
        rd_cmd (data);
        break;
        }
SET_IRQL;
}

void rd_cmd (int32 data)
{
int32 max_cyl;
UNIT *uptr = &rd_unit[CUR_DRV];

/* put command in unit */
uptr->CMD = rd_decode_cmd (data);

switch (uptr->CMD) {
    case CMD_RESET:
        rd_rg_p = 0;
        rd_term = 0;
        rd_dsect = 0;
        rd_dhead = 0;
        rd_dcyl = 0;
        uptr->CYL = 0;
        sim_debug (DBG_CMD, &rd_dev, "RESET\n");
        rd_done (TRM_OK, FALSE);
        break;

    case CMD_SETREG:
        rd_rg_p = data & 0xF;
        sim_debug (DBG_CMD, &rd_dev, "SETREG, reg = %d\n", rd_rg_p);
        break;

    case CMD_DESELECT:
        sim_debug (DBG_CMD, &rd_dev, "DESELECT\n");
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_DRVSEL:
        rd_cstat = (rd_cstat & ~CST_SDRV) | (data & CST_SDRV);
        uptr = &rd_unit[CUR_DRV];                       /* get new unit */
        if ((uptr->flags & (UNIT_DIS + UNIT_ATT)) == UNIT_ATT) {
            rd_done (TRM_OK, TRUE);                     /* drive installed */
            uptr->HEAD = rd_dhead;
            uptr->CYL = 0;
            rd_set_dstat (uptr);
            sim_debug (DBG_CMD, &rd_dev, "DRVSEL, drive = %d\n", CUR_DRV);
            }
        else {
            rd_done (TRM_ERR_TRAN, TRUE);               /* drive not installed */
            rd_set_dstat (uptr);
            sim_debug (DBG_CMD, &rd_dev, "DRVSEL, drive = %d (not present)\n", CUR_DRV);
            }
        break;

    case CMD_STEP:
        sim_debug (DBG_CMD, &rd_dev, "STEP\n");

        if (data & 0x2)                                 /* direction */
            uptr->CYL--;                                /* in */
        else
            uptr->CYL++;                                /* out */

        max_cyl = drv_tab[GET_DTYPE (uptr->flags)].cyl;
        if (uptr->CYL == max_cyl)                       /* check for wrap */
            uptr->CYL = 0;
        if (uptr->CYL == -1)
            uptr->CYL = (max_cyl - 1);

        rd_set_dstat (uptr);
        rd_done (TRM_OK, TRUE);
        break;

    default:
        sim_activate (uptr, rd_cwait);
        break;
        }
}

int32 rd_decode_cmd (int32 data)
{
if (data == 0) return CMD_RESET;                        /* 00000000    Reset */
if (data & 0x80) {
    if (data & 0x20) return CMD_WRPHY;                  /* 1x0xxxxx    Write Physical */
    else return CMD_WRLOG;                              /* 1x1xxxxx    Write Logical */
    }
if (data & 0x40) {
    if(data & 0x20) return CMD_FORMAT;                  /* 011xx_x_    Format Track */
    else if (!(data & 0x10)) return CMD_SETREG;         /* 0100xxxx    Set Register pointer */
    else if (!(data & 0x8)) return CMD_RDID;            /* 01010xxx    Seek/Read ID */
    else if (data & 0x4) return CMD_RDLOG;              /* 010111xx    Read Logical */
    else if (data & 0x2) return CMD_RDTRK;              /* 0101101x    Read Track */
    else return CMD_RDPHY;                              /* 0101100x    Read Physical */
    }
if (data & 0x20) return CMD_DRVSEL;                     /* 001xxxxx    Drive Select */
else if (data & 0x10) return CMD_POLL;                  /* 0001xxxx    Poll Drives */
else if (data & 0x4) return CMD_STEP;                   /* 000001xx    Step */
else if (data & 0x2) return CMD_RESTORE;                /* 0000001x    Restore Drive */
else if (data & 0x1) return CMD_DESELECT;               /* 00000001    Deselect Drive */
return CMD_UNKNOWN;
}

/* read cylinder 0 - simulate special formatting */

t_stat rd_rdcyl0 (int32 hd, int32 dtype)
{
uint32 i;
uint16 c;

if (hd <= 2) {
    memset (rd_xb, 0, sizeof(*rd_xb) * 256);            /* fill sector buffer with 0's */
    /* rd_xb[0]-rd_xb[3] */                             /* 8 bytes of zero */
    rd_xb[4] = 0x3600;
    rd_xb[5] = drv_tab[dtype].xbn & WMASK;              /* number of XBNs */
    rd_xb[6] = (drv_tab[dtype].xbn >> 16) & WMASK;
    rd_xb[7] = drv_tab[dtype].dbn & WMASK;              /* number of DBNs */
    rd_xb[8] = (drv_tab[dtype].dbn >> 16) & WMASK;
    rd_xb[9] = drv_tab[dtype].lbn & WMASK;              /* number of LBNs (Logical-Block-Numbers) */
    rd_xb[10] = (drv_tab[dtype].lbn >> 16) & WMASK;
    rd_xb[11] = drv_tab[dtype].rbn & WMASK;             /* number of RBNs (Replacement-Block-Numbers) */
    rd_xb[12] = (drv_tab[dtype].rbn >> 16) & WMASK;
    rd_xb[13] = drv_tab[dtype].sect;                    /* number of sectors per track */
    rd_xb[14] = drv_tab[dtype].tpg;                     /* number of tracks */
    rd_xb[15] = drv_tab[dtype].cyl;                     /* number of cylinders */
    rd_xb[16] = drv_tab[dtype].cylp;                    /* first cylinder for write precompensation */
    rd_xb[17] = drv_tab[dtype].cylr;                    /* first cylinder for reduced write current */
    rd_xb[18] = 0;                                      /* seek rate or zero for buffered seeks */
    rd_xb[19] = 1;                                      /* 0 if CRC, 1 if ECC is being used */
    rd_xb[20] = drv_tab[dtype].rcts;                    /* "replacement control table" (RCT) */
    rd_xb[21] = drv_tab[dtype].rctc;                    /* number of copies of the RCT */
    rd_xb[22] = drv_tab[dtype].med & WMASK;             /* media identifier */
    rd_xb[23] = (drv_tab[dtype].med >> 16) & WMASK;
    rd_xb[24] = 1;                                      /* sector-to-sector interleave */
    rd_xb[25] = 7;                                      /* head-to-head skew */
    rd_xb[26] = drv_tab[dtype].ccs;                     /* cylinder-to-cylinder skew */
    rd_xb[27] = 16;                                     /* size of GAP 0 in the MFM format */
    rd_xb[28] = 16;                                     /* size of GAP 1 in the MFM format */
    rd_xb[29] = 5;                                      /* size of GAP 2 in the MFM format */
    rd_xb[30] = 40;                                     /* size of GAP 3 in the MFM format */
    rd_xb[31] = 13;                                     /* sync value used when formatting */
    /* rd_xb[32]-rd_xb[47] */                           /* 32 bytes of zero - reserved for use by the RQDX formatter */
    rd_xb[48] = 0x3039;                                 /* serial number */
    /* rd_xb[49]-rd_xb[255] */                          /* 414 bytes of zero - Filler bytes to the end of the block */
    for (i = c = 0; i < 256; i++)
        c = c + rd_xb[i];
    rd_xb[255] = c;                                     /* checksum */
    }
else
    memset (&rd_xb[0], 0, RD_NUMBY);
return SCPE_OK;
}

t_stat rd_rddata (UNIT *uptr, t_lba lba, t_seccnt sects)
{
t_seccnt sectsread;
t_stat r;

r = sim_disk_rdsect (uptr, lba, (uint8 *)rd_xb, &sectsread, sects);
sim_disk_data_trace (uptr, (uint8 *)rd_xb, lba, sectsread*RD_NUMBY, "sim_disk_rdsect", DBG_DAT & rd_dev.dctrl, DBG_REQ);
return r;
}

t_stat rd_wrdata (UNIT *uptr, t_lba lba, t_seccnt sects)
{
t_seccnt sectswritten;

sim_disk_data_trace (uptr, (uint8 *)rd_xb, lba, sects*RD_NUMBY, "sim_disk_wrsect", DBG_DAT & rd_dev.dctrl, DBG_REQ);
return sim_disk_wrsect (uptr, lba, (uint8 *)rd_xb, &sectswritten, sects);
}

/* Unit service */

t_stat rd_svc (UNIT *uptr)
{
t_lba lba;
int32 dtype = GET_DTYPE (uptr->flags);

switch (uptr->CMD) {
    case CMD_RDPHY:
    case CMD_RDLOG:
        uptr->CYL = rd_dcyl;
        uptr->HEAD = rd_dhead;
        if (dtype >= RD31_DTYPE) {
            if (rd_dcyl == 0) {
                lba = 0;
                sim_debug (DBG_RD, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X\n", rd_dcyl, rd_dhead, rd_dsect, lba);
                rd_rdcyl0 (rd_dhead, dtype);
                }
            else {
                lba = GET_DA (uptr, (rd_dcyl - 1), rd_dhead, rd_dsect);
                sim_debug (DBG_RD, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X\n", rd_dcyl, rd_dhead, rd_dsect, lba);
                rd_rddata (uptr, lba, rd_scnt);
                }
            }
        else {
            if (rd_rtcnt & 0x1) {
                rd_cstat |= CST_SYNCE;
                rd_done (TRM_ERR_RD, TRUE);
                return SCPE_OK;
                }
            lba = GET_DA (uptr, rd_dcyl, rd_dhead, (rd_dsect - 1));
            sim_debug (DBG_RD, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X\n", rd_dcyl, rd_dhead, rd_dsect, lba);
            rd_rddata (uptr, lba, rd_scnt);
            }

        ddb_WriteW (rd_dma, (rd_scnt * RD_NUMBY), rd_xb);
        rd_dma = (rd_dma + (rd_scnt * RD_NUMBY)) & 0xFFFFFF;
        rd_dsect = rd_dsect + rd_scnt - 1;
        rd_scnt = 0;
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_WRPHY:
    case CMD_WRLOG:
        uptr->CYL = rd_dcyl;
        uptr->HEAD = rd_dhead;
        ddb_ReadW (rd_dma, (rd_scnt * RD_NUMBY), rd_xb);
        rd_dma = (rd_dma + (rd_scnt * RD_NUMBY)) & 0xFFFFFF;
        if (dtype >= RD31_DTYPE) {
            if (rd_dcyl == 0) {
                lba = 0;
                sim_debug (DBG_WR, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X (ignored)\n", rd_dcyl, rd_dhead, rd_dsect, lba);
                }
            else {
                lba = GET_DA (uptr, (rd_dcyl - 1), rd_dhead, rd_dsect);
                sim_debug (DBG_WR, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X\n", rd_dcyl, rd_dhead, rd_dsect, lba);
                rd_wrdata (uptr, lba, rd_scnt);
                }
            }
        else {
            if (rd_rtcnt & 0x1) {
                rd_cstat |= 0x8;
                rd_done (TRM_ERR_RD, TRUE);
                return SCPE_OK;
                }
            lba = GET_DA (uptr, rd_dcyl, rd_dhead, (rd_dsect - 1));
            sim_debug (DBG_WR, &rd_dev, "cyl=%04d, hd=%d, sect=%02d, lba=%08X\n", rd_dcyl, rd_dhead, rd_dsect, lba);
            rd_wrdata (uptr, lba, rd_scnt);
            }
        rd_dsect = rd_dsect + rd_scnt - 1;
        rd_scnt = 0;
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_RESTORE:
        sim_debug (DBG_CMD, &rd_dev, "RESTORE\n");
        uptr->CYL = 0;
        rd_set_dstat (uptr);
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_RDID:
        sim_debug (DBG_CMD, &rd_dev, "RD ID\n");
        if (uptr->CMD & 0x4) {                          /* step to new position? */
            uptr->CYL = rd_dcyl;
            uptr->HEAD = rd_dhead;
            }
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_RDTRK:                                     /* not implemented */
        sim_debug (DBG_CMD, &rd_dev, "RD TRK\n");
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_POLL:                                      /* not implemented */
        sim_debug (DBG_CMD, &rd_dev, "POLL\n");
        rd_done (TRM_OK, TRUE);
        break;

    case CMD_FORMAT:                                    /* not implemented */
        sim_debug (DBG_CMD, &rd_dev, "FORMAT\n");
        rd_done (TRM_OK, TRUE);
        break;

    default:
        rd_done (TRM_OK, TRUE);
        }

return SCPE_OK;
}

/* Update the drive status register to reflect the current unit state */

void rd_set_dstat (UNIT *uptr)
{
if ((uptr->flags & (UNIT_DIS + UNIT_ATT)) == UNIT_ATT) { /* drive present? */
    uptr->STAT = (DST_SCOM | DST_RDY);
    if (uptr->flags & UNIT_WPRT)                        /* write protected? */
        uptr->STAT |= DST_WPT;
    if (CUR_DRV != 2)                                   /* not fdd? */
        uptr->STAT |= DST_SELA;
    if (uptr->CYL == 0)                                 /* at cyl 0? */
        uptr->STAT |= DST_TRK0;
    }
else                                                    /* drive not present */
    uptr->STAT = 0;
}

/* Command complete.  Set done and put final value in interface register,
   request interrupt if needed, return to IDLE state.
*/

void rd_done (int32 term_code, t_bool setint)
{
rd_stat = ((term_code & STAT_M_TRMC) << STAT_V_TRMC) | STAT_DONE;
if ((rd_term & 0x20) && setint) {
    SET_INT (SCA);
    rd_stat = rd_stat | STAT_INT;
    }
}

/* Device initialization. */

t_stat rd_reset (DEVICE *dptr)
{
rd_rg_p = 0;
CLR_INT (SCA);                                          /* clear int req */
rd_done (TRM_OK, FALSE);
sim_cancel (&rd_unit[0]);                               /* cancel drive 0 */
sim_cancel (&rd_unit[1]);                               /* cancel drive 1 */
sim_cancel (&rd_unit[2]);                               /* cancel drive 2 */
if (rd_xb == NULL)
    rd_xb = (uint16 *) calloc (RD_MAXFR, sizeof (uint8));
if (rd_xb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* Attach routine */

t_stat rd_attach (UNIT *uptr, CONST char *cptr)
{
return sim_disk_attach (uptr, cptr, RD_NUMBY,
                        sizeof (uint8), TRUE, DBG_DSK,
                        drv_tab[GET_DTYPE (uptr->flags)].name, 0, 0);
}

/* Detach routine */

t_stat rd_detach (UNIT *uptr)
{
sim_cancel (uptr);
return sim_disk_detach (uptr);
}

/* Set unit type */

t_stat rd_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;

if ((uptr == &rd_unit[0]) || (uptr == &rd_unit[1])) {   /* hard disk only */
    if (val < RD31_DTYPE)
        return SCPE_ARG;
    uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (val << UNIT_V_DTYPE);
    }
if (uptr == &rd_unit[2]) {                              /* floppy disk only */
    if (val > RX33_DTYPE)
        return SCPE_ARG;
    uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (val << UNIT_V_DTYPE);
    }
uptr->capac = ((t_addr) drv_tab[val].lbn) * RD_NUMBY;

return SCPE_OK;
}

/* Show unit type */

t_stat rd_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s", drv_tab[GET_DTYPE (uptr->flags)].name);
return SCPE_OK;
}

t_stat rd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "HDC9224 Disk Controller (RD)\n\n");
fprintf (st, "The RD controller simulates the HDC9224 Universal Disk Controller\n");
fprintf (st, "chip with up to two hard drives and one floppy drive.\n");
if (dptr->flags & DEV_DISABLE)
    fprintf (st, "Initially the RD controller is disabled.\n");
else
    fprintf (st, "The RD controller cannot be disabled.\n");
fprintf (st, "Each unit can be set to one of several drive types:\n");
fprint_set_help (st, dptr);
fprintf (st, "\nUnit RD0 and RD1 only support hard disk types (RDxx) and unit RD2\n");
fprintf (st, "only supports a floppy disk type (RX33)\n");
fprintf (st, "Configured options can be displayed with:\n\n");
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
fprintf (st, "\nDisk drives on the RD device can be attached to simulated storage in the\n");
fprintf (st, "following ways:\n\n");
sim_disk_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *rd_description (DEVICE *dptr)
{
return "HDC9224 disk controller";
}
