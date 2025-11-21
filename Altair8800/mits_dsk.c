/* mits_dsk.c: MITS Altair 88-DCDD Simulator

   Copyright (c) 2025 Patrick A. Linstruth

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
   Minidisk support added by Mike Douglas

   History:
   07-Nov-2025 Initial version

   ==================================================================
  
   The 88-DCDD is a 8-inch floppy controller which can control up
   to 16 daisy-chained Pertec FD-400 hard-sectored floppy drives.
   Each diskette has physically 77 tracks of 32 137-byte sectors
   each.

   The controller is interfaced to the CPU by use of 3 I/O addresses,
   standardly, these are device numbers 10, 11, and 12 (octal).

   Address     Mode    Function
   -------     ----    --------
       10      Out     Selects and enables Controller and Drive
       10      In      Indicates status of Drive and Controller
       11      Out     Controls Disk Function
       11      In      Indicates current sector position of disk
       12      Out     Write data
       12      In      Read data

   Drive Select Out (Device 10 OUT):

   +---+---+---+---+---+---+---+---+
   | C | X | X | X |    Device     |
   +---+---+---+---+---+---+---+---+

   C = If this bit is 1, the disk controller selected by 'device' is
           cleared. If the bit is zero, 'device' is selected as the
           device being controlled by subsequent I/O operations.
   X = not used
   Device = value zero thru 15, selects drive to be controlled.

   Drive Status In (Device 10 IN):

   +---+---+---+---+---+---+---+---+
   | R | Z | I | X | X | H | M | W |
   +---+---+---+---+---+---+---+---+

   W - When 0, write circuit ready to write another byte.
   M - When 0, head movement is allowed
   H - When 0, indicates head is loaded for read/write
   X - not used (will be 0)
   I - When 0, indicates interrupts enabled (not used by this simulator)
   Z - When 0, indicates head is on track 0
   R - When 0, indicates that read circuit has new byte to read

   Drive Control (Device 11 OUT):

   +---+---+---+---+---+---+---+---+
   | W | C | D | E | U | H | O | I |
   +---+---+---+---+---+---+---+---+

   I - When 1, steps head IN one track
   O - When 1, steps head OUT one track
   H - When 1, loads head to drive surface
   U - When 1, unloads head
   E - Enables interrupts (ignored by this simulator)
   D - Disables interrupts (ignored by this simulator)
   C - When 1 lowers head current (ignored by this simulator)
   W - When 1, starts Write Enable sequence:   W bit on device 10
           (see above) will go 1 and data will be read from port 12
           until 137 bytes have been read by the controller from
           that port. The W bit will go off then, and the sector data
           will be written to disk. Before you do this, you must have
           stepped the track to the desired number, and waited until
           the right sector number is presented on device 11 IN, then
           set this bit.

   Sector Position (Device 11 IN):

   As the sectors pass by the read head, they are counted and the
   number of the current one is available in this register.

   +---+---+---+---+---+---+---+---+
   | X | X |  Sector Number    | T |
   +---+---+---+---+---+---+---+---+

   X = Not used
   Sector number = binary of the sector number currently under the
                                   head, 0-31.
   T = Sector True, is a 0 when the sector is positioned to read or
           write.

*/

#include "sim_defs.h"
#include "altair8800_sys.h"
#include "altair8800_dsk.h"
#include "s100_bus.h"
#include "mits_dsk.h"

static int32 poc = TRUE; /* Power On Clear */

/* Debug flags */
#define IN_MSG              (1 << 0)
#define OUT_MSG             (1 << 1)
#define READ_MSG            (1 << 2)
#define WRITE_MSG           (1 << 3)
#define SECTOR_STUCK_MSG    (1 << 4)
#define TRACK_STUCK_MSG     (1 << 5)
#define VERBOSE_MSG         (1 << 6)

static int32 mdsk10(const int32 port, const int32 io, const int32 data);
static int32 mdsk11(const int32 port, const int32 io, const int32 data);
static int32 mdsk12(const int32 port, const int32 io, const int32 data);

