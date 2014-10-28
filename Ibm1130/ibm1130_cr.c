#include "ibm1130_defs.h"
#include "ibm1130_fmt.h"
#include <ctype.h>

#ifdef _WIN32
#  include <io.h>		/* Microsoft puts definition of mktemp into io.h rather than stdlib.h */
#endif

/* ibm1130_cr.c: IBM 1130 1442 Card Reader simulator

   Based on the SIMH package written by Robert M Supnik

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org

 *  Update 2012-10-12  Added ability to specify tab expansion width in deck files

 *  Update 2008-11-24  Made card reader attach always use read-only mode, so if file does not exist
                       it will not be created as an empty file. Fixed bug in BOOT CR (cold start from card)
					   that resulted in seeing cold card data again when next card was read. (This caused
					   the DMS load deck to fail, for instance).

 *  Update 2007-05-01  Changed name of function xio_1142_card to xio_1442_card.
					   Took six years to notice the mistake.

 *  Update 2006-01-23  More fixes, in call to mktemp and in 2501 support, also thanks
 					   to Carl Claunch.

 *  Update 2006-01-03  Fixed bug found by Carl Claunch: feed function does not
 					   cause an operation complete interrupt. Standard DMS routines were not
					   sensitive to this but DUP uses its own interrupt handler, and this
					   is why DUP would hang at end of deck.
 
 *  Update 2005-05-19  Added support for 2501 reader

 *  Update 2004-11-08  Merged in correct physical card reader code

 *  Update 2004-11-02: Added -s to boot command: don't touch console switches.

 *  Update 2004-06-05: Removed "feedcycle" from cr_reset. Reset should not touch the card reader.

 *  Update 2004-04-12: Changed ascii field of CPCODE to unsigned char, caught a couple
 					   other potential problems with signed characters used as subscript indexes.

 *  Update 2003-11-25: Physical card reader support working, may not be perfect.
 					   Changed magic filename for stdin to "(stdin)".

 *  Update 2003-07-23: Added autodetect for card decks (029 vs binary),
    made this the default.
 
 *  Update 2003-06-21: Fixed bug in XIO_SENSE: op_complete and response
    bits were being cleared before the DSW was saved in ACC. Somehow DMS
    worked with this, but APL didn't.

 *  Update 2002-02-29: Added deck-list option.

 *  Update 2003-02-08: Fixed error in declaration of array list_save, pointed
    out by Ray Comas.

* -----------------------------------------------------------------------
* USAGE NOTES
* -----------------------------------------------------------------------

* Attach switches:

   The ATTACH CR command accepts several command-line switches

     -q quiet mode, the simulator will not print the name of each file it opens
        while processing deck files (which are discussed below). For example,
	  
	    ATTACH CR -q @deckfile

     -l makes the simulator convert lower case letters in text decks
        to the IBM lower-case Hollerith character codes. Normally, the simulator
	    converts lower case input to the uppercase Hollerith character codes.
	    (Lowercase codes are used in APL\1130 save decks).

     -d prints a lot of simulator debugging information

     -f converts tabs in an ascii file to spaces according to Fortran column conventions
     -a converts tabs in an ascii file to spaces according to 1130 Assembler column conventions
     -t converts tabs in an ascii file to spaces, with tab settings every 8 columns
     -# converts tabs in an ascii file to spaces, with tab settings every # columns
        (See below for a discussion of tab formatting)

     -p means that filename is a COM port connected to a physical card reader using
        the CARDREAD interface (see http://ibm1130.org/sim/downloads)

   NOTE: for the Card Reader (CR), the -r (readonly) switch is implied. If the file does
   not exist, it will NOT be created.

   The ATTACH CP command accepts the -d switch.

* Deck lists
   If you issue an attach command and specify the filename as
   "@filename", the file is interpreted as a list of filenames to
   be read in sequence; the effect is that the reader sees the concatenation
   of all of the files listed. The simulator "reset" does NOT rewind the deck list.

   Filenames may be quoted if they contain spaces.

   The strings %1, %2, etc, if they appear, are replaced with arguments passed
   on the attach command line after the name of the deckfile. These can be the
   arguments to ibm1130, or to the "do" command if a "do" script is executing, if the
   attach command is constructed this way:

	   attach CR @deckfile %1 %2 %3
   	
   This will pass the ibm1130 or do script arguments to attach, which will make
   them available in the deckfile. Then, for instance the line

       %1.for

   would be substituted accordingly.

   Blank lines and lines starting with ; # or * are ignored as comments.

   Filenames may be followed by whitespace and one or more mode options:
   The mode options are:
   	
			b		forces interpration as raw binary
			a		forces conversion from ascii to 029 coding, tabs are left alone
			af		forces 029 ascii conversion, and interprets tabs in Fortran mode
			aa		forces 029 ascii conversion, and interprets tabs in 1130 Assembler mode
			at		forces 029 ascii conversion, and interprets tabs with settings every 8 spaces
			a#		forces 029 ascii conversion, and interprets tabs with settings every # spaces

   If "a" or "b" mode is not specified, the device mode setting is used.  In this case,
   if the mode is "auto", the simulator will select binary or 029 by inspecting each
   file in turn.

   If a tab mode is not specified, tabs are left unmolested (and are treated as invalid characters)

   Example:
   
       attach cr @decklist

   reads filenames from file "decklist," which might contain:
  
       file01.for xf
	   file02.dat a
	   file03 bin b
	   file04 bin

   ('a' means 029, so, if you need 026 coding, specify the
   device default as the correct 026 code and omit the 'a' on the text files lines).

   Literal text cards can be entered in deck files by preceding an input
   line with an exclamation point. For example,

   !// JOB
   !// FOR
   program.for
   !// XEQ
   program.dat
   
   looks like two literal supervisor control cards, followed by the contents
   of file program.for, followed by an // XEQ card, followed by the contents
   of file program.dat.

   %n tokens are not replaced in literal cards.

   The line

   !BREAK
   
   has a special meaning: when read from a deck file, it stops the
   emulator as if "IMMEDIATE STOP" was pressed. This returns control to
   the command interpreter or to the current DO command script.
   
*  Card image format.
   Card files can be ascii text or binary.  There are several ASCII modes:
   CODE_029, CODE_26F, etc, corresponding to different code sets.
   Punch and reader modes can be set independently using

   		set cr binary 		set cp binary *
		set cr 029			set cp 029
		set cr 026f			set cp 026f
		set cr 026c			set cp 026c
		set cr auto	*

   (* = default mode)

   In "auto" mode, the card reader will examine the first 160 bytes of
   the deck and guess whether the card is binary or 029 text encoded.
   When a deck file is used with auto mode, the simulator guesses for
   each file named in the deck file.

*  Tab formatting. The attach command and deckfile entries can indicate
   that tabs are to be converted to spaces, to help let you write free-form
   source files.  There are three tab conversion modes, which are set
   with the attach command or in a decklist, as discussed earlier

   Fortran mode:
		Input lines of the form

			[label]<tab>statement

	    or

			[label]<tab>+continuation

		(where + is any nonalphabetic character) are rearranged in the
		appropriate manner:

					 1		   2
			12345678901234567890...
			------------------------
	  	    label statement
  		    label+continuation

		However, you must take care that you don't end up with statement text after column 72.

		Input lines with * or C in column 1 (comments and directives) and lines without tabs
		are left alone.

		(The ! escape is not used before Fortran directives as before Assembler directives)

   Assembler mode:
		Input lines of the form

			[label]<whitespace>[opcode]<tab>[tag][L]<tab>[argument]

	    are rearranged so that the input fields are placed in the appropriate columns

		The label must start on the first character of the line. If there is no label, 
		the first character(s) before the opcode must be whitespace. Following the opcode, there
		MUST be a tab character, followed by the format and tag. Following the format and tag 
		may be exactly one whitespace character, and then starts the argument.

	    Input lines with * in column 1 and blank lines are turned into Assembler comments,
		with the * in the Opcode field.

 		Assembler directive lines at the beginning of the deck must be preceded by
		! to indicate that they are not comments. For example,

		!*LIST
		* This is a comment

    Plain Tab mode:
		Tabs are replaced with spaces. Tab settings are assumed to be eight characters wide,
		as is standard for vi, notepad, etc.

* CGI mode note: The command

     attach cr (stdin)

  will attach the card reader to stdin. However, this is not compatible
  with the default encoding autodetect feature, so the command must be
  preceded with

      set cr 029

* -----------------------------------------------------------------------
* PROGRAMMING NOTES
* -----------------------------------------------------------------------

NOTE - there is a problem with this code. The Device Status Word (DSW) is
computed from current conditions when requested by an XIO load status
command; the value of DSW available to the simulator's examine & save
commands may NOT be accurate. This should probably be fixed. (I think there's
a way to have the expression evaluator call a routine? That would be one
way to solve the problem, the other is to keep DSW up-to-date all the time).

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

   (Note: Carl Claunch determined by examining DUP code that a feed cycle
   does not cause an operation complete interrupt).

-- -- this may need changing depending on how things work in hardware. TBD.
||  A read cycle on an empty deck causes an error.
||  Hmmm -- what takes the place of the Start button on
-- the card reader?

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

	   12 11  0  1  2            3   4  5  6  7  8  9   <- columns of cold start card
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
   reader loads one of the built-in boot card images. Boot with an ASCII
   deck isn't allowed.
*/

#define READ_DELAY		 35			/* see how small a number we can get away with */
#define PUNCH_DELAY		 35
#define FEED_DELAY		 25
#define READ_2501_DELAY  500

/* umm, this is a weird little future project of mine. */

#define ENABLE_PHYSICAL_CARD_READER_SUPPORT

extern UNIT cpu_unit;

static t_stat cr_svc      (UNIT *uptr);
static t_stat cr_reset    (DEVICE *dptr);
static t_stat cr_set_code (UNIT *uptr, int32 match, char *cptr, void *desc);
static t_stat cr_attach   (UNIT *uptr, char *cptr);
static int32  guess_cr_code (void);
static void	  feedcycle	  (t_bool load, t_bool punching);

static t_stat cp_reset	  (DEVICE *dptr);
static t_stat cp_set_code (UNIT *uptr, int32 match, char *cptr, void *desc);
static t_stat cp_attach   (UNIT *uptr, char *cptr);
static t_stat cp_detach   (UNIT *uptr);

static int16 cr_dsw  = 0;									/* device status word */
static int32 cr_wait = READ_DELAY;							/* read per-column wait */
static int32 cr_wait2501 = READ_2501_DELAY;					/* read card wait for 2501 reader */
static int32 cf_wait = PUNCH_DELAY;							/* punch per-column wait */
static int32 cp_wait = FEED_DELAY;							/* feed op wait */
static int32 cr_count= 0;									/* read and punch card count */
static int32 cp_count= 0;
static int32 cr_addr = 0;									/* 2501 reader transfer address */
static int32 cr_cols = 0;									/* 2501 reader column count */

