/* pdp11_io.c: PDP-11 I/O simulator

   Copyright (c) 1993-2005, Robert M Supnik

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

   15-Oct-05    RMS     Fixed bug in autoconfiguration (missing XU)
   25-Jul-05    RMS     Revised autoconfiguration algorithm and interface
   30-Sep-04    RMS     Revised Unibus interface
   28-May-04    RMS     Revised I/O dispatching (from John Dundas)
   25-Jan-04    RMS     Removed local debug logging support
   21-Dec-03    RMS     Fixed bug in autoconfigure vector assignment; added controls
   21-Nov-03    RMS     Added check for interrupt slot conflict (found by Dave Hittner)
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
extern int32 cpu_opt, cpu_bme;
extern int32 trap_req, ipl;
extern int32 cpu_log;
extern int32 autcon_enb;
extern int32 uba_last;
extern FILE *sim_log;
extern DEVICE *sim_devices[], cpu_dev;
extern UNIT cpu_unit;

int32 calc_ints (int32 nipl, int32 trq);

extern t_stat cpu_build_dib (void);
extern void init_mbus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);

/* I/O data structures */

static t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
static t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);
static DIB *iodibp[IOPAGESIZE >> 1];

int32 int_vec[IPL_HLVL][32];                            /* int req to vector */

int32 (*int_ack[IPL_HLVL][32])(void);                   /* int ack routines */

static const int32 pirq_bit[7] = {
    INT_V_PIR1, INT_V_PIR2, INT_V_PIR3, INT_V_PIR4,
    INT_V_PIR5, INT_V_PIR6, INT_V_PIR7
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

/* Calculate interrupt outstanding */

int32 calc_ints (int32 nipl, int32 trq)
{
int32 i;

for (i = IPL_HLVL - 1; i > nipl; i--) {
    if (int_req[i]) return (trq | TRAP_INT);
    }
return (trq & ~TRAP_INT);
}

/* Find vector for highest priority interrupt */

int32 get_vector (int32 nipl)
{
int32 i, j, t, vec;

for (i = IPL_HLVL - 1; i > nipl; i--) {                 /* loop thru lvls */
    t = int_req[i];                                     /* get level */
    for (j = 0; t && (j < 32); j++) {                   /* srch level */
        if ((t >> j) & 1) {                             /* irq found? */
            int_req[i] = int_req[i] & ~(1u << j);       /* clr irq */
            if (int_ack[i][j]) vec = int_ack[i][j]();
            else vec = int_vec[i][j];
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

ba = ba & BUSMASK;                                      /* trim address */
lim = ba + bc;
if (cpu_bme) {                                          /* map enabled? */
    for ( ; ba < lim; ba++) {                           /* by bytes */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma)) return (lim - ba);       /* NXM? err */
        if (ma & 1) *buf++ = (M[ma >> 1] >> 8) & 0377;  /* get byte */
        else *buf++ = M[ma >> 1] & 0377;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;                  /* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;          /* no, strt ok? */
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba++) {                          /* by bytes */
        if (ba & 1) *buf++ = (M[ba >> 1] >> 8) & 0377;  /* get byte */
        else *buf++ = M[ba >> 1] & 0377;
        }
    return (lim - alim);
    }
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 alim, lim, ma;

ba = (ba & BUSMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (cpu_bme) {                                          /* map enabled? */
    for (; ba < lim; ba = ba + 2) {                     /* by words */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma)) return (lim - ba);       /* NXM? err */
        *buf++ = M[ma >> 1];
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;                  /* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;          /* no, strt ok? */
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba = ba + 2) {                   /* by words */
        *buf++ = M[ba >> 1];
        }
    return (lim - alim);
    }
}

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 alim, lim, ma;

ba = ba & BUSMASK;                                      /* trim address */
lim = ba + bc;
if (cpu_bme) {                                          /* map enabled? */
    for ( ; ba < lim; ba++) {                           /* by bytes */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma)) return (lim - ba);       /* NXM? err */
        if (ma & 1) M[ma >> 1] = (M[ma >> 1] & 0377) |
            ((uint16) *buf++ << 8);
        else M[ma >> 1] = (M[ma >> 1] & ~0377) | *buf++;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;                  /* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;          /* no, strt ok? */
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba++) {                          /* by bytes */
        if (ba & 1) M[ba >> 1] = (M[ba >> 1] & 0377) |
            ((uint16) *buf++ << 8);
        else M[ba >> 1] = (M[ba >> 1] & ~0377) | *buf++;
        }
    return (lim - alim);
    }
}

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 alim, lim, ma;

