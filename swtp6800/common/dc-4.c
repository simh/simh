/*  c: SWTP DC-4 FDC Simulator

    Copyright (c) 2005-2012, William A. Beech

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
        FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
        WILLIAM A BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not
        be used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        28 May 22 -- Roberto Sancho Villa (RSV) fixes for other disk formats
                     and operating systems.

    NOTES:

        The DC-4 is a 5-inch floppy controller which can control up
        to 4 daisy-chained 5-inch floppy drives.  The controller is based on
        the Western Digital 1797 Floppy Disk Controller (FDC) chip. This
        file only emulates the minimum DC-4 functionality to interface with
        the virtual disk file.

        The floppy controller is interfaced to the CPU by use of 5 memory 
        addreses.  These are SS-30 slot numbers 5 and 6 (0x8014-0x801B).

        Address     Mode    Function
        -------     ----    --------

        0x8014      Read    Returns FDC interrupt status
        0x8014      Write   Selects the drive/head/motor control
        0x8018      Read    Returns status of FDC
        0x8018      Write   FDC command register
        0x8019      Read    Returns FDC track register
        0x8019      Write   Set FDC track register
        0x801A      Read    Returns FDC sector register
        0x801A      Write   Set FDC sector register
        0x801B      Read    Read data
        0x801B      Write   Write data

        Drive Select Read (0x8014):

        +---+---+---+---+---+---+---+---+
        | I | D | X | X | X | X | X | X |
        +---+---+---+---+---+---+---+---+

        I = Set indicates an interrupt request from the FDC pending.
        D = DRQ pending - same as bit 1 of FDC status register.

        Drive Select Write (0x8014):

        +---+---+---+---+---+---+---+---+
        | M | S | X | X | X | X | Device|
        +---+---+---+---+---+---+---+---+

        M = If this bit is 1, the one-shot is triggered/retriggered to 
            start/keep the motors on.
        S = Side select. If set, side one is selected otherwise side zero
            is selected.
        X = not used
        Device = value 0 thru 3, selects drive 0-3 to be controlled.

        Drive Status Read (0x8018):

        +---+---+---+---+---+---+---+---+
        | R | P | H | S | C | L | D | B |
        +---+---+---+---+---+---+---+---+

        B - When 1, the controller is busy.
        D - When 1, index mark detected (type I) or data request - read data 
            ready/write data empty (type II or III).
        H - When 1, track 0 (type I) or lost data (type II or III).
        C - When 1, crc error detected.
        S - When 1, seek (type I) or RNF (type II or III) error.
        H - When 1, head is currently loaded (type I) or record type/
            write fault (type II or III).
        P - When 1, indicates that diskette is write-protected.
        R - When 1, drive is not ready.

        Drive Control Write (0x8018) for type I commands:

        +---+---+---+---+---+---+---+---+
        | 0 | S2| S1| S0| H | V | R1| R0|
        +---+---+---+---+---+---+---+---+

        R0/R1 - Selects the step rate.
        V - When 1, verify on destination track.
        H - When 1, loads head to drive surface.
        S0/S1/S2 = 000 - home.
           001 - seek track in data register.
           010 - step without updating track register.
           011 - step and update track register.
           100 - step in without updating track register.
           101 - step in and update track register.
           110 - step out without updating track register.
           111 - step out and update track register.

        Drive Control Write (0x8018) for type II commands:

        +---+---+---+---+---+---+---+---+
        | 1 | 0 | T | M | S | E | B | A |
        +---+---+---+---+---+---+---+---+

        A - Zero for read, 1 on write deleted data mark else data mark.
        B - When 1, shifts sector length field definitions one place.
        E - When, delay operation 15 ms, 0 no delay.
        S - When 1, select side 1, 0 select side 0.
        M - When 1, multiple records, 0 for single record.
        T - When 1, write command, 0 for read.

        Drive Control Write (0x8018) for type III commands:

        +---+---+---+---+---+---+---+---+
        | 1 | 1 | T0| T1| 0 | E | 0 | 0 |
        +---+---+---+---+---+---+---+---+

        E - When, delay operation 15 ms, 0 no delay.
        T0/T1 - 00 - read address command.
                10 - read track command.
                11 - write track command.

        Tracks are numbered from 0 up to one minus the last track in the 1797!

        Track Register Read (0x8019):

        +---+---+---+---+---+---+---+---+
        |          Track Number         |
        +---+---+---+---+---+---+---+---+

        Reads the current 8-bit value from the track position.

        Track Register Write (0x8019):

        +---+---+---+---+---+---+---+---+
        |          Track Number         |
        +---+---+---+---+---+---+---+---+

        Writes the 8-bit value to the track register.

        Sectors are numbers from 1 up to the last sector in the 1797!

        Sector Register Read (0x801A):

        +---+---+---+---+---+---+---+---+
        |         Sector Number         |
        +---+---+---+---+---+---+---+---+

        Reads the current 8-bit value from the sector position.

        Sector Register Write (0x801A):

        +---+---+---+---+---+---+---+---+
        |         Sector Number         |
        +---+---+---+---+---+---+---+---+

        Writes the 8-bit value to the sector register.

        Data Register Read (0x801B):

        +---+---+---+---+---+---+---+---+
        |             Data              |
        +---+---+---+---+---+---+---+---+

        Reads the current 8-bit value from the data register.

        Data Register Write (0x801B):

        +---+---+---+---+---+---+---+---+
        |             Data              |
        +---+---+---+---+---+---+---+---+

        Writes the 8-bit value to the data register.

        A FLEX disk is defined as follows:
        (first sector on track is number 1)

        Track   Sector  Use
        0       1       Boot sector
        0       2       Boot sector (cont)
        0       3       Unused
        0       4       System Identity Record (explained below)
        0       5       Unused
        0       6-last  Directory - 10 entries/sector (explained below)
        1       1       First available data sector
        last-1  last    Last available data sector

        System Identity Record (SIR)

        Byte    Use
        0x00    Two bytes of zeroes (Clears forward link)
        0x10    Volume name in ASCII(11 bytes)
        0x1B    Volume number in binary (2 bytes)
        0x1D    Address of first free data sector (Track-Sector) (2 bytes)
        0x1F    Address of last free data sector (Track-Sector) (2 bytes)
        0x21    Total number of data sectors in binary (2 bytes)
        0x23    Current date (Month-Day-Year) in binary
        0x26    Highest track number on disk in binary (byte)
        0x27    Highest sector number on a track in binary (byte)

        The following unit registers are used by this controller emulation:

        dsk_unit[cur_drv].u3        unit current flags
        dsk_unit[cur_drv].u4        unit current track
        dsk_unit[cur_drv].u5        unit current sector
        dsk_unit[cur_drv].pos       unit current sector byte index into buffer
        dsk_unit[cur_drv].filebuf   unit current sector buffer
        dsk_unit[cur_drv].fileref   unit current attached file reference
*/

