/* vax780_mba.c: VAX 11/780 Massbus adapter

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

   mba0, mba1		RH780 Massbus adapter
*/

#include "vax_defs.h"

/* Massbus */

#define MBA_NMAPR	256				/* number of map reg */
#define MBA_V_RTYPE	10				/* nexus addr: reg type */
#define MBA_M_RTYPE	0x3
#define  MBART_INT	0x0				/* internal */
#define  MBART_EXT	0x1				/* external */
#define  MBART_MAP	0x2				/* map */
#define MBA_V_INTOFS	2				/* int reg: reg ofs */
#define MBA_M_INTOFS	0xFF
#define MBA_V_DRV	7				/* ext reg: drive num */
#define MBA_M_DRV	0x7
#define MBA_V_DEVOFS	2				/* ext reg: reg ofs */
#define MBA_M_DEVOFS	0x1F
#define MBA_RTYPE(x)	(((x) >> MBA_V_RTYPE) & MBA_M_RTYPE)
#define MBA_INTOFS(x)	(((x) >> MBA_V_INTOFS) & MBA_M_INTOFS)
#define MBA_EXTDRV(x)	(((x) >> MBA_V_DRV) & MBA_M_DRV)
#define MBA_EXTOFS(x)	(((x) >> MBA_V_DEVOFS) & MBA_M_DEVOFS)

/* Massbus configuration register */

#define MBACNF_OF	0x0
#define MBACNF_ADPDN	0x00800000			/* adap pdn - ni */
#define MBACNF_ADPUP	0x00400000			/* adap pup - ni */
#define MBACNF_CODE	0x00000020
#define MBACNF_RD	(SBI_FAULTS|MBACNF_W1C)
#define MBACNF_W1C	0x00C00000

/* Control register */

#define MBACR_OF	0x1
#define MBACR_MNT	0x00000008			/* maint */
#define MBACR_IE	0x00000004			/* int enable */
#define MBACR_ABORT	0x00000002			/* abort */
#define MBACR_INIT	0x00000001
#define MBACR_RD	0x0000000E
#define MBACR_WR	0x0000000E

/* Status register */

#define MBASR_OF	0x2
#define MBASR_DTBUSY	0x80000000			/* DT busy RO */
#define MBASR_NRCONF	0x40000000			/* no conf - ni W1C */
#define MBASR_CRD	0x20000000			/* CRD - ni W1C */
#define MBASR_CBH	0x00800000			/* CBHUNG - ni W1C */
#define MBASR_PGE	0x00080000			/* prog err - W1C int */
#define MBASR_NFD	0x00040000			/* nx drive - W1C int */
#define MBASR_MCPE	0x00020000			/* ctl perr - ni W1C int */
#define MBASR_ATA	0x00010000			/* attn - W1C int */
#define MBASR_SPE	0x00004000			/* silo par err - ni W1C int */
#define MBASR_DTCMP	0x00002000			/* xfr done - W1C int */
#define MBASR_DTABT	0x00001000			/* abort - W1C int */
#define MBASR_DLT	0x00000800			/* dat late - ni W1C abt */
#define MBASR_WCEU	0x00000400			/* wrchk upper - W1C abt */
#define MBASR_WCEL	0x00000200			/* wrchk lower - W1C abt */
#define MBASR_MXF	0x00000100			/* miss xfr - ni W1C abt */
#define MBASR_MBEXC	0x00000080			/* except - ni W1C abt */
#define MBASR_MBDPE	0x00000040			/* dat perr - ni W1C abt */
#define MBASR_MAPPE	0x00000020			/* map perr - ni W1C abt */
#define MBASR_INVM	0x00000010			/* inv map - W1C abt */
#define MBASR_ERCONF	0x00000008			/* err conf - ni W1C abt */
#define MBASR_RDS	0x00000004			/* RDS - ni W1C abt */
#define MBASR_ITMO	0x00000002			/* timeout - W1C abt */
#define MBASR_RTMO	0x00000001			/* rd timeout - W1C abt */
#define MBASR_RD	0xE08F7FFF
#define MBASR_W1C	0x608F7FFF
#define MBASR_ABORTS	0x00000FFF
#define MBASR_INTR	0x000F7000