ba = (ba & BUSMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (cpu_bme) {                                          /* map enabled? */
    for (; ba < lim; ba = ba + 2) {                     /* by words */
        ma = Map_Addr (ba);                             /* map addr */
        if (!ADDR_IS_MEM (ma)) return (lim - ba);       /* NXM? err */
        M[ma >> 1] = *buf++;
        }
    return 0;
    }
else {                                                  /* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;                  /* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;          /* no, strt ok? */
    else return bc;                                     /* no, err */
    for ( ; ba < alim; ba = ba + 2) {                   /* by words */
        M[ba >> 1] = *buf++;
        }
    return (lim - alim);
    }
}

/* Enable/disable autoconfiguration */

t_stat set_autocon (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr != NULL) return SCPE_ARG;
autcon_enb = val;
return auto_config (NULL, 0);
}

/* Show autoconfiguration status */

t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "autoconfiguration %s", (autcon_enb? "on": "off"));
return SCPE_OK;
}

/* Change device address */

t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newba;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if ((val == 0) || (uptr == NULL)) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
newba = get_uint (cptr, 8, PAMASK, &r);                 /* get new */
if (r != SCPE_OK) return r;                             /* error? */
if ((newba <= IOPAGEBASE) ||                            /* > IO page base? */
    (newba % ((uint32) val))) return SCPE_ARG;          /* check modulus */
dibp->ba = newba;                                       /* store */
dptr->flags = dptr->flags & ~DEV_FLTA;                  /* not floating */
autcon_enb = 0;                                         /* autoconfig off */
return SCPE_OK;
}

/* Show device address */

t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->ba <= IOPAGEBASE)) return SCPE_IERR;
fprintf (st, "address=%08o", dibp->ba);
if (dibp->lnt > 1)
    fprintf (st, "-%08o", dibp->ba + dibp->lnt - 1);
if (dptr->flags & DEV_FLTA) fprintf (st, "*");
return SCPE_OK;
}

/* Set address floating */

t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;

if (cptr != NULL) return SCPE_ARG;
if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dptr->flags = dptr->flags | DEV_FLTA;                   /* floating */
return auto_config (NULL, 0);                           /* autoconfigure */
}

/* Change device vector */

t_stat set_vec (UNIT *uptr, int32 arg, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newvec;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
newvec = get_uint (cptr, 8, VEC_Q + 01000, &r);
if ((r != SCPE_OK) || (newvec == VEC_Q) ||
    ((newvec + (dibp->vnum * 4)) >= (VEC_Q + 01000)) ||
    (newvec & ((dibp->vnum > 1)? 07: 03))) return SCPE_ARG;
dibp->vec = newvec;
dptr->flags = dptr->flags & ~DEV_FLTA;                  /* not floating */
autcon_enb = 0;                                         /* autoconfig off */
return SCPE_OK;
}

/* Show device vector */

t_stat show_vec (FILE *st, UNIT *uptr, int32 arg, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 vec, numvec;

if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
vec = dibp->vec;
if (arg) numvec = arg;
else numvec = dibp->vnum;
if (vec == 0) fprintf (st, "no vector");
else {
    fprintf (st, "vector=%o", vec);
    if (numvec > 1) fprintf (st, "-%o", vec + (4 * (numvec - 1)));
    }
return SCPE_OK;
}

/* Init Unibus tables */

void init_ubus_tab (void)
{
int32 i, j;

for (i = 0; i < IPL_HLVL; i++) {                        /* clear intr tab */
    for (j = 0; j < 32; j++) {
        int_vec[i][j] = 0;
        int_ack[i][j] = NULL;
        }
    }
for (i = 0; i < (IOPAGESIZE >> 1); i++) {               /* clear dispatch tab */
    iodispR[i] = NULL;
    iodispW[i] = NULL;
    iodibp[i] = NULL;
    }
return;
}

