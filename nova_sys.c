/* nova_sys.c: NOVA simulator interface

   Copyright (c) 1993-2000, Robert M. Supnik

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

   30-Oct-00	RMS	Added support for examine to file
   15-Oct-00	RMS	Added stack, byte, trap instructions
   14-Apr-99	RMS	Changed t_addr to unsigned
   27-Oct-98	RMS	V2.4 load interface
   24-Sep-97	RMS	Fixed bug in device name table (found by Dutch Owen)
*/

#include "nova_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE ptr_dev, ptp_dev;
extern DEVICE tti_dev, tto_dev;
extern DEVICE clk_dev, lpt_dev;
extern DEVICE dkp_dev, dsk_dev;
extern DEVICE mta_dev;
extern REG cpu_reg[];
extern unsigned int16 M[];
extern int32 saved_PC;

/* SCP data structures

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		number of words needed for examine
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

char sim_name[] = "NOVA";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = { &cpu_dev,
	&ptr_dev, &ptp_dev, &tti_dev, &tto_dev,
	&clk_dev, &lpt_dev, &dsk_dev, &dkp_dev,
	&mta_dev, NULL };

const char *sim_stop_messages[] = {
	"Unknown error",
	"Unknown I/O instruction",
	"HALT instruction",
	"Breakpoint",
	"Nested indirect address limit exceeded",
	"Nested indirect interrupt address limit exceeded",
	"Nested indirect trap address limit exceeded"  };

/* Binary loader

   Loader format consists of blocks, optionally preceded, separated, and
   followed by zeroes.  Each block consists of:

	lo_count
	hi_count
	lo_origin
	hi_origin
	lo_checksum
	hi_checksum
	lo_data byte	---
	hi_data byte	 |
	:		 > -count words
	lo_data byte	 |
	hi_data byte	---

   If the word count is [0,-20], then the block is normal data.
   If the word count is [-21,-n], then the block is repeated data.
   If the word count is 1, the block is the start address.
   If the word count is >1, the block is an error block.
*/

t_stat sim_load (FILE *fileref, char *cptr, int flag)
{
int32 data, csum, count, state, i;
t_addr origin;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
state = 0;
while ((i = getc (fileref)) != EOF) {
	switch (state) {
	case 0:						/* leader */
		count = i;
		state = (count != 0);
		break;
	case 1:						/* high count */
		csum = count = (i << 8) | count;
		state = 2;
		break;
	case 2:						/* low origin */
		origin = i;
		state = 3;
		break;
	case 3:						/* high origin */
		origin = (i << 8) | origin;
		csum = csum + origin;
		state = 4;
		break;
	case 4:						/* low checksum */
		csum = csum + i;
		state = 5;
		break;
	case 5:						/* high checksum */
		csum = csum + (i << 8);
		if (count == 1) saved_PC = origin;	/* count = 1? */
		if (count <= 1) {			/* count = 0/1? */
			if (csum & 0177777) return SCPE_CSUM;
			state = 0;
			break;  }
		if (count < 0100000) {			/* count > 1 */
			state = 8;
			break;  }
		count = 0200000 - count;
		state = 6;
		break;
	case 6:						/* low data  */
		data = i;
		state = 7;
		break;
	case 7:						/* high data */
		data = (i << 8) | data;
		csum = csum + data;
		if (count > 20) {			/* large block */
			for (count = count - 1; count == 1; count--) {
				if (origin >= MEMSIZE) return SCPE_NXM;
				M[origin] = data;
				origin = origin + 1;  }  }
		if (origin >= MEMSIZE) return SCPE_NXM;
		M[origin] = data;
		origin = origin + 1;
		count = count - 1;
		if (count == 0) {
			if (csum & 0177777) return SCPE_CSUM;
			state = 0;
			break;  }
		state = 6;
		break;
	case 8:						/* error block */
		if (i == 0377) state = 0;
		break;  }				/* end switch */
	}						/* end while */

/* Ok to find end of tape between blocks or in error state */

return ((state == 0) || (state == 8))? SCPE_OK: SCPE_FMT;
}

/* Symbol tables */

