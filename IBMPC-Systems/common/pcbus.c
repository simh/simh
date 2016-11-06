/*  pcbus.c: PC bus simulator

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

        This software was written by Bill Beech, Jul 2016, to allow emulation of PC XT
        Computer Systems.

*/

#include "system_defs.h"

int32   mbirq = 0;                      /* set no interrupts */

/* function prototypes */

t_stat xtbus_svc(UNIT *uptr);
t_stat xtbus_reset(DEVICE *dptr);
void set_irq(int32 int_num);
void clr_irq(int32 int_num);
uint8 nulldev(t_bool io, uint8 data);
uint16 reg_dev(uint8 (*routine)(t_bool io, uint8 data), uint16 port);
void dump_dev_table(void);
t_stat xtbus_reset (DEVICE *dptr);
uint8 xtbus_get_mbyte(uint32 addr);
void xtbus_put_mbyte(uint32 addr, uint8 val);

/* external function prototypes */

extern t_stat SBC_reset(DEVICE *dptr);      /* reset the PC XT simulator */
extern void set_cpuint(int32 int_num);

/* external globals */

extern int32 int_req;                       /* i8088 INT signal */
extern uint16 port;                         //port called in dev_table[port]

/* Standard SIMH Device Data Structures */

UNIT xtbus_unit = { 
    UDATA (&xtbus_svc, 0, 0), 20 
};

REG xtbus_reg[] = { 
    { HRDATA (MBIRQ, mbirq, 32) }, 
};

DEBTAB xtbus_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE xtbus_dev = {
    "PCBUS",                    //name 
    &xtbus_unit,             //units 
    xtbus_reg,               //registers 
    NULL,                       //modifiers
    1,                          //numunits 
    16,                         //aradix  
    16,                         //awidth  
    1,                          //aincr  
    16,                         //dradix  
    8,                          //dwidth
    NULL,                       //examine  
    NULL,                       //deposit  
    &xtbus_reset,            //reset 
    NULL,                       //boot
    NULL,                       //attach  
    NULL,                       //detach
    NULL,                       //ctxt     
    DEV_DEBUG,                  //flags 
    0,                          //dctrl 
    xtbus_debug,             //debflags
    NULL,                       //msize
    NULL                        //lname
};

/* Service routines to handle simulator functions */

/* service routine - actually does the simulated interrupts */

t_stat xtbus_svc(UNIT *uptr)
{
    switch (mbirq) {
        case INT_1:
            set_cpuint(INT_R);
            sim_printf("xtbus_svc: mbirq=%04X int_req=%04X\n", mbirq, int_req);
            break;
        default:
            //sim_printf("xtbus_svc: default mbirq=%04X\n", mbirq);
            break;
    }
    sim_activate (&xtbus_unit, xtbus_unit.wait); /* continue poll */
    return SCPE_OK;
}

/* Reset routine */

t_stat xtbus_reset(DEVICE *dptr)
{
    SBC_reset(NULL); 
    sim_printf("   Xtbus: Reset\n");
    sim_activate (&xtbus_unit, xtbus_unit.wait); /* activate unit */
    return SCPE_OK;
}

void set_irq(int32 int_num)
{
    mbirq |= int_num;
    sim_printf("set_irq: int_num=%04X mbirq=%04X\n", int_num, mbirq);
}

void clr_irq(int32 int_num)
{
    mbirq &= ~int_num;
    sim_printf("clr_irq: int_num=%04X mbirq=%04X\n", int_num, mbirq);
}

/* This is the I/O configuration table.  There are 1024 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device has been registered.
The actual 808X can address 65,536 I/O ports but the IBM only uses
the first 1024. */

struct idev {
    uint8 (*routine)(t_bool io, uint8 data);
};

struct idev dev_table[1024] = {
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
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 0FCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 100H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 104H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 108H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 10CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 110H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 114H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 118H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 11CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 120H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 124H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 128H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 12CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 130H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 134H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 138H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 13CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 140H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 144H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 148H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 14CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 150H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 154H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 158H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 15CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 160H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 164H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 168H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 16CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 170H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 174H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 178H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 17CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 180H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 184H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 188H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 18CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 190H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 194H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 198H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 19CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1A4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1A8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1B4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1B8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1C0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1C4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1C8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1CCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1D0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1D4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1D8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1DCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1E0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1E4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1E8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1ECH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1F0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1F4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1F8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 1FCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 200H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 204H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 208H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 20CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 210H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 214H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 218H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 21CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 220H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 224H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 228H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 22CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 230H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 234H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 238H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 23CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 240H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 244H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 248H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 24CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 250H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 254H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 258H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 25CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 260H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 264H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 268H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 26CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 270H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 274H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 278H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 27CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 280H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 284H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 288H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 28CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 290H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 294H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 298H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 29CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2A4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2A8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2B4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2B8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2C0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2C4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2C8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2CCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2D0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2D4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2D8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2DCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2E0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2E4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2E8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2ECH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2F0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2F4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2F8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 2FCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 300H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 304H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 308H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 30CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 310H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 314H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 318H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 31CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 320H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 324H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 328H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 32CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 330H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 334H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 338H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 33CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 340H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 344H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 348H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 34CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 350H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 354H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 358H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 35CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 360H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 364H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 368H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 36CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 370H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 374H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 378H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 37CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 380H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 384H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 388H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 38CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 390H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 394H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 398H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 39CH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3A4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3A8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3A0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3B4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3B8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3B0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3C0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3C4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3C8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3CCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3D0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3D4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3D8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3DCH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3E0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3E4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3E8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3ECH */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3F0H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3F4H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 3F8H */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev}          /* 3FCH */
};

uint8 nulldev(t_bool flag, uint8 data)
{
    sim_printf("xtbus: I/O Port %03X is not assigned io=%d data=%02X\n",
        port, flag, data);
    if (flag == 0)                      /* if we got here, no valid I/O device */
        return 0xFF;
}

uint16 reg_dev(uint8 (*routine)(t_bool io, uint8 data), uint16 port)
{
    if (dev_table[port].routine != &nulldev) {  /* port already assigned */
        sim_printf("xtbus: I/O Port %03X is already assigned\n", port);
    } else {
        sim_printf("Port %03X is assigned\n", port);
        dev_table[port].routine = routine;
    }
    //dump_dev_table();
    return port;
}

void dump_dev_table(void)
{
    int i;

    for (i=0; i<1024; i++) {
        if (dev_table[i].routine != &nulldev) {  /* assigned port */
            sim_printf("Port %03X is assigned\n", i); 
        }
    }
}

/*  get a byte from bus */

uint8 xtbus_get_mbyte(uint32 addr)
{
    return 0xFF;
}

/*  put a byte to bus */

void xtbus_put_mbyte(uint32 addr, uint8 val)
{
    ;
}

/* end of pcbus.c */

