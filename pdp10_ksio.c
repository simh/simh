/* pdp10_ksio.c: PDP-10 KS10 I/O subsystem simulator

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

   uba		Unibus adapters

   25-Aug-01	RMS	Enabled DZ11
   21-Aug-01	RMS	Updated DZ11 disable
   01-Jun-01	RMS	Updated DZ11 vectors
   12-May-01	RMS	Fixed typo

   The KS10 uses the PDP-11 Unibus for its I/O, via adapters.  While
   nominally four adapters are supported, in practice only 1 and 3
   are implemented.  The disks are placed on adapter 1, the rest of
   the I/O devices on adapter 3.

   In theory, we should maintain completely separate Unibuses, with
   distinct PI systems.  In practice, this simulator has so few devices
   that we can get away with a single PI system, masking for which
   devices are on adapter 1, and which on adapter 3.  The Unibus
   implementation is modeled on the Qbus in the PDP-11 simulator and
   is described there.

   The I/O subsystem is programmed by I/O instructions which create
   Unibus operations (read, read pause, write, write byte).  DMA is
   the responsibility of the I/O device simulators, which also implement
   Unibus to physical memory mapping.

   The priority interrupt subsystem (and other privileged functions)
   is programmed by I/O instructions with internal devices codes
   (opcodes 700-702).  These are dispatched here, although many are
   handled in the memory management unit or elsewhere.

   The ITS instructions are significantly different from the TOPS-10/20
   instructions.  They do not use the extended address calculation but
   instead provide instruction variants (Q for Unibus adapter 1, I for
   Unibus adapter 3) which insert the Unibus adapter number into the
   effective address.
*/

#include "pdp10_defs.h"
#include <setjmp.h>

#define eaRB		(ea & ~1)
#define GETBYTE(ea,x)	((((ea) & 1)? (x) >> 8: (x)) & 0377)
#define UBNXM_FAIL(pa,op) \
			n = iocmap[GET_IOUBA (pa)]; \
			if (n >= 0) ubcs[n] = ubcs[n] | UBCS_TMO | UBCS_NXD; \
			pager_word = PF_HARD | PF_VIRT | PF_IO | \
				((op == WRITEB)? PF_BYTE: 0) | \
				(TSTF (F_USR)? PF_USER: 0) | (pa); \
			ABORT (PAGE_FAIL)

/* Unibus adapter data */

int32 ubcs[UBANUM] = { 0 };				/* status registers */
int32 ubmap[UBANUM][UMAP_MEMSIZE] = { 0 };		/* Unibus maps */
int32 int_req = 0;					/* interrupt requests */
int32 dev_enb = -1 & ~(INT_PTR | INT_PTP | INT_DZ0RX);	/* device enables */

/* Map IO controller numbers to Unibus adapters: -1 = non-existent */

static int iocmap[IO_N_UBA] = {				/* map I/O ext to UBA # */
 -1, 0, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }; 

static const int32 ubabr76[UBANUM] = {
	INT_UB1 & (INT_IPL7 | INT_IPL6), INT_UB3 & (INT_IPL7 | INT_IPL6) };
static const int32 ubabr54[UBANUM] = {
	INT_UB1 & (INT_IPL5 | INT_IPL4), INT_UB3 & (INT_IPL5 | INT_IPL4) };

extern d10 *ac_cur;
extern d10 pager_word;
extern int32 flags, pi_l2bit[8];
extern UNIT cpu_unit;
extern jmp_buf save_env;

