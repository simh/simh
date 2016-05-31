/* hp2100_dp.c: HP 2100 12557A/13210A disk simulator

   Copyright (c) 1993-2016, Robert M. Supnik

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

   DP           12557A 2871 disk subsystem
                13210A 7900 disk subsystem

   13-May-16    JDB     Modified for revised SCP API function parameter types
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Dec-12    MP      Now calls sim_activate_time to get remaining seek time
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added CNTLR_TYPE cast to dp_settype
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   10-Aug-08    JDB     Added REG_FIT to register variables < 32-bit size
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   01-Mar-05    JDB     Added SET UNLOAD/LOAD
   07-Oct-04    JDB     Fixed enable/disable from either device
                        Fixed ANY ERROR status for 12557A interface
                        Fixed unattached drive status for 12557A interface
                        Status cmd without prior STC DC now completes (12557A)
                        OTA/OTB CC on 13210A interface also does CLC CC
                        Fixed RAR model
                        Fixed seek check on 13210 if sector out of range
   20-Aug-04    JDB     Fixes from Dave Bryan
                        - Check status on unattached drive set busy and not ready
                        - Check status tests wrong unit for write protect status
                        - Drive on line sets ATN, will set FLG if polling
   15-Aug-04    RMS     Controller resumes polling for ATN interrupts after
                        read status (found by Dave Bryan)
   22-Jul-04    RMS     Controller sets ATN for all commands except
                        read status (found by Dave Bryan)
   21-Apr-04    RMS     Fixed typo in boot loader (found by Dave Bryan)
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Fixed SR setting in IBL
                        Fixed interpretation of SR<0>
                        Revised IBL loader
                        Implemented DMA SRQ (follows FLG)
   25-Apr-03    RMS     Revised for extended file support
                        Fixed bug(s) in boot (found by Terry Newton)
   10-Nov-02    RMS     Added BOOT command, fixed numerous bugs
   15-Jan-02    RMS     Fixed INIT handling (found by Bill McDermith)
   10-Jan-02    RMS     Fixed f(x)write call (found by Bill McDermith)
   03-Dec-01    RMS     Changed DEVNO to use extended SET/SHOW
   24-Nov-01    RMS     Changed STA to be an array
   07-Sep-01    RMS     Moved function prototypes
   29-Nov-00    RMS     Made variable names unique
   21-Nov-00    RMS     Fixed flag, buffer power up state

   References:
   - 7900A Disc Drive Operating and Service Manual (07900-90002, Feb-1975)
   - 13210A Disc Drive Interface Kit Operating and Service Manual
            (13210-90003, Nov-1974)
   - 12557A Cartridge Disc Interface Kit Operating and Service Manual
            (12557-90001, Sep-1970)


   The simulator uses a number of state variables:

   dpc_busy             set to drive number + 1 when the controller is busy
                        of the unit in use
   dpd_xfer             set to 1 if the data channel is executing a data transfer
   dpd_wval             set to 1 by OTx if either !dpc_busy or dpd_xfer
   dpc_poll             set to 1 if attention polling is enabled

   dpc_busy and dpd_xfer are set together at the start of a read, write, refine,
   or init.  When data transfers are complete (CLC DC), dpd_xfer is cleared, but the
   operation is not necessarily over.  When the operation is complete, dpc_busy
   is cleared and the command channel flag is set.

   dpc_busy && !dpd_xfer && STC DC (controller is busy, data channel transfer has
   been terminated by CLC DC, but a word has been placed in the data channel buffer)
   indicates data overrun.

   dpd_wval is used in write operations to fill out the sector buffer with 0's
   if only a partial sector has been transferred.

   dpc_poll indicates whether seek completion polling can occur.  It is cleared
   by reset and CLC CC and set by issuance of a seek or completion of check status.

   The controller's "Record Address Register" (RAR) contains the CHS address of
   the last Seek or Address Record command executed.  The RAR is shared among
   all drives on the controller.  In addition, each drive has an internal
   position register that contains the last cylinder position transferred to the
   drive during Seek command execution (data operations always start with the
   RAR head and sector position).

   In a real drive, the address field of the sector under the head is read and
   compared to the RAR.  When they match, the target sector is under the head
   and is ready for reading or writing.  If a match doesn't occur, an Address
   Error is indicated.  In the simulator, the address field is obtained from the
   drive's current position register during a read, i.e., the "on-disc" address
   field is assumed to match the current position.

   The following implemented behaviors have been inferred from secondary sources
   (diagnostics, operating system drivers, etc.), due to absent or contradictory
   authoritative information; future correction may be needed:

     1. Status bit 15 (ATTENTION) does not set bit 0 (ANY ERROR) on the 12557A.
     2. Omitting STC DC before Status Check does not set DC flag but does poll.
*/

#include "hp2100_defs.h"

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_UNLOAD   (UNIT_V_UF + 1)                 /* heads unloaded */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_UNLOAD     (1 << UNIT_V_UNLOAD)
#define FNC             u3                              /* saved function */
#define DRV             u4                              /* drive number (DC) */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write prot */

#define DP_N_NUMWD      7
#define DP_NUMWD        (1 << DP_N_NUMWD)               /* words/sector */
#define DP_NUMSC2       12                              /* sectors/srf 12557 */
#define DP_NUMSC3       24                              /* sectors/srf 13210 */
#define DP_NUMSC        (dp_ctype ? DP_NUMSC3 : DP_NUMSC2)
#define DP_NUMSF        4                               /* surfaces/cylinder */
#define DP_NUMCY        203                             /* cylinders/disk */
#define DP_SIZE2        (DP_NUMSF * DP_NUMCY * DP_NUMSC2 * DP_NUMWD)
#define DP_SIZE3        (DP_NUMSF * DP_NUMCY * DP_NUMSC3 * DP_NUMWD)
#define DP_NUMDRV       4                               /* # drives */

