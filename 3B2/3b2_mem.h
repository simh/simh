/* 3b2_mem.h: AT&T 3B2 3B2 memory access routines

   Copyright (c) 2021, Seth J. Morabito

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

#ifndef _3B2_MEM_H_
#define _3B2_MEM_H_

#include "3b2_defs.h"

uint32 pread_w(uint32 pa);
void   pwrite_w(uint32 pa, uint32 val);
uint8  pread_b(uint32 pa);
void   pwrite_b(uint32 pa, uint8 val);
uint16 pread_h(uint32 pa);
void   pwrite_h(uint32 pa, uint16 val);

uint8  read_b(uint32 va, uint8 r_acc);
uint16 read_h(uint32 va, uint8 r_acc);
uint32 read_w(uint32 va, uint8 r_acc);
void   write_b(uint32 va, uint8 val);
void   write_h(uint32 va, uint16 val);
void   write_w(uint32 va, uint32 val);

t_stat read_operand(uint32 va, uint8 *val);
t_stat examine(uint32 va, uint8 *val);
t_stat deposit(uint32 va, uint8 val);

t_bool addr_is_rom(uint32 pa);
t_bool addr_is_mem(uint32 pa);
t_bool addr_is_io(uint32 pa);

t_stat mmu_decode_va(uint32 va, uint8 r_acc, t_bool fc, uint32 *pa);
void   mmu_enable();
void   mmu_disable();

#endif /* _3B2_MEM_H_ */
