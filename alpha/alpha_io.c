/* alpha_io.c: Alpha I/O and miscellaneous devices

   Copyright (c) 2006, Robert M. Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   rom          boot ROM
*/

#include "alpha_defs.h"
#include "alpha_sys_defs.h"

t_uint64 *rom = NULL;                                   /* boot ROM */

t_bool rom_rd (t_uint64 pa, t_uint64 *val, uint32 lnt);
t_bool rom_wr (t_uint64 pa, t_uint64 val, uint32 lnt);
t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_reset (DEVICE *dptr);

/* ROM data structures

   rom_dev      ROM device descriptor
   rom_unit     ROM units
   rom_reg      ROM register list
*/

DIB rom_dib = {
    ROMBASE, ROMBASE + ROMSIZE, &rom_rd, &rom_wr, 0
    };

UNIT rom_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_BINK, ROMSIZE)
    };

REG rom_reg[] = {
    { NULL }
    };

DEVICE rom_dev = {
    "ROM", &rom_unit, rom_reg, NULL,
    1, 16, 24, 8, 16, 64,
    &rom_ex, &rom_dep, &rom_reset,
    NULL, NULL, NULL,
    &rom_dib, DEV_DIB
    };

/* ReadIO - read IO space

   Inputs:
        pa      =       physical address
        *dat    =       pointer to data
        lnt     =       length (BWLQ)
   Output:
        TRUE if read succeeds, else FALSE
*/

t_bool ReadIO (t_uint64 pa, t_uint64 *dat, uint32 lnt)
{
DEVICE *dptr;
uint32 i;

for (i = 0; sim_devices[i] != NULL; i++) {
    dptr = sim_devices[i];
    if (dptr->flags & DEV_DIB) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if ((pa >= dibp->low) && (pa < dibp->high))
            return dibp->read (pa, dat, lnt);
        }
    }
return FALSE;
}

/* WriteIO - write register space

   Inputs:
        ctx     =       CPU context
        pa      =       physical address
        val     =       data to write, right justified in 64b quadword
        lnt     =       length (BWLQ)
   Output:
        TRUE if write succeeds, else FALSE
*/

t_bool WriteIO (t_uint64 pa, t_uint64 dat, uint32 lnt)
{
DEVICE *dptr;
uint32 i;

for (i = 0; sim_devices[i] != NULL; i++) {
    dptr = sim_devices[i];
    if (dptr->flags & DEV_DIB) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if ((pa >= dibp->low) && (pa < dibp->high))
            return dibp->write (pa, dat, lnt);
        }
    }
return FALSE;
}

/* Boot ROM read */

t_bool rom_rd (t_uint64 pa, t_uint64 *val, uint32 lnt)
{
uint32 sc, rg = ((uint32) ((pa - ROMBASE) & (ROMSIZE - 1))) >> 3;

switch (lnt) {

    case L_BYTE:
        sc = (((uint32) pa) & 7) * 8;
        *val = (rom[rg] >> sc) & M8;
        break;

    case L_WORD:
        sc = (((uint32) pa) & 6) * 8;
        *val = (rom[rg] >> sc) & M16;
        break;

    case L_LONG:
        if (pa & 4) *val = (rom[rg] >> 32) & M32;
        else *val = rom[rg] & M32;
        break;

    case L_QUAD:
        *val = rom[rg];
        break;
        }

return TRUE;
}

/* Boot ROM write */

t_bool rom_wr (t_uint64 pa, t_uint64 val, uint32 lnt)
{
uint32 sc, rg = ((uint32) ((pa - ROMBASE) & (ROMSIZE - 1))) >> 3;

switch (lnt) {

    case L_BYTE:
        sc = (((uint32) pa) & 7) * 8;
        rom[rg] = (rom[rg] & ~(((t_uint64) M8) << sc)) | (((t_uint64) (val & M8)) << sc);
        break;

    case L_WORD:
        sc = (((uint32) pa) & 6) * 8;
        rom[rg] = (rom[rg] & ~(((t_uint64) M16) << sc)) | (((t_uint64) (val & M16)) << sc);
        break;

    case L_LONG:
        if (pa & 4) rom[rg] = ((t_uint64) (rom[rg] & M32)) | (((t_uint64) (val & M32)) << 32);
        else rom[rg] = (rom[rg] & ~((t_uint64) M32)) | ((t_uint64) val & M32);
        break;

    case L_QUAD:
        rom[rg] = val;
        break;
        }

return TRUE;
}

/* ROM examine */

t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (vptr == NULL) return SCPE_ARG;
if (addr >= ROMSIZE) return SCPE_NXM;
*vptr = rom[addr >> 3];
return SCPE_OK;
}

/* ROM deposit */

t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr >= ROMSIZE) return SCPE_NXM;
rom[addr >> 3] = val;
return SCPE_OK;
}

/* ROM reset */

t_stat rom_reset (DEVICE *dptr)
{
if (rom == NULL) rom = (t_uint64 *) calloc (ROMSIZE >> 3, sizeof (t_uint64));
if (rom == NULL) return SCPE_MEM;
return SCPE_OK;
}
