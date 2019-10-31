/*  cpu.c: Intel MDS-800 CPU Module simulator

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

        This software was written by Bill Beech, Dec 2010, to allow emulation of Multibus
        Computer Systems.

    5 October 2017 - Original file.
*/

#include "system_defs.h"

/* function prototypes */

t_stat SBC_reset (DEVICE *dptr);
uint8 get_mbyte(uint16 addr);
uint16 get_mword(uint16 addr);
void put_mbyte(uint16 addr, uint8 val);
void put_mword(uint16 addr, uint16 val);
t_stat SBC_config(void);

// globals

int onetime = 0;

/* external function prototypes */

extern t_stat monitor_reset (void);
extern t_stat monitor_cfg(void);
extern t_stat fp_reset (void);
extern t_stat fp_cfg(void);
extern t_stat i8080_reset (DEVICE *dptr);   /* reset the 8080 emulator */
extern uint8 EPROM_get_mbyte(uint16 addr);
extern uint8 EPROM1_get_mbyte(uint16 addr);
extern t_stat multibus_cfg(void);   
extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern t_stat i3214_cfg(uint8 base, uint8 devnum);

// external globals

extern uint8 monitor_boot;
extern DEVICE i8080_dev;
extern uint8 i3214_mask;
extern uint8 EPROM_enable;
extern uint8 i3214_cnt;
extern uint8 i3214_ram[16];
extern uint8 BUS_OVERRIDE;

t_stat SBC_config(void)
{
    sim_printf("Configuring MDS-800 CPU Card\n  Onboard Devices:\n");
    i3214_cfg(I3214_BASE, 0);
    fp_cfg();
    monitor_cfg();
    return SCPE_OK;
}

/*  SBC reset routine 
    put here to cause a reset of the entire MDS-800 system */

t_stat SBC_reset (DEVICE *dptr)
{    
    if (onetime == 0) {
        SBC_config();
        multibus_cfg();   
        onetime++;
    }
    i8080_reset(&i8080_dev);
    EPROM_enable = 1;
    BUS_OVERRIDE = 0;
    fp_reset();
    monitor_reset();
    return SCPE_OK;
}

// memory operations

/*  get a byte from memory - handle RAM, ROM and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    uint8 val;

    if (((monitor_boot & 0x04) == 0) && (addr >= ROM0_BASE) && (addr <= (ROM0_BASE + ROM0_SIZE)))
        val = EPROM_get_mbyte(addr); 
    else if (ROM1_SIZE && (addr >= ROM1_BASE) && (addr <= (ROM1_BASE + ROM1_SIZE)))
        val = EPROM1_get_mbyte(addr);
    else val = multibus_get_mbyte(addr);
    val &= 0xFF;
    return val;
}

/*  get a word from memory */

uint16 get_mword(uint16 addr)
{
    uint16 val;

    val = get_mbyte(addr);
    val |= (get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte to memory - handle RAM, ROM and Multibus memory */

void put_mbyte(uint16 addr, uint8 val)
{
    multibus_put_mbyte(addr, val);
}

/*  put a word to memory */

void put_mword(uint16 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of cpu.c */
