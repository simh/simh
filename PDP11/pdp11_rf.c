/* pdp11_rf.c: RF11 fixed head disk simulator

   Copyright (c) 2006-2013, Robert M Supnik

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

   rf           RF11 fixed head disk

   03-Sep-13    RMS     Added explicit void * cast
   19-Mar-12    RMS     Fixed bug in updating mem addr extension (Peter Schorn)
   25-Dec-06    RMS     Fixed bug in unit mask (John Dundas)
   26-Jun-06    RMS     Cloned from RF08 simulator

   The RF11 is a head-per-track disk.  To minimize overhead, the entire RF11
   is buffered in memory.

   Two timing parameters are provided:

   rf_time      Interword timing, must be non-zero
   rf_burst     Burst mode, if 0, DMA occurs cycle by cycle; otherwise,
                DMA occurs in a burst
*/

#include "pdp11_defs.h"
#include <math.h>

#define UNIT_V_AUTO     (UNIT_V_UF + 0)                 /* autosize */
#define UNIT_V_PLAT     (UNIT_V_UF + 1)                 /* #platters - 1 */
#define UNIT_M_PLAT     (RF_NUMDK - 1)
#define UNIT_GETP(x)    ((((x) >> UNIT_V_PLAT) & UNIT_M_PLAT) + 1)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_PLAT       (UNIT_M_PLAT << UNIT_V_PLAT)

/* Constants */

#define RF_NUMWD        2048                            /* words/track */
#define RF_NUMTR        128                             /* tracks/disk */
#define RF_DKSIZE       (RF_NUMTR * RF_NUMWD)           /* words/disk */
#define RF_NUMDK        8                               /* disks/controller */
#define RF_WMASK        (RF_NUMWD - 1)                  /* word mask */

/* Parameters in the unit descriptor */

#define FUNC            u4                              /* function */

/* Status register */

#define RFCS_ERR        (CSR_ERR)                       /* error */
#define RFCS_FRZ        0040000                         /* error freeze */
#define RFCS_WCHK       0020000                         /* write check */
#define RFCS_DPAR       0010000                         /* data parity (ni) */
#define RFCS_NED        0004000                         /* nx disk */
#define RFCS_WLK        0002000                         /* write lock */
#define RFCS_MXFR       0001000                         /* missed xfer (ni) */
#define RFCS_CLR        0000400                         /* clear */
#define RFCS_DONE       (CSR_DONE)
#define RFCS_IE         (CSR_IE)
#define RFCS_M_MEX      0000003                         /* memory extension */
#define RFCS_V_MEX      4
#define RFCS_MEX        (RFCS_M_MEX << RFCS_V_MEX)
#define RFCS_MAINT      0000010                         /* maint */
#define RFCS_M_FUNC     0000003                         /* function */
#define  RFNC_NOP       0
#define  RFNC_WRITE     1
#define  RFNC_READ      2
#define  RFNC_WCHK      3
#define RFCS_V_FUNC     1
#define RFCS_FUNC       (RFCS_M_FUNC << RFCS_V_FUNC)
#define RFCS_GO         0000001
#define RFCS_ALLERR     (RFCS_FRZ|RFCS_WCHK|RFCS_DPAR|RFCS_NED|RFCS_WLK|RFCS_MXFR)
#define RFCS_W          (RFCS_IE|RFCS_MEX|RFCS_FUNC)

/* Current memory address */

#define RFCMA_RW        0177776

/* Address extension */

#define RFDAE_ALLERR    0176000
#define RFDAE_NXM       0002000
#define RFDAE_INH       0000400                         /* addr inhibit */
#define RFDAE_RLAT      0000200                         /* req late */
#define RFDAE_DAE       0000077                         /* extension */
#define RFDAE_R         0176677
#define RFDAE_W         0000677

#define GET_FUNC(x)     (((x) >> RFCS_V_FUNC) & RFCS_M_FUNC)
#define GET_MEX(x)      (((x) & RFCS_MEX) << (16 - RFCS_V_MEX))
#define GET_DEX(x)      (((x) & RFDAE_DAE) << 16)
#define GET_POS(x)      ((int) fmod (sim_gtime() / ((double) (x)), \
                        ((double) RF_NUMWD)))

extern uint16 *M;
extern int32 int_req[IPL_HLVL];

uint32 rf_cs = 0;                                       /* status register */
uint32 rf_cma = 0;
uint32 rf_wc = 0;
uint32 rf_da = 0;                                       /* disk address */
uint32 rf_dae = 0;
uint32 rf_dbr = 0;
uint32 rf_maint = 0;
uint32 rf_wlk = 0;                                      /* write lock */
uint32 rf_time = 10;                                    /* inter-word time */
uint32 rf_burst = 1;                                    /* burst mode flag */
uint32 rf_stopioe = 1;                                  /* stop on error */

