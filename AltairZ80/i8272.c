/*************************************************************************
 *                                                                       *
 * $Id: i8272.c 1773 2008-01-11 05:46:19Z hharte $                       *
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
 *     Generic Intel 8272 Disk Controller module for SIMH.               *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/* Change log:
    - 19-Apr-2008, Tony Nicholson, added other .IMD formats
*/

/*#define DBG_MSG */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#include "sim_imd.h"
#include "i8272.h"

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

#define I8272_MAX_DRIVES    4
#define I8272_SECTOR_LEN    8192

#define CMD_PHASE 0
#define EXEC_PHASE 1
#define DATA_PHASE 2

typedef union {
    uint8 raw[I8272_SECTOR_LEN];
} SECTOR_FORMAT;

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint8 ntracks;   /* number of tracks */
    uint8 nheads;    /* number of heads */
    uint32 sectsize; /* sector size, not including pre/postamble */
    uint8 track;     /* Current Track */
    uint8 ready;     /* Is drive ready? */
} I8272_DRIVE_INFO;

typedef struct {
    PNP_INFO pnp;       /* Plug-n-Play Information */
    uint32 fdc_dma_addr;/* DMA Transfer Address */
    uint8 fdc_msr;      /* 8272 Main Status Register */
    uint8 fdc_phase;    /* Phase that the 8272 is currently in */
    uint8 fdc_srt;      /* Step Rate in ms */
    uint8 fdc_hut;      /* Head Unload Time in ms */
    uint8 fdc_hlt;      /* Head Load Time in ms */
    uint8 fdc_nd;       /* Non-DMA Mode 1=Non-DMA, 0=DMA */
    uint8 fdc_head;     /* H Head Number */
    uint8 fdc_sector;   /* R Record (Sector) */
    uint8 fdc_sec_len;  /* N Sector Length */
    uint8 fdc_eot;      /* EOT End of Track (Final sector number of cyl) */
    uint8 fdc_gpl;      /* GPL Gap3 Length */
    uint8 fdc_dtl;      /* DTL Data Length */
    uint8 fdc_mt;       /* Multiple sectors */
    uint8 fdc_mfm;      /* MFM mode */
    uint8 fdc_sk;       /* Skip Deleted Data */
    uint8 fdc_hds;      /* Head Select */
    uint8 fdc_fillbyte; /* Fill-byte used for FORMAT TRACK */
    uint8 fdc_sc;       /* Sector count for FORMAT TRACK */
    uint8 fdc_status[3];/* Status Register Bytes */
    uint8 fdc_seek_end; /* Seek was executed successfully */
    uint8 cmd_index;    /* Index of command byte */
    uint8 cmd[10];      /* Storage for current command */
    uint8 cmd_len;      /* FDC Command Length */
    uint8 result_index; /* Index of result byte */
    uint8 result[10];   /* Result data */
    uint8 result_len;   /* FDC Result Length */
    uint8 sel_drive;    /* Currently selected drive */
    I8272_DRIVE_INFO drive[I8272_MAX_DRIVES];
} I8272_INFO;

static SECTOR_FORMAT sdata;
extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

/* These are needed for DMA.  PIO Mode has not been implemented yet. */
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);

#define UNIT_V_I8272_WLK        (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_I8272_WLK          (1 << UNIT_V_I8272_WLK)
#define UNIT_V_I8272_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_I8272_VERBOSE      (1 << UNIT_V_I8272_VERBOSE)
#define I8272_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */
#define I8272_CAPACITY_SSSD     (77*1*26*128)   /* Single-sided Single Density IBM Diskette1 */
#define IMAGE_TYPE_DSK          1               /* Flat binary "DSK" image file.            */
#define IMAGE_TYPE_IMD          2               /* ImageDisk "IMD" image file.              */
#define IMAGE_TYPE_CPT          3               /* CP/M Transfer "CPT" image file.          */

