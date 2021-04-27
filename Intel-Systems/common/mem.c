/*  mem.c: Intel memory simulator

    Copyright (c) 2020, William A. Beech

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

    23 Nov 20 - Original file.
*/

#include "system_defs.h"

/* function prototypes */

uint8 get_mbyte(uint16 addr);
uint16 get_mword(uint16 addr);
void put_mbyte(uint16 addr, uint8 val);
void put_mword(uint16 addr, uint16 val);

/* external function prototypes */

extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 EPROM_get_mbyte(uint16 addr, uint8 devnum);
extern UNIT EPROM_unit[];
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern UNIT RAM_unit[];
extern int mem_map;
extern uint8 monitor_boot;

/* globals */

/* extern globals */

extern uint16 PCX;                      /* program counter */
extern uint8 xack;                      /* XACK signal */
extern UNIT i8255_unit;                 //for isbc memory control
extern UNIT ipc_cont_unit;
extern UNIT ioc_cont_unit;
extern uint8 i8255_C[4];                    //port C byte I/O

/*  get a byte from memory - handle MODEL, RAM, ROM and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    uint8 val;
    
    SET_XACK(0);                        /* clear xack */
    if ((mem_map <= 1) && (addr >= 0xF800)) { //monitor ROM - always there IPB/IPC
        SET_XACK(1);                    //set xack
        return EPROM_get_mbyte(addr - 0xF000, 0); //top half of EPROM
    }
    if ((mem_map <= 1) && (addr < 0x1000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //startup IPB/IPC
        SET_XACK(1);                    //set xack
        return EPROM_get_mbyte(addr, 0); //top half of EPROM for boot
    }
    if ((mem_map <= 1) && (addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x10) == 0)) { //diagnostic ROM IPB/IPC
        SET_XACK(1);                    //set xack
        return EPROM_get_mbyte(addr - 0xE800, 0); //bottom half of EPROM
    }
    if (mem_map == 1) {                 //IPC RAM
        SET_XACK(1);                    //set xack
        return RAM_get_mbyte(addr);
    }
    if ((mem_map == 0) && (addr < 0x8000)) { //IPB RAM
        SET_XACK(1);                    //set xack
        return RAM_get_mbyte(addr);
    }
    if (mem_map == 2) {                 //800
        if (((monitor_boot & 0x04) == 0) && (addr >= EPROM_unit[0].u3) && (addr <= (EPROM_unit[0].u3 + EPROM_unit[0].capac)))
            return EPROM_get_mbyte(addr, 0); 
        else if ((addr >= EPROM_unit[1].u3) && (addr <= (EPROM_unit[1].u3 + EPROM_unit[1].capac)))
            return EPROM_get_mbyte(addr, 1);
    } 
    if (mem_map == 3) {                 //isdk80
        if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
            return EPROM_get_mbyte(addr, 0);
        } /* if local RAM handle it */
        else if ((addr >= RAM_unit->u3) && ((uint16)addr <= (RAM_unit->u3 + RAM_unit->capac))) {
            return RAM_get_mbyte(addr);
        } 
        else return 0xff;
    }
    if (mem_map == 4) {                 //isys80/XX
        /* if local EPROM handle it */
        if (i8255_C[0] & 0x80) { /* EPROM enabled */
            if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
                val = EPROM_get_mbyte(addr, 0);
                return val;
            }
        } /* if local RAM handle it */
        if (i8255_C[0] & 0x20) { /* RAM enabled */
            if ((addr >= RAM_unit->u3) && ((uint16)addr <= (RAM_unit->u3 + RAM_unit->capac))) {
                val = RAM_get_mbyte(addr);
                return val;
            }
        } /* otherwise, try the multibus */
    }
    return multibus_get_mbyte(addr);    //check multibus cards
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
    SET_XACK(0);                        /* set no XACK */
    if (addr >= 0xF800) {               //monitor ROM - always there IPB/IPC/800
        return;                         //do nothing
    } 
    if ((mem_map <= 1) && (addr < 0x1000) && ((ipc_cont_unit.u3 & 0x04) == 0)) { //startup IPB/IPC
        return;                         //do nothing
    }
    if ((mem_map <= 1) && (addr >= 0xE800) && (addr < 0xF000) && ((ipc_cont_unit.u3 & 0x10) == 0)) { //diagnostic ROM IPB/IPC
        return;                         //do nothing
    }
    if (mem_map == 1) {                 //IPC RAM
        SET_XACK(1);                    //set xack
        RAM_put_mbyte(addr, val);       //IPCRAM
        return;
    }
    if ((mem_map == 0) && (addr < 0x8000)) { //IPB RAM
        SET_XACK(1);                    //set xack
        RAM_put_mbyte(addr, val);       //IPB RAM
        return;
    }
    if (mem_map == 3) {                 //isdk80
        /* if local EPROM handle it */
        if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
            sim_printf("Write to R/O memory address %04X from PC=%04X - ignored\n", addr, PCX);
            return;
        } /* if local RAM handle it */
        if ((addr >= RAM_unit->u3) && ((uint16)addr <= (RAM_unit->u3 + RAM_unit->capac))) {
            RAM_put_mbyte(addr, val);
            return;
        }
    }
    if (mem_map == 4) {                 //isys80/xx
        /* if local EPROM handle it */
        if (i8255_C[0] & 0x80) { /* EPROM enabled */
            if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
                sim_printf("Write to R/O memory address %04X from PC=%04X - ignored\n", addr, PCX);
                return;
            }
        } /* if local RAM handle it */
        if (i8255_C[0] & 0x20) { /* RAM enabled */
            if ((addr >= RAM_unit->u3) && ((uint16)addr <= (RAM_unit->u3 + RAM_unit->capac))) {
                RAM_put_mbyte(addr, val);
                return;
            }
        } /* otherwise, try the multibus */
    }
    multibus_put_mbyte(addr, val);      //check multibus cards
}

/*  put a word to memory */

void put_mword(uint16 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of mem.c */
