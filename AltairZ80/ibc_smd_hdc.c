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
 *     IBC/Integrated Business Computers SMD Hard Disk Controller module *
 * for SIMH.                                                             *
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
#define REG_MSG     (1 << 5)
#define VERBOSE_MSG (1 << 6)

#define IBC_SMD_MAX_DRIVES          2       /* Maximum number of drives supported */
#define IBC_SMD_MAX_SECLEN          1024    /* Maximum of 1024 bytes per sector */
#define IBC_SMD_MAX_CYLS            1024
#define IBC_SMD_MAX_HEADS           8
#define IBC_SMD_MAX_SPT             256

#define DEV_NAME                    "IBCSMD"

#define IBC_SMD_STATUS_ERROR        (1 << 0)

#define IBC_SMD_ERROR_ID_NOT_FOUND  (1 << 4)

#define IBC_SMD_CMD_00              0x00
#define IBC_SMD_CMD_SELECT_UNIT     0x10
#define IBC_SMD_CMD_SET_CYL         0x20
#define IBC_SMD_CMD_SET_HEAD        0x40
#define IBC_SMD_CMD_REZERO          0x80
#define IBC_SMD_CMD_WRITE_SECT      0x81
#define IBC_SMD_CMD_READ_SECT       0x88

#define IBC_SMD_REG_ERROR           0x0         /* Read */
#define IBC_SMD_REG_ARG0            0x0         /* Write */
#define IBC_SMD_REG_ARG1            0x1         /* Write */
#define IBC_SMD_REG_CMD             0x2         /* Write */
#define IBC_SMD_REG_SEC             0x3         /* Write */
#define IBC_SMD_REG_STATUS          0x7
#define IBC_SMD_REG_DATA            0x4
#define IBC_SMD_REG_SECID           0x7

typedef struct {
    UNIT  *uptr;
    uint8  isreadonly;  /* Drive is read-only? */
    uint16 sectsize;    /* sector size */
    uint16 nsectors;    /* number of sectors/track */
    uint16 nheads;      /* number of heads */
    uint16 ncyls;       /* number of cylinders */
    uint16 cur_cyl;     /* Current cylinder */
    uint8  cur_head;    /* Current Head */
    uint8  cur_sect;    /* current starting sector of transfer */
    uint8  ready;       /* Is drive ready? */
} IBC_SMD_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   intenable;  /* Interrupt Enable */
    uint8   intvector;  /* Interrupt Vector */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   arg0;       /* IBC SMD Argument 0 Register */
    uint8   arg1;       /* IBC SMD Argument 1 Register */
    uint8   cmd;        /* IBC SMD Command Register */
    uint8   sec;        /* IBC SMD Sector Register */
    uint8   status_reg; /* Status Register */
    uint8   error_reg;  /* Error Register */
    uint8   retries;    /* Number of retries to attempt */
    uint8   ndrives;    /* Number of drives attached to the controller */
    uint8   sectbuf[IBC_SMD_MAX_SECLEN];
    uint16  secbuf_index;
    IBC_SMD_DRIVE_INFO drive[IBC_SMD_MAX_DRIVES];
} IBC_SMD_INFO;

static IBC_SMD_INFO ibc_smd_info_data = { { 0x0, 0, 0x40, 8 } };
static IBC_SMD_INFO *ibc_smd_info = &ibc_smd_info_data;

extern uint32 PCX;
extern int32 vectorInterrupt;     /* Interrupt pending */

extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

#define UNIT_V_IBC_SMD_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_IBC_SMD_VERBOSE      (1 << UNIT_V_IBC_SMD_VERBOSE)
#define IBC_SMD_CAPACITY          (512*4*16*512)   /* Default Disk Capacity Quantum 2020 */

static t_stat ibc_smd_reset(DEVICE *ibc_smd_dev);
static t_stat ibc_smd_attach(UNIT *uptr, CONST char *cptr);
static t_stat ibc_smd_detach(UNIT *uptr);
static t_stat ibc_smd_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc);
static t_stat ibc_smd_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc);
static int32 ibcsmddev(const int32 port, const int32 io, const int32 data);

