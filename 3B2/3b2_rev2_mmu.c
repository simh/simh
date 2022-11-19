/* 3b2_rev2_mmu.c: WE32101 MMU

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


#include "3b2_cpu.h"
#include "3b2_mmu.h"
#include "3b2_sys.h"
#include "3b2_mem.h"

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
    "MMU",                          /* name */
    &mmu_unit,                      /* units */
    mmu_reg,                        /* registers */
    NULL,                           /* modifiers */
    1,                              /* #units */
    16,                             /* address radix */
    8,                              /* address width */
    4,                              /* address incr */
    16,                             /* data radix */
    32,                             /* data width */
    NULL,                           /* examine routine */
    NULL,                           /* deposit routine */
    &mmu_init,                      /* reset routine */
    NULL,                           /* boot routine */
    NULL,                           /* attach routine */
    NULL,                           /* detach routine */
    NULL,                           /* context */
    DEV_DEBUG,                      /* flags */
    0,                              /* debug control flags */
    sys_deb_tab,                    /* debug flag names */
    NULL,                           /* memory size change */
    NULL,                           /* logical name */
    NULL,                           /* help routine */
    NULL,                           /* attach help routine */
    NULL,                           /* help context */
    &mmu_description                /* device description */
};

/*
 * Find an SD in the cache.
 */
static SIM_INLINE t_stat get_sdce(uint32 va, uint32 *sd0, uint32 *sd1)
{
    uint32 tag, sdch, sdcl;
    uint8 ci;

    ci    = (SID(va) * NUM_SDCE) + SD_IDX(va);
    tag   = SD_TAG(va);

    sdch  = mmu_state.sdch[ci];
    sdcl  = mmu_state.sdcl[ci];

    if ((sdch & SD_GOOD_MASK) && SDCE_TAG(sdcl) == tag) {
        *sd0 = SDCE_TO_SD0(sdch, sdcl);
        *sd1 = SDCE_TO_SD1(sdch);
        return SCPE_OK;
    }

    return SCPE_NXM;
}

/*
 * Find a PD in the cache. Sets both the PD and the cached access
 * permissions.
 */
static SIM_INLINE t_stat get_pdce(uint32 va, uint32 *pd, uint8 *pd_acc)
{
    uint32 tag, pdcll, pdclh, pdcrl, pdcrh;
    uint8 ci;

    ci    = (SID(va) * NUM_PDCE) + PD_IDX(va);
    tag   = PD_TAG(va);

    /* Left side */
    pdcll = mmu_state.pdcll[ci];
    pdclh = mmu_state.pdclh[ci];

    /* Right side */
    pdcrl = mmu_state.pdcrl[ci];
    pdcrh = mmu_state.pdcrh[ci];

    /* Search L and R to find a good entry with a matching tag. */
    if ((pdclh & PD_GOOD_MASK) && PDCXL_TAG(pdcll) == tag) {
        *pd = PDCXH_TO_PD(pdclh);
        *pd_acc = PDCXL_TO_ACC(pdcll);
        return SCPE_OK;
    } else if ((pdcrh & PD_GOOD_MASK) && PDCXL_TAG(pdcrl) == tag) {
        *pd = PDCXH_TO_PD(pdcrh);
        *pd_acc = PDCXL_TO_ACC(pdcrl);
        return SCPE_OK;
    }

    return SCPE_NXM;
}

static SIM_INLINE void put_sdce(uint32 va, uint32 sd0, uint32 sd1)
{
    uint8 ci;

    ci    = (SID(va) * NUM_SDCE) + SD_IDX(va);

    mmu_state.sdcl[ci] = SD_TO_SDCL(va, sd0);
    mmu_state.sdch[ci] = SD_TO_SDCH(sd0, sd1);
}