/* Intel 8272 Commands */
#define I8272_READ_TRACK            0x02
#define I8272_SPECIFY               0x03
#define I8272_SENSE_DRIVE_STATUS    0x04
#define I8272_WRITE_DATA            0x05
#define I8272_READ_DATA             0x06
#define I8272_RECALIBRATE           0x07
#define I8272_SENSE_INTR_STATUS     0x08
#define I8272_WRITE_DELETED_DATA    0x09
#define I8272_READ_ID               0x0A
#define I8272_READ_DELETED_DATA     0x0C
#define I8272_FORMAT_TRACK          0x0D
#define I8272_SEEK                  0x0F
#define I8272_SCAN_EQUAL            0x11
#define I8272_SCAN_LOW_EQUAL        0x19
#define I8272_SCAN_HIGH_EQUAL       0x1D

/* SENSE DRIVE STATUS bit definitions */
#define DRIVE_STATUS_TWO_SIDED  0x08
#define DRIVE_STATUS_TRACK0     0x10
#define DRIVE_STATUS_READY      0x20
#define DRIVE_STATUS_WP         0x40
#define DRIVE_STATUS_FAULT      0x80

static int32 trace_level    = 0;        /* Disable all tracing by default. */
static int32 bootstrap      = 0;

static int32 i8272dev(const int32 port, const int32 io, const int32 data);
static t_stat i8272_reset(DEVICE *dptr);
int32 find_unit_index (UNIT *uptr);

I8272_INFO i8272_info_data = { { 0x0, 0, 0xC0, 2 } };
I8272_INFO *i8272_info = &i8272_info_data;

static UNIT i8272_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) }
};

static REG i8272_reg[] = {
    { HRDATA (TRACELEVEL,   trace_level,    16), },
    { DRDATA (BOOTSTRAP,    bootstrap,      10), },
    { NULL }
};

static MTAB i8272_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",   &set_iobase, &show_iobase, NULL },
    { UNIT_I8272_WLK,       0,                  "WRTENB",   "WRTENB",   NULL },
    { UNIT_I8272_WLK,       UNIT_I8272_WLK,     "WRTLCK",   "WRTLCK",   NULL },
    /* quiet, no warning messages       */
    { UNIT_I8272_VERBOSE,   0,                  "QUIET",    "QUIET",    NULL },
    /* verbose, show warning messages   */
    { UNIT_I8272_VERBOSE,   UNIT_I8272_VERBOSE, "VERBOSE",  "VERBOSE",  NULL },
    { 0 }
};

DEVICE i8272_dev = {
    "I8272", i8272_unit, i8272_reg, i8272_mod,
    I8272_MAX_DRIVES, 10, 31, 1, I8272_MAX_DRIVES, I8272_MAX_DRIVES,
    NULL, NULL, &i8272_reset,
    NULL, &i8272_attach, &i8272_detach,
    &i8272_info_data, (DEV_DISABLE | DEV_DIS), 0,
    NULL, NULL, NULL
};

static uint8 I8272_Setup_Cmd(uint8 fdc_cmd);


