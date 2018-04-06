/* 3b2_mmu.c: AT&T 3B2 Model 400 MMU (WE32101) Header

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

#ifndef _3B2_MMU_H
#define _3B2_MMU_H

#include "3b2_defs.h"


/************************************************************************
 *
 * Vocabulary
 * ----------
 *
 *    PD:  Page Descriptor (in main memory)
 *    PDT: Page Descriptor Table (in main memory)
 *    POT: Page Offset. Bits 0-10 of a Paged virtual address.
 *    PSL: Page Select. Bits 11-16 of a Paged virtual address.
 *    SD:  Segment Descriptor (in main memory)
 *    SDT: Segment Descriptor Table (in main memory)
 *    SID: Section ID. Bits 30-31 of all virtual addresses
 *    SOT: Segment Offset. Bits 0-16 of a Contiguous virtual address.
 *    SSL: Segment Select. Bits 17-29 of all virtual addresses.
 *
 *
 * The WE32101 MMU divides the virtual address space into four
 * Sections with 8K Segments per section. Virtual address bits 30 and
 * 31 determine the section, bits 17-29 determine the Segment within
 * the section.
 *
 * There are two kinds of address translation: Contiguous Translation
 * and Paged Translation. Contiguous Translation just uses an offset
 * (bits 0-16 of the virtual address) into each Segment to find an
 * address, allowing for 128K bytes per Segment. Paged translation
 * further break Segments down into 64 Pages of 2K each.
 *
 * Details about how to do translation are held in main memory in
 * Segment Descriptors and Page Descriptors. These are located in
 * Segment Descriptor Tables and Page Descriptor Tables set up by the
 * computer before enabling the MMU.
 *
 * In addition to details in main memory, the MMU has a small cache
 * of both Segment Descriptors and Page Descriptors. This is NOT just
 * used for performance reasons! Various features of the cache,
 * such as updating R and M bits in Segment and Page Descriptors,
 * are used by various operating system features.
 *
 *
 * Virtual Address Fields
 * ----------------------
 *
 *          31 30 29               17 16                          0
 *         +-----+-------------------+-----------------------------+
 * Contig: | SID |         SSL       |            SOT              |
 *         +-----+-------------------+-----------------------------+
 *
 *          31 30 29               17 16     11 10                0
 *         +-----+-------------------+---------+-------------------+
 *  Paged: | SID |         SSL       |   PSL   |        POT        |
 *         +-----+-------------------+---------+-------------------+
 *
 *
 * Segment Descriptor Fields
 * -------------------------
 *
 *          31   24 23     10 9   8  7   6   5   4   3   2   1   0
 *         +-------+---------+-----+---+---+---+---+---+---+---+---+
 *    sd0: |  Acc  | Max Off | Res | I | V | R | T | $ | C | M | P |
 *         +-------+---------+-----+---+---+---+---+---+---+---+---+
 *
 *         +-----------------------------------------------+-------+
 *    sd1: |   Address  (high-order 27 or 29 bits)         | Soft  |
 *         +-----------------------------------------------+-------+
 *
 *
 * Segment Descriptor Cache Entry
 * ------------------------------
 *
 *          31   24 23                     10  9                  0
 *         +-------+-------------------------+---------------------+
 *    Low: |  Acc  |         Max Off         |         Tag         |
 *         +-------+-------------------------+---------------------+
 *
 *          31                               5   4   3   2   1   0
 *         +-----------------------------------+---+---+---+---+---+
 *   High: |             Address               | T | $ | C | M | G |
 *         +-----------------------------------+---+---+---+---+---+
 *
 *
 * Page Descriptor Fields
 * ----------------------
 *
 *          31            11 10   8 7   6  5   4    3    2   1   0
 *         +----------------+------+-----+---+---+-----+---+---+---+
 *         |  Page Address  | Soft | Res | R | W | Res | L | M | P |
 *         +----------------+------+-----+---+---+-----+---+---+---+
 *
 *
 * Page Descriptor Cache Entry
 * ---------------------------
 *
 *          31 24 23              16 15                           0
 *         +-----+------------------+------------------------------+
 *    Low: | Acc |        Res       |             Tag              |
 *         +-----+------------------+------------------------------+
 *
 *          31                 11 10  7  6   5   4   3   2   1   0
 *         +---------------------+-----+---+---+---+---+---+---+---+
 *   High: |       Address       | Res | U | R | W | $ | L | M | G |
 *         +---------------------+-----+---+---+---+---+---+---+---+
 *
 *  "U" is only set in the left cache entry, and indicates
 *  which slot (left or right) was most recently updated.
 *
 ***********************************************************************/

