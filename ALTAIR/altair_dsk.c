/* altair_dsk.c: MITS Altair 88-DISK Simulator

   Copyright (c) 1997-2010, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

    The 88_DISK is a 8-inch floppy controller which can control up
    to 16 daisy-chained Pertec FD-400 hard-sectored floppy drives.
    Each diskette has physically 77 tracks of 32 137-byte sectors
    each.

    The controller is interfaced to the CPU by use of 3 I/O addreses,
    standardly, these are device numbers 10, 11, and 12 (octal).

    Address     Mode    Function
    -------             ----    --------

        10              Out             Selects and enables Controller and Drive
        10              In              Indicates status of Drive and Controller
        11              Out             Controls Disk Function
        11              In              Indicates current sector position of disk
        12              Out             Write data
        12              In              Read data

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
    I - When 0, indicates interrupts enabled (not used this simulator)
    Z - When 0, indicates head is on track 0
    R - When 0, indicates that read circuit has new byte to read

    Drive Control (Device 11 OUT):

    +---+---+---+---+---+---+---+---+
    | W | C | D | E | U | H | O | I |
    +---+---+---+---+---+---+---+---+

    I - When 1, steps head IN one track
    O - When 1, steps head OUT out track
    H - When 1, loads head to drive surface
    U - When 1, unloads head
    E - Enables interrupts (ignored this simulator)
    D - Disables interrupts (ignored this simulator)
    C - When 1 lowers head current (ignored this simulator)
    W - When 1, starts Write Enable sequence:   W bit on device 10
        (see above) will go 1 and data will be read from port 12
        until 137 bytes have been read by the controller from
        that port.  The W bit will go off then, and the sector data
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
    T = Sector True, is a 1 when the sector is positioned to read or
        write.

*/

#include <stdio.h>

#include "altair_defs.h"

#define UNIT_V_ENABLE   (UNIT_V_UF + 0)                 /* Write Enable */
#define UNIT_ENABLE (1 << UNIT_V_ENABLE)

#define DSK_SECTSIZE 137
#define DSK_SECT 32
#define DSK_TRACSIZE 4384
#define DSK_SURF 1
#define DSK_CYL 77
#define DSK_SIZE (DSK_SECT * DSK_SURF * DSK_CYL * DSK_SECTSIZE)

t_stat dsk_svc (UNIT *uptr);
t_stat dsk_reset (DEVICE *dptr);
void writebuf();

extern int32 PCX;

/* Global data on status */

int32 cur_disk = 8;                                     /* Currently selected drive */
int32 cur_track[9] = {0, 0, 0, 0, 0, 0, 0, 0, 377};
int32 cur_sect[9] = {0, 0, 0, 0, 0, 0, 0, 0, 377};
int32 cur_byte[9] = {0, 0, 0, 0, 0, 0, 0, 0, 377};
int32 cur_flags[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

char dskbuf[138];                                       /* Data Buffer */
int32 dirty = 0;                                        /* 1 when buffer has unwritten data in it */
UNIT *dptr;                                             /* fileref to write dirty buffer to */

int32 dsk_rwait = 100;                                  /* rotate latency */


/* 88DSK Standard I/O Data Structures */

UNIT dsk_unit[] = {
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  },
    { UDATA (&dsk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DSK_SIZE)  }
};

REG dsk_reg[] = {
    { ORDATA (DISK, cur_disk, 4) },
    { NULL }
};

DEVICE dsk_dev = {
    "DSK", dsk_unit, dsk_reg, NULL,
    8, 10, 31, 1, 8, 8,
    NULL, NULL, &dsk_reset,
    NULL, NULL, NULL
};

/*  Service routines to handle simlulator functions */

/* service routine - actually gets char & places in buffer */

t_stat dsk_svc (UNIT *uptr)
{
return SCPE_OK;
}

/* Reset routine */

