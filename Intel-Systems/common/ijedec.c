/*  iJEDEC.c: Intel JEDEC Universal Site simulator for SBCs

    Copyright (c) 2011, William A. Beech

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

    These functions support a simulated i2732 JEDEC device on an iSBC.  This 
    allows the attachment of the device to a binary file containing the JEDEC
    code.

    Unit will support 8, 16 and 32 KB EPROMs as well as 8 and 16 KB static 
    RAMs in the JEDEC sockets.  Units must be configured for 8KB for 8KB 
    SRAM and 32KB for 32KB SRAM.  If configured for 16KB, SRAM cannot be 
    configured.  Size is set by configuring the top JEDEC site for an EPROM.  
    Size and spacing for the other JEDEC units is derived from the top JEDEC 
    site configuration.  Changing the top JEDEC site will clear the 
    configuration of all other JEDEC sites. The JEDEC driver can be set for
    either 8- or 16bit data access.
    
    The top JEDEC site can only be configured to contain an EPROM.  
    It contains the reset address for the 8088, 8086, 80188, 80186, 
    and 80286.

    For illustration 8-bit mode - 4 Sites - configured for 8KB chips

    +--------+ 0xFFFFF
    |        |
    | jedec3 | Only ROM
    |        |
    +--------+ 0xFE000

    +--------+ 0xFDFFF
    |        |
    | jedec2 | RAM/ROM
    |        |
    +--------+ 0xFC000

    +--------+ 0xFBFFF
    |        |
    | jedec1 | RAM/ROM
    |        |
    +--------+ 0xFA000

    +--------+ 0xF9FFF
    |        |
    | jedec0 | RAM/ROM
    |        |
    +--------+ 0xF8000

    For illustration 16-bit mode - 4 Sites - configured for 8KB chips

    Odd data byte           Even data byte
    High data byte          Low data byte
    +--------+ 0xFFFFF      +--------+ 0xFFFFE
    |        |              |        |
    | jedec3 | Only ROM     | jedec2 | Only ROM
    |        |              |        |
    +--------+ 0xFC001      +--------+ 0xFC000 

    +--------+ 0xFBFFF      +--------+ 0xFBFFE
    |        |              |        |
    | jedec3 | RAM/ROM      | jedec2 | RAM/ROM
    |        |              |        |
    +--------+ 0xF8001      +--------+ 0xF8000

    uptr->filename - ROM image file attached to unit
    uptr->capac - unit capacity in bytes
    uptr->u3 - unit base address
    uptr->u4 - unit device type {none|8krom|16krom|32krom|8kram|32kram}
    uptr->u5 - unit flags - ROM or RAM, 8 or 16BIT (top unit only)
    uptr->u6 - unit number
*/

#include <stdio.h>

#include "multibus_defs.h"

#define JEDEC_NUM       4

#define UNIT_V_DMODE    (UNIT_V_UF)         /* data bus mode */
#define UNIT_DMODE      (1 << UNIT_V_DMODE)
#define UNIT_V_MSIZE    (UNIT_V_UF+1)       /* Memory Size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define UNIT_NONE       0                   /* No device */
#define UNIT_8KROM      1                   /* 8KB ROM */
#define UNIT_16KROM     2                   /* 16KB ROM */
#define UNIT_32KROM     3                   /* 32KB ROM */
#define UNIT_8KRAM      4                   /* 8KB RAM */
#define UNIT_32KRAM     5                   /* 32KB RAM */

#define RAM             0x00000001
#define D16BIT          0x00000002

/* function prototypes */

t_stat JEDEC_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat JEDEC_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat JEDEC_attach (UNIT *uptr, char *cptr);
t_stat JEDEC_reset (DEVICE *dptr);
int32 JEDEC_get_mbyte(int32 addr);
void JEDEC_put_mbyte(int32 addr, int32 val);

/* SIMH JEDEC Standard I/O Data Structures */

UNIT JEDEC_unit[] = {
    { UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0),0 },
    { UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0),0 },
    { UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0),0 },
    { UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0),0 }
};

