/*  zx-200a.c: ZENDEX single/double density disk adapter

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

        28 Jun 16 - Original file.

    NOTES:

        This controller will mount 4 DD disk images on drives :F0: thru :F3: addressed
        at ports 078H to 07FH.  It also will mount 2 SD disk images on :F4: and :F5: 
        addressed at ports 088H to 08FH.  These are on physical drives :F0: and :F1:.

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

    The ZX-200A appears to the multibus system as if there were an iSBC-201
    installed addressed at 0x88-0x8f and an iSBC-202 installed addressed at 
    0x78-0x7F.  The DD disks are drive 0 - 3.  The SD disks are mapped over
    DD disks 0 - 1.  Thus drive 0 - 1 can be SD or DD, but not both.  Drive
    2 - 3 are always DD.
*/

#include "system_defs.h"                /* system header in system dir */


#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         6

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
#define FDCDD           0x10            //fdc is DD
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
#define MDSSD           256256          //single density FDD size
#define MDSDD           512512          //double density FDD size
#define MAXSECSD        26              //single density last sector
#define MAXSECDD        52              //double density last sector
#define MAXTRK          76              //last track

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern uint8 multibus_get_mbyte(uint16 addr);
extern void multibus_put_mbyte(uint16 addr, uint8 val);

/* external globals */

extern uint16    PCX;

/* internal function prototypes */

t_stat isbc064_cfg(uint16 base);
t_stat zx200a_reset(DEVICE *dptr);
void zx200a_reset1(void);
t_stat zx200a_attach (UNIT *uptr, CONST char *cptr);
t_stat zx200a_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 zx200ar0SD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar0DD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar1SD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar1DD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar2SD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar2DD(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar3(t_bool io, uint8 data, uint8 devnum);
uint8 zx200ar7(t_bool io, uint8 data, uint8 devnum);
void zx200a_diskio(void);

/* globals */

typedef    struct    {                  //FDD definition
//    uint8   *buf;
//    int     t0;
//    int     rdy;
    uint8   sec;
    uint8   cyl;
    uint8   dd;
//    uint8   maxsec;
//    uint8   maxcyl;
    }    FDDDEF;

typedef    struct    {                  //FDC definition
    uint16  baseport;                   //FDC base port
    uint16  iopb;                       //FDC IOPB
    uint8   DDstat;                     //FDC DD status
    uint8   SDstat;                     //FDC SD status
    uint8   rdychg;                     //FDC ready change
    uint8   rtype;                      //FDC result type
    uint8   rbyte0;                     //FDC result byte for type 00
    uint8   rbyte1;                     //FDC result byte for type 10
    uint8   intff;                      //fdc interrupt FF
    FDDDEF  fdd[FDD_NUM];               //indexed by the FDD number
    }    FDCDEF;

FDCDEF    zx200a;

/* ZX-200A Standard I/O Data Structures */

UNIT zx200a_unit[] = {
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSSD), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSSD), 20 }, 
};

REG zx200a_reg[] = {
    { HRDATA (STAT0, zx200a.SDstat, 8) },    /* zx200a 0 SD status */
    { HRDATA (STAT0, zx200a.DDstat, 8) },    /* zx200a 0 DD status */
    { HRDATA (RTYP0, zx200a.rtype, 8) },     /* zx200a 0 result type */
    { HRDATA (RBYT0A, zx200a.rbyte0, 8) },   /* zx200a 0 result byte 0 */
    { HRDATA (RBYT0B, zx200a.rbyte1, 8) },   /* zx200a 0 result byte 1 */
    { HRDATA (INTFF0, zx200a.intff, 8) },    /* zx200a 0 interrupt f/f */
    { NULL }
};

MTAB zx200a_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &zx200a_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &zx200a_set_mode },
    { 0 }
};

