/* vax780_sbimem.c: VAX 11/780 SBI and memory controller

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

   This module contains the VAX 11/780 system-specific registers and devices.

   mctl0, mctl1		MS780C/E memory controllers
   sbi			bus controller
*/

#include "vax_defs.h"

/* 11/780 specific IPRs */

/* Writeable control store */

#define WCSA_RW		0xFFFF				/* writeable */
#define WCSA_ADDR	0x1FFF				/* addr */
#define WCSA_CTR	0x6000				/* counter */
#define WCSA_CTR_INC	0x2000				/* increment */
#define WCSA_CTR_MAX	0x6000				/* max value */
#define WCSD_RD_VAL	0xFF				/* fixed read val */
#define WCSD_WR		0xFFFFFFFF			/* write */
#define MBRK_RW		0x1FFF				/* microbreak */

/* System registers */

#define SBIFS_RD	(0x031F0000|SBI_FAULTS)		/* SBI faults */
#define SBIFS_WR	0x03140000
#define SBIFS_W1C	0x00080000

#define SBISC_RD	0xFFFF0000			/* SBI silo comp */
#define SBISC_WR	0x7FFF0000
#define SBISC_LOCK	0x80000000			/* lock */
#define SBISC_CNT	0x000F0000			/* count */

#define SBIMT_RD	0xFFFFFF00			/* SBI maint */
#define SBIMT_WR	0xFFFFF900

#define SBIER_CRDIE	0x00008000			/* SBI error, CRD IE */
#define SBIER_CRD	0x00004000			/* CRD */
#define SBIER_RDS	0x00002000			/* RDS */
#define SBIER_TMO	0x00001000			/* timeout */
#define SBIER_STA	0x00000C00			/* timeout status (0) */
#define SBIER_CNF	0x00000100			/* error confirm */
#define SBIER_IBRDS	0x00000080
#define SBIER_IBTMO	0x00000040
#define SBIER_IBSTA	0x00000030
#define SBIER_IBCNF	0x00000008
#define SBIER_MULT	0x00000004			/* multiple errors */
#define SBIER_FREE	0x00000002			/* SBI free */
#define SBIER_RD	0x0000FDFE
#define SBIER_WR	0x00008000
#define SBIER_W1C	0x000070C0
#define SBIER_TMOW1C	(SBIER_TMO|SBIER_STA|SBIER_CNF|SBIER_MULT)
#define SBIER_IBTW1C	(SBIER_IBTMO|SBIER_STA|SBIER_IBCNF)

#define SBITMO_V_MODE	30				/* mode */
#define SBITMO_VIRT	0x20000000			/* physical */

/* Memory controller register A */

#define MCRA_OF		0x0
#define MCRA_SUMM	0x00100000			/* err summ (MS780E) */
#define MCRA_C_SIZE	0x00007C00			/* array size - fixed */
#define MCRA_V_SIZE	9
#define MCRA_ILVE	0x00000100			/* interleave wr enab */
#define MCRA_TYPE	0x000000F8			/* type */
#define MCRA_C_TYPE	0x00000010			/* 16k uninterleaved */
#define MCRA_E_TYPE	0x0000006A			/* 256k upper + lower */
#define MCRA_ILV	0x00000007			/* interleave */
#define MCRA_RD		(0x00107FFF|SBI_FAULTS)
#define MCRA_WR		0x00000100

/* Memory controller register B */

#define MCRB_OF		0x1
#define MCRB_FP		0xF0000000			/* file pointers */
#define MCRB_V_SA	15				/* start addr */
#define MCRB_M_SA	0x1FFF
#define MCRB_SA		(MCRB_M_SA << MCRB_V_SA)
#define MCRB_SAE	0x00004000			/* start addr wr enab */
#define MCRB_INIT	0x00003000			/* init state */
#define MCRB_REF	0x00000400			/* refresh */
#define MCRB_ECC	0x000003FF			/* ECC for diags */
#define MCRB_RD		0xFFFFF7FF
#define MCRB_WR		0x000043FF

/* Memory controller register C,D */

