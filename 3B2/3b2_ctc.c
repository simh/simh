/* 3b2_ctc.c: CM195H 23MB Cartridge Tape Controller CIO Card

   Copyright (c) 2018-2022, Seth J. Morabito

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

#include "3b2_ctc.h"

#include "sim_disk.h"

#include "3b2_io.h"
#include "3b2_mem.h"

#define CTQRESIZE     20
#define CTQCESIZE     16

#define DELAY_SYSGEN  2500
#define DELAY_FMT     1000000
#define DELAY_RW      10000
#define DELAY_OPEN    2500
#define DELAY_CLOSE   2500
#define DELAY_CONFIG  2500
#define DELAY_DLM     1000
#define DELAY_ULM     1000
#define DELAY_FCF     1000
#define DELAY_DOS     1000
#define DELAY_DSD     1000
#define DELAY_UNK     1000
#define DELAY_CATCHUP 10000

#define CTC_DIAG_CRC1 0xa4a5752f
#define CTC_DIAG_CRC2 0xd3d20eb3
#define CTC_DIAG_CRC3 0x0f387ce3  /* Used by SVR 2.0.5 */

#define TAPE_DEV      0    /* CTAPE device */
#define XMF_DEV       1    /* XM Floppy device */

#define VTOC_BLOCK    0

static uint8   int_slot;            /* Interrupting card ID   */
static uint8   int_subdev;          /* Interrupting subdevice */
static t_bool  ctc_conf = FALSE;    /* Has a CTC card been configured? */
static uint32  ctc_crc;             /* CRC32 of downloaded memory */

struct partition vtoc_table[VTOC_PART] = {
    { 2, 0, 5272,  8928  },   /* 00 */
    { 3, 1, 126,   5146  },   /* 01 */
    { 4, 0, 14200, 31341 },   /* 02 */
    { 0, 0, 2,     45539 },   /* 03 */
    { 0, 1, 0,     0     },   /* 04 */
    { 0, 1, 0,     0     },   /* 05 */
    { 5, 1, 0,     45541 },   /* 06 */
    { 1, 1, 0,     126   },   /* 07 */
    { 0, 1, 0,     0     },   /* 08 */
    { 0, 1, 0,     0     },   /* 09 */
    { 0, 1, 0,     0     },   /* 10 */
    { 0, 1, 0,     0     },   /* 11 */
    { 0, 1, 0,     0     },   /* 12 */
    { 0, 1, 0,     0     },   /* 13 */
    { 0, 1, 0,     0     },   /* 14 */
    { 0, 1, 0,     0     }    /* 15 */
};

/* State. Although we technically have two devices (tape and floppy),
 * only the tape drive is supported at this time. */

CTC_STATE ctc_state[2];

UNIT ctc_unit = {
    UDATA (&ctc_svc, UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|
           UNIT_ROABLE|UNIT_BINK, CTC_CAPACITY)
};

MTAB ctc_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED",
        &set_writelock, &show_writelock,   NULL, "Write enable tape drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED",
        &set_writelock, NULL,   NULL, "Write lock tape drive" },
    { 0 }
};

static DEBTAB ctc_debug[] = {
    { "IO",    IO_DBG,        "I/O"           },
    { "TRACE", TRACE_DBG,     "Call Trace"    },
    { NULL }
};

DEVICE ctc_dev = {
    "CTC",                          /* name */
    &ctc_unit,                      /* units */
    NULL,                           /* registers */
    ctc_mod,                        /* modifiers */
    1,                              /* #units */
    16,                             /* address radix */
    32,                             /* address width */
    1,                              /* address incr. */
    16,                             /* data radix */
    8,                              /* data width */
    NULL,                           /* examine routine */
    NULL,                           /* deposit routine */
    &ctc_reset,                     /* reset routine */
    NULL,                           /* boot routine */
    &ctc_attach,                    /* attach routine */
    &ctc_detach,                    /* detach routine */
    NULL,                           /* context */
    DEV_DISABLE|DEV_DIS|DEV_DEBUG|DEV_SECTORS,    /* flags */
    0,                              /* debug control flags */
    ctc_debug,                      /* debug flag names */
    NULL,                           /* memory size change */
    NULL,                           /* logical name */
    NULL,                           /* help routine */
    NULL,                           /* attach help routine */
    NULL,                           /* help context */
    NULL,                           /* device description */
};

