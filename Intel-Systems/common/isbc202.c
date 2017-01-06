/*  isbc202.c: Intel double density disk adapter adapter

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
            If result type is 10H
            bit 0 - zero
            bit 1 - zero
            bit 2 - zero
            bit 3 - zero
            bit 4 - drive 2 ready
            bit 5 - drive 3 ready
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

#define  DEBUG   0

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         4

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
#define RB1RD2          0x10            //drive 2 ready
#define RB1RD3          0x20            //drive 3 ready
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

t_stat isbc202_reset(DEVICE *dptr, uint16 base);
void isbc202_reset1(uint8 fdcnum);
t_stat isbc202_attach (UNIT *uptr, CONST char *cptr);
t_stat isbc202_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 isbc202_get_dn(void);
uint8 isbc2020(t_bool io, uint8 data);    /* isbc202 0 */
uint8 isbc2021(t_bool io, uint8 data);    /* isbc202 1 */
uint8 isbc2022(t_bool io, uint8 data);    /* isbc202 2 */
uint8 isbc2023(t_bool io, uint8 data);    /* isbc202 3 */
uint8 isbc2027(t_bool io, uint8 data);    /* isbc202 7 */
void isbc202_diskio(uint8 fdcnum);         //do actual disk i/o

/* globals */

int32 isbc202_fdcnum = 0;               //actual number of SBC-202 instances + 1

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

FDCDEF    fdc202[4];                    //indexed by the isbc-202 instance number

UNIT isbc202_unit[] = {
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 } 
};

REG isbc202_reg[] = {
    { HRDATA (STAT0, fdc202[0].stat, 8) },      /* isbc202 0 status */
    { HRDATA (RTYP0, fdc202[0].rtype, 8) },     /* isbc202 0 result type */
    { HRDATA (RBYT0A, fdc202[0].rbyte0, 8) },   /* isbc202 0 result byte 0 */
    { HRDATA (RBYT0B, fdc202[0].rbyte1, 8) },   /* isbc202 0 result byte 1 */
    { HRDATA (INTFF0, fdc202[0].intff, 8) },    /* isbc202 0 interrupt f/f */
    { HRDATA (STAT1, fdc202[1].stat, 8) },      /* isbc202 1 status */
    { HRDATA (RTYP1, fdc202[1].rtype, 8) },     /* isbc202 1 result type */
    { HRDATA (RBYT1A, fdc202[1].rbyte0, 8) },   /* isbc202 1 result byte 0 */
    { HRDATA (RBYT1B, fdc202[1].rbyte1, 8) },   /* isbc202 1 result byte 1 */
    { HRDATA (INTFF1, fdc202[1].intff, 8) },    /* isbc202 1 interrupt f/f */
    { HRDATA (STAT2, fdc202[2].stat, 8) },      /* isbc202 2 status */
    { HRDATA (RTYP2, fdc202[2].rtype, 8) },     /* isbc202 2 result type */
    { HRDATA (RBYT2A, fdc202[2].rbyte0, 8) },   /* isbc202 2 result byte 0 */
    { HRDATA (RBYT2B, fdc202[0].rbyte1, 8) },   /* isbc202 2 result byte 1 */
    { HRDATA (INTFF2, fdc202[2].intff, 8) },    /* isbc202 2 interrupt f/f */
    { HRDATA (STAT3, fdc202[3].stat, 8) },      /* isbc202 3 status */
    { HRDATA (RTYP3, fdc202[3].rtype, 8) },     /* isbc202 3 result type */
    { HRDATA (RBYT3A, fdc202[3].rbyte0, 8) },   /* isbc202 3 result byte 0 */
    { HRDATA (RBYT3B, fdc202[3].rbyte1, 8) },   /* isbc202 3 result byte 1 */
    { HRDATA (INTFF3, fdc202[0].intff, 8) },    /* isbc202 3 interrupt f/f */
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
    NULL,               //reset
    NULL,               //boot
    &isbc202_attach,    //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    DEBUG_flow + DEBUG_read + DEBUG_write, //dctrl 
    isbc202_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* Hardware reset routine */

t_stat isbc202_reset(DEVICE *dptr, uint16 base)
{
    sim_printf("Initializing iSBC-202 FDC Board\n");
    if (SBC202_NUM) {
        sim_printf("   isbc202-%d: Hardware Reset\n", isbc202_fdcnum);
        sim_printf("   isbc202-%d: Registered at %04X\n", isbc202_fdcnum, base);
        fdc202[isbc202_fdcnum].baseport = base;
        reg_dev(isbc2020, base, isbc202_fdcnum);         //read status
        reg_dev(isbc2021, base + 1, isbc202_fdcnum);     //read rslt type/write IOPB addr-l
        reg_dev(isbc2022, base + 2, isbc202_fdcnum);     //write IOPB addr-h and start 
        reg_dev(isbc2023, base + 3, isbc202_fdcnum);     //read rstl byte 
        reg_dev(isbc2027, base + 7, isbc202_fdcnum);     //write reset isbc202
        isbc202_reset1(isbc202_fdcnum);
        isbc202_fdcnum++;
    } else
        sim_printf("   No isbc202 installed\n");
    return SCPE_OK;
}

/* Software reset routine */

void isbc202_reset1(uint8 fdcnum)
{
    int32 i;
    UNIT *uptr;

    sim_printf("   isbc202-%d: Software Reset\n", fdcnum);
    fdc202[fdcnum].stat = 0;            //clear status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = isbc202_dev.units + i;
        fdc202[fdcnum].stat |= FDCPRE | FDCDD; //set the FDC status
        fdc202[fdcnum].rtype = ROK;
        if (uptr->capac == 0) {         /* if not configured */
            uptr->u5 = fdcnum;          //fdc device number
            uptr->u6 = i;               //fdd unit number
            uptr->flags |= UNIT_WPMODE; /* set WP in unit flags */
            sim_printf("   SBC202%d: Configured, Status=%02X Not attached\n", i, fdc202[fdcnum].stat);
        } else {
            switch(i){
                case 0:
                    fdc202[fdcnum].stat |= RDY0; //set FDD 0 ready
                    fdc202[fdcnum].rbyte1 |= RB1RD0;
                    break;
                case 1:
                    fdc202[fdcnum].stat |= RDY1; //set FDD 1 ready
                    fdc202[fdcnum].rbyte1 |= RB1RD1;
                    break;
                case 2:
                    fdc202[fdcnum].stat |= RDY2; //set FDD 2 ready
                    fdc202[fdcnum].rbyte1 |= RB1RD2;
                    break;
                case 3:
                    fdc202[fdcnum].stat |= RDY3; //set FDD 3 ready
                    fdc202[fdcnum].rbyte1 |= RB1RD3;
                    break;
            }
            sim_printf("   SBC202%d: Configured, Status=%02X Attached to %s\n",
                i, fdc202[fdcnum].stat, uptr->filename);
        }
    }
}

