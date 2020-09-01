/*  isbc206.c: Intel iSBC 206 disk adapter

    Copyright (c) 2017, William A. Beech

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

        31 Oct 17 - Original file.

    NOTES:

        This controller will mount 1 Hard Disk removable and three Hard Disk fixed disk 
        images on drives :F0: thru :F3: addressed at ports 068H to 06FH.  

    Registers:

        068H - Read - Subsystem status
            bit 0 - ready status of drive 0
            bit 1 - ready status of drive 1
            bit 2 - state of channel's interrupt FF
            bit 3 - controller presence indicator
            bit 4 - DD controller presence indicator
            bit 5 - ready status of drive 2
            bit 6 - ready status of drive 3
            bit 7 - zero

        069H - Read - Read result type (bits 2-7 are zero)
            00 - I/O complete with error
            01 - Reserved
            10 - Result byte contains diskette ready status
            11 - Reserved
        069H - Write - IOPB address low byte.

        06AH - Write - IOPB address high byte and start operation.

        06BH - Read - Read result byte
            If result type is 00H
            0x01 - ID field miscompare
            0x02 - Data field CRC error
            0x04 - Seek error
            0x08 - Bad sector address error
            0x0A - ID field CRC error
            0x0B - Protocol violations
            0x0C - Bad track address error
            0x0E - No ID address mark or sector found
            0x0F - D=Bad data field address mark
            0x10 - Format error
            0x20 - write protect
            0x40 - write error
            0x80 - not ready

        06FH - Write - Reset diskette system.

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
        u6 - hdd number.

*/

#include "system_defs.h"                /* system header in system dir */

#if defined (SBC206_NUM) && (SBC206_NUM > 0)

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define HDD_NUM          2               //one fixed and one removable

//disk controller operations
#define DNOP            0x00            //HDC no operation
#define DSEEK           0x01            //HDC seek
#define DFMT            0x02            //HDC format
#define DHOME           0x03            //HDC home
#define DREAD           0x04            //HDC read
#define DVCRC           0x05            //HDC verify CRC
#define DWRITE          0x06            //HDC write

//status
#define RDY0            0x01            //HDD 0 ready
#define RDY1            0x02            //HDD 1 ready
#define HDCINT          0x04            //HDC interrupt flag
#define HDCPRE          0x08            //HDC board present

//result type
#define ROK            0x00             //HDC returned error
//#define RCHG             0x02            //HDC returned ok
#define RCHG             0x01            //HDC returned ok

// If result type is RERR then rbyte is
#define RB0DR           0x01            //deleted record
#define RB0CRC          0x02            //CRC error
#define RB0SEK          0x04            //seek error
#define RB0ADR          0x08            //address error
#define RB0OU           0x10            //data overrun/underrun
#define RB0WP           0x20            //write protect
#define RB0WE           0x40            //write error
#define RB0NR           0x80            //not ready

// If result type is ROK then rbyte is
#define RB1RD0          0x40            //drive 0 ready
#define RB1RD1          0x80            //drive 1 ready

//disk geometry values
#define MDSHD           3796992         //hard disk HD size
#define MAXSECHD        144             //hard disk last sector (2 heads/2 tracks)
#define MAXTRKHD        206             //hard disk last track

#define isbc206_NAME    "Intel iSBC 206 Hard Disk Controller Board"

/* external globals */

extern uint16    PCX;

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern uint8 unreg_dev(uint8);
extern uint8 get_mbyte(uint16 addr);
extern void put_mbyte(uint16 addr, uint8 val);

/* function prototypes */

