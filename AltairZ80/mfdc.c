/*************************************************************************
 *                                                                       *
 * $Id: mfdc.c 1995 2008-07-15 03:59:13Z hharte $                        *
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
 *     Micropolis FD Control module for SIMH.                            *
 * See the "Vector Using MDOS Revision 8.4" manual at:                   *
 * www.hartetechnologies.com/manuals in the Vector Graphic section       *
 * for details of the on-disk sector format and programming information. *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG */
#define USE_VGI     /* Use 275-byte VGI-format sectors (includes all metadata) */

#include "altairz80_defs.h"
#include "sim_imd.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#ifdef DBG_MSG
#define DBG_PRINT(args) printf args
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
#define ORDERS_MSG  (1 << 7)

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
    int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

static void MFDC_Command(uint8 cData);

#define MFDC_MAX_DRIVES 4
#define JUMPER_W9       1   /* Not Installed (0) = 2MHz, Installed (1) = 4MHz. */
#define JUMPER_W10      0

#define MFDC_SECTOR_LEN 275

typedef union {
    struct {
        uint8 sync;
        uint8 header[2];
        uint8 unused[10];
        uint8 data[256];
        uint8 checksum;
        uint8 ecc[4];
        uint8 ecc_valid;    /* Not used for Micropolis FDC, but is used by FDHD. */
    } u;
    uint8 raw[MFDC_SECTOR_LEN];

} SECTOR_FORMAT;

typedef struct {
    UNIT *uptr;
    DISK_INFO *imd;
    uint8 track;
    uint8 wp;       /* Disk write protected */
    uint8 ready;    /* Drive is ready */
    uint8 sector;   /* Current Sector number */
    uint32 sector_wait_count;
} MFDC_DRIVE_INFO;

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
    uint8 read_in_progress; /* TRUE if a read is in progress */
    MFDC_DRIVE_INFO drive[MFDC_MAX_DRIVES];
} MFDC_INFO;

static MFDC_INFO mfdc_info_data = { { 0xF800, 1024, 0, 0 } };
static MFDC_INFO *mfdc_info = &mfdc_info_data;

static SECTOR_FORMAT sdata;

#define UNIT_V_MFDC_WLK         (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_MFDC_WLK           (1 << UNIT_V_MFDC_WLK)
#define UNIT_V_MFDC_VERBOSE     (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_MFDC_VERBOSE       (1 << UNIT_V_MFDC_VERBOSE)
#define MFDC_CAPACITY           (77*16*MFDC_SECTOR_LEN) /* Default Micropolis Disk Capacity */
#define IMAGE_TYPE_DSK          1               /* Flat binary "DSK" image file.            */
#define IMAGE_TYPE_IMD          2               /* ImageDisk "IMD" image file.              */
#define IMAGE_TYPE_CPT          3               /* CP/M Transfer "CPT" image file.          */

static t_stat mfdc_reset(DEVICE *mfdc_dev);
static t_stat mfdc_attach(UNIT *uptr, char *cptr);
static t_stat mfdc_detach(UNIT *uptr);
static uint8 MFDC_Read(const uint32 Addr);
static uint8 MFDC_Write(const uint32 Addr, uint8 cData);

static int32 mdskdev(const int32 Addr, const int32 rw, const int32 data);

static UNIT mfdc_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MFDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MFDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MFDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MFDC_CAPACITY) }
};

static REG mfdc_reg[] = {
    { NULL }
};

static MTAB mfdc_mod[] = {
    { MTAB_XTD|MTAB_VDV,            0,                  "MEMBASE",  "MEMBASE", &set_membase, &show_membase, NULL },
    { UNIT_MFDC_WLK,                0,                  "WRTENB",   "WRTENB", NULL  },
    { UNIT_MFDC_WLK,                UNIT_MFDC_WLK,      "WRTLCK",   "WRTLCK", NULL  },
    /* quiet, no warning messages       */
    { UNIT_MFDC_VERBOSE,            0,                  "QUIET",    "QUIET", NULL   },
    /* verbose, show warning messages   */
    { UNIT_MFDC_VERBOSE,            UNIT_MFDC_VERBOSE,  "VERBOSE",  "VERBOSE", NULL },
    { 0 }
};

