/*************************************************************************
 *                                                                       *
 * $Id: s100_hdc1001.c 1995 2008-07-15 03:59:13Z hharte $                *
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
 *     Advanced Digital Corporation (ADC) HDC-1001 Hard Disk Controller  *
 * module for SIMH.  The HDC-1001 controller uses the standard IDE/ATA   *
 * task-file, so this controller should be compatible with other con-    *
 * trollers that use IDE, like the GIDE interface.                       *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#include "sim_imd.h"

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define SEEK_MSG    (1 << 1)
#define CMD_MSG     (1 << 2)
#define RD_DATA_MSG (1 << 3)
#define WR_DATA_MSG (1 << 4)
#define VERBOSE_MSG (1 << 5)

#define HDC1001_MAX_DRIVES    4

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint16 sectsize;    /* sector size, not including pre/postamble */
    uint16 nsectors;    /* number of sectors/track */
    uint16 nheads;      /* number of heads */
    uint16 ntracks;     /* number of tracks */
    uint16 res_tracks;  /* Number of reserved tracks on drive. */
    uint16 track;       /* Current Track */

    uint16 cur_sect;    /* current starting sector of transfer */
    uint16 cur_track;   /* Current Track */
    uint16 xfr_nsects;  /* Number of sectors to transfer */
    uint8 ready;        /* Is drive ready? */
} HDC1001_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   taskfile[8]; /* ATA Task File Registers */
    uint8   mode;       /* mode (0xFF=absolute, 0x00=logical) */
    uint8   retries;    /* Number of retries to attempt */
    uint8   ndrives;    /* Number of drives attached to the controller */

    uint32  link_addr;  /* Link Address for next IOPB */
    uint32  dma_addr;   /* DMA Address for the current IOPB */

    HDC1001_DRIVE_INFO drive[HDC1001_MAX_DRIVES];
    uint8   iopb[16];
} HDC1001_INFO;

static HDC1001_INFO hdc1001_info_data = { { 0x0, 0, 0xC8, 8 } };
static HDC1001_INFO *hdc1001_info = &hdc1001_info_data;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

/* These are needed for DMA. */
extern void PutBYTEWrapper(const uint32 Addr, CONST uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);

#define UNIT_V_HDC1001_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_HDC1001_VERBOSE      (1 << UNIT_V_HDC1001_VERBOSE)
#define HDC1001_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */

static t_stat hdc1001_reset(DEVICE *hdc1001_dev);
static t_stat hdc1001_attach(UNIT *uptr, CONST char *cptr);
static t_stat hdc1001_detach(UNIT *uptr);

static int32 hdc1001dev(const int32 port, const int32 io, const int32 data);

static uint8 HDC1001_Read(const uint32 Addr);
static uint8 HDC1001_Write(const uint32 Addr, uint8 cData);
static const char* hdc1001_description(DEVICE *dptr);

static UNIT hdc1001_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) }
};

static REG hdc1001_reg[] = {
    { NULL }
};

#define HDC1001_NAME    "ADC Hard Disk Controller"

static const char* hdc1001_description(DEVICE *dptr) {
    return HDC1001_NAME;
}

static MTAB hdc1001_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_HDC1001_VERBOSE, 0,                      "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " HDC1001_NAME "n"          },
    /* verbose, show warning messages   */
    { UNIT_HDC1001_VERBOSE, UNIT_HDC1001_VERBOSE,   "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " HDC1001_NAME "n"             },
    { 0 }
};

/* Debug Flags */
static DEBTAB hdc1001_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE hdc1001_dev = {
    "HDC1001", hdc1001_unit, hdc1001_reg, hdc1001_mod,
    HDC1001_MAX_DRIVES, 10, 31, 1, HDC1001_MAX_DRIVES, HDC1001_MAX_DRIVES,
    NULL, NULL, &hdc1001_reset,
    NULL, &hdc1001_attach, &hdc1001_detach,
    &hdc1001_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    hdc1001_dt, NULL, NULL, NULL, NULL, NULL, &hdc1001_description
};

