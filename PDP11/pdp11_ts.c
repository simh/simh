/* pdp11_ts.c: TS11/TSV05 magnetic tape simulator

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

   ts		TS11/TSV05 magtape

   30-Sep-02	RMS	Added variable address support to bootstrap
			Added vector change/display support
			Fixed CTL unload/clean decode
			Implemented XS0_MOT in extended status
			New data structures, revamped error recovery
   28-Aug-02	RMS	Added end of medium support
   30-May-02	RMS	Widened POS to 32b
   22-Apr-02	RMS	Added maximum record length protection
   04-Apr-02	RMS	Fixed bug in residual frame count after space operation
   16-Feb-02	RMS	Fixed bug in message header logic
   26-Jan-02	RMS	Revised bootstrap to conform to M9312
   06-Jan-02	RMS	Revised enable/disable support
   30-Nov-01	RMS	Added read only unit, extended SET/SHOW support
   09-Nov-01	RMS	Added bus map, VAX support
   15-Oct-01	RMS	Integrated debug logging across simulator
   27-Sep-01	RMS	Implemented extended characteristics and status
			Fixed bug in write characteristics status return
   19-Sep-01	RMS	Fixed bug in bootstrap
   15-Sep-01	RMS	Fixed bug in NXM test
   07-Sep-01	RMS	Revised device disable and interrupt mechanism
   13-Jul-01	RMS	Fixed bug in space reverse (found by Peter Schorn)

   Magnetic tapes are represented as a series of variable 8b records
   of the form:

	32b record length in bytes - exact number
	byte 0
	byte 1
	:
	byte n-2
	byte n-1
	32b record length in bytes - exact number

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a single record length of 0.
   End of tape is two consecutive end of file marks.

   The TS11 functions in three environments:

   - PDP-11 Q22 systems - the I/O map is one for one, so it's safe to
     go through the I/O map
   - PDP-11 Unibus 22b systems - the TS11 behaves as an 18b Unibus
     peripheral and must go through the I/O map
   - VAX Q22 systems - the TS11 must go through the I/O map
*/

#if defined (USE_INT64)					/* VAX version */
#include "vax_defs.h"
#define VM_VAX		1
#define TS_RDX		16
#define TS_DIS		0				/* on by default */
#define ADDRTEST	0177700
#define DMASK		0xFFFF
extern int32 ReadB (t_addr pa);
extern void WriteB (t_addr pa, int32 val);
extern int32 ReadW (t_addr pa);
extern void WriteW (t_addr pa, int32 val);

#else							/* PDP11 version */
#include "pdp11_defs.h"
#define VM_PDP11	1
#define TS_RDX		8
#define TS_DIS		DEV_DIS				/* off by default */
#define ADDRTEST	(UNIBUS? 0177774: 0177700)
extern uint16 *M;
extern int32 cpu_18b, cpu_ubm;
#define ReadB(p)	((M[(p) >> 1] >> (((p) & 1)? 8: 0)) & 0377)
#define WriteB(p,v)	M[(p) >> 1] = ((p) & 1)? \
			((M[(p) >> 1] & 0377) | ((v) << 8)): \
			((M[(p) >> 1] & ~0377) | (v))
#define ReadW(p)	M[(p) >> 1]
#define WriteW(p,v)	M[(p) >> 1] = (v)
#endif

#define UNIT_V_WLK	(UNIT_V_UF + 0)			/* write locked */
#define UNIT_WLK	(1 << UNIT_V_WLK)
#define UNIT_WPRT	(UNIT_WLK | UNIT_RO)		/* write protect */

/* TSBA/TSDB - 17772520: base address/data buffer register

   read:	most recent memory address
   write word:	initiate command
   write byte:	diagnostic use
*/

/* TSSR  - 17772522: subsystem status register
   TSDBX - 17772523: extended address register

   read:	return status
   write word:	initialize
   write byte:	if odd, set extended packet address register
*/

#define TSSR_SC		0100000				/* special condition */
#define TSSR_RMR	0010000				/* reg mod refused */
#define TSSR_NXM	0004000				/* nxm */
#define TSSR_NBA	0002000				/* need buf addr */
#define TSSR_V_EMA	8				/* mem addr<17:16> */
#define TSSR_EMA	0001400
#define TSSR_SSR	0000200				/* subsystem ready */
#define TSSR_OFL	0000100				/* offline */
#define TSSR_V_TC	1				/* term class */
#define TSSR_M_TC	07
#define TSSR_TC		(TSSR_M_TC << TSSR_V_TC)
#define  TC0		(0 << TSSR_V_TC)		/* ok */
#define  TC1		(1 << TSSR_V_TC)		/* attention */
#define  TC2		(2 << TSSR_V_TC)		/* status alert */
#define  TC3		(3 << TSSR_V_TC)		/* func reject */
#define  TC4		(4 << TSSR_V_TC)		/* retry, moved */
#define  TC5		(5 << TSSR_V_TC)		/* retry */
#define  TC6		(6 << TSSR_V_TC)		/* pos lost */
#define  TC7		(7 << TSSR_V_TC)		/* fatal err */
#define TSSR_MBZ	0060060
#define GET_TC(x)	(((x) >> TSSR_V_TC) & TSSR_M_TC)	

#define TSDBX_M_XA	017				/* ext addr */
#define TSDBX_BOOT	0000200				/* boot */

/* Command packet offsets */

#define CMD_PLNT	4				/* cmd pkt length */
#define cmdhdr		tscmdp[0]			/* header */
#define cmdadl		tscmdp[1]			/* address low */
#define cmdadh		tscmdp[2]			/* address high */
#define cmdlnt		tscmdp[3]			/* length */

/* Command packet header */

