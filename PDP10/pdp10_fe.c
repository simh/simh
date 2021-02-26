/* pdp10_fe.c: PDP-10 front end (console terminal) simulator

   Copyright (c) 1993-2012, Robert M Supnik

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

   fe           KS10 console front end

   18-Apr-12    RMS     Added clock coscheduling
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input
   17-Oct-06    RMS     Synced keyboard to clock for idling
   28-May-04    RMS     Removed SET FE CTRL-C
   29-Dec-03    RMS     Added console backpressure support
   25-Apr-03    RMS     Revised for extended file support
   22-Dec-02    RMS     Added break support
   30-May-02    RMS     Widened COUNT to 32b
   30-Nov-01    RMS     Added extended SET/SHOW support
   23-Oct-01    RMS     New IO page address constants
   07-Sep-01    RMS     Moved function prototypes
*/

#include "pdp10_defs.h"
#include "sim_tmxr.h"
#define UNIT_DUMMY      (1 << UNIT_V_UF)

extern int32 tmxr_poll;
t_stat fei_svc (UNIT *uptr);
t_stat feo_svc (UNIT *uptr);
static t_stat kaf_svc (UNIT *uptr);
t_stat fe_reset (DEVICE *dptr);
t_stat fe_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
a10 fe_xct = 0;
uint32 fe_bootrh = 0;
int32 fe_bootunit = -1;
extern DIB *dib_tab[];

/* FE data structures

   fe_dev       FE device descriptor
   fe_unit      FE unit descriptor
   fe_reg       FE register list
*/

#define fei_unit        fe_unit[0]
#define feo_unit        fe_unit[1]
#define kaf_unit        fe_unit[2]

UNIT fe_unit[] = {
    { UDATA (&fei_svc, UNIT_IDLE, 0), 0 },
    { UDATA (&feo_svc, 0, 0), SERIAL_OUT_WAIT },
    { UDATA (&kaf_svc, 0, 0), (1*1000*1000) }
    };

