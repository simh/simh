/*  multibus.c: Multibus I simulator

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
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

*/

#include "system_defs.h"

#define multibus_NAME   "Intel Multibus Interface"

/* function prototypes */

t_stat multibus_svc(UNIT *uptr);
t_stat multibus_reset(DEVICE *dptr);
uint8 multibus_get_mbyte(uint16 addr);
void multibus_put_mbyte(uint16 addr, uint8 val);

/* external function prototypes */

//extern t_stat SBC_reset(DEVICE *dptr);  /* reset the iSBC80/10 emulator */
extern uint8 isbc064_get_mbyte(uint16 addr);
extern void isbc064_put_mbyte(uint16 addr, uint8 val);
extern uint8 isbc464_get_mbyte(uint16 addr);

/* local globals */

static const char* multibus_desc(DEVICE *dptr) {
    return multibus_NAME;
}

/* external globals */

extern uint8 xack;                      /* XACK signal */
extern int32 int_req;                   /* i8080 INT signal */
extern uint16 PCX;
extern DEVICE isbc064_dev;
extern DEVICE isbc464_dev;

/* multibus Standard SIMH Device Data Structures */

UNIT multibus_unit = { 
    UDATA (&multibus_svc, 0, 0), 1
};

REG multibus_reg[] = { 
    { HRDATA (XACK, xack, 8) },
    { NULL }
};

DEBTAB multibus_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { NULL }
};

DEVICE multibus_dev = {
    "MBI",              //name 
    &multibus_unit,     //units 
    multibus_reg,       //registers 
    NULL,               //modifiers
    1,                  //numunits 
    16,                 //aradix  
    16,                 //awidth  
    1,                  //aincr  
    16,                 //dradix  
    8,                  //dwidth
    NULL,               //examine  
    NULL,               //deposit  
    &multibus_reset,    //reset 
    NULL,               //boot
    NULL,               //attach  
    NULL,               //detach
    NULL,               //ctxt     
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    multibus_debug,     //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &multibus_desc      //device description
};

/* Service routines to handle simulator functions */

/* Reset routine */

t_stat multibus_reset(DEVICE *dptr)
{
//    if (SBC_reset(NULL) == 0) { 
//        sim_printf("  Multibus: Reset\n");
        sim_activate (&multibus_unit, multibus_unit.wait); /* activate unit */
        return SCPE_OK;
//    } else {
//        sim_printf("   Multibus: SBC not selected\n");
//        return SCPE_OK;
//    }
}

/* service routine - actually does the simulated interrupts */

t_stat multibus_svc(UNIT *uptr)
{
    sim_activate (&multibus_unit, multibus_unit.wait); /* continue poll */
    return SCPE_OK;
}

/*  get a byte from memory */

uint8 multibus_get_mbyte(uint16 addr)
{
    SET_XACK(0);                        /* set no XACK */
    if ((isbc464_dev.flags & DEV_DIS) == 0) { //ROM is enabled
        if (addr >= isbc464_dev.units->u3 && 
        addr < (isbc464_dev.units->u3 + isbc464_dev.units->capac)) {
            SET_XACK(1);            //set xack
            return(isbc464_get_mbyte(addr));
        }
    }
    if ((isbc064_dev.flags & DEV_DIS) == 0) { //iSBC 064 is enabled
        if (addr >= isbc064_dev.units->u3 && 
        addr < (isbc064_dev.units->u3 + isbc064_dev.units->capac)) {
            SET_XACK(1);            //set xack
            return (isbc064_get_mbyte(addr));
        }
    }
    return 0;
}

void multibus_put_mbyte(uint16 addr, uint8 val)
{
    SET_XACK(0);                        /* set no XACK */
    if ((isbc064_dev.flags & DEV_DIS) == 0) { //device is enabled
        if (addr >= isbc064_dev.units->u3 && 
        addr < (isbc064_dev.units->u3 + isbc064_dev.units->capac)) {
            SET_XACK(1);            //set xack
            isbc064_put_mbyte(addr, val);
        }
    }
}

/* end of multibus.c */
