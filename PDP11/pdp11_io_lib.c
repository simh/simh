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
dptr->flags = dptr->flags & ~DEV_FLTA;                  /* not floating */
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
if (dptr->flags & DEV_FLTA)
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
    { { "DCI" }, DCX_LINES, 2, 0, 8, { 0 } },           /* DC11 - fx CSRs */
    { { "DLI" }, DLX_LINES, 2, 0, 8, { 0 } },           /* KL11/DL11/DLV11 - fx CSRs */
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
    { { "QDSS" }, 1, 3, 0, 16, {IOBA_QDSS} },           /* QDSS - fx CSR */
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
            continue;
        dptr = find_dev (autp->dnam[j]);                /* find ctrl */
        if ((dptr == NULL) ||                           /* enabled, floating? */
            (dptr->flags & DEV_DIS) ||
            !(dptr->flags & DEV_FLTA))
            continue;
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp == NULL)                               /* not there??? */
            return SCPE_IERR;
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
