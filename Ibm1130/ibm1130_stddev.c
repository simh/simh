/* ibm1130_stddev.c: IBM 1130 standard I/O devices simulator

   Copyright (c) 2001, Brian Knittel
   Based on PDP-11 simulator written by Robert M Supnik

   Brian Knittel
   Revision History

   31July2001 - Derived from pdp11_stddev.c, which carries this disclaimer:
 
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
void xio_1403_printer	(int32 addr, int32 func, int32 modify)			{badio("1403 printer");}

void xio_2250_display	(int32 addr, int32 func, int32 modify)
{
	if (func != XIO_CONTROL)
		badio("2250 display");		// resmon issues stop control, so ignore XIO_CONTROL
}

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

#define CSET_MASK	1						/* character set */
#define CSET_NORMAL 0
#define CSET_ASCII	1

#define IRQ_KEY				0x11			/* ctrl-Q */
#define PROGRAM_STOP_KEY	0x03			/* ctrl-C */

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
	{ DRDATA (POS, tti_unit.pos, 32), PV_LEFT },
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
	{ DRDATA (POS, tto_unit.pos, 32), PV_LEFT },
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

t_stat tti_svc (UNIT *uptr)
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
		SETBIT(con_dsw, CON_DSW_PROGRAM_STOP);
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

t_stat tti_reset (DEVICE *dptr)
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

t_stat tto_svc (UNIT *uptr)
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

t_stat tto_reset (DEVICE *dptr)
{
	tto_unit.buf = 0;
	tto_dsw = 0;

	CLRBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

	sim_cancel(&tto_unit);						/* deactivate unit */

	return SCPE_OK;
}

/***************************************************************************************
 *  1132 PRINTER 
 ***************************************************************************************/

#define PRT_DSW_READ_EMITTER_RESPONSE		0x8000
#define PRT_DSW_SKIP_RESPONSE				0x4000
#define PRT_DSW_SPACE_RESPONSE				0x2000
#define PRT_DSW_CARRIAGE_BUSY				0x1000
#define PRT_DSW_PRINT_SCAN_CHECK			0x0800
#define PRT_DSW_NOT_READY					0x0400
#define PRT_DSW_PRINTER_BUSY				0x0200

#define PRT_DSW_CHANNEL_MASK				0x00FF
#define PRT_DSW_CHANNEL_1					0x0080
#define PRT_DSW_CHANNEL_2					0x0040
#define PRT_DSW_CHANNEL_3					0x0020
#define PRT_DSW_CHANNEL_4					0x0010
#define PRT_DSW_CHANNEL_5					0x0008
#define PRT_DSW_CHANNEL_6					0x0004
#define PRT_DSW_CHANNEL_9					0x0002
#define PRT_DSW_CHANNEL_12					0x0001

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

static t_stat prt_svc    (UNIT *uptr);
static t_stat prt_reset  (DEVICE *dptr);
static t_stat prt_attach (UNIT *uptr, char *cptr);
static t_stat prt_detach (UNIT *uptr);

static int16 prt_dsw = 0;									/* device status word */
static int32 prt_swait = 500;								/* line skip wait */
static int32 prt_cwait = 1000;								/* character rotation wait */

#define UNIT_V_FORMCHECK   (UNIT_V_UF + 0)					/* out of paper error */
#define UNIT_V_DATACHECK   (UNIT_V_UF + 1)					/* printer overrun error */
#define UNIT_V_SKIPPING	   (UNIT_V_UF + 2)					/* printer skipping */
#define UNIT_V_SPACING	   (UNIT_V_UF + 3)					/* printer is spacing */
#define UNIT_V_PRINTING	   (UNIT_V_UF + 4)					/* printer printing */

#define UNIT_FORMCHECK	 (1u << UNIT_V_FORMCHECK)
#define UNIT_DATACHECK	 (1u << UNIT_V_DATACHECK)
#define UNIT_SKIPPING	 (1u << UNIT_V_SKIPPING)
#define UNIT_SPACING	 (1u << UNIT_V_SPACING)
#define UNIT_PRINTING	 (1u << UNIT_V_PRINTING)

UNIT prt_unit[] = {
	{ UDATA (&prt_svc, UNIT_ATTABLE, 0) },
};

/* Parameter in the unit descriptor */

