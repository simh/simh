/* ibm1130_sys.c: IBM 1130 simulator interface

   Copyright (c) 2002, Brian Knittel
   Based on PDP-11 simulator written by Robert M Supnik

   Revision History
   0.24 2002Mar27 - Fixed BOSC bug; BOSC works in short instructions too
   0.23 2002Feb26 - Added @decklist feature for ATTACH CR.
   0.22 2002Feb26 - Replaced "strupr" with "upcase" for compatibility.
   0.21	2002Feb25 - Some compiler compatibiity changes, couple of compiler-detected
                    bugs
   0.01 2001Jul31 - Derived from pdp11_sys.c, which carries this disclaimer:

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
#include <ctype.h>

extern DEVICE cpu_dev, console_dev, dsk_dev, cr_dev, cp_dev;
extern DEVICE tti_dev, tto_dev, prt_dev, log_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern int32 saved_PC;

/* SCP data structures and interface routines

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		number of words for examine
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

char sim_name[]    = "IBM 1130";
char sim_version[] = "V0.24";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
	&cpu_dev,			/* the cpu */
	&log_dev,			/* cpu logging virtual device */
#ifdef GUI_SUPPORT
	&console_dev,		/* console display (windows GUI) */
#endif
	&dsk_dev,			/* disk drive(s) */
	&cr_dev,			/* card reader/punch */
	&cp_dev,
	&tti_dev,			/* console keyboard, selectric printer */
	&tto_dev,
	&prt_dev,			/* 1132 printer */
	NULL
};

const char *sim_stop_messages[] = {
	"Unknown error",
	"Wait",
	"Invalid command", 
	"Simulator breakpoint",
	"Use of incomplete simulator function",
};

/* Loader. IPL is normally performed by card reader (boot command). This function
 * loads hex data from a file for testing purposes. The format is:
 *
 *   blank lines or lines starting with ; / or # are ignored as comments
 *
 *   @XXXX			set load addresss to hex value XXXX
 *   XXXX			store hex word value XXXX at current load address and increment address
 *   ...
 *   =XXXX			set IAR to hex value XXXX
 *   ZXXXX			zero XXXX words and increment load address
 *   SXXXX			set console entry switches to XXXX. This lets a program specify the
 *					default value for the toggle switches.
 *
 * Multiple @ and data sections may be entered. If more than one = or S value is specified
 * the last one wins.
 *
 * Note: the load address @XXXX and data values XXXX can be followed by the letter
 * R to indicate that the values are relocatable addresses. This is ignored in this loader,
 * but the asm1130 cross assembler may put them there.
 */

t_stat my_load (FILE *fileref, char *cptr, char *fnam)
{
	char line[150], *c;
	int iaddr = -1, runaddr = -1, val, nwords;

	while (fgets(line, sizeof(line), fileref) != NULL) {
		for (c = line; *c && *c <= ' '; c++)				// find first nonblank
			;

		if (*c == '\0' || *c == '#' || *c == '/' || *c == ';')
			continue;									// empty line or comment

		if (*c == '@') {								// set load address
			if (sscanf(c+1, "%x", &iaddr) != 1)
				return SCPE_FMT;
		}
		else if (*c == '=') {
			if (sscanf(c+1, "%x", &runaddr) != 1)
				return SCPE_FMT;
		}
		else if (*c == 's' || *c == 'S') {
			if (sscanf(c+1, "%x", &val) != 1)
				return SCPE_FMT;

			CES = val & 0xFFFF;							// preload console entry switches
		}
		else if (*c == 'z' || *c == 'Z') {
			if (sscanf(c+1, "%x", &nwords) != 1)
				return SCPE_FMT;

			if (iaddr == -1)
				return SCPE_FMT;

			while (--nwords >= 0) {
				WriteW(iaddr, 0);
				iaddr++;
			}
		}
		else if (strchr("0123456789abcdefABCDEF", *c) != NULL) {
			if (sscanf(c, "%x", &val) != 1)
				return SCPE_FMT;

			if (iaddr == -1)
				return SCPE_FMT;

			WriteW(iaddr, val);							// store data
			iaddr++;
		}
		else
			return SCPE_FMT;							// unexpected data
	}

	if (runaddr != -1)
		IAR = runaddr;

	return SCPE_OK;
}

