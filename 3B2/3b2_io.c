/* 3b2_io.c: AT&T 3B2 Model 400 IO and  CIO feature cards

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

#include "3b2_defs.h"
#include "3b2_io.h"

CIO_STATE  cio[CIO_SLOTS] = {{0}};

struct iolink iotable[] = {
    { MMUBASE,    MMUBASE+MMUSIZE,       &mmu_read,   &mmu_write   },
    { IFBASE,     IFBASE+IFSIZE,         &if_read,    &if_write    },
    { IDBASE,     IDBASE+IDSIZE,         &id_read,    &id_write    },
    { TIMERBASE,  TIMERBASE+TIMERSIZE,   &timer_read, &timer_write },
    { NVRAMBASE,  NVRAMBASE+NVRAMSIZE,   &nvram_read, &nvram_write },
    { CSRBASE,    CSRBASE+CSRSIZE,       &csr_read,   &csr_write   },
    { IUBASE,     IUBASE+IUSIZE,         &iu_read,    &iu_write    },
    { DMAIDBASE,  DMAIDBASE+DMAIDSIZE,   &dmac_read,  &dmac_write  },
    { DMAIUABASE, DMAIUABASE+DMAIUASIZE, &dmac_read,  &dmac_write  },
    { DMAIUBBASE, DMAIUBBASE+DMAIUBSIZE, &dmac_read,  &dmac_write  },
    { DMACBASE,   DMACBASE+DMACSIZE,     &dmac_read,  &dmac_write  },
    { DMAIFBASE,  DMAIFBASE+DMAIFSIZE,   &dmac_read,  &dmac_write  },
    { TODBASE,    TODBASE+TODSIZE,       &tod_read,   &tod_write   },
    { 0, 0, NULL, NULL}
};

void cio_clear(uint8 cid)
{
    cio[cid].id = 0;
    cio[cid].exp_handler = NULL;
    cio[cid].full_handler = NULL;
    cio[cid].reset_handler = NULL;
    cio[cid].sysgen = NULL;
    cio[cid].rqp = 0;
    cio[cid].cqp = 0;
    cio[cid].rqs = 0;
    cio[cid].cqs = 0;
    cio[cid].ivec = 0;
    cio[cid].no_rque = 0;
    cio[cid].ipl = 0;
    cio[cid].intr = FALSE;
    cio[cid].sysgen_s = 0;
    cio[cid].seqbit = 0;
    cio[cid].op = 0;
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

void cio_sysgen(uint8 cid)
{
    uint32 sysgen_p;

    sysgen_p = pread_w(SYSGEN_PTR);

    sim_debug(CIO_DBG, &cpu_dev,
              "[%08x] [SYSGEN] Starting sysgen for card %d. sysgen_p=%08x\n",
              R[NUM_PC], cid, sysgen_p);

    /* seqbit is always reset to 0 on completion */
    cio[cid].seqbit = 0;

    cio[cid].rqp = pread_w(sysgen_p);
    cio[cid].cqp = pread_w(sysgen_p + 4);
    cio[cid].rqs = pread_b(sysgen_p + 8);
    cio[cid].cqs = pread_b(sysgen_p + 9);
    cio[cid].ivec = pread_b(sysgen_p + 10);
    cio[cid].no_rque = pread_b(sysgen_p + 11);

    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen rqp = %08x\n",
              cio[cid].rqp);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen cqp = %08x\n",
              cio[cid].cqp);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen rqs = %02x\n",
              cio[cid].rqs);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen cqs = %02x\n",
              cio[cid].cqs);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen ivec = %02x\n",
              cio[cid].ivec);
    sim_debug(CIO_DBG, &cpu_dev,
              "[SYSGEN]  sysgen no_rque = %02x\n",
              cio[cid].no_rque);

    /* If the card has a custom sysgen handler, run it */
    if (cio[cid].sysgen != NULL) {
        cio[cid].sysgen(cid);
    } else {
        sim_debug(CIO_DBG, &cpu_dev,
                  "[%08x] [cio_sysgen] Not running custom sysgen.\n",
                  R[NUM_PC]);
    }
}

