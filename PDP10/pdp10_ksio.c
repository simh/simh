/* pdp10_ksio.c: PDP-10 KS10 I/O subsystem simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   uba          Unibus adapters

   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   25-Jan-04    RMS     Added stub floating address routine
   12-Mar-03    RMS     Added logical name support
   10-Oct-02    RMS     Revised for dynamic table generation
                        Added SHOW IOSPACE routine
   29-Sep-02    RMS     Added variable vector, central map support
   25-Jan-02    RMS     Revised for multiple DZ11's
   06-Jan-02    RMS     Revised enable/disable support
   23-Sep-01    RMS     New IO page address constants
   07-Sep-01    RMS     Revised device disable mechanism
   25-Aug-01    RMS     Enabled DZ11
   21-Aug-01    RMS     Updated DZ11 disable
   01-Jun-01    RMS     Updated DZ11 vectors
   12-May-01    RMS     Fixed typo

   The KS10 uses the PDP-11 Unibus for its I/O, via adapters.  While
   nominally four adapters are supported, in practice only 1 and 3
   are implemented.  The disks are placed on adapter 1, the rest of
   the I/O devices on adapter 3.

   In theory, we should maintain completely separate Unibuses, with
   distinct PI systems.  In practice, this simulator has so few devices
   that we can get away with a single PI system, masking for which
   devices are on adapter 1, and which on adapter 3.  The Unibus
   implementation is modeled on the Qbus in the PDP-11 simulator and
   is described there.

   The I/O subsystem is programmed by I/O instructions which create
   Unibus operations (read, read pause, write, write byte).  DMA is
   the responsibility of the I/O device simulators, which also implement
   Unibus to physical memory mapping.

   The priority interrupt subsystem (and other privileged functions)
   is programmed by I/O instructions with internal devices codes
   (opcodes 700-702).  These are dispatched here, although many are
   handled in the memory management unit or elsewhere.

   The ITS instructions are significantly different from the TOPS-10/20
   instructions.  They do not use the extended address calculation but
   instead provide instruction variants (Q for Unibus adapter 1, I for
   Unibus adapter 3) which insert the Unibus adapter number into the
   effective address.
*/

#include "pdp10_defs.h"
#include <setjmp.h>
#include "sim_sock.h"
#include "sim_tmxr.h"

#define XBA_MBZ         0400000                         /* ba mbz */
#define eaRB            (ea & ~1)
#define GETBYTE(ea,x)   ((((ea) & 1)? (x) >> 8: (x)) & 0377)
#define UBNXM_FAIL(pa,op) \
                        n = iocmap[GET_IOUBA (pa)]; \
                        if (n >= 0) \
                            ubcs[n] = ubcs[n] | UBCS_TMO | UBCS_NXD; \
                        pager_word = PF_HARD | PF_VIRT | PF_IO | \
                            ((op == WRITEB)? PF_BYTE: 0) | \
                            (TSTF (F_USR)? PF_USER: 0) | (pa); \
                        ABORT (PAGE_FAIL)

/* Unibus adapter data */

int32 ubcs[UBANUM] = { 0 };                             /* status registers */
int32 ubmap[UBANUM][UMAP_MEMSIZE] = { 0 };              /* Unibus maps */
int32 int_req = 0;                                      /* interrupt requests */

/* Map IO controller numbers to Unibus adapters: -1 = non-existent */

static int iocmap[IO_N_UBA] = {                         /* map I/O ext to UBA # */
 -1, 0, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
 }; 

static const int32 ubabr76[UBANUM] = {
    INT_UB1 & (INT_IPL7 | INT_IPL6), INT_UB3 & (INT_IPL7 | INT_IPL6)
    };
static const int32 ubabr54[UBANUM] = {
    INT_UB1 & (INT_IPL5 | INT_IPL4), INT_UB3 & (INT_IPL5 | INT_IPL4)
    };
static const int32 ubashf[4] = { 18, 26, 0, 8 };

extern d10 *M;                                          /* main memory */
extern d10 *ac_cur;
extern d10 pager_word;
extern int32 flags;
extern const int32 pi_l2bit[8];
extern UNIT cpu_unit;
extern FILE *sim_log;
extern jmp_buf save_env;
extern DEVICE *sim_devices[];

extern int32 pi_eval (void);
extern int32 rp_inta (void);
extern int32 tu_inta (void);
extern int32 lp20_inta (void);
extern int32 dz_rxinta (void);
extern int32 dz_txinta (void);

