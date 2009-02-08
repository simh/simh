/* hp2100_ds.c: HP 2100 13037 disk controller simulator

   Copyright (c) 2004-2008, Robert M. Supnik

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

   DS           13037 disk controller

   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   31-Dec-07    JDB     Corrected and verified ioCRS action
   20-Dec-07    JDB     Corrected DPTR register definition from FLDATA to DRDATA
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   03-Aug-06    JDB     Fixed REQUEST STATUS command to clear status-1
                        Removed redundant attached test in "ds_detach"
   18-Mar-05    RMS     Added attached test to detach routine
   01-Mar-05    JDB     Added SET UNLOAD/LOAD

   Reference:
   - 13037 Disc Controller Technical Information Package (13037-90902, Aug-1980)


   States of the controller: the controller uP runs all the time, but most of
   the time it is waiting for an event.  The simulator only 'runs' the controller
   when there's an event to process: change in CPU interface state, change in
   disk state, or timeout.  The controller has three states:

   - Idle.  No operations other than seek or recalibrate are in progress, and
     the CPU interface is disconnected.  The controller responds both to new
     commands and to drive attention interrupts.
   - Wait.  No operations other than seek or recalibrate are in progress, but
     the CPU interface is connected.  The controller responds to new commands
     but not to drive attention interrupts.
   - Busy.  The controller is processing a command.  The controller does not
     respond to new commands or to drive attention interrupts.

   The controller busy state is loosely related to the testable (visible) busy
   flop.  If the visible busy flop is set, the controller is in the busy state;
   but the controller can also be busy (processing an invalid opcode or invalid
   unit) while visible busy is clear.

   Omissions: the following features are not implemented:

   - Drive hold.  Since this is a single CPU implementation, the drives are
     always available to the CPU.
   - Spare, defective, protected.  The disk files carry only data.
   - Formatting.  The disk files carry only data.
   - ECC.  Data errors are always uncorrectable.
*/

#include <math.h>
#include "hp2100_defs.h"

#define DS_NUMDR        8                               /* max drives */
#define DS_DRMASK       (DS_NUMDR - 1)
#define DS_NUMWD        128                             /* data words/sec */
#define DS_NUMWDF       138                             /* total words/sec */
#define DS_FSYNC        0                               /* sector offsets */
#define DS_FCYL         1
#define DS_FHS          2
#define DS_FDATA        3
#define DS_FIFO_SIZE    16                              /* fifo size */
#define DS_FIFO_EMPTY   (ds_fifo_cnt == 0)
#define ds_ctrl         ds_unit[DS_NUMDR]               /* ctrl thread */
#define ds_timer        ds_unit[DS_NUMDR + 1]           /* timeout thread */
#define GET_CURSEC(x,d) ((int32) fmod (sim_gtime() / ((double) (x)), \
                        ((double) (drv_tab[d].sc))))

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_UNLOAD   (UNIT_V_UF + 1)                 /* heads unloaded */
#define UNIT_V_DTYPE    (UNIT_V_UF + 2)                 /* disk type */
#define UNIT_M_DTYPE    3
#define UNIT_V_AUTO     (UNIT_V_UF + 4)                 /* autosize */
#define UNIT_V_FMT      (UNIT_V_UF + 5)                 /* format enabled */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_FMT        (1 << UNIT_V_FMT)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_UNLOAD     (1 << UNIT_V_UNLOAD)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPR        (UNIT_WLK | UNIT_RO)            /* write prot */

/* Parameters in the unit descriptor */

#define FNC             u3                              /* function */
#define CYL             u4                              /* current cylinder */
#define STA             u5                              /* status */

/* Arguments to subroutines */

#define CLR_BUSY        0                               /* clear visible busy */
#define SET_BUSY        1                               /* set visible busy */

/* Command word - <12:8> are opcode, <7:0> are opcode dependent

   cold load read       <7:6> = head
                        <5:0> = sector
   set file mask        <7:4> = retry count
                        <3:0> = file mask (auto-seek options)
   commands with units  <7>   = hold flag
                        <4:0> = unit number */

#define DSC_V_OP        8                               /* opcode */
#define DSC_M_OP        037
#define  DSC_COLD       000                             /* cold load read */
#define  DSC_RECAL      001                             /* recalibrate */
#define  DSC_SEEK       002                             /* seek */
#define  DSC_RSTA       003                             /* request status */
#define  DSC_RSA        004                             /* request sector addr */
#define  DSC_READ       005                             /* read */
#define  DSC_RFULL      006                             /* read full */
#define  DSC_VFY        007                             /* verify */
#define  DSC_WRITE      010                             /* write */
#define  DSC_WFULL      011                             /* write full */
#define  DSC_CLEAR      012                             /* clear */
#define  DSC_INIT       013                             /* initialize */
#define  DSC_AREC       014                             /* address record */
#define  DSC_RSYN       015                             /* request syndrome */
#define  DSC_ROFF       016                             /* read with offset */
#define  DSC_SFM        017                             /* set file mask */
#define  DSC_RNOVFY     022                             /* read no verify */
#define  DSC_WTIO       023                             /* write TIO */
#define  DSC_RDA        024                             /* request disk addr */
#define  DSC_END        025                             /* end */
#define  DSC_WAKE       026                             /* wakeup */
#define  DSC_ATN        035                             /* pseudo: ATN */
#define  DSC_BADU       036                             /* pseudo: bad unit */
#define  DSC_BADF       037                             /* pseudo: bad opcode */
#define  DSC_NEXT       0040                            /* state increment */
#define  DSC_2ND        0040                            /* subcommand states */
#define  DSC_3RD        0100
#define  DSC_4TH        0140
#define DSC_V_CHD       6                               /* cold load head */
#define DSC_M_CHD       03
#define DSC_V_CSC       0                               /* cold load sector */
#define DSC_M_CSC       077
#define DSC_V_RTY       4                               /* retry count */
#define DSC_M_RTY       017
#define DSC_V_DECR      3                               /* seek decrement */
#define DSC_V_SPEN      2                               /* enable sparing */
#define DSC_V_CYLM      1                               /* cylinder mode */
#define DSC_V_AUTO      0                               /* auto seek */
#define DSC_V_HOLD      7                               /* hold flag */
#define DSC_V_UNIT      0                               /* unit */
#define DSC_M_UNIT      017
#define DSC_V_SPAR      15                              /* INIT spare */
#define DSC_V_PROT      14                              /* INIT protected */
#define DSC_V_DFCT      13                              /* INIT defective */

#define DSC_HOLD        (1u << DSC_V_HOLD)
#define DSC_DECR        (1u << DSC_V_DECR)
#define DSC_SPEN        (1u << DSC_V_SPEN)
#define DSC_CYLM        (1u << DSC_V_CYLM)
#define DSC_AUTO        (1u << DSC_V_AUTO)
#define DSC_FMASK       ((DSC_M_RTY << DSC_V_RTY)|DSC_DECR|\
                        DSC_SPEN|DSC_CYLM|DSC_AUTO)
#define DSC_GETOP(x)    (((x) >> DSC_V_OP) & DSC_M_OP)
#define DSC_GETUNIT(x)  (((x) >> DSC_V_UNIT) & DSC_M_UNIT)
#define DSC_GETCHD(x)   (((x) >> DSC_V_CHD) & DSC_M_CHD)
#define DSC_GETCSC(x)   (((x) >> DSC_V_CSC) & DSC_M_CSC)
#define DSC_SPAR        (1u << DSC_V_SPAR)
#define DSC_PROT        (1u << DSC_V_PROT)
#define DSC_DFCT        (1u << DSC_V_DFCT)

/* Command flags */

#define CMF_UNDF        001                             /* undefined */
#define CMF_CLREC       002                             /* clear eoc flag */
#define CMF_CLRS        004                             /* clear status */
#define CMF_UIDLE       010                             /* requires unit no */

/* Cylinder words - 16b */

/* Head/sector word */

#define DSHS_V_HD       8                               /* head */
#define DSHS_M_HD       037
#define DSHS_V_SC       0                               /* sector */
#define DSHS_M_SC       0377
#define DSHS_HD         (DSHS_M_HD << DSHS_V_HD)
#define DSHS_SC         (DSHS_M_SC << DSHS_V_SC)
#define DSHS_GETHD(x)   (((x) >> DSHS_V_HD) & DSHS_M_HD)
#define DSHS_GETSC(x)   (((x) >> DSHS_V_SC) & DSHS_M_SC)

/* Status 1 */