/* isbc202 attach - attach an .IMG file to a FDD */

t_stat isbc202_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    FILE *fp;
    int32 i, c = 0;
    long flen;
    uint8 fdcnum, fddnum;

    sim_debug (DEBUG_flow, &isbc202_dev, "   isbc202_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   isbc202_attach: Attach error\n");
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
        if (fdc202[fdcnum].fdd[fddnum].buf == NULL) { /* no buffer allocated */
            fdc202[fdcnum].fdd[fddnum].buf = (uint8 *)malloc(flen);
            if (fdc202[fdcnum].fdd[fddnum].buf == NULL) {
                sim_printf("   isbc202_attach: Malloc error\n");
                return SCPE_MEM;
            }
        }
        uptr->capac = flen;
        i = 0;
        c = fgetc(fp);                  // copy disk image into buffer
        while (c != EOF) {
            *(fdc202[fdcnum].fdd[fddnum].buf + i++) = c & 0xFF;
            c = fgetc(fp);
        }
        fclose(fp);
        switch(fddnum){
            case 0:
                fdc202[fdcnum].stat |= RDY0; //set FDD 0 ready
                fdc202[fdcnum].rtype = ROK;
                fdc202[fdcnum].rbyte1 |= RB1RD0;
                break;
            case 1:
                fdc202[fdcnum].stat |= RDY1; //set FDD 1 ready
                fdc202[fdcnum].rtype = ROK;
                fdc202[fdcnum].rbyte1 |= RB1RD1;
                break;
            case 2:
                fdc202[fdcnum].stat |= RDY2; //set FDD 2 ready
                fdc202[fdcnum].rtype = ROK;
                fdc202[fdcnum].rbyte1 |= RB1RD2;
                break;
            case 3:
                fdc202[fdcnum].stat |= RDY3; //set FDD 3 ready
                fdc202[fdcnum].rtype = ROK;
                fdc202[fdcnum].rbyte1 |= RB1RD3;
                break;
        }
        if (flen == 512512) {           /* 8" 512K SSDD */
            fdc202[fdcnum].fdd[fddnum].maxcyl = 77;
            fdc202[fdcnum].fdd[fddnum].maxsec = 52;
        } else
            sim_printf("   iSBC-202-%d: Not a DD disk image\n", fdcnum);

        sim_printf("   iSBC-202%d: Configured %d bytes, Attached to %s\n",
            fdcnum, uptr->capac, uptr->filename);
    }
    sim_debug (DEBUG_flow, &isbc202_dev, "   isbc202_attach: Done\n");
    return SCPE_OK;
}

