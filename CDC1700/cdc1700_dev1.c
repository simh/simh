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

/* cdc1700_dev1.c: equipment number 1 I/O device support
 *                 Simh devices: tti, tto, ptr, ptp, cdr
 */

#include "cdc1700_defs.h"

extern char INTprefix[];

extern void fw_IOunderwayData(IO_DEVICE *, uint16);
extern void fw_IOcompleteData(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOintr(t_bool, DEVICE *, IO_DEVICE *, uint16, uint16, uint16, const char *);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_setForced(IO_DEVICE *, uint16);
extern void fw_clearForced(IO_DEVICE *, uint16);

extern void rebuildPending(void);

extern void RaiseExternalInterrupt(DEVICE *);

extern t_bool doDirectorFunc(DEVICE *, t_bool);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_stoponrej(UNIT *, int32, CONST char *, void *);
extern t_stat clr_stoponrej(UNIT *, int32, CONST char *, void *);

extern t_stat set_protected(UNIT *, int32, CONST char *, void *);
extern t_stat clear_protected(UNIT *, int32, CONST char *, void *);

extern uint16 Areg, IOAreg;

extern t_bool IOFWinitialized;

t_stat tti_svc(UNIT *);
t_stat tto_svc(UNIT *);
t_stat tti_reset(DEVICE *);
t_stat tto_reset(DEVICE *);

void TTIstate(const char *, DEVICE *, IO_DEVICE *);
void TTOstate(const char *, DEVICE *, IO_DEVICE *);
void TTstate(const char *, DEVICE *, IO_DEVICE *);
uint16 TTrebuild(void);
t_bool TTreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus TTin(IO_DEVICE *, uint8);
enum IOstatus TTout(IO_DEVICE *, uint8);

t_stat tt_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1711-A/B, 1712-A Teletypewriter

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A

      0090              Write                Read
      0091              Director Function    Director Status


  Operations:

  Director Function

    15                  10   9   8   7       5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X |   |   | X | X | X |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             |   |               |   |   |   |   |
                             |   |               |   |   |   |   Clr Controller
                             |   |               |   |   |   Clr Interrupts
                             |   |               |   |   Data Interrupt Req.
                             |   |               |   Interrupt on EOP
                             |   |               Interrupt on Alarm
                             |   Select Write Mode
                             Select Read Mode

  Status Response:

  Director Status

    15          12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X |   |   |   | X | X |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                     |   |   |           |   |   |   |   |   |   |
                     |   |   |           |   |   |   |   |   |   Ready
                     |   |   |           |   |   |   |   |   Busy
                     |   |   |           |   |   |   |   Interrupt
                     |   |   |           |   |   |   Data
                     |   |   |           |   |   End of Operation
                     |   |   |           |   Alarm
                     |   |   |           Lost Data
                     |   |   Read Mode
                     |   Motor On
                     Manual Interrupt

  The director status bits are distributed across the 3 IO_DEVICE data
  structures (TTdev, TTIdev and TTOdev). Global status bits will be held
  in TTdev and per-device status bits in the appropriate input or output
  device:

  IO_ST_READY           TTdev
  IO_ST_BUSY            TTdev
  IO_ST_INT             TTIdev/TTOdev
  IO_ST_DATA            TTIdev/TTOdev
  IO_ST_EOP             TTIdev/TTOdev (equivalent to "Not Busy")
  IO_ST_ALARM           TTIdev/TTOdev
  IO_ST_LOST            TTdev
  IO_1711_RMODE         TTIdev (set)/TTOdev (clear)
  IO_1711_MON           TTdev
  IO_1711_MANUAL        TTdev
*/

IO_DEVICE TTIdev = IODEV("TTI", "1711-A", 1711, 1, 1, 0,
                         NULL, NULL, NULL, NULL, NULL,
                         TTIstate, NULL, NULL, NULL,
                         0xF, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1, 0, 0, 0, 0, NULL);

IO_DEVICE TTOdev = IODEV("TTO", "1711-A", 1711, 1, 1, 0,
                         NULL, NULL, NULL, NULL, NULL,
                         TTOstate, NULL, NULL, NULL,
                         0xF, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1, 0, 0, 0, 0, NULL);

IO_DEVICE TTdev = IODEV("TT", "1711-A", 1711, 1, 1, 0,
                        TTreject, TTin, TTout, NULL, NULL,
                        TTstate, NULL, NULL, NULL,
                        0xF, 2,
                        MASK_REGISTER0 | MASK_REGISTER1,
                        0, 0, 0, 0, 0, NULL);

/*
 * Define usage for "private IO_DEVICE data areas
 */
#define iod_holdFull    iod_private4
#define iod_indelay     iod_private9
#define iod_rmode       iod_private4

#define IODP_TTI_XFER   0x01                    /* Transfer to data hold reg */
#define IODP_TTI_MOTION 0x02                    /* Paper motion delay */

#define IO_1711_CONTR   (IO_1711_MANUAL | IO_1711_MON | IO_ST_LOST | \
                         IO_ST_BUSY | IO_ST_READY);
#define IO_1711_IDEVICE (IO_ST_ALARM | IO_ST_EOP | IO_ST_INT | IO_ST_DATA)
#define IO_1711_ODEVICE (IO_ST_ALARM | IO_ST_EOP | IO_ST_INT | IO_ST_DATA)

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
   tti_mod      TTI modifiers list
*/

uint8 tti_manualIntr = 0x7;

#define TTUF_V_HDX      (TTUF_V_UF + 0)
#define TTUF_HDX        (1 << TTUF_V_HDX)

