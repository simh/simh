/* pdp11_rh.c: PDP-11 Massbus adapter simulator

   Copyright (c) 2004, Robert M Supnik

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

   rha, rhb		RH11/RH70 Massbus adapter

   WARNING: The interupt logic of the RH11/RH70 is unusual and must be
   simulated with great precision.  The RH11 has an internal interrupt
   request flop, CSTB INTR, which is controlled as follows:
   - Writing IE and DONE simultaneously sets CSTB INTR
   - Controller clear, INIT, and interrupt acknowledge clear CSTB INTR
     (and also clear IE)
   - A transition of DONE from 0 to 1 sets CSTB INTR from IE
   The output of CSTB INTR is OR'd with the AND of RPCS1<SC,DONE,IE> to
   create the interrupt request signal.  Thus,
   - The DONE interrupt is edge sensitive, but the SC interrupt is
     level sensitive.
   - The DONE interrupt, once set, is not disabled if IE is cleared,
     but the SC interrupt is.
*/

#if defined (VM_PDP10)					/* PDP10 version */
#error "PDP-10 uses pdp10_rp.c and pdp10_tu.c!"

#elif defined (VM_VAX)					/* VAX version */
#error "VAX uses vax780_mba.c!"

#else							/* PDP-11 version */
#include "pdp11_defs.h"
#endif

/* CS1 - base + 000 - control/status 1 */

#define CS1_OF		0
#define CS1_GO		CSR_GO				/* go */
#define CS1_V_FNC	1				/* function pos */
#define CS1_M_FNC	037				/* function mask */
#define CS1_FNC		(CS1_M_FNC << CS1_V_FNC)
#define FNC_XFER	024				/* >=? data xfr */
#define CS1_IE		CSR_IE				/* int enable */
#define CS1_DONE	CSR_DONE			/* ready */
#define CS1_V_UAE	8				/* Unibus addr ext */
#define CS1_M_UAE	03
#define CS1_UAE		(CS1_M_UAE << CS1_V_UAE)
#define CS1_DVA		0004000				/* drive avail NI */
#define CS1_MCPE	0020000				/* Mbus par err NI */
#define CS1_TRE		0040000				/* transfer err */
#define CS1_SC		0100000				/* special cond */
#define CS1_MBZ		0012000
#define CS1_DRV		(CS1_FNC | CS1_GO)
#define GET_FNC(x)	(((x) >> CS1_V_FNC) & CS1_M_FNC)

/* WC - base + 002 - word count */

#define WC_OF		1

/* BA - base + 004 - base address */

#define BA_OF		2
#define BA_MBZ		0000001				/* must be zero */

/* CS2 - base + 010 - control/status 2 */

#define CS2_OF		3
#define CS2_V_UNIT	0				/* unit pos */
#define CS2_M_UNIT	07				/* unit mask */
#define CS2_UNIT	(CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI		0000010				/* addr inhibit */
#define CS2_PAT		0000020				/* parity test NI */
#define CS2_CLR		0000040				/* controller clear */
#define CS2_IR		0000100				/* input ready */
#define CS2_OR		0000200				/* output ready */
#define CS2_MDPE	0000400				/* Mbus par err NI */
#define CS2_MXF		0001000				/* missed xfer NI */
#define CS2_PGE		0002000				/* program err */
#define CS2_NEM		0004000				/* nx mem err */
#define CS2_NED		0010000				/* nx drive err */
#define CS2_PE		0020000				/* parity err NI */
#define CS2_WCE		0040000				/* write check err */
#define CS2_DLT		0100000				/* data late NI */
#define CS2_MBZ		(CS2_CLR)
#define CS2_RW		(CS2_UNIT | CS2_UAI | CS2_PAT | CS2_MXF | CS2_PE)
#define CS2_ERR		(CS2_MDPE | CS2_MXF | CS2_PGE | CS2_NEM | \
			 CS2_NED | CS2_PE | CS2_WCE | CS2_DLT )
