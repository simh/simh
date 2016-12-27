/* i1401_dat.h: IBM 1401 character conversion tables

   Copyright (c) 1993-2008, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   20-Sep-05    RMS     Updated for compatibility with Paul Pierce conventions
*/

/* Old tables */
/* ASCII to BCD conversion */

const char ascii_to_bcd_old[128] = {
    000, 000, 000, 000, 000, 000, 000, 000,             /* 000 - 037 */
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 052, 077, 013, 053, 034, 060, 032,             /* 040 - 077 */
    017, 074, 054, 037, 033, 040, 073, 021,
    012, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 015, 056, 076, 035, 016, 072,
    014, 061, 062, 063, 064, 065, 066, 067,             /* 100 - 137 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 075, 036, 055, 020, 057,
    000, 061, 062, 063, 064, 065, 066, 067,             /* 140 - 177 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 000, 000, 000, 000, 000
    };

/* BCD to ASCII conversion - also the "full" print chain */

char bcd_to_ascii_old[64] = {
    ' ',   '1',   '2',   '3',   '4',   '5',   '6',   '7',
    '8',   '9',   '0',   '#',   '@',   ':',   '>',   '(',
    '^',   '/',   'S',   'T',   'U',   'V',   'W',   'X',
    'Y',   'Z',   '\'',  ',',   '%',   '=',   '\\',  '+',
    '-',   'J',   'K',   'L',   'M',   'N',   'O',   'P',
    'Q',   'R',   '!',   '$',   '*',   ']',   ';',   '_',
    '&',   'A',   'B',   'C',   'D',   'E',   'F',   'G',
    'H',   'I',   '?',   '.',   ')',   '[',   '<',   '"'
    };

/* New tables */
/* ASCII to BCD conversion */

const char ascii_to_bcd[128] = {
    000, 000, 000, 000, 000, 000, 000, 000,             /* 000 - 037 */
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 052, 037, 013, 053, 034, 060, 014,             /* 040 - 077 */
    034, 074, 054, 060, 033, 040, 073, 021,
    012, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 015, 056, 076, 013, 016, 072,
    014, 061, 062, 063, 064, 065, 066, 067,             /* 100 - 137 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 075, 036, 055, 020, 057,
    000, 061, 062, 063, 064, 065, 066, 067,             /* 140 - 177 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 017, 032, 077, 035, 000
    };

/* BCD to ASCII conversion */

const char bcd_to_ascii_a[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '#', '@', ':', '>', '{',
    '^', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '%', '~', '\\', '"',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}'
    };

const char bcd_to_ascii_h[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ':', '>', '{',
    '^', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '(', '~', '\\', '"',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}'
    };

/* BCD to ASCII 48 character print chains */

const char bcd_to_pca[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '#', '@', ' ', ' ', ' ',
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', ' ', ',', '%', ' ', ' ', ' ',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '-', '$', '*', ' ', ' ', ' ',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '&', '.', ')', ' ', ' ', ' '
    };

const char bcd_to_pch[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ' ', ' ', ' ',
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', ' ', ',', '(', ' ', ' ', ' ',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '-', '$', '*', ' ', ' ', ' ',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '&', '.', ')', ' ', ' ', ' '
    };

/* BCD to column binary conversion */

const uint32 bcd_to_colbin[64] = {
    00000, 00400, 00200, 00100, 00040, 00020, 00010, 00004,
    00002, 00001, 00202, 00102, 00042, 00022, 00012, 00006,
    01000, 01400, 01200, 01100, 01040, 01020, 01010, 01004,
    01002, 01001, 01202, 01102, 01042, 01022, 01012, 01006,
    02000, 02400, 02200, 02100, 02040, 02020, 02010, 02004,
    02002, 02001, 02202, 02102, 02042, 02022, 02012, 02006,
    04000, 04400, 04200, 04100, 04040, 04020, 04010, 04004,
    04002, 04001, 04202, 04102, 04042, 04022, 04012, 04006
    };