#define CMD_NONE		0
#define CMD_SPACE		1
#define CMD_SKIP		2
#define CMD_PRINT		3

REG prt_reg[] = {
	{ HRDATA (PRTDSW, prt_dsw, 16) },					/* device status word */
	{ DRDATA (STIME,  prt_swait, 24), PV_LEFT },		/* line skip wait */
	{ DRDATA (CTIME,  prt_cwait, 24), PV_LEFT },		/* character rotation wait */
	{ NULL }  };

DEVICE prt_dev = {
	"PRT", prt_unit, prt_reg, NULL,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, &prt_reset,
	NULL, prt_attach, prt_detach};

#define PRT_COLUMNS 120
#define PRT_ROWLEN	120
#define MAX_OVPRINT	 20

static char prtbuf[PRT_ROWLEN*MAX_OVPRINT];
static int  nprint[PRT_COLUMNS], ncol[MAX_OVPRINT], maxnp;
static int  prt_nchar, prt_row;							/* current printwheel position, current page row */
static int  prt_nnl;									/* number of queued newlines */

#define CC_CHANNEL_1		0x0080						/* carriage control tape punch values */
#define CC_CHANNEL_2		0x0040
#define CC_CHANNEL_3		0x0020
#define CC_CHANNEL_4		0x0010
#define CC_CHANNEL_5		0x0008
#define CC_CHANNEL_6		0x0004
#define CC_CHANNEL_9		0x0002
#define CC_CHANNEL_12		0x0001

#define PRT_PAGELENGTH 66

// glunk need to fill these two arrays in -- cctape and codewheel1132

static int cctape[PRT_PAGELENGTH];						/* standard carriage control tape */

static struct tag_ccpunches {							/* list of rows and punches on tape */
	int row, channels;
} ccpunches[] = {
	  7, CC_CHANNEL_12,									/* these came from the tape in our printer */
	 13, CC_CHANNEL_1									/* modulo 66 */
};

#include "ibm1130_prtwheel.h"

// reset_prt_line - clear the print line following paper advancement

static void reset_prt_line (void)
{
	memset(nprint, 0, sizeof(nprint));
	memset(ncol,   0, sizeof(ncol));
	maxnp = 0;
}

// save_prt_line - fire hammers for character 'ch'

static t_bool save_prt_line (int ch)
{
	int i, r, addr = 32;
	int32 mask = 0, wd = 0;

	for (i = 0; i < PRT_COLUMNS; i++) {
		if (mask == 0) {					// fetch next word from memory
			mask = 0x8000;
			wd   = M[addr++];
		}

		if (wd & mask) {					// hammer is to fire in this column
			if ((r = nprint[i]) < MAX_OVPRINT) {
				if (ncol[r] <= i) {				// we haven't moved this far yet
					if (ncol[r] == 0)									// first char in this row?
						memset(prtbuf+r*PRT_ROWLEN, ' ', PRT_COLUMNS);	// blank out the new row
					ncol[r] = i+1;										// remember new row length
				}
				prtbuf[r*PRT_ROWLEN + i] = (char) ch;					// save the character

				nprint[i]++;											// remember max overprintings for this column
				maxnp = MAX(maxnp, nprint[i]);
			}
		}

		mask >>= 1;														// prepare to examine next bit
	}

	return wd & 1;			// return TRUE if the last word has lsb set, which means all bits had been set
}

// write_line - write collected line to output file. No need to trim spaces as the hammers
// are never fired for them, so ncol[r] is the last printed position on each line.

static void flush_prt_line (FILE *fd, t_bool space)
{
	int r;

	if (! (space || maxnp))									// nothing to do
		return;

	prt_row = (prt_row+1) % PRT_PAGELENGTH;					// NEXT line

	if (space && ! maxnp) {									// spacing only
		if (prt_row == 0 && prt_nnl) {
			putc('\f', fd);
			prt_nnl = 0;
		}
		else
			prt_nnl++;

		return;
	}

	if (prt_nnl) {											// there are queued newlines
		if (prt_row == 0 && prt_nnl) {						// we spaced to top of form: use formfeed
			putc('\f', fd);
			prt_nnl = 0;
		}
		else {
			while (prt_nnl > 0) {							// spit out queued newlines
#ifdef WIN32
				putc('\r', fd);								// DOS/Windows: end with cr/lf
#endif
				putc('\n', fd);								// otherwise end with lf
				prt_nnl--;
			}
		}
	}

	for (r = 0; r < maxnp; r++) {
		if (r > 0)
			putc('\r', fd);									// carriage return between overprinted lines
		fwrite(&prtbuf[r*PRT_ROWLEN], 1, ncol[r], fd);
	}

	reset_prt_line();

	prt_nnl++;												// queue a newline
}

