/* IBM1130 CONSOLE OUTPUT TO ASCII CONVERSION TABLE 
 *
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

#define _0_ '\0'

#define CENT		'\xA2'
#define REST		IGNR
#define RDRSTP		'?'
#define NOT			'\xAC'
#define IGNR		'\xFF'
#define CRLF		'\r'

static char conout_to_ascii[] = 			/* console output code to ASCII	*/
{
			  /*  00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F */
	/* 00 */     '.',  _0_, CENT, '\n',  '@', REST,  '%',  _0_,  _0_,RDRSTP, _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* 10 */     'f', '\b',  'F',  _0_,  'g',  _0_,  'G',  _0_,  'b',  _0_,  'B',  _0_,  'c',  _0_,  'C',  _0_,
	/* 20 */     'i',  ' ',  'I',  _0_,  'h',  _0_,  'H',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* 30 */     'd',  _0_,  'D',  _0_,  'e',  _0_,  'E',  _0_,  _0_,  _0_,  _0_,  _0_,   'a', _0_,  'A',  _0_,
	/* 40 */     '$',  _0_,  '!',  _0_,  '&',  _0_,  '>',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* 50 */     'o',  _0_,  'O',  _0_,  'p',  _0_,  'P',  _0_,  'k',  _0_,  'K',  _0_,  'l',  _0_,  'L',  _0_,
	/* 60 */     'r',  _0_,  'R',  _0_,  'q',  _0_,  'Q',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* 70 */     'm',  _0_,  'M',  _0_,  'n',  _0_,  'N',  _0_,  _0_,  _0_,  _0_,  _0_,  'j',  _0_,  _0_,  _0_,
	/* 80 */     ',', CRLF,  ':',  _0_,  '-',  _0_,  '?',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* 90 */     'w',  _0_,  'W',  _0_,  'x',  _0_,  'X',  _0_,  's',  _0_,   'S', _0_,  't',  _0_,  'T',  _0_,
	/* A0 */     'z',  _0_,  'Z',  _0_,  'y',  _0_,  'Y',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* B0 */     'u',  _0_,  'U',  _0_,  'v',  _0_,  'V',  _0_,  _0_,  _0_,  _0_,  _0_,  '/',  _0_,  '_',  _0_,
	/* C0 */     '#',  _0_,  '=',  _0_,  '0',  _0_,  '|',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  'J',  _0_,
	/* D0 */     '6',  _0_,  ';',  _0_,  '7',  _0_,  '*',  _0_,  '2',  _0_,   '+', _0_,  '3',  _0_,  '<',  _0_,
	/* E0 */     '9',  _0_,  '"',  _0_,  '8',  _0_, '\'',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
	/* F0 */     '4',  _0_,  NOT,  _0_,  '5',  _0_,  ')',  _0_,  _0_,  _0_,  _0_,  _0_,  '1',  _0_,  '(',  _0_,
};