#define MCRC_OF		0x2
#define MCRD_OF		0x3
#define MCRC_DCRD	0x40000000			/* disable CRD */
#define MCRC_HER	0x20000000			/* high error rate */
#define MCRC_ERL	0x10000000			/* log error */
#define MCRC_C_ER	0x0FFFFFFF			/* MS780C error */
#define MCRC_E_PE1	0x00080000			/* MS780E par ctl 1 */
#define MCRC_E_PE0	0x00040000			/* MS780E par ctl 0 */
#define MCRC_E_CRD	0x00000200			/* MS780E CRD */
#define MCRC_E_PEW	0x00000100			/* MS780E par err wr */
#define MCRC_E_USEQ	0x00000080			/* MS780E seq err */
#define MCRC_C_RD	0x7FFFFFFF
#define MCRC_E_RD	0x700C0380
#define MCRC_WR		0x40000000
#define MCRC_C_W1C	0x30000000
#define MCRC_E_W1C	0x300C0380

#define MCRMAX_OF	0x4

#define MCRROM_OF	0x400

/* VAX-11/780 boot device definitions */

struct boot_dev {
	char		*name;
	int32		code;
	int32		let;
	};

uint32 wcs_addr = 0;
uint32 wcs_data = 0;
uint32 wcs_mbrk = 0;
uint32 nexus_req[NEXUS_HLVL];				/* nexus int req */
uint32 sbi_fs = 0;					/* SBI fault status */
uint32 sbi_sc = 0;					/* SBI silo comparator */
uint32 sbi_mt = 0;					/* SBI maintenance */
uint32 sbi_er = 0;					/* SBI error status */
uint32 sbi_tmo = 0;					/* SBI timeout addr */
uint32 mcr_a[MCTL_NUM];
uint32 mcr_b[MCTL_NUM];
uint32 mcr_c[MCTL_NUM];
uint32 mcr_d[MCTL_NUM];
uint32 rom_lw[MCTL_NUM][ROMSIZE >> 2];

static t_stat (*nexusR[NEXUS_NUM])(int32 *dat, int32 ad, int32 md);
static t_stat (*nexusW[NEXUS_NUM])(int32 dat, int32 ad, int32 md);

static struct boot_dev boot_tab[] = {
	{ "RP", BOOT_MB, 0 },
	{ "HK", BOOT_HK, 0 },
	{ "RL", BOOT_RL, 0 },
	{ "RQ", BOOT_UDA, 'A' << 24 },
	{ "TQ", BOOT_TK, 'A' << 24 },
	{ NULL }  };

extern int32 R[16];
extern int32 PSL;
extern int32 ASTLVL, SISR;
extern int32 mapen, pme, trpirq;
extern int32 in_ie;
extern int32 mchk_va, mchk_ref;
extern int32 cpu_extmem;
extern int32 crd_err, mem_err, hlt_pin;
extern int32 tmr_int, tti_int, tto_int;
extern jmp_buf save_env;
extern int32 p1;
extern int32 sim_switches;
extern UNIT cpu_unit;
extern DEVICE *sim_devices[];
extern FILE *sim_log;
extern CTAB *sim_vm_cmd;

t_stat sbi_reset (DEVICE *dptr);
t_stat mctl_reset (DEVICE *dptr);
t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode);
t_stat mctl_wrreg (int32 val, int32 pa, int32 mode);
void sbi_set_tmo (int32 pa);
t_stat vax780_boot (int32 flag, char *ptr);

void uba_eval_int (void);
extern void Write (uint32 va, int32 val, int32 lnt, int32 acc);
extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 iccs_rd (void);
extern int32 nicr_rd (void);
extern int32 icr_rd (void);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern void iccs_wr (int32 dat);
extern void nicr_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void init_mbus_tab (void);
extern void init_ubus_tab (void);
extern t_stat build_mbus_tab (DEVICE *dptr, DIB *dibp);
extern t_stat build_ubus_tab (DEVICE *dptr, DIB *dibp);

/* SBI data structures

   sbi_dev	SBI device descriptor
   sbi_unit	SBI unit
   sbi_reg	SBI register list
*/

UNIT sbi_unit = { UDATA (NULL, 0, 0) };

