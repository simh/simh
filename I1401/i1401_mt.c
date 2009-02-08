/* i1401_mt.c: IBM 1401 magnetic tape simulator

   Copyright (c) 1993-2008, Robert M. Supnik

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

   mt           7-track magtape

   11-Jul-08    RMS     Added -n (no rewind) option to BOOT (from Van Snyder)
                        Added tape mark detect to diagnostic read (from Bob Abeles)
                        Added tape mark detect in multi-character records (from Bob Abeles)
                        Fixed memory leak in tape rewind-unload op (from Bob Abeles)
                        Fixed bug, BOOT ignores GM+WM in memory (from Bob Abeles)
                        Fixed handling of indicators (from Bob Abeles)
                        Fixed bug to mask input to 6b on read (from Bob Abeles)
   07-Jul-07    RMS     Removed restriction on load-mode binary tape
   28-Jun-07    RMS     Revised read tape mark behavior based on real hardware
                        (found by Van Snyder)
   16-Feb-06    RMS     Added tape capacity checking
   15-Sep-05    RMS     Yet another fix to load read group mark plus word mark
                        Added debug printouts (from Van Snyder)
   26-Aug-05    RMS     Revised to use API for write lock check
   16-Aug-03    RMS     End-of-record on load read works like move read
                        (verified on real 1401)
                        Added diagnostic read (space forward)
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   15-Mar-03    RMS     Fixed end-of-record on load read yet again
   28-Feb-03    RMS     Modified for magtape library
   31-Oct-02    RMS     Added error record handling
   10-Oct-02    RMS     Fixed end-of-record on load read writes WM plus GM
   30-Sep-02    RMS     Revamped error handling
   28-Aug-02    RMS     Added end of medium support
   12-Jun-02    RMS     End-of-record on move read preserves old WM under GM
                        (found by Van Snyder)
   03-Jun-02    RMS     Modified for 1311 support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added protection against bad record lengths
   30-Jan-02    RMS     New zero footprint tape bootstrap from Van Snyder
   20-Jan-02    RMS     Changed write enabled modifier
   29-Nov-01    RMS     Added read only unit support
   18-Apr-01    RMS     Changed to rewind tape before boot
   07-Dec-00    RMS     Widened display width from 6 to 8 bits to see record lnt
                CEO     Added tape bootstrap
   14-Apr-99    RMS     Changed t_addr to unsigned
   04-Oct-98    RMS     V2.4 magtape format

   Magnetic tapes are represented as a series of variable 16b records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.

   (Notes on indicators from Bob Abeles)
   The 1401 implements a "tape indicator" latch per tape drive. When a  
   1401 branch-on-indicator instruction addresses the "end-of-reel"  
   indicator, the 1401 addresses the "tape indicator" latch in the  
   currently selected and ready tape drive. The tape drive itself resets  
   its "tape indicator" latch only when a tape unload occurs. Tape  
   unload may be initiated by push button control or by the CPU "Rewind  
   Tape and Unload" instruction. The tape drive sets the "tape  
   indicator" latch when in write status and the end-of-reel reflective  
   strip photocell is activated. The CPU resets the "tape indicator"  
   latch in the selected and ready tape drive when the "end-of-reel"  
   indicator is tested by a branch-on-indicator instruction. If no tape  
   drive is selected, or the currently selected tape drive is not ready,  
   the "end-of-reel" indicator will appear to be off and no "tape  
   indicator" latch will be reset. The CPU sets the "tape indicator"  
   latch in the selected and ready tape drive whenever it reads a tape- 
   mark character in the first character position of a record.
*/

#include "i1401_defs.h"
#include "sim_tape.h"

#define MT_NUMDR        7                               /* #drives */
#define MT_MAXFR        (MAXMEMSIZE * 2)                /* max transfer */
#define IND             u3                              /* drive indicator */

uint8 dbuf[MT_MAXFR];                                   /* tape buffer */
int32 mt_sel = 0;                                       /* selected unit */

extern uint8 M[];                                       /* memory */
extern int32 ind[64];
extern int32 BS, iochk;
extern UNIT cpu_unit;
extern FILE *sim_deb;

