/*  altairz80_dsk.c: MITS Altair 88-DISK Simulator

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

    Based on work by Charles E Owen (c) 1997

    The 88_DISK is a 8-inch floppy controller which can control up
    to 16 daisy-chained Pertec FD-400 hard-sectored floppy drives.
    Each diskette has physically 77 tracks of 32 137-byte sectors
    each.

    The controller is interfaced to the CPU by use of 3 I/O addresses,
    standardly, these are device numbers 10, 11, and 12 (octal).

    Address Mode    Function
    ------- ----    --------

        10          Out     Selects and enables Controller and Drive
        10          In      Indicates status of Drive and Controller
        11          Out     Controls Disk Function
        11          In      Indicates current sector position of disk
        12          Out     Write data
        12          In      Read data

    Drive Select Out (Device 10 OUT):

    +---+---+---+---+---+---+---+---+
    | C | X | X | X |   Device      |
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

    ----------------------------------------------------------

    5/22/2014 - Updated by Mike Douglas to support the Altair Minidisk.
                This disk uses 35 (vs 70) tracks of 16 (vs 32) sectors 
                of 137 bytes each.

    6/30/2014 - When the disk is an Altair Minidisk, load the head as 
                soon as the disk is enabled, and ignore the head
                unload command (both like the real hardware). 

    7/13/2014 - This code previously returned zero when the sector position
                register was read with the head not loaded. This zero looks
                like an asserted "Sector True" flag for sector zero. The real
                hardware returns 0xff in this case. The same problem occurs
                when the drive is deselected - the sector position register
                returned zero instead of 0xff. These have been corrected.

    7/13/2014   Some software for the Altair skips a sector by verifying
                that "Sector True" goes false. Previously, this code
                returned "Sector True" every time the sector register 
                was read. Now the flag alternates true and false on
                subsequent reads of the sector register. 
*/

#include "altairz80_defs.h"

/* Debug flags */
#define IN_MSG              (1 << 0)
#define OUT_MSG             (1 << 1)
#define READ_MSG            (1 << 2)
#define WRITE_MSG           (1 << 3)
#define SECTOR_STUCK_MSG    (1 << 4)
#define TRACK_STUCK_MSG     (1 << 5)
#define VERBOSE_MSG         (1 << 6)

#define UNIT_V_DSK_WLK      (UNIT_V_UF + 0)         /* write locked                             */
#define UNIT_DSK_WLK        (1 << UNIT_V_DSK_WLK)
#define DSK_SECTSIZE        137                     /* size of sector                           */
#define DSK_SECT            32                      /* sectors per track                        */
#define MAX_TRACKS          254                     /* number of tracks,
                                                    original Altair has 77 tracks only          */
#define DSK_TRACSIZE        (DSK_SECTSIZE * DSK_SECT)
#define MAX_DSK_SIZE        (DSK_TRACSIZE * MAX_TRACKS)
#define NUM_OF_DSK_MASK     (NUM_OF_DSK - 1)
#define BOOTROM_SIZE_DSK    256                     /* size of boot rom                         */

#define MINI_DISK_SECT      16                      /* mini disk sectors per track              */
#define MINI_DISK_TRACKS    35                      /* number of tracks on mini disk            */
#define MINI_DISK_SIZE      (MINI_DISK_TRACKS * MINI_DISK_SECT * DSK_SECTSIZE)
#define MINI_DISK_DELTA     4096                    /* threshold for detecting mini disks       */

int32 dsk10(const int32 port, const int32 io, const int32 data);
int32 dsk11(const int32 port, const int32 io, const int32 data);
int32 dsk12(const int32 port, const int32 io, const int32 data);
static t_stat dsk_boot(int32 unitno, DEVICE *dptr);
static t_stat dsk_reset(DEVICE *dptr);
static t_stat dsk_attach(UNIT *uptr, CONST char *cptr);
static const char* dsk_description(DEVICE *dptr);

