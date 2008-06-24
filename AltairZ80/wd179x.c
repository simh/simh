/*************************************************************************
 *                                                                       *
 * $Id: wd179x.c 1907 2008-05-21 07:04:17Z hharte $                      *
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
#define DBG_PRINT(args) printf args
#else
#define DBG_PRINT(args)
#endif

#define SEEK_MSG    0x01
#define CMD_MSG     0x04
#define RD_DATA_MSG 0x08
#define WR_DATA_MSG 0x10
#define STATUS_MSG  0x20
#define VERBOSE_MSG 0x80

#define WD179X_MAX_DRIVES    4
#define WD179X_SECTOR_LEN    8192

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
#define WD179X_STAT_REC_TYPE    (1 << 5)
#define WD179X_STAT_NOT_FOUND   (1 << 4)
#define WD179X_STAT_LOST_DATA   (1 << 2)
#define WD179X_STAT_DRQ         (1 << 1)


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
    uint8 fdc_read_addr;    /* TRUE when READ ADDRESS command is in progress */
    uint8 fdc_multiple; /* TRUE for multi-sector read/write */
    uint16 fdc_datacount; /* Read or Write data remaining transfer length */
    uint16 fdc_dataindex; /* index of current byte in sector data */
    uint8 index_pulse_wait; /* TRUE if waiting for interrupt on next index pulse. */
    uint8 fdc_sector;   /* R Record (Sector) */
    uint8 fdc_sec_len;  /* N Sector Length */
    int8 step_dir;
    WD179X_DRIVE_INFO drive[WD179X_MAX_DRIVES];
} WD179X_INFO;

static SECTOR_FORMAT sdata;
extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

t_stat wd179x_svc (UNIT *uptr);

/* These are needed for DMA.  PIO Mode has not been implemented yet. */
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);
static uint8 Do1793Command(uint8 cCommand);

#define UNIT_V_WD179X_WLK        (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_WD179X_WLK          (1 << UNIT_V_WD179X_WLK)
#define UNIT_V_WD179X_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_WD179X_VERBOSE      (1 << UNIT_V_WD179X_VERBOSE)
#define WD179X_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */
#define WD179X_CAPACITY_SSSD     (77*1*26*128)   /* Single-sided Single Density IBM Diskette1 */
#define IMAGE_TYPE_DSK          1               /* Flat binary "DSK" image file.            */
#define IMAGE_TYPE_IMD          2               /* ImageDisk "IMD" image file.              */
#define IMAGE_TYPE_CPT          3               /* CP/M Transfer "CPT" image file.          */

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

static int32 trace_level    = 0xff;        /* Disable all tracing by default. */
static int32 bootstrap      = 0;

static int32 wd179xdev(const int32 port, const int32 io, const int32 data);
static t_stat wd179x_reset(DEVICE *dptr);
int32 find_unit_index (UNIT *uptr);

WD179X_INFO wd179x_info_data = { { 0x0, 0, 0x30, 4 } };
WD179X_INFO *wd179x_info = &wd179x_info_data;

static UNIT wd179x_unit[] = {
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 },
    { UDATA (&wd179x_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, WD179X_CAPACITY), 58200 }
};

static REG wd179x_reg[] = {
    { HRDATA (TRACELEVEL,   trace_level,    16), },
    { DRDATA (BOOTSTRAP,    bootstrap,      10), },
    { NULL }
};

static MTAB wd179x_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",   &set_iobase, &show_iobase, NULL },
    { UNIT_WD179X_WLK,       0,                  "WRTENB",   "WRTENB",   NULL },
    { UNIT_WD179X_WLK,       UNIT_WD179X_WLK,     "WRTLCK",   "WRTLCK",   NULL },
    /* quiet, no warning messages       */
    { UNIT_WD179X_VERBOSE,   0,                  "QUIET",    "QUIET",    NULL },
    /* verbose, show warning messages   */
    { UNIT_WD179X_VERBOSE,   UNIT_WD179X_VERBOSE, "VERBOSE",  "VERBOSE",  NULL },
    { 0 }
};

