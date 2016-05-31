/*  bootrom.c: Boot ROM simulator for Motorola processors

    Copyright (c) 2010-2012, William A. Beech

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

        23 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a single simulated 2704 to 2764 EPROM device on 
        an 8-bit computer system..  This device allows the buffer to be loaded from
        a binary file containing the emulated EPROM code.

        These functions support a simulated 2704, 2708, 2716, 2732 or 2764 EPROM 
        device on a CPU board.  The byte get and put routines use an offset into 
        the boot EPROM image to locate the proper byte.  This allows another device 
        to set the base address for the boot EPROM.  The device type is stored as
        a binary number in the first three unit flag bits.

        This device uses a dynamically allocated buffer to hold the EPROM image.  
        A call to BOOTROM_config will free the current buffer.  A call to 
        BOOTROM_reset will allocate a new buffer of BOOTROM_unit.capac bytes.  A
        call to BOOTROM_attach will load the buffer with the EPROM image.

*/

#include <stdio.h>
#include "swtp_defs.h"


#if !defined(DONT_USE_INTERNAL_ROM)
#include "swtp_swtbug_bin.h"
#endif /* DONT_USE_INTERNAL_ROM */

#define UNIT_V_MSIZE    (UNIT_V_UF)     /* ROM Size */
#define UNIT_MSIZE      (0x7 << UNIT_V_MSIZE)
#define UNIT_NONE       (0 << UNIT_V_MSIZE) /* No EPROM */
#define UNIT_2704       (1 << UNIT_V_MSIZE) /* 2704 mode */
#define UNIT_2708       (2 << UNIT_V_MSIZE) /* 2708 mode */
#define UNIT_2716       (3 << UNIT_V_MSIZE) /* 2716 mode */
#define UNIT_2732       (4 << UNIT_V_MSIZE) /* 2732 mode */
#define UNIT_2764       (5 << UNIT_V_MSIZE) /* 2764 mode */

/* function prototypes */

t_stat BOOTROM_svc (UNIT *uptr);
t_stat BOOTROM_config (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat BOOTROM_attach (UNIT *uptr, CONST char *cptr);
t_stat BOOTROM_reset (DEVICE *dptr);
int32 BOOTROM_get_mbyte(int32 offset);

/* SIMH Standard I/O Data Structures */

UNIT BOOTROM_unit = {
#if defined(DONT_USE_INTERNAL_ROM)
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0),
#else /* !defined(DONT_USE_INTERNAL_ROM) */
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+((BOOT_CODE_SIZE>>9)<<UNIT_V_MSIZE), BOOT_CODE_SIZE),
#endif
    KBD_POLL_WAIT };

MTAB BOOTROM_mod[] = {
    { UNIT_MSIZE, UNIT_NONE, "None", "NONE", &BOOTROM_config },
    { UNIT_MSIZE, UNIT_2704, "2704", "2704", &BOOTROM_config },
    { UNIT_MSIZE, UNIT_2708, "2708", "2708", &BOOTROM_config },
    { UNIT_MSIZE, UNIT_2716, "2716", "2716", &BOOTROM_config },
    { UNIT_MSIZE, UNIT_2732, "2732", "2732", &BOOTROM_config },
    { UNIT_MSIZE, UNIT_2764, "2764", "2764", &BOOTROM_config },
    { 0 }
};

DEBTAB BOOTROM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE BOOTROM_dev = {
    "BOOTROM",                      /* name */
    &BOOTROM_unit,                  /* units */
    NULL,                           /* registers */
    BOOTROM_mod,                    /* modifiers */
    1,                              /* numunits */
    16,                             /* aradix */
    32,                             /* awidth */
    1,                              /* aincr */
    16,                             /* dradix */
    8,                              /* dwidth */
    NULL,                           /* examine */
    NULL,                           /* deposit */
    &BOOTROM_reset,                 /* reset */
    NULL,                           /* boot */
    &BOOTROM_attach,                /* attach */
    NULL,                           /* detach */
    NULL,                           /* ctxt */
    DEV_DEBUG,                      /* flags */
    0,                              /* dctrl */
    BOOTROM_debug,                  /* debflags */
    NULL,                           /* msize */
    NULL                            /* lname */
};

/* global variables */

/* BOOTROM_attach - attach file to EPROM unit */

t_stat BOOTROM_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    t_addr image_size, capac;
    int i;

    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_attach: cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_attach: Error\n");
        return r;
    }
    image_size = (t_addr)sim_fsize_ex (BOOTROM_unit.fileref);
    for (capac = 0x200, i=1; capac < image_size; capac <<= 1, i++);
    if (i > (UNIT_2764>>UNIT_V_MSIZE)) {
        detach_unit (uptr);
        return SCPE_ARG;
    }
    uptr->flags &= ~UNIT_MSIZE;
    uptr->flags |= (i << UNIT_V_MSIZE);
    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_attach: Done\n");
    return (BOOTROM_reset (NULL));
}

