#include "ibm1130_defs.h"

/* ibm1130_cr.c: IBM 1130 1442 Card Reader simulator

   Based on the SIMH package written by Robert M Supnik

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org

NOTE - there is a problem with this code. The Device Status Word (DSW) is
computed from current conditions when requested by an XIO load status
command; the value of DSW available to the simulator's examine & save
commands may NOT be accurate. This should probably be fixed.

*  Update 2002-02-29: Added deck-list option. If you issue an attach
   command and specify the filename as "@filename", the named file is interpreted
   as a list of filenames to be read in sequence; the effect is that the reader
   sees the concatenation of all of the files named.  "reset" rewinds the deck
   list. Filenames can be followed by whitespace and the letter "a" or "b",
   which indicates "ascii to 029" or "binary", respectively. Example:
   
       attach cr @decklist

   where file "decklist" contains:
  
       file01 a
	   file02 b
	   file03 b
	   file04 b

   If "a" or "b" is not specified, the device mode setting is used.

   ('a' means 029, so, if you need 026 coding, specify the
   device default as the correct 026 code and omit the 'a' on the text files lines).

*  note: I'm not sure but I think we'll need a way to simulate the 'start'
   button. What may end up being necessary is to fake this in the 'attach'
   command. In a GUI build we may want to wait until they press a button.
   Have to research: does DMS issue a read request which is only
   satisfied when START is pressed, or does START cause an interrupt that
   then asks DMS to issue a read request. I think it's the former but need
   to check. After all the status register says "empty" and "not ready"
   when the hopper is empty. So what gives? On the 360 I think the start
   button causes issues some sort of attention request.
   
*  Card image format.
   Card files can be ascii text or binary.  There are several ASCII modes:
   CODE_029, CODE_26F, etc, corresponding to different code sets.
   Punch and reader modes can be set independently.

   The 1442 card read/punch has several cycles:

   feed cycle:	moves card from hopper to read station
   					  card from read station to punch station
					  card from punch station to stacker

   read or punch: operates on card at read or punch station (but not both).

   The simulator requires input cards to be read from the file attached
   to the card reader unit. A feed cycle reads one line (text mode) or
   160 bytes (binary mode) from the input file to the read station buffer,
   copies the read station buffer to the punch station buffer, and if
   the punch unit is attached to a file, writes the punch station buffer to
   the output file.
   
   The read and punch cycles operate on the appropriate card buffer.

   Detaching the card punch flushes the punch station buffer if necessary.

   As does the 1442, a read or punch cycle w/o a feed cycle causes a
   feed cycle first.

   A feed cycle on an empty deck (reader unattaced or at EOF) clears
   the appropriate buffer, so you can punch w/o attaching a deck to
   the card reader.

// -- this may need changing depending on how things work in hardware. TBD.
||  A read cycle on an empty deck causes an error.
||  Hmmm -- what takes the place of the Start button on
\\ the card reader?

  Binary format is stored using fxwrite of short ints, in this format:

     1 1
	 2 2 0 1 2 3 4 5 6 7 8 9
	 * * * * * * * * * * * * 0 0 0 0

            MSB                                LSB
   byte 0   [ 6] [ 7] [ 8] [ 9]   0    0    0    0
   byte 1   [12] [11] [ 0] [ 1] [ 2] [ 3] [ 4] [ 5] 

   This means we can read words (little endian) and get this in memory:

       12 11 0 1 2 3 4 5 6 7 8 9 - - - -

   which is what the 1130 sees.

   ASCII can be read in blocks of 80 characters but can be terminated by newline prematurely.

   Booting: card reader IPL loads 80 columns (1 card) into memory starting
   at location 0 in a split fashion:

        ________________ _ _ _
       /
   12 |
   11 |
    0 |
    1 |
    2 |
    3 | Punched card
    4 |
    5 |
    6 |
    7 |
    8 |
    9 |
	  +------------------ - - -

	   12 11  0  1  2            3   4  5  6  7  8  9 
	    |  |  |  |  |  0  0  0  / \  |  |  |  |  |  |
	  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	  | 0| 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|
	  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
	  |  OPCODE      | F| Tag |  DISPLACEMENT		  |
	  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The zeros mean that all IPL instructions are short form,
   nonindexed. The 3 column is repeated in bits 8 and 9 so
   it's a sign bit.

   Boot command on a binary deck does this. Boot on an unattached
   reader loads the standard boot2 card image. Boot with an ASCII
   deck will not be very helpful.
*/

#define READ_DELAY		 35			// see how small a number we can get away with
#define PUNCH_DELAY		 35
#define FEED_DELAY		 25

// #define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

extern int32 sim_switches;

static t_stat cr_svc      (UNIT *uptr);
static t_stat cr_reset    (DEVICE *dptr);
static t_stat cr_set_code (UNIT *uptr, int32 match);
static t_stat cr_attach   (UNIT *uptr, char *cptr);
static t_stat cr_detach   (UNIT *uptr);

static t_stat cp_reset	  (DEVICE *dptr);
static t_stat cp_set_code (UNIT *uptr, int32 match);
static t_stat cp_detach   (UNIT *uptr);

static int16 cr_dsw  = 0;									/* device status word */
static int32 cr_wait = READ_DELAY;							/* read per-column wait */
static int32 cf_wait = PUNCH_DELAY;							/* punch per-column wait */
static int32 cp_wait = FEED_DELAY;							/* feed op wait */

