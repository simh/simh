/* vax_sysreg.c: VAX system registers simulator

   Copyright (c) 1998-2002, Robert M Supnik

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

   This module contains the CVAX system-specific devices implemented in the
   CMCTL memory controller and the SSC system support chip.  (The architecturally
   specified devices are implemented in module vax_stddev.c.)

   rom		bootstrap ROM (no registers)
   nvr		non-volatile ROM (no registers)
   csi		console storage input
   cso		console storage output
   sysd		system devices (SSC miscellany)

   19-Aug-02	RMS	Removed unused variables (found by David Hittner)
			Allowed NVR to be attached to file
   30-May-02	RMS	Widened POS to 32b
   28-Feb-02	RMS	Fixed bug, missing end of table (found by Lars Brinkhoff)
*/

#include "vax_defs.h"

/* Console storage control/status */

#define CSICSR_IMP	(CSR_DONE + CSR_IE)		/* console input */
#define CSICSR_RW	(CSR_IE)
#define CSOCSR_IMP	(CSR_DONE + CSR_IE)		/* console output */
#define CSOCSR_RW	(CSR_IE)

/* CMCTL configuration registers */

#define CMCNF_VLD	0x80000000			/* addr valid */
#define CMCNF_BA	0x1FF00000			/* base addr */
#define CMCNF_LOCK	0x00000040			/* lock NI */
#define CMCNF_SRQ	0x00000020			/* sig req WO */
#define CMCNF_SIG	0x0000001F			/* signature */
#define CMCNF_RW	(CMCNF_VLD | CMCNF_BA)		/* read/write */
#define CMCNF_MASK	(CMCNF_RW | CMCNF_SIG)
#define MEM_BANK	(1 << 22)			/* bank size 4MB */
#define MEM_SIG		0x17;				/* ECC, 4 x 4MB */

/* CMCTL error register */

#define CMERR_RDS	0x80000000			/* uncorr err NI */
#define CMERR_FRQ	0x40000000			/* 2nd RDS NI */
#define CMERR_CRD	0x20000000			/* CRD err NI */
#define CMERR_PAG	0x1FFFFC00			/* page addr NI */
#define CMERR_DMA	0x00000100			/* DMA err NI */
#define CMERR_BUS	0x00000080			/* bus err NI */
#define CMERR_SYN	0x0000007F			/* syndrome NI */
#define CMERR_W1C	(CMERR_RDS | CMERR_FRQ | CMERR_CRD | \
			 CMERR_DMA | CMERR_BUS)

/* CMCTL control/status register */

#define CMCSR_PMI	0x00002000			/* PMI speed NI */
#define CMCSR_CRD	0x00001000			/* enb CRD int NI */
#define CMCSR_FRF	0x00000800			/* force ref WONI */
#define CMCSR_DET	0x00000400			/* dis err NI */
#define CMCSR_FDT	0x00000200			/* fast diag NI */
#define CMCSR_DCM	0x00000080			/* diag mode NI */
#define CMCSR_SYN	0x0000007F			/* syndrome NI */
#define CMCSR_MASK	(CMCSR_PMI | CMCSR_CRD | CMCSR_DET | \
			 CMCSR_FDT | CMCSR_DCM | CMCSR_SYN)

/* KA655 boot/diagnostic register */

#define BDR_BRKENB	0x00000080			/* break enable */

/* KA655 cache control register */

#define CACR_DRO	0x00FFFF00			/* diag bits RO */
#define CACR_V_DPAR	24				/* data parity */
#define CACR_FIXED	0x00000040			/* fixed bits */
#define CACR_CPE	0x00000020			/* parity err W1C */
#define CACR_CEN	0x00000010			/* enable */			
#define CACR_DPE	0x00000004			/* disable par NI */
#define CACR_WWP	0x00000002			/* write wrong par NI */
#define CACR_DIAG	0x00000001			/* diag mode */
#define CACR_W1C	(CACR_CPE)
#define CACR_RW		(CACR_CEN | CACR_DPE | CACR_WWP | CACR_DIAG)

/* SSC base register */

#define SSCBASE_MBO	0x20000000			/* must be one */
#define SSCBASE_RW	0x1FFFFC00			/* base address */

/* SSC configuration register */

#define SSCCNF_BLO	0x80000000			/* batt low W1C */
#define SSCCNF_IVD	0x08000000			/* int dsbl NI */
#define SSCCNF_IPL	0x03000000			/* int IPL NI */
#define SSCCNF_ROM	0x00F70000			/* ROM param NI */
#define SSCCNF_CTLP	0x00008000			/* ctrl P enb */
#define SSCCNF_BAUD	0x00007700			/* baud rates NI */
#define SSCCNF_ADS	0x00000077			/* addr strb NI */
#define SSCCNF_W1C	SSCCNF_BLO
#define SSCCNF_RW	0x0BF7F777

/* SSC timeout register */

#define SSCBTO_BTO	0x80000000			/* timeout W1C */
#define SSCBTO_RWT	0x40000000			/* read/write W1C */
#define SSCBTO_INTV	0x00FFFFFF			/* interval NI */
#define SSCBTO_W1C	(SSCBTO_BTO | SSCBTO_RWT)
#define SSCBTO_RW	SSCBTO_INTV

