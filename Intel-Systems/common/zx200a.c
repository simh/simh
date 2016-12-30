/*  zx-200a.c: Intel double density disk adapter adapter

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

#define  DEBUG   1

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

/* external function prototypes */

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16, uint8);
extern uint8 multibus_get_mbyte(uint16 addr);
extern uint16 multibus_get_mword(uint16 addr);
extern void multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 multibus_put_mword(uint16 addr, uint16 val);

/* external globals */

extern uint16 port;                     //port called in dev_table[port]

/* internal function prototypes */

t_stat zx200a_reset(DEVICE *dptr, uint16 base);
void zx200a_reset1(uint8);
t_stat zx200a_attach (UNIT *uptr, CONST char *cptr);
t_stat zx200a_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
uint8 zx200a0(t_bool io, uint8 data);
uint8 zx200a1(t_bool io, uint8 data);
uint8 zx200a2(t_bool io, uint8 data);
uint8 zx200a3(t_bool io, uint8 data);
uint8 zx200a7(t_bool io, uint8 data);
void zx200a_diskio(uint8 fdcnum);

/* globals */

int32 zx200a_fdcnum = 0;               //actual number of SBC-202 instances + 1

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

FDCDEF    zx200a[4];                    //indexed by the isbc-202 instance number

UNIT zx200a_unit[] = {
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (0, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 } 
};