#define TRACE_PRINT(level, args)    if(mfdc_dev.dctrl & level) {    \
                                       printf args;                 \
                                    }

/* Debug Flags */
static DEBTAB mfdc_dt[] = {
    { "ERROR",  ERROR_MSG },
    { "SEEK",   SEEK_MSG },
    { "CMD",    CMD_MSG },
    { "RDDATA", RD_DATA_MSG },
    { "WRDATA", WR_DATA_MSG },
    { "STATUS", STATUS_MSG },
    { "ORDERS", ORDERS_MSG },
    { NULL,     0 }
};

DEVICE mfdc_dev = {
    "MDSK", mfdc_unit, mfdc_reg, mfdc_mod,
    MFDC_MAX_DRIVES, 10, 31, 1, MFDC_MAX_DRIVES, MFDC_MAX_DRIVES,
    NULL, NULL, &mfdc_reset,
    NULL, &mfdc_attach, &mfdc_detach,
    &mfdc_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    mfdc_dt, NULL, "Micropolis FD Control MDSK"
};

/* Micropolis FD Control Boot ROM
 * This ROM code is runtime-relocatable.  See Appendix F of the "Vector Using MDOS Revision 8.4"
 * manual at www.hartetechnologies.com/manuals in the Vector Graphic section.
 */
static uint8 mfdc_rom[256] = {
    0xF3, 0x21, 0xA2, 0x00, 0xF9, 0x36, 0xC9, 0xCD, 0xA2, 0x00, 0xEB, 0x2A, 0xA0, 0x00, 0x2E, 0x00, /* 0x00 */
    0xE5, 0x01, 0x1D, 0x00, 0x09, 0xE5, 0xE1, 0x0E, 0x1A, 0x09, 0x06, 0xBD, 0xEB, 0x3B, 0x3B, 0x1A, /* 0x10 */
    0x77, 0xBE, 0xC0, 0x23, 0x13, 0x05, 0xC0, 0xE1, 0x2A, 0xA0, 0x00, 0x11, 0x00, 0x02, 0x19, 0x22, /* 0x20 */
    0xA2, 0x00, 0x36, 0xA0, 0xC3, 0xD4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0xA2, 0x00, 0x7E, 0xE6, /* 0x30 */
    0x80, 0xCA, 0xA9, 0x00, 0x7E, 0xE6, 0x0F, 0xA8, 0xC2, 0xA9, 0x00, 0x23, 0xB6, 0xF2, 0xB7, 0x00, /* 0x40 */
    0x23, 0x7E, 0xAF, 0xEB, 0x06, 0x00, 0x00, 0x00, 0x1A, 0x77, 0x23, 0x88, 0x47, 0x1A, 0x77, 0x23, /* 0x50 */
    0x88, 0x47, 0x0D, 0xC2, 0xC3, 0x00, 0x1A, 0xB8, 0xC9, 0x2A, 0xA2, 0x00, 0x36, 0x20, 0x23, 0x7E, /* 0x60 */
    0x2B, 0xE6, 0x24, 0xEE, 0x20, 0xC2, 0xD4, 0x00, 0x0E, 0x5E, 0xCD, 0x49, 0x01, 0x23, 0x7E, 0x2B, /* 0x70 */
    0xE6, 0x24, 0xEE, 0x20, 0xC2, 0xD4, 0x00, 0x23, 0x7E, 0xE6, 0x08, 0x2B, 0xCA, 0x07, 0x01, 0x06, /* 0x80 */
    0x08, 0x36, 0x61, 0x0E, 0x0F, 0xCD, 0x49, 0x01, 0x05, 0xC2, 0xFC, 0x00, 0x23, 0x7E, 0xE6, 0x08, /* 0x90 */
    0x2B, 0xC2, 0x19, 0x01, 0x36, 0x60, 0x0E, 0x0F, 0xCD, 0x49, 0x01, 0xC3, 0x07, 0x01, 0x21, 0x5F, /* 0xA0 */
    0x01, 0xCD, 0x37, 0x01, 0xC2, 0xD4, 0x00, 0x2A, 0x69, 0x02, 0x22, 0xA4, 0x00, 0xCD, 0x37, 0x01, /* 0xB0 */
    0xC2, 0xD4, 0x00, 0x2A, 0xA4, 0x00, 0x11, 0x0C, 0x00, 0x19, 0xD1, 0xE9, 0xE5, 0xEB, 0x01, 0x86, /* 0xC0 */
    0x00, 0xCD, 0xA6, 0x00, 0xE1, 0xC2, 0x37, 0x01, 0xE5, 0x7E, 0x23, 0xB6, 0xE1, 0xC9, 0x7E, 0xE6, /* 0xD0 */
    0x20, 0x79, 0xC2, 0x51, 0x01, 0x07, 0x4F, 0x3E, 0xFF, 0xD6, 0x01, 0xB7, 0xC2, 0x54, 0x01, 0x0D, /* 0xE0 */
    0xC2, 0x52, 0x01, 0xC9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xA6, 0x00  /* 0xF0 */
};

