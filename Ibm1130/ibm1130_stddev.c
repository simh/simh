/* ibm1130_stddev.c: IBM 1130 standard I/O devices simulator

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel

   Revision History:

   2004.10.22 - Removed stub for xio_1134_papertape as it's now a supported device

   2003.11.23 - Fixed bug in new routine "quotefix" that made sim crash
   			    for all non-Windows builds :(

   2003.06.15 - added output translation code to accomodate APL font
   				added input translation feature to assist emulation of 1130 console keyboard for APL
   				changes to console input and output IO emulation, fixed bugs exposed by APL interpreter

   2002.09.13 - pulled 1132 printer out of this file into ibm1130_prt.c

 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 *
 * Notes about overstrike mapping:
 * The 1130 console printer used a Selectric typewriter element. The APL interpreter
 * used overprinting to construct some APL symbols, for example, a round O overstruck]
 * with | to get the greek phi. This doesn't accomodate a glass terminal! Instead,
 * modern APL fonts have separate character codes for the complex characters. 
 * To have APL\1130 output appear correctly, we have to do three things:
 *
 *		use simh's telnet feature to connect to the 1130 console stream
 *		have the telnet program use an APL font
 *		detect combinations of overstruck symbols, and generate the approrpiate alternate codes.
 *
 * There is a built-in table of font mappings and overstrike mappings, for the APLPLUS.TTF
 * truetype font widely available on the Internet. An font descriptor file can be used
 * to specify alternate mappings.
 *
 * The APL font codes and overstrike mapping can be enabled with the simh command
 *
 *		set tto apl
 *
 * and disabled with
 *
 *		set tto ascii  			(this is the default)
 *
 * APL also uses the red and black ribbon selection. The emulator will output
 * ansi red/black foreground commands with the setting
 *
 *		set tto ansi
 *
 * The codes can be disabled with
 *
 *		set tto noansi 			(this is the default)
 *
 * Finally, when APL mode is active, the emulator does some input key translations
 * to let the standard ASCII keyboard more closely match the physical layout of the
 * 1130 console keyboard. The numeric and punctuation key don't have their
 * traditional meaning under APL. The input mapping lets you use the APL keyboard
 * layout shown in the APL documentation.
 *
 * The translations are:
 * FROM
 * ASCII	Position on keyboard		To	1130 Key	APL interpretation
 * ------------------------------------	--------------------------------
 *	[		(key to right of P)			\r	Enter		left arrow
 *	;		(1st key to right of L)		\b	Backspace	[
 *	'		(2nd key to right of L)		^U	Erase Fld	]
 *	2		(key above Q)				@ 	@			up shift
 *	3		(key above W)				% 	%			up right shift
 *	4		(key above E)				*	*			+
 *	5		(key above R)				<	<			multiply
 *	8		(key above U)				-	-			Return
 *	9		(key above I)				/	/			Backspace
 *	-		(key above P)				^Q	INT REQ		ATTN
 *	Enter								-	-			Return 
 *	backsp								/	/			Backspace
 */

#include "ibm1130_defs.h"
#include <memory.h>

/* #define DEBUG_CONSOLE */

/* ---------------------------------------------------------------------------- */

static void badio (const char *dev)
{
/* the real 1130 just ignores attempts to use uninstalled devices. They get tested
 * at times, so it's best to just be quiet about this
 * printf("%s I/O is not yet supported", dev);
 */
}

void xio_1231_optical	(int32 addr, int32 func, int32 modify)			{badio("optical mark");}
void xio_system7		(int32 addr, int32 func, int32 modify)			{badio("System 7");}

/* ---------------------------------------------------------------------------- */

#define MAX_OUTPUT_COLUMNS 100				/* width of 1130 console printer */
#define MAX_OS_CHARS 	     4				/* maximum number of overstruck characters that can be mapped */
#define MAX_OS_MAPPINGS	   100				/* maximum number of overstrike mappings */

typedef struct tag_os_map {					/* os_map = overstrike mapping */
	int ch;									/* ch = output character */
	int nin;								/* nin = number of overstruck characters */
	unsigned char inlist[MAX_OS_CHARS];		/* inlist = overstruck ASCII characters, sorted. NOT NULL TERMINATED */
} OS_MAP;

extern int cgi;

static int32 tti_dsw = 0;					/* device status words */
static int32 tto_dsw = 0;
       int32 con_dsw = 0;
					
static unsigned char conout_map[256];		/* 1130 console code to ASCII translation. 0 = undefined, 0xFF = IGNR_ = no output */
static unsigned char conin_map[256];		/* input mapping */
static int  curcol = 0;						/* current typewriter element column, leftmost = 0 */
static int  maxcol = 0;						/* highest curcol seen in this output line         */
static unsigned char black_ribbon[30];		/* output escape sequence for black ribbon shift   */
static unsigned char red_ribbon[30];		/* output escape sequence for red ribbon shift     */

static OS_MAP os_buf[MAX_OUTPUT_COLUMNS];			/* current typewriter output line, holds character struck in each column */
static OS_MAP os_map[MAX_OS_MAPPINGS];				/* overstrike mapping entries */
static int n_os_mappings;							/* number of overstrike mappings */

static t_stat tti_svc(UNIT *uptr);
static t_stat tto_svc(UNIT *uptr);
static t_stat tti_reset(DEVICE *dptr);
static t_stat tto_reset(DEVICE *dptr);

