/*************************************************************************
 *                                                                       *
 * Copyright (c) 2007-2020 Howard M. Harte.                              *
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
 *************************************************************************/

#include "altairz80_defs.h"
#include "sim_imd.h"

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define SEEK_MSG    (1 << 1)
#define CMD_MSG     (1 << 2)
#define RD_DATA_MSG (1 << 3)
#define WR_DATA_MSG (1 << 4)
#define VERBOSE_MSG (1 << 5)

#define HDC1001_MAX_DRIVES          4       /* Maximum number of drives supported */
#define HDC1001_MAX_SECLEN          512     /* Maximum of 512 bytes per sector */
#define HDC1001_FORMAT_FILL_BYTE    0xe5    /* Real controller uses 0, but we
                                               choose 0xe5 so the disk shows
                                               up as blank under CP/M. */
#define HDC1001_MAX_CYLS            1024
#define HDC1001_MAX_HEADS           8
#define HDC1001_MAX_SPT             256

#define DEV_NAME    "ADCHD"

/* Task File Register Offsets */
#define TF_DATA     0
#define TF_ERROR    1   /* Read */
#define TF_PRECOMP  1   /* Write */
#define TF_SECNT    2
#define TF_SECNO    3
#define TF_CYLLO    4
#define TF_CYLHI    5
#define TF_SDH      6
#define TF_STATUS   7   /* Read */
#define TF_CMD      7   /* Write */

#define HDC1001_STATUS_BUSY         (1 << 7)
#define HDC1001_STATUS_READY        (1 << 6)
#define HDC1001_STATUS_WRITE_FAULT  (1 << 5)
#define HDC1001_STATUS_SEEK_COMPL   (1 << 4)
#define HDC1001_STATUS_DRQ          (1 << 3)
#define HDC1001_STATUS_ERROR        (1 << 0)

#define HDC1001_ERROR_ID_NOT_FOUND  (1 << 4)

#define HDC1001_CMD_RESTORE         0x10
#define HDC1001_CMD_READ_SECT       0x20
#define HDC1001_CMD_WRITE_SECT      0x30
#define HDC1001_CMD_FORMAT_TRK      0x50
#define HDC1001_CMD_SEEK            0x70

#define HDC1001_RWOPT_DMA           (1 << 3)
#define HDC1001_RWOPT_MULTI         (1 << 2)
#define HDC1001_RWOPT_LONG          (1 << 3)

static char* hdc1001_reg_rd_str[] = {
    "DATA    ",
    "ERROR   ",
    "SECNT   ",
    "SECNO   ",
    "CYLLO   ",
    "CYLHI   ",
    "SDH     ",
    "STATUS  "
};

static char* hdc1001_reg_wr_str[] = {
    "DATA   ",
    "PRECOMP",
    "SECNT  ",
    "SECNO  ",
    "CYLLO  ",
    "CYLHI  ",
    "SDH    ",
    "COMMAND"
};

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
    uint16 cur_sectsize;/* Current sector size in SDH register */
    uint16 xfr_nsects;  /* Number of sectors to transfer */
    uint8 ready;        /* Is drive ready? */
} HDC1001_DRIVE_INFO;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
    uint8   sel_drive;  /* Currently selected drive */
    uint8   taskfile[8]; /* ATA Task File Registers */
    uint8   status_reg; /* HDC-1001 Status Register */
    uint8   error_reg;  /* HDC-1001 Status Register */
    uint8   retries;    /* Number of retries to attempt */
    uint8   ndrives;    /* Number of drives attached to the controller */
    uint8   sectbuf[HDC1001_MAX_SECLEN];
    uint16  secbuf_index;
    HDC1001_DRIVE_INFO drive[HDC1001_MAX_DRIVES];
} HDC1001_INFO;

static HDC1001_INFO hdc1001_info_data = { { 0x0, 0, 0xE0, 8 } };
static HDC1001_INFO *hdc1001_info = &hdc1001_info_data;

extern uint32 PCX;
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

#define UNIT_V_HDC1001_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_HDC1001_VERBOSE      (1 << UNIT_V_HDC1001_VERBOSE)
#define HDC1001_CAPACITY          (512*4*16*512)   /* Default Disk Capacity Quantum 2020 */

