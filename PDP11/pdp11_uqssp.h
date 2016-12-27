/* pdp11_uqssp.h: Unibus/Qbus storage systems port definitions file

   Copyright (c) 2001-2008, Robert M Supnik
   Derived from work by Stephen F. Shirron

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

   30-Aug-02    RMS     Added TMSCP support
*/

#ifndef PDP11_UQSSP_H_
#define PDP11_UQSSP_H_ 0

/* IP register - initialization and polling

   read - controller polls command queue
   write - controller re-initializes
*/

/* SA register - status, address, and purge

   read - data and error information
   write - host startup information, purge complete
*/

#define SA_ER           0x8000                          /* error */
#define SA_S4           0x4000                          /* init step 4 */
#define SA_S3           0x2000                          /* init step 3 */
#define SA_S2           0x1000                          /* init step 2 */
#define SA_S1           0x0800                          /* init step 1 */

/* Init step 1, controller to host */

#define SA_S1C_NV       0x0400                          /* fixed vec NI */
#define SA_S1C_Q22      0x0200                          /* Q22 device */
#define SA_S1C_DI       0x0100                          /* ext diags */
#define SA_S1C_OD       0x0080                          /* odd addrs NI */
#define SA_S1C_MP       0x0040                          /* mapping */
#define SA_S1C_SM       0x0020                          /* spec fncs NI */
#define SA_S1C_CN       0x0010                          /* node name NI */

/* Init step 1, host to controller */

#define SA_S1H_VL       0x8000                          /* valid */
#define SA_S1H_WR       0x4000                          /* wrap mode */
#define SA_S1H_V_CQ     11                              /* cmd q len */
#define SA_S1H_M_CQ     0x7
#define SA_S1H_V_RQ     8                               /* resp q len */
#define SA_S1H_M_RQ     0x7
#define SA_S1H_IE       0x0080                          /* int enb */
#define SA_S1H_VEC      0x007F                          /* vector */
#define SA_S1H_CQ(x)    (1 << (((x) >> SA_S1H_V_CQ) & SA_S1H_M_CQ))
#define SA_S1H_RQ(x)    (1 << (((x) >> SA_S1H_V_RQ) & SA_S1H_M_RQ))

/* Init step 2, controller to host */

#define SA_S2C_PT       0x0000                          /* port type */
#define SA_S2C_V_EC     8                               /* info to echo */
#define SA_S2C_M_EC     0xFF
#define SA_S2C_EC(x)    (((x) >> SA_S2C_V_EC) & SA_S2C_M_EC)

/* Init step 2, host to controller */

#define SA_S2H_CLO      0xFFFE                          /* comm addr lo */
#define SA_S2H_PI       0x0001                          /* adp prg int */

/* Init step 3, controller to host */

#define SA_S3C_V_EC     0                               /* info to echo */
#define SA_S3C_M_EC     0xFF
#define SA_S3C_EC(x)    (((x) >> SA_S3C_V_EC) & SA_S3C_M_EC)

/* Init step 3, host to controller */

#define SA_S3H_PP       0x8000                          /* purge, poll test */
#define SA_S3H_CHI      0x7FFF                          /* comm addr hi */

/* Init step 4, controller to host */

#define SA_S4C_V_MOD    4                               /* adapter # */
#define SA_S4C_V_VER    0                               /* version # */

/* Init step 4, host to controller */

#define SA_S4H_CS       0x0400                          /* host scrpad NI */
#define SA_S4H_NN       0x0200                          /* snd node name NI */
#define SA_S4H_SF       0x0100                          /* spec fnc NI */
#define SA_S4H_LF       0x0002                          /* send last fail */
#define SA_S4H_GO       0x0001                          /* go */

/* Fatal error codes (generic through 32) */

#define PE_PRE          1                               /* packet read err */
#define PE_PWE          2                               /* packet write err */
#define PE_QRE          6                               /* queue read err */
#define PE_QWE          7                               /* queue write err */
#define PE_HAT          9                               /* host access tmo */
#define PE_ICI          14                              /* inv conn ident */
#define PE_PIE          20                              /* prot incompat */
#define PE_PPF          21                              /* prg/poll err */
#define PE_MRE          22                              /* map reg rd err */
#define PE_T11          475                             /* T11 err NI */
#define PE_SND          476                             /* SND err NI */
#define PE_RCV          477                             /* RCV err NI */
#define PE_NSR          478                             /* no such rsrc */

/* Comm region offsets */

#define SA_COMM_QQ      -8                              /* unused */
#define SA_COMM_PI      -6                              /* purge int */
#define SA_COMM_CI      -4                              /* cmd int */
#define SA_COMM_RI      -2                              /* resp int */
#define SA_COMM_MAX     ((4 << SA_S1H_M_CQ) + (4 << SA_S1H_M_RQ) - SA_COMM_QQ)

/* Command/response rings */

struct uq_ring {
    int32               ioff;                           /* intr offset */
    uint32              ba;                             /* base addr */
    uint32              lnt;                            /* size in bytes */
    uint32              idx;                            /* current index */
    };

/* Ring descriptor entry */

#define UQ_DESC_OWN     0x80000000                      /* ownership */
#define UQ_DESC_F       0x40000000                      /* flag */
#define UQ_ADDR         0x003FFFFE                      /* addr, word aligned */

/* Packet header */

#define UQ_HDR_OFF      -4                              /* offset */

#define UQ_HLNT         0                               /* length */
#define UQ_HCTC         1                               /* credits, type, CID */

#define UQ_HCTC_V_CR    0                               /* credits */
#define UQ_HCTC_M_CR    0xF
#define UQ_HCTC_V_TYP   4                               /* type */
#define UQ_HCTC_M_TYP   0xF
#define  UQ_TYP_SEQ     0                               /* sequential */
#define  UQ_TYP_DAT     1                               /* datagram */
#define UQ_HCTC_V_CID   8                               /* conn ID */
#define UQ_HCTC_M_CID   0xFF
#define  UQ_CID_MSCP    0                               /* MSCP */
#define  UQ_CID_TMSCP   1                               /* TMSCP */
#define  UQ_CID_DUP     2                               /* DUP */
#define  UQ_CID_DIAG    0xFF                            /* diagnostic */

#endif
