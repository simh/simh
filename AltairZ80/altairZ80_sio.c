/*	altairZ80_sio: MITS Altair serial I/O card

   Copyright (c) 2002, Peter Schorn

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

   Except as contained in this notice, the name of Peter Schorn shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Peter Schorn.

   Based on work by Charles E Owen (c) 1997

		These functions support a simulated MITS 2SIO interface card.
		The card had two physical I/O ports which could be connected
		to any serial I/O device that would connect to a current loop,
		RS232, or TTY interface. Available baud rates were jumper
		selectable for each port from 110 to 9600.

		All I/O is via programmed I/O. Each device has a status port
		and a data port. A write to the status port can select
		some options for the device (0x03 will reset the port).
		A read of the status port gets the port status:

		+---+---+---+---+---+---+---+---+
		| X	| X | X | X | X | X | O | I |
		+---+---+---+---+---+---+---+---+

		I - A 1 in this bit position means a character has been received
				on the data port and is ready to be read.
		O - A 1 in this bit means the port is ready to receive a character
				on the data port and transmit it out over the serial line.

		A read to the data port gets the buffered character, a write
		to the data port writes the character to the device.
*/

#include <stdio.h>
#include <ctype.h>

#include "altairz80_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <time.h>

#define	UNIT_V_ANSI					(UNIT_V_UF + 0)				/* ANSI mode, strip bit 8 on output							*/
#define UNIT_ANSI						(1 << UNIT_V_ANSI)
#define	UNIT_V_UPPER				(UNIT_V_UF + 1)				/* uppper case mode															*/
#define UNIT_UPPER					(1 << UNIT_V_UPPER)
#define	UNIT_V_BS						(UNIT_V_UF + 2)				/* map delete to backspace											*/
#define UNIT_BS							(1 << UNIT_V_BS)
#define	UNIT_V_SIO_VERBOSE	(UNIT_V_UF + 3)				/* verbose mode, i.e. show error messages				*/
#define UNIT_SIO_VERBOSE		(1 << UNIT_V_SIO_VERBOSE)

#define	UNIT_V_SIMH_VERBOSE	(UNIT_V_UF + 0)				/* verbose mode for SIMH pseudo device					*/
#define UNIT_SIMH_VERBOSE		(1 << UNIT_V_SIMH_VERBOSE)
#define	UNIT_V_SIMH_TIMERON	(UNIT_V_UF + 1)				/* SIMH pseudo device timer generate interrupts	*/
#define UNIT_SIMH_TIMERON		(1 << UNIT_V_SIMH_VERBOSE)

#define Terminals				4													/* lines per mux																*/

#define BACKSPACE_CHAR	0x08											/* backspace character													*/
#define DELETE_CHAR			0x7f											/* delete character															*/
#define CONTROLZ_CHAR		0x1a											/* control Z character													*/

void resetSIOWarningFlags(void);
t_stat sio_set_verbose(UNIT *uptr, int32 value, char *cptr, void *desc);
t_stat sio_svc(UNIT *uptr);
t_stat sio_reset(DEVICE *dptr);
t_stat sio_attach(UNIT *uptr, char *cptr);
t_stat sio_detach(UNIT *uptr);
t_stat ptr_reset(DEVICE *dptr);
t_stat ptp_reset(DEVICE *dptr);
int32 nulldev	(int32 port, int32 io, int32 data);
int32 sr_dev	(int32 port, int32 io, int32 data);
int32 simh_dev(int32 port, int32 io, int32 data);
int32 sio0d		(int32 port, int32 io, int32 data);
int32 sio0s		(int32 port, int32 io, int32 data);
int32 sio1d		(int32 port, int32 io, int32 data);
int32 sio1s		(int32 port, int32 io, int32 data);
void reset_sio_terminals(int32 useDefault);
t_stat simh_dev_reset(void);
t_stat simh_svc(UNIT *uptr);
t_stat simh_dev_set_timeron(void);
t_stat simh_dev_set_timeroff(void);
int32 simh_in(void);
int32 simh_out(int32 data);
void attachCPM(UNIT *uptr);
void setClockZSDOS(void);
void setClockCPM3(void);
time_t mkCPM3Origin(void);
int32 toBCD(int32 x);
int32 fromBCD(int32 x);
void printMessage(void);

extern t_stat sim_activate(UNIT *uptr, int32 interval);
extern t_stat sim_cancel(UNIT *uptr);
extern t_stat sim_poll_kbd(void);
extern t_stat sim_putchar(int32 out);
extern t_stat attach_unit(UNIT *uptr, char *cptr);
extern t_bool rtc_avail;
extern FILE *sim_log;
extern int32 PCX;
extern int32 sim_switches;
extern uint32 sim_os_msec(void);
extern const char *scp_error_messages[];
extern int32 SR;
extern int32 bankSelect;
extern int32 common;
extern uint8 GetBYTEWrapper(register uint32 Addr);
extern UNIT cpu_unit;

