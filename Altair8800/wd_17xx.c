/* wd_17xx.c: Western Digital FD17XX Floppy Disk Controller/Formatter

   Copyright (c) 2025 Patrick A. Linstruth

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   Based on work by Howard M. Harte 2007-2022

   History:
   13-Nov-2025 Initial version

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "s100_bus.h"
#include "wd_17xx.h"

#define WD17XX_NAME "WD17XX"

static uint8 sbuf[WD17XX_MAX_SECTOR_SIZE];

static void wd17xx_command(WD17XX_INFO *wd, uint8 data);
static int32 wd17xx_valid(WD17XX_INFO *wd);
static uint8 wd17xx_sec_len(WD17XX_INFO *wd);
static t_stat wd17xx_read_sector(WD17XX_INFO *wd, uint8 *sbuf, int32 *bytesread);
static t_stat wd17xx_write_sector(WD17XX_INFO *wd, uint8 *sbuf, int32 *byteswritten);
static void wd17xx_set_intrq(WD17XX_INFO *wd, int32 value);

WD17XX_INFO * wd17xx_init(DEVICE *dptr)
{
    WD17XX_INFO *wd;

    if ((dptr == NULL) || (wd = malloc(sizeof(WD17XX_INFO))) == NULL) {
        return NULL;
    }

    memset(wd, 0x00, sizeof(WD17XX_INFO));

    /* Save device */
    wd->dptr = dptr;

    return wd;
}

WD17XX_INFO * wd17xx_release(WD17XX_INFO *wd)
{
    if (wd != NULL) {
        free(wd);
    }

    return NULL;
}

void wd17xx_reset(WD17XX_INFO *wd)
{
    if (wd != NULL) {
        wd->intrq = TRUE;
        wd->drq = FALSE;
        wd->status = 0;
        wd->track = 0;
        wd->fdc_write = FALSE;
        wd->fdc_read = FALSE;
        wd->fdc_write_track = FALSE;
        wd->fdc_readadr = FALSE;
        wd->fdc_datacount = 0;
        wd->fdc_dataindex = 0;
        wd->fdc_sec_len = wd17xx_sec_len(wd);
    }
}

void wd17xx_set_intena(WD17XX_INFO *wd, int32 ena) {
    if (wd != NULL) {
        wd->intenable = ena;
    }
}

void wd17xx_set_intvec(WD17XX_INFO *wd, int32 vec) {
    if (wd != NULL) {
        wd->intvector = vec;
    }
}

void wd17xx_set_verbose_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_verbose = flag;
    }
}

void wd17xx_set_error_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_error = flag;
    }
}

void wd17xx_set_command_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_command = flag;
    }
}

void wd17xx_set_read_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_read = flag;
    }
}

void wd17xx_set_write_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_write = flag;
    }
}

void wd17xx_set_format_flag(WD17XX_INFO *wd, uint32 flag)
{
    if (wd != NULL) {
        wd->dbg_format = flag;
    }
}

void wd17xx_sel_dden(WD17XX_INFO *wd, uint8 dden)
{
    if (wd != NULL) {
        wd->dden = dden;
    }
}

void wd17xx_sel_side(WD17XX_INFO *wd, uint8 side)
{
    if (wd != NULL) {
        wd->side = side;
        wd->fdc_sec_len = wd17xx_sec_len(wd);
    }
}

void wd17xx_sel_drive_type(WD17XX_INFO *wd, uint8 type)
{
    if (wd != NULL) {
        wd->drivetype = type;
    }
}

uint8 wd17xx_intrq(WD17XX_INFO *wd)
{
    if (wd != NULL) {
        return wd->intrq;
    }

    return 0xff;
}

void wd17xx_set_fdctype(WD17XX_INFO *wd, int fdctype)
{
    if (wd != NULL) {
        wd->fdctype = fdctype;
    }
}

void wd17xx_set_dsk(WD17XX_INFO *wd, DSK_INFO *dsk)
{
    if (wd != NULL) {
        wd->dsk = dsk;
    }
}