#define DS1_V_SPAR      15                              /* spare - na */
#define DS1_V_PROT      14                              /* protected - na */
#define DS1_V_DFCT      13                              /* defective - na */
#define DS1_V_STAT      8                               /* status */
#define  DS1_OK         (000 << DS1_V_STAT)             /* normal */
#define  DS1_ILLOP      (001 << DS1_V_STAT)             /* illegal opcode */
#define  DS1_AVAIL      (002 << DS1_V_STAT)             /* available */
#define  DS1_CYLCE      (007 << DS1_V_STAT)             /* cyl compare err */
#define  DS1_UNCOR      (010 << DS1_V_STAT)             /* uncor data err */
#define  DS1_HSCE       (011 << DS1_V_STAT)             /* h/s compare err */
#define  DS1_IOPE       (012 << DS1_V_STAT)             /* IO oper err - na */
#define  DS1_EOCYL      (014 << DS1_V_STAT)             /* end cylinder */
#define  DS1_OVRUN      (016 << DS1_V_STAT)             /* overrun */
#define  DS1_CORDE      (017 << DS1_V_STAT)             /* correctible - na */
#define  DS1_ILLST      (020 << DS1_V_STAT)             /* illegal spare - na */
#define  DS1_DEFTK      (021 << DS1_V_STAT)             /* defective trk - na */
#define  DS1_ACCER      (022 << DS1_V_STAT)             /* access not rdy - na */
#define  DS1_S2ERR      (023 << DS1_V_STAT)             /* status 2 error */
#define  DS1_TKPER      (026 << DS1_V_STAT)             /* protected trk - na */
#define  DS1_UNAVL      (027 << DS1_V_STAT)             /* illegal unit */
#define  DS1_ATN        (037 << DS1_V_STAT)             /* attention */
#define DS1_V_UNIT      0
#define DS1_SPAR        (1u << DS1_V_SPAR)
#define DS1_PROT        (1u << DS1_V_PROT)
#define DS1_DFCT        (1u << DS1_V_DFCT)

/* Status 2, ^ = kept in unit status, * = dynamic */

#define DS2_ERR         0100000                         /* *error */
#define DS2_V_ID        9                               /* drive type */
#define DS2_ATN         0000200                         /* ^attention */
#define DS2_RO          0000100                         /* *read only */
#define DS2_FRM         0000040                         /* *format */
#define DS2_FLT         0000020                         /* fault - na */
#define DS2_FS          0000010                         /* ^first status */
#define DS2_SC          0000004                         /* ^seek error */
#define DS2_NR          0000002                         /* *not ready */
#define DS2_BS          0000001                         /* *busy */
#define DS2_ALLERR      (DS2_FLT|DS2_SC|DS2_NR|DS2_BS)

/* Controller state */

#define DS_IDLE         0                               /* idle */
#define DS_WAIT         1                               /* command wait */
#define DS_BUSY         2                               /* busy */

/* This controller supports four different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   7905         48              3               411             =15MB
   7906         48              4               411             =20MB
   7920         48              5               823             =50MB
   7925         64              9               823             =120MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

   The 7905 and 7906 have fixed and removable platters.  Consequently,
   they are almost always accessed with cylinders limited to each
   platter.  The 7920 and 7925 have multiple-platter packs, and so are
   almost always accessed with cylinders that span all surfaces.

   Disk image files are arranged as a linear set of tracks.  To improve
   locality, tracks on the 7905 and 7906 images are grouped per-platter,
   i.e., all tracks on heads 0 and 1, followed by all tracks on head 2
   (and, for the 7906, head 3), whereas tracks on the 7920 and 7925 are
   sequential by cylinder and head number.

   This variable-access geometry is accomplished by defining a "heads
   per cylinder" value for the fixed and removable sections of each
   drive that indicates the number of heads that should be grouped for
   locality.  The removable values are set to 2 on the 7905 and 7906,
   indicating that those drives typically use cylinders of two surfaces.
   They are set to the number of surfaces per drive for the 7920 and
   7925, as those typically use cylinders encompassing the entire
   spindle.
*/

#define GET_DA(x,y,z,t) \
    (((((y) < drv_tab[t].rh)? \
        (x) * drv_tab[t].rh + (y): \
        drv_tab[t].cyl * drv_tab[t].rh + \
            ((x) * drv_tab[t].fh + (y) - drv_tab[t].rh)) * \
        drv_tab[t].sc + (z)) * DS_NUMWD)

#define D7905_DTYPE     0
#define D7905_SECT      48
#define D7905_SURF      3
#define D7905_RH        2
#define D7905_FH        (D7905_SURF - D7905_RH)
#define D7905_CYL       411
#define D7905_ID        (2 << DS2_V_ID)
#define D7905_SIZE      (D7905_SECT * D7905_SURF * D7905_CYL * DS_NUMWD)

#define D7906_DTYPE     1
#define D7906_SECT      48
#define D7906_SURF      4
#define D7906_RH        2
#define D7906_FH        (D7906_SURF - D7906_RH)
#define D7906_CYL       411
#define D7906_ID        (0 << DS2_V_ID)
#define D7906_SIZE      (D7906_SECT * D7906_SURF * D7906_CYL * DS_NUMWD)

#define D7920_DTYPE     2
#define D7920_SECT      48
#define D7920_SURF      5
#define D7920_RH        D7920_SURF
#define D7920_FH        (D7920_SURF - D7920_RH)
#define D7920_CYL       823
#define D7920_ID        (1 << DS2_V_ID)
#define D7920_SIZE      (D7920_SECT * D7920_SURF * D7920_CYL * DS_NUMWD)

#define D7925_DTYPE     3
#define D7925_SECT      64
#define D7925_SURF      9
#define D7925_RH        D7925_SURF
#define D7925_FH        (D7925_SURF - D7925_RH)
#define D7925_CYL       823
#define D7925_ID        (3 << DS2_V_ID)
#define D7925_SIZE      (D7925_SECT * D7925_SURF * D7925_CYL * DS_NUMWD)

struct drvtyp {
    uint32      sc;                                     /* sectors */
    uint32      hd;                                     /* surfaces */
    uint32      cyl;                                    /* cylinders */
    uint32      size;                                   /* #blocks */
    uint32      id;                                     /* device type */
    uint32      rh;                                     /* removable surfaces */
    uint32      fh;                                     /* fixed surfaces */
    };

static struct drvtyp drv_tab[] = {
    { D7905_SECT, D7905_SURF, D7905_CYL, D7905_SIZE, D7905_ID, D7905_RH, D7905_FH },
    { D7906_SECT, D7906_SURF, D7906_CYL, D7906_SIZE, D7906_ID, D7906_RH, D7906_FH },
    { D7920_SECT, D7920_SURF, D7920_CYL, D7920_SIZE, D7920_ID, D7920_RH, D7920_FH },
    { D7925_SECT, D7925_SURF, D7925_CYL, D7925_SIZE, D7925_ID, D7925_RH, D7925_FH },
    { 0 }
    };

FLIP_FLOP ds_control = CLEAR;
FLIP_FLOP ds_flag = CLEAR;
FLIP_FLOP ds_flagbuf = CLEAR;
FLIP_FLOP ds_srq = CLEAR;

uint32 ds_fifo[DS_FIFO_SIZE] = { 0 };                   /* fifo */
uint32 ds_fifo_ip = 0;                                  /* insertion ptr */
uint32 ds_fifo_rp = 0;                                  /* removal ptr */
uint32 ds_fifo_cnt = 0;                                 /* count */
uint32 ds_cmd = 0;                                      /* command word */
uint32 ds_sr1 = 0;                                      /* status word 1 */
uint32 ds_busy = 0;                                     /* busy flag */
uint32 ds_eoc = 0;                                      /* end of cylinder */
uint32 ds_eod = 0;                                      /* end of data */
uint32 ds_fmask = 0;                                    /* file mask */
uint32 ds_cmdf = 0;                                     /* command follows */
uint32 ds_cmdp = 0;                                     /* command present */
uint32 ds_cyl = 0;                                      /* disk address: cyl */
uint32 ds_hs = 0;                                       /* disk address: hs */
uint32 ds_vctr = 0;                                     /* verify counter */
uint32 ds_state = 0;                                    /* controller state */
uint32 ds_lastatn = 0;                                  /* last atn intr */
int32 ds_stime = 100;                                   /* seek time */
int32 ds_rtime = 100;                                   /* inter-sector time */
int32 ds_ctime = 3;                                     /* command time */
int32 ds_dtime = 1;                                     /* dch time */
int32 ds_tmo = 2749200;                                 /* timeout = 1.74 sec */
uint32 ds_ptr = 0;                                      /* buffer ptr */
uint16 dsxb[DS_NUMWDF];                                 /* sector buffer */