#define UNIT_V_OPERATION   (UNIT_V_UF + 0)					/* operation in progress */
#define UNIT_V_CODE		   (UNIT_V_UF + 2)					/* three bits */
#define UNIT_V_CR_EMPTY	   (UNIT_V_UF + 5)			/* NOTE: THIS MUST BE SET IN ibm1130_gui.c too */
#define UNIT_V_SCRATCH	   (UNIT_V_UF + 6)
#define UNIT_V_QUIET       (UNIT_V_UF + 7)
#define UNIT_V_DEBUG       (UNIT_V_UF + 8)
#define UNIT_V_PHYSICAL	   (UNIT_V_UF + 9)			/* NOTE: THIS MUST BE SET IN ibm1130_gui.c too */
#define UNIT_V_LASTPUNCH   (UNIT_V_UF + 10)					/* used in unit_cp only */
#define UNIT_V_LOWERCASE   (UNIT_V_UF + 10)					/* used in unit_cr only */
#define UNIT_V_ACTCODE     (UNIT_V_UF + 11)					/* used in unit_cr only, 3 bits */
#define UNIT_V_2501		   (UNIT_V_UF + 14)

#define UNIT_OP			 (3u << UNIT_V_OPERATION)			/* two bits */
#define UNIT_CODE		 (7u << UNIT_V_CODE)				/* three bits */
#define UNIT_CR_EMPTY	 (1u << UNIT_V_CR_EMPTY)				
#define UNIT_SCRATCH	 (1u << UNIT_V_SCRATCH)				/* temp file */
#define UNIT_QUIET       (1u << UNIT_V_QUIET)
#define UNIT_DEBUG       (1u << UNIT_V_DEBUG)
#define UNIT_PHYSICAL	 (1u << UNIT_V_PHYSICAL)
#define UNIT_LASTPUNCH	 (1u << UNIT_V_LASTPUNCH)
#define UNIT_LOWERCASE	 (1u << UNIT_V_LOWERCASE)			/* permit lowercase input (needed for APL) */
#define UNIT_ACTCODE	 (7u << UNIT_V_ACTCODE)
#define UNIT_2501		 (1u << UNIT_V_2501)

#define OP_IDLE		 	 (0u << UNIT_V_OPERATION)
#define OP_READING	 	 (1u << UNIT_V_OPERATION)
#define OP_PUNCHING	 	 (2u << UNIT_V_OPERATION)
#define OP_FEEDING	 	 (3u << UNIT_V_OPERATION)

#define SET_OP(op) 		 {cr_unit.flags &= ~UNIT_OP; cr_unit.flags |= (op);}
#define CURRENT_OP 		 (cr_unit.flags & UNIT_OP)

#define CODE_AUTO		 (0u << UNIT_V_CODE)
#define CODE_029 		 (1u << UNIT_V_CODE)
#define CODE_026F		 (2u << UNIT_V_CODE)
#define CODE_026C		 (3u << UNIT_V_CODE)
#define CODE_BINARY		 (4u << UNIT_V_CODE)

#define GET_CODE(un)     (un.flags & UNIT_CODE)
#define SET_CODE(un,cd)  {un.flags &= ~UNIT_CODE; un.flags |= (cd);}

#define ACTCODE_029 	 (CODE_029    << (UNIT_V_ACTCODE-UNIT_V_CODE))	/* these are used ONLY in MTAB. Elsewhere */
#define ACTCODE_026F	 (CODE_026F   << (UNIT_V_ACTCODE-UNIT_V_CODE))	/* we use values CODE_xxx with macros */
#define ACTCODE_026C	 (CODE_026C   << (UNIT_V_ACTCODE-UNIT_V_CODE))	/* GET_ACTCODE and SET_ACTCODE. */
#define ACTCODE_BINARY	 (CODE_BINARY << (UNIT_V_ACTCODE-UNIT_V_CODE))

		/* get/set macros for actual-code field, these use values like CODE_029 meant for the UNIT_CODE field */
#define GET_ACTCODE(un)    ((un.flags & UNIT_ACTCODE) >> (UNIT_V_ACTCODE-UNIT_V_CODE))
#define SET_ACTCODE(un,cd) {un.flags &= ~UNIT_ACTCODE; un.flags |= (cd) << (UNIT_V_ACTCODE-UNIT_V_CODE);}

#define COLUMN		u4										/* column field in unit record */

UNIT cr_unit = { UDATA (&cr_svc, UNIT_ATTABLE|UNIT_ROABLE|UNIT_CR_EMPTY, 0) };
UNIT cp_unit = { UDATA (NULL,    UNIT_ATTABLE, 0) };

MTAB cr_mod[] = {
	{ UNIT_CODE,    CODE_029,		"029",		"029",		&cr_set_code},
	{ UNIT_CODE,    CODE_026F,		"026F",		"026F",		&cr_set_code},
	{ UNIT_CODE,    CODE_026C, 		"026C", 	"026C",		&cr_set_code},
	{ UNIT_CODE,    CODE_BINARY,	"BINARY",	"BINARY",	&cr_set_code},
	{ UNIT_CODE,    CODE_AUTO,	 	"AUTO",		"AUTO",		&cr_set_code},
	{ UNIT_ACTCODE, ACTCODE_029,	"(029)",	NULL,		NULL},		/* display-only, shows current mode */
	{ UNIT_ACTCODE, ACTCODE_026F,	"(026F)",	NULL,		NULL},
	{ UNIT_ACTCODE, ACTCODE_026C, 	"(026C)", 	NULL,		NULL},
	{ UNIT_ACTCODE, ACTCODE_BINARY,	"(BINARY)",	NULL,		NULL},
	{ UNIT_2501,    0,				"1442",    "1442",      NULL},
	{ UNIT_2501,    UNIT_2501,		"2501",    "2501",      NULL},
	{ 0 }  };

MTAB cp_mod[] = {
	{ UNIT_CODE, CODE_029,		"029",		"029",		&cp_set_code},
	{ UNIT_CODE, CODE_026F,		"026F",		"026F",		&cp_set_code},
	{ UNIT_CODE, CODE_026C, 	"026C", 	"026C",		&cp_set_code},
	{ UNIT_CODE, CODE_BINARY,	"BINARY",	"BINARY",	&cp_set_code},
	{ 0 }  };

REG cr_reg[] = {
	{ HRDATA (CRDSW,    cr_dsw,  16) },					/* device status word */
	{ DRDATA (CRTIME,   cr_wait, 24), PV_LEFT },		/* operation wait for 1442 column read*/
	{ DRDATA (2501TIME, cr_wait2501, 24), PV_LEFT },	/* operation wait for 2501 whole card read*/
	{ DRDATA (CFTIME,   cf_wait, 24), PV_LEFT },		/* operation wait */
	{ DRDATA (CRCOUNT,  cr_count, 32),PV_LEFT },		/* number of cards read since last attach cmd */
	{ HRDATA (CRADDR,   cr_addr,  32) },				/* 2501 reader transfer address */
	{ HRDATA (CRCOLS,   cr_cols,  32) },				/* 2501 reader column count */
	{ NULL }  };

REG cp_reg[] = {
	{ DRDATA (CPTIME,  cp_wait, 24), PV_LEFT },			/* operation wait */
	{ DRDATA (CPCOUNT, cp_count, 32),PV_LEFT },			/* number of cards punched since last attach cmd */
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
	NULL, cp_attach, cp_detach};

#define CR_DSW_1442_READ_RESPONSE		0x8000		/* device status word bits */
#define CR_DSW_1442_PUNCH_RESPONSE		0x4000
#define CR_DSW_1442_ERROR_CHECK			0x2000
#define CR_DSW_1442_LAST_CARD			0x1000
#define CR_DSW_1442_OP_COMPLETE			0x0800
#define CR_DSW_1442_FEED_CHECK			0x0100
#define CR_DSW_1442_BUSY				0x0002
#define CR_DSW_1442_NOT_READY			0x0001

#define CR_DSW_2501_ERROR_CHECK			0x2000		/* DSW for 2501 reader */
#define CR_DSW_2501_LAST_CARD			0x1000
#define CR_DSW_2501_OP_COMPLETE			0x0800
#define CR_DSW_2501_BUSY				0x0002
#define CR_DSW_2501_NOT_READY			0x0001

typedef struct {
	uint16 hollerith;
	unsigned char ascii;
} CPCODE;

static CPCODE cardcode_029[] =
{
	{0x0000,		' '},
	{0x8000,		'&'},	 		/* + in 026 Fortran */
	{0x4000,		'-'},
	{0x2000,		'0'},
	{0x1000,		'1'},
	{0x0800,		'2'},
	{0x0400,		'3'},
	{0x0200,		'4'},
	{0x0100,		'5'},
	{0x0080,		'6'},
	{0x0040,		'7'},
	{0x0020,		'8'},
	{0x0010,		'9'},
	{0x9000,		'A'},
	{0x8800,		'B'},
	{0x8400,		'C'},
	{0x8200,		'D'},
	{0x8100,		'E'},
	{0x8080,		'F'},
	{0x8040,		'G'},
	{0x8020,		'H'},
	{0x8010,		'I'},
	{0x5000,		'J'},
	{0x4800,		'K'},
	{0x4400,		'L'},
	{0x4200,		'M'},
	{0x4100,		'N'},
	{0x4080,		'O'},
	{0x4040,		'P'},
	{0x4020,		'Q'},
	{0x4010,		'R'},
	{0x3000,		'/'},
	{0x2800,		'S'},
	{0x2400,		'T'},
	{0x2200,		'U'},
	{0x2100,		'V'},
	{0x2080,		'W'},
	{0x2040,		'X'},
	{0x2020,		'Y'},
	{0x2010,		'Z'},
	{0x0820,		':'},
	{0x0420,		'#'},		/* = in 026 Fortran */
	{0x0220,		'@'},		/* ' in 026 Fortran */
	{0x0120,		'\''},
	{0x00A0,		'='},
	{0x0060,		'"'},
	{0x8820,		(unsigned char) '\xA2'},	/* cent, in MS-DOS encoding (this is in guess_cr_code as well) */
	{0x8420,		'.'},
	{0x8220,		'<'},		/* ) in 026 Fortran */
	{0x8120,		'('},
	{0x80A0,		'+'},
	{0x8060,		'|'},
	{0x4820,		'!'},
	{0x4420,		'$'},
	{0x4220,		'*'},
	{0x4120,		')'},
	{0x40A0,		';'},
	{0x4060,		(unsigned char) '\xAC'},	/* not, in MS-DOS encoding  (this is in guess_cr_code as well) */
	{0x2420,		','},
	{0x2220,		'%'},		/* ( in 026 Fortran */
	{0x2120,		'_'},
	{0x20A0,		'>'},
	{0xB000,		'a'},
	{0xA800,		'b'},
	{0xA400,		'c'},
	{0xA200,		'd'},
	{0xA100,		'e'},
	{0xA080,		'f'},
	{0xA040,		'g'},
	{0xA020,		'h'},
	{0xA010,		'i'},
	{0xD000,		'j'},
	{0xC800,		'k'},
	{0xC400,		'l'},
	{0xC200,		'm'},
	{0xC100,		'n'},
	{0xC080,		'o'},
	{0xC040,		'p'},
	{0xC020,		'q'},
	{0xC010,		'r'},
	{0x6800,		's'},
	{0x6400,		't'},
	{0x6200,		'u'},
	{0x6100,		'v'},
	{0x6080,		'w'},
	{0x6040,		'x'},
	{0x6020,		'y'},
	{0x6010,		'z'},				/* these odd punch codes are used by APL: */
	{0x1010,		'\001'},				/* no corresponding ASCII	using ^A */
	{0x0810,		'\002'},				/* SYN						using ^B */
	{0x0410,		'\003'},				/* no corresponding ASCII	using ^C */
	{0x0210,		'\004'},				/* PUNCH ON					using ^D */
	{0x0110,		'\005'},				/* READER STOP				using ^E */
	{0x0090,		'\006'},				/* UPPER CASE				using ^F */
	{0x0050,		'\013'},				/* EOT						using ^K */
	{0x0030,		'\016'},				/* no corresponding ASCII	using ^N */
	{0x1030,		'\017'},				/* no corresponding ASCII	using ^O */
	{0x0830,		'\020'},				/* no corresponding ASCII	using ^P */
};