#define I_V_FL		18				/* flag bits */
#define I_M_FL		07				/* flag width */
#define I_V_NPN		0				/* no operands */
#define I_V_R		1				/* reg */
#define I_V_D		2				/* device */
#define I_V_RD		3				/* reg,device */
#define I_V_M		4				/* mem addr */
#define I_V_RM		5				/* reg, mem addr */
#define I_V_RR		6				/* operate */
#define I_V_BY		7				/* byte pointer */
#define I_NPN		(I_V_NPN << I_V_FL)
#define I_R		(I_V_R << I_V_FL)
#define I_D		(I_V_D << I_V_FL)
#define I_RD		(I_V_RD << I_V_FL)
#define I_M		(I_V_M << I_V_FL)
#define I_RM		(I_V_RM << I_V_FL)
#define I_RR		(I_V_RR << I_V_FL)
#define I_BY		(I_V_BY << I_V_FL)

static const int32 masks[] = {
0177777, 0163777, 0177700, 0163700,
0174000, 0160000, 0103770, 0163477  };

static const char *opcode[] = {
 "JMP", "JSR", "ISZ", "DSZ",
 "LDA", "STA",
 "COM", "COMZ", "COMO", "COMC",
 "COML", "COMZL", "COMOL", "COMCL",
 "COMR", "COMZR", "COMOR", "COMCR",
 "COMS", "COMZS", "COMOS", "COMCS",
 "COM#", "COMZ#", "COMO#", "COMC#",
 "COML#", "COMZL#", "COMOL#", "COMCL#",
 "COMR#", "COMZR#", "COMOR#", "COMCR#",
 "COMS#", "COMZS#", "COMOS#", "COMCS#",
 "NEG", "NEGZ", "NEGO", "NEGC",
 "NEGL", "NEGZL", "NEGOL", "NEGCL",
 "NEGR", "NEGZR", "NEGOR", "NEGCR",
 "NEGS", "NEGZS", "NEGOS", "NEGCS",
 "NEG#", "NEGZ#", "NEGO#", "NEGC#",
 "NEGL#", "NEGZL#", "NEGOL#", "NEGCL#",
 "NEGR#", "NEGZR#", "NEGOR#", "NEGCR#",
 "NEGS#", "NEGZS#", "NEGOS#", "NEGCS#",
 "MOV", "MOVZ", "MOVO", "MOVC",
 "MOVL", "MOVZL", "MOVOL", "MOVCL",
 "MOVR", "MOVZR", "MOVOR", "MOVCR",
 "MOVS", "MOVZS", "MOVOS", "MOVCS",
 "MOV#", "MOVZ#", "MOVO#", "MOVC#",
 "MOVL#", "MOVZL#", "MOVOL#", "MOVCL#",
 "MOVR#", "MOVZR#", "MOVOR#", "MOVCR#",
 "MOVS#", "MOVZS#", "MOVOS#", "MOVCS#",
 "INC", "INCZ", "INCO", "INCC",
 "INCL", "INCZL", "INCOL", "INCCL",
 "INCR", "INCZR", "INCOR", "INCCR",
 "INCS", "INCZS", "INCOS", "INCCS",
 "INC#", "INCZ#", "INCO#", "INCC#",
 "INCL#", "INCZL#", "INCOL#", "INCCL#",
 "INCR#", "INCZR#", "INCOR#", "INCCR#",
 "INCS#", "INCZS#", "INCOS#", "INCCS#",
 "ADC", "ADCZ", "ADCO", "ADCC",
 "ADCL", "ADCZL", "ADCOL", "ADCCL",
 "ADCR", "ADCZR", "ADCOR", "ADCCR",
 "ADCS", "ADCZS", "ADCOS", "ADCCS",
 "ADC#", "ADCZ#", "ADCO#", "ADCC#",
 "ADCL#", "ADCZL#", "ADCOL#", "ADCCL#",
 "ADCR#", "ADCZR#", "ADCOR#", "ADCCR#",
 "ADCS#", "ADCZS#", "ADCOS#", "ADCCS#",
 "SUB", "SUBZ", "SUBO", "SUBC",
 "SUBL", "SUBZL", "SUBOL", "SUBCL",
 "SUBR", "SUBZR", "SUBOR", "SUBCR",
 "SUBS", "SUBZS", "SUBOS", "SUBCS",
 "SUB#", "SUBZ#", "SUBO#", "SUBC#",
 "SUBL#", "SUBZL#", "SUBOL#", "SUBCL#",
 "SUBR#", "SUBZR#", "SUBOR#", "SUBCR#",
 "SUBS#", "SUBZS#", "SUBOS#", "SUBCS#",
 "ADD", "ADDZ", "ADDO", "ADDC",
 "ADDL", "ADDZL", "ADDOL", "ADDCL",
 "ADDR", "ADDZR", "ADDOR", "ADDCR",
 "ADDS", "ADDZS", "ADDOS", "ADDCS",
 "ADD#", "ADDZ#", "ADDO#", "ADDC#",
 "ADDL#", "ADDZL#", "ADDOL#", "ADDCL#",
 "ADDR#", "ADDZR#", "ADDOR#", "ADDCR#",
 "ADDS#", "ADDZS#", "ADDOS#", "ADDCS#",
 "AND", "ANDZ", "ANDO", "ANDC",
 "ANDL", "ANDZL", "ANDOL", "ANDCL",
 "ANDR", "ANDZR", "ANDOR", "ANDCR",
 "ANDS", "ANDZS", "ANDOS", "ANDCS",
 "AND#", "ANDZ#", "ANDO#", "ANDC#",
 "ANDL#", "ANDZL#", "ANDOL#", "ANDCL#",
 "ANDR#", "ANDZR#", "ANDOR#", "ANDCR#",
 "ANDS#", "ANDZS#", "ANDOS#", "ANDCS#",
 "ION", "IOF",
 "RDSW", "INTA", "MSKO", "IORST", "HALT",
 "MUL", "DIV", "MULS", "DIVS",
 "PSHA", "POPA", "SAV", "RET",
 "MTSP", "MTFP", "MFSP", "MFFP",
 "LDB", "STB",
 "NIO", "NIOS", "NIOC", "NIOP",
 "DIA", "DIAS", "DIAC", "DIAP",
 "DOA", "DOAS", "DOAC", "DOAP",
 "DIB", "DIBS", "DIBC", "DIBP",
 "DOB", "DOBS", "DOBC", "DOBP",
 "DIC", "DICS", "DICC", "DICP",
 "DOC", "DOCS", "DOCC", "DOCP",
 "SKPBN", "SKPBZ", "SKPDN", "SKPDZ",
 NULL };

