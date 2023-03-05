/* 3b2_scsi.c: CM195W SCSI Controller CIO Card

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

#include "3b2_scsi.h"

#include "sim_scsi.h"
#include "sim_tape.h"

#include "3b2_cpu.h"
#include "3b2_io.h"
#include "3b2_mem.h"

#define DIAG_CRC_1 0x271b114c
#define PUMP_CRC   0x201b3617

#define HA_SCSI_ID      0
#define HA_MAXFR        (1u << 16)

static void ha_cmd(uint8 op, uint8 subdev, uint32 addr,
                   int32 len, t_bool express);
static void ha_build_req(uint8 tc, uint8 subdev, t_bool express);
static void ha_ctrl(uint8 tc);


HA_STATE ha_state;
SCSI_BUS ha_bus;
uint8    *ha_buf;
int8     ha_subdev_tab[8];    /* Map of subdevice to SCSI target */
uint8    ha_subdev_cnt;
uint32   ha_crc = 0;
uint32   cq_offset = 0;
t_bool   ha_conf = FALSE;

static DRVTYP ha_tab[] = {
    HA_DISK(SD155),
    HA_DISK(SD300),
    HA_DISK(SD327),
    HA_DISK(SD630),
    HA_TAPE(ST120),
    { 0 }
};

UNIT ha_unit[9] = {0};  /* SCSI ID 0-7 + CIO Unit */
#define SCSI_U_FLAGS  (UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_DIS+UNIT_ROABLE)

static UNIT *cio_unit  = &ha_unit[0];

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
    { "STAP",  MTSE_DBG_API,  "SCSI tape activity" },
    { NULL }
};

DEVICE ha_dev = {
    "SCSI",                                 /* name */
    ha_unit,                                /* units */
    NULL,                                   /* registers */
    ha_mod,                                 /* modifiers */
    8,                                      /* #units */
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

void ha_cio_reset(uint8 slot)
{
    sim_debug(HA_TRACE, &ha_dev,
              "Handling CIO reset\n");
    ha_state.pump_state = PUMP_NONE;
    ha_crc = 0;
}

t_stat ha_reset(DEVICE *dptr)
{
    uint8 slot;
    uint32 t;
    UNIT *uptr;
    t_stat r;
    static t_bool inited = FALSE;

    if (!inited) {
        inited = TRUE;
        for (t = 0; t < dptr->numunits - 1; t++) {
            uptr = dptr->units + t;
            uptr->action = &ha_svc;
            uptr->flags = SCSI_U_FLAGS;
            sim_disk_set_drive_type_by_name (uptr, "SD155");
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
        cio_remove_all(HA_ID);
        ha_conf = FALSE;
        return SCPE_OK;
    }

    if (!ha_conf) {
        r = cio_install(HA_ID, "SCSI", HA_IPL,
                        &ha_express, &ha_full, &ha_sysgen, &ha_cio_reset,
                        &slot);
        if (r != SCPE_OK) {
            return r;
        }
        ha_state.slot = slot;
        ha_conf = TRUE;
    }

    return SCPE_OK;
}

static void ha_calc_subdevs()
{
    uint32 tc;
    UNIT *uptr;

    ha_subdev_cnt = 0;

    memset(ha_subdev_tab, -1, 8);

    for (tc = 0; tc < 8; tc++) {
        uptr = &ha_unit[tc];
        if (uptr->flags & UNIT_ATT) {
            ha_subdev_tab[ha_subdev_cnt++] = tc;
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
    int8 i, tc = -1;
    uint8 svc_req = 0;
    ha_req *req = NULL;
    ha_resp *rep = NULL;

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_svc] SERVICE ROUTINE\n");

    /* Determine how many targets need servicing */
    for (i = 0; i < 8; i++) {
        if (ha_state.ts[i].pending) {
            if (req == NULL && rep == NULL) {
                tc = i;
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_svc] Found a job for target %d\n",
                          tc);
                req = &ha_state.ts[i].req;
                rep = &ha_state.ts[i].rep;
                ha_state.ts[i].pending = FALSE;
            }
            svc_req++;
        }
    }

    if (tc == -1) {
        return SCPE_OK;
    }

    switch(rep->type) {
    case HA_JOB_QUICK:
        ha_fcm_express(tc);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_svc] FAST MODE CQ: target=%d status=%02x op=%02x subdev=%02x ssb=%02x\n",
                  tc, rep->status, rep->op, rep->subdev, rep->ssb);
        break;
    case HA_JOB_EXPRESS:
    case HA_JOB_FULL:
        cqe.byte_count = rep->len;
        cqe.opcode = rep->status; /* Yes, status, not opcode! */
        cqe.subdevice = rep->subdev;
        cqe.address = rep->addr;

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_svc] CQE: target=%d, byte_count=%04x, opcode=%02x, subdevice=%02x, addr=%08x\n",
                  tc, cqe.byte_count, cqe.opcode, cqe.subdevice, cqe.address);

        if (rep->type == HA_JOB_EXPRESS) {
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_svc] EXPRESS MODE CQ: target=%d, byte_count=%02x "
                      "op=%02x subdev=%02x address=%08x\n",
                      tc, cqe.byte_count, cqe.opcode, cqe.subdevice, cqe.address);

            cio_cexpress(ha_state.slot, SCQCESIZE, &cqe, capp_data);
        } else {
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_svc] FULL MODE CQ: target=%d, status=%02x op=%02x subdev=%02x ssb=%02x\n",
                      tc, rep->status, rep->op, rep->subdev, rep->ssb);

            cio_cqueue(ha_state.slot, 0, SCQCESIZE, &cqe, capp_data);
        }
        break;
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_svc] IRQ for board %d (VEC=%d). PSW_CUR_IPL=%d\n",
              ha_state.slot, cio[ha_state.slot].ivec, PSW_CUR_IPL);

    CIO_SET_INT(ha_state.slot);

    /* There's more work to do after this job is done */
    if (svc_req > 1) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_svc] Scheduling job to handle another %d open requests\n",
                  svc_req - 1);
        sim_activate_abs(uptr, 1000);
    }

    return SCPE_OK;
}

