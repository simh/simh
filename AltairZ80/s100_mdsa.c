/*************************************************************************
 * s100_mdsa.c: North Star Single Density Controller Emulation
 *
 * Created by Mike Douglas
 * Based on s100_mdsad.c written by Howard Harte
 *
 * Module Description:
 *     Northstar MDS-A Single Density Disk Controller module for SIMH
 * 
 * Environment:
 *     User mode only 
 *
 *************************************************************************/

/*#define DBG_MSG*/
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
#define ERROR_MSG           (1 << 0)
#define SEEK_MSG            (1 << 1)
#define CMD_MSG             (1 << 2)
#define RD_DATA_MSG         (1 << 3)
#define WR_DATA_MSG         (1 << 4)
#define STATUS_MSG          (1 << 5)
#define RD_DATA_DETAIL_MSG  (1 << 6)
#define WR_DATA_DETAIL_MSG  (1 << 7)

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define MDSA_MAX_DRIVES        3
#define MDSA_SECTOR_LEN        256
#define MDSA_SECTORS_PER_TRACK 10
#define MDSA_TRACKS            35
#define MDSA_RAW_LEN           (16 + 1 + MDSA_SECTOR_LEN + 1)

typedef union {
    struct {
        uint8 zeros[16];
        uint8 sync[1];
        uint8 data[MDSA_SECTOR_LEN];
        uint8 checksum;
    } u;
    uint8 raw[MDSA_RAW_LEN];

} SECTOR_FORMAT;

typedef struct {
    UNIT    *uptr;
    uint8   track;
    uint8   wp;         /* Disk write protected */
    uint8   sector;     /* Current Sector number */
    uint32  sector_wait_count;
} MDSA_DRIVE_INFO;

typedef struct {
    uint8   sf;         /* Sector Flag: set when sector hole detected, reset by software. */
    uint8   wi;         /* Window: true during 96-microsecond window at beginning of sector. */
    uint8   mo;         /* Motor On: true while motor(s) are on. */
} COM_STATUS;

typedef struct {
    uint8   wr;         /* Write: controller ready to receive write data */
    uint8   bd;         /* Body: set when sync character is detected. */
    uint8   wp;         /* Write Protect: true while the diskette installed in the selected drive is write protected. */
    uint8   t0;         /* Track 0: true if selected drive is at track zero. */
} A_STATUS;

typedef struct {
    uint8   sc;         /* Sector Counter: indicates the current sector position. */
} B_STATUS;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */

    COM_STATUS  com_status;
    A_STATUS    a_status;
    B_STATUS    b_status;

    uint8       int_enable;     /* Interrupt Enable */
    uint8       stepState;      /* state of step flip-flop*/
    uint8       stepDir;        /* state of step direction flip-flop */
    uint8       currentDrive;   /* currently selected drive */
    uint32      datacount;      /* Number of data bytes transferred from controller for current sector */
    MDSA_DRIVE_INFO drive[MDSA_MAX_DRIVES];
} MDSA_INFO;

static MDSA_INFO mdsa_info_data = { { 0xE800, 1024, 0, 0 } };
static MDSA_INFO *mdsa_info = &mdsa_info_data;

static SECTOR_FORMAT sdata;
static uint32 stepCleared = TRUE;   /* true when step bit has returned to zero */

#define UNIT_V_MDSA_VERBOSE    (UNIT_V_UF + 1)   /* verbose mode, i.e. show error messages       */
#define UNIT_MDSA_VERBOSE      (1 << UNIT_V_MDSA_VERBOSE)
#define MDSA_CAPACITY          (35*10*MDSA_SECTOR_LEN)    /* Default North Star Disk Capacity */

/* MDS-AD Controller Subcases */
#define MDSA_READ_ROM0     0
#define MDSA_READ_ROM1     1    
#define MDSA_WRITE_DATA    2
#define MDSA_CTLR_COMMAND  3

/* MDS_AD Controller Command Bits */
#define MDSA_MOTORS_ON     0x80     /* 1 = motor on */
#define MDSA_READ_DATA     0x40     /* 1 = return byte read from disk */
#define MDSA_B_STATUS      0x20     /* 1 = return B status, else A status */

