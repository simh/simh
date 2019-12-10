/*************************************************************************
 *                                                                       *
 * $Id: i8272.c 1999 2008-07-22 04:25:28Z hharte $                       *
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
    - 06-Aug-2008, Tony Nicholson, READID should use HDS bit and add support
             for logical Head and Cylinder maps in the .IMD image file (AGN)
*/

/*#define DBG_MSG */

#include "altairz80_defs.h"
#include "sim_imd.h"
#include "i8272.h"

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
#define FMT_MSG     (1 << 6)
#define VERBOSE_MSG (1 << 7)
#define IRQ_MSG     (1 << 8)

#define I8272_MAX_DRIVES    4
#define I8272_MAX_SECTOR    26
#define I8272_SECTOR_LEN    8192
/* 2^(7 + I8272_MAX_N) == I8272_SECTOR_LEN */
#define I8272_MAX_N         6

#define CMD_PHASE   0
#define EXEC_PHASE  1
#define DATA_PHASE  2

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
    uint8 fdc_sec_len;  /* N Sector Length, in bytes: 2^(7 + fdc_sec_len),  fdc_sec_len <= I8272_MAX_N */
    uint8 fdc_eot;      /* EOT End of Track (Final sector number of cyl) */
    uint8 fdc_gpl;      /* GPL Gap3 Length */
    uint8 fdc_dtl;      /* DTL Data Length */
    uint8 fdc_mt;       /* Multiple sectors */
    uint8 fdc_mfm;      /* MFM mode */
    uint8 fdc_sk;       /* Skip Deleted Data */
    uint8 fdc_hds;      /* Head Select */
    uint8 fdc_fillbyte; /* Fill-byte used for FORMAT TRACK */
    uint8 fdc_sc;       /* Sector count for FORMAT TRACK */
    uint8 fdc_sectorcount; /* Current sector being formatted by FORMAT TRACK */
    uint8 fdc_sectormap[I8272_MAX_SECTOR]; /* Physical to logical sector map for FORMAT TRACK */
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
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

/* These are needed for DMA.  PIO Mode has not been implemented yet. */
extern void PutByteDMA(const uint32 Addr, const uint32 Value);
extern uint8 GetByteDMA(const uint32 Addr);

#define UNIT_V_I8272_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_I8272_VERBOSE      (1 << UNIT_V_I8272_VERBOSE)
#define I8272_CAPACITY          (77*2*16*256)   /* Default Micropolis Disk Capacity         */
#define I8272_CAPACITY_SSSD     (77*1*26*128)   /* Single-sided Single Density IBM Diskette1 */

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

static void raise_i8272_interrupt(void);
static int32 i8272dev(const int32 port, const int32 io, const int32 data);
static t_stat i8272_reset(DEVICE *dptr);
int32 find_unit_index (UNIT *uptr);
static const char* i8272_description(DEVICE *dptr);

I8272_INFO i8272_info_data = { { 0x0, 0, 0xC0, 2 } };
I8272_INFO *i8272_info = &i8272_info_data;

uint8 i8272_irq = 1;

static UNIT i8272_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, I8272_CAPACITY) }
};

#define I8272_NAME  "Intel/NEC(765) FDC Core"

static const char* i8272_description(DEVICE *dptr) {
    return I8272_NAME;
}

