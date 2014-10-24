/*************************************************************************
 *                                                                       *
 * $Id: s100_mdsad.c 1995 2008-07-15 03:59:13Z hharte $                  *
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
 *     Northstar MDS-AD Disk Controller module for SIMH                  *
 * Only Double-Density is supported for now.                             *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*#define DBG_MSG*/
#include "altairz80_defs.h"
#include "sim_imd.h"

#if defined (_WIN32)
#include <windows.h>
#endif

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

/* Debug flags */
#define ERROR_MSG           (1 << 0)
#define SEEK_MSG            (1 << 1)
#define CMD_MSG             (1 << 2)
#define RD_DATA_MSG         (1 << 3)
#define WR_DATA_MSG         (1 << 4)
#define STATUS_MSG          (1 << 5)
#define ORDERS_MSG          (1 << 6)
#define RD_DATA_DETAIL_MSG  (1 << 7)
#define WR_DATA_DETAIL_MSG  (1 << 8)

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define MDSAD_MAX_DRIVES        4
#define MDSAD_SECTOR_LEN        512
#define MDSAD_SECTORS_PER_TRACK 10
#define MDSAD_TRACKS            35
#define MDSAD_RAW_LEN           (32 + 2 + MDSAD_SECTOR_LEN + 1)

typedef union {
    struct {
        uint8 zeros[32];
        uint8 sync[2];
        uint8 data[MDSAD_SECTOR_LEN];
        uint8 checksum;
    } u;
    uint8 raw[MDSAD_RAW_LEN];

} SECTOR_FORMAT;

typedef struct {
    UNIT *uptr;
    uint8 track;
    uint8 wp;       /* Disk write protected */
    uint8 sector;   /* Current Sector number */
    uint32 sector_wait_count;
} MDSAD_DRIVE_INFO;

typedef struct {
    uint8 dd;       /* Controls density on write DD=1 for double density and DD=0 for single density. */
    uint8 ss;       /* Specifies the side of a double-sided diskette. The bottom side (and only side of a single-sided diskette) is selected when SS=0. The second (top) side is selected when SS=1. */
    uint8 dp;       /* has shared use. During stepping operations, DP=O specifies a step out and DP=1 specifies a step in. During write operations, write procompensation is invoked if and only if DP=1. */
    uint8 st;       /* controls the level of the head step signal to the disk drives. */
    uint8 ds;       /* is the drive select field, encoded as follows: */
                    /* 0=no drive selected
                     * 1=drive 1 selected
                     * 2=drive 2 selected
                     * 4=drive 3 selected
                     * 8=drive 4 selected
                     */
} ORDERS;

typedef struct {
    uint8 sf;       /* Sector Flag: set when sector hole detected, reset by software. */
    uint8 ix;       /* Index Detect: true if index hole detected during previous sector. */
    uint8 dd;       /* Double Density Indicator: true if data being read is encoded in double density. */
    uint8 mo;       /* Motor On: true while motor(s) are on. */
} COM_STATUS;

typedef struct {
    uint8 wi;       /* Window: true during 96-microsecond window at beginning of sector. */
    uint8 re;       /* Read Enable: true while phase-locked loop is enabled. */
    uint8 sp;       /* Spare: reserved for future use. */
    uint8 bd;       /* Body: set when sync character is detected. */
} A_STATUS;

typedef struct {
    uint8 wr;       /* Write: true during valid write operation. */
    uint8 sp;       /* Spare: reserved for future use. */
    uint8 wp;       /* Write Protect: true while the diskette installed in the selected drive is write protected. */
    uint8 t0;       /* Track 0: true if selected drive is at track zero. */
} B_STATUS;

typedef struct {
    uint8 sc;       /* Sector Counter: indicates the current sector position. */
} C_STATUS;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */

    ORDERS      orders;
    COM_STATUS  com_status;
    A_STATUS    a_status;
    B_STATUS    b_status;
    C_STATUS    c_status;

    uint8 int_enable;   /* Interrupt Enable */
    uint32 datacount;   /* Number of data bytes transferred from controller for current sector */
    MDSAD_DRIVE_INFO drive[MDSAD_MAX_DRIVES];
} MDSAD_INFO;

static MDSAD_INFO mdsad_info_data = { { 0xE800, 1024, 0, 0 } };
static MDSAD_INFO *mdsad_info = &mdsad_info_data;

static SECTOR_FORMAT sdata;

