/* pdp18b_dt.c: 18b DECtape simulator

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

   dt           (PDP-4, PDP-7) Type 550/555 DECtape
                (PDP-9) TC02/TU55 DECtape
                (PDP-15) TC15/TU56 DECtape

   15-Mar-17    RMS     Fixed dt_seterr to clear successor states
   09-Mar-17    RMS     Fixed dt_seterr to handle nx unit select (COVERITY)
   10-Mar-16    RMS     Added 3-cycle databreak set/show entries
   07-Mar-16    RMS     Revised for dynamically allocated memory
   13-Mar-15    RMS     Added APIVEC register
   28-Mar-15    RMS     Revised to use sim_printf
   23-Jun-06    RMS     Fixed switch conflict in ATTACH
                        Revised Type 550 header based on DECTOG formatter
   13-Jun-06    RMS     Fixed checksum calculation bug in Type 550
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   25-Jan-04    RMS     Revised for device debug support
   14-Jan-04    RMS     Revised IO device call interface
                        Changed sim_fsize calling sequence, added STOP_OFFR
   26-Oct-03    RMS     Cleaned up buffer copy code
   18-Oct-03    RMS     Fixed reverse checksum in read all
                        Added DECtape off reel message
                        Simplified timing
   25-Apr-03    RMS     Revised for extended file support
   14-Mar-03    RMS     Fixed variable size interaction with save/restore
   17-Oct-02    RMS     Fixed bug in end of reel logic
   05-Oct-02    RMS     Added DIB, device number support
   12-Sep-02    RMS     Added 16b format support
   13-Aug-02    RMS     Corrected Type 550 unit select logic
   25-Jul-02    RMS     Added PDP-4 support
   30-May-02    RMS     Widened POS to 32b
   10-Feb-02    RMS     Added PDP-7 support
   06-Jan-02    RMS     Revised enable/disable support
   29-Nov-01    RMS     Added read only unit support
   25-Nov-01    RMS     Revised interrupt structure
                        Changed POS, STATT, LASTT, FLG to arrays
   29-Aug-01    RMS     Added casts to PDP-8 unpack routine
   17-Jul-01    RMS     Moved function prototype
   11-May-01    RMS     Fixed bug in reset
   26-Apr-01    RMS     Added device enable/disable support
   15-Mar-01    RMS     Added 129th word to PDP-8 format

   18b DECtapes are represented in memory by fixed length buffer of 32b words.
   Three file formats are supported:

        18b/36b                 256 words per block [256 x 18b]
        16b                     256 words per block [256 x 16b]
        12b                     129 words per block [129 x 12b]

   When a 16b or 12b DECtape file is read in, it is converted to 18b/36b format.

   DECtape motion is measured in 3b lines.  Time between lines is 33.33us.
   Tape density is nominally 300 lines per inch.  The format of a DECtape (as
   taken from the PDP-7 formatter) is:

        reverse end zone        7144 reverse end zone codes ~ 12 feet
        reverse buffer          200 interblock codes
        block 0
         :
        block n
        forward buffer          200 interblock codes
        forward end zone        7144 forward end zone codes ~ 12 feet

   A block consists of five 18b header words, a tape-specific number of data
   words, and five 18b trailer words.  All systems except the PDP-8 use a
   standard block length of 256 words; the PDP-8 uses a standard block length
   of 86 words (x 18b = 129 words x 12b).  PDP-4/7 DECtapes came in two
   formats.  The first 5 controllers used a 4 word header/trailer (missing
   word 0/4).  All later serial numbers used the standard header.  The later,
   standard header/trailer is simulated here.

   Because a DECtape file only contains data, the simulator cannot support
   write timing and mark track and can only do a limited implementation
   of read all and write all.  Read all assumes that the tape has been
   conventionally written forward:

        header word 0           0
        header word 1           block number (for forward reads)
        header words 2,3        0
        header word 4           checksum (for reverse reads)
        :
        trailer word 4          checksum (for forward reads)
        trailer words 3,2       0
        trailer word 1          block number (for reverse reads)
        trailer word 0          0

   Write all writes only the data words and dumps the interblock words in the
   bit bucket.

   The Type 550 controller has a 4b unit select field, for units 1-8; the TC02
   has a 3b unit select field, with unit 8 being represented as 0.  The code
   assumes that the GETUNIT macro returns a unit number in the range of 0-7,
   with 8 represented as 0, and an invalid unit as -1.
*/

#include "pdp18b_defs.h"

#define DT_NUMDR        8                               /* #drives */
#define UNIT_V_8FMT     (UNIT_V_UF + 0)                 /* 12b format */
#define UNIT_V_11FMT    (UNIT_V_UF + 1)                 /* 16b format */
#define UNIT_8FMT       (1 << UNIT_V_8FMT)
#define UNIT_11FMT      (1 << UNIT_V_11FMT)
#define STATE           u3                              /* unit state */
#define LASTT           u4                              /* last time update */
#define WRITTEN         u5                              /* device buffer is dirty and needs flushing */
#define DT_WC           030                             /* word count */
#define DT_CA           031                             /* current addr */

/* System independent DECtape constants */

#define DT_LPERMC       6                               /* lines per mark track */
#define DT_BLKWD        1                               /* blk no word in h/t */
#define DT_CSMWD        4                               /* checksum word in h/t */
#define DT_HTWRD        5                               /* header/trailer words */
#define DT_EZLIN        (8192 * DT_LPERMC)              /* end zone length */
#define DT_BFLIN        (200 * DT_LPERMC)               /* buffer length */
#define DT_BLKLN        (DT_BLKWD * DT_LPERMC)          /* blk no line in h/t */
#define DT_CSMLN        (DT_CSMWD * DT_LPERMC)          /* csum line in h/t */
#define DT_HTLIN        (DT_HTWRD * DT_LPERMC)          /* header/trailer lines */

/* 16b, 18b, 36b DECtape constants */

#define D18_WSIZE       6                               /* word size in lines */
#define D18_BSIZE       256                             /* block size in 18b */
#define D18_TSIZE       578                             /* tape size */
#define D18_LPERB       (DT_HTLIN + (D18_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D18_FWDEZ       (DT_EZLIN + (D18_LPERB * D18_TSIZE))
#define D18_CAPAC       (D18_TSIZE * D18_BSIZE)         /* tape capacity */
#define D11_FILSIZ      (D18_CAPAC * sizeof (int16))

/* 12b DECtape constants */

#define D8_WSIZE        4                               /* word size in lines */
#define D8_BSIZE        86                              /* block size in 18b */
#define D8_TSIZE        1474                            /* tape size */
#define D8_LPERB        (DT_HTLIN + (D8_BSIZE * DT_WSIZE) + DT_HTLIN)
#define D8_FWDEZ        (DT_EZLIN + (D8_LPERB * D8_TSIZE))
#define D8_CAPAC        (D8_TSIZE * D8_BSIZE)           /* tape capacity */

#define D8_NBSIZE       ((D8_BSIZE * D18_WSIZE) / D8_WSIZE)
#define D8_FILSIZ       (D8_NBSIZE * D8_TSIZE * sizeof (int16))

/* This controller */

#define DT_CAPAC        D18_CAPAC                       /* default */
#define DT_WSIZE        D18_WSIZE

/* Calculated constants, per unit */

#define DTU_BSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_BSIZE: D18_BSIZE)
#define DTU_TSIZE(u)    (((u)->flags & UNIT_8FMT)? D8_TSIZE: D18_TSIZE)
#define DTU_LPERB(u)    (((u)->flags & UNIT_8FMT)? D8_LPERB: D18_LPERB)
#define DTU_FWDEZ(u)    (((u)->flags & UNIT_8FMT)? D8_FWDEZ: D18_FWDEZ)
#define DTU_CAPAC(u)    (((u)->flags & UNIT_8FMT)? D8_CAPAC: D18_CAPAC)

#define DT_LIN2BL(p,u)  (((p) - DT_EZLIN) / DTU_LPERB (u))
#define DT_LIN2OF(p,u)  (((p) - DT_EZLIN) % DTU_LPERB (u))
#define DT_LIN2WD(p,u)  ((DT_LIN2OF (p,u) - DT_HTLIN) / DT_WSIZE)
#define DT_BLK2LN(p,u)  (((p) * DTU_LPERB (u)) + DT_EZLIN)
#define DT_QREZ(u)      (((u)->pos) < DT_EZLIN)
#define DT_QFEZ(u)      (((u)->pos) >= ((uint32) DTU_FWDEZ (u)))
#define DT_QEZ(u)       (DT_QREZ (u) || DT_QFEZ (u))

