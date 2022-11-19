/* 3b2_rev2_csr.h: ED System Board Control and Status Register

   Copyright (c) 2021-2022, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#ifndef _3B2_REV2_CSR_H_
#define _3B2_REV2_CSR_H_

#include "3b2_defs.h"

typedef uint16 CSR_DATA;

/* CSR */
t_stat csr_svc(UNIT *uptr);
t_stat csr_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat csr_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat csr_reset(DEVICE *dptr);
uint32 csr_read(uint32 pa, size_t size);
void csr_write(uint32 pa, uint32 val, size_t size);

#endif /* 3B2_REV2_CSR_H_ */