t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unitno, DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, char *cp);
t_stat mt_detach (UNIT *uptr);
t_stat mt_map_status (t_stat st);
UNIT *mt_sel_unit (int32 unit);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit list
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

UNIT mt_unit[] = {
    { UDATA (NULL, UNIT_DIS, 0) },                      /* doesn't exist */
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) },
    { UDATA (NULL, UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_ROABLE + UNIT_BCD, 0) }
    };

REG mt_reg[] = {
    { FLDATA (ERR, ind[IN_TAP], 0) },
    { FLDATA (IND1, mt_unit[1].IND, 0) },
    { FLDATA (IND2, mt_unit[2].IND, 0) },
    { FLDATA (IND3, mt_unit[3].IND, 0) },
    { FLDATA (IND4, mt_unit[4].IND, 0) },
    { FLDATA (IND5, mt_unit[5].IND, 0) },
    { FLDATA (IND6, mt_unit[6].IND, 0) },
    { DRDATA (POS1, mt_unit[1].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { DRDATA (POS2, mt_unit[2].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { DRDATA (POS3, mt_unit[3].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { DRDATA (POS4, mt_unit[4].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { DRDATA (POS5, mt_unit[5].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { DRDATA (POS6, mt_unit[6].pos, T_ADDR_W), PV_LEFT + REG_RO },
    { NULL }
    };

MTAB mt_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", "CAPACITY",
      &sim_tape_set_capac, &sim_tape_show_capac, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    &mt_boot,  &mt_attach, &mt_detach,
    NULL, DEV_DEBUG
    };

/* Function routine

   Inputs:
        unit    =       unit character
        flag    =       normal, word mark, binary, or boot mode
        mod     =       modifier character
   Outputs:
        status  =       status
*/

t_stat mt_func (int32 unit, int32 flag, int32 mod)
{
t_mtrlnt tbc;
UNIT *uptr;
t_stat st;

ind[IN_TAP] = 0;                                        /* clear error */
if ((uptr = mt_sel_unit (unit)) == NULL)                /* sel unit, save */
    return STOP_INVMTU;                                 /* (not valid) */
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_UNATT;
switch (mod) {                                          /* case on modifier */

    case BCD_A:                                         /* diagnostic read */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: diagnostic read\n", unit);
        st = sim_tape_rdrecf (uptr, dbuf, &tbc, MT_MAXFR); /* read rec */
        if (st != MTSE_TMK) {                           /* not tmk? */
            if (st == MTSE_RECE)                        /* rec in error? */
                ind[IN_TAP] = 1;
            else if (st != MTSE_OK) {                   /* stop on error */
                if (DEBUG_PRS (mt_dev))
                    fprintf (sim_deb, ", stopped by status = %d\n", st);
                break;
                }
            if (!(flag & MD_BIN) &&                     /* BCD tape and */
                ((dbuf[0] & CHAR)  == BCD_TAPMRK))      /* first char TMK? */
                uptr->IND = 1;                          /* set indicator */
            }
        break;

    case BCD_B:                                         /* backspace */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: backspace\n", unit);
        st = sim_tape_sprecr (uptr, &tbc);              /* space rev */
        break;                                          /* end case */

    case BCD_E:                                         /* erase = nop */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: erase\n", unit);
        if (sim_tape_wrp (uptr))                        /* write protected? */
            return STOP_MTL;
        return SCPE_OK;

    case BCD_M:                                         /* write tapemark */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: write tape mark\n", unit);
        st = sim_tape_wrtmk (uptr);                     /* write tmk */
        break;

    case BCD_R:                                         /* rewind */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: rewind\n", unit);
        sim_tape_rewind (uptr);                         /* update position */
        return SCPE_OK;

    case BCD_U:                                         /* unload */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: rewind and unload\n", unit);
        sim_tape_rewind (uptr);                         /* update position */
        st = mt_detach (uptr);                          /* detach */
        break;

    default:
        return STOP_INVM;
        }

return mt_map_status (st);
}

/* Read and write routines

   Inputs:
        unit    =       unit character
        flag    =       normal, word mark, binary, or boot mode
        mod     =       modifier character
   Outputs:
        status  =       status

   Fine point: after a read, the system writes a group mark just
   beyond the end of the record.  However, first it checks for a
   GM + WM; if present, the GM + WM is not changed.  Otherwise,
   an MCW read sets a GM, preserving the current WM; while an LCA
   read sets a GM and clears the WM.
*/

t_stat mt_io (int32 unit, int32 flag, int32 mod)
{
int32 t, wm_seen;
t_mtrlnt i, tbc;
t_stat st;
t_bool passed_eot;
UNIT *uptr;

ind[IN_TAP] = 0;                                        /* clear error */
if ((uptr = mt_sel_unit (unit)) == NULL)                /* sel unit, save */
    return STOP_INVMTU;                                 /* (not valid) */
if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_UNATT;

switch (mod) {

    case BCD_R:                                         /* read */
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: read from %d", unit, BS);
        wm_seen = 0;                                    /* no word mk seen */
        st = sim_tape_rdrecf (uptr, dbuf, &tbc, MT_MAXFR); /* read rec */
        if (st != MTSE_TMK) {                           /* not tmk? */
            if (st == MTSE_RECE)                        /* rec in error? */
                ind[IN_TAP] = 1;
            else if (st != MTSE_OK) {                   /* stop on error */
                if (DEBUG_PRS (mt_dev))
                    fprintf (sim_deb, ", stopped by status = %d\n", st);
                break;
                }
            if (!(flag & MD_BIN) &&                     /* BCD tape and */
                ((dbuf[0] & CHAR)  == BCD_TAPMRK))      /* first char TMK? */
                uptr->IND = 1;                          /* set indicator */
            }
        for (i = 0; i < tbc; i++) {                     /* loop thru buf */
            if (!(flag & MD_BOOT) &&                    /* not boot? check */
                (M[BS] == (BCD_GRPMRK + WM))) {         /* GWM in memory? */
                if (DEBUG_PRS (mt_dev))
                    fprintf (sim_deb, " to %d, stopped by GMWM\n", BS);
                BS++;                                   /* incr BS */
                if (ADDR_ERR (BS)) {                    /* test for wrap */
                    BS = BA | (BS % MAXMEMSIZE);
                    return STOP_WRAP;
                    }
                return SCPE_OK;                         /* done */
                }
            t = dbuf[i] & CHAR;                         /* get char, strip parity */
            if (!(flag & MD_BIN) && (t == BCD_ALT))     /* BCD mode alt blank? */
                t = BCD_BLANK;                          /* real blank */
            if (flag & MD_WM) {                         /* word mk mode? */
                if ((t == BCD_WM) && (wm_seen == 0))    /* WM char, none prev? */
                    wm_seen = WM;                       /* set flag */
                else {
                    M[BS] = wm_seen | (t & CHAR);       /* char + wm seen */
                    wm_seen = 0;                        /* clear flag */
                    }
                }
            else M[BS] = (M[BS] & WM) | (t & CHAR);     /* preserve mem WM */
            if (!wm_seen)
                BS++;
            if (ADDR_ERR (BS)) {                        /* check next BS */
                BS = BA | (BS % MAXMEMSIZE);
                return STOP_WRAP;
                }
            }
        if (M[BS] != (BCD_GRPMRK + WM)) {               /* not GM+WM at end? */
            if (flag & MD_WM)                           /* LCA: clear WM */
                M[BS] = BCD_GRPMRK;
            else M[BS] = (M[BS] & WM) | BCD_GRPMRK;     /* MCW: save WM */
            }
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, " to %d, stopped by EOR\n", BS);
        BS++;                                           /* adv BS */
        if (ADDR_ERR (BS)) {                            /* check final BS */
            BS = BA | (BS % MAXMEMSIZE);
            return STOP_WRAP;
            }
        break;

    case BCD_W:
        if (sim_tape_wrp (uptr))                        /* locked? */
            return STOP_MTL;
        if (M[BS] == (BCD_GRPMRK + WM))                 /* eor? */
            return STOP_MTZ;
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, ">>MT%d: write from %d", unit, BS);
        for (tbc = 0; (t = M[BS++]) != (BCD_GRPMRK + WM); ) {
            if ((t & WM) && (flag & MD_WM))             /* WM in wm mode? */
                dbuf[tbc++] = BCD_WM;
            if (((t & CHAR) == BCD_BLANK) && !(flag & MD_BIN))
                dbuf[tbc++] = BCD_ALT;
            else dbuf[tbc++] = t & CHAR;
            if (ADDR_ERR (BS)) {                        /* check next BS */
                BS = BA | (BS % MAXMEMSIZE);
                return STOP_WRAP;
                }
            }
        if (DEBUG_PRS (mt_dev))
            fprintf (sim_deb, " to %d\n", BS - 1);
        passed_eot = sim_tape_eot (uptr);               /* passed EOT? */
        st = sim_tape_wrrecf (uptr, dbuf, tbc);         /* write record */
        if (!passed_eot && sim_tape_eot (uptr))         /* just passed EOT? */
            ind[IN_END] = 1;
        if (ADDR_ERR (BS)) {                            /* check final BS */
            BS = BA | (BS % MAXMEMSIZE);
            return STOP_WRAP;
            }
        break;

    default:
        return STOP_INVM;
        }

return mt_map_status (st);
}

/* Select unit - save selection if valid, return unit pointer */

UNIT *mt_sel_unit (int32 unit)
{
if ((unit <= 0) || (unit >= MT_NUMDR))
    return NULL;
mt_sel = unit;                                          /* save selected unit */
return mt_dev.units + unit;
}

/* Map tape status */

t_stat mt_map_status (t_stat st)
{
switch (st) {

    case MTSE_OK:                                       /* no error */
    case MTSE_BOT:                                      /* reverse into BOT */
        break;

    case MTSE_FMT:                                      /* illegal fmt */
        return SCPE_IERR;

    case MTSE_UNATT:                                    /* not attached */
        return SCPE_UNATT;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        return SCPE_MTRLNT;

    case MTSE_TMK:                                      /* end of file */
        if (mt_sel != 0)
            mt_unit[mt_sel].IND = 1;                    /* set indicator latch */
        break;

    case MTSE_IOERR:                                    /* IO error */
        ind[IN_TAP] = 1;                                /* set error */
        if (iochk)
            return SCPE_IOERR;
        break;

    case MTSE_RECE:                                     /* record in error */
    case MTSE_EOM:                                      /* end of medium */
        ind[IN_TAP] = 1;                                /* set error */
        break;

    case MTSE_WRP:                                      /* write protect */
        return STOP_MTL;
        }

return SCPE_OK;
}

/* Test and reset selected tape drive's indicator latch */

t_bool mt_testind ()
{
t_bool v;

if (mt_sel == 0)                                        /* any mt selected? */
    return FALSE;
if (!(mt_unit[mt_sel].flags & UNIT_ATT))                /* attached? */
    return FALSE;
v = mt_unit[mt_sel].IND;                                /* save latch */
mt_unit[mt_sel].IND = 0;                                /* reset latch */
return v;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

for (i = 0; i < MT_NUMDR; i++) {                        /* per drive resets */
    if (uptr = mt_sel_unit (i)) {
        MT_CLR_PNU (uptr);                              /* clear pos flag */
        uptr->IND = 0;                                  /* clear ind latch */
        }
    }
ind[IN_TAP] = 0;                                        /* clear mt err ind */
mt_sel = 0;                                             /* clear unit select */
return SCPE_OK;
}

/* Bootstrap routine

   The 1401 Reference manual states, "...tape data starts loading at address 001
   and continues until an inter-record gap is sensed."  GM+WM in memory is ignored.
*/

t_stat mt_boot (int32 unitno, DEVICE *dptr)
{
extern int32 saved_IS;
extern int32 sim_switches;

if ((sim_switches & SWMASK ('N')) == 0)                 /* unless -n */
    sim_tape_rewind (&mt_unit[unitno]);                 /* force rewind */
BS = 1;                                                 /* set BS = 001 */
mt_io (unitno, MD_WM + MD_BOOT, BCD_R);                 /* LDA %U1 001 R */
saved_IS = 1;
return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, char *cp)
{
uptr->IND = 0;                                          /* reset indicator latch */
return sim_tape_attach (uptr, cp);                      /* tape unit attach */
}

/* Detach routine */

t_stat mt_detach (UNIT *uptr)
{
uptr->IND = 0;                                          /* reset indicator latch */
return sim_tape_detach (uptr);                          /* tape unit detach */
}
