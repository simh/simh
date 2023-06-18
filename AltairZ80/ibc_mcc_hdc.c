/*************************************************************************
 *                                                                       *
 * Copyright (c) 2021-2023 Howard M. Harte.                              *
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
 * Module Description:                                                   *
 *     IBC/Integrated Business Computers MCC ST-506 Hard Disk Controller *
 * module for SIMH.                                                      *
 *                                                                       *
 *************************************************************************/

#include "altairz80_defs.h"
#include "sim_imd.h"

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define CMD_MSG     (1 << 1)
#define RD_DATA_MSG (1 << 2)
#define WR_DATA_MSG (1 << 3)
#define FIFO_MSG    (1 << 4)
#define TF_MSG      (1 << 5)
#define VERBOSE_MSG (1 << 6)

#define IBC_HDC_MAX_DRIVES          4       /* Maximum number of drives supported */
#define IBC_HDC_MAX_SECLEN          256     /* Maximum of 256 bytes per sector */
#define IBC_HDC_FORMAT_FILL_BYTE    0xe5    /* Real controller uses 0, but we
                                               choose 0xe5 so the disk shows
                                               up as blank under CP/M. */
#define IBC_HDC_MAX_CYLS            1024
#define IBC_HDC_MAX_HEADS           16
#define IBC_HDC_MAX_SPT             256

#define DEV_NAME    "IBCHDC"

/* Task File Register Offsets */
#define TF_CSEC     0
#define TF_HEAD     1
#define TF_NSEC     2
#define TF_SA3      3
#define TF_CMD      4
#define TF_DRIVE    5
#define TF_TRKL     6
#define TF_TRKH     7
#define TF_FIFO     8

#define IBC_HDC_STATUS_BUSY         (1 << 4)
#define IBC_HDC_STATUS_ERROR        (1 << 0)

#define IBC_HDC_ERROR_ID_NOT_FOUND  (1 << 4)

#define IBC_HDC_CMD_MASK            0x7f
#define IBC_HDC_CMD_RESET           0x00
#define IBC_HDC_CMD_READ_SECT       0x01
#define IBC_HDC_CMD_WRITE_SECT      0x02
#define IBC_HDC_CMD_FORMAT_TRK      0x08
#define IBC_HDC_CMD_ACCESS_FIFO     0x0b
#define IBC_HDC_CMD_READ_PARAMETERS 0x10

#define IBC_HDC_REG_STATUS          0x40
#define IBC_HDC_REG_FIFO_STATUS     0x44
#define IBC_HDC_REG_FIFO            0x48

typedef struct {
    UNIT *uptr;
    uint8  readonly;    /* Drive is read-only? */
    uint16 sectsize;    /* sector size */
    uint16 nsectors;    /* number of sectors/track */
    uint16 nheads;      /* number of heads */
    uint16 ncyls;       /* number of cylinders */
    uint16 cur_cyl;     /* Current cylinder */
    uint8  cur_head;    /* Current Head */
    uint8  cur_sect;    /* current starting sector of transfer */
    uint16 cur_sectsize;/* Current sector size in SA6 register */
    uint16 xfr_nsects;  /* Number of sectors to transfer */
    uint8 ready;        /* Is drive ready? */
} IBC_HDC_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   reg_temp_holding[4];
    uint8   taskfile[9]; /* ATA Task File Registers */
    uint8   status_reg; /* IBC Disk Slave Status Register */
    uint8   error_reg;  /* IBC Disk Slave Error Register */
    uint8   ndrives;    /* Number of drives attached to the controller */
    uint8   sectbuf[IBC_HDC_MAX_SECLEN*10];
    uint16  secbuf_index;
    IBC_HDC_DRIVE_INFO drive[IBC_HDC_MAX_DRIVES];
} IBC_HDC_INFO;

static IBC_HDC_INFO ibc_hdc_info_data = { { 0x0, 0, 0x40, 9 } };
static IBC_HDC_INFO *ibc_hdc_info = &ibc_hdc_info_data;

extern uint32 PCX;
extern int32 HL_S;                                 /* HL register                                  */
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

#define UNIT_V_IBC_HDC_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_IBC_HDC_VERBOSE      (1 << UNIT_V_IBC_HDC_VERBOSE)
#define IBC_HDC_CAPACITY          (512*4*32*256)   /* Default Disk Capacity Quantum 2020 */

