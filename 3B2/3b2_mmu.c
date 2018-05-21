/* 3b2_mmu.c: AT&T 3B2 Model 400 MMU (WE32101) Implementation

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

#include "3b2_mmu.h"

UNIT mmu_unit = { UDATA(NULL, 0, 0) };

MMU_STATE mmu_state;

REG mmu_reg[] = {
    { HRDATAD (ENABLE, mmu_state.enabled, 1, "Enabled?")        },
    { HRDATAD (CONFIG, mmu_state.conf,   32, "Configuration")   },
    { HRDATAD (VAR,    mmu_state.var,    32, "Virtual Address") },
    { HRDATAD (FCODE,  mmu_state.fcode,  32, "Fault Code")      },
    { HRDATAD (FADDR,  mmu_state.faddr,  32, "Fault Address")   },
    { BRDATA  (SDCL,   mmu_state.sdcl,   16, 32, MMU_SDCS)      },
    { BRDATA  (SDCR,   mmu_state.sdch,   16, 32, MMU_SDCS)      },
    { BRDATA  (PDCLL,  mmu_state.pdcll,  16, 32, MMU_PDCS)      },
    { BRDATA  (PDCLH,  mmu_state.pdclh,  16, 32, MMU_PDCS)      },
    { BRDATA  (PDCRL,  mmu_state.pdcrl,  16, 32, MMU_PDCS)      },
    { BRDATA  (PDCRH,  mmu_state.pdcrh,  16, 32, MMU_PDCS)      },
    { BRDATA  (SRAMA,  mmu_state.sra,    16, 32, MMU_SRS)       },
    { BRDATA  (SRAMB,  mmu_state.srb,    16, 32, MMU_SRS)       },
    { NULL }
};

DEVICE mmu_dev = {
    "MMU", &mmu_unit, mmu_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &mmu_init,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat mmu_init(DEVICE *dptr)
{
    flush_caches();
    return SCPE_OK;
}

uint32 mmu_read(uint32 pa, size_t size)
{
    uint32 offset;
    uint32 data = 0;

    offset = (pa >> 2) & 0x1f;

    switch ((pa >> 8) & 0xf) {
    case MMU_SDCL:
        data = mmu_state.sdcl[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] [pa=%08x] MMU_SDCL[%d] = %08x\n",
                  R[NUM_PC], pa, offset, data);
        break;
    case MMU_SDCH:
        data = mmu_state.sdch[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_SDCH[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_PDCRL:
        data = mmu_state.pdcrl[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_PDCRL[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_PDCRH:
        data = mmu_state.pdcrh[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_PDCRH[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_PDCLL:
        data = mmu_state.pdcll[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_PDCLL[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_PDCLH:
        data = mmu_state.pdclh[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_PDCLH[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_SRAMA:
        data = mmu_state.sra[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_SRAMA[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_SRAMB:
        data = mmu_state.srb[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_SRAMB[%d] = %08x\n",
                  R[NUM_PC], offset, data);
        break;
    case MMU_FC:
        data = mmu_state.fcode;
        break;
    case MMU_FA:
        data = mmu_state.faddr;
        break;
    case MMU_CONF:
        data = mmu_state.conf & 0x7;
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_CONF = %08x\n",
                  R[NUM_PC], data);
        break;
    case MMU_VAR:
        data = mmu_state.var;
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] MMU_VAR = %08x\n",
                  R[NUM_PC], data);
        break;
    }

    return data;
}

void mmu_write(uint32 pa, uint32 val, size_t size)
{
    uint32 offset;

    offset = (pa >> 2) & 0x1f;

    switch ((pa >> 8) & 0xf) {
    case MMU_SDCL:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_SDCL[%d] = %08x\n",
                  offset, val);
        mmu_state.sdcl[offset] = val;
        break;
    case MMU_SDCH:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_SDCH[%d] = %08x\n",
                  offset, val);
        mmu_state.sdch[offset] = val;
        break;
    case MMU_PDCRL:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_PDCRL[%d] = %08x\n",
                  offset, val);
        mmu_state.pdcrl[offset] = val;
        break;
    case MMU_PDCRH:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_PDCRH[%d] = %08x\n",
                  offset, val);
        mmu_state.pdcrh[offset] = val;
        break;
    case MMU_PDCLL:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_PDCLL[%d] = %08x\n",
                  offset, val);
        mmu_state.pdcll[offset] = val;
        break;
    case MMU_PDCLH:
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_PDCLH[%d] = %08x\n",
                  offset, val);
        mmu_state.pdclh[offset] = val;
        break;
    case MMU_SRAMA:
        offset = offset & 3;
        mmu_state.sra[offset] = val;
        mmu_state.sec[offset].addr = val & 0xffffffe0;
        /* We flush the entire section on writing SRAMA */
        sim_debug(WRITE_MSG, &mmu_dev,
                  "[%08x] MMU_SRAMA[%d] = %08x\n",
                  R[NUM_PC], offset, val);
        flush_cache_sec((uint8) offset);
        break;
    case MMU_SRAMB:
        offset = offset & 3;
        mmu_state.srb[offset] = val;
        mmu_state.sec[offset].len = (val >> 10) & 0x1fff;
        /* We do not flush the cache on writing SRAMB */
        sim_debug(WRITE_MSG, &mmu_dev,
                  "[%08x] MMU_SRAMB[%d] = %08x (len=%06x)\n",
                  R[NUM_PC], offset, val, mmu_state.sec[offset].len);
        break;
    case MMU_FC:
        mmu_state.fcode = val;
        break;
    case MMU_FA:
        mmu_state.faddr = val;
        break;
    case MMU_CONF:
        mmu_state.conf = val & 0x7;
        break;
    case MMU_VAR:
        mmu_state.var = val;
        flush_sdce(val);
        flush_pdce(val);
        break;
    }
}