/* SSC output port */

#define SSCOTP_MASK	0x0000000F			/* output port */

/* SSC timer control/status */

#define TMR_CSR_ERR	0x80000000			/* error W1C */
#define TMR_CSR_DON	0x00000080			/* done W1C */
#define TMR_CSR_IE	0x00000040			/* int enb */
#define TMR_CSR_SGL	0x00000020			/* single WO */
#define TMR_CSR_XFR	0x00000010			/* xfer WO */
#define TMR_CSR_STP	0x00000004			/* stop */
#define TMR_CSR_RUN	0x00000001			/* run */
#define TMR_CSR_W1C	(TMR_CSR_ERR | TMR_CSR_DON)
#define TMR_CSR_RW	(TMR_CSR_IE | TMR_CSR_STP | TMR_CSR_RUN)

/* SSC timer intervals */

#define TMR_INC		10000				/* usec/interval */

/* SSC timer vector */

#define TMR_VEC_MASK	0x000003FC			/* vector */

/* SSC address strobes */

#define SSCADS_MASK	0x3FFFFFFC			/* match or mask */

extern int32 int_req[IPL_HLVL];
extern UNIT cpu_unit;
extern jmp_buf save_env;
extern int32 p1;
extern int32 sim_switches;
extern int32 MSER;
extern int32 tmr_poll;

uint32 *rom = NULL;					/* boot ROM */
uint32 *nvr = NULL;					/* non-volatile mem */
int32 csi_csr = 0;					/* control/status */
int32 cso_csr = 0;					/* control/status */
int32 cmctl_reg[CMCTLSIZE >> 2] = { 0 };		/* CMCTL reg */
int32 ka_cacr = 0;					/* KA655 cache ctl */
int32 ka_bdr = BDR_BRKENB;				/* KA655 boot diag */
int32 ssc_base = SSCBASE;				/* SSC base */
int32 ssc_cnf = 0;					/* SSC conf */
int32 ssc_bto = 0;					/* SSC timeout */
int32 ssc_otp = 0;					/* SSC output port */
int32 tmr_csr[2] = { 0 };				/* SSC timers */
uint32 tmr_tir[2] = { 0 };				/* curr interval */
uint32 tmr_tnir[2] = { 0 };				/* next interval */
int32 tmr_tivr[2] = { 0 };				/* vector */
uint32 tmr_inc[2] = { 0 };				/* tir increment */
uint32 tmr_sav[2] = { 0 };				/* saved inst cnt */
int32 ssc_adsm[2] = { 0 };				/* addr strobes */
int32 ssc_adsk[2] = { 0 };
int32 cdg_dat[CDASIZE >> 2];				/* cache data */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat rom_reset (DEVICE *dptr);
t_stat nvr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat nvr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat nvr_reset (DEVICE *dptr);
t_stat nvr_attach (UNIT *uptr, char *cptr);
t_stat nvr_detach (UNIT *uptr);
t_stat csi_reset (DEVICE *dptr);
t_stat cso_reset (DEVICE *dptr);
t_stat cso_svc (UNIT *uptr);
t_stat tmr_svc (UNIT *uptr);
t_stat sysd_reset (DEVICE *dptr);

int32 rom_rd (int32 pa);
int32 nvr_rd (int32 pa);
void nvr_wr (int32 pa, int32 val, int32 lnt);
int32 csrs_rd (void);
int32 csrd_rd (void);
int32 csts_rd (void);
void csrs_wr (int32 dat);
void csts_wr (int32 dat);
void cstd_wr (int32 dat);
int32 cmctl_rd (int32 pa);
void cmctl_wr (int32 pa, int32 val, int32 lnt);
int32 ka_rd (int32 pa);
void ka_wr (int32 pa, int32 val, int32 lnt);
int32 cdg_rd (int32 pa);
void cdg_wr (int32 pa, int32 val, int32 lnt);
int32 ssc_rd (int32 pa);
void ssc_wr (int32 pa, int32 val, int32 lnt);
int32 tmr_tir_rd (int32 tmr, t_bool interp);
void tmr_csr_wr (int32 tmr, int32 val);
void tmr_sched (int32 tmr);
void tmr_incr (int32 tmr, uint32 inc);
int32 tmr0_inta (void);
int32 tmr1_inta (void);
int32 parity (int32 val, int32 odd);

extern int32 cqmap_rd (int32 pa);
extern void cqmap_wr (int32 pa, int32 val, int32 lnt);
extern int32 cqipc_rd (int32 pa);
extern void cqipc_wr (int32 pa, int32 val, int32 lnt);
extern int32 cqbic_rd (int32 pa);
extern void cqbic_wr (int32 pa, int32 val, int32 lnt);
extern int32 cqmem_rd (int32 pa);
extern void cqmem_wr (int32 pa, int32 val, int32 lnt);
extern int32 iccs_rd (void);
extern int32 todr_rd (void);
extern int32 rxcs_rd (void);
extern int32 rxdb_rd (void);
extern int32 txcs_rd (void);
extern void iccs_wr (int32 dat);
extern void todr_wr (int32 dat);
extern void rxcs_wr (int32 dat);
extern void txcs_wr (int32 dat);
extern void txdb_wr (int32 dat);
extern void ioreset_wr (int32 dat);

