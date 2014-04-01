/*  altairz80_mhdsk.c: MITS 88-HDSK Hard Disk simulator

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

------------------------------------------------------------------------------

    The 88-HDSK from MITS/Pertec consists of a 5mb removable platter and
    a fixed 5mb platter. Each platter is double sided. Head 0 and 1 are the
    top and bottom surface of the removable platter and head 2 and 3 are the
    top and bottom surface of the fixed platter. Hard disk BASIC treats the
    two platters as two separate drives. Each platter has 406 cylinders
    with 24 sectors per track and 256 bytes per sector.

    The disk image file starts with head 0, track 0, sector 0 (0,0,0) through 
    (0,0,23), followed by head 1, track 0, sector 0 (1,0,0) through (1,0,23).
    The pattern then repeats starting with (0,1,0).

    The external hard disk is accessed through eight ports of a 4-PIO card 
    at I/O addresses A0h-A7h. 

    Written by Mike Douglas March, 2014
    Disk images provided by Martin Eberhard

-------------------------------------------------------------------------------
*/
#include "altairz80_defs.h"
#include <assert.h>

/**  Typedefs & Defines  **************************************/
#define HDSK_SECTOR_SIZE        256             /* size of sector */
#define HDSK_SECTORS_PER_TRACK  24              /* sectors per track */
#define HDSK_NUM_HEADS          2               /* heads per disk */
#define HDSK_NUM_TRACKS         406             /* tracks per surface */
#define HDSK_TRACK_SIZE         (HDSK_SECTOR_SIZE * HDSK_SECTORS_PER_TRACK)
#define HDSK_CYLINDER_SIZE      (HDSK_TRACK_SIZE * 2)
#define HDSK_CAPACITY           (HDSK_CYLINDER_SIZE * HDSK_NUM_TRACKS)  
#define HDSK_NUMBER             8               /* number of hard disks */
#define IO_IN                   0               /* I/O operation is input */
#define IO_OUT                  1               /* I/O operation is output */
#define UNIT_V_DSK_WLK          (UNIT_V_UF + 0)         /* write locked  */
#define UNIT_DSK_WLK            (1 << UNIT_V_DSK_WLK)

/* boot related */
#define BOOTROM_SIZE_MHDSK      256
#define MHDSK_BOOT_ADDRESS      0xfc00
static t_stat mhdsk_boot(int32 unitno, DEVICE *dptr);
extern t_stat install_bootrom(int32 bootrom[], int32 size, int32 addr, int32 makeROM);

// Disk controller commands are in upper nibble of command high byte.

#define CMD_SHIFT       4       // shift right 4 places
#define CMD_MASK        0x0f    // mask after shifting
#define CMD_SEEK        0       // seek to track
#define CMD_WRITE_SEC   2       // write sector from buf n
#define CMD_READ_SEC    3       // read sector into buf n
#define CMD_WRITE_BUF   4       // load buffer n from CPU
#define CMD_READ_BUF    5       // read buffer n into CPU
#define CMD_READ_STATUS 6       // read controller IV byte
#define CMD_SET_IV_BYTE 8       // set controller IV byte
#define CMD_READ_UNFMT  10      // read unformatted sector
#define CMD_FORMAT      12
#define CMD_INITIALIZE  14

// Other disk controller bit fields

#define UNIT_SHIFT      2       // shift right 2 places
#define UNIT_MASK       0x03    // mask after shifting

#define BUFFER_MASK     0x03    // mask - no shift needed

#define TRACK_SHIFTH    8       // shift left 8 places into MSbyte
#define TRACK_MASKH     0x01    // msb of track number 
#define TRACK_MASKL     0xff    // entire lsb of track number

#define HEAD_SHIFT      5       // shift right 5 places
#define HEAD_MASK       0x03    // mask after shifting (no heads 4-7)

#define SECTOR_MASK     0x1f    // mask - no shift needed

// Command status equates

#define CSTAT_WRITE_PROTECT     0x80    // disk is write protected
#define CSTAT_NOT_READY         0x01    // drive not ready
#define CSTAT_BAD_SECTOR        0x02    // invalid sector number


