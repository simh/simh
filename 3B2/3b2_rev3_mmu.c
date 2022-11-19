/* 3b2_rev3_mmu.c: WE32201 MMU

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
#include "3b2_mem.h"
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
    { BRDATA  (SDCH,   mmu_state.sdch,   16, 32, MMU_SDCS)      },
    { BRDATA  (PDCL,   mmu_state.pdcl,   16, 32, MMU_PDCS)      },
    { BRDATA  (PDCH,   mmu_state.pdch,   16, 32, MMU_PDCS)      },
    { BRDATA  (SRAMA,  mmu_state.sra,    16, 32, MMU_SRS)       },
    { BRDATA  (SRAMB,  mmu_state.srb,    16, 32, MMU_SRS)       },
    { NULL }
};

#define MMU_EXEC_DBG    1
#define MMU_TRACE_DBG   1 << 1
#define MMU_CACHE_DBG   1 << 2
#define MMU_FAULT_DBG   1 << 3
#define MMU_READ_DBG    1 << 4
#define MMU_WRITE_DBG   1 << 5

static DEBTAB mmu_debug[] = {
    { "EXEC",  MMU_EXEC_DBG,   "Simple execution" },
    { "CACHE", MMU_CACHE_DBG,  "Cache trace" },
    { "TRACE", MMU_TRACE_DBG,  "Translation trace" },
    { "FAULT", MMU_FAULT_DBG,  "Faults" },
    { "READ",  MMU_READ_DBG,   "Peripheral Read"},
    { "WRITE", MMU_WRITE_DBG,  "Peripheral Write"},
    { NULL }
};

MTAB mmu_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "SDT", NULL,
      NULL, &mmu_show_sdt, NULL, "Display SDT for section n [0-3]" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "SDC", NULL,
      NULL, &mmu_show_sdc, NULL, "Display SD Cache" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "PDC", NULL,
      NULL, &mmu_show_pdc, NULL, "Display PD Cache" },
    { 0 }
};

DEVICE mmu_dev = {
    "MMU",                          /* name */
    &mmu_unit,                      /* units */
    mmu_reg,                        /* registers */
    mmu_mod,                        /* modifiers */
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
    mmu_debug,                      /* debug flag names */
    NULL,                           /* memory size change */
    NULL,                           /* logical name */
    NULL,                           /* help routine */
    NULL,                           /* attach help routine */
    NULL,                           /* help context */
    &mmu_description                /* device description */
};

/*
 * Each bitmask corresponds to the pattern of bits used for the tag in
 * the first word of a segment descriptor in the cache. The outer
 * index corresponds to mode (0=Single-Context Mode, 1=Multi-Context
 * Mode), the inner index corresponds to page size (0=2kB, 1=4kB,
 * 2=8kB, 3=undefined)
 */
uint32 pdc_tag_masks[2][4] = {
    {0x43ffffe0, 0x43ffffc0, 0x43ffff80, 0},
    {0x7fffffe0, 0x7fffffc0, 0x7fffff80, 0},
};

/*
 * Bitmasks for generating page addresses for contiguous segments on
 * cache miss.
 */
uint32 pd_addr_masks[4] = {
    0xfffff800, 0xfffff000, 0xffffe000, 0
};

uint32 pd_psl_masks[4] = {
    0x1f800, 0x1f000, 0x1e000, 0
};

/* Masks used when searching the PD cache for a matching tag. */
#define PDC_TAG_MASK     (pdc_tag_masks[MMU_CONF_MCE][MMU_CONF_PS])

/* Mask off the bottom 10 bits of virtual address when generating PD
 * cache tags */
#define VA_TO_TAG_MASK   0xfffff800

/*
 * Macros used to generate PD cache tags from virtual-addresses
 */
#define PDC_MTAG(VA)    ((((VA) & VA_TO_TAG_MASK) >> 6)   | \
                         (mmu_state.cidnr[SID(VA)] << 26) | \
                         (1 << 30))

#define PDC_STAG(VA)    ((((VA) & VA_TO_TAG_MASK) >> 6) |   \
                         (1 << 30))

#define PDC_TAG(VA)     (MMU_CONF_MCE ? PDC_MTAG(VA) : PDC_STAG(VA))

/*
 * Retrieve a Segment Descriptor from the SD cache. The Segment
 * Descriptor Cache entry is returned in sd_lo and sd_hi, if found.
 *
 * If there is a cache hit, this function returns SCPE_OK.
 * If there is a cache miss, this function returns SCPE_NXM.
 */