static void cio_irq(uint8 slot, uint8 dev, int32 delay)
{
    int_slot = slot;
    int_subdev = dev & 0x3f;
    sim_activate_after(&ctc_unit, delay);
}

/*
 * Write a VTOC and pdinfo to the tape file
 */
static t_stat ctc_write_vtoc(struct vtoc *vtoc, struct pdinfo *pdinfo, uint32 maxpass)
{
    uint8 buf[PD_BYTES];
    uint32 wr, offset;

    memcpy(buf, vtoc, sizeof(struct vtoc));
    offset = sizeof(struct vtoc);
    memcpy(buf + offset, pdinfo, sizeof(struct pdinfo));
    offset += sizeof(struct pdinfo);
    memcpy(buf + offset, &maxpass, sizeof(uint32));

    return sim_disk_wrsect(&ctc_unit, VTOC_BLOCK, buf, &wr, 1);
}

/*
 * Load a VTOC and pdinfo from the tape file
 */
static t_stat ctc_read_vtoc(struct vtoc *vtoc, struct pdinfo *pdinfo, uint32 *maxpass)
{
    uint8 buf[PD_BYTES];
    uint32 wr, offset;
    t_stat result;

    result = sim_disk_rdsect(&ctc_unit, VTOC_BLOCK, buf, &wr, 1);

    if (result != SCPE_OK) {
        return result;
    }

    memcpy(vtoc, buf, sizeof(struct vtoc));
    offset = sizeof(struct vtoc);
    memcpy(pdinfo, buf + offset, sizeof(struct pdinfo));
    offset += sizeof(struct pdinfo);
    memcpy(maxpass, buf + offset, sizeof(uint32));

    return result;
}

/*
 * Update the host's in-memory copy of the VTOC and pdinfo
 */
static void ctc_update_vtoc(uint32 maxpass,
                            uint32 vtoc_addr, uint32 pdinfo_addr,
                            struct vtoc *vtoc, struct pdinfo *pdinfo)
{
    uint32 i;

    pwrite_w(vtoc_addr + 12, VTOC_VALID, BUS_PER);
    pwrite_w(vtoc_addr + 16, vtoc->version, BUS_PER);
    for (i = 0; i < 8; i++) {
        pwrite_b(vtoc_addr + 20 + i, (uint8)(vtoc->volume[i]), BUS_PER);
    }
    pwrite_h(vtoc_addr + 28, vtoc->sectorsz, BUS_PER);
    pwrite_h(vtoc_addr + 30, vtoc->nparts, BUS_PER);

    for (i = 0; i < VTOC_PART; i++) {
        pwrite_h(vtoc_addr + 72 + (i * 12) + 0, vtoc_table[i].id, BUS_PER);
        pwrite_h(vtoc_addr + 72 + (i * 12) + 2, vtoc_table[i].flag, BUS_PER);
        pwrite_w(vtoc_addr + 72 + (i * 12) + 4, vtoc_table[i].sstart, BUS_PER);
        pwrite_w(vtoc_addr + 72 + (i * 12) + 8, vtoc_table[i].ssize, BUS_PER);
    }