/* ROM data structures

   rom_dev	ROM device descriptor
   rom_unit	ROM units
   rom_reg	ROM register list
*/

UNIT rom_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, ROMSIZE) };

REG rom_reg[] = {
	{ NULL }  };

DEVICE rom_dev = {
	"ROM", &rom_unit, rom_reg, NULL,
	1, 16, ROMAWIDTH, 4, 16, 32,
	&rom_ex, &rom_dep, &rom_reset,
	NULL, NULL, NULL,
	NULL, 0 };

/* NVR data structures

   nvr_dev	NVR device descriptor
   nvr_unit	NVR units
   nvr_reg	NVR register list
*/

UNIT nvr_unit =
	{ UDATA (NULL, UNIT_FIX+UNIT_BINK, NVRSIZE) };

REG nvr_reg[] = {
	{ NULL }  };

DEVICE nvr_dev = {
	"NVR", &nvr_unit, nvr_reg, NULL,
	1, 16, NVRAWIDTH, 4, 16, 32,
	&nvr_ex, &nvr_dep, &nvr_reset,
	NULL, &nvr_attach, &nvr_detach,
	NULL, 0 };

/* CSI data structures

   csi_dev	CSI device descriptor
   csi_unit	CSI unit descriptor
   csi_reg	CSI register list
*/

DIB csi_dib = { 0, 0, NULL, NULL, 1, IVCL (CSI), SCB_CSI, { NULL } };

UNIT csi_unit = { UDATA (NULL, 0, 0), KBD_POLL_WAIT };

REG csi_reg[] = {
	{ ORDATA (BUF, csi_unit.buf, 8) },
	{ ORDATA (CSR, csi_csr, 16) },
	{ FLDATA (INT, int_req[IPL_CSI], INT_V_CSI) },
	{ FLDATA (DONE, csi_csr, CSR_V_DONE) },
	{ FLDATA (IE, csi_csr, CSR_V_IE) },
	{ DRDATA (POS, csi_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, csi_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB csi_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,	NULL, &show_vec },
	{ 0 }  };

DEVICE csi_dev = {
	"CSI", &csi_unit, csi_reg, csi_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &csi_reset,
	NULL, NULL, NULL,
	&csi_dib, 0 };

/* CSO data structures

   cso_dev	CSO device descriptor
   cso_unit	CSO unit descriptor
   cso_reg	CSO register list
*/

DIB cso_dib = { 0, 0, NULL, NULL, 1, IVCL (CSO), SCB_CSO, { NULL } };

UNIT cso_unit = { UDATA (&cso_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT };

REG cso_reg[] = {
	{ ORDATA (BUF, cso_unit.buf, 8) },
	{ ORDATA (CSR, cso_csr, 16) },
	{ FLDATA (INT, int_req[IPL_CSO], INT_V_CSO) },
	{ FLDATA (DONE, cso_csr, CSR_V_DONE) },
	{ FLDATA (IE, cso_csr, CSR_V_IE) },
	{ DRDATA (POS, cso_unit.pos, 32), PV_LEFT },
	{ DRDATA (TIME, cso_unit.wait, 24), PV_LEFT },
	{ NULL }  };

MTAB cso_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,	NULL, &show_vec },
	{ 0 }  };

DEVICE cso_dev = {
	"CSO", &cso_unit, cso_reg, cso_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &cso_reset,
	NULL, NULL, NULL,
	&cso_dib, 0 };

/* SYSD data structures

   sysd_dev	SYSD device descriptor
   sysd_unit	SYSD units
   sysd_reg	SYSD register list
*/

DIB sysd_dib[] = { 0, 0, NULL, NULL,
		   2, IVCL (TMR0), 0, { &tmr0_inta, &tmr1_inta } };

UNIT sysd_unit[] = {
	{ UDATA (&tmr_svc, 0, 0) },
	{ UDATA (&tmr_svc, 0, 0) }  };

