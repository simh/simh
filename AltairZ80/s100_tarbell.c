/*  s100_tarbell.c: Tarbell 1011 SSSD Disk Controller
  
    Created by Patrick Linstruth (patrick@deltecent.com)
    Based on s100_mdsa.c written by Mike Douglas
  
    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Patrick Linstruth shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Patrick Linstruth.
*/

/* #define DBG_MSG */

#include "altairz80_defs.h"
#include "sim_imd.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define TARBELL_MAX_ADAPTERS      1
#define TARBELL_MAX_DRIVES        4
#define TARBELL_SECTOR_LEN        128
#define TARBELL_SECTORS_PER_TRACK 26
#define TARBELL_BYTES_PER_TRACK   ((TARBELL_SECTORS_PER_TRACK * 186) + 73 + 247)
#define TARBELL_TRACKS            77
#define TARBELL_CAPACITY          (256256)    /* Default Tarbell Disk Capacity */
#define TARBELL_ROTATION_MS       50
#define TARBELL_HEAD_TIMEOUT      (TARBELL_ROTATION_MS * 2)

#define TARBELL_PROM_SIZE         32
#define TARBELL_PROM_MASK         (TARBELL_PROM_SIZE-1)
#define TARBELL_RAM_SIZE          256
#define TARBELL_RAM_MASK          (TARBELL_RAM_SIZE-1)
#define TARBELL_PROM_READ         FALSE
#define TARBELL_PROM_WRITE        TRUE

/* Tarbell PROM is 32 bytes */
static uint8 tarbell_prom[TARBELL_PROM_SIZE] = {
    0xdb, 0xfc, 0xaf, 0x6f, 0x67, 0x3c, 0xd3, 0xfa,
    0x3e, 0x8c, 0xd3, 0xf8, 0xdb, 0xfc, 0xb7, 0xf2,
    0x19, 0x00, 0xdb, 0xfb, 0x77, 0x23, 0xc3, 0x0c,
    0x00, 0xdb, 0xf8, 0xb7, 0xca, 0x7d, 0x00, 0x76
};

static uint8 tarbell_ram[TARBELL_RAM_SIZE];

/*
** Western Digital FD1771 Registers and Interface Controls
*/
typedef struct {
    uint8   track;          /* Track Register */
    uint8   sector;         /* Sector Register */
    uint8   command;        /* Command Register */
    uint8   status;         /* Status Register */
    uint8   data;           /* Data Register */
    uint8   intrq;          /* Interrupt Request */
    int8    stepDir;        /* Last Step Direction */
    uint32  dataCount;      /* Number of data bytes transferred from controller for current sector/address */
    uint32  trkCount;       /* Number of data bytes transferred from controller for current track */
    uint8   readActive;     /* Read Active */
    uint8   readTrkActive;  /* Read Track Active */
    uint8   writeActive;    /* Write Active */
    uint8   writeTrkActive; /* Write Track Active */
    uint8   addrActive;     /* Address Active */
    uint8   driveNotReady;  /* Drive Not Ready */
    uint8   headLoaded;     /* Head Loaded */
    uint32  headUnlTime;    /* Time to unload head */
} FD1771_REG;

#define FD1771_STAT_NOTREADY   0x80
#define FD1771_STAT_WRITEPROT  0x40
#define FD1771_STAT_RTYPEMSB   0x40
#define FD1771_STAT_HEADLOAD   0x20
#define FD1771_STAT_RTYPELSB   0x20
#define FD1771_STAT_WRITEFAULT 0x20
#define FD1771_STAT_SEEKERROR  0x10
#define FD1771_STAT_NOTFOUND   0x10
#define FD1771_STAT_CRCERROR   0x08
#define FD1771_STAT_TRACK0     0x04
#define FD1771_STAT_LOSTDATA   0x04
#define FD1771_STAT_INDEX      0x02
#define FD1771_STAT_DRQ        0x02
#define FD1771_STAT_BUSY       0x01

typedef struct {
    PNP_INFO  pnp;            /* Plug and Play */
    uint8     promEnabled;    /* PROM is enabled */
    uint8     writeProtect;   /* Write Protect is enabled */
    uint8     currentDrive;   /* currently selected drive */
    FD1771_REG FD1771[TARBELL_MAX_DRIVES];   /* FD1771 Registers and Data */
    UNIT *uptr[TARBELL_MAX_DRIVES];
} TARBELL_INFO;

#define TARBELL_PNP_MEMBASE     0x0000
#define TARBELL_PNP_MEMSIZE     TARBELL_RAM_SIZE
#define TARBELL_PNP_IOBASE      0xF8
#define TARBELL_PNP_IOSIZE      5

static TARBELL_INFO tarbell_info_data = { { TARBELL_PNP_MEMBASE, TARBELL_PNP_MEMSIZE, TARBELL_PNP_IOBASE, TARBELL_PNP_IOSIZE } };
static TARBELL_INFO *tarbell_info = &tarbell_info_data;

static uint8 sdata[TARBELL_SECTOR_LEN];

/* Tarbell Registers */
#define TARBELL_REG_STATUS         0x00
#define TARBELL_REG_COMMAND        0x00
#define TARBELL_REG_TRACK          0x01
#define TARBELL_REG_SECTOR         0x02
#define TARBELL_REG_DATA           0x03
#define TARBELL_REG_EXT            0x04

