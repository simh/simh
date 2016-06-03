/*************************************************************************
 *                                                                       *
 * $Id: wd179x.c 1999 2008-07-22 04:25:28Z hharte $                      *
 *                                                                       *
 * Copyright (c) 2007-2008 Howard M. Harte.                              *
 * http://www.hartetec.com                                               *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * Howard M. Harte.                                                      *
 *                                                                       *
 * SIMH Interface based on altairz80_hdsk.c, by Peter Schorn.            *
 *                                                                       *
 * Module Description:                                                   *
 *     Generic WD179X Disk Controller module for SIMH.                   *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#include "sim_imd.h"
#include "wd179x.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

#define CROMFDC_SIM_100US   291     /* Number of "ticks" in 100uS, where does this come from? */
#define CROMFDC_8IN_ROT     (167 * CROMFDC_SIM_100US)
#define CROMFDC_5IN_ROT     (200 * CROMFDC_SIM_100US)

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define SEEK_MSG    (1 << 1)
#define CMD_MSG     (1 << 2)
#define RD_DATA_MSG (1 << 3)
#define WR_DATA_MSG (1 << 4)
#define STATUS_MSG  (1 << 5)
#define FMT_MSG     (1 << 6)
#define VERBOSE_MSG (1 << 7)

#define WD179X_MAX_DRIVES   4
#define WD179X_SECTOR_LEN   8192
/* 2^(7 + WD179X_MAX_SEC_LEN) == WD179X_SECTOR_LEN */
#define WD179X_MAX_SEC_LEN  6
#define WD179X_MAX_SECTOR   26

#define CMD_PHASE 0
#define EXEC_PHASE 1
#define DATA_PHASE 2

/* Status Bits for Type I Commands */
#define WD179X_STAT_NOT_READY   (1 << 7)
#define WD179X_STAT_WPROT       (1 << 6)
#define WD179X_STAT_HLD         (1 << 5)
#define WD179X_STAT_SEEK_ERROR  (1 << 4)
#define WD179X_STAT_CRC_ERROR   (1 << 3)
#define WD179X_STAT_TRACK0      (1 << 2)
#define WD179X_STAT_INDEX       (1 << 1)
#define WD179X_STAT_BUSY        (1 << 0)

/* Status Bits for Type II, III Commands */
/*#define WD179X_STAT_NOT_READY (1 << 7) */
/*#define WD179X_STAT_WPROT     (1 << 6) */
#define WD179X_STAT_REC_TYPE    (1 << 5)    /* Also Write Fault */
#define WD179X_STAT_NOT_FOUND   (1 << 4)
/*#define WD179X_STAT_CRC_ERROR (1 << 3) */
#define WD179X_STAT_LOST_DATA   (1 << 2)
#define WD179X_STAT_DRQ         (1 << 1)
/*#define WD179X_STAT_BUSY      (1 << 0) */

typedef union {
    uint8 raw[WD179X_SECTOR_LEN];
} SECTOR_FORMAT;

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint8 ntracks;   /* number of tracks */
    uint8 nheads;    /* number of heads */
    uint32 sectsize; /* sector size, not including pre/postamble */
    uint8 track;     /* Current Track */
    uint8 ready;     /* Is drive ready? */
} WD179X_DRIVE_INFO;

typedef struct {
    PNP_INFO pnp;       /* Plug-n-Play Information */
    uint8 intrq;        /* WD179X Interrupt Request Output (EOJ) */
    uint8 hld;          /* WD179X Head Load Output */
    uint8 drq;          /* WD179X DMA Request Output */
    uint8 ddens;        /* WD179X Double-Density Input */
    uint8 fdc_head;     /* H Head Number */
    uint8 sel_drive;    /* Currently selected drive */
    uint8 drivetype;    /* 8 or 5 depending on disk type. */
    uint8 fdc_status;   /* WD179X Status Register */
    uint8 verify;       /* WD179X Type 1 command Verify flag */
    uint8 fdc_data;     /* WD179X Data Register */
    uint8 fdc_read;     /* TRUE when reading */
    uint8 fdc_write;    /* TRUE when writing */
    uint8 fdc_write_track;  /* TRUE when writing an entire track */
    uint8 fdc_fmt_state;    /* Format track statemachine state */
    uint8 fdc_gap[4];   /* Gap I - Gap IV lengths */
    uint8 fdc_fmt_sector_count; /* sector count for format track */
    uint8 fdc_sectormap[WD179X_MAX_SECTOR]; /* Physical to logical sector map */
    uint8 fdc_header_index; /* Index into header */
    uint8 fdc_read_addr;    /* TRUE when READ ADDRESS command is in progress */
    uint8 fdc_multiple; /* TRUE for multi-sector read/write */
    uint16 fdc_datacount;   /* Read or Write data remaining transfer length */
    uint16 fdc_dataindex;   /* index of current byte in sector data */
    uint8 index_pulse_wait; /* TRUE if waiting for interrupt on next index pulse. */
    uint8 fdc_sector;   /* R Record (Sector) */
    uint8 fdc_sec_len;  /* N Sector Length */
    int8 step_dir;
    uint8 cmdtype;      /* Type of current/former command */
    WD179X_DRIVE_INFO drive[WD179X_MAX_DRIVES];
} WD179X_INFO;

static SECTOR_FORMAT sdata;
extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 find_unit_index (UNIT *uptr);

