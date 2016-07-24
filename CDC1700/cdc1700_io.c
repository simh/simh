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

/* cdc1700_io.c: CDC1700 I/O subsystem
 */

#include "cdc1700_defs.h"

extern char INTprefix[];

extern t_bool inProtectedMode(void);
extern uint16 dev1INTR(DEVICE *);
extern uint16 cpuINTR(DEVICE *);
extern uint16 dcINTR(void);

extern void RaiseExternalInterrupt(DEVICE *);

extern enum IOstatus fw_doIO(DEVICE *, t_bool);

extern uint16 Areg, Mreg, Preg, OrigPreg, Qreg, Pending, IOAreg, IOQreg, M[];
extern uint8 Protected, INTflag;
extern t_uint64 Instructions;

extern t_bool FirstRejSeen;
extern uint32 CountRejects;

extern DEVICE cpu_dev, dca_dev, dcb_dev, dcc_dev, tti_dev, tto_dev,
  ptr_dev, ptp_dev, cdr_dev;

static const char *status[] = {
  "REPLY", "REJECT", "INTERNALREJECT"
};

/*
 * The I/O sub-system uses the Q-register to provide controller addressing:
 * 
 *       15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 *      +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *      |       W      |     E     |       Command      |
 *      +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * If W is non-zero, it addresses a 1706-A buffered data channel. If W
 * is zero, it addresses a non-buffered controller.
 *
 * Note that buffered operations (DMA) can be performed by certain controllers
 * (e.g. The Disk Pack Controller) using DSA (Direct Storage Access).
 */

typedef enum IOstatus devIO(DEVICE *, t_bool);

/*
 * There can be up to 16 equipment addresses.
 */
devIO *IOcall[16];
DEVICE *IOdev[16];
devINTR *IOintr[16];

/*
 * Display equipment/station address, buffered data channel and optional
 * additional information:
 *
 *      Stop on Reject status
 *      Protected status
 */
t_stat show_addr(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  DEVICE *dptr;
  IO_DEVICE *iod;

  if (uptr == NULL)
    return SCPE_IERR;

  dptr = find_dev_from_unit(uptr);
  if (dptr == NULL)
    return SCPE_IERR;

  iod = (IO_DEVICE *)dptr->ctxt;
  fprintf(st, "\n\tequip: 0x");
  fprint_val(st, (t_value)iod->iod_equip, DEV_RDX, 16, PV_LEFT);
  if (iod->iod_station != 0xFF) {
    fprintf(st, ", station: ");
    fprint_val(st, (t_value)iod->iod_station, DEV_RDX, 8, PV_LEFT);
  }
  if (iod->iod_dc != 0)
    fprintf(st, ", Buffered Data Channel: %c", '0' + iod->iod_dc);

  if ((dptr->flags & DEV_REJECT) != 0)
    fprintf(st, ", Stop on Reject");
  if ((dptr->flags & DEV_PROTECTED) != 0)
    fprintf(st, ", Protected");
  return SCPE_OK;
}

/*
 * Device stop on reject handling.
 */
t_stat set_stoponrej(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  DEVICE *dptr;

  if (cptr != NULL)
    return SCPE_ARG;

  dptr = find_dev_from_unit(uptr);
  if (dptr == NULL)
    return SCPE_IERR;

  dptr->flags |= DEV_REJECT;
  return SCPE_OK;
}

t_stat clr_stoponrej(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  DEVICE *dptr;

  if (cptr != NULL)
    return SCPE_ARG;

  dptr = find_dev_from_unit(uptr);
  if (dptr == NULL)
    return SCPE_IERR;

  dptr->flags &= ~DEV_REJECT;
  return SCPE_OK;
}

/*
 * Protected device.
 */
t_stat set_protected(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  DEVICE *dptr;

  if (cptr != NULL)
    return SCPE_ARG;

  dptr = find_dev_from_unit(uptr);
  if (dptr == NULL)
    return SCPE_IERR;

  dptr->flags |= DEV_PROTECTED;
  return SCPE_OK;
}

t_stat clear_protected(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  DEVICE *dptr;

  if (cptr != NULL)
    return SCPE_ARG;

  dptr = find_dev_from_unit(uptr);
  if (dptr == NULL)
    return SCPE_IERR;

  dptr->flags &= ~DEV_PROTECTED;
  return SCPE_OK;
}

/*
 * Device interrupt handling
 */

/*
 * Interrupt status for a non-existent device
 */
uint16 noneINTR(DEVICE *dptr)
{
  return 0;
}

/*
 * Generic device interrupt status
 */
uint16 deviceINTR(DEVICE *dptr)
{
  IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

  if ((iod->iod_flags & STATUS_ZERO) != 0)
    return 0;

  return (DEVSTATUS(iod) & IO_ST_INT) != 0 ? iod->iod_interrupt : 0;
}

