/* pdp11_io.c: PDP-11 I/O simulator

   Copyright (c) 1993-2012, Robert M Supnik

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

   27-Mar-12    RMS     Fixed order of int_internal (Jordi Guillaumes i Pons)
   19-Mar-12    RMS     Fixed declaration of cpu_opt (Mark Pizzolato)
   12-Dec-11    RMS     Fixed Qbus interrupts to treat all IO devices as BR4
   19-Nov-08    RMS     Moved I/O support routines to I/O library
   16-May-08    RMS     Added multiple DC11 support
                        Renamed DL11 in autoconfigure
   02-Feb-08    RMS     Fixed DMA memory address limit test (John Dundas)
   06-Jul-06    RMS     Added multiple KL11/DL11 support
   15-Oct-05    RMS     Fixed bug in autoconfiguration (missing XU)
   25-Jul-05    RMS     Revised autoconfiguration algorithm and interface
   30-Sep-04    RMS     Revised Unibus interface
   28-May-04    RMS     Revised I/O dispatching (John Dundas)
   25-Jan-04    RMS     Removed local debug logging support
   21-Dec-03    RMS     Fixed bug in autoconfigure vector assignment; added controls
   21-Nov-03    RMS     Added check for interrupt slot conflict (Dave Hittner)
   12-Mar-03    RMS     Added logical name support
   08-Oct-02    RMS     Trimmed I/O bus addresses
                        Added support for dynamic tables
                        Added show I/O space, autoconfigure routines
   12-Sep-02    RMS     Added support for TMSCP, KW11P, RX211
   26-Jan-02    RMS     Revised for multiple DZ's
   06-Jan-02    RMS     Revised I/O access, enable/disable support
   11-Dec-01    RMS     Moved interrupt debug code
   08-Nov-01    RMS     Cloned from cpu sources
*/

#include "pdp11_defs.h"

extern uint16 *M;
extern int32 int_req[IPL_HLVL];
extern int32 ub_map[UBM_LNT_LW];
extern uint32 cpu_opt;
extern int32 cpu_bme;
extern int32 trap_req, ipl;
extern int32 cpu_log;
extern int32 autcon_enb;
extern int32 uba_last;
extern DEVICE cpu_dev;
extern t_addr cpu_memsize;

int32 calc_ints (int32 nipl, int32 trq);

extern t_stat cpu_build_dib (void);
extern void init_mbus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);
extern void fixup_mbus_tab (void);

/* I/O data structures */

t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);

int32 int_vec[IPL_HLVL][32];                            /* int req to vector */
int32 (*int_ack[IPL_HLVL][32])(void);                   /* int ack routines */

static const int32 pirq_bit[7] = {
    INT_V_PIR1, INT_V_PIR2, INT_V_PIR3, INT_V_PIR4,
    INT_V_PIR5, INT_V_PIR6, INT_V_PIR7
    };

static const int32 int_internal[IPL_HLVL] = {
    0,             INT_INTERNAL1, INT_INTERNAL2, INT_INTERNAL3,
    INT_INTERNAL4, INT_INTERNAL5, INT_INTERNAL6, INT_INTERNAL7
    };

/* I/O page lookup and linkage routines

   Inputs:
        *data   =       pointer to data to read, if READ
        data    =       data to store, if WRITE or WRITEB
        pa      =       address
        access  =       READ, WRITE, or WRITEB
   Outputs:
        status  =       SCPE_OK or SCPE_NXM
*/

t_stat iopageR (int32 *data, uint32 pa, int32 access)
{
int32 idx;
t_stat stat;

idx = (pa & IOPAGEMASK) >> 1;
if (iodispR[idx]) {
    stat = iodispR[idx] (data, pa, access);
    trap_req = calc_ints (ipl, trap_req);
    return stat;
    }
return SCPE_NXM;
}

t_stat iopageW (int32 data, uint32 pa, int32 access)
{
int32 idx;
t_stat stat;

idx = (pa & IOPAGEMASK) >> 1;
if (iodispW[idx]) {
    stat = iodispW[idx] (data, pa, access);
    trap_req = calc_ints (ipl, trap_req);
    return stat;
    }
return SCPE_NXM;
}

/* Calculate interrupt outstanding
   In a Qbus system, all device interrupts are treated as BR4 */