static const uint32 ds_opflags[32] = {                  /* flags for ops */
    CMF_CLREC|CMF_CLRS|CMF_UIDLE,                       /* cold read */
    CMF_CLREC|CMF_CLRS|CMF_UIDLE,                       /* recalibrate */
    CMF_CLREC|CMF_CLRS|CMF_UIDLE,                       /* seek */
    0,                                                  /* read status */
    CMF_CLRS,                                           /* read sector */
    CMF_CLRS|CMF_UIDLE,                                 /* read */
    CMF_CLRS|CMF_UIDLE,                                 /* read full */
    CMF_CLRS|CMF_UIDLE,                                 /* verify */
    CMF_CLRS|CMF_UIDLE,                                 /* write */
    CMF_CLRS|CMF_UIDLE,                                 /* write full */
    CMF_CLRS,                                           /* clear */
    CMF_CLRS|CMF_UIDLE,                                 /* init */
    CMF_CLREC|CMF_CLRS,                                 /* addr record */
    0,                                                  /* read syndrome */
    CMF_CLRS|CMF_UIDLE,                                 /* read offset */
    CMF_CLRS,                                           /* set file mask */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_CLRS|CMF_UIDLE,                                 /* read no verify */
    CMF_CLRS,                                           /* write TIO */
    CMF_CLRS,                                           /* read disk addr */
    CMF_CLRS,                                           /* end */
    CMF_CLRS,                                           /* wake */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS,                                  /* undefined */
    CMF_UNDF|CMF_CLRS                                   /* undefined */
    };

DEVICE ds_dev;
uint32 dsio (uint32 select_code, IOSIG signal, uint32 data);
t_stat ds_svc_c (UNIT *uptr);
t_stat ds_svc_u (UNIT *uptr);
t_stat ds_svc_t (UNIT *uptr);
t_stat ds_reset (DEVICE *dptr);
t_stat ds_attach (UNIT *uptr, char *cptr);
t_stat ds_detach (UNIT *uptr);
t_stat ds_boot (int32 unitno, DEVICE *dptr);
t_stat ds_load_unload (UNIT *uptr, int32 value, char *cptr, void *desc);
t_stat ds_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
void ds_poll (void);
void ds_docmd (uint32 cmd);
void ds_doatn (void);
uint32 ds_updds2 (UNIT *uptr);
void ds_cmd_done (t_bool sf, uint32 sr1);
void ds_wait_for_cpu (UNIT *uptr, uint32 newst);
void ds_set_idle (void);
void ds_sched_ctrl_op (uint32 op, uint32 arg, uint32 busy);
void ds_reqad (uint16 *cyl, uint16 *hs);
void ds_start_seek (UNIT *uptr, uint32 cyl, uint32 newst);
t_bool ds_start_rw (UNIT *uptr, int32 tm, t_bool vfy);
void ds_next_sec (UNIT *uptr);
void ds_next_cyl (UNIT *uptr);
t_stat ds_start_rd (UNIT *uptr, uint32 off, t_bool vfy);
void ds_start_wr (UNIT *uptr, t_bool vfy);
void ds_cont_rd (UNIT *uptr, uint32 bsize);
t_stat ds_cont_wr (UNIT *uptr, uint32 off, uint32 bsize);
void ds_end_rw (UNIT *uptr, uint32 newst);
t_stat ds_set_uncorr (UNIT *uptr);
t_stat ds_clear (void);
void ds_sched_atn (UNIT *uptr);
uint32 ds_fifo_read (void);
void ds_fifo_write (uint32 dat);
void ds_fifo_reset (void);

/* DS data structures

   ds_dev       DS device descriptor
   ds_unit      DS unit list
   ds_reg       DS register list
   ds_mod       DS modifier list
*/

DIB ds_dib = { DS, &dsio };

UNIT ds_unit[] = {
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_u, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, D7905_SIZE) },
    { UDATA (&ds_svc_c, UNIT_DIS, 0) },
    { UDATA (&ds_svc_t, UNIT_DIS, 0) }
    };