REG sysd_reg[] = {
	{ BRDATA (CMCSR, cmctl_reg, 16, 32, CMCTLSIZE >> 2) },
	{ HRDATA (CACR, ka_cacr, 8) },
	{ HRDATA (BDR, ka_bdr, 8) },
	{ HRDATA (BASE, ssc_base, 29) },
	{ HRDATA (CNF, ssc_cnf, 32) },
	{ HRDATA (BTO, ssc_bto, 32) },
	{ HRDATA (OTP, ssc_otp, 4) },
	{ HRDATA (TCSR0, tmr_csr[0], 32) },
	{ HRDATA (TIR0, tmr_tir[0], 32) },
	{ HRDATA (TNIR0, tmr_tnir[0], 32) },
	{ HRDATA (TIVEC0, tmr_tivr[0], 9) },
	{ HRDATA (TINC0, tmr_inc[0], 32) },
	{ HRDATA (TSAV0, tmr_sav[0], 32) },
	{ HRDATA (TCSR1, tmr_csr[1], 32) },
	{ HRDATA (TIR1, tmr_tir[1], 32) },
	{ HRDATA (TNIR1, tmr_tnir[1], 32) },
	{ HRDATA (TIVEC1, tmr_tivr[1], 9) },
	{ HRDATA (TINC1, tmr_inc[1], 32) },
	{ HRDATA (TSAV1, tmr_sav[1], 32) },
	{ HRDATA (ADSM0, ssc_adsm[0], 32) },
	{ HRDATA (ADSK0, ssc_adsk[0], 32) },
	{ HRDATA (ADSM1, ssc_adsm[1], 32) },
	{ HRDATA (ADSK1, ssc_adsk[1], 32) },
	{ BRDATA (CDGDAT, cdg_dat, 16, 32, CDASIZE >> 2) },
	{ NULL }  };

DEVICE sysd_dev = {
	"SYSD", sysd_unit, sysd_reg, NULL,
	2, 16, 16, 1, 16, 8,
	NULL, NULL, &sysd_reset,
	NULL, NULL, NULL,
	&sysd_dib, 0 };

/* ROM: read only memory - stored in a buffered file
   Register space access routines see ROM twice
*/

int32 rom_rd (int32 pa)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;

return rom[rg];
}

void rom_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;

if (lnt < L_LONG) {					/* byte or word? */
	int32 sc = (pa & 3) << 3;			/* merge */
	int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
	rom[rg] = ((val & mask) << sc) | (rom[rg] & ~(mask << sc));  }
else rom[rg] = val;
return;
}

/* ROM examine */

t_stat rom_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if ((vptr == NULL) || (addr & 03)) return SCPE_ARG;
if (addr >= ROMSIZE) return SCPE_NXM;
*vptr = rom[addr >> 2];
return SCPE_OK;
}

/* ROM deposit */

t_stat rom_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr & 03) return SCPE_ARG;
if (addr >= ROMSIZE) return SCPE_NXM;
rom[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* ROM reset */

t_stat rom_reset (DEVICE *dptr)
{
if (rom == NULL) rom = calloc (ROMSIZE >> 2, sizeof (int32));
if (rom == NULL) return SCPE_MEM;
return SCPE_OK;
}

/* NVR: non-volatile RAM - stored in a buffered file */

int32 nvr_rd (int32 pa)
{
int32 rg = (pa - NVRBASE) >> 2;

return nvr[rg];
}

void nvr_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - NVRBASE) >> 2;

if (lnt < L_LONG) {					/* byte or word? */
	int32 sc = (pa & 3) << 3;			/* merge */
	int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
	nvr[rg] = ((val & mask) << sc) | (nvr[rg] & ~(mask << sc));  }
else nvr[rg] = val;
return;
}

/* NVR examine */

t_stat nvr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if ((vptr == NULL) || (addr & 03)) return SCPE_ARG;
if (addr >= NVRSIZE) return SCPE_NXM;
*vptr = nvr[addr >> 2];
return SCPE_OK;
}

/* NVR deposit */