static const opc_val[] = {
 0000000+I_M, 0004000+I_M, 0010000+I_M, 0014000+I_M,
 0020000+I_RM, 0040000+I_RM,
 0100000+I_RR, 0100020+I_RR, 0100040+I_RR, 0100060+I_RR,
 0100100+I_RR, 0100120+I_RR, 0100140+I_RR, 0100160+I_RR,
 0100200+I_RR, 0100220+I_RR, 0100240+I_RR, 0100260+I_RR,
 0100300+I_RR, 0100320+I_RR, 0100340+I_RR, 0100360+I_RR,
 0100010+I_RR, 0100030+I_RR, 0100050+I_RR, 0100070+I_RR,
 0100110+I_RR, 0100130+I_RR, 0100150+I_RR, 0100170+I_RR,
 0100210+I_RR, 0100230+I_RR, 0100250+I_RR, 0100270+I_RR,
 0100310+I_RR, 0100330+I_RR, 0100350+I_RR, 0100370+I_RR,
 0100400+I_RR, 0100420+I_RR, 0100440+I_RR, 0100460+I_RR,
 0100500+I_RR, 0100520+I_RR, 0100540+I_RR, 0100560+I_RR,
 0100600+I_RR, 0100620+I_RR, 0100640+I_RR, 0100660+I_RR,
 0100700+I_RR, 0100720+I_RR, 0100740+I_RR, 0100760+I_RR,
 0100410+I_RR, 0100430+I_RR, 0100450+I_RR, 0100470+I_RR,
 0100510+I_RR, 0100530+I_RR, 0100550+I_RR, 0100570+I_RR,
 0100610+I_RR, 0100630+I_RR, 0100650+I_RR, 0100670+I_RR,
 0100710+I_RR, 0100730+I_RR, 0100750+I_RR, 0100770+I_RR,
 0101000+I_RR, 0101020+I_RR, 0101040+I_RR, 0101060+I_RR,
 0101100+I_RR, 0101120+I_RR, 0101140+I_RR, 0101160+I_RR,
 0101200+I_RR, 0101220+I_RR, 0101240+I_RR, 0101260+I_RR,
 0101300+I_RR, 0101320+I_RR, 0101340+I_RR, 0101360+I_RR,
 0101010+I_RR, 0101030+I_RR, 0101050+I_RR, 0101070+I_RR,
 0101110+I_RR, 0101130+I_RR, 0101150+I_RR, 0101170+I_RR,
 0101210+I_RR, 0101230+I_RR, 0101250+I_RR, 0101270+I_RR,
 0101310+I_RR, 0101330+I_RR, 0101350+I_RR, 0101370+I_RR,
 0101400+I_RR, 0101420+I_RR, 0101440+I_RR, 0101460+I_RR,
 0101500+I_RR, 0101520+I_RR, 0101540+I_RR, 0101560+I_RR,
 0101600+I_RR, 0101620+I_RR, 0101640+I_RR, 0101660+I_RR,
 0101700+I_RR, 0101720+I_RR, 0101740+I_RR, 0101760+I_RR,
 0101410+I_RR, 0101430+I_RR, 0101450+I_RR, 0101470+I_RR,
 0101510+I_RR, 0101530+I_RR, 0101550+I_RR, 0101570+I_RR,
 0101610+I_RR, 0101630+I_RR, 0101650+I_RR, 0101670+I_RR,
 0101710+I_RR, 0101730+I_RR, 0101750+I_RR, 0101770+I_RR,
 0102000+I_RR, 0102020+I_RR, 0102040+I_RR, 0102060+I_RR,
 0102100+I_RR, 0102120+I_RR, 0102140+I_RR, 0102160+I_RR,
 0102200+I_RR, 0102220+I_RR, 0102240+I_RR, 0102260+I_RR,
 0102300+I_RR, 0102320+I_RR, 0102340+I_RR, 0102360+I_RR,
 0102010+I_RR, 0102030+I_RR, 0102050+I_RR, 0102070+I_RR,
 0102110+I_RR, 0102130+I_RR, 0102150+I_RR, 0102170+I_RR,
 0102210+I_RR, 0102230+I_RR, 0102250+I_RR, 0102270+I_RR,
 0102310+I_RR, 0102330+I_RR, 0102350+I_RR, 0102370+I_RR,
 0102400+I_RR, 0102420+I_RR, 0102440+I_RR, 0102460+I_RR,
 0102500+I_RR, 0102520+I_RR, 0102540+I_RR, 0102560+I_RR,
 0102600+I_RR, 0102620+I_RR, 0102640+I_RR, 0102660+I_RR,
 0102700+I_RR, 0102720+I_RR, 0102740+I_RR, 0102760+I_RR,
 0102410+I_RR, 0102430+I_RR, 0102450+I_RR, 0102470+I_RR,
 0102510+I_RR, 0102530+I_RR, 0102550+I_RR, 0102570+I_RR,
 0102610+I_RR, 0102630+I_RR, 0102650+I_RR, 0102670+I_RR,
 0102710+I_RR, 0102730+I_RR, 0102750+I_RR, 0102770+I_RR,
 0103000+I_RR, 0103020+I_RR, 0103040+I_RR, 0103060+I_RR,
 0103100+I_RR, 0103120+I_RR, 0103140+I_RR, 0103160+I_RR,
 0103200+I_RR, 0103220+I_RR, 0103240+I_RR, 0103260+I_RR,
 0103300+I_RR, 0103320+I_RR, 0103340+I_RR, 0103360+I_RR,
 0103010+I_RR, 0103030+I_RR, 0103050+I_RR, 0103070+I_RR,
 0103110+I_RR, 0103130+I_RR, 0103150+I_RR, 0103170+I_RR,
 0103210+I_RR, 0103230+I_RR, 0103250+I_RR, 0103270+I_RR,
 0103310+I_RR, 0103330+I_RR, 0103350+I_RR, 0103370+I_RR,
 0103400+I_RR, 0103420+I_RR, 0103440+I_RR, 0103460+I_RR,
 0103500+I_RR, 0103520+I_RR, 0103540+I_RR, 0103560+I_RR,
 0103600+I_RR, 0103620+I_RR, 0103640+I_RR, 0103660+I_RR,
 0103700+I_RR, 0103720+I_RR, 0103740+I_RR, 0103760+I_RR,
 0103410+I_RR, 0103430+I_RR, 0103450+I_RR, 0103470+I_RR,
 0103510+I_RR, 0103530+I_RR, 0103550+I_RR, 0103570+I_RR,
 0103610+I_RR, 0103630+I_RR, 0103650+I_RR, 0103670+I_RR,
 0103710+I_RR, 0103730+I_RR, 0103750+I_RR, 0103770+I_RR,
 0060177+I_NPN, 0060277+I_NPN,
 0060477+I_R, 0061477+I_R, 0062077+I_R, 0062677+I_NPN, 0063077+I_NPN,
 0073301+I_NPN, 0073101+I_NPN, 0077201+I_NPN, 0077001+I_NPN,
 0061401+I_R, 0061601+I_R, 0062401+I_NPN, 0062601+I_NPN,
 0061001+I_R, 0060001+I_R, 0061201+I_R, 0060201+I_R,
 0060401+I_BY, 0062001+I_BY,
 0060000+I_D, 0060100+I_D, 0060200+I_D, 0060300+I_D,
 0060400+I_RD, 0060500+I_RD, 0060600+I_RD, 0060700+I_RD,
 0061000+I_RD, 0061100+I_RD, 0061200+I_RD, 0061300+I_RD,
 0061400+I_RD, 0061500+I_RD, 0061600+I_RD, 0061700+I_RD,
 0062000+I_RD, 0062100+I_RD, 0062200+I_RD, 0062300+I_RD,
 0062400+I_RD, 0062500+I_RD, 0062600+I_RD, 0062700+I_RD,
 0063000+I_RD, 0063100+I_RD, 0063200+I_RD, 0063300+I_RD,
 0063400+I_D, 0063500+I_D, 0063600+I_D, 0063700+I_D,
 -1 };
 