static CPCODE cardcode_026F[] =		/* 026 fortran */
{
	{0x0000,		' '},
	{0x8000, 		'+'},
	{0x4000,		'-'},
	{0x2000,		'0'},
	{0x1000,		'1'},
	{0x0800,		'2'},
	{0x0400,		'3'},
	{0x0200,		'4'},
	{0x0100,		'5'},
	{0x0080,		'6'},
	{0x0040,		'7'},
	{0x0020,		'8'},
	{0x0010,		'9'},
	{0x9000, 		'A'},
	{0x8800,		'B'},
	{0x8400,		'C'},
	{0x8200,		'D'},
	{0x8100,		'E'},
	{0x8080,		'F'},
	{0x8040,		'G'},
	{0x8020,		'H'},
	{0x8010,		'I'},
	{0x5000, 		'J'},
	{0x4800,		'K'},
	{0x4400,		'L'},
	{0x4200,		'M'},
	{0x4100,		'N'},
	{0x4080,		'O'},
	{0x4040,		'P'},
	{0x4020,		'Q'},
	{0x4010,		'R'},
	{0x3000, 		'/'},
	{0x2800,		'S'},
	{0x2400,		'T'},
	{0x2200,		'U'},
	{0x2100,		'V'},
	{0x2080,		'W'},
	{0x2040,		'X'},
	{0x2020,		'Y'},
	{0x2010,		'Z'},
	{0x0420,		'='},
	{0x0220,		'\''},
	{0x8420,		'.'},
	{0x8220,		')'},
	{0x8220,		'<'},				/* if ASCII has <, treat like ) */
	{0x4420,		'$'},
	{0x4220,		'*'},
	{0x2420,		','},
	{0x2220,		'('},
	{0x2220,		'%'},				/* if ASCII has %, treat like ) */
};

static CPCODE cardcode_026C[] =		/* 026 commercial */
{
	{0x0000,		' '},
	{0x8000, 		'+'},
	{0x4000,		'-'},
	{0x2000,		'0'},
	{0x1000,		'1'},
	{0x0800,		'2'},
	{0x0400,		'3'},
	{0x0200,		'4'},
	{0x0100,		'5'},
	{0x0080,		'6'},
	{0x0040,		'7'},
	{0x0020,		'8'},
	{0x0010,		'9'},
	{0x9000, 		'A'},
	{0x8800,		'B'},
	{0x8400,		'C'},
	{0x8200,		'D'},
	{0x8100,		'E'},
	{0x8080,		'F'},
	{0x8040,		'G'},
	{0x8020,		'H'},
	{0x8010,		'I'},
	{0x5000, 		'J'},
	{0x4800,		'K'},
	{0x4400,		'L'},
	{0x4200,		'M'},
	{0x4100,		'N'},
	{0x4080,		'O'},
	{0x4040,		'P'},
	{0x4020,		'Q'},
	{0x4010,		'R'},
	{0x3000, 		'/'},
	{0x2800,		'S'},
	{0x2400,		'T'},
	{0x2200,		'U'},
	{0x2100,		'V'},
	{0x2080,		'W'},
	{0x2040,		'X'},
	{0x2020,		'Y'},
	{0x2010,		'Z'},
	{0x0420,		'='},
	{0x0220,		'\''},
	{0x8420,		'.'},
	{0x8220,		'<'},
    {0x8220,		')'},			/* if ASCII has ), treat like < */
	{0x4420,		'$'},
	{0x4220,		'*'},
	{0x2420,		','},
	{0x2220,		'%'},
    {0x2220,		'('},			/* if ASCII has (, treat like % */
};

extern int cgi;

static int16 ascii_to_card[256];

static CPCODE *cardcode;
static int ncardcode;
static FILE *deckfile = NULL;
static char tempfile[128];
static int any_punched = 0;

#define MAXARGLEN 80					/* max length of a saved attach command argument */
#define MAXARGS   10					/* max number of arguments to save */
static char list_save[MAXARGS][MAXARGLEN], *list_arg[MAXARGLEN+1];
static int list_nargs = 0;
static char* (*tab_proc)(char* str, int width) = NULL;		/* tab reformatting routine	*/
static int tab_width = 8;

static uint16 punchstation[80];
static uint16 readstation[80];
static enum {STATION_EMPTY, STATION_LOADED, STATION_READ, STATION_PUNCHED} punchstate = STATION_EMPTY, readstate = STATION_EMPTY;

static t_bool nextdeck (void);
static void checkdeck (void);

static t_stat pcr_attach(UNIT *uptr, char *devname);
static t_stat pcr_detach(UNIT *uptr);
static t_stat pcr_svc(UNIT *uptr);
static void   pcr_xio_sense(int modify);
static void   pcr_xio_feedcycle(void);
static void   pcr_xio_startread(void);
static void   pcr_reset(void);

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

	SET_ACTCODE(cr_unit, match);

	if (! lookup_codetable(match, &code, &ncode))
		return SCPE_ARG;

	if (code != NULL) {					/* if an ASCII mode was selected */
		memset(ascii_to_card, 0, sizeof(ascii_to_card));

		for (i = 0; i < ncode; i++)		/* set ascii to card code table */
			ascii_to_card[code[i].ascii] = code[i].hollerith;
	}

	return SCPE_OK;
}

static t_stat cr_set_code (UNIT *uptr, int32 match, char *cptr, void *desc)
{
	if (match == CODE_AUTO)
		match = guess_cr_code();

	return set_active_cr_code(match);
}

static int32 guess_cr_code (void)
{
	int i;
	long filepos;
	int32 guess;
	union {
		uint16 w[80];				/* one card image, viewed as 80 short words */
		char   c[160];				/* same, viewed as 160 characters */
	} line;

	/* here, we can see if the attached file is binary or ascii and auto-set the
	 * mode. If we the file is a binary deck, we should be able to read a record of 80 short
	 * words, and the low 4 bits of each word must be zero. If the file was an ascii deck,
	 * then these low 4 bits are the low 4 bits of every other character in the first 160
	 * chararacters of the file. They would all only be 0 if all of these characters were
	 * in the following set: {NUL ^P space 0 @ P ` p} . It seems very unlikely that
	 * this would happen, as even if the deck consisted of untrimmed card images and
	 * the first two lines were blank, the 81'st character would be a newline, and it would
	 * appear at one of the every-other characters seen on little-endian machines, anyway.
	 * So: if the code mode is AUTO, we can use this test and select either BINARY or 029.
	 * Might as well also check for the all-blanks and newlines case in case this is a
	 * big-endian machine.
	 */


	guess = CODE_029;									/* assume ASCII, 029 */

	if ((cr_unit.flags & UNIT_ATT) && (cr_unit.fileref != NULL)) {
		filepos = ftell(cr_unit.fileref);				/* remember current position in file */
		fseek(cr_unit.fileref, 0, SEEK_SET);			/* go to first record of file */
														/* read card image; if file too short, leave guess set to 029 */
		if (fxread(line.w, sizeof(line.w[0]), 80, cr_unit.fileref) == 80) {
			guess = CODE_BINARY;						/* we got a card image, assume binary */

			for (i = 0; i < 80; i++) {					/* make sure low bits are zeroes, which our binary card format promises */
				if (line.w[i] & 0x000F) {
					guess = CODE_029;					/* low bits set, must be ascii text */
					break;
				}
			}

			if (guess == CODE_BINARY) {					/* if we saw no low bits, it could have been all spaces. */
				guess = CODE_029;						/* so now assume file is text */
				for (i = 0; i < 160; i++) {				/* ensure all 160 characters are 7-bit ASCII (or not or cent) */
														/* 3.0-3, changed test for > 0x7f to & 0x80 */
					if ((strchr("\r\n\t\xA2\xAC", line.c[i]) == NULL) && ((line.c[i] < ' ') || (line.c[i] & 0x80))) {
						guess = CODE_BINARY;			/* oops, null or weird character, it's binary after all */
						break;
					}
				}
			}
		}

		fseek(cr_unit.fileref, filepos, SEEK_SET);		/* return to original position */
	}

	return guess;
}

static t_stat cp_set_code (UNIT *uptr, int32 match, char *cptr, void *desc)
{
	CPCODE *code;
	int ncode;

	if (! lookup_codetable(match, &code, &ncode))
		return SCPE_ARG;

	cardcode  = code;				/* save code table for punch output */
	ncardcode = ncode;

	return SCPE_OK;
}

