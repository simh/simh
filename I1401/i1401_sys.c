/* i1401_sys.c: IBM 1401 simulator interface

   Copyright (c) 1993-2002, Robert M. Supnik

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

   03-Jun-02	RMS	Added 1311 support
   18-May-02	RMS	Added -D feature from Van Snyder
   26-Jan-02	RMS	Fixed H, NOP with no trailing wm (found by Van Snyder)
   17-Sep-01	RMS	Removed multiconsole support
   13-Jul-01	RMS	Fixed bug in symbolic output (found by Peter Schorn)
   27-May-01	RMS	Added multiconsole support
   14-Mar-01	RMS	Revised load/dump interface (again)
   30-Oct-00	RMS	Added support for examine to file
   27-Oct-98	RMS	V2.4 load interface
*/

#include "i1401_defs.h"
#include <ctype.h>

#define LINE_LNT	80
extern DEVICE cpu_dev, inq_dev, lpt_dev;
extern DEVICE cdr_dev, cdp_dev, stack_dev;
extern DEVICE dp_dev, mt_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint8 M[];
extern char bcd_to_ascii[64], ascii_to_bcd[128];
extern char *get_glyph (char *cptr, char *gbuf, char term);
extern int32 store_addr_h (int32 addr);
extern int32 store_addr_t (int32 addr);
extern int32 store_addr_u (int32 addr);

/* SCP data structures and interface routines

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		maximum number of words for examine/deposit
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

char sim_name[] = "IBM 1401";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = LINE_LNT;

DEVICE *sim_devices[] = {
	&cpu_dev,
	&inq_dev,
	&cdr_dev,
	&cdp_dev,
	&stack_dev,
	&lpt_dev,
	&mt_dev,
	&dp_dev,
	NULL };

const char *sim_stop_messages[] = {
	"Unknown error",
	"Unimplemented instruction",
	"Non-existent memory",
	"Non-existent device",
	"No WM at instruction start",
	"Invalid A address",
	"Invalid B address",
	"Invalid instruction length",
	"Invalid modifer",
	"Invalid branch address",
	"Breakpoint",
	"HALT instruction",
	"Invalid MT unit number",
	"Invalid MT record length",
	"Write to locked MT unit",  
	"Skip to unpunched CCT channel",
	"Card reader empty",
	"Address register wrap",
	"MCE data field too short",
	"MCE control field too short",
	"MCE EPE hanging $",
	"I/O check",
	"Invalid disk sector address",
	"Invalid disk sector count",
	"Invalid disk unit",
	"Invalid disk function",
	"Invalid disk record length",
	"Write track while disabled",
	"Write check error",
	"Disk address miscompare",
	"Direct seek cylinder exceeds maximum"
  };

/* Binary loader -- load carriage control tape

   A carriage control tape consists of entries of the form

	(repeat count) column number,column number,column number,...

   The CCT entries are stored in cct[0:lnt-1], cctlnt contains the
   number of entries
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
int32 col, rpt, ptr, mask, cctbuf[CCT_LNT];
t_stat r;
extern int32 cctlnt, cctptr, cct[CCT_LNT];
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
ptr = 0;
for ( ; (cptr = fgets (cbuf, CBUFSIZE, fileref)) != NULL; ) {	/* until eof */
	mask = 0;
	if (*cptr == '(') {				/* repeat count? */
		cptr = get_glyph (cptr + 1, gbuf, ')');	/* get 1st field */
		rpt = get_uint (gbuf, 10, CCT_LNT, &r);	/* repeat count */
		if (r != SCPE_OK) return SCPE_FMT;  }
	else rpt = 1;
	while (*cptr != 0) {				/* get col no's */
		cptr = get_glyph (cptr, gbuf, ',');	/* get next field */
		col = get_uint (gbuf, 10, 12, &r);	/* column number */
		if (r != SCPE_OK) return SCPE_FMT;
		mask = mask | (1 << col);  }		/* set bit */
	for ( ; rpt > 0; rpt--) {			/* store vals */
		if (ptr >= CCT_LNT) return SCPE_FMT;
		cctbuf[ptr++] = mask;  }  }
if (ptr == 0) return SCPE_FMT;
cctlnt = ptr;
cctptr = 0;
for (rpt = 0; rpt < cctlnt; rpt++) cct[rpt] = cctbuf[rpt];
return SCPE_OK;
}

