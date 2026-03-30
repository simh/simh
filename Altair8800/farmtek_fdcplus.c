/* farmtek_fdcplus.c: FarmTek FDC+ Simulator

   Copyright (c) 2026 Patrick A. Linstruth

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

   Based on work by Charles E Owen (c) 1997
   Based on work by Peter Schorn (c) 2002-2023

   History:
   29-Mar-2026 Initial version

   ==================================================================
  
   The Altair FDC+ is an enhanced version of the original MITS 8"
   floppy disk controller for the Altair 8800. The FDC+ is a 100%
   compatible drop-in replacement for the original two-board
   Altair FDC.

   This device supports the following FDC+ drive types:

   5 - 5.25" drive as an Altair 8" drive or as 1.5Mb drive
   7 - Serial Drive as Altair 8" Drive (coming soon)

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "altair8800_dsk.h"
#include "s100_bus.h"
#include "s100_cpu.h"
#include "farmtek_fdcplus.h"

#define DEVICE_NAME    "FarmTek FDC+ Floppy Disk Controller"
#define DEVICE_DEV     "FDCP"

/* Debug flags */
#define IN_MSG              (1 << 0)
#define OUT_MSG             (1 << 1)
#define READ_MSG            (1 << 2)
#define WRITE_MSG           (1 << 3)
#define SECTOR_STUCK_MSG    (1 << 4)
#define TRACK_STUCK_MSG     (1 << 5)
#define VERBOSE_MSG         (1 << 6)

static int32 poc = TRUE;

static int32 fdcp_08h(const int32 port, const int32 io, const int32 data);
static int32 fdcp_09h(const int32 port, const int32 io, const int32 data);
static int32 fdcp_0ah(const int32 port, const int32 io, const int32 data);
static int32 fdcp_0bh(const int32 port, const int32 io, const int32 data);

static t_stat fdcp_boot(int32 unitno, DEVICE *dptr);
static t_stat fdcp_reset(DEVICE *dptr);
static t_stat fdcp_attach(UNIT *uptr, const char *cptr);
static t_stat fdcp_detach(UNIT *uptr);
static t_stat fdcp_set_type(UNIT *uptr, int32 value, const char *cptr, void *desc);
static t_stat fdcpd_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char* fdcp_description(DEVICE *dptr);

/* global data on status */

/* currently selected drive (values are 0 .. NUM_OF_DSK)
   current_disk < NUM_OF_DSK implies that the corresponding disk is attached to a file */
static int32 current_disk      = NUM_OF_DSK;

static int32 current_track     [NUM_OF_DSK];
static int32 current_sector    [NUM_OF_DSK];
static int32 current_byte      [NUM_OF_DSK];
static int32 current_flag      [NUM_OF_DSK];
static int32 read_enable       [NUM_OF_DSK]; /* For 1.5 MB Drive Type 5 */
static int32 dummy_read        [NUM_OF_DSK]; /* For 1.5 MB Drive Type 5 */
static int32 sectors_per_track [NUM_OF_DSK];
static int32 sector_size       [NUM_OF_DSK];
static int32 tracks            [NUM_OF_DSK];
static int32 in9_count         = 0;
static int32 in9_message       = FALSE;
static int32 dirty             = FALSE;      /* TRUE when buffer has unwritten data in it    */
static uint8 dskbuf[FDCP15_DISK_SECTSIZE];   /* data Buffer                                  */
static int32 sector_true       = 0;          /* sector true flag for sector register read    */

static int32 drive_type        = 0;
static int32 logical_track     = 0;          /* For 1.5 MB Drive Type 5                      */
static int32 logical_sector    = 0;          /* For 1.5 MB Drive Type 5                      */

/****************************************************************************************
*
*  FDC+
*
*****************************************************************************************/

#define FDCP_SELECT     0x08 /* Out */
#define FDCP_DRVSTAT    0x08 /* In  */
#define FDCP_CONTROL    0x09 /* Out */
#define FDCP_SECTOR     0x09 /* In  */
#define FDCP_WDATA      0x0a /* Out */
#define FDCP_RDATA      0x0a /* In  */
#define FDCP_IOSTAT     0x0b /* Out */
#define FDCP_TRACK      0x0b /* In  */

#define FDCP_CLEAR      0x80 /* Clear disk select */

