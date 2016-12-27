/* i7094_dat.h: IBM 7094 data conversion tables

   Copyright (c) 2003-2008, Robert M. Supnik

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
*/

/* Nine-code to ASCII conversion */

const char nine_to_ascii_a[64] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '^', '#', '@', ':', '>', '{',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '%', '~', '\\', '"',
    };

const char nine_to_ascii_h[64] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '^', '=', '\'', ':', '>', '{',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '(', '~', '\\', '"',
    };

/* ASCII to nine-code conversion */

const char ascii_to_nine[128] = {
    060, 060, 060, 060, 060, 060, 060, 060,             /* 000 - 037 */
    060, 060, 060, 060, 060, 060, 060, 060,
    060, 060, 060, 060, 060, 060, 060, 060,
    060, 060, 060, 060, 060, 060, 060, 060,
    060, 052, 077, 013, 053, 074, 020, 014,             /* 040 - 077 */
    074, 034, 054, 020, 073, 040, 033, 061,
    000, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 015, 056, 036, 013, 016, 032,
    014, 021, 022, 023, 024, 025, 026, 027,             /* 100 - 137 */
    030, 031, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 062, 063, 064, 065, 066,
    067, 070, 071, 035, 076, 055, 012, 057,
    060, 021, 022, 023, 024, 025, 026, 027,             /* 140 - 177 */
    030, 031, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 062, 063, 064, 065, 066,
    067, 070, 071, 017, 072, 037, 075, 060
    };

/* ASCII to BCD conversion */

const char ascii_to_bcd[128] = {
    000, 000, 000, 000, 000, 000, 000, 000,             /* 000 - 037 */
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 000, 000, 000, 000, 000, 000, 000,
    000, 052, 037, 013, 053, 074, 060, 014,             /* 040 - 077 */
    034, 074, 054, 060, 033, 040, 073, 021,
    020, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 015, 056, 076, 013, 016, 072,
    014, 061, 062, 063, 064, 065, 066, 067,             /* 100 - 137 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 075, 036, 055, 012, 057,
    000, 061, 062, 063, 064, 065, 066, 067,             /* 140 - 177 */
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 017, 032, 077, 035, 000
    };

/* BCD to ASCII conversion */

const char bcd_to_ascii_a[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '^', '#', '@', ':', '>', '{',
    '0', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '%', '~', '\\', '"',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}'
    };

const char bcd_to_ascii_h[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '^', '=', '\'', ':', '>', '{',
    '0', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '|', ',', '(', '~', '\\', '"',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '_',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '}'
    };

/* BCD to ASCII 48 character print chains */

const char bcd_to_pca[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', ' ', '#', '@', ' ', ' ', ' ',
    '0', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', ' ', ',', '%', ' ', ' ', ' ',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '-', '$', '*', ' ', ' ', ' ',
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '&', '.', ')', ' ', ' ', ' '
    };

const char bcd_to_pch[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', ' ', '=', '\'', ' ', ' ', ' ',
    '0', '/', 'S', 'T', 'U', 'V', 'W', 'X',
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