t_stat ubmap_rd (int32 *data, int32 addr, int32 access);
t_stat ubmap_wr (int32 data, int32 addr, int32 access);
t_stat ubs_rd (int32 *data, int32 addr, int32 access);
t_stat ubs_wr (int32 data, int32 addr, int32 access);
t_stat rd_zro (int32 *data, int32 addr, int32 access);
t_stat wr_nop (int32 data, int32 addr, int32 access);
t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_reset (DEVICE *dptr);
d10 ReadIO (a10 ea);
void WriteIO (a10 ea, d10 val, int32 mode);

/* Unibus adapter data structures

   uba_dev      UBA device descriptor
   uba_unit     UBA units
   uba_reg      UBA register list
*/

DIB ubmp1_dib = { IOBA_UBMAP1, IOLN_UBMAP1, &ubmap_rd, &ubmap_wr, 0 };
DIB ubmp3_dib = { IOBA_UBMAP3, IOLN_UBMAP3, &ubmap_rd, &ubmap_wr, 0 };
DIB ubcs1_dib = { IOBA_UBCS1, IOLN_UBCS1, &ubs_rd, &ubs_wr, 0 };
DIB ubcs3_dib = { IOBA_UBCS3, IOLN_UBCS3, &ubs_rd, &ubs_wr, 0 };
DIB ubmn1_dib = { IOBA_UBMNT1, IOLN_UBMNT1, &rd_zro, &wr_nop, 0 };
DIB ubmn3_dib = { IOBA_UBMNT3, IOLN_UBMNT3, &rd_zro, &wr_nop, 0 };
DIB msys_dib = { 00100000, 1, &rd_zro, &wr_nop, 0 };

UNIT uba_unit[] = {
    { UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) },
    { UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) }
    };

REG uba_reg[] = {
    { ORDATA (INTREQ, int_req, 32), REG_RO },
    { ORDATA (UB1CS, ubcs[0], 18) },
    { ORDATA (UB3CS, ubcs[1], 18) },
    { NULL }
    };

DEVICE uba_dev = {
    "UBA", uba_unit, uba_reg, NULL,
    UBANUM, 8, UMAP_ASIZE, 1, 8, 32,
    &uba_ex, &uba_dep, &uba_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* PDP-11 I/O structures */

DIB *dib_tab[DIB_MAX];                                  /* run-time DIBs */

int32 (*int_ack[32])(void);                             /* int ack routines */

int32 int_vec[32];                                      /* int vectors */

DIB *std_dib[] = {                                      /* standard DIBs */
    &ubmp1_dib,
    &ubmp3_dib,
    &ubcs1_dib,
    &ubcs3_dib,
    &ubmn1_dib,
    &ubmn3_dib,
    &msys_dib,
    NULL
    };

/* IO 710       (DEC) TIOE - test I/O word, skip if zero
                (ITS) IORDI - read word from Unibus 3
                returns TRUE if skip, FALSE otherwise
*/

t_bool io710 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS)                                              /* IORDI */
    AC(ac) = ReadIO (IO_UBA3 | ea);
else {                                                  /* TIOE */
    val = ReadIO (ea);                                  /* read word */
    if ((AC(ac) & val) == 0)
        return TRUE;
    }
return FALSE;
}

/* IO 711       (DEC) TION - test I/O word, skip if non-zero
                (ITS) IORDQ - read word from Unibus 1
                returns TRUE if skip, FALSE otherwise
*/

t_bool io711 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS)                                              /* IORDQ */
    AC(ac) = ReadIO (IO_UBA1 | ea);
else {                                                  /* TION */
    val = ReadIO (ea);                                  /* read word */
    if ((AC(ac) & val) != 0)
        return TRUE;
    }
return FALSE;
}

/* IO 712       (DEC) RDIO - read I/O word, addr in ea
                (ITS) IORD - read I/O word, addr in M[ea]
*/

d10 io712 (a10 ea)
{
return ReadIO (ea);                                     /* RDIO, IORD */
}

/* IO 713       (DEC) WRIO - write I/O word, addr in ea
                (ITS) IOWR - write I/O word, addr in M[ea]
*/

void io713 (d10 val, a10 ea)
{
WriteIO (ea, val & 0177777, WRITE);                     /* WRIO, IOWR */
return;
}