t_stat isbc206_set_port(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc206_set_int(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc206_set_verb(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc206_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat isbc206_reset(DEVICE *dptr);
void isbc206_reset_dev(void);
t_stat isbc206_attach (UNIT *uptr, CONST char *cptr);
t_stat isbc206_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 isbc206r0(t_bool io, uint8 data, uint8 devnum);  /* isbc206 port 0 */
uint8 isbc206r1(t_bool io, uint8 data, uint8 devnum);  /* isbc206 port 1 */
uint8 isbc206r2(t_bool io, uint8 data, uint8 devnum);  /* isbc206 port 2 */
uint8 isbc206r3(t_bool io, uint8 data, uint8 devnum);  /* isbc206 port 3 */
uint8 isbc206r7(t_bool io, uint8 data, uint8 devnum);  /* isbc206 port 7 */
void isbc206_diskio(void);       //do actual disk i/o

/* globals */

int isbc206_onetime = 1;

static const char* isbc206_desc(DEVICE *dptr) {
    return isbc206_NAME;
}
typedef    struct    {                  //HDD definition
    int     t0;
    int     rdy;
    uint8   sec;
    uint8   cyl;
    }    HDDDEF;

typedef    struct    {                  //HDC definition
    uint8  baseport;                    //HDC base port
    uint8   intnum;                     //interrupt number
    uint8   verb;                       //verbose flag
    uint16  iopb;                       //HDC IOPB
    uint8   stat;                       //HDC status
    uint8   rdychg;                     //HDC ready change
    uint8   rtype;                      //HDC result type
    uint8   rbyte0;                     //HDC result byte for type 00
    uint8   rbyte1;                     //HDC result byte for type 10
    uint8   intff;                      //HDC interrupt FF
    HDDDEF  hd[HDD_NUM];                //indexed by the HDD number
    }    HDCDEF;

HDCDEF    hdc206;                       //indexed by the isbc-206 instance number


UNIT isbc206_unit[] = {                 // 2 HDDs
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF|UNIT_FIX, MDSHD) }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF|UNIT_FIX, MDSHD) }, 
    { NULL }
};

REG isbc206_reg[] = {
    { HRDATA (STAT0, hdc206.stat, 8) },      /* isbc206 0 status */
    { HRDATA (RTYP0, hdc206.rtype, 8) },     /* isbc206 0 result type */
    { HRDATA (RBYT0A, hdc206.rbyte0, 8) },   /* isbc206 0 result byte 0 */
    { HRDATA (RBYT0B, hdc206.rbyte1, 8) },   /* isbc206 0 result byte 1 */
    { HRDATA (INTFF0, hdc206.intff, 8) },    /* isbc206 0 interrupt f/f */
    { NULL }
};

MTAB isbc206_mod[] = {
    { UNIT_WPMODE, 0, "RW", "RW", &isbc206_set_mode },
    { UNIT_WPMODE, UNIT_WPMODE, "WP", "WP", &isbc206_set_mode },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "VERB", &isbc206_set_verb,
        NULL, NULL, "Sets the verbose mode for iSBC206"},
    { MTAB_XTD | MTAB_VDV, 0, NULL, "PORT", &isbc206_set_port,
        NULL, NULL, "Sets the base port for iSBC206"},
    { MTAB_XTD | MTAB_VDV, 0, NULL, "INT", &isbc206_set_int,
        NULL, NULL, "Sets the interrupt number for iSBC206"},
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, &isbc206_show_param, NULL, 
        "show configured parametes for iSBC206" },
    { 0 }
};

DEBTAB isbc206_debug[] = {
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

DEVICE isbc206_dev = {
    "SBC206",           //name
    isbc206_unit,       //units
    isbc206_reg,        //registers
    isbc206_mod,        //modifiers
    HDD_NUM,             //numunits 
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    isbc206_reset,      //reset
    NULL,               //boot
    &isbc206_attach,    //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl 
    isbc206_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* isbc206 set mode = Write protect */

t_stat isbc206_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    if (uptr->flags & UNIT_ATT)
        return sim_messagef (SCPE_ALATT, "%s is already attached to %s\n", sim_uname(uptr), uptr->filename);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
        if (hdc206.verb)
            sim_printf("    sbc206: WP\n");
    } else {                            /* read write */
        uptr->flags &= ~val;
        if (hdc206.verb)
            sim_printf("    sbc206: RW\n");
    }
    return SCPE_OK;
}

// set base address parameter

