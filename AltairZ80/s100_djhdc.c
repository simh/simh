/*************************************************************************
 *                                                                       *
 * Copyright (c) 2022 Howard M. Harte.                                   *
 * https://github.com/hharte                                             *
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-            *
 * INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE   *
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN       *
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN     *
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE      *
 * SOFTWARE.                                                             *
 *                                                                       *
 * Except as contained in this notice, the names of The Authors shall    *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * from the Authors.                                                     *
 *                                                                       *
 * Based on s100_disk3.c                                                 *
 *                                                                       *
 * Module Description:                                                   *
 *     Morrow Disk Jockey HDC-DMA Hard Disk Controller module for SIMH.  *
 * Reference:                                                            *
 * http://www.bitsavers.org/pdf/morrow/boards/HDC_DMA_Technical_Manual_1983.pdf *
 *                                                                       *
 *************************************************************************/

#include "altairz80_defs.h"
#include "sim_imd.h"

#define DEV_NAME    "DJHDC"

#define DJHDC_MAX_CYLS            1024
#define DJHDC_MAX_HEADS           8
#define DJHDC_MAX_SPT             256

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define SEEK_MSG    (1 << 1)
#define OPCODE_MSG  (1 << 2)
#define RD_DATA_MSG (1 << 3)
#define WR_DATA_MSG (1 << 4)
#define IRQ_MSG     (1 << 5)
#define VERBOSE_MSG (1 << 6)
#define FORMAT_MSG  (1 << 7)

#define DJHDC_MAX_DRIVES    4

/* DJHDC I/O Ports */
#define DJHDC_RESET     0   /* Reset */
#define DJHDC_START     1   /* Start */

#define DJHDC_LINK_ADDR 0x000050    /* Default link address in RAM */

#define DJHDC_STEP_DIR  0x10        /* Bit 4, Step OUT */

#define DJHDC_IRQ_EN_MASK   0x80    /* Interrupt enable mask */

#define DJHDC_OPCODE_READ_DATA      0x00
#define DJHDC_OPCODE_WRITE_DATA     0x01
#define DJHDC_OPCODE_READ_HEADER    0x02
#define DJHDC_OPCODE_FORMAT_TRACK   0x03
#define DJHDC_OPCODE_LOAD_CONSTANTS 0x04
#define DJHDC_OPCODE_SENSE_STATUS   0x05
#define DJHDC_OPCODE_NOOP           0x06

#define DJHDC_STATUS_BUSY               0x00    /* Busy */
#define DJHDC_STATUS_NOT_READY          0x01    /* Drive not ready */
#define DJHDC_STATUS_HEADER_NOT_FOUND   0x04    /* Sector header not found */
#define DJHDC_STATUS_DATA_NOT_FOUND     0x05    /* Sector data not found */
#define DJHDC_STATUS_DATA_OVERRUN       0x06    /* Data overrun(channel error) */
#define DJHDC_STATUS_DATA_CRC_ERROR     0x07    /* Data CRC error */
#define DJHDC_STATUS_WRITE_FAULT        0x08    /* Write fault */
#define DJHDC_STATUS_HEADER_CRC_ERROR   0x09    /* Sector header CRC error */
#define DJHDC_STATUS_ILLEGAL_COMMAND    0xA0    /* Illegal command */
#define DJHDC_STATUS_COMPLETE           0xFF    /* Successful completion */

#define DJHDC_TRACK_0_DETECT            (1 << 0)    /* 0 = track 0. */
#define DJHDC_WRITE_FAULT_SIGNAL        (1 << 1)    /* Drive Write fault (0). */
#define DJHDC_DRIVE_READY_SIGNAL        (1 << 2)    /* Drive is up to speed (0). */

#define DJHDC_OPCODE_MASK       0x07

#define DJHDC_IOPB_LEN  16

#define DJHDC_IOPB_SELDRV   0
#define DJHDC_IOPB_STEP_L   1
#define DJHDC_IOPB_STEP_H   2
#define DJHDC_IOPB_SEL_HD   3
#define DJHDC_IOPB_DMA_L    4
#define DJHDC_IOPB_DMA_H    5
#define DJHDC_IOPB_DMA_E    6
#define DJHDC_IOPB_ARG0     7
#define DJHDC_IOPB_ARG1     8
#define DJHDC_IOPB_ARG2     9
#define DJHDC_IOPB_ARG3     10
#define DJHDC_IOPB_OPCODE   11
#define DJHDC_IOPB_STATUS   12
#define DJHDC_IOPB_LINK     13
#define DJHDC_IOPB_LINK_H   14
#define DJHDC_IOPB_LINK_E   15