static SIM_INLINE void put_pdce(uint32 va, uint32 sd0, uint32 pd)
{
    uint8  ci;

    ci    = (SID(va) * NUM_PDCE) + PD_IDX(va);

    /* Cache Replacement Algorithm
     * (from the WE32101 MMU Information Manual)
     *
     * 1. If G==0 for the left-hand entry, the new PD is cached in the
     *    left-hand entry and the U bit (left-hand side) is cleared to
     *    0.
     *
     * 2. If G==1 for the left-hand entry, and G==0 for the right-hand
     *    entry, the new PD is cached in the right-hand entry and the
     *    U bit (left-hand side) is set to 1.
     *
     * 3. If G==1 for both entries, the U bit in the left-hand entry
     *    is examined. If U==0, the new PD is cached in the right-hand
     *    entry of the PDC row and U is set to 1. If U==1, it is
     *    cached in the left-hand entry and U is cleared to 0.
     */

    if ((mmu_state.pdclh[ci] & PD_GOOD_MASK) == 0) {
        /* Use the left entry */
        mmu_state.pdcll[ci] = SD_TO_PDCXL(va, sd0);
        mmu_state.pdclh[ci] = PD_TO_PDCXH(pd, sd0);
        mmu_state.pdclh[ci] &= ~PDCLH_USED_MASK;
    } else if ((mmu_state.pdcrh[ci] & PD_GOOD_MASK) == 0) {
        /* Use the right entry */
        mmu_state.pdcrl[ci] = SD_TO_PDCXL(va, sd0);
        mmu_state.pdcrh[ci] = PD_TO_PDCXH(pd, sd0);
        mmu_state.pdclh[ci] |= PDCLH_USED_MASK;
    } else {
        /* Pick the least-recently-replaced side */
        if (mmu_state.pdclh[ci] & PDCLH_USED_MASK) {
            mmu_state.pdcll[ci] = SD_TO_PDCXL(va, sd0);
            mmu_state.pdclh[ci] = PD_TO_PDCXH(pd, sd0);
            mmu_state.pdclh[ci] &= ~PDCLH_USED_MASK;
        } else {
            mmu_state.pdcrl[ci] = SD_TO_PDCXL(va, sd0);
            mmu_state.pdcrh[ci] = PD_TO_PDCXH(pd, sd0);
            mmu_state.pdclh[ci] |= PDCLH_USED_MASK;
        }
    }
}

static SIM_INLINE void flush_sdce(uint32 va)
{
    uint8 ci;

    ci  = (SID(va) * NUM_SDCE) + SD_IDX(va);

    if (mmu_state.sdch[ci] & SD_GOOD_MASK) {
        mmu_state.sdch[ci] &= ~SD_GOOD_MASK;
    }
}

static SIM_INLINE void flush_pdce(uint32 va)
{
    uint32 tag, pdcll, pdclh, pdcrl, pdcrh;
    uint8 ci;

    ci  = (SID(va) * NUM_PDCE) + PD_IDX(va);
    tag = PD_TAG(va);

    /* Left side */
    pdcll = mmu_state.pdcll[ci];
    pdclh = mmu_state.pdclh[ci];
    /* Right side */
    pdcrl = mmu_state.pdcrl[ci];
    pdcrh = mmu_state.pdcrh[ci];

    /* Search L and R to find a good entry with a matching tag. */
    if ((pdclh & PD_GOOD_MASK) && PDCXL_TAG(pdcll) == tag)  {
        mmu_state.pdclh[ci] &= ~PD_GOOD_MASK;
    } else if ((pdcrh & PD_GOOD_MASK) && PDCXL_TAG(pdcrl) == tag) {
        mmu_state.pdcrh[ci] &= ~PD_GOOD_MASK;
    }
}

static SIM_INLINE void flush_cache_sec(uint8 sec)
{
    int i;

    for (i = 0; i < NUM_SDCE; i++) {
        mmu_state.sdch[(sec * NUM_SDCE) + i] &= ~SD_GOOD_MASK;
    }
    for (i = 0; i < NUM_PDCE; i++) {
        mmu_state.pdclh[(sec * NUM_PDCE) + i] &= ~PD_GOOD_MASK;
        mmu_state.pdcrh[(sec * NUM_PDCE) + i] &= ~PD_GOOD_MASK;
    }
}

static SIM_INLINE void flush_caches()
{
    uint8 i;

    for (i = 0; i < NUM_SEC; i++) {
        flush_cache_sec(i);
    }
}

static SIM_INLINE t_stat mmu_check_perm(uint8 flags, uint8 r_acc)
{
    switch(MMU_PERM(flags)) {
    case 0:  /* No Access */
        return SCPE_NXM;
    case 1:  /* Exec Only */
        if (r_acc != ACC_IF &&
            r_acc != ACC_IFAD) {
            return SCPE_NXM;
        }
        return SCPE_OK;
    case 2: /* Read / Execute */
        if (r_acc != ACC_IF &&
            r_acc != ACC_IFAD &&
            r_acc != ACC_OF &&
            r_acc != ACC_AF &&
            r_acc != ACC_MT) {
            return SCPE_NXM;
        }
        return SCPE_OK;
    default:
        return SCPE_OK;
    }
}

/*
 * Update the M (modified) or R (referenced) bit the SD and cache
 */
