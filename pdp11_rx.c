/* pdp11_rx.c: RX11/RX01 floppy disk simulator

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

   rx		RX11 disk controller

   14-Apr-99	RMS	Changed t_addr to unsigned

   An RX01 diskette consists of 77 tracks, each with 26 sectors of 128B.
   Tracks are numbered 0-76, sectors 1-26.
*/

#include "pdp11_defs.h"

#define RX_NUMTR	77				/* tracks/disk */
#define RX_M_TRACK	0377
#define RX_NUMSC	26				/* sectors/track */
#define RX_M_SECTOR	0177
#define RX_NUMBY	128				/* bytes/sector */
#define RX_SIZE		(RX_NUMTR * RX_NUMSC * RX_NUMBY)	/* bytes/disk */
#define RX_NUMDR	2				/* drives/controller */
#define RX_M_NUMDR	01
#define UNIT_V_WLK	(UNIT_V_UF)			/* write locked */
#define UNIT_WLK	(1u << UNIT_V_UF)

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
#define RXCS_V_DRV	4				/* drive select */
#define RXCS_V_DONE	5				/* done */
#define RXCS_V_TR	7				/* xfer request */
#define RXCS_V_INIT	14				/* init */
#define RXCS_FUNC	(RXCS_M_FUNC << RXCS_V_FUNC)
#define RXCS_DRV	(1u << RXCS_V_DRV)
#define RXCS_DONE	(1u << RXCS_V_DONE)
#define RXCS_TR		(1u << RXCS_V_TR)
#define RXCS_INIT	(1u << RXCS_V_INIT)
#define RXCS_ROUT	(CSR_ERR+RXCS_TR+CSR_IE+RXCS_DONE)
#define RXCS_IMP	(RXCS_ROUT+RXCS_DRV+RXCS_FUNC)
#define RXCS_RW		(CSR_IE)			/* read/write */

#define RXES_CRC	0001				/* CRC error */
#define RXES_PAR	0002				/* parity error */
#define RXES_ID		0004				/* init done */
#define RXES_WLK	0010				/* write protect */
#define RXES_DD		0100				/* deleted data */
#define RXES_DRDY	0200				/* drive ready */

#define TRACK u3					/* current track */
#define CALC_DA(t,s) (((t) * RX_NUMSC) + ((s) - 1)) * RX_NUMBY

extern int32 int_req;
int32 rx_csr = 0;					/* control/status */
int32 rx_dbr = 0;					/* data buffer */
int32 rx_esr = 0;					/* error status */
int32 rx_ecode = 0;					/* error code */
int32 rx_track = 0;					/* desired track */
int32 rx_sector = 0;					/* desired sector */
int32 rx_state = IDLE;					/* controller state */
int32 rx_stopioe = 1;					/* stop on error */
int32 rx_cwait = 100;					/* command time */
int32 rx_swait = 10;					/* seek, per track */
int32 rx_xwait = 1;					/* tr set time */
unsigned int8 buf[RX_NUMBY] = { 0 };			/* sector buffer */
int32 bptr = 0;						/* buffer pointer */
t_stat rx_svc (UNIT *uptr);
t_stat rx_reset (DEVICE *dptr);
t_stat rx_boot (int32 unitno);
extern t_stat sim_activate (UNIT *uptr, int32 delay);
extern t_stat sim_cancel (UNIT *uptr);

/* RX11 data structures

   rx_dev	RX device descriptor
   rx_unit	RX unit list
   rx_reg	RX register list
   rx_mod	RX modifier list
*/

UNIT rx_unit[] = {
	{ UDATA (&rx_svc,
	  UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) },
	{ UDATA (&rx_svc,
	  UNIT_FIX+UNIT_ATTABLE+UNIT_BUFABLE+UNIT_MUSTBUF, RX_SIZE) } };

