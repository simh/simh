/* vax610_mem.c: MSV11-P memory controller

   Copyright (c) 2011-2012, Matt Burke

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

   15-Feb-2012  MB      First Version
*/

#include "vax_defs.h"

#define MAX_MCTL_COUNT  16

#define MCSR_PEN        0x0001                          /* parity enable */
#define MCSR_WWP        0x0004                          /* write wrong parity */
#define MCSR_ECR        0x4000                          /* extended CSR read enable */
#define MCSR_RW         (MCSR_ECR|MCSR_WWP|MCSR_PEN)

int32 mctl_csr[MAX_MCTL_COUNT];
int32 mctl_count = 0;

t_stat mctl_rd (int32 *data, int32 PA, int32 access);
t_stat mctl_wr (int32 data, int32 PA, int32 access);
t_stat mctl_reset (DEVICE *dptr);
const char *mctl_description (DEVICE *dptr);

/* MCTL data structures

   mctl_dev       MCTL device descriptor
   mctl_unit      MCTL unit list
   mctl_reg       MCTL register list
   mctl_mod       MCTL modifier list
*/

#define IOLN_MEM        040

DIB mctl_dib = {
    IOBA_AUTO, IOLN_MEM, &mctl_rd, &mctl_wr,
    1, 0, 0, { NULL }
    };

UNIT mctl_unit =  { UDATA (NULL, 0, 0) };

REG mctl_reg[] = {
    { DRDATAD (COUNT, mctl_count, 16, "Memory Module Count") },
    { BRDATAD (CSR,     mctl_csr, DEV_RDX, 16, MAX_MCTL_COUNT, "control/status registers") },
    { NULL }
    };

MTAB mctl_mod[] = {
    { MTAB_XTD|MTAB_VDV, 010, "ADDRESS", "ADDRESS",
        NULL, &show_addr, NULL, "Bus address" },
    { 0 }
    };

DEVICE mctl_dev = {
    "MCTL", &mctl_unit, mctl_reg, mctl_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &mctl_reset,
    NULL, NULL, NULL,
    &mctl_dib, DEV_QBUS, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &mctl_description
    };

/* I/O dispatch routines */

t_stat mctl_rd (int32 *data, int32 PA, int32 access)
{
int32 rg = (PA >> 1) & 0xF;
if (rg >= mctl_count)
    return SCPE_NXM;
*data = mctl_csr[rg];
return SCPE_OK;
}

t_stat mctl_wr (int32 data, int32 PA, int32 access)
{
int32 rg = (PA >> 1) & 0xF;
if (rg >= mctl_count)
    return SCPE_NXM;
mctl_csr[rg] = data & MCSR_RW;
return SCPE_OK;
}

t_stat mctl_reset (DEVICE *dptr)
{
int32 rg;
for (rg = 0; rg < MAX_MCTL_COUNT; rg++) {
    mctl_csr[rg] = 0;
    }
mctl_count = (int32)(MEMSIZE >> 18);                    /* memory controllers enabled */
return SCPE_OK;
}

const char *mctl_description (DEVICE *dptr)
{
return "memory controller";
}

/* Used by CPU */

void rom_wr_B (int32 pa, int32 val)
{
return;
}

t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
uint32 memsize = (uint32)(MEMSIZE>>10);
uint32 baseaddr = 0;
uint32 csraddr = mctl_dib.ba;
struct {
    uint32 capacity;
    const char *option;
    } boards[] = {
        {  4096, "MSV11-QC"},
        {  2048, "MSV11-QB"},
        {  1024, "MSV11-QA"}, 
        {   512, "MSV11-PL"}, 
        {   256, "MSV11-PK"}, 
        {     0, NULL}};
int32 i;

while (memsize) {
    for (i=0; boards[i].capacity > memsize; ++i)
        ;
    fprintf(st, "Memory (@0x%08x): %3d %sbytes (%s) - CSR: 0x%08x.\n", baseaddr, boards[i].capacity/((boards[i].capacity >= 1024) ? 1024 : 1), (boards[i].capacity >= 1024) ? "M" : "K", boards[i].option, csraddr);
    memsize -= boards[i].capacity;
    baseaddr += boards[i].capacity*1024;
    csraddr += (boards[i].capacity/256)*2;
    }
return SCPE_OK;
}
