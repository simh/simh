/*  isbc202.c: Intel double density disk adapter

    Copyright (c) 2016, William A. Beech

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

        27 Jun 16 - Original file.

    NOTES:

        This controller will mount 4 DD disk images on drives :F0: thru :F3: addressed
        at ports 078H to 07FH.  

    Registers:

        078H - Read - Subsystem status
            bit 0 - ready status of drive 0
            bit 1 - ready status of drive 1
            bit 2 - state of channel's interrupt FF
            bit 3 - controller presence indicator
            bit 4 - DD controller presence indicator
            bit 5 - ready status of drive 2
            bit 6 - ready status of drive 3
            bit 7 - zero

        079H - Read - Read result type (bits 2-7 are zero)
            00 - I/O complete with error
            01 - Reserved
            10 - Result byte contains diskette ready status
            11 - Reserved
        079H - Write - IOPB address low byte.

        07AH - Write - IOPB address high byte and start operation.

        07BH - Read - Read result byte
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

        07FH - Write - Reset diskette system.

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
            bit 3 - data word length (=8-bit, 1=16-bit)
            bit 4-5 - interrupt control
                00 - I/O complete interrupt to be issued
                01 - I/O complete interrupts are disabled
                10 - illegal code
                11 - illegal code
            bit 6- randon format sequence

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

        Byte 4 - Track Address

        Byte 5 - Sector Address

        Byte 6 - Buffer Low Address

        Byte 7 - Buffer High Address

        u3 -
        u4 -
        u5 - 
        u6 - fdd number.

*/

#include "system_defs.h"                /* system header in system dir */

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         4
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
#define FDCDD           0x10            //FDC is DD
#define RDY2            0x20            //FDD 2 ready
#define RDY3            0x40            //FDD 3 ready

//result type
#define ROK             0x00            //FDC error
#define RCHG            0x02            //FDC OK OR disk changed

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
#define RB1RD2          0x10            //drive 2 ready
#define RB1RD3          0x20            //drive 3 ready
#define RB1RD0          0x40            //drive 0 ready
#define RB1RD1          0x80            //drive 1 ready

//disk geometry values
#define MDSDD           512512          //double density FDD size
#define MAXSECDD        52              //double density last sector
#define MAXTRK          76              //last track

/* external globals */

extern uint16    PCX;

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern uint8 multibus_get_mbyte(uint16 addr);
extern void multibus_put_mbyte(uint16 addr, uint8 val);

/* function prototypes */

t_stat isbc202_cfg(uint8 base);
t_stat isbc202_reset(DEVICE *dptr);
void isbc202_reset_dev(void);
t_stat isbc202_attach (UNIT *uptr, CONST char *cptr);
t_stat isbc202_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 isbc202r0(t_bool io, uint8 data, uint8 devnum); /* isbc202 0 */
uint8 isbc202r1(t_bool io, uint8 data, uint8 devnum); /* isbc202 1 */
uint8 isbc202r2(t_bool io, uint8 data, uint8 devnum); /* isbc202 2 */
uint8 isbc202r3(t_bool io, uint8 data, uint8 devnum); /* isbc202 3 */
uint8 isbc202r7(t_bool io, uint8 data, uint8 devnum); /* isbc202 7 */
void isbc202_diskio(void);      //do actual disk i/o

/* globals */

typedef    struct    {                  //FDD definition
    uint8   sec;
    uint8   cyl;
    }    FDDDEF;

typedef    struct    {                  //FDC definition
//    uint16  baseport;                   //FDC base port
    uint16  iopb;                       //FDC IOPB
    uint8   stat;                       //FDC status
    uint8   rdychg;                     //FDC ready change
    uint8   rtype;                      //FDC result type
    uint8   rbyte0;                     //FDC result byte for type 00
    uint8   rbyte1;                     //FDC result byte for type 10
    uint8   intff;                      //fdc interrupt FF
    FDDDEF  fdd[FDD_NUM];               //indexed by the FDD number
    }    FDCDEF;

FDCDEF    fdc202;              //indexed by the isbc-202 instance number

/* isbc202 Standard I/O Data Structures */

UNIT isbc202_unit[] = { // 4 FDDs
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { NULL }
};