/* IO 714       (DEC) BSIO - set bit in I/O address
                (ITS) IOWRI - write word to Unibus 3
*/

void io714 (d10 val, a10 ea)
{
d10 temp;

val = val & 0177777;
if (Q_ITS)                                              /* IOWRI */
    WriteIO (IO_UBA3 | ea, val, WRITE);
else {
    temp = ReadIO (ea);                                 /* BSIO */
    temp = temp | val;
    WriteIO (ea, temp, WRITE);
    }
return;
}

/* IO 715       (DEC) BCIO - clear bit in I/O address
                (ITS) IOWRQ - write word to Unibus 1
*/

void io715 (d10 val, a10 ea)
{
d10 temp;

val = val & 0177777;
if (Q_ITS)                                              /* IOWRQ */
    WriteIO (IO_UBA1 | ea, val, WRITE);
else {
    temp = ReadIO (ea);                                 /* BCIO */
    temp = temp & ~val;
    WriteIO (ea, temp, WRITE);
    }
return;
}

/* IO 720       (DEC) TIOEB - test I/O byte, skip if zero
                (ITS) IORDBI - read byte from Unibus 3
                returns TRUE if skip, FALSE otherwise
*/

t_bool io720 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS) {                                            /* IORDBI */
    val = ReadIO (IO_UBA3 | eaRB);
    AC(ac) = GETBYTE (ea, val);
    }
else {                                                  /* TIOEB */
    val = ReadIO (eaRB);
    val = GETBYTE (ea, val);
    if ((AC(ac) & val) == 0)
        return TRUE;
    }
return FALSE;
}

/* IO 721       (DEC) TIONB - test I/O word, skip if non-zero
                (ITS) IORDBQ - read word from Unibus 1
                returns TRUE if skip, FALSE otherwise
*/

t_bool io721 (int32 ac, a10 ea)
{
d10 val;

if (Q_ITS) {                                            /* IORDBQ */
    val = ReadIO (IO_UBA1 | eaRB);
    AC(ac) = GETBYTE (ea, val);
    }
else {                                                  /* TIONB */
    val = ReadIO (eaRB);
    val = GETBYTE (ea, val);
    if ((AC(ac) & val) != 0)
        return TRUE;
    }
return FALSE;
}

/* IO 722       (DEC) RDIOB - read I/O byte, addr in ea
                (ITS) IORDB - read I/O byte, addr in M[ea]
*/

d10 io722 (a10 ea)
{
d10 val;

val = ReadIO (eaRB);                                    /* RDIOB, IORDB */
return GETBYTE (ea, val);
}

/* IO 723       (DEC) WRIOB - write I/O byte, addr in ea
                (ITS) IOWRB - write I/O byte, addr in M[ea]
*/

void io723 (d10 val, a10 ea)
{
WriteIO (ea, val & 0377, WRITEB);                       /* WRIOB, IOWRB */
return;
}

/* IO 724       (DEC) BSIOB - set bit in I/O byte address
                (ITS) IOWRBI - write byte to Unibus 3
*/

void io724 (d10 val, a10 ea)
{
d10 temp;

val = val & 0377;
if (Q_ITS)                                              /* IOWRBI */
    WriteIO (IO_UBA3 | ea, val, WRITEB);
else {
    temp = ReadIO (eaRB);                               /* BSIOB */
    temp = GETBYTE (ea, temp);
    temp = temp | val;
    WriteIO (ea, temp, WRITEB);
    }
return;
}

/* IO 725       (DEC) BCIOB - clear bit in I/O byte address
                (ITS) IOWRBQ - write byte to Unibus 1
*/

void io725 (d10 val, a10 ea)
{
d10 temp;

val = val & 0377;
if (Q_ITS)                                              /* IOWRBQ */
    WriteIO (IO_UBA1 | ea, val, WRITEB);
else {
    temp = ReadIO (eaRB);                               /* BCIOB */
    temp = GETBYTE (ea, temp);
    temp = temp & ~val;
    WriteIO (ea, temp, WRITEB);
    }
return;
}

/* Read and write I/O devices.
   These routines are the linkage between the 64b world of the main
   simulator and the 32b world of the device simulators.
*/

d10 ReadIO (a10 ea)
{
uint32 pa = (uint32) ea;
int32 i, n, val;
DIB *dibp;

for (i = 0; dibp = dib_tab[i]; i++ ) {
    if ((pa >= dibp->ba) &&
       (pa < (dibp->ba + dibp->lnt))) {
        dibp->rd (&val, pa, READ);
        pi_eval ();
        return ((d10) val);
        }
    }
UBNXM_FAIL (pa, READ);
}