#define UNIT_V_OPERATION   (UNIT_V_UF + 0)					/* operation in progress */
#define UNIT_V_CODE		   (UNIT_V_UF + 2)
#define UNIT_V_EMPTY	   (UNIT_V_UF + 4)
#define UNIT_V_SCRATCH	   (UNIT_V_UF + 5)
#define UNIT_V_QUIET       (UNIT_V_UF + 6)
#define UNIT_V_DEBUG       (UNIT_V_UF + 7)

#define UNIT_V_LASTPUNCH   (UNIT_V_UF + 0)					/* bit in unit_cp flags */

#define UNIT_OP			 (3u << UNIT_V_OPERATION)			/* two bits */
#define UNIT_CODE		 (3u << UNIT_V_CODE)				/* two bits */
#define UNIT_EMPTY		 (1u << UNIT_V_EMPTY)
#define UNIT_SCRATCH	 (1u << UNIT_V_SCRATCH)				/* temp file */
#define UNIT_QUIET       (1u << UNIT_V_QUIET)
#define UNIT_DEBUG       (1u << UNIT_V_DEBUG)

#define UNIT_LASTPUNCH	 (1u << UNIT_V_LASTPUNCH)

#define OP_IDLE		 	 (0u << UNIT_V_OPERATION)
#define OP_READING	 	 (1u << UNIT_V_OPERATION)
#define OP_PUNCHING	 	 (2u << UNIT_V_OPERATION)
#define OP_FEEDING	 	 (3u << UNIT_V_OPERATION)

#define SET_OP(op) {cr_unit.flags &= ~UNIT_OP; cr_unit.flags |= op;}

#define CURRENT_OP (cr_unit.flags & UNIT_OP)

#define CODE_029 		 (0u << UNIT_V_CODE)
#define CODE_026F		 (1u << UNIT_V_CODE)
#define CODE_026C		 (2u << UNIT_V_CODE)
#define CODE_BINARY		 (3u << UNIT_V_CODE)

#define SET_CODE(un,cd) {un.flags &= ~UNIT_CODE; un.flags |= cd;}

#define COLUMN		u4										/* column field in unit record */

UNIT cr_unit = { UDATA (&cr_svc, UNIT_ATTABLE|UNIT_ROABLE, 0) };
UNIT cp_unit = { UDATA (NULL,    UNIT_ATTABLE, 0) };

MTAB cr_mod[] = {
	{ UNIT_CODE, CODE_029,		"029",		"029",		&cr_set_code},
	{ UNIT_CODE, CODE_026F,		"026F",		"026F",		&cr_set_code},
	{ UNIT_CODE, CODE_026C, 	"026C", 	"026C",		&cr_set_code},
	{ UNIT_CODE, CODE_BINARY,	"BINARY",	"BINARY",	&cr_set_code},
	{ 0 }  };

MTAB cp_mod[] = {
	{ UNIT_CODE, CODE_029,		"029",		"029",		&cp_set_code},
	{ UNIT_CODE, CODE_026F,		"026F",		"026F",		&cp_set_code},
	{ UNIT_CODE, CODE_026C, 	"026C", 	"026C",		&cp_set_code},
	{ UNIT_CODE, CODE_BINARY,	"BINARY",	"BINARY",	&cp_set_code},
	{ 0 }  };

REG cr_reg[] = {
	{ HRDATA (CRDSW,   cr_dsw,  16) },					/* device status word */
	{ DRDATA (CRTIME,  cr_wait, 24), PV_LEFT },			/* operation wait */
	{ DRDATA (CFTIME,  cf_wait, 24), PV_LEFT },			/* operation wait */
	{ NULL }  };

REG cp_reg[] = {
	{ DRDATA (CPTIME,  cp_wait, 24), PV_LEFT },			/* operation wait */
	{ NULL }  };

DEVICE cr_dev = {
	"CR", &cr_unit, cr_reg, cr_mod,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, cr_reset,
	cr_boot, cr_attach, cr_detach};

DEVICE cp_dev = {
	"CP", &cp_unit, cp_reg, cp_mod,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, cp_reset,
	NULL, NULL, cp_detach};

#define CR_DSW_READ_RESPONSE			0x8000		/* device status word bits */
#define CR_DSW_PUNCH_RESPONSE			0x4000
#define CR_DSW_ERROR_CHECK				0x2000
#define CR_DSW_LAST_CARD				0x1000
#define CR_DSW_OP_COMPLETE				0x0800
#define CR_DSW_FEED_CHECK				0x0100
#define CR_DSW_BUSY						0x0002
#define CR_DSW_NOT_READY				0x0001

typedef struct {
	int		hollerith;
	char	ascii;
} CPCODE;

static CPCODE cardcode_029[] =
{
	0x0000,		' ',
	0x8000, 	'&',	 		// + in 026 Fortran
	0x4000,		'-',
	0x2000,		'0',
	0x1000,		'1',
	0x0800,		'2',
	0x0400,		'3',
	0x0200,		'4',
	0x0100,		'5',
	0x0080,		'6',
	0x0040,		'7',
	0x0020,		'8',
	0x0010,		'9',
	0x9000, 	'A',
	0x8800,		'B',
	0x8400,		'C',
	0x8200,		'D',
	0x8100,		'E',
	0x8080,		'F',
	0x8040,		'G',
	0x8020,		'H',
	0x8010,		'I',
	0x5000, 	'J',
	0x4800,		'K',
	0x4400,		'L',
	0x4200,		'M',
	0x4100,		'N',
	0x4080,		'O',
	0x4040,		'P',
	0x4020,		'Q',
	0x4010,		'R',
	0x3000, 	'/',
	0x2800,		'S',
	0x2400,		'T',
	0x2200,		'U',
	0x2100,		'V',
	0x2080,		'W',
	0x2040,		'X',
	0x2020,		'Y',
	0x2010,		'Z',
	0x0820,		':',
	0x0420,		'#',		// = in 026 Fortran
	0x0220,		'@',		// ' in 026 Fortran
	0x0120,		'\'',
	0x00A0,		'=',
	0x0060,		'"',
	0x8820,		'c',		// cent
	0x8420,		'.',
	0x8220,		'<',		// ) in 026 Fortran
	0x8120,		'(',
	0x80A0,		'+',
	0x8060,		'|',
	0x4820,		'!',
	0x4420,		'$',
	0x4220,		'*',
	0x4120,		')',
	0x40A0,		';',
	0x4060,		'n',		// not
	0x2820,		'x',		// what?
	0x2420,		',',
	0x2220,		'%',		// ( in 026 Fortran
	0x2120,		'_',
	0x20A0,		'>',
	0x2060,		'>',
};