UNIT tti_unit = { UDATA(&tti_svc, UNIT_IDLE+TT_MODE_KSR+TTUF_HDX, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
  { HRDATAD(MODE, TTdev.iod_rmode, 1, "Read/Write mode (Read == TRUE)") },
  { HRDATAD(FUNCTION, TTdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, TTdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, TTIdev.IENABLE, 16, "Interrupts enabled") },
  { HRDATAD(INTRKEY, tti_manualIntr, 8, "Manual interrupt keycode") },
  { NULL }
};

MTAB tti_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1711-A Console Terminal (Input)" },
  { TTUF_HDX, 0, "full duplex", "FDX",
    NULL, NULL, NULL, "Set TT device to full duplex" },
  { TTUF_HDX, TTUF_HDX, "half duplex", "HDX",
    NULL, NULL, NULL, "Set TT device to half duplex" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", NULL,
    NULL, &show_addr, NULL, "Display equipment address" },
  { 0 }
};

DEBTAB tti_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DLOC },
  { NULL }
};

DEVICE tti_dev = {
  "TTI", &tti_unit, tti_reg, tti_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &tti_reset,
  NULL, NULL, NULL,
  &TTdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV, 0, tti_deb,
  NULL, NULL, &tt_help, NULL, NULL, NULL
};

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
   tto_mod      TTO modifiers list
*/

UNIT tto_unit = { UDATA(&tto_svc, TT_MODE_KSR+TTUF_HDX, 0), TT_OUT_WAIT };

REG tto_reg[] = {
  { HRDATAD(MODE, TTdev.iod_rmode, 1, "Read/Write mode (Read == TRUE)") },
  { HRDATAD(FUNCTION, TTdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, TTdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, TTOdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

MTAB tto_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1711-A Console Terminal (Output)" },
  { TTUF_HDX, 0, "full duplex", "FDX",
    NULL, NULL, NULL, "Set TT device to full duplex" },
  { TTUF_HDX, TTUF_HDX, "half duplex", "HDX",
    NULL, NULL, NULL, "Set TT device to half duplex" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", NULL,
    NULL, &show_addr, NULL, "Display equipment address" },
  { 0 }
};


DEBTAB tto_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display devide state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "LOCATION",    DBG_DLOC,       "Display address for I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DLOC },
  { NULL }
};

DEVICE tto_dev = {
  "TTO", &tto_unit, tto_reg, tto_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &tto_reset,
  NULL, NULL, NULL,
  &TTdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_OUTDEV, 0, tto_deb,
  NULL, NULL, &tt_help, NULL, NULL, NULL
};

/*
 * Support routines for terminal physical input device
 */

/*
 * Dump the current state of the TTI device
 */
void TTIstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  char temp[32];

  temp[0] = '\0';

  if (TTIdev.iod_holdFull)
    sprintf(temp, ", Hold full (%02X)", tti_unit.buf & 0xFF);

  fprintf(DBGOUT,
          "%s[TTI %s: Func: %04X, Sta: %04X, Ena: %04X, Dly: %c%s]\r\n",
          INTprefix, where,
          TTIdev.FUNCTION, TTIdev.STATUS, TTIdev.IENABLE, 
          TTIdev.iod_indelay + '0', temp);
}

/* Unit service */