/*
 * Rebuild the pending interrupt status based on the current status of
 * each device.
 */
void rebuildPending(void)
{
  int i;

  /*
   * Leave the CPU interrupt pending bit alone.
   */
  Pending &= 1;

  for (i = 0; i < 16; i++) {
    devINTR *rtn = IOintr[i];

    Pending |= rtn(IOdev[i]);
  }

  Pending |= dcINTR();
}

/*
 * Handle generic director function(s) for a device. The function request is
 * in IOAreg and the bits will be cleared in IOAreg as they are processed.
 * Return TRUE if an explicit change was made to the device interrupt mask.
 */
t_bool doDirectorFunc(DEVICE *dptr, t_bool allowStacked)
{
  IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

  /*
   * Mask out unsupported commands
   */
  IOAreg &= iod->iod_dmask;

  if ((IOAreg & (IO_DIR_CINT | IO_DIR_CCONT)) != 0) {
    if ((IOAreg & IO_DIR_CCONT) != 0) {
      /*
       * Preferentially use a device specific "Clear Controller" routine
       * over the device reset routine.
       */
      if (iod->iod_clear != NULL) {
        iod->iod_clear(dptr);
      } else {
        if (dptr->reset != NULL)
          dptr->reset(dptr);
      }
    }

    /*
     * Clear all interrupt enables.
     */
    iod->iod_ienable = 0;
    iod->iod_oldienable = 0;

    /*
     * Clear all pending interrupts.
     */
    DEVSTATUS(iod) &= ~iod->iod_cmask;

    rebuildPending();

    /*
     * The device may allow other commands to be stacked along with Clear
     * Interrupts and Clear Controller.
     */
    if (!allowStacked) {
      IOAreg = 0;
      return FALSE;
    }
    IOAreg &= ~(IO_DIR_CINT | IO_DIR_CCONT);
  }

  if ((IOAreg & iod->iod_imask) != 0) {
    /*
     * This request is enabling one or more interrupts.
     */
    iod->iod_oldienable = iod->iod_ienable;
    iod->iod_ienable |= Areg & iod->iod_imask;
    IOAreg &= ~iod->iod_imask;
    return TRUE;
  }
  return FALSE;
}

/*
 * Perform an I/O operation. Note that the "Continue" bit is only supported
 * on the 1706 buffered data channel devices since it is not relevant in the
 * emulation environment.
 */
enum IOstatus doIO(t_bool output, DEVICE **device)
{
  enum IOstatus result;
  DEVICE *dev;
  devIO *rtn;
  const char *name;
  IO_DEVICE *iod;

  /*
   * Make a private copy of Areg and Qreg for use by I/O routines
   */
  IOAreg = Areg;
  IOQreg = Qreg;

  /*
   * Get the target device and access routine
   */
  dev = IOdev[((IOQreg & IO_EQUIPMENT) >> 7) & 0xF];
  rtn = IOcall[((IOQreg & IO_EQUIPMENT) >> 7) & 0xF];

  if ((((IOQreg & IO_EQUIPMENT) >> 7) & 0xF) == 1) {
    /*
     * Device address 1 requires special processing. This address
     * multiplexes the console teletypewriter, the paper tape reader and
     * punch and the card reader using different station addresses:
     *
     *  001     - 1711/1712/1713 teletypewriter
     *  010     - 1721/1722 paper tape reader
     *  100     - 1723/1724 paper tape punch
     *  110     - 1729 card reader
     */
    switch ((IOQreg >> 4) & 0x7) {
    case 0x01:
      dev = &tti_dev;
      break;
      
    case 0x02:
      dev = &ptr_dev;
      break;

    case 0x04:
      dev = &ptp_dev;
      break;

    case 0x06:
      dev = &cdr_dev;
      break;
      
    default:
      return IO_INTERNALREJECT;
    }
  }

  if ((IOQreg & IO_W) != 0) {
    /*
     * Buffered data channel access.
     */

    /*
     * Check if this device is only accessible on the AQ channel.
     */
    if (dev != NULL) {
      iod = (IO_DEVICE *)dev->ctxt;

      if ((iod->iod_flags & AQ_ONLY) != 0) {
        *device = dev;
        return IO_INTERNALREJECT;
      }
    }

    switch (IOQreg & IO_W) {
      /*
       * 1706-A Channel #1
       */
      case IO_1706_1_A:
      case IO_1706_1_B:
      case IO_1706_1_C:
      case IO_1706_1_D:
        dev = &dca_dev;
        break;

      /*
       * 1706-A Channel #2
       */
      case IO_1706_2_A:
      case IO_1706_2_B:
      case IO_1706_2_C:
      case IO_1706_2_D:
        dev = &dcb_dev;
        break;

      /*
       * 1706-A Channel #3
       */
      case IO_1706_3_A:
      case IO_1706_3_B:
      case IO_1706_3_C:
      case IO_1706_3_D:
        dev = &dcc_dev;
        break;

      default:
        return IO_INTERNALREJECT;
    }
    rtn = fw_doIO;
  }