static t_stat hdc1001_reset(DEVICE *hdc1001_dev);
static t_stat hdc1001_attach(UNIT *uptr, CONST char *cptr);
static t_stat hdc1001_detach(UNIT *uptr);
static t_stat hdc1001_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc);
static t_stat hdc1001_unit_show_geometry(FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static int32 hdc1001dev(const int32 port, const int32 io, const int32 data);

static uint8 HDC1001_Read(const uint32 Addr);
static uint8 HDC1001_Write(const uint32 Addr, uint8 cData);
static t_stat HDC1001_doCommand(void);
static const char* hdc1001_description(DEVICE *dptr);

static UNIT hdc1001_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDC1001_CAPACITY) }
};

static REG hdc1001_reg[] = {
    { HRDATAD (TF_ERROR,    hdc1001_info_data.error_reg,            8, "Taskfile Error Register"), },
    { HRDATAD (TF_STATUS,   hdc1001_info_data.status_reg,           8, "Taskfile Status Register"), },
    { HRDATAD (TF_DATA,     hdc1001_info_data.taskfile[TF_DATA],    8, "Taskfile Data Register"), },
    { HRDATAD (TF_PRECOMP,  hdc1001_info_data.taskfile[TF_PRECOMP], 8, "Taskfile Precomp Register"), },
    { HRDATAD (TF_SECNT,    hdc1001_info_data.taskfile[TF_SECNT],   8, "Taskfile Sector Count Register"), },
    { HRDATAD (TF_SECNO,    hdc1001_info_data.taskfile[TF_SECNO],   8, "Taskfile Sector Number Register"), },
    { HRDATAD (TF_CYLLO,    hdc1001_info_data.taskfile[TF_CYLLO],   8, "Taskfile Cylinder Low Register"), },
    { HRDATAD (TF_CYLHI,    hdc1001_info_data.taskfile[TF_CYLHI],   8, "Taskfile Cylinder High Register"), },
    { HRDATAD (TF_SDH,      hdc1001_info_data.taskfile[TF_SDH],     8, "Taskfile SDH Register"), },
    { HRDATAD (TF_CMD,      hdc1001_info_data.taskfile[TF_CMD],     8, "Taskfile Command Register"), },
    { NULL }
};

#define HDC1001_NAME    "ADC HDC-1001 Hard Disk Controller"

static const char* hdc1001_description(DEVICE *dptr) {
    return HDC1001_NAME;
}

static MTAB hdc1001_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR,    0,                  "GEOMETRY",     "GEOMETRY",
        &hdc1001_unit_set_geometry, &hdc1001_unit_show_geometry, NULL,
        "Set disk geometry C:nnnn/H:n/S:nnn/N:nnnn" },
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
    DEV_NAME, hdc1001_unit, hdc1001_reg, hdc1001_mod,
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
        sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &hdc1001dev, "hdc1001dev", TRUE);
    } else {
        /* Connect HDC1001 at base address */
        if(sim_map_resource(pnp->io_base, pnp->io_size, RESOURCE_TYPE_IO, &hdc1001dev, "hdc1001dev", FALSE) != 0) {
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
            return SCPE_ARG;
        }
    }

    hdc1001_info->status_reg = 0;
    hdc1001_info->error_reg = 0;
    hdc1001_info->sel_drive = 0;
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

    /* Defaults for the Quantum 2020 Drive */
    pDrive->ready = 0;
    if (pDrive->ncyls == 0) {
        /* If geometry was not specified, default to Quantun 2020 */
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
            hdc1001_detach(uptr);
            return r;
        }
    }

    sim_debug(VERBOSE_MSG, &hdc1001_dev, DEV_NAME "%d, attached to '%s', type=DSK, len=%d\n",
        i, cptr, uptr->capac);

    pDrive->readonly = (uptr->flags & UNIT_RO) ? 1 : 0;
    hdc1001_info->error_reg = 0;
    pDrive->ready = 1;

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

    sim_debug(VERBOSE_MSG, &hdc1001_dev, "Detach " DEV_NAME "%d\n", i);

    r = detach_unit(uptr);  /* detach unit */
    if ( r != SCPE_OK)
        return r;

    return SCPE_OK;
}

