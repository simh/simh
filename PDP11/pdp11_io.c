/* pdp11_io.c: PDP-11 I/O simulator

   Copyright (c) 1993-2001, Robert M Supnik

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

extern t_stat CPU_rd (int32 *data, int32 addr, int32 access);
extern t_stat CPU_wr (int32 data, int32 addr, int32 access);
extern t_stat APR_rd (int32 *data, int32 addr, int32 access);
extern t_stat APR_wr (int32 data, int32 addr, int32 access);
extern t_stat SR_MMR012_rd (int32 *data, int32 addr, int32 access);
extern t_stat SR_MMR012_wr (int32 data, int32 addr, int32 access);
extern t_stat MMR3_rd (int32 *data, int32 addr, int32 access);
extern t_stat MMR3_wr (int32 data, int32 addr, int32 access);
extern t_stat ubm_rd (int32 *data, int32 addr, int32 access);
extern t_stat ubm_wr (int32 data, int32 addr, int32 access);
extern t_stat std_rd (int32 *data, int32 addr, int32 access);
extern t_stat std_wr (int32 data, int32 addr, int32 access);
extern t_stat lpt_rd (int32 *data, int32 addr, int32 access);
extern t_stat lpt_wr (int32 data, int32 addr, int32 access);
extern t_stat dz_rd (int32 *data, int32 addr, int32 access);
extern t_stat dz_wr (int32 data, int32 addr, int32 access);
extern t_stat rk_rd (int32 *data, int32 addr, int32 access);
extern t_stat rk_wr (int32 data, int32 addr, int32 access);
extern int32 rk_inta (void);
extern int32 rk_enb;
/* extern t_stat rk6_rd (int32 *data, int32 addr, int32 access);
extern t_stat rk6_wr (int32 data, int32 addr, int32 access);
extern int32 rk6_inta (void);
extern int32 rk6_enb; */
extern t_stat rl_rd (int32 *data, int32 addr, int32 access);
extern t_stat rl_wr (int32 data, int32 addr, int32 access);
extern int32 rl_enb;
extern t_stat rp_rd (int32 *data, int32 addr, int32 access);
extern t_stat rp_wr (int32 data, int32 addr, int32 access);
extern int32 rp_inta (void);
extern int32 rp_enb;
extern t_stat rq_rd (int32 *data, int32 addr, int32 access);
extern t_stat rq_wr (int32 data, int32 addr, int32 access);
extern int32 rq_inta (void);
extern int32 rq_enb;
extern t_stat rx_rd (int32 *data, int32 addr, int32 access);
extern t_stat rx_wr (int32 data, int32 addr, int32 access);
extern int32 rx_enb;
extern t_stat dt_rd (int32 *data, int32 addr, int32 access);
extern t_stat dt_wr (int32 data, int32 addr, int32 access);
extern int32 dt_enb;
extern t_stat tm_rd (int32 *data, int32 addr, int32 access);
extern t_stat tm_wr (int32 data, int32 addr, int32 access);
extern int32 tm_enb;
extern t_stat ts_rd (int32 *data, int32 addr, int32 access);
extern t_stat ts_wr (int32 data, int32 addr, int32 access);
extern int32 ts_enb;

/* I/O data structures */

struct iolink {						/* I/O page linkage */
	int32	low;					/* low I/O addr */
	int32	high;					/* high I/O addr */
	int32	*enb;					/* enable flag */
	t_stat	(*read)();				/* read routine */
	t_stat	(*write)();  };				/* write routine */

struct iolink iotable[] = {
	{ IOBA_CPU, IOBA_CPU+IOLN_CPU, NULL, &CPU_rd, &CPU_wr },
	{ IOBA_STD, IOBA_STD+IOLN_STD, NULL, &std_rd, &std_wr },
	{ IOBA_LPT, IOBA_LPT+IOLN_LPT, NULL, &lpt_rd, &lpt_wr },
	{ IOBA_DZ,  IOBA_DZ +IOLN_DZ,  NULL, &dz_rd, &dz_wr },
	{ IOBA_RK,  IOBA_RK +IOLN_RK,  &rk_enb, &rk_rd, &rk_wr },
	{ IOBA_RL,  IOBA_RL +IOLN_RL,  &rl_enb, &rl_rd, &rl_wr },
	{ IOBA_RP,  IOBA_RP +IOLN_RP,  &rp_enb, &rp_rd, &rp_wr },
	{ IOBA_RQ,  IOBA_RQ +IOLN_RQ,  &rq_enb, &rq_rd, &rq_wr },
	{ IOBA_RX,  IOBA_RX +IOLN_RX,  &rx_enb, &rx_rd, &rx_wr },
	{ IOBA_TC,  IOBA_TC +IOLN_TC,  &dt_enb, &dt_rd, &dt_wr },
	{ IOBA_TM,  IOBA_TM +IOLN_TM,  &tm_enb, &tm_rd, &tm_wr },
	{ IOBA_TS,  IOBA_TS +IOLN_TS,  &ts_enb, &ts_rd, &ts_wr },
/*	{ IOBA_RK6, IOBA_RK6+IOLN_RK6, &rk6_enb, &rk6_rd, &rk6_wr }, */
	{ IOBA_APR, IOBA_APR+IOLN_APR, NULL, &APR_rd, &APR_wr },
	{ IOBA_APR1, IOBA_APR1+IOLN_APR1, NULL, &APR_rd, &APR_wr },
	{ IOBA_SRMM, IOBA_SRMM+IOLN_SRMM, NULL, &SR_MMR012_rd, &SR_MMR012_wr },
	{ IOBA_MMR3, IOBA_MMR3+IOLN_MMR3, NULL, &MMR3_rd, &MMR3_wr },
	{ IOBA_UBM, IOBA_UBM+IOLN_UBM, NULL, &ubm_rd, &ubm_wr },
	{ 0, 0, NULL, NULL, NULL }  };

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
	  &rp_inta, NULL, NULL, &rq_inta },
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

t_stat iopageR (int32 *data, int32 pa, int32 access)
{
t_stat stat;
struct iolink *p;

for (p = &iotable[0]; p -> low != 0; p++ ) {
	if ((pa >= p -> low) && (pa < p -> high) &&
	    ((p -> enb == NULL) || *p -> enb))  {
		stat = p -> read (data, pa, access);
		trap_req = calc_ints (ipl, trap_req);
		return stat;  }  }
return SCPE_NXM;
}

t_stat iopageW (int32 data, int32 pa, int32 access)
{
t_stat stat;
struct iolink *p;

for (p = &iotable[0]; p -> low != 0; p++ ) {
	if ((pa >= p -> low) && (pa < p -> high) &&
	    ((p -> enb == NULL) || *p -> enb))  {
		stat = p -> write (data, pa, access);
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