/* Build Unibus tables */

t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp)
{
int32 i, idx, vec, ilvl, ibit;

if ((dptr == NULL) || (dibp == NULL)) return SCPE_IERR; /* validate args */
if (dibp->vnum > VEC_DEVMAX) return SCPE_IERR;
for (i = 0; i < dibp->vnum; i++) {                      /* loop thru vec */
    idx = dibp->vloc + i;                               /* vector index */
    vec = dibp->vec? (dibp->vec + (i * 4)): 0;          /* vector addr */
    ilvl = idx / 32;
    ibit = idx % 32;
    if ((int_ack[ilvl][ibit] && dibp->ack[i] &&         /* conflict? */
        (int_ack[ilvl][ibit] != dibp->ack[i])) ||
        (int_vec[ilvl][ibit] && vec &&
        (int_vec[ilvl][ibit] != vec))) {
        printf ("Device %s interrupt slot conflict at %d\n",
            sim_dname (dptr), idx);
        if (sim_log) fprintf (sim_log,
        "Device %s interrupt slot conflict at %d\n",
            sim_dname (dptr), idx);
        return SCPE_STOP;
        }
    if (dibp->ack[i]) int_ack[ilvl][ibit] = dibp->ack[i];
    else if (vec) int_vec[ilvl][ibit] = vec;
    }
for (i = 0; i < (int32) dibp->lnt; i = i + 2) {         /* create entries */
    idx = ((dibp->ba + i) & IOPAGEMASK) >> 1;           /* index into disp */
    if ((iodispR[idx] && dibp->rd &&                    /* conflict? */
        (iodispR[idx] != dibp->rd)) ||
        (iodispW[idx] && dibp->wr &&
        (iodispW[idx] != dibp->wr))) {
        printf ("Device %s address conflict at %08o\n",
            sim_dname (dptr), dibp->ba);
        if (sim_log) fprintf (sim_log,
            "Device %s address conflict at %08o\n",
            sim_dname (dptr), dibp->ba);
        return SCPE_STOP;
        }
    if (dibp->rd) iodispR[idx] = dibp->rd;              /* set rd dispatch */
    if (dibp->wr) iodispW[idx] = dibp->wr;              /* set wr dispatch */
    iodibp[idx] = dibp;                                 /* remember DIB */
    }
return SCPE_OK;
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
if (r = cpu_build_dib ()) return r;                     /* build CPU entries */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if (dptr->flags & DEV_MBUS) {                   /* Massbus? */
            if (r = build_mbus_tab (dptr, dibp))        /* add to Mbus tab */
                return r;
            }
        else {                                          /* no, Unibus */
            if (r = build_ubus_tab (dptr, dibp))        /* add to Unibus tab */
                return r;
            }
        }                                               /* end if enabled */
    }                                                   /* end for */
return SCPE_OK;
}

/* Show IO space */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 i, j;
DEVICE *dptr;
DIB *dibp;

if (build_dib_tab ()) return SCPE_OK;                   /* build IO page */
for (i = 0, dibp = NULL; i < (IOPAGESIZE >> 1); i++) {  /* loop thru entries */
    if (iodibp[i] && (iodibp[i] != dibp)) {             /* new block? */
        dibp = iodibp[i];                               /* DIB for block */
        for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
            if (((DIB*) sim_devices[j]->ctxt) == dibp) {
                dptr = sim_devices[j];                  /* locate device */
                break;
                }                                       /* end if */
            }                                           /* end for j */
        fprintf (st, "%08o - %08o%c\t%s\n",             /* print block entry */
            dibp->ba, dibp->ba + dibp->lnt - 1,
            (dptr && (dptr->flags & DEV_FLTA))? '*': ' ',
            dptr? sim_dname (dptr): "CPU");
        }                                               /* end if */
    }                                                   /* end for i */
return SCPE_OK;
}

/* Autoconfiguration

   The table reflects the MicroVAX 3900 microcode, with one addition - the
   number of controllers field handles devices where multiple instances
   are simulated through a single DEVICE structure (e.g., DZ, VH).

   A minus number of vectors indicates a field that should be calculated
   but not placed in the DIB (RQ, TQ dynamic vectors) */

