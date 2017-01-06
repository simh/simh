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

        078H - Read - Subsystem status
            bit 0 - ready status of drive 0
            bit 1 - ready status of drive 1
            bit 2 - state of channel's interrupt FF
            bit 3 - controller presence indicator
            bit 4 - zero
            bit 5 - zero
            bit 6 - zero
            bit 7 - zero

        079H - Read - Read result type (bits 2-7 are zero)
            00 - I/O complete with error(unlinked)
            01 - I/O complete with error(linked)(hi 6-bits are block num)
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
            If result type is 10H
            bit 0 - zero
            bit 1 - zero
            bit 2 - zero
            bit 3 - zero
            bit 4 - zero
            bit 5 - zero
            bit 6 - drive 0 ready
            bit 7 - drive 1 ready

        07FH - Write - Reset diskette system.

    Operations:
        Recalibrate -
        Seek -
        Format Track -
        Write Data -
        Write Deleted Data -
        Read Data -
        Verify CRC -

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
        u5 - fdc number
        u6 - fdd number.

*/

#include "system_defs.h"                /* system header in system dir */

#define  DEBUG   0

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         2

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
#define RERR            0x00            //FDC returned error
#define ROK             0x02            //FDC returned ok

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

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);
extern uint8 multibus_get_mbyte(uint16 addr);
extern uint16 multibus_get_mword(uint16 addr);
extern void multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 multibus_put_mword(uint16 addr, uint16 val);

/* function prototypes */

t_stat isbc201_reset(DEVICE *dptr, uint16 base);
void isbc201_reset1(uint8 fdcnum);
t_stat isbc201_attach (UNIT *uptr, CONST char *cptr);
t_stat isbc201_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 isbc201_get_dn(void);
uint8 isbc2010(t_bool io, uint8 data);  /* isbc201 0 */
uint8 isbc2011(t_bool io, uint8 data);  /* isbc201 1 */
uint8 isbc2012(t_bool io, uint8 data);  /* isbc201 2 */
uint8 isbc2013(t_bool io, uint8 data);  /* isbc201 3 */
uint8 isbc2017(t_bool io, uint8 data);  /* isbc201 7 */
void isbc201_diskio(uint8 fdcnum);      //do actual disk i/o

/* globals */

int32 isbc201_fdcnum = 0;               //actual number of SBC-201 instances + 1

typedef    struct    {                  //FDD definition
    uint8   *buf;
    int     t0;
    int     rdy;
    uint8   maxsec;
    uint8   maxcyl;
    }    FDDDEF;

typedef    struct    {                  //FDC definition
    uint16  baseport;                   //FDC base port
    uint16  iopb;                       //FDC IOPB
    uint8   stat;                       //FDC status
    uint8   rtype;                      //FDC result type
    uint8   rbyte0;                     //FDC result byte for type 00
    uint8   rbyte1;                     //FDC result byte for type 10
    uint8   intff;                      //fdc interrupt FF
    FDDDEF  fdd[FDD_NUM];               //indexed by the FDD number
    }    FDCDEF;

FDCDEF    fdc201[4];                    //indexed by the isbc-201 instance number

UNIT isbc201_unit[] = {
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 } 
};

