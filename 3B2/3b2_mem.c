/* 3b2_mem.c: Memory Map Access Routines

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

#include "3b2_mem.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_io.h"
#include "3b2_mmu.h"
#include "3b2_stddev.h"
#include "3b2_dmac.h"

#if defined(REV3)
static uint32 ecc_addr;  /* ECC address */
static t_bool ecc_err;   /* ECC multi-bit error */
#endif

/*
 * ECC is simulated just enough to pass diagnostics, and no more.
 *
 * Checking and setting of ECC syndrome bits is a no-op for Rev 2.
 */
static SIM_INLINE void check_ecc(uint32 pa, t_bool write, uint8 src)
{
#if defined(REV3)
    /* Force ECC Syndrome mode enables a diagnostic mode on the AM2960
       data correction ICs */
    if (write && !CSR(CSRFECC)) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "ECC Error on Write. pa=%08x\n",
                  pa);
        ecc_addr = pa;
        ecc_err = TRUE;
    } else if (ecc_err && !write && pa == ecc_addr) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "ECC Error detected on Read. pa=%08x psw=%08x cur_ipl=%d csr=%08x\n",
                  pa, R[NUM_PSW], PSW_CUR_IPL, csr_data);
        flt[0] = ecc_addr & 0x3ffff;
        flt[1] = MA_CPU_IO|MA_CPU_BU;
        ecc_err = FALSE;
        CSRBIT(CSRFRF, TRUE);    /* Fault registers frozen */
        CSRBIT(CSRMBERR, TRUE);  /* Multi-bit error */
        CPU_SET_INT(INT_MBERR);
        /* Only abort if CPU is doing the read */
        if (src == BUS_CPU) {
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        }
    }
#endif
}