REG sbi_reg[] = {
	{ HRDATA (NREQ14, nexus_req[0], 16) },
	{ HRDATA (NREQ15, nexus_req[1], 16) },
	{ HRDATA (NREQ16, nexus_req[2], 16) },
	{ HRDATA (NREQ17, nexus_req[3], 16) },
	{ HRDATA (WCSA, wcs_addr, 16) },
	{ HRDATA (WCSD, wcs_data, 32) },
	{ HRDATA (MBRK, wcs_mbrk, 13) },
	{ HRDATA (SBIFS, sbi_fs, 32) },
	{ HRDATA (SBISC, sbi_sc, 32) },
	{ HRDATA (SBIMT, sbi_mt, 32) },
	{ HRDATA (SBIER, sbi_er, 32) },
	{ HRDATA (SBITMO, sbi_tmo, 32) },
	{ NULL }  };

DEVICE sbi_dev = {
	"SBI", &sbi_unit, sbi_reg, NULL,
	1, 16, 16, 1, 16, 8,
	NULL, NULL, &sbi_reset,
	NULL, NULL, NULL,
	NULL, 0 };

/* MCTLx data structures

   mctlx_dev	MCTLx device descriptor
   mctlx_unit	MCTLx unit
   mctlx_reg	MCTLx register list
*/

