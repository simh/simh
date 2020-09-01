/*  isbc464.c: Intel iSBC 464 32K Byte ROM Card

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

        29 Oct 17 - Original file.

    NOTES:

*/

#include "system_defs.h"

#if defined (SBC464_NUM) && (SBC464_NUM > 0)

#define BASE_ADDR       u3    

/* prototypes */

t_stat isbc464_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc464_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc464_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat isbc464_reset (DEVICE *dptr);
t_stat isbc464_attach (UNIT *uptr, CONST char *cptr);
uint8 isbc464_get_mbyte(uint16 addr);

/* external function prototypes */

/* external globals */

extern uint8 xack;                         /* XACK signal */

/* local globals */

int isbc464_onetime = 1;
    
/* isbc464 Standard I/O Data Structures */

UNIT isbc464_unit = {
    UDATA (NULL, UNIT_ATTABLE+UNIT_BINK+UNIT_ROABLE+UNIT_RO+UNIT_BUFABLE+
        UNIT_MUSTBUF, 0)
};

MTAB isbc464_mod[] = {
    { MTAB_XTD | MTAB_VDV, 0, NULL, "SIZE", &isbc464_set_size,
        NULL, NULL, "Sets the ROM size for iSBC464"               },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "BASE", &isbc464_set_base,
        NULL, NULL, "Sets the ROM base for iSBC464"               },
    { MTAB_XTD|MTAB_VDV, 0, "PARAM", NULL, NULL, &isbc464_show_param, NULL, 
        "Parameter" },
    { 0 }
};

DEBTAB isbc464_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE isbc464_dev = {
    "SBC464",           //name
    &isbc464_unit,      //units
    NULL,               //registers
    isbc464_mod,       //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposite
    &isbc464_reset,     //reset
    NULL,               //boot
    isbc464_attach,     //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DISABLE+DEV_DIS, //flags
    0,                  //dctrl
    isbc464_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* isbc464 globals */

// set size parameter

t_stat isbc464_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        switch (size) {
            case 0x10:                  //16K
                uptr->capac = 16384;
                break;
            case 0x20:                  //32K
                uptr->capac = 32768;
                break;
            case 0x30:                  //48K
                uptr->capac = 49152;
                break;
            case 0x40:                  //64K
                uptr->capac = 65536;
                break;
            default:
                sim_printf("SBC464: Size error\n");
                return SCPE_ARG;     
        }    
        sim_printf("SBC464: Size=%04X\n", uptr->capac);
        return SCPE_OK;
    }   
    return SCPE_ARG;
}

// set base address parameter

t_stat isbc464_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        switch (size) {
            case 0x00:                  //0K
                uptr->BASE_ADDR = 0;
                break;
            case 0x10:                  //16K
                uptr->BASE_ADDR = 16384;
                break;
            case 0x20:                  //32K
                uptr->BASE_ADDR = 32768;
                break;
            case 0x30:                  //48K
                uptr->BASE_ADDR = 49152;
                break;
            default:
                sim_printf("SBC464: Base error\n");
                return SCPE_ARG;     
        }    
        sim_printf("SBC464: Base=%04X\n", uptr->BASE_ADDR);
        return SCPE_OK;
    }   
    return SCPE_ARG;
}

// show configuration parameters

t_stat isbc464_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s Size=%04X  Base=%04X  ", 
        ((isbc464_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        uptr->capac, uptr->BASE_ADDR);
    return SCPE_OK;
}

/* Reset routine */

t_stat isbc464_reset (DEVICE *dptr)
{
    if (isbc464_onetime) {
        isbc464_dev.units->capac = SBC464_SIZE; //set default size
        isbc464_dev.units->BASE_ADDR = SBC464_BASE; //set default base
        isbc464_onetime = 0;
    }
    if (dptr == NULL)
        return SCPE_ARG;
    if ((dptr->flags & DEV_DIS) == 0) { //already enabled
        isbc464_dev.units->filebuf = (uint8 *)calloc(isbc464_dev.units->capac, sizeof(uint8)); //alloc buffer
        if (isbc464_dev.units->filebuf == NULL) { //CALLOC error
            sim_printf ("    sbc464: Calloc error\n");
            return SCPE_MEM;
        }
        sim_printf("    sbc464: Enabled 0%04XH bytes at base 0%04XH\n",
            isbc464_dev.units->capac, isbc464_dev.units->BASE_ADDR);
    } else {
        if (isbc464_dev.units->filebuf)
            free(isbc464_dev.units->filebuf);   //return allocated memory
        sim_printf("    sbc464: Disabled\n");
    }
    return SCPE_OK;
}

/* isbc464 attach  */

t_stat isbc464_attach (UNIT *uptr, CONST char *cptr) 
{
    t_stat r;

    isbc464_reset(NULL);                //odd fix, but it works
    if ((r = attach_unit (uptr, cptr)) != SCPE_OK) {
        sim_printf ("isbc464_attach: Error %d\n", r);
        return r;
    }
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 isbc464_get_mbyte(uint16 addr)
{
    uint8 val;

    val = *((uint8 *)isbc464_unit.filebuf + (addr - isbc464_unit.BASE_ADDR));
    return (val & 0xFF);
}

#endif /* SBC464_NUM > 0 */

/* end of isbc464.c */
