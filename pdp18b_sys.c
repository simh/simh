/* pdp18b_sys.c: 18b PDP's simulator interface

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

   27-May-01	RMS	Added second Teletype support
   18-May-01	RMS	Added PDP-9,-15 API IOT's
   12-May-01	RMS	Fixed bug in RIM loaders
   14-Mar-01	RMS	Added extension detection of RIM format tapes
   21-Jan-01	RMS	Added DECtape support
   30-Nov-00	RMS	Added PDP-9,-15 RIM/BIN loader format
   30-Oct-00	RMS	Added support for examine to file
   27-Oct-98	RMS	V2.4 load interface
   20-Oct-97	RMS	Fixed endian dependence in RIM loader
			(found by Michael Somos)
*/

#include "pdp18b_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern DEVICE ptr_dev, ptp_dev;
extern DEVICE tti_dev, tto_dev;
extern UNIT tti_unit, tto_unit;
extern DEVICE clk_dev;
extern DEVICE lpt_dev;
#if defined (DRM)
extern DEVICE drm_dev;
#endif
#if defined (RF)
extern DEVICE rf_dev;
#endif
#if defined (RP)
extern DEVICE rp_dev;
#endif
#if defined (MTA)
extern DEVICE mt_dev;
#endif
#if defined (DTA)
extern DEVICE dt_dev;
#endif
#if defined (TTY1)
extern DEVICE tti1_dev, tto1_dev;
extern UNIT tti1_unit, tto1_unit;
#endif
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern int32 M[];
extern int32 memm;
extern int32 saved_PC;

/* SCP data structures and interface routines

   sim_name		simulator name string
   sim_PC		pointer to saved PC register descriptor
   sim_emax		number of words for examine
   sim_devices		array of pointers to simulated devices
   sim_stop_messages	array of pointers to stop messages
   sim_load		binary loader
*/

#if defined (PDP4)
char sim_name[] = "PDP-4";
#elif defined (PDP7)
char sim_name[] = "PDP-7";
#elif defined (PDP9)
char sim_name[] = "PDP-9";
#elif defined (PDP15)
char sim_name[] = "PDP-15";
#endif

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 3;

DEVICE *sim_devices[] = { &cpu_dev,
	&ptr_dev, &ptp_dev,
	&tti_dev, &tto_dev,
	&clk_dev, &lpt_dev,
#if defined (DRM)
	&drm_dev,
#endif
#if defined (RF)
	&rf_dev,
#endif
#if defined (RP)
	&rp_dev,
#endif
#if defined (DTA)
	&dt_dev,
#endif
#if defined (MTA)
	&mt_dev,
#endif
#if defined (TTY1)
	&tti1_dev, &tto1_dev,
#endif
	NULL };

#if defined (TTY1)
UNIT *sim_consoles[] = {
	&tti_unit, &tto_unit,
	&tti1_unit, &tto1_unit,
	NULL };
#else
UNIT *sim_consoles = NULL;
#endif

const char *sim_stop_messages[] = {
	"Unknown error",
	"Undefined instruction",
	"HALT instruction",
	"Breakpoint",
	"Nested XCT's",
	"Invalid API interrupt"  };

/* Binary loader */

int32 getword (FILE *fileref, int32 *hi)
{
int32 word, bits, st, ch;

word = st = bits = 0;
do {	if ((ch = getc (fileref)) == EOF) return -1;
	if (ch & 0200) {
		word = (word << 6) | (ch & 077);
		bits = (bits << 1) | ((ch >> 6) & 1);
		st++;  }  }
while (st < 3);
if (hi != NULL) *hi = bits;
return word;
}

#if defined (PDP4) || defined (PDP7)

/* PDP-4/PDP-7: RIM format only

   Tape format
	dac addr
	data
	:
	dac addr
	data
	jmp addr or hlt
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
int32 origin, val;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
for (;;) {
	if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
	if ((val & 0760000) == 0040000) {		/* DAC? */
		origin = val & 017777;
		if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
		if (MEM_ADDR_OK (origin)) M[origin++] = val;  }
	else if ((val & 0760000) == OP_JMP) {		/* JMP? */
		saved_PC = ((origin - 1) & 060000) | (val & 017777);
		return SCPE_OK;  }
	else if (val == OP_HLT) return SCPE_OK;		/* HLT? */
	else return SCPE_FMT;  }			/* error */
return SCPE_FMT;					/* error */
}

#else

/* PDP-9/PDP-15: RIM format and BIN format

   RIM format (read in address specified externally)
	data
	:
	data
	word to execute (bit 1 of last character set)

   BIN format (starts after RIM bootstrap)
    block/	origin (>= 0)
		count
		checksum
		data
		:
		data
    block/
    :
    endblock/	origin (< 0)
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
extern int32 sim_switches;
int32 i, bits, origin, count, cksum, val;
t_stat r;
char gbuf[CBUFSIZE];
extern t_bool match_ext (char *fnm, char *ext);

/* RIM loader */