uint8 wd17xx_inp(WD17XX_INFO *wd, uint8 port)
{
    uint8 r = 0xff;
    int32 bytesread;

    sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME " INP %02X\n", port);

    if (wd == NULL || wd->dsk == NULL) {
        return r;
    }

    switch (port) {
        case WD17XX_REG_STATUS:
            /* Fix up status based on Command Type */
            if ((wd->cmdtype == 0) || (wd->cmdtype == 1) || (wd->cmdtype == 4)) {
                wd->status ^= WD17XX_STAT_IDX;   /* Generate Index pulses */
                wd->status &= ~WD17XX_STAT_TRK0;
                wd->status |= (wd->track == 0) ? WD17XX_STAT_TRK0 : 0;
            }
            else { /* Command Type 3 */
                wd->status &= ~WD17XX_STAT_IDX;  /* Mask index pulses */
                wd->status |= (wd->drq) ? WD17XX_STAT_DRQ : 0;
            }

            wd->status &= ~WD17XX_STAT_NRDY;
            wd->status |= (wd->dsk->unit == NULL || wd->dsk->unit->fileref == NULL) ? WD17XX_STAT_NRDY : 0;

            sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                      " RD STATUS = 0x%02x, CMDTYPE=%x\n", s100_bus_get_addr(), wd->status, wd->cmdtype);

            r = wd->status;
            break;

        case WD17XX_REG_TRACK:
            r = wd->track;
            break;

        case WD17XX_REG_SECTOR:
            r = wd->sector;
            break;

        case WD17XX_REG_DATA:
            r = 0xFF;      /* Return High-Z data */
            if (wd->fdc_read == TRUE) {
                if (wd->fdc_dataindex < wd->fdc_datacount) {
                    wd->status &= ~(WD17XX_STAT_BUSY);       /* Clear BUSY */
                    wd->data = sbuf[wd->fdc_dataindex];
                    r = wd->data;

                    if (wd->fdc_readadr == TRUE) {
                        sim_debug(wd->dbg_read, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " READ_ADDR[%d/%d] = 0x%02x\n", s100_bus_get_addr(), wd->fdc_dataindex, wd->fdc_datacount, wd->data);
                    }

                    wd->fdc_dataindex++;
                    if (wd->fdc_dataindex == wd->fdc_datacount) {
                        if (wd->fdc_multi == FALSE) {
                            wd->status &= ~(WD17XX_STAT_DRQ | WD17XX_STAT_BUSY); /* Clear DRQ, BUSY */
                            wd17xx_set_intrq(wd, TRUE);
                            wd->fdc_read = FALSE;
                            wd->fdc_readadr = FALSE;
                        } else {
                            wd->sector++;

                            sim_debug(wd->dbg_read, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " MULTI_READ_REC, T:%2d/H:%d/S:%2d, %s, len=%d\n",
                                s100_bus_get_addr(), wd->track, wd->side, wd->sector, wd->dden ? "DD" : "SD",
                                dsk_sector_size(wd->dsk, wd->track, wd->side));

                            if (wd->dsk->unit->fileref == NULL) {
                                sim_debug(wd->dbg_error, wd->dptr, ".fileref is NULL!\n");
                            } else {
                                dsk_read_sector(wd->dsk, wd->track, wd->side, wd->sector, sbuf, &bytesread);
                            }
                        }
                    }
                }
            }
            break;

        default:
            break;
    }

    return r;
}