/* Status register A */

#if defined (TC02)                                      /* TC02/TC15 */
#define DTA_V_UNIT      15                              /* unit select */
#define DTA_M_UNIT      07
#define DTA_UNIT        (DTA_M_UNIT << DTA_V_UNIT)
#define DTA_V_MOT       13                              /* motion */
#define DTA_M_MOT       03
#define DTA_V_MODE      12                              /* mode */
#define DTA_V_FNC       9                               /* function */
#define DTA_M_FNC       07
#define  FNC_MOVE        00                             /* move */
#define  FNC_SRCH        01                             /* search */
#define  FNC_READ        02                             /* read */
#define  FNC_RALL        03                             /* read all */
#define  FNC_WRIT        04                             /* write */
#define  FNC_WALL        05                             /* write all */
#define  FNC_WMRK        06                             /* write timing */
#define DTA_V_ENB       8                               /* int enable */
#define DTA_V_CERF      7                               /* clr error flag */
#define DTA_V_CDTF      6                               /* clr DECtape flag */
#define DTA_FWDRV       (1u << (DTA_V_MOT + 1))
#define DTA_STSTP       (1u << DTA_V_MOT)
#define DTA_MODE        (1u << DTA_V_MODE)
#define DTA_ENB         (1u << DTA_V_ENB)
#define DTA_CERF        (1u << DTA_V_CERF)
#define DTA_CDTF        (1u << DTA_V_CDTF)
#define DTA_RW          (0777700 & ~(DTA_CERF | DTA_CDTF))
#define DTA_GETUNIT(x)  (((x) >> DTA_V_UNIT) & DTA_M_UNIT)
#define DT_UPDINT       if ((dtsa & DTA_ENB) && (dtsb & (DTB_ERF | DTB_DTF))) \
                            SET_INT (DTA); \
                        else CLR_INT (DTA);

#else                                                   /* Type 550 */

static const int32 map_unit[16] = {                     /* Type 550 unit map */
    -1, 1,  2,  3,  4,  5,  6,  7,
    0, -1, -1, -1, -1, -1, -1, -1
    };

#define DTA_V_UNIT      12                              /* unit select */
#define DTA_M_UNIT      017
#define DTA_UNIT        (DTA_M_UNIT << DTA_V_UNIT)
#define DTA_V_MOT       4                               /* motion */
#define DTA_M_MOT       03
#define DTA_V_FNC       0                               /* function */
#define DTA_M_FNC       07
#define  FNC_MOVE        00                             /* move */
#define  FNC_SRCH        01                             /* search */
#define  FNC_READ        02                             /* read */
#define  FNC_WRIT        03                             /* write */
#define  FNC_RALL        05                             /* read all */
#define  FNC_WALL        06                             /* write all */
#define  FNC_WMRK        07                             /* write timing */
#define DTA_STSTP       (1u << (DTA_V_MOT + 1))
#define DTA_FWDRV       (1u << DTA_V_MOT)
#define DTA_MODE        0                               /* not implemented */
#define DTA_RW          077
#define DTA_GETUNIT(x)  map_unit[(((x) >> DTA_V_UNIT) & DTA_M_UNIT)]
#define DT_UPDINT       if (dtsb & (DTB_DTF | DTB_BEF | DTB_ERF)) \
                            SET_INT (DTA); \
                        else CLR_INT (DTA);
#endif

#define DTA_GETMOT(x)   (((x) >> DTA_V_MOT) & DTA_M_MOT)
#define DTA_GETFNC(x)   (((x) >> DTA_V_FNC) & DTA_M_FNC)

/* Status register B */

#if defined (TC02)                                      /* TC02/TC15 */
#define DTB_V_ERF       17                              /* error flag */
#define DTB_V_MRK       16                              /* mark trk err */
#define DTB_V_END       15                              /* end zone err */
#define DTB_V_SEL       14                              /* select err */
#define DTB_V_PAR       13                              /* parity err */
#define DTB_V_TIM       12                              /* timing err */
#define DTB_V_DTF       6                               /* DECtape flag */
#define DTB_ERF         (1u << DTB_V_ERF)
#define DTB_MRK         (1u << DTB_V_MRK)
#define DTB_END         (1u << DTB_V_END)
#define DTB_SEL         (1u << DTB_V_SEL)
#define DTB_PAR         (1u << DTB_V_PAR)
#define DTB_TIM         (1u << DTB_V_TIM)
#define DTB_DTF         (1u << DTB_V_DTF)
#define DTB_ALLERR      (DTB_ERF | DTB_MRK | DTB_END | DTB_SEL | \
                         DTB_PAR | DTB_TIM)

#else                                                   /* Type 550 */
#define DTB_V_DTF       17                              /* data flag */
#define DTB_V_BEF       16                              /* block end flag */
#define DTB_V_ERF       15                              /* error flag */
#define DTB_V_END       14                              /* end of tape */
#define DTB_V_TIM       13                              /* timing err */
#define DTB_V_REV       12                              /* reverse */
#define DTB_V_GO        11                              /* go */
#define DTB_V_MRK       10                              /* mark trk err */
#define DTB_V_SEL       9                               /* select err */
#define DTB_DTF         (1u << DTB_V_DTF)
#define DTB_BEF         (1u << DTB_V_BEF)
#define DTB_ERF         (1u << DTB_V_ERF)
#define DTB_END         (1u << DTB_V_END)
#define DTB_TIM         (1u << DTB_V_TIM)
#define DTB_REV         (1u << DTB_V_REV)
#define DTB_GO          (1u << DTB_V_GO)
#define DTB_MRK         (1u << DTB_V_MRK)
#define DTB_SEL         (1u << DTB_V_SEL)
#define DTB_ALLERR      (DTB_END | DTB_TIM | DTB_MRK | DTB_SEL)
#endif

/* DECtape state */

#define DTS_V_MOT       3                               /* motion */
#define DTS_M_MOT       07
#define  DTS_STOP        0                              /* stopped */
#define  DTS_DECF        2                              /* decel, fwd */
#define  DTS_DECR        3                              /* decel, rev */
#define  DTS_ACCF        4                              /* accel, fwd */
#define  DTS_ACCR        5                              /* accel, rev */
#define  DTS_ATSF        6                              /* @speed, fwd */
#define  DTS_ATSR        7                              /* @speed, rev */
#define DTS_DIR         01                              /* dir mask */
#define DTS_V_FNC       0                               /* function */
#define DTS_M_FNC       07
#define  DTS_OFR        7                               /* "off reel" */
#define DTS_GETMOT(x)   (((x) >> DTS_V_MOT) & DTS_M_MOT)
#define DTS_GETFNC(x)   (((x) >> DTS_V_FNC) & DTS_M_FNC)
#define DTS_V_2ND       6                               /* next state */
#define DTS_V_3RD       (DTS_V_2ND + DTS_V_2ND)         /* next next */
#define DTS_STA(y,z)    (((y) << DTS_V_MOT) | ((z) << DTS_V_FNC))
#define DTS_SETSTA(y,z) uptr->STATE = DTS_STA (y, z)
#define DTS_SET2ND(y,z) uptr->STATE = (uptr->STATE & 077) | \
                        ((DTS_STA (y, z)) << DTS_V_2ND)
#define DTS_SET3RD(y,z) uptr->STATE = (uptr->STATE & 07777) | \
                        ((DTS_STA (y, z)) << DTS_V_3RD)
#define DTS_NXTSTA(x)   (x >> DTS_V_2ND)

/* Operation substates */

#define DTO_WCO         1                               /* wc overflow */
#define DTO_SOB         2                               /* start of block */

/* Logging */

#define LOG_MS          001                             /* move, search */
#define LOG_RW          002                             /* read, write */
#define LOG_RA          004                             /* read all */
#define LOG_BL          010                             /* block # lblk */

#define ABS(x)          (((x) < 0)? (-(x)): (x))

extern int32 *M;
extern int32 int_hwre[API_HLVL+1];
extern int32 api_vec[API_HLVL][32];
extern UNIT cpu_unit;

