/* 3b2_scsi.c: AT&T 3B2 SCSI (CM195W) feature card

   Copyright (c) 2020, Seth J. Morabito

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

#include "3b2_scsi.h"

#include "sim_scsi.h"
#include "sim_tape.h"

#include "3b2_cpu.h"
#include "3b2_io.h"
#include "3b2_mem.h"

#define PUMP_CRC 0x201b3617

static void ha_cmd(uint8 op, uint8 subdev, uint32 addr,
                   int32 len, t_bool express);
static void ha_build_req(uint8 subdev, t_bool express);
static void ha_ctrl();
static void ha_write();
static void ha_write_ext();
static void ha_read_ext();
static void ha_read_capacity();

static void dump_rep();
static void dump_req();
static void dump_edt();

/* Do not initiate host IRQ if ivec is <= this value */
#define SCSI_MIN_IVEC   1
#define HA_SCSI_ID      0
#define HA_MAXFR        (1u << 16)

HA_STATE ha_state;
SCSI_BUS ha_bus;
uint8    *ha_buf;
int8     ha_subdev_tab[8];    /* Map of subdevice to SCSI target */
uint8    ha_subdev_cnt;
uint32   ha_crc = 0;
uint32   cq_offset = 0;

static DRVTYP ha_tab[] = {
    HA_DISK(SD327),
    HA_TAPE(ST120)
};

#define SCSI_U_FLAGS  (UNIT_FIX|UNIT_ATTABLE|UNIT_DISABLE|UNIT_ROABLE)

UNIT ha_unit[9] = {0};  /* SCSI ID 0-7 + CIO Unit */

static UNIT *cio_unit  = &ha_unit[8];

MTAB ha_mod[] = {
    { SCSI_WLK, 0, NULL, "WRITEENABLED",
      &scsi_set_wlk, NULL, NULL, "Write enable disk drive" },
    { SCSI_WLK, SCSI_WLK, NULL, "LOCKED",
      &scsi_set_wlk, NULL, NULL, "Write lock disk drive" },
    { MTAB_XTD|MTAB_VUN, 0, "WRITE", NULL,
      NULL, &scsi_show_wlk, NULL, "Display drive writelock status" },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &scsi_set_fmt, &scsi_show_fmt, NULL, "Set/Display unit format" },
    { 0 }
};

#define HA_TRACE 1

static DEBTAB ha_debug[] = {
    { "TRACE", HA_TRACE,      "Call Trace" },
    { "SCMD",  SCSI_DBG_CMD,  "SCSI commands"},
    { "SBUS",  SCSI_DBG_BUS,  "SCSI bus activity" },
    { "SMSG",  SCSI_DBG_MSG,  "SCSI messages" },
    { "SDSK",  SCSI_DBG_DSK,  "SCSI disk activity" },
    { NULL }
};

DEVICE ha_dev = {
    "SCSI",                                 /* name */
    ha_unit,                                /* units */
    NULL,                                   /* registers */
    ha_mod,                                 /* modifiers */
    9,                                      /* #units */
    16,                                     /* address radix */
    32,                                     /* address width */
    1,                                      /* address incr. */
    16,                                     /* data radix */
    8,                                      /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &ha_reset,                              /* reset routine */
    NULL,                                   /* boot routine */
    &ha_attach,                             /* attach routine */
    &ha_detach,                             /* detach routine */
    NULL,                                   /* context */
    DEV_DEBUG|DEV_DISK|DEV_SECTORS,         /* flags */
    0,                                      /* debug control flags */
    ha_debug,                               /* debug flag names */
    NULL,                                   /* memory size change */
    NULL,                                   /* logical name */
    NULL,                                   /* help routine */
    NULL,
    NULL,
    NULL,
    NULL,
    &ha_tab
};

void ha_cio_reset(uint8 cid)
{
    sim_debug(HA_TRACE, &ha_dev,
              "[%08x] Handling CIO reset\n",
              R[NUM_PC]);
    ha_state.pump_state = PUMP_NONE;
    ha_crc = 0;
}