t_stat nvr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr & 03) return SCPE_ARG;
if (addr >= NVRSIZE) return SCPE_NXM;
nvr[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* NVR reset */

t_stat nvr_reset (DEVICE *dptr)
{
if (nvr == NULL) {
	nvr = calloc (NVRSIZE >> 2, sizeof (int32));
	nvr_unit.filebuf = nvr;
	ssc_cnf = ssc_cnf | SSCCNF_BLO;  }
if (nvr == NULL) return SCPE_MEM;
return SCPE_OK;
}

/* NVR attach */

t_stat nvr_attach (UNIT *uptr, char *cptr)
{
t_stat r;

uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
	uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
else {	uptr->hwmark = uptr->capac;
	ssc_cnf = ssc_cnf & ~SSCCNF_BLO;  }
return r;
}

/* NVR detach */

t_stat nvr_detach (UNIT *uptr)
{
t_stat r;

r = detach_unit (uptr);
if ((uptr->flags & UNIT_ATT) == 0)
	uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
return r;
}

/* CSI: console storage input */

int32 csrs_rd (void)
{
return (csi_csr & CSICSR_IMP);
}

int32 csrd_rd (void)
{
csi_csr = csi_csr & ~CSR_DONE;
CLR_INT (CSI);
return (csi_unit.buf & 0377);
}

void csrs_wr (int32 data)
{
if ((data & CSR_IE) == 0) CLR_INT (CSI);
else if ((csi_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
	SET_INT (CSI);
csi_csr = (csi_csr & ~CSICSR_RW) | (data & CSICSR_RW);
return;
}

t_stat csi_reset (DEVICE *dptr)
{
csi_unit.buf = 0;
csi_csr = 0;
CLR_INT (CSI);
return SCPE_OK;
}

/* CSO: console storage output */

int32 csts_rd (void)
{
return (cso_csr & CSOCSR_IMP);
}

void csts_wr (int32 data)
{
if ((data & CSR_IE) == 0) CLR_INT (CSO);
else if ((cso_csr & (CSR_DONE + CSR_IE)) == CSR_DONE)
	SET_INT (CSO);
cso_csr = (cso_csr & ~CSOCSR_RW) | (data & CSOCSR_RW);
return;
}

void cstd_wr (int32 data)
{
cso_unit.buf = data & 0377;
cso_csr = cso_csr & ~CSR_DONE;
CLR_INT (CSO);
sim_activate (&cso_unit, cso_unit.wait);
return;
}

t_stat cso_svc (UNIT *uptr)
{
cso_csr = cso_csr | CSR_DONE;
if (cso_csr & CSR_IE) SET_INT (CSO);
if ((cso_unit.flags & UNIT_ATT) == 0) return SCPE_OK;
if (putc (cso_unit.buf, cso_unit.fileref) == EOF) {
	perror ("CSO I/O error");
	clearerr (cso_unit.fileref);
	return SCPE_IOERR;  }
cso_unit.pos = cso_unit.pos + 1;
return SCPE_OK;
}

t_stat cso_reset (DEVICE *dptr)
{
cso_unit.buf = 0;
cso_csr = CSR_DONE;
CLR_INT (CSO);
sim_cancel (&cso_unit);					/* deactivate unit */
return SCPE_OK;
}

/* SYSD: SSC access mechanisms and devices

   - IPR space read/write routines
   - register space read/write routines
   - SSC local register read/write routines
   - SSC console storage UART
   - SSC timers
   - CMCTL local register read/write routines
*/

/* Read/write IPR register space

   These routines implement the SSC's response to IPR's which are
   sent off the CPU chip for processing.
*/

int32 ReadIPR (int32 rg)
{
int32 val;

switch (rg) {
case MT_ICCS:						/* ICCS */
	val = iccs_rd ();
	break;
case MT_CSRS:						/* CSRS */
	val = csrs_rd ();
	break;
case MT_CSRD:						/* CSRD */
	val = csrd_rd ();
	break;
case MT_CSTS:						/* CSTS */
	val = csts_rd ();
	break;
case MT_CSTD:						/* CSTD */
	val = 0;
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
case MT_TODR:
	val = todr_rd ();
	break;
default:
	ssc_bto = ssc_bto | SSCBTO_BTO;			/* set BTO */
	val = 0;
	break;  }
return val;
}

void WriteIPR (int32 rg, int32 val)
{
switch (rg) {
case MT_ICCS:						/* ICCS */
	iccs_wr (val);
	break;
case MT_TODR:						/* TODR */
	todr_wr (val);
	break;
case MT_CSRS:						/* CSRS */
	csrs_wr (val);
	break;
case MT_CSRD:						/* CSRD */
	break;
case MT_CSTS:						/* CSTS */
	csts_wr (val);
	break;
case MT_CSTD:						/* CSTD */
	cstd_wr (val);
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
case MT_IORESET:					/* IORESET */
	ioreset_wr (val);
	break;
default:
	ssc_bto = ssc_bto | SSCBTO_BTO;			/* set BTO */
	break;  }
return;
}

/* Read/write I/O register space

   These routines are the 'catch all' for address space map.  Any
   address that doesn't explicitly belong to memory, I/O, or ROM
   is given to these routines for processing.
*/

struct reglink {					/* register linkage */
	int32	low;					/* low addr */
	int32	high;					/* high addr */
	t_stat	(*read)();				/* read routine */
	void	(*write)();  };				/* write routine */

struct reglink regtable[] = {
	{ CQMAPBASE, CQMAPBASE+CQMAPSIZE, &cqmap_rd, &cqmap_wr },
	{ ROMBASE, ROMBASE+ROMSIZE+ROMSIZE, &rom_rd, NULL },
	{ NVRBASE, NVRBASE+NVRSIZE, &nvr_rd, &nvr_wr },
	{ CMCTLBASE, CMCTLBASE+CMCTLSIZE, &cmctl_rd, &cmctl_wr },
	{ SSCBASE, SSCBASE+SSCSIZE, &ssc_rd, &ssc_wr },
	{ KABASE, KABASE+KASIZE, &ka_rd, &ka_wr },
	{ CQBICBASE, CQBICBASE+CQBICSIZE, &cqbic_rd, &cqbic_wr },
	{ CQIPCBASE, CQIPCBASE+CQIPCSIZE, &cqipc_rd, &cqipc_wr },
	{ CQMBASE, CQMBASE+CQMSIZE, &cqmem_rd, &cqmem_wr },
	{ CDGBASE, CDGBASE+CDGSIZE, &cdg_rd, &cdg_wr },
	{ 0, 0, NULL, NULL }  };

/* ReadReg - read register space

   Inputs:
	pa	=	physical address
	lnt	=	length (BWLQ) - ignored
   Output:
	longword of data
*/

int32 ReadReg (int32 pa, int32 lnt)
{
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
	if ((pa >= p->low) && (pa < p->high) && p->read)
	    return p->read (pa);  }
ssc_bto = ssc_bto | SSCBTO_BTO | SSCBTO_RWT;
MACH_CHECK (MCHK_READ);
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
struct reglink *p;

for (p = &regtable[0]; p->low != 0; p++) {
	if ((pa >= p->low) && (pa < p->high) && p->write) {
	    p->write (pa, val, lnt);  
	    return;  }  }
ssc_bto = ssc_bto | SSCBTO_BTO | SSCBTO_RWT;
MACH_CHECK (MCHK_WRITE);
return;
}

/* CMCTL registers

   CMCTL00 - 15 configure memory banks 00 - 15.  Note that they are
   here merely to entertain the firmware; the actual configuration
   of memory is unaffected by the settings here.

   CMCTL16 - error status register

   CMCTL17 - control/diagnostic status register

   The CMCTL registers are cleared at power up.
*/

int32 cmctl_rd (int32 pa)
{
int32 rg = (pa - CMCTLBASE) >> 2;

switch (rg) {
default:						/* config reg */
	return cmctl_reg[rg] & CMCNF_MASK;
case 16:						/* err status */
	return cmctl_reg[rg];
case 17:						/* csr */
	return cmctl_reg[rg] & CMCSR_MASK;  }
return 0;
}

void cmctl_wr (int32 pa, int32 val, int32 lnt)
{
int32 i, rg = (pa - CMCTLBASE) >> 2;

if (lnt < L_LONG) {					/* LW write only */
	int32 sc = (pa & 3) << 3;			/* shift data to */
	val = val << sc;  }				/* proper location */
switch (rg) {
default:						/* config reg */
	if (val & CMCNF_SRQ) {				/* sig request? */
	    for (i = rg; i < (rg + 4); i++) {
		cmctl_reg[i] = cmctl_reg[i] & ~CMCNF_SIG;
		if (ADDR_IS_MEM (i * MEM_BANK))
		    cmctl_reg[i] = cmctl_reg[i] | MEM_SIG;  }  }
	cmctl_reg[rg] = (cmctl_reg[rg] & ~CMCNF_RW) | (val & CMCNF_RW);
	break;
case 16:						/* err status */
	cmctl_reg[rg] = cmctl_reg[rg] & ~(val & CMERR_W1C);
	break;
case 17:						/* csr */
	cmctl_reg[rg] = val & CMCSR_MASK;
	break;  }
return;
}

/* KA655 registers */

int32 ka_rd (int32 pa)
{
int32 rg = (pa - KABASE) >> 2;

switch (rg)
{
case 0:							/* CACR */
	return ka_cacr;
case 1:							/* BDR */
	return ka_bdr;  }
return 0;
}

void ka_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - KABASE) >> 2;

if ((rg == 0) && ((pa & 3) == 0)) {			/* lo byte only */
	ka_cacr = (ka_cacr & ~(val & CACR_W1C)) | CACR_FIXED;
	ka_cacr = (ka_cacr & ~CACR_RW) | (val & CACR_RW);  }
return;
}