/**  Module Globals - Private  ********************************/
static uint32 selectedDisk = 0;         // current active disk
static uint32 selectedSector = 0;       // current sector
static uint32 selectedTrack = 0;        // current track
static uint32 selectedHead = 0;         // current head
static uint32 selectedBuffer = 0;       // current buffer # in use
static uint32 bufferIdx = 0;            // current index into selected buffer
static uint32 maxBufferIdx = 256;       // maximum buffer index allowed
static uint32 cmdLowByte = 0;           // low byte of command

//  Controller status bytes

static uint8 cstat = 0;         // command status from controller

// The hard disk controller support four 256 byte disk buffers */

static uint8 diskBuf1[HDSK_SECTOR_SIZE];
static uint8 diskBuf2[HDSK_SECTOR_SIZE];
static uint8 diskBuf3[HDSK_SECTOR_SIZE];
static uint8 diskBuf4[HDSK_SECTOR_SIZE];
static uint8 *diskBuf[] = { diskBuf1, diskBuf2, diskBuf3, diskBuf4 };

/**  Forward and external Prototypes  **************************************/

static int32 hdReturnReady(const int32 port, const int32 io, const int32 data);
static int32 hdCstat(const int32 port, const int32 io, const int32 data);
static int32 hdAcmd(const int32 port, const int32 io, const int32 data);
static int32 hdCdata(const int32 port, const int32 io, const int32 data);
static int32 hdAdata(const int32 port, const int32 io, const int32 data);
static void doRead(void);
static void doWrite(void);
static t_stat dsk_reset(DEVICE *dptr);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

/* 88DSK Standard I/O Data Structures */

static UNIT dsk_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) },
    { UDATA (NULL, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, HDSK_CAPACITY) }};

static MTAB dsk_mod[] = {
    { UNIT_DSK_WLK,     0,                  "WRTENB",    "WRTENB",  NULL                },
    { UNIT_DSK_WLK,     UNIT_DSK_WLK,       "WRTLCK",    "WRTLCK",  NULL                },
    { 0 }
};

DEVICE mhdsk_dev = {
    "MHDSK", dsk_unit, NULL, dsk_mod,
    HDSK_NUMBER, 10, 31, 1, 8, 8,
    NULL, NULL, &dsk_reset,
    &mhdsk_boot, NULL, NULL,
    NULL, (DEV_DISABLE | DEV_DEBUG), 0,
    NULL, NULL, "MITS Hard Disk MHDSK"
};

