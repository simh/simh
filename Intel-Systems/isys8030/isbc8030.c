/*  iSBC8030.c: Intel iSBC 80/30 SBC simulator

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

        04 Nov 16 - Original file.

    NOTES:

        This software was written by Bill Beech, Nov 2016, to allow emulation of Multibus
        Computer Systems.
*/

#include "system_defs.h"

/* function prototypes */

t_stat SBC_config(void);
uint8 get_mbyte(uint16 addr);
uint16 get_mword(uint16 addr);
void put_mbyte(uint16 addr, uint8 val);
void put_mword(uint16 addr, uint16 val);
t_stat SBC_reset (DEVICE *dptr);

/* external globals */

extern uint8 i8255_C[4];                    //port C byte I/O
extern uint16   PCX;

/* external function prototypes */

extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern t_stat i8080_reset (DEVICE *dptr);   /* reset the 8080 emulator */
extern DEVICE i8080_dev;
extern t_stat i8251_reset (DEVICE *dptr);
extern DEVICE i8251_dev;
extern t_stat i8253_reset (DEVICE *dptr);
extern DEVICE i8253_dev;
extern t_stat i8255_reset (DEVICE *dptr);
extern DEVICE i8255_dev;
extern t_stat i8259_reset (DEVICE *dptr);
extern DEVICE i8259_dev;
extern uint8 EPROM_get_mbyte(uint16 addr, uint8 devnum);
extern UNIT EPROM_unit[];
extern t_stat EPROM_reset (DEVICE *dptr, uint16 base, uint16 size);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern UNIT RAM_unit;
extern t_stat RAM_reset (DEVICE *dptr, uint16 base, uint16 size);
extern t_stat i8251_cfg(uint8 base, uint8 devnum);
extern t_stat i8253_cfg(uint8 base, uint8 devnum);
extern t_stat i8255_cfg(uint8 base, uint8 devnum);
extern t_stat i8259_cfg(uint8 base, uint8 devnum);
extern t_stat RAM_cfg(uint16 base, uint16 size);
extern t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum);

// globals

int onetime = 0;

t_stat SBC_config(void)
{
    sim_printf("Configuring iSBC-80/30 SBC\n  Onboard Devices:\n");
    i8251_cfg(I8251_BASE, 0);
    i8253_cfg(I8253_BASE, 0);
    i8255_cfg(I8255_BASE, 0);
    i8259_cfg(I8259_BASE, 0);
    EPROM_cfg(ROM_BASE, ROM_SIZE, 0);
    RAM_cfg(RAM_BASE, RAM_SIZE);
    return SCPE_OK;
}

/*  SBC reset routine */

t_stat SBC_reset (DEVICE *dptr)
{    
    if (onetime == 0) {
        SBC_config();
        onetime++;
    }
    i8080_reset(&i8080_dev);
    i8251_reset(&i8251_dev);
    i8253_reset(&i8253_dev);
    i8255_reset(&i8255_dev);
    i8259_reset(&i8259_dev);
    return SCPE_OK;
}

/*  get a byte from memory - handle RAM, ROM, I/O, and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    /* if local EPROM handle it */
    if ((ROM_DISABLE && (i8255_C[0] & 0x80)) || (ROM_DISABLE == 0)) { /* EPROM enabled */
        if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
            return EPROM_get_mbyte(addr, 0);
        }
    } /* if local RAM handle it */
    if ((RAM_DISABLE && (i8255_C[0] & 0x20)) || (RAM_DISABLE == 0)) { /* RAM enabled */
        if ((addr >= RAM_unit.u3) && ((uint16)addr <= (RAM_unit.u3 + RAM_unit.capac))) {
            return RAM_get_mbyte(addr);
        }
    } /* otherwise, try the multibus */
    return multibus_get_mbyte(addr);
}

/*  get a word from memory */

uint16 get_mword(uint16 addr)
{
    uint16 val;

    val = get_mbyte(addr);
    val |= (get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte to memory - handle RAM, ROM, I/O, and Multibus memory */

void put_mbyte(uint16 addr, uint8 val)
{
    /* if local EPROM handle it */
    if ((ROM_DISABLE && (i8255_C[0] & 0x80)) || (ROM_DISABLE == 0)) { /* EPROM enabled */
        if ((addr >= EPROM_unit->u3) && ((uint16)addr <= (EPROM_unit->u3 + EPROM_unit->capac))) {
            sim_printf("Write to R/O memory address %04X from %04X - ignored\n", addr, PCX);
            return;
        }
    } /* if local RAM handle it */
    if ((RAM_DISABLE && (i8255_C[0] & 0x20)) || (RAM_DISABLE == 0)) { /* RAM enabled */
        if ((addr >= RAM_unit.u3) && ((uint16)addr <= (RAM_unit.u3 + RAM_unit.capac))) {
            RAM_put_mbyte(addr, val);
            return;
        }
    } /* otherwise, try the multibus */
    multibus_put_mbyte(addr, val);
}

/*  put a word to memory */

void put_mword(uint16 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of iSBC8030.c */
