/* pdp11_io.c: PDP-11 I/O simulator

   Copyright (c) 1993-2004, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   25-Jan-04	RMS	Removed local debug logging support
   21-Dec-03	RMS	Fixed bug in autoconfigure vector assignment; added controls
   21-Nov-03	RMS	Added check for interrupt slot conflict (found by Dave Hittner)
   12-Mar-03	RMS	Added logical name support
   08-Oct-02	RMS	Trimmed I/O bus addresses
			Added support for dynamic tables
			Added show I/O space, autoconfigure routines
   12-Sep-02	RMS	Added support for TMSCP, KW11P, RX211
   26-Jan-02	RMS	Revised for multiple DZ's
   06-Jan-02	RMS	Revised I/O access, enable/disable support
   11-Dec-01	RMS	Moved interrupt debug code
   08-Nov-01	RMS	Cloned from cpu sources
*/

#include "pdp11_defs.h"

extern uint16 *M;
extern int32 int_req[IPL_HLVL];
extern int32 ub_map[UBM_LNT_LW];
extern UNIT cpu_unit;
extern int32 cpu_bme, cpu_18b, cpu_ubm;
extern int32 trap_req, ipl;
extern int32 cpu_log;
extern int32 autcon_enb;
extern FILE *sim_log;
extern DEVICE *sim_devices[];

int32 calc_ints (int32 nipl, int32 trq);

extern DIB cpu0_dib, cpu1_dib, cpu2_dib;
extern DIB cpu3_dib, cpu4_dib, ubm_dib;

/* I/O data structures */

DIB *dib_tab[DIB_MAX];					/* run time DIBs */

int32 int_vec[IPL_HLVL][32];				/* int req to vector */

int32 (*int_ack[IPL_HLVL][32])(void);			/* int ack routines */

static DIB *std_dib[] = {				/* standard DIBs */
	&cpu0_dib,
	&cpu1_dib,
	&cpu2_dib,
	&cpu3_dib,
	&cpu4_dib,
	NULL };

static int32 pirq_vloc[7] = {
	IVCL (PIR7), IVCL (PIR6), IVCL (PIR5), IVCL (PIR4),
	IVCL (PIR3), IVCL (PIR2), IVCL (PIR1) };

/* I/O page lookup and linkage routines

   Inputs:
	*data	=	pointer to data to read, if READ
	data	=	data to store, if WRITE or WRITEB
	pa	=	address
	access	=	READ, WRITE, or WRITEB
   Outputs:
	status	=	SCPE_OK or SCPE_NXM
*/

t_stat iopageR (int32 *data, uint32 pa, int32 access)
{
int32 i;
DIB *dibp;
t_stat stat;

for (i = 0; dibp = dib_tab[i]; i++ ) {
	if ((pa >= dibp->ba) &&
	   (pa < (dibp->ba + dibp->lnt))) {
	    stat = dibp->rd (data, pa, access);
	    trap_req = calc_ints (ipl, trap_req);
	    return stat;  }  }
return SCPE_NXM;
}

t_stat iopageW (int32 data, uint32 pa, int32 access)
{
int32 i;
DIB *dibp;
t_stat stat;

for (i = 0; dibp = dib_tab[i]; i++ ) {
	if ((pa >= dibp->ba) &&
	   (pa < (dibp->ba + dibp->lnt))) {
	    stat = dibp->wr (data, pa, access);
	    trap_req = calc_ints (ipl, trap_req);
	    return stat;  }  }
return SCPE_NXM;
}

/* Calculate interrupt outstanding */

int32 calc_ints (int32 nipl, int32 trq)
{
int32 i;

for (i = IPL_HLVL - 1; i > nipl; i--) {
	if (int_req[i]) return (trq | TRAP_INT);  }
return (trq & ~TRAP_INT);
}

/* Find vector for highest priority interrupt */

int32 get_vector (int32 nipl)
{
int32 i, j, t, vec;

for (i = IPL_HLVL - 1; i > nipl; i--) {			/* loop thru lvls */
	t = int_req[i];					/* get level */
	for (j = 0; t && (j < 32); j++) {		/* srch level */
	    if ((t >> j) & 1) {				/* irq found? */
		int_req[i] = int_req[i] & ~(1u << j);	/* clr irq */
		if (int_ack[i][j]) vec = int_ack[i][j]();
		else vec = int_vec[i][j];
		return vec;				/* return vector */
		}					/* end if t */
	    }						/* end for j */
	}						/* end for i */
return 0;
}