static CPCODE cardcode_026F[] =		// 026 fortran
{
	0x0000,		' ',
	0x8000, 	'+',
	0x4000,		'-',
	0x2000,		'0',
	0x1000,		'1',
	0x0800,		'2',
	0x0400,		'3',
	0x0200,		'4',
	0x0100,		'5',
	0x0080,		'6',
	0x0040,		'7',
	0x0020,		'8',
	0x0010,		'9',
	0x9000, 	'A',
	0x8800,		'B',
	0x8400,		'C',
	0x8200,		'D',
	0x8100,		'E',
	0x8080,		'F',
	0x8040,		'G',
	0x8020,		'H',
	0x8010,		'I',
	0x5000, 	'J',
	0x4800,		'K',
	0x4400,		'L',
	0x4200,		'M',
	0x4100,		'N',
	0x4080,		'O',
	0x4040,		'P',
	0x4020,		'Q',
	0x4010,		'R',
	0x3000, 	'/',
	0x2800,		'S',
	0x2400,		'T',
	0x2200,		'U',
	0x2100,		'V',
	0x2080,		'W',
	0x2040,		'X',
	0x2020,		'Y',
	0x2010,		'Z',
	0x0420,		'=',
	0x0220,		'\'',		// ' in 026 Fortran
	0x8420,		'.',
	0x8220,		')',
	0x4420,		'$',
	0x4220,		'*',
	0x2420,		',',
	0x2220,		'(',
};

static CPCODE cardcode_026C[] =		// 026 commercial
{
	0x0000,		' ',
	0x8000, 	'+',
	0x4000,		'-',
	0x2000,		'0',
	0x1000,		'1',
	0x0800,		'2',
	0x0400,		'3',
	0x0200,		'4',
	0x0100,		'5',
	0x0080,		'6',
	0x0040,		'7',
	0x0020,		'8',
	0x0010,		'9',
	0x9000, 	'A',
	0x8800,		'B',
	0x8400,		'C',
	0x8200,		'D',
	0x8100,		'E',
	0x8080,		'F',
	0x8040,		'G',
	0x8020,		'H',
	0x8010,		'I',
	0x5000, 	'J',
	0x4800,		'K',
	0x4400,		'L',
	0x4200,		'M',
	0x4100,		'N',
	0x4080,		'O',
	0x4040,		'P',
	0x4020,		'Q',
	0x4010,		'R',
	0x3000, 	'/',
	0x2800,		'S',
	0x2400,		'T',
	0x2200,		'U',
	0x2100,		'V',
	0x2080,		'W',
	0x2040,		'X',
	0x2020,		'Y',
	0x2010,		'Z',
	0x0420,		'=',
	0x0220,		'\'',		// ' in 026 Fortran
	0x8420,		'.',
	0x8220,		')',
	0x4420,		'$',
	0x4220,		'*',
	0x2420,		',',
	0x2220,		'(',
};

extern int cgi;
extern void sub_args (char *instr, char *tmpbuf, int32 maxstr, int32 nargs, char *arg[]);

static int16 ascii_to_card[256];

static CPCODE *cardcode;
static int ncardcode;
static int32 active_cr_code;			/* the code most recently specified */
static FILE *deckfile = NULL;
static char tempfile[128];
static int cardnum;
static int any_punched = 0;

#define MAXARG 80						/* saved arguments to attach command */
static char list_save[MAXARG][10], *list_arg[MAXARG];
static int list_nargs = 0;

static int16 punchstation[80];
static int16 readstation[80];
static enum {STATION_EMPTY, STATION_LOADED, STATION_READ, STATION_PUNCHED} punchstate = STATION_EMPTY, readstate = STATION_EMPTY;

static t_bool nextdeck (void);
static void checkdeck (void);

/* lookup_codetable - use code flag setting to get code table pointer and length */

static t_bool lookup_codetable (int32 match, CPCODE **pcode, int *pncode)
{
	switch (match) {
		case CODE_029:
			*pcode  = cardcode_029;
			*pncode = sizeof(cardcode_029) / sizeof(CPCODE);
			break;

		case CODE_026F:
			*pcode  = cardcode_026F;
			*pncode = sizeof(cardcode_026F) / sizeof(CPCODE);
			break;

		case CODE_026C:
			*pcode  = cardcode_026C;
			*pncode = sizeof(cardcode_026C) / sizeof(CPCODE);
			break;

		case CODE_BINARY:
			*pcode  = NULL;
			*pncode = 0;
			break;

		default:
			printf("Eek! Undefined code table index");
			return FALSE;
	}
	return TRUE;
}