REG isbc202_reg[] = {
    { HRDATA (STAT0, fdc202.stat, 8) },      /* isbc202 status */
    { HRDATA (RTYP0, fdc202.rtype, 8) },     /* isbc202 result type */
    { HRDATA (RBYT0A, fdc202.rbyte0, 8) },   /* isbc202 result byte 0 */
    { HRDATA (RBYT0B, fdc202.rbyte1, 8) },   /* isbc202 result byte 1 */
    { HRDATA (INTFF0, fdc202.intff, 8) },    /* isbc202 interrupt f/f */
    { NULL }
};

MTAB isbc202_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &isbc202_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &isbc202_set_mode },
    { 0 }
};

DEBTAB isbc202_debug[] = {
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

DEVICE isbc202_dev = {
    "SBC202",           //name
    isbc202_unit,       //units
    isbc202_reg,        //registers
    isbc202_mod,        //modifiers
    FDD_NUM,            //numunits 
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    isbc202_reset,      //reset
    NULL,               //boot
    &isbc202_attach,    //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl 
    isbc202_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

// configuration routine

t_stat isbc202_cfg(uint8 base)
{
    int32 i;
    UNIT *uptr;

    sim_printf("    sbc202: at base 0%02XH\n",
        base);
    reg_dev(isbc202r0, base, 0);         //read status
    reg_dev(isbc202r1, base + 1, 0);     //read rslt type/write IOPB addr-l
    reg_dev(isbc202r2, base + 2, 0);     //write IOPB addr-h and start 
    reg_dev(isbc202r3, base + 3, 0);     //read rstl byte 
    reg_dev(isbc202r7, base + 7, 0);     //write reset fdc201
    // one-time initialization for all FDDs for this FDC instance
    for (i = 0; i < FDD_NUM; i++) { 
        uptr = isbc202_dev.units + i;
        uptr->u6 = i;               //fdd unit number
    }
    return SCPE_OK;
}

/* Hardware reset routine */

t_stat isbc202_reset(DEVICE *dptr)
{
    isbc202_reset_dev(); //software reset
    return SCPE_OK;
}

/* Software reset routine */

void isbc202_reset_dev(void)
{
    int32 i;
    UNIT *uptr;

    fdc202.stat = 0;            //clear status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = isbc202_dev.units + i;
        fdc202.stat |= FDCPRE | FDCDD; //set the FDC status
        fdc202.rtype = ROK;
        fdc202.rbyte0 = 0;              //set no error
        if (uptr->flags & UNIT_ATT) { /* if attached */
            switch(i){
                case 0:
                    fdc202.stat |= RDY0; //set FDD 0 ready
                    fdc202.rbyte1 |= RB1RD0;
                    break;
                case 1:
                    fdc202.stat |= RDY1; //set FDD 1 ready
                    fdc202.rbyte1 |= RB1RD1;
                    break;
                case 2:
                    fdc202.stat |= RDY2; //set FDD 2 ready
                    fdc202.rbyte1 |= RB1RD2;
                    break;
                case 3:
                    fdc202.stat |= RDY3; //set FDD 3 ready
                    fdc202.rbyte1 |= RB1RD3;
                    break;
            }
            fdc202.rdychg = 0;
        }
    }
}

/* isbc202 attach - attach an .IMG file to an FDD */

t_stat isbc202_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    uint8 fddnum;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   isbc202_attach: Attach error %d\n", r);
        return r;
    }
    fddnum = uptr->u6;
    switch(fddnum){
        case 0:
            fdc202.stat |= RDY0; //set FDD 0 ready
            fdc202.rbyte1 |= RB1RD0;
            break;
        case 1:
            fdc202.stat |= RDY1; //set FDD 1 ready
            fdc202.rbyte1 |= RB1RD1;
            break;
        case 2:
            fdc202.stat |= RDY2; //set FDD 2 ready
            fdc202.rbyte1 |= RB1RD2;
            break;
        case 3:
            fdc202.stat |= RDY3; //set FDD 3 ready
            fdc202.rbyte1 |= RB1RD3;
            break;
    }
    fdc202.rtype = ROK;
    fdc202.rbyte0 = 0;              //set no error
    return SCPE_OK;
}

/* isbc202 set mode = Write protect */

t_stat isbc202_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

/* iSBC202 control port functions */

uint8 isbc202r0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read ststus*/
        return fdc202.stat;
    }
    return 0;
}

uint8 isbc202r1(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        fdc202.intff = 0;           //clear interrupt FF
        fdc202.stat &= ~FDCINT;
        fdc202.rtype = ROK;
        return fdc202.rtype;
    } else {                        /* write data port */
        fdc202.iopb = data;
    }
    return 0;
}

