/* hp2100_mux.c: HP 2100 12920A terminal multiplexor simulator

   Copyright (c) 2002-2004, Robert M Supnik

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

   mux,muxl,muxc	12920A terminal multiplexor

   05-Jan-04	RMS	Revised for tmxr library changes
   21-Dec-03	RMS	Added invalid character screening for TSB (from Mike Gemeny)
   09-May-03	RMS	Added network device flag
   01-Nov-02	RMS	Added 7B/8B support
   22-Aug-02	RMS	Updated for changes to sim_tmxr

   The 12920A consists of three separate devices

   mux			scanner (upper data card)
   muxl			lines (lower data card)
   muxm			modem control (control card)

   The lower data card has no CMD flop; the control card has no CMD flop.
   The upper data card has none of the usual flops.
*/

#include "hp2100_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define MUX_LINES	16				/* user lines */
#define MUX_ILINES	5				/* diag rcv only */
#define UNIT_V_8B	(UNIT_V_UF + 0)			/* 8B */
#define UNIT_V_UC	(UNIT_V_UF + 1)			/* UC only */
#define UNIT_V_MDM	(UNIT_V_UF + 2)			/* modem control */
#define UNIT_8B		(1 << UNIT_V_8B)
#define UNIT_UC		(1 << UNIT_V_UC)
#define UNIT_MDM	(1 << UNIT_V_MDM)
#define MUXU_INIT_POLL	8000
#define MUXL_WAIT	500

/* Channel number (OTA upper, LIA lower or upper) */

#define MUX_V_CHAN	10				/* channel num */
#define MUX_M_CHAN	037
#define MUX_CHAN(x)	(((x) >> MUX_V_CHAN) & MUX_M_CHAN)

/* OTA, lower = parameters or data */

#define OTL_P		0100000				/* parameter */
#define OTL_TX		0040000				/* transmit */
#define OTL_ENB		0020000				/* enable */
#define OTL_TPAR	0010000				/* xmt parity */
#define OTL_ECHO	0010000				/* rcv echo */
#define OTL_DIAG	0004000				/* diagnose */
#define OTL_SYNC	0004000				/* sync */
#define OTL_V_LNT	8				/* char length */
#define OTL_M_LNT	07
#define OTL_LNT(x)	(((x) >> OTL_V_LNT) & OTL_M_LNT)
#define OTL_V_BAUD	0				/* baud rate */
#define OTL_M_BAUD	0377
#define OTL_BAUD(x)	(((x) >> OTL_V_BAUD) & OTL_M_BAUD)
#define OTL_CHAR	01777				/* char mask */

/* LIA, lower = received data */

#define LIL_PAR		0100000				/* parity */
#define PUT_DCH(x)	(((x) & MUX_M_CHAN) << MUX_V_CHAN)
#define LIL_CHAR	01777				/* character */

/* LIA, upper = status */

#define LIU_SEEK	0100000				/* seeking NI */
#define LIU_DG		0000010				/* diagnose */
#define LIU_BRK		0000004				/* break */
#define LIU_LOST	0000002				/* char lost */
#define LIU_TR		0000001				/* trans/rcv */

/* OTA, control */

#define OTC_SCAN	0100000				/* scan */
#define OTC_UPD		0040000				/* update */
#define OTC_V_CHAN	10				/* channel */
#define OTC_M_CHAN	017
#define OTC_CHAN(x)	(((x) >> OTC_V_CHAN) & OTC_M_CHAN)
#define OTC_EC2		0000200				/* enable Cn upd */
#define OTC_EC1		0000100
#define OTC_C2		0000040				/* Cn flops */
#define OTC_C1		0000020
#define OTC_ES2		0000010				/* enb comparison */
#define OTC_ES1		0000004
#define OTC_V_ES	2
#define OTC_SS2		0000002				/* SSn flops */
#define OTC_SS1		0000001
#define OTC_RW		(OTC_ES2|OTC_ES1|OTC_SS2|OTC_SS1)
#define RTS		OCT_C2				/* C2 = rts */
#define DTR		OTC_C1				/* C1 = dtr */

/* LIA, control */