static int32 bootrom_mhdsk[BOOTROM_SIZE_MHDSK] = {
    0xf3, 0x31, 0x00, 0xf8, 0x21, 0x1b, 0x41, 0x2b, /* fc00-fc07 */
    0x7c, 0xb5, 0xc2, 0x07, 0xfc, 0xe5, 0xd3, 0xa0, /* fc08-fc0f */
    0xd3, 0xa2, 0xd3, 0xa4, 0xd3, 0xa6, 0xd3, 0xa1, /* fc10-fc17 */
    0xd3, 0xa5, 0x2f, 0xd3, 0xa3, 0xd3, 0xa7, 0x3e, /* fc18-fc1f */
    0x2c, 0xd3, 0xa0, 0xd3, 0xa4, 0xd3, 0xa6, 0x3e, /* fc20-fc27 */
    0x24, 0xd3, 0xa2, 0xdb, 0xa1, 0x3e, 0x03, 0xd3, /* fc28-fc2f */
    0x10, 0x3e, 0x11, 0xd3, 0x10, 0xcd, 0xe5, 0xfc, /* fc30-fc37 */
    0x0d, 0x0a, 0x48, 0x44, 0x42, 0x4c, 0x20, 0x31, /* fc38-fc3f */
    0x2e, 0x30, 0xb1, 0xcd, 0x77, 0xfc, 0x11, 0x2c, /* fc40-fc47 */
    0x00, 0x7a, 0xbb, 0xdb, 0xa5, 0xd2, 0x54, 0xfc, /* fc48-fc4f */
    0x6c, 0x61, 0x48, 0x47, 0x14, 0xc2, 0x49, 0xfc, /* fc50-fc57 */
    0xcd, 0xe5, 0xfc, 0x0d, 0x0a, 0x4c, 0x4f, 0x41, /* fc58-fc5f */
    0x44, 0x49, 0x4e, 0xc7, 0xd1, 0xd5, 0xcd, 0x77, /* fc60-fc67 */
    0xfc, 0xdb, 0xa5, 0x12, 0x13, 0x05, 0xc2, 0x69, /* fc68-fc6f */
    0xfc, 0x23, 0x0d, 0xc2, 0x66, 0xfc, 0xc9, 0xe5, /* fc70-fc77 */
    0xd5, 0xc5, 0x01, 0xd0, 0xff, 0x11, 0xff, 0xff, /* fc78-fc7f */
    0x13, 0x09, 0xda, 0x80, 0xfc, 0x7d, 0xc6, 0x30, /* fc80-fc87 */
    0xeb, 0xfe, 0x18, 0xda, 0x90, 0xfc, 0xc6, 0x08, /* fc88-fc8f */
    0x47, 0xcd, 0xaf, 0xfc, 0x26, 0x30, 0xdb, 0xff, /* fc90-fc97 */
    0xe6, 0x03, 0x0f, 0x0f, 0xb0, 0xcd, 0xb0, 0xfc, /* fc98-fc9f */
    0xdb, 0xa5, 0xdb, 0xa3, 0xaf, 0xd3, 0xa7, 0x3e, /* fca0-fca7 */
    0x50, 0xd3, 0xa3, 0xc1, 0xd1, 0xe1, 0xc9, 0x7d, /* fca8-fcaf */
    0xd3, 0xa7, 0xdb, 0xa1, 0xdb, 0xa3, 0xdb, 0xff, /* fcb0-fcb7 */
    0xe6, 0x00, 0xb4, 0xd3, 0xa3, 0xdb, 0xa0, 0x07, /* fcb8-fcbf */
    0xd2, 0xbd, 0xfc, 0xdb, 0xa1, 0xe6, 0x7f, 0xc8, /* fcc0-fcc7 */
    0xfb, 0xf5, 0xcd, 0xe5, 0xfc, 0x0d, 0x0a, 0x4c, /* fcc8-fccf */
    0x4f, 0x41, 0x44, 0x20, 0x45, 0x52, 0x52, 0x4f, /* fcd0-fcd7 */
    0x52, 0xba, 0x21, 0x00, 0xfd, 0x34, 0xca, 0xde, /* fcd8-fcdf */
    0xfc, 0xe3, 0xc3, 0xcf, 0xfd, 0xe3, 0xdb, 0x10, /* fce0-fce7 */
    0xe6, 0x02, 0xca, 0xe6, 0xfc, 0x7e, 0xe6, 0x7f, /* fce8-fcef */
    0xd3, 0x11, 0xbe, 0x23, 0xca, 0xe6, 0xfc, 0xe3, /* fcf0-fcf7 */
    0xc9, 0x70, 0x4a, 0x01, 0x00, 0xd4, 0xb4, 0x13, /* fcf8-fcff */
};

static t_stat mhdsk_boot(int32 unitno, DEVICE *dptr) {
    const t_bool installSuccessful = (install_bootrom(bootrom_mhdsk, BOOTROM_SIZE_MHDSK,
                                                      MHDSK_BOOT_ADDRESS, FALSE) == SCPE_OK);
    assert(installSuccessful);
    *((int32 *) sim_PC -> loc) = MHDSK_BOOT_ADDRESS;
    return SCPE_OK;
}

/*----------------------------------------------------------------------------------
 
 dsk_reset - install I/O handlers and initialize variables.
 
 ----------------------------------------------------------------------------------*/

