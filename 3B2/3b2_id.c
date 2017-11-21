/* 3b2_cpu.h: AT&T 3B2 Model 400 Hard Disk (uPD7261) Implementation

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

/*
 * The larger of the two hard drive options shipped with the AT&T 3B2
 * was a 72MB Wren II ST-506 MFM hard disk. That's what we emulate
 * here, as a start.
 *
 *   Formatted Capacity: 76,723,200 Bytes
 *
 *   Cylinders:     925
 *   Sectors/Track:  18
 *   Heads:           9
 *   Bytes/Sector:  512
 *   Avg. seek:      28 ms
 *   Seek/track:      5 ms
 *
 *   Drive Information from the 3B2 FAQ:
 *
 *   Drive type      drv id  cyls trk/cyl sec/trk byte/cyl    abbrev
 *   --------------- ------  ---- ------- ------- --------    ------
 *   Wren II 30MB       3     697     5      18     512       HD30
 *   Wren II 72MB       5     925     9      18     512       HD72
 *   Fujitsu M2243AS    8     754    11      18     512       HD72C
 *   Micropolis 1325    5    1024     8      18     512       HD72
 *   Maxtor 1140        4*    918=   15      18     512       HD120
 *   Maxtor 1190        11   1224+   15      18     512       HD135
 *
 */

#include <assert.h>
#include "3b2_id.h"

/* Wait times, in CPU steps, for various actions */

/* Each step is 50 us in buffered mode */
#define ID_SEEK_WAIT        100    /* us */
#define ID_SEEK_BASE        700    /* us */
#define ID_RECAL_WAIT       6000   /* us */

/* Reading data takes about 8ms per sector, plus time to seek if not
   on cylinder */
#define ID_RW_WAIT          8000   /* us */

/* Sense Unit Status completes in about 200 us */
#define ID_SUS_WAIT         200    /* us */

/* Specify takes a bit longer, 1.25 ms */
#define ID_SPEC_WAIT        1250   /* us */

/* Sense Interrupt Status is about 142 us */
#define ID_SIS_WAIT         142    /* us */

/* The catch-all command wait time is about 140 us */
#define ID_CMD_WAIT         140    /* us */

/* State. The DP7261 supports four MFM (winchester) disks connected
   simultaneously.  There is only one set of registers, however, so
   commands must be completed for one unit before they can begin on
   another unit. */

/* Data FIFO pointer - Read */
uint8    id_dpr = 0;
/* Data FIFO pointer - Write */
uint8    id_dpw = 0;
/* Selected unit */
uint8    id_sel = 0;
/* Controller Status Register */
uint8    id_status = 0;
/* Unit Interrupt Status */
uint8    id_int_status;
/* Last command received */
uint8    id_cmd = 0;
/* DMAC request */
t_bool   id_drq = FALSE;
/* 8-byte FIFO */
uint8    id_data[ID_FIFO_LEN] = {0};
/* INT output pin */
t_bool   id_irq = FALSE;
/* Special flag for seek end SIS */
t_bool   id_seek_sis = FALSE;

/* State of each drive */

/* Cylinder the drive is positioned on */
uint16   id_cyl[ID_NUM_UNITS] = {0};

/* DTLH byte for each drive */
uint8    id_dtlh[ID_NUM_UNITS] = {0};

/* Arguments of last READ, WRITE, VERIFY ID, or READ ID command */

/* Ending Track Number (from Specify) */
uint8    id_etn = 0;
/* Ending Sector Number (from Specify) */
uint8    id_esn = 0;
/* Physical sector number */
uint8    id_psn = 0;
/* Physical head number */
uint8    id_phn = 0;
/* Logical cylinder number, high byte */
uint8    id_lcnh = 0;
/* Logical cylinder number, low byte */
uint8    id_lcnl = 0;
/* Logical head number */
uint8    id_lhn = 0;
/* Logical sector number */
uint8    id_lsn = 0;
/* Number of sectors to transfer, decremented after each sector */
uint8    id_scnt = 0;
/* Sector buffer */
uint8    id_buf[ID_SEC_SIZE];
/* Buffer pointer */
size_t   id_buf_ptr = 0;