static t_stat emit_conout_character(int ch);
static t_stat map_conout_character(int ch);
static void   reset_mapping (void);
static void   set_conout_mapping(int32 flags);
static t_stat validate_conout_mapping(UNIT *uptr, int32 match, CONST char *cvptr, void *desc);
static void   set_default_mapping(int32 flags);
static void   finish_conout_mapping(int32 flags);
static void   strsort (int n, unsigned char *s);		/* sorts an array of n characters */
static int    os_map_comp (OS_MAP *a, OS_MAP *b);		/* compares two mapping entries */
static t_stat font_cmd(int32 flag, CONST char *cptr);			/* handles font command */
static void   read_map_file(FILE *fd);					/* reads a font map file */
static t_bool str_match(const char *str, const char *keyword);/* keyword/string comparison */
static const char * handle_map_ansi_definition(char **pc);	/* input line parsers for map file sections */
static const char * handle_map_input_definition(char **pc);
static const char * handle_map_output_definition(char **pc);
static const char * handle_map_overstrike_definition(char **pc);

#define UNIT_V_CSET		(UNIT_V_UF + 0)		/* user flag: character set */
#define UNIT_V_LOCKED	(UNIT_V_UF + 2)		/* user flag: keyboard locked */
#define UNIT_V_ANSI		(UNIT_V_UF + 3)

#define CSET_ASCII		(0u << UNIT_V_CSET)
#define CSET_1130		(1u << UNIT_V_CSET)
#define CSET_APL		(2u << UNIT_V_CSET)	
#define CSET_MASK		(3u << UNIT_V_CSET)
#define ENABLE_ANSI		(1u << UNIT_V_ANSI)

#define KEYBOARD_LOCKED	(1u << UNIT_V_LOCKED)

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
	{ ORDATA (BUF,   tti_unit.buf,  16) },
	{ ORDATA (DSW,   tti_dsw,       16) },
	{ DRDATA (POS,   tti_unit.pos,  31), PV_LEFT },
	{ DRDATA (STIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
	{ NULL }  };

MTAB tti_mod[] = {
	{ CSET_MASK,  CSET_ASCII,  "ASCII", "ASCII",  NULL},
	{ CSET_MASK,  CSET_1130,   "1130",  "1130",   NULL},
	{ 0 }  };

DEVICE tti_dev = {
	"KEYBOARD", &tti_unit, tti_reg, tti_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tti_reset,
	NULL, basic_attach, NULL };

/* TTO data structures

   tto_dev	TTO device descriptor
   tto_unit	TTO unit descriptor
   tto_reg	TTO register list
*/

		/* 14-Nov-03 -- the wait time was SERIAL_OUT_WAIT, but recent versions of SIMH reduced
		 * this to 100, and wouldn't you know it, APL\1130 has about 120 instructions between the XIO WRITE
		 * to the console and the associated WAIT.
		 */

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), 200 };

REG tto_reg[] = {
	{ ORDATA (BUF, tto_unit.buf, 16) },
	{ ORDATA (DSW, tto_dsw, 16) },
	{ DRDATA (POS, tto_unit.pos, 31), PV_LEFT },
	{ DRDATA (STIME, tto_unit.wait, 24), PV_LEFT },
	{ NULL }  };

MTAB tto_mod[] = {
	{ CSET_MASK,  CSET_ASCII,  "ASCII",  "ASCII",  validate_conout_mapping, NULL, NULL},
	{ CSET_MASK,  CSET_1130,   "1130",   "1130",   validate_conout_mapping, NULL, NULL},
	{ CSET_MASK,  CSET_APL,    "APL",    "APL",    validate_conout_mapping, NULL, NULL},
	{ ENABLE_ANSI,0,		   "NOANSI", "NOANSI", NULL},
	{ ENABLE_ANSI,ENABLE_ANSI, "ANSI",   "ANSI",   NULL},
	{ 0 }  };

DEVICE tto_dev = {
	"TTO", &tto_unit, tto_reg, tto_mod,
	1, 10, 31, 1, 8, 8,
	NULL, NULL, &tto_reset,
	NULL, basic_attach, NULL };

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
			SETBIT(tti_dsw, TT_DSW_KEYBOARD_BUSY);		/* select and unlock the keyboard */
			keyboard_selected(TRUE);
			CLRBIT(tti_unit.flags, KEYBOARD_LOCKED);
			tti_unit.buf = 0;							/* no key character yet */
			break;

		case XIO_READ:							   	
			WriteW(iocc_addr, tti_unit.buf);			/* return keycode */
			CLRBIT(tti_dsw, TT_DSW_KEYBOARD_BUSY);		/* this ends selected mode */
			keyboard_selected(FALSE);
			SETBIT(tti_unit.flags, KEYBOARD_LOCKED);	/* keyboard is locked when not selected */
			tti_unit.buf = 0;							/* subsequent reads will return zero */
			break;

		case XIO_WRITE:
			ch = (ReadW(iocc_addr) >> 8) & 0xFF;		/* get character to write */
			tto_unit.buf = emit_conout_character(ch);	/* output character and save write status */

/*			fprintf(stderr, "[CONOUT] %02x\n", ch); */

			SETBIT(tto_dsw, TT_DSW_PRINTER_BUSY);
			sim_activate(&tto_unit, tto_unit.wait);		/* schedule interrupt */
			break;

		case XIO_SENSE_DEV:
			ACC = tto_dsw | tti_dsw;
			if (modify & 0x01) {						/* reset interrupts */
				CLRBIT(tto_dsw, TT_DSW_PRINTER_RESPONSE);
				CLRBIT(tti_dsw, TT_DSW_KEYBOARD_RESPONSE);
				CLRBIT(tti_dsw, TT_DSW_INTERRUPT_REQUEST);
				CLRBIT(ILSW[4], ILSW_4_CONSOLE);
			}
			break;

		default:
			sprintf(msg, "Invalid console XIO function %x", func);
			xio_error(msg);
	}

