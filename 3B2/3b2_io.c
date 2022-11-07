/* 3b2_io.c: Common I/O (CIO) Feature Card Support

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

#include "3b2_io.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"
#include "3b2_dmac.h"
#include "3b2_if.h"
#include "3b2_iu.h"
#include "3b2_mem.h"
#include "3b2_mmu.h"
#include "3b2_stddev.h"
#include "3b2_timer.h"

#if defined(REV2)
#include "3b2_id.h"
#endif

CIO_STATE cio[CIO_SLOTS] = {{0}};
uint16 cio_int_req = 0; /* Bitset of card slots requesting interrupts */

#if defined(REV3)
iolink iotable[] = {
    { MMUBASE,    MMUBASE+MMUSIZE,       &mmu_read,    &mmu_write    },
    { IFBASE,     IFBASE+IFSIZE,         &if_read,     &if_write     },
    { IFCSRBASE,  IFCSRBASE+IFCSRSIZE,   &if_csr_read, &if_csr_write },
    { FLTLBASE,   FLTLBASE+FLTLSIZE,     &flt_read,    &flt_write    },
    { FLTHBASE,   FLTHBASE+FLTHSIZE,     &flt_read,    &flt_write    },
    { NVRBASE,    NVRBASE+NVRSIZE,       &nvram_read,  &nvram_write  },
    { TIMERBASE,  TIMERBASE+TIMERSIZE,   &timer_read,  &timer_write  },
    { CSRBASE,    CSRBASE+CSRSIZE,       &csr_read,    &csr_write    },
    { IUBASE,     IUBASE+IUSIZE,         &iu_read,     &iu_write     },
    { DMAIUABASE, DMAIUABASE+DMAIUASIZE, &dmac_read,   &dmac_write   },
    { DMAIUBBASE, DMAIUBBASE+DMAIUBSIZE, &dmac_read,   &dmac_write   },
    { DMACBASE,   DMACBASE+DMACSIZE,     &dmac_read,   &dmac_write   },
    { DMAIFBASE,  DMAIFBASE+DMAIFSIZE,   &dmac_read,   &dmac_write   },
    { TODBASE,    TODBASE+TODSIZE,       &tod_read,    &tod_write    },
    { 0, 0, NULL, NULL}
};
#else
iolink iotable[] = {
    { MMUBASE,    MMUBASE+MMUSIZE,       &mmu_read,   &mmu_write   },
    { IFBASE,     IFBASE+IFSIZE,         &if_read,    &if_write    },
    { IDBASE,     IDBASE+IDSIZE,         &id_read,    &id_write    },
    { DMAIDBASE,  DMAIDBASE+DMAIDSIZE,   &dmac_read,  &dmac_write  },
    { NVRBASE,    NVRBASE+NVRSIZE,       &nvram_read, &nvram_write },
    { TIMERBASE,  TIMERBASE+TIMERSIZE,   &timer_read, &timer_write },
    { CSRBASE,    CSRBASE+CSRSIZE,       &csr_read,   &csr_write   },
    { IUBASE,     IUBASE+IUSIZE,         &iu_read,    &iu_write    },
    { DMAIUABASE, DMAIUABASE+DMAIUASIZE, &dmac_read,  &dmac_write  },
    { DMAIUBBASE, DMAIUBBASE+DMAIUBSIZE, &dmac_read,  &dmac_write  },
    { DMACBASE,   DMACBASE+DMACSIZE,     &dmac_read,  &dmac_write  },
    { DMAIFBASE,  DMAIFBASE+DMAIFSIZE,   &dmac_read,  &dmac_write  },
    { TODBASE,    TODBASE+TODSIZE,       &tod_read,   &tod_write   },
    { 0, 0, NULL, NULL}
};
#endif

/*
 * Insert a CIO card into the backplane.
 *
 * If a space could be found, SPCE_OK is returned, and the slot the
 * card was installed in is placed in `slot`.
 *
 * If no room is availalbe, return SCPE_NXM.
 */