void wd17xx_outp(WD17XX_INFO *wd, uint8 port, uint8 data)
{
    int32 byteswritten;

    sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME " OUTP %02X %02X\n", port, data);

    if (wd == NULL || wd->dsk == NULL) {
        return;
    }

    switch (port) {
        case WD17XX_REG_COMMAND:
            wd->fdc_read = FALSE;
            wd->fdc_write = FALSE;
            wd->fdc_write_track = FALSE;
            wd->fdc_datacount = 0;
            wd->fdc_dataindex = 0;
            if (wd->intenable) {
                s100_bus_int(1 << wd->intvector, wd->intvector * 2);
            }

            wd17xx_command(wd, data);
            break;

        case WD17XX_REG_TRACK:
            wd->track = data;
            wd->fdc_sec_len = wd17xx_sec_len(wd);
            break;

        case WD17XX_REG_SECTOR:
            wd->sector = data;
            break;

        case WD17XX_REG_DATA:
        sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " WR DATA  = 0x%02x\n", s100_bus_get_addr(), data);
        if (wd->fdc_write == TRUE) {
            if (wd->fdc_dataindex < wd->fdc_datacount) {
                sbuf[wd->fdc_dataindex] = data;

                wd->fdc_dataindex++;
                if (wd->fdc_dataindex == wd->fdc_datacount) {
                    wd->status &= ~(WD17XX_STAT_DRQ | WD17XX_STAT_BUSY); /* Clear DRQ, BUSY */
                    wd17xx_set_intrq(wd, TRUE);
                    if (wd->intenable) {
                        s100_bus_int(1 << wd->intvector, wd->intvector * 2);
                    }

                    sim_debug(wd->dbg_write, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " Writing sector, T:%2d/S:%d/H:%2d, Len=%d\n",
                        s100_bus_get_addr(), wd->track, wd->side, wd->sector,
                        dsk_sector_size(wd->dsk, wd->track, wd->side));

                    wd17xx_write_sector(wd, sbuf, &byteswritten);

                    wd->fdc_write = FALSE;
                }
            }
        }

        if (wd->fdc_write_track == TRUE) {
            if (wd->fdc_fmt_state == WD17XX_FMT_GAP1) {
                if (data != 0xFC && (data != 0x00 && wd->fdc_gap[0] < 32)) {
                    wd->fdc_gap[0]++;
                }
                else {
                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " FMT GAP1 Length = %d\n", s100_bus_get_addr(), wd->fdc_gap[0]);
                    wd->fdc_gap[1] = 0;
                    wd->fdc_fmt_state = WD17XX_FMT_GAP2;
                }
            } else if (wd->fdc_fmt_state == WD17XX_FMT_GAP2) {
                if (data != 0xFE) {
                    wd->fdc_gap[1]++;
                }
                else {
                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " FMT GAP2 Length = %d\n", s100_bus_get_addr(), wd->fdc_gap[1]);
                    wd->fdc_gap[2] = 0;
                    wd->fdc_fmt_state = WD17XX_FMT_HEADER;
                    wd->fdc_header_index = 0;
                }
            } else if (wd->fdc_fmt_state == WD17XX_FMT_HEADER) {
                if (wd->fdc_header_index == 5) {
                    wd->fdc_gap[2] = 0;
                    wd->fdc_fmt_state = WD17XX_FMT_GAP3;
                } else {
                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " HEADER[%d]=%02x\n", s100_bus_get_addr(), wd->fdc_header_index, data);

                    switch (wd->fdc_header_index) {
                        case 0:
                            wd->fdc_fmt_track = data;
                            break;
                        case 1:
                            wd->fdc_fmt_side = data;
                            break;
                        case 2:
                            wd->fdc_fmt_sector = data;
                            break;
                        case 3:
                        case 4:
                            break;
                    }

                    wd->fdc_header_index++;
                }
            } else if (wd->fdc_fmt_state == WD17XX_FMT_GAP3) {
                if (data != 0xFB) {
                    wd->fdc_gap[2]++;
                }
                else {
                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " FMT GAP3 Length = %d\n", s100_bus_get_addr(), wd->fdc_gap[2]);
                    wd->fdc_fmt_state = WD17XX_FMT_DATA;
                    wd->fdc_dataindex = 0;
                }
            } else if (wd->fdc_fmt_state == WD17XX_FMT_DATA) { /* data bytes */
                if (data != 0xF7) {
                    sbuf[wd->fdc_dataindex] = data;
                    wd->fdc_dataindex++;
                }
                else {
                    wd->fdc_sec_len = sys_floorlog2(wd->fdc_dataindex) - 7;

                    if (wd->fdc_sec_len > wd17xx_sec_len(wd)) { /* Error calculating N or N too large */
                        sim_debug(wd->dbg_error, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                            " Invalid sector size!\n", s100_bus_get_addr());
                        wd->fdc_sec_len = 0;
                    }
                    if (wd->fdc_fmt_sector_count >= dsk_sectors(wd->dsk, wd->track, wd->side)) {
                        sim_debug(wd->dbg_error, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                            " Illegal sector count\n", s100_bus_get_addr());

                        wd->fdc_fmt_sector_count = 0;
                    }

                    wd->fdc_fmt_sector_count++;

                    /* Write the sector to disk */
                    dsk_write_sector(wd->dsk, wd->track, wd->side, wd->fdc_fmt_sector_count, sbuf, NULL);

                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                        " FMT Data Length = %d\n", s100_bus_get_addr(), wd->fdc_dataindex);

                    sim_debug(wd->dbg_format, wd->dptr, WD17XX_NAME ADDRESS_FORMAT 
                        " FORMAT T:%2d (%02d)/H:%d (%02d)/S:%2d (%02d)/L=%d (%02X)\n", s100_bus_get_addr(),
                        wd->track, wd->fdc_fmt_track, wd->side, wd->fdc_fmt_side,
                        wd->fdc_fmt_sector_count, wd->fdc_fmt_sector,
                        wd->fdc_dataindex, wd->fdc_sec_len);

                    wd->fdc_gap[1] = 0;
                    wd->fdc_fmt_state = WD17XX_FMT_GAP2;

                    if (wd->fdc_fmt_sector_count == dsk_sectors(wd->dsk, wd->track, wd->side)) {
                        wd->status &= ~(WD17XX_STAT_BUSY | WD17XX_STAT_LOSTD);     /* Clear BUSY, LOST_DATA */
                        wd17xx_set_intrq(wd, TRUE);
                        if (wd->intenable) {
                            s100_bus_int(1 << wd->intvector, wd->intvector * 2);
                        }

                        /* Recalculate disk size */
                        wd->dsk->unit->capac = sim_fsize(wd->dsk->unit->fileref);
                    }
                }
            }
        }

        wd->data = data;
        break;

        default:
            break;
    }
}