int32 calc_ints (int32 nipl, int32 trq)
{
int32 i, t;
t_bool all_int = (UNIBUS || (nipl < IPL_HMIN));

for (i = IPL_HLVL - 1; i > nipl; i--) {
    t = all_int? int_req[i]: (int_req[i] & int_internal[i]);
    if (t)
        return (trq | TRAP_INT);
    }
return (trq & ~TRAP_INT);
}

/* Find vector for highest priority interrupt
   In a Qbus system, all device interrupts are treated as BR4 */

int32 get_vector (int32 nipl)
{
int32 i, j, t, vec;
t_bool all_int = (UNIBUS || (nipl < IPL_HMIN));

for (i = IPL_HLVL - 1; i > nipl; i--) {                 /* loop thru lvls */
    t = all_int? int_req[i]: (int_req[i] & int_internal[i]);
    for (j = 0; t && (j < 32); j++) {                   /* srch level */
        if ((t >> j) & 1) {                             /* irq found? */
            int_req[i] = int_req[i] & ~(1u << j);       /* clr irq */
            if (int_ack[i][j])
                vec = int_ack[i][j]();
            else
                vec = int_vec[i][j];
            return vec;                                 /* return vector */
            }                                           /* end if t */
        }                                               /* end for j */
    }                                                   /* end for i */
return 0;
}

/* Read and write Unibus map registers

   In any even/odd pair
   even = low 16b, bit <0> clear
   odd  = high 6b

   The Unibus map is stored as an array of longwords.
   These routines are only reachable if a Unibus map is configured.
*/

t_stat ubm_rd (int32 *data, int32 addr, int32 access)
{
int32 pg = (addr >> 2) & UBM_M_PN;

*data = (addr & 2)? ((ub_map[pg] >> 16) & 077):
    (ub_map[pg] & 0177776);
return SCPE_OK;
}

t_stat ubm_wr (int32 data, int32 addr, int32 access)
{
int32 sc, pg = (addr >> 2) & UBM_M_PN;

if (access == WRITEB) {
    sc = (addr & 3) << 3;
    ub_map[pg] = (ub_map[pg] & ~(0377 << sc)) |
        ((data & 0377) << sc);
    }
else {
    sc = (addr & 2) << 3;
    ub_map[pg] = (ub_map[pg] & ~(0177777 << sc)) |
        ((data & 0177777) << sc);
    }
ub_map[pg] = ub_map[pg] & 017777776;
return SCPE_OK;
}

/* Mapped memory access routines for DMA devices */

#define BUSMASK         ((UNIBUS)? UNIMASK: PAMASK)

/* Map I/O address to memory address - caller checks cpu_bme */

uint32 Map_Addr (uint32 ba)
{
int32 pg = UBM_GETPN (ba);                              /* map entry */
int32 off = UBM_GETOFF (ba);                            /* offset */

if (pg != UBM_M_PN)                                     /* last page? */
    uba_last = (ub_map[pg] + off) & PAMASK;             /* no, use map */
else uba_last = (IOPAGEBASE + off) & PAMASK;            /* yes, use fixed */
return uba_last;
}

/* I/O buffer routines, aligned access

   Map_ReadB    -       fetch byte buffer from memory
   Map_ReadW    -       fetch word buffer from memory
   Map_WriteB   -       store byte buffer into memory
   Map_WriteW   -       store word buffer into memory

   These routines are used only for Unibus and Qbus devices.
   Massbus devices have their own IO routines.  As a result,
   the historic 'map' parameter is no longer needed.

   - In a U18 configuration, the map is always disabled.
     Device addresses are trimmed to 18b.
   - In a U22 configuration, the map is always configured
     (although it may be disabled).  Device addresses are
     trimmed to 18b.
   - In a Qbus configuration, the map is always disabled.
     Device addresses are trimmed to 22b.
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 alim, lim, ma;

if (ba >= IOPAGEBASE) {
    int32 value;

    while (bc) {
        if (iopageR( &value, (ba & ~1), READ) != SCPE_OK)
            break;
        *buf++ = (uint8) (((ba & 1)? (value >> 8): value) & 0xff);
        ba++;
        bc--;
        }
    return bc;
    }
ba = ba & BUSMASK;                                      /* trim address */
lim = ba + bc;
if (cpu_bme) {                                          /* map enabled? */
    for ( ; ba < lim; ba++) {                           /* by bytes */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma))                          /* NXM? err */
            return (lim - ba);
        if (ma & 1)                                     /* get byte */
            *buf++ = (M[ma >> 1] >> 8) & 0377;
        else *buf++ = M[ma >> 1] & 0377;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim))                              /* end ok? */
        alim = lim;
    else if (ADDR_IS_MEM (ba))                          /* no, strt ok? */
        alim = cpu_memsize;
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba++) {                          /* by bytes */
        if (ba & 1)
            *buf++ = (M[ba >> 1] >> 8) & 0377;          /* get byte */
        else *buf++ = M[ba >> 1] & 0377;
        }
    return (lim - alim);
    }
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 alim, lim, ma;

