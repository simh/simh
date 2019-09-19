/* pdp18b_dpy.c: PDP-7 Type 340 interface

   Copyright (c) 2019, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.

   dpy          (PDP-7) Type 340 Precision Incremental CRT Display, with
                        Type 341 Interface, Type 347 Subroutine Interface,
                        and Type 342 Symbol Generator.
*/


#include "pdp18b_defs.h"

#if defined(TYPE340)
#include "display/type340.h"
#include "display/display.h"

#define DBG_IOT         001         /* IOT instructions. */
#define DBG_IRQ         002         /* Interrupts. */
#define DBG_INS         004         /* 340 instructions. */

/*
 * Number of microseconds between svc calls.  Used to age display and
 * poll for WS events.
 */
#define DPY_CYCLE_US    100

int32 dpy05 (int32 dev, int32 pulse, int32 dat);
int32 dpy06 (int32 dev, int32 pulse, int32 dat);
int32 dpy07 (int32 dev, int32 pulse, int32 dat);
int32 dpy10 (int32 dev, int32 pulse, int32 dat);
int32 dpy_iors (void);
t_stat dpy_svc (UNIT *uptr);
t_stat dpy_reset (DEVICE *dptr);

DIB dpy_dib = { DEV_DPY, 4, &dpy_iors, { &dpy05, &dpy06, &dpy07, &dpy10 } };

UNIT dpy_unit[] = {
    { UDATA (&dpy_svc, 0, 0) },
};

DEBTAB dpy_deb[] = {
    { "IOT", DBG_IOT },
    { "IRQ", DBG_IRQ },
    { "INS", DBG_INS },
    { NULL, 0 }
    };

DEVICE dpy_dev = {
    "DPY", dpy_unit, NULL, NULL,
    1, 8, 12, 1, 8, 18,
    NULL, NULL, &dpy_reset,
    NULL, NULL, NULL,
    &dpy_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0,
    dpy_deb, NULL, NULL
    };

t_stat dpy_svc (UNIT *uptr)
{
  sim_activate_after(uptr, DPY_CYCLE_US);
  display_age(DPY_CYCLE_US, 0);
  ty340_cycle();
  return SCPE_OK;
}

ty340word ty340_fetch(ty340word addr)
{
  extern int32 *M;
  return (ty340word)M[addr];
}

t_stat dpy_reset (DEVICE *dptr)
{
  if (!(dptr->flags & DEV_DIS)) {
    display_reset();
    ty340_reset(dptr);
  }
  sim_cancel (&dpy_unit[0]);
  return SCPE_OK;
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2)
{
}

void
cpu_set_switches(unsigned long w1, unsigned long w2)
{
}

void
ty340_lp_int(ty340word x, ty340word y)
{
}

void
ty340_rfd(void)
{
}

int32 dpy_iors (void)
{
#if defined IOS_LPEN
  return IOS_LPEN;
#else
  return 0;
#endif
}

int32 dpy05 (int32 dev, int32 pulse, int32 dat)
{
  sim_debug(DBG_IOT, &dpy_dev, "7005%02o, %06o\n", pulse, dat);

  if (pulse & 001) {
    if (ty340_sense(ST340_VEDGE))
      dat |= IOT_SKP;
  }

  if (pulse & 002) {
    dat |= ty340_get_dac();
  }

  if (pulse & 004) {
    ty340_clear (ST340_LPHIT);
    sim_activate_abs (dpy_unit, 0);
  }

  return dat;
}

int32 dpy06 (int32 dev, int32 pulse, int32 dat)
{
  sim_debug(DBG_IOT, &dpy_dev, "7006%02o, %06o\n", pulse, dat);

  if (pulse & 001) {
    if (ty340_sense(ST340_STOPPED))
      dat |= IOT_SKP;
  }

  if (pulse & 002) {
    ty340_set_dac (0);
  }

  if (pulse & 004) {
    if ((pulse & 010) == 0)
      ty340_set_dac (dat & 07777);
    ty340_clear (ST340_STOPPED|ST340_STOP_INT);
    sim_activate_abs (dpy_unit, 0);
  }

  return dat;
}

int32 dpy07 (int32 dev, int32 pulse, int32 dat)
{
  sim_debug(DBG_IOT, &dpy_dev, "7007%02o, %06o\n", pulse, dat);

  if (pulse & 001) {
    if (ty340_sense(ST340_LPHIT))
      dat |= IOT_SKP;
  }

  if (pulse & 002) {
    dat |= 0; // X, Y
  }

  if (pulse & 004) {
    ty340_clear(~0);
  }

  return dat;
}

int32 dpy10 (int32 dev, int32 pulse, int32 dat)
{
  sim_debug(DBG_IOT, &dpy_dev, "7010%02o, %06o\n", pulse, dat);

  if (pulse & 001) {
    if (ty340_sense(ST340_HEDGE))
      dat |= IOT_SKP;
  }

  if (pulse & 002) {
    dat |= ty340_get_asr();
  }

  if (pulse & 004) {
    dat |= 0; // Light pen.
  }

  return dat;
}

#else /* !TYPE340 */
char pdp18b_dpy_unused;   /* sometimes empty object modules cause problems */
#endif /* !TYPE340 */
