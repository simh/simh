/* i1401_dat.h: IBM 1401 character conversion tables

   Copyright (c) 1993-2004, Robert M. Supnik

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

/* ASCII to BCD conversion */

const char ascii_to_bcd[128] = {
	000, 000, 000, 000, 000, 000, 000, 000,		/* 000 - 037 */
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 052, 077, 013, 053, 034, 060, 032,		/* 040 - 077 */
	017, 074, 054, 037, 033, 040, 073, 021,
	012, 001, 002, 003, 004, 005, 006, 007,
	010, 011, 015, 056, 076, 035, 016, 072,
	014, 061, 062, 063, 064, 065, 066, 067,		/* 100 - 137 */
	070, 071, 041, 042, 043, 044, 045, 046,
	047, 050, 051, 022, 023, 024, 025, 026,
	027, 030, 031, 075, 036, 055, 020, 057,
	000, 061, 062, 063, 064, 065, 066, 067,		/* 140 - 177 */
	070, 071, 041, 042, 043, 044, 045, 046,
	047, 050, 051, 022, 023, 024, 025, 026,
	027, 030, 031, 000, 000, 000, 000, 000 };

/* BCD to ASCII conversion - also the "full" print chain */

char bcd_to_ascii[64] = {
	' ', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '0', '#', '@', ':', '>', '(',
	'^', '/', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', '\'', ',', '%', '=', '\\', '+',
	'-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', '!', '$', '*', ']', ';', '_',
	'&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', '?', '.', ')', '[', '<', '"' };
