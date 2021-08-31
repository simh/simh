/*  irq.c: Intel Interrupt simulator

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

        20 Sep 20 - Original file.

    NOTES:

*/

#include "system_defs.h"

#define irq_NAME   "Intel Interrupt Simulator"

/* function prototypes */

t_stat irq_svc(UNIT *uptr);
t_stat irq_reset(DEVICE *dptr);
void set_irq(int32 irq_num);
void clr_irq(int32 irq_num);

/* external function prototypes */

//extern t_stat SBC_reset(DEVICE *dptr);  /* reset the iSBC80/10 emulator */
extern void set_cpuint(int32 irq_num);

/* local globals */

int32   mbirq = 0;                      /* set no multibus interrupts */
static const char* irq_desc(DEVICE *dptr) {
    return irq_NAME;
}

/* external globals */

extern uint8 xack;                      /* XACK signal */
extern int32 irq_req;                   /* i8080 INT signal */
extern uint16 PCX;
extern DEVICE isbc064_dev;
extern DEVICE isbc464_dev;

/* multibus Standard SIMH Device Data Structures */

UNIT irq_unit = { 
    UDATA (&irq_svc, 0, 0), 1
};

REG irq_reg[] = { 
    { HRDATA (MBIRQ, mbirq, 32) }, 
    { NULL }
};

DEBTAB irq_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE irq_dev = {
    "IRQ",              //name 
    &irq_unit,          //units 
    irq_reg,            //registers 
    NULL,               //modifiers
    1,                  //numunits 
    16,                 //aradix  
    16,                 //awidth  
    1,                  //aincr  
    16,                 //dradix  
    8,                  //dwidth
    NULL,               //examine  
    NULL,               //deposit  
    &irq_reset,         //reset 
    NULL,               //boot
    NULL,               //attach  
    NULL,               //detach
    NULL,               //ctxt     
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    irq_debug,          //debflags
    NULL,               //msize
    NULL,               //lname
    NULL,               //help routine
    NULL,               //attach help routine
    NULL,               //help context
    &irq_desc           //device description
};

/* Service routines to handle simulator functions */

/* Reset routine */

t_stat irq_reset(DEVICE *dptr)
{
//    if (SBC_reset(NULL) == 0) { 
        sim_printf("  Interrupt: Reset\n");
        sim_activate (&irq_unit, irq_unit.wait); /* activate unit */
        return SCPE_OK;
//    } else {
//        sim_printf("   Interrupt: SBC not selected\n");
//        return SCPE_OK;
//    }
}

/* service routine - actually does the simulated interrupts */

t_stat irq_svc(UNIT *uptr)
{
    switch (mbirq) {
        case INT_2:
            set_cpuint(INT_R);
            break;
        default:
            break;
    }
    sim_activate (&irq_unit, irq_unit.wait); /* continue poll */
    return SCPE_OK;
}

void set_irq(int32 irq_num)
{
    mbirq |= irq_num;
}

void clr_irq(int32 irq_num)
{
    mbirq &= ~irq_num;
}

/* end of irq.c */