/* Set geometry of the disk drive */
static t_stat hdc1001_unit_set_geometry(UNIT* uptr, int32 value, CONST char* cptr, void* desc)
{
    HDC1001_DRIVE_INFO* pDrive;
    int8 i;
    int32 result;
    uint16 newCyls, newHeads, newSPT, newSecLen;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &hdc1001_info->drive[i];

    if (cptr == NULL)
        return SCPE_ARG;

    result = sscanf(cptr, "C:%hd/H:%hd/S:%hd/N:%hd", &newCyls, &newHeads, &newSPT, &newSecLen);

    /* Validate Cyl, Heads, Sector, Length are valid for the HDC-1001 */
    if (newCyls < 1 || newCyls > HDC1001_MAX_CYLS) {
        sim_debug(ERROR_MSG, &hdc1001_dev, DEV_NAME "%d: Number of cylinders must be 1-%d.\n",
            hdc1001_info->sel_drive, HDC1001_MAX_CYLS);
        return SCPE_ARG;
    }
    if (newHeads < 1 || newHeads > HDC1001_MAX_HEADS) {
        sim_debug(ERROR_MSG, &hdc1001_dev, DEV_NAME "%d: Number of heads must be 1-%d.\n",
            hdc1001_info->sel_drive, HDC1001_MAX_HEADS);
        return SCPE_ARG;
    }
    if (newSPT < 1 || newSPT > HDC1001_MAX_SPT) {
        sim_debug(ERROR_MSG, &hdc1001_dev, DEV_NAME "%d: Number of sectors per track must be 1-%d.\n",
            hdc1001_info->sel_drive, HDC1001_MAX_SPT);
        return SCPE_ARG;
    }
    if (newSecLen != 512 && newSecLen != 256 && newSecLen != 128) {
        sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: Sector length must be 128, 256, or 512.\n",
            hdc1001_info->sel_drive);
        return SCPE_ARG;
    }

    pDrive->ncyls = newCyls;
    pDrive->nheads = newHeads;
    pDrive->nsectors = newSPT;
    pDrive->sectsize = newSecLen;

    return SCPE_OK;
}

/* Show geometry of the disk drive */
static t_stat hdc1001_unit_show_geometry(FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    HDC1001_DRIVE_INFO* pDrive;
    int8 i;

    i = find_unit_index(uptr);

    if (i == -1) {
        return (SCPE_IERR);
    }

    pDrive = &hdc1001_info->drive[i];

    fprintf(st, "C:%d/H:%d/S:%d/N:%d",
        pDrive->ncyls, pDrive->nheads, pDrive->nsectors, pDrive->sectsize);

    return SCPE_OK;
}


/* HDC-1001 I/O Dispatch */
static int32 hdc1001dev(const int32 port, const int32 io, const int32 data)
{
    if(io) {
        HDC1001_Write(port, data);
        return 0;
    } else {
        return(HDC1001_Read(port));
    }
}