/* Virtual address register */

#define MBAVA_OF	0x3
#define MBAVA_RD	0x0001FFFF
#define MBAVA_WR	(MBAVA_RD)

/* Byte count */

#define MBABC_OF	0x4
#define MBABC_RD	0xFFFFFFFF
#define MBABC_WR	0x0000FFFF
#define MBABC_V_CNT	16				/* active count */

/* Diagnostic register */

#define MBADR_OF	0x5
#define MBADR_RD	0xFFFFFFFF
#define MBADR_WR	0xFFC00000

/* Selected map entry - read only */

#define MBASMR_OF	0x6
#define MBASMR_RD	(MBAMAP_RD)

/* Command register (SBI) - read only */

#define MBACMD_OF	0x7

#define MBAMAX_OF	0x8

/* External registers */

#define MBA_CS1		0x00				/* device CSR1 */
#define MBA_CS1_WR	0x3F				/* writeable bits */
#define MBA_CS1_DT	0x28				/* >= for data xfr */

/* Map registers */

#define MBAMAP_VLD	0x80000000			/* valid */
#define MBAMAP_PAG	0x001FFFFF
#define MBAMAP_RD	(MBAMAP_VLD | MBAMAP_PAG)
#define MBAMAP_WR	(MBAMAP_RD)

struct mbctx {
	uint32 cnf;					/* config reg */
	uint32 cr;					/* control reg */
	uint32 sr;					/* status reg */
	uint32 va;					/* virt addr */
	uint32 bc;					/* byte count */
	uint32 dr;					/* diag reg */
	uint32 smr;					/* sel map reg */
	uint32 map[MBA_NMAPR];				/* map */
	};

typedef struct mbctx MBACTX;
MBACTX massbus[MBA_NUM];

extern uint32 nexus_req[NEXUS_HLVL];
extern UNIT cpu_unit;
extern FILE *sim_log;
extern int32 sim_switches;

t_stat mba_reset (DEVICE *dptr);
t_stat mba_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mba_wrreg (int32 val, int32 pa, int32 lnt);
t_bool mba_map_addr (uint32 va, uint32 *ma, uint32 mb);
void mba_set_int (uint32 mb);
void mba_clr_int (uint32 mb);
void mba_upd_sr (uint32 set, uint32 clr, uint32 mb);
DEVICE mba0_dev, mba1_dev;
DIB mba0_dib, mba1_dib;

extern int32 ReadB (uint32 pa);
extern int32 ReadW (uint32 pa);
extern int32 ReadL (uint32 pa);
extern void WriteB (uint32 pa, int32 val);
extern void WriteW (uint32 pa, int32 val);
extern void WriteL (uint32 pa, int32 val);

/* Maps */

static MBACTX *ctxmap[MBA_NUM] = { &massbus[0], &massbus[1] };
static DEVICE *devmap[MBA_NUM] = { &mba0_dev, &mba1_dev };
static DIB *dibmap[MBA_NUM] = { &mba0_dib, &mba1_dib };

/* Massbus register dispatches */

