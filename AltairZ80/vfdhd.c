/*************************************************************************
 *                                                                       *
 * $Id: vfdhd.c 1995 2008-07-15 03:59:13Z hharte $                       *
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
 *     Micropolis FDC module for SIMH                                    *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */
#define USE_VGI     /* Use 275-byte VGI-format sectors (includes all metadata) */

#include "altairz80_defs.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#include "sim_imd.h"

/* #define DBG_MSG */

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define SEEK_MSG    (1 << 1)
#define CMD_MSG     (1 << 2)
#define RD_DATA_MSG (1 << 3)
#define WR_DATA_MSG (1 << 4)
#define STATUS_MSG  (1 << 5)
#define VERBOSE_MSG (1 << 6)

static void VFDHD_Command(void);

#define VFDHD_MAX_DRIVES    4

#define VFDHD_SECTOR_LEN    275
#define VFDHD_RAW_LEN       (40 + VFDHD_SECTOR_LEN + 128)

typedef union {
    struct {
        uint8 preamble[40]; /* Hard disk uses 30 bytes of preamble, floppy uses 40. */
        uint8 sync;
        uint8 header[2];
        uint8 unused[10];
        uint8 data[256];
        uint8 checksum;
        uint8 ecc[4];
        uint8 ecc_valid;    /* 0xAA indicates ECC is being used. */
        uint8 postamble[128];
    } u;
    uint8 raw[VFDHD_RAW_LEN];

} SECTOR_FORMAT;

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint16 ntracks; /* number of tracks */
    uint8 nheads;   /* number of heads */
    uint8 nspt;     /* number of sectors per track */
    uint8 npre_len; /* preamble length */
    uint32 sectsize; /* sector size, not including pre/postamble */
    uint16 track;
    uint8 wp;       /* Disk write protected */
    uint8 ready;    /* Drive is ready */
    uint8 write_fault;
    uint8 seek_complete;
    uint8 sync_lost;
    uint32 sector_wait_count;
} VFDHD_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8 xfr_flag;     /* Indicates controller is ready to send/receive data */
    uint8 sel_drive;    /* Currently selected drive */
    uint8 selected;     /* 1 if drive is selected */
    uint8 track0;       /* Set it selected drive is on track 0 */
    uint8 head;         /* Currently selected head */
    uint8 wr_latch;     /* Write enable latch */
    uint8 int_enable;   /* Interrupt Enable */
    uint32 datacount;   /* Number of data bytes transferred from controller for current sector */
    uint8 step;
    uint8 direction;
    uint8 rwc;
    uint8 sector;
    uint8 read;
    uint8 ecc_enable;
    uint8 precomp;
    uint8 floppy_sel;
    uint8 controller_busy;
    uint8 motor_on;
    uint8 hdsk_type;
    VFDHD_DRIVE_INFO drive[VFDHD_MAX_DRIVES];
} VFDHD_INFO;

static VFDHD_INFO vfdhd_info_data = { { 0x0, 0, 0xC0, 4 } };
static VFDHD_INFO *vfdhd_info = &vfdhd_info_data;
static const char* vfdhd_description(DEVICE *dptr);

static SECTOR_FORMAT sdata;
extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define UNIT_V_VFDHD_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_VFDHD_VERBOSE      (1 << UNIT_V_VFDHD_VERBOSE)
#define VFDHD_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */

static t_stat vfdhd_reset(DEVICE *vfdhd_dev);
static t_stat vfdhd_attach(UNIT *uptr, CONST char *cptr);
static t_stat vfdhd_detach(UNIT *uptr);

static int32 vfdhddev(const int32 port, const int32 io, const int32 data);

static uint8 VFDHD_Read(const uint32 Addr);
static uint8 VFDHD_Write(const uint32 Addr, uint8 cData);

static int32 hdSize = 5;

static UNIT vfdhd_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFDHD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFDHD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFDHD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, VFDHD_CAPACITY) }
};

static REG vfdhd_reg[] = {
    { DRDATAD (HDSIZE, hdSize, 10, "Size register"), },
    { NULL }
};

#define VFDHD_NAME  "Vector Graphic FD-HD Controller"

static const char* vfdhd_description(DEVICE *dptr) {
    return VFDHD_NAME;
}

static MTAB vfdhd_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_VFDHD_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " VFDHD_NAME "n"            },
    /* verbose, show warning messages   */
    { UNIT_VFDHD_VERBOSE,   UNIT_VFDHD_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " VFDHD_NAME "n"               },
    { 0 }
};