t_stat load_cr_boot (int32 drvno, int switches)
{
	int i;
	char *name, msg[80];
	t_bool expand;
	uint16 word, *boot;
	static uint16 dms_boot_data[] = {				/* DMSV2M12, already expanded to 16 bits */
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
	static uint16 apl_boot_data[] = {				/* APLIPL, already expanded */
		0x7021, 0x3000, 0x7038, 0xa0c0, 0x0002, 0x4808, 0x0003, 0x0026,
		0x0001, 0x0001, 0x000c, 0x0000, 0x0000, 0x0800, 0x48f8, 0x0027,
		0x7002, 0x08f2, 0x3800, 0xe0fe, 0x18cc, 0x100e, 0x10c1, 0x4802,
		0x7007, 0x4828, 0x7005, 0x4804, 0x7001, 0x70f3, 0x08e7, 0x70e1,
		0x08ed, 0x70f1, 0xc0e0, 0x1807, 0xd0de, 0xc0df, 0x1801, 0xd0dd,
		0x800d, 0xd00c, 0xc0e3, 0x1005, 0xe80a, 0xd009, 0xc0d8, 0x1008,
		0xd0d6, 0xc0dd, 0x1008, 0x80d4, 0xd0da, 0x1000, 0xb000, 0x00f6,
		0x70e7, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x9000, 0x4004, 0x40c0, 0x8001, 0x4004, 0x40c0, 0x0000, 0x0000	};
	static uint16 aplp_boot_data[] = {				/* APLIPL Privileged, already expanded */
		0x7021, 0x3000, 0x7038, 0xa0c0, 0x0002, 0x4808, 0x0003, 0x0026,
		0x0001, 0x0001, 0x000c, 0x0000, 0x0000, 0x0800, 0x48f8, 0x0027,
		0x7002, 0x08f2, 0x3800, 0xe0fe, 0x18cc, 0x100e, 0x10c1, 0x4802,
		0x7007, 0x4828, 0x7005, 0x4804, 0x7001, 0x70f3, 0x08e7, 0x70e1,
		0x08ed, 0x70f1, 0xc0e0, 0x1807, 0xd0de, 0xc0df, 0x1801, 0xd0dd,
		0x800d, 0xd00c, 0xc0e3, 0x1005, 0xe80a, 0xd009, 0xc0d8, 0x1008,
		0xd0d6, 0xc0dd, 0x1008, 0x80d4, 0xd0da, 0x1002, 0xb000, 0x00f6,
		0x70e7, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x9000, 0x4004, 0x40c0, 0x8001, 0x4004, 0x40c0, 0x4004, 0x4001
	};

	if ((switches & SWMASK('A')) && (switches & SWMASK('P'))) {
		boot   = aplp_boot_data;
		name   = "APL\\1130 Privileged";
		expand = FALSE;
	}
	else if (switches & SWMASK('A')) {
		boot   = apl_boot_data;
		name   = "APL\\1130";
		expand = FALSE;
	}
	else {
		boot   = dms_boot_data;
		name   = "DMS V2M12";
		expand = FALSE;
	}

	if (drvno >= 0 && ! (switches & SWMASK('S')))				/* if specified, set toggle switches to disk drive no */
		CES = drvno;				/* so BOOT DSK1 will work correctly (DMS boot uses this) */
									/* but do not touch switches if -S was specified */

	IAR = 0;						/* clear IAR */

	for (i = 0; i < 80; i++) {		/* store the boot image to core words 0..79 */
		word = boot[i];				/* expanding the 12-bit card data to 16 bits if not already expanded */
		if (expand)
			word = (word & 0xF800) | ((word & 0x0400) ? 0x00C0 : 0x0000) | ((word & 0x03F0) >> 4);

		WriteW(i, word);
	}
									/* quiet switch or CGI mode inhibit the boot remark */
	if (((switches & SWMASK('Q')) == 0) && ! cgi) {					/* 3.0-3, parenthesized & operation, per lint check */
		sprintf(msg, "Loaded %s cold start card", name);

#ifdef GUI_SUPPORT
		remark_cmd(msg);
#else
		printf("%s\n", msg);
#endif
	}

	return SCPE_OK;
}

t_stat cr_boot (int32 unitno, DEVICE *dptr)
{
	t_stat rval;
	int i;

	if ((rval = reset_all(0)) != SCPE_OK)
		return rval;

	if (! (cr_unit.flags & UNIT_ATT))			/* no deck; load standard boot anyway */
		return load_cr_boot(-1, 0);

	if (GET_ACTCODE(cr_unit) != CODE_BINARY) {
		printf("Can only boot from card reader when set to BINARY mode\n");
		return SCPE_IOERR;
	}

	if (cr_unit.fileref == NULL)				/* this will happen if no file in deck file can be opened */
		return SCPE_IOERR;

	feedcycle(TRUE, FALSE);

	if (readstate != STATION_LOADED) {
		printf("No cards in reader\n");
		return SCPE_IOERR;
	}

/*	if (fxread(buf, sizeof(buf[0]), 80, cr_unit.fileref) != 80) */
/*		return SCPE_IOERR; */

	IAR = 0;									/* Program Load sets IAR = 0 */

	for (i = 0; i < 80; i++)					/* shift 12 bits into 16 */
		WriteW(i, (readstation[i] & 0xF800) | ((readstation[i] & 0x0400) ? 0x00C0 : 0x0000) | ((readstation[i] & 0x03F0) >> 4));

	readstate = STATION_READ;					/* the current card has been consumed */
	return SCPE_OK;
}

char card_to_ascii (uint16 hol)
{
	int i;

	for (i = 0; i < ncardcode; i++)
		if (cardcode[i].hollerith == hol)
			return (char) cardcode[i].ascii;

	return '?';
}

/* hollerith_to_ascii - provide a generic conversion for simulator debugging  */

char hollerith_to_ascii (uint16 hol)
{
	int i;

	for (i = 0; i < ncardcode; i++)
		if (cardcode_029[i].hollerith == hol)
			return (char) cardcode[i].ascii;

	return ' ';
}

/* feedcycle - move cards to next station */

static void feedcycle (t_bool load, t_bool punching)
{
	char buf[84], *x, *result;
	int i, nread, nwrite, ch;

	/* write punched card if punch is attached to a file */
	if (cp_unit.flags & UNIT_ATT) {
		if (any_punched && punchstate != STATION_EMPTY) {
			if (GET_CODE(cp_unit) == CODE_BINARY) {
				fxwrite(punchstation, sizeof(punchstation[0]), 80, cp_unit.fileref);
			}
			else {
				for (i = 80; --i >= 0; ) {		/* find last nonblank column */
					if (punchstation[i] != 0)
						break;
				}

				/* i is now index of last character to output or -1 if all blank */

				for (nwrite = 0; nwrite <= i; nwrite++)	{			/* convert characters */
					buf[nwrite] = card_to_ascii(punchstation[nwrite]);
				}

				/* nwrite is now number of characters to output */

#ifdef WIN32
				buf[nwrite++] = '\r';				/* add CR before NL for microsoft */
#endif
				buf[nwrite++] = '\n';				/* append newline */
				fxwrite(buf, sizeof(char), nwrite, cp_unit.fileref);
			}
		}

		cp_count++;
	}

	if (! load)										/* all we wanted to do was flush the punch */
		return;

	/* slide cards from reader to punch. If we know we're punching,
	 * generate a blank card in any case. Otherwise, it should take two feed
	 * cycles to get a read card from the hopper to punch station. Also when
	 * the reader is a 2501, we assume the 1442 is a punch only */

	if (readstate == STATION_EMPTY || (cr_unit.flags & UNIT_2501)) {
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

		else if (GET_ACTCODE(cr_unit) == CODE_BINARY)		/* binary read is straightforward */
			nread = fxread(readstation, sizeof(readstation[0]), 80, cr_unit.fileref);

		else if (fgets(buf, sizeof(buf), cr_unit.fileref) == NULL)	/* read up to 80 chars */
			nread = 0;										/* hmm, end of file */

		else {												/* check for CRLF or newline */
			if ((x = strchr(buf, '\r')) == NULL)
				x = strchr(buf, '\n');

			if (x == NULL) {								/* there were no delimiters, burn rest of line */
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
				if ((nread = strlen(buf)) > 80)			/* use the line as read, at most 80 characters */
					nread = 80;
			}
			else
				nread = x-buf;							/* reduce length of string */

			if (! (cr_unit.flags & UNIT_LOWERCASE))
				upcase(buf);							/* force uppercase */

			if (tab_proc != NULL) {						/* apply tab editing, if specified */
				buf[nread] = '\0';						/* .. be sure string is terminated	*/
				result = (*tab_proc)(buf, tab_width);	/* .. convert tabs 	spaces	 		*/
				nread  = strlen(result);				/* .. set new read length			*/
			}
			else
				result = buf;

			for (i = 0; i < nread; i++)					/* convert ascii to punch code */
				readstation[i] = ascii_to_card[(unsigned char) result[i]];

			nread = 80;									/* even if line was blank consider it present */
		}

		if (nread <= 0) {								/* set hopper flag accordingly */
			if (deckfile != NULL && nextdeck())
				goto again;

			if (punching)								/* pretend we loaded a blank card */
				nread = 80;
		}

		if (nread == 0) {
			SETBIT(cr_unit.flags, UNIT_CR_EMPTY);
			readstate = STATION_EMPTY;
			cr_count = -1;								/* nix the card counter */
		}
		else {
			CLRBIT(cr_unit.flags, UNIT_CR_EMPTY);
			readstate = STATION_LOADED;
			cr_count++;
			cr_unit.pos++;
		}
	}
/*	else */
/*		readstate = STATION_EMPTY; */

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

	cr_count = -1;									/* nix the card counter */

	if (punchstate == STATION_PUNCHED)
		feedcycle(FALSE, FALSE);					/* flush out card just punched */

	readstate = punchstate = STATION_EMPTY;
	cr_unit.COLUMN = -1;							/* neither device is currently cycling */
	cp_unit.COLUMN = -1;
    SETBIT(cr_unit.flags, UNIT_CR_EMPTY);			/* set hopper empty */
}

#endif

/* skipbl - skip leading whitespace in a string */

static char * skipbl (char *str)
{
	while (*str && *str <= ' ')
		str++;

	return str;
}

static char * trim (char *str)
{
	char *s, *lastnb;

	for (lastnb = str-1, s = str; *s; s++)	/* point to last nonblank characteter in string */
		if (*s > ' ')
			lastnb = s;

	lastnb[1] = '\0';						/* clip just after it */

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
		fseek(cr_unit.fileref, 0, SEEK_END);		/* seek to end of file */
		empty = ftell(cr_unit.fileref) <= 0;		/* file is empty if there was nothing in it*/
		cr_count = 0;								/* reset card counter */
		cr_unit.pos = 0;
		fseek(cr_unit.fileref, 0, SEEK_SET);		/* rewind deck */
	}

	if (empty) {
		SETBIT(cr_unit.flags, UNIT_CR_EMPTY);
		if (cr_unit.fileref != NULL)				/* real file but it's empty, hmmm, try another */
			nextdeck();
	}
	else {
		CLRBIT(cr_unit.flags, UNIT_CR_EMPTY);
	}
}

/* nextdeck - attempt to load a new file from the deck list into the hopper */

