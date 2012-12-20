/* pdp11_io_lib.c: Unibus/Qbus common support routines

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
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif
#include "sim_sock.h"
#include "sim_tmxr.h"

extern FILE *sim_log;
extern DEVICE *sim_devices[];
extern int32 autcon_enb;
extern int32 int_vec[IPL_HLVL][32];
extern int32 (*int_ack[IPL_HLVL][32])(void);
extern t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
extern t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);

extern t_stat build_dib_tab (void);

static DIB *iodibp[IOPAGESIZE >> 1];

#define AUTO_MAXC       32              /* Maximum number of controllers */
#define AUTO_CSRBASE    0010
#define AUTO_CSRMAX    04000
#define AUTO_VECBASE    0300

/* Enable/disable autoconfiguration */

t_stat set_autocon (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr != NULL)
    return SCPE_ARG;
autcon_enb = val;
return auto_config (NULL, 0);
}

/* Show autoconfiguration status */

t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "autoconfiguration ");
fprintf (st, autcon_enb? "enabled": "disabled");
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
newba = (uint32) get_uint (cptr, DEV_RDX, IOPAGEBASE+IOPAGEMASK, &r); /* get new */
if (r != SCPE_OK)
    return r;
if ((newba <= IOPAGEBASE) ||                            /* > IO page base? */
    (newba % ((uint32) val)))                           /* check modulus */
    return SCPE_ARG;
dibp->ba = newba;                                       /* store */
autcon_enb = 0;                                         /* autoconfig off */
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
fprintf (st, "address=");
fprint_val (st, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
if (dibp->lnt > 1) {
    fprintf (st, "-");
    fprint_val (st, (t_value) dibp->ba + dibp->lnt - 1, DEV_RDX, 32, PV_LEFT);
    }
if (dibp->ba < IOPAGEBASE + AUTO_CSRBASE + AUTO_CSRMAX)
    fprintf (st, "*");
return SCPE_OK;
}

/* Set address floating */

t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;

if (cptr != NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
return auto_config (NULL, 0);                           /* autoconfigure */
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
newvec = (uint32) get_uint (cptr, DEV_RDX, VEC_Q + 01000, &r);
if ((r != SCPE_OK) || (newvec == VEC_Q) ||
    ((newvec + (dibp->vnum * 4)) >= (VEC_Q + 01000)) ||
    (newvec & ((dibp->vnum > 1)? 07: 03)))
    return SCPE_ARG;
dibp->vec = newvec;
autcon_enb = 0;                                         /* autoconfig off */
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
    fprintf (st, "vector=");
    fprint_val (st, (t_value) vec, DEV_RDX, 16, PV_LEFT);
    if (numvec > 1) {
        fprintf (st, "-");
        fprint_val (st, (t_value) vec + (4 * (numvec - 1)), DEV_RDX, 16, PV_LEFT);
        }
    }
if (vec >= VEC_Q + AUTO_VECBASE)
    fprintf (st, "*");
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

/* Init Unibus tables */

void init_ubus_tab (void)
{
size_t i, j;

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

if ((dptr == NULL) || (dibp == NULL))                   /* validate args */
    return SCPE_IERR;
if (dibp->vnum > VEC_DEVMAX)
    return SCPE_IERR;
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
        if (sim_log)
            fprintf (sim_log, "Device %s interrupt slot conflict at %d\n",
                     sim_dname (dptr), idx);
        return SCPE_STOP;
        }
    if (dibp->ack[i])
        int_ack[ilvl][ibit] = dibp->ack[i];
    else if (vec)
        int_vec[ilvl][ibit] = vec;
    }