DEVICE rf_dev;
t_stat rf_rd (int32 *data, int32 PA, int32 access);
t_stat rf_wr (int32 data, int32 PA, int32 access);
int32 rf_inta (void);
t_stat rf_svc (UNIT *uptr);
t_stat rf_reset (DEVICE *dptr);
t_stat rf_boot (int32 unitno, DEVICE *dptr);
t_stat rf_attach (UNIT *uptr, char *cptr);
t_stat rf_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
uint32 update_rfcs (uint32 newcs, uint32 newdae);
t_stat rf_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *rf_description (DEVICE *dptr);

/* RF11 data structures

   rf_dev       RF device descriptor
   rf_unit      RF unit descriptor
   rf_reg       RF register list
*/

#define IOLN_RF         020

DIB rf_dib = {
    IOBA_AUTO, IOLN_RF, &rf_rd, &rf_wr,
    1, IVCL (RF), VEC_AUTO, {NULL}, IOLN_RF,
    };


UNIT rf_unit = {
    UDATA (&rf_svc, UNIT_FIX+UNIT_ATTABLE+
           UNIT_BUFABLE+UNIT_MUSTBUF, RF_DKSIZE)
    };

REG rf_reg[] = {
    { ORDATAD (RFCS, rf_cs, 16, "control/status") },
    { ORDATAD (RFWC, rf_wc, 16, "word count") },
    { ORDATAD (RFCMA, rf_cma, 16, "current memory address") },
    { ORDATAD (RFDA, rf_da, 16, "current disk address") },
    { ORDATAD (RFDAE, rf_dae, 16, "disk address extension") },
    { ORDATAD (RFDBR, rf_dbr, 16, "data buffer") },
    { ORDATAD (RFMR, rf_maint, 16, "maintenance register") },
    { ORDATAD (RFWLK, rf_wlk, 32, "write lock switches") },
    { FLDATAD (INT, IREQ (RF), INT_V_RF, "interrupt pending flag") },
    { FLDATAD (ERR, rf_cs, CSR_V_ERR, "device error flag") },
    { FLDATAD (DONE, rf_cs, CSR_V_DONE, "device done flag") },
    { FLDATAD (IE, rf_cs, CSR_V_IE, "interrupt enable flag") },
    { DRDATAD (TIME, rf_time, 24, "rotational delay, per word"), REG_NZ + PV_LEFT },
    { FLDATAD (BURST, rf_burst, 0, "burst flag") },
    { FLDATAD (STOP_IOE, rf_stopioe, 0, "stop on I/O error") },
    { ORDATA (DEVADDR, rf_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, rf_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB rf_mod[] = {
    { UNIT_PLAT, (0 << UNIT_V_PLAT), NULL, "1P", &rf_set_size, NULL, NULL, "set drive to one platter (256K)" },
    { UNIT_PLAT, (1 << UNIT_V_PLAT), NULL, "2P", &rf_set_size, NULL, NULL, "set drive to two platters (512K)" },
    { UNIT_PLAT, (2 << UNIT_V_PLAT), NULL, "3P", &rf_set_size, NULL, NULL, "set drive to three platters (768K)" },
    { UNIT_PLAT, (3 << UNIT_V_PLAT), NULL, "4P", &rf_set_size, NULL, NULL, "set drive to four platters (1024K)" },
    { UNIT_PLAT, (4 << UNIT_V_PLAT), NULL, "5P", &rf_set_size, NULL, NULL, "set drive to five platters (1280K)" },
    { UNIT_PLAT, (5 << UNIT_V_PLAT), NULL, "6P", &rf_set_size, NULL, NULL, "set drive to six platters (1536K)" },
    { UNIT_PLAT, (6 << UNIT_V_PLAT), NULL, "7P", &rf_set_size, NULL, NULL, "set drive to seven platters (1792K)" },
    { UNIT_PLAT, (7 << UNIT_V_PLAT), NULL, "8P", &rf_set_size, NULL, NULL, "set drive to eight platters (2048K)" },
    { UNIT_AUTO, UNIT_AUTO, "autosize", "AUTOSIZE", NULL, NULL, NULL, "set drive to autosize platters" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEVICE rf_dev = {
    "RF", &rf_unit, rf_reg, rf_mod,
    1, 8, 21, 1, 8, 16,
    NULL, NULL, &rf_reset,
    &rf_boot, &rf_attach, NULL,
    &rf_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0,
    NULL, NULL, NULL, &rf_help, NULL, NULL,
    &rf_description
    };

/* I/O dispatch routine, I/O addresses 17777460 - 17777476 */

t_stat rf_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 07) {                               /* decode PA<3:1> */

    case 0:                                             /* RFCS */
        *data = update_rfcs (0, 0);                     /* update RFCS */
        break;

    case 1:                                             /* RFWC */
        *data = rf_wc;
        break;

    case 2:                                             /* RFCMA */
        *data = rf_cma & RFCMA_RW;
        break;

    case 3:                                             /* RFDA */
        *data = rf_da;
        break;

    case 4:                                             /* RFDAE */
        *data = rf_dae & RFDAE_R;
        break;

    case 5:                                             /* RFDBR */
        *data = rf_dbr;
        break;

    case 6:                                             /* RFMR */
        *data = rf_maint;
        break;

    case 7:                                             /* RFADS */
        *data = GET_POS (rf_time);
        break;
        }                                               /* end switch */
return SCPE_OK;
}

t_stat rf_wr (int32 data, int32 PA, int32 access)
{
int32 t, fnc;

switch ((PA >> 1) & 07) {                               /* decode PA<3:1> */

    case 0:                                             /* RFCS */
        if (access == WRITEB)
            data = (PA & 1)? (rf_cs & 0377) | (data << 8): (rf_cs & ~0377) | data;
        if (data & RFCS_CLR)                            /* clear? */
            rf_reset (&rf_dev);
        if ((data & RFCS_IE) == 0)                      /* int disable? */
            CLR_INT (RF);                               /* clr int request */
        else if ((rf_cs & (RFCS_DONE + RFCS_IE)) == RFCS_DONE)
            SET_INT (RF);                               /* set int request */
        rf_cs = (rf_cs & ~RFCS_W) | (data & RFCS_W);    /* merge */
        if ((rf_cs & RFCS_DONE) && (data & RFCS_GO) &&  /* new function? */
            ((fnc = GET_FUNC (rf_cs)) != RFNC_NOP)) {
            rf_unit.FUNC = fnc;                         /* save function */
            t = (rf_da & RF_WMASK) - GET_POS (rf_time); /* delta to new loc */
            if (t < 0)                                  /* wrap around? */
                t = t + RF_NUMWD;
            sim_activate (&rf_unit, t * rf_time);       /* schedule op */
            rf_cs &= ~(RFCS_WCHK|RFCS_DPAR|RFCS_NED|RFCS_WLK|RFCS_MXFR|RFCS_DONE);
            CLR_INT (RF);
            if (DEBUG_PRS (rf_dev))
                fprintf (sim_deb, ">>RF start: cs = %o, da = %o, ma = %o\n",
                    update_rfcs (0, 0), GET_DEX (rf_dae) | rf_da, GET_MEX (rf_cs) | rf_cma);
            }
        break;

    case 1:                                             /* RFWC */
        if (access == WRITEB)
            data = (PA & 1)? (rf_wc & 0377) | (data << 8): (rf_wc & ~0377) | data;
        rf_wc = data;
        break;

    case 2:                                             /* RFCMA */
        if (access == WRITEB)
            data = (PA & 1)? (rf_cma & 0377) | (data << 8): (rf_cma & ~0377) | data;
        rf_cma = data & RFCMA_RW;
        break;

    case 3:                                             /* RFDA */
        if (access == WRITEB)
            data = (PA & 1)? (rf_da & 0377) | (data << 8): (rf_da & ~0377) | data;
        rf_da = data;
        break;

    case 4:                                             /* RFDAE */
        if (access == WRITEB)
            data = (PA & 1)? (rf_dae & 0377) | (data << 8): (rf_dae & ~0377) | data;
        rf_dae = (rf_dae & ~RFDAE_W) | (data & RFDAE_W);
        break;

    case 5:                                             /* RFDBR */
        rf_dbr = data;
        break;

    case 6:                                             /* RFMR */
        rf_maint = data;
        break;

    case 7:                                             /* RFADS */
        break;                                          /* read only */
        }                                               /* end switch */

update_rfcs (0, 0);
return SCPE_OK;
}

/* Unit service

   Note that for reads and writes, memory addresses wrap around in the
   current field.  This code assumes the entire disk is buffered.
*/

t_stat rf_svc (UNIT *uptr)
{
uint32 ma, da, t;
uint16 dat;
uint16 *fbuf = (uint16 *) uptr->filebuf;

if ((uptr->flags & UNIT_BUF) == 0) {                    /* not buf? abort */
    update_rfcs (RFCS_NED|RFCS_DONE, 0);                /* nx disk */
    return IORETURN (rf_stopioe, SCPE_UNATT);
    }

ma = GET_MEX (rf_cs) | rf_cma;                          /* 18b mem addr */
da = GET_DEX (rf_dae) | rf_da;                          /* 22b disk addr */
do {
    if (da >= rf_unit.capac) {                          /* disk overflow? */
        update_rfcs (RFCS_NED, 0);
        break;
        }
    if (uptr->FUNC == RFNC_READ) {                      /* read? */
        dat = fbuf[da];                                 /* get disk data */
        rf_dbr = dat;
        if (Map_WriteW (ma, 2, &dat)) {                 /* store mem, nxm? */
            update_rfcs (0, RFDAE_NXM);
            break;
            }
        }
    else if (uptr->FUNC == RFNC_WCHK) {                 /* write check? */
        rf_dbr = fbuf[da];                              /* get disk data */
        if (Map_ReadW (ma, 2, &dat)) {                  /* read mem, nxm? */
            update_rfcs (0, RFDAE_NXM);
            break;
            }
        if (rf_dbr != dat) {                            /* miscompare? */
            update_rfcs (RFCS_WCHK, 0);
            break;
            }
        }
    else {                                              /* write */
        t = (da >> 15) & 037;
        if ((rf_wlk >> t) & 1) {                        /* write locked? */
            update_rfcs (RFCS_WLK, 0);
            break;
            }
        else {                                          /* not locked */
            if (Map_ReadW (ma, 2, &dat)) {              /* read mem, nxm? */
                update_rfcs (0, RFDAE_NXM);
                break;
                }
            fbuf[da] = dat;                            /* write word */
            rf_dbr = dat;
            if (da >= uptr->hwmark)
                uptr->hwmark = da + 1;
            }
        }
    da = (da + 1) & 017777777;                          /* incr disk addr */
    if ((rf_dae & RFDAE_INH) == 0)                      /* inhibit clear? */
        ma = (ma + 2) & UNIMASK;                        /* incr mem addr */
    rf_wc = (rf_wc + 1) & DMASK;                        /* incr word count */
    } while ((rf_wc != 0) && (rf_burst != 0));          /* brk if wc, no brst */

rf_da = da & DMASK;                                     /* split da */
rf_dae = (rf_dae & ~RFDAE_DAE) | ((rf_da >> 16) & RFDAE_DAE);
rf_cma = ma & DMASK;                                    /* split ma */
rf_cs = (rf_cs & ~RFCS_MEX) | ((ma >> (16 - RFCS_V_MEX)) & RFCS_MEX); 
if ((rf_wc != 0) && ((rf_cs & RFCS_ERR) == 0))          /* more to do? */
    sim_activate (&rf_unit, rf_time);                   /* sched next */
else {
    update_rfcs (RFCS_DONE, 0);
    if (DEBUG_PRS (rf_dev))
        fprintf (sim_deb, ">>RF done: cs = %o, dae = %o, da = %o, ma = %o, wc = %o\n",
            rf_cs, rf_dae, rf_da, rf_cma, rf_wc);
    }
return SCPE_OK;
}

/* Update CS register */

uint32 update_rfcs (uint32 newcs, uint32 newdae)
{
uint32 oldcs = rf_cs;
uint32 da = GET_DEX (rf_dae) | rf_da;

rf_dae |= newdae;                                       /* update DAE */
rf_cs |= newcs;                                         /* update CS */
if (da >= rf_unit.capac)                                /* update CS<ned> */
    rf_cs |= RFCS_NED;
else rf_cs &= ~RFCS_NED;
if (rf_dae & RFDAE_ALLERR)                              /* update CS<frz> */
    rf_cs |= RFCS_FRZ;
else rf_cs &= ~RFCS_FRZ;
if (rf_cs & RFCS_ALLERR)                                /* update CS<err> */
    rf_cs |= RFCS_ERR;
else rf_cs &= ~RFCS_ERR;
if ((rf_cs & RFCS_IE) &&                                /* IE and */
    (rf_cs & RFCS_DONE) &&!(oldcs & RFCS_DONE))         /* done 0->1? */
    SET_INT (RF);
return rf_cs;
}

/* Reset routine */

t_stat rf_reset (DEVICE *dptr)
{
rf_cs = RFCS_DONE;
rf_da = rf_dae = 0;
rf_dbr = 0;
rf_cma = 0;
rf_wc = 0;
rf_maint = 0;
CLR_INT (RF);
sim_cancel (&rf_unit);
return auto_config (0, 0);
}

/* Bootstrap routine */

/* Device bootstrap */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_CSR        (BOOT_START + 032)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint16))

static const uint16 boot_rom[] = {
    0043113,                        /* "FD" */
    0012706, BOOT_START,            /* MOV #boot_start, SP */
    0012701, 0177472,               /* MOV #RFDAE+2, R1     ; csr block */
    0005041,                        /* CLR -(R1)            ; clear dae */
    0005041,                        /* CLR -(R1),           ; clear da */
    0005041,                        /* CLR -(R1),           ; clear cma */
    0012741, 0177000,               /* MOV #-256.*2, -(R1)  ; load wc */
    0012741, 0000005,               /* MOV #READ+GO, -(R1)  ; read & go */
    0005002,                        /* CLR R2 */
    0005003,                        /* CLR R3 */
    0012704, BOOT_START+020,        /* MOV #START+20, R4 */
    0005005,                        /* CLR R5 */
    0105711,                        /* TSTB (R1) */
    0100376,                        /* BPL .-2 */
    0105011,                        /* CLRB (R1) */
    0005007                         /* CLR PC */
    };

t_stat rf_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_CSR >> 1] = (rf_dib.ba & DMASK) + 012;
saved_PC = BOOT_ENTRY;
return SCPE_OK;
}