#define PRT_CMD_START_PRINTER		0x0080
#define PRT_CMD_STOP_PRINTER		0x0040
#define PRT_CMD_START_CARRIAGE		0x0004
#define PRT_CMD_STOP_CARRIAGE		0x0002
#define PRT_CMD_SPACE				0x0001

#define PRT_CMD_MASK				0x00C7

/* xio_1132_printer - XIO command interpreter for the 1132 printer */

void xio_1132_printer (int32 iocc_addr, int32 func, int32 modify)
{
	char msg[80];
	UNIT *uptr = &prt_unit[0];

	switch (func) {
		case XIO_READ:
			M[iocc_addr & mem_mask] = codewheel1132[prt_nchar].ebcdic << 8;

			if ((uptr->flags & UNIT_PRINTING) == 0)				/* if we're not printing, advance this after every test */
				prt_nchar = (prt_nchar + 1) % WHEELCHARS;
			break;

		case XIO_SENSE_DEV:
			ACC = prt_dsw;
			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(prt_dsw, PRT_DSW_READ_EMITTER_RESPONSE | PRT_DSW_SKIP_RESPONSE | PRT_DSW_SPACE_RESPONSE);
				CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
			}
			break;

		case XIO_CONTROL:
			if (modify & PRT_CMD_START_PRINTER)
				SETBIT(uptr->flags, UNIT_PRINTING);

			if (modify & PRT_CMD_STOP_PRINTER)
				CLRBIT(uptr->flags, UNIT_PRINTING);

			if (modify & PRT_CMD_START_CARRIAGE)
				SETBIT(uptr->flags, UNIT_SKIPPING);

			if (modify & PRT_CMD_STOP_CARRIAGE)
				CLRBIT(uptr->flags, UNIT_SKIPPING);

			if (modify & PRT_CMD_SPACE)
				SETBIT(uptr->flags, UNIT_SPACING);

			sim_cancel(uptr);
			if (uptr->flags & PRT_CMD_MASK)	{				// busy bit = doing something
				SETBIT(prt_dsw, PRT_DSW_PRINTER_BUSY);
				sim_activate(uptr, prt_cwait);
			}
			else
				CLRBIT(prt_dsw, PRT_DSW_PRINTER_BUSY);

			if (uptr->flags & (UNIT_SKIPPING|UNIT_SPACING))
				SETBIT(prt_dsw, PRT_DSW_CARRIAGE_BUSY);
			else
				CLRBIT(prt_dsw, PRT_DSW_CARRIAGE_BUSY);
			
			if ((uptr->flags & (UNIT_SKIPPING|UNIT_SPACING)) == (UNIT_SKIPPING|UNIT_SPACING)) {
				sprintf(msg, "1132 printer skip and space at same time?");
				xio_error(msg);
			}
			break;

		default:
			sprintf(msg, "Invalid 1132 printer XIO function %x", func);
			xio_error(msg);
	}
}

#define SET_ACTION(u,a) {(u)->flags &= ~(UNIT_SKIPPING|UNIT_SPACING|UNIT_PRINTING); (u)->flags |= a;}