t_stat cio_install(uint16 id,
                   CONST char *name,
                   uint8 ipl,
                   void (*exp_handler)(uint8 slot),
                   void (*full_handler)(uint8 slot),
                   void (*sysgen)(uint8 slot),
                   void (*reset_handler)(uint8 slot),
                   uint8 *slot)
{
    uint8 s;

    for (s = 0; s < CIO_SLOTS; s++) {
        sim_debug(EXECUTE_MSG, &cpu_dev,
                  "[cio_install]  cio[%d]: populated=%d, id=%d\n",
                  s, cio[s].populated, cio[s].id);
        if (!cio[s].populated) {
            sim_debug(EXECUTE_MSG, &cpu_dev,
                      "[cio_install]    >>> I found a free slot! Slot #%d has nothing\n", s);
            *slot = s;
            /* Ensure the slot is in a clean state */
            cio_remove(s);
            /* Populate the slot */
            cio[s].populated = TRUE;
            cio[s].id = id;
            cio[s].ipl = ipl;
            strncpy(cio[s].name, name, CIO_NAME_LEN);
            cio[s].exp_handler = exp_handler;
            cio[s].full_handler = full_handler;
            cio[s].sysgen = sysgen;
            cio[s].reset_handler = reset_handler;
            return SCPE_OK;
        }
    }

    return SCPE_NXM;
}

/*
 * Remove a CIO card from the specified backplane slot.
 */
void cio_remove(uint8 slot)
{
    memset(&cio[slot], 0, sizeof(CIO_STATE));
    /*    cio[slot].populated = FALSE; */
    CIO_CLR_INT(slot);
}

/*
 * Remove all CIO cards of the matching type.
 */
void cio_remove_all(uint16 id)
{
    int i;

    for (i = 0; i < CIO_SLOTS; i++) {
        if (cio[i].populated && cio[i].id == id) {
            cio_remove(i);
        }
    }
}

/*
 * A braindead CRC32 calculator.
 *
 * This is overkill for what we need: A simple way to tag the contents
 * of a block of memory uploaded to a CIO card (so we can
 * differentiate between desired functions without actually having to
 * disassemble and understand 80186 code!)
 */
uint32 cio_crc32_shift(uint32 crc, uint8 data)
{
    uint8 i;

    crc = ~crc;
    crc ^= data;
    for (i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ CRC_POLYNOMIAL;
        } else {
            crc = crc >> 1;
        }
    }

    return ~crc;
}

void cio_sysgen(uint8 slot)
{
    uint32 sysgen_p;

    sysgen_p = pread_w(SYSGEN_PTR, BUS_PER);

    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN] Starting sysgen for card %d (%s). sysgen_p=%08x\n",
              slot, cio[slot].name, sysgen_p);

    /* seqbit is always reset to 0 on completion */
    cio[slot].seqbit = 0;

    cio[slot].rqp = pread_w(sysgen_p, BUS_PER);
    cio[slot].cqp = pread_w(sysgen_p + 4, BUS_PER);
    cio[slot].rqs = pread_b(sysgen_p + 8, BUS_PER);
    cio[slot].cqs = pread_b(sysgen_p + 9, BUS_PER);
    cio[slot].ivec = pread_b(sysgen_p + 10, BUS_PER);
    cio[slot].no_rque = pread_b(sysgen_p + 11, BUS_PER);

    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen rqp = %08x\n",
              cio[slot].rqp);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen cqp = %08x\n",
              cio[slot].cqp);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen rqs = %02x\n",
              cio[slot].rqs);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen cqs = %02x\n",
              cio[slot].cqs);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen ivec = %02x\n",
              cio[slot].ivec);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen no_rque = %02x\n",
              cio[slot].no_rque);

    /* If the card has a custom sysgen handler, run it */
    if (cio[slot].sysgen != NULL) {
        cio[slot].sysgen(slot);
    } else {
        sim_debug(CIO_DBG, &cpu_dev,
                  "[cio_sysgen] Not running custom sysgen.\n");
    }
}