t_stat tti_svc(UNIT *uptr)
{
  int32 c, out;

  if (TTIdev.iod_indelay != 0) {
    /*
     * Waiting for functions related to character input:
     *
     * 1. Transfering the character from the TTY to the hold buffer.
     * 2. Wait for carriage control motion (CR, LF etc)
     */
    if ((TTIdev.iod_indelay & IODP_TTI_XFER) != 0) {
      TTIdev.iod_indelay &= ~IODP_TTI_XFER;
      TTIdev.iod_holdFull = TRUE;
      if ((tti_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT, "%s[TTI: tti_svc() transfer complete]\r\n", INTprefix);

      if ((TTIdev.iod_indelay & IODP_TTI_MOTION) != 0) {
        sim_activate(uptr, TT_IN_MOTION);
        if ((tti_dev.dctrl & DBG_DTRACE) != 0)
          fprintf(DBGOUT, "%s[TTI: tti_svc() motion delay]\r\n", INTprefix);

        return SCPE_OK;
      }
    }

    if ((TTIdev.iod_indelay & IODP_TTI_MOTION) != 0) {
      TTIdev.iod_indelay &= ~IODP_TTI_MOTION;
      if ((tti_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT, "%s[TTI: tti_svc() motion delay complete\r\n", INTprefix);
    }

    TTdev.STATUS &= IO_ST_BUSY;
    TTIdev.STATUS |= IO_ST_EOP;

    fw_IOintr(FALSE, &tti_dev, &TTIdev, 0, 0, 0xFFFF, "Motion delay");
    TTrebuild();

    /*
     * Resume normal polling
     */
    sim_activate(uptr, uptr->wait);
    return SCPE_OK;
  }

  /*
   * Restart the poller.
   */
  sim_activate(uptr, uptr->wait);

  if ((c = sim_poll_kbd()) < SCPE_KFLAG)
    return c;                                   /* No character or error */

  out = c & 0xfF;

  if (out == tti_manualIntr) {
    if ((tti_dev.dctrl & DBG_DTRACE) != 0)
      fprintf(DBGOUT, "%s[TTI: tti_svc() manual interrupt]\r\n", INTprefix);

    TTdev.STATUS |= IO_1711_MANUAL;
    TTIdev.STATUS |= IO_ST_INT;
    RaiseExternalInterrupt(&tti_dev);
    return SCPE_OK;
  }

  if ((c & SCPE_BREAK) != 0)
    c = 0;
  else c = sim_tt_inpcvt(c, TT_GET_MODE(uptr->flags) | TTUF_KSR);

  if (TTIdev.iod_holdFull) {
    if ((tti_dev.dctrl & DBG_DTRACE) != 0)
      fprintf(DBGOUT, "%s[TTI: tti_svc() hold register full]\r\n", INTprefix);

    TTIdev.STATUS |= IO_ST_ALARM | IO_ST_LOST;
    fw_IOintr(FALSE, &tti_dev, &TTIdev, 0, 0, 0xFFFF, "Lost char");
    TTrebuild();
    return SCPE_OK;
  }

  if (((uptr->flags & TTUF_HDX) != 0) && out &&
      ((out = sim_tt_outcvt(out, TT_GET_MODE(uptr->flags) | TTUF_KSR)) >= 0)) {
    sim_putchar(out);
    tto_unit.pos++;
  }
  uptr->buf = c;
  uptr->pos++;

  /*
   * Start a delay while the input character is transferred from the TTY to
   * the hold buffer.
   */
  TTIdev.iod_indelay = IODP_TTI_XFER;
  if ((out == '\r') || (out == '\n') || (out == '\f'))
    TTIdev.iod_indelay |= IODP_TTI_MOTION;

  sim_cancel(uptr);
  sim_activate(uptr, TT_IN_XFER);

  TTdev.STATUS |= IO_ST_BUSY;
  TTIdev.STATUS |= IO_ST_DATA;

  if ((tti_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[TTI: tti_svc() transfer started]\r\n", INTprefix);
    if ((tti_dev.dctrl & DBG_DSTATE) != 0)
      TTIstate("tti_svc", &tti_dev, &TTIdev);
  }

  fw_IOintr(FALSE, &tti_dev, &TTIdev, 0, 0, 0xFFFF, "Input char");
  TTrebuild();

  return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset(DEVICE *dptr)
{
  /*** Reset TT? ***/
  TTIdev.STATUS = IO_1711_RMODE;
  TTIdev.iod_holdFull = FALSE;
  TTIdev.iod_indelay = 0;

  tti_unit.buf = 0;

  if (!sim_is_running)
    sim_activate(&tti_unit, tti_unit.wait);
  return SCPE_OK;
}

/* Perform I/O */

enum IOstatus TTIin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The logical TT device driver only passes INP operations for the data
   * register (0x90).
   */
  TTIdev.iod_holdFull = FALSE;
  Areg = tti_unit.buf;

  TTdev.STATUS &= ~IO_ST_BUSY;
  TTIdev.STATUS |= IO_ST_EOP;
  TTIdev.STATUS &= ~(IO_ST_INT | IO_ST_DATA);
  TTrebuild();
  rebuildPending();

  return IO_REPLY;
}

void TTIcint(void)
{
  /*
   * Clear all interrupt enables
   */
  TTIdev.iod_ienable = 0;
  TTIdev.iod_oldienable = 0;

  /*
   * Clear all pending interrupts
   */
  TTIdev.STATUS &= ~IO_1711_IDEVICE;
  if (TTIdev.iod_holdFull)
    TTIdev.STATUS |= IO_ST_DATA;
}

void TTIccont(void)
{
  tti_reset(&tti_dev);
  TTIdev.iod_holdFull = FALSE;

  TTIcint();
}

/*
 * Support routines for terminal physical output device
 */

/*
 * Dump the current state of the TTO device
 */
void TTOstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  char temp[32];

  temp[0] = '\0';

  if (TTOdev.iod_holdFull)
    sprintf(temp, ", Hold full (%02X)", tto_unit.buf & 0xFF);

  fprintf(DBGOUT,
          "%s[TTO %s: Func: %04X, Sta: %04X, Ena: %04X%s]\r\n",
          INTprefix, where,
          TTOdev.FUNCTION, TTOdev.STATUS, TTOdev.IENABLE, temp);
}

/* Unit service */

t_stat tto_svc(UNIT *uptr)
{
  int32 c;
  t_stat r;

  c = sim_tt_outcvt(uptr->buf, TT_GET_MODE(uptr->flags) | TTUF_KSR);
  if (c >= 0) {
    if ((r = sim_putchar_s(c)) != SCPE_OK) {
      sim_activate(uptr, uptr->wait);           /* Try again later */
      return (r == SCPE_STALL) ? SCPE_OK : r;
    }
  }

  TTOdev.iod_holdFull = FALSE;
  TTOdev.STATUS |= IO_ST_EOP | IO_ST_DATA;
  TTdev.STATUS &= ~IO_ST_BUSY;

  if ((tto_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[TTO: tto_svc()]\r\n", INTprefix);
    if ((tto_dev.dctrl & DBG_DSTATE) != 0)
      TTOstate("tto_svc", &tto_dev, &TTOdev);
  }

  uptr->pos++;

  fw_IOintr(FALSE, &tto_dev, &TTOdev, 0, 0, 0xFFFF, "Output done");
  TTrebuild();

  return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset(DEVICE *dptr)
{
  /*** Reset TT? ***/
  TTOdev.STATUS = IO_ST_DATA;
  TTOdev.iod_holdFull = FALSE;

  return SCPE_OK;
}

/* Perform I/O */

enum  IOstatus TTOout(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The logical TT device driver only passes OUT operations for the data
   * register (0x90).
   */
  sim_activate(&tto_unit, tto_unit.wait);

  tto_unit.buf = Areg;
  TTOdev.iod_holdFull = TRUE;

  TTOdev.STATUS &= ~(IO_ST_EOP | IO_ST_INT | IO_ST_DATA);
  TTdev.STATUS |= IO_ST_BUSY;
  TTrebuild();
  rebuildPending();

  return IO_REPLY;
}

void TTOcint(void)
{
  /*
   * Clear all interrupt enables
   */
  TTOdev.iod_ienable = 0;
  TTOdev.iod_oldienable = 0;

  /*
   * Clear all pending interrupts
   */
  TTOdev.STATUS &= ~IO_1711_ODEVICE;

  if (!TTOdev.iod_holdFull)
    TTOdev.STATUS |= IO_ST_DATA;
}

void TTOccont(void)
{
  tto_reset(&tto_dev);
  sim_cancel(&tto_unit);

  TTOcint();
}

/*
 * Support routines for the terminal logical device
 */

/*
 * Dump the current internal state of the TT device. Note that "dev" is
 * not used by this routine since it is a logical device which has no
 * associated physical device.
 */
void TTstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[TT %s: Func: %04X, Sta: %04X, Mode: %c]\r\n",
          INTprefix, where, iod->FUNCTION, iod->STATUS,
          iod->iod_rmode ? 'R' : 'W');

  if ((tti_dev.dctrl & DBG_DSTATE) != 0)
    TTIstate(where, &tti_dev, &TTIdev);

  if ((tto_dev.dctrl & DBG_DSTATE) != 0)
    TTOstate(where, &tto_dev, &TTOdev);
}

/*
 * Reset routine
 */
void TTreset(void)
{
  TTdev.STATUS = IO_1711_MON | IO_ST_READY;
  TTdev.iod_rmode = TRUE;
}

/* Rebuild the director status register */

uint16 TTrebuild(void)
{
  TTdev.STATUS &= IO_1711_CONTR;
  if (TTdev.iod_rmode) {
    TTdev.STATUS |= (TTIdev.STATUS & IO_1711_IDEVICE) | IO_1711_RMODE;
  } else TTdev.STATUS |= TTOdev.STATUS & IO_1711_ODEVICE;
  TTdev.STATUS |= IO_1711_MON | IO_ST_READY;

  return TTdev.STATUS;
}

/* Check if I/O should be rejected */

t_bool TTreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  if (reg == 0) {
    if (output)
      return (TTdev.STATUS & IO_ST_BUSY) != 0;
    return !TTIdev.iod_holdFull;
  }

  if (output) {
    uint16 func = Areg & IO_1711_DIRMSK;

    if (func != 0) {
      if ((func & (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA | \
                   IO_DIR_CINT | IO_DIR_CCONT)) != 0)
        return FALSE;

      /*
       * Select read/write mode must be set - reject if both are set.
       */
      if ((func & (IO_1711_SREAD | IO_1711_SWRITE)) == 
          (IO_1711_SREAD | IO_1711_SWRITE))
        return TRUE;

      return (TTdev.STATUS & IO_ST_BUSY) != 0;
    }
  }
  return FALSE;
}

/* Perform I/O */

enum IOstatus TTin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * Certain invalid operations will already have been rejected in TTreject().
   */
  if (reg == 0)
    return TTIin(&TTIdev, reg);

  Areg = TTrebuild();
  return IO_REPLY;
}

