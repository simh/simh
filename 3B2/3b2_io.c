/* 3b2_cpu.h: AT&T 3B2 Model 400 IO and  CIO feature cards

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

#include "3b2_io.h"

CIO_STATE  cio[CIO_SLOTS] = { 0 };

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

void cio_sysgen(uint8 cid)
{
    uint32 sysgen_p;
    uint32 cq_exp;
    cio_entry cqe;

    sysgen_p = pread_w(SYSGEN_PTR);

    sim_debug(IO_D_MSG, &cpu_dev,
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

    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen rqp = %08x\n",
              cio[cid].rqp);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen cqp = %08x\n",
              cio[cid].cqp);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen rqs = %02x\n",
              cio[cid].rqs);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen cqs = %02x\n",
              cio[cid].cqs);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen ivec = %02x\n",
              cio[cid].ivec);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN]  sysgen no_rque = %02x\n",
              cio[cid].no_rque);

    cq_exp = cio[cid].cqp;

    cqe.byte_count = 0;
    cqe.subdevice = 0;
    cqe.opcode = 3;
    cqe.address = 0;
    cqe.app_data = 0;

    cio_cexpress(cid, &cqe);
    sim_debug(IO_D_MSG, &cpu_dev,
              "[SYSGEN] Sysgen complete. Completion Queue written.\n");

    /* If the card has a custom sysgen handler, run it */
    if (cio[cid].sysgen != NULL) {
        cio[cid].sysgen(cid);
    } else {
        sim_debug(IO_D_MSG, &cpu_dev,
                  "[%08x] [cio_sysgen] Not running custom sysgen.\n",
                  R[NUM_PC]);
    }
}

void cio_cexpress(uint8 cid, cio_entry *cqe)
{
    uint32 cqp;

    cqp = cio[cid].cqp;

    sim_debug(IO_D_MSG, &cpu_dev,
              "[%08x] [cio_cexpress] cqp = %08x seqbit = %d\n",
              R[NUM_PC], cqp, cio[cid].seqbit);

    cio[cid].seqbit ^= 1;

    if (cio[cid].seqbit) {
        cqe->subdevice |= CIO_SEQBIT;
    }

    pwrite_h(cqp,     cqe->byte_count);
    pwrite_b(cqp + 2, cqe->subdevice);
    pwrite_b(cqp + 3, cqe->opcode);
    pwrite_w(cqp + 4, cqe->address);
    pwrite_w(cqp + 8, cqe->app_data);
}

/* Write an entry into the Completion Queue */
void cio_cqueue(uint8 cid, cio_entry *cqe)
{
    uint32 cqp, top;
    uint16 lp;

    /* Get the physical address of the completion queue
     * in main memory */
    cqp = cio[cid].cqp;

    /* Get the physical address of the first entry in
     * the completion queue */
    top = cqp + QUE_OFFSET;

    /* Get the load pointer. This is a 16-bit absolute offset
     * from the top of the queue to the start of the entry. */
    lp = pread_h(cqp + LOAD_OFFSET);

    /* Load the entry at the supplied address */
    pwrite_h(top + lp,     cqe->byte_count);
    pwrite_b(top + lp + 2, cqe->subdevice);
    pwrite_b(top + lp + 3, cqe->opcode);
    pwrite_w(top + lp + 4, cqe->address);
    pwrite_w(top + lp + 8, cqe->app_data);

    /* Increment the load pointer to the next queue location.
     * If we go past the end of the queue, wrap around to the
     * start of the queue */
    if (cio[cid].cqs > 0) {
        lp = (lp + QUE_E_SIZE) % (QUE_E_SIZE * cio[cid].cqs);

        /* Store it back to the correct location */
        pwrite_h(cqp + LOAD_OFFSET, lp);
    } else {
        sim_debug(IO_D_MSG, &cpu_dev,
                  "[%08x] [cio_cqueue] ERROR! Completion Queue Size is 0!",
                  R[NUM_PC]);
    }

}