#define UNIT_V_MDSAD_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages       */
#define UNIT_MDSAD_VERBOSE      (1 << UNIT_V_MDSAD_VERBOSE)
#define MDSAD_CAPACITY          (70*10*MDSAD_SECTOR_LEN)    /* Default North Star Disk Capacity */

/* MDS-AD Controller Subcases */
#define MDSAD_READ_ROM      0
#define MDSAD_WRITE_DATA    1
#define MDSAD_CTLR_ORDERS   2
#define MDSAD_CTLR_COMMAND  3

/* MDS-AD Controller Commands */
#define MDSAD_CMD_NOP       0
#define MDSAD_CMD_RESET_SF  1
#define MDSAD_CMD_INTR_DIS  2
#define MDSAD_CMD_INTR_ARM  3
#define MDSAD_CMD_SET_BODY  4
#define MDSAD_CMD_MOTORS_ON 5
#define MDSAD_CMD_BEGIN_WR  6
#define MDSAD_CMD_RESET     7

/* MDS-AD Data returned on DI bus */
#define MDSAD_A_STATUS      1
#define MDSAD_B_STATUS      2
#define MDSAD_C_STATUS      3
#define MDSAD_READ_DATA     4

/* MDS-AD status byte masks */
/* A-Status */
#define MDSAD_A_SF          0x80
#define MDSAD_A_IX          0x40
#define MDSAD_A_DD          0x20
#define MDSAD_A_MO          0x10
#define MDSAD_A_WI          0x08
#define MDSAD_A_RE          0x04
#define MDSAD_A_SP          0x02
#define MDSAD_A_BD          0x01

/* B-Status */
#define MDSAD_B_SF          0x80
#define MDSAD_B_IX          0x40
#define MDSAD_B_DD          0x20
#define MDSAD_B_MO          0x10
#define MDSAD_B_WR          0x08
#define MDSAD_B_SP          0x04
#define MDSAD_B_WP          0x02
#define MDSAD_B_T0          0x01

/* C-Status */
#define MDSAD_C_SF          0x80
#define MDSAD_C_IX          0x40
#define MDSAD_C_DD          0x20
#define MDSAD_C_MO          0x10
#define MDSAD_C_SC          0x0f

/* Local function prototypes */
static t_stat mdsad_reset(DEVICE *mdsad_dev);
static t_stat mdsad_attach(UNIT *uptr, char *cptr);
static t_stat mdsad_detach(UNIT *uptr);
static t_stat mdsad_boot(int32 unitno, DEVICE *dptr);
static uint8 MDSAD_Read(const uint32 Addr);

static int32 mdsaddev(const int32 Addr, const int32 rw, const int32 data);

static UNIT mdsad_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSAD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSAD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSAD_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSAD_CAPACITY) }
};

static REG mdsad_reg[] = {
    { NULL }
};

#define MDSAD_NAME  "North Star Floppy Controller MDSAD"