t_stat my_save (FILE *fileref, char *cptr, char *fnam)
{
	int iaddr, nzeroes = 0, nwords = (int) (MEMSIZE/2), val;

	fprintf(fileref, "=%04x\r\n", IAR);
	fprintf(fileref, "@0000\r\n");
	for (iaddr = 0; iaddr < nwords; iaddr++) {
		val = ReadW(iaddr);
		if (val == 0)						// queue up zeroes
			nzeroes++;
		else {
			if (nzeroes >= 4) {				// spit out a Z directive
				fprintf(fileref, "Z%04x\r\n", nzeroes);
				nzeroes = 0;
			}
			else {							// write queued zeroes literally
				while (nzeroes > 0) {
					fprintf(fileref, " 0000\r\n");
					nzeroes--;
				}
			}
			fprintf(fileref, " %04x\r\n", val);
		}
	}
	if (nzeroes >= 4) {						// emit any queued zeroes
		fprintf(fileref, "Z%04x\r\n", nzeroes);
		nzeroes = 0;
	}
	else {
		while (nzeroes > 0) {
			fprintf(fileref, " 0000\r\n");
			nzeroes--;
		}
	}

	return SCPE_OK;
}

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
	if (flag)
		return my_save(fileref, cptr, fnam);
	else
		return my_load(fileref, cptr, fnam);
}

/* Specifier decode

   Inputs:
	*of	=	output stream
	addr	=	current PC
	spec	=	specifier
	nval	=	next word
	flag	=	TRUE if decoding for CPU
	iflag	=	TRUE if decoding integer instruction
   Outputs:
	count	=	-number of extra words retired
*/

/* Symbolic decode

   Inputs:
	*of	=	output stream
	addr	=	current PC
	*val	=	values to decode
	*uptr	=	pointer to unit
	sw	=	switches
   Outputs:
	return	=	if >= 0, error code
			if < 0, number of extra words retired
*/

static char *opcode[] = {
	"?00 ",		"XIO ",		"SLA ",		"SRA ",
	"LDS ",		"STS ",		"WAIT",		"?07 ",
	"BSI ",		"BSC ",		"?0A ",		"?0B ",
	"LDX ",		"STD ",		"MDX ",		"?0F ",
	"A   ",		"AD  ",		"S   ",		"SD  ",
	"M   ",		"D   ",		"?16 ",		"?17 ",
	"LD  ",		"LDD ",		"STO ",		"STD ",
	"AND ",		"OR  ",		"EOR ",		"?1F ",
};

static char relative[] = {						// true if short mode displacements are IAR relative
	FALSE,		TRUE,		FALSE,		FALSE,
	FALSE,		TRUE,		FALSE,		FALSE,
	TRUE,		FALSE,		FALSE,		FALSE,
	TRUE,		TRUE,		TRUE,		FALSE,
	TRUE,		TRUE,		TRUE,		TRUE,
	TRUE,		TRUE,		FALSE,		FALSE,
	TRUE,		TRUE,		TRUE,		TRUE,
	TRUE,		TRUE,		TRUE,		FALSE
};

