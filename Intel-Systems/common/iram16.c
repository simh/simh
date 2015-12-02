/*  iram.c: Intel RAM simulator for 16-bit SBCs

    Copyright (c) 2011, William A. Beech

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
    William A. Beech BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of William A. Beech shall not be
    used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from William A. Beech.

    These functions support a simulated RAM device in low memory on an iSBC.  These
    SBCs do not have the capability to switch off this RAM.  In most cases a portion 
    of the RAM is dual-ported so it also appears in the multibus memory map at
    a configurable location.

    Unit will support 16K SRAM sizes.

*/

#include <stdio.h>

#include "multibus_defs.h"

#define UNIT_V_RSIZE    (UNIT_V_UF)                     /* RAM Size */
#define UNIT_RSIZE      (0x1 << UNIT_V_RSIZE)
#define UNIT_NONE               (0)                     /* No unit */
#define UNIT_16K                (1)                     /* 16KB */

/* function prototypes */

t_stat RAM_svc (UNIT *uptr);
t_stat RAM_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat RAM_reset (DEVICE *dptr);
int32 RAM_get_mbyte(int32 addr);
void RAM_put_mbyte(int32 addr, int32 val);

/* SIMH RAM Standard I/O Data Structures */

UNIT RAM_unit = { UDATA (NULL, UNIT_BINK, 0), KBD_POLL_WAIT };

MTAB RAM_mod[] = {
    { UNIT_RSIZE, UNIT_NONE, "None", "none", &RAM_set_size },
    { UNIT_RSIZE, UNIT_16K, "16KB", "16KB", &RAM_set_size },
    { 0 }
};

DEBTAB RAM_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE RAM_dev = {
    "RAM",              //name
    &RAM_unit,          //units 
    NULL,               //registers 
    RAM_mod,            //modifiers
    1,                  //numunits
    16,                 //aradix 
    32,                 //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth
    NULL,               //examine 
    NULL,               //deposit 
    &RAM_reset,         //reset
    NULL,               //boot
    NULL,               //attach 
    NULL,               //detach
    NULL,               //ctxt                
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    RAM_debug,          //debflags
    NULL,               //msize
    NULL                //lname
};

/* global variables */

uint8 *RAM_buf = NULL;                                  /* RAM buffer pointer */

/* RAM functions */

/* RAM set size = none or 16KB */

t_stat RAM_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    if (RAM_dev.dctrl & DEBUG_flow)
        sim_printf("RAM_set_size: val=%d\n", val);
    if ((val < UNIT_NONE) || (val > UNIT_16K)) {
        if (RAM_dev.dctrl & DEBUG_flow)
            sim_printf("RAM_set_size: Size error\n");
        return SCPE_ARG;
    }
    RAM_unit.capac = 0x4000 * val;                      /* set size */  
    RAM_unit.u3 = 0x0000;                               /* base is 0 */
    RAM_unit.u4 = val;                                  /* save val */
    if (RAM_buf) {                                      /* if changed, allocate new buffer */
        free (RAM_buf);
        RAM_buf = NULL;
    }
    if (RAM_dev.dctrl & DEBUG_flow)
        sim_printf("RAM_set_size: Done\n");
    return (RAM_reset (NULL));                          /* force reset after reconfig */
}

/* RAM reset */

t_stat RAM_reset (DEVICE *dptr)
{
    int j;
    FILE *fp;

    if (RAM_dev.dctrl & DEBUG_flow)
        sim_printf("RAM_reset: \n");
    if (RAM_unit.capac == 0) {                          /* if undefined */
        sim_printf("   RAM: defaulted for 16KB\n");
        sim_printf("      \"set RAM 16KB\"\n");
        RAM_unit.capac = 0x4000;
        RAM_unit.u3 = 0;
        RAM_unit.u4 = 1;
    }
    sim_printf("   RAM: Initializing [%04X-%04XH]\n", 
        RAM_unit.u3,
        RAM_unit.u3 + RAM_unit.capac - 1);
    if (RAM_buf == NULL) {                              /* no buffer allocated */
        RAM_buf = malloc(RAM_unit.capac);
        if (RAM_buf == NULL) { 
            if (RAM_dev.dctrl & DEBUG_flow)
                sim_printf("RAM_reset: Malloc error\n");
            return SCPE_MEM;
        }
    }
    if (RAM_dev.dctrl & DEBUG_flow)
        sim_printf("RAM_reset: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    RAM memory read or write is issued.
*/

/*  get a byte from memory */

int32 RAM_get_mbyte(int32 addr)
{
    int32 val, org, len;

    org = RAM_unit.u3;
    len = RAM_unit.capac - 1;
    if (RAM_dev.dctrl & DEBUG_read)
        sim_printf("RAM_get_mbyte: addr=%04X", addr);
    if ((addr >= org) && (addr <= org + len)) {
        val = *(RAM_buf + (addr - org));
        if (RAM_dev.dctrl & DEBUG_read)
            sim_printf(" val=%04X\n", val);
        return (val & 0xFF);
    }
    if (RAM_dev.dctrl & DEBUG_read)
        sim_printf(" Out of range\n", addr);
    return 0xFF;
}

/*  put a byte to memory */

void RAM_put_mbyte(int32 addr, int32 val)
{
    int32 org, len;

    org = RAM_unit.u3;
    len = RAM_unit.capac - 1;
    if (RAM_dev.dctrl & DEBUG_write)
        sim_printf("RAM_put_mbyte: addr=%04X, val=%02X", addr, val);
    if ((addr >= org) && (addr < org + len)) {
        *(RAM_buf + (addr - org)) = val & 0xFF;
        if (RAM_dev.dctrl & DEBUG_write)
            sim_printf("\n");
        return;
    }
    if (RAM_dev.dctrl & DEBUG_write)
        sim_printf(" Out of range\n", val);
}

/* end of iram.c */
