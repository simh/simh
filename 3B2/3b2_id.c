/* 3b2_d.h: AT&T 3B2 Model 400 Hard Disk (uPD7261) Implementation

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
 * This file contains the code for the Integrated Disk (ID) controller
 * (based on the uPD7261) and up to two winchester hard disks.
 *
 * Supported winchester drives are:
 *
 *   SIMH Name  ID    Cyl  Head  Sec  Byte/Sec  Note
 *   ---------  --   ----  ----  ---  --------  ----------------------
 *   HD30       3     697     5   18    512     CDC Wren 94155-36
 *   HD72       5     925     9   18    512     CDC Wren II 94156-86
 *   HD72C      8     754    11   18    512     Fujitsu M2243AS
 *   HD135      11   1224    15   18    512     Maxtor XT1190
 */

#include "3b2_defs.h"
#include "3b2_id.h"

#define ID_SEEK_WAIT        50
#define ID_SEEK_BASE        700
#define ID_RECAL_WAIT       6000
#define ID_RW_WAIT          1000
#define ID_SUS_WAIT         200
#define ID_SPEC_WAIT        1250
#define ID_SIS_WAIT         142
#define ID_CMD_WAIT         140

/* Static function declarations */
static SIM_INLINE t_lba id_lba(uint16 cyl, uint8 head, uint8 sec);

/* Data FIFO pointer - Read */
uint8    id_dpr = 0;
/* Data FIFO pointer - Write */
uint8    id_dpw = 0;
/* Controller Status Register */
uint8    id_status = 0;
/* Unit Interrupt Status */
uint8    id_int_status = 0;
/* Last command received */
uint8    id_cmd = 0;
/* DMAC request */
t_bool   id_drq = FALSE;
/* 8-byte FIFO */
uint8    id_data[ID_FIFO_LEN] = {0};
/* SRQM bit */
t_bool   id_srqm = FALSE;
/* The logical unit number (0-1) */
uint8    id_unit_num = 0;
/* The physical unit number (0-3) */
uint8    id_ua = 0;
/* Cylinder the drive is positioned on */
uint16   id_cyl[ID_NUM_UNITS] = {0};
/* Ending Track Number (from Specify) */
uint8    id_etn = 0;
/* Ending Sector Number (from Specify) */
uint8    id_esn = 0;
/* DTLH word (from Specify) */
uint8    id_dtlh = 0;
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
/* Whether we are using polling mode or not */
t_bool   id_polling = FALSE;
/* Sector buffer */
uint8    id_buf[ID_SEC_SIZE];
/* Buffer pointer */
size_t   id_buf_ptr = 0;

uint8    id_idfield[ID_IDFIELD_LEN];
uint8    id_idfield_ptr = 0;

int8     id_seek_state[ID_NUM_UNITS] = {ID_SEEK_NONE};

struct id_dtype {
    uint8  hd;    /* Number of heads */
    uint32 capac; /* Capacity (in sectors) */
    const char *name;
};

static struct id_dtype id_dtab[] = {
    ID_DRV(HD30),
    ID_DRV(HD72),
    ID_DRV(HD72C),
    ID_DRV(HD135),
    ID_DRV(HD161),
    { 0 }
};

UNIT id_unit[] = {
    { UDATA (&id_unit_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BINK+ID_AUTOSIZE+
             (ID_HD72_DTYPE << ID_V_DTYPE), ID_DSK_SIZE(HD72)), 0, ID0, 0 },
    { UDATA (&id_unit_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BINK+ID_AUTOSIZE+
             (ID_HD72_DTYPE << ID_V_DTYPE), ID_DSK_SIZE(HD72)), 0, ID1, 0 },
    { UDATA (&id_ctlr_svc, 0, 0) },
    { NULL }
};

UNIT    *id_ctlr_unit = &id_unit[ID_CTLR];

/* The currently selected drive number */
UNIT    *id_sel_unit = &id_unit[ID0];