static uint8 IBC_SMD_Read(const uint32 Addr);
static uint8 IBC_SMD_Write(const uint32 Addr, uint8 cData);
static t_stat IBC_SMD_doCommand(void);
static const char* ibc_smd_description(DEVICE *dptr);

static UNIT ibc_smd_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_SMD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_SMD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_SMD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, IBC_SMD_CAPACITY) }
};

static REG ibc_smd_reg[] = {
    { HRDATAD (SMD_ERROR,   ibc_smd_info_data.error_reg,    8, "SMD Error Register"), },
    { HRDATAD (SMD_STATUS,  ibc_smd_info_data.status_reg,   8, "SMD Status Register"), },
    { HRDATAD (SMD_ARG0,    ibc_smd_info_data.arg0,         8, "SMD ARG0 Register"), },
    { HRDATAD (SMD_ARG1,    ibc_smd_info_data.arg1,         8, "SMD ARG1 Register"), },
    { HRDATAD (SMD_CMD,     ibc_smd_info_data.cmd,          8, "SMD Command Register"), },
    { HRDATAD (SMD_SEC,     ibc_smd_info_data.sec,          8, "SMD Sector Register"), },
    { FLDATAD (INTENABLE,   ibc_smd_info_data.intenable,    1, "SMD Interrupt Enable"), },
    { DRDATAD (INTVECTOR,   ibc_smd_info_data.intvector,    8, "SMD Interrupt Vector"), },

    { NULL }
};

#define IBC_SMD_NAME    "IBC SMD Hard Disk Controller"

static const char* ibc_smd_description(DEVICE *dptr) {
    if (dptr == NULL) {
        return NULL;
    }

    return IBC_SMD_NAME;
}

static MTAB ibc_smd_mod[] = {
    { MTAB_XTD|MTAB_VDV,              0,    "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL,    "Sets disk controller I/O base address"    },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR,    0,    "GEOMETRY", "GEOMETRY",
        &ibc_smd_unit_set_geometry, &ibc_smd_unit_show_geometry, NULL,
        "Set disk geometry C:nnnn/H:n/S:nnn/N:nnnn" },
    { 0 }
};

/* Debug Flags */
static DEBTAB ibc_smd_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "REG",        REG_MSG,        "Register messages" },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "FIFO",       FIFO_MSG,       "FIFO messages"     },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { NULL,         0                                   }
};

DEVICE ibc_smd_dev = {
    DEV_NAME, ibc_smd_unit, ibc_smd_reg, ibc_smd_mod,
    IBC_SMD_MAX_DRIVES, 10, 31, 1, IBC_SMD_MAX_DRIVES, IBC_SMD_MAX_DRIVES,
    NULL, NULL, &ibc_smd_reset,
    NULL, &ibc_smd_attach, &ibc_smd_detach,
    &ibc_smd_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    ibc_smd_dt, NULL, NULL, NULL, NULL, NULL, &ibc_smd_description
};

/* Reset routine */
static t_stat ibc_smd_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ibcsmddev, "ibcsmddev", TRUE);
    } else {
        /* Connect IBC_SMD at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &ibcsmddev, "ibcsmddev", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    ibc_smd_info->status_reg = 0xd1;
    ibc_smd_info->error_reg = 0x80;
    ibc_smd_info->sel_drive = 0;
    return SCPE_OK;
}


/* Attach routine */
static t_stat ibc_smd_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r = SCPE_OK;
    IBC_SMD_DRIVE_INFO *pDrive;
    int i = 0;

    i = find_unit_index(uptr);
    if (i == -1) {
        return (SCPE_IERR);
    }
    pDrive = &ibc_smd_info->drive[i];

    /* Defaults for the Quantum 2020 Drive */
    pDrive->ready = 0;
    if (pDrive->ncyls == 0) {
        /* If geometry was not specified, default to Quantum 2020 */
        pDrive->ncyls = 512;
        pDrive->nheads = 4;
        pDrive->nsectors = 16;
        pDrive->sectsize = 512;
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
            ibc_smd_detach(uptr);
            return r;
        }
    }

    sim_debug(VERBOSE_MSG, &ibc_smd_dev, DEV_NAME "%d, attached to '%s', type=DSK, len=%d\n",
        i, cptr, uptr->capac);

    pDrive->isreadonly = (uptr->flags & UNIT_RO) ? 1 : 0;
    ibc_smd_info->error_reg = 0;
    pDrive->ready = 1;

    ibc_smd_info->status_reg = 0;

    return SCPE_OK;
}