DEVICE wd179x_dev = {
    "WD179X", wd179x_unit, wd179x_reg, wd179x_mod,
    WD179X_MAX_DRIVES, 10, 31, 1, WD179X_MAX_DRIVES, WD179X_MAX_DRIVES,
    NULL, NULL, &wd179x_reset,
    NULL, &wd179x_attach, &wd179x_detach,
    &wd179x_info_data, (DEV_DISABLE | DEV_DIS), 0,
    NULL, NULL, NULL
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
            printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

extern int32 find_unit_index (UNIT *uptr);

void wd179x_external_restore(void)
{
    WD179X_DRIVE_INFO    *pDrive;
    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        TRACE_PRINT(SEEK_MSG,
            ("WD179X: " ADDRESS_FORMAT " No drive selected, cannot restore." NLP, PCX))
        return;
    }

    TRACE_PRINT(SEEK_MSG,
        ("WD179X[%d]: " ADDRESS_FORMAT " External Restore drive to track 0" NLP, wd179x_info->sel_drive, PCX))

    pDrive->track = 0;

}

/* Attach routine */
t_stat wd179x_attach(UNIT *uptr, char *cptr)
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
        fgets(header, 4, uptr->fileref);
        if(!strcmp(header, "IMD")) {
            uptr->u3 = IMAGE_TYPE_IMD;
        } else if(!strcmp(header, "CPT")) {
            printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            wd179x_detach(uptr);
            return SCPE_OPENERR;
        } else {
            printf("DSK images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_DSK;
            wd179x_detach(uptr);
            return SCPE_OPENERR;
        }
    } else {
        /* creating file, must be DSK format. */
        printf("Cannot create images, must start with a WD179X IMD image.\n");
        uptr->u3 = IMAGE_TYPE_DSK;
        wd179x_detach(uptr);
        return SCPE_OPENERR;
    }

    if (uptr->flags & UNIT_WD179X_VERBOSE)
        printf("WD179X%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < WD179X_CAPACITY_SSSD) { /*was 318000 but changed to allow 8inch SSSD disks*/
            printf("IMD file too small for use with SIMH.\nCopy an existing file and format it with CP/M.\n");
        }

        if (uptr->flags & UNIT_WD179X_VERBOSE)
            printf("--------------------------------------------------------\n");
        wd179x_info->drive[i].imd = diskOpen((uptr->fileref), (uptr->flags & UNIT_WD179X_VERBOSE));
        wd179x_info->drive[i].ready = 1;
        if (uptr->flags & UNIT_WD179X_VERBOSE)
            printf("\n");
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
        return (SCPE_IERR);
    }

    DBG_PRINT(("Detach WD179X%d\n", i));
    diskClose(wd179x_info->drive[i].imd);
    wd179x_info->drive[i].ready = 0;

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
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

static uint8 floorlog2(unsigned int n)
{
    /* Compute log2(n) */
    uint8 r = 0;
    if(n >= 1<<16) { n >>=16; r += 16; }
    if(n >= 1<< 8) { n >>= 8; r +=  8; }
    if(n >= 1<< 4) { n >>= 4; r +=  4; }
    if(n >= 1<< 2) { n >>= 2; r +=  2; }
    if(n >= 1<< 1) {          r +=  1; }
    return ((n == 0) ? (0xFF) : r); /* 0xFF is error return value */
}

