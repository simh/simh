/* chip_i8259.c: system independent implementation of PIC chip

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
   03-Jun-10    HV      Repair POLL function (defective FDC interrupt handling in SAGEBIOS)
*/

#include "sim_defs.h"
#include "m68k_cpu.h"
#include "chip_defs.h"

/* Debug Flags */
DEBTAB i8259_dt[] = {
    { "READ",   DBG_PIC_RD },
    { "WRITE",  DBG_PIC_WR },
    { "IRQIN",  DBG_PIC_II },
    { "IRQOUT",  DBG_PIC_IO },
    { NULL,     0 }
};

static int32 priomask[] = { 0x0000,0x4000,0x6000,0x7000,0x7800,0x7c00,0x7e00,0x7f00 };

t_stat i8259_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask)
{
	int port = ioh->offset;
	I8259* chip = (I8259*)ioh->ctxt;
	if (rw==MEM_WRITE) {
		return chip->write ? chip->write(chip,port,*value) : i8259_write(chip,port,*value);
	} else {
		return chip->read ? chip->read(chip,port,value) : i8259_read(chip,port,value);
	}
}

t_stat i8259_write(I8259* chip,int addr,uint32 value)
{
	int i, bit;

#if 0
	TRACE_PRINT2(DBG_PIC_WR,"WR addr=%d data=0x%x",addr,value);
#endif
	if (addr==1) {
		switch (chip->state) {
		default:
		case 0: /* after reset */
			sim_printf("PIC: write addr=1 without initialization\n");
			return SCPE_IOERR;
		case 1: /* expect ICW2 */
			TRACE_PRINT2(DBG_PIC_WR,"WR ICW2: addr=%d data=0x%x",addr,value);
			chip->icw2 = value;
			if (chip->icw1 & I8259_ICW1_SNGL) {
				chip->state = (chip->icw1 & I8259_ICW1_IC4) ? 4 : 5;
			} else {
				/* attempt to program cascade mode */
				sim_printf("PIC: attempt to program chip for cascade mode - not wired for this!\n");
				chip->state = 0;
				return SCPE_IOERR;
			}
			break;
		case 4: /* expect ICW4 */
			TRACE_PRINT2(DBG_PIC_WR,"WR ICW4 addr=%d data=0x%x",addr,value);
			chip->icw4 = value;
			if (chip->icw4 & I8259_ICW4_AEOI) {
				sim_printf("PIC: attempt to program chip for AEOI mode - not wired for this!\n");
				return SCPE_IOERR;
			}
			if (chip->icw4 & I8259_ICW4_BUF) {
				sim_printf("PIC: attempt to program chip for buffered mode - not wired for this!\n");
				return SCPE_IOERR;
			}
			if (chip->icw4 & I8259_ICW4_SFNM) {
				sim_printf("PIC: attempt to program chip for spc nested mode - not wired for this!\n");
				return SCPE_IOERR;
			}
			chip->state = 5;
			break;
		case 5: /* ready to accept interrupt requests and ocw commands */
			/* ocw1 */
			TRACE_PRINT2(DBG_PIC_WR,"WR IMR addr=%d data=0x%x",addr,value);
			chip->imr = value;
			break;
		}
	} else {
		if (value & I8259_ICW1) { /* state initialization sequence */
			TRACE_PRINT2(DBG_PIC_WR,"WR ICW1 addr=%d data=0x%x",addr,value);
			chip->icw1 = value; 
			chip->state = 1;
			chip->rmode = 0;
			chip->prio = 7;
			if ((chip->icw1 & I8259_ICW1_IC4)==0) chip->icw4 = 0;
			
			return SCPE_OK;
		} else { /* ocw2 and ocw3 */
			if (value & I8259_OCW3) { /* ocw3 */
				TRACE_PRINT2(DBG_PIC_WR,"WR OCW3 addr=%d data=0x%x",addr,value);
				if (value & I8259_OCW3_ESMM) {
					sim_printf("PIC: ESMM not yet supported\n");
					return STOP_IMPL;
				}
				if (value & I8259_OCW3_POLL) {
					chip->rmode |= 2;
					return SCPE_OK;
				}
				if (value & I8259_OCW3_RR)
					chip->rmode = (value & I8259_OCW3_RIS) ? 1 /*isr*/ : 0; /* irr */
			} else { /* ocw2 */
				TRACE_PRINT2(DBG_PIC_WR,"WR OCW2 addr=%d data=0x%x",addr,value);
				switch (value & I8259_OCW2_MODE) {
				case 0xa0: /* rotate on nospecific eoi */
				case 0x20: /* nonspecific eoi */
					bit = 1 << (7 - chip->prio);
					for (i=0; i<7; i++) {
						if (chip->isr & bit) break;
						bit = bit << 1; if (bit==0x100) bit = 1;
					}
					chip->isr &= ~bit; break;
					if ((value & I8259_OCW2_MODE) == 0xa0) { 
						chip->prio = 7 - i + chip->prio; if (chip->prio>7) chip->prio -= 8;
					}
					break;
				case 0xe0: /* rotate on specific eoi */
					chip->prio = 7 - (value & 7) + chip->prio; if (chip->prio>7) chip->prio -= 8;
					/*fallthru*/
				case 0x60: /* specific eoi */
					bit = 1 << (value & 7);
					chip->isr = chip->isr & ~bit & 0xff;
					break;
				case 0x80: /* rotate in autoeoi (set) */
				case 0x00: /* rotate in autoeoi (clear) */
					sim_printf("PIC: AEOI not supported\n");
					return SCPE_IOERR;
				case 0xc0: /* set prio */
					chip->prio = value & 7;
					return SCPE_OK;
				case 0x40: /* nop */
					break;
				default:
					return SCPE_IERR;
				}
			}		
		}
	}
	return SCPE_OK;
}