static t_stat get_sdce(uint32 va, uint32 *sd_hi, uint32 *sd_lo)
{
    uint32 hi, lo, va_tag, sdc_tag;

    hi = mmu_state.sdch[SDC_IDX(va)];
    lo = mmu_state.sdcl[SDC_IDX(va)];
    va_tag = (va >> 20) & 0xfff;
    sdc_tag = lo & 0xfff;

    if ((hi & SDC_G_MASK) && (va_tag == sdc_tag)) {
        *sd_hi = SDCE_TO_SDH(hi);
        *sd_lo = SDCE_TO_SDL(hi,lo);
        return SCPE_OK;
    }

    return SCPE_NXM;
}

/*
 * Insert a Segment Descriptor into the SD cache.
 */
static void put_sdce(uint32 va, uint32 sd_hi, uint32 sd_lo)
{
    uint8 ci = SDC_IDX(va);

    mmu_state.sdch[ci] = SD_TO_SDCH(sd_hi, sd_lo);
    mmu_state.sdcl[ci] = SD_TO_SDCL(sd_lo, va);

    sim_debug(MMU_CACHE_DBG, &mmu_dev,
              "CACHED SD AT IDX %d. va=%08x sd_hi=%08x sd_lo=%08x sdc_hi=%08x sdc_lo=%08x\n",
              ci, va, sd_hi, sd_lo, mmu_state.sdch[ci], mmu_state.sdcl[ci]);

}

/*
 * Update the "Used" bit in the Page Descriptor cache for the given
 * entry.
 */
static void set_u_bit(uint32 index)
{
    uint32 i;

    mmu_state.pdch[index] |= PDC_U_MASK;

    /* Check to see if all U bits have been set. If so, the cache will
     * need to be flushed on the next put */
    for (i = 0; i < MMU_PDCS; i++) {
        if ((mmu_state.pdch[i] & PDC_U_MASK) == 0) {
            return;
        }
    }

    mmu_state.flush_u = TRUE;
}

/*
 * Retrieve a Page Descriptor Cache Entry from the Page Descriptor
 * Cache.
 */
static t_stat get_pdce(uint32 va, uint32 *pd, uint8 *pd_acc, uint32 *pdc_idx)
{
    uint32 i, key_tag, target_tag;

    *pdc_idx = 0;

    /* This is a fully associative cache, so we must scan for an entry
       with the correct tag. */
    key_tag = PDC_TAG(va) & PDC_TAG_MASK;

    for (i = 0; i < MMU_PDCS; i++) {
        target_tag = mmu_state.pdch[i] & PDC_TAG_MASK;
        if (target_tag == key_tag) {
            /* Construct the PD from the cached version */
            *pd = PDCE_TO_PD(mmu_state.pdcl[i]);
            *pd_acc = (mmu_state.pdcl[i] >> 24) & 0xff;
            *pdc_idx = i;
            sim_debug(MMU_TRACE_DBG, &mmu_dev,
                      "PDC HIT. va=%08x idx=%d tag=%03x pd=%08x pdcl=%08x pdch=%08x\n",
                      va, i, key_tag, *pd,
                      mmu_state.pdcl[i], mmu_state.pdch[i]);
            set_u_bit(i);
            return SCPE_OK;
        }
    }

    sim_debug(MMU_CACHE_DBG, &mmu_dev,
              "PDC MISS. va=%08x tag=%03x\n",
              va, key_tag);

    return SCPE_NXM;
}

/*
 * Cache a Page Descriptor in the specified slot.
 */
static void put_pdce_at(uint32 va, uint32 sd_lo, uint32 pd, uint32 slot)
{
    mmu_state.pdcl[slot] = PD_TO_PDCL(pd, sd_lo);
    mmu_state.pdch[slot] = VA_TO_PDCH(va, sd_lo);
    sim_debug(MMU_CACHE_DBG, &mmu_dev,
              "Caching MMU PDC entry at index %d (pdc_hi=%08x pdc_lo=%08x va=%08x)\n",
              slot, mmu_state.pdch[slot], mmu_state.pdcl[slot], va);
    set_u_bit(slot);
    mmu_state.last_cached = slot;
}

/*
 * Cache a Page Descriptor in the first available, least recently used
 * slot.
 */
static uint32 put_pdce(uint32 va, uint32 sd_lo, uint32 pd)
{
    uint32 i;

    /*
     * If all the U bits have been set, flush them all EXCEPT the most
     * recently cached entry.
     */
    if (mmu_state.flush_u) {
        sim_debug(MMU_CACHE_DBG, &mmu_dev,
                  "Flushing PDC U bits on all-set condition.\n");
        mmu_state.flush_u = FALSE;
        for (i = 0; i < MMU_PDCS; i++) {
            if (i != mmu_state.last_cached) {
                mmu_state.pdch[i] &= ~(PDC_U_MASK);
            }
        }
    }

    /* TODO: This can be done in one pass! Clean it up */

    /* Cache the Page Descriptor in the first slot with a cleared G
     * bit */
    for (i = 0; i < MMU_PDCS; i++) {
        if ((mmu_state.pdch[i] & PDC_G_MASK) == 0) {
            put_pdce_at(va, sd_lo, pd, i);
            return i;
        }
    }

    /* If ALL slots had their G bit set, find the first slot with a
       cleared U bit */
    for (i = 0; i < MMU_PDCS; i++) {
        if ((mmu_state.pdch[i] & PDC_U_MASK) == 0) {
            put_pdce_at(va, sd_lo, pd, i);
            return i;
        }
    }

    /* This should never happen, since if all U bits become set, they
     * are automatically all cleared */
    stop_reason = STOP_MMU;

    return 0;
}

