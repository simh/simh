/*  i2716.c: Intel 2716 EPROM simulator for 8-bit processors

    Copyright (c) 2011-2012, William A. Beech

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

    NOTES:

        These functions support a simulated 2704 to 2764 EPROMs device on an 8-bit 
        computer system.  This device allows the attachment of the device to a binary file
        containing the EPROM code.

        These functions support emulation of 0 to 4 2716 EPROM  devices on a CPU board.
        The byte get and put routines use an offset into the boot EPROM image to locate 
        the proper byte.  This allows another device to set the base address for each 
        EPROM.  

        This device uses a dynamically allocated buffer to hold each EPROM image.  
        A call to BOOTROM_config will free the current buffer.  A call to 
        i2716_reset will allocate a new buffer of 2048 bytes.  A
        call to BOOTROM_attach will load the buffer with the EPROM image.
*/

#include <stdio.h>
#include "swtp_defs.h"

#define I2716_NUM       4               /* number of 2716 EPROMS */

extern  int32 get_base(void);

/* function prototypes */

t_stat i2716_attach (UNIT *uptr, CONST char *cptr);
t_stat i2716_reset (DEVICE *dptr);
int32 i2716_get_mbyte(int32 offset);

/* SIMH EPROM Standard I/O Data Structures */

UNIT i2716_unit[] = { 
    { UDATA (NULL,UNIT_ATTABLE+UNIT_ROABLE+UNIT_RO, 0), 0 },
    { UDATA (NULL,UNIT_ATTABLE+UNIT_ROABLE+UNIT_RO, 0), 0 },
    { UDATA (NULL,UNIT_ATTABLE+UNIT_ROABLE+UNIT_RO, 0), 0 },
    { UDATA (NULL,UNIT_ATTABLE+UNIT_ROABLE+UNIT_RO, 0), 0 }
};

MTAB i2716_mod[] = {
    { 0 }
};

DEBTAB i2716_debug[] = {
    { "ALL", DEBUG_all, "All debug bits" },
    { "FLOW", DEBUG_flow, "Flow control" },
    { "READ", DEBUG_read, "Read Command" },
    { "WRITE", DEBUG_write, "Write Command"},
    { NULL }
};

DEVICE i2716_dev = {
    "I2716",                        /* name */
    i2716_unit,                     /* units */
    NULL,                           /* registers */
    i2716_mod,                      /* modifiers */
    I2716_NUM,                      /* numunits */
    16,                             /* aradix */
    32,                             /* awidth */
    1,                              /* aincr */
    16,                             /* dradix */
    8,                              /* dwidth */
    NULL,                           /* examine */
    NULL,                           /* deposit */
    &i2716_reset,                   /* reset */
    NULL,                           /* boot */
    &i2716_attach,                  /* attach */
    NULL,                           /* detach */
    NULL,                           /* ctxt */
    DEV_DEBUG,                      /* flags */
    0,                              /* dctrl */
    i2716_debug,                    /* debflags */
    NULL,                           /* msize */
    NULL                            /* lname */
};

/* global variables */

/* i2716_attach - attach file to EPROM unit 
                  force EPROM reset at completion */

t_stat i2716_attach (UNIT *uptr, CONST char *cptr)
{
    int32 j, c;
    t_stat r;
    FILE *fp;

    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        return r;
    }
    fp = fopen(uptr->filename, "rb");   /* open EPROM file */
    if (fp == NULL) {
        printf("i2716%d: Unable to open ROM file %s\n", (int)(uptr - i2716_dev.units), uptr->filename);
        printf("\tNo ROM image loaded!!!\n");
        return SCPE_OK;
    }
    j = 0;                              /* load EPROM file */
    c = fgetc(fp);
    while (c != EOF) {
        *((uint8 *)(uptr->filebuf) + j++) = c & 0xFF;
        c = fgetc(fp);
        if (j > 2048) {
            printf("\tImage is too large - Load truncated!!!\n");
            break;
        }
    }
    fclose(fp);
    return SCPE_OK;
}

/* EPROM reset */

t_stat i2716_reset (DEVICE *dptr)
{
    int32 i, base;
    UNIT *uptr;

    for (i = 0; i < I2716_NUM; i++) {   /* init all units */
        uptr = i2716_dev.units + i;
        uptr->capac = 2048;
        uptr->u3 = 2048 * i;
        base = get_base();
        if (uptr->filebuf == NULL) {    /* no buffer allocated */
            uptr->filebuf = calloc(2048, sizeof(uint8)); /* allocate EPROM buffer */
            if (uptr->filebuf == NULL) {
                return SCPE_MEM;
            }
        }
        if (base == 0) {
            continue;
        }
    }
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    EPROM memory read or write is issued.
*/

/*  get a byte from memory */

int32 i2716_get_mbyte(int32 offset)
{
    int32 i, val, org, len;
    UNIT *uptr;

    for (i = 0; i < I2716_NUM; i++) {   /* find addressed unit */
        uptr = i2716_dev.units + i;
        org = uptr->u3;
        len = uptr->capac - 1;
        if ((offset >= org) && (offset < (org + len))) {
            if (uptr->filebuf == NULL) {
                return 0xFF;
            } else {
                val = *((uint8 *)(uptr->filebuf) + (offset - org));
                return (val & 0xFF);
            }
        }
    }
    return 0xFF;
}

/* end of i2716.c */