if ((sim_switches & SWMASK ('R')) ||			/* RIM format? */
    (match_ext (fnam, "RIM") && !(sim_switches & SWMASK ('B')))) {
	if (*cptr != 0) {				/* more input? */
		cptr = get_glyph (cptr, gbuf, 0);	/* get origin */
		origin = get_uint (gbuf, 8, ADDRMASK, &r);
		if (r != SCPE_OK) return r;
		if (*cptr != 0) return SCPE_ARG;  }	/* no more */
	else origin = 0200;				/* default 200 */

	for (;;) {					/* word loop */
		if ((val = getword (fileref, &bits)) < 0) return SCPE_FMT;
		if (bits & 1) {				/* end of tape? */
			if ((val & 0760000) == OP_JMP) saved_PC = 
				((origin - 1) & 060000) | (val & 017777);
			else if (val != OP_HLT) return SCPE_FMT;
			break;  }
		else if (MEM_ADDR_OK (origin)) M[origin++] = val;  }
	return SCPE_OK;  }

/* Binary loader */

if (*cptr != 0) return SCPE_ARG;			/* no arguments */
do {	val = getc (fileref);  }			/* find end RIM */
while (((val & 0100) == 0) && (val != EOF));
if (val == EOF) rewind (fileref);			/* no RIM? rewind */ 
for (;;) {						/* block loop */
	if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
	if (val & SIGN) {
		if (val != DMASK) saved_PC = val & 077777;
		return SCPE_OK;  }
	cksum = origin = val;				/* save origin */
	if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
	cksum = cksum + val;				/* add to cksum */
	count = (-val) & DMASK;				/* save count */
	if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
	cksum = cksum + val;				/* add to cksum */
	for (i = 0; i < count; i++) {
		if ((val = getword (fileref, NULL)) < 0) return SCPE_FMT;
		cksum = cksum + val;
		if (MEM_ADDR_OK (origin)) M[origin++] = val;  }
	if ((cksum & DMASK) != 0) return SCPE_CSUM;  }
return SCPE_FMT;
}
#endif

/* Symbol tables */

#define I_V_FL		18				/* inst class */
#define	I_M_FL		017				/* class mask */
#define I_V_DC		22				/* default count */
#define I_V_NPN		0				/* no operand */
#define I_V_NPI		1				/* no operand IOT */
#define I_V_IOT		2				/* IOT */
#define I_V_MRF		3				/* memory reference */
#define I_V_OPR		4				/* OPR */
#define I_V_LAW		5				/* LAW */
#define I_V_XR		6				/* index */
#define I_V_XR9		7				/* index literal */
#define I_V_EST		8				/* EAE setup */
#define I_V_ESH		9				/* EAE shift */
#define I_V_EMD		10				/* EAE mul-div */
#define I_NPN		(I_V_NPN << I_V_FL)		/* no operand */
#define I_NPI		(I_V_NPI << I_V_FL)		/* no operand IOT */
#define I_IOT		(I_V_IOT << I_V_FL)		/* IOT */
#define I_MRF		(I_V_MRF << I_V_FL)		/* memory reference */
#define I_OPR		(I_V_OPR << I_V_FL)		/* OPR */
#define I_LAW		(I_V_LAW << I_V_FL)		/* LAW */
#define I_XR		(I_V_XR << I_V_FL)		/* index */
#define I_XR9		(I_V_XR9 << I_V_FL)		/* index literal */
#define I_EST		(I_V_EST << I_V_FL)		/* EAE setup */
#define I_ESH		(I_V_ESH << I_V_FL)		/* EAE shift */
#define I_EMD		(I_V_EMD << I_V_FL)		/* EAE mul-div */
#define MD(x) ((I_EMD) + ((x) << I_V_DC))

static const int32 masks[] = {
 0777777, 0777767, 0740000, 0760000,
 0763730, 0760000, 0777000, 0777000,
 0740700, 0760700, 0777700 };