/*
 * Flush the cache for an individual virtual address.
 */
static void flush_pdc(uint32 va)
{
    uint32 i, j, key_tag, target_tag;

    /* Flush the PDC. This is a fully associative cache, so we must
     * scan for an entry with the correct tag. */

    key_tag = PDC_TAG(va) & PDC_TAG_MASK;

    for (i = 0; i < MMU_PDCS; i++) {
        target_tag = mmu_state.pdch[i] & PDC_TAG_MASK;
        if (target_tag == key_tag) {
            sim_debug(MMU_CACHE_DBG, &mmu_dev,
                      "Flushing MMU PDC entry pdc_lo=%08x pdc_hi=%08x index %d (va=%08x)\n",
                      mmu_state.pdcl[i],
                      mmu_state.pdch[i],
                      i,
                      va);
            if (mmu_state.pdch[i] & PDC_C_MASK) {
                sim_debug(MMU_CACHE_DBG, &mmu_dev,
                          "Flushing MMU PDC entry: CONTIGUOUS\n");
                /* If this PD came from a contiguous SD, we need to
                 * flush ALL entries belonging to the same SD. All
                 * pages within the same segment have the same upper
                 * 11 bits. */
                for (j = 0; j < MMU_PDCS; j++) {
                    if ((mmu_state.pdch[j] & 0x3ffc000) ==
                        (mmu_state.pdch[i] & 0x3ffc000)) {
                        mmu_state.pdch[j] &= ~(PDC_G_MASK|PDC_U_MASK);
                    }
                }
            } else {
                /* Otherwise, just flush the one entry */
                mmu_state.pdch[i] &= ~(PDC_G_MASK|PDC_U_MASK);
            }
            return;
        }
    }

    sim_debug(MMU_CACHE_DBG, &mmu_dev,
              "Flushing MMU PDC entry: NOT FOUND (va=%08x key_tag=%08x)\n",
              va, key_tag);

}

/*
 * Flush all entries in both SDC and PDC.
 */
static void flush_caches()
{
    uint32 i;

    sim_debug(MMU_CACHE_DBG, &mmu_dev,
              "Flushing MMU PDC and SDC\n");

    for (i = 0; i < MMU_SDCS; i++) {
        mmu_state.sdch[i] &= ~SDC_G_MASK;
    }

    for (i = 0; i < MMU_PDCS; i++) {
        mmu_state.pdch[i] &= ~PDC_G_MASK;
        mmu_state.pdch[i] &= ~PDC_U_MASK;
    }
}

/*
 * Check permissions for a set of permission flags and an access type.
 *
 * Return SCPE_OK if permission is granted, SCPE_NXM if permission is
 * not allowed.
 */
static t_stat mmu_check_perm(uint8 flags, uint8 r_acc)
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
 * Initialize the MMU device
 */
t_stat mmu_init(DEVICE *dptr)
{
    flush_caches();
    return SCPE_OK;
}

/*
 * Memory-mapped (peripheral mode) read of the MMU device
 */
uint32 mmu_read(uint32 pa, size_t size)
{
    uint8 entity, index;
    uint32 data = 0;

    /* Register entity */
    entity = ((uint8)(pa >> 8)) & 0xf;

    /* Index into entity */
    index = (uint8)((pa >> 2) & 0x1f);

    switch (entity) {
    case MMU_SDCL:
        data = mmu_state.sdcl[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_SDCL[%d] = %08x\n",
                  index, data);
        break;
    case MMU_SDCH:
        data = mmu_state.sdch[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_SDCH[%d] = %08x\n",
                  index, data);
        break;
    case MMU_PDCL:
        data = mmu_state.pdcl[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_PDCL[%d] = %08x\n",
                  index, data);
        break;
    case MMU_PDCH:
        data = mmu_state.pdch[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_PDCH[%d] = %08x\n",
                  index, data);
        break;
    case MMU_SRAMA:
        data = mmu_state.sra[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_SRAMA[%d] = %08x\n",
                  index, data);
        break;
    case MMU_SRAMB:
        data = mmu_state.srb[index];
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_SRAMB[%d] = %08x\n",
                  index, data);
        break;
    case MMU_FC:
        data = mmu_state.fcode;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_FC = %08x\n",
                  data);
        break;
    case MMU_FA:
        data = mmu_state.faddr;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_FA = %08x\n",
                  data);
        break;
    case MMU_CONF:
        data = mmu_state.conf;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_CONF = %02x (M=%d R=%d $=%d PS=%d MCE=%d DCE=%d)\n",
                  data,
                  MMU_CONF_M,
                  MMU_CONF_R,
                  MMU_CONF_C,
                  MMU_CONF_PS,
                  MMU_CONF_MCE,
                  MMU_CONF_DCE);
        break;
    case MMU_VAR:
        data = mmu_state.var;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_VAR = %08x\n",
                  data);
        break;
    case MMU_IDC:
        /* TODO: Implement */
        data = 0;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_IDC\n");
        break;
    case MMU_IDNR:
        /* TODO: Implement */
        data = 0;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_IDNR\n");
        break;
    case MMU_FIDNR:
        /* TODO: Implement */
        data = 0;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_FIDNR\n");
        break;
    case MMU_VR:
        data = MMU_REV3_VER;
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "MMU_VR = 0x23\n");
        break;
    default:
        sim_debug(MMU_READ_DBG, &mmu_dev,
                  "Invalid MMU register: pa=%08x\n",
                  pa);
        CSRBIT(CSRTIMO, TRUE);
        break;
    }

    return data;
}