#define FDCP_ENWD       0x01 /* Enter new write data */
#define FDCP_MVHD       0x02 /* Move head            */
#define FDCP_HS         0x04 /* Head status          */
#define FDCP_RDY        0x08 /* Ready                */
#define FDCP_WP         0x10 /* Write protected      */
#define FDCP_INTE       0x20 /* Interrupt enabled    */
#define FDCP_TRK0       0x40 /* Track 0              */
#define FDCP_NRDA       0x80 /* New read data avail. */

#define FDCP_STEPIN     0x01 /* Step in              */
#define FDCP_STEPOUT    0x02 /* Step out             */
#define FDCP_HDLD       0x04 /* Head load            */
#define FDCP_HDUL       0x08 /* Head unload          */
#define FDCP_IE         0x10 /* Interrupt enable     */
#define FDCP_RE         0x10 /* Read enable (Type 5) */
#define FDCP_ID         0x20 /* Interrupt disable    */
#define FDCP_HCS        0x40 /* Head current switch  */
#define FDCP_WE         0x80 /* Write enable         */

/****************************************************************************************
*
*  bootSector - HD Floppy boot code in an original Altair 137 byte sector
*     The disk format and data interchange between the 8080 and the FDC+ is completely
*     different for the HD floppy drive option than for standard Altair drives. This
*     means a different boot PROM is required to boot the loader found at the start of
*     track zero on an HD floppy drive. As a way to get around this requirement, the
*     main sector loop  returns the standard Altair 137 byte sector below whenever
*     the index pulse is reached (i.e., new sector) AND an HD floppy read is not in
*     progress. This means an Altair boot PROM will receive this sector and jump
*     to it as if it came from a standard Altair disk.
*
*****************************************************************************************/
static uint8 bootSector[] = {
    0x80,0x80,0x00,0xf3,0x31,0x00,0x3f,0x3e,
    0x80,0xd3,0x08,0xaf,0xd3,0x08,0xd3,0x0b,
    0x3e,0x04,0xd3,0x09,0xdb,0x08,0xe6,0x02,
    0xc2,0x11,0x00,0xdb,0x08,0xe6,0x40,0xca,
    0x26,0x00,0x3e,0x02,0xd3,0x09,0xc3,0x11,
    0x00,0xdb,0x09,0x1f,0xda,0x26,0x00,0x21,
    0x00,0x40,0x01,0x00,0x14,0x3e,0x10,0xd3,
    0x09,0xdb,0x08,0xb7,0xfa,0x36,0x00,0xdb,
    0x0a,0xdb,0x0a,0x77,0x23,0xdb,0x0a,0x77,
    0x23,0x0b,0x78,0xb1,0xc2,0x3e,0x00,0xdb,
    0x0b,0x32,0xff,0x3f,0xe6,0x7f,0xc2,0x00,
    0x00,0x3e,0x80,0xd3,0x08,0xc3,0x00,0x40,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xff,0xa6,0x00,0x00,0x00,
    0x00,0x00
};

/* 88DSK Standard I/O Data Structures */

static UNIT fdcp_unit[NUM_OF_DSK] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) }
};

static REG fdcp_reg[] = {
    { DRDATAD (TYPE,         drive_type,        4,
               "Selected drive type"),                                                      },
    { DRDATAD (DISK,         current_disk,      4,
               "Selected disk register"),                                                   },
    { BRDATAD (CURTRACK,     current_track,     10, 32, NUM_OF_DSK,
               "Selected track register array"), REG_CIRC + REG_RO                          },
    { BRDATAD (CURSECTOR,    current_sector,    10, 32, NUM_OF_DSK,
               "Selected sector register array"), REG_CIRC + REG_RO                         },
    { BRDATAD (CURBYTE,      current_byte,      10, 32, NUM_OF_DSK,
               "Current byte register array"), REG_CIRC + REG_RO                            },
    { BRDATAD (CURFLAG,      current_flag,      10, 32, NUM_OF_DSK,
               "Current flag register array"), REG_CIRC + REG_RO                            },
    { BRDATAD (TRACKS,      tracks,             10, 32,  NUM_OF_DSK,
               "Number of tracks register array"), REG_CIRC                                 },
    { BRDATAD (SECTPERTRACK, sectors_per_track, 10, 32,  NUM_OF_DSK,
               "Number of sectors per track register array"), REG_CIRC                      },
    { BRDATAD (SECTSIZE,     sector_size,       10, 32,  NUM_OF_DSK,
               "Sector size register array"), REG_CIRC                                      },
    { DRDATAD (IN9COUNT,     in9_count,         4,
               "Count of IN(9) register"),  REG_RO                                          },
    { DRDATAD (IN9MESSAGE,   in9_message,       4,
               "BOOL for IN(9) message register"), REG_RO                                   },
    { DRDATAD (DIRTY,        dirty,             4,
               "BOOL for write needed register"), REG_RO                                    },
    { BRDATAD (DISKBUFFER,   dskbuf,           10, 8,  DSK_SECTSIZE,
               "Disk data buffer array"), REG_CIRC + REG_RO                                 },
    { NULL }
};