/* the following variables define state for the SIMH pseudo device */
/* ZSDOS clock definitions */
int32 ClockZSDOSDelta					= 0;			/* delta between real clock and Altair clock											*/
int32 setClockZSDOSPos				= 0;			/* determines state for receiving address of parameter block			*/
int32 setClockZSDOSAdr				= 0;			/* address in M of 6 byte parameter block for setting time				*/
int32 getClockZSDOSPos				= 0;			/* determines state for sending clock information									*/

/* CPM3 clock definitions */
int32 ClockCPM3Delta					= 0;			/* delta between real clock and Altair clock											*/
int32 setClockCPM3Pos					= 0;			/* determines state for receiving address of parameter block			*/
int32 setClockCPM3Adr					= 0;			/* address in M of 5 byte parameter block for setting time				*/
int32 getClockCPM3Pos					= 0;			/* determines state for sending clock information									*/
int32 daysCPM3SinceOrg				= 0;			/* days since 1 Jan 1978																					*/

int32 timerDelta							= 100;		/* interrupt every 100 ms																					*/
int32 tmpTimerDelta;
uint32 timeOfNextInterrupt;
int32 timerInterrupt					= FALSE;	/* timer interrupt pending																				*/
int32 timerInterruptHandler		= 0x0fc00;/* default address of interrupt handling routine									*/
int32 tmpTimerInterruptHandler;
int32 setTimerDeltaPos				= 0;			/* determines state for receiving timerDelta											*/
int32 setTimerInterruptAdrPos	= 0;			/* determines state for receiving timerInterruptHandler						*/
int32 markTimeSP							= 0;			/* stack pointer for timer stack																	*/
int32 versionPos							= 0;			/* determines state for sending device identifier									*/
int32 lastCPMStatus						= 0;			/* result of last attachCPM command																*/
int32 lastCommand							= 0;			/* most recent command processed on port 0xfeh										*/
int32 getCommonPos						= 0;			/* determines state for sending the 'common' register							*/
int32 warnLevelSIO						= 3;			/* display at most 'warnLevelSIO' times the same warning					*/
int32 warnUnattachedPTP				= 0;			/* display a warning message if < warnLevel and SIO set to
																					VERBOSE and output to PTP without an attached file							*/
int32 warnUnattachedPTR				= 0;			/* display a warning message if < warnLevel and SIO set to
																					VERBOSE and attempt to read from PTR without an attached file		*/
int32 warnPTREOF							= 0;			/* display a warning message if < warnLevel and SIO set to
																					VERBOSE and attempt to read from PTR past EOF										*/
int32 warnUnassignedPort			= 0;			/* display a warning message if < warnLevel and SIO set to
																					VERBOSE andattempt to perform IN or OUT on an unassigned PORT		*/
char messageBuffer[256];

void printMessage(void) {
	printf(messageBuffer);
	if (sim_log) {
		fprintf(sim_log, messageBuffer);
	}
}



/* 2SIO Standard I/O Data Structures */
struct sio_terminal {
	int32 data;						/* data for this terminal									*/
	int32 status;					/* status information for this terminal		*/
	int32 statusPort;			/* status port of this terminal						*/
	int32 dataPort;				/* data port of this terminal							*/
	int32 defaultStatus;	/* default status value for this terminal	*/
};

typedef struct sio_terminal SIO_TERMINAL;

SIO_TERMINAL sio_terminals[Terminals] = {	{0, 0, 0x10, 0x11, 0x02},
																					{0, 0, 0x14, 0x15, 0x00},
																					{0, 0, 0x16, 0x17, 0x00},
																					{0, 0, 0x18, 0x19, 0x00} };
TMLN TerminalLines[Terminals] = { {0} };	/* four terminals				*/
TMXR altairTMXR = {Terminals, 0, 0 };	/* mux descriptor				*/

UNIT sio_unit = { UDATA (&sio_svc, UNIT_ATTABLE, 0), KBD_POLL_WAIT };

REG sio_reg[] = {
	{ HRDATA (DATA0,		sio_terminals[0].data,		8)	},
	{ HRDATA (STAT0,		sio_terminals[0].status,	8)	},
	{ HRDATA (DATA1,		sio_terminals[1].data,		8)	},
	{ HRDATA (STAT1,		sio_terminals[1].status,	8)	},
	{ HRDATA (DATA2,		sio_terminals[2].data,		8)	},
	{ HRDATA (STAT2,		sio_terminals[2].status,	8)	},
	{ HRDATA (DATA3,		sio_terminals[3].data,		8)	},
	{ HRDATA (STAT3,		sio_terminals[3].status,	8)	},
	{ DRDATA (SIOWL,		warnLevelSIO,							32)	},
	{ DRDATA (WUPTP,		warnUnattachedPTP,				32)	},
	{ DRDATA (WUPTR,		warnUnattachedPTR,				32)	},
	{ DRDATA (WPTREOF,	warnPTREOF,								32)	},
	{ DRDATA (WUPORT,		warnUnassignedPort,				32)	},
	{ NULL } };