/* Tarbell Commands */
#define TARBELL_CMD_RESTORE        0x00
#define TARBELL_CMD_SEEK           0x10
#define TARBELL_CMD_STEP           0x20
#define TARBELL_CMD_STEPU          (TARBELL_CMD_STEP | TARBELL_FLAG_U)
#define TARBELL_CMD_STEPIN         0x40
#define TARBELL_CMD_STEPINU        (TARBELL_CMD_STEPIN | TARBELL_FLAG_U)
#define TARBELL_CMD_STEPOUT        0x60
#define TARBELL_CMD_STEPOUTU       (TARBELL_CMD_STEPOUT | TARBELL_FLAG_U)
#define TARBELL_CMD_READ           0x80
#define TARBELL_CMD_READM          (TARBELL_CMD_READM | TARBELL_FLAG_M)
#define TARBELL_CMD_WRITE          0xA0
#define TARBELL_CMD_WRITEM         (TARBELL_CMD_WRITEM | TARBELL_FLAG_M)
#define TARBELL_CMD_READ_ADDRESS   0xC0
#define TARBELL_CMD_READ_TRACK     0xE0
#define TARBELL_CMD_WRITE_TRACK    0xF0
#define TARBELL_CMD_FORCE_INTR     0xD0

#define TARBELL_FLAG_V             0x04
#define TARBELL_FLAG_H             0x08
#define TARBELL_FLAG_U             0x10
#define TARBELL_FLAG_M             0x10
#define TARBELL_FLAG_B             0x08
#define TARBELL_FLAG_S             0x01
#define TARBELL_FLAG_E             0x04

#define TARBELL_FLAG_A1A0_FB       0x00
#define TARBELL_FLAG_A1A0_FA       0x01
#define TARBELL_FLAG_A1A0_F9       0x02
#define TARBELL_FLAG_A1A0_F8       0x03

#define TARBELL_FLAG_I0            0x01
#define TARBELL_FLAG_I1            0x02
#define TARBELL_FLAG_I2            0x04
#define TARBELL_FLAG_I3            0x08

#define TARBELL_FLAG_R1R0_6MS      0x00
#define TARBELL_FLAG_R1R0_10ms     0x02
#define TARBELL_FLAG_R1R0_20ms     0x03

#define TARBELL_ADDR_TRACK         0x00
#define TARBELL_ADDR_ZEROS         0x01
#define TARBELL_ADDR_SECTOR        0x02
#define TARBELL_ADDR_LENGTH        0x03
#define TARBELL_ADDR_CRC1          0x04
#define TARBELL_ADDR_CRC2          0x05

/* Local function prototypes */
static t_stat tarbell_reset(DEVICE *tarbell_dev);
static t_stat tarbell_svc(UNIT *uptr);
static t_stat tarbell_attach(UNIT *uptr, CONST char *cptr);
static t_stat tarbell_detach(UNIT *uptr);
static t_stat tarbell_boot(int32 unitno, DEVICE *dptr);
static t_stat tarbell_set_prom(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static void TARBELL_HeadLoad(UNIT *uptr, FD1771_REG *pFD1771, uint8 load);
static uint8 TARBELL_Read(uint32 Addr);
static uint8 TARBELL_Write(uint32 Addr, int32 data);
static uint8 TARBELL_Command(UNIT *uptr, FD1771_REG *pFD1771, int32 data);
static uint32 TARBELL_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static uint32 TARBELL_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static const char* tarbell_description(DEVICE *dptr);
static void showdata(int32 isRead);
static void showregs(FD1771_REG *pFD1771);

static int32 tarbelldev(int32 Addr, int32 rw, int32 data);
static int32 tarbellprom(int32 Addr, int32 rw, int32 data);

static UNIT tarbell_unit[TARBELL_MAX_DRIVES] = {
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 }
};

static REG tarbell_reg[] = {
    { NULL }
};

#define TARBELL_NAME  "Tarbell 1011 Single Density Controller"
#define TARBELL_SNAME "TARBELL"

static const char* tarbell_description(DEVICE *dptr) {
    return TARBELL_NAME;
}

#define UNIT_V_TARBELL_VERBOSE      (UNIT_V_UF + 0)                      /* VERBOSE / QUIET */
#define UNIT_TARBELL_VERBOSE        (1 << UNIT_V_TARBELL_VERBOSE)
#define UNIT_V_TARBELL_WPROTECT     (UNIT_V_UF + 1)                      /* WRTENB / WRTPROT */
#define UNIT_TARBELL_WPROTECT       (1 << UNIT_V_TARBELL_WPROTECT)

static MTAB tarbell_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller IO base address"   },
    { MTAB_XTD | MTAB_VDV,  1,     NULL,           "PROM",         &tarbell_set_prom,
        NULL, NULL, "Enable Tarbell boot PROM"},
    { MTAB_XTD | MTAB_VDV,  0,      NULL,           "NOPROM",          &tarbell_set_prom,
        NULL, NULL, "Disable Tarbell boot PROM"   },
    { UNIT_TARBELL_VERBOSE,   0,                    "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " TARBELL_NAME "n"                 },
    { UNIT_TARBELL_VERBOSE,   UNIT_TARBELL_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " TARBELL_NAME "n"                    },
    { UNIT_TARBELL_WPROTECT,  0,                      "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " TARBELL_NAME "n for writing"                 },
    { UNIT_TARBELL_WPROTECT,  UNIT_TARBELL_WPROTECT,  "WRTPROT",    "WRTPROT",  NULL, NULL, NULL,
        "Protects " TARBELL_NAME "n from writing"                },
    { 0 }
};