#define GET_UNIT(x)	(((x) >> CS2_V_UNIT) & CS2_M_UNIT)

/* DB - base + 022 - data buffer */

#define DB_OF		4

/* BAE - base + 050/34 - bus address extension */

#define BAE_OF		5
#define AE_M_MAE	0				/* addr ext pos */
#define AE_V_MAE	077				/* addr ext mask */
#define AE_MBZ		0177700

/* CS3 - base + 052/36 - control/status 3 */

#define CS3_OF		6
#define CS3_APE		0100000				/* addr perr - NI */
#define CS3_DPO		0040000				/* data perr odd - NI */
#define CS3_DPE		0020000				/* data perr even - NI */
#define CS3_WCO		0010000				/* wchk err odd */
#define CS3_WCE		0004000				/* wchk err even */
#define CS3_DBL		0002000				/* dbl word xfer - NI */
#define CS3_IPCK	0000017				/* wrong par - NI */
#define CS3_ERR		(CS3_APE|CS3_DPO|CS3_DPE|CS3_WCO|CS3_WCE)
#define CS3_MBZ		0001660
#define CS3_RW		(CS1_IE | CS3_IPCK)

#define MBA_OFSMASK	077				/* max 32 reg */
#define INT		0000				/* int reg flag */
#define EXT		0100				/* ext reg flag */

/* Declarations */

#define RH11		(cpu_opt & OPT_RH11)

struct mbctx {
	uint32 cs1;					/* ctrl/status 1 */
	uint32 wc;					/* word count */
	uint32 ba;					/* bus addr */
	uint32 cs2;					/* ctrl/status 2 */
	uint32 db;					/* data buffer */
	uint32 bae;					/* addr ext */
	uint32 cs3;					/* ctrl/status 3 */
	uint32 iff;					/* int flip flop */
	};

typedef struct mbctx MBACTX;
MBACTX massbus[MBA_NUM];

extern int32 cpu_opt, cpu_bme;
extern uint16 *M;
extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];
extern UNIT cpu_unit;
extern FILE *sim_deb;
extern FILE *sim_log;
extern int32 sim_switches;

t_stat mba_reset (DEVICE *dptr);
t_stat mba_rd (int32 *val, int32 pa, int32 access);
t_stat mba_wr (int32 val, int32 pa, int32 access);
t_stat mba_set_type (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat mba_show_type (FILE *st, UNIT *uptr, int32 val, void *desc);
int32 mba0_inta (void);
int32 mba1_inta (void);
void mba_set_int (uint32 mb);
void mba_clr_int (uint32 mb);
void mba_upd_cs1 (uint32 set, uint32 clr, uint32 mb);
void mba_set_cs2 (uint32 flg, uint32 mb);
uint32 mba_map_pa (int32 pa, int32 *ofs);
DEVICE mba0_dev, mba1_dev;

extern uint32 Map_Addr (uint32 ba);

/* Maps */

static MBACTX *ctxmap[MBA_NUM] = { &massbus[0], &massbus[1] };
static DEVICE *devmap[MBA_NUM] = { &mba0_dev, &mba1_dev };

/* Massbus register dispatches */

static t_stat (*mbregR[MBA_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*mbregW[MBA_NUM])(int32 dat, int32 ad, int32 md);
static int32 (*mbabort[MBA_NUM])(void);

/* Unibus to register offset map */

static int32 mba_mapofs[(MBA_OFSMASK + 1) >> 1] =
 {  INT|0,  INT|1,  INT|2,  EXT|5,  INT|3,  EXT|1,  EXT|2,  EXT|4,
    EXT|7,  INT|4,  EXT|3,  EXT|6,  EXT|8,  EXT|9,  EXT|10, EXT|11,
    EXT|12, EXT|13, EXT|14, EXT|15, EXT|16, EXT|17, EXT|18, EXT|19,
    EXT|20, EXT|21, EXT|22, EXT|23, EXT|24, EXT|25, EXT|26, EXT|27 };

/* Massbus adapter data structures

   mbax_dev	RHx device descriptor
   mbax_unit	RHx units
   mbax_reg	RHx register list
*/

DIB mba0_dib = { IOBA_RP, IOLN_RP, &mba_rd, &mba_wr,
		1, IVCL (RP), VEC_RP, { &mba0_inta } };

UNIT mba0_unit = { UDATA (NULL, 0, 0) };

REG mba0_reg[] = {
	{ ORDATA (CS1, massbus[0].cs1, 16) },
	{ ORDATA (WC, massbus[0].wc, 16) },
	{ ORDATA (BA, massbus[0].ba, 16) },
	{ ORDATA (CS2, massbus[0].cs2, 16) },
	{ ORDATA (DB, massbus[0].db, 16) },
	{ ORDATA (BAE, massbus[0].bae, 6) },
	{ ORDATA (CS3, massbus[0].cs3, 16) },
	{ FLDATA (IFF, massbus[0].iff, 0) },
	{ FLDATA (INT, IREQ (RP), INT_V_RP) },
	{ FLDATA (SC, massbus[0].cs1, CSR_V_ERR) },
	{ FLDATA (DONE, massbus[0].cs1, CSR_V_DONE) },
	{ FLDATA (IE, massbus[0].cs1, CSR_V_IE) },
	{ ORDATA (DEVADDR, mba0_dib.ba, 32), REG_HRO },
	{ ORDATA (DEVVEC, mba0_dib.vec, 16), REG_HRO },
	{ NULL }  };

MTAB mba0_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0100, "ADDRESS", "ADDRESS",
		&set_addr, &show_addr, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
		&set_vec, &show_vec, NULL },
	{ 0 }  };

