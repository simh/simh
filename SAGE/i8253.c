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

/* Debug Flags */
DEBTAB i8253_dt[] = {
    { "READ",   DBG_TMR_RD },
    { "WRITE",  DBG_TMR_WR },
    { NULL,     0 }
};

static const char* rltype[] = { "latch","8bitL","8bitH", "16bit" };

t_stat i8253_write(I8253* chip, int addr, uint32 value)
{
    I8253CNTR* cntr;
    t_stat rc; 
    int num;

    if (addr==3) { /* mode reg */
        TRACE_PRINT(DBG_TMR_WR,(sim_deb,"WR MODE=%x (SC=%d RL=%s MODE=%d BCD=%d)",
                    value,(value>>6)&3,rltype[(value>>4)&3],(value>>1)&7,value&1));
        if (chip->ckmode && (rc=chip->ckmode(chip,value))!= SCPE_OK) return rc;
        num = (value & I8253_SCMASK)>>6;
        cntr = &chip->cntr[num];
        if ((value & I8253_RLMASK)==I8253_LATCH) {
            /* calculate current value of count */
            cntr->latch = cntr->count; /* latch it */
            cntr->state |= I8253_ST_LATCH;
        } else {
            cntr->mode = value;
            cntr->state = (value & I8253_RLMASK)==I8253_MSB ? I8253_ST_MSBNEXT : I8253_ST_LSBNEXT;
        }
    } else { /* write dividers */
        cntr = &chip->cntr[addr];
        switch (cntr->mode & I8253_RLMASK) {
        case I8253_MSB:
            TRACE_PRINT2(DBG_TMR_WR,"WR CNT=%d DIVMSB=%x",addr,value);
            cntr->divider = (cntr->divider & 0x00ff) | ((value<<8) | 0xff);
            cntr->state &= ~I8253_ST_LATCH;
            cntr->count = cntr->divider;
            break;
        case I8253_LSB:
            TRACE_PRINT2(DBG_TMR_WR,"WR CNT=%d DIVLSB=%x",addr,value);
            cntr->divider = (cntr->divider & 0xff00) | (value | 0xff);
            cntr->state &= ~I8253_ST_LATCH;
            cntr->count = cntr->divider;
            break;
        case I8253_BOTH:
            if (cntr->state & I8253_ST_MSBNEXT) {
                TRACE_PRINT2(DBG_TMR_WR,"WR CNT=%d DIV16MSB=%x",addr,value);
                cntr->divider = (cntr->divider & 0x00ff) | ((value & 0xff)<<8);
                cntr->state = I8253_ST_LSBNEXT; /* reset latch mode and MSB bit */
                cntr->count = cntr->divider;
            } else {
                TRACE_PRINT2(DBG_TMR_WR,"WR CNT=%d DIV16LSB=%x",addr,value);
                cntr->divider = (cntr->divider & 0xff00) | (value & 0xff);
                cntr->state = I8253_ST_MSBNEXT; /* reset latch mode and LSB bit */
            }
        default:
            break;
        }
        /* execute a registered callback before returning result */
        if (cntr->call && (rc=(*cntr->call)(chip,1,&value)) != SCPE_OK) return rc;
    }
    return SCPE_OK;
}

t_stat i8253_read(I8253* chip,int addr,uint32* value)
{
    t_stat rc;
    I8253CNTR* cntr = &chip->cntr[addr];
    int32 src = cntr->state & I8253_ST_LATCH ? cntr->latch : cntr->count;
    if (cntr->call && (rc=(*cntr->call)(chip,0,(uint32*)&src)) != SCPE_OK) return rc;

    switch (cntr->mode & I8253_RLMASK) {
    case I8253_MSB:
        src >>= 8;
        TRACE_PRINT2(DBG_TMR_RD,"RD CNT=%d CNTMSB=%x",addr,src&0xff);
        cntr->state &= ~I8253_ST_LATCH;
        break;
    case I8253_LSB:
        cntr->state &= ~I8253_ST_LATCH;
        TRACE_PRINT2(DBG_TMR_RD,"RD CNT=%d CNTLSB=%x",addr,src&0xff);
        break;
    case I8253_BOTH:
        if (cntr->state & I8253_ST_MSBNEXT) {
            src >>= 8; cntr->state = I8253_ST_LSBNEXT; /* reset latch mode and MSB bit */
            TRACE_PRINT2(DBG_TMR_RD,"RD CNT=%d CNT16MSB=%x",addr,src&0xff);
        } else {
            TRACE_PRINT2(DBG_TMR_RD,"RD CNT=%d CNT16LSB=%x",addr,src&0xff);
            cntr->state |= I8253_ST_MSBNEXT;        /* does not reset latch mode if set */
        }
        break;
    default:
        return SCPE_OK;
    }
    *value = src & 0xff;
    return SCPE_OK;
}

t_stat i8253_reset(I8253* chip)
{
    int i;
    for (i=0; i<3; i++) chip->cntr[i].state = 0;
    return SCPE_OK;
}

t_stat i8253_io(IOHANDLER* ioh,uint32* value,uint32 rw,uint32 mask)
{
    int port = ioh->offset;
    I8253* chip = (I8253*)ioh->ctxt;
    return rw==MEM_WRITE ? i8253_write(chip,port,*value) : i8253_read(chip,port,value);
}