MTAB sio_mod[] = {
	{ UNIT_ANSI,	0,					"TTY",			"TTY",			NULL },	/* keep bit 8 as is for output						*/
	{ UNIT_ANSI,	UNIT_ANSI,	"ANSI",			"ANSI",			NULL },	/* set bit 8 to 0 before output						*/
	{ UNIT_UPPER,	0,					"ALL",			"ALL",			NULL },	/* do not change case of input characters	*/
	{ UNIT_UPPER,	UNIT_UPPER,	"UPPER",		"UPPER",		NULL },	/* change input characters to upper case	*/
	{ UNIT_BS,		0,					"BS",				"BS",				NULL },	/* map delete to backspace								*/
	{ UNIT_BS,		UNIT_BS,		"DEL",			"DEL",			NULL },	/* map backspace to delete								*/
	{ UNIT_SIO_VERBOSE,	0,		"QUIET",		"QUIET",		NULL },	/* quiet, no error messages								*/
	{ UNIT_SIO_VERBOSE,	UNIT_SIO_VERBOSE,	"VERBOSE",	"VERBOSE",	&sio_set_verbose },
																														/* verbose, display warning messages			*/
	{ 0 } };

DEVICE sio_dev = {
	"SIO", &sio_unit, sio_reg, sio_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &sio_reset,
	NULL, &sio_attach, &sio_detach, NULL, 0 };

UNIT ptr_unit = { UDATA (NULL, UNIT_SEQ + UNIT_ATTABLE + UNIT_ROABLE, 0),
	KBD_POLL_WAIT };

REG ptr_reg[] = {
	{ HRDATA (DATA,	ptr_unit.buf,	8)	},
	{ HRDATA (STAT,	ptr_unit.u3,	8)	},
	{ DRDATA (POS,	ptr_unit.pos,	31)	},
	{ NULL } };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	NULL, NULL, NULL, NULL, 0 };

UNIT ptp_unit = { UDATA (NULL, UNIT_SEQ + UNIT_ATTABLE, 0),
	KBD_POLL_WAIT };

REG ptp_reg[] = {
	{ HRDATA (DATA,	ptp_unit.buf,	8)	},
	{ HRDATA (STAT,	ptp_unit.u3,	8)	},
	{ DRDATA (POS,	ptp_unit.pos,	31)	},
	{ NULL } };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL, NULL, 0 };

/*	Synthetic device SIMH for communication
		between Altair and SIMH environment using port 0xfe */
UNIT simh_unit = { UDATA (&simh_svc, 0, 0), KBD_POLL_WAIT };

REG simh_reg[] = {
	{ DRDATA (CZD,		ClockZSDOSDelta,				31)						},
	{ DRDATA (SCZP,		setClockZSDOSPos,				8),		REG_RO	},
	{ DRDATA (GCZP,		getClockZSDOSPos,				8),		REG_RO	},
	{ HRDATA (SCZA,		setClockZSDOSAdr,				17),	REG_RO	},
	{ DRDATA (CC3D,		ClockCPM3Delta,					31)						},
	{ DRDATA (TIMD,		timerDelta,							31)						},
	{ DRDATA (TIMI,		timerInterrupt,					3)						},
	{ HRDATA (TIMH,		timerInterruptHandler,	17)						},
	{ DRDATA (SCC3P,	setClockCPM3Pos,				8),		REG_RO	},
	{ DRDATA (GCC3P,	getClockCPM3Pos,				8),		REG_RO	},
	{ HRDATA (SCC3A,	setClockCPM3Adr,				17),	REG_RO	},
	{ DRDATA (MTSP,		markTimeSP,							8),		REG_RO	},
	{ DRDATA (VP,			versionPos,							8),		REG_RO	},
	{ DRDATA (CP,			getCommonPos,						8),		REG_RO	},
	{ DRDATA (LC,			lastCommand,						8),		REG_RO	},
	{ NULL } };

MTAB simh_mod[] = {
	/* quiet, no warning messages					*/
	{ UNIT_SIMH_VERBOSE,	0,									"QUIET",		"QUIET",		NULL										},
	/* verbose, display warning messages	*/
	{ UNIT_SIMH_VERBOSE,	UNIT_SIMH_VERBOSE,	"VERBOSE",	"VERBOSE",	NULL										},
	/* quiet, no warning messages					*/
	{ UNIT_SIMH_TIMERON,	0,									"TIMEROFF",	"TIMEROFF",	&simh_dev_set_timeroff	},
	/* verbose, display warning messages	*/
	{ UNIT_SIMH_TIMERON,	UNIT_SIMH_TIMERON,	"TIMERON",	"TIMERON",	&simh_dev_set_timeron		},
	{ 0 } };

DEVICE simh_device = {
	"SIMH", &simh_unit, simh_reg, simh_mod,
	1, 10, 31, 1, 16, 4,
	NULL, NULL, &simh_dev_reset,
	NULL, NULL, NULL, NULL, 0 };


void resetSIOWarningFlags(void) {
	warnUnattachedPTP		= 0;
	warnUnattachedPTR		= 0;
	warnPTREOF					= 0;
	warnUnassignedPort	= 0;
}

t_stat sio_set_verbose(UNIT *uptr, int32 value, char *cptr, void *desc) {
	resetSIOWarningFlags();
	return SCPE_OK;
}

t_stat sio_attach(UNIT *uptr, char *cptr) {
	int32 i;
	for (i = 0; i < Terminals; i++) {
		altairTMXR.ldsc[i] = &TerminalLines[i];
	}
	reset_sio_terminals(FALSE);
	return tmxr_attach(&altairTMXR, uptr, cptr);	/* attach mux */
}