DEVICE mba0_dev = {
	"RHA", &mba0_unit, mba0_reg, mba0_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &mba_reset,
	NULL, NULL, NULL,
	&mba0_dib, DEV_DEBUG | DEV_DISABLE | DEV_UBUS | DEV_QBUS };

DIB mba1_dib = { IOBA_TU, IOLN_TU, &mba_rd, &mba_wr,
		1, IVCL (TU), VEC_TU, { &mba1_inta } };

UNIT mba1_unit = { UDATA (NULL, 0, 0) };

REG mba1_reg[] = {
	{ ORDATA (CS1, massbus[1].cs1, 16) },
	{ ORDATA (WC, massbus[1].wc, 16) },
	{ ORDATA (BA, massbus[1].ba, 16) },
	{ ORDATA (CS2, massbus[1].cs2, 16) },
	{ ORDATA (DB, massbus[1].db, 16) },
	{ ORDATA (BAE, massbus[1].bae, 6) },
	{ ORDATA (CS3, massbus[1].cs3, 16) },
	{ FLDATA (IFF, massbus[1].iff, 0) },
	{ FLDATA (INT, IREQ (TU), INT_V_TU) },
	{ FLDATA (SC, massbus[1].cs1, CSR_V_ERR) },
	{ FLDATA (DONE, massbus[1].cs1, CSR_V_DONE) },
	{ FLDATA (IE, massbus[1].cs1, CSR_V_IE) },
	{ ORDATA (DEVADDR, mba1_dib.ba, 32), REG_HRO },
	{ ORDATA (DEVVEC, mba1_dib.vec, 16), REG_HRO },
	{ NULL }  };

MTAB mba1_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0040, "ADDRESS", "ADDRESS",
		&set_addr, &show_addr, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
		&set_vec, &show_vec, NULL },
	{ 0 }  };

DEVICE mba1_dev = {
	"RHB", &mba1_unit, mba1_reg, mba1_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &mba_reset,
	NULL, NULL, NULL,
	&mba1_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_QBUS };

/* Read Massbus adapter register */