static const char *opcode[] = {
 "CAL", "DAC", "JMS", "DZM",				/* mem refs */
 "LAC", "XOR", "ADD", "TAD",
 "XCT", "ISZ", "AND", "SAD",
 "JMP",

#if defined (PDP9) || defined (PDP15)			/* mem ref ind */
 "CAL*", "DAC*", "JMS*", "DZM*",			/* normal */
 "LAC*", "XOR*", "ADD*", "TAD*",
 "XCT*", "ISZ*", "AND*", "SAD*",
 "JMP*",
#else
 "CAL I", "DAC I", "JMS I", "DZM I",			/* decode only */
 "LAC I", "XOR I", "ADD I", "TAD I",
 "XCT I", "ISZ I", "AND I", "SAD I",
 "JMP I",
#endif

 "LAW",							/* LAW */

 "LACQ", "LACS", "ABS", "GSM", "LMQ",			/* EAE */
 "MUL", "MULS", "DIV", "DIVS",
 "IDIV", "IDIVS", "FRDIV", "FRDIVS",
 "NORM", "NORMS",
 "MUY", "LLK MUY", "DVI", "LLK DVI",
 "NMI", "NMIS", "LRS", "LRSS",
 "LLS", "LLSS", "ALS", "ALSS",
 "EAE-setup", "EAE",					/* setup, general */

 "CLSF", "IOF", "ION", "CLOF", "CLON",			/* standard IO devs */
 "RSF", "RRB", "RCF", "RSA", "RSB",
 "PSF", "PCF", "PSA", "PSB", "PLS",
 "KSF", "KRB", "KCF", "IORS", "IOOS",
 "TSF", "TCF", "TPC", "TLS",
#if defined (TYPE62)					/* PDP-4 LPT */
 "LPSF", "LPCF", "LPLD", "LPSE",
 "LSSF", "LSCF", "LSPR",
#elif defined (TYPE647)					/* PDP-7, PDP-9 LPT */
 "LPSF", "LPCB", "LPCD", "LPCD", "LPCD",
 "LPL2", "LPLD", "LPL1",
 "LPEF", "LPCF", "LPCF", "LPCF", "LPCF",
 "LPPB", "LPLS", "LPPS",
#elif defined (LP15)
 "LPSF", "LPPM", "LPP1", "LPDI",
 "LPRS", "LPOS", "LPEI", "LPCD", "LPCF",
#endif
#if defined (DRM)					/* drum */
 "DRLR", "DRLW", "DRSS", "DRCS",
 "DRSF", "DRSN", "DRCF",
 "DRLCRD", "DRLCWR", "DRLBLK", "DRCONT",
 "DRSF", "DRSOK", "DRCF",
#endif
#if defined (RF)					/* RF09 */
 "DSSF", "DSCC", "DSCF",
 "DRBR", "DRAL", "DSFX", "DRAH",
 "DLBR", "DLAL", "DSCN", "DLAH",
 "DLOK",         "DSCD", "DSRS",
 "DGHS", "DGSS",
#endif
#if defined (RP)
 "DPSF", "DPSA", "DPSJ", "DPSE",
 "DPRSA", "DPOSA", "DPRSB", "DPOSB",
 "DPRM", "DPOM",
 "DPLA", "DPCS", "DPCA", "DPWC",
 "DPLM", "DPEM", "DPSN",
 "DPRU", "DPOU", "DPRA", "DPOA",
 "DPRC", "DPOC", "DPRW", "DPOW",
 "DPCF", "DPLZ", "DPCN", "DPLO", "DPLF",
#endif
#if defined (MTA)					/* TC59 */
 "MTTR", "MTCR", "MTSF", "MTRC", "MTAF",
 "MTRS", "MTGO", "MTCM", "MTLC",
#endif
#if defined (DTA)					/* TC02/TC15 */
 "DTCA", "DTRA", "DTXA", "DTLA",
 "DTEF", "DTRB", "DTDF",
#endif
#if defined (TTY1)
 "KSF1", "KRB1",
 "TSF1", "TCF1", "TLS1", "TCF1!TLS1",
#endif
#if defined (PDP7)
 "ITON", "TTS", "SKP7", "CAF",
 "SEM", "EEM", "EMIR", "LEM",
#endif
#if defined (PDP9)
 "SKP7", "SEM", "EEM", "LEM",
 "LPDI", "LPEI",
#endif
#if defined (PDP15)
 "SPCO", "SKP15", "RES",
 "SBA", "DBA", "EBA",
 "AAS", "PAX", "PAL", "AAC",
 "PXA", "AXS", "PXL", "PLA",
 "PLX", "CLAC","CLX", "CLLR", "AXR",
#endif
#if defined (PDP9) || defined (PDP15)
 "MPSK", "MPSNE", "MPCV", "MPEU",
 "MPLD", "MPCNE", "PFSF",
 "TTS", "CAF", "DBK", "DBR",
 "SPI", "RPL", "ISA",
#endif
 "IOT",							/* general */

 "NOP", "STL", "RCL", "RCR",
 "CLC", "LAS", "GLK",
 "OPR", "SMA", "SZA", "SZA SMA",
 "SNL", "SNL SMA", "SNL SZA", "SNL SZA SMA",
 "SKP", "SPA", "SNA", "SNA SPA",
 "SZL", "SZL SPA", "SZL SNA", "SZL SZA SPA",
 "RAL", "SMA RAL", "SZA RAL", "SZA SMA RAL",
 "SNL RAL", "SNL SMA RAL", "SNL SZA RAL", "SNL SZA SMA RAL",
 "SKP RAL", "SPA RAL", "SNA RAL", "SNA SPA RAL",
 "SZL RAL", "SZL SPA RAL", "SZL SNA RAL", "SZL SZA SPA RAL",
 "RAR", "SMA RAR", "SZA RAR", "SZA SMA RAR",
 "SNL RAR", "SNL SMA RAR", "SNL SZA RAR", "SNL SZA SMA RAR",
 "SKP RAR", "SPA RAR", "SNA RAR", "SNA SPA RAR",
 "SZL RAR", "SZL SPA RAR", "SZL SNA RAR", "SZL SZA SPA RAR",
#if defined (PDP15)
 "IAC", "SMA IAC", "SZA IAC", "SZA SMA IAC",
 "SNL IAC", "SNL SMA IAC", "SNL SZA IAC", "SNL SZA SMA IAC",
 "SKP IAC", "SPA IAC", "SNA IAC", "SNA SPA IAC",
 "SZL IAC", "SZL SPA IAC", "SZL SNA IAC", "SZL SZA SPA IAC",
#else
 "RAL RAR", "SMA RAL RAR", "SZA RAL RAR", "SZA SMA RAL RAR",
 "SNL RAL RAR", "SNL SMA RAL RAR", "SNL SZA RAL RAR", "SNL SZA SMA RAL RAR",
 "SKP RAL RAR", "SPA RAL RAR", "SNA RAL RAR", "SNA SPA RAL RAR",
 "SZL RAL RAR", "SZL SPA RAL RAR", "SZL SNA RAL RAR", "SZL SZA SPA RAL RAR",
#endif
 "RTWO", "SMA RTWO", "SZA RTWO", "SZA SMA RTWO",
 "SNL RTWO", "SNL SMA RTWO", "SNL SZA RTWO", "SNL SZA SMA RTWO",
 "SKP RTWO", "SPA RTWO", "SNA RTWO", "SNA SPA RTWO",
 "SZL RTWO", "SZL SPA RTWO", "SZL SNA RTWO", "SZL SZA SPA RTWO",
 "RTL", "SMA RTL", "SZA RTL", "SZA SMA RTL",
 "SNL RTL", "SNL SMA RTL", "SNL SZA RTL", "SNL SZA SMA RTL",
 "SKP RTL", "SPA RTL", "SNA RTL", "SNA SPA RTL",
 "SZL RTL", "SZL SPA RTL", "SZL SNA RTL", "SZL SZA SPA RTL",
 "RTR", "SMA RTR", "SZA RTR", "SZA SMA RTR",
 "SNL RTR", "SNL SMA RTR", "SNL SZA RTR", "SNL SZA SMA RTR",
 "SKP RTR", "SPA RTR", "SNA RTR", "SNA SPA RTR",
 "SZL RTR", "SZL SPA RTR", "SZL SNA RTR", "SZL SZA SPA RTR",
#if defined (PDP15)
 "BSW", "SMA BSW", "SZA BSW", "SZA SMA BSW",
 "SNL BSW", "SNL SMA BSW", "SNL SZA BSW", "SNL SZA SMA BSW",
 "SKP BSW", "SPA BSW", "SNA BSW", "SNA SPA BSW",
 "SZL BSW", "SZL SPA BSW", "SZL SNA BSW", "SZL SZA SPA BSW",
#else
 "RTL RTR", "SMA RTL RTR", "SZA RTL RTR", "SZA SMA RTL RTR",
 "SNL RTL RTR", "SNL SMA RTL RTR", "SNL SZA RTL RTR", "SNL SZA SMA RTL RTR",
 "SKP RTL RTR", "SPA RTL RTR", "SNA RTL RTR", "SNA SPA RTL RTR",
 "SZL RTL RTR", "SZL SPA RTL RTR", "SZL SNA RTL RTR", "SZL SZA SPA RTL RTR",
#endif

 "LLK", "CLQ", "LSN", "OACQ", "ECLA",			/* encode only masks */
 "CMQ", "OMQ", "OSC", 
 "CLA", "CLL", "CML", "CMA",
 "OAS", "HLT",
  NULL  };

