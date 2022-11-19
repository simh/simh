/* 3b2_stddev.h: Miscellaneous System Board Devices

   Copyright (c) 2017-2022, Seth J. Morabito

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

typedef struct tod_data {
    time_t time;     /* System time */

    uint8 ctrl;      /* Control register (Rev 3 only) */
    uint8 flags;     /* Data Changed & Interrutpt Flags (Rev 3 only) */
    uint8 clkset;    /* Clock / Setting register (Rev 3 only) */

    uint8 tsec;      /* 1/100th seconds, 00-99 */
    uint8 sec;       /* Seconds, 00-59 */
    uint8 min;       /* Minutes, 00-59 */
    uint8 hour;      /* Hours, 00-23 */
    uint8 day;       /* Days, 00-27, 28, 29, or 30 */
    uint8 mon;       /* Months, 00-11 */
    uint8 year;      /* Years, 00-99 (Rev 3 only) */
    uint8 wday;      /* Day of Week, 0-6 */
    uint8 lyear;     /* Years since last leap year */
} TOD_DATA;

#if defined(REV2)

#define TOD_TEST        0x00
#define TOD_TSEC        0x04
#define TOD_1SEC        0x08
#define TOD_10SEC       0x0c
#define TOD_1MIN        0x10
#define TOD_10MIN       0x14
#define TOD_1HOUR       0x18
#define TOD_10HOUR      0x1c
#define TOD_1DAY        0x20
#define TOD_10DAY       0x24
#define TOD_WDAY        0x28
#define TOD_1MON        0x2c
#define TOD_10MON       0x30
#define TOD_1YEAR       0x34
#define TOD_STARTSTOP   0x38
#define TOD_INT         0x3c

#else

#define TOD_FLAG_CHG    0x08
#define TOD_FLAG_IRQ    0x01

#define TOD_CTRL        0x00
#define TOD_TSEC        0x04
#define TOD_1SEC        0x08
#define TOD_10SEC       0x0c
#define TOD_1MIN        0x10
#define TOD_10MIN       0x14
#define TOD_1HOUR       0x18
#define TOD_10HOUR      0x1c
#define TOD_1DAY        0x20
#define TOD_10DAY       0x24
#define TOD_1MON        0x28
#define TOD_10MON       0x2c
#define TOD_1YEAR       0x30
#define TOD_10YEAR      0x34
#define TOD_WDAY        0x38
#define TOD_SET_INT     0x3c

#endif

void tod_update_delta();
t_stat tod_reset(DEVICE *dptr);
t_stat tod_attach(UNIT *uptr, CONST char *cptr);
t_stat tod_detach(UNIT *uptr);
t_stat tod_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *tod_description(DEVICE *dptr);
uint32 tod_read(uint32 pa, size_t size);
void tod_write(uint32, uint32 val, size_t size);

/* Global symbols */

extern int32 tmxr_poll;

#if defined(REV3)
/* Fault Register */

#define FLT_MSK         0xffffff00
#define MEM_EQP         0x4
#define MEM_4M          0x2
#define MEM_16M         0x3

uint32 flt_read(uint32 pa, size_t size);
void flt_write(uint32 pa, uint32 val, size_t size);
t_stat flt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *flt_description(DEVICE *dptr);

extern uint32 flt[2];

#endif /* defined(REV3) */

#endif /* _3B2_STDDEV_H_ */
