/* i8255.c: helper for 8255 implementation

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

   14-Mar-10    HV      Initial version
*/

#include "sim_defs.h"
#include "m68k_cpu.h"
#include "chip_defs.h"

static t_stat i8255_error(const char* err)
{
	sim_printf("I8255: Missing method '%s'\n",err);
	return STOP_IMPL;
}

t_stat i8255_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask)
{
	int port = ioh->offset;
	I8255* chip = (I8255*)ioh->ctxt;
	if (rw==MEM_WRITE) {
		return chip->write ? chip->write(chip,port,*value) : i8255_error("write");
	} else {
		return chip->read ? chip->read(chip,port,value) : i8255_error("read");
	}
}

t_stat i8255_read(I8255* chip,int port,uint32* data)
{
	t_stat rc;
	switch (port) {
	case 0:
		if (chip->calla && (rc=(*chip->calla)(chip,0)) != SCPE_OK) return rc;
		*data = chip->porta;
		return SCPE_OK;
	case 1:
		if (chip->callb && (rc=(*chip->callb)(chip,0)) != SCPE_OK) return rc;
		*data = chip->portb;
		return SCPE_OK;
	case 2:
		if (chip->callc && (rc=(*chip->callc)(chip,0)) != SCPE_OK) return rc;
		*data = chip->portc;
		return SCPE_OK;
	case 3:
		*data = 0xff; /* undefined */
		return SCPE_OK;
	default:
		return SCPE_IERR;
	}
}

t_stat i8255_write(I8255* chip,int port,uint32 data)
{
	t_stat rc;
	uint32 bit;
	switch(port) {
	case 0: /*port a*/
		chip->last_porta = chip->porta;
		chip->porta = data;
		return chip->calla ? (*chip->calla)(chip,1) : SCPE_OK;
	case 1: /*port b*/
		chip->last_portb = chip->portb;
		return chip->callb ? (*chip->callb)(chip,1) : SCPE_OK;
	case 2:
		chip->last_portc = chip->portc;
		chip->portc = data & 0xff;
		return chip->callc ? (*chip->callc)(chip,1) : SCPE_OK;
	case 3:
		if (data & 0x80) { /* mode set mode */
			if (chip->ckmode && (rc=chip->ckmode(chip,data))) return rc;
			chip->ctrl = data & 0x7f;
			return SCPE_OK;
		} else { /* bit set mode */
			chip->last_portc = chip->portc;
			bit = 1 << ((data & 0x0e)>>1);
			TRACE_PRINT2(DBG_PP_WRC,"WR PORTC %s bit=%x",data&1 ? "SET": "CLR",bit);
			if (data & 1) chip->portc |= bit; else chip->portc &= ~bit;
			chip->portc &= 0xff;
			return chip->callc ? (*chip->callc)(chip,1) : SCPE_OK;
		}
	default:
		return SCPE_IERR;
	}
}
