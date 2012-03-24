/* swtp_dc4_dsk.c: SWTP DC-4 DISK Simulator

   Copyright (c) 2005, William A. Beech

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A. Beech shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A. Beech.
   
   Based on work by Charles E Owen (c) 1997 and Peter Schorn (c) 2002-2005


	The DC-4 is a 5-inch floppy controller which can control up
    to 4 daisy-chained 5-inch floppy drives.  The controller is based on
	the Western Digital 1797 Floppy Disk Controller (FDC) chip. This
	file only emulates the minimum DC-4 functionality to interface with
	the virtual disk file.

    The floppy controller  is interfaced to the CPU by use of 5 memory 
	addreses.  These are device numbers 5 and 6 (0x8014-0x801B).

    Address 	Mode	Function
    -------		----	--------

	0x8014		Read	Returns FDC interrupt status
	0x8014		Write	Selects the drive/head/motor control
    0x8018		Read	Returns status of FDC
    0x8018		Write	FDC command register
	0x8019		Read	Returns FDC track register
	0x8019		Write	Set FDC track register
    0x801A		Read	Returns FDC sector register
	0x801A		Write	Set FDC sector register
    0x801B		Read	Read data
    0x801B		Write	Write data

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

	Track	Sector	Use
	0		1		Boot sector
	0		2		Boot sector (cont)
	0		3		Unused
	0		4		System Identity Record (explained below)
	0		5		Unused
	0		6-last	Directory - 10 entries/sector (explained below)
	1		1		First available data sector
	last-1	last	Last available data sector

	System Identity Record

	Byte	Use
	0x10	Volume ID (8 bytes)
	0x18	???
	0x19	???
	0x1A	???
	0x1B	Volume number (2 bytes)
	0x1D	First free sector (2 bytes)
	0x1F	Last track minus one (byte)
	0x20	Last sector (byte)
	0x21	Total sectors on disk (2 bytes)
	0x23	Month (byte
	0x24	Day (byte)
	0x25	Year (byte)
	0x26	Last track minus one (byte)
	0x27	Last sector (byte)	

*/

#include <stdio.h>

#include "swtp_defs.h"

#define	UNIT_V_ENABLE	(UNIT_V_UF + 0)	/* Write Enable */
#define UNIT_ENABLE	(1 << UNIT_V_ENABLE)

/* emulate a SS FLEX disk with 72 sectors and 80 tracks */

#define	NUM_DISK		4			/* standard 1797 maximum */
#define SECT_SIZE		256			/* standard FLEX sector */
#define NUM_SECT		72			/* sectors/track */
#define TRAK_SIZE		(SECT_SIZE * NUM_SECT)
#define HEADS			1			/* handle as SS with twice the sectors */
#define NUM_CYL			80			/* maximum tracks */				
#define DSK_SIZE		(NUM_SECT * HEADS * NUM_CYL * SECT_SIZE)

/* 1797 status bits */

#define	BUSY			0x01
#define	DRQ				0x02
#define	WRPROT			0x40
#define	NOTRDY			0x80

/* debug prints */

#define	DEBUG	0


/* prototypes */

t_stat dsk_svc (UNIT *uptr);
t_stat dsk_reset (DEVICE *dptr);
int32 fdcdrv(int32 io, int32 data);
int32 fdccmd(int32 io, int32 data);
int32 fdctrk(int32 io, int32 data);
int32 fdcsec(int32 io, int32 data);
int32 fdcdata(int32 io, int32 data);

/* Global data on status */

int32 cur_dsk = NUM_DISK;			/* Currently selected drive */
int32 cur_trk[NUM_DISK] = {0, 0, 0, 0};
int32 cur_sec[NUM_DISK] = {0, 0, 0, 0};
int32 cur_byt[NUM_DISK] = {0, 0, 0, 0};
int32 cur_flg[NUM_DISK] = {NOTRDY, NOTRDY, NOTRDY, NOTRDY};

/* Variables */

uint8 dskbuf[SECT_SIZE];			/* Data Buffer */
UNIT *dptr = NULL;					/* fileref to write dirty buffer to */
int32 fdcbyte;
int32 intrq = 0;					/* interrupt request flag */

/* DC-4 Simh Device Data Structures */

UNIT dsk_unit[] = {
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
	{ UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  } };