void WriteIO (a10 ea, d10 val, int32 mode)
{
uint32 pa = (uint32) ea;
int32 i, n;
DIB *dibp;

for (i = 0; dibp = dib_tab[i]; i++ ) {
    if ((pa >= dibp->ba) &&
       (pa < (dibp->ba + dibp->lnt))) {
        dibp->wr ((int32) val, pa, mode);
        pi_eval ();
        return;
        } 
    }
UBNXM_FAIL (pa, mode);
}

/* Mapped read and write routines - used by standard Unibus devices on Unibus 1 */

a10 Map_Addr10 (a10 ba, int32 ub)
{
a10 pa10;
int32 vpn = PAG_GETVPN (ba >> 2);                       /* get PDP-10 page number */
    
if ((vpn >= UMAP_MEMSIZE) || (ba & XBA_MBZ) ||          /* invalid map? */
    ((ubmap[ub][vpn] & UMAP_VLD) == 0))
    return -1;
pa10 = (ubmap[ub][vpn] + PAG_GETOFF (ba >> 2)) & PAMASK;
return pa10;
}

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 lim;
a10 pa10;

lim = ba + bc;
for ( ; ba < lim; ba++) {                               /* by bytes */
    pa10 = Map_Addr10 (ba, 1);                          /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {            /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;                   /* UBA times out */
        return (lim - ba);                              /* return bc */
        }
    *buf++ = (uint8) ((M[pa10] >> ubashf[ba & 3]) & 0377);
    }
return 0;
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 lim;
a10 pa10;

ba = ba & ~01;                                          /* align start */
lim = ba + (bc & ~01);
for ( ; ba < lim; ba = ba + 2) {                        /* by words */
    pa10 = Map_Addr10 (ba, 1);                          /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {            /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;                   /* UBA times out */
        return (lim - ba);                              /* return bc */
        }
    *buf++ = (uint16) ((M[pa10] >> ((ba & 2)? 0: 18)) & 0177777);
    }
return 0;
}

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf)
{
uint32 lim;
a10 pa10;
d10 mask;

lim = ba + bc;
for ( ; ba < lim; ba++) {                               /* by bytes */
    pa10 = Map_Addr10 (ba, 1);                          /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {            /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;                   /* UBA times out */
        return (lim - ba);                              /* return bc */
        }
    mask = 0377;
    M[pa10] = (M[pa10] & ~(mask << ubashf[ba & 3])) |
        (((d10) *buf++) << ubashf[ba & 3]);
    }
return 0;
}

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf)
{
uint32 lim;
a10 pa10;
d10 val;

ba = ba & ~01;                                          /* align start */
lim = ba + (bc & ~01);
for ( ; ba < lim; ba++) {                               /* by bytes */
    pa10 = Map_Addr10 (ba, 1);                          /* map addr */
    if ((pa10 < 0) || MEM_ADDR_NXM (pa10)) {            /* inv map or NXM? */
        ubcs[1] = ubcs[1] | UBCS_TMO;                   /* UBA times out */
        return (lim - ba);                              /* return bc */
        }
    val = *buf++;                                       /* get data */
    if (ba & 2)
        M[pa10] = (M[pa10] & 0777777600000) | val;
    else M[pa10] = (M[pa10] & 0600000777777) | (val << 18);
    }
return 0;
}

/* Evaluate Unibus priority interrupts */

int32 pi_ub_eval ()
{
int32 i, lvl;

for (i = lvl = 0; i < UBANUM; i++) {
    if (int_req & ubabr76[i])
        lvl = lvl | pi_l2bit[UBCS_GET_HI (ubcs[i])];
    if (int_req & ubabr54[i])
        lvl = lvl | pi_l2bit[UBCS_GET_LO (ubcs[i])];
    }
return lvl;
}

/* Return Unibus device vector

   Takes as input the request level calculated by pi_eval
   If there is an interrupting Unibus device at that level, return its vector,
        otherwise, returns 0
*/

