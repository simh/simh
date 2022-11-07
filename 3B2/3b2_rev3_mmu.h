/* 3b2_rev3_mmu.h: WE32201 MMU

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

#ifndef _3B2_REV3_MMU_H_
#define _3B2_REV3_MMU_H_

#include "3b2_defs.h"

#define MMU_SRS         4        /* Section RAM array size (words) */
#define MMU_SDCS        8        /* SD Cache H/L array size */
#define MMU_PDCS        64       /* PD Cache H/L array size */
#define MMU_IDNCS       16       /* ID Number Cache array size */

/* Register address offsets */
#define MMU_SDCL        0        /* SDC - Low Bits */
#define MMU_SDCH        1        /* SDC - High Bits */
#define MMU_PDCL        2        /* PDC - Low Bits */
#define MMU_PDCH        3        /* PDC - High Bits */
#define MMU_FDCR        4        /* Flush Data Cache Register */
#define MMU_SRAMA       6        /* Section RAM A */
#define MMU_SRAMB       7        /* Section RAM B */
#define MMU_FC          8        /* Fault Code */
#define MMU_FA          9        /* Fault Address */
#define MMU_CONF        10       /* Configuration */
#define MMU_VAR         11       /* Virtual Address Register */
#define MMU_IDC         12       /* ID Number Cache */
#define MMU_IDNR        13       /* ID Number Register */
#define MMU_FIDNR       14       /* Flush ID Number Register */
#define MMU_VR          15       /* Version Register */

#define MMU_REV3_VER    0x23     /* Version byte returned by WE32201 MMU */

#define MMU_CONF_M      (mmu_state.conf & 0x1)
#define MMU_CONF_R      ((mmu_state.conf & 0x2) >> 1)
#define MMU_CONF_C      ((mmu_state.conf & 0x4) >> 1)
#define MMU_CONF_PS     ((mmu_state.conf & 0x18) >> 3)
#define MMU_CONF_MCE    ((mmu_state.conf & 0x20) >> 5)
#define MMU_CONF_DCE    ((mmu_state.conf & 0x40) >> 6)

/* Shift and mask the flag bits for the current CPU mode */
#define MMU_PERM(f)     ((f >> ((3 - (CPU_CM)) * 2)) & 3)

/* Codes set in the MMU Fault register */
#define MMU_F_MISS_MEM           1
#define MMU_F_RM_UPD             2
#define MMU_F_SDTLEN             3
#define MMU_F_PW                 4
#define MMU_F_PDTLEN             5
#define MMU_F_INV_SD             6
#define MMU_F_SEG_NOT_PRES       7
#define MMU_F_PDT_NOT_PRES       9
#define MMU_F_PAGE_NOT_PRES      10
#define MMU_F_INDIRECT           11
#define MMU_F_ACC                13
#define MMU_F_SEG_OFFSET         14

/* Access Request types */
#define ACC_MT         0  /* Move Translated */
#define ACC_SPW        1  /* Support processor write */
#define ACC_SPF        3  /* Support processor fetch */
#define ACC_IR         7  /* Interlocked read */
#define ACC_AF         8  /* Address fetch */
#define ACC_OF         9  /* Operand fetch */
#define ACC_W          10 /* Write */
#define ACC_IFAD       12 /* Instruction fetch after discontinuity */
#define ACC_IF         13 /* Instruction fetch */

/* Pluck out Virtual Address fields */
#define SID(va)        (((va) >> 30) & 3)
#define SSL(va)        (((va) >> 17) & 0x1fff)
#define SOT(va)        (va & 0x1ffff)

/* PSL will be either:
 *    - Bits 11-16 (2K pages: MMU_CONF_PS = 0)
 *    - Bits 12-16 (4K pages: MMU_CONF_PS = 1)
 *    - Bits 13-16 (8K pages: MMU_CONF_PS = 2)
 */
#define PSL(va)        (((va) >> (11 + MMU_CONF_PS)) & (0x3f >> MMU_CONF_PS))
#define PSL_C(va)      (((va) & pd_psl_masks[MMU_CONF_PS]))

/* POT will be either:
 *    - Bits 0-10 (2K pages: MMU_CONF_PS = 0)
 *    - Bits 0-11 (4K pages: MMU_CONF_PS = 1)
 *    - Bits 0-12 (8K pages: MMU_CONF_PS = 2)
 */
#define POT(va)        ((va) & (0x1fff >> (2 - (MMU_CONF_PS))))

/* Get the maximum length of an SSL from SRAMB */
#define SRAMB_LEN(va)     (mmu_state.sec[SID(va)].len)

/* Pluck out Segment Descriptor fields */
#define SD_PRESENT(sd_lo)   ((sd_lo) & 1)
#define SD_MODIFIED(sd_lo)  (((sd_lo) >> 1) & 1)
#define SD_CONTIG(sd_lo)    (((sd_lo) >> 2) & 1)
#define SD_VALID(sd_lo)     (((sd_lo) >> 6) & 1)
#define SD_INDIRECT(sd_lo)  (((sd_lo) >> 7) & 1)
#define SD_MAX_OFF(sd_lo)   (((sd_lo) >> 18) & 0x3f)
#define SD_ACC(sd_lo)       (((sd_lo) >> 24) & 0xff)

