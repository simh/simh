/* hp2100_ipl.c: HP 2000 interprocessor link simulator

   Copyright (c) 2002-2003, Robert M Supnik

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

   ipli, iplo	12556B interprocessor link pair

   09-May-03	RMS	Added network device flag
   31-Jan-03	RMS	Links are full duplex (found by Mike Gemeny)
*/

#include "hp2100_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define UNIT_V_DIAG	(UNIT_V_UF + 0)			/* diagnostic mode */
#define UNIT_V_ACTV	(UNIT_V_UF + 1)			/* making connection */
#define UNIT_V_ESTB	(UNIT_V_UF + 2)			/* connection established */
#define UNIT_V_HOLD	(UNIT_V_UF + 3)			/* character holding */
#define UNIT_DIAG	(1 << UNIT_V_DIAG)
#define UNIT_ACTV	(1 << UNIT_V_ACTV)
#define UNIT_ESTB	(1 << UNIT_V_ESTB)
#define UNIT_HOLD	(1 << UNIT_V_HOLD)
#define IBUF		buf				/* input buffer */
#define OBUF		wait				/* output buffer */
#define DSOCKET		u3				/* data socket */
#define LSOCKET		u4				/* listening socket */

extern uint32 PC;
extern uint32 dev_cmd[2], dev_ctl[2], dev_flg[2], dev_fbf[2];
extern FILE *sim_log;
int32 ipl_ptime = 400;					/* polling interval */
int32 ipl_stopioe = 0;					/* stop on error */
int32 ipl_hold[2] = { 0 };				/* holding character */