uint8    id_idfield[ID_IDFIELD_LEN];
uint8    id_idfield_ptr = 0;

/*
 * TODO: Macros used for debugging timers. Remove when debugging is complete.
 */
double id_start_time;

#define ID_START_TIME() { id_start_time = sim_gtime(); }
#define ID_DIFF_MS()    ((sim_gtime() - id_start_time) / INST_PER_MS)

UNIT id_unit[] = {
    {UDATA (&id_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BINK, ID_DSK_SIZE), 0, 0 },
    {UDATA (&id_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BINK, ID_DSK_SIZE), 0, 1 },
    { NULL }
};

/* The currently selected drive number */
UNIT    *id_sel_unit = &id_unit[0];

REG id_reg[] = {
    { HRDATAD(CMD,  id_cmd,    8, "Command") },
    { HRDATAD(STAT, id_status, 8, "Status")  },
    { BRDATAD(CYL,  id_cyl,    8, 8, ID_NUM_UNITS, "Track")   },
    { NULL }
};

DEVICE id_dev = {
    "ID", id_unit, id_reg, NULL,
    ID_NUM_UNITS, 16, 32, 1, 16, 8,
    NULL, NULL, &id_reset,
    NULL, &id_attach, &id_detach, NULL,
    DEV_DEBUG|DEV_SECTORS, 0, sys_deb_tab,
    NULL, NULL, &id_help, NULL, NULL,
    &id_description
};

/* Function implementation */

static SIM_INLINE void id_activate(uint32 delay)
{
    ID_START_TIME();
    sim_activate_abs(id_sel_unit, (int32) delay);
}

static SIM_INLINE void id_clear_fifo()
{
    id_dpr = 0;
    id_dpw = 0;
}

t_stat id_svc(UNIT *uptr)
{
    /* Complete the last command */
    id_status = ID_STAT_CEH;

    switch (CMD_NUM) {
    case ID_CMD_SEEK:   /* fall-through */
    case ID_CMD_RECAL:
        /* SRQ is only set in polling mode (POL bit is 0) */
        if ((id_dtlh[UNIT_NUM] & ID_DTLH_POLL) == 0) {
            id_status |= ID_STAT_SRQ;
        }
        if (uptr->flags & UNIT_ATT) {
            id_int_status = ID_IST_SEN|(uint8)uptr->ID_UNIT_NUM;
        } else {
            id_int_status = ID_IST_NR|(uint8)uptr->ID_UNIT_NUM;
        }
        break;
    case ID_CMD_SIS:
        if (!id_seek_sis) {
            id_status = ID_STAT_CEL;
        }
        id_seek_sis = FALSE;
        id_data[0] = id_int_status;
        id_status &= ~ID_STAT_SRQ;
        break;
    case ID_CMD_SUS:
        if ((id_sel_unit->flags & UNIT_ATT) == 0) {
            /* If no HD is attached, SUS puts 0x00 into the data
               buffer */
            id_data[0] = 0;
        } else {
            /* Put Unit Status into byte 0 */
            id_data[0] = (ID_UST_DSEL|ID_UST_SCL|ID_UST_RDY);
            if (id_cyl[UNIT_NUM] == 0) {
                id_data[0] |= ID_UST_TK0;
            }
        }
        break;
    default:
        break;
    }

    sim_debug(EXECUTE_MSG, &id_dev,
              "[%08x] \tINTR\t\tDELTA=%f ms\n",
              R[NUM_PC], ID_DIFF_MS());

    id_irq = TRUE;

    return SCPE_OK;
}

t_stat id_reset(DEVICE *dptr)
{
    id_clear_fifo();
    return SCPE_OK;
}