#define SD_SEG_ADDR(sd_hi)  ((sd_hi) & 0xfffffff8u)

#define SD_P_MASK         0x1
#define SD_M_MASK         0x2
#define SD_C_MASK         0x4
#define SD_CACHE_MASK     0x8
#define SD_R_MASK         0x20
#define SD_V_MASK         0x40
#define SD_MAX_OFF_MASK   0xfc0000
#define SD_ACC_MASK       0xff000000u
#define SD_ADDR_MASK      0xfffffff8u
#define SD_VADDR_MASK     0xfff00000u
#define SD_RES_MASK       0xfffc00efu

#define SDC_VADDR_MASK    0xfff
#define SDC_ACC_MASK      0xff000000u
#define SDC_MAX_OFF_MASK  0x001f8000
#define SDC_G_MASK        0x1
#define SDC_C_MASK        0x2
#define SDC_CACHE_MASK    0x4
#define SDC_M_MASK        0x400000
#define SDC_R_MASK        0x800000

#define PD_P_MASK         0x1
#define PD_M_MASK         0x2
#define PD_W_MASK         0x10
#define PD_R_MASK         0x20
#define PD_PADDR_MASK     0xfffff800u

#define PDC_PADDR_MASK    0x1fffff
#define PDC_C_MASK        0x2
#define PDC_W_MASK        0x200000
#define PDC_M_MASK        0x400000
#define PDC_R_MASK        0x800000
#define PDC_G_MASK        0x40000000
#define PDC_U_MASK        0x80000000u

#define MAX_INDIRECTS     3

#define PD_ADDR(pd)       (pd & (pd_addr_masks[MMU_CONF_PS]))
#define SD_ADDR(va)       (mmu_state.sec[SID(va)].addr + (SSL(va) * 8))

#define SDC_IDX(va)       ((uint8)((va) >> 17) & 7)

/* Convert from sd to sd cache entry */
#define SD_TO_SDCH(hi,lo)     (((hi) & SD_ADDR_MASK)         | \
                               ((lo) & SD_C_MASK) >> 1       | \
                               ((lo) & SD_CACHE_MASK) >> 1   | \
                               (SDC_G_MASK))
#define SD_TO_SDCL(lo,va)     (((lo) & SD_ACC_MASK)          | \
                               ((lo) & SD_MAX_OFF_MASK) >> 3 | \
                               ((lo) & SD_R_MASK) << 18      | \
                               ((lo) & SD_M_MASK) << 21      | \
                               ((va) & SD_VADDR_MASK) >> 20)

/* Convert from sd cache entry to sd */
#define SDCE_TO_SDH(hi)       ((hi) & SD_ADDR_MASK)
#define SDCE_TO_SDL(hi,lo)    (((lo) & SDC_ACC_MASK)          | \
                               ((lo) & SDC_MAX_OFF_MASK) << 3 | \
                               ((lo) & SDC_R_MASK) >> 18      | \
                               ((lo) & SDC_M_MASK) >> 21      | \
                               ((hi) & SDC_C_MASK) << 1       | \
                               ((hi) & SDC_CACHE_MASK) << 1   | \
                               SD_V_MASK | SD_P_MASK)

/* Convert from pd cache entry to pd */
#define PDCE_TO_PD(pdcl)      ((((pdcl) & PDC_PADDR_MASK) << 11) | \
                               (((pdcl) & PDC_W_MASK) >> 17)     | \
                               (((pdcl) & PDC_M_MASK) >> 21)     | \
                               (((pdcl) & PDC_R_MASK) >> 18)     | \
                               PD_P_MASK)

/* Convert from pd to pd cache entry (low word) */
#define PD_TO_PDCL(pd, sd_lo) ((((pd) & PD_PADDR_MASK) >> 11) | \
                               (((pd) & PD_W_MASK)     << 17) | \
                               (((pd) & PD_M_MASK)     << 21) | \
                               (((pd) & PD_R_MASK)     << 18) | \
                               ((sd_lo) & SD_ACC_MASK))

/* Convert from va to pd cache entry (high word / tag) */
#define VA_TO_PDCH(va, sd_lo) ((1 << 30)                        | \
                               (mmu_state.cidnr[SID(va)] << 26) | \
                               ((va & 0xfffff800u)       >> 6)  | \
                               ((sd_lo & SD_CACHE_MASK)  >> 1)  | \
                               ((sd_lo & SD_C_MASK)      >> 1))

/* Maximum offset (in bytes) of a paged segment */
#define MAX_SEG_OFF(w)     (((SD_MAX_OFF(w) + 1) * ((MMU_CONF_PS + 1) * 2048)) - 1)

#define IDNC_TAG(val)      ((val) & 0xfffffff8)
#define IDNC_U(val)        ((val) & 0x1)

/* Fault codes */
#define MMU_FAULT(f) {                                      \
        if (fc) {                                           \
            mmu_state.fcode = ((((uint32)r_acc)<<7) |       \
                               (((uint32)(CPU_CM))<<5) |    \
                               (f & 0x1f));                 \
            mmu_state.faddr = va;                           \
        }                                                   \
    }