/*	fprintf(stderr, "After XIO             %04x %04x\n", tti_dsw, tto_dsw); */
}

/* emit_conout_character - write character with 1130 console code 'ch' */

static t_stat emit_conout_character (int ch)
{
	t_stat status;

#ifdef DEBUG_CONSOLE
	printf("{%02x}", ch); 
#endif

	if ((tto_unit.flags & CSET_MASK) == CSET_1130)	/* 1130 (binary) mode, write the raw 8-bit value */
		return sim_putchar(ch);

	if (ch & COUT_IS_CTRL) {
		/* red/black shift can be combined with another control */
		/* if present, emit the color shift characters alone */

		if (ch & COUT_CTRL_BLACK) {				
			if ((status = map_conout_character(COUT_IS_CTRL|COUT_CTRL_BLACK)) != SCPE_OK)
				return status;
		}
		else if (ch & COUT_CTRL_RED) {
			if ((status = map_conout_character(COUT_IS_CTRL|COUT_CTRL_RED)) != SCPE_OK)
				return status;
		}

		ch &= ~(COUT_CTRL_BLACK|COUT_CTRL_RED);	/* remove the ribbon shift bits */

		if (ch & ~COUT_IS_CTRL)	{				/* if another control remains, emit it */
			if ((status = map_conout_character(ch)) != SCPE_OK)
				return status;
		}

		return SCPE_OK;
	}

	return map_conout_character(ch);
}

static void SendBeep (void)			/* notify user keyboard was locked or key was bad */
{
	sim_putchar(7);
}

/* tti_svc - keyboard polling (never stops) */

static t_stat tti_svc (UNIT *uptr)
{
	int32 temp;

	if (cgi)										/* if running in CGI mode, no keyboard and no keyboard polling! */
		return SCPE_OK;
													/* otherwise, so ^E can interrupt the simulator, */
	sim_activate(&tti_unit, tti_unit.wait);			/* always continue polling keyboard */

	assert(sim_clock_queue != QUEUE_LIST_END);

	temp = sim_poll_kbd();

	if (temp < SCPE_KFLAG)
		return temp;								/* no char or error? */

	temp &= 0xFF;									/* remove SCPE_KFLAG */

	if ((tti_unit.flags & CSET_MASK) == CSET_ASCII)
		temp = conin_map[temp] & 0xFF;				/* perform input translation */

	if (temp == IRQ_KEY) {							/* INT REQ (interrupt request) key -- process this even if no keyboard input request pending */
		SETBIT(tti_dsw, TT_DSW_INTERRUPT_REQUEST);	/* queue interrupt */
		SETBIT(ILSW[4], ILSW_4_CONSOLE);
		calc_ints();

		CLRBIT(tti_unit.flags, KEYBOARD_LOCKED);	/* keyboard restore, according to func. char. manual */

#ifdef DEBUG_CONSOLE
		printf("[*IRQ*]");
#endif
		tti_unit.buf = 0;							/* subsequent reads need to return 0 (required by APL\1130) */
		return SCPE_OK;
	}

	if (temp == PROGRAM_STOP_KEY) {					/* simulate the program stop button */
		SETBIT(con_dsw, CPU_DSW_PROGRAM_STOP);
		SETBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);
		calc_ints();

#ifdef DEBUG_CONSOLE
		printf("[*PSTOP*]");
#endif

		return SCPE_OK;
	}
													// keyboard is locked or no active input request?
	if ((tti_unit.flags & KEYBOARD_LOCKED) || ! (tti_dsw & TT_DSW_KEYBOARD_BUSY)) {
		SendBeep();
		calc_ints();
		return SCPE_OK;
	}

	if ((tti_unit.flags & CSET_MASK) == CSET_ASCII) 
		temp = ascii_to_conin[temp];

	if (temp == 0)	{							/* ignore invalid characters (no mapping to 1130 input code) */
		SendBeep();
		calc_ints();
		return SCPE_OK;
	}

	tti_unit.buf = temp & 0xFFFE;				/* save keystroke except last bit (not defined) */
	tti_unit.pos = tti_unit.pos + 1;			/* but it lets us distinguish 0 from no punch ' ' */

#ifdef DEBUG_CONSOLE
	printf("[%04x]", tti_unit.buf & 0xFFFF);
#endif

	SETBIT(tti_unit.flags, KEYBOARD_LOCKED);	/* prevent further keystrokes */

	SETBIT(tti_dsw, TT_DSW_KEYBOARD_RESPONSE);	/* queue interrupt */
	SETBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

/*	fprintf(stderr, "TTI interrupt svc SET %04x %04x\n", tti_dsw, tto_dsw); */

	return SCPE_OK;
}

static t_stat tti_reset (DEVICE *dptr)
{
	tti_unit.buf = 0;
	tti_dsw = 0;

	CLRBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();
	keyboard_selected(FALSE);

	SETBIT(tti_unit.flags, KEYBOARD_LOCKED);

	if (cgi)
		sim_cancel(&tti_unit);					/* in cgi mode, never poll keyboard */
	else
		sim_activate(&tti_unit, tti_unit.wait);	/* otherwise, always poll keyboard */

	return SCPE_OK;
}

/* basic_attach - fix quotes in filename, then call standard unit attach routine */

t_stat basic_attach (UNIT *uptr, CONST char *cptr)
{
    char gbuf[2*CBUFSIZE];

	return attach_unit(uptr, quotefix(cptr, gbuf));	/* fix quotes in filenames & attach */
}

/* quotefix - strip off quotes around filename, if present */