#define LIC_MBO		0140000				/* always set */
#define LIC_V_CHAN	10				/* channel */
#define LIC_M_CHAN	017
#define PUT_CCH(x)	(((x) & OTC_M_CHAN) << OTC_V_CHAN)
#define LIC_I2		0001000				/* change flags */
#define LIC_I1		0000400
#define LIC_S2		0000002				/* Sn flops */
#define LIC_S1		0000001
#define LIC_V_I		8				/* S1 to I1 */
#define CDET		LIC_S2				/* S2 = cdet */
#define DSR		LIC_S1				/* S1 = dsr */

#define LIC_TSTI(ch)	(((muxc_lia[ch] ^ muxc_ota[ch]) & \
			 ((muxc_ota[ch] & (OTC_ES2|OTC_ES1)) >> OTC_V_ES)) \
			 << LIC_V_I)

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];

uint16 mux_sta[MUX_LINES];				/* line status */
uint16 mux_rpar[MUX_LINES + MUX_ILINES];		/* rcv param */
uint16 mux_xpar[MUX_LINES];				/* xmt param */
uint8 mux_rbuf[MUX_LINES + MUX_ILINES];			/* rcv buf */
uint8 mux_xbuf[MUX_LINES];				/* xmt buf */
uint8 mux_rchp[MUX_LINES + MUX_ILINES];			/* rcv chr pend */
uint8 mux_xdon[MUX_LINES];				/* xmt done */
uint8 muxc_ota[MUX_LINES];				/* ctrl: Cn,ESn,SSn */
uint8 muxc_lia[MUX_LINES];				/* ctrl: Sn */
uint32 mux_tps = 100;					/* polls/second */
uint32 muxl_ibuf = 0;					/* low in: rcv data */
uint32 muxl_obuf = 0;					/* low out: param */
uint32 muxu_ibuf = 0;					/* upr in: status */
uint32 muxu_obuf = 0;					/* upr out: chan */
uint32 muxc_chan = 0;					/* ctrl chan */
uint32 muxc_scan = 0;					/* ctrl scan */

TMLN mux_ldsc[MUX_LINES] = { 0 };			/* line descriptors */
TMXR mux_desc = { MUX_LINES, 0, 0, mux_ldsc };		/* mux descriptor */

DEVICE muxl_dev, muxu_dev, muxc_dev;
int32 muxlio (int32 inst, int32 IR, int32 dat);
int32 muxuio (int32 inst, int32 IR, int32 dat);
int32 muxcio (int32 inst, int32 IR, int32 dat);
t_stat muxi_svc (UNIT *uptr);
t_stat muxo_svc (UNIT *uptr);
t_stat mux_reset (DEVICE *dptr);
t_stat mux_attach (UNIT *uptr, char *cptr);
t_stat mux_detach (UNIT *uptr);
t_stat mux_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat mux_show (FILE *st, UNIT *uptr, int32 val, void *desc);
void mux_data_int (void);
void mux_ctrl_int (void);
void mux_diag (int32 c);

