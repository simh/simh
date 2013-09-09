/* pdp11_tc.c: PDP-11 DECtape simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   tc           TC11/TU56 DECtape

   23-Jun-06    RMS     Fixed switch conflict in ATTACH
   10-Feb-06    RMS     READ sets extended data bits in TCST (Alan Frisbie)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Jul-05    RMS     Removed extraneous externs
   30-Sep-04    RMS     Revised Unibus interface
   25-Jan-04    RMS     Revised for device debug support
   09-Jan-04    RMS     Changed sim_fsize calling sequence, added STOP_OFFR
   29-Dec-03    RMS     Changed initial status to disabled (in Qbus system)
   18-Oct-03    RMS     Fixed reverse checksum in read all
                        Added DECtape off reel message
                        Simplified timing
   25-Apr-03    RMS     Revised for extended file support
   14-Mar-03    RMS     Fixed variable size interaction with save/restore
   29-Sep-02    RMS     Added variable address support to bootstrap
                        Added vector change/display support
                        Added 16b format support
                        New data structures
   30-May-02    RMS     Widened POS to 32b
   26-Jan-02    RMS     Revised bootstrap to conform to M9312
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Converted POS, STATT, LASTT to arrays
   09-Nov-01    RMS     Added bus map support
   15-Sep-01    RMS     Integrated debug logging
   27-Sep-01    RMS     Fixed interrupt after stop for RSTS/E
   07-Sep-01    RMS     Revised device disable and interrupt mechanisms
   29-Aug-01    RMS     Added casts to PDP-8 unpack routine
   17-Jul-01    RMS     Moved function prototype
   11-May-01    RMS     Fixed bug in reset
   26-Apr-01    RMS     Added device enable/disable support
   18-Apr-01    RMS     Changed to rewind tape before boot
   16-Mar-01    RMS     Fixed bug in interrupt after stop
   15-Mar-01    RMS     Added 129th word to PDP-8 format

   PDP-11 DECtapes are represented in memory by fixed length buffer of 32b words.
   Three file formats are supported:

        18b/36b                 256 words per block [256 x 18b]
        16b                     256 words per block [256 x 16b]
        12b                     129 words per block [129 x 12b]

   When a 16b or 12b DECtape file is read in, it is converted to 18b/36b format.

   DECtape motion is measured in 3b lines.  Time between lines is 33.33us.
   Tape density is nominally 300 lines per inch.  The format of a DECtape (as
   taken from the TD8E formatter) is:

        reverse end zone        8192 reverse end zone codes ~ 10 feet
        reverse buffer          200 interblock codes
        block 0
         :
        block n
        forward buffer          200 interblock codes
        forward end zone        8192 forward end zone codes ~ 10 feet

   A block consists of five 18b header words, a tape-specific number of data
   words, and five 18b trailer words.  All systems except the PDP-8 use a
   standard block length of 256 words; the PDP-8 uses a standard block length
   of 86 words (x 18b = 129 words x 12b).

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
*/

#include "pdp11_defs.h"

#define DT_NUMDR        8                               /* #drives */
#define DT_M_NUMDR      (DT_NUMDR - 1)
#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_8FMT     (UNIT_V_UF + 1)                 /* 12b format */
#define UNIT_V_11FMT    (UNIT_V_UF + 2)                 /* 16b format */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_8FMT       (1 << UNIT_V_8FMT)
#define UNIT_11FMT      (1 << UNIT_V_11FMT)
#define STATE           u3                              /* unit state */
#define LASTT           u4                              /* last time update */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

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
#define D16_FILSIZ      (D18_TSIZE * D18_BSIZE * sizeof (int16))

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

/* TCST - 177340 - status register */

#define STA_END         0100000                         /* end zone */
#define STA_PAR         0040000                         /* parity err */
#define STA_MRK         0020000                         /* mark trk err */
#define STA_ILO         0010000                         /* illegal op */
#define STA_SEL         0004000                         /* select err */
#define STA_BLKM        0002000                         /* block miss err */
#define STA_DATM        0001000                         /* data miss err */
#define STA_NXM         0000400                         /* nx mem err */
#define STA_UPS         0000200                         /* up to speed */
#define STA_V_XD        0                               /* extended data */
#define STA_M_XD        03
#define STA_ALLERR      (STA_END | STA_PAR | STA_MRK | STA_ILO | \
                         STA_SEL | STA_BLKM | STA_DATM | STA_NXM )
#define STA_RWERR       (STA_END | STA_PAR | STA_MRK | \
                         STA_BLKM | STA_DATM | STA_NXM )
#define STA_RW          0000003
#define STA_GETXD(x)    (((x) >> STA_V_XD) & STA_M_XD)

/* TCCM - 177342 - command register */

/* #define CSR_ERR      0100000 */
#define CSR_MNT         0020000                         /* maint (unimpl) */
#define CSR_INH         0010000                         /* delay inhibit */
#define CSR_DIR         0004000                         /* reverse */
#define CSR_V_UNIT      8                               /* unit select */
#define CSR_M_UNIT      07
#define CSR_UNIT        (CSR_M_UNIT << CSR_V_UNIT)
/* #define CSR_DONE     0000200 */
/* #define CSR_IE       0000100 */
#define CSR_V_MEX       4                               /* mem extension */
#define CSR_M_MEX       03
#define CSR_MEX         (CSR_M_MEX << CSR_V_MEX)
#define CSR_V_FNC       1                               /* function */
#define CSR_M_FNC       07
#define  FNC_STOP        00                             /* stop all */
#define  FNC_SRCH        01                             /* search */
#define  FNC_READ        02                             /* read */
#define  FNC_RALL        03                             /* read all */
#define  FNC_SSEL        04                             /* stop selected */
#define  FNC_WMRK        05                             /* write */
#define  FNC_WRIT        06                             /* write all */
#define  FNC_WALL        07                             /* write timing */
/* define CSR_GO        0000001 */
#define CSR_RW          0117576                         /* read/write */