enum IOstatus TTout(IO_DEVICE *iod, uint8 reg)
{
  t_bool rmode, changed = FALSE;
  DEVICE *dptr;

  /*
   * Certain invalid operation will already have been rejected in TTreject().
   */
  if (reg == 0)
    return TTOout(&TTOdev, reg);

  if ((IOAreg & IO_DIR_CCONT) != 0) {
    /*
     * Clear both sides of the controller and switch to read-mode.
     */
    if (((tti_dev.dctrl & DBG_DSTATE) != 0) ||
        ((tto_dev.dctrl & DBG_DSTATE)!= 0))
      fprintf(DBGOUT, "%s[TT: Controller Reset\r\n", INTprefix);

    TTIccont();
    TTOccont();
    TTdev.STATUS &= ~IO_ST_BUSY;

    TTdev.iod_rmode = TRUE;
  }

  if ((IOAreg & IO_DIR_CINT) != 0) {
    /*
     * Clear interrupts for the currently active mode
     */
    if (TTdev.iod_rmode)
      TTIcint();
    else TTOcint();
    TTdev.STATUS &= ~IO_1711_MANUAL;
  }

  /*
   * If Clear Controller or Clear Interrupts was set, don't process
   * read/write select bits.
   */
  if ((IOAreg & (IO_DIR_CINT | IO_DIR_CCONT)) == 0) {
    /*
     * Check for switching modes. We've already rejected a request to select
     * both modes.
     */
    if ((IOAreg & IO_1711_SREAD) != 0)
      TTdev.iod_rmode = TRUE;
    if ((IOAreg & IO_1711_SWRITE) != 0) {
      TTdev.iod_rmode = FALSE;
      TTIdev.STATUS &= ~IO_ST_LOST;
    }
  }

  rebuildPending();

  rmode = TTdev.iod_rmode;

  if ((IOAreg & (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)) != 0) {
    iod = rmode ? &TTIdev : &TTOdev;
    dptr = rmode ? &tti_dev : &tto_dev;

    iod->iod_oldienable = iod->iod_ienable;
    iod->iod_ienable |= IOAreg & (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA);
    changed = iod->iod_ienable != iod->iod_oldienable;
  }

