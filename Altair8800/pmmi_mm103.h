/* pmmi_mm103.h

   Copyright (c) 2026 Patrick A. Linstruth

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
   01/18/26 Initial version

*/

#ifndef _PMMI_MM103_H
#define _PMMI_MM103_H


#define UNIT_V_PMMI_VERBOSE  (UNIT_V_UF + 0)     /* Verbose messages               */
#define UNIT_PMMI_VERBOSE    (1 << UNIT_V_PMMI_VERBOSE)
#define UNIT_V_PMMI_CONSOLE  (UNIT_V_UF + 1)     /* Use this device for console    */
#define UNIT_PMMI_CONSOLE    (1 << UNIT_V_PMMI_CONSOLE)
#define UNIT_V_PMMI_RTS      (UNIT_V_UF + 2)     /* RTS follows DTR                */
#define UNIT_PMMI_RTS        (1 << UNIT_V_PMMI_RTS)

#define PMMI_WAIT        500            /* Service Wait Interval */

#define PMMI_IOBASE      0xC0
#define PMMI_IOSIZE      4

#define PMMI_REG0        0              /* Relative Address 0 */
#define PMMI_REG1        1              /* Relative Address 1 */
#define PMMI_REG2        2              /* Relative Address 2 */
#define PMMI_REG3        3              /* Relative Address 3 */

#define PMMI_TBMT        0x01           /* Transmit Data Register Empty */
#define PMMI_DAV         0x02           /* Receive Data Register Full   */
#define PMMI_TEOC        0x04           /* Transmit Serializer Empty    */
#define PMMI_RPE         0x08           /* Parity Error                 */
#define PMMI_OR          0x10           /* Overrun                      */
#define PMMI_FE          0x20           /* Framing Error                */

#define PMMI_DT          0x01           /* Dial Tone                    */
#define PMMI_RNG         0x02           /* Ringing                      */
#define PMMI_CTS         0x04           /* Clear to Send                */
#define PMMI_RXBRK       0x08           /* RX Break                     */
#define PMMI_AP          0x10           /* Answer Phone                 */
#define PMMI_FO          0x20           /* Digital Carrier Signal       */
#define PMMI_MODE        0x40           /* Mode                         */
#define PMMI_TMR         0x80           /* Timer Pulses                 */

#define PMMI_ST          0x10           /* Self Test                    */
#define PMMI_DTR         0x40           /* DTR                          */

#define PMMI_SH          0x01           /* Switch Hook                  */
#define PMMI_RI          0x02           /* Ring Indicator               */
#define PMMI_5BIT        0x00           /* 5 Data Bits                  */
#define PMMI_6BIT        0x04           /* 6 Data Bits                  */
#define PMMI_7BIT        0x08           /* 7 Data Bits                  */
#define PMMI_8BIT        0x0C           /* 8 Data Bits                  */
#define PMMI_BMSK        0x0C           /* Data Bits Bit Mask           */

#define PMMI_OPAR        0x00           /* Odd Parity                   */
#define PMMI_NPAR        0x10           /* No Parity                    */
#define PMMI_EPAR        0x20           /* Odd Parity                   */
#define PMMI_PMSK        0x30           /* Parity Bit Mask              */

#define PMMI_1SB         0x00           /* 1 Stop Bit                   */
#define PMMI_15SB        0x40           /* 1.5 Stop Bits                */
#define PMMI_2SB         0x40           /* 2 Stop Bits                  */
#define PMMI_SMSK        0x40           /* Stop Bits Bit Mask           */

#define PMMI_CLOCK       2500           /* Rate Generator / 100         */
#define PMMI_BAUD        300            /* Default baud rate            */

#endif

