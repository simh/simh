/* vax_bi.h: VAXBI Standard Definitions

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   This file covers the VAXBI registers that are contained in the BIIC chip
   on each VAXBI node.
*/

#ifndef _VAXBI_DEFS_H_
#define _VAXBI_DEFS_H_        1

/* Register Offsets */

#define BI_DTYPE        0                               /* device type */
#define BI_CSR          1                               /* control/status */
#define BI_BER          2                               /* bus error */
#define BI_EICR         3                               /* error interrupt control */
#define BI_IDEST        4                               /* interrupt destination */
#define BI_IMSK         5                               /* IPINTR mask */
#define BI_FIDEST       6                               /* force IPINTR destination */
#define BI_ISRC         7                               /* IPINTR source */
#define BI_SA           8                               /* start address */
#define BI_EA           9                               /* end address */
#define BI_BCIC         10                              /* BCI control */
#define BI_WSTS         11                              /* write status */
#define BI_FICMD        12                              /* force IPINTR command */
#define BI_UIIC         16                              /* user interface interrupt control */
#define BI_GPR0         60                              /* general purpose register 0 */
#define BI_GPR1         61                              /* general purpose register 1 */
#define BI_GPR2         62                              /* general purpose register 2 */
#define BI_GPR3         63                              /* general purpose register 3 */
#define BI_SOSR         64                              /* slave only status register */
#define BI_RXCD         128                             /* receive console data register */

/* VAXBI device types */

#define DTYPE_MS820     0x0001                          /* MS820 MOS Memory */
#define DTYPE_DWBUA     0x0102                          /* DWBUA Unibus Adapter */
#define DTYPE_KA820     0x0105                          /* KA820 CPU */
#define DTYPE_CIBCA     0x0105                          /* CI Adapter */
#define DTYPE_KDB50     0x010E                          /* Disk Adapter */
#define DTYPE_DEBNA     0x410F                          /* Ethernet Adapter */

/* VAXBI control/status register */

#define BICSR_V_IR      24                              /* interface revision */
#define BICSR_M_IR      0xFF
#define BICSR_V_IF      16                              /* interface type */
#define BICSR_M_IF      0xFF
#define BICSR_HES       0x00004000                      /* hard error summary */
#define BICSR_SES       0x00004000                      /* soft error summary */
#define BICSR_INI       0x00002000                      /* initialise */
#define BICSR_BRK       0x00001000                      /* broke - NI */
#define BICSR_STS       0x00000800                      /* self test status */
#define BICSR_RST       0x00000400                      /* node reset */
#define BICSR_UWP       0x00000100                      /* unlock write pending */
#define BICSR_HIE       0x00000080                      /* hard error interrupt en */
#define BICSR_SIE       0x00000040                      /* soft error interrupt en */
#define BICSR_AC        0x00000030                      /* arbitration control */
#define BICSR_NODE      0x0000000F                      /* BI node ID */
#define BICSR_RW        (BICSR_HIE | BICSR_SIE | BICSR_AC)
#define BICSR_RD        0xFFFFFDFF

/* VAXBI bus error register */

#define BIBER_NMR       0x40000000                      /* no ACK to multi resp command */
#define BIBER_MTCE      0x20000000                      /* master transmit check error */
#define BIBER_CTE       0x10000000                      /* control transmit error */
#define BIBER_MPE       0x08000000                      /* master parity error */
#define BIBER_ISE       0x04000000                      /* interlock sequence error */
#define BIBER_TDF       0x02000000                      /* transmitter during fault */
#define BIBER_IVE       0x01000000                      /* ident vector error */
#define BIBER_CPE       0x00800000                      /* command parity error */
#define BIBER_SPE       0x00400000                      /* slave parity error */
#define BIBER_RDS       0x00200000                      /* read data substitute */
#define BIBER_RTO       0x00100000                      /* retry timeout */
#define BIBER_STO       0x00080000                      /* stall timeout */
#define BIBER_BTO       0x00040000                      /* bus timeout */
#define BIBER_NEX       0x00020000                      /* nonexistant address */
#define BIBER_ICE       0x00010000                      /* illegal confirmation error */
#define BIBER_UPE       0x00000008                      /* user parity enable */
#define BIBER_IPE       0x00000004                      /* ID parity error */
#define BIBER_CRD       0x00000002                      /* corrected read data */
#define BIBER_NPE       0x00000001                      /* null bus parity error */
#define BIBER_RD        0xFFFF000F
#define BIBER_W1C       0xFFFF0007

/* VAXBI error interrupt control register */

#define BIECR_ABO       0x01000000                      /* interrupt abort */
#define BIECR_COM       0x00800000                      /* interrupt complete */
#define BIECR_SNT       0x00200000                      /* interrupt sent */
#define BIECR_FRC       0x00100000                      /* force */
#define BIECR_LVL       0x000F0000                      /* interrupt level */
#define BIECR_V_LVL     16
#define BIECR_M_LVL     0xF
#define BIECR_VEC       0x00003FFC                      /* vector */
#define BIECR_RW        0x001F3FFC
#define BIECR_W1C       0x01A00000
#define BIECR_RD        (BIECR_RW | BIECR_W1C)

/* VAXBI interrupt destination register */

#define BIID_RW         0x0000FFFF
#define BIID_RD         BIID_RW

/* VAXBI user interface interrupt control register */

#define BIICR_ABO       0xF0000000                      /* interrupt abort */
#define BIICR_ITC       0x0F000000                      /* interrupt complete */
#define BIICR_SNT       0x00F00000                      /* interrupt sent */
#define BIICR_FRC       0x000F0000                      /* force */
#define BIICR_EXV       0x00008000                      /* external vector */
#define BIICR_VEC       0x00003FFC                      /* vector */
#define BIICR_RW        0x000FBFFC
#define BIICR_W1C       0xFFF00000
#define BIICR_RD        (BIICR_RW | BIICR_W1C)

typedef struct {
    uint32 dtype;
    uint32 csr;
    uint32 ber;
    uint32 eicr;
    uint32 idest;
    uint32 imsk;
    uint32 fidest;
    uint32 isrc;
    uint32 sa;
    uint32 ea;
    uint32 bcic;
    uint32 wsts;
    uint32 ficmd;
    uint32 uiic;
    uint32 gpr0;
    uint32 gpr1;
    uint32 gpr2;
    uint32 gpr3;
    uint32 sosr;
    uint32 rxcd;
} BIIC;

#endif