DIB mctl0_dib[] = { TR_MCTL0, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl0_unit = { UDATA (NULL, 0, 0) };

REG mctl0_reg[] = {
	{ HRDATA (CRA, mcr_a[0], 32) },
	{ HRDATA (CRB, mcr_b[0], 32) },
	{ HRDATA (CRC, mcr_c[0], 32) },
	{ HRDATA (CRD, mcr_d[0], 32) },
	{ BRDATA (ROM, rom_lw[0], 16, 32, ROMSIZE >> 2) },
	{ NULL }  };

MTAB mctl0_mod[] = {
	{ MTAB_XTD|MTAB_VDV, TR_MCTL0, "NEXUS", NULL,
	  NULL, &show_nexus },
	{ 0 }  };

DEVICE mctl0_dev = {
	"MCTL0", &mctl0_unit, mctl0_reg, mctl0_mod,
	1, 16, 16, 1, 16, 8,
	NULL, NULL, &mctl_reset,
	NULL, NULL, NULL,
	&mctl0_dib, DEV_NEXUS };

DIB mctl1_dib[] = { TR_MCTL1, 0, &mctl_rdreg, &mctl_wrreg, 0 };

UNIT mctl1_unit = { UDATA (NULL, 0, 0) };

MTAB mctl1_mod[] = {
	{ MTAB_XTD|MTAB_VDV, TR_MCTL1, "NEXUS", NULL,
	  NULL, &show_nexus },
	{ 0 }  };

REG mctl1_reg[] = {
	{ HRDATA (CRA, mcr_a[1], 32) },
	{ HRDATA (CRB, mcr_b[1], 32) },
	{ HRDATA (CRC, mcr_c[1], 32) },
	{ HRDATA (CRD, mcr_d[1], 32) },
	{ BRDATA (ROM, rom_lw[1], 16, 32, ROMSIZE >> 2) },
	{ NULL }  };

DEVICE mctl1_dev = {
	"MCTL1", &mctl1_unit, mctl1_reg, mctl1_mod,
	1, 16, 16, 1, 16, 8,
	NULL, NULL, &mctl_reset,
	NULL, NULL, NULL,
	&mctl1_dib, DEV_NEXUS };

DIB mctl_dib[] = { TR_MCTL0, 2, &mctl_rdreg, &mctl_wrreg, 0 };

/* Special boot command, overrides regular boot */

CTAB vax780_cmd[] = {
	{ "BOOT", &vax780_boot, RU_BOOT,
	  "bo{ot} <device>{/R5:flg} boot device\n" },
	{ NULL }  };

/* The VAX 11/780 has three sources of interrupts

   - internal device interrupts (CPU, console, clock)
   - nexus interupts (e.g., memory controller, MBA, UBA)
   - external device interrupts (Unibus)

   Internal devices vector to fixed SCB locations.

   Nexus interrupts vector to an SCB location based on this
   formula: SCB_NEXUS + ((IPL - 0x14) * 0x40) + (TR# * 0x4)

   External device interrupts do not vector directly.
   Instead, the interrupt handler for a given UBA IPL
   reads a vector register that contains the Unibus vector
   for that IPL.

/* Find highest priority vectorable interrupt */

int32 eval_int (void)
{
int32 ipl = PSL_GETIPL (PSL);
int32 i, t;

static const int32 sw_int_mask[IPL_SMAX] = {
	0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,			/* 0 - 3 */
	0xFFE0, 0xFFC0, 0xFF80, 0xFF00,			/* 4 - 7 */
	0xFE00, 0xFC00, 0xF800, 0xF000,			/* 8 - B */
	0xE000, 0xC000, 0x8000 };			/* C - E */

if (hlt_pin) return IPL_HLTPIN;				/* hlt pin int */
if ((ipl < IPL_MEMERR) && mem_err) return IPL_MEMERR;	/* mem err int */
if ((ipl < IPL_CRDERR) && crd_err) return IPL_CRDERR;	/* crd err int */
if ((ipl < IPL_CLKINT) && tmr_int) return IPL_CLKINT;	/* clock int */
uba_eval_int ();					/* update UBA */
for (i = IPL_HMAX; i >= IPL_HMIN; i--) {		/* chk hwre int */
	if (i <= ipl) return 0;				/* at ipl? no int */
	if (nexus_req[i - IPL_HMIN]) return i;  }	/* req != 0? int */
if ((ipl < IPL_TTINT) && (tti_int || tto_int))		/* console int */
	return IPL_TTINT;
if (ipl >= IPL_SMAX) return 0;				/* ipl >= sw max? */
if ((t = SISR & sw_int_mask[ipl]) == 0) return 0;	/* eligible req */
for (i = IPL_SMAX; i > ipl; i--) {			/* check swre int */
	if ((t >> i) & 1) return i;  }			/* req != 0? int */
return 0;
}

/* Return vector for highest priority hardware interrupt at IPL lvl */

int32 get_vector (int32 lvl)
{
int32 i;
int32 l = lvl - IPL_HMIN;

if (lvl == IPL_MEMERR) {				/* mem error? */
	mem_err = 0;
	return SCB_MEMERR;  }
if (lvl == IPL_CRDERR) {				/* CRD error? */
	crd_err = 0;
	return SCB_CRDERR;  }
if ((lvl == IPL_CLKINT) && tmr_int) {			/* clock? */
	tmr_int = 0;					/* clear req */
	return SCB_INTTIM;  }				/* return vector */
if (lvl > IPL_HMAX) {					/* error req lvl? */
	ABORT (STOP_UIPL);  }				/* unknown intr */
if ((lvl <= IPL_HMAX) && (lvl >= IPL_HMIN)) {		/* nexus? */
	for (i = 0; nexus_req[l] && (i < NEXUS_NUM); i++) {
	    if ((nexus_req[l] >> i) & 1) {
		nexus_req[l] = nexus_req[l] & ~(1u << i);
		return SCB_NEXUS + (l << 6) + (i << 2);	/* return vector */
		}
	    }
	}
if (lvl == IPL_TTINT) {					/* console? */
	if (tti_int) {					/* input? */
	    tti_int = 0;				/* clear req */
	    return SCB_TTI;  }				/* return vector */
	if (tto_int) {					/* output? */
	    tto_int = 0;				/* clear req */
	    return SCB_TTO;  }				/* return vector */
	}
return 0;
}

/* Read 780-specific IPR's */

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {
case MT_ICCS:						/* ICCS */
	val = iccs_rd ();
	break;
case MT_NICR:						/* NICR */
	val = nicr_rd ();
	break;
case MT_ICR:						/* ICR */
	val = icr_rd ();
	break;
case MT_TODR:						/* TODR */
	val = todr_rd ();
	break;
case MT_ACCS:						/* ACCS (not impl) */
	val = 0;
	break;
case MT_WCSA:						/* WCSA */
	val = wcs_addr & WCSA_RW;
	break;
case MT_WCSD:						/* WCSD */
	val = WCSD_RD_VAL;
	break;
case MT_RXCS:						/* RXCS */
	val = rxcs_rd ();
	break;
case MT_RXDB:						/* RXDB */
	val = rxdb_rd ();
	break;
case MT_TXCS:						/* TXCS */
	val = txcs_rd ();
	break;
case MT_TXDB:						/* TXDB */
	val = 0;
	break;
case MT_SBIFS:						/* SBIFS */
	val = sbi_fs & SBIFS_RD;
	break;
case MT_SBIS:						/* SBIS */
	val = 0;
	break;
case MT_SBISC:						/* SBISC */
	val = sbi_sc & SBISC_RD;
	break;
case MT_SBIMT:						/* SBIMT */
	val = sbi_mt & SBIMT_RD;
	break;
case MT_SBIER:						/* SBIER */
	val = sbi_er & SBIER_RD;
	break;
case MT_SBITA:						/* SBITA */
	val = sbi_tmo;
	break;
case MT_MBRK:						/* MBRK */
	val = wcs_mbrk & MBRK_RW;
	break;
case MT_SID:						/* SID */
	val = VAX780_SID | VAX780_ECO | VAX780_PLANT | VAX780_SN;
	break;
default:
	RSVD_OPND_FAULT;
	}
return val;
}