/* Detach routine */
static t_stat ibc_smd_detach(UNIT *uptr)
{
    IBC_SMD_DRIVE_INFO *pDrive;
    t_stat r;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_smd_info->drive[i];

    pDrive->ready = 0;

    sim_debug(VERBOSE_MSG, &ibc_smd_dev, "Detach " DEV_NAME "%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}

/* Set geometry of the disk drive */
static t_stat ibc_smd_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    IBC_SMD_DRIVE_INFO* pDrive;
    int32 i;
    int32 result;
    uint16 newCyls, newHeads, newSPT, newSecLen;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_smd_info->drive[i];

    if (cptr == NULL)
        return SCPE_ARG;

    result = sscanf(cptr, "C:%hd/H:%hd/S:%hd/N:%hd", &newCyls, &newHeads, &newSPT, &newSecLen);
    if (result != 4)
        return SCPE_ARG;

    /* Validate Cyl, Heads, Sector, Length */
    if (newCyls < 1 || newCyls > IBC_SMD_MAX_CYLS) {
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME "%d: Number of cylinders must be 1-%d.\n",
            ibc_smd_info->sel_drive, IBC_SMD_MAX_CYLS);
        return SCPE_ARG;
    }
    if (newHeads < 1 || newHeads > IBC_SMD_MAX_HEADS) {
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME "%d: Number of heads must be 1-%d.\n",
            ibc_smd_info->sel_drive, IBC_SMD_MAX_HEADS);
        return SCPE_ARG;
    }
    if (newSPT < 1 || newSPT > IBC_SMD_MAX_SPT) {
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME "%d: Number of sectors per track must be 1-%d.\n",
            ibc_smd_info->sel_drive, IBC_SMD_MAX_SPT);
        return SCPE_ARG;
    }
    if (newSecLen != 512 && newSecLen != 256 && newSecLen != 128) {
        sim_debug(ERROR_MSG, &ibc_smd_dev,DEV_NAME "%d: Sector length must be 128, 256, or 512.\n",
            ibc_smd_info->sel_drive);
        return SCPE_ARG;
    }

    pDrive->ncyls = newCyls;
    pDrive->nheads = newHeads;
    pDrive->nsectors = newSPT;
    pDrive->sectsize = newSecLen;

    return SCPE_OK;
}

/* Show geometry of the disk drive */
static t_stat ibc_smd_unit_show_geometry(FILE* st, UNIT* uptr, int32 value, CONST void* desc)
{
    IBC_SMD_DRIVE_INFO* pDrive;
    int32 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &ibc_smd_info->drive[i];

    fprintf(st, "C:%d/H:%d/S:%d/N:%d",
        pDrive->ncyls, pDrive->nheads, pDrive->nsectors, pDrive->sectsize);

    return SCPE_OK;
}


/* IBC SMD I/O Dispatch */
static int32 ibcsmddev(const int32 port, const int32 io, const int32 data)
{
    if(io) {
        IBC_SMD_Write(port, (uint8)data);
        return 0;
    } else {
        return(IBC_SMD_Read(port));
    }
}