/* Command word */

#define CW_V_FNC        12                              /* function */
#define CW_M_FNC        017
#define CW_GETFNC(x)    (((x) >> CW_V_FNC) & CW_M_FNC)
#define  FNC_STA        000                             /* status check */
#define  FNC_WD         001                             /* write */
#define  FNC_RD         002                             /* read */
#define  FNC_SEEK       003                             /* seek */
#define  FNC_REF        005                             /* refine */
#define  FNC_CHK        006                             /* check */
#define  FNC_INIT       011                             /* init */
#define  FNC_AR         013                             /* address */
#define  FNC_SEEK1      020                             /* fake - seek1 */
#define  FNC_SEEK2      021                             /* fake - seek2 */
#define  FNC_SEEK3      022                             /* fake - seek3 */
#define  FNC_CHK1       023                             /* fake - check1 */
#define  FNC_AR1        024                             /* fake - arec1 */
#define CW_V_DRV        0                               /* drive */
#define CW_M_DRV        03
#define CW_GETDRV(x)    (((x) >> CW_V_DRV) & CW_M_DRV)

/* Disk address words */

#define DA_V_CYL        0                               /* cylinder */
#define DA_M_CYL        0377
#define DA_GETCYL(x)    (((x) >> DA_V_CYL) & DA_M_CYL)
#define DA_V_HD         8                               /* head */
#define DA_M_HD         03
#define DA_GETHD(x)     (((x) >> DA_V_HD) & DA_M_HD)
#define DA_V_SC         0                               /* sector */
#define DA_M_SC2        017
#define DA_M_SC3        037
#define DA_M_SC         (dp_ctype ? DA_M_SC3 : DA_M_SC2)
#define DA_GETSC(x)     (((x) >> DA_V_SC) & DA_M_SC)
#define DA_CKMASK2      037                             /* check mask */
#define DA_CKMASK3      077
#define DA_CKMASK       (dp_ctype ? DA_CKMASK3 : DA_CKMASK2)

/* Status in dpc_sta[drv], (u) = unused in 13210, (d) = dynamic */

#define STA_ATN         0100000                         /* attention (u) */
#define STA_1ST         0040000                         /* first status */
#define STA_OVR         0020000                         /* overrun */
#define STA_RWU         0010000                         /* rw unsafe NI (u) */
#define STA_ACU         0004000                         /* access unsafe NI */
#define STA_HUNT        0002000                         /* hunting NI (12557) */
#define STA_PROT        0002000                         /* protected (13210) */
#define STA_SKI         0001000                         /* incomplete NI (u) */
#define STA_SKE         0000400                         /* seek error */
/*                      0000200                            (unused) */
#define STA_NRDY        0000100                         /* not ready (d) */
#define STA_EOC         0000040                         /* end of cylinder */
#define STA_AER         0000020                         /* addr error */
#define STA_FLG         0000010                         /* flagged */
#define STA_BSY         0000004                         /* seeking */
#define STA_DTE         0000002                         /* data error */
#define STA_ERR         0000001                         /* any error (d) */

#define STA_ERSET2      (STA_1ST | STA_OVR | STA_RWU | STA_ACU | \
                         STA_SKI | STA_SKE | STA_NRDY | \
                         STA_EOC | STA_AER | STA_DTE)   /* 12557A error set */
#define STA_ERSET3      (STA_ATN | STA_1ST | STA_OVR | STA_RWU | STA_ACU | \
                         STA_SKI | STA_SKE | STA_NRDY | STA_EOC | STA_AER | \
                         STA_FLG | STA_BSY | STA_DTE)   /* 13210A error set */
#define STA_ANYERR      (dp_ctype ? STA_ERSET3 : STA_ERSET2)

#define STA_UNLOADED    (dp_ctype ? (STA_NRDY | STA_BSY) : STA_NRDY)
#define STA_MBZ13       (STA_ATN | STA_RWU | STA_SKI)   /* zero in 13210 */

struct {
    FLIP_FLOP command;                                  /* cch command flip-flop */
    FLIP_FLOP control;                                  /* cch control flip-flop */
    FLIP_FLOP flag;                                     /* cch flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* cch flag buffer flip-flop */
    } dpc = { CLEAR, CLEAR, CLEAR, CLEAR };

/* Controller types */

typedef enum {
    A12557,
    A13210
    } CNTLR_TYPE;

CNTLR_TYPE dp_ctype = A13210;                           /* ctrl type */
int32 dpc_busy = 0;                                     /* cch unit */
int32 dpc_poll = 0;                                     /* cch poll enable */
int32 dpc_cnt = 0;                                      /* check count */
int32 dpc_eoc = 0;                                      /* end of cyl */
int32 dpc_stime = 100;                                  /* seek time */
int32 dpc_ctime = 100;                                  /* command time */
int32 dpc_xtime = 5;                                    /* xfer time */
int32 dpc_dtime = 2;                                    /* dch time */
int32 dpd_obuf = 0, dpd_ibuf = 0;                       /* dch buffers */
int32 dpc_obuf = 0;                                     /* cch buffers */

struct {
    FLIP_FLOP command;                                  /* dch command flip-flop */
    FLIP_FLOP control;                                  /* dch control flip-flop */
    FLIP_FLOP flag;                                     /* dch flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* dch flag buffer flip-flop */
    } dpd = { CLEAR, CLEAR, CLEAR, CLEAR };

