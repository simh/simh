/*  fd400.c: Percom LFD-400 FDC Simulator

    Copyright (c) 2022, Roberto Sancho

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

        Except as contained in this notice, the name of Roberto Sancho shall not
        be used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from Roberto Sancho .

    MODIFICATIONS:

    NOTES:

        The FDC-400 is a 5-1/4"-inch floppy controller which can control up
        to four 5-1/4inch floppy drives.  
        This file only emulates the minimum functionality to interface with
        the virtual disk file.

        The floppy controller is interfaced to the CPU by use of 7 memory 
        addreses (0xCC00-0xCC06).

        Address     Mode    Function
        -------     ----    --------

        0xCC00      Read    CONTROLLER STATUS
        0xCC00      Write   SYNC WORD PORT
        0xCC01      Read    RECEIVED DATA
        0xCC01      Write   WRITE DATA PORT
        0xCC02      Read    SECTOR COUNTER
        0xCC02      Write   FILL WORD PORT
        0xCC03      Read    DRIVE STATUS
        0xCC03      Write   DRIVE AND TRACK SELECT
        0xCC04      Read    RECEIVER RESTART PULSE
        0xCC04      Write   WRITE PULSE
        0xCC05      Read    MOTOR ON PULSE
        0xCC06      Read    MOTOR OFF PULSE


        CONTROLLER STATUS (Read 0xCC00):

        +---+---+---+---+---+---+---+---+
        | B | x | x | x | x | x | x | R |
        +---+---+---+---+---+---+---+---+

        B = Controller Ready (0=Busy, 1=Ready)
        R = Read byte ready (1=can retrieve read byte)


        RECEIVED DATA (Read 0xCC01):
        WRITE DATA PORT (Write 0xCC01): 

        +---+---+---+---+---+---+---+---+
        |              byte             |
        +---+---+---+---+---+---+---+---+

        Data byte from retrieved sector being read
        Data byte to store in sector being write


        CURRENT SECTOR (Read 0xCC02):
        WRITE FILL CHAR (Write 0xCC02): 

        +---+---+---+---+---+---+---+---+
        | x |          Sector           |
        +---+---+---+---+---+---+---+---+

        Return current Sector 
        Set the fill char for write sector


        DRIVE STATUS (Read 0xCC03):

        +---+---+---+---+---+---+---+---+
        |  DD   | I | S | W | M | T | P |
        +---+---+---+---+---+---+---+---+

        P = Write allowed Bit (0=disk is write protected)
        T = Track Zero Bit (1=head is NOT positioned in track zero)
        M = Motor Test Bit (1=motor stopped)
        W = Write Gate Bit (1=drive gate door is closed)
        S = Sector Pulse Bit (1=head at start of sector)
        I = Index Pulse bit (???)
        DD = Current selected drive (0..3)


        DRIVE AND TRACK SELECT (Write 0xCC03):

        +---+---+---+---+---+---+---+---+
        |  DD   | S | D | x | x | x | x |
        +---+---+---+---+---+---+---+---+

        D = Direction bit (1=Track In=increment track number)
        S = Step Bit (1=Move disk head one track using direction D)
        DD = Select drive (0..3)

        LFD-400 Disck supports these operating systems (1977)

        - MINIDOS: Just Load/Save ram starting at given sector in disk. 
                   No files. No sector allocation management. Rom based 
        - MPX (also know as MiniDOS Plus or MiniDOS/MPX or MiniDOS-PlusX): 
                   Based on MiniDOS, add named files, contiguos allocation management. Transient disk command
        - MiniDisk+ DOS: 
                   Based on MiniDOS, add named files, contiguos allocation management. More disk commands


        MiniDOS files have 40 track (0..39) with 10 sectors (0..9) each with 256 bytes of data

        The following unit registers are used by this controller emulation:

        fd400_dsk_unit[cur_drv].u4        unit current track
        fd400_dsk_unit[cur_drv].u5        unit current sector
        fd400_dsk_unit[cur_drv].pos       unit current sector byte index into buffer
        fd400_dsk_unit[cur_drv].filebuf   unit current sector buffer
        fd400_dsk_unit[cur_drv].fileref   unit current attached file reference

        At start, units discs from controller are dissabled
        To use it, befor attaching the disk image, it is mecessary to do "set lfd-4000 enabled"
        (for unit 0), and optionally "set lfd-4001 enabled" (for unit 1) and so on.
*/

