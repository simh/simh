/* vax_2681.h: 2681 DUART Simulator

   Copyright (c) 2011-2013, Matt Burke

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

   11-Jun-2013  MB      First version
*/

#include "sim_defs.h"

typedef t_stat (*put_char_t)(uint8);
typedef t_stat (*get_char_t)(uint8*);
typedef void   (*set_int_t)(uint32);

struct uart2681_port_t {
    put_char_t  put_char;
    get_char_t  get_char;
    uint32      sts;
    uint32      cmd;
    uint32      mode[2];
    uint32      mode_ptr;
    uint32      buf;
    };

struct uart2681_t {
    set_int_t   set_int;
    set_int_t   output_port;
    struct uart2681_port_t port[2];
    uint32      ists;
    uint32      imask;
    uint8       iport;
    uint8       ipcr;
    uint8       oport;
    uint8       opcr;
    uint8       acr;
    };

typedef struct uart2681_t UART2681;
    
void ua2681_wr (UART2681 *ctx, uint32 rg, uint32 data);
uint32 ua2681_rd (UART2681 *ctx, uint32 rg);
void ua2681_ip0_wr (UART2681 *ctx, uint32 set);
void ua2681_ip1_wr (UART2681 *ctx, uint32 set);
void ua2681_ip2_wr (UART2681 *ctx, uint32 set);
void ua2681_ip3_wr (UART2681 *ctx, uint32 set);
t_stat ua2681_svc (UART2681 *ctx);
t_stat ua2681_reset (UART2681 *ctx);