/* MDS-AD Enumerated Controller Commands */
#define MDSA_CMD_DRIVE     0        /* select drive in M1,M0 */
#define MDSA_CMD_BEGIN_WR  1        /* start write */
#define MDSA_CMD_STEP      2        /* load step bit from M0 */
#define MDSA_CMD_INTR      3        /* load interrupt enable from M0 */
#define MDSA_CMD_NOP       4        
#define MDSA_CMD_RESET_SF  5        /* reset sector flag */
#define MDSA_CMD_RESET     6        /* reset controller, raise heads, stop motors */
#define MDSA_CMD_STEP_DIR  7        /* load step direction from M0, 1=in, 0=out */

/* MDS-AD status byte masks */
/* A-Status */
#define MDSA_A_SF   0x80
#define MDSA_A_WI   0x40
#define MDSA_A_MO   0x10
#define MDSA_A_WR   0x08
#define MDSA_A_BD   0x04
#define MDSA_A_WP   0x02
#define MDSA_A_T0   0x01

/* B-Status */
#define MDSA_B_SF   0x80
#define MDSA_B_WI   0x40
#define MDSA_B_MO   0x10
#define MDSA_B_SC   0x0f

/* Local function prototypes */
static t_stat mdsa_reset(DEVICE *mdsa_dev);
static t_stat mdsa_attach(UNIT *uptr, CONST char *cptr);
static t_stat mdsa_detach(UNIT *uptr);
static t_stat mdsa_boot(int32 unitno, DEVICE *dptr);
static uint8 MDSA_Read(const uint32 Addr);
static const char* mdsa_description(DEVICE *dptr);

static int32 mdsadev(const int32 Addr, const int32 rw, const int32 data);

static UNIT mdsa_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSA_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSA_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSA_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MDSA_CAPACITY) }
};

static REG mdsa_reg[] = {
    { NULL }
};

#define MDSA_NAME  "North Star Single Density Controller"

static const char* mdsa_description(DEVICE *dptr) {
    return MDSA_NAME;
}

static MTAB mdsa_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                  "MEMBASE",  "MEMBASE",
        &set_membase, &show_membase, NULL, "Sets disk controller memory base address"   },
    /* quiet, no warning messages       */
    { UNIT_MDSA_VERBOSE,   0,                  "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " MDSA_NAME "n"                 },
    /* verbose, show warning messages   */
    { UNIT_MDSA_VERBOSE,   UNIT_MDSA_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " MDSA_NAME "n"                    },
    { 0 }
};

/* Debug Flags */
static DEBTAB mdsa_dt[] = {
    { "ERROR",      ERROR_MSG,          "Error messages"        },
    { "SEEK",       SEEK_MSG,           "Seek messages"         },
    { "CMD",        CMD_MSG,            "Command messages"      },
    { "READ",       RD_DATA_MSG,        "Read messages"         },
    { "WRITE",      WR_DATA_MSG,        "Write messages"        },
    { "STATUS",     STATUS_MSG,         "Status messages"       },
    { "RDDETAIL",   RD_DATA_DETAIL_MSG, "Read detail messages"  },
    { "WRDETAIL",   WR_DATA_DETAIL_MSG, "Write detail messags"  },
    { NULL,         0                                           }
};

DEVICE mdsa_dev = {
    "MDSA", mdsa_unit, mdsa_reg, mdsa_mod,
    MDSA_MAX_DRIVES, 10, 31, 1, MDSA_MAX_DRIVES, MDSA_MAX_DRIVES,
    NULL, NULL, &mdsa_reset,
    &mdsa_boot, &mdsa_attach, &mdsa_detach,
    &mdsa_info_data, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), ERROR_MSG, 
    mdsa_dt, NULL, NULL, NULL, NULL, NULL, &mdsa_description
};