/* I/O Write to IBC SMD Registers */
static uint8 IBC_SMD_Write(const uint32 Addr, uint8 cData)
{
    switch(Addr & 7) {
    case IBC_SMD_REG_ARG0:
        switch (cData) {
        case 0x00:
            ibc_smd_info->error_reg = 0x0;
            break;
        case 0x01:
            ibc_smd_info->error_reg = 0x20;
            break;
        case IBC_SMD_CMD_READ_SECT:
            ibc_smd_info->error_reg = 0x30;
            break;
        default:
            ibc_smd_info->error_reg = 0x30;
            break;
        }
        ibc_smd_info->arg0 = cData;
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR SMD_ARG0 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    case IBC_SMD_REG_ARG1:
        ibc_smd_info->arg1 = cData;
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR SMD_ARG1 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    case IBC_SMD_REG_CMD:
        ibc_smd_info->cmd = cData;
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR SMD_CMD 0x%02x=0x%02x\n", PCX, Addr, cData);
        IBC_SMD_doCommand();
        break;
    case IBC_SMD_REG_SEC:
        ibc_smd_info->sec = cData;
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR SMD_SEC 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    case IBC_SMD_REG_SECID:
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR SECID  0x%02x=0x%02x\n", PCX, Addr, cData);
        ibc_smd_info->secbuf_index = 0;
        break;
    case IBC_SMD_REG_DATA:
        sim_debug(FIFO_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " WR FIFO   0x%02x=0x%02x\n", PCX, Addr, cData);
        ibc_smd_info->sectbuf[ibc_smd_info->secbuf_index] = cData;
        ibc_smd_info->secbuf_index++;
        break;
    default:
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " Unhandled WR 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    }

    return 0;
}

/* I/O Read from IBC SMD Registers */
static uint8 IBC_SMD_Read(const uint32 Addr)
{
    uint8 cData = 0xFF;

    switch (Addr & 7) {
    case IBC_SMD_REG_ERROR:
        cData = ibc_smd_info->error_reg;
        sim_debug(REG_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " RD ERROR  0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    case 0x1:
        cData = 0x7f;
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " RD Unknown  0x%02x=0x%02x\n", PCX, Addr, cData);
    case IBC_SMD_REG_DATA:
        cData = ibc_smd_info->sectbuf[ibc_smd_info->secbuf_index];
        sim_debug(FIFO_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " RD DATA[0x%02x]=0x%02x\n", PCX, ibc_smd_info->secbuf_index, cData);
        ibc_smd_info->secbuf_index++;
        break;
    case IBC_SMD_REG_STATUS:
        cData = ibc_smd_info->status_reg;
        sim_debug(REG_MSG, &ibc_smd_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD STATUS 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    default:
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME ": " ADDRESS_FORMAT
            " Unhandled RD 0x%02x=0x%02x\n", PCX, Addr, cData);
        break;
    }

    return (cData);
}

/* Validate that Cyl, Head, Sector are valid for the current
 * disk drive geometry.
 */
static int IBC_SMD_Validate_CHSN(IBC_SMD_DRIVE_INFO* pDrive)
{
    int status = SCPE_OK;

    /* Check to make sure we're operating on a valid C/H/S/N. */
    if ((pDrive->cur_cyl >= pDrive->ncyls) ||
        (pDrive->cur_head >= pDrive->nheads) ||
        (pDrive->cur_sect >= pDrive->nsectors))
    {
        /* Set error bit in status register. */
        ibc_smd_info->status_reg |= IBC_SMD_STATUS_ERROR;

        /* Set ID_NOT_FOUND bit in error register. */
        ibc_smd_info->error_reg |= IBC_SMD_ERROR_ID_NOT_FOUND;

        sim_debug(ERROR_MSG, &ibc_smd_dev,DEV_NAME "%d: " ADDRESS_FORMAT
            " C:%d/H:%d/S:%d: ID Not Found (check disk geometry.)\n", ibc_smd_info->sel_drive, PCX,
            pDrive->cur_cyl,
            pDrive->cur_head,
            pDrive->cur_sect);

        status = SCPE_IOERR;
    }
    else {
        /* Clear ID_NOT_FOUND bit in error register. */
        ibc_smd_info->error_reg &= ~IBC_SMD_ERROR_ID_NOT_FOUND;
    }

    return (status);
}

/* Perform SMD Disk Command */
static t_stat IBC_SMD_doCommand(void)
{
    t_stat r = SCPE_OK;
    IBC_SMD_DRIVE_INFO* pDrive = &ibc_smd_info->drive[ibc_smd_info->sel_drive];
    uint8 cmd = ibc_smd_info->cmd;

    switch (cmd) {
    case IBC_SMD_CMD_00:
        break;
    case IBC_SMD_CMD_SELECT_UNIT:
        ibc_smd_info->sel_drive = (ibc_smd_info->arg0 >> 4) & 1;
        sim_debug(CMD_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " 0x%02x: Select Unit %d\n",
            ibc_smd_info->sel_drive, PCX,
            cmd, ibc_smd_info->arg0 >> 4);
        break;
    case IBC_SMD_CMD_SET_CYL:  /* Set Cyl */
        pDrive->cur_cyl = ibc_smd_info->arg0 << 8;
        pDrive->cur_cyl |= ibc_smd_info->arg1;
        sim_debug(CMD_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " 0x%02x: Set Cylinder %d\n",
            ibc_smd_info->sel_drive, PCX,
            cmd, pDrive->cur_cyl);
        break;
    case IBC_SMD_CMD_SET_HEAD:  /* Set Head */
        pDrive->cur_head = ibc_smd_info->arg1;
        sim_debug(CMD_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " 0x%02x: Set Head %d\n",
            ibc_smd_info->sel_drive, PCX,
            cmd, pDrive->cur_head);
        break;
    case IBC_SMD_CMD_REZERO:
        sim_debug(CMD_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " 0x%02x: Rezero %d\n",
            ibc_smd_info->sel_drive, PCX,
            cmd, ibc_smd_info->arg1);
        ibc_smd_info->status_reg = 0xd1;
        break;
    case IBC_SMD_CMD_READ_SECT:
    case IBC_SMD_CMD_WRITE_SECT:
    {
        uint32 xfr_len;
        uint32 file_offset;

        pDrive->cur_sect = ibc_smd_info->sec;
        ibc_smd_info->secbuf_index = 0;
        ibc_smd_info->sectbuf[0] = (pDrive->cur_cyl >> 8) & 0xFF;
        ibc_smd_info->sectbuf[1] = pDrive->cur_cyl & 0xFF;
        ibc_smd_info->sectbuf[2] = pDrive->cur_head;
        ibc_smd_info->sectbuf[3] = pDrive->cur_sect;

        /* Abort the read/write operation if C/H/S/N is not valid. */
        if (IBC_SMD_Validate_CHSN(pDrive) != SCPE_OK) break;

        /* Calculate file offset */
        file_offset = (pDrive->cur_cyl * pDrive->nheads * pDrive->nsectors);   /* Full cylinders */
        file_offset += (pDrive->cur_head * pDrive->nsectors);   /* Add full heads */
        file_offset += (pDrive->cur_sect);  /* Add sectors for current request */
        file_offset *= pDrive->sectsize;    /* Convert #sectors to byte offset */

        xfr_len = pDrive->sectsize;

        if (0 != (r = sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET)))
            break;

        if (cmd == IBC_SMD_CMD_READ_SECT) { /* Read */
            sim_debug(RD_DATA_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                " RD SECTOR  C:%04d/H:%d/S:%04d, offset=%5x, len=%d\n",
                ibc_smd_info->sel_drive, PCX,
                pDrive->cur_cyl, pDrive->cur_head, pDrive->cur_sect,
                file_offset, xfr_len);
            if (sim_fread(&ibc_smd_info->sectbuf[4], 1, xfr_len, (pDrive->uptr)->fileref) != xfr_len) {
                r = SCPE_IOERR;
            }
        }
        else { /* Write */
            sim_debug(WR_DATA_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                " WR SECTOR  C:%04d/H:%d/S:%04d offset=%5x, len=%d\n",
                ibc_smd_info->sel_drive, PCX,
                pDrive->cur_cyl, pDrive->cur_head, pDrive->cur_sect,
                file_offset, xfr_len);
            if (sim_fwrite(&ibc_smd_info->sectbuf[4], 1, xfr_len, (pDrive->uptr)->fileref) != xfr_len) {
                r = SCPE_IOERR;
            }
        }
        break;
    }
    default:
        sim_debug(ERROR_MSG, &ibc_smd_dev, DEV_NAME "%d: " ADDRESS_FORMAT
            " UNKNOWN COMMAND 0x%02x\n",
            ibc_smd_info->sel_drive, PCX,
            cmd);
        break;
    }
    return r;
}