for (i = 0; i < (int32) dibp->lnt; i = i + 2) {         /* create entries */
    idx = ((dibp->ba + i) & IOPAGEMASK) >> 1;           /* index into disp */
    if ((iodispR[idx] && dibp->rd &&                    /* conflict? */
        (iodispR[idx] != dibp->rd)) ||
        (iodispW[idx] && dibp->wr &&
        (iodispW[idx] != dibp->wr))) {
        printf ("Device %s address conflict at \n", sim_dname (dptr));
        fprint_val (stdout, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
        if (sim_log) {
            fprintf (sim_log, "Device %s address conflict at \n", sim_dname (dptr));
            fprint_val (sim_log, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
            }
        return SCPE_STOP;
        }
    if (dibp->rd)                                       /* set rd dispatch */
        iodispR[idx] = dibp->rd;
    if (dibp->wr)                                       /* set wr dispatch */
        iodispW[idx] = dibp->wr;
    iodibp[idx] = dibp;                                 /* remember DIB */
    }
return SCPE_OK;
}

/* Show IO space */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 i, j;
DEVICE *dptr;
DIB *dibp;

if (build_dib_tab ())                                   /* build IO page */
    return SCPE_OK;
for (i = 0, dibp = NULL; i < (IOPAGESIZE >> 1); i++) {  /* loop thru entries */
    if (iodibp[i] && (iodibp[i] != dibp)) {             /* new block? */
        dibp = iodibp[i];                               /* DIB for block */
        for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
            if (((DIB*) sim_devices[j]->ctxt) == dibp) {
                dptr = sim_devices[j];                  /* locate device */
                break;
                }                                       /* end if */
            }                                           /* end for j */
        fprint_val (st, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
        fprintf (st, " - ");
        fprint_val (st, (t_value) dibp->ba + dibp->lnt - 1, DEV_RDX, 32, PV_LEFT);
        fprintf (st, "%c\t%s\n",                        /* print block entry */
            (dibp->ba < IOPAGEBASE + AUTO_CSRBASE + AUTO_CSRMAX)? '*': ' ',
            dptr? sim_dname (dptr): "CPU");
        }                                               /* end if */
    }                                                   /* end for i */
return SCPE_OK;
}

/* Autoconfiguration

   The table reflects the MicroVAX 3900 microcode, with one field addition - the
   number of controllers field handles devices where multiple instances
   are simulated through a single DEVICE structure (e.g., DZ, VH).

   The table has been reviewed, extended and updated to reflect the contents of
   the auto configure table in VMS sysgen (V5.5-2)

   A minus number of vectors indicates a field that should be calculated
   but not placed in the DIB (RQ, TQ dynamic vectors)

   An amod value of 0 indicates that all addresses are FIXED
   An vmod value of 0 indicates that all vectors are FIXED */


typedef struct {
    char        *dnam[AUTO_MAXC];
    int32       numc;
    int32       numv;
    uint32      amod;
    uint32      vmod;
    uint32      fixa[AUTO_MAXC];
    uint32      fixv[AUTO_MAXC];
    } AUTO_CON;