t_stat wd179x_svc (UNIT *uptr);

/* These are needed for DMA.  PIO Mode has not been implemented yet. */
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);

#define UNIT_V_WD179X_WLK        (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_WD179X_WLK          (1 << UNIT_V_WD179X_WLK)
#define UNIT_V_WD179X_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_WD179X_VERBOSE      (1 << UNIT_V_WD179X_VERBOSE)
#define WD179X_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */
#define WD179X_CAPACITY_SSSD     (77*1*26*128)   /* Single-sided Single Density IBM Diskette1 */

/* Write Track (format) Statemachine states */
#define FMT_GAP1    1
#define FMT_GAP2    2
#define FMT_GAP3    3
#define FMT_GAP4    4
#define FMT_HEADER  5
#define FMT_DATA    6

/* WD179X Commands */
#define WD179X_RESTORE               0x00   /* Type I */
#define WD179X_SEEK                  0x10   /* Type I */
#define WD179X_STEP                  0x20   /* Type I */
#define WD179X_STEP_U                0x30   /* Type I */
#define WD179X_STEP_IN               0x40   /* Type I */
#define WD179X_STEP_IN_U             0x50   /* Type I */
#define WD179X_STEP_OUT              0x60   /* Type I */
#define WD179X_STEP_OUT_U            0x70   /* Type I */
#define WD179X_READ_REC              0x80   /* Type II */
#define WD179X_READ_RECS             0x90   /* Type II */
#define WD179X_WRITE_REC             0xA0   /* Type II */
#define WD179X_WRITE_RECS            0xB0   /* Type II */
#define WD179X_READ_ADDR             0xC0   /* Type III */
#define WD179X_FORCE_INTR            0xD0   /* Type IV */
#define WD179X_READ_TRACK            0xE0   /* Type III */
#define WD179X_WRITE_TRACK           0xF0   /* Type III */

static int32 wd179xdev(const int32 port, const int32 io, const int32 data);
static t_stat wd179x_reset(DEVICE *dptr);
static const char* wd179x_description(DEVICE *dptr);
uint8 floorlog2(unsigned int n);

WD179X_INFO wd179x_info_data = { { 0x0, 0, 0x30, 4 } };
WD179X_INFO *wd179x_info = &wd179x_info_data;
WD179X_INFO_PUB *wd179x_infop = (WD179X_INFO_PUB *)&wd179x_info_data;

static UNIT wd179x_unit[] = {
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 }
};

#define WD179X_NAME "Western Digital FDC Core"

static const char* wd179x_description(DEVICE *dptr) {
    return WD179X_NAME;
}

static MTAB wd179x_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { UNIT_WD179X_WLK,      0,                      "WRTENB",   "WRTENB",
        NULL, NULL, NULL, "Enables " WD179X_NAME "n for writing"                    },
    { UNIT_WD179X_WLK,      UNIT_WD179X_WLK,        "WRTLCK",   "WRTLCK",
        NULL, NULL, NULL, "Locks " WD179X_NAME "n for writing"                      },
    /* quiet, no warning messages       */
    { UNIT_WD179X_VERBOSE,  0,                      "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " WD179X_NAME "n"           },
    /* verbose, show warning messages   */
    { UNIT_WD179X_VERBOSE,  UNIT_WD179X_VERBOSE,    "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " WD179X_NAME "n"              },
    { 0 }
};

/* Debug Flags */
static DEBTAB wd179x_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "FMT",        FMT_MSG,        "Format messages"   },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE wd179x_dev = {
    "WD179X", wd179x_unit, NULL, wd179x_mod,
    WD179X_MAX_DRIVES, 10, 31, 1, WD179X_MAX_DRIVES, WD179X_MAX_DRIVES,
    NULL, NULL, &wd179x_reset,
    NULL, &wd179x_attach, &wd179x_detach,
    &wd179x_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    wd179x_dt, NULL, NULL, NULL, NULL, NULL, &wd179x_description
};

/* Unit service routine */
/* Used to generate INDEX pulses in response to a FORCE_INTR command */
t_stat wd179x_svc (UNIT *uptr)
{

    if(wd179x_info->index_pulse_wait == TRUE) {
        wd179x_info->index_pulse_wait = FALSE;
        wd179x_info->intrq = 1;
    }

    return SCPE_OK;
}