int32 dtsa = 0;                                         /* status A */
int32 dtsb = 0;                                         /* status B */
int32 dtdb = 0;                                         /* data buffer */
int32 dt_ltime = 12;                                    /* interline time */
int32 dt_dctime = 40000;                                /* decel time */
int32 dt_substate = 0;
int32 dt_logblk = 0;
int32 dt_stopoffr = 0;                                  /* stop on off reel */

int32 dt75 (int32 dev, int32 pulse, int32 dat);
int32 dt76 (int32 dev, int32 pulse, int32 dat);
int32 dt_iors (void);
t_stat dt_svc (UNIT *uptr);
t_stat dt_reset (DEVICE *dptr);
t_stat dt_attach (UNIT *uptr, CONST char *cptr);
void dt_flush (UNIT *uptr);
t_stat dt_detach (UNIT *uptr);
void dt_deselect (int32 oldf);
void dt_newsa (int32 newf);
void dt_newfnc (UNIT *uptr, int32 newsta);
t_bool dt_setpos (UNIT *uptr);
void dt_schedez (UNIT *uptr, int32 dir);
void dt_seterr (UNIT *uptr, int32 e);
int32 dt_comobv (int32 val);
int32 dt_csum (UNIT *uptr, int32 blk);
int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos);

/* DT data structures

   dt_dev       DT device descriptor
   dt_unit      DT unit list
   dt_reg       DT register list
   dt_mod       DT modifier list
*/

DIB dt_dib = { DEV_DTA, 2, &dt_iors, { &dt75, &dt76 } };

UNIT dt_unit[] = {
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE, DT_CAPAC) }
    };

REG dt_reg[] = {
    { ORDATAD (DTSA, dtsa, 18, "status register A") },
    { ORDATAD (DTSB, dtsb, 18, "status register B") },
    { ORDATAD (DTDB, dtdb, 18, "data buffer") },
    { FLDATAD (INT, int_hwre[API_DTA], INT_V_DTA, "interrupt pending flag") },
#if defined (DTA_V_ENB)
    { FLDATAD (ENB, dtsa, DTA_V_ENB, "interrupt enable flag") },
#endif
    { FLDATAD (DTF, dtsb, DTB_V_DTF, "DECtape flag") },
#if defined (DTB_V_BEF)
    { FLDATAD (BEF, dtsb, DTB_V_BEF, "block and flag") },
#endif
    { FLDATAD (ERF, dtsb, DTB_V_ERF, "error flag") },
    { DRDATAD (LTIME, dt_ltime, 31, "time between lines"), REG_NZ },
    { DRDATAD (DCTIME, dt_dctime, 31, "time to declarate to a full stop"), REG_NZ },
    { ORDATAD (SUBSTATE, dt_substate, 2, "read/write command substate") },
    { DRDATA (LBLK, dt_logblk, 12), REG_HIDDEN },
    { URDATAD (POS, dt_unit[0].pos, 10, T_ADDR_W, 0,
              DT_NUMDR, PV_LEFT | REG_RO, "positions in lines, units 0 to 7") },
    { URDATAD (STATT, dt_unit[0].STATE, 8, 18, 0,
              DT_NUMDR, REG_RO, "unit state, units 0 to 7") },
    { URDATA (LASTT, dt_unit[0].LASTT, 10, T_ADDR_W, 0,
              DT_NUMDR, REG_HRO) },
    { ORDATA (DEVNO, dt_dib.dev, 6), REG_HRO },
    { FLDATAD (STOP_OFFR, dt_stopoffr, 0, "stop on off-reel error") },
#if defined (TC02)
    { ORDATA (APIVEC, api_vec[API_DTA][INT_V_DTA], 6), REG_HRO },
#endif
    { NULL }
    };

MTAB dt_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    { UNIT_8FMT + UNIT_11FMT, 0, "18b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_8FMT, "12b", NULL, NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_11FMT, "16b", NULL, NULL },
#if defined (TC02)
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, DT_WC, "WC", "WC", &set_3cyc_reg, &show_3cyc_reg, (void *)"WC" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, DT_CA, "CA", "CA", &set_3cyc_reg, &show_3cyc_reg, (void *)"CA" },
#endif
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno },
    { 0 }
    };

DEBTAB dt_deb[] = {
    { "MOTION", LOG_MS },
    { "DATA", LOG_RW },
    { "READALL", LOG_RA },
    { "BLOCK", LOG_BL },
    { NULL, 0 }
    };

DEVICE dt_dev = {
    "DT", dt_unit, dt_reg, dt_mod,
    DT_NUMDR, 8, 24, 1, 8, 18,
    NULL, NULL, &dt_reset,
    NULL, &dt_attach, &dt_detach,
    &dt_dib, DEV_DISABLE | DEV_DEBUG, 0,
    dt_deb, NULL, NULL
    };

/* IOT routines */

#if defined (TC02)                                      /* TC02/TC15 */
int32 dt75 (int32 dev, int32 pulse, int32 dat)
{
int32 old_dtsa = dtsa, fnc;
UNIT *uptr;

if (((pulse & 060) == 040) && (pulse & 05)) {           /* select */
    if (pulse & 01)                                     /* DTCA */
        dtsa = 0;
    if (pulse & 02)                                     /* DTRA!... */
        dat = dtsa;
    if (pulse & 04) {                                   /* DTXA */
        if ((dat & DTA_CERF) == 0)
            dtsb = dtsb & ~DTB_ALLERR;
        if ((dat & DTA_CDTF) == 0)
            dtsb = dtsb & ~DTB_DTF;
        dtsa = dtsa ^ (dat & DTA_RW);
        }
    if ((old_dtsa ^ dtsa) & DTA_UNIT)
        dt_deselect (old_dtsa);
    uptr = dt_dev.units + DTA_GETUNIT (dtsa);           /* get unit */
    fnc = DTA_GETFNC (dtsa);                            /* get fnc */
    if (((uptr->flags) & UNIT_DIS) ||                   /* disabled? */
         (fnc >= FNC_WMRK) ||                           /* write mark? */
        ((fnc == FNC_WRIT) && (uptr->flags & UNIT_WPRT)) ||
        ((fnc == FNC_WALL) && (uptr->flags & UNIT_WPRT)))
        dt_seterr (uptr, DTB_SEL);                      /* select err */
    else dt_newsa (dtsa);                               /* new func */
    DT_UPDINT;
    return dat;
    }
if ((pulse & 067) == 042)                               /* DTRA */
    return dtsa;
if ((pulse & 067) == 061)                               /* DTEF */
    return ((dtsb & DTB_ERF)? IOT_SKP + dat: dat);
if ((pulse & 067) == 062)                               /* DTRB */
    return dtsb;
if ((pulse & 067) == 063)                               /* DTEF!DTRB */
    return ((dtsb & DTB_ERF)? IOT_SKP + dtsb: dtsb);
return dat;
}

int32 dt76 (int32 dev, int32 pulse, int32 dat)
{
if ((pulse & 01) && (dtsb & DTB_DTF))                   /* DTDF */
    return IOT_SKP + dat;
return dat;
}

#else                                                   /* Type 550 */
int32 dt75 (int32 dev, int32 pulse, int32 dat)
{
if (((pulse & 041) == 001) && (dtsb & DTB_DTF))         /* MMDF */
    dat = dat | IOT_SKP;
else if (((pulse & 041) == 041) && (dtsb & DTB_ERF))    /* MMEF */
    dat = dat | IOT_SKP;
if (pulse & 002) {                                      /* MMRD */
    dat = (dat & ~DMASK) | dtdb;
    dtsb = dtsb & ~(DTB_DTF | DTB_BEF);
    }
if (pulse & 004) {                                      /* MMWR */
    dtdb = dat & DMASK;
    dtsb = dtsb & ~(DTB_DTF | DTB_BEF);
    }
DT_UPDINT;
return dat;
}

