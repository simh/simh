/*  ipc.c: Intel IPC Processor simulator

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

    07 Jun 16 - Original file.
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
//extern uint8 multibus_get_mbyte(uint16 addr);
//extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 EPROM_get_mbyte(uint16 addr);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern UNIT i8255_unit;
extern UNIT EPROM_unit;
extern UNIT RAM_unit;
extern UNIT ipc_cont_unit;
extern UNIT ioc_cont_unit;
extern t_stat i8251_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern t_stat i8253_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern t_stat i8255_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern t_stat i8259_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern t_stat EPROM_reset(DEVICE *dptr, uint16 size);
extern t_stat RAM_reset(DEVICE *dptr, uint16 base, uint16 size);
extern t_stat ipc_cont_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern t_stat ioc_cont_reset(DEVICE *dptr, uint16 base, uint8 devnum);
extern uint32 saved_PC;                    /* program counter */

/*  CPU reset routine 
    put here to cause a reset of the entire IPC system */

t_stat SBC_reset (DEVICE *dptr)
{    
    sim_printf("Initializing MDS-225\n");
    i8080_reset(NULL);
    i8251_reset(NULL, I8251_BASE_0, 0);
    i8251_reset(NULL, I8251_BASE_1, 0);
    i8253_reset(NULL, I8253_BASE, 0);
    i8255_reset(NULL, I8255_BASE_0, 0);
    i8255_reset(NULL, I8255_BASE_1, 1);
    i8259_reset(NULL, I8259_BASE_0, 0);
    i8259_reset(NULL, I8259_BASE_1, 1);
    EPROM_reset(NULL, ROM_SIZE);
    RAM_reset(NULL, RAM_BASE, RAM_SIZE);
    ipc_cont_reset(NULL, ICONT_BASE, 0);
    ioc_cont_reset(NULL, DBB_BASE, 0);
    return SCPE_OK;
}

/*  get a byte from memory - handle RAM, ROM and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    if (addr >= 0xF800) {               //monitor ROM - always there
//        sim_printf("get_mbyte: Monitor ROM ipc_cont=%02X\n", ipc_cont_unit.u3);
        return EPROM_get_mbyte(addr - 0xF000);
    }
    if ((addr < 0x1000) && ((ipc_cont_unit.u3 & 0x01) == 0)) { //startup
//        sim_printf("get_mbyte: Startup ROM ipc_cont=%02X\n", ipc_cont_unit.u3);
        return EPROM_get_mbyte(addr);
    }
    if ((addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //diagnostic ROM
//        sim_printf("get_mbyte: Diagnostic ROM ipc_cont=%02X\n", ipc_cont_unit.u3);
        return EPROM_get_mbyte(addr - 0xE800);
    }
    return RAM_get_mbyte(addr);
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
        sim_printf("Write to R/O memory address %04X from PC=%04X - ignored\n", addr, saved_PC);
        return;
    } 
    if ((addr < 0x1000) && ((ipc_cont_unit.u3 & 0x01) == 0)) { //startup
        sim_printf("Write to R/O memory address %04X from PC=%04X - ignored\n", addr, saved_PC);
        return;
    }
    if ((addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //diagnostic ROM
        sim_printf("Write to R/O memory address %04X from PC=%04X - ignored\n", addr, saved_PC);
        return;
    }
    RAM_put_mbyte(addr, val);
}

/*  put a word to memory */

void put_mword(uint16 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of ipc.c */