/* Read and write Unibus map registers

   In any even/odd pair
   even = low 16b, bit <0> clear
   odd  = high 6b

   The Unibus map is stored as an array of longwords
*/

t_stat ubm_rd (int32 *data, int32 addr, int32 access)
{
if (cpu_ubm) {
	int32 pg = (addr >> 2) & UBM_M_PN;
	*data = (addr & 2)? ((ub_map[pg] >> 16) & 077):
		(ub_map[pg] & 0177776);
	return SCPE_OK;  }
return SCPE_NXM;
}

t_stat ubm_wr (int32 data, int32 addr, int32 access)
{
if (cpu_ubm) {
	int32 sc, pg = (addr >> 2) & UBM_M_PN;
	if (access == WRITEB) {
	    sc = (addr & 3) << 3;
	    ub_map[pg] = (ub_map[pg] & ~(0377 << sc)) |
		((data & 0377) << sc);  }
	else {
	    sc = (addr & 2) << 3;
	    ub_map[pg] = (ub_map[pg] & ~(0177777 << sc)) |
		((data & 0177777) << sc);  }
	ub_map[pg] = ub_map[pg] & 017777776;
	return SCPE_OK;  }
return SCPE_NXM;
}

/* Mapped memory access routines for DMA devices */

#define BUSMASK(m)	((cpu_18b || (cpu_ubm && (m)))? UNIMASK: PAMASK)

/* Map I/O address to memory address */

t_bool Map_Addr (uint32 ba, uint32 *ma)
{
if (cpu_bme) {						/* bus map on? */
	int32 pg = UBM_GETPN (ba);			/* map entry */
	int32 off = UBM_GETOFF (ba);			/* offset */
	if (pg != UBM_M_PN)				/* last page? */
	    *ma = (ub_map[pg] + off) & PAMASK;		/* no, use map */
	else *ma = (IOPAGEBASE + off) & PAMASK;  }	/* yes, use fixed */
else *ma = ba;						/* else physical */
return TRUE;
}

/* I/O buffer routines, aligned access

   Map_ReadB	-	fetch byte buffer from memory
   Map_ReadW 	-	fetch word buffer from memory
   Map_WriteB 	-	store byte buffer into memory
   Map_WriteW 	-	store word buffer into memory
*/

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf, t_bool map)
{
uint32 alim, lim, ma;

ba = ba & BUSMASK (map);				/* trim address */
lim = ba + bc;
if (map && cpu_bme) {					/* map req & on? */
    for ( ; ba < lim; ba++) {				/* by bytes */
	Map_Addr (ba, &ma);				/* map addr */
	if (!ADDR_IS_MEM (ma)) return (lim - ba);	/* NXM? err */
	if (ma & 1) *buf++ = (M[ma >> 1] >> 8) & 0377;	/* get byte */
	else *buf++ = M[ma >> 1] & 0377;  }
    return 0;  }
else {							/* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;			/* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;		/* no, strt ok? */
    else return bc;					/* no, err */
    for ( ; ba < alim; ba++) {				/* by bytes */
	if (ba & 1) *buf++ = (M[ba >> 1] >> 8) & 0377;	/* get byte */
	else *buf++ = M[ba >> 1] & 0377;  }
    return (lim - alim);  }
}

int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf, t_bool map)
{
uint32 alim, lim, ma;

ba = (ba & BUSMASK (map)) & ~01;			/* trim, align addr */
lim = ba + (bc & ~01);
if (map && cpu_bme) {					/* map req & on? */
    for (; ba < lim; ba = ba + 2) {			/* by words */
	Map_Addr (ba, &ma);				/* map addr */
	if (!ADDR_IS_MEM (ma)) return (lim - ba);	/* NXM? err */
	*buf++ = M[ma >> 1];  }
     return 0;  }
else {							/* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;			/* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;		/* no, strt ok? */
    else return bc;					/* no, err */
    for ( ; ba < alim; ba = ba + 2) {			/* by words */
	*buf++ = M[ba >> 1];  }
    return (lim - alim);  }
}