#define DJHDC_INT         1   /* DJHDC interrupts tied to VI1 */

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
    uint16 cur_cyl;     /* Current Track */
    uint16 cur_head;    /* Number of sectors to transfer */
    uint16 cur_sectsize;/* Current sector size */
    uint8 ready;        /* Is drive ready? */
} DJHDC_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   mode;       /* mode (0xFF=absolute, 0x00=logical) */
    uint8   ndrives;    /* Number of drives attached to the controller */

    uint32  link_addr;  /* Link Address for next IOPB */
    uint32  dma_addr;   /* DMA Address for the current IOPB */

    uint16  steps;      /* Step count */
    uint8   step_dir;   /* Step direction, 1 = out. */
    uint8   irq_enable;
    uint8   step_delay;
    uint8   head_settle_time;
    uint8   sector_size_code;

    DJHDC_DRIVE_INFO drive[DJHDC_MAX_DRIVES];
    uint8   iopb[16];
} DJHDC_INFO;

static DJHDC_INFO djhdc_info_data = { { 0x0, 0, 0x54, 2 } };
static DJHDC_INFO *djhdc_info = &djhdc_info_data;

/* Disk geometries:
 *            IMI	    SCRIBE
 *  Sectsize: 1024      1024
 *   Sectors: 8         8
 *     Heads: 6         4
 *    Tracks: 306       480
 */

/* Default geometry for a 15MB hard disk. */
#define SCRIBE_SECTSIZE  1024
#define SCRIBE_NSECTORS  8
#define SCRIBE_NHEADS    4
#define SCRIBE_NTRACKS   480

static const char* djhdc_opcode_str[] = {
    "Read Data     ",
    "Write Data    ",
    "Read Header   ",
    "Format Track  ",
    "Load Constants",
    "Sense Status  ",
    "No Operation  ",
    "Invalid       "
};

static int32 ntracks      = SCRIBE_NTRACKS;
static int32 nheads       = SCRIBE_NHEADS;
static int32 nsectors     = SCRIBE_NSECTORS;
static int32 sectsize     = SCRIBE_SECTSIZE;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);
extern void raise_scp300f_interrupt(uint8 intnum);

/* These are needed for DMA. */
extern void PutByteDMA(const uint32 Addr, const uint32 Value);
extern uint8 GetByteDMA(const uint32 Addr);

#define UNIT_V_DJHDC_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_DJHDC_VERBOSE      (1 << UNIT_V_DJHDC_VERBOSE)
#define DJHDC_CAPACITY          (SCRIBE_NTRACKS * SCRIBE_NHEADS * \
                                 SCRIBE_NSECTORS * SCRIBE_SECTSIZE)   /* Default Disk Capacity */

static t_stat djhdc_reset(DEVICE *djhdc_dev);
static t_stat djhdc_attach(UNIT *uptr, CONST char *cptr);
static t_stat djhdc_detach(UNIT *uptr);
static t_stat djhdc_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc);
static t_stat djhdc_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc);
static int DJHDC_Validate_CHSN(DJHDC_DRIVE_INFO* pDrive);
#ifdef DJHDC_INTERRUPTS
static void raise_djhdc_interrupt(void);
#endif /* DJHDC_INTERRUPTS */

static const char* djhdc_description(DEVICE *dptr);

static int32 djhdcdev(const int32 port, const int32 io, const int32 data);

/* static uint8 DJHDC_Read(const uint32 Addr); */
static uint8 DJHDC_Write(const uint32 Addr, uint8 cData);

static UNIT djhdc_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DJHDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DJHDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DJHDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, DJHDC_CAPACITY) }
};