static MTAB mdsad_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "MEMBASE",  "MEMBASE",
        &set_membase, &show_membase, NULL, "Sets disk controller memory base address"   },
    /* quiet, no warning messages       */
    { UNIT_MDSAD_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " MDSAD_NAME "n"                },
    /* verbose, show warning messages   */
    { UNIT_MDSAD_VERBOSE,   UNIT_MDSAD_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " MDSAD_NAME "n"                   },
    { 0 }
};

/* Debug Flags */
static DEBTAB mdsad_dt[] = {
    { "ERROR",      ERROR_MSG,          "Error messages"        },
    { "SEEK",       SEEK_MSG,           "Seek messages"         },
    { "CMD",        CMD_MSG,            "Command messages"      },
    { "READ",       RD_DATA_MSG,        "Read messages"         },
    { "WRITE",      WR_DATA_MSG,        "Write messages"        },
    { "STATUS",     STATUS_MSG,         "Status messages"       },
    { "ORDERS",     ORDERS_MSG,         "Orders messages"       },
    { "RDDETAIL",   RD_DATA_DETAIL_MSG, "Read detail messages"  },
    { "WRDETAIL",   WR_DATA_DETAIL_MSG, "Write detail messags"  },
    { NULL,         0                                           }
};

DEVICE mdsad_dev = {
    "MDSAD", mdsad_unit, mdsad_reg, mdsad_mod,
    MDSAD_MAX_DRIVES, 10, 31, 1, MDSAD_MAX_DRIVES, MDSAD_MAX_DRIVES,
    NULL, NULL, &mdsad_reset,
    &mdsad_boot, &mdsad_attach, &mdsad_detach,
    &mdsad_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG,
    mdsad_dt, NULL, "North Star Floppy Controller MDSAD"
};

/* Reset routine */
static t_stat mdsad_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) {
        sim_map_resource(pnp->mem_base, pnp->mem_size,
            RESOURCE_TYPE_MEMORY, &mdsaddev, TRUE);
    } else {
        /* Connect MDSAD at base address */
        if(sim_map_resource(pnp->mem_base, pnp->mem_size,
            RESOURCE_TYPE_MEMORY, &mdsaddev, FALSE) != 0) {
            sim_printf("%s: error mapping resource at 0x%04x\n",
                __FUNCTION__, pnp->mem_base);
            dptr->flags |= DEV_DIS;
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

/* Attach routine */
static t_stat mdsad_attach(UNIT *uptr, char *cptr)
{
    char header[4];
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if(r != SCPE_OK)                /* error?       */
        return r;

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = MDSAD_CAPACITY;
    }

    for(i = 0; i < MDSAD_MAX_DRIVES; i++) {
        mdsad_info->drive[i].uptr = &mdsad_dev.units[i];
    }

    for(i = 0; i < MDSAD_MAX_DRIVES; i++) {
        if(mdsad_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            sim_printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            mdsad_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_MDSAD_VERBOSE)
        sim_printf("MDSAD%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    return SCPE_OK;
}


/* Detach routine */
static t_stat mdsad_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for(i = 0; i < MDSAD_MAX_DRIVES; i++) {
        if(mdsad_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= MDSAD_MAX_DRIVES)
        return SCPE_ARG;

    DBG_PRINT(("Detach MDSAD%d\n", i));

    r = detach_unit(uptr);  /* detach unit */
    if(r != SCPE_OK)
        return r;

    mdsad_dev.units[i].fileref = NULL;
    return SCPE_OK;
}

static t_stat mdsad_boot(int32 unitno, DEVICE *dptr)
{

    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    DBG_PRINT(("Booting MDSAD Controller at 0x%04x, unit %d" NLP,
        pnp->mem_base+1+(unitno&3), unitno & 3));

    /* Unit 3 can't be booted yet.  This involves modifying the A register. */
    *((int32 *) sim_PC->loc) = pnp->mem_base+1+(unitno&3);
    return SCPE_OK;
}

static int32 mdsaddev(const int32 Addr, const int32 rw, const int32 data)
{
    if(rw == 0) { /* Read */
        return(MDSAD_Read(Addr));
    } else {    /* Write */
        DBG_PRINT(("MDSAD: write attempt at 0x%04x ignored." NLP, Addr));
        return (-1);
    }
}

/* This ROM image is taken from the Solace Emulator, which uses         */
/* a ROM from a "Micro Complex Phase Lock II" dual-                     */
/* density controller card.  It is supposedly compatible with the       */
/* Northstar-designed dual density floppy controller.  It has the       */
/* interesting property that by jumping to base_addr+0 (or +1) and      */
/* it boots from floppy 0; jump to base_addr+2 you boot from floppy 1;  */
/* jump to base_addr+3 and you boot from floppy 2.  You can boot from   */
/* floppy 3 by loading A with 08H and jumping to base_addr+7.           */
static uint8 mdsad_rom[] = {
    0x44, 0x01, 0x01, 0x01, 0x82, 0x84, 0x78, 0xE6, 0x07, 0x4F, 0x00, 0x31, 0x30, 0x00, 0x21, 0x29, /* 0x00 */
    0x00, 0xE5, 0x21, 0x2C, 0xC2, 0xE5, 0x21, 0x77, 0x13, 0xE5, 0x21, 0xC9, 0x1A, 0xE5, 0xCD, 0x28, /* 0x10 */
    0x00, 0x21, 0x30, 0x00, 0x5B, 0x52, 0x44, 0x54, 0x5D, 0x3A, 0x27, 0x00, 0x57, 0xC3, 0x29, 0x00, /* 0x20 */
    0x14, 0x14, 0x1E, 0x15, 0x1A, 0x26, 0x30, 0xCD, 0xD9, 0x00, 0x42, 0x05, 0x0A, 0xCD, 0xD4, 0x00, /* 0x30 */
    0x2E, 0x0D, 0x2D, 0xCA, 0x43, 0x00, 0xCD, 0xD7, 0x00, 0x1A, 0xE6, 0x40, 0xCA, 0x42, 0x00, 0x3E, /* 0x40 */
    0x0A, 0xF5, 0xCD, 0xC1, 0x00, 0x1E, 0x20, 0x1A, 0xE6, 0x01, 0xC2, 0x63, 0x00, 0xCD, 0xC5, 0x00, /* 0x50 */
    0xC3, 0x55, 0x00, 0x2E, 0x04, 0xCD, 0xE7, 0x00, 0x1E, 0x10, 0x1A, 0xE6, 0x04, 0xCA, 0x68, 0x00, /* 0x60 */
    0x3E, 0x09, 0x3D, 0xC2, 0x72, 0x00, 0x1A, 0xE6, 0x20, 0xC2, 0x84, 0x00, 0xCD, 0xC1, 0x00, 0x2E, /* 0x70 */
    0x08, 0xCD, 0xE7, 0x00, 0x06, 0xA3, 0x1E, 0x10, 0x05, 0xCA, 0xF4, 0x00, 0x1A, 0x0F, 0xD2, 0x88, /* 0x80 */
    0x00, 0x1E, 0x40, 0x1A, 0x67, 0x2E, 0x00, 0x36, 0x59, 0x07, 0x47, 0x23, 0x1A, 0x77, 0xA8, 0x07, /* 0x90 */
    0x47, 0x2C, 0xC2, 0x9C, 0x00, 0x24, 0x1A, 0x77, 0xA8, 0x07, 0x47, 0x2C, 0xC2, 0xA6, 0x00, 0x1A, /* 0xA0 */
    0xA8, 0xC2, 0xF4, 0x00, 0x25, 0x2E, 0x03, 0x71, 0x2D, 0x36, 0x59, 0xC2, 0xB8, 0x00, 0x2E, 0x0A, /* 0xB0 */
    0xE9, 0x3E, 0x20, 0x81, 0x4F, 0x0A, 0x3E, 0x10, 0x81, 0x4F, 0x0A, 0x3E, 0xF0, 0x81, 0x4F, 0x0A, /* 0xC0 */
    0x79, 0xE6, 0x0F, 0x4F, 0xCD, 0xD7, 0x00, 0x26, 0x01, 0x1E, 0x11, 0x1A, 0x1D, 0x1A, 0xB7, 0xF2, /* 0xD0 */
    0xDD, 0x00, 0x25, 0xC2, 0xD9, 0x00, 0xC9, 0xCD, 0xD7, 0x00, 0x1E, 0x35, 0x1A, 0xE6, 0x0F, 0xBD, /* 0xE0 */
    0xC2, 0xE7, 0x00, 0xC9, 0xF1, 0x3D, 0xF5, 0xC2, 0x55, 0x00, 0xC3, 0xFA, 0x00, 0x52, 0x44, 0x54  /* 0xF0 */
};

static void showdata(int32 isRead) {
    int32 i;
    sim_printf("MDSAD: " ADDRESS_FORMAT " %s Sector =" NLP "\t", PCX, isRead ? "Read" : "Write");
    for(i=0; i < MDSAD_SECTOR_LEN; i++) {
        sim_printf("%02X ", sdata.u.data[i]);
        if(((i+1) & 0xf) == 0)
            sim_printf(NLP "\t");
    }
    sim_printf(NLP);
}

static int checksum;
static uint32 sec_offset;

static uint32 calculate_mdsad_sec_offset(uint8 track, uint8 head, uint8 sector)
{
    if(mdsad_info->orders.ss == 0) {
        return ((track * (MDSAD_SECTOR_LEN * MDSAD_SECTORS_PER_TRACK)) + (sector * MDSAD_SECTOR_LEN));
    } else {
        return ((((MDSAD_TRACKS-1) - track) * (MDSAD_SECTOR_LEN * MDSAD_SECTORS_PER_TRACK)) +
                 ((MDSAD_SECTOR_LEN * MDSAD_SECTORS_PER_TRACK) * MDSAD_TRACKS) + /* Skip over side 0 */
                 (sector * MDSAD_SECTOR_LEN)); /* Sector offset from beginning of track. */
    }
}

static uint8 MDSAD_Read(const uint32 Addr)
{
    uint8 cData;
    uint8 ds;
    MDSAD_DRIVE_INFO *pDrive;
    int32 rtn;

    cData = 0x00;

    pDrive = &mdsad_info->drive[mdsad_info->orders.ds];

    switch( (Addr & 0x300) >> 8 ) {
        case MDSAD_READ_ROM:
            cData = mdsad_rom[Addr & 0xFF];
            break;
        case MDSAD_WRITE_DATA:
        {
            if(mdsad_info->datacount == 0) {
                sim_debug(WR_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                          " WRITE Start:  Drive: %d, Track=%d, Head=%d, Sector=%d\n",
                          PCX, mdsad_info->orders.ds, pDrive->track,
                          mdsad_info->orders.ss, pDrive->sector);

                sec_offset = calculate_mdsad_sec_offset(pDrive->track,
                    mdsad_info->orders.ss,
                    pDrive->sector);

            }

            DBG_PRINT(("MDSAD: " ADDRESS_FORMAT
                " WRITE-DATA[offset:%06x+%03x]=%02x" NLP,
                PCX, sec_offset, mdsad_info->datacount, Addr & 0xFF));
            mdsad_info->datacount++;
            if(mdsad_info->datacount < MDSAD_RAW_LEN)
                sdata.raw[mdsad_info->datacount] = Addr & 0xFF;

            if(mdsad_info->datacount == (MDSAD_RAW_LEN - 1)) {
                sim_debug(WR_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                          " Write Complete\n", PCX);

                if ((pDrive->uptr == NULL) || (pDrive->uptr->fileref == NULL)) {
                    sim_debug(WR_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " Drive: %d not attached - write ignored.\n", PCX, mdsad_info->orders.ds);
                    return 0x00;
                }
                if(mdsad_dev.dctrl & WR_DATA_DETAIL_MSG)
                    showdata(FALSE);
                switch((pDrive->uptr)->u3)
                {
                    case IMAGE_TYPE_DSK:
                        if(pDrive->uptr->fileref == NULL) {
                            sim_printf(".fileref is NULL!" NLP);
                        } else {
                            sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET);
                            sim_fwrite(sdata.u.data, 1, MDSAD_SECTOR_LEN,
                                (pDrive->uptr)->fileref);
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
            break;
        }
        case MDSAD_CTLR_ORDERS:
            mdsad_info->orders.dd = (Addr & 0x80) >> 7;
            mdsad_info->orders.ss = (Addr & 0x40) >> 6;
            mdsad_info->orders.dp = (Addr & 0x20) >> 5;
            mdsad_info->orders.st = (Addr & 0x10) >> 4;
            mdsad_info->orders.ds = (Addr & 0x0F);

            ds = mdsad_info->orders.ds;
            switch(mdsad_info->orders.ds) {
                case 0:
                case 1:
                    mdsad_info->orders.ds = 0;
                    break;
                case 2:
                    mdsad_info->orders.ds = 1;
                    break;
                case 4:
                    mdsad_info->orders.ds = 2;
                    break;
                case 8:
                    mdsad_info->orders.ds = 3;
                    break;
            }

            if(mdsad_info->orders.ds != (mdsad_info->orders.ds & 0x03)) {
                sim_debug(ERROR_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                          " Controller Orders update drive %x\n", PCX, mdsad_info->orders.ds);
                mdsad_info->orders.ds &= 0x03;
            }
            sim_debug(ORDERS_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                      " Controller Orders: Drive=%x[%x], DD=%d, SS=%d, DP=%d, ST=%d\n",
                      PCX, mdsad_info->orders.ds, ds, mdsad_info->orders.dd,
                      mdsad_info->orders.ss, mdsad_info->orders.dp, mdsad_info->orders.st);

            /* use latest selected drive */
            pDrive = &mdsad_info->drive[mdsad_info->orders.ds];

            if(mdsad_info->orders.st == 1) {
                if(mdsad_info->orders.dp == 0) {
                    sim_debug(SEEK_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " Step out: Track=%d%s\n", PCX, pDrive->track,
                              pDrive->track == 0 ? "[Warn: already at 0]" : "");
                    if(pDrive->track > 0) /* anything to do? */
                        pDrive->track--;
                } else {
                    sim_debug(SEEK_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " Step  in: Track=%d%s\n", PCX, pDrive->track,
                              pDrive->track == (MDSAD_TRACKS - 1) ? "[Warn: already at highest track]" : "");
                    if(pDrive->track < (MDSAD_TRACKS - 1)) /* anything to do? */
                        pDrive->track++;
                }
            }
            /* always update t0 */
            mdsad_info->b_status.t0 = (pDrive->track == 0);
            break;
        case MDSAD_CTLR_COMMAND:
/*          sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT " DM=%x\n", PCX, (Addr & 0xF0) >> 4); */
            switch(Addr & 0x0F) {
                case MDSAD_CMD_MOTORS_ON:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Motors On\n", PCX);
                    mdsad_info->com_status.mo = 1;      /* Turn motors on */
                    break;

                case MDSAD_CMD_NOP:
                    pDrive->sector_wait_count++;
                    switch(pDrive->sector_wait_count) {
                        case 10:
                        {
                            mdsad_info->com_status.sf = 1;
                            mdsad_info->a_status.wi = 0;
                            mdsad_info->a_status.re = 0;
                            mdsad_info->a_status.bd = 0;
                            pDrive->sector_wait_count = 0;
                            pDrive->sector++;
                            if(pDrive->sector >= MDSAD_SECTORS_PER_TRACK) {
                                pDrive->sector = 0;
                                mdsad_info->com_status.ix = 1;
                            } else {
                                mdsad_info->com_status.ix = 0;
                            }
                            break;
                        }
                        case 2:
                            mdsad_info->a_status.wi = 1;
                            break;
                        case 3:
                            mdsad_info->a_status.re = 1;
                            mdsad_info->a_status.bd = 1;
                            break;
                        default:
                            break;
                    }
                    break;
                case MDSAD_CMD_RESET_SF:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Reset Sector Flag\n", PCX);
                    mdsad_info->com_status.sf = 0;
                    mdsad_info->datacount = 0;
                    break;
                case MDSAD_CMD_INTR_DIS:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Disarm Interrupt\n", PCX);
                    mdsad_info->int_enable = 0;
                    break;
                case MDSAD_CMD_INTR_ARM:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Arm Interrupt\n", PCX);
                    mdsad_info->int_enable = 1;
                    break;
                case MDSAD_CMD_SET_BODY:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Set Body (Diagnostic)\n", PCX);
                    break;
                case MDSAD_CMD_BEGIN_WR:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Begin Write\n", PCX);
                    break;
                case MDSAD_CMD_RESET:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " CMD=Reset Controller\n", PCX);
                    mdsad_info->com_status.mo = 0;  /* Turn motors off */
                    break;
                default:
                    sim_debug(CMD_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " Unsupported CMD=0x%x\n", PCX, Addr & 0x0F);
                    break;
            }

            /* Always Double-Density for now... */
            mdsad_info->com_status.dd = 1;

            cData  = (mdsad_info->com_status.sf & 1) << 7;
            cData |= (mdsad_info->com_status.ix & 1) << 6;
            cData |= (mdsad_info->com_status.dd & 1) << 5;
            cData |= (mdsad_info->com_status.mo & 1) << 4;

            mdsad_info->c_status.sc = pDrive->sector;

            switch( (Addr & 0xF0) >> 4) {
                case MDSAD_A_STATUS:    /* A-STATUS */
                    cData |= (mdsad_info->a_status.wi & 1) << 3;
                    cData |= (mdsad_info->a_status.re & 1) << 2;
                    cData |= (mdsad_info->a_status.sp & 1) << 1;
                    cData |= (mdsad_info->a_status.bd & 1);
                    sim_debug(STATUS_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " A-Status = <%s %s %s %s %s %s %s %s>\n",
                              PCX,
                              cData & MDSAD_A_SF ? "SF" : "  ",
                              cData & MDSAD_A_IX ? "IX" : "  ",
                              cData & MDSAD_A_DD ? "DD" : "  ",
                              cData & MDSAD_A_MO ? "MO" : "  ",
                              cData & MDSAD_A_WI ? "WI" : "  ",
                              cData & MDSAD_A_RE ? "RE" : "  ",
                              cData & MDSAD_A_SP ? "SP" : "  ",
                              cData & MDSAD_A_BD ? "BD" : "  ");
                    break;
                case MDSAD_B_STATUS:    /* B-STATUS */
                    cData |= (mdsad_info->b_status.wr & 1) << 3;
                    cData |= (mdsad_info->b_status.sp & 1) << 2;
                    cData |= (mdsad_info->b_status.wp & 1) << 1;
                    cData |= (mdsad_info->b_status.t0 & 1);
                    sim_debug(STATUS_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " B-Status = <%s %s %s %s %s %s %s %s>\n",
                              PCX,
                              cData & MDSAD_B_SF ? "SF" : "  ",
                              cData & MDSAD_B_IX ? "IX" : "  ",
                              cData & MDSAD_B_DD ? "DD" : "  ",
                              cData & MDSAD_B_MO ? "MO" : "  ",
                              cData & MDSAD_B_WR ? "WR" : "  ",
                              cData & MDSAD_B_SP ? "SP" : "  ",
                              cData & MDSAD_B_WP ? "WP" : "  ",
                              cData & MDSAD_B_T0 ? "T0" : "  ");
                    break;
                case MDSAD_C_STATUS:    /* C-STATUS */
                    cData |= (mdsad_info->c_status.sc & 0xF);
                    sim_debug(STATUS_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                              " C-Status = <%s %s %s %s %i>\n",
                              PCX,
                              cData & MDSAD_C_SF ? "SF" : "  ",
                              cData & MDSAD_C_IX ? "IX" : "  ",
                              cData & MDSAD_C_DD ? "DD" : "  ",
                              cData & MDSAD_C_MO ? "MO" : "  ",
                              cData & MDSAD_C_SC);
                    break;
                case MDSAD_READ_DATA:   /* READ DATA */
                {
                    if(mdsad_info->datacount == 0) {
                        sim_debug(RD_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                                  " READ Start:  Drive: %d, Track=%d, Head=%d, Sector=%d\n",
                                  PCX,
                                  mdsad_info->orders.ds,
                                  pDrive->track,
                                  mdsad_info->orders.ss,
                                  pDrive->sector);

                            checksum = 0;

                            sec_offset = calculate_mdsad_sec_offset(pDrive->track,
                                mdsad_info->orders.ss,
                                pDrive->sector);

                            if ((pDrive->uptr == NULL) ||
                                    (pDrive->uptr->fileref == NULL)) {
                                sim_debug(RD_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                                          " Drive: %d not attached - read ignored.\n",
                                          PCX, mdsad_info->orders.ds);
                                return 0xe5;
                            }

                            switch((pDrive->uptr)->u3)
                            {
                                case IMAGE_TYPE_DSK:
                                    if(pDrive->uptr->fileref == NULL) {
                                        sim_printf(".fileref is NULL!" NLP);
                                    } else {
                                        sim_fseek((pDrive->uptr)->fileref,
                                            sec_offset, SEEK_SET);
                                        rtn = sim_fread(&sdata.u.data[0], 1, MDSAD_SECTOR_LEN,
                                            (pDrive->uptr)->fileref);
                                        if (rtn != MDSAD_SECTOR_LEN) {
                                            sim_debug(ERROR_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                                                      " READ: sim_fread error.\n", PCX);
                                        }
                                    }
                                    break;
                                case IMAGE_TYPE_CPT:
                                    sim_printf("%s: CPT Format not supported"
                                        NLP, __FUNCTION__);
                                    break;
                                default:
                                    sim_printf("%s: Unknown image Format"
                                        NLP, __FUNCTION__);
                                    break;
                            }
                            if(mdsad_dev.dctrl & RD_DATA_DETAIL_MSG)
                                showdata(TRUE);
                    }

                    if(mdsad_info->datacount < MDSAD_SECTOR_LEN) {
                        cData = sdata.u.data[mdsad_info->datacount];

                        /* Exclusive OR */
                        checksum ^= cData;
                        /* Rotate Left Circular */
                        checksum = ((checksum << 1) | ((checksum & 0x80) != 0)) & 0xff;

                        DBG_PRINT(("MDSAD: " ADDRESS_FORMAT
                            " READ-DATA[offset:%06x+%03x]=%02x" NLP,
                            PCX, sec_offset, mdsad_info->datacount, cData));
                    } else { /* checksum */
                        cData = checksum;
                        sim_debug(RD_DATA_MSG, &mdsad_dev, "MDSAD: " ADDRESS_FORMAT
                                  " READ-DATA: Checksum is: 0x%02x\n",
                                  PCX, cData);
                    }

                    mdsad_info->datacount++;
                    break;
                }
                default:
                    DBG_PRINT(("MDSAD: " ADDRESS_FORMAT
                        " Invalid DM=%x" NLP, PCX, Addr & 0xF));
                    break;
            }

            break;
    }
    return (cData);
}