/* Debug flags */
#define ERROR_MSG           (1 << 0)
#define SEEK_MSG            (1 << 1)
#define CMD_MSG             (1 << 2)
#define RD_DATA_MSG         (1 << 3)
#define WR_DATA_MSG         (1 << 4)
#define STATUS_MSG          (1 << 5)
#define RD_DATA_DETAIL_MSG  (1 << 6)
#define WR_DATA_DETAIL_MSG  (1 << 7)

/* Debug Flags */
static DEBTAB tarbell_dt[] = {
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

DEVICE tarbell_dev = {
    "TARBELL",                            /* name */
    tarbell_unit,                         /* unit */
    tarbell_reg,                          /* registers */
    tarbell_mod,                          /* modifiers */
    TARBELL_MAX_DRIVES,                   /* # units */
    10,                                   /* address radix */
    31,                                   /* address width */
    1,                                    /* addr increment */
    TARBELL_MAX_DRIVES,                   /* data radix */
    TARBELL_MAX_DRIVES,                   /* data width */
    NULL,                                 /* examine routine */
    NULL,                                 /* deposit routine */
    &tarbell_reset,                       /* reset routine */
    &tarbell_boot,                        /* boot routine */
    &tarbell_attach,                      /* attach routine */
    &tarbell_detach,                      /* detach routine */
    &tarbell_info_data,                   /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG),  /* flags */
    ERROR_MSG,                            /* debug control */
    tarbell_dt,                           /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &tarbell_description                  /* description */
};

/* Reset routine */
t_stat tarbell_reset(DEVICE *dptr)
{
    uint8 i;
    TARBELL_INFO *pInfo = (TARBELL_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pInfo->pnp.mem_base, pInfo->pnp.mem_size, RESOURCE_TYPE_MEMORY, &tarbellprom, TRUE);
        sim_map_resource(pInfo->pnp.io_base, pInfo->pnp.io_size, RESOURCE_TYPE_IO, &tarbelldev, TRUE);
    } else {
        if(sim_map_resource(pInfo->pnp.mem_base, pInfo->pnp.mem_size, RESOURCE_TYPE_MEMORY, &tarbellprom, FALSE) != 0) {
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Error mapping MEM resource at 0x%04x" NLP, pInfo->pnp.mem_base);
            return SCPE_ARG;
        }
        /* Connect I/O Ports at base address */
        if(sim_map_resource(pInfo->pnp.io_base, pInfo->pnp.io_size, RESOURCE_TYPE_IO, &tarbelldev, FALSE) != 0) {
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Error mapping I/O resource at 0x%02x" NLP, pInfo->pnp.io_base);
            return SCPE_ARG;
        }
    }

    pInfo->currentDrive = 0;
    pInfo->promEnabled = TRUE;
    pInfo->writeProtect = FALSE;

    /* Reset Registers and Interface Controls */
    for (i=0; i < TARBELL_MAX_DRIVES; i++) {
        pInfo->FD1771[i].track = 0;
        pInfo->FD1771[i].sector = 1;
        pInfo->FD1771[i].command = 0;
        pInfo->FD1771[i].status = 0;
        pInfo->FD1771[i].data = 0;
        pInfo->FD1771[i].intrq = 0;
        pInfo->FD1771[i].stepDir = 1;
        pInfo->FD1771[i].dataCount = 0;
        pInfo->FD1771[i].trkCount = 0;
        pInfo->FD1771[i].addrActive = FALSE;
        pInfo->FD1771[i].readActive = FALSE;
        pInfo->FD1771[i].readTrkActive = FALSE;
        pInfo->FD1771[i].writeActive = FALSE;
        pInfo->FD1771[i].writeTrkActive = FALSE;
        pInfo->FD1771[i].addrActive = FALSE;
        pInfo->FD1771[i].headLoaded = FALSE;
        pInfo->FD1771[i].driveNotReady = ((pInfo->uptr[i] == NULL) || (pInfo->uptr[i]->fileref == NULL)) ? TRUE : FALSE;
    }

    sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": reset controller." NLP);

    return SCPE_OK;
}

static t_stat tarbell_svc(UNIT *uptr)
{
    FD1771_REG *pFD1771;
    uint32 now;

    pFD1771 = &tarbell_info->FD1771[tarbell_info->currentDrive];

    /*
    ** Get current msec time
    */
    now = sim_os_msec();

    if (now < pFD1771->headUnlTime) {
        sim_activate(uptr, 100000);  /* restart timer */
    }
    else if (pFD1771->headLoaded == TRUE) {
        TARBELL_HeadLoad(uptr, pFD1771, FALSE);
    }

    return SCPE_OK;
}