static const char *skip[] = {
 "SKP", "SZC", "SNC", "SZR", "SNR", "SEZ", "SBN",
 NULL };

static const char *device[] = {
 "TTI", "TTO", "PTR", "PTP", "RTC", "PLT", "CDR", "LPT",
 "DSK", "MTA", "DCM", "ADCV", "DKP", "CAS", "CPU",
 NULL };

static const int32 dev_val[] = {
 010, 011, 012, 013, 014, 015, 016, 017,
 020, 022, 024, 030, 033, 034, 077,
 -1 };

/* Address decode

   Inputs:
	*of	=	output stream
	addr	=	current PC
	inst	=	instruction to decode
	cflag	=	true if decoding for CPU
   Outputs:
	return	=	error code
*/

t_stat fprint_addr (FILE *of, t_addr addr, int32 inst, int32 cflag)
{
int32 disp;

if (inst & I_IND) fprintf (of, "@");			/* indirect? */
disp = I_GETDISP (inst);				/* displacement */
switch (I_GETMODE (inst)) {				/* mode */
case 0:							/* page zero */
	fprintf (of, "%-o", disp);
	break;
case 1:							/* PC rel */
	if (disp & DISPSIGN) {
		if (cflag) fprintf (of, "%-o", (addr + 0177400 + disp) & AMASK);
		else fprintf (of, ".-%-o", 0400 - disp);  }
	else {	if (cflag) fprintf (of, "%-o", (addr + disp) & AMASK);
		else fprintf (of, ".+%-o", disp);  }
	break;
case 2:							/* AC2 rel */
	if (disp & DISPSIGN) fprintf (of, "-%-o,2", 0400 - disp);
	else fprintf (of, "%-o,2", disp);
	break;
case 3:							/* AC3 rel */
	if (disp & DISPSIGN) fprintf (of, "-%-o,3", 0400 - disp);
	else fprintf (of, "%-o,3", disp);
	break;  }					/* end switch */
return SCPE_OK;
}