int32 dpd_xfer = 0;                                     /* xfer in prog */
int32 dpd_wval = 0;                                     /* write data valid */
int32 dp_ptr = 0;                                       /* buffer ptr */
uint8 dpc_rarc = 0;                                     /* RAR cylinder */
uint8 dpc_rarh = 0;                                     /* RAR head */
uint8 dpc_rars = 0;                                     /* RAR sector */
uint8 dpc_ucyl[DP_NUMDRV] = { 0 };                      /* unit cylinder */
uint16 dpc_sta[DP_NUMDRV] = { 0 };                      /* status regs */
uint16 dpxb[DP_NUMWD];                                  /* sector buffer */

DEVICE dpd_dev, dpc_dev;

IOHANDLER dpdio;
IOHANDLER dpcio;

t_stat dpc_svc (UNIT *uptr);
t_stat dpd_svc (UNIT *uptr);
t_stat dpc_reset (DEVICE *dptr);
t_stat dpc_attach (UNIT *uptr, CONST char *cptr);
t_stat dpc_detach (UNIT* uptr);
t_stat dpc_boot (int32 unitno, DEVICE *dptr);
void dp_god (int32 fnc, int32 drv, int32 time);
void dp_goc (int32 fnc, int32 drv, int32 time);
t_stat dpc_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
t_stat dp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* DPD data structures

   dpd_dev      DPD device descriptor
   dpd_unit     DPD unit list
   dpd_reg      DPD register list
*/

DIB dp_dib[] = {
    { &dpdio, DPD },
    { &dpcio, DPC }
    };

#define dpd_dib dp_dib[0]
#define dpc_dib dp_dib[1]

UNIT dpd_unit = { UDATA (&dpd_svc, 0, 0) };