t_stat isbc206_set_port(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result;
    
    if (uptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%02x", &size);
    hdc206.baseport = size;
    if (hdc206.verb)
        sim_printf("SBC206: Base port=%04X\n", hdc206.baseport);
    return SCPE_OK;
}

// set interrupt parameter

t_stat isbc206_set_int(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result;
    
    if (uptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%02x", &size);
    hdc206.intnum = size;
    if (hdc206.verb)
        sim_printf("SBC206: Interrupt number=%04X\n", hdc206.intnum);
    return SCPE_OK;
}

t_stat isbc206_set_verb(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    if (cptr == NULL)
        return SCPE_ARG;
    if (strncasecmp(cptr, "OFF", 4) == 0) {
        hdc206.verb = 0;
        return SCPE_OK;
    }
    if (strncasecmp(cptr, "ON", 3) == 0) {
        hdc206.verb = 1;
        sim_printf("   SBC206: hdc206.verb=%d\n", hdc206.verb);
        return SCPE_OK;
    }
    return SCPE_ARG;
}

// show configuration parameters

t_stat isbc206_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "%s Base port at %04X  Interrupt # is %i  %s", 
        ((isbc206_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        hdc206.baseport, hdc206.intnum,
        hdc206.verb ? "Verbose" : "Quiet"
        );
    return SCPE_OK;
}

/* Hardware reset routine */

t_stat isbc206_reset(DEVICE *dptr)
{
    int i;
    UNIT *uptr;
    
    if (dptr == NULL)
        return SCPE_ARG;
    if (isbc206_onetime) {
        hdc206.baseport = SBC206_BASE;  //set default base
        hdc206.intnum = SBC206_INT;     //set default interrupt
        hdc206.verb = 0;                //set verb = 0
        isbc206_onetime = 0;
        // one-time initialization for all FDDs for this FDC instance
        for (i = 0; i < HDD_NUM; i++) { 
            uptr = isbc206_dev.units + i;
            uptr->u6 = i;               //fdd unit number
        }
    }
    if ((dptr->flags & DEV_DIS) == 0) { // enabled
        reg_dev(isbc206r0, hdc206.baseport, 0);         //read status
        reg_dev(isbc206r1, hdc206.baseport + 1, 0);     //read rslt type/write IOPB addr-l
        reg_dev(isbc206r2, hdc206.baseport + 2, 0);     //write IOPB addr-h and start 
        reg_dev(isbc206r3, hdc206.baseport + 3, 0);     //read rstl byte 
        reg_dev(isbc206r7, hdc206.baseport + 7, 0);     //write reset fdc201
        isbc206_reset_dev(); //software reset
        if (hdc206.verb)
            sim_printf("    sbc206: Enabled base port at 0%02XH  Interrupt #=%02X  %s\n",
            hdc206.baseport, hdc206.intnum, hdc206.verb ? "Verbose" : "Quiet" );
    } else {
        unreg_dev(hdc206.baseport);         //read status
        unreg_dev(hdc206.baseport + 1);     //read rslt type/write IOPB addr-l
        unreg_dev(hdc206.baseport + 2);     //write IOPB addr-h and start 
        unreg_dev(hdc206.baseport + 3);     //read rstl byte 
        unreg_dev(hdc206.baseport + 7);     //write reset fdc201
        if (hdc206.verb)
            sim_printf("    sbc206: Disabled\n");
    }
    return SCPE_OK;
}

/* Software reset routine */

void isbc206_reset_dev(void)
{
    int32 i;
    UNIT *uptr;

    hdc206.stat = 0;            //clear status
    for (i = 0; i < HDD_NUM; i++) {      /* handle all units */
        uptr = isbc206_dev.units + i;
        hdc206.stat |= (HDCPRE + 0x80);  //set the HDC status
        hdc206.rtype = ROK;
        hdc206.rbyte0 = 0;              //set no error
        if (uptr->flags & UNIT_ATT) { /* if attached */
            switch(i){
                case 0:
                    hdc206.stat |= RDY0; //set HDD 0 ready
                    hdc206.rbyte1 |= RB1RD0;
                    break;
                case 1:
                    hdc206.stat |= RDY1; //set HDD 1 ready
                    hdc206.rbyte1 |= RB1RD1;
                    break;
            }
            hdc206.rdychg = 0;
        }
    }
}

/* isbc206 attach - attach an .IMG file to a HDD */

t_stat isbc206_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    uint8 hddnum;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   isbc206_attach: Attach error %d\n", r);
        return r;
    }
    hddnum = uptr->u6;
    switch(hddnum){
        case 0:
            hdc206.stat |= RDY0; //set HDD 0 ready
            hdc206.rbyte1 |= RB1RD0;
            break;
        case 1:
            hdc206.stat |= RDY1; //set HDD 1 ready
            hdc206.rbyte1 |= RB1RD1;
            break;
    }
    hdc206.rtype = ROK;
    hdc206.rbyte0 = 0;              //set no error
    return SCPE_OK;
}

/* iSBC206 control port functions */

uint8 isbc206r0(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read status*/
        return hdc206.stat;
    }
    return 0;
}

uint8 isbc206r1(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        hdc206.intff = 0;           //clear interrupt FF
        hdc206.stat &= ~(HDCINT + 0x80);
        hdc206.rtype = ROK;
        return hdc206.rtype;
    } else {                        /* write data port */
        hdc206.iopb = data;
    }
    return 0;
}

uint8 isbc206r2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        hdc206.iopb |= (data << 8);
        isbc206_diskio();
        if (hdc206.intff)
            hdc206.stat |= HDCINT;
    }
    return 0;
}

