/* pdp11_mb.c: MB11, MAR and history registers

   Copyright (c) 2022, Lars Brinkhoff

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.
*/

#include "pdp11_defs.h"

t_stat mb_rd(int32 *data, int32 PA, int32 access);
t_stat mb_wr(int32 data, int32 PA, int32 access);
t_stat mb_reset(DEVICE *dptr);
const char *mb_description (DEVICE *dptr);

#define HSIZE 64

static uint16 MBCSR;
static uint16 MBXHGH;
static uint16 MBXLOW;
static uint16 MBYHGH;
static uint16 MBYLOW;
static uint16 MBHHGH;
static uint16 MBHLOW;
static uint16 MBHCNT;
static uint32 history[HSIZE];

/* BITS IN MBCSR */
#define MBINTE 0100
#define MBAFRZ 0200
#define MBXAYR 0400             /* X<A<Y READ TRAP */
#define MBXAYW 01000            /* X<A<Y WRITE TRAP */
#define MBNOIN 02000            /* IGNORE INIT */
#define MBINAO 04000            /* INTERRUPT ON ALMOST OVERFLOW */

/* BITS IN MBXHGH AND MBYHGH*/
#define MBREDT 04               /* READ TRAP BIT */
#define MBWRTT 010              /* WRITE TRAP BIT */

/* BITS IN MBHHGH */
#define MBWRTB 04               /* WRITE BIT IN HISTORY MEMORY HIGH BITS */

#define IOLN_MB   020
DIB mb_dib = {
  IOBA_AUTO, IOLN_MB, mb_rd, mb_wr,
  0, 0, 0, {NULL}, IOLN_MB
};

UNIT mb_unit = {
  UDATA (NULL, 0, 0), 0
};

REG mb_reg[] = {
  { ORDATAD (MBCSR,  MBCSR,  16, "MB11 control and status") },
  { ORDATAD (MBXHGH, MBXHGH, 16, "MB11 high bits of X register") },
  { ORDATAD (MBXLOW, MBXLOW, 16, "MB11 low bits of X register") },
  { ORDATAD (MBYHGH, MBYHGH, 16, "MB11 high bits of Y register") },
  { ORDATAD (MBYLOW, MBYLOW, 16, "MB11 low bits of Y register") },
  { ORDATAD (MBHHGH, MBHHGH, 16, "MB11 high bits of history register") },
  { ORDATAD (MBHLOW, MBHLOW, 16, "MB11 low bits of history register") },
  { ORDATAD (MBHCNT, MBHCNT, 16, "MB11 history memory counter") },
  { NULL }
};

MTAB mb_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Bus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VECTOR",  "VECTOR",
    &set_vec,  &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
  { 0 }  };

#define DBG_IO        0001

DEBTAB mb_deb[] = {
  { "IO", DBG_IO, "trace" },
  { NULL, 0 }
};

DEVICE mb_dev = {
    "MB", &mb_unit, mb_reg, mb_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &mb_reset,
    NULL, NULL, NULL,
    &mb_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG,
    0, mb_deb, NULL, NULL, NULL, NULL, NULL, 
    &mb_description
};

t_stat
mb_rd(int32 *data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;
  switch (PA & 017) {
  case 000:
    sim_debug (DBG_IO, &mb_dev, "READ MBCSR\n");
    break;
  case 002:
    sim_debug (DBG_IO, &mb_dev, "READ MBXHGH\n");
    break;
  case 004:
    sim_debug (DBG_IO, &mb_dev, "READ MBXLOW\n");
    break;
  case 006:
    sim_debug (DBG_IO, &mb_dev, "READ MBYHGH\n");
    break;
  case 010:
    sim_debug (DBG_IO, &mb_dev, "READ MBYLOW\n");
    break;
  case 012:
    sim_debug (DBG_IO, &mb_dev, "READ MBHHGH\n");
    break;
  case 014:
    sim_debug (DBG_IO, &mb_dev, "READ MBHLOW\n");
    break;
  case 016:
    sim_debug (DBG_IO, &mb_dev, "READ MBHCNT\n");
    break;
  }
  *data = 0;
  return stat;
}

t_stat
mb_wr(int32 data, int32 PA, int32 access)
{
  switch (PA & 017) {
  case 000:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBCSR %06o\n", data);
    break;
  case 002:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBXHGH %06o\n", data);
    break;
  case 004:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBXLOW %06o\n", data);
    break;
  case 006:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBYHGH %06o\n", data);
    break;
  case 010:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBYLOW %06o\n", data);
    break;
  case 012:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBHHGH %06o\n", data);
    break;
  case 014:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBHLOW %06o\n", data);
    break;
  case 016:
    sim_debug (DBG_IO, &mb_dev, "WRITE MBHCNT %06o\n", data);
    break;
    }
  return SCPE_OK;
}

t_stat mb_reset(DEVICE *dptr)
{
  if (MBCSR & MBNOIN)
    return SCPE_OK;

  MBCSR = 0;
  return SCPE_OK;
}

const char *mb_description (DEVICE *dptr)
{
  return "MB11 MAR and history";
}
