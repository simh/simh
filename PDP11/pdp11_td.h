/* pdp11_td.h: TU58 cartridge controller API
  ------------------------------------------------------------------------------

   Copyright (c) 2015, Mark Pizzolato

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

  ------------------------------------------------------------------------------

  Modification history:

  20-Sep-15  MP   Initial Version

  ------------------------------------------------------------------------------
*/

#ifndef PDP11_TD_H
#define PDP11_TD_H

#include "sim_defs.h"

typedef struct CTLR CTLR;


t_stat td_connect_console_device (DEVICE *dptr,
                                  void (*rx_set_int) (int32 ctlr_num, t_bool val),
                                  void (*tx_set_int) (int32 ctlr_num, t_bool val));

t_stat td_rd_i_csr (CTLR *ctlr, int32 *data);
t_stat td_rd_i_buf (CTLR *ctlr, int32 *data);
t_stat td_rd_o_csr (CTLR *ctlr, int32 *data);
t_stat td_rd_o_buf (CTLR *ctlr, int32 *data);

t_stat td_wr_i_csr (CTLR *ctlr, int32 data);
t_stat td_wr_i_buf (CTLR *ctlr, int32 data);
t_stat td_wr_o_csr (CTLR *ctlr, int32 data);
t_stat td_wr_o_buf (CTLR *ctlr, int32 data);

/* Debug detail levels */

#define TDDEB_OPS       00001                           /* transactions */
#define TDDEB_IRD       00002                           /* input reg reads */
#define TDDEB_ORD       00004                           /* output reg reads */
#define TDDEB_RRD       00006                           /* reg reads */
#define TDDEB_IWR       00010                           /* input reg writes */
#define TDDEB_OWR       00020                           /* output reg writes */
#define TDDEB_RWR       00030                           /* reg writes */
#define TDDEB_TRC       00040                           /* trace */
#define TDDEB_INT       00100                           /* interrupts */
#define TDDEB_PKT       00200                           /* packet */
#define TDDEB_DAT       00400                           /* data */

static DEBTAB td_deb[] = {
    { "OPS", TDDEB_OPS, "transactions" },
    { "PKT", TDDEB_PKT, "packet" },
    { "RRD", TDDEB_RRD, "reg reads"},
    { "IRD", TDDEB_IRD, "input reg reads" },
    { "ORD", TDDEB_ORD, "output reg reads" },
    { "RWR", TDDEB_RWR, "reg writes" },
    { "IWR", TDDEB_IWR, "input reg writes" },
    { "OWR", TDDEB_OWR, "output reg writes" },
    { "INT", TDDEB_INT, "interrupts" },
    { "TRC", TDDEB_TRC, "trace" },
    { "DAT", TDDEB_DAT, "data" },
    { NULL, 0 }
    };

#endif /* _PDP11_TD_H */