static t_stat (*mbregR[MBA_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*mbregW[MBA_NUM])(int32 dat, int32 ad, int32 md);
static int32 (*mbabort[MBA_NUM])(void);

/* Massbus adapter data structures

   mba_dev	UBA device descriptor
   mba_unit	UBA units
   mba_reg	UBA register list
*/

DIB mba0_dib = { TR_MBA0, 0, &mba_rdreg, &mba_wrreg, 0, NVCL (MBA0) };

UNIT mba0_unit = { UDATA (NULL, 0, 0) };

REG mba0_reg[] = {
	{ HRDATA (CNFR, massbus[0].cnf, 32) },
	{ HRDATA (CR, massbus[0].cr, 4) },
	{ HRDATA (SR, massbus[0].sr, 32) },
	{ HRDATA (VA, massbus[0].va, 17) },
	{ HRDATA (BC, massbus[0].bc, 32) },
	{ HRDATA (DR, massbus[0].dr, 32) },
	{ HRDATA (SMR, massbus[0].dr, 32) },
	{ BRDATA (MAP, massbus[0].map, 16, 32, MBA_NMAPR) },
	{ FLDATA (NEXINT, nexus_req[IPL_MBA0], TR_MBA0) },
	{ NULL }  };

MTAB mba0_mod[] = {
	{ MTAB_XTD|MTAB_VDV, TR_MBA0, "NEXUS", NULL,
	  NULL, &show_nexus },
	{ 0 }  };

DEVICE mba0_dev = {
	"MBA0", &mba0_unit, mba0_reg, mba0_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &mba_reset,
	NULL, NULL, NULL,
	&mba0_dib, DEV_NEXUS };

DIB mba1_dib = { TR_MBA1, 0, &mba_rdreg, &mba_wrreg, 0, NVCL (MBA0) };

UNIT mba1_unit = { UDATA (NULL, 0, 0) };

MTAB mba1_mod[] = {
	{ MTAB_XTD|MTAB_VDV, TR_MBA1, "NEXUS", NULL,
	  NULL, &show_nexus },
	{ 0 }  };

REG mba1_reg[] = {
	{ HRDATA (CNFR, massbus[1].cnf, 32) },
	{ HRDATA (CR, massbus[1].cr, 4) },
	{ HRDATA (SR, massbus[1].sr, 32) },
	{ HRDATA (VA, massbus[1].va, 17) },
	{ HRDATA (BC, massbus[1].bc, 32) },
	{ HRDATA (DR, massbus[1].dr, 32) },
	{ HRDATA (SMR, massbus[1].dr, 32) },
	{ BRDATA (MAP, massbus[1].map, 16, 32, MBA_NMAPR) },
	{ FLDATA (NEXINT, nexus_req[IPL_MBA1], TR_MBA1) },
	{ NULL }  };

DEVICE mba1_dev = {
	"MBA1", &mba1_unit, mba1_reg, mba1_mod,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &mba_reset,
	NULL, NULL, NULL,
	&mba1_dib, DEV_NEXUS };

/* Read Massbus adapter register */

t_stat mba_rdreg (int32 *val, int32 pa, int32 mode)
{
int32 mb, ofs, drv, rtype;
t_stat r;
MBACTX *mbp;

mb = NEXUS_GETNEX (pa) - TR_MBA0;			/* get MBA */
if (mb >= MBA_NUM) return SCPE_NXM;			/* valid? */
mbp = ctxmap[mb];					/* get context */
rtype = MBA_RTYPE (pa);					/* get reg type */

switch (rtype) {					/* case on type */

case MBART_INT:						/* internal */
	ofs = MBA_INTOFS (pa);				/* check range */
	if (ofs >= MBAMAX_OF) return SCPE_NXM;
	switch (ofs) {
	case MBACNF_OF:					/* CNF */
	    *val = (mbp->cnf & MBACNF_RD) | MBACNF_CODE;
	    break;
	case MBACR_OF:					/* CR */
	    *val = mbp->cr & MBACR_RD;
	    break;
	case MBASR_OF:					/* SR */
	    *val = mbp->sr & MBASR_RD;
	    break;
	case MBAVA_OF:					/* VA */
	    *val = mbp->va & MBAVA_RD;
	    break;
	case MBABC_OF:					/* BC */
	    *val = mbp->bc & MBABC_RD;
	     break;
	case MBADR_OF:					/* DR */
	    *val = mbp->dr & MBADR_RD;
	     break;
	case MBASMR_OF:					/* SMR */
	    *val = mbp->smr & MBASMR_RD;
	    break;
	case MBACMD_OF:					/* CMD */
	    *val = 0;
	     break;
	default:
	    return SCPE_NXM;
	    }
	break;

case MBART_EXT:						/* external */
	if (!mbregR[mb]) return SCPE_NXM;		/* device there? */
	drv = MBA_EXTDRV (pa);				/* get dev num */
	ofs = MBA_EXTOFS (pa);				/* get reg offs */
	r = mbregR[mb] (val, ofs, drv);			/* call device */
	if (r == MBE_NXD) mba_upd_sr (MBASR_NFD, 0, mb);/* nx drive? */
	else if (r == MBE_NXR) return SCPE_NXM;		/* nx reg? */
	break; 

case MBART_MAP:						/* map */
	ofs = MBA_INTOFS (pa);
	*val = mbp->map[ofs] & MBAMAP_RD;
	break;

default:
	return SCPE_NXM;  }

return SCPE_OK;
}

/* Write Massbus adapter register */

t_stat mba_wrreg (int32 val, int32 pa, int32 lnt)
{
int32 mb, ofs, drv, rtype;
t_stat r;
t_bool cs1dt;
MBACTX *mbp;

mb = NEXUS_GETNEX (pa) - TR_MBA0;			/* get MBA */
if (mb >= MBA_NUM) return SCPE_NXM;			/* valid? */
mbp = ctxmap[mb];					/* get context */
rtype = MBA_RTYPE (pa);					/* get reg type */

switch (rtype) {					/* case on type */

case MBART_INT:						/* internal */
	ofs = MBA_INTOFS (pa);				/* check range */
	if (ofs >= MBAMAX_OF) return SCPE_NXM;
	switch (ofs) {
	case MBACNF_OF:					/* CNF */
	    mbp->cnf = mbp->cnf & ~(val & MBACNF_W1C);
	    break;
	case MBACR_OF:					/* CR */
	    if (val & MBACR_INIT)			/* init? */
		mba_reset (devmap[mb]);			/* reset MBA */
	    if ((val & MBACR_ABORT) && (mbp->sr & MBASR_DTBUSY)) {
		if (mbabort[mb]) mbabort[mb] ();	/* abort? */
		mba_upd_sr (MBASR_DTABT, 0, mb);  }
	    if ((val & MBACR_MNT) && (mbp->sr & MBASR_DTBUSY)) {
		mba_upd_sr (MBASR_PGE, 0, mb);		/* mnt & xfer? */
		val = val & ~MBACR_MNT;  }
	    if ((val & MBACR_IE) == 0) mba_clr_int (mb);
	    mbp->cr = (mbp->cr & ~MBACR_WR) | (val & MBACR_WR);
	    break;
	case MBASR_OF:					/* SR */
	    mbp->sr = mbp->sr & ~(val & MBASR_W1C);
	    break;
	case MBAVA_OF:					/* VA */
	    if (mbp->sr & MBASR_DTBUSY)			/* err if xfr */
		mba_upd_sr (MBASR_PGE, 0, mb);
	    else mbp->va = val & MBAVA_WR;
	    break;
	case MBABC_OF:					/* BC */
	    if (mbp->sr & MBASR_DTBUSY)			/* err if xfr */
		mba_upd_sr (MBASR_PGE, 0, mb);
	    else {
		val = val & MBABC_WR;
		mbp->bc = (val << MBABC_V_CNT) | val;
		}
	     break;
	case MBADR_OF:					/* DR */
	    mbp->dr = (mbp->dr & ~MBADR_WR) | (val & MBADR_WR);
	    break;
	default:
	    return SCPE_NXM;
	    }
	break;

case MBART_EXT:						/* external */
	if (!mbregW[mb]) return SCPE_NXM;		/* device there? */
	drv = MBA_EXTDRV (pa);				/* get dev num */
	ofs = MBA_EXTOFS (pa);				/* get reg offs */
	cs1dt = (ofs == MBA_CS1) && (val & CSR_GO) &&	/* starting xfr? */
	   ((val & MBA_CS1_WR) >= MBA_CS1_DT);
	if (cs1dt && (mbp->sr & MBASR_DTBUSY)) {	/* xfr while busy? */
	    mba_upd_sr (MBASR_PGE, 0, mb);		/* prog error */
	    break;
	    }
	r = mbregW[mb] (val, ofs, drv);			/* write dev reg */
	if (r == MBE_NXD) mba_upd_sr (MBASR_NFD, 0, mb);/* nx drive? */
	else if (r == MBE_NXR) return SCPE_NXM;		/* nx reg? */
	if (cs1dt && (r == SCPE_OK))			/* did dt start? */		
	    mbp->sr = (mbp->sr | MBASR_DTBUSY) & ~MBASR_W1C;
	break; 

case MBART_MAP:						/* map */
	ofs = MBA_INTOFS (pa);
	mbp->map[ofs] = val & MBAMAP_WR;
	break;

default:
	return SCPE_NXM;  }

return SCPE_OK;
}

/* Massbus I/O routine

   mb_rdbufW 	-	fetch word buffer from memory
   mb_wrbufW 	-	store word buffer into memory
   mb_chbufW	-	compare word buffer with memory

   Returns number of bytes successfully transferred/checked
*/

int32 mba_rdbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa, dat;

if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = mbp->va;						/* get virt addr */
mbc = ((MBABC_WR + 1) - (mbp->bc >> MBABC_V_CNT)) & MBABC_WR; /* get Mbus bc */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (!mba_map_addr (ba + i, &pa, mb)) break;	/* page inv? */
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_upd_sr (MBASR_RTMO, 0, mb);
	    break;  }
	pbc = VA_PAGSIZE - VA_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	if ((pa | pbc) & 1) {				/* aligned word? */
	    for (j = 0; j < pbc; pa++, j++) {		/* no, bytes */
		if ((i + j) & 1) {			/* odd byte? */
		    *buf = (*buf & BMASK) | (ReadB (pa) << 8);
		    buf++;
		    }
		else *buf = (*buf & ~BMASK) | ReadB (pa);
		}
	    }
	else if ((pa | pbc) & 3) {			/* aligned LW? */
	    for (j = 0; j < pbc; pa = pa + 2, j = j + 2) {	/* no, words */
		*buf++ = ReadW (pa);			/* get word */
		}
	    }
	else {						/* yes, do by LW */
	    for (j = 0; j < pbc; pa = pa + 4, j = j + 4) {
		dat = ReadL (pa);			/* get lw */
		*buf++ = dat & WMASK;			/* low 16b */
		*buf++ = (dat >> 16) & WMASK;		/* high 16b */
		}
	    }
	}