t_stat ha_reset(DEVICE *dptr)
{
    uint8 cid;
    uint32 t;
    UNIT *uptr;
    t_stat r;
    static t_bool inited = FALSE;

    sim_debug(HA_TRACE, &ha_dev,
              "[%08x] [ha_reset] Resetting SCSI device\n",
              R[NUM_PC]);

    if (!inited) {
        inited = TRUE;
        for (t = 0; t < dptr->numunits - 1; t++) {
            uptr = dptr->units + t;
            uptr->action = &ha_svc;
            uptr->flags = SCSI_U_FLAGS;
            sim_disk_set_drive_type_by_name (uptr, "SD327");
            }
        uptr = dptr->units + t;
        uptr->action = &ha_svc;
        uptr->flags = UNIT_DIS;
        }

    ha_state.pump_state = PUMP_NONE;

    if (ha_buf == NULL) {
        ha_buf = (uint8 *)calloc(HA_MAXFR, sizeof(uint8));
    }

    r = scsi_init(&ha_bus, HA_MAXFR);

    if (r != SCPE_OK) {
        return r;
    }

    ha_bus.dptr = dptr;

    scsi_reset(&ha_bus);

    for (t = 0; t < 8; t++) {
        uptr = dptr->units + t;
        if (t == HA_SCSI_ID) {
            uptr->flags = UNIT_DIS;
        }
        scsi_add_unit(&ha_bus, t, uptr);
        scsi_reset_unit(uptr);
    }

    if (dptr->flags & DEV_DIS) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_reset] REMOVING CARD\n");

        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == HA_ID) {
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

        ha_state.initialized = FALSE;

    } else if (!ha_state.initialized) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_reset] Attaching SCSI Card\n");

        /* Find the first available slot */
        for (cid = 0; cid < CIO_SLOTS; cid++) {
            if (cio[cid].id == 0) {
                break;
            }
        }

        if (cid == CIO_SLOTS) {
            return SCPE_NXM;
        }

        cio[cid].id = HA_ID;
        cio[cid].ipl = HA_IPL;
        cio[cid].exp_handler = &ha_express;
        cio[cid].full_handler = &ha_full;
        cio[cid].reset_handler = &ha_cio_reset;
        cio[cid].sysgen = &ha_sysgen;

        ha_state.initialized = TRUE;
        ha_state.cid = cid;

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_reset] SCSI Card now enabled in IO Slot %d\n",
                  cid);
    }

    return SCPE_OK;
}

static void ha_calc_subdevs()
{
    uint32 target;
    UNIT *uptr;

    ha_subdev_cnt = 0;

    memset(ha_subdev_tab, -1, 8);

    for (target = 0; target < 8; target++) {
        uptr = &ha_unit[target];
        if (uptr->flags & UNIT_ATT) {
            ha_subdev_tab[ha_subdev_cnt++] = target;
        }
    }
}

t_stat ha_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    r = scsi_attach(uptr, cptr);
    ha_calc_subdevs();
    return r;
}

t_stat ha_detach(UNIT *uptr)
{
    t_stat r;
    r = scsi_detach(uptr);
    ha_calc_subdevs();
    return r;
}

t_stat ha_svc(UNIT *uptr)
{
    cio_entry cqe;
    uint8 capp_data[CAPP_LEN] = {0};

    /* Finish any pending job */
    if (ha_state.reply.pending) {
        ha_state.reply.pending = FALSE;

        switch(ha_state.reply.type) {
        case HA_JOB_QUICK:
            ha_fcm_express();

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_svc] FAST MODE CQ: status=%02x op=%02x subdev=%02x ssb=%02x\n",
                      ha_state.reply.status, ha_state.reply.op, ha_state.reply.subdev, ha_state.reply.ssb);
            break;
        case HA_JOB_EXPRESS:
        case HA_JOB_FULL:
            cqe.byte_count = ha_state.reply.len;
            cqe.opcode = ha_state.reply.status; /* Yes, status, not opcode! */
            cqe.subdevice = ha_state.reply.subdev;
            cqe.address = ha_state.reply.addr;

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_svc] CQE: byte_count=%04x, opcode=%02x, subdevice=%02x, addr=%08x\n",
                      cqe.byte_count, cqe.opcode, cqe.subdevice, cqe.address);

            if (ha_state.reply.type == HA_JOB_EXPRESS) {
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_svc] EXPRESS MODE CQ: byte_count=%02x op=%02x subdev=%02x address=%08x\n",
                          cqe.byte_count, cqe.opcode, cqe.subdevice, cqe.address);

                cio_cexpress(ha_state.cid, SCQCESIZE, &cqe, capp_data);
            } else {
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_svc] FULL MODE CQ: status=%02x op=%02x subdev=%02x ssb=%02x\n",
                          ha_state.reply.status, ha_state.reply.op, ha_state.reply.subdev, ha_state.reply.ssb);

                cio_cqueue(ha_state.cid, 0, SCQCESIZE, &cqe, capp_data);
            }
            break;
        }

        dump_rep();
    }

    if (cio[ha_state.cid].ivec > SCSI_MIN_IVEC) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_svc] IRQ for board %d (VEC=%d).\n",
                  R[NUM_PC], ha_state.cid, cio[ha_state.cid].ivec);
        CIO_SET_INT(ha_state.cid);
    }

    return SCPE_OK;
}

