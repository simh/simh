/* vax750_mem.c: VAX 11/750 memory controllers

   Copyright (c) 2010-2012, Matt Burke

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   mctl               MS750 memory controller

   21-Oct-2012  MB      First Version
*/

#include "vax_defs.h"

/* Memory adapter register 0 */

#define MCSR0_OF        0x00
#define MCSR0_ES        0x0000007F                      /* Error syndrome */
#define MCSR0_V_EP      9
#define MCSR0_M_EP      0x7FFF
#define MCSR0_EP        (MCSR0_M_EP << MCSR0_V_EP)      /* Error page */
#define MCSR0_CRD       0x20000000                      /* Corrected read data */
#define MCSR0_RDSH      0x40000000                      /* Read data subs high */
#define MCSR0_RDS       0x80000000                      /* Read data substitute */
#define MCSR0_RS        (MCSR0_CRD | MCSR0_RDSH | MCSR0_RDS)

/* Memory adapter register 1 */

#define MCSR1_OF        0x01
#define MCSR1_CS        0x0000007F                      /* Check syndrome */
#define MCSR1_V_EP      9
#define MCSR1_M_EP      0x7FFF
#define MCSR1_EP        (MCSR1_M_EP << MCSR1_V_EP)      /* Page mode address */
#define MCSR1_ECCD      0x02000000                      /* ECC disable */
#define MCSR1_DIAG      0x04000000                      /* Diag mode */
#define MCSR1_PM        0x08000000                      /* Page mode */
#define MCSR1_CRE       0x10000000                      /* CRD enable */
#define MCSR1_RW        (MCSR1_CS | MCSR1_ECCD | MCSR1_DIAG | \
                         MCSR1_PM | MCSR1_CRE)

/* Memory adapter register 2 */

#define MCSR2_OF        0x02
#define MCSR2_M_MAP     0xFFFF                          /* Memory present */
#define MCSR2_INIT      0x00010000                      /* Cold/warm restart flag */
#define MCSR2_V_SA      17
#define MCSR2_M_SA      0x7F                            /* Start address */
#define MCSR2_V_CS      24
#define MCSR2_CS        (1u << MCSR2_V_CS)              /* Chip size */
#define MCSR2_MBZ       0xFF000000

/* Debug switches */

#define MCTL_DEB_RRD     0x01                            /* reg reads */
#define MCTL_DEB_RWR     0x02                            /* reg writes */

#define MEM_SIZE_16K    (1u << 17)                       /* Board size (16k chips) */
#define MEM_SIZE_64K    (1u << 19)                       /* Board size (64k chips) */
#define MEM_BOARD_MASK(x,y)  ((1u << (uint32)(x/y)) - 1)
#define MEM_64K_MASK     0x5555

extern UNIT cpu_unit;
extern FILE *sim_log, *sim_deb;

uint32 mcsr0 = 0;
uint32 mcsr1 = 0;
uint32 mcsr2 = 0;

t_stat mctl_reset (DEVICE *dptr);
t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mctl_wrreg (int32 val, int32 pa, int32 mode);

/* MCTL data structures

   mctl_dev    MCTL device descriptor
   mctl_unit   MCTL unit
   mctl_reg    MCTL register list
*/

DIB mctl_dib = { TR_MCTL, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl_unit = { UDATA (NULL, 0, 0) };

REG mctl_reg[] = {
    { NULL }
    };

MTAB mctl_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL, "NEXUS", NULL,
      NULL, &show_nexus },
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
    mctl_deb, 0, 0
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
        *val = mcsr1;
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
        mcsr0 = mcsr0 & ~(MCSR0_RS & val);
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

/* Used by CPU */

void rom_wr_B (int32 pa, int32 val)
{
return;
}

/* Memory controller reset */

t_stat mctl_reset (DEVICE *dptr)
{
mcsr0 = 0;
mcsr1 = 0;
if (MEMSIZE > MAXMEMSIZE)                               /* More than 2MB? */
    mcsr2 = MCSR2_INIT | (MEM_BOARD_MASK(MEMSIZE, MEM_SIZE_64K) & MEM_64K_MASK) | MCSR2_CS;  /* Use 64k chips */
else
    mcsr2 = MCSR2_INIT | MEM_BOARD_MASK(MEMSIZE, MEM_SIZE_16K);  /* Use 16k chips */
return SCPE_OK;
}
