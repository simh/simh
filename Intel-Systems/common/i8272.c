/*  i8272.c: Intel 8272 FDC adapter

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

        ?? ??? 10 - Original file.
        16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        u6 - unit number/device number
*/

#include "system_defs.h"

/* external globals */

extern uint16 port;

#define UNIT_V_ANSI (UNIT_V_UF + 0)     /* ANSI mode */
#define UNIT_ANSI   (1 << UNIT_V_ANSI)

#define TXR         0x01
#define RXR         0x02
#define TXE         0x04
#define SD          0x40

extern uint16 reg_dev(uint8 (*routine)(t_bool, uint8), uint16);

/* function prototypes */

t_stat i8272_svc (UNIT *uptr);
t_stat i8272_reset (DEVICE *dptr, uint16 base);
void i8272_reset1(uint8 devnum);
uint8 i8272_get_dn(void);
uint8 i8251s(t_bool io, uint8 data);
uint8 i8251d(t_bool io, uint8 data);

/* globals */

int32 i8272_devnum = 0;             //initially, no 8251 instances
uint16 i8272_port[4];               //base port assigned to each 8251 instance

/* i8251 Standard I/O Data Structures */
/* up to 1 i8251 devices */

UNIT i8272_unit[4] = { 
    { UDATA (&i8272_svc, 0, 0), KBD_POLL_WAIT },
    { UDATA (&i8272_svc, 0, 0), KBD_POLL_WAIT },
    { UDATA (&i8272_svc, 0, 0), KBD_POLL_WAIT },
    { UDATA (&i8272_svc, 0, 0), KBD_POLL_WAIT }
};

REG i8272_reg[4] = {
    { HRDATA (DATA, i8272_unit[0].buf, 8) },
    { HRDATA (STAT, i8272_unit[0].u3, 8) },
    { HRDATA (MODE, i8272_unit[0].u4, 8) },
    { HRDATA (CMD, i8272_unit[0].u5, 8) }
};

DEBTAB i8272_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "XACK", DEBUG_xack },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

MTAB i8272_mod[] = {
    { UNIT_ANSI, 0, "TTY", "TTY", NULL },
    { UNIT_ANSI, UNIT_ANSI, "ANSI", "ANSI", NULL },
    { 0 }
};

/* address width is set to 16 bits to use devices in 8086/8088 implementations */

DEVICE i8272_dev = {
    "I8272",             //name
    i8272_unit,        //units
    i8272_reg,          //registers
    i8272_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix
    16,                  //awidth
    1,                  //aincr
    16,                 //dradix
    8,                  //dwidth
    NULL,               //examine
    NULL,               //deposit
//    &i8272_reset,       //reset
    NULL,       //reset
    NULL,               //boot
    NULL,               //attach
    NULL,               //detach
    NULL,               //ctxt
    0,                  //flags
    0,                  //dctrl
    i8272_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

/* Service routines to handle simulator functions */

/* i8272_svc - actually gets char & places in buffer */

t_stat i8272_svc(UNIT *uptr)
{
    int32 temp;

    sim_activate(&i8272_unit[uptr->u6], i8272_unit[uptr->u6].wait); /* continue poll */
    if (uptr->u6 >= i8272_devnum) return SCPE_OK;
    if ((temp = sim_poll_kbd()) < SCPE_KFLAG)
        return temp;                    /* no char or error? */
    //sim_printf("i8272_svc: received character temp=%04X devnum=%d\n", temp, uptr->u6);
    i8272_unit[uptr->u6].buf = temp & 0xFF; /* Save char */
    i8272_unit[uptr->u6].u3 |= RXR;     /* Set status */
    return SCPE_OK;
}

/* Reset routine */ 

t_stat i8272_reset(DEVICE *dptr, uint16 base)
{
    if (i8272_devnum >= i8272_NUM) {
        sim_printf("8251_reset: too many devices!\n");
        return 0;
    }
    i8272_reset1(i8272_devnum);
    sim_printf("   8251-%d: Registered at %03X\n", i8272_devnum, base);
    i8272_port[i8272_devnum] = reg_dev(i8251d, base); 
    reg_dev(i8251s, base + 1); 
    i8272_unit[i8272_devnum].u6 = i8272_devnum;
    sim_activate(&i8272_unit[i8272_devnum], i8272_unit[i8272_devnum].wait); /* activate unit */
    i8272_devnum++;
    return SCPE_OK;
}

void i8272_reset1(uint8 devnum)
{
    i8272_unit[devnum].u3 = TXR + TXE;          /* status */
    i8272_unit[devnum].u4 = 0;                  /* mode instruction */
    i8272_unit[devnum].u5 = 0;                  /* command instruction */
    i8272_unit[devnum].buf = 0;
    i8272_unit[devnum].pos = 0;
    sim_printf("   8251-%d: Reset\n", devnum);
}

uint8 i8272_get_dn(void)
{
    int i;

    for (i=0; i<i8272_NUM; i++)
        if (port >=i8272_port[i] && port <= i8272_port[i] + 1)
            return i;
    sim_printf("i8272_get_dn: port %03X not in 8251 device table\n", port);
    return 0xFF;
}

/*  I/O instruction handlers, called from the CPU module when an
    IN or OUT instruction is issued.
*/

uint8 i8251s(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8272_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read status port */
            return i8272_unit[devnum].u3;
        } else {                            /* write status port */
            if (i8272_unit[devnum].u6) {    /* if mode, set cmd */
                i8272_unit[devnum].u5 = data;
                sim_printf("   8251-%d: Command Instruction=%02X\n", devnum, data);
                if (data & SD)              /* reset port! */
                    i8272_reset1(devnum);
            } else {                        /* set mode */
                i8272_unit[devnum].u4 = data;
                sim_printf("   8251-%d: Mode Instruction=%02X\n", devnum, data);
                i8272_unit[devnum].u6 = 1;  /* set cmd received */
            }
        }
    }
    return 0;
}

uint8 i8251d(t_bool io, uint8 data)
{
    uint8 devnum;

    if ((devnum = i8272_get_dn()) != 0xFF) {
        if (io == 0) {                      /* read data port */
            i8272_unit[devnum].u3 &= ~RXR;
            return (i8272_unit[devnum].buf);
        } else {                            /* write data port */
            sim_putchar(data);
        }
    }
    return 0;
}

/* end of i8251.c */