static const int32 opc_val[] = {
 0000000+I_MRF, 0040000+I_MRF, 0100000+I_MRF, 0140000+I_MRF,
 0200000+I_MRF, 0240000+I_MRF, 0300000+I_MRF, 0340000+I_MRF,
 0400000+I_MRF, 0440000+I_MRF, 0500000+I_MRF, 0540000+I_MRF,
 0600000+I_MRF,
 0020000+I_MRF, 0060000+I_MRF, 0120000+I_MRF, 0160000+I_MRF,
 0220000+I_MRF, 0260000+I_MRF, 0320000+I_MRF, 0360000+I_MRF,
 0420000+I_MRF, 0460000+I_MRF, 0520000+I_MRF, 0560000+I_MRF,
 0620000+I_MRF,

 0760000+I_LAW,

 0641002+I_NPN, 0641001+I_NPN, 0644000+I_NPN, 0664000+I_NPN, 0652000+I_NPN,
 0653100+MD(022), 0657100+MD(022), 0640300+MD(023), 0644300+MD(023),
 0653300+MD(023), 0657300+MD(023), 0650300+MD(023), 0654300+MD(023),
 0640400+MD(044), 0660400+MD(044),
 0640100+I_ESH, 0660100+I_ESH, 0640300+I_ESH, 0660300+I_ESH,
 0640400+I_ESH, 0660400+I_ESH, 0640500+I_ESH, 0660500+I_ESH,
 0640600+I_ESH, 0660600+I_ESH, 0640700+I_ESH, 0660700+I_ESH,
 0640000+I_EST, 0640000+I_IOT, 

 0700001+I_NPI, 0700002+I_NPI, 0700042+I_NPI, 0700004+I_NPI, 0700044+I_NPI,
 0700101+I_NPI, 0700112+I_NPN, 0700102+I_NPI, 0700104+I_NPI, 0700144+I_NPI,
 0700201+I_NPI, 0700202+I_NPI, 0700204+I_NPI, 0700244+I_NPI, 0700206+I_NPI,
 0700301+I_NPI, 0700312+I_NPN, 0700302+I_NPI, 0700314+I_NPN, 0700304+I_NPI,
 0700401+I_NPI, 0700402+I_NPI, 0700404+I_NPI, 0700406+I_NPI,
#if defined (TYPE62)
 0706501+I_NPI, 0706502+I_NPI, 0706542+I_NPI, 0706506+I_NPI,
 0706601+I_NPI, 0706602+I_NPI, 0706606+I_NPI,
#elif defined (TYPE647)
 0706501+I_NPI, 0706502+I_NPI, 0706522+I_NPI, 0706542+I_NPI, 0706562+I_NPI,
 0706526+I_NPI, 0706546+I_NPI, 0706566+I_NPI,
 0706601+I_NPI, 0706602+I_NPI, 0706622+I_NPI, 0706642+I_NPI, 0706662+I_NPI, 
 0706606+I_NPI, 0706626+I_NPI, 0706646+I_NPI,
#elif defined (LP15)
 0706501+I_NPI, 0706521+I_NPI, 0706541+I_NPI, 0706561+I_NPI,
 0706552+I_NPN, 0706542+I_NPI, 0706544+I_NPI, 0706621+I_NPI, 0706641+I_NPI,
#endif
#if defined (DRM)
 0706006+I_NPI, 0706046+I_NPI, 0706106+I_NPI, 0706204+I_NPI,
 0706101+I_NPI, 0706201+I_NPI, 0706102+I_NPI,
 0706006+I_NPI, 0706046+I_NPI, 0706106+I_NPI, 0706204+I_NPI,
 0706101+I_NPI, 0706201+I_NPI, 0706102+I_NPI,
#endif
#if defined (RF)
 0707001+I_NPI, 0707021+I_NPI, 0707041+I_NPI,
 0707002+I_NPI, 0707022+I_NPI, 0707042+I_NPI, 0707062+I_NPI,
 0707004+I_NPI, 0707024+I_NPI, 0707044+I_NPI, 0707064+I_NPI,
 0707202+I_NPI,                0707242+I_NPI, 0707262+I_NPI,
 0707204+I_NPI, 0707224+I_NPI,
#endif
#if defined (RP)
 0706301+I_NPI, 0706321+I_NPI, 0706341+I_NPI, 0706361+I_NPI,
 0706312+I_NPN, 0706302+I_NPI, 0706332+I_NPN, 0706322+I_NPI, 
 0706342+I_NPN, 0706352+I_NPI,
 0706304+I_NPI, 0706324+I_NPI, 0706344+I_NPI, 0706364+I_NPI,
 0706411+I_NPN, 0706401+I_NPI, 0706421+I_NPI,
 0706412+I_NPN, 0706402+I_NPI, 0706432+I_NPN, 0706422+I_NPI, 
 0706452+I_NPN, 0706442+I_NPI, 0706472+I_NPN, 0706462+I_NPI, 
 0706404+I_NPI, 0706424+I_NPI, 0706454+I_NPN, 0706444+I_NPI, 0706464+I_NPI,
#endif 
#if defined (MTA)
 0707301+I_NPI, 0707321+I_NPI, 0707341+I_NPI, 0707312+I_NPN, 0707322+I_NPI,
 0707352+I_NPN, 0707304+I_NPI, 0707324+I_NPI, 0707326+I_NPI, 
#endif
#if defined (DTA)
 0707541+I_NPI, 0707552+I_NPN, 0707544+I_NPI, 0707545+I_NPI,
 0707561+I_NPI, 0707572+I_NPN, 0707601+I_NPI,
#endif
#if defined (TTY1)
 0704101+I_NPI, 0704112+I_NPN,
 0704001+I_NPI, 0704002+I_NPI, 0704004+I_NPI, 0704006+I_NPI,
#endif
#if defined (PDP7)
 0703201+I_NPI, 0703301+I_NPI, 0703341+I_NPI, 0703302+I_NPI,
 0707701+I_NPI, 0707702+I_NPI, 0707742+I_NPI, 0707704+I_NPI,
#endif
#if defined (PDP9)
 0703341+I_NPI, 0707701+I_NPI, 0707702+I_NPI, 0707704+I_NPI,
 0706504+I_NPI, 0706604+I_NPI,
#endif
#if defined (PDP15)
 0703341+I_NPI, 0707741+I_NPI, 0707742+I_NPI,
 0707761+I_NPI, 0707762+I_NPI, 0707764+I_NPI,
 0720000+I_XR9, 0721000+I_XR, 0722000+I_XR, 0723000+I_XR9,
 0724000+I_XR, 0725000+I_XR9, 0726000+I_XR, 0730000+I_XR,
 0731000+I_XR, 0734000+I_XR, 0735000+I_XR, 0736000+I_XR, 0737000+I_XR9,
#endif
#if defined (PDP9) || defined (PDP15)
 0701701+I_NPI, 0701741+I_NPI, 0701702+I_NPI, 0701742+I_NPI,
 0701704+I_NPI, 0701744+I_NPI, 0703201+I_NPI,
 0703301+I_NPI, 0703302+I_NPI, 0703304+I_NPI, 0703344+I_NPI,
 0705501+I_NPI, 0705512+I_NPN, 0705504+I_NPI,
#endif
 0700000+I_IOT,

 0740000+I_NPN, 0744002+I_NPN, 0744010+I_NPN, 0744020+I_NPN,
 0750001+I_NPN, 0750004+I_NPN, 0750010+I_NPN,
 0740000+I_OPR, 0740100+I_OPR, 0740200+I_OPR, 0740300+I_OPR,
 0740400+I_OPR, 0740500+I_OPR, 0740600+I_OPR, 0740700+I_OPR,
 0741000+I_OPR, 0741100+I_OPR, 0741200+I_OPR, 0741300+I_OPR,
 0741400+I_OPR, 0741500+I_OPR, 0741600+I_OPR, 0741700+I_OPR,
 0740010+I_OPR, 0740110+I_OPR, 0740210+I_OPR, 0740310+I_OPR,
 0740410+I_OPR, 0740510+I_OPR, 0740610+I_OPR, 0740710+I_OPR,
 0741010+I_OPR, 0741110+I_OPR, 0741210+I_OPR, 0741310+I_OPR,
 0741410+I_OPR, 0741510+I_OPR, 0741610+I_OPR, 0741710+I_OPR,
 0740020+I_OPR, 0740120+I_OPR, 0740220+I_OPR, 0740320+I_OPR,
 0740420+I_OPR, 0740520+I_OPR, 0740620+I_OPR, 0740720+I_OPR,
 0741020+I_OPR, 0741120+I_OPR, 0741220+I_OPR, 0741320+I_OPR,
 0741420+I_OPR, 0741520+I_OPR, 0741620+I_OPR, 0741720+I_OPR,
 0740030+I_OPR, 0740130+I_OPR, 0740230+I_OPR, 0740330+I_OPR,
 0740430+I_OPR, 0740530+I_OPR, 0740630+I_OPR, 0740730+I_OPR,
 0741030+I_OPR, 0741130+I_OPR, 0741230+I_OPR, 0741330+I_OPR,
 0741430+I_OPR, 0741530+I_OPR, 0741630+I_OPR, 0741730+I_OPR,
 0742000+I_OPR, 0742100+I_OPR, 0742200+I_OPR, 0742300+I_OPR,
 0742400+I_OPR, 0742500+I_OPR, 0742600+I_OPR, 0742700+I_OPR,
 0743000+I_OPR, 0743100+I_OPR, 0743200+I_OPR, 0743300+I_OPR,
 0743400+I_OPR, 0743500+I_OPR, 0743600+I_OPR, 0743700+I_OPR,
 0742010+I_OPR, 0742110+I_OPR, 0742210+I_OPR, 0742310+I_OPR,
 0742410+I_OPR, 0742510+I_OPR, 0742610+I_OPR, 0742710+I_OPR,
 0743010+I_OPR, 0743110+I_OPR, 0743210+I_OPR, 0743310+I_OPR,
 0743410+I_OPR, 0743510+I_OPR, 0743610+I_OPR, 0743710+I_OPR,
 0742020+I_OPR, 0742120+I_OPR, 0742220+I_OPR, 0742320+I_OPR,
 0742420+I_OPR, 0742520+I_OPR, 0742620+I_OPR, 0742720+I_OPR,
 0743020+I_OPR, 0743120+I_OPR, 0743220+I_OPR, 0743320+I_OPR,
 0743420+I_OPR, 0743520+I_OPR, 0743620+I_OPR, 0743720+I_OPR,
 0742030+I_OPR, 0742130+I_OPR, 0742230+I_OPR, 0742330+I_OPR,
 0742430+I_OPR, 0742530+I_OPR, 0742630+I_OPR, 0742730+I_OPR,
 0743030+I_OPR, 0743130+I_OPR, 0743230+I_OPR, 0743330+I_OPR,
 0743430+I_OPR, 0743530+I_OPR, 0743630+I_OPR, 0743730+I_OPR,

 0660000+I_EST, 0650000+I_EST, 0644000+I_EST, 0642000+I_EST, 0641000+I_EST,
 0640004+I_EST, 0640002+I_EST, 0640001+I_EST,
 0750000+I_OPR, 0744000+I_OPR, 0740002+I_OPR, 0740001+I_OPR,
 0740004+I_OPR, 0740040+I_OPR,
 -1 };