typedef struct {
    uint32 addr;
    uint32 len;
} mmu_sec;


/*
 * Segment Descriptor Cache Entry Format
 * =====================================
 *
 * The Segment Descriptor Cache is a directly mapped cache, indexed by
 * bits 19, 18, and 17 of the virtual address. Some notes:
 *
 *   - "Acc", "R", "M", "Max Offset", "Address", "$", and "C" are all
 *     copied from the SD in main memory.
 *   - "VAddr" holds bits 20-31 of the virtual address
 *   - "Address" holds a pointer (word-aligned, so the top 30 bits) to
 *     a page descriptor table in paged mode, or a segment in
 *     contiguous segment mode.
 *   - "Max Offset" holds the number of pages minus one in the
 *     segment. Depending on current page size, various bits of this
 *     field will be ignored:
 *         o Bits 20-15 are used for 2K pages
 *         o Bits 20-16 are used for 4K pages
 *         o Bits 20-17 are used for 8K pages
 *
 * Low Word (bits 0-31)
 * --------------------
 *
 *  31   24  23  22  21  20       15 14  12 11                       0
 * +-------+---+---+---+------------+------+--------------------------+
 * |  Acc  | R | M | - | Max Offset |   -  |         VAddr            |
 * +-------+---+---+---+------------+------+--------------------------+
 *
 * High Word (bits 32-63)
 * ----------------------
 *
 *  31                                                  3   2   1   0
 * +------------------------------------------------------+---+---+---+
 * |                       Address                        | $ | C | G |
 * +------------------------------------------------------+---+---+---+
 *
 *
 * Page Descriptor Cache Entry Format
 * ==================================
 *
 * The Page Descriptor Cache is a fully associative cache, with a
 * tag constructed from the "G" and "IDN" bits, and bits 31-11 of
 * the virtual address.
 *
 * Depending on the current page size and access mode, various bits of
 * "VAddr" are ignored.
 *
 *    o Multi-context mode, all ops except single-entry flush:
 *      VAddr bits 29-11 are used.
 *    o Multi-context mode, single-entry flush:
 *      VAddr bits 31-11 are used.
 *    o Single-context mode, all ops:
 *      Vaddr bits 31-11 are used.
 *    o In ALL CASES:
 *      + 2KB Page Size: Bits 11-12 are used.
 *      + 4KB Page Size: Bit 11 ignored, 12 used.
 *      + 8KB Page Size: Bits 11-12 ignored.
 *
 * Low Word (bits 0-31)
 * --------------------
 *
 *  31   24  23  22  21  20                                          0
 * +-------+---+---+---+----------------------------------------------+
 * |  Acc  | R | M | W |                Physical Address              |
 * +-------+---+---+---+----------------------------------------------+
 *
 *
 * High Word (bits 32-63)
 * ----------------------
 *
 *   31  30 29     26  25                       5   4   3   2   1   0
 * +---+---+---------+----------------------------+-------+---+---+---+
 * | U | G |   IDN   | (31)       VAddr       (11)|   -   | $ | C | - |
 * +---+---+---------+----------------------------+-------+---+---+---+
 *
 */

typedef struct _mmu_state {
    t_bool enabled;         /* Global enabled/disabled flag */

    t_bool flush_u;         /* If true, flush all but last cached entry */
    uint32 last_cached;     /* The index of the last cached PDC entry */

    uint32 sdcl[MMU_SDCS];  /* SDC low bits (0-31) */
    uint32 sdch[MMU_SDCS];  /* SDC high bits (32-63) */

    uint32 pdcl[MMU_PDCS];  /* PDC low bits (0-31) */
    uint32 pdch[MMU_PDCS];  /* PDC high bits (32-63) */

    uint32 sra[4];          /* Section RAM A */
    uint32 srb[4];          /* Section RAM B */

    uint32 cidnr[4];        /* Current ID Number Registers */
    uint32 idnc[16];        /* ID Number Cache */

    mmu_sec sec[4];         /* Section descriptors decoded from
                               Section RAM A and B */

    uint32 fcode;           /* Fault Code Register */
    uint32 faddr;           /* Fault Address Register */
    uint32 conf;            /* Configuration Register */
    uint32 var;             /* Virtual Address Register */

} MMU_STATE;

t_stat mmu_init(DEVICE *dptr);
uint32 mmu_read(uint32 pa, size_t size);
void   mmu_write(uint32 pa, uint32 val, size_t size);
CONST char *mmu_description(DEVICE *dptr);

/* Virtual memory translation */
uint32 mmu_xlate_addr(uint32 va, uint8 r_acc);
t_stat mmu_decode_vaddr(uint32 vaddr, uint8 r_acc,
                        t_bool fc, uint32 *pa);

t_stat mmu_decode_va(uint32 va, uint8 r_acc, t_bool fc, uint32 *pa);
void   mmu_enable();
void   mmu_disable();

t_stat mmu_show_sdt(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat mmu_show_sdc(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat mmu_show_pdc(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

#endif /* _3B2_REV3_MMU_H_ */