/* Write 780-specific IPR's */

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {
case MT_ICCS:						/* ICCS */
	iccs_wr (val);
	break;
case MT_NICR:						/* NICR */
	nicr_wr (val);
	break;
case MT_TODR:						/* TODR */
	todr_wr (val);
	break;
case MT_WCSA:						/* WCSA */
	wcs_addr = val & WCSA_RW;
	break;
case MT_WCSD:						/* WCSD */
	wcs_data = val & WCSD_WR;
	wcs_addr = (wcs_addr & ~WCSA_CTR) |
	    ((wcs_addr + WCSA_CTR_INC) & WCSA_CTR);
	if ((wcs_addr & WCSA_CTR) == WCSA_CTR_MAX)
	    wcs_addr = (wcs_addr & ~WCSA_ADDR) |
	    ((wcs_addr + 1) & WCSA_ADDR);
	break;
case MT_RXCS:						/* RXCS */
	rxcs_wr (val);
	break;
case MT_RXDB:						/* RXDB */
	break;
case MT_TXCS:						/* TXCS */
	txcs_wr (val);
	break;
case MT_TXDB:						/* TXDB */
	txdb_wr (val);
	break;
case MT_SBIFS:						/* SBIFS */
	sbi_fs = (sbi_fs & ~SBIFS_WR) | (val & SBIFS_WR);
	sbi_fs = sbi_fs & ~(val & SBIFS_W1C);
	break;
case MT_SBISC:						/* SBISC */
	sbi_sc = (sbi_sc & ~SBISC_WR) | (val & SBISC_WR);
	if ((val & SBISC_CNT) != SBISC_CNT) sbi_sc = sbi_sc & ~SBISC_LOCK;
	break;
case MT_SBIMT:						/* SBIMT */
	sbi_mt = (sbi_mt & ~SBIMT_WR) | (val & SBIMT_WR);
	break;
case MT_SBIER:						/* SBIER */
	sbi_er = (sbi_er & ~SBIER_WR) | (val & SBIER_WR);
	sbi_er = sbi_er & ~(val & SBIER_W1C);
	if (val & SBIER_TMO) sbi_er = sbi_er & ~SBIER_TMOW1C;
	if (val & SBIER_IBTMO) sbi_er = sbi_er & ~SBIER_IBTW1C;
	if ((sbi_er & SBIER_CRDIE) && (sbi_er & SBIER_CRD))
	    crd_err = 1;
	else crd_err = 0;
	break;
case MT_SBIQC:
//	tbd						/* SBIQC */
	break;
case MT_MBRK:						/* MBRK */
	wcs_mbrk = val & MBRK_RW;
	break;
default:
	RSVD_OPND_FAULT;
	}
return;
}

/* ReadReg - read register space

   Inputs:
	pa	=	physical address
	lnt	=	length (BWLQ) - ignored
   Output:
	longword of data
*/

int32 ReadReg (int32 pa, int32 lnt)
{
int32 nexus, val;

if (ADDR_IS_REG (pa)) {					/* reg space? */
	nexus = NEXUS_GETNEX (pa);			/* get nexus */
	if (nexusR[nexus] &&				/* valid? */
	    (nexusR[nexus] (&val, pa, lnt) == SCPE_OK)) {
	    SET_IRQL;
	    return val;
	    }
	}
sbi_set_tmo (pa);					/* timeout */
MACH_CHECK (MCHK_RD_F);					/* machine check */
return 0;
} 