int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf, t_bool map)
{
uint32 alim, lim, ma;

ba = ba & BUSMASK (map);				/* trim address */
lim = ba + bc;
if (map && cpu_bme) {					/* map req & on? */
    for ( ; ba < lim; ba++) {				/* by bytes */
	Map_Addr (ba, &ma);				/* map addr */
	if (!ADDR_IS_MEM (ma)) return (lim - ba);	/* NXM? err */
	if (ma & 1) M[ma >> 1] = (M[ma >> 1] & 0377) |
		((uint16) *buf++ << 8);
	else M[ma >> 1] = (M[ma >> 1] & ~0377) | *buf++;  }
    return 0;  }
else {							/* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;			/* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;		/* no, strt ok? */
    else return bc;					/* no, err */
    for ( ; ba < alim; ba++) {				/* by bytes */
	if (ba & 1) M[ba >> 1] = (M[ba >> 1] & 0377) |
		((uint16) *buf++ << 8);
	else M[ba >> 1] = (M[ba >> 1] & ~0377) | *buf++;  }
    return (lim - alim);  }
}

int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf, t_bool map)
{
uint32 alim, lim, ma;

ba = (ba & BUSMASK (map)) & ~01;			/* trim, align addr */
lim = ba + (bc & ~01);
if (map && cpu_bme) {					/* map req & on? */
    for (; ba < lim; ba = ba + 2) {			/* by words */
	Map_Addr (ba, &ma);				/* map addr */
	if (!ADDR_IS_MEM (ma)) return (lim - ba);	/* NXM? err */
	M[ma >> 1] = *buf++;  }				/* store word */
    return 0;  }
else {							/* physical */
    if (ADDR_IS_MEM (lim)) alim = lim;			/* end ok? */
    else if (ADDR_IS_MEM (ba)) alim = MEMSIZE;		/* no, strt ok? */
    else return bc;					/* no, err */
    for ( ; ba < alim; ba = ba + 2) {			/* by words */
	M[ba >> 1] = *buf++;  }
    return (lim - alim);  }
}

/* Enable/disable autoconfiguration */

t_stat set_autocon (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr != NULL) return SCPE_ARG;
autcon_enb = val;
return auto_config (0, 0);
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

if (cptr == NULL) return SCPE_ARG;
if ((val == 0) || (uptr == NULL)) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
newba = get_uint (cptr, 8, PAMASK, &r);			/* get new */
if (r != SCPE_OK) return r;				/* error? */
if ((newba <= IOPAGEBASE) ||				/* > IO page base? */
    (newba % ((uint32) val))) return SCPE_ARG;		/* check modulus */
dibp->ba = newba;					/* store */
dptr->flags = dptr->flags & ~DEV_FLTA;			/* not floating */
autcon_enb = 0;						/* autoconfig off */
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
dptr->flags = dptr->flags | DEV_FLTA;			/* floating */
return auto_config (0, 0);				/* autoconfigure */
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
dptr->flags = dptr->flags & ~DEV_FLTA;			/* not floating */
autcon_enb = 0;						/* autoconfig off */
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
else {	fprintf (st, "vector=%o", vec);
	if (numvec > 1) fprintf (st, "-%o", vec + (4 * (numvec - 1)));  }
return SCPE_OK;
}

/* Test for conflict in device addresses */

t_bool dev_conflict (DIB *curr)
{
uint32 i, end;
DEVICE *dptr;
DIB *dibp;

end = curr->ba + curr->lnt - 1;				/* get end */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* loop thru dev */
	dibp = (DIB *) dptr->ctxt;			/* get DIB */
	if ((dibp == NULL) || (dibp == curr) ||
	    (dptr->flags & DEV_DIS)) continue;
	if (((curr->ba >= dibp->ba) &&			/* overlap start? */
	    (curr->ba < (dibp->ba + dibp->lnt))) ||
	    ((end >= dibp->ba) &&			/* overlap end? */
	    (end < (dibp->ba + dibp->lnt)))) {
		printf ("Device %s address conflict at %08o\n",
		    sim_dname (dptr), dibp->ba);
		if (sim_log) fprintf (sim_log,
		    "Device %s address conflict at %08o\n",
		    sim_dname (dptr), dibp->ba);
		return TRUE;  }	 }
return FALSE;
}

/* Build interrupt tables */

t_bool build_int_vec (int32 vloc, int32 ivec, int32 (*iack)(void) )
{
int32 ilvl = vloc / 32;
int32 ibit = vloc % 32;

if (iack != NULL) {
	if (int_ack[ilvl][ibit] &&
	   (int_ack[ilvl][ibit] != iack)) return TRUE;
	 int_ack[ilvl][ibit] = iack;  }
else if (ivec != 0) {
	if (int_vec[ilvl][ibit] &&
	    (int_vec[ilvl][ibit] != ivec)) return TRUE;
	int_vec[ilvl][ibit] = ivec;  }
return FALSE;
}