REG dsk_reg[] = {
	{ HRDATA (DISK, cur_dsk, 4) },
	{ NULL } };

MTAB dsk_mod[] = {
	{ UNIT_ENABLE, UNIT_ENABLE, "RW", "RW", NULL },
	{ UNIT_ENABLE, 0, "RO", "RO", NULL },
	{ 0 } };

DEVICE dsk_dev = {
	"DSK", dsk_unit, dsk_reg, dsk_mod,
	NUM_DISK, 16, 16, 1, 16, 8,
	NULL, NULL, &dsk_reset,
	NULL, NULL, NULL };

/* service routines to handle simlulator functions */

/* service routine - actually gets char & places in buffer */

t_stat dsk_svc (UNIT *uptr)
{
return SCPE_OK;
}

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
cur_dsk = 0;
return SCPE_OK;
}

/* I/O instruction handlers, called from the CPU module when an
   memory read or write to the proper addresses is issued.

   Each function is passed an 'io' flag, where 0 means a read from
   the port, and 1 means a write to the port.  On input, the actual
   input is passed as the return value, on output, 'data' is written
   to the device.
*/

/* DC-4 drive select register routine - this register is not part of the 1797
*/

int32 fdcdrv(int32 io, int32 data)
{
/* **** probably need to grab the parameters from the SIR and set the limits */
	if (io) {							/* write to DC-4 drive register */
		cur_dsk = data & 0x03;			/* only 2 drive select bits */
#if DEBUG > 0
		printf("Drive set to %d\n\r", cur_dsk);
#endif
		if ((dsk_unit[cur_dsk].flags & UNIT_ENABLE) == 0)	
			cur_flg[cur_dsk] |= WRPROT;	/* set WPROT */
		return 0;
	} else {							/* read from DC-4 drive register */
#if DEBUG > 0
		printf("Drive read as %02X\n\r", intrq);
#endif
		return intrq;
	}
}

/* WD 1797 FDC command register routine */

int32 fdccmd(int32 io, int32 data)
{
	static int32 val = 0, val1 = NOTRDY, i;
    static long pos;
    UNIT *uptr;
 
	if ((dsk_unit[cur_dsk].flags & UNIT_ATT) == 0) { /* not attached */
		cur_flg[cur_dsk] |= NOTRDY;		/* set not ready flag */
		printf("Drive %d is not attached\n\r", cur_dsk);
		return 0;
	} else {
		cur_flg[cur_dsk] &= ~NOTRDY;	/* clear not ready flag */
	}	
	uptr = dsk_dev.units + cur_dsk;		/* get virtual drive address */
	if (io) {							/* write command to fdc */
		switch(data) {
			case 0x8C:					/* read command */
			case 0x9C:
#if DEBUG > 0
				printf("Read of disk %d, track %d, sector %d\n\r", 
					cur_dsk, cur_trk[cur_dsk], cur_sec[cur_dsk]);
#endif
				pos = TRAK_SIZE * cur_trk[cur_dsk]; /* calculate file offset */
				pos += SECT_SIZE * (cur_sec[cur_dsk] - 1);
#if DEBUG > 0
				printf("Read pos = %ld ($%04X)\n\r", pos, pos);
#endif
				sim_fseek(uptr -> fileref, pos, 0); /* seek to offset */
				sim_fread(dskbuf, 256, 1, uptr -> fileref); /* read in buffer */
				cur_flg[cur_dsk] |= BUSY | DRQ;	/* set DRQ & BUSY */
				i = cur_byt[cur_dsk] = 0;	/* clear counter */
				break;
			case 0xAC:					/* write command */
#if DEBUG > 0
				printf("Write of disk %d, track %d, sector %d\n\r", 
					cur_dsk, cur_trk[cur_dsk], cur_sec[cur_dsk]);
#endif
				if (cur_flg[cur_dsk] & WRPROT) {		
					printf("Drive %d is write-protected\n\r", cur_dsk);
				} else {
					pos = TRAK_SIZE * cur_trk[cur_dsk]; /* calculate file offset */
					pos += SECT_SIZE * (cur_sec[cur_dsk] - 1);
#if DEBUG > 1
					printf("Write pos = %ld ($%04X)\n\r", pos, pos);
#endif
					sim_fseek(uptr -> fileref, pos, 0); /* seek to offset */
					dptr = uptr;			/* save pointer for actual write */
					cur_flg[cur_dsk] |= BUSY | DRQ;/* set DRQ & BUSY */
					i = cur_byt[cur_dsk] = 0;	/* clear counter */
				}
				break;
			case 0x18:					/* seek command */
			case 0x1B:
				cur_trk[cur_dsk] = fdcbyte;	/* set track */
				cur_flg[cur_dsk] &= ~(BUSY | DRQ);	/* clear flags */
#if DEBUG > 0
				printf("Seek of disk %d, track %d\n\r", cur_dsk, fdcbyte);
#endif
				break;
			case 0x0B:					/* restore command */
				cur_trk[cur_dsk] = 0;	/* home the drive */
				cur_flg[cur_dsk] &= ~(BUSY | DRQ);	/* clear flags */
#if DEBUG > 0
				printf("Drive %d homed\n\r", cur_dsk);
#endif
				break;
			default:
				printf("Unknown FDC command %02XH\n\r", data);
		}
	} else {							/* read status from fdc */
		val = cur_flg[cur_dsk];			/* set return value */
		if (val1 == 0 && val == 0x03)	/* delay BUSY going high */
			val = 0x02;					/* set DRQ first */
		if (val != val1) {				/* now allow BUSY after on read */
			val1 = val;
#if DEBUG > 0
			printf("Drive %d status=%02X\n\r", cur_dsk, cur_flg[cur_dsk]);
#endif
		}
    }
	return val;
}

