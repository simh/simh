/*  isbc201.c: Intel single density disk adapter adapter

    Copyright (c) 2010, William A. Beech

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
        WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
        IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
        CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

        Except as contained in this notice, the name of William A. Beech shall not be
        used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        31 Oct 16 - Original file.

    NOTES:

        This controller will mount 2 SD disk images on drives :F0: and :F1: addressed
        at ports 088H to 08FH.  

    Registers:

        088H - Read - Subsystem status
            bit 0 - ready status of drive 0
            bit 1 - ready status of drive 1
            bit 2 - state of channel's interrupt FF
            bit 3 - controller presence indicator
            bit 4 - zero
            bit 5 - zero
            bit 6 - zero
            bit 7 - zero

        089H - Read - Read result type (bits 2-7 are zero)
            00 - I/O complete with error(unlinked)
            01 - I/O complete with error(linked)(hi 6-bits are block num)
            10 - Result byte contains diskette ready status
            11 - Reserved
        089H - Write - IOPB address low byte.

        08AH - Write - IOPB address high byte and start operation.

        08BH - Read - Read result byte
            If result type is 00H
            bit 0 - deleted record
            bit 1 - CRC error
            bit 2 - seek error
            bit 3 - address error
            bit 4 - data overrun/underrun
            bit 5 - write protect
            bit 6 - write error
            bit 7 - not ready
            If result type is 02H and ready has changed
            bit 0 - zero
            bit 1 - zero
            bit 2 - zero
            bit 3 - zero
            bit 4 - drive 2 ready
            bit 5 - drive 3 ready
            bit 6 - drive 0 ready
            bit 7 - drive 1 ready
            else return 0

        08FH - Write - Reset diskette system.

    Operations:
        NOP - 0x00
        Seek - 0x01
        Format Track - 0x02
        Recalibrate - 0x03
        Read Data - 0x04
        Verify CRC - 0x05
        Write Data - 0x06
        Write Deleted Data - 0x07

    IOPB - I/O Parameter Block
        Byte 0 - Channel Word
            bit 0 - wait
            bit 1 - branch on wait
            bit 2 - successor bit
            bit 3 - data word length (=8-bit, 1=16-bit)
            bit 4-5 - interrupt control
                00 - I/O complete interrupt to be issued
                01 - I/O complete interrupts are disabled
                10 - illegal code
                11 - illegal code
            bit 6 - randon format sequence
            bit 7 - lock override

        Byte 1 - Diskette Instruction
            bit 0-2 - operation code
                000 - no operation
                001 - seek
                010 - format track
                011 - recalibrate
                100 - read data
                101 - verify CRC
                110 - write data
                111 - write deleted data
            bit 3 - data word length ( same as byte-0, bit-3)
            bit 4-5 - unit select
                00 - drive 0
                01 - drive 1
                10 - drive 2
                11 - drive 3
            bit 6-7 - reserved (zero)

        Byte 2 - Number of Records

        Byte 3 - Track Address

        Byte 4 - Sector Address

        Byte 5 - Buffer Low Address

        Byte 6 - Buffer High Address

        Byte 8 - Block Number

        Byte 9 - Next IOPB Lower Address

        Byte 10 - Next IOPB Upper Address

        u3 -
        u4 -
        u5 -
        u6 - fdd number.

 SSSD - Bootable
    This is actually an IBM 3740 format disk.  It has 77 tracks of 26 SD
    sectors of 128 bytes each for a total of 2002 sectors.  The disk layout 
    of the first 6 tracks of the SSSD 256256 byte disk image is as follows:

    File        Link Blk Addr     Data Blk Addr
                                  From        To
                    Trk   Sec    Trk Sec     Trk Sec
    ISIS.T0         000h 018h   000h 001h   000h 017h   bin file    0B80    0000    0B00
    ISIS.DIR        001h 001h   001h 002h   001h 01Ah               0D00    0D80    1980
    ISIS.MAP        002h 001h   002h 002h   002h 003h               1A00    1A80    1B00
    ISIS.LAB        000h 019h   000h 01Ah   000h 01Ah               0C00    0C80    0C80
    ISIS.BIN        002h 004h   002h 005h   004h 00Eh   pkd file    1B80    1C00    3A80
                    004h 00Fh   004h 010h   005h 013h               3B00    3B80    4A00
    ISIS.CLI        005h 014h   005h 015h   006h 00Dh   reg file    4A80    4B00    5480
    NEXT BLK        006h 00Eh                                               5500

*/