/* WriteReg - write register space

   Inputs:
	pa	=	physical address
	val	=	data to write, right justified in 32b longword
	lnt	=	length (BWLQ)
   Outputs:
	none
*/

void WriteReg (int32 pa, int32 val, int32 lnt)
{
int32 nexus;

if (ADDR_IS_REG (pa)) {					/* reg space? */
	nexus = NEXUS_GETNEX (pa);			/* get nexus */
	if (nexusW[nexus] &&				/* valid? */
	    (nexusW[nexus] (val, pa, lnt) == SCPE_OK)) {
	    SET_IRQL;
	    return;
	    }
	}
sbi_set_tmo (pa);					/* timeout */
mem_err = 1;						/* interrupt */
eval_int ();
return;
}

/* Set SBI timeout */

void sbi_set_tmo (int32 pa)
{
if ((sbi_er & SBIER_TMO) == 0) {			/* not yet set? */
	sbi_tmo = pa >> 2;				/* save addr */
	if (mchk_ref == REF_V) sbi_tmo |= SBITMO_VIRT |	/* virt? add mode */
	    (PSL_GETCUR (PSL) << SBITMO_V_MODE);
	sbi_er |= SBIER_TMO;  }				/* set tmo flag */
else sbi_er |= SBIER_MULT;				/* yes, multiple */
return;
}

/* Memory controller register read */

t_stat mctl_rdreg (int32 *val, int32 pa, int32 mode)
{
int32 mctl, ofs;

mctl = NEXUS_GETNEX (pa) - TR_MCTL0;			/* get mctl num */
ofs = NEXUS_GETOFS (pa);				/* get offset */
if (ofs >= MCRROM_OF) {					/* ROM? */
	*val = rom_lw[mctl][ofs - MCRROM_OF];		/* get lw */
	return SCPE_OK;
	}	
if (ofs >= MCRMAX_OF) return SCPE_NXM;			/* in range? */
switch (ofs) {

case MCRA_OF:						/* CR A */
	*val = mcr_a[mctl] & MCRA_RD;
	break;
case MCRB_OF:						/* CR B */
	*val = (mcr_b[mctl] & MCRB_RD) | MCRB_INIT;
	break;
case MCRC_OF:						/* CR C */
	*val = mcr_c[mctl] & (cpu_extmem? MCRC_E_RD: MCRC_C_RD);
	break;
case MCRD_OF:						/* CR D */
	if (!cpu_extmem) return SCPE_NXM;		/* MS780E only */
	*val = mcr_d[mctl] & MCRC_E_RD;
	break;
	}
return SCPE_OK;
}

/* Memory controller register write */

t_stat mctl_wrreg (int32 val, int32 pa, int32 mode)
{
int32 mctl, ofs, mask;

mctl = NEXUS_GETNEX (pa) - TR_MCTL0;			/* get mctl num */
ofs = NEXUS_GETOFS (pa);				/* get offset */
if (ofs >= MCRMAX_OF) return SCPE_NXM;			/* in range? */
switch (ofs) {

case MCRA_OF:						/* CR A */
	mask = MCRA_WR | ((val & MCRA_ILVE)? MCRA_ILV: 0);
	mcr_a[mctl] = (mcr_a[mctl] & ~mask) | (val & mask);
	break;
case MCRB_OF:						/* CR B */
	mask = MCRB_WR | ((val & MCRB_SAE)? MCRB_SA: 0);
	mcr_b[mctl] = (mcr_b[mctl] & ~mask) | (val & mask);
	break;
case MCRC_OF:						/* CR C */
	mcr_c[mctl] = ((mcr_c[mctl] & MCRC_WR) | (val & MCRC_WR)) &
	    ~(val & (cpu_extmem? MCRC_E_W1C: MCRC_C_W1C));
	break;
case MCRD_OF:						/* CR D */
	if (!cpu_extmem) return SCPE_NXM;		/* MS780E only */
	mcr_d[mctl] = ((mcr_d[mctl] & MCRC_WR) | (val & MCRC_WR)) &
	    ~(val & MCRC_E_W1C);
	break;
	}
return SCPE_OK;
}