static void wd17xx_command(WD17XX_INFO *wd, uint8 cmd)
{
    int32 bytesread;

    if (wd->status & WD17XX_STAT_BUSY) {
        if (((cmd & WD17XX_CMD_MASK) != WD17XX_CMD_FI)) {
            sim_debug(wd->dbg_error, wd->dptr, WD17XX_NAME " " ADDRESS_FORMAT
                      " ERROR: Command 0x%02x ignored because controller is BUSY\n\n", s100_bus_get_addr(), cmd);
        }
        return;
    }

    switch(cmd & WD17XX_CMD_MASK) {
        /* Type I Commands */
        case WD17XX_CMD_RESTORE:
        case WD17XX_CMD_SEEK:
        case WD17XX_CMD_STEP:
        case WD17XX_CMD_STEPU:
        case WD17XX_CMD_STEPIN:
        case WD17XX_CMD_STEPINU:
        case WD17XX_CMD_STEPOUT:
        case WD17XX_CMD_STEPOUTU:
            wd->cmdtype = 1;
            wd->status |= WD17XX_STAT_BUSY;        /* Set BUSY */
            wd->status &= ~(WD17XX_STAT_CRC | WD17XX_STAT_SEEK | WD17XX_STAT_DRQ);
            wd17xx_set_intrq(wd, FALSE);
            wd->hld = cmd & WD17XX_FLG_H;
            wd->verify = cmd & WD17XX_FLG_V;
            if (wd->fdctype == WD17XX_FDCTYPE_1795) {
                /* WD1795 and WD1797 have a side select output. */
                wd->side = (cmd & WD17XX_FLG_F1) >> 1;
            }
            break;

        /* Type II Commands */
        case WD17XX_CMD_RD:
        case WD17XX_CMD_RDM:
        case WD17XX_CMD_WR:
        case WD17XX_CMD_WRM:
            wd->cmdtype = 2;
            wd->status = WD17XX_STAT_BUSY;     /* Set BUSY, clear all others */
            wd17xx_set_intrq(wd, FALSE);
            wd->hld = 1;   /* Load the head immediately, E Flag not checked. */
            if (wd->fdctype != WD17XX_FDCTYPE_1771) {
                /* WD1795 and WD1797 have a side select output. */
                wd->side = (cmd & WD17XX_FLG_F1) >> 1;
            }
            break;

        /* Type III Commands */
        case WD17XX_CMD_RDADR:
        case WD17XX_CMD_RDTRK:
        case WD17XX_CMD_WRTRK:
            wd->cmdtype = 3;
            break;

        /* Type IV Commands */
        case WD17XX_CMD_FI:
            wd->cmdtype = 4;
            break;

        default:
            wd->cmdtype = 0;
            sim_debug(wd->dbg_error, wd->dptr, WD17XX_NAME " Invalid command %02X\n", cmd);
            break;
    }


    switch(cmd & WD17XX_CMD_MASK) {

        /* Type I Commands */
        case WD17XX_CMD_RESTORE:
            wd->track = 0;
            wd17xx_set_intrq(wd, TRUE);

            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=RESTORE %s\n", s100_bus_get_addr(), wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_SEEK:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT
                      " CMD=SEEK, track=%d, new=%d %s\n", s100_bus_get_addr(), wd->track, wd->data, wd->verify ? "[VERIFY]" : "");

            wd->track = wd->data;
            break;

        case WD17XX_CMD_STEP:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP %s\n", s100_bus_get_addr(), wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_STEPU:
            if (wd->fdc_step_dir == 1) {
                if (wd->track < wd->dsk->fmt.tracks - 1) {
                    wd->track++;
                }
            } else if (wd->fdc_step_dir == -1) {
                if (wd->track > 0) {
                    wd->track--;
                }
            }
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP_U dir=%d track=%d %s\n", s100_bus_get_addr(), wd->fdc_step_dir, wd->track, wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_STEPIN:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP_IN %s\n", s100_bus_get_addr(), wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_STEPINU:
            if (wd->track < wd->dsk->fmt.tracks - 1) {
                wd->track++;
            }

            wd->fdc_step_dir = 1;

            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP_IN_U, track=%d %s\n", s100_bus_get_addr(), wd->track, wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_STEPOUT:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP_OUT %s\n", s100_bus_get_addr(), wd->verify ? "[VERIFY]" : "");
            break;

        case WD17XX_CMD_STEPOUTU:
            if (wd->track > 0) {
                wd->track--;
            }

            wd->fdc_step_dir = -1;

            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=STEP_OUT_U, track=%d %s\n", s100_bus_get_addr(), wd->track, wd->verify ? "[VERIFY]" : "");
            break;

        /* Type II Commands */
        case WD17XX_CMD_RD:
        case WD17XX_CMD_RDM:
            wd->fdc_multi = (cmd & WD17XX_FLG_M) ? TRUE : FALSE;

            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=READ_REC, T:%2d/H:%d/S:%2d, %s, %s len=%d\n", s100_bus_get_addr(), wd->track,
                      wd->side, wd->sector,
                      wd->fdc_multi ? "Multiple" : "Single",
                      wd->dden ? "DD" : "SD", wd->dsk->fmt.track[wd->track][wd->side].sectorsize);

            if (wd17xx_valid(wd) == FALSE) {
                wd->status |= WD17XX_STAT_RNF; /* Record not found */
                wd->status &= ~WD17XX_STAT_BUSY;
                wd17xx_set_intrq(wd, TRUE);
            } else {
                wd17xx_read_sector(wd, sbuf, &bytesread);
            }
            break;

        case WD17XX_CMD_WR:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=WRITE_REC, T:%2d/H:%d/S:%2d, %s.\n", s100_bus_get_addr(), wd->track, wd->side, wd->sector, (cmd & WD17XX_FLG_M) ? "Multiple" : "Single");

            wd->status |= (WD17XX_STAT_DRQ);       /* Set DRQ */
            wd->status |= (wd->dsk->unit->flags & UNIT_RO) ? WD17XX_STAT_WP : 0;       /* Set WP  */
            wd->drq = 1;
            wd->fdc_datacount = dsk_sector_size(wd->dsk, wd->track, wd->side);
            wd->fdc_dataindex = 0;
            wd->fdc_write = TRUE;
            wd->fdc_write_track = FALSE;
            wd->fdc_read = FALSE;
            wd->fdc_readadr = FALSE;

            sbuf[wd->fdc_dataindex] = wd->data;
            break;

        case WD17XX_CMD_WRM:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " Error: WRITE_RECS not implemented.\n", s100_bus_get_addr());
            break;

        /* Type III Commands */
        case WD17XX_CMD_RDADR:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=READ_ADDR, T:%d/S:%d, %s\n", 
                      s100_bus_get_addr(), wd->track, wd->side, wd->dden ? "DD" : "SD");

            if (wd17xx_valid(wd) == FALSE) {
                wd->status = WD17XX_STAT_RNF; /* Record not found */
                wd17xx_set_intrq(wd, TRUE);
            } else {
                wd->status = (WD17XX_STAT_DRQ | WD17XX_STAT_BUSY);     /* Set DRQ, BUSY */
                wd->drq = 1;
                wd->fdc_datacount = 6;
                wd->fdc_dataindex = 0;
                wd->fdc_read = TRUE;
                wd->fdc_readadr = TRUE;

                sbuf[0] = wd->track;
                sbuf[1] = wd->side;
                sbuf[2] = (wd->sector < dsk_start_sector(wd->dsk, wd->track, wd->side)) ? wd->sector : dsk_start_sector(wd->dsk, wd->track, wd->side);
                sbuf[3] = wd->fdc_sec_len;
                sbuf[4] = 0xAA; /* CRC1 */
                sbuf[5] = 0x55; /* CRC2 */

                wd->sector = wd->track;

                wd->status &= ~(WD17XX_STAT_BUSY);     /* Clear BUSY */

                wd17xx_set_intrq(wd, TRUE);
            }
            break;

        case WD17XX_CMD_RDTRK:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=READ_TRACK\n", s100_bus_get_addr());
            sim_debug(wd->dbg_error, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " Error: READ_TRACK not implemented.\n", s100_bus_get_addr());
            break;

        case WD17XX_CMD_WRTRK:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=WRITE_TRACK, T:%2d/H:%d/S:%d.\n",
                      s100_bus_get_addr(), wd->track, wd->side,
                      wd->dsk->fmt.track[wd->track][wd->side].sectorsize);

            wd->status |= (WD17XX_STAT_DRQ);       /* Set DRQ */
            wd->status |= (wd->dsk->unit->flags & UNIT_RO) ? WD17XX_STAT_WP : 0;       /* Set WP  */
            wd17xx_set_intrq(wd, FALSE);
            wd->fdc_datacount = dsk_sector_size(wd->dsk, wd->track, wd->side);
            wd->fdc_dataindex = 0;
            wd->fdc_write = FALSE;
            wd->fdc_write_track = TRUE;
            wd->fdc_read = FALSE;
            wd->fdc_readadr = FALSE;
            wd->fdc_fmt_state = WD17XX_FMT_GAP1;  /* TRUE when writing an entire track */
            wd->fdc_fmt_sector_count = 0;

            break;

        /* Type IV Commands */
        case WD17XX_CMD_FI:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " CMD=FORCE_INTR\n", s100_bus_get_addr());
            if ((cmd & WD17XX_CMD_MASK) == 0) { /* I0-I3 == 0, no intr, but clear BUSY and terminate command */
                wd->status &= ~(WD17XX_STAT_DRQ | WD17XX_STAT_BUSY); /* Clear DRQ, BUSY */
                wd->drq = 0;
                wd->fdc_write = FALSE;
                wd->fdc_read = FALSE;
                wd->fdc_write_track = FALSE;
                wd->fdc_readadr = FALSE;
                wd->fdc_datacount = 0;
                wd->fdc_dataindex = 0;
            }
            else if (cmd & 0x08) {   /* Immediate Interrupt */
                wd17xx_set_intrq(wd, TRUE);

                if (wd->intenable) {
                    s100_bus_int(1 << wd->intvector, wd->intvector * 2);
                }
                wd->status &= ~(WD17XX_STAT_BUSY);     /* Clear BUSY */
            }
            else {                  /* Other interrupts not implemented yet */
                wd->status &= ~(WD17XX_STAT_BUSY);     /* Clear BUSY */
            }
            break;

        default:
            sim_debug(wd->dbg_command, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " ERROR: Unknown command 0x%02x.\n\n", s100_bus_get_addr(), cmd);
            break;
    }

    /* Post processing of Type-specific command */
    switch(cmd & WD17XX_CMD_MASK) {

        /* Type I Commands */
        case WD17XX_CMD_RESTORE:
        case WD17XX_CMD_SEEK:
        case WD17XX_CMD_STEP:
        case WD17XX_CMD_STEPU:
        case WD17XX_CMD_STEPIN:
        case WD17XX_CMD_STEPINU:
        case WD17XX_CMD_STEPOUT:
        case WD17XX_CMD_STEPOUTU:
            if (wd->verify) { /* Verify the selected track/side is ok. */
                sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME ADDRESS_FORMAT " Verify ", s100_bus_get_addr());
                if (dsk_validate(wd->dsk, wd->track, 0, 1) != SCPE_OK) { /* Not validating side or sector */
                        sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME "FAILED\n");
                        wd->status |= WD17XX_STAT_SEEK; /* Seek error */
                } else {
                        sim_debug(wd->dbg_verbose, wd->dptr, WD17XX_NAME "Ok\n");
                }
            }

            wd->status &= ~(WD17XX_STAT_TRK0);
            if (wd->track == 0) {
                wd->status |= WD17XX_STAT_TRK0;
            }

            wd->fdc_sec_len = wd17xx_sec_len(wd);

            wd->status &= ~(WD17XX_STAT_BUSY);     /* Clear BUSY */
            wd17xx_set_intrq(wd, TRUE);

            if (wd->intenable) {
                s100_bus_int(1 << wd->intvector, wd->intvector * 2);
            }

            break;

        /* Type II Commands */
        case WD17XX_CMD_RD:
        case WD17XX_CMD_RDM:
        case WD17XX_CMD_WR:
        case WD17XX_CMD_WRM:

        /* Type III Commands */
        case WD17XX_CMD_RDADR:
        case WD17XX_CMD_RDTRK:
        case WD17XX_CMD_WRTRK:
            wd->status &= ~(WD17XX_STAT_BUSY);     /* Clear BUSY */

            if (wd->intenable) {
                wd17xx_set_intrq(wd, TRUE);
                s100_bus_int(1 << wd->intvector, wd->intvector * 2);
            }

            wd->drq = 1;
            break;

        /* Type IV Commands */
        case WD17XX_CMD_FI:
        default:
            break;
    }
}

