/* alpha_mmu.c - Alpha memory management simulator

   Copyright (c) 2003-2006, Robert M Supnik

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

   This module contains the routines for

        ReadB,W,L,Q     -       read aligned virtual
        ReadAccL,Q      -       read aligned virtual, special access check
        ReadPB,W,L,Q    -       read aligned physical
        WriteB,W,L,Q    -       write aligned virtual
        WriteAccL,Q     -       write aligned virtual, special access check
        WritePB,W,L,Q   -       write aligned physical

   The TLB is organized for optimum lookups and is broken up into three fields:

        tag             VA<42:13> for an 8KB page system
        pte             PTE<31:0>, <31:16> are zero; FOE, FOR, FOW stored inverted
        pfn             PFN<31:0> left shifted by page size

   The inversion of FOE, FOR, FOW means that all checked bits must be one
   for a reference to proceed.

   All Alpha implementations provide support for a 43b superpage for Unix,
   and a 32b superpage for NT:

   43b superpage                0xFFFFFC0000000000:0xFFFFFDFFFFFFFFFF
   32b superpage                0xFFFFFFFF80000000:0xFFFFFFFFBFFFFFFF
*/

#include "alpha_defs.h"

extern t_uint64 trans_i (t_uint64 va);
extern t_uint64 trans_d (t_uint64 va, uint32 acc);

extern t_uint64 *M;
extern t_uint64 p1;
extern uint32 pal_mode, dmapen;
extern uint32 cm_eacc, cm_racc, cm_wacc;
extern jmp_buf save_env;
extern UNIT cpu_unit;

/* Read virtual aligned

   Inputs:
        va      =       virtual address
   Output:
        returned data, right justified
*/

t_uint64 ReadB (t_uint64 va)
{
t_uint64 pa;

if (dmapen) pa = trans_d (va, cm_racc);                 /* mapping on? */
else pa = va;
return ReadPB (pa);
}

t_uint64 ReadW (t_uint64 va)
{
t_uint64 pa;

if (va & 1) ABORT1 (va, EXC_ALIGN);                     /* must be W aligned */
if (dmapen) pa = trans_d (va, cm_racc);                 /* mapping on? */
else pa = va;
return ReadPW (pa);
}

t_uint64 ReadL (t_uint64 va)
{
t_uint64 pa;

if (va & 3) ABORT1 (va, EXC_ALIGN);                     /* must be L aligned */
if (dmapen) pa = trans_d (va, cm_racc);                 /* mapping on? */
else pa = va;
return ReadPL (pa);
}

t_uint64 ReadQ (t_uint64 va)
{
t_uint64 pa;

if (va & 7) ABORT1 (va, EXC_ALIGN);                     /* must be Q aligned */
if (dmapen) pa = trans_d (va, cm_racc);                 /* mapping on? */
else pa = va;
return ReadPQ (pa);
}

/* Read with generalized access controls - used by PALcode */

t_uint64 ReadAccL (t_uint64 va, uint32 acc)
{
t_uint64 pa;

if (va & 3) ABORT1 (va, EXC_ALIGN);                     /* must be L aligned */
if (dmapen) pa = trans_d (va, acc);                     /* mapping on? */
else pa = va;
return ReadPL (pa);
}

t_uint64 ReadAccQ (t_uint64 va, uint32 acc)
{
t_uint64 pa;

if (va & 7) ABORT1 (va, EXC_ALIGN);                     /* must be Q aligned */
if (dmapen) pa = trans_d (va, acc);                     /* mapping on? */
else pa = va;
return ReadPQ (pa);
}

/* Read instruction */

uint32 ReadI (t_uint64 va)
{
t_uint64 pa;

if (!pal_mode) pa = trans_i (va);                       /* mapping on? */
else pa = va;
return (uint32) ReadPL (pa);
}

/* Write virtual aligned

   Inputs:
        va      =       virtual address
        val     =       data to be written, right justified in 32b or 64b
   Output:
        none
*/

void WriteB (t_uint64 va, t_uint64 dat)
{
t_uint64 pa;

if (dmapen) pa = trans_d (va, cm_wacc);                 /* mapping on? */
else pa = va;
WritePB (pa, dat);
return;
}

void WriteW (t_uint64 va, t_uint64 dat)
{
t_uint64 pa;

if (va & 1) ABORT1 (va, EXC_ALIGN);                     /* must be W aligned */
if (dmapen) pa = trans_d (va, cm_wacc);                 /* mapping on? */
else pa = va;
WritePW (pa, dat);
return;
}