/* I/O Write to HDC-1001 Task File */
static uint8 HDC1001_Write(const uint32 Addr, uint8 cData)
{
    HDC1001_DRIVE_INFO *pDrive;
    uint8 cmd;

    pDrive = &hdc1001_info->drive[hdc1001_info->sel_drive];

    switch(Addr & 0x07) {
    case TF_DATA:   /* Data FIFO */
        hdc1001_info->sectbuf[hdc1001_info->secbuf_index] = cData;
        sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
            " WR TF[DATA 0x%03x]=0x%02x\n", PCX, hdc1001_info->secbuf_index, cData);
        hdc1001_info->secbuf_index++;
        if (hdc1001_info->secbuf_index == (pDrive->xfr_nsects * pDrive->sectsize)) HDC1001_doCommand();
        break;
        case TF_SDH:
            hdc1001_info->sel_drive = (cData >> 3) & 0x03;
        pDrive = &hdc1001_info->drive[hdc1001_info->sel_drive];
        switch ((cData >> 5) & 0x03) {  /* Sector Size */
            case 0:
                pDrive->cur_sectsize = 256;
                break;
            case 1:
                pDrive->cur_sectsize = 512;
                break;
            case 2:
                sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                    " Invalid sector size specified in SDH registrer.\n", hdc1001_info->sel_drive, PCX);
                pDrive->cur_sectsize = 512;
                break;
            case 3:
                pDrive->cur_sectsize = 128;
                break;
            }

        if (pDrive->sectsize != pDrive->cur_sectsize) {
            sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                " Sector size specified in SDH registrer (0x%x) does not match disk geometry (0x%x.)\n",
                hdc1001_info->sel_drive, PCX, pDrive->cur_sectsize, pDrive->sectsize);
        }
            /* fall through */
        case TF_PRECOMP:
        case TF_SECNT:
        case TF_SECNO:
        case TF_CYLLO:
        case TF_CYLHI:
            hdc1001_info->taskfile[Addr & 0x07] = cData;
            sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
                      " WR TF[%s]=0x%02x\n", PCX, hdc1001_reg_wr_str[Addr & 0x7], cData);
            break;
        case TF_CMD:
        {
            uint8 rwopts;

            hdc1001_info->secbuf_index = 0;
            hdc1001_info->taskfile[TF_CMD] = cData;
            hdc1001_info->status_reg &= ~HDC1001_STATUS_ERROR;  /* Clear error bit in status register. */
            pDrive->cur_cyl = hdc1001_info->taskfile[TF_CYLLO] | (hdc1001_info->taskfile[TF_CYLHI] << 8);

            cmd = hdc1001_info->taskfile[TF_CMD] & 0x70;

            rwopts = hdc1001_info->taskfile[TF_CMD] & 0xE;
            if (rwopts & HDC1001_RWOPT_MULTI) {
            pDrive->xfr_nsects = hdc1001_info->taskfile[TF_SECNT];
                sim_debug(ERROR_MSG, &hdc1001_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                    " Multi-sector Read/Write have not been verified.\n", hdc1001_info->sel_drive, PCX);
            }
            else {
                pDrive->xfr_nsects = 1;
            }


            /* Everything except Write commands are executed immediately. */
            if (cmd != HDC1001_CMD_WRITE_SECT) {
                HDC1001_doCommand();
            }
            else {
                /* Writes will be executed once the proper number of bytes
                   are written to the DATA FIFO. */
                hdc1001_info->secbuf_index = 0;
            }
        }
    }
    return 0;
}


/* I/O Read from HDC-1001 Task File */
static uint8 HDC1001_Read(const uint32 Addr)
{
    HDC1001_DRIVE_INFO* pDrive;
    uint8 cData = 0xFF;

    pDrive = &hdc1001_info->drive[hdc1001_info->sel_drive];

    cData = hdc1001_info->status_reg |= (pDrive->ready ? HDC1001_STATUS_READY : 0);

    switch (Addr & 0x07) {
    case TF_DATA:   /* Data FIFO */
        cData = hdc1001_info->sectbuf[hdc1001_info->secbuf_index];
        sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[DATA 0x%03x]=0x%02x\n", PCX, hdc1001_info->secbuf_index, cData);
        hdc1001_info->secbuf_index++;
        if (hdc1001_info->secbuf_index > HDC1001_MAX_SECLEN) hdc1001_info->secbuf_index = 0;
        break;
    case TF_SDH:
    case TF_SECNT:
    case TF_SECNO:
    case TF_CYLLO:
    case TF_CYLHI:
    cData = hdc1001_info->taskfile[Addr & 0x07];
        sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[%s]=0x%02x\n", PCX, hdc1001_reg_rd_str[Addr & 0x7], cData);
        break;
    case TF_ERROR:
        cData = hdc1001_info->error_reg;
        sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[ERROR]=0x%02x\n", PCX, cData);
        break;
    case TF_STATUS:
        cData = hdc1001_info->status_reg;
        sim_debug(VERBOSE_MSG, &hdc1001_dev,DEV_NAME ": " ADDRESS_FORMAT
            " RD TF[STATUS]=0x%02x\n", PCX, cData);
        break;
    default:
        break;
    }

    return (cData);
}

/* Validate that Cyl, Head, Sector, Sector Length are valid for the current
 * disk drive geometry.
 */