t_bool addr_is_rom(uint32 pa)
{
    return (pa < BOOT_CODE_SIZE);
}

t_bool addr_is_mem(uint32 pa)
{
    return (pa >= PHYS_MEM_BASE &&
            pa < (PHYS_MEM_BASE + MEM_SIZE));
}

t_bool addr_is_io(uint32 pa)
{
    return ((pa >= IO_BOTTOM && pa < IO_TOP) ||
            (pa >= CIO_BOTTOM && pa < CIO_TOP));
}

/*
 * Raw physical reads and writes.
 *
 * The WE32100 is a BIG-endian machine, meaning that words are
 * arranged in increasing address from most-significant byte to
 * least-significant byte.
 */

/*
 * Read Word (Physical Address)
 */
uint32 pread_w(uint32 pa)
{
    uint32 *m;
    uint32 index;

    if (pa & 3) {
        sim_debug(READ_MSG, &mmu_dev,
                  "[%08x] Cannot read physical address. ALIGNMENT ISSUE: %08x\n",
                  R[NUM_PC], pa);
        csr_data |= CSRALGN;
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
        csr_data |= CSRALGN;
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
        csr_data |= CSRALGN;
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
        csr_data |= CSRALGN;
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

/*
 * Write Byte (Physical Address)
 */
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

/* Helper functions for MMU decode. */

/*
 * Get the Segment Descriptor for a virtual address on a cache miss.
 *
 * Returns SCPE_OK on success, SCPE_NXM on failure.
 *
 * If SCPE_NXM is returned, a failure code and fault address will be
 * set in the appropriate registers.
 *
 * As always, the flag 'fc' may be set to FALSE to avoid certain
 * typses of fault checking.
 *
 */
t_stat mmu_get_sd(uint32 va, uint8 r_acc, t_bool fc,
                  uint32 *sd0, uint32 *sd1)
{
    /* We immediately do some bounds checking (fc flag is not checked
     * because this is a fatal error) */
    if (SSL(va) > SRAMB_LEN(va)) {
        MMU_FAULT(MMU_F_SDTLEN);
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "[%08x] SDT Length Fault. sramb_len=%x ssl=%x va=%08x\n",
                  R[NUM_PC], SRAMB_LEN(va), SSL(va), va);
        return SCPE_NXM;
    }

    /* sd0 contains the segment descriptor, sd1 contains a pointer to
       the PDT or Segment */
    *sd0 = pread_w(SD_ADDR(va));
    *sd1 = pread_w(SD_ADDR(va) + 4);

    if (!SD_VALID(*sd0)) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "[%08x] Invalid Segment Descriptor. va=%08x sd0=%08x\n",
                  R[NUM_PC], va, *sd0);
        MMU_FAULT(MMU_F_INV_SD);
        return SCPE_NXM;
    }

    /* TODO: Handle indirect lookups. */
    if (SD_INDIRECT(*sd0)) {
        stop_reason = STOP_MMU;
        return SCPE_NXM;
    }

    /* If the segment descriptor isn't present, we need to
     * fail out */
    if (!SD_PRESENT(*sd0)) {
        if (SD_CONTIG(*sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Segment Not Present. va=%08x",
                      R[NUM_PC], va);
            MMU_FAULT(MMU_F_SEG_NOT_PRES);
            return SCPE_NXM;
        } else {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] PDT Not Present. va=%08x",
                      R[NUM_PC], va);
            MMU_FAULT(MMU_F_PDT_NOT_PRES);
            return SCPE_NXM;
        }
    }

    if (SHOULD_CACHE_SD(*sd0)) {
        put_sdce(va, *sd0, *sd1);
    }

    return SCPE_OK;
}