CONST char * quotefix (CONST char *cptr, char * buf)
{
    const char *c;
    int quote;

    while (sim_isspace(*cptr))
        ++cptr;
    if (*cptr == '"' || *cptr == '\'') {
        quote = *cptr++;                        /* remember quote and skip over it */

        cptr = buf;
        for (c = cptr; *c && *c != quote; c++)
            *buf++ = *c;                        /* find closing quote, or end of string */

        if (*c)                                 /* terminate string at closing quote */
            *buf = '\0';
        }
    return cptr;                                /* return pointer to cleaned-up name */
}

t_bool keyboard_is_busy (void)					/* return TRUE if keyboard is not expecting a character */
{
	return (tti_dsw & TT_DSW_KEYBOARD_BUSY);
}

static t_stat tto_svc (UNIT *uptr)
{
	CLRBIT(tto_dsw, TT_DSW_PRINTER_BUSY);
	SETBIT(tto_dsw, TT_DSW_PRINTER_RESPONSE);

	SETBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

/*	fprintf(stderr, "TTO interrupt svc SET %04x %04x\n", tti_dsw, tto_dsw); */

	return (t_stat) tto_unit.buf;				/* return status saved during output conversion */
}

static t_stat tto_reset (DEVICE *dptr)
{
	tto_unit.buf = 0;
	tto_dsw = 0;

	CLRBIT(ILSW[4], ILSW_4_CONSOLE);
	calc_ints();

	sim_cancel(&tto_unit);				/* deactivate unit */

	set_conout_mapping(tto_unit.flags);	/* initialize the overstrike mappings */
										/* register the font-mapping command */
	register_cmd("FONT", font_cmd, 0, "font MAPFILE             use font mapping definitions in MAPFILE\n");

	return SCPE_OK;
}

#ifdef _MSC_VER
#  pragma warning(disable:4245)			/* disable int->char demotion warning caused by characters with high-bit set */
#endif

#ifdef __SUNPRO_C
#  pragma error_messages (off, E_INIT_DOES_NOT_FIT)		/* disable int->char demotion warning caused by characters with high-bit set */
#endif

static struct {							/* default input mapping for APL */
	unsigned char in;
	unsigned char out;
} conin_to_APL[] =
{										/* these map input keys to those in like positions on 1130 keyboard */
    {'[',	'\r'},						/* enter (EOF) is APL left arrow */
    {';',	'\b'},						/* backspace is APL [ */
    {'\'',	'\x15'},					/* ctrl-U, erase field, is APL ]*/
    {'2',	'@'},						/* APL upshift */
    {'3',	'%'},						/* APL rightshift */
    {'4',	'*'},						/* APL + and - */
    {'5',	'<'},						/* APL x and divide */
    {'8',	'-'},						/* APL return */
    {'9',	'/'},						/* APL backspace */
    {'-',	IRQ_KEY},					/* ctrl-q (INT REQ), APL ATTN */
    {'\r',	'-'},						/* APL return */
    {'\b',	'/'}						/* APL backspace */
};

#define NCONIN_TO_APL (sizeof(conin_to_APL)/sizeof(conin_to_APL[0]))

static struct {							/* default output mapping for APLPLUS font */
	unsigned char in;
	unsigned char out;
} conout_to_APL[] =
{
    {'\x01', IGNR_},						/* controls */
    {'\x03', '\n'},
    {'\x05', IGNR_},						/* (black and red are handled by ansi sequences) */
    {'\x09', IGNR_},
    {'\x11', '\b'},
    {'\x21', ' '},
    {'\x41', '\t'},
    {'\x81', CRLF_},

    {'\xC4', '\x30'},						/* (if you're curious, order here is position on APL typeball) */
    {'\xE4', '\x38'},
    {'\xD4', '\x37'},
    {'\xF4', '\x35'},
    {'\xDC', '\x33'},
    {'\xFC', '\x31'},
    {'\xC2', '\x29'},
    {'\xE2', '\x9F'},
    {'\xD2', '\x89'},
    {'\xF2', '\x88'},
    {'\xDA', '\xAF'},
    {'\xC6', '\x5E'},
    {'\xE6', '\xAC'},
    {'\xD6', '\x3E'},
    {'\xF6', '\x3D'},
    {'\xDE', '\x3C'},
    {'\xFE', '\xA8'},
    {'\xC0', '\x5D'},
    {'\xE0', '\x39'},
    {'\xD0', '\x36'},
    {'\xF0', '\x34'},
    {'\xD8', '\x32'},

    {'\x84', '\x84'},
    {'\xA4', '\x59'},
    {'\x94', '\x58'},
    {'\xB4', '\x56'},
    {'\x9C', '\x54'},
    {'\xBC', '\x2F'},
    {'\x82', '\x3B'},
    {'\xA2', '\x9B'},
    {'\x92', '\xBE'},
    {'\xB2', '\x87'},
    {'\x9A', '\x97'},
    {'\x86', '\x85'},
    {'\xA6', '\x86'},
    {'\x96', '\x9C'},
    {'\xB6', '\x9E'},
    {'\x9E', '\x7E'},
    {'\xBE', '\x5C'},
    {'\x80', '\x2C'},
    {'\xA0', '\x5A'},
    {'\x90', '\x57'},
    {'\xB0', '\x55'},
    {'\x98', '\x53'},

    {'\x44', '\x2B'},
    {'\x64', '\x51'},
    {'\x54', '\x50'},
    {'\x74', '\x4E'},
    {'\x5C', '\x4C'},
    {'\x7C', '\x4A'},
    {'\x42', '\x28'},
    {'\x62', '\xBD'},
    {'\x52', '\xB1'},
    {'\x72', '\x7C'},
    {'\x5A', '\x27'},
    {'\x46', '\x2D'},
    {'\x66', '\x3F'},
    {'\x56', '\x2A'},
    {'\x76', '\x82'},
    {'\x5E', '\x8C'},
    {'\x7E', '\xB0'},
    {'\x40', '\x5B'},
    {'\x60', '\x52'},
    {'\x50', '\x4F'},
    {'\x70', '\x4D'},
    {'\x58', '\x4B'},

    {'\x04', '\xD7'},
    {'\x24', '\x48'},
    {'\x14', '\x47'},
    {'\x34', '\x45'},
    {'\x1C', '\x43'},
    {'\x3C', '\x41'},
    {'\x02', '\x3A'},
    {'\x22', '\xBC'},
    {'\x12', '\x5F'},
    {'\x32', '\x98'},
    {'\x1A', '\x83'},
    {'\x06', '\xF7'},
    {'\x26', '\x91'},
    {'\x16', '\x92'},
    {'\x36', '\xB9'},
    {'\x1E', '\x9D'},
    {'\x3E', '\xB8'},
    {'\x00', '\x2E'},
    {'\x20', '\x49'},
    {'\x10', '\x46'},
    {'\x30', '\x44'},
    {'\x18', '\x42'},
};