void ha_sysgen(uint8 cid)
{
    uint32 sysgen_p;
    uint32 alert_buf_p;

    cq_offset = 0;

    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen] Handling Sysgen.\n");
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    rqp=%08x\n", cio[cid].rqp);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    cqp=%08x\n", cio[cid].cqp);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    rqs=%d\n", cio[cid].rqs);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    cqs=%d\n", cio[cid].cqs);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    ivec=%d\n", cio[cid].ivec);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    no_rque=%d\n", cio[cid].no_rque);

    sysgen_p = pread_w(SYSGEN_PTR);
    alert_buf_p  = pread_w(sysgen_p + 12);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    alert_bfr=%08x\n", alert_buf_p);

    ha_state.frq = (cio[cid].no_rque == 0);

    ha_state.reply.type = ha_state.frq ? HA_JOB_QUICK : HA_JOB_EXPRESS;
    ha_state.reply.addr = 0;
    ha_state.reply.len = 0;
    ha_state.reply.status = 3;
    ha_state.reply.op = 0;
    ha_state.reply.pending = TRUE;

    if (ha_crc == PUMP_CRC) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_full] PUMP: NEW STATE = PUMP_SYSGEN\n",
                  R[NUM_PC]);
        ha_state.pump_state = PUMP_SYSGEN;
    } else {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_full] PUMP: NEW STATE = PUMP_NONE\n",
                  R[NUM_PC]);
        ha_state.pump_state = PUMP_NONE;
    }

    sim_activate_abs(cio_unit, 100000);
}

void ha_fast_queue_check()
{
    uint8 busy, op, subdev;
    uint32 timeout, addr, len, rqp;
    rqp = cio[ha_state.cid].rqp;

    busy    = pread_b(rqp);
    op      = pread_b(rqp + 1);
    subdev  = pread_b(rqp + 2);
    timeout = pread_w(rqp + 4);
    addr    = pread_w(rqp + 8);
    len     = pread_w(rqp + 12);

    if (busy == 0xff || ha_state.pump_state != PUMP_COMPLETE) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_fast_queue_check] Job pending (opcode=0x%02x subdev=%02x)\n",
                  R[NUM_PC], op, subdev);
        pwrite_b(rqp, 0); /* Job has been taken */
        ha_cmd(op, subdev, addr, len, FALSE);
    }
}

void ha_express(uint8 cid)
{
    cio_entry rqe;
    uint8 rapp_data[RAPP_LEN] = {0};

    sim_debug(HA_TRACE, &ha_dev,
              "[%08x] [ha_express] Handling Express Request\n",
              R[NUM_PC]);

    if (ha_state.frq) {
        ha_fast_queue_check();
    } else {
        cio_rexpress(cid, SCQRESIZE, &rqe, rapp_data);

        ha_cmd(rqe.opcode, rqe.subdevice, rqe.address,
               rqe.byte_count, TRUE);
    }
}

void ha_full(uint8 cid)
{
    sim_debug(HA_TRACE, &ha_dev,
              "[%08x] [ha_full] Handling Full Request (INT3)\n",
              R[NUM_PC]);

    if (ha_state.pump_state == PUMP_SYSGEN) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_full] PUMP: NEW STATE = PUMP_COMPLETE\n",
                  R[NUM_PC]);
        ha_state.pump_state = PUMP_COMPLETE;
    }

    if (ha_state.frq) {
        ha_fast_queue_check();
    } else {
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_full] NON_FRQ NOT HANDLED\n",
                  R[NUM_PC]);
    }
}

static void dump_req()
{
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp,
              pread_w(cio[ha_state.cid].rqp));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 4,
              pread_w(cio[ha_state.cid].rqp + 4));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 8,
              pread_w(cio[ha_state.cid].rqp + 8));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 12,
              pread_w(cio[ha_state.cid].rqp + 12));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 16,
              pread_w(cio[ha_state.cid].rqp + 16));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 20,
              pread_w(cio[ha_state.cid].rqp + 20));
    sim_debug(HA_TRACE, &ha_dev,
              "[REQ]      [%08x] %08x\n",
              cio[ha_state.cid].rqp + 24,
              pread_w(cio[ha_state.cid].rqp + 24));
}

static void dump_rep()
{
    uint32 i;
    uint32 cqs = cio[ha_state.cid].cqs;

    for (i = 0; i < cqs; i++) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[CEQ]      [%08x] %08x\n",
                  cio[ha_state.cid].cqp + (i * 4),
                  pread_w(cio[ha_state.cid].cqp + (i * 4)));
    }
}

static void ha_boot_disk(UNIT *uptr, uint8 target)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i, boot_loc;

    /* Read in the Physical Descriptor (PD) block (block 0) */
    r = sim_disk_rdsect(uptr, 0, buf, &sectsread, 1);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_disk] Could not read LBA 0\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    /* Store the Physical Descriptor (PD) block at well-known
       address 0x2004400 */
    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Storing PD block at 0x%08x.\n",
              HA_PDINFO_ADDR);
    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(HA_PDINFO_ADDR + i, buf[i]);
    }


    /* The PD block points to the logical start of disk */
    boot_loc = ATOW(buf, HA_PDLS_OFF);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Logical Start is at 0x%x\n",
              boot_loc);

    r = sim_disk_rdsect(uptr, boot_loc, buf, &sectsread, 1);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Storing boot block %d at 0x%08x.\n",
              boot_loc, HA_BOOT_ADDR);

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(HA_BOOT_ADDR + i, buf[i]);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Done storing boot block at 0x%08x\n",
              HA_BOOT_ADDR);

    HA_STAT(HA_GOOD, CIO_SUCCESS);

    ha_state.reply.addr = HA_BOOT_ADDR;
    ha_state.reply.len = HA_BLKSZ;
}

