/*************************************************************************
 *                                                                       *
 * $Id: s100_disk2.c 1995 2008-07-15 03:59:13Z hharte $                  *
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
 *     CompuPro DISK2 Hard Disk Controller module for SIMH.              *
 * This module must be used in conjunction with the CompuPro Selector    *
 * Channel Module for proper operation.                                  *
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
#define STATUS_MSG  (1 << 5)
#define IRQ_MSG     (1 << 6)
#define VERBOSE_MSG (1 << 7)

#define DISK2_MAX_DRIVES    4

typedef union {
    uint8 raw[2051];
    struct {
        uint8 header[3];
        uint8 data[2048];
    } u;
} SECTOR_FORMAT;

static SECTOR_FORMAT sdata;

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint16 ntracks;  /* number of tracks */
    uint8 nheads;    /* number of heads */
    uint8 nsectors;  /* number of sectors/track */
    uint32 sectsize; /* sector size, not including pre/postamble */
    uint16 track;    /* Current Track */
    uint8 ready;     /* Is drive ready? */
} DISK2_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   head_sel;   /* Head select (signals to drive itself) */
    uint8   head;       /* Head set by write to the HEAD register */
    uint8   cyl;        /* Cyl that the current operation is targetting */
    uint8   sector;     /* Sector the current READ/WRITE operation is targetting */
    uint8   hdr_sector; /* Current sector for WRITE_HEADER */
    uint8   ctl_attn;
    uint8   ctl_run;
    uint8   ctl_op;
    uint8   ctl_fault_clr;
    uint8   ctl_us;
    uint8   timeout;
    uint8   crc_error;
    uint8   overrun;
    uint8   seek_complete;
    uint8   write_fault;
    DISK2_DRIVE_INFO drive[DISK2_MAX_DRIVES];
} DISK2_INFO;

static DISK2_INFO disk2_info_data = { { 0x0, 0, 0xC8, 2 } };
static DISK2_INFO *disk2_info = &disk2_info_data;

/* Default geometry for a 20MB hard disk. */
#define C20MB_NTRACKS   243
#define C20MB_NHEADS    8
#define C20MB_NSECTORS  11
#define C20MB_SECTSIZE  1024

static int32 ntracks      = C20MB_NTRACKS;
static int32 nheads       = C20MB_NHEADS;
static int32 nsectors     = C20MB_NSECTORS;
static int32 sectsize     = C20MB_SECTSIZE;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 selchan_dma(uint8 *buf, uint32 len);
extern int32 find_unit_index(UNIT *uptr);
extern void raise_ss1_interrupt(uint8 intnum);

#define UNIT_V_DISK2_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_DISK2_VERBOSE      (1 << UNIT_V_DISK2_VERBOSE)
#define DISK2_CAPACITY          (C20MB_NTRACKS*C20MB_NHEADS*C20MB_NSECTORS*C20MB_SECTSIZE)   /* Default Disk Capacity */

static t_stat disk2_reset(DEVICE *disk2_dev);
static t_stat disk2_attach(UNIT *uptr, CONST char *cptr);
static t_stat disk2_detach(UNIT *uptr);
static const char* disk2_description(DEVICE *dptr);

static void raise_disk2_interrupt(void);

static int32 disk2dev(const int32 port, const int32 io, const int32 data);

static uint8 DISK2_Read(const uint32 Addr);
static uint8 DISK2_Write(const uint32 Addr, uint8 cData);

static UNIT disk2_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK2_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DISK2_CAPACITY) }
};

static REG disk2_reg[] = {
    { DRDATAD (NTRACKS,    ntracks,             10,
               "Number of tracks"),                                     },
    { DRDATAD (NHEADS,     nheads,              8,
               "Number of heads"),                                      },
    { DRDATAD (NSECTORS,   nsectors,            8,
               "Number of sectors per track"),                          },
    { DRDATAD (SECTSIZE,   sectsize,            11,
               "Sector size not including pre/postamble"),              },
    { HRDATAD (SEL_DRIVE,  disk2_info_data.sel_drive, 3,
               "Currently selected drive"),                             },
    { HRDATAD (CYL,        disk2_info_data.cyl,       8,
               "Cylinder that the current operation is targetting"),    },
    { HRDATAD (HEAD,       disk2_info_data.head,      8,
               "Head that the current operation is targetting"),        },
    { HRDATAD (SECTOR,     disk2_info_data.sector,    8,
               "Sector that the current operation is targetting"),      },

    { NULL }
};

