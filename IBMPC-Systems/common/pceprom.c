/*  pceprom.c: Intel EPROM simulator for 8-bit SBCs

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

        11 Jul 16 - Original file.

    NOTES:

        These functions support a simulated ROM devices on aPC XT SBC.
        This allows the attachment of the device to a binary file containing the EPROM
        code image.  Unit will support a single 2764, 27128, 27256, or 27512 type EPROM.
*/

#include "system_defs.h"

/* function prototypes */

t_stat EPROM_attach (UNIT *uptr, CONST char *cptr);
t_stat EPROM_reset (DEVICE *dptr, uint32 base, uint32 size);
uint8 EPROM_get_mbyte(uint32 addr);

/* external function prototypes */

/* SIMH EPROM Standard I/O Data Structures */

UNIT EPROM_unit = {
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO, 0), 0
};

DEBTAB EPROM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE EPROM_dev = {
    "EPROM",            //name
    &EPROM_unit,        //units
    NULL,               //registers
    NULL,               //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &EPROM_reset,       //reset
    NULL,				//reset
    NULL,               //boot
    &EPROM_attach,      //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG,          //flags
    0,                  //dctrl
    EPROM_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* global variables */

/* EPROM functions */

/* EPROM attach  */

t_stat EPROM_attach (UNIT *uptr, CONST char *cptr)
{
    uint16 j;
    int c;
    FILE *fp;
    t_stat r;

    sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: cptr=%s\n", cptr);
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: Error\n");
        return r;
    }
    sim_debug (DEBUG_read, &EPROM_dev, "\tAllocate buffer\n");
    if (EPROM_unit.filebuf == NULL) {   /* no buffer allocated */
        EPROM_unit.filebuf = malloc(EPROM_unit.capac); /* allocate EPROM buffer */
        if (EPROM_unit.filebuf == NULL) {
            sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: Malloc error\n");
            return SCPE_MEM;
        }
    }
    sim_debug (DEBUG_read, &EPROM_dev, "\tOpen file %s\n", EPROM_unit.filename);
    fp = fopen(EPROM_unit.filename, "rb"); /* open EPROM file */
    if (fp == NULL) {
        sim_printf("EPROM: Unable to open ROM file %s\n", EPROM_unit.filename);
        sim_printf("\tNo ROM image loaded!!!\n");
        return SCPE_OK;
    }
    sim_debug (DEBUG_read, &EPROM_dev, "\tRead file\n");
    j = 0;                              /* load EPROM file */
    c = fgetc(fp);
    while (c != EOF) {
        *((uint8 *)EPROM_unit.filebuf + j++) = c & 0xFF;
        c = fgetc(fp);
        if (j > EPROM_unit.capac) {
            sim_printf("\tImage is too large - Load truncated!!!\n");
            break;
        }
    }
    sim_printf("\tImage size=%05X unit_capac=%05X\n", j, EPROM_unit.capac);
    sim_debug (DEBUG_read, &EPROM_dev, "\tClose file\n");
    fclose(fp);
    sim_printf("EPROM: %d bytes of ROM image %s loaded\n", j, EPROM_unit.filename);
    sim_debug (DEBUG_flow, &EPROM_dev, "EPROM_attach: Done\n");
    return SCPE_OK;
}

/* EPROM reset */

t_stat EPROM_reset (DEVICE *dptr, uint32 base, uint32 size)
{
    sim_debug (DEBUG_flow, &EPROM_dev, "   EPROM_reset: base=%05X size=%05X\n", base, size);
    if ((EPROM_unit.flags & UNIT_ATT) == 0) { /* if unattached */
        EPROM_unit.capac = size;        /* set EPROM size */
        EPROM_unit.u3 = base;           /* set EPROM base addr */
        sim_debug (DEBUG_flow, &EPROM_dev, "Done1\n");
        sim_printf("   EPROM: Available [%05X-%05XH]\n", 
            base, size);
        return SCPE_OK;
    } else
        sim_printf("EPROM: No file attached\n");
    sim_debug (DEBUG_flow, &EPROM_dev, "Done2\n");
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 EPROM_get_mbyte(uint32 addr)
{
    uint8 val;
    uint32 romoff;

    romoff = addr - EPROM_unit.u3;
    sim_debug (DEBUG_read, &EPROM_dev, "EPROM_get_mbyte: addr=%05X romoff=%05X\n", addr, romoff);
    if (romoff < EPROM_unit.capac) {
        val = *((uint8 *)EPROM_unit.filebuf + romoff);
        sim_debug (DEBUG_read, &EPROM_dev, " val=%02X\n", val);
        return (val & 0xFF);
    }
    sim_debug (DEBUG_read, &EPROM_dev, " Out of range\n");
    return 0xFF;
}

/* end of pceprom.c */