int32 dt76 (int32 dev, int32 pulse, int32 dat)
{
int32 fnc, mot, unum;
UNIT *uptr = NULL;

unum = DTA_GETUNIT (dtsa);                              /* get unit no */
if (unum >= 0)                                          /* get unit */
    uptr = dt_dev.units + unum;
if ((pulse & 001) && (dtsb & DTB_BEF))                  /* MMBF */
    dat = dat | IOT_SKP;
if (pulse & 002) {                                      /* MMRS */
    dtsb = dtsb & ~(DTB_REV | DTB_GO);                  /* clr rev, go */
    if (uptr) {                                         /* valid unit? */
        mot = DTS_GETMOT (uptr->STATE);                 /* get motion */
        if (mot & DTS_DIR)                              /* rev? set */
            dtsb = dtsb | DTB_REV;
        if ((mot >= DTS_ACCF) || (uptr->STATE & 0777700))
            dtsb = dtsb | DTB_GO;                       /* accel? go */
        }
    dat = (dat & ~DMASK) | dtsb;
    }
if ((pulse & 044) == 044) {                             /* MMSE */
    if ((dtsa ^ dat) & DTA_UNIT)                        /* new unit? */
        dt_deselect (dtsa);
    dtsa = (dtsa & ~DTA_UNIT) | (dat & DTA_UNIT);
    dtsb = dtsb & ~(DTB_DTF | DTB_BEF | DTB_ERF | DTB_ALLERR);
    }
else if ((pulse & 044) == 004) {                        /* MMLC */
    dtsa = (dtsa & ~DTA_RW) | (dat & DTA_RW);           /* load dtsa */
    dtsb = dtsb & ~(DTB_DTF | DTB_BEF | DTB_ERF | DTB_ALLERR);
    fnc = DTA_GETFNC (dtsa);                            /* get fnc */
    if ((uptr == NULL) ||                               /* invalid? */
        ((uptr->flags) & UNIT_DIS) ||                   /* disabled? */
         (fnc >= FNC_WMRK) ||                           /* write mark? */
        ((fnc == FNC_WRIT) && (uptr->flags & UNIT_WPRT)) ||
        ((fnc == FNC_WALL) && (uptr->flags & UNIT_WPRT)))
        dt_seterr (uptr, DTB_SEL);                      /* select err */
    else dt_newsa (dtsa);
    }
DT_UPDINT;
return dat;
}
#endif

/* Unit deselect */

void dt_deselect (int32 oldf)
{
int32 old_unit, old_mot;
UNIT *uptr;

old_unit = DTA_GETUNIT (oldf);                          /* get unit no */
if (old_unit < 0)                                       /* invalid? */
    return;
uptr = dt_dev.units + old_unit;                         /* get unit */
old_mot = DTS_GETMOT (uptr->STATE);
if (old_mot >= DTS_ATSF)                                /* at speed? */
    dt_newfnc (uptr, DTS_STA (old_mot, DTS_OFR));
else if (old_mot >= DTS_ACCF)                           /* accelerating? */
    DTS_SET2ND (DTS_ATSF | (old_mot & DTS_DIR), DTS_OFR);
return;
}

/* Command register change

   1. If change in motion, stop to start
        - schedule acceleration
        - set function as next state
   2. If change in motion, start to stop
        - if not already decelerating (could be reversing),
          schedule deceleration
   3. If change in direction,
        - if not decelerating, schedule deceleration
        - set accelerating (other dir) as next state
        - set function as next next state
   4. If not accelerating or at speed,
        - schedule acceleration
        - set function as next state
   5. If not yet at speed,
        - set function as next state
   6. If at speed,
        - set function as current state, schedule function
*/

void dt_newsa (int32 newf)
{
int32 new_unit, prev_mot, new_fnc;
int32 prev_mving, new_mving, prev_dir, new_dir;
UNIT *uptr;

new_unit = DTA_GETUNIT (newf);                          /* new unit */
if (new_unit < 0)                                       /* invalid? */
    return;
uptr = dt_dev.units + new_unit;
if ((uptr->flags & UNIT_ATT) == 0) {                    /* new unit attached? */
    dt_seterr (uptr, DTB_SEL);                          /* no, error */
    return;
    }
prev_mot = DTS_GETMOT (uptr->STATE);                    /* previous motion */
prev_mving = prev_mot != DTS_STOP;                      /* previous moving? */
prev_dir = prev_mot & DTS_DIR;                          /* previous dir? */
new_mving = (newf & DTA_STSTP) != 0;                    /* new moving? */
new_dir = (newf & DTA_FWDRV) != 0;                      /* new dir? */
new_fnc = DTA_GETFNC (newf);                            /* new function? */

if ((prev_mving | new_mving) == 0)                      /* stop to stop */
    return;

if (new_mving & ~prev_mving) {                          /* start? */
    if (dt_setpos (uptr))                               /* update pos */
        return;
    sim_cancel (uptr);                                  /* stop current */
    sim_activate (uptr, dt_dctime - (dt_dctime >> 2));  /* sched accel */
    DTS_SETSTA (DTS_ACCF | new_dir, 0);                 /* state = accel */
    DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);           /* next = fnc */
    return;
    }

if (prev_mving & ~new_mving) {                          /* stop? */
    if ((prev_mot & ~DTS_DIR) != DTS_DECF) {            /* !already stopping? */
        if (dt_setpos (uptr))                           /* update pos */
            return;
        sim_cancel (uptr);                              /* stop current */
        sim_activate (uptr, dt_dctime);                 /* schedule decel */
        }
    DTS_SETSTA (DTS_DECF | prev_dir, 0);                /* state = decel */
    return;
    }

if (prev_dir ^ new_dir) {                               /* dir chg? */
    if ((prev_mot & ~DTS_DIR) != DTS_DECF) {            /* !already stopping? */
        if (dt_setpos (uptr))                           /* update pos */
            return;
        sim_cancel (uptr);                              /* stop current */
        sim_activate (uptr, dt_dctime);                 /* schedule decel */
        }
    DTS_SETSTA (DTS_DECF | prev_dir, 0);                /* state = decel */
    DTS_SET2ND (DTS_ACCF | new_dir, 0);                 /* next = accel */
    DTS_SET3RD (DTS_ATSF | new_dir, new_fnc);           /* next next = fnc */
    return;
    }

if (prev_mot < DTS_ACCF) {                              /* not accel/at speed? */
    if (dt_setpos (uptr))                               /* update pos */
        return;
    sim_cancel (uptr);                                  /* cancel cur */
    sim_activate (uptr, dt_dctime - (dt_dctime >> 2));  /* sched accel */
    DTS_SETSTA (DTS_ACCF | new_dir, 0);                 /* state = accel */
    DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);           /* next = fnc */
    return;
    }

if (prev_mot < DTS_ATSF) {                              /* not at speed? */
    DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);           /* next = fnc */
    return;
    }

dt_newfnc (uptr, DTS_STA (DTS_ATSF | new_dir, new_fnc));/* state = fnc */
return; 
}

/* Schedule new DECtape function

   This routine is only called if
   - the selected unit is attached
   - the selected unit is at speed (forward or backward)

   This routine
   - updates the selected unit's position
   - updates the selected unit's state
   - schedules the new operation
*/

