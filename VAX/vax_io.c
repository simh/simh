/* vax_io.c: VAX Qbus IO simulator

   Copyright (c) 1998-2004, Robert M Supnik

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

   21-Mar-04	RMS	Added RXV21 support
   21-Dec-03	RMS	Fixed bug in autoconfigure vector assignment; added controls
   21-Nov-03	RMS	Added check for interrupt slot conflict (found by Dave Hittner)
   29-Oct-03	RMS	Fixed WriteX declaration (found by Mark Pizzolato)
   19-Apr-03	RMS	Added optimized byte and word DMA routines
   12-Mar-03	RMS	Added logical name support
   22-Dec-02	RMS	Added console halt support
   12-Oct-02	RMS	Added autoconfigure support
			Added SHOW IO space routine
   29-Sep-02	RMS	Added dynamic table support
   07-Sep-02	RMS	Added TMSCP and variable vector support
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

extern uint32 *M;
extern UNIT cpu_unit;
extern int32 PSL, SISR, trpirq, mem_err, hlt_pin;
extern int32 p1;
extern int32 ssc_bto;
extern int32 autcon_enb;
extern jmp_buf save_env;
extern DEVICE *sim_devices[];

extern int32 ReadB (uint32 pa);
extern int32 ReadW (uint32 pa);
extern int32 ReadL (uint32 pa);
extern void WriteB (uint32 pa, int32 val);
extern void WriteW (uint32 pa, int32 val);
extern void WriteL (uint32 pa, int32 val);
extern FILE *sim_log;

t_stat dbl_rd (int32 *data, int32 addr, int32 access);
t_stat dbl_wr (int32 data, int32 addr, int32 access);
int32 eval_int (void);
void cq_merr (int32 pa);
void cq_serr (int32 pa);
t_stat qba_reset (DEVICE *dptr);

/* Qbus adapter data structures

   qba_dev	QBA device descriptor
   qba_unit	QBA units
   qba_reg	QBA register list
*/

DIB qba_dib = { IOBA_DBL, IOLN_DBL, &dbl_rd, &dbl_wr, 0 };

UNIT qba_unit = { UDATA (NULL, 0, 0) };

REG qba_reg[] = {
	{ HRDATA (SCR, cq_scr, 16) },
	{ HRDATA (DSER, cq_dser, 8) },
	{ HRDATA (MEAR, cq_mear, 13) },
	{ HRDATA (SEAR, cq_sear, 20) },
	{ HRDATA (MBR, cq_mbr, 29) },
	{ HRDATA (IPC, cq_ipc, 16) },
	{ HRDATA (IPL17, int_req[3], 32), REG_RO },
	{ HRDATA (IPL16, int_req[2], 32), REG_RO },
	{ HRDATA (IPL15, int_req[1], 32), REG_RO },
	{ HRDATA (IPL14, int_req[0], 32), REG_RO },
	{ NULL }  };

DEVICE qba_dev = {
	"QBA", &qba_unit, qba_reg, NULL,
	1, 0, 0, 0, 0, 0,
	NULL, NULL, &qba_reset,
	NULL, NULL, NULL,
	&qba_dib, DEV_QBUS };

/* IO page addresses */

DIB *dib_tab[DIB_MAX];					/* DIB table */

/* Interrupt request to interrupt action map */

int32 (*int_ack[IPL_HLVL][32])();			/* int ack routines */

/* Interrupt request to vector map */

int32 int_vec[IPL_HLVL][32];				/* int req to vector */

/* The KA65x handles errors in I/O space as follows

	- read: set DSER<7>, latch addr in MEAR, machine check
	- write: set DSER<7>, latch addr in MEAR, MEMERR interrupt
*/

