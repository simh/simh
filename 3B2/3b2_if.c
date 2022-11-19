/* 3b2_if.c: TMS2797 Integrated Floppy Controller

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

#include "3b2_if.h"

#include "sim_disk.h"

#include "3b2_cpu.h"
#include "3b2_csr.h"

/* Static function declarations */
static SIM_INLINE uint32 if_lba();

/*
 * Disk Format:
 * ------------
 *
 * - 80 Tracks
 * - 9 Sectors per track
 * - 2 heads
 * - 512 bytes per sector
 *
 * 80 * 9 * 2 * 512 = 720KB
 *
 */

#define IF_STEP_DELAY       300     /* us */
#define IF_R_DELAY          6500    /* us */
#define IF_W_DELAY          7000    /* us */
#define IF_VERIFY_DELAY     2000    /* us */
#define IF_HLD_DELAY        6000    /* us */
#define IF_HSW_DELAY        4000    /* us */

#if defined(REV3)
#define SET_INT CPU_SET_INT(INT_FLOPPY)
#define CLR_INT CPU_CLR_INT(INT_FLOPPY)
#else
#define SET_INT do {             \
        CPU_SET_INT(INT_FLOPPY); \
        SET_CSR(CSRDISK);        \
    } while(0)
#define CLR_INT do {             \
        CPU_CLR_INT(INT_FLOPPY); \
        CLR_CSR(CSRDISK);        \
    } while(0)
#endif

UNIT if_unit = {
    UDATA (&if_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE,
           IF_DSK_SIZE_SECS)
};

REG if_reg[] = {
    { NULL }
};

DEVICE if_dev = {
    "IFLOPPY", &if_unit, if_reg, NULL,
    1, 16, 8, 1, 16, 8,
    NULL, NULL, &if_reset,
    NULL, &if_attach, &if_detach, NULL,
    DEV_DEBUG|DEV_DISK|DEV_SECTORS, 0, sys_deb_tab,
    NULL, NULL, &if_help, NULL, NULL,
    &if_description
};

IF_STATE if_state;
uint8    if_buf[IF_SEC_SIZE];
uint32   if_sec_ptr = 0;

/* Function implementation */

static SIM_INLINE void if_activate(uint32 delay_us)
{
    sim_activate_after(&if_unit, delay_us);
}

t_stat if_svc(UNIT *uptr)
{
    uint32 lba; /* Logical block address for write */
    t_seccnt sectswritten;

    if_state.status &= ~(IF_BUSY);

    switch(if_state.cmd & 0xf0) {
    case IF_RESTORE:
        if_state.status = (IF_TK_0|IF_HEAD_LOADED);
        break;
    case IF_SEEK:
        if_state.status = IF_HEAD_LOADED;
        if (if_state.track == 0) {
            if_state.status |= IF_TK_0;
        }
        break;
    case IF_WRITE_SEC:
        lba = if_lba();

        /* If we're read-only, don't actually do anything. */
        if (if_unit.flags & UNIT_RO) {
            break;
        }

        if (sim_disk_wrsect(&if_unit, lba, if_buf, &sectswritten, 1) == SCPE_OK) {
            if (sectswritten != 1) {
                sim_debug(EXECUTE_MSG, &if_dev,
                          "ERROR: ASKED TO wRITE ONE SECTOR, WROTE %d\n",
                          sectswritten);
            }
        }

        break;
    }

    if_state.cmd = 0;

    /* Request an interrupt */
    sim_debug(IRQ_MSG, &if_dev, "\tINTR\n");
    SET_INT;

    return SCPE_OK;
}

t_stat if_reset(DEVICE *dptr)
{
    if_state.status = IF_TK_0;
    if_state.track = 0;
    if_state.sector = 1;
    if_sec_ptr = 0;

    return SCPE_OK;
}

t_stat if_attach(UNIT *uptr, CONST char *cptr)
{
    return sim_disk_attach(uptr, cptr, 512, 1, TRUE, 0, NULL, 0, 0);
}