t_stat set_active_cr_code (int match)
{
	CPCODE *code;
	int i, ncode;

	active_cr_code = match;

	if (! lookup_codetable(match, &code, &ncode))
		return SCPE_ARG;

	memset(ascii_to_card, 0, sizeof(ascii_to_card));

	for (i = 0; i < ncode; i++)		// set ascii to card code table
		ascii_to_card[code[i].ascii] = (int16) code[i].hollerith;

	return SCPE_OK;
}

t_stat cr_set_code (UNIT *uptr, int32 match)
{
	return set_active_cr_code(match);
}

t_stat cp_set_code (UNIT *uptr, int32 match)
{
	CPCODE *code;
	int ncode;

	if (! lookup_codetable(match, &code, &ncode))
		return SCPE_ARG;

	cardcode  = code;				// save code table for punch output
	ncardcode = ncode;

	return SCPE_OK;
}

t_stat load_cr_boot (int drvno)
{
	/* this is from the "boot2" cold start card. Columns have been */
	/* expanded already from 12 to 16 bits. */

	static unsigned short boot2_data[] = {
		0xc80a, 0x18c2, 0xd008, 0xc019, 0x8007, 0xd017, 0xc033, 0x100a,
		0xd031, 0x7015, 0x000c, 0xe800, 0x0020, 0x08f8, 0x4828, 0x7035,
		0x70fa, 0x4814, 0xf026, 0x2000, 0x8800, 0x9000, 0x9800, 0xa000,
		0xb000, 0xb800, 0xb810, 0xb820, 0xb830, 0xb820, 0x3000, 0x08ea,
		0xc0eb, 0x4828, 0x70fb, 0x9027, 0x4830, 0x70f8, 0x8001, 0xd000,
		0xc0f4, 0xd0d9, 0xc01d, 0x1804, 0xe8d6, 0xd0d9, 0xc8e3, 0x18d3,
		0xd017, 0x18c4, 0xd0d8, 0x9016, 0xd815, 0x90db, 0xe8cc, 0xd0ef,
		0xc016, 0x1807, 0x0035, 0x00d0, 0xc008, 0x1803, 0xe8c4, 0xd00f,
		0x080d, 0x08c4, 0x1003, 0x4810, 0x70d9, 0x3000, 0x08df, 0x3000,
		0x7010, 0x00d1, 0x0028, 0x000a, 0x70f3, 0x0000, 0x00d0, 0xa0c0
	};
	int i;

	if (drvno >= 0)					/* if specified, set toggle switches to disk drive no */
		CES = drvno;				/* so BOOT DSK1 will work correctly */

	IAR = 0;						/* clear IAR */

	for (i = 0; i < 80; i++)		/* copy memory */
		WriteW(i, boot2_data[i]);

#ifdef GUI_SUPPORT
	if (! cgi)
		remark_cmd("Loaded BOOT2 cold start card\n");
#endif
	return SCPE_OK;
}

t_stat cr_boot (int unitno)
{
	t_stat rval;
	short buf[80];
	int i;

	if ((rval = reset_all(0)) != SCPE_OK)
		return rval;

	if (! (cr_unit.flags & UNIT_ATT))			// no deck; load standard boot anyway
		return load_cr_boot(-1);

	if ((active_cr_code & UNIT_CODE) != CODE_BINARY) {
		printf("Can only boot from card reader when set to BINARY mode");
		return SCPE_IOERR;
	}

	if (fxread(buf, sizeof(short), 80, cr_unit.fileref) != 80)
		return SCPE_IOERR;

	IAR = 0;									/* Program Load sets IAR = 0 */

	for (i = 0; i < 80; i++)					/* shift 12 bits into 16 */
		WriteW(i, (buf[i] & 0xF800) | ((buf[i] & 0x0400) ? 0x00C0 : 0x0000) | ((buf[i] & 0x03F0) >> 4));

	return SCPE_OK;
}

char card_to_ascii (int16 hol)
{
	int i;

	for (i = 0; i < ncardcode; i++)
		if (cardcode[i].hollerith == hol)
			return cardcode[i].ascii;

	return ' ';
}

// hollerith_to_ascii - provide a generic conversion for simulator debugging 

char hollerith_to_ascii (int16 hol)
{
	int i;

	for (i = 0; i < ncardcode; i++)
		if (cardcode_029[i].hollerith == hol)
			return cardcode[i].ascii;

	return ' ';
}

/* feedcycle - move cards to next station */