AUTO_CON auto_tab[] = {/*c  #v  am vm  fxa   fxv */
    { { "QBA" },         1,  0,  0, 0, 
        {017500} },                                     /* doorbell - fx CSR, no VEC */
    { { "MCTL" },        1,  0,  0, 0, 
        {012100} },                                     /* MSV11-P - fx CSR, no VEC */
    { { "KE" },          1,  0,  0, 0, 
        {017300} },                                     /* KE11-A - fx CSR, no VEC */
    { { "KG" },          1,  0,  0, 0, 
        {010700} },                                     /* KG11-A - fx CSR, no VEC */
    { { "RHA", "RHB" },  1,  1,  0, 0, 
        {016700, 012440}, {0254, 0224} },               /* RH11/RH70 - fx CSR, fx VEC */
    { { "CLK" },         1,  0,  0, 0, 
        {017546}, {0100} },                             /* KW11L - fx CSR, fx VEC */
    { { "PTR" },         1,  1,  0, 0, 
        {017550}, {0070} },                             /* PC11 reader - fx CSR, fx VEC */
    { { "PTP" },         1,  1,  0, 0, 
        {017554}, {0074} },                             /* PC11 punch - fx CSR, fx VEC */
    { { "RK" },          1,  1,  0, 0, 
        {017400}, {0220} },                             /* RK11 - fx CSR, fx VEC */
    { { "TM" },          1,  1,  0, 0, 
        {012520}, {0224} },                             /* TM11 - fx CSR, fx VEC */
    { { "RC" },          1,  1,  0, 0, 
        {017440}, {0210} },                             /* RC11 - fx CSR, fx VEC */
    { { "RF" },          1,  1,  0, 0, 
        {017460}, {0204} },                             /* RF11 - fx CSR, fx VEC */
    { { "CR" },          1,  1,  0, 0, 
        {017160}, {0230} },                             /* CR11 - fx CSR, fx VEC */
    { { "HK" },          1,  1,  0, 0, 
        {017440}, {0210} },                             /* RK611 - fx CSR, fx VEC */
    { { "LPT" },         1,  1,  0, 0, 
        {017514, 004004, 004014, 004024, 004034}, 
        {0200,     0170,   0174,   0270,   0274} },     /* LP11 - fx CSR, fx VEC */
    { { "RB" },          1,  1,  0, 0, 
        {015606}, {0250} },                             /* RB730 - fx CSR, fx VEC */
    { { "RL" },          1,  1,  0, 0, 
        {014400}, {0160} },                             /* RL11 - fx CSR, fx VEC */
    { { "RL" },          1,  1,  0, 0, 
        {014400}, {0160} },                             /* RL11 - fx CSR, fx VEC */
    { { "DCI" }, DCX_LINES,  2,  0, 8, 
        {014000, 014010, 014020, 014030, 
         014040, 014050, 014060, 014070, 
         014100, 014110, 014120, 014130, 
         014140, 014150, 014160, 014170, 
         014200, 014210, 014220, 014230, 
         014240, 014250, 014260, 014270,
         014300, 014310, 014320, 014330, 
         014340, 014350, 014360, 014370} },             /* DC11 - fx CSRs */
    { { NULL },          1,  2,  0, 8, 
        {016500, 016510, 016520, 016530, 
         016540, 016550, 016560, 016570,
         016600, 016610, 016620, 016630,
         016640, 016650, 016660, 016670} },             /* TU58 - fx CSRs */
    { { NULL },          1,  1,  0, 4, 
        {015200, 015210, 015220, 015230, 
         015240, 015250, 015260, 015270,
         015300, 015310, 015320, 015330,
         015340, 015350, 015360, 015370} },             /* DN11 - fx CSRs */
    { { NULL },          1,  1,  0, 4, 
        {010500, 010510, 010520, 010530, 
         010540, 010550, 010560, 010570, 
         010600, 010610, 010620, 010630, 
         010640, 010650, 010660, 010670} },             /* DM11B - fx CSRs */
    { { NULL },          1,  2,  0, 8, 
        {007600, 007570, 007560, 007550, 
         007540, 007530, 007520, 007510,
         007500, 007470, 007460, 007450,
         007440, 007430, 007420, 007410} },             /* DR11C - fx CSRs */
    { { NULL },          1,  1,  0, 8, 
        {012600, 012604, 012610, 012614, 
         012620, 012624, 012620, 012624} },             /* PR611 - fx CSRs */
    { { NULL },          1,  1,  0, 8, 
        {017420, 017422, 017424, 017426, 
         017430, 017432, 017434, 017436} },             /* DT11 - fx CSRs */
    { { NULL },          1,  2,  0, 8,
      {016200, 016240} },                               /* DX11 */
    { { "DLI" }, DLX_LINES,  2,  0, 8, 
        {016500, 016510, 016520, 016530,
         016540, 016550, 016560, 016570,
         016600, 016610, 016620, 016630,
         016740, 016750, 016760, 016770} },             /* KL11/DL11/DLV11 - fx CSRs */
    { { NULL },          1,  2,  0, 8, { 0 } },         /* DLV11J - fx CSRs */
    { { NULL },          1,  2,  8, 8 },                /* DJ11 */
    { { NULL },          1,  2, 16, 8 },                /* DH11 */
    { { NULL },          1,  4,  0, 8,
      {012000, 012010, 012020, 012030} },               /* GT40 */
    { { NULL },          1,  2,  0, 8,
      {010400} },                                       /* LPS11 */
    { { NULL },          1,  2,  8, 8 },                /* DQ11 */
    { { NULL },          1,  2,  0, 8,
      {012400} },                                       /* KW11W */
    { { NULL },          1,  2,  8, 8 },                /* DU11 */
    { { NULL },          1,  2,  8, 8 },                /* DUP11 */
    { { NULL },          1,  3,  0, 8,
      {015000, 015040, 015100, 015140, }},              /* DV11 */
    { { NULL },          1,  2,  8, 8 },                /* LK11A */
    { { "DMC0", "DMC1", "DMC2", "DMC3" }, 
                         1,  2,  8, 8 },                /* DMC11 */
    { { "DZ" },   DZ_MUXES,  2,  8, 8 },                /* DZ11 */
    { { NULL },          1,  2,  8, 8 },                /* KMC11 */
    { { NULL },          1,  2,  8, 8 },                /* LPP11 */
    { { NULL },          1,  2,  8, 8 },                /* VMV21 */
    { { NULL },          1,  2, 16, 8 },                /* VMV31 */
    { { NULL },          1,  2,  8, 8 },                /* DWR70 */
    { { "RL", "RLB"},    1,  1,  8, 4, 
        {014400}, {0160} },                             /* RL11 */
    { { "TS", "TSB", "TSC", "TSD"}, 
                         1,  1,  0, 4,                  /* TS11 */
        {012520, 012524, 012530, 012534},
        {0224} },
    { { NULL },          1,  2, 16, 8,
        {010460} },                                     /* LPA11K */
    { { NULL },          1,  2,  8, 8 },                /* KW11C */
    { { NULL },          1,  1,  8, 8 },                /* reserved */
    { { "RX", "RY" },    1,  1,  8, 4, 
        {017170} , {0264} },                            /* RX11/RX211 */
    { { NULL },          1,  1,  8, 4 },                /* DR11W */
    { { NULL },          1,  1,  8, 4, 
        {012410, 012410}, {0124} },                     /* DR11B - fx CSRs,vec */
    { { "DMP" },         1,  2,  8, 8 },                /* DMP11 */
    { { NULL },          1,  2,  8, 8 },                /* DPV11 */
    { { NULL },          1,  2,  8, 8 },                /* ISB11 */
    { { NULL },          1,  2, 16, 8 },                /* DMV11 */
    { { "XU", "XUB" },   1,  1,  8, 4, 
        {014510}, {0120} },                             /* DEUNA */
    { { "XQ", "XQB" },   1, -1,  0, 4,
        {014440, 014460, 014520, 014540}, {0120} },     /* DEQNA */
    { { "RQ", "RQB", "RQC", "RQD" }, 
                         1, -1,  4, 4,                  /* RQDX3 */
        {012150}, {0154} },
    { { NULL },          1,  8, 32, 4 },                /* DMF32 */
    { { NULL },          1,  3, 16, 8 },                /* KMS11 */
    { { NULL },          1,  2,  0, 8,
        {004200, 004240, 004300, 004340} },             /* PLC11 */
    { { NULL },          1,  1, 16, 4 },                /* VS100 */
    { { "TQ", "TQB" },   1, -1,  4, 4, 
        {014500}, {0260} },                             /* TQK50 */
    { { NULL },          1,  2, 16, 8 },                /* KMV11 */
    { { NULL },          1,  2,  0, 8,
        {004400, 004440, 004500, 004540} },             /* KTC32 */
    { { NULL },          1,  2,  0, 8,
        {004100} },                                     /* IEQ11 */
    { { "VH" },   VH_MUXES,  2, 16, 8 },                /* DHU11/DHQ11 */
    { { NULL },          1,  6, 32, 4 },                /* DMZ32 */
    { { NULL },          1,  6, 32, 4 },                /* CP132 */
    { { NULL },          1,  1,  0, 0,
        {017340}, {0214} },                             /* TC11 */
    { { NULL },          1,  2, 64, 8, 
        {017200} },                                     /* QVSS - fx CSR */
    { { NULL },          1,  1,  8, 4 },                /* VS31 */
    { { NULL },          1,  1,  0, 4,
        {016200} },                                     /* LNV11 - fx CSR */
    { { NULL },          1,  1, 16, 4 },                /* LNV21/QPSS */
    { { NULL },          1,  1,  8, 4, 
        {012570} },                                     /* QTA - fx CSR */
    { { NULL },          1,  1,  8, 4 },                /* DSV11 */
    { { NULL },          1,  2,  8, 8 },                /* CSAM */
    { { NULL },          1,  2,  8, 8 },                /* ADV11C */
    { { NULL },          1,  0,  8, 8, 
        {010440} },                                     /* AAV11/AAV11C */
    { { NULL },          1,  2,  8, 8, 
        {016400}, {0140} },                             /* AXV11C - fx CSR,vec */
    { { NULL },          1,  2,  4, 8, 
        {010420} },                                     /* KWV11C - fx CSR */
    { { NULL },          1,  2,  8, 8, 
        {016410} },                                     /* ADV11D - fx CSR */
    { { NULL },          1,  2,  8, 8, 
        {016420} },                                     /* AAV11D - fx CSR */
    { { "QDSS" },        1,  3,  0, 16,
        {017400, 017402, 017404, 017406, 
         017410, 017412, 017414, 017416} },             /* VCB02 - QDSS - fx CSR */
    { { NULL },          1, 16,  0, 4, 
        {004160, 004140, 004120} },                     /* DRV11J - fx CSR */
    { { NULL },          1,  2, 16, 8 },                /* DRQ3B */
    { { NULL },          1,  1,  8, 4 },                /* VSV24 */
    { { NULL },          1,  1,  8, 4 },                /* VSV21 */
    { { NULL },          1,  1,  8, 4 },                /* IBQ01 */
    { { NULL },          1,  1,  8, 8 },                /* IDV11A */
    { { NULL },          1,  0,  8, 8 },                /* IDV11B */
    { { NULL },          1,  0,  8, 8 },                /* IDV11C */
    { { NULL },          1,  1,  8, 8 },                /* IDV11D */
    { { NULL },          1,  2,  8, 8 },                /* IAV11A */
    { { NULL },          1,  0,  8, 8 },                /* IAV11B */
    { { NULL },          1,  2,  8, 8 },                /* MIRA */
    { { NULL },          1,  2, 16, 8 },                /* IEQ11 */
    { { NULL },          1,  2, 32, 8 },                /* ADQ32 */
    { { NULL },          1,  2,  8, 8 },                /* DTC04, DECvoice */
    { { NULL },          1,  1, 32, 4 },                /* DESNA */
    { { NULL },          1,  2,  4, 8 },                /* IGQ11 */
    { { NULL },          1,  2, 32, 8 },                /* KMV1F */
    { { NULL },          1,  1,  8, 4 },                /* DIV32 */
    { { NULL },          1,  2,  4, 8 },                /* DTCN5, DECvoice */
    { { NULL },          1,  2,  4, 8 },                /* DTC05, DECvoice */
    { { NULL },          1,  2,  8, 8 },                /* KWV32 (DSV11) */
    { { NULL },          1,  1, 64, 4 },                /* QZA */
    { { NULL }, -1 }                                    /* end table */
};

