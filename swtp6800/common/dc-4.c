/*  dc4.c: SWTP DC-4 FDC Simulator

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

        23 Apr 15 -- Modified to use simh_debug

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

        Track   Sector  Use
        0       1       Boot sector
        0       2       Boot sector (cont)
        0       3       Unused
        0       4       System Identity Record (explained below)
        0       5       Unused
        0       6-last  Directory - 10 entries/sector (explained below)
        1       1       First available data sector
        last-1  last    Last available data sector

        System Identity Record

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

#define DEBUG   0

#define UNIT_V_ENABLE   (UNIT_V_UF + 0) /* Write Enable */
#define UNIT_ENABLE     (1 << UNIT_V_ENABLE)

/* emulate a SS FLEX disk with 72 sectors and 80 tracks */

#define NUM_DISK        4               /* standard 1797 maximum */
#define SECT_SIZE       256             /* standard FLEX sector */
#define NUM_SECT        72              /* sectors/track */
#define TRAK_SIZE       (SECT_SIZE * NUM_SECT) /* trk size (bytes) */
#define HEADS           1               /* handle as SS with twice the sectors */
#define NUM_CYL         80              /* maximum tracks */
#define DSK_SIZE        (NUM_SECT * HEADS * NUM_CYL * SECT_SIZE) /* dsk size (bytes) */

#define SECSIZ          256             /* standard FLEX sector */

/* SIR offsets */
#define MAXCYL          0x26            /* last cylinder # */
#define MAXSEC          0x27            /* last sector # */

/* 1797 status bits */

#define BUSY            0x01
#define DRQ             0x02
#define WRPROT          0x40
#define NOTRDY          0x80

/* function prototypes */

t_stat dsk_reset (DEVICE *dptr);

/* SS-50 I/O address space functions */

int32 fdcdrv(int32 io, int32 data);
int32 fdccmd(int32 io, int32 data);
int32 fdctrk(int32 io, int32 data);
int32 fdcsec(int32 io, int32 data);
int32 fdcdata(int32 io, int32 data);

/* Local Variables */

int32 fdcbyte;
int32 intrq = 0;                        /* interrupt request flag */
int32 cur_dsk;                          /* Currently selected drive */
int32 wrt_flag = 0;                     /* FDC write flag */

int32   spt;                            /* sectors/track */
int32   trksiz;                         /* trk size (bytes) */
int32   heds;                           /* number of heads */
int32   cpd;                            /* cylinders/disk */
int32   dsksiz;                         /* dsk size (bytes) */

/* Floppy Disk Controller data structures

       dsk_dev        Mother Board device descriptor
       dsk_unit       Mother Board unit descriptor
       dsk_reg        Mother Board register list
       dsk_mod        Mother Board modifiers list
*/

UNIT dsk_unit[] = {
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, 0)  }
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
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE dsk_dev = {
    "DC-4",                             //name
    dsk_unit,                           //units
    dsk_reg,                            //registers
    dsk_mod,                            //modifiers
    4,                                  //numunits
    16,                                 //aradix
    16,                                 //awidth
    1,                                  //aincr
    16,                                 //dradix
    8,                                  //dwidth
    NULL,                               //examine
    NULL,                               //deposit
    &dsk_reset,                         //reset
    NULL,                               //boot
    NULL,                               //attach
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DEBUG,                          //flags
    0,                                  //dctrl
    dsk_debug,                          /* debflags */
    NULL,                               //msize
    NULL                                //lname
};

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
    int i;

    cur_dsk = 5;                        /* force initial SIR read */
    for (i=0; i<NUM_DISK; i++) {
        dsk_unit[i].u3 = 0;             /* clear current flags */
        dsk_unit[i].u4 = 0;             /* clear current cylinder # */
        dsk_unit[i].u5 = 0;             /* clear current sector # */
        dsk_unit[i].pos = 0;            /* clear current byte ptr */
        if (dsk_unit[i].filebuf == NULL) {
            dsk_unit[i].filebuf = malloc(256); /* allocate buffer */
            if (dsk_unit[i].filebuf == NULL) {
                printf("dc-4_reset: Malloc error\n");
                return SCPE_MEM;
            }
        }
    }
    spt = 0;
    trksiz = 0;
    heds = 0;
    cpd = 0;
    dsksiz = 0;
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the MP-B2 module when a
   read or write occur to addresses 0x8004-0x8007. */

/* DC-4 drive select register routine - this register is not part of the 1797
*/