REG ds_reg[] = {
    { ORDATA (CMD, ds_cmd, 16) },
    { BRDATA (FIFO, ds_fifo, 8, 16, DS_FIFO_SIZE) },
    { ORDATA (SR1, ds_sr1, 16) },
    { ORDATA (VCTR, ds_vctr, 16) },
    { ORDATA (FMASK, ds_fmask, 8) },
    { ORDATA (CYL, ds_cyl, 16) },
    { ORDATA (HS, ds_hs, 16) },
    { ORDATA (STATE, ds_state, 2), REG_RO },
    { ORDATA (LASTA, ds_lastatn, 3) },
    { DRDATA (FIP, ds_fifo_ip, 4) },
    { DRDATA (FRP, ds_fifo_rp, 4) },
    { DRDATA (FCNT, ds_fifo_cnt, 5) },
    { FLDATA (CTL, ds_control, 0) },
    { FLDATA (FLG, ds_flag,    0) },
    { FLDATA (FBF, ds_flagbuf, 0) },
    { FLDATA (SRQ, ds_srq,     0) },
    { FLDATA (BUSY, ds_busy, 0) },
    { FLDATA (CMDF, ds_cmdf, 0) },
    { FLDATA (CMDP, ds_cmdp, 0) },
    { FLDATA (EOC, ds_eoc, 0) },
    { FLDATA (EOD, ds_eod, 0) },
    { BRDATA (DBUF, dsxb, 8, 16, DS_NUMWDF) },
    { DRDATA (DPTR, ds_ptr, 8) },
    { DRDATA (CTIME, ds_ctime, 24), PV_LEFT + REG_NZ },
    { DRDATA (DTIME, ds_dtime, 24), PV_LEFT + REG_NZ },
    { DRDATA (STIME, ds_stime, 24), PV_LEFT + REG_NZ },
    { DRDATA (RTIME, ds_rtime, 24), PV_LEFT + REG_NZ },
    { DRDATA (TIMEOUT, ds_tmo, 31), PV_LEFT + REG_NZ },
    { URDATA (UCYL, ds_unit[0].CYL, 10, 10, 0,
              DS_NUMDR + 1, PV_LEFT | REG_HRO) },
    { URDATA (UFNC, ds_unit[0].FNC, 8, 8, 0,
              DS_NUMDR + 1, REG_HRO) },
    { URDATA (USTA, ds_unit[0].STA, 8, 16, 0,
              DS_NUMDR + 1, REG_HRO) },
    { URDATA (CAPAC, ds_unit[0].capac, 10, T_ADDR_W, 0,
              DS_NUMDR, PV_LEFT | REG_HRO) },
    { ORDATA (DEVNO, ds_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB ds_mod[] = {
    { UNIT_UNLOAD, UNIT_UNLOAD, "heads unloaded", "UNLOADED", ds_load_unload },
    { UNIT_UNLOAD, 0, "heads loaded", "LOADED", ds_load_unload },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { UNIT_FMT, 0, "format disabled", "NOFORMAT", NULL },
    { UNIT_FMT, UNIT_FMT, "format enabled", "FORMAT", NULL },
    { (UNIT_DTYPE+UNIT_ATT), (D7905_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "7905", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (D7906_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "7906", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (D7920_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "7920", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (D7925_DTYPE << UNIT_V_DTYPE) + UNIT_ATT,
      "7925", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (D7905_DTYPE << UNIT_V_DTYPE),
      "7905", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (D7906_DTYPE << UNIT_V_DTYPE),
      "7906", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (D7920_DTYPE << UNIT_V_DTYPE),
      "7920", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (D7925_DTYPE << UNIT_V_DTYPE),
      "7925", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (D7905_DTYPE << UNIT_V_DTYPE),
      NULL, "7905", &ds_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (D7906_DTYPE << UNIT_V_DTYPE),
      NULL, "7906", &ds_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (D7920_DTYPE << UNIT_V_DTYPE),
      NULL, "7920", &ds_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (D7925_DTYPE << UNIT_V_DTYPE),
      NULL, "7925", &ds_set_size },
    { MTAB_XTD | MTAB_VDV, 0, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &ds_dev },
    { 0 }
    };

DEVICE ds_dev = {
    "DS", ds_unit, ds_reg, ds_mod,
    DS_NUMDR + 2, 8, 27, 1, 8, 16,
    NULL, NULL, &ds_reset,
    &ds_boot, &ds_attach, &ds_detach,
    &ds_dib, DEV_DISABLE
    };


/* I/O signal handler.

   The 13175A disc interface is unusual in that the flag and SRQ signals are
   decoupled.  This is done to allow DMA transfers at the maximum possible speed
   (driving SRQ from the flag limits transfers to only every other cycle).  SRQ
   is based on the card's FIFO; if data or room in the FIFO is available, SRQ is
   set to transfer it.  The flag is only used to signal an interrupt at the end
   of a command.

   Also unusual is that SFC and SFS test different things, rather than
   complementaty states of the same thing.  SFC tests the busy flip-flop, and
   SFS tests the flag flip-flop.

   Implementation notes:

    1. The dispatcher runs the command poll after each I/O signal, except for
       SIR and ENF.  Running the poll for these two will cause multi-drive
       access to fail.
*/

uint32 dsio (uint32 select_code, IOSIG signal, uint32 data)
{
const IOSIG base_signal = IOBASE (signal);              /* derive base signal */

switch (base_signal) {                                  /* dispatch base I/O signal */

    case ioCLF:                                         /* clear flag flip-flop */
        ds_flag = ds_flagbuf = CLEAR;                   /* clear flag */
        ds_srq = CLEAR;                                 /* CLF clears SRQ */
        break;


    case ioSTF:                                         /* set flag flip-flop */
    case ioENF:                                         /* enable flag */
        ds_flag = ds_flagbuf = SET;                     /* set flag and flag buffer */
        break;


    case ioSFC:                                         /* skip if flag is clear */
        setSKF (ds_busy == 0);                          /* skip if not busy */
        break;


    case ioSFS:                                         /* skip if flag is set */
        setstdSKF (ds);
        break;


    case ioIOI:                                         /* I/O data input */
        data = ds_fifo_read ();
        break;


    case ioIOO:                                         /* I/O data output */
        if (ds_cmdf) {                                  /* expecting command? */
            ds_cmd = data;                              /* save command */
            ds_cmdf = 0;
            ds_cmdp = 1;                                /* command present */
            }

        else
            ds_fifo_write (data);                       /* put in fifo */
        break;


    case ioPOPIO:                                       /* power-on preset to I/O */
        ds_flag = ds_flagbuf = SET;                     /* set flag and flag buffer */
        ds_cmdp = 0;                                    /* clear command ready */
                                                        /* fall into CRS handler */

    case ioCRS:                                         /* control reset */
        ds_control = CLEAR;                             /* clear control */
        ds_cmdf = 0;                                    /* not expecting command */
        ds_clear ();                                    /* do controller CLEAR */
        break;


    case ioCLC:                                         /* clear control flip-flop */
        ds_control = CLEAR;                             /* clear control */
        ds_cmdf = 1;                                    /* expecting command */
        ds_cmdp = 0;                                    /* none pending */
        ds_eod = 1;                                     /* set EOD flag */
        ds_fifo_reset ();                               /* clear fifo */
        break;


    case ioSTC:                                         /* set control flip-flop */
        ds_control = SET;                               /* set control */
        break;


    case ioEDT:                                         /* end data transfer */
        ds_eod = 1;                                     /* flag end transfer */
        break;


    case ioSIR:                                         /* set interrupt request */
        setstdPRL (select_code, ds);                    /* set standard PRL signal */
        setstdIRQ (select_code, ds);                    /* set standard IRQ signal */
        setSRQ (select_code, ds_srq);                   /* set SRQ signal */
        break;


    case ioIAK:                                         /* interrupt acknowledge */
        ds_flagbuf = CLEAR;
        break;


    default:                                            /* all other signals */
        break;                                          /*   are ignored */
    }

if (signal > ioCLF)                                     /* multiple signals? */
    dsio (select_code, ioCLF, 0);                       /* issue CLF */
else if (signal > ioSIR)                                /* signal affected interrupt status? */
    dsio (select_code, ioSIR, 0);                       /* set interrupt request */


if ((signal != ioSIR) && (signal != ioENF))             /* if not IRQ update */
    ds_poll ();                                         /*   run the controller */

return data;
}


/* Run the controller polling loop, based on ds_state:

        IDLE    commands and ATN interrupts
        WAIT    commands only
        BUSY    nothing
*/

void ds_poll (void)
{
if ((ds_state != DS_BUSY) && ds_cmdp)                   /* cmd pending? */
    ds_docmd (ds_cmd);                                  /* do it */
if ((ds_state == DS_IDLE) && ds_control)                /* idle? */
    ds_doatn ();                                        /* check ATN */
return;
}


/* Process a command - ctrl state is either IDLE or WAIT.

   - A drive may be processing a seek or recalibrate
   - The controller unit is idle
   - If the command can be processed, ds_state is set to BUSY, and
     the interface command buffer is cleared
   - If the command cannot be processed, ds_state is set to WAIT,
     and the command is retained in the interface command buffer */

void ds_docmd (uint32 cmd)
{
uint32 op, f, dtyp, unum;

op = DSC_GETOP (cmd);                                   /* operation */
f = ds_opflags[op];                                     /* flags */
if (op == DSC_COLD) unum = 0;                           /* boot force unit 0 */
else unum = DSC_GETUNIT (cmd);                          /* get unit */
if ((f & CMF_UIDLE) && (unum < DS_NUMDR) &&             /* idle required */
     sim_is_active (&ds_unit[unum])) {                  /* but unit busy? */
        ds_state = DS_WAIT;                             /* wait */
        return;
        }
ds_cmdp = 0;                                            /* flush command */
ds_state = DS_BUSY;                                     /* ctrl is busy */
if (f & CMF_CLRS) ds_sr1 = 0;                           /* clear status */
if (f & CMF_CLREC) ds_eoc = 0;                          /* clear end cyl */
if (f & CMF_UNDF) {                                     /* illegal op? */
    ds_sched_ctrl_op (DSC_BADF, 0, CLR_BUSY);           /* sched, clr busy */
    return;
    }
switch (op) {

/* Drive commands */

    case DSC_COLD:                                      /* cold load read */
        ds_fmask = DSC_SPEN;                            /* sparing enabled */
        ds_cyl = 0;                                     /* cylinder 0 */
        ds_hs = (DSC_GETCHD (ds_cmd) << DSHS_V_HD) |    /* reformat hd/sec */
            (DSC_GETCSC (ds_cmd) << DSHS_V_SC);
    case DSC_RECAL:                                     /* recalibrate */
    case DSC_SEEK:                                      /* seek */
    case DSC_READ:                                      /* read */
    case DSC_RFULL:                                     /* read full */
    case DSC_ROFF:                                      /* read offset */
    case DSC_RNOVFY:                                    /* read no verify */
    case DSC_VFY:                                       /* verify */
    case DSC_WRITE:                                     /* write */
    case DSC_WFULL:                                     /* write full */
    case DSC_INIT:                                      /* init */
        ds_sr1 = unum;                                  /* init status */
        if (unum >= DS_NUMDR) {                         /* invalid unit? */
            ds_sched_ctrl_op (DSC_BADU, unum, CLR_BUSY);/* sched, not busy */
            return;
            }
        if (op == DSC_INIT) ds_sr1 |=                   /* init? */
            ((cmd & DSC_SPAR)? DS1_SPAR: 0) |           /* copy SPD to stat1 */
            ((cmd & DSC_PROT)? DS1_PROT: 0) |
            ((cmd & DSC_DFCT)? DS1_DFCT: 0);
        ds_unit[unum].FNC = op;                         /* save op */
        ds_unit[unum].STA &= ~DS2_ATN;                  /* clear ATN */
        sim_cancel (&ds_unit[unum]);                    /* cancel current */
        sim_activate (&ds_unit[unum], ds_ctime);        /* schedule unit */
        ds_busy = 1;                                    /* set visible busy */
        break;

/* Read status commands */

    case DSC_RSTA:                                      /* read status */
        dsxb[1] = ds_sr1;                               /* return SR1 */
        ds_sr1 = 0;                                     /* clear SR1 */
        if (unum < DS_NUMDR) {                          /* return SR2 */
            dsxb[0] = ds_updds2 (&ds_unit[unum]);
            ds_unit[unum].STA &= ~DS2_FS;               /* clear 1st */
            }
        else dsxb[0] = DS2_ERR|DS2_NR;
        ds_sched_ctrl_op (DSC_RSTA, 2, SET_BUSY);       /* sched 2 wds, busy */
        break;

    case DSC_RSA:                                       /* read sector address */
        dtyp = GET_DTYPE (ds_unit[unum].flags);         /* get unit type */
        dsxb[0] = GET_CURSEC (ds_dtime * DS_NUMWD, dtyp);       /* rot position */
        ds_sched_ctrl_op (DSC_RSTA, 1, SET_BUSY);       /* sched 1 wd, busy */
        break;

    case DSC_RDA:                                       /* read disk address */
        ds_reqad (&dsxb[1], &dsxb[0]);                  /* return disk address */
        ds_sched_ctrl_op (DSC_RSTA, 2, SET_BUSY);       /* sched 2 wds, busy */
        break;

    case DSC_RSYN:                                      /* read syndrome */
        dsxb[6] = ds_sr1;                               /* return SR1 */
        ds_reqad (&dsxb[5], &dsxb[4]);                  /* return disk address */
        dsxb[3] = dsxb[2] = dsxb[1] = dsxb[0] = 0;      /* syndrome is 0 */
        ds_sched_ctrl_op (DSC_RSTA, 7, SET_BUSY);       /* sched 7 wds, busy */
        break;

/* Other controller commands */

    case DSC_SFM:                                       /* set file mask */
    case DSC_CLEAR:                                     /* clear */
    case DSC_AREC:                                      /* address record */
    case DSC_WAKE:                                      /* wakeup */
    case DSC_WTIO:                                      /* write TIO */
        ds_sched_ctrl_op (op, 0, SET_BUSY);             /* schedule, busy */
        break;

    case DSC_END:                                       /* end */
        ds_set_idle ();                                 /* idle ctrl */
        break;
        }

return;
}


/* Check for attention */

void ds_doatn (void)
{
uint32 i;

for (i = 0; i < DS_NUMDR; i++) {                        /* intr disabled? */
    ds_lastatn = (ds_lastatn + 1) & DS_DRMASK;          /* loop through units */
    if (ds_unit[ds_lastatn].STA & DS2_ATN) {            /* ATN set? */
        ds_unit[ds_lastatn].STA &= ~DS2_ATN;            /* clear ATN */
        dsio (ds_dib.devno, ioENF, 0);                  /* request interrupt */
        ds_sr1 = DS1_ATN | ds_lastatn;                  /* set up status 1 */
        ds_state = DS_WAIT;                             /* block atn intrs */
        return;
        }
    }
return;
}


/* Controller service

   The argument for the function, if any, is stored in uptr->CYL */

t_stat ds_svc_c (UNIT *uptr)
{
uint32 op;

op = uptr->FNC;
switch (op) {

    case DSC_AREC:                                      /* address record */
        ds_wait_for_cpu (uptr, DSC_AREC|DSC_2ND);       /* set flag, new state */
        break;
    case DSC_AREC | DSC_2ND:                            /* poll done */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? */
            ds_cyl = ds_fifo_read ();                   /* save cylinder */
            ds_wait_for_cpu (uptr, DSC_AREC|DSC_3RD);   /* set flag, new state */
            }
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;
    case DSC_AREC | DSC_3RD:                            /* poll done */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? */
            ds_hs = ds_fifo_read ();                    /* save head/sector */
            ds_cmd_done (0, DS1_OK);                    /* op done, no flag */
            }
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;

    case DSC_RSTA:                                      /* rd stat (all forms) */
        if (DS_FIFO_EMPTY) {                            /* fifo empty? */
            uptr->CYL--;
            ds_fifo_write (dsxb[uptr->CYL]);            /* store next status */
            ds_wait_for_cpu (uptr, DSC_RSTA |
                (uptr->CYL? 0: DSC_2ND));               /* set flag, new state */
            }
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;
    case DSC_RSTA | DSC_2ND:                            /* poll done */
        if (DS_FIFO_EMPTY) ds_cmd_done (0, DS1_OK);     /* op done? no flag */
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;

    case DSC_CLEAR:                                     /* clear */
        ds_clear ();                                    /* reset ctrl */

        ds_control = CLEAR;                             /* clear CTL, SRQ */
        ds_srq = CLEAR;
        dsio (ds_dib.devno, ioSIR, 0);                  /* set interrupt request */

        ds_cmd_done (1, DS1_OK);                        /* op done, set flag */
        break;

    case DSC_SFM:                                       /* set file mask */
        ds_fmask = ds_cmd & DSC_FMASK;
        ds_cmd_done (1, DS1_OK);                        /* op done, set flag */
        break;

    case DSC_WTIO:                                      /* write I/O */
        ds_cmd_done (0, DS1_OK);                        /* op done, no flag */
        break;

    case DSC_WAKE:                                      /* wakeup */
        ds_cmd_done (1, DS1_AVAIL);                     /* op done, set flag */
        break;

    case DSC_BADU:                                      /* invalid unit */
        if (uptr->CYL > 10) ds_cmd_done (1, DS1_UNAVL); /* [11,16]? bad unit */
        else ds_cmd_done (1, DS1_S2ERR);                /* else unit not ready */
        break;

    case DSC_BADF:                                      /* invalid operation */
        ds_cmd_done (1, DS1_ILLOP);                     /* op done, set flag */
        break;

    default:
        return SCPE_IERR;
        }

ds_poll ();                                             /* run the controller */
return SCPE_OK;
}


/* Timeout service */

t_stat ds_svc_t (UNIT *uptr)
{
int32 i;

for (i = 0; i < (DS_NUMDR + 1); i++)                    /* cancel all ops */
    sim_cancel (&ds_unit[i]);
ds_set_idle ();                                         /* idle the controller */
ds_fmask = 0;                                           /* clear file mask */
ds_poll ();                                             /* run the controller */
return SCPE_OK;
}


/* Unit service */

t_stat ds_svc_u (UNIT *uptr)
{
uint32 op, dtyp;
t_stat r;

op = uptr->FNC;
dtyp = GET_DTYPE (uptr->flags);

switch (op) {                                           /* case on function */

/* Seek and recalibrate */

    case DSC_RECAL:                                     /* recalibrate */
        if ((uptr->flags & UNIT_UNLOAD) == 0) {         /* drive up? */
            ds_start_seek (uptr, 0, DSC_RECAL|DSC_2ND); /* set up seek */
            ds_set_idle ();                             /* ctrl is idle */
            }
        else ds_cmd_done (1, DS1_S2ERR);                /* not ready error */
        break;
    case DSC_RECAL | DSC_2ND:                           /* recal complete */
        uptr->STA = uptr->STA | DS2_ATN;                /* set attention */
        break;

    case DSC_SEEK:                                      /* seek */
        ds_wait_for_cpu (uptr, DSC_SEEK|DSC_2ND);       /* set flag, new state */
        break;
    case DSC_SEEK | DSC_2ND:                            /* waiting for word 1 */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? */
            ds_cyl = ds_fifo_read ();                   /* save cylinder */
            ds_wait_for_cpu (uptr, DSC_SEEK|DSC_3RD);   /* set flag, new state */
            }
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;
    case DSC_SEEK | DSC_3RD:                            /* waiting for word 2 */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? */
            ds_hs = ds_fifo_read ();                    /* save head/sector */
            if ((uptr->flags & UNIT_UNLOAD) == 0) {     /* drive up? */
                ds_start_seek (uptr, ds_cyl, DSC_SEEK|DSC_4TH); /* set up seek */
                ds_set_idle ();                         /* ctrl is idle */
                }
            else ds_cmd_done (1, DS1_S2ERR);            /* else not ready error */
            }
        else sim_activate (uptr, ds_ctime);             /* continue poll */
        break;
    case DSC_SEEK | DSC_4TH:                            /* seek complete */
        uptr->STA = uptr->STA | DS2_ATN;                /* set attention */
        break;

/* Read variants */

    case DSC_ROFF:                                      /* read with offset */
        ds_wait_for_cpu (uptr, DSC_ROFF|DSC_2ND);       /* set flag, new state */
        break;
    case DSC_ROFF | DSC_2ND:                            /* poll done */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? new state */
            ds_fifo_read ();                            /* drain fifo */
            uptr->FNC = DSC_READ;
            dsio (ds_dib.devno, ioENF, 0);              /* handshake */
            }
        sim_activate (uptr, ds_ctime);                  /* schedule unit */
        break;

    case DSC_COLD:                                      /* cold load read */
        if ((uptr->flags & UNIT_UNLOAD) == 0)           /* drive up? */
            ds_start_seek (uptr, 0, DSC_READ);          /* set up seek */
        else ds_cmd_done (1, DS1_S2ERR);                /* no, not ready error */
        break;

    case DSC_READ:                                      /* read */
        if (r = ds_start_rd (uptr, 0, 1)) return r;     /* new sector; error? */
        break;
    case DSC_READ | DSC_2ND:                            /* word transfer */
        ds_cont_rd (uptr, DS_NUMWD);                    /* xfr wd, check end */
        break;
    case DSC_READ | DSC_3RD:                            /* end of sector */
        ds_end_rw (uptr, DSC_READ);                     /* see if more to do */
        break;

    case DSC_RNOVFY:                                    /* read, no verify */
        if (r = ds_start_rd (uptr, 0, 0)) return r;     /* new sector; error? */
        break;
    case DSC_RNOVFY | DSC_2ND:                          /* word transfer */
        ds_cont_rd (uptr, DS_NUMWD);                    /* xfr wd, check end */
        break;
    case DSC_RNOVFY | DSC_3RD:                          /* end of sector */
        ds_end_rw (uptr, DSC_RNOVFY);                   /* see if more to do */
        break;

    case DSC_RFULL:                                     /* read full */
        dsxb[DS_FSYNC] = 0100376;                       /* fill in header */
        dsxb[DS_FCYL] = uptr->CYL;
        dsxb[DS_FHS] = ds_hs;                           /* before h/s update */
        if (r = ds_start_rd (uptr, DS_FDATA, 0))        /* new sector; error? */
            return r;
        break;
    case DSC_RFULL | DSC_2ND:                           /* word transfer */
        ds_cont_rd (uptr, DS_NUMWDF);                   /* xfr wd, check end */
        break;
    case DSC_RFULL | DSC_3RD:                           /* end of sector */
        ds_end_rw (uptr, DSC_RFULL);                    /* see if more to do */
        break;

    case DSC_VFY:                                       /* verify */
        ds_wait_for_cpu (uptr, DSC_VFY|DSC_2ND);        /* set flag, new state */
        break;
    case DSC_VFY | DSC_2ND:                                     /* poll done */
        if (!DS_FIFO_EMPTY) {                           /* OTA ds? */
            ds_vctr = ds_fifo_read ();                  /* save count */
            uptr->FNC = DSC_VFY | DSC_3RD;              /* next state */
            sim_activate (uptr, ds_rtime);              /* delay for transfer */
            }
        else sim_activate (uptr, ds_ctime);             /* no, continue poll */
        break;
    case DSC_VFY | DSC_3RD:                             /* start sector */
        if (ds_start_rw (uptr, ds_dtime * DS_NUMWD, 1)) break;
                                                        /* new sector; error? */
        ds_next_sec (uptr);                             /* increment hd, sc */
        break;
    case DSC_VFY | DSC_4TH:                                     /* end sector */
        ds_vctr = (ds_vctr - 1) & DMASK;                /* decrement count */
        if (ds_vctr) ds_end_rw (uptr, DSC_VFY|DSC_3RD); /* more to do? */
        else ds_cmd_done (1, DS1_OK);                   /* no, set done */
        break;

/* Write variants */

    case DSC_WRITE:                                     /* write */
        ds_start_wr (uptr, 1);                          /* new sector */
        break;
    case DSC_WRITE | DSC_2ND:
        if (r = ds_cont_wr (uptr, 0, DS_NUMWD))         /* write word */
            return r;                                   /* error? */
        break;
    case DSC_WRITE | DSC_3RD:                           /* end sector */
        ds_end_rw (uptr, DSC_WRITE);                    /* see if more to do */
        break;

    case DSC_INIT:                                      /* init */
        ds_start_wr (uptr, 0);                          /* new sector */
        break;
    case DSC_INIT | DSC_2ND:
        if (r = ds_cont_wr (uptr, 0, DS_NUMWD))         /* write word */
            return r;                                   /* error? */
        break;
    case DSC_INIT | DSC_3RD:                            /* end sector */
        ds_end_rw (uptr, DSC_INIT);                     /* see if more to do */
        break;

    case DSC_WFULL:                                     /* write full */
        ds_start_wr (uptr, 0);                          /* new sector */
        break;
    case DSC_WFULL | DSC_2ND:
        if (r = ds_cont_wr (uptr, DS_FDATA, DS_NUMWDF)) /* write word */
            return r;                                   /* error */
        break;
    case DSC_WFULL | DSC_3RD:
        ds_end_rw (uptr, DSC_WFULL);                    /* see if more to do */
        break;

    default:
        break;
        }

ds_poll ();
return SCPE_OK;
}


/* Schedule timed wait for CPU response

   - Set flag to get CPU attention
   - Set specified unit to 'newstate' and schedule
   - Schedule timeout */

void ds_wait_for_cpu (UNIT *uptr, uint32 newst)
{
dsio (ds_dib.devno, ioENF, 0);                          /* set flag */
uptr->FNC = newst;                                      /* new state */
sim_activate (uptr, ds_ctime);                          /* activate unit */
sim_cancel (&ds_timer);                                 /* activate timeout */
sim_activate (&ds_timer, ds_tmo);
return;
}


/* Set idle state

   - Controller is set to idle state
   - Visible busy is cleared
   - Timeout is cancelled */

void ds_set_idle (void)
{
ds_busy = 0;                                            /* busy clear */
ds_state = DS_IDLE;                                     /* ctrl idle */
sim_cancel (&ds_timer);                                 /* no timeout */
return;
}


/* Set wait state

   - Set flag if required
   - Set controller to wait state
   - Clear visible busy
   - Schedule timeout */

void ds_cmd_done (t_bool sf, uint32 sr1)
{
if (sf)                                                 /* set host flag? */
    dsio (ds_dib.devno, ioENF, 0);                      /* set flag */

ds_busy = 0;                                            /* clear visible busy */
ds_sr1 = ds_sr1 | sr1;                                  /* final status */
ds_state = DS_WAIT;                                     /* ctrl waiting */
sim_cancel (&ds_timer);                                 /* activate timeout */
sim_activate (&ds_timer, ds_tmo);
return;
}


/* Return drive status (status word 2) */

uint32 ds_updds2 (UNIT *uptr)
{
uint32 sta;
uint32 dtyp = GET_DTYPE (uptr->flags);

sta = drv_tab[dtyp].id |                                /* form status */
    uptr->STA |                                         /* static bits */
    ((uptr->flags & UNIT_WPR)? DS2_RO: 0) |             /* dynamic bits */
    ((uptr->flags & UNIT_FMT)? DS2_FRM: 0) |
    ((uptr->flags & UNIT_UNLOAD)? DS2_NR | DS2_BS: 0) |
    (sim_is_active (uptr)? DS2_BS: 0);
if (sta & DS2_ALLERR) sta = sta | DS2_ERR;              /* set error */
return sta;
}


/* Schedule controller operation */

void ds_sched_ctrl_op (uint32 op, uint32 arg, uint32 busy)
{
ds_ctrl.FNC = op;                                       /* save op */
ds_ctrl.CYL = arg;                                      /* save argument */
ds_busy = busy;                                         /* set visible busy */
sim_activate (&ds_ctrl, ds_ctime);                      /* schedule */
sim_cancel (&ds_timer);                                 /* activate timeout */
sim_activate (&ds_timer, ds_tmo);
return;
}


/* Request address - if pending eoc, report cylinder + 1 */

void ds_reqad (uint16 *cyl, uint16 *hs)
{
*cyl = ds_cyl + (ds_eoc? 1: 0);
*hs = ds_hs;
return;
}


/* Start seek - schedule whether in bounds or out of bounds */

void ds_start_seek (UNIT *uptr, uint32 cyl, uint32 newst)
{
int32 t;
uint32 hd, sc;
uint32 dtyp = GET_DTYPE (uptr->flags);

uptr->FNC = newst;                                      /* set new state */
if (cyl >= drv_tab[dtyp].cyl) {                         /* out of bounds? */
    t = 0;                                              /* don't change cyl */
    uptr->STA = uptr->STA | DS2_SC;                     /* set seek check */
    }
else {
    t = abs (uptr->CYL - cyl);                          /* delta cylinders */
    uptr->CYL = cyl;                                    /* put on cylinder */
    hd = DSHS_GETHD (ds_hs);                            /* invalid head or sec? */
    sc = DSHS_GETSC (ds_hs);
    if ((hd >= drv_tab[dtyp].hd) ||
        (sc >= drv_tab[dtyp].sc))
        uptr->STA = uptr->STA | DS2_SC;                 /* set seek check */
    else uptr->STA = uptr->STA & ~DS2_SC;               /* clear seek check */
    }
sim_activate (uptr, ds_stime * (t + 1));                /* schedule */
return;
}


/* Start next sector for read or write

   - If error, set command done, return TRUE, nothing is scheduled
   - If implicit seek, return TRUE, implicit seek is scheduled, but
     state is not changed - we will return here when seek is done
   - Otherwise, advance state, set position in file, schedule next state */

t_bool ds_start_rw (UNIT *uptr, int32 tm, t_bool vfy)
{
uint32 da, hd, sc;
uint32 dtyp = GET_DTYPE (uptr->flags);

ds_eod = 0;                                             /* init eod */
ds_ptr = 0;                                             /* init buffer ptr */
if (uptr->flags & UNIT_UNLOAD) {                        /* drive down? */
    ds_cmd_done (1, DS1_S2ERR);
    return TRUE;
    }
if (ds_eoc) {                                           /* at end of cylinder? */
    ds_next_cyl (uptr);                                 /* auto seek to next */
    return TRUE;                                        /* or error */
    }
if (vfy && ((uint32) uptr->CYL != ds_cyl)) {            /* on wrong cylinder? */
    if (ds_cyl >= drv_tab[dtyp].cyl)                    /* seeking to bad? */
        ds_cmd_done (1, DS1_CYLCE);                     /* lose */
    else ds_start_seek (uptr, ds_cyl, uptr->FNC);       /* seek right cyl */
    return TRUE;
    }
hd = DSHS_GETHD (ds_hs);
sc = DSHS_GETSC (ds_hs);
if ((uint32) uptr->CYL >= drv_tab[dtyp].cyl) {          /* valid cylinder? */
    uptr->STA = uptr->STA | DS2_SC;                     /* set seek check */
    ds_cmd_done (1, DS1_S2ERR);                         /* error */
    return TRUE;
    }
if ((hd >= drv_tab[dtyp].hd) ||                         /* valid head, sector? */
    (sc >= drv_tab[dtyp].sc)) {
    ds_cmd_done (1, DS1_HSCE);                          /* no, error */
    return TRUE;
    }
da = GET_DA (uptr->CYL, hd, sc, dtyp);                  /* position in file */
sim_fseek (uptr->fileref, da * sizeof (uint16), SEEK_SET); /* set file pos */
uptr->FNC += DSC_NEXT;                                  /* next state */
sim_activate (uptr, tm);                                /* activate unit */
return FALSE;
}


/* Start next sector for read

   - Do common start for read and write
   - If error, return, command has been terminated, nothing scheduled
   - If implicit seek, return, seek scheduled
   - If no error or seek, state has been advanced and unit scheduled
   - Read sector
   - If read error, terminate command and return, nothing scheduled
   - If no error, advance head/sector, next state scheduled */

t_stat ds_start_rd (UNIT *uptr, uint32 off, t_bool vfy)
{
uint32 t;

if (ds_start_rw (uptr, ds_rtime, vfy)) return SCPE_OK;  /* new sec; err or seek? */
t = sim_fread (dsxb + off, sizeof (uint16), DS_NUMWD, uptr->fileref);
for (t = t + off ; t < DS_NUMWDF; t++) dsxb[t] = 0;     /* fill sector */
if (ferror (uptr->fileref))                             /* error? */
    return ds_set_uncorr (uptr);                        /* say uncorrectable */
ds_next_sec (uptr);                                     /* increment hd, sc */
return SCPE_OK;
}


/* Start next sector for write

   - Do common start for read and write
   - If error, return, command has been terminated, nothing scheduled
   - If implicit seek, return, seek scheduled
   - If no error or seek, state has been advanced and unit scheduled
   - Clear buffer
   - Set service request */

void ds_start_wr (UNIT *uptr, t_bool vfy)
{
uint32 i;

if ((uptr->flags & UNIT_WPR) ||                         /* write protected? */
    (!vfy && ((uptr->flags & UNIT_FMT) == 0))) {        /* format, not enbl? */
    ds_cmd_done (1, DS1_S2ERR);                         /* error */
    return;
    }
if (ds_start_rw (uptr, ds_rtime, vfy)) return;          /* new sec; err or seek? */
for (i = 0; i < DS_NUMWDF; i++) dsxb[i] = 0;            /* clear buffer */
ds_srq = SET;                                           /* request word */
dsio (ds_dib.devno, ioSIR, 0);                          /* set interrupt request */
return;
}


/* Advance to next sector (but not next cylinder) */

void ds_next_sec (UNIT *uptr)
{
uint32 dtyp = GET_DTYPE (uptr->flags);

ds_hs = ds_hs + 1;                                      /* increment sector */
if (DSHS_GETSC (ds_hs) < drv_tab[dtyp].sc) return;      /* end of track? */
ds_hs = ds_hs & ~DSHS_SC;                               /* yes, wrap sector */
if (ds_fmask & DSC_CYLM) {                              /* cylinder mode? */
    ds_hs = ds_hs + (1 << DSHS_V_HD);                   /* increment head */
    if (DSHS_GETHD (ds_hs) < drv_tab[dtyp].hd) return;  /* end of cyl? */
    ds_hs = ds_hs & ~DSHS_HD;                           /* 0 head */
    }
ds_eoc = 1;                                             /* flag end cylinder */
return;
}


/* Advance to next cylinder

   - If autoseek enabled, seek to cylinder +/- 1
   - Otherwise, done with end of cylinder error */

void ds_next_cyl (UNIT *uptr)
{
if (ds_fmask & DSC_AUTO) {                              /* auto seek allowed? */
    if (ds_fmask & DSC_DECR) ds_cyl = (ds_cyl - 1) & DMASK;
    else ds_cyl = (ds_cyl + 1) & DMASK;
    ds_eoc = 0;                                         /* clear end cylinder */
    ds_start_seek (uptr, ds_cyl, uptr->FNC);            /* seek, same state */
    }
else ds_cmd_done (1, DS1_EOCYL);                        /* no, end of cyl err */
return;
}


/* Transfer word for read

   - If end of data, terminate command, nothing scheduled
   - Otherwise, transfer word, advance state if last word, schedule */

void ds_cont_rd (UNIT *uptr, uint32 bsize)
{
if (ds_eod) ds_cmd_done (1, DS1_OK);                    /* DMA end? done */
else if (ds_srq) {                                      /* overrun? */
    ds_cmd_done (1, DS1_OVRUN);                         /* set done */
    return;
    }
else {
    ds_fifo_write (dsxb[ds_ptr++]);                     /* next word */
    ds_srq = SET;                                       /* request service */
    dsio (ds_dib.devno, ioSIR, 0);                      /* set interrupt request */
    if (ds_ptr >= bsize) uptr->FNC += DSC_NEXT;         /* sec done? next state */
    sim_activate (uptr, ds_dtime);                      /* schedule */
    }
return;
}


/* Transfer word for write

   - Copy word from fifo to buffer
   - If end of data, write buffer, terminate command, nothing scheduled
   - If end of sector, write buffer, next state, schedule
   - Otherwises, set service request, schedule  */

t_stat ds_cont_wr (UNIT *uptr, uint32 off, uint32 bsize)
{
uint32 i, dat;

if (ds_srq) {                                           /* overrun? */
    ds_cmd_done (1, DS1_OVRUN);                         /* set done */
    return SCPE_OK;
    }
dsxb[ds_ptr++] = dat = ds_fifo_read ();                 /* next word */
if (ds_eod || (ds_ptr >= bsize)) {                      /* xfr or sector done? */
    for (i = ds_ptr; i < bsize; i++) dsxb[i] = dat;     /* fill sector */
    sim_fwrite (dsxb + off, sizeof (uint16), DS_NUMWD, uptr->fileref);
    if (ferror (uptr->fileref))                         /* error on write? */
        return ds_set_uncorr (uptr);                    /* uncorrectable */
    ds_next_sec (uptr);                                 /* increment hd, sc */
    if (ds_eod) {                                       /* end data? */
        ds_cmd_done (1, DS1_OK);                        /* set done */
        return SCPE_OK;
        }
    else uptr->FNC += DSC_NEXT;                         /* no, next state */
    }
else {
    ds_srq = SET;                                       /* request next word */
    dsio (ds_dib.devno, ioSIR, 0);                      /* set interrupt request */
    }
sim_activate (uptr, ds_dtime);                          /* schedule */
return SCPE_OK;
}


/* End sector for read or write

   - If end of data, terminate command, nothing scheduled
   - If end of cylinder, schedule next cylinder
   - Else schedule start of next sector */

void ds_end_rw (UNIT *uptr, uint32 newst)
{
uptr->FNC = newst;                                      /* new state */
if (ds_eod) ds_cmd_done (1, DS1_OK);                    /* done? */
else if (ds_eoc) ds_next_cyl (uptr);                    /* end cyl? seek */
else sim_activate (uptr, ds_rtime);                     /* normal transfer */
return;
}


/* Report uncorrectable data error */

t_stat ds_set_uncorr (UNIT *uptr)
{
sim_cancel (uptr);                                      /* cancel any operation */
ds_cmd_done (1, DS1_UNCOR);                             /* done with error */
perror ("DS I/O error");                                /* visible error */
clearerr (uptr->fileref);
ds_poll ();                                             /* force poll */
return SCPE_IOERR;
}


/* Fifo read */

uint32 ds_fifo_read (void)
{
uint32 dat;

if (ds_fifo_cnt == 0) return ds_fifo[ds_fifo_rp];
dat = ds_fifo[ds_fifo_rp++];
if (ds_fifo_rp >= DS_FIFO_SIZE) ds_fifo_rp = 0;
ds_fifo_cnt--;
return dat;
}

void ds_fifo_write (uint32 dat)
{
ds_fifo[ds_fifo_ip++] = dat;
if (ds_fifo_ip >= DS_FIFO_SIZE) ds_fifo_ip = 0;
if (ds_fifo_cnt < DS_FIFO_SIZE) ds_fifo_cnt++;
return;
}

void ds_fifo_reset (void)
{
uint32 i;

ds_fifo_ip = ds_fifo_rp = ds_fifo_cnt = 0;
for (i = 0; i < DS_FIFO_SIZE; i++) ds_fifo[i] = 0;
return;
}


/* Controller clear */

t_stat ds_clear (void)
{
int32 i;

ds_cmd = 0;                                             /* clear command */
ds_cmdf = ds_cmdp = 0;                                  /* clear commands flops */
ds_fifo_reset ();                                       /* clear fifo */
ds_eoc = ds_eod = 0;
ds_busy = 0;
ds_state = DS_IDLE;                                     /* ctrl idle */
ds_lastatn = 0;
ds_fmask = 0;
ds_ptr = 0;
ds_cyl = ds_hs = 0;
ds_vctr = 0;
for (i = 0; i < DS_NUMDR; i++) {                        /* loop thru drives */
    sim_cancel (&ds_unit[i]);                           /* cancel activity */
    ds_unit[i].FNC = 0;                                 /* clear function */
    ds_unit[i].CYL = 0;
    ds_unit[i].STA = 0;
    }
sim_cancel (&ds_ctrl);
sim_cancel (&ds_timer);
return SCPE_OK;
}


/* Reset routine.

   The PON signal clears the Interface Selected flip-flop, disconnecting the
   interface from the disc controller.  Under simulation, the interface always
   remains connected to the controller, so we take no special action on
   power-up.
*/

t_stat ds_reset (DEVICE *dptr)
{
dsio (ds_dib.devno, ioPOPIO, 0);                        /* send POPIO signal */
ds_srq = CLEAR;                                         /* clear SRQ */
return SCPE_OK;
}


/* Device attach */

t_stat ds_attach (UNIT *uptr, char *cptr)
{
uint32 i, p;
t_stat r;

uptr->capac = drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK) return r;                             /* error? */
ds_load_unload (uptr, 0, NULL, NULL);                   /* if OK, load heads */
ds_sched_atn (uptr);                                    /* schedule attention */
if (((uptr->flags & UNIT_AUTO) == 0) ||                 /* static size? */
    ((p = sim_fsize (uptr->fileref)) == 0)) return SCPE_OK;     /* new file? */
for (i = 0; drv_tab[i].sc != 0; i++) {                  /* find best fit */
    if (p <= (drv_tab[i].size * sizeof (uint16))) {
        uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
        uptr->capac = drv_tab[i].size;
        return SCPE_OK;
        }
    }
return SCPE_OK;
}