/* Debug Flags */
static DEBTAB vfdhd_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE vfdhd_dev = {
    "VFDHD", vfdhd_unit, vfdhd_reg, vfdhd_mod,
    VFDHD_MAX_DRIVES, 10, 31, 1, VFDHD_MAX_DRIVES, VFDHD_MAX_DRIVES,
    NULL, NULL, &vfdhd_reset,
    NULL, &vfdhd_attach, &vfdhd_detach,
    &vfdhd_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    vfdhd_dt, NULL, NULL, NULL, NULL, NULL, &vfdhd_description
};

/* Reset routine */
static t_stat vfdhd_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) {
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &vfdhddev, TRUE);
    } else {
        /* Connect MFDC at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &vfdhddev, FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}


/* Attach routine */
static t_stat vfdhd_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit                          */
    if(r != SCPE_OK)                /* error?                               */
        return r;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    for(i = 0; i < VFDHD_MAX_DRIVES; i++) {
        vfdhd_info->drive[i].uptr = &vfdhd_dev.units[i];
    }

    for(i = 0; i < VFDHD_MAX_DRIVES; i++) {
        if(vfdhd_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }
    if(i == VFDHD_MAX_DRIVES) {
        return (SCPE_IERR);
    }

    if(uptr->capac > 0) {
        r = assignDiskType(uptr);
        if (r != SCPE_OK) {
            vfdhd_detach(uptr);
            return r;
        }
    } else {
        /* creating file, must be DSK format. */
        uptr->u3 = IMAGE_TYPE_DSK;
    }

    if (uptr->flags & UNIT_VFDHD_VERBOSE)
        sim_printf("VFDHD%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            sim_printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            vfdhd_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_VFDHD_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        vfdhd_info->drive[i].imd = diskOpenEx((uptr->fileref), (uptr->flags & UNIT_VFDHD_VERBOSE),
                                              &vfdhd_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_VFDHD_VERBOSE)
            sim_printf("\n");
    } else {
        vfdhd_info->drive[i].imd = NULL;
    }

    if(i>0) { /* Floppy Disk, Unit 1-3 */
        vfdhd_info->drive[i].ntracks  = 77;     /* number of tracks */
        vfdhd_info->drive[i].nheads   = 2;      /* number of heads */
        vfdhd_info->drive[i].nspt     = 16;     /* number of sectors per track */
        vfdhd_info->drive[i].npre_len = 40;     /* preamble length */
        vfdhd_info->drive[i].sectsize = VFDHD_SECTOR_LEN;   /* sector size, not including pre/postamble */
    } else { /* Hard Disk, Unit 0 */
        if(hdSize == 10) {
            vfdhd_info->drive[i].ntracks  = 153;    /* number of tracks */
            vfdhd_info->drive[i].nheads   = 6;      /* number of heads */
            vfdhd_info->hdsk_type = 1;
            sim_printf("10MB\n");
        } else if (hdSize == 5) {
            vfdhd_info->drive[i].ntracks  = 153;    /* number of tracks */
            vfdhd_info->drive[i].nheads   = 4;      /* number of heads */
            vfdhd_info->hdsk_type = 0;
            sim_printf("5MB\n");
        } else {
            vfdhd_info->drive[i].ntracks  = 512;    /* number of tracks */
            vfdhd_info->drive[i].nheads   = 8;      /* number of heads */
            vfdhd_info->hdsk_type = 1;
            sim_printf("32MB\n");
        }

        vfdhd_info->drive[i].nheads   = 4;      /* number of heads */
        vfdhd_info->drive[i].nspt     = 32;     /* number of sectors per track */
        vfdhd_info->drive[i].npre_len = 30;     /* preamble length */
        vfdhd_info->drive[i].sectsize = VFDHD_SECTOR_LEN;   /* sector size, not including pre/postamble */
        vfdhd_info->drive[i].ready = 1;
        vfdhd_info->drive[i].seek_complete = 1;
        vfdhd_info->drive[i].sync_lost = 1;     /* Active LOW */
    }

    vfdhd_info->motor_on = 1;
    return SCPE_OK;
}


/* Detach routine */
static t_stat vfdhd_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for(i = 0; i < VFDHD_MAX_DRIVES; i++) {
        if(vfdhd_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }
    if(i == VFDHD_MAX_DRIVES) {
        return (SCPE_IERR);
    }

    DBG_PRINT(("Detach VFDHD%d\n", i));
    r = diskClose(&vfdhd_info->drive[i].imd);
    if (r != SCPE_OK)
        return r;

    r = detach_unit(uptr);  /* detach unit */
    if (r != SCPE_OK)
        return r;

    return SCPE_OK;
}


