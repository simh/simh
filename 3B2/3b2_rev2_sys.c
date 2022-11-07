/* 3b2_rev2_sys.c: Version 2 (3B2/400) System Definition

   Copyright (c) 2017-2022, Seth J. Morabito

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

#include "3b2_defs.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_ctc.h"
#include "3b2_id.h"
#include "3b2_if.h"
#include "3b2_iu.h"
#include "3b2_ni.h"
#include "3b2_ports.h"
#include "3b2_mau.h"
#include "3b2_stddev.h"
#include "3b2_timer.h"

char sim_name[] = "AT&T 3B2/400";

DEVICE *sim_devices[] = {
    &cpu_dev,
    &mmu_dev,
    &mau_dev,
    &timer_dev,
    &tod_dev,
    &nvram_dev,
    &csr_dev,
    &tti_dev,
    &tto_dev,
    &contty_dev,
    &iu_timer_dev,
    &dmac_dev,
    &if_dev,
    &id_dev,
    &ports_dev,
    &ctc_dev,
    &ni_dev,
    NULL
};

void full_reset()
{
    cpu_reset(&cpu_dev);
    mau_reset(&mau_dev);
    tti_reset(&tti_dev);
    contty_reset(&contty_dev);
    iu_timer_reset(&iu_timer_dev);
    timer_reset(&timer_dev);
    if_reset(&if_dev);
    id_reset(&id_dev);
    csr_reset(&csr_dev);
    ports_reset(&ports_dev);
    ctc_reset(&ctc_dev);
    ni_reset(&ni_dev);
}
