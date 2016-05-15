/* ibm1130_prt.c: IBM 1130 line printer emulation

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel
   Revision History

   2006.12.06 - Moved CGI stuff out of this routine into cgi1130 main() module.

   2006.07.06 - Made 1403 printer 132 columns wide, was 120 previously

   2006.01.03 - Fixed bug in prt_attach, found and fixed by Carl Claunch. Detach followed
   				by reattach of 1403-mode printer left device permanently not-ready.

   2004.11.08 - HACK for demo mode: in physical (-p) mode, multiple consecutive formfeeds are suppressed.
				This lets us do a formfeed at the end of a job to kick the last page out
				without getting another blank page at the beginning of the next job.

   2003.12.02 - Added -p option for physical line printer output (flushes
   				output buffer after each line). When using a physical printer on
   				Windows, be sure to set printer to "send output directly to printer"
   				to disable spooling, otherwise nothing appears until printer is
				detatched.

   2003.11.25 - Changed magic filename for standard output to "(stdout)".

   2002.09.13 - Added 1403 support. New file, taken from part of ibm1130_stddev.c

   Note: The 1403 is much faster, even in emulation, because it takes much
   less CPU power to run it. DMS doesn't use the WAIT command when waiting for
   printer operations to complete, so it ends up burning LOTS of cpu cycles.
   The 1403 printer doesn't require as many.  HOWEVER: DMS must be loaded for the 1403,
   and Fortran IOCS control cards must specify it.

   The 1132 is still the default printer.

   As written, we can't have two printers.

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

#include "ibm1130_defs.h"
#include <stdlib.h>				/* needed for atexit, for cgi mode */

/***************************************************************************************
 *  1132 PRINTER 
 ***************************************************************************************/

#define PRT1132_DSW_READ_EMITTER_RESPONSE		0x8000
#define PRT1132_DSW_SKIP_RESPONSE				0x4000
#define PRT1132_DSW_SPACE_RESPONSE				0x2000
#define PRT1132_DSW_CARRIAGE_BUSY				0x1000
#define PRT1132_DSW_PRINT_SCAN_CHECK			0x0800
#define PRT1132_DSW_NOT_READY					0x0400
#define PRT1132_DSW_PRINTER_BUSY				0x0200

#define PRT1132_DSW_CHANNEL_MASK				0x00FF			/* 1132 printer DSW bits */
#define PRT1132_DSW_CHANNEL_1					0x0080
#define PRT1132_DSW_CHANNEL_2					0x0040
#define PRT1132_DSW_CHANNEL_3					0x0020
#define PRT1132_DSW_CHANNEL_4					0x0010
#define PRT1132_DSW_CHANNEL_5					0x0008
#define PRT1132_DSW_CHANNEL_6					0x0004
#define PRT1132_DSW_CHANNEL_9					0x0002
#define PRT1132_DSW_CHANNEL_12					0x0001

#define PRT1403_DSW_PARITY_CHECK				0x8000			/* 1403 printer DSW bits */
#define PRT1403_DSW_TRANSFER_COMPLETE			0x4000
#define PRT1403_DSW_PRINT_COMPLETE				0x2000
#define PRT1403_DSW_CARRIAGE_COMPLETE			0x1000
#define PRT1403_DSW_RING_CHECK					0x0400
#define PRT1403_DSW_SYNC_CHECK					0x0200
#define PRT1403_DSW_CH9							0x0010
#define PRT1403_DSW_CH12						0x0008
#define PRT1403_DSW_CARRIAGE_BUSY				0x0004
#define PRT1403_DSW_PRINTER_BUSY				0x0002
#define PRT1403_DSW_NOT_READY					0x0001

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

static t_stat prt1132_svc(UNIT *uptr);
static t_stat prt1403_svc(UNIT *uptr);
static t_stat prt_svc    (UNIT *uptr);
static t_stat prt_reset  (DEVICE *dptr);
static t_stat prt_attach (UNIT *uptr, CONST char *cptr);
static t_stat prt_detach (UNIT *uptr);

static int16 PRT_DSW   = 0;									/* device status word */
static int32 prt_swait = 500;								/* line skip wait */
static int32 prt_cwait = 1250;								/* character rotation wait */
static int32 prt_fwait = 100;								/* fast wait, for 1403 operations */
static int32 prt_twait = 50;								/* transfer wait, for 1403 operations */
#define SKIPTARGET	(uptr->u4)								/* target for skip operation */