static SIM_INLINE void mmu_update_sd(uint32 va, uint32 mask)
{
    uint32 sd0;
    uint8  ci;

    ci  = (SID(va) * NUM_SDCE) + SD_IDX(va);

    /* We go back to main memory to find the SD because the SD may
       have been loaded from cache, which is lossy. */
    sd0 = pread_w(SD_ADDR(va), BUS_PER);
    pwrite_w(SD_ADDR(va), sd0|mask, BUS_PER);

    /* There is no 'R' bit in the SD cache, only an 'M' bit. */
    if (mask == SD_M_MASK) {
        mmu_state.sdch[ci] |= mask;
    }
}

/*
 * Update the M (modified) or R (referenced) bit the PD and cache
 */
static SIM_INLINE void mmu_update_pd(uint32 va, uint32 pd_addr, uint32 mask)
{
    uint32 pd, tag, pdcll, pdclh, pdcrl, pdcrh;
    uint8  ci;

    tag = PD_TAG(va);
    ci  = (SID(va) * NUM_PDCE) + PD_IDX(va);

    /* We go back to main memory to find the PD because the PD may
       have been loaded from cache, which is lossy. */
    pd = pread_w(pd_addr, BUS_PER);
    pwrite_w(pd_addr, pd|mask, BUS_PER);

    /* Update in the cache */

    /* Left side */
    pdcll = mmu_state.pdcll[ci];
    pdclh = mmu_state.pdclh[ci];

    /* Right side */
    pdcrl = mmu_state.pdcrl[ci];
    pdcrh = mmu_state.pdcrh[ci];

    /* Search L and R to find a good entry with a matching tag, then
       update the appropriate bit */
    if ((pdclh & PD_GOOD_MASK) && PDCXL_TAG(pdcll) == tag) {
        mmu_state.pdclh[ci] |= mask;
    } else if ((pdcrh & PD_GOOD_MASK) && PDCXL_TAG(pdcrl) == tag) {
        mmu_state.pdcrh[ci] |= mask;
    }
}

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
                  "[pa=%08x] MMU_SDCL[%d] = %08x\n",
                  pa, offset, data);
        break;
    case MMU_SDCH:
        data = mmu_state.sdch[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_SDCH[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_PDCRL:
        data = mmu_state.pdcrl[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_PDCRL[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_PDCRH:
        data = mmu_state.pdcrh[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_PDCRH[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_PDCLL:
        data = mmu_state.pdcll[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_PDCLL[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_PDCLH:
        data = mmu_state.pdclh[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_PDCLH[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_SRAMA:
        data = mmu_state.sra[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_SRAMA[%d] = %08x\n",
                  offset, data);
        break;
    case MMU_SRAMB:
        data = mmu_state.srb[offset];
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_SRAMB[%d] = %08x\n",
                  offset, data);
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
                  "MMU_CONF = %08x\n",
                  data);
        break;
    case MMU_VAR:
        data = mmu_state.var;
        sim_debug(READ_MSG, &mmu_dev,
                  "MMU_VAR = %08x\n",
                  data);
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
                  "MMU_SRAMA[%d] = %08x\n",
                  offset, val);
        flush_cache_sec((uint8) offset);
        break;
    case MMU_SRAMB:
        offset = offset & 3;
        mmu_state.srb[offset] = val;
        mmu_state.sec[offset].len = (val >> 10) & 0x1fff;
        /* We do not flush the cache on writing SRAMB */
        sim_debug(WRITE_MSG, &mmu_dev,
                  "MMU_SRAMB[%d] = %08x (len=%06x)\n",
                  offset, val, mmu_state.sec[offset].len);
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
                  "SDT Length Fault. sramb_len=%x ssl=%x va=%08x\n",
                  SRAMB_LEN(va), SSL(va), va);
        return SCPE_NXM;
    }

    /* sd0 contains the segment descriptor, sd1 contains a pointer to
       the PDT or Segment */
    *sd0 = pread_w(SD_ADDR(va), BUS_PER);
    *sd1 = pread_w(SD_ADDR(va) + 4, BUS_PER);

    if (!SD_VALID(*sd0)) {
        sim_debug(EXECUTE_MSG, &mmu_dev,
                  "Invalid Segment Descriptor. va=%08x sd0=%08x\n",
                  va, *sd0);
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
                      "Segment Not Present. va=%08x",
                      va);
            MMU_FAULT(MMU_F_SEG_NOT_PRES);
            return SCPE_NXM;
        } else {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "PDT Not Present. va=%08x",
                      va);
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
                  "PDT Length Fault. "
                  "PDT Offset=%08x Max Offset=%08x va=%08x\n",
                  (PSL(va) * 4),
                  MAX_OFFSET(sd0), va);
        MMU_FAULT(MMU_F_PDTLEN);
        return SCPE_NXM;
    }

    *pd = pread_w(pd_addr, BUS_PER);

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
                      "Updating R bit in SD\n");
            mmu_update_sd(va, SD_R_MASK);
        }

        if (SHOULD_UPDATE_SD_M_BIT(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Updating M bit in SD\n");
            mmu_update_sd(va, SD_M_MASK);
        }

        /* Generate object trap if needed */
        if (SD_TRAP(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Object Trap. va=%08x",
                      va);
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
                  "Page Not Present. "
                  "pd=%08x r_acc=%x va=%08x\n",
                  pd, r_acc, va);
        MMU_FAULT(MMU_F_PAGE_NOT_PRES);
        return SCPE_NXM;
    }

    if (fc) {
        /* If this is a write or interlocked read access, and
           the 'W' bit is set, trigger a write fault */
        if ((r_acc == ACC_W || r_acc == ACC_IR) && PD_WFAULT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Page Write Fault. va=%08x\n",
                      va);
            MMU_FAULT(MMU_F_PW);
            return SCPE_NXM;
        }

        /* If this is a write, modify the M bit */
        if (SHOULD_UPDATE_PD_M_BIT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Updating M bit in PD\n");
            mmu_update_pd(va, PD_LOC(sd1, va), PD_M_MASK);
        }

        /* Modify the R bit and write it back */
        if (SHOULD_UPDATE_PD_R_BIT(pd)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Updating R bit in PD\n");
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
                      "Could not get PD (partial miss). r_acc=%d, fc=%d, va=%08x\n",
                      r_acc, fc, va);
            return SCPE_NXM;
        }
    } else if (sd_cached != SCPE_OK && pd_cached == SCPE_OK) {
        if (mmu_get_sd(va, r_acc, fc, &sd0, &sd1) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Could not get SD (partial miss). r_acc=%d, fc=%d, va=%08x\n",
                      r_acc, fc, va);
            return SCPE_NXM;
        }
    } else if (sd_cached != SCPE_OK && pd_cached != SCPE_OK) {
        if (mmu_get_sd(va, r_acc, fc, &sd0, &sd1) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Could not get SD (full miss). r_acc=%d, fc=%d, va=%08x\n",
                      r_acc, fc, va);
            return SCPE_NXM;
        }

        if (SD_PAGED(sd0) && mmu_get_pd(va, r_acc, fc, sd0, sd1, &pd, &pd_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "Could not get PD (full miss). r_acc=%d, fc=%d, va=%08x\n",
                      r_acc, fc, va);
            return SCPE_NXM;
        }
    }

    if (SD_PAGED(sd0)) {
        if (fc && mmu_check_perm(pd_acc, r_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "PAGED: NO ACCESS TO MEMORY AT %08x.\n"
                      "\t\tcpu_cm=%d r_acc=%x pd_acc=%02x\n"
                      "\t\tpd=%08x psw=%08x\n",
                      va, CPU_CM, r_acc, pd_acc,
                      pd, R[NUM_PSW]);
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }
        if (PD_LAST(pd) && (PSL_C(va) | POT(va)) >= MAX_OFFSET(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "PAGED: Segment Offset Fault.\n");
            MMU_FAULT(MMU_F_SEG_OFFSET);
            return SCPE_NXM;
        }
        return mmu_decode_paged(va, r_acc, fc, sd1, pd, pd_acc, pa);
    } else {
        if (fc && mmu_check_perm(SD_ACC(sd0), r_acc) != SCPE_OK) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "CONTIGUOUS: NO ACCESS TO MEMORY AT %08x.\n"
                      "\t\tsd0=%08x sd0_addr=%08x\n"
                      "\t\tcpu_cm=%d acc_req=%x sd_acc=%02x\n",
                      va, sd0, SD_ADDR(va),
                      CPU_CM, r_acc, SD_ACC(sd0));
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }
        if (SOT(va) >= MAX_OFFSET(sd0)) {
            sim_debug(EXECUTE_MSG, &mmu_dev,
                      "CONTIGUOUS: Segment Offset Fault. "
                      "sd0=%08x sd_addr=%08x SOT=%08x len=%08x va=%08x\n",
                      sd0, SD_ADDR(va), SOT(va),
                      MAX_OFFSET(sd0), va);
            MMU_FAULT(MMU_F_SEG_OFFSET);
            return SCPE_NXM;
        }
        return mmu_decode_contig(va, r_acc, sd0, sd1, fc, pa);
    }
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
              "Enabling MMU.\n");
    mmu_state.enabled = TRUE;
}

void mmu_disable()
{
    sim_debug(EXECUTE_MSG, &mmu_dev,
              "Disabling MMU.\n");
    mmu_state.enabled = FALSE;
}

CONST char *mmu_description(DEVICE *dptr)
{
    return "WE32101";
}