REG zx200a_reg[] = {
    { HRDATA (STAT0, zx200a[0].stat, 8) },      /* zx200a 0 status */
    { HRDATA (RTYP0, zx200a[0].rtype, 8) },     /* zx200a 0 result type */
    { HRDATA (RBYT0A, zx200a[0].rbyte0, 8) },   /* zx200a 0 result byte 0 */
    { HRDATA (RBYT0B, zx200a[0].rbyte1, 8) },   /* zx200a 0 result byte 1 */
    { HRDATA (INTFF0, zx200a[0].intff, 8) },    /* zx200a 0 interrupt f/f */
    { HRDATA (STAT1, zx200a[1].stat, 8) },      /* zx200a 1 status */
    { HRDATA (RTYP1, zx200a[1].rtype, 8) },     /* zx200a 1 result type */
    { HRDATA (RBYT1A, zx200a[1].rbyte0, 8) },   /* zx200a 1 result byte 0 */
    { HRDATA (RBYT1B, zx200a[1].rbyte1, 8) },   /* zx200a 1 result byte 1 */
    { HRDATA (INTFF1, zx200a[1].intff, 8) },    /* zx200a 1 interrupt f/f */
    { HRDATA (STAT2, zx200a[2].stat, 8) },      /* zx200a 2 status */
    { HRDATA (RTYP2, zx200a[2].rtype, 8) },     /* zx200a 2 result type */
    { HRDATA (RBYT2A, zx200a[2].rbyte0, 8) },   /* zx200a 2 result byte 0 */
    { HRDATA (RBYT2B, zx200a[0].rbyte1, 8) },   /* zx200a 2 result byte 1 */
    { HRDATA (INTFF2, zx200a[2].intff, 8) },    /* zx200a 2 interrupt f/f */
    { HRDATA (STAT3, zx200a[3].stat, 8) },      /* zx200a 3 status */
    { HRDATA (RTYP3, zx200a[3].rtype, 8) },     /* zx200a 3 result type */
    { HRDATA (RBYT3A, zx200a[3].rbyte0, 8) },   /* zx200a 3 result byte 0 */
    { HRDATA (RBYT3B, zx200a[3].rbyte1, 8) },   /* zx200a 3 result byte 1 */
    { HRDATA (INTFF3, zx200a[0].intff, 8) },    /* zx200a 3 interrupt f/f */
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
    "ZX200A",             //name
    zx200a_unit,         //units
    zx200a_reg,          //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &zx200a_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    &zx200a_attach,     //attach  
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    DEBUG_flow + DEBUG_read + DEBUG_write, //dctrl 
    zx200a_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* Service routines to handle simulator functions */

/* Reset routine */

t_stat zx200a_reset(DEVICE *dptr, uint16 base)
{
    sim_printf("Initializing ZX-200A FDC Board\n");
    if (ZX200A_NUM) {
        sim_printf("   ZX200A-%d: Hardware Reset\n", zx200a_fdcnum);
        sim_printf("   ZX200A-%d: Registered at %04X\n", zx200a_fdcnum, base);
        zx200a[zx200a_fdcnum].baseport = base;
        reg_dev(zx200a0, base, zx200a_fdcnum); 
        reg_dev(zx200a1, base + 1, zx200a_fdcnum); 
        reg_dev(zx200a2, base + 2, zx200a_fdcnum); 
        reg_dev(zx200a3, base + 3, zx200a_fdcnum); 
        reg_dev(zx200a7, base + 7, zx200a_fdcnum); 
        zx200a_unit[zx200a_fdcnum].u3 = 0x00; /* ipc reset */
        zx200a_reset1(zx200a_fdcnum);
        zx200a_fdcnum++;
    } else
        sim_printf("   No ZX-200A installed\n");
    return SCPE_OK;
}

void zx200a_reset1(uint8 fdcnum)
{
    int32 i;
    UNIT *uptr;

    sim_printf("   ZX-200A-%d: Initializing\n", fdcnum);
    zx200a[fdcnum].stat = 0;            //clear status
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = zx200a_dev.units + i;
        zx200a[fdcnum].stat |= FDCPRE | FDCDD; //set the FDC status
        zx200a[fdcnum].rtype = ROK;
        if (uptr->capac == 0) {         /* if not configured */
            uptr->capac = 0;            /* initialize unit */
            uptr->u4 = 0;
            uptr->u5 = fdcnum;          //fdc device number
            uptr->u6 = i;               /* unit number - only set here! */
            uptr->flags |= UNIT_WPMODE; /* set WP in unit flags */
            sim_printf("   ZX-200A%d: Configured, Status=%02X Not attached\n", i, zx200a[fdcnum].stat);
        } else {
            switch(i){
                case 0:
                    zx200a[fdcnum].stat |= RDY0; //set FDD 0 ready
                    zx200a[fdcnum].rbyte1 |= RB1RD0;
                    break;
                case 1:
                    zx200a[fdcnum].stat |= RDY1; //set FDD 1 ready
                    zx200a[fdcnum].rbyte1 |= RB1RD1;
                    break;
                case 2:
                    zx200a[fdcnum].stat |= RDY2; //set FDD 2 ready
                    zx200a[fdcnum].rbyte1 |= RB1RD2;
                    break;
                case 3:
                    zx200a[fdcnum].stat |= RDY3; //set FDD 3 ready
                    zx200a[fdcnum].rbyte1 |= RB1RD3;
                    break;
            }
            sim_printf("   ZX-200A%d: Configured, Status=%02X Attached to %s\n",
                i, zx200a[fdcnum].stat, uptr->filename);
        }
    }
}

/* zx200a attach - attach an .IMG file to a FDD */