#include <stdio.h>
#include "swtp_defs.h"

#define UNIT_V_ENABLE   (UNIT_V_UF + 0) /* Write Enable */
#define UNIT_ENABLE     (1 << UNIT_V_ENABLE)

/* emulate a Disk disk with 10 sectors and 40 tracks */

#define NUM_DISK        4               
#define SECT_SIZE       (8+4+256)  /* sector size=header (10 bytes) + data (256 bytes) + CRC (2 bytes)  */
#define NUM_SECT        10              /* sectors/track */
#define TRAK_SIZE       (SECT_SIZE * NUM_SECT) /* trk size (bytes) */
#define HEADS           1               /* handle as SS with twice the sectors */
#define NUM_CYL         40              /* maximum tracks */
#define DSK_SIZE        (NUM_SECT * HEADS * NUM_CYL * SECT_SIZE) /* dsk size (bytes) */

#define TRK             fd400_dsk_unit[fd400.cur_dsk].u4   // current disk track and sector
#define SECT            fd400_dsk_unit[fd400.cur_dsk].u5

#define BUF_SIZE        (SECT_SIZE+16)  // sector buffer in memory

/* function prototypes */

t_stat fd400_dsk_reset (DEVICE *dptr);
t_stat fd400_attach (UNIT *, CONST char *);

/* SS-50 I/O address space functions */

int32 fd400_fdcstatus(int32 io, int32 data);
int32 fd400_cstatus(int32 io, int32 data);
int32 fd400_data(int32 io, int32 data);
int32 fd400_cursect(int32 io, int32 data);
int32 fd400_startrw(int32 io, int32 data);

/* Local Variables */

struct {
    int32   cur_dsk;                        /* Currently selected drive */
    int32   SectorPulse;                    // Head positioned at beginning of sector
    int32   StepBit;                        
    uint8   FillChar; 
} fd400 = {0};

/* Floppy Disk Controller data structures

       fd400_dsk_dev        Disk Controller device descriptor
       fd400_dsk_unit       Disk Controller unit descriptor
       fd400_dsk_reg        Disk Controller register list
       fd400_dsk_mod        Disk Controller modifiers list
*/

UNIT fd400_dsk_unit[] = {
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0)  },
        { UDATA (NULL, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+UNIT_DIS, 0)  }
};

REG fd400_dsk_reg[] = {
        { HRDATA (DISK, fd400.cur_dsk, 4) },
        { NULL }
};

DEBTAB fd400_dsk_debug[] = {
    { "ALL", DEBUG_all, "All debug bits" },
    { "FLOW", DEBUG_flow, "Flow control" },
    { "READ", DEBUG_read, "Read Command" },
    { "WRITE", DEBUG_write, "Write Command"},
    { NULL }
};

DEVICE fd400_dsk_dev = {
    "LFD-400",                          //name
    fd400_dsk_unit,                     //units
    fd400_dsk_reg,                      //registers
    NULL,                               //modifiers
    NUM_DISK,                           //numunits
    16,                                 //aradix
    16,                                 //awidth
    1,                                  //aincr
    16,                                 //dradix
    8,                                  //dwidth
    NULL,                               //examine
    NULL,                               //deposit
    &fd400_dsk_reset,                   //reset
    NULL,                               //boot
    &fd400_attach,                      //attach
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DISABLE|DEV_DIS|DEV_DEBUG,      //flags
    0,                                  //dctrl
    fd400_dsk_debug,                    //debflags
    NULL,                               //msize
    NULL                                //lname
};

/* Reset routine */

t_stat fd400_dsk_reset (DEVICE *dptr)
{
    int i;

    for (i=0; i<NUM_DISK; i++) {
        fd400_dsk_unit[i].u3 = 0;             /* clear current flags */
        fd400_dsk_unit[i].u4 = 0;             /* clear current cylinder # */
        fd400_dsk_unit[i].u5 = 0;             /* clear current sector # */
        fd400_dsk_unit[i].pos = 0;            /* clear current byte ptr */
        if (fd400_dsk_unit[i].filebuf == NULL) {
            fd400_dsk_unit[i].filebuf = malloc(BUF_SIZE); /* allocate buffer */
            if (fd400_dsk_unit[i].filebuf == NULL) {
                printf("fc400_reset: Malloc error\n");
                return SCPE_MEM;
            }
        }
    }
    memset(&fd400, 0, sizeof(fd400));

    return SCPE_OK;
}

