/*  iRAM8.c: Intel RAM simulator for 8-bit SBCs

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

    MODIFICATIONS:

        ?? ??? 11 - Original file.

    NOTES:

        These functions support a simulated RAM devices on an iSBC-80/XX SBCs.
        These functions also support bit 2 of 8255 number 1, port B, to enable/
        disable the onboard RAM.
*/

#include "system_defs.h"

#define BASE_ADDR       u3    
#define iRAM_NAME    "Intel RAM Chip"

/* function prototypes */

t_stat RAM_cfg(uint16 base, uint16 size, uint8 dummy);
t_stat RAM_clr(void);
t_stat RAM_reset (DEVICE *dptr);
t_stat RAM_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat RAM_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat RAM_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
uint8 RAM_get_mbyte(uint16 addr);
void RAM_put_mbyte(uint16 addr, uint8 val);

/* external function prototypes */

/* external globals */

// globals

static const char* iRAM_desc(DEVICE *dptr) {
    return iRAM_NAME;
}

/* SIMH RAM Standard I/O Data Structures */

UNIT RAM_unit = { UDATA (NULL, UNIT_BINK, 0) };

MTAB RAM_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, NULL, "BASE", &RAM_set_base,
        NULL, NULL, "Sets the base address for RAM"},
    { MTAB_XTD | MTAB_VDV, 0, NULL, "SIZE", &RAM_set_size,
        NULL, NULL, "Sets the size for RAM"},
    { MTAB_XTD | MTAB_VDV, 0, "PARAM", NULL, NULL, RAM_show_param, NULL, 
        "show configured parametes for RAM" },
    { 0 }
};

DEBTAB RAM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE RAM_dev = {
    "RAM",              //name
    &RAM_unit,          //units
    NULL,               //registers
    RAM_mod,            //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    RAM_reset,          //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags 
    0,                  //dctrl
    RAM_debug,          //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &iRAM_desc          //device description
};

/* RAM functions */

// RAM configuration

t_stat RAM_cfg(uint16 base, uint16 size, uint8 dummy)
{
    RAM_unit.capac = size;              /* set RAM size */
    RAM_unit.u3 = base;                 /* set RAM base */
    RAM_unit.filebuf = (uint8 *)calloc(size, sizeof(uint8));
    if (RAM_unit.filebuf == NULL) {
        sim_printf ("    RAM: Calloc error\n");
        return SCPE_MEM;
    }
    sim_printf("    RAM: 0%04XH bytes at base address 0%04XH\n",
        size, base);
    return SCPE_OK;
}

t_stat RAM_clr(void)
{
    RAM_unit.capac = 0;
    RAM_unit.u3 = 0;
    free(RAM_unit.filebuf);
    return SCPE_OK;
}

/* RAM reset */

t_stat RAM_reset (DEVICE *dptr)
{
    return SCPE_OK;
}

// set size parameter

t_stat RAM_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        if (size & 0xff8f) {
            sim_printf("RAM: Size error\n");
            return SCPE_ARG;     
        } else {
            RAM_unit.capac = (size * 1024) - 1;
            sim_printf("RAM: Size=%04XH\n", RAM_unit.capac);
            return SCPE_OK;
        }
    }   
    return SCPE_ARG;
}

// set base address parameter

t_stat RAM_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        if (size & 0xff8f) {
            sim_printf("RAM: Base error\n");
            return SCPE_ARG;     
        } else {
            RAM_unit.BASE_ADDR = size * 1024;
            sim_printf("RAM: Base=%04XH\n", RAM_unit.BASE_ADDR);
            return SCPE_OK;
        }
    }   
    return SCPE_ARG;
}

// show configuration parameters

t_stat RAM_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_ARG;
    fprintf(st, "%s at Base Address 0%04XH (%dD) for 0%04XH (%dD) Bytes ", 
        ((RAM_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        RAM_unit.u3, RAM_unit.u3, RAM_unit.capac, RAM_unit.capac);
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 RAM_get_mbyte(uint16 addr)
{
    uint8 val;

    val = *((uint8 *)RAM_unit.filebuf + (addr - RAM_unit.u3));
    return (val & 0xFF);
}

/*  put a byte into memory */

void RAM_put_mbyte(uint16 addr, uint8 val)
{
    *((uint8 *)RAM_unit.filebuf + (addr - RAM_unit.u3)) = val & 0xFF;
    return;
}

/* end of iRAM8.c */