void mmu_write(uint32 pa, uint32 val, size_t size)
{
    uint32 index, entity, i;

    /* Register entity */
    entity = ((uint8)(pa >> 8)) & 0xf;

    /* Index into entity */
    index = (uint8)((pa >> 2) & 0x1f);

    switch (entity) {
    case MMU_SDCL:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_SDCL[%d] = %08x\n",
                  index, val);
        mmu_state.sdcl[index] = val;
        break;
    case MMU_SDCH:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_SDCH[%d] = %08x\n",
                  index, val);
        mmu_state.sdch[index] = val;
        break;
    case MMU_PDCL:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_PDCL[%d] = %08x\n",
                  index, val);
        mmu_state.pdcl[index] = val;
        break;
    case MMU_PDCH:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_PDCH[%d] = %08x\n",
                  index, val);
        mmu_state.pdch[index] = val;
        break;
    case MMU_FDCR:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_FDCR\n");
        /* Data cache is not implemented */
        break;
    case MMU_SRAMA:
        index = index & 3;
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_SRAMA[%d] = %08x\n",
                  index, val);
        mmu_state.sra[index] = val;
        mmu_state.sec[index].addr = val & 0xfffffffc;

        /* Flush all SDC cache entries for this section */
        for (i = 0; i < MMU_SDCS; i++) {
            if (((mmu_state.sdcl[i] >> 10) & 0x3) == index) {
                sim_debug(MMU_CACHE_DBG, &mmu_dev,
                          "Flushing MMU SDC entry at index %d "
                          "(sdc_lo=%08x sdc_hi=%08x)\n",
                          i, mmu_state.sdcl[i], mmu_state.sdch[i]);
                mmu_state.sdch[i] &= ~(SDC_G_MASK);
            }
        }

        /* Flush all PDC cache entries for this section */
        for (i = 0; i < MMU_PDCS; i++) {
            if (((mmu_state.pdch[i] >> 24) & 0x3) == index) {
                mmu_state.pdch[i] &= ~(PDC_G_MASK);
            }
        }
        break;
    case MMU_SRAMB:
        index = index & 3;
        mmu_state.srb[index] = val;
        mmu_state.sec[index].len = (val >> 10) & 0x1fff;
        /* We do not flush the cache on writing SRAMB */
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_SRAMB[%d] length=%04x (%d segments)\n",
                  index,
                  mmu_state.sec[index].len,
                  mmu_state.sec[index].len + 1);
        break;
    case MMU_FC:
        /* Set a default value */
        mmu_state.fcode = (((CPU_CM) << 5) | (0xa << 7));
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_FC = %08x\n",
                  mmu_state.fcode);
        break;
    case MMU_FA:
        mmu_state.faddr = val;
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_FADDR = %08x\n",
                  val);
        break;
    case MMU_CONF:
        mmu_state.conf = val & 0x7f;
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_CONF = %02x (M=%d R=%d $=%d PS=%d MCE=%d DCE=%d)\n",
                  val,
                  MMU_CONF_M,
                  MMU_CONF_R,
                  MMU_CONF_C,
                  MMU_CONF_PS,
                  MMU_CONF_MCE,
                  MMU_CONF_DCE);
        break;
    case MMU_VAR:
        mmu_state.var = val;
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_VAR = %08x\n", val);
        if ((mmu_state.sdcl[SDC_IDX(val)] & SDC_VADDR_MASK) ==
            ((val >> 20) & SDC_VADDR_MASK)) {
            sim_debug(MMU_CACHE_DBG, &mmu_dev,
                      "Flushing MMU SDC entry at index %d "
                      "(sdc_lo=%08x sdc_hi=%08x)\n",
                      SDC_IDX(val),
                      mmu_state.sdcl[SDC_IDX(val)],
                      mmu_state.sdch[SDC_IDX(val)]);
            mmu_state.sdch[SDC_IDX(val)] &= ~SDC_G_MASK;
        }
        flush_pdc(val);
        break;
    case MMU_IDC:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_IDC = %08x\n",
                  val);
        break;
    case MMU_IDNR:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_IDNR = %08x\n",
                  val);
        break;
    case MMU_FIDNR:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_FIDNR = %08x\n",
                  val);
        break;
    case MMU_VR:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "MMU_VR = %08x\n",
                  val);
        break;
    default:
        sim_debug(MMU_WRITE_DBG, &mmu_dev,
                  "UNHANDLED WRITE (entity=0x%x, index=0x%x, val=%08x)\n",
                  entity, index, val);
        break;
    }
}

