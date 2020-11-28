/*  s100_tarbell.c: Tarbell 1011/2022 Disk Controller
  
    Created by Patrick Linstruth (patrick@deltecent.com)
  
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


    These functions support simulated Tarbell model 1011 single density and
    model 2022 double density floppy disk controllers.

    The model is selected using the "SET TARBELL MODEL={SD|DD}" command.

    This device does not support the DMA feature of the double density
    controller. All software using this simulator must use programmed
    I/O.
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
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);

/* These are needed for DMA. */
extern void PutByteDMA(const uint32 addr, const uint32 val);
extern uint8 GetByteDMA(const uint32 addr);

#define TARBELL_MAX_ADAPTERS      1
#define TARBELL_MAX_DRIVES        4
#define TARBELL_SECTOR_LEN        128
#define TARBELL_SPT_SD            26
#define TARBELL_SPT_DD            51
#define TARBELL_TRACKS            77
#define TARBELL_CAPACITY          (256256)      /* Default Tarbell Single Density Disk Capacity */
#define TARBELL_ROTATION_MS       (166)         /* 166 milliseconds per revolution */
#define TARBELL_HEAD_TIMEOUT      (TARBELL_ROTATION_MS * 1000 * 2)   /* usec * 2 revolutions */

#define IBM3740_TRK_HDR_LEN       73
#define IBM3740_SD_SEC_LEN        186
#define IBM3740_DD_SEC_LEN        196

#define TARBELL_PROM_SIZE         32
#define TARBELL_PROM_MASK         (TARBELL_PROM_SIZE-1)
#define TARBELL_RAM_SIZE          256
#define TARBELL_RAM_MASK          (TARBELL_RAM_SIZE-1)
#define TARBELL_PROM_READ         FALSE
#define TARBELL_PROM_WRITE        TRUE

#define TARBELL_MEMBASE     0x0000
#define TARBELL_MEMSIZE     TARBELL_RAM_SIZE
#define TARBELL_IOBASE      0xF8
#define TARBELL_IOSIZE_SD   5
#define TARBELL_IOSIZE_DD   6
#define TARBELL_DMABASE     0xE0
#define TARBELL_DMASIZE     16

/* Tarbell PROM is 32 bytes */
static uint8 tarbell_prom[TARBELL_PROM_SIZE] = {
    0xdb, 0xfc, 0xaf, 0x6f, 0x67, 0x3c, 0xd3, 0xfa,
    0x3e, 0x8c, 0xd3, 0xf8, 0xdb, 0xfc, 0xb7, 0xf2,
    0x19, 0x00, 0xdb, 0xfb, 0x77, 0x23, 0xc3, 0x0c,
    0x00, 0xdb, 0xf8, 0xb7, 0xca, 0x7d, 0x00, 0x76
};

static uint8 tarbell_ram[TARBELL_RAM_SIZE];

/*
** Western Digital FD17XX Registers and Interface Controls
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
    uint8   dataAddrMrk;    /* Data Addr Mark Flag */
    uint8   addrActive;     /* Address Active */
} FD17XX_REG;

#define FD17XX_STAT_NOTREADY   0x80
#define FD17XX_STAT_WRITEPROT  0x40
#define FD17XX_STAT_RTYPEMSB   0x40
#define FD17XX_STAT_HEADLOAD   0x20
#define FD17XX_STAT_RTYPELSB   0x20
#define FD17XX_STAT_WRITEFAULT 0x20
#define FD17XX_STAT_SEEKERROR  0x10
#define FD17XX_STAT_NOTFOUND   0x10
#define FD17XX_STAT_CRCERROR   0x08
#define FD17XX_STAT_TRACK0     0x04
#define FD17XX_STAT_LOSTDATA   0x04
#define FD17XX_STAT_INDEX      0x02
#define FD17XX_STAT_DRQ        0x02
#define FD17XX_STAT_BUSY       0x01

typedef struct {
    PNP_INFO  pnp;            /* Plug and Play */
    uint32    dma_base;       /* DMA base I/O address */
    uint32    dma_size;       /* DMA I/O size */
    uint32    ddEnabled;      /* 0=SD,1=DD */
    uint32    headTimeout;    /* Head unload timer value */
    uint8     promEnabled;    /* PROM is enabled */
    uint8     writeProtect;   /* Write Protect is enabled */
    uint8     currentDrive;   /* currently selected drive */
    uint8     secsPerTrack;   /* sectors per track */
    uint16    bytesPerTrack;  /* bytes per track */
    uint8     headLoaded[TARBELL_MAX_DRIVES];     /* Head Loaded */
    uint8     doubleDensity[TARBELL_MAX_DRIVES];  /* true if double density */
    uint8     side[TARBELL_MAX_DRIVES];           /* side 0 or side 1 */
    FD17XX_REG FD17XX;        /* FD17XX Registers and Data */
    UNIT *uptr[TARBELL_MAX_DRIVES];
} TARBELL_INFO;

static TARBELL_INFO tarbell_info_data = {
    { TARBELL_MEMBASE, TARBELL_MEMSIZE, TARBELL_IOBASE, TARBELL_IOSIZE_SD }, TARBELL_DMABASE, TARBELL_DMASIZE
};

static TARBELL_INFO *tarbell_info = &tarbell_info_data;

static uint8 sdata[TARBELL_SECTOR_LEN];

/* Tarbell Registers */
#define TARBELL_REG_STATUS         0x00
#define TARBELL_REG_COMMAND        0x00
#define TARBELL_REG_TRACK          0x01
#define TARBELL_REG_SECTOR         0x02
#define TARBELL_REG_DATA           0x03
#define TARBELL_REG_WAIT           0x04
#define TARBELL_REG_DRVSEL         0x04
#define TARBELL_REG_DMASTAT        0x05
#define TARBELL_REG_EXTADDR        0x05

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

#define TARBELL_DENS_MASK          0x08
#define TARBELL_DSEL_MASK          0x30
#define TARBELL_SIDE_MASK          0x40