    /* Write the pdinfo */
    pwrite_w(pdinfo_addr, pdinfo->driveid, BUS_PER);
    pwrite_w(pdinfo_addr + 4, pdinfo->sanity, BUS_PER);
    pwrite_w(pdinfo_addr + 8, pdinfo->version, BUS_PER);
    for (i = 0; i < 12; i++) {
        pwrite_b(pdinfo_addr + 12 + i, pdinfo->serial[i], BUS_PER);
    }
    pwrite_w(pdinfo_addr + 24, pdinfo->cyls, BUS_PER);
    pwrite_w(pdinfo_addr + 28, pdinfo->tracks, BUS_PER);
    pwrite_w(pdinfo_addr + 32, pdinfo->sectors, BUS_PER);
    pwrite_w(pdinfo_addr + 36, pdinfo->bytes, BUS_PER);
    pwrite_w(pdinfo_addr + 40, pdinfo->logicalst, BUS_PER);
    pwrite_w(pdinfo_addr + 44, pdinfo->errlogst, BUS_PER);
    pwrite_w(pdinfo_addr + 48, pdinfo->errlogsz, BUS_PER);
    pwrite_w(pdinfo_addr + 52, pdinfo->mfgst, BUS_PER);
    pwrite_w(pdinfo_addr + 56, pdinfo->mfgsz, BUS_PER);
    pwrite_w(pdinfo_addr + 60, pdinfo->defectst, BUS_PER);
    pwrite_w(pdinfo_addr + 64, pdinfo->defectsz, BUS_PER);
    pwrite_w(pdinfo_addr + 68, pdinfo->relno, BUS_PER);
    pwrite_w(pdinfo_addr + 72, pdinfo->relst, BUS_PER);
    pwrite_w(pdinfo_addr + 76, pdinfo->relsz, BUS_PER);
    pwrite_w(pdinfo_addr + 80, pdinfo->relnext, BUS_PER);

    /* Now something horrible happens. We sneak RIGHT off the end of
     * the pdinfo struct and reach deep into the pdsector struct that
     * it is part of. */

    pwrite_w(pdinfo_addr + 128, maxpass, BUS_PER);
}

/*
 * Handle a single request taken from the Request Queue.
 *
 * Note that the driver stuffs parameters into various different
 * fields of the Request Queue entry seemingly at random, and also
 * expects response parameters to be placed in specific fields of the
 * Completion Queue entry. It can be confusing to follow.
 */
