/* 3b2_cpu.h: AT&T 3B2 Model 400 System Devices (Header)

   Copyright (c) 2017, Seth J. Morabito

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

#ifndef _3B2_SYSDEV_H_
#define _3B2_SYSDEV_H_

#include "3b2_defs.h"
#include "3b2_sys.h"
#include "3b2_cpu.h"

extern DEVICE nvram_dev;
extern DEVICE timer_dev;
extern DEVICE csr_dev;
extern DEVICE tod_dev;
extern DEBTAB sys_deb_tab[];

struct timer_ctr {
    uint16 divider;
    uint8  mode;
    t_bool lmb;
    t_bool enabled;
    t_bool gate;
    double stime;     /* Most recent start time of counter */
};

/* NVRAM */
t_stat nvram_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvram_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvram_reset(DEVICE *dptr);
uint32 nvram_read(uint32 pa, size_t size);
t_stat nvram_attach(UNIT *uptr, CONST char *cptr);
t_stat nvram_detach(UNIT *uptr);
const char *nvram_description(DEVICE *dptr);
void nvram_write(uint32 pa, uint32 val, size_t size);

/* 8253 Timer */
t_stat timer_reset(DEVICE *dptr);
uint32 timer_read(uint32 pa, size_t size);
void timer_write(uint32 pa, uint32 val, size_t size);
t_stat timer0_svc(UNIT *uptr);
t_stat timer1_svc(UNIT *uptr);
t_stat timer2_svc(UNIT *uptr);

/* CSR */
t_stat csr_svc(UNIT *uptr);
t_stat csr_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat csr_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat csr_reset(DEVICE *dptr);
uint32 csr_read(uint32 pa, size_t size);
void csr_write(uint32 pa, uint32 val, size_t size);

/* TOD */
t_stat tod_svc(UNIT *uptr);
t_stat tod_reset(DEVICE *dptr);
uint32 tod_read(uint32 pa, size_t size);
void tod_write(uint32, uint32 val, size_t size);
#endif