/*
 * Load a page descriptor from memory
 */
t_stat mmu_get_pd(uint32 va, uint8 r_acc, t_bool fc,
                  uint32 sd0, uint32 sd1,
                  uint32 *pd, uint8 *pd_acc)
{
    uint32 pd_addr;

    /* Where do we find the page descriptor? */
    pd_addr = SD_SEG_ADDR(sd1) + (PSL(va) * 4);

    /* Bounds checking on length */
    if ((PSL(va) * 4) >= MAX_OFFSET(sd0)) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "[%08x] PDT Length Fault. "
                  "PDT Offset=%08x Max Offset=%08x va=%08x\n",
                  R[NUM_PC], (PSL(va) * 4),
                  MAX_OFFSET(sd0), va);
        MMU_FAULT(MMU_F_PDTLEN);
        return SCPE_NXM;
    }

    *pd = pread_w(pd_addr);

    /* Copy the access flags from the SD */
    *pd_acc = SD_ACC(sd0);

    /* Cache it */
    if (SHOULD_CACHE_PD(*pd)) {
        put_pdce(va, sd0, *pd);
    }

    return SCPE_OK;
}

/*
 * Decode an address from a contiguous segment.
 */
t_stat mmu_decode_contig(uint32 va, uint8 r_acc,
                         uint32 sd0, uint32 sd1,
                         t_bool fc, uint32 *pa)
{
    if (fc) {
        /* Update R and M bits if configured */
        if (SHOULD_UPDATE_SD_R_BIT(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Updating R bit in SD\n",
                      R[NUM_PC]);
            mmu_update_sd(va, SD_R_MASK);
        }

        if (SHOULD_UPDATE_SD_M_BIT(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Updating M bit in SD\n",
                      R[NUM_PC]);
            mmu_update_sd(va, SD_M_MASK);
        }

        /* Generate object trap if needed */
        if (SD_TRAP(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Object Trap. va=%08x",
                      R[NUM_PC], va);
            MMU_FAULT(MMU_F_OTRAP);
            return SCPE_NXM;
        }
    }

    *pa = SD_SEG_ADDR(sd1) + SOT(va);
    return SCPE_OK;
}