/* Build dib_tab from device list */

t_stat build_dib_tab (int32 ubm)
{
int32 i, j, k;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < IPL_HLVL; i++) {			/* clear int tables */
	for (j = 0; j < 32; j++) {
	    int_vec[i][j] = 0;
	    int_ack[i][j] = NULL;  }  }
for (i = j = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* loop thru dev */
	dibp = (DIB *) dptr->ctxt;			/* get DIB */
	if (dibp && !(dptr->flags & DEV_DIS)) {		/* defined, enabled? */
	    if (dibp->vnum > VEC_DEVMAX) return SCPE_IERR;
	    for (k = 0; k < dibp->vnum; k++) {		/* loop thru vec */
	        if (build_int_vec (dibp->vloc + k,	/* add vector */
		    dibp->vec + (k * 4), dibp->ack[k])) {
		    printf ("Device %s interrupt slot conflict at %d\n",
			sim_dname (dptr), dibp->vloc + k);
		    if (sim_log) fprintf (sim_log,
			"Device %s interrupt slot conflict at %d\n",
			sim_dname (dptr), dibp->vloc + k);
		    return SCPE_IERR;  }  }
	    if (dibp->lnt != 0) {			/* I/O addresses? */
		dib_tab[j++] = dibp;			/* add DIB to dib_tab */
		if (j >= DIB_MAX) return SCPE_IERR;  }	/* too many? */	
	    }						/* end if enabled */
	}						/* end for */
for (i = 0; (dibp = std_dib[i]) != NULL; i++) {		/* loop thru std */
	dib_tab[j++] = dibp;				/* add to dib_tab */
	if (j >= DIB_MAX) return SCPE_IERR;  }		/* too many? */
if (ubm) {						/* Unibus map? */
	dib_tab[j++] = &ubm_dib;			/* add to dib_tab */
	if (j >= DIB_MAX) return SCPE_IERR;  }		/* too many? */
dib_tab[j] = NULL;					/* end with NULL */
for (i = 0; i < 7; i++)					/* add PIRQ intr */
	build_int_vec (pirq_vloc[i], VEC_PIRQ, NULL);
for (i = 0; (dibp = dib_tab[i]) != NULL; i++) {		/* test built dib_tab */
	if (dev_conflict (dibp)) {
	    return SCPE_STOP;  }  }			/* for conflicts */
return SCPE_OK;
}

/* Show dib_tab */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, j, done = 0;
DEVICE *dptr;
DIB *dibt;

build_dib_tab (cpu_ubm);				/* build table */
while (done == 0) {					/* sort ascending */
	done = 1;					/* assume done */
	for (i = 0; dib_tab[i + 1] != NULL; i++) {	/* check table */
	    if (dib_tab[i]->ba > dib_tab[i + 1]->ba) {	/* out of order? */
		dibt = dib_tab[i];			/* interchange */
		dib_tab[i] = dib_tab[i + 1];
		dib_tab[i + 1] = dibt;
		done = 0;  }  }				/* not done */
	}						/* end while */
for (i = 0; dib_tab[i] != NULL; i++) {			/* print table */
	for (j = 0, dptr = NULL; sim_devices[j] != NULL; j++) {
	    if (((DIB*) sim_devices[j]->ctxt) == dib_tab[i]) {
		dptr = sim_devices[j];
		break;  }  }
	fprintf (st, "%08o - %08o%c\t%s\n", dib_tab[i]->ba,
		dib_tab[i]->ba + dib_tab[i]->lnt - 1,
		(dptr && (dptr->flags & DEV_FLTA))? '*': ' ',
		dptr? sim_dname (dptr): "CPU");
	}
return SCPE_OK;
}

/* Autoconfiguration */

#define AUTO_DYN	0001
#define AUTO_VEC	0002
#define AUTO_MAXC	4
#define AUTO_CSRBASE	0010
#define AUTO_VECBASE	0300

struct auto_con {
	uint32	amod;
	uint32	vmod;
	uint32	flags;
	uint32	num;
	uint32	fix;
	char	*dnam[AUTO_MAXC]; };

