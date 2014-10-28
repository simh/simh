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
#define MCSR2_V_CS64    24
#define MCSR2_CS64      (1u << MCSR2_V_CS64)            /* Chip size */
#define MCSR2_V_CS256   25
#define MCSR2_CS256     (1u << MCSR2_V_CS256)           /* Chip size */
#define MCSR2_MBZ       0xFC000000

/* Debug switches */

#define MCTL_DEB_RRD     0x01                            /* reg reads */
#define MCTL_DEB_RWR     0x02                            /* reg writes */

#define MEM_SIZE_16K    (1u << 18)                       /* Board size (16k chips) */
#define MEM_SIZE_64K    (1u << 20)                       /* Board size (64k chips) */
#define MEM_SIZE_256K   (1u << 22)                       /* Board size (256k chips) */
#define MEM_64K_MASK     0x5555
#define MEM_BOARD_MASK_64K(x)  ((((1u << (uint32)(x/MEM_SIZE_64K)) - 1) & MEM_64K_MASK) | MCSR2_CS64)
#define MEM_256K_MASK    0x5555
#define MEM_BOARD_MASK_256K(x) ((((1u << (uint32)(x/MEM_SIZE_256K)) - 1) & MEM_256K_MASK) | MCSR2_CS256)

extern UNIT cpu_unit;

uint32 mcsr0 = 0;
uint32 mcsr1 = 0;
uint32 mcsr2 = 0;

t_stat mctl_reset (DEVICE *dptr);
char *mctl_description (DEVICE *dptr);
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
    { HRDATAD (CSR0, mcsr0, 32, "ECC syndrome bits") },
    { HRDATAD (CSR1, mcsr1, 32, "CPU error control/check bits") },
    { HRDATAD (CSR2, mcsr2, 32, "Memory Configuration") },
    { NULL }
    };

MTAB mctl_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display Nexus" },
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
uint32 large_slot_size = MEM_SIZE_16K, large_slots;
uint32 small_slot_size, small_slots;
uint32 boards, board_mask;

mcsr0 = 0;
mcsr1 = 0;
if (MEMSIZE > MAXMEMSIZE_Y)                         /* More than 8MB? */
    large_slot_size = MEM_SIZE_256K;                /* Use 256k chips */
else {
    if (MEMSIZE > MAXMEMSIZE)
        large_slot_size = MEM_SIZE_64K;
    }
small_slot_size = large_slot_size >> 2;
large_slots = (uint32)(MEMSIZE/large_slot_size);
small_slots = (MEMSIZE & (large_slot_size -1))/small_slot_size;
boards = ((1u << ((large_slots + small_slots) << 1)) - 1);
board_mask = (((large_slot_size == MEM_SIZE_16K)? 0xFFFF : 0x5555) & (((1u << (large_slots << 1)) - 1))) | (((large_slot_size == MEM_SIZE_256K) ? 0xAAAA : 0xFFFF) << (large_slots << 1));
mcsr2 = MCSR2_INIT | (boards & board_mask) | ((large_slot_size == MEM_SIZE_256K) ? MCSR2_CS256 : 0);  /* Use 256k chips */
return SCPE_OK;
}

char *mctl_description (DEVICE *dptr)
{
return "Memory controller";
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, void* desc)
{
uint32 baseaddr = 0;
struct {
    uint32 capacity;
    char *option;
    } boards[] = {
        { 4096, "MS750-JD M7199"},
        { 1024, "MS750-CA M8750"},
        {  256, "MS750-AA M8728"}, 
        {    0, NULL}};
int32 i, bd;

for (i=0; i<8; i++) {
    if (mcsr2&MCSR2_CS256) {
        switch ((mcsr2&(3<<(i*2)))>>(i*2)) {
            case 0:
            case 3:
                bd = 3;         /* Not Present */
                break;
            case 2:
                bd = 1;         /* 64Kb chips */
                break;
            case 1:
                bd = 0;         /* 256Kb chips */
                break;
            }
        }
    else {
        switch ((mcsr2&(3<<(i*2)))>>(i*2)) {
            case 0:
                bd = 3;         /* Not Present */
                break;
            case 3:
                bd = 2;         /* 16Kb chips */
                break;
            case 1:
            case 2:
                bd = 1;         /* 64Kb chips */
                break;
            }
        }
    if (boards[bd].capacity)
        fprintf(st, "Memory slot %d (@0x%08x): %3d %sbytes (%s)\n", 11+i, baseaddr, boards[bd].capacity/((boards[bd].capacity>=1024) ? 1024 : 1), (boards[bd].capacity>=1024) ? "M" : "K", boards[bd].option);
    baseaddr += boards[bd].capacity<<10;
    }
return SCPE_OK;
}