int32 pi_ub_vec (int32 rlvl, int32 *uba)
{
int32 i, masked_irq;

for (i = masked_irq = 0; i < UBANUM; i++) {
    if ((rlvl == UBCS_GET_HI (ubcs[i])) &&              /* req on hi level? */
        (masked_irq = int_req & ubabr76[i]))
        break;
    if ((rlvl == UBCS_GET_LO (ubcs[i])) &&              /* req on lo level? */
        (masked_irq = int_req & ubabr54[i]))
        break;
    }
*uba = (i << 1) + 1;                                    /* store uba # */
for (i = 0; (i < 32) && masked_irq; i++) {              /* find hi pri req */
    if ((masked_irq >> i) & 1) {
        int_req = int_req & ~(1u << i);                 /* clear req */
        if (int_ack[i])
            return int_ack[i]();
        return int_vec[i];                              /* return vector */
        }
    }
return 0;
}

/* Unibus adapter map routines */

t_stat ubmap_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
*val = ubmap[n][pa & UMAP_AMASK];
return SCPE_OK;
}

t_stat ubmap_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
ubmap[n][pa & UMAP_AMASK] = UMAP_POSFL (val) | UMAP_POSPN (val);
return SCPE_OK;
}

/* Unibus adapter control/status routines */

t_stat ubs_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
if (int_req & ubabr76[n])
    ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n])
    ubcs[n] = ubcs[n] | UBCS_LO;
*val = ubcs[n] = ubcs[n] & ~UBCS_RDZ;
return SCPE_OK;
}

t_stat ubs_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0)
    ABORT (STOP_ILLIOC);
if (val & UBCS_INI) {
    reset_all (5);                                      /* start after UBA */
    ubcs[n] = val & UBCS_DXF;
    }
else ubcs[n] = val & UBCS_RDW;
if (int_req & ubabr76[n])
    ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n])
    ubcs[n] = ubcs[n] | UBCS_LO;
return SCPE_OK;
}

/* Unibus adapter read zero/write ignore routines */

t_stat rd_zro (int32 *val, int32 pa, int32 mode)
{
*val = 0;
return SCPE_OK;
}

t_stat wr_nop (int32 val, int32 pa, int32 mode)
{
return SCPE_OK;
}

/* Simulator interface routines */

t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE)
    return SCPE_NXM;
*vptr = ubmap[uba][addr];
return SCPE_OK;
}

t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE)
    return SCPE_NXM;
ubmap[uba][addr] = (int32) val & UMAP_MASK;
return SCPE_OK;
}

t_stat uba_reset (DEVICE *dptr)
{
int32 i, uba;

int_req = 0;
for (uba = 0; uba < UBANUM; uba++) {
    ubcs[uba] = 0;
    for (i = 0; i < UMAP_MEMSIZE; i++)
        ubmap[uba][i] = 0;
    }
pi_eval ();
return SCPE_OK;
}

/* Change device address */

t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newba;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if ((val == 0) || (uptr == NULL))
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newba = (uint32) get_uint (cptr, 8, PAMASK, &r);        /* get new */
if ((r != SCPE_OK) || (newba == dibp->ba))
    return r;
if (GET_IOUBA (newba) != GET_IOUBA (dibp->ba))
    return SCPE_ARG;
if (newba % ((uint32) val))                             /* check modulus */
    return SCPE_ARG;
dibp->ba = newba;                                       /* store */
return SCPE_OK;
}

/* Show device address */

t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->ba <= IOPAGEBASE))
    return SCPE_IERR;
fprintf (st, "address=%07o", dibp->ba);
if (dibp->lnt > 1)
    fprintf (st, "-%07o", dibp->ba + dibp->lnt - 1);
return SCPE_OK;
}

/* Change device vector */

t_stat set_vec (UNIT *uptr, int32 arg, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newvec;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newvec = (uint32) get_uint (cptr, 8, VEC_Q + 01000, &r);
if ((r != SCPE_OK) || (newvec == VEC_Q) ||
    ((newvec + (dibp->vnum * 4)) >= (VEC_Q + 01000)) ||
    (newvec & ((dibp->vnum > 1)? 07: 03)))
    return SCPE_ARG;
dibp->vec = newvec;
return SCPE_OK;
}

/* Show device vector */

t_stat show_vec (FILE *st, UNIT *uptr, int32 arg, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 vec, numvec;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
vec = dibp->vec;
if (arg)
    numvec = arg;
else numvec = dibp->vnum;
if (vec == 0)
    fprintf (st, "no vector");
else {
    fprintf (st, "vector=%o", vec);
    if (numvec > 1)
        fprintf (st, "-%o", vec + (4 * (numvec - 1)));
    }
return SCPE_OK;
}

