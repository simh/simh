/* id4_sys.c: Interdata 4 simulator interface

   Copyright (c) 1993-2001, Robert M. Supnik

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

   14-Mar-01	RMS	Revised load/dump interface (again)
   30-Oct-00	RMS	Added support for examine to file
   27-Oct-98	RMS	V2.4 load interface
*/

#include "id4_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern DEVICE pt_dev, tt_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint16 *M;
extern int32 saved_PC;

/* SCP data structures and interface routines

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		number of words for examine
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

char sim_name[] = "Interdata 4";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 2;

DEVICE *sim_devices[] = { &cpu_dev,
	&pt_dev, &tt_dev,
	NULL };

const char *sim_stop_messages[] = {
	"Unknown error",
	"Reserved instruction",
	"HALT instruction",
	"Breakpoint",
	"Wait state" };

/* Binary loader.

   To be specified
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
return SCPE_FMT;					/* unexpected eof */
}

/* Symbol tables */

#define I_V_FL		16				/* class bits */
#define I_M_FL		07				/* class mask */
#define I_V_MR		0				/* mask-register */
#define I_V_RR		1				/* register-register */
#define I_V_R		2				/* register */
#define I_V_MX		3				/* mask-memory */
#define I_V_RX		4				/* register-memory */
#define I_V_X		5				/* memory */
#define I_V_FF		6				/* float reg-reg */
#define I_V_FX		7				/* float reg-mem */
#define I_MR		(I_V_MR << I_V_FL)
#define I_RR		(I_V_RR << I_V_FL)
#define I_R		(I_V_R << I_V_FL)
#define I_MX		(I_V_MX << I_V_FL)
#define I_RX		(I_V_RX << I_V_FL)
#define I_X		(I_V_X << I_V_FL)
#define I_FF		(I_V_FF << I_V_FL)
#define I_FX		(I_V_FX << I_V_FL)

static const int32 masks[] =
{ 0xFFF0, 0xFF00, 0xFFF0, 0xFF00,
  0xFF00, 0xFFF0, 0xFF00, 0xFF00 };

static const char *opcode[] = {
"BZ",  "BNZ", "BE",  "BNE",
"BP",  "BNP", "BL",  "BNL",
"BM",  "BNM", "BO",  "BC",
"B",   "BR",
       "BALR","BTCR","BFCR",
"NHR", "CLHR","OHR", "XHR",
"LHR",        "AHR", "SHR",
"MHR", "DHR", "ACHR","SCHR",
"LER", "CER", "AER", "SER",
"MER", "DER",
       "BAL", "BTC", "BFC",
"NH",  "CLH", "OH",  "XH",
"LH",         "AH",  "SH",
"MH",  "DH",  "ACH", "SCH",
"STE",
"LE",  "CE",  "AE",  "SE",
"ME",  "DE",
              "STBR","LBR",
              "WBR", "RBR",
              "WDR", "RDR",
       "SSR", "OCR", "AIR",
"BXH", "BXLE","LPSW",
"NHI", "CLHI","OHI", "XHI",
"LHI",        "AHI", "SHI",
"SRHL","SLHL","SRHA","SLHA",
"STM", "LM",  "STB", "LB",
       "AL",  "WB",  "RB",
              "WD",  "RD",
       "SS",  "OC",  "AI",
NULL };

static const int32 opc_val[] = {
0x4330+I_X,  0x4230+I_X,  0x4330+I_X,  0x4230+I_X,
0x4220+I_X,  0x4320+I_X,  0x4280+I_X,  0x4380+I_X,
0x4210+I_X,  0x4310+I_X,  0x4240+I_X,  0x4280+I_X,
0x4300+I_X,  0x0300+I_R,
             0x0100+I_RR, 0x0200+I_MR, 0x0300+I_MR,
0x0400+I_RR, 0x0500+I_RR, 0x0600+I_RR, 0x0700+I_RR,
0x0800+I_RR,              0x0A00+I_RR, 0x0B00+I_RR,
0x0C00+I_RR, 0x0D00+I_RR, 0x0E00+I_RR, 0x0F00+I_RR,
0x2800+I_FF, 0x2900+I_FF, 0x2A00+I_FF, 0x2B00+I_FF,
0x2C00+I_FF, 0x2D00+I_FF,
             0x4100+I_RX, 0x4200+I_MX, 0x4300+I_MX,
0x4400+I_RX, 0x4500+I_RX, 0x4600+I_RX, 0x4700+I_RX,
0x4800+I_RX,              0x4A00+I_RX, 0x4B00+I_RX,
0x4C00+I_RX, 0x4D00+I_RX, 0x4E00+I_RX, 0x4F00+I_RX,
0x6000+I_FX,
0x6800+I_FX, 0x6900+I_FX, 0x6A00+I_FX, 0x6B00+I_FX,
0x6C00+I_FX, 0x6D00+I_FX,
                          0x9200+I_RR, 0x9300+I_RR,
                          0x9600+I_RR, 0x9700+I_RR,
                          0x9A00+I_RR, 0x9B00+I_RR,
             0x9D00+I_RR, 0x9E00+I_RR, 0x9F00+I_RR,
0xC000+I_RX, 0xC100+I_RX, 0xC200+I_RX,
0xC400+I_RX, 0xC500+I_RX, 0xC600+I_RX, 0xC700+I_RX,
0xC800+I_RX,              0xCA00+I_RX, 0xCB00+I_RX,
0xCC00+I_RX, 0xCD00+I_RX, 0xCE00+I_RX, 0xCF00+I_RX,
0xD000+I_RX, 0xD100+I_RX, 0xD200+I_RX, 0xD300+I_RX,
             0xD500+I_RX, 0xD600+I_RX, 0xD700+I_RX,
                          0xDA00+I_RX, 0xDB00+I_RX,
             0xDD00+I_RX, 0xDE00+I_RX, 0xDF00+I_RX,
-1 };

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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
int32 i, j, c1, c2, inst, r1, r2;