/* Operate or EAE decode

   Inputs:
	*of	=	output stream
	inst	=	mask bits
	class	=	instruction class code
	sp	=	space needed?
   Outputs:
	status	=	space needed?
*/

int32 fprint_opr (FILE *of, int32 inst, int32 class, int32 sp)
{
int32 i, j;

for (i = 0; opc_val[i] >= 0; i++) {			/* loop thru ops */
	j = (opc_val[i] >> I_V_FL) & I_M_FL;		/* get class */
	if ((j == class) && (opc_val[i] & inst)) {	/* same class? */
		inst = inst & ~opc_val[i];		/* mask bit set? */
		fprintf (of, (sp? " %s": "%s"), opcode[i]);
		sp = 1;  }  }
return sp;
}

/* Symbolic decode

   Inputs:
	*of	=	output stream
	addr	=	current PC
	*val	=	pointer to values
	*uptr	=	pointer to unit
	sw	=	switches
   Outputs:
	return	=	status code
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)
#define SIXTOASC(x) (((x) >= 040)? (x): (x) + 0100)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
int32 cflag, i, j, k, sp, inst, disp, ma;

inst = val[0];
i = val[1];
cflag = (uptr == NULL) || (uptr == &cpu_unit);
if (sw & SWMASK ('A')) {				/* ASCII? */
	if (inst > 0377) return SCPE_ARG;
	fprintf (of, FMTASC (inst & 0177));
	return SCPE_OK;  }