REG isbc201_reg[] = {
    { HRDATA (STATUS0, isbc201_unit[0].u3, 8) }, /* isbc201 0 status */
    { HRDATA (RTYP0, isbc201_unit[0].u4, 8) }, /* isbc201 0 result type */
    { HRDATA (RBYT0, isbc201_unit[0].u5, 8) }, /* isbc201 0 result byte */
    { HRDATA (STATUS1, isbc201_unit[1].u3, 8) }, /* isbc201 1 status */
    { HRDATA (RTYP1, isbc201_unit[1].u4, 8) }, /* isbc201 0 result type */
    { HRDATA (RBYT1, isbc201_unit[1].u5, 8) }, /* isbc201 0 result byte */
    { HRDATA (STATUS2, isbc201_unit[2].u3, 8) }, /* isbc201 2 status */
    { HRDATA (RTYP2, isbc201_unit[0].u4, 8) }, /* isbc201 0 result type */
    { HRDATA (R$BYT2, isbc201_unit[2].u5, 8) }, /* isbc201 0 result byte */
    { HRDATA (STATUS3, isbc201_unit[3].u3, 8) }, /* isbc201 3 status */
    { HRDATA (RTYP3, isbc201_unit[3].u4, 8) }, /* isbc201 0 result type */
    { HRDATA (RBYT3, isbc201_unit[3].u5, 8) }, /* isbc201 0 result byte */
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
    NULL,               //reset
    NULL,               //boot
    &isbc201_attach,    //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    DEBUG_flow + DEBUG_read + DEBUG_write, //dctrl 
    isbc201_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* Hardware reset routine */

t_stat isbc201_reset(DEVICE *dptr, uint16 base)
{
    sim_printf("Initializing iSBC-201 FDC Board\n");
    if (SBC201_NUM) {
        sim_printf("   isbc201-%d: Hardware Reset\n", isbc201_fdcnum);
        sim_printf("   isbc201-%d: Registered at %04X\n", isbc201_fdcnum, base);
        fdc201[isbc201_fdcnum].baseport = base;
        reg_dev(isbc2010, base, isbc201_fdcnum);         //read status
        reg_dev(isbc2011, base + 1, isbc201_fdcnum);     //read rslt type/write IOPB addr-l
        reg_dev(isbc2012, base + 2, isbc201_fdcnum);     //write IOPB addr-h and start 
        reg_dev(isbc2013, base + 3, isbc201_fdcnum);     //read rstl byte 
        reg_dev(isbc2017 , base + 7, isbc201_fdcnum);     //write reset isbc202
        isbc201_reset1(isbc201_fdcnum);
        isbc201_fdcnum++;
    } else {
        sim_printf("   No isbc201 installed\n");
    }
    return SCPE_OK;
}

/* Software reset routine */

void isbc201_reset1(uint8 fdcnum)
{
    int32 i;
    UNIT *uptr;

    sim_printf("   isbc201-%d: Software Reset\n", fdcnum);
    fdc201[fdcnum].stat = 0;            //clear status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = isbc201_dev.units + i;
        fdc201[fdcnum].stat |= FDCPRE;  //set the FDC status
        fdc201[fdcnum].rtype = ROK;
        if (uptr->capac == 0) {         /* if not configured */
            uptr->capac = 0;            /* initialize unit */
            uptr->u5 = fdcnum;          //fdc device number
            uptr->u6 = i;               //fdd unit number
            uptr->flags |= UNIT_WPMODE; /* set WP in unit flags */
            sim_printf("   isbc201-%d: Configured, Status=%02X Not attached\n", i, fdc201[fdcnum].stat);
        } else {
            switch(i){
                case 0:
                    fdc201[fdcnum].stat |= RDY0; //set FDD 0 ready
                    fdc201[fdcnum].rbyte1 |= RB1RD0;
                    break;
                case 1:
                    fdc201[fdcnum].stat |= RDY1; //set FDD 1 ready
                    fdc201[fdcnum].rbyte1 |= RB1RD1;
                    break;
            }
            sim_printf("   isbc201-%d: Configured, Status=%02X Attached to %s\n",
                i, fdc201[fdcnum].stat, uptr->filename);
        }
    }
}

/* isbc202 attach - attach an .IMG file to a FDD */

t_stat isbc201_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    FILE *fp;
    int32 i, c = 0;
    long flen;
    uint8 fdcnum, fddnum;

    sim_debug (DEBUG_flow, &isbc201_dev, "   isbc201_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   isbc201_attach: Attach error\n");
        return r;
    }
    fdcnum = uptr->u5;
    fddnum = uptr->u6;
    fp = fopen(uptr->filename, "rb");
    if (fp == NULL) {
        sim_printf("   Unable to open disk image file %s\n", uptr->filename);
        sim_printf("   No disk image loaded!!!\n");
    } else {
        sim_printf("isbc202: Attach\n");
        fseek(fp, 0, SEEK_END);         /* size disk image */
        flen = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fdc201[fdcnum].fdd[fddnum].buf == NULL) { /* no buffer allocated */
            fdc201[fdcnum].fdd[fddnum].buf = (uint8 *)malloc(flen);
            if (fdc201[fdcnum].fdd[fddnum].buf == NULL) {
                sim_printf("   isbc201_attach: Malloc error\n");
                return SCPE_MEM;
            }
        }
        uptr->capac = flen;
        i = 0;
        c = fgetc(fp);                  // copy disk image into buffer
        while (c != EOF) {
            *(fdc201[fdcnum].fdd[fddnum].buf + i++) = c & 0xFF;
            c = fgetc(fp);
        }
        fclose(fp);
        switch(fddnum){
            case 0:
                fdc201[fdcnum].stat |= RDY0; //set FDD 0 ready
                fdc201[fdcnum].rtype = ROK;
                fdc201[fdcnum].rbyte1 |= RB1RD0;
                break;
            case 1:
                fdc201[fdcnum].stat |= RDY1; //set FDD 1 ready
                fdc201[fdcnum].rtype = ROK;
                fdc201[fdcnum].rbyte1 |= RB1RD1;
                break;
        }
        if (flen == 256256) {           /* 8" 256K SSSD */
            fdc201[fdcnum].fdd[fddnum].maxcyl = 77;
            fdc201[fdcnum].fdd[fddnum].maxsec = 26;
        }
        sim_printf("   iSBC-201%d: Configured %d bytes, Attached to %s\n",
            fdcnum, uptr->capac, uptr->filename);
    }
    sim_debug (DEBUG_flow, &isbc201_dev, "   isbc201_attach: Done\n");
    return SCPE_OK;
}