/* Local function prototypes */
static t_stat tarbell_reset(DEVICE *tarbell_dev);
static t_stat tarbell_svc(UNIT *uptr);
static t_stat tarbell_attach(UNIT *uptr, CONST char *cptr);
static t_stat tarbell_detach(UNIT *uptr);
static t_stat tarbell_boot(int32 unitno, DEVICE *dptr);
static t_stat tarbell_set_dmabase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat tarbell_show_dmabase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat tarbell_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat tarbell_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat tarbell_set_model(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat tarbell_show_model(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static uint32 secs_per_track(uint8 track);
static uint32 bytes_per_track(uint8 track);
static uint32 calculate_tarbell_sec_offset(uint8 track, uint8 sector);
static void TARBELL_HeadLoad(UNIT *uptr, FD17XX_REG *pFD17XX, uint8 load);
static uint8 TARBELL_Read(uint32 Addr);
static uint8 TARBELL_Write(uint32 Addr, int32 data);
static uint8 TARBELL_Command(UNIT *uptr, FD17XX_REG *pFD17XX, int32 data);
static uint32 TARBELL_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static uint32 TARBELL_WriteSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer);
static const char* tarbell_description(DEVICE *dptr);
static void showdata(int32 isRead);

static int32 tarbelldev(int32 Addr, int32 rw, int32 data);
static int32 tarbelldma(int32 Addr, int32 rw, int32 data);
static int32 tarbellprom(int32 Addr, int32 rw, int32 data);

static UNIT tarbell_unit[TARBELL_MAX_DRIVES] = {
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 },
    { UDATA (tarbell_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, TARBELL_CAPACITY), 10000 }
};

static REG tarbell_reg[] = {
    { DRDATAD (DRIVE, tarbell_info_data.currentDrive, 8, "Current drive register"), },
    { HRDATAD (STATUS, tarbell_info_data.FD17XX.status, 8, "Status register"), },
    { HRDATAD (COMMAND, tarbell_info_data.FD17XX.command, 8, "Command register"), },
    { HRDATAD (DATA, tarbell_info_data.FD17XX.data, 8, "Data register"), },
    { DRDATAD (TRACK, tarbell_info_data.FD17XX.track, 8, "Track register"), },
    { DRDATAD (SECTOR, tarbell_info_data.FD17XX.sector, 8, "Sector register"), },
    { DRDATAD (SPT, tarbell_info_data.secsPerTrack, 8, "Sectors per track register"), },
    { DRDATAD (BPT, tarbell_info_data.bytesPerTrack, 16, "Bytes per track register"), },
    { DRDATAD (STEPDIR, tarbell_info_data.FD17XX.stepDir, 8, "Last step direction register"), },
    { DRDATAD (SECCNT, tarbell_info_data.FD17XX.dataCount, 16, "Sector byte count register"), },
    { DRDATAD (TRKCNT, tarbell_info_data.FD17XX.trkCount, 16, "Track byte count register"), },
    { FLDATAD (RDACT, tarbell_info_data.FD17XX.readActive, 0, "Read sector active status bit"), },
    { FLDATAD (WRACT, tarbell_info_data.FD17XX.writeActive, 0, "Write sector active status bit"), },
    { FLDATAD (RDTACT, tarbell_info_data.FD17XX.readTrkActive, 0, "Read track active status bit"), },
    { FLDATAD (WRTACT, tarbell_info_data.FD17XX.writeTrkActive, 0, "Write track active status bit"), },
    { FLDATAD (INTRQ, tarbell_info_data.FD17XX.intrq, 0, "INTRQ status bit"), },
    { FLDATAD (PROM, tarbell_info_data.promEnabled, 0, "PROM enabled bit"), },
    { FLDATAD (WRTPROT, tarbell_info_data.writeProtect, 0, "Write protect enabled bit"), },
    { DRDATAD (HDUNLD, tarbell_info_data.headTimeout, 32, "Head unload timeout"), },
    { NULL }
};

#define TARBELL_NAME  "Tarbell SD/DD Floppy Disk Interface"
#define TARBELL_SNAME "TARBELL"

static const char* tarbell_description(DEVICE *dptr) {
    return TARBELL_NAME;
}

#define UNIT_V_TARBELL_VERBOSE      (UNIT_V_UF + 0)                      /* VERBOSE / QUIET  */
#define UNIT_TARBELL_VERBOSE        (1 << UNIT_V_TARBELL_VERBOSE)
#define UNIT_V_TARBELL_WPROTECT     (UNIT_V_UF + 1)                      /* WRTENB / WRTPROT */
#define UNIT_TARBELL_WPROTECT       (1 << UNIT_V_TARBELL_WPROTECT)

#define UNIT_V_SIO_SLEEP    (UNIT_V_UF + 7)     /* sleep after keyboard status check            */
#define UNIT_SIO_SLEEP      (1 << UNIT_V_SIO_SLEEP)