REG rx_reg[] = {
	{ ORDATA (RXCS, rx_csr, 16) },
	{ ORDATA (RXDB, rx_dbr, 8) },
	{ ORDATA (RXES, rx_esr, 8) },
	{ ORDATA (RXERR, rx_ecode, 8) },
	{ ORDATA (RXTA, rx_track, 8) },
	{ ORDATA (RXSA, rx_sector, 8) },
	{ ORDATA (STAPTR, rx_state, 3), REG_RO },
	{ ORDATA (BUFPTR, bptr, 7)  },
	{ FLDATA (INT, int_req, INT_V_RX) },
	{ FLDATA (ERR, rx_csr, CSR_V_ERR) },
	{ FLDATA (TR, rx_csr, RXCS_V_TR) },
	{ FLDATA (IE, rx_csr, CSR_V_IE) },
	{ FLDATA (DONE, rx_csr, RXCS_V_DONE) },
	{ DRDATA (CTIME, rx_cwait, 24), PV_LEFT },
	{ DRDATA (STIME, rx_swait, 24), PV_LEFT },
	{ DRDATA (XTIME, rx_xwait, 24), PV_LEFT },
	{ FLDATA (FLG0, rx_unit[0].flags, UNIT_V_WLK), REG_HRO },
	{ FLDATA (FLG1, rx_unit[1].flags, UNIT_V_WLK), REG_HRO },
	{ FLDATA (STOP_IOE, rx_stopioe, 0) },
	{ BRDATA (**BUF, buf, 8, 8, RX_NUMBY), REG_HRO },
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

/* I/O dispatch routine, I/O addresses 17777170 - 17777172

   17777170		floppy CSR
   17777172		floppy data register
*/

t_stat rx_rd (int32 *data, int32 PA, int32 access)
{
switch ((PA >> 1) & 1) {				/* decode PA<1> */
case 0:							/* RXCS */
	rx_csr = rx_csr & RXCS_IMP;			/* clear junk */
	*data = rx_csr & RXCS_ROUT;
	return SCPE_OK;
case 1:							/* RXDB */
	if (rx_state == EMPTY) {			/* empty? */
		sim_activate (&rx_unit[0], rx_xwait);
		rx_csr = rx_csr & ~RXCS_TR;  }		/* clear xfer */
	*data = rx_dbr;					/* return data */
	return SCPE_OK;  }				/* end switch PA */
}

t_stat rx_wr (int32 data, int32 PA, int32 access)
{
int32 drv;

switch ((PA >> 1) & 1) {				/* decode PA<1> */

/* Writing RXCS, three cases:
   1. Writing INIT, reset device
   2. Idle and writing new function
	- clear error, done, transfer ready, int req
	- save int enable, function, drive
	- start new function
   3. Otherwise, write IE and update interrupts
*/

case 0:							/* RXCS */
	rx_csr = rx_csr & RXCS_IMP;			/* clear junk */
	if (access == WRITEB) data = (PA & 1)?		/* write byte? */
		(rx_csr & 0377) | (data << 8): (rx_csr & ~0377) | data;
	if (data & RXCS_INIT) {				/* initialize? */
		rx_reset (&rx_dev);			/* reset device */
		return SCPE_OK;  }			/* end if init */
	if ((data & CSR_GO) && (rx_state == IDLE)) {	/* new function? */
		rx_csr = data & (CSR_IE + RXCS_DRV + RXCS_FUNC);
		bptr = 0;				/* clear buf pointer */
		switch ((data >> RXCS_V_FUNC) & RXCS_M_FUNC) {
		case RXCS_FILL:
			rx_state = FILL;		/* state = fill */
			rx_csr = rx_csr | RXCS_TR;	/* xfer is ready */
			break;
		case RXCS_EMPTY:
			rx_state = EMPTY;		/* state = empty */
			sim_activate (&rx_unit[0], rx_xwait);
			break;
		case RXCS_READ: case RXCS_WRITE: case RXCS_WRDEL:
			rx_state = RWDS;		/* state = get sector */
			rx_csr = rx_csr | RXCS_TR;	/* xfer is ready */
			rx_esr = rx_esr & RXES_ID;	/* clear errors */
			break;
		default:
			rx_state = CMD_COMPLETE;	/* state = cmd compl */
			drv = (data & RXCS_DRV) > 0;	/* get drive number */
			sim_activate (&rx_unit[drv], rx_cwait);
			break;  }			/* end switch func */
		return SCPE_OK;  }			/* end if GO */
	if ((data & CSR_IE) == 0) int_req = int_req & ~INT_RX;
	else if ((rx_csr & (RXCS_DONE + CSR_IE)) == RXCS_DONE)
		int_req = int_req | INT_RX;
	rx_csr = (rx_csr & ~RXCS_RW) | (data & RXCS_RW);
	return SCPE_OK;					/* end case RXCS */

/* Accessing RXDB, two cases:
   1. Write idle, write
   2. Write not idle and TR set, state dependent
*/

case 1:							/* RXDB */
	if ((PA & 1) || ((rx_state != IDLE) && ((rx_csr & RXCS_TR) == 0)))
		return SCPE_OK;				/* if ~IDLE, need tr */
	rx_dbr = data & 0377;				/* save data */
	if ((rx_state == FILL) || (rx_state == RWDS)) {	/* fill or sector? */
		sim_activate (&rx_unit[0], rx_xwait);	/* sched event */
		rx_csr = rx_csr & ~RXCS_TR;  }		/* clear xfer */
	if (rx_state == RWDT) {				/* track? */
		drv = (rx_csr & RXCS_DRV) > 0;		/* get drive number */
		sim_activate (&rx_unit[drv],		/* sched done */
			rx_swait * abs (rx_track - rx_unit[drv].TRACK));
		rx_csr = rx_csr & ~RXCS_TR;  }		/* clear xfer */
	return SCPE_OK;					/* end case RXDB */
	}						/* end switch PA */
}

/* Unit service; the action to be taken depends on the transfer state:

   IDLE		Should never get here, treat as unknown command
   RWDS		Just transferred sector, wait for track, set tr
   RWDT		Just transferred track, do read or write, finish command
   FILL		copy ir to buf[bptr], advance ptr
		if bptr > max, finish command, else set tr
   EMPTY	if bptr > max, finish command, else
		copy buf[bptr] to ir, advance ptr, set tr
   CMD_COMPLETE	copy requested data to ir, finish command
   INIT_COMPLETE read drive 0, track 1, sector 1 to buffer, finish command

   For RWDT and CMD_COMPLETE, the input argument is the selected drive;
   otherwise, it is drive 0.
*/

t_stat rx_svc (UNIT *uptr)
{
int32 i, func;
t_addr da;
t_stat rval;
void rx_done (int new_dbr, int new_ecode);

rval = SCPE_OK;						/* assume ok */
func = (rx_csr >> RXCS_V_FUNC) & RXCS_M_FUNC;		/* get function */
switch (rx_state) {					/* case on state */
case IDLE:						/* idle */
	rx_done (rx_esr, 0);				/* done */
	break;
case EMPTY:						/* empty buffer */
	if (bptr >= RX_NUMBY) rx_done (rx_esr, 0);	/* done all? */
	else {	rx_dbr = buf[bptr];			/* get next */
		bptr = bptr + 1;
		rx_csr = rx_csr | RXCS_TR;  }		/* set xfer */
	break;
case FILL:						/* fill buffer */
	buf[bptr] = rx_dbr;				/* write next */
	bptr = bptr + 1;
	if (bptr < RX_NUMBY) rx_csr = rx_csr | RXCS_TR;	/* if more, set xfer */
	else rx_done (rx_esr, 0);			/* else done */
	break;
case RWDS:						/* wait for sector */
	rx_sector = rx_dbr & RX_M_SECTOR;		/* save sector */
	rx_csr = rx_csr | RXCS_TR;			/* set xfer */
	rx_state = RWDT;				/* advance state */
	break;
case RWDT:						/* wait for track */
	rx_track = rx_dbr & RX_M_TRACK;			/* save track */
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
   request interrupt if needed, return to IDLE state.
*/

void rx_done (int32 new_dbr, int32 new_ecode)
{
rx_csr = rx_csr | RXCS_DONE;				/* set done */
if (rx_csr & CSR_IE) int_req = int_req | INT_RX;	/* if ie, intr */
rx_dbr = new_dbr;					/* update RXDB */
if (new_ecode != 0) {					/* test for error */
	rx_ecode = new_ecode;
	rx_csr = rx_csr | CSR_ERR;  }
rx_state = IDLE;					/* now idle */
return;
}

/* Device initialization.  The RX is one of the few devices that schedules
   an I/O transfer as part of its initialization.
*/

t_stat rx_reset (DEVICE *dptr)
{
rx_csr = rx_dbr = 0;					/* clear regs */
rx_esr = rx_ecode = 0;					/* clear error */
rx_state = INIT_COMPLETE;				/* set state */
int_req = int_req & ~INT_RX;				/* clear int req */
sim_cancel (&rx_unit[1]);				/* cancel drive 1 */
sim_activate (&rx_unit[0],				/* start drive 0 */
	rx_swait * abs (1 - rx_unit[0].TRACK));
return SCPE_OK;
}

/* Device bootstrap */

#define BOOT_START 02000
#define BOOT_UNIT 02006
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
	0012706, 0002000,		/* MOV #2000, SP */
	0012700, 0000000,		/* MOV #unit, R0	; unit number */
	0010003,			/* MOV R0, R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0006303,			/* ASL R3 */
	0012701, 0177170,		/* MOV #RXCS, R1	; csr */
	0032711, 0000040,		/* BITB #40, (R1)	; ready? */
	0001775,			/* BEQ .-4 */
	0052703, 0000007,		/* BIS #READ+GO, R3 */
	0010311,			/* MOV R3, (R1)		; read & go */
	0105711,			/* TSTB (R1)		; xfr ready? */
	0100376,			/* BPL .-2 */
	0012761, 0000001, 0000002,	/* MOV #1, 2(R1)	; sector */
	0105711,			/* TSTB (R1)		; xfr ready? */
	0100376,			/* BPL .-2 */
	0012761, 0000001, 0000002,	/* MOV #1, 2(R1)	; track */
	0005003,			/* CLR R3 */
	0032711, 0000040,		/* BITB #40, (R1)	; ready? */
	0001775,			/* BEQ .-4 */
	0012711, 0000003,		/* MOV #EMPTY+GO, (R1)	; empty & go */
	0105711,			/* TSTB (R1)		; xfr, done? */
	0001776,			/* BEQ .-2 */
	0100003,			/* BPL .+010 */
	0116123, 0000002,		/* MOVB 2(R1), (R3)+	; move byte */
	0000772,			/* BR .-012 */
	0005002,			/* CLR R2 */
	0005003,			/* CLR R3 */
	0005004,			/* CLR R4 */
	0012705, 0062170,		/* MOV #"DX, R5 */
	0005007				/* CLR R7 */
};

t_stat rx_boot (int32 unitno)
{
int32 i;
extern int32 saved_PC;
extern unsigned short *M;

for (i = 0; i < BOOT_LEN; i++) M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & RX_M_NUMDR;
saved_PC = BOOT_START;
return SCPE_OK;
}