/* Reset routine */
t_stat mdsa_reset(DEVICE *dptr)
{
    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) {
        sim_map_resource(pnp->mem_base, pnp->mem_size,
            RESOURCE_TYPE_MEMORY, &mdsadev, TRUE);
    } else {
        /* Connect MDSA at base address */
        if(sim_map_resource(pnp->mem_base, pnp->mem_size,
            RESOURCE_TYPE_MEMORY, &mdsadev, FALSE) != 0) {
            printf("%s: error mapping resource at 0x%04x\n",
                __FUNCTION__, pnp->mem_base);
            dptr->flags |= DEV_DIS;
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

/* Attach routine */
t_stat mdsa_attach(UNIT *uptr, CONST char *cptr)
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
        uptr->capac = MDSA_CAPACITY;
    }

    for(i = 0; i < MDSA_MAX_DRIVES; i++) {
        mdsa_info->drive[i].uptr = &mdsa_dev.units[i];
    }

    for(i = 0; i < MDSA_MAX_DRIVES; i++) {
        if(mdsa_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            mdsa_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_MDSA_VERBOSE)
        printf("MDSA%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);

    return SCPE_OK;
}


/* Detach routine */
t_stat mdsa_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for(i = 0; i < MDSA_MAX_DRIVES; i++) {
        if(mdsa_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= MDSA_MAX_DRIVES)
        return SCPE_ARG;

    DBG_PRINT(("Detach MDSA%d\n", i));

    r = detach_unit(uptr);  /* detach unit */
    if(r != SCPE_OK)
        return r;

    mdsa_dev.units[i].fileref = NULL;
    return SCPE_OK;
}

static t_stat mdsa_boot(int32 unitno, DEVICE *dptr)
{

    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    DBG_PRINT(("Booting MDSA Controller at 0x%04x" NLP, pnp->mem_base));

    *((int32 *) sim_PC->loc) = pnp->mem_base;
    return SCPE_OK;
}

static int32 mdsadev(const int32 Addr, const int32 rw, const int32 data)
{
    if(rw == 0) { /* Read */
        return(MDSA_Read(Addr));
    } else {    /* Write */
        DBG_PRINT(("MDSA: write attempt at 0x%04x ignored." NLP, Addr));
        return (-1);
    }
}

/* This ROM image is taken from the ROMs on the single density controller. This is an
   older version of the ROM which retries forever. Newer ROMs give up after 10 tries. */
static uint8 mdsa_rom[] = {
    0x31,0x14,0x21,0x3E,0x59,0x32,0x00,0x20,0x32,0x03,0x20,0x01,0x01,0x00,0x79,0x16,  /* 00 */
    0x04,0x59,0x21,0x00,0x20,0xCD,0x1E,0xE9,0xC2,0x00,0xE9,0xC3,0x04,0x20,0xF5,0xE5,  /* 10 */
    0xD5,0xC5,0x06,0xEB,0x3A,0x90,0xEB,0xE6,0x10,0xC2,0x34,0xE9,0x16,0x32,0xCD,0xD0,  /* 20 */
    0xE9,0xC3,0x3B,0xE9,0x3A,0x03,0x20,0xB9,0xCA,0x45,0xE9,0x0A,0x79,0x32,0x03,0x20,  /* 30 */
    0x16,0x0D,0xCD,0xD0,0xE9,0x21,0xFF,0x34,0x09,0xF1,0x57,0x96,0x72,0xCA,0x81,0xE9,  /* 40 */
    0x21,0x1D,0xEB,0x4F,0xF2,0x65,0xE9,0x2F,0x3C,0x4F,0x3A,0x10,0xEB,0xE6,0x01,0xC2,  /* 40 */
    0x81,0xE9,0x21,0x1C,0xEB,0x7E,0x3A,0x09,0xEB,0xE3,0xE3,0x3A,0x08,0xEB,0x16,0x02,  /* 60 */
    0xCD,0xD0,0xE9,0x3A,0x10,0xEB,0xE6,0x01,0xCA,0x7D,0xE9,0x0E,0x01,0x0D,0xC2,0x66,  /* 70 */
    0xE9,0xC1,0xCD,0xCE,0xE9,0x3A,0x30,0xEB,0xE6,0x0F,0xB8,0xC2,0x82,0xE9,0xE1,0x0D,  /* 80 */
    0xFA,0x0A,0x20,0xC2,0x07,0x20,0x06,0x46,0x11,0x50,0xEB,0x0E,0x00,0x3A,0x10,0xEB,  /* 90 */
    0xE6,0x04,0xC2,0xAE,0xE9,0x05,0xC2,0x9D,0xE9,0x3E,0x01,0xC1,0xB7,0xC9,0x41,0x1A,  /* A0 */
    0x77,0xA8,0x07,0x47,0x23,0x0D,0xC2,0xAF,0xE9,0x1A,0xA8,0xCA,0xC4,0xE9,0x78,0x3E,  /* B0 */
    0x02,0xC3,0xAB,0xE9,0xF1,0x3D,0xC8,0xF5,0xCD,0xCE,0xE9,0xC3,0x96,0xE9,0x16,0x01,  /* C0 */
    0x3A,0x14,0xEB,0x3A,0x90,0xEB,0xE6,0x80,0xCA,0xD3,0xE9,0x15,0xC8,0xC3,0xD0,0xE9,  /* D0 */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  /* E0 */
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  /* F0 */
};

static void showdata(int32 isRead) {
    int32 i;
    printf("MDSA: " ADDRESS_FORMAT " %s Sector =" NLP "\t", PCX, isRead ? "Read" : "Write");
    for(i=0; i < MDSA_SECTOR_LEN; i++) {
        printf("%02X ", sdata.u.data[i]);
        if(((i+1) & 0xf) == 0)
            printf(NLP "\t");
    }
    printf(NLP); 
}

static int checksum;
static uint32 sec_offset;

static uint32 calculate_mdsa_sec_offset(uint8 track, uint8 sector)
{
    return ((track * (MDSA_SECTOR_LEN * MDSA_SECTORS_PER_TRACK)) + (sector * MDSA_SECTOR_LEN));
}

static uint8 MDSA_Read(const uint32 Addr)
{
    uint8 cData;
    uint8 driveNum;
    MDSA_DRIVE_INFO *pDrive;
    int32 rtn;
    
    cData = 0;
    pDrive = &mdsa_info->drive[mdsa_info->currentDrive];
    switch( (Addr & 0x300) >> 8 ) {
        case MDSA_READ_ROM0:            /* respond to ROM at E800 or E900 */
        case MDSA_READ_ROM1:
            cData = mdsa_rom[Addr & 0xFF];
            break;

        case MDSA_WRITE_DATA:
            if(mdsa_info->datacount == 0) {
                sim_debug(WR_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                          " WRITE Start:  Drive: %d, Track=%d, Sector=%d\n",
                          PCX, mdsa_info->currentDrive, pDrive->track, pDrive->sector);
                sec_offset = calculate_mdsa_sec_offset(pDrive->track, pDrive->sector);
            }

            DBG_PRINT(("MDSA: " ADDRESS_FORMAT " WRITE-DATA[offset:%06x+%03x]=%02x" NLP,
                PCX, sec_offset, mdsa_info->datacount, Addr & 0xFF));
            mdsa_info->datacount++;
            if(mdsa_info->datacount < MDSA_RAW_LEN)
                sdata.raw[mdsa_info->datacount] = Addr & 0xFF;

            if(mdsa_info->datacount == (MDSA_RAW_LEN - 1)) {
                sim_debug(WR_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT " Write Complete\n", PCX);

                if ((pDrive->uptr == NULL) || (pDrive->uptr->fileref == NULL)) {
                    sim_debug(WR_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                              " Drive: %d not attached - write ignored.\n", PCX, mdsa_info->currentDrive);
                    return 0x00;
                }
                if(mdsa_dev.dctrl & WR_DATA_DETAIL_MSG)
                    showdata(FALSE);
                switch((pDrive->uptr)->u3)
                {
                    case IMAGE_TYPE_DSK:
                        if(pDrive->uptr->fileref == NULL) {
                            printf(".fileref is NULL!" NLP);
                        } else {
                            sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET);
                            sim_fwrite(sdata.u.data, 1, MDSA_SECTOR_LEN, (pDrive->uptr)->fileref);
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
            break;

        case MDSA_CTLR_COMMAND:
            if (Addr & MDSA_MOTORS_ON) {
                 sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT " CMD=Motors On\n", PCX);
                 mdsa_info->com_status.mo = 1;            /* Turn motors on */
            }

/* If read data bit is set, return data from disk and ignore command field */

            if (Addr & MDSA_READ_DATA) {
                if(mdsa_info->datacount == 0) {
                    sim_debug(RD_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                              " READ Start:  Drive: %d, Track=%d, Sector=%d\n",
                              PCX, mdsa_info->currentDrive, pDrive->track, pDrive->sector);  

                    checksum = 0;
                    sec_offset = calculate_mdsa_sec_offset(pDrive->track, pDrive->sector);

                    if ((pDrive->uptr == NULL) || (pDrive->uptr->fileref == NULL)) {
                        sim_debug(RD_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " Drive: %d not attached - read ignored.\n",
                                  PCX, mdsa_info->currentDrive);
                        return 0xe5;
                    }

                    switch((pDrive->uptr)->u3) {
                        case IMAGE_TYPE_DSK:
                            if(pDrive->uptr->fileref == NULL) {
                                printf(".fileref is NULL!" NLP);
                            } 
                            else {
                                sim_fseek((pDrive->uptr)->fileref, sec_offset, SEEK_SET);
                                rtn = sim_fread(&sdata.u.data[0], 1, MDSA_SECTOR_LEN,
                                    (pDrive->uptr)->fileref);
                                if (rtn != MDSA_SECTOR_LEN) {
                                    sim_debug(ERROR_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                              " READ: sim_fread error.\n", PCX);
                                }
                            }
                            break;

                        case IMAGE_TYPE_CPT:
                            printf("%s: CPT Format not supported"
                                NLP, __FUNCTION__);
                            break;

                        default:
                            printf("%s: Unknown image Format"
                                NLP, __FUNCTION__);
                            break;
                    }
                    if(mdsa_dev.dctrl & RD_DATA_DETAIL_MSG)
                        showdata(TRUE);
                }

                if(mdsa_info->datacount < MDSA_SECTOR_LEN) {
                    cData = sdata.u.data[mdsa_info->datacount];

                    /* Exclusive OR */
                    checksum ^= cData;
                    /* Rotate Left Circular */
                    checksum = ((checksum << 1) | ((checksum & 0x80) != 0)) & 0xff;

                    DBG_PRINT(("MDSA: " ADDRESS_FORMAT
                        " READ-DATA[offset:%06x+%03x]=%02x" NLP,
                        PCX, sec_offset, mdsa_info->datacount, cData));
                } 
                else { /* checksum */
                    cData = checksum;
                    sim_debug(RD_DATA_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                              " READ-DATA: Checksum is: 0x%02x\n",
                              PCX, cData);
                }

                mdsa_info->datacount++;
             }

/* Not a read from disk, process the command field */

            else {
                switch((Addr & 0x1c) >> 2) {                /* switch based on 3-bit command field */    
                    case MDSA_CMD_DRIVE:                    /* select drive in M1, M0 */
                        driveNum = Addr & 0x03;             /* drive number in two lsbits */
                        if (driveNum == 0)                  /* force drive numbers to 1-3 */
                            driveNum++;
                        mdsa_info->currentDrive = driveNum - 1;     /* map NS drive 1-3 to 0-2 */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                    " CMD=Select Drive: Drive=%x\n", PCX, mdsa_info->currentDrive);
                        pDrive = &mdsa_info->drive[mdsa_info->currentDrive];
                        mdsa_info->a_status.t0 = (pDrive->track == 0);    
                        break;

                    case MDSA_CMD_NOP:
                        pDrive->sector_wait_count++;
                        switch(pDrive->sector_wait_count) {
                            case 10:
                                mdsa_info->com_status.sf = 1;       /* new sector */
                                mdsa_info->com_status.wi = 1;       /* in the 96us window at sector start */
                                mdsa_info->a_status.wr = 0;         /* not ready to write */
                                mdsa_info->a_status.bd = 0;         /* not body (not ready to read) */
                                pDrive->sector_wait_count = 0;
                                pDrive->sector++;
                                if(pDrive->sector >= MDSA_SECTORS_PER_TRACK)
                                    pDrive->sector = 0;
                                break;

                            case 2:                                  /* end 96us window, set ready to write */
                                mdsa_info->com_status.wi = 0;
                                mdsa_info->a_status.wr = 1;
                              break;

                            case 4:                                  /* start of body - ready to read */
                                mdsa_info->a_status.bd = 1;
                                break;

                            default:
                                break;
                        }
                        break;

                    case MDSA_CMD_RESET_SF:             /* reset sector flag */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " CMD=Reset Sector Flag\n", PCX);
                        mdsa_info->com_status.sf = 0;
                        mdsa_info->datacount = 0;
                        break;

                    case MDSA_CMD_INTR:                 /* load interrupt enable/disable */
                        mdsa_info->int_enable = Addr & 0x01;    /* enable bit is M0 */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " CMD=Enable/Disable Interrupt: %d\n", PCX, mdsa_info->int_enable);
                        break;

                    case MDSA_CMD_STEP:                 /* load step flip-flop */
                        mdsa_info->stepState = Addr & 0x01;
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " CMD=Set step flip-flop to %d\n", PCX, mdsa_info->stepState);
       
                        if((mdsa_info->stepState == 1) && stepCleared) {
                            if(mdsa_info->stepDir == 0) {
                                sim_debug(SEEK_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                        " Step out from track %d%s\n", PCX, pDrive->track,
                                        pDrive->track == 0 ? "[Warn: already at 0]" : "");
                                if(pDrive->track > 0)
                                    pDrive->track--;
                            } 
                            else {
                                sim_debug(SEEK_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                     " Step in from track %d%s\n", PCX, pDrive->track,
                                     pDrive->track == (MDSA_TRACKS - 1) ? "[Warn: already at highest track]" : "");
                                if(pDrive->track < (MDSA_TRACKS - 1))
                                    pDrive->track++;
                            }
                        }
                        stepCleared = (mdsa_info->stepState == 0);
                        mdsa_info->a_status.t0 = (pDrive->track == 0);
                        break;

                    case MDSA_CMD_STEP_DIR:                     /* load step direction flip-flop*/
                        mdsa_info->stepDir = Addr & 0x01;       /* direction is in M0 */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                            " CMD=Step direction: %s\n", PCX, mdsa_info->stepDir == 1 ? "In" : "Out");
                        break;

                    case MDSA_CMD_BEGIN_WR:                     /* begin write */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " CMD=Begin Write\n", PCX);
                        break;

                    case MDSA_CMD_RESET:                        /* reset controller */
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " CMD=Reset Controller\n", PCX);
                        mdsa_info->com_status.mo = 0;           /* Turn motors off */
                        break;

                    default:
                        sim_debug(CMD_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                                  " Unsupported CMD=0x%x\n", PCX, Addr & 0x0F);
                        break;
                }