void cio_cexpress(uint8 cid, uint32 esize, cio_entry *cqe, uint8 *app_data)
{
    uint32 i, cqp;

    cqp = cio[cid].cqp;

    sim_debug(CIO_DBG, &cpu_dev,
              "[%08x] [cio_cexpress] cqp = %08x seqbit = %d\n",
              R[NUM_PC], cqp, cio[cid].seqbit);

    cio[cid].seqbit ^= 1;

    cqe->subdevice |= (cio[cid].seqbit << 6);

    pwrite_h(cqp,     cqe->byte_count);
    pwrite_b(cqp + 2, cqe->subdevice);
    pwrite_b(cqp + 3, cqe->opcode);
    pwrite_w(cqp + 4, cqe->address);

    /* Write application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        pwrite_b(cqp + 8 + i, app_data[i]);
    }
}

void cio_cqueue(uint8 cid, uint8 cmd_stat, uint32 esize,
                cio_entry *cqe, uint8 *app_data)
{
    uint32 i, cqp, top;
    uint16 lp;

    /* Apply the CMD/STAT bit */
    cqe->subdevice |= (cmd_stat << 7);

    /* Get the physical address of the completion queue
     * in main memory */
    cqp = cio[cid].cqp;

    /* Get the physical address of the first entry in
     * the completion queue */
    top = cqp + esize + LUSIZE;

    /* Get the load pointer. This is a 16-bit absolute offset
     * from the top of the queue to the start of the entry. */
    lp = pread_h(cqp + esize);

    /* Load the entry at the supplied address */
    pwrite_h(top + lp,     cqe->byte_count);
    pwrite_b(top + lp + 2, cqe->subdevice);
    pwrite_b(top + lp + 3, cqe->opcode);
    pwrite_w(top + lp + 4, cqe->address);

    /* Write application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        pwrite_b(top + lp + 8 + i, app_data[i]);
    }

    /* Increment the load pointer to the next queue location.
     * If we go past the end of the queue, wrap around to the
     * start of the queue */
    if (cio[cid].cqs > 0) {
        lp = (lp + esize) % (esize * cio[cid].cqs);
        /* Store it back to the correct location */
        pwrite_h(cqp + esize, lp);
    }
}

/*
 * Retrieve the Express Entry from the Request Queue
 */
void cio_rexpress(uint8 cid, uint32 esize, cio_entry *rqe, uint8 *app_data)
{
    uint32 i;
    uint32 rqp;

    rqp = cio[cid].rqp;

    /* Unload the express entry from the request queue */
    rqe->byte_count  = pread_h(rqp);
    rqe->subdevice   = pread_b(rqp + 2);
    rqe->opcode      = pread_b(rqp + 3);
    rqe->address     = pread_w(rqp + 4);

    for (i = 0; i < (esize - QESIZE); i++) {
        app_data[i] = pread_b(rqp + 8 + i);
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
t_stat cio_rqueue(uint8 cid, uint32 qnum, uint32 esize,
                  cio_entry *rqe, uint8 *app_data)
{
    uint32 i, rqp, top;
    uint16 lp, ulp;

    /* Get the physical address of the request queue in main memory */
    rqp = cio[cid].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[cid].rqs)));

    lp = pread_h(rqp);
    ulp = pread_h(rqp + 2);

    /* Check to see if the request queue is empty. If it is, there's
     * nothing to take. */
    if (lp == ulp) {
        return SCPE_NXM;
    }

    top = rqp + LUSIZE;

    /* Retrieve the entry at the supplied address */
    rqe->byte_count  = pread_h(top + ulp);
    rqe->subdevice   = pread_b(top + ulp + 2);
    rqe->opcode      = pread_b(top + ulp + 3);
    rqe->address     = pread_w(top + ulp + 4);

    /* Read application-specific data. */
    for (i = 0; i < (esize - QESIZE); i++) {
        app_data[i] = pread_b(top + ulp + 8 + i);
    }

    /* Increment the unload pointer to the next queue location.  If we
     * go past the end of the queue, wrap around to the start of the
     * queue */
    if (cio[cid].rqs > 0) {
        ulp = (ulp + esize) % (esize * cio[cid].rqs);
        pwrite_h(rqp + 2, ulp);
    }

    return SCPE_OK;
}

