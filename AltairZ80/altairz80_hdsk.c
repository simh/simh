/*  altairz80_hdsk.c: simulated hard disk device to increase capacity

    Copyright (c) 2002-2014, Peter Schorn

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

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.

    Contains code from Howard M. Harte for defining and changing disk geometry.
*/

#include "altairz80_defs.h"
#include <assert.h>
#include "sim_imd.h"

/* Debug flags */
#define READ_MSG    (1 << 0)
#define WRITE_MSG   (1 << 1)
#define VERBOSE_MSG (1 << 2)

/* The following routines are based on work from Howard M. Harte */
static t_stat set_geom(UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat show_geom(FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat set_format(UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat show_format(FILE *st, UNIT *uptr, int32 val, void *desc);

static t_stat hdsk_reset(DEVICE *dptr);
static t_stat hdsk_attach(UNIT *uptr, char *cptr);
static t_stat hdsk_detach(UNIT *uptr);
static uint32 is_imd(const UNIT *uptr);
static void assignFormat(UNIT *uptr);
static void verifyDiskInfo(const DISK_INFO *info, const char unitChar);

#define UNIT_V_HDSK_WLK         (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_HDSK_WLK           (1 << UNIT_V_HDSK_WLK)
#define HDSK_MAX_SECTOR_SIZE    1024            /* maximum size of a sector                 */
#define HDSK_SECTOR_SIZE        u5              /* size of sector                           */
#define HDSK_SECTORS_PER_TRACK  u4              /* sectors per track                        */
#define HDSK_NUMBER_OF_TRACKS   u3              /* number of tracks                         */
#define HDSK_FORMAT_TYPE        u6              /* Disk Format Type                         */
#define HDSK_CAPACITY           (2048*32*128)   /* Default Altair HDSK Capacity             */
#define HDSK_NUMBER             8               /* number of hard disks                     */
#define CPM_OK                  0               /* indicates to CP/M everything ok          */
#define CPM_ERROR               1               /* indicates to CP/M an error condition     */
#define CPM_EMPTY               0xe5            /* default value for non-existing bytes     */
#define HDSK_NONE               0
#define HDSK_RESET              1
#define HDSK_READ               2
#define HDSK_WRITE              3
#define HDSK_PARAM              4
#define HDSK_BOOT_ADDRESS       0x5c00
#define DPB_NAME_LENGTH         15
#define BOOTROM_SIZE_HDSK       256

extern uint32 PCX;
extern REG *sim_PC;
extern UNIT cpu_unit;

extern void install_ALTAIRbootROM(void);
extern void PutBYTEWrapper(const uint32 Addr, const uint32 Value);
extern uint8 GetBYTEWrapper(const uint32 Addr);
extern t_stat install_bootrom(int32 bootrom[], int32 size, int32 addr, int32 makeROM);
extern int32 bootrom_dsk[];
extern t_stat set_iobase(UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
        int32 (*routine)(const int32, const int32, const int32), uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

static t_stat hdsk_boot(int32 unitno, DEVICE *dptr);
int32 hdsk_io(const int32 port, const int32 io, const int32 data);

static int32 hdskLastCommand        = HDSK_NONE;
static int32 hdskCommandPosition    = 0;
static int32 parameterCount         = 0;
static int32 selectedDisk;
static int32 selectedSector;
static int32 selectedTrack;
static int32 selectedDMA;

typedef struct {
    char    name[DPB_NAME_LENGTH + 1];  /* name of CP/M disk parameter block                        */
    t_addr  capac;                      /* capacity                                                 */
    uint16  spt;                        /* sectors per track                                        */
    uint8   bsh;                        /* data allocation block shift factor                       */
    uint8   blm;                        /* data allocation block mask                               */
    uint8   exm;                        /* extent mask                                              */
    uint16  dsm;                        /* maximum data block number                                */
    uint16  drm;                        /* total number of directory entries                        */
    uint8   al0;                        /* determine reserved directory blocks                      */
    uint8   al1;                        /* determine reserved directory blocks                      */
    uint16  cks;                        /* size of directory check vector                           */
    uint16  off;                        /* number of reserved tracks                                */
    uint8   psh;                        /* physical record shift factor, CP/M 3                     */
    uint8   phm;                        /* physical record mask, CP/M 3                             */
    int32   physicalSectorSize;         /* 0 for 128 << psh, > 0 for special                        */
    int32   offset;                     /* offset in physical sector where logical sector starts    */
    int32   *skew;                      /* pointer to skew table or NULL                            */
} DPB;

typedef struct {
    PNP_INFO    pnp;    /* Plug and Play */
} HDSK_INFO;

#define SPT16   16
#define SPT32   32
#define SPT26   26
#define SPT52   52

static HDSK_INFO hdsk_info_data = { { 0x0000, 0, 0xFD, 1 } };

static int32 standard8[SPT26]           = { 0,  6,  12, 18, 24, 4,  10, 16,
                                            22, 2,  8,  14, 20, 1,  7,  13,
                                            19, 25, 5,  11, 17, 23, 3,  9,
                                            15, 21                          };

static int32 apple_ii_DOS[SPT16]       = { 0,  6,  12, 3,  9,  15, 14, 5,
                                            11, 2,  8,  7,  13, 4,  10, 1   };

static int32 apple_ii_DOS2[SPT32]      = { 0,  1,  12, 13, 24, 25, 6,  7,
                                            18, 19, 30, 31, 28, 29, 10, 11,
                                            22, 23, 4,  5,  16, 17, 14, 15,
                                            26, 27, 8,  9,  20, 21, 2,  3   };

static int32 apple_ii_PRODOS[SPT16]    = { 0,  9,  3,  12, 6,  15, 1,  10,
                                            4,  13, 7,  8,  2,  11, 5,  14  };

static int32 apple_ii_PRODOS2[SPT32]   = { 0,  1,  18, 19, 6,  7,  24, 25,
                                            12, 13, 30, 31, 2,  3,  20, 21,
                                            8,  9,  26, 27, 14, 15, 16, 17,
                                            4,  5,  22, 23, 10, 11, 28, 29  };

static int32 mits[SPT32]                = { 0,  17, 2,  19, 4,  21, 6,  23,
                                            8,  25, 10, 27, 12, 29, 14, 31,
                                            16, 1,  18, 3,  20, 5,  22, 7,
                                            24, 9,  26, 11, 28, 13, 30, 15  };

/* Note in the following CKS = 0 for fixed media which are not supposed to be
 changed while CP/M is executing. Also note that spt (sectors per track) is
 measured in CP/M sectors of size 128 bytes. Standard format "HDSK" must be
 first as index 0 is used as default in some cases.
 */
static DPB dpb[] = {
/*      name    capac           spt     bsh     blm     exm     dsm     drm
        al0     al1     cks     off     psh     phm     ss      off skew                                                */
    { "HDSK",   HDSK_CAPACITY,  32,     0x05,   0x1F,   0x01,   0x07f9, 0x03FF,
        0xFF,   0x00,   0x0000, 0x0006, 0x00,   0x00,   0,      0,  NULL },             /* AZ80 HDSK                    */

    { "EZ80FL", 131072,         32,     0x03,   0x07,   0x00,   127,    0x003E,
        0xC0,   0x00,   0x0000, 0x0000, 0x02,   0x03,   0,      0,  NULL },             /* 128K FLASH                   */

    { "P112",   1474560,        72,     0x04,   0x0F,   0x00,   710,    0x00FE,
        0xF0,   0x00,   0x0000, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* 1.44M P112                   */

    { "SU720",  737280,         36,     0x04,   0x0F,   0x00,   354,    0x007E,
        0xC0,   0x00,   0x0020, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* 720K Super I/O               */

    { "OSB1",   102400,         20,     0x04,   0x0F,   0x01,   45,     0x003F,
        0x80,   0x00,   0x0000, 0x0003, 0x02,   0x03,   0,      0,  NULL },             /* Osborne1 5.25" SS SD         */

    { "OSB2",   204800,         40,     0x03,   0x07,   0x00,   184,    0x003F,
        0xC0,   0x00,   0x0000, 0x0003, 0x02,   0x03,   0,      0,  NULL },             /* Osborne1 5.25" SS DD         */

    { "NSSS1",  179200,         40,     0x03,   0x07,   0x00,   0xA4,   0x003F,
        0xC0,   0x00,   0x0010, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Northstar SSDD Format 1      */

    { "NSSS2",  179200,         40,     0x04,   0x0F,   0x01,   0x51,   0x003F,
        0x80,   0x00,   0x0010, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Northstar SSDD Format 2      */

    { "NSDS2",  358400,         40,     0x04,   0x0F,   0x01,   0xA9,   0x003F,
        0x80,   0x00,   0x0010, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Northstar DSDD Format 2      */

    { "VGSS",   315392,         32,     0x04,   0x0F,   0x00,   149,    0x007F,
        0xC0,   0x00,   0x0020, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Vector SS SD                 */

    { "VGDS",   630784,         32,     0x04,   0x0F,   0x00,   299,    0x007F,
        0xC0,   0x00,   0x0020, 0x0004, 0x02,   0x03,   0,      0,  NULL },             /* Vector DS SD                 */

    { "DISK1A", 630784,         64,     0x04,   0x0F,   0x00,   299,    0x007F,
        0xC0,   0x00,   0x0020, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* CompuPro Disk1A 8" SS SD     */

    { "SSSD8",  256256,         SPT26,  0x03,   0x07,   0x00,   242,    0x003F,
        0xC0,   0x00,   0x0000, 0x0002, 0x00,   0x00,   0,      0,  NULL },             /* Standard 8" SS SD            */

    { "SSSD8S",  256256,        SPT26,  0x03,   0x07,   0x00,   242,    0x003F,
        0xC0,   0x00,   0x0000, 0x0002, 0x00,   0x00,   0,      0,  standard8 },        /* Standard 8" SS SD with skew  */

    { "SSDD8",  512512,         SPT52,  0x04,   0x0F,   0x01,   242,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x01,   0x01,   0,      0,  NULL },             /* Standard 8" SS DD            */
    
    { "SSDD8S", 512512,         SPT52,  0x04,   0x0F,   0x01,   242,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x01,   0x01,   0,      0,  standard8 },        /* Standard 8" SS DD with skew  */
    
    { "DSDD8",  1025024,        SPT52,  0x04,   0x0F,   0x00,   493,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x01,   0x01,   0,      0,  NULL },             /* Standard 8" DS DD            */
    
    { "DSDD8S", 1025024,        SPT52,  0x04,   0x0F,   0x00,   493,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x01,   0x01,   0,      0,  NULL },             /* Standard 8" DS DD with skew  */
    
    {"512SSDD8",591360,         60,     0x04,   0x0F,   0x00,   280,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Standard 8" SS DD with 512 byte sectors  */
    
    {"512DSDD8",1182720,        60,     0x04,   0x0F,   0x00,   569,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x02,   0x03,   0,      0,  NULL },             /* Standard 8" DS DD with 512 byte sectors  */
 
#if 0
    /* CP/M 3 BIOS currently does not support physical sector size 1024 */
    {"1024SSDD8",630784,        64,     0x04,   0x0F,   0x00,   299,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x03,   0x07,   0,      0,  NULL },             /* Standard 8" SS DD with 1024 byte sectors  */
    
    {"1024DSDD8",1261568,       64,     0x04,   0x0F,   0x00,   607,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x03,   0x07,   0,      0,  NULL },             /* Standard 8" DS DD with 1024 byte sectors  */
#endif
    
    { "APPLE-DO",143360,        SPT32,  0x03,   0x07,   0x00,   127,    0x003F,
        0xC0,   0x00,   0x0000, 0x0003, 0x01,   0x01,   0,      0,  apple_ii_DOS },     /* Apple II DOS 3.3             */

    { "APPLE-PO",143360,        SPT32,  0x03,   0x07,   0x00,   127,    0x003F,
        0xC0,   0x00,   0x0000, 0x0003, 0x01,   0x01,   0,      0,  apple_ii_PRODOS },  /* Apple II PRODOS              */

    { "APPLE-D2",143360,        SPT32,  0x03,   0x07,   0x00,   127,    0x003F,
        0xC0,   0x00,   0x0000, 0x0003, 0x00,   0x00,   0,      0,  apple_ii_DOS2 },    /* Apple II DOS 3.3, deblocked */

    { "APPLE-P2",143360,        SPT32,  0x03,   0x07,   0x00,   127,    0x003F,
        0xC0,   0x00,   0x0000, 0x0003, 0x00,   0x00,   0,      0,  apple_ii_PRODOS2 }, /* Apple II PRODOS, deblocked  */

    { "MITS",   337568,         SPT32,  0x03,   0x07,   0x00,   254,    0x00FF,
        0xFF,   0x00,   0x0000, 0x0006, 0x00,   0x00,   137,    3,  mits },             /* MITS Altair original         */

    { "MITS2",  1113536,        SPT32,  0x04,   0x0F,   0x00,   0x1EF,  0x00FF,
        0xF0,   0x00,   0x0000, 0x0006, 0x00,   0x00,   137,    3,  mits },             /* MITS Altair original, extra  */

    /*
     dw     40              ;#128 byte records/track
     db     4,0fh           ;block shift mask (2K)
     db     1               ;extent  mask
     dw     194             ;maximun  block number
     dw     127             ;max number of dir entry - 1
     db     0C0H,00h        ;alloc vector for directory
     dw     0020h           ;checksum size
     dw     2               ;offset for sys tracks
     db     2,3             ;physical sector shift (512 sector)
     */
    { "V1050",  409600,             40, 0x04,   0x0F,   0x01,   194,    0x007F,
        0xC0,   0x00,   0x0000, 0x0002, 0x02,   0x03,   0,      0, NULL },              /* Visual Technology Visual 1050, http://www.metabarn.com/v1050/index.html  */

    { "", 0 }
};

static UNIT hdsk_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) }
};
static DISK_INFO* hdsk_imd[HDSK_NUMBER];

static REG hdsk_reg[] = {
    { DRDATA (HDCMD,        hdskLastCommand,        32),    REG_RO  },
    { DRDATA (HDPOS,        hdskCommandPosition,    32),    REG_RO  },
    { DRDATA (HDDSK,        selectedDisk,           32),    REG_RO  },
    { DRDATA (HDSEC,        selectedSector,         32),    REG_RO  },
    { DRDATA (HDTRK,        selectedTrack,          32),    REG_RO  },
    { DRDATA (HDDMA,        selectedDMA,            32),    REG_RO  },
    { NULL }
};

static MTAB hdsk_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,              "IOBASE",   "IOBASE", &set_iobase, &show_iobase, NULL },
    { MTAB_XTD|MTAB_VUN,    0,              "FORMAT",   "FORMAT", &set_format, &show_format, NULL },
    { UNIT_HDSK_WLK,        0,              "WRTENB",   "WRTENB", NULL  },
    { UNIT_HDSK_WLK,        UNIT_HDSK_WLK,  "WRTLCK",   "WRTLCK", NULL  },
    { MTAB_XTD|MTAB_VUN,    0,              "GEOM",     "GEOM", &set_geom, &show_geom, NULL },
    { 0 }
};

/* Debug Flags */
static DEBTAB hdsk_dt[] = {
    { "READ",       READ_MSG    },
    { "WRITE",      WRITE_MSG   },
    { "VERBOSE",    VERBOSE_MSG },
    { NULL,         0 }
};

DEVICE hdsk_dev = {
    "HDSK", hdsk_unit, hdsk_reg, hdsk_mod,
    8, 10, 31, 1, 8, 8,
    NULL, NULL, &hdsk_reset,
    &hdsk_boot, &hdsk_attach, &hdsk_detach,
    &hdsk_info_data, (DEV_DISABLE | DEV_DEBUG), 0,
    hdsk_dt, NULL, "Hard Disk HDSK"
};

/* Reset routine */
static t_stat hdsk_reset(DEVICE *dptr)  {
    PNP_INFO *pnp = (PNP_INFO *)dptr -> ctxt;
    if (dptr -> flags & DEV_DIS) {
        sim_map_resource(pnp -> io_base, pnp -> io_size, RESOURCE_TYPE_IO, &hdsk_io, TRUE);
    } else {
        /* Connect HDSK at base address */
        if (sim_map_resource(pnp -> io_base, pnp -> io_size, RESOURCE_TYPE_IO, &hdsk_io, FALSE) != 0) {
            printf("%s: error mapping I/O resource at 0x%04x\n", __FUNCTION__, pnp -> mem_base);
            dptr -> flags |= DEV_DIS;
            return SCPE_ARG;
        }
    }
    return SCPE_OK;
}

#ifdef _WIN32
#define strcasecmp _stricmp
#endif
static uint32 is_imd(const UNIT *uptr) {
    return ((uptr != NULL) && (uptr -> filename != NULL) && (strlen(uptr -> filename) > 3) &&
            (strcasecmp(".IMD", uptr -> filename + strlen(uptr -> filename) - 4) == 0));
}

static void assignFormat(UNIT *uptr) {
    uint32 i;
    uptr -> HDSK_FORMAT_TYPE = -1;            /* default to unknown format type       */
    for (i = 0; dpb[i].capac != 0; i++) {   /* find disk parameter block            */
        if (dpb[i].capac == uptr -> capac) {  /* found if correct capacity            */
            uptr -> HDSK_FORMAT_TYPE = i;
            break;
        }
    }
}

static void verifyDiskInfo(const DISK_INFO *info, const char unitChar) {
    uint32 track, head;
    if (info->ntracks < 1)
        printf("HDSK%c (IMD): WARNING: Number of tracks is 0.\n", unitChar);
    if (info->nsides < 1) {
        printf("HDSK%c (IMD): WARNING: Number of sides is 0.\n", unitChar);
        return;
    }
    for (track = 0; track < info->ntracks / info->nsides; track++)
        for (head = 0; head < info->nsides; head++) {
            if (info->track[track][head].nsects != info->track[1][0].nsects)
                printf("HDSK%c (IMD): WARNING: For track %i and head %i expected number of sectors "
                       "%i but got %i.\n", unitChar, track, head,
                       info->track[1][0].nsects, info->track[track][head].nsects);
            if (info->track[track][head].sectsize != info->track[1][0].sectsize)
                printf("HDSK%c (IMD): WARNING: For track %i and head %i expected sector size "
                       "%i but got %i.\n", unitChar, track, head,
                       info->track[1][0].sectsize, info->track[track][head].sectsize);
            if (info->track[track][head].start_sector != info->track[1][0].start_sector)
                printf("HDSK%c (IMD): WARNING: For track %i and head %i expected start sector "
                       "%i but got %i.\n", unitChar, track, head,
                       info->track[1][0].start_sector, info->track[track][head].start_sector);
        }
}

/* Attach routine */
static t_stat hdsk_attach(UNIT *uptr, char *cptr) {
    int32 thisUnitIndex;
    char unitChar;
    const t_stat r = attach_unit(uptr, cptr);           /* attach unit                          */
    if (r != SCPE_OK)                                   /* error?                               */
        return r;
    
    assert(uptr != NULL);
    thisUnitIndex = find_unit_index(uptr);
    unitChar = '0' + thisUnitIndex;
    assert((0 <= thisUnitIndex) && (thisUnitIndex < HDSK_NUMBER));
    
    if (is_imd(uptr)) {
        if ((sim_fsize(uptr -> fileref) == 0) &&
            (diskCreate(uptr -> fileref, "$Id: SIMH hdsk.c $") != SCPE_OK)) {
            printf("HDSK%c (IMD): Failed to create IMD disk.\n", unitChar);
            detach_unit(uptr);
            return SCPE_OPENERR;
        }
        hdsk_imd[thisUnitIndex] = diskOpen(uptr -> fileref, sim_deb && (hdsk_dev.dctrl & VERBOSE_MSG));
        if (hdsk_imd[thisUnitIndex] == NULL)
            return SCPE_IOERR;
        verifyDiskInfo(hdsk_imd[thisUnitIndex], '0' + thisUnitIndex);
        uptr -> HDSK_NUMBER_OF_TRACKS = hdsk_imd[thisUnitIndex] -> ntracks;
        uptr -> HDSK_SECTORS_PER_TRACK = hdsk_imd[thisUnitIndex] -> track[1][0].nsects;
        uptr -> HDSK_SECTOR_SIZE = hdsk_imd[thisUnitIndex] -> track[1][0].sectsize;
        uptr -> capac = ((uptr -> HDSK_NUMBER_OF_TRACKS) *
                         (uptr -> HDSK_SECTORS_PER_TRACK) *
                         (uptr -> HDSK_SECTOR_SIZE));
        assignFormat(uptr);
        if (uptr -> HDSK_FORMAT_TYPE == -1) {           /* Case 1: no disk parameter block found*/
            uptr -> HDSK_FORMAT_TYPE = 0;
            printf("HDSK%c (IMD): WARNING: Unsupported disk capacity, assuming HDSK type "
                   "with capacity %iKB.\n", unitChar, uptr -> capac / 1000);
            uptr -> flags |= UNIT_HDSK_WLK;
            printf("HDSK%c (IMD): WARNING: Forcing WRTLCK.\n", unitChar);
        }
        return SCPE_OK;
    }
    
    /* Step 1: Determine capacity of this disk														*/
    uptr -> capac = sim_fsize(uptr -> fileref);				/* the file length is a good indication */
    if (uptr -> capac == 0) {								/* file does not exist or has length 0  */
        uptr -> capac = (uptr -> HDSK_NUMBER_OF_TRACKS *
                         uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE);
        if (uptr -> capac == 0)
            uptr -> capac = HDSK_CAPACITY;
    }														/* post condition: uptr -> capac > 0	*/
    assert(uptr -> capac);
    
    /* Step 2: Determine format based on disk capacity												*/
    assignFormat(uptr);
    
    /* Step 3: Set number of sectors per track and sector size										*/
    if (uptr -> HDSK_FORMAT_TYPE == -1) {                 /* Case 1: no disk parameter block found	*/
        uptr -> HDSK_FORMAT_TYPE = 0;
        printf("HDSK%c: WARNING: Unsupported disk capacity, assuming HDSK type with capacity %iKB.\n",
               unitChar, uptr -> capac / 1000);
        uptr -> flags |= UNIT_HDSK_WLK;
        printf("HDSK%c: WARNING: Forcing WRTLCK.\n", unitChar);
        /* check whether capacity corresponds to setting of tracks, sectors per track and sector size	*/
        if (uptr -> capac != (uint32)(uptr -> HDSK_NUMBER_OF_TRACKS *
                                      uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE)) {
            printf("HDSK%c: WARNING: Fixing geometry.\n", unitChar);
            if (uptr -> HDSK_SECTORS_PER_TRACK == 0)
                uptr -> HDSK_SECTORS_PER_TRACK = 32;
            if (uptr -> HDSK_SECTOR_SIZE == 0)
                uptr -> HDSK_SECTOR_SIZE = 128;
        }
    }
    else {  /* Case 2: disk parameter block found														*/
        uptr -> HDSK_SECTORS_PER_TRACK  = dpb[uptr -> HDSK_FORMAT_TYPE].spt >> dpb[uptr -> HDSK_FORMAT_TYPE].psh;
        uptr -> HDSK_SECTOR_SIZE        = (128 << dpb[uptr -> HDSK_FORMAT_TYPE].psh);
    }
    assert((uptr -> HDSK_SECTORS_PER_TRACK) && (uptr -> HDSK_SECTOR_SIZE) && (uptr -> HDSK_FORMAT_TYPE >= 0));
    
    /* Step 4: Number of tracks is smallest number to accomodate capacity								*/
    uptr -> HDSK_NUMBER_OF_TRACKS = (uptr -> capac + uptr -> HDSK_SECTORS_PER_TRACK *
                                     uptr -> HDSK_SECTOR_SIZE - 1) / (uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE);
    assert( ( (t_addr) ((uptr -> HDSK_NUMBER_OF_TRACKS - 1) * uptr -> HDSK_SECTORS_PER_TRACK *
                        uptr -> HDSK_SECTOR_SIZE) < uptr -> capac) &&
           (uptr -> capac <= (t_addr) (uptr -> HDSK_NUMBER_OF_TRACKS *
                                       uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE) ) );
    
    return SCPE_OK;
}

static t_stat hdsk_detach(UNIT *uptr) {
    t_stat result;
    int32 unitIndex;
    if (uptr == NULL)
        return SCPE_IERR;
    if (is_imd(uptr)) {
        unitIndex = find_unit_index(uptr);
        if (unitIndex == -1)
            return SCPE_IERR;
        assert((0 <= unitIndex) && (unitIndex < HDSK_NUMBER));
        diskClose(&hdsk_imd[unitIndex]);
    }
    result = detach_unit(uptr);
    uptr -> capac = HDSK_CAPACITY;
    uptr -> HDSK_FORMAT_TYPE = 0;
    uptr -> HDSK_SECTOR_SIZE = 0;
    uptr -> HDSK_SECTORS_PER_TRACK = 0;
    uptr -> HDSK_NUMBER_OF_TRACKS = 0;
    return result;
}

/* Set disk geometry routine */
static t_stat set_geom(UNIT *uptr, int32 val, char *cptr, void *desc) {
    uint32 numberOfTracks, numberOfSectors, sectorSize;
    int result, n;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (((uptr -> flags) & UNIT_ATT) == 0) {
        printf("Cannot set geometry for not attached unit %i.\n", find_unit_index(uptr));
        return SCPE_ARG;
    }
    result = sscanf(cptr, "%d/%d/%d%n", &numberOfTracks, &numberOfSectors, &sectorSize, &n);
    if ((result != 3) || (result == EOF) || (cptr[n] != 0)) {
        result = sscanf(cptr, "T:%d/N:%d/S:%d%n", &numberOfTracks, &numberOfSectors, &sectorSize, &n);
        if ((result != 3) || (result == EOF) || (cptr[n] != 0))
            return SCPE_ARG;
    }
    uptr -> HDSK_NUMBER_OF_TRACKS     = numberOfTracks;
    uptr -> HDSK_SECTORS_PER_TRACK    = numberOfSectors;
    uptr -> HDSK_SECTOR_SIZE          = sectorSize;
    uptr -> capac                     = numberOfTracks * numberOfSectors * sectorSize;
    return SCPE_OK;
}

/* Show disk geometry routine */
static t_stat show_geom(FILE *st, UNIT *uptr, int32 val, void *desc) {
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "T:%d/N:%d/S:%d", uptr -> HDSK_NUMBER_OF_TRACKS,
        uptr -> HDSK_SECTORS_PER_TRACK, uptr -> HDSK_SECTOR_SIZE);
    return SCPE_OK;
}

#define QUOTE1(text) #text
#define QUOTE2(text) QUOTE1(text)
/* Set disk format routine */
static t_stat set_format(UNIT *uptr, int32 val, char *cptr, void *desc) {
    char fmtname[DPB_NAME_LENGTH + 1];
    int32 i;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (sscanf(cptr, "%" QUOTE2(DPB_NAME_LENGTH) "s", fmtname) == 0)
        return SCPE_ARG;
    if (((uptr -> flags) & UNIT_ATT) == 0) {
        printf("Cannot set format for not attached unit %i.\n", find_unit_index(uptr));
        return SCPE_ARG;
    }
    for (i = 0; dpb[i].capac != 0; i++) {
        if (strncmp(fmtname, dpb[i].name, strlen(fmtname)) == 0) {
            uptr -> HDSK_FORMAT_TYPE = i;
            uptr -> capac = dpb[i].capac; /* Set capacity */

            /* Configure physical disk geometry */
            uptr -> HDSK_SECTOR_SIZE          = (128 << dpb[uptr -> HDSK_FORMAT_TYPE].psh);
            uptr -> HDSK_SECTORS_PER_TRACK    = dpb[uptr -> HDSK_FORMAT_TYPE].spt >> dpb[uptr -> HDSK_FORMAT_TYPE].psh;
            uptr -> HDSK_NUMBER_OF_TRACKS     = (uptr -> capac +
                uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE - 1) /
                (uptr -> HDSK_SECTORS_PER_TRACK * uptr -> HDSK_SECTOR_SIZE);

            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

/* Show disk format routine */
static t_stat show_format(FILE *st, UNIT *uptr, int32 val, void *desc) {
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "%s", dpb[uptr -> HDSK_FORMAT_TYPE].name);
    return SCPE_OK;
}

static int32 bootrom_hdsk[BOOTROM_SIZE_HDSK] = {
    0xf3, 0x06, 0x80, 0x3e, 0x0e, 0xd3, 0xfe, 0x05, /* 5c00-5c07 */
    0xc2, 0x05, 0x5c, 0x3e, 0x16, 0xd3, 0xfe, 0x3e, /* 5c08-5c0f */
    0x12, 0xd3, 0xfe, 0xdb, 0xfe, 0xb7, 0xca, 0x20, /* 5c10-5c17 */
    0x5c, 0x3e, 0x0c, 0xd3, 0xfe, 0xaf, 0xd3, 0xfe, /* 5c18-5c1f */
    0x06, 0x20, 0x3e, 0x01, 0xd3, 0xfd, 0x05, 0xc2, /* 5c20-5c27 */
    0x24, 0x5c, 0x11, 0x08, 0x00, 0x21, 0x00, 0x00, /* 5c28-5c2f */
    0x0e, 0xb8, 0x3e, 0x02, 0xd3, 0xfd, 0x3a, 0x37, /* 5c30-5c37 */
    0xff, 0xd6, 0x08, 0xd3, 0xfd, 0x7b, 0xd3, 0xfd, /* 5c38-5c3f */
    0x7a, 0xd3, 0xfd, 0xaf, 0xd3, 0xfd, 0x7d, 0xd3, /* 5c40-5c47 */
    0xfd, 0x7c, 0xd3, 0xfd, 0xdb, 0xfd, 0xb7, 0xca, /* 5c48-5c4f */
    0x53, 0x5c, 0x76, 0x79, 0x0e, 0x80, 0x09, 0x4f, /* 5c50-5c57 */
    0x0d, 0xc2, 0x60, 0x5c, 0xfb, 0xc3, 0x00, 0x00, /* 5c58-5c5f */
    0x1c, 0x1c, 0x7b, 0xfe, 0x20, 0xca, 0x73, 0x5c, /* 5c60-5c67 */
    0xfe, 0x21, 0xc2, 0x32, 0x5c, 0x1e, 0x00, 0x14, /* 5c68-5c6f */
    0xc3, 0x32, 0x5c, 0x1e, 0x01, 0xc3, 0x32, 0x5c, /* 5c70-5c77 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5c78-5c7f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5c80-5c87 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5c88-5c8f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5c90-5c97 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5c98-5c9f */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5ca0-5ca7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5ca8-5caf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cb0-5cb7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cb8-5cbf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cc0-5cc7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cc8-5ccf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cd0-5cd7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cd8-5cdf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5ce0-5ce7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5ce8-5cef */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cf0-5cf7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 5cf8-5cff */
};

static t_stat hdsk_boot(int32 unitno, DEVICE *dptr) {
    t_bool installSuccessful;
    if (MEMORYSIZE < 24*KB) {
        printf("Need at least 24KB RAM to boot from hard disk.\n");
        return SCPE_ARG;
    }
    if (cpu_unit.flags & (UNIT_CPU_ALTAIRROM | UNIT_CPU_BANKED)) {
        /* check whether we are really modifying an LD A,<> instruction */
        if (bootrom_dsk[UNIT_NO_OFFSET_1 - 1] == LDA_INSTRUCTION)
            bootrom_dsk[UNIT_NO_OFFSET_1] = (unitno + NUM_OF_DSK) & 0xff;   /* LD A,<unitno> */
        else { /* Attempt to modify non LD A,<> instructions is refused. */
            printf("Incorrect boot ROM offset detected.\n");
            return SCPE_IERR;
        }
        install_ALTAIRbootROM();                                            /* install modified ROM */
    }
    installSuccessful = (install_bootrom(bootrom_hdsk, BOOTROM_SIZE_HDSK, HDSK_BOOT_ADDRESS,
                                         FALSE) == SCPE_OK);
    assert(installSuccessful);
    *((int32 *) sim_PC -> loc) = HDSK_BOOT_ADDRESS;
    return SCPE_OK;
}

/*  The hard disk port is 0xfd. It understands the following commands.

    1.  Reset
        ld  b,32
        ld  a,HDSK_RESET
    l:  out (0fdh),a
        dec b
        jp  nz,l

    2.  Read / write
        ; parameter block
        cmd:        db  HDSK_READ or HDSK_WRITE
        hd:         db  0   ; 0 .. 7, defines hard disk to be used
        sector:     db  0   ; 0 .. 31, defines sector
        track:      dw  0   ; 0 .. 2047, defines track
        dma:        dw  0   ; defines where result is placed in memory

        ; routine to execute
        ld  b,7             ; size of parameter block
        ld  hl,cmd          ; start address of parameter block
    l:  ld  a,(hl)          ; get byte of parameter block
        out (0fdh),a        ; send it to port
        inc hl              ; point to next byte
        dec b               ; decrement counter
        jp  nz,l            ; again, if not done
        in  a,(0fdh)        ; get result code

    3.  Retrieve Disk Parameters from controller (Howard M. Harte)
        Reads a 19-byte parameter block from the disk controller.
        This parameter block is in CP/M DPB format for the first 17 bytes,
        and the last two bytes are the lsb/msb of the disk's physical
        sector size.

        ; routine to execute
        ld   a,hdskParam    ; hdskParam = 4
        out  (hdskPort),a   ; Send 'get parameters' command, hdskPort = 0fdh
        ld   a,(diskno)
        out  (hdskPort),a   ; Send selected HDSK number
        ld   b,17
    1:  in   a,(hdskPort)   ; Read 17-bytes of DPB
        ld   (hl), a
        inc  hl
        djnz 1
        in   a,(hdskPort)   ; Read LSB of disk's physical sector size.
        ld   (hsecsiz), a
        in   a,(hdskPort)   ; Read MSB of disk's physical sector size.
        ld   (hsecsiz+1), a

*/

/* check the parameters and return TRUE iff parameters are correct or have been repaired */
static int32 checkParameters(void) {
    UNIT *uptr;
    if ((selectedDisk < 0) || (selectedDisk >= HDSK_NUMBER)) {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Disk %i does not exist, will use HDSK0 instead.\n",
                  selectedDisk, PCX, selectedDisk);
        selectedDisk = 0;
    }
    uptr = &hdsk_dev.units[selectedDisk];
    if ((hdsk_dev.units[selectedDisk].flags & UNIT_ATT) == 0) {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Disk %i is not attached.\n", selectedDisk, PCX, selectedDisk);
        return FALSE; /* cannot read or write */
    }
    if ((selectedSector < 0) || (selectedSector >= uptr -> HDSK_SECTORS_PER_TRACK)) {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Constraint violation 0 <= Sector=%02d < %d, will use sector 0 instead.\n",
                  selectedDisk, PCX, selectedSector, uptr -> HDSK_SECTORS_PER_TRACK);
        selectedSector = 0;
    }
    if ((selectedTrack < 0) || (selectedTrack >= uptr -> HDSK_NUMBER_OF_TRACKS)) {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Constraint violation 0 <= Track=%04d < %04d, will use track 0 instead.\n",
                  selectedDisk, PCX, selectedTrack, uptr -> HDSK_NUMBER_OF_TRACKS);
        selectedTrack = 0;
    }
    selectedDMA &= ADDRMASK;
    if (hdskLastCommand == HDSK_READ) {
        sim_debug(READ_MSG, &hdsk_dev, "HDSK%d " ADDRESS_FORMAT
                  " Read Track=%04d Sector=%02d Len=%04d DMA=%04x\n",
               selectedDisk, PCX, selectedTrack, selectedSector, uptr -> HDSK_SECTOR_SIZE, selectedDMA);
    }
    if (hdskLastCommand == HDSK_WRITE) {
        sim_debug(WRITE_MSG, &hdsk_dev, "HDSK%d " ADDRESS_FORMAT
                  " Write Track=%04d Sector=%02d Len=%04d DMA=%04x\n",
               selectedDisk, PCX, selectedTrack, selectedSector, uptr -> HDSK_SECTOR_SIZE, selectedDMA);
    }
    return TRUE;
}

/* pre-condition: checkParameters has been executed to repair any faulty parameters */
static int32 doSeek(void) {
    UNIT *uptr = &hdsk_dev.units[selectedDisk];
    int32 hostSector = (dpb[uptr -> HDSK_FORMAT_TYPE].skew == NULL) ?
        selectedSector : dpb[uptr -> HDSK_FORMAT_TYPE].skew[selectedSector];
    int32 sectorSize = (dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize == 0) ?
        uptr -> HDSK_SECTOR_SIZE : dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize;
    if (sim_fseek(uptr -> fileref,
        sectorSize * (uptr -> HDSK_SECTORS_PER_TRACK * selectedTrack + hostSector) +
              dpb[uptr -> HDSK_FORMAT_TYPE].offset, SEEK_SET)) {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Could not access Sector=%02d[=%02d] Track=%04d.\n",
                  selectedDisk, PCX, selectedSector, hostSector, selectedTrack);
        return CPM_ERROR;
    }
    return CPM_OK;
}

static uint8 hdskbuf[HDSK_MAX_SECTOR_SIZE] = { 0 };    /* data buffer  */

/* pre-condition: checkParameters has been executed to repair any faulty parameters */
static int32 doRead(void) {
    int32 i;
    t_stat result;
    DISK_INFO *thisDisk;
    int32 hostSector;
    int32 sectorSize;
    uint32 flags;
    uint32 readlen;
    uint32 cylinder;
    uint32 head;
    UNIT *uptr = &hdsk_dev.units[selectedDisk];
    if (is_imd(uptr)) {
        thisDisk = hdsk_imd[selectedDisk];
        hostSector = ((dpb[uptr -> HDSK_FORMAT_TYPE].skew == NULL) ?
                      selectedSector : dpb[uptr -> HDSK_FORMAT_TYPE].skew[selectedSector]) + thisDisk -> track[1][0].start_sector;
        sectorSize = ((dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize == 0) ?
                      uptr -> HDSK_SECTOR_SIZE :
                      dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize);
        flags = 0;
        readlen = 0;
        cylinder = selectedTrack;
        head = 0;
        if (cylinder >= thisDisk -> ntracks / thisDisk -> nsides) {
            head = 1;
            cylinder -= thisDisk -> ntracks / thisDisk -> nsides;
        }
        result = sectRead(thisDisk, cylinder, head, hostSector, hdskbuf, sectorSize,
                          &flags, &readlen);
        if (result != SCPE_OK) {
            for (i = 0; i < uptr -> HDSK_SECTOR_SIZE; i++)
                hdskbuf[i] = CPM_EMPTY;
            sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d (IMD): " ADDRESS_FORMAT
                      " . Could not read Sector=%02d Track=%04d.\n",
                      selectedDisk, PCX, selectedSector, selectedTrack);
            return CPM_ERROR;
        }
    } else {
        if (doSeek())
            return CPM_ERROR;
        
        if (sim_fread(hdskbuf, 1, uptr -> HDSK_SECTOR_SIZE, uptr -> fileref) != (size_t)(uptr -> HDSK_SECTOR_SIZE)) {
            for (i = 0; i < uptr -> HDSK_SECTOR_SIZE; i++)
                hdskbuf[i] = CPM_EMPTY;
            sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                      " Could not read Sector=%02d Track=%04d.\n",
                      selectedDisk, PCX, selectedSector, selectedTrack);
            return CPM_OK; /* allows the creation of empty hard disks */
        }
    }
    for (i = 0; i < uptr -> HDSK_SECTOR_SIZE; i++)
        PutBYTEWrapper(selectedDMA + i, hdskbuf[i]);
    return CPM_OK;
}

/* pre-condition: checkParameters has been executed to repair any faulty parameters */
static int32 doWrite(void) {
    int32 i;
    t_stat result;
    DISK_INFO *thisDisk;
    int32 hostSector;
    int32 sectorSize;
    uint32 flags;
    uint32 writelen;
    uint32 cylinder;
    uint32 head;
    size_t rtn;
    UNIT *uptr = &hdsk_dev.units[selectedDisk];
    if (((uptr -> flags) & UNIT_HDSK_WLK) == 0) { /* write enabled */
        if (is_imd(uptr)) {
            for (i = 0; i < uptr -> HDSK_SECTOR_SIZE; i++)
                hdskbuf[i] = GetBYTEWrapper(selectedDMA + i);
            thisDisk = hdsk_imd[selectedDisk];
            hostSector = ((dpb[uptr -> HDSK_FORMAT_TYPE].skew == NULL) ?
                          selectedSector : dpb[uptr -> HDSK_FORMAT_TYPE].skew[selectedSector]) + thisDisk -> track[1][0].start_sector;
            sectorSize = ((dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize == 0) ?
                          uptr -> HDSK_SECTOR_SIZE :
                          dpb[uptr -> HDSK_FORMAT_TYPE].physicalSectorSize);
            flags = 0;
            writelen = 0;
            cylinder = selectedTrack;
            head = 0;
            if (cylinder >= thisDisk -> ntracks / thisDisk -> nsides) {
                head = 1;
                cylinder -= thisDisk -> ntracks / thisDisk -> nsides;
            }
            result = sectWrite(thisDisk, cylinder, head, hostSector, hdskbuf,
                               sectorSize, &flags, &writelen);
            if (result != SCPE_OK) {
                sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d (IMD): " ADDRESS_FORMAT
                          " . Could not write Sector=%02d Track=%04d.\n",
                          selectedDisk, PCX, selectedSector, selectedTrack);
                return CPM_ERROR;
            }
        } else {
            if (doSeek())
                return CPM_ERROR;
            for (i = 0; i < uptr -> HDSK_SECTOR_SIZE; i++)
                hdskbuf[i] = GetBYTEWrapper(selectedDMA + i);
            rtn = sim_fwrite(hdskbuf, 1, uptr -> HDSK_SECTOR_SIZE, uptr -> fileref);
            if (rtn != (size_t)(uptr -> HDSK_SECTOR_SIZE)) {
                sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                          " Could not write Sector=%02d Track=%04d Result=%zd.\n",
                          selectedDisk, PCX, selectedSector, selectedTrack, rtn);
                return CPM_ERROR;
            }
        }
    }
    else {
        sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                  " Could not write to locked disk Sector=%02d Track=%04d.\n",
                  selectedDisk, PCX, selectedSector, selectedTrack);
        return CPM_ERROR;
    }
    return CPM_OK;
}