void ha_sysgen(uint8 slot)
{
    uint32 sysgen_p;
    uint32 alert_buf_p;

    cq_offset = 0;

    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen] Handling Sysgen.\n");
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    rqp=%08x\n", cio[slot].rqp);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    cqp=%08x\n", cio[slot].cqp);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    rqs=%d\n", cio[slot].rqs);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    cqs=%d\n", cio[slot].cqs);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    ivec=%d\n", cio[slot].ivec);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    no_rque=%d\n", cio[slot].no_rque);

    sysgen_p = pread_w(SYSGEN_PTR, BUS_PER);
    alert_buf_p  = pread_w(sysgen_p + 12, BUS_PER);
    sim_debug(HA_TRACE, &ha_dev, "[ha_sysgen]    alert_bfr=%08x\n", alert_buf_p);

    ha_state.frq = (cio[slot].no_rque == 0);

    ha_state.ts[HA_SCSI_ID].rep.type = ha_state.frq ? HA_JOB_QUICK : HA_JOB_EXPRESS;
    ha_state.ts[HA_SCSI_ID].rep.addr = 0;
    ha_state.ts[HA_SCSI_ID].rep.len = 0;
    ha_state.ts[HA_SCSI_ID].rep.status = 3;
    ha_state.ts[HA_SCSI_ID].rep.op = 0;
    ha_state.ts[HA_SCSI_ID].pending = TRUE;

    if (ha_crc == PUMP_CRC) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_sysgen] PUMP: NEW STATE = PUMP_SYSGEN\n");
        ha_state.pump_state = PUMP_SYSGEN;
    } else {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_sysgen] PUMP: NEW STATE = PUMP_NONE\n");
        ha_state.pump_state = PUMP_NONE;
    }

    sim_activate_abs(cio_unit, 1000);
}

void ha_fast_queue_check()
{
    uint8 busy, op, subdev;
    uint32 addr, len, rqp;

    rqp = cio[ha_state.slot].rqp;

    busy    = pread_b(rqp, BUS_PER);
    op      = pread_b(rqp + 1, BUS_PER);
    subdev  = pread_b(rqp + 2, BUS_PER);
    /* 4-byte timeout value at rqp + 4 not used */
    addr    = pread_w(rqp + 8, BUS_PER);
    len     = pread_w(rqp + 12, BUS_PER);

    if (busy == 0xff || ha_state.pump_state != PUMP_COMPLETE) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_fast_queue_check] Job pending (opcode=0x%02x subdev=%02x)\n",
                  op, subdev);
        pwrite_b(rqp, 0, BUS_PER); /* Job has been taken */
        ha_cmd(op, subdev, addr, len, FALSE);
    }
}

void ha_express(uint8 slot)
{
    cio_entry rqe;
    uint8 rapp_data[RAPP_LEN] = {0};

    if (ha_state.frq) {
        ha_fast_queue_check();
    } else {
        cio_rexpress(slot, SCQRESIZE, &rqe, rapp_data);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_express] Handling Express Request. subdev=%02x\n",
                  rqe.subdevice);

        ha_cmd(rqe.opcode, rqe.subdevice, rqe.address, rqe.byte_count, TRUE);
    }
}