/*
 * Update history bits in cache and memory.
 */
static t_stat mmu_update_history(uint32 va, uint8 r_acc, uint32 pdc_idx, t_bool fc)
{
    uint32 sd_hi, sd_lo, pd;
    uint32 pd_addr;
    t_bool update_sdc = TRUE;

    if (get_sdce(va, &sd_hi, &sd_lo) != SCPE_OK) {
        update_sdc = FALSE;
    }

    sd_lo = pread_w(SD_ADDR(va), BUS_PER);
    sd_hi = pread_w(SD_ADDR(va) + 4, BUS_PER);

    if (MMU_CONF_M && r_acc == ACC_W && (mmu_state.sdcl[SDC_IDX(va)] & SDC_M_MASK) == 0) {
        if (update_sdc) {
            mmu_state.sdcl[SDC_IDX(va)] |= SDC_M_MASK;
        }

        if (mmu_check_perm(SD_ACC(sd_lo), r_acc) != SCPE_OK) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "MMU R&M Update Fault (M)\n");
            MMU_FAULT(MMU_F_RM_UPD);
            return SCPE_NXM;
        }

        pwrite_w(SD_ADDR(va), sd_lo | SD_M_MASK, BUS_PER);
    }

    if (MMU_CONF_R && (mmu_state.sdcl[SDC_IDX(va)] & SDC_R_MASK) == 0) {
        if (update_sdc) {
            mmu_state.sdcl[SDC_IDX(va)] |= SDC_R_MASK;
        }

        if (mmu_check_perm(SD_ACC(sd_lo), r_acc) != SCPE_OK) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "MMU R&M Update Fault (R)\n");
            MMU_FAULT(MMU_F_RM_UPD);
            return SCPE_NXM;
        }

        pwrite_w(SD_ADDR(va), sd_lo | SD_R_MASK, BUS_PER);
    }

    if (!SD_CONTIG(sd_lo)) {
        pd_addr = SD_SEG_ADDR(sd_hi) + (PSL(va) * 4);

        if (r_acc == ACC_W && (mmu_state.pdcl[pdc_idx] & PDC_M_MASK) == 0) {
            mmu_state.pdcl[pdc_idx] |= PDC_M_MASK;
            pd = pread_w(pd_addr, BUS_PER);
            pwrite_w(pd_addr, pd | PD_M_MASK, BUS_PER);
        }

        if ((mmu_state.pdcl[pdc_idx] & PDC_R_MASK) == 0) {
            mmu_state.pdcl[pdc_idx] |= PDC_R_MASK;
            pd = pread_w(pd_addr, BUS_PER);
            pwrite_w(pd_addr, pd | PD_R_MASK, BUS_PER);
        }
    }

    return SCPE_OK;
}

/*
 * Handle a Page Descriptor cache miss.
 *
 * - va is the virtual address for the PD.
 * - r_acc is the requested access type.
 * - fc is the fault check flag.
 *
 * If there was a miss when reading the SDC, TRUE will be returned in
 * "sd_miss". The page descriptor will be returned in "pd", and the
 * segment descriptor will be returned in "sd_lo" and "sd_hi".
 *
 * Returns SCPE_OK on success, SCPE_NXM on failure. If SCPE_NXM is
 * returned, a failure code and fault address will be set in the
 * appropriate registers.
 *
 * As always, the flag 'fc' may be set to FALSE to avoid certain
 * types of fault checking.
 *
 * For detailed documentation, See:
 *
 * "WE 32201 Memory Management Unit Information Manual", AT&T Select
 * Code 307-706, February 1987; Figure 2-18, pages 2-24 through 2-25.
 */
