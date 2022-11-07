/* 3b2_rev2_csr.c: ED System Board Control and Status Register

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

#include "3b2_rev2_csr.h"

#include "3b2_cpu.h"
#include "3b2_sys.h"
#include "3b2_timer.h"
#include "3b2_sys.h"

CSR_DATA csr_data;

BITFIELD csr_bits[] = {
    BIT(IOF),
    BIT(DMA),
    BIT(DISK),
    BIT(UART),
    BIT(PIR9),
    BIT(PIR8),
    BIT(CLK),
    BIT(IFLT),
    BIT(ITIM),
    BIT(FLOP),
    BIT(NA),
    BIT(LED),
    BIT(ALGN),
    BIT(RRST),
    BIT(PARE),
    BIT(TIMO),
    ENDBITS
};

UNIT csr_unit = {
    UDATA(NULL, UNIT_FIX, CSRSIZE)
};

REG csr_reg[] = {
    { HRDATADF(DATA, csr_data, 16, "CSR Data", csr_bits) },
    { NULL }
};

DEVICE csr_dev = {
    "CSR", &csr_unit, csr_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &csr_ex, &csr_dep, &csr_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat csr_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_reset(DEVICE *dptr)
{
    csr_data = 0;
    return SCPE_OK;
}

uint32 csr_read(uint32 pa, size_t size)
{
    uint32 reg = pa - CSRBASE;

    sim_debug(READ_MSG, &csr_dev,
              "CSR=%04x\n",
              csr_data);

    switch (reg) {
    case 0x2:
        if (size == 8) {
            return (csr_data >> 8) & 0xff;
        } else {
            return csr_data;
        }
    case 0x3:
        return csr_data & 0xff;
    default:
        return 0;
    }
}

void csr_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg = pa - CSRBASE;

    switch (reg) {
    case 0x03:    /* Clear Bus Timeout Error */
        csr_data &= ~CSRTIMO;
        break;
    case 0x07:    /* Clear Memory Parity Error */
        csr_data &= ~CSRPARE;
        break;
    case 0x0b:    /* Set System Reset Request */
        full_reset();
        cpu_boot(0, &cpu_dev);
        break;
    case 0x0f:    /* Clear Memory Alignment Fault */
        csr_data &= ~CSRALGN;
        break;
    case 0x13:    /* Set Failure LED */
        csr_data |= CSRLED;
        break;
    case 0x17:    /* Clear Failure LED */
        csr_data &= ~CSRLED;
        break;
    case 0x1b:    /* Set Floppy Motor On */
        csr_data |= CSRFLOP;
        break;
    case 0x1f:    /* Clear Floppy Motor On */
        csr_data &= ~CSRFLOP;
        break;
    case 0x23:    /* Set Inhibit Timers */
        sim_debug(WRITE_MSG, &csr_dev,
                  "SET INHIBIT TIMERS\n");
        csr_data |= CSRITIM;
        timer_gate(TIMER_INTERVAL, TRUE);
        break;
    case 0x27:    /* Clear Inhibit Timers */
        sim_debug(WRITE_MSG, &csr_dev,
                  "CLEAR INHIBIT TIMERS\n");
        csr_data &= ~CSRITIM;
        timer_gate(TIMER_INTERVAL, FALSE);
        break;
    case 0x2b:    /* Set Inhibit Faults */
        csr_data |= CSRIFLT;
        break;
    case 0x2f:    /* Clear Inhibit Faults */
        csr_data &= ~CSRIFLT;
        break;
    case 0x33:    /* Set PIR9 */
        csr_data |= CSRPIR9;
        CPU_SET_INT(INT_PIR9);
        break;
    case 0x37:    /* Clear PIR9 */
        csr_data &= ~CSRPIR9;
        CPU_CLR_INT(INT_PIR9);
        break;
    case 0x3b:    /* Set PIR8 */
        csr_data |= CSRPIR8;
        CPU_SET_INT(INT_PIR8);
        break;
    case 0x3f:    /* Clear PIR8 */
        csr_data &= ~CSRPIR8;
        CPU_CLR_INT(INT_PIR8);
        break;
    default:
        break;
    }
}