static t_stat mdsk_boot(int32 unitno, DEVICE *dptr);
static t_stat mdsk_reset(DEVICE *dptr);
static t_stat mdsk_attach(UNIT *uptr, CONST char *cptr);
static t_stat mdsk_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char* mdsk_description(DEVICE *dptr);

/* global data on status */

/* currently selected drive (values are 0 .. NUM_OF_DSK)
   current_disk < NUM_OF_DSK implies that the corresponding disk is attached to a file */
static int32 current_disk      = NUM_OF_DSK;

static int32 current_track     [NUM_OF_DSK];
static int32 current_sector    [NUM_OF_DSK];
static int32 current_byte      [NUM_OF_DSK];
static int32 current_flag      [NUM_OF_DSK];
static int32 sectors_per_track [NUM_OF_DSK];
static int32 current_imageSize [NUM_OF_DSK];
static int32 tracks            [NUM_OF_DSK];
static int32 in9_count         = 0;
static int32 in9_message       = FALSE;
static int32 dirty             = FALSE;    /* TRUE when buffer has unwritten data in it    */
static int32 warnLevelDSK      = 3;
static int32 warnLock          [NUM_OF_DSK];
static int32 warnAttached      [NUM_OF_DSK];
static int32 warnDSK10         = 0;
static int32 warnDSK11         = 0;
static int32 warnDSK12         = 0;
static int8 dskbuf[DSK_SECTSIZE];                       /* data Buffer                                  */
static int32 sector_true       = 0;        /* sector true flag for sector register read    */

/* 88DSK Standard I/O Data Structures */

static UNIT mdsk_unit[NUM_OF_DSK] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) }
};

static REG mdsk_reg[] = {
    { FLDATAD (POC,     poc,       0x01,         "Power on Clear flag"), },
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
    { BRDATAD (IMAGESIZE, current_imageSize, 10, 32,  NUM_OF_DSK,
               "Size of disk image array"), REG_CIRC + REG_RO                               },
    { DRDATAD (IN9COUNT,     in9_count,         4,
               "Count of IN(9) register"),  REG_RO                                          },
    { DRDATAD (IN9MESSAGE,   in9_message,       4,
               "BOOL for IN(9) message register"), REG_RO                                   },
    { DRDATAD (DIRTY,        dirty,             4,
               "BOOL for write needed register"), REG_RO                                    },
    { DRDATAD (DSKWL,        warnLevelDSK,      32,
               "Warn level register")                                                       },
    { BRDATAD (WARNLOCK,     warnLock,          10, 32, NUM_OF_DSK,
               "Count of write to locked register array"), REG_CIRC + REG_RO                },
    { BRDATAD (WARNATTACHED, warnAttached,      10, 32, NUM_OF_DSK,
               "Count for selection of unattached disk register array"), REG_CIRC + REG_RO  },
    { DRDATAD (WARNDSK10,    warnDSK10,         4,
               "Count of IN(8) on unattached disk register"), REG_RO                        },
    { DRDATAD (WARNDSK11,    warnDSK11,         4,
               "Count of IN/OUT(9) on unattached disk register"), REG_RO                    },
    { DRDATAD (WARNDSK12,    warnDSK12,         4,
               "Count of IN/OUT(10) on unattached disk register"), REG_RO                   },
    { BRDATAD (DISKBUFFER,   dskbuf,           10, 8,  DSK_SECTSIZE,
               "Disk data buffer array"), REG_CIRC + REG_RO                                 },
    { NULL }
};

#define DSK_NAME    "MITS 88-DCDD Floppy Disk Controller"
#define DEV_NAME    "DSK"

static const char* mdsk_description(DEVICE *dptr) {
    return DSK_NAME;
}

static MTAB mdsk_mod[] = {
    { UNIT_DSK_WLK,     0,                  "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " DSK_NAME "n for writing" },
    { UNIT_DSK_WLK,     UNIT_DSK_WLK,       "WRTLCK",    "WRTLCK",  NULL, NULL, NULL,
        "Locks " DSK_NAME "n for writing" },
    { 0 }
};