#define NCONOUT_TO_APL (sizeof(conout_to_APL)/sizeof(conout_to_APL[0]))

static OS_MAP default_os_map[] =			/* overstrike mapping for APLPLUS font */
{
	{'\x8a',	2,	"\x5e\x7e"},
	{'\x8b',	2,	"\x9f\x7e"},
	{'\x8d',	2,	"\x8c\x27"},
	{'\x8e',	3,	"\x8c\x2d\x3a"},
	{'\x8f',	2,	"\x91\x5f"},
	{'\x90',	2,	"\x92\x7e"},
	{'\x93',	2,	"\x91\x7c"},
	{'\x94',	2,	"\x92\x7c"},
	{'\x95',	2,	"\xb0\x82"},
	{'\x96',	2,	"\xb0\x83"},
	{'\x99',	2,	"\x2d\x5c"},
	{'\x9a',	2,	"\x2d\x2f"},
	{'\xae',	2,	"\x2c\x2d"},
	{'\xb2',	2,	"\xb1\x7c"},
	{'\xb3',	2,	"\xb1\x5c"},
	{'\xb4',	2,	"\xb1\x2d"},
	{'\xb5',	2,	"\xb1\x2a"},
	{'\xba',	2,	"\xb9\x5f"},
	{'\xd0',	2,	"\x30\x7e"},
	{'\xd8',	2,	"\x4f\x2f"},
	{'\x21',	2,  "\x27\x2e"},
    {'\xa4',	2,	"\xb0\xb1"},		/*  map degree in circle to circle cross (APL uses this as character error symbol) */
	{'\xf0',	2,	"\xb0\xa8"},
	{'\xfe',	2,	"\x3a\xa8"},
};

#ifdef __SUNPRO_C
#  pragma error_messages (default, E_INIT_DOES_NOT_FIT)		/* enable int->char demotion warning caused by characters with high-bit set */
#endif

#ifdef _MSC_VER
#  pragma warning(default:4245)						/* enable int->char demotion warning */
#endif

/* os_map_comp - compare to OS_MAP entries */

static int os_map_comp (OS_MAP *a, OS_MAP *b)
{
	unsigned char *sa, *sb;
	int i;

	if (a->nin > b->nin)
		return +1;

	if (a->nin < b->nin)
		return -1;

	sa = a->inlist;
	sb = b->inlist;

	for (i = a->nin; --i >= 0;) {
		if (*sa > *sb)
			return +1;

		if (*sa < *sb)
			return -1;

		sa++;
		sb++;
	}

	return 0;
}

/* strsort - sorts the n characters of array 's' using insertion sort */

static void strsort (int n, unsigned char *s)
{
	unsigned char temp;
	int i, big;

	while (--n > 0) {				/* repeatedly */
		big = 0;					/* find largest value of s[0]...s[n] */
		for (i = 1; i <= n; i++)
			if (s[i] > s[big]) big = i;

		temp   = s[n];				/* put largest value at end of array */
		s[n]   = s[big];
		s[big] = temp;
	}	
}

/* file format:

[font XXX]			 font named XXX
OUT					 failure character
OUT IN				 single character mapping
OUT IN IN ...		 overstrike mapping

*/

static void set_conout_mapping (int32 flags)
{
	curcol = 0;
	maxcol = 0;

	/* set the default mappings. We may later override them with settings from an ini file */

	set_default_mapping(flags);
}

/* finish_conout_mapping - sort the finalized overstrike mapping */

static void finish_conout_mapping (int32 flags)
{
	int i, n, big;
	OS_MAP temp;

	for (i = 0; i < n_os_mappings; i++)		/* sort the inlist strings individually */
		strsort(os_map[i].nin, os_map[i].inlist);

	for (n = n_os_mappings; --n > 0; ) {	/* then sort the os_map array itself with insertion sort */
		big = 0;							/* find largest value of s[0]...s[n] */
		for (i = 1; i <= n; i++)
			if (os_map_comp(os_map+i, os_map+big) > 0) big = i;

		if (big != n) {
			temp        = os_map[n];		/* put largest value at end of array */
			os_map[n]   = os_map[big];
			os_map[big] = temp;
		}
	}
}

/* validate_conout_mapping - called when set command gets a new value */

static t_stat validate_conout_mapping (UNIT *uptr, int32 match, CONST char *cvptr, void *desc)
{
	set_conout_mapping(match);
	return SCPE_OK;
}

