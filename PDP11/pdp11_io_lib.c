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
#include "sim_ether.h"

extern int32 autcon_enb;
extern int32 int_vec[IPL_HLVL][32];
#if !defined(VEC_SET)
#define VEC_SET 0
#endif
#if (VEC_SET != 0)
extern int32 int_vec_set[IPL_HLVL][32];                 /* bits to set in vector */
#endif
extern int32 (*int_ack[IPL_HLVL][32])(void);
extern t_stat (*iodispR[IOPAGESIZE >> 1])(int32 *dat, int32 ad, int32 md);
extern t_stat (*iodispW[IOPAGESIZE >> 1])(int32 dat, int32 ad, int32 md);

extern t_stat build_dib_tab (void);

static DIB *iodibp[IOPAGESIZE >> 1];

static void build_vector_tab (void);

#if !defined(UNIMEMSIZE)
#define UNIMEMSIZE      001000000                       /* 2**18 */
#endif

#define AUTO_MAXC       32              /* Maximum number of controllers */
#define AUTO_CSRBASE    0010
#define AUTO_CSRMAX    04000
#define AUTO_VECBASE    0300

/* Enable/disable autoconfiguration */

t_stat set_autocon (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr != NULL)
    return SCPE_ARG;
autcon_enb = val;
return auto_config (NULL, 0);
}

/* Show autoconfiguration status */

t_stat show_autocon (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "autoconfiguration ");
fprintf (st, autcon_enb? "enabled": "disabled");
return SCPE_OK;
}

/* Change device address */