void ha_full(uint8 slot)
{
    sim_debug(HA_TRACE, &ha_dev,
              "[ha_full] Handling Full Request (INT3)\n");

    if (ha_state.pump_state == PUMP_SYSGEN) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_full] PUMP: NEW STATE = PUMP_COMPLETE\n");

        ha_state.pump_state = PUMP_COMPLETE;
    }

    if (ha_state.frq) {
        ha_fast_queue_check();
    } else {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_full] NON_FRQ NOT HANDLED\n");
    }
}

static void ha_boot_disk(UNIT *uptr, uint8 tc)
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
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    /* Store the Physical Descriptor (PD) block at well-known
       address 0x2004400 */
    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Storing PD block at 0x%08x.\n",
              HA_PDINFO_ADDR);
    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(HA_PDINFO_ADDR + i, buf[i], BUS_PER);
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
        pwrite_b(HA_BOOT_ADDR + i, buf[i], BUS_PER);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_disk] Done storing boot block at 0x%08x\n",
              HA_BOOT_ADDR);

    HA_STAT(tc, HA_GOOD, CIO_SUCCESS);

    ha_state.ts[tc].rep.addr = HA_BOOT_ADDR;
    ha_state.ts[tc].rep.len = HA_BLKSZ;
}

static void ha_boot_tape(UNIT *uptr, uint8 tc)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    if (!(uptr->flags & UNIT_ATT)) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Target not attached\n");
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rewind(uptr);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Could not rewind tape\n");
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rdrecf(uptr, buf, &sectsread, HA_BLKSZ); /* Read block 0 */

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_boot_tape] Could not read PD block.\n");
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(HA_BOOT_ADDR + i, buf[i], BUS_PER);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_boot_tape] Transfered 512 bytes to 0x%08x\n",
              HA_BOOT_ADDR);

    r = sim_tape_sprecf(uptr, &sectsread);           /* Skip block 1 */

    HA_STAT(tc, HA_GOOD, CIO_SUCCESS);

    ha_state.ts[tc].rep.addr = HA_BOOT_ADDR;
    ha_state.ts[tc].rep.len = HA_BLKSZ;
}

static void ha_read_block_tape(UNIT *uptr, uint32 addr, uint8 tc)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    if (!(uptr->flags & UNIT_ATT)) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_tape] Target not attached\n");
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    r = sim_tape_rdrecf(uptr, buf, &sectsread, HA_BLKSZ);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_tape] Could not read next block.\n");
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(addr + i, buf[i], BUS_PER);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_read_block_tape] Transfered 512 bytes to 0x%08x\n",
              addr);

    HA_STAT(tc, HA_GOOD, CIO_SUCCESS);

    ha_state.ts[tc].rep.addr = addr;
    ha_state.ts[tc].rep.len = HA_BLKSZ;
}

static void ha_read_block_disk(UNIT *uptr, uint32 addr, uint8 tc, uint32 lba)
{
    t_seccnt sectsread;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    r = sim_disk_rdsect(uptr, lba, buf, &sectsread, 1);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_block_disk] Could not read block %d\n",
                  lba);
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    for (i = 0; i < HA_BLKSZ; i++) {
        pwrite_b(addr + i, buf[i], BUS_PER);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_read_block_disk] Transferred 512 bytes to 0x%08x\n",
              addr);

    HA_STAT(tc, HA_GOOD, CIO_SUCCESS);

    ha_state.ts[tc].rep.addr = addr;
    ha_state.ts[tc].rep.len = HA_BLKSZ;
}

static void ha_write_block_disk(UNIT *uptr, uint32 addr, uint8 tc, uint32 lba)
{
    t_seccnt sectswritten;
    t_stat r;
    uint8 buf[HA_BLKSZ];
    uint32 i;

    for (i = 0 ; i < HA_BLKSZ; i++) {
        buf[i] = pread_b(addr + i, BUS_PER);
    }

    r = sim_disk_wrsect(uptr, lba, buf, &sectswritten, 1);

    if (r != SCPE_OK) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_write_block_disk] Could not write block %d\n",
                  lba);
        HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
        return;
    }

    HA_STAT(tc, HA_GOOD, CIO_SUCCESS);

    ha_state.ts[tc].rep.addr = addr;
    ha_state.ts[tc].rep.len = HA_BLKSZ;
}

