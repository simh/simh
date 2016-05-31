/* vax_2681.c: 2681 DUART Simulator

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

   09-Aug-2013  MB      Fixed reporting of status bits in receiver buffer
   11-Jun-2013  MB      First version
*/

#include "vax_2681.h"

#define CMD_ERX         0x0001                          /* Enable receiver */
#define CMD_DRX         0x0002                          /* Disable receiver */
#define CMD_ETX         0x0004                          /* Enable transmitter */
#define CMD_DTX         0x0008                          /* Disable transmitter */
#define CMD_V_CMD       4                               /* Command */
#define CMD_M_CMD       0x7

#define STS_RXR         0x0001                          /* Receiver ready */
#define STS_FFL         0x0002                          /* FIFO full */
#define STS_TXR         0x0004                          /* Transmitter ready */
#define STS_TXE         0x0008                          /* Transmitter empty */
#define STS_OER         0x0010                          /* Overrun error */
#define STS_PER         0x0020                          /* Parity error */
#define STS_FER         0x0040                          /* Framing error */
#define STS_RXB         0x0080                          /* Received break */

#define ISTS_TAI        0x0001                          /* Transmitter ready A */
#define ISTS_RAI        0x0002                          /* Receiver ready A */
#define ISTS_CBA        0x0004                          /* Change in break A */
#define ISTS_CRI        0x0008                          /* Counter ready */
#define ISTS_TBI        0x0010                          /* Transmitter ready B */
#define ISTS_RBI        0x0020                          /* Receiver ready B */
#define ISTS_CBB        0x0040                          /* Change in break B */
#define ISTS_IPC        0x0080                          /* Interrupt port change */

#define MODE_V_CHM      6                               /* Channel mode */
#define MODE_M_CHM      0x3

#define PORT_A          0
#define PORT_B          1

void ua2681_update_rxi (UART2681 *ctx);
void ua2681_update_txi (UART2681 *ctx);
uint8 ua2681_oport (UART2681 *ctx);