#include "system_defs.h"                /* system header in system dir */

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         2
#define SECSIZ          128                     

//disk controoler operations
#define DNOP            0x00            //disk no operation
#define DSEEK           0x01            //disk seek
#define DFMT            0x02            //disk format
#define DHOME           0x03            //disk home
#define DREAD           0x04            //disk read
#define DVCRC           0x05            //disk verify CRC
#define DWRITE          0x06            //disk write

//status
#define RDY0            0x01            //FDD 0 ready
#define RDY1            0x02            //FDD 1 ready
#define FDCINT          0x04            //FDC interrupt flag
#define FDCPRE          0x08            //FDC board present

//result type
#define ROK             0x00            //FDC OK OR ERROR
#define RCHG            0x02            //FDC DISK CHANGED

// If result type is ROK then rbyte is
#define RB0DR           0x01            //deleted record
#define RB0CRC          0x02            //CRC error
#define RB0SEK          0x04            //seek error
#define RB0ADR          0x08            //address error
#define RB0OU           0x10            //data overrun/underrun
#define RB0WP           0x20            //write protect
#define RB0WE           0x40            //write error
#define RB0NR           0x80            //not ready

// If result type is RCHG then rbyte is
#define RB1RD0          0x40            //drive 0 ready
#define RB1RD1          0x80            //drive 1 ready

//disk geometry values
#define MDSSD           256256          //single density FDD size
#define MAXSECSD        26              //single density last sector
#define MAXTRK          76              //last track

/* external globals */

extern uint16    PCX;

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern uint8 multibus_get_mbyte(uint16 addr);
extern void multibus_put_mbyte(uint16 addr, uint8 val);

/* function prototypes */

t_stat isbc201_cfg(uint8 base);
t_stat isbc201_reset(DEVICE *dptr);
void isbc201_reset1(void);
t_stat isbc201_attach (UNIT *uptr, CONST char *cptr);
t_stat isbc201_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 isbc201r0(t_bool io, uint8 data, uint8 devnum);  /* isbc201 0 */
uint8 isbc201r1(t_bool io, uint8 data, uint8 devnum);  /* isbc201 1 */
uint8 isbc201r2(t_bool io, uint8 data, uint8 devnum);  /* isbc201 2 */
uint8 isbc201r3(t_bool io, uint8 data, uint8 devnum);  /* isbc201 3 */
uint8 isbc201r7(t_bool io, uint8 data, uint8 devnum);  /* isbc201 7 */
void isbc201_diskio(void);      //do actual disk i/o

/* globals */

typedef    struct    {                  //FDD definition
    uint8   sec;
    uint8   cyl;
    }    FDDDEF;

typedef    struct    {                  //FDC definition
    uint16  iopb;                       //FDC IOPB
    uint8   stat;                       //FDC status
    uint8   rdychg;                     //FDC ready changed
    uint8   rtype;                      //FDC result type
    uint8   rbyte0;                     //FDC result byte for type 00
    uint8   rbyte1;                     //FDC result byte for type 10
    uint8   intff;                      //fdc interrupt FF
    FDDDEF  fdd[FDD_NUM];               //indexed by the FDD number
    }    FDCDEF;

FDCDEF    fdc201;  

/* isbc201 Standard I/O Data Structures */

UNIT isbc201_unit[] = {                 //2 FDDs
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSSD) }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF, MDSSD) } 
};

REG isbc201_reg[] = {
    { HRDATA (STAT0, fdc201.stat, 8) },      //fdc201 0 status
    { HRDATA (RTYP0, fdc201.rtype, 8) },     //fdc201 0 result type
    { HRDATA (RBYT0A, fdc201.rbyte0, 8) },   //fdc201 0 result byte 0
    { HRDATA (RBYT0B, fdc201.rbyte1, 8) },   //fdc201 0 result byte 1
    { HRDATA (INTFF0, fdc201.intff, 8) },    //fdc201 0 interrupt f/f
    { NULL }
};

