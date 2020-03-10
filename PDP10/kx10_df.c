/* kx10_df.c: DF10 common routines.

   Copyright (c) 2015-2020, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"


/* Set an IRQ for a DF10 device */
void df10_setirq(struct df10 *df) {
      df->status |= PI_ENABLE;
      set_interrupt(df->devnum, df->status);
}

/* Generate the DF10 complete word */
void df10_writecw(struct df10 *df) {
      uint64  wrd;
      df->status |= 1 << df->ccw_comp;
      if (df->wcr != 0)
          df->cda++;
      wrd = ((uint64)(df->ccw & WMASK) << CSHIFT) | ((uint64)(df->cda) & AMASK);
      (void)Mem_write_word(df->cia|1, &wrd, 0);
}

/* Finish off a DF10 transfer */
void df10_finish_op(struct df10 *df, int flags) {
      df->status &= ~BUSY;
      df->status |= flags;
      df10_writecw(df);
      df10_setirq(df);
}

/* Setup for a DF10 transfer */
void df10_setup(struct df10 *df, uint32 addr) {
      df->cia = addr & ICWA;
      df->ccw = df->cia;
      df->wcr = 0;
      df->status |= BUSY;
      df->status &= ~(1 << df->ccw_comp);
}

/* Fetch the next IO control word */
int df10_fetch(struct df10 *df) {
      uint64 data;
      if (Mem_read_word(df->ccw, &data, 0)) {
           df10_finish_op(df, df->nxmerr);
           return 0;
      }
      while((data & (WMASK << CSHIFT)) == 0) {
          if ((data & AMASK) == 0 || (uint32)(data & AMASK) == df->ccw) {
                df10_finish_op(df,0);
                return 0;
          }
          df->ccw = (uint32)(data & AMASK);
          if (Mem_read_word(df->ccw, &data, 0)) {
                df10_finish_op(df, 1<<df->nxmerr);
                return 0;
          }
      }
#if KA & ITS
      if (cpu_unit[0].flags & UNIT_ITSPAGE) {
          df->wcr = (uint32)((data >> CSHIFT) & 0077777) | 0700000;
          df->cda = (uint32)(data & RMASK);
          df->cda |= (uint32)((data >> 15) & 00000007000000LL) ^ 07000000;
          df->ccw = (uint32)((df->ccw + 1) & AMASK);
          return 1;
      }
#endif
      df->wcr = (uint32)((data >> CSHIFT) & WMASK);
      df->cda = (uint32)(data & AMASK);
      df->ccw = (uint32)((df->ccw + 1) & AMASK);
      return 1;
}

/* Read next word */
int df10_read(struct df10 *df) {
     uint64 data;
     if (df->wcr == 0) {
         if (!df10_fetch(df))
             return 0;
     }
     df->wcr = (uint32)((df->wcr + 1) & WMASK);
     if (df->cda != 0) {
        if (df->cda > MEMSIZE) {
            df10_finish_op(df, 1<<df->nxmerr);
            return 0;
        }
#if KA & ITS
        if (cpu_unit[0].flags & UNIT_ITSPAGE)
            df->cda = (uint32)((df->cda + 1) & RMASK) | (df->cda & 07000000);
        else
#endif
        df->cda = (uint32)((df->cda + 1) & AMASK);
        if (Mem_read_word(df->cda, &data, 0)) {
            df10_finish_op(df, 1<<df->nxmerr);
            return 0;
        }
     } else {
        data = 0;
     }
     df->buf = data;
     if (df->wcr == 0) {
        return df10_fetch(df);
     }
     return 1;
}

/* Write next word */
int df10_write(struct df10 *df) {
     if (df->wcr == 0) {
         if (!df10_fetch(df))
             return 0;
     }
     df->wcr = (uint32)((df->wcr + 1) & WMASK);
     if (df->cda != 0) {
        if (df->cda > MEMSIZE) {
           df10_finish_op(df, 1<<df->nxmerr);
           return 0;
        }
#if KA & ITS
        if (cpu_unit[0].flags & UNIT_ITSPAGE)
            df->cda = (uint32)((df->cda + 1) & RMASK) | (df->cda & 07000000);
        else
#endif
        df->cda = (uint32)((df->cda + 1) & AMASK);
        if (Mem_write_word(df->cda, &df->buf, 0)) {
           df10_finish_op(df, 1<<df->nxmerr);
           return 0;
        }
     }
     if (df->wcr == 0) {
        return df10_fetch(df);
     }
     return 1;
}