static void ha_boot_tape(UNIT *uptr)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    if (!(uptr->flags & UNIT_ATT)) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Target not attached\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rewind(uptr);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Could not rewind tape\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rdrecf(uptr, buf, &sectsread, HA_BLKSZ); /* Read block 0 */

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Could not read PD block.\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(HA_BOOT_ADDR + i, buf[i]);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_tape] Transfered 512 bytes to 0x%08x\n",
              HA_BOOT_ADDR);

    r = sim_tape_sprecf(uptr, &sectsread);           /* Skip block 1 */

    HA_STAT(HA_GOOD, CIO_SUCCESS);

    ha_state.reply.addr = HA_BOOT_ADDR;
    ha_state.reply.len = HA_BLKSZ;
}

static void ha_read_block_tape(UNIT *uptr, uint32 addr)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    if (!(uptr->flags & UNIT_ATT)) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_tape] Target not attached\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rdrecf(uptr, buf, &sectsread, HA_BLKSZ);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_tape] Could not read next block.\n");
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(addr + i, buf[i]);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_read_block_tape] Transfered 512 bytes to 0x%08x\n",
              addr);

    HA_STAT(HA_GOOD, CIO_SUCCESS);

    ha_state.reply.addr = addr;
    ha_state.reply.len = HA_BLKSZ;
}

static void ha_read_block_disk(UNIT *uptr, uint8 target, uint32 addr, uint32 lba)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_read_block_disk] Block translated from LBA 0x%X to PBA 0x%X\n",
              lba, lba);

    r = sim_disk_rdsect(uptr, lba, buf, &sectsread, 1);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_disk] Could not read block %d\n",
                  lba);
        HA_STAT(HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(addr + i, buf[i]);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_read_block_disk] Transferred 512 bytes to 0x%08x\n",
              addr);

    HA_STAT(HA_GOOD, CIO_SUCCESS);

    ha_state.reply.addr = addr;
    ha_state.reply.len = HA_BLKSZ;
}

static void ha_build_req(uint8 subdev, t_bool express)
{
    uint32 i, rqp, ptr, dma_lst;
    uint32 len, addr;
    cio_entry rqe;
    uint8 rapp_data[RAPP_LEN] = {0};

    /*
     * There are two possible ways to get the SCSI command we've
     * been asked to perform.
     *
     * 1. If this is a "fast mode" operation, then the SCSI command
     *    is embedded in the Fast Request Queue entry.
     *
     * 2. If this is a regular queue operation, then the SCSI command
     *    is embedded in a struct pointed to by the "address" field
     *    of the queue entry.
     */

    for (i = 0; i < HA_MAX_CMD; i++) {
        ha_state.request.cmd[i] = 0;
    }

    if (ha_state.frq) {
        rqp = cio[ha_state.cid].rqp;

        subdev = pread_b(rqp + 2);

        ha_state.request.tc = FC_TC(subdev);
        ha_state.request.lu = FC_LU(subdev);
        ha_state.request.timeout = pread_w(rqp + 4);
        ha_state.request.cmd_len = pread_h(rqp + 18);
        for (i = 0; i < HA_MAX_CMD; i++) {
            ha_state.request.cmd[i] = pread_b(rqp + 20 + i);
        }
        ha_state.request.op = ha_state.request.cmd[0];

        /* Possible list of DMA scatter/gather addresses */
        dma_lst = pread_h(rqp + 16) / 8;

        if (dma_lst) {
            t_bool link;

            /* There's a list of address / lengths. Each entry is 8
             * bytes long. */
            ptr = pread_w(rqp + 8);
            link = FALSE;

            sim_debug(HA_TRACE, &ha_dev,
                      "[build_req] Building a list of scatter/gather addresses.\n");

            for (i = 0; (i < dma_lst) || link; i++) {
                addr = pread_w(ptr);
                len = pread_w(ptr + 4);

                if (len == 0) {
                    sim_debug(HA_TRACE, &ha_dev,
                              "[build_req] Found length of 0, bailing early.\n");
                    break; /* Done early */
                }

                if (len > 0x1000) {
                    /* There's a new pointer in town */
                    ptr = pread_w(ptr);
                    sim_debug(HA_TRACE, &ha_dev,
                              "[build_req] New ptr=%08x\n",
                              ptr);
                    link = TRUE;
                    continue;
                }

                sim_debug(HA_TRACE, &ha_dev,
                          "[build_req]   daddr[%d]: addr=%08x, len=%d (%x)\n",
                          i, addr, len, len);

                ha_state.request.daddr[i].addr = addr;
                ha_state.request.daddr[i].len = len;

                ptr += 8;
            }

            ha_state.request.dlen = i;
        } else {
            /* There's only one embedded address / length */
            ha_state.request.daddr[0].addr = pread_w(rqp + 8);
            ha_state.request.daddr[0].len = pread_w(rqp + 12);
            ha_state.request.dlen = 1;
        }

    } else {
        if (express) {
            cio_rexpress(ha_state.cid, SCQRESIZE, &rqe, rapp_data);
        } else {
            /* TODO: Find correct queue number! */
            cio_rqueue(ha_state.cid, 0, SCQRESIZE, &rqe, rapp_data);
        }

        ptr = rqe.address;

        ha_state.request.tc = FC_TC(rqe.subdevice);
        ha_state.request.lu = FC_LU(rqe.subdevice);
        ha_state.request.cmd_len = pread_w(ptr + 4);
        ha_state.request.timeout = pread_w(ptr + 8);
        ha_state.request.daddr[0].addr = pread_w(ptr + 12);
        ha_state.request.daddr[0].len = rqe.byte_count;
        ha_state.request.dlen = 1;

        sim_debug(HA_TRACE, &ha_dev,
                  "[build_req] [non-fast] Building a list of 1 scatter/gather addresses.\n");

        ptr = pread_w(ptr);

        for (i = 0; (i < ha_state.request.cmd_len) && (i < HA_MAX_CMD); i++) {
            ha_state.request.cmd[i] = pread_b(ptr + i);
        }

        ha_state.request.op = ha_state.request.cmd[0];
    }
}