t_stat zx200a_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    FILE *fp;
    int32 i, c = 0;
    long flen;
    uint8 fdcnum, fddnum;

    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   zx200a_attach: Attach error\n");
        return r;
    }
    fdcnum = uptr->u5;
    fddnum = uptr->u6;
    fp = fopen(uptr->filename, "rb");
    if (fp == NULL) {
        sim_printf("   Unable to open disk image file %s\n", uptr->filename);
        sim_printf("   No disk image loaded!!!\n");
    } else {
        sim_printf("zx200a: Attach\n");
        fseek(fp, 0, SEEK_END);         /* size disk image */
        flen = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (zx200a[fdcnum].fdd[fddnum].buf == NULL) { /* no buffer allocated */
            zx200a[fdcnum].fdd[fddnum].buf = (uint8 *)malloc(flen);
            if (zx200a[fdcnum].fdd[fddnum].buf == NULL) {
                sim_printf("   zx200a_attach: Malloc error\n");
                return SCPE_MEM;
            }
        }
        uptr->capac = flen;
        i = 0;
        c = fgetc(fp);                  // copy disk image into buffer
        while (c != EOF) {
            *(zx200a[fdcnum].fdd[fddnum].buf + i++) = c & 0xFF;
            c = fgetc(fp);
        }
        fclose(fp);
        switch(fddnum){
            case 0:
                zx200a[fdcnum].stat |= RDY0; //set FDD 0 ready
                zx200a[fdcnum].rtype = ROK;
                zx200a[fdcnum].rbyte1 |= RB1RD0;
                break;
            case 1:
                zx200a[fdcnum].stat |= RDY1; //set FDD 1 ready
                zx200a[fdcnum].rtype = ROK;
                zx200a[fdcnum].rbyte1 |= RB1RD1;
                break;
            case 2:
                zx200a[fdcnum].stat |= RDY2; //set FDD 2 ready
                zx200a[fdcnum].rtype = ROK;
                zx200a[fdcnum].rbyte1 |= RB1RD2;
                break;
            case 3:
                zx200a[fdcnum].stat |= RDY3; //set FDD 3 ready
                zx200a[fdcnum].rtype = ROK;
                zx200a[fdcnum].rbyte1 |= RB1RD3;
                break;
            }
        if (flen == 256256) {           /* 8" 256K SSSD */
            zx200a[fdcnum].fdd[fddnum].maxcyl = 77;
            zx200a[fdcnum].fdd[fddnum].maxsec = 26;
        }
        else if (flen == 512512) {      /* 8" 512K SSDD */
            zx200a[fdcnum].fdd[fddnum].maxcyl = 77;
            zx200a[fdcnum].fdd[fddnum].maxsec = 52;
        } else
            sim_printf("   ZX-200A-%d: Not a known ISIS-II disk image\n", fdcnum);
        sim_printf("   ZX-200A-%d: Configured %d bytes, Attached to %s\n",
            fdcnum, uptr->capac, uptr->filename);
    }
    sim_debug (DEBUG_flow, &zx200a_dev, "   ZX-200A_attach: Done\n");
    return SCPE_OK;
}

/* zx200a set mode = Write protect */

t_stat zx200a_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
//    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_set_mode: Entered with val=%08XH uptr->flags=%08X\n", 
//        val, uptr->flags);
    if (val & UNIT_WPMODE) {            /* write protect */
        uptr->flags |= val;
    } else {                            /* read write */
        uptr->flags &= ~val;
    }
//    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_set_mode: Done\n");
    return SCPE_OK;
}

uint8 zx200_get_dn(void)
{
    int i;

    for (i=0; i<ZX200A_NUM; i++)
        if (port >= zx200a[i].baseport && port <= zx200a[i].baseport + 7)
            return i;
    sim_printf("zx200_get_dn: port %04X not in zx200 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* zx200a control port functions */

uint8 zx200a0(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = zx200_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read ststus*/
//            sim_printf("\n   ZX200A-%d: returned status=%02X", fdcnum, zx200a[fdcnum].stat);
            return zx200a[fdcnum].stat;
        }
    }
    return 0;
}

uint8 zx200a1(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = zx200_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read operation */
            zx200a[fdcnum].intff = 0;      //clear interrupt FF
            zx200a[fdcnum].stat &= ~FDCINT;
            if (DEBUG)
                sim_printf("\n   ZX-200A1-%d: returned rtype=%02X intff=%02X status=%02X", 
                    fdcnum, zx200a[fdcnum].rtype, zx200a[fdcnum].intff, zx200a[fdcnum].stat);
            return zx200a[fdcnum].rtype;
        } else {                            /* write control port */
            zx200a[fdcnum].iopb = data;
            if (DEBUG)
                sim_printf("\n   ZX-200A1-%d: IOPB low=%02X", fdcnum, data);
        }
    }
    return 0;
}