REG id_reg[] = {
    { HRDATAD(CMD,  id_cmd,    8, "Command") },
    { HRDATAD(STAT, id_status, 8, "Status")  },
    { BRDATAD(CYL,  id_cyl,    8, 8, ID_NUM_UNITS, "Track")   },
    { NULL }
};

/* HD161 and HD135 are identical; the difference is only in the
 * software being run on the emulator. SVR 2.0 will support a maximum
 * of 1024 cylinders, so can only format the first 1024 cylinders of
 * the HD135. SVR 3.0+ can support all 1224 cylinders of the HD161. */
MTAB id_mod[] = {
    { MTAB_XTD|MTAB_VUN, ID_HD30_DTYPE, NULL, "HD30",
      &id_set_type, NULL, NULL, "Set HD30 Disk Type" },
    { MTAB_XTD|MTAB_VUN, ID_HD72_DTYPE, NULL, "HD72",
      &id_set_type, NULL, NULL, "Set HD72 Disk Type" },
    { MTAB_XTD|MTAB_VUN, ID_HD72C_DTYPE, NULL, "HD72C",
      &id_set_type, NULL, NULL, "Set HD72C Disk Type" },
    { MTAB_XTD|MTAB_VUN, ID_HD135_DTYPE, NULL, "HD135",
      &id_set_type, NULL, NULL, "Set HD135 Disk Type" },
    { MTAB_XTD|MTAB_VUN, ID_HD161_DTYPE, NULL, "HD161",
      &id_set_type, NULL, NULL, "Set HD161 Disk Type" },
    { MTAB_XTD|MTAB_VUN, 0, "TYPE", NULL,
      NULL, &id_show_type, NULL, "Display device type" },
    { ID_AUTOSIZE, ID_AUTOSIZE, "autosize", "AUTOSIZE", 
      NULL, NULL, NULL, "Set type based on file size at attach" },
    { ID_AUTOSIZE,           0, "noautosize",   "NOAUTOSIZE",   
      NULL, NULL, NULL, "Disable disk autosize on attach" },
    { 0 }
};

DEVICE id_dev = {
    "IDISK", id_unit, id_reg, id_mod,
    ID_NUM_UNITS, 16, 32, 1, 16, 8,
    NULL, NULL, &id_reset,
    NULL, &id_attach, &id_detach, NULL,
    DEV_DEBUG|DEV_DISK|DEV_SECTORS, 0, sys_deb_tab,
    NULL, NULL, &id_help, NULL, NULL,
    &id_description
};

/* Function implementation */

t_bool id_int()
{
    return (((id_status & ID_STAT_CEL) ||
             (id_status & ID_STAT_CEH) ||
             ((id_status & ID_STAT_SRQ) && !id_srqm)));
}

static SIM_INLINE void id_clear_fifo()
{
    id_dpr = 0;
    id_dpw = 0;
}

static SIM_INLINE void id_activate(UNIT *uptr, int32 delay)
{
    sim_activate_abs(uptr, delay);
}

/*
 * Service routine for ID controller.
 *
 * The simulated HD controller must service Sense Interrupt Status,
 * Specify, and Detect Error independent of the operation of either ID
 * unit, which may be in the middle of a seek or other operation.
 */
t_stat id_ctlr_svc(UNIT *uptr)
{
    uint8 cmd;

    cmd = uptr->u4;  /* The command that caused the activity */

    id_srqm = FALSE;
    id_status &= ~(ID_STAT_CB);
    id_status |= ID_STAT_CEH;
    uptr->u4 = 0;

    switch (cmd) {
    case ID_CMD_SIS:
        sim_debug(EXECUTE_MSG, &id_dev,
                  "[%08x]\tINTR\t\tCOMPLETING Sense Interrupt Status.\n",
                  R[NUM_PC]);
        id_data[0] = id_int_status;
        id_int_status = 0;
        break;
    default:
        sim_debug(EXECUTE_MSG, &id_dev,
                  "[%08x]\tINTR\t\tCOMPLETING OTHER COMMAND 0x%x (CONTROLLER)\n",
                  R[NUM_PC], cmd);
        break;
    }

    return SCPE_OK;
}