return i;
}

int32 mba_wrbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa, dat;

if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = mbp->va;						/* get virt addr */
mbc = ((MBABC_WR + 1) - (mbp->bc >> MBABC_V_CNT)) & MBABC_WR; /* get Mbus bc */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (!mba_map_addr (ba + i, &pa, mb)) break;	/* page inv? */
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_upd_sr (MBASR_RTMO, 0, mb);
	    break;  }
	pbc = VA_PAGSIZE - VA_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	if ((pa | pbc) & 1) {				/* aligned word? */
	    for (j = 0; j < pbc; pa++, j++) {		/* no, bytes */
		if ((i + j) & 1) {
		    WriteB (pa, (*buf >> 8) & BMASK);
		    buf++;  }
		else WriteB (pa, *buf & BMASK);
		}
	    }
	else if ((pa | pbc) & 3) {			/* aligned LW? */
	    for (j = 0; j < pbc; pa = pa + 2, j = j + 2) {	/* no, words */
		WriteW (pa, *buf);			/* write word */
		buf++;
		}
	    }
	else {						/* yes, do by LW */
	    for (j = 0; j < pbc; pa = pa + 4, j = j + 4) {
		dat = (uint32) *buf++;			/* get low 16b */
		dat = dat | (((uint32) *buf++) << 16);	/* merge hi 16b */
		WriteL (pa, dat);			/* store LW */
		}
	    }
	}