DEBTAB zx200a_debug[] = {
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

DEVICE zx200a_dev = {
    "ZX200A",           //name
    zx200a_unit,        //units
    zx200a_reg,         //registers
    zx200a_mod,         //modifiers
    FDD_NUM,            //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    zx200a_reset,       //reset
    NULL,               //boot
    &zx200a_attach,     //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
//    DEBUG_flow + DEBUG_read + DEBUG_write, //dctrl 
    0,                  //dctrl 
    zx200a_debug,       //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* Service routines to handle simulator functions */

// configuration routine

t_stat zx200a_cfg(uint8 base)
{
    int32 i;
    UNIT *uptr;

    sim_printf("    zx200a: at base 0%02XH\n",
        base);
    //register I/O port addresses for each function
    reg_dev(zx200ar0DD, base, 0);       //read status
    reg_dev(zx200ar1DD, base + 1, 0);   //read rslt type/write IOPB addr-l
    reg_dev(zx200ar2DD, base + 2, 0);   //write IOPB addr-h and start 
    reg_dev(zx200ar3, base + 3, 0);     //read rstl byte 
    reg_dev(zx200ar7, base + 7, 0);     //write reset zx200a
    reg_dev(zx200ar0SD, base + 16, 0);  //read status
    reg_dev(zx200ar1SD, base + 17, 0);  //read rslt type/write IOPB addr-l
    reg_dev(zx200ar2SD, base + 18, 0);  //write IOPB addr-h and start 
    reg_dev(zx200ar3, base + 19, 0);    //read rstl byte 
    reg_dev(zx200ar7, base + 23, 0);    //write reset zx200a
    // one-time initialization for all FDDs
    for (i = 0; i < FDD_NUM; i++) { 
        uptr = zx200a_dev.units + i;
        uptr->u6 = i;               //fdd unit number
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat zx200a_reset(DEVICE *dptr)
{
    zx200a_reset1(); //software reset
    return SCPE_OK;
}

/* Software reset routine */

void zx200a_reset1(void)
{
    int32 i;
    UNIT *uptr;

    zx200a.DDstat = 0; //clear the FDC DD status
    zx200a.SDstat = 0; //clear the FDC SD status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = zx200a_dev.units + i;
        zx200a.DDstat |= FDCPRE | FDCDD; //set the FDC DD status
        zx200a.SDstat |= FDCPRE; //set the FDC SD status
        if (i <= 3 ) {             //first 4 are DD, last 2 are SD
            zx200a.fdd[i].dd = 1;   //set double density
        } else {
            zx200a.fdd[i].dd = 0;   //set single density
        }
        zx200a.rtype = ROK;
        zx200a.rbyte0 = 0;              //set no error
        if (uptr->flags & UNIT_ATT) { /* if attached */
            switch(i){
                case 0:
                    zx200a.DDstat |= RDY0; //set FDD 0 ready
                    zx200a.rbyte1 |= RB1RD0;
                    break;
                case 1:
                    zx200a.DDstat |= RDY1; //set FDD 1 ready
                    zx200a.rbyte1 |= RB1RD1;
                    break;
                case 2:
                    zx200a.DDstat |= RDY2; //set FDD 2 ready
                    zx200a.rbyte1 |= RB1RD2;
                    break;
                case 3:
                    zx200a.DDstat |= RDY3; //set FDD 3 ready
                    zx200a.rbyte1 |= RB1RD3;
                    break;
                case 4:
                    zx200a.SDstat |= RDY0; //set FDD 0 ready
                    zx200a.rbyte1 |= RB1RD0;
                    break;
                case 5:
                    zx200a.SDstat |= RDY1; //set FDD 1 ready
                    zx200a.rbyte1 |= RB1RD1;
                    break;
            }
            zx200a.rdychg = 0;
        }
    }
}

/* zx200a attach - attach an .IMG file to a FDD */

t_stat zx200a_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    uint8 fddnum;

    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   zx200a_attach: Attach error %d\n", r);
        return r;
    }
    fddnum = uptr->u6;
    switch(fddnum){
        case 0:
            zx200a.DDstat |= RDY0; //set FDD 0 ready
            zx200a.rbyte1 |= RB1RD0;
            break;
        case 1:
            zx200a.DDstat |= RDY1; //set FDD 1 ready
            zx200a.rbyte1 |= RB1RD1;
            break;
        case 2:
            zx200a.DDstat |= RDY2; //set FDD 2 ready
            zx200a.rbyte1 |= RB1RD2;
            break;
        case 3:
            zx200a.DDstat |= RDY3; //set FDD 3 ready
            zx200a.rbyte1 |= RB1RD3;
            break;
        case 4:
            zx200a.SDstat |= RDY0; //set FDD 0 ready
            zx200a.rbyte1 |= RB1RD0;
            break;
        case 5:
            zx200a.SDstat |= RDY1; //set FDD 1 ready
            zx200a.rbyte1 |= RB1RD1;
            break;
        }
    zx200a.rtype = ROK;
    zx200a.rbyte0 = 0;              //set no error
    return SCPE_OK;
}

/* zx200a set mode = Write protect */

t_stat zx200a_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

/* zx200a control port functions */

uint8 zx200ar0SD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read ststus*/
        return zx200a.SDstat;
    }
    return 0;
}