/*
 * Service routine for ID0 and ID1 units.
 */
t_stat id_unit_svc(UNIT *uptr)
{
    uint8 unit, other, cmd;

    unit  = uptr->u3;  /* The unit number that needs an interrupt */
    cmd   = uptr->u4;  /* The command that caused the activity    */
    other = unit ^ 1;  /* The number of the other unit            */

    /* If the other unit is active, we cannot interrupt, so we delay
     * here */
    if (id_unit[other].u4 == ID_CMD_RDATA ||
        id_unit[other].u4 == ID_CMD_WDATA) {
        id_activate(uptr, 1000);
        return SCPE_OK;
    }

    id_srqm = FALSE;
    id_status &= ~(ID_STAT_CB);
    /* Note that we don't set CEH, in case this is a SEEK/RECAL ID_SEEK_1 */

    switch (cmd) {
    case ID_CMD_SEEK:   /* fall-through */
    case ID_CMD_RECAL:
        /* In POLLING mode, SEEK and RECAL actually interrupt twice.
         *
         * 1. Immediately after the correct number of stepping pulses
         *    have been issued (SRQ is not set)
         *
         * 2. After the drive has completed seeking and is ready
         *    for a new command (SRQ is set)
         */
        if (id_polling) {
            switch (id_seek_state[unit]) {
            case ID_SEEK_0:
                id_status |= ID_STAT_CEH;
                sim_debug(EXECUTE_MSG, &id_dev,
                          "[%08x]\tINTR\t\tCOMPLETING Recal/Seek SEEK_0 UNIT %d\n",
                          R[NUM_PC], unit);
                id_seek_state[unit] = ID_SEEK_1;
                id_activate(uptr, 8000); /* TODO: Correct Delay based on steps */
                break;
            case ID_SEEK_1:
                sim_debug(EXECUTE_MSG, &id_dev,
                          "[%08x]\tINTR\t\tCOMPLETING Recal/Seek SEEK_1 UNIT %d\n",
                          R[NUM_PC], unit);
                id_seek_state[unit] = ID_SEEK_NONE;
                id_status |= ID_STAT_SRQ;
                uptr->u4 = 0; /* Only clear out the command on a SEEK_1, never a SEEK_0 */
                if (uptr->flags & UNIT_ATT) {
                    id_int_status |= (ID_IST_SEN|unit);
                } else {
                    id_int_status |= (ID_IST_NR|unit);
                }
                break;
            default:
                sim_debug(EXECUTE_MSG, &id_dev,
                          "[%08x]\tINTR\t\tERROR, NOT SEEK_0 OR SEEK_1, UNIT %d\n",
                          R[NUM_PC], unit);
                break;
            }
        } else {
            sim_debug(EXECUTE_MSG, &id_dev,
                      "[%08x]\tINTR\t\tCOMPLETING NON-POLLING Recal/Seek UNIT %d\n",
                      R[NUM_PC], unit);
            id_status |= ID_STAT_CEH;
            uptr->u4 = 0;
            if (uptr->flags & UNIT_ATT) {
                id_int_status |= (ID_IST_SEN|unit);
            } else {
                id_int_status |= (ID_IST_NR|unit);
            }
        }

        break;
    case ID_CMD_SUS:
        sim_debug(EXECUTE_MSG, &id_dev,
                  "[%08x]\tINTR\t\tCOMPLETING Sense Unit Status UNIT %d\n",
                  R[NUM_PC], unit);
        id_status |= ID_STAT_CEH;
        uptr->u4 = 0;
        if ((uptr->flags & UNIT_ATT) == 0) {
            /* If no HD is attached, SUS puts 0x00 into the data
               buffer */
            id_data[0] = 0;
        } else {
            /* Put Unit Status into byte 0 */
            id_data[0] = (ID_UST_DSEL|ID_UST_SCL|ID_UST_RDY);
            if (id_cyl[unit] == 0) {
                id_data[0] |= ID_UST_TK0;
            }
        }
        break;
    default:
        sim_debug(EXECUTE_MSG, &id_dev,
                  "[%08x]\tINTR\t\tCOMPLETING OTHER COMMAND 0x%x UNIT %d\n",
                  R[NUM_PC], cmd, unit);
        id_status |= ID_STAT_CEH;
        uptr->u4 = 0;
        break;
    }

    return SCPE_OK;
}