/* Symbolic output

   Inputs:
	*of	=	output stream
	addr	=	current PC
	*val	=	pointer to values
	*uptr	=	pointer to unit
	sw	=	switches
   Outputs:
	status	=	error code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
int32 cflag, i, j, c1, c2, inst, dv, src, dst, skp, dev, byac;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
c1 = (val[0] >> 8) & 0177;
c2 = val[0] & 0177;
if (sw & SWMASK ('A')) {				/* ASCII? */
	fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
	return SCPE_OK;  }
if (sw & SWMASK ('C')) {				/* character? */
	fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
	fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
	return SCPE_OK;  }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;		/* mnemonic? */

/* Instruction decode */

inst = val[0];
for (i = 0; opc_val[i] >= 0; i++) {			/* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;		/* get class */
    if ((opc_val[i] & 0177777) == (inst & masks[j])) {	/* match? */
	src = I_GETSRC (inst);				/* opr fields */
	dst = I_GETDST (inst);
	skp = I_GETSKP (inst);
	dev = I_GETDEV (inst);				/* IOT fields */
	for (dv = 0; (dev_val[dv] >= 0) && (dev_val[dv] != dev); dv++) ;

	switch (j) {					/* switch on class */
	case I_V_NPN:					/* no operands */
		fprintf (of, "%s", opcode[i]);		/* opcode */
		break;
	case I_V_R:					/* reg only */
		fprintf (of, "%s %-o", opcode[i], dst);
		break;
	case I_V_D:					/* dev only */
		if (dev_val[dv] >= 0)
			fprintf (of, "%s %s", opcode[i], device[dv]);
		else fprintf (of, "%s %-o", opcode[i], dev);
		break;
	case I_V_RD:					/* reg, dev */
		if (dev_val[dv] >= 0)
			fprintf (of, "%s %-o,%s", opcode[i], dst, device[dv]);
		else fprintf (of, "%s %-o,%-o", opcode[i], dst, dev);
		break;
	case I_V_M:					/* addr only */
		fprintf (of, "%s ", opcode[i]);
		fprint_addr (of, addr, inst, cflag);
		break;
	case I_V_RM:					/* reg, addr */
		fprintf (of, "%s %-o,", opcode[i], dst);
		fprint_addr (of, addr, inst, cflag);
		break;
	case I_V_RR:					/* operate */
		fprintf (of, "%s %-o,%-o", opcode[i], src, dst);
		if (skp) fprintf (of, ",%s", skip[skp-1]);
		break;
	case I_V_BY:					/* byte */
		byac = I_GETPULSE (inst);		/* src = pulse */
		fprintf (of, "%s %-o,%-o", opcode[i], byac, dst);
		break;  }				/* end case */
	return SCPE_OK;  }				/* end if */
	}						/* end for */