static t_stat prt_svc (UNIT *uptr)
{
	if (prt_dsw & PRT_DSW_NOT_READY) {						// cancel operation if printer went offline
		SETBIT(uptr->flags, UNIT_FORMCHECK);
		SET_ACTION(uptr, 0);
		forms_check(TRUE);									// and turn on forms check lamp
		return SCPE_OK;
	}

	if (uptr->flags & UNIT_SPACING) {
		flush_prt_line(uptr->fileref, TRUE);

		prt_dsw = prt_dsw & ~PRT_DSW_CHANNEL_MASK;
		prt_dsw |= cctape[prt_row];

		SETBIT(prt_dsw, PRT_DSW_SPACE_RESPONSE);
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);
		calc_ints();

		CLRBIT(uptr->flags, UNIT_SPACING);				// done with this
		CLRBIT(prt_dsw, PRT_DSW_PRINTER_BUSY|PRT_DSW_CARRIAGE_BUSY);
	}

	if (uptr->flags & UNIT_SKIPPING) {
		do {
			flush_prt_line(uptr->fileref, TRUE);
			prt_dsw = (prt_dsw & ~PRT_DSW_CHANNEL_MASK) | cctape[prt_row];
		} while (cctape[prt_row] == 0);					// slew directly to a cc tape punch

		SETBIT(prt_dsw, PRT_DSW_SKIP_RESPONSE);
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);
		calc_ints();
	}

	if (uptr->flags & UNIT_PRINTING) {
		if (! save_prt_line(codewheel1132[prt_nchar].ascii)) {	// save previous printed line
			SETBIT(uptr->flags, UNIT_DATACHECK);		// buffer wasn't set in time
			SET_ACTION(uptr, 0);
			print_check(TRUE);							// and turn on forms check lamp
			return SCPE_OK;
		}

		prt_nchar = (prt_nchar + 1) % WHEELCHARS;		// advance print drum

		SETBIT(prt_dsw, PRT_DSW_READ_EMITTER_RESPONSE);	// issue interrupt to tell printer to set buffer
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);			// we'll save the printed stuff just before next emitter response (later than on real 1130)
		calc_ints();
	}

	if (uptr->flags & (UNIT_SPACING|UNIT_SKIPPING|UNIT_PRINTING)) {	// still doing something
		SETBIT(prt_dsw, PRT_DSW_PRINTER_BUSY);
		sim_activate(uptr, prt_cwait);
	}
	else
		CLRBIT(prt_dsw, PRT_DSW_PRINTER_BUSY);

	return SCPE_OK;
}

static t_stat prt_reset (DEVICE *dptr)
{
	UNIT *uptr = &prt_unit[0];
	int i;

	sim_cancel(uptr);

	memset(cctape, 0, sizeof(cctape));			// copy punch list into carriage control tape image
	for (i = 0; i < (sizeof(ccpunches)/sizeof(ccpunches[0])); i++)
		cctape[ccpunches[i].row-1] |= ccpunches[i].channels;

	CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
	CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK);
	SET_ACTION(uptr, 0);
	calc_ints();

	prt_nchar = 0;
	prt_row   = 0;
	prt_nnl   = 0;
	prt_dsw   = cctape[prt_row];
	reset_prt_line();

	if (! IS_ONLINE(uptr))
		SETBIT(prt_dsw, PRT_DSW_NOT_READY);

	forms_check(FALSE);
	return SCPE_OK;
}

static t_stat prt_attach (UNIT *uptr, char *cptr)
{
	t_stat rval;

	if (uptr->flags & UNIT_ATT) {
		if ((rval = prt_detach(uptr)) != SCPE_OK) {
			prt_dsw |= PRT_DSW_NOT_READY;
			return rval;
		}
	}

	sim_cancel(uptr);

	if ((rval = attach_unit(uptr, cptr)) != SCPE_OK) {
		prt_dsw |= PRT_DSW_NOT_READY;
		return rval;
	}

	CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
	CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK);
	SET_ACTION(uptr, 0);
	calc_ints();

	prt_nchar = 0;
	prt_nnl   = 0;
	prt_row   = 0;
	prt_dsw   = (prt_dsw & ~PRT_DSW_CHANNEL_MASK) | cctape[prt_row];
	if (IS_ONLINE(uptr))
		CLRBIT(prt_dsw, PRT_DSW_NOT_READY);
	else
		SETBIT(prt_dsw, PRT_DSW_NOT_READY);

	reset_prt_line();
	forms_check(FALSE);
	return SCPE_OK;
}

static t_stat prt_detach (UNIT *uptr)
{
	t_stat rval;

	flush_prt_line(uptr->fileref, FALSE);

	if ((rval = detach_unit(uptr)) != SCPE_OK)
		return rval;

	sim_cancel(uptr);

	CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
	CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK);
	SET_ACTION(uptr, 0);
	calc_ints();

	SETBIT(prt_dsw, PRT_DSW_NOT_READY);

	forms_check(FALSE);
	return SCPE_OK;
}