static REG djhdc_reg[] = {
    { DRDATAD (NTRACKS,    ntracks,                     10,
               "Number of tracks"),                             },
    { DRDATAD (NHEADS,     nheads,                      8,
               "Number of heads"),                              },
    { DRDATAD (NSECTORS,   nsectors,                    8,
               "Number of sectors per track"),                  },
    { DRDATAD (SECTSIZE,   sectsize,                    11,
               "Sector size not including pre/postamble"),      },
    { HRDATAD (SEL_DRIVE,  djhdc_info_data.sel_drive,   3,
               "Currently selected drive"),                     },
    { HRDATAD (MODE,       djhdc_info_data.mode,        8,
               "Mode (0xFF=absolute, 0x00=logical)"),           },
    { HRDATAD (NDRIVES,    djhdc_info_data.ndrives,     8,
               "Number of drives attached to the controller"),  },
    { HRDATAD (LINK_ADDR,  djhdc_info_data.link_addr,   32,
               "Link address for next IOPB"),                   },
    { HRDATAD (DMA_ADDR,   djhdc_info_data.dma_addr,    32,
               "DMA address for the current IOPB"),             },
    { BRDATAD (IOPB,       djhdc_info_data.iopb,       16, 8, 16,
               "IOPB command register"),                        },
    { NULL }
};

#define DJHDC_NAME  "Morrow HDC/DMA Hard Disk Controller"

static const char* djhdc_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }
    return DJHDC_NAME;
}

static MTAB djhdc_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { MTAB_XTD | MTAB_VUN | MTAB_VALR,    0,                  "GEOMETRY",     "GEOMETRY",
        &djhdc_unit_set_geometry, &djhdc_unit_show_geometry, NULL,
        "Set disk geometry C:nnnn/H:n/S:nnn/N:nnnn" },
    { 0 }
};

/* Debug Flags */
static DEBTAB djhdc_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "OPCODE",     OPCODE_MSG,     "Opcode messages"   },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "FORMAT",     FORMAT_MSG,     "Format messages"  },
    { NULL,         0                                   }
};

DEVICE djhdc_dev = {
    DEV_NAME, djhdc_unit, djhdc_reg, djhdc_mod,
    DJHDC_MAX_DRIVES, 10, 31, 1, DJHDC_MAX_DRIVES, DJHDC_MAX_DRIVES,
    NULL, NULL, &djhdc_reset,
    NULL, &djhdc_attach, &djhdc_detach,
    &djhdc_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    djhdc_dt, NULL, NULL, NULL, NULL, NULL, &djhdc_description
};

/* Reset routine */
static t_stat djhdc_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &djhdcdev, "djhdcdev", TRUE);
    } else {
        /* Connect DJHDC at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &djhdcdev, "djhdcdev", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    djhdc_info->link_addr = DJHDC_LINK_ADDR;   /* After RESET, the link pointer is at 0x000050. */

    return SCPE_OK;
}


/* Attach routine */
static t_stat djhdc_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r = SCPE_OK;
    DJHDC_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &djhdc_info->drive[i];

    pDrive->ready = 1;
    pDrive->track = 5;

    if (pDrive->ntracks == 0) {
        /* If geometry was not specified, default to Miniscribe 15MB */
        pDrive->ntracks = SCRIBE_NTRACKS;
        pDrive->nheads = SCRIBE_NHEADS;
        pDrive->nsectors = SCRIBE_NSECTORS;
        pDrive->sectsize = SCRIBE_SECTSIZE;
    }

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
            djhdc_detach(uptr);
            return r;
        }
    }

    if (uptr->flags & UNIT_DJHDC_VERBOSE)
        sim_printf("DJHDC%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if(uptr->capac < 318000) {
            sim_printf("Cannot create IMD files with SIMH.\nCopy an existing file and format it with CP/M.\n");
            djhdc_detach(uptr);
            return SCPE_OPENERR;
        }

        if (uptr->flags & UNIT_DJHDC_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        djhdc_info->drive[i].imd = diskOpenEx((uptr->fileref), (uptr->flags & UNIT_DJHDC_VERBOSE),
                                              &djhdc_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_DJHDC_VERBOSE)
            sim_printf("\n");
    } else {
        djhdc_info->drive[i].imd = NULL;
    }

    return SCPE_OK;
}