/*  I/O instruction handlers, called from the MP-B2 module when a
   read or write occur to addresses 0xCC00-0xCC07. */

/* FDC STATUS register $CC03 */

int32 fd400_fdcstatus(int32 io, int32 data)
{
    int32 val;
    UNIT * uptr; 

    uptr = &fd400_dsk_unit[fd400.cur_dsk]; 
    if (io==0) { 
        // io=0 when reading from io register (return data read from i/o device register)
        val=(fd400.cur_dsk & 3) << 6; 
        val |= 8;  // write gate door allways closed
        if ((uptr->flags & UNIT_ATT) == 0) {  
            sim_debug (DEBUG_flow, &fd400_dsk_dev, "Current Drive %d has no file attached \n", fd400.cur_dsk);
        } else {
            // file attached = disk inserted into unit
            if ((uptr->flags & UNIT_RO) == 0) val |= 1;  // unit is read/write
            if (TRK!=0)                       val |= 2;  // head is NOT in track zero
            if (fd400.SectorPulse)  { 
                val |= 16;  
                fd400.SectorPulse--;
            } else { 
                // set sector pulse and incr current sector
                // Simulates somewhat disk rotation whitout having to set up _svr and sim_activate
                fd400.SectorPulse=2; 
                SECT++; if (SECT >= NUM_SECT) SECT = 0;
                uptr->pos=0; // init sector read
            }
            if (SECT==0)  val |= 32;  // index pulse: head is positioned in sector zero
        }
        sim_debug (DEBUG_flow, &fd400_dsk_dev, "Status Returned %02X, Current Drive %d TRK %d SECT %d \n", 
            val, fd400.cur_dsk, TRK, SECT);
        return val; 
    }
    // io=1 -> writing data to i/o register,
    fd400.cur_dsk = data >> 6; // select current disk
    fd400.SectorPulse = 0; // init sector pulse 
    if (data & 32) {
        fd400.StepBit=1; // Step bit set to one
    } else if ((data & 32)==0) { // Step bit set to zero
        if (fd400.StepBit==0) {
            // but was already zero -> no action
        } else {
            // StepBit changing from 1 to 0 -> step track motot
            fd400.StepBit=0; 
            // step track depending on direction bit
            if (data & 16) TRK++; else TRK--;
            if (TRK < 0) TRK=0; 
            if (TRK >= NUM_CYL) TRK = NUM_CYL-1; 
            // on changing track also incr sect num, but not issue sector pulse (landed on middle of sector)
            SECT++; if (SECT >= NUM_SECT) SECT = 0; 
            uptr->pos=0; // init sector read
        }
    }
    sim_debug (DEBUG_flow, &fd400_dsk_dev, "Set Drive and Track %02X, Current Drive %d TRK %d SECT %d \n", 
        data, fd400.cur_dsk, TRK, SECT);
    return 0; 
}

/* CONTROLLER STATUS register (read $CC00)*/

int32 fd400_cstatus(int32 io, int32 data)
{
    // controller allways ready, byte read from sector allways ready
    // writing to is set the sync byte. This is not implemented
    return 128+1; 
}

/* DATA register */