void reset_sio_terminals(int32 useDefault) {
	int32 i;
	for (i = 0; i < Terminals; i++) {
		sio_terminals[i].status = useDefault ? sio_terminals[i].defaultStatus : 0;	/* Status	*/
		sio_terminals[i].data		= 0x00;																							/* Data		*/
	}
}

/* Detach */
t_stat sio_detach(UNIT *uptr) {
	reset_sio_terminals(TRUE);
	return tmxr_detach(&altairTMXR, uptr);
}

/* Service routines to handle simulator functions */

/* service routine - actually gets char & places in buffer */

t_stat sio_svc(UNIT *uptr) {
	int32 temp;

	sim_activate(&sio_unit, sio_unit.wait);						/* continue poll			*/

	if (sio_unit.flags & UNIT_ATT) {
		if (sim_poll_kbd() == SCPE_STOP) {						/* listen for ^E			*/
			return SCPE_STOP;
		}
		temp = tmxr_poll_conn(&altairTMXR);							/* poll connection		*/
		if (temp >= 0) {
			altairTMXR.ldsc[temp] -> rcve = 1;						/* enable receive			*/
		}
		tmxr_poll_rx(&altairTMXR);											/* poll input					*/
		tmxr_poll_tx(&altairTMXR);											/* poll output				*/
	}
	else {
		if ((temp = sim_poll_kbd()) < SCPE_KFLAG) {
			return temp;																	/* no char or error?	*/
		}
		sio_terminals[0].data = temp & 0xff;						/* Save char					*/
		sio_terminals[0].status |= 0x01;								/* Set status					*/
	}
	return SCPE_OK;
}


/* Reset routines */

t_stat sio_reset(DEVICE *dptr) {
	int32 i;
	resetSIOWarningFlags();
	if (sio_unit.flags & UNIT_ATT) {
		for (i = 0; i < Terminals; i++) {
			if (altairTMXR.ldsc[i] -> conn > 0) {
				tmxr_reset_ln(altairTMXR.ldsc[i]);
			}
		}
		reset_sio_terminals(FALSE);
	}
	else {
		reset_sio_terminals(TRUE);
	}
	sim_activate(&sio_unit, sio_unit.wait);	/* activate unit */
	return SCPE_OK;
}

t_stat ptr_reset(DEVICE *dptr) {
	resetSIOWarningFlags();
	ptr_unit.buf	= 0;
	ptr_unit.u3		= 0;
	ptr_unit.pos	= 0;
	if (ptr_unit.flags & UNIT_ATT)	{	/* attached? */
		rewind(ptr_dev.units -> fileref);
	}
	sim_cancel(&ptp_unit);	/* deactivate unit */
	return SCPE_OK;
}

t_stat ptp_reset(DEVICE *dptr) {
	resetSIOWarningFlags();
	ptp_unit.buf = 0;
	ptp_unit.u3 = 0x02;
	sim_cancel(&ptp_unit);	/* deactivate unit */
	return SCPE_OK;
}


/*	I/O instruction handlers, called from the CPU module when an
		IN or OUT instruction is issued.

		Each function is passed an 'io' flag, where 0 means a read from
		the port, and 1 means a write to the port. On input, the actual
		input is passed as the return value, on output, 'data' is written
		to the device.

		Port 1 controls console I/O. We distinguish two cases:
		1) SIO attached to a port (i.e. Telnet console I/O)
		2) SIO not attached to a port (i.e. "regular" console I/O)
*/

int32 sio0s(int32 port, int32 io, int32 data) {
	int32 ti;
	for (ti = 0; ti < Terminals; ti++) {
		if (sio_terminals[ti].statusPort == port) {
			break;
		}
	}
	if (io == 0) { /* IN */
		if (sio_unit.flags & UNIT_ATT) {
			sio_terminals[ti].status =
				(((tmxr_rqln(altairTMXR.ldsc[ti]) > 0	? 0x01 : 0) |
				/* read possible if character available							*/
				((altairTMXR.ldsc[ti] -> conn) && (altairTMXR.ldsc[ti] -> xmte) ? 0x02 : 0x00)));
				/* write possible if connected and transmit enabled	*/
		}
		return sio_terminals[ti].status;
	}
	else { /* OUT */
		if (sio_unit.flags & UNIT_ATT) {
			if (data == 0x03) {		/* reset port! */
				sio_terminals[ti].status	= 0x00;
				sio_terminals[ti].data		= 0;
			}
		}
		else {
			if (data == 0x03) {		/* reset port! */
				sio_terminals[ti].status	= sio_terminals[ti].defaultStatus;
				sio_terminals[ti].data		= 0;
			}
		}
		return 0;	/* ignored since OUT */
	}
}