/* Retrieve an entry from the Request Queue */
void cio_rqueue(uint8 cid, cio_entry *cqe)
{
    uint32 rqp, top, i;
    uint16 ulp;

    /* Get the physical address of the request queue in main memory */
    rqp = cio[cid].rqp + 12; /* Skip past the Express Queue Entry */

    /* Scan each queue until we find one with a command in it. */
    for (i = 0; i < cio[cid].no_rque; i++) {
        /* Get the physical address of the first entry in the request
         * queue */
        top = rqp + 4;

        /* Check to see what we've got in the queue. */
        ulp = pread_h(rqp + 2);

        cqe->opcode = pread_b(top + ulp + 3);

        if (cqe->opcode > 0) {
            break;
        }

        rqp += 4 + (12 * cio[cid].rqs);
    }

    if (i >= cio[cid].no_rque) {
        sim_debug(IO_D_MSG, &cpu_dev,
                  "[%08x] [cio_rque] FAILURE! NO MORE QUEUES TO EXAMINE.\n",
                  R[NUM_PC]);
        return;
    }

    /* Retrieve the entry at the supplied address */
    cqe->byte_count = pread_h(top + ulp);
    cqe->subdevice  = pread_b(top + ulp + 2);
    cqe->address    = pread_w(top + ulp + 4);
    cqe->app_data   = pread_w(top + ulp + 8);

    dump_entry("REQUEST", cqe);

    /* Increment the unload pointer to the next queue location.  If we
     * go past the end of the queue, wrap around to the start of the
     * queue */
    if (cio[cid].rqs > 0) {
        ulp = (ulp + QUE_E_SIZE) % (QUE_E_SIZE * cio[cid].rqs);

        /* Store it back to the correct location */
        pwrite_h(rqp + 2, ulp);
    } else {
        sim_debug(IO_D_MSG, &cpu_dev,
                  "[%08x] [cio_rqueue] ERROR! Request Queue Size is 0!",
                  R[NUM_PC]);
    }
}