static int HDC1001_Validate_CHSN(HDC1001_DRIVE_INFO* pDrive)
{
    int status = SCPE_OK;

    /* Check to make sure we're operating on a valid C/H/S/N. */
    if ((pDrive->cur_cyl >= pDrive->ncyls) ||
        (pDrive->cur_head >= pDrive->nheads) ||
        (pDrive->cur_sect >= pDrive->nsectors) ||
        (pDrive->cur_sectsize != pDrive->sectsize))
    {
        /* Set error bit in status register. */
        hdc1001_info->status_reg |= HDC1001_STATUS_ERROR;

        /* Set ID_NOT_FOUND bit in error register. */
        hdc1001_info->error_reg |= HDC1001_ERROR_ID_NOT_FOUND;

        sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
            " ID Not Found (check disk geometry.)\n", hdc1001_info->sel_drive, PCX);

        status = SCPE_IOERR;
    }
    else {
        /* Clear ID_NOT_FOUND bit in error register. */
        hdc1001_info->error_reg &= ~HDC1001_ERROR_ID_NOT_FOUND;
    }

    return (status);
}


/* Perform HDC-1001 Command */
static t_stat HDC1001_doCommand(void)
{
    HDC1001_DRIVE_INFO* pDrive;
    uint8 cmd;

    cmd = hdc1001_info->taskfile[TF_CMD] & 0x70;

    pDrive = &hdc1001_info->drive[hdc1001_info->sel_drive];

    pDrive->cur_head = hdc1001_info->taskfile[TF_SDH] & 0x07;
    pDrive->cur_sect = hdc1001_info->taskfile[TF_SECNO];

    if(pDrive->ready) {

        /* Perform command */
        switch(cmd) {
            case HDC1001_CMD_RESTORE:
                pDrive->cur_cyl = 0;
                sim_debug(SEEK_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                    " RESTORE\n", hdc1001_info->sel_drive, PCX);
                hdc1001_info->status_reg |= HDC1001_STATUS_SEEK_COMPL;
                break;
            case HDC1001_CMD_SEEK:
                if (pDrive->cur_cyl >= pDrive->ncyls) {
                    sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                        " SEEK ERROR %d not found\n", hdc1001_info->sel_drive, PCX, pDrive->cur_cyl);
                    pDrive->cur_cyl = pDrive->ncyls - 1;
                }
                else {
                    sim_debug(SEEK_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                        " SEEK %d\n", hdc1001_info->sel_drive, PCX, pDrive->cur_cyl);
                    }
                hdc1001_info->status_reg |= HDC1001_STATUS_SEEK_COMPL;
                break;
            case HDC1001_CMD_WRITE_SECT:
                /* If drive is read-only, signal a write fault. */
                if (pDrive->readonly) {
                    hdc1001_info->status_reg |= HDC1001_STATUS_ERROR;
                    hdc1001_info->status_reg |= HDC1001_STATUS_WRITE_FAULT;
                break;
                } else {
                    hdc1001_info->status_reg &= ~HDC1001_STATUS_WRITE_FAULT;
            }
                /* Fall through */
            case HDC1001_CMD_READ_SECT:
            {
                uint32 track_len;
                uint32 xfr_len;
                uint32 file_offset;
                uint8 rwopts;   /* Options specified in the command: DMA, Multi-sector, long. */

                /* Abort the read/write operation if C/H/S/N is not valid. */
                if (HDC1001_Validate_CHSN(pDrive) != SCPE_OK) break;

                track_len = pDrive->nsectors * pDrive->sectsize;

                /* Calculate file offset */
                file_offset  = (pDrive->cur_cyl * pDrive->nheads * pDrive->nsectors);   /* Full cylinders */
                file_offset += (pDrive->cur_head * pDrive->nsectors);   /* Add full heads */
                file_offset += (pDrive->cur_sect);  /* Add sectors for current request */
                file_offset *= pDrive->sectsize;    /* Convert #sectors to byte offset */

                rwopts = hdc1001_info->taskfile[TF_CMD] & 0xE;

                if (rwopts & HDC1001_RWOPT_LONG) {
                    sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                        " LONG Read/Write not supported.\n", hdc1001_info->sel_drive, PCX);
                }

                xfr_len = pDrive->xfr_nsects * pDrive->sectsize;

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);

                if(cmd == HDC1001_CMD_READ_SECT) { /* Read */
                    if (rwopts & HDC1001_RWOPT_DMA) {
                        sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                            " DMA Read not supported.\n", hdc1001_info->sel_drive, PCX);
                    }
                    sim_debug(RD_DATA_MSG, &hdc1001_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                        " %s SECTOR  C:%04d/H:%d/S:%04d/#:%d, offset=%5x, len=%d\n",
                        hdc1001_info->sel_drive, PCX,
                        (cmd == HDC1001_CMD_READ_SECT) ? "READ" : "WRITE",
                        pDrive->cur_cyl, pDrive->cur_head,
                        pDrive->cur_sect, pDrive->xfr_nsects, file_offset, xfr_len);
                    sim_fread(hdc1001_info->sectbuf, 1, xfr_len, (pDrive->uptr)->fileref);
                } else { /* Write */
                    sim_debug(WR_DATA_MSG, &hdc1001_dev, DEV_NAME "%d: " ADDRESS_FORMAT
                        " %s SECTOR  C:%04d/H:%d/S:%04d/#:%d, offset=%5x, len=%d\n",
                        hdc1001_info->sel_drive, PCX,
                        (cmd == HDC1001_CMD_READ_SECT) ? "READ" : "WRITE",
                        pDrive->cur_cyl, pDrive->cur_head,
                        pDrive->cur_sect, pDrive->xfr_nsects, file_offset, xfr_len);
                    sim_fwrite(hdc1001_info->sectbuf, 1, xfr_len, (pDrive->uptr)->fileref);
                }
                hdc1001_info->status_reg |= HDC1001_STATUS_DRQ;
                break;
                }
            case HDC1001_CMD_FORMAT_TRK:
            {
                uint32 data_len;
                uint32 file_offset;
                uint8 *fmtBuffer;

                /* If drive is read-only, signal a write fault. */
                if (pDrive->readonly) {
                    hdc1001_info->status_reg |= HDC1001_STATUS_ERROR;
                    hdc1001_info->status_reg |= HDC1001_STATUS_WRITE_FAULT;
                    hdc1001_info->status_reg |= HDC1001_STATUS_DRQ;
                    break;
                }
                else {
                    hdc1001_info->status_reg &= ~HDC1001_STATUS_WRITE_FAULT;
                }

                data_len = pDrive->nsectors * pDrive->sectsize;

                /* Abort the read/write operation if C/H/S/N is not valid. */
                if (HDC1001_Validate_CHSN(pDrive) != SCPE_OK) break;

                sim_debug(WR_DATA_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                    " FORMAT TRACK: C:%d/H:%d/Fill=0x%02x/Len=%d\n",
                    hdc1001_info->sel_drive, PCX, pDrive->cur_cyl,
                    pDrive->cur_head, HDC1001_FORMAT_FILL_BYTE, data_len);

                /* Calculate file offset, formatting always handles a full track at a time. */
                file_offset = (pDrive->cur_cyl * pDrive->nheads * pDrive->nsectors);   /* Full cylinders */
                file_offset += (pDrive->cur_head * pDrive->nsectors);   /* Add full heads */
                file_offset *= pDrive->sectsize;    /* Convert #sectors to byte offset */

                fmtBuffer = calloc(data_len, sizeof(uint8));
                if (HDC1001_FORMAT_FILL_BYTE != 0) {
                    memset(fmtBuffer, HDC1001_FORMAT_FILL_BYTE, data_len);
                }

                sim_fseek((pDrive->uptr)->fileref, file_offset, SEEK_SET);
                sim_fwrite(fmtBuffer, 1, data_len, (pDrive->uptr)->fileref);

                free(fmtBuffer);
                hdc1001_info->status_reg |= HDC1001_STATUS_DRQ;

                break;
            }
            default:
                sim_debug(ERROR_MSG, &hdc1001_dev,DEV_NAME "%d: " ADDRESS_FORMAT
                    " CMD=%x Unsupported\n",
                    hdc1001_info->sel_drive, PCX, cmd);
                break;
        }
    }

    return SCPE_OK;
}