/* BOOTROM_config = None, 2704, 2708, 2716, 2732 or 2764 */

t_stat BOOTROM_config (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_config: val=%d\n", val);
    if ((val < UNIT_NONE) || (val > UNIT_2764)) { /* valid param? */
        sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_config: Parameter error\n");
        return SCPE_ARG;
    }
    if (val == UNIT_NONE)
        BOOTROM_unit.capac = 0;         /* set EPROM size */
    else
        BOOTROM_unit.capac = 0x200 << ((val >> UNIT_V_MSIZE) - 1); /* set EPROM size */
    if (BOOTROM_unit.filebuf) {         /* free buffer */
        free (BOOTROM_unit.filebuf);
        BOOTROM_unit.filebuf = NULL;
    }
    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_config: BOOTROM_unit.capac=%d\n",
            BOOTROM_unit.capac);
    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_config: Done\n");
    return SCPE_OK;
}

/* EPROM reset */

t_stat BOOTROM_reset (DEVICE *dptr)
{
    t_addr j;
    int c;
    FILE *fp;

    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_reset: \n");
    if ((BOOTROM_unit.flags & UNIT_MSIZE) == 0) { /* if none selected */
//        printf("   EPROM: Defaulted to None\n");
//        printf("      \"set eprom NONE | 2704 | 2708 | 2716 | 2732 | 2764\"\n");
//        printf("      \"att eprom <filename>\"\n");
        BOOTROM_unit.capac = 0;         /* set EPROM size to 0 */
        sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_reset: Done1\n");
        return SCPE_OK;
        }                               /* if attached */
//    printf("   EPROM: Initializing [%04X-%04XH]\n", 
//        0xE000, 0xE000 + BOOTROM_unit.capac - 1);
    if (BOOTROM_unit.filebuf == NULL) { /* no buffer allocated */
        BOOTROM_unit.filebuf = calloc(1, BOOTROM_unit.capac); /* allocate EPROM buffer */
        if (BOOTROM_unit.filebuf == NULL) {
            sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_reset: Malloc error\n");
            return SCPE_MEM;
        }
    }
#if !defined(DONT_USE_INTERNAL_ROM)
    if (!BOOTROM_unit.filename) {
        if (BOOTROM_unit.capac < BOOT_CODE_SIZE)
            return SCPE_ARG;
        memcpy (BOOTROM_unit.filebuf, BOOT_CODE_ARRAY, BOOT_CODE_SIZE);
        return SCPE_OK;
        }
#endif
    fp = fopen(BOOTROM_unit.filename, "rb"); /* open EPROM file */
    if (fp == NULL) {
        printf("\tUnable to open ROM file %s\n",BOOTROM_unit.filename);
        printf("\tNo ROM image loaded!!!\n");
        return SCPE_OK;
    }
    j = 0;                              /* load EPROM file */
    c = fgetc(fp);
    while (c != EOF) {
        *((uint8 *)(BOOTROM_unit.filebuf) + j++) = c & 0xFF;
        c = fgetc(fp);
        if (j > BOOTROM_unit.capac) {
            printf("\tImage is too large - Load truncated!!!\n");
            break;
        }
    }
    fclose(fp);
//    printf("\t%d bytes of ROM image %s loaded\n", j, BOOTROM_unit.filename);
    sim_debug (DEBUG_flow, &BOOTROM_dev, "BOOTROM_reset: Done2\n");
    return SCPE_OK;
}

/*  get a byte from memory - byte offset of image */

int32 BOOTROM_get_mbyte(int32 offset)
{
    int32 val;

    if (BOOTROM_unit.filebuf == NULL) {
        sim_debug (DEBUG_read, &BOOTROM_dev, "BOOTROM_get_mbyte: EPROM not configured\n");
        return 0xFF;
    }
    sim_debug (DEBUG_read, &BOOTROM_dev, "BOOTROM_get_mbyte: offset=%04X\n", offset);
    if ((t_addr)offset > BOOTROM_unit.capac) {
        sim_debug (DEBUG_read, &BOOTROM_dev, "BOOTROM_get_mbyte: EPROM reference beyond ROM size\n");
        return 0xFF;
    }
    val = *((uint8 *)(BOOTROM_unit.filebuf) + offset) & 0xFF;
    sim_debug (DEBUG_read, &BOOTROM_dev, "BOOTROM_get_mbyte: Normal val=%02X\n", val);
    return val;
}

/* end of bootrom.c */