/* Device detach */

t_stat ds_detach (UNIT *uptr)
{
ds_load_unload (uptr, UNIT_UNLOAD, NULL, NULL);         /* unload heads if attached */
return detach_unit (uptr);
}


/* Load and unload heads */

t_stat ds_load_unload (UNIT *uptr, int32 value, char *cptr, void *desc)
{
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;   /* must be attached to [un]load */
if (value == UNIT_UNLOAD) {                             /* unload heads? */
    uptr->flags = uptr->flags | UNIT_UNLOAD;            /* indicate unload */
    uptr->STA = DS2_ATN;                                /* update drive status */
    ds_sched_atn (uptr);                                /* schedule attention */
    }
else {                                                  /* load heads */
    uptr->flags = uptr->flags & ~UNIT_UNLOAD;           /* indicate load */
    uptr->STA = DS2_ATN | DS2_FS;                       /* update drive status */
    }
return SCPE_OK;
}


/* Schedule attention interrupt if CTL set, not restore, and controller idle */

void ds_sched_atn (UNIT *uptr)
{
int32 i;

if (!ds_control || (sim_switches & SIM_SW_REST)) return;
for (i = 0; i < (DS_NUMDR + 1); i++) {                  /* check units, ctrl */
    if (sim_is_active (ds_dev.units + i)) return;
    }
uptr->FNC = DSC_ATN;                                    /* pseudo operation */
sim_activate (uptr, 1);                                 /* do immediately */
return;
}