static void ha_cmd(uint8 op, uint8 subdev, uint32 addr, int32 len, t_bool express)
{
    int32 i, block;
    UNIT *uptr;
    int8 target;

    /* Immediately cancel any pending IRQs */
    sim_cancel(cio_unit);

    ha_state.reply.pending = TRUE;
    ha_state.reply.op = op;
    ha_state.reply.subdev = subdev;
    ha_state.reply.status = CIO_FAILURE;
    ha_state.reply.ssb = 0;
    ha_state.reply.len = 0;
    ha_state.reply.addr = 0;

    if (ha_state.pump_state == PUMP_COMPLETE) {
        ha_state.reply.op |= 0x80;
    }

    if (ha_state.frq) {
        ha_state.reply.type = HA_JOB_QUICK;
    } else if (express) {
        ha_state.reply.type = HA_JOB_EXPRESS;
    } else {
        ha_state.reply.type = HA_JOB_FULL;
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] --------------------------[START]---------------------------------\n");
    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] op=%02x (%d), subdev=%02x, addr=%08x, len=%d\n",
              op, op, subdev, addr, len);

    dump_req();

    switch (op) {
    case CIO_DLM:
        for (i = 0; i < len; i++) {
            ha_crc = cio_crc32_shift(ha_crc, pread_b(addr + i));
        }

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x (CRC=%08x)\n",
                  len, addr, addr, subdev, ha_crc);
        ha_state.reply.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1200);
        break;
    case CIO_FCF:
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI Force Function Call. (CRC=%08x)\n",
                  ha_crc);
        cio[ha_state.cid].sysgen_s = 0;
        ha_state.reply.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1200);
        break;
    case CIO_DSD:
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI DSD - %d CONFIGURED DEVICES (writing to addr %08x).\n",
                  ha_subdev_cnt, addr);

        pwrite_h(addr, ha_subdev_cnt);

        for (i = 0; i < ha_subdev_cnt; i++) {
            addr += 2;

            target = ha_subdev_tab[i];

            if (target < 0) {
                pwrite_h(addr, 0);
                continue;
            }

            uptr = &ha_unit[target];

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_cmd] [DSD] Probing subdev %d, target %d, devtype %d\n",
                      i, target, uptr->drvtyp->devtype);

            switch(uptr->drvtyp->devtype) {
            case SCSI_DISK:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Subdev %d is DISK (writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, HA_DSD_DISK);
                break;
            case SCSI_TAPE:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Subdev %d is TAPE (writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, HA_DSD_TAPE);
                break;
            default:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Warning: No device type for subdev %d (Writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, 0);
                break;
            }
        }

        ha_state.reply.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 5000);

        break;
    case HA_BOOT:
        target = ha_subdev_tab[subdev & 7];

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] TARGET %d BOOTING.\n",
                  target);

        if (target < 0) {
            ha_state.reply.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 250);
            return;
        }

        uptr = &ha_unit[target];

        if (!(uptr->flags & UNIT_ATT)) {
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_cmd] TARGET %d NOT ATTACHED.\n",
                      target);
            ha_state.reply.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 250);
            return;
        }

        switch(uptr->drvtyp->devtype) {
        case SCSI_DISK:
            ha_boot_disk(uptr, target);
            break;
        case SCSI_TAPE:
            ha_boot_tape(uptr);
            break;
        default:
            sim_debug(HA_TRACE, &ha_dev,
                      "[HA_BOOT] Cannot boot target %d (not disk or tape).\n",
                      target);
            ha_state.reply.status = CIO_SUCCESS;
            break;
        }

        sim_activate_abs(cio_unit, 5000);
        break;
    case HA_READ_BLK:
        target = ha_subdev_tab[subdev & 7];

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SUBDEV %d TARGET %d READ BLOCK (BLOCK 0x%08x TO ADDR 0x%08x)\n",
                  subdev, target, pread_w(addr), pread_w(addr + 4));

        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    addr = %08x\n",
                  addr);
        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    %08x = %08x\n",
                  addr, pread_w(addr));
        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    %08x = %08x\n",
                  addr + 4, pread_w(addr + 4));
        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    %08x = %08x\n",
                  addr + 8, pread_w(addr + 8));
        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    %08x = %08x\n",
                  addr + 12, pread_w(addr + 12));
        sim_debug(HA_TRACE, &ha_dev,
                  "[boot_next]    %08x = %08x\n",
                  addr + 16, pread_w(addr + 16));


        if (target < 0) {
            ha_state.reply.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 250);
            return;
        }

        uptr = &ha_unit[target];

        if (!(uptr->flags & UNIT_ATT)) {
            ha_state.reply.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 250);
            return;
        }

        block = pread_w(addr);     /* Logical block we've been asked to read */
        addr = pread_w(addr + 4);  /* Dereference the pointer to the destination */

        switch(uptr->drvtyp->devtype) {
        case SCSI_TAPE:
            ha_read_block_tape(uptr, addr);
            break;
        case SCSI_DISK:
            ha_read_block_disk(uptr, target, addr, block);
            break;
        default:
            sim_debug(HA_TRACE, &ha_dev,
                      "[HA_READ_BLOCK] Cannot read block %d on target %d (not disk or tape)\n",
                      block, target);
            ha_state.reply.status = CIO_SUCCESS;
            break;
        }

        sim_activate_abs(cio_unit, 5000);

        break;
    case HA_CNTRL:
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI CONTROL (subdev=%02x addr=%08x)\n",
                  subdev, addr);

        ha_build_req(subdev, express);
        ha_ctrl();
        sim_activate_abs(cio_unit, 1000);
        break;
    case HA_VERS:
        /*
         * Get Host Adapter Version
         */

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI GET VERSION (addr=%08x len=%08x)\n",
                  addr, len);

        pwrite_w(addr, HA_VERSION);
        ha_state.reply.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 5000);

        break;
    case HA_DL_EEDT:
        /*
         * This is a request to download the Extended Equipped Device
         * Table from the host adapter to the 3B2 main memory.
         */

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI DOWNLOAD EDT (%d bytes to address %08x)\n",
                  len, addr);

        for (i = 0; i < len; i++) {
            pwrite_b(addr + i, ha_state.edt[i]);
        }

        ha_state.reply.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 5000);

        break;
    case HA_UL_EEDT:
        /*
         * This is a request to upload the Extended Equipped Device
         * Table from the 3B2 main memory to the host adapter
         */

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI UPLOAD EDT (%d bytes from address %08x)\n",
                  len, addr);

        for (i = 0; i < len; i++) {
            ha_state.edt[i] = pread_b(addr + i);
        }

        ha_state.reply.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 5000);

        break;
    case HA_EDSD:
        /*
         * This command is used to determine which TCs are attached to
         * the SCSI bus, and what LUNs they support. A "1" in a slot
         * means that the target is not a direct block device. Since we
         * only support direct block devices, we just put "0" in each
         * slot.
         */

        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] [ha_cmd] SCSI EXTENDED DSD.\n",
                  R[NUM_PC]);

        ha_state.reply.status = CIO_SUCCESS;
        ha_state.reply.addr = addr;
        ha_state.reply.len = 9;

        for (i = 0; i < 8; i++) {
            uptr = &ha_unit[i];
            pwrite_b(addr + i, (uptr->flags & UNIT_ATT) ? 1 : 0);
        }

        pwrite_b(addr + 8, HA_SCSI_ID);   /* ID of the card */

        sim_activate_abs(cio_unit, 200);

        break;
    case 0x45:
        scsi_reset(&ha_bus);

        ha_state.reply.status = CIO_SUCCESS;
        ha_state.reply.addr = addr;
        ha_state.reply.len = 0;

        sim_activate_abs(cio_unit, 2500);
        break;
    default:
        sim_debug(HA_TRACE, &ha_dev,
                  "[%08x] *** SCSI WARNING: UNHANDLED OPCODE 0x%02x\n",
                  R[NUM_PC], op);

        ha_state.reply.status = CIO_FAILURE;

        sim_activate_abs(cio_unit, 200);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] ---------------------------[END]----------------------------------\n");

}