static MTAB tarbell_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,        "IOBASE",  "IOBASE",
        &set_iobase, &show_iobase, NULL, "Sets disk controller I/O base address"   },
    { MTAB_XTD|MTAB_VDV,    0,        "DMABASE",  "DMABASE",
        &tarbell_set_dmabase, &tarbell_show_dmabase, NULL, "Sets disk controller DMA base address"   },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PROM", "PROM={ENABLE|DISABLE}",
        &tarbell_set_prom, &tarbell_show_prom, NULL, "Set/Show PROM enabled/disabled status"},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MODEL", "MODEL={SD|DD}",
        &tarbell_set_model, &tarbell_show_model, NULL, "Set/Show the current controller model" },
    { UNIT_TARBELL_VERBOSE,   0,                    "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " TARBELL_SNAME "n"                 },
    { UNIT_TARBELL_VERBOSE,   UNIT_TARBELL_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " TARBELL_SNAME "n"                    },
    { UNIT_TARBELL_WPROTECT,  0,                      "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " TARBELL_SNAME "n for writing"                 },
    { UNIT_TARBELL_WPROTECT,  UNIT_TARBELL_WPROTECT,  "WRTPROT",    "WRTPROT",  NULL, NULL, NULL,
        "Protects " TARBELL_SNAME "n from writing"                },
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
    TARBELL_SNAME,                        /* name */
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
static t_stat tarbell_reset(DEVICE *dptr)
{
    uint8 i;
    TARBELL_INFO *pInfo = (TARBELL_INFO *)dptr->ctxt;

    if (dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pInfo->pnp.mem_base, pInfo->pnp.mem_size, RESOURCE_TYPE_MEMORY, &tarbellprom, "tarbellprom", TRUE);
        sim_map_resource(pInfo->pnp.io_base, pInfo->pnp.io_size, RESOURCE_TYPE_IO, &tarbelldev, "tarbelldev", TRUE);
        sim_map_resource(pInfo->dma_base, pInfo->dma_size, RESOURCE_TYPE_IO, &tarbelldma, "tarbelldma", TRUE);
    } else {
        if (sim_map_resource(pInfo->pnp.mem_base, pInfo->pnp.mem_size, RESOURCE_TYPE_MEMORY, &tarbellprom, "tarbellprom", FALSE) != 0) {
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Error mapping MEM resource at 0x%04x\n", pInfo->pnp.mem_base);
            return SCPE_ARG;
        }
        /* Connect I/O Ports at base address */
        if (sim_map_resource(pInfo->pnp.io_base, pInfo->pnp.io_size, RESOURCE_TYPE_IO, &tarbelldev, "tarbelldev", FALSE) != 0) {
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Error mapping I/O resource at 0x%02x\n", pInfo->pnp.io_base);
            return SCPE_ARG;
        }

        /* If simulating a DD, connect DMA ports */
        if (tarbell_info->ddEnabled) {
            if (sim_map_resource(pInfo->dma_base, pInfo->dma_size, RESOURCE_TYPE_IO, &tarbelldma, "tarbelldma", FALSE) != 0) {
                sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": Error mapping DMA resource at 0x%02x\n", pInfo->dma_base);
                return SCPE_ARG;
            }
        }
    }

    pInfo->currentDrive = 0;
    pInfo->promEnabled = TRUE;
    pInfo->writeProtect = FALSE;

    /* Reset Registers and Interface Controls */
    for (i=0; i < TARBELL_MAX_DRIVES; i++) {
        if (tarbell_info->uptr[i] == NULL) {
            tarbell_info->uptr[i] = &tarbell_dev.units[i];
        }

        pInfo->FD17XX.track = 0;
        pInfo->FD17XX.sector = 1;
        pInfo->FD17XX.command = 0;
        pInfo->FD17XX.status = 0;
        pInfo->FD17XX.data = 0;
        pInfo->FD17XX.intrq = 0;
        pInfo->FD17XX.stepDir = 1;
        pInfo->FD17XX.dataCount = 0;
        pInfo->FD17XX.trkCount = 0;
        pInfo->FD17XX.addrActive = FALSE;
        pInfo->FD17XX.readActive = FALSE;
        pInfo->FD17XX.readTrkActive = FALSE;
        pInfo->FD17XX.writeActive = FALSE;
        pInfo->FD17XX.writeTrkActive = FALSE;
        pInfo->FD17XX.addrActive = FALSE;

        tarbell_info->headLoaded[i] = FALSE;
        tarbell_info->doubleDensity[i] = FALSE;
        tarbell_info->side[i] = 0;
    }

    /*
    ** If the SIO device sleeps during checks, the SCP usec timer
    ** is no longer accurate and our wait time has to be adjusted
    */
    pInfo->headTimeout = TARBELL_HEAD_TIMEOUT; /* usec timeout */

    sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": reset controller.\n");

    return SCPE_OK;
}

static t_stat tarbell_svc(UNIT *uptr)
{
    FD17XX_REG *pFD17XX;

    pFD17XX = &tarbell_info->FD17XX;

    if (tarbell_info->headLoaded[tarbell_info->currentDrive] == TRUE) {
        TARBELL_HeadLoad(uptr, pFD17XX, FALSE);
    }

    return SCPE_OK;
}