static t_stat dsk_reset(DEVICE *dptr) {
    sim_map_resource(0xA0, 1, RESOURCE_TYPE_IO, &hdReturnReady, dptr->flags & DEV_DIS);
    sim_map_resource(0xA1, 1, RESOURCE_TYPE_IO, &hdCstat, dptr->flags & DEV_DIS);
    sim_map_resource(0xA2, 1, RESOURCE_TYPE_IO, &hdReturnReady, dptr->flags & DEV_DIS);
    sim_map_resource(0xA3, 1, RESOURCE_TYPE_IO, &hdAcmd, dptr->flags & DEV_DIS);
    sim_map_resource(0xA4, 1, RESOURCE_TYPE_IO, &hdReturnReady, dptr->flags & DEV_DIS);
    sim_map_resource(0xA5, 1, RESOURCE_TYPE_IO, &hdCdata, dptr->flags & DEV_DIS);
    sim_map_resource(0xA6, 1, RESOURCE_TYPE_IO, &hdReturnReady, dptr->flags & DEV_DIS);
    sim_map_resource(0xA7, 1, RESOURCE_TYPE_IO, &hdAdata, dptr->flags & DEV_DIS);

    selectedSector = 0;         // current sector
    selectedTrack = 0;          // current track
    selectedHead = 0;           // current head
    selectedBuffer = 0;         // current buffer # in use
    bufferIdx = 0;              // current index into selected buffer
    maxBufferIdx = 256;         // maximum buffer index allowed
    cmdLowByte = 0;             // low byte of command

    return SCPE_OK;
}

/*-------------------------------------------------------------------------------------
 hdReturnReady - common I/O handler for several hard disk status ports which set 
    bit 7 when the corresponding hard disk function is ready. In the emulator,
    we're always ready for the next step, so we simply return ready all the time.

    0xA0 - CREADY register. Accessed through the status/control register of 4-PIO
        port 1-A. Returns the "ready for command" status byte.

    0xA2 - ACSTA register. Accessed through the status/control register of 4-PIO
        port 1-B. Returns the "command received" status byte.
    
    0xA4 - CDSTA register. Accessed through the status/control register of 4-PIO 
        port 2-A. Returns the "command data available" status byte.

    0xA6 - ADSTA register. Accessed through the status/control register of 4-PIO 
        port 2-B. Returns the "available to write" status byte.
        
---------------------------------------------------------------------------------------*/
static int32 hdReturnReady(const int32 port, const int32 io, const int32 data)
{
    return(0x80);       // always indicate ready

// output operations have no effect
}

/*------------------------------------------------------------
 hdCstat (0xA1) CSTAT register. Accessed through the 
    data register of 4-PIO port 1-A.

    Comments:   Returns error code byte of the most recent 
                operation. Reading this byte also clears
                the CRDY bit, but this isn't actually done
                in the emulation since we're always ready.
-------------------------------------------------------------*/
static int32 hdCstat(const int32 port, const int32 io, const int32 data)
{
    return(cstat);

// output operations have no effect
}

/*------------------------------------------------------------
 hdAcmd (0xA3) ACMD register. Accessed  through the 
    data register of 4-PIO port 1-B.

    Comments:   The high byte of a command is written to
                this register and initiates the command.
                The low byte of a command is assumed to
                have already been written and stored in
                cmdLowByte;
-------------------------------------------------------------*/
static int32 hdAcmd(const int32 port, const int32 io, const int32 data)
{
    uint32 command;             // command field from command msb
    uint32 unit;                // unit number from command msb
    uint32 buffer;              // buffer number from command msb

// if not an OUT command, exit

    if (io != IO_OUT)
        return(0);

// extract command and possible unit and buffer fields.

    cstat = 0;                  // assume command success
    command = (data >> CMD_SHIFT) & CMD_MASK;
    unit = (data >> UNIT_SHIFT) & UNIT_MASK;
    buffer = data & BUFFER_MASK;

// SEEK command. Updated selectedTrack.

    if (command == CMD_SEEK) {
        selectedTrack = cmdLowByte + ((data & TRACK_MASKH) << TRACK_SHIFTH);
        if (selectedTrack >= HDSK_NUM_TRACKS)
            selectedTrack = HDSK_NUM_TRACKS-1;
    }

// READ, READ UNFORMATTED or WRITE SECTOR command.

    else if ((command==CMD_WRITE_SEC) || (command==CMD_READ_SEC) || (command==CMD_READ_UNFMT)) {
        selectedHead = (cmdLowByte >> HEAD_SHIFT) & HEAD_MASK;
        selectedDisk = (selectedHead >> 1) + unit * 2 ;
        selectedSector = cmdLowByte & SECTOR_MASK;
        selectedBuffer = buffer;
        if (mhdsk_dev.units[selectedDisk].fileref == NULL)      // make sure a file is attached
            cstat = CSTAT_NOT_READY;
        else {
            if (command == CMD_WRITE_SEC)
                doWrite();
            else
                doRead();
        }
    }

// READ or WRITE BUFFER command. Initiates reading/loading specified buffer. 

    else if ((command == CMD_WRITE_BUF) || (command == CMD_READ_BUF)) {
        selectedBuffer = buffer;
        maxBufferIdx = cmdLowByte;
        if (maxBufferIdx == 0) 
            maxBufferIdx = 256;
        bufferIdx = 0;
    }

// READ STATUS command (read IV byte)

    else if (command == CMD_READ_STATUS) {
    }

// SET IV byte command

    else if (command == CMD_SET_IV_BYTE) {
    }

// FORMAT command

    else if (command == CMD_FORMAT) {
    }

// INITIALIZE command

    else if (command == CMD_INITIALIZE) {
    }

    return(0);
}