/* Reset routine */
static t_stat wd179x_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &wd179xdev, TRUE);
    } else {
        /* Connect I/O Ports at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &wd179xdev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

void wd179x_external_restore(void)
{
    WD179X_DRIVE_INFO    *pDrive;

    if(wd179x_info->sel_drive >= WD179X_MAX_DRIVES) {
        sim_debug(ERROR_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                  " Illegal drive selected, cannot restore.\n", PCX);
        return;
    }

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        sim_debug(ERROR_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                  " No drive selected, cannot restore.\n", PCX);
        return;
    }

    sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
              " External Restore drive to track 0\n", wd179x_info->sel_drive, PCX);

    pDrive->track = 0;

}

/* Attach routine */
t_stat wd179x_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;
    int32 i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    DBG_PRINT(("Attach WD179X%d\n", i));
    wd179x_info->drive[i].uptr = uptr;

    /* Default to drive not ready */
    wd179x_info->drive[i].ready = 0;

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if ((rtn != NULL) && strncmp(header, "IMD", 3)) {
            sim_printf("WD179X: Only IMD disk images are supported\n");
            wd179x_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
    } else {
        /* create a disk image file in IMD format. */
        if (diskCreate(uptr->fileref, "$Id: wd179x.c 1999 2008-07-22 04:25:28Z hharte $") != SCPE_OK) {
            sim_printf("WD179X: Failed to create IMD disk.\n");
            wd179x_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
        uptr->capac = sim_fsize(uptr->fileref);
    }

    uptr->u3 = IMAGE_TYPE_IMD;

    if (uptr->flags & UNIT_WD179X_VERBOSE)
        sim_printf("WD179X%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if (uptr->flags & UNIT_WD179X_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        wd179x_info->drive[i].imd = diskOpenEx(uptr->fileref, uptr->flags & UNIT_WD179X_VERBOSE,
                                               &wd179x_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_WD179X_VERBOSE)
            sim_printf("\n");
        if (wd179x_info->drive[i].imd == NULL) {
            sim_printf("WD179X: IMD disk corrupt.\n");
            wd179x_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }

        /* Write-protect the unit if IMD think's it's writelocked. */
        if(imdIsWriteLocked(wd179x_info->drive[i].imd)) {
            uptr->flags |= UNIT_WD179X_WLK;
        }

        wd179x_info->drive[i].ready = 1;
    } else {
        wd179x_info->drive[i].imd = NULL;
    }

    wd179x_info->fdc_sec_len = 0; /* 128 byte sectors, fixme */
    wd179x_info->sel_drive = 0;

    return SCPE_OK;
}


/* Detach routine */
t_stat wd179x_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return SCPE_IERR;
    }

    DBG_PRINT(("Detach WD179X%d\n", i));
    r = diskClose(&wd179x_info->drive[i].imd);
    wd179x_info->drive[i].ready = 0;
    if (r != SCPE_OK)
        return r;

    r = detach_unit(uptr);  /* detach unit */
    if (r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static int32 wd179xdev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("WD179X: " ADDRESS_FORMAT " %s, Port 0x%02x Data 0x%02x" NLP,
        PCX, io ? "OUT" : " IN", port, data));
    if(io) {
        WD179X_Write(port, data);
        return 0;
    } else {
        return(WD179X_Read(port));
    }
}

uint8 floorlog2(unsigned int n)
{
    /* Compute log2(n) */
    uint8 r = 0;
    if (n >= 1<<16) {
        n >>=16;
        r += 16;
    }
    if (n >= 1<< 8) {
        n >>= 8;
        r +=  8;
    }
    if (n >= 1<< 4) {
        n >>= 4;
        r +=  4;
    }
    if (n >= 1<< 2) {
        n >>= 2;
        r +=  2;
    }
    if (n >= 1<< 1) {
        r +=  1;
    }
    return ((n == 0) ? (0xFF) : r); /* 0xFF is error return value */
}

uint8 WD179X_Read(const uint32 Addr)
{
    uint8 cData;
    WD179X_DRIVE_INFO    *pDrive;
    uint32 flags = 0;
    uint32 readlen;
    int status;

    if(wd179x_info->sel_drive >= WD179X_MAX_DRIVES) {
        return 0xFF;
    }

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    cData = 0x00;

    switch(Addr & 0x3) {
        case WD179X_STATUS:
            /* Fix up status based on Command Type */
            if((wd179x_info->cmdtype == 1) || (wd179x_info->cmdtype == 4)) {
                wd179x_info->fdc_status ^= WD179X_STAT_INDEX;   /* Generate Index pulses */
                wd179x_info->fdc_status &= ~WD179X_STAT_TRACK0;
                wd179x_info->fdc_status |= (pDrive->track == 0) ? WD179X_STAT_TRACK0 : 0;
            } else if(wd179x_info->cmdtype == 4) {
            }
            else {
                wd179x_info->fdc_status &= ~WD179X_STAT_INDEX;  /* Mask index pulses */
                wd179x_info->fdc_status |= (wd179x_info->drq) ? WD179X_STAT_DRQ : 0;
            }
            cData = (pDrive->ready == 0) ? WD179X_STAT_NOT_READY : 0;
            cData |= wd179x_info->fdc_status;   /* Status Register */
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " RD STATUS = 0x%02x\n", PCX, cData);
            wd179x_info->intrq = 0;
            break;
        case WD179X_TRACK:
            cData = pDrive->track;
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " RD TRACK = 0x%02x\n", PCX, cData);
            break;
        case WD179X_SECTOR:
            cData = wd179x_info->fdc_sector;
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " RD SECT  = 0x%02x\n", PCX, cData);
            break;
        case WD179X_DATA:
            cData = 0xFF;      /* Return High-Z data */
            if(wd179x_info->fdc_read == TRUE) {
                if(wd179x_info->fdc_dataindex < wd179x_info->fdc_datacount) {
                    cData = sdata.raw[wd179x_info->fdc_dataindex];
                    if(wd179x_info->fdc_read_addr == TRUE) {
                        sim_debug(STATUS_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                                  " READ_ADDR[%d] = 0x%02x\n",
                                  wd179x_info->sel_drive, PCX,
                                  wd179x_info->fdc_dataindex, cData);
                    }

                    wd179x_info->fdc_dataindex++;
                    if(wd179x_info->fdc_dataindex == wd179x_info->fdc_datacount) {
                        if(wd179x_info->fdc_multiple == FALSE) {
                            wd179x_info->fdc_status &= ~(WD179X_STAT_DRQ | WD179X_STAT_BUSY);       /* Clear DRQ, BUSY */
                            wd179x_info->drq = 0;
                            wd179x_info->intrq = 1;
                            wd179x_info->fdc_read = FALSE;
                            wd179x_info->fdc_read_addr = FALSE;
                        } else {

                            /* Compute Sector Size */
                            wd179x_info->fdc_sec_len = floorlog2(
                                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
                            if((wd179x_info->fdc_sec_len == 0xF8) || (wd179x_info->fdc_sec_len > WD179X_MAX_SEC_LEN)) { /* Error calculating N or N too large */
                                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT " Invalid sector size!\n", wd179x_info->sel_drive, PCX);
                                wd179x_info->fdc_sec_len = 0;
                                return cData;
                            }

                            wd179x_info->fdc_sector ++;
                            sim_debug(RD_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT " MULTI_READ_REC, T:%d/S:%d/N:%d, %s, len=%d\n", wd179x_info->sel_drive, PCX, pDrive->track, wd179x_info->fdc_head, wd179x_info->fdc_sector, wd179x_info->ddens ? "DD" : "SD", 128 << wd179x_info->fdc_sec_len);

                            status = sectRead(pDrive->imd,
                                pDrive->track,
                                wd179x_info->fdc_head,
                                wd179x_info->fdc_sector,
                                sdata.raw,
                                128 << wd179x_info->fdc_sec_len,
                                &flags,
                                &readlen);

                            if(status == SCPE_OK) {
                                wd179x_info->fdc_status = (WD179X_STAT_DRQ | WD179X_STAT_BUSY);     /* Set DRQ, BUSY */
                                wd179x_info->drq = 1;
                                wd179x_info->intrq = 0;
                                wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
                                wd179x_info->fdc_dataindex = 0;
                                wd179x_info->fdc_read = TRUE;
                                wd179x_info->fdc_read_addr = FALSE;
                            } else {
                                wd179x_info->fdc_status = 0; /* Clear DRQ, BUSY */
                                wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;
                                wd179x_info->drq = 0;
                                wd179x_info->intrq = 1;
                                wd179x_info->fdc_read = FALSE;
                                wd179x_info->fdc_read_addr = FALSE;
                            }
                        }
                    }
                }
            }
            break;
    }

    return (cData);
}