c1 = (val[0] >> 8) & 0x3F;				/* big endian */
c2 = val[0] & 0177;
if (sw & SWMASK ('A')) {				/* ASCII? */
	fprintf (of, (c2 < 0x20)? "<%02X>": "%c", c2);
	return SCPE_OK;  }
if (sw & SWMASK ('C')) {				/* character? */
	fprintf (of, (c1 < 0x20)? "<%02X>": "%c", c1);
	fprintf (of, (c2 < 0x20)? "<%02X>": "%c", c2);
	return SCPE_OK;  }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

inst = val[0];
for (i = 0; opc_val[i] >= 0; i++) {			/* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;		/* get class */
    if ((opc_val[i] & 0xFFFF) == (inst & masks[j])) {	/* match? */
	r1 = (val[0] >> 4) & 0xF;
	r2 = val[0] & 0xF;
	switch (j) {					/* case on class */
	case I_V_MR:					/* mask-register */
		fprintf (of, "%s %-X,R%-X", opcode[i], r1, r2);
		return SCPE_OK;
	case I_V_RR:					/* register-register */
	case I_V_FF:					/* floating-floating */
		fprintf (of, "%s R%-X,R%-X", opcode[i], r1, r2);
		return SCPE_OK;
	case I_V_R:					/* register */
		fprintf (of, "%s R%-X", opcode[i], r2);
		return SCPE_OK;
	case I_V_MX:					/* mask-memory */
		fprintf (of, "%s %-X,%-X", opcode[i], r1, val[1]);
		break;
	case I_V_RX:					/* register-memory */
	case I_V_FX:					/* floating-memory */
		fprintf (of, "%s R%-X,%-X", opcode[i], r1, val[1]);
		break;
	case I_V_X:					/* memory */
		fprintf (of, "%s %-X", opcode[i], val[1]);
		break;  }				/* end case */
	if (r2) fprintf (of, "(R%-X)", r2);  
	return -1;  }					/* end if */
	}						/* end for */
return SCPE_ARG;					/* no match */
}

/* Register number

   Inputs:
	*cptr	=	pointer to input string
	mchar	=	match character
	regflt	=	false for integer, true for floating
   Outputs:
	rnum	=	output register number, -1 if error
*/

int32 get_reg (char *cptr, char mchar, t_bool regflt)
{
int32 reg = -1;

if ((*cptr == 'R') || (*cptr == 'r')) cptr++;
if (*(cptr + 1) != mchar) return reg;
if ((*cptr >= '0') && (*cptr <= '9')) reg = *cptr - '0';
else if ((*cptr >= 'a') && (*cptr <= 'f')) reg = (*cptr - 'a') + 10;
else if ((*cptr >= 'A') && (*cptr <= 'F')) reg = (*cptr - 'A') + 10;
if (regflt && (reg & 1)) return -1;
return reg;
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
int32 i, j, r1, r2;
t_bool regflt;
char *tptr, gbuf[CBUFSIZE];

regflt = FALSE;						/* assume int reg */
while (isspace (*cptr)) cptr++;				/* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = (t_value) cptr[0];
	return SCPE_OK;  }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = ((t_value) cptr[0] << 8) + (t_value) cptr[1];
	return SCPE_OK;  }

cptr = get_glyph (cptr, gbuf, 0);			/* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL) return SCPE_ARG;
val[0] = opc_val[i] & 0xFFFF;				/* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;			/* get class */
switch (j) {						/* case on class */

case I_V_FF:						/* float-float */
	regflt = TRUE;					/* fall thru */
case I_V_MR: case I_V_RR:				/* mask/reg-register */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register 1 */
	if ((r1 = get_reg (gbuf, 0, regflt)) < 0) return SCPE_ARG;
	val[0] = val[0] | (r1 << 4);			/* fall thru for reg 2 */

case I_V_R:						/* register */
	cptr = get_glyph (cptr, gbuf, 0);		/* get r2 */
	if ((r2 = get_reg (gbuf, 0, regflt)) < 0) return SCPE_ARG;
	val[0] = val[0] | r2;
	if (*cptr != 0) return SCPE_ARG;
	return SCPE_OK;

case I_V_FX:						/* float-memory */
	regflt = TRUE;					/* fall thru */
case I_V_MX: case I_V_RX:				/* mask/reg-memory */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register 1 */
	if ((r1 = get_reg (gbuf, 0, regflt)) < 0) return SCPE_ARG;
	val[0] = val[0] | (r1 << 4);			/* fall thru for memory */

case I_V_X:						/* memory */
	val[1] = strtoul (cptr, &tptr, 16);
	if (cptr == tptr) return SCPE_ARG;
	if (*tptr == 0) return -1;
	if ((*tptr != '(') || (*(tptr + 4) != 0)) return SCPE_ARG;
	if ((r2 = get_reg (tptr + 1, ')', FALSE)) < 0) return SCPE_ARG;
	val[0] = val[0] | r2;
	return -1;  }					/* end case */
}