struct auto_con auto_tab[AUTO_LNT + 1] = {
	{ 0x7, 0x7 },					/* DJ11 */
	{ 0xf, 0x7 },					/* DH11 */
	{ 0x7, 0x7 },					/* DQ11 */
	{ 0x7, 0x7 },					/* DU11 */
	{ 0x7, 0x7 },					/* DUP11 */
	{ 0x7, 0x7 },					/* LK11A */
	{ 0x7, 0x7 },					/* DMC11 */
	{ 0x7, 0x7, AUTO_VEC, DZ_MUXES, 0, { "DZ" } },

	{ 0x7, 0x7 },					/* KMC11 */
	{ 0x7, 0x7 },					/* LPP11 */
	{ 0x7, 0x7 },					/* VMV21 */
	{ 0xf, 0x7 },					/* VMV31 */
	{ 0x7, 0x7 },					/* DWR70 */
	{ 0x7, 0x3, AUTO_DYN|AUTO_VEC, 0, IOBA_RL, { "RL", "RLB" } },
	{ 0xf, 0x7 },					/* LPA11K */
	{ 0x7, 0x7 },					/* KW11C */

	{ 0x7, 0 },					/* reserved */
	{ 0x7, 0x3, AUTO_DYN|AUTO_VEC, 0, IOBA_RX, { "RX", "RY" } },
	{ 0x7, 0x3 },					/* DR11W */
	{ 0x7, 0x3 },					/* DR11B */
	{ 0x7, 0x7 },					/* DMP11 */
	{ 0x7, 0x7 },					/* DPV11 */
	{ 0x7, 0x7 },					/* ISB11 */
	{ 0xf, 0x7 },					/* DMV11 */

	{ 0x7, 0x3, AUTO_DYN|AUTO_VEC, 0, IOBA_XU, { "XU", "XUB" } },
	{ 0x3, 0x3, AUTO_DYN|AUTO_VEC, 0, IOBA_RQ, { "RQ", "RQB", "RQC", "RQD" } },
	{ 0x1f, 0x3 },					/* DMF32 */
	{ 0xf, 0x7 },					/* KMS11 */
	{ 0xf, 0x3 },					/* VS100 */
	{ 0x3, 0x3, AUTO_DYN|AUTO_VEC, 0, IOBA_TQ, { "TQ", "TQB" } },
	{ 0xf, 0x7 },					/* KMV11 */
	{ 0xf, 0x7 },					/* DHU11/DHQ11 */

	{ 0x1f, 0x7 },					/* DMZ32 */
	{ 0x1f, 0x7 },					/* CP132 */
	{ 0 },						/* padding */
};

t_stat auto_config (uint32 rank, uint32 nctrl)
{
uint32 csr = IOPAGEBASE + AUTO_CSRBASE;
uint32 vec = VEC_Q + AUTO_VECBASE;
struct auto_con *autp;
DEVICE *dptr;
DIB *dibp;
int32 i, j, k;
extern DEVICE *find_dev (char *ptr);

if (autcon_enb == 0) return SCPE_OK;			/* enabled? */
if (rank > AUTO_LNT) return SCPE_IERR;			/* legal rank? */
if (rank) auto_tab[rank - 1].num = nctrl;		/* update num? */
for (i = 0, autp = auto_tab; i < AUTO_LNT; i++) {	/* loop thru table */
	for (j = k = 0; (j < AUTO_MAXC) && autp->dnam[j]; j++) {
	    dptr = find_dev (autp->dnam[j]);		/* find ctrl */
	    if ((dptr == NULL) || (dptr->flags & DEV_DIS) ||
		!(dptr->flags & DEV_FLTA)) continue;	/* enabled, floating? */
	    dibp = (DIB *) dptr->ctxt;			/* get DIB */
	    if ((k++ == 0) && autp->fix)		/* 1st & fixed? */
		dibp->ba = autp->fix;			/* gets fixed CSR */
	    else {					/* no, float */
		dibp->ba = csr;				/* set CSR */
		csr = (csr + autp->amod + 1) & ~autp->amod;	/* next CSR */
		if ((autp->flags & AUTO_DYN) == 0)	/* static? */
		    csr = csr + ((autp->num - 1) * (autp->amod + 1));
		if (autp->flags & AUTO_VEC) {		/* vectors too? */
		    dibp->vec = (vec + autp->vmod) & ~autp->vmod;
		    if (autp->flags & AUTO_DYN) vec = vec + autp->vmod + 1;
		    else vec = vec + (autp->num * (autp->vmod + 1));  }
		}					/* end else flt */
	    }						/* end for j */
	autp++;
	csr = (csr + autp->amod + 1) & ~autp->amod;	/* gap */
	}						/* end for i */
return SCPE_OK;
}