#include <stdio.h>
#include "swtp_defs.h"

#define UNIT_V_ENABLE   (UNIT_V_UF + 0) /* Write Enable */
#define UNIT_ENABLE     (1 << UNIT_V_ENABLE)

/* emulate a SS FLEX disk with 72 sectors and 80 tracks */

#define NUM_DISK        4               /* standard 1797 maximum */
#define SECT_SIZE       256             /* standard FLEX sector */
#define NUM_SECT        36              //sectors/track
#define TRAK_SIZE       (SECT_SIZE * NUM_SECT) /* trk size (bytes) */
#define HEADS           2               //handle as DS
#define NUM_CYL         80              /* maximum cylinders */
#define DSK_SIZE        (NUM_SECT * HEADS * NUM_CYL * SECT_SIZE) /* dsk size (bytes) */

//SIR offsets TK 0, SEC 3

#define LABEL           0x10            //disk label (11 char)
#define VOLNUM          0x1B            //volume number (word)
#define FSTUSRTRK       0x1D            //first user track (byte)
#define FSTUSRSEC       0x1E            //first user sector (byte)
#define LSTUSRTRK       0x1F            //last user track (byte)
#define LSTUSRSEC       0x20            //last user sector (byte)
#define TOTSEC          0x21            //total sectors (word)
#define CREMON          0x23            //creation month (byte)
#define CREDAY          0x24            //creation day (byte)
#define CREYR           0x25            //creation year (byte)
#define MAXCYL          0x26            //last cylinder #
#define MAXSEC          0x27            //last sector #