void dt_newfnc (UNIT *uptr, int32 newsta)
{
int32 fnc, dir, blk, unum, newpos;
#if defined (TC02)
int32 relpos;
#endif
uint32 oldpos;

oldpos = uptr->pos;                                     /* save old pos */
if (dt_setpos (uptr))                                   /* update pos */
    return;
uptr->STATE = newsta;                                   /* update state */
fnc = DTS_GETFNC (uptr->STATE);                         /* set variables */
dir = DTS_GETMOT (uptr->STATE) & DTS_DIR;
unum = (int32) (uptr - dt_dev.units);
if (oldpos == uptr->pos)                                /* bump pos */
    uptr->pos = uptr->pos + (dir? -1: 1);
blk = DT_LIN2BL (uptr->pos, uptr);

if (dir? DT_QREZ (uptr): DT_QFEZ (uptr)) {              /* wrong ez? */
    dt_seterr (uptr, DTB_END);                          /* set ez flag, stop */
    return;
    }
sim_cancel (uptr);                                      /* cancel cur op */
dt_substate = DTO_SOB;                                  /* substate = block start */
switch (fnc) {                                          /* case function */

    case DTS_OFR:                                       /* off reel */
        if (dir)                                        /* rev? < start */
            newpos = -1000;
        else newpos = DTU_FWDEZ (uptr) + DT_EZLIN + 1000;       /* fwd? > end */
        break;

    case FNC_MOVE:                                      /* move */
        dt_schedez (uptr, dir);                         /* sched end zone */
        if (DEBUG_PRI (dt_dev, LOG_MS))
            fprintf (sim_deb, ">>DT%d: moving %s\n",
                     unum, (dir? "backward": "forward"));
        return;                                         /* done */

    case FNC_SRCH:                                      /* search */
        if (dir)
            newpos = DT_BLK2LN ((DT_QFEZ (uptr)?
            DTU_TSIZE (uptr): blk), uptr) - DT_BLKLN - DT_WSIZE;
        else newpos = DT_BLK2LN ((DT_QREZ (uptr)?
            0: blk + 1), uptr) + DT_BLKLN + (DT_WSIZE - 1);
        if (DEBUG_PRI (dt_dev, LOG_MS))
            fprintf (sim_deb, ">>DT%d: searching %s\n", unum,
                     (dir? "backward": "forward"));
        break;

    case FNC_WRIT:                                      /* write */
    case FNC_READ:                                      /* read */
#if defined (TC02)                                      /* TC02/TC15 */
        if (DT_QEZ (uptr)) {                            /* in "ok" end zone? */
            if (dir)
                newpos = DTU_FWDEZ (uptr) - DT_HTLIN - DT_WSIZE;
            else newpos = DT_EZLIN + DT_HTLIN + (DT_WSIZE - 1);
            break;
            }
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            dt_seterr (uptr, DTB_SEL);
            return;
            }
        if (dir)
            newpos = DT_BLK2LN (((relpos >= (DTU_LPERB (uptr) - DT_HTLIN))?
                blk + 1: blk), uptr) - DT_HTLIN - DT_WSIZE;
        else newpos = DT_BLK2LN (((relpos < DT_HTLIN)?
            blk: blk + 1), uptr) + DT_HTLIN + (DT_WSIZE - 1);
        break;
#endif

    case FNC_RALL:                                      /* read all */
    case FNC_WALL:                                      /* write all */
        if (DT_QEZ (uptr)) {                            /* in "ok" end zone? */
            if (dir)
                newpos = DTU_FWDEZ (uptr) - DT_WSIZE;
            else newpos = DT_EZLIN + (DT_WSIZE - 1);
            }
        else {
            newpos = ((uptr->pos) / DT_WSIZE) * DT_WSIZE;
            if (!dir)
                newpos = newpos + (DT_WSIZE - 1);
            }
        if (DEBUG_PRI (dt_dev, LOG_RA) ||
           (DEBUG_PRI (dt_dev, LOG_BL) && (blk == dt_logblk)))
            fprintf (sim_deb, ">>DT%d: read all block %d %s%s\n",
                     unum, blk, (dir? "backward": "forward"),
                    ((dtsa & DTA_MODE)? " continuous]": " "));
        break;

    default:
        dt_seterr (uptr, DTB_SEL);                      /* bad state */
        return;
        }

#if defined (TYPE550)                                   /* Type 550 */
if ((fnc == FNC_WRIT) || (fnc == FNC_WALL)) {           /* write function? */
    dtsb = dtsb | DTB_DTF;                              /* set data flag */
    DT_UPDINT;
    }
#endif

sim_activate (uptr, ABS (newpos - ((int32) uptr->pos)) * dt_ltime);
return;
}

/* Update DECtape position

   DECtape motion is modeled as a constant velocity, with linear
   acceleration and deceleration.  The motion equations are as follows:

        t       =       time since operation started
        tmax    =       time for operation (accel, decel only)
        v       =       at speed velocity in lines (= 1/dt_ltime)

   Then:
        at speed dist = t * v
        accel dist = (t^2 * v) / (2 * tmax)
        decel dist = (((2 * t * tmax) - t^2) * v) / (2 * tmax)

   This routine uses the relative (integer) time, rather than the absolute
   (floating point) time, to allow save and restore of the start times.
*/

t_bool dt_setpos (UNIT *uptr)
{
uint32 new_time, ut, ulin, udelt;
int32 mot = DTS_GETMOT (uptr->STATE);
int32 unum, delta = 0;

new_time = sim_grtime ();                               /* current time */
ut = new_time - uptr->LASTT;                            /* elapsed time */
if (ut == 0)                                            /* no time gone? exit */
    return FALSE;
uptr->LASTT = new_time;                                 /* update last time */
switch (mot & ~DTS_DIR) {                               /* case on motion */

    case DTS_STOP:                                      /* stop */
        delta = 0;
        break;

    case DTS_DECF:                                      /* slowing */
        ulin = ut / (uint32) dt_ltime;
        udelt = dt_dctime / dt_ltime;
        delta = ((ulin * udelt * 2) - (ulin * ulin)) / (2 * udelt);
        break;

    case DTS_ACCF:                                      /* accelerating */
        ulin = ut / (uint32) dt_ltime;
        udelt = (dt_dctime - (dt_dctime >> 2)) / dt_ltime;
        delta = (ulin * ulin) / (2 * udelt);
        break;

    case DTS_ATSF:                                      /* at speed */
        delta = ut / (uint32) dt_ltime;
        break;
        }

if (mot & DTS_DIR)                                      /* update pos */
    uptr->pos = uptr->pos - delta;
else uptr->pos = uptr->pos + delta;
if (((int32) uptr->pos < 0) ||
    ((int32) uptr->pos > (DTU_FWDEZ (uptr) + DT_EZLIN))) {
    detach_unit (uptr);                                 /* off reel? */
    uptr->STATE = uptr->pos = 0;
    unum = (int32) (uptr - dt_dev.units);
    if (unum == DTA_GETUNIT (dtsa))                     /* if selected, */
        dt_seterr (uptr, DTB_SEL);                      /* error */
    return TRUE;
    }
return FALSE;
}

/* Unit service

   Unit must be attached, detach cancels operation
*/

t_stat dt_svc (UNIT *uptr)
{
int32 mot = DTS_GETMOT (uptr->STATE);
int32 dir = mot & DTS_DIR;
int32 fnc = DTS_GETFNC (uptr->STATE);
int32 *fbuf = (int32 *) uptr->filebuf;
int32 unum = uptr - dt_dev.units;
int32 blk, wrd, ma, relpos;
uint32 ba;

/* Motion cases

   Decelerating - if next state != stopped, must be accel reverse
   Accelerating - next state must be @speed, schedule function
   At speed - do functional processing
*/

switch (mot) {

    case DTS_DECF: case DTS_DECR:                       /* decelerating */
        if (dt_setpos (uptr))                           /* upd pos; off reel? */
            return IORETURN (dt_stopoffr, STOP_DTOFF);
        uptr->STATE = DTS_NXTSTA (uptr->STATE);         /* advance state */
        if (uptr->STATE)                                /* not stopped? */
            sim_activate (uptr, dt_dctime - (dt_dctime >> 2));  /* reversing */
        return SCPE_OK;

    case DTS_ACCF: case DTS_ACCR:                       /* accelerating */
        dt_newfnc (uptr, DTS_NXTSTA (uptr->STATE));     /* adv state, sched */
        return SCPE_OK;

    case DTS_ATSF: case DTS_ATSR:                       /* at speed */
        break;                                          /* check function */

    default:                                            /* other */
        dt_seterr (uptr, DTB_SEL);                      /* state error */
        return SCPE_OK;
        }

/* Functional cases

   Move - must be at end zone
   Search - transfer block number, schedule next block
   Off reel - detach unit (it must be deselected)
*/

if (dt_setpos (uptr))                                   /* upd pos; off reel? */
    return IORETURN (dt_stopoffr, STOP_DTOFF);
if (DT_QEZ (uptr)) {                                    /* in end zone? */
    dt_seterr (uptr, DTB_END);                          /* end zone error */
    return SCPE_OK;
    }
blk = DT_LIN2BL (uptr->pos, uptr);                      /* get block # */

switch (fnc) {                                          /* at speed, check fnc */

    case FNC_MOVE:                                      /* move */
        dt_seterr (uptr, DTB_END);                      /* end zone error */
        return SCPE_OK;

    case DTS_OFR:                                       /* off reel */
        detach_unit (uptr);                             /* must be deselected */
        uptr->STATE = uptr->pos = 0;                    /* no visible action */
        break;

/* TC02/TC15 service */
/* Search */

#if defined (TC02)                                      /* TC02/TC15 */

    case FNC_SRCH:                                      /* search */
        if (dtsb & DTB_DTF) {                           /* DTF set? */
            dt_seterr (uptr, DTB_TIM);                  /* timing error */
            return SCPE_OK;
            }
        sim_activate (uptr, DTU_LPERB (uptr) * dt_ltime);/* sched next block */
        M[DT_WC] = (M[DT_WC] + 1) & DMASK;              /* inc WC */
        ma = M[DT_CA] & AMASK;                          /* get mem addr */
        if (MEM_ADDR_OK (ma))                           /* store block # */
            M[ma] = blk;
        if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
                dtsb = dtsb | DTB_DTF;                  /* set DTF */
        if (DEBUG_PRI (dt_dev, LOG_MS))
            fprintf (sim_deb, ">>DT%d: found block %d\n", unum, blk);
        break;

/* Read has four subcases

   Start of block, not wc ovf - check that DTF is clear, otherwise normal
   Normal - increment MA, WC, copy word from tape to memory
        if read dir != write dir, bits must be scrambled
        if wc overflow, next state is wc overflow
        if end of block, possibly set DTF, next state is start of block
   Wc ovf, not start of block - 
        if end of block, possibly set DTF, next state is start of block
   Wc ovf, start of block - if end of block reached, timing error,
        otherwise, continue to next word
*/

    case FNC_READ:                                      /* read */
        wrd = DT_LIN2WD (uptr->pos, uptr);              /* get word # */
        switch (dt_substate) {                          /* case on substate */

        case DTO_SOB:                                   /* start of block */
            if (dtsb & DTB_DTF) {                       /* DTF set? */
                dt_seterr (uptr, DTB_TIM);              /* timing error */
                return SCPE_OK;
                }
            if (DEBUG_PRI (dt_dev, LOG_RW) ||
               (DEBUG_PRI (dt_dev, LOG_BL) && (blk == dt_logblk)))
                fprintf (sim_deb, ">>DT%d: reading block %d %s%s\n",
                         unum, blk, (dir? "backward": "forward"),
                         ((dtsa & DTA_MODE)? " continuous": " "));
            dt_substate = 0;
            /* fall through */
        case 0:                                         /* normal read */
            M[DT_WC] = (M[DT_WC] + 1) & DMASK;          /* incr WC, CA */
            M[DT_CA] = (M[DT_CA] + 1) & DMASK;
            ma = M[DT_CA] & AMASK;                      /* mem addr */
            ba = (blk * DTU_BSIZE (uptr)) + wrd;        /* buffer ptr */
            dtdb = fbuf[ba];                            /* get tape word */
            if (dir)                                    /* rev? comp obv */
                dtdb = dt_comobv (dtdb);
            if (MEM_ADDR_OK (ma))                       /* mem addr legal? */
                M[ma] = dtdb;
            if (M[DT_WC] == 0)                          /* wc ovf? */
                dt_substate = DTO_WCO;
            /* fall through */
        case DTO_WCO:                                   /* wc ovf, not sob */
            if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))  /* not last? */
                sim_activate (uptr, DT_WSIZE * dt_ltime);
            else {
                dt_substate = dt_substate | DTO_SOB;
                sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
                if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
                    dtsb = dtsb | DTB_DTF;              /* set DTF */
                }
            break;                      

        case DTO_WCO | DTO_SOB:                         /* next block */        
            if (wrd == (dir? 0: DTU_BSIZE (uptr)))      /* end of block? */
                dt_seterr (uptr, DTB_TIM);              /* timing error */
            else sim_activate (uptr, DT_WSIZE * dt_ltime);
            break;
            }                                           /* end case subst */
        break;

