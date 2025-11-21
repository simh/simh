/* s100_sio.h

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   11/07/25 Initial version

*/

#ifndef _S100_SIO_H
#define _S100_SIO_H

#define UNIT_SIO_V_VERBOSE      (UNIT_V_UF+0)
#define UNIT_SIO_VERBOSE        (1 << UNIT_SIO_V_VERBOSE)
#define UNIT_SIO_V_CONSOLE      (UNIT_V_UF+1)
#define UNIT_SIO_CONSOLE        (1 << UNIT_SIO_V_CONSOLE)

typedef struct {
    uint8 type;     /* Type Value                        */
    char *name;     /* Name                              */
    char *desc;     /* Description                       */
    int32 base;     /* Base Port                         */
    int32 stat;     /* Status Port Offset                */
    int32 data;     /* Data Port Offset                  */
    int32 rdre;     /* Receive Data Register Empty Mask  */
    int32 rdrf;     /* Receive Data Register Full Mask   */
    int32 tdre;     /* Transmit Data Register Empty Mask */
    int32 tdrf;     /* Transmit Data Register Full Mask  */
} SIO;

typedef struct {
    uint8 type;     /* Board SIO Configuration Type      */
    char *name;     /* Board Name                        */
    char *desc;     /* Board Description                 */
    int32 base;     /* Board Base I/O Address            */
} SIO_BOARD;

#endif