int32 sio0d(int32 port, int32 io, int32 data) {
	int32 ti;
	for (ti = 0; ti < Terminals; ti++) {
		if (sio_terminals[ti].dataPort == port) {
			break;
		}
	}
	if (io == 0) { /* IN */
		if (sio_unit.flags & UNIT_ATT) {
			sio_terminals[ti].data = tmxr_getc_ln(altairTMXR.ldsc[ti]) & 0xff;
		}
		sio_terminals[ti].status &= 0xfe;
		if (sio_unit.flags & UNIT_BS) {
			if (sio_terminals[ti].data == BACKSPACE_CHAR) {
				sio_terminals[ti].data = DELETE_CHAR;
			}
		}
		else {
			if (sio_terminals[ti].data == DELETE_CHAR) {
				sio_terminals[ti].data = BACKSPACE_CHAR;
			}
		}
		return (sio_unit.flags & UNIT_UPPER) ? toupper(sio_terminals[ti].data) : sio_terminals[ti].data;
	}
	else { /* OUT */
		if (sio_unit.flags & UNIT_ANSI) {
			data &= 0x7f;
		}
		if (sio_unit.flags & UNIT_ATT) {
			tmxr_putc_ln(altairTMXR.ldsc[ti], data);
		}
		else {
			sim_putchar(data);
		}
		return 0;	/* ignored since OUT */
	}
}

/* Port 2 controls the PTR/PTP devices */

int32 sio1s(int32 port, int32 io, int32 data) {
	if (io == 0) {
		/* reset I bit iff PTR unit not attached or no more data available.	*/
		/* O bit is always set since write always possible.									*/
		if ((ptr_unit.flags & UNIT_ATT) == 0) {
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnattachedPTR < warnLevelSIO)) {
				warnUnattachedPTR++;
/*06*/	message1("Attempt to test status of unattached PTR. 0x02 returned.\n");
			}
			return 0x02;
		}
		return (ptr_unit.u3 != 0) ? 0x02 : 0x03;
	}
	else { /* OUT */
		if (data == 0x03) {
			ptr_unit.u3		= 0;
			ptr_unit.buf	= 0;
			ptr_unit.pos	= 0;
			ptp_unit.u3		= 0;
			ptp_unit.buf	= 0;
			ptp_unit.pos	= 0;
		}
		return 0;	/* ignored since OUT */
	}
}

int32 sio1d(int32 port, int32 io, int32 data) {
	int32 temp;
	if (io == 0) {	/* IN */
		if (ptr_unit.u3) { /* no more data available */
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnPTREOF < warnLevelSIO)) {
				warnPTREOF++;
/*07*/	message1("PTR attempted to read past EOF. 0x00 returned.\n");
			}
			return 0;
		}
		if ((ptr_unit.flags & UNIT_ATT) == 0) { /* not attached */
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnattachedPTR < warnLevelSIO)) {
				warnUnattachedPTR++;
/*08*/	message1("Attempt to read from unattached PTR. 0x00 returned.\n");
			}
			return 0;
		}
		if ((temp = getc(ptr_dev.units -> fileref)) == EOF) {	/* end of file? */
			ptr_unit.u3 = 0x01;
			return CONTROLZ_CHAR;	/* control Z denotes end of text file in CP/M */
		}
		ptr_unit.pos++;
		return temp & 0xff;
	}
	else { /* OUT */
		if (ptp_unit.flags & UNIT_ATT) {	/* unit must be attached	*/
			putc(data, ptp_dev.units -> fileref);
		}																	/* else ignore data				*/
		else if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnattachedPTP < warnLevelSIO)) {
			warnUnattachedPTP++;
/*09*/message2("Attempt to output '0x%02x' to unattached PTP - ignored.\n", data);
		}
		ptp_unit.pos++;
		return 0;	/* ignored since OUT */
	}
}

int32 nulldev(int32 port, int32 io, int32 data) {
	if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnassignedPort < warnLevelSIO)) {
		warnUnassignedPort++;
		if (io == 0) {
			message2("Unassigned IN(%2xh) - ignored.\n", port);
		}
		else {
			message3("Unassigned OUT(%2xh) -> %2xh - ignored.\n", port, data);
		}
	}
	return io == 0 ? 0xff : 0;
}

int32 sr_dev(int32 port, int32 io, int32 data) {
	return io == 0 ? SR : 0;
}

int32 toBCD(int32 x) {
	return (x / 10) * 16 + (x % 10);
}

int32 fromBCD(int32 x) {
	return 10 * ((0xf0 & x) >> 4) + (0x0f & x);
}

/*	Z80 or 8080 programs communicate with the SIMH pseudo device via port 0xfe.
		The principles are as follows:

	1) For commands that do not require parameters and do not return results
		ld	a,<cmd>
		out	(0feh),a
		Special case is the reset command which needs to be send 128 times to make
		sure that the internal state is properly reset.

	2) For commands that require parameters and do not return results
		ld	a,<cmd>
		out	(0feh),a
		ld	a,<p1>
		out	(0feh),a
		ld	a,<p2>
		out	(0feh),a
		...
		Note: The calling program must send all parameter bytes. Otherwise
		the pseudo device is left in an unexpected state.

	3) For commands that do not require parameters and return results
		ld	a,<cmd>
		out	(0feh),a
		in	a,(0feh)	; <A> contains first byte of result
		in	a,(0feh)	; <A> contains second byte of result
		...
		Note: The calling program must request all bytes of the result. Otherwise
		the pseudo device is left in an unexpected state.

	4) Commands requiring parameters and returning results do not exist currently.

*/

