/*************************************************************************
 *                                                                       *
 * $Id: s100_disk3.c 1997 2008-07-18 05:29:52Z hharte $                  *
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
 *     CompuPro DISK3 Hard Disk Controller module for SIMH.              *
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
#define IRQ_MSG     (1 << 5)
#define VERBOSE_MSG (1 << 6)
#define SPECIFY_MSG (1 << 7)

#define DISK3_MAX_DRIVES    4

#define DISK3_CSR   0   /* R=DISK3 Status / W=DISK3 Control Register */
#define DISK3_DATA  1   /* R=Step Pulse / W=Write Data Register */

#define DISK3_OP_DRIVE  0x00
#define DISK3_OP_CYL    0x01
#define DISK3_OP_HEAD   0x02
#define DISK3_OP_SECTOR 0x03

#define DISK3_CMD_NULL          0x00
#define DISK3_CMD_READ_DATA     0x01
#define DISK3_CMD_WRITE_DATA    0x02
#define DISK3_CMD_WRITE_HEADER  0x03
#define DISK3_CMD_READ_HEADER   0x04

#define DISK3_STATUS_BUSY       0
#define DISK3_STATUS_RANGE      1
#define DISK3_STATUS_NOT_READY  2
#define DISK3_STATUS_TIMEOUT    3
#define DISK3_STATUS_DAT_CRC    4
#define DISK3_STATUS_WR_FAULT   5
#define DISK3_STATUS_OVERRUN    6
#define DISK3_STATUS_HDR_CRC    7
#define DISK3_STATUS_MAP_FULL   8
#define DISK3_STATUS_COMPLETE   0xFF    /* Complete with No Error */

#define DISK3_CODE_NOOP         0x00
#define DISK3_CODE_VERSION      0x01
#define DISK3_CODE_GLOBAL       0x02
#define DISK3_CODE_SPECIFY      0x03
#define DISK3_CODE_SET_MAP      0x04
#define DISK3_CODE_HOME         0x05
#define DISK3_CODE_SEEK         0x06
#define DISK3_CODE_READ_HDR     0x07
#define DISK3_CODE_READWRITE    0x08
#define DISK3_CODE_RELOCATE     0x09
#define DISK3_CODE_FORMAT       0x0A
#define DISK3_CODE_FORMAT_BAD   0x0B
#define DISK3_CODE_STATUS       0x0C
#define DISK3_CODE_SELECT       0x0D
#define DISK3_CODE_EXAMINE      0x0E
#define DISK3_CODE_MODIFY       0x0F

#define DISK3_CMD_MASK          0x3F
#define DISK3_REQUEST_IRQ       0x80

#define DISK3_IOPB_LEN  16

#define DISK3_IOPB_CMD      0
#define DISK3_IOPB_STATUS   1
#define DISK3_IOPB_DRIVE    2
#define DISK3_IOPB_ARG1     3
#define DISK3_IOPB_ARG2     4
#define DISK3_IOPB_ARG3     5
#define DISK3_IOPB_ARG4     6
#define DISK3_IOPB_ARG5     7
#define DISK3_IOPB_ARG6     8
#define DISK3_IOPB_ARG7     9
#define DISK3_IOPB_DATA     10
#define DISK3_IOPB_LINK     13

#define DISK3_MODE_ABS      0xFF
#define DISK3_MODE_LOGICAL  0x00

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
} DISK3_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   mode;       /* mode (0xFF=absolute, 0x00=logical) */
    uint8   retries;    /* Number of retries to attempt */
    uint8   ndrives;    /* Number of drives attached to the controller */

    uint32  link_addr;  /* Link Address for next IOPB */
    uint32  dma_addr;   /* DMA Address for the current IOPB */

    DISK3_DRIVE_INFO drive[DISK3_MAX_DRIVES];
    uint8   iopb[16];
} DISK3_INFO;

static DISK3_INFO disk3_info_data = { { 0x0, 0, 0x90, 2 } };
static DISK3_INFO *disk3_info = &disk3_info_data;

/* Disk geometries:
 *            ST506     ST412   CMI5619 Q520    Q540    Q2080
 *  Sectsize: 1024      1024    1024    1024    1024    1024
 *   Sectors: 9         9       9       9       9       11
 *     Heads: 4         4       6       4       8       7
 *    Tracks: 153       306     306     512     512     1172
 */

/* Default geometry for a 20MB hard disk. */
#define C20MB_SECTSIZE  1024
#define C20MB_NSECTORS  9
#define C20MB_NHEADS    4
#define C20MB_NTRACKS   512