static void ha_build_req(uint8 tc, uint8 subdev, t_bool express)
{
    uint32 i, rqp, ptr, dma_lst, daddr_ptr;
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
        ha_state.ts[tc].req.cmd[i] = 0;
    }

    if (ha_state.frq) {
        rqp = cio[ha_state.slot].rqp;

        subdev = pread_b(rqp + 2, BUS_PER);

        ha_state.ts[tc].req.tc = FC_TC(subdev);
        ha_state.ts[tc].req.lu = FC_LU(subdev);
        ha_state.ts[tc].req.timeout = pread_w(rqp + 4, BUS_PER);
        ha_state.ts[tc].req.cmd_len = pread_h(rqp + 18, BUS_PER);
        for (i = 0; i < HA_MAX_CMD; i++) {
            ha_state.ts[tc].req.cmd[i] = pread_b(rqp + 20 + i, BUS_PER);
        }
        ha_state.ts[tc].req.op = ha_state.ts[tc].req.cmd[0];

        /* Possible list of DMA scatter/gather addresses */
        dma_lst = pread_h(rqp + 16, BUS_PER) / 8;

        if (dma_lst) {
            t_bool link;

            /* There's a list of address / lengths. Each entry is 8
             * bytes long. */
            ptr = pread_w(rqp + 8, BUS_PER);
            link = FALSE;

            sim_debug(HA_TRACE, &ha_dev,
                      "[build_req] Building a list of scatter/gather addresses.\n");

            daddr_ptr = 0;

            for (i = 0; (i < dma_lst) || link; i++) {
                addr = pread_w(ptr, BUS_PER);
                len = pread_w(ptr + 4, BUS_PER);

                if (len == 0) {
                    sim_debug(HA_TRACE, &ha_dev,
                              "[build_req] Found length of 0, bailing early.\n");
                    break; /* Done early */
                }

                if (len > 0x1000) {
                    /* There's a new pointer in town */
                    ptr = pread_w(ptr, BUS_PER);
                    sim_debug(HA_TRACE, &ha_dev,
                              "[build_req] New ptr=%08x\n",
                              ptr);
                    link = TRUE;
                    continue;
                }

                sim_debug(HA_TRACE, &ha_dev,
                          "[build_req]   daddr[%d]: addr=%08x, len=%d (%x)\n",
                          daddr_ptr, addr, len, len);

                ha_state.ts[tc].req.daddr[daddr_ptr].addr = addr;
                ha_state.ts[tc].req.daddr[daddr_ptr].len = len;

                daddr_ptr++;
                ptr += 8;
            }

            ha_state.ts[tc].req.dlen = i;
        } else {
            /* There's only one embedded address / length */
            ha_state.ts[tc].req.daddr[0].addr = pread_w(rqp + 8, BUS_PER);
            ha_state.ts[tc].req.daddr[0].len = pread_w(rqp + 12, BUS_PER);
            ha_state.ts[tc].req.dlen = 1;
        }

    } else {
        if (express) {
            cio_rexpress(ha_state.slot, SCQRESIZE, &rqe, rapp_data);
        } else {
            /* TODO: Find correct queue number! */
            cio_rqueue(ha_state.slot, 0, SCQRESIZE, &rqe, rapp_data);
        }

        ptr = rqe.address;

        ha_state.ts[tc].req.tc = FC_TC(rqe.subdevice);
        ha_state.ts[tc].req.lu = FC_LU(rqe.subdevice);
        ha_state.ts[tc].req.cmd_len = pread_w(ptr + 4, BUS_PER);
        ha_state.ts[tc].req.timeout = pread_w(ptr + 8, BUS_PER);
        ha_state.ts[tc].req.daddr[0].addr = pread_w(ptr + 12, BUS_PER);
        ha_state.ts[tc].req.daddr[0].len = rqe.byte_count;
        ha_state.ts[tc].req.dlen = 1;

        sim_debug(HA_TRACE, &ha_dev,
                  "[build_req] [non-fast] Building a list of 1 scatter/gather addresses.\n");

        ptr = pread_w(ptr, BUS_PER);

        for (i = 0; (i < ha_state.ts[tc].req.cmd_len) && (i < HA_MAX_CMD); i++) {
            ha_state.ts[tc].req.cmd[i] = pread_b(ptr + i, BUS_PER);
        }

        ha_state.ts[tc].req.op = ha_state.ts[tc].req.cmd[0];
    }
}