uint8 isbc202r2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        fdc202.iopb |= (data << 8);
        isbc202_diskio();
        if (fdc202.intff)
            fdc202.stat |= FDCINT;
    }
    return 0;
}

uint8 isbc202r3(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        if (fdc202.rtype == ROK) {
            return fdc202.rbyte0;
        } else {
            if (fdc202.rdychg) {
                return fdc202.rbyte1;
            } else {
                return fdc202.rbyte0;
            }
        }
    } else {                        /* write data port */
        ; //stop diskette operation
    }
    return 0;
}

uint8 isbc202r7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        isbc202_reset_dev();
    }
    return 0;
}

// perform the actual disk I/O operation

void isbc202_diskio(void)
{
    uint8 cw, di, nr, ta, sa, data, nrptr;
    uint16 ba;
    uint32 dskoff;
    uint8 fddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    uint8 *fbuf;

    //parse the IOPB 
    cw = multibus_get_mbyte(fdc202.iopb);
    di = multibus_get_mbyte(fdc202.iopb + 1);
    nr = multibus_get_mbyte(fdc202.iopb + 2);
    ta = multibus_get_mbyte(fdc202.iopb + 3);
    sa = multibus_get_mbyte(fdc202.iopb + 4);
    ba = multibus_get_mbyte(fdc202.iopb + 5);
    ba |= (multibus_get_mbyte(fdc202.iopb + 6) << 8);
    fddnum = (di & 0x30) >> 4;
    uptr = isbc202_dev.units + fddnum;
    fbuf = (uint8 *) uptr->filebuf;
    //check for not ready
    switch(fddnum) {
        case 0:
            if ((fdc202.stat & RDY0) == 0) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0NR;
                fdc202.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Ready error", fddnum);
                return;
            }
            break;
        case 1:
            if ((fdc202.stat & RDY1) == 0) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0NR;
                fdc202.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Ready error", fddnum);
                return;
            }
            break;
        case 2:
            if ((fdc202.stat & RDY2) == 0) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0NR;
                fdc202.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Ready error", fddnum);
                return;
            }
            break;
        case 3:
            if ((fdc202.stat & RDY3) == 0) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0NR;
                fdc202.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Ready error", fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        ((di & 0x07) != DHOME) && (
        (sa > MAXSECDD) ||
        ((sa + nr) > (MAXSECDD + 1)) ||
        (sa == 0) ||
        (ta > MAXTRK)
        )) {
        fdc202.rtype = ROK;
        fdc202.rbyte0 = RB0ADR;
        fdc202.intff = 1;      //set interrupt FF
        sim_printf("\n   SBC202: FDD %d - Address error sa=%02X nr=%02X ta=%02X PCX=%04X",
            fddnum, sa, nr, ta, PCX);
         return;
    }
    switch (di & 0x07) {
        case DNOP:
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DSEEK:
            fdc202.fdd[fddnum].sec = sa;
            fdc202.fdd[fddnum].cyl = ta;
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DHOME:
            fdc202.fdd[fddnum].sec = sa;
            fdc202.fdd[fddnum].cyl = 0;
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DVCRC:
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DFMT:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0WP;
                fdc202.intff = 1;       //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Write protect error DFMT", fddnum);
                return;
            }
            fmtb = multibus_get_mbyte(ba); //get the format byte
            //calculate offset into disk image
            dskoff = ((ta * MAXSECDD) + (sa - 1)) * SECSIZ;
            for(i=0; i<=((uint32)(MAXSECDD) * SECSIZ); i++) {
                *(fbuf + (dskoff + i)) = fmtb;
            }
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECDD) + (sa - 1)) * SECSIZ;
                //copy sector from disk image to RAM
                for (i=0; i<SECSIZ; i++) { 
                    data = *(fbuf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc202.rtype = ROK;
                fdc202.rbyte0 = RB0WP;
                fdc202.intff = 1;       //set interrupt FF
                sim_printf("\n   SBC202: FDD %d - Write protect error DWRITE", fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECDD) + (sa - 1)) * SECSIZ;
                //copy sector from RAM to disk image
                for (i=0; i<SECSIZ; i++) { 
                    data = multibus_get_mbyte(ba + i);
                    *(fbuf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc202.rtype = ROK;
            fdc202.rbyte0 = 0;          //set no error
            fdc202.intff = 1;           //set interrupt FF
            break;
        default:
            sim_printf("\n   SBC202: FDD %d - isbc202_diskio bad di=%02X", fddnum, di & 0x07);
            break;
    }
}

/* end of isbc202.c */