int32 fdcdrv(int32 io, int32 data)
{
    static long pos;

    if (io) {                           /* write to DC-4 drive register */
        sim_debug (DEBUG_write, &dsk_dev, "\nfdcdrv: Drive selected %d cur_dsk=%d",
            data & 0x03, cur_dsk);
        if (cur_dsk == (data & 0x03)) 
            return 0;                   /* already selected */
        cur_dsk = data & 0x03;          /* only 2 drive select bits */
        sim_debug (DEBUG_write, &dsk_dev, "\nfdcdrv: Drive set to %d", cur_dsk);
        if ((dsk_unit[cur_dsk].flags & UNIT_ENABLE) == 0) {
            dsk_unit[cur_dsk].u3 |= WRPROT; /* set 1797 WPROT */
            sim_debug (DEBUG_write, &dsk_dev, "\nfdcdrv: Drive write protected");
        } else {
            dsk_unit[cur_dsk].u3 &= ~WRPROT; /* set 1797 not WPROT */
            sim_debug (DEBUG_write, &dsk_dev, "\nfdcdrv: Drive NOT write protected");
        }
        pos = 0x200;                    /* Read in SIR */
        sim_debug (DEBUG_read, &dsk_dev, "\nfdcdrv: Read pos = %ld ($%04X)",
            pos, (unsigned int) pos);
        sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to offset */
        sim_fread(dsk_unit[cur_dsk].filebuf, SECSIZ, 1, dsk_unit[cur_dsk].fileref); /* read in buffer */
        dsk_unit[cur_dsk].u3 |= BUSY | DRQ; /* set DRQ & BUSY */
        dsk_unit[cur_dsk].pos = 0;      /* clear counter */
        spt = *((uint8 *)(dsk_unit[cur_dsk].filebuf) + MAXSEC) & 0xFF;
        heds = 0;
        cpd = *((uint8 *)(dsk_unit[cur_dsk].filebuf) + MAXCYL) & 0xFF;
        trksiz = spt * SECSIZ;
        dsksiz = trksiz * cpd;
        sim_debug (DEBUG_read, &dsk_dev, "\nfdcdrv: spt=%d heds=%d cpd=%d trksiz=%d dsksiz=%d flags=%08X u3=%08X",
                spt, heds, cpd, trksiz, dsksiz, dsk_unit[cur_dsk].flags, dsk_unit[cur_dsk].u3);
        return 0;
    } else {                            /* read from DC-4 drive register */
        sim_debug (DEBUG_read, &dsk_dev, "\nfdcdrv: Drive read as %02X", intrq);
        return intrq;
    }
}

/* WD 1797 FDC command register routine */

int32 fdccmd(int32 io, int32 data)
{
    static int32 val = 0, val1 = NOTRDY;
    static long pos;
 
    if ((dsk_unit[cur_dsk].flags & UNIT_ATT) == 0) { /* not attached */
        dsk_unit[cur_dsk].u3 |= NOTRDY; /* set not ready flag */
        sim_debug (DEBUG_flow, &dsk_dev, "\nfdccmd: Drive %d is not attached", cur_dsk);
        return 0;
    } else {
        dsk_unit[cur_dsk].u3 &= ~NOTRDY; /* clear not ready flag */
    }
    if (io) {                           /* write command to fdc */
        switch(data) {
            case 0x8C:                  /* read command */
            case 0x9C:
                sim_debug (DEBUG_read, &dsk_dev, "\nfdccmd: Read of disk %d, track %d, sector %d", 
                        cur_dsk, dsk_unit[cur_dsk].u4, dsk_unit[cur_dsk].u5);
                pos = trksiz * dsk_unit[cur_dsk].u4; /* calculate file offset */
                pos += SECSIZ * (dsk_unit[cur_dsk].u5 - 1);
                sim_debug (DEBUG_read, &dsk_dev, "\nfdccmd: Read pos = %ld ($%08X)",
                    pos, (unsigned int) pos);
                sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to offset */
                sim_fread(dsk_unit[cur_dsk].filebuf, SECSIZ, 1, dsk_unit[cur_dsk].fileref); /* read in buffer */
                dsk_unit[cur_dsk].u3 |= BUSY | DRQ; /* set DRQ & BUSY */
                dsk_unit[cur_dsk].pos = 0; /* clear counter */
                break;
            case 0xAC:                  /* write command */
                sim_debug (DEBUG_write, &dsk_dev, "\nfdccmd: Write of disk %d, track %d, sector %d",
                        cur_dsk, dsk_unit[cur_dsk].u4, dsk_unit[cur_dsk].u5);
                if (dsk_unit[cur_dsk].u3 & WRPROT) {
                    printf("\nfdccmd: Drive %d is write-protected", cur_dsk);
                } else {
                    pos = trksiz * dsk_unit[cur_dsk].u4; /* calculate file offset */
                    pos += SECSIZ * (dsk_unit[cur_dsk].u5 - 1);
                    sim_debug (DEBUG_write, &dsk_dev, "\nfdccmd: Write pos = %ld ($%08X)",
                        pos, (unsigned int) pos);
                    sim_fseek(dsk_unit[cur_dsk].fileref, pos, SEEK_SET); /* seek to offset */
                    wrt_flag = 1;           /* set write flag */
                    dsk_unit[cur_dsk].u3 |= BUSY | DRQ;/* set DRQ & BUSY */
                    dsk_unit[cur_dsk].pos = 0; /* clear counter */
                }
                break;
            case 0x18:                  /* seek command */
            case 0x1B:
                dsk_unit[cur_dsk].u4 = fdcbyte; /* set track */
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                sim_debug (DEBUG_flow, &dsk_dev, "\nfdccmd: Seek of disk %d, track %d",
                    cur_dsk, fdcbyte);
                break;
            case 0x0B:                  /* restore command */
                dsk_unit[cur_dsk].u4 = 0;   /* home the drive */
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                sim_debug (DEBUG_flow, &dsk_dev, "\nfdccmd: Drive %d homed", cur_dsk);
                break;
            case 0xF0:                  /* write track command */
                sim_debug (DEBUG_write, &dsk_dev, "\nfdccmd: Write track command for drive %d",
                    cur_dsk);
                break;
            default:
                printf("Unknown FDC command %02XH\n\r", data);
        }
    } else {                            /* read status from fdc */
        val = dsk_unit[cur_dsk].u3;     /* set return value */
        /* either print below will force the val to 0x43 forever.  timing problem in
        the 6800 disk driver software? */
//        sim_debug (DEBUG_flow, &dsk_dev, "\nfdccmd: Exit Drive %d status=%02X",
//            cur_dsk, val);
//        sim_debug (DEBUG_flow, &dsk_dev, "\n%02X", val); //even this short fails it!
        if (val1 == 0 && ((val & (BUSY + DRQ)) == (BUSY + DRQ)))   /* delay BUSY going high */
            val &= ~BUSY;
        if (val != val1)                /* now allow BUSY after one read */
            val1 = val;
        sim_debug (DEBUG_flow, &dsk_dev, "\nfdccmd: Exit Drive %d status=%02X",
            cur_dsk, val);
    }
    return val;
}