/*
 * Handle a raw SCSI control message.
 */
void ha_ctrl()
{
    volatile t_bool txn_done;
	uint32 i, j;
    uint32 plen, ha_ptr;
    uint32 in_len, out_len;
    uint8 lu, status;
    uint8 msgi_buf[64];
    uint32 msgi_len;
    uint32 to_read;

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_ctrl] [HA_REQ] TC=%d LU=%d TIMEOUT=%d DLEN=%d\n",
              ha_state.request.tc, ha_state.request.lu, ha_state.request.timeout, ha_state.request.dlen);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_ctrl] [HA_REQ] CMD_LEN=%d CMD=<%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x>\n",
              ha_state.request.cmd_len,
              ha_state.request.cmd[0], ha_state.request.cmd[1],
              ha_state.request.cmd[2], ha_state.request.cmd[3],
              ha_state.request.cmd[4], ha_state.request.cmd[5],
              ha_state.request.cmd[6], ha_state.request.cmd[7],
              ha_state.request.cmd[8], ha_state.request.cmd[9]);

    in_len = out_len = 0;

    /*
     * These ops need special handling.
     */
    switch(ha_state.request.op) {
    case HA_TESTRDY:
        /* Fail early if LU is set */
        if (ha_state.request.lu) {
            HA_STAT(HA_CKCON, CIO_TIMEOUT);
            return;
        }
        break;
    case HA_FORMAT:   /* Not yet handled by sim_scsi library */
    case HA_VERIFY:   /* Not yet handled by sim_scsi library */
        /* Just mimic success */
        HA_STAT(HA_GOOD, CIO_SUCCESS);
        return;
    }

    /* Get the bus's attention */
    if (!scsi_arbitrate(&ha_bus, HA_SCSI_ID)) {
        HA_STAT(HA_CKCON, CIO_TIMEOUT);
        return;
    }

    scsi_set_atn(&ha_bus);

    if (!scsi_select(&ha_bus, ha_state.request.tc)) {
        HA_STAT(HA_CKCON, CIO_TIMEOUT);
        scsi_release(&ha_bus);
        return;
    }

    /* Select the correct LU */
    lu = 0x80 | ha_state.request.lu;
    scsi_write(&ha_bus, &lu, 1);

    txn_done = FALSE;

    while (!txn_done) {
        switch(ha_bus.phase) {
        case SCSI_CMD:
            plen = scsi_write(&ha_bus, ha_state.request.cmd, ha_state.request.cmd_len);
            if (plen < ha_state.request.cmd_len) {
                HA_STAT(HA_CKCON, CIO_SUCCESS);
                scsi_release(&ha_bus);
                return;
            }
            break;
        case SCSI_DATI:
            /* This is a read */
            in_len = scsi_read(&ha_bus, ha_buf, HA_MAXFR);

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_ctrl] SCSI_DATI: Consumed %d (0x%X) bytes to ha_buf in SCSI read.\n",
                      in_len, in_len);

            /* We need special handling based on the op code */
            switch(ha_state.request.op) {
            case HA_READ:
            case HA_READEXT:
                ha_ptr = 0;

                for (i = 0; i < ha_state.request.dlen; i++) {
                    /*
                     * Consume the lesser of:
                     *   - The total bytes we consumed, or:
                     *   - The length of the current block
                     */
                    to_read = MIN(ha_state.request.daddr[i].len, in_len);

                    sim_debug(HA_TRACE, &ha_dev,
                              "[(%02x) TC%d,LU%d] DATI: Processing %d bytes to address %08x...\n",
                              ha_state.request.op, ha_state.request.tc, ha_state.request.lu, to_read, ha_state.request.daddr[i].addr);

                    for (j = 0; j < to_read; j++) {
                        pwrite_b(ha_state.request.daddr[i].addr + j, ha_buf[ha_ptr++]);
                    }

                    if (in_len >= to_read) {
                        in_len -= to_read;
                    } else {
                        /* Nothing left to write */
                        break;
                    }
                }

                break;
            default:
                sim_debug(HA_TRACE, &ha_dev,
                          "[(%02x) TC%d,LU%d] DATI: Processing %d bytes to address %08x...\n",
                          ha_state.request.op, ha_state.request.tc,
                          ha_state.request.lu, in_len,
                          ha_state.request.daddr[0].addr);
                for (i = 0; i < in_len; i++) {
                    sim_debug(HA_TRACE, &ha_dev, "[%04x] [DATI] 0x%02x\n", i, ha_buf[i]);
                    pwrite_b(ha_state.request.daddr[0].addr + i, ha_buf[i]);
                }

                break;
            }

            break;
        case SCSI_DATO:
            /* This is a write */
            ha_ptr = 0;
            out_len = 0;
            ha_state.reply.len = ha_state.request.dlen;

            for (i = 0; i < ha_state.request.dlen; i++) {
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_ctrl] [%d] DATO: Writing %d bytes to ha_buf.\n",
                          i, ha_state.request.daddr[i].len);

                for (j = 0; j < ha_state.request.daddr[i].len; j++) {
                    ha_buf[ha_ptr++] = pread_b(ha_state.request.daddr[i].addr + j);
                }

                out_len += ha_state.request.daddr[i].len;
            }

            if (ha_state.request.op == HA_WRITE || ha_state.request.op == HA_WRTEXT) {
                /* If total len is not on a block boundary, we have to
                   bump it up in order to write the whole block. */
                if (out_len % HA_BLKSZ) {
                    out_len = out_len + (HA_BLKSZ - (out_len % HA_BLKSZ));
                }
            }

            scsi_write(&ha_bus, ha_buf, out_len);

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_ctrl] SCSI Write of %08x (%d) bytes Complete\n",
                      out_len, out_len);
            break;
        case SCSI_STS:
            scsi_read(&ha_bus, &status, 1);
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_ctrl] STATUS BYTE: %02x\n",
                      status);
            break;
        case SCSI_MSGI:
            msgi_len = scsi_read(&ha_bus, msgi_buf, 64);
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_ctrl] MESSAGE IN LENGTH %d\n",
                      msgi_len);

            for (i = 0; i < msgi_len; i++) {
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_ctrl]    MSGI[%02d] = %02x\n",
                          i, msgi_buf[i]);
            }

            txn_done = TRUE;
            break;
        }
    }

    if (ha_bus.sense_info) {
        sim_debug(HA_TRACE, &ha_dev, "[ha_ctrl] SENSE INFO=%d, CKCON.\n", ha_bus.sense_info);
        HA_STAT(HA_CKCON, 0x60);
    } else {
        sim_debug(HA_TRACE, &ha_dev, "[ha_ctrl] NO SENSE INFO.\n");
        HA_STAT(HA_GOOD, CIO_SUCCESS);
    }

    /* Release the bus */
    scsi_release(&ha_bus);
}