extern d10 Read (a10 ea);
extern void pi_eval ();
extern t_stat dz0_rd (int32 *data, int32 addr, int32 access);
extern t_stat dz0_wr (int32 data, int32 addr, int32 access);
extern t_stat pt_rd (int32 *data, int32 addr, int32 access);
extern t_stat pt_wr (int32 data, int32 addr, int32 access);
extern t_stat lp20_rd (int32 *data, int32 addr, int32 access);
extern t_stat lp20_wr (int32 data, int32 addr, int32 access);
extern int32 lp20_inta (void);
extern t_stat rp_rd (int32 *data, int32 addr, int32 access);
extern t_stat rp_wr (int32 data, int32 addr, int32 access);
extern int32 rp_inta (void);
extern t_stat tu_rd (int32 *data, int32 addr, int32 access);
extern t_stat tu_wr (int32 data, int32 addr, int32 access);
extern int32 tu_inta (void);
extern t_stat tcu_rd (int32 *data, int32 addr, int32 access);
t_stat ubmap_rd (int32 *data, int32 addr, int32 access);
t_stat ubmap_wr (int32 data, int32 addr, int32 access);
t_stat ubs_rd (int32 *data, int32 addr, int32 access);
t_stat ubs_wr (int32 data, int32 addr, int32 access);
t_stat rd_zro (int32 *data, int32 addr, int32 access);
t_stat wr_nop (int32 data, int32 addr, int32 access);
t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat uba_reset (DEVICE *dptr);
d10 ReadIO (a10 ea);
void WriteIO (a10 ea, d10 val, int32 mode);

/* Unibus adapter data structures

   uba_dev	UBA device descriptor
   uba_unit	UBA units
   uba_reg	UBA register list
*/

UNIT uba_unit[] = {
	{ UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) },
	{ UDATA (NULL, UNIT_FIX, UMAP_MEMSIZE) }  };

REG uba_reg[] = {
	{ ORDATA (INTREQ, int_req, 32), REG_RO },
	{ ORDATA (UB1CS, ubcs[0], 18) },
	{ ORDATA (UB3CS, ubcs[1], 18) },
	{ ORDATA (DEVENB, dev_enb, 32), REG_HRO },
	{ NULL }  };

DEVICE uba_dev = {
	"UBA", uba_unit, uba_reg, NULL,
	UBANUM, 8, UMAP_ASIZE, 1, 8, 32,
	&uba_ex, &uba_dep, &uba_reset,
	NULL, NULL, NULL };

/* PDP-11 I/O structures */

struct iolink {						/* I/O page linkage */
	int32	low;					/* low I/O addr */
	int32	high;					/* high I/O addr */
	int32	enb;					/* enable mask */
	t_stat	(*read)();				/* read routine */
	t_stat	(*write)();  };				/* write routine */

/* Table of I/O devices and corresponding read/write routines
   The expected Unibus adapter number is included as the high 2 bits */

struct iolink iotable[] = {
	{ IO_UBA1+IO_RHBASE, IO_UBA1+IO_RHBASE+047, 0,
			&rp_rd, &rp_wr },		/* disk */
	{ IO_UBA3+IO_TMBASE, IO_UBA3+IO_TMBASE+033, 0,
			&tu_rd, &tu_wr },		/* mag tape */
	{ IO_UBA3+IO_DZBASE, IO_UBA3+IO_DZBASE+07, INT_DZ0RX,
			&dz0_rd, &dz0_wr },		/* terminal mux */
	{ IO_UBA3+IO_LPBASE, IO_UBA3+IO_LPBASE+017, 0,
			&lp20_rd, &lp20_wr },		/* line printer */
	{ IO_UBA3+IO_PTBASE, IO_UBA3+IO_PTBASE+07, INT_PTR,
			&pt_rd, &pt_wr },		/* paper tape */
	{ IO_UBA1+IO_UBMAP, IO_UBA1+IO_UBMAP+077, 0,
			&ubmap_rd, &ubmap_wr },		/* Unibus 1 map */
	{ IO_UBA3+IO_UBMAP, IO_UBA3+IO_UBMAP+077, 0,
			&ubmap_rd, &ubmap_wr },		/* Unibus 3 map */
	{ IO_UBA1+IO_UBCS, IO_UBA1+IO_UBCS, 0,
			&ubs_rd, &ubs_wr },		/* Unibus 1 c/s */
	{ IO_UBA3+IO_UBCS, IO_UBA3+IO_UBCS, 0,
			&ubs_rd, &ubs_wr },		/* Unibus 3 c/s */
	{ IO_UBA1+IO_UBMNT, IO_UBA1+IO_UBMNT, 0,
			&rd_zro, &wr_nop },		/* Unibus 1 maint */
	{ IO_UBA3+IO_UBMNT, IO_UBA3+IO_UBMNT, 0,
			&rd_zro, &wr_nop },		/* Unibus 3 maint */
	{ IO_UBA3+IO_TCUBASE, IO_UBA3+IO_TCUBASE+05, 0,
			&tcu_rd, &wr_nop },		/* TCU150 */
	{ 00100000, 00100000, 0, &rd_zro, &wr_nop },	/* Mem sys stat */
	{ 0, 0, 0, NULL, NULL }  };

