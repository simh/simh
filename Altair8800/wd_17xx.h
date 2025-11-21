/* wd_17xx.h: Western Digital FD17XX Floppy Disk Controller/Formatter

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

   History:
   13-Nov-2025 Initial version

*/

#ifndef _WD_17XX_H
#define _WD_17XX_H

#include "altair8800_dsk.h"

#define WD17XX_MAX_SECTOR_SIZE  4096

typedef struct {
    uint16 fdctype;     /* Default is 1771 */
    uint8 intenable;    /* Interrupt Enable */
    uint8 intvector;    /* Interrupt Vector */
    uint8 drq;          /* WD17XX DMA Request Output */
    uint8 intrq;        /* WD17XX Interrupt Request Output (EOJ) */
    uint8 hld;          /* WD17XX Head Load Output */
    uint8 dden;         /* WD17XX Double-Density Input */
    uint8 verify;       /* Verify */
    uint8 drivetype;    /* 8 or 5 depending on disk type. */
    uint8 status;       /* Status Register */
    uint8 command;      /* Command Register */
    uint8 track;        /* Track Register */
    uint8 side;         /* Side select */
    uint8 sector;       /* Sector Register */
    uint8 data;         /* Data Register */
    DSK_INFO *dsk;      /* Current DSK information */
    uint8 cmdtype;      /* Command type */
    uint8 fdc_read;     /* TRUE when reading */
    uint8 fdc_readadr;  /* TRUE when reading address */
    uint8 fdc_write;    /* TRUE when writing */
    uint8 fdc_write_track;  /* TRUE when writing an entire track */
    uint8 fdc_multi;    /* Multiple records */
    uint8 fdc_sec_len;     /* Sector length field */
    uint16 fdc_datacount;   /* Read or Write data remaining transfer length */
    uint16 fdc_dataindex;   /* index of current byte in sector data */
    uint8 fdc_fmt_state;    /* Format track statemachine state */
    uint8 fdc_fmt_track;
    uint8 fdc_fmt_side;
    uint8 fdc_fmt_sector;
    uint8 fdc_gap[4];       /* Gap I - Gap IV lengths */
    uint8 fdc_fmt_sector_count; /* sector count for format track */
    uint8 fdc_header_index; /* Index into header */
    int8 fdc_step_dir;
    DEVICE *dptr;       /* Device Owner */
    uint32 dbg_verbose; /* Verbose debug flag */
    uint32 dbg_error;   /* Error debug flag */
    uint32 dbg_read;    /* read debug flag */
    uint32 dbg_write;   /* write debug flag */
    uint32 dbg_command; /* command debug flag */
    uint32 dbg_format;  /* format debug flag */
} WD17XX_INFO;

#define WD17XX_FDCTYPE_1771 0x01
#define WD17XX_FDCTYPE_1791 0x02
#define WD17XX_FDCTYPE_1793 0x02
#define WD17XX_FDCTYPE_1795 0x04
#define WD17XX_FDCTYPE_1797 0x04

#define WD17XX_REG_STATUS   0x00
#define WD17XX_REG_COMMAND  0x00
#define WD17XX_REG_TRACK    0x01
#define WD17XX_REG_SECTOR   0x02
#define WD17XX_REG_DATA     0x03

#define WD17XX_CMD_MASK     0xf0    /* Command Mask */
#define WD17XX_CMD_RESTORE  0x00
#define WD17XX_CMD_SEEK     0x10
#define WD17XX_CMD_STEP     0x20
#define WD17XX_CMD_STEPU    0x30
#define WD17XX_CMD_STEPIN   0x40
#define WD17XX_CMD_STEPINU  0x50
#define WD17XX_CMD_STEPOUT  0x60
#define WD17XX_CMD_STEPOUTU 0x70
#define WD17XX_CMD_RD       0x80
#define WD17XX_CMD_RDM      0x90
#define WD17XX_CMD_WR       0xA0
#define WD17XX_CMD_WRM      0xB0
#define WD17XX_CMD_RDADR    0xC0
#define WD17XX_CMD_RDTRK    0xE0
#define WD17XX_CMD_WRTRK    0xF0
#define WD17XX_CMD_FI       0xD0