REG fe_reg[] = {
    { ORDATAD (IBUF, fei_unit.buf, 8, "input buffer") },
    { DRDATAD (ICOUNT, fei_unit.pos, T_ADDR_W, "count of input characters"), REG_RO + PV_LEFT },
    { DRDATAD (ITIME, fei_unit.wait, 24, "input polling interval (if 0, the keyboard is polled                            synchronously with the clock"), PV_LEFT },
    { ORDATAD (OBUF, feo_unit.buf, 8, "output buffer") },
    { DRDATAD (OCOUNT, feo_unit.pos, T_ADDR_W, "count of output characters"), REG_RO + PV_LEFT },
    { DRDATAD (OTIME, feo_unit.wait, 24, "console output response time"), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB fe_mod[] = {
    { UNIT_DUMMY, 0, NULL, "STOP", &fe_stop_os },
    { 0 }
    };

DEVICE fe_dev = {
    "FE", fe_unit, fe_reg, fe_mod,
    3, 10, 31, 1, 8, 8,
    NULL, NULL, &fe_reset,
    NULL, NULL, NULL
    };

/* Front end processor (console terminal)

   Communications between the KS10 and its front end is based on an in-memory
   status block and two interrupt lines: interrupt-to-control (APR_ITC) and
   interrupt-from-console (APR_CON).  When the KS10 wants to print a character
   on the terminal,

   1. It places a character, plus the valid flag, in FE_CTYOUT.
   2. It interrupts the front end processor.
   3. The front end processor types the character and then zeroes FE_CTYOUT.
   4. The front end procesor interrupts the KS10.

   When the front end wants to send an input character to the KS10,

   1. It places a character, plus the valid flag, in FE_CTYIN.
   2. It interrupts the KS10.
   3. It waits for the KS10 to take the character and clear the valid flag.
   4. It can then send more input (the KS10 may signal this by interrupting
      the front end).

   Note that the protocol has both ambiguity (interrupt to the KS10 may mean
   character printed, or input character available, or both) and lack of
   symmetry (the KS10 does not inform the front end that it has taken an
   input character).  
*/

/* Here is the definition of the communications area:
XPP RLWORD,31           ;RELOAD WORD  [FE_KEEPA]
    KSRLD==1B4          ;RELOAD REQUEST    (8080 will reload -10 if this is set)
    KPACT==1B5          ;KEEP ALIVE ACTIVE (8080 reloads -10 if KPALIV doesn't change)
    KLACT==1B6          ;KLINIK ACTIVE     (Remote diagnosis line enabled)
    PAREN==1B7          ;PARITY ERROR DETECT ENABLED
    CRMPAR==1B8         ;CRAM PAR ERR DETECT ENABLED
    DRMPAR==1B9         ;DRAM PAR ERR DETECT ENABLED
    CASHEN==1B10        ;CACHE ENABLED
    MILSEN==1B11        ;1MSEC ENABLED
    TRPENA==1B12        ;TRAPS ENABLED
    MFGMOD==1B13        ;MANUFACTURING MODE
    KPALIV==377B27      ;KEEP ALIVE WORD CHECKED EVERY 1 SEC, AFTER 15, FAIL
    ; Why reload (8080->10)
    AUTOBT==1B32        ;BOOT SWITCH OR POWER UP CONDITION
    PWRFAL==1B33        ;POWER FAIL restart (Start at 70)
    FORREL==1B34        ;FORCED RELOAD
    KEPFAL==1B35        ;KEEP ALIVE FAILURE (XCT exec 71)

XPP CTYIWD,32       ;CTY INPUT WORD [FE_CTYIN]
    CTYICH==377B35      ;CTY INPUT CHARACTER
    CTYIVL==1B27        ;INPUT VALID BIT (Actually, this is an 8-bit function code)

XPP CTYOWD,33       ;CTY OUTPUT WORD [FE_CTYOUT]
    CTYOCH==377B35      ;CTY OUTPUT CHARACTER
    CTYOVL==1B27        ;OUTPUT VALID FLAG

XPP KLIIWD,34       ;KLINIK INPUT WORD [FE_KLININ]
    KLIICH==377B35      ;KLINIK INPUT CHARACTER
    KLIIVL==1B27        ;KLINIK INPUT VALID (Historical)
    KLICHR==1B27        ;KLINIK CHARACTER
    KLIINI==2B27        ;KLINIK INITED
    KLICAR==3B27        ;CARRIER LOST


XPP KLIOWD,35       ;KLINIK OUTPUT WORD [FE_KLINOUT]
    KLIOCH==377B35      ;KLINIK OUTPUT CHARACTER
    KLIOVL==1B27        ;KLINIK OUTPUT VALID (Historical)
    KLOCHR==1B27        ;KLINIK CHARACTER AVAILABLE
    KLIHUP==2B27        ;KLINIK HANGUP REQUEST
*/

void fe_intr (void)
{
if (M[FE_CTYOUT] & FE_CVALID) {                         /* char to print? */
    feo_unit.buf = (int32) M[FE_CTYOUT] & 0177;         /* pick it up */
    feo_unit.pos = feo_unit.pos + 1;
    sim_activate (&feo_unit, feo_unit.wait);            /* sched completion */
    }
else if ((M[FE_CTYIN] & FE_CVALID) == 0) {              /* input char taken? */
    sim_cancel (&fei_unit);                             /* sched immediate */
    sim_activate (&fei_unit, 0);                        /* keyboard poll */
    }
return;
}

t_stat feo_svc (UNIT *uptr)
{
t_stat r;

if ((r = sim_putchar_s (uptr->buf)) != SCPE_OK) {       /* output; error? */
    sim_activate (uptr, uptr->wait);                    /* try again */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }
M[FE_CTYOUT] = 0;                                       /* clear char */
apr_flg = apr_flg | APRF_CON;                           /* interrupt KS10 */
return SCPE_OK;
}

t_stat fei_svc (UNIT *uptr)
{
int32 temp;

sim_clock_coschedule (uptr, tmxr_poll);                 /* continue poll */

if (M[FE_CTYIN] & FE_CVALID)                            /* previous character still pending? */
    return SCPE_OK;                                     /* wait until it gets digested */

temp = sim_poll_kbd ();                                 /* get possible char or error? */
if (temp < SCPE_KFLAG)                                  /* no char or error? */
    return temp;
if (temp & SCPE_BREAK)                                  /* ignore break */
    return SCPE_OK;
uptr->buf = temp & 0177;
uptr->pos = uptr->pos + 1;
M[FE_CTYIN] = uptr->buf | FE_CVALID;                    /* put char in mem */
apr_flg = apr_flg | APRF_CON;                           /* interrupt KS10 */
return SCPE_OK;
}

/* Keep-alive service
 * If the 8080 detects the 'force reload' bit, it initiates a disk
 * boot.  IO is reset, but memory is preserved.
 *
 * If the keep-alive enable bit is set, the -10 updates the keep-alive
 * count field every second.  The 8080 also checks the word every second.
 * If the 8080 finds that the count hasn't changed for 15 consecutive seconds,
 * a Keep-Alive Failure is declared.  This forces the -10 to execute the
 * contents of exec location 71 to collect status and initiate error recovery.
 */
static t_stat kaf_svc (UNIT *uptr)
{
if (M[FE_KEEPA] & INT64_C(0020000000000)) {              /* KSRLD - "Forced" (actually, requested) reload */
    uint32 oldsw = sim_switches;
    DEVICE *bdev = NULL;
    int32 i;

    sim_switches &= ~SWMASK ('P');
    reset_all (4);                                      /* RESET IO starting with UBA */
    sim_switches = oldsw;

    M[FE_KEEPA] &= ~INT64_C(0030000177777);             /* Clear KAF, RLD, KPALIV & reason
                                                         * 8080 ucode actually clears HW 
                                                         * status too, but that's a bug. */
    M[FE_KEEPA] |= 02;                                  /* Reason = FORREL */
    fei_unit.buf = feo_unit.buf = 0;
    M[FE_CTYIN] = M[FE_CTYOUT] = 0;
    M[FE_KLININ] = M[FE_KLINOUT] = 0;

    /* The 8080 has the disk RH address & unit in its memory, even if
     * the previous boot was from tape.  It has no NVM, so the last opr
     * selection will do here.  The case of DS MT <rld> would require a
     * SET FE command.  It's not a common case.
     */

    /* The device may have been detached, disabled or reconfigured since boot time.
     * Therefore, search for it by CSR address & validate that it's bootable.
     * If there are problems, the processor is halted.
     */

    for (i = 0; fe_bootrh && (bdev = sim_devices[i]) != NULL; i++ ) {
        DIB *dibp = (DIB *)bdev->ctxt;
        if (dibp && (fe_bootrh >= dibp->ba) &&
           (fe_bootrh < (dibp->ba + dibp->lnt))) {
            break;
            }
        }

    fe_xct = 2;
    if ((bdev != NULL) && (fe_bootunit >= 0) && (fe_bootunit < (int32) bdev->numunits)) {
        UNIT *bunit = bdev->units + fe_bootunit;

        if (!(bunit->flags & UNIT_DIS) && (bunit->flags & UNIT_ATTABLE) && (bunit->flags & UNIT_ATT)) {
            if (bdev->boot (fe_bootunit, bdev) == SCPE_OK) /* boot the device */
                fe_xct = 1;
            }
        }
    }
else if (M[FE_KEEPA] & INT64_C(0010000000000)) {        /* KPACT */
    d10 kav = M[FE_KEEPA] & INT64_C(0000000177400);     /* KPALIV */
    if (kaf_unit.u3 != (int32)kav) {
        kaf_unit.u3 = (int32)kav;
        kaf_unit.u4 = 0;
        }
    else if (++kaf_unit.u4 >= 15) {
        kaf_unit.u4 = 0;
        M[FE_KEEPA] = (M[FE_KEEPA] & ~INT64_C(0000000000377)) | 01; /* RSN = KAF (leaves enabled) */
        fei_unit.buf = feo_unit.buf = 0;
        M[FE_CTYIN] = M[FE_CTYOUT] = 0;
        M[FE_KLININ] = M[FE_KLINOUT] = 0;
        fe_xct = 071;
        }
    }

sim_activate_after (&kaf_unit, kaf_unit.wait);
if (fe_xct == 2) {
    fe_xct = 0;
    return STOP_CONSOLE;
    }
return SCPE_OK;
}
/* Reset */

t_stat fe_reset (DEVICE *dptr)
{
tmxr_set_console_units (&fei_unit, &feo_unit);
fei_unit.buf = feo_unit.buf = 0;

M[FE_CTYIN] = M[FE_CTYOUT] = 0;
M[FE_KLININ] = M[FE_KLINOUT] = 0;

M[FE_KEEPA] = INT64_C(0003740000000);                  /* PARITY STOP, CRM, DP PAREN, CACHE EN, 1MSTMR, TRAPEN */
kaf_unit.u3 = 0;
kaf_unit.u4 = 0;
apr_flg = apr_flg & ~(APRF_ITC | APRF_CON);
sim_activate (&fei_unit, tmxr_poll);
sim_activate_after (&kaf_unit, kaf_unit.wait);
return SCPE_OK;
}

/* Stop operating system */

t_stat fe_stop_os (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
M[FE_SWITCH] = IOBA_RP;                                 /* tell OS to stop */
return SCPE_OK;
}