#define CSR_GETUNIT(x)  (((x) >> CSR_V_UNIT) & CSR_M_UNIT)
#define CSR_GETMEX(x)   (((x) >> CSR_V_MEX) & CSR_M_MEX)
#define CSR_GETFNC(x)   (((x) >> CSR_V_FNC) & CSR_M_FNC)
#define CSR_INCMEX(x)   (((x) & ~CSR_MEX) | (((x) + (1 << CSR_V_MEX)) & CSR_MEX))

/* TCWC - 177344 - word count */

/* TCBA - 177346 - bus address */

/* TCDT - 177350 - data */

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
#define  DTS_OFR        FNC_WMRK                        /* "off reel" */
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

/* Logging */

#define LOG_MS          0x1
#define LOG_RW          0x2
#define LOG_BL          0x4

#define DT_SETDONE      tccm = tccm | CSR_DONE; \
                        if (tccm & CSR_IE) \
                            SET_INT (DTA)
#define DT_CLRDONE      tccm = tccm & ~CSR_DONE; \
                        CLR_INT (DTA)
#define ABS(x)          (((x) < 0)? (-(x)): (x))

extern uint16 *M;                                       /* memory */
extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;

int32 tcst = 0;                                         /* status */
int32 tccm = 0;                                         /* command */
int32 tcwc = 0;                                         /* word count */
int32 tcba = 0;                                         /* bus address */
int32 tcdt = 0;                                         /* data */
int32 dt_ctime = 100;                                   /* fast cmd time */
int32 dt_ltime = 12;                                    /* interline time */
int32 dt_dctime = 40000;                                /* decel time */
int32 dt_substate = 0;
int32 dt_logblk = 0;
int32 dt_stopoffr = 0;

DEVICE dt_dev;
t_stat dt_rd (int32 *data, int32 PA, int32 access);
t_stat dt_wr (int32 data, int32 PA, int32 access);
t_stat dt_svc (UNIT *uptr);
t_stat dt_svcdone (UNIT *uptr);
t_stat dt_reset (DEVICE *dptr);
t_stat dt_attach (UNIT *uptr, char *cptr);
t_stat dt_detach (UNIT *uptr);
t_stat dt_boot (int32 unitno, DEVICE *dptr);
void dt_deselect (int32 oldf);
void dt_newsa (int32 newf);
void dt_newfnc (UNIT *uptr, int32 newsta);
t_bool dt_setpos (UNIT *uptr);
void dt_schedez (UNIT *uptr, int32 dir);
void dt_seterr (UNIT *uptr, int32 e);
void dt_stopunit (UNIT *uptr);
int32 dt_comobv (int32 val);
int32 dt_csum (UNIT *uptr, int32 blk);
int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos);
t_stat dt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *dt_description (DEVICE *dptr);

/* DT data structures

   dt_dev       DT device descriptor
   dt_unit      DT unit list
   dt_reg       DT register list
   dt_mod       DT modifier list
*/

#define IOLN_TC         012

DIB dt_dib = {
    IOBA_AUTO, IOLN_TC, &dt_rd, &dt_wr,
    1, IVCL (DTA), VEC_AUTO, { NULL }, IOLN_TC,
    };

UNIT dt_unit[] = {
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
             UNIT_ROABLE+UNIT_11FMT, DT_CAPAC) },
    { UDATA (&dt_svcdone, UNIT_DIS, 0) }
    };

#define DT_TIMER        (DT_NUMDR)