static t_stat ibc_hdc_reset(DEVICE *ibc_hdc_dev);
static t_stat ibc_hdc_attach(UNIT *uptr, CONST char *cptr);
static t_stat ibc_hdc_detach(UNIT *uptr);
static t_stat ibc_hdc_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc);
static t_stat ibc_hdc_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc);
static int32 ibchdcdev(const int32 port, const int32 io, const int32 data);

static uint8 IBC_HDC_Read(const uint32 Addr);
static uint8 IBC_HDC_Write(const uint32 Addr, uint8 cData);
static t_stat IBC_HDC_doCommand(void);
static const char* ibc_hdc_description(DEVICE *dptr);

static UNIT ibc_hdc_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_HDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_HDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_HDC_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_HDC_CAPACITY) }
};

static REG ibc_hdc_reg[] = {
    { HRDATAD (TF_ERROR,    ibc_hdc_info_data.error_reg,            8, "Taskfile Error Register"), },
    { HRDATAD (TF_STATUS,   ibc_hdc_info_data.status_reg,           8, "Taskfile Status Register"), },
    { HRDATAD (TF_CSEC,     ibc_hdc_info_data.taskfile[TF_CSEC],    8, "Taskfile Current Sector Register"), },
    { HRDATAD (TF_HEAD,     ibc_hdc_info_data.taskfile[TF_HEAD],    8, "Taskfile Current Head Register"), },
    { HRDATAD (TF_NSEC,     ibc_hdc_info_data.taskfile[TF_NSEC],    8, "Taskfile Sector Count Register"), },
    { HRDATAD (TF_SA3,      ibc_hdc_info_data.taskfile[TF_SA3],     8, "Taskfile SA3 Register"), },
    { HRDATAD (TF_CMD,      ibc_hdc_info_data.taskfile[TF_CMD],     8, "Taskfile Command Register"), },
    { HRDATAD (TF_DRIVE,    ibc_hdc_info_data.taskfile[TF_DRIVE],   8, "Taskfile Drive Register"), },
    { HRDATAD (TF_TRKL,     ibc_hdc_info_data.taskfile[TF_TRKL],    8, "Taskfile Track Low Register"), },
    { HRDATAD (TF_TRKH,     ibc_hdc_info_data.taskfile[TF_TRKH],    8, "Taskfile Track High Register"), },
    { HRDATAD (TF_FIFO,     ibc_hdc_info_data.taskfile[TF_FIFO],    8, "Data FIFO"), },
    { NULL }
};

#define IBC_HDC_NAME    "IBC MCC ST-506 Hard Disk Controller"

static const char* ibc_hdc_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }

    return IBC_HDC_NAME;
}

static MTAB ibc_hdc_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR,    0,                  "GEOMETRY",     "GEOMETRY",
        &ibc_hdc_unit_set_geometry, &ibc_hdc_unit_show_geometry, NULL,
        "Set disk geometry C:nnnn/H:n/S:nnn/N:nnnn" },
    { 0 }
};

/* Debug Flags */
static DEBTAB ibc_hdc_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "FIFO",       FIFO_MSG,       "FIFO messages"     },
    { "TF",         TF_MSG,         "Taskfile messages" },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE ibc_hdc_dev = {
    DEV_NAME, ibc_hdc_unit, ibc_hdc_reg, ibc_hdc_mod,
    IBC_HDC_MAX_DRIVES, 10, 31, 1, IBC_HDC_MAX_DRIVES, IBC_HDC_MAX_DRIVES,
    NULL, NULL, &ibc_hdc_reset,
    NULL, &ibc_hdc_attach, &ibc_hdc_detach,
    &ibc_hdc_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    ibc_hdc_dt, NULL, NULL, NULL, NULL, NULL, &ibc_hdc_description
};

