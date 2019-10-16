/*  ipcmultibus.c: Multibus I simulator

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

        01 Mar 18 - Original file.

    NOTES:

        This software was written by Bill Beech, Dec 2010, to allow emulation of Multibus
        Computer Systems.

*/

#include "system_defs.h"

int32   mbirq = 0;                      /* set no multibus interrupts */

/* function prototypes */

t_stat multibus_cfg(void);
t_stat multibus_svc(UNIT *uptr);
t_stat multibus_reset(DEVICE *dptr);
void set_irq(int32 int_num);
void clr_irq(int32 int_num);
uint8 nulldev(t_bool io, uint8 data, uint8 devnum);
t_stat multibus_reset (DEVICE *dptr);
uint8 multibus_get_mbyte(uint16 addr);
uint16 multibus_get_mword(uint16 addr);
void multibus_put_mbyte(uint16 addr, uint8 val);
void multibus_put_mword(uint16 addr, uint16 val);

/* external function prototypes */

extern t_stat SBC_reset(DEVICE *dptr);      /* reset the IPC simulator */
extern void set_cpuint(int32 int_num);
extern t_stat zx200a_reset(DEVICE *dptr);
extern t_stat isbc201_reset (DEVICE *dptr);
extern t_stat isbc202_reset (DEVICE *dptr);
extern t_stat isbc206_reset (DEVICE *dptr);
extern t_stat isbc464_reset (DEVICE *dptr);
extern t_stat isbc064_reset (DEVICE *dptr);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern uint8 isbc064_get_mbyte(uint16 addr);
extern void isbc064_put_mbyte(uint16 addr, uint8 val);
extern uint8 isbc464_get_mbyte(uint16 addr);
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern t_stat isbc064_cfg(uint16 base, uint16 size);
extern t_stat isbc464_cfg(uint16 base, uint16 size);
extern t_stat isbc201_cfg(uint8 base);
extern t_stat isbc202_cfg(uint8 base);
extern t_stat isbc206_cfg(uint8 base);
extern t_stat zx200a_cfg(uint8 base);
extern t_stat RAM_cfg(uint16 base, uint16 size);
extern t_stat EPROM_cfg(uint16 base, uint16 size);

/* external globals */

extern uint8 xack;                          /* XACK signal */
extern int32 int_req;                       /* i8080 INT signal */
extern DEVICE isbc464_dev;
extern DEVICE isbc064_dev;
extern DEVICE isbc201_dev;
extern DEVICE isbc202_dev;
extern DEVICE isbc206_dev;
extern DEVICE zx200a_dev;

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
    if (SBC064_NUM) isbc064_cfg(SBC064_BASE, SBC064_SIZE);
    if (SBC464_NUM) isbc464_cfg(SBC464_BASE, SBC464_SIZE);
    if (SBC201_NUM) isbc201_cfg(SBC201_BASE);
    if (SBC202_NUM) isbc202_cfg(SBC202_BASE);
    if (SBC206_NUM) isbc206_cfg(SBC206_BASE);
    if (ZX200A_NUM) zx200a_cfg(ZX200A_BASE);
    return SCPE_OK;
}
/* service routine - actually does the simulated interrupts */

t_stat multibus_svc(UNIT *uptr)
{
    switch (mbirq) {
        case INT_1:
            set_cpuint(INT_R);
#if NIPC
            clr_irq(SBC202_INT);    /***** bad, bad, bad! */
#endif
//            sim_printf("multibus_svc: mbirq=%04X int_req=%04X\n", mbirq, int_req);
            break;
        default:
//            sim_printf("multibus_svc: default mbirq=%04X\n", mbirq);
            break;
    }
    sim_activate (&multibus_unit, multibus_unit.wait); /* continue poll */
    return SCPE_OK;
}

/* Reset routine */

