/*  mp-a2.c: SWTP MP-A2 M6800 CPU simulator

    Copyright (c) 2011-2012, William Beech

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

        Except as contained in this notice, the name of William A. Beech shall not
        be used in advertising or otherwise to promote the sale, use or other dealings
        in this Software without prior written authorization from William A. Beech.

    MODIFICATIONS:

        24 Apr 15 -- Modified to use simh_debug

    NOTES:

        The MP-A2 CPU Board contains the following devices [mp-a2.c]:
            M6800 processor [m6800.c].
            M6810 128 byte RAM at 0xA000 [m6810.c].
            M6830, SWTBUG, or custom boot ROM at 0xE000 [bootrom.c].
            4 ea 2716 EPROMs at either 0xC000, 0xC800, 0xD000 and 0xD800 (LO_PROM)or
                0xE000, 0xE800, 0xF000 and 0xF800 (HI_PROM) [eprom.c].
            Interface to the SS-50 bus and the MP-B2 Mother Board for I/O 
                and memory boards [mp-b2.c].
            Note: The file names of the emulator source programs for each device are
            contained in "[]".
*/

#include <stdio.h>
#include "swtp_defs.h"

#define UNIT_V_USER_D   (UNIT_V_UF)     /* user defined switch */
#define UNIT_USER_D     (1 << UNIT_V_USER_D)
#define UNIT_V_4K_8K    (UNIT_V_UF+1)   /* off if HI_PROM and only 2K EPROM */
#define UNIT_4K_8K      (1 << UNIT_V_4K_8K)
#define UNIT_V_SWT      (UNIT_V_UF+2)   /* on SWTBUG, off MIKBUG */
#define UNIT_SWT        (1 << UNIT_V_SWT)
#define UNIT_V_8K       (UNIT_V_UF+3)   /* off if HI_PROM and only 2K or 4k EPROM */
#define UNIT_8K         (1 << UNIT_V_8K)
#define UNIT_V_RAM      (UNIT_V_UF+4)   /* off disables 6810 RAM */
#define UNIT_RAM        (1 << UNIT_V_RAM)
#define UNIT_V_LO_PROM  (UNIT_V_UF+5)   /* on EPROMS @ C000-CFFFH, off no EPROMS */
#define UNIT_LO_PROM    (1 << UNIT_V_LO_PROM)
#define UNIT_V_HI_PROM  (UNIT_V_UF+6)   /* on EPROMS @ F000-FFFFH, off fo LO_PROM, MON, or no EPROMS */
#define UNIT_HI_PROM    (1 << UNIT_V_HI_PROM)
#define UNIT_V_MON      (UNIT_V_UF+7)   /* on for monitor vectors in high memory */
#define UNIT_MON        (1 << UNIT_V_MON)

/* local global variables */

/* function prototypes */

int32 get_base(void);
int32 CPU_BD_get_mbyte(int32 addr);
int32 CPU_BD_get_mword(int32 addr);
void CPU_BD_put_mbyte(int32 addr, int32 val);
void CPU_BD_put_mword(int32 addr, int32 val);

/* external routines */

/* MP-B2 bus routines */
extern int32 MB_get_mbyte(int32 addr);
extern int32 MB_get_mword(int32 addr);
extern void MB_put_mbyte(int32 addr, int32 val);
extern void MB_put_mword(int32 addr, int32 val);

/* M6810 bus routines */
extern int32 m6810_get_mbyte(int32 addr);
extern void m6810_put_mbyte(int32 addr, int32 val);

/* BOOTROM bus routines */
extern UNIT BOOTROM_unit;
extern int32 BOOTROM_get_mbyte(int32 offset);

/* I2716 bus routines */
extern int32 i2716_get_mbyte(int32 offset);

/* MP-A2 data structures

   CPU_BD_dev        MP-A2 device descriptor
   CPU_BD_unit       MP-A2 unit descriptor
   CPU_BD_reg        MP-A2 register list
   CPU_BD_mod        MP-A2 modifiers list */

UNIT CPU_BD_unit = { UDATA (NULL, 0, 0) };

REG CPU_BD_reg[] = {
    { NULL }
};