static uint8 cy;
static uint8 adc(uint8 sum, uint8 a1)
{
    uint32 total;

    total = sum + a1 + cy;

    if(total > 0xFF) {
        cy = 1;
    } else {
        cy = 0;
    }

    return(total & 0xFF);
}

static int32 vfdhddev(const int32 port, const int32 io, const int32 data)
{
    DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " IO %s, Port %02x" NLP, PCX, io ? "WR" : "RD", port));
    if(io) {
        VFDHD_Write(port, data);
        return 0;
    } else {
        return(VFDHD_Read(port));
    }
}

#define FDHD_CTRL_STATUS0   0   /* R=Status Port 0, W=Control Port 0 */
#define FDHD_CTRL_STATUS1   1   /* R=Status Port 1, W=Control Port 0 */
#define FDHD_DATA           2   /* R/W=Data Port */
#define FDHD_RESET_START    3   /* R=RESET, W=START */

static uint8 VFDHD_Read(const uint32 Addr)
{
    uint8 cData;
    VFDHD_DRIVE_INFO    *pDrive;

    pDrive = &vfdhd_info->drive[vfdhd_info->sel_drive];

    cData = 0x00;

    switch(Addr & 0x3) {
        case FDHD_CTRL_STATUS0:
            cData  = (pDrive->wp & 1);                 /* [0] Write Protect (FD) */
            cData |= (pDrive->ready & 1) << 1;         /* [1] Drive ready (HD) */
            cData |= (pDrive->track == 0) ? 0x04 : 0;  /* [2] TK0 (FD/HD) */
            cData |= (pDrive->write_fault & 1) << 3;   /* [3] Write Fault (HD) */
            cData |= (pDrive->seek_complete & 1) << 4; /* [4] Seek Complete (HD) */
            cData |= (pDrive->sync_lost & 1) << 5;     /* [5] Loss of Sync (HD) */
            cData |= 0xC0;                                                              /* [7:6] Reserved (pulled up) */
            sim_debug(STATUS_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " RD S0 = 0x%02x\n", PCX, cData);
            break;
        case FDHD_CTRL_STATUS1:
            vfdhd_info->floppy_sel = (vfdhd_info->sel_drive == 0) ? 0 : 1;
            cData  = (vfdhd_info->floppy_sel & 0x1);            /* [0] Floppy Selected */
            cData |= (vfdhd_info->controller_busy & 0x1) << 1;  /* [1] Controller busy */
            cData |= (vfdhd_info->motor_on & 0x1) << 2;         /* [2] Motor On (FD) */
            cData |= (vfdhd_info->hdsk_type & 0x1) << 3;        /* [3] Hard Disk Type (0=5MB, 1=10MB) */
            cData |= 0xF0;                                      /* [7:4] Reserved (pulled up) */
            if(vfdhd_info->sel_drive == 0) {
/*              cData &= 0xF0; */
            }

            vfdhd_info->controller_busy = 0;

            sim_debug(STATUS_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " RD S1 = 0x%02x\n", PCX, cData);
            break;
        case FDHD_DATA:
/*          DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " RD Data" NLP, PCX)); */
            if(vfdhd_info->datacount+40 >= VFDHD_RAW_LEN) {
                sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Illegal data count %d.\n", PCX, vfdhd_info->datacount);
                vfdhd_info->datacount = 0;
            }
            cData = sdata.raw[vfdhd_info->datacount+40];

            vfdhd_info->datacount++;

/*          DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " RD Data Sector %d[%03d]: 0x%02x" NLP, PCX, pDrive->sector, vfdhd_info->datacount, cData)); */
            break;
        case FDHD_RESET_START:      /* Reset */
            sim_debug(CMD_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Reset\n", PCX);
            vfdhd_info->datacount = 0;
            cData = 0xFF;           /* Return High-Z data */
            break;
    }

    return (cData);
}

static uint8 VFDHD_Write(const uint32 Addr, uint8 cData)
{
    VFDHD_DRIVE_INFO    *pDrive;

    pDrive = &vfdhd_info->drive[vfdhd_info->sel_drive];

    switch(Addr & 0x3) {
        case FDHD_CTRL_STATUS0:
            vfdhd_info->sel_drive = cData & 0x03;
            vfdhd_info->head = (cData >> 2) & 0x7;
            vfdhd_info->step = (cData >> 5) & 1;
            vfdhd_info->direction = (cData >> 6) & 1;
            vfdhd_info->rwc = (cData >> 7) & 1;

            sim_debug(WR_DATA_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " WR C0=%02x: sel_drive=%d, head=%d, step=%d, dir=%d, rwc=%d\n", PCX, cData, vfdhd_info->sel_drive, vfdhd_info->head, vfdhd_info->step, vfdhd_info->direction, vfdhd_info->rwc);

            if(vfdhd_info->step == 1) {
                if(vfdhd_info->direction == 1) { /* Step IN */
                    pDrive->track++;
                } else { /* Step OUT */
                    if(pDrive->track != 0) {
                        pDrive->track--;
                    }
                }
                sim_debug(SEEK_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Drive %d on track %d\n", PCX, vfdhd_info->sel_drive, pDrive->track);
            }

            break;
        case FDHD_CTRL_STATUS1:
            vfdhd_info->sector = (cData & 0x1f);
            vfdhd_info->read = (cData >> 5) & 1;
            vfdhd_info->ecc_enable = (cData >> 6) & 1;
            vfdhd_info->precomp = (cData >> 7) & 1;
            if(cData == 0xFF) {
                sim_debug(SEEK_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Home Disk %d\n", PCX, vfdhd_info->sel_drive);
                pDrive->track = 0;
            }
            DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " WR C1=%02x: sector=%d, read=%d, ecc_en=%d, precomp=%d" NLP,
                PCX,
                cData,
                vfdhd_info->sector,
                vfdhd_info->read,
                vfdhd_info->ecc_enable,
                vfdhd_info->precomp));
            break;
        case FDHD_DATA:     /* Data Port */
            DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " WR Data" NLP, PCX));
#ifdef USE_VGI
            if(vfdhd_info->sel_drive > 0) { /* Floppy */
                if(vfdhd_info->datacount >= VFDHD_RAW_LEN) {
                    sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Illegal data count %d.\n", PCX, vfdhd_info->datacount);
                    vfdhd_info->datacount = 0;
                }
                sdata.raw[vfdhd_info->datacount] = cData;
            } else { /* Hard */
                if(vfdhd_info->datacount+10 >= VFDHD_RAW_LEN) {
                    sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Illegal data count %d.\n", PCX, vfdhd_info->datacount);
                    vfdhd_info->datacount = 0;
                }
                sdata.raw[vfdhd_info->datacount+10] = cData;
            }
#else
            if((vfdhd_info->datacount-13 >= VFDHD_RAW_LEN) || (vfdhd_info->datacount < 13)) {
                sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Illegal data count %d.\n", PCX, vfdhd_info->datacount);
                vfdhd_info->datacount = 13;
            }
            sdata.u.data[vfdhd_info->datacount-13] = cData;
#endif /* USE_VGI */

            vfdhd_info->datacount ++;

            break;
        case FDHD_RESET_START:
            sim_debug(CMD_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " Start Command\n", PCX);
            VFDHD_Command();
            break;
    }

    cData = 0x00;

    return (cData);
}

