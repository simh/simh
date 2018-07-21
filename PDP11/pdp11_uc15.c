/* pdp11_uc15.c: UC15 interface simulator

   Copyright (c) 2016, Robert M Supnik

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

   uca           DR11 #1
   ucb           DR11 #2

   The DR11Cs provide control communications with the DR15C in the PDP15.

   The PDP15 and UC15 use a master/slave communications protocol.
   - The PDP15 initiates a request to the PDP11 by writing TCBP and
     clearing TCBP acknowledge. This alerts/interrupts the PDP11.
   - The PDP11 reads TCBP. This sets TCBP acknowledge, which is
     not wired to interrupt on the PDP15. Note that TCBP has been
     converted from a word address to a byte address by the way
     the two systems are wired together.
   - The PDP11 processes the request.
   - The PDP11 signals completion by writing a vector into one of
     four API request levels.
   - The PDP15 is interrupted, and the request is considered complete.

   The UC15 must "call out" to the PDP15 to signal two conditions:
   - the TCB pointer has been read
   - an API interrupt is requested

   The DR15 must "call in" to the UC15 for two reasons:
   - the TCBP has been written
   - API interrupt status has changed

   The DR15 and UC15 use a shared memory section and ATOMIC operations
   to communicate. Shared state is maintained in shared memory, with one
   side having read/write access, the other read-only. Actions are
   implemented by setting signals with an atomic compare-and-swap.
   The signals may be polled with non-atomic operations but must be
   verified with an atomic compare-and-swap.
*/

#include "pdp11_defs.h"

#include "sim_fio.h"
#include "uc15_defs.h"

/* Constants */

/* DR11 #1 */

#define UCAC_APID           (CSR_DONE)
#define UCAB_V_TCBHI        0
#define UCAB_M_TCBHI        03
#define UCAB_API2           0000100
#define UCAB_API0           0000200
#define UCAB_V_LOCAL        8
#define UCAB_M_LOCAL        07
#define UCAB_API3           0040000
#define UCAB_API1           0100000

/* DR11 #2 */

#define UCBC_NTCB           (CSR_DONE)

/* Declarations */

extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;
extern uint16 *M;

int32 uca_csr = 0;                                      /* DR11C #1 CSR */
int32 uca_buf = 0;                                      /* DR11C #1 input buffer */
int32 ucb_csr = 0;
int32 ucb_buf = 0;
int32 uc15_poll = 3;                                    /* polling interval */
SHMEM *uc15_shmem = NULL;                               /* shared state identifier */
int32 *uc15_shstate = NULL;                             /* shared state base */
SHMEM *pdp15_shmem = NULL;                              /* PDP15 mem identifier */
int32 *pdp15_mem = NULL;
uint32 uc15_memsize = 0;

t_stat uca_rd (int32 *data, int32 PA, int32 access);
t_stat uca_wr (int32 data, int32 PA, int32 access);
t_stat ucb_rd (int32 *data, int32 PA, int32 access);
t_stat ucb_wr (int32 data, int32 PA, int32 access);
t_stat uc15_reset (DEVICE *dptr);
t_stat uc15_svc (UNIT *uptr);
t_stat uc15_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat uc15_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat uc15_attach (UNIT *uptr, CONST char *cptr);
t_stat uc15_detach (UNIT *uptr);

void uc15_set_memsize (void);
int32 uc15_get_uca_buf (void);
t_stat uc15_api_req (int32 lvl, int32 vec);

/* UC15 data structures

   uca_dev, ucb_dev       UC15 device descriptor
   uca_unit, ucb_unit     UC15 unit descriptor
   uca_reg, ucb reg       UC15 register list

   The two DR11Cs must be separate devices because they interrupt at
   different IPLs and must have different DIBs!
*/

DIB uca_dib = {
    IOBA_UCA, IOLN_UCA, &uca_rd, &uca_wr,
    1, IVCL (UCA), VEC_UCA, { NULL }
    };

UNIT uca_unit = { UDATA (&uc15_svc, 0, UNIT_ATTABLE) };

REG uca_reg[] = {
    { ORDATA (CSR, uca_csr, 16) },
    { ORDATA (BUF, uca_buf, 16) },
    { FLDATA (APID, uca_csr, CSR_V_DONE) },
    { FLDATA (IE, uca_csr, CSR_V_IE) },
    { DRDATA (POLL, uc15_poll, 10), REG_NZ },
    { DRDATA (UCMEMSIZE, uc15_memsize, 18), REG_HRO },
    { NULL }
    };

MTAB uc15_mod[] = {
    { MTAB_XTD|MTAB_VDV, 006, "ADDRESS", "ADDRESS",
      NULL, &show_addr, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
      NULL, &show_vec, NULL },
    { 0 }
    };