static t_bool formfed = FALSE;								/* last line printed was a formfeed */

#define UNIT_V_FORMCHECK    (UNIT_V_UF + 0)					/* out of paper error */
#define UNIT_V_DATACHECK    (UNIT_V_UF + 1)					/* printer overrun error */
#define UNIT_V_SKIPPING	    (UNIT_V_UF + 2)					/* printer skipping */
#define UNIT_V_SPACING	    (UNIT_V_UF + 3)					/* printer is spacing */
#define UNIT_V_PRINTING	    (UNIT_V_UF + 4)					/* printer printing */
#define UNIT_V_TRANSFERRING (UNIT_V_UF + 5)					/* unit is transferring print buffer (1403 only) */
#define UNIT_V_1403		    (UNIT_V_UF + 6)					/* printer model is 1403 rather than 1132 */
#define UNIT_V_PARITYCHECK	(UNIT_V_UF + 7)					/* error flags for 1403 */
#define UNIT_V_RINGCHECK	(UNIT_V_UF + 8)
#define UNIT_V_SYNCCHECK	(UNIT_V_UF + 9)
#define UNIT_V_PHYSICAL_PTR	(UNIT_V_UF + 10)				/* this appears in ibm1130_gui as well */
#define UNIT_V_TRACE        (UNIT_V_UF + 11)

#define UNIT_FORMCHECK	  (1u << UNIT_V_FORMCHECK)
#define UNIT_DATACHECK	  (1u << UNIT_V_DATACHECK)
#define UNIT_SKIPPING	  (1u << UNIT_V_SKIPPING)
#define UNIT_SPACING	  (1u << UNIT_V_SPACING)
#define UNIT_PRINTING	  (1u << UNIT_V_PRINTING)
#define UNIT_TRANSFERRING (1u << UNIT_V_TRANSFERRING)
#define UNIT_1403	 	  (1u << UNIT_V_1403)
#define UNIT_PARITYCHECK  (1u << UNIT_V_PARITYCHECK)	
#define UNIT_RINGCHECK	  (1u << UNIT_V_RINGCHECK)
#define UNIT_SYNCCHECK	  (1u << UNIT_V_SYNCCHECK)
#define UNIT_PHYSICAL_PTR (1u << UNIT_V_PHYSICAL_PTR)
#define UNIT_TRACE		  (1u << UNIT_V_TRACE)

UNIT prt_unit[] = {
	{ UDATA (&prt_svc, UNIT_ATTABLE, 0) },
};

#define IS_1403(uptr)      (uptr->flags & UNIT_1403)					/* model test */
#define IS_1132(uptr)     ((uptr->flags & UNIT_1403) == 0)				/* model test */
#define IS_PHYSICAL(uptr)  (uptr->flags & UNIT_PHYSICAL_PTR)
#define DO_TRACE(uptr)	   (uptr->flags & UNIT_TRACE)

/* Parameter in the unit descriptor (1132 printer) */

#define CMD_NONE		0
#define CMD_SPACE		1
#define CMD_SKIP		2
#define CMD_PRINT		3

REG prt_reg[] = {
	{ HRDATA (PRTDSW, PRT_DSW, 16) },					/* device status word */
	{ DRDATA (STIME,  prt_swait, 24), PV_LEFT },		/* line skip wait */
	{ DRDATA (CTIME,  prt_cwait, 24), PV_LEFT },		/* character rotation wait */
	{ DRDATA (FTIME,  prt_fwait, 24), PV_LEFT },		/* 1403 fast wait */
	{ DRDATA (TTIME,  prt_twait, 24), PV_LEFT },		/* 1403 transfer wait */
	{ NULL }  };

MTAB prt_mod[] = {
	{ UNIT_1403, 0,           "1132",    "1132", NULL },	/* model option */
	{ UNIT_1403, UNIT_1403,   "1403",    "1403", NULL },
	{ UNIT_TRACE, UNIT_TRACE, "TRACE",   "TRACE", NULL },
	{ UNIT_TRACE, 0,          "NOTRACE", "NOTRACE", NULL },
	{ 0 }  };

DEVICE prt_dev = {
	"PRT", prt_unit, prt_reg, prt_mod,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, &prt_reset,
	NULL, prt_attach, prt_detach};

#define MAX_COLUMNS     120								
#define MAX_OVPRINT	     20
#define PRT1132_COLUMNS 120
#define PRT1403_COLUMNS 120								/* the 1130's version of the 1403 printed in 120 columns only (see Functional Characteristics) */