/*------------------------------------------------------------
 hdCdata (0xA5) Cdata register. Accessed through the 
    data register of 4-PIO port 1-B.
    
    Comments:   Returns data from the read buffer
-------------------------------------------------------------*/
static int32 hdCdata(const int32 port, const int32 io, const int32 data)
{
    if (io == IO_IN) {
        if (bufferIdx < maxBufferIdx)
            return(diskBuf[selectedBuffer][bufferIdx++]);
    }
    return(0);

// output operations have no effect
}


/*------------------------------------------------------------
 hdAdata (0xA7) ADATA register. Accessed through the 
    data register of 4-PIO port 2-B.
    
    Comments:   Accepts data into the current buffer 
                and is also the low byte of a command.
-------------------------------------------------------------*/
static int32 hdAdata(const int32 port, const int32 io, const int32 data)
{
    if (io == IO_OUT) {
        cmdLowByte = data & 0xff;
        if (bufferIdx < maxBufferIdx)
            diskBuf[selectedBuffer][bufferIdx++] = data;
    }
    return(0);
}

/*--  doRead  -------------------------------------------------
    Performs read from MITS Hard Disk image file
    
    Params:     nothing
    Uses:       selectedTrack, selectedHead, selectedSector
                selectedDisk, diskBuf[], mhdsk_dev
    Returns:    nothing (updates cstat directly)
    Comments:   
-------------------------------------------------------------*/
static void doRead(void)
{
    UNIT *uptr;
    uint32 fileOffset;
    
    uptr = mhdsk_dev.units + selectedDisk;
    fileOffset = HDSK_CYLINDER_SIZE * selectedTrack + 
                    HDSK_TRACK_SIZE * (selectedHead & 0x01) + 
                    HDSK_SECTOR_SIZE * selectedSector;
    if (sim_fseek(uptr->fileref, fileOffset, SEEK_SET))
        cstat = CSTAT_NOT_READY;                    /* seek error */
    else if (sim_fread(diskBuf[selectedBuffer], 1, HDSK_SECTOR_SIZE, uptr->fileref) != HDSK_SECTOR_SIZE)
        cstat = CSTAT_NOT_READY;                    /* write error */
}


/*--  doWrite  ------------------------------------------------
    Performs write to MITS Hard Disk image file
    
    Params:     none
    Uses:       selectedTrack, selectedHead, selectedSector
                selectedDisk, diskBuf[], mhdsk_dev
    Returns:    nothing (updates cstat directly)
    Comments:   
-------------------------------------------------------------*/
static void doWrite(void)
{
    UNIT *uptr;
    uint32 fileOffset;
        
    uptr = mhdsk_dev.units + selectedDisk;
    if (((uptr->flags) & UNIT_DSK_WLK) == 0) {          /* write enabled */
        fileOffset = HDSK_CYLINDER_SIZE * selectedTrack + 
                        HDSK_TRACK_SIZE * (selectedHead & 0x01) + 
                        HDSK_SECTOR_SIZE * selectedSector;
        if (sim_fseek(uptr->fileref, fileOffset, SEEK_SET))
            cstat = CSTAT_NOT_READY;                    /* seek error */
        else if (sim_fwrite(diskBuf[selectedBuffer], 1, HDSK_SECTOR_SIZE, uptr->fileref) != HDSK_SECTOR_SIZE)
            cstat = CSTAT_NOT_READY;                    /* write error */
    }
    else
        cstat = CSTAT_WRITE_PROTECT;
}