uint8 zx200a2(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = zx200_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            zx200a[fdcnum].iopb |= (data << 8);
            if (DEBUG)
                sim_printf("\n   zx200a-%d: IOPB=%04X", fdcnum, zx200a[fdcnum].iopb);
            zx200a_diskio(fdcnum);
            if (zx200a[fdcnum].intff)
                zx200a[fdcnum].stat |= FDCINT;
        }
    }
    return 0;
}

uint8 zx200a3(t_bool io, uint8 data)
{
    uint8 fdcnum, rslt;

    if ((fdcnum = zx200_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            switch(zx200a[fdcnum].rtype) {
                case 0x00:
                    rslt = zx200a[fdcnum].rbyte0;
                    break;
                case 0x02:
                    rslt = zx200a[fdcnum].rbyte1;
                    break;
            }
            if (DEBUG)
                sim_printf("\n   zx200a-%d: returned result byte=%02X", fdcnum, rslt);
            return rslt;
        } else {                        /* write data port */
            ; //stop diskette operation
        }
    }
    return 0;
}

/* reset ZX-200A */
uint8 zx200a7(t_bool io, uint8 data)
{
    uint8 fdcnum;

    if ((fdcnum = zx200_get_dn()) != 0xFF) {
        if (io == 0) {                  /* read data port */
            ;
        } else {                        /* write data port */
            zx200a_reset1(fdcnum);
        }
    }
    return 0;
}

// perform the actual disk I/O operation