return SCPE_ARG;
}

/* Address parse

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	cflag	=	true if parsing for CPU
	*val	=	pointer to output value
   Outputs:
	optr	=	pointer to next char in input string
			NULL if error
*/

#define A_FL	001					/* CPU flag */
#define A_NX	002					/* index seen */
#define A_PER	004					/* period seen */
#define A_NUM	010					/* number seen */
#define A_SI	020					/* sign seen */
#define A_MI	040					/* - seen */

char *get_addr (char *cptr, t_addr addr, int32 cflag, int32 *val)
{
int32 d, r, x, pflag;
t_addr sd;
char gbuf[CBUFSIZE];

*val = 0;						/* clear result */
d = 0;							/* default no num */
x = 1;							/* default PC rel */

pflag = cflag & A_FL;					/* isolate flag */
if (*cptr == '@') {					/* indirect? */
	*val = I_IND;
	cptr++;  }		
if (*cptr == '.') {					/* relative? */
	pflag = pflag | A_PER;
	cptr++;  }
if (*cptr == '+') {					/* + sign? */
	pflag = pflag | A_SI;
	cptr++;  }
else if (*cptr == '-') {				/* - sign? */
	pflag = pflag | A_MI | A_SI;
	cptr++;  }	
if (*cptr != 0) {					/* number? */
	cptr = get_glyph (cptr, gbuf, ',');		/* get glyph */
	d = get_uint (gbuf, 8, AMASK, &r);
	if (r != SCPE_OK) return NULL;
	pflag = pflag | A_NUM;
	sd = (pflag & A_MI)? -d: d;  }
if (*cptr != 0) {					/* index? */
	cptr = get_glyph (cptr, gbuf, 0);		/* get glyph */
	x = get_uint (gbuf, 8, I_M_DST, &r);
	if ((r != SCPE_OK) || (x < 2)) return NULL;
	pflag = pflag | A_NX;  }

/* Address parse, continued */

switch (pflag & ~A_MI) {				/* case on flags */
case A_NUM: case A_NUM+A_SI:				/* ~CPU, (+/-) num */
	if (sd <= I_M_DISP) *val = *val + sd;
	else return NULL;
	break;
case A_NUM+A_FL: case A_NUM+A_SI+A_FL:			/* CPU, (+/-) num */
	if (sd <= I_M_DISP) *val = *val + sd;
	else if (((sd >= ((addr - 0200) & AMASK)) &&
		  (sd <= ((addr + 0177) & AMASK))) ||
		 (sd >= (addr + 077600)))
		*val = *val + 0400 + ((sd - addr) & I_M_DISP);
	else return NULL;
	break;
case A_PER: case A_PER+A_FL:				/* .+/- num */
case A_PER+A_SI+A_NUM: case A_PER+A_SI+A_NUM+A_FL:
case A_NX+A_NUM: case A_NX+A_NUM+A_FL:			/* (+/-) num, ndx */
case A_NX+A_SI+A_NUM: case A_NX+A_SI+A_NUM+A_FL:
	if (((pflag & A_MI) == 0) && (d <= 0177)) *val = *val + (x << 8) + d;
	else if ((pflag & A_MI) && (d <= 0200))
		*val = *val + (x << 8) + 0400 - d;
	else return NULL;
	break;
default:
	return NULL;  }					/* end case */
return cptr;
}