/* WD 1797 FDC track register routine */

int32 fdctrk(int32 io, int32 data)
{
    if (io) {
        dsk_unit[cur_dsk].u4 = data & 0xFF;
        sim_debug (DEBUG_read, &dsk_dev, "\nfdctrk: Drive %d track set to %d",
            cur_dsk, dsk_unit[cur_dsk].u4);
    }
    sim_debug (DEBUG_write, &dsk_dev, "\nfdctrk: Drive %d track read as %d",
        cur_dsk, dsk_unit[cur_dsk].u4);
    return dsk_unit[cur_dsk].u4;
}

/* WD 1797 FDC sector register routine */

int32 fdcsec(int32 io, int32 data)
{
    if (io) {
        dsk_unit[cur_dsk].u5 = data & 0xFF;
        if (dsk_unit[cur_dsk].u5 == 0)  /* fix for swtp boot! */
            dsk_unit[cur_dsk].u5 = 1;
        sim_debug (DEBUG_write, &dsk_dev, "\nfdcsec: Drive %d sector set to %d",
            cur_dsk, dsk_unit[cur_dsk].u5);
    }
    sim_debug (DEBUG_read, &dsk_dev, "\nfdcsec: Drive %d sector read as %d",
        cur_dsk, dsk_unit[cur_dsk].u5);
    return dsk_unit[cur_dsk].u5;
}

/* WD 1797 FDC data register routine */

int32 fdcdata(int32 io, int32 data)
{
    int32 val;

    if (io) {                           /* write byte to fdc */
        fdcbyte = data;                 /* save for seek */
        if (dsk_unit[cur_dsk].pos < SECSIZ) { /* copy bytes to buffer */
            sim_debug (DEBUG_write, &dsk_dev, "\nfdcdata: Writing byte %d of %02X",
                dsk_unit[cur_dsk].pos, data);
            *((uint8 *)(dsk_unit[cur_dsk].filebuf) + dsk_unit[cur_dsk].pos) = data; /* byte into buffer */
            dsk_unit[cur_dsk].pos++;    /* step counter */
            if (dsk_unit[cur_dsk].pos == SECSIZ) {
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ);
                if (wrt_flag) {         /* if initiated by FDC write command */
                    sim_fwrite(dsk_unit[cur_dsk].filebuf, SECSIZ, 1, dsk_unit[cur_dsk].fileref); /* write it */
                    wrt_flag = 0;       /* clear write flag */
                }
                sim_debug (DEBUG_write, &dsk_dev, "\nfdcdata: Sector write complete");
            }
        }
        return 0;
    } else {                            /* read byte from fdc */
        if (dsk_unit[cur_dsk].pos < SECSIZ) { /* copy bytes from buffer */
            sim_debug (DEBUG_read, &dsk_dev, "\nfdcdata: Reading byte %d u3=%02X",
                dsk_unit[cur_dsk].pos, dsk_unit[cur_dsk].u3);
            val = *((uint8 *)(dsk_unit[cur_dsk].filebuf) + dsk_unit[cur_dsk].pos) & 0xFF;
            dsk_unit[cur_dsk].pos++;        /* step counter */
            if (dsk_unit[cur_dsk].pos == SECSIZ) { /* done? */
                dsk_unit[cur_dsk].u3 &= ~(BUSY | DRQ); /* clear flags */
                sim_debug (DEBUG_read, &dsk_dev, "\nfdcdata: Sector read complete");
            }
            return val;
        } else
        return 0;
    }
}

/* end of dc-4.c */