static uint8 odd_par[256] = {
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 000-017 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 020-037 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 040-067 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 060-077 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 100-117 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 120-137 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 140-157 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 160-177 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 200-217 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 220-237 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 240-257 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 260-277 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,	/* 300-317 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 320-337 */
 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0,	/* 340-367 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 };	/* 360-377 */

#define RCV_PAR(x)	(odd_par[(x) & 0377]? LIL_PAR: 0)

DIB mux_dib[] = {
	{ MUXL, 0, 0, 0, 0, &muxlio },
	{ MUXU, 0, 0, 0, 0, &muxuio }  };

#define muxl_dib mux_dib[0]
#define muxu_dib mux_dib[1]

/* MUX data structures

   muxu_dev	MUX device descriptor
   muxu_unit	MUX unit descriptor
   muxu_reg	MUX register list
   muxu_mod	MUX modifiers list
*/

UNIT muxu_unit = { UDATA (&muxi_svc, UNIT_ATTABLE, 0), MUXU_INIT_POLL };

REG muxu_reg[] = {
	{ ORDATA (IBUF, muxu_ibuf, 16) },
	{ ORDATA (OBUF, muxu_obuf, 16) },
	{ FLDATA (CMD, muxu_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, muxu_dib.ctl, 0), REG_HRO },
	{ FLDATA (FLG, muxu_dib.flg, 0), REG_HRO },
	{ FLDATA (FBF, muxu_dib.fbf, 0), REG_HRO },
	{ ORDATA (DEVNO, muxu_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB muxu_mod[] = {
	{ UNIT_ATT, UNIT_ATT, "connections", NULL, NULL, &mux_summ },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
		NULL, &mux_show, NULL },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
		NULL, &mux_show, NULL },
	{ MTAB_XTD|MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &muxl_dev },
	{ MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
		&tmxr_dscln, NULL, &mux_desc },
	{ 0 }  };

DEVICE muxu_dev = {
	"MUX", &muxu_unit, muxu_reg, muxu_mod,
	1, 10, 31, 1, 8, 8,
	&tmxr_ex, &tmxr_dep, &mux_reset,
	NULL, &mux_attach, &mux_detach,
	&muxu_dib, DEV_NET | DEV_DISABLE };

/* MUXL data structures

   muxl_dev	MUXL device descriptor
   muxl_unit	MUXL unit descriptor
   muxl_reg	MUXL register list
   muxl_mod	MUXL modifiers list
*/

UNIT muxl_unit[] = {
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT },
	{ UDATA (&muxo_svc, UNIT_UC, 0), MUXL_WAIT } };

MTAB muxl_mod[] = {
	{ UNIT_UC+UNIT_8B, UNIT_UC, "UC", "UC", NULL },
	{ UNIT_UC+UNIT_8B, 0      , "7b", "7B", NULL },
	{ UNIT_UC+UNIT_8B, UNIT_8B, "8b", "8B", NULL },
	{ UNIT_MDM, 0, "no dataset", "NODATASET", NULL },
	{ UNIT_MDM, UNIT_MDM, "dataset", "DATASET", NULL },
	{ MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
	    &tmxr_set_log, &tmxr_show_log, &mux_desc },
	{ MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
	    &tmxr_set_nolog, NULL, &mux_desc },
	{ MTAB_XTD|MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &muxl_dev },
	{ 0 }  };

REG muxl_reg[] = {
	{ FLDATA (CMD, muxl_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, muxl_dib.ctl, 0) },
	{ FLDATA (FLG, muxl_dib.flg, 0) },
	{ FLDATA (FBF, muxl_dib.fbf, 0) },
	{ BRDATA (STA, mux_sta, 8, 16, MUX_LINES) },
	{ BRDATA (RPAR, mux_rpar, 8, 16, MUX_LINES + MUX_ILINES) },
	{ BRDATA (XPAR, mux_xpar, 8, 16, MUX_LINES) },
	{ BRDATA (RBUF, mux_rbuf, 8, 8, MUX_LINES + MUX_ILINES) },
	{ BRDATA (XBUF, mux_xbuf, 8, 8, MUX_LINES) },
	{ BRDATA (RCHP, mux_rchp, 8, 1, MUX_LINES + MUX_ILINES) },
	{ BRDATA (XDON, mux_xdon, 8, 1, MUX_LINES) },
	{ URDATA (TIME, muxl_unit[0].wait, 10, 24, 0,
		  MUX_LINES, REG_NZ + PV_LEFT) },
	{ ORDATA (DEVNO, muxl_dib.devno, 6), REG_HRO },
	{ NULL }  };

DEVICE muxl_dev = {
	"MUXL", muxl_unit, muxl_reg, muxl_mod,
	MUX_LINES, 10, 31, 1, 8, 8,
	NULL, NULL, &mux_reset,
	NULL, NULL, NULL,
	&muxl_dib, 0 };

/* MUXM data structures

   muxc_dev	MUXM device descriptor
   muxc_unit	MUXM unit descriptor
   muxc_reg	MUXM register list
   muxc_mod	MUXM modifiers list
*/

DIB muxc_dib = { MUXC, 0, 0, 0, 0, &muxcio };

UNIT muxc_unit = { UDATA (NULL, 0, 0) };

REG muxc_reg[] = {
	{ FLDATA (CMD, muxc_dib.cmd, 0), REG_HRO },
	{ FLDATA (CTL, muxc_dib.ctl, 0) },
	{ FLDATA (FLG, muxc_dib.flg, 0) },
	{ FLDATA (FBF, muxc_dib.fbf, 0) },
	{ FLDATA (SCAN, muxc_scan, 0) },
	{ ORDATA (CHAN, muxc_chan, 4) },
	{ BRDATA (DSO, muxc_ota, 8, 6, MUX_LINES) },
	{ BRDATA (DSI, muxc_lia, 8, 2, MUX_LINES) },
	{ ORDATA (DEVNO, muxc_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB muxc_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &muxc_dev },
	{ 0 }  };

DEVICE muxc_dev = {
	"MUXM", &muxc_unit, muxc_reg, muxc_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &mux_reset,
	NULL, NULL, NULL,
	&muxc_dib, 0 };

/* IOT routines: data cards */

int32 muxlio (int32 inst, int32 IR, int32 dat)
{
int32 dev, ln;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	muxl_obuf = dat;				/* store data */
	break;
case ioMIX:						/* merge */
	dat = dat | muxl_ibuf;
	break;
case ioLIX:						/* load */
	dat = muxl_ibuf;
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) { clrCTL (dev); }		/* CLC */
	else {						/* STC */
	    setCTL (dev);				/* set ctl */
	    ln = MUX_CHAN (muxu_obuf);			/* get chan # */
	    if (muxl_obuf & OTL_P) {			/* parameter set? */
		if (muxl_obuf & OTL_TX) {		/* transmit? */
		    if (ln < MUX_LINES)			/* to valid line? */
			mux_xpar[ln] = muxl_obuf;  }
		else if (ln < (MUX_LINES + MUX_ILINES))	/* rcv, valid line? */
		    mux_rpar[ln] = muxl_obuf;  }
	    else if ((muxl_obuf & OTL_TX) &&		/* xmit data? */
		    (ln < MUX_LINES)) {			/* to valid line? */
		if (sim_is_active (&muxl_unit[ln]))	/* still working? */
		    mux_sta[ln] = mux_sta[ln] | LIU_LOST;
		else sim_activate (&muxl_unit[ln], muxl_unit[ln].wait);
		mux_xbuf[ln] = muxl_obuf & OTL_CHAR;  }	/* load buffer */
	    }						/* end STC */
	break;
default:
	break;  }
if (IR & I_HC) {					/* H/C option */
	clrFLG (dev);					/* clear flag */
	mux_data_int ();  }				/* look for new int */
return dat;
}

