/* ibm1130_stddev.c: IBM 1130 standard I/O devices simulator

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel

   Revision History:
   2002.09.13 - pulled 1132 printer out of this file into ibm1130_prt.c

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

#include "ibm1130_defs.h"

/* ---------------------------------------------------------------------------- */

static void badio (char *dev)
{
// the real 1130 just ignores attempts to use uninstalled devices. They get tested
// at times, so it's best to be quiet about this
// printf("%s I/O is not yet supported", dev);
// wait_state = WAIT_INVALID_OP;
}

void xio_1134_papertape	(int32 addr, int32 func, int32 modify)			{badio("papertape");}
void xio_1627_plotter	(int32 addr, int32 func, int32 modify)			{badio("plotter");}
void xio_1231_optical	(int32 addr, int32 func, int32 modify)			{badio("optical mark");}
void xio_2501_card		(int32 addr, int32 func, int32 modify)			{badio("2501 card");}
void xio_1131_synch		(int32 addr, int32 func, int32 modify)			{badio("SCA");}
void xio_system7		(int32 addr, int32 func, int32 modify)			{badio("System 7");}

/* ---------------------------------------------------------------------------- */

static int32 tti_dsw = 0;					/* device status words */
static int32 tto_dsw = 0;
static int32 con_dsw = 0;
					
static t_stat tti_svc (UNIT *uptr);
static t_stat tto_svc (UNIT *uptr);
static t_stat tti_reset (DEVICE *dptr);
static t_stat tto_reset (DEVICE *dptr);

extern t_stat sim_poll_kbd (void);
extern t_stat sim_wait_kbd (void);
extern t_stat sim_putchar (int32 out);

extern UNIT *sim_clock_queue;
extern int cgi;

#define CSET_MASK	1						/* character set */
#define CSET_NORMAL 0
#define CSET_ASCII	1

#define IRQ_KEY				0x11			/* ctrl-Q */
#define PROGRAM_STOP_KEY	0x10			/* ctrl-P */

#include "ibm1130_conout.h"					/* conout_to_ascii table */
#include "ibm1130_conin.h"					/* ascii_to_conin  table */

/* TTI data structures

   tti_dev	TTI device descriptor
   tti_unit	TTI unit descriptor
   tti_reg	TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
	{ ORDATA (BUF, tti_unit.buf, 8) },
	{ ORDATA (DSW, tti_dsw, 16) },
	{ DRDATA (POS, tti_unit.pos, 31), PV_LEFT },
	{ DRDATA (STIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB tti_mod[] = {
	{ CSET_MASK,  CSET_NORMAL, NULL, "1130", NULL},
	{ CSET_MASK,  CSET_ASCII,  NULL, "ASCII",  NULL},
	{ 0 }  };

DEVICE tti_dev = {
	"KEYBOARD", &tti_unit, tti_reg, tti_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti_reset,
	NULL, NULL, NULL };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit descriptor
   tto_reg	TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
	{ ORDATA (BUF, tto_unit.buf, 8) },
	{ ORDATA (DSW, tto_dsw, 16) },
	{ DRDATA (POS, tto_unit.pos, 31), PV_LEFT },
	{ DRDATA (STIME, tto_unit.wait, 24), PV_LEFT },
	{ NULL }  };

MTAB tto_mod[] = {
	{ CSET_MASK,  CSET_NORMAL, NULL, "1130",   NULL},
	{ CSET_MASK,  CSET_ASCII,  NULL, "ASCII",  NULL},
	{ 0 }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, tto_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto_reset,
	NULL, NULL, NULL };

/* Terminal input routines

   tti_svc		process event (character ready)
   tti_reset	process reset
   tto_svc		process event (print character)
   tto_reset	process reset
*/

#define TT_DSW_PRINTER_RESPONSE			0x8000
#define TT_DSW_KEYBOARD_RESPONSE		0x4000
#define TT_DSW_INTERRUPT_REQUEST		0x2000
#define TT_DSW_KEYBOARD_CONSOLE			0x1000
#define TT_DSW_PRINTER_BUSY				0x0800
#define TT_DSW_PRINTER_NOT_READY		0x0400
#define TT_DSW_KEYBOARD_BUSY			0x0200