#define CMD_ACK		0100000				/* acknowledge */
#define CMD_CVC		0040000				/* clear vol chk */
#define CMD_OPP		0020000				/* opposite */
#define CMD_SWP		0010000				/* swap bytes */
#define CMD_V_MODE	8				/* mode */
#define CMD_M_MODE	017
#define CMD_IE		0000200				/* int enable */
#define CMD_V_FNC	0				/* function */
#define CMD_M_FNC	037				/* function */
#define CMD_N_FNC	(CMD_M_FNC + 1)
#define  FNC_READ	001				/* read */
#define  FNC_WCHR	004				/* write char */
#define  FNC_WRIT	005				/* write */
#define  FNC_WSSM	006				/* write mem */
#define  FNC_POS	010				/* position */
#define  FNC_FMT	011				/* format */
#define  FNC_CTL	012				/* control */
#define  FNC_INIT	013				/* init */
#define  FNC_GSTA	017				/* get status */
#define CMD_MBZ		0000140
#define GET_FNC(x)	(((x) >> CMD_V_FNC) & CMD_M_FNC)
#define GET_MOD(x)	(((x) >> CMD_V_MODE) & CMD_M_MODE)

/* Function test flags */

#define FLG_MO		001				/* motion */
#define FLG_WR		002				/* write */
#define FLG_AD		004				/* addr mem */

/* Message packet offsets */

#define MSG_PLNT	8				/* packet length */
#define msghdr		tsmsgp[0]			/* header */
#define msglnt		tsmsgp[1]			/* length */
#define msgrfc		tsmsgp[2]			/* residual frame */
#define msgxs0		tsmsgp[3]			/* ext status 0 */
#define msgxs1		tsmsgp[4]			/* ext status 1 */
#define msgxs2		tsmsgp[5]			/* ext status 2 */
#define msgxs3		tsmsgp[6]			/* ext status 3 */
#define msgxs4		tsmsgp[7]			/* ext status 4 */

/* Message packet header */

#define MSG_ACK		0100000				/* acknowledge */
#define MSG_MATN	0000000				/* attention */
#define MSG_MILL	0000400				/* illegal */
#define MSG_MNEF	0001000				/* non exec fnc */
#define MSG_CEND	0000020				/* end */
#define MSG_CFAIL	0000021				/* fail */
#define MSG_CERR	0000022				/* error */
#define MSG_CATN	0000023				/* attention */

/* Extended status register 0 */

#define XS0_TMK		0100000				/* tape mark */
#define XS0_RLS		0040000				/* rec lnt short */
#define XS0_LET		0020000				/* log end tape */
#define XS0_RLL		0010000				/* rec lnt long */
#define XS0_WLE		0004000				/* write lock err */
#define XS0_NEF		0002000				/* non exec fnc */
#define XS0_ILC		0001000				/* illegal cmd */
#define XS0_ILA		0000400				/* illegal addr */
#define XS0_MOT		0000200				/* tape has moved */
#define XS0_ONL		0000100				/* online */
#define XS0_IE		0000040				/* int enb */
#define XS0_VCK		0000020				/* volume check */
#define XS0_PET		0000010				/* 1600 bpi */
#define XS0_WLK		0000004				/* write lock */
#define XS0_BOT		0000002				/* BOT */
#define XS0_EOT		0000001				/* EOT */
#define XS0_ALLCLR	0177600				/* clear at start */

/* Extended status register 1 */

#define XS1_UCOR	0000002				/* uncorrectable */

/* Extended status register 2 */

#define XS2_XTF		0000200				/* ext features */

/* Extended status register 3 */

#define XS3_OPI		0000100				/* op incomplete */
#define XS3_REV		0000040				/* reverse */
#define XS3_RIB		0000001				/* reverse to BOT */

/* Extended status register 4 */

#define XS4_HDS		0100000				/* high density */

/* Write characteristics packet offsets */

#define WCH_PLNT	5				/* packet length */
#define wchadl		tswchp[0]			/* address low */
#define wchadh		tswchp[1]			/* address high */
#define wchlnt		tswchp[2]			/* length */
#define wchopt		tswchp[3]			/* options */
#define wchxopt		tswchp[4]			/* ext options */

/* Write characteristics options */

#define WCH_ESS		0000200				/* stop dbl tmk */
#define WCH_ENB		0000100				/* BOT = tmk */
#define WCH_EAI		0000040				/* enb attn int */
#define WCH_ERI		0000020				/* enb mrls int */

/* Write characteristics extended options */

#define WCHX_HDS	0000040				/* high density */

#define MAX(a,b)	(((a) >= (b))? (a): (b))

extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];

extern UNIT cpu_unit;
extern int32 cpu_log;
extern FILE *sim_log;
uint8 *tsxb = NULL;					/* xfer buffer */
int32 tssr = 0;						/* status register */
int32 tsba = 0;						/* mem addr */
int32 tsdbx = 0;					/* data buf ext */
int32 tscmdp[CMD_PLNT] = { 0 };				/* command packet */
int32 tsmsgp[MSG_PLNT] = { 0 };				/* message packet */
int32 tswchp[WCH_PLNT] = { 0 };				/* wr char packet */
int32 ts_ownc = 0;					/* tape owns cmd */
int32 ts_ownm = 0;					/* tape owns msg */
int32 ts_qatn = 0;					/* queued attn */
int32 ts_bcmd = 0;					/* boot cmd */
int32 ts_time = 10;					/* record latency */

DEVICE ts_dev;
t_stat ts_rd (int32 *data, int32 PA, int32 access);
t_stat ts_wr (int32 data, int32 PA, int32 access);
t_stat ts_svc (UNIT *uptr);
t_stat ts_reset (DEVICE *dptr);
t_stat ts_attach (UNIT *uptr, char *cptr);
t_stat ts_detach (UNIT *uptr);
t_stat ts_boot (int32 unitno, DEVICE *dptr);
int32 ts_updtssr (int32 t);
int32 ts_updxs0 (int32 t);
void ts_cmpendcmd (int32 s0, int32 s1);
void ts_endcmd (int32 ssf, int32 xs0f, int32 msg);