t_stat id_attach(UNIT *uptr, CONST char *cptr)
{
    return sim_disk_attach(uptr, cptr, 512, 1, TRUE, 0, "HD72", 0, 0);
}

t_stat id_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}

/* Return the logical block address of the given sector */
static SIM_INLINE t_lba id_lba(uint16 cyl, uint8 head, uint8 sec)
{
    return((ID_SEC_CNT * ID_HEADS * cyl) +
           (ID_SEC_CNT * head) +
           sec);
}

/* At the end of each sector read or write, we update the FIFO
 * with the correct return parameters. */
static void SIM_INLINE id_end_rw(uint8 est) {
    sim_debug(EXECUTE_MSG, &id_dev,
              ">>> ending R/W with status: %02x\n",
              est);
    id_dpr = 0;
    id_dpw = 0;
    id_data[0] = est;
    id_data[1] = id_phn;
    id_data[2] = ~(id_lcnh);
    id_data[3] = id_lcnl;
    id_data[4] = id_lhn;
    id_data[5] = id_lsn;
    id_data[6] = id_scnt;
}

/* The controller wraps id_lsn, id_lhn, and id_lcnl on each sector
 * read, so that they point to the next C/H/S */
static void SIM_INLINE id_update_chs() {
    sim_debug(EXECUTE_MSG, &id_dev,
              ">>> id_update_chs(): id_esn=%02x id_etn=%02x\n",
              id_esn, id_etn);

    if (id_lsn++ >= id_esn) {
        sim_debug(EXECUTE_MSG, &id_dev,
                  ">>> id_update_chs(): id_lsn reset to 0. id_lhn is %02x\n",
                  id_lhn);
        id_lsn = 0;
        if (id_lhn++ >= id_etn) {
            sim_debug(EXECUTE_MSG, &id_dev,
                      ">>> id_update_chs(): id_lhn reset to 0. id_lcnl is %02x\n",
                      id_lcnl);
            id_lhn = 0;
            if (id_lcnl == 0xff) {
                id_lcnl = 0;
                id_lcnh++;
            } else {
                id_lcnl++;
            }
        }
    }
}