static void ctc_cmd(uint8 slot,
                    cio_entry *rqe, uint8 *rapp_data,
                    cio_entry *cqe, uint8 *capp_data)
{
    uint32 vtoc_addr, pdinfo_addr, ctjob_addr;
    uint32 maxpass, blkno, delay, last_byte;
    uint8  dev, c;
    uint8  sec_buf[VTOC_SECSZ];
    int32  b, i, j;
    int32 block_count, read_bytes, remainder, dest;
    t_seccnt secrw = 0;
    struct vtoc vtoc = {{0}};
    struct pdinfo pdinfo = {0};
    t_stat result;

    uint32 lba;   /* Logical Block Address */

    maxpass = 0;
    dev = rqe->subdevice & 1;  /* Tape or Floppy device */

    capp_data[7] = rqe->opcode;
    cqe->subdevice = rqe->subdevice;

    switch(rqe->opcode) {
    case CIO_DLM:
        for (i = 0; i < rqe->byte_count; i++) {
            ctc_crc = cio_crc32_shift(ctc_crc, pread_b(rqe->address + i, BUS_PER));
        }
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CIO Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x (CRC=%08x)\n",
                  rqe->byte_count, rqe->address,
                  rqe->address, rqe->subdevice, ctc_crc);
        delay = DELAY_DLM;
        cqe->address = rqe->address + rqe->byte_count;
        cqe->opcode = CTC_SUCCESS;
        break;
    case CIO_ULM:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CIO Upload Memory: return opcode 0\n");
        delay = DELAY_ULM;
        cqe->opcode = CTC_SUCCESS;
        break;
    case CIO_FCF:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CIO Force Function Call (CRC=%08x)\n", ctc_crc);
        delay = DELAY_FCF;

        /* If the currently running program is a diagnostic program,
         * we are expected to write results into memory at address
         * 0x200f000 */
        if (ctc_crc == CTC_DIAG_CRC1 ||
            ctc_crc == CTC_DIAG_CRC2 ||
            ctc_crc == CTC_DIAG_CRC3) {
            pwrite_h(0x200f000, 0x1, BUS_PER);   /* Test success */
            pwrite_h(0x200f002, 0x0, BUS_PER);   /* Test Number */
            pwrite_h(0x200f004, 0x0, BUS_PER);   /* Actual */
            pwrite_h(0x200f006, 0x0, BUS_PER);   /* Expected */
            pwrite_b(0x200f008, 0x1, BUS_PER);   /* Success flag again */
        }

        /* An interesting (?) side-effect of FORCE FUNCTION CALL is
         * that it resets the card state such that a new SYSGEN is
         * required in order for new commands to work. In fact, an
         * INT0/INT1 combo _without_ a RESET can sysgen the board. So,
         * we reset the command bits here. */
        cio[slot].sysgen_s = 0;
        cqe->opcode = CTC_SUCCESS;
        break;
    case CIO_DOS:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CIO_DOS (%d)\n",
                  rqe->opcode);
        delay = DELAY_DOS;
        cqe->opcode = CTC_SUCCESS;
        break;
    case CIO_DSD:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_DSD (%d)\n",
                  rqe->opcode);
        delay = DELAY_DSD;
        /* Write subdevice information to the host. */
        pwrite_h(rqe->address, CTC_NUM_SD, BUS_PER);
        pwrite_h(rqe->address + 2, CTC_SD_FT25, BUS_PER);
        pwrite_h(rqe->address + 4, CTC_SD_FD5, BUS_PER);
        cqe->opcode = CTC_SUCCESS;
        break;
    case CTC_FORMAT:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_FORMAT (%d)\n",
                  rqe->opcode);

        delay = DELAY_FMT;

        /* FORMAT stores the job pointer in the jio_start field of the
         * completion queue entry's application data */
        capp_data[0] = rapp_data[4];
        capp_data[1] = rapp_data[5];
        capp_data[2] = rapp_data[6];
        capp_data[3] = rapp_data[7];

        if (dev == XMF_DEV) {
            cqe->opcode = CTC_NOTREADY;
            break;
        }

        if ((ctc_unit.flags & UNIT_ATT) == 0) {
            cqe->opcode = CTC_NOMEDIA;
            break;
        }

        if (ctc_unit.flags & UNIT_WPRT) {
            cqe->opcode = CTC_RDONLY;
            break;
        }

        /* Write a valid VTOC and pdinfo to the tape */

        vtoc.sanity   = VTOC_VALID;
        vtoc.version  = 1;
        strcpy((char *)vtoc.volume, "ctctape");
        vtoc.sectorsz = PD_BYTES;
        vtoc.nparts   = VTOC_PART;

        pdinfo.driveid = PD_DRIVEID;
        pdinfo.sanity = PD_VALID;
        pdinfo.version = 0;
        memset(pdinfo.serial, 0, 12);
        pdinfo.cyls = PD_CYLS;
        pdinfo.tracks = PD_TRACKS;
        pdinfo.sectors = PD_SECTORS;
        pdinfo.bytes = PD_BYTES;
        pdinfo.logicalst = PD_LOGICALST;
        pdinfo.errlogst = 0xffffffff;
        pdinfo.errlogsz = 0xffffffff;
        pdinfo.mfgst = 0xffffffff;
        pdinfo.mfgsz = 0xffffffff;
        pdinfo.defectst = 0xffffffff;
        pdinfo.defectsz = 0xffffffff;
        pdinfo.relno = 0xffffffff;
        pdinfo.relst = 0xffffffff;
        pdinfo.relsz = 0xffffffff;
        pdinfo.relnext = 0xffffffff;

        maxpass = rqe->address;

        ctc_write_vtoc(&vtoc, &pdinfo, maxpass);

        cqe->opcode = CTC_SUCCESS;

        /* The address field holds the total amount of time (in 25ms
         * chunks) used during this format session.  We'll fudge and
         * say 1 minute for formatting. */
        cqe->address = 2400;

        break;
    case CTC_OPEN:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_OPEN (%d)\n",
                  rqe->opcode);

        delay = DELAY_OPEN;

        ctc_state[dev].time = 0;  /* Opening always resets session time to 0 */
        ctc_state[dev].bytnum = 0;

        vtoc_addr = rqe->address;
        pdinfo_addr = ATOW(rapp_data, 4);
        ctjob_addr = ATOW(rapp_data, 8);

        /* For OPEN commands, the Completion Queue Entry's address
         * field contains a pointer to the ctjobstat. */
        cqe->address = ctjob_addr;

        if (dev == XMF_DEV) {
            cqe->opcode = CTC_NOTREADY;
            break;
        }

        if ((ctc_unit.flags & UNIT_ATT) == 0) {
            cqe->opcode = CTC_NOMEDIA;
            break;
        }

        /* Load the vtoc, pdinfo, and maxpass from the tape */
        ctc_read_vtoc(&vtoc, &pdinfo, &maxpass);

        ctc_update_vtoc(maxpass, vtoc_addr, pdinfo_addr, &vtoc, &pdinfo);
        cqe->opcode = CTC_SUCCESS;
        break;
    case CTC_CLOSE:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_CLOSE (%d)\n",
                  rqe->opcode);

        delay = DELAY_CLOSE;

        /* The Request Queue Entry's address field contains the
         * ctjobstat pointer, which the driver will want to find in
         * the first word of our Completion Queue Entry's application
         * data. This must be in place whether we have media attached
         * or not. */
        capp_data[3] = rqe->address & 0xff;
        capp_data[2] = (rqe->address & 0xff00) >> 8;
        capp_data[1] = (rqe->address & 0xff0000) >> 16;
        capp_data[0] = (rqe->address & 0xff000000) >> 24;

        /* The Completion Queue Entry's address field holds the total
         * tape time used in this session. */
        cqe->address = ctc_state[dev].time;
        cqe->opcode = CTC_SUCCESS;

        break;
    case CTC_WRITE:
    case CTC_VWRITE:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_WRITE or CTC_VWRITE (%d)\n",
                  rqe->opcode);

        delay = DELAY_RW;

        cqe->byte_count = rqe->byte_count;
        cqe->subdevice = rqe->subdevice;
        cqe->address = ATOW(rapp_data, 4);

        if (dev == XMF_DEV) {
            cqe->opcode = CTC_NOTREADY;
            break;
        }

        if ((ctc_unit.flags & UNIT_ATT) == 0) {
            cqe->opcode = CTC_NOMEDIA;
            break;
        }

        if (ctc_unit.flags & UNIT_WPRT) {
            cqe->opcode = CTC_RDONLY;
            break;
        }

        blkno = ATOW(rapp_data, 0);

        for (b = 0; b < rqe->byte_count / VTOC_SECSZ; b++) {
            ctc_state[dev].time += 10;
            for (j = 0; j < VTOC_SECSZ; j++) {
                /* Fill the buffer */
                sec_buf[j] = pread_b(rqe->address + (b * VTOC_SECSZ) + j, BUS_PER);
            }
            lba = blkno + b;
            result = sim_disk_wrsect(&ctc_unit, lba, sec_buf, &secrw, 1);
            if (result == SCPE_OK) {
                sim_debug(TRACE_DBG, &ctc_dev,
                          "[ctc_cmd] ... CTC_WRITE: 512 bytes at block %d (0x%x)\n",
                          lba, lba);
                cqe->opcode = CTC_SUCCESS;
            } else {
                cqe->opcode = CTC_RWERROR;
                break;
            }
        }

        break;
    case CTC_READ:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_READ (%d)\n",
                  rqe->opcode);
        delay = DELAY_RW;
        cqe->byte_count = rqe->byte_count;
        cqe->subdevice = rqe->subdevice;
        cqe->address = ATOW(rapp_data, 4);
        dest = rqe->address;

        if (dev == XMF_DEV) {
            cqe->opcode = CTC_NOTREADY;
            break;
        }

        if ((ctc_unit.flags & UNIT_ATT) == 0) {
            cqe->opcode = CTC_NOMEDIA;
            break;
        }

        /*
         * This read routine supports both streaming and block
         * oriented modes.
         *
         * Read requests from the host give a block number, and a
         * number of bytes to read. In streaming mode, however, there
         * is no requirement that the number of bytes to read has to
         * be block-aligned, so we must support reading an arbitrary
         * number of bytes from the tape stream and remembering the
         * current position in the byte stream.
         *
         */

        /* The block number to begin reading from is supplied in the
         * request queue entry's APP_DATA field. */
        blkno = ATOW(rapp_data, 0);

        /* Since we may start reading from the data stream at an
         * arbitrary location, we compute the offset of the last byte
         * to be read, and use that to figure out how many bytes will
         * be left over to read from an "extra" block */
        last_byte = ctc_state[dev].bytnum + rqe->byte_count;
        remainder = last_byte % VTOC_SECSZ;

        /* The number of blocks we have to read in total is computed
         * by looking at the byte count, PLUS any remainder that will
         * be left after crossing a block boundary */
        block_count = rqe->byte_count / VTOC_SECSZ;
        if (((rqe->byte_count % VTOC_SECSZ) > 0 || remainder > 0)) {
            block_count++;
        }

        /* Now step over each block, and start reading from the
         * necessary location. */
        for (b = 0; b < block_count; b++) {
            uint32 start_byte;
            /* Add some read time to the read time counter */
            ctc_state[dev].time += 10;
            start_byte = ctc_state[dev].bytnum % VTOC_SECSZ;
            lba = blkno + b;
            result = sim_disk_rdsect(&ctc_unit, lba, sec_buf, &secrw, 1);
            if (result == SCPE_OK) {
                /* If this is the last "extra" block, we will only
                 * read the remainder of bytes from it. Otherwise, we
                 * need to consume the whole block. */
                if (b == (block_count - 1) && remainder > 0) {
                    read_bytes = remainder;
                } else {
                    read_bytes = VTOC_SECSZ - start_byte;
                }
                for (j = 0; j < read_bytes; j++) {
                    uint32 offset;
                    /* Drain the buffer */
                    if (b == 0 && (j + start_byte) < VTOC_SECSZ) {
                        /* This is a partial read of the first block,
                         * continuing to read from a previous partial
                         * block read. */
                        offset = j + start_byte;
                    } else {
                        offset = j;
                    }
                    c = sec_buf[offset];
                    pwrite_b(dest++, c, BUS_PER);
                    ctc_state[dev].bytnum++;
                }
            } else {
                sim_debug(TRACE_DBG, &ctc_dev,
                          "[ctc_cmd] Error reading sector at address %d. Giving up\n", lba);
                break;
            }
        }

        if (result == SCPE_OK) {
            cqe->opcode = CTC_SUCCESS;
        } else {
            cqe->opcode = CTC_RWERROR;
        }

        break;
    case CTC_CONFIG:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_CONFIG (%d)\n",
                  rqe->opcode);
        delay = DELAY_CONFIG;
        cqe->opcode = CTC_SUCCESS;
        break;
    default:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] UNHANDLED OP: %d (0x%02x)\n",
                  rqe->opcode, rqe->opcode);
        delay = DELAY_UNK;
        cqe->opcode = CTC_HWERROR;
        break;
    }

    cio_irq(slot, rqe->subdevice, delay);
}