static SIM_INLINE void ha_cmd_prep(uint8 tc, uint8 op, uint8 subdev, t_bool express)
{
    ha_state.ts[tc].pending = TRUE;
    ha_state.ts[tc].rep.op = op;
    ha_state.ts[tc].rep.subdev = subdev;
    ha_state.ts[tc].rep.status = CIO_FAILURE;
    ha_state.ts[tc].rep.ssb = 0;
    ha_state.ts[tc].rep.len = 0;
    ha_state.ts[tc].rep.addr = 0;

    if (ha_state.pump_state == PUMP_COMPLETE) {
        ha_state.ts[tc].rep.op |= 0x80;
    }

    if (ha_state.frq) {
        ha_state.ts[tc].rep.type = HA_JOB_QUICK;
    } else if (express) {
        ha_state.ts[tc].rep.type = HA_JOB_EXPRESS;
    } else {
        ha_state.ts[tc].rep.type = HA_JOB_FULL;
    }
}

static void ha_cmd(uint8 op, uint8 subdev, uint32 addr, int32 len, t_bool express)
{
    int32 i, block;
    UNIT *uptr;
    uint8 dsd_tc, tc;

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] --------------------------[START]---------------------------------\n");
    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] op=%02x (%d), subdev=%02x, addr=%08x, len=%d\n",
              op, op, subdev, addr, len);

    switch (op) {
    case CIO_DLM:
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        for (i = 0; i < len; i++) {
            ha_crc = cio_crc32_shift(ha_crc, pread_b(addr + i, BUS_PER));
        }

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x (CRC=%08x)\n",
                  len, addr, addr, subdev, ha_crc);
        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);
        break;
    case CIO_FCF:
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI Force Function Call. (CRC=%08x)\n",
                  ha_crc);
        if (ha_crc == DIAG_CRC_1) {
            pwrite_h(0x200f000, 0x1, BUS_PER);   /* Test success */
            pwrite_h(0x200f002, 0x0, BUS_PER);   /* Test Number */
            pwrite_h(0x200f004, 0x0, BUS_PER);   /* Actual */
            pwrite_h(0x200f006, 0x0, BUS_PER);   /* Expected */
            pwrite_b(0x200f008, 0x1, BUS_PER);   /* Success flag again */
        }

        cio[ha_state.slot].sysgen_s = 0;
        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);
        break;
    case CIO_DSD:
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI DSD - %d CONFIGURED DEVICES (writing to addr %08x).\n",
                  ha_subdev_cnt, addr);

        pwrite_h(addr, ha_subdev_cnt, BUS_PER);

        for (i = 0; i < ha_subdev_cnt; i++) {
            addr += 2;

            dsd_tc = ha_subdev_tab[i];

            if (dsd_tc < 0) {
                pwrite_h(addr, 0, BUS_PER);
                continue;
            }

            uptr = &ha_unit[dsd_tc];

            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_cmd] [DSD] Probing subdev %d, target %d, devtype %d\n",
                      i, dsd_tc, uptr->drvtyp->devtype);

            switch(uptr->drvtyp->devtype) {
            case SCSI_DISK:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Subdev %d is DISK (writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, HA_DSD_DISK, BUS_PER);
                break;
            case SCSI_TAPE:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Subdev %d is TAPE (writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, HA_DSD_TAPE, BUS_PER);
                break;
            default:
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_cmd] [DSD] Warning: No device type for subdev %d (Writing to addr %08x)\n",
                          i, addr);
                pwrite_h(addr, 0, BUS_PER);
                break;
            }
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_BOOT:
        tc = ha_subdev_tab[subdev & 7];
        ha_cmd_prep(tc, op, subdev, express);


        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] TARGET %d BOOTING.\n",
                  tc);

        if (tc < 0) {
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        uptr = &ha_unit[tc];

        if (!(uptr->flags & UNIT_ATT)) {
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_cmd] TARGET %d NOT ATTACHED.\n",
                      tc);
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        switch(uptr->drvtyp->devtype) {
        case SCSI_DISK:
            ha_boot_disk(uptr, tc);
            break;
        case SCSI_TAPE:
            ha_boot_tape(uptr, tc);
            break;
        default:
            sim_debug(HA_TRACE, &ha_dev,
                      "[HA_BOOT] Cannot boot target %d (not disk or tape).\n",
                      tc);
            break;
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);
        break;
    case HA_READ_BLK:
        tc = ha_subdev_tab[subdev & 7];
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SUBDEV %d TARGET %d READ BLOCK (BLOCK 0x%08x TO ADDR 0x%08x)\n",
                  subdev, tc, pread_w(addr, BUS_PER), pread_w(addr + 4, BUS_PER));

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blk]    addr = %08x\n",
                  addr);
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blk]    %08x = %08x\n",
                  addr, pread_w(addr, BUS_PER));
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blk]    %08x = %08x\n",
                  addr + 4, pread_w(addr + 4, BUS_PER));
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blk]    %08x = %08x\n",
                  addr + 8, pread_w(addr + 8, BUS_PER));
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blk]    %08x = %08x\n",
                  addr + 12, pread_w(addr + 12, BUS_PER));
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_read_blik]    %08x = %08x\n",
                  addr + 16, pread_w(addr + 16, BUS_PER));


        if (tc < 0) {
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        uptr = &ha_unit[tc];

        if (!(uptr->flags & UNIT_ATT)) {
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        block = pread_w(addr, BUS_PER);     /* Logical block we've been asked to read */
        addr = pread_w(addr + 4, BUS_PER);  /* Dereference the pointer to the destination */

        switch(uptr->drvtyp->devtype) {
        case SCSI_TAPE:
            ha_read_block_tape(uptr, addr, tc);
            break;
        case SCSI_DISK:
            ha_read_block_disk(uptr, addr, tc, block);
            break;
        default:
            sim_debug(HA_TRACE, &ha_dev,
                      "[HA_READ_BLOCK] Cannot read block %d on target %d (not disk or tape)\n",
                      block, tc);
            break;
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_WRITE_BLK:
        tc = ha_subdev_tab[subdev & 7];
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SUBDEV %d TARGET %d WRITE BLOCK (BLOCK 0x%08x FROM ADDR 0x%08x)\n",
                  subdev, tc, pread_w(addr, BUS_PER), pread_w(addr + 4, BUS_PER));

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_write_blk]    addr = %08x\n",
                  addr);
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_write_blk]    %08x = %08x\n",
                  addr, pread_w(addr, BUS_PER));
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_write_blk]    %08x = %08x\n",
                  addr + 4, pread_w(addr + 4, BUS_PER));

        if (tc < 0) {
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        uptr = &ha_unit[tc];

        if (!(uptr->flags & UNIT_ATT)) {
            ha_state.ts[tc].rep.status = CIO_TIMEOUT;
            sim_activate_abs(cio_unit, 1000);
            return;
        }

        block = pread_w(addr, BUS_PER);     /* Logical block we've been asked to write */
        addr = pread_w(addr + 4, BUS_PER);  /* Dereference the pointer to the source */

        switch(uptr->drvtyp->devtype) {
        case SCSI_DISK:
            ha_write_block_disk(uptr, addr, tc, block);
            break;
        default:
            sim_debug(HA_TRACE, &ha_dev,
                      "[ha_write_blk] Cannot write block %d on target %d (not disk)\n",
                      block, tc);
            break;
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);
        break;
    case HA_CNTRL:
        tc = FC_TC(subdev);
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI CONTROL (subdev=%02x addr=%08x)\n",
                  subdev, addr);

        ha_build_req(tc, subdev, express);
        ha_ctrl(tc);
        sim_activate_abs(cio_unit, 1000);
        break;
    case HA_VERS:
        /*
         * Get Host Adapter Version
         */
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI GET VERSION (addr=%08x len=%08x)\n",
                  addr, len);

        pwrite_w(addr, HA_VERSION, BUS_PER);
        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_DL_EEDT:
        /*
         * This is a request to download the Extended Equipped Device
         * Table from the host adapter to the 3B2 main memory.
         */
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI DOWNLOAD EDT (%d bytes to address %08x)\n",
                  len, addr);

        for (i = 0; i < len; i++) {
            pwrite_b(addr + i, ha_state.edt[i], BUS_PER);
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_UL_EEDT:
        /*
         * This is a request to upload the Extended Equipped Device
         * Table from the 3B2 main memory to the host adapter
         */

        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI UPLOAD EDT (%d bytes from address %08x)\n",
                  len, addr);

        for (i = 0; i < len; i++) {
            ha_state.edt[i] = pread_b(addr + i, BUS_PER);
        }

        ha_state.ts[tc].rep.status = CIO_SUCCESS;

        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_EDSD:
        /*
         * This command is used to determine which TCs are attached to
         * the SCSI bus, and what LUNs they support.
         */
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI EXTENDED DSD.\n");

        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        ha_state.ts[tc].rep.addr = addr;
        ha_state.ts[tc].rep.len = 9;

        /*
         * Loop over each SCSI ID and configure LUNs.
         */
        for (i = 0; i < 8; i++) {
            uptr = &ha_unit[i];
            /*
             * TODO: The byte being written here is a bit mask of
             * equipped luns. e.g.,
             *
             *   - 0x01 means LUN 0 is equipped,
             *   - 0x80 means LUN 7 is equipped,
             *   - 0x33 means LUNs 0, 1, 4, and 5 are equipped.
             *
             * For now, we only support one LUN per target, and it's
             * always LUN 0.
             */
            pwrite_b(addr + i, (uptr->flags & UNIT_ATT) ? 1 : 0, BUS_PER);
        }

        pwrite_b(addr + 8, HA_SCSI_ID, BUS_PER);   /* ID of the card */

        sim_activate_abs(cio_unit, 1000);

        break;
    case HA_RESET:
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        scsi_reset(&ha_bus);

        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_cmd] SCSI RESET.\n");

        ha_state.ts[tc].rep.status = CIO_SUCCESS;
        ha_state.ts[tc].rep.addr = addr;
        ha_state.ts[tc].rep.len = 0;

        sim_activate_abs(cio_unit, 1000);
        break;
    default:
        tc = HA_SCSI_ID;
        ha_cmd_prep(tc, op, subdev, express);

        sim_debug(HA_TRACE, &ha_dev,
                  "*** SCSI WARNING: UNHANDLED OPCODE 0x%02x\n",
                  op);

        ha_state.ts[tc].rep.status = CIO_FAILURE;

        sim_activate_abs(cio_unit, 1000);
    }

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_cmd] ---------------------------[END]----------------------------------\n");

}

