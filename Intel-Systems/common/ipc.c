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
#define IPC     1

/* function prototypes */

t_stat SBC_config(void);
t_stat SBC_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat i8080_reset (DEVICE *dptr);   /* reset the 8080 emulator */
extern uint8 multibus_get_mbyte(uint16 addr);
extern void  multibus_put_mbyte(uint16 addr, uint8 val);
extern uint8 EPROM_get_mbyte(uint16 addr, uint8 devnum);
extern uint8 RAM_get_mbyte(uint16 addr);
extern void RAM_put_mbyte(uint16 addr, uint8 val);
extern  t_stat i8251_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern  t_stat i8253_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern  t_stat i8255_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern  t_stat i8259_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat i8251_reset(DEVICE *dptr);
extern t_stat i8253_reset(DEVICE *dptr);
extern t_stat i8255_reset(DEVICE *dptr);
extern t_stat i8259_reset(DEVICE *dptr);
extern t_stat EPROM_reset(DEVICE *dptr);
extern t_stat RAM_reset(DEVICE *dptr);
extern t_stat ipc_cont_reset(DEVICE *dptr);
extern  t_stat ioc_cont_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern  t_stat ipc_cont_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat ioc_cont_reset(DEVICE *dptr);
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint8, uint8);
extern t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum);
extern  t_stat RAM_cfg(uint16 base, uint16 size, uint8 dummy);

/* external globals */

extern uint8 xack;
extern uint32 PCX;                    /* program counter */
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

/* globals */

int ipc_onetime = 0;

t_stat SBC_config(void)
{
    sim_printf("Configuring IPC SBC\n  Onboard Devices:\n");
    i8251_cfg(I8251_BASE_0, 0, 0); 
    i8251_cfg(I8251_BASE_1, 1, 0); 
    i8253_cfg(I8253_BASE, 0, 0); 
    i8255_cfg(I8255_BASE_0, 0, 0); 
    i8255_cfg(I8255_BASE_1, 1, 0); 
    i8259_cfg(I8259_BASE_0, 0, 0); 
    i8259_cfg(I8259_BASE_1, 1, 0); 
    ipc_cont_cfg(ICONT_BASE, 0, 0); 
    ioc_cont_cfg(DBB_BASE, 0, 0); 
    EPROM_cfg(ROM_BASE, ROM_SIZE, 0);
    RAM_cfg(RAM_BASE, RAM_SIZE, 0);
    return SCPE_OK;
}

/*  CPU reset routine 
    put here to cause a reset of the entire IPC system */

t_stat SBC_reset (DEVICE *dptr)
{ 
    if (ipc_onetime == 0) {
        SBC_config();   
        ipc_onetime++;
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

/* end of ipc.c */
