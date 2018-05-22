/* 3b2_ctc.c: AT&T 3B2 Model 400 "CTC" feature card

   Copyright (c) 2018, Seth J. Morabito

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

extern CIO_STATE cio[CIO_SLOTS];
extern UNIT cio_unit;

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

#define TAPE_DEV      0    /* CTAPE device */
#define XMF_DEV       1    /* XM Floppy device */

#define VTOC_BLOCK    0

#define ATOW(arr,i)  ((uint32)arr[i+3] + ((uint32)arr[i+2] << 8) +      \
                      ((uint32)arr[i+1] << 16) + ((uint32)arr[i] << 24))

static uint8   int_cid;             /* Interrupting card ID   */
static uint8   int_subdev;          /* Interrupting subdevice */
static t_bool  ctc_conf = FALSE;    /* Has a CTC card been configured? */

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
    { UNIT_WLK,         0, "write enabled", "WRITEENABLED",
      NULL, NULL, NULL, "Write enabled tape drive" },
    { UNIT_WLK,  UNIT_WLK, "write locked", "LOCKED",
      NULL, NULL, NULL, "Write lock tape drive" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "RQUEUE=n", NULL,
      NULL, &ctc_show_rqueue, NULL, "Display Request Queue for card n" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "CQUEUE=n", NULL,
      NULL, &ctc_show_cqueue, NULL, "Display Completion Queue for card n" },
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

static void cio_irq(uint8 cid, uint8 dev, int32 delay)
{
    int_cid = cid;
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

    pwrite_w(vtoc_addr + 12, VTOC_VALID);
    pwrite_w(vtoc_addr + 16, vtoc->version);
    for (i = 0; i < 8; i++) {
        pwrite_b(vtoc_addr + 20 + i, (uint8)(vtoc->volume[i]));
    }
    pwrite_h(vtoc_addr + 28, vtoc->sectorsz);
    pwrite_h(vtoc_addr + 30, vtoc->nparts);

    for (i = 0; i < VTOC_PART; i++) {
        pwrite_h(vtoc_addr + 72 + (i * 12) + 0, vtoc_table[i].id);
        pwrite_h(vtoc_addr + 72 + (i * 12) + 2, vtoc_table[i].flag);
        pwrite_w(vtoc_addr + 72 + (i * 12) + 4, vtoc_table[i].sstart);
        pwrite_w(vtoc_addr + 72 + (i * 12) + 8, vtoc_table[i].ssize);
    }

    /* Write the pdinfo */
    pwrite_w(pdinfo_addr, pdinfo->driveid);
    pwrite_w(pdinfo_addr + 4, pdinfo->sanity);
    pwrite_w(pdinfo_addr + 8, pdinfo->version);
    for (i = 0; i < 12; i++) {
        pwrite_b(pdinfo_addr + 12 + i, pdinfo->serial[i]);
    }
    pwrite_w(pdinfo_addr + 24, pdinfo->cyls);
    pwrite_w(pdinfo_addr + 28, pdinfo->tracks);
    pwrite_w(pdinfo_addr + 32, pdinfo->sectors);
    pwrite_w(pdinfo_addr + 36, pdinfo->bytes);
    pwrite_w(pdinfo_addr + 40, pdinfo->logicalst);
    pwrite_w(pdinfo_addr + 44, pdinfo->errlogst);
    pwrite_w(pdinfo_addr + 48, pdinfo->errlogsz);
    pwrite_w(pdinfo_addr + 52, pdinfo->mfgst);
    pwrite_w(pdinfo_addr + 56, pdinfo->mfgsz);
    pwrite_w(pdinfo_addr + 60, pdinfo->defectst);
    pwrite_w(pdinfo_addr + 64, pdinfo->defectsz);
    pwrite_w(pdinfo_addr + 68, pdinfo->relno);
    pwrite_w(pdinfo_addr + 72, pdinfo->relst);
    pwrite_w(pdinfo_addr + 76, pdinfo->relsz);
    pwrite_w(pdinfo_addr + 80, pdinfo->relnext);

    /* Now something horrible happens. We sneak RIGHT off the end of
     * the pdinfo struct and reach deep into the pdsector struct that
     * it is part of. */

    pwrite_w(pdinfo_addr + 128, maxpass);
}

/*
 * Handle a single request taken from the Request Queue.
 *
 * Note that the driver stuffs parameters into various different
 * fields of the Request Queue entry seemingly at random, and also
 * expects response parameters to be placed in specific fields of the
 * Completion Queue entry. It can be confusing to follow.
 */