/* Symbolic input

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	*uptr	=	pointer to unit
	*val	=	pointer to output values
	sw	=	switches
   Outputs:
	status	=	error status
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, d, i, j;
t_stat r;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;				/* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = (t_value) cptr[0];
	return SCPE_OK;  }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = ((t_value) cptr[0] << 8) + (t_value) cptr[1];
	return SCPE_OK;  }

/* Instruction parse */

cptr = get_glyph (cptr, gbuf, 0);			/* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL) return SCPE_ARG;
val[0] = opc_val[i] & 0177777;				/* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;			/* get class */

switch (j) {						/* case on class */
case I_V_NPN:						/* no operand */
	break;
case I_V_R:						/* IOT reg */
	cptr = get_glyph (cptr, gbuf, 0);		/* get register */
	d = get_uint (gbuf, 8, I_M_DST, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_DST);		/* put in place */
	break;
case I_V_RD:						/* IOT reg,dev */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_DST, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_DST);		/* put in place */
case I_V_D:						/* IOT dev */
	cptr = get_glyph (cptr, gbuf, 0);		/* get device */
	for (i = 0; (device[i] != NULL) && (strcmp (device[i], gbuf) != 0);
		i++);
	if (device[i] != NULL) val[0] = val[0] | dev_val[i];
	else {	d = get_uint (gbuf, 8, I_M_DEV, &r);
		if (r != SCPE_OK) return SCPE_ARG;
		val[0] = val[0] | (d << I_V_DEV);  }
	break;
case I_V_RM:						/* mem reg,addr */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_DST, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_DST);		/* put in place */
case I_V_M:
	if ((cptr = get_addr (cptr, addr, cflag, &d)) == NULL) return SCPE_ARG;
	val[0] = val[0] | d;
	break;
case I_V_RR:						/* operate */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_SRC, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_SRC);		/* put in place */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_DST, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_DST);		/* put in place */
	if (*cptr != 0) {				/* skip? */
		cptr = get_glyph (cptr, gbuf, 0);	/* get skip */
		for (i = 0; (skip[i] != NULL) &&
			(strcmp (skip[i], gbuf) != 0); i++) ;
		if (skip[i] == NULL) return SCPE_ARG;
		val[0] = val[0] | (i + 1);  }		/* end for */
	break;
case I_V_BY:						/* byte */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_PULSE, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_PULSE);		/* put in place */
	cptr = get_glyph (cptr, gbuf, ',');		/* get register */
	d = get_uint (gbuf, 8, I_M_DST, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | (d << I_V_DST);		/* put in place */
	break;  }					/* end case */
if (*cptr != 0) return SCPE_ARG;			/* any leftovers? */
return SCPE_OK;
}