DEVICE uca_dev = {
    "UCA", &uca_unit, uca_reg, uc15_mod,
    1, 8, 10, 1, 8, 32,
    &uc15_ex, &uc15_dep, &uc15_reset,
    NULL, &uc15_attach, &uc15_detach,
    &uca_dib, DEV_DISABLE | DEV_DEBUG
    };

DIB ucb_dib = {
    IOBA_UCB, IOLN_UCB, &ucb_rd, &ucb_wr,
    1, IVCL (UCB), VEC_UCB, { NULL }
    };

UNIT ucb_unit = { UDATA (NULL, 0, 0) };

REG ucb_reg[] = {
    { ORDATA (CSR, ucb_csr, 16) },
    { ORDATA (BUF, ucb_buf, 16) },
    { FLDATA (NTCB, ucb_csr, CSR_V_DONE) },
    { FLDATA (IE, ucb_csr, CSR_V_IE) },
    { NULL }
    };

DEVICE ucb_dev = {
    "UCB", &ucb_unit, ucb_reg, uc15_mod,
    1, 8, 18, 1, 8, 18,
    NULL, NULL, NULL,
    NULL, NULL, NULL,
    &ucb_dib, DEV_DISABLE
    };

/* IO routines */

/* DR11 #1 */

t_stat uca_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

case 0:                                                 /* CSR */
    *data = uca_csr;
    return SCPE_OK;

case 1:                                                 /* output buffers */
    return SCPE_OK;

case 2:                                                 /* input buffer */
    *data = uc15_get_uca_buf ();                        /* assemble buffer */
    return SCPE_OK;
    }

return SCPE_NXM;
}

t_stat uca_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

case 0:                                                 /* CSR */
    if (PA & 1)
        return SCPE_OK;
    if ((data & CSR_IE) == 0)
        CLR_INT (UCA);
    else if ((uca_csr & (UCAC_APID + CSR_IE)) == UCAC_APID)
        SET_INT (UCA);
    uca_csr = (uca_csr & ~CSR_IE) | (data & CSR_IE);
    return SCPE_OK;

case 1:                                                 /* output buffer */
    if (PA & 1)                                         /* odd byte? API 1 */
        uc15_api_req (1, data & 0377);
    else {
        if (access == WRITE)                            /* full word? API 1 */
            uc15_api_req (1, (data >> 8) & 0377);
        uc15_api_req (0, data & 0377);                  /* API 0 */
        }
    return SCPE_OK;

case 2:
    return SCPE_OK;
    }

return SCPE_NXM;
}

t_stat ucb_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

case 0:                                                 /* CSR */
    *data = ucb_csr;
    return SCPE_OK;

case 1:                                                 /* output buffers */
    return SCPE_OK;

case 2:                                                 /* input buffer */
     *data = ucb_buf = (UC15_SHARED_RD (UC15_TCBP) << 1) & 0177777;
     ucb_csr &= ~UCBC_NTCB;                             /* clear TCBP rdy */
     CLR_INT (UCB);                                     /* clear int */
     UC15_ATOMIC_CAS (UC15_TCBP_RD, 0, 1);              /* send ACK */
     if (DEBUG_PRS (uca_dev)) {
        uint32 apiv, apil, fnc, tsk, pa;
        t_bool spl;

        pa = ucb_buf + MEMSIZE;
        apiv = RdMemB (pa);
        apil = RdMemB (pa + 1);
        fnc = RdMemB (pa + 2);
        spl = (RdMemB (pa + 3) & 0200) != 0;
        tsk = RdMemB (pa + 3) & 0177;
        fprintf (sim_deb, ">> UC15: TCB rcvd, API = %o/%d, fnc = %o, %s task = %o, eventvar = %o\n",
            apiv, apil, fnc, spl? "Spooled": "Unspooled", tsk, RdMemW (pa + 4));
        fprintf (sim_deb, "Additional parameters = %o %o %o %o %o\n",
            RdMemW (pa + 6), RdMemW (pa + 8), RdMemW (pa + 10), RdMemW (pa + 12), RdMemW (pa + 14));
        }
     return SCPE_OK;
     }

return SCPE_NXM;
}     

t_stat ucb_wr (int32 data, int32 PA, int32 access)
{
switch ((PA >> 1) & 03) {                               /* case on PA<2:1> */

case 0:                                                 /* CSR */
    if (PA & 1)
        return SCPE_OK;
    if ((data & CSR_IE) == 0)                           /* IE = 0? */
        CLR_INT (UCB);
    else if ((ucb_csr & (UCBC_NTCB + CSR_IE)) == UCBC_NTCB)
        SET_INT (UCB);
    ucb_csr = (ucb_csr & ~CSR_IE) | (data & CSR_IE);
    return SCPE_OK;

case 1:                                                 /* output buffer */
    if (PA & 1)                                         /* odd byte? API 3*/
        uc15_api_req (3, data & 0377);
    else {
        if (access == WRITE)                            /* full word? API 3 */
            uc15_api_req (3, (data >> 8) & 0377);
        uc15_api_req (2, data & 0377);                  /* API 2 */
        }
    return SCPE_OK;

case 2:
    return SCPE_OK;
    }

return SCPE_NXM;
}