/* Attach routine */
t_stat tarbell_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if(r != SCPE_OK) {              /* error?       */
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": ATTACH error=%d" NLP, r);
        return r;
    }

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = TARBELL_CAPACITY;
    }

    DBG_PRINT(("TARBELL: ATTACH uptr->capac=%d" NLP, uptr->capac));

    for (i = 0; i < TARBELL_MAX_DRIVES; i++) {
        tarbell_info->uptr[i] = &tarbell_dev.units[i];
    }

    for (i = 0; i < TARBELL_MAX_DRIVES; i++) {
        if(tarbell_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            sim_printf("CPT images not yet supported" NLP);
            uptr->u3 = IMAGE_TYPE_CPT;
            tarbell_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_TARBELL_VERBOSE) {
        sim_printf(TARBELL_SNAME "%d, attached to '%s', type=%s, len=%d" NLP, i, cptr,
            uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);
    }

    /*
    ** Clear Not Ready Flag
    */
    tarbell_info->FD1771[i].driveNotReady = FALSE;

    return SCPE_OK;
}


/* Detach routine */
t_stat tarbell_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < TARBELL_MAX_DRIVES; i++) {
        if(tarbell_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= TARBELL_MAX_DRIVES) {
        return SCPE_ARG;
    }

    DBG_PRINT(("Detach TARBELL%d\n", i));

    r = detach_unit(uptr);  /* detach unit */

    if (r != SCPE_OK) {
        return r;
    }

    tarbell_dev.units[i].fileref = NULL;

    if (uptr->flags & UNIT_TARBELL_VERBOSE) {
        sim_printf(TARBELL_SNAME "%d detached." NLP, i);
    }

    /*
    ** Set Not Ready Flag
    */
    tarbell_info->FD1771[i].driveNotReady = TRUE;

    return SCPE_OK;
}

static t_stat tarbell_set_prom(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    tarbell_info->promEnabled = (uint8) value;

    return SCPE_OK;
}

static t_stat tarbell_boot(int32 unitno, DEVICE *dptr)
{

    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Booting Controller at 0x%04x" NLP, pnp->mem_base);

    *((int32 *) sim_PC->loc) = pnp->mem_base;

    return SCPE_OK;
}

static int32 tarbelldev(int32 Addr, int32 rw, int32 data)
{
    if (rw == 0) { /* Read */
        return(TARBELL_Read(Addr));
    } else {    /* Write */
        return(TARBELL_Write(Addr, data));
    }
}

static void showdata(int32 isRead) {
    int32 i;
    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &tarbell_dev, TARBELL_SNAME ": %s sector:" NLP "\t", isRead ? "Read" : "Write");
    for (i=0; i < TARBELL_SECTOR_LEN; i++) {
        sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &tarbell_dev, "%02X ", sdata[i]);
        if (((i+1) & 0xf) == 0) {
            sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &tarbell_dev, NLP "\t");
        }
    }
    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &tarbell_dev, NLP); 
}

static void showregs(FD1771_REG *pFD1771)
{
    DBG_PRINT(("TARBELL: DRV=%d PE=%d AA=%d RA=%d WA=%d DC=%03d CMD=%02Xh DATA=%02Xh TRK=%03d SEC=%03d STAT=%02X" NLP,
        tarbell_info->currentDrive, tarbell_info->promEnabled, pFD1771->addrActive, pFD1771->readActive, pFD1771->writeActive, pFD1771->dataCount,
        pFD1771->command, pFD1771->data, pFD1771->track, pFD1771->sector, pFD1771->status));
}


static uint32 calculate_tarbell_sec_offset(uint8 track, uint8 sector)
{
    uint32 offset;

    offset = ((track * (TARBELL_SECTOR_LEN * TARBELL_SECTORS_PER_TRACK)) + ((sector-1) * TARBELL_SECTOR_LEN));

    DBG_PRINT(("TARBELL: CALC track=%d sector=%d offset=%04X" NLP, track, sector, offset));

    return (offset);
}

static void TARBELL_HeadLoad(UNIT *uptr, FD1771_REG *pFD1771, uint8 load)
{
    sim_cancel(uptr);            /* cancel timer */

    if (load) {
        pFD1771->headUnlTime = sim_os_msec() + TARBELL_HEAD_TIMEOUT;
        sim_activate(uptr, 100000);  /* activate timer */
    }

    if (load == TRUE && pFD1771->headLoaded == FALSE) {
        sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Drive %d head Loaded." NLP, tarbell_info->currentDrive);
    }

    if (load == FALSE && pFD1771->headLoaded == TRUE) {
        sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Drive %d head Unloaded." NLP, tarbell_info->currentDrive);
    }

    pFD1771->headLoaded = load;
}