static MTAB i8272_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "IOBASE",   "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"    },
    /* quiet, no warning messages       */
    { UNIT_I8272_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " I8272_NAME "n"            },
    /* verbose, show warning messages   */
    { UNIT_I8272_VERBOSE,   UNIT_I8272_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " I8272_NAME "n"               },
    { 0 }
};

/* Debug Flags */
static DEBTAB i8272_dt[] = {
    { "ERROR",      ERROR_MSG,      "Error messages"    },
    { "SEEK",       SEEK_MSG,       "Seek messages"     },
    { "CMD",        CMD_MSG,        "Command messages"  },
    { "READ",       RD_DATA_MSG,    "Read messages"     },
    { "WRITE",      WR_DATA_MSG,    "Write messages"    },
    { "STATUS",     STATUS_MSG,     "Status messages"   },
    { "FMT",        FMT_MSG,        "Format messages"   },
    { "VERBOSE",    VERBOSE_MSG,    "Verbose messages"  },
    { "IRQ",        IRQ_MSG,        "IRQ messages"      },
    { NULL,         0                                   }
};

DEVICE i8272_dev = {
    "I8272", i8272_unit, NULL, i8272_mod,
    I8272_MAX_DRIVES, 10, 31, 1, I8272_MAX_DRIVES, I8272_MAX_DRIVES,
    NULL, NULL, &i8272_reset,
    NULL, &i8272_attach, &i8272_detach,
    &i8272_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    i8272_dt, NULL, NULL, NULL, NULL, NULL, &i8272_description
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
            sim_printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp->io_base);
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

    if (uptr == NULL)
        return (-1);
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
t_stat i8272_attach(UNIT *uptr, CONST char *cptr)
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
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && strncmp(header, "IMD", 3)) {
            sim_printf("I8272: Only IMD disk images are supported\n");
            i8272_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
    } else {
        /* create a disk image file in IMD format. */
        if (diskCreate(uptr->fileref, "$Id: i8272.c 1999 2008-07-22 04:25:28Z hharte $") != SCPE_OK) {
            sim_printf("I8272: Failed to create IMD disk.\n");
            i8272_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
        uptr->capac = sim_fsize(uptr->fileref);
    }

    uptr->u3 = IMAGE_TYPE_IMD;

    if (uptr->flags & UNIT_I8272_VERBOSE) {
        sim_printf("I8272%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_IMD ? "IMD" : uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);
    }

    if(uptr->u3 == IMAGE_TYPE_IMD) {
        if (uptr->flags & UNIT_I8272_VERBOSE)
            sim_printf("--------------------------------------------------------\n");
        i8272_info->drive[i].imd = diskOpenEx(uptr->fileref, uptr->flags & UNIT_I8272_VERBOSE,
                                              &i8272_dev, VERBOSE_MSG, VERBOSE_MSG);
        if (uptr->flags & UNIT_I8272_VERBOSE)
            sim_printf("\n");
        if (i8272_info->drive[i].imd == NULL) {
            sim_printf("I8272: IMD disk corrupt.\n");
            i8272_info->drive[i].uptr = NULL;
            return SCPE_OPENERR;
        }
        i8272_info->drive[i].ready = 1;
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
    r = diskClose(&i8272_info->drive[i].imd);
    i8272_info->drive[i].ready = 0;
    if (r != SCPE_OK)
        return r;

    r = detach_unit(uptr);  /* detach unit */
    if (r != SCPE_OK)
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

extern uint8 floorlog2(unsigned int n);

#define I8272_MSR_RQM           (1 << 7)
#define I8272_MSR_DATA_OUT      (1 << 6)
#define I8272_MSR_NON_DMA       (1 << 5)
#define I8272_MSR_FDC_BUSY      (1 << 4)

uint8 I8272_Read(const uint32 Addr)
{
    uint8 cData;
    I8272_DRIVE_INFO    *pDrive;

    pDrive = &i8272_info->drive[i8272_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    /* the switch statement ensures that cData is set in all cases! */
    switch(Addr & 0x3) {
        case I8272_FDC_MSR:
            cData  = i8272_info->fdc_msr | I8272_MSR_RQM;
            if(i8272_info->fdc_phase == CMD_PHASE) {
                cData &= ~I8272_MSR_DATA_OUT;
            } else {
                cData |= I8272_MSR_DATA_OUT;
            }
#if 0
            if(i8272_info->fdc_phase == EXEC_PHASE) {
                cData |= I8272_MSR_FDC_BUSY;
            } else {
                cData |= ~I8272_MSR_FDC_BUSY;
            }
#endif /* 0 hharte */
            sim_debug(STATUS_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                      " RD FDC MSR = 0x%02x\n", PCX, cData);
            break;
        case I8272_FDC_DATA:
            if(i8272_info->fdc_phase == DATA_PHASE) {
                cData = i8272_info->result[i8272_info->result_index];
                sim_debug(VERBOSE_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                          " RD Data, phase=%d, [%d]=0x%02x\n",
                          PCX, i8272_info->fdc_phase, i8272_info->result_index, cData);
                i8272_irq = 0;
                i8272_info->result_index ++;
                if(i8272_info->result_index == i8272_info->result_len) {
                    sim_debug(VERBOSE_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                              " result phase complete.\n", PCX);
                    i8272_info->fdc_phase = CMD_PHASE;
                }
            } else {
                cData = i8272_info->result[0]; /* hack, in theory any value should be ok but this makes "format" work */
                sim_debug(VERBOSE_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                          " error, reading data register when not in data phase. "
                          "Returning 0x%02x\n", PCX, cData);
            }
            break;
        default:
            sim_debug(VERBOSE_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                      " Cannot read register %x\n", PCX, Addr);
            cData = 0xFF;
    }

    return (cData);
}

static const char *messages[0x20] = {
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
    uint32 flags = 0;
    uint32 readlen;
    uint8   disk_read = 0;
    int32 i;

    pDrive = &i8272_info->drive[i8272_info->sel_drive];

    if(pDrive->uptr == NULL) {
        return 0xFF;
    }

    switch(Addr & 0x3) {
        case I8272_FDC_MSR:
            sim_debug(WR_DATA_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                      " WR Drive Select Reg=%02x\n", PCX, cData);
            break;
        case I8272_FDC_DATA:
            i8272_info->fdc_msr &= 0xF0;
            sim_debug(VERBOSE_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                      " WR Data, phase=%d, index=%d\n",
                      PCX, i8272_info->fdc_phase, i8272_info->cmd_index);
            if(i8272_info->fdc_phase == CMD_PHASE) {
                i8272_info->cmd[i8272_info->cmd_index] = cData;

                if(i8272_info->cmd_index == 0) {
                    sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                              " CMD=0x%02x[%s]\n", PCX, cData & 0x1F, messages[cData & 0x1F]);
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
                        if(pDrive->track != i8272_info->cmd[2]) {
                            sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " ERROR: CMD=0x%02x[%s]: Drive: %d, Command wants track %d, "
                                      "but positioner is on track %d.\n",
                                      PCX, i8272_info->cmd[0] & 0x1F,
                                      messages[i8272_info->cmd[0] & 0x1F],
                                      i8272_info->sel_drive, i8272_info->cmd[2], pDrive->track);
                        }

                        pDrive->track = i8272_info->cmd[2];
                        i8272_info->fdc_head = i8272_info->cmd[3] & 1; /* AGN mask to head 0 or 1 */
                        i8272_info->fdc_sector = i8272_info->cmd[4];
                        i8272_info->fdc_sec_len = i8272_info->cmd[5];
                        if(i8272_info->fdc_sec_len > I8272_MAX_N) {
                            sim_debug(ERROR_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " Illegal sector size %d [N=%d]. Reset to %d [N=%d].\n",
                                      PCX, 128 << i8272_info->fdc_sec_len,
                                      i8272_info->fdc_sec_len, 128 << I8272_MAX_N, I8272_MAX_N);
                            i8272_info->fdc_sec_len = I8272_MAX_N;
                        }
                        i8272_info->fdc_eot = i8272_info->cmd[6];
                        i8272_info->fdc_gpl = i8272_info->cmd[7];
                        i8272_info->fdc_dtl = i8272_info->cmd[8];

                        sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " CMD=0x%02x[%s]: Drive: %d, %s %s, C=%d. H=%d. S=%d, N=%d, "
                                  "EOT=%02x, GPL=%02x, DTL=%02x\n", PCX,
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
                                  i8272_info->fdc_dtl);

                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 0x03);
                        i8272_info->fdc_status[0] |= 0x40;

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->imd->track[pDrive->track][i8272_info->fdc_head].logicalCyl[i8272_info->fdc_sector]; /* AGN logicalCyl */
                        i8272_info->result[4] = pDrive->imd->track[pDrive->track][i8272_info->fdc_head].logicalHead[i8272_info->fdc_sector];    /* AGN logicalHead */
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
                        /* Compute the i8272 "N" value from the sectorsize of this              */
                        /* disk's current track - i.e. N = log2(sectsize) - log2(128)           */
                        /* The calculation also works for non-standard format disk images with  */
                        /* sectorsizes of 2048, 4096 and 8192 bytes                             */
                        i8272_info->fdc_sec_len = floorlog2(
                            pDrive->imd->track[pDrive->track][i8272_info->fdc_hds].sectsize) - 7; /* AGN fix to use fdc_hds (was fdc_head)*/
                        /* For now always return the starting sector number   */
                        /* but could return (say) a valid sector number based */
                        /* on elapsed time for a more "realistic" simulation. */
                        /* This would allow disk analysis programs that use   */
                        /* READID to detect non-standard disk formats.        */
                        i8272_info->fdc_sector = pDrive->imd->track[pDrive->track][i8272_info->fdc_hds].start_sector;
                        if((i8272_info->fdc_sec_len == 0xF8) || (i8272_info->fdc_sec_len > I8272_MAX_N)) { /* Error calculating N or N too large */
                            sim_debug(ERROR_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " Illegal sector size N=%d. Reset to 0.\n",
                                      PCX, i8272_info->fdc_sec_len);
                            i8272_info->fdc_sec_len = 0;
                            return 0xFF;
                        }
                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 0x03);

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->imd->track[pDrive->track][i8272_info->fdc_hds].logicalCyl[i8272_info->fdc_sector];  /* AGN logicalCyl */
                        i8272_info->result[4] = pDrive->imd->track[pDrive->track][i8272_info->fdc_hds].logicalHead[i8272_info->fdc_sector]; /* AGN logicalHead */
                        i8272_info->result[5] = i8272_info->fdc_sector;
                        i8272_info->result[6] = i8272_info->fdc_sec_len;
                        break;
                    case I8272_RECALIBRATE: /* RECALIBRATE */
                        i8272_info->sel_drive = i8272_info->cmd[1] & 0x03;
                        pDrive = &i8272_info->drive[i8272_info->sel_drive];
                        if(pDrive->uptr == NULL) {
                            return 0xFF;
                        }

                        pDrive->track = 0;
                        i8272_info->fdc_phase = CMD_PHASE;  /* No result phase */
                        i8272_info->fdc_seek_end = 1;
                        sim_debug(SEEK_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Recalibrate: Drive 0x%02x\n",
                                  PCX, i8272_info->sel_drive);
                        break;
                    case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
                        i8272_info->fdc_mfm = (i8272_info->cmd[0] & 0x40) >> 6;
                        i8272_info->fdc_hds = (i8272_info->cmd[1] & 0x04) >> 2;
                        i8272_info->fdc_head = i8272_info->fdc_hds;
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
                        if(i8272_info->fdc_sec_len > I8272_MAX_N) {
                            sim_debug(ERROR_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " Illegal sector size %d [N=%d]. Reset to %d [N=%d].\n",
                                      PCX, 128 << i8272_info->fdc_sec_len,
                                      i8272_info->fdc_sec_len, 128 << I8272_MAX_N, I8272_MAX_N);
                            i8272_info->fdc_sec_len = I8272_MAX_N;
                        }
                        i8272_info->fdc_sc = i8272_info->cmd[3];
                        i8272_info->fdc_gpl = i8272_info->cmd[4];
                        i8272_info->fdc_fillbyte = i8272_info->cmd[5];

                        sim_debug(FMT_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Format Drive: %d, %s, C=%d. H=%d. N=%d, SC=%d, GPL=%02x, FILL=%02x\n",
                                  PCX,
                                  i8272_info->sel_drive,
                                  i8272_info->fdc_mfm ? "MFM" : "FM",
                                  pDrive->track,
                                  i8272_info->fdc_head,
                                  i8272_info->fdc_sec_len,
                                  i8272_info->fdc_sc,
                                  i8272_info->fdc_gpl,
                                  i8272_info->fdc_fillbyte);

                        i8272_info->fdc_status[0]  = (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->fdc_status[0] |= (i8272_info->sel_drive & 0x03);

                        i8272_info->fdc_status[1]  = 0;
                        i8272_info->fdc_status[2]  = 0;
                        i8272_info->fdc_sectorcount = 0;

                        i8272_info->result[0] = i8272_info->fdc_status[0];
                        i8272_info->result[1] = i8272_info->fdc_status[1];
                        i8272_info->result[2] = i8272_info->fdc_status[2];
                        i8272_info->result[3] = pDrive->track;
                        i8272_info->result[4] = i8272_info->fdc_head;   /* AGN for now we cannot format with logicalHead */
                        i8272_info->result[5] = i8272_info->fdc_sector; /* AGN ditto for logicalCyl */
                        i8272_info->result[6] = i8272_info->fdc_sec_len;
                        break;
                    case I8272_SENSE_INTR_STATUS:   /* SENSE INTERRUPT STATUS */
                        sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Sense Interrupt Status\n", PCX);
                        i8272_info->result[0]  = i8272_info->fdc_seek_end ? 0x20 : 0x00;  /* SEEK_END */
                        i8272_info->result[0] |= i8272_info->sel_drive;
                        i8272_info->result[1]  = pDrive->track;
                        i8272_irq = 0;
                        break;
                    case I8272_SPECIFY: /* SPECIFY */
                        i8272_info->fdc_srt = 16 - ((i8272_info->cmd[1] & 0xF0) >> 4);
                        i8272_info->fdc_hut = (i8272_info->cmd[1] & 0x0F) * 16;
                        i8272_info->fdc_hlt = ((i8272_info->cmd[2] & 0xFE) >> 1) * 2;
                        i8272_info->fdc_nd  = (i8272_info->cmd[2] & 0x01);
                        i8272_info->fdc_phase = CMD_PHASE;  /* No result phase */
                        sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Specify: SRT=%d, HUT=%d, HLT=%d, ND=%s\n",
                                  PCX, i8272_info->fdc_srt,
                                  i8272_info->fdc_hut,
                                  i8272_info->fdc_hlt,
                                  i8272_info->fdc_nd ? "NON-DMA" : "DMA");
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
                            i8272_info->result[0] |= DRIVE_STATUS_TWO_SIDED;    /* Two-sided?       */
                        }
                        if(imdIsWriteLocked(pDrive->imd)) {
                            i8272_info->result[0] |= DRIVE_STATUS_WP;           /* Write Protected? */
                        }
                        i8272_info->result[0] |= (i8272_info->fdc_hds & 1) << 2;
                        i8272_info->result[0] |= (i8272_info->sel_drive & 0x03);
                        i8272_info->result[0] |= (pDrive->track == 0) ? DRIVE_STATUS_TRACK0 : 0x00; /* Track 0 */
                        sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Sense Drive Status = 0x%02x\n", PCX, i8272_info->result[0]);
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
                        i8272_info->fdc_head = i8272_info->fdc_hds; /*AGN seek should save the head */
                        i8272_info->fdc_seek_end = 1;
                        sim_debug(SEEK_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                  " Seek Drive: %d, %s %s, C=%d. Skip Deleted Data=%s Head Select=%s\n",
                                  PCX,
                                  i8272_info->sel_drive,
                                  i8272_info->fdc_mt ? "Multi" : "Single",
                                  i8272_info->fdc_mfm ? "MFM" : "FM",
                                  i8272_info->cmd[2],
                                  i8272_info->fdc_sk ? "True" : "False",
                                  i8272_info->fdc_hds ? "True" : "False");
                        break;
                    default:    /* INVALID */
                        break;
                }

                if(i8272_info->fdc_phase == EXEC_PHASE) {
                    switch(i8272_info->cmd[0] & 0x1F) {
                        case I8272_READ_TRACK:
                            sim_printf("I8272: " ADDRESS_FORMAT " Read a track (untested.)" NLP, PCX);
                            i8272_info->fdc_sector = 1; /* Read entire track from sector 1...eot */
                            /* fall through */

                        case I8272_READ_DATA:
                            /* fall through */
                            
                        case I8272_READ_DELETED_DATA:
                            disk_read = 1;
                            /* fall through */

                        case I8272_WRITE_DATA:
                        case I8272_WRITE_DELETED_DATA:
                            for(;i8272_info->fdc_sector<=i8272_info->fdc_eot;i8272_info->fdc_sector++) {
                                sim_debug(RD_DATA_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                          " %s Data, sector: %d sector len=%d\n",
                                          PCX, disk_read ? "RD" : "WR",
                                          i8272_info->fdc_sector,
                                          128 << i8272_info->fdc_sec_len);

                                if(pDrive->imd == NULL) {
                                    sim_printf(".imd is NULL!" NLP);
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
                                        PutByteDMA(i8272_info->fdc_dma_addr, sdata.raw[i]);
                                        i8272_info->fdc_dma_addr++;
                                    }
                                    sim_debug(RD_DATA_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                              " T:%d/H:%d/S:%d/L:%4d: Data transferred to RAM at 0x%06x\n",
                                              PCX, pDrive->track,
                                              i8272_info->fdc_head,
                                              i8272_info->fdc_sector,
                                              128 << i8272_info->fdc_sec_len,
                                              i8272_info->fdc_dma_addr - i);
                                } else { /* Write */
                                    for(i=0;i<(128 << i8272_info->fdc_sec_len);i++) {
                                        sdata.raw[i] = GetByteDMA(i8272_info->fdc_dma_addr);
                                        i8272_info->fdc_dma_addr++;
                                    }
                                    sim_debug(WR_DATA_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                              " Data transferred from RAM at 0x%06x\n",
                                              PCX, i8272_info->fdc_dma_addr);
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
                            }
                            break;
                        case I8272_FORMAT_TRACK:    /* FORMAT A TRACK */
                            for(i8272_info->fdc_sector = 1;i8272_info->fdc_sector<=i8272_info->fdc_sc;i8272_info->fdc_sector++) {
                                sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                          " Format Track %d, Sector=%d, len=%d\n", PCX, pDrive->track, i8272_info->fdc_sector, 128 << i8272_info->fdc_sec_len);

                                if(i8272_info->fdc_sectorcount >= I8272_MAX_SECTOR) {
                                    sim_debug(ERROR_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                              " Illegal sector count\n", PCX);
                                    i8272_info->fdc_sectorcount = 0;
                                }
                                i8272_info->fdc_sectormap[i8272_info->fdc_sectorcount] = i8272_info->fdc_sector;
                                i8272_info->fdc_sectorcount++;
                                if(i8272_info->fdc_sectorcount == i8272_info->fdc_sc) {
                                    trackWrite(pDrive->imd,
                                        pDrive->track,
                                        i8272_info->fdc_head,
                                        i8272_info->fdc_sc,
                                        128 << i8272_info->fdc_sec_len,
                                        i8272_info->fdc_sectormap,
                                        i8272_info->fdc_mfm ? 3 : 0,
                                        i8272_info->fdc_fillbyte,
                                        &flags);

                                    /* Recalculate disk size */
                                    pDrive->uptr->capac = sim_fsize(pDrive->uptr->fileref);

                                }
                            }
                            break;

                        case I8272_SCAN_LOW_EQUAL:  /* SCAN LOW OR EQUAL */
                        case I8272_SCAN_HIGH_EQUAL: /* SCAN HIGH OR EQUAL */
                        case I8272_SCAN_EQUAL:  /* SCAN EQUAL */
                            sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " Scan Data\n", PCX);
                            sim_debug(ERROR_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " ERROR: Scan not implemented.\n", PCX);
                            break;
                        case I8272_READ_ID:  /* READ ID */
                            sim_debug(CMD_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT
                                      " READ ID Drive %d result ST0=%02x ST1=%02x ST2=%02x "
                                      "C=%d H=%d R=%02x N=%d\n", PCX,
                                      i8272_info->sel_drive,
                                      i8272_info->result[0], i8272_info->result[1],
                                      i8272_info->result[2], i8272_info->result[3],
                                      i8272_info->result[4], i8272_info->result[5],
                                      i8272_info->result[6]);
                            break;

                        default:
                            break;
                    }
                }

                if(i8272_info->result_len != 0) {
                    i8272_info->fdc_phase ++;
                } else {
                    i8272_info->fdc_phase = CMD_PHASE;
                }

                i8272_info->result_index = 0;
                if((i8272_info->cmd[0] & 0x1F) != I8272_SENSE_INTR_STATUS) {
                    raise_i8272_interrupt();
                }
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

extern void raise_disk1a_interrupt(void);

static void raise_i8272_interrupt(void)
{
    sim_debug(IRQ_MSG, &i8272_dev, "I8272: " ADDRESS_FORMAT " FDC Interrupt\n", PCX);
    i8272_irq = 1;
    raise_disk1a_interrupt();
}
