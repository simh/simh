/* pdp18b_dr15.c: DR15C simulator

   Copyright (c) 2016-2020, Robert M Supnik

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

   dr           PDP-15 DR15C interface for UC15 system

   04-Jul 20    RMS     Zero out shared state on first allocation

   The DR15C provides control communications with the DR11Cs in the UC15.
   Its state consists of an 18b register (the Task Control Block Pointer),
   a one bit flag (TCBP acknowledge, not wired to interrupt), four interrupt
   requests wired to API, four interrupt vectors for the API levels, and an
   API interrupt enable/disable flag.

   The PDP15 and UC15 use a master/save communications protocol.
   - The PDP15 initiates a request to the PDP11 by writing TCBP and
     clearing TCBP acknowledge. This alerts/interrupts the PDP11.
   - The PDP11 reads TCBP. This sets TCBP acknowledge.
   - The PDP11 processes the request.
   - The PDP11 signals completion by writing a vector into one of
     four API request levels.
   - The PDP15 is interrupted, and the request is considered complete.

   The DR15 must "call out" to the UC15 to signal two conditions:
   - a new TCBP has been written
   - API requests have been updated

   The UC15 must "call in" to the DR15 for two reasons:
   - the TCBP has been read
   - an API interrupt is requested

   The DR15 and UC15 use a shared memory section and ATOMIC operations
   to communicate. Shared state is maintained in shared memory, with one
   side having read/write access, the other read-only. Actions are
   implemented by setting signals with an atomic compare-and-swap.
   The signals may be polled with non-atomic operations but must be
   verified with an atomic compare-and-swap.

   Debug hooks - when DEBUG is turned on, the simulator will print
   information relating to PIREX operation.
*/

#include "pdp18b_defs.h"
#include "uc15_defs.h"

/* Declarations */

extern int32 int_hwre[API_HLVL+1];
extern int32 api_vec[API_HLVL][32];
extern int32 *M;
extern UNIT cpu_unit;

uint32 dr15_tcbp = 0;                                   /* buffer = TCB ptr */
int32 dr15_tcb_ack = 0;                                 /* TCBP write ack */
int32 dr15_ie = 0;                                      /* int enable */
uint32 dr15_int_req = 0;                                /* int req 0-3 */
int32 dr15_poll = 3;                                    /* polling interval */
SHMEM *uc15_shmem = NULL;                               /* shared state identifier */
int32 *uc15_shstate = NULL;                             /* shared state base */
SHMEM *pdp15_shmem = NULL;                              /* PDP15 mem identifier */

int32 dr60 (int32 dev, int32 pulse, int32 AC);
int32 dr61 (int32 dev, int32 pulse, int32 AC);
t_stat dr15_reset (DEVICE *dptr);
void dr15_set_clr_ie (int32 val);
t_stat dr15_svc (UNIT *uptr);
t_stat dr15_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat dr15_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat dr15_attach (UNIT *uptr, CONST char *cptr);
t_stat dr15_detach (UNIT *uptr);

t_stat uc15_new_api (int32 val);                         /* callouts */
t_stat uc15_tcbp_wr (int32 val);

/* DR15 data structures

   dr15_dev       DR15 device descriptor
   dr15_unit      DR15 unit descriptor
   dr15_reg       DR15 register list
*/

DIB dr15_dib = { DEV_DR, 2 ,NULL, { &dr60, &dr61 } };

UNIT dr15_unit = {
    UDATA (&dr15_svc, UNIT_FIX+UNIT_BINK+UNIT_ATTABLE, UC15_STATE_SIZE)
    };

