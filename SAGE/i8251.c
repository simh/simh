/* sage_stddev.c: Standard devices for sage-II system

   Copyright (c) 2009-2010 Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   22-Jan-10    HV      Initial version
*/

#include "sim_defs.h"
#include "m68k_cpu.h"
#include "chip_defs.h"

static int i8251_bitmask[] = { 0x1f, 0x3f, 0x7f, 0xff };

/* Debug Flags */
DEBTAB i8251_dt[] = {
	{ "READ", DBG_UART_RD },
	{ "WRITE", DBG_UART_WR },
	{ "IRQ", DBG_UART_IRQ },
	{ NULL, 0 }
};

t_stat i8251_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask)
{
	int port = ioh->offset;
	I8251* chip = (I8251*)ioh->ctxt;
	if (rw==MEM_WRITE) {
		return chip->write ? chip->write(chip,port,*value) : i8251_write(chip,port,*value);
	} else {
		return chip->read ? chip->read(chip,port,value) : i8251_read(chip,port,value);
	}
}

t_stat i8251_write(I8251* chip,int port,uint32 value)
{
	int bits;

	if (port==0) { /* data port */
		chip->obuf = value & chip->bitmask;
		TRACE_PRINT1(DBG_UART_WR,"WR DATA = 0x%02x",chip->obuf);
		if (chip->init==3) { /* is fully initialized */
			if ((chip->mode & I8251_MODE_BAUD)==I8251_MODE_SYNC) {
				sim_printf("i8251: sync mode not implemented\n");
				return STOP_IMPL;
			}
			if (chip->cmd & I8251_CMD_TXEN) {
				/* transmit data */
				chip->status &= ~(I8251_ST_TXEMPTY|I8251_ST_TXRDY);
				sim_activate(chip->out,chip->out->wait);
			}
		}
		return SCPE_OK;
	} else { /* control port */
		switch (chip->init) {
		case 0: /* expect mode word */
			chip->mode = value; 
			TRACE_PRINT1(DBG_UART_WR,"WR MODE = 0x%02x",value);
			chip->init = (value & I8251_MODE_BAUD)==I8251_MODE_SYNC ? 1 : 3;
			bits = (chip->mode & I8251_AMODE_BITS) >> 2;
			chip->bitmask = i8251_bitmask[bits];
			break;
		case 1: /* expect sync1 */
			chip->sync1 = value;
			TRACE_PRINT1(DBG_UART_WR,"WR SYNC1 = 0x%02x",value);
			chip->init = 2;
			break;
		case 2: /* expect sync2 */
			chip->sync2 = value;
			TRACE_PRINT1(DBG_UART_WR,"WR SYNC2 = 0x%02x",value);
			chip->init = 3;
			break;
		case 3: /* expect cmd word */
			chip->cmd = value;
			TRACE_PRINT1(DBG_UART_WR,"WR CMD = 0x%02x",value);
			if (value & I8251_CMD_EH) {
				sim_printf("i8251: hunt mode not implemented\n");
				return STOP_IMPL;
			}
			if (value & I8251_CMD_IR)
				chip->init = 0;
			if (value & I8251_CMD_ER)
				chip->status &= ~(I8251_ST_FE|I8251_ST_OE|I8251_ST_PE);
			if (value & I8251_CMD_SBRK)
				sim_printf("i8251: BREAK sent\n");
			if (value & I8251_CMD_RXE) {
				sim_activate(chip->in,chip->in->wait);
			} else {
				if (!chip->oob) sim_cancel(chip->in);
			}
			if (value & I8251_CMD_TXEN) {
				if (!(chip->status & I8251_ST_TXEMPTY))
					sim_activate(chip->out,chip->out->wait);
				else {
					chip->status |= I8251_ST_TXRDY;
					if (chip->txint) chip->txint(chip);
				}
			} else {
				chip->status &= ~I8251_ST_TXRDY;
				sim_cancel(chip->out);
			}
		}
		return SCPE_OK;
	}
}

t_stat i8251_read(I8251* chip,int port,uint32* value)
{
	if (port==0) { /* data read */
		*value = chip->ibuf;
		chip->status &= ~I8251_ST_RXRDY; /* mark read buffer as empty */
		TRACE_PRINT1(DBG_UART_RD,"RD DATA = 0x%02x",*value);
	} else { /* status read */
		*value = chip->status & 0xff;
		TRACE_PRINT1(DBG_UART_RD,"RD STATUS = 0x%02x",*value);
	}
	return SCPE_OK;
}

t_stat i8251_reset(I8251* chip)
{
	chip->init = 0;
	chip->oob = FALSE;
	chip->crlf = 0;
	return SCPE_OK;
}