return 0;
}

int32 mba_chbufW (uint32 mb, int32 bc, uint16 *buf)
{
MBACTX *mbp;
int32 i, j, ba, mbc, pbc;
uint32 pa, dat, cmp;

if (mb >= MBA_NUM) return 0;				/* valid MBA? */
mbp = ctxmap[mb];					/* get context */
ba = mbp->va;						/* get virt addr */
mbc = ((MBABC_WR + 1) - (mbp->bc >> MBABC_V_CNT)) & MBABC_WR; /* get Mbus bc */
if (bc > mbc) bc = mbc;					/* use smaller */
for (i = 0; i < bc; i = i + pbc) {			/* loop by pages */
	if (!mba_map_addr (ba + i, &pa, mb)) break;	/* page inv? */
	if (!ADDR_IS_MEM (pa)) {			/* NXM? */
	    mba_upd_sr (MBASR_RTMO, 0, mb);
	    break;  }
	pbc = VA_PAGSIZE - VA_GETOFF (pa);		/* left in page */
	if (pbc > (bc - i)) pbc = bc - i;		/* limit to rem xfr */
	for (j = 0; j < pbc; j++, pa++) {		/* byte by byte */
	    cmp = ReadB (pa);
	    if ((i + j) & 1) dat = (*buf++ >> 8) & BMASK;
	    else dat = *buf & BMASK;
	    if (cmp != dat) {
		mba_upd_sr ((j & 1)? MBASR_WCEU: MBASR_WCEL, 0, mb);
		break;
		}					/* end if */
	    }						/* end for j */
	}						/* end for i */
return i;
}