/* Attach routine */
static t_stat tarbell_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if (r != SCPE_OK) {              /* error?       */
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": ATTACH error=%d\n", r);
        return r;
    }

    /* Determine length of this disk */
    if (sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = TARBELL_CAPACITY;
    }

    DBG_PRINT(("TARBELL: ATTACH uptr->capac=%d\n", uptr->capac));

    for (i = 0; i < TARBELL_MAX_DRIVES; i++) {
        if (tarbell_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= TARBELL_MAX_DRIVES) {
        return SCPE_ARG;
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if (uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if ((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            sim_printf("CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            tarbell_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_TARBELL_VERBOSE) {
        sim_printf(TARBELL_SNAME "%d, attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);
    }

    return SCPE_OK;
}


/* Detach routine */
static t_stat tarbell_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < TARBELL_MAX_DRIVES; i++) {
        if (tarbell_dev.units[i].fileref == uptr->fileref) {
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
        sim_printf(TARBELL_SNAME "%d detached.\n", i);
    }

    return SCPE_OK;
}

static t_stat tarbell_set_dmabase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 newba;
    t_stat r;

    if (cptr == NULL || !(tarbell_info->ddEnabled)) {
        return SCPE_ARG;
    }

    newba = get_uint (cptr, 16, 0xFF, &r);

    if (r != SCPE_OK) {
        return r;
    }

    if ((newba > 0xFF) || (newba % tarbell_info->dma_size)) {
        return SCPE_ARG;
    }

    if (tarbell_dev.flags & DEV_DIS) {
        sim_printf("device not enabled yet.\n");
        tarbell_info->dma_base = newba & ~(tarbell_info->dma_size-1);
    } else {
        tarbell_dev.flags |= DEV_DIS;
        tarbell_reset(&tarbell_dev);
        tarbell_info->dma_base = newba & ~(tarbell_info->dma_size-1);
        tarbell_dev.flags &= ~DEV_DIS;
        tarbell_reset(&tarbell_dev);
    }

    return SCPE_OK;
}

static t_stat tarbell_show_dmabase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (tarbell_info->ddEnabled) {
        fprintf(st, "DMA=0x%02X-0x%02X", tarbell_info->dma_base, tarbell_info->dma_base+tarbell_info->dma_size-1);
    } else {
        fprintf(st, "DMA=N/A");
    }

    return SCPE_OK;
}

static t_stat tarbell_set_model(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;

    /* this assumes that the parameter has already been upcased */
    if (!strcmp(cptr, "DD")) {
        tarbell_info->ddEnabled = TRUE;
        tarbell_info->pnp.io_size = TARBELL_IOSIZE_DD;
    } else if (!strcmp(cptr, "SD")) {
        tarbell_info->ddEnabled = FALSE;
        tarbell_info->pnp.io_size = TARBELL_IOSIZE_SD;
    } else {
        return SCPE_ARG;
    }

    /* Reset the device if enabled */
    if (!(tarbell_dev.flags & DEV_DIS)) {
        tarbell_dev.flags |= DEV_DIS;
        tarbell_reset(&tarbell_dev);
        tarbell_dev.flags &= ~DEV_DIS;
        tarbell_reset(&tarbell_dev);
    }


    return SCPE_OK;
}

static t_stat tarbell_show_model(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "MODEL=%s", (tarbell_info->ddEnabled) ? "DD" : "SD");

    return SCPE_OK;
}

static t_stat tarbell_set_prom(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (!cptr) return SCPE_IERR;
    if (!strlen(cptr)) return SCPE_ARG;

    /* this assumes that the parameter has already been upcased */
    if (!strncmp(cptr, "ENABLE", strlen(cptr))) {
        tarbell_info->promEnabled = TRUE;
    } else if (!strncmp(cptr, "DISABLE", strlen(cptr))) {
        tarbell_info->promEnabled = FALSE;
    } else {
        return SCPE_ARG;
    }

    return SCPE_OK;
}

static t_stat tarbell_show_prom(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s", (tarbell_info->promEnabled) ? "PROM" : "NOPROM");

    return SCPE_OK;
}

static t_stat tarbell_boot(int32 unitno, DEVICE *dptr)
{

    PNP_INFO *pnp = (PNP_INFO *)dptr->ctxt;

    sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Booting Controller at 0x%04x\n", pnp->mem_base);

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
    sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &tarbell_dev, TARBELL_SNAME ": %s track/sector %02d/%03d:\n\t", isRead ? "Read" : "Write", tarbell_info->FD17XX.track, tarbell_info->FD17XX.sector);
    for (i=0; i < TARBELL_SECTOR_LEN; i++) {
        sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &tarbell_dev, "%02X ", sdata[i]);
        if (((i+1) & 0xf) == 0) {
            sim_debug(isRead ? RD_DATA_DETAIL_MSG : WR_DATA_DETAIL_MSG, &tarbell_dev, "\n\t");
        }
    }
    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &tarbell_dev, "\n"); 
}

static uint32 secs_per_track(uint8 track)
{
    /* Track 0 / side 0 is always single density */
    int32 secs = (tarbell_info->doubleDensity[tarbell_info->currentDrive] &&
        (tarbell_info->side[tarbell_info->currentDrive] || track > 0)) ? TARBELL_SPT_DD : TARBELL_SPT_SD;

    tarbell_info->secsPerTrack = secs;

    return secs;
}

static uint32 bytes_per_track(uint8 track)
{
    int32 bytes;
    int32 dd;

    dd = tarbell_info->doubleDensity[tarbell_info->currentDrive];

    if (dd) {
        bytes = IBM3740_TRK_HDR_LEN + 247 + (TARBELL_SPT_DD * IBM3740_DD_SEC_LEN);
    } else {
        bytes = IBM3740_TRK_HDR_LEN + 247 + (TARBELL_SPT_SD * IBM3740_SD_SEC_LEN);
    }

    tarbell_info->bytesPerTrack = bytes;

    return bytes;
}

static uint32 calculate_tarbell_sec_offset(uint8 track, uint8 sector)
{
    uint32 offset;
    uint8 ds = tarbell_info->side[tarbell_info->currentDrive];

    /*
    ** Side 0: tracks 0-76
    ** Side 1: tracks 77-153
    */
    if (ds) {
        track += 77;
    }

    /*
    ** Calculate track offset
    */
    if (track==0) {
        offset=0;
    } else {
        offset=TARBELL_SPT_SD * TARBELL_SECTOR_LEN; /* Track 0 / Side 0 always SD */
        offset+=(track-1) * secs_per_track(track) * TARBELL_SECTOR_LEN; /* Track 1-153 */
    }

    /*
    ** Add sector offset to track offset
    */
    offset += (sector-1)*TARBELL_SECTOR_LEN;

    DBG_PRINT(("TARBELL: OFFSET drive=%d side=%d den=%d track=%03d sector=%03d\n", tarbell_info->currentDrive, ds, tarbell_info->doubleDensity[tarbell_info->currentDrive], track, sector));

    return (offset);
}

static void TARBELL_HeadLoad(UNIT *uptr, FD17XX_REG *pFD17XX, uint8 load)
{
    /*
    ** If no disk has been attached, uptr will be NULL - return
    */
    if (uptr == NULL) {
        return;
    }

    if (load) {
        sim_activate_after_abs(uptr, tarbell_info->headTimeout);  /* activate timer */

        if (tarbell_info->headLoaded[tarbell_info->currentDrive] == FALSE) {
            sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Drive %d head Loaded.\n", tarbell_info->currentDrive);
        }
    }

    if (load == FALSE && tarbell_info->headLoaded[tarbell_info->currentDrive] == TRUE) {
        sim_cancel(uptr);            /* cancel timer */
        sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Drive %d head Unloaded.\n", tarbell_info->currentDrive);
    }

    tarbell_info->headLoaded[tarbell_info->currentDrive] = load;
}

