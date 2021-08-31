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
#define IPC     0

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
extern t_stat i8251_cfg(uint8 base, uint8 devnum, uint8 dummy);
extern t_stat i8251_reset(DEVICE *dptr);
extern t_stat i8253_cfg(uint8 base, uint8 devnum, uint8 dummy);
extern t_stat i8253_reset(DEVICE *dptr);
extern t_stat i8255_cfg(uint8 base, uint8 devnum, uint8 dummy);
extern t_stat i8255_reset(DEVICE *dptr);
extern t_stat i8259_cfg(uint8 base, uint8 devnum, uint8 dummy);
extern t_stat i8259_reset(DEVICE *dptr);
extern t_stat EPROM_reset(DEVICE *dptr);
extern t_stat RAM_reset(DEVICE *dptr);
extern t_stat ipc_cont_reset(DEVICE *dptr);
extern t_stat ipc_cont_cfg(uint8 base, uint8 devnum, uint8 dummy); 
extern t_stat ioc_cont_reset(DEVICE *dptr);
extern t_stat ioc_cont_cfg(uint8 base, uint8 devnum, uint8 dummy); 
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint16, uint8);
extern t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum);
extern t_stat RAM_cfg(uint16 base, uint16 size, uint8 dummy);

/* globals */

int ipb_onetime = 0;

/* extern globals */

extern uint16 PCX;                      /* program counter */
extern uint8 xack;                      /* XACK signal */
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
    return SCPE_OK;
}

/*  CPU reset routine 
    put here to cause a reset of the entire IPC system */

t_stat SBC_reset (DEVICE *dptr)
{    
    if (ipb_onetime == 0) {
        ipb_onetime++;
    }
    return SCPE_OK;
}

/* end of ipb.c */