t_stat id_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (val < 0 || val > ID_MAX_DTYPE) {
        return SCPE_ARG;
    }

    if (uptr->flags & UNIT_ATT) {
        return SCPE_ALATT;
    }

    uptr->flags = (uptr->flags & ~ID_DTYPE) | (val << ID_V_DTYPE);
    uptr->capac = (t_addr)id_dtab[val].capac;

    return SCPE_OK;
}

t_stat id_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "%s", id_dtab[ID_GET_DTYPE(uptr->flags)].name);
    return SCPE_OK;
}

t_stat id_reset(DEVICE *dptr)
{
    id_clear_fifo();
    return SCPE_OK;
}

t_stat id_attach(UNIT *uptr, CONST char *cptr)
{
    static const char *drives[] = {"HD30", "HD72", "HD72C", "HD135", "HD161", NULL};

    return sim_disk_attach_ex(uptr, cptr, 512, 1, TRUE, 0, id_dtab[ID_GET_DTYPE(uptr->flags)].name, 
                              0, 0, (uptr->flags & ID_AUTOSIZE) ? drives : NULL);
}

t_stat id_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}

/* Return the logical block address of the given sector */
static t_lba id_lba(uint16 cyl, uint8 head, uint8 sec)
{
    uint8 dtype;

    dtype = ID_GET_DTYPE(id_sel_unit->flags);

    return((ID_SEC_CNT * id_dtab[dtype].hd * cyl) +
           (ID_SEC_CNT * head) +
           sec);
}

/* At the end of each sector read or write, we update the FIFO
 * with the correct return parameters. */