int32 fd400_data(int32 io, int32 data)
{
    uint32 loc;
    UNIT * uptr = &fd400_dsk_unit[fd400.cur_dsk]; 
    uint8 dsk_sect[SECT_SIZE]; // image of sector read/saved in disk image
    uint8 * p = (uint8 *)(uptr->filebuf); // sector byte stream as seen by program
    int i, n; 

    if ((uptr->flags & UNIT_ATT) == 0) return 0; // not attached
    if (io==0) { 
        // io=0 when reading from io register (return data read from i/o device register)
        // read data from sector
        if (uptr->pos==0) {
            // read new sector buffer
            // init buffer to zero
            memset(uptr->filebuf, 0, SECT_SIZE);
            // calculate location of current sector in disk image file
            loc=(TRK * NUM_SECT + SECT) * SECT_SIZE; 
            if (loc >= uptr->capac) {
                // reading past image file current size -> read as zeroes
            } else {
                // read sector
                sim_fseek(uptr->fileref, loc, SEEK_SET);
                sim_fread(dsk_sect, 1, SECT_SIZE, uptr->fileref);
                // reorganize buffer to match the byte order expected by MiniDOS
                //
                // Data in MiniDOS MPX disk image:
                //     BT BS FT FS NN AH AL TY   CH CL PH PL   [256 data bytes] = 268 bytes
                //     where BT=Backward link track, DS=Backward link sector, 
                //           FT=Forward link track, FS=Forward link sector. = 00 00 if this is last sector of file
                //           NN=Number of data bytes (00=256 bytes)
                //           AH AL=Addr in RAM where to load sector data bytes (H=High byte, L=Low byte)
                //           TY=file type
                //           CH CL=CheckSum Hi/Low byte
                //           PH PL=Postamble Hi/Low byte. Holds program start addr on last sector
                //
                // Data as expected by MiniDOS ROM when reading a sector 
                //     SY TR SE BT BS FT FS NN AH AL TY [NN data bytes] CH CL PH PL
                //     where SY=Sync Byte=$FD
                //           TR SE=This track and sector
                //           BT BS FT FS NN AH AL TY=same as above
                //           CH CL=same as above
                //
                // so we create the filebuf (p pointer) reordering data read from disk image file (dsk_sect)
                p[0]=0xFB; // sync byte
                p[1]=TRK; p[2]=SECT; // this sector track and sector number
                for (i=0; i<8; i++) p[3+i]=dsk_sect[i]; // 8 byte header with bytes BT ... TY
                n=dsk_sect[4]; if (n==0) n=256; // number of data bytes
                for (i=0; i<n; i++) p[3+8+i]=dsk_sect[i+12]; // copy data bytes from disk-read-buffer to sector buffer in unit
                for (i=0; i<4; i++) p[3+8+n+i]=dsk_sect[i+8]; // copy checksum and postamble
            }
            sim_debug (DEBUG_read, &fd400_dsk_dev, "Read Disc Image at loc %d, Current Drive %d TRK %d SECT %d \n", 
              loc, fd400.cur_dsk, TRK, SECT);
            uptr->pos=0; 
        }
        // retrieve read byte from sector buffer
        if (uptr->pos>=BUF_SIZE) {
           sim_debug (DEBUG_write, &fd400_dsk_dev, "Sector overrun - do not read data\n"); 
           return 0; 
        }
        data=p[uptr->pos];
        sim_debug (DEBUG_read, &fd400_dsk_dev, "Read byte %02X (dec=%d char='%c'), Current Drive %d TRK %d SECT %d POS %d\n", 
              data, data, (data < 32) ? '?':data, fd400.cur_dsk, TRK, SECT, uptr->pos);
        uptr->pos++;
        
        return data;
    }
    // io=1 -> writing data to i/o register,
    // write data to sector
    if (uptr->flags & UNIT_RO) {
        sim_debug (DEBUG_write, &fd400_dsk_dev, "Write data %02X, but Current Drive %d is Read Only\n", 
            data, fd400.cur_dsk);
        return 0; 
    }
    sim_debug (DEBUG_write, &fd400_dsk_dev, "Write data %02X, Current Drive %d TRK %d SECT %d POS %d\n", 
        data, fd400.cur_dsk, TRK, SECT, uptr->pos);
    // store byte into sector buffer
    if (uptr->pos==0) {
        if (data==0) return 0; // ignore zero bytes before sync byte
    }
    if (uptr->pos >= BUF_SIZE) {
       sim_debug (DEBUG_write, &fd400_dsk_dev, "Sector overrun - do not write data\n"); 
       return 0; 
    }
    // save byte to buffer
    p[uptr->pos]=data; 
    uptr->pos++;
    // calculate location of current sector in disk image file
    loc=(TRK * NUM_SECT + SECT) * SECT_SIZE; 
    if (loc >= uptr->capac) {
       // writing past image file current size -> extend disk image size
       uint8 buf[SECT_SIZE];
       memset(buf, 0, sizeof(buf));
       sim_fseek(uptr->fileref, uptr->capac, SEEK_SET);
       while (uptr->capac <= loc) {
           sim_fwrite(buf, 1, SECT_SIZE, uptr->fileref); 
           uptr->capac += SECT_SIZE;
       }
       sim_debug (DEBUG_write, &fd400_dsk_dev, "Disk image extended up to %d bytes \n", uptr->capac); 
    } 
    // convert byte stream into sector format to save to disk. 
    // reorganize buffer to match the byte order expected by MiniDOS
    //
    // Data as sent to controller by MiniDOS ROM when writing a sector 
    //     SY TR SE BT BS FT FS NN AH AL TY [NN data bytes] CH CL PH PL
    //     where SY=Sync Byte=$FD
    //           TR SE=This track and sector
    //           BT BS FT FS NN AH AL TY=same as below
    //           CH CL=same as below
    //
    // Data in MiniDOS MPX disk image:
    //     BT BS FT FS NN AH AL TY   CH CL PH PL   [256 data bytes] = 268 bytes
    //     where BT=Backward link track, DS=Backward link sector, 
    //           FT=Forward link track, FS=Forward link sector. = 00 00 if this is last sector of file
    //           NN=Number of data bytes (00=256 bytes)
    //           AH AL=Addr in RAM where to load sector data bytes (H=High byte, L=Low byte)
    //           TY=file type
    //           CH CL=CheckSum Hi/Low byte
    //           PH PL=Postamble Hi/Low byte. Holds program start addr on last sector
    //
    // so we create the sector to write in disk image file (dsk_sect) reordering data from filebuf (p pointer) 

    memset(dsk_sect, 255, sizeof(dsk_sect));
    for (i=0; i<8; i++) dsk_sect[i]=p[3+i]; // disk sector start with 8 byte header with bytes BT BS FT FS NN AH AL TY
    n=uptr->pos-11; // number of data bytes
    if (n>256+4) n=256+4; 
    if (n>4) {
        for (i=0; i<4; i++) dsk_sect[i+8]=p[3+8+n+i-4]; // copy checksum and postamble
        for (i=0; i<n-4; i++) dsk_sect[i+12]=p[3+8+i]; // copy data bytes to disk-write-buffer 
    }
    // does a whole sector save for each byte sent to fdc controller, as there is no an end-of-sector-write signal 
    // This is quite ineficient, but host is fast enought and has clever caching, so no worries
    sim_fseek(uptr->fileref, loc, SEEK_SET);
    sim_fwrite(dsk_sect, 1, SECT_SIZE, uptr->fileref); 
    return 0;
}