/* Request PDP15 to take an API interrupt */

t_stat uc15_api_req (int32 lvl, int32 vec)
{
UC15_SHARED_WR (UC15_API_VEC + (lvl * UC15_API_VEC_MUL), vec);
UC15_ATOMIC_CAS (UC15_API_REQ + (lvl * UC15_API_VEC_MUL), 0, 1);
if (DEBUG_PRS (uca_dev))
    fprintf (sim_deb, ">>UC15: API request sent, API = %o/%d\n",
                vec, lvl);
return SCPE_OK;
}

/* Routine to poll for state changes from PDP15 */

t_stat uc15_svc (UNIT *uptr)
{
uint32 t;

t = UC15_SHARED_RD (UC15_TCBP_WR);                      /* TCBP written? */
if ((t != 0) && UC15_ATOMIC_CAS (UC15_TCBP_WR, 1, 0)) { /* for real? */
    ucb_csr |= UCBC_NTCB;                               /* set new TCB flag */
    if (ucb_csr & CSR_IE)
        SET_INT (UCB);
    uc15_set_memsize ();                                /* update mem size */
    }
t = UC15_SHARED_RD (UC15_API_UPD);                      /* API update? */
if ((t != 0) && UC15_ATOMIC_CAS (UC15_API_UPD, 1, 0)) { /* for real? */
    uc15_get_uca_buf ();                                /* update UCA buf */
    }
sim_activate (uptr, uc15_poll);                         /* next poll */
return SCPE_OK;
}

/* Routine to assemble/update uca_buf

  Note that the PDP-15 and PDP-11 have opposite interpretations of
  API requests. On the PDP-15, a "1" indicates an active request.
  On the PDP-11, a "1" indicates request done (API inactive).
*/

int32 uc15_get_uca_buf (void)
{
int32 i, t;
static int32 ucab_api[4] =
    { UCAB_API0, UCAB_API1, UCAB_API2, UCAB_API3 };

t = UC15_SHARED_RD (UC15_TCBP);                         /* get TCB ptr */
uca_buf = (t >> 15) & UCAB_M_TCBHI;                     /* PDP15 bits<1:2> */
t = cpu_unit.capac >> 13;                               /* local mem in 4KW */
uca_buf |= ((t & UCAB_M_LOCAL) << UCAB_V_LOCAL);
t = UC15_SHARED_RD (UC15_API_SUMM);                     /* get API summary */
for (i = 0; i < 4; i++) {                               /* check 0..3 */
    if (((t >> i) & 1) == 0)                            /* level inactive? */
        uca_buf |= ucab_api[i];                          /* set status bit */
    }
if ((t == 0) && ((uca_csr & UCAC_APID) == 0)) {         /* API req now 0? */
    uca_csr |= UCAC_APID;                               /* set flag */
    if ((uca_csr & CSR_IE) != 0)                        /* if ie, req int */
        SET_INT (UCA);
    }
return uca_buf;
}

/* Routine to set overall memory limit for UC15 checking */

void uc15_set_memsize (void)
{
uint32 t = UC15_SHARED_RD (UC15_PDP15MEM);              /* get PDP15 memory size */
if (t == 0)                                             /* PDP15 not running? */
    t = PDP15_MAXMEM * 2;                               /* max mem in bytes */
uc15_memsize = t + MEMSIZE;                             /* shared + local mem */
if (uc15_memsize > (UNIMEMSIZE - IOPAGESIZE))           /* more than 18b? */
    uc15_memsize = UNIMEMSIZE - IOPAGESIZE;             /* limit */
return;
}

/* Reset routine

   Aside from performing a device reset, this routine sets up shared
   UC15 state and shared PDP15 main memory. It also reads the size
   of PDP15 main memory (in PDP11 bytes) from the shared state region.
*/

t_stat uc15_reset (DEVICE *dptr)
{
t_stat r;
void *basead;

uca_csr = 0;
uca_buf = 0;
ucb_csr = 0;
ucb_buf = 0;
CLR_INT (UCA);
CLR_INT (UCB);
if (uc15_shmem == NULL) {                               /* allocate shared state */
    r = sim_shmem_open ("UC15SharedState", UC15_STATE_SIZE * sizeof (int32), &uc15_shmem, &basead);
    if (r != SCPE_OK)
        return r;
    uc15_shstate = (int32 *) basead;
    }
if (pdp15_shmem == NULL) {                              /* allocate shared memory */
    r = sim_shmem_open ("PDP15MainMemory", PDP15_MAXMEM * sizeof (int32), &pdp15_shmem, &basead);
    if (r != SCPE_OK)
        return r;
    pdp15_mem = (int32 *) basead;
    }
uc15_set_memsize ();
sim_activate (dptr->units, uc15_poll);                  /* start polling */
return SCPE_OK;
}

