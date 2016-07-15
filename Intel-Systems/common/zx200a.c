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

#define UNIT_V_WPMODE   (UNIT_V_UF)     /* Write protect */
#define UNIT_WPMODE     (1 << UNIT_V_WPMODE)

#define FDD_NUM         4

#define WP              0x40            /* Write protect */
#define RDY             0x20            /* Ready */
#define T0              0x10            /* Track 0 */
#define TS              0x08            /* Two sided */

/* internal function prototypes */

t_stat zx200a_svc (UNIT *uptr);
uint8 zx200a0(t_bool io, uint8 data, uint8 devnum);
uint8 zx200a1(t_bool io, uint8 data, uint8 devnum);
uint8 zx200a2(t_bool io, uint8 data, uint8 devnum);
uint8 zx200a3(t_bool io, uint8 data, uint8 devnum);
uint8 zx200a7(t_bool io, uint8 data, uint8 devnum);
t_stat zx200a_attach (UNIT *uptr, CONST char *cptr);
t_stat zx200a_reset(DEVICE *dptr, uint16 base, uint8 devnum);
void zx200a_reset1(void);

/* external function prototypes */

extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16 port, uint8 devnum);

/* globals */

uint16 iopb;
uint8 syssta = 0, rsttyp = 0, rstbyt1 = 0, rstbyt2 = 0, cmd = 0;

uint8 *zx200a_buf[FDD_NUM] = {         /* FDD buffer pointers */
    NULL,
    NULL,
    NULL,
    NULL
};

int32 fddst[FDD_NUM] = {                // fdd status
    0,                                  // status of FDD 0
    0,                                  // status of FDD 1
    0,                                  // status of FDD 2
    0                                   // status of FDD 3
};

int32 maxsec[FDD_NUM] = {               // last sector
    0,                                  // status of FDD 0
    0,                                  // status of FDD 1
    0,                                  // status of FDD 2
    0                                   // status of FDD 3
};

int8 maxcyl[FDD_NUM] = { 
    0,                                  // last cylinder + 1 of FDD 0
    0,                                  // last cylinder + 1 of FDD 1
    0,                                  // last cylinder + 1 of FDD 2
    0                                   // last cylinder + 1 of FDD 3
};