#define splimit									10
#define printTimeCmd						0		/* print the current time in milliseconds															*/
#define startTimerCmd						1		/* start a new timer on the top of the timer stack										*/
#define stopTimerCmd						2		/* stop timer on top of timer stack and show time difference					*/
#define resetPTRCmd							3		/* reset the PTR device																								*/
#define attachPTRCmd						4		/* attach the PTR device																							*/
#define detachPTRCmd						5		/* detach the PTR device																							*/
#define getSIMHVersionCMD				6		/* get the current version of the SIMH pseudo device									*/
#define getClockZSDOSCmd				7		/* get the current time in ZSDOS format																*/
#define setClockZSDOSCmd				8		/* set the current time in ZSDOS format																*/
#define getClockCPM3Cmd					9		/* get the current time in CP/M 3 format															*/
#define setClockCPM3Cmd					10	/* set the current time in CP/M 3 format															*/
#define	getBankSelectCmd				11	/* get the selected bank																							*/
#define	setBankSelectCmd				12	/* set the selected bank																							*/
#define getCommonCmd						13	/* get the base address of the common memory segment									*/
#define resetSIMHInterfaceCmd		14	/* reset the SIMH pseudo device																				*/
#define showTimerCmd						15	/* show time difference to timer on top of stack											*/
#define attachPTPCmd						16	/* attach PTP to the file with name at beginning of CP/M command line	*/
#define detachPTPCmd						17	/* detach PTP																													*/
#define hasBankedMemoryCmd			18	/* determines whether machine has banked memory												*/
#define setZ80CPUCmd						19	/* set the CPU to a Z80																								*/
#define set8080CPUCmd						20	/* set the CPU to an 8080																							*/
#define startTimerInterruptsCmd	21	/* statr timer interrupts																							*/
#define stopTimerInterruptsCmd	22	/* stop timer interrupts																							*/
#define setTimerDeltaCmd				23	/* set the timer interval	in which interrupts occur										*/
#define setTimerInterruptAdrCmd	24	/* set the address to call by timer interrupts												*/
#define cpmCommandLineLength		128
struct tm *currentTime = NULL;
uint32 markTime[splimit];
char version[] = "SIMH002";

t_stat simh_dev_reset(void) {
	currentTime							= NULL;
	ClockZSDOSDelta					= 0;
	setClockZSDOSPos				= 0;
	getClockZSDOSPos				= 0;
	ClockCPM3Delta					= 0;
	setClockCPM3Pos					= 0;
	getClockCPM3Pos					= 0;
	getCommonPos						= 0;
	setTimerDeltaPos				= 0;
	setTimerInterruptAdrPos = 0;
	markTimeSP							= 0;
	versionPos							= 0;
	lastCommand							= 0;
	lastCPMStatus						= SCPE_OK;
	timerInterrupt					= FALSE;
	if (simh_unit.flags & UNIT_SIMH_TIMERON) {
		simh_dev_set_timeron();
	}
	return SCPE_OK;
}

t_stat simh_dev_set_timeron(void) {
	if (rtc_avail) {
		timeOfNextInterrupt = sim_os_msec() + timerDelta;
		return sim_activate(&simh_unit, simh_unit.wait);	/* activate unit */
	}
	else {
		printf("Sorry - no real time clock available.\n");
		return SCPE_ARG;
	}
}

t_stat simh_dev_set_timeroff(void) {
	timerInterrupt = FALSE;
	sim_cancel(&simh_unit);
	return SCPE_OK;
}

t_stat simh_svc(UNIT *uptr) {
	uint32 n = sim_os_msec();
	if (n >= timeOfNextInterrupt) {
		timerInterrupt = TRUE;
		timeOfNextInterrupt += timerDelta;
		if (n >= timeOfNextInterrupt) { /* time of next interrupt is not in the future */
			timeOfNextInterrupt = n + timerDelta; /* make sure it is in the future! */
		}
	}
	if (simh_unit.flags & UNIT_SIMH_TIMERON) {
		sim_activate(&simh_unit, simh_unit.wait);	/* activate unit */
	}
	return SCPE_OK;
}

/* The CP/M commandline is used as the name of a file and UNIT* uptr is attached to it */
void attachCPM(UNIT *uptr) {
	char cpmCommandLine[cpmCommandLineLength];
	uint32 i, len = (GetBYTEWrapper(0x80) & 0x7f) - 1; /* 0x80 contains length of commandline, discard first char */
	for (i = 0; i < len; i++) {
		cpmCommandLine[i] = (char)GetBYTEWrapper(0x82 + i); /* the first char, typically ' ', is discarded */
	}
	cpmCommandLine[i] = 0; /* make C string */
	if (uptr == &ptr_unit) {
		sim_switches = SWMASK('R');
	}
	else if (uptr == &ptp_unit) {
		sim_switches = SWMASK('W');
	}
	lastCPMStatus = attach_unit(uptr, cpmCommandLine);
	if ((lastCPMStatus != SCPE_OK) && (simh_unit.flags & UNIT_SIMH_VERBOSE)) {
		message3("Cannot open '%s' (%s).\n", cpmCommandLine, scp_error_messages[lastCPMStatus - SCPE_BASE]);
	}
}