void cio_cexpress(uint8 slot, uint32 esize, cio_entry *cqe, uint8 *app_data)
{
    uint32 i, cqp;

    cqp = cio[slot].cqp;

    sim_debug(CIO_DBG, &cpu_dev,
              "[cio_cexpress] [%s] cqp = %08x seqbit = %d\n",
              cio[slot].name, cqp, cio[slot].seqbit);

    cio[slot].seqbit ^= 1;

    cqe->subdevice |= (cio[slot].seqbit << 6);

    pwrite_h(cqp,     cqe->byte_count, BUS_PER);
    pwrite_b(cqp + 2, cqe->subdevice, BUS_PER);
    pwrite_b(cqp + 3, cqe->opcode, BUS_PER);
    pwrite_w(cqp + 4, cqe->address, BUS_PER);

    /* Write application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        pwrite_b(cqp + 8 + i, app_data[i], BUS_PER);
    }
}

void cio_cqueue(uint8 slot, uint8 cmd_stat, uint32 esize,
                cio_entry *cqe, uint8 *app_data)
{
    uint32 i, cqp, top;
    uint16 lp;

    /* Apply the CMD/STAT bit */
    cqe->subdevice |= (cmd_stat << 7);

    /* Get the physical address of the completion queue
     * in main memory */
    cqp = cio[slot].cqp;

    /* Get the physical address of the first entry in
     * the completion queue */
    top = cqp + esize + LUSIZE;

    /* Get the load pointer. This is a 16-bit absolute offset
     * from the top of the queue to the start of the entry. */
    lp = pread_h(cqp + esize, BUS_PER);

    /* Load the entry at the supplied address */
    pwrite_h(top + lp,     cqe->byte_count, BUS_PER);
    pwrite_b(top + lp + 2, cqe->subdevice, BUS_PER);
    pwrite_b(top + lp + 3, cqe->opcode, BUS_PER);
    pwrite_w(top + lp + 4, cqe->address, BUS_PER);

    /* Write application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        pwrite_b(top + lp + 8 + i, app_data[i], BUS_PER);
    }

    /* Increment the load pointer to the next queue location.
     * If we go past the end of the queue, wrap around to the
     * start of the queue */
    if (cio[slot].cqs > 0) {
        lp = (lp + esize) % (esize * cio[slot].cqs);
        /* Store it back to the correct location */
        pwrite_h(cqp + esize, lp, BUS_PER);
    }
}

/*
 * Retrieve the Express Entry from the Request Queue
 */
void cio_rexpress(uint8 slot, uint32 esize, cio_entry *rqe, uint8 *app_data)
{
    uint32 i;
    uint32 rqp;

    rqp = cio[slot].rqp;

    /* Unload the express entry from the request queue */
    rqe->byte_count  = pread_h(rqp, BUS_PER);
    rqe->subdevice   = pread_b(rqp + 2, BUS_PER);
    rqe->opcode      = pread_b(rqp + 3, BUS_PER);
    rqe->address     = pread_w(rqp + 4, BUS_PER);

    for (i = 0; i < (esize - QESIZE); i++) {
        app_data[i] = pread_b(rqp + 8 + i, BUS_PER);
    }
}

/*
 * Retrieve an entry from the Request Queue. This function
 * returns the load pointer that points to the NEXT available slot.
 * This may be used by callers to determine which queue(s) need to
 * be serviced.
 *
 * Returns SCPE_OK on success, or SCPE_NXM if no entry was found.
 *
 */
t_stat cio_rqueue(uint8 slot, uint32 qnum, uint32 esize,
                  cio_entry *rqe, uint8 *app_data)
{
    uint32 i, rqp, top;
    uint16 lp, ulp;

    /* Get the physical address of the request queue in main memory */
    rqp = cio[slot].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[slot].rqs)));

    lp = pread_h(rqp, BUS_PER);
    ulp = pread_h(rqp + 2, BUS_PER);

    /* Check to see if the request queue is empty. If it is, there's
     * nothing to take. */
    if (lp == ulp) {
        return SCPE_NXM;
    }

    top = rqp + LUSIZE;

    /* Retrieve the entry at the supplied address */
    rqe->byte_count  = pread_h(top + ulp, BUS_PER);
    rqe->subdevice   = pread_b(top + ulp + 2, BUS_PER);
    rqe->opcode      = pread_b(top + ulp + 3, BUS_PER);
    rqe->address     = pread_w(top + ulp + 4, BUS_PER);

    /* Read application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        app_data[i] = pread_b(top + ulp + 8 + i, BUS_PER);
    }

    /* Increment the unload pointer to the next queue location.  If we
     * go past the end of the queue, wrap around to the start of the
     * queue */
    if (cio[slot].rqs > 0) {
        ulp = (ulp + esize) % (esize * cio[slot].rqs);
        pwrite_h(rqp + 2, ulp, BUS_PER);
    }

    return SCPE_OK;
}

/*
 * Return the Load Pointer for the given request queue
 */
uint16 cio_r_lp(uint8 slot, uint32 qnum, uint32 esize)
{
    uint32 rqp;

    rqp = cio[slot].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[slot].rqs)));

    return pread_h(rqp, BUS_PER);
}

/*
 * Return the Unload Pointer for the given request queue
 */
uint16 cio_r_ulp(uint8 slot, uint32 qnum, uint32 esize)
{
    uint32 rqp;

    rqp = cio[slot].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[slot].rqs)));

    return pread_h(rqp + 2, BUS_PER);
}

uint16 cio_c_lp(uint8 slot, uint32 esize)
{
    uint32 cqp;
    cqp = cio[slot].cqp + esize;
    return pread_h(cqp, BUS_PER);
}