/*
 * Handle a raw SCSI control message.
 */
void ha_ctrl(uint8 tc)
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
              ha_state.ts[tc].req.tc,
              ha_state.ts[tc].req.lu,
              ha_state.ts[tc].req.timeout,
              ha_state.ts[tc].req.dlen);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_ctrl] [HA_REQ] CMD_LEN=%d CMD=<%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x>\n",
              ha_state.ts[tc].req.cmd_len,
              ha_state.ts[tc].req.cmd[0], ha_state.ts[tc].req.cmd[1],
              ha_state.ts[tc].req.cmd[2], ha_state.ts[tc].req.cmd[3],
              ha_state.ts[tc].req.cmd[4], ha_state.ts[tc].req.cmd[5],
              ha_state.ts[tc].req.cmd[6], ha_state.ts[tc].req.cmd[7],
              ha_state.ts[tc].req.cmd[8], ha_state.ts[tc].req.cmd[9]);

    in_len = out_len = 0;

    /*
     * These ops need special handling.
     */
    switch(ha_state.ts[tc].req.op) {
    case HA_TESTRDY:
        /* Fail early if LU is set */
        if (ha_state.ts[tc].req.lu) {
            HA_STAT(tc, HA_CKCON, CIO_TIMEOUT);
            return;
        }
        break;
    case HA_FORMAT:   /* Not yet handled by sim_scsi library */
    case HA_VERIFY:   /* Not yet handled by sim_scsi library */
        /* Just mimic success */
        HA_STAT(tc, HA_GOOD, CIO_SUCCESS);
        return;
    }

    /* Get the bus's attention */
    if (!scsi_arbitrate(&ha_bus, HA_SCSI_ID)) {
        HA_STAT(tc, HA_CKCON, CIO_TIMEOUT);
        return;
    }

    scsi_set_atn(&ha_bus);

    if (!scsi_select(&ha_bus, ha_state.ts[tc].req.tc)) {
        HA_STAT(tc, HA_CKCON, CIO_TIMEOUT);
        scsi_release(&ha_bus);
        return;
    }

    /* Select the correct LU */
    lu = 0x80 | ha_state.ts[tc].req.lu;
    scsi_write(&ha_bus, &lu, 1);

    txn_done = FALSE;

    /* TODO: Fix this. Work around a bug in command length. The host
     * occasionally sends a command length of 8 for 6-byte SCSI
     * commands. The sim_scsi library knows to only consume 6 bytes,
     * which leaves the buffer in a bad state. */
    if (ha_state.ts[tc].req.cmd_len == 8) {
        ha_state.ts[tc].req.cmd_len = 6;
    }

    while (!txn_done) {
        switch(ha_bus.phase) {
        case SCSI_CMD:
            plen = scsi_write(&ha_bus, ha_state.ts[tc].req.cmd, ha_state.ts[tc].req.cmd_len);
            if (plen < ha_state.ts[tc].req.cmd_len) {
                HA_STAT(tc, HA_CKCON, CIO_SUCCESS);
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
            switch(ha_state.ts[tc].req.op) {
            case HA_READ:
            case HA_READEXT:
                ha_ptr = 0;

                for (i = 0; i < ha_state.ts[tc].req.dlen; i++) {
                    /*
                     * Consume the lesser of:
                     *   - The total bytes we consumed, or:
                     *   - The length of the current block
                     */
                    to_read = MIN(ha_state.ts[tc].req.daddr[i].len, in_len);

                    sim_debug(HA_TRACE, &ha_dev,
                              "[(%02x) TC%d,LU%d] DATI: Processing %d bytes to address %08x...\n",
                              ha_state.ts[tc].req.op,
                              ha_state.ts[tc].req.tc,
                              ha_state.ts[tc].req.lu,
                              to_read,
                              ha_state.ts[tc].req.daddr[i].addr);

                    for (j = 0; j < to_read; j++) {
                        pwrite_b(ha_state.ts[tc].req.daddr[i].addr + j, ha_buf[ha_ptr++], BUS_PER);
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
                          ha_state.ts[tc].req.op, ha_state.ts[tc].req.tc,
                          ha_state.ts[tc].req.lu, in_len,
                          ha_state.ts[tc].req.daddr[0].addr);
                for (i = 0; i < in_len; i++) {
                    sim_debug(HA_TRACE, &ha_dev, "[%04x] [DATI] 0x%02x\n", i, ha_buf[i]);
                    pwrite_b(ha_state.ts[tc].req.daddr[0].addr + i, ha_buf[i], BUS_PER);
                }

                break;
            }

            break;
        case SCSI_DATO:
            /* This is a write */
            ha_ptr = 0;
            out_len = 0;
            ha_state.ts[tc].rep.len = ha_state.ts[tc].req.dlen;

            for (i = 0; i < ha_state.ts[tc].req.dlen; i++) {
                sim_debug(HA_TRACE, &ha_dev,
                          "[ha_ctrl] [%d] DATO: Writing %d bytes to ha_buf.\n",
                          i, ha_state.ts[tc].req.daddr[i].len);

                for (j = 0; j < ha_state.ts[tc].req.daddr[i].len; j++) {
                    ha_buf[ha_ptr++] = pread_b(ha_state.ts[tc].req.daddr[i].addr + j, BUS_PER);
                    if (ha_state.ts[tc].req.op == 0x15) {
                        sim_debug(HA_TRACE, &ha_dev,
                                  "[ha_ctrl] [%d]\t\t%02x\n",
                                  j, ha_buf[ha_ptr - 1]);
                    }
                }

                out_len += ha_state.ts[tc].req.daddr[i].len;
            }

            if (ha_state.ts[tc].req.op == HA_WRITE || ha_state.ts[tc].req.op == HA_WRTEXT) {
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

    if (ha_bus.sense_key || ha_bus.sense_code) {
        sim_debug(HA_TRACE, &ha_dev,
                  "[ha_ctrl] SENSE KEY=%d CODE=%d INFO=%d, CKCON.\n",
                  ha_bus.sense_key, ha_bus.sense_code, ha_bus.sense_info);
        HA_STAT(tc, HA_CKCON, 0x60);
    } else {
        sim_debug(HA_TRACE, &ha_dev, "[ha_ctrl] NO SENSE INFO.\n");
        HA_STAT(tc, HA_GOOD, CIO_SUCCESS);
    }

    /* Release the bus */
    scsi_release(&ha_bus);
}

void ha_fcm_express(uint8 tc)
{
    uint32 cqp, cqs;

    cqp = cio[ha_state.slot].cqp;
    cqs = cio[ha_state.slot].cqs;

    /* Write the fast completion entry. */
    pwrite_b(cqp + cq_offset, ha_state.ts[tc].rep.status, BUS_PER);
    pwrite_b(cqp + cq_offset + 1, ha_state.ts[tc].rep.op, BUS_PER);
    pwrite_b(cqp + cq_offset + 2, ha_state.ts[tc].rep.subdev, BUS_PER);
    pwrite_b(cqp + cq_offset + 3, ha_state.ts[tc].rep.ssb, BUS_PER);

    sim_debug(HA_TRACE, &ha_dev,
              "[ha_fcm_express] stat=%02x, op=%02x (%d), cq_index=%d target=%d, lun=%d, ssb=%02x\n",
              ha_state.ts[tc].rep.status, ha_state.ts[tc].rep.op, ha_state.ts[tc].rep.op,
              (cq_offset / 4),
              FC_TC(ha_state.ts[tc].rep.subdev), FC_LU(ha_state.ts[tc].rep.subdev),
              ha_state.ts[tc].rep.ssb);

    if (ha_state.pump_state == PUMP_COMPLETE && cqs > 0) {
        cq_offset = (cq_offset + 4) % (cqs * 4);
    } else {
        cq_offset = 0;
    }
}