static void ctc_cmd(uint8 cid,
                    cio_entry *rqe, uint8 *rapp_data,
                    cio_entry *cqe, uint8 *capp_data)
{
    uint32 vtoc_addr, pdinfo_addr, ctjob_addr;
    uint32 maxpass, blkno, delay;
    uint8  dev;
    uint8  sec_buf[512];
    int32  b, j;
    t_seccnt secrw = 0;
    struct vtoc vtoc = {0};
    struct pdinfo pdinfo = {0};

    uint32 lba;   /* Logical Block Address */

    dev = rqe->subdevice & 1;  /* Tape or Floppy device */

    capp_data[7] = rqe->opcode;
    cqe->subdevice = rqe->subdevice;

    switch(rqe->opcode) {
    case CIO_DLM:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CIO Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x\n",
                  rqe->byte_count, rqe->address,
                  rqe->address, rqe->subdevice);
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
                  "[ctc_cmd] CIO Force Function Call: return opcode 0\n");
        delay = DELAY_FCF;

        /* This is to pass diagnostics. TODO: Figure out how to parse
         * the given test x86 code and determine how to respond
         * correctly */
        pwrite_h(0x200f000, 0x1);   /* Test success */
        pwrite_h(0x200f002, 0x0);   /* Test Number */
        pwrite_h(0x200f004, 0x0);   /* Actual */
        pwrite_h(0x200f006, 0x0);   /* Expected */
        pwrite_b(0x200f008, 0x1);   /* Success flag again */
        pwrite_b(0x200f009, 0x30);  /* ??? */

        /* An interesting (?) side-effect of FORCE FUNCTION CALL is
         * that it resets the card state such that a new SYSGEN is
         * required in order for new commands to work. In fact, an
         * INT0/INT1 combo _without_ a RESET can sysgen the board. So,
         * we reset the command bits here. */
        cio[cid].sysgen_s = 0;
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
        /* The system wants us to write sub-device structures at the
         * supplied address, but we have nothing to write. */
        pwrite_h(rqe->address, 0x0);
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

        if (ctc_unit.flags & UNIT_WLK) {
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

        if (ctc_unit.flags & UNIT_WLK) {
            cqe->opcode = CTC_RDONLY;
            break;
        }

        blkno = ATOW(rapp_data, 0);

        for (b = 0; b < rqe->byte_count / 512; b++) {
            ctc_state[dev].time += 10;
            for (j = 0; j < 512; j++) {
                /* Fill the buffer */
                sec_buf[j] = pread_b(rqe->address + (b * 512) + j);
            }
            lba = blkno + b;
            sim_debug(TRACE_DBG, &ctc_dev,
                      "[ctc_cmd] ... CTC_WRITE: 512 bytes at block %d (0x%x)\n",
                      lba, lba);
            sim_disk_wrsect(&ctc_unit, lba, sec_buf, &secrw, 1);
        }

        cqe->opcode = CTC_SUCCESS;
        break;
    case CTC_READ:
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_cmd] CTC_READ (%d)\n",
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

        blkno = ATOW(rapp_data, 0);

        for (b = 0; b < rqe->byte_count / 512; b++) {
            ctc_state[dev].time += 10;
            lba = blkno + b;
            sim_debug(TRACE_DBG, &ctc_dev,
                      "[ctc_cmd] ... CTC_READ: 512 bytes from block %d (0x%x)\n",
                      lba, lba);
            sim_disk_rdsect(&ctc_unit, lba, sec_buf, &secrw, 1);
            for (j = 0; j < 512; j++) {
                /* Drain the buffer */
                pwrite_b(rqe->address + (b * 512) + j, sec_buf[j]);
            }
        }

        cqe->opcode = CTC_SUCCESS;
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

    cio_irq(cid, rqe->subdevice, delay);
}

void ctc_sysgen(uint8 cid)
{
    cio_entry cqe = {0};
    uint8 rapp_data[12] = {0};

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen] Handling Sysgen.\n");
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    rqp=%08x\n", cio[cid].rqp);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    cqp=%08x\n", cio[cid].cqp);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    rqs=%d\n", cio[cid].rqs);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    cqs=%d\n", cio[cid].cqs);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    ivec=%d\n", cio[cid].ivec);
    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_sysgen]    no_rque=%d\n", cio[cid].no_rque);

    cqe.opcode = 3; /* Sysgen success! */

    cio_cexpress(cid, CTQCESIZE, &cqe, rapp_data);
    cio_cqueue(cid, CIO_STAT, CTQCESIZE, &cqe, rapp_data);

    int_cid = cid;
    sim_activate_after(&ctc_unit, DELAY_SYSGEN);
}