static uint8 TARBELL_Read(uint32 Addr)
{
    uint8 cData;
    uint8 driveNum;
    FD1771_REG *pFD1771;
    UNIT *uptr;

    cData = 0;
    driveNum = tarbell_info->currentDrive;
    uptr = tarbell_info->uptr[driveNum];
    pFD1771 = &tarbell_info->FD1771[driveNum];

    switch(Addr & 0x07) {
        case TARBELL_REG_STATUS:
            cData = pFD1771->status;
            break;

        case TARBELL_REG_TRACK:
            cData = pFD1771->track;
            break;

        case TARBELL_REG_DATA:
            /*
            ** If a READ operation is currently active, get the next byte
            */
            if (pFD1771->readActive) {
                /* Store byte in DATA register */
                pFD1771->data = sdata[pFD1771->dataCount++];

                /* If we reached the end of the sector, terminate command and set INTRQ */
                if (pFD1771->dataCount == TARBELL_SECTOR_LEN) {
                    pFD1771->readActive = FALSE;
                    pFD1771->dataCount = 0;
                    pFD1771->status = 0x00;
                    pFD1771->intrq = TRUE;

                    showregs(pFD1771);
                }
                else {
                    pFD1771->status |= FD1771_STAT_DRQ; /* Another byte is ready */
                }

                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }
            else if (pFD1771->addrActive) {
                /* Store byte in DATA register */
                pFD1771->data = sdata[pFD1771->dataCount++];

                DBG_PRINT(("TARBELL: READ ADDR data=%03d" NLP, pFD1771->data));

                /* If we reached the end of the address data, terminate command and set INTRQ */
                if (pFD1771->dataCount > TARBELL_ADDR_CRC2) {
                    pFD1771->addrActive = FALSE;
                    pFD1771->status = 0x00;
                    pFD1771->intrq = TRUE;

                    showregs(pFD1771);
                }
                else {
                    pFD1771->status |= FD1771_STAT_DRQ; /* Another byte is ready */
                }

                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            cData = pFD1771->data;
            break;

        case TARBELL_REG_SECTOR:
            cData = pFD1771->sector;
            break;

        case TARBELL_REG_EXT:
            cData = (pFD1771->intrq) ? 0x00 : 0x80;   /* Bit 7 True if DRQ */
            break;

        default:
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READ Invalid I/O Address %02x (%02x)" NLP, Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    return (cData);
}

static uint8 TARBELL_Write(uint32 Addr, int32 Data)
{
    uint8 cData;
    uint8 driveNum;
    int32 rtn;
    UNIT *uptr;
    FD1771_REG *pFD1771;

    DBG_PRINT(("TARBELL: WRITE Address %02x Data %02x" NLP, Addr & 0xFF, Data & 0xFF));

    cData = 0;
    driveNum = tarbell_info->currentDrive;
    uptr = tarbell_info->uptr[driveNum];
    pFD1771 = &tarbell_info->FD1771[driveNum];

    switch(Addr & 0x07) {
        case TARBELL_REG_COMMAND:
            cData = TARBELL_Command(uptr, pFD1771, Data);
            break;

        case TARBELL_REG_DATA:
            pFD1771->data = Data;   /* Store byte in DATA register */

            if (pFD1771->writeActive) {

                /* Store DATA register in Sector Buffer */
                sdata[pFD1771->dataCount++] = pFD1771->data;

                /* If we reached the end of the sector, write sector, terminate command and set INTRQ */
                if (pFD1771->dataCount == TARBELL_SECTOR_LEN) {
                    pFD1771->status = 0x00;  /* Clear Status Bits */

                    rtn = TARBELL_WriteSector(uptr, pFD1771->track, pFD1771->sector, sdata);

                    if (rtn != TARBELL_SECTOR_LEN) {
                        pFD1771->status |= FD1771_STAT_WRITEFAULT;
                    }
                    pFD1771->writeActive = FALSE;
                    pFD1771->dataCount = 0;
                    pFD1771->intrq = TRUE;

                    showregs(pFD1771);
                }
                else {
                    pFD1771->status |= FD1771_STAT_DRQ; /* Ready for another byte */
                }

                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }
            else if (pFD1771->writeTrkActive) {

                /*
                ** Increment number for bytes written to track
                */
                pFD1771->trkCount++;

                if (pFD1771->data == 0xE5) {

                    /* Store DATA register in Sector Buffer */
                    sdata[pFD1771->dataCount++] = pFD1771->data;

                    /* If we reached the end of the sector, write sector, terminate command and set INTRQ */
                    if (pFD1771->dataCount == TARBELL_SECTOR_LEN) {
                        pFD1771->status &= ~FD1771_STAT_WRITEFAULT;  /* Clear Status Bit */

                        rtn = TARBELL_WriteSector(uptr, pFD1771->track, pFD1771->sector, sdata);

                        if (rtn != TARBELL_SECTOR_LEN) {
                            pFD1771->status |= FD1771_STAT_WRITEFAULT;
                        }

                        pFD1771->sector++;
                        pFD1771->dataCount = 0;
                        pFD1771->status &= ~FD1771_STAT_BUSY;  /* Clear BUSY Bit */

                        DBG_PRINT(("TARBELL: WRITE TRACK track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X" NLP, pFD1771->track, pFD1771->sector, pFD1771->trkCount, pFD1771->dataCount, pFD1771->data, pFD1771->status));

                        showregs(pFD1771);
                    }
                }

                if (pFD1771->trkCount < TARBELL_BYTES_PER_TRACK) {
                    pFD1771->status |= FD1771_STAT_DRQ; /* Ready for another byte */
                }
                else {
                    pFD1771->status = 0x00;  /* Clear Status Bits */
                    pFD1771->intrq = TRUE;   /* Simulate reaching index hole */
                    sim_debug(WR_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": WRITE TRACK track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X" NLP, pFD1771->track, pFD1771->sector, pFD1771->trkCount, pFD1771->dataCount, pFD1771->data, pFD1771->status);
                }

                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            DBG_PRINT(("TARBELL: WRITE DATA REG %02X" NLP, Data));
            break;

        case TARBELL_REG_TRACK:
            pFD1771->track = Data;
            DBG_PRINT(("TARBELL: TRACK REG=%d" NLP, pFD1771->track));
            break;

        case TARBELL_REG_SECTOR:
            if (Data > TARBELL_SECTORS_PER_TRACK) {
                pFD1771->sector = 1;
            }

            pFD1771->sector = Data;

            DBG_PRINT(("TARBELL: SECTOR REG=%d" NLP, pFD1771->sector));

            break;

        case TARBELL_REG_EXT:
            cData = ~(Data >> 4) & 0x03;
            if (cData < TARBELL_MAX_DRIVES) {
                tarbell_info->currentDrive = cData;
                sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Current drive now %d" NLP, tarbell_info->currentDrive);
            }
            else {
              sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Invalid Drive Number drive=%02x (%02x)" NLP, Data, cData);
            }
            break;

        default:
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": WRITE Invalid I/O Address %02x (%02x)" NLP, Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    DBG_PRINT(("TARBELL: COMPLETE currentDrive=%d sector=%02x data=%02x" NLP, tarbell_info->currentDrive, pFD1771->sector, pFD1771->data));

    return(cData);
}

static uint32 TARBELL_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC uptr.fileref is NULL!" NLP);
        return 0;
    }

    sec_offset = calculate_tarbell_sec_offset(track, sector);

    sim_debug(RD_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC track %03d sector %03d at offset %04X" NLP, track, sector, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC sim_fseek error." NLP);
        return 0;
    }

    rtn = sim_fread(buffer, 1, TARBELL_SECTOR_LEN, uptr->fileref);

    return rtn;
}


static uint32 TARBELL_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC uptr.fileref is NULL!" NLP);
        return 0;
    }

    sec_offset = calculate_tarbell_sec_offset(track, sector);

    sim_debug(WR_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": WRITESEC track %03d sector %03d at offset %04X" NLP, track, sector, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": WRITESEC sim_fseek error." NLP);
        return 0;
    }

    rtn = sim_fwrite(buffer, 1, TARBELL_SECTOR_LEN, uptr->fileref);

    return rtn;
}