static t_bool nextdeck (void)
{
	char buf[200], *fname, *c, quote;
	int code;
	long fpos;

	cr_count = 0;							/* clear read count */
	cr_unit.pos = 0;

	if (deckfile == NULL)					/* we can't help */
		return FALSE;

	code = GET_CODE(cr_unit);				/* default code as set */

	if (cr_unit.fileref != NULL) {			/* this pulls the rug out from under scp */
		fclose(cr_unit.fileref);			/* since the attach flag is still set. be careful! */
		cr_unit.fileref = NULL;

		if (cr_unit.flags & UNIT_SCRATCH) {
			remove(tempfile);
			CLRBIT(cr_unit.flags, UNIT_SCRATCH);
		}
	}

	for (;;) {								/* get a filename */
		tab_proc = NULL;					/* default: no tab editing */
		tab_width = 8;

		if (fgets(buf, sizeof(buf), deckfile) == NULL)
			break;							/* oops, no more names */

		alltrim(buf);						/* remove leading and trailing spaces */

		if (! *buf)
			continue;						/* empty line */

		if (*buf == '#' || *buf == '*' || *buf == ';')
			continue;						/* comment */

		if (strnicmp(buf, "!BREAK", 6) == 0) {	/* stop the simulation */
			break_simulation(STOP_DECK_BREAK);
			continue;
		}

		if (buf[0] == '!') {				/* literal text line, make a temporary file */

#if defined  (__GNUC__) && !defined (_WIN32)				/* GCC complains about mktemp & always provides mkstemp */

			if (*tempfile == '\0') {						/* first time, open guaranteed-unique file */
				int fh;					

				strcpy(tempfile, "tempXXXXXX");				/* get modifiable copy of name template */

				if ((fh = mkstemp(tempfile)) == -1) {		/* open file. Actual name is set by side effect */
					printf("Cannot create temporary deck file\n");
					break_simulation(STOP_DECK_BREAK);
					return 0;
				}
															/* get FILE * from the file handle */
				if ((cr_unit.fileref = fdopen(fh, "w+b")) == NULL) {
					printf("Cannot use temporary deck file %s\n", tempfile);
					break_simulation(STOP_DECK_BREAK);
					return 0;
				}
			}
			else {											/* on later opens, just reuse the old name */
				if ((cr_unit.fileref = fopen(tempfile, "w+b")) == NULL) {
					printf("Cannot create temporary file %s\n", tempfile);
					break_simulation(STOP_DECK_BREAK);
					return 0;
				}
			}
#else					/* ANSI standard C always provides mktemp */

			if (*tempfile == '\0') {						/* first time, construct unique name */
				strcpy(tempfile, "tempXXXXXX");				/* make a modifiable copy of the template */
				if (mktemp(tempfile) == NULL) {
					printf("Cannot create temporary card file name\n");
					break_simulation(STOP_DECK_BREAK);
					return 0;
				}
			}
															/* (re)create file */
			if ((cr_unit.fileref = fopen(tempfile, "w+b")) == NULL) {
				printf("Cannot create temporary file %s\n", tempfile);
				break_simulation(STOP_DECK_BREAK);
				return 0;
			}
#endif

			SETBIT(cr_unit.flags, UNIT_SCRATCH);

			for (;;) {										/* store literal cards into temporary file */
				upcase(buf+1);
				fputs(buf+1, cr_unit.fileref);
				putc('\n', cr_unit.fileref);

				if (cpu_unit.flags & UNIT_ATT)
					trace_io("(Literal card %s\n)", buf+1);
				if (! (cr_unit.flags & UNIT_QUIET))
					printf(  "(Literal card %s)\n", buf+1);

				fpos = ftell(deckfile);
				if (fgets(buf, sizeof(buf), deckfile) == NULL)
					break;							/* oops, end of file */
				if (buf[0] != '!' || strnicmp(buf, "!BREAK", 6) == 0)
					break;
				alltrim(buf);
			}
			fseek(deckfile, fpos, SEEK_SET);		/* restore deck file to just before non-literal card */

			fseek(cr_unit.fileref, 0, SEEK_SET);	/* rewind scratch file for reading */
			code = CODE_029;						/* assume literal cards use keycode 029 */
			break;
		}

		sim_sub_args(buf, sizeof(buf), list_arg);	/* substitute in stuff from the attach command line */

		c = buf;									/* pick filename from string */

		while (*c && *c <= ' ')						/* skip leading blanks (there could be some now after subsitution) */
			c++;

		fname = c;									/* remember start */

	    if (*c == '\'' || *c == '"') {				/* quoted string */
			quote = *c++;							/* remember the quote type */
			fname++;								/* skip the quote */
			while (*c && (*c != quote))
				c++;								/* skip to end of quote */
		}
		else {										/* not quoted; look for terminating whitespace */
			while (*c && (*c > ' '))
				c++;
		}

		if (*c)
			*c++ = 0;								/* term arg at space or closing quote & move to next character */

		if (! *fname)								/* blank line, no filename */
			continue;

		if ((cr_unit.fileref = fopen(fname, "rb")) == NULL) {
			printf("File '%s' specified in deck file '%s' cannot be opened\n", fname, cr_unit.filename+1);
			continue;
		}

		c = skipbl(c);						/* skip to next token, which would be mode, if present */

		switch (*c) {
			case 'b':
			case 'B':
				code = CODE_BINARY;					/* force code */
				c++;								/* accept mode character by moving past it */
				break;

			case 'a':
			case 'A':
				code = CODE_029;
				c++;

				switch (*c) {						/* is ascii mode followed by another character? */
					case 'F':
					case 'f':
						tab_proc = EditToFortran;
						c++;
						break;

					case 'A':
					case 'a':
						tab_proc = EditToAsm;
						c++;
						break;

					case 't':
					case 'T':
						tab_proc = EditToWhitespace;
						c++;
						tab_width = 0;				/* see if there is a digit after the 4 -- if so use it as tab expansion width */
						while (isdigit(*c))
							tab_width = tab_width*10 + *c++ - '0';

						if (tab_width == 0)
							tab_width = 8;

						break;
				}
		}

		if (code == CODE_AUTO)						/* otherwise if mode is auto, guess it, otherwise use default */
			code = guess_cr_code();

		if (cpu_unit.flags & UNIT_ATT)
			trace_io("(Opened %s deck %s%s)\n", (code == CODE_BINARY) ? "binary" : "text", fname, tab_proc ? (*tab_proc)(NULL, tab_width) : "");

		if (! (cr_unit.flags & UNIT_QUIET))
			printf(  "(Opened %s deck %s%s)\n", (code == CODE_BINARY) ? "binary" : "text", fname, tab_proc ? (*tab_proc)(NULL, tab_width) : "");

		break;
	}

	checkdeck();

	if (code != CODE_AUTO)						/* if code was determined, set it */
		set_active_cr_code(code);				/* (it may be left at CODE_AUTO when deckfile is exhausted */

	return (cr_unit.flags & UNIT_CR_EMPTY) == 0;/* return TRUE if a deck has been loaded */
}

static t_stat cr_reset (DEVICE *dptr)
{
	if (GET_ACTCODE(cr_unit) == CODE_AUTO)
		SET_ACTCODE(cr_unit, CODE_029);			/* if actual code is not yet set, select 029 for now*/

	cr_set_code(&cr_unit, GET_ACTCODE(cr_unit), NULL, NULL);	/* reset to specified code table */

	readstate = STATION_EMPTY;

	cr_dsw = 0;
	sim_cancel(&cr_unit);							/* cancel any pending ops */
	calc_ints();

	SET_OP(OP_IDLE);

	cr_unit.COLUMN = -1;							/* neither device is currently cycling */

	if (cr_unit.flags & UNIT_PHYSICAL) {
		pcr_reset();
		return SCPE_OK;
	}

	return SCPE_OK;
}

static t_stat cp_reset (DEVICE *dptr)
{
	if (GET_CODE(cp_unit) == CODE_AUTO)
		SET_CODE(cp_unit, CODE_BINARY);				/* punch is never in auto mode; turn it to binary on startup */

	cp_set_code(&cp_unit, GET_CODE(cp_unit), NULL, NULL);
	punchstate = STATION_EMPTY;

	cp_unit.COLUMN = -1;
	return SCPE_OK;
}

t_stat cr_rewind (void)
{
	if ((cr_unit.flags & UNIT_ATT) == 0)
		return SCPE_UNATT;

	if (deckfile) {
		fseek(deckfile, 0, SEEK_SET);
		nextdeck();
	}
	else {
		fseek(cr_unit.fileref, 0, SEEK_SET);
		checkdeck();
		cr_set_code(&cr_unit, GET_CODE(cr_unit), NULL, NULL);
	}

	cr_unit.pos = 0;

	/* there is a read pending. Pull the card in to make it go */
	if (CURRENT_OP == OP_READING || CURRENT_OP == OP_PUNCHING || CURRENT_OP == OP_FEEDING)
		feedcycle(TRUE, (cp_unit.flags & UNIT_ATT) != 0);

	return SCPE_OK;
}

static t_stat cr_attach (UNIT *uptr, char *cptr)
{
	t_stat rval;
	t_bool use_decklist, old_quiet;
	char *c, *arg, quote;

	cr_detach(uptr);								/* detach file and possibly deck file */

	CLRBIT(uptr->flags, UNIT_SCRATCH|UNIT_QUIET|UNIT_DEBUG|UNIT_PHYSICAL|UNIT_LOWERCASE);	/* set options */

	tab_proc = NULL;
	tab_width = 8;
	use_decklist = FALSE;

	sim_switches |= SWMASK('R');	// the card reader is readonly. Don't create an empty file if file does not exist

	if (sim_switches & SWMASK('D')) SETBIT(uptr->flags, UNIT_DEBUG);
	if (sim_switches & SWMASK('Q')) SETBIT(uptr->flags, UNIT_QUIET);
	if (sim_switches & SWMASK('L')) SETBIT(uptr->flags, UNIT_LOWERCASE);

	if (sim_switches & SWMASK('F')) tab_proc = EditToFortran;
	if (sim_switches & SWMASK('A')) tab_proc = EditToAsm;
	if (sim_switches & SWMASK('T')) tab_proc = EditToWhitespace;

	/* user can specify multiple names on the CR attach command if using a deck file. The deck file 
	 * can contain %n tokens to pickup the additional name(s). */

	c = cptr;										/* extract arguments */
	for (list_nargs = 0; list_nargs < MAXARGS; list_nargs++) {
		while (*c && (*c <= ' '))					/* skip blanks */
			c++;

		if (! *c)
			break;									/* all done */

		if (list_nargs == 0 && *c == '@') {			/* @ might occur before a quoted name; check first */
			c++;
			use_decklist = TRUE;
		}

	    if (*c == '\'' || *c == '"') {				/* quoted string */
			quote = *c++;
			arg = c;  								/* save start */
			while (*c && (*c != quote))
				c++;
		}
		else {
			arg = c;								/* save start */
			while (*c && (*c > ' '))
				c++;
		}

		if (*c)
			*c++ = 0;								/* term arg at space or closing quote */

		list_arg[list_nargs] = list_save[list_nargs];	/* set pointer to permanent storage location */
		strncpy(list_arg[list_nargs], arg, MAXARGLEN);	/* store copy */
	}
	list_arg[list_nargs] = NULL;					/* NULL terminate the end of the argument list */


	if (list_nargs <= 0)							/* need at least 1 */
		return SCPE_2FARG;

	cr_count = 0;									/* reset card counter */

	cptr = list_arg[0];								/* filename is first argument */
	if (*cptr == '@') {								/* @ might also occur inside a quoted name; check afterwards too */
		use_decklist = TRUE;
		cptr++;
	}

	else if (sim_switches & SWMASK('P')) {			/* open physical card reader device */
		return pcr_attach(uptr, cptr);
	}

	if (list_nargs > 1 && ! use_decklist)			/* if not using deck file, there should have been only one name */
		return SCPE_2MARG;

	if (strcmp(cptr, "(stdin)") == 0 && ! use_decklist) {			/* standard input */
		if (uptr->flags & UNIT_DIS) return SCPE_UDIS;		/* disabled? */
		uptr->filename = calloc(CBUFSIZE, sizeof(char));
		strcpy(uptr->filename, "(stdin)");
	    uptr->fileref = stdin;
		SETBIT(uptr->flags, UNIT_ATT);
		uptr->pos = 0;
	}
	else {
		old_quiet = sim_quiet;						/* attach the file, but set sim_quiet so we don't get the "CR is read-only" message */
		sim_quiet = TRUE;
		rval = attach_unit(uptr, cptr);
		sim_quiet = old_quiet;

		if (rval != SCPE_OK)						/* file did not exist */
			return rval;
	}

	if (use_decklist) {								/* if we skipped the '@', store the actually-specified name */
		uptr->filename[0] = '@';
		strncpy(uptr->filename+1, cptr, CBUFSIZE-1);

		deckfile = cr_unit.fileref;					/* save the deck file stream in our local variable */
		cr_unit.fileref  = NULL;
		nextdeck();
	}
	else {
		checkdeck();
		cr_set_code(&cr_unit, GET_CODE(cr_unit), NULL, NULL);
	}

	/* there is a read pending. Pull the card in to make it go */
	if (CURRENT_OP == OP_READING || CURRENT_OP == OP_PUNCHING || CURRENT_OP == OP_FEEDING)
		feedcycle(TRUE, (cp_unit.flags & UNIT_ATT) != 0);

	return SCPE_OK;
}