/* Reset routine */
static t_stat i8272_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &i8272dev, TRUE);
    } else {
        /* Connect I/O Ports at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &i8272dev, FALSE) != 0) {
            printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}


/* find_unit_index   find index of a unit

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result  =       index of device
*/
int32 find_unit_index (UNIT *uptr)
{
    DEVICE *dptr;
    uint32 i;

    if (uptr == NULL) return (-1);
    dptr = find_dev_from_unit(uptr);
    for(i=0; i<dptr->numunits; i++) {
        if(dptr->units + i == uptr) {
            break;
        }
    }
    if(i == dptr->numunits) {
        return (-1);
    }
    return (i);
}

/* Attach routine */
t_stat i8272_attach(UNIT *uptr, char *cptr)
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

    DBG_PRINT(("Attach I8272%d\n", i));
    i8272_info->drive[i].uptr = uptr;

    /* Default to drive not ready */
    i8272_info->drive[i].ready = 0;

    if(uptr->capac > 0) {
        fgets(header, 4, uptr->fileref);
        if(!strcmp(header, "IMD")) {
            uptr->u3 = IMAGE_TYPE_IMD;
        } else if(!strcmp(header, "CPT")) {
            printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            i8272_detach(uptr);
            return SCPE_OPENERR;
        } else {
            printf("DSK images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_DSK;
            i8272_detach(uptr);
            return SCPE_OPENERR;
        }
    } else {
        /* creating file, must be DSK format. */
        printf("Cannot create images, must start with a I8272 IMD image.\n");
        uptr->u3 = IMAGE_TYPE_DSK;
        i8272_detach(uptr);
        return SCPE_OPENERR;
    }

    if (uptr->flags & UNIT_I8272_VERBOSE)
        printf("I8272%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < I8272_CAPACITY_SSSD) { /*was 318000 but changed to allow 8inch SSSD disks*/
            printf("IMD file too small for use with SIMH.\nCopy an existing file and format it with CP/M.\n");
            i8272_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_I8272_VERBOSE)
            printf("--------------------------------------------------------\n");
        i8272_info->drive[i].imd = diskOpen((uptr->fileref), (uptr->flags & UNIT_I8272_VERBOSE));
        i8272_info->drive[i].ready = 1;
        if (uptr->flags & UNIT_I8272_VERBOSE)
            printf("\n");
    } else {
        i8272_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
t_stat i8272_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    DBG_PRINT(("Detach I8272%d\n", i));
    diskClose(i8272_info->drive[i].imd);
    i8272_info->drive[i].ready = 0;

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static int32 i8272dev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("I8272: " ADDRESS_FORMAT " %s, Port 0x%02x Data 0x%02x" NLP,
        PCX, io ? "OUT" : " IN", port, data));
    if(io) {
        I8272_Write(port, data);
        return 0;
    } else {
        return(I8272_Read(port));
    }
}