/*
 * Command processing happens in three stages:
 * 1. Flags and initial conditions are set up based on the Type of the command.
 * 2. The execution phase takes place.
 * 3. Status is updated based on the Type and outcome of the command execution.
 *
 * See the WD179x-02 Datasheet available on www.hartetechnologies.com/manuals/
 *
 */
static uint8 Do1793Command(uint8 cCommand)
{
    uint8 result = 0;
    WD179X_DRIVE_INFO    *pDrive;
    uint32 flags = 0;
    uint32 readlen;
    int status;

    if(wd179x_info->sel_drive >= WD179X_MAX_DRIVES) {
        return 0xFF;
    }

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    if(wd179x_info->fdc_status & WD179X_STAT_BUSY) {
        if(((cCommand & 0xF0) != WD179X_FORCE_INTR)) { /* && ((cCommand & 0xF0) != WD179X_RESTORE)) { */
            sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " ERROR: Command 0x%02x ignored because controller is BUSY\n\n",
                      wd179x_info->sel_drive, PCX, cCommand);
        }
        return 0xFF;
    }

    wd179x_info->fdc_status &= ~WD179X_STAT_NOT_READY;

    /* Extract Type-specific command flags, and set initial conditions */
    switch(cCommand & 0xF0) {
        /* Type I Commands */
        case WD179X_RESTORE:
        case WD179X_SEEK:
        case WD179X_STEP:
        case WD179X_STEP_U:
        case WD179X_STEP_IN:
        case WD179X_STEP_IN_U:
        case WD179X_STEP_OUT:
        case WD179X_STEP_OUT_U:
            wd179x_info->cmdtype = 1;
            wd179x_info->fdc_status |= WD179X_STAT_BUSY;        /* Set BUSY */
            wd179x_info->fdc_status &= ~(WD179X_STAT_CRC_ERROR | WD179X_STAT_SEEK_ERROR | WD179X_STAT_DRQ);
            wd179x_info->intrq = 0;
            wd179x_info->hld = cCommand & 0x08;
            wd179x_info->verify = cCommand & 0x04;
            break;
        /* Type II Commands */
        case WD179X_READ_REC:
        case WD179X_READ_RECS:
        case WD179X_WRITE_REC:
        case WD179X_WRITE_RECS:
            wd179x_info->cmdtype = 2;
            wd179x_info->fdc_status = WD179X_STAT_BUSY;     /* Set BUSY, clear all others */
            wd179x_info->intrq = 0;
            wd179x_info->hld = 1;   /* Load the head immediately, E Flag not checked. */
            break;
        /* Type III Commands */
        case WD179X_READ_ADDR:
        case WD179X_READ_TRACK:
        case WD179X_WRITE_TRACK:
            wd179x_info->cmdtype = 3;
            break;
        /* Type IV Commands */
        case WD179X_FORCE_INTR:
            wd179x_info->cmdtype = 4;
            break;
        default:
            wd179x_info->cmdtype = 0;
            break;
    }

    switch(cCommand & 0xF0) {
        /* Type I Commands */
        case WD179X_RESTORE:
            sim_debug(CMD_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=RESTORE %s\n", wd179x_info->sel_drive, PCX,
                      wd179x_info->verify ? "[VERIFY]" : "");
            pDrive->track = 0;
            wd179x_info->intrq = 1;
            break;
        case WD179X_SEEK:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=SEEK, track=%d, new=%d\n", wd179x_info->sel_drive,
                      PCX, pDrive->track, wd179x_info->fdc_data);
            pDrive->track = wd179x_info->fdc_data;
            break;
        case WD179X_STEP:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP\n", wd179x_info->sel_drive, PCX);
            break;
        case WD179X_STEP_U:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP_U dir=%d\n", wd179x_info->sel_drive,
                      PCX, wd179x_info->step_dir);
            if(wd179x_info->step_dir == 1) {
                if (pDrive->track < 255)
                    pDrive->track++;
            } else if (wd179x_info->step_dir == -1) {
                if (pDrive->track > 0)
                    pDrive->track--;
            } else {
                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                          " ERROR: undefined direction for STEP\n",
                          wd179x_info->sel_drive, PCX);
            }
            break;
        case WD179X_STEP_IN:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP_IN\n", wd179x_info->sel_drive, PCX);
            break;
        case WD179X_STEP_IN_U:
            if (pDrive->track < 255)
                pDrive->track++;
            wd179x_info->step_dir = 1;
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP_IN_U, Track=%d\n", wd179x_info->sel_drive,
                      PCX, pDrive->track);
            break;
        case WD179X_STEP_OUT:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP_OUT\n", wd179x_info->sel_drive, PCX);
            break;
        case WD179X_STEP_OUT_U:
            sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=STEP_OUT_U\n", wd179x_info->sel_drive, PCX);
            if (pDrive->track > 0)
                pDrive->track--;
            wd179x_info->step_dir = -1;
            break;
        /* Type II Commands */
        case WD179X_READ_REC:
        case WD179X_READ_RECS:
            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if((wd179x_info->fdc_sec_len == 0xF8) || (wd179x_info->fdc_sec_len > WD179X_MAX_SEC_LEN)) { /* Error calculating N or N too large */
                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                          " Invalid sector size!\n", wd179x_info->sel_drive, PCX);
                wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;       /* Sector not found */
                wd179x_info->fdc_status &= ~WD179X_STAT_BUSY;
                wd179x_info->intrq = 1;
                wd179x_info->drq = 0;
                wd179x_info->fdc_sec_len = 0;
                return 0xFF;
            }

            wd179x_info->fdc_multiple = (cCommand & 0x10) ? TRUE : FALSE;
            sim_debug(RD_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=READ_REC, T:%d/S:%d/N:%d, %s, %s len=%d\n",
                      wd179x_info->sel_drive, PCX, pDrive->track,
                      wd179x_info->fdc_head, wd179x_info->fdc_sector,
                      wd179x_info->fdc_multiple ? "Multiple" : "Single",
                      wd179x_info->ddens ? "DD" : "SD", 128 << wd179x_info->fdc_sec_len);

            if(IMD_MODE_MFM(pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].mode) != (wd179x_info->ddens)) {
                wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;       /* Sector not found */
                wd179x_info->fdc_status &= ~WD179X_STAT_BUSY;
                wd179x_info->intrq = 1;
                wd179x_info->drq = 0;
            } else {

                status = sectRead(pDrive->imd,
                    pDrive->track,
                    wd179x_info->fdc_head,
                    wd179x_info->fdc_sector,
                    sdata.raw,
                    128 << wd179x_info->fdc_sec_len,
                    &flags,
                    &readlen);

                    if(status == SCPE_OK) {
                        wd179x_info->fdc_status |= (WD179X_STAT_DRQ);       /* Set DRQ */
                        wd179x_info->drq = 1;
                        wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
                        wd179x_info->fdc_dataindex = 0;
                        wd179x_info->fdc_write = FALSE;
                        wd179x_info->fdc_write_track = FALSE;
                        wd179x_info->fdc_read = TRUE;
                        wd179x_info->fdc_read_addr = FALSE;
                    } else {
                        wd179x_info->fdc_status = 0; /* Clear DRQ, BUSY */
                        wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;
                        wd179x_info->fdc_status &= ~WD179X_STAT_BUSY;
                        wd179x_info->drq = 0;
                        wd179x_info->intrq = 1;
                        wd179x_info->fdc_read = FALSE;
                        wd179x_info->fdc_read_addr = FALSE;
                    }
            }
            break;
        case WD179X_WRITE_RECS:
            sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " Error: WRITE_RECS not implemented.\n", wd179x_info->sel_drive, PCX);
            break;
        case WD179X_WRITE_REC:
            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if((wd179x_info->fdc_sec_len == 0xF8) || (wd179x_info->fdc_sec_len > WD179X_MAX_SEC_LEN)) { /* Error calculating N or N too large */
                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                          " Invalid sector size!\n", wd179x_info->sel_drive, PCX);
                wd179x_info->fdc_sec_len = 0;
            }

            sim_debug(WR_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=WRITE_REC, T:%d/S:%d/N:%d, %s.\n", wd179x_info->sel_drive, PCX, pDrive->track, wd179x_info->fdc_head, wd179x_info->fdc_sector, (cCommand & 0x10) ? "Multiple" : "Single");
            wd179x_info->fdc_status |= (WD179X_STAT_DRQ);       /* Set DRQ */
            wd179x_info->drq = 1;
            wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
            wd179x_info->fdc_dataindex = 0;
            wd179x_info->fdc_write = TRUE;
            wd179x_info->fdc_write_track = FALSE;
            wd179x_info->fdc_read = FALSE;
            wd179x_info->fdc_read_addr = FALSE;

            sdata.raw[wd179x_info->fdc_dataindex] = wd179x_info->fdc_data;
            break;
        /* Type III Commands */
        case WD179X_READ_ADDR:
            sim_debug(RD_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=READ_ADDR, T:%d/S:%d, %s\n", wd179x_info->sel_drive,
                      PCX, pDrive->track, wd179x_info->fdc_head, wd179x_info->ddens ? "DD" : "SD");

            /* For some reason 86-DOS tries to use this track, force it to 0.   Need to investigate this more. */
            if (pDrive->track == 0xFF)
                pDrive->track=0;

            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if((wd179x_info->fdc_sec_len == 0xF8) || (wd179x_info->fdc_sec_len > WD179X_MAX_SEC_LEN)) { /* Error calculating N or N too large */
                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                          " Invalid sector size!\n", wd179x_info->sel_drive, PCX);
                wd179x_info->fdc_sec_len = 0;
            }

            if(IMD_MODE_MFM(pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].mode) != (wd179x_info->ddens)) {
                wd179x_info->fdc_status = WD179X_STAT_NOT_FOUND;        /* Sector not found */
                wd179x_info->intrq = 1;
            } else {
                wd179x_info->fdc_status = (WD179X_STAT_DRQ | WD179X_STAT_BUSY);     /* Set DRQ, BUSY */
                wd179x_info->drq = 1;
                wd179x_info->fdc_datacount = 6;
                wd179x_info->fdc_dataindex = 0;
                wd179x_info->fdc_read = TRUE;
                wd179x_info->fdc_read_addr = TRUE;

                sdata.raw[0] = pDrive->track;
                sdata.raw[1] = wd179x_info->fdc_head;
                sdata.raw[2] = wd179x_info->fdc_sector;
                sdata.raw[3] = wd179x_info->fdc_sec_len;
                sdata.raw[4] = 0xAA; /* CRC1 */
                sdata.raw[5] = 0x55; /* CRC2 */

                wd179x_info->fdc_sector = pDrive->track;
                wd179x_info->fdc_status &= ~(WD179X_STAT_BUSY);     /* Clear BUSY */
                wd179x_info->intrq = 1;
            }
            break;
        case WD179X_READ_TRACK:
            sim_debug(RD_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=READ_TRACK\n", wd179x_info->sel_drive, PCX);
            sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " Error: READ_TRACK not implemented.\n", wd179x_info->sel_drive, PCX);
            break;
        case WD179X_WRITE_TRACK:
            sim_debug(WR_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=WRITE_TRACK\n", wd179x_info->sel_drive, PCX);
            sim_debug(FMT_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=WRITE_TRACK, T:%d/S:%d.\n", wd179x_info->sel_drive,
                      PCX, pDrive->track, wd179x_info->fdc_head);
            wd179x_info->fdc_status |= (WD179X_STAT_DRQ);       /* Set DRQ */
            wd179x_info->drq = 1;
            wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
            wd179x_info->fdc_dataindex = 0;
            wd179x_info->fdc_write = FALSE;
            wd179x_info->fdc_write_track = TRUE;
            wd179x_info->fdc_read = FALSE;
            wd179x_info->fdc_read_addr = FALSE;
            wd179x_info->fdc_fmt_state = FMT_GAP1;  /* TRUE when writing an entire track */
            wd179x_info->fdc_fmt_sector_count = 0;

            break;
        /* Type IV Commands */
        case WD179X_FORCE_INTR:
            sim_debug(CMD_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " CMD=FORCE_INTR\n", wd179x_info->sel_drive, PCX);
            if((cCommand & 0x0F) == 0) { /* I0-I3 == 0, no intr, but clear BUSY and terminate command */
                wd179x_info->fdc_status &= ~(WD179X_STAT_DRQ | WD179X_STAT_BUSY);       /* Clear DRQ, BUSY */
                wd179x_info->drq = 0;
                wd179x_info->fdc_write = FALSE;
                wd179x_info->fdc_read = FALSE;
                wd179x_info->fdc_write_track = FALSE;
                wd179x_info->fdc_read_addr = FALSE;
                wd179x_info->fdc_datacount = 0;
                wd179x_info->fdc_dataindex = 0;
            } else {
                if(wd179x_info->fdc_status & WD179X_STAT_BUSY) { /* Force Interrupt when command is pending */
                } else { /* Command not pending, clear status */
                    wd179x_info->fdc_status = 0;
                }

                if(cCommand & 0x04) {
                    wd179x_info->index_pulse_wait = TRUE;
                    if(wd179x_info->sel_drive < WD179X_MAX_DRIVES) {
                        sim_activate (wd179x_unit, ((wd179x_info->drive[wd179x_info->sel_drive].imd->ntracks % 77) == 0) ? CROMFDC_8IN_ROT : CROMFDC_5IN_ROT); /* Generate INDEX pulse */
/*                      sim_printf("Drive %d Num tracks=%d\n", wd179x_info->sel_drive, wd179x_info->drive[wd179x_info->sel_drive].imd->ntracks); */
                    }
                } else {
                    wd179x_info->intrq = 1;
                }
                wd179x_info->fdc_status &= ~(WD179X_STAT_BUSY);     /* Clear BUSY */
            }
            break;
        default:
            sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                      " ERROR: Unknown command 0x%02x.\n\n", wd179x_info->sel_drive, PCX, cCommand);
            break;
    }

    /* Post processing of Type-specific command */
    switch(cCommand & 0xF0) {
        /* Type I Commands */
        case WD179X_RESTORE:
        case WD179X_SEEK:
        case WD179X_STEP:
        case WD179X_STEP_U:
        case WD179X_STEP_IN:
        case WD179X_STEP_IN_U:
        case WD179X_STEP_OUT:
        case WD179X_STEP_OUT_U:
            if(wd179x_info->verify) { /* Verify the selected track/head is ok. */
                sim_debug(SEEK_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                          " Verify ", wd179x_info->sel_drive, PCX);
                if(sectSeek(pDrive->imd, pDrive->track, wd179x_info->fdc_head) != SCPE_OK) {
                    sim_debug(SEEK_MSG, &wd179x_dev, "FAILED\n");
                    wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;
                } else  if(IMD_MODE_MFM(pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].mode) != (wd179x_info->ddens)) {
                    wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;       /* Sector not found */
                    sim_debug(SEEK_MSG, &wd179x_dev, "NOT FOUND\n");
                } else {
                    sim_debug(SEEK_MSG, &wd179x_dev, "Ok\n");
                }
            }

            if(pDrive->track == 0) {
                wd179x_info->fdc_status |= WD179X_STAT_TRACK0;
            } else {
                wd179x_info->fdc_status &= ~(WD179X_STAT_TRACK0);
            }

            wd179x_info->fdc_status &= ~(WD179X_STAT_BUSY);     /* Clear BUSY */
            wd179x_info->intrq = 1;
            break;
        /* Type II Commands */
        case WD179X_READ_REC:
        case WD179X_READ_RECS:
        case WD179X_WRITE_REC:
        case WD179X_WRITE_RECS:
        /* Type III Commands */
        case WD179X_READ_ADDR:
        case WD179X_READ_TRACK:
        case WD179X_WRITE_TRACK:
        /* Type IV Commands */
        case WD179X_FORCE_INTR:
        default:
            break;
    }


    return result;
}