/* Reset routine */
static t_stat ibc_hdc_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ibchdcdev, "ibchdcdev", TRUE);
    } else {
        /* Connect IBC_HDC at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ibchdcdev, "ibchdcdev", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    ibc_hdc_info->status_reg = 0x80;
    ibc_hdc_info->error_reg = 0;
    ibc_hdc_info->sel_drive = 0;
    return SCPE_OK;
}


/* Attach routine */
static t_stat ibc_hdc_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r = SCPE_OK;
    IBC_HDC_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &ibc_hdc_info->drive[i];

    /* Defaults for the Quantum 2020 Drive */
    pDrive->ready = 0;
    if (pDrive->ncyls == 0) {
        /* If geometry was not specified, default to Quantun 2020 */
        pDrive->ncyls = 512;
        pDrive->nheads = 4;
        pDrive->nsectors = 32;
        pDrive->sectsize = 256;
    }

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = (pDrive->ncyls * pDrive->nsectors * pDrive->nheads * pDrive->sectsize);
    }

    pDrive->uptr = uptr;

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        r = assignDiskType(uptr);
        if (r != SCPE_OK) {
            ibc_hdc_detach(uptr);
            return r;
        }
    }

    sim_debug(VERBOSE_MSG, &ibc_hdc_dev, DEV_NAME "%d, attached to '%s', type=DSK, len=%d\n",
        i, cptr, uptr->capac);

    pDrive->readonly = (uptr->flags & UNIT_RO) ? 1 : 0;
    ibc_hdc_info->error_reg = 0;
    pDrive->ready = 1;

    return SCPE_OK;
}


/* Detach routine */
static t_stat ibc_hdc_detach(UNIT *uptr)
{
    IBC_HDC_DRIVE_INFO *pDrive;
    t_stat r;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_hdc_info->drive[i];

    pDrive->ready = 0;

    sim_debug(VERBOSE_MSG, &ibc_hdc_dev, "Detach " DEV_NAME "%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}

/* Set geometry of the disk drive */
static t_stat ibc_hdc_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    IBC_HDC_DRIVE_INFO* pDrive;
    int32 i;
    int32 result;
    uint16 newCyls, newHeads, newSPT, newSecLen;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_hdc_info->drive[i];

    if (cptr == NULL)
        return SCPE_ARG;

    result = sscanf(cptr, "C:%hd/H:%hd/S:%hd/N:%hd", &newCyls, &newHeads, &newSPT, &newSecLen);
    if (result != 4)
        return SCPE_ARG;

    /* Validate Cyl, Heads, Sector, Length */
    if (newCyls < 1 || newCyls > IBC_HDC_MAX_CYLS) {
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: Number of cylinders must be 1-%d.\n",
            ibc_hdc_info->sel_drive, IBC_HDC_MAX_CYLS);
        return SCPE_ARG;
    }
    if (newHeads < 1 || newHeads > IBC_HDC_MAX_HEADS) {
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: Number of heads must be 1-%d.\n",
            ibc_hdc_info->sel_drive, IBC_HDC_MAX_HEADS);
        return SCPE_ARG;
    }
    if (newSPT < 1 || newSPT > IBC_HDC_MAX_SPT) {
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: Number of sectors per track must be 1-%d.\n",
            ibc_hdc_info->sel_drive, IBC_HDC_MAX_SPT);
        return SCPE_ARG;
    }
    if (newSecLen != 512 && newSecLen != 256 && newSecLen != 128) {
        sim_debug(ERROR_MSG, &ibc_hdc_dev,DEV_NAME "%d: Sector length must be 128, 256, or 512.\n",
            ibc_hdc_info->sel_drive);
        return SCPE_ARG;
    }

    pDrive->ncyls = newCyls;
    pDrive->nheads = newHeads;
    pDrive->nsectors = newSPT;
    pDrive->sectsize = newSecLen;

    return SCPE_OK;
}

/* Show geometry of the disk drive */
static t_stat ibc_hdc_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc)
{
    IBC_HDC_DRIVE_INFO* pDrive;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_hdc_info->drive[i];

    fprintf(st, "C:%d/H:%d/S:%d/N:%d",
        pDrive->ncyls, pDrive->nheads, pDrive->nsectors, pDrive->sectsize);

    return SCPE_OK;
}


/* IBC HDC I/O Dispatch */
static int32 ibchdcdev(const int32 port, const int32 io, const int32 data)
{
    if(io) {
        IBC_HDC_Write(port, (uint8)data);
        return 0;
    } else {
        return(IBC_HDC_Read(port));
    }
}