static char prtbuf[MAX_COLUMNS*MAX_OVPRINT];
static int  nprint[MAX_COLUMNS], ncol[MAX_OVPRINT], maxnp;
static int  prt_nchar, prt_row;							/* current printwheel position, current page row */
static int  prt_nnl;									/* number of queued newlines */

#define CC_CHANNEL_1		0x0800						/* carriage control tape punch values */
#define CC_CHANNEL_2		0x0400
#define CC_CHANNEL_3		0x0200
#define CC_CHANNEL_4		0x0100
#define CC_CHANNEL_5		0x0080
#define CC_CHANNEL_6		0x0040						/* 7, 8, 10 and 11 are not used on 1132 printer */
#define CC_CHANNEL_7		0x0020
#define CC_CHANNEL_8		0x0010
#define CC_CHANNEL_9		0x0008
#define CC_CHANNEL_10		0x0004
#define CC_CHANNEL_11		0x0002
#define CC_CHANNEL_12		0x0001

#define CC_1403_BITS		0x0FFF						/* all bits for 1403, most for 1132 */
#define CC_1132_BITS		(CC_1403_BITS & ~(CC_CHANNEL_7|CC_CHANNEL_8|CC_CHANNEL_10|CC_CHANNEL_11))

#define PRT_PAGELENGTH 66

static int cctape[PRT_PAGELENGTH];						/* standard carriage control tape */

static struct tag_ccpunches {							/* list of rows and punches on tape */
	int row, channels;
}
ccpunches[] = {
    { 2, CC_CHANNEL_1},								/* channel  1 = top of form */
    {62, CC_CHANNEL_12}								/* channel 12 = bottom of form */
},
cccgi[] = {
    {2, CC_CHANNEL_1}								/* channel  1 = top of form; no bottom of form */
};

#include "ibm1130_prtwheel.h"

/* cc_format_1132 and cc_format_1403 - turn cctape bits into proper format for DSW or status read */

static int cc_format_1132 (int bits)
{
	return ((bits & (CC_CHANNEL_1|CC_CHANNEL_2|CC_CHANNEL_3|CC_CHANNEL_4|CC_CHANNEL_5|CC_CHANNEL_6)) >> 4) |
	       ((bits & CC_CHANNEL_9) >> 3) |
		    (bits & CC_CHANNEL_12);
}

#define cc_format_1403(bits) (bits)

/* reset_prt_line - clear the print line following paper advancement */

static void reset_prt_line (void)
{
	memset(nprint, 0, sizeof(nprint));
	memset(ncol,   0, sizeof(ncol));
	maxnp = 0;
}

/* save_1132_prt_line - fire hammers for character 'ch' */

static t_bool save_1132_prt_line (int ch)
{
	int i, r, addr = 32;
	int32 mask = 0, wd = 0;

	for (i = 0; i < PRT1132_COLUMNS; i++) {
		if (mask == 0) {					/* fetch next word from memory */
			mask = 0x8000;
			wd   = M[addr++];
		}

		if (wd & mask) {					/* hammer is to fire in this column */
			if ((r = nprint[i]) < MAX_OVPRINT) {
				if (ncol[r] <= i) {			/* we haven't moved this far yet */
					if (ncol[r] == 0)									/* first char in this row?  */
						memset(prtbuf+r*MAX_COLUMNS, ' ', PRT1132_COLUMNS);	/* blank out the new row */
					ncol[r] = i+1;										/* remember new row length */
				}
				prtbuf[r*MAX_COLUMNS + i] = (char) ch;					/* save the character */

				nprint[i]++;											/* remember max overprintings for this column */
				maxnp = MAX(maxnp, nprint[i]);
			}
		}

		mask >>= 1;														/* prepare to examine next bit */
	}

	return wd & 1;			/* return TRUE if the last word has lsb set, which means all bits had been set */
}

/* write_line - write collected line to output file. No need to trim spaces as the hammers
 * are never fired for them, so ncol[r] is the last printed position on each line.
 */

static void newpage (FILE *fd, t_bool physical_printer)
{
	if (cgi)
		fputs("<HR>\n", fd);
	else if (! formfed) {
		putc('\f', fd);
		if (physical_printer) {
			fflush(fd);										/* send the ff out to the printer immediately */
			formfed = TRUE;									/* hack: inhibit consecutive ff's */
		}
	}
}