/* CURRENT SECTOR / FILL CHAR REGISTER */

int32 fd400_cursect(int32 io, int32 data)
{
    UNIT * uptr; 

    uptr = &fd400_dsk_unit[fd400.cur_dsk]; 
    if ((uptr->flags & UNIT_ATT) == 0) return 0; // not attached
    if (io==0) { 
        // io=0 when reading from io register (return data read from i/o device register)
        // return current sector
        sim_debug (DEBUG_flow, &fd400_dsk_dev, "Current Drive %d TRK %d SECT %d \n", 
            fd400.cur_dsk, TRK, SECT);
        return SECT;
    }
    // io=1 -> writing data to i/o register,
    // set fill char
    fd400.FillChar=data; 
    return 0;
}

/*  RECEIVER RESTART / WRITE PULSE $CC00 */

int32 fd400_startrw(int32 io, int32 data)
{
    UNIT * uptr; 

    uptr = &fd400_dsk_unit[fd400.cur_dsk]; 
    if ((uptr->flags & UNIT_ATT) == 0) return 0; // not attached
    if (io==0) { 
        // io=0 when reading from io register (return data read from i/o device register)
        // start read from sector -> init received so it will return sync char and tracks 
        uptr->pos=0;
        return 0;
    }
    // io=1 -> writing data to i/o register,
    // start write to sector
    uptr->pos=0;
    return 0;
}

t_stat fd400_attach (UNIT * uptr, CONST char * file)
{
    t_stat r; 

    if ((r = attach_unit(uptr, file)) != SCPE_OK) return r;
    uptr->u4 = uptr->u5 = 0;
    uptr->capac = sim_fsize(uptr->fileref);
    uptr->pos = 0; 
    return SCPE_OK;
}

/* end of fd400.c */