#define DISK2_NAME  "Compupro Hard Disk Controller"

static const char* disk2_description(DEVICE *dptr) {
    return DISK2_NAME;
}

static MTAB disk2_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_DISK2_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " DISK2_NAME "n"            },
    /* verbose, show warning messages   */
    { UNIT_DISK2_VERBOSE,   UNIT_DISK2_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " DISK2_NAME "n"               },
    { 0 }
};

/* Debug Flags */
static DEBTAB disk2_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE disk2_dev = {
    "DISK2", disk2_unit, disk2_reg, disk2_mod,
    DISK2_MAX_DRIVES, 10, 31, 1, DISK2_MAX_DRIVES, DISK2_MAX_DRIVES,
    NULL, NULL, &disk2_reset,
    NULL, &disk2_attach, &disk2_detach,
    &disk2_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    disk2_dt, NULL, NULL, NULL, NULL, NULL, &disk2_description
};

/* Reset routine */
static t_stat disk2_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &disk2dev, TRUE);
    } else {
        /* Connect DISK2 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &disk2dev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}


/* Attach routine */
static t_stat disk2_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r = SCPE_OK;
    DISK2_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &disk2_info->drive[i];

    pDrive->ready = 1;
    disk2_info->write_fault = 1;
    pDrive->track = 5;
    pDrive->ntracks = ntracks;
    pDrive->nheads = nheads;
    pDrive->nsectors = nsectors;
    pDrive->sectsize = sectsize;

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
            disk2_detach(uptr);
            return r;
        }
    }

    if (uptr->flags & UNIT_DISK2_VERBOSE)
        sim_printf("DISK2%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            sim_printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            disk2_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_DISK2_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        disk2_info->drive[i].imd = diskOpenEx((uptr->fileref), (uptr->flags & UNIT_DISK2_VERBOSE),
                                              &disk2_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_DISK2_VERBOSE)
            sim_printf("\n");
    } else {
        disk2_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
static t_stat disk2_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    if (uptr->flags & UNIT_DISK2_VERBOSE)
        sim_printf("Detach DISK2%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static int32 disk2dev(const int32 port, const int32 io, const int32 data)
{
/*  sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT " IO %s, Port %02x\n", PCX, io ? "WR" : "RD", port); */
    if(io) {
        DISK2_Write(port, data);
        return 0;
    } else {
        return(DISK2_Read(port));
    }
}

#define DISK2_CSR   0   /* R=DISK2 Status / W=DISK2 Control Register */
#define DISK2_DATA  1   /* R=Step Pulse / W=Write Data Register */

static uint8 DISK2_Read(const uint32 Addr)
{
    uint8 cData;
    DISK2_DRIVE_INFO *pDrive;

    pDrive = &disk2_info->drive[disk2_info->sel_drive];
    cData = 0x00;

    switch(Addr & 0x1) {
        case DISK2_CSR:
            cData  = (disk2_info->ctl_attn) << 7;
            cData |= (disk2_info->timeout) << 6;
            cData |= (disk2_info->crc_error) << 5;
            cData |= (disk2_info->overrun) << 4;
            cData |= (pDrive->ready == 0) ? 0x08 : 0x00;
            cData |= (disk2_info->seek_complete == 0) ? 0x04 : 0x00;
            cData |= (disk2_info->write_fault) << 1;
            cData |= ((pDrive->track != 0) || (disk2_info->seek_complete == 0)) ? 0x01 : 0x00;
            sim_debug(STATUS_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                      " RD STATUS = 0x%02x\n", PCX, cData);

            disk2_info->seek_complete = 1;
            break;
        case DISK2_DATA:
            if(disk2_info->ctl_op & 0x04) {
                if(pDrive->track < pDrive->ntracks) {
                    pDrive->track ++;
                }
            } else {
                if(pDrive->track > 0) {
                    pDrive->track --;
                }
            }
            sim_debug(SEEK_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                      " Step %s, Track=%d\n", PCX,
                      disk2_info->ctl_op & 0x04 ? "IN" : "OUT", pDrive->track);
            disk2_info->seek_complete = 0;
            cData = 0xFF;           /* Return High-Z data */
            break;
    }

    return (cData);
}

#define DISK2_OP_DRIVE  0x00
#define DISK2_OP_CYL    0x01
#define DISK2_OP_HEAD   0x02
#define DISK2_OP_SECTOR 0x03

#define DISK2_CMD_NULL          0x00
#define DISK2_CMD_READ_DATA     0x01
#define DISK2_CMD_WRITE_DATA    0x02
#define DISK2_CMD_WRITE_HEADER  0x03
#define DISK2_CMD_READ_HEADER   0x04

static uint8 DISK2_Write(const uint32 Addr, uint8 cData)
{
    uint32 track_offset;
    uint8 result = 0;
    uint8 i;
    long file_offset;
    DISK2_DRIVE_INFO *pDrive;
    size_t rtn;

    pDrive = &disk2_info->drive[disk2_info->sel_drive];

    switch(Addr & 0x1) {
        case DISK2_CSR:     /* Write CTL register */
            disk2_info->ctl_attn = (cData & 0x80) >> 7;
            disk2_info->ctl_run = (cData & 0x40) >> 6;
            disk2_info->ctl_op = (cData & 0x38) >> 3;
            disk2_info->ctl_fault_clr = (cData & 0x04) >> 2;
            if(disk2_info->ctl_fault_clr == 1) {
                disk2_info->timeout = 0;
            }
            disk2_info->ctl_us = (cData & 0x03);
            sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                      " ATTN*=%d, RUN=%d, OP=%d, FAULT_CLR=%d, US=%d\n",
                      PCX, disk2_info->ctl_attn, disk2_info->ctl_run,
                      disk2_info->ctl_op, disk2_info->ctl_fault_clr,
                      disk2_info->ctl_us);

            /* FIXME: seek_complete = 1 is needed by CP/M, but why? Also, maybe related,
             *        there appears to be a bug in the seeking logic.  For some reason, the
             *        pDrive->track does not equal the disk2_info->cyl, when doing READ_DATA and
             *        WRITE_DATA commands.  For this reason, disk2_info->cyl is used instead of
             *        pDrive->track for these commands.  For READ_HEADER and WRITE_HEADER,
             *        pDrive->track is used, because the DISK2 format program (DISK2.COM) does not
             *        issue DISK2_OP_CYL.  The root cause of this anomaly needs to be determined,
             *        because it is surely a bug in the logic somewhere.
             */
             /* pDrive->track may be different from disk2_info->cyl when a program such as DISK2.COM
                moves the position of the track without informing the CP/M BIOS which stores the
                current track for each drive. This appears to be an application program bug.
            */
            disk2_info->seek_complete = 1;

            if(disk2_info->ctl_run == 1) {
                disk2_info->timeout = 0;
                track_offset = disk2_info->cyl * pDrive->nheads * pDrive->nsectors * (pDrive->sectsize + 3);

                switch(disk2_info->ctl_op) {
                    case DISK2_CMD_NULL:
                        sim_debug(CMD_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                  " NULL Command\n", PCX);
                        break;
                    case DISK2_CMD_READ_DATA:
                        sim_debug(RD_DATA_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                  " READ_DATA: (C:%d/H:%d/S:%d)\n", PCX, disk2_info->cyl, disk2_info->head, disk2_info->sector);
                        if(disk2_info->head_sel != disk2_info->head) {
                            sim_printf("DISK2: " ADDRESS_FORMAT
                                   " READ_DATA: head_sel != head" NLP, PCX);
                        }
                        /* See FIXME above... that might be why this does not work properly... */
                        if(disk2_info->cyl != pDrive->track) { /* problem, should not happen, see above */
                            sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                      " READ_DATA: cyl=%d, track=%d\n", PCX, disk2_info->cyl, pDrive->track);
                            pDrive->track = disk2_info->cyl; /* update track */
                        }
                        sim_fseek((pDrive->uptr)->fileref, track_offset + (disk2_info->head_sel * pDrive->nsectors * (pDrive->sectsize + 3)), SEEK_SET);
                        for(i=0;i<pDrive->nsectors;i++) {
                            /* Read sector */
                            rtn = sim_fread(sdata.raw, 1, (pDrive->sectsize + 3), (pDrive->uptr)->fileref);
                            if (rtn != (size_t)(pDrive->sectsize + 3)) {
                                sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                          " READ_DATA: sim_fread error.\n", PCX);
                            }
                            if(sdata.u.header[2] == disk2_info->sector) {
                                if(sdata.u.header[0] != disk2_info->cyl) { /*pDrive->track) { */
                                    sim_printf("DISK2: " ADDRESS_FORMAT
                                           " READ_DATA Incorrect header: track" NLP, PCX);
                                    disk2_info->timeout = 1;
                                }
                                if(sdata.u.header[1] != disk2_info->head) {
                                    sim_printf("DISK2: " ADDRESS_FORMAT
                                           " READ_DATA Incorrect header: head" NLP, PCX);
                                    disk2_info->timeout = 1;
                                }

                                selchan_dma(sdata.u.data, pDrive->sectsize);
                                break;
                            }
                            if(i == pDrive->nsectors) {
                                sim_printf("DISK2: " ADDRESS_FORMAT
                                       " Sector not found" NLP, PCX);
                                disk2_info->timeout = 1;
                            }
                        }

                        break;
                    case DISK2_CMD_WRITE_DATA:
                        sim_debug(WR_DATA_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                  " WRITE_DATA: (C:%d/H:%d/S:%d)\n", PCX, disk2_info->cyl, disk2_info->head, disk2_info->sector);
                        if(disk2_info->head_sel != disk2_info->head) {
                            sim_printf("DISK2: " ADDRESS_FORMAT " WRITE_DATA: head_sel != head" NLP, PCX);
                        }
                        if(disk2_info->cyl != pDrive->track) { /* problem, should not happen, see above */
                            sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                      " WRITE_DATA = 0x%02x, cyl=%d, track=%d\n", PCX, cData, disk2_info->cyl, pDrive->track);
                            pDrive->track = disk2_info->cyl; /* update track */
                       }

                        sim_fseek((pDrive->uptr)->fileref, track_offset + (disk2_info->head_sel * pDrive->nsectors * (pDrive->sectsize + 3)), SEEK_SET);
                        for(i=0;i<pDrive->nsectors;i++) {
                            /* Read sector */
                            file_offset = ftell((pDrive->uptr)->fileref);
                            rtn = sim_fread(sdata.raw, 1, 3, (pDrive->uptr)->fileref);
                            if (rtn != 3) {
                                sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                          " WRITE_DATA: sim_fread error.\n", PCX);
                            }
                            if(sdata.u.header[2] == disk2_info->sector) {
                                if(sdata.u.header[0] != disk2_info->cyl) {
                                    sim_printf("DISK2: " ADDRESS_FORMAT
                                           " WRITE_DATA Incorrect header: track" NLP, PCX);
                                    disk2_info->timeout = 1;
                                }
                                if(sdata.u.header[1] != disk2_info->head) {
                                    sim_printf("DISK2: " ADDRESS_FORMAT
                                           " WRITE_DATA Incorrect header: head" NLP, PCX);
                                    disk2_info->timeout = 1;
                                }

                                selchan_dma(sdata.u.data, pDrive->sectsize);
                                sim_fseek((pDrive->uptr)->fileref, file_offset+3, SEEK_SET);
                                sim_fwrite(sdata.u.data, 1, (pDrive->sectsize), (pDrive->uptr)->fileref);
                                break;
                            }
                            rtn = sim_fread(sdata.raw, 1, pDrive->sectsize, (pDrive->uptr)->fileref);
                            if (rtn != (size_t)(pDrive->sectsize)) {
                                sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                          " WRITE_DATA: sim_fread error.\n", PCX);
                            }
                            if(i == pDrive->nsectors) {
                                sim_printf("DISK2: " ADDRESS_FORMAT " Sector not found" NLP, PCX);
                                disk2_info->timeout = 1;
                            }
                        }
                        break;
                    case DISK2_CMD_WRITE_HEADER:
                        track_offset = pDrive->track * pDrive->nheads * pDrive->nsectors * (pDrive->sectsize + 3);
                        sim_debug(CMD_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                  " WRITE_HEADER Command: track=%d (%d), Head=%d, Sector=%d\n",
                                  PCX, pDrive->track, disk2_info->cyl,
                                  disk2_info->head_sel, disk2_info->hdr_sector);

                        i = disk2_info->hdr_sector;
                        selchan_dma(sdata.raw, 3);
                        sim_fseek((pDrive->uptr)->fileref, track_offset + (disk2_info->head_sel * (pDrive->sectsize + 3) * pDrive->nsectors) + (i * (pDrive->sectsize + 3)), SEEK_SET);
                        sim_fwrite(sdata.raw, 1, 3, (pDrive->uptr)->fileref);

                        disk2_info->hdr_sector++;
                        if(disk2_info->hdr_sector >= pDrive->nsectors) {
                            disk2_info->hdr_sector = 0;
                            disk2_info->timeout = 1;
                        }
                        break;
                    case DISK2_CMD_READ_HEADER:
                        track_offset = pDrive->track * pDrive->nheads * pDrive->nsectors * (pDrive->sectsize + 3);
                        sim_debug(CMD_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                  " READ_HEADER Command\n", PCX);
                        sim_fseek((pDrive->uptr)->fileref, track_offset + (disk2_info->head_sel * pDrive->nsectors * (pDrive->sectsize + 3)), SEEK_SET);
                        rtn = sim_fread(sdata.raw, 1, 3, (pDrive->uptr)->fileref);
                        if (rtn != 3) {
                            sim_debug(ERROR_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                                      " READ_HEADER: sim_fread error.\n", PCX);
                        }
                        selchan_dma(sdata.raw, 3);

                        break;
                    default:
                        sim_printf("DISK2: " ADDRESS_FORMAT " Unknown CMD=%d" NLP, PCX, disk2_info->ctl_op);
                        break;
                }

                raise_disk2_interrupt();
                disk2_info->ctl_attn = 0;
            }

            break;
        case DISK2_DATA:
            switch(disk2_info->ctl_op) {
                case DISK2_OP_DRIVE:
                    switch(cData >> 4) {
                        case 0x01:
                            disk2_info->sel_drive = 0;
                            break;
                        case 0x02:
                            disk2_info->sel_drive = 1;
                            break;
                        case 0x04:
                            disk2_info->sel_drive = 2;
                            break;
                        case 0x08:
                            disk2_info->sel_drive = 3;
                            break;
                        default:
                            sim_printf("DISK2: " ADDRESS_FORMAT
                                   " Error, invalid drive select=0x%x" NLP, PCX, cData >> 4);
                            break;
                    }

                    disk2_info->head_sel = cData & 0x0F;

                    sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                              " Write DATA [DRIVE]=%d, Head=%d\n",
                              PCX, disk2_info->sel_drive, disk2_info->head);
                    break;
                case DISK2_OP_CYL:
                    disk2_info->cyl = cData;
                    sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                              " Write DATA [CYL] = %02x\n", PCX, cData);
                    break;
                case DISK2_OP_HEAD:
                    disk2_info->head = cData;
                    sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                              " Write DATA [HEAD] = %02x\n", PCX, cData);
                    break;
                case DISK2_OP_SECTOR:
                    disk2_info->sector = cData;
                    sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                              " Write Register [SECTOR] = %02x\n", PCX, cData);
                    break;
                default:
                    sim_debug(VERBOSE_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
                              " Write Register unknown op [%d] = %02x\n",
                              PCX, disk2_info->ctl_op, cData);
                    break;
            }
    }

    return (result);
}

#define SS1_VI1_INT         1   /* DISK2/DISK3 interrupts tied to VI1 */

static void raise_disk2_interrupt(void)
{
    sim_debug(IRQ_MSG, &disk2_dev, "DISK2: " ADDRESS_FORMAT
              " Interrupt\n", PCX);

    raise_ss1_interrupt(SS1_VI1_INT);

}