MTAB JEDEC_mod[] = {
    { UNIT_DMODE, 0, "8-Bit", "8B", &JEDEC_set_mode },
    { UNIT_DMODE, UNIT_DMODE, "16-Bit", "16B", &JEDEC_set_mode },
    { UNIT_MSIZE, UNIT_NONE, "Not configured", "NONE", &JEDEC_set_size },
    { UNIT_MSIZE, UNIT_8KROM, "8KB ROM", "8KROM", &JEDEC_set_size },
    { UNIT_MSIZE, UNIT_16KROM, "16KB ROM", "16KROM", &JEDEC_set_size },
    { UNIT_MSIZE, UNIT_32KROM, "32KB ROM", "32KROM", &JEDEC_set_size },
    { UNIT_MSIZE, UNIT_8KRAM, "8KB RAM", "8KRAM", &JEDEC_set_size },
    { UNIT_MSIZE, UNIT_32KRAM, "32KB RAM", "32KRAM", &JEDEC_set_size },
    { 0 }
};

DEBTAB JEDEC_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE JEDEC_dev = {
    "JEDEC",            //name
    JEDEC_unit,         //units
    NULL,               //registers
    JEDEC_mod,          //modifiers
    JEDEC_NUM,          //numunits
    16,                 //aradix 
    32,                 //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth
    NULL,               //examine 
    NULL,               //deposit 
    &JEDEC_reset,       //reset
    NULL,               //boot
    &JEDEC_attach,      //attach 
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    JEDEC_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* global variables */

uint8 *JEDEC_buf[JEDEC_NUM] = {             /* JEDEC buffer pointers */
    NULL,
    NULL,
    NULL,
    NULL
};

/* JEDEC functions */

/* JEDEC attach - force JEDEC reset at completion */

t_stat JEDEC_attach (UNIT *uptr, char *cptr)
{
    t_stat r;

    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_attach: Entered with cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) { 
        if (JEDEC_dev.dctrl & DEBUG_flow)
            sim_printf("\tJEDEC_attach: Error\n");
        return r;
    }
    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_attach: Done\n");
    return (JEDEC_reset (NULL));
}

/* JEDEC set mode = 8- or 16-bit data bus */

t_stat JEDEC_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    UNIT *uptr1;

    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_set_mode: Entered with val=%08XH, unit=%d\n", val, uptr->u6);
    uptr1 = JEDEC_dev.units + JEDEC_NUM - 1; /* top unit holds this configuration */
    if (val) {                              /* 16-bit mode */
        uptr1->u5 |= D16BIT;
    } else {                                /* 8-bit mode */
        uptr1->u5 &= ~D16BIT;
    }
    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("JEDEC%d->u5=%08XH\n", JEDEC_NUM - 1, uptr1->u5);
        sim_printf("\tJEDEC_set_mode: Done\n");
}

/* JEDEC set type = none, 8krom, 16krom, 32krom, 8kram or 32kram */