REG dt_reg[] = {
    { ORDATAD (TCST, tcst, 16, "status register") },
    { ORDATAD (TCCM, tccm, 16, "command register") },
    { ORDATAD (TCWC, tcwc, 16, "word count register") },
    { ORDATAD (TCBA, tcba, 16, "bus address register") },
    { ORDATAD (TCDT, tcdt, 16, "data register") },
    { FLDATAD (INT, IREQ (DTA), INT_V_DTA, "interrupt pending flag") },
    { FLDATAD (ERR, tccm, CSR_V_ERR, "error flag") },
    { FLDATAD (DONE, tccm, CSR_V_DONE, "done flag") },
    { FLDATAD (IE, tccm, CSR_V_DONE, "interrupt enable flag") },
    { DRDATAD (CTIME, dt_ctime, 31, "time to complete transport stop"), REG_NZ },
    { DRDATAD (LTIME, dt_ltime, 31, "time between lines"), REG_NZ },
    { DRDATAD (DCTIME, dt_dctime, 31, "time to decelerate to a full stop"), REG_NZ },
    { ORDATAD (SUBSTATE, dt_substate, 1, "read/write command substate") },
    { DRDATA (LBLK, dt_logblk, 12), REG_HIDDEN },
    { URDATAD (POS, dt_unit[0].pos, 10, T_ADDR_W, 0,
              DT_NUMDR, PV_LEFT | REG_RO, "position, in lines, units 0 to 7") },
    { URDATAD (STATT, dt_unit[0].STATE, 8, 18, 0,
              DT_NUMDR, REG_RO, "unit state, units 0 to 7") },
    { URDATA (LASTT, dt_unit[0].LASTT, 10, 32, 0,
              DT_NUMDR, REG_HRO) },
    { FLDATAD (STOP_OFFR, dt_stopoffr, 0, "stop on off-reel error") },
    { ORDATA (DEVADDR, dt_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, dt_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB dt_mod[] = {
    { UNIT_WLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable tape drive" },
    { UNIT_WLK, UNIT_WLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock tape drive"  },
    { UNIT_8FMT + UNIT_11FMT,          0, "18b", NULL },
    { UNIT_8FMT + UNIT_11FMT,  UNIT_8FMT, "12b", NULL },
    { UNIT_8FMT + UNIT_11FMT, UNIT_11FMT, "16b", NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEBTAB dt_deb[] = {
    { "MOTION", LOG_MS },
    { "DATA", LOG_RW },
    { "BLOCK", LOG_BL },
    { NULL, 0 }
    };

DEVICE dt_dev = {
    "TC", dt_unit, dt_reg, dt_mod,
    DT_NUMDR + 1, 8, 24, 1, 8, 18,
    NULL, NULL, &dt_reset,
    &dt_boot, &dt_attach, &dt_detach,
    &dt_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0,
    dt_deb, NULL, NULL, &dt_help, NULL, NULL,
    &dt_description
    };

/* IO dispatch routines, I/O addresses 17777340 - 17777350 */

t_stat dt_rd (int32 *data, int32 PA, int32 access)
{
int32 j, unum, mot, fnc;

j = (PA >> 1) & 017;                                    /* get reg offset */
unum = CSR_GETUNIT (tccm);                              /* get drive */
switch (j) {

    case 000:                                           /* TCST */
        mot = DTS_GETMOT (dt_unit[unum].STATE);         /* get motion */
        if (mot >= DTS_ATSF)                            /* set/clr speed */
            tcst = tcst | STA_UPS;
        else tcst = tcst & ~STA_UPS;
        *data = tcst;
        break;

    case 001:                                           /* TCCM */
        if (tcst & STA_ALLERR)                          /* set/clr error */
            tccm = tccm | CSR_ERR;
        else tccm = tccm & ~CSR_ERR;
        *data = tccm;
        break;

    case 002:                                           /* TCWC */
        *data = tcwc;
        break;

    case 003:                                           /* TCBA */
        *data = tcba;
        break;

    case 004:                                           /* TCDT */
        fnc = DTS_GETFNC (dt_unit[unum].STATE);         /* get function */
        if (fnc == FNC_RALL) {                          /* read all? */
            DT_CLRDONE;                                 /* clear done */
            }
        *data = tcdt;
        break;
        }

return SCPE_OK;
}

t_stat dt_wr (int32 data, int32 PA, int32 access)
{
int32 i, j, unum, old_tccm, fnc;
UNIT *uptr;

j = (PA >> 1) & 017;                                    /* get reg offset */
switch (j) {

    case 000:                                           /* TCST */
        if ((access == WRITEB) && (PA & 1))
            break;
        tcst = (tcst & ~STA_RW) | (data & STA_RW);
        break;

    case 001:                                           /* TCCM */
        old_tccm = tccm;                                /* save prior */
        if (access == WRITEB)
            data = (PA & 1)? (tccm & 0377) | (data << 8): (tccm & ~0377) | data;
        if ((data & CSR_IE) == 0)
            CLR_INT (DTA);
        else if ((((tccm & CSR_IE) == 0) && (tccm & CSR_DONE)) ||
            (data & CSR_DONE)) SET_INT (DTA);
        tccm = (tccm & ~CSR_RW) | (data & CSR_RW);
        if ((data & CSR_GO) && (tccm & CSR_DONE)) {     /* new cmd? */
            tcst = tcst & ~STA_ALLERR;                  /* clear errors */
            tccm = tccm & ~(CSR_ERR | CSR_DONE);        /* clear done, err */
            CLR_INT (DTA);                              /* clear int */
            if ((old_tccm ^ tccm) & CSR_UNIT)
                dt_deselect (old_tccm);
            unum = CSR_GETUNIT (tccm);                  /* get drive */
            fnc = CSR_GETFNC (tccm);                    /* get function */
            if (fnc == FNC_STOP) {                      /* stop all? */
                sim_activate (&dt_dev.units[DT_TIMER], dt_ctime);
                for (i = 0; i < DT_NUMDR; i++)
                    dt_stopunit (dt_dev.units + i);     /* stop unit */
                break;
                }
            uptr = dt_dev.units + unum;
            if (uptr->flags & UNIT_DIS)                 /* disabled? */
                dt_seterr (uptr, STA_SEL);              /* select err */
            if ((fnc == FNC_WMRK) ||                    /* write mark? */
                ((fnc == FNC_WALL) && (uptr->flags & UNIT_WPRT)) ||
                ((fnc == FNC_WRIT) && (uptr->flags & UNIT_WPRT)))
                dt_seterr (uptr, STA_ILO);              /* illegal op */
            if (!(tccm & CSR_ERR))
                dt_newsa (tccm);
            }
        else if ((tccm & CSR_ERR) == 0) {               /* clear err? */
            tcst = tcst & ~STA_RWERR;
            if (tcst & STA_ALLERR)
                tccm = tccm | CSR_ERR;
            }
        break;

    case 002:                                           /* TCWC */
        tcwc = data;                                    /* word write only! */
        break;

    case 003:                                           /* TCBA */
        tcba = data;                                    /* word write only! */
        break;          

    case 004:                                           /* TCDT */
        unum = CSR_GETUNIT (tccm);                      /* get drive */
        fnc = DTS_GETFNC (dt_unit[unum].STATE);         /* get function */
        if (fnc == FNC_WALL) {                          /* write all? */
            DT_CLRDONE;                                 /* clear done */
            }
        tcdt = data;                                    /* word write only! */
        break;
        }

return SCPE_OK;
}

/* Unit deselect */

void dt_deselect (int32 oldf)
{
int32 old_unit = CSR_GETUNIT (oldf);
UNIT *uptr = dt_dev.units + old_unit;
int32 old_mot = DTS_GETMOT (uptr->STATE);

if (old_mot >= DTS_ATSF)                                /* at speed? */
    dt_newfnc (uptr, DTS_STA (old_mot, DTS_OFR));
else if (old_mot >= DTS_ACCF)                           /* accelerating? */
    DTS_SET2ND (DTS_ATSF | (old_mot & DTS_DIR), DTS_OFR);
return;
}

/* New operation

   1. If function = stop
        - if not already stopped or decelerating, schedule deceleration
        - schedule command completion
   2. If change in direction,
        - if not decelerating, schedule deceleration
        - set accelerating (other dir) as next state
        - set function as next next state
   3. If not accelerating or at speed,
        - schedule acceleration
        - set function as next state
   4. If not yet at speed,
        - set function as next state
   5. If at speed,
        - set function as current state, schedule function
*/

void dt_newsa (int32 newf)
{
int32 new_unit, prev_mot, new_fnc;
int32 prev_dir, new_dir;
UNIT *uptr;

new_unit = CSR_GETUNIT (newf);                          /* new, old units */
uptr = dt_dev.units + new_unit;
if ((uptr->flags & UNIT_ATT) == 0) {                    /* new unit attached? */
    dt_seterr (uptr, STA_SEL);                          /* no, error */
    return;
    }
prev_mot = DTS_GETMOT (uptr->STATE);                    /* previous motion */
prev_dir = prev_mot & DTS_DIR;                          /* previous dir */
new_fnc = CSR_GETFNC (newf);                            /* new function */
new_dir = (newf & CSR_DIR) != 0;                        /* new di? */

if (new_fnc == FNC_SSEL) {                              /* stop unit? */
    sim_activate (&dt_dev.units[DT_TIMER], dt_ctime);   /* sched done */
    dt_stopunit (uptr);                                 /* stop unit */
    return;
    }

if (prev_mot == DTS_STOP) {                             /* start? */
    if (dt_setpos (uptr))                               /* update pos */
        return;
    sim_cancel (uptr);                                  /* stop current */
    sim_activate (uptr, dt_dctime - (dt_dctime >> 2));  /* sched accel */
    DTS_SETSTA (DTS_ACCF | new_dir, 0);                 /* state = accel */
    DTS_SET2ND (DTS_ATSF | new_dir, new_fnc);           /* next = fnc */
    return;
    }

if (prev_dir ^ new_dir) {                               /* dir chg? */
    dt_stopunit (uptr);                                 /* stop unit */
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
int32 fnc, dir, blk, unum, relpos, newpos;
uint32 oldpos;

oldpos = uptr->pos;                                     /* save old pos */
if (dt_setpos (uptr))                                   /* update pos */
    return;
uptr->STATE = newsta;                                   /* update state */
fnc = DTS_GETFNC (uptr->STATE);                         /* set variables */
dir = DTS_GETMOT (uptr->STATE) & DTS_DIR;
unum = (int32) (uptr - dt_dev.units);
if (oldpos == uptr->pos)
    uptr->pos = uptr->pos + (dir? -1: 1);
blk = DT_LIN2BL (uptr->pos, uptr);

if (dir? DT_QREZ (uptr): DT_QFEZ (uptr)) {              /* wrong ez? */
    dt_seterr (uptr, STA_END);                          /* set ez flag, stop */
    return;
    }
dt_substate = 0;                                        /* substate = normal */
sim_cancel (uptr);                                      /* cancel cur op */
switch (fnc) {                                          /* case function */

    case DTS_OFR:                                       /* off reel */
        if (dir)                                        /* rev? < start */
            newpos = -1000;
        else newpos = DTU_FWDEZ (uptr) + DT_EZLIN + 1000; /* fwd? > end */
        break;

    case FNC_SRCH:                                      /* search */
        if (dir)
            newpos = DT_BLK2LN ((DT_QFEZ (uptr)? DTU_TSIZE (uptr): blk), uptr) -
                DT_BLKLN - DT_WSIZE;
        else newpos = DT_BLK2LN ((DT_QREZ (uptr)? 0: blk + 1), uptr) + 
                DT_BLKLN + (DT_WSIZE - 1);
        if (DEBUG_PRI (dt_dev, LOG_MS))
            fprintf (sim_deb, ">>DT%d: searching %s\n", unum,
                    (dir? "backward": "forward"));
        break;

    case FNC_WRIT:                                      /* write */
    case FNC_READ:                                      /* read */
        if (DT_QEZ (uptr)) {                            /* in "ok" end zone? */
            if (dir)
                newpos = DTU_FWDEZ (uptr) - DT_HTLIN - DT_WSIZE;
            else newpos = DT_EZLIN + DT_HTLIN + (DT_WSIZE - 1);
            break;
            }
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            dt_seterr (uptr, STA_BLKM);
            return;
            }
        if (dir)
            newpos = DT_BLK2LN (((relpos >= (DTU_LPERB (uptr) - DT_HTLIN))? blk + 1: blk), uptr) -
                DT_HTLIN - DT_WSIZE;
        else newpos = DT_BLK2LN (((relpos < DT_HTLIN)? blk: blk + 1), uptr) +
                DT_HTLIN + (DT_WSIZE - 1);
        if (DEBUG_PRI (dt_dev, LOG_RW) ||
           (DEBUG_PRI (dt_dev, LOG_BL) && (blk == dt_logblk)))
            fprintf (sim_deb, ">>DT%d: %s block %d %s\n",
                unum, ((fnc == FNC_READ)? "read": "write"),
                blk, (dir? "backward": "forward"));
        break;

    case FNC_RALL:                                      /* read all */
    case FNC_WALL:                                      /* write all */
        if (DT_QEZ (uptr)) {                            /* in "ok" end zone? */
            if (dir)
                newpos = DTU_FWDEZ (uptr) - DT_WSIZE;
            else newpos = DT_EZLIN + (DT_WSIZE - 1);
            }
        else {
            relpos = DT_LIN2OF (uptr->pos, uptr);       /* cur pos in blk */
            if (dir? (relpos < (DTU_LPERB (uptr) - DT_CSMLN)): /* switch in time? */
                (relpos >= DT_CSMLN)) {
                dt_seterr (uptr, STA_BLKM);
                return;
                }
            if (dir)
                newpos = DT_BLK2LN (blk + 1, uptr) - DT_CSMLN - DT_WSIZE;
            else newpos = DT_BLK2LN (blk, uptr) + DT_CSMLN + (DT_WSIZE - 1);
            }
        if (fnc == FNC_WALL) sim_activate               /* write all? */
            (&dt_dev.units[DT_TIMER], dt_ctime);        /* sched done */
        if (DEBUG_PRI (dt_dev, LOG_RW) ||
           (DEBUG_PRI (dt_dev, LOG_BL) && (blk == dt_logblk)))
            fprintf (sim_deb, ">>DT%d: read all block %d %s\n",
                unum, blk, (dir? "backward": "forward"));
        break;

    default:
        dt_seterr (uptr, STA_SEL);                      /* bad state */
        return;
        }

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
    if ((unum == CSR_GETUNIT (tccm)) && (CSR_GETFNC (tccm) != FNC_STOP))
        dt_seterr (uptr, STA_SEL);                      /* error */
    return TRUE;
    }
return FALSE;
}

/* Command timer service after stop - set done */

t_stat dt_svcdone (UNIT *uptr)
{
DT_SETDONE;
return SCPE_OK;
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
int32 blk, wrd, relpos, dat;
uint32 ba, ma;
uint16 wbuf;

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
            sim_activate (uptr, dt_dctime - (dt_dctime >> 2)); /* reversing */
        return SCPE_OK;

    case DTS_ACCF: case DTS_ACCR:                       /* accelerating */
        dt_newfnc (uptr, DTS_NXTSTA (uptr->STATE));     /* adv state, sched */
        return SCPE_OK;

    case DTS_ATSF: case DTS_ATSR:                       /* at speed */
        break;                                          /* check function */

    default:                                            /* other */
        dt_seterr (uptr, STA_SEL);                      /* state error */
        return SCPE_OK;
        }

/* Functional cases

   Search - transfer block number, schedule next block
   Off reel - detach unit (it must be deselected)
*/

if (dt_setpos (uptr))                                   /* upd pos; off reel? */
    return IORETURN (dt_stopoffr, STOP_DTOFF);
if (DT_QEZ (uptr)) {                                    /* in end zone? */
    dt_seterr (uptr, STA_END);                          /* end zone error */
    return SCPE_OK;
    }
blk = DT_LIN2BL (uptr->pos, uptr);                      /* get block # */

switch (fnc) {                                          /* at speed, check fnc */

    case FNC_SRCH:                                      /* search */
        tcdt = blk;                                     /* set block # */
        dt_schedez (uptr, dir);                         /* sched end zone */
        DT_SETDONE;                                     /* set done */
        break;

    case DTS_OFR:                                       /* off reel */
        detach_unit (uptr);                             /* must be deselected */
        uptr->STATE = uptr->pos = 0;                    /* no visible action */
        break;

/* Read

   If wc ovf has not occurred, inc ma, wc and copy word from tape to memory
   If wc ovf, set flag
   If not end of block, schedule next word
   If end of block and not wc ovf, schedule next block
   If end of block and wc ovf, set done, schedule end zone
*/

    case FNC_READ:                                      /* read */
        wrd = DT_LIN2WD (uptr->pos, uptr);              /* get word # */
        if (!dt_substate) {                             /* !wc ovf? */
            ma = (CSR_GETMEX (tccm) << 16) | tcba;      /* form 18b addr */
            ba = (blk * DTU_BSIZE (uptr)) + wrd;        /* buffer ptr */
            tcdt = wbuf = fbuf[ba] & DMASK;             /* read word */
            tcst = (tcst & ~STA_M_XD) | ((fbuf[ma] >> 16) & STA_M_XD);
            if (Map_WriteW (ma, 2, &wbuf)) {            /* store, nxm? */
                dt_seterr (uptr, STA_NXM);
                break;
                }
            tcwc = (tcwc + 1) & DMASK;                  /* incr MA, WC */
            tcba = (tcba + 2) & DMASK;
            if (tcba <= 1)
                tccm = CSR_INCMEX (tccm);
            if (tcwc == 0)
                dt_substate = 1;
            }
        if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))      /* not end blk? */
            sim_activate (uptr, DT_WSIZE * dt_ltime);
        else if (dt_substate) {                         /* wc ovf? */
            dt_schedez (uptr, dir);                     /* sched end zone */
            DT_SETDONE;                                 /* set done */
            }
        else sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
        break;                  

/* Write

   If wc ovf has not occurred, inc ma, wc
   Copy word from memory (or 0, to fill block) to tape
   If wc ovf, set flag
   If not end of block, schedule next word
   If end of block and not wc ovf, schedule next block
   If end of block and wc ovf, set done, schedule end zone
*/

    case FNC_WRIT:                                      /* write */
        wrd = DT_LIN2WD (uptr->pos, uptr);              /* get word # */
        if (dt_substate)                                /* wc ovf? fill */
            tcdt = 0;
        else {
            ma = (CSR_GETMEX (tccm) << 16) | tcba;      /* form 18b addr */
            if (Map_ReadW (ma, 2, &wbuf)) {             /* fetch word */
                dt_seterr (uptr, STA_NXM);
                break;
                }
            tcdt = wbuf;                                /* get word */
            tcwc = (tcwc + 1) & DMASK;                  /* incr MA, WC */
            tcba = (tcba + 2) & DMASK;
            if (tcba <= 1)
                tccm = CSR_INCMEX (tccm);
            }
        ba = (blk * DTU_BSIZE (uptr)) + wrd;            /* buffer ptr */
        fbuf[ba] = tcdt;                                /* write word */
        if (ba >= uptr->hwmark)
            uptr->hwmark = ba + 1;
        if (tcwc == 0)
            dt_substate = 1;
        if (wrd != (dir? 0: DTU_BSIZE (uptr) - 1))      /* not end blk? */
            sim_activate (uptr, DT_WSIZE * dt_ltime);
        else if (dt_substate) {                         /* wc ovf? */
            dt_schedez (uptr, dir);                     /* sched end zone */
            DT_SETDONE;
            }
        else sim_activate (uptr, ((2 * DT_HTLIN) + DT_WSIZE) * dt_ltime);
        break;                  

/* Read all - read current header or data word */

    case FNC_RALL:
        if (tccm & CSR_DONE) {                          /* done set? */
            dt_seterr (uptr, STA_DATM);                 /* data miss */
            break;
            }
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            wrd = DT_LIN2WD (uptr->pos, uptr);
            ba = (blk * DTU_BSIZE (uptr)) + wrd;        /* buffer ptr */
            dat = fbuf[ba];                             /* get tape word */
            }
        else dat = dt_gethdr (uptr, blk, relpos);       /* get hdr */
        if (dir)                                        /* rev? comp obv */
            dat = dt_comobv (dat);
        tcdt = dat & DMASK;                             /* low 16b */
        tcst = (tcst & ~STA_M_XD) | ((dat >> 16) & STA_M_XD);
        sim_activate (uptr, DT_WSIZE * dt_ltime);
        DT_SETDONE;                                     /* set done */
        break;

/* Write all - write current header or data word */

    case FNC_WALL:
        if (tccm & CSR_DONE) {                          /* done set? */
            dt_seterr (uptr, STA_DATM);                 /* data miss */
            break;
            }
        relpos = DT_LIN2OF (uptr->pos, uptr);           /* cur pos in blk */
        if ((relpos >= DT_HTLIN) &&                     /* in data zone? */
            (relpos < (DTU_LPERB (uptr) - DT_HTLIN))) {
            wrd = DT_LIN2WD (uptr->pos, uptr);
            dat = (STA_GETXD (tcst) << 16) | tcdt;      /* get data word */
            if (dir)                                    /* rev? comp obv */
                dat = dt_comobv (dat);
            ba = (blk * DTU_BSIZE (uptr)) + wrd;        /* buffer ptr */
            fbuf[ba] = dat;                             /* write word */
            if (ba >= uptr->hwmark)
                uptr->hwmark = ba + 1;
            }
/*      else                                          *//* ignore hdr */ 
        sim_activate (uptr, DT_WSIZE * dt_ltime);
        DT_SETDONE;                                     /* set done */
        break;

    default:
        dt_seterr (uptr, STA_SEL);                      /* impossible state */
        break;
        }
return SCPE_OK;
}

/* Utility routines */

/* Set error flag */

void dt_seterr (UNIT *uptr, int32 e)
{
int32 mot = DTS_GETMOT (uptr->STATE);

tcst = tcst | e;                                        /* set error flag */
tccm = tccm | CSR_ERR;
if (!(tccm & CSR_DONE)) {                               /* not done? */
    DT_SETDONE;
    }
if (mot >= DTS_ACCF) {                                  /* ~stopped or stopping? */
    sim_cancel (uptr);                                  /* cancel activity */
    if (dt_setpos (uptr))                               /* update position */
        return;
    sim_activate (uptr, dt_dctime);                     /* sched decel */
    DTS_SETSTA (DTS_DECF | (mot & DTS_DIR), 0);         /* state = decel */
    }
return;
}

/* Stop unit */

void dt_stopunit (UNIT *uptr)
{
int32 mot = DTS_GETMOT (uptr->STATE);
int32 dir = mot & DTS_DIR;

if (mot == DTS_STOP) return;                            /* already stopped? */
if ((mot & ~DTS_DIR) != DTS_DECF) {                     /* !already stopping? */
    if (dt_setpos (uptr))                               /* update pos */
        return;
    sim_cancel (uptr);                                  /* stop current */
    sim_activate (uptr, dt_dctime);                     /* schedule decel */
    }
DTS_SETSTA (DTS_DECF | dir, 0);                         /* state = decel */
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

/* Complement obverse routine (18b) */

int32 dt_comobv (int32 dat)
{
dat = dat ^ 0777777;                                    /* compl obverse */
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

csum = 077;                                             /* init csum */
for (i = 0; i < DTU_BSIZE (uptr); i++) {                /* loop thru buf */
    wrd = fbuf[ba + i] ^ 0777777;                       /* get ~word */
    csum = csum ^ (wrd >> 12) ^ (wrd >> 6) ^ wrd;
    }
return (csum & 077);
}

/* Get header word (18b) */

int32 dt_gethdr (UNIT *uptr, int32 blk, int32 relpos)
{
int32 wrd = relpos / DT_WSIZE;

if (wrd == DT_BLKWD)                                    /* fwd blknum */
    return blk;
if (wrd == DT_CSMWD)                                    /* rev csum */
    return 077;
if (wrd == (2 * DT_HTWRD + DTU_BSIZE (uptr) - DT_CSMWD - 1)) /* fwd csum */
    return (dt_csum (uptr, blk) << 12);
if (wrd == (2 * DT_HTWRD + DTU_BSIZE (uptr) - DT_BLKWD - 1)) /* rev blkno */
    return dt_comobv (blk);
return 0;                                               /* all others */
}

/* Reset routine */

t_stat dt_reset (DEVICE *dptr)
{
int32 i, prev_mot;
UNIT *uptr;

for (i = 0; i < DT_NUMDR; i++) {                        /* stop all activity */
    uptr = dt_dev.units + i;
    if (sim_is_running) {                               /* RESET? */
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
tcst =  tcwc = tcba = tcdt = 0;                         /* clear reg */
tccm = CSR_DONE;
CLR_INT (DTA);                                          /* clear int req */
return auto_config (0, 0);
}

/* Device bootstrap */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 020)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    0042124,                        /* "TD" */
    0012706, BOOT_START,            /* MOV #boot_start, SP */
    0012700, 0000000,               /* MOV #unit, R0        ; unit number */
    0010003,                        /* MOV R0, R3 */
    0000303,                        /* SWAB R3 */
    0012701, 0177342,               /* MOV #TCCM, R1        ; csr */
    0012702, 0004003,               /* RW: MOV #4003, R2    ; rev+rnum+go */
    0050302,                        /* BIS R3, R2 */
    0010211,                        /* MOV R2, (R1)         ; load csr */
    0032711, 0100200,               /* BIT #100200, (R1)    ; wait */
    0001775,                        /* BEQ .-4 */
    0100370,                        /* BPL RW               ; no err, cont */
    0005761, 0177776,               /* TST -2(R1)           ; end zone? */
    0100036,                        /* BPL ER               ; no, err */
    0012702, 0000003,               /* MOV #3, R2           ; rnum+go */
    0050302,                        /* BIS R3, R2 */
    0010211,                        /* MOV R2, (R1)         ; load csr */
    0032711, 0100200,               /* BIT #100200, (R1)    ; wait */
    0001775,                        /* BEQ .-4 */
    0100426,                        /* BMI ER               ; err, die */
    0005761, 0000006,               /* TST 6(R1)            ; blk 0? */
    0001023,                        /* BNE ER               ; no, die */
    0012761, 0177000, 0000002,      /* MOV #-256.*2, 2(R1)  ; load wc */
    0005061, 0000004,               /* CLR 4(R1)            ; clear ba */
    0012702, 0000005,               /* MOV #READ+GO, R2     ; read & go */
    0050302,                        /* BIS R3, R2 */
    0010211,                        /* MOV R2, (R1)         ; load csr */
    0005002,                        /* CLR R2 */
    0005003,                        /* CLR R3 */
    0012704, BOOT_START+020,        /* MOV #START+20, R4 */
    0005005,                        /* CLR R5 */
    0032711, 0100200,               /* BIT #100200, (R1)    ; wait */
    0001775,                        /* BEQ .-4 */
    0100401,                        /* BMI ER               ; err, die */
    0005007,                        /* CLR PC */
    0012711, 0000001,               /* ER: MOV #1, (R1)     ; stop all */
    0000000                         /* HALT */
    };

t_stat dt_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern int32 saved_PC;

dt_unit[unitno].pos = DT_EZLIN;
for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & DT_M_NUMDR;
M[BOOT_CSR >> 1] = (dt_dib.ba & DMASK) + 02;
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

/* Attach routine

   Determine 12b, 16b, or 18b/36b format
   Allocate buffer
   If 12b, read 12b format and convert to 18b in buffer
   If 16b, read 16b format and convert to 18b in buffer
   If 18b/36b, read data into buffer
*/

t_stat dt_attach (UNIT *uptr, char *cptr)
{
uint16 pdp8b[D8_NBSIZE];
uint16 pdp11b[D18_BSIZE];
uint32 ba, sz, k, *fbuf;
int32 u = uptr - dt_dev.units;
t_stat r;

r = attach_unit (uptr, cptr);                           /* attach */
if (r != SCPE_OK)                                       /* fail? */
    return r;
if ((sim_switches & SIM_SW_REST) == 0) {                /* not from rest? */
    uptr->flags = (uptr->flags | UNIT_11FMT) & ~UNIT_8FMT; /* default 16b */
    if (sim_switches & SWMASK ('T'))                    /* att 12b? */
        uptr->flags = (uptr->flags | UNIT_8FMT) & ~UNIT_11FMT;
    else if (sim_switches & SWMASK ('F'))               /* att 18b? */
        uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);
    else if (!(sim_switches & SWMASK ('A')) &&          /* autosize? */
        ((sz = sim_fsize (uptr->fileref)) > D16_FILSIZ)) {
        if (sz <= D8_FILSIZ)
            uptr->flags = (uptr->flags | UNIT_8FMT) & ~UNIT_11FMT;
        else uptr->flags = uptr->flags & ~(UNIT_8FMT | UNIT_11FMT);
        }
    }
uptr->capac = DTU_CAPAC (uptr);                         /* set capacity */
uptr->filebuf = calloc (uptr->capac, sizeof (uint32));
if (uptr->filebuf == NULL) {                            /* can't alloc? */
    detach_unit (uptr);
    return SCPE_MEM;
    }
fbuf = (uint32 *) uptr->filebuf;                        /* file buffer */
printf ("%s%d: ", sim_dname (&dt_dev), u);
if (uptr->flags & UNIT_8FMT)
    printf ("12b format");
else if (uptr->flags & UNIT_11FMT)
    printf ("16b format");
else printf ("18b/36b format");
printf (", buffering file in memory\n");
if (uptr->flags & UNIT_8FMT) {                          /* 12b? */
    for (ba = 0; ba < uptr->capac; ) {                  /* loop thru file */
        k = fxread (pdp8b, sizeof (int16), D8_NBSIZE, uptr->fileref);
        if (k == 0)
            break;
        for ( ; k < D8_NBSIZE; k++)
            pdp8b[k] = 0;
        for (k = 0; k < D8_NBSIZE; k = k + 3) {         /* loop thru blk */
            fbuf[ba] = ((uint32) (pdp8b[k] & 07777) << 6) |
                ((uint32) (pdp8b[k + 1] >> 6) & 077);
            fbuf[ba + 1] = ((uint32) (pdp8b[k + 1] & 077) << 12) |
                ((uint32) pdp8b[k + 2] & 07777);
            ba = ba + 2;
            }                                           /* end blk loop */
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

t_stat dt_detach (UNIT* uptr)
{
uint16 pdp8b[D8_NBSIZE];
uint16 pdp11b[D18_BSIZE];
uint32 ba, k, *fbuf;
int32 u = uptr - dt_dev.units;

if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
if (sim_is_active (uptr)) {                             /* active? cancel op */
    sim_cancel (uptr);
    if ((u == CSR_GETUNIT (tccm)) && ((tccm & CSR_DONE) == 0)) {
        tcst = tcst | STA_SEL;
        tccm = tccm | CSR_ERR | CSR_DONE;
        if (tccm & CSR_IE)
            SET_INT (DTA);
        }
    uptr->STATE = uptr->pos = 0;
    }
fbuf = (uint32 *) uptr->filebuf;                        /* file buffer */
if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {   /* any data? */
    printf ("%s%d: writing buffer to file\n", sim_dname (&dt_dev), u);
    rewind (uptr->fileref);                             /* start of file */
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
    else if (uptr->flags & UNIT_11FMT) {                /* 16b? */
        for (ba = 0; ba < uptr->hwmark; ) {             /* loop thru file */
            for (k = 0; k < D18_BSIZE; k++)             /* loop blk */
                pdp11b[k] = fbuf[ba++] & DMASK;
            fxwrite (pdp11b, sizeof (uint16), D18_BSIZE, uptr->fileref);
            if (ferror (uptr->fileref))
                break;
            }                                           /* end loop file */
        }                                               /* end if 16b */
    else fxwrite (uptr->filebuf, sizeof (uint32),       /* write file */
        uptr->hwmark, uptr->fileref);
    if (ferror (uptr->fileref)) perror ("I/O error");
    }                                                   /* end if hwmark */
free (uptr->filebuf);                                   /* release buf */
uptr->flags = uptr->flags & ~UNIT_BUF;                  /* clear buf flag */
uptr->filebuf = NULL;                                   /* clear buf ptr */
uptr->flags = (uptr->flags | UNIT_11FMT) & ~UNIT_8FMT;  /* default fmt */
uptr->capac = DT_CAPAC;                                 /* default size */
return detach_unit (uptr);
}

t_stat dt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *text2;
const char *const text =
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"TC11/TU56 DECtape Controller (DT)\n"
"\n"
" The TCll is a DECtape system consists a Controller and up to 4 dual-unit\n"
" bidirectional magnetic-tape transports, and DECtape 3/4-inch magnetic\n"
" tape on 3.9-inch reels.  Low cost, low maintenance and high reliability\n"
" are assured by:\n"
"\n"
"   - Simply designed transport mechanisms which have no capstans and\n"
"     no pinch rollers.\n"
"   - Hydrodynamically lubricated tape guiding (the tape floats on air\n"
"     over the tape guides while in motion)\n"
"   - Redundant recording\n"
"   - Manchester phase recording techniques (virtually eliminate drop outs)\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" Each transport has a read/write head for information recording and\n"
" playback on five channels of tape.  The system stores information at\n"
" fixed positions on magnetic tape as in magnetic disk or drum storage\n"
" devices, rather than at unknown or variable positions as in conventional\n"
" magnetic tape systems.  This feature allows replacement of blocks of\n"
" data on tape in a random fashion without disturbing other previously\n"
" recorded information.  In particular, during the writing of information\n"
" on tape, the system reads format (mark) and timing information from the\n"
" tape and uses this information to determine the exact position at which\n"
" to record the information to be written. Similarly, in reading, the\n"
" same mark and timing information is used to locate data to be played\n"
" back from the tape.\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" The system utilizes a lO-track read/write head. The first five tracks\n"
" on the tape include a timing track, a mark track, and three data tracks.\n"
" The other five tracks are identical counterparts and are used for\n"
" redundant recording to increase system reliability.  The redundant\n"
" recording of each character bit on non-adjacent tracks materially\n"
" reduces bit dropouts and minimizes the effect of skew. The use of\n"
" Manchester phase recording, rather than amplitude sensing techniques,\n"
" virtually eliminates dropouts.\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" The timing and mark channels control the timing of operations within\n"
" the Controller and establish the format of data contained on the \n"
" information channels. The timing and mark channels are recorded prior\n"
" to all normal data reading and writing on the information channels. The\n"
" timing of operations performed by the tape drive and some control\n"
" functions are determined by the information on the timing channel.\n"
" Therefore, wide variations in the speed of tape motion do not affect\n"
" system performance.\n"
"\n"
" The standard format tape is divided into 578 blocks. The structure of\n"
" each block is symmetric: block numbers and checksums are recorded at\n"
" both ends of a block and thus searching, reading, or writing can occur\n"
" in either direction.  However, a block read in the opposite direction\n"
" than it was written will have the order of the data words reversed.\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n"
" Information read from the mark channel is used during reading and\n"
" writing data to indicate the beginning and end of data blocks and to\n"
" determine the functions performed by the system in each control mode.\n"
" The data tracks ara located in the middle of the tape where the effect\n"
" of skew is minimum.  The data in one bit position of each track is\n"
" referred to as a line or as a character.  Since. six lines make up a\n"
" word, the tape can record, 18-bit data words.  During normal data\n"
" writing, the Controller disassembles the 18-bit word and distributes\n"
" the bits so they are recorded as six 3·bit characters. Since PDP-11\n"
" words are l6·bits long, the Controller writes the extra two bits as 0's\n"
" and ignores them when reading.  However, during special modes, the\n"
" extra two bits can be written and recovered.\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" A 260 foot reel of DECtape is divided into three major areas: end zones\n"
" (forward and reverse), extension zones (forward and reverse), and the\n"
" information zone.  The two end zones (each approximately 10 feet) mark\n"
" the end of the physical tape and are used for winding the tape around\n"
" the heads and onto the take·up reel.  These zones never contain data.\n"
" The forward and reverse extension areas mark the end of the information\n"
" region of the tape. Their length is sufficient to ensure that once the\n"
" end zone is entered and tape motion is reversed; there is adequate\n"
" distance for the transport to come up to proper tape speed before\n"
" entering the information area.\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n"
" The information area, consists of blocks of data.  The standard is a\n"
" nominal 578 blocks, each containing 256 data words (nominally). In \n"
" addition each block contains 10 control words.\n"
"\n"
" The blocks permit digital data to be partitioned into groups of words\n"
" which are interrelated while at the same time reducing the amount of\n"
" storage area that would be needed for addressing individual words.  A\n"
" simple example of such a group of words is a program.  A program can\n"
" be stored and retrieved from magnetic tape in a single block format\n"
" because it is not necessary to be able to retrieve only a single word\n"
" from the program.  It is necessary; however, to be able to retrieve\n"
" different programs which may not be related in any way. Thus, each\n"
" program can be stored in a different block on the tape.\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" Since DECtape is a fixed address system, the programmer need not know\n"
" accurately where the tape has stopped. To locate a specific point on\n"
" tape he must only start the tape motion in the search mode. The address\n"
" of the block currently passing over the head is read into the DECtape\n"
" Control and loaded into an interface register.  Simultaneously, a flag\n"
" is set and a program interrupt can occur.  The program can then compare\n"
" the block number found with the desired block address and tape motion\n"
" continued or reversed accordingly.\n"
"\n"
" DECtape options include the ability to make units write enabled or write\n"
" locked.\n"
" The TC11 supports the BOOT command.  The TC11 is automatically disabled\n"
" in a Qbus system.\n"
"\n"
" The TC11 supports supports PDP-8 format, PDP-11 format, and 18b format\n"
" DECtape images.  ATTACH assumes the image is in PDP-11 format; the user\n"
" can force other choices with switches:\n"
"\n"
"   -t             PDP-8 format\n"
"   -f             18b format\n"
"   -a             autoselect based on file size\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" The DECtape controller is a data-only simulator; the timing and mark\n"
" track, and block header and trailer, are not stored.  Thus, the WRITE\n"
" TIMING AND MARK TRACK function is not supported; the READ ALL function\n"
" always returns the hardware standard block header and trailer; and the\n"
" WRITE ALL function dumps non-data words into the bit bucket.\n";
fprintf (st, "%s", text);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
text2 = 
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n"
" It is critically important to maintain certain timing relationships\n"
" among the DECtape parameters, or the DECtape simulator will fail to\n"
" operate correctly.\n"
"\n"
"    -  LTIME must be at least 6\n"
"    -  DCTIME needs to be at least 100 times LTIME\n"
"\n"
" Acceleration time is set to 75% of deceleration time.\n";
fprintf (st, "%s", text2);
return SCPE_OK;
}

char *dt_description (DEVICE *dptr)
{
return "TC11/TU56 DECtape controller";
}