static void flush_prt_line (FILE *fd, int spacemode, t_bool physical_printer)
{
	int r;

	if (! (spacemode || maxnp))								/* nothing to do */
		return;

	prt_row = (prt_row+1) % PRT_PAGELENGTH;					/* NEXT line */

	if (spacemode && ! maxnp) {								/* spacing only */
		if (prt_row == 0 && prt_nnl) {
#ifdef _WIN32
			if (! cgi)
				putc('\r', fd);								/* DOS/Windows: end with cr/lf */
#endif
			putc('\n', fd);									/* otherwise end with lf */
			if (spacemode & UNIT_SKIPPING)					/* add formfeed if we crossed page boundary while skipping */
				newpage(fd, physical_printer);

			prt_nnl = 0;
		}
		else {
			prt_nnl++;
			formfed = FALSE;
		}

		prt_unit->pos++;									/* note something written */
		return;
	}

	if (prt_nnl) {											/* there are queued newlines */
		while (prt_nnl > 0) {								/* spit out queued newlines */
#ifdef _WIN32
			if (! cgi)
				putc('\r', fd);								/* DOS/Windows: end with cr/lf */
#endif
			putc('\n', fd);									/* otherwise end with lf */
			prt_nnl--;
		}
	}

	for (r = 0; r < maxnp; r++) {
		if (r > 0)
			putc('\r', fd);									/* carriage return between overprinted lines */

		fxwrite(&prtbuf[r*MAX_COLUMNS], 1, ncol[r], fd);
	}

	reset_prt_line();

	prt_unit->pos++;										/* note something written */
	prt_nnl++;												/* queue a newline */

	if (physical_printer)									/* if physical printer, send buffered output to device */
		fflush(fd);

	formfed = FALSE;										/* note that something is now on the page */
}

/* 1132 printer commands */

#define PRT_CMD_START_PRINTER		0x0080
#define PRT_CMD_STOP_PRINTER		0x0040
#define PRT_CMD_START_CARRIAGE		0x0004
#define PRT_CMD_STOP_CARRIAGE		0x0002
#define PRT_CMD_SPACE				0x0001

#define PRT_CMD_MASK				0x00C7

extern const char * saywhere (int addr);

static void mytrace (int start, const char *what)
{
	const char *where;

	if ((where = saywhere(prev_IAR)) == NULL) where = "?";
	trace_io("%s %s at %04x: %s", start ? "start" : "stop", what, prev_IAR, where);
}

/* xio_1132_printer - XIO command interpreter for the 1132 printer */

