/* sdkmultibus.c: Multibus I simulator

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

/* function prototypes */

t_stat multibus_cfg(void);
t_stat multibus_svc(UNIT *uptr);
t_stat multibus_reset(DEVICE *dptr);
void set_irq(int32 int_num);
void clr_irq(int32 int_num);
uint8 nulldev(t_bool io, uint8 port, uint8 devnum);
uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
t_stat multibus_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat SBC_reset(DEVICE *dptr);  /* reset the iSBC80/10 emulator */
extern void set_cpuint(int32 int_num);

/* local globals */

int32   mbirq = 0;                      /* set no multibus interrupts */

/* external globals */

extern uint8 xack;                      /* XACK signal */
extern int32 int_req;                   /* i8080 INT signal */
extern uint16 PCX;

/* multibus Standard SIMH Device Data Structures */

UNIT multibus_unit = { 
    UDATA (&multibus_svc, 0, 0), 20 
};

REG multibus_reg[] = { 
    { HRDATA (MBIRQ, mbirq, 32) }, 
    { HRDATA (XACK, xack, 8) },
    { NULL }
};

DEBTAB multibus_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE multibus_dev = {
    "MBIRQ",                    //name 
    &multibus_unit,             //units 
    multibus_reg,               //registers 
    NULL,                       //modifiers
    1,                          //numunits 
    16,                         //aradix  
    16,                         //awidth  
    1,                          //aincr  
    16,                         //dradix  
    8,                          //dwidth
    NULL,                       //examine  
    NULL,                       //deposit  
    &multibus_reset,            //reset 
    NULL,                       //boot
    NULL,                       //attach  
    NULL,                       //detach
    NULL,                       //ctxt     
    DEV_DEBUG,                  //flags 
    0,                          //dctrl 
    multibus_debug,             //debflags
    NULL,                       //msize
    NULL                        //lname
};

/* Service routines to handle simulator functions */

// multibus_cfg

t_stat multibus_cfg(void)
{
    sim_printf("Configuring Multibus Devices\n");
    return SCPE_OK;
}

/* Reset routine */

t_stat multibus_reset(DEVICE *dptr)
{
    if (SBC_reset(NULL) == 0) { 
        sim_printf("  Multibus: Reset\n");
        sim_activate (&multibus_unit, multibus_unit.wait); /* activate unit */
        return SCPE_OK;
    } else {
        sim_printf("   Multibus: SBC not selected\n");
        return SCPE_OK;
    }
}

/* service routine - actually does the simulated interrupts */

t_stat multibus_svc(UNIT *uptr)
{
    switch (mbirq) {
        case INT_2:
            set_cpuint(INT_R);
            break;
        default:
            break;
    }
    sim_activate (&multibus_unit, multibus_unit.wait); /* continue poll */
    return SCPE_OK;
}

void set_irq(int32 int_num)
{
    mbirq |= int_num;
}

void clr_irq(int32 int_num)
{
    mbirq &= ~int_num;
}

/* This is the I/O configuration table.  There are 256 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device has been registered.
*/
struct idev {
    uint8 (*routine)(t_bool io, uint8 data, uint8 devnum); 
    uint8 port;
    uint8 devnum;
};

struct idev dev_table[256] = {
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 000H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 004H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 008H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 00CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 010H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 014H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 018H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 01CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 020H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 024H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 028H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 02CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 030H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 034H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 038H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 03CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 040H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 044H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 048H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 04CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 050H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 054H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 058H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 05CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 060H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 064H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 068H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 06CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 070H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 074H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 078H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 07CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 080H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 084H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 088H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 08CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 090H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 094H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 098H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 09CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0A4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0A8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0B4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0B8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0C0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0C4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0C8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0CCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0D0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0D4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0D8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0DCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0E0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0E4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0E8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0ECH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0F0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0F4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0F8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev}          /* 0FCH */
};

uint8 nulldev(t_bool io, uint8 data, uint8 devnum)
{
    SET_XACK(0);                        /* set no XACK */
    return 0xff;                        /* multibus has active high pullups and inversion */
}

uint8 reg_dev(uint8 (*routine)(t_bool io, uint8 data, uint8 devnum), uint8 port, uint8 devnum)
{
    if (dev_table[port].routine != &nulldev) { /* port already assigned */
        if (dev_table[port].routine != routine)
            sim_printf("         I/O Port %02X is already assigned\n", port);
    } else {
        dev_table[port].routine = routine;
        dev_table[port].devnum = devnum;
    }
    return 0;
}

/* end of multibus.c */