void zx200a_diskio(uint8 fdcnum)
{
    uint8 cw, di, nr, ta, sa, data, nrptr, c;
    uint16 ba;
    uint32 dskoff;
    uint8 fddnum, fmtb;
    uint32 i;
    UNIT *uptr;
    FILE *fp;
    //parse the IOPB 
    cw = multibus_get_mbyte(zx200a[fdcnum].iopb);
    di = multibus_get_mbyte(zx200a[fdcnum].iopb + 1);
    nr = multibus_get_mbyte(zx200a[fdcnum].iopb + 2);
    ta = multibus_get_mbyte(zx200a[fdcnum].iopb + 3);
    sa = multibus_get_mbyte(zx200a[fdcnum].iopb + 4);
    ba = multibus_get_mword(zx200a[fdcnum].iopb + 5);
    fddnum = (di & 0x30) >> 4;
    uptr = zx200a_dev.units + fddnum;
    if (DEBUG) {
        sim_printf("\n   zx200a-%d: zx200a_diskio IOPB=%04X FDD=%02X STAT=%02X",
            fdcnum, zx200a[fdcnum].iopb, fddnum, zx200a[fdcnum].stat);
        sim_printf("\n   zx200a-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X",
            fdcnum, cw, di, nr, ta, sa, ba);
        sim_printf("\n   zx200a-%d: maxsec=%02X maxcyl=%02X",
            fdcnum, zx200a[fdcnum].fdd[fddnum].maxsec, zx200a[fdcnum].fdd[fddnum].maxcyl);
    }
    //check for not ready
    switch(fddnum) {
        case 0:
            if ((zx200a[fdcnum].stat & RDY0) == 0) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0NR;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 1:
            if ((zx200a[fdcnum].stat & RDY1) == 0) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0NR;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 2:
            if ((zx200a[fdcnum].stat & RDY2) == 0) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0NR;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
        case 3:
            if ((zx200a[fdcnum].stat & RDY3) == 0) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0NR;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Ready error on drive %d", fdcnum, fddnum);
                return;
            }
            break;
    }
    //check for address error
    if (
        (sa > zx200a[fdcnum].fdd[fddnum].maxsec) ||
        ((sa + nr) > (zx200a[fdcnum].fdd[fddnum].maxsec + 1)) ||
        (sa == 0) ||
        (ta > zx200a[fdcnum].fdd[fddnum].maxcyl)
        ) {
            if (DEBUG)
                sim_printf("\n   zx200a-%d: maxsec=%02X maxcyl=%02X",
                    fdcnum, zx200a[fdcnum].fdd[fddnum].maxsec, zx200a[fdcnum].fdd[fddnum].maxcyl);
            zx200a[fdcnum].rtype = RERR;
            zx200a[fdcnum].rbyte0 = RB0ADR;
            zx200a[fdcnum].intff = 1;      //set interrupt FF
            sim_printf("\n   zx200a-%d: Address error on drive %d", fdcnum, fddnum);
            return;
    }
    switch (di & 0x07) {
        case DNOP:
        case DSEEK:
        case DHOME:
        case DVCRC:
            zx200a[fdcnum].rtype = ROK;
            zx200a[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DFMT:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0WP;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Write protect error 1 on drive %d", fdcnum, fddnum);
                return;
            }
            fmtb = multibus_get_mbyte(ba); //get the format byte
            //calculate offset into disk image
            dskoff = ((ta * (uint32)(zx200a[fdcnum].fdd[fddnum].maxsec)) + (sa - 1)) * 128;
            for(i=0; i<=((uint32)(zx200a[fdcnum].fdd[fddnum].maxsec) * 128); i++) {
                *(zx200a[fdcnum].fdd[fddnum].buf + (dskoff + i)) = fmtb;
            }
            //*** quick fix. Needs more thought!
            fp = fopen(uptr->filename, "wb"); // write out modified image
            for (i=0; i<uptr->capac; i++) {
                c = *(zx200a[fdcnum].fdd[fddnum].buf + i);
                fputc(c, fp);
            }
            fclose(fp);
            zx200a[fdcnum].rtype = ROK;
            zx200a[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DREAD:
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * zx200a[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
//                sim_printf("\n   zx200a-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X dskoff=%06X",
//                    fdcnum, cw, di, nr, ta, sa, ba, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = *(zx200a[fdcnum].fdd[fddnum].buf + (dskoff + i));
                    multibus_put_mbyte(ba + i, data);
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            zx200a[fdcnum].rtype = ROK;
            zx200a[fdcnum].intff = 1;      //set interrupt FF
            break;
        case DWRITE:
            //check for WP
            if(uptr->flags & UNIT_WPMODE) {
                zx200a[fdcnum].rtype = RERR;
                zx200a[fdcnum].rbyte0 = RB0WP;
                zx200a[fdcnum].intff = 1;  //set interrupt FF
                sim_printf("\n   zx200a-%d: Write protect error 2 on drive %d", fdcnum, fddnum);
                return;
            }
            nrptr = 0;
            while(nrptr < nr) {
                //calculate offset into disk image
                dskoff = ((ta * zx200a[fdcnum].fdd[fddnum].maxsec) + (sa - 1)) * 128;
 //               sim_printf("\n   zx200a-%d: cw=%02X di=%02X nr=%02X ta=%02X sa=%02X ba=%04X dskoff=%06X",
 //                   fdcnum, cw, di, nr, ta, sa, ba, dskoff);
                for (i=0; i<128; i++) {     //copy sector from image to RAM
                    data = multibus_get_mbyte(ba + i);
                    *(zx200a[fdcnum].fdd[fddnum].buf + (dskoff + i)) = data;
                }
                sa++;
                ba+=0x80;
                nrptr++;
            }
            //*** quick fix. Needs more thought!
            fp = fopen(uptr->filename, "wb"); // write out modified image
            for (i=0; i<uptr->capac; i++) {
                c = *(zx200a[fdcnum].fdd[fddnum].buf + i);
                fputc(c, fp);
            }
            fclose(fp);
            zx200a[fdcnum].rtype = ROK;
            zx200a[fdcnum].intff = 1;      //set interrupt FF
            break;
        default:
            sim_printf("\n   zx200a-%d: zx200a_diskio bad di=%02X", fdcnum, di & 0x07);
            break;
    }
}

/* end of zx-200a.c */