static void feedcycle (t_bool load, t_bool punching)
{
	char buf[84], *x;
	int i, nread, nwrite, ch;

	/* write punched card if punch is attached to a file */
	if (cp_unit.flags & UNIT_ATT) {
		if (any_punched && punchstate != STATION_EMPTY) {
			if ((cp_unit.flags & UNIT_CODE) == CODE_BINARY) {
				fxwrite(punchstation, sizeof(short), 80, cp_unit.fileref);
			}
			else {
				for (i = 80; --i >= 0; ) {		/* find last nonblank column */
					if (buf[i] != 0)
						break;
				}

				/* i is now index of last character to output or -1 if all blank */

				for (nwrite = 0; nwrite <= i; nwrite++)	{			/* convert characters */
					buf[nwrite] = card_to_ascii(punchstation[nwrite]);
				}

				/* nwrite is now number of characters to output */

				buf[nwrite++] = '\n';				/* append newline */
				fxwrite(buf, sizeof(char), nwrite, cp_unit.fileref);
			}
		}
	}

	if (! load)			// all we wanted to do was flush the punch
		return;

	/* slide cards from reader to punch. If we know we're punching,
	 * generate a blank card in any case. Otherwise, it should take two feed
	 * cycles to get a read card from the hopper to punch station */

	if (readstate == STATION_EMPTY) {
		if (punching) {
			memset(punchstation, 0, sizeof(punchstation));
			punchstate = STATION_LOADED;
		}
		else
			punchstate = STATION_EMPTY;
	}
	else {
		memcpy(punchstation, readstation, sizeof(punchstation));
		punchstate = STATION_LOADED;
	}

	/* load card into read station */

again:		/* jump here if we've loaded a new deck after emptying the previous one */

	if (cr_unit.flags & UNIT_ATT) {

		memset(readstation, 0, sizeof(readstation));		/* blank out the card image */

		if (cr_unit.fileref == NULL)
			nread = 0;

		else if ((active_cr_code & UNIT_CODE) == CODE_BINARY)	/* binary read is straightforward */
			nread = fxread(readstation, sizeof(short), 80, cr_unit.fileref);

		else if (fgets(buf, sizeof(buf), cr_unit.fileref) == NULL)	/* read up to 80 chars */
			nread = 0;									/* hmm, end of file */

		else {											/* check for newline */
			if ((x = strchr(buf, '\r')) == NULL)
				x = strchr(buf, '\n');

			if (x == NULL) {							/* there were no delimiters, burn rest of line */
				while ((ch = getc(cr_unit.fileref)) != EOF) {	/* get character */
					if (ch == '\n')								/* newline, done */
						break;

					if (ch == '\r') {							/* CR, try to take newline too */
						ch = getc(cr_unit.fileref);
						if (ch != EOF && ch != '\n')			/* hmm, put it back */
							ungetc(ch, cr_unit.fileref);

						break;
					}
				}
				nread = 80;								/* take just the first 80 characters */
			}
			else
				nread = x-buf;							/* reduce length of string */

			upcase(buf);								/* force uppercase */

			for (i = 0; i < nread; i++)					/* convert ascii to punch code */
				readstation[i] = ascii_to_card[buf[i]];

			nread = 80;									/* even if line was blank consider it present */
		}

		if (nread <= 0) {								/* set hopper flag accordingly */
			if (deckfile != NULL && nextdeck())
				goto again;

			SETBIT(cr_unit.flags, UNIT_EMPTY);
			readstate = STATION_EMPTY;
			cardnum = -1;								/* nix the card counter */
		}
		else {
			CLRBIT(cr_unit.flags, UNIT_EMPTY);
			readstate = STATION_LOADED;
			cardnum++;									/* advance card counter */
		}
	}
	else
		readstate = STATION_EMPTY;

	cr_unit.COLUMN = -1;								/* neither device is currently cycling */
	cp_unit.COLUMN = -1;
}

#ifdef NO_USE_FOR_THIS_CURRENTLY

/* this routine should probably be hooked up to the GUI somehow */

/* NPRO - nonprocess runout, flushes out the reader/punch */

static void npro (void)
{
	if (cr_unit.flags & UNIT_ATT)
		fseek(cr_unit.fileref, 0, SEEK_END);		/* push reader to EOF */
	if (deckfile != NULL)
		fseek(deckfile, 0, SEEK_END);				/* skip to end of deck list */

	cardnum = -1;									/* nix the card counter */

	if (punchstate == STATION_PUNCHED)
		feedcycle(FALSE, FALSE);					/* flush out card just punched */

	readstate = punchstate = STATION_EMPTY;
	cr_unit.COLUMN = -1;							/* neither device is currently cycling */
	cp_unit.COLUMN = -1;
    SETBIT(cr_unit.flags, UNIT_EMPTY);				/* set hopper empty */
}

#endif

/* skipbl - skip leading whitespace in a string */

static char * skipbl (char *str)
{
	while (*str && *str <= ' ')
		str++;

	return str;
}

/* alltrim - remove all leading and trailing whitespace from a string */

static char * alltrim (char *str)
{
	char *s, *lastnb;

	if ((s = skipbl(str)) != str)			/* slide down over leading whitespace */
		strcpy(str, s);

	for (lastnb = str-1, s = str; *s; s++)	/* point to last nonblank characteter in string */
		if (*s > ' ')
			lastnb = s;

	lastnb[1] = '\0';						/* clip just after it */

	return str;
}

/* checkdeck - set hopper empty status based on condition of current reader file */

static void checkdeck (void)
{
	t_bool empty;

	if (cr_unit.fileref == NULL) {					/* there is no open file */
		empty = TRUE;
	}
	else {
		fseek(cr_unit.fileref, 0, SEEK_END);
		empty = ftell(cr_unit.fileref) <= 0;		/* see if file has anything) */
		fseek(cr_unit.fileref, 0, SEEK_SET);		/* rewind deck */
		cardnum = 0;								/* reset card counter */
	}

	if (empty) {
		SETBIT(cr_unit.flags, UNIT_EMPTY);
		if (cr_unit.fileref != NULL)				/* real file but it's empty, hmmm, try another */
			nextdeck();
	}
	else
		CLRBIT(cr_unit.flags, UNIT_EMPTY);
}

/* nextdeck - attempt to load a new file from the deck list into the hopper */