void xio_1132_printer (int32 iocc_addr, int32 func, int32 modify)
{
	char msg[80];
	UNIT *uptr = &prt_unit[0];

	switch (func) {
		case XIO_READ:
			M[iocc_addr & mem_mask] = codewheel1132[prt_nchar].ebcdic << 8;

			if ((uptr->flags & UNIT_PRINTING) == 0)				/* if we're not printing, advance this after every test */
				prt_nchar = (prt_nchar + 1) % WHEELCHARS_1132;
			break;

		case XIO_SENSE_DEV:
			ACC = PRT_DSW;
			if (modify & 0x01) {								/* reset interrupts */
				CLRBIT(PRT_DSW, PRT1132_DSW_READ_EMITTER_RESPONSE | PRT1132_DSW_SKIP_RESPONSE | PRT1132_DSW_SPACE_RESPONSE);
				CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
			}
			trace_io("* Printer DSW %04x mod %x", ACC, modify);
			break;

		case XIO_CONTROL:
			if (modify & PRT_CMD_START_PRINTER) {
				SETBIT(uptr->flags, UNIT_PRINTING);
				if (DO_TRACE(uptr)) mytrace(1, "printing");
			}

			if (modify & PRT_CMD_STOP_PRINTER) {
				CLRBIT(uptr->flags, UNIT_PRINTING);
				if (DO_TRACE(uptr)) mytrace(0, "printing");
			}

			if (modify & PRT_CMD_START_CARRIAGE) {
				SETBIT(uptr->flags, UNIT_SKIPPING);
				if (DO_TRACE(uptr))	mytrace(1, "skipping");
			}

			if (modify & PRT_CMD_STOP_CARRIAGE) {
				CLRBIT(uptr->flags, UNIT_SKIPPING);
				if (DO_TRACE(uptr))	mytrace(0, "skipping");
			}

			if (modify & PRT_CMD_SPACE) {
				SETBIT(uptr->flags, UNIT_SPACING);
				if (DO_TRACE(uptr))	mytrace(1, "space");
			}

			sim_cancel(uptr);
			if (uptr->flags & (UNIT_SKIPPING|UNIT_SPACING|UNIT_PRINTING)) {		/* busy bits = doing something */
				SETBIT(PRT_DSW, PRT1132_DSW_PRINTER_BUSY);
				sim_activate(uptr, prt_cwait);
			}
			else
				CLRBIT(PRT_DSW, PRT1132_DSW_PRINTER_BUSY);

			if (uptr->flags & (UNIT_SKIPPING|UNIT_SPACING))
				SETBIT(PRT_DSW, PRT1132_DSW_CARRIAGE_BUSY);
			else
				CLRBIT(PRT_DSW, PRT1132_DSW_CARRIAGE_BUSY);
			
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

#define SET_ACTION(u,a) {(u)->flags &= ~(UNIT_SKIPPING|UNIT_SPACING|UNIT_PRINTING|UNIT_TRANSFERRING); (u)->flags |= a;}

static t_stat prt_svc (UNIT *uptr)
{
	return IS_1403(uptr) ? prt1403_svc(uptr) : prt1132_svc(uptr);
}

/* prt1132_svc - emulated timeout for 1132 operation */

static t_stat prt1132_svc (UNIT *uptr)
{
	if (PRT_DSW & PRT1132_DSW_NOT_READY) {					/* cancel operation if printer went offline */
		if (DO_TRACE(uptr))	trace_io("1132 form check");
		SETBIT(uptr->flags, UNIT_FORMCHECK);
		SET_ACTION(uptr, 0);
		forms_check(TRUE);									/* and turn on forms check lamp */
		return SCPE_OK;
	}

	if (uptr->flags & UNIT_SPACING) {
		flush_prt_line(uptr->fileref, UNIT_SPACING, IS_PHYSICAL(uptr));

		CLRBIT(PRT_DSW, PRT1132_DSW_CHANNEL_MASK|PRT1132_DSW_PRINTER_BUSY|PRT1132_DSW_CARRIAGE_BUSY);
		SETBIT(PRT_DSW, cc_format_1132(cctape[prt_row]) | PRT1132_DSW_SPACE_RESPONSE);
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);
		CLRBIT(uptr->flags, UNIT_SPACING);					/* done with this */
		calc_ints();
	}

	if (uptr->flags & UNIT_SKIPPING) {
		do {
			flush_prt_line(uptr->fileref, UNIT_SKIPPING, IS_PHYSICAL(uptr));
			CLRBIT(PRT_DSW, PRT1132_DSW_CHANNEL_MASK);
			SETBIT(PRT_DSW, cc_format_1132(cctape[prt_row]));
		} while ((cctape[prt_row] & CC_1132_BITS) == 0);			/* slew directly to a cc tape punch */

		SETBIT(PRT_DSW, cc_format_1132(cctape[prt_row]) | PRT1132_DSW_SKIP_RESPONSE);
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);
		calc_ints();
	}

	if (uptr->flags & UNIT_PRINTING) {
		if (! save_1132_prt_line(codewheel1132[prt_nchar].ascii)) {	/* save previous printed line */
			trace_io("* Print check -- buffer not set in time");
			SETBIT(uptr->flags, UNIT_DATACHECK);					/* buffer wasn't set in time */
			SET_ACTION(uptr, 0);
			print_check(TRUE);										/* and turn on forms check lamp */

/*	if (running)
		reason = STOP_IMMEDIATE;	// halt on check
*/

			return SCPE_OK;
		}

		prt_nchar = (prt_nchar + 1) % WHEELCHARS_1132;				/* advance print drum */

		SETBIT(PRT_DSW, PRT1132_DSW_READ_EMITTER_RESPONSE);			/* issue interrupt to tell printer to set buffer */
		SETBIT(ILSW[1], ILSW_1_1132_PRINTER);						/* we'll save the printed stuff just before next emitter response (later than on real 1130) */
		calc_ints();
	}

	if (uptr->flags & (UNIT_SPACING|UNIT_SKIPPING|UNIT_PRINTING)) {	/* still doing something */
		SETBIT(PRT_DSW, PRT1132_DSW_PRINTER_BUSY);
		sim_activate(uptr, prt_cwait);
	}
	else
		CLRBIT(PRT_DSW, PRT1132_DSW_PRINTER_BUSY);

	return SCPE_OK;
}