uint32 id_read(uint32 pa, size_t size) {
    uint8 reg;
    uint16 cyl;
    t_lba lba;
    uint32 data;
    t_seccnt sectsread;

    reg = (uint8) (pa - IDBASE);

    switch(reg) {
    case ID_DATA_REG:     /* Data Buffer Register */
        /* If we're in a DMA transfer, we need to be reading data from
         * the disk buffer. Otherwise, we're reading from the FIFO. */

        if (id_drq) {
            /* If the drive isn't attached, there's really nothing we
               can do. */
            if ((id_sel_unit->flags & UNIT_ATT) == 0) {
                id_end_rw(ID_EST_NR);
                return 0;
            }

            /* We could be in one of these commands:
             *    - Read Data
             *    - Read ID
             */

            if (CMD_NUM == ID_CMD_RDATA) {
                /* If we're still in DRQ but we've read all our sectors,
                 * that's an error state. */
                if (id_scnt == 0) {
                    sim_debug(READ_MSG, &id_dev,
                              "[%08x] ERROR\tid_scnt = 0 but still in dma\n",
                              R[NUM_PC]);
                    id_end_rw(ID_EST_OVR);
                    return 0;
                }

                /* If the disk buffer is empty, fill it. */
                if (id_buf_ptr == 0 || id_buf_ptr >= ID_SEC_SIZE) {
                    /* It's time to read a new sector into our sector buf */
                    id_buf_ptr = 0;
                    cyl = (uint16) (((uint16)id_lcnh << 8)|(uint16)id_lcnl);
                    id_cyl[UNIT_NUM] = cyl;
                    lba = id_lba(cyl, id_lhn, id_lsn);
                    if (sim_disk_rdsect(id_sel_unit, lba, id_buf, &sectsread, 1) == SCPE_OK) {
                        if (sectsread !=1) {
                            sim_debug(READ_MSG, &id_dev,
                                      "[%08x]\tERROR: ASKED TO READ ONE SECTOR, READ: %d\n",
                                      R[NUM_PC], sectsread);
                        }
                        sim_debug(READ_MSG, &id_dev,
                                  "[%08x] \tRDATA\tCYL=%d PHN=%d LCNH=%02x "
                                  "LCNL=%02x LHN=%d LSN=%d SCNT=%d LBA=%04x\n",
                                  R[NUM_PC], cyl, id_phn, id_lcnh, id_lcnl,
                                  id_lhn, id_lsn, id_scnt-1, lba);
                        id_update_chs();
                    } else {
                        /* Uh-oh! */
                        sim_debug(READ_MSG, &id_dev,
                                  "[%08x]\tRDATA READ ERROR. Failure from sim_disk_rdsect!\n",
                                  R[NUM_PC]);
                        id_end_rw(ID_EST_DER);
                        return 0;
                    }
                }

                data = id_buf[id_buf_ptr++];

                sim_debug(READ_MSG, &id_dev,
                          "[%08x]\tSECTOR DATA\t%02x\t(%c)\n",
                          R[NUM_PC], data, (data >= 0x20 && data < 0x7f) ? data : '.');

                /* Done with this current sector, update id_scnt */
                if (id_buf_ptr >= ID_SEC_SIZE) {
                    if (--id_scnt == 0) {
                        id_end_rw(0);
                    }
                }
            } else if (CMD_NUM == ID_CMD_RID) {
                /* We have to return the ID bytes for the current C/H/S */
                if (id_idfield_ptr == 0 || id_idfield_ptr >= ID_IDFIELD_LEN) {
                    id_idfield[0] = ~(id_lcnh);
                    id_idfield[1] = id_lcnl;
                    id_idfield[2] = id_lhn;
                    id_idfield[3] = id_lsn;
                    id_idfield_ptr = 0;
                }

                data = id_idfield[id_idfield_ptr++];
                sim_debug(READ_MSG, &id_dev,
                          "[%08x]\tID DATA\t%02x\n",
                          R[NUM_PC], data);

                if (id_idfield_ptr >= ID_IDFIELD_LEN) {
                    if (id_scnt-- > 0) {
                        /* Another sector to ID */
                        id_idfield_ptr = 0;
                    } else {
                        /* All done, set return codes */
                        id_dpr = 0;
                        id_dpw = 0;
                        id_data[0] = 0;
                        id_data[1] = id_scnt;
                    }
                }
            } else {
                assert(0); // cmd not Read Data or Read ID
            }

            return data;
        } else {
            if (id_dpr < ID_FIFO_LEN) {
                sim_debug(READ_MSG, &id_dev,
                          "[%08x]\tDATA\t%02x\n",
                          R[NUM_PC], id_data[id_dpr]);
                return id_data[id_dpr++];
            } else {
                sim_debug(READ_MSG, &id_dev,
                          "[%08x] ERROR\tFIFO OVERRUN\n",
                          R[NUM_PC]);
                return 0;
            }
        }

        break;
    case ID_CMD_STAT_REG:     /* Status Register */
        sim_debug(READ_MSG, &id_dev,
                  "[%08x]\tSTATUS\t%02x\n",
                  R[NUM_PC], id_status|id_drq);
        return id_status|(id_drq ? 1u : 0);
    }

    sim_debug(READ_MSG, &id_dev,
              "[%08x] Read of unsuported register %x\n",
              R[NUM_PC], id_status);

    return 0;
}

