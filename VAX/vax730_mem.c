/* vax730_mem.c: VAX 11/730 memory adapter

   Copyright (c) 2010-2011, Matt Burke

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

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   This module contains the VAX 11/730 memory controller registers.

   mctl         MS730 memory adapter

   29-Mar-2011  MB      First Version

*/

#include "vax_defs.h"

/* Memory adapter register 0 */

#define MCSR0_OF        0x00
#define MCSR0_ES        0x0000007F                      /* Error syndrome */
#define MCSR0_V_FPN     9
#define MCSR0_M_FPN     0x7FFF
#define MCSR0_FPN       (MCSR0_M_FPN << MCSR0_V_FPN)    /* Failing page number */

/* Memory adapter register 1 */

#define MCSR1_OF        0x01
#define MCSR1_RW        0x3E000000
#define MCSR1_MBZ       0x01FFFF80

/* Memory adapter register 2 */

#define MCSR2_OF        0x02
#define MCSR2_M_MAP     0xFFFF;
#define MCSR2_V_CS      24
#define MCSR2_CS        (1u << MCSR2_V_CS)
#define MCSR2_MBZ       0xFEFF0000

/* Debug switches */

#define MCTL_DEB_RRD     0x01                            /* reg reads */
#define MCTL_DEB_RWR     0x02                            /* reg writes */

#define MEM_SIZE_16K    (1u << 17)                       /* Board size (16k chips) */
#define MEM_SIZE_64K    (1u << 19)                       /* Board size (64k chips) */

#define MEM_BOARD_MASK(x,y)  ((1u << (uint32)(x/y)) - 1)

uint32 mcsr0 = 0;
uint32 mcsr1 = 0;
uint32 mcsr2 = 0;

t_stat mctl_reset (DEVICE *dptr);
t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mctl_wrreg (int32 val, int32 pa, int32 mode);
const char *mctl_description (DEVICE *dptr);

/* MCTLx data structures

   mctlx_dev    MCTLx device descriptor
   mctlx_unit   MCTLx unit
   mctlx_reg    MCTLx register list
*/

DIB mctl_dib = { TR_MCTL, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl_unit = { UDATA (NULL, 0, 0) };

REG mctl_reg[] = {
    { HRDATAD (CSR0, mcsr0, 32, "ECC syndrome bits") },
    { HRDATAD (CSR1, mcsr1, 32, "CPU error control/check bits") },
    { HRDATAD (CSR2, mcsr2, 32, "Unibus error control/check bits") },
    { NULL }
    };

MTAB mctl_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { 0 }
    };

DEBTAB mctl_deb[] = {
    { "REGREAD", MCTL_DEB_RRD },
    { "REGWRITE", MCTL_DEB_RWR },
    { NULL, 0 }
    };

DEVICE mctl_dev = {
    "MCTL", &mctl_unit, mctl_reg, mctl_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl_dib, DEV_NEXUS | DEV_DEBUG, 0,
    mctl_deb, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    };

/* Memory controller register read */

t_stat mctl_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 ofs;
ofs = NEXUS_GETOFS (pa);                                /* get offset */

switch (ofs) {                                          /* case on offset */

    case MCSR0_OF:                                      /* CSR0 */
        *val = mcsr0;
        break;

    case MCSR1_OF:                                      /* CSR1 */
        *val = mcsr1 & ~MCSR1_MBZ;
        break;

    case MCSR2_OF:                                      /* CSR2 */
        *val = mcsr2 & ~MCSR2_MBZ;
        break;

    default:
        return SCPE_NXM;
    }

if (DEBUG_PRI (mctl_dev, MCTL_DEB_RRD))
    fprintf (sim_deb, ">>MCTL: reg %d read, value = %X\n", ofs, *val);
return SCPE_OK;
}

/* Memory controller register write */

t_stat mctl_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 ofs;

ofs = NEXUS_GETOFS (pa);                                /* get offset */

switch (ofs) {                                          /* case on offset */

    case MCSR0_OF:                                      /* CSR0 */
        break;

    case MCSR1_OF:                                      /* CSR1 */
        mcsr1 = val & MCSR1_RW;
        break;

    case MCSR2_OF:                                      /* CSR2 */
        break;

    default:
        return SCPE_NXM;
    }

if (DEBUG_PRI (mctl_dev, MCTL_DEB_RWR))
    fprintf (sim_deb, ">>MCTL: reg %d write, value = %X\n", ofs, val);
return SCPE_OK;
}

/* Used by CPU and loader */

void rom_wr_B (int32 pa, int32 val)
{
return;
}

/* MEMCTL reset */

t_stat mctl_reset (DEVICE *dptr)
{
mcsr0 = 0;
mcsr1 = 0;
mcsr2 = MEM_BOARD_MASK(MEMSIZE, MEM_SIZE_64K) | MCSR2_CS;     /* Use 64k chips */
return SCPE_OK;
}

const char *mctl_description (DEVICE *dptr)
{
return "memory controller";
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
uint32 memsize = (uint32)(MEMSIZE>>20);
uint32 baseaddr = 0;
uint32 slot = 6;
struct {
    uint32 capacity;
    const char *option;
    } boards[] = {
        {  1, "MS730-CA M8750"}, 
        {  0, NULL}};
int32 bd;

while (memsize) {
    bd = 0;
    fprintf(st, "Memory slot %d (@0x%08x): %3d Mbytes (%s)\n", slot, baseaddr, boards[bd].capacity, boards[bd].option);
    memsize -= boards[bd].capacity;
    baseaddr += boards[bd].capacity<<20;
    ++slot;
    }
return SCPE_OK;
}