/* isbc202 set mode = Write protect */

t_stat isbc201_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
//    sim_debug (DEBUG_flow, &isbc201_dev, "   isbc201_set_mode: Entered with val=%08XH uptr->flags=%08X\n", 
//        val, uptr->flags);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
    } else {                            /* read write */
        uptr->flags &= ~val;
    }
//    sim_debug (DEBUG_flow, &isbc201_dev, "   isbc201_set_mode: Done\n");
    return SCPE_OK;
}

uint8 isbc201_get_dn(void)
{
    int i;

    for (i=0; i<SBC201_NUM; i++)
        if (port >= fdc201[i].baseport && port <= fdc201[i].baseport + 7)
            return i;
    sim_printf("isbc201_get_dn: port %04X not in isbc202 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* ISBC201 control port functions */

uint8 isbc2010(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc201_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read ststus*/
            if (DEBUG)
                sim_printf("\n   isbc201-%d: returned status=%02X", 
                    fdcnum, fdc201[fdcnum].stat);
            return fdc201[fdcnum].stat;
        }
    }
    return 0;
}

uint8 isbc2011(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc201_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            fdc201[fdcnum].intff = 0;      //clear interrupt FF
            fdc201[fdcnum].stat &= ~FDCINT;
            if (DEBUG)
                sim_printf("\n   isbc201-%d: returned rtype=%02X intff=%02X status=%02X", 
                    fdcnum, fdc201[fdcnum].rtype, fdc201[fdcnum].intff, fdc201[fdcnum].stat);
            return fdc201[fdcnum].rtype;
        } else {                        /* write data port */
            fdc201[fdcnum].iopb = data;
            if (DEBUG)
                sim_printf("\n   isbc201-%d: IOPB low=%02X", fdcnum, data);
        }
    }
    return 0;
}

uint8 isbc2012(t_bool io, uint8 data)
{
    uint8 fdcnum;
    
    if ((fdcnum = isbc201_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            fdc201[fdcnum].iopb |= (data << 8);
            if (DEBUG)
                sim_printf("\n   isbc201-%d: data=%02X IOPB=%04X", fdcnum, data, fdc201[fdcnum].iopb);
            isbc201_diskio(fdcnum);
            if (fdc201[fdcnum].intff)
                fdc201[fdcnum].stat |= FDCINT;
        }
    }
    return 0;
}

uint8 isbc2013(t_bool io, uint8 data)
{
    uint8 fdcnum, rslt;

    if ((fdcnum = isbc201_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            switch(fdc201[fdcnum].rtype) {
                case 0x00:
                    rslt = fdc201[fdcnum].rbyte0;
                    break;
                case 0x02:
                    rslt = fdc201[fdcnum].rbyte1;
                    break;
            }
            if (DEBUG)
                sim_printf("\n   isbc201-%d: returned result byte=%02X", fdcnum, rslt);
            return rslt;
        } else {                        /* write data port */
            ; //stop diskette operation
        }
    }
    return 0;
}

uint8 isbc2017(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc201_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            isbc201_reset1(fdcnum);
        }
    }
    return 0;
}

// perform the actual disk I/O operation