t_stat set_addr (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

t_stat show_addr (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 radix = DEV_RDX;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if ((dibp == NULL) || (dibp->ba <= IOPAGEBASE))
    return SCPE_IERR;
if (sim_switches & SWMASK ('H'))
    radix = 16;
if (sim_switches & SWMASK ('O'))
    radix = 8;
fprintf (st, "address=");
fprint_val (st, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
if (radix != DEV_RDX) {
    fprintf (st, "(");
    fprint_val (st, (t_value) dibp->ba, radix, 32, PV_LEFT);
    fprintf (st, ")");
    }
if (dibp->lnt > 1) {
    fprintf (st, "-");
    fprint_val (st, (t_value) dibp->ba + dibp->lnt - 1, DEV_RDX, 32, PV_LEFT);
    if (radix != DEV_RDX) {
        fprintf (st, "(");
        fprint_val (st, (t_value) dibp->ba + dibp->lnt - 1, radix, 32, PV_LEFT);
        fprintf (st, ")");
        }
    }
if (dibp->ba < IOPAGEBASE + AUTO_CSRBASE + AUTO_CSRMAX)
    fprintf (st, "*");
return SCPE_OK;
}

/* Set address floating */

t_stat set_addr_flt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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

t_stat set_vec (UNIT *uptr, int32 arg, CONST char *cptr, void *desc)
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
newvec = (uint32) get_uint (cptr, DEV_RDX, 01000, &r);
if ((r != SCPE_OK) ||
    ((newvec + (dibp->vnum * 4)) >= 01000) ||           /* total too big? */
    (newvec & ((dibp->vnum > 1)? 07: 03)))              /* properly aligned value? */
    return SCPE_ARG;
dibp->vec = newvec;
autcon_enb = 0;                                         /* autoconfig off */
return SCPE_OK;
}

/* Show device vector */

t_stat show_vec (FILE *st, UNIT *uptr, int32 arg, CONST void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 vec, numvec, radix = DEV_RDX;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
if (sim_switches & SWMASK ('H'))
    radix = 16;
if (sim_switches & SWMASK ('O'))
    radix = 8;
vec = dibp->vec;
if (arg)
    numvec = arg;
else
    numvec = dibp->vnum;
if (vec == 0)
    fprintf (st, "no vector");
else {
#if (VEC_SET != 0)
    vec |= (int_vec_set[dibp->vloc / 32][dibp->vloc % 32] & ~3);
    vec &= (int_vec_set[dibp->vloc / 32][dibp->vloc % 32] | 0x1FF);
#endif
    fprintf (st, "vector=");
    fprint_val (st, (t_value) vec, DEV_RDX, 16, PV_LEFT);
    if (radix != DEV_RDX) {
        fprintf (st, "(");
        fprint_val (st, (t_value) vec, radix, 16, PV_LEFT);
        fprintf (st, ")");
        }
    if (numvec > 1) {
        fprintf (st, "-");
        fprint_val (st, (t_value) vec + (4 * (numvec - 1)), DEV_RDX, 16, PV_LEFT);
        if (radix != DEV_RDX) {
            fprintf (st, "(");
            fprint_val (st, (t_value) vec + (4 * (numvec - 1)), radix, 16, PV_LEFT);
            fprintf (st, ")");
            }
        }
    }
if (vec >= ((VEC_SET | AUTO_VECBASE) & ~3))
    fprintf (st, "*");
return SCPE_OK;
}

/* Show vector for terminal multiplexor */

t_stat show_vec_mux (FILE *st, UNIT *uptr, int32 arg, CONST void *desc)
{
const TMXR *mp = (const TMXR *) desc;

if ((mp == NULL) || (arg == 0))
    return SCPE_IERR;
return show_vec (st, uptr, ((mp->lines * 2) / arg), desc);
}

/* Init Unibus tables */

void init_ubus_tab (void)
{
size_t i, j;

build_vector_tab ();
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
int32 i, idx, vec, hivec, ilvl, ibit;
DEVICE *cdptr;
size_t j;
const char *cdname;

if ((dptr == NULL) || (dibp == NULL))                   /* validate args */
    return SCPE_IERR;
if (dibp->vnum > VEC_DEVMAX)
    return SCPE_IERR;
vec = dibp->vec;
ilvl = dibp->vloc / 32;
ibit = dibp->vloc % 32;
#if (VEC_SET != 0)
if (vec)
    vec |= (int_vec_set[ilvl][ibit] & ~3);
#endif
/* hivec & cdhivec are first vector AFTER device */
hivec = vec + (dibp->vnum * 4 * (dibp->ulnt? dibp->lnt/dibp->ulnt:
                                 (dptr->numunits? dptr->numunits: 1)));
/* Check for vector conflict with any other enabled device.
 * Skip vector checks if device (currently) doesn't have a vector assigned.
 * Also skip if power-up reset to allow for auto-configure.
 */
if (vec && !(sim_switches & SWMASK ('P'))) {
    for (j = 0; vec && (cdptr = sim_devices[j]) != NULL; j++) {
        DIB *cdibp = (DIB *)(cdptr->ctxt);
        int32 cdvec, cdhivec;

        if (!cdibp || (cdptr->flags & DEV_DIS)) {
            continue;
            }
        cdvec = cdibp->vec;
        ilvl = cdibp->vloc / 32;
        ibit = cdibp->vloc % 32;
#if (VEC_SET != 0)
        if (cdvec)
            cdvec |= (int_vec_set[ilvl][ibit] & ~3);
#endif
        cdhivec = cdvec + (cdibp->vnum * 4 * 
                           (cdibp->ulnt? cdibp->lnt/cdibp->ulnt:
                            (cdptr->numunits? cdptr->numunits: 1)));
        if (cdptr == dptr || !cdvec || !dibp->vnum) {
            continue;
            }
        if (hivec <= cdvec || vec >= cdhivec) {
            continue;
            }
        cdname = cdptr? sim_dname(cdptr): NULL;
        if (!cdname) {
            cdname = "CPU";
        }
        return sim_messagef (SCPE_STOP, (DEV_RDX == 16) ? 
                                        "Device %s interrupt vector conflict with %s at 0x%X\n" :
                                        "Device %s interrupt vector conflict with %s at 0%o\n",
                             sim_dname (dptr), cdname, (int)dibp->vec);
        }
    }
/* Interrupt slot assignment and conflict check. */
for (i = 0; i < dibp->vnum; i++) {                      /* loop thru vec */
    idx = dibp->vloc + i;                               /* vector index */
    vec = dibp->vec? (dibp->vec + (i * 4)): 0;          /* vector addr */
    ilvl = idx / 32;
    ibit = idx % 32;
#if (VEC_SET != 0)
    if (vec)
        vec |= (int_vec_set[ilvl][ibit] & ~3);
#endif
    if ((int_ack[ilvl][ibit] && dibp->ack[i] &&         /* conflict? */
        (int_ack[ilvl][ibit] != dibp->ack[i])) ||
        (int_vec[ilvl][ibit] && vec &&
        (int_vec[ilvl][ibit] != vec))) {
        return sim_messagef (SCPE_STOP, "Device %s interrupt slot conflict at %d\n",
                             sim_dname (dptr), idx);
        }
    if (dibp->ack[i])
        int_ack[ilvl][ibit] = dibp->ack[i];
    else {
        if (vec)
            int_vec[ilvl][ibit] = vec;
        }
    }
/* Register I/O space address and check for conflicts */
for (i = 0; i < (int32) dibp->lnt; i = i + 2) {         /* create entries */
    idx = ((dibp->ba + i) & IOPAGEMASK) >> 1;           /* index into disp */
    if ((iodispR[idx] && dibp->rd &&                    /* conflict? */
        (iodispR[idx] != dibp->rd)) ||
        (iodispW[idx] && dibp->wr &&
        (iodispW[idx] != dibp->wr))) {
        for (j = 0; (cdptr = sim_devices[j]) != NULL; j++) { /* Find conflicting device */
            DIB *cdibp = (DIB *)(cdptr->ctxt);
            if ((cdptr->flags & DEV_DIS) || !cdibp || cdibp == dibp) {
                continue;
                }
            if ((iodispR[idx] && dibp->rd &&
                (iodispR[idx] != dibp->rd) &&
                (cdibp->rd == iodispR[idx])) ||
                (iodispW[idx] && dibp->wr &&
                (iodispW[idx] != dibp->wr) &&
                (cdibp->wr == iodispW[idx]))) {
                break;
                }
            }
        cdname = cdptr? sim_dname(cdptr): NULL;
        if (!cdname) {
            cdname = "CPU";
            }
        return sim_messagef (SCPE_STOP, (DEV_RDX == 16) ? 
                                        "Device %s address conflict with %s at 0x%X\n" :
                                        "Device %s address conflict with %s at 0%o\n",
                             sim_dname (dptr), cdname, (int)dibp->ba);
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

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 i, j;
DEVICE *dptr;
DIB *dibp;
uint32 maxaddr, maxname, maxdev;
int32 maxvec, vecwid;
int32 brbase = 0;
char valbuf[40];

#if defined DEV_RDX && DEV_RDX == 16
#define VEC_FMT "X"
#else
#define VEC_FMT "o"
#endif

if (build_dib_tab ())                                   /* build IO page */
    return SCPE_OK;

maxaddr = 0;
maxvec = 0;
maxname = 0;
maxdev = 1;
#if defined (VM_VAX)
brbase = 4;
#endif

for (i = 0, dibp = NULL; i < (IOPAGESIZE >> 1); i++) {  /* loop thru entries */
    size_t l;
    if (iodibp[i] && (iodibp[i] != dibp)) {             /* new block? */
        dibp = iodibp[i];                               /* DIB for block */
        for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
            if (((DIB*) sim_devices[j]->ctxt) == dibp) {
                dptr = sim_devices[j];                  /* locate device */
                break;
                }                                       /* end if */
            }                                           /* end for j */
        if ((dibp->ba+ dibp->lnt - 1) > maxaddr)
            maxaddr = dibp->ba+ dibp->lnt - 1;
        if (dibp->vec > maxvec)
            maxvec = dibp->vec;
        l = strlen (dptr? sim_dname (dptr): "CPU");
        if (l>maxname)
            maxname = (int32)l;
        j = (dibp->ulnt? dibp->lnt/dibp->ulnt:
             (dptr? dptr->numunits: 1));
        if (j > maxdev)
            maxdev = j;
        }                                               /* end if */
    }                                                   /* end for i */
maxaddr = fprint_val (NULL, (t_value) dibp->ba, DEV_RDX, 32, PV_LEFT);
sprintf (valbuf, "%03" VEC_FMT, maxvec);
vecwid = maxvec = (int32) strlen (valbuf);
if (vecwid < 3)
    vecwid = 3;
sprintf (valbuf, "%u", maxdev);
maxdev = (uint32)strlen (valbuf);

j = strlen ("Address");
i = (maxaddr*2)+3+1;
if (i <= j)
    i = 0;
else
    i -= j;
maxaddr = i+j;
fprintf (st, "%*.*sAddress%*.*s", i/2, i/2, " ", (i/2)+i%2, (i/2)+i%2, " ");

j = strlen ("Vector");
i = ((maxvec*2)+1+1);
if (i <= j)
    i = 0;
else
    i -= j;
maxvec = i+j;
fprintf (st, " %*.*sVector%*.*s", i/2, i/2, " ", (i/2)+i%2, (i/2)+i%2, " ");

fprintf (st, " BR %*.*s# Device\n", (maxdev -1), (maxdev-1), " ");
for (i = 0; i < maxaddr; i++)
    fputc ('-', st);
fprintf (st, " ");
for (i = 0; i < (uint32)maxvec; i++)
    fputc ('-', st);

fprintf (st, " -- ");
for (i=0; i < maxdev; i++) {
    fputc ('-', st);
}
fputc (' ', st);

i = strlen ("Device");
if (maxname < i)
    maxname = i;

for (i = 0; i < maxname; i++)
    fputc ('-', st);
fputc ('\n', st);

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
        fprintf (st, "%c ",                        /* print block entry */
            (dibp->ba < IOPAGEBASE + AUTO_CSRBASE + AUTO_CSRMAX)? '*': ' ');
        if (dibp->vec == 0)
            fprintf (st, "%*s", ((vecwid*2)+1+1), " ");
        else {
            fprintf (st, "%0*" VEC_FMT, vecwid, dibp->vec);
            if (dibp->vnum > 1)
                fprintf (st, "-%0*" VEC_FMT, vecwid, dibp->vec + (4 *
                (dibp->ulnt? dibp->lnt/dibp->ulnt:
                                            (dptr? dptr->numunits: 1)) * dibp->vnum) - 4);
            else
                fprintf (st, " %*s", vecwid, " ");
            fprintf (st, "%1s", (dibp->vnum >= AUTO_VECBASE)? "*": " ");
            }
        if (dibp->vnum || dibp->vloc)
            fprintf (st, " %2u", brbase + dibp->vloc/32);
        else
            fprintf (st, "   ");
        fprintf (st, " %*u %s\n", maxdev,  (dibp->ulnt? dibp->lnt/dibp->ulnt:
                                            (dptr? dptr->numunits: 1)),
                 dptr? sim_dname (dptr): "CPU");
        }                                               /* end if */
    }                                                   /* end for i */
return SCPE_OK;
#undef VEC_FMT
}