MTAB isbc201_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &isbc201_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &isbc201_set_mode },
    { 0 }
};

DEBTAB isbc201_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE isbc201_dev = {
    "SBC201",           //name
    isbc201_unit,       //units
    isbc201_reg,        //registers
    isbc201_mod,        //modifiers
    FDD_NUM,            //numunits 
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    isbc201_reset,      //reset
    NULL,               //boot
    &isbc201_attach,    //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl 
    isbc201_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

// configuration routine

t_stat isbc201_cfg(uint8 base)
{
    int32 i;
    UNIT *uptr;

    sim_printf("    sbc201: at base 0%02XH\n",
        base);
    reg_dev(isbc201r0, base, 0);         //read status
    reg_dev(isbc201r1, base + 1, 0);     //read rslt type/write IOPB addr-l
    reg_dev(isbc201r2, base + 2, 0);     //write IOPB addr-h and start 
    reg_dev(isbc201r3, base + 3, 0);     //read rstl byte 
    reg_dev(isbc201r7, base + 7, 0);     //write reset fdc201
    // one-time initialization for all FDDs for this FDC instance
    for (i = 0; i < FDD_NUM; i++) { 
        uptr = isbc201_dev.units + i;
        uptr->u6 = i;               //fdd unit number
    }
    return SCPE_OK;
}
/* Hardware reset routine */

t_stat isbc201_reset(DEVICE *dptr)
{
    isbc201_reset1();
    return SCPE_OK;
}

/* Software reset routine */

void isbc201_reset1(void)
{
    int32 i;
    UNIT *uptr;

    fdc201.stat = 0;            //clear status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = isbc201_dev.units + i;
        fdc201.stat |= FDCPRE; //set the FDC status
        fdc201.rtype = ROK;
        fdc201.rbyte0 = 0;              //set no error
        if (uptr->flags & UNIT_ATT) { /* if attached */
            switch(i){
                case 0:
                    fdc201.stat |= RDY0; //set FDD 0 ready
                    fdc201.rbyte1 |= RB1RD0;
                    break;
                case 1:
                    fdc201.stat |= RDY1; //set FDD 1 ready
                    fdc201.rbyte1 |= RB1RD1;
                    break;
            }
            fdc201.rdychg = 0;
        }
    }
}

/* fdc201 attach - attach an .IMG file to a FDD */

t_stat isbc201_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    uint8 fddnum;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   SBC201_attach: Attach error %d\n", r);
        return r;
    }
    fddnum = uptr->u6;
    switch(fddnum){
        case 0:
            fdc201.stat |= RDY0; //set FDD 0 ready
            fdc201.rbyte1 |= RB1RD0;
            break;
        case 1:
            fdc201.stat |= RDY1; //set FDD 1 ready
            fdc201.rbyte1 |= RB1RD1;
            break;
    }
    fdc201.rtype = ROK;
    fdc201.rbyte0 = 0;
    return SCPE_OK;
}

/* fdc201 set mode = Write protect */

t_stat isbc201_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (uptr->flags & UNIT_ATT)
        return sim_messagef (SCPE_ALATT, "%s is already attached to %s\n", sim_uname(uptr), uptr->filename);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
    } else {                            /* read write */
        uptr->flags &= ~val;
    }
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* ISBC201 control port functions */

uint8 isbc201r0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read status*/
        return fdc201.stat;
        }
    return 0;
}

uint8 isbc201r1(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        fdc201.intff = 0;           //clear interrupt FF
        fdc201.stat &= ~FDCINT;
        return fdc201.rtype;
    } else {                        /* write data port */
        fdc201.iopb = data;
        }
    return 0;
}

uint8 isbc201r2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        fdc201.iopb |= (data << 8);
        isbc201_diskio();
        if (fdc201.intff)
            fdc201.stat |= FDCINT;
        }
    return 0;
}

uint8 isbc201r3(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        if (fdc201.rtype == ROK) {
            return fdc201.rbyte0;
        } else {
            if (fdc201.rdychg) {
                return fdc201.rbyte1;
            } else {
                return fdc201.rbyte0;
            }
        }
    } else {                        /* write data port */
        ; //stop diskette operation
    }
    return 0;
}

uint8 isbc201r7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        isbc201_reset1();
    }
    return 0;
}

