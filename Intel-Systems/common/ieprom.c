/*  iEPROM.c: Intel EPROM simulator for 8-bit SBCs

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

        ?? ??? 10 - Original file.

    NOTES:

        These functions support a simulated ROM devices on an iSBC-80/XX SBCs.
        This allows the attachment of the device to a binary file containing the EPROM
        code image.  Unit will support a single 2708, 2716, 2732, or 2764 type EPROM.
        These functions also support bit 0x80 of 8255 number 0, port C, to enable/
        disable the onboard ROM.
*/

#include "system_defs.h"

#define EPROM_NAME    "Intel EPROM Chip"

/* function prototypes */

t_stat EPROM_cfg (uint16 base, uint16 size, uint8 devnum);
t_stat EPROM_clr(void);
t_stat EPROM_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat EPROM_attach (UNIT *uptr, CONST char *cptr);
t_stat EPROM_reset (DEVICE *dptr);
uint8 EPROM_get_mbyte (uint16 addr, uint8 devnum);

/* external function prototypes */

/* external globals */

/* globals */

static const char* EPROM_desc(DEVICE *dptr) {
    return EPROM_NAME;
}
int ieprom_num = 0;

/* SIMH EPROM Standard I/O Data Structures */

UNIT EPROM_unit[] = {
    {UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+UNIT_BUFABLE+UNIT_MUSTBUF, 0) },
    {UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+UNIT_BUFABLE+UNIT_MUSTBUF, 0) }
};

MTAB EPROM_mod[] = {
//    { MTAB_XTD | MTAB_VDV, 0, NULL, "SIZE", &isbc464_set_size,
//        NULL, NULL, "Sets the ROM size for EPROM"               },
//    { MTAB_XTD | MTAB_VDV, 0, NULL, "BASE", &isbc464_set_base,
//        NULL, NULL, "Sets the ROM base for EPROM"               },
    { MTAB_XTD|MTAB_VDV, 0, "PARAM", NULL, NULL, &EPROM_show_param, NULL, 
        "Parameters" },
    { 0 }
};

DEBTAB EPROM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE EPROM_dev = {
    "EPROM",            //name
    EPROM_unit,         //units
    NULL,               //registers
    EPROM_mod,          //modifiers
    EPROM_NUM,          //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    &EPROM_reset,       //reset
    NULL,               //boot
    &EPROM_attach,      //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl
    EPROM_debug,        //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &EPROM_desc         //device description
};

/* EPROM functions */

// EPROM configuration

t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum)
{
    EPROM_unit[ieprom_num].capac = size;        /* set EPROM size */
    EPROM_unit[ieprom_num].u3 = base;           /* set EPROM base */
    EPROM_unit[ieprom_num].filebuf = (uint8 *)calloc(size, sizeof(uint8));
    if (EPROM_unit[ieprom_num].filebuf == NULL) {
        sim_printf ("    EPROM%d: Calloc error\n", ieprom_num);
        return SCPE_MEM;
    }
    sim_printf("    EPROM%d: 0%04XH bytes at base address 0%04XH\n",
        ieprom_num, EPROM_unit[ieprom_num].capac, EPROM_unit[ieprom_num].u3);
    ieprom_num++;
    return SCPE_OK;
}

t_stat EPROM_clr(void)
{
    int i;
    
    for(i=0; i<ieprom_num; i++) {
        EPROM_unit[i].capac = 0;
        EPROM_unit[i].u3 = 0;
        free(EPROM_unit[i].filebuf);
    }
    ieprom_num = 0;
    return SCPE_OK;
}

/* EPROM reset */

t_stat EPROM_reset (DEVICE *dptr)
{
    return SCPE_OK;
}

// show configuration parameters

t_stat EPROM_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int i;
    
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "Device %s\n", ((EPROM_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled");
    for (i=0; i<ieprom_num; i++) {
        fprintf(st, "Unit %d at Base Address 0%04XH (%dD) for 0%04XH (%dD) Bytes ", 
            i,
            EPROM_unit[i].u3, EPROM_unit[i].u3, EPROM_unit[i].capac, EPROM_unit[i].capac);
    }
    return SCPE_OK;
}

/* EPROM attach  */

t_stat EPROM_attach (UNIT *uptr, CONST char *cptr) 
{
    t_stat r;

    if (uptr == NULL)
        return SCPE_ARG;
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_printf ("EPROM_attach: Error %d\n", r);
        return r;
    }
    return SCPE_OK;
}

/*  get a byte from memory */ 

uint8 EPROM_get_mbyte(uint16 addr, uint8 devnum)
{
    uint8 val;

    val = *((uint8 *)EPROM_unit[devnum].filebuf + (addr - EPROM_unit[devnum].u3));
    return (val & 0xFF);
}

/* end of iEPROM.c */