static uint8 TARBELL_Command(UNIT *uptr, FD1771_REG *pFD1771, int32 Data)
{
    uint8 cData;
    uint8 newTrack;
    uint8 statusUpdate;
    int32 rtn;

    cData = 0;
    rtn=0;
    statusUpdate = TRUE;

    pFD1771->command = (Data & 0xF0);

    pFD1771->status |= FD1771_STAT_BUSY;
    pFD1771->intrq = FALSE;

    /*
    ** Type II-IV Command
    */
    if (pFD1771->command & 0x80) {
        pFD1771->readActive = FALSE;
        pFD1771->writeActive = FALSE;
        pFD1771->readTrkActive = FALSE;
        pFD1771->writeTrkActive = FALSE;
        pFD1771->addrActive = FALSE;
        pFD1771->dataCount = 0;

        pFD1771->status &= ~FD1771_STAT_DRQ;    /* Reset DRQ */
    }

    /*
    ** Set BUSY for all but Force Interrupt
    */
    if ((pFD1771->command & TARBELL_CMD_FORCE_INTR) == TARBELL_CMD_FORCE_INTR) {
        pFD1771->status |= FD1771_STAT_BUSY;
    }

    switch(pFD1771->command) {
        case TARBELL_CMD_RESTORE:
            pFD1771->track = 0;

            sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": RESTORE track=%03d" NLP, pFD1771->track);

            if (Data & TARBELL_FLAG_H) {
                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            pFD1771->status &= ~FD1771_STAT_SEEKERROR;
            pFD1771->status &= ~FD1771_STAT_BUSY;
            pFD1771->status &= ~FD1771_STAT_DRQ;
            pFD1771->intrq = TRUE;
            break;

        case TARBELL_CMD_SEEK:
            newTrack = pFD1771->data;

            DBG_PRINT(("TARBELL: TRACK DATA=%d (SEEK)" NLP, newTrack));

            pFD1771->status &= ~FD1771_STAT_SEEKERROR;

            if (newTrack < TARBELL_TRACKS) {
                pFD1771->track = newTrack;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": SEEK       track=%03d" NLP, pFD1771->track);
            }
            else {
                pFD1771->status |= FD1771_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": SEEK ERR   track=%03d" NLP, newTrack);
            }

            if (Data & TARBELL_FLAG_H) {
                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            pFD1771->status &= ~FD1771_STAT_BUSY;
            pFD1771->status &= ~FD1771_STAT_DRQ;
            pFD1771->intrq = TRUE;
            break;

        case TARBELL_CMD_STEP:
        case TARBELL_CMD_STEPU:
            pFD1771->status &= ~FD1771_STAT_SEEKERROR;

            newTrack = pFD1771->track + pFD1771->stepDir;

            if (newTrack < TARBELL_TRACKS-1) {
                if (Data & TARBELL_FLAG_U) {
                    pFD1771->track = newTrack;
                }
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEP        track=%03d" NLP, pFD1771->track);
            }
            else {
                pFD1771->status |= FD1771_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEP ERR    track=%03d" NLP, newTrack);
            }

            if (Data & TARBELL_FLAG_H) {
                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            pFD1771->status &= ~FD1771_STAT_BUSY;
            pFD1771->status &= ~FD1771_STAT_DRQ;
            pFD1771->intrq = TRUE;
            break;

        case TARBELL_CMD_STEPIN:
        case TARBELL_CMD_STEPINU:
            pFD1771->status &= ~FD1771_STAT_SEEKERROR;

            if (pFD1771->track < TARBELL_TRACKS) {
                if (Data & TARBELL_FLAG_U) {
                    pFD1771->track++;
                }
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPIN      track=%03d" NLP, pFD1771->track);
            }
            else {
                pFD1771->status |= FD1771_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPIN ERR  track=%03d" NLP, pFD1771->track+1);
            }

            if (Data & TARBELL_FLAG_H) {
                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            pFD1771->stepDir = 1;
            pFD1771->status &= ~FD1771_STAT_BUSY;
            pFD1771->status &= ~FD1771_STAT_DRQ;
            pFD1771->intrq = TRUE;
            break;

        case TARBELL_CMD_STEPOUT:
        case TARBELL_CMD_STEPOUTU:
            pFD1771->status &= ~FD1771_STAT_SEEKERROR;

            if (pFD1771->track > 0) {
                if (Data & TARBELL_FLAG_U) {
                    pFD1771->track--;
                }
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPOUT     track=%03d" NLP, pFD1771->track);
            }
            else {
                pFD1771->status |= FD1771_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPOUT ERR track=%03d" NLP, pFD1771->track-1);
            }

            if (Data & TARBELL_FLAG_H) {
                TARBELL_HeadLoad(uptr, pFD1771, TRUE);
            }

            pFD1771->stepDir = -1;
            pFD1771->status &= ~FD1771_STAT_BUSY;
            pFD1771->status &= ~FD1771_STAT_DRQ;
            pFD1771->intrq = TRUE;
            break;

        case TARBELL_CMD_READ:

            if ((uptr == NULL) || (uptr->fileref == NULL)) {
                DBG_PRINT(("TARBELL: " ADDRESS_FORMAT
                          " Drive: %d not attached - read ignored." NLP,
                          PCX, tarbell_info->currentDrive));

                pFD1771->status &= ~FD1771_STAT_BUSY;

                return cData;
            }

            rtn = TARBELL_ReadSector(uptr, pFD1771->track, pFD1771->sector, sdata);

            if (rtn == TARBELL_SECTOR_LEN) {
                pFD1771->readActive = TRUE;

                showdata(TRUE);
            }
            else {
                DBG_PRINT(("TARBELL: " ADDRESS_FORMAT " READ: sim_fread error." NLP, PCX));

                pFD1771->status |= FD1771_STAT_NOTFOUND;
                pFD1771->intrq = TRUE;
            }

            break;

        case TARBELL_CMD_WRITE:
            if ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) {
                DBG_PRINT((TARBELL_SNAME ": Disk write protected. uptr->flags=%04x writeProtect=%04x" NLP, uptr->flags & UNIT_TARBELL_WPROTECT, tarbell_info->writeProtect));
                pFD1771->intrq = TRUE;
            }
            else {
                pFD1771->writeActive = TRUE;
                pFD1771->dataCount = 0;
                pFD1771->status |= FD1771_STAT_DRQ;    /* Set DRQ */
            }

            break;

        case TARBELL_CMD_READ_ADDRESS:
            sdata[TARBELL_ADDR_TRACK]=pFD1771->track;
            sdata[TARBELL_ADDR_ZEROS]=0;
            sdata[TARBELL_ADDR_SECTOR]=pFD1771->sector;
            sdata[TARBELL_ADDR_LENGTH]=TARBELL_SECTOR_LEN;
            sdata[TARBELL_ADDR_CRC1]=0;
            sdata[TARBELL_ADDR_CRC2]=0;

            pFD1771->addrActive = TRUE;
            pFD1771->status |= FD1771_STAT_DRQ;    /* Set DRQ */

            break;

        case TARBELL_CMD_READ_TRACK:
            pFD1771->readTrkActive = TRUE;
            pFD1771->trkCount=0;
            pFD1771->dataCount=0;
            pFD1771->sector=1;
            pFD1771->status |= FD1771_STAT_DRQ;    /* Set DRQ */
            break;

        case TARBELL_CMD_WRITE_TRACK:
            if ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) {
                DBG_PRINT((TARBELL_SNAME ": Disk write protected. uptr->flags=%04x writeProtect=%04x" NLP, uptr->flags & UNIT_TARBELL_WPROTECT, tarbell_info->writeProtect));
                pFD1771->intrq = TRUE;
            }
            else {
                pFD1771->writeTrkActive = TRUE;
                pFD1771->trkCount=0;
                pFD1771->dataCount=0;
                pFD1771->sector=1;
                pFD1771->status |= FD1771_STAT_DRQ;    /* Set DRQ */
            }
            break;

        case TARBELL_CMD_FORCE_INTR:
            if (pFD1771->status & FD1771_STAT_BUSY) {
                pFD1771->status &= ~FD1771_STAT_BUSY;
                statusUpdate = FALSE;
            }

            /* Reset Status */
            pFD1771->dataCount = 0;
            pFD1771->readActive = FALSE;
            pFD1771->readTrkActive = FALSE;
            pFD1771->writeActive = FALSE;
            pFD1771->writeTrkActive = FALSE;
            pFD1771->addrActive = FALSE;

            break;

        default:
            cData=0xFF;
            sim_debug(ERROR_MSG, &tarbell_dev, "TARBELL: UNRECOGNIZED CMD %02X" NLP, pFD1771->command);
            break;
    }

    /**************************/
    /* Update Status Register */
    /**************************/

    pFD1771->status |= (pFD1771->driveNotReady) ? FD1771_STAT_NOTREADY : 0x00;

    switch(pFD1771->command) {
        case TARBELL_CMD_RESTORE:
        case TARBELL_CMD_SEEK:
        case TARBELL_CMD_STEP:
        case TARBELL_CMD_STEPU:
        case TARBELL_CMD_STEPIN:
        case TARBELL_CMD_STEPINU:
        case TARBELL_CMD_STEPOUT:
        case TARBELL_CMD_STEPOUTU:
        case TARBELL_CMD_FORCE_INTR:
            if (statusUpdate) {
                pFD1771->status &= ~FD1771_STAT_WRITEPROT;
                pFD1771->status &= ~FD1771_STAT_CRCERROR;
                pFD1771->status &= ~FD1771_STAT_TRACK0;
                pFD1771->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD1771_STAT_WRITEPROT : 0x00;
                pFD1771->status |= (pFD1771->track) ? 0x00 : FD1771_STAT_TRACK0;
                pFD1771->status |= (pFD1771->headLoaded) ? FD1771_STAT_HEADLOAD : 0x00;
                pFD1771->status |= FD1771_STAT_INDEX;    /* Always set Index Flag if Drive Ready */
            }
            break;

        case TARBELL_CMD_READ:
            pFD1771->status &= ~FD1771_STAT_LOSTDATA;
            pFD1771->status &= ~FD1771_STAT_NOTFOUND;
            pFD1771->status &= ~FD1771_STAT_CRCERROR;
            break;

        case TARBELL_CMD_WRITE:
            pFD1771->status &= ~FD1771_STAT_WRITEPROT;
            pFD1771->status &= ~FD1771_STAT_LOSTDATA;
            pFD1771->status &= ~FD1771_STAT_NOTFOUND;
            pFD1771->status &= ~FD1771_STAT_CRCERROR;
            pFD1771->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD1771_STAT_WRITEPROT : 0x00;
            break;

        case TARBELL_CMD_READ_ADDRESS:
            pFD1771->status &= ~0x20;
            pFD1771->status &= ~0x40;
            pFD1771->status &= ~FD1771_STAT_LOSTDATA;
            pFD1771->status &= ~FD1771_STAT_NOTFOUND;
            pFD1771->status &= ~FD1771_STAT_CRCERROR;
            break;

        case TARBELL_CMD_READ_TRACK:
            pFD1771->status &= ~0x08;
            pFD1771->status &= ~0x10;
            pFD1771->status &= ~0x20;
            pFD1771->status &= ~0x40;
            pFD1771->status &= ~FD1771_STAT_LOSTDATA;
            break;

        case TARBELL_CMD_WRITE_TRACK:
            pFD1771->status &= ~0x08;
            pFD1771->status &= ~0x10;
            pFD1771->status &= ~FD1771_STAT_WRITEPROT;
            pFD1771->status &= ~FD1771_STAT_LOSTDATA;
            pFD1771->status &= ~FD1771_STAT_WRITEFAULT;
            pFD1771->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD1771_STAT_WRITEPROT : 0x00;
            break;
    }

    sim_debug(CMD_MSG, &tarbell_dev, TARBELL_SNAME ": CMD drive=%d cmd=%02X track=%03d sector=%03d status=%02X" NLP,
        tarbell_info->currentDrive, pFD1771->command, pFD1771->track, pFD1771->sector, pFD1771->status);

    return(cData);
}

