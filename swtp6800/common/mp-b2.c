/*  mp-b2.c: SWTP SS-50/SS-30 MP-B2 Mother Board

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

        28 May 22 -- Roberto Sancho Villa (RSV) fixes for other disk controllers

    NOTES:

*/

#include <stdio.h>
#include "swtp_defs.h"

#define UNIT_V_RAM_0000   (UNIT_V_UF)   /* MP-8M board 0 enable */
#define UNIT_RAM_0000     (1 << UNIT_V_RAM_0000)
#define UNIT_V_RAM_2000   (UNIT_V_UF+1) /* MP-8M board 1 enable */
#define UNIT_RAM_2000     (1 << UNIT_V_RAM_2000)
#define UNIT_V_RAM_4000   (UNIT_V_UF+2) /* MP-8M board 2 enable */
#define UNIT_RAM_4000     (1 << UNIT_V_RAM_4000)
#define UNIT_V_RAM_6000   (UNIT_V_UF+3) /* MP-8M board 3 enable */
#define UNIT_RAM_6000     (1 << UNIT_V_RAM_6000)
#define UNIT_V_RAM_A000   (UNIT_V_UF+4) /* MP-8M board 4 enable */
#define UNIT_RAM_A000     (1 << UNIT_V_RAM_A000)
#define UNIT_V_RAM_C000   (UNIT_V_UF+5) /* MP-8M board 5 enable */
#define UNIT_RAM_C000     (1 << UNIT_V_RAM_C000)

/* function prototypes */

// CPU Boards
extern int32 CPU_BD_get_mbyte(int32 addr);
extern int32 CPU_BD_get_mword(int32 addr);
extern void CPU_BD_put_mbyte(int32 addr, int32 val);
extern void CPU_BD_put_mword(int32 addr, int32 val);

/* empty I/O device routine */
int32 nulldev(int32 io, int32 data);

/* SS-50 MB routines */
int32 MB_get_mbyte(int32 addr);
int32 MB_get_mword(int32 addr);
void MB_put_mbyte(int32 addr, int32 val);
void MB_put_mword(int32 addr, int32 val);

/* MP-8M bus routines */
extern int32 mp_8m_get_mbyte(int32 addr);
extern void mp_8m_put_mbyte(int32 addr, int32 val);

/* SS-50 I/O address space functions */

/* MP-S serial I/O routines */
extern int32 sio0s(int32 io, int32 data);
extern int32 sio0d(int32 io, int32 data);
extern int32 sio1s(int32 io, int32 data);
extern int32 sio1d(int32 io, int32 data);

/* DC-4 FDC I/O routines */
extern int32 fdcdrv(int32 io, int32 data);
extern int32 fdccmd(int32 io, int32 data);
extern int32 fdctrk(int32 io, int32 data);
extern int32 fdcsec(int32 io, int32 data);
extern int32 fdcdata(int32 io, int32 data);

/* LFD-400 FDC I/O routines */

extern int32 fd400_fdcstatus(int32 io, int32 data);
extern int32 fd400_cstatus(int32 io, int32 data);
extern int32 fd400_data(int32 io, int32 data);
extern int32 fd400_cursect(int32 io, int32 data);
extern int32 fd400_startrw(int32 io, int32 data);

extern DEVICE fd400_dsk_dev;

/* This is the I/O configuration table.  There are 32 possible
device addresses, if a device is plugged into a port it's routine
address is here, 'nulldev' means no device is available
*/

struct idev {
        int32 (*routine)(int32, int32);
};

struct idev dev_table[32] = {
        {&nulldev},     {&nulldev},     {&nulldev},     {&nulldev}, /*Port 0 8000-8003 */
        {&sio0s},       {&sio0d},       {&sio1s},       {&sio1d},   /*Port 1 8004-8007 */
/* sio1x routines just return the last value read on the matching
   sio0x routine.  SWTBUG tests for the MP-C with most port reads! */
        {&nulldev},     {&nulldev},     {&nulldev},     {&nulldev},     /*Port 2 8008-800B*/
        {&nulldev},     {&nulldev},     {&nulldev},     {&nulldev},     /*Port 3 800C-800F*/
        {&nulldev},     {&nulldev},     {&nulldev},     {&nulldev},     /*Port 4 8010-8013*/
        {&fdcdrv},      {&nulldev},     {&nulldev},     {&nulldev},     /*Port 5 8014-8017*/
        {&fdccmd},      {&fdctrk},      {&fdcsec},      {&fdcdata},     /*Port 6 8018-801B*/
        {&nulldev},     {&nulldev},     {&nulldev},     {&nulldev}      /*Port 7 801C-801F*/
};

/* RSV - LFD-400 routines */
struct idev dev_table2[8] = {
        {&fd400_cstatus}, {&fd400_data}, {&fd400_cursect}, {&fd400_fdcstatus}, /* Port CC00-CC03 */ 
        {&fd400_startrw}, {&nulldev},    {&nulldev},       {&nulldev}   /* Port CC04-CC07 */
};

/* dummy i/o device */

int32 nulldev(int32 io, int32 data)
{
    if (io == 0)
        return (0xFF);
    return 0;
}

/* Mother Board data structures

    MB_dev        Mother Board device descriptor
    MB_unit       Mother Board unit descriptor
    MB_reg        Mother Board register list
    MB_mod        Mother Board modifiers list
*/

UNIT MB_unit = { 
    UDATA (NULL, 0, 0)
};