static void SIM_INLINE id_end_rw(uint8 est)
{
    id_clear_fifo();
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
static void SIM_INLINE id_update_chs()
{
    if (id_lsn++ >= id_esn) {
        id_lsn = 0;
        if (id_lhn++ >= id_etn) {
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

uint32 id_read(uint32 pa, size_t size)
{
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
                    id_cyl[id_unit_num] = cyl;
                    lba = id_lba(cyl, id_lhn, id_lsn);
                    if (sim_disk_rdsect(id_sel_unit, lba, id_buf, &sectsread, 1) == SCPE_OK) {
                        if (sectsread !=1) {
                            sim_debug(READ_MSG, &id_dev,
                                      "[%08x]\tERROR: ASKED TO READ ONE SECTOR, READ: %d\n",
                                      R[NUM_PC], sectsread);
                        }
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
                          "[%08x]\tDATA\t%02x\n",
                          R[NUM_PC], data);

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
                        id_clear_fifo();
                        id_data[0] = 0;
                        id_data[1] = id_scnt;
                    }
                }
            } else {
                /* cmd not Read Data or Read ID */
                stop_reason = STOP_ERR;
                return 0;
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
                id_buf[id_buf_ptr++] = (uint8)(val & 0xff);
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x]\tDATA\t%02x\n",
                          R[NUM_PC], (uint8)(val & 0xff));
            } else {
                sim_debug(WRITE_MSG, &id_dev,
                          "[%08x]\tERROR\tWDATA OVERRUN\n",
                          R[NUM_PC]);
                id_end_rw(ID_EST_OVR);
                return;
            }

            /* If we've hit the end of a sector, flush it */
            if (id_buf_ptr >= ID_SEC_SIZE) {
                /* It's time to start the next sector, and flush the old. */
                id_buf_ptr = 0;
                cyl = (uint16) (((uint16) id_lcnh << 8)|(uint16)id_lcnl);
                id_cyl[id_unit_num] = cyl;
                lba = id_lba(cyl, id_lhn, id_lsn);
                if (sim_disk_wrsect(id_sel_unit, lba, id_buf, &sectswritten, 1) == SCPE_OK) {
                    if (sectswritten !=1) {
                        sim_debug(WRITE_MSG, &id_dev,
                                  "[%08x]\tERROR: ASKED TO WRITE ONE SECTOR, WROTE: %d\n",
                                  R[NUM_PC], sectswritten);
                    }
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

    /* Reset the FIFO pointer */
    id_clear_fifo();

    /* Is this an aux command or a full command? */
    if ((val & 0xf0) == 0) {
        aux_cmd = val & 0x0f;

        if (aux_cmd & ID_AUX_CLCE) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x] \tCOMMAND\t%02x\tAUX:CLCE\n",
                      R[NUM_PC], val);
            id_status &= ~(ID_STAT_CEH|ID_STAT_CEL);
        }

        if (aux_cmd & ID_AUX_HSRQ) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x] \tCOMMAND\t%02x\tAUX:HSRQ\n",
                      R[NUM_PC], val);
            id_srqm = TRUE;
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
            id_clear_fifo();
            sim_cancel(id_sel_unit);
            sim_cancel(id_ctlr_unit);
            id_status = 0;
            id_srqm = FALSE;
        }

        /* Just return early */
        return;
    }

    /* If the controller is busy and this isn't an AUX command, do
     * nothing */
    if (id_status & ID_STAT_CB) {
        sim_debug(EXECUTE_MSG, &id_dev,
                  "!!! Controller Busy. Skipping command byte %02x\n",
                  val);
        return;
    }

    /* A full command always resets CEH and CEL */
    id_status &= ~(ID_STAT_CEH|ID_STAT_CEL);

    /* Save the full command byte */
    id_cmd = val;
    cmd = (id_cmd >> 4) & 0xf;

    /* Now that we know it's not an aux command, we can get the unit
     * number. Note that we don't update the unit in the case of three
     * special commands. */
    if (cmd != ID_CMD_SIS && cmd != ID_CMD_SPEC && cmd != ID_CMD_DERR) {
        if ((id_cmd & 3) != id_ua) {
            id_unit_num = id_cmd & 1;
            id_ua = id_cmd & 3;
            id_sel_unit = &id_unit[id_unit_num];
        }
    }

    /* TODO: Fix this hack */
    if (cmd == ID_CMD_SIS || cmd == ID_CMD_SPEC || cmd == ID_CMD_DERR) {
        id_ctlr_unit->u4 = cmd;
    } else {
        id_sel_unit->u4 = cmd;
    }

    id_status |= ID_STAT_CB;

    switch(cmd) {
    case ID_CMD_SIS:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSense Int. Status\n",
                  R[NUM_PC], val);
        id_status &= ~ID_STAT_SRQ; /* SIS immediately de-asserts SRQ */
        id_activate(id_ctlr_unit, ID_SIS_WAIT);
        break;
    case ID_CMD_SPEC:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSpecify - ETN=%02x ESN=%02x\n",
                  R[NUM_PC], val, id_data[3], id_data[4]);
        id_dtlh = id_data[1];
        id_etn = id_data[3];
        id_esn = id_data[4];
        id_polling = (id_dtlh & ID_DTLH_POLL) == 0;
        id_activate(id_ctlr_unit, ID_SPEC_WAIT);
        break;
    case ID_CMD_SUS:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tSense Unit Status - %d\n",
                  R[NUM_PC], val, id_ua);
        id_activate(id_sel_unit, ID_SUS_WAIT);
        break;
    case ID_CMD_DERR:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tDetect Error\n",
                  R[NUM_PC], val);
        id_activate(id_ctlr_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_RECAL:
        time = id_cyl[id_unit_num];
        id_cyl[id_unit_num] = 0;
        id_seek_state[id_unit_num] = ID_SEEK_0;
        if (id_polling) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tRecalibrate - %d - POLLING\n",
                      R[NUM_PC], val, id_ua);
            id_activate(id_sel_unit, 1000);
        } else {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tRecalibrate - %d - NORMAL\n",
                      R[NUM_PC], val, id_ua);
            id_activate(id_sel_unit, (ID_RECAL_WAIT + (time * ID_SEEK_WAIT)));
        }
        break;
    case ID_CMD_SEEK:
        id_lcnh = id_data[0];
        id_lcnl = id_data[1];
        cyl = id_lcnh << 8 | id_lcnl;
        time = (uint32) abs(id_cyl[id_unit_num] - cyl);
        id_cyl[id_unit_num] = cyl;
        id_seek_state[id_unit_num] = ID_SEEK_0;

        if (id_polling) {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tSeek - %d - POLLING\n",
                      R[NUM_PC], val, id_ua);
            id_activate(id_sel_unit, 4000);
        } else {
            sim_debug(WRITE_MSG, &id_dev,
                      "[%08x]\tCOMMAND\t%02x\tSeek - %d - NORMAL\n",
                      R[NUM_PC], val, id_ua);
            id_activate(id_sel_unit, ID_SEEK_BASE + (time * ID_SEEK_WAIT));
        }
        break;
    case ID_CMD_FMT:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tFormat - %d\n",
                  R[NUM_PC], val, id_ua);

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
                lba = id_lba(id_cyl[id_unit_num], id_phn, sec++);
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

        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_VID:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tVerify ID - %d\n",
                  R[NUM_PC], val, id_ua);
        id_data[0] = 0;
        id_data[1] = 0x05; /* What do we put here? */
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_RID:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead ID - %d\n",
                  R[NUM_PC], val, id_ua);
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
                      R[NUM_PC], id_ua);
        }
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_RDIAG:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead Diag - %d\n",
                  R[NUM_PC], val, id_ua);
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_RDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tRead Data - %d\n",
                  R[NUM_PC], val, id_ua);
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
                      R[NUM_PC], id_ua);
        }
        id_activate(id_sel_unit, ID_RW_WAIT);
        break;
    case ID_CMD_CHECK:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tCheck - %d\n",
                  R[NUM_PC], val, id_ua);
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_SCAN:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tScan - %d\n",
                  R[NUM_PC], val, id_ua);
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_VDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tVerify Data - %d\n",
                  R[NUM_PC], val, id_ua);
        id_activate(id_sel_unit, ID_CMD_WAIT);
        break;
    case ID_CMD_WDATA:
        sim_debug(WRITE_MSG, &id_dev,
                  "[%08x]\tCOMMAND\t%02x\tWrite Data - %d\n",
                  R[NUM_PC], val, id_ua);
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
                      R[NUM_PC], id_ua);
        }
        id_activate(id_sel_unit, ID_RW_WAIT);
        break;
    }
}