int32 muxuio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {						/* case on opcode */
case ioOTX:						/* output */
	muxu_obuf = dat;				/* store data */
	break;
case ioMIX:						/* merge */
	dat = dat | muxu_ibuf;
	break;
case ioLIX:						/* load */
	dat = muxu_ibuf;
	break;
default:
	break;  }
return dat;
}

/* IOT routine: control card */

int32 muxcio (int32 inst, int32 IR, int32 dat)
{
int32 dev, ln, t, old;

dev = IR & I_DEVMASK;					/* get device no */
switch (inst) {						/* case on opcode */
case ioFLG:						/* flag clear/set */
	if ((IR & I_HC) == 0) { setFLG (dev); }		/* STF */
	break;
case ioSFC:						/* skip flag clear */
	if (FLG (dev) == 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioSFS:						/* skip flag set */
	if (FLG (dev) != 0) PC = (PC + 1) & VAMASK;
	return dat;
case ioOTX:						/* output */
	if (dat & OTC_SCAN) muxc_scan = 1;		/* set scan flag */
	else muxc_scan = 0;
	if (dat & OTC_UPD) {				/* update? */
	    ln = OTC_CHAN (dat);			/* get channel */
	    old = muxc_ota[ln];				/* save prior val */
	    muxc_ota[ln] = (muxc_ota[ln] & ~OTC_RW) |	/* save ESn,SSn */
		(dat & OTC_RW);
	    if (dat & OTC_EC2) muxc_ota[ln] =		/* if EC2, upd C2 */
		(muxc_ota[ln] & ~OTC_C2) | (dat & OTC_C2);
	    if (dat & OTC_EC1) muxc_ota[ln] =		/* if EC1, upd C1 */
		(muxc_ota[ln] & ~OTC_C1) | (dat & OTC_C1);
	    if ((muxl_unit[ln].flags & UNIT_MDM) &&	/* modem ctrl? */
		(old & DTR) && !(muxc_ota[ln] & DTR)) {	/* DTR drop? */
	    	tmxr_linemsg (&mux_ldsc[ln], "\r\nLine hangup\r\n");
		tmxr_reset_ln (&mux_ldsc[ln]);		/* reset line */
		muxc_lia[ln] = 0;  }			/* dataset off */
	    }						/* end update */
	break;
case ioLIX:						/* load */
	dat = 0;
case ioMIX:						/* merge */
	t = LIC_MBO | PUT_CCH (muxc_chan) |		/* mbo, chan num */
	    LIC_TSTI (muxc_chan) |			/* I2, I1 */
	    (muxc_ota[muxc_chan] & (OTC_ES2 | OTC_ES1)) | /* ES2, ES1 */
	    (muxc_lia[muxc_chan] & (LIC_S2 | LIC_S1));	/* S2, S1 */
	dat = dat | t;					/* return status */
	muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;	/* incr channel */
	break;
case ioCTL:						/* ctrl clear/set */
	if (IR & I_CTL) { clrCTL (dev); }		/* CLC */
	else { setCTL (dev); }				/* STC */
	break;
default:
	break;  }
if (IR & I_HC) {					/* H/C option */
	clrFLG (dev);					/* clear flag */
	mux_ctrl_int (); }				/* look for new int */
return dat;
}

/* Unit service - receive side

   Poll for new connections
   Poll all active lines for input
*/

t_stat muxi_svc (UNIT *uptr)
{
int32 ln, c, t;

if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;	/* attached? */
t = sim_rtcn_calb (mux_tps, TMR_MUX);			/* calibrate */
sim_activate (uptr, t);					/* continue poll */
ln = tmxr_poll_conn (&mux_desc);			/* look for connect */
if (ln >= 0) {						/* got one? */
	if ((muxl_unit[ln].flags & UNIT_MDM) &&		/* modem ctrl? */
	    (muxc_ota[ln] & DTR))			/* DTR? */
	    muxc_lia[ln] = muxc_lia[ln] | CDET;		/* set cdet */
	muxc_lia[ln] = muxc_lia[ln] | DSR;		/* set dsr */
	mux_ldsc[ln].rcve = 1;  }			/* rcv enabled */ 
tmxr_poll_rx (&mux_desc);				/* poll for input */
for (ln = 0; ln < MUX_LINES; ln++) {			/* loop thru lines */
	if (mux_ldsc[ln].conn) {			/* connected? */
	    if (c = tmxr_getc_ln (&mux_ldsc[ln])) {	/* get char */
	        if (c & SCPE_BREAK) {			/* break? */
		    mux_sta[ln] = mux_sta[ln] | LIU_BRK;
		    mux_rbuf[ln] = 0;  }		/* no char */
		else {					/* normal */
		    if (mux_rchp[ln]) mux_sta[ln] = mux_sta[ln] | LIU_LOST;
		    if (muxl_unit[ln].flags & UNIT_UC) {	/* cvt to UC? */
			c = c & 0177;
			if (islower (c)) c = toupper (c);  }
		    else c = c & ((muxl_unit[ln].flags & UNIT_8B)? 0377: 0177);
		    if (mux_rpar[ln] & OTL_ECHO) {		/* echo? */
			TMLN *lp = &mux_ldsc[ln];		/* get line */
			tmxr_putc_ln (lp, c);		/* output char */
			tmxr_poll_tx (&mux_desc);  }	/* poll xmt */
		    mux_rbuf[ln] = c;			/* save char */
		    mux_rchp[ln] = 1;  }		/* char pending */
		if (mux_rpar[ln] & OTL_DIAG) mux_diag (c); /* rcv diag? */
		}					/* end if char */
	    }						/* end if connected */
	else muxc_lia[ln] = 0;				/* disconnected */
	}						/* end for */
if (!FLG (muxl_dib.devno)) mux_data_int ();		/* scan for data int */
if (!FLG (muxc_dib.devno)) mux_ctrl_int ();		/* scan modem */
return SCPE_OK;
}

/* Unit service - transmit side */

t_stat muxo_svc (UNIT *uptr)
{
int32 c, ln = uptr - muxl_unit;				/* line # */

if (mux_ldsc[ln].conn) {				/* connected? */
	if (mux_ldsc[ln].xmte) {			/* xmt enabled? */
	    if ((mux_xbuf[ln] & OTL_SYNC) == 0) {	/* start bit 0? */
		TMLN *lp = &mux_ldsc[ln];		/* get line */
		c = mux_xbuf[ln];			/* get char */
		if (muxl_unit[ln].flags & UNIT_UC) {	/* cvt to UC? */
		    c = c & 0177;
		    if (islower (c)) c = toupper (c);  }
		else c = c & ((muxl_unit[ln].flags & UNIT_8B)? 0377: 0177);
		if (mux_xpar[ln] & OTL_DIAG)		/* xmt diag? */
		    mux_diag (mux_xbuf[ln]);		/* before munge */
		mux_xdon[ln] = 1;			/* set done */
		if (!(muxl_unit[ln].flags & UNIT_8B) &&	/* not transparent? */
		    (c != 0x7f) &&  (c != 0x13) &&	/* not del, ^S? */
		    (c != 0x11) && (c != 0x5))		/* not ^Q, ^E? */
		     tmxr_putc_ln (lp, c);		/* output char */
		tmxr_poll_tx (&mux_desc);  }  }		/* poll xmt */
	else {						/* buf full */
	    tmxr_poll_tx (&mux_desc);			/* poll xmt */
	    sim_activate (uptr, muxl_unit[ln].wait);	/* wait */
	    return SCPE_OK;  }  }
if (!FLG (muxl_dib.devno)) mux_data_int ();		/* scan for int */
return SCPE_OK;
}

/* Look for data interrupt */

void mux_data_int (void)
{
int32 i;

for (i = 0; i < MUX_LINES; i++) {			/* rcv lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {	/* enabled, char? */
	muxl_ibuf = PUT_CCH (i) | mux_rbuf[i] |		/* lo buf = char */
	    RCV_PAR (mux_rbuf[i]);
	muxu_ibuf = PUT_CCH (i) | mux_sta[i];		/* hi buf = stat */
	mux_rchp[i] = 0;				/* clr char, stat */
	mux_sta[i] = 0;
	setFLG (muxl_dib.devno);			/* interrupt */
	return;  }  }
for (i = 0; i < MUX_LINES; i++) {			/* xmt lines */
    if ((mux_xpar[i] & OTL_ENB) && mux_xdon[i]) {	/* enabled, done? */
	muxu_ibuf = PUT_CCH (i) | mux_sta[i] | LIU_TR;	/* hi buf = stat */
	mux_xdon[i] = 0;				/* clr done, stat */
	mux_sta[i] = 0;
	setFLG (muxl_dib.devno);			/* interrupt */
	return;  }  }
for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++) {	/* diag lines */
    if ((mux_rpar[i] & OTL_ENB) && mux_rchp[i]) {	/* enabled, char? */
	muxl_ibuf = PUT_CCH (i) | mux_rbuf[i] |		/* lo buf = char */
	    RCV_PAR (mux_rbuf[i]);
	muxu_ibuf = PUT_CCH (i) | mux_sta[i] | LIU_DG;	/* hi buf = stat */
	mux_rchp[i] = 0;				/* clr char, stat */
	mux_sta[i] = 0;
	setFLG (muxl_dib.devno);
	return;  }  }