/* Write has four subcases

   Start of block, not wc ovf - check that DTF is clear, set block direction
   Normal - increment MA, WC, copy word from memory to tape
        if wc overflow, next state is wc overflow
        if end of block, possibly set DTF, next state is start of block
   Wc ovf, not start of block -
        copy 0 to tape
        if end of block, possibly set DTF, next state is start of block
   Wc ovf, start of block - schedule end zone
*/

    case FNC_WRIT:                                      /* write */
        wrd = DT_LIN2WD (uptr->pos, uptr);              /* get word # */
        switch (dt_substate) {                          /* case on substate */

        case DTO_SOB:                                   /* start block */
            if (dtsb & DTB_DTF) {                       /* DTF set? */
                dt_seterr (uptr, DTB_TIM);              /* timing error */
                return SCPE_OK;
                }
            if (DEBUG_PRI (dt_dev, LOG_RW) ||
               (DEBUG_PRI (dt_dev, LOG_BL) && (blk == dt_logblk)))
                fprintf (sim_deb, ">>DT%d: writing block %d %s%s\n", unum, blk,
                         (dir? "backward": "forward"),
                         ((dtsa & DTA_MODE)? " continuous": " "));
            dt_substate = 0;
            /* fall through */
        case 0:                                         /* normal write */
            M[DT_WC] = (M[DT_WC] + 1) & DMASK;          /* incr WC, CA */
            M[DT_CA] = (M[DT_CA] + 1) & DMASK;
            /* fall through */
        case DTO_WCO:                                   /* wc ovflo */
            ma = M[DT_CA] & AMASK;                      /* mem addr */
            ba = (blk * DTU_BSIZE (uptr)) + wrd;        /* buffer ptr */
            dtdb = dt_substate? 0: M[ma];               /* get word */
            if (dir)                                    /* rev? comp obv */
                dtdb = dt_comobv (dtdb);
            fbuf[ba] = dtdb;                            /* write word */
            uptr->WRITTEN = TRUE;
            if (ba >= uptr->hwmark)
                uptr->hwmark = ba + 1;
            if (M[DT_WC] == 0)
                dt_substate = DTO_WCO;
            if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))  /* not last? */
                sim_activate (uptr, DT_WSIZE * dt_ltime);
            else {
                dt_substate = dt_substate | DTO_SOB;
                sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
                if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
                    dtsb = dtsb | DTB_DTF;              /* set DTF */
                }
            break;                      

        case DTO_WCO | DTO_SOB:                         /* all done */
            dt_schedez (uptr, dir);                     /* sched end zone */
            break;
            }                                           /* end case subst */
        break;

