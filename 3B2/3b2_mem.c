/* 3b2_mem.c: AT&T 3B2 memory access routines

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

#include "3b2_mem.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_io.h"
#include "3b2_mmu.h"

t_bool addr_is_rom(uint32 pa)
{
    return (pa < rom_size);
}

t_bool addr_is_mem(uint32 pa)
{
    return (pa >= PHYS_MEM_BASE &&
            pa < (PHYS_MEM_BASE + MEM_SIZE));
}

t_bool addr_is_io(uint32 pa)
{
#if defined(REV3)
    return ((pa >= IO_BOTTOM && pa < IO_TOP) ||
            (pa >= CIO_BOTTOM && pa < CIO_TOP) ||
            (pa >= VCACHE_BOTTOM && pa < VCACHE_TOP) ||
            (pa >= BUB_BOTTOM && pa < BUB_TOP));
#else
    return ((pa >= IO_BOTTOM && pa < IO_TOP) ||
            (pa >= CIO_BOTTOM && pa < CIO_TOP));
#endif
}

/* Read Word (Physical Address) */
uint32 pread_w(uint32 pa)
{
    uint32 *m;
    uint32 index;

    if (pa & 3) {
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] Cannot read physical address. ALIGNMENT ISSUE: %08x\n",
                  R[NUM_PC], pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (addr_is_io(pa)) {
        return io_read(pa, 32);
    }

    if (addr_is_rom(pa)) {
        m = ROM;
        index = pa >> 2;
    } else if (addr_is_mem(pa)) {
        m = RAM;
        index = (pa - PHYS_MEM_BASE) >> 2;
    } else {
        return 0;
    }

    return m[index];
}

/*
 * Write Word (Physical Address)
 */
void pwrite_w(uint32 pa, uint32 val)
{
    if (pa & 3) {
        sim_debug(WRITE_MSG, &mmu_dev,
                  "[%08x] Cannot write physical address. ALIGNMENT ISSUE: %08x\n",
                  R[NUM_PC], pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (addr_is_io(pa)) {
        io_write(pa, val, 32);
        return;
    }

    if (addr_is_mem(pa)) {
        RAM[(pa - PHYS_MEM_BASE) >> 2] = val;
        return;
    }
}

/*
 * Read Halfword (Physical Address)
 */
uint16 pread_h(uint32 pa)
{
    uint32 *m;
    uint32 index;

    if (pa & 1) {
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] Cannot read physical address. ALIGNMENT ISSUE %08x\n",
                  R[NUM_PC], pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (addr_is_io(pa)) {
        return (uint16) io_read(pa, 16);
    }

    if (addr_is_rom(pa)) {
        m = ROM;
        index = pa >> 2;
    } else if (addr_is_mem(pa)) {
        m = RAM;
        index = (pa - PHYS_MEM_BASE) >> 2;
    } else {
        return 0;
    }

    if (pa & 2) {
        return m[index] & HALF_MASK;
    } else {
        return (m[index] >> 16) & HALF_MASK;
    }
}

/*
 * Write Halfword (Physical Address)
 */
void pwrite_h(uint32 pa, uint16 val)
{
    uint32 *m;
    uint32 index;
    uint32 wval = (uint32)val;

    if (pa & 1) {
        sim_debug(WRITE_MSG, &mmu_dev,
                  "[%08x] Cannot write physical address %08x, ALIGNMENT ISSUE\n",
                  R[NUM_PC], pa);
        CSRBIT(CSRALGN, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    }

    if (addr_is_io(pa)) {
        io_write(pa, val, 16);
        return;
    }

    if (addr_is_mem(pa)) {
        m = RAM;
        index = (pa - PHYS_MEM_BASE) >> 2;
    } else {
        return;
    }

    if (pa & 2) {
        m[index] = (m[index] & ~HALF_MASK) | wval;
    } else {
        m[index] = (m[index] & HALF_MASK) | (wval << 16);
    }
}

/*
 * Read Byte (Physical Address)
 */
uint8 pread_b(uint32 pa)
{
    uint32 data;
    int32 sc = (~(pa & 3) << 3) & 0x1f;

    if (addr_is_io(pa)) {
        return (uint8)(io_read(pa, 8));
    }

    if (addr_is_rom(pa)) {
        data = ROM[pa >> 2];
    } else if (addr_is_mem(pa)) {
        data = RAM[(pa - PHYS_MEM_BASE) >> 2];
    } else {
        return 0;
    }

    return (data >> sc) & BYTE_MASK;
}

/* Write Byte (Physical Address) */
void pwrite_b(uint32 pa, uint8 val)
{
    uint32 *m;
    int32 index;
    int32 sc = (~(pa & 3) << 3) & 0x1f;
    uint32 mask = 0xffu << sc;

    if (addr_is_io(pa)) {
        io_write(pa, val, 8);
        return;
    }

    if (addr_is_mem(pa)) {
        m = RAM;
        index = (pa - PHYS_MEM_BASE) >> 2;
        m[index] = (m[index] & ~mask) | (uint32) (val << sc);
        return;
    }
}

/* Read Byte (Virtual Address) */
uint8 read_b(uint32 va, uint8 r_acc)
{
    return pread_b(mmu_xlate_addr(va, r_acc));
}

/* Write Byte (Virtual Address) */
void write_b(uint32 va, uint8 val)
{
    pwrite_b(mmu_xlate_addr(va, ACC_W), val);
}

/* Read Halfword (Virtual Address) */
uint16 read_h(uint32 va, uint8 r_acc)
{
    return pread_h(mmu_xlate_addr(va, r_acc));
}

/* Write Halfword (Virtual Address) */
void write_h(uint32 va, uint16 val)
{
    pwrite_h(mmu_xlate_addr(va, ACC_W), val);
}

/* Read Word (Virtual Address) */
uint32 read_w(uint32 va, uint8 r_acc)
{
    return pread_w(mmu_xlate_addr(va, r_acc));
}

/* Write Word (Virtual Address) */
void write_w(uint32 va, uint32 val)
{
    pwrite_w(mmu_xlate_addr(va, ACC_W), val);
}

t_stat read_operand(uint32 va, uint8 *val)
{
    uint32 pa;
    t_stat succ;
 
    succ = mmu_decode_va(va, ACC_IF, TRUE, &pa);

    if (succ == SCPE_OK) {
        *val = pread_b(pa);
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
        if (addr_is_rom(pa) || addr_is_mem(pa)) {
            *val = pread_b(pa);
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
        if (addr_is_mem(pa)) {
            pwrite_b(pa, val);
            return SCPE_OK;
        } else {
            return SCPE_NXM;
        }
    } else {
        return succ;
    }
}