/* Detach routine */
static t_stat djhdc_detach(UNIT *uptr)
{
    DJHDC_DRIVE_INFO *pDrive;
    t_stat r;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &djhdc_info->drive[i];

    pDrive->ready = 0;

    if (uptr->flags & UNIT_DJHDC_VERBOSE)
        sim_printf("Detach DJHDC%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}

/* Set geometry of the disk drive */
static t_stat djhdc_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    DJHDC_DRIVE_INFO* pDrive;
    int32 i;
    int32 result;
    uint16 newCyls, newHeads, newSPT, newSecLen;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &djhdc_info->drive[i];

    if (cptr == NULL)
        return SCPE_ARG;

    result = sscanf(cptr, "C:%hd/H:%hd/S:%hd/N:%hd", &newCyls, &newHeads, &newSPT, &newSecLen);
    if (result != 4)
        return SCPE_ARG;

    /* Validate Cyl, Heads, Sector, Length are valid for the HDC-1001 */
    if (newCyls < 1 || newCyls > DJHDC_MAX_CYLS) {
        sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "%d: Number of cylinders must be 1-%d.\n",
            djhdc_info->sel_drive, DJHDC_MAX_CYLS);
        return SCPE_ARG;
    }
    if (newHeads < 1 || newHeads > DJHDC_MAX_HEADS) {
        sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "%d: Number of heads must be 1-%d.\n",
            djhdc_info->sel_drive, DJHDC_MAX_HEADS);
        return SCPE_ARG;
    }
    if (newSPT < 1 || newSPT > DJHDC_MAX_SPT) {
        sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "%d: Number of sectors per track must be 1-%d.\n",
            djhdc_info->sel_drive, DJHDC_MAX_SPT);
        return SCPE_ARG;
    }
    if (newSecLen != 2048 && newSecLen != 1024 && newSecLen != 512 && newSecLen != 256 && newSecLen != 128) {
        sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "%d: Sector length must be 128, 256, 512, 1024, or 2048.\n",
            djhdc_info->sel_drive);
        return SCPE_ARG;
    }

    pDrive->ntracks = newCyls;
    pDrive->nheads = newHeads;
    pDrive->nsectors = newSPT;
    pDrive->sectsize = newSecLen;

    return SCPE_OK;
}

/* Show geometry of the disk drive */
static t_stat djhdc_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc)
{
    DJHDC_DRIVE_INFO* pDrive;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &djhdc_info->drive[i];

    fprintf(st, "C:%d/H:%d/S:%d/N:%d",
        pDrive->ntracks, pDrive->nheads, pDrive->nsectors, pDrive->sectsize);

    return SCPE_OK;
}

static int32 djhdcdev(const int32 port, const int32 io, const int32 data)
{
    sim_debug(VERBOSE_MSG, &djhdc_dev, DEV_NAME ": " ADDRESS_FORMAT
              " IO %s, Port %02x\n", PCX, io ? "WR" : "RD", port);
    if(io) {
        DJHDC_Write(port, (uint8)data);
        return 0;
    } else {
        return(0xFF);
    }
}