/* TS data structures

   ts_dev	TS device descriptor
   ts_unit	TS unit list
   ts_reg	TS register list
   ts_mod	TS modifier list
*/

DIB ts_dib = { IOBA_TS, IOLN_TS, &ts_rd, &ts_wr,
		1, IVCL (TS), VEC_TS, { NULL } };

UNIT ts_unit = { UDATA (&ts_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) };

REG ts_reg[] = {
	{ GRDATA (TSSR, tssr, TS_RDX, 16, 0) },
	{ GRDATA (TSBA, tsba, TS_RDX, 22, 0) },
	{ GRDATA (TSDBX, tsdbx, TS_RDX, 8, 0) },
	{ GRDATA (CHDR, cmdhdr, TS_RDX, 16, 0) },
	{ GRDATA (CADL, cmdadl, TS_RDX, 16, 0) },
	{ GRDATA (CADH, cmdadh, TS_RDX, 16, 0) },
	{ GRDATA (CLNT, cmdlnt, TS_RDX, 16, 0) },
	{ GRDATA (MHDR, msghdr, TS_RDX, 16, 0) },
	{ GRDATA (MRFC, msgrfc, TS_RDX, 16, 0) },
	{ GRDATA (MXS0, msgxs0, TS_RDX, 16, 0) },
	{ GRDATA (MXS1, msgxs1, TS_RDX, 16, 0) },
	{ GRDATA (MXS2, msgxs2, TS_RDX, 16, 0) },
	{ GRDATA (MXS3, msgxs3, TS_RDX, 16, 0) },
	{ GRDATA (MSX4, msgxs4, TS_RDX, 16, 0) },
	{ GRDATA (WADL, wchadl, TS_RDX, 16, 0) },
	{ GRDATA (WADH, wchadh, TS_RDX, 16, 0) },
	{ GRDATA (WLNT, wchlnt, TS_RDX, 16, 0) },
	{ GRDATA (WOPT, wchopt, TS_RDX, 16, 0) },
	{ GRDATA (WXOPT, wchxopt, TS_RDX, 16, 0) },
	{ FLDATA (INT, IREQ (TS), INT_V_TS) },
	{ FLDATA (ATTN, ts_qatn, 0) },
	{ FLDATA (BOOT, ts_bcmd, 0) },
	{ FLDATA (OWNC, ts_ownc, 0) },
	{ FLDATA (OWNM, ts_ownm, 0) },
	{ DRDATA (TIME, ts_time, 24), PV_LEFT + REG_NZ },
	{ DRDATA (POS, ts_unit.pos, 32), PV_LEFT + REG_RO },
	{ GRDATA (DEVADDR, ts_dib.ba, TS_RDX, 32, 0), REG_HRO },
	{ GRDATA (DEVVEC, ts_dib.vec, TS_RDX, 16, 0), REG_HRO },
	{ NULL }  };

MTAB ts_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL }, 
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
		&set_addr, &show_addr, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", "VECTOR",
		&set_vec, &show_vec, NULL },
	{ 0 }  };

DEVICE ts_dev = {
	"TS", &ts_unit, ts_reg, ts_mod,
	1, 10, 31, 1, TS_RDX, 8,
	NULL, NULL, &ts_reset,
	&ts_boot, &ts_attach, &ts_detach,
	&ts_dib, DEV_DISABLE | TS_DIS | DEV_UBUS | DEV_QBUS };

/* I/O dispatch routine, I/O addresses 17772520 - 17772522

   17772520	TSBA	read/write
   17772522	TSSR	read/write
*/

t_stat ts_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 01) {				/* decode PA<1> */
case 0:							/* TSBA */
	*data = tsba & DMASK;				/* low 16b of ba */
	break;
case 1:							/* TSSR */
	*data = tssr = ts_updtssr (tssr);		/* update tssr */
	break;  }
return SCPE_OK;
}

t_stat ts_wr (int32 data, int32 PA, int32 access)
{
int32 i;
t_addr pa;

switch ((PA >> 1) & 01) {				/* decode PA<1> */
case 0:							/* TSDB */
	if ((tssr & TSSR_SSR) == 0) {			/* ready? */
		tssr = tssr | TSSR_RMR;			/* no, refuse */
		break;  }
	tsba = ((tsdbx & TSDBX_M_XA) << 18) |		/* form pkt addr */
		((data & 03) << 16) | (data & 0177774);
	tsdbx = 0;					/* clr tsdbx */
	tssr = ts_updtssr (tssr & TSSR_NBA);		/* clr ssr, err */
	msgxs0 = ts_updxs0 (msgxs0 & ~XS0_ALLCLR);	/* clr, upd xs0 */
	msgrfc = msgxs1 = msgxs2 = msgxs3 = msgxs4 = 0;	/* clr status */
	CLR_INT (TS);					/* clr int req */
	for (i = 0; i < CMD_PLNT; i++) {		/* get cmd pkt */
		if (Map_Addr (tsba, &pa) && ADDR_IS_MEM (pa))
			tscmdp[i] = ReadW (pa);
		else {	ts_endcmd (TSSR_NXM + TC5, 0, MSG_ACK|MSG_MNEF|MSG_CFAIL);
			return SCPE_OK;  }
		tsba = tsba + 2;  }			/* incr tsba */
	ts_ownc = ts_ownm = 1;				/* tape owns all */
	sim_activate (&ts_unit, ts_time);		/* activate */
	break;
case 1:							/* TSSR */
	if (PA & 1) {					/* TSDBX */
		if (UNIBUS) return SCPE_OK;		/* not in TS11 */
		if (tssr & TSSR_SSR) {			/* ready? */
			tsdbx = data;			/* save */
			if (data & TSDBX_BOOT) {
				ts_bcmd = 1;
				sim_activate (&ts_unit, ts_time);  }  }
		else tssr = tssr | TSSR_RMR;  }		/* no, err */
	else if (access == WRITE) ts_reset (&ts_dev);	/* reset */
	break;  }
return SCPE_OK;
}