t_stat mba_rd (int32 *val, int32 pa, int32 mode)
{
int32 ofs, dat, mb, drv;
t_stat r;
MBACTX *mbp;

mb = mba_map_pa (pa, &ofs);				/* get mb number */
if ((mb < 0) || (ofs < 0)) return SCPE_NXM;		/* valid? */
mbp = ctxmap[mb];					/* get context */
drv = GET_UNIT (mbp->cs2);				/* get drive */
mba_upd_cs1 (0, 0, mb);					/* update CS1 */

if (ofs & EXT) {					/* external? */
	if (!mbregR[mb]) return SCPE_NXM;		/* device there? */
	r = mbregR[mb] (val, ofs & ~EXT, drv);		/* call device */
	if (r == MBE_NXD) mba_set_cs2 (CS2_NED, mb);	/* nx drive? */
	else if (r == MBE_NXR) return SCPE_NXM;		/* nx reg? */
	return SCPE_OK; 
	}

switch (ofs) {						/* case on reg */
case CS1_OF:						/* CS1 */
	if (!mbregR[mb]) return SCPE_NXM;		/* nx device? */
	r = mbregR[mb] (&dat, ofs, drv);		/* get dev cs1 */
	if (r == MBE_NXD) mba_set_cs2 (CS2_NED, mb);	/* nx drive? */
	*val = mbp->cs1 | dat;
	break;
case WC_OF:						/* WC */
	*val = mbp->wc;
	break;
case BA_OF:						/* BA */
	*val = mbp->ba & ~BA_MBZ;
	break;
case CS2_OF:						/* CS2 */
	*val = mbp->cs2 = (mbp->cs2 & ~CS2_MBZ) | CS2_IR | CS2_OR;
	break;
case DB_OF:						/* DB */
	*val = mbp->db;
	break;
case BAE_OF:						/* BAE */
	*val = mbp->bae = mbp->bae & ~AE_MBZ;
	break;
case CS3_OF:						/* CS3 */
	*val = mbp->cs3 = (mbp->cs3 & ~(CS1_IE | CS3_MBZ)) | (mbp->cs1 & CS1_IE);
	break;
default:						/* huh? */
	return SCPE_NXM;
	}
return SCPE_OK;
}