static int32 tarbellprom(int32 Addr, int32 rw, int32 Data)
{
    /*
    ** The Tarbell controller overlays the first 32 bytes of RAM with a PROM.
    ** The PROM is enabled/disabled with switch position 7:
    ** ON  = ENABLED
    ** OFF = DISABLED
    **
    ** If the PROM is enabled, writes to 0x0000-0x001F are written to RAM, reads are
    ** from the PROM.
    **
    ** The PROM is disabled if the controller detects a memory read with bit A5 enabled,
    ** which can't be implemented because Examine reads 6 bytes at a time. We will disable
    ** the PROM if address >= 0x0025 is read. Hack.
    */

    if (rw == TARBELL_PROM_WRITE) {
        tarbell_ram[Addr & TARBELL_RAM_MASK] = Data;
        return 0;
    } else {
        if (Addr >= 0x0025 && tarbell_info->promEnabled == TRUE) {
            tarbell_info->promEnabled = FALSE;
            sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Boot PROM disabled." NLP);
        }

        if (tarbell_info->promEnabled == TRUE && Addr < TARBELL_PROM_SIZE) {
            return(tarbell_prom[Addr & TARBELL_PROM_MASK]);
        } else {
            return(tarbell_ram[Addr & TARBELL_RAM_MASK]);
        }
    }
}

