/* 3b2_stddev.h: AT&T 3B2 miscellaneous system board devices.

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

#ifndef _3B2_STDDEV_H_
#define _3B2_STDDEV_H_

#include "3b2_defs.h"

/* NVRAM */
t_stat nvram_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvram_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvram_reset(DEVICE *dptr);
uint32 nvram_read(uint32 pa, size_t size);
t_stat nvram_attach(UNIT *uptr, CONST char *cptr);
t_stat nvram_detach(UNIT *uptr);
const char *nvram_description(DEVICE *dptr);
t_stat nvram_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
void nvram_write(uint32 pa, uint32 val, size_t size);

/* TOD */
typedef struct tod_data {
    int32 delta;       /* Delta between simulated time and real time (sec.) */
    uint8 tsec;        /* 1/10 seconds */
    uint8 unit_sec;    /* 1's column seconds */
    uint8 ten_sec;     /* 10's column seconds */
    uint8 unit_min;    /* 1's column minutes */
    uint8 ten_min;     /* 10's column  minutes */
    uint8 unit_hour;   /* 1's column hours */
    uint8 ten_hour;    /* 10's column hours */
    uint8 unit_day;    /* 1's column day of month */
    uint8 ten_day;     /* 10's column day of month */
    uint8 wday;        /* Day of week (0-6) */
    uint8 unit_mon;    /* 1's column month */
    uint8 ten_mon;     /* 10's column month */
    uint8 year;        /* 1, 2, 4, 8 shift register */
    uint8 pad[3];      /* Padding to 32 bytes */
} TOD_DATA;

void tod_resync();
void tod_update_delta();
t_stat tod_reset(DEVICE *dptr);
t_stat tod_attach(UNIT *uptr, CONST char *cptr);
t_stat tod_detach(UNIT *uptr);
const char *tod_description(DEVICE *dptr);
t_stat tod_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
uint32 tod_read(uint32 pa, size_t size);
void tod_write(uint32, uint32 val, size_t size);

#if defined(REV3)
/* Fault Register */
uint32 flt_read(uint32 pa, size_t size);
void flt_write(uint32 pa, uint32 val, size_t size);
#endif

#endif /* _3B2_STDDEV_H_ */