t_stat if_detach(UNIT *uptr)
{
    return sim_disk_detach(uptr);
}

uint32 if_read(uint32 pa, size_t size) {
    uint8 reg, data;
    UNIT *uptr;

    uptr = &(if_dev.units[0]);
    reg = (uint8)(pa - IFBASE);

    switch (reg) {
    case IF_STATUS_REG:
        data = if_state.status;
        /* If there's no image attached, we're not ready */
        if ((uptr->flags & (UNIT_ATT|UNIT_BUF)) == 0) {
            data |= IF_NRDY;
        }
        /* Reading the status register always de-asserts the IRQ line */
        CLR_INT;
        sim_debug(READ_MSG, &if_dev, "\tSTATUS\t%02x\n", data);
        break;
    case IF_TRACK_REG:
        data = if_state.track;
        sim_debug(READ_MSG, &if_dev, "\tTRACK\t%02x\n", data);
        break;
    case IF_SECTOR_REG:
        data = if_state.sector;
        sim_debug(READ_MSG, &if_dev, "\tSECTOR\t%02x\n", data);
        break;
    case IF_DATA_REG:
        if_state.status &= ~IF_DRQ;

        if (((uptr->flags & (UNIT_ATT|UNIT_BUF)) == 0) ||
            ((if_state.cmd & 0xf0) != IF_READ_SEC &&
             (if_state.cmd & 0xf0) != IF_READ_SEC_M)) {
            /* Not attached, or not a read command */

            switch (if_state.cmd & 0xf0) {
            case IF_READ_ADDR:
                /* Special state machine. */
                switch (if_state.read_addr_ptr++) {
                case 0:
                    if_state.data = if_state.track;
                    break;
                case 1:
                    if_state.data = if_state.side;
                    break;
                case 2:
                    if_state.data = if_state.sector;
                    break;
                case 3:
                    if_state.data = 2; /* 512 byte */
                    break;
                case 4:
                    /* TODO: Checksum */
                    if_state.data = 0;
                    break;
                case 5:
                    /* TODO: Checksum */
                    if_state.data = 0;
                    if_state.read_addr_ptr = 0;
                    break;
                }
            }

            sim_debug(READ_MSG, &if_dev, "\tDATA\t%02x\n", if_state.data);
            return if_state.data;
        }

        data = if_buf[if_sec_ptr++];
        sim_debug(READ_MSG, &if_dev, "\tDATA\t%02x\n", data);

        if (if_sec_ptr >= IF_SEC_SIZE) {
            if_sec_ptr = 0;
        }

        break;
    default:
        data = 0xffu; // Compiler warning
        break;
    }

    return data;
}