static char *lsopcode[] = {"SLA ", "SLCA ", "SLT ", "SLC "};
static char *rsopcode[] = {"SRA ", "?188 ", "SRT ", "RTE "};
static char tagc[]      = " 123";

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
	int32 cflag, c1, c2, OP, F, TAG, INDIR, DSPLC, IR, eaddr;
	char *mnem, tst[12];

	cflag = (uptr == NULL) || (uptr == &cpu_unit);
	c1    = val[0] & 0177;
	c2    = (val[0] >> 8) & 0177;

	if (sw & SWMASK ('A')) {				/* ASCII? */
		fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
		return SCPE_OK;
	}

	if (sw & SWMASK ('C')) {				/* character? */
		fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
		fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
		return SCPE_OK;
	}

	if (! (sw & SWMASK ('M')))
		return SCPE_ARG;

	IR  = val[0];
	OP  = (IR >> 11) & 0x1F;			/* opcode */
	F   = IR & 0x0400;					/* format bit: 1 = long instr */
	TAG = IR & 0x0300;					/* tag bits: index reg select */
	if (TAG)
		TAG >>= 8;

	if (F) {							/* long instruction, ASSUME it's valid (have to decrement IAR if not) */
		INDIR = IR & 0x0080;			/* indirect bit */
		DSPLC = IR & 0x007F;			/* displacement or modifier */
		if (DSPLC & 0x0040)
			DSPLC |= ~ 0x7F;			/* sign extend */

		eaddr = val[1];					/* get reference address */
	}
	else {								/* short instruction, use displacement */
		INDIR = 0;						/* never indirect */
		DSPLC = IR & 0x00FF;			/* get displacement */
		if (DSPLC & 0x0080)
			DSPLC |= ~ 0xFF;

		eaddr = DSPLC;
		if (relative[OP] && ! TAG)
			eaddr += addr+1;			/* turn displacement into address */
	}

	mnem = opcode[OP];					/* get mnemonic */
	if (OP == 0x02) {					/* left shifts are special */
		mnem = lsopcode[(DSPLC >> 6) & 0x0003];
		DSPLC &= 0x003F;
		eaddr = DSPLC;
	}
	else if (OP == 0x03) {				/* right shifts too */
		mnem = rsopcode[(DSPLC >> 6) & 0x0003];
		DSPLC &= 0x003F;
		eaddr = DSPLC;
	}
	else if (OP == 0x09) {
		if (IR & 0x40)
			mnem = "BOSC";

		tst[0] = '\0';
		if (DSPLC & 0x20)	strcat(tst, "Z");
		if (DSPLC & 0x10)	strcat(tst, "-");
		if (DSPLC & 0x08)	strcat(tst, "+");
		if (DSPLC & 0x04)	strcat(tst, "E");
		if (DSPLC & 0x02)	strcat(tst, "C");
		if (DSPLC & 0x01)	strcat(tst, "O");

		if (F) {
			fprintf(of, "%04x %s %c%c %s,%04x   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], tst, eaddr & 0xFFFF);
			return -1;
		}
		fprintf(of, "%04x %s %c%c %s   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], tst);
		return SCPE_OK;
	}
	else if (OP == 0x0e && TAG == 0) {		// MDX with no tag => MDM or jump
		if (F) {
			fprintf(of, "%04x %s %c%c %04x,%x (%d)   ", IR & 0xFFFF, "MDM ", (INDIR ? 'I' : 'L'), tagc[TAG], eaddr & 0xFFFF, DSPLC & 0xFFFF, DSPLC);
			return -1;
		}
		mnem = "JMP ";
	}

	fprintf(of, "%04x %s %c%c %04x   ", IR & 0xFFFF, mnem, F ? (INDIR ? 'I' : 'L') : ' ', tagc[TAG], eaddr & 0xFFFF);
	return F ? -1 : SCPE_OK;			/* inform how many words we read */
}

int32 get_reg (char *cptr, const char *strings[], char mchar)
{
return -1;
}

/* Number or memory address

   Inputs:
	*cptr	=	pointer to input string
	*dptr	=	pointer to output displacement
	*pflag	=	pointer to accumulating flags
   Outputs:
	cptr	=	pointer to next character in input string
			NULL if parsing error

   Flags: 0 (no result), A_NUM (number), A_REL (relative)
*/

char *get_addr (char *cptr, int32 *dptr, int32 *pflag)
{
	return 0;
}

/* Specifier decode

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	n1	=	0 if no extra word used
			-1 if extra word used in prior decode
	*sptr	=	pointer to output specifier
	*dptr	=	pointer to output displacement
	cflag	=	true if parsing for the CPU
	iflag	=	true if integer specifier
   Outputs:
	status	=	= -1 extra word decoded
			=  0 ok
			= +1 error
*/

t_stat get_spec (char *cptr, t_addr addr, int32 n1, int32 *sptr, t_value *dptr,
	int32 cflag, int32 iflag)
{
	return -1;
}

/* Symbolic input

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	*uptr	=	pointer to unit
	*val	=	pointer to output values
	sw	=	switches
   Outputs:
	status	=	> 0   error code
			<= 0  -number of extra words
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
	return SCPE_ARG;
}
