/* vax820_mem.c: VAX 8200 memory controllers

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   mctl0, mctl1         MS820 memory controllers
*/

#include "vax_defs.h"

/* Memory CSR 1 */

#define MCSR1_OF        0x40
#define MCSR1_V_SIZE    18                              /* memory size */
#define MCSR1_M_SIZE    0x7FF
#define MCSR1_MWE       0x00000400                      /* masked write error - NI */
#define MCSR1_ICE       0x00000200                      /* internal controller error - NI */
#define MCSR1_CDI       0x00008000                      /* CRD interrupt inhibit - NI */

/* Memory CSR 2 */

#define MCSR2_OF        0x41

uint32 mcsr_1[MCTL_NUM];
uint32 mcsr_2[MCTL_NUM];

t_stat mctl_reset (DEVICE *dptr);
const char *mctl_description (DEVICE *dptr);
t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mctl_wrreg (int32 val, int32 pa, int32 mode);

/* MCTLx data structures

   mctlx_dev    MCTLx device descriptor
   mctlx_unit   MCTLx unit
   mctlx_reg    MCTLx register list
*/

DIB mctl0_dib[] = { { TR_MCTL0, 0, &mctl_rdreg, &mctl_wrreg, 0 } };

UNIT mctl0_unit = { UDATA (NULL, 0, 0) };

REG mctl0_reg[] = {
    { HRDATA (CSR1, mcsr_1[0], 32) },
    { HRDATA (CSR2, mcsr_2[0], 32) },
    { NULL }
    };

MTAB mctl0_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL0, "NEXUS", NULL,
      NULL, &show_nexus, NULL, "Display nexus" },
    { 0 }
    };

DIB mctl1_dib[] = { { TR_MCTL1, 0, &mctl_rdreg, &mctl_wrreg, 0 } };

UNIT mctl1_unit = { UDATA (NULL, 0, 0) };

MTAB mctl1_mod[] = {
    { MTAB_XTD|MTAB_VDV, TR_MCTL1, "NEXUS", NULL,
      NULL, &show_nexus },
    { 0 }  };

REG mctl1_reg[] = {
    { HRDATA (CSR1, mcsr_1[1], 32) },
    { HRDATA (CSR2, mcsr_2[1], 32) },
    { NULL }
    };

DEVICE mctl_dev[] = {
    {
    "MCTL0", &mctl0_unit, mctl0_reg, mctl0_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl0_dib, DEV_NEXUS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    },
    {
    "MCTL1", &mctl1_unit, mctl1_reg, mctl1_mod,
    1, 16, 16, 1, 16, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl1_dib, DEV_NEXUS, 0,
    NULL, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    }
    };

/* Memory controller register read */

t_stat mctl_rdreg (int32 *val, int32 pa, int32 lnt)
{
int32 mctl, ofs;

mctl = NEXUS_GETNEX (pa) - TR_MCTL0;                    /* get mctl num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
switch (ofs) {

    case BI_DTYPE:
        *val = DTYPE_MS820;
        break;

    case BI_CSR:
    case BI_BER:
    case BI_EICR:
    case BI_IDEST:
        *val = 0;
        break;

    case BI_SA:                                         /* start address */
        *val = (mctl == 0) ? 0 : (int32)(MEMSIZE >> 1);
        break;

    case BI_EA:                                         /* end address */
        *val = (mctl == 0) ? (int32)(MEMSIZE >> 1) : (int32)MEMSIZE;
        break;

    case MCSR1_OF:                                       /* CSR 1 */
        *val = mcsr_1[mctl];
        break;

    case MCSR2_OF:                                       /* CSR 2 */
        *val = mcsr_2[mctl];
        break;

    default:
        return SCPE_NXM;
        }

return SCPE_OK;
}

/* Memory controller register write */

t_stat mctl_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 mctl, ofs;

mctl = NEXUS_GETNEX (pa) - TR_MCTL0;                    /* get mctl num */
ofs = NEXUS_GETOFS (pa);                                /* get offset */
switch (ofs) {

    case BI_CSR:
    case BI_BER:
    case BI_EICR:
    case BI_IDEST:
        break;

    case MCSR1_OF:                                       /* CSR 1 */
    case MCSR2_OF:                                       /* CSR 2 */
        break;

    default:
        return SCPE_NXM;
        }

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
int32 i;
for (i = 0; i < MCTL_NUM; i++) {                        /* init for MS820 */
    mcsr_1[i] = (MCSR1_M_SIZE << MCSR1_V_SIZE);
    mcsr_2[i] = 0;
    }
return SCPE_OK;
}

const char *mctl_description (DEVICE *dptr)
{
return "memory controller";
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
// TODO
return SCPE_OK;
}