MTAB CPU_BD_mod[] = {
    { UNIT_USER_D, UNIT_USER_D, "USER_D", "USER_D", NULL },
    { UNIT_USER_D, 0, "NOUSER_D", "NOUSER_D", NULL },
    { UNIT_4K_8K, UNIT_4K_8K, "4K_8K", "4K_8K", NULL },
    { UNIT_4K_8K, 0, "NO4K_8K", "NO4K_8K", NULL },
    { UNIT_SWT, UNIT_SWT, "SWT", "SWT", NULL },
    { UNIT_SWT, 0, "NOSWT", "NOSWT", NULL },
    { UNIT_8K, UNIT_8K, "8K", "8K", NULL },
    { UNIT_8K, 0, "NO8K", "NO8K", NULL },
    { UNIT_RAM, UNIT_RAM, "RAM", "RAM", NULL },
    { UNIT_RAM, 0, "NORAM", "NORAM", NULL },
    { UNIT_LO_PROM, UNIT_LO_PROM, "LO_PROM", "LO_PROM", NULL },
    { UNIT_LO_PROM, 0, "NOLO_PROM", "NOLO_PROM", NULL },
    { UNIT_HI_PROM, UNIT_HI_PROM, "HI_PROM", "HI_PROM", NULL },
    { UNIT_HI_PROM, 0, "NOHI_PROM", "NOHI_PROM", NULL },
    { UNIT_MON, UNIT_MON, "MON", "MON", NULL },
    { UNIT_MON, 0, "NOMON", "NOMON", NULL },
    { 0 }
};

DEBTAB CPU_BD_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { NULL }
};

DEVICE CPU_BD_dev = {
    "MP-A2",                            //name
    &CPU_BD_unit,                        //units
    CPU_BD_reg,                          //registers
    CPU_BD_mod,                          //modifiers
    1,                                  //numunits
    16,                                 //aradix 
    16,                                 //awidth 
    1,                                  //aincr 
    16,                                 //dradix 
    8,                                  //dwidth
    NULL,                               //examine 
    NULL,                               //deposit 
    NULL,                               //reset
    NULL,                               //boot
    NULL,                               //attach 
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DEBUG,                          //flags 
    0,                                  //dctrl 
    CPU_BD_debug,                       /* debflags */
    NULL,                               //msize
    NULL                                //lname
};

/*  get base address of 2716's */

int32 get_base(void)
{
    if (CPU_BD_unit.flags & UNIT_LO_PROM)
        return 0xC000;
    else if (CPU_BD_unit.flags & UNIT_HI_PROM)
        return 0xF000;
    return 0;
}

/*  get a byte from memory */

int32 CPU_BD_get_mbyte(int32 addr)
{
    int32 val;

    sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: addr=%04X\n", addr);
    switch(addr & 0xF000) {
        case 0xA000:
            if (CPU_BD_unit.flags & UNIT_RAM) {
                val = m6810_get_mbyte(addr - 0xA000) & 0xFF;
                sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: m6810 val=%02X\n", val);
                return val;
            } else {
                val = MB_get_mbyte(addr) & 0xFF;
                sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: m6810 val=%02X\n", val);
                return val;
            }
        case 0xC000:
            if (CPU_BD_unit.flags & UNIT_LO_PROM) {
                val = i2716_get_mbyte(addr - 0xC000) & 0xFF;
                sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: 2716=%02X\n", val);
                return val;
            } else
                return 0xFF;
            break;
        case 0xE000:
            val = BOOTROM_get_mbyte(addr - 0xE000) & 0xFF;
            sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: EPROM=%02X\n", val);
            return val;
        case 0xF000:
            if (CPU_BD_unit.flags & UNIT_MON) {
                val = BOOTROM_get_mbyte(addr - (0x10000 - BOOTROM_unit.capac)) & 0xFF;
                sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: EPROM=%02X\n", val);
                return val;
            }
        default:
            val = MB_get_mbyte(addr) & 0xFF;
            sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mbyte: mp_b2 val=%02X\n", val);
            return val;
    }
}

/*  get a word from memory */

int32 CPU_BD_get_mword(int32 addr)
{
    int32 val;

    sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mword: addr=%04X\n", addr);
    val = (CPU_BD_get_mbyte(addr) << 8);
    val |= CPU_BD_get_mbyte(addr+1);
    val &= 0xFFFF;
    sim_debug (DEBUG_read, &CPU_BD_dev, "CPU_BD_get_mword: val=%04X\n", val);
    return val;
}

/*  put a byte to memory */

void CPU_BD_put_mbyte(int32 addr, int32 val)
{
    sim_debug (DEBUG_write, &CPU_BD_dev, "CPU_BD_put_mbyte: addr=%04X, val=%02X\n",
        addr, val);
    switch(addr & 0xF000) {
        case 0xA000:
            if (CPU_BD_unit.flags & UNIT_RAM) {
                m6810_put_mbyte(addr - 0xA000, val);
                return;
            } else {
                MB_put_mbyte(addr, val);
                return;
            }
        default:
            MB_put_mbyte(addr, val);
            return;
    }
}

/*  put a word to memory */

void CPU_BD_put_mword(int32 addr, int32 val)
{
    sim_debug (DEBUG_write, &CPU_BD_dev, "CPU_BD_put_mword: addr=%04X, val=%04X\n",
        addr, val);
    CPU_BD_put_mbyte(addr, val >> 8);
    CPU_BD_put_mbyte(addr+1, val);
}

/* end of mp-a2.c */