uint8 zx200ar0DD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read ststus*/
        return zx200a.DDstat;
    }
    return 0;
}

uint8 zx200ar1SD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read operation */
        zx200a.intff = 0;               //clear interrupt FF
        if (zx200a.intff)
            zx200a.SDstat &= ~FDCINT;
        zx200a.rtype = ROK;
        return zx200a.rtype;
    } else {                            /* write control port */
        zx200a.iopb = data;
    }
    return 0;
}

uint8 zx200ar1DD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read operation */
        zx200a.intff = 0;               //clear interrupt FF
        if (zx200a.intff)
            zx200a.DDstat &= ~FDCINT;
        if (zx200a.rdychg) {
            zx200a.rtype = ROK;
            return zx200a.rtype;
        } else {
            zx200a.rtype = ROK;
            return zx200a.rtype;
        }
    } else {                            /* write control port */
        zx200a.iopb = data;
    }
    return 0;
}

uint8 zx200ar2SD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        zx200a.iopb |= (data << 8);
        zx200a_diskio();
        if (zx200a.intff)
            zx200a.SDstat |= FDCINT;
    }
    return 0;
}

uint8 zx200ar2DD(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        zx200a.iopb |= (data << 8);
        zx200a_diskio();
        if (zx200a.intff)
            zx200a.DDstat |= FDCINT;
    }
    return 0;
}

uint8 zx200ar3(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        if (zx200a.rtype == 0) {
            return zx200a.rbyte0;
        } else {
            if (zx200a.rdychg) {
                return zx200a.rbyte1;
            } else {
                return zx200a.rbyte0;
            }
        }
    } else {                        /* write data port */
        ; //stop diskette operation
    }
    return 0;
}

/* reset ZX-200A */
uint8 zx200ar7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        zx200a_reset1();
    }
    return 0;
}

// perform the actual disk I/O operation