t_stat mmu_decode_paged(uint32 va, uint8 r_acc, t_bool fc,
                        uint32 sd1, uint32 pd,
                        uint8 pd_acc, uint32 *pa)
{
    /* If the PD is not marked present, fail */
    if (!PD_PRESENT(pd)) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "[%08x] Page Not Present. "
                  "pd=%08x r_acc=%x va=%08x\n",
                  R[NUM_PC], pd, r_acc, va);
        MMU_FAULT(MMU_F_PAGE_NOT_PRES);
        return SCPE_NXM;
    }

    if (fc) {
        /* If this is a write or interlocked read access, and
           the 'W' bit is set, trigger a write fault */
        if ((r_acc == ACC_W || r_acc == ACC_IR) && PD_WFAULT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Page Write Fault. va=%08x\n",
                      R[NUM_PC], va);
            MMU_FAULT(MMU_F_PW);
            return SCPE_NXM;
        }

        /* If this is a write, modify the M bit */
        if (SHOULD_UPDATE_PD_M_BIT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Updating M bit in PD\n",
                      R[NUM_PC]);
            mmu_update_pd(va, PD_LOC(sd1, va), PD_M_MASK);
        }

        /* Modify the R bit and write it back */
        if (SHOULD_UPDATE_PD_R_BIT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Updating R bit in PD\n",
                      R[NUM_PC]);
            mmu_update_pd(va, PD_LOC(sd1, va), PD_R_MASK);
        }
    }

    *pa = PD_ADDR(pd) + POT(va);
    return SCPE_OK;
}

/*
 * Translate a virtual address into a physical address.
 *
 * If "fc" is false, this function will bypass:
 *
 *   - Access flag checks
 *   - Cache insertion
 *   - Setting MMU fault registers
 *   - Modifying segment and page descriptor bits
 */

t_stat mmu_decode_va(uint32 va, uint8 r_acc, t_bool fc, uint32 *pa)
{
    uint32 sd0, sd1, pd;
    uint8 pd_acc;
    t_stat sd_cached, pd_cached;

    if (!mmu_state.enabled) {
        *pa = va;
        return SCPE_OK;
    }

    /* We must check both caches first to determine what kind of miss
       processing to do. */

    sd_cached = get_sdce(va, &sd0, &sd1);
    pd_cached = get_pdce(va, &pd, &pd_acc);

    if (sd_cached == SCPE_OK && pd_cached != SCPE_OK) {
        if (SD_PAGED(sd0) && mmu_get_pd(va, r_acc, fc, sd0, sd1, &pd, &pd_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Could not get PD (partial miss). r_acc=%d, fc=%d, va=%08x\n",
                      R[NUM_PC], r_acc, fc, va);
            return SCPE_NXM;
        }
    } else if (sd_cached != SCPE_OK && pd_cached == SCPE_OK) {
        if (mmu_get_sd(va, r_acc, fc, &sd0, &sd1) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Could not get SD (partial miss). r_acc=%d, fc=%d, va=%08x\n",
                      R[NUM_PC], r_acc, fc, va);
            return SCPE_NXM;
        }
    } else if (sd_cached != SCPE_OK && pd_cached != SCPE_OK) {
        if (mmu_get_sd(va, r_acc, fc, &sd0, &sd1) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Could not get SD (full miss). r_acc=%d, fc=%d, va=%08x\n",
                      R[NUM_PC], r_acc, fc, va);
            return SCPE_NXM;
        }

        if (SD_PAGED(sd0) && mmu_get_pd(va, r_acc, fc, sd0, sd1, &pd, &pd_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] Could not get PD (full miss). r_acc=%d, fc=%d, va=%08x\n",
                      R[NUM_PC], r_acc, fc, va);
            return SCPE_NXM;
        }
    }

    if (SD_PAGED(sd0)) {
        if (fc && mmu_check_perm(pd_acc, r_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] PAGED: NO ACCESS TO MEMORY AT %08x.\n"
                      "\t\tcpu_cm=%d r_acc=%x pd_acc=%02x\n"
                      "\t\tpd=%08x psw=%08x\n",
                      R[NUM_PC], va, CPU_CM, r_acc, pd_acc,
                      pd, R[NUM_PSW]);
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }
        if (PD_LAST(pd) && (PSL_C(va) | POT(va)) >= MAX_OFFSET(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] PAGED: Segment Offset Fault.\n",
                      R[NUM_PC]);
            MMU_FAULT(MMU_F_SEG_OFFSET);
            return SCPE_NXM;
        }
        return mmu_decode_paged(va, r_acc, fc, sd1, pd, pd_acc, pa);
    } else {
        if (fc && mmu_check_perm(SD_ACC(sd0), r_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] CONTIGUOUS: NO ACCESS TO MEMORY AT %08x.\n"
                      "\t\tsd0=%08x sd0_addr=%08x\n"
                      "\t\tcpu_cm=%d acc_req=%x sd_acc=%02x\n",
                      R[NUM_PC], va,
                      sd0, SD_ADDR(va),
                      CPU_CM, r_acc, SD_ACC(sd0));
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }
        if (SOT(va) >= MAX_OFFSET(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "[%08x] CONTIGUOUS: Segment Offset Fault. "
                      "sd0=%08x sd_addr=%08x SOT=%08x len=%08x va=%08x\n",
                      R[NUM_PC], sd0, SD_ADDR(va), SOT(va),
                      MAX_OFFSET(sd0), va);
            MMU_FAULT(MMU_F_SEG_OFFSET);
            return SCPE_NXM;
        }
        return mmu_decode_contig(va, r_acc, sd0, sd1, fc, pa);
    }
}