DEVICE ipli_dev, iplo_dev;
int32 ipliio (int32 inst, int32 IR, int32 dat);
int32 iploio (int32 inst, int32 IR, int32 dat);
int32 iplio (UNIT *uptr, int32 inst, int32 IR, int32 dat);
t_stat ipl_svc (UNIT *uptr);
t_stat ipl_reset (DEVICE *dptr);
t_stat ipl_attach (UNIT *uptr, char *cptr);
t_stat ipl_detach (UNIT *uptr);
t_stat ipl_boot (int32 unitno, DEVICE *dptr);
t_stat ipl_dscln (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ipl_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool ipl_check_conn (UNIT *uptr);

/* IPLI data structures

   ipli_dev	IPLI device descriptor
   ipli_unit	IPLI unit descriptor
   ipli_reg	IPLI register list
*/

DIB ipl_dib[] = {
	{ IPLI, 0, 0, 0, 0, &ipliio },
	{ IPLO, 0, 0, 0, 0, &iploio }  };

#define ipli_dib ipl_dib[0]
#define iplo_dib ipl_dib[1]

UNIT ipl_unit[] = {
	{ UDATA (&ipl_svc, UNIT_ATTABLE, 0) },
	{ UDATA (&ipl_svc, UNIT_ATTABLE, 0) }  };

#define ipli_unit ipl_unit[0]
#define iplo_unit ipl_unit[1]

REG ipli_reg[] = {
	{ ORDATA (IBUF, ipli_unit.IBUF, 16) },
	{ ORDATA (OBUF, ipli_unit.OBUF, 16) },
	{ FLDATA (CMD, ipli_dib.cmd, 0) },
	{ FLDATA (CTL, ipli_dib.ctl, 0) },
	{ FLDATA (FLG, ipli_dib.flg, 0) },
	{ FLDATA (FBF, ipli_dib.fbf, 0) },
	{ ORDATA (HOLD, ipl_hold[0], 8) },
	{ DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
	{ FLDATA (STOP_IOE, ipl_stopioe, 0) },
	{ ORDATA (DEVNO, ipli_dib.devno, 6), REG_HRO },
	{ NULL }  };

MTAB ipl_mod[] = {
	{ UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", &ipl_setdiag },
	{ UNIT_DIAG, 0, "link mode", "LINK", &ipl_setdiag },
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "DISCONNECT",
		&ipl_dscln, NULL, NULL },
	{ MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
		&hp_setdev, &hp_showdev, &ipli_dev },
	{ 0 }  };

DEVICE ipli_dev = {
	"IPLI", &ipli_unit, ipli_reg, ipl_mod,
	1, 10, 31, 1, 16, 16,
	&tmxr_ex, &tmxr_dep, &ipl_reset,
	&ipl_boot, &ipl_attach, &ipl_detach,
	&ipli_dib, DEV_NET | DEV_DISABLE | DEV_DIS  };

/* IPLO data structures

   iplo_dev	IPLO device descriptor
   iplo_unit	IPLO unit descriptor
   iplo_reg	IPLO register list
*/

REG iplo_reg[] = {
	{ ORDATA (IBUF, iplo_unit.IBUF, 16) },
	{ ORDATA (OBUF, iplo_unit.OBUF, 16) },
	{ FLDATA (CMD, iplo_dib.cmd, 0) },
	{ FLDATA (CTL, iplo_dib.ctl, 0) },
	{ FLDATA (FLG, iplo_dib.flg, 0) },
	{ FLDATA (FBF, iplo_dib.fbf, 0) },
	{ ORDATA (HOLD, ipl_hold[1], 8) },
	{ DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
	{ ORDATA (DEVNO, iplo_dib.devno, 6), REG_HRO },
	{ NULL }  };

DEVICE iplo_dev = {
	"IPLO", &iplo_unit, iplo_reg, ipl_mod,
	1, 10, 31, 1, 16, 16,
	&tmxr_ex, &tmxr_dep, &ipl_reset,
	&ipl_boot, &ipl_attach, &ipl_detach,
	&iplo_dib, DEV_NET | DEV_DISABLE | DEV_DIS  };

/* Interprocessor link I/O routines */

int32 ipliio (int32 inst, int32 IR, int32 dat)
{
return iplio (&ipli_unit, inst, IR, dat);
}

int32 iploio (int32 inst, int32 IR, int32 dat)
{
return iplio (&iplo_unit, inst, IR, dat);
}

int32 iplio (UNIT *uptr, int32 inst, int32 IR, int32 dat)
{
uint32 u, dev, odev;
int32 sta;
int8 msg[2];

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
	uptr->OBUF = dat;
	break;
case ioLIX:						/* load */
	dat = uptr->IBUF;				/* return val */
	break;
case ioMIX:						/* merge */
	dat = dat | uptr->IBUF;				/* get return data */
	break;
case ioCTL:						/* control clear/set */
	if (IR & I_CTL) {				/* CLC */
	    clrCMD (dev);				/* clear ctl, cmd */
	    clrCTL (dev);  }
	else {						/* STC */
	    setCMD (dev);				/* set ctl, cmd */
	    setCTL (dev);
	    if (uptr->flags & UNIT_ATT) {		/* attached? */
		if ((uptr->flags & UNIT_ESTB) == 0) {	/* established? */
		    if (!ipl_check_conn (uptr))		/* not established? */
			return STOP_NOCONN;		/* lose */
		    uptr->flags = uptr->flags | UNIT_ESTB;  }
		msg[0] = (uptr->OBUF >> 8) & 0377;
		msg[1] = uptr->OBUF & 0377;
		sta = sim_write_sock (uptr->DSOCKET, msg, 2);
		if (sta == SOCKET_ERROR) {
		    printf ("IPL: socket write error\n");
		    return SCPE_IOERR;  }
		sim_os_sleep (0);  }
	    else if (uptr->flags & UNIT_DIAG) {		/* diagnostic mode? */
		u = (uptr - ipl_unit) ^ 1;		/* find other device */
		ipl_unit[u].IBUF = uptr->OBUF;		/* output to other */
		odev = ipl_dib[u].devno;		/* other device no */
		setFLG (odev);  }			/* set other flag */
	    else return SCPE_UNATT;  }			/* lose */
	break;
default:
	break;  }
if (IR & I_HC) { clrFLG (dev); }			/* H/C option */
return dat;
}

/* Unit service - poll for input */

t_stat ipl_svc (UNIT *uptr)
{
int32 u, nb, dev;
int8 msg[2];

u = uptr - ipl_unit;					/* get link number */
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;	/* not attached? */
sim_activate (uptr, ipl_ptime);				/* reactivate */
if ((uptr->flags & UNIT_ESTB) == 0) {			/* not established? */
	if (!ipl_check_conn (uptr)) return SCPE_OK;	/* check for conn */
	uptr->flags = uptr->flags | UNIT_ESTB;  }
nb = sim_read_sock (uptr->DSOCKET, msg, ((uptr->flags & UNIT_HOLD)? 1: 2));
if (nb < 0) {						/* connection closed? */
	printf ("IPL: socket read error\n");
	return SCPE_IOERR;  }
if (nb == 0) return SCPE_OK;				/* no data? */
if (uptr->flags & UNIT_HOLD) {				/* holdover byte? */
	uptr->IBUF = (ipl_hold[u] << 8) | (((int32) msg[0]) & 0377);
	uptr->flags = uptr->flags & ~UNIT_HOLD;  }
else if (nb == 1) {
	ipl_hold[u] = ((int32) msg[0]) & 0377;
	uptr->flags = uptr->flags | UNIT_HOLD;  }
else uptr->IBUF = ((((int32) msg[0]) & 0377) << 8) |
	(((int32) msg[1]) & 0377);
dev = ipl_dib[u].devno;					/* get device number */
clrCMD (dev);						/* clr cmd, set flag */
setFLG (dev);
return SCPE_OK;
}

t_bool ipl_check_conn (UNIT *uptr)
{
SOCKET sock;

if (uptr->flags & UNIT_ESTB) return TRUE;		/* established? */
if (uptr->flags & UNIT_ACTV) {				/* active connect? */
	if (sim_check_conn (uptr->DSOCKET, 0) <= 0) return FALSE;  }
else {	sock = sim_accept_conn (uptr->LSOCKET, NULL);	/* poll connect */
	if (sock == INVALID_SOCKET) return FALSE;	/* got a live one? */
	uptr->DSOCKET = sock;  }			/* save data socket */
uptr->flags = uptr->flags | UNIT_ESTB;			/* conn established */
return TRUE;
}

/* Reset routine */

t_stat ipl_reset (DEVICE *dptr)
{
DIB *dibp = (DIB *) dptr->ctxt;
UNIT *uptr = dptr->units;

hp_enbdis_pair (&ipli_dev, &iplo_dev);			/* make pair cons */
dibp->cmd = dibp->ctl = 0;				/* clear cmd, ctl */
dibp->flg = dibp->fbf = 1;				/* set flg, fbf */
uptr->IBUF = uptr->OBUF = 0;				/* clr buffers */
if (uptr->flags & UNIT_ATT) sim_activate (uptr, ipl_ptime);
else sim_cancel (uptr);					/* deactivate unit */
uptr->flags = uptr->flags & ~UNIT_HOLD;
return SCPE_OK;
}

/* Attach routine

   attach -l - listen for connection on port
   attach -c - connect to ip address and port
*/

t_stat ipl_attach (UNIT *uptr, char *cptr)
{
extern int32 sim_switches;
SOCKET newsock;
uint32 i, t, ipa, ipp, oldf;
char *tptr;
t_stat r;

r = get_ipaddr (cptr, &ipa, &ipp);
if ((r != SCPE_OK) || (ipp == 0)) return SCPE_ARG;
oldf = uptr->flags;
if (oldf & UNIT_ATT) ipl_detach (uptr);
if ((sim_switches & SWMASK ('C')) ||
    ((sim_switches & SIM_SW_REST) && (oldf & UNIT_ACTV))) {
	if (ipa == 0) ipa = 0x7F000001;
	newsock = sim_connect_sock (ipa, ipp);
	if (newsock == INVALID_SOCKET) return SCPE_IOERR;
	printf ("Connecting to IP address %d.%d.%d.%d, port %d\n",
	    (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
	    (ipa >> 8) & 0xff, ipa & 0xff, ipp);
	if (sim_log) fprintf (sim_log,
	    "Connecting to IP address %d.%d.%d.%d, port %d\n",
	    (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
	    (ipa >> 8) & 0xff, ipa & 0xff, ipp);
	uptr->flags = uptr->flags | UNIT_ACTV;
	uptr->LSOCKET = 0;
	uptr->DSOCKET = newsock;  }
else {	if (ipa != 0) return SCPE_ARG;
	newsock = sim_master_sock (ipp);
	if (newsock == INVALID_SOCKET) return SCPE_IOERR;
	printf ("Listening on port %d\n", ipp);
	if (sim_log) fprintf (sim_log, "Listening on port %d\n", ipp);
	uptr->flags = uptr->flags & ~UNIT_ACTV;
	uptr->LSOCKET = newsock;
	uptr->DSOCKET = 0;  }
uptr->IBUF = uptr->OBUF = 0;
uptr->flags = (uptr->flags | UNIT_ATT) & ~(UNIT_ESTB | UNIT_HOLD);
tptr = malloc (strlen (cptr) + 1);			/* get string buf */
if (tptr == NULL) {					/* no memory? */
	ipl_detach (uptr);				/* close sockets */
	return SCPE_MEM;  }
strcpy (tptr, cptr);					/* copy ipaddr:port */
uptr->filename = tptr;					/* save */
sim_activate (uptr, ipl_ptime);				/* activate poll */
if (sim_switches & SWMASK ('W')) {			/* wait? */
	for (i = 0; i < 30; i++) {			/* check for 30 sec */
	    if (t = ipl_check_conn (uptr)) break;	/* established? */
	    if ((i % 10) == 0)				/* status every 10 sec */
		printf ("Waiting for connnection\n");
	    sim_os_sleep (1);  }			/* sleep 1 sec */
	if (t) printf ("Connection established\n");  }
return SCPE_OK;
}

/* Detach routine */

t_stat ipl_detach (UNIT *uptr)
{
if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;	/* attached? */
if (uptr->flags & UNIT_ACTV) sim_close_sock (uptr->DSOCKET, 1);
else {	if (uptr->flags & UNIT_ESTB)			/* if established, */
	    sim_close_sock (uptr->DSOCKET, 0);		/* close data socket */
	sim_close_sock (uptr->LSOCKET, 1);  }		/* closen listen socket */
free (uptr->filename);					/* free string */
uptr->filename = NULL;
uptr->LSOCKET = 0;
uptr->DSOCKET = 0;
uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_ACTV | UNIT_ESTB);
sim_cancel (uptr);					/* don't poll */
return SCPE_OK;
}

/* Disconnect routine */

t_stat ipl_dscln (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr) return SCPE_ARG;
if (((uptr->flags & UNIT_ATT) == 0) || (uptr->flags & UNIT_ACTV) ||
    ((uptr->flags & UNIT_ESTB) == 0)) return SCPE_NOFNC;
sim_close_sock (uptr->DSOCKET, 0);
uptr->DSOCKET = 0;
uptr->flags = uptr->flags & ~UNIT_ESTB;
return SCPE_OK;
}

/* Diagnostic/normal mode routine */

t_stat ipl_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val) {
	ipli_unit.flags = ipli_unit.flags | UNIT_DIAG;
	iplo_unit.flags = iplo_unit.flags | UNIT_DIAG;  }
else {	ipli_unit.flags = ipli_unit.flags & ~UNIT_DIAG;
	iplo_unit.flags = iplo_unit.flags & ~UNIT_DIAG;  }
return SCPE_OK;
}