/* Handle the most recently received command */
void if_handle_command()
{
    uint32 delay_ms = 0;
    uint32 head_switch_delay = 0;
    uint32 head_load_delay = 0;
    uint32 lba; /* Logical block address */
    t_seccnt sectsread;

    if_sec_ptr = 0;

    /* We're starting a new command. */
    if_state.status = IF_BUSY;

    /* Clear read addr state */
    if_state.read_addr_ptr = 0;

    switch(if_state.cmd & 0xf0) {
    case IF_RESTORE:
    case IF_SEEK:
    case IF_STEP:
    case IF_STEP_T:
    case IF_STEP_IN:
    case IF_STEP_IN_T:
    case IF_STEP_OUT:
    case IF_STEP_OUT_T:
        if_state.cmd_type = 1;
        if (if_state.cmd & IF_H_FLAG) {
            head_load_delay = IF_HLD_DELAY;
        }
        break;

    case IF_READ_SEC:
    case IF_READ_SEC_M:
    case IF_WRITE_SEC:
    case IF_WRITE_SEC_M:
        if_state.cmd_type = 2;
#if defined(REV2)
        if (((if_state.cmd & IF_U_FLAG) >> 1) != if_state.side) {
            head_switch_delay = IF_HSW_DELAY;
            if_state.side = (if_state.cmd & IF_U_FLAG) >> 1;
        }
#endif
        break;

    case IF_READ_ADDR:
    case IF_READ_TRACK:
    case IF_WRITE_TRACK:
        if_state.cmd_type = 3;
#if defined(REV2)
        if (((if_state.cmd & IF_U_FLAG) >> 1) != if_state.side) {
            head_switch_delay = IF_HSW_DELAY;
            if_state.side = (if_state.cmd & IF_U_FLAG) >> 1;
        }
#endif
        break;
    }

    switch(if_state.cmd & 0xf0) {
    case IF_RESTORE:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tRestore\n", if_state.cmd);

        /* Reset HLT */
        if_state.status &= ~IF_HEAD_LOADED;

        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
        }

        /* If head should be loaded immediately, do so now */
        if (if_state.cmd & IF_H_FLAG) {
            if_state.status |= IF_HEAD_LOADED;
        }

        if (if_state.track == 0) {
            if_state.status |= IF_TK_0;
            if_state.track = 1; /* Kind of a gross hack */
        }

        if (if_state.cmd & IF_V_FLAG) {
            delay_ms = (IF_STEP_DELAY * if_state.track) + IF_VERIFY_DELAY;
        } else {
            delay_ms = IF_STEP_DELAY * if_state.track;
        }

        if_activate(delay_ms);

        if_state.data = 0;
        if_state.track = 0;
        break;

    case IF_STEP:
    case IF_STEP_T:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tStep\n", if_state.cmd);
        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
        }
        if_activate(IF_STEP_DELAY);
        if_state.track = (uint8) MIN(MAX((int) if_state.track + if_state.step_dir, 0), 0x4f);
        break;
    case IF_STEP_IN:
    case IF_STEP_IN_T:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tStep In\n", if_state.cmd);
        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
        }
        if_state.step_dir = IF_STEP_IN_DIR;
        if_state.track = (uint8) MAX((int) if_state.track + if_state.step_dir, 0);
        if_activate(IF_STEP_DELAY);
        break;
    case IF_STEP_OUT:
    case IF_STEP_OUT_T:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tStep Out\n", if_state.cmd);
        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
        }
        if_state.step_dir = IF_STEP_OUT_DIR;
        if_state.track = (uint8) MIN((int) if_state.track + if_state.step_dir, 0x4f);
        if_activate(IF_STEP_DELAY);
        break;
    case IF_SEEK:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tSeek\n", if_state.cmd);

        /* Reset HLT */
        if_state.status &= ~IF_HEAD_LOADED;

        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
        }

        /* If head should be loaded immediately, do so now */
        if (if_state.cmd & IF_H_FLAG) {
            if_state.status |= IF_HEAD_LOADED;
        }

        /* Save the direction for stepping */
        if (if_state.data > if_state.track) {
            if_state.step_dir = IF_STEP_IN_DIR;
        } else if (if_state.data < if_state.track) {
            if_state.step_dir = IF_STEP_OUT_DIR;
        }

        /* The new track is in the data register */

        if (if_state.data > IF_TRACK_COUNT-1) {
            if_state.data = IF_TRACK_COUNT-1;
        }

        if (if_state.data == 0) {
            if_state.status |= IF_TK_0;
        } else {
            if_state.status &= ~(IF_TK_0);
        }

        delay_ms = (uint32) abs(if_state.data - if_state.track);

        if (delay_ms == 0) {
            delay_ms++;
        }

        if (if_state.cmd & IF_V_FLAG) {
            if_activate((IF_STEP_DELAY * delay_ms) + IF_VERIFY_DELAY + head_load_delay);
        } else {
            if_activate((IF_STEP_DELAY * delay_ms) + head_load_delay);
        }

        if_state.track = if_state.data;
        break;

    case IF_READ_SEC:
        lba = if_lba();

        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tRead Sector %d/%d/%d (lba=%d)\n",
                  if_state.cmd, if_state.track, if_state.side, if_state.sector, lba);

        if (sim_disk_rdsect(&if_unit, lba, if_buf, &sectsread, 1) == SCPE_OK) {
            if (sectsread != 1) {
                sim_debug(EXECUTE_MSG, &if_dev,
                          "ERROR: ASKED TO READ ONE SECTOR, READ %d\n",
                          sectsread);
            }
            /* We set DRQ right away to request the transfer. */
            if_state.drq = TRUE;
            if_state.status |= IF_DRQ;
            if (if_state.cmd & IF_E_FLAG) {
                if_activate(IF_R_DELAY + IF_VERIFY_DELAY + head_switch_delay);
            } else {
                if_activate(IF_R_DELAY + head_switch_delay);
            }
        }

        break;
    case IF_READ_SEC_M:
        /* Not yet implemented. Halt the emulator. */
        sim_debug(EXECUTE_MSG, &if_dev,
                  "\tCOMMAND\t%02x\tRead Sector (Multi) - NOT IMPLEMENTED\n",
                  if_state.cmd);
        stop_reason = STOP_ERR;
        break;
    case IF_WRITE_SEC:
        lba = if_lba();

        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tWrite Sector %d/%d/%d (lba=%d)\n",
                  if_state.cmd, if_state.track, if_state.side, if_state.sector, lba);

        if (if_unit.flags & UNIT_RO) {
            if_state.status |= IF_WP;
            sim_debug(EXECUTE_MSG, &if_dev, "\tWON'T WRITE: WRITE PROTECTED.\n");
            /* Still cause an interrupt... */
            if_activate(IF_W_DELAY + head_switch_delay);
            /* But don't set DRQ and ask for a transfer. */
            break;
        }

        /* We set DRQ right away to request the transfer. Data will
         * be written by the host into our buffer by 512 writes to the
         * data register. When the IF device later activates, the data
         * will actually be written. */
        if_state.drq = TRUE;
        if_state.status |= IF_DRQ;
        if (if_state.cmd & IF_E_FLAG) {
            if_activate(IF_W_DELAY + IF_VERIFY_DELAY + head_switch_delay);
        } else {
            if_activate(IF_W_DELAY + head_switch_delay);
        }
        break;
    case IF_WRITE_SEC_M:
        /* Not yet implemented. Halt the emulator. */
        sim_debug(EXECUTE_MSG, &if_dev,
                  "\tCOMMAND\t%02x\tWrite Sector (Multi) - NOT IMPLEMENTED\n",
                  if_state.cmd);
        stop_reason = STOP_ERR;
        break;
    case IF_READ_ADDR:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tRead Address\n", if_state.cmd);
        if_state.drq = TRUE;
        if_state.status |= IF_DRQ;
        if_activate(IF_R_DELAY);
        break;
    case IF_READ_TRACK:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tRead Track\n", if_state.cmd);
        /* Not yet implemented. Halt the emulator. */
        stop_reason = STOP_ERR;
        break;
    case IF_WRITE_TRACK:
        sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tWrite Track\n", if_state.cmd);
        /* Set DRQ */
        if_state.drq = TRUE;
        if_state.status |= IF_DRQ;
        if (if_state.cmd & IF_E_FLAG) {
            if_activate(IF_W_DELAY + IF_VERIFY_DELAY + head_switch_delay);
        } else {
            if_activate(IF_W_DELAY + head_switch_delay);
        }
        break;
    }
}