if (sw & SWMASK ('C')) {				/* character? */
	fprintf (of, "%c", SIXTOASC ((inst >> 12) & 077));
	fprintf (of, "%c", SIXTOASC ((inst >> 6) & 077));
	fprintf (of, "%c", SIXTOASC (inst & 077));
	return SCPE_OK;  }
#if defined (PDP15)
if (sw & SWMASK ('P')) {				/* packed ASCII? */
	fprintf (of, "%c", FMTASC ((inst >> 11) & 0177));
	fprintf (of, "%c", FMTASC ((inst >> 4) & 0177));
	fprintf (of, "%c", FMTASC (((inst << 3) | (i >> 15)) & 0177));
	fprintf (of, "%c", FMTASC ((i >> 8) & 0177));
	fprintf (of, "%c", FMTASC ((i >> 1) & 0177));
	return -1;  }
#endif
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

/* Instruction decode */

for (i = 0; opc_val[i] >= 0; i++) {			/* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;		/* get class */
    if ((opc_val[i] & 0777777) == (inst & masks[j])) {	/* match? */

	switch (j) {					/* case on class */
	case I_V_NPN:					/* no operands */
	case I_V_XR:					/* index no opers */
		fprintf (of, "%s", opcode[i]);		/* opcode */
		break;
	case I_V_NPI:					/* IOT no operand */
		fprintf (of, "%s", opcode[i]);		/* opcode */
		if (inst & 010) fprintf (of, " +10");
		break;
	case I_V_IOT:					/* IOT or EAE */
		fprintf (of, "%s %-o", opcode[i], inst & 037777);
		break;
	case I_V_MRF:					/* mem ref */
#if defined (PDP15)
		if (memm) {
			disp = inst & 017777;  
			ma = (addr & 0760000) | disp;  }
		else {	disp = inst & 007777;
			ma = (addr & 0770000) | disp;  }
		fprintf (of, "%s %-o", opcode[i],
			(cflag? ma & ADDRMASK: disp));
		if (!memm && (inst & 0010000)) fprintf (of, ",X");
#else
		disp = inst & 017777;
		ma = (addr & 0760000) | disp;
		fprintf (of, "%s %-o", opcode[i],
			(cflag? ma & ADDRMASK: disp));
#endif
		break;
	case I_V_OPR:					/* operate */
		if (sp = (inst & 03730)) fprintf (of, "%s", opcode[i]);
		fprint_opr (of, inst & 014047, I_V_OPR, sp);
		break;
	case I_V_LAW:					/* LAW */
		fprintf (of, "%s %-o", opcode[i], inst & 017777);
		break;
	case I_V_XR9:					/* index with lit */
		disp = inst & 0777;
		if (disp & 0400) fprintf (of, "%s -%-o", opcode[i], 01000 - disp);
		else fprintf (of, "%s %-o", opcode[i], disp);
		break;
	case I_V_EST:					/* EAE setup */
		fprint_opr (of, inst & 037007, I_V_EST, 0);
		break;
	case I_V_ESH:					/* EAE shift */
		sp = fprint_opr (of, inst & 017000, I_V_EST, 0);
		fprintf (of, (sp? " %s %-o": "%s %-o"), opcode[i], inst & 077);
		break;
	case I_V_EMD:					/* EAE mul-div */
		disp = inst & 077;			/* get actual val */
		k = (opc_val[i] >> I_V_DC) & 077;	/* get default val */
		if (disp == k) fprintf (of, "%s", opcode[i]);
		else if (disp < k) fprintf (of, "%s -%-o", opcode[i], k - disp);
		else fprintf (of, "%s +%-o", opcode[i], disp - k);
		break;  }				/* end case */
	return SCPE_OK;  }				/* end if */
	}						/* end for */