static t_bool nextdeck (void)
{
	char buf[200], tmpbuf[200], *fname, *mode, *tn;
	int code;
	long fpos;
	static char white[] = " \t\r\n";

	cardnum = 0;							/* reset card counter */

	if (deckfile == NULL)					/* we can't help */
		return FALSE;

	code = cr_unit.flags & UNIT_CODE;		/* default code */

	if (cr_unit.fileref != NULL) {			/* this pulls the rug out from under scp */
		fclose(cr_unit.fileref);			/* since the attach flag is still set. be careful! */
		cr_unit.fileref = NULL;

		if (cr_unit.flags & UNIT_SCRATCH) {
			unlink(tempfile);
			CLRBIT(cr_unit.flags, UNIT_SCRATCH);
		}
	}

	for (;;) {								/* get a filename */
		if (fgets(buf, sizeof(buf), deckfile) == NULL)
			break;							/* oops, no more names */

		alltrim(buf);
		if (! *buf)
			continue;						/* empty line */

		if (strnicmp(buf, "!BREAK", 6) == 0) {	/* stop the simulation */
			break_simulation(STOP_DECK_BREAK);
			continue;
		}

		if (buf[0] == '!') {				/* literal text line, make a temporary file */
			if (*tempfile == '\0') {
				if ((tn = tempnam(".", "1130")) == NULL) {
					printf("Cannot create temporary card file name\n");
					break_simulation(STOP_DECK_BREAK);
					return 0;
				}
				strcpy(tempfile, tn);
				strcat(tempfile, ".tmp");
			}

			if ((cr_unit.fileref = fopen(tempfile, "wb+")) == NULL) {
				printf("Cannot create temporary file %s\n", tempfile);
				break_simulation(STOP_DECK_BREAK);
				return 0;
			}

			SETBIT(cr_unit.flags, UNIT_SCRATCH);

			for (;;) {						/* store literal cards into temporary file */
				upcase(buf+1);
				fputs(buf+1, cr_unit.fileref);
				putc('\n', cr_unit.fileref);

				trace_io("(Literal card %s\n)", buf+1);
				if (! (cr_unit.flags & UNIT_QUIET))
					printf("(Literal card %s)\n", buf+1);

				fpos = ftell(deckfile);
				if (fgets(buf, sizeof(buf), deckfile) == NULL)
					break;					/* oops, end of file */
				if (buf[0] != '!' || strnicmp(buf, "!BREAK", 6) == 0)
					break;
				alltrim(buf);
			}
			fseek(deckfile, fpos, SEEK_SET);		/* restore deck file to just before non-literal card */

			fseek(cr_unit.fileref, 0, SEEK_SET);	/* rewind scratch file for reading */
			code = CODE_029;						/* assume keycode 029 */
			break;
		}

		sub_args(buf, tmpbuf, sizeof(buf), list_nargs, list_arg);	/* substitute in stuff from the attach command line */

		if ((fname = strtok(buf, white)) == NULL)
			continue;

		if (*fname == '#' || *fname == '*' || *fname == ';')
			continue;						/* comment */

		if ((mode = strtok(NULL, white)) != NULL) {
			if (*mode == 'b' || *mode == 'B') 
				code = CODE_BINARY;
			else if (*mode == 'a' || *mode == 'A')
				code = CODE_029;
		}

		if ((cr_unit.fileref = fopen(fname, "rb")) == NULL)
			printf("File '%s' specified in deck file '%s' cannot be opened\n", fname, cr_unit.filename+1);
		else {
			trace_io("(Opened %s deck %s)\n", (code == CODE_BINARY) ? "binary" : "text", fname);
			if (! (cr_unit.flags & UNIT_QUIET))
				printf("(Opened %s deck %s)\n", (code == CODE_BINARY) ? "binary" : "text", fname);
			break;
		}
	}

	checkdeck();

	set_active_cr_code(code);					/* set specified code */

	return (cr_unit.flags & UNIT_EMPTY) == 0;	/* return TRUE if a deck has been loaded */
}

static t_stat cr_reset (DEVICE *dptr)
{
	cr_set_code(&cr_unit, active_cr_code & UNIT_CODE);	/* reset to specified code table */

	readstate  = STATION_EMPTY;

	cr_dsw = 0;
	sim_cancel(&cr_unit);							/* cancel any pending ops */
	calc_ints();

	SET_OP(OP_IDLE);

	SETBIT(cr_unit.flags, UNIT_EMPTY);				/* assume hopper empty */

	if (cr_unit.flags & UNIT_ATT) {
//		if (deckfile != NULL) {
//			fseek(deckfile, 0, SEEK_SET);
//			nextdeck();
//		}
//		else 
//			checkdeck();

		if (cr_unit.fileref != NULL)
			feedcycle(FALSE, FALSE);
	}

	cr_unit.COLUMN = -1;							/* neither device is currently cycling */

	return SCPE_OK;
}

static t_stat cp_reset (DEVICE *dptr)
{
	cp_set_code(&cp_unit, cp_unit.flags & UNIT_CODE);
	punchstate = STATION_EMPTY;

	cp_unit.COLUMN = -1;
	return SCPE_OK;
}

