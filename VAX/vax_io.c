/* vax_io.c: VAX Qbus IO simulator

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

   qba		Qbus adapter
*/

#include "vax_defs.h"

/* CQBIC system configuration register */

#define CQSCR_POK	0x00008000			/* power ok RO1 */
#define CQSCR_BHL	0x00004000			/* BHALT enb */
#define CQSCR_AUX	0x00000400			/* aux mode RONI */
#define CQSCR_DBO	0x0000000C			/* offset NI */
#define CQSCR_RW	(CQSCR_BHL | CQSCR_DBO)
#define CQSCR_MASK	(CQSCR_RW | CQSCR_POK | CQSCR_AUX)

/* CQBIC DMA system error register - W1C */

#define CQDSER_BHL	0x00008000			/* BHALT NI */
#define CQDSER_DCN	0x00004000			/* DC ~OK NI */
#define CQDSER_MNX	0x00000080			/* master NXM */
#define CQDSER_MPE	0x00000020			/* master par NI */
#define CQDSER_SME	0x00000010			/* slv mem err NI */
#define CQDSER_LST	0x00000008			/* lost err */
#define CQDSER_TMO	0x00000004			/* no grant NI */
#define CQDSER_SNX	0x00000001			/* slave NXM */
#define CQDSER_ERR	(CQDSER_MNX | CQDSER_MPE | CQDSER_TMO | CQDSER_SNX)
#define CQDSER_MASK	0x0000C0BD

/* CQBIC master error address register */

#define CQMEAR_MASK	0x00001FFF			/* Qbus page */

/* CQBIC slave error address register */

#define CQSEAR_MASK	0x000FFFFF			/* mem page */

/* CQBIC map base register */

#define CQMBR_MASK	0x1FFF8000			/* 32KB aligned */

/* CQBIC IPC register */

#define CQIPC_QME	0x00008000			/* Qbus read NXM W1C */
#define CQIPC_INV	0x00004000			/* CAM inval NIWO */
#define CQIPC_AHLT	0x00000100			/* aux halt NI */
#define CQIPC_DBIE	0x00000040			/* dbell int enb NI */
#define CQIPC_LME	0x00000020			/* local mem enb */
#define CQIPC_DB	0x00000001			/* doorbell req NI */
#define CQIPC_W1C	CQIPC_QME
#define CQIPC_RW	(CQIPC_AHLT | CQIPC_DBIE | CQIPC_LME | CQIPC_DB)
#define CQIPC_MASK	(CQIPC_RW | CQIPC_QME )

/* CQBIC map entry */

#define CQMAP_VLD	0x80000000			/* valid */
#define CQMAP_PAG	0x000FFFFF			/* mem page */

int32 int_req[IPL_HLVL] = { 0 };			/* intr, IPL 14-17 */
int32 cq_scr = 0;					/* SCR */
int32 cq_dser = 0;					/* DSER */
int32 cq_mear = 0;					/* MEAR */
int32 cq_sear = 0;					/* SEAR */
int32 cq_mbr = 0;					/* MBR */
int32 cq_ipc = 0;					/* IPC */
static int_dummy = 0;				/* keep save/restore working */

extern uint32 *M;
extern UNIT cpu_unit;
extern int32 PSL, SISR, trpirq, mem_err;
extern int32 p1;
extern int32 ssc_bto;
extern jmp_buf save_env;

extern int32 ReadB (t_addr pa);
extern int32 ReadW (t_addr pa);
extern int32 ReadL (t_addr pa);
extern int32 WriteB (t_addr pa, int32 val);
extern int32 WriteW (t_addr pa, int32 val);
extern int32 WriteL (t_addr pa, int32 val);
extern DIB pt_dib;
extern DIB lpt_dib, dz_dib;
extern DIB rl_dib, rq_dib;
extern DIB ts_dib;
extern FILE *sim_log;

t_stat dbl_rd (int32 *data, int32 addr, int32 access);
t_stat dbl_wr (int32 data, int32 addr, int32 access);
int32 eval_int (void);
void cq_merr (int32 pa);
void cq_serr (int32 pa);
t_bool dev_conflict (uint32 nba, DIB *curr);
t_stat qba_reset (DEVICE *dptr);
extern int32 rq_inta (void);
extern int32 tmr0_inta (void);
extern int32 tmr1_inta (void);
extern dz_rxinta (void);
extern dz_txinta (void);