/* setClockZSDOSAdr points to 6 byte block in M: YY MM DD HH MM SS in BCD notation */
void setClockZSDOS(void) {
	struct tm newTime;
	int32 year = fromBCD(GetBYTEWrapper(setClockZSDOSAdr));
	newTime.tm_year	= year < 50 ? year + 100 : year;
	newTime.tm_mon	= fromBCD(GetBYTEWrapper(setClockZSDOSAdr + 1)) - 1;
	newTime.tm_mday	= fromBCD(GetBYTEWrapper(setClockZSDOSAdr + 2));
	newTime.tm_hour	= fromBCD(GetBYTEWrapper(setClockZSDOSAdr + 3));
	newTime.tm_min	= fromBCD(GetBYTEWrapper(setClockZSDOSAdr + 4));
	newTime.tm_sec	= fromBCD(GetBYTEWrapper(setClockZSDOSAdr + 5));
	ClockZSDOSDelta = mktime(&newTime) - time(NULL);
}

#define secondsPerMinute	60
#define secondsPerHour		(60 * secondsPerMinute)
#define	secondsPerDay			(24 * secondsPerHour)
time_t mkCPM3Origin(void) {
	struct tm date;
	date.tm_year	= 77;
	date.tm_mon		= 11;
	date.tm_mday	= 31;
	date.tm_hour	= 0;
	date.tm_min		= 0;
	date.tm_sec		= 0;
	return mktime(&date);
}

/* setClockCPM3Adr points to 5 byte block in M:
	0 - 1 int16:		days since 31 Dec 77
			2 BCD byte:	HH
			3 BCD byte:	MM
			4 BCD byte:	SS																*/
void setClockCPM3(void) {
	ClockCPM3Delta = mkCPM3Origin()																																	+
		(GetBYTEWrapper(setClockCPM3Adr) + GetBYTEWrapper(setClockCPM3Adr + 1) * 256) * secondsPerDay	+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 2)) * secondsPerHour																	+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 3)) * secondsPerMinute																+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 4)) - time(NULL);
}

int32 simh_in(void) {
	int32 result;
	switch(lastCommand) {
		case attachPTRCmd:
		case attachPTPCmd:
			result = lastCPMStatus;
			break;
		case getClockZSDOSCmd:
			if (currentTime) {
				switch(getClockZSDOSPos) {
					case 0:
						result = toBCD(currentTime -> tm_year > 99 ?
							currentTime -> tm_year - 100 : currentTime -> tm_year);
						break;
					case 1:		result = toBCD(currentTime -> tm_mon + 1);	break;
					case 2:		result = toBCD(currentTime -> tm_mday);			break;
					case 3:		result = toBCD(currentTime -> tm_hour);			break;
					case 4:		result = toBCD(currentTime -> tm_min);			break;
					case 5:		result = toBCD(currentTime -> tm_sec);			break;
					default:	result = 0;
				}
				getClockZSDOSPos++;
			}
			else {
				result = 0;
			}
			break;
		case getClockCPM3Cmd:
			if (currentTime) {
				switch(getClockCPM3Pos) {
					case 0:		result = daysCPM3SinceOrg & 0xff;					break;
					case 1:		result = (daysCPM3SinceOrg >> 8) & 0xff;	break;
					case 2:		result = toBCD(currentTime -> tm_hour);		break;
					case 3:		result = toBCD(currentTime -> tm_min);		break;
					case 4:		result = toBCD(currentTime -> tm_sec);		break;
					default:	result = 0;
				}
				getClockCPM3Pos++;
			}
			else {
				result = 0;
			}
			break;
		case getSIMHVersionCMD:
			result = version[versionPos++];
			if (result == 0) {
				versionPos = 0;
			}
			break;
		case getBankSelectCmd:
			if (cpu_unit.flags & UNIT_BANKED) {
				result = bankSelect;
			}
			else {
				result = 0;
				if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
					message1("Get selected bank ignored for non-banked memory.");
				}
			}
			break;
		case getCommonCmd:
			if (getCommonPos == 0) {
				result = common & 0xff;
				getCommonPos = 1;
			}
			else {
				result = (common >> 8) & 0xff;
				getCommonPos = 0;
			}
			break;
		case hasBankedMemoryCmd:
			result = cpu_unit.flags & UNIT_BANKED ? MAXBANKS : 0;
			break;
		default:
			result = 0;
	}
	return result;
}