/* Show vector for terminal multiplexor */

t_stat show_vec_mux (FILE *st, UNIT *uptr, int32 arg, void *desc)
{
TMXR *mp = (TMXR *) desc;

if ((mp == NULL) || (arg == 0))
    return SCPE_IERR;
return show_vec (st, uptr, ((mp->lines * 2) / arg), desc);
}

/* Test for conflict in device addresses */

t_bool dev_conflict (DIB *curr)
{
uint32 i, end;
DEVICE *dptr;
DIB *dibp;

end = curr->ba + curr->lnt - 1;                         /* get end */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((dibp == NULL) || (dibp == curr) ||
        (dptr->flags & DEV_DIS))
        continue;
    if (((curr->ba >= dibp->ba) &&                      /* overlap start? */
        (curr->ba < (dibp->ba + dibp->lnt))) ||
        ((end >= dibp->ba) &&                           /* overlap end? */
        (end < (dibp->ba + dibp->lnt)))) {
        printf ("Device %s address conflict at %08o\n",
            sim_dname (dptr), dibp->ba);
        if (sim_log)
            fprintf (sim_log, "Device %s address conflict at %08o\n",
                     sim_dname (dptr), dibp->ba);
        return TRUE;
        }
    }
return FALSE;
}

/* Build interrupt tables */

void build_int_vec (int32 vloc, int32 ivec, int32 (*iack)(void) )
{
if (iack != NULL)
    int_ack[vloc] = iack;
else int_vec[vloc] = ivec;
return;
}

/* Build dib_tab from device list */

t_bool build_dib_tab (void)
{
int32 i, j, k;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < 32; i++) {                              /* clear intr tables */
    int_vec[i] = 0;
    int_ack[i] = NULL;
	}
for (i = j = 0; (dptr = sim_devices[i]) != NULL; i++) { /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* defined, enabled? */
        if (dibp->vnum > VEC_DEVMAX)
            return SCPE_IERR;
        for (k = 0; k < dibp->vnum; k++)                /* loop thru vec */
            build_int_vec (dibp->vloc + k,              /* add vector */
                dibp->vec + (k * 4), dibp->ack[k]);
        if (dibp->lnt != 0) {                           /* I/O addresses? */
            dib_tab[j++] = dibp;                        /* add DIB to dib_tab */
            if (j >= DIB_MAX)                           /* too many? */
                return SCPE_IERR;
            }   
        }                                               /* end if enabled */
    }                                                   /* end for */
for (i = 0; (dibp = std_dib[i]) != NULL; i++) {         /* loop thru std */
    dib_tab[j++] = dibp;                                /* add to dib_tab */
    if (j >= DIB_MAX)                                   /* too many? */
        return SCPE_IERR;
    }
dib_tab[j] = NULL;                                      /* end with NULL */
for (i = 0; (dibp = dib_tab[i]) != NULL; i++) {         /* test built dib_tab */
    if (dev_conflict (dibp))                            /* for conflicts */
        return SCPE_STOP;
    }
return SCPE_OK;
}

/* Show dib_tab */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, j, done = 0;
DEVICE *dptr;
DIB *dibt;

build_dib_tab ();                                       /* build table */
while (done == 0) {                                     /* sort ascending */
    done = 1;                                           /* assume done */
    for (i = 0; dib_tab[i + 1] != NULL; i++) {          /* check table */
        if (dib_tab[i]->ba > dib_tab[i + 1]->ba) {      /* out of order? */
            dibt = dib_tab[i];                          /* interchange */
            dib_tab[i] = dib_tab[i + 1];
            dib_tab[i + 1] = dibt;
            done = 0;                                   /* not done */
            }
        }
    }                                                   /* end while */
for (i = 0; dib_tab[i] != NULL; i++) {                  /* print table */
    for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
        if (((DIB*) sim_devices[j]->ctxt) == dib_tab[i]) {
            dptr = sim_devices[j];
            break;
            }
        }
    fprintf (st, "%07o - %07o\t%s\n", dib_tab[i]->ba,
            dib_tab[i]->ba + dib_tab[i]->lnt - 1,
            dptr? sim_dname (dptr): "CPU");
    }
return SCPE_OK;
}

/* Stub auto-configure */

t_stat auto_config (char *name, int32 num)
{
return SCPE_OK;
}

/* Stub floating address */

t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
return SCPE_OK;
}