/* Interrupt request to interrupt action map */

int32 (*int_ack[32])() = {				/* int ack routines */
	NULL, NULL, NULL, NULL, NULL, NULL, &rp_inta, &tu_inta,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, &lp20_inta, NULL, NULL, NULL, NULL, NULL  };

/* Interrupt request to vector map */

int32 int_vec[32] = {					/* int req to vector */
	0, 0, 0, 0, 0, 0, VEC_RP, VEC_TU,
	0, 0, 0, 0, 0, 0, 0, 0,
	VEC_DZ0RX, VEC_DZ0TX, 0, 0, 0, 0, 0, 0, 
	VEC_PTR, VEC_PTP, VEC_LP20, 0, 0, 0, 0, 0 };

/* IO 710	(DEC) TIOE - test I/O word, skip if zero
		(ITS) IORDI - read word from Unibus 3
			returns TRUE if skip, FALSE otherwise
*/

t_bool io710 (int32 ac, a10 ea)
{
d10 val;

if (ITS) AC(ac) = ReadIO (IO_UBA3 | ea);		/* IORDI */
else {							/* TIOE */
	val = ReadIO (ea);				/* read word */
	if ((AC(ac) & val) == 0) return TRUE;  }
return FALSE;
}

/* IO 711	(DEC) TION - test I/O word, skip if non-zero
		(ITS) IORDQ - read word from Unibus 1
			returns TRUE if skip, FALSE otherwise
*/

t_bool io711 (int32 ac, a10 ea)
{
d10 val;

if (ITS) AC(ac) = ReadIO (IO_UBA1 | ea);		/* IORDQ */
else {							/* TION */
	val = ReadIO (ea);				/* read word */
	if ((AC(ac) & val) != 0) return TRUE;  }
return FALSE;
}

/* IO 712	(DEC) RDIO - read I/O word, addr in ea
		(ITS) IORD - read I/O word, addr in M[ea]
*/

d10 io712 (a10 ea)
{
return ReadIO (ea);					/* RDIO, IORD */
}

/* IO 713	(DEC) WRIO - write I/O word, addr in ea
		(ITS) IOWR - write I/O word, addr in M[ea]
*/

void io713 (d10 val, a10 ea)
{
WriteIO (ea, val & 0177777, WRITE);			/* WRIO, IOWR */
return;
}

/* IO 714	(DEC) BSIO - set bit in I/O address
		(ITS) IOWRI - write word to Unibus 3
*/

void io714 (d10 val, a10 ea)
{
d10 temp;

val = val & 0177777;
if (ITS) WriteIO (IO_UBA3 | ea, val, WRITE);		/* IOWRI */
else {
	temp = ReadIO (ea);				/* BSIO */
	temp = temp | val;
	WriteIO (ea, temp, WRITE);  }
return;
}

/* IO 715	(DEC) BCIO - clear bit in I/O address
		(ITS) IOWRQ - write word to Unibus 1
*/

void io715 (d10 val, a10 ea)
{
d10 temp;

val = val & 0177777;
if (ITS) WriteIO (IO_UBA1 | ea, val, WRITE);		/* IOWRQ */
else {
	temp = ReadIO (ea);				/* BCIO */
	temp = temp & ~val;
	WriteIO (ea, temp, WRITE);  }
return;
}