static uint8 TARBELL_Read(uint32 Addr)
{
    uint8 cData;
    uint8 driveNum;
    FD17XX_REG *pFD17XX;
    UNIT *uptr;

    driveNum = tarbell_info->currentDrive;
    uptr = tarbell_info->uptr[driveNum];
    pFD17XX = &tarbell_info->FD17XX;

    switch(Addr & 0x07) {
        case TARBELL_REG_STATUS:
            cData = pFD17XX->status;
            break;

        case TARBELL_REG_TRACK:
            cData = pFD17XX->track;
            break;

        case TARBELL_REG_DATA:
            /*
            ** If a READ operation is currently active, get the next byte
            */
            if (pFD17XX->readActive) {
                /* Store byte in DATA register */
                pFD17XX->data = sdata[pFD17XX->dataCount++];

                /* If we reached the end of the sector, terminate command and set INTRQ */
                if (pFD17XX->dataCount == TARBELL_SECTOR_LEN) {
                    pFD17XX->readActive = FALSE;
                    pFD17XX->dataCount = 0;
                    pFD17XX->status = 0x00;
                    pFD17XX->intrq = TRUE;
                } else {
                    pFD17XX->status |= FD17XX_STAT_DRQ; /* Another byte is ready */
                }

                TARBELL_HeadLoad(uptr, pFD17XX, TRUE);
            } else if (pFD17XX->readTrkActive) {
                /* If we reached the end of the track data, terminate command and set INTRQ */
                if (pFD17XX->trkCount == bytes_per_track(pFD17XX->track)) {
                    pFD17XX->readTrkActive = FALSE;
                    pFD17XX->status = 0x00;
                    pFD17XX->intrq = TRUE;
                } else {
                    pFD17XX->trkCount++;

                    pFD17XX->status |= FD17XX_STAT_DRQ; /* Another byte is ready */
                }

                TARBELL_HeadLoad(uptr, pFD17XX, TRUE);
            } else if (pFD17XX->addrActive) {
                /* Store byte in DATA register */
                pFD17XX->data = sdata[pFD17XX->dataCount++];

                DBG_PRINT(("TARBELL: READ ADDR data=%03d\n", pFD17XX->data));

                /* If we reached the end of the address data, terminate command and set INTRQ */
                if (pFD17XX->dataCount > TARBELL_ADDR_CRC2) {
                    pFD17XX->addrActive = FALSE;
                    pFD17XX->status = 0x00;
                    pFD17XX->intrq = TRUE;
                } else {
                    pFD17XX->status |= FD17XX_STAT_DRQ; /* Another byte is ready */
                }

                TARBELL_HeadLoad(uptr, pFD17XX, TRUE);
            }

            cData = pFD17XX->data;
            break;

        case TARBELL_REG_SECTOR:
            cData = pFD17XX->sector;
            break;

        case TARBELL_REG_WAIT:
            cData = (pFD17XX->intrq) ? 0x00 : 0x80;   /* Bit 7 True if DRQ */
            break;

        case TARBELL_REG_DMASTAT:
            cData = 0x00;    /* Always show DMA is complete */
            break;

        default:
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READ Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    DBG_PRINT(("TARBELL: READ COMPLETE currentDrive=%d doubleDensity=%d sector=%03d track=%02d data=%02x\n", tarbell_info->currentDrive, tarbell_info->doubleDensity[tarbell_info->currentDrive], pFD17XX->track, pFD17XX->sector, pFD17XX->data));

    return (cData);
}

static uint8 TARBELL_Write(uint32 Addr, int32 Data)
{
    uint8 cData;
    uint8 driveNum;
    int32 rtn;
    UNIT *uptr;
    FD17XX_REG *pFD17XX;

    sim_debug(CMD_MSG, &tarbell_dev, TARBELL_SNAME ": OUT %02x Data %02x\n", Addr & 0xFF, Data & 0xFF);

    cData = 0;
    driveNum = tarbell_info->currentDrive;
    uptr = tarbell_info->uptr[driveNum];
    pFD17XX = &tarbell_info->FD17XX;

    switch(Addr & 0x07) {
        case TARBELL_REG_COMMAND:
            cData = TARBELL_Command(uptr, pFD17XX, Data);
            break;

        case TARBELL_REG_DATA:
            pFD17XX->data = Data;   /* Store byte in DATA register */

            if (pFD17XX->writeActive) {

                /* Store DATA register in Sector Buffer */
                sdata[pFD17XX->dataCount++] = pFD17XX->data;

                /* If we reached the end of the sector, write sector, terminate command and set INTRQ */
                if (pFD17XX->dataCount == TARBELL_SECTOR_LEN) {
                    pFD17XX->status = 0x00;  /* Clear Status Bits */

                    rtn = TARBELL_WriteSector(uptr, pFD17XX->track, pFD17XX->sector, sdata);

                    showdata(FALSE);

                    if (rtn != TARBELL_SECTOR_LEN) {
                        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": sim_fwrite errno=%d\n", errno);

                        pFD17XX->status |= FD17XX_STAT_WRITEFAULT;
                    }
                    pFD17XX->writeActive = FALSE;
                    pFD17XX->dataCount = 0;
                    pFD17XX->intrq = TRUE;
                } else {
                    pFD17XX->status |= FD17XX_STAT_DRQ; /* Ready for another byte */
                }

                TARBELL_HeadLoad(uptr, pFD17XX, TRUE);
            } else if (pFD17XX->writeTrkActive) {

                if (pFD17XX->dataAddrMrk) {
                    /* Store DATA register in Sector Buffer */
                    sdata[pFD17XX->dataCount++] = pFD17XX->data;

                    /* If we reached the end of the sector, write sector */
                    if (pFD17XX->dataCount == TARBELL_SECTOR_LEN) {
                        pFD17XX->status &= ~FD17XX_STAT_WRITEFAULT;  /* Clear Status Bit */

                        rtn = TARBELL_WriteSector(uptr, pFD17XX->track, pFD17XX->sector, sdata);

                        if (rtn != TARBELL_SECTOR_LEN) {
                            pFD17XX->status |= FD17XX_STAT_WRITEFAULT;
                            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": WRITE ERROR could not write track %03d sector %03d\n", pFD17XX->track, pFD17XX->sector);
                        }

                        DBG_PRINT(("TARBELL: WRITE TRACK drive=%d track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X\n", driveNum, pFD17XX->track, pFD17XX->sector, pFD17XX->trkCount, pFD17XX->dataCount, pFD17XX->data, pFD17XX->status));

                        pFD17XX->dataCount = 0;
                        pFD17XX->dataAddrMrk = 0;

                        if (pFD17XX->sector < secs_per_track(pFD17XX->track)) {
                            pFD17XX->sector++;
                        }
                    }
                } else if (pFD17XX->data == 0xFB) {
                        pFD17XX->dataAddrMrk = 1;
                        DBG_PRINT(("TARBELL: 0xFB Address Mark Found\n"));
                }

                /*
                ** Increment number for bytes written to track
                */
                pFD17XX->trkCount++;

                if (pFD17XX->trkCount < bytes_per_track(pFD17XX->track)) {
                    pFD17XX->status |= FD17XX_STAT_DRQ; /* Ready for another byte */
                } else {
                    pFD17XX->status = 0x00;  /* Clear Status Bits */
                    pFD17XX->intrq = TRUE;
                    pFD17XX->status &= ~FD17XX_STAT_BUSY;  /* Clear BUSY Bit */
                    pFD17XX->writeTrkActive = FALSE;
                    sim_debug(WR_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": WRITE TRACK COMPLETE track=%03d sector=%03d trkcount=%d datacount=%d data=%02X status=%02X\n", pFD17XX->track, pFD17XX->sector, pFD17XX->trkCount, pFD17XX->dataCount, pFD17XX->data, pFD17XX->status);
                }

                TARBELL_HeadLoad(uptr, pFD17XX, TRUE);
            }

            DBG_PRINT(("TARBELL: WRITE DATA REG %02X\n", Data));
            break;

        case TARBELL_REG_TRACK:
            pFD17XX->track = Data;
            DBG_PRINT(("TARBELL: TRACK REG=%d\n", pFD17XX->track));
            break;

        case TARBELL_REG_SECTOR:
            pFD17XX->sector = Data;

            DBG_PRINT(("TARBELL: SECTOR REG=%d\n", pFD17XX->sector));

            break;

        case TARBELL_REG_DRVSEL:
            /* Drive Select */
            if (tarbell_info->ddEnabled) {    /* Tarbell DD Controller */
                cData = (Data & TARBELL_DSEL_MASK) >> 4;

                /* Density */
                tarbell_info->doubleDensity[cData] = (Data & TARBELL_DENS_MASK) != 0x00;

                /* Side */
                tarbell_info->side[cData] = (Data & TARBELL_SIDE_MASK) != 0x00;

                DBG_PRINT(("REG_DRVSEL=%02X\n", Data));

            } else {
                cData = ~(Data >> 4) & 0x03;    /* Tarbell SD drive bits inverted */
            }

            if (tarbell_info->currentDrive != cData) {
                sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Current drive now %d\n", cData);
            }

            tarbell_info->currentDrive = cData;

            break;

        case TARBELL_REG_EXTADDR:
            cData = 0x00;
            break;

        default:
            sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": WRITE Invalid I/O Address %02x (%02x)\n", Addr & 0xFF, Addr & 0x07);
            cData = 0xff;
            break;
    }

    DBG_PRINT(("TARBELL: WRITE COMPLETE currentDrive=%d doubleDensity=%d sector=%03d track=%02d data=%02x\n", tarbell_info->currentDrive, tarbell_info->doubleDensity[tarbell_info->currentDrive], pFD17XX->track, pFD17XX->sector, pFD17XX->data));

    return(cData);
}