return SCPE_ARG;
}

/* Get 18b signed number

   Inputs:
	*cptr	=	pointer to input string
	*sign	=	pointer to sign
	*status	=	pointer to error status
   Outputs:
	val	=	output value
*/

t_value get_sint (char *cptr, int32 *sign, t_stat *status)
{
*sign = 0;
if (*cptr == '+') {
	*sign = 1;
	cptr++;  }
else if (*cptr == '-') {
	*sign = -1;
	cptr++;  }
return get_uint (cptr, 8, 0777777, status);
}

/* Symbolic input

   Inputs:
	*cptr	=	pointer to input string
	addr	=	current PC
	uptr	=	pointer to unit
	*val	=	pointer to output values
	sw	=	switches
   Outputs:
	status	=	error status
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, d, i, j, k, sign, dmask, epcmask;
t_stat r;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;
for (i = 1; (i < 5) && (cptr[i] != 0); i++)
	if (cptr[i] == 0) for (j = i + 1; j <= 5; j++) cptr[j] = 0;
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = (t_value) cptr[0] | 0200;
	return SCPE_OK;  }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = (((t_value) cptr[0] & 077) << 12) |
		 (((t_value) cptr[1] & 077) << 6) |
		  ((t_value) cptr[2] & 077);
	return SCPE_OK;  }
#if defined (PDP15)
if ((sw & SWMASK ('P')) || ((*cptr == '#') && cptr++)) { /* packed string? */
	if (cptr[0] == 0) return SCPE_ARG;		/* must have 1 char */
	val[0] = (((t_value) cptr[0] & 0177) << 11) |
		 (((t_value) cptr[1] & 0177) << 4) |
		 (((t_value) cptr[2] & 0170) >> 3);
	val[1] = (((t_value) cptr[2] & 0007) << 15) |
		 (((t_value) cptr[3] & 0177) << 8) |
		 (((t_value) cptr[4] & 0177) << 1);
	return -1;  }
