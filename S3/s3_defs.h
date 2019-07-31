/* s3_defs.h: IBM System/3 simulator definitions 

   Copyright (c) 2001-2005, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.
*/

#include "sim_defs.h"                                   /* simulator defns */

/* Simulator stop codes */

#define STOP_RSRV       1                               /* must be 1 */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_INVOP      4                               /* program check - invalid op */
#define STOP_INVQ       5                               /* Prog check - invalid Q */
#define STOP_INVADDR    6                               /* Prog check - invalid addr */
#define STOP_INVDEV     7                               /* Prog check - invalid dev cmd */
#define STOP_NOCD       8                               /* ATTN card reader */
#define RESET_INTERRUPT 77                              /* special return from SIO */

/* Memory */

#define MAXMEMSIZE      65536                           /* max memory size */
#define AMASK           (MAXMEMSIZE - 1)                /* logical addr mask */
#define PAMASK          (MAXMEMSIZE - 1)                /* physical addr mask */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */

#define MAX_DECIMAL_DIGITS 31                           /* max size of a decimal number */
#define CDR_WIDTH       80                              /* Max card size */
#define CDP_WIDTH       80                              /* Punch width */
#define LPT_WIDTH       132
#define CCT_LNT         132

#define DSK_SECTSIZE    256                             /* Sector length */
#define DSK_CYLSIZE     256*48                          /* Cylinder length */

/* I/O structure

   The I/O structure is tied together by dev_table, indexed by
   the device number.  Each entry in dev_table consists of

        level           Interrupt level for device (0-7)
        priority        Priority for device (1-8)
        routine         IOT action routine
*/

struct ndev {
    int32   level;                                      /* interrupt level */
    int32   pri;                                        /* Device priority */
    int32   (*routine)(int32, int32, int32, int32);     /* dispatch routine */
};

/* Structure to define operation codes */

struct opdef {
    const char op[6];                                   /* Mnemonic for op */
    int32   opmask;                                     /* Bits set on in opcode */
    int32   q;                                          /* Qbyte */
    int32   form;                                       /* Forms are:
                                                           0 - 1-byte hex operand
                                                           1 - 1-byte register addr, A-Addr
                                                           2 - A-addr,B-addr,Qbyte
                                                           3 - A-addr,Qbyte
                                                           4 - da,m,n
                                                           5 - da,m,n,cc
                                                           6 - da,m,n,A-addr
                                                           7 - 1-address implict Q
                                                           8 - 2-address implict Q */
    int32   group;                                      /* Group Code:
                                                           0 - Command Format (0xFx)
                                                           1 - 1-address A (0x<C,D,E>x) 
                                                           2 - 2-address (0x<0,1,2,4,5,6,8,9,A>x)
                                                           3 - 1-address B (0x<3,7,B>x) */
};
