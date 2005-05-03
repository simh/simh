/*	altairz80_sio: MITS Altair serial I/O card

		Copyright (c) 2002-2005, Peter Schorn

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
		PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
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

#include <ctype.h>

#include "altairz80_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <time.h>

#define	UNIT_V_ANSI					(UNIT_V_UF + 0)				/* ANSI mode, strip bit 8 on output							*/
#define UNIT_ANSI						(1 << UNIT_V_ANSI)
#define	UNIT_V_UPPER				(UNIT_V_UF + 1)				/* upper case mode															*/
#define UNIT_UPPER					(1 << UNIT_V_UPPER)
#define	UNIT_V_BS						(UNIT_V_UF + 2)				/* map delete to backspace											*/
#define UNIT_BS							(1 << UNIT_V_BS)
#define	UNIT_V_SIO_VERBOSE	(UNIT_V_UF + 3)				/* verbose mode, i.e. show error messages				*/
#define UNIT_SIO_VERBOSE		(1 << UNIT_V_SIO_VERBOSE)
#define	UNIT_V_MAP					(UNIT_V_UF + 4)				/* mapping mode on															*/
#define UNIT_MAP						(1 << UNIT_V_MAP)

#define	UNIT_V_SIMH_VERBOSE	(UNIT_V_UF + 0)				/* verbose mode for SIMH pseudo device					*/
#define UNIT_SIMH_VERBOSE		(1 << UNIT_V_SIMH_VERBOSE)
#define	UNIT_V_SIMH_TIMERON	(UNIT_V_UF + 1)				/* SIMH pseudo device timer generate interrupts	*/
#define UNIT_SIMH_TIMERON		(1 << UNIT_V_SIMH_VERBOSE)

#define Terminals				4													/* lines per mux																*/

#define BACKSPACE_CHAR	0x08											/* backspace character													*/
#define DELETE_CHAR			0x7f											/* delete character															*/
#define CONTROLZ_CHAR		0x1a											/* control Z character													*/

