/* pdp8_rx.c: RX8E/RX01 floppy disk simulator

   Copyright (c) 1993-1999, Robert M Supnik

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

   rx		RX8E disk controller

   An RX01 diskette consists of 77 tracks, each with 26 sectors of 128B.
   Tracks are numbered 0-76, sectors 1-26.  The RX8E can store data in
   8b mode or 12b mode.  In 8b mode, the controller reads or writes
   128 bytes per sector.  In 12b mode, the reads or writes 64 12b words
   per sector.  The 12b words are bit packed into the first 96 bytes
   of the sector; the last 32 bytes are zeroed on writes.

   14-Apr-99	RMS	Changed t_addr to unsigned
   15-Aug-96	RMS	Fixed bug in LCD
*/

#include "pdp8_defs.h"

#define RX_NUMTR	77				/* tracks/disk */
#define RX_M_TRACK	0377
#define RX_NUMSC	26				/* sectors/track */
#define RX_M_SECTOR	0177				/* cf Jones!! */
#define RX_NUMBY	128				/* bytes/sector */
#define RX_NUMWD	(RX_NUMBY / 2)			/* words/sector */
#define RX_SIZE		(RX_NUMTR * RX_NUMSC * RX_NUMBY)	/* bytes/disk */
#define RX_NUMDR	2				/* drives/controller */
#define RX_M_NUMDR	01
#define UNIT_V_WLK	(UNIT_V_UF)			/* write locked */
#define UNIT_WLK	(1 << UNIT_V_UF)

#define IDLE		0				/* idle state */
#define RWDS		1				/* rw, sect next */
#define RWDT		2				/* rw, track next */
#define FILL		3				/* fill buffer */
#define EMPTY		4				/* empty buffer */
#define CMD_COMPLETE	5				/* set done next */
#define INIT_COMPLETE	6				/* init compl next */

#define RXCS_V_FUNC	1				/* function */
#define RXCS_M_FUNC	7
#define  RXCS_FILL	0				/* fill buffer */
#define  RXCS_EMPTY	1				/* empty buffer */
#define  RXCS_WRITE	2				/* write sector */
#define  RXCS_READ	3				/* read sector */
#define  RXCS_RXES	5				/* read status */
#define  RXCS_WRDEL	6				/* write del data */
#define  RXCS_ECODE	7				/* read error code */
#define RXCS_DRV	0020				/* drive */
#define RXCS_MODE	0100				/* mode */
#define RXCS_MAINT	0200				/* maintenance */

#define RXES_CRC	0001				/* CRC error */
#define RXES_PAR	0002				/* parity error */
#define RXES_ID		0004				/* init done */
#define RXES_WLK	0010				/* write protect */
#define RXES_DD		0100				/* deleted data */
#define RXES_DRDY	0200				/* drive ready */

#define TRACK u3					/* current track */
#define READ_RXDBR ((rx_csr & RXCS_MODE)? AC | (rx_dbr & 0377): rx_dbr)
#define CALC_DA(t,s) (((t) * RX_NUMSC) + ((s) - 1)) * RX_NUMBY

extern int32 int_req, dev_done, dev_enable;
int32 rx_tr = 0;					/* xfer ready flag */
int32 rx_err = 0;					/* error flag */
int32 rx_csr = 0;					/* control/status */
int32 rx_dbr = 0;					/* data buffer */
int32 rx_esr = 0;					/* error status */
int32 rx_ecode = 0;					/* error code */
int32 rx_track = 0;					/* desired track */
int32 rx_sector = 0;					/* desired sector */
int32 rx_state = IDLE;					/* controller state */
int32 rx_cwait = 100;					/* command time */
int32 rx_swait = 10;					/* seek, per track */
int32 rx_xwait = 1;					/* tr set time */
int32 rx_stopioe = 1;					/* stop on error */
unsigned int8 buf[RX_NUMBY] = { 0 };			/* sector buffer */
int32 bufptr = 0;					/* buffer pointer */
t_stat rx_svc (UNIT *uptr);
t_stat rx_reset (DEVICE *dptr);
t_stat rx_boot (int32 unitno);
extern t_stat sim_activate (UNIT *uptr, int32 delay);
extern t_stat sim_cancel (UNIT *uptr);