/* Used by CPU and loader */

void rom_wr_B (int32 pa, int32 val)
{
uint32 mctl = NEXUS_GETNEX (pa) - TR_MCTL0;		/* get mctl num */
uint32 ofs = NEXUS_GETOFS (pa) - MCRROM_OF;		/* get offset */
int32 sc = (pa & 3) << 3;

rom_lw[mctl][ofs] = ((val & 0xFF) << sc) | (rom_lw[mctl][ofs] & ~(0xFF << sc));
return;
}

/* Machine check

   Error status word format
   <2:0> =	ASTLVL
   <3> =	PME
   <6:4> =	arith trap code
   Rest will be zero
*/

int32 machine_check (int32 p1, int32 opc, int32 cc)
{
int32 acc, err;

err = (GET_TRAP (trpirq) << 4) | (pme << 3) | ASTLVL;	/* error word */
cc = intexc (SCB_MCHK, cc, 0, IE_SVE);			/* take exception */
acc = ACC_MASK (KERN);					/* in kernel mode */
in_ie = 1;
SP = SP - 44;						/* push 11 words */
Write (SP, 40, L_LONG, WA);				/* # bytes */
Write (SP + 4, p1, L_LONG, WA);				/* mcheck type */
Write (SP + 8, err, L_LONG, WA);			/* CPU error status */
Write (SP + 12, 0, L_LONG, WA);				/* uPC */
Write (SP + 16, mchk_va, L_LONG, WA);			/* VA */
Write (SP + 20, 0, L_LONG, WA);				/* D register */
Write (SP + 24, mapen, L_LONG, WA);			/* TB status 1 */
Write (SP + 28, 0, L_LONG, WA);				/* TB status 2 */
Write (SP + 32, sbi_tmo, L_LONG, WA);			/* SBI timeout addr */
Write (SP + 36, 0, L_LONG, WA);				/* cache status */
Write (SP + 40, sbi_er, L_LONG, WA);			/* SBI error */
in_ie = 0;
return cc;
}

/* Console entry */

int32 con_halt (int32 code, int32 cc)
{
ABORT (STOP_HALT);
return cc;
}

/* Special boot command - linked into SCP by initial reset

   Syntax: BOOT <device>{/R5:val}

   Sets up R0-R5, calls SCP boot processor with effective BOOT CPU
*/

t_stat vax780_boot (int32 flag, char *ptr)
{
char gbuf[CBUFSIZE];
char *slptr, *regptr;
int32 i, r5v, unitno;
DEVICE *dptr;
UNIT *uptr;
DIB *dibp;
t_stat r;

regptr = get_glyph (ptr, gbuf, 0);			/* get glyph */
if (slptr = strchr (gbuf, '/')) {			/* found slash? */
	regptr = strchr (ptr, '/');			/* locate orig */
	*slptr = 0;  }					/* zero in string */
dptr = find_unit (gbuf, &uptr);				/* find device */
if ((dptr == NULL) || (uptr == NULL)) return SCPE_ARG;
dibp = (DIB *) dptr->ctxt;				/* get DIB */
if (dibp == NULL) return SCPE_ARG;
unitno = uptr - dptr->units;
r5v = 0;
if ((strncmp (regptr, "/R5:", 4) == 0) ||
    (strncmp (regptr, "/R5=", 4) == 0) ||
    (strncmp (regptr, "/r5:", 4) == 0) ||
    (strncmp (regptr, "/r5=", 4) == 0)) {
	r5v = (int32) get_uint (regptr + 4, 16, LMASK, &r);
	if (r != SCPE_OK) return r;  }
else if (*regptr != 0) return SCPE_ARG;
for (i = 0; boot_tab[i].name != NULL; i++) {
	if (strcmp (dptr->name, boot_tab[i].name) == 0) {
	    R[0] = boot_tab[i].code;
	    if (dptr->flags & DEV_MBUS) {
		R[1] = dibp->ba + TR_MBA0;
		R[2] = unitno;
		}
	    else {
		R[1] = TR_UBA;
		R[2] = boot_tab[i].let | (dibp->ba & UBADDRMASK);
		}
	    R[3] = unitno;
	    R[4] = 0;
	    R[5] = r5v;
	    return run_cmd (flag, "CPU");
	    }
	}
return SCPE_NOFNC;
}