static t_stat wd17xx_read_sector(WD17XX_INFO *wd, uint8 *sbuf, int32 *bytesread)
{
    t_stat r;

    if (wd == NULL || wd->dsk == NULL) {
        return SCPE_ARG;
    }

    if ((r = dsk_read_sector(wd->dsk, wd->track, wd->side, wd->sector, sbuf, bytesread)) == SCPE_OK) {
        wd->status |= (WD17XX_STAT_DRQ | WD17XX_STAT_BUSY); /* Set DRQ, BUSY */
        wd17xx_set_intrq(wd, FALSE);
        wd->fdc_datacount = dsk_sector_size(wd->dsk, wd->track, wd->side);
        wd->fdc_dataindex = 0;
        wd->fdc_read = TRUE;
        wd->fdc_readadr = FALSE;
    }
    else {
        wd->status &= ~WD17XX_STAT_BUSY; /* Clear DRQ, BUSY */
        wd->status |= WD17XX_STAT_RNF;
        wd17xx_set_intrq(wd, TRUE);
        wd->fdc_read = FALSE;
        wd->fdc_readadr = FALSE;
    }

    return r;
}

static t_stat wd17xx_write_sector(WD17XX_INFO *wd, uint8 *sbuf, int32 *byteswritten)
{
    t_stat r;

    if (wd == NULL || wd->dsk == NULL) {
        return SCPE_ARG;
    }

    r = dsk_write_sector(wd->dsk, wd->track, wd->side, wd->sector, sbuf, byteswritten);

    return r;
}