void id_after_dma()
{
    id_status &= ~ID_STAT_DRQ;
    id_drq = FALSE;
}

CONST char *id_description(DEVICE *dptr)
{
    return "Integrated Hard Disk";
}

t_stat id_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "Integrated Hard Disk (IDISK)\n\n");
    fprintf(st, "The IDISK device implements the integrated MFM hard disk of the\n");
    fprintf(st, "3B2/400. Up to two drives are supported on a single controller.\n\n");
    fprintf(st, "Supported device types are:\n\n");
    fprintf(st, "  Name    Size    ID    Cyl  Head  Sec  Byte/Sec  Description\n");
    fprintf(st, "  ----  --------  --   ----  ----  ---  --------  ----------------------\n");
    fprintf(st, "  HD30   30.6 MB   3    697     5   18    512     CDC Wren 94155-36\n");
    fprintf(st, "  HD72   73.2 MB   5    925     9   18    512     CDC Wren II 94156-86\n");
    fprintf(st, "  HD72C  72.9 MB   8    754    11   18    512     Fujitsu M2243AS\n");
    fprintf(st, "  HD135 135.0 MB  11   1024    15   18    512     Maxtor XT1190 (SVR2)\n");
    fprintf(st, "  HD161 161.4 MB  11   1224    15   18    512     Maxtor XT1190 (SVR3+)\n\n");
    fprintf(st, "The drive ID and geometry values are used when low-level formatting a\n");
    fprintf(st, "drive using the AT&T 'idtools' utility.\n");
    return SCPE_OK;
}