/* Read all has two subcases

        Not word count overflow - increment MA, WC, copy word from tape to memory
        Word count overflow - schedule end zone
*/

    case FNC_RALL:                                      /* read all */
        switch (dt_substate) {                          /* case on substate */

        case 0: case DTO_SOB:                           /* read in progress */
            if (dtsb & DTB_DTF) {                       /* DTF set? */
                dt_seterr (uptr, DTB_TIM);              /* timing error */
                return SCPE_OK;
                }
            relpos = DT_LIN2OF (uptr->pos, uptr);       /* cur pos in blk */
            M[DT_WC] = (M[DT_WC] + 1) & DMASK;          /* incr WC, CA */
            M[DT_CA] = (M[DT_CA] + 1) & DMASK;
            ma = M[DT_CA] & AMASK;                      /* mem addr */
            if ((relpos >= DT_HTLIN) &&                 /* in data zone? */
                (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
                wrd = DT_LIN2WD (uptr->pos, uptr);
                ba = (blk * DTU_BSIZE (uptr)) + wrd;
                dtdb = fbuf[ba];                        /* get tape word */
                }
            else dtdb = dt_gethdr (uptr, blk, relpos);  /* get hdr */
            if (dir)                                    /* rev? comp obv */
                dtdb = dt_comobv (dtdb);
            sim_activate (uptr, DT_WSIZE * dt_ltime);
            if (MEM_ADDR_OK (ma))                       /* mem addr legal? */
                M[ma] = dtdb;
            if (M[DT_WC] == 0)
                dt_substate = DTO_WCO;
            if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
                dtsb = dtsb | DTB_DTF;                  /* set DTF */
            break;

        case DTO_WCO: case DTO_WCO | DTO_SOB:           /* all done */
            dt_schedez (uptr, dir);                     /* sched end zone */
            break;
            }                                           /* end case substate */
        break;

/* Write all has two subcases

        Not word count overflow - increment MA, WC, copy word from memory to tape
        Word count overflow - schedule end zone
*/

    case FNC_WALL:                                      /* write all */
        switch (dt_substate) {                          /* case on substate */

        case 0: case DTO_SOB:                           /* read in progress */
            if (dtsb & DTB_DTF) {                       /* DTF set? */
                dt_seterr (uptr, DTB_TIM);              /* timing error */
                return SCPE_OK;
                }
            relpos = DT_LIN2OF (uptr->pos, uptr);       /* cur pos in blk */
            M[DT_WC] = (M[DT_WC] + 1) & DMASK;          /* incr WC, CA */
            M[DT_CA] = (M[DT_CA] + 1) & DMASK;
            ma = M[DT_CA] & AMASK;                      /* mem addr */
            if ((relpos >= DT_HTLIN) &&                 /* in data zone? */
                (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
                dtdb = M[ma];                           /* get mem word */
                if (dir)
                    dtdb = dt_comobv (dtdb);
                wrd = DT_LIN2WD (uptr->pos, uptr);
                ba = (blk * DTU_BSIZE (uptr)) + wrd;
                fbuf[ba] = dtdb;                        /* write word */
                if (ba >= uptr->hwmark)
                    uptr->hwmark = ba + 1;
                }
                                                        /* ignore hdr */
            sim_activate (uptr, DT_WSIZE * dt_ltime);
            if (M[DT_WC] == 0)
                dt_substate = DTO_WCO;
            if (((dtsa & DTA_MODE) == 0) || (M[DT_WC] == 0))
                dtsb = dtsb | DTB_DTF;                  /* set DTF */
            break;

        case DTO_WCO: case DTO_WCO | DTO_SOB:           /* all done */
            dt_schedez (uptr, dir);                     /* sched end zone */
            break;
            }                                           /* end case substate */
        break;

/* Type 550 service */
/* Search */

#else                                                   /* Type 550 */
    case FNC_SRCH:                                      /* search */
        if (dtsb & DTB_DTF) {                           /* DTF set? */
            dt_seterr (uptr, DTB_TIM);                  /* timing error */
            return SCPE_OK;
            }
        sim_activate (uptr, DTU_LPERB (uptr) * dt_ltime);/* sched next block */
        dtdb = blk;                                     /* store block # */
        dtsb = dtsb | DTB_DTF;                          /* set DTF */
        if (DEBUG_PRI (dt_dev, LOG_MS))
            fprintf (sim_deb, ">>DT%d: search found block %d\n", unum, blk);
        break;

/* Read and read all */

    case FNC_READ: case FNC_RALL:
        if (dtsb & DTB_DTF) {                           /* DTF set? */
            dt_seterr (uptr, DTB_TIM);                  /* timing error */
            return SCPE_OK;
            }
        sim_activate (uptr, DT_WSIZE * dt_ltime);       /* sched next word */
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            wrd = DT_LIN2WD (uptr->pos, uptr);
            ba = (blk * DTU_BSIZE (uptr)) + wrd;
            dtdb = fbuf[ba];                            /* get tape word */
            dtsb = dtsb | DTB_DTF;                      /* set flag */
            }
        else {
            ma = (2 * DT_HTWRD) + DTU_BSIZE (uptr) - DT_CSMWD - 1;
            wrd = relpos / DT_WSIZE;                    /* hdr start = wd 0 */
#if defined (OLD_TYPE550)
            if ((wrd == 0) ||                           /* skip 1st, last */
                (wrd == ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - 1))) break;
#endif
            if ((fnc == FNC_READ) &&                    /* read, skip if not */
                (wrd != DT_CSMWD) &&                    /* fwd, rev cksum */
                (wrd != ma))
                break;
            dtdb = dt_gethdr (uptr, blk, relpos);
            if (wrd == (dir? DT_CSMWD: ma))             /* at end csum? */
                dtsb = dtsb | DTB_BEF;                  /* end block */
            else dtsb = dtsb | DTB_DTF;                 /* else next word */
            }
        if (dir)
            dtdb = dt_comobv (dtdb);
        break;

/* Write and write all */

    case FNC_WRIT: case FNC_WALL:
        if (dtsb & DTB_DTF) {                           /* DTF set? */
            dt_seterr (uptr, DTB_TIM);                  /* timing error */
            return SCPE_OK;
            }
        sim_activate (uptr, DT_WSIZE * dt_ltime);       /* sched next word */
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            wrd = DT_LIN2WD (uptr->pos, uptr);
            ba = (blk * DTU_BSIZE (uptr)) + wrd;
            if (dir)                                    /* get data word */
                fbuf[ba] = dt_comobv (dtdb);
            else fbuf[ba] = dtdb;
            if (ba >= uptr->hwmark)
                uptr->hwmark = ba + 1;
            if (wrd == (dir? 0: DTU_BSIZE (uptr) - 1))
                dtsb = dtsb | DTB_BEF;                  /* end block */
            else dtsb = dtsb | DTB_DTF;                 /* else next word */
            }
        else {
            wrd = relpos / DT_WSIZE;                    /* hdr start = wd 0 */
#if defined (OLD_TYPE550)
            if ((wrd == 0) ||                           /* skip 1st, last */
                (wrd == ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - 1))) break;
#endif
            if ((fnc == FNC_WRIT) &&                    /* wr, skip if !csm */
                (wrd != ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - DT_CSMWD - 1)))
                break;
            dtsb = dtsb | DTB_DTF;                      /* set flag */
            }
        break;
#endif

    default:
        dt_seterr (uptr, DTB_SEL);                      /* impossible state */
        break;
        }                                               /* end case function */

DT_UPDINT;                                              /* update interrupts */
return SCPE_OK;
}

/* Utility routines */

/* Set error flag */

void dt_seterr (UNIT *uptr, int32 e)
{
dtsa = dtsa & ~DTA_STSTP;                               /* clear go */
dtsb = dtsb | DTB_ERF | e;                              /* set error flag */
if (uptr != NULL) {                                     /* valid select? */
    int32 mot = DTS_GETMOT (uptr->STATE);               /* get motion */
    if (mot >= DTS_ACCF) {                              /* ~stopped or stopping? */
        sim_cancel (uptr);                              /* cancel activity */
        if (dt_setpos (uptr))                           /* update position */
            return;
        sim_activate (uptr, dt_dctime);                 /* sched decel */
        DTS_SETSTA (DTS_DECF | (mot & DTS_DIR), 0);     /* state = decel */
        }
    else DTS_SETSTA (mot, 0);                           /* clear 2nd, 3rd */
    }
DT_UPDINT;
return;
}

/* Schedule end zone */

void dt_schedez (UNIT *uptr, int32 dir)
{
int32 newpos;

if (dir)                                                /* rev? rev ez */
    newpos = DT_EZLIN - DT_WSIZE;
else newpos = DTU_FWDEZ (uptr) + DT_WSIZE;              /* fwd? fwd ez */
sim_activate (uptr, ABS (newpos - ((int32) uptr->pos)) * dt_ltime);
return;
}

/* Complement obverse routine */

int32 dt_comobv (int32 dat)
{
dat = dat ^ DMASK;                                      /* compl obverse */
dat = ((dat >> 15) & 07) | ((dat >> 9) & 070) |
    ((dat >> 3) & 0700) | ((dat & 0700) << 3) |
    ((dat & 070) << 9) | ((dat & 07) << 15);
return dat;
}

/* Checksum routine */

int32 dt_csum (UNIT *uptr, int32 blk)
{
int32 *fbuf = (int32 *) uptr->filebuf;
int32 ba = blk * DTU_BSIZE (uptr);
int32 i, csum, wrd;

#if defined (TC02)                                      /* TC02/TC15 */
csum = 077;                                             /* init csum */
for (i = 0; i < DTU_BSIZE (uptr); i++) {                /* loop thru buf */
    wrd = fbuf[ba + i] ^ DMASK;                         /* get ~word */
    csum = csum ^ (wrd >> 12) ^ (wrd >> 6) ^ wrd;
    }
return (csum & 077);
#else                                                   /* Type 550 */
csum = 0777777;
for (i = 0; i < DTU_BSIZE (uptr); i++) {                /* loop thru buf */
    wrd = fbuf[ba + i];                                 /* get word */
    csum = csum + wrd;                                  /* 1's comp add */
    if (csum > DMASK)
        csum = (csum + 1) & DMASK;
    }
return (csum ^ DMASK);                                  /* 1's comp res */
#endif
}

/* Get header word */

int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos)
{
int32 wrd = relpos / DT_WSIZE;

if (wrd == DT_BLKWD)                                    /* fwd blknum */
    return blk;
#if defined (TC02)                                      /* TC02/TC15 */
if (wrd == DT_CSMWD)                                    /* rev csum */
    return 077;
if (wrd == ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - DT_CSMWD - 1))  /* fwd csum */
    return (dt_csum (uptr, blk) << 12);
#else                                                   /* Type 550 */
if (wrd == DT_CSMWD)                                    /* rev csum */
    return 0777777;
if (wrd == ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - DT_CSMWD - 1))  /* fwd csum */
    return (dt_csum (uptr, blk));
#endif
if (wrd == ((2 * DT_HTWRD) + DTU_BSIZE (uptr) - DT_BLKWD - 1))  /* rev blkno */
    return dt_comobv (blk);