#define AUTO_MAXC       4
#define AUTO_CSRBASE    0010
#define AUTO_VECBASE    0300

typedef struct {
    char        *dnam[AUTO_MAXC];
    int32       numc;
    int32       numv;
    uint32      amod;
    uint32      vmod;
    uint32      fixa[AUTO_MAXC];
    uint32      fixv[AUTO_MAXC];
    } AUTO_CON;

AUTO_CON auto_tab[] = {
    { { NULL }, 1, 2, 0, 8, { 0 } },                    /* DLV11J - fx CSRs */
    { { NULL }, 1, 2, 8, 8 },                           /* DJ11 */
    { { NULL }, 1, 2, 16, 8 },                          /* DH11 */
    { { NULL }, 1, 2, 8, 8 },                           /* DQ11 */
    { { NULL }, 1, 2, 8, 8 },                           /* DU11 */
    { { NULL }, 1, 2, 8, 8 },                           /* DUP11 */
    { { NULL }, 10, 2, 8, 8 },                          /* LK11A */
    { { NULL }, 1, 2, 8, 8 },                           /* DMC11 */
    { { "DZ" }, DZ_MUXES, 2, 8, 8 },                    /* DZ11 */
    { { NULL }, 1, 2, 8, 8 },                           /* KMC11 */
    { { NULL }, 1, 2, 8, 8 },                           /* LPP11 */
    { { NULL }, 1, 2, 8, 8 },                           /* VMV21 */
    { { NULL }, 1, 2, 16, 8 },                          /* VMV31 */
    { { NULL }, 1, 2, 8, 8 },                           /* DWR70 */
    { { "RL", "RLB" }, 1, 1, 8, 4, {IOBA_RL}, {VEC_RL} }, /* RL11 */
    { { "TS", "TSB", "TSC", "TSD" }, 1, 1, 0, 4,        /* TS11 */
        {IOBA_TS, IOBA_TS + 4, IOBA_TS + 8, IOBA_TS + 12},
        {VEC_TS} },
    { { NULL }, 1, 2, 16, 8 },                          /* LPA11K */
    { { NULL }, 1, 2, 8, 8 },                           /* KW11C */
    { { NULL }, 1, 1, 8, 8 },                           /* reserved */
    { { "RX", "RY" }, 1, 1, 8, 4, {IOBA_RX} , {VEC_RX} }, /* RX11/RX211 */
    { { NULL }, 1, 1, 8, 4 },                           /* DR11W */
    { { NULL }, 1, 1, 8, 4, { 0, 0 }, { 0 } },          /* DR11B - fx CSRs,vec */
    { { NULL }, 1, 2, 8, 8 },                           /* DMP11 */
    { { NULL }, 1, 2, 8, 8 },                           /* DPV11 */
    { { NULL }, 1, 2, 8, 8 },                           /* ISB11 */
    { { NULL }, 1, 2, 16, 8 },                          /* DMV11 */
    { { "XU", "XUB" }, 1, 1, 8, 4, {IOBA_XU}, {VEC_XU} },   /* DEUNA */
    { { "XQ", "XQB" }, 1, 1, 0, 4,                      /* DEQNA */
        {IOBA_XQ,IOBA_XQB}, {VEC_XQ} },
    { { "RQ", "RQB", "RQC", "RQD" }, 1, -1, 4, 4,       /* RQDX3 */
        {IOBA_RQ}, {VEC_RQ} },
    { { NULL }, 1, 8, 32, 4 },                          /* DMF32 */
    { { NULL }, 1, 2, 16, 8 },                          /* KMS11 */
    { { NULL }, 1, 1, 16, 4 },                          /* VS100 */
    { { "TQ", "TQB" }, 1, -1, 4, 4, {IOBA_TQ}, {VEC_TQ} }, /* TQK50 */
    { { NULL }, 1, 2, 16, 8 },                          /* KMV11 */
    { { "VH" }, VH_MUXES, 2, 16, 8 },                   /* DHU11/DHQ11 */
    { { NULL }, 1, 6, 32, 4 },                          /* DMZ32 */
    { { NULL }, 1, 6, 32, 4 },                          /* CP132 */
    { { NULL }, 1, 2, 64, 8, { 0 } },                   /* QVSS - fx CSR */
    { { NULL }, 1, 1, 8, 4 },                           /* VS31 */
    { { NULL }, 1, 1, 0, 4, { 0 } },                    /* LNV11 - fx CSR */
    { { NULL }, 1, 1, 16, 4 },                          /* LNV21/QPSS */
    { { NULL }, 1, 1, 8, 4, { 0 } },                    /* QTA - fx CSR */
    { { NULL }, 1, 1, 8, 4 },                           /* DSV11 */
    { { NULL }, 1, 2, 8, 8 },                           /* CSAM */
    { { NULL }, 1, 2, 8, 8 },                           /* ADV11C */
    { { NULL }, 1, 0, 8, 0 },                           /* AAV11C */
    { { NULL }, 1, 2, 8, 8, { 0 }, { 0 } },             /* AXV11C - fx CSR,vec */
    { { NULL }, 1, 2, 4, 8, { 0 } },                    /* KWV11C - fx CSR */
    { { NULL }, 1, 2, 8, 8, { 0 } },                    /* ADV11D - fx CSR */
    { { NULL }, 1, 2, 8, 8, { 0 } },                    /* AAV11D - fx CSR */
/*  { { "QDSS" }, 1, 3, 0, 16, {IOBA_QDSS} },           /* QDSS - fx CSR */
    { { NULL }, -1 }                                    /* end table */
};

