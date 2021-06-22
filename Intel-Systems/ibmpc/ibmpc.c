/*  ibmpcxt.c: IBM PC Processor simulator

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

        This software was written by Bill Beech, Jul 2016, to allow emulation of IBM PC
        Computer Systems.
*/

#include "system_defs.h"

int32   nmiflg = 0;                     //mask NMI off
uint8   dmapagreg0, dmapagreg1, dmapagreg2, dmapagreg3; 
extern uint16 port;                     //port called in dev_table[port]

/* function prototypes */

uint8 get_mbyte(uint32 addr);
uint16 get_mword(uint32 addr);
void put_mbyte(uint32 addr, uint8 val);
void put_mword(uint32 addr, uint16 val);
t_stat SBC_reset (DEVICE *dptr, uint16 base);
uint8 enbnmi(t_bool io, uint8 data);
uint8 dmapag(t_bool io, uint8 data);
uint8 dmapag0(t_bool io, uint8 data);
uint8 dmapag1(t_bool io, uint8 data);
uint8 dmapag2(t_bool io, uint8 data);
uint8 dmapag3(t_bool io, uint8 data);

/* external function prototypes */

extern t_stat i8088_reset (DEVICE *dptr);   /* reset the 8088 emulator */
extern uint8 xtbus_get_mbyte(uint32 addr);
extern void  xtbus_put_mbyte(uint32 addr, uint8 val);
extern uint8 EPROM_get_mbyte(uint32 addr);
extern uint8 RAM_get_mbyte(uint32 addr);
extern void RAM_put_mbyte(uint32 addr, uint8 val);
extern UNIT i8255_unit[];
extern UNIT EPROM_unit;
extern UNIT RAM_unit;
extern t_stat i8237_reset (DEVICE *dptr);
extern t_stat i8253_reset (DEVICE *dptr);
extern t_stat i8255_reset (DEVICE *dptr);
extern t_stat i8259_reset (DEVICE *dptr);
extern t_stat EPROM_reset (DEVICE *dptr);
extern t_stat RAM_reset (DEVICE *dptr);
extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint8);

/*  SBC reset routine */

t_stat SBC_reset (DEVICE *dptr, uint16 base)
{    
    sim_printf("Initializing IBM PC:\n");
    i8088_reset (NULL);
    i8237_reset (I8237_BASE_0);
    i8253_reset (I8253_BASE_0);
    i8255_reset (I8255_BASE_0);
    i8259_reset (I8259_BASE_0);
    EPROM_reset (ROM_BASE, ROM_SIZE);
    RAM_reset (RAM_BASE, RAM_SIZE);
    reg_dev(enbnmi, NMI_BASE); 
    reg_dev(dmapag0, DMAPAG_BASE_0); 
    reg_dev(dmapag1, DMAPAG_BASE_1); 
    reg_dev(dmapag2, DMAPAG_BASE_2); 
    reg_dev(dmapag3, DMAPAG_BASE_3); 
    return SCPE_OK;
}

uint8 dmapag0(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read data port */
        ;
    } else {                            /* write data port */
        dmapagreg0 = data;
        //sim_printf("dmapag0: dmapagreg0=%04X\n", data);
    }
    return 0;
}

uint8 dmapag1(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read data port */
        ;
    } else {                            /* write data port */
        dmapagreg1 = data;
        //sim_printf("dmapag1: dmapagreg1=%04X\n", data);
    }
    return 0;
}

uint8 dmapag2(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read data port */
        ;
    } else {                            /* write data port */
        dmapagreg2 = data;
        //sim_printf("dmapag2: dmapagreg2=%04X\n", data);
    }
    return 0;
}

uint8 dmapag3(t_bool io, uint8 data)
{
    //sim_printf("dmapag3: entered\n");
    if (io == 0) {                      /* read data port */
        ;
    } else {                            /* write data port */
        dmapagreg3 = data;
        //sim_printf("dmapag3: dmapagreg3=%04X\n", data);
    }
    return 0;
}

uint8 enbnmi(t_bool io, uint8 data)
{
    if (io == 0) {                      /* read data port */
        ;
    } else {                            /* write data port */
        if (data & 0x80) {
            nmiflg = 1;
            //sim_printf("enbnmi: NMI enabled\n");
        } else {
            nmiflg = 0;
            //sim_printf("enbnmi: NMI disabled\n");
        }
    }
    return 0;
}

/*  get a byte from memory - handle RAM, ROM, I/O, and pcbus memory */

uint8 get_mbyte(uint32 addr)
{
    /* if local EPROM handle it */
    if ((addr >= (uint32)EPROM_unit.u3) && (addr <= (uint32)(EPROM_unit.u3 + EPROM_unit.capac))) {
//        sim_printf("Write to R/O memory address %05X - ignored\n", addr);
        return EPROM_get_mbyte(addr);
    }
    /* if local RAM handle it */
    if ((addr >= (uint32)RAM_unit.u3) && (addr <= (uint32)(RAM_unit.u3 + RAM_unit.capac))) {
        return RAM_get_mbyte(addr);
    }
    /* otherwise, try the pcbus */
    return xtbus_get_mbyte(addr);
}

/*  get a word from memory - handle RAM, ROM, I/O, and pcbus memory */

uint16 get_mword(uint32 addr)
{
    uint16 val;

    val = get_mbyte(addr);
    val |= (get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte to memory - handle RAM, ROM, I/O, and pcbus memory */

void put_mbyte(uint32 addr, uint8 val)
{
    /* if local EPROM handle it */
    if ((addr >= (uint32)EPROM_unit.u3) && (addr <= (uint32)(EPROM_unit.u3 + EPROM_unit.capac))) {
        sim_printf("Write to R/O memory address %04X - ignored\n", addr);
        return;
    } /* if local RAM handle it */
    if ((addr >= (uint32)RAM_unit.u3) && (addr <= (uint32)(RAM_unit.u3 + RAM_unit.capac))) {
        RAM_put_mbyte(addr, val);
        return;
    } /* otherwise, try the pcbus */
    xtbus_put_mbyte(addr, val);
}

/*  put a word to memory - handle RAM, ROM, I/O, and pcbus memory */

void put_mword(uint32 addr, uint16 val)
{
    put_mbyte(addr, val & 0xff);
    put_mbyte(addr+1, val >> 8);
}

/* end of ibmpc.c */