REG dpd_reg[] = {
    { ORDATA (IBUF, dpd_ibuf, 16) },
    { ORDATA (OBUF, dpd_obuf, 16) },
    { BRDATA (DBUF, dpxb, 8, 16, DP_NUMWD) },
    { DRDATA (BPTR, dp_ptr, DP_N_NUMWD) },
    { FLDATA (CMD, dpd.command, 0) },
    { FLDATA (CTL, dpd.control, 0) },
    { FLDATA (FLG, dpd.flag,    0) },
    { FLDATA (FBF, dpd.flagbuf, 0) },
    { FLDATA (XFER, dpd_xfer, 0) },
    { FLDATA (WVAL, dpd_wval, 0) },
    { ORDATA (SC, dpd_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, dpd_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB dpd_mod[] = {
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &dpd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &dpd_dev },
    { 0 }
    };

DEVICE dpd_dev = {
    "DPD", &dpd_unit, dpd_reg, dpd_mod,
    1, 10, DP_N_NUMWD, 1, 8, 16,
    NULL, NULL, &dpc_reset,
    NULL, NULL, NULL,
    &dpd_dib, DEV_DISABLE
    };

/* DPC data structures

   dpc_dev      DPC device descriptor
   dpc_unit     DPC unit list
   dpc_reg      DPC register list
   dpc_mod      DPC modifier list
*/

UNIT dpc_unit[] = {
    { UDATA (&dpc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DP_SIZE3) },
    { UDATA (&dpc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DP_SIZE3) },
    { UDATA (&dpc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DP_SIZE3) },
    { UDATA (&dpc_svc, UNIT_FIX | UNIT_ATTABLE | UNIT_ROABLE |
             UNIT_DISABLE | UNIT_UNLOAD, DP_SIZE3) }
    };

REG dpc_reg[] = {
    { ORDATA (OBUF, dpc_obuf, 16) },
    { ORDATA (BUSY, dpc_busy, 4), REG_RO },
    { ORDATA (CNT, dpc_cnt, 5) },
    { FLDATA (CMD, dpc.command, 0) },
    { FLDATA (CTL, dpc.control, 0) },
    { FLDATA (FLG, dpc.flag,    0) },
    { FLDATA (FBF, dpc.flagbuf, 0) },
    { FLDATA (EOC, dpc_eoc, 0) },
    { FLDATA (POLL, dpc_poll, 0) },
    { DRDATA (RARC, dpc_rarc, 8), PV_RZRO | REG_FIT },
    { DRDATA (RARH, dpc_rarh, 2), PV_RZRO | REG_FIT },
    { DRDATA (RARS, dpc_rars, 5), PV_RZRO | REG_FIT },
    { BRDATA (CYL, dpc_ucyl, 10, 8, DP_NUMDRV), PV_RZRO },
    { BRDATA (STA, dpc_sta, 8, 16, DP_NUMDRV) },
    { DRDATA (CTIME, dpc_ctime, 24), PV_LEFT },
    { DRDATA (DTIME, dpc_dtime, 24), PV_LEFT },
    { DRDATA (STIME, dpc_stime, 24), PV_LEFT },
    { DRDATA (XTIME, dpc_xtime, 24), REG_NZ | PV_LEFT },
    { FLDATA (CTYPE, dp_ctype, 0), REG_HRO },
    { URDATA (UFNC, dpc_unit[0].FNC, 8, 8, 0,
              DP_NUMDRV, REG_HRO) },
    { URDATA (CAPAC, dpc_unit[0].capac, 10, T_ADDR_W, 0,
              DP_NUMDRV, PV_LEFT | REG_HRO) },
    { ORDATA (SC, dpc_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, dpc_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB dpc_mod[] = {
    { UNIT_UNLOAD, UNIT_UNLOAD, "heads unloaded", "UNLOADED", dpc_load_unload },
    { UNIT_UNLOAD, 0, "heads loaded", "LOADED", dpc_load_unload },
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "13210A",
      &dp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "12557A",
      &dp_settype, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, "TYPE", NULL,
      NULL, &dp_showtype, NULL },
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &dpd_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &dpd_dev },
    { 0 }
    };

DEVICE dpc_dev = {
    "DPC", dpc_unit, dpc_reg, dpc_mod,
    DP_NUMDRV, 8, 24, 1, 8, 16,
    NULL, NULL, &dpc_reset,
    &dpc_boot, &dpc_attach, &dpc_detach,
    &dpc_dib, DEV_DISABLE
    };


/* Data channel I/O signal handler.

   For the 12557A, the card contains the usual control, flag, and flag buffer
   flip-flops.  PRL, IRQ, and SRQ are standard.  A command flip-flop indicates
   that data is available.

   For the 13210A, the card has a flag and a flag buffer flip-flop, but no
   control or interrupt flip-flop.  SRQ is standard.  IRQ and PRL are not
   driven, and the card does not respond to IAK.  STC sets the command flip-flop
   to initiate a data transfer.  CLC has no effect.

   Implementation notes:

    1. The CRS signal clears the drive attention register.  Under simulation,
       drive attention status is generated dynamically, so there is no attention
       register.
*/

uint32 dpdio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            dpd.flag = dpd.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            dpd.flag = dpd.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (dpd);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (dpd);
            break;


        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, dpd_ibuf);   /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            dpd_obuf = IODATA (stat_data);              /* clear supplied status */

            if (!dpc_busy || dpd_xfer)                  /* if !overrun */
                dpd_wval = 1;                           /* valid */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            dpd.flag = dpd.flagbuf = SET;               /* set flag buffer and flag */

            if (dp_ctype == A12557)                     /* 12557? */
                dpd_obuf = 0;                           /* clear output buffer */
            break;


        case ioCRS:                                     /* control reset */
            dpd.command = CLEAR;                        /* clear command */

            if (dp_ctype == A12557)                     /* 12557? */
                dpd.control = CLEAR;                    /* clear control */

            else {                                      /* 13210 */
                dpc_rarc = 0;                           /* clear controller cylinder address */
                dpc_ucyl [CW_GETDRV (dpc_obuf)] = 0;    /* clear last drive addressed cylinder */
                }
            break;


        case ioCLC:                                     /* clear control flip-flop */
            if (dp_ctype == A12557)                     /* 12557? */
                dpd.control = CLEAR;                    /* clear control */

            dpd_xfer = 0;                               /* clr xfer in progress */
            break;


        case ioSTC:                                     /* set control flip-flop */
            if (dp_ctype == A12557)                     /* 12557? */
                dpd.control = SET;                      /* set control */

            dpd.command = SET;                          /* set cmd */

            if (dpc_busy && !dpd_xfer)                  /* overrun? */
                dpc_sta[dpc_busy - 1] |= STA_OVR;
            break;


        case ioSIR:                                     /* set interrupt request */
            if (dp_ctype == A12557) {                   /* 12557? */
                setstdPRL (dpd);                        /* set standard PRL signal */
                setstdIRQ (dpd);                        /* set standard IRQ signal */
                }

            setstdSRQ (dpd);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            if (dp_ctype == A12557)                     /* 12557? */
                dpd.flagbuf = CLEAR;                    /* clear flag buffer */
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Command channel I/O signal handler.

   The 12557A and 13210A have the usual control, flag, and flag buffer
   flip-flops.  Only the 12557A has a command flip-flop.  IRQ, PRL, and SRQ are
   standard.

   Implementation notes:

    1. In hardware, the command channel card passes PRH to PRL.  The data card
       actually drives PRL with the command channel's control and flag states,
       even though the command channel's control, flag, and flag buffer drive
       IRQH.  That is, the priority chain is broken at the data card, although
       the command card is interrupting.  This works in hardware, but we must
       break PRL at the command card under simulation to allow the command card
       to interrupt.
*/

uint32 dpcio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
int32 i, fnc, drv;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            dpc.flag = dpc.flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            dpc.flag = dpc.flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (dpc);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (dpc);
            break;


        case ioIOI:                                     /* I/O data input */
            data = 0;

            for (i = 0; i < DP_NUMDRV; i++)             /* form attention register value */
                if (dpc_sta[i] & STA_ATN)
                    data = data | (uint16) (1 << i);

            stat_data = IORETURN (SCPE_OK, data);       /* merge in return status */
            break;


        case ioIOO:                                     /* I/O data output */
            dpc_obuf = IODATA (stat_data);              /* clear supplied status */

            if (dp_ctype == A13210)                     /* 13210? */
                dpcio (dibptr, ioCLC, 0);               /* OTx causes CLC */
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            dpc.flag = dpc.flagbuf = SET;               /* set flag buffer and flag */

            if (dp_ctype == A12557)                     /* 12557? */
                dpd_obuf = 0;                           /* clear output buffer */
            break;


        case ioCRS:                                     /* control reset */
            dpc.control = CLEAR;                        /* clear control */

            if (dp_ctype == A12557)                     /* 12557? */
                dpc.command = CLEAR;                    /* clear command */
            break;


        case ioCLC:                                     /* clear control flip-flop */
            dpc.control = CLEAR;                        /* clr ctl */

            if (dp_ctype == A12557)                     /* 12557? */
                dpc.command = CLEAR;                    /* cancel non-seek */

            if (dpc_busy)
                sim_cancel (&dpc_unit[dpc_busy - 1]);

            sim_cancel (&dpd_unit);                     /* cancel dch */
            dpd_xfer = 0;                               /* clr dch xfer */
            dpc_busy = 0;                               /* clr cch busy */
            dpc_poll = 0;                               /* clr cch poll */
            break;


        case ioSTC:                                     /* set control flip-flop */
            dpc.control = SET;                          /* set ctl */

            if ((dp_ctype == A13210) || !dpc.command) { /* 13210 or command is clear? */
                if (dp_ctype == A12557)                 /* 12557? */
                    dpc.command = SET;                  /* set command */

                drv = CW_GETDRV (dpc_obuf);             /* get fnc, drv */
                fnc = CW_GETFNC (dpc_obuf);             /* from cmd word */

                switch (fnc) {                          /* case on fnc */

                    case FNC_SEEK:                      /* seek */
                        dpc_poll = 1;                   /* enable polling */
                        dp_god (fnc, drv, dpc_dtime);   /* sched dch xfr */
                        break;

                    case FNC_STA:                       /* rd sta */
                        if (dp_ctype == A13210)         /* 13210? clr dch flag */
                            dpdio (&dpd_dib, ioCLF, 0);

                    case FNC_CHK:                       /* check */
                    case FNC_AR:                        /* addr rec */
                        dp_god (fnc, drv, dpc_dtime);   /* sched dch xfr */
                        break;

                    case FNC_RD: case FNC_WD:           /* read, write */
                    case FNC_REF: case FNC_INIT:        /* refine, init */
                        dp_goc (fnc, drv, dpc_ctime);   /* sched drive */
                        break;
                    }                                   /* end case */
                }                                       /* end if */
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (dpc);                            /* set standard PRL signal */
            setstdIRQ (dpc);                            /* set standard IRQ signal */
            setstdSRQ (dpc);                            /* set standard SRQ signal */
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            dpc.flagbuf = CLEAR;                        /* clear flag buffer */
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Start data channel operation */

void dp_god (int32 fnc, int32 drv, int32 time)
{
dpd_unit.DRV = drv;                                     /* save unit */
dpd_unit.FNC = fnc;                                     /* save function */
sim_activate (&dpd_unit, time);
return;
}


/* Start controller operation */

void dp_goc (int32 fnc, int32 drv, int32 time)
{
int32 t;

t = sim_activate_time (&dpc_unit[drv]);
if (t) {                                                /* still seeking? */
    sim_cancel (&dpc_unit[drv]);                        /* stop seek */
    dpc_sta[drv] = dpc_sta[drv] & ~STA_BSY;             /* clear busy */
    time = time + t;                                    /* include seek time */
    }
dp_ptr = 0;                                             /* init buf ptr */
dpc_eoc = 0;                                            /* clear end cyl */
dpc_busy = drv + 1;                                     /* set busy */
dpd_xfer = 1;                                           /* xfer in prog */
dpc_unit[drv].FNC = fnc;                                /* save function */
dpc_sta[drv] = dpc_sta[drv] & ~STA_ATN;                 /* clear ATN */
sim_activate (&dpc_unit[drv], time);                    /* activate unit */
return;
}


/* Data channel unit service

   This routine handles the data channel transfers.  It also handles
   data transfers that are blocked by seek in progress.

   uptr->DRV    =       target drive
   uptr->FNC    =       target function

   Seek substates
        seek    -       transfer cylinder
        seek1   -       transfer head/surface
   Address record
        ar      -       transfer cylinder
        ar1     -       transfer head/surface, finish operation
   Status check -       transfer status, finish operation
   Check data
        chk     -       transfer sector count
*/

t_stat dpd_svc (UNIT *uptr)
{
int32 i, drv, st;

drv = uptr->DRV;                                        /* get drive no */
switch (uptr->FNC) {                                    /* case function */

    case FNC_AR:                                        /* arec, need cyl */
    case FNC_SEEK:                                      /* seek, need cyl */
        if (dpd.command) {                              /* dch active? */
            dpc_rarc = DA_GETCYL (dpd_obuf);            /* set RAR from cyl word */
            dpd_wval = 0;                               /* clr data valid */

            dpd.command = CLEAR;                        /* clr dch cmd */
            dpdio (&dpd_dib, ioENF, 0);                 /* set dch flg */

            if (uptr->FNC == FNC_AR) uptr->FNC = FNC_AR1;
            else uptr->FNC = FNC_SEEK1;                 /* advance state */
            }
        sim_activate (uptr, dpc_xtime);                 /* no, wait more */
        break;

    case FNC_AR1:                                       /* arec, need hd/sec */
    case FNC_SEEK1:                                     /* seek, need hd/sec */
        if (dpd.command) {                              /* dch active? */
            dpc_rarh = DA_GETHD (dpd_obuf);             /* set RAR from head */
            dpc_rars = DA_GETSC (dpd_obuf);             /* set RAR from sector */
            dpd_wval = 0;                               /* clr data valid */

            dpd.command = CLEAR;                        /* clr dch cmd */
            dpdio (&dpd_dib, ioENF, 0);                 /* set dch flg */

            if (uptr->FNC == FNC_AR1) {
                dpc.command = CLEAR;                    /* clr cch cmd */
                dpcio (&dpc_dib, ioENF, 0);             /* set cch flg */

                dpc_sta[drv] = dpc_sta[drv] | STA_ATN;  /* set drv attn */
                break;                                  /* done if Address Record */
                }
            if (sim_is_active (&dpc_unit[drv])) {       /* if busy, */
                dpc_sta[drv] = dpc_sta[drv] | STA_SKE;  /* seek check */
                break;                                  /* allow prev seek to cmpl */
                }
            if ((dpc_rarc >= DP_NUMCY) ||               /* invalid cyl? */
                ((dp_ctype == A13210) &&                /*   or 13210A */
                 (dpc_rars >= DP_NUMSC3))) {            /*   and invalid sector? */
                dpc_sta[drv] = dpc_sta[drv] | STA_SKE;  /* seek check */
                sim_activate (&dpc_unit[drv], 1);       /* schedule drive no-wait */
                dpc_unit[drv].FNC = FNC_SEEK3;          /* do immed compl w/poll */
                break;
                }
            st = abs (dpc_rarc - dpc_ucyl[drv]) * dpc_stime;
            if (st == 0) st = dpc_stime;                /* min time */
            dpc_ucyl[drv] = dpc_rarc;                   /* transfer RAR */
            sim_activate (&dpc_unit[drv], st);          /* schedule drive */
            dpc_sta[drv] = (dpc_sta[drv] | STA_BSY) &
                ~(STA_SKE | STA_SKI | STA_HUNT);
            dpc_unit[drv].FNC = FNC_SEEK2;              /* set operation */
            }
        else sim_activate (uptr, dpc_xtime);            /* no, wait more */
        break;

    case FNC_STA:                                       /* read status */
        if (dpd.command || (dp_ctype == A13210)) {      /* dch act or 13210? */
            if ((dpc_unit[drv].flags & UNIT_UNLOAD) == 0) {  /* drive up? */
                dpd_ibuf = dpc_sta[drv] & ~STA_ERR;     /* clear err */
                if (dp_ctype == A13210) dpd_ibuf =      /* 13210? */
                    (dpd_ibuf & ~(STA_MBZ13 | STA_PROT)) |
                    (dpc_unit[drv].flags & UNIT_WPRT? STA_PROT: 0);
                }
            else dpd_ibuf = STA_UNLOADED;               /* not ready */
            if (dpd_ibuf & STA_ANYERR)                  /* errors? set flg */
                dpd_ibuf = dpd_ibuf | STA_ERR;

            dpc.command = CLEAR;                        /* clr cch cmd */
            dpd.command = CLEAR;                        /* clr dch cmd */
            dpdio (&dpd_dib, ioENF, 0);                 /* set dch flg */
            }

        dpc_sta[drv] = dpc_sta[drv] &                   /* clr sta flags */
            ~(STA_ATN | STA_1ST | STA_OVR |
            STA_RWU | STA_ACU | STA_EOC |
            STA_AER | STA_FLG | STA_DTE);
        dpc_poll = 1;                                   /* enable polling */
        for (i = 0; i < DP_NUMDRV; i++) {               /* loop thru drives */
            if (dpc_sta[i] & STA_ATN) {                 /* any ATN set? */
                dpcio (&dpc_dib, ioENF, 0);             /* set cch flg */
                break;
                }
            }
        break;

    case FNC_CHK:                                       /* check, need cnt */
        if (dpd.command) {                              /* dch active? */
            dpc_cnt = dpd_obuf & DA_CKMASK;             /* get count */
            dpd_wval = 0;                               /* clr data valid */
            dp_goc (FNC_CHK1, drv, dpc_xtime);          /* sched drv */
            }
        else sim_activate (uptr, dpc_xtime);            /* wait more */
        break;

    default:
        return SCPE_IERR;
        }

return SCPE_OK;
}


/* Drive unit service

   This routine handles the data transfers.

   Seek substates
        seek2   -       done
   Refine sector -      erase sector, finish operation
   Check data
        chk1    -       finish operation
   Read
   Write
*/

#define GETDA(x,y,z) \
    (((((x) * DP_NUMSF) + (y)) * DP_NUMSC) + (z)) * DP_NUMWD

t_stat dpc_svc (UNIT *uptr)
{
int32 da, drv, err;

err = 0;                                                /* assume no err */
drv = uptr - dpc_unit;                                  /* get drive no */
if (uptr->flags & UNIT_UNLOAD) {                        /* drive down? */

    dpc.command = CLEAR;                                /* clr cch cmd */
    dpcio (&dpc_dib, ioENF, 0);                         /* set cch flg */

    dpc_sta[drv] = 0;                                   /* clr status */
    dpc_busy = 0;                                       /* ctlr is free */
    dpc_poll = 0;                                       /* polling disabled */
    dpd_xfer = 0;
    dpd_wval = 0;
    return SCPE_OK;
    }
switch (uptr->FNC) {                                    /* case function */

    case FNC_SEEK2:                                     /* positioning done */
        dpc_sta[drv] = (dpc_sta[drv] | STA_ATN) & ~STA_BSY;  /* fall into cmpl */
    case FNC_SEEK3:                                     /* seek complete */
        if (dpc_poll) {                                 /* polling enabled? */
            dpc.command = CLEAR;                        /* clr cch cmd */
            dpcio (&dpc_dib, ioENF, 0);                 /* set cch flg */
            }
        return SCPE_OK;

    case FNC_REF:                                       /* refine sector */
        break;                                          /* just a NOP */

    case FNC_RD:                                        /* read */
    case FNC_CHK1:                                      /* check */
        if (dp_ptr == 0) {                              /* new sector? */
            if (!dpd.command && (uptr->FNC != FNC_CHK1)) break;
            if (dpc_rarc != dpc_ucyl[drv])              /* RAR cyl miscompare? */
                dpc_sta[drv] = dpc_sta[drv] | STA_AER;  /* set flag, read */
            if (dpc_rars >= DP_NUMSC) {                 /* bad sector? */
                dpc_sta[drv] = dpc_sta[drv] | STA_AER;  /* set flag, stop */
                break;
                }
            if (dpc_eoc) {                              /* end of cyl? */
                dpc_sta[drv] = dpc_sta[drv] | STA_EOC;
                break;
                }
            da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);  /* calc disk addr */
            dpc_rars = (dpc_rars + 1) % DP_NUMSC;       /* incr sector */
            if (dpc_rars == 0) {                        /* wrap? */
                dpc_rarh = dpc_rarh ^ 1;                /* incr head */
                dpc_eoc = ((dpc_rarh & 1) == 0);        /* calc eoc */
                }
            err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);
            if (err)                                    /* error? */
                 break;
            fxread (dpxb, sizeof (int16), DP_NUMWD, uptr->fileref);
            err = ferror (uptr->fileref);
            if (err)                                    /* error? */
                 break;
            }
        dpd_ibuf = dpxb[dp_ptr++];                      /* get word */
        if (dp_ptr >= DP_NUMWD) {                       /* end of sector? */
            if (uptr->FNC == FNC_CHK1) {                /* check? */
                dpc_cnt = (dpc_cnt - 1) & DA_CKMASK;    /* decr count */
                if (dpc_cnt == 0) break;                /* stop at zero */
                }
            dp_ptr = 0;                                 /* wrap buf ptr */
            }
        if (dpd.command && dpd_xfer)                    /* dch on, xfer? */
            dpdio (&dpd_dib, ioENF, 0);                 /* set dch flg */

        dpd.command = CLEAR;                            /* clr dch cmd */
        sim_activate (uptr, dpc_xtime);                 /* sched next word */
        return SCPE_OK;

    case FNC_INIT:                                      /* init */
    case FNC_WD:                                        /* write */
        if (dp_ptr == 0) {                              /* start sector? */
            if (!dpd.command && !dpd_wval) break;       /* xfer done? */
            if (uptr->flags & UNIT_WPRT) {              /* wr prot? */
                dpc_sta[drv] = dpc_sta[drv] | STA_FLG;  /* set status */
                break;                                  /* done */
                }
            if ((dpc_rarc != dpc_ucyl[drv]) ||          /* RAR cyl miscompare? */
                (dpc_rars >= DP_NUMSC)) {               /* bad sector? */
                dpc_sta[drv] = dpc_sta[drv] | STA_AER;  /* address error */
                break;
                }
            if (dpc_eoc) {                              /* end of cyl? */
                dpc_sta[drv] = dpc_sta[drv] | STA_EOC;  /* set status */
                break;                                  /* done */
                }
            }
        dpxb[dp_ptr++] = dpd_wval ? (uint16) dpd_obuf : 0;  /* store word/fill */
        dpd_wval = 0;                                   /* clr data valid */
        if (dp_ptr >= DP_NUMWD) {                       /* buffer full? */
            da = GETDA (dpc_rarc, dpc_rarh, dpc_rars);  /* calc disk addr */
            dpc_rars = (dpc_rars + 1) % DP_NUMSC;       /* incr sector */
            if (dpc_rars == 0) {                        /* wrap? */
                dpc_rarh = dpc_rarh ^ 1;                /* incr head */
                dpc_eoc = ((dpc_rarh & 1) == 0);        /* calc eoc */
                }
            err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);
            if (err)                                    /* error? */
                 break;
            fxwrite (dpxb, sizeof (int16), DP_NUMWD, uptr->fileref);
            err = ferror (uptr->fileref);
            if (err)                                    /* error? */
                 break;
            dp_ptr = 0;                                 /* next sector */
            }
        if (dpd.command && dpd_xfer)                    /* dch on, xfer? */
            dpdio (&dpd_dib, ioENF, 0);                 /* set dch flg */

        dpd.command = CLEAR;                            /* clr dch cmd */
        sim_activate (uptr, dpc_xtime);                 /* sched next word */
        return SCPE_OK;

    default:
        return SCPE_IERR;
        }                                               /* end case fnc */