t_stat mmu_pdc_miss(uint32 va, uint8 r_acc, t_bool fc,
                    uint32 *pd, uint32 *pdc_idx)
{
    uint32 sd_ptr, sd_hi, sd_lo, pd_addr;
    uint32 indirect_count = 0;
    t_bool sdc_miss = FALSE;

    *pdc_idx = 0;

    /* If this was an instruction fetch, the actual requested level
     * here will become "Instruction Fetch After Discontinuity"
     * due to the page miss. */
    r_acc = (r_acc == ACC_IF ? ACC_IFAD : r_acc);

    /* We immediately do SSL bounds checking. The 'fc' flag is not
     * checked because SSL out of bounds is a fatal error. */
    if (SSL(va) > SRAMB_LEN(va)) {
        sim_debug(MMU_FAULT_DBG, &mmu_dev,
                  "SDT Length Fault. sramb_len=%x ssl=%x va=%08x\n",
                  SRAMB_LEN(va), SSL(va), va);
        MMU_FAULT(MMU_F_SDTLEN);
        return SCPE_NXM;
    }

    /* This loop handles segment descriptor indirection (if any) */
    sd_ptr = SD_ADDR(va);
    while (1) {
        /* Try to find the SD in the cache */
        if (get_sdce(va, &sd_hi, &sd_lo) != SCPE_OK) {
            /* This was a miss, so we need to load the SD out of
             * memory. */
            sdc_miss = TRUE;

            sd_lo = pread_w(sd_ptr, BUS_PER);      /* Control Bits */
            sd_hi = pread_w(sd_ptr + 4, BUS_PER);  /* Address Bits */
            sim_debug(MMU_CACHE_DBG, &mmu_dev,
                      "SDC miss. Read sd_ptr=%08x sd_lo=%08x sd_hi=%08x va=%08x\n",
                      sd_ptr, sd_lo, sd_hi, va);
        }

        if (!SD_VALID(sd_lo)) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "Invalid Segment Descriptor. va=%08x sd_hi=%08x sd_lo=%08x\n",
                      va, sd_hi, sd_lo);
            MMU_FAULT(MMU_F_INV_SD);
            return SCPE_NXM;
        }

        if (SD_INDIRECT(sd_lo)) {
            if (++indirect_count > MAX_INDIRECTS) {
                sim_debug(MMU_FAULT_DBG, &mmu_dev,
                          "Max Indirects Fault. va=%08x sd_hi=%08x sd_lo=%08x\n",
                          va, sd_hi, sd_lo);
                MMU_FAULT(MMU_F_INDIRECT);
                return SCPE_NXM;
            }

            /* Any permission failure at this point is actually an MMU_F_MISS_MEM */
            if (mmu_check_perm(SD_ACC(sd_lo), r_acc) != SCPE_OK) {
                sim_debug(MMU_FAULT_DBG, &mmu_dev,
                          "MMU Miss Processing Memory Fault (SD Access) (ckm=%d pd_acc=%02x r_acc=%02x)\n",
                          CPU_CM, SD_ACC(sd_lo), r_acc);
                MMU_FAULT(MMU_F_MISS_MEM);
                return SCPE_NXM;
            }

            /* sd_hi is a pointer to a new segment descriptor */
            sd_ptr = sd_hi;

            sd_lo = pread_w(sd_ptr, BUS_PER);
            sd_hi = pread_w(sd_ptr + 4, BUS_PER);
        } else {
            /* If it's not an indirection, we're done. */
            break;
        }
    }

    /* Fault if the segment descriptor P bit isn't set */
    if (!SD_PRESENT(sd_lo)) {
        /* If the C bit is set, this is a SEGMENT NOT PRESENT
           fault; otherwise, it's a PDT NOT PRESENT fault. */
        if (SD_CONTIG(sd_lo)) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "Segment Not Present. va=%08x\n",
                      va);
            MMU_FAULT(MMU_F_SEG_NOT_PRES);
            return SCPE_NXM;
        } else {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "PDT Not Present. va=%08x\n",
                      va);
            MMU_FAULT(MMU_F_PDT_NOT_PRES);
            return SCPE_NXM;
        }
    }

    /* Check to see if the segment is too long. */
    if (SD_CONTIG(sd_lo)) {
        if (PSL(va) > SD_MAX_OFF(sd_lo)) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "Segment Offset Fault. va=%08x\n",
                      va);
            MMU_FAULT(MMU_F_SEG_OFFSET);
            return SCPE_NXM;
        }
    } else {
        if ((va & 0x1ffff) > MAX_SEG_OFF(sd_lo)) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "PDT Length Fault. va=%08x max_seg_off=0x%x\n",
                      va, MAX_SEG_OFF(sd_lo));
            MMU_FAULT(MMU_F_PDTLEN);
            return SCPE_NXM;
        }
    }

    /* Either load or construct the PD */
    if (SD_CONTIG(sd_lo)) {
        /* TODO: VERIFY */
        if (mmu_check_perm(SD_ACC(sd_lo), r_acc) != SCPE_OK) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "[AFTER DISCONTINUITY] Access to Memory Denied (va=%08x ckm=%d pd_acc=%02x r_acc=%02x)\n",
                       va, CPU_CM, SD_ACC(sd_lo), r_acc);
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }

        /* We have to construct a PD for this SD. */
        *pd = (((sd_hi & pd_addr_masks[MMU_CONF_PS]) + PSL_C(va)) |
               ((sd_lo & 0x800000) >> 18) |  /* Copy R bit */
               ((sd_lo & 0x400000) >> 21) |  /* Copy M bit */
               1);                           /* P bit */

        sim_debug(MMU_CACHE_DBG, &mmu_dev,
                  "Contiguous Segment. Constructing PD. PSIZE=%d va=%08x sd_hi=%08x sd_lo=%08x pd=%08x\n",
                  MMU_CONF_PS, va, sd_hi, sd_lo, *pd);
    } else {
        /* We can find the PD in main memory */
        pd_addr = SD_SEG_ADDR(sd_hi) + (PSL(va) * 4);

        *pd = pread_w(pd_addr, BUS_PER);

        sim_debug(MMU_CACHE_DBG, &mmu_dev,
                  "Paged Segment. Loaded PD. va=%08x sd_hi=%08x sd_lo=%08x pd_addr=%08x pd=%08x\n",
                  va, sd_hi, sd_lo, pd_addr, *pd);
    }

    if (r_acc == ACC_W && (*pd & PD_W_MASK)) {
        sim_debug(MMU_FAULT_DBG, &mmu_dev,
                  "Page Write Fault, pd=%08x va=%08x\n",
                  *pd, va);
        MMU_FAULT(MMU_F_PW);
        return SCPE_NXM;
    }

    if ((*pd & PD_P_MASK) != PD_P_MASK) {
        sim_debug(MMU_FAULT_DBG, &mmu_dev,
                  "Page Not Present Fault. pd=%08x va=%08x\n",
                  *pd, va);
        MMU_FAULT(MMU_F_PAGE_NOT_PRES);
        return SCPE_NXM;
    }

    /* Finally, cache the PD */

    if (sdc_miss) {
        put_sdce(va, sd_hi, sd_lo);
    }

    *pdc_idx = put_pdce(va, sd_lo, *pd);

    return SCPE_OK;
}