#define WD17XX_FLG_F1       0x02
#define WD17XX_FLG_V        0x04
#define WD17XX_FLG_F2       0x08
#define WD17XX_FLG_H        0x08
#define WD17XX_FLG_B        0x08    /* Block Length Flag (1771) */
#define WD17XX_FLG_U        0x10    /* Update track register */
#define WD17XX_FLG_M        0x10    /* Multiple record flag  */

#define WD17XX_STAT_BUSY    0x01    /* S0 - Busy */
#define WD17XX_STAT_IDX     0x02    /* S1 - Index */
#define WD17XX_STAT_DRQ     0x02    /* S1 - Data Request */
#define WD17XX_STAT_TRK0    0x04    /* S2 - Track 0 */
#define WD17XX_STAT_LOSTD   0x04    /* S2 - Lost Data */
#define WD17XX_STAT_CRC     0x08    /* S3 - CRC Error */
#define WD17XX_STAT_SEEK    0x10    /* S4 - Seek Error */
#define WD17XX_STAT_RNF     0x10    /* S4 - Record Not Found */
#define WD17XX_STAT_HDLD    0x20    /* S5 - Head Loaded */
#define WD17XX_STAT_RT      0x20    /* S5 - Record Type */
#define WD17XX_STAT_WF      0x20    /* S5 - Write Fault */
#define WD17XX_STAT_WP      0x40    /* S6 - Write Protect */
#define WD17XX_STAT_NRDY    0x80    /* S7 - Not Ready */

/* Write Track (format) Statemachine states */
#define WD17XX_FMT_GAP1    1
#define WD17XX_FMT_GAP2    2
#define WD17XX_FMT_GAP3    3
#define WD17XX_FMT_GAP4    4
#define WD17XX_FMT_HEADER  5
#define WD17XX_FMT_DATA    6

extern WD17XX_INFO * wd17xx_init(DEVICE *dptr);
extern WD17XX_INFO * wd17xx_release(WD17XX_INFO *wd);
extern void wd17xx_reset(WD17XX_INFO *wd);
extern void wd17xx_sel_dden(WD17XX_INFO *wd, uint8 dden);
extern void wd17xx_sel_side(WD17XX_INFO *wd, uint8 side);
extern void wd17xx_sel_drive_type(WD17XX_INFO *wd, uint8 type);
extern void wd17xx_set_fdctype(WD17XX_INFO *wd, int fdctype);
extern void wd17xx_set_dsk(WD17XX_INFO *wd, DSK_INFO *dsk);
extern void wd17xx_set_intena(WD17XX_INFO *wd, int32 ena);
extern void wd17xx_set_intvec(WD17XX_INFO *wd, int32 vec);
extern void wd17xx_set_verbose_flag(WD17XX_INFO *wd, uint32 flag);
extern void wd17xx_set_error_flag(WD17XX_INFO *wd, uint32 flag);
extern void wd17xx_set_read_flag(WD17XX_INFO *wd, uint32 flag);
extern void wd17xx_set_write_flag(WD17XX_INFO *wd, uint32 flag);
extern void wd17xx_set_command_flag(WD17XX_INFO *wd, uint32 flag);
extern void wd17xx_set_format_flag(WD17XX_INFO *wd, uint32 flag);
extern uint8 wd17xx_intrq(WD17XX_INFO *wd);
extern uint8 wd17xx_inp(WD17XX_INFO *wd, uint8 port);
extern void wd17xx_outp(WD17XX_INFO *wd, uint8 port, uint8 data);
extern void wd17xx_show(WD17XX_INFO *wd);

#endif