t_stat JEDEC_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    uint32 i, basadr;
    UNIT *uptr1;

    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_set_size: Entered with val=%d, unit=%d\n", val, uptr->u6);
    uptr1 = JEDEC_dev.units + JEDEC_NUM - 1; /* top unit holds u5 configuration */
    uptr->u4 = val;
    switch(val) {
        case UNIT_NONE:
            uptr->capac = 0;
            uptr->u5 &= ~RAM;               /* ROM */
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                uptr->u3 = 0;               /* base address */
                sim_printf("JEDEC site size set to 8KB\n");
                for (i = 0; i < JEDEC_NUM-1; i++) {     /* clear all units but last unit */
                    uptr1 = JEDEC_dev.units + i;
                    uptr1->capac = 0;
                }
            }
            break;
        case UNIT_8KROM:
            uptr->capac = 0x2000;
            uptr1->u5 &= ~RAM;               /* ROM */
            basadr = 0x100000 - (uptr->capac * JEDEC_NUM);
            sim_printf("JEDEC site base address = %06XH\n", basadr);
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                uptr->u3 = basadr + (uptr->capac * uptr->u6); /* base address */
                sim_printf("JEDEC site size set to 8KB\n");
                for (i = 0; i < JEDEC_NUM-1; i++) {     /* clear all units but last unit */
                    uptr1 = JEDEC_dev.units + i;
                    uptr1->capac = 0;
                }
            } else {
                if (uptr1->capac != uptr->capac) {
                    uptr->capac = 0;
                    sim_printf("JEDEC site size precludes use of this device\n");
                }
            }
            break;
        case UNIT_16KROM:
            uptr->capac = 0x4000;
            uptr1->u5 &= ~RAM;               /* ROM */
            basadr = 0x100000 - (uptr->capac * JEDEC_NUM);
            sim_printf("JEDEC site base address = %06XH\n", basadr);
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                uptr->u3 = basadr + (uptr->capac * uptr->u6); /* base address */
                sim_printf("JEDEC site size set to 16KB\n");
                for (i = 0; i < JEDEC_NUM-1; i++) {     /* clear all units but last unit */
                    uptr1 = JEDEC_dev.units + i;
                    uptr1->capac = 0;
                }
            } else {
                if (uptr1->capac != uptr->capac) {
                    uptr->capac = 0;
                    sim_printf("JEDEC site size precludes use of this device\n");
                }
            }
            break;
        case UNIT_32KROM:
            uptr->capac = 0x8000;
            uptr1->u5 &= ~RAM;               /* ROM */
            basadr = 0x100000 - (uptr->capac * JEDEC_NUM);
            sim_printf("JEDEC site base address = %06XH\n", basadr);
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                uptr->u3 = basadr + (uptr->capac * uptr->u6); /* base address */
                sim_printf("JEDEC site size set to 32KB\n");
                for (i = 0; i < JEDEC_NUM-1; i++) {        /* clear all units but last unit */
                    uptr1 = JEDEC_dev.units + i;
                    uptr1->capac = 0;
                }
            } else {
                if (uptr1->capac != uptr->capac) {
                    uptr->capac = 0;
                    sim_printf("JEDEC site size precludes use of this device\n");
                }
            }
            break;
        case UNIT_8KRAM:
            uptr->capac = 0x2000;
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                sim_printf("JEDEC%d cannot be SRAM\n", uptr->u6);
            } else {
                if (uptr1->capac != uptr->capac) {
                    uptr->capac = 0;
                    sim_printf("JEDEC site size precludes use of this device\n");
                } else {
                    uptr->u5 |= RAM;         /* RAM */
                }
            }
            break;
        case UNIT_32KRAM:
            uptr->capac = 0x8000;
            if (uptr->u6 == JEDEC_NUM - 1) {/* top unit ? */
                sim_printf("JEDEC%d cannot be SRAM\n", uptr->u6);
            } else {
                if (uptr1->capac != uptr->capac) {
                    uptr->capac = 0;
                    sim_printf("JEDEC site size precludes use of this device\n");
                } else {
                    uptr->u5 |= RAM;         /* RAM */
                }
            }
            break;
        default:
            if (JEDEC_dev.dctrl & DEBUG_flow)
                sim_printf("\tJEDEC_set_size: Error\n");
            return SCPE_ARG;
    }
    if (JEDEC_buf[uptr->u6]) {              /* any change requires a new buffer */
        free (JEDEC_buf[uptr->u6]);
        JEDEC_buf[uptr->u6] = NULL;
    }
    if (JEDEC_dev.dctrl & DEBUG_flow) {
        sim_printf("\tJEDEC%d->capac=%04XH\n", uptr->u6, uptr->capac);
        sim_printf("\tJEDEC%d->u3[Base addr]=%06XH\n", uptr->u6, uptr->u3);
        sim_printf("\tJEDEC%d->u4[val]=%06XH\n", uptr->u6, uptr->u4);
        sim_printf("\tJEDEC%d->u5[Flags]=%06XH\n", uptr->u6, uptr->u5);
        sim_printf("\tJEDEC%d->u6[unit #]=%06XH\n", uptr->u6, uptr->u6);
        uptr1 = JEDEC_dev.units + JEDEC_NUM - 1; /* top unit holds u5 configuration */
        sim_printf("\tJEDEC%d->u5[Flags]=%06XH\n", JEDEC_NUM - 1, uptr1->u5);
        sim_printf("\tJEDEC_set_size: Done\n");
    }
    return SCPE_OK;
}

/* JEDEC reset */