/* Reset routine */
static t_stat hdc1001_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &hdc1001dev, TRUE);
    } else {
        /* Connect HDC1001 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &hdc1001dev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    hdc1001_info->link_addr = 0x50; /* After RESET, the link pointer is at 0x50. */

    return SCPE_OK;
}


/* Attach routine */
static t_stat hdc1001_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r = SCPE_OK;
    HDC1001_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &hdc1001_info->drive[i];

    pDrive->ready = 1;
    pDrive->track = 5;
    pDrive->ntracks = 243;
    pDrive->nheads = 8;
    pDrive->nsectors = 11;
    pDrive->sectsize = 1024;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = (pDrive->ntracks * pDrive->nsectors * pDrive->nheads * pDrive->sectsize);
    }

    pDrive->uptr = uptr;

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        r = assignDiskType(uptr);
        if (r != SCPE_OK) {
            hdc1001_detach(uptr);
            return r;
        }
    }

    if (uptr->flags & UNIT_HDC1001_VERBOSE)
        sim_printf("HDC1001%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            sim_printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            hdc1001_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_HDC1001_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        hdc1001_info->drive[i].imd = diskOpenEx((uptr->fileref), (uptr->flags & UNIT_HDC1001_VERBOSE),
                                                &hdc1001_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_HDC1001_VERBOSE)
            sim_printf("\n");
    } else {
        hdc1001_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
static t_stat hdc1001_detach(UNIT *uptr)
{
    HDC1001_DRIVE_INFO *pDrive;
    t_stat r;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &hdc1001_info->drive[i];

    pDrive->ready = 0;

    if (uptr->flags & UNIT_HDC1001_VERBOSE)
        sim_printf("Detach HDC1001%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static int32 hdc1001dev(const int32 port, const int32 io, const int32 data)
{
/*    sim_debug(VERBOSE_MSG, &hdc1001_dev, "HDC1001: " ADDRESS_FORMAT " IO %s, Port %02x\n", PCX, io ? "WR" : "RD", port); */
    if(io) {
        HDC1001_Write(port, data);
        return 0;
    } else {
        return(HDC1001_Read(port));
    }
}

#define HDC1001_CSR   0   /* R=HDC1001 Status / W=HDC1001 Control Register */
#define HDC1001_DATA  1   /* R=Step Pulse / W=Write Data Register */

#define HDC1001_OP_DRIVE  0x00
#define HDC1001_OP_CYL    0x01
#define HDC1001_OP_HEAD   0x02
#define HDC1001_OP_SECTOR 0x03

#define HDC1001_CMD_NULL          0x00
#define HDC1001_CMD_READ_DATA     0x01
#define HDC1001_CMD_WRITE_DATA    0x02
#define HDC1001_CMD_WRITE_HEADER  0x03
#define HDC1001_CMD_READ_HEADER   0x04

#define HDC1001_STATUS_BUSY     0
#define HDC1001_STATUS_RANGE        1
#define HDC1001_STATUS_NOT_READY    2
#define HDC1001_STATUS_TIMEOUT  3
#define HDC1001_STATUS_DAT_CRC  4
#define HDC1001_STATUS_WR_FAULT 5
#define HDC1001_STATUS_OVERRUN  6
#define HDC1001_STATUS_HDR_CRC  7
#define HDC1001_STATUS_MAP_FULL 8
#define HDC1001_STATUS_COMPLETE 0xFF    /* Complete with No Error */

#define HDC1001_CODE_NOOP           0x00
#define HDC1001_CODE_VERSION        0x01
#define HDC1001_CODE_GLOBAL     0x02
#define HDC1001_CODE_SPECIFY        0x03
#define HDC1001_CODE_SET_MAP        0x04
#define HDC1001_CODE_HOME           0x05
#define HDC1001_CODE_SEEK           0x06
#define HDC1001_CODE_READ_HDR       0x07
#define HDC1001_CODE_READWRITE  0x08
#define HDC1001_CODE_RELOCATE       0x09
#define HDC1001_CODE_FORMAT     0x0A
#define HDC1001_CODE_FORMAT_BAD 0x0B
#define HDC1001_CODE_STATUS     0x0C
#define HDC1001_CODE_SELECT     0x0D
#define HDC1001_CODE_EXAMINE        0x0E
#define HDC1001_CODE_MODIFY     0x0F

#define HDC1001_IOPB_LEN    16

#define TF_DATA     0
#define TF_ERROR    1
#define TF_SECNT    2
#define TF_SECNO    3
#define TF_CYLLO    4
#define TF_CYLHI    5
#define TF_SDH      6
#define TF_CMD      7

static uint8 HDC1001_Write(const uint32 Addr, uint8 cData)
{
/*    uint8 result = HDC1001_STATUS_COMPLETE; */

    HDC1001_DRIVE_INFO *pDrive;

    pDrive = &hdc1001_info->drive[hdc1001_info->sel_drive];

    switch(Addr & 0x07) {
        case TF_SDH:
            hdc1001_info->sel_drive = (cData >> 3) & 0x03;
        case TF_DATA:
        case TF_ERROR:
        case TF_SECNT:
        case TF_SECNO:
        case TF_CYLLO:
        case TF_CYLHI:
            hdc1001_info->taskfile[Addr & 0x07] = cData;
            sim_debug(VERBOSE_MSG, &hdc1001_dev, "HDC1001: " ADDRESS_FORMAT
                      " WR TF[%d]=0x%02x\n", PCX, Addr & 7, cData);
            break;
        case TF_CMD:
            pDrive->track = hdc1001_info->taskfile[TF_CYLLO] | (hdc1001_info->taskfile[TF_CYLHI] << 8);
            pDrive->xfr_nsects = hdc1001_info->taskfile[TF_SECNT];

            sim_debug(CMD_MSG, &hdc1001_dev, "HDC1001[%d]: Command=%d, T:%d/H:%d/S:%d N=%d\n",
                      hdc1001_info->sel_drive,
                      hdc1001_info->taskfile[TF_CMD],
                      pDrive->track, hdc1001_info->taskfile[TF_SDH] & 0x07,
                      hdc1001_info->taskfile[TF_SECNO], pDrive->xfr_nsects);
            break;
        default:
            break;
    }
    return 0;
}

static uint8 HDC1001_Read(const uint32 Addr)
{
    uint8 cData;

    cData = hdc1001_info->taskfile[Addr & 0x07];
    sim_debug(VERBOSE_MSG, &hdc1001_dev, "HDC1001: " ADDRESS_FORMAT
              " RD TF[%d]=0x%02x\n", PCX, Addr & 7, cData);

    return (cData);
}

#if 0
    for(i = 0; i < HDC1001_IOPB_LEN; i++) {
        hdc1001_info->iopb[i] = GetBYTEWrapper(hdc1001_info->link_addr + i);
    }

    cmd = hdc1001_info->iopb[0];
    hdc1001_info->sel_drive = hdc1001_info->iopb[2];

    hdc1001_info->dma_addr = hdc1001_info->iopb[0x0A];
    hdc1001_info->dma_addr |= hdc1001_info->iopb[0x0B] << 8;
    hdc1001_info->dma_addr |= hdc1001_info->iopb[0x0C] << 16;

    next_link = hdc1001_info->iopb[0x0D];
    next_link |= hdc1001_info->iopb[0x0E] << 8;
    next_link |= hdc1001_info->iopb[0x0F] << 16;

    sim_debug(VERBOSE_MSG, &hdc1001_dev, "HDC1001[%d]: LINK=0x%05x, NEXT=0x%05x, CMD=%x, DMA@0x%05x\n", hdc1001_info->sel_drive, hdc1001_info->link_addr, next_link, hdc1001_info->iopb[0], hdc1001_info->dma_addr);



    if(pDrive->ready) {

        /* Perform command */
        switch(cmd) {
            case HDC1001_CODE_NOOP:
                sim_debug(VERBOSE_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " NOOP\n", hdc1001_info->sel_drive, PCX);
                break;
            case HDC1001_CODE_VERSION:
                break;
            case HDC1001_CODE_GLOBAL:
                sim_debug(CMD_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " GLOBAL\n", hdc1001_info->sel_drive, PCX);

                hdc1001_info->mode = hdc1001_info->iopb[3];
                hdc1001_info->retries = hdc1001_info->iopb[4];
                hdc1001_info->ndrives = hdc1001_info->iopb[5];

                sim_debug(VERBOSE_MSG, &hdc1001_dev, "        Mode: 0x%02x\n", hdc1001_info->mode);
                sim_debug(VERBOSE_MSG, &hdc1001_dev, "   # Retries: 0x%02x\n", hdc1001_info->retries);
                sim_debug(VERBOSE_MSG, &hdc1001_dev, "    # Drives: 0x%02x\n", hdc1001_info->ndrives);

                break;
            case HDC1001_CODE_SPECIFY:
                {
                    uint8 specify_data[22];
                    sim_debug(CMD_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " SPECIFY\n", hdc1001_info->sel_drive, PCX);

                    for(i = 0; i < 22; i++) {
                        specify_data[i] = GetBYTEWrapper(hdc1001_info->dma_addr + i);
                    }

                    pDrive->sectsize = specify_data[4] | (specify_data[5] << 8);
                    pDrive->nsectors = specify_data[6] | (specify_data[7] << 8);
                    pDrive->nheads = specify_data[8] | (specify_data[9] << 8);
                    pDrive->ntracks = specify_data[10] | (specify_data[11] << 8);
                    pDrive->res_tracks = specify_data[18] | (specify_data[19] << 8);

                    sim_debug(VERBOSE_MSG, &hdc1001_dev, "    Sectsize: %d\n", pDrive->sectsize);
                    sim_debug(VERBOSE_MSG, &hdc1001_dev, "     Sectors: %d\n", pDrive->nsectors);
                    sim_debug(VERBOSE_MSG, &hdc1001_dev, "       Heads: %d\n", pDrive->nheads);
                    sim_debug(VERBOSE_MSG, &hdc1001_dev, "      Tracks: %d\n", pDrive->ntracks);
                    sim_debug(VERBOSE_MSG, &hdc1001_dev, "    Reserved: %d\n", pDrive->res_tracks);
                    break;
                }
            case HDC1001_CODE_HOME:
                pDrive->track = 0;
                sim_debug(SEEK_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " HOME\n", hdc1001_info->sel_drive, PCX);
                break;
            case HDC1001_CODE_SEEK:
                pDrive->track = hdc1001_info->iopb[3];
                pDrive->track |= (hdc1001_info->iopb[4] << 8);

                if(pDrive->track > pDrive->ntracks) {
                    sim_debug(ERROR_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " SEEK ERROR %d not found\n", hdc1001_info->sel_drive, PCX, pDrive->track);
                    pDrive->track = pDrive->ntracks - 1;
                    result = HDC1001_STATUS_TIMEOUT;
                } else {
                    sim_debug(SEEK_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " SEEK %d\n", hdc1001_info->sel_drive, PCX, pDrive->track);
                }
                break;
            case HDC1001_CODE_READ_HDR:
            {
                sim_debug(CMD_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " READ HEADER: %d\n", pDrive->track, PCX);
                PutBYTEWrapper(hdc1001_info->dma_addr + 0, pDrive->track & 0xFF);
                PutBYTEWrapper(hdc1001_info->dma_addr + 1, (pDrive->track >> 8) & 0xFF);
                PutBYTEWrapper(hdc1001_info->dma_addr + 2, 0);
                PutBYTEWrapper(hdc1001_info->dma_addr + 3, 1);

                break;
            }
            case HDC1001_CODE_READWRITE:
            {
                uint32 track_len;
                uint32 xfr_len;
                uint32 file_offset;
                uint32 xfr_count = 0;
                uint8 *dataBuffer;

                pDrive->cur_sect = hdc1001_info->iopb[4] | (hdc1001_info->iopb[5] << 8);
                pDrive->cur_track = hdc1001_info->iopb[6] | (hdc1001_info->iopb[7] << 8);
                pDrive->xfr_nsects = hdc1001_info->iopb[8] | (hdc1001_info->iopb[9] << 8);

                track_len = pDrive->nsectors * pDrive->sectsize;

                file_offset = (pDrive->cur_track * track_len); /* Calculate offset based on current track */
                file_offset += pDrive->cur_sect + pDrive->sectsize;

                xfr_len = pDrive->xfr_nsects * pDrive->sectsize;

                dataBuffer = malloc(xfr_len);

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);

                if(hdc1001_info->iopb[3] == 1) { /* Read */
                    sim_debug(RD_DATA_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT "  READ @0x%05x T:%04d/S:%04d/#:%d\n", hdc1001_info->sel_drive, PCX, hdc1001_info->dma_addr, pDrive->cur_track, pDrive->cur_sect, pDrive->xfr_nsects );

                    sim_fread(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);

                    /* Perform DMA Transfer */
                    for(xfr_count = 0;xfr_count < xfr_len; xfr_count++) {
                        PutBYTEWrapper(hdc1001_info->dma_addr + xfr_count, dataBuffer[xfr_count]);
                    }
                } else { /* Write */
                    sim_debug(WR_DATA_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " WRITE @0x%05x T:%04d/S:%04d/#:%d\n", hdc1001_info->sel_drive, PCX, hdc1001_info->dma_addr, pDrive->cur_track, pDrive->cur_sect, pDrive->xfr_nsects );

                    /* Perform DMA Transfer */
                    for(xfr_count = 0;xfr_count < xfr_len; xfr_count++) {
                        dataBuffer[xfr_count] = GetBYTEWrapper(hdc1001_info->dma_addr + xfr_count);
                    }

                    sim_fwrite(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);
                }

                free(dataBuffer);

                break;
                }
            case HDC1001_CODE_FORMAT:
            {
                uint32 data_len;
                uint32 file_offset;
                uint8 *fmtBuffer;

                data_len = pDrive->nsectors * pDrive->sectsize;

                sim_debug(WR_DATA_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " FORMAT T:%d/H:%d/Fill=0x%02x/Len=%d\n", hdc1001_info->sel_drive, PCX, pDrive->track, hdc1001_info->iopb[5], hdc1001_info->iopb[4], data_len );

                file_offset = (pDrive->track * (pDrive->nheads) * data_len); /* Calculate offset based on current track */
                file_offset += (hdc1001_info->iopb[5] * data_len);

                fmtBuffer = malloc(data_len);
                memset(fmtBuffer, hdc1001_info->iopb[4], data_len);

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);
                sim_fwrite(fmtBuffer, 1, data_len, (pDrive->uptr)->fileref);

                free(fmtBuffer);

                break;
            }
            case HDC1001_CODE_SET_MAP:
            case HDC1001_CODE_RELOCATE:
            case HDC1001_CODE_FORMAT_BAD:
            case HDC1001_CODE_STATUS:
            case HDC1001_CODE_SELECT:
            case HDC1001_CODE_EXAMINE:
            case HDC1001_CODE_MODIFY:
            default:
                sim_debug(ERROR_MSG, &hdc1001_dev, "HDC1001[%d]: " ADDRESS_FORMAT " CMD=%x Unsupported\n", hdc1001_info->sel_drive, PCX, cmd);
                break;
        }
    } else { /* Drive not ready */
        result = HDC1001_STATUS_NOT_READY;
    }

    /* Return status */
    PutBYTEWrapper(hdc1001_info->link_addr + 1, result);

    hdc1001_info->link_addr = next_link;

    return 0;
}
#endif /* 0 */