/* Qbus adapter data structures

   qba_dev	QBA device descriptor
   qba_unit	QBA units
   qba_reg	QBA register list
*/

DIB qba_dib = { 1, IOBA_DBL, IOLN_DBL, &dbl_rd, &dbl_wr };

UNIT qba_unit = { UDATA (NULL, 0, 0) };

REG qba_reg[] = {
	{ HRDATA (SCR, cq_scr, 16) },
	{ HRDATA (DSER, cq_dser, 8) },
	{ HRDATA (MEAR, cq_mear, 13) },
	{ HRDATA (SEAR, cq_sear, 20) },
	{ HRDATA (MBR, cq_mbr, 29) },
	{ HRDATA (IPC, cq_ipc, 16) },
	{ HRDATA (IPL18, int_dummy, 32), REG_HRO },
	{ HRDATA (IPL17, int_req[3], 32), REG_RO },
	{ HRDATA (IPL16, int_req[2], 32), REG_RO },
	{ HRDATA (IPL15, int_req[1], 32), REG_RO },
	{ HRDATA (IPL14, int_req[0], 32), REG_RO },
	{ NULL }  };

DEVICE qba_dev = {
	"QBA", &qba_unit, qba_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &qba_reset,
	NULL, NULL, NULL };

struct iolink {						/* I/O page linkage */
	int32	low;					/* low I/O addr */
	int32	high;					/* high I/O addr */
	int32	*enb;					/* enable flag */
	t_stat	(*read)();				/* read routine */
	t_stat	(*write)();  };				/* write routine */

/* IO page addresses */

DIB *dib_tab[] = {
	&dz_dib,
	&rq_dib,
	&rl_dib,
	&pt_dib,
	&lpt_dib,
	&ts_dib,
	&qba_dib,
	NULL };

/* Interrupt request to interrupt action map */

int32 (*int_ack[IPL_HLVL][32])() = {			/* int ack routines */
	{ NULL, NULL, NULL, NULL,			/* IPL 14 */
	  NULL, NULL, NULL, &tmr0_inta,
	  &tmr1_inta },
	{ &rq_inta, NULL, &dz_rxinta, &dz_txinta,	/* IPL 15 */
	  NULL, NULL },
	{ NULL },					/* IPL 16 */
	{ NULL }  };					/* IPL 17 */

/* Interrupt request to vector map */

int32 int_vec[IPL_HLVL][32] = {				/* int req to vector */
	{ SCB_TTI, SCB_TTO, VEC_PTR, VEC_PTP,		/* IPL 14 */
	  VEC_LPT, SCB_CSI, SCB_CSO, 0,
	  0 },
	{ VEC_RQ, VEC_RL, VEC_DZRX, VEC_DZTX,		/* IPL 15 */
	  VEC_RP, VEC_TS },
	{ SCB_INTTIM },					/* IPL 16 */
	{ 0 }  };					/* IPL 17 */

/* The KA65x handles errors in I/O space as follows

	- read: set DSER<7>, latch addr in MEAR, machine check
	- write: set DSER<7>, latch addr in MEAR, MEMERR interrupt
*/

int32 ReadQb (uint32 pa)
{
int32 i, val;
DIB *dibp;

for (i = 0; dibp = dib_tab[i]; i++ ) {
	if (dibp -> enb && (pa >= dibp -> ba) &&
	   (pa < (dibp -> ba + dibp -> lnt))) {
		dibp -> rd (&val, pa, READ);
		return val;  }  }
cq_merr (pa);
MACH_CHECK (MCHK_READ);
return 0;
}

void WriteQb (uint32 pa, int32 val, int32 mode)
{
int32 i;
DIB *dibp;

for (i = 0; dibp = dib_tab[i]; i++ ) {
	if (dibp -> enb && (pa >= dibp -> ba) &&
	   (pa < (dibp -> ba + dibp -> lnt))) {
		dibp -> wr (val, pa, mode);
		return;  }  }
cq_merr (pa);
mem_err = 1;
return;
}

/* ReadIO - read I/O space

   Inputs:
	pa	=	physical address
	lnt	=	length (BWLQ)
   Output:
	longword of data
*/

int32 ReadIO (int32 pa, int32 lnt)
{
int32 iod;

iod = ReadQb (pa);					/* wd from Qbus */
if (lnt < L_LONG) iod = iod << ((pa & 2)? 16: 0);	/* bw? position */
else iod = (ReadQb (pa + 2) << 16) | iod;		/* lw, get 2nd wd */
SET_IRQL;
return iod;
}

