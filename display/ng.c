/*
 * Copyright (c) 2018 Lars Brinkhoff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the names of the authors shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

#include "display.h"                    /* XY plot interface */
#include "ng.h"


/* Bits in the CSR register, type Dazzle. */
#define TKRUN   004000
#define TKGO    010000
#define TKSTOP  020000


static void *ng_dptr;
static int ng_dbit;

#if defined(__cplusplus)
extern "C" {
#endif
#define DEVICE void
extern void _sim_debug_device (unsigned int dbits, DEVICE* dptr, const char* fmt, ...);

#define DEBUGF(...) _sim_debug_device (ng_dbit, ng_dptr, ##  __VA_ARGS__)
#if defined(__cplusplus)
}
#endif

int ng_type = 0;
int ng_scale = PIX_SCALE;

static uint16 status = 0;
static int reloc = 0;
static int console = 0;
static int dpc[8];
static int x[8];
static int y[8];

static unsigned char sync_period = 0;
static unsigned char time_out = 0;

int32
ng_get_csr(void)
{
  if (ng_type == TYPE_DAZZLE) {
    DEBUGF("[%d] Get CSR: ", 0);
    if (status & TKRUN)
      DEBUGF("running\n");
    else
      DEBUGF("stopped\n");
  } else if (ng_type == TYPE_LOGO) {
    DEBUGF("Get CSR: %06o\n", status);
  }
  return status;
}

int32
ng_get_reloc(void)
{
  return reloc & 0177777;
}

void
ng_set_csr(uint16 d)
{
  if (ng_type == TYPE_DAZZLE) {
    console = d & 0377;

    if (d & TKGO) {
      DEBUGF("[%d] Set CSR: GO\n", console);
      if ((status & TKRUN) == 0)
        dpc[console] = 2*console;
      status |= TKRUN;
    }
    if (d & TKSTOP) {
      DEBUGF("[%d] Set CSR: STOP\n", console);
      status &= ~TKRUN;
    }
  } else if (ng_type == TYPE_LOGO) {
    DEBUGF("Set CSR: %06o\n", d);
    if ((status & 1) == 0 && (d & 1))
      dpc[0] = 2*0;
    status = d;
  }
}

void
ng_set_reloc(uint16 d)
{
  reloc = d;
  DEBUGF("Set REL: %06o\n", d);
}

int
ng_init(void *dev, int debug)
{
  ng_dptr = dev;
  ng_dbit = debug;
  return display_init(DIS_NG, ng_scale, ng_dptr);
}

static int fetch (int a, uint16 *x)
{
  return ng_fetch (a + reloc, x);
}

static int store (int a, uint16 x)
{
  return ng_store (a + reloc, x);
}

static void point (void)
{
  int x1 = x[console];
  int y1 = y[console];

  DEBUGF("[%d] POINT %d %d\n", console, x[console], y[console]);
  display_point(x1 + 256, y1 + 256, DISPLAY_INT_MAX, 0);
}

void increment (uint16 inst)
{
  int n = (inst >> 8) & 7;
  int i, mask;

  if (n == 0)
    n = 8;

  DEBUGF("[%d] Increment %d, direction %d, bits %o\n",
         console, n, (inst >> 11) & 7, inst & 0377);

  mask = 0200;
  if (ng_type == TYPE_DAZZLE)
    mask = 0200 >> (8 - n);

  for (i = 0; i < n; i++, mask >>= 1) {
    switch (inst & 034000) {
    case 000000:
      if (inst & mask)
        x[console]++;
      y[console]++;
      break;
    case 004000:
      if (inst & mask)
        y[console]++;
      x[console]++;
      break;
    case 010000:
      if (inst & mask)
        y[console]--;
      x[console]++;
      break;
    case 014000:
      if (inst & mask)
        x[console]++;
      y[console]--;
      break;
    case 020000:
      if (inst & mask)
        x[console]--;
      y[console]--;
      break;
    case 024000:
      if (inst & mask)
        y[console]--;
      x[console]--;
      break;
    case 030000:
      if (inst & mask)
        y[console]++;
      x[console]--;
      break;
    case 034000:
      if (inst & mask)
        x[console]--;
      y[console]++;
      break;
    }
    point ();
  }
}

void pushj (uint16 inst)
{
  uint16 a;
  fetch (16 + 2*console, &a);
  store (16 + 2*console, a + 1);
  store (2*a, dpc[console]);
  DEBUGF("[%d] PUSHJ %06o -> %06o (%06o->%06o)\n",
         console, dpc[console], inst << 1, a, a+1);
  dpc[console] = inst << 1;
}

void stop (void)
{
  DEBUGF("[%d] STOP\n", console);
  if (ng_type == TYPE_DAZZLE)
    status &= ~TKRUN;
  else if (ng_type == TYPE_LOGO)
    dpc[0] = 2*0;
}

uint16 pop (void)
{
  uint16 a;
  fetch (16 + 2*console, &a);
  store (16 + 2*console, a - 1);
  DEBUGF("[%d] POP (%06o -> %06o)\n", console, a, a - 1);
  return a - 1;
}

void popj (void)
{
  uint16 a, x;
  a = pop ();
  fetch (2*a, &x);
  DEBUGF("[%d] POPJ %06o -> %06o\n", console, dpc[console], x);
  dpc[console] = x;
}

void resetx (void)
{
  DEBUGF("[%d] RESET X\n", console);
  x[console] = 0;
}

void resety (void)
{
  DEBUGF("[%d] RESET Y\n", console);
  y[console] = 0;
}

void delta (uint16 inst)
{
  int delta = inst & 01777;

  if (inst & 01000)
    delta |= ~0u << 10;

  switch (inst & 014000) {
  case 000000:
    if (inst & 02000)
      resetx ();
    if (inst & 01000)
      resety ();
    if (inst & 00400)
      stop ();
    if (inst & 00200)
        pop ();
    if (inst & 00100)
      popj ();
    return;
  case 004000:
    y[console] += delta;
    break;
  case 010000:
    x[console] += delta;
    break;
  case 014000:
    x[console] += delta;
    y[console] += delta;
    break;
  }

  DEBUGF("[%d] DELTA %d\n", console, delta);

  if (inst & 02000) {
    point ();
  }
}

int
ng_cycle(int us, int slowdown)
{
  uint16 inst;
  static uint32 usec = 0;
  static uint32 msec = 0;
  uint32 new_msec;

  new_msec = (usec += us) / 1000;

  /* if awaiting sync, look for next frame start */
  if (sync_period && (msec / sync_period != new_msec / sync_period))
    sync_period = 0;                /* start next frame */

  msec = new_msec;

  if (ng_type == TYPE_DAZZLE) {
    if ((status & TKRUN) == 0)
      goto age_ret;
  } else if (ng_type == TYPE_LOGO) {
    DEBUGF("STATUS %06o\n", status);
    if ((status & 1) == 0)
      goto age_ret;
  } else
    return 1;

  if (sync_period)
    goto age_ret;

  for (console = 0; console < 1; console++) {
    time_out = fetch(dpc[console], &inst);
    DEBUGF("[%d] PC %06o, INSTR %06o\n", console, dpc[console], inst);
    dpc[console] += 2;

    switch (inst & 0160000) {
    case 0040000:
    case 0060000:
      increment (inst);
      break;
    case 0100000:
    case 0120000:
      pushj (inst & 037777);
      break;
    case 0140000:
      delta (inst);
      break;
    }
  }

 age_ret:
  display_age(us, slowdown);
  return 1;
}