int32 sysd_hlt_enb (void)
{
return ka_bdr & BDR_BRKENB;
}

/* Cache diagnostic space - byte/word merges done in WriteReg */

int32 cdg_rd (int32 pa)
{
int32 t, row = CDG_GETROW (pa);

t = cdg_dat[row];
ka_cacr = ka_cacr & ~CACR_DRO;				/* clear diag */
ka_cacr = ka_cacr |
	(parity ((t >> 24) & 0xFF, 1) << (CACR_V_DPAR + 3)) |
	(parity ((t >> 16) & 0xFF, 0) << (CACR_V_DPAR + 2)) |
	(parity ((t >> 8) & 0xFF, 1) << (CACR_V_DPAR + 1)) |
	(parity (t & 0xFF, 0) << CACR_V_DPAR);
return t;
}

void cdg_wr (int32 pa, int32 val, int32 lnt)
{
int32 row = CDG_GETROW (pa);

if (lnt < L_LONG) {					/* byte or word? */
	int32 sc = (pa & 3) << 3;			/* merge */
	int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
	int32 t = cdg_dat[row];
	val = ((val & mask) << sc) | (t & ~(mask << sc));  }
cdg_dat[row] = val;					/* store data */
return;
}

int32 parity (int32 val, int32 odd)
{
for ( ; val != 0; val = val >> 1) {
	if (val & 1) odd = odd ^ 1;  }
return odd;
}

/* SSC registers - byte/word merges done in WriteReg */