static uint32 TARBELL_ReadSector(UNIT *uptr, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 sec_offset;
    uint32 rtn = 0;

    if (uptr->fileref == NULL) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_tarbell_sec_offset(track, sector);

    sim_debug(RD_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC track %03d sector %03d at offset %04X\n", track, sector, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC sim_fseek error.\n");
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
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": READSEC uptr.fileref is NULL!\n");
        return 0;
    }

    sec_offset = calculate_tarbell_sec_offset(track, sector);

    sim_debug(WR_DATA_MSG, &tarbell_dev, TARBELL_SNAME ": WRITESEC track %03d sector %03d at offset %04X\n", track, sector, sec_offset);

    if (sim_fseek(uptr->fileref, sec_offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": WRITESEC sim_fseek error.\n");
        return 0;
    }

    rtn = sim_fwrite(buffer, 1, TARBELL_SECTOR_LEN, uptr->fileref);

    return rtn;
}


static uint8 TARBELL_Command(UNIT *uptr, FD17XX_REG *pFD17XX, int32 Data)
{
    uint8 cData;
    uint8 newTrack;
    uint8 statusUpdate;
    int32 rtn;

    cData = 0;
    statusUpdate = TRUE;

    if (uptr == NULL) {
        return cData;
    }

    pFD17XX->command = Data;

    /*
    ** Type II-IV Command
    */
    if (pFD17XX->command & 0x80) {
        pFD17XX->readActive = FALSE;
        pFD17XX->writeActive = FALSE;
        pFD17XX->readTrkActive = FALSE;
        pFD17XX->writeTrkActive = FALSE;
        pFD17XX->addrActive = FALSE;
        pFD17XX->dataCount = 0;

        pFD17XX->status &= ~FD17XX_STAT_DRQ;    /* Reset DRQ */
    }

    /*
    ** Set BUSY for all but Force Interrupt
    */
    if ((pFD17XX->command & TARBELL_CMD_FORCE_INTR) != TARBELL_CMD_FORCE_INTR) {
        pFD17XX->status |= FD17XX_STAT_BUSY;
    }

    pFD17XX->intrq = FALSE;

    switch(pFD17XX->command & 0xf0) {
        case TARBELL_CMD_RESTORE:
            pFD17XX->track = 0;

            sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": RESTORE track=%03d\n", pFD17XX->track);

            TARBELL_HeadLoad(uptr, pFD17XX, (Data & TARBELL_FLAG_H) ? TRUE : FALSE);

            pFD17XX->status &= ~FD17XX_STAT_SEEKERROR;
            pFD17XX->status &= ~FD17XX_STAT_BUSY;
            pFD17XX->status &= ~FD17XX_STAT_DRQ;
            pFD17XX->intrq = TRUE;
            break;

        case TARBELL_CMD_SEEK:
            newTrack = pFD17XX->data;

            DBG_PRINT(("TARBELL: TRACK DATA=%d (SEEK)\n", newTrack));

            pFD17XX->status &= ~FD17XX_STAT_SEEKERROR;

            if (newTrack < TARBELL_TRACKS) {
                pFD17XX->track = newTrack;

                TARBELL_HeadLoad(uptr, pFD17XX, (Data & TARBELL_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": SEEK       track=%03d\n", pFD17XX->track);
            } else {
                pFD17XX->status |= FD17XX_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": SEEK ERR   track=%03d\n", newTrack);
            }

            pFD17XX->status &= ~FD17XX_STAT_BUSY;
            pFD17XX->status &= ~FD17XX_STAT_DRQ;
            pFD17XX->intrq = TRUE;
            break;

        case TARBELL_CMD_STEP:
        case TARBELL_CMD_STEPU:
            pFD17XX->status &= ~FD17XX_STAT_SEEKERROR;

            newTrack = pFD17XX->track + pFD17XX->stepDir;

            if (newTrack < TARBELL_TRACKS) {
                if (Data & TARBELL_FLAG_U) {
                    pFD17XX->track = newTrack;
                }
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEP        track=%03d\n", pFD17XX->track);
            } else {
                pFD17XX->status |= FD17XX_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEP ERR    track=%03d\n", newTrack);
            }

            TARBELL_HeadLoad(uptr, pFD17XX, (Data & TARBELL_FLAG_H) ? TRUE : FALSE);

            pFD17XX->status &= ~FD17XX_STAT_BUSY;
            pFD17XX->status &= ~FD17XX_STAT_DRQ;
            pFD17XX->intrq = TRUE;
            break;

        case TARBELL_CMD_STEPIN:
        case TARBELL_CMD_STEPINU:
            pFD17XX->status &= ~FD17XX_STAT_SEEKERROR;

            if (pFD17XX->track < TARBELL_TRACKS-1) {
                if (Data & TARBELL_FLAG_U) {
                    pFD17XX->track++;
                }

                TARBELL_HeadLoad(uptr, pFD17XX, (Data & TARBELL_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPIN      track=%03d\n", pFD17XX->track);
            } else {
                pFD17XX->status |= FD17XX_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPIN ERR  track=%03d\n", pFD17XX->track+1);
            }

            pFD17XX->stepDir = 1;
            pFD17XX->status &= ~FD17XX_STAT_BUSY;
            pFD17XX->status &= ~FD17XX_STAT_DRQ;
            pFD17XX->intrq = TRUE;
            break;

        case TARBELL_CMD_STEPOUT:
        case TARBELL_CMD_STEPOUTU:
            pFD17XX->status &= ~FD17XX_STAT_SEEKERROR;

            if (pFD17XX->track > 0) {
                if (Data & TARBELL_FLAG_U) {
                    pFD17XX->track--;
                }

                TARBELL_HeadLoad(uptr, pFD17XX, (Data & TARBELL_FLAG_H) ? TRUE : FALSE);

                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPOUT     track=%03d\n", pFD17XX->track);
            } else {
                pFD17XX->status |= FD17XX_STAT_SEEKERROR;
                sim_debug(SEEK_MSG, &tarbell_dev, TARBELL_SNAME ": STEPOUT ERR track=%03d\n", pFD17XX->track-1);
            }

            pFD17XX->stepDir = -1;
            pFD17XX->status &= ~FD17XX_STAT_BUSY;
            pFD17XX->status &= ~FD17XX_STAT_DRQ;
            pFD17XX->intrq = TRUE;
            break;

        case TARBELL_CMD_READ:

            if ((uptr == NULL) || (uptr->fileref == NULL)) {
                sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": " ADDRESS_FORMAT
                          " Drive: %d not attached - read ignored.\n",
                          PCX, tarbell_info->currentDrive);

                pFD17XX->status &= ~FD17XX_STAT_BUSY;

                return cData;
            }

            rtn = TARBELL_ReadSector(uptr, pFD17XX->track, pFD17XX->sector, sdata);

            if (rtn == TARBELL_SECTOR_LEN) {
                pFD17XX->readActive = TRUE;

                showdata(TRUE);
            } else {
                sim_debug(ERROR_MSG, &tarbell_dev, TARBELL_SNAME ": sim_fread errno=%d\n", errno);

                pFD17XX->status |= FD17XX_STAT_NOTFOUND;
                pFD17XX->intrq = TRUE;
            }

            break;

        case TARBELL_CMD_WRITE:
            /*
            ** If no disk in drive, return
            */
            if ((uptr == NULL) || (uptr->fileref == NULL)) {
                sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": " ADDRESS_FORMAT
                          " Drive: %d not attached - write ignored.\n",
                          PCX, tarbell_info->currentDrive);

                pFD17XX->status &= ~FD17XX_STAT_BUSY;
            }

            if ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) {
                DBG_PRINT((TARBELL_SNAME ": Disk write protected. uptr->flags=%04x writeProtect=%04x\n", uptr->flags & UNIT_TARBELL_WPROTECT, tarbell_info->writeProtect));
                pFD17XX->intrq = TRUE;
            } else {
                pFD17XX->writeActive = TRUE;
                pFD17XX->dataCount = 0;
                pFD17XX->status |= FD17XX_STAT_DRQ;    /* Set DRQ */
            }

            break;

        case TARBELL_CMD_READ_ADDRESS:
            sdata[TARBELL_ADDR_TRACK]=pFD17XX->track;
            sdata[TARBELL_ADDR_ZEROS]=0;
            sdata[TARBELL_ADDR_SECTOR]=pFD17XX->sector;
            sdata[TARBELL_ADDR_LENGTH]=TARBELL_SECTOR_LEN;
            sdata[TARBELL_ADDR_CRC1]=0;
            sdata[TARBELL_ADDR_CRC2]=0;

            pFD17XX->addrActive = TRUE;
            pFD17XX->status |= FD17XX_STAT_DRQ;    /* Set DRQ */

            break;

        case TARBELL_CMD_READ_TRACK:
            pFD17XX->readTrkActive = TRUE;
            pFD17XX->trkCount=0;
            pFD17XX->dataCount=0;
            pFD17XX->sector=1;
            pFD17XX->status |= FD17XX_STAT_DRQ;    /* Set DRQ */
            break;

        case TARBELL_CMD_WRITE_TRACK:
            if ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) {
                DBG_PRINT((TARBELL_SNAME ": Disk write protected. uptr->flags=%04x writeProtect=%04x\n", uptr->flags & UNIT_TARBELL_WPROTECT, tarbell_info->writeProtect));
                pFD17XX->intrq = TRUE;
            } else {
                pFD17XX->writeTrkActive = TRUE;
                pFD17XX->trkCount=0;
                pFD17XX->dataCount=0;
                pFD17XX->sector=1;
                pFD17XX->dataAddrMrk=0;
                pFD17XX->status |= FD17XX_STAT_DRQ;    /* Set DRQ */
            }
            break;

        case TARBELL_CMD_FORCE_INTR:
            if (pFD17XX->status & FD17XX_STAT_BUSY) {
                pFD17XX->status &= ~FD17XX_STAT_BUSY;
                statusUpdate = FALSE;
            }

            /* Reset Status */
            pFD17XX->dataCount = 0;
            pFD17XX->trkCount = 0;
            pFD17XX->readActive = FALSE;
            pFD17XX->readTrkActive = FALSE;
            pFD17XX->writeActive = FALSE;
            pFD17XX->writeTrkActive = FALSE;
            pFD17XX->addrActive = FALSE;

            break;

        default:
            cData=0xFF;
            sim_debug(ERROR_MSG, &tarbell_dev, "TARBELL: UNRECOGNIZED CMD %02X\n", pFD17XX->command);
            pFD17XX->intrq = TRUE;
            break;
    }

    /**************************/
    /* Update Status Register */
    /**************************/

    /* drive not ready bit */
    pFD17XX->status &= ~FD17XX_STAT_NOTREADY;
    pFD17XX->status |= (uptr->fileref == NULL) ? FD17XX_STAT_NOTREADY : 0x00;

    switch(pFD17XX->command & 0xf0) {
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
                pFD17XX->status &= ~FD17XX_STAT_HEADLOAD;
                pFD17XX->status &= ~FD17XX_STAT_WRITEPROT;
                pFD17XX->status &= ~FD17XX_STAT_CRCERROR;
                pFD17XX->status &= ~FD17XX_STAT_TRACK0;
                pFD17XX->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD17XX_STAT_WRITEPROT : 0x00;
                pFD17XX->status |= (pFD17XX->track) ? 0x00 : FD17XX_STAT_TRACK0;
                pFD17XX->status |= (tarbell_info->headLoaded[tarbell_info->currentDrive]) ? FD17XX_STAT_HEADLOAD : 0x00;
                pFD17XX->status |= (pFD17XX->status & FD17XX_STAT_NOTREADY) ? 0x00 : FD17XX_STAT_INDEX;    /* Always set Index Flag if Drive Ready */
            }
            break;

        case TARBELL_CMD_READ:
            pFD17XX->status &= ~FD17XX_STAT_LOSTDATA;
            pFD17XX->status &= ~FD17XX_STAT_NOTFOUND;
            pFD17XX->status &= ~FD17XX_STAT_CRCERROR;
            pFD17XX->status &= ~FD17XX_STAT_RTYPELSB;
            break;

        case TARBELL_CMD_WRITE:
            pFD17XX->status &= ~FD17XX_STAT_WRITEPROT;
            pFD17XX->status &= ~FD17XX_STAT_LOSTDATA;
            pFD17XX->status &= ~FD17XX_STAT_NOTFOUND;
            pFD17XX->status &= ~FD17XX_STAT_CRCERROR;
            pFD17XX->status &= ~FD17XX_STAT_RTYPELSB;
            pFD17XX->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD17XX_STAT_WRITEPROT : 0x00;
            break;

        case TARBELL_CMD_READ_ADDRESS:
            pFD17XX->status &= ~0x20;
            pFD17XX->status &= ~0x40;
            pFD17XX->status &= ~FD17XX_STAT_LOSTDATA;
            pFD17XX->status &= ~FD17XX_STAT_NOTFOUND;
            pFD17XX->status &= ~FD17XX_STAT_CRCERROR;
            break;

        case TARBELL_CMD_READ_TRACK:
            pFD17XX->status &= ~0x08;
            pFD17XX->status &= ~0x10;
            pFD17XX->status &= ~0x20;
            pFD17XX->status &= ~0x40;
            pFD17XX->status &= ~FD17XX_STAT_LOSTDATA;
            break;

        case TARBELL_CMD_WRITE_TRACK:
            pFD17XX->status &= ~0x08;
            pFD17XX->status &= ~0x10;
            pFD17XX->status &= ~FD17XX_STAT_WRITEPROT;
            pFD17XX->status &= ~FD17XX_STAT_LOSTDATA;
            pFD17XX->status |= ((uptr->flags & UNIT_TARBELL_WPROTECT) || tarbell_info->writeProtect) ? FD17XX_STAT_WRITEPROT : 0x00;
            break;
    }

    sim_debug(CMD_MSG, &tarbell_dev,
            TARBELL_SNAME ": CMD cmd=%02X drive=%d side=%d track=%03d sector=%03d status=%02X\n",
            pFD17XX->command, tarbell_info->currentDrive,
            tarbell_info->side[tarbell_info->currentDrive],
            pFD17XX->track, pFD17XX->sector, pFD17XX->status);

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
            sim_debug(STATUS_MSG, &tarbell_dev, TARBELL_SNAME ": Boot PROM disabled.\n");
        }

        if (tarbell_info->promEnabled == TRUE && Addr < TARBELL_PROM_SIZE) {
            return(tarbell_prom[Addr & TARBELL_PROM_MASK]);
        } else {
            return(tarbell_ram[Addr & TARBELL_RAM_MASK]);
        }
    }
}

static int32 tarbelldma(int32 Addr, int32 rw, int32 data)
{
    return 0x00;    /* DMA is not implemented */
}