/* Read Word (Physical Address) */
uint32 pread_w(uint32 pa, uint8 src)
{
    uint8 *m;
    uint32 index = 0;

#if defined(REV3)
    if ((pa & 3) && (R[NUM_PSW] & PSW_EA_MASK) == 0) {
#else
    if (pa & 3) {
#endif
        sim_debug(READ_MSG, &mmu_dev,
                  "Cannot read physical address. ALIGNMENT ISSUE: %08x\n",
                  pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (IS_IO(pa)) {
        return io_read(pa, 32);
    }

    if (IS_ROM(pa)) {
        m = ROM;
        index = pa;
    } else if (IS_RAM(pa)) {
        check_ecc(pa, FALSE, src);
        m = RAM;
        index = (pa - PHYS_MEM_BASE);
    } else {
        return 0;
    }

    return ATOW(m, index);
}

/*
 * Write Word (Physical Address)
 */
void pwrite_w(uint32 pa, uint32 val, uint8 src)
{
    uint32 index;

    if (pa & 3) {
        sim_debug(WRITE_MSG, &mmu_dev,
                  "Cannot write physical address. ALIGNMENT ISSUE: %08x\n",
                  pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (IS_IO(pa)) {
        io_write(pa, val, 32);
        return;
    }

    if (IS_RAM(pa)) {
        check_ecc(pa, TRUE, src);
        index = pa - PHYS_MEM_BASE;
        RAM[index] = (val >> 24) & 0xff;
        RAM[index + 1] = (val >> 16) & 0xff;
        RAM[index + 2] = (val >> 8) & 0xff;
        RAM[index + 3] = val & 0xff;
        return;
    }
}

/*
 * Read Halfword (Physical Address)
 */
uint16 pread_h(uint32 pa, uint8 src)
{
    uint8 *m;
    uint32 index;

    if (pa & 1) {
        sim_debug(READ_MSG, &mmu_dev,
                  "Cannot read physical address. ALIGNMENT ISSUE %08x\n",
                  pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (IS_IO(pa)) {
        return (uint16) io_read(pa, 16);
    }

    if (IS_ROM(pa)) {
        m = ROM;
        index = pa;
    } else if (IS_RAM(pa)) {
        check_ecc(pa, FALSE, src);
        m = RAM;
        index = pa - PHYS_MEM_BASE;
    } else {
        return 0;
    }

    return ATOH(m, index);
}

/*
 * Write Halfword (Physical Address)
 */
void pwrite_h(uint32 pa, uint16 val, uint8 src)
{
    uint32 index;

#if defined(REV3)
    if ((pa & 1) && (R[NUM_PSW] & PSW_EA_MASK) == 0) {
#else
    if (pa & 1) {
#endif
        sim_debug(WRITE_MSG, &mmu_dev,
                  "Cannot write physical address %08x, ALIGNMENT ISSUE\n",
                  pa);
        CSRBIT(CSRALGN, TRUE);
    }

    if (IS_IO(pa)) {
        io_write(pa, val, 16);
        return;
    }

    if (IS_RAM(pa)) {
        check_ecc(pa, TRUE, src);
        index = pa - PHYS_MEM_BASE;
        RAM[index] = (val >> 8) & 0xff;
        RAM[index + 1] = val & 0xff;
        return;
    }
}

/*
 * Read Byte (Physical Address)
 */
uint8 pread_b(uint32 pa, uint8 src)
{
    if (IS_IO(pa)) {
        return (uint8)(io_read(pa, 8));
    }

    if (IS_ROM(pa)) {
        return ROM[pa];
    } else if (IS_RAM(pa)) {
        check_ecc(pa, FALSE, src);
        return RAM[pa - PHYS_MEM_BASE];
    } else {
        return 0;
    }
}

/* Write Byte (Physical Address) */
void pwrite_b(uint32 pa, uint8 val, uint8 src)
{
    uint32 index;

    if (IS_IO(pa)) {
        io_write(pa, val, 8);
        return;
    }

    if (IS_RAM(pa)) {
        check_ecc(pa, TRUE, src);
        index = pa - PHYS_MEM_BASE;
        RAM[index] = val;
        return;
    }
}

/* Write to ROM (used by ROM load) */
void pwrite_b_rom(uint32 pa, uint8 val) {
     if (IS_ROM(pa)) {
         ROM[pa] = val;
     }
 }

/* Read Byte (Virtual Address) */
uint8 read_b(uint32 va, uint8 r_acc, uint8 src)
{
    return pread_b(mmu_xlate_addr(va, r_acc), src);
}

/* Write Byte (Virtual Address) */
void write_b(uint32 va, uint8 val, uint8 src)
{
    pwrite_b(mmu_xlate_addr(va, ACC_W), val, src);
}

/* Read Halfword (Virtual Address) */
uint16 read_h(uint32 va, uint8 r_acc, uint8 src)
{
    return pread_h(mmu_xlate_addr(va, r_acc), src);
}

/* Write Halfword (Virtual Address) */
void write_h(uint32 va, uint16 val, uint8 src)
{
    pwrite_h(mmu_xlate_addr(va, ACC_W), val, src);
}

/* Read Word (Virtual Address) */
uint32 read_w(uint32 va, uint8 r_acc, uint8 src)
{
    return pread_w(mmu_xlate_addr(va, r_acc), src);
}

/* Write Word (Virtual Address) */
void write_w(uint32 va, uint32 val, uint8 src)
{
    pwrite_w(mmu_xlate_addr(va, ACC_W), val, src);
}

t_stat read_operand(uint32 va, uint8 *val)
{
    uint32 pa;
    t_stat succ;

    succ = mmu_decode_va(va, ACC_IF, TRUE, &pa);

    if (succ == SCPE_OK) {
        *val = pread_b(pa, BUS_CPU);
    } else {
        *val = 0;
    }

    return succ;
}

t_stat examine(uint32 va, uint8 *val)
{
    uint32 pa;
    t_stat succ;

    succ = mmu_decode_va(va, 0, FALSE, &pa);

    if (succ == SCPE_OK) {
        if (IS_ROM(pa) || IS_RAM(pa)) {
            *val = pread_b(pa, BUS_CPU);
            return SCPE_OK;
        } else {
            *val = 0;
            return SCPE_NXM;
        }
    } else {
        *val = 0;
        return succ;
    }
}

t_stat deposit(uint32 va, uint8 val)
{
    uint32 pa;
    t_stat succ;

    succ = mmu_decode_va(va, 0, FALSE, &pa);

    if (succ == SCPE_OK) {
        if (IS_RAM(pa)) {
            pwrite_b(pa, val, BUS_CPU);
            return SCPE_OK;
        } else {
            return SCPE_NXM;
        }
    } else {
        return succ;
    }
}