UNIT zx200a_unit[] = {
    { UDATA (&zx200a_svc, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (&zx200a_svc, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (&zx200a_svc, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 }, 
    { UDATA (&zx200a_svc, UNIT_ATTABLE+UNIT_DISABLE, 0), 20 } 
};

REG zx200a_reg[] = {
    { HRDATA (SUBSYSSTA, zx200a_unit[0].u3, 8) }, /* subsytem status */
    { HRDATA (RSTTYP, zx200a_unit[0].u4, 8) }, /* result type */
    { HRDATA (RSTBYT0, zx200a_unit[0].u5, 8) }, /* result byte 0 RSTTYP = 0*/
    { HRDATA (RSTBYT1, zx200a_unit[0].u6, 8) }, /* result byte 1 RSTTYP = 10*/
    { NULL }
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
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    zx200a_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

/* Service routines to handle simulator functions */

/* service routine - actually does the simulated disk I/O */

t_stat zx200a_svc (UNIT *uptr)
{
    sim_activate(&zx200a_unit[uptr->u6], zx200a_unit[uptr->u6].wait);
    return SCPE_OK;
}

/* zx200a control port functions */

uint8 zx200a0(t_bool io, uint8 data, uint8 devnum)
{
    int val;

    if (io == 0) {                      /* read operation */
        val = zx200a_unit[0].u3;
        sim_printf("   zx200a0: read data=%02X devnum=%02X val=%02X\n", data, devnum, val);
        return val;
    } else {                            /* write control port */
        sim_printf("   zx200a0: write data=%02X port=%02X\n", data, devnum);
    }
}

uint8 zx200a1(t_bool io, uint8 data, uint8 devnum)
{
    int val;

    if (io == 0) {                      /* read operation */
        val = zx200a_unit[0].u4;
        sim_printf("   zx200a1: read data=%02X devnum=%02X val=%02X\n", data, devnum, val);
        return val;
    } else {                            /* write control port */
        iopb = data;
        sim_printf("   zx200a1: write data=%02X port=%02X iopb=%04X\n", data, devnum, iopb);
    }
}

uint8 zx200a2(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read operation */
        sim_printf("   zx200a2: read data=%02X devnum=%02X\n", data, devnum);
        return 0x00;
    } else {                            /* write control port */
        iopb |= (data << 8);
        sim_printf("   zx200a2: write data=%02X port=%02X iopb=%04X\n", data, devnum, iopb);
    }
}

uint8 zx200a3(t_bool io, uint8 data, uint8 devnum)
{
    int val;

    if (io == 0) {                      /* read operation */
        if (zx200a_unit[0].u4)
            val = zx200a_unit[0].u5;
        else
            val = zx200a_unit[0].u6;
        sim_printf("   zx200a3: read data=%02X devnum=%02X val=%02X\n", data, devnum, val);
        return val;
    } else {                            /* write control port */
        sim_printf("   zx200a3: write data=%02X port=%02X\n", data, devnum);
    }
}

/* reset ZX-200A */
uint8 zx200a7(t_bool io, uint8 data, uint8 devnum)
{
    if (io == 0) {                      /* read operation */
        sim_printf("   zx200a7: read data=%02X devnum=%02X\n", data, devnum);
        return 0x00;
    } else {                            /* write control port */
        sim_printf("   zx200a7: write data=%02X port=%02X\n", data, devnum);
        zx200a_reset(NULL, ZX200A_BASE_DD, 0); //for now
    }
}

/* zx200a attach - attach an .IMG file to a FDD */

t_stat zx200a_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    FILE *fp;
    int32 i, c = 0;
    long flen;

    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_attach: Entered with uptr=%08X cptr=%s\n", uptr, cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        sim_printf("   zx200a_attach: Attach error\n");
        return r;
    }
    fp = fopen(uptr->filename, "rb");
    if (fp == NULL) {
        sim_printf("   Unable to open disk image file %s\n", uptr->filename);
        sim_printf("   No disk image loaded!!!\n");
    } else {
        sim_printf("zx200a: Attach\n");
        fseek(fp, 0, SEEK_END);         /* size disk image */
        flen = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (zx200a_buf[uptr->u6] == NULL) { /* no buffer allocated */
            zx200a_buf[uptr->u6] = (uint8 *)malloc(flen);
            if (zx200a_buf[uptr->u6] == NULL) {
                sim_printf("   zx200a_attach: Malloc error\n");
                return SCPE_MEM;
            }
        }
        uptr->capac = flen;
        i = 0;
        c = fgetc(fp);                  // copy disk image into buffer
        while (c != EOF) {
            *(zx200a_buf[uptr->u6] + i++) = c & 0xFF;
            c = fgetc(fp);
        }
        fclose(fp);
        switch(uptr->u6){
        case 0:
            fddst[uptr->u6] |= 0x01;    /* set unit ready */
            break;
        case 1:
            fddst[uptr->u6] |= 0x02;    /* set unit ready */
            break;
        case 2:
            fddst[uptr->u6] |= 0x20;    /* set unit ready */
            break;
        case 3:
            fddst[uptr->u6] |= 0x40;    /* set unit ready */
            break;
        }
        if (flen == 256256) {           /* 8" 256K SSSD */
            maxcyl[uptr->u6] = 77;
            maxsec[uptr->u6] = 26;
        }
        else if (flen == 512512) {      /* 8" 512K SSDD */
            maxcyl[uptr->u6] = 77;
            maxsec[uptr->u6] = 52;
        }
        sim_printf("   Drive-%d: %d bytes of disk image %s loaded, fddst=%02X\n", 
            uptr->u6, i, uptr->filename, fddst[uptr->u6]);
    }
    sim_debug (DEBUG_flow, &zx200a_dev, "   zx200a_attach: Done\n");
    return SCPE_OK;
}
/* Reset routine */

t_stat zx200a_reset(DEVICE *dptr, uint16 base, uint8 devnum)
{
    reg_dev(zx200a0, base, devnum); 
    reg_dev(zx200a1, base + 1, devnum); 
    reg_dev(zx200a2, base + 2, devnum); 
    reg_dev(zx200a3, base + 3, devnum); 
    reg_dev(zx200a7, base + 7, devnum); 
    zx200a_unit[devnum].u3 = 0x00; /* ipc reset */
    sim_printf("   zx200a-%d: Reset\n", devnum);
    sim_printf("   zx200a-%d: Registered at %04X\n", devnum, base);
    if ((zx200a_dev.flags & DEV_DIS) == 0) 
        zx200a_reset1();
    return SCPE_OK;
}

void zx200a_reset1(void)
{
    int32 i;
    UNIT *uptr;
    static int flag = 1;

    if (flag) sim_printf("ZX-200A: Initializing\n");
    for (i = 0; i < FDD_NUM; i++) {     /* handle all units */
        uptr = zx200a_dev.units + i;
        if (uptr->capac == 0) {         /* if not configured */
//            sim_printf("   ZX-200A%d: Not configured\n", i);
//            if (flag) {
//                sim_printf("      ALL: \"set ZX-200A en\"\n");
//                sim_printf("      EPROM: \"att ZX-200A0 <filename>\"\n");
//                flag = 0;
//            }
            uptr->capac = 0;            /* initialize unit */
            uptr->u3 = 0; 
            uptr->u4 = 0;
            uptr->u5 = 0;
            uptr->u6 = i;               /* unit number - only set here! */
            fddst[i] = WP + T0 + i;     /* initial drive status */
            uptr->flags |= UNIT_WPMODE; /* set WP in unit flags */
            sim_activate (&zx200a_unit[uptr->u6], zx200a_unit[uptr->u6].wait);
        } else {
            fddst[i] = RDY + WP + T0 + i; /* initial attach drive status */
//            sim_printf("   SBC208%d: Configured, Attached to %s\n", i, uptr->filename);
        }
    }
    cmd = 0;                            /* clear command */
    flag = 0;
}
/* end of zx-200a.c */
