/* 3b2_rev3_csr.c: CM518B System Board Control, Status & Error Register

   Copyright (c) 2020-2022, Seth J. Morabito

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

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_if.h"
#include "3b2_timer.h"
#include "3b2_sys.h"

CSR_DATA csr_data;

BITFIELD csr_bits[] = {
    BIT(UTIM),
    BIT(PWDN),
    BIT(OI15),
    BIT(IUINT),
    BIT(IUDMA),
    BIT(PIR9),
    BIT(PIR8),
    BIT(IUTIM),

    BIT(ISTY),
    BIT(IUBUS),
    BIT(IFLT),
    BIT(ISBER),
    BIT(IBUS),
    BIT(IBUB),
    BIT(FECC),
    BIT(THERM),

    BIT(FLED),
    BIT(PSPWR),
    BIT(FLSPD),
    BIT(FLSD1),
    BIT(FLMOT),
    BIT(FLDEN),
    BIT(FLSZ),
    BIT(SBER),

    BIT(MBER),
    BIT(UBFL),
    BIT(TIMO),
    BIT(FLTFR),
    BIT(DALGN),
    BIT(STTIM),
    BIT(ABRT),
    BIT(RSTR),

    ENDBITS
};

UNIT csr_unit = {
    UDATA(NULL, UNIT_FIX, CSRSIZE)
};

REG csr_reg[] = {
    { HRDATADF(DATA, csr_data, 32, "CSR Data", csr_bits) },
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
    CSRBIT(CSRFECC, TRUE);
    CSRBIT(CSRTHERM, FALSE);
    CSRBIT(CSRITIM, TRUE);
    CSRBIT(CSRISTIM, TRUE);
    CSRBIT(CSRIBUB, TRUE);
    CSRBIT(CSRPWRSPDN, FALSE);
    CSRBIT(CSRFLPMO, TRUE);

    return SCPE_OK;
}

uint32 csr_read(uint32 pa, size_t size)
{
    uint32 reg = (pa - CSRBASE) & 0xff;

    switch (reg & 0xf0) {
    case 0x00:
        return csr_data & 0xff;
    case 0x20:
        return (csr_data >> 8) & 0xff;
    case 0x40:
        return (csr_data >> 16) & 0xff;
    case 0x60:
        return (csr_data >> 24) & 0xff;
    default:
        sim_debug(WRITE_MSG, &csr_dev,
                  "CSR READ. Warning, unexpected register = %02x)\n",
                  reg);
        return 0;
    }
}

#define SET_INT(flag, val) {                     \
        if (val) {                               \
            CPU_SET_INT(flag);                   \
        } else {                                 \
            CPU_CLR_INT(flag);                   \
        }                                        \
    }

void csr_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg = pa - CSRBASE;

    switch (reg) {

    case 0x00:
        CSRBIT(CSRCLK, val);
        SET_INT(INT_CLOCK, val);
        break;
    case 0x04:
        CSRBIT(CSRPWRDN, val);
        SET_INT(INT_PWRDWN, val);
        break;
    case 0x08:
        CSRBIT(CSROPINT15, val);
        SET_INT(INT_BUS_OP, val);
        break;
    case 0x0c:
        CSRBIT(CSRUART, val);
        SET_INT(INT_UART, val);
        break;
    case 0x10:
        CSRBIT(CSRDMA, val);
        SET_INT(INT_UART_DMA, val);
        break;
    case 0x14:
        CSRBIT(CSRPIR9, val);
        SET_INT(INT_PIR9, val);
        break;
    case 0x18:
        CSRBIT(CSRPIR8, val);
        SET_INT(INT_PIR8, val);
        break;
    case 0x1c:
        CSRBIT(CSRITIM, val);
        timer_gate(TIMER_INTERVAL, CSR(CSRITIM));
        break;
    case 0x20:
        CSRBIT(CSRISTIM, val);
        timer_gate(TIMER_SANITY, CSR(CSRISTIM));
        break;
    case 0x24:
        CSRBIT(CSRITIMO, val);
        timer_gate(TIMER_BUS, CSR(CSRITIMO));
        break;
    case 0x28:
        CSRBIT(CSRICPUFLT, val);
        break;
    case 0x2c:
        CSRBIT(CSRISBERR, val);
        break;
    case 0x30:
        CSRBIT(CSRIIOBUS, val);
        break;
    case 0x34:
        CSRBIT(CSRIBUB, val);
        break;
    case 0x38:
        CSRBIT(CSRFECC, val);
        sim_debug(WRITE_MSG, &csr_dev,
                  "CSR WRITE. Force ECC Syndrome = %d\n",
                  val);
        break;
    case 0x3c:
        CSRBIT(CSRTHERM, val);
        cpu_nmi = val ? TRUE : FALSE; /* Immediate NMI */
        break;
    case 0x40:
        CSRBIT(CSRLED, val);
        break;
    case 0x44:
        CSRBIT(CSRPWRSPDN, val);
        if (!val) {
            /* Stop the simulator - power down */
            stop_reason = STOP_POWER;
        }
        break;
    case 0x48:
        CSRBIT(CSRFLPFST, val);
        break;
    case 0x4c: /* Floppy Side 1: Set when Cleared */
        if_state.side = (val & 1) ? 0 : 1;
        CSRBIT(CSRFLPS1, val & 1);
        break;
    case 0x50:
        CSRBIT(CSRFLPMO, val);
        break;
    case 0x54:
        CSRBIT(CSRFLPDEN, val);
        break;
    case 0x58:
        CSRBIT(CSRFLPSZ, val);
        break;
    case 0x5c:
        CSRBIT(CSRSBERR, val);
        if (val) {
            if (!(csr_data & CSRISBERR)) {
                SET_INT(INT_SBERR, TRUE);
            }
        } else {
            SET_INT(INT_SBERR, FALSE);
        }
        break;
    case 0x60:
        CSRBIT(CSRMBERR, val);
        SET_INT(INT_MBERR, val);
        break;
    case 0x64:
        CSRBIT(CSRUBUBF, val);
        SET_INT(INT_BUS_RXF, val);
        break;
    case 0x68:
        CSRBIT(CSRTIMO, val);
        if (val) {
            if (!(csr_data & CSRITIMO)) {
                SET_INT(INT_BUS_TMO, TRUE);
            }
        } else {
            SET_INT(INT_BUS_TMO, FALSE);
        }
        break;
    case 0x6c:
        CSRBIT(CSRFRF, val);
        break;
    case 0x70:
        CSRBIT(CSRALGN, val);
        break;
    case 0x74:
        CSRBIT(CSRSTIMO, val);
        cpu_nmi = val ? TRUE : FALSE; /* Immediate NMI */
        break;
    case 0x78:
        CSRBIT(CSRABRT, val);
        cpu_nmi = val ? TRUE : FALSE; /* Immediate NMI */
        break;
    case 0x7c:
        /* System reset request */
        full_reset();
        cpu_boot(0, &cpu_dev);
        break;
    default:
        /* Do nothing */
        break;
    }
}
