/* pdp11_io.c: PDP-11 I/O simulator

   Copyright (c) 1993-2002, Robert M Supnik

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
extern int32 cpu_bme, cpu_ubm;
extern int32 trap_req, ipl;
extern int32 cpu_log;
extern FILE *sim_log;
int32 calc_ints (int32 nipl, int32 trq);

extern DIB cpu0_dib, cpu1_dib, cpu2_dib;
extern DIB cpu3_dib, cpu4_dib, ubm_dib;
extern DIB pt_dib, tt_dib, clk_dib;
extern DIB lpt_dib, dz_dib;
extern DIB rk_dib, rl_dib;
extern DIB rp_dib, rq_dib;
extern DIB rx_dib, dt_dib;
extern DIB tm_dib, ts_dib;
extern int32 rk_inta (void);
extern int32 rp_inta (void);
extern int32 rq_inta (void);
extern int32 dz_rxinta (void);
extern int32 dz_txinta (void);

t_bool dev_conflict (uint32 nba, DIB *curr);

/* I/O data structures */

DIB *dib_tab[] = {
	&cpu0_dib,
	&cpu1_dib,
	&cpu2_dib,
	&cpu3_dib,
	&cpu4_dib,
	&ubm_dib,
	&pt_dib,
	&tt_dib,
	&clk_dib,
	&lpt_dib,
	&dz_dib,
	&rk_dib,
	&rl_dib,
	&rp_dib,
	&rq_dib,
	&rx_dib,
	&dt_dib,
	&tm_dib,
	&ts_dib,
	NULL };

int32 int_vec[IPL_HLVL][32] = {				/* int req to vector */
	{ 0 },						/* IPL 0 */
	{ VEC_PIRQ },					/* IPL 1 */
	{ VEC_PIRQ },					/* IPL 2 */
	{ VEC_PIRQ },					/* IPL 3 */
	{ VEC_TTI, VEC_TTO, VEC_PTR, VEC_PTP,		/* IPL 4 */
	  VEC_LPT, VEC_PIRQ },
	{ VEC_RK, VEC_RL, VEC_RX, VEC_TM,		/* IPL 5 */
	  VEC_RP, VEC_TS, VEC_RK6, VEC_RQ,
	  VEC_DZRX, VEC_DZTX, VEC_PIRQ }, 
	{ VEC_CLK, VEC_DTA, VEC_PIRQ },			/* IPL 6 */
	{ VEC_PIRQ }  };				/* IPL 7 */

int32 (*int_ack[IPL_HLVL][32])() = {			/* int ack routines */
	{ NULL },					/* IPL 0 */
	{ NULL },					/* IPL 1 */
	{ NULL },					/* IPL 2 */
	{ NULL },					/* IPL 3 */
	{ NULL },					/* IPL 4 */
	{ &rk_inta, NULL, NULL, NULL,			/* IPL 5 */
	  &rp_inta, NULL, NULL, &rq_inta,
	  &dz_rxinta, &dz_txinta, NULL },
	{ NULL },					/* IPL 6 */
	{ NULL }  };					/* IPL 7 */

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
	if (dibp -> enb && (pa >= dibp -> ba) &&
	   (pa < (dibp -> ba + dibp -> lnt))) {
		stat = dibp -> rd (data, pa, access);
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
	if (dibp -> enb && (pa >= dibp -> ba) &&
	   (pa < (dibp -> ba + dibp -> lnt))) {
		stat = dibp -> wr (data, pa, access);
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
		if (DBG_LOG (LOG_CPU_I)) fprintf (sim_log,
		    ">>INT: lvl=%d, flag=%d, vec=%o\n", i, j, vec);
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
	else {	sc = (addr & 2) << 3;
		ub_map[pg] = (ub_map[pg] & ~(0177777 << sc)) |
			((data & 0177777) << sc);  }
	ub_map[pg] = ub_map[pg] & 017777776;
	return SCPE_OK;  }
return SCPE_NXM;
}

/* Mapped memory access routines for DMA devices */

/* Map I/O address to memory address */