dpc_sta[drv] = dpc_sta[drv] | STA_ATN;                  /* set ATN */

dpc.command = CLEAR;                                    /* clr cch cmd */
dpcio (&dpc_dib, ioENF, 0);                             /* set cch flg */

dpc_busy = 0;                                           /* ctlr is free */
dpd_xfer = dpd_wval = 0;

if (err != 0) {                                         /* error? */
    perror ("DP I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}


/* Reset routine */

t_stat dpc_reset (DEVICE *dptr)
{
int32 drv;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */

hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &dpd_dev) ? &dpc_dev : &dpd_dev);

if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    dpd_ibuf = dpd_obuf = 0;                            /* clear buffers */
    dpc_obuf = 0;                                       /* clear buffer */
    dpc_rarc = dpc_rarh = dpc_rars = 0;                 /* clear RAR */
    }

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

dpc_busy = 0;                                           /* reset controller state */
dpc_poll = 0;
dpd_xfer = 0;
dpd_wval = 0;
dpc_eoc = 0;
dp_ptr = 0;

sim_cancel (&dpd_unit);                                 /* cancel dch */

for (drv = 0; drv < DP_NUMDRV; drv++) {                 /* loop thru drives */
    sim_cancel (&dpc_unit[drv]);                        /* cancel activity */
    dpc_unit[drv].FNC = 0;                              /* clear function */
    dpc_ucyl[drv] = 0;                                  /* clear drive pos */
    if (dpc_unit[drv].flags & UNIT_ATT)
        dpc_sta[drv] = dpc_sta[drv] & STA_1ST;          /* first seek status */
    else dpc_sta[drv] = 0;                              /* clear status */
    }