t_stat cr_detach (UNIT *uptr)
{
	t_stat rval;

	cr_count = 0;									/* clear read count */

	if (cr_unit.flags & UNIT_PHYSICAL)
		return pcr_detach(uptr);

	if (cr_unit.flags & UNIT_ATT && deckfile != NULL) {
		if (cr_unit.fileref != NULL)			/* close the active card deck */
			fclose(cr_unit.fileref);

		if (cr_unit.flags & UNIT_SCRATCH) {
			remove(tempfile);
			CLRBIT(cr_unit.flags, UNIT_SCRATCH);
		}

		cr_unit.fileref = deckfile;				/* give scp a file to close */
	}

	if (uptr->fileref == stdin) {
		CLRBIT(uptr->flags, UNIT_ATT);
		free(uptr->filename);
		uptr->filename = NULL;
		uptr->fileref  = NULL;
		rval = SCPE_OK;
	}
	else
		rval = detach_unit(uptr);

	return rval;
}

static t_stat cp_attach (UNIT *uptr, char *cptr)
{
												/* if -d is specified turn on debugging (bit is in card reader UNIT) */
	if (sim_switches & SWMASK('D')) SETBIT(cr_unit.flags, UNIT_DEBUG);

	return attach_unit(uptr, quotefix(cptr));	/* fix quotes in filenames & attach */
}

static t_stat cp_detach   (UNIT *uptr)
{
	if (cp_unit.flags & UNIT_ATT)
		if (punchstate == STATION_PUNCHED)
			feedcycle(FALSE, FALSE);			/* flush out card just punched */

	any_punched = 0;							/* reset punch detected */
	cp_count = 0;								/* clear punch count */

	return detach_unit(uptr);
}

static void op_done (UNIT *u, char *opname, t_bool issue_intr)
{
	if (u->flags & UNIT_DEBUG)
		DEBUG_PRINT("!CR %s Op Complete, card %d%s", opname, cr_count, issue_intr ? ", interrupt" : "");

	SET_OP(OP_IDLE);

	if (u->flags & UNIT_2501)					/* we use u-> not cr_unit. because PUNCH is always a 1442 */
		CLRBIT(cr_dsw,  CR_DSW_2501_BUSY);
	else
		CLRBIT(cr_dsw,  CR_DSW_1442_BUSY);		/* this is trickier. 1442 cr and cp share a dsw */

	if (issue_intr) {							/* issue op-complete interrupt for read and punch ops but not feed */
		if (u->flags & UNIT_2501) {
			SETBIT(cr_dsw,  CR_DSW_2501_OP_COMPLETE);
			SETBIT(ILSW[4], ILSW_4_2501_CARD);
		}
		else {
			SETBIT(cr_dsw,  CR_DSW_1442_OP_COMPLETE);
			SETBIT(ILSW[4], ILSW_4_1442_CARD);
		}
		calc_ints();
	}
}

static t_stat cr_svc (UNIT *uptr)
{
	int i;

	if (uptr->flags & UNIT_PHYSICAL)
		return pcr_svc(uptr);

	switch (CURRENT_OP) {
		case OP_IDLE:
			break;

		case OP_FEEDING:
			op_done(&cr_unit, "feed", FALSE);
			break;

		case OP_READING:
			if (readstate == STATION_EMPTY) {			/* read active but no cards? hang */
				sim_activate(&cr_unit, cf_wait);
				break;
			}

			if (cr_unit.flags & UNIT_2501) { 			/* 2501 transfers entire card then interrupts */
				for (i = 0; i < cr_cols; i++)			/* (we wait until end of delay time before transferring data) */
					M[(cr_addr + i) & mem_mask] = readstation[i];

				readstate = STATION_READ;
				op_done(&cr_unit, "read", TRUE);
			}
			else if (++cr_unit.COLUMN < 80) {			/* 1442 interrupts on each column... */
				SETBIT(cr_dsw,  CR_DSW_1442_READ_RESPONSE);
				SETBIT(ILSW[0], ILSW_0_1442_CARD);
				calc_ints();
				sim_activate(&cr_unit, cr_wait);
				if (cr_unit.flags & UNIT_DEBUG)
					DEBUG_PRINT("!CR Read Response %d : %d", cr_count, cr_unit.COLUMN+1);
			}
			else {										/* ... then issues op-complete */
				readstate = STATION_READ;
				op_done(&cr_unit, "read", TRUE);
			}
			break;

		case OP_PUNCHING:
			if (punchstate == STATION_EMPTY) {			/* punch active but no cards? hang */
				sim_activate(&cr_unit, cf_wait);
				break;
			}

			if (cp_unit.flags & UNIT_LASTPUNCH) {
				punchstate = STATION_PUNCHED;
				op_done(&cp_unit, "punch", TRUE);
			}
			else if (++cp_unit.COLUMN < 80) {
				SETBIT(cr_dsw,  CR_DSW_1442_PUNCH_RESPONSE);
				SETBIT(ILSW[0], ILSW_0_1442_CARD);
				calc_ints();
				sim_activate(&cr_unit, cp_wait);
				if (cr_unit.flags & UNIT_DEBUG)
					DEBUG_PRINT("!CR Punch Response");
			}
			else {
				punchstate = STATION_PUNCHED;
				op_done(&cp_unit, "punch", TRUE);
			}
			break;
	}

	return SCPE_OK;
}

void xio_2501_card (int32 addr, int32 func, int32 modify)
{
	char msg[80];
	int ch;
	t_bool lastcard;

/* it would be nice for simulated reader to be able to use 2501 mode -- much more
 * efficient. Using the 1403 printer and 2501 reader speeds things up quite considerably. */

	switch (func) {
		case XIO_SENSE_DEV:
			if (cr_unit.flags & UNIT_PHYSICAL) {
				pcr_xio_sense(modify);
				break;
			}

// the following part is questionable -- the 2501 might need to be more picky about setting
// the LAST_CARD bit...

			if ((cr_unit.flags & UNIT_ATT) == 0)
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

			CLRBIT(cr_dsw, CR_DSW_2501_LAST_CARD|CR_DSW_2501_BUSY|CR_DSW_2501_NOT_READY);

			if (lastcard)
				SETBIT(cr_dsw, CR_DSW_2501_LAST_CARD|CR_DSW_2501_NOT_READY);
			// don't clear it here -- modify bit must be set before last card can be cleared

			if (CURRENT_OP != OP_IDLE)
				SETBIT(cr_dsw, CR_DSW_2501_BUSY|CR_DSW_2501_NOT_READY);

			ACC = cr_dsw;							/* return the DSW */

			if (cr_unit.flags & UNIT_DEBUG)
				DEBUG_PRINT("#CR Sense %04x%s", cr_dsw & 0xFFFF, (modify & 1) ? " RESET" : "");

			if (modify & 0x01) {					/* reset interrupts */
//				if (! lastcard)						/* (lastcard is reset only when modify bit is set)  */
					CLRBIT(cr_dsw, CR_DSW_2501_LAST_CARD);
				CLRBIT(cr_dsw, CR_DSW_2501_OP_COMPLETE);
				CLRBIT(ILSW[4], ILSW_4_2501_CARD);
			}
			break;

		case XIO_INITR:
			if (cr_unit.flags & UNIT_DEBUG)
				DEBUG_PRINT("#CR Start read");

			cr_unit.COLUMN = -1;

			cr_cols = M[addr & mem_mask];				/* save column count and transfer address */
			cr_addr = addr+1;

			if ((cr_cols < 0) || (cr_cols > 80))		/* this is questionable -- what would hardware do? */
				cr_cols = 80;

			if (cr_unit.flags & UNIT_PHYSICAL) {
				pcr_xio_startread();
				break;
			}

			if (readstate != STATION_LOADED)
				feedcycle(TRUE, (cp_unit.flags & UNIT_ATT) != 0);

			SET_OP(OP_READING);
			sim_cancel(&cr_unit);
			sim_activate(&cr_unit, cr_wait2501);
			break;

		default:
			sprintf(msg, "Invalid 2501 XIO function %x", func);
			xio_error(msg);
			break;
	}
}