/* 1797 status bits type I commands*/

#define NOTRDY          0x80
#define WRPROT          0x40
#define HEDLOD          0x20
#define SEEKERR         0x10
#define CRCERR          0x08
#define LOST            0x04
#define INDEX           0x02
#define BUSY            0x01

/* 1797 status bits type II/III commands*/

#define NOTRDY          0x80
#define WRPROT          0x40
#define WRTFALT         0x20
#define RECNF           0x10
#define CRCERR          0x08
#define LOST            0x04
#define DRQ             0x02
#define BUSY            0x01

/* function prototypes */

t_stat  dsk_reset (DEVICE *dptr);
t_stat  dsk_attach (UNIT *uptr, CONST char *cptr);

/* SS-50 I/O address space functions */

int32   fdcdrv(int32 io, int32 data);
int32   fdccmd(int32 io, int32 data);
int32   fdctrk(int32 io, int32 data);
int32   fdcsec(int32 io, int32 data);
int32   fdcdata(int32 io, int32 data);

/* Local Variables */

int32   fdcbyte;
int32   intrq;                          /* interrupt request flag */
int32   cur_dsk;                        /* Currently selected drive */
int32   wrt_flag;                       /* FDC write flag */

int32   spt;                            /* sectors/track */
int32   trksiz;                         /* trk size (bytes) */
int32   heds;                           /* number of heads */
int32   cpd;                            /* cylinders/disk */
int32   dsksiz;                         /* dsk size (bytes) */
int32   sectsize;                       // Sector size (bytes)

int32   multiple_sector;                // multiple read-write flag
int32   index_countdown;                // index countdown for type I commands
int32   sector_base;                    // indicates is first sector on track is sector 1 or sector 0

/* Floppy Disk Controller data structures

       dsk_dev        Disk Controller device descriptor
       dsk_unit       Disk Controller unit descriptor
       dsk_reg        Disk Controller register list
       dsk_mod        Disk Controller modifiers list
*/