// perform the actual disk I/O operation

void isbc201_diskio(void)
{
    uint8 cw, di, nr, ta, sa, bn, data, nrptr;
    uint16 ba, ni;
    uint32 dskoff;
    uint8 fddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    uint8 *fbuf;

    //parse the IOPB
    cw = multibus_get_mbyte(fdc201.iopb);
    di = multibus_get_mbyte(fdc201.iopb + 1);
    nr = multibus_get_mbyte(fdc201.iopb + 2);
    ta = multibus_get_mbyte(fdc201.iopb + 3);
    sa = multibus_get_mbyte(fdc201.iopb + 4) & 0x1f;
    ba = multibus_get_mbyte(fdc201.iopb + 5);
    ba |= (multibus_get_mbyte(fdc201.iopb + 6) << 8);
    bn = multibus_get_mbyte(fdc201.iopb + 7);
    ni = multibus_get_mbyte(fdc201.iopb + 8);
    ni |= (multibus_get_mbyte(fdc201.iopb + 9) << 8);
    fddnum = (di & 0x10) >> 4;
    uptr = isbc201_dev.units + fddnum;
    fbuf = (uint8 *) (isbc201_dev.units + fddnum)->filebuf;
    //check for not ready
      switch(fddnum) {
        case 0:
            if ((fdc201.stat & RDY0) == 0) {
                fdc201.rtype = ROK;
                fdc201.rbyte0 = RB0NR;
                fdc201.intff = 1; //set interrupt FF
                sim_printf("\n   SBC201: FDD %d - Ready error", fddnum);
                return;
            }
            break;
        case 1:
            if ((fdc201.stat & RDY1) == 0) {
                fdc201.rtype = ROK;
                fdc201.rbyte0 = RB0NR;
                fdc201.intff = 1; //set interrupt FF
                sim_printf("\n   SBC201: FDD %d - Ready error", fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        ((di & 0x07) != DHOME) && (
        (sa > MAXSECSD) ||
        ((sa + nr) > (MAXSECSD + 1)) ||
        (sa == 0) ||
        (ta > MAXTRK)
        )) {
        fdc201.rtype = ROK;
        fdc201.rbyte0 = RB0ADR;
        fdc201.intff = 1;      //set interrupt FF
        sim_printf("\n   SBC201: FDD %d - Address error at %04X", fddnum, PCX);
        return;
    }
    switch (di & 0x07) {
        case DNOP:
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DSEEK:
            fdc201.fdd[fddnum].sec = sa;
            fdc201.fdd[fddnum].cyl = ta;
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DHOME:
            fdc201.fdd[fddnum].sec = sa;
            fdc201.fdd[fddnum].cyl = 0;
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DVCRC:
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DFMT:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc201.rtype = ROK;
                fdc201.rbyte0 = RB0WP;
                fdc201.intff = 1; //set interrupt FF
                sim_printf("\n   SBC201: FDD %d - Write protect error 1", fddnum);
                return;
            }
            fmtb = multibus_get_mbyte(ba); //get the format byte
            //calculate offset into disk image
            dskoff = ((ta * MAXSECSD) + (sa - 1)) * SECSIZ;
            for(i=0; i<=((uint32)(MAXSECSD) * SECSIZ); i++) {
                *(fbuf + (dskoff + i)) = fmtb;
            }
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECSD) + (sa - 1)) * SECSIZ;
                //copy sector from image to RAM
                for (i=0; i<SECSIZ; i++) { 
                    data = *(fbuf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc201.rtype = ROK;
                fdc201.rbyte0 = RB0WP;
                fdc201.intff = 1; //set interrupt FF
                sim_printf("\n   SBC201: FDD %d - Write protect error 2", fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECSD) + (sa - 1)) * SECSIZ;
                for (i=0; i<SECSIZ; i++) { //copy sector from image to RAM
                    data = multibus_get_mbyte(ba + i);
                    *(fbuf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc201.rtype = ROK;
            fdc201.rbyte0 = 0;
            fdc201.intff = 1;   //set interrupt FF
            break;
        default:
            sim_printf("\n   SBC201: FDD %d - isbc201_diskio bad di=%02X", fddnum, di & 0x07);
            break;
    }
}

/* end of isbc201.c */
