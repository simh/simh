/*

   Copyright (c) 2015-2016, John Forecast

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
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

/* cdc1700_rtc.c: 10336-1 Real-time clock support
 *                Simh devices: rtc
 */
#include "cdc1700_defs.h"

#define HOLDREG         iod_writeR[0]           /* Holding register */
#define COUNTER         iod_readR[1]            /* Counter */

extern char INTprefix[];

extern t_stat checkReset(DEVICE *, uint8);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_equipment(UNIT *, int32, CONST char *, void *);

extern void RaiseExternalInterrupt(DEVICE *);
extern void rebuildPending(void);

extern uint16 Areg;

extern t_bool IOFWinitialized;

t_stat rtc_show_rate(FILE *, UNIT *, int32, CONST void *);
t_stat rtc_set_rate(UNIT *, int32, CONST char *, void *);

void RTCstate(char *, DEVICE *, IO_DEVICE *);
uint16 RTCraised(DEVICE *);
enum IOstatus RTCin(IO_DEVICE *, uint8);
enum IOstatus RTCout(IO_DEVICE *, uint8);

t_stat rtc_svc(UNIT *);
t_stat rtc_reset(DEVICE *);

t_stat rtc_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        10336-1 Real-Time Clock

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A
  (Bits 01-00)

       00               Load Register
       01               Director Function    Read Counter

  Operations:

  Director Function 1

    15  14                           7   6                   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   | X | X | X | X | X | X |   |   | X | X | X | X |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   |                           |   |                   |   |
     |   |                           |   |                   |   Clr Controller
     |   |                           |   |                   Ack. Interrupt
     |   |                           |   Stop Clock
     |   |                           Start Clock
     |   Disable Interrupt
     Enable Interrupt

  The counter and register values are unsigned 16-bit values.
*/

IO_DEVICE RTCdev = IODEV(NULL, "10336-1", 10336, 13, 0xFF, 0,
                         NULL, RTCin, RTCout, NULL, NULL,
                         NULL, NULL, RTCraised, NULL,
                         0x7F, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1,
                         MASK_REGISTER0, 0,
                         AQ_ONLY, 0, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_RTCstate    iod_private
#define iod_RTCraised   iod_private4

/*
 * Current state of the device.
 */
#define IODP_RTCIDLE    0x0000
#define IODP_RTCRUNNING 0x0001
#define IODP_RTCINTR    0x0002

/*
 * The RTC operates at a user determined frequency (via a jumper plug).
 * Basic time periods are:
 *
 *      1 uSec, 10 uSec, 100 uSec, 1 mSec, 10 mSec, 100 mSec and 1 second.
 *
 * We use CPU instruction execution as a proxy for generating these
 * frequencies. If we assume an average execution time of 1.25 uSec (1784-2
 * processor), each time period will be represented by the following
 * instruction counts:
 *
 *      1, 8, 80, 800, 8000, 80000, 800000
 */
#define RTC_1USEC       1
#define RTC_10USEC      8
#define RTC_100USEC     80
#define RTC_1MSEC       800
#define RTC_10MSEC      8000
#define RTC_100MSEC     80000
#define RTC_1SEC        800000

struct RTCtimebase {
  const char    *name;
  const char    *rate;
  int32         icount;
} timeBase[] = {
  { "1USEC", "1 uSec", RTC_1USEC },
  { "10USEC", "10 uSec", RTC_10USEC },
  { "100USEC", "100 uSec", RTC_100USEC },
  { "1MSEC", "1 mSec", RTC_1MSEC },
  { "10MSEC", "10 mSec", RTC_10MSEC },
  { "100MSEC", "100 mSec", RTC_100MSEC },
  { "1SEC", "1 Seccond", RTC_1SEC },
  { NULL }
};

/* RTC data structures

   rtc_dev      RTC device descriptor
   rtc_unit     RTC unit descriptor
   rtc_reg      RTC register list
   rtc_mod      RTC modifiers list
*/

UNIT rtc_unit = {
  UDATA(&rtc_svc, 0, 0), RTC_10MSEC
};

REG rtc_reg[] = {
  { HRDATAD(FUNCTION, RTCdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(COUNTER, RTCdev.COUNTER, 16, "Counter register") },
  { HRDATAD(HOLDING, RTCdev.HOLDREG, 16, "Hold register") },
  { NULL }
};

MTAB rtc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "10336-1 Real Time Clock" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, "RATE", "RATE={1usec|10usec|100usec|1msec|10msec|100msec|1second}",
    &rtc_set_rate, &rtc_show_rate, NULL, "Show timer tick interval" },
  { 0 }
};

DEBTAB rtc_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { NULL }
};

DEVICE rtc_dev = {
  "RTC", &rtc_unit, rtc_reg, rtc_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &rtc_reset,
  NULL, NULL, NULL,
  &RTCdev,
  DEV_DEBUG | DEV_DISABLE, 0, rtc_deb,
  NULL, NULL, &rtc_help, NULL, NULL, NULL
};