t_stat mba_wr (int32 val, int32 pa, int32 access)
{
int32 ofs, cs1f, drv, mb;
t_stat r;
t_bool cs1dt;
MBACTX *mbp;

mb = mba_map_pa (pa, &ofs);				/* get mb number */
if ((mb < 0) || (ofs < 0)) return SCPE_NXM;		/* valid? */
mbp = ctxmap[mb];					/* get context */
drv = GET_UNIT (mbp->cs2);				/* get drive */

if (ofs & EXT) {					/* external? */
	if (!mbregW[mb]) return SCPE_NXM;		/* device there? */
	if ((access == WRITEB) && (pa & 1))		/* byte writes */
	    val = val << 8;				/* don't work */
	r = mbregW[mb] (val, ofs & ~EXT, drv);		/* write dev reg */
	if (r == MBE_NXD) mba_set_cs2 (CS2_NED, mb);	/* nx drive? */
	else if (r == MBE_NXR) return SCPE_NXM;		/* nx reg? */
	mba_upd_cs1 (0, 0, mb);				/* update status */
	return SCPE_OK;
	} 

cs1f = 0;						/* no int on cs1 upd */
switch (ofs) {						/* case on reg */
case CS1_OF:						/* CS1 */
	if (!mbregW[mb]) return SCPE_NXM;		/* device exist? */
	if ((access == WRITEB) && (pa & 1)) val = val << 8;
	if (val & CS1_TRE) {				/* error clear? */
	    mbp->cs1 = mbp->cs1 & ~CS1_TRE;		/* clr CS1<TRE> */
	    mbp->cs2 = mbp->cs2 & ~CS2_ERR;		/* clr CS2<15:8> */
	    mbp->cs3 = mbp->cs3 & ~CS3_ERR;		/* clr CS3<15:11> */
	    }
	if ((access == WRITE) || (pa & 1)) {		/* hi byte write? */
	    if (mbp->cs1 & CS1_DONE)			/* done set? */
		mbp->cs1 = (mbp->cs1 & ~CS1_UAE) | (val & CS1_UAE);  }
	if ((access == WRITE) || !(pa & 1)) {		/* lo byte write? */
	    if ((val & CS1_DONE) && (val & CS1_IE))	/* to DONE+IE? */
		mbp->iff = 1;				/* set CSTB INTR */
	    mbp->cs1 = (mbp->cs1 & ~CS1_IE) | (val & CS1_IE);
	    cs1dt = (val & CS1_GO) && (GET_FNC (val) >= FNC_XFER);
	    if (cs1dt && ((mbp->cs1 & CS1_DONE) == 0))	/* dt, done clr? */
		mba_set_cs2 (CS2_PGE, mb);		/* prgm error */
	    else {
		r = mbregW[mb] (val & 077, ofs, drv);	/* write dev CS1 */
		if (r == MBE_NXD) mba_set_cs2 (CS2_NED, mb);	/* nx drive? */
		else if (r == MBE_NXR) return SCPE_NXM;	/* nx reg? */
		else if (cs1dt && (r == SCPE_OK)) {	/* xfer, no err? */
		    mbp->cs1 &= ~(CS1_TRE | CS1_MCPE | CS1_DONE);
		    mbp->cs2 &= ~CS2_ERR;		/* clear errors */
		    mbp->cs3 &= ~(CS3_ERR | CS3_DBL);
		    }
		}
	    }
	mbp->cs3 = (mbp->cs3 & ~CS1_IE) |		/* update CS3 */
	    (mbp->cs1 & CS1_IE);
	mbp->bae = (mbp->bae & ~CS1_M_UAE) |		/* update BAE */
	    ((mbp->cs1 >> CS1_V_UAE) & CS1_M_UAE);
	break;	
case WC_OF:						/* WC */
	if (access == WRITEB) val = (pa & 1)?
	    (mbp->wc & 0377) | (val << 8): (mbp->wc & ~0377) | val;
	mbp->wc = val;
	break;
case BA_OF:						/* BA */
	if (access == WRITEB) val = (pa & 1)?
	    (mbp->ba & 0377) | (val << 8): (mbp->ba & ~0377) | val;
	mbp->ba = val & ~BA_MBZ;
	break;
case CS2_OF:						/* CS2 */
	if ((access == WRITEB) && (pa & 1)) val = val << 8;
	if (val & CS2_CLR) mba_reset (devmap[mb]);	/* init? */
	else {
	    if ((val & ~mbp->cs2) & (CS2_PE | CS2_MXF))
		cs1f = CS1_SC;				/* diagn intr */
	    if (access == WRITEB) val = (mbp->cs2 &	/* merge val */
		((pa & 1)? 0377: 0177400)) | val;
	    mbp->cs2 = (mbp->cs2 & ~CS2_RW) |
		(val & CS2_RW) | CS2_IR | CS2_OR;  }
	break;
case DB_OF:						/* DB */
	if (access == WRITEB) val = (pa & 1)?
	    (mbp->db & 0377) | (val << 8): (mbp->db & ~0377) | val;
	mbp->db = val;
	break;
case BAE_OF:						/* BAE */
	if ((access == WRITEB) && (pa & 1)) break;
	mbp->bae = val & ~AE_MBZ;
	mbp->cs1 = (mbp->cs1 & ~CS1_UAE) |		/* update CS1 */
	    ((mbp->bae << CS1_V_UAE) & CS1_UAE);
	break;
case CS3_OF:						/* CS3 */
	if ((access == WRITEB) && (pa & 1)) break;
	mbp->cs3 = (mbp->cs3 & ~CS3_RW) | (val & CS3_RW);
	mbp->cs1 = (mbp->cs1 & ~CS1_IE) |		/* update CS1 */
	    (mbp->cs3 & CS1_IE);
	break;
default:
	return SCPE_NXM;
	}
mba_upd_cs1 (cs1f, 0, mb);				/* update status */
return SCPE_OK;
}