uint16 cio_c_ulp(uint8 slot, uint32 esize)
{
    uint32 cqp;
    cqp = cio[slot].cqp + esize;
    return pread_h(cqp + 2, BUS_PER);
}

/*
 * Returns true if there is room in the completion queue
 * for a new entry.
 */
t_bool cio_cqueue_avail(uint8 slot, uint32 esize)
{
    uint32 lp, ulp;

    lp = pread_h(cio[slot].cqp + esize, BUS_PER);
    ulp = pread_h(cio[slot].cqp + esize + 2, BUS_PER);

    return(((lp + esize) % (cio[slot].cqs * esize)) != ulp);
}

t_bool cio_rqueue_avail(uint8 slot, uint32 qnum, uint32 esize)
{
    uint32 rqp, lp, ulp;

    /* Get the physical address of the request queue in main memory */
    rqp = cio[slot].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[slot].rqs)));

    lp = pread_h(rqp, BUS_PER);
    ulp = pread_h(rqp + 2, BUS_PER);

    return(lp != ulp);
}

uint32 io_read(uint32 pa, size_t size)
{
    iolink *p;
    uint8 slot, reg, data;

#if defined (REV3)
    if (pa >= VCACHE_BOTTOM && pa < VCACHE_TOP) {
        sim_debug(EXECUTE_MSG, &cpu_dev,
                  "[UBUB] (VCACHE) Read addr %08x\n", pa);
        CSRBIT(CSRTIMO, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        return 0;
    }

    if (pa >= BUB_BOTTOM && pa < BUB_TOP) {
        sim_debug(EXECUTE_MSG, &cpu_dev,
                  "[BUB] Read addr %08x\n", pa);
        CSRBIT(CSRTIMO, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        return 0;
    }
#else
    if (pa == MEMSIZE_REG) {

        /* The following values map to memory sizes:
           0x00: 512KB (  524,288 B)
           0x01: 2MB   (2,097,152 B)
           0x02: 1MB   (1,048,576 B)
           0x03: 4MB   (4,194,304 B)
        */
        switch(MEM_SIZE) {
        case 0x80000:  /* 512KB */
            return 0;
        case 0x100000: /* 1MB */
            return 2;
        case 0x200000: /* 2MB */
            return 1;
        case 0x400000: /* 4MB */
            return 3;
        default:
            return 0;
        }
    }
#endif

    /* CIO board area */
    if (pa >= CIO_BOTTOM && pa < CIO_TOP) {
        slot = SLOT(pa);
        reg = pa - CADDR(slot);

        if (!cio[slot].populated) {
            /* Nothing lives here */
            sim_debug(IO_DBG, &cpu_dev,
                      "[READ] No card at slot=%d reg=%d\n",
                      slot, reg);
            CSRBIT(CSRTIMO, TRUE);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
            return 0;
        }

        /* A normal SYSGEN sequence is: RESET -> INT0 -> INT1.
         * However, there's a bug in the 3B2/400 DGMON test suite that
         * runs on every startup. This diagnostic code performs a
         * SYSGEN by calling RESET -> INT1 -> INT0. So, we must handle
         * both orders. */

        switch (reg) {
        case IOF_ID:
        case IOF_VEC:
            switch(cio[slot].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT0: /* We've seen an INT0 but not an INT1. */
                cio[slot].sysgen_s |= CIO_INT0;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT0) ID\n",
                          cio[slot].name, slot);
                /* Return the correct byte of our board ID */
                if (reg == IOF_ID) {
                    data = (cio[slot].id >> 8) & 0xff;
                } else {
                    data = (cio[slot].id & 0xff);
                }
                break;
            case CIO_INT1: /* We've seen an INT1 but not an INT0. Time to sysgen */
                cio[slot].sysgen_s |= CIO_INT0;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT0) SYSGEN\n",
                          cio[slot].name, slot);
                cio_sysgen(slot);
                data = cio[slot].ivec;
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                cio[slot].sysgen_s |= CIO_INT0; /* This must come BEFORE the exp_handler */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT0) EXPRESS JOB\n",
                          cio[slot].name, slot);
                cio[slot].exp_handler(slot);
                data = cio[slot].ivec;
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT0) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          cio[slot].name, slot, cio[slot].sysgen_s);
                data = 0;
                break;
            }

            return data;
        case IOF_CTRL:
            switch(cio[slot].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT1:     /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT1) IGNORED\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1;
                break;
            case CIO_INT0: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT1) SYSGEN\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1;
                cio_sysgen(slot);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT1) FULL\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1; /* This must come BEFORE the full handler */
                cio[slot].full_handler(slot);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%s] (%d INT1) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          cio[slot].name, slot, cio[slot].sysgen_s);
                break;
            }

            return 0; /* Data returned is arbitrary */
        case IOF_STAT:
            sim_debug(CIO_DBG, &cpu_dev,
                      "[READ] [%s] (%d RESET)\n",
                      cio[slot].name, slot);
            if (cio[slot].reset_handler) {
                cio[slot].reset_handler(slot);
            }
            cio[slot].sysgen_s = 0;
            return 0; /* Data returned is arbitrary */
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[READ] No card at slot=%d reg=%d\n",
                      slot, reg);
            CSRBIT(CSRTIMO, TRUE);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
            return 0;
        }
    }

    /* Memory-mapped IO devices */
    for (p = &iotable[0]; p->low != 0; p++) {
        if ((pa >= p->low) && (pa < p->high) && p->read) {
            return p->read(pa, size);
        }
    }

    /* Not found. */
    sim_debug(IO_DBG, &cpu_dev,
              "[io_read] ADDR=%08x: No device found.\n",
              pa);
    CSRBIT(CSRTIMO, TRUE);
    cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    return 0;
}