return;
}

/* Look for control interrupt */

void mux_ctrl_int (void)
{
int32 i;

if (muxc_scan == 0) return;
for (i = 0; i < MUX_LINES; i++) {
	muxc_chan = (muxc_chan + 1) & LIC_M_CHAN;	/* step channel */
	if (LIC_TSTI (muxc_chan)) {			/* status change? */
	    setFLG (muxc_dib.devno);			/* set flag */
	    break;  }  }
return;
}

/* Set diagnostic lines for given character */

void mux_diag (int32 c)
{
int32 i;

for (i = MUX_LINES; i < (MUX_LINES + MUX_ILINES); i++) {
	if (c & SCPE_BREAK) {				/* break? */
	    mux_sta[i] = mux_sta[i] | LIU_BRK;
	    mux_rbuf[i] = 0;  }				/* no char */
	else {
	    if (mux_rchp[i]) mux_sta[i] = mux_sta[i] | LIU_LOST;
	    mux_rchp[i] = 1;
	    mux_rbuf[i] = c;  }  }
return;
}

/* Reset an individual line */

void mux_reset_ln (int32 i)
{
mux_rbuf[i] = mux_xbuf[i] = 0;				/* clear state */
mux_rpar[i] = mux_xpar[i] = 0;
mux_rchp[i] = mux_xdon[i] = 0;
mux_sta[i] = 0;
muxc_ota[i] = muxc_lia[i] = 0;				/* clear modem */
if (mux_ldsc[i].conn)					/* connected? */
	muxc_lia[i] = muxc_lia[i] | DSR |		/* cdet, dsr */
	(muxl_unit[i].flags & UNIT_MDM? CDET: 0);
sim_cancel (&muxl_unit[i]);
return;
}