/* Set size command validation routine */

t_stat ds_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
uptr->capac = drv_tab[GET_DTYPE (val)].size;
return SCPE_OK;
}


/* 13037 bootstrap routine (HP 12992B ROM) */

const BOOT_ROM ds_rom = {
    0017727,                    /* STRT JSB STAT        ; get status */
    0002021,                    /*      SSA,RSS         ; is drive ready? */
    0027742,                    /*      JMP DMA         ; yes, set up DMA */
    0013714,                    /*      AND B20         ; no, check status bits */
    0002002,                    /*      SZA             ; faulty or hard down? */
    0102030,                    /*      HLT 30B         ; HALT 30B */
    0027700,                    /*      JMP STRT        ; try again */
    0102011,                    /* ADR1 OCT 102011 */
    0102055,                    /* ADR2 OCT 102055 */
    0164000,                    /* CNT  DEC -6144 */
    0000007,                    /* D7   OCT 7 */
    0001400,                    /* STCM OCT 1400 */
    0000020,                    /* B20  OCT 20 */
    0017400,                    /* STMS OCT 17400 */
    0000000,                    /* 9 NOP's */
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,
    0000000,                    /* STAT NOP             ; status check routine */
    0107710,                    /*      CLC DC,C        ; set command mode */
    0063713,                    /*      LDA STCM        ; get status command */
    0102610,                    /*      OTA DC          ; output status command */
    0102310,                    /*      SFS DC          ; wait for stat#1 word */
    0027733,                    /*      JMP *-1 */
    0107510,                    /*      LIB DC,C        ; B-reg - status#1 word */
    0102310,                    /*      SFS DC          ; wait for stat#2 word */
    0027736,                    /*      JMP *-1 */
    0103510,                    /*      LIA DC,C        ; A-reg - status#2 word */
    0127727,                    /*      JMP STAT,I      ; return */
    0067776,                    /* DMA  LDB DMAC        ; get DMA control word */
    0106606,                    /*      OTB 6           ; output DMA ctrl word */
    0067707,                    /*      LDB ADR1        ; get memory address */
    0106702,                    /*      CLC 2           ; set memory addr mode */
    0106602,                    /*      OTB 2           ; output mem addr to DMA */
    0102702,                    /*      STC 2           ; set word count mode */
    0067711,                    /*      LDB CNT         ; get word count */
    0106602,                    /*      OTB 2           ; output word cnt to DMA */
    0106710,                    /* CLC  CLC DC          ; set command follows */
    0102501,                    /*      LIA 1           ; load switches */
    0106501,                    /*      LIB 1           ; register settings */
    0013712,                    /*      AND D7          ; isolate head number */
    0005750,                    /*      BLF,CLE,SLB     ; bit 12 = 0? */
    0027762,                    /*      JMP *+3         ; no, manual boot */
    0002002,                    /*      SZA             ; yes, RPL, head# = 0? */
    0001000,                    /*      ALS             ; no, head# = 1 --> 2 */
    0001720,                    /*      ALF,ALS         ; form cold load */
    0001000,                    /*      ALS             ; command word */
    0103706,                    /*      STC 6,C         ; activate DMA */
    0103610,                    /*      OTA DC,C        ; output cold load cmd */
    0102310,                    /*      SFS DC          ; is cold load done? */
    0027766,                    /*      JMP *-1         ; no, wait */
    0017727,                    /*      JSB STAT        ; yes, get status */
    0060001,                    /*      LDA 1           ; get status word #1 */
    0013715,                    /*      AND STMS        ; isolate status bits */
    0002002,                    /*      SZA             ; is transfer ok? */
    0027700,                    /*      JMP STRT        ; no, try again */
    0117710,                    /*      JSB ADR2,I      ; yes, start program */
    0000010,                    /* DMAC ABS DC          ; DMA command word */
    0170100,                    /*      ABS -STRT */
    };

t_stat ds_boot (int32 unitno, DEVICE *dptr)
{
int32 dev;

if (unitno != 0) return SCPE_NOFNC;                     /* only unit 0 */
dev = ds_dib.devno;                                     /* get data chan dev */
if (ibl_copy (ds_rom, dev)) return SCPE_IERR;           /* copy boot to memory */
SR = (SR & (IBL_OPT | IBL_DS_HEAD)) | IBL_DS | IBL_MAN | (dev << IBL_V_DEV);
return SCPE_OK;
}