void ua2681_wr (UART2681 *ctx, uint32 rg, uint32 data)
{
uint32 mp;

switch (rg) {

    case 0:                                             /* mode 1A,2A */
        mp = ctx->port[PORT_A].mode_ptr;
        ctx->port[PORT_A].mode[mp++] = data & 0xFF;
        if (mp >= 2) mp = 0;
        ctx->port[PORT_A].mode_ptr = mp;
        break;

    case 1:                                             /* status/clock A */
        /* Used to set baud rate - NI */
        break;

    case 2:                                             /* command A */
        if (data & CMD_ETX)                             /* enable transmitter */
            ctx->port[PORT_A].cmd |= CMD_ETX;
        else if (data & CMD_DTX)                        /* disable transmitter */
            ctx->port[PORT_A].cmd &= ~CMD_ETX;

        if (data & CMD_ERX)                             /* enable receiver */
            ctx->port[PORT_A].cmd |= CMD_ERX;
        else if (data & CMD_DRX)                        /* disable receiver */
            ctx->port[PORT_A].cmd &= ~CMD_ERX;

        switch ((data >> CMD_V_CMD) & CMD_M_CMD) {
            case 1:
                ctx->port[PORT_A].mode_ptr = 0;
                break;

            case 2:
                ctx->port[PORT_A].cmd &= ~CMD_ERX;
                ctx->port[PORT_A].sts &= ~STS_RXR;
                break;

            case 3:
                ctx->port[PORT_A].sts &= ~STS_TXR;
                break;

            case 4:
                ctx->port[PORT_A].sts &= ~(STS_FER | STS_PER | STS_OER);
                break;
                }
        ua2681_update_rxi (ctx);
        ua2681_update_txi (ctx);
        break;

    case 3:                                             /* tx/rx buf A */
        if (((ctx->port[PORT_A].mode[1] >> MODE_V_CHM) & MODE_M_CHM) == 0x2) {   /* Maint */
            ctx->port[PORT_A].buf = data & 0xFF;
            ctx->port[PORT_A].sts |= STS_RXR;
            ctx->ists |= ISTS_RAI;
            }
        else {
            if (ctx->port[PORT_A].put_char != NULL)
                ctx->port[PORT_A].put_char ((uint8)data);
            }
        ua2681_update_txi (ctx);
        break;

    case 4:                                             /* auxiliary control */
        ctx->acr = data & 0xFF;
        break;

    case 5:                                             /* interrupt status/mask */
        ctx->imask = data & 0xFF;
        break;

    case 8:                                             /* mode 1B,2B */
        mp = ctx->port[PORT_B].mode_ptr;
        ctx->port[PORT_B].mode[mp++] = data & 0xFF;
        if (mp >= 2) mp = 0;
        ctx->port[PORT_B].mode_ptr = mp;
        break;

    case 9:                                             /* status/clock B */
        /* Used to set baud rate - NI */
        break;

    case 10:                                            /* command B */
        if (data & CMD_ETX)                             /* enable transmitter */
            ctx->port[PORT_B].cmd |= CMD_ETX;
        else if (data & CMD_DTX)                        /* disable transmitter */
            ctx->port[PORT_B].cmd &= ~CMD_ETX;

        if (data & CMD_ERX)                             /* enable receiver */
            ctx->port[PORT_B].cmd |= CMD_ERX;
        else if (data & CMD_DRX)                        /* disable receiver */
            ctx->port[PORT_B].cmd &= ~CMD_ERX;

        switch ((data >> CMD_V_CMD) & CMD_M_CMD) {
            case 1:                                     /* reset mode pointer */
                ctx->port[PORT_B].mode_ptr = 0;
                break;

            case 2:
                ctx->port[PORT_B].cmd &= ~CMD_ERX;
                ctx->port[PORT_B].sts &= ~STS_RXR;
                break;

            case 3:
                ctx->port[PORT_B].sts &= ~STS_TXR;
                break;

            case 4:
                ctx->port[PORT_B].sts &= ~(STS_FER | STS_PER | STS_OER);
                break;
                }
        ua2681_update_rxi (ctx);
        ua2681_update_txi (ctx);
        break;

    case 11:                                            /* tx/rx buf B (mouse) */
        if (((ctx->port[PORT_B].mode[1] >> MODE_V_CHM) & MODE_M_CHM) == 0x2) {   /* Maint */
            ctx->port[PORT_B].buf = data & 0xFF;
            ctx->port[PORT_B].sts |= STS_RXR;
            ctx->ists |= ISTS_RBI;
            }
        else {
            if (ctx->port[PORT_B].put_char != NULL)
                ctx->port[PORT_B].put_char ((uint8)data);
            }
        ua2681_update_txi (ctx);
        break;

    case 13:
        ctx->opcr = data;
        break;

    case 14:
        ctx->oport |= data;
        ctx->output_port (ua2681_oport (ctx));
        break;

    case 15:
        ctx->oport &= ~data;
        ctx->output_port (ua2681_oport (ctx));
        break;

    default:                                            /* NI */
        break;
    }
}

