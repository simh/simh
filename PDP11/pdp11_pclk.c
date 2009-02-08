/* pdp11_pclk.c: KW11P programmable clock simulator

   Copyright (c) 1993-2008, Robert M Supnik
   Written by John Dundas, used with his gracious permission

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

   pclk         KW11P line frequency clock

   20-May-08    RMS     Standardized clock delay at 1mips
   18-Jun-07    RMS     Added UNIT_IDLE flag
   07-Jul-05    RMS     Removed extraneous externs

   KW11-P Programmable Clock

   I/O Page Registers:

        CSR     17 772 540
        CSB     17 772 542
        CNT     17 772 544

   Vector:      0104

   Priority:    BR6

   ** Theory of Operation **

   A real KW11-P is built around the following major components:
   - 16-bit up/down counter
   - 16-bit count set buffer
   - 9-bit control and status register
   - clocks: crystal controlled (1) 100 kHz and (2) 10 kHz clocks,
     (3) a 50/60 Hz line frequency clock, and (4) an analog signal
     input trigger
   This software emulator for SIMH implements all of the above with
   the exception of the external input trigger, which is arbitrarily
   wired to 10Hz.

   Operation of this emulator is rather simplistic as compared to the
   actual device.  The register read and write routines are responsible
   for copying internal state from the simulated device to the operating
   program.  Clock state variables are altered in the write routine
   as well as the desired clock ticking rate.  Possible rates are
   given in the table below.

    Rate                Bit 2   Bit 1
   100 kHz                0       0
   10 kHz                 0       1
   Line frequency         1       0
   External               1       1

   I think SIMH would have a hard time actually keeping up with a 100
   kHz ticking rate.  I haven't tried this to verify, though.

   The clock service routine (pclk_svc) is responsible for ticking
   the clock.  The routine does implement up/down, repeat vs.
   single-interrupt, and single clocking (maintenance).  The routine
   updates the internal state according to the options selected and
   signals interrupts when appropriate.

   For a complete description of the device, please see DEC-11-HPWB-D
   KW11-P Programmable Real-Time Clock Manual.

   ** Notes **

   1. The device is disabled by default.

   2. Use XXDP V2.5 test program ZKWBJ1.BIC; loads at 1000, starts at
      1100?  Seems to execute the first few tests correctly then waits
      for input from the console.  I don't have a description of how this
      diagnostic works and thus don't know how to proceed from that point.

   3. The read and write routines don't do anything with odd address
      accesses.  The manual says that byte writes don't work.

   4. RSTS can use this clock in place of the standard KW11-L line
      frequency clock.  In order to do this, use the DEFAULT response in
      the OPTION: dialog.  To the Preferred clock prompt answer "P".
      Then you have the option of line frequency "L" or some multiple of
      50 between 50 and 1000 to use the programmable portion of the clock.

   5. This is really a Unibus peripheral and thus doesn't actually make
      sense within a J-11 system as there never was a Qbus version of
      this to the best of my knowledge.  However the OSs I have tried
      don't appear to exhibit any dissonance between this option and the
      processor/bus emulation.  I think the options that would make
      somewhat more sense in a Qbus environment the KWV11-C and/or KWV11-S.
      I don't know if any of the -11 OSs contained support for using
      these as the system clock, though.
*/

#include "pdp11_defs.h"

#define PCLKCSR_RDMASK  0100377                         /* readable */
#define PCLKCSR_WRMASK  0000137                         /* writeable */

#define UNIT_V_LINE50HZ (UNIT_V_UF + 0)
#define UNIT_LINE50HZ   (1 << UNIT_V_LINE50HZ)

/* CSR - 17772540 */

#define CSR_V_FIX       5                               /* single tick */
#define CSR_V_UPDN      4                               /* down/up */
#define CSR_V_MODE      3                               /* single/repeat */
#define CSR_FIX         (1u << CSR_V_FIX)
#define CSR_UPDN        (1u << CSR_V_UPDN)
#define CSR_MODE        (1u << CSR_V_MODE)
#define CSR_V_RATE      1                               /* rate */
#define CSR_M_RATE      03
#define CSR_GETRATE(x)  (((x) >> CSR_V_RATE) & CSR_M_RATE)

extern int32 int_req[IPL_HLVL];

uint32 pclk_csr = 0;                                    /* control/status */
uint32 pclk_csb = 0;                                    /* count set buffer */
uint32 pclk_ctr = 0;                                    /* counter */
static uint32 rate[4] = { 100000, 10000, 60, 10 };      /* ticks per second */
static uint32 xtim[4] = { 10, 100, 16667, 100000 };     /* nominal time delay */

DEVICE pclk_dev;
t_stat pclk_rd (int32 *data, int32 PA, int32 access);
t_stat pclk_wr (int32 data, int32 PA, int32 access);
t_stat pclk_svc (UNIT *uptr);
t_stat pclk_reset (DEVICE *dptr);
t_stat pclk_set_line (UNIT *uptr, int32 val, char *cptr, void *desc);
void pclk_tick (void);

/* PCLK data structures

   pclk_dev     PCLK device descriptor
   pclk_unit    PCLK unit descriptor
   pclk_reg     PCLK register list
*/

DIB pclk_dib = {
    IOBA_PCLK, IOLN_PCLK, &pclk_rd, &pclk_wr,
    1, IVCL (PCLK), VEC_PCLK, { NULL }
    };

UNIT pclk_unit = { UDATA (&pclk_svc, UNIT_IDLE, 0) };

