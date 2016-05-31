/*  system_80_20_cfg.h: Intel System 80/20 simulator definitions

    This file holds the configuration for the System 80/20 
    boards I/O and Memory.

    Copyright (c) 2010, William A. Beech

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
    William A. Beech BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of William A. Beech shall not be
    used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from William A. Beech.

    16 Dec 12 - Original file
*/

/* set the base I/O address for the iSBC 208 */
#define	SBC208_BASE	0x40

/* configure interrupt request line */
#define SBC208_INT      INT_1

/* set the base and size for the iSBC 064 */
#define SBC064_BASE     0x0000
#define SBC064_SIZE     0x10000