void zx200a_diskio(void)
{
    uint8 cw, di, nr, ta, sa, data, nrptr;
    uint16 ba;
    uint32 dskoff;
    uint8 fddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    uint8 *fbuf;

    //parse the IOPB 
    cw = multibus_get_mbyte(zx200a.iopb);
    di = multibus_get_mbyte(zx200a.iopb + 1);
    nr = multibus_get_mbyte(zx200a.iopb + 2);
    ta = multibus_get_mbyte(zx200a.iopb + 3);
    sa = multibus_get_mbyte(zx200a.iopb + 4);
    ba = multibus_get_mbyte(zx200a.iopb + 5);
    ba |= (multibus_get_mbyte(zx200a.iopb + 6) << 8);
    fddnum = (di & 0x30) >> 4;
    uptr = zx200a_dev.units + fddnum;
    fbuf = (uint8 *) uptr->filebuf;
    //check for not ready
    switch(fddnum) {
        case 0:
            if ((zx200a.DDstat & RDY0) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
        case 1:
            if ((zx200a.DDstat & RDY1) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
        case 2:
            if ((zx200a.DDstat & RDY2) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
        case 3:
            if ((zx200a.DDstat & RDY3) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
        case 4:
            if ((zx200a.SDstat & RDY0) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
        case 5:
            if ((zx200a.SDstat & RDY1) == 0) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0NR;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Ready error on drive %d", fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (zx200a.fdd[fddnum].dd == 1) {
        if (
            ((di & 0x07) != DHOME) && (
            (sa > MAXSECDD) ||
            ((sa + nr) > (MAXSECDD + 1)) ||
            (sa == 0) ||
            (ta > MAXTRK)
            )) {
            zx200a.rtype = ROK;
            zx200a.rbyte0 = RB0ADR;
            zx200a.intff = 1;      //set interrupt FF
            sim_printf("\n   ZX200A: FDD %d - Address error sa=%02X nr=%02X ta=%02X PCX=%04X",
                fddnum, sa, nr, ta, PCX);
            return;
        }
    } else {
        if (
            ((di & 0x07) != DHOME) && (
            (sa > MAXSECSD) ||
            ((sa + nr) > (MAXSECSD + 1)) ||
            (sa == 0) ||
            (ta > MAXTRK)
            )) {
            zx200a.rtype = ROK;
            zx200a.rbyte0 = RB0ADR;
            zx200a.intff = 1;      //set interrupt FF
            sim_printf("\n   ZX200A: FDD %d - Address error sa=%02X nr=%02X ta=%02X PCX=%04X",
                fddnum, sa, nr, ta, PCX);
            return;
        }
    }
    switch (di & 0x07) {
        case DNOP:
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DSEEK:
            zx200a.fdd[fddnum].sec = sa;
            zx200a.fdd[fddnum].cyl = ta;
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DHOME:
            zx200a.fdd[fddnum].sec = sa;
            zx200a.fdd[fddnum].cyl = 0;
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DVCRC:
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DFMT:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0WP;
                zx200a.intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a: Write protect error 1 on drive %d", fddnum);
                return;
            }
            fmtb = multibus_get_mbyte(ba); //get the format byte
            if (zx200a.fdd[fddnum].dd == 1) {
                //calculate offset into DD disk image
                dskoff = ((ta * MAXSECDD) + (sa - 1)) * 128;
                for(i=0; i<=((uint32)(MAXSECDD) * 128); i++) {
                    *(fbuf + (dskoff + i)) = fmtb;
                }
            } else {
                //calculate offset into SD disk image
                dskoff = ((ta * MAXSECSD) + (sa - 1)) * 128;
                for(i=0; i<=((uint32)(MAXSECSD) * 128); i++) {
                    *(fbuf + (dskoff + i)) = fmtb;
                }
            }
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                if (zx200a.fdd[fddnum].dd == 1) {
                    dskoff = ((ta * MAXSECDD) + (sa - 1)) * 128;
                } else {
                    dskoff = ((ta * MAXSECSD) + (sa - 1)) * 128;
                }
                //copy sector from image to RAM
                for (i=0; i<128; i++) { 
                    data = *(fbuf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                zx200a.rtype = ROK;
                zx200a.rbyte0 = RB0WP;
                zx200a.intff = 1;       //set interrupt FF
                sim_printf("\n   zx200a: Write protect error 2 on drive %d", fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                if (zx200a.fdd[fddnum].dd == 1) {
                    dskoff = ((ta * MAXSECDD) + (sa - 1)) * 128;
                } else {
                    dskoff = ((ta * MAXSECSD) + (sa - 1)) * 128;
                }
                for (i=0; i<128; i++) { //copy sector from image to RAM
                    data = multibus_get_mbyte(ba + i);
                    *(fbuf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            zx200a.rtype = ROK;
            zx200a.rbyte0 = 0;          //set no error
            zx200a.intff = 1;           //set interrupt FF
            break;
        default:
            sim_printf("\n   zx200a: zx200a_diskio bad di=%02X", di & 0x07);
            break;
    }
}

/* end of zx-200a.c */