void ha_fcm_express()
{
    uint32 rqp, cqp, cqs;

    rqp = cio[ha_state.cid].rqp;
    cqp = cio[ha_state.cid].cqp;
    cqs = cio[ha_state.cid].cqs;

    /* Write the fast completion entry. */
    pwrite_b(cqp + cq_offset, ha_state.reply.status);
    pwrite_b(cqp + cq_offset + 1, ha_state.reply.op);
    pwrite_b(cqp + cq_offset + 2, ha_state.reply.subdev);
    pwrite_b(cqp + cq_offset + 3, ha_state.reply.ssb);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_fcm_express] stat=%02x, op=%02x (%d), cq_index=%d target=%d, lun=%d, ssb=%02x\n",
              ha_state.reply.status, ha_state.reply.op, ha_state.reply.op,
              (cq_offset / 4),
              FC_TC(ha_state.reply.subdev), FC_LU(ha_state.reply.subdev),
              ha_state.reply.ssb);

    if (ha_state.pump_state == PUMP_COMPLETE && cqs > 0) {
        cq_offset = (cq_offset + 4) % (cqs * 4);
    } else {
        cq_offset = 0;
    }
}

/* Used for debugging only */
/* TODO: Remove after testing */
static void dump_edt()
{
    uint8 tc_size, lu_size, num_tc, num_lu, i, j;

    uint32 offset;

    char name[11];

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Sanity: %08x\n",
              ATOW(ha_state.edt, 0));

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Version: %d\n",
              ha_state.edt[4]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Slot: %d\n",
              ha_state.edt[5]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Max TC: %d\n",
              ha_state.edt[6]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  TC Size: %d\n",
              ha_state.edt[7]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Max LUs: %d\n",
              ha_state.edt[8]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  LU Size: %d\n",
              ha_state.edt[9]);

    sim_debug(HA_TRACE, &ha_dev,
              "[EDT]  Equipped TCs: %d\n",
              ha_state.edt[10]);

    tc_size = ha_state.edt[7];
    lu_size = ha_state.edt[9];
    num_tc = ha_state.edt[10] + 1;

    for (i = 0; i < num_tc; i++) {
        offset = 12 + (tc_size * i);
        num_lu = ha_state.edt[offset + 17];

        strncpy(name, ((const char *)ha_state.edt + offset + 4), 10);

        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      -------------------------\n");
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Major Number: %d\n",
                  i, ATOW(ha_state.edt, offset));
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Name: %s\n",
                  i, name);
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Type: %d\n",
                  i, ATOH(ha_state.edt, offset + 14));
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Equipped?: %d\n",
                  i, ha_state.edt[offset + 16]);
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Equipped LUs: %d\n",
                  i, ha_state.edt[offset + 17]);
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] Maximum LUs: %d\n",
                  i, ha_state.edt[offset + 18]);
        sim_debug(HA_TRACE, &ha_dev,
                  "[EDT]      [TC%d] LU Index: %04x\n",
                  i, ATOH(ha_state.edt, offset + 20));

        offset = ATOH(ha_state.edt, offset + 20);

        for (j = 0; j < num_lu; j++) {

            offset = offset + (j * lu_size);

            sim_debug(HA_TRACE, &ha_dev,
                      "[EDT]              -------------------------\n");
            sim_debug(HA_TRACE, &ha_dev,
                      "[EDT]              [TC%d,LU%d] LU #: %d\n",
                      i, j, ha_state.edt[offset]);
            sim_debug(HA_TRACE, &ha_dev,
                      "[EDT]              [TC%d,LU%d] PD Type: 0x%02x\n",
                      i, j, ha_state.edt[offset + 1]);
            sim_debug(HA_TRACE, &ha_dev,
                      "[EDT]              [TC%d,LU%d] Dev Type: 0x%02x\n",
                      i, j, ha_state.edt[offset + 2] >> 1);
            sim_debug(HA_TRACE, &ha_dev,
                      "[EDT]              [TC%d,LU%d] Removable?: %d\n",
                      i, j, ha_state.edt[offset + 2] & 1);
        }

    }

}