uint32 ua2681_rd(UART2681 *ctx, uint32 rg)
{
uint32 data;

switch (rg) {

    case 0:                                             /* mode 1A,2A */
        data = ctx->port[PORT_A].mode[ctx->port[PORT_A].mode_ptr];
        ctx->port[PORT_A].mode_ptr++;
        if (ctx->port[PORT_A].mode_ptr >= 2) ctx->port[PORT_A].mode_ptr = 0;
        break;

    case 1:                                             /* status/clock A */
        data = ctx->port[PORT_A].sts;
        break;

    case 3:                                             /* tx/rx buf A */
        data = ctx->port[PORT_A].buf | (ctx->port[PORT_A].sts << 8);
        ctx->port[PORT_A].sts &= ~STS_RXR;
        ctx->ists &= ~ISTS_RAI;
        ua2681_update_rxi (ctx);
        break;

    case 4:                                             /* input port change */
        data = ctx->ipcr;
        ctx->ipcr &= 0x0f;
        ctx->ists &= ~ISTS_IPC;
        ua2681_update_rxi (ctx);
        break;

    case 5:                                             /* interrupt status/mask */
        data = ctx->ists;
        break;

    case 8:                                             /* mode 1B,2B */
        data = ctx->port[PORT_B].mode[ctx->port[PORT_B].mode_ptr];
        ctx->port[PORT_B].mode_ptr++;
        if (ctx->port[PORT_B].mode_ptr >= 2) ctx->port[PORT_B].mode_ptr = 0;
        break;

    case 9:                                             /* status/clock B */
        data = ctx->port[PORT_B].sts;
        break;

    case 11:                                            /* tx/rx buf B */
        data = ctx->port[PORT_B].buf | (ctx->port[PORT_B].sts << 8);
        ctx->port[PORT_B].sts &= ~STS_RXR;
        ctx->ists &= ~ISTS_RBI;
        ua2681_update_rxi (ctx);
        break;

    case 13:                                            /* input port */
        data = ctx->iport;
        break;

    default:                                            /* NI */
        data = 0;
        break;
    }

return data;
}

void ua2681_update_txi (UART2681 *ctx)
{
if (ctx->port[PORT_A].cmd & CMD_ETX) {                  /* Transmitter A enabled? */
    ctx->port[PORT_A].sts |= STS_TXR;                   /* ready */
    ctx->port[PORT_A].sts |= STS_TXE;                   /* empty */
    ctx->ists |= ISTS_TAI;                              /* set int */
    }
else {
    ctx->port[PORT_A].sts &= ~STS_TXR;                  /* clear ready */
    ctx->port[PORT_A].sts &= ~STS_TXE;                  /* clear empty */
    ctx->ists &= ~ISTS_TAI;                             /* clear int */
    }
if (ctx->port[PORT_B].cmd & CMD_ETX) {                  /* Transmitter B enabled? */
    ctx->port[PORT_B].sts |= STS_TXR;                   /* ready */
    ctx->port[PORT_B].sts |= STS_TXE;                   /* empty */
    ctx->ists |= ISTS_TBI;                              /* set int */
    }
else {
    ctx->port[PORT_B].sts &= ~STS_TXR;                  /* clear ready */
    ctx->port[PORT_B].sts &= ~STS_TXE;                  /* clear empty */
    ctx->ists &= ~ISTS_TBI;                             /* clear int */
    }

if ((ctx->ists & ctx->imask) > 0)                       /* unmasked ints? */
     ctx->set_int (1);
else ctx->set_int (0);

if (ctx->opcr & 0xc0)
    ctx->output_port (ua2681_oport (ctx));

}

void ua2681_update_rxi (UART2681 *ctx)
{
uint8 c;
t_stat r;

if (ctx->port[PORT_A].cmd & CMD_ERX) {
    if (((ctx->port[PORT_A].sts & STS_RXR) == 0) &&
       (ctx->port[PORT_A].get_char != NULL)) {
        r = ctx->port[PORT_A].get_char (&c);
        if (r == SCPE_OK) {
            ctx->port[PORT_A].buf = c;
            ctx->port[PORT_A].sts |= STS_RXR;
            ctx->ists |= ISTS_RAI;
            }
        else {
            ctx->port[PORT_A].sts &= ~STS_RXR;
            ctx->ists &= ~ISTS_RAI;
            }
        }
    }
else {
    ctx->port[PORT_A].sts &= ~STS_RXR;
    ctx->ists &= ~ISTS_RAI;
    }

if (ctx->port[PORT_B].cmd & CMD_ERX) {
    if (((ctx->port[PORT_B].sts & STS_RXR) == 0) &&
       (ctx->port[PORT_B].get_char != NULL)) {
        r = ctx->port[PORT_B].get_char (&c);
        if (r == SCPE_OK) {
            ctx->port[PORT_B].buf = c;
            ctx->port[PORT_B].sts |= STS_RXR;
            ctx->ists |= ISTS_RBI;
            }
        else {
            ctx->port[PORT_B].sts &= ~STS_RXR;
            ctx->ists &= ~ISTS_RBI;
            }
        }
    }
else {
    ctx->port[PORT_B].sts &= ~STS_RXR;
    ctx->ists &= ~ISTS_RBI;
    }

if ((ctx->ists & ctx->imask) > 0)
     ctx->set_int (1);
else ctx->set_int (0);

if (ctx->opcr & 0x30)
    ctx->output_port (ua2681_oport (ctx));

}