int32 ReadQb (uint32 pa)
{
int32 i, val;
DIB *dibp;

for (i = 0; dibp = dib_tab[i]; i++ ) {
	if ((pa >= dibp->ba) &&
	   (pa < (dibp->ba + dibp->lnt))) {
	    dibp->rd (&val, pa, READ);
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
	if ((pa >= dibp->ba) &&
	   (pa < (dibp->ba + dibp->lnt))) {
	    dibp->wr (val, pa, mode);
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

if (hlt_pin) return IPL_HLTPIN;				/* hlt pin int */
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

/* Return vector for highest priority hardware interrupt at IPL lvl */

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
uint32 ma;

if (map_addr (qa, &ma)) return M[ma >> 2];		/* map addr */
MACH_CHECK (MCHK_READ);					/* err? mcheck */
return 0;
}

void cqmem_wr (int32 pa, int32 val, int32 lnt)
{
int32 qa = pa & CQMAMASK;				/* Qbus addr */
uint32 ma;

if (map_addr (qa, &ma)) {				/* map addr */
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

t_bool map_addr (uint32 qa, uint32 *ma)
{
int32 qblk = (qa >> VA_V_VPN);				/* Qbus blk */
int32 qmma = ((qblk << 2) & CQMAPAMASK) + cq_mbr;	/* map entry */

if (ADDR_IS_MEM (qmma)) {				/* legit? */
	int32 qmap = M[qmma >> 2];			/* get map */
	if (qmap & CQMAP_VLD) {				/* valid? */
	    *ma = ((qmap & CQMAP_PAG) << VA_V_VPN) + VA_GETOFF (qa);
	    if (ADDR_IS_MEM (*ma)) return 1;		/* legit addr */
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

int32 map_readB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma, dat;

if ((ba | bc) & 03) {					/* check alignment */
	for (i = ma = 0; i < bc; i++, buf++) {		/* by bytes */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
		*buf = ReadB (ma);
		ma = ma + 1;  }
	}
else {	for (i = ma = 0; i < bc; i = i + 4, buf++) {	/* by longwords */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    dat = ReadL (ma);				/* get lw */
	    *buf++ = dat & BMASK;			/* low 8b */
	    *buf++ = (dat >> 8) & BMASK;		/* next 8b */
	    *buf++ = (dat >> 16) & BMASK;		/* next 8b */
	    *buf = (dat >> 24) & BMASK;
	    ma = ma + 4;  }
	}
return 0;
}

int32 map_readW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i;
uint32 ma,dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {					/* check alignment */
	for (i = ma = 0; i < bc; i = i + 2, buf++) {	/* by words */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    *buf = ReadW (ma);
	    ma = ma + 2;  }
	}
else {	for (i = ma = 0; i < bc; i = i + 4, buf++) {	/* by longwords */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    dat = ReadL (ma);				/* get lw */
	    *buf++ = dat & WMASK;			/* low 16b */
	    *buf = (dat >> 16) & WMASK;			/* high 16b */
	    ma = ma + 4;  }
	}
return 0;
}

int32 map_readL (uint32 ba, int32 bc, uint32 *buf)
{
int32 i;
uint32 ma;

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

int32 map_writeB (uint32 ba, int32 bc, uint8 *buf)
{
int32 i;
uint32 ma, dat;

if ((ba | bc) & 03) {					/* check alignment */
	for (i = ma = 0; i < bc; i++, buf++) {		/* by bytes */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    WriteB (ma, *buf);
	    ma = ma + 1;  }
	}
else {	for (i = ma = 0; i < bc; i = i + 4, buf++) {	/* by longwords */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    dat = (uint32) *buf++;			/* get low 8b */
	    dat = dat | (((uint32) *buf++) << 8);	/* merge next 8b */
	    dat = dat | (((uint32) *buf++) << 16);	/* merge next 8b */
	    dat = dat | (((uint32) *buf) << 24);	/* merge hi 8b */
	    WriteL (ma, dat);				/* store lw */
	    ma = ma + 4;  }
	}
return 0;
}

int32 map_writeW (uint32 ba, int32 bc, uint16 *buf)
{
int32 i;
uint32 ma, dat;

ba = ba & ~01;
bc = bc & ~01;
if ((ba | bc) & 03) {					/* check alignment */
	for (i = ma = 0; i < bc; i = i + 2, buf++) {	/* by words */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    WriteW (ma, *buf);
	    ma = ma + 2;  }
	}
else {	for (i = ma = 0; i < bc; i = i + 4, buf++) {	/* by longwords */
	    if ((ma & VA_M_OFF) == 0) {			/* need map? */
		if (!map_addr (ba + i, &ma) ||		/* inv or NXM? */
		    !ADDR_IS_MEM (ma)) return (bc - i);  }
	    dat = (uint32) *buf++;			/* get low 16b */
	    dat = dat | (((uint32) *buf) << 16);	/* merge hi 16b */
	    WriteL (ma, dat);				/* store lw */
	    ma = ma + 4;  }
	}
return 0;
}

int32 map_writeL (uint32 ba, int32 bc, uint32 *buf)
{
int32 i;
uint32 ma;

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
newba = (uint32) get_uint (cptr, 16, IOPAGEBASE+IOPAGEMASK, &r);	/* get new */
if (r != SCPE_OK) return r;
if ((newba <= IOPAGEBASE) ||				/* must be > 0 */
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
fprintf (st, "address=%08X", dibp->ba);
if (dibp->lnt > 1)
	fprintf (st, "-%08X", dibp->ba + dibp->lnt - 1);
if (dptr->flags & DEV_FLTA) fprintf (st, "*");
return SCPE_OK;
}

/* Set address floating */

t_stat set_addr_flt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;

if (cptr == NULL) return SCPE_ARG;
if ((val == 0) || (uptr == NULL)) return SCPE_IERR;
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
newvec = (uint32) get_uint (cptr, 16, VEC_Q + 01000, &r);
if ((r != SCPE_OK) || (newvec <= VEC_Q) ||
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
else {	fprintf (st, "vector=%X", vec);
	if (numvec > 1) fprintf (st, "-%X", vec + (4 * (numvec - 1)));  }
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
		printf ("Device %s address conflict at %08X\n",
		    sim_dname (dptr), dibp->ba);
		if (sim_log) fprintf (sim_log,
		    "Device %s address conflict at %08X\n",
		    sim_dname (dptr), dibp->ba);
		return TRUE;  }  }
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

t_stat build_dib_tab (void)
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
dib_tab[j] = NULL;					/* end with NULL */
for (i = 0; (dibp = dib_tab[i]) != NULL; i++) {		/* test built dib_tab */
	if (dev_conflict (dibp)) return SCPE_STOP;  }	/* for conflicts */
return FALSE;
}

/* Show dib_tab */

t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, j, done = 0;
DEVICE *dptr;
DIB *dibt;

build_dib_tab ();					/* build table */
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
	fprintf (st, "%08X - %08X%c\t%s\n", dib_tab[i]->ba,
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

	{ 0x7, 0x3 },					/* DEUNA/DELUA */
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