/* isbc202 set mode = Write protect */

t_stat isbc202_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
//    sim_debug (DEBUG_flow, &isbc202_dev, "   isbc202_set_mode: Entered with val=%08XH uptr->flags=%08X\n", 
//        val, uptr->flags);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
    } else {                            /* read write */
        uptr->flags &= ~val;
    }
//    sim_debug (DEBUG_flow, &isbc202_dev, "   isbc202_set_mode: Done\n");
    return SCPE_OK;
}

uint8 isbc202_get_dn(void)
{
    int i;

    for (i=0; i<SBC202_NUM; i++)
        if (port >= fdc202[i].baseport && port <= fdc202[i].baseport + 7)
            return i;
    sim_printf("isbc202_get_dn: port %04X not in isbc202 device table\n", port);
    return 0xFF;
}

/* ISBC202 control port functions */

uint8 isbc2020(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc202_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read ststus*/
            if (DEBUG)
                sim_printf("\n   isbc202-%d: returned status=%02X", fdcnum, fdc202[fdcnum].stat);
            return fdc202[fdcnum].stat;
        }
    }
    return 0;
}

uint8 isbc2021(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc202_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            fdc202[fdcnum].intff = 0;      //clear interrupt FF
            fdc202[fdcnum].stat &= ~FDCINT;
            if (DEBUG)
                sim_printf("\n   isbc202-%d: returned rtype=%02X intff=%02X status=%02X", 
                    fdcnum, fdc202[fdcnum].rtype, fdc202[fdcnum].intff, fdc202[fdcnum].stat);
            return fdc202[fdcnum].rtype;
        } else {                        /* write data port */
            fdc202[fdcnum].iopb = data;
            if (DEBUG)
                sim_printf("\n   isbc202-%d: IOPB low=%02X", fdcnum, data);
        }
    }
    return 0;
}

uint8 isbc2022(t_bool io, uint8 data)
{
    uint8 fdcnum;
    
    if ((fdcnum = isbc202_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            fdc202[fdcnum].iopb |= (data << 8);
            if (DEBUG)
                sim_printf("\n   isbc202-%d: IOPB=%04X", fdcnum, fdc202[fdcnum].iopb);
            isbc202_diskio(fdcnum);
            if (fdc202[fdcnum].intff)
                fdc202[fdcnum].stat |= FDCINT;
        }
    }
    return 0;
}

uint8 isbc2023(t_bool io, uint8 data)
{
    uint8 fdcnum, rslt;

    if ((fdcnum = isbc202_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            switch(fdc202[fdcnum].rtype) {
                case 0x00:
                    rslt = fdc202[fdcnum].rbyte0;
                    break;
                case 0x02:
                    rslt = fdc202[fdcnum].rbyte1;
                    break;
            }
            if (DEBUG)
                sim_printf("\n   isbc202-%d: returned result byte=%02X", fdcnum, rslt);
            return rslt;
        } else {                        /* write data port */
            ; //stop diskette operation
        }
    }
    return 0;
}

uint8 isbc2027(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = isbc202_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            isbc202_reset1(fdcnum);
        }
    }
    return 0;
}

// perform the actual disk I/O operation