UNIT dsk_unit[] = {
    { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
    { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
    { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
    { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  }
};

REG dsk_reg[] = {
    { HRDATA (DISK, cur_dsk, 4) },
    { NULL }
};

MTAB dsk_mod[] = {
    { UNIT_ENABLE, UNIT_ENABLE, "RW", "RW", NULL },
    { UNIT_ENABLE, 0, "RO", "RO", NULL },
    { 0 }
};

DEBTAB dsk_debug[] = {
    { "ALL", DEBUG_all, "All debug bits" },
    { "FLOW", DEBUG_flow, "Flow control" },
    { "READ", DEBUG_read, "Read Command" },
    { "WRITE", DEBUG_write, "Write Command"},
    { NULL }
};

DEVICE dsk_dev = {
    "DC-4",                             //name
    dsk_unit,                           //units
    dsk_reg,                            //registers
    dsk_mod,                            //modifiers
    NUM_DISK,                           //numunits
    16,                                 //aradix
    16,                                 //awidth
    1,                                  //aincr
    16,                                 //dradix
    8,                                  //dwidth
    NULL,                               //examine
    NULL,                               //deposit
    &dsk_reset,                         //reset
    NULL,                               //boot
    dsk_attach,                         //attach
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DISABLE|DEV_DEBUG,              //flags
    0,                                  //dctrl
    dsk_debug,                          //debflags
    NULL,                               //msize
    NULL                                //lname
};

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
    uint32 i;

    cur_dsk = 5;                        /* force initial SIR read */
    for (i=0; i<dptr->numunits; i++) {
        dptr->units[i].u3 = NOTRDY;     /* current flags = NOTRDY*/
        dptr->units[i].u4 = 0;          /* clear current cylinder # */
        dptr->units[i].u5 = 0;          /* clear current sector # */
        dptr->units[i].pos = 0;         /* clear current byte ptr */
        if (dptr->units[i].filebuf == NULL) {
            dptr->units[i].filebuf = calloc(SECT_SIZE, sizeof(uint8)); /* allocate buffer */
            if (dptr->units[i].filebuf == NULL) {
                printf("dc-4_reset: Calloc error\n");
                return SCPE_MEM;
            }
        }
    }
    spt = 0;
    trksiz = 0;
    heds = 0;
    cpd = 0;
    dsksiz = 0;
    //RSV - for hansling multiple disk formats and OSs
    sectsize = SECT_SIZE; 
    multiple_sector=0;
    index_countdown=0;
    sector_base=1; 
    return SCPE_OK;
}

/* dsk attach - attach an .IMG file to an FDD */

t_stat dsk_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("dsk_attach: Attach error %d\n", r);
        return r;
    }
    uptr->u3 &= ~NOTRDY;  //reset FDD to ready
    uptr->capac = sim_fsize(uptr->fileref); //file size
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the MP-B2 module when a
   read or write occur to addresses 0x8014-0x801B. */

/* DC-4 drive select register routine - this register is not part of the 1797
*/

int32 fdcdrv(int32 io, int32 data)
{
    static long pos;
    static int32 err;
    uint8  *SIR; 
    int32  disk_image_size; 

    if (io) {                           // write to DC-4 drive register
        if (cur_dsk == (data & 0x03))   // already selected?
            return 0;                   //yes                   
        cur_dsk = data & 0x03;          // only 2 drive select bits/
        dsk_unit[cur_dsk].u3 &= ~LOST;  //RSV - reset LOST flag
        if ((dsk_unit[cur_dsk].flags & UNIT_ENABLE) == 0) //RO?
            dsk_unit[cur_dsk].u3 |= WRPROT; /* Set WPROT */
        else
            dsk_unit[cur_dsk].u3 &= ~WRPROT; /* SET not WPROT */
        if (dsk_unit[cur_dsk].fileref == 0) // no file attached
            return 0;
        /* RSV - Read in SIR */
        pos = 0x200;                    
        sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to SIR */
        sim_fread(dsk_unit[cur_dsk].filebuf, SECT_SIZE, 1, dsk_unit[cur_dsk].fileref); /* read in SIR */
        dsk_unit[cur_dsk].u3 |= BUSY | DRQ; /* set DRQ & BUSY */
        dsk_unit[cur_dsk].pos = 0;      /* clear counter */
        SIR = (uint8 * )(dsk_unit[cur_dsk].filebuf); 
        // detect disk type based on image geometry or SIR record
        disk_image_size = sim_fsize(dsk_unit[cur_dsk].fileref); //get actual file size
        if (disk_image_size == 35 * 10 * 256) { // 89600 bytes -> FDOS image
            // FDOS disc has no SIR record. 
            spt = 10;                   // 10 sectors
            cpd = 35;                   // 35 tracks
            sectsize = 256; 
            sector_base = 0;            // first sector in track is number ZERO
        } else if (disk_image_size == 35 * 18 * 128) { // 80640 bytes -> FLEX 1.0 image
            spt = 18;                   // 18 sectors
            cpd = 35;                   // 35 tracks
            sectsize = 128; 
            sector_base = 1;            // first sector in track is number ONE
        } else if ((SIR[0] == 0) && (SIR[1] == 0)) {
            // FLEX disc has SIR record. on disk image offset $200
            spt = SIR[MAXSEC];          // Highest number of tracks. As in FLEX sectors are numbered as 
                                        // 1,2,..Hi this is also the number of sectors per track
            cpd = SIR[MAXCYL] + 1;      // highest track number . On FLEX, first track is track zero
            sectsize = 256; 
            sector_base = 1;            // first sector in track is number ONE
        } else {
            spt = 18; 
            sectsize = 128; 
            cpd = disk_image_size / (spt * sectsize); 
            sector_base = 1;            // first sector in track is number ONE
        }
        heds = 0;                       //RSV - always SS
        trksiz = spt * sectsize;
        dsksiz = trksiz * cpd;

        return 0;
    } else {                            /* read from DC-4 drive register */
        return intrq;
    }
}

/* WD 1797 FDC command register routine */

int32 fdccmd(int32 io, int32 data)
{
    static int32 val = 0, val1 = NOTRDY;
    static long pos;
    static int32 err;

    if ((dsk_unit[cur_dsk].flags & UNIT_ATT) == 0) { /* not attached */
        val = dsk_unit[cur_dsk].u3 |= NOTRDY; /* set not ready flag */
        return SEEKERR;                 // RSV - return SEEK ERROR STATUS bit
    } else {
        dsk_unit[cur_dsk].u3 &= ~(NOTRDY); /* clear not ready flag */
    }
    if (io) {                           /* write command to fdc */
        // RSV - on commands type I ...
        if ((data & 0x80) == 0) {
            // ... set bits h V r1r0 to h=1 (home drive), V=0 (verify off), r1r0=11 (40msec track stepping)
            data = ((data & 0xF0) | 0x0B); 
            // and starts countdown for index status bit
            index_countdown = 10; 
        } else {
            index_countdown = 0; 
        }
        // process command
        switch(data) {
            case 0x8C:                  //read sector command type II
            case 0x9C:                  //read multiple sectors command type II
                if ((dsk_unit[cur_dsk].u5 - sector_base >= spt) || (dsk_unit[cur_dsk].u5 < sector_base)) {
                    dsk_unit[cur_dsk].u3 |= RECNF; /* RSV - set RECORD NOT FOUND */
                    break; 
                }
                dsk_unit[cur_dsk].u3 |= BUSY; /* set BUSY */
                pos = trksiz * dsk_unit[cur_dsk].u4; /* calculate file offset */
                pos += sectsize * (dsk_unit[cur_dsk].u5 - sector_base);
                err = sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to offset */
                if (err) {
                    sim_printf("fdccmd: Seek error in read command\n");
                    return SCPE_IOERR;
                }
                /* read in buffer */
                err = sim_fread(dsk_unit[cur_dsk].filebuf, sectsize, 1, dsk_unit[cur_dsk].fileref); 
                if (err != 1) {
                    sim_printf("fdccmd: File error in read command\n");
                    return SCPE_IOERR;
                }
                dsk_unit[cur_dsk].u3 |= DRQ; /* set DRQ */
                dsk_unit[cur_dsk].pos = 0; /* clear counter */
                multiple_sector = (data == 0x9C) ? 1:0; // RSV = set multiple sector TYPE II cmds
                break;
            case 0xAC:                  //write command type II
            case 0xBC:                  //write multiple sectors command type II
                if (dsk_unit[cur_dsk].u3 & WRPROT) {
                } else {
                    dsk_unit[cur_dsk].u3 |= BUSY;/* set BUSY */
                    pos = trksiz * dsk_unit[cur_dsk].u4; /* calculate file offset */
                    pos += sectsize * (dsk_unit[cur_dsk].u5 - sector_base);
                    err = sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to offset */
                    if (err) {
                        sim_printf("fdccmd: Seek error in write command\n");
                        return SCPE_IOERR;
                    } 
                    dsk_unit[cur_dsk].u3 |= DRQ;/* set DRQ */
                    wrt_flag = 1;       /* RSV - set write flag */
                    dsk_unit[cur_dsk].pos = 0; /* clear counter */
                }
                break;
            case 0x1B:                  //seek command type I
                dsk_unit[cur_dsk].u4 = fdcbyte;      /* set track */
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                break;
            case 0x0B:                  //restore command type I  
                dsk_unit[cur_dsk].u4 = 0;                /* home the drive */
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ | RECNF); /* clear flags */
                break;
            case 0xF0:                  //write track command type III
            case 0xF4:                  //write track command type III
                break;
            case 0xD0:                  //Force Interrupt - terminate current command
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                break;
            default:
                sim_printf("Unknown FDC command %02X\n\r", data);
        }
    } else {                            /* read status from fdc */
        val = dsk_unit[cur_dsk].u3;     /* set return value */
        if (index_countdown) {          // RSV - Handle INDEX flag
            index_countdown--;
            // if index countdoen expires, set index flag in status returned to cpu
            if (index_countdown==0) val |= INDEX; 
        }
    }
    return val;
}