/* Massbus I/O routines

   mb_rdbufW 	-	fetch word buffer from memory
   mb_wrbufW 	-	store word buffer into memory
   mb_chbufW	-	compare word buffer with memory

   Returns number of bytes successfully transferred/checked
*/

int32 mba_rdbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;						/* bc even */
if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = (mbp->bae << 16) | mbp->ba;			/* get busaddr */
mbc = (0200000 - mbp->wc) << 1;				/* MB byte count */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (RH11 && cpu_bme) pa = Map_Addr (ba);	/* map addr */
	else pa = ba;
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_set_cs2 (CS2_NEM, mb);			/* set error */
	    break;  }
	pbc = UBM_PAGSIZE - UBM_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	for (j = 0; j < pbc; j = j + 2) {		/* loop by words */
	    *buf++ = M[pa >> 1];			/* fetch word */
	    if (!(mbp->cs2 & CS2_UAI)) {		/* if not inhb */
		ba = ba + 2;				/* incr ba, pa */
		pa = pa + 2;
		}
	    }
	}
mbp->wc = (mbp->wc + (bc >> 1)) & DMASK;		/* update wc */
mbp->ba = ba & DMASK;					/* update ba */
mbp->bae = (ba >> 16) & ~AE_MBZ;			/* upper 6b */
mbp->cs1 = (mbp->cs1 & ~ CS1_UAE) |			/* update CS1 */
	((mbp->bae << CS1_V_UAE) & CS1_UAE);
return i;
}

int32 mba_wrbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;						/* bc even */
if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = (mbp->bae << 16) | mbp->ba;			/* get busaddr */
mbc = (0200000 - mbp->wc) << 1;				/* MB byte count */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (RH11 && cpu_bme) pa = Map_Addr (ba);	/* map addr */
	else pa = ba;
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_set_cs2 (CS2_NEM, mb);			/* set error */
	    break;  }
	pbc = UBM_PAGSIZE - UBM_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	for (j = 0; j < pbc; j = j + 2) {		/* loop by words */
	    M[pa >> 1] = *buf++;			/* put word */
	    if (!(mbp->cs2 & CS2_UAI)) {		/* if not inhb */
		ba = ba + 2;				/* incr ba, pa */
		pa = pa + 2;
		}
	    }
	}
mbp->wc = (mbp->wc + (bc >> 1)) & DMASK;		/* update wc */
mbp->ba = ba & DMASK;					/* update ba */
mbp->bae = (ba >> 16) & ~AE_MBZ;			/* upper 6b */
mbp->cs1 = (mbp->cs1 & ~ CS1_UAE) |			/* update CS1 */
	((mbp->bae << CS1_V_UAE) & CS1_UAE);
return i;
}

int32 mba_chbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa;

bc = bc & ~1;						/* bc even */
if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = (mbp->bae << 16) | mbp->ba;			/* get busaddr */
mbc = (0200000 - mbp->wc) << 1;				/* MB byte count */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (RH11 && cpu_bme) pa = Map_Addr (ba);	/* map addr */
	else pa = ba;
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_set_cs2 (CS2_NEM, mb);			/* set error */
	    break;  }
	pbc = UBM_PAGSIZE - UBM_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	for (j = 0; j < pbc; j = j + 2) {		/* loop by words */
	    mbp->db = *buf++;				/* get dev word */
	    if (M[pa >> 1] != mbp->db) {		/* miscompare? */
		mba_set_cs2 (CS2_WCE, mb);		/* set error */
		mbp->cs3 = mbp->cs3 |			/* set even/odd */
		    ((pa & 1)? CS3_WCO: CS3_WCE);
		break;  }
	    if (!(mbp->cs2 & CS2_UAI)) {		/* if not inhb */
		ba = ba + 2;				/* incr ba, pa */
		pa = pa + 2;
		}
	    }
	}