uint8 isbc206r3(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        if (hdc206.rtype == ROK) {
            return hdc206.rbyte0;
        } else {
            if (hdc206.rdychg) {
                return hdc206.rbyte1;
            } else {
                return hdc206.rbyte0;
            }
        }
    } else {                        /* write data port */
        ; //stop diskette operation
    }
    return 0;
}


uint8 isbc206r7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                  /* read data port */
        ;
    } else {                        /* write data port */
        isbc206_reset_dev();
    }
    return 0;
}

// perform the actual disk I/O operation

void isbc206_diskio(void)
{
    uint8 cw, di, nr, ta, sa, data, nrptr;
    uint16 ba;
    uint32 dskoff;
    uint8 hddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    uint8 *fbuf;

    //parse the IOPB 
    cw = get_mbyte(hdc206.iopb);
    di = get_mbyte(hdc206.iopb + 1);
    nr = get_mbyte(hdc206.iopb + 2);
    ta = get_mbyte(hdc206.iopb + 3);
    sa = get_mbyte(hdc206.iopb + 4);
    ba = get_mbyte(hdc206.iopb + 5);
    hddnum = (di & 0x30) >> 4;
    uptr = isbc206_dev.units + hddnum;
    fbuf = (uint8 *) (isbc206_dev.units + hddnum)->filebuf;
    //check for not ready
    switch(hddnum) {
        case 0:
            if ((hdc206.stat & RDY0) == 0) {
                hdc206.rtype = ROK;
                hdc206.rbyte0 = RB0NR;
                hdc206.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC206: HDD %d - Ready error", hddnum);
                return;
            }
            break;
        case 1:
            if ((hdc206.stat & RDY1) == 0) {
                hdc206.rtype = ROK;
                hdc206.rbyte0 = RB0NR;
                hdc206.intff = 1;  //set interrupt FF
                sim_printf("\n   SBC206: HDD %d - Ready error", hddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        ((di & 0x07) != DHOME) && (
        (sa > MAXSECHD) ||
        ((sa + nr) > (MAXSECHD + 1)) ||
        (sa == 0) ||
        (ta > MAXTRKHD)
        )) {
        hdc206.rtype = ROK;
        hdc206.rbyte0 = RB0ADR;
        hdc206.intff = 1;           //set interrupt FF
        sim_printf("\n   SBC206: FDD %d - Address error sa=%02X nr=%02X ta=%02X PCX=%04X",
            hddnum, sa, nr, ta, PCX);
        return;
    }
    switch (di & 0x07) {
        case DNOP:
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        case DSEEK:
            hdc206.hd[hddnum].sec = sa;
            hdc206.hd[hddnum].cyl = ta;
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        case DHOME:
            hdc206.hd[hddnum].sec = sa;
            hdc206.hd[hddnum].cyl = 0;
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        case DVCRC:
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        case DFMT:
           //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                hdc206.rtype = ROK;
                hdc206.rbyte0 = RB0WP;
                hdc206.intff = 1;       //set interrupt FF
                sim_printf("\n   SBC206: HDD %d - Write protect error DFMT", hddnum);
                return;
            }
            fmtb = get_mbyte(ba); //get the format byte
            //calculate offset into disk image
            dskoff = ((ta * MAXSECHD) + (sa - 1)) * 128;
            for(i=0; i<=((uint32)(MAXSECHD) * 128); i++) {
                *(fbuf + (dskoff + i)) = fmtb;
            }
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;              //set no error
            hdc206.intff = 1;      //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECHD) + (sa - 1)) * 128;
                //copy sector from image to RAM
                for (i=0; i<128; i++) { 
                    data = *(fbuf + (dskoff + i));
                    put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                hdc206.rtype = ROK;
                hdc206.rbyte0 = RB0WP;
                hdc206.intff = 1;       //set interrupt FF
                sim_printf("\n   SBC206: HDD %d - Write protect error DWRITE", hddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * MAXSECHD) + (sa - 1)) * 128;
                //copy sector from image to RAM
                for (i=0; i<128; i++) { 
                    data = get_mbyte(ba + i);
                    *(fbuf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            hdc206.rtype = ROK;
            hdc206.rbyte0 = 0;          //set no error
            hdc206.intff = 1;           //set interrupt FF
            break;
        default:
            sim_printf("\n   SBC206: HDD %d - isbc206_diskio bad di=%02X", hddnum, di & 0x07);
            break;
    }
}

#endif /* SBC206_NUM > 0 */

/* end of isbc206.c */