t_stat multibus_reset(DEVICE *dptr)
{
    if (SBC_reset(NULL) == 0) { 
        sim_printf("  Multibus: Reset\n");
        if (SBC064_NUM) {          //device installed
            isbc064_reset(&isbc064_dev);
            sim_printf("    Multibus: SBC064 reset\n");
        }
        if (SBC464_NUM) { //unit enabled
            isbc464_reset(&isbc464_dev);
            sim_printf("    Multibus: SBC464 reset\n");
            }
        if (SBC201_NUM) { //unit enabled
            isbc201_reset(&isbc201_dev);
            sim_printf("    Multibus: SBC201 reset\n");
            }
        if (SBC202_NUM) { //unit enabled
            isbc202_reset(&isbc202_dev);
            sim_printf("    Multibus: SBC202 reset\n");
            }
        if (SBC206_NUM) { //unit enabled
            isbc206_reset(&isbc206_dev);
            sim_printf("    Multibus: SBC206 reset\n");
            }
        if (ZX200A_NUM) { //unit enabled
            zx200a_reset(&zx200a_dev);
            sim_printf("    Multibus: ZX200A reset\n");
            }
        sim_activate (&multibus_unit, multibus_unit.wait); /* activate unit */
        return SCPE_OK;
    } else {
        sim_printf("   Multibus: SBC not selected\n");
        return SCPE_OK;
    }
}

void set_irq(int32 int_num)
{
    mbirq |= int_num;
//    sim_printf("set_irq: int_num=%04X mbirq=%04X\n", int_num, mbirq);
}

void clr_irq(int32 int_num)
{
    mbirq &= ~int_num;
//    sim_printf("clr_irq: int_num=%04X mbirq=%04X\n", int_num, mbirq);
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

uint8 nulldev(t_bool flag, uint8 data, uint8 devnum)
{
    SET_XACK(0);                        /* set no XACK */
    return 0;                           //should be 0xff, but ISIS won't boot if not 0!
}

uint8 reg_dev(uint8 (*routine)(t_bool io, uint8 data, uint8 devnum), uint8 port, uint8 devnum)
{
    if (dev_table[port].routine != &nulldev) {  /* port already assigned */
        if (dev_table[port].routine != routine) {  /* different device? */
            sim_printf("         Multibus: I/O Port %04X is already assigned to different device\n", port);
            return 1;
        } else
            return 0;
    } else {
        dev_table[port].routine = routine;
        dev_table[port].devnum = devnum;
    }
    return 0;
}

/*  get a byte from memory */

uint8 multibus_get_mbyte(uint16 addr)
{
    uint8 val = 0;

    SET_XACK(0);                        /* set no XACK */
    if ((isbc464_dev.flags & DEV_DIS) == 0) { //ROM is enabled
        if (addr >= SBC464_BASE && addr <= (SBC464_BASE + SBC464_SIZE - 1))
            return(isbc464_get_mbyte(addr));
    } 
    if ((isbc064_dev.flags & DEV_DIS) == 0) { //RAM is enabled
        if (addr >= SBC064_BASE && addr <= (SBC064_BASE + SBC064_SIZE - 1))
            val = isbc064_get_mbyte(addr);
    }
    if (xack == 0)
        val = RAM_get_mbyte(addr);
    return val;
}

/*  get a word from memory */

uint16 multibus_get_mword(uint16 addr)
{
    uint16 val;

    val = multibus_get_mbyte(addr);
    val |= (multibus_get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte to memory */

void multibus_put_mbyte(uint16 addr, uint8 val)
{
    SET_XACK(0);                        /* set no XACK */
    if ((isbc064_dev.flags & DEV_DIS) == 0) { //RAM is enabled
        if (addr >= SBC064_BASE && addr <= (SBC064_BASE + SBC064_SIZE - 1))
            isbc064_put_mbyte(addr, val);
    }
    RAM_put_mbyte(addr, val);
}

/*  put a word to memory */

void multibus_put_mword(uint16 addr, uint16 val)
{
    multibus_put_mbyte(addr, val & 0xff);
    multibus_put_mbyte(addr+1, val >> 8);
}

/* end of ipbmultibus.c */