#define MMUBASE 0x40000
#define MMUSIZE 0x1000

#define MMU_SRS  0x04       /* Section RAM array size (words) */
#define MMU_SDCS 0x20       /* Segment Descriptor Cache H/L array size
                               (words) */
#define MMU_PDCS 0x20       /* Page Descriptor Cache H/L array size
                               (words) */

/* Register address offsets */
#define MMU_SDCL   0
#define MMU_SDCH   1
#define MMU_PDCRL  2
#define MMU_PDCRH  3
#define MMU_PDCLL  4
#define MMU_PDCLH  5
#define MMU_SRAMA  6
#define MMU_SRAMB  7
#define MMU_FC     8
#define MMU_FA     9
#define MMU_CONF   10
#define MMU_VAR    11

#define MMU_CONF_M  (mmu_state.conf & 0x1)
#define MMU_CONF_R  (mmu_state.conf & 0x2)

/* Caching */
#define NUM_SEC    4u    /* Number of memory sections */
#define NUM_SDCE   8     /* SD cache entries per section */
#define NUM_PDCE   8     /* PD cache entries per section per side (l/r) */
#define SET_SIZE   2     /* PDs are held in a 2-way associative set */

/* Cache Tag for SDs */
#define SD_TAG(vaddr)     ((vaddr >> 20) & 0x3ff)

/* Cache Tag for PDs */
#define PD_TAG(vaddr)     (((vaddr >> 13) & 0xf) | ((vaddr >> 14) & 0xfff0))

/* Index of entry in the SD cache */
#define SD_IDX(vaddr)     ((vaddr >> 17) & 7)

/* Index of entry in the PD cache */
#define PD_IDX(vaddr)     (((vaddr >> 11) & 3) | ((vaddr >> 15) & 4))

/* Shift and mask the flag bits for the current CPU mode */
#define MMU_PERM(f)  ((f >> ((3 - CPU_CM) * 2)) & 3)

#define ROM_SIZE       0x10000
#define BOOT_CODE_SIZE 0x8000

/* Codes set in the MMU Fault register */
#define MMU_F_SDTLEN             0x03
#define MMU_F_PW                 0x04
#define MMU_F_PDTLEN             0x05
#define MMU_F_INV_SD             0x06
#define MMU_F_SEG_NOT_PRES       0x07
#define MMU_F_OTRAP              0x08
#define MMU_F_PDT_NOT_PRES       0x09
#define MMU_F_PAGE_NOT_PRES      0x0a
#define MMU_F_ACC                0x0d
#define MMU_F_SEG_OFFSET         0x0e

/* Pluck out Virtual Address fields */
#define SID(va)           (((va) >> 30) & 3)
#define SSL(va)           (((va) >> 17) & 0x1fff)
#define SOT(va)           (va & 0x1ffff)
#define PSL(va)           (((va) >> 11) & 0x3f)
#define PSL_C(va)         ((va) & 0x1f800)
#define POT(va)           (va & 0x7ff)

/* Get the maximum length of an SSL from SRAMB */
#define SRAMB_LEN(va)     (mmu_state.sec[SID(va)].len + 1)