/* RX8E data structures

   rx_dev	RX device descriptor
   rx_unit	RX unit list
   rx_reg	RX register list
   rx_mod	RX modifier list
*/

UNIT rx_unit[] = {
	{ UDATA (&rx_svc,
	  UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) },
	{ UDATA (&rx_svc,
	  UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) }  };

REG rx_reg[] = {
	{ ORDATA (RXCS, rx_csr, 12) },
	{ ORDATA (RXDB, rx_dbr, 12) },
	{ ORDATA (RXES, rx_esr, 8) },
	{ ORDATA (RXERR, rx_ecode, 8) },
	{ ORDATA (RXTA, rx_track, 8) },
	{ ORDATA (RXSA, rx_sector, 8) },
	{ ORDATA (STAPTR, rx_state, 3), REG_RO },
	{ ORDATA (BUFPTR, bufptr, 7)  },
	{ FLDATA (TR, rx_tr, 0) },
	{ FLDATA (ERR, rx_err, 0) },
	{ FLDATA (DONE, dev_done, INT_V_RX) },
	{ FLDATA (ENABLE, dev_enable, INT_V_RX) },
	{ FLDATA (INT, int_req, INT_V_RX) },
	{ DRDATA (CTIME, rx_cwait, 24), PV_LEFT },
	{ DRDATA (STIME, rx_swait, 24), PV_LEFT },
	{ DRDATA (XTIME, rx_xwait, 24), PV_LEFT },
	{ FLDATA (FLG0, rx_unit[0].flags, UNIT_V_WLK), REG_HRO },
	{ FLDATA (FLG1, rx_unit[1].flags, UNIT_V_WLK), REG_HRO },
	{ FLDATA (STOP_IOE, rx_stopioe, 0) },
	{ BRDATA (*BUF, buf, 8, 8, RX_NUMBY), REG_HRO },
	{ NULL }  };