/* Symbol table */

const char *opcode[64] = {
 NULL,  "R",   "W",  "WR",  "P",   "RP",  "WP",  "WRP",
 "RF",  "WF",  NULL, "MA",  "MUL", NULL,  NULL,  NULL,
 NULL,  "CS",  "S",  NULL,  "MTF", "BWZ", "BBE", NULL,
 "MZ",  "MSZ", NULL, "SWM", "DIV", NULL,  NULL,  NULL,
 NULL,  NULL,  "SS", "LCA", "MCW", "NOP", NULL,  "MCM",
 "SAR", NULL,  "ZS", NULL,  NULL,  NULL,  NULL,  NULL,
 NULL,  "A",   "B",  "C",   "MN",  "MCE", "CC",  NULL,
 "SBR", NULL,  "ZA", "H",   "CWM", NULL,  NULL,  NULL };

/* Print an address from three characters */

void fprint_addr (FILE *of, t_value *dig)
{
int32 addr, xa;
extern int32 hun_table[64], ten_table[64], one_table[64];

addr = hun_table[dig[0]] + ten_table[dig[1]] + one_table[dig[2]];
xa = (addr >> V_INDEX) & M_INDEX;
if (xa) fprintf (of, " %d,%d", addr & ADDRMASK, ((xa - (X1 >> V_INDEX)) / 5) + 1);
else if (addr >= MAXMEMSIZE) fprintf (of, " %d*", addr & ADDRMASK);
else fprintf (of, " %d", addr);
return;
}

/* Symbolic decode

   Inputs:
	*of	=	output stream
	addr	=	current address
	*val	=	values to decode
	*uptr	=	pointer to unit
	sw	=	switches
   Outputs:
	return	=	if >= 0, error code
			if < 0, number of extra words retired
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
int32 op, flags, ilnt, i, t;
extern int32 op_table[64], len_table[9];

if (sw & SWMASK ('C')) {				/* character? */
	t = val[0];
	if (uptr->flags & UNIT_BCD)
		fprintf (of, (t & WM)? "~%c": "%c", bcd_to_ascii[t & CHAR]);
	else fprintf (of, FMTASC (t & 0177));
	return SCPE_OK;  }
if ((uptr != NULL) && (uptr != &cpu_unit)) return SCPE_ARG;	/* CPU? */
if (sw & SWMASK ('D')) {                                /* dump? */
	for (i = 0; i < 50; i++) fprintf (of, "%c", bcd_to_ascii[val[i]&CHAR]) ;
	fprintf (of, "\n\t");
        for (i = 0; i < 50; i++) fprintf (of, (val[i]&WM)? "1": " ") ;
	return -(i - 1);  }
if (sw & SWMASK ('S')) {				/* string? */
	i = 0;
	do {	t = val[i++];
		fprintf (of, (t & WM)? "~%c": "%c", bcd_to_ascii[t & CHAR]);  }
	while ((i < LINE_LNT) && ((val[i] & WM) == 0));
	return -(i - 1);  }
if ((sw & SWMASK ('M')) == 0) return SCPE_ARG;

if ((val[0] & WM) == 0) return STOP_NOWM;		/* WM under op? */
op = val[0]& CHAR;					/* isolate op */
flags = op_table[op];					/* get flags */
for (ilnt = 1; ilnt < sim_emax; ilnt++) if (val[ilnt] & WM) break;
if ((flags & (NOWM | HNOP)) && (ilnt > 7)) ilnt = 7;	/* cs, swm, h, nop? */
else if ((op == OP_B) && (ilnt > 4) && (val[4] == BCD_BLANK)) ilnt = 4;
else if ((ilnt > 8) && (op != OP_NOP)) ilnt = 8;	/* cap length */
if (((flags & len_table[ilnt]) == 0) &&			/* valid lnt, */
	((op != OP_NOP) == 0)) return STOP_INVL;	/* nop? */
fprintf (of, "%s",opcode[op]);				/* print opcode */
if (ilnt > 2) {						/* A address? */
	if (((flags & IO) || (op == OP_NOP)) && (val[1] == BCD_PERCNT))
       	 fprintf (of, " %%%c%c", bcd_to_ascii[val[2]], bcd_to_ascii[val[3]]);
	else fprint_addr (of, &val[1]);  }