/* Pluck out Segment Descriptor fields */
#define SD_PRESENT(sd0)   ((sd0) & 1)
#define SD_MODIFIED(sd0)  (((sd0) >> 1) & 1)
#define SD_CONTIG(sd0)    (((sd0) >> 2) & 1)
#define SD_PAGED(sd0)     ((((sd0) >> 2) & 1) == 0)
#define SD_CACHE(sd0)     (((sd0) >> 3) & 1)
#define SD_TRAP(sd0)      (((sd0) >> 4) & 1)
#define SD_REF(sd0)       (((sd0) >> 5) & 1)
#define SD_VALID(sd0)     (((sd0) >> 6) & 1)
#define SD_INDIRECT(sd0)  (((sd0) >> 7) & 1)
#define SD_SEG_ADDR(sd1)  ((sd1) & 0xffffffe0)
#define SD_MAX_OFF(sd0)   (((sd0) >> 10) & 0x3fff)
#define SD_ACC(sd0)       (((sd0) >> 24) & 0xff)
#define SD_R_MASK         0x20
#define SD_M_MASK         0x2
#define SD_GOOD_MASK      0x1u
#define SDCE_TAG(sdcl)    ((sdcl) & 0x3ff)

#define SD_ADDR(va)  (mmu_state.sec[SID(va)].addr + (SSL(va) * 8))


/* Convert from sd to sd cache entry */
#define SD_TO_SDCL(va,sd0)     ((sd0 & 0xfffffc00)|SD_TAG(va))
#define SD_TO_SDCH(sd0,sd1)    (SD_SEG_ADDR(sd1)|(sd0 & 0x1e)|1)

/* Note that this is a lossy transform. We will lose the state of the
   I and R flags, as well as the software flags. We don't need them.
   The V and P flags can be inferred as set. */

#define SDCE_TO_SD0(sdch,sdcl) ((sdcl & 0xfffffc00)|0x40|(sdch & 0x1e)|1)
#define SDCE_TO_SD1(sdch)      (sdch & 0xffffffe0)

/* Maximum size (in bytes) of a segment */
#define MAX_OFFSET(sd0)   ((SD_MAX_OFF(sd0) + 1) * 8)

#define PD_PRESENT(pd)    (pd & 1)
#define PD_MODIFIED(pd)   ((pd >> 1) & 1)
#define PD_LAST(pd)       ((pd >> 2) & 1)
#define PD_WFAULT(pd)     ((pd >> 4) & 1)
#define PD_REF(pd)        ((pd >> 5) & 1)
#define PD_ADDR(pd)       (pd & 0xfffff800)   /* Address portion of PD */
#define PD_R_MASK         0x20
#define PD_M_MASK         0x2
#define PD_GOOD_MASK      0x1u
#define PDCLH_USED_MASK   0x40u
#define PDCXL_TAG(pdcxl)  (pdcxl & 0xffff)

#define PD_LOC(sd1,va)    SD_SEG_ADDR(sd1) + (PSL(va) * 4)

/* Page Descriptor Cache Entry
 *
 */

/* Convert from pd to pd cache entry. Alwasy sets "Good" bit. */
#define SD_TO_PDCXL(va,sd0)   ((sd0 & 0xff000000)|PD_TAG(va))
#define PD_TO_PDCXH(pd,sd0)   ((pd & 0xfffff836)|(sd0 & 0x8)|1)

/* Always set 'present' to true on conversion */
#define PDCXH_TO_PD(pdch)     ((pdch & 0xfffff836)|1)
#define PDCXL_TO_ACC(pdcl)    (((pdcl & 0xff000000) >> 24) & 0xff)

/* Fault codes */
#define MMU_FAULT(f) {                                      \
        if (fc) {                                           \
            mmu_state.fcode = ((((uint32)r_acc)<<7)|        \
                               (((uint32)(CPU_CM))<<5)|f);  \
            mmu_state.faddr = va;                           \
        }                                                   \
    }

typedef struct _mmu_sec {
    uint32 addr;
    uint32 len;
} mmu_sec;