void ctc_express(uint8 cid)
{
    cio_entry rqe, cqe;
    uint8 rapp_data[12] = {0};
    uint8 capp_data[8] = {0};

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_express] Handling Express Request\n");

    cio_rexpress(cid, CTQRESIZE, &rqe, rapp_data);
    ctc_cmd(cid, &rqe, rapp_data, &cqe, capp_data);

    dump_entry(TRACE_DBG, &ctc_dev, "COMPLETION",
               CTQCESIZE, &cqe, capp_data);

    cio_cexpress(cid, CTQCESIZE, &cqe, capp_data);
}

void ctc_full(uint8 cid)
{
    cio_entry rqe, cqe;
    uint8 rapp_data[12] = {0};
    uint8 capp_data[8] = {0};

    sim_debug(TRACE_DBG, &ctc_dev, "[ctc_full] Handling Full Request\n");

    while (cio_cqueue_avail(cid, CTQCESIZE) &&
           cio_rqueue(cid, TAPE_DEV, CTQRESIZE, &rqe, rapp_data) == SCPE_OK) {
        ctc_cmd(cid, &rqe, rapp_data, &cqe, capp_data);
    }
    cio_cqueue(cid, CIO_STAT, CTQCESIZE, &cqe, capp_data);
}

t_stat ctc_reset(DEVICE *dptr)
{
    uint8 cid;

    sim_debug(TRACE_DBG, &ctc_dev,
              "[ctc_reset] Resetting CTC device\n");

    memset(ctc_state, 0, 2 * sizeof(CTC_STATE));

    if (dptr->flags & DEV_DIS) {
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_reset] REMOVING CARD\n");

        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == CTC_ID) {
                break;
            }
        }

        if (cid == CIO_SLOTS) {
            /* No card was ever attached */
            return SCPE_OK;
        }

        cio[cid].id = 0;
        cio[cid].ipl = 0;
        cio[cid].ivec = 0;
        cio[cid].exp_handler = NULL;
        cio[cid].full_handler = NULL;
        cio[cid].sysgen = NULL;

        ctc_conf = FALSE;
    } else if (!ctc_conf) {
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[ctc_reset] ATTACHING CARD\n");

        /* Find the first avaialable slot */
        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == 0) {
                break;
            }
        }

        /* Do we have room? */
        if (cid == CIO_SLOTS) {
            return SCPE_NXM;
        }

        cio[cid].id = CTC_ID;
        cio[cid].ipl = CTC_IPL;
        cio[cid].exp_handler = &ctc_express;
        cio[cid].full_handler = &ctc_full;
        cio[cid].sysgen = &ctc_sysgen;

        ctc_conf = TRUE;
    }

    return SCPE_OK;
}

t_stat ctc_svc(UNIT *uptr)
{
    uint16 lp, ulp;

    if (cio[int_cid].ivec > 0) {
        sim_debug(TRACE_DBG, &ctc_dev,
                  "[cio_svc] IRQ for board %d (VEC=%d)\n",
                  int_cid, cio[int_cid].ivec);
        cio[int_cid].intr = TRUE;
    }

    /* Check to see if the completion queue has more work in it. We
     * need to schedule an interrupt for each job if we've fallen
     * behind (this should be rare) */
    lp = cio_c_lp(int_cid, CTQCESIZE);
    ulp = cio_c_ulp(int_cid, CTQCESIZE);

    if ((ulp + CTQCESIZE) % (CTQCESIZE * cio[int_cid].cqs) != lp) {
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
    return sim_disk_attach(uptr, cptr, 512, 1, TRUE, 0, "CIPHER23", 0, 0);
}

t_stat ctc_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}

t_stat ctc_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ctc_show_queue_common(st, uptr, val, desc, TRUE);
}

t_stat ctc_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ctc_show_queue_common(st, uptr, val, desc, FALSE);
}