/* Attach routine */

t_stat rf_attach (UNIT *uptr, char *cptr)
{
uint32 sz, p;
uint32 ds_bytes = RF_DKSIZE * sizeof (int16);

if ((uptr->flags & UNIT_AUTO) && (sz = sim_fsize_name (cptr))) {
    p = (sz + ds_bytes - 1) / ds_bytes;
    if (p >= RF_NUMDK)
        p = RF_NUMDK - 1;
    uptr->flags = (uptr->flags & ~UNIT_PLAT) |
        (p << UNIT_V_PLAT);
    }
uptr->capac = UNIT_GETP (uptr->flags) * RF_DKSIZE;
return attach_unit (uptr, cptr);
}

/* Change disk size */

t_stat rf_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val < 0)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = UNIT_GETP (val) * RF_DKSIZE;
uptr->flags = uptr->flags & ~UNIT_AUTO;
return SCPE_OK;
}

t_stat rf_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char *text2, *text3;
const char *const text =
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"RF11/RS11 Fixed Head Disk Controller (RF)\n"
"\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
" The RFll-A is a fast, low-cost, random·access bulk-storage system.  An\n"
" RFll-A provides 262,144 17-bit words (16 data bits and 1 parity bit)\n"
" of storage. Up to eight RSll disk platters can be controlled by one RFll\n"
" Controller for a total of 2,047,152 words of storage.  An RFll-A includes\n"
" a Control Unit and the first Disk Drive.\n"
"\n"
" The RF11-A  is unique in fixed head disks because each word is address-\n"
" able. Data transfers may be as small as one word or as large as 65,536\n"
" words. Individual words or groups of words may be read or rewritten\n"
" without any limits of fixed blocks or sectors, providing optimum use of\n"
" both disk storage and main memory in the PDP-11 system.\n"
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n"
" The RSll disk contains a nickel·cobalt·plated disk driven by a hysterisis\n"
" synchronous motor. Data is recorded on a single disk surface by 128\n"
" fixed read/write heads.\n"
" Operation\n"
" Fast track switching time permits spiral read or write.  Data may be\n"
" written in blocks from 1 to 65,536 words.  The RFll Control automatic-\n"
" ally continues on the next track, or on the next disk surface, when the\n"
" last address on a track or surface has been used.\n";
fprintf (st, "%s", text);
fprint_set_help (st, dptr);
text2 = 
"\n"
" The default is one platter.  The RF11 supports the BOOT command.  The\n"
" RF11 is disabled at startup and is automatically disabled in a Qbus\n"
" system.\n";
fprintf (st, "%s", text2);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
text3 = 
/*567901234567890123456789012345678901234567890123456789012345678901234567890*/
"\n"
" The RF11 is a DMA device.  If BURST = 0, word transfers are scheduled\n"
" individually; if BURST = 1, the entire transfer occurs in a single DMA\n"
" transfer.\n"
"\n"
" Error handling is as follows:\n"
"\n"
"   error          STOP_IOE     processed as\n"
"\n"
"   not attached    1           report error and stop\n"
"                   0           non-existent disk\n"
"\n"
" RF11 data files are buffered in memory; therefore, end of file and OS\n"
" I/O errors cannot occur.\n";
fprintf (st, "%s", text2);
return SCPE_OK;
}

char *rf_description (DEVICE *dptr)
{
return "RF11-A Fixed Head Disk controller";
}