  if (changed)
    if (!rmode)
      fw_IOintr(FALSE, dptr, iod, 0, 0, 0xFFFF, "Can output");

  TTrebuild();

  return IO_REPLY;
}

t_stat tt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The TTI/TTO device is a 1711-A teletype console. The device is\n"
    " implemented as 2 seperate devices within the simulator; TTI for input\n"
    " and TTO for output.\n"
    "1 Hardware Description\n"
    " The 1711-A consists of a teletype console terminal with an extra button\n"
    " which is used to generate a 'manual interrupt'. By default, the\n"
    " simulator uses the 'Control+G' combination to generate the interrupt.\n"
    " This key combination may be changed by:\n\n"
    "+sim> DEPOSIT TTI INTRKEY keycodeValue\n\n"
    "2 Equipment Address\n"
    " The console device is part of the low-speed package and, as such, is at\n"
    " fixed equipment address 1, station 1.\n"
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

t_stat ptr_svc(UNIT *);
t_stat ptr_reset(DEVICE *);
t_stat ptr_attach(UNIT *, CONST char *);
t_stat ptr_detach(UNIT *);

enum IOstatus PTRin(IO_DEVICE *, uint8);
enum IOstatus PTRout(IO_DEVICE *, uint8);

t_stat ptr_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1721-A/B/C/D, 1722-A/B Paper Tape Reader

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A

      00A0                                   Read
      00A1              Director Function    Director Status


  Operations:

  Director Function

    15                               7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X |   |   |   | X |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                         |   |   |       |   |   |
                                         |   |   |       |   |   Clr Controller
                                         |   |   |       |   Clr Interrupts
                                         |   |   |       Data Interrupt Req.
                                         |   |   Interrupt on Alarm
                                         |   Start Motion
                                         Stop Motion
  Status Response:

  Director Status

    15              11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X |   |   |   |   |   |   | X |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                         |   |   |   |   |   |       |   |   |   |
                         |   |   |   |   |   |       |   |   |   Ready
                         |   |   |   |   |   |       |   |   Busy
                         |   |   |   |   |   |       |   Interrupt
                         |   |   |   |   |   |       Data
                         |   |   |   |   |   Alarm
                         |   |   |   |   Lost Data
                         |   |   |   Protected
                         |   |   Existence Code
                         |   Paper Motion Failure
                         Power On
 */

IO_DEVICE PTRdev = IODEV(NULL, "1721-A", 1721, 1, 2, 0,
                         fw_reject, PTRin, PTRout, NULL, NULL,
                         NULL, NULL, NULL, NULL,
                         0xF, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1, 0, 0, 0, 0, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_PTRmotion   iod_private
/*
 * Current state of the device (STOPPED/STARTED).
 */
#define IODP_PTRSTOPPED 0x0000
#define IODP_PTRSTARTED 0x0001
#define IODP_PTR_MASK   0x0001

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
   ptr_mod      PTR modifiers list
*/

UNIT ptr_unit = {
  UDATA(&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), PTR_IN_WAIT
};

REG ptr_reg[] = {
  { HRDATAD(FUNCTION, PTRdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, PTRdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, PTRdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

MTAB ptr_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1721-A Paper Tape Reader" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", NULL,
    NULL, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)" },
  { 0 }
};

DEBTAB ptr_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Traced device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejected" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DLOC },
  { NULL }
};

DEVICE ptr_dev = {
  "PTR", &ptr_unit, ptr_reg, ptr_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &ptr_reset,
  NULL, &ptr_attach, &ptr_detach,
  &PTRdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV | DEV_PROTECT, 0, ptr_deb,
  NULL, NULL, &ptr_help, NULL, NULL, NULL
};

/*
 * Dump the current internal state of the PTR device.
 */
const char *PTRprivateState[2] = {
  "", "In Motion"
};

void PTRstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Sta: %04X, Ena: %04X, Private: %s\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->STATUS, iod->IENABLE,
          PTRprivateState[iod->iod_PTRmotion & IODP_PTR_MASK]);
}

/* Unit service */