/* IO 720	(DEC) TIOEB - test I/O byte, skip if zero
		(ITS) IORDBI - read byte from Unibus 3
			returns TRUE if skip, FALSE otherwise
*/

t_bool io720 (int32 ac, a10 ea)
{
d10 val;

if (ITS) {						/* IORDBI */
	val = ReadIO (IO_UBA3 | eaRB);
	AC(ac) = GETBYTE (ea, val);  }
else {							/* TIOEB */
	val = ReadIO (eaRB);
	val = GETBYTE (ea, val);
	if ((AC(ac) & val) == 0) return TRUE;  }
return FALSE;
}

/* IO 721	(DEC) TIONB - test I/O word, skip if non-zero
		(ITS) IORDBQ - read word from Unibus 1
			returns TRUE if skip, FALSE otherwise
*/

t_bool io721 (int32 ac, a10 ea)
{
d10 val;

if (ITS) {						/* IORDBQ */
	val = ReadIO (IO_UBA1 | eaRB);
	AC(ac) = GETBYTE (ea, val);  }
else {							/* TIONB */
	val = ReadIO (eaRB);
	val = GETBYTE (ea, val);
	if ((AC(ac) & val) != 0) return TRUE;  }
return FALSE;
}

/* IO 722	(DEC) RDIOB - read I/O byte, addr in ea
		(ITS) IORDB - read I/O byte, addr in M[ea]
*/

d10 io722 (a10 ea)
{
d10 val;

val = ReadIO (eaRB);					/* RDIOB, IORDB */
return GETBYTE (ea, val);
}

/* IO 723	(DEC) WRIOB - write I/O byte, addr in ea
		(ITS) IOWRB - write I/O byte, addr in M[ea]
*/

void io723 (d10 val, a10 ea)
{
WriteIO (ea, val & 0377, WRITEB);			/* WRIOB, IOWRB */
return;
}

/* IO 724	(DEC) BSIOB - set bit in I/O byte address
		(ITS) IOWRBI - write byte to Unibus 3
*/

void io724 (d10 val, a10 ea)
{
d10 temp;

val = val & 0377;
if (ITS) WriteIO (IO_UBA3 | ea, val, WRITEB);		/* IOWRBI */
else {
	temp = ReadIO (eaRB);				/* BSIOB */
	temp = GETBYTE (ea, temp);
	temp = temp | val;
	WriteIO (ea, temp, WRITEB);  }
return;
}

/* IO 725	(DEC) BCIOB - clear bit in I/O byte address
		(ITS) IOWRBQ - write byte to Unibus 1
*/

void io725 (d10 val, a10 ea)
{
d10 temp;

val = val & 0377;
if (ITS) WriteIO (IO_UBA1 | ea, val, WRITEB);		/* IOWRBQ */
else {
	temp = ReadIO (eaRB);				/* BCIOB */
	temp = GETBYTE (ea, temp);
	temp = temp & ~val;
	WriteIO (ea, temp, WRITEB);  }
return;
}

/* Read and write I/O devices.
   These routines are the linkage between the 64b world of the main
   simulator and the 32b world of the device simulators.
*/

d10 ReadIO (a10 ea)
{
int32 n, pa, val;
struct iolink *p;

pa = (int32) ea;					/* cvt addr to 32b */
for (p = &iotable[0]; p -> low != 0; p++ ) {
	if ((pa >= p -> low) && (pa <= p -> high) &&
	    ((p -> enb == 0) || (dev_enb & p -> enb)))  {
		p -> read (&val, pa, READ);
		pi_eval ();
		return ((d10) val);  }  }
UBNXM_FAIL (pa, READ);
}

void WriteIO (a10 ea, d10 val, int32 mode)
{
int32 n, pa;
struct iolink *p;

pa = (int32) ea;					/* cvt addr to 32b */
for (p = &iotable[0]; p -> low != 0; p++ ) {
	if ((pa >= p -> low) && (pa <= p -> high) &&
	    ((p -> enb == 0) || (dev_enb & p -> enb)))  {
		p -> write ((int32) val, pa, mode);
		pi_eval ();
		return;  }  }
UBNXM_FAIL (pa, mode);
}