static void reset_mapping (void)
{
	int i;

	black_ribbon[0] = '\0';							/* erase the ribbon sequences */
	red_ribbon[0]   = '\0';

	memset(conout_map, 0, sizeof(conout_map));		/* erase output mapping */

	n_os_mappings = 0;								/* erase overstrike mapping */

	for (i = (sizeof(conin_map)/sizeof(conin_map[0])); --i >= 0; )
		conin_map[i] = (unsigned char) i;			/* default conin_map is identity map */
}

/* set_default_mapping - create standard font and overstrike map */

static void set_default_mapping (int32 flags)
{
	int i;

	reset_mapping();

	strcpy((char *) black_ribbon, "\033[30m");
	strcpy((char *) red_ribbon,	 "\033[31m");

	switch (flags & CSET_MASK) {
		case CSET_1130:
			break;

		case CSET_ASCII:
			memcpy(conout_map, conout_to_ascii, sizeof(conout_to_ascii));
			break;

		case CSET_APL:
			for (i = NCONOUT_TO_APL; --i >= 0; )
				conout_map[conout_to_APL[i].in] = conout_to_APL[i].out;

			for (i = NCONIN_TO_APL; --i >= 0; )
				conin_map[conin_to_APL[i].in] = conin_to_APL[i].out;

			memcpy(os_map, default_os_map, sizeof(default_os_map));			
			n_os_mappings = (sizeof(default_os_map) / sizeof(default_os_map[0]));
			break;
	}

	finish_conout_mapping(flags);				/* sort conout mapping if necessary */
}

/* sim_putstr - write a string to the console */

t_stat sim_putstr (char *s)
{
	t_stat status;

	while (*s) {
		if ((status = sim_putchar(*s)) != SCPE_OK)
			return status;

		s++;
	}

	return SCPE_OK;
}

/* map_conout_character - translate and write a single character */

static t_stat map_conout_character (int ch)
{
	t_stat status;
	int i, cmp;

	if (ch == (COUT_IS_CTRL|COUT_CTRL_BLACK))
		return (tto_unit.flags & ENABLE_ANSI) ? sim_putstr((char *) black_ribbon) : SCPE_OK;

	if (ch == (COUT_IS_CTRL|COUT_CTRL_RED))
		return (tto_unit.flags & ENABLE_ANSI) ? sim_putstr((char *) red_ribbon)   : SCPE_OK;

	if ((ch = conout_map[ch & 0xFF]) == 0)
		ch = '?';						/* unknown character? print ? */

	if (ch == '\n') {					/* newline: reset overstrike buffer */
		curcol = 0;
		maxcol = -1;
	}
	else if (ch == '\r') {				/* carriage return: rewind to column 0 */
		curcol = 0;
		maxcol = -1;					/* assume it advances paper too */
	}
	else if (ch == '\b') {				/* backspace: back up one character */
		if (curcol > 0)
			curcol--;
	}
	else if (n_os_mappings && ch != (unsigned char) IGNR_) {
		if (curcol >= MAX_OUTPUT_COLUMNS)
			map_conout_character('\x81');		/* precede with automatic carriage return/line feed, I guess */
		
		if (curcol > maxcol) {					/* first time in this column, no overstrike possible yet */
			os_buf[curcol].nin = 0;
			maxcol = curcol;
		}

		if (ch != ' ' && ch != 0) {				/* (if it's not a blank or unknown) */
			os_buf[curcol].inlist[os_buf[curcol].nin] = (unsigned char) ch;
			strsort(++os_buf[curcol].nin, os_buf[curcol].inlist);
		}

		if (os_buf[curcol].nin == 0)			/* if nothing but blanks seen, */
			ch = ' ';							/* output is a blank */
		else if (os_buf[curcol].nin == 1) {		/* if only one printing character seen, display it */
			ch = os_buf[curcol].inlist[0];
		}
		else {									/* otherwise look up mapping */
			ch = '?';

			for (i = 0; i < n_os_mappings; i++) {
				cmp = os_map_comp(&os_buf[curcol], &os_map[i]);
				if (cmp == 0) {					/* a hit */
					ch = os_map[i].ch;
					break;
				}
				else if (cmp < 0)				/* not found */
					break;
			}
		}

		if (curcol < MAX_OUTPUT_COLUMNS)		/* this should now never happen, as we automatically return */
			curcol++;
	}

	switch (ch) {
		case IGNR_:
			break;

		case CRLF_:
			if (! cgi) {
				if ((status = sim_putchar('\r')) != SCPE_OK)
					return status;

				tto_unit.pos++;
			}

			if ((status = sim_putchar('\n')) != SCPE_OK)
				return status;

			tto_unit.pos++;						/* hmm, why do we count these? */
			break;

		default:
			if ((status = sim_putchar(ch)) != SCPE_OK)
				return status;

			tto_unit.pos++;
			break;
	}

	return SCPE_OK;
}

/* font_cmd - parse a font mapping file. Sets input and output translations */

static t_stat font_cmd (int32 flag, CONST char *iptr)
{
	char *fname, quote;
        char gbuf[4*CBUFSIZE], *cptr = gbuf;
	FILE *fd;

    gbuf[sizeof(gbuf)-1] = '\0';
    strncpy(gbuf, iptr, sizeof(gbuf)-1);
	while (*cptr && (*cptr <= ' ')) cptr++;			/* skip blanks */
	if (! *cptr) return SCPE_2FARG;					/* argument missing */

	fname = cptr;									/* save start */
    if (*cptr == '\'' || *cptr == '"') {			/* quoted string */
		quote = *cptr++;							/* remember quote character */
		fname++;									/* skip the quote */

		while (*cptr && (*cptr != quote))			/* find closing quote */
			cptr++;
	}
	else {
		while (*cptr && (*cptr > ' '))				/* find terminating blank */
	    	cptr++; 
	}
	*cptr = '\0';									/* terminate name */

	if ((fd = fopen(fname, "r")) == NULL)
		return SCPE_OPENERR;

	reset_mapping();								/* remove all default mappings */

	read_map_file(fd);
	fclose(fd);

	finish_conout_mapping(tto_unit.flags);
	return SCPE_OK;
}