t_stat ptr_svc(UNIT *uptr)
{
  int32 temp;

  if ((ptr_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[PTR: ptr_svc() entry]\r\n", INTprefix);
    if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
      PTRstate("svc entry", &ptr_dev, &PTRdev);
  }

  if ((uptr->flags & UNIT_ATT) == 0) {
    if ((ptr_dev.dctrl & DBG_DTRACE) != 0)
      fprintf(DBGOUT,
               "%s[PTR: ptr_svc() exit - no attached file]\r\n", INTprefix);
    return SCPE_OK;
  }

  if ((temp = getc(uptr->fileref)) == EOF) {
    if (feof(uptr->fileref)) {
      if ((ptr_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT, "%s[PTR: ptr_svc() exit - EOF]\r\n", INTprefix);

      /*
       * We've run off the end of the tape. Indicate motion failure and alarm
       * status and generate an interrupt if requested.
       */
      fw_IOintr(FALSE, &ptr_dev, &PTRdev, IO_ST_ALARM | IO_1721_MOTIONF, IO_ST_READY, 0xFFFF, "End of tape");
      return SCPE_OK;
    } else perror("PTR I/O error");
    clearerr(uptr->fileref);
    if ((ptr_dev.dctrl & DBG_DTRACE) != 0)
      fprintf(DBGOUT, "%s[PTR: ptr_svc() exit - Read Error]\r\n", INTprefix);
    return SCPE_IOERR;
  }
  uptr->buf = temp & 0xFF;
  uptr->pos++;

  fw_IOcompleteData(FALSE, &ptr_dev, &PTRdev, 0xFFFF, "Read Complete");

  if ((ptr_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT,
            "%s[PTR: ptr_svc() exit => %2X]\r\n", INTprefix, uptr->buf);
    if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
      PTRstate("svc exit", &ptr_dev, &PTRdev);
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset(DEVICE *dptr)
{
  DEVRESET(&PTRdev);

  PTRdev.iod_PTRmotion = IODP_PTRSTOPPED;

  if ((ptr_unit.flags & UNIT_ATT) != 0)
    fw_setForced(&PTRdev, IO_1721_POWERON | IO_ST_READY);

  ptr_unit.buf = 0;
  return SCPE_OK;
}

/* Attach routine */

t_stat ptr_attach(UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if ((r = attach_unit(uptr, cptr)) != SCPE_OK)
    return r;

  fw_setForced(&PTRdev, IO_1721_POWERON | IO_ST_READY);
  return SCPE_OK;
}

/* Detach routine */

t_stat ptr_detach(UNIT *uptr)
{
  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;

  fw_clearForced(&PTRdev, IO_1721_POWERON | IO_ST_READY);
  return detach_unit(uptr);
}

/* Perform I/O */

enum IOstatus PTRin(IO_DEVICE *iod, uint8 reg)
{
  if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
    PTRstate("before", &ptr_dev, &PTRdev);

  /*
   * The framework only passes IN operations for the data register (0x90)
   */
  Areg = (Areg & 0xFF00) | ptr_unit.buf;
  fw_IOunderwayData(&PTRdev, 0);
  if (PTRdev.iod_PTRmotion == IODP_PTRSTARTED)
    sim_activate(&ptr_unit, ptr_unit.wait);

  if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
    PTRstate("after", &ptr_dev, &PTRdev);
  return IO_REPLY;
}

enum IOstatus PTRout(IO_DEVICE *iod, uint8 reg)
{
  if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
    PTRstate("before", &ptr_dev, &PTRdev);

  switch (reg) {
    case 0x00:
      if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
        PTRstate("after", &ptr_dev, &PTRdev);
      return IO_REJECT;

    case 0x01:
      doDirectorFunc(&ptr_dev, FALSE);

      if ((IOAreg & IO_DIR_START) != 0) {
        fw_setForced(&PTRdev, IO_ST_BUSY);
        PTRdev.iod_PTRmotion = IODP_PTRSTARTED;
      }
      if ((IOAreg & IO_DIR_STOP) != 0) {
        fw_clearForced(&PTRdev, IO_ST_BUSY);
        PTRdev.iod_PTRmotion = IODP_PTRSTOPPED;
      }

      if (PTRdev.iod_PTRmotion == IODP_PTRSTARTED)
        sim_activate(&ptr_unit, ptr_unit.wait);
  }
  if ((ptr_dev.dctrl & DBG_DSTATE) != 0)
    PTRstate("after", &ptr_dev, &PTRdev);
  return IO_REPLY;
}

t_stat ptr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1721-A paper tape reader.\n"
    "1 Hardware Description\n"
    " The 1721-A consists of a controller and a physical paper tape reader.\n"
    "2 Equipment Address\n"
    " The paper tape reader is part of the low-speed package and, as such, is\n"
    " at fixed equipment address 1, station 2.\n"
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device. These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation. STATUS always\n"
    " contains the current status of the device as it would be read by an\n"
    " application program.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}

t_stat ptp_svc(UNIT *);
t_stat ptp_reset(DEVICE *);

enum IOstatus PTPin(IO_DEVICE *, uint8);
enum IOstatus PTPout(IO_DEVICE *, uint8);

t_stat ptp_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1723-A/B, 1724-A/B Paper Tape Punch

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A

      00C0              Write
      00C1              Director Function    Director Status

  Operations:

  Director Function

    15                               7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X |   |   |   | X |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                         |   |   |       |   |   |
                                         |   |   |       |   |   Clr Controller
                                         |   |   |       |   Clr Interrupts
                                         |   |   |       Data Interrupt Req.
                                         |   |   Interrupt on Alarm
                                         |   Start Motion
                                         Stop Motion

  Status Response:

  Director Status

    15          12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X |   |   |   |   |   | X |   | X |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                     |   |   |   |   |       |       |   |   |   |
                     |   |   |   |   |       |       |   |   |   Ready
                     |   |   |   |   |       |       |   |   Busy
                     |   |   |   |   |       |       |   Interrupt
                     |   |   |   |   |       |       Data
                     |   |   |   |   |       Alarm
                     |   |   |   |   Protected
                     |   |   |   Existence Code
                     |   |   Tape Break
                     |   Power On
                     Tape Supply Low
 */

IO_DEVICE PTPdev = IODEV(NULL, "1723-A", 1723, 1, 4, 0,
                         fw_reject, PTPin, PTPout, NULL, NULL,
                         NULL, NULL, NULL, NULL,
                         0xF, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1, 0, 0, 0, 0, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_PTPdelay    iod_private

/*
 * Current delay state of the device.
 */
#define IODP_PTPINTRWAIT        0x0001
#define IODP_PTPDATAWAIT        0x0002
#define IODP_PTP_MASK           (IODP_PTPINTRWAIT | IODP_PTPDATAWAIT)

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
   ptp_mod      PTP modifiers list
*/

UNIT ptp_unit = {
  UDATA(&ptp_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), PTP_OUT_WAIT
};

REG ptp_reg[] = {
  { HRDATAD(FUNCTION, PTPdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, PTPdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, PTPdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

MTAB ptp_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1723-A Paper Tape Punch" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", NULL,
    NULL, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)" },
  { 0 }
};

DEBTAB ptp_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DLOC },
  { NULL }
};

DEVICE ptp_dev = {
  "PTP", &ptp_unit, ptp_reg, ptp_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &ptp_reset,
  NULL, NULL, NULL,
  &PTPdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_OUTDEV | DEV_PROTECT, 0, ptp_deb,
  NULL, NULL, &ptp_help, NULL, NULL, NULL
};
 
/*
 * Dump the current internal state of the PTP device.
 */
const char *PTPprivateState[4] = {
  "", "INTRWAIT", "DATAWAIT", "DATAWAIT,INTRWAIT"
};

void PTPstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Sta: %04X, Ena: %04X, Private: %s\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->STATUS, iod->IENABLE,
          PTPprivateState[iod->iod_PTPdelay & IODP_PTP_MASK]);
}

/* Unit service */

t_stat ptp_svc(UNIT *uptr)
{
  if ((ptp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[PTP: ptp_svc() entry]\r\n", INTprefix);
    if ((ptp_dev.dctrl & DBG_DSTATE) != 0)
      PTPstate("svc entry", &ptp_dev, &PTPdev);
  }

  if ((PTPdev.iod_PTPdelay & IODP_PTPINTRWAIT) != 0) {
    /*
     * Generate an interrupt indicating that the motor is up to speed.
     */
    PTPdev.iod_PTPdelay &= ~IODP_PTPINTRWAIT;
    fw_IOintr(FALSE, &ptp_dev, &PTPdev, 0, 0, 0xFFFF, "Up to speed");

    if ((PTPdev.iod_PTPdelay & IODP_PTP_MASK) != 0)
      sim_activate(&ptp_unit, ptp_unit.wait);
    goto done;
  }

  if ((PTPdev.iod_PTPdelay & IODP_PTPDATAWAIT) != 0) {
    /*
     * Now process the actual output of data to be punched.
     */
    PTPdev.iod_PTPdelay &= ~IODP_PTPDATAWAIT;

    if ((uptr->flags & UNIT_ATT) != 0) {
      if (putc(uptr->buf, uptr->fileref) == EOF) {
        perror("PTP I/O error");
        clearerr(uptr->fileref);
      } else uptr->pos++;
    }

    fw_IOcompleteData(FALSE, &ptp_dev, &PTPdev, 0xFFFF, "Output complete");
  }

 done:
  if ((ptp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[PTP: ptp_svc() exit]\r\n", INTprefix);
    if ((ptp_dev.dctrl & DBG_DSTATE) != 0)
      PTPstate("svc exit", &ptp_dev, &PTPdev);
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset(DEVICE *dptr)
{
  DEVRESET(&PTPdev);

  PTPdev.iod_PTPdelay = 0;
  fw_setForced(&PTPdev, IO_1723_POWERON | IO_ST_READY);

  ptp_unit.buf = 0;
  if (!sim_is_running)
    sim_activate(&ptp_unit, ptp_unit.wait);
  return SCPE_OK;
}

/* Perform I/O */

enum IOstatus PTPin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The framework only passes IN operations for the data register (0x90)
   */
  return IO_REJECT;
}

enum IOstatus PTPout(IO_DEVICE *iod, uint8 reg)
{
  if ((ptp_dev.dctrl & DBG_DSTATE) != 0)
    PTPstate("before", &ptp_dev, &PTPdev);

  switch (reg) {
    case 0x00:
      PTPdev.iod_PTPdelay |= IODP_PTPDATAWAIT;
      ptp_unit.buf = Areg;

      fw_IOunderwayData(&PTPdev, IO_ST_INT);

      rebuildPending();

      sim_activate(&ptp_unit, ptp_unit.wait);

      if ((ptp_dev.dctrl & DBG_DSTATE) != 0)
        PTPstate("after", &ptp_dev, &PTPdev);
      break;

    case 0x01:
      /*
       * Check for illegal combination of commands
       */
      if (STARTSTOP(Areg))
        return IO_REJECT;

      if (doDirectorFunc(&ptp_dev, FALSE)) {
        /*
         * The device interrupt mask has been explicitly changed. If the
         * interrupt on data was just set and the device is ready, generate
         * a delayed interrupt.
         */
        if ((ICHANGED(&PTPdev) & IO_DIR_DATA) != 0) {
          if ((PTPdev.STATUS & IO_ST_READY) != 0) {
            if ((PTPdev.iod_PTPdelay & IODP_PTP_MASK) == 0) {
              if ((ptp_dev.dctrl & DBG_DTRACE) != 0)
                fprintf(DBGOUT,
                        "%sPTP: Mask change interrupt\n", INTprefix);
              sim_activate(&ptp_unit, ptp_unit.wait);
              PTPdev.iod_PTPdelay |= IODP_PTPINTRWAIT;
            }
          }
        }
      }

      if (IOAreg != 0) {
        if ((IOAreg & (IO_DIR_START | IO_DIR_STOP)) != 0) {
          sim_activate(&ptp_unit, 5 * ptp_unit.wait);
          PTPdev.iod_PTPdelay |= IODP_PTPINTRWAIT;
          if ((IOAreg & IO_DIR_START) != 0) {
            fw_setForced(&PTPdev, IO_ST_BUSY);
            PTPdev.STATUS |= IO_ST_DATA;
          }
          if ((IOAreg & IO_DIR_STOP) != 0) {
            fw_clearForced(&PTPdev, IO_ST_BUSY);
            PTPdev.STATUS &= ~IO_ST_DATA;
          }
        }
      }

      if ((ptp_dev.dctrl & DBG_DSTATE) != 0)
        PTPstate("after", &ptp_dev, &PTPdev);
      break;
  }
  return IO_REPLY;
}

t_stat ptp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1723-A paper tape punch.\n"
    "1 Hardware Description\n"
    " The 1723-A consists of a controller and a physical paper tape punch.\n"
    "2 Equipment Address\n"
    " The paper tape reader is part of the low-speed package and, as such, is\n"
    " at fixed equipment address 1, station 4.\n"
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device. These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation. STATUS always\n"
    " contains the current status of the device as it would be read by an\n"
    " application program.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}

t_stat cdr_svc(UNIT *);
t_stat cdr_reset(DEVICE *);

enum IOstatus CDRin(IO_DEVICE *, uint8);
enum IOstatus CDRout(IO_DEVICE *, uint8);

/*
        1729-A/B Card Reader

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A

      00E0                                   Read
      00E1              Director Function    Director Status


  Operations:

  Director Function

    15                               7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                         |   |   |   |   |   |   |
                                         |   |   |   |   |   |   Clr Controller
                                         |   |   |   |   |   Clr Interrupts
                                         |   |   |   |   Data Interrupt Req.
                                         |   |   |   Interrupt on End of Record
                                         |   |   Interrupt on Alarm
                                         |   Start Motion
                                         Stop Motion

  Status Response:

  Director Status

    15                  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             |   |   |   |   |   |   |   |   |   |
                             |   |   |   |   |   |   |   |   |   Ready
                             |   |   |   |   |   |   |   |   Busy
                             |   |   |   |   |   |   |   Interrupt
                             |   |   |   |   |   |   Data
                             |   |   |   |   |   End Of Record
                             |   |   |   |   Alarm
                             |   |   |   Lost Data
                             |   |   Protected
                             |   Existence Code
                             Read Station Empty

 */

IO_DEVICE CDRdev = IODEV(NULL, "1729", 1729, 1, 6, 0,
                         fw_reject, CDRin, CDRout, NULL, NULL,
                         NULL, NULL, NULL, NULL,
                         0xF, 2,
                         MASK_REGISTER0 | MASK_REGISTER1,
                         MASK_REGISTER1, 0, 0, 0, 0, NULL);

/* CDR data structures

   cdr_dev      CDR device descriptor
   cdr_unit     CDR unit descriptor
   cdr_reg      CDR register list
   cdr_mod      CDR modifiers list
*/

UNIT cdr_unit = {
  UDATA(&cdr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), SERIAL_IN_WAIT
};

REG cdr_reg[] = {
  { HRDATAD(FUNCTION, CDRdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, CDRdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, CDRdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

MTAB cdr_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1729 Card Reader" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", NULL,
    NULL, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)" },
  { 0 }
};

DEBTAB cdr_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "LOCATION",    DBG_DLOC,       "Display address for I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DLOC },
  { NULL }
};

DEVICE cdr_dev = {
  "CDR", &cdr_unit, cdr_reg, cdr_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &cdr_reset,
  NULL, NULL, NULL,
  &CDRdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV | DEV_PROTECT, 0, cdr_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

/* Unit service */

t_stat cdr_svc(UNIT *uptr)
{
  /***  TODO: Implement Card Reader support ***/
  return SCPE_OK;
}

/* Reset routine */

t_stat cdr_reset(DEVICE *dptr)
{
  DEVRESET(&CDRdev);

  ptr_unit.buf = 0;
  if (!sim_is_running)
    sim_activate(&cdr_unit, cdr_unit.wait);
  return SCPE_OK;
}

/* Perform I/O */

enum IOstatus CDRin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The framework only passes IN operations for the data register (0x90)
   */
  Areg = cdr_unit.buf;
  CDRdev.STATUS &= IO_ST_BUSY | IO_ST_DATA;
  return IO_REPLY;
}

enum IOstatus CDRout(IO_DEVICE *iod, uint8 reg)
{
  switch (reg) {
    case 0x00:
      return IO_REJECT;

    case 0x01:
      doDirectorFunc(&cdr_dev, FALSE);
      /*** TODO: Process local director functions ***/
      break;
  }
  return IO_REPLY;
}

/*
 * Return device 1 interrupt status. If any of the sub-devices have their
 * interrupt status active, return the device 1 interrupt mask bit.
 */
uint16 dev1INTR(DEVICE *dptr)
{
  uint16 status;

  status = TTIdev.STATUS |  TTOdev.STATUS | PTRdev.STATUS | PTPdev.STATUS | CDRdev.STATUS;

  return (status & IO_ST_INT) != 0 ? 1 << 1 : 0;
}

/*
 * Update a buffer indicating which device 1 stations are asserting interrupt
 * status.
 */
void dev1Interrupts(char *buf)
{
  buf[0] = '\0';

  if ((TTIdev.STATUS & IO_ST_INT) != 0)
    strcat(buf, " TTI");
  if ((TTOdev.STATUS & IO_ST_INT) != 0)
    strcat(buf, "TTO");
  if ((PTRdev.STATUS & IO_ST_INT) != 0)
    strcat(buf, " PTR");
  if ((PTPdev.STATUS & IO_ST_INT) != 0)
    strcat(buf, " PTP");
  if ((CDRdev.STATUS & IO_ST_INT) != 0)
    strcat(buf, " CDR");
}