void io_write(uint32 pa, uint32 val, size_t size)
{
    iolink *p;
    uint8 slot, reg;

#if defined(REV3)
    if (pa >= VCACHE_BOTTOM && pa < VCACHE_TOP) {
        sim_debug(EXECUTE_MSG, &cpu_dev,
                  "[UBUB] (VCACHE) Write addr %08x val 0x%x\n",
                  pa, val);
        CSRBIT(CSRTIMO, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        return;
    }

    if (pa >= BUB_BOTTOM && pa < BUB_TOP) {
        sim_debug(EXECUTE_MSG, &cpu_dev,
                  "[BUB] Write addr %08x val 0x%x\n",
                  pa, val);
        CSRBIT(CSRTIMO, TRUE);
        cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        return;
    }
#endif

    /* Feature Card Area */
    if (pa >= CIO_BOTTOM && pa < CIO_TOP) {
        slot = SLOT(pa);
        reg = pa - CADDR(slot);

        if (!cio[slot].populated) {
            /* Nothing lives here */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] No card at slot=%d reg=%d\n",
                      slot, reg);
            CSRBIT(CSRTIMO, TRUE);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
            return;
        }

        /* A normal SYSGEN sequence is: RESET -> INT0 -> INT1.
         * However, there's a bug in the 3B2/400 DGMON test suite that
         * runs on every startup. This diagnostic code performs a
         * SYSGEN by calling RESET -> INT1 -> INT0. So, we must handle
         * both orders. */

        switch (reg) {
        case IOF_ID:
        case IOF_VEC:
            switch(cio[slot].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT0:     /* We've seen an INT0 but not an INT1. */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT0) ID\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT0;
                break;
            case CIO_INT1: /* We've seen an INT1 but not an INT0. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT0) SYSGEN\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT0;
                cio_sysgen(slot);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT0) EXPRESS JOB\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT0;
                cio[slot].exp_handler(slot);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT0) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          cio[slot].name, slot, cio[slot].sysgen_s);
                break;
            }

            return;
        case IOF_CTRL:
            switch(cio[slot].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT1:     /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT1) IGNORED\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1;
                break;
            case CIO_INT0: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT1) SYSGEN\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1;
                cio_sysgen(slot);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT1) FULL\n",
                          cio[slot].name, slot);
                cio[slot].sysgen_s |= CIO_INT1;
                cio[slot].full_handler(slot);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%s] (%d INT1) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          cio[slot].name, slot, cio[slot].sysgen_s);
                break;
            }

            return;
        case IOF_STAT:
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] [%s] (%d RESET)\n",
                      cio[slot].name, slot);
            if (cio[slot].reset_handler) {
                cio[slot].reset_handler(slot);
            }
            cio[slot].sysgen_s = 0;
            return;
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] No card at slot=%d reg=%d\n",
                      slot, reg);
            CSRBIT(CSRTIMO, TRUE);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
            return;
        }
    }

    /* Memory-mapped IO devices */
    for (p = &iotable[0]; p->low != 0; p++) {
        if ((pa >= p->low) && (pa < p->high) && p->write) {
            p->write(pa, val, size);
            return;
        }
    }

    /* Not found. */
    sim_debug(IO_DBG, &cpu_dev,
              "[io_write] ADDR=%08x: No device found.\n",
              pa);
    CSRBIT(CSRTIMO, TRUE);
    cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
}
