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
        
    NOTES:

        This iSBC 202 device simulator (DEVICES) supports 4 floppy disk drives 
        (UNITS).  It uses the SBC202_BASE and SBC202_INT from system_defs.h to 
        set the default base port and interrupt.
        
        The default base port can be changed by "sim> set sbc202 port=88".  The
        default interrupt can be changed by "sim> set sbc202 int=5".  Current 
        settings can be shown by "sim> show sbc202 param".
        
        This device simulator can be enabled or disabled if SBC202_NUM in
        system_defs.h is set to 1.  Only one board can be simulated.  It is 
        enabled by "sim> Sset sbc202 ena" and disabled by "sim> set sbc202 dis".
        
        The disk images in each FDD can be set to RW or WP.  They default to WP
        
        
*/

#include "system_defs.h"                /* system header in system dir */

#if defined (SBC202_NUM) && (SBC202_NUM > 0)

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

#define isbc202_NAME    "Intel iSBC 202 Floppy Disk Controller Board"

/* external globals */

extern uint16    PCX;

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern uint8 unreg_dev(uint8);
extern uint8 get_mbyte(uint16 addr);
extern void put_mbyte(uint16 addr, uint8 val);

/* function prototypes */

t_stat isbc202_set_port(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc202_set_int(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc202_set_verb(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc202_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
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

int isbc202_onetime = 1;

static const char* isbc202_desc(DEVICE *dptr) {
    return isbc202_NAME;
}

typedef    struct    {                  //FDD definition
    uint8   sec;
    uint8   cyl;
    }    FDDDEF;

typedef    struct    {                  //FDC definition
    uint8   baseport;                   //FDC base port
    uint8   intnum;                     //interrupt number
    uint8   verb;                       //verbose flag
    uint16  iopb;                       //FDC IOPB
    uint8   stat;                       //FDC status
    uint8   rdychg;                     //FDC ready change
    uint8   rtype;                      //FDC result type
    uint8   rbyte0;                     //FDC result byte for type 00
    uint8   rbyte1;                     //FDC result byte for type 10
    uint8   intff;                      //fdc interrupt FF
    FDDDEF  fdd[FDD_NUM];               //indexed by the FDD number
    }    FDCDEF;

FDCDEF    fdc202;                       //indexed by the isbc-202 instance number

/* isbc202 Standard I/O Data Structures */

UNIT isbc202_unit[] = { // 4 FDDs
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD) }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD) }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD) }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE+UNIT_BUFABLE+UNIT_MUSTBUF+UNIT_FIX, MDSDD) }, 
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
    { MTAB_XTD | MTAB_VDV, 0, NULL, "VERB", &isbc202_set_verb,
        NULL, NULL, "Sets the verbose mode for iSBC202"},
    { MTAB_XTD | MTAB_VDV, 0, NULL, "PORT", &isbc202_set_port,
        NULL, NULL, "Sets the base port for iSBC202"},
    { MTAB_XTD | MTAB_VDV, 0, NULL, "INT", &isbc202_set_int,
        NULL, NULL, "Sets the interrupt number for iSBC202"},
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, &isbc202_show_param, NULL, 
        "show configured parametes for iSBC202" },
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
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &isbc202_desc       //device description
};

/* isbc202 set mode = Write protect */

t_stat isbc202_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    if (uptr->flags & UNIT_ATT)
        return sim_messagef (SCPE_ALATT, "%s is already attached to %s\n", sim_uname(uptr), 
            uptr->filename);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
        if (fdc202.verb)
            sim_printf("    sbc202: WP\n");
    } else {                            /* read write */
        uptr->flags &= ~val;
        if (fdc202.verb)
            sim_printf("    sbc202: RW\n");
    }
    return SCPE_OK;
}

// set base address parameter