t_stat i8259_read(I8259* chip,int addr, uint32* value)
{
	int i, bit, num;

	if (addr) {
		*value = chip->imr;
	} else {
		switch (chip->rmode) {
		case 0:
			TRACE_PRINT2(DBG_PIC_RD,"Read IRR addr=%d data=0x%x",addr,chip->irr);
			*value = chip->irr; break;
		case 1:
			TRACE_PRINT2(DBG_PIC_RD,"Read ISR addr=%d data=0x%x",addr,chip->irr);
			*value = chip->isr; break;
		case 2:
		case 3:
			TRACE_PRINT2(DBG_PIC_RD,"Read POLL addr=%d data=0x%x",addr,chip->irr);
			num = chip->prio;
			bit = 1 << chip->prio;
			for (i=0; i<8; i++,num--) {
				if (chip->isr & bit) {
					*value = 0x80 | (num & 7);
					TRACE_PRINT2(DBG_PIC_RD,"Read POLL addr=%d data=0x%x",addr,*value);
					return SCPE_OK;
				}
				bit >>= 1;
				if (bit==0) { bit = 0x80; num = 7; }
			}
			chip->rmode &= ~2;
		}
	}
#if 0
	TRACE_PRINT2(DBG_PIC_RD,"Read addr=%d data=0x%x",addr,*value);
#endif
	return SCPE_OK;
}

t_stat i8259_raiseint(I8259* chip,int level)
{
	int32 bit, isr, myprio;

	TRACE_PRINT1(DBG_PIC_II,"Request INT level=%d",level);
	
	if (chip->state != 5) return SCPE_OK; /* not yet initialized, ignore interrupts */
	bit = 1<<level;
	if (chip->imr & bit) return SCPE_OK; /* inhibited */
	chip->isr = (chip->isr | bit) & 0xff; /* request this interrupt level */
	
    /* bit7=prio7 => bitN = prioN	
	   bit7=prio6 => bitN = prioN-1
	   ...
	   bit7=prio0 => bitN = prioN-7
	*/	   
	isr = (chip->isr<<8) | chip->isr; /* simple rotation */
	isr = isr << (7-level); /* shift level bit into bit 15 */
	myprio = chip->prio - 7 + level; if (myprio < 0) myprio += 8;
	if (!(isr & priomask[myprio])) { /* higher interrupt is pending */
		if (chip->autoint) {
			TRACE_PRINT1(DBG_PIC_IO,"Raise AUTOINT level=%d",chip->intlevel);
			return m68k_raise_autoint(chip->intlevel);
		} else {
			TRACE_PRINT2(DBG_PIC_IO,"Raise VECTORINT level=%d vector=%x",chip->intlevel,chip->intvector);
			return m68k_raise_vectorint(chip->intlevel,chip->intvector);
		}
	}
	return SCPE_OK;
}

t_stat i8259_reset(I8259* chip)
{
	chip->autoint = TRUE;
	chip->intlevel = 1;
	chip->intvector = 0;
	chip->state = 0;
	chip->rmode = 0;
	chip->imr = 0;
	return SCPE_OK;
}
