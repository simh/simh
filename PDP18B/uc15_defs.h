/* uc15_defs.h: PDP15/UC15 shared state definitions

   Copyright (c) 2016, Robert M Supnik

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
*/

#ifndef UC15_DEFS_H_
#define UC15_DEFS_H_        0

#define UC15_STATE_SIZE     1024                /* size (int32's) */

/* The shared state region is divided into four quadrants

   000-255      PDP-15 read/write, PDP-11 read only, data
   255-511      PDP-11 read/write, PDP-15 read only, data
   768-1023     Event signals (locks), read/write
*/

#define PDP15_MAXMEM            0400000             /* PDP15 max mem, words */

#define UC15_PDP15MEM           0040                /* PDP15 mem size, bytes */
#define UC15_TCBP               0100                /* TCB pointer */
#define UC15_API_SUMM           0140                /* API summary */

#define UC15_API_VEC            0600                /* vectors[4] */
#define UC15_API_VEC_MUL        010                 /* vector spread factor */

#define UC15_TCBP_WR            01000               /* TCBP write signal */
#define UC15_TCBP_RD            01040               /* TCBP read signal */
#define UC15_API_UPD            01100               /* API summ update */
#define UC15_API_REQ            01200               /* +1 for API req[4] */

#define UC15_SHARED_RD(p)       (*(uc15_shstate + (p)))
#define UC15_SHARED_WR(p,d)     *(uc15_shstate + (p)) = (d)

#define UC15_ATOMIC_CAS(p,o,n)  sim_shmem_atomic_cas ((uc15_shstate + (p)), o, n)
#define UC15_ATOMIC_ADD(p,a)    sim_shmem_atomic_add ((uc15_shstate + (p)), (a))

#endif