return SCPE_OK;
}


/* Attach routine */

t_stat dpc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);                           /* attach unit */
if (r == SCPE_OK) dpc_load_unload (uptr, 0, NULL, NULL);/* if OK, load heads */
return r;
}


/* Detach routine */

t_stat dpc_detach (UNIT* uptr)
{
dpc_load_unload (uptr, UNIT_UNLOAD, NULL, NULL);        /* unload heads */
return detach_unit (uptr);                              /* detach unit */
}


/* Load and unload heads */

t_stat dpc_load_unload (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
int32 drv;

if ((uptr->flags & UNIT_ATT) == 0) return SCPE_UNATT;   /* must be attached to load */

if (value == UNIT_UNLOAD)                               /* unload heads? */
    uptr->flags = uptr->flags | UNIT_UNLOAD;            /* indicate unload */
else {                                                  /* load heads */
    uptr->flags = uptr->flags & ~UNIT_UNLOAD;           /* indicate load */
    drv = uptr - dpc_unit;                              /* get drive no */
    dpc_sta[drv] = dpc_sta[drv] | STA_ATN | STA_1ST;    /* update status */
    if (dpc_poll)                                       /* polling enabled? */
        dpcio (&dpc_dib, ioENF, 0);                     /* set flag */
    }
return SCPE_OK;
}