t_bool Map_Addr (t_addr ba, t_addr *ma)
{
if (cpu_bme) {						/* bus map on? */
	int32 pg = UBM_GETPN (ba);			/* map entry */
	int32 off = UBM_GETOFF (ba);			/* offset */
	if (pg != UBM_M_PN)				/* last page? */
		*ma = (ub_map[pg] + off) & PAMASK;	/* no, use map */
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

int32 Map_ReadB (t_addr ba, int32 bc, uint8 *buf, t_bool ub)
{
t_addr alim, lim, ma;

lim = ba + bc;
if (ub && cpu_bme) {					/* UB, map on? */
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

int32 Map_ReadW (t_addr ba, int32 bc, uint16 *buf, t_bool ub)
{
t_addr alim, lim, ma;

ba = ba & ~01;						/* align start */
lim = ba + (bc & ~01);
if (ub && cpu_bme) {					/* UB, map on? */
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

int32 Map_WriteB (t_addr ba, int32 bc, uint8 *buf, t_bool ub)
{
t_addr alim, lim, ma;

lim = ba + bc;
if (ub && cpu_bme) {					/* UB, map on? */
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

int32 Map_WriteW (t_addr ba, int32 bc, uint16 *buf, t_bool ub)
{
t_addr alim, lim, ma;

ba = ba & ~01;						/* align start */
lim = ba + (bc & ~01);
if (ub && cpu_bme) {					/* UB, map on? */
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

/* Change device number for a device */

t_stat set_addr (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DIB *dibp;
uint32 newba;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if ((val == 0) || (desc == NULL)) return SCPE_IERR;
dibp = (DIB *) desc;
newba = get_uint (cptr, 8, PAMASK, &r);			/* get new */
if ((r != SCPE_OK) || (newba == dibp -> ba)) return r;
if (newba <= IOPAGEBASE) return SCPE_ARG;		/* > IO page base? */
if (newba % ((uint32) val)) return SCPE_ARG;		/* check modulus */
if (dev_conflict (newba, dibp)) return SCPE_OK;
dibp -> ba = newba;					/* store */
return SCPE_OK;
}

/* Show device address */

t_stat show_addr (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DIB *dibp;

if (desc == NULL) return SCPE_IERR;
dibp = (DIB *) desc;
if (dibp -> ba <= IOPAGEBASE) return SCPE_IERR;
fprintf (st, "address=%08o", dibp -> ba);
if (dibp -> lnt > 1)
	fprintf (st, "-%08o", dibp -> ba + dibp -> lnt - 1);
return SCPE_OK;
}

/* Enable or disable a device */

t_stat set_enbdis (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i;
DEVICE *dptr;
DIB *dibp;
UNIT *up;

if (cptr != NULL) return SCPE_ARG;
if ((uptr == NULL) || (desc == NULL)) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);			/* find device */
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) desc;
if ((val ^ dibp -> enb) == 0) return SCPE_OK;		/* enable chg? */
if (val) {						/* enable? */
    if (dev_conflict (dibp -> ba, dibp)) return SCPE_OK;  }
else {							/* disable */
    for (i = 0; i < dptr -> numunits; i++) {		/* check units */
	up = (dptr -> units) + i;
	if ((up -> flags & UNIT_ATT) || sim_is_active (up))
	    return SCPE_NOFNC;  }  }
dibp -> enb = val;
if (dptr -> reset) return dptr -> reset (dptr);
else return SCPE_OK;
}

/* Test for conflict in device addresses */

t_bool dev_conflict (uint32 nba, DIB *curr)
{
uint32 i, end;
DIB *dibp;

end = nba + curr -> lnt - 1;				/* get end */
for (i = 0; dibp = dib_tab[i]; i++) {			/* loop thru dev */
	if (!dibp -> enb || (dibp == curr)) continue;	/* skip disabled */
	if (((nba >= dibp -> ba) &&			/* overlap start? */
	    (nba < (dibp -> ba + dibp -> lnt))) ||
	    ((end >= dibp -> ba) &&			/* overlap end? */
	    (end < (dibp -> ba + dibp -> lnt)))) {
		printf ("Device address conflict at %08o\n", dibp -> ba);
		if (sim_log) fprintf (sim_log,
			"Device number conflict at %08o\n", dibp -> ba);
		return TRUE;  }  }
return FALSE;
}