void isbc202_diskio(uint8 fdcnum)
{
    uint8 cw, di, nr, ta, sa, data, nrptr, c;
    uint16 ba;
    uint32 dskoff;
    uint8 fddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    FILE *fp;
    //parse the IOPB 
    cw = multibus_get_mbyte(fdc202[fdcnum].iopb);
    di = multibus_get_mbyte(fdc202[fdcnum].iopb + 1);
    nr = multibus_get_mbyte(fdc202[fdcnum].iopb + 2);
    ta = multibus_get_mbyte(fdc202[fdcnum].iopb + 3);
    sa = multibus_get_mbyte(fdc202[fdcnum].iopb + 4);
    ba = multibus_get_mword(fdc202[fdcnum].iopb + 5);
    fddnum = (di & 0x30) >> 4;
    uptr = isbc202_dev.units + fddnum;
    if (DEBUG) {
        sim_printf("\n   isbc202-%d: isbc202_diskio IOPB=%04X FDD=%02X STAT=%02X",
            fdcnum, fdc202[fdcnum].iopb, fddnum, fdc202[fdcnum].stat);
        sim_printf("\n   isbc202-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X",
            fdcnum, cw, di, nr, ta, sa, ba);
        sim_printf("\n   isbc202-%d: maxsec=%02X maxcyl=%02X",
            fdcnum, fdc202[fdcnum].fdd[fddnum].maxsec, fdc202[fdcnum].fdd[fddnum].maxcyl);
    }
    //check for not ready
    switch(fddnum) {
        case 0:
            if ((fdc202[fdcnum].stat & RDY0) == 0) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0NR;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 1:
            if ((fdc202[fdcnum].stat & RDY1) == 0) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0NR;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 2:
            if ((fdc202[fdcnum].stat & RDY2) == 0) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0NR;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 3:
            if ((fdc202[fdcnum].stat & RDY3) == 0) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0NR;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        (sa > fdc202[fdcnum].fdd[fddnum].maxsec) ||
        ((sa + nr) > (fdc202[fdcnum].fdd[fddnum].maxsec + 1)) ||
        (sa == 0) ||
        (ta > fdc202[fdcnum].fdd[fddnum].maxcyl)
        ) {
            if (DEBUG)
                sim_printf("\n   isbc202-%d: maxsec=%02X maxcyl=%02X",
                    fdcnum, fdc202[fdcnum].fdd[fddnum].maxsec, fdc202[fdcnum].fdd[fddnum].maxcyl);
            fdc202[fdcnum].rtype = RERR;
            fdc202[fdcnum].rbyte0 = RB0ADR;
            fdc202[fdcnum].intff = 1;      //set interrupt FF
            sim_printf("\n   isbc202-%d: Address error on drive %d", fdcnum, fddnum);
            return;
    }
    switch (di & 0x07) {
        case DNOP:
        case DSEEK:
        case DHOME:
        case DVCRC:
            fdc202[fdcnum].rtype = ROK;
            fdc202[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DFMT:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0WP;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Write protect error 1 on drive %d", fdcnum, fddnum);
                return;
            }
            fmtb = multibus_get_mbyte(ba); //get the format byte
            //calculate offset into disk image
            dskoff = ((ta * (uint32)(fdc202[fdcnum].fdd[fddnum].maxsec)) + (sa - 1)) * 128;
            for(i=0; i<=((uint32)(fdc202[fdcnum].fdd[fddnum].maxsec) * 128); i++) {
                *(fdc202[fdcnum].fdd[fddnum].buf + (dskoff + i)) = fmtb;
            }
            //*** quick fix. Needs more thought!
            fp = fopen(uptr->filename, "wb"); // write out modified image
            for (i=0; i<uptr->capac; i++) {
                c = *(fdc202[fdcnum].fdd[fddnum].buf + i);
                fputc(c, fp);
            }
            fclose(fp);
            fdc202[fdcnum].rtype = ROK;
            fdc202[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * fdc202[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
//                sim_printf("\n   isbc202-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X dskoff=%06X",
//                    fdcnum, cw, di, nr, ta, sa, ba, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = *(fdc202[fdcnum].fdd[fddnum].buf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            fdc202[fdcnum].rtype = ROK;
            fdc202[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                fdc202[fdcnum].rtype = RERR;
                fdc202[fdcnum].rbyte0 = RB0WP;
                fdc202[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   isbc202-%d: Write protect error 2 on drive %d", fdcnum, fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * fdc202[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
 //               sim_printf("\n   isbc202-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X dskoff=%06X",
 //                   fdcnum, cw, di, nr, ta, sa, ba, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = multibus_get_mbyte(ba + i);
                    *(fdc202[fdcnum].fdd[fddnum].buf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            //*** quick fix. Needs more thought!
            fp = fopen(uptr->filename, "wb"); // write out modified image
            for (i=0; i<uptr->capac; i++) {
                c = *(fdc202[fdcnum].fdd[fddnum].buf + i);
                fputc(c, fp);
            }
            fclose(fp);
            fdc202[fdcnum].rtype = ROK;
            fdc202[fdcnum].intff = 1;      //set interrupt FF
            break;
        default:
            sim_printf("\n   isbc202-%d: isbc202_diskio bad di=%02X", fdcnum, di & 0x07);
            break;
    }
}

/* end of isbc202.c */
