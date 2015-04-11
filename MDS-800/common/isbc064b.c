/* isbc064.c: Intel iSBC064 64K Byte Memory Card

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
   WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A. Beech shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A. Beech.

    These functions support a simulated isbc016, isbc032, isbc048 and isbc064 memory card
    on an Intel multibus system.  

    */

#include <stdio.h>

#include "multibus_defs.h"

#define UNIT_V_MSIZE    (UNIT_V_UF)                   /* Memory Size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define UNIT_V_MBASE    (UNIT_V_UF+1)                 /* Memory Base */
#define UNIT_MBASE      (1 << UNIT_V_MBASE)

/* prototypes */

t_stat isbc064_reset (DEVICE *dptr);
t_stat isbc064_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat isbc064_set_base (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 isbc064_get_mbyte(int32 addr);
int32 isbc064_get_mword(int32 addr);
void isbc064_put_mbyte(int32 addr, int32 val);
void isbc064_put_mword(int32 addr, int32 val);

/* isbc064 Standard I/O Data Structures */

UNIT isbc064_unit = { UDATA (NULL, UNIT_FIX+UNIT_DISABLE+UNIT_BINK, 65536), KBD_POLL_WAIT };

MTAB isbc064_mod[] = {
    { UNIT_MSIZE, 16384, NULL, "16K", &isbc064_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &isbc064_set_size },
    { UNIT_MSIZE, 49152, NULL, "48K", &isbc064_set_size },
    { UNIT_MSIZE, 65535, NULL, "64K", &isbc064_set_size },
    { UNIT_MBASE, 0, NULL, "B0K", &isbc064_set_base },
    { UNIT_MBASE, 16384, NULL, "B16K", &isbc064_set_base },
    { UNIT_MBASE, 32768, NULL, "B32K", &isbc064_set_base },
    { UNIT_MBASE, 49152, NULL, "B48K", &isbc064_set_base },
    { 0 }
};

DEBTAB isbc064_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE isbc064_dev = {
    "SBC064",           //name
    &isbc064_unit,      //units
    NULL,               //registers 
    isbc064_mod,        //modifiers
    1,                  //numunits 
    16,                 //aradix 
    8,                  //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth 
    NULL,               //examine
    NULL,               //deposite
    &isbc064_reset,     //reset
    NULL,               //boot 
    NULL,               //attach 
    NULL,               //detach 
    NULL,               //ctxt 
    DEV_DEBUG+DEV_DISABLE+DEV_DIS, //flags
    0,                  //dctrl 
    isbc064_debug,      //debflags
    NULL,               //msize
    NULL                //lname
};

/* iSBC064 globals */

uint8 *MB_buf = NULL;   //pointer to memory buffer

/* Set memory size routine */

t_stat isbc064_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    int32 mc = 0;
    uint32 i;

    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_set_size: val=%04X\n", val);
    if ((val <= 0) || (val > MAXMEMSIZE)) {
        if (isbc064_dev.dctrl & DEBUG_flow)
            printf("isbc064_set_size: Memory size error\n");
        return SCPE_ARG;
    }
    isbc064_unit.capac = val;
    for (i = isbc064_unit.capac; i < MAXMEMSIZE; i++)
        isbc064_put_mbyte(i, 0);
    isbc064_unit.capac = val;
    isbc064_unit.u3 = 0;
    if (MB_buf) {
        free (MB_buf);
        MB_buf = NULL;
    }
    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_set_size: Done\n");
    return SCPE_OK;
}

/* Set memory base address routine */

t_stat isbc064_set_base (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_set_base: val=%04X\n", val);
    if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0)) {
        if (isbc064_dev.dctrl & DEBUG_flow)
            printf("isbc064_set_base: Base address error\n");
        return SCPE_ARG;
    }
    isbc064_unit.u3 = val;
    if (MB_buf) {
        free (MB_buf);
        MB_buf = NULL;
    }
    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_set_base: Done\n");
    return (isbc064_reset (NULL));
}

/* Reset routine */

t_stat isbc064_reset (DEVICE *dptr)
{
    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_reset: \n");
    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        printf("Initializing %s [%04X-%04XH]\n", "iSBC-064", 
            isbc064_unit.u3,
            isbc064_unit.u3 + isbc064_unit.capac - 1);
        if (MB_buf == NULL) {
            MB_buf = malloc(isbc064_unit.capac);
            if (MB_buf == NULL) {
                if (isbc064_dev.dctrl & DEBUG_flow)
                    printf("isbc064_reset: Malloc error\n");
                return SCPE_MEM;
            }
        }
    }
    if (isbc064_dev.dctrl & DEBUG_flow)
        printf("isbc064_reset: Done\n");
    return SCPE_OK;
}

/*  I/O instruction handlers, called from the CPU module when an
    external memory read or write is issued.
*/

/*  get a byte from memory */

int32 isbc064_get_mbyte(int32 addr)
{
    int32 val, org, len;
    int i = 0;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac - 1;
        if (isbc064_dev.dctrl & DEBUG_read)
            printf("isbc064_get_mbyte: addr=%04X", addr);
        if ((addr >= org) && (addr <= org + len)) {
            val = *(MB_buf + (addr - org));
            if (isbc064_dev.dctrl & DEBUG_read)
                printf(" val=%04X\n", val);
            return (val & 0xFF);
        } else {
            if (isbc064_dev.dctrl & DEBUG_read)
                printf(" Out of range\n");
            return 0xFF;    /* multibus has active high pullups */
        }
    }
    if (isbc064_dev.dctrl & DEBUG_read)
        printf(" Disabled\n");
    return 0xFF;        /* multibus has active high pullups */
}

/*  get a word from memory */

int32 isbc064_get_mword(int32 addr)
{
    int32 val;

    val = isbc064_get_mbyte(addr);
    val |= (isbc064_get_mbyte(addr+1) << 8);
    return val;
}

/*  put a byte into memory */

void isbc064_put_mbyte(int32 addr, int32 val)
{
    int32 org, len, type;
    int i = 0;

    if ((isbc064_dev.flags & DEV_DIS) == 0) {
        org = isbc064_unit.u3;
        len = isbc064_unit.capac - 1;
        if (isbc064_dev.dctrl & DEBUG_write)
            printf("isbc064_put_mbyte: addr=%04X, val=%02X", addr, val);
        if ((addr >= org) && (addr < org + len)) {
            *(MB_buf + (addr - org)) = val & 0xFF;
            if (isbc064_dev.dctrl & DEBUG_write)
                printf("\n");
            return;
        } else {
            if (isbc064_dev.dctrl & DEBUG_write)
                printf(" Out of range\n");
            return;
        }
    }
    if (isbc064_dev.dctrl & DEBUG_write)
        printf("isbc064_put_mbyte: Disabled\n");
}

/*  put a word into memory */

void isbc064_put_mword(int32 addr, int32 val)
{
    isbc064_put_mbyte(addr, val);
    isbc064_put_mbyte(addr+1, val << 8);
}

/* end of isbc064.c */
