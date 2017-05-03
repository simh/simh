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

#define CENT_       0xA2                    /* cent and not: standard DOS mapping */
#define NOT_        0xAC
#define IGNR_       0xFF
#define CRLF_       '\r'

#define COUT_IS_CTRL            0x01        /* conout characters with bit 1 set are controls: */

#define COUT_CTRL_BLACK         0x04        /* none or one of these bits */
#define COUT_CTRL_RED           0x08

#define COUT_CTRL_LINEFEED      0x02        /* plus none or one of these bits */
#define COUT_CTRL_BACKSPACE     0x10
#define COUT_CTRL_SPACE         0x20
#define COUT_CTRL_TAB           0x40
#define COUT_CTRL_RETURN        0x80

#ifdef _MSC_VER
#  pragma warning(disable:4245)                     /* enable int->char demotion warning caused by characters with high-bit set */
#endif

static unsigned char conout_to_ascii[] =    /* console output code to ASCII */
{
              /*  00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F */
    /* 00 */     '.',  IGNR_,CENT_, '\n', '@', IGNR_,'%',  _0_,  _0_,  IGNR_,_0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* 10 */     'F', '\b',  'f',  _0_,  'G',  _0_,  'g',  _0_,  'B',  _0_,  'b',  _0_,  'C',  _0_,  'c',  _0_,
    /* 20 */     'I',  ' ',  'i',  _0_,  'H',  _0_,  'h',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* 30 */     'D',  _0_,  'd',  _0_,  'E',  _0_,  'e',  _0_,  _0_,  _0_,  _0_,  _0_,   'A', _0_,  'a',  _0_,
    /* 40 */     '$',  '\t', '!',  _0_,  '&',  _0_,  '>',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* 50 */     'O',  _0_,  'o',  _0_,  'P',  _0_,  'o',  _0_,  'K',  _0_,  'k',  _0_,  'L',  _0_,  'l',  _0_,
    /* 60 */     'R',  _0_,  'r',  _0_,  'Q',  _0_,  'q',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* 70 */     'M',  _0_,  'm',  _0_,  'N',  _0_,  'n',  _0_,  _0_,  _0_,  _0_,  _0_,  'J',  _0_,  'j',  _0_,
    /* 80 */     ',', CRLF_, ':',  _0_,  '-',  _0_,  '?',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* 90 */     'W',  _0_,  'w',  _0_,  'X',  _0_,  'x',  _0_,  'S',  _0_,   's', _0_,  'T',  _0_,  't',  _0_,
    /* A0 */     'Z',  _0_,  'z',  _0_,  'Y',  _0_,  'y',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* B0 */     'U',  _0_,  'u',  _0_,  'V',  _0_,  'v',  _0_,  _0_,  _0_,  _0_,  _0_,  '/',  _0_,  '_',  _0_,
    /* C0 */     '#',  _0_,  '=',  _0_,  '0',  _0_,  '|',  _0_,  _0_,  _0_,  _0_,  _0_,  'J',  _0_,  'j',  _0_,
    /* D0 */     '6',  _0_,  ';',  _0_,  '7',  _0_,  '*',  _0_,  '2',  _0_,   '+', _0_,  '3',  _0_,  '<',  _0_,
    /* E0 */     '9',  _0_,  '"',  _0_,  '8',  _0_, '\'',  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,  _0_,
    /* F0 */     '4',  _0_,  NOT_, _0_,  '5',  _0_,  ')',  _0_,  _0_,  _0_,  _0_,  _0_,  '1',  _0_,  '(',  _0_,
};

#ifdef _MSC_VER
#  pragma warning(default:4245)                     /* enable int->char demotion warning */
#endif