/* I/O Write to IBC Disk Slave Task File */
static uint8 IBC_HDC_Write(const uint32 Addr, uint8 cData)
{
    switch(Addr) {
    case 0x40:
        ibc_hdc_info->reg_temp_holding[0] = cData;
        sim_debug(TF_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR 0x%02x=0x%02x, HL=0x%04x\n", PCX, Addr, cData, HL_S);
        if (cData & 0x80) {
            ibc_hdc_info->taskfile[TF_CMD] = ibc_hdc_info->reg_temp_holding[0];
            ibc_hdc_info->taskfile[TF_DRIVE] = ibc_hdc_info->reg_temp_holding[1];
            ibc_hdc_info->taskfile[TF_TRKL] = ibc_hdc_info->reg_temp_holding[2];
            ibc_hdc_info->taskfile[TF_TRKH] = ibc_hdc_info->reg_temp_holding[3];
            if ((ibc_hdc_info->taskfile[TF_CMD] & IBC_HDC_CMD_MASK) != IBC_HDC_CMD_READ_PARAMETERS) {
                ibc_hdc_info->sel_drive = ibc_hdc_info->taskfile[TF_DRIVE] & 0x03;
            }
            ibc_hdc_info->status_reg = 0x30;
        }
        else {
            ibc_hdc_info->taskfile[TF_CSEC] = ibc_hdc_info->reg_temp_holding[0];
            ibc_hdc_info->taskfile[TF_HEAD] = ibc_hdc_info->reg_temp_holding[1];
            ibc_hdc_info->taskfile[TF_NSEC] = ibc_hdc_info->reg_temp_holding[2];
            ibc_hdc_info->taskfile[TF_SA3] = ibc_hdc_info->reg_temp_holding[3];
            ibc_hdc_info->status_reg = 0x20;
            IBC_HDC_doCommand();
        }
        break;
        /* Fall through */
    case 0x41:
    case 0x42:
    case 0x43:
        ibc_hdc_info->reg_temp_holding[Addr & 0x03] = cData;
        sim_debug(TF_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR 0x%02x=0x%02x, HL=0x%04x\n", PCX, Addr, cData, HL_S);
        break;
    case IBC_HDC_REG_FIFO_STATUS:
        ibc_hdc_info->secbuf_index = 0;
        break;
    case IBC_HDC_REG_FIFO:
        sim_debug(FIFO_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR FIFO 0x%02x=0x%02x, HL=0x%04x\n", PCX, Addr, cData, HL_S);
        ibc_hdc_info->sectbuf[ibc_hdc_info->secbuf_index++] = cData;
        break;
    default:
        sim_debug(TF_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " Unhandled WR 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    }

    return 0;
}


/* I/O Read from IBC Disk Slave Task File */
static uint8 IBC_HDC_Read(const uint32 Addr)
{
    uint8 cData = 0xFF;

    switch (Addr) {
    case IBC_HDC_REG_STATUS:
        cData = ibc_hdc_info->status_reg;
        sim_debug(TF_MSG, &ibc_hdc_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[STATUS]=0x%02x\n", PCX, cData);
        break;
    case IBC_HDC_REG_FIFO:
        cData = ibc_hdc_info->sectbuf[ibc_hdc_info->secbuf_index];

        sim_debug(FIFO_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[FIFO][0x%02x]=0x%02x\n", PCX, ibc_hdc_info->secbuf_index, cData);
        ibc_hdc_info->secbuf_index++;
        break;
    case IBC_HDC_REG_FIFO_STATUS:
        break;
    default:
        sim_debug(TF_MSG, &ibc_hdc_dev, DEV_NAME ": " ADDRESS_FORMAT
            " Unhandled RD 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    }

    return (cData);
}

/* Validate that Cyl, Head, Sector, Sector Length are valid for the current
 * disk drive geometry.
 */
static int IBC_HDC_Validate_CHSN(IBC_HDC_DRIVE_INFO* pDrive)
{
    int status = SCPE_OK;

    /* Check to make sure we're operating on a valid C/H/S/N. */
    if ((pDrive->cur_cyl >= pDrive->ncyls) ||
        (pDrive->cur_head >= pDrive->nheads) ||
        (pDrive->cur_sect >= pDrive->nsectors) ||
        (pDrive->cur_sectsize != pDrive->sectsize))
    {
        /* Set error bit in status register. */
        ibc_hdc_info->status_reg |= IBC_HDC_STATUS_ERROR;

        /* Set ID_NOT_FOUND bit in error register. */
        ibc_hdc_info->error_reg |= IBC_HDC_ERROR_ID_NOT_FOUND;

        sim_debug(ERROR_MSG, &ibc_hdc_dev,DEV_NAME "%d: " ADDRESS_FORMAT
            " C:%d/H:%d/S:%d/N:%d: ID Not Found (check disk geometry.)\n", ibc_hdc_info->sel_drive, PCX,
            pDrive->cur_cyl,
            pDrive->cur_head,
            pDrive->cur_sect,
            pDrive->cur_sectsize);

        status = SCPE_IOERR;
    }
    else {
        /* Clear ID_NOT_FOUND bit in error register. */
        ibc_hdc_info->error_reg &= ~IBC_HDC_ERROR_ID_NOT_FOUND;
    }

    return (status);
}

/* 85MB Fixed Disk Drive 0: C:680/H:15/N:32/L:256
 * 10MB Removable Cartridge Drive 3: C:612/H:2/N:32/L:256
 */
unsigned char HDParameters[108] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 0x00
    0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00,   // 0x08 0088=136
    0x00, 0x10, 0x01, 0x00, 0x00, 0x98, 0x01, 0x00,   // 0x10 0110=272, 0198=408
    0x00, 0x20, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00,   // 0x18 0220=544
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x61, 0x62,   // 0x20
    0x20, 0x00, 0x61, 0x02, 0x02, 0x00, 0x00, 0x00,   // 0x28
    0x0F, 0x00, 0x88, 0x00, 0x20, 0x00, 0x1D, 0x03,   // 0x30=#heads
    0x0F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 0x38
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 0x40
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00,   // 0x48
    0x61, 0x62, 0x20, 0x00, 0x61, 0x02, 0x02, 0x00,   // 0x50
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 0x58
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // 0x60
    0xFF, 0xFF, 0xFF, 0xFF                            // 0x68
};

/* Perform IBC Disk Controller Command */
static t_stat IBC_HDC_doCommand(void)
{
    t_stat r = SCPE_OK;
    IBC_HDC_DRIVE_INFO* pDrive = &ibc_hdc_info->drive[ibc_hdc_info->sel_drive];
    uint8 cmd = ibc_hdc_info->taskfile[TF_CMD] & IBC_HDC_CMD_MASK;

    pDrive->cur_cyl    = (uint16)ibc_hdc_info->taskfile[TF_TRKH] << 8;
    pDrive->cur_cyl   |= ibc_hdc_info->taskfile[TF_TRKL];
    pDrive->xfr_nsects = ibc_hdc_info->taskfile[TF_NSEC];
    pDrive->cur_head   = ibc_hdc_info->taskfile[TF_HEAD];
    pDrive->cur_sect   = ibc_hdc_info->taskfile[TF_CSEC];
    pDrive->cur_sectsize = 256;
    if (pDrive->xfr_nsects == 0) {
        pDrive->xfr_nsects = 1;
    }

    switch (cmd) {
    case IBC_HDC_CMD_RESET:  /* Reset */
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " RESET COMMAND 0x%02x\n",
            ibc_hdc_info->sel_drive, PCX,
            cmd);
        ibc_hdc_info->status_reg = 0x20;
        break;
    case IBC_HDC_CMD_READ_SECT:
    case IBC_HDC_CMD_WRITE_SECT:
    {
        uint32 xfr_len;
        uint32 file_offset;

        sim_debug(CMD_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " CMD: %02x: Params 0x%02x,%02x,%02x - 0x%02x,%02x,%02x,%02x.\n", ibc_hdc_info->sel_drive, PCX,
            ibc_hdc_info->taskfile[TF_CMD], ibc_hdc_info->taskfile[TF_TRKH], ibc_hdc_info->taskfile[TF_TRKL], ibc_hdc_info->taskfile[TF_DRIVE],
            ibc_hdc_info->taskfile[TF_SA3], ibc_hdc_info->taskfile[TF_NSEC], ibc_hdc_info->taskfile[TF_HEAD], ibc_hdc_info->taskfile[TF_CSEC]);

        /* Abort the read/write operation if C/H/S/N is not valid. */
        if (IBC_HDC_Validate_CHSN(pDrive) != SCPE_OK) break;

        /* Calculate file offset */
        file_offset  = (pDrive->cur_cyl * pDrive->nheads * pDrive->nsectors);   /* Full cylinders */
        file_offset += (pDrive->cur_head * pDrive->nsectors);   /* Add full heads */
        file_offset += (pDrive->cur_sect);  /* Add sectors for current request */
        file_offset *= pDrive->sectsize;    /* Convert #sectors to byte offset */

        xfr_len = pDrive->xfr_nsects * pDrive->sectsize;

        if (0 != (r = sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET)))
            break;

        if (cmd == IBC_HDC_CMD_READ_SECT) { /* Read */
            sim_debug(RD_DATA_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                " READ SECTOR  C:%04d/H:%d/S:%04d/#:%d, offset=%5x, len=%d\n",
                ibc_hdc_info->sel_drive, PCX,
                pDrive->cur_cyl, pDrive->cur_head,
                pDrive->cur_sect, pDrive->xfr_nsects, file_offset, xfr_len);
            if (sim_fread(ibc_hdc_info->sectbuf, 1, xfr_len, (pDrive->uptr)->fileref) != xfr_len) {
                r = SCPE_IOERR;
            }
            ibc_hdc_info->status_reg = 0x60;
        }
        else { /* Write */
            sim_debug(WR_DATA_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                " WRITE SECTOR  C:%04d/H:%d/S:%04d/#:%d, offset=%5x, len=%d\n",
                ibc_hdc_info->sel_drive, PCX,
                pDrive->cur_cyl, pDrive->cur_head,
                pDrive->cur_sect, pDrive->xfr_nsects, file_offset, xfr_len);

            if (sim_fwrite(ibc_hdc_info->sectbuf, 1, xfr_len, (pDrive->uptr)->fileref) != xfr_len) {
                r = SCPE_IOERR;
            }

            ibc_hdc_info->status_reg = 0x60;
        }
        break;
    }
    case IBC_HDC_CMD_FORMAT_TRK:
    {
        uint32 data_len;
        uint32 file_offset;
        uint8* fmtBuffer;

        sim_debug(WR_DATA_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " FORMAT TRACK  C:%04d/H:%d\n",
            ibc_hdc_info->sel_drive, PCX,
            pDrive->cur_cyl, pDrive->cur_head);

        data_len = pDrive->nsectors * pDrive->sectsize;

        /* Abort the read/write operation if C/H/S/N is not valid. */
        if (IBC_HDC_Validate_CHSN(pDrive) != SCPE_OK) break;

        sim_debug(WR_DATA_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " FORMAT TRACK: C:%d/H:%d/Fill=0x%02x/Len=%d\n",
            ibc_hdc_info->sel_drive, PCX, pDrive->cur_cyl,
            pDrive->cur_head, IBC_HDC_FORMAT_FILL_BYTE, data_len);

        /* Calculate file offset, formatting always handles a full track at a time. */
        file_offset = (pDrive->cur_cyl * pDrive->nheads * pDrive->nsectors);   /* Full cylinders */
        file_offset += (pDrive->cur_head * pDrive->nsectors);   /* Add full heads */
        file_offset *= pDrive->sectsize;    /* Convert #sectors to byte offset */

        fmtBuffer = calloc(data_len, sizeof(uint8));

        if (fmtBuffer == 0) {
            return sim_messagef(SCPE_MEM, "Cannot allocate %d bytes for format buffer.\n", data_len);
        }

#if (IBC_HDC_FORMAT_FILL_BYTE != 0)
        memset(fmtBuffer, IBC_HDC_FORMAT_FILL_BYTE, data_len);
#endif

        if (0 != (r = sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET))) {
            if (sim_fwrite(fmtBuffer, 1, data_len, (pDrive->uptr)->fileref) != data_len) {
                r = SCPE_IOERR;
            }
        }

        free(fmtBuffer);
        ibc_hdc_info->status_reg = 0x20;

        break;
    }
    case IBC_HDC_CMD_ACCESS_FIFO: /* Access FIFO */
        sim_debug(WR_DATA_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " ACCESS FIFO  %d blocks.\n",
            ibc_hdc_info->sel_drive, PCX,
            ibc_hdc_info->taskfile[TF_NSEC]);
        ibc_hdc_info->secbuf_index = 0;
        ibc_hdc_info->status_reg = 0x20;
        break;
    case IBC_HDC_CMD_READ_PARAMETERS:  /* Read Drive Parameters */
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " READ DRIVE PARAMETERS C:%0d/H:%d/S:%2d\n",
            ibc_hdc_info->sel_drive, PCX,
            pDrive->cur_cyl, pDrive->cur_head, pDrive->cur_sect);
        memcpy(ibc_hdc_info->sectbuf, HDParameters, sizeof(HDParameters));
        ibc_hdc_info->status_reg = 0x60;
        break;
    default:
        sim_debug(ERROR_MSG, &ibc_hdc_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " UNKNOWN COMMAND 0x%02x\n",
            ibc_hdc_info->sel_drive, PCX,
            cmd);
        ibc_hdc_info->status_reg = 0x60;
        break;
    }
    return r;
}