t_stat examine(uint32 va, uint8 *val) {
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

t_stat deposit(uint32 va, uint8 val) {
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

t_stat read_operand(uint32 va, uint8 *val) {
    uint32 pa;
    t_stat succ;

    succ = mmu_decode_va(va, ACC_OF, TRUE, &pa);

    if (succ == SCPE_OK) {
        *val = pread_b(pa);
    } else {
        *val = 0;
    }

    return succ;
}

uint32 mmu_xlate_addr(uint32 va, uint8 r_acc)
{
    uint32 pa;
    t_stat succ;

    succ = mmu_decode_va(va, r_acc, TRUE, &pa);

    if (succ == SCPE_OK) {
        mmu_state.var = va;
        return pa;
    } else {
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        return 0;
    }
}

void mmu_enable()
{
    sim_debug(EXECUTE_MSG, &mmu_dev,
              "[%08x] Enabling MMU.\n",
              R[NUM_PC]);
    mmu_state.enabled = TRUE;
}

void mmu_disable()
{
    sim_debug(EXECUTE_MSG, &mmu_dev,
              "[%08x] Disabling MMU.\n",
              R[NUM_PC]);
    mmu_state.enabled = FALSE;
}

/*
 * MMU Virtual Read and Write Functions
 */

uint8 read_b(uint32 va, uint8 r_acc)
{
    return pread_b(mmu_xlate_addr(va, r_acc));
}

uint16 read_h(uint32 va, uint8 r_acc)
{
    return pread_h(mmu_xlate_addr(va, r_acc));
}

uint32 read_w(uint32 va, uint8 r_acc)
{
    return pread_w(mmu_xlate_addr(va, r_acc));
}

void write_b(uint32 va, uint8 val)
{
    pwrite_b(mmu_xlate_addr(va, ACC_W), val);
}

void write_h(uint32 va, uint16 val)
{
    pwrite_h(mmu_xlate_addr(va, ACC_W), val);
}

void write_w(uint32 va, uint32 val)
{
    pwrite_w(mmu_xlate_addr(va, ACC_W), val);
}