/* Reset routine */

t_stat mux_reset (DEVICE *dptr)
{
int32 i, t;

if (muxu_dev.flags & DEV_DIS) {				/* enb/dis dev */
	muxl_dev.flags = muxu_dev.flags | DEV_DIS;
	muxc_dev.flags = muxc_dev.flags | DEV_DIS;  }
else {	muxl_dev.flags = muxl_dev.flags & ~DEV_DIS;
	muxc_dev.flags = muxc_dev.flags & ~DEV_DIS;  }
muxl_dib.cmd = muxl_dib.ctl = 0;			/* init lower */
muxl_dib.flg = muxl_dib.fbf = 1;
muxu_dib.cmd = muxu_dib.ctl = 0;			/* upper not */
muxu_dib.flg = muxu_dib.fbf = 0;			/* implemented */
muxc_dib.cmd = muxc_dib.ctl = 0;			/* init ctrl */
muxc_dib.flg = muxc_dib.fbf = 1;
muxc_chan = muxc_scan = 0;				/* init modem scan */
if (muxu_unit.flags & UNIT_ATT) {			/* master att? */
	if (!sim_is_active (&muxu_unit)) {
	    t = sim_rtcn_init (muxu_unit.wait, TMR_MUX);
	    sim_activate (&muxu_unit, t);  }  }		/* activate */
else sim_cancel (&muxu_unit);				/* else stop */
for (i = 0; i < MUX_LINES; i++) mux_reset_ln (i);
return SCPE_OK;
}