t_stat auto_config (char *name, int32 nctrl)
{
uint32 csr = IOPAGEBASE + AUTO_CSRBASE;
uint32 vec = VEC_Q + AUTO_VECBASE;
AUTO_CON *autp;
DEVICE *dptr;
DIB *dibp;
uint32 j, k, vmask, amask;

if (autcon_enb == 0) return SCPE_OK;                    /* enabled? */
if (name) {                                             /* updating? */
    if (nctrl < 0) return SCPE_ARG;
    for (autp = auto_tab; autp->numc >= 0; autp++) {
        for (j = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
            if (strcmp (name, autp->dnam[j]) == 0)
                autp->numc = nctrl;
            }
        }
    }
for (autp = auto_tab; autp->numc >= 0; autp++) {        /* loop thru table */
    if (autp->amod) {                                   /* floating csr? */
        amask = autp->amod - 1;
        csr = (csr + amask) & ~amask;                   /* align csr */
        }
    for (j = k = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
        dptr = find_dev (autp->dnam[j]);                /* find ctrl */
        if ((dptr == NULL) || (dptr->flags & DEV_DIS) ||
            !(dptr->flags & DEV_FLTA)) continue;        /* enabled, floating? */
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp == NULL) return SCPE_IERR;             /* not there??? */
        if (autp->amod) {                               /* dyn csr needed? */
            if (autp->fixa[k])                          /* fixed csr avail? */
                dibp->ba = autp->fixa[k];               /* use it */
            else {                                      /* no fixed left */
                dibp->ba = csr;                         /* set CSR */
                csr += (autp->numc * autp->amod);       /* next CSR */
                }                                       /* end else */
            }                                           /* end if dyn csr */
        if (autp->numv && autp->vmod) {                 /* dyn vec needed? */
            uint32 numv = abs (autp->numv);             /* get num vec */
            if (autp->fixv[k]) {                        /* fixed vec avail? */
                if (autp->numv > 0)
                    dibp->vec = autp->fixv[k];          /* use it */
                }
            else {                                      /* no fixed left */
                vmask = autp->vmod - 1;
                vec = (vec + vmask) & ~vmask;           /* align vector */
                if (autp->numv > 0)
                    dibp->vec = vec;                    /* set vector */
                vec += (autp->numc * numv * 4);
                }                                       /* end else */
            }                                           /* end if dyn vec */
        k++;                                            /* next instance */
        }                                               /* end for j */
    if (autp->amod) csr = csr + 2;                      /* flt CSR? gap */
    }                                                   /* end for i */
return SCPE_OK;
}