void ctc_sysgen(uint8 slot)
{
    cio_entry cqe = {0};
    uint8 rapp_data[12] = {0};

    ctc_crc = 0;

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen] Handling Sysgen.\n");
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    rqp=%08x\n", cio[slot].rqp);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    cqp=%08x\n", cio[slot].cqp);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    rqs=%d\n", cio[slot].rqs);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    cqs=%d\n", cio[slot].cqs);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    ivec=%d\n", cio[slot].ivec);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    no_rque=%d\n", cio[slot].no_rque);

    cqe.opcode = 3; /* Sysgen success! */

    cio_cexpress(slot, CTQCESIZE, &cqe, rapp_data);
    cio_cqueue(slot, CIO_STAT, CTQCESIZE, &cqe, rapp_data);

    int_slot = slot;
    sim_activate_after(&ctc_unit, DELAY_SYSGEN);
}

void ctc_express(uint8 slot)
{
    cio_entry rqe, cqe;
    uint8 rapp_data[12] = {0};
    uint8 capp_data[8] = {0};

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_express] Handling Express Request\n");

    cio_rexpress(slot, CTQRESIZE, &rqe, rapp_data);
    ctc_cmd(slot, &rqe, rapp_data, &cqe, capp_data);

    cio_cexpress(slot, CTQCESIZE, &cqe, capp_data);
}