/* Map an address via the translation map */

t_bool mba_map_addr (uint32 va, uint32 *ma, uint32 mb)
{
MBACTX *mbp = ctxmap[mb];
uint32 vblk = (va >> VA_V_VPN);				/* map index */
uint32 mmap = mbp->map[vblk];				/* get map */

mbp->smr = mmap;					/* save map reg */
if (mmap & MBAMAP_VLD) {				/* valid? */
	*ma = ((mmap & MBAMAP_PAG) << VA_V_VPN) + VA_GETOFF (va);
	return 1;  }					/* legit addr */
mba_upd_sr (MBASR_INVM, 0, mb);				/* invalid map */
return 0;
}

/* Device access, status, and interrupt routines */

void mba_set_don (uint32 mb)
{
mba_upd_sr (MBASR_DTCMP, 0, mb);
return;
}

void mba_upd_ata (uint32 mb, uint32 val)
{
if (val) mba_upd_sr (MBASR_ATA, 0, mb);
else mba_upd_sr (0, MBASR_ATA, mb);
return;
}

void mba_set_exc (uint32 mb)
{
mba_upd_sr (MBASR_MBEXC, 0, mb);
return;
}

int32 mba_get_bc (uint32 mb)
{
MBACTX *mbp;

if (mb >= MBA_NUM) return 0;
mbp = ctxmap[mb];
return ((0x10000 - (mbp->bc >> MBABC_V_CNT)) & MBABC_WR);
}

void mba_set_int (uint32 mb)
{
DEVICE *dptr;
DIB *dibp;

if (mb >= MBA_NUM) return;
dptr = devmap[mb];
dibp = (DIB *) dptr->ctxt;
nexus_req[dibp->vloc >> 5] |= (1u << (dibp->vloc & 0x1F));
return;
}

void mba_clr_int (uint32 mb)
{
DEVICE *dptr;
DIB *dibp;

if (mb >= MBA_NUM) return;
dptr = devmap[mb];
dibp = (DIB *) dptr->ctxt;
nexus_req[dibp->vloc >> 5] &= ~(1u << (dibp->vloc & 0x1F));
return;
}

void mba_upd_sr (uint32 set, uint32 clr, uint32 mb)
{
MBACTX *mbp;

if (mb >= MBA_NUM) return;
mbp = ctxmap[mb];
if (set & MBASR_ABORTS) set |= (MBASR_DTCMP|MBASR_DTABT);
if (set & (MBASR_DTCMP|MBASR_DTABT)) mbp->sr &= ~MBASR_DTBUSY;
mbp->sr = (mbp->sr | set) & ~clr;
if ((set & MBASR_INTR) && (mbp->cr & MBACR_IE))
	mba_set_int (mb);
return;
}

/* Reset Massbus adapter */

t_stat mba_reset (DEVICE *dptr)
{
int32 i, mb;
MBACTX *mbp;

for (mb = 0; mb < MBA_NUM; mb++) {
	mbp = ctxmap[mb];
	if (dptr == devmap[mb]) break;
	}
mbp->cnf = 0;
mbp->cr = mbp->cr & MBACR_MNT;
mbp->sr = 0;
mbp->bc = 0;
mbp->va = 0;
mbp->dr = 0;
mbp->smr = 0;
if (sim_switches & SWMASK ('P')) {
	for (i = 0; i < MBA_NMAPR; i++) mbp->map[i] = 0;
	}
if (mbabort[mb]) mbabort[mb] ();			/* reset device */
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