/* Bootstrap - finish up bootstrap process */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
t_stat r;

printf ("Loading boot code from vmb780.bin\n");
if (sim_log) fprintf (sim_log, 
	"Loading boot code from vmb780.bin\n");
r = load_cmd (0, "-O vmb780.bin 200");
if (r != SCPE_OK) return r;
SP = PC = 512;
return SCPE_OK;
}

/* SBI reset */

t_stat sbi_reset (DEVICE *dptr)
{
wcs_addr = 0;
wcs_data = 0;
wcs_mbrk = 0;
sbi_fs = 0;
sbi_sc = 0;
sbi_mt = 0;
sbi_er = 0;
sbi_tmo = 0;
sim_vm_cmd = vax780_cmd;
return SCPE_OK;
}

/* MEMCTL reset */

t_stat mctl_reset (DEVICE *dptr)
{
int32 i, amb;

amb = (MEMSIZE / 2) >> 20;				/* array size MB */
for (i = 0; i < MCTL_NUM; i++) {			/* init for MS780C */
	if (cpu_extmem) {				/* extended memory? */
	    mcr_a[i] = (amb << MCRA_V_SIZE) | MCRA_E_TYPE;
	    mcr_b[i] = MCRB_INIT | ((i * amb) << MCRB_V_SA);
	    }
	else {
	    mcr_a[i] = MCRA_C_SIZE | MCRA_C_TYPE;
	    mcr_b[i] = MCRB_INIT | (i << 21);
	    }
	mcr_c[i] = 0;
	mcr_d[i] = 0;
	}
return SCPE_OK;
}

/* Show nexus */

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "nexus=%d", val);
return SCPE_OK;
}

/* Init nexus tables */

void init_nexus_tab (void)
{
uint32 i;

for (i = 0; i < NEXUS_NUM; i++) {
	nexusR[i] = NULL;
	nexusW[i] = NULL;
	}
return;
}

/* Build nexus tables

   Inputs:
	dptr	=	pointer to device
	dibp	=	pointer to DIB
   Outputs:
	status
*/


t_stat build_nexus_tab (DEVICE *dptr, DIB *dibp)
{
uint32 idx;

if ((dptr == NULL) || (dibp == NULL)) return SCPE_IERR;
idx = dibp->ba;
if (idx >= NEXUS_NUM) return SCPE_IERR;
if ((nexusR[idx] && dibp->rd &&				/* conflict? */
    (nexusR[idx] != dibp->rd)) ||
    (nexusW[idx] && dibp->wr &&
    (nexusW[idx] != dibp->wr))) {
	printf ("Nexus %s conflict at %d\n",
	    sim_dname (dptr), dibp->ba);
	if (sim_log) fprintf (sim_log,
	    "Nexus %s conflict at %d\n",
	    sim_dname (dptr), dibp->ba);
	return SCPE_STOP;
	}
if (dibp->rd) nexusR[idx] = dibp->rd;			/* set rd dispatch */
if (dibp->wr) nexusW[idx] = dibp->wr;			/* set wr dispatch */
return SCPE_OK;
}

/* Build dib_tab from device list */

t_stat build_dib_tab (void)
{
uint32 i;
DEVICE *dptr;
DIB *dibp;
t_stat r;

init_nexus_tab ();
init_ubus_tab ();
init_mbus_tab ();
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {	/* loop thru dev */
	dibp = (DIB *) dptr->ctxt;			/* get DIB */
	if (dibp && !(dptr->flags & DEV_DIS)) {		/* defined, enabled? */
	    if (dptr->flags & DEV_NEXUS) {		/* Nexus? */
		if (r = build_nexus_tab (dptr, dibp))	/* add to dispatch table */
		    return r;
		}
	    else if (dptr->flags & DEV_MBUS) {		/* Massbus? */
		if (r = build_mbus_tab (dptr, dibp))
		    return r;
		}
	    else {					/* no, Unibus device */
		if (r = build_ubus_tab (dptr, dibp))	/* add to dispatch tab */
		    return r;
		}					/* end else */
	    }						/* end if enabled */
	}						/* end for */
return SCPE_OK;
}