static int32 wd17xx_valid(WD17XX_INFO *wd)
{
    return TRUE;
}

/* Convert sector size to sector length field */
static uint8 wd17xx_sec_len(WD17XX_INFO *wd)
{
    uint8 i;
    int32 secsize;

    if (wd == NULL || wd->dsk == NULL) {
        return 0;
    }

    secsize = dsk_sector_size(wd->dsk, wd->track, wd->side);

    for (i = 0; i <= 4; i++) {
        if ( (0x80 << i) == secsize ) {
            sim_debug(wd->dbg_verbose | wd->dbg_write, wd->dptr, "%d sector size = %02X sector len field\n", secsize, i);
            return i;
        }
    }

    return 0;  /* default to 128-byte sectors */
}

static void wd17xx_set_intrq(WD17XX_INFO *wd, int32 value)
{
    wd->intrq = (value) ? TRUE : FALSE;  /* INTRQ and DRQ are mutually exclusive */
    wd->drq = !wd->intrq;
}

void wd17xx_show(WD17XX_INFO *wd)
{
    sim_debug(wd->dbg_verbose, wd->dptr, "fdctype: %02X\n", wd->fdctype);
    sim_debug(wd->dbg_verbose, wd->dptr, "intenable: %02X\n", wd->intenable);
    sim_debug(wd->dbg_verbose, wd->dptr, "intvector: %02X\n", wd->intvector);
    sim_debug(wd->dbg_verbose, wd->dptr, "drq: %02X\n", wd->drq);
    sim_debug(wd->dbg_verbose, wd->dptr, "intrq: %02X\n", wd->intrq);
    sim_debug(wd->dbg_verbose, wd->dptr, "hld: %02X\n", wd->hld);
    sim_debug(wd->dbg_verbose, wd->dptr, "dden: %02X\n", wd->dden);
    sim_debug(wd->dbg_verbose, wd->dptr, "side: %02X\n", wd->side);
    sim_debug(wd->dbg_verbose, wd->dptr, "drivetype: %02X\n", wd->drivetype);
    sim_debug(wd->dbg_verbose, wd->dptr, "status: %02X\n", wd->status);
    sim_debug(wd->dbg_verbose, wd->dptr, "command: %02X\n", wd->command);
    sim_debug(wd->dbg_verbose, wd->dptr, "track: %02X\n", wd->track);
    sim_debug(wd->dbg_verbose, wd->dptr, "sector: %02X\n", wd->sector);
    sim_debug(wd->dbg_verbose, wd->dptr, "data: %02X\n", wd->data);
}