/*
 * Return the Load Pointer for the given request queue
 */
uint16 cio_r_lp(uint8 cid, uint32 qnum, uint32 esize)
{
    uint32 rqp;

    rqp = cio[cid].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[cid].rqs)));

    return pread_h(rqp);
}

/*
 * Return the Unload Pointer for the given request queue
 */
uint16 cio_r_ulp(uint8 cid, uint32 qnum, uint32 esize)
{
    uint32 rqp;

    rqp = cio[cid].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[cid].rqs)));

    return pread_h(rqp + 2);
}

uint16 cio_c_lp(uint8 cid, uint32 esize)
{
    uint32 cqp;
    cqp = cio[cid].cqp + esize;
    return pread_h(cqp);
}

uint16 cio_c_ulp(uint8 cid, uint32 esize)
{
    uint32 cqp;
    cqp = cio[cid].cqp + esize;
    return pread_h(cqp + 2);
}

/*
 * Returns true if there is room in the completion queue
 * for a new entry.
 */
t_bool cio_cqueue_avail(uint8 cid, uint32 esize)
{
    uint32 lp, ulp;

    lp = pread_h(cio[cid].cqp + esize);
    ulp = pread_h(cio[cid].cqp + esize + 2);

    return(((lp + esize) % (cio[cid].cqs * esize)) != ulp);
}

t_bool cio_rqueue_avail(uint8 cid, uint32 qnum, uint32 esize)
{
    uint32 rqp, lp, ulp;

    /* Get the physical address of the request queue in main memory */
    rqp = cio[cid].rqp +
        esize +
        (qnum * (LUSIZE + (esize * cio[cid].rqs)));

    lp = pread_h(rqp);
    ulp = pread_h(rqp + 2);

    return(lp != ulp);
}

uint32 io_read(uint32 pa, size_t size)
{
    struct iolink *p;
    uint8 cid, reg, data;

    /* Special devices */
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

    /* CIO board area */
    if (pa >= CIO_BOTTOM && pa < CIO_TOP) {
        cid = CID(pa);
        reg = pa - CADDR(cid);

        if (cio[cid].id == 0) {
            /* Nothing lives here */
            sim_debug(IO_DBG, &cpu_dev,
                      "[READ] [%08x] No card at cid=%d reg=%d\n",
                      R[NUM_PC], cid, reg);
            csr_data |= CSRTIMO;
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
            switch(cio[cid].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT0: /* We've seen an INT0 but not an INT1. */
                cio[cid].sysgen_s |= CIO_INT0;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) ID\n",
                          R[NUM_PC], cid);
                /* Return the correct byte of our board ID */
                if (reg == IOF_ID) {
                    data = (cio[cid].id >> 8) & 0xff;
                } else {
                    data = (cio[cid].id & 0xff);
                }
                break;
            case CIO_INT1: /* We've seen an INT1 but not an INT0. Time to sysgen */
                cio[cid].sysgen_s |= CIO_INT0;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) SYSGEN\n",
                          R[NUM_PC], cid);
                cio_sysgen(cid);
                data = cio[cid].ivec;
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                cio[cid].sysgen_s |= CIO_INT0; /* This must come BEFORE the exp_handler */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) EXPRESS JOB\n",
                          R[NUM_PC], cid);
                cio[cid].exp_handler(cid);
                data = cio[cid].ivec;
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          R[NUM_PC], cid, cio[cid].sysgen_s);
                data = 0;
                break;
            }

            return data;
        case IOF_CTRL:
            switch(cio[cid].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT1:     /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) IGNORED\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1;
                break;
            case CIO_INT0: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) SYSGEN\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1;
                cio_sysgen(cid);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) FULL\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1; /* This must come BEFORE the full handler */
                cio[cid].full_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          R[NUM_PC], cid, cio[cid].sysgen_s);
                break;
            }

            return 0; /* Data returned is arbitrary */
        case IOF_STAT:
            sim_debug(CIO_DBG, &cpu_dev,
                      "[READ] [%08x] (%d RESET)\n",
                      R[NUM_PC], cid);
            if (cio[cid].reset_handler) {
                cio[cid].reset_handler(cid);
            }
            cio[cid].sysgen_s = 0;
            return 0; /* Data returned is arbitrary */
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[READ] [%08x] No card at cid=%d reg=%d\n",
                      R[NUM_PC], cid, reg);
            csr_data |= CSRTIMO;
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
              "[%08x] [io_read] ADDR=%08x: No device found.\n",
              R[NUM_PC], pa);
    csr_data |= CSRTIMO;
    cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
    return 0;
}

