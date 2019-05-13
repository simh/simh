/* vax610_io.c: MicroVAX I Qbus IO simulator

   Copyright (c) 2011-2012, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   qba          Qbus adapter

   15-Feb-2012  MB      First Version
*/

#include "vax_defs.h"

int32 int_req[IPL_HLVL] = { 0 };                        /* intr, IPL 14-17 */
int32 int_vec_set[IPL_HLVL][32] = { 0 };                /* bits to set in vector */
int32 autcon_enb = 1;                                   /* autoconfig enable */

extern int32 vc_mem_rd (int32 pa);
extern void vc_mem_wr (int32 pa, int32 val, int32 lnt);

int32 eval_int (void);
t_stat qba_reset (DEVICE *dptr);
const char *qba_description (DEVICE *dptr);

/* Qbus adapter data structures

   qba_dev      QBA device descriptor
   qba_unit     QBA units
   qba_reg      QBA register list
*/

UNIT qba_unit = { UDATA (NULL, 0, 0) };

REG qba_reg[] = {
    { HRDATAD (IPL17, int_req[3], 32, "IPL 17 interrupt flags"), REG_RO },
    { HRDATAD (IPL16, int_req[2], 32, "IPL 16 interrupt flags"), REG_RO },
    { HRDATAD (IPL15, int_req[1], 32, "IPL 15 interrupt flags"), REG_RO },
    { HRDATAD (IPL14, int_req[0], 32, "IPL 14 interrupt flags"), REG_RO },
    { FLDATA (AUTOCON, autcon_enb, 0), REG_HRO },
    { NULL }
    };

MTAB qba_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
      NULL, &show_iospace, NULL, "Display I/O space address map" },
    { MTAB_XTD|MTAB_VDV, 1, "AUTOCONFIG", "AUTOCONFIG",
      &set_autocon, &show_autocon, NULL, "Enable/Display autoconfiguration" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOAUTOCONFIG",
      &set_autocon, NULL, NULL, "Disable autoconfiguration" },
    { 0 }
    };

DEVICE qba_dev = {
    "QBUS", &qba_unit, qba_reg, qba_mod,
    1, 16, 4, 2, 16, 16,
    NULL, NULL, &qba_reset,
    NULL, NULL, NULL,
    NULL, DEV_QBUS, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &qba_description
    };

/* IO page dispatches */

t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);
DIB *iodibp[IOPAGESIZE >> 1];

/* Interrupt request to interrupt action map */

int32 (*int_ack[IPL_HLVL][32])(void);                   /* int ack routines */

/* Interrupt request to vector map */

int32 int_vec[IPL_HLVL][32];                            /* int req to vector */

#define QB_VEC_MASK     0x1FC                           /* Interrupt Vector value mask */

/* The KA610 handles errors in I/O space as follows

        - read: machine check
        - write: machine check (?)
*/

int32 ReadQb (uint32 pa)
{
int32 idx, val;

if (ADDR_IS_QVM (pa))
    return vc_mem_rd (pa);

idx = (pa & IOPAGEMASK) >> 1;
if (iodispR[idx]) {
    iodispR[idx] (&val, pa, READ);
    return val;
    }
MACH_CHECK (MCHK_READ);
return 0;
}

void WriteQb (uint32 pa, int32 val, int32 mode)
{
int32 idx;

if (ADDR_IS_QVM (pa)) {
    vc_mem_wr (pa, val, mode);
    return;
    }

idx = (pa & IOPAGEMASK) >> 1;
if (iodispW[idx]) {
    iodispW[idx] (val, pa, mode);
    return;
    }
MACH_CHECK (MCHK_WRITE);                                /* FIXME: is this correct? */
return;
}

/* ReadIO - read I/O space - aligned access

   Inputs:
        pa      =       physical address
        lnt     =       length (BWLQ)
   Output:
        longword of data
*/

int32 ReadIO (uint32 pa, int32 lnt)
{
int32 iod;

iod = ReadQb (pa);                                      /* wd from Qbus */
if (lnt < L_LONG)                                       /* bw? position */
    iod = iod << ((pa & 2)? 16: 0);
else
    iod = (ReadQb (pa + 2) << 16) | iod;               /* lw, get 2nd wd */
SET_IRQL;
return iod;
}