int32 ssc_rd (int32 pa)
{
int32 rg = (pa - SSCBASE) >> 2;

switch (rg) {
case 0x00:						/* base reg */
	return ssc_base;
case 0x04:						/* conf reg */
	return ssc_cnf;
case 0x08:						/* bus timeout */
	return ssc_bto;
case 0x0C:						/* output port */
	return ssc_otp & SSCOTP_MASK;
case 0x1B:						/* TODR */
	return todr_rd ();
case 0x1C:						/* CSRS */
	return csrs_rd ();
case 0x1D:						/* CSRD */
	return csrd_rd ();
case 0x1E:						/* CSTS */
	return csts_rd ();
case 0x20:						/* RXCS */
	return rxcs_rd ();
case 0x21:						/* RXDB */
	return rxdb_rd ();
case 0x22:						/* TXCS */
	return txcs_rd ();
case 0x40:						/* T0CSR */
	return tmr_csr[0];
case 0x41:						/* T0INT */
	return tmr_tir_rd (0, FALSE);
case 0x42:						/* T0NI */
	return tmr_tnir[0];
case 0x43:						/* T0VEC */
	return tmr_tivr[0];
case 0x44:						/* T1CSR */
	return tmr_csr[1];
case 0x45:						/* T1INT */
	return tmr_tir_rd (1, FALSE);
case 0x46:						/* T1NI */
	return tmr_tnir[1];
case 0x47:						/* T1VEC */
	return tmr_tivr[1];
case 0x4C:						/* ADS0M */
	return ssc_adsm[0];
case 0x4D:						/* ADS0K */
	return ssc_adsk[0];
case 0x50:						/* ADS1M */
	return ssc_adsm[1];
case 0x51:						/* ADS1K */
	return ssc_adsk[1];  }
return 0;
}

void ssc_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - SSCBASE) >> 2;

if (lnt < L_LONG) {					/* byte or word? */
	int32 sc = (pa & 3) << 3;			/* merge */
	int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
	int32 t = ssc_rd (pa);
	val = ((val & mask) << sc) | (t & ~(mask << sc));  }

switch (rg) {
case 0x00:						/* base reg */
	ssc_base = (val & SSCBASE_RW) | SSCBASE_MBO;
	break;
case 0x04:						/* conf reg */
	ssc_cnf = ssc_cnf & ~(val & SSCCNF_W1C);
	ssc_cnf = (ssc_cnf & ~SSCCNF_RW) | (val & SSCCNF_RW);
	break;
case 0x08:						/* bus timeout */
	ssc_bto = ssc_bto & ~(val & SSCBTO_W1C);
	ssc_bto = (ssc_bto & ~SSCBTO_RW) | (val & SSCBTO_RW);
	break;
case 0x0C:						/* output port */
	ssc_otp = val & SSCOTP_MASK;
	break;
case 0x1B:						/* TODR */
	todr_wr (val);
	break;
case 0x1C:						/* CSRS */
	csrs_wr (val);
	break;
case 0x1E:						/* CSTS */
	csts_wr (val);
	break;
case 0x1F:						/* CSTD */
	cstd_wr (val);
	break;
case 0x20:						/* RXCS */
	rxcs_wr (val);
	break;
case 0x22:						/* TXCS */
	txcs_wr (val);
	break;
case 0x23:						/* TXDB */
	txdb_wr (val);
	break;
case 0x40:						/* T0CSR */
	tmr_csr_wr (0, val);
	break;
case 0x42:						/* T0NI */
	tmr_tnir[0] = val;
	break;
case 0x43:						/* T0VEC */
	tmr_tivr[0] = val & TMR_VEC_MASK;
	break;
case 0x44:						/* T1CSR */
	tmr_csr_wr (1, val);
	break;
case 0x46:						/* T1NI */
	tmr_tnir[1] = val;
	break;
case 0x47:						/* T1VEC */
	tmr_tivr[1] = val & TMR_VEC_MASK;
	break;
case 0x4C:						/* ADS0M */
	ssc_adsm[0] = val & SSCADS_MASK;
	break;
case 0x4D:						/* ADS0K */
	ssc_adsk[0] = val & SSCADS_MASK;
	break;
case 0x50:						/* ADS1M */
	ssc_adsm[1] = val & SSCADS_MASK;
	break;
case 0x51:						/* ADS1K */
	ssc_adsk[1] = val & SSCADS_MASK;
	break;  }
return;
}

/* Programmable timers

   The SSC timers, which increment at 1Mhz, cannot be accurately
   simulated due to the overhead that would be required for 1M
   clock events per second.  Instead, a gross hack is used.  When
   a timer is started, the clock interval is inspected.

   if (int < 0 and small) then testing timer, count instructions
   if (int >= 0 or large) then counting a real interval, schedule
	clock events at 100Hz using calibrated line clock delay

   If the interval register is read, then its value between events
   is interpolated using the current instruction count versus the
   count when the most recent event started.
*/

int32 tmr_tir_rd (int32 tmr, t_bool interp)
{
uint32 delta;

if (interp || (tmr_csr[tmr] & TMR_CSR_RUN)) {		/* interp, running? */
	delta = sim_grtime () - tmr_sav[tmr];		/* delta inst */
	if (delta >= tmr_inc[tmr]) delta = tmr_inc[tmr] - 1;
	return tmr_tir[tmr] + delta;  }
return tmr_tir[tmr];
}

