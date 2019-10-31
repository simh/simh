/*  ipb.c: Intel IPB Processor simulator

    Copyright (c) 2017, William A. Beech

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

    01 Mar 18 - Original file.
*/

#include "system_defs.h"

/* function prototypes */

uint8 get_mbyte(uint16 addr);
uint16 get_mword(uint16 addr);
void put_mbyte(uint16 addr, uint8 val);
void put_mword(uint16 addr, uint16 val);
t_stat SBC_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat i8080_reset (DEVICE *dptr);   /* reset the 8080 emulator */
extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 EPROM_get_mbyte(uint16 addr);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern t_stat i8251_cfg(uint8 base, uint8 devnum);
extern t_stat i8251_reset(DEVICE *dptr);
extern t_stat i8253_cfg(uint8 base, uint8 devnum);
extern t_stat i8253_reset(DEVICE *dptr);
extern t_stat i8255_cfg(uint8 base, uint8 devnum);
extern t_stat i8255_reset(DEVICE *dptr);
extern t_stat i8259_cfg(uint8 base, uint8 devnum);
extern t_stat i8259_reset(DEVICE *dptr);
extern t_stat EPROM_reset(DEVICE *dptr);
extern t_stat RAM_reset(DEVICE *dptr);
extern t_stat ipc_cont_reset(DEVICE *dptr);
extern t_stat ipc_cont_cfg(uint8 base, uint8 devnum); 
extern t_stat ioc_cont_reset(DEVICE *dptr);
extern t_stat ioc_cont_cfg(uint8 base, uint8 devnum); 
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern t_stat EPROM_cfg(uint16 base, uint16 size);
extern t_stat RAM_cfg(uint16 base, uint16 size);
extern t_stat multibus_cfg();   

/* globals */

int onetime = 0;

/* extern globals */

extern uint16 PCX;                    /* program counter */
extern UNIT i8255_unit;
extern UNIT EPROM_unit;
extern UNIT RAM_unit;
extern UNIT ipc_cont_unit;
extern UNIT ioc_cont_unit;
extern DEVICE i8080_dev;
extern DEVICE i8251_dev;
extern DEVICE i8253_dev;
extern DEVICE i8255_dev;
extern DEVICE i8259_dev;
extern DEVICE ipc_cont_dev;
extern DEVICE ioc_cont_dev;

t_stat SBC_config(void)
{
    sim_printf("Configuring IPB SBC\n  Onboard Devices:\n");
    i8251_cfg(I8251_BASE_0, 0); 
    i8251_cfg(I8251_BASE_1, 1); 
    i8253_cfg(I8253_BASE, 0); 
    i8255_cfg(I8255_BASE_0, 0); 
    i8255_cfg(I8255_BASE_1, 1); 
    i8259_cfg(I8259_BASE_0, 0); 
    i8259_cfg(I8259_BASE_1, 1); 
    ipc_cont_cfg(ICONT_BASE, 0); 
    ioc_cont_cfg(DBB_BASE, 0); 
    EPROM_cfg(ROM_BASE, ROM_SIZE);
    RAM_cfg(RAM_BASE, RAM_SIZE);
    return SCPE_OK;
}

/*  CPU reset routine 
    put here to cause a reset of the entire IPC system */

t_stat SBC_reset (DEVICE *dptr)
{    
    if (onetime == 0) {
        SBC_config();   
        multibus_cfg();   
        onetime++;
    }
    i8080_reset(&i8080_dev);
    i8251_reset(&i8251_dev);
    i8253_reset(&i8253_dev);
    i8255_reset(&i8255_dev);
    i8259_reset(&i8259_dev);
    ipc_cont_reset(&ipc_cont_dev);
    ioc_cont_reset(&ioc_cont_dev);
    return SCPE_OK;
}

/*  get a byte from memory - handle RAM, ROM and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    if (addr >= 0xF800) {               //monitor ROM - always there
        return EPROM_get_mbyte(addr - 0xF000); //top half of EPROM
    }
    if ((addr < 0x1000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //startup
        return EPROM_get_mbyte(addr);   //top half of EPROM for boot
    }
    if ((addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x10) == 0)) { //diagnostic ROM
        return EPROM_get_mbyte(addr - 0xE800); //bottom half of EPROM
    }
    if (addr < 0x8000)                  //IPB RAM
        return RAM_get_mbyte(addr);
    else
        return multibus_get_mbyte(addr); //check multibus cards
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
    if (addr >= 0xF800) {               //monitor ROM - always there
        return;
    } 
    if ((addr < 0x1000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //startup
        return;
    }
    if ((addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x10) == 0)) { //diagnostic ROM
        return;
    }
    if (addr < 0x8000) {
        RAM_put_mbyte(addr, val);       //IPB RAM
        return;
    }
    multibus_put_mbyte(addr, val);      //check multibus cards
}

/*  put a word to memory */

void put_mword(uint16 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of ipb.c */