/* ReadIOU - read I/O space - unaligned access

   Inputs:
        pa      =       physical address
        lnt     =       length (1, 2, 3 bytes)
   Output:
        data, not shifted

Note that all of these cases are presented to the existing aligned IO routine:

bo = 0, byte, word, or longword length
bo = 2, word
bo = 1, 2, 3, byte length

All the other cases are end up at ReadIOU and WriteIOU, and they must turn
the request into the exactly correct number of Qbus accesses AND NO MORE,
because Qbus reads can have side-effects, and word read-modify-write is NOT
the same as a byte write.

Note that the sum of the pa offset and the length cannot be greater than 4.
The read cases are:

bo = 0, byte or word - read one word
bo = 0, tribyte - read two words
bo = 1, byte - read one word
bo = 1, word or tribyte - read two words
bo = 2, byte or word - read one word
bo = 3, byte - read one word
*/

int32 ReadIOU (uint32 pa, int32 lnt)
{
int32 iod;

iod = ReadQb (pa);                                      /* wd from Qbus */
if ((lnt + (pa & 1)) <= 2)                              /* byte or (word & even) */
    iod = iod << ((pa & 2)? 16: 0);                     /* one op */
else
    iod = (ReadQb (pa + 2) << 16) | iod;               /* two ops, get 2nd wd */
SET_IRQL;
return iod;
}

/* WriteIO - write I/O space - aligned access

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (BWLQ)
   Outputs:
        none
*/

void WriteIO (uint32 pa, int32 val, int32 lnt)
{
if (lnt == L_BYTE)
    WriteQb (pa, val, WRITEB);
else if (lnt == L_WORD)
    WriteQb (pa, val, WRITE);
else {
    WriteQb (pa, val & 0xFFFF, WRITE);
    WriteQb (pa + 2, (val >> 16) & 0xFFFF, WRITE);
    }
SET_IRQL;
return;
}

/* WriteIOU - write I/O space

   Inputs:
        pa      =       physical address
        val     =       data to write, right justified in 32b longword
        lnt     =       length (1, 2, or 3 bytes)
   Outputs:
        none

The write cases are:

bo = x, lnt = byte - write one byte
bo = 0 or 2, lnt = word - write one word
bo = 1, lnt = word - write two bytes
bo = 0, lnt = tribyte - write word, byte
bo = 1, lnt = tribyte - write byte, word
*/

void WriteIOU (uint32 pa, int32 val, int32 lnt)
{
switch (lnt) {
case L_BYTE:                                            /* byte */
    WriteQb (pa, val & BMASK, WRITEB);
    break;

case L_WORD:                                            /* word */
    if (pa & 1) {                                       /* odd addr? */
        WriteQb (pa, val & BMASK, WRITEB);
        WriteQb (pa + 1, (val >> 8) & BMASK, WRITEB);
        }
    else WriteQb (pa, val & WMASK, WRITE);
    break;

case 3:                                                 /* tribyte */
    if (pa & 1) {                                       /* odd addr? */
        WriteQb (pa, val & BMASK, WRITEB);              /* byte then word */
        WriteQb (pa + 1, (val >> 8) & WMASK, WRITE);
        }
    else {                                              /* even */
        WriteQb (pa, val & WMASK, WRITE);               /* word then byte */
        WriteQb (pa + 2, (val >> 16) & BMASK, WRITEB);
        }
    break;
    }
SET_IRQL;
return;
}

/* Find highest priority outstanding interrupt */

int32 eval_int (void)
{
int32 ipl = PSL_GETIPL (PSL);
int32 i, t;

static const int32 sw_int_mask[IPL_SMAX] = {
    0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,                     /* 0 - 3 */
    0xFFE0, 0xFFC0, 0xFF80, 0xFF00,                     /* 4 - 7 */
    0xFE00, 0xFC00, 0xF800, 0xF000,                     /* 8 - B */
    0xE000, 0xC000, 0x8000                              /* C - E */
    };

if (hlt_pin)                                            /* hlt pin int */
    return IPL_HLTPIN;
if ((ipl < IPL_MEMERR) && mem_err)                      /* mem err int */
    return IPL_MEMERR;
for (i = IPL_HMAX; i >= IPL_HMIN; i--) {                /* chk hwre int */
    if (i <= ipl)                                       /* at ipl? no int */
        return 0;
    if (int_req[i - IPL_HMIN])                          /* req != 0? int */
        return i;
    }
if (ipl >= IPL_SMAX)                                    /* ipl >= sw max? */
    return 0;
if ((t = SISR & sw_int_mask[ipl]) == 0)                 /* eligible req */
    return 0;
for (i = IPL_SMAX; i > ipl; i--) {                      /* check swre int */
    if ((t >> i) & 1)                                   /* req != 0? int */
        return i;
    }
return 0;
}