void xio_1131_console (int32 iocc_addr, int32 func, int32 modify)
{
	int ch;
	char msg[80];

	switch (func) {
		case XIO_CONTROL:
			SETBIT(tti_dsw, TT_DSW_KEYBOARD_BUSY);
			keyboard_selected(TRUE);
//			sim_activate(&tti_unit, tti_unit.wait);		/* poll keyboard  never stops */
			break;

		case XIO_READ:
			WriteW(iocc_addr, tti_unit.buf);
			CLRBIT(tti_dsw, TT_DSW_KEYBOARD_BUSY);
			keyboard_selected(FALSE);
			break;

		case XIO_WRITE:
			ch = (ReadW(iocc_addr) >> 8) & 0xFF;		/* get character to write */

			if ((tto_unit.flags & CSET_MASK) == CSET_NORMAL) 
				ch = conout_to_ascii[ch];				/* convert to ASCII */

			if (ch == 0)
				ch = '?';								/* hmmm, unknown character */

			tto_unit.buf = ch;							/* save character to write */
			SETBIT(tto_dsw, TT_DSW_PRINTER_BUSY);
			sim_activate(&tto_unit, tto_unit.wait);		/* schedule interrupt */
			break;

		case XIO_SENSE_DEV:
			ACC = tto_dsw | tti_dsw;
			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(tto_dsw, TT_DSW_PRINTER_RESPONSE);
				CLRBIT(tti_dsw, TT_DSW_KEYBOARD_RESPONSE);
				CLRBIT(ILSW[4], ILSW_4_CONSOLE);
			}
			break;

		default:
			sprintf(msg, "Invalid console XIO function %x", func);
			xio_error(msg);
	}
}

static void Beep (void)			// notify user keyboard was locked or key was bad
{
	sim_putchar(7);
}

// tti_svc - keyboard polling (never stops)

static t_stat tti_svc (UNIT *uptr)
{
	int32 temp;

	sim_activate(&tti_unit, tti_unit.wait);			/* continue polling */

	temp = sim_poll_kbd();

	if (temp < SCPE_KFLAG)
		return temp;								/* no char or error? */

	temp &= 0xFF;									/* remove SCPE_KFLAG */

	if (temp == IRQ_KEY) {							/* interrupt request key */
		SETBIT(tti_dsw, TT_DSW_INTERRUPT_REQUEST);	/* queue interrupt */
		SETBIT(ILSW[4], ILSW_4_CONSOLE);
		calc_ints();

		return SCPE_OK;
	}

	if (temp == PROGRAM_STOP_KEY) {					/* simulate the program stop button */
		SETBIT(con_dsw, CPU_DSW_PROGRAM_STOP);
		SETBIT(ILSW[5], ILSW_5_PROGRAM_STOP);
		calc_ints();

		return SCPE_OK;
	}

	if (tti_dsw & TT_DSW_KEYBOARD_BUSY) {			/* only store character if it was requested (keyboard unlocked) */
		if ((uptr->flags & CSET_MASK) == CSET_NORMAL) 
			temp = ascii_to_conin[temp];

		if (temp == 0)	{							/* ignore invalid characters */
			Beep();
			calc_ints();
			return SCPE_OK;
		}

		tti_unit.buf = temp & 0xFFFE;				/* save keystroke except last bit (not defined, */
		tti_unit.pos = tti_unit.pos + 1;			/* but it lets us distinguish 0 from no punch ' ' */

		CLRBIT(tti_dsw, TT_DSW_KEYBOARD_BUSY);		/* clear busy flag (relock keyboard) */
		keyboard_selected(FALSE);

		SETBIT(tti_dsw, TT_DSW_KEYBOARD_RESPONSE);	/* queue interrupt */
		SETBIT(ILSW[4], ILSW_4_CONSOLE);
		calc_ints();
	}
	else
		Beep();

	return SCPE_OK;
}

static t_stat tti_reset (DEVICE *dptr)
{
	tti_unit.buf = 0;
	tti_dsw = 0;

	CLRBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();
	keyboard_selected(FALSE);

	sim_activate(&tti_unit, tti_unit.wait);			/* always poll keyboard */

	return SCPE_OK;
}

t_bool keyboard_is_locked (void)					/* return TRUE if keyboard is not expecting a character */
{
	return (tti_dsw & TT_DSW_KEYBOARD_BUSY) == 0;
}

static t_stat tto_svc (UNIT *uptr)
{
	int32 temp;
	int ch;

	CLRBIT(tto_dsw, TT_DSW_PRINTER_BUSY);
	SETBIT(tto_dsw, TT_DSW_PRINTER_RESPONSE);

	SETBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

	ch = tto_unit.buf & 0xFF;

	switch (ch) {
		case IGNR:
			break;

		case CRLF:
			if (! cgi)
				if ((temp = sim_putchar('\r')) != SCPE_OK)
					return temp;
			if ((temp = sim_putchar('\n')) != SCPE_OK)
				return temp;

			break;

		default:
			if ((temp = sim_putchar(ch)) != SCPE_OK)
				return temp;

			break;
	}

	tto_unit.pos = tto_unit.pos + 1;			/* hmm, why do we count these? */
	
	return SCPE_OK;
}

static t_stat tto_reset (DEVICE *dptr)
{
	tto_unit.buf = 0;
	tto_dsw = 0;

	CLRBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

	sim_cancel(&tto_unit);						/* deactivate unit */

	return SCPE_OK;
}