  *device = dev;

  if (dev != NULL) {
    iod = (IO_DEVICE *)dev->ctxt;
    name = iod->iod_name != NULL ? iod->iod_name : dev->name;

    if ((dev->dctrl & DBG_DTRACE) != 0) {
      if (!FirstRejSeen) {
        /* Trace I/O before operation */
        if ((Qreg & IO_W) != 0)
          fprintf(DBGOUT,
                  "%s[%s: %s, A: %04X, Q: %04X (%04X/%04X), M: %04X, I: %c]\r\n",
                  INTprefix, name, output ? "OUT" : "INP", Areg, Qreg,
                  Qreg & IO_W, Qreg & (IO_EQUIPMENT | IO_COMMAND), Mreg,
                  INTflag ? '1' : '0');
        else fprintf(DBGOUT,
                     "%s[%s: %s, A: %04X, Q: %04X, M: %04X, I: %c]\r\n",
                     INTprefix, name, output ? "OUT" : "INP", Areg, Qreg,
                     Mreg, INTflag ? '1' : '0');
        if ((dev->dctrl & DBG_DSTATE) != 0) {
          if (iod->iod_state != NULL)
            (*iod->iod_state)("before", dev, iod);
        }
      }
    }

    if ((dev->dctrl & DBG_DLOC) != 0) {
      if (!FirstRejSeen) {
        /*
         * Trace location of the I/O instruction + instruction count
         */
        fprintf(DBGOUT, "%s[%s: P: %04X, Inst: %llu]\r\n",
                INTprefix, name, OrigPreg, Instructions);
      }
    }

    /*
     * Reject I/O requests from non-protected instructions to protected
     * devices unless it is a status register read.
     */
    if (inProtectedMode()) {
      if (!Protected) {
        if ((dev->flags & DEV_PROTECT) != 0) {
          if ((dev->flags & DEV_PROTECTED) == 0) {
            if (output || ((Qreg & iod->iod_rmask) != 1)) {
              if ((cpu_dev.dctrl & DBG_PROTECT) != 0) {
                fprintf(DBGOUT,
                        "%sProtect REJECT\r\n", INTprefix);
              }
              return IO_REJECT;
            }
          }
        }
      }
    }
  }
  result = rtn(dev, output);
  if (dev != NULL) {
    if ((dev->dctrl & DBG_DTRACE) != 0) {
      if (!FirstRejSeen || (result == IO_REPLY)) {
        /* Trace I/O after operation */
        if ((dev->dctrl & DBG_DSTATE) != 0) {
          if (iod->iod_state != NULL)
            (*iod->iod_state)("after", dev, iod);
        }
        if (output)
          fprintf(DBGOUT, "%s[%s: => %s]\r\n", 
                  INTprefix, name, status[result]);
        else fprintf(DBGOUT, "%s[%s: => %s, A: %04X]\r\n",
                     INTprefix, name, status[result], Areg);
      }
    }
  }
  return result;
}

/*
 * Default I/O routine for devices which are not present
 */
static enum IOstatus notPresent(DEVICE *dev, t_bool output)
{
  if ((cpu_dev.dctrl & DBG_MISSING) != 0) {
    fprintf(DBGOUT,
            "%sAccess to missing device (Q: %04X, Equipment: %2u)\r\n",
            INTprefix, Qreg, (Qreg & 0x7800) >> 7);
  }
  return IO_INTERNALREJECT;
}

/*
 * Build the I/O call table according to the enabled devices
 */
void buildIOtable(void)
{
  DEVICE *dptr;
  int i;

  /*
   * By default, all devices are marked "not present"
   */
  for (i = 0; i < 16; i++) {
    IOdev[i] = NULL;
    IOcall[i] = notPresent;
    IOintr[i] = noneINTR;
  }

  /*
   * Scan the device table and add equipment devices.
   */
  i = 0;
  while ((dptr = sim_devices[i++]) != NULL) {
    if ((dptr->flags & (DEV_NOEQUIP | DEV_DIS)) == 0) {
      IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

      IOdev[iod->iod_equip] = dptr;
      IOcall[iod->iod_equip] = fw_doIO;
      IOintr[iod->iod_equip] = 
        iod->iod_raised != NULL ? iod->iod_raised : deviceINTR;
    }
  }

  /*
   * Set up fixed equipment code devices
   */
  IOcall[1] = fw_doIO;
  IOintr[1] = dev1INTR;

  IOintr[0] = cpuINTR;
}

/*
 * Load bootstrap code into memory
 */
void loadBootstrap(uint16 *code, int len, uint16 base, uint16 start)
{
  while (len--) {
    M[base++] = *code++;
  }
  Preg = start;
}
