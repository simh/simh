/* mp-8m.c: SWTP 8K Byte Memory Card emulator

    Copyright (c) 2011-2012, William A. Beech

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

        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        These functions support 6 simulated MP-8M memory cards on an SS-50 system.

        Each unit uses a dynamically allocated 8192 byte buffer to hold the data.
        Each unit contains the base address in mp_8m_unit.u3.  The unit capacity is
        held in mp_8m_unit.capac.  Each unit can be enabled or disabled to reconfigure
        the RAM for the system.
*/

#include <stdio.h>
#include "swtp_defs.h"

#define MP_8M_NUM       6               /* number of MP-*m boards */

/* prototypes */

t_stat mp_8m_reset (DEVICE *dptr);
int32 mp_8m_get_mbyte(int32 addr);
int32 mp_8m_get_mword(int32 addr);
void mp_8m_put_mbyte(int32 addr, int32 val);
void mp_8m_put_mword(int32 addr, int32 val);

/* isbc064 Standard I/O Data Structures */

UNIT mp_8m_unit[] = { 
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 },
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 },
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 },
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 },
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 },
    { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_DISABLE, 0),0 }
};

MTAB mp_8m_mod[] = { 
    { 0 }
};

DEBTAB mp_8m_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE mp_8m_dev = {
    "MP-8M",                            //name
    mp_8m_unit,                         //units
    NULL,                               //registers
    mp_8m_mod,                          //modifiers
    MP_8M_NUM,                          //numunits
    16,                                 //aradix
    8,                                  //awidth
    1,                                  //aincr
    16,                                 //dradix
    8,                                  //dwidth
    NULL,                               //examine
    NULL,                               //deposite
    &mp_8m_reset,                       //reset
    NULL,                               //boot
    NULL,                               //attach
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DEBUG,                          //flags
    0,                                  //dctrl
    mp_8m_debug,                        //debflags
    NULL,                               //msize
    NULL                                //lname
};

/* Reset routine */

t_stat mp_8m_reset (DEVICE *dptr)
{
    int32 i, j, val;
    UNIT *uptr;

    sim_debug (DEBUG_flow, &mp_8m_dev, "mp_8m_reset: \n");
    for (i = 0; i < MP_8M_NUM; i++) {   /* init all units */
        uptr = mp_8m_dev.units + i;
        sim_debug (DEBUG_flow, &mp_8m_dev, "MP-8M %d unit.flags=%08X\n",
            i, uptr->flags);
        uptr->capac = 0x2000;
        if (i < 4)
            uptr->u3 = 0x2000 * i;
        else
            uptr->u3 = 0x2000 * (i + 1);
        if (uptr->filebuf == NULL) {
            uptr->filebuf = malloc(0x2000);
            if (uptr->filebuf == NULL) {
                printf("mp_8m_reset: Malloc error\n");
                return SCPE_MEM;
            }
            for (j=0; j<8192; j++) {    /* fill pattern for testing */
                val = (0xA0 |  i);
                *((uint8 *)(uptr->filebuf) + j) = val & 0xFF;
            }
        }
        sim_debug (DEBUG_flow, &mp_8m_dev, "MP-8M %d initialized at [%04X-%04XH]\n",
            i, uptr->u3, uptr->u3 + uptr->capac - 1);
    }
    sim_debug (DEBUG_flow, &mp_8m_dev, "mp_8m_reset: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the mp-b2 module when an
    external memory read or write is issued.
*/

/*  get a byte from memory */

int32 mp_8m_get_mbyte(int32 addr)
{
    int32 val, org, len;
    int32 i;
    UNIT *uptr;

    sim_debug (DEBUG_read, &mp_8m_dev, "mp_8m_get_mbyte: addr=%04X", addr);
    for (i = 0; i < MP_8M_NUM; i++) { /* find addressed unit */
        uptr = mp_8m_dev.units + i;
        org = uptr->u3;
        len = uptr->capac - 1;
        if ((addr >= org) && (addr <= org + len)) {
            val = *((uint8 *)(uptr->filebuf) + (addr - org));
            sim_debug (DEBUG_read, &mp_8m_dev, " val=%04X\n", val);
            return (val & 0xFF);
        }
    }
    sim_debug (DEBUG_read, &mp_8m_dev, "mp_8m_get_mbyte: Out of range\n");
    return 0xFF;        /* multibus has active high pullups */
}

/*  get a word from memory */

int32 mp_8m_get_mword(int32 addr)
{
    int32 val;

    val = (mp_8m_get_mbyte(addr) << 8);
    val |= mp_8m_get_mbyte(addr+1);
    return val;
}

/*  put a byte into memory */

void mp_8m_put_mbyte(int32 addr, int32 val)
{
    int32 org, len;
    int32 i;
    UNIT *uptr;

    sim_debug (DEBUG_write, &mp_8m_dev, "mp_8m_put_mbyte: addr=%04X, val=%02X",
        addr, val);
    for (i = 0; i < MP_8M_NUM; i++) { /* find addressed unit */
        uptr = mp_8m_dev.units + i;
        org = uptr->u3;
        len = uptr->capac - 1;
        if ((addr >= org) && (addr < org + len)) {
            *((uint8 *)(uptr->filebuf) + (addr - org)) = val & 0xFF;
            sim_debug (DEBUG_write, &mp_8m_dev, "\n");
            return;
        }
    }
    sim_debug (DEBUG_write, &mp_8m_dev, "mp_8m_put_mbyte: Out of range\n");
}

/*  put a word into memory */

void mp_8m_put_mword(int32 addr, int32 val)
{
    mp_8m_put_mbyte(addr, val >> 8);
    mp_8m_put_mbyte(addr+1, val);
}

/* end of mp-8m.c */