void if_write(uint32 pa, uint32 val, size_t size)
{
    UNIT *uptr;
    uint8 reg;

    val = val & 0xff;

    uptr = &(if_dev.units[0]);
    reg = (uint8) (pa - IFBASE);

    switch (reg) {
    case IF_CMD_REG:
        if_state.cmd = (uint8) val;
        /* Writing to the command register always de-asserts the IRQ line */
        CLR_INT;

        /* If this is a FORCE INTERRUPT, handle it immediately. All
         * other commands require that the unit be attached and a
         * diskette loaded. This one does not. */
        if ((if_state.cmd & 0xf0) == IF_FORCE_INT) {
            sim_debug(EXECUTE_MSG, &if_dev, "\tCOMMAND\t%02x\tForce Interrupt\n", if_state.cmd);
            if_state.status = 0;

            if ((uptr->flags & UNIT_ATT) && if_state.track == 0) {
                if_state.status |= (IF_TK_0|IF_HEAD_LOADED);
            }

            if ((if_state.cmd & 0xf) == 0) {
                sim_cancel(&if_unit);
#if defined(REV2)
                CLR_INT; /* TODO: Confirm this is right */
#endif
            } else if ((if_state.cmd & 0x8) == 0x8) {
                if_state.status |= IF_DRQ;
                SET_INT;
            }
            break;
        }

        if ((uptr->flags & UNIT_ATT) == 0) {
            /* If not attached, do nothing */
            break;
        }

        if_handle_command();
        break;
    case IF_TRACK_REG:
        if_state.track = (uint8) val;
        sim_debug(WRITE_MSG, &if_dev, "\tTRACK\t%02x\n", val);
        break;
    case IF_SECTOR_REG:
        if_state.sector = (uint8) val;
        sim_debug(WRITE_MSG, &if_dev, "\tSECTOR\t%02x\n", val);
        break;
    case IF_DATA_REG:
        if_state.data = (uint8) val;

        sim_debug(WRITE_MSG, &if_dev, "\tDATA\t%02x\n", val);

        if ((uptr->flags & UNIT_ATT) == 0) {
            /* Not attached */
            break;
        }

        if ((if_state.cmd & 0xf0) == IF_WRITE_TRACK) {
            /* We intentionally ignore WRITE TRACK data, because
             * This is only used for low-level MFM formatting,
             * which we do not emulate. */
        } else if ((if_state.cmd & 0xf0) == IF_WRITE_SEC ||
                   (if_state.cmd & 0xf0) == IF_WRITE_SEC_M) {

            if_buf[if_sec_ptr++] = (uint8) val;

            if (if_sec_ptr >= IF_SEC_SIZE) {
                if_sec_ptr = 0;
            }
        }

        break;
    default:
        break;
    }
}