return 0;                                               /* all others */
}  

/* Reset routine */

t_stat dt_reset (DEVICE *dptr)
{
int32 i, prev_mot;
UNIT *uptr;

for (i = 0; i < DT_NUMDR; i++) {                        /* stop all drives */
    uptr = dt_dev.units + i;
    if (sim_is_running) {                               /* CAF? */
        prev_mot = DTS_GETMOT (uptr->STATE);            /* get motion */
        if ((prev_mot & ~DTS_DIR) > DTS_DECF) {         /* accel or spd? */
            if (dt_setpos (uptr))                       /* update pos */
                continue;
            sim_cancel (uptr);
            sim_activate (uptr, dt_dctime);             /* sched decel */
            DTS_SETSTA (DTS_DECF | (prev_mot & DTS_DIR), 0);
            }
        }
    else {
        sim_cancel (uptr);                              /* sim reset */
        uptr->STATE = 0;  
        uptr->LASTT = sim_grtime ();
        }
    }
dtsa = dtsb = 0;                                        /* clear status */
DT_UPDINT;                                              /* reset interrupt */
return SCPE_OK;
}

/* IORS routine */

int32 dt_iors (void)
{
#if defined IOS_DTA
return ((dtsb & (DTB_ERF | DTB_DTF))? IOS_DTA: 0);
#else
return 0;
#endif
}

/* Attach routine

   Determine 12b, 16b, or 18b/36b format
   Allocate buffer
   If 12b, read 12b format and convert to 18b in buffer
   If 16b, read 16b format and convert to 18b in buffer
   If 18b/36b, read data into buffer
*/

t_stat dt_attach (UNIT *uptr, CONST char *cptr)
{
uint16 pdp8b[D8_NBSIZE];
uint16 pdp11b[D18_BSIZE];
uint32 ba, sz, k, *fbuf;
int32 u = uptr - dt_dev.units;
t_stat r;

r = attach_unit (uptr, cptr);                           /* attach */
if (r != SCPE_OK)                                       /* error? */
    return r;
if ((sim_switches & SIM_SW_REST) == 0) {                /* not from rest? */
    uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);      /* default 18b */
    if (sim_switches & SWMASK ('T'))                    /* att 12b? */
        uptr->flags = uptr->flags | UNIT_8FMT;
    else if (sim_switches & SWMASK ('S'))               /* att 16b? */
        uptr->flags = uptr->flags | UNIT_11FMT;
    else if (!(sim_switches & SWMASK ('A')) &&          /* autosize? */
        (sz = sim_fsize (uptr->fileref))) {
        if (sz == D8_FILSIZ)
            uptr->flags = uptr->flags | UNIT_8FMT;
        else if (sz == D11_FILSIZ)
            uptr->flags = uptr->flags | UNIT_11FMT;
        }
    }
uptr->capac = DTU_CAPAC (uptr);                         /* set capacity */
uptr->filebuf = calloc (uptr->capac, sizeof (uint32));
if (uptr->filebuf == NULL) {                            /* can't alloc? */
    detach_unit (uptr);
    return SCPE_MEM;
    }
fbuf = (uint32 *) uptr->filebuf;                        /* file buffer */
sim_printf ("%s%d: ", sim_dname (&dt_dev), u);
if (uptr->flags & UNIT_8FMT)
    sim_printf ("12b format");
else if (uptr->flags & UNIT_11FMT)
    sim_printf ("16b format");
else sim_printf ("18b/36b format");
sim_printf (", buffering file in memory\n");
uptr->io_flush = dt_flush;
if (uptr->flags & UNIT_8FMT) {                          /* 12b? */
    for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
        k = fxread (pdp8b, sizeof (uint16), D8_NBSIZE, uptr->fileref);
        if (k == 0)
            break;
        for ( ; k < D8_NBSIZE; k++)
            pdp8b[k] = 0;
        for (k = 0; k < D8_NBSIZE; k = k + 3) {         /* loop thru blk */
            fbuf[ba] = ((uint32) (pdp8b[k] & 07777) << 6) |
                ((uint32) (pdp8b[k + 1] >> 6) & 077);
            fbuf[ba + 1] = ((uint32) (pdp8b[k + 1] & 077) << 12) |
                ((uint32) pdp8b[k + 2] & 07777);
            ba = ba + 2;                                /* end blk loop */
            }
        }                                               /* end file loop */
    uptr->hwmark = ba;
    }                                                   /* end if */
else if (uptr->flags & UNIT_11FMT) {                    /* 16b? */
    for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
        k = fxread (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
        if (k == 0)
            break;
        for ( ; k < D18_BSIZE; k++)
            pdp11b[k] = 0;
        for (k = 0; k < D18_BSIZE; k++)
            fbuf[ba++] = pdp11b[k];
            }
    uptr->hwmark = ba;
    }                                                   /* end elif */
else uptr->hwmark = fxread (uptr->filebuf, sizeof (uint32),
    uptr->capac, uptr->fileref);
uptr->flags = uptr->flags | UNIT_BUF;                   /* set buf flag */
uptr->pos = DT_EZLIN;                                   /* beyond leader */
uptr->LASTT = sim_grtime ();                            /* last pos update */
return SCPE_OK;
}

/* Detach routine

   Cancel in progress operation
   If 12b, convert 18b buffer to 12b and write to file
   If 16b, convert 18b buffer to 16b and write to file
   If 18b/36b, write buffer to file
   Deallocate buffer
*/

void dt_flush (UNIT* uptr)
{
uint16 pdp8b[D8_NBSIZE];
uint16 pdp11b[D18_BSIZE];
int32 k;
uint32 ba, *fbuf;

if (uptr->WRITTEN && uptr->hwmark && ((uptr->flags & UNIT_RO)== 0)) {    /* any data? */
    sim_printf ("%s: writing buffer to file: %s\n", sim_uname (uptr), uptr->filename);
    rewind (uptr->fileref);                             /* start of file */
    fbuf = (uint32 *) uptr->filebuf;                    /* file buffer */
    if (uptr->flags & UNIT_8FMT) {                      /* 12b? */
        for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
            for (k = 0; k < D8_NBSIZE; k = k + 3) {     /* loop blk */
                pdp8b[k] = (fbuf[ba] >> 6) & 07777;
                pdp8b[k + 1] = ((fbuf[ba] & 077) << 6) |
                    ((fbuf[ba + 1] >> 12) & 077);
                pdp8b[k + 2] = fbuf[ba + 1] & 07777;
                ba = ba + 2;
                }                                       /* end loop blk */
            fxwrite (pdp8b, sizeof (uint16), D8_NBSIZE, uptr->fileref);
            if (ferror (uptr->fileref))
                break;
            }                                           /* end loop file */
        }                                               /* end if 12b */
    else
        if (uptr->flags & UNIT_11FMT) {                 /* 16b? */
            for (ba = 0; ba < uptr->hwmark; ) {         /* loop thru file */
                for (k = 0; k < D18_BSIZE; k++)         /* loop blk */
                    pdp11b[k] = fbuf[ba++] & 0177777;
                fxwrite (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
                if (ferror (uptr->fileref))
                    break;
                }                                       /* end loop file */
            }                                           /* end if 16b */
        else 
            fxwrite (uptr->filebuf, sizeof (uint32),       /* write file */
                     uptr->hwmark, uptr->fileref);
    if (ferror (uptr->fileref))
        sim_perror ("I/O error");
    }
uptr->WRITTEN = FALSE;                                  /* no longer dirty */
}

t_stat dt_detach (UNIT* uptr)
{
int32 u = uptr - dt_dev.units;

if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
if (sim_is_active (uptr)) {
    sim_cancel (uptr);
    if ((u == DTA_GETUNIT (dtsa)) && (dtsa & DTA_STSTP)) {
        dtsb = dtsb | DTB_ERF | DTB_SEL | DTB_DTF;
        DT_UPDINT;
        }
    uptr->STATE = uptr->pos = 0;
    }
if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0))     /* any data? */
    dt_flush (uptr);                                    /* end if hwmark */
free (uptr->filebuf);                                   /* release buf */
uptr->flags = uptr->flags & ~UNIT_BUF;                  /* clear buf flag */
uptr->filebuf = NULL;                                   /* clear buf ptr */
uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);  /* default fmt */
uptr->capac = DT_CAPAC;                                 /* default size */
return detach_unit (uptr);
}