/* Attach master unit */

t_stat mux_attach (UNIT *uptr, char *cptr)
{
t_stat r;
int32 t;

r = tmxr_attach (&mux_desc, uptr, cptr);		/* attach */
if (r != SCPE_OK) return r;				/* error */
t = sim_rtcn_init (muxu_unit.wait, TMR_MUX);
sim_activate (uptr, t);					/* start poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat mux_detach (UNIT *uptr)
{
int32 i;
t_stat r;

r = tmxr_detach (&mux_desc, uptr);			/* detach */
for (i = 0; i < MUX_LINES; i++) mux_ldsc[i].rcve = 0;	/* disable rcv */
sim_cancel (uptr);					/* stop poll */
return r;
}

/* Show summary processor */

t_stat mux_summ (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, t;

for (i = t = 0; i < MUX_LINES; i++) t = t + (mux_ldsc[i].conn != 0);
if (t == 1) fprintf (st, "1 connection");
else fprintf (st, "%d connections", t);
return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat mux_show (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i;

for (i = 0; (i < MUX_LINES) && (mux_ldsc[i].conn == 0); i++) ;
if (i < MUX_LINES) {
	for (i = 0; i < MUX_LINES; i++) {
	    if (mux_ldsc[i].conn) { 
		if (val) tmxr_fconns (st, &mux_ldsc[i], i);
		else tmxr_fstats (st, &mux_ldsc[i], i);  }  }  }
else fprintf (st, "all disconnected\n");
return SCPE_OK;
}