REG MB_reg[] = {
    { NULL }
};

MTAB MB_mod[] = {
    { UNIT_RAM_0000, UNIT_RAM_0000, "BD0 On", "BD0", NULL },
    { UNIT_RAM_0000, 0, "BD0 Off", "NOBD0", NULL },
    { UNIT_RAM_2000, UNIT_RAM_2000, "BD1 On", "BD1", NULL },
    { UNIT_RAM_2000, 0, "BD1 Off", "NOBD1", NULL },
    { UNIT_RAM_4000, UNIT_RAM_4000, "BD2 On", "BD2", NULL },
    { UNIT_RAM_4000, 0, "BD2 Off", "NOBD2", NULL },
    { UNIT_RAM_6000, UNIT_RAM_6000, "BD3 On", "BD3", NULL },
    { UNIT_RAM_6000, 0, "BD3 Off", "NOBD3", NULL },
    { UNIT_RAM_A000, UNIT_RAM_A000, "BD4 On", "BD4", NULL },
    { UNIT_RAM_A000, 0, "BD4 Off", "NOBD4", NULL },
    { UNIT_RAM_C000, UNIT_RAM_C000, "BD5 On", "BD5", NULL },
    { UNIT_RAM_C000, 0, "BD5 Off", "NOBD5", NULL },
    { 0 }
};

DEBTAB MB_debug[] = {
    { "ALL", DEBUG_all, "All debug bits" },
    { "FLOW", DEBUG_flow, "Flow control" },
    { "READ", DEBUG_read, "Read Command" },
    { "WRITE", DEBUG_write, "Write Command"},
    { NULL }
};

DEVICE MB_dev = {
    "MP-B2",                            //name
    &MB_unit,                           //units
    MB_reg,                             //registers
    MB_mod,                             //modifiers
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
    MB_debug,                           /* debflags */
    NULL,                               //msize
    NULL                                //lname
};

/*  get a byte from memory */

int32 MB_get_mbyte(int32 addr)
{
    int32 val;

    switch(addr & 0xE000) {
        case 0x0000:                    //0000-1FFFh
            if (MB_unit.flags & UNIT_RAM_0000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        case 0x2000:                    //2000-3FFFh
            if (MB_unit.flags & UNIT_RAM_2000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        case 0x4000:                    //4000-5FFFh
            if (MB_unit.flags & UNIT_RAM_4000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        case 0x6000:                    //6000-7FFFh
            if (MB_unit.flags & UNIT_RAM_6000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        case 0x8000:                    //8000-9FFFh (I/O ports)
            if (addr < 0x8020) {
                val = (dev_table[addr - 0x8000].routine(0, 0)) & BYTEMASK;
            } else
                val = 0xFF;
            break;
        case 0xA000:                    //A000-AFFFh
            if (MB_unit.flags & UNIT_RAM_A000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        case 0xC000:                    //C000-CFFFh
            if (((fd400_dsk_dev.flags & DEV_DIS) == 0) && ((addr & 0xFFF0) == 0xCC00)) //RSV
                val = (dev_table2[addr - 0xCC00].routine(0, 0));
            else if (MB_unit.flags & UNIT_RAM_C000)
                val = mp_8m_get_mbyte(addr) & BYTEMASK;
            else
                val = 0xFF;
            break;
        default:
            val = 0xFF;
    }
    return val;
}

/*  get a word from memory */

int32 MB_get_mword(int32 addr)
{
    int32 val;

    val = (MB_get_mbyte(addr) << 8);
    val |= MB_get_mbyte(addr+1);
    val &= 0xFFFF;
    return val;
}

/*  put a byte to memory */

void MB_put_mbyte(int32 addr, int32 val)
{
    switch(addr & 0xE000) {
        case 0x0000:                    //0000-1FFFh
            if (MB_unit.flags & UNIT_RAM_0000)
                mp_8m_put_mbyte(addr, val);
            break;
        case 0x2000:                    //2000-3FFFh
            if (MB_unit.flags & UNIT_RAM_2000)
                mp_8m_put_mbyte(addr, val);
            break;
        case 0x4000:                    //4000-5FFFh
            if (MB_unit.flags & UNIT_RAM_4000)
                mp_8m_put_mbyte(addr, val);
            break;
        case 0x6000:                    //6000-7FFFh
            if (MB_unit.flags & UNIT_RAM_6000)
                mp_8m_put_mbyte(addr, val);
            break;
        case 0x8000:                    //8000-9FFFh (I/O ports)
            if (addr < 0x8020)
                dev_table[addr - 0x8000].routine(1, val);
            break;
        case 0xA000:                    //A000-AFFFh
            if (MB_unit.flags & UNIT_RAM_A000)
                mp_8m_put_mbyte(addr, val);
            break;
        case 0xC000:                    //C000-CFFFh
            if (((fd400_dsk_dev.flags & DEV_DIS) == 0) && ((addr & 0xFFF0) == 0xCC00)) //RSV
                dev_table2[addr - 0xCC00].routine(1, val);
            else if (MB_unit.flags & UNIT_RAM_C000)
                mp_8m_put_mbyte(addr, val);
            break;
        default:
            ;
    }
}

/*  put a word to memory */

void MB_put_mword(int32 addr, int32 val)
{
    MB_put_mbyte(addr, val >> 8);
    MB_put_mbyte(addr+1, val);
}

/* end of mp-b2.c */