void save_1403_prt_line (int32 addr)
{
	size_t j;
	int i, r, ch, even = TRUE;
	unsigned char ebcdic;
	int32 wd;

	for (i = 0; i < PRT1403_COLUMNS; i++) {
		if (even) {										/* fetch next word from memory */
			wd     = M[addr++];
			ebcdic = (unsigned char) ((wd >> 8) & 0x7F);
			even   = FALSE;
		}
		else {
			ebcdic = (unsigned char) (wd & 0x7F);		/* use low byte of previously fetched word */
			even   = TRUE;
		}

		ch = ' ';										/* translate ebcdic to ascii. Don't bother checking for parity errors */
		for (j = 0; j < WHEELCHARS_1403; j++) {
			if (codewheel1403[j].ebcdic == ebcdic) {
				ch = codewheel1403[j].ascii;
				break;
			}
		}

		if (ch > ' ') {
			if ((r = nprint[i]) < MAX_OVPRINT) {
				if (ncol[r] <= i) {						/* we haven't moved this far yet */
					if (ncol[r] == 0)					/* first char in this row? */
						memset(prtbuf+r*MAX_COLUMNS, ' ', PRT1403_COLUMNS);	/* blank out the new row */
					ncol[r] = i+1;											/* remember new row length */
				}
				prtbuf[r*MAX_COLUMNS + i] = (char) ch;	/* save the character */

				nprint[i]++;							/* remember max overprintings for this column */
				maxnp = MAX(maxnp, nprint[i]);
			}
		}
	}
}

void xio_1403_printer (int32 iocc_addr, int32 func, int32 modify)
{
	UNIT *uptr = &prt_unit[0];

	switch (func) {
		case XIO_INITW:												/* print a line */
			save_1403_prt_line(iocc_addr);							/* put formatted line into our print buffer */

			SETBIT(uptr->flags, UNIT_TRANSFERRING);					/* schedule transfer complete interrupt */
			SETBIT(PRT_DSW, PRT1403_DSW_PRINTER_BUSY);
			sim_activate(uptr, prt_twait);
			break;

		case XIO_CONTROL:											/* initiate single space */
			if (uptr->flags & UNIT_SKIPPING) {
				xio_error("1403 printer skip and space at same time?");
			}
			else {
				SETBIT(uptr->flags, UNIT_SPACING);
				SETBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_BUSY);
				sim_activate(uptr, prt_fwait);
			}
			break;

		case XIO_WRITE:												/* initiate skip */
			if (uptr->flags & UNIT_SPACING) {
				xio_error("1403 printer skip and space at same time?");
			}
			else {
				SETBIT(uptr->flags, UNIT_SKIPPING);
				SKIPTARGET = ReadW(iocc_addr) & CC_1403_BITS;		/* get CC bits that we're to match */
				SETBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_BUSY);
				sim_activate(uptr, prt_fwait);
			}
			break;

		case XIO_SENSE_DEV:											/* get device status word */
			ACC = PRT_DSW;
			if (modify & 0x01) {									/* reset interrupts */
				CLRBIT(PRT_DSW, PRT1403_DSW_PARITY_CHECK   | PRT1403_DSW_TRANSFER_COMPLETE |
								PRT1403_DSW_PRINT_COMPLETE | PRT1403_DSW_CARRIAGE_COMPLETE | 
								PRT1403_DSW_RING_CHECK     | PRT1403_DSW_SYNC_CHECK);
				CLRBIT(ILSW[4], ILSW_4_1403_PRINTER);
			}
			break;
	}
}