t_stat auto_config (char *name, int32 nctrl)
{
uint32 csr = IOPAGEBASE + AUTO_CSRBASE;
uint32 vec = VEC_Q + AUTO_VECBASE;
AUTO_CON *autp;
DEVICE *dptr;
DIB *dibp;
t_bool auto_fixed = TRUE;
uint32 j, k, vmask, amask;

if (autcon_enb == 0)                                    /* enabled? */
    return SCPE_OK;
if (name) {                                             /* updating? */
    if (nctrl < 0)
        return SCPE_ARG;
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
        if (autp->dnam[j] == NULL)                      /* no device? */
            break;
        dptr = find_dev (autp->dnam[j]);                /* find ctrl */
        if ((dptr == NULL) ||                           /* enabled, floating? */
            (dptr->flags & DEV_DIS))
            continue;
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp == NULL)                               /* not there??? */
            return SCPE_IERR;
        if (auto_fixed || (autp->amod)) {               /* dyn csr needed? */
            if (autp->fixa[k])                          /* fixed csr avail? */
                dibp->ba = IOPAGEBASE + autp->fixa[k];  /* use it */
            else {                                      /* no fixed left */
                dibp->ba = csr;                         /* set CSR */
                csr += (autp->numc * autp->amod);       /* next CSR */
                }                                       /* end else */
            }                                           /* end if dyn csr */
        if (autp->numv && (autp->vmod || auto_fixed)) { /* dyn vec needed? */
            uint32 numv = abs (autp->numv);             /* get num vec */
            if (autp->fixv[k]) {                        /* fixed vec avail? */
                if (autp->numv > 0)
                    dibp->vec = VEC_Q + autp->fixv[k];  /* use it */
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
    if (autp->amod)                                     /* flt CSR? gap */
        csr = csr + 2;
    }                                                   /* end for i */
return SCPE_OK;
}

/* Factory bad block table creation routine

   This routine writes a DEC standard 044 compliant bad block table on the
   last track of the specified unit.  The bad block table consists of 10
   repetitions of the same table, formatted as follows:

        words 0-1       pack id number
        words 2-3       cylinder/sector/surface specifications
         :
        words n-n+1     end of table (-1,-1)

   Inputs:
        uptr    =       pointer to unit
        sec     =       number of sectors per surface
        wds     =       number of words per sector
   Outputs:
        sta     =       status code
*/

t_stat pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds)
{
int32 i;
t_addr da;
uint16 *buf;

if ((sec < 2) || (wds < 16))
    return SCPE_ARG;
if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
if (uptr->flags & UNIT_RO)
    return SCPE_RO;
if (!get_yn ("Overwrite last track? [N]", FALSE))
    return SCPE_OK;
da = (uptr->capac - (sec * wds)) * sizeof (uint16);
if (sim_fseek (uptr->fileref, da, SEEK_SET))
    return SCPE_IOERR;
if ((buf = (uint16 *) malloc (wds * sizeof (uint16))) == NULL)
    return SCPE_MEM;
buf[0] = buf[1] = 012345u;
buf[2] = buf[3] = 0;
for (i = 4; i < wds; i++)
    buf[i] = 0177777u;
for (i = 0; (i < sec) && (i < 10); i++)
    sim_fwrite (buf, sizeof (uint16), wds, uptr->fileref);
free (buf);
if (ferror (uptr->fileref))
    return SCPE_IOERR;
return SCPE_OK;
}