/* WriteIO - write I/O space

   Inputs:
	pa	=	physical address
	val	=	data to write, right justified in 32b longword
	lnt	=	length (BWLQ)
   Outputs:
	none
*/

void WriteIO (int32 pa, int32 val, int32 lnt)
{
if (lnt == L_BYTE) WriteQb (pa, val, WRITEB);
else if (lnt == L_WORD) WriteQb (pa, val, WRITE);
else {	WriteQb (pa, val & 0xFFFF, WRITE);
	WriteQb (pa + 2, (val >> 16) & 0xFFFF, WRITE);  }
SET_IRQL;
return;
}

/* Find highest priority outstanding interrupt */

int32 eval_int (void)
{
int32 ipl = PSL_GETIPL (PSL);
int32 i, t;

static const int32 sw_int_mask[IPL_SMAX] = {
	0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0,			/* 0 - 3 */
	0xFFE0, 0xFFC0, 0xFF80, 0xFF00,			/* 4 - 7 */
	0xFE00, 0xFC00, 0xF800, 0xF000,			/* 8 - B */
	0xE000, 0xC000, 0x8000 };			/* C - E */

if ((ipl < IPL_MEMERR) && mem_err) return IPL_MEMERR;	/* mem err int */
for (i = IPL_HMAX; i >= IPL_HMIN; i--) {		/* chk hwre int */
	if (i <= ipl) return 0;				/* at ipl? no int */
	if (int_req[i - IPL_HMIN]) return i;  }		/* req != 0? int */
if (ipl >= IPL_SMAX) return 0;				/* ipl >= sw max? */
if ((t = SISR & sw_int_mask[ipl]) == 0) return 0;	/* eligible req */
for (i = IPL_SMAX; i > ipl; i--) {			/* check swre int */
	if ((t >> i) & 1) return i;  }			/* req != 0? int */
return 0;
}

/* Return vector for highest priority hardware interrupt at IPL lvl*/

int32 get_vector (int32 lvl)
{
int32 i;
int32 l = lvl - IPL_HMIN;

for (i = 0; int_req[l] && (i < 32); i++) {
	if ((int_req[l] >> i) & 1) {
		int_req[l] = int_req[l] & ~(1u << i);
		if (int_ack[l][i]) return int_ack[l][i]();
		return int_vec[l][i];  }  }
return 0;
}

/* CQBIC registers

   SCR 		system configuration register
   DSER		DMA system error register (W1C)
   MEAR		master error address register (RO)
   SEAR		slave error address register (RO)
   MBR		map base register
   IPC		inter-processor communication register
*/

int32 cqbic_rd (int32 pa)
{
int32 rg = (pa - CQBICBASE) >> 2;

switch (rg) {
case 0:							/* SCR */
	return (cq_scr | CQSCR_POK) & CQSCR_MASK;
case 1:							/* DSER */
	return cq_dser & CQDSER_MASK;
case 2:							/* MEAR */
	return cq_mear & CQMEAR_MASK;
case 3:							/* SEAR */
	return cq_sear & CQSEAR_MASK;
case 4:							/* MBR */
	return cq_mbr & CQMBR_MASK;  }
return 0;
}

void cqbic_wr (int32 pa, int32 val, int32 lnt)
{
int32 nval, rg = (pa - CQBICBASE) >> 2;

if (lnt < L_LONG) {
	int32 sc = (pa & 3) << 3;
	int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
	int32 t = cqbic_rd (pa);
	nval = ((val & mask) << sc) | (t & ~(mask << sc));
	val = val << sc;  }
else nval = val;

switch (rg) {
case 0:							/* SCR */
	cq_scr = ((cq_scr & ~CQSCR_RW) | (nval & CQSCR_RW)) & CQSCR_MASK;
	break;
case 1:							/* DSER */
	cq_dser = (cq_dser & ~val) & CQDSER_MASK;
	if (val & CQDSER_SME) cq_ipc = cq_ipc & ~CQIPC_QME;
	break;
case 2: case 3:
	cq_merr (pa);					/* MEAR, SEAR */
	MACH_CHECK (MCHK_WRITE);
	break;
case 4:							/* MBR */
	cq_mbr = nval & CQMBR_MASK;
	break;  }
return;
}