static int32 ntracks      = C20MB_NTRACKS;
static int32 nheads       = C20MB_NHEADS;
static int32 nsectors     = C20MB_NSECTORS;
static int32 sectsize     = C20MB_SECTSIZE;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);
extern void raise_ss1_interrupt(uint8 intnum);

/* These are needed for DMA. */
extern void PutByteDMA(const uint32 Addr, const uint32 Value);
extern uint8 GetByteDMA(const uint32 Addr);

#define UNIT_V_DISK3_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_DISK3_VERBOSE      (1 << UNIT_V_DISK3_VERBOSE)
#define DISK3_CAPACITY          (C20MB_NTRACKS*C20MB_NHEADS*C20MB_NSECTORS*C20MB_SECTSIZE)   /* Default Disk Capacity */

static t_stat disk3_reset(DEVICE *disk3_dev);
static t_stat disk3_attach(UNIT *uptr, char *cptr);
static t_stat disk3_detach(UNIT *uptr);
static void raise_disk3_interrupt(void);

static int32 disk3dev(const int32 port, const int32 io, const int32 data);

/* static uint8 DISK3_Read(const uint32 Addr); */
static uint8 DISK3_Write(const uint32 Addr, uint8 cData);

static UNIT disk3_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK3_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK3_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK3_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK3_CAPACITY) }
};

static REG disk3_reg[] = {
    { DRDATAD (NTRACKS,    ntracks,             10,
               "Number of tracks"),                             },
    { DRDATAD (NHEADS,     nheads,              8,
               "Number of heads"),                              },
    { DRDATAD (NSECTORS,   nsectors,            8,
               "Number of sectors per track"),                  },
    { DRDATAD (SECTSIZE,   sectsize,            11,
               "Sector size not including pre/postamble"),      },
    { HRDATAD (SEL_DRIVE,  disk3_info_data.sel_drive, 3,
               "Currently selected drive"),                     },
    { HRDATAD (MODE,       disk3_info_data.mode,       8,
               "Mode (0xFF=absolute, 0x00=logical)"),           },
    { HRDATAD (RETRIES,    disk3_info_data.retries,    8,
               "Number of retries to attempt"),                 },
    { HRDATAD (NDRIVES,    disk3_info_data.ndrives,    8,
               "Number of drives attached to the controller"),  },
    { HRDATAD (LINK_ADDR,  disk3_info_data.link_addr, 32,
               "Link address for next IOPB"),                   },
    { HRDATAD (DMA_ADDR,   disk3_info_data.dma_addr,  32,
               "DMA address for the current IOPB"),             },
    { BRDATAD (IOPB,       &disk3_info_data.iopb[DISK3_IOPB_CMD],  16, 8, 16,
               "IOPB command register"), }                      ,
    { NULL }
};

#define DISK3_NAME  "Compupro ST-506 Disk Controller DISK3"

static MTAB disk3_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_DISK3_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " DISK3_NAME "n"            },
    /* verbose, show warning messages   */
    { UNIT_DISK3_VERBOSE,   UNIT_DISK3_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " DISK3_NAME "n"               },
    { 0 }
};

/* Debug Flags */
static DEBTAB disk3_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,     "Write messages"   },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "SPECIFY",    SPECIFY_MSG,    "Specify messages"  },
    { NULL,         0                                   }
};

DEVICE disk3_dev = {
    "DISK3", disk3_unit, disk3_reg, disk3_mod,
    DISK3_MAX_DRIVES, 10, 31, 1, DISK3_MAX_DRIVES, DISK3_MAX_DRIVES,
    NULL, NULL, &disk3_reset,
    NULL, &disk3_attach, &disk3_detach,
    &disk3_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    disk3_dt, NULL, DISK3_NAME
};

/* Reset routine */
static t_stat disk3_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &disk3dev, TRUE);
    } else {
        /* Connect DISK3 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &disk3dev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    disk3_info->link_addr = 0x50;   /* After RESET, the link pointer is at 0x50. */

    return SCPE_OK;
}