static void VFDHD_Command(void)
{
    VFDHD_DRIVE_INFO *pDrive;

    uint32 bytesPerTrack;
    uint32 bytesPerHead;

    uint32 sec_offset;
    uint32 flags;
    int32 rtn;

    pDrive = &(vfdhd_info->drive[vfdhd_info->sel_drive]);

    bytesPerTrack = pDrive->sectsize * pDrive->nspt;
    bytesPerHead  = bytesPerTrack * pDrive->ntracks;

    sec_offset = (pDrive->track * bytesPerTrack) + \
                 (vfdhd_info->head * bytesPerHead) + \
                 (vfdhd_info->sector * pDrive->sectsize);

    vfdhd_info->controller_busy = 1;

    if(vfdhd_info->read == 1) { /* Perform a Read operation */
        unsigned int i, checksum;
        uint32 readlen;

        sim_debug(RD_DATA_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " RD: Drive=%d, Track=%d, Head=%d, Sector=%d\n", PCX, vfdhd_info->sel_drive, pDrive->track, vfdhd_info->head, vfdhd_info->sector);

        /* Clear out unused portion of sector. */
        memset(&sdata.u.unused[0], 0x00, 10);

        sdata.u.sync = 0xFF;
        sdata.u.header[0] = pDrive->track & 0xFF;
        sdata.u.header[1] = vfdhd_info->sector;

        switch((pDrive->uptr)->u3)
        {
            case IMAGE_TYPE_IMD:
                if(pDrive->imd == NULL) {
                    sim_printf(".imd is NULL!" NLP);
                }
                sim_printf("%s: Read: imd=%p" NLP, __FUNCTION__, pDrive->imd);
                sectRead(pDrive->imd,
                    pDrive->track,
                    vfdhd_info->head,
                    vfdhd_info->sector,
                    sdata.u.data,
                    256,
                    &flags,
                    &readlen);

                adc(0,0); /* clear Carry bit */
                checksum = 0;

                /* Checksum everything except the sync byte */
                for(i=1;i<269;i++) {
                    checksum = adc(checksum, sdata.raw[i+40]);
                }

                sdata.u.checksum = checksum & 0xFF;
                sdata.u.ecc_valid = 0xAA;
                break;
            case IMAGE_TYPE_DSK:
                if(pDrive->uptr->fileref == NULL) {
                    sim_printf(".fileref is NULL!" NLP);
                } else if(sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET) == 0) {
                    rtn = sim_fread(&sdata.u.sync, 1, 274, /*VFDHD_SECTOR_LEN,*/ (pDrive->uptr)->fileref);
                    if (rtn != 274) {
                        sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " READ: sim_fread error.\n", PCX);
                    }

                    memset(&sdata.u.preamble, 0, 40);
                    memset(&sdata.u.ecc, 0, 4); /* Clear out the ECC bytes  */
                    sdata.u.ecc_valid = 0xAA;   /* Set the ECC Valid byte   */
                    for(vfdhd_info->datacount = 0; sdata.raw[vfdhd_info->datacount] == 0x00; vfdhd_info->datacount++) {
                    }

                    DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " READ: Sync found at offset %d" NLP, PCX, vfdhd_info->datacount));
                } else {
                    sim_debug(ERROR_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " READ: sim_fseek error.\n", PCX);
                }
                break;
            case IMAGE_TYPE_CPT:
                sim_printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                break;
            default:
                sim_printf("%s: Unknown image Format" NLP, __FUNCTION__);
                break;
        }

    } else {    /* Perform a Write operation */
        uint32 writelen;

        sim_debug(WR_DATA_MSG, &vfdhd_dev, "VFDHD: " ADDRESS_FORMAT " WR: Drive=%d, Track=%d, Head=%d, Sector=%d\n", PCX, vfdhd_info->sel_drive, pDrive->track, vfdhd_info->head, vfdhd_info->sector);

#ifdef USE_VGI
#else
        int data_index = vfdhd_info->datacount - 13;

        sec_offset = (pDrive->track * 4096) + \
                     (vfdhd_info->head * 315392) + \
                     (vfdhd_info->sector * 256);
#endif /* USE_VGI */

        switch((pDrive->uptr)->u3)
        {
            case IMAGE_TYPE_IMD:
                if(pDrive->imd == NULL) {
                    sim_printf(".imd is NULL!" NLP);
                }
                sectWrite(pDrive->imd,
                    pDrive->track,
                    vfdhd_info->head,
                    vfdhd_info->sector,
                    sdata.u.data,
                    256,
                    &flags,
                    &writelen);
                break;
            case IMAGE_TYPE_DSK:
                if(pDrive->uptr->fileref == NULL) {
                    sim_printf(".fileref is NULL!" NLP);
                } else {
                    DBG_PRINT(("VFDHD: " ADDRESS_FORMAT " WR drive=%d, track=%d, head=%d, sector=%d" NLP,
                               PCX,
                               vfdhd_info->sel_drive,
                               pDrive->track,
                               vfdhd_info->head,
                               vfdhd_info->sector));
                    if(sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET) == 0) {
#ifdef USE_VGI
                        sim_fwrite(&sdata.u.sync, 1, VFDHD_SECTOR_LEN, (pDrive->uptr)->fileref);
#else
                        sim_fwrite(sdata.u.data, 1, 256, (pDrive->uptr)->fileref);
#endif /* USE_VGI */
                    } else {
                        sim_printf("%s: sim_fseek error" NLP, __FUNCTION__);
                    }
                }
                break;
            case IMAGE_TYPE_CPT:
                sim_printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                break;
            default:
                sim_printf("%s: Unknown image Format" NLP, __FUNCTION__);
                break;
        }
    }
}