static t_stat ctc_show_queue_common(FILE *st, UNIT *uptr, int32 val,
                                      CONST void *desc, t_bool rq)
{
    uint8 cid;
    char *cptr = (char *) desc;
    t_stat result;
    uint32 ptr, size, no_rque, i, j;
    uint8  op, dev, seq, cmdstat;

    if (cptr) {
        cid = (uint8) get_uint(cptr, 10, 12, &result);
        if (result != SCPE_OK) {
            return SCPE_ARG;
        }
    } else {
        return SCPE_ARG;
    }

    /* If the card is not sysgen'ed, give up */
    if (cio[cid].sysgen_s != CIO_SYSGEN) {
        fprintf(st, "No card in slot %d, or card has not completed sysgen\n", cid);
        return SCPE_ARG;
    }

    if (rq) {
        ptr = cio[cid].rqp;
        size = cio[cid].rqs;
        no_rque = cio[cid].no_rque;
        fprintf(st, "Dumping %d Request Queues\n", no_rque);
        fprintf(st, "---------------------------------------------------------\n");
        fprintf(st, "EXPRESS ENTRY:\n");
        fprintf(st, "    Byte Count: %d\n",     pread_h(ptr));
        fprintf(st, "    Subdevice:  %d\n",     pread_b(ptr + 2));
        fprintf(st, "    Opcode:     0x%02x\n", pread_b(ptr + 3));
        fprintf(st, "    Addr/Data:  0x%08x\n", pread_w(ptr + 4));
        fprintf(st, "    App Data:   0x%08x\n", pread_w(ptr + 8));
        ptr += CTQRESIZE;

        for (i = 0; i < no_rque; i++) {
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "REQUEST QUEUE %d\n", i);
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "Load Pointer:   %d\n", pread_h(ptr) / CTQRESIZE);
            fprintf(st, "Unload Pointer: %d\n", pread_h(ptr + 2) / CTQRESIZE);
            fprintf(st, "---------------------------------------------------------\n");
            ptr += 4;
            for (j = 0; j < size; j++) {
                dev = pread_b(ptr + 2);
                op = pread_b(ptr + 3);
                seq = (dev & 0x40) >> 6;
                cmdstat = (dev & 0x80) >> 7;
                fprintf(st, "REQUEST ENTRY %d\n", j);
                fprintf(st, "    Byte Count: %d\n",          pread_h(ptr));
                fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
                fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
                fprintf(st, "    Seqbit:     %d\n",          seq);
                fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
                fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
                fprintf(st, "    App Data:   0x%08x 0x%08x 0x%08x\n",
                        pread_w(ptr + 8), pread_w(ptr + 12), pread_w(ptr + 16));
                ptr += CTQRESIZE;
            }
        }
    } else {
        ptr = cio[cid].cqp;
        size = cio[cid].cqs;
        no_rque = 0; /* Not used */
        fprintf(st, "Dumping Completion Queue\n");
        fprintf(st, "---------------------------------------------------------\n");
        fprintf(st, "EXPRESS ENTRY:\n");
        fprintf(st, "    Byte Count: %d\n",     pread_h(ptr));
        fprintf(st, "    Subdevice:  %d\n",     pread_b(ptr + 2));
        fprintf(st, "    Opcode:     0x%02x\n", pread_b(ptr + 3));
        fprintf(st, "    Addr/Data:  0x%08x\n", pread_w(ptr + 4));
        fprintf(st, "    App Data:   0x%08x\n", pread_w(ptr + 8));
        ptr += CTQCESIZE;

        fprintf(st, "---------------------------------------------------------\n");
        fprintf(st, "Load Pointer:   %d\n", pread_h(ptr) / CTQCESIZE);
        fprintf(st, "Unload Pointer: %d\n", pread_h(ptr + 2) / CTQCESIZE);
        fprintf(st, "---------------------------------------------------------\n");
        ptr += 4;
        for (i = 0; i < size; i++) {
            dev = pread_b(ptr + 2);
            op = pread_b(ptr + 3);
            seq = (dev & 0x40) >> 6;
            cmdstat = (dev & 0x80) >> 7;
            fprintf(st, "COMPLETION ENTRY %d\n", i);
            fprintf(st, "    Byte Count: %d\n",          pread_h(ptr));
            fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
            fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
            fprintf(st, "    Seqbit:     %d\n",          seq);
            fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
            fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
            fprintf(st, "    App Data:   0x%08x 0x%08x\n",
                    pread_w(ptr + 8), pread_w(ptr + 12));
            ptr += CTQCESIZE;
        }
    }

    return SCPE_OK;
}