void tmr_csr_wr (int32 tmr, int32 val)
{
if ((tmr < 0) || (tmr > 1)) return;
if ((val & TMR_CSR_RUN) == 0) {				/* clearing run? */
	sim_cancel (&sysd_unit[tmr]);			/* cancel timer */
	if (tmr_csr[tmr] & TMR_CSR_RUN)			/* run 1 -> 0? */
	    tmr_tir[tmr] = tmr_tir_rd (tmr, TRUE);  }	/* update itr */
tmr_csr[tmr] = tmr_csr[tmr] & ~(val & TMR_CSR_W1C);	/* W1C csr */
tmr_csr[tmr] = (tmr_csr[tmr] & ~TMR_CSR_RW) |		/* new r/w */
	(val & TMR_CSR_RW);
if (val & TMR_CSR_XFR) tmr_tir[tmr] = tmr_tnir[tmr];	/* xfr set? */
if (val & TMR_CSR_RUN)	{				/* run? */
	if (val & TMR_CSR_XFR)				/* new tir? */
	    sim_cancel (&sysd_unit[tmr]);		/* stop prev */
	if (!sim_is_active (&sysd_unit[tmr]))		/* not running? */
	    tmr_sched (tmr);  }				/* activate */
else if (val & TMR_CSR_SGL) {				/* single step? */
	tmr_incr (tmr, 1);				/* incr tmr */
	if (tmr_tir[tmr] == 0)				/* if ovflo, */
	    tmr_tir[tmr] = tmr_tnir[tmr];  }		/* reload tir */
if ((tmr_csr[tmr] & (TMR_CSR_DON | TMR_CSR_IE)) !=	/* update int */
    (TMR_CSR_DON | TMR_CSR_IE)) {
	if (tmr) CLR_INT (TMR1);
	else CLR_INT (TMR0);  }
return;
}

/* Unit service */

t_stat tmr_svc (UNIT *uptr)
{
int32 tmr = uptr - sysd_dev.units;			/* get timer # */

tmr_incr (tmr, tmr_inc[tmr]);				/* incr timer */
return SCPE_OK;
}

/* Timer increment */

void tmr_incr (int32 tmr, uint32 inc)
{
uint32 new_tir = tmr_tir[tmr] + inc;			/* add incr */

if (new_tir < tmr_tir[tmr]) {				/* ovflo? */
	tmr_tir[tmr] = 0;				/* now 0 */
	if (tmr_csr[tmr] & TMR_CSR_DON)			/* done? set err */
	    tmr_csr[tmr] = tmr_csr[tmr] | TMR_CSR_ERR;
	else tmr_csr[tmr] = tmr_csr[tmr] | TMR_CSR_DON;	/* set done */
	if (tmr_csr[tmr] & TMR_CSR_STP)			/* stop? */
	    tmr_csr[tmr] = tmr_csr[tmr] & ~TMR_CSR_RUN;	/* clr run */
	if (tmr_csr[tmr] & TMR_CSR_RUN) {		/* run? */
	    tmr_tir[tmr] = tmr_tnir[tmr];		/* reload */
	    tmr_sched (tmr);  }				/* reactivate */
	if (tmr_csr[tmr] & TMR_CSR_IE) {		/* set int req */
	    if (tmr) SET_INT (TMR1);
	    else SET_INT (TMR0); }  }
else {	tmr_tir[tmr] = new_tir;				/* no, upd tir */
	if (tmr_csr[tmr] & TMR_CSR_RUN)			/* still running? */
	    tmr_sched (tmr);  }				/* reactivate */
return;
}

/* Timer scheduling */

void tmr_sched (int32 tmr)
{
tmr_sav[tmr] = sim_grtime ();				/* save intvl base */
if (tmr_tir[tmr] > (0xFFFFFFFFu - TMR_INC)) {		/* short interval? */
	tmr_inc[tmr] = (~tmr_tir[tmr] + 1);		/* inc = interval */
	sim_activate (&sysd_unit[tmr], tmr_inc[tmr]);  }
else {	tmr_inc[tmr] = TMR_INC;				/* usec/interval */
	sim_activate (&sysd_unit[tmr], tmr_poll);  }	/* use calib clock */
return;
}

int32 tmr0_inta (void)
{
return tmr_tivr[0];
}

int32 tmr1_inta (void)
{
return tmr_tivr[1];
}

/* SYSD reset */

t_stat sysd_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < 2; i++) {
	tmr_csr[i] = tmr_tnir[i] = tmr_tir[i] = 0;
	tmr_inc[i] = tmr_sav[i] = 0;
	sim_cancel (&sysd_unit[i]);  }
csi_csr = 0;
csi_unit.buf = 0;
sim_cancel (&csi_unit);
CLR_INT (CSI);
cso_csr = CSR_DONE;
cso_unit.buf = 0;
sim_cancel (&cso_unit);
CLR_INT (CSO);
return SCPE_OK;
}

/* SYSD powerup */

t_stat sysd_powerup (void)
{
int32 i;

for (i = 0; i < (CMCTLSIZE >> 2); i++) cmctl_reg[i] = 0;
for (i = 0; i < 2; i++) {
	tmr_tivr[i] = 0;
	ssc_adsm[i] = ssc_adsk[i] = 0;  }
ka_cacr = 0;
ssc_base = SSCBASE;
ssc_cnf = ssc_cnf & SSCCNF_BLO;
ssc_bto = 0;
ssc_otp = 0;
return sysd_reset (&sysd_dev);
}