t_stat dsk_reset (DEVICE *dptr)
{
cur_disk = 0;
return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.

    Each function is passed an 'io' flag, where 0 means a read from
    the port, and 1 means a write to the port.  On input, the actual
    input is passed as the return value, on output, 'data' is written
    to the device.
*/

/* Disk Controller Status/Select */

/*  IMPORTANT: The status flags read by port 8 IN instruction are
    INVERTED, that is, 0 is true and 1 is false.  To handle this, the
    simulator keeps it's own status flags as 0=false, 1=true; and
    returns the COMPLEMENT of the status flags when read.  This makes
    setting/testing of the flag bits more logical, yet meets the
    simulation requirement that they are reversed in hardware.
*/

int32 dsk10(int32 io, int32 data)
{

    if (io == 0) {                                      /* IN: return flags */
        return ((~cur_flags[cur_disk]) & 0xFF);         /* Return the COMPLEMENT! */
    }

    /* OUT: Controller set/reset/enable/disable */

    if (dirty == 1)
        writebuf();

    /*printf("\n[%o] OUT 10: %x", PCX, data);*/
    cur_disk = data & 0x0F;
    if (data & 0x80) {
        cur_flags[cur_disk] = 0;                        /* Disable drive */
        cur_sect[cur_disk] = 0377;
        cur_byte[cur_disk] = 0377;
        return (0);
    }
    cur_flags[cur_disk] = 0x1A;                         /* Enable: head move true */
    cur_sect[cur_disk] = 0377;                          /* reset internal counters */
    cur_byte[cur_disk] = 0377;
    if (cur_track[cur_disk] == 0)
        cur_flags[cur_disk] |= 0x40;                    /* track 0 if there */
    return (0);
}

/* Disk Drive Status/Functions */

int32 dsk11(int32 io, int32 data)
{
    int32 stat;

    if (io == 0) {                                      /* Read sector position */
        /*printf("\n[%o] IN 11", PCX);*/
        if (dirty == 1)
            writebuf();
        if (cur_flags[cur_disk] & 0x04) {               /* head loaded? */
            cur_sect[cur_disk]++;
            if (cur_sect[cur_disk] > 31)
                cur_sect[cur_disk] = 0;
            cur_byte[cur_disk] = 0377;
            stat = cur_sect[cur_disk] << 1;
            stat &= 0x3E;                               /* return 'sector true' bit = 0 (true) */
            stat |= 0xC0;                               /* set on 'unused' bits */
            return (stat);
        } else {
            return (0);                                 /* head not loaded - return 0 */
        }
    }

    /* Drive functions */

    if (cur_disk > 7)
        return (0);                                     /* no drive selected - can do nothin */

    /*printf("\n[%o] OUT 11: %x", PCX, data);*/
    if (data & 0x01) {                                  /* Step head in */
        cur_track[cur_disk]++;
        if (cur_track[cur_disk] > 76 )
            cur_track[cur_disk] = 76;
        if (dirty == 1)
            writebuf();
        cur_sect[cur_disk] = 0377;
        cur_byte[cur_disk] = 0377;
    }

    if (data & 0x02) {                                  /* Step head out */
        cur_track[cur_disk]--;
        if (cur_track[cur_disk] < 0) {
            cur_track[cur_disk] = 0;
            cur_flags[cur_disk] |= 0x40;                /* track 0 if there */
        }
        if (dirty == 1)
            writebuf();
        cur_sect[cur_disk] = 0377;
        cur_byte[cur_disk] = 0377;
    }

    if (dirty == 1)
        writebuf();

    if (data & 0x04) {                                  /* Head load */
        cur_flags[cur_disk] |= 0x04;                    /* turn on head loaded bit */
        cur_flags[cur_disk] |= 0x80;                    /* turn on 'read data available */
    }

    if (data & 0x08) {                                  /* Head Unload */
        cur_flags[cur_disk] &= 0xFB;                    /* off on 'head loaded' */
        cur_flags[cur_disk] &= 0x7F;                    /* off on 'read data avail */
        cur_sect[cur_disk] = 0377;
        cur_byte[cur_disk] = 0377;
    }

    /* Interrupts & head current are ignored */

    if (data & 0x80) {                                  /* write sequence start */
        cur_byte[cur_disk] = 0;
        cur_flags[cur_disk] |= 0x01;                    /* enter new write data on */
    }
    return 0;
}

/* Disk Data In/Out*/

int32 dsk12(int32 io, int32 data)
{
    static int32 rtn, i;
    static long pos;
    UNIT *uptr;

    uptr = dsk_dev.units + cur_disk;
    if (io == 0) {
        if ((i = cur_byte[cur_disk]) < 138) {           /* just get from buffer */
            cur_byte[cur_disk]++;
            return (dskbuf[i] & 0xFF);
        }
        /* physically read the sector */
        /*printf("\n[%o] IN 12 (READ) T%d S%d", PCX, cur_track[cur_disk],
                        cur_sect[cur_disk]);*/
        pos = DSK_TRACSIZE * cur_track[cur_disk];
        pos += DSK_SECTSIZE * cur_sect[cur_disk];
        if ((uptr == NULL) || (uptr->fileref == NULL))
            return 0;
        rtn = fseek(uptr -> fileref, pos, 0);
        rtn = fread(dskbuf, 137, 1, uptr -> fileref);
        cur_byte[cur_disk] = 1;
        return (dskbuf[0] & 0xFF);
    } else {
        if (cur_byte[cur_disk] > 136) {
            i = cur_byte[cur_disk];
            dskbuf[i] = data & 0xFF;
            writebuf();
            return (0);
        }
        i = cur_byte[cur_disk];
        dirty = 1;
        dptr = uptr;
        dskbuf[i] = data & 0xFF;
        cur_byte[cur_disk]++;
        return (0);
    }
}

void writebuf()
{
    long pos;
    int32 rtn, i;

    i = cur_byte[cur_disk];                             /* null-fill rest of sector if any */
    while (i < 138) {
        dskbuf[i] = 0;
        i++;
    }
    /*printf("\n[%o] OUT 12 (WRITE) T%d S%d", PCX, cur_track[cur_disk],
                        cur_sect[cur_disk]); i = getch(); */
    pos = DSK_TRACSIZE * cur_track[cur_disk];           /* calc file pos */
    pos += DSK_SECTSIZE * cur_sect[cur_disk];
    rtn = fseek(dptr -> fileref, pos, 0);
    rtn = fwrite(dskbuf, 137, 1, dptr -> fileref);
    cur_flags[cur_disk] &= 0xFE;                        /* ENWD off */
    cur_byte[cur_disk] = 0377;
    dirty = 0;
    return;
}
