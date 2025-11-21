/* mits_2sio.h

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

#ifndef _MITS_2SIO_H
#define _MITS_2SIO_H

#define UNIT_V_M2SIO_CONSOLE  (UNIT_V_UF + 0)     /* Port checks console for input */
#define UNIT_M2SIO_CONSOLE    (1 << UNIT_V_M2SIO_CONSOLE)
#define UNIT_V_M2SIO_MAP      (UNIT_V_UF + 1)     /* map keyboard characters       */
#define UNIT_M2SIO_MAP        (1 << UNIT_V_M2SIO_MAP)
#define UNIT_V_M2SIO_BS       (UNIT_V_UF + 2)     /* map delete to backspace       */
#define UNIT_M2SIO_BS         (1 << UNIT_V_M2SIO_BS)
#define UNIT_V_M2SIO_UPPER    (UNIT_V_UF + 3)     /* map to upper case             */
#define UNIT_M2SIO_UPPER      (1 << UNIT_V_M2SIO_UPPER)
#define UNIT_V_M2SIO_DTR      (UNIT_V_UF + 4)     /* DTR follows RTS               */
#define UNIT_M2SIO_DTR        (1 << UNIT_V_M2SIO_DTR)
#define UNIT_V_M2SIO_DCD      (UNIT_V_UF + 5)     /* Force DCD active low          */
#define UNIT_M2SIO_DCD        (1 << UNIT_V_M2SIO_DCD)
#define UNIT_V_M2SIO_CTS      (UNIT_V_UF + 6)     /* Force CTS active low          */
#define UNIT_M2SIO_CTS        (1 << UNIT_V_M2SIO_CTS)

typedef struct {
    int32 port;          /* Port 0 or 1      */
    t_bool conn;         /* Connected Status */
    int32 baud;          /* Baud rate        */
    int32 rts;           /* RTS Status       */
    int32 rxb;           /* Receive Buffer   */
    int32 txb;           /* Transmit Buffer  */
    t_bool txp;          /* Transmit Pending */
    int32 stb;           /* Status Buffer    */
    int32 ctb;           /* Control Buffer   */
    t_bool rie;          /* Rx Int Enable    */
    t_bool tie;          /* Tx Int Enable    */
    t_bool dcdl;         /* DCD latch        */
    uint8 intenable;     /* Interrupt Enable */
    uint8 intvector;     /* Interrupt Vector */
    uint8 databus;       /* Data Bus Value   */
} M2SIO_REG;

#endif