/* Interprocessor link bootstrap routine (HP Access Manual) */

#define LDR_BASE	073
#define IPL_PNTR	074
#define PTR_PNTR	075
#define IPL_DEVA	076
#define PTR_DEVA	077

static const int32 pboot[IBL_LNT] = {
	0163774,		/*BBL LDA ICK,I		; IPL sel code */
	0027751,		/*    JMP CFG		; go configure */
	0107700,		/*ST  CLC 0,C		; intr off */
	0002702,		/*    CLA,CCE,SZA	; skip in */		
	0063772,		/*CN  LDA M26		; feed frame */
	0002307,		/*EOC CCE,INA,SZA,RSS	; end of file? */
	0027760,		/*    JMP EOT		; yes */
	0017736,		/*    JSB READ		; get #char */
	0007307,		/*    CMB,CCE,INB,SZB,RSS ; 2's comp; null? */
	0027705,		/*    JMP EOC		; read next */
	0077770,		/*    STB WC		; word in rec */
	0017736,		/*    JSB READ		; get feed frame */
	0017736,		/*    JSB READ		; get address */
	0074000,		/*    STB 0		; init csum */
	0077771,		/*    STB AD		; save addr */
	0067771,		/*CK  LDB AD		; check addr */
	0047773,		/*    ADB MAXAD		; below loader */
	0002040,		/*    SEZ		; E =0 => OK */
	0102055,		/*    HLT 55 */
	0017736,		/*    JSB READ		; get word */
	0040001,		/*    ADA 1		; cont checksum */
	0177771,		/*    STB AD,I		; store word */
	0037771,		/*    ISZ AD */
	0000040,		/*    CLE		; force wd read */
	0037770,		/*    ISZ WC		; block done? */
	0027717,		/*    JMP CK		; no */
	0017736,		/*    JSB READ		; get checksum */
	0054000,		/*    CPB 0		; ok? */
	0027704,		/*    JMP CN		; next block */
	0102011,		/*    HLT 11		; bad csum */
	0000000,		/*RD  0 */
	0006600,		/*    CLB,CME		; E reg byte ptr */
	0103700,		/*IO1 STC RDR,C		; start reader */
	0102300,		/*IO2 SFS RDR		; wait */
	0027741,		/*    JMP *-1 */
	0106400,		/*IO3 MIB RDR		; get byte */
	0002041,		/*    SEZ,RSS		; E set? */
	0127736,		/*    JMP RD,I		; no, done */
	0005767,		/*    BLF,CLE,BLF	; shift byte */
	0027740,		/*    JMP IO1		; again */
	0163775,		/*    LDA PTR,I		; get ptr code */
	0043765,		/*CFG ADA SFS		; config IO */
	0073741,		/*    STA IO2 */
	0043766,		/*    ADA STC */
	0073740,		/*    STA IO1 */
	0043767,		/*    ADA MIB */
	0073743,		/*    STA IO3 */
	0027702,		/*    JMP ST */
	0063777,		/*EOT LDA PSC		; put select codes */
	0067776,		/*    LDB ISC		; where xloader wants */
	0102077,		/*    HLT 77 */
	0027702,		/*    JMP ST */
	0000000,		/*    NOP */
	0102300,		/*SFS SFS 0 */
	0001400,		/*STC 1400 */
	0002500,		/*MIB 2500 */
	0000000,		/*WC  0 */
	0000000,		/*AD  0 */
	0177746,		/*M26 -26 */
	0000000,		/*MAX -BBL */
	0007776,		/*ICK ISC */
	0007777,		/*PTR IPT */
	0000000,		/*ISC 0 */
	0000000			/*IPT 0 */
};

t_stat ipl_boot (int32 unitno, DEVICE *dptr)
{
int32 i, devi, devp;
extern DIB ptr_dib;
extern UNIT cpu_unit;
extern uint32 SR;
extern uint16 *M;

devi = ipli_dib.devno;					/* get device no */
devp = ptr_dib.devno;
PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;		/* start at mem top */
SR = (devi << IBL_V_DEV) | devp;			/* set SR */
for (i = 0; i < IBL_LNT; i++) M[PC + i] = pboot[i];	/* copy bootstrap */
M[PC + LDR_BASE] = (~PC + 1) & DMASK;			/* fix ups */
M[PC + IPL_PNTR] = M[PC + IPL_PNTR] | PC;
M[PC + PTR_PNTR] = M[PC + PTR_PNTR] | PC;
M[PC + IPL_DEVA] = devi;
M[PC + PTR_DEVA] = devp;
return SCPE_OK;
}