REG dr15_reg[] = {
    { ORDATA (TCBP, dr15_tcbp, ADDRSIZE) },
    { FLDATA (TCBACK, dr15_tcb_ack, 0) },
    { FLDATA (IE, dr15_ie, 0) },
    { ORDATA (REQ, dr15_int_req, 4) },
    { FLDATA (API0, int_hwre[API_DR0], INT_V_DR) },
    { FLDATA (API1, int_hwre[API_DR1], INT_V_DR) },
    { FLDATA (API2, int_hwre[API_DR2], INT_V_DR) },
    { FLDATA (API3, int_hwre[API_DR3], INT_V_DR) },
    { ORDATA (APIVEC0, api_vec[API_DR0][INT_V_DR], 7) },
    { ORDATA (APIVEC1, api_vec[API_DR1][INT_V_DR], 7) },
    { ORDATA (APIVEC2, api_vec[API_DR2][INT_V_DR], 7) },
    { ORDATA (APIVEC3, api_vec[API_DR3][INT_V_DR], 7) },
    { DRDATA (POLL, dr15_poll, 10), REG_NZ },
    { ORDATA (DEVNO, dr15_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB dr15_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", NULL, &show_devno },
    { 0 }
    };

DEVICE dr15_dev = {
    "DR", &dr15_unit, dr15_reg, dr15_mod,
    1, 8, 10, 1, 8, 32,
    &dr15_ex, &dr15_dep, &dr15_reset,
    NULL, &dr15_attach, &dr15_detach,
    &dr15_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG
    };

/* IOT routines */

int32 dr60 (int32 dev, int32 pulse, int32 AC)
{
if (((pulse & 01) != 0) && (dr15_tcb_ack != 0))         /* SIOA */
    AC |= IOT_SKP;
if ((pulse & 02) != 0)                                  /* CIOP */
    dr15_tcb_ack = 0;
if ((pulse & 04) != 0) {                                /* LIOR */
    dr15_tcbp = AC & AMASK;                             /* top bit zero */
    uc15_tcbp_wr (dr15_tcbp);                           /* inform UC15 */
    }
return AC;
}

int32 dr61 (int32 dev, int32 pulse, int32 AC)
{
int32 subdev = (pulse >> 4) & 03;

if (pulse & 01) {                                       /* SAPIn */
    if (((dr15_int_req >> subdev) & 01) != 0)
        AC = AC | IOT_SKP;
    }
if (pulse & 02) {
    if (subdev == 0)                                    /* RDRS */
        AC |= dr15_ie;
    else if (subdev == 1)
        dr15_set_clr_ie (AC & 1);
    }
if (pulse & 04) {                                       /* CAPI */
    int32 old_int_req = dr15_int_req;
    dr15_int_req &= ~(1 << subdev);                     /* clear local req */
    int_hwre[subdev] &= ~INT_DR;                        /* clear hwre req */
    if (dr15_int_req != old_int_req)                    /* state change? */
        uc15_new_api (dr15_int_req);                    /* inform UC15 */
    }
return AC;
}

/* Set/clear interrupt enable */

void dr15_set_clr_ie (int32 val)
{
int32 i;

dr15_ie = val;
for (i = 0; i < 4; i++) {
    if ((dr15_ie != 0) && (((dr15_int_req >> i) & 01) != 0))
        int_hwre[i] |= INT_DR;
     else int_hwre[i] &= ~INT_DR;
     }
return;
}

/* Routines to inform UC15 of state changes */

t_stat uc15_new_api (int32 req)
{
UC15_SHARED_WR (UC15_API_SUMM, req);                    /* new value */
UC15_ATOMIC_CAS (UC15_API_UPD, 0, 1);                   /* signal UC15 */
return SCPE_OK;
}

t_stat uc15_tcbp_wr (int32 tcbp)
{
UC15_SHARED_WR (UC15_TCBP, tcbp);                       /* new value */
UC15_ATOMIC_CAS (UC15_TCBP_WR, 0, 1);                   /* signal UC15 */
if (DEBUG_PRS (dr15_dev)) {
    uint32 apiv, apil, fnc, tsk;
    t_bool spl;

    apiv = (M[tcbp] >> 8) & 0377;
    apil = M[tcbp] & 0377;
    fnc = (M[tcbp + 1] >> 8) & 0377;
    spl = (M[tcbp + 1] & 0200) != 0;
    tsk = (M[tcbp + 1] & 0177);
    fprintf (sim_deb, ">> DR15: TCB write, API = %o/%d, fnc = %o, %s task = %o, eventvar = %o\n",
        apiv, apil, fnc, spl? "Spooled": "Unspooled", tsk, M[tcbp + 2]);
    fprintf (sim_deb, "Additional parameters = %o %o %o %o %o\n",
        M[tcbp + 3], M[tcbp + 4], M[tcbp + 5], M[tcbp + 6], M[tcbp + 7]);
    }
return SCPE_OK;
}

/* Routine to poll for state changes from UC15 */

t_stat dr15_svc (UNIT *uptr)
{
int32 i, t;
uint32 old_int_req = dr15_int_req;

t = UC15_SHARED_RD (UC15_TCBP_RD);                      /* TCBP read? */
if ((t != 0) && UC15_ATOMIC_CAS (UC15_TCBP_RD, 1, 0))   /* for real? clear */
    dr15_tcb_ack = 1;                                   /* set ack */
for (i = 0; i < 4; i++) {                               /* API req */
    t = UC15_SHARED_RD (UC15_API_REQ + (i * UC15_API_VEC_MUL));
    if ((t != 0) &&                                     /* API req? for real? */
        UC15_ATOMIC_CAS (UC15_API_REQ + (i * UC15_API_VEC_MUL), 1, 0)) {
        api_vec[i][INT_V_DR] = UC15_SHARED_RD (UC15_API_VEC + (i * UC15_API_VEC_MUL)) & 0177;
        dr15_int_req |= (1u << i);
        if (dr15_ie != 0)
            int_hwre[i] |= INT_DR;
        if (DEBUG_PRS (dr15_dev))
            fprintf (sim_deb, ">>DR15: API request, API = %o/%d\n",
                api_vec[i][INT_V_DR], i);
        }                                               /* end if changed */
    }                                                   /* end for */
if (dr15_int_req != old_int_req)                        /* changes? */
    uc15_new_api (dr15_int_req);                        /* inform UC15 */
sim_activate (uptr, dr15_poll);                         /* next poll */
return SCPE_OK;
}

/* Reset routine

   Aside from performing a device reset, this routine sets up shared
   UC15 state and shared PDP15 main memory. It also writes the size
   of PDP15 main memory (in PDP11 bytes) into the shared state region.
*/

t_stat dr15_reset (DEVICE *dptr)
{
int32 i;
t_stat r;
void *basead;

dr15_int_req = 0;                                       /* clear API req */
dr15_ie = 1;                                            /* IE inits to 1 */
dr15_tcb_ack = 1;                                       /* TCBP ack inits to 1 */
dr15_int_req = 0;
for (i = 0; i < 4; i++) {                               /* clear intr and vectors */
    int_hwre[i] &= ~INT_DR;
    api_vec[i][INT_V_DR] = 0;
    }
sim_cancel (dptr->units);
if ((dptr->flags & DEV_DIS) != 0)                       /* disabled? */
    return SCPE_OK;

if (uc15_shmem == NULL) {                               /* allocate shared state */
    r = sim_shmem_open ("UC15SharedState", UC15_STATE_SIZE * sizeof (int32), &uc15_shmem, &basead);
    if (r != SCPE_OK)
        return r;
    uc15_shstate = (int32 *) basead;
    for (i = 0; i < UC15_STATE_SIZE; i++) {             /* zero out shared state region */
        UC15_SHARED_WR (i, 0);
        }
    }
if (pdp15_shmem == NULL) {                              /* allocate shared memory */
    r = sim_shmem_open ("PDP15MainMemory", MAXMEMSIZE * sizeof (int32), &pdp15_shmem, &basead);
    if (r != SCPE_OK)
        return r;
    free (M);                                           /* release normal memory */
    M = (int32 *) basead;
    }
UC15_SHARED_WR (UC15_PDP15MEM, cpu_unit.capac << 1);    /* write mem size to shared state */
uc15_new_api (dr15_int_req);                            /* inform UC15 of new API (and mem) */
sim_activate (dptr->units, dr15_poll);                  /* start polling */
return SCPE_OK;
}

/* Shared state ex/mod routines for debug */

t_stat dr15_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= UC15_STATE_SIZE)
    return SCPE_NXM;
if (vptr != NULL) {
    if (uc15_shmem != NULL)
        *vptr = UC15_SHARED_RD ((int32) addr);
    else *vptr = 0;
    }
return SCPE_OK;
}

t_stat dr15_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= UC15_STATE_SIZE)
    return SCPE_NXM;
if (uc15_shmem != NULL)
    UC15_SHARED_WR ((int32) addr, (int32) val);
return SCPE_OK;
}

/* Fake attach routine to kill attach attempts */

t_stat dr15_attach (UNIT *uptr, CONST char *cptr)
{
return SCPE_NOFNC;
}

/* Shutdown detach routine to release shared memories */

t_stat dr15_detach (UNIT *uptr)
{
if ((sim_switches & SIM_SW_SHUT) == 0)                  /* only if shutdown */
    return SCPE_NOFNC;
sim_shmem_close (uc15_shmem);                           /* release shared state */
sim_shmem_close (pdp15_shmem);                          /* release shared mem */
return SCPE_OK;
}