void id_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg;
    uint16 cyl;
    t_lba lba;
    t_seccnt sectswritten;

    reg = (uint8) (pa - IDBASE);

    switch(reg) {
    case ID_DATA_REG:
        /* If we're in a DMA transfer, we need to be writing data to
         * the disk buffer. Otherwise, we're writing to the FIFO. */

        if (id_drq) {
            /* If we're still in DRQ but we've written all our sectors,
             * that's an error state. */
            if (id_scnt == 0) {
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x] ERROR\tid_scnt = 0 but still in dma\n",
                          R[NUM_PC]);
                id_end_rw(ID_EST_OVR);
                return;
            }

            /* Write to the disk buffer */
            if (id_buf_ptr < ID_SEC_SIZE) {
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x]\tSECTOR DATA\t%02x\t(%c)\n",
                          R[NUM_PC], val, (val >= 0x20 && val < 0x7f) ? val : '.');
                id_buf[id_buf_ptr++] = (uint8)(val & 0xff);
            } else {
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x] ERROR\tWDATA OVERRUN\n",
                          R[NUM_PC]);
                id_end_rw(ID_EST_OVR);
                return;
            }

            /* If we've hit the end of a sector, flush it */
            if (id_buf_ptr >= ID_SEC_SIZE) {
                /* It's time to start the next sector, and flush the old. */
                id_buf_ptr = 0;
                cyl = (uint16) (((uint16) id_lcnh << 8)|(uint16)id_lcnl);
                id_cyl[UNIT_NUM] = cyl;
                lba = id_lba(cyl, id_lhn, id_lsn);
                if (sim_disk_wrsect(id_sel_unit, lba, id_buf, &sectswritten, 1) == SCPE_OK) {
                    if (sectswritten !=1) {
                        sim_debug(WRITE_MSG, &id_dev,
                                  "[%08x]\tERROR: ASKED TO WRITE ONE SECTOR, WROTE: %d\n",
                                  R[NUM_PC], sectswritten);
                    }
                    sim_debug(WRITE_MSG, &id_dev,
                              "[%08x]\tWDATA\tCYL=%d PHN=%d LCNH=%02x "
                              "LCNL=%02x LHN=%d LSN=%d SCNT=%d LBA=%04x\n",
                              R[NUM_PC], cyl, id_phn, id_lcnh, id_lcnl,
                              id_lhn, id_lsn, id_scnt, lba);
                    id_update_chs();
                    if (--id_scnt == 0) {
                        id_end_rw(0);
                    }
                } else {
                    /* Uh-oh! */
                    sim_debug(WRITE_MSG, &id_dev,
                              "[%08x] ERROR\tWDATA WRITE ERROR. lba=%04x\n",
                              R[NUM_PC], lba);
                    id_end_rw(ID_EST_DER);
                    return;
                }
            }

            return;
        } else {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tDATA\t%02x\n",
                      R[NUM_PC], val);

            if (id_dpw < ID_FIFO_LEN) {
                id_data[id_dpw++] = (uint8) val;
            } else {
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x] ERROR\tFIFO OVERRUN\n",
                          R[NUM_PC]);
            }
        }
        return;
    case ID_CMD_STAT_REG:
        id_handle_command((uint8) val);
        return;
    default:
        return;
    }
}