#if defined(REV3)
uint32 if_csr_read(uint32 pa, size_t size)
{
    return (uint32)(if_state.csr);
}

void if_csr_write(uint32 pa, uint32 val, size_t size)
{
    if_state.csr = val & 0xff;
}
#endif

CONST char *if_description(DEVICE *dptr)
{
    return "Integrated Floppy Disk";
}

t_stat if_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "Integrated Floppy Disk (IFLOPPY)\n\n");
    fprintf(st, "The IFLOPPY device implements the integrated 720 KB floppy disk\n");
    fprintf(st, "of the 3B2/400. A single floppy disk is supported on the controller.\n\n");
    fprintf(st, "The format of the diskette media is as follows:\n\n");
    fprintf(st, "    Size     Sides   Tracks/Side   Sectors/Track   Bytes/Track\n");
    fprintf(st, "    ------   -----   -----------   -------------   -----------\n");
    fprintf(st, "    720 KB       2            80               9           512\n\n");
    fprintf(st, "Physical media is Double Sided/Quad Density, 96 tpi, 250kbps MFM encoding.\n");

    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    fprint_reg_help(st, dptr);

    return SCPE_OK;
}

/*
 * Compute the offset of the currently selected C/H/S (in # of sectors)
 */
static SIM_INLINE uint32 if_lba()
{
    /* Reminder that sectors are numbered 1-9 instead
     * of being numbered 0-8 */
    return((if_state.track * IF_SEC_COUNT * 2) +
           (if_state.side * IF_SEC_COUNT) +
           (if_state.sector - 1));
}

void if_after_dma()
{
    if_state.drq = FALSE;
    if_state.status &= ~IF_DRQ;
}