extern UNIT cpu_unit;
extern uint32 PCX;

extern t_stat install_bootrom(const int32 bootrom[], const int32 size, const int32 addr, const int32 makeROM);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
void install_ALTAIRbootROM(void);
extern int32 find_unit_index(UNIT *uptr);

/* global data on status */

/* currently selected drive (values are 0 .. NUM_OF_DSK)
   current_disk < NUM_OF_DSK implies that the corresponding disk is attached to a file */
static int32 current_disk                   = NUM_OF_DSK;
static int32 current_track  [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 current_sector [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 current_byte   [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 current_flag   [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 sectors_per_track [NUM_OF_DSK] = { DSK_SECT, DSK_SECT, DSK_SECT, DSK_SECT,
                                                DSK_SECT, DSK_SECT, DSK_SECT, DSK_SECT,
                                                DSK_SECT, DSK_SECT, DSK_SECT, DSK_SECT,
                                                DSK_SECT, DSK_SECT, DSK_SECT, DSK_SECT };
static int32 tracks         [NUM_OF_DSK]    = { MAX_TRACKS, MAX_TRACKS, MAX_TRACKS, MAX_TRACKS,
                                                MAX_TRACKS, MAX_TRACKS, MAX_TRACKS, MAX_TRACKS,
                                                MAX_TRACKS, MAX_TRACKS, MAX_TRACKS, MAX_TRACKS,
                                                MAX_TRACKS, MAX_TRACKS, MAX_TRACKS, MAX_TRACKS };
static int32 in9_count                      = 0;
static int32 in9_message                    = FALSE;
static int32 dirty                          = FALSE;    /* TRUE when buffer has unwritten data in it    */
static int32 warnLevelDSK                   = 3;
static int32 warnLock       [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 warnAttached   [NUM_OF_DSK]    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int32 warnDSK10                      = 0;
static int32 warnDSK11                      = 0;
static int32 warnDSK12                      = 0;
static int8 dskbuf[DSK_SECTSIZE];                       /* data Buffer                                  */
static int32 sector_true                    = 0;        /* sector true flag for sector register read    */

const static int32 alt_bootrom_dsk[BOOTROM_SIZE_DSK] = {  // boot ROM for mini disk support
    0x21, 0x13, 0xff, 0x11, 0x00, 0x4c, 0x0e, 0xe3, /* ff00-ff07 */
    0x7e, 0x12, 0x23, 0x13, 0x0d, 0xc2, 0x08, 0xff, /* ff08-ff0f */
    0xc3, 0x00, 0x4c, 0xf3, 0xaf, 0xd3, 0x22, 0x2f, /* ff10-ff17 */
    0xd3, 0x23, 0x3e, 0x2c, 0xd3, 0x22, 0x3e, 0x03, /* ff18-ff1f */
    0xd3, 0x10, 0xdb, 0xff, 0xe6, 0x11, 0x0f, 0x0f, /* ff20-ff27 */
    0xc6, 0x10, 0xd3, 0x10, 0x31, 0x71, 0x4d, 0xaf, /* ff28-ff2f */
    0xd3, 0x08, 0xdb, 0x08, 0xe6, 0x08, 0xc2, 0x1c, /* ff30-ff37 */
    0x4c, 0x3e, 0x04, 0xd3, 0x09, 0xc3, 0x38, 0x4c, /* ff38-ff3f */
    0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x2d, 0x4c, 0x3e, /* ff40-ff47 */
    0x02, 0xd3, 0x09, 0xdb, 0x08, 0xe6, 0x40, 0xc2, /* ff48-ff4f */
    0x2d, 0x4c, 0x11, 0x00, 0x00, 0x06, 0x00, 0x3e, /* ff50-ff57 */
    0x10, 0xf5, 0xd5, 0xc5, 0xd5, 0x11, 0x86, 0x80, /* ff58-ff5f */
    0x21, 0xe3, 0x4c, 0xdb, 0x09, 0x1f, 0xda, 0x50, /* ff60-ff67 */
    0x4c, 0xe6, 0x1f, 0xb8, 0xc2, 0x50, 0x4c, 0xdb, /* ff68-ff6f */
    0x08, 0xb7, 0xfa, 0x5c, 0x4c, 0xdb, 0x0a, 0x77, /* ff70-ff77 */
    0x23, 0x1d, 0xc2, 0x5c, 0x4c, 0xe1, 0x11, 0xe6, /* ff78-ff7f */
    0x4c, 0x01, 0x80, 0x00, 0x1a, 0x77, 0xbe, 0xc2, /* ff80-ff87 */
    0xc3, 0x4c, 0x80, 0x47, 0x13, 0x23, 0x0d, 0xc2, /* ff88-ff8f */
    0x71, 0x4c, 0x1a, 0xfe, 0xff, 0xc2, 0x88, 0x4c, /* ff90-ff97 */
    0x13, 0x1a, 0xb8, 0xc1, 0xeb, 0xc2, 0xba, 0x4c, /* ff98-ff9f */
    0xf1, 0xf1, 0x2a, 0xe4, 0x4c, 0xcd, 0xdd, 0x4c, /* ffa0-ffa7 */
    0xd2, 0xb3, 0x4c, 0x04, 0x04, 0x78, 0xfe, 0x10, /* ffa8-ffaf */
    0xda, 0x44, 0x4c, 0x06, 0x01, 0xca, 0x44, 0x4c, /* ffb0-ffb7 */
    0xdb, 0x08, 0xe6, 0x02, 0xc2, 0xa5, 0x4c, 0x3e, /* ffb8-ffbf */
    0x01, 0xd3, 0x09, 0xc3, 0x42, 0x4c, 0x3e, 0x80, /* ffc0-ffc7 */
    0xd3, 0x08, 0xc3, 0x00, 0x00, 0xd1, 0xf1, 0x3d, /* ffc8-ffcf */
    0xc2, 0x46, 0x4c, 0x3e, 0x43, 0x01, 0x3e, 0x4d, /* ffd0-ffd7 */
    0xfb, 0x32, 0x00, 0x00, 0x22, 0x01, 0x00, 0x47, /* ffd8-ffdf */
    0x3e, 0x80, 0xd3, 0x08, 0x78, 0xd3, 0x01, 0xd3, /* ffe0-ffe7 */
    0x11, 0xd3, 0x05, 0xd3, 0x23, 0xc3, 0xd2, 0x4c, /* ffe8-ffef */
    0x7a, 0xbc, 0xc0, 0x7b, 0xbd, 0xc9, 0x00, 0x00, /* fff0-fff7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fff8-ffff */
};

/* Altair MITS modified BOOT EPROM, fits in upper 256 byte of memory */
int32 bootrom_dsk[BOOTROM_SIZE_DSK] = {
    0xf3, 0x06, 0x80, 0x3e, 0x0e, 0xd3, 0xfe, 0x05, /* ff00-ff07 */
    0xc2, 0x05, 0xff, 0x3e, 0x16, 0xd3, 0xfe, 0x3e, /* ff08-ff0f */
    0x12, 0xd3, 0xfe, 0xdb, 0xfe, 0xb7, 0xca, 0x20, /* ff10-ff17 */
    0xff, 0x3e, 0x0c, 0xd3, 0xfe, 0xaf, 0xd3, 0xfe, /* ff18-ff1f */
    0x21, 0x00, 0x5c, 0x11, 0x33, 0xff, 0x0e, 0x88, /* ff20-ff27 */
    0x1a, 0x77, 0x13, 0x23, 0x0d, 0xc2, 0x28, 0xff, /* ff28-ff2f */
    0xc3, 0x00, 0x5c, 0x31, 0x21, 0x5d, 0x3e, 0x00, /* ff30-ff37 */
    0xd3, 0x08, 0x3e, 0x04, 0xd3, 0x09, 0xc3, 0x19, /* ff38-ff3f */
    0x5c, 0xdb, 0x08, 0xe6, 0x02, 0xc2, 0x0e, 0x5c, /* ff40-ff47 */
    0x3e, 0x02, 0xd3, 0x09, 0xdb, 0x08, 0xe6, 0x40, /* ff48-ff4f */
    0xc2, 0x0e, 0x5c, 0x11, 0x00, 0x00, 0x06, 0x08, /* ff50-ff57 */
    0xc5, 0xd5, 0x11, 0x86, 0x80, 0x21, 0x88, 0x5c, /* ff58-ff5f */
    0xdb, 0x09, 0x1f, 0xda, 0x2d, 0x5c, 0xe6, 0x1f, /* ff60-ff67 */
    0xb8, 0xc2, 0x2d, 0x5c, 0xdb, 0x08, 0xb7, 0xfa, /* ff68-ff6f */
    0x39, 0x5c, 0xdb, 0x0a, 0x77, 0x23, 0x1d, 0xc2, /* ff70-ff77 */
    0x39, 0x5c, 0xd1, 0x21, 0x8b, 0x5c, 0x06, 0x80, /* ff78-ff7f */
    0x7e, 0x12, 0x23, 0x13, 0x05, 0xc2, 0x4d, 0x5c, /* ff80-ff87 */
    0xc1, 0x21, 0x00, 0x5c, 0x7a, 0xbc, 0xc2, 0x60, /* ff88-ff8f */
    0x5c, 0x7b, 0xbd, 0xd2, 0x80, 0x5c, 0x04, 0x04, /* ff90-ff97 */
    0x78, 0xfe, 0x20, 0xda, 0x25, 0x5c, 0x06, 0x01, /* ff98-ff9f */
    0xca, 0x25, 0x5c, 0xdb, 0x08, 0xe6, 0x02, 0xc2, /* ffa0-ffa7 */
    0x70, 0x5c, 0x3e, 0x01, 0xd3, 0x09, 0x06, 0x00, /* ffa8-ffaf */
    0xc3, 0x25, 0x5c, 0x3e, 0x80, 0xd3, 0x08, 0xfb, /* ffb0-ffb7 */
    0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffb8-ffbf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffc0-ffc7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffc8-ffcf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffd0-ffd7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffd8-ffdf */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffe0-ffe7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ffe8-ffef */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fff0-fff7 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* fff8-ffff */
};

/* 88DSK Standard I/O Data Structures */

static UNIT dsk_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, MAX_DSK_SIZE) },
};

static REG dsk_reg[] = {
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

#define DSK_NAME    "Altair Floppy Disk"

static const char* dsk_description(DEVICE *dptr) {
    return DSK_NAME;
}

static MTAB dsk_mod[] = {
    { UNIT_DSK_WLK,     0,                  "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " DSK_NAME "n for writing" },
    { UNIT_DSK_WLK,     UNIT_DSK_WLK,       "WRTLCK",    "WRTLCK",  NULL, NULL, NULL,
        "Locks " DSK_NAME "n for writing" },
    { 0 }
};

/* Debug Flags */
static DEBTAB dsk_dt[] = {
    { "IN",             IN_MSG,             "IN operations"     },
    { "OUT",            OUT_MSG,            "OUT operations"    },
    { "READ",           READ_MSG,           "Read operations"   },
    { "WRITE",          WRITE_MSG,          "Write operations"  },
    { "SECTOR_STUCK",   SECTOR_STUCK_MSG,   "Sector stuck"      },
    { "TRACK_STUCK",    TRACK_STUCK_MSG,    "Track stuck"       },
    { "VERBOSE",        VERBOSE_MSG,        "Verbose messages"  },
    { NULL,             0                   }
};

DEVICE dsk_dev = {
    "DSK", dsk_unit, dsk_reg, dsk_mod,
    NUM_OF_DSK, 10, 31, 1, 8, 8,
    NULL, NULL, &dsk_reset,
    &dsk_boot, &dsk_attach, NULL,
    NULL, (DEV_DISABLE | DEV_DEBUG), 0,
    dsk_dt, NULL, NULL, NULL, NULL, NULL, &dsk_description
};

static const char* selectInOut(const int32 io) {
    return io == 0 ? "IN" : "OUT";
}

/* service routines to handle simulator functions */
/* reset routine */

static t_stat dsk_reset(DEVICE *dptr) {
    int32 i;
    for (i = 0; i < NUM_OF_DSK; i++) {
        warnLock[i]     = 0;
        warnAttached[i] = 0;
        current_track[i] = 0;
        current_sector[i] = 0;
        current_byte[i] = 0;
        current_flag[i] = 0;
    }
    warnDSK10       = 0;
    warnDSK11       = 0;
    warnDSK12       = 0;
    current_disk    = NUM_OF_DSK;
    in9_count       = 0;
    in9_message     = FALSE;
    sim_map_resource(0x08, 1, RESOURCE_TYPE_IO, &dsk10, "dsk10", dptr->flags & DEV_DIS);
    sim_map_resource(0x09, 1, RESOURCE_TYPE_IO, &dsk11, "dsk11", dptr->flags & DEV_DIS);
    sim_map_resource(0x0A, 1, RESOURCE_TYPE_IO, &dsk12, "dsk12", dptr->flags & DEV_DIS);
    return SCPE_OK;
}
/* dsk_attach - determine type of drive attached based on disk image size */

static t_stat dsk_attach(UNIT *uptr, CONST char *cptr) {
    int32 thisUnitIndex;
    int32 imageSize;
    const t_stat r = attach_unit(uptr, cptr);           /* attach unit  */
    if (r != SCPE_OK)                                   /* error?       */
        return r;

    ASSURE(uptr != NULL);
    thisUnitIndex = find_unit_index(uptr);
    ASSURE((0 <= thisUnitIndex) && (thisUnitIndex < NUM_OF_DSK));

    /*  If the file size is close to the mini-disk image size, set the number of
     tracks to 16, otherwise, 32 sectors per track. */

    imageSize = sim_fsize(uptr -> fileref);
    sectors_per_track[thisUnitIndex] = (((MINI_DISK_SIZE - MINI_DISK_DELTA < imageSize) &&
                                         (imageSize < MINI_DISK_SIZE + MINI_DISK_DELTA)) ?
                                        MINI_DISK_SECT : DSK_SECT);
    return SCPE_OK;
}

void install_ALTAIRbootROM(void) {
    const t_bool result = (install_bootrom(bootrom_dsk, BOOTROM_SIZE_DSK, ALTAIR_ROM_LOW, TRUE) ==
                           SCPE_OK);
    ASSURE(result);
}

/*  The boot routine modifies the boot ROM in such a way that subsequently
    the specified disk is used for boot purposes.
*/
static t_stat dsk_boot(int32 unitno, DEVICE *dptr) {
    if (cpu_unit.flags & (UNIT_CPU_ALTAIRROM | UNIT_CPU_BANKED)) {
        if (sectors_per_track[unitno] == MINI_DISK_SECT) {
            const t_bool result = (install_bootrom(alt_bootrom_dsk, BOOTROM_SIZE_DSK,
                                                   ALTAIR_ROM_LOW, TRUE) == SCPE_OK);
            ASSURE(result);
        } else {
        /* check whether we are really modifying an LD A,<> instruction */
            if ((bootrom_dsk[UNIT_NO_OFFSET_1 - 1] == LDA_INSTRUCTION) &&
                (bootrom_dsk[UNIT_NO_OFFSET_2 - 1] == LDA_INSTRUCTION)) {
            bootrom_dsk[UNIT_NO_OFFSET_1] = unitno & 0xff;             /* LD A,<unitno>        */
            bootrom_dsk[UNIT_NO_OFFSET_2] = 0x80 | (unitno & 0xff);    /* LD a,80h | <unitno>  */
        } else { /* Attempt to modify non LD A,<> instructions is refused. */
                sim_printf("Incorrect boot ROM offsets detected.\n");
            return SCPE_IERR;
        }
        install_ALTAIRbootROM();                                         /* install modified ROM */
    }
    }
    *((int32 *) sim_PC->loc) = ALTAIR_ROM_LOW;
    return SCPE_OK;
}

static int32 dskseek(const UNIT *xptr) {
    return sim_fseek(xptr -> fileref, DSK_SECTSIZE * sectors_per_track[current_disk] * current_track[current_disk] +
        DSK_SECTSIZE * current_sector[current_disk], SEEK_SET);
}

/* precondition: current_disk < NUM_OF_DSK */
static void writebuf(void) {
    int32 i, rtn;
    UNIT *uptr;
    i = current_byte[current_disk];         /* null-fill rest of sector if any */
    while (i < DSK_SECTSIZE)
        dskbuf[i++] = 0;
    uptr = dsk_dev.units + current_disk;
    if (((uptr -> flags) & UNIT_DSK_WLK) == 0) { /* write enabled */
        sim_debug(WRITE_MSG, &dsk_dev,
                  "DSK%i: " ADDRESS_FORMAT " OUT 0x0a (WRITE) D%d T%d S%d\n",
                  current_disk, PCX, current_disk,
                  current_track[current_disk], current_sector[current_disk]);
        if (dskseek(uptr)) {
            sim_debug(VERBOSE_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " fseek failed D%d T%d S%d\n",
                      current_disk, PCX, current_disk,
                      current_track[current_disk], current_sector[current_disk]);
        }
        rtn = sim_fwrite(dskbuf, 1, DSK_SECTSIZE, uptr -> fileref);
        if (rtn != DSK_SECTSIZE) {
            sim_debug(VERBOSE_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " sim_fwrite failed T%d S%d Return=%d\n",
                      current_disk, PCX, current_track[current_disk],
                      current_sector[current_disk], rtn);
        }
    } else if ( (dsk_dev.dctrl & VERBOSE_MSG) && (warnLock[current_disk] < warnLevelDSK) ) {
        /* write locked - print warning message if required */
        warnLock[current_disk]++;
        sim_debug(VERBOSE_MSG, &dsk_dev,
                  "DSK%i: " ADDRESS_FORMAT " Attempt to write to locked DSK%d - ignored.\n",
                  current_disk, PCX, current_disk);
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

int32 dsk10(const int32 port, const int32 io, const int32 data) {
    int32 current_disk_flags;
    in9_count = 0;
    if (io == 0) {                                      /* IN: return flags */
        if (current_disk >= NUM_OF_DSK) {
            if ((dsk_dev.dctrl & VERBOSE_MSG) && (warnDSK10 < warnLevelDSK)) {
                warnDSK10++;
                sim_debug(VERBOSE_MSG, &dsk_dev,
                          "DSK%i: " ADDRESS_FORMAT
                          " Attempt of IN 0x08 on unattached disk - ignored.\n",
                          current_disk, PCX);
            }
            return 0xff;                                /* no drive selected - can do nothing */
        }
        return (~current_flag[current_disk]) & 0xff;    /* return the COMPLEMENT! */
    }

    /* OUT: Controller set/reset/enable/disable */
    if (dirty)    /* implies that current_disk < NUM_OF_DSK */
        writebuf();
    sim_debug(OUT_MSG, &dsk_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x08: %x\n", current_disk, PCX, data);
    current_disk = data & NUM_OF_DSK_MASK; /* 0 <= current_disk < NUM_OF_DSK */
    current_disk_flags = (dsk_dev.units + current_disk) -> flags;
    if ((current_disk_flags & UNIT_ATT) == 0) { /* nothing attached? */
        if ( (dsk_dev.dctrl & VERBOSE_MSG) && (warnAttached[current_disk] < warnLevelDSK) ) {
            warnAttached[current_disk]++;
            sim_debug(VERBOSE_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt to select unattached DSK%d - ignored.\n",
                      current_disk, PCX, current_disk);
        }
        current_disk = NUM_OF_DSK;
    } else {
        current_sector[current_disk]    = 0xff; /* reset internal counters */
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

int32 dsk11(const int32 port, const int32 io, const int32 data) {
    if (current_disk >= NUM_OF_DSK) {
        if ((dsk_dev.dctrl & VERBOSE_MSG) && (warnDSK11 < warnLevelDSK)) {
            warnDSK11++;
            sim_debug(VERBOSE_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt of %s 0x09 on unattached disk - ignored.\n",
                      current_disk, PCX, selectInOut(io));
        }
        return 0xff;               /* no drive selected - can do nothing */
    }

    /* now current_disk < NUM_OF_DSK */
    if (io == 0) {  /* read sector position */
        in9_count++;
        if ((dsk_dev.dctrl & SECTOR_STUCK_MSG) && (in9_count > 2 * DSK_SECT) && (!in9_message)) {
            in9_message = TRUE;
            sim_debug(SECTOR_STUCK_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Looping on sector find.\n",
                      current_disk, PCX);
        }
        sim_debug(IN_MSG, &dsk_dev, "DSK%i: " ADDRESS_FORMAT " IN 0x09\n", current_disk, PCX);
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

    sim_debug(OUT_MSG, &dsk_dev, "DSK%i: " ADDRESS_FORMAT " OUT 0x09: %x\n", current_disk, PCX, data);
    if (data & 0x01) {      /* step head in                             */
        if (current_track[current_disk] == (tracks[current_disk] - 1)) {
            sim_debug(TRACK_STUCK_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Unnecessary step in.\n",
                   current_disk, PCX);
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
            sim_debug(TRACK_STUCK_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " Unnecessary step out.\n",
                      current_disk, PCX);
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

int32 dsk12(const int32 port, const int32 io, const int32 data) {
    int32 i, rtn;
    UNIT *uptr;

    if (current_disk >= NUM_OF_DSK) {
        if ((dsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
            warnDSK12++;
            sim_debug(VERBOSE_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT
                      " Attempt of %s 0x0a on unattached disk - ignored.\n",
                      current_disk, PCX, selectInOut(io));
        }
        return 0;
    }

    /* now current_disk < NUM_OF_DSK */
    in9_count = 0;
    uptr = dsk_dev.units + current_disk;
    if (io == 0) {
        if (current_byte[current_disk] >= DSK_SECTSIZE) {
            /* physically read the sector */
            sim_debug(READ_MSG, &dsk_dev,
                      "DSK%i: " ADDRESS_FORMAT " IN 0x0a (READ) D%d T%d S%d\n",
                      current_disk, PCX, current_disk,
                      current_track[current_disk], current_sector[current_disk]);
            for (i = 0; i < DSK_SECTSIZE; i++)
                dskbuf[i] = 0;
            if (dskseek(uptr)) {
                if ((dsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
                    warnDSK12++;
                    sim_debug(VERBOSE_MSG, &dsk_dev,
                              "DSK%i: " ADDRESS_FORMAT " fseek error D%d T%d S%d\n",
                              current_disk, PCX, current_disk,
                              current_track[current_disk], current_sector[current_disk]);
                }
            }
            rtn = sim_fread(dskbuf, 1, DSK_SECTSIZE, uptr -> fileref);
            if (rtn != DSK_SECTSIZE) {
                if ((dsk_dev.dctrl & VERBOSE_MSG) && (warnDSK12 < warnLevelDSK)) {
                    warnDSK12++;
                    sim_debug(VERBOSE_MSG, &dsk_dev,
                              "DSK%i: " ADDRESS_FORMAT " sim_fread error D%d T%d S%d\n",
                              current_disk, PCX, current_disk,
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