/* Maximum number of sectors per track for format */
uint8 max_sectors_per_track[2][7] = {
    /* 128, 256, 512, 1024, 2048, 4096, 8192 */
    {   26,  15,  8,   4,    2,    1,    0  }, /* Single-density table */
    {   26,  26, 15,   8,    4,    2,    1  }  /* Double-density table */
};

uint8 WD179X_Write(const uint32 Addr, uint8 cData)
{
    WD179X_DRIVE_INFO    *pDrive;
/*    uint8   disk_read = 0; */
    uint32 flags = 0;
    uint32 writelen;

    if(wd179x_info->sel_drive >= WD179X_MAX_DRIVES) {
        return 0xFF;
    }

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    switch(Addr & 0x3) {
        case WD179X_STATUS:
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " WR CMD   = 0x%02x\n", PCX, cData);
            wd179x_info->fdc_read = FALSE;
            wd179x_info->fdc_write = FALSE;
            wd179x_info->fdc_write_track = FALSE;
            wd179x_info->fdc_datacount = 0;
            wd179x_info->fdc_dataindex = 0;

            Do1793Command(cData);
            break;
        case WD179X_TRACK:
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " WR TRACK = 0x%02x\n", PCX, cData);
                pDrive->track = cData;
            break;
        case WD179X_SECTOR:     /* Sector Register */
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " WR SECT  = 0x%02x\n", PCX, cData);
                wd179x_info->fdc_sector = cData;
            break;
        case WD179X_DATA:
            sim_debug(STATUS_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                      " WR DATA  = 0x%02x\n", PCX, cData);
            if(wd179x_info->fdc_write == TRUE) {
                if(wd179x_info->fdc_dataindex < wd179x_info->fdc_datacount) {
                    sdata.raw[wd179x_info->fdc_dataindex] = cData;

                    wd179x_info->fdc_dataindex++;
                    if(wd179x_info->fdc_dataindex == wd179x_info->fdc_datacount) {
                        wd179x_info->fdc_status &= ~(WD179X_STAT_DRQ | WD179X_STAT_BUSY);       /* Clear DRQ, BUSY */
                        wd179x_info->drq = 0;
                        wd179x_info->intrq = 1;

                    sim_debug(WR_DATA_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                              " Writing sector, T:%d/S:%d/N:%d, Len=%d\n", wd179x_info->sel_drive, PCX, pDrive->track, wd179x_info->fdc_head, wd179x_info->fdc_sector, 128 << wd179x_info->fdc_sec_len);

                    sectWrite(pDrive->imd,
                        pDrive->track,
                        wd179x_info->fdc_head,
                        wd179x_info->fdc_sector,
                        sdata.raw,
                        128 << wd179x_info->fdc_sec_len,
                        &flags,
                        &writelen);

                    wd179x_info->fdc_write = FALSE;
                    }
                }
            }

            if(wd179x_info->fdc_write_track == TRUE) {
                    if(wd179x_info->fdc_fmt_state == FMT_GAP1) {
                        if(cData != 0xFC) {
                            wd179x_info->fdc_gap[0]++;
                        } else {
                            sim_debug(VERBOSE_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " FMT GAP1 Length = %d\n", PCX, wd179x_info->fdc_gap[0]);
                            wd179x_info->fdc_gap[1] = 0;
                            wd179x_info->fdc_fmt_state = FMT_GAP2;
                        }
                    } else if(wd179x_info->fdc_fmt_state == FMT_GAP2) {
                        if(cData != 0xFE) {
                            wd179x_info->fdc_gap[1]++;
                        } else {
                            sim_debug(VERBOSE_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " FMT GAP2 Length = %d\n", PCX, wd179x_info->fdc_gap[1]);
                            wd179x_info->fdc_gap[2] = 0;
                            wd179x_info->fdc_fmt_state = FMT_HEADER;
                            wd179x_info->fdc_header_index = 0;
                        }
                    } else if(wd179x_info->fdc_fmt_state == FMT_HEADER) {
                        if(wd179x_info->fdc_header_index == 5) {
                            wd179x_info->fdc_gap[2] = 0;
                            wd179x_info->fdc_fmt_state = FMT_GAP3;
                        } else {
                            sim_debug(VERBOSE_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " HEADER[%d]=%02x\n", PCX, wd179x_info->fdc_header_index, cData);
                            switch(wd179x_info->fdc_header_index) {
                            case 0:
                                pDrive->track = cData;
                                break;
                            case 1:
                                wd179x_info->fdc_head = cData;
                                break;
                            case 2:
                                wd179x_info->fdc_sector = cData;
                                break;
                            case 3:
                                if(cData != 0x00) {
                                }
                                break;
                            case 4:
                                if(cData != 0xF7) {
                                }
                                break;
                            }
                            wd179x_info->fdc_header_index++;
                        }
                    } else if(wd179x_info->fdc_fmt_state == FMT_GAP3) {
                        if(cData != 0xFB) {
                            wd179x_info->fdc_gap[2]++;
                        } else {
                            sim_debug(VERBOSE_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " FMT GAP3 Length = %d\n", PCX, wd179x_info->fdc_gap[2]);
                            wd179x_info->fdc_fmt_state = FMT_DATA;
                            wd179x_info->fdc_dataindex = 0;
                        }
                    } else if(wd179x_info->fdc_fmt_state == FMT_DATA) { /* data bytes */
                        if(cData != 0xF7) {
                            sdata.raw[wd179x_info->fdc_dataindex] = cData;
                            wd179x_info->fdc_dataindex++;
                        } else {
                            wd179x_info->fdc_sec_len = floorlog2(wd179x_info->fdc_dataindex) - 7;
                            if((wd179x_info->fdc_sec_len == 0xF8) || (wd179x_info->fdc_sec_len > WD179X_MAX_SEC_LEN)) { /* Error calculating N or N too large */
                                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X[%d]: " ADDRESS_FORMAT
                                          " Invalid sector size!\n", wd179x_info->sel_drive, PCX);
                                wd179x_info->fdc_sec_len = 0;
                            }
                            if(wd179x_info->fdc_fmt_sector_count >= WD179X_MAX_SECTOR) {
                                sim_debug(ERROR_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                          " Illegal sector count\n", PCX);
                                wd179x_info->fdc_fmt_sector_count = 0;
                            }
                            wd179x_info->fdc_sectormap[wd179x_info->fdc_fmt_sector_count] = wd179x_info->fdc_sector;
                            wd179x_info->fdc_fmt_sector_count++;
                            sim_debug(VERBOSE_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " FMT Data Length = %d\n", PCX, wd179x_info->fdc_dataindex);

                            sim_debug(FMT_MSG, &wd179x_dev, "WD179X: " ADDRESS_FORMAT
                                      " FORMAT T:%d/H:%d/N:%d=%d/L=%d[%d] Fill=0x%02x\n", PCX,
                                      pDrive->track, wd179x_info->fdc_head,
                                      wd179x_info->fdc_fmt_sector_count,
                                      wd179x_info->fdc_sectormap[wd179x_info->fdc_fmt_sector_count],
                                      wd179x_info->fdc_dataindex, wd179x_info->fdc_sec_len, sdata.raw[0]);

                            wd179x_info->fdc_gap[1] = 0;
                            wd179x_info->fdc_fmt_state = FMT_GAP2;

                            if(wd179x_info->fdc_fmt_sector_count == max_sectors_per_track[wd179x_info->ddens & 1][wd179x_info->fdc_sec_len]) {
                                trackWrite(pDrive->imd,
                                           pDrive->track,
                                           wd179x_info->fdc_head,
                                           wd179x_info->fdc_fmt_sector_count,
                                           wd179x_info->fdc_sec_len,
                                           wd179x_info->fdc_sectormap,
                                           wd179x_info->ddens ? 3 : 0, /* data mode */
                                           sdata.raw[0],
                                           &flags);

                                wd179x_info->fdc_status &= ~(WD179X_STAT_BUSY | WD179X_STAT_LOST_DATA);     /* Clear BUSY, LOST_DATA */
                                wd179x_info->drq = 0;
                                wd179x_info->intrq = 1;

                                /* Recalculate disk size */
                                pDrive->uptr->capac = sim_fsize(pDrive->uptr->fileref);
                            }
                        }
                    }
            }

            wd179x_info->fdc_data = cData;
            break;
    }

    return 0;
}