mbp->wc = (mbp->wc + (bc >> 1)) & DMASK;		/* update wc */
mbp->ba = ba & DMASK;					/* update ba */
mbp->bae = (ba >> 16) & ~AE_MBZ;			/* upper 6b */
mbp->cs1 = (mbp->cs1 & ~ CS1_UAE) |			/* update CS1 */
	((mbp->bae << CS1_V_UAE) & CS1_UAE);
return i;
}

/* Device access, status, and interrupt routines */

void mba_set_don (uint32 mb)
{
mba_upd_cs1 (CS1_DONE, 0, mb);
return;
}

void mba_upd_ata (uint32 mb, uint32 val)
{
if (val) mba_upd_cs1 (CS1_SC, 0, mb);
else mba_upd_cs1 (0, CS1_SC, mb);
return;
}

void mba_set_exc (uint32 mb)
{
mba_upd_cs1 (CS1_TRE | CS1_DONE, 0, mb);
return;
}

int32 mba_get_bc (uint32 mb)
{
MBACTX *mbp;

if (mb >= MBA_NUM) return 0;
mbp = ctxmap[mb];
return ((0200000 - mbp->wc) << 1);
}

int32 mba_get_csr (uint32 mb)
{
DEVICE *dptr;
DIB *dibp;

if (mb >= MBA_NUM) return 0;
dptr = devmap[mb];
dibp = (DIB *) dptr->ctxt;
return dibp->ba;
}

void mba_set_int (uint32 mb)
{
DEVICE *dptr;
DIB *dibp;

if (mb >= MBA_NUM) return;
dptr = devmap[mb];
dibp = (DIB *) dptr->ctxt;
int_req[dibp->vloc >> 5] |= (1 << (dibp->vloc & 037));
return;
}

void mba_clr_int (uint32 mb)
{
DEVICE *dptr;
DIB *dibp;

if (mb >= MBA_NUM) return;
dptr = devmap[mb];
dibp = (DIB *) dptr->ctxt;
int_req[dibp->vloc >> 5] &= ~(1 << (dibp->vloc & 037));
return;
}

void mba_upd_cs1 (uint32 set, uint32 clr, uint32 mb)
{
MBACTX *mbp;

if (mb >= MBA_NUM) return;
mbp = ctxmap[mb];
if ((set & ~mbp->cs1) & CS1_DONE)			/* DONE 0 to 1? */
	mbp->iff = (mbp->cs1 & CS1_IE)? 1: 0;		/* CSTB INTR <- IE */
mbp->cs1 = (mbp->cs1 & ~(clr | CS1_MCPE | CS1_MBZ | CS1_DRV)) | CS1_DVA | set;
if (mbp->cs2 & CS2_ERR) mbp->cs1 = mbp->cs1 | CS1_TRE | CS1_SC;
else if (mbp->cs1 & CS1_TRE) mbp->cs1 = mbp->cs1 | CS1_SC;
if (mbp->iff || ((mbp->cs1 & CS1_SC) && (mbp->cs1 & CS1_DONE) && (mbp->cs1 & CS1_IE)))
	mba_set_int (mb);
else mba_clr_int (mb);
return;
}

void mba_set_cs2 (uint32 flag, uint32 mb)
{
MBACTX *mbp;

if (mb >= MBA_NUM) return;
mbp = ctxmap[mb];
mbp->cs2 = mbp->cs2 | flag;
mba_upd_cs1 (0, 0, mb);
return;
}

/* Interrupt acknowledge */

int32 mba0_inta (void)
{
massbus[0].cs1 &= ~CS1_IE;				/* clear int enable */
massbus[0].cs3 &= ~CS1_IE;				/* in both registers */
massbus[0].iff = 0;					/* clear CSTB INTR */
return mba0_dib.vec;					/* acknowledge */
}