/* Return vector for highest priority hardware interrupt at IPL lvl */

int32 get_vector (int32 lvl)
{
int32 i;
int32 l = lvl - IPL_HMIN;

if (lvl == IPL_MEMERR) {                                /* mem error? */
    mem_err = 0;
    return SCB_MEMERR;
    }
if (lvl > IPL_HMAX) {                                   /* error req lvl? */
    ABORT (STOP_UIPL);                                  /* unknown intr */
    }
for (i = 0; int_req[l] && (i < 32); i++) {
    if ((int_req[l] >> i) & 1) {
        int32 vec;

        int_req[l] = int_req[l] & ~(1u << i);
        if (int_ack[l][i])
            vec =int_ack[l][i]();
        else
            vec = int_vec[l][i];
        vec |= int_vec_set[l][i];
        vec &= (int_vec_set[l][i] | QB_VEC_MASK);
        return vec;
        }
    }
return 0;
}

/* Reset I/O bus */

void ioreset_wr (int32 data)
{
reset_all (5);                                          /* from qba on... */
return;
}

/* Reset Qbus */

t_stat qba_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < IPL_HLVL; i++)
    int_req[i] = 0;
return SCPE_OK;
}

const char *qba_description (DEVICE *dptr)
{
return "Qbus adapter";
}

/* Qbus I/O buffer routines, aligned access

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma = ba & 0x3FFFFF;
uint32 dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {              /* by bytes */
        *buf = ReadB (ma);
        ma = ma + 1;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {        /* by longwords */
        dat = ReadL (ma);                               /* get lw */
        *buf++ = dat & BMASK;                           /* low 8b */
        *buf++ = (dat >> 8) & BMASK;                    /* next 8b */
        *buf++ = (dat >> 16) & BMASK;                   /* next 8b */
        *buf = (dat >> 24) & BMASK;
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i;
uint32 ma = ba & 0x3FFFFF;
uint32 dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        *buf = ReadW (ma);
        ma = ma + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        dat = ReadL (ma);                               /* get lw */
        *buf++ = dat & WMASK;                           /* low 16b */
        *buf = (dat >> 16) & WMASK;                     /* high 16b */
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf)
{
int32 i;
uint32 ma = ba & 0x3FFFFF;
uint32 dat;

if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i++, buf++) {                   /* by bytes */
        WriteB (ma, *buf);
        ma = ma + 1;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        dat = (uint32) *buf++;                          /* get low 8b */
        dat = dat | (((uint32) *buf++) << 8);           /* merge next 8b */
        dat = dat | (((uint32) *buf++) << 16);          /* merge next 8b */
        dat = dat | (((uint32) *buf) << 24);            /* merge hi 8b */
        WriteL (ma, dat);                               /* store lw */
        ma = ma + 4;
        }
    }
return 0;
}

int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf)
{
int32 i;
uint32 ma = ba & 0x3FFFFF;
uint32 dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {                                   /* check alignment */
    for (i = 0; i < bc; i = i + 2, buf++) {             /* by words */
        WriteW (ma, *buf);
        ma = ma + 2;
        }
    }
else {
    for (i = 0; i < bc; i = i + 4, buf++) {             /* by longwords */
        dat = (uint32) *buf++;                          /* get low 16b */
        dat = dat | (((uint32) *buf) << 16);            /* merge hi 16b */
        WriteL (ma, dat);                               /* store lw */
        ma = ma + 4;
        }
    }
return 0;
}

/* Build dib_tab from device list */

t_stat build_dib_tab (void)
{
int32 i;
DEVICE *dptr;
DIB *dibp;
t_stat r;

init_ubus_tab ();                                       /* init bus tables */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if ((r = build_ubus_tab (dptr, dibp)))          /* add to bus tab */
            return r;
        }                                               /* end if enabled */
    }                                                   /* end for */
return SCPE_OK;
}