static t_stat cr_attach (UNIT *uptr, char *cptr)
{
	t_stat rval;
	t_bool use_decklist;
	char *c, *arg, quote;

// no - don't cancel pending read?
//	sim_cancel(uptr);								/* cancel pending operations */


	CLRBIT(uptr->flags, UNIT_QUIET|UNIT_DEBUG);		/* set debug/quiet flags */
	if (sim_switches & SWMASK('D')) SETBIT(uptr->flags, UNIT_DEBUG);
	else if (sim_switches & SWMASK('Q')) SETBIT(uptr->flags, UNIT_QUIET);

	cr_detach(uptr);								/* detach file and possibly deckfile */
	CLRBIT(uptr->flags, UNIT_SCRATCH);

	c = cptr;
	for (list_nargs = 0; list_nargs < 10; ) {		/* extract arguments */
		while (*c && (*c <= ' '))					/* skip blanks */
			c++;
		if (! *c)
			break;									/* all done */

		arg = c;									/* save start */

		while (*c && (*c > ' ')) {
		    if (*c == '\'' || *c == '"') {			/* quoted string */
				for (quote = *c++; *c;)
					if (*c++ == quote)
						break;
			}
			else c++;
		}
		if (*c)
			*c++ = 0;								/* term arg at space */

		list_arg[list_nargs] = list_save[list_nargs];	/* set pointer to permanent storage location */
		strncpy(list_arg[list_nargs++], arg, MAXARG);	/* store copy */
	}

	if (list_nargs <= 0)							/* need at least 1 */
		return SCPE_2FARG;

	cptr = list_arg[0];								/* filename is first argument */

	use_decklist = (*cptr == '@');					/* filename starts with @: it's a deck list */
	if (use_decklist)
		cptr++;

	if (strcmp(cptr, "-") == 0 && ! use_decklist) {			/* standard input */
		if (uptr -> flags & UNIT_DIS) return SCPE_UDIS;		/* disabled? */
		uptr->filename = calloc(CBUFSIZE, sizeof(char));
		strcpy(uptr->filename, "(stdin)");
	    uptr->fileref = stdin;
		SETBIT(uptr->flags, UNIT_ATT);
		uptr->pos = 0;
	}
	else if ((rval = attach_unit(uptr, cptr)) != SCPE_OK)
		return rval;

	if (use_decklist) {								/* if we skipped the '@', store the actually-specified name */
		strncpy(uptr->filename, cptr-1, CBUFSIZE);
		deckfile = cr_unit.fileref;					/* save the deck file stream in our local variable */
		cr_unit.fileref  = NULL;
		nextdeck();
	}
	else
		checkdeck();

	// there is a read pending. Pull the card in to make it go
	if (CURRENT_OP == OP_READING || CURRENT_OP == OP_PUNCHING || CURRENT_OP == OP_FEEDING)
		feedcycle(TRUE, CURRENT_OP == OP_PUNCHING);

// no - don't reset the reader
//	cr_reset(&cr_dev);								/* reset the whole thing */
//	cp_reset(&cp_dev);

	cardnum = 0;									/* reset card counter */

	return SCPE_OK;
}

static t_stat cr_detach   (UNIT *uptr)
{
	t_stat rval;

	if (cr_unit.flags & UNIT_ATT && deckfile != NULL) {
		if (cr_unit.fileref != NULL)			/* close the active card deck */
			fclose(cr_unit.fileref);

		if (cr_unit.flags & UNIT_SCRATCH) {
			unlink(tempfile);
			CLRBIT(cr_unit.flags, UNIT_SCRATCH);
		}

		cr_unit.fileref = deckfile;				/* give scp a file to close */
	}

	if (uptr->fileref == stdout) {
		CLRBIT(uptr->flags, UNIT_ATT);
		free(uptr->filename);
		uptr->filename = NULL;
		rval = SCPE_OK;
	}
	else
		rval = detach_unit(uptr);

	return rval;
}

static t_stat cp_detach   (UNIT *uptr)
{
	if (cp_unit.flags & UNIT_ATT)
		if (punchstate == STATION_PUNCHED)
			feedcycle(FALSE, FALSE);			/* flush out card just punched */

	any_punched = 0;							/* reset punch detected */

	return detach_unit(uptr);
}

static void op_done (void)
{
	if (cr_unit.flags & UNIT_DEBUG)
		DEBUG_PRINT("!CR Op Complete, card %d", cardnum);

	SET_OP(OP_IDLE);
	SETBIT(cr_dsw, CR_DSW_OP_COMPLETE);
	SETBIT(ILSW[4], ILSW_4_1442_CARD);
	calc_ints();
}

static t_stat cr_svc (UNIT *uptr)
{
	switch (CURRENT_OP) {
		case OP_IDLE:
			break;

		case OP_FEEDING:
			op_done();
			break;

		case OP_READING:
			if (readstate == STATION_EMPTY) {			/* read active but no cards? hang */
				sim_activate(&cr_unit, cf_wait);
				break;
			}

			if (++cr_unit.COLUMN < 80) {
				SETBIT(cr_dsw, CR_DSW_READ_RESPONSE);
				SETBIT(ILSW[0], ILSW_0_1442_CARD);
				calc_ints();
				sim_activate(&cr_unit, cr_wait);
				if (cr_unit.flags & UNIT_DEBUG)
					DEBUG_PRINT("!CR Read Response %d : %d", cardnum, cr_unit.COLUMN+1);
			}
			else {
				readstate = STATION_READ;
				op_done();
			}
			break;

		case OP_PUNCHING:
			if (punchstate == STATION_EMPTY) {			/* punch active but no cards? hang */
				sim_activate(&cr_unit, cf_wait);
				break;
			}

			if (cp_unit.flags & UNIT_LASTPUNCH) {
				punchstate = STATION_PUNCHED;
				op_done();
			}
			else {
				SETBIT(cr_dsw, CR_DSW_PUNCH_RESPONSE);
				SETBIT(ILSW[0], ILSW_0_1442_CARD);
				calc_ints();
				sim_activate(&cr_unit, cp_wait);
				if (cr_unit.flags & UNIT_DEBUG)
					DEBUG_PRINT("!CR Punch Response");
			}
			break;
	}

	return SCPE_OK;
}