/* str_match - compare the string str to the keyword, case insensitive */

static t_bool str_match (const char *str, const char *keyword)
{
	char kch, sch;

	while (*keyword) {							/* see if str matches the keyword... */
		kch = *keyword++;						/* get pair of characters */
		sch = *str++;

		if (BETWEEN(kch, 'A', 'Z')) kch += 32;	/* change upper to lower case */
		if (BETWEEN(sch, 'A', 'Z')) sch += 32;

		if (kch != sch)							/* characters must match; if not, quit */
			return FALSE;
	}

	return *str <= ' ' || *str == ';';			/* success if the input string ended or is in whitespace or comment */ 
}

/* read_map_file - process definition lines in opened mapping file */

static void read_map_file (FILE *fd)
{
	char str[256], *c;
    const char *errmsg;
	int lineno = 0;
	enum {SECT_UNDEFINED, SECT_DEFAULT, SECT_ANSI, SECT_INPUT, SECT_OUTPUT, SECT_OVERSTRIKE}
		section = SECT_UNDEFINED;

	while (fgets(str, sizeof(str), fd) != NULL) {
		++lineno;									/* count input lines */

		if ((c = strchr(str, '\n')) != NULL)		/* terminate at newline */
			*c = '\0';

		for (c = str; *c && *c <= ' '; c++)			/* skip blanks */
			;

		if (c[0] == '\0' || c[0] == ';')			/* ignore blank lines and lines starting with ; */
			continue;

		if (*c == '[') {
			if (str_match(c, "[default]")) {		/* check for section separators */
				set_default_mapping(tto_unit.flags);
				section = SECT_UNDEFINED;
				continue;
			}
			if (str_match(c, "[ansi]")) {
				section = SECT_ANSI;
				continue;
			}
			if (str_match(c, "[input]")) {
				section = SECT_INPUT;
				continue;
			}
			if (str_match(c, "[output]")) {
				section = SECT_OUTPUT;
				continue;
			}
			if (str_match(c, "[overstrike]")) {
				section = SECT_OVERSTRIKE;
				continue;
			}
		}

		switch (section) {							/* if we get here, we have a definition line */
			case SECT_ANSI:
				errmsg = handle_map_ansi_definition(&c);
				break;
			case SECT_INPUT:
				errmsg = handle_map_input_definition(&c);
				break;
			case SECT_OUTPUT:
				errmsg = handle_map_output_definition(&c);
				break;
			case SECT_OVERSTRIKE:
				errmsg = handle_map_overstrike_definition(&c);
				break;
			default:
				errmsg = "line occurs before valid [section]";
				break;
		}

		if (errmsg == NULL) {						/* if no other error detected, */
			while (*c && *c <= ' ')					/* skip past any whitespace */
				c++;

			if (*c && *c != ';')					/* if line doesn't end or run into a comment, complain */
				errmsg = "too much stuff on input line";
		}

		if (errmsg != NULL)	{						/* print error message and offending line */
			printf("* Warning: %s", errmsg);

			switch (section) {						/* add section name if possible */
				case SECT_ANSI:			errmsg = "ansi";		break;
				case SECT_INPUT:		errmsg = "input";		break;
				case SECT_OUTPUT:		errmsg = "output";		break;
				case SECT_OVERSTRIKE:	errmsg = "overstrike";	break;
				default:				errmsg = NULL;			break;
			}
			if (errmsg != NULL)
				printf(" in [%s] section", errmsg);

			printf(", line %d\n%s\n", lineno, str);
		}
	}
}

/* get_num_char - read an octal or hex character specification of exactly 'ndigits' digits
 * the input pointers is left pointing to the last character of the number, so that it
 * may be incremented by the caller
 */

static const char * get_num_char (char **pc, unsigned char *out, int ndigits, int base, const char *errmsg)
{
	int ch = 0, digit;
	char *c = *pc;

	while (--ndigits >= 0) {			/* collect specified number of digits */
		if (BETWEEN(*c, '0', '9'))
			digit = *c - '0';
		else if (BETWEEN(*c, 'A', 'F'))
			digit = *c - 'A' + 10;
		else if (BETWEEN(*c, 'a', 'f'))
			digit = *c - 'a' + 10;
		else
			digit = base;

		if (digit >= base)				/* bad digit */
			return errmsg;

		ch = ch * base + digit;			/* accumulate digit */
		c++;
	}

	*out = (unsigned char) ch;			/* return parsed character */
	*pc  = c-1;							/* make input pointer point to last character seen */
	return NULL;						/* no error */
}

/* get_characters - read character specification(s) from input string pointed to 
 * by *pc. Results stored in outstr; up to nmax characters parsed. Actual number
 * found returned in *nout. Returns NULL on success or error message if syntax
 * error encountered. *pc is advanced to next whitespace or whatever followed input.
 */