uint8 WD179X_Read(const uint32 Addr)
{
    uint8 cData;
    WD179X_DRIVE_INFO    *pDrive;
    unsigned int flags;
    unsigned int readlen;
    int status;

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    cData = 0x00;

    switch(Addr & 0x3) {
        case WD179X_STATUS:
            cData = (pDrive->ready == 0) ? WD179X_STAT_NOT_READY : 0;
            cData |= wd179x_info->fdc_status;   /* Status Register */
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " RD STATUS = 0x%02x" NLP, PCX, cData))
            wd179x_info->intrq = 0;
            break;
        case WD179X_TRACK:
            cData = pDrive->track;
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " RD TRACK = 0x%02x" NLP, PCX, cData))
            break;
        case WD179X_SECTOR:
            cData = wd179x_info->fdc_sector;
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " RD SECT  = 0x%02x" NLP, PCX, cData))
            break;
        case WD179X_DATA:
            cData = 0xFF;      /* Return High-Z data */
            if(wd179x_info->fdc_read == TRUE) {
                if(wd179x_info->fdc_dataindex < wd179x_info->fdc_datacount) {
                    cData = sdata.raw[wd179x_info->fdc_dataindex];
                    if(wd179x_info->fdc_read_addr == TRUE) {
                        TRACE_PRINT(STATUS_MSG,
                            ("WD179X[%d]: " ADDRESS_FORMAT " READ_ADDR[%d] = 0x%02x" NLP, wd179x_info->sel_drive, PCX, wd179x_info->fdc_dataindex, cData))
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
                            if(wd179x_info->fdc_sec_len == 0xF8) { /*Error calculating N*/
                                printf("Invalid sector size!\n");
                            }

                            wd179x_info->fdc_sector ++;
                            TRACE_PRINT(RD_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " MULTI_READ_REC, T:%d/S:%d/N:%d, %s, len=%d" NLP,
                                wd179x_info->sel_drive,
                                PCX,
                                pDrive->track,
                                wd179x_info->fdc_head,
                                wd179x_info->fdc_sector,
                                wd179x_info->ddens ? "DD" : "SD",
                                128 << wd179x_info->fdc_sec_len));

                            status = sectRead(pDrive->imd,
                                pDrive->track,
                                wd179x_info->fdc_head,
                                wd179x_info->fdc_sector,
                                sdata.raw,
                                128 << wd179x_info->fdc_sec_len,
                                &flags,
                                &readlen);

                            if(status != -1) {
                                wd179x_info->fdc_status = (WD179X_STAT_DRQ | WD179X_STAT_BUSY);     /* Set DRQ, BUSY */
                                wd179x_info->drq = 1;
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
    unsigned int flags;
    unsigned int readlen;

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    if(wd179x_info->fdc_status & WD179X_STAT_BUSY) {
        if((cCommand & 0xF0) != WD179X_FORCE_INTR) {
            printf("WD179X[%d]: ERROR: Command 0x%02x ignored because controller is BUSY\n", wd179x_info->sel_drive, cCommand);
        }
        return 0xFF;
    }

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
            wd179x_info->fdc_status |= WD179X_STAT_BUSY;        /* Set BUSY */
            wd179x_info->fdc_status &= ~(WD179X_STAT_CRC_ERROR | WD179X_STAT_SEEK_ERROR | WD179X_STAT_DRQ);
            wd179x_info->intrq = 0;
            wd179x_info->hld = cCommand && 0x08;
            wd179x_info->verify = cCommand & 0x04;
            break;
        /* Type II Commands */
        case WD179X_READ_REC:
        case WD179X_READ_RECS:
        case WD179X_WRITE_REC:
        case WD179X_WRITE_RECS:
            wd179x_info->fdc_status = WD179X_STAT_BUSY;     /* Set BUSY, clear all others */
            wd179x_info->intrq = 0;
            wd179x_info->hld = 1;   /* Load the head immediately, E Flag not checked. */
            break;
        /* Type III Commands */
        case WD179X_READ_ADDR:
        case WD179X_READ_TRACK:
        case WD179X_WRITE_TRACK:
        /* Type IV Commands */
        case WD179X_FORCE_INTR:
        default:
            break;
    }

    switch(cCommand & 0xF0) {
        /* Type I Commands */
        case WD179X_RESTORE:
            TRACE_PRINT(CMD_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=RESTORE" NLP, wd179x_info->sel_drive, PCX));
            pDrive->track = 0;
            wd179x_info->intrq = 1;
            break;
        case WD179X_SEEK:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=SEEK, track=%d, new=%d" NLP, wd179x_info->sel_drive, PCX, pDrive->track, wd179x_info->fdc_data));
            pDrive->track = wd179x_info->fdc_data;
            break;
        case WD179X_STEP:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP" NLP, wd179x_info->sel_drive, PCX));
            break;
        case WD179X_STEP_U:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP_U dir=%d" NLP, wd179x_info->sel_drive, PCX, wd179x_info->step_dir));
            if(wd179x_info->step_dir == 1) {
                if(pDrive->track < 255) pDrive->track++;
            } else if (wd179x_info->step_dir == -1) {
                if(pDrive->track > 0) pDrive->track--;
            } else {
                printf("WD179X[%d]: Error, undefined direction for STEP\n", wd179x_info->sel_drive);
            }
            break;
        case WD179X_STEP_IN:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP_IN" NLP, wd179x_info->sel_drive, PCX));
            break;
        case WD179X_STEP_IN_U:
            if(pDrive->track < 255) pDrive->track++;
            wd179x_info->step_dir = 1;
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP_IN_U, Track=%d" NLP,
                wd179x_info->sel_drive, PCX, pDrive->track));
            break;
        case WD179X_STEP_OUT:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP_OUT" NLP,
                wd179x_info->sel_drive, PCX));
            break;
        case WD179X_STEP_OUT_U:
            TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=STEP_OUT_U" NLP,
                wd179x_info->sel_drive, PCX));
            if(pDrive->track > 0) pDrive->track--;
            wd179x_info->step_dir = -1;
            break;
        /* Type II Commands */
        case WD179X_READ_REC:
        case WD179X_READ_RECS:
            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if(wd179x_info->fdc_sec_len == 0xF8) { /*Error calculating N*/
                printf("Invalid sector size!\n");
            }

            wd179x_info->fdc_multiple = (cCommand & 0x10) ? TRUE : FALSE;
            TRACE_PRINT(RD_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=READ_REC, T:%d/S:%d/N:%d, %s, %s len=%d" NLP,
                wd179x_info->sel_drive,
                PCX, pDrive->track,
                wd179x_info->fdc_head,
                wd179x_info->fdc_sector,
                wd179x_info->fdc_multiple ? "Multiple" : "Single",
                wd179x_info->ddens ? "DD" : "SD",
                128 << wd179x_info->fdc_sec_len));

            sectRead(pDrive->imd,
                pDrive->track,
                wd179x_info->fdc_head,
                wd179x_info->fdc_sector,
                sdata.raw,
                128 << wd179x_info->fdc_sec_len,
                &flags,
                &readlen);

            if(IMD_MODE_MFM(pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].mode) != (wd179x_info->ddens)) {
                printf("Sector not found\n");
                wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;       /* Sector not found */
                wd179x_info->intrq = 1;
            } else {
                wd179x_info->fdc_status |= (WD179X_STAT_DRQ);       /* Set DRQ */
                wd179x_info->drq = 1;
                wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
                wd179x_info->fdc_dataindex = 0;
                wd179x_info->fdc_write = FALSE;
                wd179x_info->fdc_read = TRUE;
                wd179x_info->fdc_read_addr = FALSE;
            }
            break;
        case WD179X_WRITE_RECS:
            printf("-->> Error: WRITE_RECS not implemented." NLP);
        case WD179X_WRITE_REC:
            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if(wd179x_info->fdc_sec_len == 0xF8) { /*Error calculating N*/
                printf("Invalid sector size!\n");
            }

            TRACE_PRINT(WR_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=WRITE_REC, T:%d/S:%d/N:%d, %s." NLP,
                wd179x_info->sel_drive,
                PCX,
                pDrive->track,
                cCommand&&0x08,
                wd179x_info->fdc_sector,
                (cCommand & 0x10) ? "Multiple" : "Single"));
            wd179x_info->fdc_status |= (WD179X_STAT_DRQ);       /* Set DRQ */
            wd179x_info->drq = 1;
            wd179x_info->fdc_datacount = 128 << wd179x_info->fdc_sec_len;
            wd179x_info->fdc_dataindex = 0;
            wd179x_info->fdc_write = TRUE;
            wd179x_info->fdc_read = FALSE;
            wd179x_info->fdc_read_addr = FALSE;

            sdata.raw[wd179x_info->fdc_dataindex] = wd179x_info->fdc_data;
            break;
        /* Type III Commands */
        case WD179X_READ_ADDR:
            TRACE_PRINT(RD_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=READ_ADDR, T:%d/S:%d, %s" NLP,
                wd179x_info->sel_drive,
                PCX,
                pDrive->track,
                wd179x_info->fdc_head,
                wd179x_info->ddens ? "DD" : "SD"));

            /* Compute Sector Size */
            wd179x_info->fdc_sec_len = floorlog2(
                pDrive->imd->track[pDrive->track][wd179x_info->fdc_head].sectsize) - 7;
            if(wd179x_info->fdc_sec_len == 0xF8) { /*Error calculating N*/
                printf("Invalid sector size!\n");
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
            TRACE_PRINT(RD_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=READ_TRACK" NLP, wd179x_info->sel_drive, PCX));
            printf("-->> Error: READ_TRACK not implemented." NLP);
            break;
        case WD179X_WRITE_TRACK:
            TRACE_PRINT(WR_DATA_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=WRITE_TRACK" NLP, wd179x_info->sel_drive, PCX));
            printf("-->> Error: WRITE_TRACK not implemented." NLP);
            break;
        /* Type IV Commands */
        case WD179X_FORCE_INTR:
            TRACE_PRINT(CMD_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " CMD=FORCE_INTR" NLP, wd179x_info->sel_drive, PCX));
            if((cCommand & 0x0F) == 0) { /* I0-I3 == 0, no intr, but clear BUSY and terminate command */
                wd179x_info->fdc_status &= ~(WD179X_STAT_DRQ | WD179X_STAT_BUSY);       /* Clear DRQ, BUSY */
                wd179x_info->drq = 0;
                wd179x_info->fdc_write = FALSE;
                wd179x_info->fdc_read = FALSE;
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
                    sim_activate (wd179x_unit, wd179x_info->drivetype == 8 ? 48500 : 58200); /* Generate INDEX pulse */
                } else {
                    wd179x_info->intrq = 1;
                }
                wd179x_info->fdc_status &= ~(WD179X_STAT_BUSY);     /* Clear BUSY */
            }
            break;
        default:
            printf("WD179X[%d]: Unknown WD179X command 0x%02x.\n", wd179x_info->sel_drive, cCommand);
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
                TRACE_PRINT(SEEK_MSG, ("WD179X[%d]: " ADDRESS_FORMAT " Verify ", wd179x_info->sel_drive, PCX));
                if(sectSeek(pDrive->imd, pDrive->track, wd179x_info->fdc_head) != 0) {
                    TRACE_PRINT(SEEK_MSG, ("FAILED" NLP));
                    wd179x_info->fdc_status |= WD179X_STAT_NOT_FOUND;
                } else {
                    TRACE_PRINT(SEEK_MSG, ("Ok" NLP));
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

uint8 WD179X_Write(const uint32 Addr, uint8 cData)
{
    WD179X_DRIVE_INFO    *pDrive;
    unsigned int flags;
    unsigned int writelen;

    pDrive = &wd179x_info->drive[wd179x_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    switch(Addr & 0x3) {
        case WD179X_STATUS:
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " WR CMD   = 0x%02x" NLP, PCX, cData))
            wd179x_info->fdc_read = FALSE;
            wd179x_info->fdc_write = FALSE;
            wd179x_info->fdc_datacount = 0;
            wd179x_info->fdc_dataindex = 0;

            Do1793Command(cData);
            break;
        case WD179X_TRACK:
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " WR TRACK = 0x%02x" NLP, PCX, cData))
                pDrive->track = cData;
            break;
        case WD179X_SECTOR:     /* Sector Register */
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " WR SECT  = 0x%02x" NLP, PCX, cData))
                wd179x_info->fdc_sector = cData;
            break;
        case WD179X_DATA:
            TRACE_PRINT(STATUS_MSG,
                ("WD179X: " ADDRESS_FORMAT " WR DATA  = 0x%02x" NLP, PCX, cData))
            if(wd179x_info->fdc_write == TRUE) {
                if(wd179x_info->fdc_dataindex < wd179x_info->fdc_datacount) {
                    sdata.raw[wd179x_info->fdc_dataindex] = cData;

                    wd179x_info->fdc_dataindex++;
                    if(wd179x_info->fdc_dataindex == wd179x_info->fdc_datacount) {
                        wd179x_info->fdc_status &= ~(WD179X_STAT_DRQ | WD179X_STAT_BUSY);       /* Clear DRQ, BUSY */
                        wd179x_info->drq = 0;
                        wd179x_info->intrq = 1;

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
            wd179x_info->fdc_data = cData;
            break;
    }

    return 0;
}