/* Tape motion routines */

#define XTC(x,t)	(((unsigned) (x) << 16) | (t))
#define GET_X(x)	(((x) >> 16) & 0177777)
#define GET_T(x)	((x) & 0177777)

int32 ts_rdlntf (UNIT *uptr, t_mtrlnt *tbc)
{
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);	/* read rec lnt */
if (ferror (uptr->fileref)) {				/* error? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return (XTC (XS0_RLS, TC6));  }			/* pos lost */
if (feof (uptr->fileref) || (*tbc == MTR_EOM)) {	/* end of medium? */
	msgxs3 = msgxs3 | XS3_OPI;			/* incomplete */
	return (XTC (XS0_RLS, TC6));  }			/* pos lost */
return 0;
}

int32 ts_spacef (UNIT *uptr, int32 fc, t_bool upd)
{
int32 st;
t_mtrlnt tbc;

do {	fc = (fc - 1) & DMASK;				/* decr wc */
	if (upd) msgrfc = fc;
	if (st = ts_rdlntf (uptr, &tbc)) return st;	/* read rec lnt */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* update pos */
	msgxs0 = msgxs0 | XS0_MOT;			/* tape has moved */
	if (tbc == MTR_TMK)				/* tape mark? */
		return (XTC (XS0_TMK | XS0_RLS, TC2));
	uptr->pos = uptr->pos + ((MTRL (tbc) + 1) & ~1) + sizeof (t_mtrlnt);
	}
while (fc != 0);
return 0;
}

int32 ts_skipf (UNIT *uptr, int32 fc)
{
int32 tc;
t_mtrlnt prvp;
t_bool tmkprv = FALSE;

msgrfc = fc;
if ((uptr->pos == 0) && (wchopt & WCH_ENB)) tmkprv = TRUE;
do {	prvp = uptr->pos;				/* save cur pos */
	tc = ts_spacef (uptr, 0, FALSE);		/* space fwd */
	if (GET_X (tc) & XS0_TMK) {			/* tape mark? */
		msgrfc = (msgrfc - 1) & DMASK;		/* decr count */
		if (tmkprv && (wchopt & WCH_ESS) &&
		   (uptr->pos - prvp == sizeof (t_mtrlnt)))
			return (XTC ((msgrfc? XS0_RLS: 0) |
				XS0_TMK | XS0_LET, TC2));
		tmkprv = TRUE;  }
	else {	if (tc) return tc;			/* other err? */
		tmkprv = FALSE;  }  }
while (msgrfc != 0);
return 0;
}

int32 ts_rdlntr (UNIT *uptr, t_mtrlnt *tbc)
{
msgxs3 = msgxs3 | XS3_REV;				/* set rev op */
if (uptr->pos < sizeof (t_mtrlnt)) {			/* BOT? */
	msgxs3 = msgxs3 | XS3_RIB;			/* set status */
	return (XTC (XS0_BOT | XS0_RLS, TC2));  }	/* tape alert */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt), SEEK_SET);