/* WD 1797 FDC track register routine */

int32 fdctrk(int32 io, int32 data)
{
    if (io) {
        dsk_unit[cur_dsk].u3 &= ~(RECNF); /* reset RECNF flag */
        dsk_unit[cur_dsk].u4 = data & BYTEMASK;
    }
    return dsk_unit[cur_dsk].u4;
}

/* WD 1797 FDC sector register routine */

int32 fdcsec(int32 io, int32 data)
{
    if (io) {
        dsk_unit[cur_dsk].u3 &= ~(RECNF); /* reset RECNF flag */
        dsk_unit[cur_dsk].u5 = data & BYTEMASK;
        if (dsk_unit[cur_dsk].u5 < sector_base) //RSV - force to sector 1 
            dsk_unit[cur_dsk].u5 = sector_base; 
        return 0; 
    }
    return dsk_unit[cur_dsk].u5;
}

/* WD 1797 FDC data register routine */

int32 fdcdata(int32 io, int32 data)
{
    int32 val, err;

    if (cur_dsk >= NUM_DISK)            // RSV - illegal disk 
        return 0; 
    if (io) {                           /* write byte to fdc */
        fdcbyte = data;                 /* save for seek */
        if (dsk_unit[cur_dsk].pos < (t_addr) sectsize) { /* copy bytes to buffer */
            *((uint8 *)(dsk_unit[cur_dsk].filebuf) + dsk_unit[cur_dsk].pos) = data; /* byte into buffer */
            dsk_unit[cur_dsk].pos++;    /* step counter */
            if (dsk_unit[cur_dsk].pos == sectsize) {
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ);
                if (wrt_flag) {         /* if initiated by FDC write command */
                    sim_fwrite(dsk_unit[cur_dsk].filebuf, sectsize, 1, dsk_unit[cur_dsk].fileref); /* write it */
                    wrt_flag = 0;       /* clear write flag */
                }
            }
        }
        return 0;
    } else {                            /* read byte from fdc */
        if (dsk_unit[cur_dsk].pos < (t_addr) sectsize) { /* copy bytes from buffer */
            val = *((uint8 *)(dsk_unit[cur_dsk].filebuf) + dsk_unit[cur_dsk].pos) & BYTEMASK;
            dsk_unit[cur_dsk].pos++;    /* step counter */
            if (dsk_unit[cur_dsk].pos == sectsize) { // sector finished
                if ((multiple_sector) && (dsk_unit[cur_dsk].u5-sector_base < spt-1)) { // read multiple in progress
                    dsk_unit[cur_dsk].u5++;
                    err = sim_fread(dsk_unit[cur_dsk].filebuf, sectsize, 1, dsk_unit[cur_dsk].fileref); /* read in buffer */
                    if (err != 1) {
                        sim_printf("fdccmd: File error in read command\n");
                        return SCPE_IOERR;
                    }
                    dsk_unit[cur_dsk].pos = 0; 
                } else { // RSV - handle multiple sector disk read
                    dsk_unit[cur_dsk].u5++;
                    dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                    if (multiple_sector) {
                        multiple_sector=0;
                    }
                }
            }
            return val;
        } else
            return 0;
    }
}

/* end of dc-4.c */