uint8 I8272_Set_DMA(const uint32 dma_addr)
{
    i8272_info->fdc_dma_addr = dma_addr & 0xFFFFFF;

    return 0;
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

uint8 I8272_Read(const uint32 Addr)
{
    uint8 cData;
    I8272_DRIVE_INFO    *pDrive;

    pDrive = &i8272_info->drive[i8272_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    cData = 0x00;

    switch(Addr & 0x3) {
        case I8272_FDC_MSR:
            cData  = i8272_info->fdc_msr | 0x80;
            if(i8272_info->fdc_phase == 0) {
                cData &= ~0x40;
            } else {
                cData |= 0x40;
            }

            TRACE_PRINT(STATUS_MSG, ("I8272: " ADDRESS_FORMAT " RD FDC MSR = 0x%02x" NLP, PCX, cData));
            break;
        case I8272_FDC_DATA:
            if(i8272_info->fdc_phase == DATA_PHASE) {
                cData = i8272_info->result[i8272_info->result_index];
                i8272_info->result_index ++;
                if(i8272_info->result_index == i8272_info->result_len) {
                    TRACE_PRINT(VERBOSE_MSG, ("I8272: " ADDRESS_FORMAT " result phase complete." NLP, PCX));
                    i8272_info->fdc_phase = 0;
                }
            }

            TRACE_PRINT(VERBOSE_MSG, ("I8272: " ADDRESS_FORMAT " RD Data, phase=%d, [%d]=0x%02x" NLP, PCX, i8272_info->fdc_phase, i8272_info->result_index-1, cData));

            break;
        default:
            TRACE_PRINT(VERBOSE_MSG, ("I8272: " ADDRESS_FORMAT " Cannot read register %x" NLP, PCX, Addr));
            cData = 0xFF;
    }

    return (cData);
}

static char *messages[0x20] = {
/*  0                           1                       2                       3                   */
    "Undefined Command 0x0","Undefined Command 0x1","Read Track",           "Specify",
/*  4                           5                       6                       7                   */
    "Sense Drive Status",   "Write Data",           "Read Data",            "Recalibrate",
/*  8                           9                       A                       B                   */
    "Sense Interrupt Status", "Write Deleted Data", "Read ID",              "Undefined Command 0xB",
/*  C                           D                       E                       F                   */
    "Read Deleted Data",    "Format Track",         "Undefined Command 0xE","Seek",
/*  10                          11                      12                      13                  */
    "Undefined Command 0x10","Scan Equal",          "Undefined Command 0x12","Undefined Command 0x13",
/*  14                          15                      16                      17                  */
    "Undefined Command 0x14","Undefined Command 0x15","Undefined Command 0x16","Undefined Command 0x17",
/*  18                          19                      1A                      1B                  */
    "Undefined Command 0x18","Scan Low Equal",      "Undefined Command 0x1A","Undefined Command 0x1B",
/*  1C                          1D                      1E                      1F                  */
    "Undefined Command 0x1C","Scan High Equal",     "Undefined Command 0x1E","Undefined Command 0x1F"
};

uint8 I8272_Write(const uint32 Addr, uint8 cData)
{
    I8272_DRIVE_INFO    *pDrive;
    unsigned int flags;
    unsigned int readlen;
    uint8   disk_read = 0;
    int32 i;

    pDrive = &i8272_info->drive[i8272_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    switch(Addr & 0x3) {
        case I8272_FDC_MSR:
            TRACE_PRINT(WR_DATA_MSG, ("I8272: " ADDRESS_FORMAT " WR Drive Select Reg=%02x" NLP,
                PCX, cData));
            break;
        case I8272_FDC_DATA:
            TRACE_PRINT(VERBOSE_MSG, ("I8272: " ADDRESS_FORMAT " WR Data, phase=%d, index=%d" NLP,
                PCX, i8272_info->fdc_phase, i8272_info->cmd_index));
            if(i8272_info->fdc_phase == CMD_PHASE) {
                i8272_info->cmd[i8272_info->cmd_index] = cData;

                if(i8272_info->cmd_index == 0) {
                    TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " CMD=0x%02x[%s]" NLP,
                        PCX, cData & 0x1F, messages[cData & 0x1F]));
                    I8272_Setup_Cmd(cData & 0x1F);
                }
                i8272_info->cmd_index ++;

                if(i8272_info->cmd_len == i8272_info->cmd_index) {
                    i8272_info->cmd_index = 0;
                    i8272_info->fdc_phase = EXEC_PHASE;
                }
            }

            if(i8272_info->fdc_phase == EXEC_PHASE) {
                switch(i8272_info->cmd[0] & 0x1F) {
                    case I8272_READ_DATA:
                    case I8272_WRITE_DATA:
                    case I8272_READ_DELETED_DATA:
                    case I8272_WRITE_DELETED_DATA:
                    case I8272_READ_TRACK:
                    case I8272_SCAN_LOW_EQUAL:
                    case I8272_SCAN_HIGH_EQUAL:
                    case I8272_SCAN_EQUAL:
                        i8272_info->fdc_mt = (i8272_info->cmd[0] & 0x80) >> 7;
                        i8272_info->fdc_mfm = (i8272_info->cmd[0] & 0x40) >> 6;
                        i8272_info->fdc_sk = (i8272_info->cmd[0] & 0x20) >> 5;
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->sel_drive = (i8272_info->cmd[1] & 0x03);
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        if(pDrive->track != i8272_info->cmd[2]) {
                            i8272_info->fdc_seek_end = 1;
                        } else {
                            i8272_info->fdc_seek_end = 0;
                        }
                        pDrive->track = i8272_info->cmd[2];
                        i8272_info->fdc_head = i8272_info->cmd[3];
                        i8272_info->fdc_sector = i8272_info->cmd[4];
                        i8272_info->fdc_sec_len = i8272_info->cmd[5];
                        i8272_info->fdc_eot = i8272_info->cmd[6];
                        i8272_info->fdc_gpl = i8272_info->cmd[7];
                        i8272_info->fdc_dtl = i8272_info->cmd[8];

                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT
                            " CMD=0x%02x[%s]: Drive: %d, %s %s, C=%d. H=%d. S=%d, N=%d, EOT=%02x, GPL=%02x, DTL=%02x" NLP,
                            PCX,
                            i8272_info->cmd[0] & 0x1F,
                            messages[i8272_info->cmd[0] & 0x1F],
                            i8272_info->sel_drive,
                            i8272_info->fdc_mt ? "Multi" : "Single",
                            i8272_info->fdc_mfm ? "MFM" : "FM",
                            pDrive->track,
                            i8272_info->fdc_head,
                            i8272_info->fdc_sector,
                            i8272_info->fdc_sec_len,
                            i8272_info->fdc_eot,
                            i8272_info->fdc_gpl,
                            i8272_info->fdc_dtl));

                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 3);
                        i8272_info->fdc_status[0] |= 0x40;

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->track;
                        i8272_info->result[4] = i8272_info->fdc_head;
                        i8272_info->result[5] = i8272_info->fdc_sector;
                        i8272_info->result[6] = i8272_info->fdc_sec_len;
                        break;
                    case I8272_READ_ID: /* READ ID */
                        i8272_info->fdc_mfm = (i8272_info->cmd[0] & 0x40) >> 6;
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->sel_drive = (i8272_info->cmd[1] & 0x03);
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }
                        /* Compute the i8272 "N" value from the sectorsize of this */
                        /* disk's current track - i.e. N = log2(sectsize) - log2(128) */
                        /* The calculation also works for non-standard format disk images with */
                        /* sectorsizes of 2048, 4096 and 8192 bytes */
                        i8272_info->fdc_sec_len = floorlog2(
                            pDrive->imd->track[pDrive->track][i8272_info->fdc_head].sectsize) - 7;
                        if(i8272_info->fdc_sec_len == 0xF8) { /*Error calculating N*/
                            return 0xFF;
                        }
                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 3);

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->track;
                        i8272_info->result[4] = i8272_info->fdc_head;
                        i8272_info->result[5] = i8272_info->fdc_sector;
                        i8272_info->result[6] = i8272_info->fdc_sec_len; /*was hardcoded to 0x3*/
                        break;
                    case I8272_RECALIBRATE: /* RECALIBRATE */
                        i8272_info->sel_drive = i8272_info->cmd[1] & 3;
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        pDrive->track = 0;
                        i8272_info->fdc_phase = 0;  /* No result phase */
                        pDrive->track = 0;
                        i8272_info->fdc_seek_end = 1;
                        TRACE_PRINT(SEEK_MSG, ("I8272: " ADDRESS_FORMAT " Recalibrate: Drive 0x%02x" NLP,
                            PCX, i8272_info->sel_drive));
                        break;
                    case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
                        i8272_info->fdc_mfm = (i8272_info->cmd[0] & 0x40) >> 6;
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->fdc_head = i8272_info->fdc_hds; /* psco added */
                        i8272_info->sel_drive = (i8272_info->cmd[1] & 0x03);
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        if(pDrive->track != i8272_info->cmd[2]) {
                            i8272_info->fdc_seek_end = 1;
                        } else {
                            i8272_info->fdc_seek_end = 0;
                        }
                        i8272_info->fdc_sec_len = i8272_info->cmd[2];
                        i8272_info->fdc_sc = i8272_info->cmd[3];
                        i8272_info->fdc_gpl = i8272_info->cmd[4];
                        i8272_info->fdc_fillbyte = i8272_info->cmd[5];

                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Format Drive: %d, %s, C=%d. H=%d. N=%d, SC=%d, GPL=%02x, FILL=%02x" NLP,
                                PCX,
                                i8272_info->sel_drive,
                                i8272_info->fdc_mfm ? "MFM" : "FM",
                                pDrive->track,
                                i8272_info->fdc_head,
                                i8272_info->fdc_sec_len,
                                i8272_info->fdc_sc,
                                i8272_info->fdc_gpl,
                                i8272_info->fdc_fillbyte));

                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 3);
                        /*i8272_info->fdc_status[0] |= 0x40; psco removed */

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->track;
                        i8272_info->result[4] = i8272_info->fdc_head;
                        i8272_info->result[5] = i8272_info->fdc_sector;
                        i8272_info->result[6] = i8272_info->fdc_sec_len;
                        break;
                    case I8272_SENSE_INTR_STATUS:   /* SENSE INTERRUPT STATUS */
                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Sense Interrupt Status" NLP, PCX));
                        i8272_info->result[0]  = i8272_info->fdc_seek_end ? 0x20 : 0x00;  /* SEEK_END */
                        i8272_info->result[0] |= i8272_info->sel_drive;
                        i8272_info->result[1]  = pDrive->track;
                        break;
                    case I8272_SPECIFY: /* SPECIFY */
                        i8272_info->fdc_srt = 16 - ((i8272_info->cmd[1] & 0xF0) >> 4);
                        i8272_info->fdc_hut = (i8272_info->cmd[1] & 0x0F) * 16;
                        i8272_info->fdc_hlt = ((i8272_info->cmd[2] & 0xFE) >> 1) * 2;
                        i8272_info->fdc_nd  = (i8272_info->cmd[2] & 0x01);
                        i8272_info->fdc_phase = 0;  /* No result phase */
                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Specify: SRT=%d, HUT=%d, HLT=%d, ND=%s" NLP,
                            PCX,
                            i8272_info->fdc_srt,
                            i8272_info->fdc_hut,
                            i8272_info->fdc_hlt,
                            i8272_info->fdc_nd ? "NON-DMA" : "DMA"));
                        break;
                    case I8272_SENSE_DRIVE_STATUS:  /* Setup Status3 Byte */
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->sel_drive = (i8272_info->cmd[1] & 0x03);
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        i8272_info->result[0]  = (pDrive->ready) ? DRIVE_STATUS_READY : 0; /* Drive Ready */
                        if(imdGetSides(pDrive->imd) == 2) {
                            i8272_info->result[0] |= DRIVE_STATUS_TWO_SIDED;    /* Two-sided? */
                        }
                        if(imdIsWriteLocked(pDrive->imd)) {
                            i8272_info->result[0] |= DRIVE_STATUS_WP;   /* Write Protected? */
                        }
                        i8272_info->result[0] |= (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->result[0] |= (i8272_info->sel_drive & 3);
                        i8272_info->result[0] |= (pDrive->track == 0) ? DRIVE_STATUS_TRACK0 : 0x00; /* Track 0 */
                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Sense Drive Status = %02x" NLP,
                            PCX, i8272_info->result[0]));
                        break;
                    case I8272_SEEK:    /* SEEK */
                        i8272_info->fdc_mt = (i8272_info->cmd[0] & 0x80) >> 7;
                        i8272_info->fdc_mfm = (i8272_info->cmd[0] & 0x40) >> 6;
                        i8272_info->fdc_sk = (i8272_info->cmd[0] & 0x20) >> 5;
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->sel_drive = (i8272_info->cmd[1] & 0x03);
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        pDrive->track = i8272_info->cmd[2];
                        i8272_info->fdc_seek_end = 1;
                        TRACE_PRINT(SEEK_MSG, ("I8272: " ADDRESS_FORMAT " Seek %d" NLP,
                            PCX, pDrive->track));
                        break;
                    default:    /* INVALID */
                        break;
                }

            if(i8272_info->fdc_phase == EXEC_PHASE) {
                switch(i8272_info->cmd[0] & 0x1F) {
                    case I8272_READ_TRACK:
                        printf("I8272: " ADDRESS_FORMAT " Read a track (untested.)" NLP, PCX);
                        i8272_info->fdc_sector = 1; /* Read entire track from sector 1...eot */
                    case I8272_READ_DATA:
                    case I8272_READ_DELETED_DATA:
                        disk_read = 1;
                    case I8272_WRITE_DATA:
                    case I8272_WRITE_DELETED_DATA:
                        for(;i8272_info->fdc_sector<=i8272_info->fdc_eot;i8272_info->fdc_sector++) {
                            TRACE_PRINT(RD_DATA_MSG, ("I8272: " ADDRESS_FORMAT " %s Data, sector: %d sector len=%d" NLP,
                                PCX, disk_read ? "RD" : "WR",
                                i8272_info->fdc_sector,
                                128 << i8272_info->fdc_sec_len));
                            switch((pDrive->uptr)->u3)
                            {
                                case IMAGE_TYPE_IMD:
                                    if(pDrive->imd == NULL) {
                                        printf(".imd is NULL!" NLP);
                                    }
                                    if(disk_read) { /* Read sector */
                                        sectRead(pDrive->imd,
                                            pDrive->track,
                                            i8272_info->fdc_head,
                                            i8272_info->fdc_sector,
                                            sdata.raw,
                                            128 << i8272_info->fdc_sec_len,
                                            &flags,
                                            &readlen);

                                        for(i=0;i<(128 << i8272_info->fdc_sec_len);i++) {
                                            PutBYTEWrapper(i8272_info->fdc_dma_addr, sdata.raw[i]);
                                            i8272_info->fdc_dma_addr++;
                                        }
                                        TRACE_PRINT(RD_DATA_MSG, ("I8272: " ADDRESS_FORMAT " Data transferred to RAM at 0x%06x" NLP,
                                            PCX, i8272_info->fdc_dma_addr));
                                    } else { /* Write */
                                        for(i=0;i<(128 << i8272_info->fdc_sec_len);i++) {
                                            sdata.raw[i] = GetBYTEWrapper(i8272_info->fdc_dma_addr);
                                            i8272_info->fdc_dma_addr++;
                                        }
                                        TRACE_PRINT(WR_DATA_MSG, ("I8272: " ADDRESS_FORMAT " Data transferred from RAM at 0x%06x" NLP,
                                            PCX, i8272_info->fdc_dma_addr));
                                        sectWrite(pDrive->imd,
                                            pDrive->track,
                                            i8272_info->fdc_head,
                                            i8272_info->fdc_sector,
                                            sdata.raw,
                                            128 << i8272_info->fdc_sec_len,
                                            &flags,
                                            &readlen);
                                    }

                                    i8272_info->result[5] = i8272_info->fdc_sector;
                                    i8272_info->result[1] = 0x80;
                                    break;
                                case IMAGE_TYPE_DSK:
                                    printf("%s: DSK Format not supported" NLP, __FUNCTION__);
                                    break;
                                case IMAGE_TYPE_CPT:
                                    printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                                    break;
                                default:
                                    printf("%s: Unknown image Format" NLP, __FUNCTION__);
                                    break;
                            }
                        }
                        break;
                    case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
                        for(i8272_info->fdc_sector = 1;i8272_info->fdc_sector<=i8272_info->fdc_sc;i8272_info->fdc_sector++) {
                            TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Format Track %d, Sector=%d, len=%d" NLP,
                                PCX,
                                pDrive->track,
                                i8272_info->fdc_sector,
                                128 << i8272_info->fdc_sec_len));
                            switch((pDrive->uptr)->u3)
                            {
                                case IMAGE_TYPE_IMD:
                                    if(pDrive->imd == NULL) {
                                        printf(".imd is NULL!" NLP);
                                    }
                                    TRACE_PRINT(WR_DATA_MSG, ("%s: Write: imd=%p t=%i h=%i s=%i l=%i" NLP,
                                        __FUNCTION__, pDrive->imd, pDrive->track, i8272_info->fdc_head,
                                        i8272_info->fdc_sector, 128 << i8272_info->fdc_sec_len));
                                    memset(sdata.raw, i8272_info->fdc_fillbyte, 128 << i8272_info->fdc_sec_len);
                                    sectWrite(pDrive->imd,
                                        pDrive->track,
                                        i8272_info->fdc_head,
                                        i8272_info->fdc_sector,
                                        sdata.raw,
                                        128 << i8272_info->fdc_sec_len,
                                        &flags,
                                        &readlen);
                                    i8272_info->result[1] = 0x80;
                                    i8272_info->result[5] = i8272_info->fdc_sector;
                                    break;
                                case IMAGE_TYPE_DSK:
                                    printf("%s: DSK Format not supported" NLP, __FUNCTION__);
                                    break;
                                case IMAGE_TYPE_CPT:
                                    printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                                    break;
                                default:
                                    printf("%s: Unknown image Format" NLP, __FUNCTION__);
                                    break;
                            }
                        }
                        break;

                    case I8272_SCAN_LOW_EQUAL:  /* SCAN LOW OR EQUAL */
                    case I8272_SCAN_HIGH_EQUAL: /* SCAN HIGH OR EQUAL */
                    case I8272_SCAN_EQUAL:  /* SCAN EQUAL */
                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT " Scan Data" NLP,
                            PCX));
                        printf("I8272: " ADDRESS_FORMAT " Scan not implemented." NLP, PCX);
                        break;
                    case I8272_READ_ID:  /* READ ID */
                        TRACE_PRINT(CMD_MSG, ("I8272: " ADDRESS_FORMAT
                            " READ ID Drive %d result ST0=%02x ST1=%02x ST2=%02x C=%02x H=%02x R=%02x N=%02x"
                            NLP, PCX, i8272_info->sel_drive, i8272_info->result[0],
                            i8272_info->result[1],i8272_info->result[2],i8272_info->result[3],
                            i8272_info->result[4],i8272_info->result[5],i8272_info->result[6]));
                        break;

                    default:
                        break;
                }
            }


                if(i8272_info->result_len != 0) {
                    i8272_info->fdc_phase ++;
                } else {
                    i8272_info->fdc_phase = 0;
                }

                i8272_info->result_index = 0;
            }


            break;
    }

    cData = 0x00;

    return (cData);
}