int32 mba1_inta (void)
{
massbus[1].cs1 &= ~CS1_IE;				/* clear int enable */
massbus[1].cs3 &= ~CS1_IE;				/* in both registers */
massbus[1].iff = 0;					/* clear CSTB INTR */
return mba1_dib.vec;					/* acknowledge */
}

/* Map physical address to Massbus number, offset */

uint32 mba_map_pa (int32 pa, int32 *ofs)
{
int32 i, uo, ba, lnt;
DEVICE *dptr;
DIB *dibp;

for (i = 0; i < MBA_NUM; i++) {				/* loop thru ctrls */
	dptr = devmap[i];				/* get device */
	dibp = (DIB *) dptr->ctxt;			/* get DIB */
	ba = dibp->ba;
	lnt = dibp->lnt;
	if ((pa >= ba) &&				/* in range? */
	    (pa < (ba + lnt))) {
	    if (pa < (ba + (lnt - 4))) {		/* not last two? */
		uo = ((pa - ba) & MBA_OFSMASK) >> 1;	/* get Unibus offset */
	        *ofs = mba_mapofs[uo];			/* map thru PROM */
	        return i;				/* return ctrl idx */
		}
	    else if (RH11) return -1;			/* RH11? done */
	    else {					/* RH70 */
		uo = (pa - (ba + (lnt - 4))) >> 1;	/* offset relative */
		*ofs = BAE_OF + uo;			/* to BAE */
		return i;
		}
	    }
	}
return -1;
}

/* Reset Massbus adapter */

t_stat mba_reset (DEVICE *dptr)
{
int32 mb;
MBACTX *mbp;

for (mb = 0; mb < MBA_NUM; mb++) {
	mbp = ctxmap[mb];
	if (dptr == devmap[mb]) break;
	}
if (mb >= MBA_NUM) return SCPE_NOFNC;
mbp->cs1 = CS1_DONE;
mbp->wc = 0;
mbp->ba = 0;
mbp->cs2 = 0;
mbp->db = 0;
mbp->bae= 0;
mbp->cs3 = 0;
mbp->iff = 0;
mba_clr_int (mb);
if (mbabort[mb]) mbabort[mb] ();
return SCPE_OK;
}

/* Show Massbus adapter number */

t_stat mba_show_num (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr = find_dev_from_unit (uptr);
DIB *dibp;

if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
fprintf (st, "Massbus adapter %d", dibp->ba);
return SCPE_OK;
}

/* Init Mbus tables */

void init_mbus_tab (void)
{
uint32 i;

for (i = 0; i < MBA_NUM; i++) {
	mbregR[i] = NULL;
	mbregW[i] = NULL;
	mbabort[i] = NULL;
	}
return;
}

/* Build dispatch tables */

t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp)
{
uint32 idx;

if ((dptr == NULL) || (dibp == NULL)) return SCPE_IERR;	/* validate args */
idx = dibp->ba;						/* Mbus # */
if (idx >= MBA_NUM) return SCPE_STOP;
if ((mbregR[idx] && dibp->rd &&				/* conflict? */
    (mbregR[idx] != dibp->rd)) ||
    (mbregW[idx] && dibp->wr &&
    (mbregW[idx] != dibp->wr)) ||
    (mbabort[idx] && dibp->ack[0] &&
    (mbabort[idx] != dibp->ack[0]))) {
	printf ("Massbus %s assignment conflict at %d\n",
	    sim_dname (dptr), dibp->ba);
	if (sim_log) fprintf (sim_log,
	    "Massbus %s assignment conflict at %d\n",
	    sim_dname (dptr), dibp->ba);
	return SCPE_STOP;
	}
if (dibp->rd) mbregR[idx] = dibp->rd;			/* set rd dispatch */
if (dibp->wr) mbregW[idx] = dibp->wr;			/* set wr dispatch */
if (dibp->ack[0]) mbabort[idx] = dibp->ack[0];		/* set abort dispatch */
return SCPE_OK;
}

