/*  monitor.c: Intel MDS-800 Monitor Module simulator

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

t_stat monitor_cfg(uint16 base, uint16 size, uint8 devnum);
t_stat monitor_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat i8251_reset(DEVICE *dptr);
extern t_stat i8251_cfg(uint16 base, uint16 devnum, uint8 dummy);
extern t_stat EPROM_reset(DEVICE *dptr);
extern t_stat EPROM_cfg(uint16 base, uint16 size, uint8 devnum);
extern uint8 reg_dev(uint8 (*routine)(t_bool, uint8, uint8), uint16, uint16, uint8);
extern uint8 unreg_dev(uint16);
extern uint8 i3214_monitor_do_boot(t_bool io, uint8 data, uint8 devnum);

// external globals

extern uint32 PCX;                    /* program counter */
extern DEVICE i8251_dev;
extern DEVICE EPROM_dev;
extern uint8 monitor_boot;

// globals

t_stat monitor_cfg(uint16 base, uint16 size, uint8 devnum)
{
    return SCPE_OK;
}

/*  Monitor reset routine 
    put here to cause a reset of the entire IPC system */

t_stat monitor_reset (DEVICE *dptr)
{    
    monitor_boot = 0x00;
    return SCPE_OK;
}

/* end of monitor.c */