static uint8 DJHDC_Write(const uint32 Addr, uint8 cData)
{
    uint32 next_link;
    uint8 result = DJHDC_STATUS_COMPLETE;
    uint8 i;
    uint8 opcode;

    DJHDC_DRIVE_INFO *pDrive;

    /* RESET */
    if ((Addr & 1) == DJHDC_RESET) {
        djhdc_info->link_addr = DJHDC_LINK_ADDR - DJHDC_IOPB_LINK;

        sim_debug(VERBOSE_MSG, &djhdc_dev, DEV_NAME "[%d]: RESET\n",
            djhdc_info->sel_drive);

        return 0;
    }

    /* START */

    /* Read first three bytes of IOPB (link address) */
    for (i = DJHDC_IOPB_LINK; i < DJHDC_IOPB_LEN; i++) {
        djhdc_info->iopb[i] = GetByteDMA((djhdc_info->link_addr) + i);
    }

    next_link  = djhdc_info->iopb[DJHDC_IOPB_LINK + 0];
    next_link |= djhdc_info->iopb[DJHDC_IOPB_LINK+1] << 8;
    next_link |= djhdc_info->iopb[DJHDC_IOPB_LINK+2] << 16;

    /* Point IOPB to new link */
    djhdc_info->link_addr = next_link;

    /* Read remaineder of IOPB */
    for(i = 0; i < DJHDC_IOPB_LEN-3; i++) {
        djhdc_info->iopb[i] = GetByteDMA((djhdc_info->link_addr) + i);
    }

    /* Process the IOPB */
    djhdc_info->iopb[DJHDC_IOPB_OPCODE] = djhdc_info->iopb[DJHDC_IOPB_OPCODE] & DJHDC_OPCODE_MASK;

    opcode = djhdc_info->iopb[DJHDC_IOPB_OPCODE];
    djhdc_info->sel_drive = djhdc_info->iopb[DJHDC_IOPB_SELDRV] & 0x03;
    djhdc_info->step_dir = (djhdc_info->iopb[DJHDC_IOPB_SELDRV] & DJHDC_STEP_DIR) ? 1 : 0;

    djhdc_info->steps  = djhdc_info->iopb[DJHDC_IOPB_STEP_L];
    djhdc_info->steps |= djhdc_info->iopb[DJHDC_IOPB_STEP_H] << 8;

    djhdc_info->dma_addr  = djhdc_info->iopb[DJHDC_IOPB_DMA_L];
    djhdc_info->dma_addr |= djhdc_info->iopb[DJHDC_IOPB_DMA_H] << 8;
    djhdc_info->dma_addr |= djhdc_info->iopb[DJHDC_IOPB_DMA_E] << 16;

    sim_debug(VERBOSE_MSG, &djhdc_dev, DEV_NAME "[%d]: SEEK=%d %s, LINK=0x%05x, OPCODE=%x, %s DMA@0x%05x\n",
              djhdc_info->sel_drive,
              djhdc_info->steps,
              djhdc_info->step_dir ? "OUT" : "IN",
              djhdc_info->link_addr,
              djhdc_info->iopb[DJHDC_IOPB_OPCODE],
              djhdc_opcode_str[djhdc_info->iopb[DJHDC_IOPB_OPCODE]],
              djhdc_info->dma_addr);

    pDrive = &djhdc_info->drive[djhdc_info->sel_drive];

    if(pDrive->ready) {
        /* Seek phase */
        if (djhdc_info->step_dir) {
            /* Step Out */
            if (djhdc_info->steps >= pDrive->cur_cyl) {
                pDrive->cur_cyl = 0;
                sim_debug(SEEK_MSG, &djhdc_dev, DEV_NAME "[%d]: HOME\n",
                    djhdc_info->sel_drive);
            }
            else {
                pDrive->cur_cyl -= djhdc_info->steps;
            }
        }
        else {
            /* Step In */
            pDrive->cur_cyl += djhdc_info->steps;
        }

        sim_debug(SEEK_MSG, &djhdc_dev, DEV_NAME "[%d]: Current track: %d\n",
            djhdc_info->sel_drive,
            pDrive->cur_cyl);


        /* Perform command */
        switch(opcode) {
            case DJHDC_OPCODE_READ_DATA:
            case DJHDC_OPCODE_WRITE_DATA:
            {
                uint32 track_len;
                uint32 xfr_len;
                uint32 file_offset;
                uint32 xfr_count = 0;
                uint8* dataBuffer;
                size_t rtn;

                pDrive->cur_cyl = djhdc_info->iopb[DJHDC_IOPB_ARG0] | (djhdc_info->iopb[DJHDC_IOPB_ARG1] << 8);
                pDrive->cur_head = djhdc_info->iopb[DJHDC_IOPB_ARG2];
                pDrive->cur_sect = djhdc_info->iopb[DJHDC_IOPB_ARG3] - 1;

                if (DJHDC_Validate_CHSN(pDrive) != SCPE_OK) {
                    result = DJHDC_STATUS_HEADER_NOT_FOUND;
                    break;
                }

                track_len = pDrive->nsectors * pDrive->nheads * pDrive->sectsize;

                file_offset  = (pDrive->cur_cyl * track_len); /* Calculate offset based on current track */
                file_offset += pDrive->nsectors * pDrive->cur_head * pDrive->sectsize;
                file_offset += pDrive->cur_sect * pDrive->sectsize;

                xfr_len = pDrive->sectsize;

                dataBuffer = (uint8*)malloc(xfr_len);
                if (dataBuffer == NULL) {
                    sim_printf("%s: error allocating memory\n", __FUNCTION__);
                    return (0);
                }

                if (sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET) == 0) {

                    if (opcode == DJHDC_OPCODE_READ_DATA) { /* Read */
                        rtn = sim_fread(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);

                        sim_debug(RD_DATA_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                            "  READ @0x%05x C:%04d/H:%d/S:%04d len=%d, file_offset=%d, %s\n",
                            djhdc_info->sel_drive,
                            PCX,
                            djhdc_info->dma_addr,
                            pDrive->cur_cyl,
                            pDrive->cur_head,
                            pDrive->cur_sect,
                            xfr_len,
                            file_offset,
                            rtn == (size_t)xfr_len ? "OK" : "NOK");


                        /* Perform DMA Transfer */
                        for (xfr_count = 0; xfr_count < xfr_len; xfr_count++) {
                            PutByteDMA(djhdc_info->dma_addr + xfr_count, dataBuffer[xfr_count]);
                        }
                    }
                    else { /* Write */
                        sim_debug(WR_DATA_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                            " WRITE @0x%05x C:%04d/H:%d/S:%04d file_offset=%d, len=%d\n",
                            djhdc_info->sel_drive,
                            PCX, djhdc_info->dma_addr,
                            pDrive->cur_cyl,
                            pDrive->cur_head,
                            pDrive->cur_sect,
                            file_offset,
                            xfr_len);

                        /* Perform DMA Transfer */
                        for (xfr_count = 0; xfr_count < xfr_len; xfr_count++) {
                            dataBuffer[xfr_count] = GetByteDMA(djhdc_info->dma_addr + xfr_count);
                        }

                        sim_fwrite(dataBuffer, 1, xfr_len, (pDrive->uptr)->fileref);
                    }
                }
                else {
                    sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT " READWRITE: sim_fseek error.\n", djhdc_info->sel_drive, PCX);
                }

                free(dataBuffer);

                break;
            }
            case DJHDC_OPCODE_READ_HEADER:
                sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT " READ_HEADER: not implemented.\n", djhdc_info->sel_drive, PCX);
                result = DJHDC_STATUS_HEADER_NOT_FOUND;
                break;
            case DJHDC_OPCODE_FORMAT_TRACK:
            {
                uint32  track_len;
                uint32  file_offset;
                uint8*  fmtBuffer;
                uint8   head;
                uint8   gap;
                uint8   sector_count;
                uint8   sector_size_code;
                uint8   fill_byte;

                head = ~(djhdc_info->iopb[DJHDC_IOPB_SEL_HD] >> 2) & 7;
                gap = djhdc_info->iopb[DJHDC_IOPB_ARG0];
                sector_count = 255 - djhdc_info->iopb[DJHDC_IOPB_ARG1];
                sector_size_code = djhdc_info->iopb[DJHDC_IOPB_ARG2];
                fill_byte = djhdc_info->iopb[DJHDC_IOPB_ARG3];

                switch (sector_size_code) {
                case 0xFF:
                    pDrive->cur_sectsize = 128;
                    break;
                case 0xFE:
                    pDrive->cur_sectsize = 256;
                    break;
                case 0xFC:
                    pDrive->cur_sectsize = 512;
                    break;
                case 0xF8:
                    pDrive->cur_sectsize = 1024;
                    break;
                case 0xF0:
                    pDrive->cur_sectsize = 2048;
                    break;
                default:
                    sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME ": Invalid sector size code: 0x%02x.\n",
                        djhdc_info->sector_size_code);
                    pDrive->cur_sectsize = 0;
                    result = DJHDC_STATUS_ILLEGAL_COMMAND;
                    break;
                }

                if (DJHDC_Validate_CHSN(pDrive) != SCPE_OK) {
                    result = DJHDC_STATUS_HEADER_NOT_FOUND;
                    break;
                }

                track_len = pDrive->nheads * sector_count * pDrive->sectsize;

                file_offset  = pDrive->cur_cyl * track_len; /* Calculate offset based on current track */
                file_offset += head * sector_count * pDrive->sectsize;

                sim_debug(FORMAT_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                    " FORMAT C:%d/H:%d, Gap=%d, Fill=0x%02x, Count=%d, Sector Size:=%d, file offset: 0x%08x\n",
                    djhdc_info->sel_drive,
                    PCX,
                    pDrive->cur_cyl,
                    head,
                    gap,
                    fill_byte,
                    sector_count,
                    pDrive->sectsize,
                    file_offset);

                fmtBuffer = (uint8*)malloc(track_len);

                if (fmtBuffer == NULL) {
                    sim_printf("%s: error allocating memory\n", __FUNCTION__);
                    return (0);
                }

                memset(fmtBuffer, fill_byte, track_len);

                if (sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET) == 0) {
                    sim_fwrite(fmtBuffer, 1, track_len, (pDrive->uptr)->fileref);
                }
                else {
                    sim_debug(WR_DATA_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT " FORMAT: sim_fseek error.\n", djhdc_info->sel_drive, PCX);
                    result = DJHDC_STATUS_WRITE_FAULT;
                }

                free(fmtBuffer);

                break;
            }
        case DJHDC_OPCODE_LOAD_CONSTANTS:
                djhdc_info->irq_enable = (djhdc_info->iopb[DJHDC_IOPB_ARG1] & DJHDC_IRQ_EN_MASK) ? 1 : 0;
                djhdc_info->step_delay = djhdc_info->iopb[DJHDC_IOPB_ARG1] & ~DJHDC_IRQ_EN_MASK;
                djhdc_info->head_settle_time = djhdc_info->iopb[DJHDC_IOPB_ARG2];
                djhdc_info->sector_size_code = djhdc_info->iopb[DJHDC_IOPB_ARG3];

                switch (djhdc_info->sector_size_code) {
                case 0x00:
                    pDrive->cur_sectsize = 128;
                    break;
                case 0x01:
                    pDrive->cur_sectsize = 256;
                    break;
                case 0x03:
                    pDrive->cur_sectsize = 512;
                    break;
                case 0x07:
                    pDrive->cur_sectsize = 1024;
                    break;
                case 0x0F:
                    pDrive->cur_sectsize = 2048;
                    break;
                default:
                    sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME ": Invalid sector size code: 0x%02x.\n",
                        djhdc_info->sector_size_code);
                    pDrive->cur_sectsize = 0;
                    result = DJHDC_STATUS_ILLEGAL_COMMAND;
                    break;
                }

                sim_debug(VERBOSE_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                    " Load Constants: Interrupt Enable: %d, step delay: %d, head settle time: %d, sector size %d (code: 0x%02x)\n",
                    djhdc_info->sel_drive, PCX,
                    djhdc_info->irq_enable,
                    djhdc_info->step_delay,
                    djhdc_info->head_settle_time,
                    pDrive->sectsize,
                    djhdc_info->sector_size_code);
                break;
        case DJHDC_OPCODE_SENSE_STATUS:
            sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT " SENSE_STATUS: not implemented.\n", djhdc_info->sel_drive, PCX);
            result = DJHDC_DRIVE_READY_SIGNAL;
            if (pDrive->cur_cyl != 0) result = DJHDC_TRACK_0_DETECT;
            break;
        case DJHDC_OPCODE_NOOP:
            sim_debug(VERBOSE_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                        " NOOP\n", djhdc_info->sel_drive, PCX);
            break;

        default:
            sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "[%d]: " ADDRESS_FORMAT
                        " OPCODE=%x Unsupported\n",
                        djhdc_info->sel_drive,
                        PCX,
                        opcode & DJHDC_OPCODE_MASK);
            result = DJHDC_STATUS_ILLEGAL_COMMAND;
            break;
        }
    } else { /* Drive not ready */
        result = DJHDC_STATUS_NOT_READY;
    }
    /* Return status */
    djhdc_info->iopb[DJHDC_IOPB_STATUS] = result;

    /* Update IOPB in host memory */
    PutByteDMA(djhdc_info->link_addr + DJHDC_IOPB_STATUS, djhdc_info->iopb[DJHDC_IOPB_STATUS]);