REG pclk_reg[] = {
    { ORDATA (CSR, pclk_csr, 16) },
    { ORDATA (CSB, pclk_csb, 16) },
    { ORDATA (CNT, pclk_ctr, 16) },
    { FLDATA (INT, IREQ (PCLK), INT_V_PCLK) },
    { FLDATA (OVFL, pclk_csr, CSR_V_ERR) },
    { FLDATA (DONE, pclk_csr, CSR_V_DONE) },
    { FLDATA (IE, pclk_csr, CSR_V_IE) },
    { FLDATA (UPDN, pclk_csr, CSR_V_UPDN) },
    { FLDATA (MODE, pclk_csr, CSR_V_MODE) },
    { FLDATA (RUN, pclk_csr, CSR_V_GO) },
    { BRDATA (TIME, xtim, 10, 32, 4), REG_NZ + PV_LEFT },
    { BRDATA (TPS, rate, 10, 32, 4), REG_NZ + PV_LEFT },
    { DRDATA (CURTIM, pclk_unit.wait, 32), REG_HRO },
    { ORDATA (DEVADDR, pclk_dib.ba, 32), REG_HRO },
    { ORDATA (DEVVEC, pclk_dib.vec, 16), REG_HRO },
    { NULL }
    };

MTAB pclk_mod[] = {
    { UNIT_LINE50HZ, UNIT_LINE50HZ, "50 Hz", "50HZ", &pclk_set_line },
    { UNIT_LINE50HZ,             0, "60 Hz", "60HZ", &pclk_set_line },
    { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      &set_vec, &show_vec, NULL },
    { 0 }
    };

DEVICE pclk_dev = {
    "PCLK", &pclk_unit, pclk_reg, pclk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &pclk_reset,
    NULL, NULL, NULL,
    &pclk_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS
    };

/* Clock I/O address routines */

t_stat pclk_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {

    case 00:                                            /* CSR */
        *data = pclk_csr & PCLKCSR_RDMASK;              /* return CSR */
        pclk_csr = pclk_csr & ~(CSR_ERR | CSR_DONE);    /* clr err, done */
        CLR_INT (PCLK);                                 /* clr intr */
        break;

    case 01:                                            /* buffer */
        *data = 0;                                      /* read only */
        break;

    case 02:                                            /* counter */
        *data = pclk_ctr & DMASK;                       /* return counter */
        break;
        }

return SCPE_OK;
}

t_stat pclk_wr (int32 data, int32 PA, int32 access)
{
int32 old_csr = pclk_csr;
int32 rv;

switch ((PA >> 1) & 03) {

    case 00:                                            /* CSR */
        pclk_csr = data & PCLKCSR_WRMASK;               /* clear and write */
        CLR_INT (PCLK);                                 /* clr intr */
        rv = CSR_GETRATE (pclk_csr);                    /* new rate */
        pclk_unit.wait = xtim[rv];                      /* new delay */
        if ((pclk_csr & CSR_GO) == 0) {                 /* stopped? */
            sim_cancel (&pclk_unit);                    /* cancel */
            if (data & CSR_FIX)                         /* fix? tick */
                pclk_tick ();
            }
        else if (((old_csr & CSR_GO) == 0) ||           /* run 0 -> 1? */
                 (rv != CSR_GETRATE (old_csr))) {       /* rate change? */
            sim_cancel (&pclk_unit);                    /* cancel */
            sim_activate (&pclk_unit,                   /* start clock */
                sim_rtcn_init (pclk_unit.wait, TMR_PCLK));
            }
        break;

    case 01:                                            /* buffer */
        pclk_csb = pclk_ctr = data;                     /* store ctr */
        pclk_csr = pclk_csr & ~(CSR_ERR | CSR_DONE);    /* clr err, done */
        CLR_INT (PCLK);                                 /* clr intr */
        break;

    case 02:                                            /* counter */
        break;                                          /* read only */
        }

return SCPE_OK;
}

/* Clock tick (automatic or manual) */

void pclk_tick (void)
{
if (pclk_csr & CSR_UPDN)                                /* up or down? */
    pclk_ctr = (pclk_ctr + 1) & DMASK;                  /* 1 = up */
else pclk_ctr = (pclk_ctr - 1) & DMASK;                 /* 0 = down */
if (pclk_ctr == 0) {                                    /* reached zero? */
    if (pclk_csr & CSR_DONE)                            /* done already set? */
        pclk_csr = pclk_csr | CSR_ERR;                  /* set error */
    else pclk_csr = pclk_csr | CSR_DONE;                /* else set done */
    if (pclk_csr & CSR_IE)                              /* if IE, set int */
        SET_INT (PCLK);
    if (pclk_csr & CSR_MODE)                            /* if rpt, reload */
        pclk_ctr = pclk_csb;
    else {
        pclk_csb = 0;                                   /* else clr ctr */
        pclk_csr = pclk_csr & ~CSR_GO;                  /* and clr go */
        }
    }
return;
}

/* Clock service */

t_stat pclk_svc (UNIT *uptr)
{
int32 rv;

pclk_tick ();                                           /* tick clock */
if ((pclk_csr & CSR_GO) == 0)                           /* done? */
    return SCPE_OK;
rv = CSR_GETRATE (pclk_csr);                            /* get rate */
sim_activate (&pclk_unit, sim_rtcn_calb (rate[rv], TMR_PCLK));
return SCPE_OK;
}

/* Clock reset */

t_stat pclk_reset (DEVICE *dptr)
{
pclk_csr = 0;                                           /* clear reg */
pclk_csb = 0;
pclk_ctr = 0;
CLR_INT (PCLK);                                         /* clear int */
sim_cancel (&pclk_unit);                                /* cancel */
pclk_unit.wait = xtim[0];                               /* reset delay */
return SCPE_OK;
}

/* Set line frequency */

t_stat pclk_set_line (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val == UNIT_LINE50HZ)
    rate[2] = 50;
else rate[2] = 60;
return SCPE_OK;
}