t_stat isbc202_set_port(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result;
    
    if (uptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%02x", &size);
    fdc202.baseport = size;
    if (fdc202.verb)
        sim_printf("SBC202: Base port=%04X\n", fdc202.baseport);
    return SCPE_OK;
}

// set interrupt parameter

t_stat isbc202_set_int(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result;
    
    if (uptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%02x", &size);
    fdc202.intnum = size;
    if (fdc202.verb)
        sim_printf("SBC202: Interrupt number=%04X\n", fdc202.intnum);
    return SCPE_OK;
}

t_stat isbc202_set_verb(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    if (cptr == NULL)
        return SCPE_ARG;
    if (strncasecmp(cptr, "OFF", 4) == 0) {
        fdc202.verb = 0;
        return SCPE_OK;
    }
    if (strncasecmp(cptr, "ON", 3) == 0) {
        fdc202.verb = 1;
        sim_printf("   SBC202: fdc202.verb=%d\n", fdc202.verb);
        return SCPE_OK;
    }
    return SCPE_ARG;
}

// show configuration parameters

t_stat isbc202_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "%s Base port at %04X  Interrupt # is %i  %s", 
        ((isbc202_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        fdc202.baseport, fdc202.intnum,
        fdc202.verb ? "Verbose" : "Quiet"
        );
    return SCPE_OK;
}

/* Hardware reset routine */

t_stat isbc202_reset(DEVICE *dptr)
{
    int i;
    UNIT *uptr;
    
    if (dptr == NULL)
        return SCPE_ARG;
    if (isbc202_onetime) {
        fdc202.baseport = SBC202_BASE;  //set default base
        fdc202.intnum = SBC202_INT;     //set default interrupt
        fdc202.verb = 0;                //set verb = off
        isbc202_onetime = 0;
        // one-time initialization for all FDDs for this FDC instance
        for (i = 0; i < FDD_NUM; i++) { 
            uptr = isbc202_dev.units + i;
            uptr->u6 = i;               //fdd unit number
        }
    }
    if ((dptr->flags & DEV_DIS) == 0) { // enabled
        reg_dev(isbc202r0, fdc202.baseport, 0);         //read status
        reg_dev(isbc202r1, fdc202.baseport + 1, 0);     //read rslt type/write IOPB addr-l
        reg_dev(isbc202r2, fdc202.baseport + 2, 0);     //write IOPB addr-h and start 
        reg_dev(isbc202r3, fdc202.baseport + 3, 0);     //read rstl byte 
        reg_dev(isbc202r7, fdc202.baseport + 7, 0);     //write reset fdc201
        isbc202_reset_dev(); //software reset
//        if (fdc202.verb)
            sim_printf("    sbc202: Enabled base port at 0%02XH  Interrupt #=%02X  %s\n",
                fdc202.baseport, fdc202.intnum, fdc202.verb ? "Verbose" : "Quiet" );
    } else {
        unreg_dev(fdc202.baseport);         //read status
        unreg_dev(fdc202.baseport + 1);     //read rslt type/write IOPB addr-l
        unreg_dev(fdc202.baseport + 2);     //write IOPB addr-h and start 
        unreg_dev(fdc202.baseport + 3);     //read rstl byte 
        unreg_dev(fdc202.baseport + 7);     //write reset fdc201
//        if (fdc202.verb)
            sim_printf("    sbc202: Disabled\n");
    }
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
    cw = get_mbyte(fdc202.iopb);
    di = get_mbyte(fdc202.iopb + 1);
    nr = get_mbyte(fdc202.iopb + 2);
    ta = get_mbyte(fdc202.iopb + 3);
    sa = get_mbyte(fdc202.iopb + 4);
    ba = get_mbyte(fdc202.iopb + 5);
    ba |= (get_mbyte(fdc202.iopb + 6) << 8);
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
            fmtb = get_mbyte(ba); //get the format byte
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
                    put_mbyte(ba + i, data);
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
                    data = get_mbyte(ba + i);
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
            sim_printf("\n   SBC202: FDD %d - isbc202_diskio bad command di=%02X",
                fddnum, di & 0x07);
            break;
    }
}

#endif /* SBC202_NUM > 0 */

/* end of isbc202.c */