void xio_1442_card (int32 addr, int32 func, int32 modify)
{
	char msg[80];
	int ch;
	uint16 wd;
	t_bool lastcard;

	switch (func) {
		case XIO_SENSE_DEV:
			if (cr_unit.flags & UNIT_PHYSICAL) {
				pcr_xio_sense(modify);
				break;
			}

/* glunk
 * have to separate out what status is 1442 is punch only and 2501 is the reader */

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

			CLRBIT(cr_dsw, CR_DSW_1442_LAST_CARD | CR_DSW_1442_BUSY | CR_DSW_1442_NOT_READY);

			if (lastcard)
				SETBIT(cr_dsw, CR_DSW_1442_LAST_CARD);

			if (CURRENT_OP != OP_IDLE)
				SETBIT(cr_dsw, CR_DSW_1442_BUSY | CR_DSW_1442_NOT_READY);
			else if (readstate == STATION_EMPTY && punchstate == STATION_EMPTY && lastcard)
				SETBIT(cr_dsw, CR_DSW_1442_NOT_READY);

			ACC = cr_dsw;							/* return the DSW */

			if (cr_unit.flags & UNIT_DEBUG)
				DEBUG_PRINT("#CR Sense %04x%s%s", cr_dsw & 0xFFFF, (modify & 1) ? " RESET0" : "", (modify & 2) ? " RESET4" : "");

			if (modify & 0x01) {					/* reset interrupts */
				CLRBIT(cr_dsw,  CR_DSW_1442_READ_RESPONSE | CR_DSW_1442_PUNCH_RESPONSE);
				CLRBIT(ILSW[0], ILSW_0_1442_CARD);
			}

			if (modify & 0x02) {
				CLRBIT(cr_dsw,  CR_DSW_1442_OP_COMPLETE);
				CLRBIT(ILSW[4], ILSW_4_1442_CARD);
			}
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
					cr_unit.COLUMN++;		/* don't report it again */
				}
			}
			else {
/* don't complain: APL\1130 issues both reads and writes on every interrupt
 * (probably to keep the code small). Apparently it's just ignored if corresponding
 *  control didn't initiate a read cycle.
 *				xio_error("1442: Read when not in a read cycle!"); */
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
					wd = (uint16) ReadW(addr);			/* store one word to punch buffer */
					punchstation[cp_unit.COLUMN] = wd & 0xFFF0;
					if (wd & 0x0008)			/* mark this as last column to be punched */
						SETBIT(cp_unit.flags, UNIT_LASTPUNCH);
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Punch %03x%s", (wd >> 4) & 0xFFF, (wd & 8) ? " LAST" : "");
				}
				else if (cp_unit.COLUMN == 80) {
					xio_error("1442: Punch past column 80!");
					cp_unit.COLUMN++;			/* don't report it again */
				}
			}
			else {
/* don't complain: APL\1130 issues both reads and writes on every interrupt
 * (probably to keep the code small). Apparently it's just ignored if corresponding
 *  control didn't initiate a punch cycle.
 *				xio_error("1442: Write when not in a punch cycle!"); */
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

					if (cr_unit.flags & UNIT_PHYSICAL) {
						pcr_xio_feedcycle();
						break;
					}

					feedcycle(TRUE, (cp_unit.flags & UNIT_ATT) != 0);

					SET_OP(OP_FEEDING);
					sim_cancel(&cr_unit);
					sim_activate(&cr_unit, cf_wait);
					break;

				case 4:								/* start read */
					if (cr_unit.flags & UNIT_DEBUG)
						DEBUG_PRINT("#CR Start read");

					cr_unit.COLUMN = -1;

					if (cr_unit.flags & UNIT_PHYSICAL) {
						pcr_xio_startread();
						break;
					}

					if (readstate != STATION_LOADED)
						feedcycle(TRUE, (cp_unit.flags & UNIT_ATT) != 0);

					SET_OP(OP_READING);
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

#if ! (defined(ENABLE_PHYSICAL_CARD_READER_SUPPORT) && defined(WIN32))

	/* stub out the physical card reader routines */

	static t_stat pcr_attach        (UNIT *uptr, char *devname) {return SCPE_ARG;}
	static t_stat pcr_detach        (UNIT *uptr)                {return detach_unit(uptr);}
	static t_stat pcr_svc			(UNIT *uptr)                {return SCPE_OK;}
	static void   pcr_xio_sense     (int modify) {}
	static void   pcr_xio_feedcycle (void) {}
	static void   pcr_xio_startread (void) {}
	static void   pcr_reset         (void) {}

#else

/*
 * This code supports a physical card reader interface I built. Interface schematic
 * and documentation can be downloaded from http://ibm1130.org/sim/downloads/cardread.zip
 */

#include <windows.h>

#define PCR_STATUS_READY		 1					/* bits in interface reply byte */
#define PCR_STATUS_ERROR		 2
#define PCR_STATUS_HEMPTY		 4
#define PCR_STATUS_EOF			 8
#define PCR_STATUS_PICKING		16

#define PCR_STATUS_MSEC			150					/* when idle, get status every 150 msec	*/

typedef enum {
	PCR_STATE_IDLE,									/* nothing expected from the interface */
	PCR_STATE_WAIT_CMD_RESPONSE,					/* waiting for response from any command other than P */
	PCR_STATE_WAIT_PICK_CMD_RESPONSE,				/* waiting for response from P command */
	PCR_STATE_WAIT_DATA_START,						/* waiting for introduction to data from P command */
	PCR_STATE_WAIT_DATA,							/* waiting for data from P command */
	PCR_STATE_WAIT_PICK_FINAL_RESPONSE,				/* waiting for status byte after last of the card data */
	PCR_STATE_CLOSED
} PCR_STATE;

static void  pcr_cmd (char cmd);
static DWORD CALLBACK pcr_thread (LPVOID arg);
static BOOL	 pcr_handle_status_byte (int nrcvd);
static void  pcr_trigger_interrupt_0(void);
static void  begin_pcr_critical_section (void);
static void  end_pcr_critical_section (void);
static void  pcr_set_dsw_from_status (BOOL post_pick);
static t_stat pcr_open_controller (char *devname);

static PCR_STATE pcr_state  = PCR_STATE_CLOSED;		/* current state of connection to physical card reader interface */
static char 	 pcr_status = 0;					/* last status byte received from the interface */
static int       pcr_nleft;							/* number of bytes still expected from pick command */
static int		 pcr_nready;						/* number of bytes waiting in the input buffer for simulator to read */
static BOOL		 pcr_done;
static HANDLE    hpcr        = INVALID_HANDLE_VALUE;
static HANDLE    hPickEvent  = INVALID_HANDLE_VALUE;
static HANDLE    hResetEvent = INVALID_HANDLE_VALUE;
static OVERLAPPED ovRd, ovWr;						/* overlapped IO structures for reading from, writing to device */
static int 		 nwaits;							/* number of timeouts waiting for response from interface */
static char 	 response_byte;						/* buffer to receive command/status response byte from overlapped read */
static char		 lastcmd = '?';

/* pcr_attach - perform attach function to physical card reader */

static t_stat pcr_attach (UNIT *uptr, char *devname)
{
	DWORD thread_id;
	t_stat rval;

	pcr_state = PCR_STATE_CLOSED;
	sim_cancel(uptr);
	cr_unit.COLUMN = -1;							/* device is not currently cycling */

	if ((rval = pcr_open_controller(devname)) != SCPE_OK)
		return rval;

	if (hPickEvent == INVALID_HANDLE_VALUE)
		hPickEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (hResetEvent == INVALID_HANDLE_VALUE)
		hResetEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	pcr_status = PCR_STATUS_HEMPTY;					/* set default status: offline, no cards */
	pcr_state  = PCR_STATE_IDLE;
	pcr_done   = FALSE;
	cr_dsw     = CR_DSW_1442_LAST_CARD | CR_DSW_1442_NOT_READY;

	set_active_cr_code(CODE_BINARY);				/* force binary mode */

	if (CreateThread(NULL, 0, pcr_thread, NULL, 0, &thread_id) == NULL) {
		pcr_state = PCR_STATE_CLOSED;
		CloseHandle(hpcr);
		hpcr = INVALID_HANDLE_VALUE;
		printf("Error creating card reader thread\n");
		return SCPE_IERR;
	}

	SETBIT(uptr->flags, UNIT_PHYSICAL|UNIT_ATT);	/* mark device as attached */
	uptr->filename = malloc(strlen(devname)+1);
	strcpy(uptr->filename, devname);

	return SCPE_OK;
}

/* pcr_open_controller - open the USB device's virtual COM port and configure the interface */

static t_stat pcr_open_controller (char *devname)
{
	DCB dcb;
	COMMTIMEOUTS cto;
	DWORD nerr;

	if (hpcr != INVALID_HANDLE_VALUE)
		return SCPE_OK;
													/* open the COM port */
	hpcr = CreateFile(devname, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (hpcr == INVALID_HANDLE_VALUE)
		return SCPE_OPENERR;

	memset(&dcb, 0, sizeof(dcb));					/* set communications parameters */

	dcb.DCBlength		= sizeof(DCB);
	dcb.BaudRate		= CBR_115200;				/* for the USB virtual com port, baud rate is irrelevant */
    dcb.fBinary  		= 1;
    dcb.fParity	  		= 0;
    dcb.fOutxCtsFlow    = 0;
    dcb.fOutxDsrFlow    = 0;
    dcb.fDtrControl		= DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = 0;
    dcb.fOutX			= 0;
    dcb.fInX			= 0;
    dcb.fErrorChar		= 0;
    dcb.fNull			= 0;
    dcb.fRtsControl		= RTS_CONTROL_ENABLE;                
    dcb.fAbortOnError   = 0;
    dcb.XonLim			= 0;
    dcb.XoffLim			= 0;
    dcb.ByteSize		= 8;
    dcb.Parity			= NOPARITY;
    dcb.StopBits		= ONESTOPBIT;
    dcb.XonChar			= 0;
    dcb.XoffChar		= 0;
    dcb.ErrorChar		= 0;
    dcb.EofChar			= 0;
    dcb.EvtChar			= 0;

	if (! SetCommState(hpcr, &dcb)) {
		CloseHandle(hpcr);
		hpcr = INVALID_HANDLE_VALUE;
		printf("Call to SetCommState failed\n");
		return SCPE_OPENERR;
	}

    cto.ReadIntervalTimeout         = 100;			/* stop if 100 msec elapses between two received bytes */
    cto.ReadTotalTimeoutMultiplier  = 0;			/* no length sensitivity */
    cto.ReadTotalTimeoutConstant    = 400;			/* allow 400 msec for a read (reset command can take a while) */

    cto.WriteTotalTimeoutMultiplier = 0;
    cto.WriteTotalTimeoutConstant   = 200;			/* allow 200 msec for a write */

	if (! SetCommTimeouts(hpcr, &cto)) {
		CloseHandle(hpcr);
		hpcr = INVALID_HANDLE_VALUE;
		printf("Call to SetCommTimeouts failed\n");
		return SCPE_OPENERR;
	}

	PurgeComm(hpcr, PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR);
	ClearCommError(hpcr, &nerr, NULL);

	return SCPE_OK;
}

/* pcr_detach - detach physical reader from CR device */

static t_stat pcr_detach (UNIT *uptr)
{
	if (cr_unit.flags & UNIT_ATT) {
		CloseHandle(hpcr);							/* close the COM port (this will lead to the thread closing) */
		hpcr = INVALID_HANDLE_VALUE;
		pcr_state = PCR_STATE_CLOSED;

		free(uptr->filename);						/* release the name copy */
		uptr->filename = NULL;
	}

	CLRBIT(cr_unit.flags, UNIT_PHYSICAL|UNIT_ATT);	/* drop the attach and physical bits */
	return SCPE_OK;
}

/* pcr_xio_sense - perform XIO sense function on physical card reader */

static void pcr_xio_sense (int modify)
{
	if (modify & 0x01) {							/* reset simulated interrupts */
		CLRBIT(cr_dsw,  CR_DSW_1442_READ_RESPONSE | CR_DSW_1442_PUNCH_RESPONSE);
		CLRBIT(ILSW[0], ILSW_0_1442_CARD);
	}

	if (modify & 0x02) {
		CLRBIT(cr_dsw,  CR_DSW_1442_OP_COMPLETE);
		CLRBIT(ILSW[4], ILSW_4_1442_CARD);
	}

	ACC = cr_dsw;									/* DSW was set in real-time, just return the DSW */

	if (cr_unit.flags & UNIT_DEBUG)
		DEBUG_PRINT("#CR Sense %04x%s%s", cr_dsw, (modify & 1) ? " RESET0" : "", (modify & 2) ? " RESET4" : "");
}

/* report_error - issue detailed report of Windows IO error */

static void report_error (char *msg, DWORD err)
{
	char *lpMessageBuffer = NULL;
		
	FormatMessage(
	  FORMAT_MESSAGE_ALLOCATE_BUFFER |
	  FORMAT_MESSAGE_FROM_SYSTEM,
	  NULL,
	  GetLastError(),
	  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* The user default language */
	  (LPTSTR) &lpMessageBuffer,
	  0,
	  NULL );

	printf("GetOverlappedResult failed, %s, %s\n",
		msg, lpMessageBuffer);

	LocalFree(lpMessageBuffer);
}

/* pcr_thread - thread to handle card reader interface communications */

static DWORD CALLBACK pcr_thread (LPVOID arg)
{
	DWORD event;
	long nrcvd, nread, nwritten;
	HANDLE objs[4];
	BOOL pick_queued = FALSE, reset_queued = FALSE;

	nwaits = 0;

	ZeroMemory(&ovRd,  sizeof(ovRd));
	ZeroMemory(&ovWr,  sizeof(ovWr));
	ovRd.hEvent = CreateEvent(NULL, TRUE,  FALSE, NULL);	/* create an event for async IO reads */
	ovWr.hEvent = CreateEvent(NULL, TRUE,  FALSE, NULL);	/* create an event for async IO writes */

	objs[0] = ovRd.hEvent;
	objs[1] = ovWr.hEvent;
	objs[2] = hResetEvent;
	objs[3] = hPickEvent;

	while (hpcr != INVALID_HANDLE_VALUE) {
		if (pcr_state == PCR_STATE_IDLE) {
			if (pick_queued) {
				pcr_cmd('P');
				pick_queued = FALSE;
				pcr_done    = FALSE;
				pcr_state   = PCR_STATE_WAIT_PICK_CMD_RESPONSE;
			}
			else if (reset_queued) {
				pcr_cmd('X');
				reset_queued = FALSE;
				pcr_state = PCR_STATE_WAIT_CMD_RESPONSE;
			}
		}

		event = WaitForMultipleObjects(4, objs, FALSE, PCR_STATUS_MSEC);

		switch (event) {
			case WAIT_OBJECT_0+0:						/* read complete */
				ResetEvent(ovRd.hEvent);
				if (! GetOverlappedResult(hpcr, &ovRd, &nrcvd, TRUE))
					report_error("PCR_Read", GetLastError());
				else if (cr_unit.flags & UNIT_DEBUG)
					printf("PCR_Read: event, %d rcvd\n", nrcvd);
				break;

			case WAIT_OBJECT_0+1:						/* write complete */
				nwritten = 0;
				ResetEvent(ovWr.hEvent);
				if (! GetOverlappedResult(hpcr, &ovWr, &nwritten, TRUE))
					report_error("PCR_Write", GetLastError());
				else if (cr_unit.flags & UNIT_DEBUG)
					printf("PCR_Write: event, %d sent\n", nwritten);
				continue;

			case WAIT_OBJECT_0+2:						/* reset request from simulator */
				reset_queued = TRUE;
				pick_queued  = FALSE;
				continue;

			case WAIT_OBJECT_0+3:						/* pick request from simulator */
				pick_queued = TRUE;
				continue;

			case WAIT_TIMEOUT:
				if (pcr_state == PCR_STATE_IDLE) {
					pcr_state = PCR_STATE_WAIT_CMD_RESPONSE;
					ovRd.Offset = ovRd.OffsetHigh = 0;
					pcr_cmd('S');
				}
				else if (pcr_state == PCR_STATE_WAIT_CMD_RESPONSE && ++nwaits >= 6) {
					printf("Requesting status again!\n");
					ovRd.Offset = ovRd.OffsetHigh = 0;
					pcr_cmd('S');
				}
				continue;

			default:
				printf("Unexpected pcr_wait result %08lx", event);
				continue;
		}

		/* We only get here if read event occurred */

		switch (pcr_state) {
			case PCR_STATE_IDLE:						/* nothing expected from the interface */
				PurgeComm(hpcr, PURGE_RXCLEAR|PURGE_RXABORT);
				break;

			case PCR_STATE_WAIT_CMD_RESPONSE:			/* waiting for response from any command other than P */
				if (pcr_handle_status_byte(nrcvd))
					pcr_state = PCR_STATE_IDLE;
				break;

			case PCR_STATE_WAIT_PICK_CMD_RESPONSE:		/* waiting for response from P command */
				if (pcr_handle_status_byte(nrcvd)) {
					pcr_cmd('\0');						/* queue a response read */
					pcr_state = PCR_STATE_WAIT_DATA_START;
				}
				break;

			case PCR_STATE_WAIT_DATA_START:				/* waiting for leadin character from P command (= or !) */
				if (nrcvd <= 0) {						/* (this could take an indefinite amount of time) */
					if (cr_unit.flags & UNIT_DEBUG)	
						printf("PCR: NO RESP YET\n");

					continue;							/* reader is not ready */
				}

				if (cr_unit.flags & UNIT_DEBUG)			/* (this could take an indefinite amount of time) */
					printf("PCR: GOT %c\n", response_byte);

				switch (response_byte) {
					case '=':							/* = means pick in progress, 160 bytes of data will be coming */
						pcr_state  = PCR_STATE_WAIT_DATA;
						ovRd.Offset = ovRd.OffsetHigh = 0;
						nread = 20;						/* initiate a read */
						ReadFile(hpcr, ((char *) readstation), nread, &nrcvd, &ovRd);
						break;

					case '!':							/* ! means pick has been canceled, status will be coming next */
						pcr_state = PCR_STATE_WAIT_CMD_RESPONSE;
						pcr_cmd('\0');					/* initiate read */
						break;

					default:							/* anything else is a datacomm error, or something */
						/* indicate read check or something */
/*						pcr_state = PCR_STATE_IDLE; */
						break;
				}
				break;

			case PCR_STATE_WAIT_DATA:					/* waiting for data from P command */
				if (cr_unit.flags & UNIT_DEBUG)
					printf((nrcvd <= 0) ? "PCR: NO RESP!\n" : "PCR: GOT %d BYTES\n", nrcvd);

				if (nrcvd > 0) {
					pcr_nleft -= nrcvd;

					begin_pcr_critical_section();
					pcr_nready += nrcvd;
					end_pcr_critical_section();
				}

				if (pcr_nleft > 0) {
					ovRd.Offset = ovRd.OffsetHigh = 0;
					nread = min(pcr_nleft, 20);
					ReadFile(hpcr, ((char *) readstation)+160-pcr_nleft, nread, &nrcvd, &ovRd);
				}
				else {
					pcr_state = PCR_STATE_WAIT_PICK_FINAL_RESPONSE;
					pcr_cmd('\0');							/* queue read */
				}
				break;

			case PCR_STATE_WAIT_PICK_FINAL_RESPONSE:		/* waiting for status byte after last of the card data */
				if (pcr_handle_status_byte(nrcvd)) {
					readstate = STATION_READ;
					pcr_state = PCR_STATE_IDLE;
					pcr_done  = TRUE;
				}
				break;
		}
	}

	CloseHandle(ovRd.hEvent);
	CloseHandle(ovWr.hEvent);

	return 0;
}

/* pcr_cmd - issue command byte to interface. Read of response byte is queued */

static void pcr_cmd (char cmd)
{
	long nwritten, nrcvd;
	int status;

	if (cmd != '\0') {
		if (cr_unit.flags & UNIT_DEBUG /* && (cmd != 'S' || cmd != lastcmd) */)
			printf("PCR: SENT %c\n", cmd);

		lastcmd = cmd;

		ResetEvent(ovWr.hEvent);
		ovWr.Offset = ovWr.OffsetHigh = 0;
		status = WriteFile(hpcr, &cmd, 1, &nwritten, &ovWr);
		if (status == 0 && GetLastError() != ERROR_IO_PENDING)
			printf("Error initiating write in pcr_cmd\n");
	}

	ovRd.Offset = ovRd.OffsetHigh = 0;
	status = ReadFile(hpcr, &response_byte, 1, &nrcvd, &ovRd);		/* if no bytes ready, just return -- a later wait-event will catch it */
	if (status == 0 && GetLastError() != ERROR_IO_PENDING)
		printf("Error initiating read in pcr_cmd\n");

/*	if (cr_unit.flags & UNIT_DEBUG)
 * 		if (nrcvd == 0)
 *			printf("PCR: NO RESPONSE\n");
 *		else
 *			printf("PCR: RESPONSE %c\n", response_byte); */

	nwaits = 0;
}

/* pcr_handle_status_byte - handle completion of read of response byte */

static BOOL pcr_handle_status_byte (int nrcvd)
{
	static char prev_status = '?';
	BOOL show;

	if (nrcvd <= 0)
		return FALSE;

	pcr_status = response_byte;						/* save new status */

	show = lastcmd != 'S' || pcr_status != prev_status;

	if ((cr_unit.flags & UNIT_DEBUG) && show) {
		printf("PCR: status %c\n", pcr_status);
		prev_status = pcr_status;
	}

	pcr_set_dsw_from_status(FALSE);

	return TRUE;
}

/* pcr_set_dsw_from_status - construct device status word from current physical reader status */

static void pcr_set_dsw_from_status (BOOL post_pick)
{
													/* set 1130 status word bits */
	CLRBIT(cr_dsw, CR_DSW_1442_LAST_CARD | CR_DSW_1442_BUSY | CR_DSW_1442_NOT_READY | CR_DSW_1442_ERROR_CHECK);

	if (pcr_status & PCR_STATUS_HEMPTY)				
		SETBIT(cr_dsw, CR_DSW_1442_LAST_CARD | CR_DSW_1442_NOT_READY);

	if (pcr_status & PCR_STATUS_ERROR)
		SETBIT(cr_dsw, CR_DSW_1442_ERROR_CHECK);

	/* we have a problem -- ready doesn't come back up right away after a pick. */
	/* I think I'll fudge this and not set NOT_READY immediately after a pick   */

	if ((! post_pick) && ! (pcr_status & PCR_STATUS_READY))
		SETBIT(cr_dsw, CR_DSW_1442_NOT_READY);

	if (CURRENT_OP != OP_IDLE)
		SETBIT(cr_dsw, CR_DSW_1442_BUSY | CR_DSW_1442_NOT_READY);
}

static void pcr_xio_feedcycle (void)
{
	SET_OP(OP_FEEDING);
	cr_unit.COLUMN = -1;
	SetEvent(hPickEvent);
	sim_activate(&cr_unit, cr_wait);			/* keep checking frequently */
}

static void pcr_xio_startread (void)
{
	SET_OP(OP_READING);
	cr_unit.COLUMN = -1;
	pcr_nleft  = 160;
	pcr_nready = 0;
	SetEvent(hPickEvent);
	sim_activate(&cr_unit, cr_wait);			/* keep checking frequently */
}

static void pcr_reset (void)
{
	pcr_status = PCR_STATUS_HEMPTY;				/* set default status: offline, no cards */
	pcr_state  = PCR_STATE_IDLE;
	cr_dsw     = CR_DSW_1442_LAST_CARD | CR_DSW_1442_NOT_READY;

	sim_cancel(&cr_unit);

	SetEvent(hResetEvent);
}

/* pcr_trigger_interrupt_0 - simulate a read response interrupt so OS will read queued column data */

static void pcr_trigger_interrupt_0 (void)
{
	if (++cr_unit.COLUMN < 80) {
		SETBIT(cr_dsw,  CR_DSW_1442_READ_RESPONSE);
		SETBIT(ILSW[0], ILSW_0_1442_CARD);
		calc_ints();

		begin_pcr_critical_section();
		pcr_nready -= 2;
		end_pcr_critical_section();

		if (cr_unit.flags & UNIT_DEBUG)
			printf("SET IRQ0 col %d\n", cr_unit.COLUMN+1);
	}
}

static t_stat pcr_svc (UNIT *uptr)
{
	switch (CURRENT_OP) {
		case OP_IDLE:
			break;

		case OP_READING:
			if (pcr_nready >= 2) {							/* if there is a whole column buffered, simulate column interrupt*/
															/* pcr_trigger_interrupt_0 - simulate a read response interrupt so OS will read queued column data */

				pcr_trigger_interrupt_0();
				sim_activate(&cr_unit, cr_wait);			/* keep checking frequently */
			}
			else if (pcr_done) {
				pcr_done = FALSE;
				cr_count++;
				op_done(&cr_unit, "pcr read", TRUE);
				pcr_set_dsw_from_status(TRUE);
			}
			else
				sim_activate(&cr_unit, cr_wait);			/* keep checking frequently */
			break;

		case OP_FEEDING:
			if (pcr_done) {
				cr_count++;
				op_done(&cr_unit, "pcr feed", FALSE);
				pcr_set_dsw_from_status(TRUE);
			}
			else
				sim_activate(&cr_unit, cr_wait);			/* keep checking frequently */

			break;

		case OP_PUNCHING:
			return cr_svc(uptr);
	}

	return SCPE_OK;
}

static CRITICAL_SECTION pcr_critsect;

static void begin_pcr_critical_section (void)
{
	static BOOL mustinit = TRUE;

	if (mustinit) {
		InitializeCriticalSection(&pcr_critsect);
		mustinit = FALSE;
	}

	EnterCriticalSection(&pcr_critsect);
}

static void end_pcr_critical_section (void)
{
	LeaveCriticalSection(&pcr_critsect);
}

#endif