/* Shared state ex/mod routines for debug */

t_stat uc15_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= UC15_STATE_SIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = UC15_SHARED_RD ((int32) addr);
return SCPE_OK;
}

t_stat uc15_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= UC15_STATE_SIZE)
    return SCPE_NXM;
UC15_SHARED_WR ((int32) addr, (int32) val);
return SCPE_OK;
}

/* Fake attach routine to kill attach attempts */

t_stat uc15_attach (UNIT *uptr, CONST char *cptr)
{
return SCPE_NOFNC;
}

/* Shutdown detach routine to release shared memories */

t_stat uc15_detach (UNIT *uptr)
{
if ((sim_switches & SIM_SW_SHUT) == 0)                  /* only if shutdown */
    return SCPE_NOFNC;
sim_shmem_close (uc15_shmem);                           /* release shared state */
sim_shmem_close (pdp15_shmem);                          /* release shared mem */
return SCPE_OK;
}

/* Physical read/write memory routines
   Used by CPU and IO devices
   Physical address is known to be legal
   We can use MEMSIZE rather than cpu_memsize because configurations
   were limited to 16KW of local memory
   8b and 16b writes clear the upper 2b of PDP-15 memory
*/

int32 uc15_RdMemW (int32 pa)
{
if (((uint32) pa) < MEMSIZE)
    return M[pa >> 1];
else {
    pa = pa - MEMSIZE;
    return (pdp15_mem[pa >> 1] & DMASK);
    }
}

int32 uc15_RdMemB (int32 pa)
{
if (((uint32) pa) < MEMSIZE)
    return ((pa & 1)? (M[pa >> 1] >> 8): (M[pa >> 1] & 0377));
else {
    pa = pa - MEMSIZE;
    return ((pa & 1)? (pdp15_mem[pa >> 1] >> 8): (pdp15_mem[pa >> 1] & 0377));
    }
}

void uc15_WrMemW (int32 pa, int32 d)
{
if (((uint32) pa) < MEMSIZE)
    M[pa >> 1] = d;
else {
    pa = pa - MEMSIZE;
    pdp15_mem[pa >> 1] = d & DMASK;
    }
return;
}

void uc15_WrMemB (int32 pa, int32 d)
{
if (((uint32) pa) < MEMSIZE)
    M[pa >> 1] = (pa & 1)?
         ((M[pa >> 1] & 0377) | ((d & 0377) << 8)): \
         ((M[pa >> 1] & ~0377) | (d & 0377));
else {
    pa = pa - MEMSIZE;
    pdp15_mem[pa >> 1] = (pa & 1)?
         ((pdp15_mem[pa >> 1] & 0377) | ((d & 0377) << 8)): \
         ((pdp15_mem[pa >> 1] & ~0377) | (d & 0377));
    }
return;
}

/* 18b DMA routines - physical only */

int32 Map_Read18 (uint32 ba, int32 bc, uint32 *buf)
{
uint32 alim, lim;

ba = (ba & UNIMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (lim < uc15_memsize)                                 /* end ok? */
    alim = lim;
else if (ba < uc15_memsize)                             /* no, strt ok? */
    alim = uc15_memsize;
else return bc;                                         /* no, err */
for ( ; ba < alim; ba = ba + 2) {                       /* by 18b words */
    if (ba < MEMSIZE)
        *buf++ = M[ba >> 1];
    else *buf++ = pdp15_mem[(ba - MEMSIZE) >> 1] & 0777777;
    }
return (lim - alim);
}

int32 Map_Write18 (uint32 ba, int32 bc, uint32 *buf)
{
uint32 alim, lim;

ba = (ba & UNIMASK) & ~01;                              /* trim, align addr */
lim = ba + (bc & ~01);
if (lim < uc15_memsize)                                 /* end ok? */
    alim = lim;
else if (ba < uc15_memsize)                              /* no, strt ok? */
    alim = uc15_memsize;
else return bc;                                         /* no, err */
for ( ; ba < alim; ba = ba + 2) {                       /* by 18 bit words */
    if (ba < MEMSIZE)
        M[ba >> 1] = *buf++ & DMASK;
    else pdp15_mem[(ba - MEMSIZE) >> 1] = *buf++ & 0777777;
    }
return (lim - alim);
}