/* Set controller type */

t_stat dp_settype (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;

if ((val < 0) || (val > 1) || (cptr != NULL))
    return SCPE_ARG;

for (i = 0; i < DP_NUMDRV; i++) {
    if (dpc_unit[i].flags & UNIT_ATT)
        return SCPE_ALATT;
    }

for (i = 0; i < DP_NUMDRV; i++)
    dpc_unit[i].capac = (val? DP_SIZE3: DP_SIZE2);

dp_ctype = (CNTLR_TYPE) val;
return SCPE_OK;
}


/* Show controller type */

t_stat dp_showtype (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (dp_ctype == A13210)
    fprintf (st, "13210A");
else
    fprintf (st, "12557A");

return SCPE_OK;
}


/* 7900/7901 bootstrap routine (HP 12992F ROM) */

const BOOT_ROM dp_rom = {
    0106710,                    /*ST CLC DC             ; clr dch */
    0106711,                    /*   CLC CC             ; clr cch */
    0017757,                    /*   JSB STAT           ; get status */
    0067746,                    /*SK LDB SKCMD          ; seek cmd */
    0106610,                    /*   OTB DC             ; cyl # */
    0103710,                    /*   STC DC,C           ; to dch */
    0106611,                    /*   OTB CC             ; seek cmd */
    0103711,                    /*   STC CC,C           ; to cch */
    0102310,                    /*   SFS DC             ; addr wd ok? */
    0027710,                    /*   JMP *-1            ; no, wait */
    0006400,                    /*   CLB */
    0102501,                    /*   LIA 1              ; read switches */
    0002011,                    /*   SLA,RSS            ; <0> set? */
    0047747,                    /*   ADB BIT9           ; head 2 = removable */
    0106610,                    /*   OTB DC             ; head/sector */
    0103710,                    /*   STC DC,C           ; to dch */
    0102311,                    /*   SFS CC             ; seek done? */
    0027720,                    /*   JMP *-1            ; no, wait */
    0017757,                    /*   JSB STAT           ; get status */
    0067776,                    /*   LDB DMACW          ; DMA control */
    0106606,                    /*   OTB 6 */
    0067750,                    /*   LDB ADDR1          ; memory addr */
    0106602,                    /*   OTB 2 */
    0102702,                    /*   STC 2              ; flip DMA ctrl */
    0067752,                    /*   LDB CNT            ; word count */
    0106602,                    /*   OTB 2 */
    0063745,                    /*   LDB RDCMD          ; read cmd */
    0102611,                    /*   OTA CC             ; to cch */
    0103710,                    /*   STC DC,C           ; start dch */
    0103706,                    /*   STC 6,C            ; start DMA */
    0103711,                    /*   STC CC,C           ; start cch */
    0102311,                    /*   SFS CC             ; done? */
    0027737,                    /*   JMP *-1            ; no, wait */
    0017757,                    /*   JSB STAT           ; get status */
    0027775,                    /*   JMP XT             ; done */
    0037766,                    /*FSMSK 037766          ; status mask */
    0004000,                    /*STMSK 004000          ; unsafe mask */
    0020000,                    /*RDCMD 020000          ; read cmd */
    0030000,                    /*SKCMD 030000          ; seek cmd */
    0001000,                    /*BIT9  001000          ; head 2 select */
    0102011,                    /*ADDR1 102011 */
    0102055,                    /*ADDR2 102055 */
    0164000,                    /*CNT   -6144. */
    0, 0, 0, 0,                 /* unused */
    0000000,                    /*STAT 0 */
    0002400,                    /*   CLA                ; status request */
    0102611,                    /*   OTC CC             ; to cch */
    0103711,                    /*   STC CC,C           ; start cch */
    0102310,                    /*   SFS DC             ; done? */
    0027763,                    /*   JMP *-1 */
    0102510,                    /*   LIA DC             ; get status */
    0013743,                    /*   AND FSMSK          ; mask 15,14,3,0 */
    0002003,                    /*   SZA,RSS            ; drive ready? */
    0127757,                    /*   JMP STAT,I         ; yes */
    0013744,                    /*   AND STMSK          ; fault? */
    0002002,                    /*   SZA */
    0102030,                    /*   HLT 30             ; yes */
    0027700,                    /*   JMP ST             ; no, retry */
    0117751,                    /*XT JSB ADDR2,I        ; start program */
    0120010,                    /*DMACW 120000+DC */
    0000000                     /*   -ST */
    };

t_stat dpc_boot (int32 unitno, DEVICE *dptr)
{
const int32 dev = dpd_dib.select_code;                  /* data chan select code */

if (unitno != 0)                                        /* boot supported on drive unit 0 only */
    return SCPE_NOFNC;                                  /* report "Command not allowed" if attempted */

if (ibl_copy (dp_rom, dev, IBL_OPT,                     /* copy the boot ROM to memory and configure */
              IBL_DP | IBL_SET_SC (dev)                 /*   the S register accordingly */
                | (sim_switches & SWMASK ('R') ? IBL_DP_REM : 0)))
    return SCPE_IERR;                                   /* return an internal error if the copy failed */
else
    return SCPE_OK;
}
