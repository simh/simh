/*  isbc064.c: Intel iSBC064 64K Byte Memory Card

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

        ?? ??? 11 -- Original file.
        16 Dec 12 -- Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support a simulated isbc016, isbc032, isbc048 and isbc064
        memory card on an Intel multibus system.
*/

#include "system_defs.h"

#if defined (SBC064_NUM) && (SBC064_NUM > 0)    //if board allowed with this system

#define BASE_ADDR       u3    

/* prototypes */

t_stat isbc064_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc064_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat isbc064_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat isbc064_reset(DEVICE *dptr);
uint8 isbc064_get_mbyte(uint16 addr);
void isbc064_put_mbyte(uint16 addr, uint8 val);

/* external function prototypes */

/* local globals */

int isbc064_onetime = 1;

/* external globals */

extern uint8 xack;

/* isbc064 Standard SIMH Device Data Structures */

UNIT isbc064_unit = {
    UDATA (NULL, 0, 0)
};

MTAB isbc064_mod[] = {
    { MTAB_XTD | MTAB_VDV,              /* mask */
    0,                                  /* match */
    NULL,                               /* print string */
    "SIZE",                             /* match string */
    &isbc064_set_size,                  /* validation routine */
    NULL,                               /* display routine */
    NULL,                               /* location descriptor */
    "Sets the RAM size for iSB 064"     /* help string */
},
    { MTAB_XTD | MTAB_VDV,              /* mask */ 
    0                                   /* match */, 
    NULL,                               /* print string */ 
    "BASE",                             /* match string */ 
    &isbc064_set_base,                  /* validation routine */
    NULL,                               /* display routine */
    NULL,                               /* location descriptor */
    "Sets the RAM base for iSBC 064"    /* help string */
},
    { MTAB_XTD|MTAB_VDV,                /* mask */ 
    0,                                  /* match */
    "PARAM",                            /* print string */ 
    NULL,                               /* match string */ 
    NULL,                               /* validation routine */ 
    &isbc064_show_param,                /* display routine */
    NULL,                               /* location descriptor */ 
    "Show current Parameters for iSBC 064" /* help string */
},
    { 0 }
};

DEBTAB isbc064_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE isbc064_dev = {
    "SBC064",           //name
    &isbc064_unit,      //units
    NULL,               //registers
    isbc064_mod,        //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                 //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
    &isbc064_reset,     //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    DEV_DISABLE+DEV_DIS, //flags
    0,                  //dctrl
    isbc064_debug,      //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    NULL                //device description
};

/* Service routines to handle simulator functions */


// set size parameter

t_stat isbc064_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        if (size & 0xff8f) {
            sim_printf("SBC064: Size error\n");
            return SCPE_ARG;     
        } else {
            isbc064_unit.capac = (size * 1024) - 1;
            sim_printf("SBC064: Size=%04XH\n", isbc064_unit.capac);
            return SCPE_OK;
        }
    }   
    return SCPE_ARG;
}

// set base address parameter

t_stat isbc064_set_base(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 size, result, i;
    
    if (cptr == NULL)
        return SCPE_ARG;
    result = sscanf(cptr, "%i%n", &size, &i);
    if ((result == 1) && (cptr[i] == 'K') && ((cptr[i + 1] == 0) ||
        ((cptr[i + 1] == 'B') && (cptr[i + 2] == 0)))) {
        if (size & 0xff8f) {
            sim_printf("SBC064: Base error\n");
            return SCPE_ARG;     
        } else {
            isbc064_unit.BASE_ADDR = size * 1024;
            sim_printf("SBC064: Base=%04XH\n", isbc064_unit.BASE_ADDR);
            return SCPE_OK;
        }
    }   
    return SCPE_ARG;
}

// show configuration parameters

t_stat isbc064_show_param (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s Base=%04XH  Size=%04XH  ", 
        ((isbc064_dev.flags & DEV_DIS) == 0) ? "Enabled" : "Disabled", 
        isbc064_unit.BASE_ADDR, isbc064_unit.capac);
    return SCPE_OK;
}

/* Reset routine */

t_stat isbc064_reset (DEVICE *dptr)
{
    if (dptr == NULL)
        return SCPE_ARG;
    if (isbc064_onetime) {
        isbc064_dev.units->capac = SBC064_SIZE; //set default size
        isbc064_dev.units->BASE_ADDR = SBC064_BASE; //set default base
        isbc064_onetime = 0;
    }
    if ((dptr->flags & DEV_DIS) == 0) { //already enabled
        isbc064_dev.units->filebuf = (uint8 *)calloc(isbc064_unit.capac, sizeof(uint8)); //alloc buffer
        if (isbc064_dev.units->filebuf == NULL) { //CALLOC error
            sim_printf ("    sbc064: Calloc error\n");
            return SCPE_MEM;
        }
        sim_printf("    sbc064: Enabled 0%04XH bytes at base 0%04XH\n",
            isbc064_dev.units->capac, isbc064_dev.units->BASE_ADDR);
    } else {
        if (isbc064_dev.units->filebuf)
            free(isbc064_dev.units->filebuf);   //return allocated memory
        sim_printf("    sbc064: Disabled\n");
    }
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 isbc064_get_mbyte(uint16 addr)
{
    uint8 val;

    val = *((uint8 *)isbc064_unit.filebuf + (addr - isbc064_unit.BASE_ADDR));
    return (val & 0xFF);
}

/*  put a byte into memory */

void isbc064_put_mbyte(uint16 addr, uint8 val)
{
    *((uint8 *)isbc064_unit.filebuf + (addr - isbc064_unit.BASE_ADDR)) = val & 0xFF;
    return;
}

#endif /* SBC064_NUM > 0 */

/* end of isbc064.c */