static const char * get_characters (char **pc, unsigned char *outstr, int nmax, int *nout)
{
	char *c = *pc;
    const char *errstr;
	unsigned char *out = outstr;

	while (*c && *c <= ' ')					/* skip leading whitespace */
		c++;

	while (--nmax >= 0) {					/* get up to maximum number of characters */
		if (*c == ';' || *c <= ' ')			/* we ran into a comment, whitespace or end of string: we're done */
			break;

		if (*c == '\\') {					/* backslash escape of some sort */
			switch (*++c) {
				case 'b':					/* backspace */
				case 'B':
					*out++ = '\b';
					break;

				case 'e':					/* ascii ESCAPE */
				case 'E':
					*out++ = '\033';
					break;

				case 'f':					/* formfeed */
				case 'F':
					*out++ = '\f';
					break;

				case 'n':					/* newline */
				case 'N':
					*out++ = '\n';
					break;

				case 'r':					/* return */
				case 'R':
					*out++ = '\r';
					break;

				case 't':					/* tab */
				case 'T':
					*out++ = '\t';
					break;

				case 'x':					/* hex specification */
				case 'X':
					c++;
					if ((errstr = get_num_char(&c, out, 2, 16, "bad hex character")) != NULL)
						return errstr;

					out++;					/* advance out pointer */
					break;

				default:					/* anything else */
					if (BETWEEN(*c, '0', '7')) {	/* octal specification */
						if ((errstr = get_num_char(&c, out, 3, 8, "bad octal character")) != NULL)
							return errstr;
						
						out++;						/* advance out pointer */
					}
					else if (BETWEEN(*c, 'A', 'Z') || BETWEEN(*c, 'a', 'z'))
						return "invalid \\ escape";	/* other \x letters are bad */
					else {
						*out++ = (unsigned char) *c;/* otherwise, accept \x as literal character x */
					}
					break;
			}
		}
		else if (*c == '^') {				/* control character */
			c++;
			if (BETWEEN(*c, 'A', 'Z'))		/* convert alpha, e.g. A -> 1 */
				*out++ = (unsigned char) (*c - 'A' + 1);
			else if (BETWEEN(*c, 'a', 'z'))
				*out++ = (unsigned char) (*c - 'z' + 1);
			else							/* non alpha is bad */
				return "invalid control letter";
		}
		else if (str_match(c, "IGNORE")) {	/* magic word: a character that will never be output */
			*out++ = (unsigned char) IGNR_;
			c += 6;
		}
		else {
			*out++ = (unsigned char) *c;	/* save literal character */
		}

		c++;
	}

	if (*c && *c != ';' && *c > ' ')		/* we should be at end of string, whitespace or comment */
		return "too many characters specified";

	*pc = c;								/* save advanced pointer */
	*nout = out-outstr;						/* save number of characters stored */

	return NULL;							/* no error */
}

/* handle_map_ansi_definition - process line in [ansi] section */

static const char * handle_map_ansi_definition (char **pc)
{
	unsigned char *outstr;
	const char *errmsg;
	int n;

	if (str_match(*pc, "black")) {							/* find which string we're setting */
		outstr = black_ribbon;								/* this is where we'll save the output string */
		*pc += 5;											/* skip over the token */
	}
	else if (str_match(*pc, "red")) {
		outstr = red_ribbon;
		*pc += 3;
	}
	else
		return "invalid variable name";
															/* get list of characters */
	if ((errmsg = get_characters(pc, outstr, sizeof(black_ribbon)-1, &n)) != NULL)
		return errmsg;

	outstr[n] = '\0';										/* null terminate the string */

	return (n > 0) ? NULL : "missing output string";		/* NULL if OK, error msg if no characters */
}

/* handle_map_input_definition - process line in [input] section */

static const char * handle_map_input_definition (char **pc)
{
	unsigned char cin, cout;
	const char *errmsg;
	int n;

	if ((errmsg = get_characters(pc, &cin, 1, &n)) != NULL)	/* get input character */
		return errmsg;

	if (n != 1)
		return "missing input character";

	if ((errmsg = get_characters(pc, &cout, 1, &n)) != NULL)	/* get output character */
		return errmsg;

	if (n != 1)
		return "missing output character";

	conin_map[cin] = cout;									/* set the mapping */
	return NULL;
}

/* handle_map_output_definition - process line in [output] section */

static const char * handle_map_output_definition (char **pc)
{
	unsigned char cin, cout;
	const char *errmsg;
	int n;

	if ((errmsg = get_characters(pc, &cin, 1, &n)) != NULL)	/* get input character */
		return errmsg;

	if (n != 1)
		return "missing input character";

	if ((errmsg = get_characters(pc, &cout, 1, &n)) != NULL)	/* get output character */
		return errmsg;

	if (n != 1)
		return "missing output character";

	conout_map[cin] = cout;									/* set the mapping */
	return NULL;
}

/* handle_map_overstrike_definition - process line in [overstrike] section */

static const char * handle_map_overstrike_definition (char **pc)
{
	unsigned char ch, inlist[MAX_OS_CHARS];
	const char *errmsg;
	int nin;

	if (n_os_mappings >= MAX_OS_MAPPINGS)					/* os_map is full, no more room */
		return "too many overstrike mappings";
															/* get output character */
	if ((errmsg = get_characters(pc, &ch, 1, &nin)) != NULL)
		return errmsg;

	if (nin != 1)
		return "missing output character";
															/* get input list */
	if ((errmsg = get_characters(pc, inlist, MAX_OS_CHARS, &nin)) != NULL)
		return errmsg;

	if (nin < 2)											/* expect at least two characters overprinted */
		return "missing input list";

	os_map[n_os_mappings].ch  = ch;							/* save in next os_map slot */
	os_map[n_os_mappings].nin = nin;
	memmove(os_map[n_os_mappings].inlist, inlist, nin);

	n_os_mappings++;
	return NULL;
}