fxread (tbc, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {				/* error? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return (XTC (XS0_RLS, TC6));  }			/* pos lost */
if (feof (uptr->fileref)) {				/* end of file? */
	msgxs3 = msgxs3 | XS3_OPI;			/* incomplete */
	return (XTC (XS0_RLS, TC6));  }			/* pos lost */
if (*tbc == MTR_EOM) {					/* eom? */
	msgxs3 = msgxs3 | XS3_OPI;			/* incomplete */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* spc over eom */
	return (XTC (XS0_RLS, TC6));  }			/* pos lost */
return 0;
}

int32 ts_spacer (UNIT *uptr, int32 fc, t_bool upd)
{
int32 st;
t_mtrlnt tbc;

if (upd) msgrfc = fc;
do {	fc = (fc - 1) & DMASK;				/* decr wc */
	if (upd) msgrfc = fc;
	if (st = ts_rdlntr (uptr, &tbc)) return st;	/* read rec lnt */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* update pos */
	msgxs0 = msgxs0 | XS0_MOT;			/* tape has moved */
	if (tbc == MTR_TMK)				/* tape mark? */
		return (XTC (XS0_TMK | XS0_RLS, TC2));
	uptr->pos = uptr->pos - ((MTRL (tbc) + 1) & ~1) - sizeof (t_mtrlnt);
	}
while (fc != 0);
return 0;
}

int32 ts_skipr (UNIT *uptr, int32 fc)
{
int32 tc;
t_mtrlnt prvp;
t_bool tmkprv = FALSE;

msgrfc = fc;
do {	prvp = uptr->pos;				/* save cur pos */
	tc = ts_spacer (uptr, 0, FALSE);		/* space rev */
	if (GET_X (tc) & XS0_TMK) {			/* tape mark? */
		msgrfc = (msgrfc - 1) & DMASK;		/* decr wc */
		if (tmkprv && (wchopt & WCH_ESS) &&
		   (prvp - uptr->pos == sizeof (t_mtrlnt)))
			return (XTC ((msgrfc? XS0_RLS: 0) |
				XS0_TMK | XS0_LET, TC2));
		tmkprv = TRUE;  }
	else {	if (tc) return tc;			/* other err? */
		tmkprv = FALSE;  }  }
while (msgrfc != 0);
return 0;
}

int32 ts_readf (UNIT *uptr, uint32 fc)
{
int32 st;
t_mtrlnt i, tbc, wbc;
t_addr wa, pa;

msgrfc = fc;
if (st = ts_rdlntf (uptr, &tbc)) return st;		/* read rec lnt */
if (tbc == MTR_TMK) {					/* tape mark? */
	uptr->pos = uptr->pos + sizeof (t_mtrlnt);	/* update pos */
	msgxs0 = msgxs0 | XS0_MOT;			/* tape has moved */
	return (XTC (XS0_TMK | XS0_RLS, TC2));  }
if (fc == 0) fc = 0200000;				/* byte count */
tsba = (cmdadh << 16) | cmdadl;				/* buf addr */
tbc = MTRL (tbc);					/* ignore err flag */
if (tbc > MT_MAXFR) {					/* record too long? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return XTC (XS0_RLS, TC6);  }			/* pos lost */
wbc = (tbc > fc)? fc: tbc;				/* cap buf size */
i = fxread (tsxb, sizeof (uint8), wbc, uptr->fileref);	/* read record */
if (ferror (uptr->fileref)) {				/* error? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return XTC (XS0_RLS, TC6);  }			/* pos lost */
for ( ; i < wbc; i++) tsxb[i] = 0;			/* fill with 0's */
uptr->pos = uptr->pos + ((tbc + 1) & ~1) + (2 * sizeof (t_mtrlnt));
msgxs0 = msgxs0 | XS0_MOT;				/* tape has moved */
for (i = 0; i < wbc; i++) {				/* copy buffer */
	wa = (cmdhdr & CMD_SWP)? tsba ^ 1: tsba;	/* apply OPP */
	if (Map_Addr (wa, &pa) && ADDR_IS_MEM (pa))	/* map addr, nxm? */
		WriteB (pa, tsxb[i]);			/* no, store */
	else {	tssr = ts_updtssr (tssr | TSSR_NXM);	/* set error */
		return (XTC (XS0_RLS, TC4));  }
	tsba = tsba + 1;
	msgrfc = (msgrfc - 1) & DMASK;  }
if (msgrfc) return (XTC (XS0_RLS, TC2));		/* buf too big? */
if (tbc > wbc) return (XTC (XS0_RLL, TC2));		/* rec too big? */
return 0;
}

int32 ts_readr (UNIT *uptr, uint32 fc)
{
int32 st;
t_mtrlnt i, tbc, wbc;
t_addr wa, pa;

msgrfc = fc;
if (st = ts_rdlntr (uptr, &tbc)) return st;		/* read rec lnt */
if (tbc == MTR_TMK) {					/* tape mark? */
	uptr->pos = uptr->pos - sizeof (t_mtrlnt);	/* update pos */
	msgxs0 = msgxs0 | XS0_MOT;			/* tape has moved */
	return XTC (XS0_TMK | XS0_RLS, TC2);  }
if (fc == 0) fc = 0200000;				/* byte count */
tsba = (cmdadh << 16) | cmdadl + fc;			/* buf addr */
tbc = MTRL (tbc);					/* ignore err flag */
if (tbc > MT_MAXFR) {					/* record too long? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return XTC (XS0_RLS, TC6);  }			/* pos lost */
wbc = (tbc > fc)? fc: tbc;				/* cap buf size */
fseek (uptr->fileref, uptr->pos - sizeof (t_mtrlnt) - wbc, SEEK_SET);
i = fxread (tsxb, sizeof (uint8), wbc, uptr->fileref);
for ( ; i < wbc; i++) tsxb[i] = 0;			/* fill with 0's */
if (ferror (uptr->fileref)) {				/* error? */
	msgxs1 = msgxs1 | XS1_UCOR;			/* uncorrectable */
	return XTC (XS0_RLS, TC6);  }			/* pos lost */
uptr->pos = uptr->pos - ((tbc + 1) & ~1) - (2 * sizeof (t_mtrlnt));
msgxs0 = msgxs0 | XS0_MOT;				/* tape has moved */
for (i = wbc; i > 0; i--) {				/* copy buffer */
	tsba = tsba - 1;
	wa = (cmdhdr & CMD_SWP)? tsba ^ 1: tsba;	/* apply OPP */
	if (Map_Addr (wa, &pa) && ADDR_IS_MEM (pa))	/* map addr, nxm? */
		WriteB (pa, tsxb[i - 1]);		/* no, store */
	else {	tssr = ts_updtssr (tssr | TSSR_NXM);
		return (XTC (XS0_RLS, TC4));  }
	msgrfc = (msgrfc - 1) & DMASK;  }
if (msgrfc) return (XTC (XS0_RLS, TC2));		/* buf too big? */
if (tbc > wbc) return (XTC (XS0_RLL, TC2));		/* rec too big? */
return 0;
}

int32 ts_write (UNIT *uptr, int32 fc)
{
int32 i, ebc;
t_addr wa, pa;

msgrfc = fc;
if (fc == 0) fc = 0200000;				/* byte count */
tsba = (cmdadh << 16) | cmdadl;				/* buf addr */
for (i = 0; i < fc; i++) {				/* copy mem to buf */
	wa = (cmdhdr & CMD_SWP)? tsba ^ 1: tsba;	/* apply OPP */
	if (Map_Addr (wa, &pa) && ADDR_IS_MEM (pa))	/* map addr, nxm? */
		tsxb[i] = ReadB (pa);			/* no, store */
	else {	tssr = ts_updtssr (tssr | TSSR_NXM);
		return TC5;  }
	tsba = tsba + 1;  }
ebc = (fc + 1) & ~1;					/* force even */
fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* position */
fxwrite (&fc, sizeof (t_mtrlnt), 1, uptr->fileref);
fxwrite (tsxb, sizeof (uint8), ebc, uptr->fileref);
fxwrite (&fc, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {				/* error? */
	msgxs3 = msgxs3 | XS3_OPI;
	return TC6;  }
uptr->pos = uptr->pos + ebc + (2 * sizeof (t_mtrlnt));	/* update pos */
msgxs0 = msgxs0 | XS0_MOT;				/* tape has moved */
msgrfc = 0;
return 0;
}

int32 ts_wtmk (UNIT *uptr)
{
t_mtrlnt bceof = MTR_TMK;

fseek (uptr->fileref, uptr->pos, SEEK_SET);		/* set pos */
fxwrite (&bceof, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) return TC6;
uptr->pos = uptr->pos + sizeof (t_mtrlnt);		/* update position */
msgxs0 = msgxs0 | XS0_MOT;				/* tape has moved */
return XTC (XS0_TMK, TC0);
}

/* Unit service */

t_stat ts_svc (UNIT *uptr)
{
int32 i, fnc, mod, st0, st1;
t_addr pa;

static const int32 fnc_mod[CMD_N_FNC] = {		/* max mod+1 0 ill */
 0, 4, 0, 0, 1, 2, 1, 0,				/* 00 - 07 */
 5, 3, 5, 1, 0, 0, 0, 1,				/* 10 - 17 */
 0, 0, 0, 0, 0, 0, 0, 0,				/* 20 - 27 */
 0, 0, 0, 0, 0, 0, 0, 0 };				/* 30 - 37 */
static const int32 fnc_flg[CMD_N_FNC] = {
 0, FLG_MO+FLG_AD, 0, 0, 0, FLG_MO+FLG_WR+FLG_AD, FLG_AD, 0,
 FLG_MO, FLG_MO+FLG_WR, FLG_MO, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0,				/* 20 - 27 */
 0, 0, 0, 0, 0, 0, 0, 0 };				/* 30 - 37 */

if (ts_bcmd) {						/* boot? */
	ts_bcmd = 0;					/* clear flag */
	uptr->pos = 0; 					/* rewind */
	if (uptr->flags & UNIT_ATT) {			/* attached? */
		cmdlnt = cmdadh = cmdadl = 0;		/* defang rd */
		ts_spacef (uptr, 1, FALSE);		/* space fwd */
		ts_readf (uptr, 512);			/* read blk */
		tssr = ts_updtssr (tssr | TSSR_SSR);  }
	else tssr = ts_updtssr (tssr | TSSR_SSR | TC3);
	if (cmdhdr & CMD_IE) SET_INT (TS);
	return SCPE_OK;  }

if (!(cmdhdr & CMD_ACK)) {				/* no acknowledge? */
	tssr = ts_updtssr (tssr | TSSR_SSR);		/* set rdy, int */
	if (cmdhdr & CMD_IE) SET_INT (TS);
	ts_ownc = ts_ownm = 0;				/* CPU owns all */
	return SCPE_OK;  }
fnc = GET_FNC (cmdhdr);					/* get fnc+mode */
mod = GET_MOD (cmdhdr);
if (DBG_LOG (LOG_TS))
	fprintf (sim_log, ">>TS: cmd=%o, mod=%o, buf=%o, lnt=%d, pos=%d\n",
		fnc, mod, cmdadl, cmdlnt, ts_unit.pos);
if ((fnc != FNC_WCHR) && (tssr & TSSR_NBA)) {		/* ~wr chr & nba? */
	ts_endcmd (TC3, 0, 0);				/* error */
	return SCPE_OK;  }
if (ts_qatn && (wchopt & WCH_EAI)) {			/* attn pending? */
	ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);	/* send attn msg */
	SET_INT (TS);					/* set interrupt */
	ts_qatn = 0;					/* not pending */
	return SCPE_OK;  }
if (cmdhdr & CMD_CVC)					/* cvc? clr vck */
	msgxs0 = msgxs0 & ~XS0_VCK;
if ((cmdhdr & CMD_MBZ) || (mod >= fnc_mod[fnc])) {	/* test mbz */
	ts_endcmd (TC3, XS0_ILC, MSG_ACK | MSG_MILL | MSG_CFAIL);
	return SCPE_OK;  }
if ((fnc_flg[fnc] & FLG_MO) &&				/* mot+(vck|!att)? */
	((msgxs0 & XS0_VCK) || !(uptr->flags & UNIT_ATT))) {
	ts_endcmd (TC3, XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
	return SCPE_OK;  }
if ((fnc_flg[fnc] & FLG_WR) &&				/* write? */
    (uptr->flags & UNIT_WPRT)) {			/* write lck? */
	ts_endcmd (TC3, XS0_WLE | XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
	return SCPE_OK;  }
if ((((fnc == FNC_READ) && (mod == 1)) ||		/* read rev */
     ((fnc == FNC_POS) && (mod & 1))) &&		/* space rev */
     (uptr->pos == 0)) {				/* BOT? */
	ts_endcmd (TC3, XS0_NEF, MSG_ACK | MSG_MNEF | MSG_CFAIL);
	return SCPE_OK;  }
if ((fnc_flg[fnc] & FLG_AD) && (cmdadh & ADDRTEST)) {	/* buf addr > 22b? */
	ts_endcmd (TC3, XS0_ILA, MSG_ACK | MSG_MILL | MSG_CFAIL);
	return SCPE_OK;  }

st0 = st1 = 0;
switch (fnc) {						/* case on func */
case FNC_INIT:						/* init */
	if (uptr->pos) msgxs0 = msgxs0 | XS0_MOT;	/* set if tape moves */
	uptr->pos = 0;					/* rewind */
case FNC_WSSM:						/* write mem */
case FNC_GSTA:						/* get status */
	ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);		/* send end packet */
	return SCPE_OK;
case FNC_WCHR:						/* write char */
	if ((cmdadh & ADDRTEST) || (cmdadl & 1) || (cmdlnt < 6)) {
		ts_endcmd (TSSR_NBA | TC3, XS0_ILA, 0);
		break;  }
	tsba = (cmdadh << 16) | cmdadl;
	for (i = 0; (i < WCH_PLNT) && (i < (cmdlnt / 2)); i++) {
		if (Map_Addr (tsba, &pa) && ADDR_IS_MEM (pa))
			tswchp[i] = ReadW (pa);
		else {	ts_endcmd (TSSR_NBA | TSSR_NXM | TC5, 0, 0);
			return SCPE_OK;  }
		tsba = tsba + 2;  }
	if ((wchlnt < ((MSG_PLNT - 1) * 2)) || (wchadh & 0177700) ||
	    (wchadl & 1)) ts_endcmd (TSSR_NBA | TC3, 0, 0);
	else {	msgxs2 = msgxs2 | XS2_XTF | 1;
		tssr = ts_updtssr (tssr & ~TSSR_NBA);
		ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);  }
	return SCPE_OK;
case FNC_CTL:						/* control */
	switch (mod) {					/* case mode */
	case 00:					/* msg buf rls */
		tssr = ts_updtssr (tssr | TSSR_SSR);	/* set SSR */
		if (wchopt & WCH_ERI) SET_INT (TS);
		ts_ownc = 0; ts_ownm = 1;		/* keep msg */
		break;
	case 01:					/* rewind and unload */
		if (uptr->pos) 	msgxs0 = msgxs0 | XS0_MOT; /* if tape moves */
		detach_unit (uptr);			/* unload */
		ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);
		break;
	case 02:					/* clean */
		ts_endcmd (TC0, 0, MSG_ACK | MSG_CEND);	/* nop */
		break;
	case 03:					/* undefined */
		ts_endcmd (TC3, XS0_ILC, MSG_ACK | MSG_MILL | MSG_CFAIL);
		return SCPE_OK;
	case 04:					/* rewind */
		if (uptr->pos) msgxs0 = msgxs0 | XS0_MOT; /* if tape moves */
		uptr->pos = 0;
		ts_endcmd (TC0, XS0_BOT, MSG_ACK | MSG_CEND);
		break;  }
	break;

case FNC_READ:						/* read */
	switch (mod) {					/* case mode */
	case 00:					/* fwd */
		st0 = ts_readf (uptr, cmdlnt);		/* read */
		break; 
	case 01:					/* back */
		st0 = ts_readr (uptr, cmdlnt);		/* read */
		break;
	case 02:					/* reread fwd */
		if (cmdhdr & CMD_OPP) {			/* opposite? */
			st0 = ts_readr (uptr, cmdlnt);
			st1 = ts_spacef (uptr, 1, FALSE);  }
		else {	st0 = ts_spacer (uptr, 1, FALSE);
			st1 = ts_readf (uptr, cmdlnt);  }
		break;
	case 03:					/* reread back */
		if (cmdhdr & CMD_OPP) {			/* opposite */
			st0 = ts_readf (uptr, cmdlnt);
			st1 = ts_spacer (uptr, 1, FALSE);  }
		else {	st0 = ts_spacef (uptr, 1, FALSE);
			st1 = ts_readr (uptr, cmdlnt);  }
		break;  }
	ts_cmpendcmd (st0, st1);
	break;
case FNC_WRIT:						/* write */
	switch (mod) {					/* case mode */
	case 00:					/* write */
		st0 = ts_write (uptr, cmdlnt);
		break;
	case 01:					/* rewrite */
		st0 = ts_spacer (uptr, 1, FALSE);
		st1 = ts_write (uptr, cmdlnt);
		break;  }
	ts_cmpendcmd (st0, st1);
	break;
case FNC_FMT:						/* format */
	switch (mod) {					/* case mode */
	case 00:					/* write tmk */
		st0 = ts_wtmk (uptr);
		break;
	case 01:					/* erase */
		break;
	case 02:					/* retry tmk */
		st0 = ts_spacer (uptr, 1, FALSE);
		st1 = ts_wtmk (uptr);
		break;  }
	ts_cmpendcmd (st0, st1);
	break;
case FNC_POS:
	switch (mod) {					/* case mode */
	case 00:					/* space fwd */
		st0 = ts_spacef (uptr, cmdadl, TRUE);
		break;
	case 01:					/* space rev */
		st0 = ts_spacer (uptr, cmdadl, TRUE);
		break;
	case 02:					/* space ffwd */
		st0 = ts_skipf (uptr, cmdadl);
		break;
	case 03:					/* space frev */
		st0 = ts_skipr (uptr, cmdadl);
		break;
	case 04:					/* rewind */
		if (uptr->pos) msgxs0 = msgxs0 | XS0_MOT; /* if tape moves */
		uptr->pos = 0;
		break;  }
	ts_cmpendcmd (st0, 0);
	break;  }
return SCPE_OK;
}

/* Utility routines */

int32 ts_updtssr (int32 t)
{
t = (t & ~TSSR_EMA) | ((tsba >> (16 - TSSR_V_EMA)) & TSSR_EMA);
if (ts_unit.flags & UNIT_ATT) t = t & ~TSSR_OFL;
else t = t | TSSR_OFL;
return (t & ~TSSR_MBZ);
}

int32 ts_updxs0 (int32 t)
{
t = (t & ~(XS0_ONL | XS0_WLK | XS0_BOT | XS0_IE)) | XS0_PET;
if (ts_unit.flags & UNIT_ATT) {
	t = t | XS0_ONL;
	if (ts_unit.flags & UNIT_WPRT) t = t | XS0_WLK;
	if (ts_unit.pos == 0) t = (t | XS0_BOT) & ~XS0_EOT;  }
else t = t & ~XS0_EOT;
if (cmdhdr & CMD_IE) t = t | XS0_IE;
return t;
}

void ts_cmpendcmd (int32 s0, int32 s1)
{
int32 xs0, ssr, tc;
static const int32 msg[8] = {
 MSG_ACK | MSG_CEND, MSG_ACK | MSG_MATN | MSG_CATN,
 MSG_ACK | MSG_CEND, MSG_ACK | MSG_CFAIL,
 MSG_ACK | MSG_CERR, MSG_ACK | MSG_CERR,
 MSG_ACK | MSG_CERR, MSG_ACK | MSG_CERR };

xs0 = GET_X (s0) | GET_X (s1);				/* or XS0 errs */
s0 = GET_T (s0);					/* get SSR errs */
s1 = GET_T (s1);
ssr = (s0 | s1) & ~TSSR_TC;				/* or SSR errs */
tc = MAX (GET_TC (s0), GET_TC (s1));			/* max term code */
ts_endcmd (ssr | (tc << TSSR_V_TC), xs0, msg[tc]);	/* end cmd */
return;
}

void ts_endcmd (int32 tc, int32 xs0, int32 msg)
{
int32 i;
t_addr pa;

msgxs0 = ts_updxs0 (msgxs0 | xs0);			/* update XS0 */
if (wchxopt & WCHX_HDS) msgxs4 = msgxs4 | XS4_HDS;	/* update XS4 */
if (msg && !(tssr & TSSR_NBA)) {			/* send end pkt */
	msghdr = msg;
	msglnt = wchlnt - 4;				/* exclude hdr, bc */
	tsba = (wchadh << 16) | wchadl;
	for (i = 0; (i < MSG_PLNT) && (i < (wchlnt / 2)); i++) {
		if (Map_Addr (tsba, &pa) && ADDR_IS_MEM (pa))
			WriteW (pa, tsmsgp[i]);
		else {	tssr = tssr | TSSR_NXM;
			tc = (tc & ~TSSR_TC) | TC4;
			break;  }  
		tsba = tsba + 2;  }  }
tssr = ts_updtssr (tssr | tc | TSSR_SSR | (tc? TSSR_SC: 0));
if (cmdhdr & CMD_IE) SET_INT (TS);
ts_ownm = 0; ts_ownc = 0;
if (DBG_LOG (LOG_TS))
	fprintf (sim_log, ">>TS: sta=%o, tc=%o, rfc=%d, pos=%d\n",
		msgxs0, GET_TC (tssr), msgrfc, ts_unit.pos);
return;
}

/* Device reset */

t_stat ts_reset (DEVICE *dptr)
{
int32 i;

ts_unit.pos = 0;
tsba = tsdbx = 0;
ts_ownc = ts_ownm = 0;
ts_bcmd = 0;
ts_qatn = 0;
tssr = ts_updtssr (TSSR_NBA | TSSR_SSR);
for (i = 0; i < CMD_PLNT; i++) tscmdp[i] = 0;
for (i = 0; i < WCH_PLNT; i++) tswchp[i] = 0;
for (i = 0; i < MSG_PLNT; i++) tsmsgp[i] = 0;
msgxs0 = ts_updxs0 (XS0_VCK);
CLR_INT (TS);
if (tsxb == NULL) tsxb = calloc (MT_MAXFR, sizeof (unsigned int8));
if (tsxb == NULL) return SCPE_MEM;
return SCPE_OK;
}

/* Attach */

t_stat ts_attach (UNIT *uptr, char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);				/* attach unit */
if (r != SCPE_OK) return r;				/* error? */
tssr = tssr & ~TSSR_OFL;				/* clr offline */
if ((tssr & TSSR_NBA) || !(wchopt & WCH_EAI)) return r;	/* attn msg? */
if (ts_ownm) {						/* own msg buf? */
	ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);	/* send attn */
	SET_INT (TS);					/* set interrupt */
	ts_qatn = 0;  }					/* don't queue */
else ts_qatn = 1;					/* else queue */
return r;
}

/* Detach routine */

t_stat ts_detach (UNIT* uptr)
{
t_stat r;

r = detach_unit (uptr);					/* detach unit */
if (r != SCPE_OK) return r;				/* error? */
tssr = tssr | TSSR_OFL;					/* set offline */
if ((tssr & TSSR_NBA) || !(wchopt & WCH_EAI)) return r;	/* attn msg? */
if (ts_ownm) {						/* own msg buf? */
	ts_endcmd (TC1, 0, MSG_MATN | MSG_CATN);	/* send attn */
	SET_INT (TS);					/* set interrupt */
	ts_qatn = 0;  }					/* don't queue */
else ts_qatn = 1;					/* else queue */
return r;
}

/* Boot */

#if defined (VM_PDP11)
#define BOOT_START	01000
#define BOOT_CSR0	(BOOT_START + 006)
#define BOOT_CSR1	(BOOT_START + 012)
#define BOOT_LEN	(sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
	0012706, 0001000,		/* mov #boot_start, sp */
	0012700, 0172520,		/* mov #tsba, r0 */
	0012701, 0172522,		/* mov #tssr, r1 */
	0005011,			/* clr (r1)		; init, rew */
	0105711,			/* tstb (r1)		; wait */
	0100376,			/* bpl .-2 */
	0012710, 0001070,		/* mov #pkt1, (r0)	; set char */
	0105711,			/* tstb (r1)		; wait */
	0100376,			/* bpl .-2 */
	0012710, 0001110,		/* mov #pkt2, (r0)	; read, skip */
	0105711,			/* tstb (r1)		; wait */
	0100376,			/* bpl .-2 */
	0012710, 0001110,		/* mov #pkt2, (r0)	; read */
	0105711,			/* tstb (r1)		; wait */
	0100376,			/* bpl .-2 */
	0005711,			/* tst (r1)		; err? */
	0100421,			/* bmi hlt */
	0005000,			/* clr r0 */
	0012704, 0001066+020,		/* mov #sgnt+20, r4 */
	0005007,			/* clr r7 */
	0046523,			/* sgnt: "SM" */
	0140004,			/* pkt1: 140004, wcpk, 0, 8. */
	0001100,
	0000000,
	0000010,
	0001122,			/* wcpk: msg, 0, 14., 0 */
	0000000,
	0000016,
	0000000,
	0140001,			/* pkt2: 140001, 0, 0, 512. */
	0000000,
	0000000,
	0001000,
	0000000				/* hlt:  halt */
					/* msg:	.blk 4 */
};

t_stat ts_boot (int32 unitno, DEVICE *dptr)
{
int32 i;
extern int32 saved_PC;

ts_unit.pos = 0;
for (i = 0; i < BOOT_LEN; i++)
	M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_CSR0 >> 1] = ts_dib.ba & DMASK;
M[BOOT_CSR1 >> 1] = (ts_dib.ba & DMASK) + 02;
saved_PC = BOOT_START;
return SCPE_OK;
} 
#else

t_stat ts_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}
#endif