/* Autoconfiguration

   The table reflects the MicroVAX 3900 microcode, with one field 
   addition:
      a valid flag marking the end of the list when the value is -1

   The table has been reviewed, extended and updated to reflect the 
   contents of the auto configure table in VMS sysgen (V5.5-2)

   A minus number of vectors indicates a field that should be 
   calculated but not placed in the DIB (RQ, TQ dynamic vectors)

   An amod value of 0 indicates that all addresses are FIXED
   An vmod value of 0 indicates that all vectors are FIXED */


typedef struct {
    const char  *dnam[AUTO_MAXC];
    int32       valid;
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
        {012100} },                                     /* MSV11-P/MSV11-Q - fx CSR, no VEC */
    { { "KE" },          1,  0,  0, 0, 
        {017300} },                                     /* KE11-A - fx CSR, no VEC */
    { { "KG" },          1,  0,  0, 0, 
        {010700} },                                     /* KG11-A - fx CSR, no VEC */
    { { "RHA", "RHB", "RHC" },  1,  1,  0, 0, 
        {016700, 012440, 012040}, {0254, 0224, 0204} }, /* RH11/RH70 - fx CSR, fx VEC */
    { { "CLK" },         1,  1,  0, 0, 
        {017546}, {0100} },                             /* KW11L - fx CSR, fx VEC */
    { { "PCLK" },        1,  1,  0, 0, 
        {012540}, {0104} },                             /* KW11P - fx CSR, fx VEC */
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
    { { "DCI" },         1,  2,  0, 8, 
        {014000, 014010, 014020, 014030, 
         014040, 014050, 014060, 014070, 
         014100, 014110, 014120, 014130, 
         014140, 014150, 014160, 014170, 
         014200, 014210, 014220, 014230, 
         014240, 014250, 014260, 014270,
         014300, 014310, 014320, 014330, 
         014340, 014350, 014360, 014370} },             /* DC11 - fx CSRs */
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
    { { "TDC", "DLI" },  1,  2,  0, 8, 
        {016500, 016510, 016520, 016530,
         016540, 016550, 016560, 016570,
         016600, 016610, 016620, 016630,
         016740, 016750, 016760, 016770} },             /* KL11/DL11/DLV11/TU58 - fx CSRs */
    { { NULL },          1,  2,  0, 8, { 0 } },         /* DLV11J - fx CSRs */
    { { NULL },          1,  2,  8, 8 },                /* DJ11 */
    { { NULL },          1,  2, 16, 8 },                /* DH11 */
    { { "VT" },          1,  4,  0, 8,
      {012000, 012010, 012020, 012030} },               /* VT11/GT40 - fx CSRs  */
    { { "VS60" },        1,  4,  0, 8,
      {012000} },                                       /* VS60/GT48 - fx CSRs  */
    { { NULL },          1,  2,  0, 8,
      {010400} },                                       /* LPS11 */
    { { NULL },          1,  2,  8, 8 },                /* DQ11 */
    { { NULL },          1,  2,  0, 8,
      {012400} },                                       /* KW11W */
    { { NULL },          1,  2,  8, 8 },                /* DU11 */
    { { "DUP" },         1,  2,  8, 8 },                /* DUP11 */
    { { NULL },          1,  3,  0, 8,
      {015000, 015040, 015100, 015140, }},              /* DV11 */
    { { NULL },          1,  2,  8, 8 },                /* LK11A */
    { { "DMC" }, 
                         1,  2,  8, 8 },                /* DMC11 */
    { { "DZ" },          1,  2,  8, 8 },                /* DZ11 */
    { { "KDP" },         1,  2,  8, 8 },                /* KMC11 */
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
    { { "DPV" },         1,  2,  8, 8 },                /* DPV11 */
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
    { { "VH" },          1,  2, 16, 8 },                /* DHU11/DHQ11 */
    { { NULL },          1,  6, 32, 4 },                /* DMZ32 */
    { { NULL },          1,  6, 32, 4 },                /* CP132 */
    { { "TC" },          1,  1,  0, 0,
        {017340}, {0214} },                             /* TC11 */
    { { "TA" },          1,  1,  0, 0,
        {017500}, {0260} },                             /* TA11 */
    { { "QVSS" },        1,  2, 64, 8, 
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
    { { NULL },         -1 }                            /* end table */
};