#define PARAMETER_BLOCK_SIZE    19
static uint8 parameterBlock[PARAMETER_BLOCK_SIZE];

static int32 hdsk_in(const int32 port) {
    if ((hdskCommandPosition == 6) && ((hdskLastCommand == HDSK_READ) || (hdskLastCommand == HDSK_WRITE))) {
        int32 result = checkParameters() ? ((hdskLastCommand == HDSK_READ) ? doRead() : doWrite()) : CPM_ERROR;
        hdskLastCommand = HDSK_NONE;
        hdskCommandPosition = 0;
        return result;
    }
    if (hdskLastCommand == HDSK_PARAM) {
        if (++parameterCount >= PARAMETER_BLOCK_SIZE)
            hdskLastCommand = HDSK_NONE;
        return parameterBlock[parameterCount - 1];
    }
    sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
              " Illegal IN command detected (port=%03xh, cmd=%d, pos=%d).\n",
              selectedDisk, PCX, port, hdskLastCommand, hdskCommandPosition);
    return CPM_OK;
}

static int32 hdsk_out(const int32 port, const int32 data) {
    int32 thisDisk;
    UNIT *uptr;
    DPB current;

    switch(hdskLastCommand) {

        case HDSK_PARAM:
            parameterCount = 0;
            thisDisk = (0 <= data) && (data < HDSK_NUMBER) ? data : 0;
            uptr = &hdsk_dev.units[thisDisk];
            if ((uptr -> flags) & UNIT_ATT) {
                current = dpb[uptr -> HDSK_FORMAT_TYPE];
                parameterBlock[17] = uptr -> HDSK_SECTOR_SIZE & 0xff;
                parameterBlock[18] = (uptr -> HDSK_SECTOR_SIZE >> 8) & 0xff;
            }
            else {
                current = dpb[0];
                parameterBlock[17] = 128;
                parameterBlock[18] = 0;
            }
            parameterBlock[ 0] = current.spt & 0xff; parameterBlock[ 1] = (current.spt >> 8) & 0xff;
            parameterBlock[ 2] = current.bsh;
            parameterBlock[ 3] = current.blm;
            parameterBlock[ 4] = current.exm;
            parameterBlock[ 5] = current.dsm & 0xff; parameterBlock[ 6] = (current.dsm >> 8) & 0xff;
            parameterBlock[ 7] = current.drm & 0xff; parameterBlock[ 8] = (current.drm >> 8) & 0xff;
            parameterBlock[ 9] = current.al0;
            parameterBlock[10] = current.al1;
            parameterBlock[11] = current.cks & 0xff; parameterBlock[12] = (current.cks >> 8) & 0xff;
            parameterBlock[13] = current.off & 0xff; parameterBlock[14] = (current.off >> 8) & 0xff;
            parameterBlock[15] = current.psh;
            parameterBlock[16] = current.phm;
            break;

        case HDSK_READ:

        case HDSK_WRITE:
            switch(hdskCommandPosition) {

                case 0:
                    selectedDisk = data;
                    hdskCommandPosition++;
                    break;

                case 1:
                    selectedSector = data;
                    hdskCommandPosition++;
                    break;

                case 2:
                    selectedTrack = data;
                    hdskCommandPosition++;
                    break;

                case 3:
                    selectedTrack += (data << 8);
                    hdskCommandPosition++;
                    break;

                case 4:
                    selectedDMA = data;
                    hdskCommandPosition++;
                    break;

                case 5:
                    selectedDMA += (data << 8);
                    hdskCommandPosition++;
                    break;

                default:
                    hdskLastCommand = HDSK_NONE;
                    hdskCommandPosition = 0;
            }
            break;

        default:
            if ((HDSK_RESET <= data) && (data <= HDSK_PARAM))
                 hdskLastCommand = data;
            else {
                sim_debug(VERBOSE_MSG, &hdsk_dev, "HDSK%d: " ADDRESS_FORMAT
                          " Illegal OUT command detected (port=%03xh, cmd=%d).\n",
                          selectedDisk, PCX, port, data);
                hdskLastCommand = HDSK_RESET;
            }
            hdskCommandPosition = 0;
    }
    return 0; /* ignored, since OUT */
}

int32 hdsk_io(const int32 port, const int32 io, const int32 data) {
    return io == 0 ? hdsk_in(port) : hdsk_out(port, data);
}