#ifdef DJHDC_INTERRUPTS
    if(djhdc_info->irq_enable) {
        raise_djhdc_interrupt();
    }
#endif /* DJHDC_INTERRUPTS */

    return 0;
}

/* Validate that Cyl, Head, Sector, Sector Length are valid for the current
 * disk drive geometry.
 */
static int DJHDC_Validate_CHSN(DJHDC_DRIVE_INFO* pDrive)
{
    int status = SCPE_OK;

    /* Check to make sure we're operating on a valid C/H/S/N. */
    if ((pDrive->cur_cyl >= pDrive->ntracks) ||
        (pDrive->cur_head >= pDrive->nheads) ||
        (pDrive->cur_sect >= pDrive->nsectors) ||
        (pDrive->cur_sectsize != pDrive->sectsize))
    {

        sim_debug(ERROR_MSG, &djhdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " ID Not Found (check disk geometry.)\n", djhdc_info->sel_drive, PCX);

        status = SCPE_IOERR;
    }

    return (status);
}

#ifdef DJHDC_INTERRUPTS
static void raise_djhdc_interrupt(void)
{
    sim_debug(IRQ_MSG, &djhdc_dev, DEV_NAME ": " ADDRESS_FORMAT " Interrupt\n", PCX);

    raise_scp300f_interrupt(DJHDC_INT);
}
#endif /* DJHDC_INTERRUPTS */