typedef struct _mmu_state {
    t_bool enabled;         /* Global enabled/disabled flag */

    uint32 sdcl[MMU_SDCS];  /* SDC low bits (0-31) */
    uint32 sdch[MMU_SDCS];  /* SDC high bits (32-63) */

    uint32 pdcll[MMU_PDCS]; /* PDC low bits (left) (0-31) */
    uint32 pdclh[MMU_PDCS]; /* PDC high bits (left) (32-63) */

    uint32 pdcrl[MMU_PDCS]; /* PDC low bits (right) (0-31) */
    uint32 pdcrh[MMU_PDCS]; /* PDC high bits (right) (32-63) */

    uint32 sra[MMU_SRS];    /* Section RAM A */
    uint32 srb[MMU_SRS];    /* Section RAM B */

    mmu_sec sec[MMU_SRS];   /* Section descriptors decoded from
                               Section RAM A and B */

    uint32 fcode;           /* Fault Code Register */
    uint32 faddr;           /* Fault Address Register */
    uint32 conf;            /* Configuration Register */
    uint32 var;             /* Virtual Address Register */

} MMU_STATE;

extern MMU_STATE mmu_state;

extern volatile int32 stop_reason;
extern DEVICE mmu_dev;

t_stat mmu_init(DEVICE *dptr);
uint32 mmu_read(uint32 pa, size_t size);
void mmu_write(uint32 pa, uint32 val, size_t size);

/* Physical memory read/write */
uint8  pread_b(uint32 pa);
uint16 pread_h(uint32 pa);
uint32 pread_w(uint32 pa);
uint32 pread_w_u(uint32 pa);
void   pwrite_b(uint32 pa, uint8 val);
void   pwrite_h(uint32 pa, uint16 val);
void   pwrite_w(uint32 pa, uint32 val);

/* Virtual memory translation */
uint32 mmu_xlate_addr(uint32 va, uint8 r_acc);
t_stat mmu_decode_vaddr(uint32 vaddr, uint8 r_acc,
                        t_bool fc, uint32 *pa);

#define SHOULD_CACHE_PD(pd) \
    (fc && PD_PRESENT(pd))

#define SHOULD_CACHE_SD(sd)                                 \
    (fc && SD_VALID(sd) && SD_PRESENT(sd))

#define SHOULD_UPDATE_SD_R_BIT(sd)              \
    (MMU_CONF_R && !((sd) & SD_R_MASK))

#define SHOULD_UPDATE_SD_M_BIT(sd)                              \
    (MMU_CONF_M && r_acc == ACC_W && !((sd) & SD_M_MASK))

#define SHOULD_UPDATE_PD_R_BIT(pd)              \
    (!((pd) & PD_R_MASK))

#define SHOULD_UPDATE_PD_M_BIT(pd)                              \
    (r_acc == ACC_W && !((pd) & PD_M_MASK))

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
        if (r_acc != ACC_IF && r_acc != ACC_IFAD) {
            return SCPE_NXM;
        }
        return SCPE_OK;
    case 2: /* Read / Execute */
        if (r_acc != ACC_AF && r_acc != ACC_OF &&
            r_acc != ACC_IF && r_acc != ACC_IFAD &&
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
    sd0 = pread_w(SD_ADDR(va));
    pwrite_w(SD_ADDR(va), sd0|mask);

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
    pd = pread_w(pd_addr);
    pwrite_w(pd_addr, pd|mask);

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

/* Special functions for reading operands and examining memory
   safely */
t_stat read_operand(uint32 va, uint8 *val);
t_stat examine(uint32 va, uint8 *val);
t_stat deposit(uint32 va, uint8 val);

/* Dispatch to the MMU when enabled, or to physical RW when
   disabled */
uint8  read_b(uint32 va, uint8 r_acc);
uint16 read_h(uint32 va, uint8 r_acc);
uint32 read_w(uint32 va, uint8 r_acc);
void   write_b(uint32 va, uint8 val);
void   write_h(uint32 va, uint16 val);
void   write_w(uint32 va, uint32 val);

t_bool addr_is_rom(uint32 pa);
t_bool addr_is_mem(uint32 pa);
t_bool addr_is_io(uint32 pa);

#endif