void WriteL (t_uint64 va, t_uint64 dat)
{
t_uint64 pa;

if (va & 3) ABORT1 (va, EXC_ALIGN);                     /* must be L aligned */
if (dmapen) pa = trans_d (va, cm_wacc);                 /* mapping on? */
else pa = va;
WritePL (pa, dat);
return;
}

void WriteQ (t_uint64 va, t_uint64 dat)
{
t_uint64 pa;

if (va & 7) ABORT1 (va, EXC_ALIGN);                     /* must be Q aligned */
if (dmapen) pa = trans_d (va, cm_wacc);                 /* mapping on? */
else pa = va;
WritePQ (pa, dat);
return;
}

/* Write with generalized access controls - used by PALcode */

void WriteAccL (t_uint64 va, t_uint64 dat, uint32 acc)
{
t_uint64 pa;

if (va & 3) ABORT1 (va, EXC_ALIGN);                     /* must be L aligned */
if (dmapen) pa = trans_d (va, acc);                     /* mapping on? */
else pa = va;
WritePL (pa, dat);
return;
}

void WriteAccQ (t_uint64 va, t_uint64 dat, uint32 acc)
{
t_uint64 pa;

if (va & 7) ABORT1 (va, EXC_ALIGN);                     /* must be Q aligned */
if (dmapen) pa = trans_d (va, acc);                     /* mapping on? */
else pa = va;
WritePQ (pa, dat);
return;
}

/* Read and write physical aligned - access point to I/O */

INLINE t_uint64 ReadPB (t_uint64 pa)
{
t_uint64 val;

if (ADDR_IS_MEM (pa)) {
    uint32 bo = ((uint32) pa) & 07;
    return (((M[pa >> 3] >> (bo << 3))) & M8);
    }
if (ReadIO (pa, &val, L_BYTE)) return val;
return 0;
}

INLINE t_uint64 ReadPW (t_uint64 pa)
{
t_uint64 val;

if (ADDR_IS_MEM (pa)) {
    uint32 bo = ((uint32) pa) & 06;
    return (((M[pa >> 3] >> (bo << 3))) & M16);
    }
if (ReadIO (pa, &val, L_WORD)) return val;
return 0;
}

INLINE t_uint64 ReadPL (t_uint64 pa)
{
t_uint64 val;

if (ADDR_IS_MEM (pa)) {
    if (pa & 4) return (((M[pa >> 3] >> 32)) & M32);
    return ((M[pa >> 3]) & M32);
    }
if (ReadIO (pa, &val, L_LONG)) return val;
return 0;
}

INLINE t_uint64 ReadPQ (t_uint64 pa)
{
t_uint64 val;

if (ADDR_IS_MEM (pa)) return M[pa >> 3];
if (ReadIO (pa, &val, L_QUAD)) return val;
return 0;
}

INLINE void WritePB (t_uint64 pa, t_uint64 dat)
{
dat = dat & M8;
if (ADDR_IS_MEM (pa)) {
    uint32 bo = ((uint32) pa) & 07;
    M[pa >> 3] = (M[pa >> 3] & ~(((t_uint64) M8) << (bo << 3))) |
        (dat << (bo << 3));
    }
else WriteIO (pa, dat, L_BYTE);
return;
}

INLINE void WritePW (t_uint64 pa, t_uint64 dat)
{
dat = dat & M16;
if (ADDR_IS_MEM (pa)) {
    uint32 bo = ((uint32) pa) & 07;
    M[pa >> 3] = (M[pa >> 3] & ~(((t_uint64) M16) << (bo << 3))) |
        (dat << (bo << 3));
    }
else WriteIO (pa, dat, L_WORD);
return;
}

INLINE void WritePL (t_uint64 pa, t_uint64 dat)
{
dat = dat & M32;
if (ADDR_IS_MEM (pa)) {
    if (pa & 4) M[pa >> 3] = (M[pa >> 3] & M32) |
        (dat << 32);
    else M[pa >> 3] = (M[pa >> 3] & ~((t_uint64) M32)) | dat;
    }
else WriteIO (pa, dat, L_LONG);
return;
}

INLINE void WritePQ (t_uint64 pa, t_uint64 dat)
{
if (ADDR_IS_MEM (pa)) M[pa >> 3] = dat;
else WriteIO (pa, dat, L_QUAD);
return;
}