/* Evaluate Unibus priority interrupts */

int32 pi_ub_eval ()
{
int32 i, lvl;

for (i = lvl = 0; i < UBANUM; i++) {
	if (int_req & ubabr76[i])
		lvl = lvl | pi_l2bit[UBCS_GET_HI (ubcs[i])];
	if (int_req & ubabr54[i])
		lvl = lvl | pi_l2bit[UBCS_GET_LO (ubcs[i])];  }
return lvl;
}

/* Return Unibus device vector

   Takes as input the request level calculated by pi_eval
   If there is an interrupting Unibus device at that level, return its vector,
	otherwise, returns 0
*/

int32 pi_ub_vec (int32 rlvl, int32 *uba)
{
int32 i, masked_irq;

for (i = masked_irq = 0; i < UBANUM; i++) {
	if ((rlvl == UBCS_GET_HI (ubcs[i])) &&		/* req on hi level? */
		(masked_irq = int_req & ubabr76[i])) break;
	if ((rlvl == UBCS_GET_LO (ubcs[i])) &&		/* req on lo level? */
		(masked_irq = int_req & ubabr54[i])) break;  }
*uba = (i << 1) + 1;					/* store uba # */
for (i = 0; (i < 32) && masked_irq; i++) {		/* find hi pri req */
	if ((masked_irq >> i) & 1) {
		int_req = int_req & ~(1u << i);		/* clear req */
		if (int_ack[i]) return int_ack[i]();
		return int_vec[i];  }  }		/* return vector */
return 0;
}

/* Unibus adapter map routines */

t_stat ubmap_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0) ABORT (STOP_ILLIOC);
*val = ubmap[n][pa & UMAP_AMASK];
return SCPE_OK;
}

t_stat ubmap_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0) ABORT (STOP_ILLIOC);
ubmap[n][pa & UMAP_AMASK] = UMAP_POSFL (val) | UMAP_POSPN (val);
return SCPE_OK;
}

/* Unibus adapter control/status routines */

t_stat ubs_rd (int32 *val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0) ABORT (STOP_ILLIOC);
if (int_req & ubabr76[n]) ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n]) ubcs[n] = ubcs[n] | UBCS_LO;
*val = ubcs[n] = ubcs[n] & ~UBCS_RDZ;
return SCPE_OK;
}

t_stat ubs_wr (int32 val, int32 pa, int32 mode)
{
int32 n = iocmap[GET_IOUBA (pa)];

if (n < 0) ABORT (STOP_ILLIOC);
if (val & UBCS_INI) {
	reset_all (5);					/* start after UBA */
	ubcs[n] = val & UBCS_DXF;  }
else ubcs[n] = val & UBCS_RDW;
if (int_req & ubabr76[n]) ubcs[n] = ubcs[n] | UBCS_HI;
if (int_req & ubabr54[n]) ubcs[n] = ubcs[n] | UBCS_LO;
return SCPE_OK;
}

/* Unibus adapter read zero/write ignore routines */

t_stat rd_zro (int32 *val, int32 pa, int32 mode)
{
*val = 0;
return SCPE_OK;
}

t_stat wr_nop (int32 val, int32 pa, int32 mode)
{
return SCPE_OK;
}

t_stat uba_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE) return SCPE_NXM;
*vptr = ubmap[uba][addr];
return SCPE_OK;
}

t_stat uba_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
int32 uba = uptr - uba_unit;

if (addr >= UMAP_MEMSIZE) return SCPE_NXM;
ubmap[uba][addr] = (int32) val & UMAP_MASK;
return SCPE_OK;
}

t_stat uba_reset (DEVICE *dptr)
{
int32 i, uba;

int_req = 0;
for (uba = 0; uba < UBANUM; uba++) {
	ubcs[uba] = 0;
	for (i = 0; i < UMAP_MEMSIZE; i++) ubmap[uba][i] = 0;  }
pi_eval ();
return SCPE_OK;
}