static void resetSIOWarningFlags(void);
static t_stat sio_set_verbose				(UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat simh_dev_set_timeron	(UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat simh_dev_set_timeroff	(UNIT *uptr, int32 value, char *cptr, void *desc);
static t_stat sio_svc(UNIT *uptr);
static t_stat sio_reset(DEVICE *dptr);
static t_stat sio_attach(UNIT *uptr, char *cptr);
static t_stat sio_detach(UNIT *uptr);
static t_stat ptr_reset(DEVICE *dptr);
static t_stat ptp_reset(DEVICE *dptr);
int32 nulldev	(const int32 port, const int32 io, const int32 data);
int32 sr_dev	(const int32 port, const int32 io, const int32 data);
int32 simh_dev(const int32 port, const int32 io, const int32 data);
int32 sio0d		(const int32 port, const int32 io, const int32 data);
int32 sio0s		(const int32 port, const int32 io, const int32 data);
int32 sio1d		(const int32 port, const int32 io, const int32 data);
int32 sio1s		(const int32 port, const int32 io, const int32 data);
static void reset_sio_terminals(const int32 useDefault);
static t_stat simh_dev_reset(DEVICE *dptr);
static t_stat simh_svc(UNIT *uptr);
static int32 simh_in(const int32 port);
static int32 simh_out(const int32 port, const int32 data);
static void attachCPM(UNIT *uptr);
static void setClockZSDOS(void);
static void setClockCPM3(void);
static time_t mkCPM3Origin(void);
static int32 toBCD(const int32 x);
static int32 fromBCD(const int32 x);
void printMessage(void);
static void warnNoRealTimeClock(void);

extern t_stat sim_activate(UNIT *uptr, int32 interval);
extern t_stat sim_cancel(UNIT *uptr);
extern t_stat sim_poll_kbd(void);
extern t_stat sim_putchar(int32 out);
extern t_stat attach_unit(UNIT *uptr, char *cptr);
extern int32 getBankSelect(void);
extern void setBankSelect(int32 b);
extern uint32 getCommon(void);
extern t_bool rtc_avail;
extern FILE *sim_log;
extern int32 PCX;
extern int32 sim_switches;
extern uint32 sim_os_msec(void);
extern const char *scp_error_messages[];
extern int32 SR;
extern uint8 GetBYTEWrapper(register uint32 Addr);
extern UNIT cpu_unit;

/* SIMH pseudo device status registers																																					*/
/* ZSDOS clock definitions																																											*/
static time_t ClockZSDOSDelta				= 0;			/* delta between real clock and Altair clock											*/
static int32 setClockZSDOSPos				= 0;			/* determines state for receiving address of parameter block			*/
static int32 setClockZSDOSAdr				= 0;			/* address in M of 6 byte parameter block for setting time				*/
static int32 getClockZSDOSPos				= 0;			/* determines state for sending clock information									*/

/* CPM3 clock definitions																																												*/
static time_t ClockCPM3Delta				= 0;			/* delta between real clock and Altair clock											*/
static int32 setClockCPM3Pos				= 0;			/* determines state for receiving address of parameter block			*/
static int32 setClockCPM3Adr				= 0;			/* address in M of 5 byte parameter block for setting time				*/
static int32 getClockCPM3Pos				= 0;			/* determines state for sending clock information									*/
static int32 daysCPM3SinceOrg				= 0;			/* days since 1 Jan 1978																					*/

/* interrupt related																																														*/
static uint32 timeOfNextInterrupt;						/* time when next interrupt is scheduled													*/
       int32 timerInterrupt					= FALSE;	/* timer interrupt pending																				*/
       int32 timerInterruptHandler	= 0x0fc00;/* default address of interrupt handling routine									*/
static int32 setTimerInterruptAdrPos= 0;			/* determines state for receiving timerInterruptHandler						*/
static int32 timerDelta							= 100;		/* interrupt every 100 ms																					*/
static int32 setTimerDeltaPos				= 0;			/* determines state for receiving timerDelta											*/

/* stop watch and timer related																																									*/
static uint32 stopWatchDelta				= 0;			/* stores elapsed time of stop watch															*/
static int32 getStopWatchDeltaPos		= 0;			/* determines the state for receiving stopWatchDelta							*/
static uint32 stopWatchNow					= 0;			/* stores starting time of stop watch															*/
static int32 markTimeSP							= 0;			/* stack pointer for timer stack																	*/

/* miscellaneous																																																*/
static int32 versionPos							= 0;			/* determines state for sending device identifier									*/
static int32 lastCPMStatus					= 0;			/* result of last attachCPM command																*/
static int32 lastCommand						= 0;			/* most recent command processed on port 0xfeh										*/
static int32 getCommonPos						= 0;			/* determines state for sending the 'common' register							*/

/* SIO status registers																																													*/
static int32 warnLevelSIO						= 3;			/* display at most 'warnLevelSIO' times the same warning					*/
static int32 warnUnattachedPTP			= 0;			/* display a warning message if < warnLevel and SIO set to
																								VERBOSE and output to PTP without an attached file							*/
static int32 warnUnattachedPTR			= 0;			/* display a warning message if < warnLevel and SIO set to
																								VERBOSE and attempt to read from PTR without an attached file		*/
static int32 warnPTREOF							= 0;			/* display a warning message if < warnLevel and SIO set to
																								VERBOSE and attempt to read from PTR past EOF										*/
static int32 warnUnassignedPort			= 0;			/* display a warning message if < warnLevel and SIO set to
																								VERBOSE and attempt to perform IN or OUT on an unassigned PORT		*/
struct sio_terminal {
	int32 data;						/* data for this terminal									*/
	int32 status;					/* status information for this terminal		*/
	int32 statusPort;			/* status port of this terminal						*/
	int32 dataPort;				/* data port of this terminal							*/
	int32 defaultStatus;	/* default status value for this terminal	*/
};

typedef struct sio_terminal SIO_TERMINAL;

static SIO_TERMINAL sio_terminals[Terminals] =
	{	{0, 0, 0x10, 0x11, 0x02},
		{0, 0, 0x14, 0x15, 0x00},
		{0, 0, 0x16, 0x17, 0x00},
		{0, 0, 0x18, 0x19, 0x00} };
static TMLN TerminalLines[Terminals] = { {0} };	/* four terminals				*/
static TMXR altairTMXR = {Terminals, 0, 0, TerminalLines};		/* mux descriptor				*/

static UNIT sio_unit = { UDATA (&sio_svc, UNIT_ATTABLE + UNIT_MAP, 0), KBD_POLL_WAIT };

static REG sio_reg[] = {
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

static MTAB sio_mod[] = {
	{ UNIT_ANSI,	0,					"TTY",			"TTY",			NULL },	/* keep bit 8 as is for output						*/
	{ UNIT_ANSI,	UNIT_ANSI,	"ANSI",			"ANSI",			NULL },	/* set bit 8 to 0 before output						*/
	{ UNIT_UPPER,	0,					"ALL",			"ALL",			NULL },	/* do not change case of input characters	*/
	{ UNIT_UPPER,	UNIT_UPPER,	"UPPER",		"UPPER",		NULL },	/* change input characters to upper case	*/
	{ UNIT_BS,		0,					"BS",				"BS",				NULL },	/* map delete to backspace								*/
	{ UNIT_BS,		UNIT_BS,		"DEL",			"DEL",			NULL },	/* map backspace to delete								*/
	{ UNIT_SIO_VERBOSE,	0,		"QUIET",		"QUIET",		NULL },	/* quiet, no error messages								*/
	{ UNIT_SIO_VERBOSE,	UNIT_SIO_VERBOSE,	"VERBOSE",	"VERBOSE",	&sio_set_verbose },
																														/* verbose, display warning messages			*/
	{ UNIT_MAP,		0,					"NOMAP",		"NOMAP",		NULL },	/*  disable character mapping							*/
	{ UNIT_MAP,		UNIT_MAP,		"MAP",			"MAP",			NULL },	/*  enable all character mapping					*/
	{ 0 } };

DEVICE sio_dev = {
	"SIO", &sio_unit, sio_reg, sio_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &sio_reset,
	NULL, &sio_attach, &sio_detach,
	NULL, 0, 0,
	NULL, NULL, NULL };

static UNIT ptr_unit = { UDATA (NULL, UNIT_SEQ + UNIT_ATTABLE + UNIT_ROABLE, 0),
	KBD_POLL_WAIT };

static REG ptr_reg[] = {
	{ HRDATA (DATA,	ptr_unit.buf,	8)	},
	{ HRDATA (STAT,	ptr_unit.u3,	8)	},
	{ DRDATA (POS,	ptr_unit.pos,	32)	},
	{ NULL } };

DEVICE ptr_dev = {
	"PTR", &ptr_unit, ptr_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptr_reset,
	NULL, NULL, NULL,
	NULL, 0, 0,
	NULL, NULL, NULL };

static UNIT ptp_unit = { UDATA (NULL, UNIT_SEQ + UNIT_ATTABLE, 0),
	KBD_POLL_WAIT };

static REG ptp_reg[] = {
	{ HRDATA (DATA,	ptp_unit.buf,	8)	},
	{ HRDATA (STAT,	ptp_unit.u3,	8)	},
	{ DRDATA (POS,	ptp_unit.pos,	32)	},
	{ NULL } };

DEVICE ptp_dev = {
	"PTP", &ptp_unit, ptp_reg, NULL,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &ptp_reset,
	NULL, NULL, NULL,
	NULL, 0, 0,
	NULL, NULL, NULL };

/*	Synthetic device SIMH for communication
		between Altair and SIMH environment using port 0xfe */
static UNIT simh_unit = { UDATA (&simh_svc, 0, 0), KBD_POLL_WAIT };

static REG simh_reg[] = {
	{ DRDATA (CZD,		ClockZSDOSDelta,				32)						},
	{ DRDATA (SCZP,		setClockZSDOSPos,				8),		REG_RO	},
	{ HRDATA (SCZA,		setClockZSDOSAdr,				16),	REG_RO	},
	{ DRDATA (GCZP,		getClockZSDOSPos,				8),		REG_RO	},
	
	{ DRDATA (CC3D,		ClockCPM3Delta,					32)						},
	{ DRDATA (SC3DP,	setClockCPM3Pos,				8),		REG_RO	},
	{ HRDATA (SC3DA,	setClockCPM3Adr,				16),	REG_RO	},
	{ DRDATA (GC3DP,	getClockCPM3Pos,				8),		REG_RO	},
	{ DRDATA (D3DO,		daysCPM3SinceOrg,				32),	REG_RO	},
	
	{ DRDATA (TOFNI,	timeOfNextInterrupt,		32),	REG_RO	},
	{ DRDATA (TIMI,		timerInterrupt,					3)						},
	{ HRDATA (TIMH,		timerInterruptHandler,	16)						},
	{ DRDATA (STIAP,	setTimerInterruptAdrPos,8),		REG_RO	},
	{ DRDATA (TIMD,		timerDelta,							32)						},
	{ DRDATA (STDP,		setTimerDeltaPos,				8),		REG_RO	},
	
	{ DRDATA (STPDT,	stopWatchDelta,					32),	REG_RO	},
	{ DRDATA (STPOS,	getStopWatchDeltaPos,		8),		REG_RO	},
	{ DRDATA (STPNW,	stopWatchNow,						32),	REG_RO	},
	{ DRDATA (MTSP,		markTimeSP,							8),		REG_RO	},
	
	{ DRDATA (VPOS,		versionPos,							8),		REG_RO	},
	{ DRDATA (LCPMS,	lastCPMStatus,					8),		REG_RO	},
	{ DRDATA (LCMD,		lastCommand,						8),		REG_RO	},
	{ DRDATA (CPOS,		getCommonPos,						8),		REG_RO	},
	{ NULL } };

static MTAB simh_mod[] = {
	/* quiet, no warning messages					*/
	{ UNIT_SIMH_VERBOSE,	0,									"QUIET",		"QUIET",		NULL										},
	/* verbose, display warning messages	*/
	{ UNIT_SIMH_VERBOSE,	UNIT_SIMH_VERBOSE,	"VERBOSE",	"VERBOSE",	NULL										},
	/* timer generated interrupts are off	*/
	{ UNIT_SIMH_TIMERON,	0,									"TIMEROFF",	"TIMEROFF",	&simh_dev_set_timeroff	},
	/* timer generated interrupts are on	*/
	{ UNIT_SIMH_TIMERON,	UNIT_SIMH_TIMERON,	"TIMERON",	"TIMERON",	&simh_dev_set_timeron		},
	{ 0 } };

DEVICE simh_device = {
	"SIMH", &simh_unit, simh_reg, simh_mod,
	1, 10, 31, 1, 16, 4,
	NULL, NULL, &simh_dev_reset,
	NULL, NULL, NULL,
	NULL, 0, 0,
	NULL, NULL, NULL };

char messageBuffer[256];

void printMessage(void) {
	printf(messageBuffer);
#if defined(__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD__) || defined (__APPLE__)
/* need to make sure that carriage return is executed - ttrunstate() of scp_tty.c
	has disabled \n translation */
	printf("\r\n");
#else
	printf("\n");
#endif
	if (sim_log) {
		fprintf(sim_log, messageBuffer);
		fprintf(sim_log,"\n");
	}
}

static void resetSIOWarningFlags(void) {
	warnUnattachedPTP		= 0;
	warnUnattachedPTR		= 0;
	warnPTREOF					= 0;
	warnUnassignedPort	= 0;
}

static t_stat sio_set_verbose(UNIT *uptr, int32 value, char *cptr, void *desc) {
	resetSIOWarningFlags();
	return SCPE_OK;
}

static t_stat sio_attach(UNIT *uptr, char *cptr) {
	reset_sio_terminals(FALSE);
	return tmxr_attach(&altairTMXR, uptr, cptr);	/* attach mux */
}

static void reset_sio_terminals(const int32 useDefault) {
	int32 i;
	for (i = 0; i < Terminals; i++) {
		sio_terminals[i].status = useDefault ? sio_terminals[i].defaultStatus : 0;	/* status	*/
		sio_terminals[i].data		= 0x00;																							/* data		*/
	}
}

/* detach */
static t_stat sio_detach(UNIT *uptr) {
	reset_sio_terminals(TRUE);
	return tmxr_detach(&altairTMXR, uptr);
}

/* service routines to handle simulator functions */

/* service routine - actually gets char & places in buffer */

static t_stat sio_svc(UNIT *uptr) {
	int32 temp;

	sim_activate(&sio_unit, sio_unit.wait);						/* continue poll			*/

	if (sio_unit.flags & UNIT_ATT) {
		if (sim_poll_kbd() == SCPE_STOP) {							/* listen for ^E			*/
			return SCPE_STOP;
		}
		temp = tmxr_poll_conn(&altairTMXR);							/* poll connection		*/
		if (temp >= 0) {
			TerminalLines[temp].rcve = 1;									/* enable receive			*/
		}
		tmxr_poll_rx(&altairTMXR);											/* poll input					*/
		tmxr_poll_tx(&altairTMXR);											/* poll output				*/
	}
	else {
		if ((temp = sim_poll_kbd()) < SCPE_KFLAG) {
			return temp;																	/* no char or error?	*/
		}
		sio_terminals[0].data = temp & 0xff;						/* save character			*/
		sio_terminals[0].status |= 0x01;								/* set status					*/
	}
	return SCPE_OK;
}


/* reset routines */

static t_stat sio_reset(DEVICE *dptr) {
	int32 i;
	resetSIOWarningFlags();
	if (sio_unit.flags & UNIT_ATT) {
		for (i = 0; i < Terminals; i++) {
			if (TerminalLines[i].conn > 0) {
				tmxr_reset_ln(&TerminalLines[i]);
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

static t_stat ptr_reset(DEVICE *dptr) {
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

static t_stat ptp_reset(DEVICE *dptr) {
	resetSIOWarningFlags();
	ptp_unit.buf	= 0;
	ptp_unit.u3		= 0x02;
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

int32 sio0s(const int32 port, const int32 io, const int32 data) {
	int32 ti;
	for (ti = 0; ti < Terminals; ti++) {
		if (sio_terminals[ti].statusPort == port) {
			break;
		}
	}
	if (io == 0) { /* IN */
		if (sio_unit.flags & UNIT_ATT) {
			sio_terminals[ti].status =
				(((tmxr_rqln(&TerminalLines[ti]) > 0	? 0x01 : 0) |
				/* read possible if character available							*/
				((TerminalLines[ti].conn) && (TerminalLines[ti].xmte) ? 0x02 : 0x00)));
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

int32 sio0d(const int32 port, const int32 io, const int32 data) {
	int32 ti;
	for (ti = 0; ti < Terminals; ti++) {
		if (sio_terminals[ti].dataPort == port) {
			break;
		}
	}
	if (io == 0) { /* IN */
		if (sio_unit.flags & UNIT_ATT) {
			sio_terminals[ti].data = tmxr_getc_ln(&TerminalLines[ti]) & 0xff;
		}
		sio_terminals[ti].status &= 0xfe;
		if (sio_unit.flags & UNIT_MAP) {
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
		}
		return ((sio_unit.flags & UNIT_UPPER) && (sio_unit.flags & UNIT_MAP)) ?
			toupper(sio_terminals[ti].data) : sio_terminals[ti].data;
	}
	else { /* OUT */
		int32 d = sio_unit.flags & UNIT_ANSI ? data & 0x7f : data;
		if (sio_unit.flags & UNIT_ATT) {
			tmxr_putc_ln(&TerminalLines[ti], d); /* status ignored */
		}
		else {
			sim_putchar(d);
		}
		return 0;	/* ignored since OUT */
	}
}

/* port 2 controls the PTR/PTP devices */

int32 sio1s(const int32 port, const int32 io, const int32 data) {
	if (io == 0) {
		/* reset I bit iff PTR unit not attached or no more data available.	*/
		/* O bit is always set since write always possible.									*/
		if ((ptr_unit.flags & UNIT_ATT) == 0) {
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnattachedPTR < warnLevelSIO)) {
				warnUnattachedPTR++;
/*06*/	message1("Attempt to test status of unattached PTR. 0x02 returned.");
			}
			return 0x02;
		}
		return ptr_unit.u3 ? 0x02 : 0x03;
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

int32 sio1d(const int32 port, const int32 io, const int32 data) {
	int32 temp;
	if (io == 0) {	/* IN */
		if (ptr_unit.u3) { /* no more data available */
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnPTREOF < warnLevelSIO)) {
				warnPTREOF++;
/*07*/	message1("PTR attempted to read past EOF. 0x00 returned.");
			}
			return 0;
		}
		if ((ptr_unit.flags & UNIT_ATT) == 0) { /* not attached */
			if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnattachedPTR < warnLevelSIO)) {
				warnUnattachedPTR++;
/*08*/	message1("Attempt to read from unattached PTR. 0x00 returned.");
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
/*09*/message2("Attempt to output '0x%02x' to unattached PTP - ignored.", data);
		}
		ptp_unit.pos++;
		return 0;	/* ignored since OUT */
	}
}

int32 nulldev(const int32 port, const int32 io, const int32 data) {
	if ((sio_unit.flags & UNIT_SIO_VERBOSE) && (warnUnassignedPort < warnLevelSIO)) {
		warnUnassignedPort++;
		if (io == 0) {
			message2("Unassigned IN(%2xh) - ignored.", port);
		}
		else {
			message3("Unassigned OUT(%2xh) -> %2xh - ignored.", port, data);
		}
	}
	return io == 0 ? 0xff : 0;
}

int32 sr_dev(const int32 port, const int32 io, const int32 data) {
	return io == 0 ? SR : 0;
}

static int32 toBCD(const int32 x) {
	return (x / 10) * 16 + (x % 10);
}

static int32 fromBCD(const int32 x) {
	return 10 * ((0xf0 & x) >> 4) + (0x0f & x);
}

/*		Z80 or 8080 programs communicate with the SIMH pseudo device via port 0xfe.
			The following principles apply:

	1)	For commands that do not require parameters and do not return results
			ld	a,<cmd>
			out	(0feh),a
			Special case is the reset command which needs to be send 128 times to make
			sure that the internal state is properly reset.

	2)	For commands that require parameters and do not return results
			ld	a,<cmd>
			out	(0feh),a
			ld	a,<p1>
			out	(0feh),a
			ld	a,<p2>
			out	(0feh),a
			...
			Note: The calling program must send all parameter bytes. Otherwise
			the pseudo device is left in an unexpected state.

	3)	For commands that do not require parameters and return results
			ld	a,<cmd>
			out	(0feh),a
			in	a,(0feh)	; <A> contains first byte of result
			in	a,(0feh)	; <A> contains second byte of result
			...
			Note: The calling program must request all bytes of the result. Otherwise
			the pseudo device is left in an unexpected state.

	4)	Commands requiring parameters and returning results do not exist currently.

*/

enum simhPseudoDeviceCommands { /* do not change order or remove commands, add only at the end		*/
	printTimeCmd,						/*  0 print the current time in milliseconds														*/
	startTimerCmd,					/*  1 start a new timer on the top of the timer stack										*/
	stopTimerCmd,						/*  2 stop timer on top of timer stack and show time difference					*/
	resetPTRCmd,						/*  3 reset the PTR device																							*/
	attachPTRCmd,						/*  4 attach the PTR device																							*/
	detachPTRCmd,						/*  5 detach the PTR device																							*/
	getSIMHVersionCmd,			/*  6 get the current version of the SIMH pseudo device									*/
	getClockZSDOSCmd,				/*  7 get the current time in ZSDOS format															*/
	setClockZSDOSCmd,				/*  8 set the current time in ZSDOS format															*/
	getClockCPM3Cmd,				/*  9 get the current time in CP/M 3 format															*/
	setClockCPM3Cmd,				/* 10 set the current time in CP/M 3 format															*/
	getBankSelectCmd,				/* 11 get the selected bank																							*/
	setBankSelectCmd,				/* 12 set the selected bank																							*/
	getCommonCmd,						/* 13 get the base address of the common memory segment									*/
	resetSIMHInterfaceCmd,	/* 14 reset the SIMH pseudo device																			*/
	showTimerCmd,						/* 15 show time difference to timer on top of stack											*/
	attachPTPCmd,						/* 16 attach PTP to the file with name at beginning of CP/M command line*/
	detachPTPCmd,						/* 17 detach PTP																												*/
	hasBankedMemoryCmd,			/* 18 determines whether machine has banked memory											*/
	setZ80CPUCmd,						/* 19 set the CPU to a Z80																							*/
	set8080CPUCmd,					/* 20 set the CPU to an 8080																						*/
	startTimerInterruptsCmd,/* 21 start timer interrupts																						*/
	stopTimerInterruptsCmd,	/* 22 stop timer interrupts																							*/
	setTimerDeltaCmd,				/* 23 set the timer interval	in which interrupts occur									*/
	setTimerInterruptAdrCmd,/* 24 set the address to call by timer interrupts												*/
	resetStopWatchCmd,			/* 25 reset the millisecond stop watch																	*/
	readStopWatchCmd				/* 26 read the millisecond stop watch																		*/
};

#define cpmCommandLineLength	128
#define splimit								10	/* stack depth of timer stack	*/
static uint32 markTime[splimit];	/* timer stack								*/
static struct tm currentTime;
static int32 currentTimeValid = FALSE;
static char version[] = "SIMH002";

static t_stat simh_dev_reset(DEVICE *dptr) {
	currentTimeValid				= FALSE;
	ClockZSDOSDelta					= 0;
	setClockZSDOSPos				= 0;
	getClockZSDOSPos				= 0;
	ClockCPM3Delta					= 0;
	setClockCPM3Pos					= 0;
	getClockCPM3Pos					= 0;
	getStopWatchDeltaPos		= 0;
	getCommonPos						= 0;
	setTimerDeltaPos				= 0;
	setTimerInterruptAdrPos = 0;
	markTimeSP							= 0;
	versionPos							= 0;
	lastCommand							= 0;
	lastCPMStatus						= SCPE_OK;
	timerInterrupt					= FALSE;
	if (simh_unit.flags & UNIT_SIMH_TIMERON) {
		simh_dev_set_timeron(NULL, 0, NULL, NULL);
	}
	return SCPE_OK;
}

static void warnNoRealTimeClock(void) {
	if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
		printf("Sorry - no real time clock available.\n");
	}
}

static t_stat simh_dev_set_timeron(UNIT *uptr, int32 value, char *cptr, void *desc) {
	if (rtc_avail) {
		timeOfNextInterrupt = sim_os_msec() + timerDelta;
		return sim_activate(&simh_unit, simh_unit.wait);	/* activate unit */
	}
	else {
		warnNoRealTimeClock();
		return SCPE_ARG;
	}
}

static t_stat simh_dev_set_timeroff(UNIT *uptr, int32 value, char *cptr, void *desc) {
	timerInterrupt = FALSE;
	sim_cancel(&simh_unit);
	return SCPE_OK;
}

static t_stat simh_svc(UNIT *uptr) {
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

/* The CP/M command line is used as the name of a file and UNIT* uptr is attached to it. */
static void attachCPM(UNIT *uptr) {
	char cpmCommandLine[cpmCommandLineLength];
	uint32 i, len = (GetBYTEWrapper(0x80) & 0x7f) - 1; /* 0x80 contains length of command line, discard first char */
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
		message3("Cannot open '%s' (%s).", cpmCommandLine, scp_error_messages[lastCPMStatus - SCPE_BASE]);
	}
}

/* setClockZSDOSAdr points to 6 byte block in M: YY MM DD HH MM SS in BCD notation */
static void setClockZSDOS(void) {
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
static time_t mkCPM3Origin(void) {
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
static void setClockCPM3(void) {
	ClockCPM3Delta = mkCPM3Origin()																																	+
		(GetBYTEWrapper(setClockCPM3Adr) + GetBYTEWrapper(setClockCPM3Adr + 1) * 256) * secondsPerDay	+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 2)) * secondsPerHour																	+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 3)) * secondsPerMinute																+
		fromBCD(GetBYTEWrapper(setClockCPM3Adr + 4)) - time(NULL);
}

static int32 simh_in(const int32 port) {
	int32 result = 0;
	switch(lastCommand) {
		case attachPTRCmd:
		case attachPTPCmd:
			result = lastCPMStatus;
			lastCommand = 0;
			break;
		case getClockZSDOSCmd:
			if (currentTimeValid) {
				switch(getClockZSDOSPos) {
					case 0:
						result = toBCD(currentTime.tm_year > 99 ?
							currentTime.tm_year - 100 : currentTime.tm_year);
						getClockZSDOSPos = 1;
						break;
					case 1:
						result = toBCD(currentTime.tm_mon + 1);
						getClockZSDOSPos = 2;
						break;
					case 2:
						result = toBCD(currentTime.tm_mday);
						getClockZSDOSPos = 3;
						break;
					case 3:
						result = toBCD(currentTime.tm_hour);
						getClockZSDOSPos = 4;
						break;
					case 4:
						result = toBCD(currentTime.tm_min);
						getClockZSDOSPos = 5;
						break;
					case 5:
						result = toBCD(currentTime.tm_sec);
						getClockZSDOSPos = lastCommand = 0;
						break;
				}
			}
			else {
				result = getClockZSDOSPos = lastCommand = 0;
			}
			break;
		case getClockCPM3Cmd:
			if (currentTimeValid) {
				switch(getClockCPM3Pos) {
					case 0:
						result = daysCPM3SinceOrg & 0xff;
						getClockCPM3Pos = 1;
						break;
					case 1:
						result = (daysCPM3SinceOrg >> 8) & 0xff;
						getClockCPM3Pos = 2;
						break;
					case 2:
						result = toBCD(currentTime.tm_hour);
						getClockCPM3Pos = 3;
						break;
					case 3:
						result = toBCD(currentTime.tm_min);
						getClockCPM3Pos = 4;
						break;
					case 4:
						result = toBCD(currentTime.tm_sec);
						getClockCPM3Pos = lastCommand = 0;
						break;
				}
			}
			else {
				result = getClockCPM3Pos = lastCommand = 0;
			}
			break;
		case getSIMHVersionCmd:
			result = version[versionPos++];
			if (result == 0) {
				versionPos = lastCommand = 0;
			}
			break;
		case getBankSelectCmd:
			if (cpu_unit.flags & UNIT_BANKED) {
				result = getBankSelect();
			}
			else {
				result = 0;
				if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
					message1("Get selected bank ignored for non-banked memory.");
				}
			}
			lastCommand = 0;
			break;
		case getCommonCmd:
			if (getCommonPos == 0) {
				result = getCommon() & 0xff;
				getCommonPos = 1;
			}
			else {
				result = (getCommon() >> 8) & 0xff;
				getCommonPos = lastCommand = 0;
			}
			break;
		case hasBankedMemoryCmd:
			result = cpu_unit.flags & UNIT_BANKED ? MAXBANKS : 0;
			lastCommand = 0;
			break;
		case readStopWatchCmd:
			if (getStopWatchDeltaPos == 0) {
				result = stopWatchDelta & 0xff;
				getStopWatchDeltaPos = 1;
			}
			else {
				result = (stopWatchDelta >> 8) & 0xff;
				getStopWatchDeltaPos = lastCommand = 0;
			}
			break;
		default:
			if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
				message2("Unnecessary IN from SIMH pseudo device on port %03xh ignored.",
					port);
			}
			result = lastCommand = 0;
	}
	return result;
}

static int32 simh_out(const int32 port, const int32 data) {
	time_t now;
	switch(lastCommand) {
		case setClockZSDOSCmd:
			if (setClockZSDOSPos == 0) {
				setClockZSDOSAdr = data;
				setClockZSDOSPos = 1;
			}
			else {
				setClockZSDOSAdr |= (data << 8);
				setClockZSDOS();
				setClockZSDOSPos = lastCommand = 0;
			}
			break;
		case setClockCPM3Cmd:
			if (setClockCPM3Pos == 0) {
				setClockCPM3Adr = data;
				setClockCPM3Pos = 1;
			}
			else {
				setClockCPM3Adr |= (data << 8);
				setClockCPM3();
				setClockCPM3Pos = lastCommand = 0;
			}
			break;
		case setBankSelectCmd:
			if (cpu_unit.flags & UNIT_BANKED) {
				setBankSelect(data & BANKMASK);
			}
			else if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
				message2("Set selected bank to %i ignored for non-banked memory.", data & 3);
			}
			lastCommand = 0;
			break;
		case setTimerDeltaCmd:
			if (setTimerDeltaPos == 0) {
				timerDelta				= data;
				setTimerDeltaPos	= 1;
			}
			else {
				timerDelta |= (data << 8);
				setTimerDeltaPos = lastCommand = 0;
			}
			break;
		case setTimerInterruptAdrCmd:
			if (setTimerInterruptAdrPos == 0) {
				timerInterruptHandler			= data;
				setTimerInterruptAdrPos		= 1;
			}
			else {
				timerInterruptHandler |= (data << 8);
				setTimerInterruptAdrPos = lastCommand = 0;
			}
			break;
		default:
			lastCommand = data;
			switch(data) {
				case printTimeCmd:	/* print time */
					if (rtc_avail) {
						message2("Current time in milliseconds = %d.", sim_os_msec());
					}
					else {
						warnNoRealTimeClock();
					}
					break;
				case startTimerCmd:	/* create a new timer on top of stack */
					if (rtc_avail) {
						if (markTimeSP < splimit) {
							markTime[markTimeSP++] = sim_os_msec();
						}
						else {
							message1("Timer stack overflow.");
						}
					}
					else {
						warnNoRealTimeClock();
					}
					break;
				case stopTimerCmd:	/* stop timer on top of stack and show time difference */
					if (rtc_avail) {
						if (markTimeSP > 0) {
							uint32 delta = sim_os_msec() - markTime[--markTimeSP];
							message2("Timer stopped. Elapsed time in milliseconds = %d.", delta);
						}
						else {
							message1("No timer active.");
						}
					}
					else {
						warnNoRealTimeClock();
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
				case getSIMHVersionCmd:
					versionPos = 0;
					break;
				case getClockZSDOSCmd:
					time(&now);
					now += ClockZSDOSDelta;
					currentTime = *localtime(&now);
					currentTimeValid = TRUE;
					getClockZSDOSPos = 0;
					break;
				case setClockZSDOSCmd:
					setClockZSDOSPos = 0;
					break;
				case getClockCPM3Cmd:
					time(&now);
					now += ClockCPM3Delta;
					currentTime = *localtime(&now);
					currentTimeValid = TRUE;
					daysCPM3SinceOrg = (now - mkCPM3Origin()) / secondsPerDay;
					getClockCPM3Pos = 0;
					break;
				case setClockCPM3Cmd:
					setClockCPM3Pos = 0;
					break;
				case getBankSelectCmd:
				case setBankSelectCmd:
				case getCommonCmd:
				case hasBankedMemoryCmd:
					break;
				case resetSIMHInterfaceCmd:
					markTimeSP	= 0;
					lastCommand	= 0;
					break;
				case showTimerCmd:	/* show time difference to timer on top of stack */
					if (rtc_avail) {
						if (markTimeSP > 0) {
							uint32 delta = sim_os_msec() - markTime[markTimeSP - 1];
							message2("Timer running. Elapsed in milliseconds = %d.", delta);
						}
						else {
							message1("No timer active.");
						}
					}
					else {
						warnNoRealTimeClock();
					}
					break;
				case attachPTPCmd:	/* attach ptp to the file with name at beginning of CP/M command line */
					attachCPM(&ptp_unit);
					break;
				case detachPTPCmd:	/* detach ptp */
					detach_unit(&ptp_unit);
					break;
				case setZ80CPUCmd:
					cpu_unit.flags |= UNIT_CHIP;
					break;
				case set8080CPUCmd:
					cpu_unit.flags &= ~UNIT_CHIP;
					break;
				case startTimerInterruptsCmd:
					if (simh_dev_set_timeron(NULL, 0, NULL, NULL) == SCPE_OK) {
						timerInterrupt = FALSE;
						simh_unit.flags |= UNIT_SIMH_TIMERON;
					}
					break;
				case stopTimerInterruptsCmd:
					simh_unit.flags &= ~UNIT_SIMH_TIMERON;
					simh_dev_set_timeroff(NULL, 0, NULL, NULL);
					break;
				case setTimerDeltaCmd:
					setTimerDeltaPos = 0;
					break;
				case setTimerInterruptAdrCmd:
					setTimerInterruptAdrPos = 0;
					break;
				case resetStopWatchCmd:
					stopWatchNow = rtc_avail ? sim_os_msec() : 0;
					break;
				case readStopWatchCmd:
					getStopWatchDeltaPos = 0;
					stopWatchDelta = rtc_avail ? sim_os_msec() - stopWatchNow : 0;
					break;
				default:
					if (simh_unit.flags & UNIT_SIMH_VERBOSE) {
						message3("Unknown command (%i) to SIMH pseudo device on port %03xh ignored.",
							data, port);
					}
			}
	}
	return 0; /* ignored, since OUT */
}

/* port 0xfe is a device for communication SIMH <--> Altair machine */
int32 simh_dev(const int32 port, const int32 io, const int32 data) {
	return io == 0 ? simh_in(port) : simh_out(port, data);
}