void id_handle_command(uint8 val)
{
    uint8 cmd, aux_cmd, sec, pattern;
    uint16 cyl;
    uint32 time;
    t_lba lba;

    /* Save the full command byte */
    id_cmd = val;

    /* Reset the FIFO pointer */
    id_dpr = 0;
    id_dpw = 0;

    /* Writing a command always de-asserts INT output, UNLESS
       the SRQ bit is set. */
    if ((id_status & ID_STAT_SRQ) != ID_STAT_SRQ) {
        id_irq = FALSE;
    }

    /* Is this an aux command or a full command? */
    if ((val & 0xf0) == 0) {
        aux_cmd = val & 0x0f;
        id_status &= ~(ID_STAT_CB);

        if (aux_cmd & ID_AUX_CLCE) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x] \tCOMMAND\t%02x\tAUX:CLCE\n",
                      R[NUM_PC], val);
            id_status &= ~(ID_STAT_CEL|ID_STAT_CEH);
            sim_cancel(id_sel_unit);
        }

        if (aux_cmd & ID_AUX_HSRQ) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x] \tCOMMAND\t%02x\tAUX:HSRQ\n",
                      R[NUM_PC], val);
            id_status &= ~ID_STAT_SRQ;
            sim_cancel(id_sel_unit);
        }

        if (aux_cmd & ID_AUX_CLB) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tAUX:CLBUF\n",
                      R[NUM_PC], val);
            id_clear_fifo();
        }

        if (aux_cmd & ID_AUX_RST) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tAUX:RESET\n",
                      R[NUM_PC], val);
            sim_cancel(id_sel_unit);
            id_clear_fifo();
        }

        /* Just return early */
        return;
    }

    /* Now that we know it's not an aux command, get the unit number
       this command is for */
    id_sel_unit = &id_unit[UNIT_NUM];

    cmd = (id_cmd >> 4) & 0xf;

    /* If this command is anything BUT a sense interrupt status, set
     * the seek flag to false.
     */
    if (cmd != ID_CMD_SIS) {
        id_seek_sis = FALSE;
    }

    id_status = ID_STAT_CB;

    switch(cmd) {
    case ID_CMD_SIS:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSense Int. Status - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_SIS_WAIT));
        break;
    case ID_CMD_SPEC:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSpecify - %d - ETN=%02x ESN=%02x\n",
                  R[NUM_PC], val, UNIT_NUM, id_data[3], id_data[4]);
        id_dtlh[UNIT_NUM] = id_data[1];
        id_etn = id_data[3];
        id_esn = id_data[4];
        id_activate(DELAY_US(ID_SPEC_WAIT));
        break;
    case ID_CMD_SUS:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSense Unit Status - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_SUS_WAIT));
        break;
    case ID_CMD_DERR:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tDetect Error - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_status |= ID_STAT_CEH;
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_RECAL:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRecalibrate - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_cyl[UNIT_NUM] = 0;
        time = id_cyl[UNIT_NUM];
        id_activate(DELAY_US(ID_RECAL_WAIT + (time * ID_SEEK_WAIT)));
        id_seek_sis = TRUE;
        break;
    case ID_CMD_SEEK:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSeek - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_lcnh = id_data[0];
        id_lcnl = id_data[1];
        cyl = id_lcnh << 8 | id_lcnl;
        time = (uint32) abs(id_cyl[UNIT_NUM] - cyl);
        id_activate(DELAY_US(ID_SEEK_BASE + (ID_SEEK_WAIT * time)));
        id_cyl[UNIT_NUM] = cyl;
        id_seek_sis = TRUE;
        break;
    case ID_CMD_FMT:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tFormat - %d\n",
                  R[NUM_PC], val, UNIT_NUM);

        id_phn  = id_data[0];
        id_scnt = id_data[1];
        pattern = id_data[2];

        /* Format scnt sectors with the given pattern, if attached */
        if (id_sel_unit->flags & UNIT_ATT) {
            /* Formatting soft-sectored disks always begins at sector 0 */
            sec = 0;

            while (id_scnt-- > 0) {
                /* Write one sector of pattern */
                for (id_buf_ptr = 0; id_buf_ptr < ID_SEC_SIZE; id_buf_ptr++) {
                    id_buf[id_buf_ptr] = pattern;
                }
                lba = id_lba(id_cyl[UNIT_NUM], id_phn, sec++);
                if (sim_disk_wrsect(id_sel_unit, lba, id_buf, NULL, 1) == SCPE_OK) {
                    sim_debug(EXECUTE_MSG, &id_dev,
                              "[%08x]\tFORMAT: PHN=%d SCNT=%d PAT=%02x LBA=%04x\n",
                              R[NUM_PC], id_phn, id_scnt, pattern, lba);
                } else {
                    sim_debug(EXECUTE_MSG, &id_dev,
                              "[%08x]\tFORMAT FAILED! PHN=%d SCNT=%d PAT=%02x LBA=%04x\n",
                              R[NUM_PC], id_phn, id_scnt, pattern, lba);
                    break;
                }
            }

            id_data[0] = 0;
        } else {
            /* Not attached */
            id_data[0] = ID_EST_NR;
        }

        id_data[1] = id_scnt;

        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_VID:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tVerify ID - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_data[0] = 0;
        id_data[1] = 0x05; /* What do we put here? */
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_RID:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead ID - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        if (id_sel_unit->flags & UNIT_ATT) {
            id_drq = TRUE;

            /* Grab our arguments */
            id_phn = id_data[0];
            id_scnt = id_data[1];

            /* Compute logical values used by ID verification */
            id_lhn = id_phn;
            id_lsn = 0;
        } else {
            sim_debug(EXECUTE_MSG, &id_dev,
                      "[%08x]\tUNIT %d NOT ATTACHED, CANNOT READ ID.\n",
                      R[NUM_PC], UNIT_NUM);
        }
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_RDIAG:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead Diag - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_RDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead Data - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        if (id_sel_unit->flags & UNIT_ATT) {
            id_drq = TRUE;
            id_buf_ptr = 0;

            /* Grab our arguments */
            id_phn  = id_data[0];
            id_lcnh = ~(id_data[1]);
            id_lcnl = id_data[2];
            id_lhn  = id_data[3];
            id_lsn  = id_data[4];
            id_scnt = id_data[5];
        } else {
            sim_debug(EXECUTE_MSG, &id_dev,
                      "[%08x]\tUNIT %d NOT ATTACHED, CANNOT READ DATA.\n",
                      R[NUM_PC], UNIT_NUM);
        }

        time = (uint32) abs(id_cyl[UNIT_NUM] - ((id_lcnh<<8)|id_lcnl));
        if (time == 0) {
            time++;
        }
        time = time * ID_SEEK_WAIT;
        id_activate(DELAY_US(time + ID_RW_WAIT));
        break;
    case ID_CMD_CHECK:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tCheck - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_SCAN:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tScan - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_VDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tVerify Data - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        id_activate(DELAY_US(ID_CMD_WAIT));
        break;
    case ID_CMD_WDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tWrite Data - %d\n",
                  R[NUM_PC], val, UNIT_NUM);
        if (id_sel_unit->flags & UNIT_ATT) {
            id_drq = TRUE;
            id_buf_ptr = 0;

            /* Grab our arguments */
            id_phn  = id_data[0];
            id_lcnh = ~(id_data[1]);
            id_lcnl = id_data[2];
            id_lhn  = id_data[3];
            id_lsn  = id_data[4];
            id_scnt = id_data[5];
        } else {
            sim_debug(EXECUTE_MSG, &id_dev,
                      "[%08x]\tUNIT %d NOT ATTACHED, CANNOT WRITE.\n",
                      R[NUM_PC], UNIT_NUM);
        }
        time = (uint32) abs(id_cyl[UNIT_NUM] - ((id_lcnh<<8)|id_lcnl));
        if (time == 0) {
            time++;
        }
        time = time * ID_SEEK_WAIT;
        id_activate(DELAY_US(time + ID_RW_WAIT));
        break;
    }
}

void id_drq_handled()
{
    id_status &= ~ID_STAT_DRQ;
    id_drq = FALSE;
}

CONST char *id_description(DEVICE *dptr)
{
    return "72MB MFM Hard Disk";
}

t_stat id_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "71MB MFM Integrated Hard Disk (ID)\n\n");
    fprintf(st,
            "The ID controller implements the integrated MFM hard disk controller\n"
            "of the 3B2/400. Up to four drives are supported on a single controller.\n");
    return SCPE_OK;
}