if (ba >= IOPAGEBASE) {
    int32 value;
    if ((ba & 1) || (bc & 1))
        return bc;
    while (bc) {
        if (iopageR( &value, ba, READ) != SCPE_OK)
            break;
        *buf++ = (uint16) (value & 0xffff);
        ba += 2;
        bc -= 2;
        }
    return bc;
    }
ba = (ba & BUSMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (cpu_bme) {                                          /* map enabled? */
    for (; ba < lim; ba = ba + 2) {                     /* by words */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma))                          /* NXM? err */
            return (lim - ba);
        *buf++ = M[ma >> 1];
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim))                              /* end ok? */
        alim = lim;
    else if (ADDR_IS_MEM (ba))                          /* no, strt ok? */
        alim = cpu_memsize;
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba = ba + 2) {                   /* by words */
        *buf++ = M[ba >> 1];
        }
    return (lim - alim);
    }
}

int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf)
{
uint32 alim, lim, ma;

if (ba >= IOPAGEBASE) {
    while (bc) {
        if (iopageW( ((int32) *buf++) & 0xff, ba, WRITEB) != SCPE_OK)
            break;
        ba++;
        bc--;
        }
    return bc;
}
ba = ba & BUSMASK;                                      /* trim address */
lim = ba + bc;
if (cpu_bme) {                                          /* map enabled? */
    for ( ; ba < lim; ba++) {                           /* by bytes */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma))                          /* NXM? err */
            return (lim - ba);
        if (ma & 1) M[ma >> 1] = (M[ma >> 1] & 0377) |
            ((uint16) *buf++ << 8);
        else M[ma >> 1] = (M[ma >> 1] & ~0377) | *buf++;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim))                              /* end ok? */
        alim = lim;
    else if (ADDR_IS_MEM (ba))                          /* no, strt ok? */
        alim = cpu_memsize;
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba++) {                          /* by bytes */
        if (ba & 1)
            M[ba >> 1] = (M[ba >> 1] & 0377) | ((uint16) *buf++ << 8);
        else M[ba >> 1] = (M[ba >> 1] & ~0377) | *buf++;
        }
    return (lim - alim);
    }
}

int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf)
{
uint32 alim, lim, ma;

if (ba >= IOPAGEBASE) {
    if ((ba & 1) || (bc & 1))
        return bc;
    while (bc) {
        if (iopageW( ((int32) *buf++) & 0xffff, ba, WRITE) != SCPE_OK)
            break;
        ba += 2;
        bc -= 2;
        }
    return bc;
}
ba = (ba & BUSMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (cpu_bme) {                                          /* map enabled? */
    for (; ba < lim; ba = ba + 2) {                     /* by words */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma))                          /* NXM? err */
            return (lim - ba);
        M[ma >> 1] = *buf++;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim))                              /* end ok? */
        alim = lim;
    else if (ADDR_IS_MEM (ba))                          /* no, strt ok? */
        alim = cpu_memsize;
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba = ba + 2) {                   /* by words */
        M[ba >> 1] = *buf++;
        }
    return (lim - alim);
    }
}

/* Build tables from device list */

t_stat build_dib_tab (void)
{
int32 i;
DEVICE *dptr;
DIB *dibp;
t_stat r;

init_ubus_tab ();                                       /* init Unibus tables */
init_mbus_tab ();                                       /* init Massbus tables */
for (i = 0; i < 7; i++)                                 /* seed PIRQ intr */
    int_vec[i + 1][pirq_bit[i]] = VEC_PIRQ;
if ((r = cpu_build_dib ()))                             /* build CPU entries */
    return r;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if (dptr->flags & DEV_MBUS) {                   /* Massbus? */
            if ((r = build_mbus_tab (dptr, dibp)))      /* add to Mbus tab */
                return r;
            }
        else {                                          /* no, Unibus */
            if ((r = build_ubus_tab (dptr, dibp)))      /* add to Unibus tab */
                return r;
            }
        }                                               /* end if enabled */
    }                                                   /* end for */
fixup_mbus_tab ();
return SCPE_OK;
}