/* Reset routine */
t_stat mfdc_reset(DEVICE *dptr)
{
    uint8 i;
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) {
        sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &mdskdev, TRUE);
    } else {
        /* Connect MFDC at base address */
        for(i = 0; i < MFDC_MAX_DRIVES; i++) {
            mfdc_info->drive[i].uptr = &mfdc_dev.units[i];
        }
        if(sim_map_resource(pnp->mem_base, pnp->mem_size, RESOURCE_TYPE_MEMORY, &mdskdev, FALSE) != 0) {
            printf("%s: error mapping resource at 0x%04x\n", __FUNCTION__, pnp->mem_base);
            dptr->flags |= DEV_DIS;
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

/* Attach routine */
t_stat mfdc_attach(UNIT *uptr, char *cptr)
{
    char header[4];
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = MFDC_CAPACITY;
    }

    i = find_unit_index(uptr);

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        fgets(header, 4, uptr->fileref);
        if(!strcmp(header, "IMD")) {
            uptr->u3 = IMAGE_TYPE_IMD;
        } else if(!strcmp(header, "CPT")) {
            printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            mfdc_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_MFDC_VERBOSE)
        printf("MDSK%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            mfdc_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_MFDC_VERBOSE)
            printf("--------------------------------------------------------\n");
        mfdc_info->drive[i].imd = diskOpen((uptr->fileref), (uptr->flags & UNIT_MFDC_VERBOSE));
        if (uptr->flags & UNIT_MFDC_VERBOSE)
            printf("\n");
    } else {
        mfdc_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
t_stat mfdc_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for(i = 0; i < MFDC_MAX_DRIVES; i++) {
        if(mfdc_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= MFDC_MAX_DRIVES)
        return SCPE_ARG;

    DBG_PRINT(("Detach MFDC%d\n", i));
    r = diskClose(&mfdc_info->drive[i].imd);
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

/* Main Entry Point for Memory-Mapped I/O to the Micropolis FD Control Board
 *
 * The controller is typically located at 0xF800 in the Memory Map, and occupies
 * 1K of address space.  Accesses are broken down as follows:
 *
 * 0xF800-0xF8FF:   Bootstrap ROM
 * 0xF900-0xF9FF:   Nothing (reads 0xFF)
 * 0xFA00-0xFBFF:   Controller registers: there are four registers, which are shadowed
 *                  throughout this 512-byte range.
 *
 * The controller can be relocated on any 1K boundary in the memory map, and since the
 * boot ROM code is runtime relocatable, it moves with the controller registers.
 */
static int32 mdskdev(const int32 Addr, const int32 rw, const int32 data)
{
    switch(Addr & 0x300) {
        case 0x000:         /* Boot ROM */
            if(rw == 0) {   /* Read boot ROM */
                return(mfdc_rom[Addr & 0xFF]);
            } else {
                printf("MFDC: Attempt to write to boot ROM." NLP);
                return (-1);
            }
            break;
        case 0x100:         /* Nothing */
            return(0xFF);
            break;
        case 0x200:
        case 0x300:         /* Controller Registers */
            if(rw == 0) {   /* Read Register */
                return(MFDC_Read(Addr));
            } else {        /* Write Register */
                return(MFDC_Write(Addr, data));
            }
            break;
    }

    return(-1);
}


static uint8 MFDC_Read(const uint32 Addr)
{
    uint8 cData;
    MFDC_DRIVE_INFO *pDrive;

    cData = 0x00;

    pDrive = &mfdc_info->drive[mfdc_info->sel_drive];

    switch(Addr & 0x3) {
        case 0:
            if(mfdc_info->read_in_progress == FALSE) {
                pDrive->sector_wait_count++;
                if(pDrive->sector_wait_count > 10) {
                    pDrive->sector++;
                    pDrive->sector &= 0x0F;     /* Max of 16 sectors */
                    mfdc_info->wr_latch = 0;    /* on new sector, disable the write latch */
                    DBG_PRINT(("Head over sector %d" NLP, pDrive->sector));
                    pDrive->sector_wait_count = 0;
                }
            }

            cData = (pDrive->sector) & 0xF;  /* [3:0] current sector */
            cData |= (JUMPER_W10 << 4);
            cData |= ((~JUMPER_W9) & 1) << 5;
            cData |= (0 << 6);      /* Sector Interrupt Flag, reset by RESET command or Interrupt Disable */
            cData |= (1 << 7);      /* Sector Flag */
            mfdc_info->xfr_flag = 1;    /* Drive has data */
            mfdc_info->datacount = 0;
            TRACE_PRINT(STATUS_MSG, ("MFDC: " ADDRESS_FORMAT " RD Sector Register = 0x%02x" NLP, PCX, cData));
            break;
        case 1:
            cData  = (mfdc_info->sel_drive & 0x3);  /* [1:0] selected drive */
            cData |= (!mfdc_info->selected << 2);   /* [2] drive is selected */
            cData |= (pDrive->track == 0) ? 0x08 : 0; /* [3] TK0 */
            pDrive->wp = ((pDrive->uptr)->flags & UNIT_MFDC_WLK) ? 1 : 0;
            cData |= (pDrive->wp << 4); /* [4] Write Protect */
            cData |= (pDrive->ready << 5); /* [5] Drive Ready */
            cData |= (0 << 6); /* [6] PINTE from S-100 Bus */
            cData |= (mfdc_info->xfr_flag << 7); /* [7] Transfer Flag */

            TRACE_PRINT(STATUS_MSG, ("MFDC: " ADDRESS_FORMAT " RD Status = 0x%02x" NLP, PCX, cData));
            break;
        case 2:
        case 3:
            if(mfdc_info->datacount == 0) {
                unsigned int i, checksum;
                unsigned long sec_offset;
                unsigned int flags;
                unsigned int readlen;

                /* Clear out unused portion of sector. */
                memset(&sdata.u.unused[0], 0x00, 10);

                sdata.u.sync = 0xFF;
                sdata.u.header[0] = pDrive->track;
                sdata.u.header[1] = pDrive->sector;

                TRACE_PRINT(RD_DATA_MSG, ("MFDC: " ADDRESS_FORMAT " RD Data T:%d S:[%d]" NLP,
                    PCX,
                    pDrive->track,
                    pDrive->sector));

#ifdef USE_VGI
                sec_offset = (pDrive->track * MFDC_SECTOR_LEN * 16) + \
                             (pDrive->sector * MFDC_SECTOR_LEN);
#else
                sec_offset = (pDrive->track * 4096) + \
                             (pDrive->sector * 256);
#endif /* USE_VGI */

                if (!(pDrive->uptr->flags & UNIT_ATT)) {
                    if (pDrive->uptr->flags & UNIT_MFDC_VERBOSE)
                        printf("MFDC: " ADDRESS_FORMAT " MDSK%i not attached." NLP, PCX,
                            mfdc_info->sel_drive);
                    return 0x00;
                }

                switch((pDrive->uptr)->u3)
                {
                    case IMAGE_TYPE_IMD:
                        if(pDrive->imd == NULL) {
                            printf(".imd is NULL!" NLP);
                        }
/*                      printf("%s: Read: imd=%p" NLP, __FUNCTION__, pDrive->imd); */
                        sectRead(pDrive->imd,
                            pDrive->track,
                            mfdc_info->head,
                            pDrive->sector,
                            sdata.u.data,
                            256,
                            &flags,
                            &readlen);
                        break;
                    case IMAGE_TYPE_DSK:
                        if(pDrive->uptr->fileref == NULL) {
                            printf(".fileref is NULL!" NLP);
                        } else {
                            sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET);
#ifdef USE_VGI
                            fread(sdata.raw, MFDC_SECTOR_LEN, 1, (pDrive->uptr)->fileref);
#else
                            fread(sdata.u.data, 256, 1, (pDrive->uptr)->fileref);
#endif /* USE_VGI */
                        }
                        break;
                    case IMAGE_TYPE_CPT:
                        printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                        break;
                    default:
                        printf("%s: Unknown image Format" NLP, __FUNCTION__);
                        break;
                }

/*              printf("%d/%d @%04x Len=%04x" NLP, sdata.u.header[0], sdata.u.header[1], sdata.u.header[9]<<8|sdata.u.header[8], sdata.u.header[11]<<8|sdata.u.header[10]); */

                adc(0,0); /* clear Carry bit */
                checksum = 0;

                /* Checksum everything except the sync byte */
                for(i=1;i<269;i++) {
                    checksum = adc(checksum, sdata.raw[i]);
                }

                sdata.u.checksum = checksum & 0xFF;
/*              DBG_PRINT(("Checksum=%x" NLP, sdata.u.checksum)); */
                mfdc_info->read_in_progress = TRUE;
            }

            cData = sdata.raw[mfdc_info->datacount];

            mfdc_info->datacount++;
            if(mfdc_info->datacount == 270) {
                TRACE_PRINT(RD_DATA_MSG, ("MFDC: " ADDRESS_FORMAT " Read sector [%d] complete" NLP,
                    PCX, pDrive->sector));
                mfdc_info->read_in_progress = FALSE;
            }

/*          DBG_PRINT(("MFDC: " ADDRESS_FORMAT " RD Data Sector %d[%03d]: 0x%02x" NLP, PCX, pDrive->sector, mfdc_info->datacount, cData)); */
            break;
    }

    return (cData);
}

static uint8 MFDC_Write(const uint32 Addr, uint8 cData)
{
    unsigned int sec_offset;
    unsigned int flags = 0;
    unsigned int writelen;
    MFDC_DRIVE_INFO *pDrive;

    pDrive = &mfdc_info->drive[mfdc_info->sel_drive];

    switch(Addr & 0x3) {
        case 0:
        case 1:
            MFDC_Command(cData);
            break;
        case 2:
        case 3:
/*          DBG_PRINT(("MFDC: " ADDRESS_FORMAT " WR Data" NLP, PCX)); */
            if(mfdc_info->wr_latch == 0) {
                printf("MFDC: " ADDRESS_FORMAT " Error, attempt to write data when write latch is not set." NLP, PCX);
            } else {
#ifdef USE_VGI
                sec_offset = (pDrive->track * MFDC_SECTOR_LEN * 16) + \
                             (pDrive->sector * MFDC_SECTOR_LEN);

                sdata.raw[mfdc_info->datacount] = cData;
#else
                int data_index = mfdc_info->datacount - 13;

                sec_offset = (pDrive->track * 4096) + \
                             (pDrive->sector * 256);

                if((data_index >= 0) && (data_index < 256)) {
                    DBG_PRINT(("writing data [%03d]=%02x" NLP, data_index, cData));

                    sdata.u.data[data_index] = cData;

                }

#endif /* USE_VGI */

                mfdc_info->datacount ++;

                if(mfdc_info->datacount == 270) {
                    TRACE_PRINT(WR_DATA_MSG, ("MFDC: " ADDRESS_FORMAT " WR Data T:%d S:[%d]" NLP,
                        PCX,
                        pDrive->track,
                        pDrive->sector));

                    if (!(pDrive->uptr->flags & UNIT_ATT)) {
                        if (pDrive->uptr->flags & UNIT_MFDC_VERBOSE)
                            printf("MFDC: " ADDRESS_FORMAT " MDSK%i not attached." NLP, PCX,
                                mfdc_info->sel_drive);
                        return 0x00;
                    }

                    switch((pDrive->uptr)->u3)
                    {
                        case IMAGE_TYPE_IMD:
                            if(pDrive->imd == NULL) {
                                printf(".imd is NULL!" NLP);
                            }
                            sectWrite(pDrive->imd,
                                pDrive->track,
                                mfdc_info->head,
                                pDrive->sector,
                                sdata.u.data,
                                256,
                                &flags,
                                &writelen);
                            break;
                        case IMAGE_TYPE_DSK:
                            if(pDrive->uptr->fileref == NULL) {
                                printf(".fileref is NULL!" NLP);
                            } else {
                                sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET);
#ifdef USE_VGI
                                fwrite(sdata.raw, MFDC_SECTOR_LEN, 1, (pDrive->uptr)->fileref);
#else
                                fwrite(sdata.u.data, 256, 1, (pDrive->uptr)->fileref);
#endif /* USE_VGI */
                            }
                            break;
                        case IMAGE_TYPE_CPT:
                            printf("%s: CPT Format not supported" NLP, __FUNCTION__);
                            break;
                        default:
                            printf("%s: Unknown image Format" NLP, __FUNCTION__);
                            break;
                    }
                }
            }
            break;
    }

    cData = 0x00;

    return (cData);
}

#define MFDC_CMD_NOP        0
#define MFDC_CMD_SELECT     1
#define MFDC_CMD_INTR       2
#define MFDC_CMD_STEP       3
#define MFDC_CMD_SET_WRITE  4
#define MFDC_CMD_RESET      5

static void MFDC_Command(uint8 cData)
{
    uint8 cCommand;
    uint8 cModifier;
    MFDC_DRIVE_INFO *pDrive;

    pDrive = &mfdc_info->drive[mfdc_info->sel_drive];


    cCommand = cData >> 5;
    cModifier = cData & 0x1F;

    switch(cCommand) {
        case MFDC_CMD_NOP:
            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " No Op." NLP, PCX));
            break;
        case MFDC_CMD_SELECT:
            mfdc_info->sel_drive = cModifier & 0x03;
            mfdc_info->head = (cModifier & 0x10) >> 4;
            mfdc_info->selected = TRUE;

            if(pDrive->uptr->fileref != NULL) {
                pDrive->ready = 1;
            } else {
                pDrive->ready = 0;
            }

            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " Select Drive: %d, Head: %s" NLP,
                PCX, mfdc_info->sel_drive, (mfdc_info->head) ? "Upper" : "Lower"));
            break;
        case MFDC_CMD_INTR:
            mfdc_info->int_enable = cModifier & 1;  /* 0=int disable, 1=enable */
            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " Interrupts %s." NLP,
                PCX, mfdc_info->int_enable ? "Enabled" : "Disabled"));
            break;
        case MFDC_CMD_STEP:
            if(cModifier & 1) { /* Step IN */
                pDrive->track++;
            }
            else { /* Step OUT */
                if(pDrive->track != 0) {
                    pDrive->track--;
                }
            }

            TRACE_PRINT(SEEK_MSG, ("MFDC: " ADDRESS_FORMAT " Step %s, Track=%d." NLP,
                PCX, (cModifier & 1) ? "IN" : "OUT", pDrive->track));

            break;
        case MFDC_CMD_SET_WRITE:
            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " Set WRITE." NLP, PCX));
            mfdc_info->wr_latch = 1;    /* Allow writes for the current sector */
            mfdc_info->datacount = 0;   /* reset the byte counter */
            break;
        case MFDC_CMD_RESET:
            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " Reset Controller." NLP, PCX));
            mfdc_info->selected = 0;    /* de-select the drive */
            mfdc_info->wr_latch = 0;    /* Disable the write latch */
            mfdc_info->datacount = 0;   /* reset the byte counter */
            break;
        default:
            TRACE_PRINT(CMD_MSG, ("MFDC: " ADDRESS_FORMAT " Unsupported command." NLP, PCX));
            break;
    }
}
