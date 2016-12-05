/*  iSBC80-30.c: Intel iSBC 80/30 Processor simulator

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

uint8 get_mbyte(uint16 addr);
uint16 get_mword(uint16 addr);
void put_mbyte(uint16 addr, uint8 val);
void put_mword(uint16 addr, uint16 val);
t_stat SBC_reset (DEVICE *dptr);

/* external globals */

extern uint8 i8255_C[4];                    //port C byte I/O

/* external function prototypes */

extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern t_stat i8080_reset (DEVICE *dptr);   /* reset the 8080 emulator */
extern int32 i8251_devnum;
extern t_stat i8251_reset (DEVICE *dptr, uint16 base);
extern int32 i8253_devnum;
extern t_stat i8253_reset (DEVICE *dptr, uint16 base);
extern int32 i8255_devnum;
extern t_stat i8255_reset (DEVICE *dptr, uint16 base);
extern int32 i8259_devnum;
extern t_stat i8259_reset (DEVICE *dptr, uint16 base);
extern uint8 EPROM_get_mbyte(uint16 addr);
extern UNIT EPROM_unit;
extern t_stat EPROM_reset (DEVICE *dptr, uint16 size);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern UNIT RAM_unit;
extern t_stat RAM_reset (DEVICE *dptr, uint16 base, uint16 size);

/*  SBC reset routine */

t_stat SBC_reset (DEVICE *dptr)
{    
    sim_printf("Initializing iSBC-80/24:\n");
    i8080_reset (NULL);
    i8251_devnum = 0;
    i8251_reset (NULL, I8251_BASE);
    i8253_devnum = 0;
    i8253_reset (NULL, I8253_BASE);
    i8255_devnum = 0;
    i8255_reset (NULL, I8255_BASE);
    i8259_devnum = 0;
    i8259_reset (NULL, I8259_BASE);
    EPROM_reset (NULL, ROM_SIZE);
    RAM_reset (NULL, RAM_BASE, RAM_SIZE);
    return SCPE_OK;
}

/*  get a byte from memory - handle RAM, ROM, I/O, and Multibus memory */

uint8 get_mbyte(uint16 addr)
{
    /* if local EPROM handle it */
    if ((ROM_DISABLE && (i8255_C[0] & 0x20)) || (ROM_DISABLE == 0)) { /* EPROM enabled */
	if ((addr >= EPROM_unit.u3) && ((uint16)addr < (EPROM_unit.u3 + EPROM_unit.capac))) {
	    return EPROM_get_mbyte(addr);
	}
    } /* if local RAM handle it */
    if ((RAM_DISABLE && (i8255_C[0] & 0x10)) || (RAM_DISABLE == 0)) { /* RAM enabled */
	if ((addr >= RAM_unit.u3) && ((uint16)addr < (RAM_unit.u3 + RAM_unit.capac))) {
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
    if ((ROM_DISABLE && (i8255_C[0] & 0x20)) || (ROM_DISABLE == 0)) { /* EPROM enabled */
        if ((addr >= EPROM_unit.u3) && ((uint16)addr <= (EPROM_unit.u3 + EPROM_unit.capac))) {
            sim_printf("Write to R/O memory address %04X - ignored\n", addr);
        return;
        }
    } /* if local RAM handle it */
    if ((RAM_DISABLE && (i8255_C[0] & 0x10)) || (RAM_DISABLE == 0)) { /* RAM enabled */
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

/* end of iSBC80-30.c */