static uint8 I8272_Setup_Cmd(uint8 fdc_cmd)
{
    uint8 result = 0;

    switch(fdc_cmd) {
        case I8272_READ_DATA:
        case I8272_WRITE_DATA:
        case I8272_READ_DELETED_DATA:
        case I8272_WRITE_DELETED_DATA:
        case I8272_READ_TRACK:
        case I8272_SCAN_LOW_EQUAL:
        case I8272_SCAN_HIGH_EQUAL:
        case I8272_SCAN_EQUAL:
            i8272_info->cmd_len = 9;
            i8272_info->result_len = 7;
            break;
        case I8272_READ_ID: /* READ ID */
            i8272_info->cmd_len = 2;
            i8272_info->result_len = 7;
            break;
        case I8272_RECALIBRATE: /* RECALIBRATE */
            i8272_info->cmd_len = 2;
            i8272_info->result_len = 0;
            break;
        case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
            i8272_info->cmd_len = 6;
            i8272_info->result_len = 7;
            break;
        case I8272_SENSE_INTR_STATUS:   /* SENSE INTERRUPT STATUS */
            i8272_info->cmd_len = 1;
            i8272_info->result_len = 2;
            break;
        case I8272_SPECIFY: /* SPECIFY */
            i8272_info->cmd_len = 3;
            i8272_info->result_len = 0;
            break;
        case I8272_SENSE_DRIVE_STATUS:  /* SENSE DRIVE STATUS */
            i8272_info->cmd_len = 2;
            i8272_info->result_len = 1;
            break;
        case I8272_SEEK:    /* SEEK */
            i8272_info->cmd_len = 3;
            i8272_info->result_len = 0;
            break;
        default:    /* INVALID */
            i8272_info->cmd_len = 1;
            i8272_info->result_len = 1;
            result = -1;
            break;
    }
    return (result);
}