if (ilnt > 5) fprint_addr (of, &val[4]);		/* B address? */
if ((ilnt == 2) || (ilnt == 5) || (ilnt == 8))		/* d character? */
	fprintf (of, " '%c", bcd_to_ascii[val[ilnt - 1]]);
return -(ilnt - 1);					/* return # chars */
}

/* get_addr - get address + index pair */

t_stat get_addr (char *cptr, t_value *val)
{
int32 addr, index;
t_stat r;
char gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, ',');			/* get address */
addr = get_uint (gbuf, 10, MAXMEMSIZE, &r);
if (r != SCPE_OK) return SCPE_ARG;
if (*cptr != 0) {					/* more? */
	cptr = get_glyph (cptr, gbuf, ' ');
	index = get_uint (gbuf, 10, 3, &r);
	if ((r != SCPE_OK) || (index == 0)) return SCPE_ARG;  }
else index = 0;
if (*cptr != 0) return SCPE_ARG;
val[0] = store_addr_h (addr);
val[1] = store_addr_t (addr) | (index << V_ZONE);
val[2] = store_addr_u (addr);
return SCPE_OK;
}

/* get_io - get I/O address */

t_stat get_io (char *cptr, t_value *val)
{
if ((cptr[0] != '%') || (cptr[3] != 0) || !isalnum (cptr[1]) ||
	!isalnum (cptr[2])) return SCPE_ARG;
val[0] = BCD_PERCNT;
val[1] = ascii_to_bcd[cptr[1]];
val[2] = ascii_to_bcd[cptr[2]];
return SCPE_OK;
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
int32 i, op, ilnt, t, cflag, wm_seen;
extern int32 op_table[64], len_table[9];
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;				/* absorb spaces */
if ((sw & SWMASK ('C')) || (sw & SWMASK ('S')) || (*cptr == '~') ||
    ((*cptr == '\'') && cptr++) || ((*cptr == '"') && cptr++)) {
	wm_seen = 0;
	for (i = 0; (i < sim_emax) && (*cptr != 0); ) {
		t = *cptr++;				/* get character */
		if (cflag && (wm_seen == 0) && (t == '~')) wm_seen = WM;
		else if (uptr->flags & UNIT_BCD) {
			if (t < 040) return SCPE_ARG;
			val[i++] = ascii_to_bcd[t] | wm_seen;
			wm_seen = 0;  }
		else val[i++] = t;  }
	if ((i == 0) || wm_seen) return SCPE_ARG;
	return -(i-1);  }

if (cflag == 0) return SCPE_ARG;			/* CPU only */
cptr = get_glyph (cptr, gbuf, 0);			/* get opcode */
for (op = 0; op < 64; op++)				/* look it up */
	if (opcode[op] && strcmp (gbuf, opcode[op]) == 0) break;
if (op >= 64) return SCPE_ARG;				/* successful? */
val[0] = op | WM;					/* store opcode */
cptr = get_glyph (cptr, gbuf, 0);			/* get addr or d */
if (((op_table[op] && IO) && (get_io (gbuf, &val[1]) == SCPE_OK)) ||
     (get_addr (gbuf, &val[1]) == SCPE_OK)) {
	cptr = get_glyph (cptr, gbuf, 0);		/* get addr or d */
	if (get_addr (gbuf, &val[4]) == SCPE_OK) {
		cptr = get_glyph (cptr, gbuf, ',');	/* get d */
		ilnt = 7;  }				/* a and b addresses */
	else ilnt = 4;  }				/* a address */
else ilnt = 1;						/* no addresses */
if ((gbuf[0] == '\'') || (gbuf[0] == '"')) {		/* d character? */
	t = gbuf[1];
	if ((gbuf[2] != 0) || (*cptr != 0) || (t < 040))
		return SCPE_ARG;			/* end and legal? */
	val[ilnt] = ascii_to_bcd[t];			/* save D char */
	ilnt = ilnt + 1;  }
else if (gbuf[0] != 0) return SCPE_ARG;			/* not done? */
if ((op_table[op] & len_table[ilnt]) == 0) return STOP_INVL;
return -(ilnt - 1);
}