#if !defined(DEV_NEXUS) 
#if defined(DEV_MBUS)
#define DEV_NEXUS DEV_MBUS
#else
#define DEV_NEXUS 0
#endif
#endif

#define MAX(a,b) (((a)>(b))? (a) : (b))

static void build_vector_tab (void)
{
int32 ilvl, ibit;
static t_bool done = FALSE;
AUTO_CON *autp;
DEVICE *dptr;
DIB *dibp;
uint32 j, k;

if (done)
    return;
/* Locate all Unibus/Qbus devices and make sure vector masks are set */
for (j = 0; (dptr = sim_devices[j]) != NULL; j++) {
    if ((dptr->flags & (DEV_UBUS | DEV_QBUS)) == 0)
        continue;
    for (autp = auto_tab; autp->valid >= 0; autp++) {
        for (k=0; autp->dnam[k]; k++) {
            if (!strcmp(dptr->name, autp->dnam[k])) {
                dibp = (DIB *)dptr->ctxt;
                ilvl = dibp->vloc / 32;
                ibit = dibp->vloc % 32;
#if (VEC_SET != 0)
                if (1) {
                    int v;
                    
                    for (v=0; v<MAX(dibp->vnum, 1); v++)
                        int_vec_set[ilvl][ibit+v] = VEC_SET;
                    }
#endif
                break;
                }
            }
        }
    }
done = TRUE;
}