MTAB rx_mod[] = {
	{ UNIT_WLK, 0, "write enabled", "ENABLED", NULL },
	{ UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
	{ 0 }  };

DEVICE rx_dev = {
	"RX", rx_unit, rx_reg, rx_mod,
	RX_NUMDR, 8, 20, 1, 8, 8,
	NULL, NULL, &rx_reset,
	&rx_boot, NULL, NULL };

/* IOT routine */

int32 rx (int32 pulse, int32 AC)
{
int32 drv;

switch (pulse) {					/* decode IR<9:11> */
case 0:							/* unused */
	return AC;
case 1:							/* LCD */
	if (rx_state != IDLE) return AC;		/* ignore if busy */
	rx_dbr = rx_csr = AC;				/* save new command */
	dev_done = dev_done & ~INT_RX;			/* clear done, int */
	int_req = int_req & ~INT_RX;
	rx_tr = rx_err = 0;				/* clear flags */
	bufptr = 0;					/* clear buf pointer */
	switch ((AC >> RXCS_V_FUNC) & RXCS_M_FUNC) {	/* decode command */
	case RXCS_FILL:
		rx_state = FILL;			/* state = fill */
		rx_tr = 1;				/* xfer is ready */
		break;
	case RXCS_EMPTY:
		rx_state = EMPTY;			/* state = empty */
		sim_activate (&rx_unit[0], rx_xwait);	/* sched xfer */
		break;
	case RXCS_READ: case RXCS_WRITE: case RXCS_WRDEL:
		rx_state = RWDS;			/* state = get sector */
		rx_tr = 1;				/* xfer is ready */
		rx_esr = rx_esr & RXES_ID;		/* clear errors */
		break;
	default:
		rx_state = CMD_COMPLETE;		/* state = cmd compl */
		drv = (rx_csr & RXCS_DRV) > 0;		/* get drive number */
		sim_activate (&rx_unit[drv], rx_cwait);	/* sched done */
		break;  }				/* end switch func */
	return 0;					/* clear AC */
case 2:							/* XDR */
	switch (rx_state & 07) {			/* case on state */
	default:					/* default */
		return READ_RXDBR;			/* return data reg */
	case EMPTY:					/* emptying buffer */
		sim_activate (&rx_unit[0], rx_xwait);	/* sched xfer */
		return READ_RXDBR;			/* return data reg */
	case RWDS:					/* sector */
		rx_sector = AC & RX_M_SECTOR;		/* save sector */
	case FILL:					/* fill */
		rx_dbr = AC;				/* save data */
		sim_activate (&rx_unit[0], rx_xwait);	/* sched xfer */
		break;
	case RWDT:					/* track */
		rx_track = AC & RX_M_TRACK;		/* save track */
		rx_dbr = AC;				/* save data */
		drv = (rx_csr & RXCS_DRV) > 0;		/* get drive number */
		sim_activate (&rx_unit[drv],		/* sched done */
			rx_swait * abs (rx_track - rx_unit[drv].TRACK));
		break;  }				/* end switch state */
	return AC;
case 3:							/* STR */
	if (rx_tr != 0) {
		rx_tr = 0;
		return IOT_SKP + AC;  }
	return AC;
case 4:							/* SER */
	if (rx_err != 0) {
		rx_err = 0;
		return IOT_SKP + AC;  }
	return AC;
case 5:							/* SDN */
	if ((dev_done & INT_RX) != 0) {
		dev_done = dev_done & ~INT_RX;
		int_req = int_req & ~INT_RX;
		return IOT_SKP + AC;  }
	return AC;
case 6:							/* INTR */
	if (AC & 1) dev_enable = dev_enable | INT_RX;
	else dev_enable = dev_enable & ~INT_RX;
	int_req = INT_UPDATE;
	return AC;
case 7:							/* INIT */
	rx_reset (&rx_dev);				/* reset device */
	return AC;  }					/* end case pulse */
}

/* Unit service; the action to be taken depends on the transfer state:

   IDLE		Should never get here, treat as unknown command
   RWDS		Just transferred sector, wait for track, set tr
   RWDT		Just transferred track, do read or write, finish command
   FILL		copy dbr to buf[bufptr], advance ptr
   		if bufptr > max, finish command, else set tr
   EMPTY	if bufptr > max, finish command, else
		copy buf[bufptr] to dbr, advance ptr, set tr
   CMD_COMPLETE	copy requested data to dbr, finish command
   INIT_COMPLETE read drive 0, track 1, sector 1 to buffer, finish command

   For RWDT and CMD_COMPLETE, the input argument is the selected drive;
   otherwise, it is drive 0.
*/

t_stat rx_svc (UNIT *uptr)
{
int32 i, func, byptr;
t_addr da;
t_stat rval;
void rx_done (int32 new_dbr, int32 new_ecode);
#define PTR12(x) (((x) + (x) + (x)) >> 1)

rval = SCPE_OK;						/* assume ok */
func = (rx_csr >> RXCS_V_FUNC) & RXCS_M_FUNC;		/* get function */
switch (rx_state) {					/* case on state */
case IDLE:						/* idle */
	rx_done (rx_esr, 0);				/* done */
	break;
case EMPTY:						/* empty buffer */
	if (rx_csr & RXCS_MODE) {			/* 8b xfer? */
		if (bufptr >= RX_NUMBY) {		/* done? */
			rx_done (rx_esr, 0);		/* set done */
			break;  }			/* and exit */
		rx_dbr = buf[bufptr];  }		/* else get data */
	else {	byptr = PTR12 (bufptr);			/* 12b xfer */
		if (bufptr >= RX_NUMWD) {		/* done? */
			rx_done (rx_esr, 0);		/* set done */
			break;  }			/* and exit */
		rx_dbr = (bufptr & 1)?			/* get data */
			((buf[byptr] & 017) << 8) | buf[byptr + 1]:
			(buf[byptr] << 4) | ((buf[byptr + 1] >> 4) & 017);  }
	bufptr = bufptr + 1;
	rx_tr = 1;
	break;
case FILL:						/* fill buffer */
	if (rx_csr & RXCS_MODE) {			/* 8b xfer? */
		buf[bufptr] = rx_dbr;			/* fill buffer */
		bufptr = bufptr + 1;
		if (bufptr < RX_NUMBY) rx_tr = 1;	/* if more, set xfer */
		else rx_done (rx_esr, 0);  }		/* else done */
	else { 	byptr = PTR12 (bufptr);			/* 12b xfer */
		if (bufptr & 1) {			/* odd or even? */
		  buf[byptr] = (buf[byptr] & 0360) | ((rx_dbr >> 8) & 017);
		  buf[byptr + 1] = rx_dbr & 0377;  }
		else {
		  buf[byptr] = (rx_dbr >> 4) & 0377;
		  buf[byptr + 1] = (rx_dbr & 017) << 4;  }
		bufptr = bufptr + 1;
		if (bufptr < RX_NUMWD) rx_tr = 1;	/* if more, set xfer */
		else {	for (i = PTR12 (RX_NUMWD); i < RX_NUMBY; i++)
				buf[i] = 0;		/* else fill sector */
			rx_done (rx_esr, 0);  }  }	/* set done */
	break;
case RWDS:						/* wait for sector */
	rx_tr = 1;					/* set xfer ready */
	rx_state = RWDT;				/* advance state */
	break;
case RWDT:						/* wait for track */
	if (rx_track >= RX_NUMTR) {			/* bad track? */
		rx_done (rx_esr, 0040);			/* done, error */
		break;  }
	uptr -> TRACK = rx_track;			/* now on track */
	if ((rx_sector == 0) || (rx_sector > RX_NUMSC)) {	/* bad sect? */
		rx_done (rx_esr, 0070);			/* done, error */
		break;  }
	if ((uptr -> flags & UNIT_BUF) == 0) {		/* not buffered? */
		rx_done (rx_esr, 0110);			/* done, error */
		rval = SCPE_UNATT;			/* return error */
		break;  }
	da = CALC_DA (rx_track, rx_sector);		/* get disk address */
	if (func == RXCS_WRDEL) rx_esr = rx_esr | RXES_DD;	/* del data? */
	if (func == RXCS_READ) {			/* read? */
		for (i = 0; i < RX_NUMBY; i++)
			buf[i] = *(((int8 *) uptr -> filebuf) + da + i);  }
	else {	if (uptr -> flags & UNIT_WLK) {		/* write and locked? */
			rx_esr = rx_esr | RXES_WLK;	/* flag error */
			rx_done (rx_esr, 0100);		/* done, error */
			break;  }
		for (i = 0; i < RX_NUMBY; i++)		/* write */
			*(((int8 *) uptr -> filebuf) + da + i) = buf[i];
		da = da + RX_NUMBY;
		if (da > uptr -> hwmark) uptr -> hwmark = da;  }
	rx_done (rx_esr, 0);				/* done */
	break;
case CMD_COMPLETE:					/* command complete */
	if (func == RXCS_ECODE) rx_done (rx_ecode, 0);
	else if (uptr -> flags & UNIT_ATT) rx_done (rx_esr | RXES_DRDY, 0);
	else rx_done (rx_esr, 0);
	break;
case INIT_COMPLETE:					/* init complete */
	rx_unit[0].TRACK = 1;				/* drive 0 to trk 1 */
	rx_unit[1].TRACK = 0;				/* drive 1 to trk 0 */
	if ((rx_unit[0].flags & UNIT_BUF) == 0) {	/* not buffered? */
		rx_done (rx_esr | RXES_ID, 0010);	/* init done, error */
		break;	}
	da = CALC_DA (1, 1);				/* track 1, sector 1 */
	for (i = 0; i < RX_NUMBY; i++)			/* read sector */
		buf[i] = *(((int8 *) uptr -> filebuf) + da + i);
	rx_done (rx_esr | RXES_ID | RXES_DRDY, 0);	/* set done */
	if ((rx_unit[1].flags & UNIT_ATT) == 0) rx_ecode = 0020;
	break;  }					/* end case state */
return IORETURN (rx_stopioe, rval);
}

/* Command complete.  Set done and put final value in interface register,
   return to IDLE state.
*/

void rx_done (int32 new_dbr, int32 new_ecode)
{
dev_done = dev_done | INT_RX;				/* set done */
int_req = INT_UPDATE;					/* update ints */
rx_dbr = new_dbr;					/* update buffer */
if (new_ecode != 0) {					/* test for error */
	rx_ecode = new_ecode;
	rx_err = 1;  }
rx_state = IDLE;					/* now idle */
return;
}

/* Reset routine.  The RX is one of the few devices that schedules
   an I/O transfer as part of its initialization.
*/

t_stat rx_reset (DEVICE *dptr)
{
rx_esr = rx_ecode = 0;					/* clear error */
rx_tr = rx_err = 0;					/* clear flags */
dev_done = dev_done & ~INT_RX;				/* clear done, int */
int_req = int_req & ~INT_RX;
rx_dbr = rx_csr = 0;					/* 12b mode, drive 0 */
rx_state = INIT_COMPLETE;				/* set state */
sim_cancel (&rx_unit[1]);				/* cancel drive 1 */
sim_activate (&rx_unit[0],				/* start drive 0 */
	rx_swait * abs (1 - rx_unit[0].TRACK));
return SCPE_OK;
}

/* Bootstrap routine */

#define BOOT_START 022
#define BOOT_INST 060
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {
	06755,			/* 22, SDN */
	05022,			/* 23, JMP .-1 */
	07126,			/* 24, CLL CML RTL	; read command + */
	01060,			/* 25, TAD UNIT		; unit no */
	06751,			/* 26, LCD		; load read+unit */
	07201,			/* 27, CLL IAC 		; AC = 1 */
	04053,			/* 30, JMS 053		; load sector */
	04053,			/* 31, JMS 053		; load track */
	07104,			/* 32, CLL RAL		; AC = 2 */
	06755,			/* 33, SDN */
	05054,			/* 34, JMP 54 */
	06754,			/* 35, SER */
	07450,			/* 36, SNA		; more to do? */
	07610,			/* 37, CLA SKP		; error */
	05046,			/* 40, JMP 46		; go empty */
	07402, 07402,		/* 41-45, HALT		; error */
	07402, 07402, 07402,
	06751,			/* 46, LCD		; load empty */
	04053,			/* 47, JMS 53		; get data */
	03002,			/* 50, DCA 2		; store */
	02050,			/* 51, ISZ 50		; incr store */
	05047,			/* 52, JMP 47		; loop until done */
	00000,			/* 53, 0 */
	06753,			/* 54, STR */
	05033,			/* 55, JMP 33 */
	06752,			/* 56, XDR */
	05453,			/* 57, JMP I 53 */
	07024,			/* UNIT, CML RAL	; for unit 1 */
	06030			/* 61, KCC */
};

t_stat rx_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;
extern unsigned int16 M[];

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
M[BOOT_INST] = unitno? 07024: 07004;
saved_PC = BOOT_START;
return SCPE_OK;
}