/* Return status register A or B based on the B Status bit in Addr */

                cData  = (mdsa_info->com_status.sf & 1) << 7;   /* form common status bits */
                cData |= (mdsa_info->com_status.wi & 1) << 6;
                cData |= (mdsa_info->com_status.mo & 1) << 4;
                mdsa_info->b_status.sc = pDrive->sector;
                
                if (Addr & MDSA_B_STATUS) {     /* return B status register */
                    cData |= (mdsa_info->b_status.sc & 0x0f);
                    sim_debug(STATUS_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                              " B-Status = <%s %s %s %i>\n", PCX,
                              cData & MDSA_B_SF ? "SF" : "  ",
                              cData & MDSA_B_WI ? "WI" : "  ",
                              cData & MDSA_B_MO ? "MO" : "  ",
                              cData & MDSA_B_SC);
                }

                else {                          /* return A status register */
                    cData |= (mdsa_info->a_status.wr & 1) << 3;
                    cData |= (mdsa_info->a_status.bd & 1) << 2;
                    cData |= (mdsa_info->a_status.wp & 1) << 1;
                    cData |= (mdsa_info->a_status.t0 & 1);
                    sim_debug(STATUS_MSG, &mdsa_dev, "MDSA: " ADDRESS_FORMAT
                              " A-Status = <%s %s %s %s %s %s %s>\n",
                              PCX,
                              cData & MDSA_A_SF ? "SF" : "  ",
                              cData & MDSA_A_WI ? "WI" : "  ",
                              cData & MDSA_A_MO ? "MO" : "  ",
                              cData & MDSA_A_WR ? "WR" : "  ",
                              cData & MDSA_A_BD ? "BD" : "  ",
                              cData & MDSA_A_WP ? "WP" : "  ",
                              cData & MDSA_A_T0 ? "T0" : "  ");
                }
            }
    }
    return (cData);
}