uint32 io_read(uint32 pa, size_t size)
{
    struct iolink *p;
    uint8 cid, reg, data;

    /* Special devices */
    if (pa == MEMSIZE_REG) {

        /* It appears that the following values map to memory sizes:
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

    /* IO Board Area - Unimplemented */
    if (pa >= CIO_BOTTOM && pa < CIO_TOP) {
        cid = CID(pa);
        reg = pa - CADDR(cid);

        if (cio[cid].id == 0) {
            /* Nothing lives here */
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
            switch(cio[cid].cmdbits) {
            case 0x00: /* We've never seen an INT0 or INT1 */
            case 0x01: /* We've seen an INT0 but not an INT1. */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) ID\n",
                          R[NUM_PC], cid);
                /* Return the correct byte of our board ID */
                if (reg == IOF_ID) {
                    data = (cio[cid].id >> 8) & 0xff;
                } else {
                    data = (cio[cid].id & 0xff);
                }
                break;
            case 0x02: /* We've seen an INT1 but not an INT0. Time to sysgen */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) SYSGEN\n",
                          R[NUM_PC], cid);
                cio_sysgen(cid);
                data = cio[cid].ivec;
                break;
            case 0x03: /* We've already sysgen'ed */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) EXPRESS JOB\n",
                          R[NUM_PC], cid);
                cio[cid].exp_handler(cid);
                data = cio[cid].ivec;
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) ERROR IN STATE MACHINE cmdbits=%02x\n",
                          R[NUM_PC], cid, cio[cid].cmdbits);
                data = 0;
                break;
            }

            /* Record that we've seen an INT0 */
            cio[cid].cmdbits |= CIO_INT0;
            return data;
        case IOF_CTRL:
            switch(cio[cid].cmdbits) {
            case 0x00: /* We've never seen an INT0 or INT1 */
            case 0x02: /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                break;
            case 0x01: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) SYSGEN\n",
                          R[NUM_PC], cid);
                cio_sysgen(cid);
                break;
            case 0x03: /* We've already sysgen'ed */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) FULL\n",
                          R[NUM_PC], cid);
                cio[cid].full_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT1) ERROR IN STATE MACHINE cmdbits=%02x\n",
                          R[NUM_PC], cid, cio[cid].cmdbits);
                break;
            }

            /* Record that we've seen an INT1 */
            cio[cid].cmdbits |= CIO_INT1;
            return 0; /* Data returned is arbitrary */
        case IOF_STAT:
            sim_debug(IO_D_MSG, &cpu_dev,
                      "[READ] [%08x] (%d RESET)\n",
                      R[NUM_PC], cid);
            cio[cid].cmdbits = 0;
            return 0; /* Data returned is arbitrary */
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
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
    sim_debug(IO_D_MSG, &cpu_dev,
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
            switch(cio[cid].cmdbits) {
            case 0x00: /* We've never seen an INT0 or INT1 */
            case 0x01: /* We've seen an INT0 but not an INT1. */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT0) ID\n",
                          R[NUM_PC], cid);
                break;
            case 0x02: /* We've seen an INT1 but not an INT0. Time to sysgen */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) SYSGEN\n",
                          R[NUM_PC], cid);
                cio_sysgen(cid);
                break;
            case 0x03: /* We've already sysgen'ed */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) EXPRESS JOB\n",
                          R[NUM_PC], cid);
                cio[cid].exp_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[READ] [%08x] (%d INT0) ERROR IN STATE MACHINE cmdbits=%02x\n",
                          R[NUM_PC], cid, cio[cid].cmdbits);
                break;
            }

            /* Record that we've seen an INT0 */
            cio[cid].cmdbits |= CIO_INT0;
            return;
        case IOF_CTRL:
            switch(cio[cid].cmdbits) {
            case 0x00: /* We've never seen an INT0 or INT1 */
            case 0x02: /* We've seen an INT1 but not an INT0 */
                /* There's nothing to do in this instance */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1)\n",
                          R[NUM_PC], cid);
                break;
            case 0x01: /* We've seen an INT0 but not an INT1. Time to sysgen */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) SYSGEN\n",
                          R[NUM_PC], cid);
                cio_sysgen(cid);
                break;
            case 0x03: /* We've already sysgen'ed */
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) FULL\n",
                          R[NUM_PC], cid);
                cio[cid].full_handler(cid);
                break;
            default:
                /* This should never happen */
                stop_reason = STOP_ERR;
                sim_debug(IO_D_MSG, &cpu_dev,
                          "[WRITE] [%08x] (%d INT1) ERROR IN STATE MACHINE cmdbits=%02x\n",
                          R[NUM_PC], cid, cio[cid].cmdbits);
                break;
            }

            /* Record that we've seen an INT1 */
            cio[cid].cmdbits |= CIO_INT1;
            return;
        case IOF_STAT:
            sim_debug(IO_D_MSG, &cpu_dev,
                      "[WRITE] [%08x] (%d RESET)\n",
                      R[NUM_PC], cid);
            cio[cid].cmdbits = 0;
            return;
        default:
            /* We should never reach here, but if we do, there's
             * nothing listening. */
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
    sim_debug(IO_D_MSG, &cpu_dev,
              "[%08x] [io_write] ADDR=%08x: No device found.\n",
              R[NUM_PC], pa);
    csr_data |= CSRTIMO;
    cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
}


/* For debugging only */
void dump_entry(CONST char *type, cio_entry *entry)
{
    sim_debug(IO_D_MSG, &cpu_dev,
              "*** %s ENTRY: byte_count=%04x, subdevice=%02x,\n"
              "    opcode=%d, address=%08x, app_data=%08x\n",
              type, entry->byte_count, entry->subdevice,
              entry->opcode, entry->address, entry->app_data);
}