int32 simh_out(int32 data) {
	uint32 delta;
	time_t now;
	switch(lastCommand) {
		case setClockZSDOSCmd:
			switch(setClockZSDOSPos) {
				case 0:
					setClockZSDOSAdr = data;
					setClockZSDOSPos++;
					break;
				case 1:
					setClockZSDOSAdr += (data << 8);
					setClockZSDOS();
					lastCommand = 0;
					break;
				default:;
			}
			break;
		case setClockCPM3Cmd:
			switch(setClockCPM3Pos) {
				case 0:
					setClockCPM3Adr = data;
					setClockCPM3Pos++;
					break;
				case 1:
					setClockCPM3Adr += (data << 8);
					setClockCPM3();
					lastCommand = 0;
					break;
				default:;
			}
			break;
		case setBankSelectCmd:
			if (cpu_unit.flags & UNIT_BANKED) {
				bankSelect = data & BANKMASK;
			}
			else if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
				message2("Set selected bank to %i ignored for non-banked memory.", data & 3);
			}
			lastCommand = 0;
			break;
		case setTimerDeltaCmd:
			switch(setTimerDeltaPos) {
				case 0:
					tmpTimerDelta = data;
					setTimerDeltaPos++;
					break;
				case 1:
					timerDelta = tmpTimerDelta + (data << 8);
					lastCommand = 0;
					break;
				default:;
			}
			break;
		case setTimerInterruptAdrCmd:
			switch(setTimerInterruptAdrPos) {
				case 0:
					tmpTimerInterruptHandler = data;
					setTimerInterruptAdrPos++;
					break;
				case 1:
					timerInterruptHandler = tmpTimerInterruptHandler + (data << 8);
					lastCommand = 0;
					break;
				default:;
			}
			break;
		default:
			lastCommand = data;
			switch(data) {
				case printTimeCmd:	/* print time */
					if (rtc_avail) {
						message2("Current time in milliseconds = %d.\n", sim_os_msec());
					}
					break;
				case startTimerCmd:		/* create a new timer on top of stack */
					if (rtc_avail) {
						if (markTimeSP < splimit) {
							markTime[markTimeSP++] = sim_os_msec();
						}
						else {
							message1("Timer stack overflow.\n");
						}
					}
					break;
				case stopTimerCmd:		/* stop timer on top of stack and show time difference */
					if (rtc_avail) {
						if (markTimeSP > 0) {
							delta = sim_os_msec() - markTime[--markTimeSP];
							message2("Timer stopped. Elapsed time in milliseconds = %d.\n", delta);
						}
						else {
							message1("No timer active.\n");
						}
					}
					break;
				case resetPTRCmd:		/* reset ptr device */
					ptr_reset(NULL);
					break;
				case attachPTRCmd:	/* attach ptr to the file with name at beginning of CP/M command line */
					attachCPM(&ptr_unit);
					break;
				case detachPTRCmd:	/* detach ptr */
					detach_unit(&ptr_unit);
					break;
				case getSIMHVersionCMD:
					versionPos = 0;
					break;
				case getClockZSDOSCmd:
					time(&now);
					now += ClockZSDOSDelta;
					currentTime = localtime(&now);
					getClockZSDOSPos = 0;
					break;
				case setClockZSDOSCmd:
					setClockZSDOSPos = 0;
					break;
				case getClockCPM3Cmd:
					time(&now);
					now += ClockCPM3Delta;
					currentTime = localtime(&now);
					daysCPM3SinceOrg = (now - mkCPM3Origin()) / secondsPerDay;
					getClockCPM3Pos = 0;
					break;
				case setClockCPM3Cmd:
					setClockCPM3Pos = 0;
					break;
				case getBankSelectCmd:
					break;
				case setBankSelectCmd:
					break;
				case getCommonCmd:
					break;
				case resetSIMHInterfaceCmd:
					markTimeSP	= 0;
					lastCommand	= 0;
					break;
				case showTimerCmd:		/* show time difference to timer on top of stack */
					if (rtc_avail) {
						if (markTimeSP > 0) {
							delta = sim_os_msec() - markTime[markTimeSP - 1];
							message2("Timer running. Elapsed in milliseconds = %d.\n", delta);
						}
						else {
							message1("No timer active.\n");
						}
					}
					break;
				case attachPTPCmd:	/* attach ptp to the file with name at beginning of CP/M command line */
					attachCPM(&ptp_unit);
					break;
				case detachPTPCmd:	/* detach ptp */
					detach_unit(&ptp_unit);
					break;
				case hasBankedMemoryCmd:
					break;
				case setZ80CPUCmd:
					cpu_unit.flags |= UNIT_CHIP;
					break;
				case set8080CPUCmd:
					cpu_unit.flags &= ~UNIT_CHIP;
					break;
				case startTimerInterruptsCmd:
					if (simh_dev_set_timeron() == SCPE_OK) {
						timerInterrupt = FALSE;
						simh_unit.flags |= UNIT_SIMH_TIMERON;
					}
					break;
				case stopTimerInterruptsCmd:
					simh_unit.flags &= ~UNIT_SIMH_TIMERON;
					simh_dev_set_timeroff();
					break;
				case setTimerDeltaCmd:
					setTimerDeltaPos = 0;
					break;
				case setTimerInterruptAdrCmd:
					setTimerInterruptAdrPos = 0;
					break;
				default:
					if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
						message2("Unknown command (%i) to SIMH pseudo device ignored.\n", data);
					}
			}
	}
	return 0; /* ignored, since OUT */
}

/* port 0xfe is a device for communication SIMH <--> Altair machine */
int32 simh_dev(int32 port, int32 io, int32 data) {
	return io == 0 ? simh_in() : simh_out(data);
}
