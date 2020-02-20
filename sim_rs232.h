/* sim_rs232.h: RS-232 signal declarations

   Copyright (c) 2020, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   09-Jan-20    JDB     First release version
   01-Jan-20    JDB     Created file


   This module contains the declarations of the RS-232 modem control signals as
   provided by the 9-pin PC serial port.  The port provides these signals
   (called "circuits" in the RS-232 standard):

     Pin  Code  Description          Function
     ---  ----  -------------------  --------
      3   TXD   Transmitted Data     Data
      2   RXD   Received Data        Data
      7   RTS   Request to Send      Control
      8   CTS   Clear to Send        Status
      4   DTR   Data Terminal Ready  Control
      6   DSR   Data Set Ready       Status
      1   DCD   Data Carrier Detect  Status
      9   RI    Ring Indicator       Status
      5   GND   Signal Ground        Power


   Implementation notes:

    1. The signals are declared as an enumeration because the "gdb" debugger has
       explicit display support for sets containing enumeration values.

    2. In addition to the RS-232 declarations, we also declare a "reset" value
       that is used to reinitialize modem handling during a device reset, and an
       "error" value that is returned for an error condition.
*/



typedef enum {                                  /* RS-232 signals */
    No_Signals    = 0000,                       /*   no signals          */
    Reset_Control = 0001,                       /*   reset control       */
    DTR_Control   = 0002,                       /*   Data Terminal Ready */
    RTS_Control   = 0004,                       /*   Request To Send     */
    DSR_Status    = 0010,                       /*   Data Set Ready      */
    CTS_Status    = 0020,                       /*   Clear To Send       */
    DCD_Status    = 0040,                       /*   Data Carrier Detect */
    RI_Status     = 0100,                       /*   Ring Indicator      */
    Error_Status  = 0200                        /*   error status        */
    } RS232_SIGNAL;