static const char* fdcp_description(DEVICE *dptr) {
    return DEVICE_NAME;
}

static MTAB fdcp_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_VALO, 0, NULL, "TYPE={5,7}",  &fdcp_set_type,  NULL, NULL, "FDC+ Drive Type" },
    { UNIT_DSK_WLK,     0,                  "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " DEVICE_DEV "n for writing" },
    { UNIT_DSK_WLK,     UNIT_DSK_WLK,       "WRTLCK",    "WRTLCK",  NULL, NULL, NULL,
        "Locks " DEVICE_DEV "n for writing" },
    { 0 }
};

/* Debug Flags */
static DEBTAB fdcp_dt[] = {
    { "IN",             IN_MSG,             "IN operations"     },
    { "OUT",            OUT_MSG,            "OUT operations"    },
    { "READ",           READ_MSG,           "Read operations"   },
    { "WRITE",          WRITE_MSG,          "Write operations"  },
    { "SECTOR_STUCK",   SECTOR_STUCK_MSG,   "Sector stuck"      },
    { "TRACK_STUCK",    TRACK_STUCK_MSG,    "Track stuck"       },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

DEVICE fdcp_dev = {
    DEVICE_DEV, fdcp_unit, fdcp_reg, fdcp_mod,
    NUM_OF_DSK, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &fdcp_reset,
    &fdcp_boot, &fdcp_attach, &fdcp_detach,
    NULL, (DEV_DISABLE | DEV_DIS | DEV_DEBUG), 0,
    fdcp_dt, NULL, NULL, &fdcpd_show_help, &dsk_attach_help, NULL,
    &fdcp_description
};

static const char* selectInOut(const int32 io) {
    return io == S100_IO_READ ? "IN" : "OUT";
}

/* service routines to handle simulator functions */
/* reset routine */

static t_stat fdcp_reset(DEVICE *dptr)
{
    int32 i;

    if (dptr->flags & DEV_DIS) {
        s100_bus_remio(0x08, 1, &fdcp_08h);
        s100_bus_remio(0x09, 1, &fdcp_09h);
        s100_bus_remio(0x0A, 1, &fdcp_0ah);
        s100_bus_remio(0x0B, 1, &fdcp_0bh);

        poc = TRUE;
    }
    else {
        if (poc) { /* Powerup? */
            s100_bus_addio(0x08, 1, &fdcp_08h, dptr->name);
            s100_bus_addio(0x09, 1, &fdcp_09h, dptr->name);
            s100_bus_addio(0x0A, 1, &fdcp_0ah, dptr->name);
            s100_bus_addio(0x0B, 1, &fdcp_0bh, dptr->name);

            drive_type = 5;

            for (i = 0; i < NUM_OF_DSK; i++) {
                sectors_per_track[i] = DSK_SECT;
                sector_size[i] = DSK_SECTSIZE;
                tracks[i] = MAX_TRACKS;
            }

            poc = FALSE;
        }

        for (i = 0; i < NUM_OF_DSK; i++) {
            current_track[i] = 0;
            current_sector[i] = 0;
            current_byte[i] = 0;
            current_flag[i] = 0;
            read_enable[i] = 0;
            dummy_read[i] = 0;
        }

        current_disk = NUM_OF_DSK;
        in9_count = 0;
        in9_message = FALSE;
    }

    return SCPE_OK;
}
/* fdcp_attach - determine type of drive attached based on disk image size */

static t_stat fdcp_attach(UNIT *uptr, const char *cptr)
{
    int32 thisUnitIndex;
    t_stat r;

    r = attach_unit(uptr, cptr);      /* attach unit     */

    if (r != SCPE_OK) {               /* error?          */
        return r;
    }

    ASSURE(uptr != NULL);
    thisUnitIndex = sys_find_unit_index(uptr);
    ASSURE((0 <= thisUnitIndex) && (thisUnitIndex < NUM_OF_DSK));

    uptr->capac = sim_fsize(uptr -> fileref);

    drive_type = 5;
    sectors_per_track[thisUnitIndex] = FDCP15_DISK_SECT;
    sector_size[thisUnitIndex] = FDCP15_DISK_SECTSIZE;

    sim_printf("DSK%i: " ADDRESS_FORMAT
        "Drive type: %i SPT:%i SS:%i\n", thisUnitIndex, s100_bus_get_addr(),
        drive_type, sectors_per_track[thisUnitIndex], sector_size[thisUnitIndex]);

    sim_debug(VERBOSE_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT
        "Drive type: %i SPT:%i SS:%i\n", thisUnitIndex, s100_bus_get_addr(),
        drive_type, sectors_per_track[thisUnitIndex], sector_size[thisUnitIndex]);

    return SCPE_OK;
}

static t_stat fdcp_detach(UNIT *uptr)
{
    return detach_unit(uptr);
}

static t_stat fdcp_boot(int32 unitno, DEVICE *dptr)
{
    cpu_set_pc_loc(0xff00);

    return SCPE_OK;
}

static int32 dskseek(const UNIT *xptr)
{
    return sim_fseek(xptr -> fileref, sector_size[current_disk] * sectors_per_track[current_disk] * 
        ((drive_type == 5) ? logical_track : current_track[current_disk]) +
        (sector_size[current_disk] * current_sector[current_disk]), SEEK_SET);
}

/* precondition: current_disk < NUM_OF_DSK */
static void writebuf(void)
{
    int32 i, rtn;
    UNIT *uptr;

    i = current_byte[current_disk];         /* null-fill rest of sector if any */

    while (i < sector_size[current_disk]) {
        dskbuf[i++] = 0;
    }

    uptr = fdcp_dev.units + current_disk;

    if (((uptr -> flags) & UNIT_DSK_WLK) == 0) { /* write enabled */
        sim_debug(WRITE_MSG, &fdcp_dev,
                "DSK%i: " ADDRESS_FORMAT " OUT 0x0a (WRITE) D%d T%d S%d\n",
                current_disk, s100_bus_get_addr(), current_disk,
                current_track[current_disk], current_sector[current_disk]);

        if (dskseek(uptr)) {
            sim_debug(VERBOSE_MSG, &fdcp_dev,
                "DSK%i: " ADDRESS_FORMAT " fseek failed D%d T%d S%d\n",
                current_disk, s100_bus_get_addr(), current_disk,
                current_track[current_disk], current_sector[current_disk]);
        }

        rtn = sim_fwrite(dskbuf, 1, sector_size[current_disk], uptr -> fileref);

        if (rtn != sector_size[current_disk]) {
            sim_debug(VERBOSE_MSG, &fdcp_dev,
                "DSK%i: " ADDRESS_FORMAT " sim_fwrite failed T%d S%d Return=%d\n",
                current_disk, s100_bus_get_addr(), current_track[current_disk],
                current_sector[current_disk], rtn);
        }
    }
    else {
        sim_debug(VERBOSE_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT " Attempt to write to locked DSK%d - ignored.\n",
                  current_disk, s100_bus_get_addr(), current_disk);
    }

    current_flag[current_disk] &= ~FDCP_ENWD;    /* ENWD off */
    current_byte[current_disk] = MAX_SECT_SIZE;
    dirty = FALSE;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port. On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/

/* Disk Controller Status/Select */

/*  IMPORTANT: The status flags read by port 8 IN instruction are
    INVERTED, that is, 0 is true and 1 is false. To handle this, the
    simulator keeps it's own status flags as 0=false, 1=true; and
    returns the COMPLEMENT of the status flags when read. This makes
    setting/testing of the flag bits more logical, yet meets the
    simulation requirement that they are reversed in hardware.
*/

static int32 fdcp_08h(const int32 port, const int32 io, const int32 data)
{
    int32 current_disk_flags;

    in9_count = 0;

    if (io == S100_IO_READ) {                                      /* IN: return flags */
        if (current_disk >= NUM_OF_DSK) {
            sim_debug(VERBOSE_MSG, &fdcp_dev,
                "DSK%i: " ADDRESS_FORMAT " Attempt of IN DRVSTAT on unattached disk - ignored.\n",
                current_disk, s100_bus_get_addr());

            return 0xff;                                /* no drive selected - can do nothing */
        }

        return (~current_flag[current_disk]) & 0xff;    /* return the COMPLEMENT! */
    }

    /* OUT: Controller set/reset/enable/disable */
    if (dirty) {   /* implies that current_disk < NUM_OF_DSK */
        writebuf();
    }

    sim_debug(OUT_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT " OUT SELECT: %x\n", current_disk, s100_bus_get_addr(), data);

    current_disk = data & NUM_OF_DSK_MASK; /* 0 <= current_disk < NUM_OF_DSK */
    current_disk_flags = (fdcp_dev.units + current_disk)->flags;

    if ((current_disk_flags & UNIT_ATT) == 0) { /* nothing attached? */
        sim_debug(VERBOSE_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT
            " Attempt to select unattached DSK%d - ignored.\n",
            current_disk, s100_bus_get_addr(), current_disk);

        current_disk = NUM_OF_DSK;
    } else {
        current_sector[current_disk]    = 0xff;     /* reset internal counters */
        current_byte[current_disk]      = MAX_SECT_SIZE;

        if (data & FDCP_CLEAR) {                    /* disable drive? */
            current_flag[current_disk] = 0;         /* yes, clear all flags */
            read_enable[current_disk] = 0;
            dummy_read[current_disk] = 0;

            sim_debug(READ_MSG, &fdcp_dev,
                "DSK%i: " ADDRESS_FORMAT " Read Disable (CLEAR)\n",
                current_disk, s100_bus_get_addr());
        }
        else {                                      /* enable drive */
            current_flag[current_disk] = 0x0a;      /* move head true */

            if (current_track[current_disk] == 0) { /* track 0? */
                current_flag[current_disk] |= FDCP_TRK0; /* yes, set track 0 true as well */
            }
        }
    }

   return 0xff;   /* ignored since OUT */
}

/* Disk Drive Status/Functions */

static int32 fdcp_09h(const int32 port, const int32 io, const int32 data)
{
    if (current_disk >= NUM_OF_DSK) {
        sim_debug(VERBOSE_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT
                  " Attempt of %s 0x09 on unattached disk - ignored.\n",
                  current_disk, s100_bus_get_addr(), selectInOut(io));

        return 0xff;    /* no drive selected - can do nothing */
    }

    /* now current_disk < NUM_OF_DSK */
    if (io == S100_IO_READ) {  /* read sector position */
        in9_count++;

        if ((fdcp_dev.dctrl & SECTOR_STUCK_MSG) && (in9_count > 2 * DSK_SECT) && (!in9_message)) {
            in9_message = TRUE;
            sim_debug(SECTOR_STUCK_MSG, &fdcp_dev,
                      "DSK%i: " ADDRESS_FORMAT " Looping on sector find.\n",
                      current_disk, s100_bus_get_addr());
        }

        sim_debug(IN_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT " IN 0x09\n", current_disk, s100_bus_get_addr());

        if (dirty) { /* implies that current_disk < NUM_OF_DSK */
            writebuf();
        }

        if (current_flag[current_disk] & FDCP_HS) { /* head loaded? */
            sector_true ^= 1;                       /* return sector true every other entry */
            if (sector_true == 0) {                 /* true when zero */
                current_sector[current_disk]++;
                if (current_sector[current_disk] >= (sectors_per_track[current_disk] < MINI_DISK_SECT) ? DSK_SECT : sectors_per_track[current_disk]) {
                    current_sector[current_disk] = 0;
                }
                current_byte[current_disk] = MAX_SECT_SIZE;
            }
            return (((current_sector[current_disk] << 1) & 0x3e)    /* return sector number and...) */
                    | 0xc0 | sector_true);                          /* sector true, and set 'unused' bits */
        }
        else {
            return 0xff;                                            /* head not loaded - return 0xff */
        }
    }

    in9_count = 0;
    /* drive functions */

    sim_debug(OUT_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x09: %x\n", current_disk, s100_bus_get_addr(), data);

    if (data & FDCP_STEPIN) {      /* step head in                             */
        if (current_track[current_disk] == (tracks[current_disk] - 1)) {
            sim_debug(TRACK_STUCK_MSG, &fdcp_dev,
                   "DSK%i: " ADDRESS_FORMAT " Unnecessary step in.\n",
                   current_disk, s100_bus_get_addr());
        }

        current_track[current_disk]++;
        current_flag[current_disk] &= ~FDCP_TRK0;     /* mwd 1/29/13: track zero now false */

        if (current_track[current_disk] > (tracks[current_disk] - 1)) {
            current_track[current_disk] = (tracks[current_disk] - 1);
        }
        if (dirty) {         /* implies that current_disk < NUM_OF_DSK   */
            writebuf();
        }

        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = MAX_SECT_SIZE;
    }

    if (data & FDCP_STEPOUT) {      /* step head out                            */
        if (current_track[current_disk] == 0) {
            sim_debug(TRACK_STUCK_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT " Unnecessary step out.\n",
                  current_disk, s100_bus_get_addr());
        }

        current_track[current_disk]--;

        if (current_track[current_disk] < 0) {
            current_track[current_disk] = 0;
            current_flag[current_disk] |= FDCP_TRK0; /* track 0 if there     */
        }
        if (dirty) {        /* implies that current_disk < NUM_OF_DSK   */
            writebuf();
        }

        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = MAX_SECT_SIZE;
    }

    if (dirty) {            /* implies that current_disk < NUM_OF_DSK   */
        writebuf();
    }

    if (data & FDCP_HDLD) {      /* head load                                */
        current_flag[current_disk] |= FDCP_HS;      /* turn on head loaded bit          */
        current_flag[current_disk] |= FDCP_NRDA;    /* turn on 'read data available'    */
    }

    if ((data & FDCP_HDUL) && (drive_type != DSK_ALTAIR_MINIDISK)) { /* head unload */
        current_flag[current_disk]      &= ~FDCP_HS;   /* turn off 'head loaded'   bit     */
        current_flag[current_disk]      &= ~FDCP_NRDA; /* turn off 'read data available'   */
        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = MAX_SECT_SIZE;
    }

    /* interrupts & head current are ignored                            */

    if (data & FDCP_WE) {                      /* write sequence start     */
        current_byte[current_disk] = 0;
        current_flag[current_disk] |= FDCP_ENWD;    /* enter new write data on  */
    }

    if ((data & FDCP_RE) && (drive_type == 5)) { /* Read Enable */
        read_enable[current_disk] = 1;
        dummy_read[current_disk] = 1;

        sim_debug(READ_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT " Read Enable\n",
                  current_disk, s100_bus_get_addr());
    }

    return 0;                               /* ignored since OUT        */
}

/* Disk Data In/Out */

static int32 fdcp_0ah(const int32 port, const int32 io, const int32 data)
{
    int32 i, rtn;
    UNIT *uptr;

    if (current_disk >= NUM_OF_DSK) {
        sim_debug(VERBOSE_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT
                  " Attempt of %s 0x0a on unattached disk - ignored.\n",
                  current_disk, s100_bus_get_addr(), selectInOut(io));

        return 0;
    }

    /* now current_disk < NUM_OF_DSK */
    in9_count = 0;
    uptr = fdcp_dev.units + current_disk;

    if (io == S100_IO_READ) {
        if (current_byte[current_disk] >= sector_size[current_disk]) {
            if (drive_type == 5 && read_enable[current_disk] == 0) {
                for (i = 0; i < sizeof(bootSector); i++) {
                    dskbuf[i] = bootSector[i];
                }
                current_byte[current_disk] = 0; /* reset sector buffer index */

                sim_debug(VERBOSE_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT "Sending BOOT loader\n",
                    current_disk, s100_bus_get_addr());
            }
            else {
                /* physically read the sector */
                sim_debug(READ_MSG, &fdcp_dev,
                      "DSK%i: " ADDRESS_FORMAT " IN 0x0a (READ) D%d T%d (L%d) S%d SS%d\n",
                      current_disk, s100_bus_get_addr(), current_disk,
                      current_track[current_disk], logical_track, current_sector[current_disk], sector_size[current_disk]);

                /* clear disk buffer */
                for (i = 0; i < sector_size[current_disk]; i++) {
                    dskbuf[i] = 0x00;
                }

                /* seek to track/sector */
                if (dskseek(uptr)) {
                    sim_debug(VERBOSE_MSG, &fdcp_dev,
                          "DSK%i: " ADDRESS_FORMAT " fseek error D%d T%d S%d\n",
                          current_disk, s100_bus_get_addr(), current_disk,
                          current_track[current_disk], current_sector[current_disk]);
                }

                /* read sector */
                rtn = sim_fread(dskbuf, 1, sector_size[current_disk], uptr -> fileref);

                if (rtn != sector_size[current_disk]) {
                    sim_debug(VERBOSE_MSG, &fdcp_dev,
                          "DSK%i: " ADDRESS_FORMAT " sim_fread error D%d T%d S%d\n",
                          current_disk, s100_bus_get_addr(), current_disk,
                          current_track[current_disk], current_sector[current_disk]);
                }

                current_byte[current_disk] = 0; /* reset sector buffer index */
                dummy_read[current_disk] = 1;
                read_enable[current_disk] = 0;

                sim_debug(READ_MSG, &fdcp_dev,
                    "DSK%i: " ADDRESS_FORMAT " Read Disable (SEC READ)\n",
                    current_disk, s100_bus_get_addr());
            }
        }

        /*
         * When an IN DRVDATA is executed, it interrupts the PIC processor on the FDC+
         * which then grabs the next sector byte and writes it to the output register that
         * the 8080 sees. However, this cannot be done fast enough to be the data that that
         * same IN instruction fetches. So IN instruction "n" returns the data output by
         * the PIC in response to IN instruction "n-1".
        */
        if (drive_type == 5 && dummy_read[current_disk]) {
            dummy_read[current_disk] = 0;
            return 0xff;
        }

        return dskbuf[current_byte[current_disk]++] & 0xff;
    }
    else { /* write */
        if (current_byte[current_disk] >= sector_size[current_disk]) {
            writebuf();     /* from above we have that current_disk < NUM_OF_DSK */
        }
        else {
            dirty = TRUE;   /* this guarantees for the next call to writebuf that current_disk < NUM_OF_DSK */
            dskbuf[current_byte[current_disk]++] = data & 0xff;
        }

        return 0xff;   /* ignored since OUT */
    }
}

/* FDC+ Extended I/O */

static int32 fdcp_0bh(const int32 port, const int32 io, const int32 data)
{
    if (io == S100_IO_READ) {
        sim_debug(IN_MSG, &fdcp_dev,
                  "DSK%i: " ADDRESS_FORMAT " IN 0x0b %02X\n",
                  current_disk, s100_bus_get_addr(), 0x80);

        return 0x80;
    }
    else {
        sim_debug(OUT_MSG, &fdcp_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x0b: %x\n", current_disk, s100_bus_get_addr(), data);

        if ((data < 0) || (data >= 2 * tracks[current_disk])) {
            logical_track = 2 * tracks[current_disk] - 1;
        }
        else {
            logical_track = data;
        }
    }

    return 0xff;
}

static t_stat fdcp_set_type(UNIT *uptr, int32 value, const char *cptr, void *desc)
{
    int32 i, result, type = 0;

    if (cptr != NULL) {
        result = sscanf(cptr, "%d", &type);

        if (type < 0 || type > 7) {
            return SCPE_ARG;
        }
        else if (type != 5 && type != 7) {
            sim_printf("Use the DSK device for drive type %d\n", type);

            return SCPE_ARG | SCPE_NOMESSAGE;
        }
    }

    if (drive_type != type) { /* Detach all disks */
        for (i = 0; i < NUM_OF_DSK; i++) {
            fdcp_detach(&fdcp_unit[i]);
        }
    }

    if (type == 7) {
        sim_printf("Drive type 7 not implemented.\n");
    }
    else {
        drive_type = type;
    }

    return SCPE_OK;
}

static t_stat fdcpd_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nFarmTek FDC+ (%s)\n", sim_dname(dptr));

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    fprintf(st, "\n----- NOTES -----\n\n");
    fprintf(st, "The %s device simulates FarmTek's FDC+ disk controller.\n", DEVICE_DEV);
    fprintf(st, "The following drive types are supported:\n\n");
    fprintf(st, "  Type 5 - 1.5MB on 5.25\" floppy disk\n");
    fprintf(st, "  Type 7 - Serial Drive Server\n\n");
    fprintf(st, "Additional information on the FDC+ can be found at\n");
    fprintf(st, "https://deramp.com/fdc_plus.html\n");

    fprintf(st, "\n");

    return SCPE_OK;
}