/* IPC can be read as local register or as Qbus I/O
   Because of the W1C */

int32 cqipc_rd (int32 pa)
{
return cq_ipc & CQIPC_MASK;				/* IPC */
}

void cqipc_wr (int32 pa, int32 val, int32 lnt)
{
int32 nval = val;

if (lnt < L_LONG) {
	int32 sc = (pa & 3) << 3;
	nval = val << sc;  }

cq_ipc = cq_ipc & ~(nval & CQIPC_W1C);			/* W1C */
if ((pa & 3) == 0)					/* low byte only */
	cq_ipc = ((cq_ipc & ~CQIPC_RW) | (val & CQIPC_RW)) & CQIPC_MASK;
return;
}

/* I/O page routines */

t_stat dbl_rd (int32 *data, int32 addr, int32 access)
{
*data = cq_ipc & CQIPC_MASK;
return SCPE_OK;
}

t_stat dbl_wr (int32 data, int32 addr, int32 access)
{
cqipc_wr (addr, data, (access == WRITEB)? L_BYTE: L_WORD);
return SCPE_OK;
}

/* CQBIC map read and write (reflects to main memory)

   Read error: set DSER<0>, latch slave address, machine check
   Write error: set DSER<0>, latch slave address, memory error interrupt
*/

int32 cqmap_rd (int32 pa)
{
int32 ma = (pa & CQMAPAMASK) + cq_mbr;			/* mem addr */

if (ADDR_IS_MEM (ma)) return M[ma >> 2];
cq_serr (ma);						/* set err */
MACH_CHECK (MCHK_READ);					/* mcheck */
return 0;
}

void cqmap_wr (int32 pa, int32 val, int32 lnt)
{
int32 ma = (pa & CQMAPAMASK) + cq_mbr;			/* mem addr */

if (ADDR_IS_MEM (ma)) {
	if (lnt < L_LONG) {
		int32 sc = (pa & 3) << 3;
		int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
		int32 t = M[ma >> 2];
		val = ((val & mask) << sc) | (t & ~(mask << sc));  }
	M[ma >> 2] = val;  }
else {	cq_serr (ma);					/* error */
	mem_err = 1;  }
return;
}

/* CQBIC Qbus memory read and write (reflects to main memory)

   May give master or slave error, depending on where the failure occurs
*/

int32 cqmem_rd (int32 pa)
{
int32 qa = pa & CQMAMASK;				/* Qbus addr */
t_addr ma;

if (map_addr (qa, &ma)) return M[ma >> 2];		/* map addr */
MACH_CHECK (MCHK_READ);					/* err? mcheck */
return 0;
}

void cqmem_wr (int32 pa, int32 val, int32 lnt)
{
int32 qa = pa & CQMAMASK;				/* Qbus addr */
t_addr ma;

if (map_addr (qa, &ma)) {					/* map addr */
	if (lnt < L_LONG) {
		int32 sc = (pa & 3) << 3;
		int32 mask = (lnt == L_WORD)? 0xFFFF: 0xFF;
		int32 t = M[ma >> 2];
		val = ((val & mask) << sc) | (t & ~(mask << sc));  }
	M[ma >> 2] = val;  }
else mem_err = 1;
return;
}

/* Map an address via the translation map */

t_bool map_addr (t_addr qa, t_addr *ma)
{
int32 qblk = (qa >> VA_V_VPN);				/* Qbus blk */
int32 qmma = ((qblk << 2) & CQMAPAMASK) + cq_mbr;	/* map entry */

if (ADDR_IS_MEM (qmma)) {				/* legit? */
	int32 qmap = M[qmma >> 2];			/* get map */
	if (qmap & CQMAP_VLD) {				/* valid? */
		*ma = ((qmap & CQMAP_PAG) << VA_V_VPN) + VA_GETOFF (qa);
		if (ADDR_IS_MEM (*ma)) return 1;	/* legit addr */
		cq_serr (*ma);				/* slave nxm */
		return 0;  }
	cq_merr (qa);					/* master nxm */
	return 0;  }
cq_serr (0);						/* inv mem */
return 0;
}

/* Set master error */

void cq_merr (int32 pa)
{
if (cq_dser & CQDSER_ERR) cq_dser = cq_dser | CQDSER_LST;
cq_dser = cq_dser | CQDSER_MNX;				/* master nxm */
cq_mear = (pa >> VA_V_VPN) & CQMEAR_MASK;		/* page addr */
return;
}