static t_stat prt1403_svc(UNIT *uptr)
{
	if (PRT_DSW & PRT1403_DSW_NOT_READY) {					/* cancel operation if printer went offline */
		SET_ACTION(uptr, 0);
		if (DO_TRACE(uptr))	trace_io("1403 form check");
		forms_check(TRUE);									/* and turn on forms check lamp */
	}
	else if (uptr->flags & UNIT_TRANSFERRING) {				/* end of transfer */
		CLRBIT(uptr->flags, UNIT_TRANSFERRING);
		SETBIT(uptr->flags, UNIT_PRINTING);					/* schedule "print complete" */

		SETBIT(PRT_DSW, PRT1403_DSW_TRANSFER_COMPLETE);		/* issue transfer complete interrupt */
		SETBIT(ILSW[4], ILSW_4_1403_PRINTER);
	}
	else if (uptr->flags & UNIT_PRINTING) {
		CLRBIT(uptr->flags, UNIT_PRINTING);
		CLRBIT(PRT_DSW, PRT1403_DSW_PRINTER_BUSY);

		SETBIT(PRT_DSW, PRT1403_DSW_PRINT_COMPLETE);
		SETBIT(ILSW[4], ILSW_4_1403_PRINTER);				/* issue print complete interrupt */
	}
	else if (uptr->flags & UNIT_SKIPPING) {
		do {												/* find line with exact match of tape punches */
			flush_prt_line(uptr->fileref, UNIT_SKIPPING, IS_PHYSICAL(uptr));
		} while (cctape[prt_row] != SKIPTARGET);			/* slew directly to requested cc tape punch */

		CLRBIT(uptr->flags, UNIT_SKIPPING);					/* done with this */
		CLRBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_BUSY);

		SETBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_COMPLETE);
		SETBIT(ILSW[4], ILSW_4_1403_PRINTER);
	}
	else if (uptr->flags & UNIT_SPACING) {
		flush_prt_line(uptr->fileref, UNIT_SPACING, IS_PHYSICAL(uptr));

		CLRBIT(uptr->flags, UNIT_SPACING);					/* done with this */
		CLRBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_BUSY);

		SETBIT(PRT_DSW, PRT1403_DSW_CARRIAGE_COMPLETE);
		SETBIT(ILSW[4], ILSW_4_1403_PRINTER);
	}

	if (uptr->flags & (UNIT_PRINTING|UNIT_SKIPPING|UNIT_SPACING|UNIT_TRANSFERRING))
		sim_activate(uptr, prt_fwait);

	CLRBIT(PRT_DSW, PRT1403_DSW_CH9|PRT1403_DSW_CH12);		/* set the two CC bits in the DSW */
	if (cctape[prt_row] & CC_CHANNEL_9)
		SETBIT(PRT_DSW, PRT1403_DSW_CH9);
	if (cctape[prt_row] & CC_CHANNEL_12)
		SETBIT(PRT_DSW, PRT1403_DSW_CH12);

	calc_ints();
	return SCPE_OK;
}

/* delete_cmd - SCP command to delete a file */

static t_stat delete_cmd (int32 flag, CONST char *cptr)
{
	char gbuf[CBUFSIZE];
	int status;

	cptr = get_glyph (cptr, gbuf, 0);			/* get next glyph */
	if (*gbuf == 0) return SCPE_2FARG;
	if (*cptr != 0) return SCPE_2MARG;			/* now eol? */

	status = remove(gbuf);						/* delete the file */

	if (status != 0 && errno != ENOENT)			/* print message if failed and file exists */
		sim_perror(gbuf);

	return SCPE_OK;
}

/* prt_reset - reset emulated printer */

static t_stat prt_reset (DEVICE *dptr)
{
	UNIT *uptr = &prt_unit[0];
	size_t i;

/* add a DELETE filename command so we can be sure to have clean listings */
	register_cmd("DELETE", &delete_cmd, 0, "del{ete} filename        remove file\n");

	sim_cancel(uptr);

	memset(cctape, 0, sizeof(cctape));			/* copy punch list into carriage control tape image */

	if (cgi) {
		for (i = 0; i < (sizeof(cccgi)/sizeof(cccgi[0])); i++)
			cctape[cccgi[i].row-1] |= cccgi[i].channels;
	}
	else
		for (i = 0; i < (sizeof(ccpunches)/sizeof(ccpunches[0])); i++)
			cctape[ccpunches[i].row-1] |= ccpunches[i].channels;

	prt_nchar = 0;
	prt_row   = 0;
	prt_nnl   = 0;

	CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK|UNIT_PRINTING|UNIT_SPACING|UNIT_SKIPPING|
						UNIT_TRANSFERRING|UNIT_PARITYCHECK|UNIT_RINGCHECK|UNIT_SYNCCHECK);

	if (IS_1132(uptr)) {
		CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
		PRT_DSW = cc_format_1132(cctape[prt_row]);
		if (! IS_ONLINE(uptr))
			SETBIT(PRT_DSW, PRT1132_DSW_NOT_READY);
	}
	else {
		CLRBIT(ILSW[4], ILSW_4_1403_PRINTER);
		PRT_DSW = 0;
		if (cctape[prt_row] & CC_CHANNEL_9)
			SETBIT(PRT_DSW, PRT1403_DSW_CH9);
		if (cctape[prt_row] & CC_CHANNEL_12)
			SETBIT(PRT_DSW, PRT1403_DSW_CH12);
		if (! IS_ONLINE(uptr))
			SETBIT(PRT_DSW, PRT1403_DSW_NOT_READY);
	}

	SET_ACTION(uptr, 0);
	calc_ints();
	reset_prt_line();

	forms_check(FALSE);
	return SCPE_OK;
}