/* Debug Flags */
static DEBTAB mdsk_dt[] = {
    { "IN",             IN_MSG,             "IN operations"     },
    { "OUT",            OUT_MSG,            "OUT operations"    },
    { "READ",           READ_MSG,           "Read operations"   },
    { "WRITE",          WRITE_MSG,          "Write operations"  },
    { "SECTOR_STUCK",   SECTOR_STUCK_MSG,   "Sector stuck"      },
    { "TRACK_STUCK",    TRACK_STUCK_MSG,    "Track stuck"       },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

DEVICE mdsk_dev = {
    DEV_NAME, mdsk_unit, mdsk_reg, mdsk_mod,
    NUM_OF_DSK, ADDRRADIX, ADDRWIDTH, 1, DATARADIX, DATAWIDTH,
    NULL, NULL, &mdsk_reset,
    &mdsk_boot, &mdsk_attach, NULL,
    NULL, (DEV_DISABLE | DEV_DEBUG), 0,
    mdsk_dt, NULL, NULL, &mdsk_show_help, &dsk_attach_help, NULL,
    &mdsk_description
};

static const char* selectInOut(const int32 io) {
    return io == 0 ? "IN" : "OUT";
}

/* service routines to handle simulator functions */
/* reset routine */

static t_stat mdsk_reset(DEVICE *dptr)
{
    int32 i;

    if (dptr->flags & DEV_DIS) {
        s100_bus_remio(0x08, 1, &mdsk10);
        s100_bus_remio(0x09, 1, &mdsk11);
        s100_bus_remio(0x0A, 1, &mdsk12);

        poc = TRUE;
    }
    else {
        if (poc) {
            s100_bus_addio(0x08, 1, &mdsk10, dptr->name);
            s100_bus_addio(0x09, 1, &mdsk11, dptr->name);
            s100_bus_addio(0x0A, 1, &mdsk12, dptr->name);

            for (i = 0; i < NUM_OF_DSK; i++) {
                current_imageSize[i] = 0;
                sectors_per_track[i] = DSK_SECT;
                tracks[i] = MAX_TRACKS;
            }
        }
    }

    for (i = 0; i < NUM_OF_DSK; i++) {
        warnLock[i]         = 0;
        warnAttached[i]     = 0;
        current_track[i]    = 0;
        current_sector[i]   = 0;
        current_byte[i]     = 0;
        current_flag[i]     = 0;
    }

    warnDSK10       = 0;
    warnDSK11       = 0;
    warnDSK12       = 0;
    current_disk    = NUM_OF_DSK;
    in9_count       = 0;
    in9_message     = FALSE;

    return SCPE_OK;
}
/* mdsk_attach - determine type of drive attached based on disk image size */

static t_stat mdsk_attach(UNIT *uptr, CONST char *cptr)
{
    int32 thisUnitIndex;
    int32 imageSize;
    t_stat r;

    sim_switches |= SWMASK ('E');     /* File must exist */

    r = attach_unit(uptr, cptr);      /* attach unit     */

    if (r != SCPE_OK) {               /* error?          */
        return r;
    }

    ASSURE(uptr != NULL);
    thisUnitIndex = sys_find_unit_index(uptr);
    ASSURE((0 <= thisUnitIndex) && (thisUnitIndex < NUM_OF_DSK));

    /*  If the file size is close to the mini-disk image size, set the number of
     tracks to 16, otherwise, 32 sectors per track. */

    imageSize = sim_fsize(uptr -> fileref);
    current_imageSize[thisUnitIndex] = imageSize;
    sectors_per_track[thisUnitIndex] = (((MINI_DISK_SIZE - MINI_DISK_DELTA < imageSize) &&
                                         (imageSize < MINI_DISK_SIZE + MINI_DISK_DELTA)) ?
                                        MINI_DISK_SECT : DSK_SECT);
    return SCPE_OK;
}

static t_stat mdsk_boot(int32 unitno, DEVICE *dptr)
{
    *((int32 *) sim_PC->loc) = 0xff00;
    return SCPE_OK;
}

static int32 dskseek(const UNIT *xptr)
{
    return sim_fseek(xptr -> fileref, DSK_SECTSIZE * sectors_per_track[current_disk] * current_track[current_disk] +
        DSK_SECTSIZE * current_sector[current_disk], SEEK_SET);
}

/* precondition: current_disk < NUM_OF_DSK */
static void writebuf(void)
{
    int32 i, rtn;
    UNIT *uptr;
    i = current_byte[current_disk];         /* null-fill rest of sector if any */
    while (i < DSK_SECTSIZE)
        dskbuf[i++] = 0;
    uptr = mdsk_dev.units + current_disk;
    if (((uptr -> flags) & UNIT_DSK_WLK) == 0) { /* write enabled */
        sim_debug(WRITE_MSG, &mdsk_dev,
                  "DSK%i: " ADDRESS_FORMAT " OUT 0x0a (WRITE) D%d T%d S%d\n",
                  current_disk, s100_bus_get_addr(), current_disk,
                  current_track[current_disk], current_sector[current_disk]);
        if (dskseek(uptr)) {
            sim_debug(VERBOSE_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " fseek failed D%d T%d S%d\n",
                      current_disk, s100_bus_get_addr(), current_disk,
                      current_track[current_disk], current_sector[current_disk]);
        }
        rtn = sim_fwrite(dskbuf, 1, DSK_SECTSIZE, uptr -> fileref);
        if (rtn != DSK_SECTSIZE) {
            sim_debug(VERBOSE_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " sim_fwrite failed T%d S%d Return=%d\n",
                      current_disk, s100_bus_get_addr(), current_track[current_disk],
                      current_sector[current_disk], rtn);
        }
    } else if ( (mdsk_dev.dctrl & VERBOSE_MSG) && (warnLock[current_disk] < warnLevelDSK) ) {
        /* write locked - print warning message if required */
        warnLock[current_disk]++;
        sim_debug(VERBOSE_MSG, &mdsk_dev,
                  "DSK%i: " ADDRESS_FORMAT " Attempt to write to locked DSK%d - ignored.\n",
                  current_disk, s100_bus_get_addr(), current_disk);
    }
    current_flag[current_disk]  &= 0xfe;    /* ENWD off */
    current_byte[current_disk]  = 0xff;
    dirty                       = FALSE;
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

static int32 mdsk10(const int32 port, const int32 io, const int32 data)
{
    int32 current_disk_flags;
    in9_count = 0;
    if (io == 0) {                                      /* IN: return flags */
        if (current_disk >= NUM_OF_DSK) {
            if ((mdsk_dev.dctrl & VERBOSE_MSG) && (warnDSK10 < warnLevelDSK)) {
                warnDSK10++;
                sim_debug(VERBOSE_MSG, &mdsk_dev,
                          "DSK%i: " ADDRESS_FORMAT
                          " Attempt of IN 0x08 on unattached disk - ignored.\n",
                          current_disk, s100_bus_get_addr());
            }
            return 0xff;                                /* no drive selected - can do nothing */
        }
        return (~current_flag[current_disk]) & 0xff;    /* return the COMPLEMENT! */
    }

    /* OUT: Controller set/reset/enable/disable */
    if (dirty)    /* implies that current_disk < NUM_OF_DSK */
        writebuf();
    sim_debug(OUT_MSG, &mdsk_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x08: %x\n", current_disk, s100_bus_get_addr(), data);
    current_disk = data & NUM_OF_DSK_MASK; /* 0 <= current_disk < NUM_OF_DSK */
    current_disk_flags = (mdsk_dev.units + current_disk) -> flags;
    if ((current_disk_flags & UNIT_ATT) == 0) { /* nothing attached? */
        if ( (mdsk_dev.dctrl & VERBOSE_MSG) && (warnAttached[current_disk] < warnLevelDSK) ) {
            warnAttached[current_disk]++;
            sim_debug(VERBOSE_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt to select unattached DSK%d - ignored.\n",
                      current_disk, s100_bus_get_addr(), current_disk);
        }
        current_disk = NUM_OF_DSK;
    } else {
        current_sector[current_disk]    = 0xff;     /* reset internal counters */
        current_byte[current_disk]      = 0xff;
        if (data & 0x80)                            /* disable drive? */
            current_flag[current_disk] = 0;         /* yes, clear all flags */
        else {                                      /* enable drive */
            current_flag[current_disk] = 0x1a;      /* move head true */
            if (current_track[current_disk] == 0)   /* track 0? */
                current_flag[current_disk] |= 0x40; /* yes, set track 0 true as well */
            if (sectors_per_track[current_disk] == MINI_DISK_SECT)  /* drive enable loads head for Minidisk */
                current_flag[current_disk] |= 0x84;
        }
    }
    return 0;   /* ignored since OUT */
}

/* Disk Drive Status/Functions */

static int32 mdsk11(const int32 port, const int32 io, const int32 data)
{
    if (current_disk >= NUM_OF_DSK) {
        if ((mdsk_dev.dctrl & VERBOSE_MSG) && (warnDSK11 < warnLevelDSK)) {
            warnDSK11++;
            sim_debug(VERBOSE_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt of %s 0x09 on unattached disk - ignored.\n",
                      current_disk, s100_bus_get_addr(), selectInOut(io));
        }
        return 0xff;    /* no drive selected - can do nothing */
    }

    /* now current_disk < NUM_OF_DSK */
    if (io == 0) {  /* read sector position */
        in9_count++;
        if ((mdsk_dev.dctrl & SECTOR_STUCK_MSG) && (in9_count > 2 * DSK_SECT) && (!in9_message)) {
            in9_message = TRUE;
            sim_debug(SECTOR_STUCK_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Looping on sector find.\n",
                      current_disk, s100_bus_get_addr());
        }
        sim_debug(IN_MSG, &mdsk_dev, "DSK%i: " ADDRESS_FORMAT " IN 0x09\n", current_disk, s100_bus_get_addr());
        if (dirty)  /* implies that current_disk < NUM_OF_DSK */
            writebuf();
        if (current_flag[current_disk] & 0x04) {    /* head loaded? */
            sector_true ^= 1;                       /* return sector true every other entry */
            if (sector_true == 0) {                 /* true when zero */
                current_sector[current_disk]++;
                if (current_sector[current_disk] >= sectors_per_track[current_disk])
                    current_sector[current_disk] = 0;
                current_byte[current_disk] = 0xff;
            }
            return (((current_sector[current_disk] << 1) & 0x3e)    /* return sector number and...) */
                    | 0xc0 | sector_true);                          /* sector true, and set 'unused' bits */
        } else
            return 0xff;                                            /* head not loaded - return 0xff */
    }

    in9_count = 0;
    /* drive functions */

    sim_debug(OUT_MSG, &mdsk_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x09: %x\n", current_disk, s100_bus_get_addr(), data);
    if (data & 0x01) {      /* step head in                             */
        if (current_track[current_disk] == (tracks[current_disk] - 1)) {
            sim_debug(TRACK_STUCK_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Unnecessary step in.\n",
                   current_disk, s100_bus_get_addr());
        }
        current_track[current_disk]++;
        current_flag[current_disk] &= 0xbf;     /* mwd 1/29/13: track zero now false */
        if (current_track[current_disk] > (tracks[current_disk] - 1))
            current_track[current_disk] = (tracks[current_disk] - 1);
        if (dirty)          /* implies that current_disk < NUM_OF_DSK   */
            writebuf();
        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = 0xff;
    }

    if (data & 0x02) {      /* step head out                            */
        if (current_track[current_disk] == 0) {
            sim_debug(TRACK_STUCK_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Unnecessary step out.\n",
                      current_disk, s100_bus_get_addr());
        }
        current_track[current_disk]--;
        if (current_track[current_disk] < 0) {
            current_track[current_disk] = 0;
            current_flag[current_disk] |= 0x40; /* track 0 if there     */
        }
        if (dirty)          /* implies that current_disk < NUM_OF_DSK   */
            writebuf();
        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = 0xff;
    }

    if (dirty)              /* implies that current_disk < NUM_OF_DSK   */
        writebuf();

    if (data & 0x04) {      /* head load                                */
        current_flag[current_disk] |= 0x04;         /* turn on head loaded bit          */
        current_flag[current_disk] |= 0x80;         /* turn on 'read data available'    */
    }

    if ((data & 0x08) && (sectors_per_track[current_disk] != MINI_DISK_SECT)) { /* head unload */
        current_flag[current_disk]      &= 0xfb;    /* turn off 'head loaded'   bit     */
        current_flag[current_disk]      &= 0x7f;    /* turn off 'read data available'   */
        current_sector[current_disk]    = 0xff;
        current_byte[current_disk]      = 0xff;
    }

    /* interrupts & head current are ignored                            */

    if (data & 0x80) {                      /* write sequence start     */
        current_byte[current_disk] = 0;
        current_flag[current_disk] |= 0x01; /* enter new write data on  */
    }
    return 0;                               /* ignored since OUT        */
}

/* Disk Data In/Out */

static int32 mdsk12(const int32 port, const int32 io, const int32 data)
{
    int32 i, rtn;
    UNIT *uptr;

    if (current_disk >= NUM_OF_DSK) {
        if ((mdsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
            warnDSK12++;
            sim_debug(VERBOSE_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt of %s 0x0a on unattached disk - ignored.\n",
                      current_disk, s100_bus_get_addr(), selectInOut(io));
        }
        return 0;
    }

    /* now current_disk < NUM_OF_DSK */
    in9_count = 0;
    uptr = mdsk_dev.units + current_disk;
    if (io == 0) {
        if (current_byte[current_disk] >= DSK_SECTSIZE) {
            /* physically read the sector */
            sim_debug(READ_MSG, &mdsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " IN 0x0a (READ) D%d T%d S%d\n",
                      current_disk, s100_bus_get_addr(), current_disk,
                      current_track[current_disk], current_sector[current_disk]);
            for (i = 0; i < DSK_SECTSIZE; i++)
                dskbuf[i] = 0;
            if (dskseek(uptr)) {
                if ((mdsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
                    warnDSK12++;
                    sim_debug(VERBOSE_MSG, &mdsk_dev,
                              "DSK%i: " ADDRESS_FORMAT " fseek error D%d T%d S%d\n",
                              current_disk, s100_bus_get_addr(), current_disk,
                              current_track[current_disk], current_sector[current_disk]);
                }
            }
            rtn = sim_fread(dskbuf, 1, DSK_SECTSIZE, uptr -> fileref);
            if (rtn != DSK_SECTSIZE) {
                if ((mdsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
                    warnDSK12++;
                    sim_debug(VERBOSE_MSG, &mdsk_dev,
                              "DSK%i: " ADDRESS_FORMAT " sim_fread error D%d T%d S%d\n",
                              current_disk, s100_bus_get_addr(), current_disk,
                              current_track[current_disk], current_sector[current_disk]);
                }
            }
            current_byte[current_disk] = 0;
        }
        return dskbuf[current_byte[current_disk]++] & 0xff;
    } else {
        if (current_byte[current_disk] >= DSK_SECTSIZE)
            writebuf();     /* from above we have that current_disk < NUM_OF_DSK */
        else {
            dirty = TRUE;   /* this guarantees for the next call to writebuf that current_disk < NUM_OF_DSK */
            dskbuf[current_byte[current_disk]++] = data & 0xff;
        }
        return 0;   /* ignored since OUT */
    }
}

static t_stat mdsk_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "\nAltair 8800 88-DCDD (%s)\n", sim_dname(dptr));

    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    fprint_reg_help (st, dptr);

    return SCPE_OK;
}