uint8 ua2681_oport (UART2681 *ctx)
{
uint8 t = ctx->oport;

if (ctx->opcr & 0x80) {
    t &= ~0x80;
    if (ctx->ists & ISTS_TBI) t |= 0x80;
    }
if (ctx->opcr & 0x40) {
    t &= ~0x40;
    if (ctx->ists & ISTS_TAI) t |= 0x40;
    }
if (ctx->opcr & 0x20) {
    t &= ~0x20;
    if (ctx->ists & ISTS_RBI) t |= 0x20;
    }
if (ctx->opcr & 0x10) {
    t &= ~0x10;
    if (ctx->ists & ISTS_RAI) t |= 0x10;
    }

return t ^ 0xff;
}

/* input ports */

void ua2681_ip0_wr (UART2681 *ctx, uint32 set)
{
uint8 new_val = (ctx->iport & ~1) | (set ? 1 : 0);

if (new_val != ctx->iport) {
    ctx->ipcr &= ~0x0f;
    ctx->ipcr |= (new_val & 0x0f);
    ctx->ipcr |= 0x10;
    if (ctx->acr & 0x01)
    	ctx->ists |= ISTS_IPC;
    }

ctx->iport = new_val;
}

void ua2681_ip1_wr (UART2681 *ctx, uint32 set)
{
uint8 new_val = (ctx->iport & ~2) | (set ? 2 : 0);

if (new_val != ctx->iport) {
    ctx->ipcr &= ~0x0f;
    ctx->ipcr |= (new_val & 0x0f);
    ctx->ipcr |= 0x20;
    if (ctx->acr & 0x02)
    	ctx->ists |= ISTS_IPC;
    }

ctx->iport = new_val;
}

void ua2681_ip2_wr (UART2681 *ctx, uint32 set)
{
uint8 new_val = (ctx->iport & ~4) | (set ? 4 : 0);

if (new_val != ctx->iport) {
    ctx->ipcr &= ~0x0f;
    ctx->ipcr |= (new_val & 0x0f);
    ctx->ipcr |= 0x40;
    if (ctx->acr & 0x04)
    	ctx->ists |= ISTS_IPC;
    }

ctx->iport = new_val;
}

void ua2681_ip3_wr (UART2681 *ctx, uint32 set)
{
uint8 new_val = (ctx->iport & ~8) | (set ? 8 : 0);

if (new_val != ctx->iport) {
    ctx->ipcr &= ~0x0f;
    ctx->ipcr |= (new_val & 0x0f);
    ctx->ipcr |= 0x80;
    if (ctx->acr & 0x08)
    	ctx->ists |= ISTS_IPC;
    }

ctx->iport = new_val;
}

/**/

t_stat ua2681_svc (UART2681 *ctx)
{
ua2681_update_rxi (ctx);
return SCPE_OK;
}

t_stat ua2681_reset (UART2681 *ctx)
{
ctx->ists = 0;
ctx->imask = 0;
ctx->iport = ctx->ipcr = 0;
ctx->oport = 0;

ctx->port[PORT_A].sts = 0;
ctx->port[PORT_A].mode[0] = 0;
ctx->port[PORT_A].mode[1] = 0;
ctx->port[PORT_A].mode_ptr = 0;
ctx->port[PORT_A].buf = 0;

ctx->port[PORT_B].sts = 0;
ctx->port[PORT_B].mode[0] = 0;
ctx->port[PORT_B].mode[1] = 0;
ctx->port[PORT_B].mode_ptr = 0;
ctx->port[PORT_B].buf = 0;
return SCPE_OK;
}