t_stat rtc_show_rate(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  struct RTCtimebase *tb = timeBase;

  while (tb->name != NULL) {
    if (tb->icount == rtc_unit.wait) {
      fprintf(st, "Timebase rate: %s", tb->rate);
      return SCPE_OK;
    }
    tb++;
  }
  return SCPE_IERR;
}

t_stat rtc_set_rate(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (cptr) {
    struct RTCtimebase *tb = timeBase;

    while (tb->name != NULL) {
      if (!strcmp(cptr, tb->name)) {
        rtc_unit.wait = tb->icount;
        return SCPE_OK;
      }
      tb++;
    }
  }
  return SCPE_IERR;
}

/*
 * Determine if the clock interrupt is asserted, returning the appropriate
 * interrupt bit or 0.
 */
uint16 RTCraised(DEVICE *dptr)
{
  IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

  return iod->iod_RTCraised ? iod->iod_interrupt : 0;
}

/* Unit service */

t_stat rtc_svc(UNIT *uptr)
{
  if (RTCdev.iod_RTCstate != IODP_RTCIDLE) {
    if ((RTCdev.iod_RTCstate & IODP_RTCRUNNING) != 0) {
      RTCdev.COUNTER++;

      if ((RTCdev.iod_RTCstate & IODP_RTCINTR) != 0) {
        if (RTCdev.COUNTER == RTCdev.HOLDREG) {
          RTCdev.COUNTER = 0;
          RTCdev.iod_RTCraised = TRUE;
          RaiseExternalInterrupt(&rtc_dev);
        }
      }
      sim_activate(&rtc_unit, rtc_unit.wait);
    }
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat rtc_reset(DEVICE * dptr)
{
  t_stat r;

  if (IOFWinitialized)
    if ((dptr->flags & DEV_DIS) == 0)
      if ((r = checkReset(dptr, RTCdev.iod_equip)) != SCPE_OK)
        return r;

  RTCdev.iod_RTCstate = IODP_RTCIDLE;
  RTCdev.iod_RTCraised = FALSE;

  return SCPE_OK;
}

/* Perform I/O */

enum IOstatus RTCin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The framework only passes IN operations for the data register.
   */
  return IO_REJECT;
}

enum IOstatus RTCout(IO_DEVICE *iod, uint8 reg)
{
  switch (reg) {
    case 0x00:
      RTCdev.HOLDREG = Areg;
      break;

    case 0x01:
      /*
       * Check for illegal bit combinations
       */
      if (((Areg & (IO_10336_ENA | IO_10336_DIS)) ==
           (IO_10336_ENA | IO_10336_DIS)) ||
          ((Areg & (IO_10336_START | IO_10336_STOP)) ==
           (IO_10336_START | IO_10336_STOP)))
        return IO_REJECT;

      if ((Areg & IO_DIR_CCONT) != 0) {
        sim_cancel(&rtc_unit);

        RTCdev.iod_RTCstate = IODP_RTCIDLE;
        RTCdev.iod_RTCraised = FALSE;
        rebuildPending();

        RTCdev.HOLDREG = 0;
        RTCdev.COUNTER = 0;
      }

      if ((Areg & IO_10336_STOP) != 0) {
        RTCdev.iod_RTCstate &= ~IODP_RTCRUNNING;
        sim_cancel(&rtc_unit);
      }

      if ((Areg & IO_10336_START) != 0) {
        RTCdev.COUNTER = 0;
        RTCdev.iod_RTCstate |= IODP_RTCRUNNING;
        sim_activate(&rtc_unit, rtc_unit.wait);
      }

      if ((Areg & IO_10336_ACK) != 0) {
        RTCdev.iod_RTCraised = FALSE;
        rebuildPending();
      }

      if ((Areg & IO_10336_DIS) != 0) {
        RTCdev.iod_RTCstate &= ~IODP_RTCINTR;
        RTCdev.iod_RTCraised = FALSE;
        rebuildPending();
      }

      if ((Areg & IO_10336_ENA) != 0)
        RTCdev.iod_RTCstate |= IODP_RTCINTR;
      break;
  }
  return IO_REPLY;
}

t_stat rtc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device 10336-1 Real Time Clock.\n"
    "1 Hardware Description\n"
    " The 10336-1 is a Real Time Clock which can generate periodic interrupts\n"
    " or measure elapsed time. The timer resolution is set via jumpers on the\n"
    " physical hardware. For the simulator, the resolution can be changed by:\n\n"
    "+sim> SET %D RATE=1usec\n"
    "+sim> SET %D RATE=10usec\n"
    "+sim> SET %D RATE=100usec\n"
    "+sim> SET %D RATE=1msec\n"
    "+sim> SET %D RATE=10msec\n"
    "+sim> SET %D RATE=100msec\n"
    "+sim> SET %D RATE=1second\n\n"
    "2 Equipment Address\n"
    " The %D device is set to equipment address 13. This address may be\n"
    " changed by:\n\n"
    "+sim> SET %D EQUIPMENT=hexValue\n\n"
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device. These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET commands\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}