/*
 * Translate a virtual address into a physical address.
 *
 * Note that unlike 'mmu_xlate_addr', this function will _not_ abort
 * on failure. The decoded physical address is returned in "pa". If
 * the argument "fc" is FALSE, this function will bypass:
 *
 *   - Access flag checks,
 *   - Cache insertion,
 *   - Setting MMU fault registers,
 *   - Modifying segment and page descriptor bits.
 *
 * In other words, setting "fc" to FALSE does the minimum work
 * necessary to translate a virtual address without changing any MMU
 * state. The primary use case for this flag is to provide simulator
 * debugging access to memory translation while avoiding that access
 * undermining the currently running operating system (if any).
 *
 * This function returns SCPE_OK if translation succeeded, and
 * SCPE_NXM if translation failed.
 *
 */
t_stat mmu_decode_va(uint32 va, uint8 r_acc, t_bool fc, uint32 *pa)
{
    uint32 pd, pdc_idx;
    uint8 pd_acc;
    t_stat succ;

    /*
     * If the MMU is disabled, virtual == physical.
     */
    if (!mmu_state.enabled) {
        *pa = va;
        return SCPE_OK;
    }

    /*
     * 1. Check PDC for an entry.
     */
    if (get_pdce(va, &pd, &pd_acc, &pdc_idx) == SCPE_OK) {
        if (mmu_check_perm(pd_acc, r_acc) != SCPE_OK) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "Access to Memory Denied (va=%08x ckm=%d pd_acc=%02x r_acc=%02x)\n",
                      va, CPU_CM, pd_acc, r_acc);
            MMU_FAULT(MMU_F_ACC);
            return SCPE_NXM;
        }

        if (r_acc == ACC_W && (pd & PD_W_MASK)) {
            sim_debug(MMU_FAULT_DBG, &mmu_dev,
                      "Page Write Fault, pd=%08x va=%08x\n",
                      pd, va);
            MMU_FAULT(MMU_F_PW);
            return SCPE_NXM;
        }
    } else {
        /* Do miss processing. This will cache the PD if necessary. */
        succ = mmu_pdc_miss(va, r_acc, fc, &pd, &pdc_idx);
        if (succ != SCPE_OK) {
            return succ;
        }
    }

    /*
     * 2. Update history bits
     */
    succ = mmu_update_history(va, r_acc, pdc_idx, fc);
    if (succ != SCPE_OK) {
        return succ;
    }

    /*
     * 3. Translation from Page Descriptor
     */
    *pa = PD_ADDR(pd) + POT(va);

    sim_debug(MMU_TRACE_DBG, &mmu_dev,
              "XLATE DONE.  r_acc=%d  va=%08x  pa=%08x\n",
              r_acc, va, *pa);

    return SCPE_OK;
}