static t_stat prt_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat rval;
    char gbuf[2*CBUFSIZE];
														/* assume failure */
	SETBIT(PRT_DSW, IS_1132(uptr) ? PRT1132_DSW_NOT_READY : PRT1403_DSW_NOT_READY);
	formfed = FALSE;

	if (uptr->flags & UNIT_ATT) {
		if ((rval = prt_detach(uptr)) != SCPE_OK) {
			return rval;
		}
	}

	if (sim_switches & SWMASK('P'))						/* set physical (unbuffered) printer flag */
		SETBIT(uptr->flags, UNIT_PHYSICAL_PTR);
	else
		CLRBIT(uptr->flags, UNIT_PHYSICAL_PTR);

	sim_cancel(uptr);

	if (strcmp(cptr, "(stdout)") == 0) {				/* connect printer to stdout */
		if (uptr -> flags & UNIT_DIS) return SCPE_UDIS;	/* disabled? */
		uptr->filename = (char *)calloc(CBUFSIZE, sizeof(char));
		strcpy(uptr->filename, "(stdout)");
	    uptr->fileref = stdout;
		SETBIT(uptr->flags, UNIT_ATT);
		uptr->pos = 0;
	}
	else {
		if ((rval = attach_unit(uptr, quotefix(cptr, gbuf))) != SCPE_OK)
			return rval;
	}

	fseek(uptr->fileref, 0, SEEK_END);					/* if we opened an existing file, append to it */
	uptr->pos = ftell(uptr->fileref);

	if (IS_1132(uptr)) {
		CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
		CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK);
	}
	else {
		CLRBIT(ILSW[4], ILSW_4_1403_PRINTER);
		CLRBIT(uptr->flags, UNIT_PARITYCHECK|UNIT_RINGCHECK|UNIT_SYNCCHECK);
	}

	SET_ACTION(uptr, 0);
	calc_ints();

	prt_nchar = 0;
	prt_nnl   = 0;
	prt_row   = 0;
	reset_prt_line();

	if (IS_1132(uptr)) {
		PRT_DSW = (PRT_DSW & ~PRT1132_DSW_CHANNEL_MASK) | cc_format_1132(cctape[prt_row]);

		if (IS_ONLINE(uptr))
			CLRBIT(PRT_DSW, PRT1132_DSW_NOT_READY);
	}
	else {
		CLRBIT(PRT_DSW, PRT1403_DSW_CH9 | PRT1403_DSW_CH12);
		if (cctape[prt_row] & CC_CHANNEL_9)
			SETBIT(PRT_DSW, PRT1403_DSW_CH9);
		if (cctape[prt_row] & CC_CHANNEL_12)
			SETBIT(PRT_DSW, PRT1403_DSW_CH12);

		if (IS_ONLINE(uptr))
			CLRBIT(PRT_DSW, PRT1403_DSW_NOT_READY);		/* fixed by Carl Claunch */
	}

	forms_check(FALSE);

	return SCPE_OK;
}

static t_stat prt_detach (UNIT *uptr)
{
	t_stat rval;

	if (uptr->flags & UNIT_ATT)
		flush_prt_line(uptr->fileref, TRUE, TRUE);

	if (uptr->fileref == stdout) {
		CLRBIT(uptr->flags, UNIT_ATT);
		free(uptr->filename);
		uptr->filename = NULL;
	}
	else if ((rval = detach_unit(uptr)) != SCPE_OK)
		return rval;

	sim_cancel(uptr);

	if (IS_1132(uptr)) {
		CLRBIT(ILSW[1], ILSW_1_1132_PRINTER);
		CLRBIT(uptr->flags, UNIT_FORMCHECK|UNIT_DATACHECK);
		SETBIT(PRT_DSW, PRT1132_DSW_NOT_READY);
	}
	else {
		CLRBIT(ILSW[4], ILSW_4_1403_PRINTER);
		SETBIT(PRT_DSW, PRT1403_DSW_NOT_READY);
	}
	SET_ACTION(uptr, 0);

	calc_ints();

	forms_check(FALSE);
	return SCPE_OK;
}