/* Attach routine */
static t_stat disk3_attach(UNIT *uptr, char *cptr)
{
    t_stat r = SCPE_OK;
    DISK3_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &disk3_info->drive[i];

    pDrive->ready = 1;
    pDrive->track = 5;
    pDrive->ntracks = C20MB_NTRACKS;
    pDrive->nheads = C20MB_NHEADS;
    pDrive->nsectors = C20MB_NSECTORS;
    pDrive->sectsize = C20MB_SECTSIZE;

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
            disk3_detach(uptr);
            return r;
        }
    }

    if (uptr->flags & UNIT_DISK3_VERBOSE)
        sim_printf("DISK3%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            sim_printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            disk3_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_DISK3_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        disk3_info->drive[i].imd = diskOpenEx((uptr->fileref), (uptr->flags & UNIT_DISK3_VERBOSE),
                                              &disk3_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_DISK3_VERBOSE)
            sim_printf("\n");
    } else {
        disk3_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
static t_stat disk3_detach(UNIT *uptr)
{
    DISK3_DRIVE_INFO *pDrive;
    t_stat r;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &disk3_info->drive[i];

    pDrive->ready = 0;

    if (uptr->flags & UNIT_DISK3_VERBOSE)
        sim_printf("Detach DISK3%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static int32 disk3dev(const int32 port, const int32 io, const int32 data)
{
    sim_debug(VERBOSE_MSG, &disk3_dev, "DISK3: " ADDRESS_FORMAT
              " IO %s, Port %02x\n", PCX, io ? "WR" : "RD", port);
    if(io) {
        DISK3_Write(port, data);
        return 0;
    } else {
        return(0xFF);
    }
}

static uint8 DISK3_Write(const uint32 Addr, uint8 cData)
{
    uint32 next_link;
    uint8 result = DISK3_STATUS_COMPLETE;
    uint8 i;
    uint8 cmd;

    DISK3_DRIVE_INFO *pDrive;

    for(i = 0; i < DISK3_IOPB_LEN; i++) {
        disk3_info->iopb[i] = GetByteDMA(disk3_info->link_addr + i);
    }

    cmd = disk3_info->iopb[DISK3_IOPB_CMD];
    disk3_info->sel_drive = disk3_info->iopb[DISK3_IOPB_DRIVE] & 0x03;

    disk3_info->dma_addr = disk3_info->iopb[0x0A];
    disk3_info->dma_addr |= disk3_info->iopb[0x0B] << 8;
    disk3_info->dma_addr |= disk3_info->iopb[0x0C] << 16;

    next_link  = disk3_info->iopb[DISK3_IOPB_LINK+0];
    next_link |= disk3_info->iopb[DISK3_IOPB_LINK+1] << 8;
    next_link |= disk3_info->iopb[DISK3_IOPB_LINK+2] << 16;

    sim_debug(VERBOSE_MSG, &disk3_dev, "DISK3[%d]: LINK=0x%05x, NEXT=0x%05x, CMD=%x, %s DMA@0x%05x\n",
              disk3_info->sel_drive,
              disk3_info->link_addr,
              next_link,
              disk3_info->iopb[DISK3_IOPB_CMD] & DISK3_CMD_MASK,
              (disk3_info->iopb[DISK3_IOPB_CMD] & DISK3_REQUEST_IRQ) ? "IRQ" : "POLL",
              disk3_info->dma_addr);

    pDrive = &disk3_info->drive[disk3_info->sel_drive];

    if(pDrive->ready) {

        /* Perform command */
        switch(cmd & DISK3_CMD_MASK) {
            case DISK3_CODE_NOOP:
                sim_debug(VERBOSE_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " NOOP\n", disk3_info->sel_drive, PCX);
                break;
            case DISK3_CODE_VERSION:
                break;
            case DISK3_CODE_GLOBAL:
                sim_debug(CMD_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " GLOBAL\n", disk3_info->sel_drive, PCX);

                disk3_info->mode = disk3_info->iopb[DISK3_IOPB_ARG1];
                disk3_info->retries = disk3_info->iopb[DISK3_IOPB_ARG2];
                disk3_info->ndrives = disk3_info->iopb[DISK3_IOPB_ARG3];

                sim_debug(SPECIFY_MSG, &disk3_dev, "        Mode: 0x%02x\n", disk3_info->mode);
                sim_debug(SPECIFY_MSG, &disk3_dev, "   # Retries: 0x%02x\n", disk3_info->retries);
                sim_debug(SPECIFY_MSG, &disk3_dev, "    # Drives: 0x%02x\n", disk3_info->ndrives);

                if(disk3_info->mode == DISK3_MODE_ABS) {
                    sim_debug(ERROR_MSG, &disk3_dev, "DISK3: Absolute addressing not supported.\n");
                }

                break;
            case DISK3_CODE_SPECIFY:
                {
                    uint8 specify_data[22];
                    sim_debug(CMD_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                              " SPECIFY\n", disk3_info->sel_drive, PCX);

                    for(i = 0; i < 22; i++) {
                        specify_data[i] = GetByteDMA(disk3_info->dma_addr + i);
                    }

                    pDrive->sectsize = specify_data[4] | (specify_data[5] << 8);
                    pDrive->nsectors = specify_data[6] | (specify_data[7] << 8);
                    pDrive->nheads = specify_data[8] | (specify_data[9] << 8);
                    pDrive->ntracks = specify_data[10] | (specify_data[11] << 8);
                    pDrive->res_tracks = specify_data[18] | (specify_data[19] << 8);

                    sim_debug(SPECIFY_MSG, &disk3_dev, "    Sectsize: %d\n", pDrive->sectsize);
                    sim_debug(SPECIFY_MSG, &disk3_dev, "     Sectors: %d\n", pDrive->nsectors);
                    sim_debug(SPECIFY_MSG, &disk3_dev, "       Heads: %d\n", pDrive->nheads);
                    sim_debug(SPECIFY_MSG, &disk3_dev, "      Tracks: %d\n", pDrive->ntracks);
                    sim_debug(SPECIFY_MSG, &disk3_dev, "    Reserved: %d\n", pDrive->res_tracks);
                    break;
                }
            case DISK3_CODE_HOME:
                pDrive->track = 0;
                sim_debug(SEEK_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " HOME\n", disk3_info->sel_drive, PCX);
                break;
            case DISK3_CODE_SEEK:
                pDrive->track = disk3_info->iopb[DISK3_IOPB_ARG1];
                pDrive->track |= (disk3_info->iopb[DISK3_IOPB_ARG2] << 8);

                if(pDrive->track > pDrive->ntracks) {
                    sim_debug(ERROR_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                              " SEEK ERROR %d not found\n", disk3_info->sel_drive, PCX, pDrive->track);
                    pDrive->track = pDrive->ntracks - 1;
                    result = DISK3_STATUS_TIMEOUT;
                } else {
                    sim_debug(SEEK_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                              " SEEK %d\n", disk3_info->sel_drive, PCX, pDrive->track);
                }
                break;
            case DISK3_CODE_READ_HDR:
            {
                sim_debug(CMD_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " READ HEADER: %d\n", pDrive->track, PCX, pDrive->track >> 8);
                PutByteDMA(disk3_info->dma_addr + 0, pDrive->track & 0xFF);
                PutByteDMA(disk3_info->dma_addr + 1, (pDrive->track >> 8) & 0xFF);
                PutByteDMA(disk3_info->dma_addr + 2, 0);
                PutByteDMA(disk3_info->dma_addr + 3, 1);

                break;
            }
            case DISK3_CODE_READWRITE:
            {
                uint32 track_len;
                uint32 xfr_len;
                uint32 file_offset;
                uint32 xfr_count = 0;
                uint8 *dataBuffer;
                size_t rtn;

                if(disk3_info->mode == DISK3_MODE_ABS) {
                    sim_debug(ERROR_MSG, &disk3_dev, "DISK3: Absolute addressing not supported.\n");
                    break;
                }

                pDrive->cur_sect = disk3_info->iopb[DISK3_IOPB_ARG2] | (disk3_info->iopb[DISK3_IOPB_ARG3] << 8);
                pDrive->cur_track = disk3_info->iopb[DISK3_IOPB_ARG4] | (disk3_info->iopb[DISK3_IOPB_ARG5] << 8);
                pDrive->xfr_nsects = disk3_info->iopb[DISK3_IOPB_ARG6] | (disk3_info->iopb[DISK3_IOPB_ARG7] << 8);

                track_len = pDrive->nsectors * pDrive->sectsize;

                file_offset = (pDrive->cur_track * track_len); /* Calculate offset based on current track */
                file_offset += pDrive->cur_sect * pDrive->sectsize;

                xfr_len = pDrive->xfr_nsects * pDrive->sectsize;

                dataBuffer = malloc(xfr_len);

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);

                if(disk3_info->iopb[DISK3_IOPB_ARG1] == 1) { /* Read */
                    rtn = sim_fread(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);

                    sim_debug(RD_DATA_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                              "  READ @0x%05x T:%04d/S:%04d/#:%d %s\n",
                              disk3_info->sel_drive,
                              PCX,
                              disk3_info->dma_addr,
                              pDrive->cur_track,
                              pDrive->cur_sect,
                              pDrive->xfr_nsects,
                              rtn == (size_t)xfr_len ? "OK" : "NOK" );


                    /* Perform DMA Transfer */
                    for(xfr_count = 0;xfr_count < xfr_len; xfr_count++) {
                        PutByteDMA(disk3_info->dma_addr + xfr_count, dataBuffer[xfr_count]);
                    }
                } else { /* Write */
                    sim_debug(WR_DATA_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                              " WRITE @0x%05x T:%04d/S:%04d/#:%d\n", disk3_info->sel_drive, PCX, disk3_info->dma_addr, pDrive->cur_track, pDrive->cur_sect, pDrive->xfr_nsects );

                    /* Perform DMA Transfer */
                    for(xfr_count = 0;xfr_count < xfr_len; xfr_count++) {
                        dataBuffer[xfr_count] = GetByteDMA(disk3_info->dma_addr + xfr_count);
                    }

                    sim_fwrite(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);
                }

                free(dataBuffer);
                /* Update Track/Sector in IOPB */
                pDrive->cur_sect += pDrive->xfr_nsects;
                if(pDrive->cur_sect >= pDrive->nsectors) {
                    pDrive->cur_sect = pDrive->cur_sect % pDrive->nsectors;
                    pDrive->cur_track++;
                }
                disk3_info->iopb[DISK3_IOPB_ARG2] = pDrive->cur_sect & 0xFF;
                disk3_info->iopb[DISK3_IOPB_ARG3] = (pDrive->cur_sect >> 8) & 0xFF;
                disk3_info->iopb[DISK3_IOPB_ARG4] = pDrive->cur_track & 0xFF;
                disk3_info->iopb[DISK3_IOPB_ARG5] = (pDrive->cur_track >> 8) & 0xFF;
                disk3_info->iopb[DISK3_IOPB_ARG6] = 0;
                disk3_info->iopb[DISK3_IOPB_ARG7] = 0;

                /* Update the DATA field in the IOPB */
                disk3_info->dma_addr += xfr_len;
                disk3_info->iopb[DISK3_IOPB_DATA+0] = disk3_info->dma_addr & 0xFF;
                disk3_info->iopb[DISK3_IOPB_DATA+1] = (disk3_info->dma_addr >> 8) & 0xFF;
                disk3_info->iopb[DISK3_IOPB_DATA+2] = (disk3_info->dma_addr >> 16) & 0xFF;

                break;
                }
            case DISK3_CODE_FORMAT:
            {
                uint32 data_len;
                uint32 file_offset;
                uint8 *fmtBuffer;

                data_len = pDrive->nsectors * pDrive->sectsize;

                sim_debug(WR_DATA_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " FORMAT T:%d/H:%d/Fill=0x%02x/Len=%d\n",
                          disk3_info->sel_drive,
                          PCX,
                          pDrive->track,
                          disk3_info->iopb[DISK3_IOPB_ARG3],
                          disk3_info->iopb[DISK3_IOPB_ARG2],
                          data_len);

                file_offset = (pDrive->track * (pDrive->nheads) * data_len); /* Calculate offset based on current track */
                file_offset += (disk3_info->iopb[DISK3_IOPB_ARG3] * data_len);

                fmtBuffer = malloc(data_len);
                memset(fmtBuffer, disk3_info->iopb[DISK3_IOPB_ARG2], data_len);

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);
                sim_fwrite(fmtBuffer, 1, data_len, (pDrive->uptr)->fileref);

                free(fmtBuffer);

                break;
            }
            case DISK3_CODE_SET_MAP:
                break;
            case DISK3_CODE_RELOCATE:
            case DISK3_CODE_FORMAT_BAD:
            case DISK3_CODE_STATUS:
            case DISK3_CODE_SELECT:
            case DISK3_CODE_EXAMINE:
            case DISK3_CODE_MODIFY:
            default:
                sim_debug(ERROR_MSG, &disk3_dev, "DISK3[%d]: " ADDRESS_FORMAT
                          " CMD=%x Unsupported\n",
                          disk3_info->sel_drive,
                          PCX,
                          cmd & DISK3_CMD_MASK);
                break;
        }
    } else { /* Drive not ready */
        result = DISK3_STATUS_NOT_READY;
    }

    /* Return status */
    disk3_info->iopb[DISK3_IOPB_STATUS] = result;

    /* Update IOPB in host memory */
    for(i = 0; i < DISK3_IOPB_LEN; i++) {
        PutByteDMA(disk3_info->link_addr + i, disk3_info->iopb[i]);
    }

    if(cmd & DISK3_REQUEST_IRQ) {
        raise_disk3_interrupt();
    }
    disk3_info->link_addr = next_link;

    return 0;
}

#define SS1_VI1_INT         1   /* DISK2/DISK3 interrupts tied to VI1 */

static void raise_disk3_interrupt(void)
{
    sim_debug(IRQ_MSG, &disk3_dev, "DISK3: " ADDRESS_FORMAT " Interrupt\n", PCX);

    raise_ss1_interrupt(SS1_VI1_INT);

}