void ctc_full(uint8 slot)
{
    cio_entry rqe, cqe;
    uint8 rapp_data[12] = {0};
    uint8 capp_data[8] = {0};

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_full] Handling Full Request\n");

    while (cio_cqueue_avail(slot, CTQCESIZE) &&
           cio_rqueue(slot, TAPE_DEV, CTQRESIZE, &rqe, rapp_data) == SCPE_OK) {
        ctc_cmd(slot, &rqe, rapp_data, &cqe, capp_data);
    }
    cio_cqueue(slot, CIO_STAT, CTQCESIZE, &cqe, capp_data);
}

t_stat ctc_reset(DEVICE *dptr)
{
    uint8 slot;
    t_stat r;

    ctc_crc = 0;

    memset(ctc_state, 0, 2 * sizeof(CTC_STATE));

    if (dptr->flags & DEV_DIS) {
        cio_remove_all(CTC_ID);
        ctc_conf = FALSE;
        return SCPE_OK;
    }

    if (!ctc_conf) {
        r = cio_install(CTC_ID, "CTC", CTC_IPL,
                        &ctc_express, &ctc_full, &ctc_sysgen, NULL,
                        &slot);
        if (r != SCPE_OK) {
            return r;
        }
        ctc_conf = TRUE;
    }

    return SCPE_OK;
}

t_stat ctc_svc(UNIT *uptr)
{
    uint16 lp, ulp;

    if (cio[int_slot].ivec > 0) {
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[cio_svc] IRQ for board %d (VEC=%d)\n",
                  int_slot, cio[int_slot].ivec);
        CIO_SET_INT(int_slot);
    }

    /* Check to see if the completion queue has more work in it. We
     * need to schedule an interrupt for each job if we've fallen
     * behind (this should be rare) */
    lp = cio_c_lp(int_slot, CTQCESIZE);
    ulp = cio_c_ulp(int_slot, CTQCESIZE);

    if ((ulp + CTQCESIZE) % (CTQCESIZE * cio[int_slot].cqs) != lp) {
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[cio_svc] Completion queue has fallen behind (lp=%04x ulp=%04x)\n",
                  lp, ulp);
        /* Schedule a catch-up interrupt */
        sim_activate_abs(&ctc_unit, DELAY_CATCHUP);
    }

    return SCPE_OK;
}

t_stat ctc_attach(UNIT *uptr, CONST char *cptr)
{
    return sim_disk_attach(uptr, cptr, VTOC_SECSZ, 1, TRUE, 0, "CIPHER23", 0, 0);
}

t_stat ctc_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}