void isbc201_diskio(uint8 fdcnum)
{
    uint8 cw, di, nr, ta, sa, bn, data, nrptr, c;
    uint16 ba, ni;
    uint32 dskoff;
    uint8 fddnum;
    uint32 i;
    UNIT *uptr;
    FILE *fp;

    //parse the IOPB
    cw = multibus_get_mbyte(fdc201[fdcnum].iopb);
    di = multibus_get_mbyte(fdc201[fdcnum].iopb + 1);
    nr = multibus_get_mbyte(fdc201[fdcnum].iopb + 2);
    ta = multibus_get_mbyte(fdc201[fdcnum].iopb + 3);
    sa = multibus_get_mbyte(fdc201[fdcnum].iopb + 4);
    ba = multibus_get_mword(fdc201[fdcnum].iopb + 5);
    bn = multibus_get_mbyte(fdc201[fdcnum].iopb + 7);
    ni = multibus_get_mword(fdc201[fdcnum].iopb + 8);
    fddnum = (di & 0x30) >> 4;
    uptr = isbc201_dev.units + fddnum;
    if (DEBUG) {
        sim_printf("\n   isbc201-%d: isbc201_diskio IOPB=%04X FDD=%02X STAT=%02X",
            fdcnum, fdc201[fdcnum].iopb, fddnum, fdc201[fdcnum].stat);
        sim_printf("\n   isbc201-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X bn=%02X ni=%04X",
            fdcnum, cw, di, nr, ta, sa, ba, bn, ni);
    }
    //check for not ready
    switch(fddnum) {
        case 0:
            if ((fdc201[fdcnum].stat & RDY0) == 0) {
                fdc201[fdcnum].rtype = RERR;
                fdc201[fdcnum].rbyte0 = RB0NR;
                fdc201[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc201-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 1:
            if ((fdc201[fdcnum].stat & RDY1) == 0) {
                fdc201[fdcnum].rtype = RERR;
                fdc201[fdcnum].rbyte0 = RB0NR;
                fdc201[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc201-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        (sa > fdc201[fdcnum].fdd[fddnum].maxsec) ||
        ((sa + nr) > (fdc201[fdcnum].fdd[fddnum].maxsec + 1)) ||
        (sa == 0) ||
        (ta > fdc201[fdcnum].fdd[fddnum].maxcyl)
        ) {
//            sim_printf("\n   isbc201-%d: maxsec=%02X maxcyl=%02X",
//                fdcnum, fdc201[fdcnum].fdd[fddnum].maxsec, fdc201[fdcnum].fdd[fddnum].maxcyl);
            fdc201[fdcnum].rtype = RERR;
            fdc201[fdcnum].rbyte0 = RB0ADR;
            fdc201[fdcnum].intff = 1;      //set interrupt FF
            sim_printf("\n   isbc201-%d: Address error on drive %d", fdcnum, fddnum);
            return;
    }
    switch (di & 0x07) {
        case DNOP:
        case DSEEK:
        case DHOME:
        case DVCRC:
            fdc201[fdcnum].rtype = ROK;
            fdc201[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * fdc201[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
                if (DEBUG)
                    sim_printf("\n   isbc201-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X bn=%02X ni=%04X dskoff=%06X",
                        fdcnum, cw, di, nr, ta, sa, ba, bn, ni, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = *(fdc201[fdcnum].fdd[fddnum].buf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc201[fdcnum].rtype = ROK;
            fdc201[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc201[fdcnum].rtype = RERR;
                fdc201[fdcnum].rbyte0 = RB0WP;
                fdc201[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc201-%d: Write protect error on drive %d", fdcnum, fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * fdc201[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
                if (DEBUG)
                    sim_printf("\n   isbc201-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X bn=%02X ni=%04X dskoff=%06X",
                        fdcnum, cw, di, nr, ta, sa, ba, bn, ni, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = multibus_get_mbyte(ba + i);
                    *(fdc201[fdcnum].fdd[fddnum].buf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            //*** quick fix. Needs more thought!
            fp = fopen(uptr->filename, "wb"); // write out modified image
            for (i=0; i<uptr->capac; i++) {
                c = *(fdc201[fdcnum].fdd[fddnum].buf + i);
                fputc(c, fp);
            }
            fclose(fp);
            fdc201[fdcnum].rtype = ROK;
            fdc201[fdcnum].intff = 1;      //set interrupt FF
            break;
        default:
            sim_printf("\n   isbc201-%d: isbc201_diskio bad di=%02X", fdcnum, di & 0x07);
            break;
    }
}

/* end of isbc201.c */