void io_write(uint32 pa, uint32 val, size_t size)
{
    struct iolink *p;
    uint8 cid, reg;

    /* Feature Card Area */
    if (pa >= CIO_BOTTOM && pa < CIO_TOP) {
        cid = CID(pa);
        reg = pa - CADDR(cid);

        if (cio[cid].id == 0) {
            /* Nothing lives here */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] [%08x] No card at cid=%d reg=%d\n",
                      R[NUM_PC], cid, reg);
            csr_data |= CSRTIMO;
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
            switch(cio[cid].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT0:     /* We've seen an INT0 but not an INT1. */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT0) ID\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT0;
                break;
            case CIO_INT1: /* We've seen an INT1 but not an INT0. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT0) SYSGEN\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT0;
                cio_sysgen(cid);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT0) EXPRESS JOB\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT0;
                cio[cid].exp_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT0) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          R[NUM_PC], cid, cio[cid].sysgen_s);
                break;
            }

            return;
        case IOF_CTRL:
            switch(cio[cid].sysgen_s) {
            case CIO_INT_NONE: /* We've never seen an INT0 or INT1 */
            case CIO_INT1:     /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) IGNORED\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1;
                break;
            case CIO_INT0: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) SYSGEN\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1;
                cio_sysgen(cid);
                break;
            case CIO_SYSGEN: /* We've already sysgen'ed */
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) FULL\n",
                          R[NUM_PC], cid);
                cio[cid].sysgen_s |= CIO_INT1;
                cio[cid].full_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(CIO_DBG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) ERROR IN STATE MACHINE sysgen_s=%02x\n",
                          R[NUM_PC], cid, cio[cid].sysgen_s);
                break;
            }

            return;
        case IOF_STAT:
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] [%08x] (%d RESET)\n",
                      R[NUM_PC], cid);
            if (cio[cid].reset_handler) {
                cio[cid].reset_handler(cid);
            }
            cio[cid].sysgen_s = 0;
            return;
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
            sim_debug(CIO_DBG, &cpu_dev,
                      "[WRITE] [%08x] No card at cid=%d reg=%d\n",
                      R[NUM_PC], cid, reg);
            csr_data |= CSRTIMO;
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
              "[%08x] [io_write] ADDR=%08x: No device found.\n",
              R[NUM_PC], pa);
    csr_data |= CSRTIMO;
    cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
}


/* For debugging only */
void dump_entry(uint32 dbits, DEVICE *dev, CONST char *type,
                uint32 esize, cio_entry *entry, uint8 *app_data)
{
    char appl[64];
    uint32 i, c_offset;

    for (i = 0, c_offset=0; i < (esize - QESIZE); i++) {
        snprintf(appl + c_offset, 3, "%02x", app_data[i]);
        c_offset += 2;
    }

    sim_debug(dbits, dev,
              "*** %s ENTRY: byte_count=%04x, subdevice=%02x, opcode=%d, address=%08x, app_data=%s\n",
              type, entry->byte_count, entry->subdevice,
              entry->opcode, entry->address, appl);
}