/*
 * Translate a virtual address into a physical address.
 *
 * This function returns the translated virtual address, and aborts
 * without returning if translation failed.
 */
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

/*
 * Enable the MMU and allow virtual address translation.
 */
void mmu_enable()
{
    mmu_state.enabled = TRUE;
}

/*
 * Disable the MMU. All memory access will be through physical addresses only.
 */
void mmu_disable()
{
    mmu_state.enabled = FALSE;
}

CONST char *mmu_description(DEVICE *dptr)
{
    return "WE32201 MMU";
}

/*
 * Display the segment descriptor cache
 */
t_stat mmu_show_sdc(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 sd_lo, sd_hi, base, pages, i;

    fprintf(st, "\nSegment Descriptor Cache\n\n");

    fprintf(st, "start     sdc (lo) sdc (hi)   sd (lo)  sd (hi)   C/P  seg start   pages\n");
    fprintf(st, "--------  -------- --------   -------- --------  ---  ---------   -----\n");

    for (i = 0; i < MMU_SDCS; i++) {
        sd_lo = SDCE_TO_SDL(mmu_state.sdch[i], mmu_state.sdcl[i]);
        sd_hi = SDCE_TO_SDH(mmu_state.sdch[i]);
        base = ((mmu_state.sdcl[i] & 0xfff) << 20) | ((i & 7) << 17);
        pages = ((sd_lo & SD_MAX_OFF_MASK) >> 18) + 1;

        fprintf(st, "%08x  %08x %08x   %08x %08x   %s   %08x    %d\n",
                base,
                mmu_state.sdcl[i],
                mmu_state.sdch[i],
                sd_lo, sd_hi,
                SD_CONTIG(sd_lo) ? "C" : "P",
                sd_hi & SD_ADDR_MASK,
                pages);

    }

    return SCPE_OK;
}

t_stat mmu_show_pdc(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 i, pdc_hi, pdc_lo;

    fprintf(st, "\nPage Descriptor Cache\n\n");
    fprintf(st, "IDX  pdc (hi) pdc (lo)    U G C W   vaddr      pd addr\n");
    fprintf(st, "---- -------- --------    - - - -   --------   --------\n");

    for (i = 0; i < MMU_PDCS; i++) {
        pdc_hi = mmu_state.pdch[i];
        pdc_lo = mmu_state.pdcl[i];

        fprintf(st, "%02d   %08x %08x    %s %s %s %s   %08x   %08x\n",
                i,
                pdc_hi, pdc_lo,
                pdc_hi & PDC_U_MASK ? "U" : " ",
                pdc_hi & PDC_G_MASK ? "G" : " ",
                pdc_hi & PDC_C_MASK ? "C" : "P",
                pdc_lo & PDC_W_MASK ? "W" : " ",
                (pdc_hi & PDC_TAG_MASK) << 6,
                PDCE_TO_PD(pdc_lo));
    }

    return SCPE_OK;
}

/*
 * Display the segment table for a section.
 */
t_stat mmu_show_sdt(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    uint32 addr, len, sd_lo, sd_hi, base, pages, i;
    uint8 sec;
    char *cptr = (char *) desc;
    t_stat result;

    if (cptr == NULL) {
        fprintf(st, "Missing section number\n");
        return SCPE_ARG;
    }

    sec = (uint8) get_uint(cptr, 10, 3, &result);
    if (result != SCPE_OK) {
        fprintf(st, "Please specify a section from 0-3\n");
        return SCPE_ARG;
    }

    addr = mmu_state.sec[sec].addr;
    len = mmu_state.sec[sec].len + 1;

    fprintf(st, "\nSection %d SDT\n\n", sec);
    fprintf(st, "start    end       sd (lo)  sd (hi)  C/P seg start   pages\n");
    fprintf(st, "-------- --------  -------- -------- --- ---------   ------\n");

    for (i = 0; i < len; i++) {
        sd_lo = pread_w(addr + (i * 8), BUS_PER) & SD_RES_MASK;
        sd_hi = pread_w(addr + (i * 8) + 4, BUS_PER);
        base = (sec << 14 | i << 1) << 16;
        pages = ((sd_lo & SD_MAX_OFF_MASK) >> 18) + 1;

        if (SD_VALID(sd_lo)) {
            fprintf(st, "%08x-%08x  %08x %08x  %s  %08x    %d\n",
                    base,
                    base + (((sd_lo & SD_MAX_OFF_MASK) >> 15) * 2048) - 1,
                    sd_lo, sd_hi,
                    SD_CONTIG(sd_lo) ? "C" : "P",
                    sd_hi & SD_ADDR_MASK,
                    pages);
        }
    }

    return SCPE_OK;
}