/* WD 1797 FDC track register routine */

int32 fdctrk(int32 io, int32 data)
{
	if (io) {
		cur_trk[cur_dsk] = data & 0xFF;
#if DEBUG > 1
		printf("Drive %d track set to %d\n\r", cur_dsk, data);
#endif
	} else
		;
#if DEBUG > 1
		printf("Drive %d track read as %d\n\r", cur_dsk, cur_trk[cur_dsk]);
#endif
	return cur_trk[cur_dsk];
}

/* WD 1797 FDC sector register routine */

int32 fdcsec(int32 io, int32 data)
{
	if (io) {
		cur_sec[cur_dsk] = data & 0xFF;
		if (cur_sec[cur_dsk] == 0)	/* fix for swtp boot! */
			cur_sec[cur_dsk] = 1;
#if DEBUG > 1
		printf("Drive %d sector set to %d\n\r", cur_dsk, data);
#endif
	} else
		;
#if DEBUG > 1
		printf("Drive %d sector read as %d\n\r", cur_dsk, cur_sec[cur_dsk]);
#endif
	return cur_sec[cur_dsk];
}

/* WD 1797 FDC data register routine */

int32 fdcdata(int32 io, int32 data)
{
	int32 i;

	if (io) {							/* write byte to fdc */
		fdcbyte = data;					/* save for seek */
        if ((i = cur_byt[cur_dsk]) < SECT_SIZE) { /* copy bytes to buffer */
#if DEBUG > 3
			printf("Writing byte %d of %02X\n\r", cur_byt[cur_dsk], data);
#endif
        	cur_byt[cur_dsk]++;			/* step counter */
			dskbuf[i] = data;			/* byte into buffer */
			if (cur_byt[cur_dsk] == SECT_SIZE) {
				cur_flg[cur_dsk] &= ~(BUSY | DRQ);
				if (dptr) {				/* if initiated by FDC write command */
					sim_fwrite(dskbuf, 256, 1, dptr -> fileref); /* write it */
					dptr = NULL;
				}
#if DEBUG > 0
				printf("Sector write complete\n\r");
#endif
			}
		}
		return 0;
	} else {							/* read byte from fdc */
        if ((i = cur_byt[cur_dsk]) < SECT_SIZE) { /* copy bytes from buffer */
#if DEBUG > 1
			printf("Reading byte %d\n\r", cur_byt[cur_dsk]);
#endif
        	cur_byt[cur_dsk]++;			/* step counter */
			if (cur_byt[cur_dsk] == SECT_SIZE) { /* done? */
				cur_flg[cur_dsk] &= ~(BUSY | DRQ); /* clear flags */
#if DEBUG > 0
				printf("Sector read complete\n\r");
#endif
			}
       		return (dskbuf[i] & 0xFF);
		} else
			return 0;
	}
}