/* Set slave error */

void cq_serr (int32 pa)
{
if (cq_dser & CQDSER_ERR) cq_dser = cq_dser | CQDSER_LST;
cq_dser = cq_dser | CQDSER_SNX;				/* slave nxm */
cq_sear = (pa >> VA_V_VPN) & CQSEAR_MASK;
return;
}

/* Reset I/O bus */

void ioreset_wr (int32 data)
{
reset_all (5);						/* from qba on... */
return;
}

/* Reset CQBIC */

t_stat qba_reset (DEVICE *dptr)
{
int32 i;

cq_scr = (cq_scr & CQSCR_BHL) | CQSCR_POK;
cq_dser = cq_mear = cq_sear = cq_ipc = 0;
for (i = 0; i < IPL_HLVL; i++) int_req[i] = 0;
return SCPE_OK;
}

/* Powerup CQBIC */

t_stat qba_powerup (void)
{
cq_mbr = 0;
cq_scr = CQSCR_POK;
return qba_reset (&qba_dev);
}

/* I/O buffer routines, aligned access

   map_ReadB	-	fetch byte buffer from memory
   map_ReadW 	-	fetch word buffer from memory
   map_ReadL 	-	fetch longword buffer from memory
   map_WriteB 	-	store byte buffer into memory
   map_WriteW 	-	store word buffer into memory
   map_WriteL 	-	store longword buffer into memory
*/

int32 map_readB (t_addr ba, int32 bc, uint8 *buf)
{
int32 i;
t_addr ma;

for (i = ma = 0; i < bc; i++, buf++) {			/* by bytes */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	*buf = ReadB (ma);
	ma = ma + 1;  }
return 0;
}

int32 map_readW (t_addr ba, int32 bc, uint16 *buf)
{
int32 i;
t_addr ma;

ba = ba & ~01;
bc = bc & ~01;
for (i = ma = 0; i < bc; i = i + 2, buf++) {		/* by words */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	*buf = ReadW (ma);
	ma = ma + 2;  }
return 0;
}

int32 map_readL (t_addr ba, int32 bc, uint32 *buf)
{
int32 i;
t_addr ma;

ba = ba & ~03;
bc = bc & ~03;
for (i = ma = 0; i < bc; i = i + 4, buf++) {		/* by lw */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	*buf = ReadL (ma);
	ma = ma + 4;  }
return 0;
}

int32 map_writeB (t_addr ba, int32 bc, uint8 *buf)
{
int32 i;
t_addr ma;

for (i = ma = 0; i < bc; i++, buf++) {			/* by bytes */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	WriteB (ma, *buf);
	ma = ma + 1;  }
return 0;
}

int32 map_writeW (t_addr ba, int32 bc, uint16 *buf)
{
int32 i;
t_addr ma;

ba = ba & ~01;
bc = bc & ~01;
for (i = ma = 0; i < bc; i = i + 2, buf++) {		/* by words */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	WriteW (ma, *buf);
	ma = ma + 2;  }
return 0;
}

int32 map_writeL (t_addr ba, int32 bc, uint32 *buf)
{
int32 i;
t_addr ma;

ba = ba & ~03;
bc = bc & ~03;
for (i = ma = 0; i < bc; i = i + 4, buf++) {		/* by lw */
	if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	WriteL (ma, *buf);
	ma = ma + 4;  }
return 0;
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
newba = (uint32) get_uint (cptr, 16, IOPAGEBASE+IOPAGEMASK, &r);	/* get new */
if ((r != SCPE_OK) || (newba == dibp -> ba)) return r;
if (newba <= IOPAGEBASE) return SCPE_ARG;		/* must be > 0 */
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
fprintf (st, "address=%08X", dibp -> ba);
if (dibp -> lnt > 1)
	fprintf (st, "-%08X", dibp -> ba + dibp -> lnt - 1);
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
	if (((nba >= dibp -> ba) &&
	    (nba < (dibp -> ba + dibp -> lnt))) ||
	    ((end >= dibp -> ba) &&
	    (end < (dibp -> ba + dibp -> lnt)))) {
		printf ("Device address conflict at %08X\n", dibp -> ba);
		if (sim_log) fprintf (sim_log,
			"Device number conflict at %08X\n", dibp -> ba);
		return TRUE;  }  }
return FALSE;
}