t_stat JEDEC_reset (DEVICE *dptr)
{
    int32 i, j, c;
    FILE *fp;
    t_stat r;
    UNIT *uptr;
    static int flag = 1;

    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_reset: Entered\n");
    for (i = 0; i < JEDEC_NUM; i++) {       /* handle all umits */
        uptr = JEDEC_dev.units + i;
        if (uptr->capac == 0) {             /* if not configured */
            sim_printf("   JEDEC%d: Not configured\n", i);
            if (flag) {
                sim_printf("      ALL: \"set JEDEC3 None | 8krom | 16krom | 32krom | 8kram | 32kram\"\n");
                sim_printf("      EPROM: \"att JEDEC3 <filename>\"\n");
                flag = 0;
            }
            uptr->capac = 0;
            /* assume 8KB in base address calculation */
            uptr->u3 = 0xF8000 + (0x2000 * i);  /* base address */
            uptr->u4 = 0;                   /* None */
            uptr->u5 = 0;                   /* RO */
            uptr->u6 = i;                   /* unit number - only set here! */
        }
        if (uptr->capac) {                  /* if configured */
            sim_printf("   JEDEC%d: Initializing %2XKB %s [%04X-%04XH]\n", 
                i,
                uptr->capac / 0x400,
                uptr->u5 ? "Ram" : "Rom",
                uptr->u3,
                uptr->u3 + uptr->capac - 1);
            if (JEDEC_buf[uptr->u6] == NULL) {/* no buffer allocated */
                JEDEC_buf[uptr->u6] = malloc(uptr->capac);
                if (JEDEC_buf[uptr->u6] == NULL) {
                    if (JEDEC_dev.dctrl & DEBUG_flow)
                        sim_printf("\tJEDEC_reset: Malloc error\n");
                    return SCPE_MEM;
                }
            }
            if ((uptr->u5 & 0x0001) == 0) { /* ROM - load file */
                fp = fopen(uptr->filename, "rb");
                if (fp == NULL) {
                    sim_printf("\tUnable to open ROM file %s\n", uptr->filename);
                    sim_printf("\tNo ROM image loaded!!!\n");
                } else {
                    j = 0;
                    c = fgetc(fp);
                    while (c != EOF) {
                        *(JEDEC_buf[uptr->u6] + j++) = c & 0xFF;
                        c = fgetc(fp);
                        if (j >= JEDEC_unit[uptr->u6].capac) {
                            sim_printf("\tImage is too large - Load truncated!!!\n");
                            break;
                        }
                    }
                    fclose(fp);
                    sim_printf("\t%d bytes of ROM image %s loaded\n", j, uptr->filename);
                }
            }
        }
    }
    if (JEDEC_dev.dctrl & DEBUG_flow)
        sim_printf("\tJEDEC_reset: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    JEDEC memory read or write is issued.

    Need to fix for hi/low memory operations 
*/

/*  get a byte from memory */

int32 JEDEC_get_mbyte(int32 addr)
{
    int32 i, val, org, len;
    UNIT *uptr;

    if (JEDEC_dev.dctrl & DEBUG_read)
        sim_printf("\tJEDEC_get_mbyte: Entered\n");
    for (i = 0; i < JEDEC_NUM; i++) {       /* test all umits for address */
        uptr = JEDEC_dev.units + i;
        org = uptr->u3;
        len = uptr->capac - 1;
        if ((addr >= org) && (addr <= org + len)) {
            if (JEDEC_dev.dctrl & DEBUG_read)
                sim_printf("\tJEDEC%d Addr=%06XH Org=%06XH Len=%06XH\n", i, addr, org, len);
            val = *(JEDEC_buf[uptr->u6] + (addr - org));
            if (JEDEC_dev.dctrl & DEBUG_read)
                sim_printf("\tJEDEC_get_mbyte: Exit with [%0XH]\n", val & 0xFF);
            return (val & 0xFF);
        }
    }
    if (JEDEC_dev.dctrl & DEBUG_read)
        sim_printf("\tJEDEC_get_mbyte: Exit - Out of range\n", addr);
    return 0xFF;
}

/*  put a byte into memory */

void JEDEC_put_mbyte(int32 addr, int32 val)
{
    int32 i, org, len, type;
    UNIT *uptr;

    if (JEDEC_dev.dctrl & DEBUG_write)
        sim_printf("\tJEDEC_put_mbyte: Entered\n");
    for (i = 0; i < JEDEC_NUM; i++) {       /* test all umits for address */
        uptr = JEDEC_dev.units + i;
        org = uptr->u3;
        len = uptr->capac - 1;
        if ((addr >= org) && (addr < org + len)) {
            if (JEDEC_dev.dctrl & DEBUG_write)
                sim_printf("\tJEDEC%d Org=%06XH Len=%06XH\n", i, org, len);
            if (uptr->u5 & RAM) {           /* can't write to ROM */
                *(JEDEC_buf[uptr->u6] + (addr - org)) = val & 0xFF;
                if (JEDEC_dev.dctrl & DEBUG_write)
                    sim_printf("\tJEDEC_put_mbyte: Exit with [%06XH]=%02XH\n", addr, val);
            } else
                sim_printf("\tJEDEC_put_mbyte: Write to ROM ignored\n");
        }
    }
    if (JEDEC_dev.dctrl & DEBUG_write)
        sim_printf("\tJEDEC_put_mbyte: Exit - Out of range\n");
}

/* end of iJEDEC.c */