void xio_1142_card (int32 addr, int32 func, int32 modify)
{
	char msg[80];
	int ch;
	int16 wd;
	t_bool lastcard;

	switch (func) {
		case XIO_SENSE_DEV:
			if (cp_unit.flags & UNIT_ATT)
				lastcard = FALSE;					/* if punch file is open, assume infinite blank cards in reader */
			else if ((cr_unit.flags & UNIT_ATT) == 0)
				lastcard = TRUE;					/* if nothing to read, hopper's empty */
			else if (readstate == STATION_LOADED)
				lastcard = FALSE;
			else if (cr_unit.fileref == NULL)
				lastcard = TRUE;
			else if ((ch = getc(cr_unit.fileref)) != EOF) {
				ungetc(ch, cr_unit.fileref);		/* put character back; hopper's not empty */
				lastcard = FALSE;
			}
			else if (deckfile != NULL && nextdeck())
				lastcard = FALSE;
			else
				lastcard = TRUE;					/* there is nothing left to read for a next card */

			CLRBIT(cr_dsw, CR_DSW_LAST_CARD|CR_DSW_BUSY|CR_DSW_NOT_READY);

			if (lastcard)
				SETBIT(cr_dsw, CR_DSW_LAST_CARD);

			if (CURRENT_OP != OP_IDLE)
				SETBIT(cr_dsw, CR_DSW_BUSY|CR_DSW_NOT_READY);
			else if (readstate == STATION_EMPTY && punchstate == STATION_EMPTY && lastcard)
				SETBIT(cr_dsw, CR_DSW_NOT_READY);

			if (modify & 0x01) {					/* reset interrupts */
				CLRBIT(cr_dsw, CR_DSW_READ_RESPONSE|CR_DSW_PUNCH_RESPONSE);
				CLRBIT(ILSW[0], ILSW_0_1442_CARD);
			}

			if (modify & 0x02) {
				CLRBIT(cr_dsw, CR_DSW_OP_COMPLETE);
				CLRBIT(ILSW[4], ILSW_4_1442_CARD);
			}

			ACC = cr_dsw;							/* return the DSW */

			if (cr_unit.flags & UNIT_DEBUG)
				DEBUG_PRINT("#CR Sense %04x%s%s", cr_dsw, (modify & 1) ? " RESET0" : "", (modify & 2) ? " RESET4" : "");
			break;

		case XIO_READ:								/* get card data into word pointed to in IOCC packet */
			if (cr_unit.flags & OP_READING) {
				if (cr_unit.COLUMN < 0) {
					xio_error("1442: Premature read!");
				}
				else if (cr_unit.COLUMN < 80) {
					WriteW(addr, readstation[cr_unit.COLUMN]);
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Read %03x", (readstation[cr_unit.COLUMN] >> 4));
				}
				else if (cr_unit.COLUMN == 80) {
					xio_error("1442: Read past column 80!");
					cr_unit.COLUMN++;		// don't report it again
				}
			}
			else {
				xio_error("1442: Read when not in a read cycle!");
			}
			break;

		case XIO_WRITE:
			if (cr_unit.flags & OP_PUNCHING) {
				if (cp_unit.COLUMN < 0) {
					xio_error("1442: Premature write!");
				}
				else if (cp_unit.flags & UNIT_LASTPUNCH) {
					xio_error("1442: Punch past last-punch column!");
					cp_unit.COLUMN = 81;
				}
				else if (cp_unit.COLUMN < 80) {
					wd = ReadW(addr);			/* store one word to punch buffer */
					punchstation[cp_unit.COLUMN] = wd & 0xFFF0;
					if (wd & 0x0008)			/* mark this as last column to be punched */
						SETBIT(cp_unit.flags, UNIT_LASTPUNCH);
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Punch %03x%s", (wd >> 4) & 0xFFF, (wd & 8) ? " LAST" : "");
				}
				else if (cp_unit.COLUMN == 80) {
					xio_error("1442: Punch past column 80!");
					cp_unit.COLUMN++;			// don't report it again
				}
			}
			else {
				xio_error("1442: Write when not in a punch cycle!");
			}
			break;

		case XIO_CONTROL:
			switch (modify & 7) {
				case 1:								/* start punch */
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Start Punch");
					if (punchstate != STATION_LOADED)
						feedcycle(TRUE, TRUE);

					SET_OP(OP_PUNCHING);
					cp_unit.COLUMN = -1;

					CLRBIT(cp_unit.flags, UNIT_LASTPUNCH);

					any_punched = 1;				/* we've started punching, so enable writing to output deck file */

					sim_cancel(&cr_unit);
					sim_activate(&cr_unit, cp_wait);
					break;

				case 2:								/* feed cycle */
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Feed");
					feedcycle(TRUE, FALSE);

					SET_OP(OP_FEEDING);

					sim_cancel(&cr_unit);
					sim_activate(&cr_unit, cf_wait);
					break;

				case 4:								/* start read */
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Start read");
					if (readstate != STATION_LOADED)
						feedcycle(TRUE, FALSE);

					SET_OP(OP_READING);
					cr_unit.COLUMN = -1;

					sim_cancel(&cr_unit);
					sim_activate(&cr_unit, cr_wait);
					break;

				case 0:
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR NOP");
					break;

				default:
					sprintf(msg, "1442: Multiple operations in XIO_CONTROL: %x", modify);
					xio_error(msg);
					return;
			}

			break;

		default:
			sprintf(msg, "Invalid 1442 XIO function %x", func);
			xio_error(msg);
			break;
	}
}