#endif

/* Symbolic input, continued */

cptr = get_glyph (cptr, gbuf, 0);			/* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL) return SCPE_ARG;
val[0] = opc_val[i] & DMASK;				/* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;			/* get class */

switch (j) {						/* case on class */
case I_V_XR:						/* index */
	break;
case I_V_XR9:						/* index literal */
	cptr = get_glyph (cptr, gbuf, 0);		/* get next field */
	d = get_sint (gbuf, &sign, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	if (((sign >= 0) && (d > 0377)) || ((sign < 0) && (d > 0400)))
		return SCPE_ARG;
	val[0] = val[0] | ((sign >= 0)? d: (01000 - d));
	break;
case I_V_LAW:						/* law */
	cptr = get_glyph (cptr, gbuf, 0);		/* get next field */
	d = get_uint (gbuf, 8, 017777, &r);
	if (r != SCPE_OK) return SCPE_ARG;
	val[0] = val[0] | d;
	break;
case I_V_MRF:						/* mem ref */
#if defined (PDP15)
	if (memm) dmask = 017777;
	else dmask = 07777;
	cptr = get_glyph (cptr, gbuf, ',');		/* get glyph */
#else
	dmask = 017777;
	cptr = get_glyph (cptr, gbuf, 0);		/* get next field */
#endif
#if defined (PDP4) || defined (PDP7)
	if (strcmp (gbuf, "I") == 0) {			/* indirect? */
		val[0] = val[0] | 020000;
		cptr = get_glyph (cptr, gbuf, 0);  }
#endif
	epcmask = ADDRMASK & ~dmask;			/* get ePC */
	d = get_uint (gbuf, 8, ADDRMASK, &r);		/* get addr */
	if (r != SCPE_OK) return SCPE_ARG;
	if (d <= dmask) val[0] = val[0] | d;		/* fit in 12/13b? */
	else if (cflag && (((addr ^ d) & epcmask) == 0))
		val[0] = val[0] | (d & dmask);		/* hi bits = ePC? */
	else return SCPE_ARG;
#if defined (PDP15)
	if (!memm) {
		cptr = get_glyph (cptr, gbuf, 0);
		if (gbuf[0] != 0) {
			if (strcmp (gbuf, "X") != 0) return SCPE_ARG;
			val[0] = val[0] | 010000;  }  }
#endif
	break;
case I_V_EMD:						/* or'able */
	val[0] = val[0] | ((opc_val[i] >> I_V_DC) & 077); /* default shift */
case I_V_EST: case I_V_ESH: 
case I_V_NPN: case I_V_NPI: case I_V_IOT: case I_V_OPR:
	for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
		cptr = get_glyph (cptr, gbuf, 0)) {
		for (i = 0; (opcode[i] != NULL) &&
			(strcmp (opcode[i], gbuf) != 0) ; i++) ;
		if (opcode[i] != NULL) {
			k = opc_val[i] & DMASK;
			if (((k ^ val[0]) & 0740000) != 0) return SCPE_ARG;
			val[0] = val[0] | k;  }
		else {	d = get_sint (gbuf, & sign, &r);
			if (r != SCPE_OK) return SCPE_ARG;
			if (sign > 0) val[0] = val[0] + d;  
			else if (sign < 0) val[0] = val[0] - d;
			else val[0] = val[0] | d;  }  }
	break;  }					/* end case */
if (*cptr != 0) return SCPE_ARG;			/* junk at end? */
return SCPE_OK;
}