t_stat auto_config (const char *name, int32 nctrl)
{
uint32 csr = IOPAGEBASE + AUTO_CSRBASE;
uint32 vec = AUTO_VECBASE;
int32 ilvl, ibit, numc;
extern UNIT cpu_unit;
AUTO_CON *autp;
DEVICE *dptr;
DIB *dibp;
uint32 j, k, jena, vmask, amask;

if (autcon_enb == 0)                                    /* enabled? */
    return SCPE_OK;
if (name) {                                             /* updating? */
    dptr = find_dev (name);
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((nctrl < 0) || (dptr == NULL) || (dibp == NULL))
        return SCPE_ARG;
    dibp->numc = nctrl;
    }
for (autp = auto_tab; autp->valid >= 0; autp++) {       /* loop thru table */
    if (autp->amod) {                                   /* floating csr? */
        amask = autp->amod - 1;
        csr = (csr + amask) & ~amask;                   /* align csr */
        }
    for (j = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
        if (autp->dnam[j] == NULL)                      /* no device? */
            break;
        dptr = find_dev (autp->dnam[j]);                /* find ctrl */
        if ((dptr == NULL) ||                           /* enabled, not (nexus or unibus or qbus)? */
            (dptr->flags & DEV_DIS) ||
            (dptr->flags & DEV_NEXUS) ||
            !(dptr->flags & (DEV_UBUS | DEV_QBUS | DEV_Q18)) )
            continue;
        /* Sanity check that enabled devices can work on the current bus */
        if (!((UNIBUS && (dptr->flags & (DEV_UBUS | DEV_Q18))) ||
             ((!UNIBUS) && ((dptr->flags & DEV_QBUS) || 
                            ((dptr->flags & DEV_Q18) && (MEMSIZE <= UNIMEMSIZE)))))) {
            dptr->flags |= DEV_DIS;
            if (sim_switches & SWMASK ('P'))
                continue;
            return sim_messagef (SCPE_NOFNC, "%s device not compatible with system bus\n", sim_dname(dptr));
            }
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if (dibp == NULL)                               /* not there??? */
            return SCPE_IERR;
        numc = dibp->numc ? dibp->numc : 1;
        ilvl = dibp->vloc / 32;
        ibit = dibp->vloc % 32;
        /* Identify how many devices earlier in the device list are 
           enabled and use that info to determine fixed address assignments */
        for (k=jena=0; k<j; k++) {
            DEVICE *kdptr = find_dev (autp->dnam[k]);
            
            if (kdptr && (!(kdptr->flags & DEV_DIS)))
                jena += ((DIB *)kdptr->ctxt)->numc ? ((DIB *)kdptr->ctxt)->numc : 1;
            }
        if (autp->fixa[jena])                           /* fixed csr avail? */
            dibp->ba = IOPAGEBASE + autp->fixa[jena];   /* use it */
        else {                                          /* no fixed left */
            dibp->ba = csr;                             /* set CSR */
            csr += (numc * autp->amod);                 /* next CSR */
            }                                           /* end else */
        if (autp->numv) {                               /* vec needed? */
            if (autp->fixv[jena]) {                     /* fixed vec avail? */
                if (autp->numv > 0)
                    dibp->vec = autp->fixv[jena];       /* use it */
                }
            else {                                      /* no fixed left */
                uint32 numv = abs (autp->numv);         /* get num vec */
                vmask = autp->vmod - 1;
                vec = (vec + vmask) & ~vmask;           /* align vector */
                if (autp->numv > 0)
                    dibp->vec = vec;                    /* set vector */
                vec += (numc * numv * 4);
                }                                       /* end else */
            }                                           /* end vec needed */
        }                                               /* end for j */
    if (autp->amod)                                     /* flt CSR? gap */
        csr = csr + 2;
    }                                                   /* end for i */
return SCPE_OK;
}

/* Factory bad block table creation routine

   This routine writes a DEC standard 144 compliant bad block table on the
   last track of the specified unit as described in: 
      EL-00144_B_DEC_STD_144_Disk_Standard_for_Recording_and_Handling_Bad_Sectors_Nov76.pdf
   The bad block table consists of 10 repetitions of the same table, 
   formatted as follows:

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
char *namebuf, *c;
uint32 packid;

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
namebuf = uptr->filename;
if ((c = strrchr (namebuf, '/')))
    namebuf = c+1;
if ((c = strrchr (namebuf, '\\')))
    namebuf = c+1;
if ((c = strrchr (namebuf, ']')))
    namebuf = c+1;
packid = eth_crc32(0, namebuf, strlen (namebuf));
buf[0] = (uint16)packid;
buf[1] = (uint16)(packid >> 16) & 0x7FFF;   /* Make sure MSB is clear */
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
