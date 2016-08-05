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

/* cdc1700_dc.c: CDC1700 Buffered data channel support
 *               Simh devices: dca, dcb, dcc
 */

#include "cdc1700_defs.h"

extern char INTprefix[];

extern uint16 Areg, Preg, Qreg, IOAreg, IOQreg, M[];

extern t_bool IOFWinitialized;

extern DEVICE *IOdev[];
extern UNIT cpu_unit;

extern uint16 LoadFromMem(uint16);
extern t_bool IOStoreToMem(uint16, uint16, t_bool);

extern void rebuildPending(void);
extern void RaiseExternalInterrupt(DEVICE *);

extern IO_DEVICE *fw_findChanDevice(IO_DEVICE *, uint16);
extern enum IOstatus fw_doIO(DEVICE *, t_bool);
extern enum IOstatus fw_doBDCIO(IO_DEVICE *, uint16 *, t_bool, uint8);

extern uint16 LoadFromMem(uint16);
extern t_bool IOStoreToMem(uint16, uint16, t_bool);

t_stat set_intr(UNIT *uptr, int32 val, CONST char *, void *);
t_stat show_intr(FILE *, UNIT *, int32, CONST void *);
t_stat show_target(FILE *, UNIT *, int32, CONST void *);

t_stat dc_svc(UNIT *);
t_stat dc_reset(DEVICE *);

void DCstate(const char *, DEVICE *, IO_DEVICE *);
t_bool DCreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus DCin(IO_DEVICE *, uint8);
enum IOstatus DCout(IO_DEVICE *, uint8);

t_stat dc_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1706-A Buffered Data Channel

    Addresses (A maximum of 3 1706-A's may be attached to a 1700 series system)

                                        Computer Instruction
    Q Register                  Output From A           Input To A
    (Bits 11-15)

    #1     #2     #3
  00010  00111  01100           Direct Output           Direct Input
  00011  01000  01101           Function                Terminate Buffer
  00100  01001  01110           Buffered Output         1706-A Status
  00101  01010  01111           Buffered Input          1706-A Current Address

  Operations:

  Function

    15  14                                                   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   |                                                   |   |
     |   +---------------------------------------------------+   EOP Interrupt
     |                             |                             Request
     |                             Not defined
     Set/Clear condition bits 0 - 14

  Status Response:

  Status

    15                  10           7       5       3           0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X |   |   | X |   | X |   | X |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             |   |       |       |       |   |   |
                             |   |       |       |       |   |   Ready
                             |   |       |       |       |   Busy
                             |   |       |       |       Interrupt
                             |   |       |       End of Operation
                             |   |       Program Protect Fault
                             |   Device Reject
                             Device Reply
 */

IO_DEVICE DCAdev = IODEV(NULL, "1706-A", DC, 0, 0xFF, IO_1706_1_A,
                         DCreject, DCin, DCout, NULL, NULL,
                         DCstate, NULL, NULL, NULL,
                         0x7F, 4,
                         MASK_REGISTER0 | MASK_REGISTER1 |      \
                         MASK_REGISTER2 | MASK_REGISTER3,
                         MASK_REGISTER2, 0, 0, DEVICE_DC, 0, NULL);

IO_DEVICE DCBdev = IODEV(NULL, "1706-A", DC, 0, 0xFF, IO_1706_2_A,
                         DCreject, DCin, DCout, NULL, NULL,
                         DCstate, NULL, NULL, NULL,
                         0x7F, 4,
                         MASK_REGISTER0 | MASK_REGISTER1 |      \
                         MASK_REGISTER2 | MASK_REGISTER3,
                         MASK_REGISTER2, 0, 0, DEVICE_DC, 0, NULL);

IO_DEVICE DCCdev = IODEV(NULL, "1706-A", DC, 0, 0xFF, IO_1706_3_A,
                         DCreject, DCin, DCout, NULL, NULL,
                         DCstate, NULL, NULL, NULL,
                         0x7F, 4,
                         MASK_REGISTER0 | MASK_REGISTER1 |      \
                         MASK_REGISTER2 | MASK_REGISTER3,
                         MASK_REGISTER2, 0, 0, DEVICE_DC, 0, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_lastIO      iod_private
#define iod_target      iod_private2
#define iod_svcstate    iod_private3
#define iod_CWA         iod_private6
#define iod_LWA         iod_private7
#define iod_nextAddr    iod_private8
#define iod_reg         iod_private9

/*
 * Define current state of the 1706-A with respect to the Direct Storage
 * Access Bus.
 */
#define IO_BDC_IDLE     0x00
#define IO_BDC_STARTR   0x01            /* Start read sequence */
#define IO_BDC_STARTW   0x02            /* Start write sequence */
#define IO_BDC_READING  0x03            /* Read sequence in progress */
#define IO_BDC_WRITING  0x04            /* Write sequence in  progress */
#define IO_BDC_DONE     0x05            /* Transfer has completed */

/* Buffered Data Channel (DC) data structures

   dca_dev      DC device descriptor
   dcb_dev      DC device descriptor
   dcc_dev      DC device descriptor
   dca_unit     DC unit
   dcb_unit     DC unit
   dcc_unit     DC unit
   dc_reg       DC register list
   dc_mod       DC modifier list
*/
UNIT dca_unit[] = {
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) }
};
UNIT dcb_unit[] = {
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) }
};
UNIT dcc_unit[] = {
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) },
  {UDATA(&dc_svc, UNIT_DISABLE, 0) }
};

REG dca_reg[] = {
  { HRDATAD(STATUS, DCAdev.iod_readR[2], 16, "1706 Status") },
  { HRDATAD(CWA, DCAdev.iod_CWA, 16, "1706 Current Address") },
  { HRDATAD(NEXT, DCAdev.iod_nextAddr, 16, "Next transfer address") },
  { HRDATAD(LWA, DCAdev.iod_LWA, 16, "Last word address") },
  { HRDATAD(IENABLE, DCAdev.IENABLE, 16, "Interrupt enabled") },
  { NULL }
};

REG dcb_reg[] = {
  { HRDATAD(STATUS, DCBdev.iod_readR[2], 16, "1706 Status") },
  { HRDATAD(CWA, DCBdev.iod_CWA, 16, "1706 Current Address") },
  { HRDATAD(NEXT, DCBdev.iod_nextAddr, 16, "Next transfer address") },
  { HRDATAD(LWA, DCBdev.iod_LWA, 16, "Last word address") },
  { HRDATAD(IENABLE, DCBdev.IENABLE, 16, "Interrupt enabled") },
  { NULL }
};

REG dcc_reg[] = {
  { HRDATAD(STATUS, DCCdev.iod_readR[2], 16, "1706 Status") },
  { HRDATAD(CWA, DCCdev.iod_CWA, 16, "1706 Current Address") },
  { HRDATAD(NEXT, DCCdev.iod_nextAddr, 16, "Next transfer address") },
  { HRDATAD(LWA, DCCdev.iod_LWA, 16, "Last word address") },
  { HRDATAD(IENABLE, DCCdev.IENABLE, 16, "Interrupt enabled") },
  { NULL }
};

MTAB dc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "1706-A Buffered Data Channel" },
  { MTAB_XTD|MTAB_VDV, 0, "TARGET", NULL,
    NULL, &show_target, NULL, "Display devices attached to the data channel" },
  { MTAB_XTD|MTAB_VDV, 0, "INTERRUPT", "INTERRUPT=hexValue",
    &set_intr, &show_intr, NULL, "Display data channel interrupt" },
  { 0 }
};

DEBTAB dc_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "LOCATION",    DBG_DLOC,       "Display address for I/O instructions" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DINTR | DBG_DLOC },
  { NULL }
};

DEVICE dca_dev = {
  "DCA", dca_unit, dca_reg, dc_mod,
  0, 16, 16, 1, 16, 16,
  NULL, NULL, &dc_reset,
  NULL, NULL, NULL,
  &DCAdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV | DEV_OUTDEV, 0, dc_deb,
  NULL, NULL, &dc_help, NULL, NULL, NULL
};

DEVICE dcb_dev = {
  "DCB", dcb_unit, dcb_reg, dc_mod,
  0, 16, 16, 1, 16, 16,
  NULL, NULL, &dc_reset,
  NULL, NULL, NULL,
  &DCBdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV | DEV_OUTDEV, 0, dc_deb,
  NULL, NULL, &dc_help, NULL, NULL, NULL
};

DEVICE dcc_dev = {
  "DCC", dcc_unit, dcc_reg, dc_mod,
  0, 16, 16, 1, 16, 16,
  NULL, NULL, &dc_reset,
  NULL, NULL, NULL,
  &DCCdev,
  DEV_DEBUG | DEV_NOEQUIP | DEV_INDEV | DEV_OUTDEV, 0, dc_deb,
  NULL, NULL, &dc_help, NULL, NULL, NULL
};

static DEVICE *dc_devices[IO_1706_MAX] = {
  &dca_dev, &dcb_dev, &dcc_dev
};

/*
 * Dump the current state of the Buffered Data Channel.
 */
const char *DCstateStr[] = {
  "Idle", "StartR", "StartW", "Read", "Write", "Done"
};

void DCstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: Sta: %04X, %s, ena: %04X, cur: %04X, next: %04X, last: %04X, reg: %d]\r\n",
          INTprefix, dev->name, where,
          DCSTATUS(iod), DCstateStr[iod->iod_svcstate], ENABLED(iod),
          iod->iod_CWA, iod->iod_nextAddr, iod->iod_LWA, iod->iod_reg);
}

/*
 * Display device description.
 */
static const char *description(DEVICE *dptr)
{
  return "1706-A";
}

/*
 * Unit service
 */
t_stat dc_svc(UNIT *uptr)
{
  DEVICE *dptr;
  enum IOstatus status;
  uint16 temp;

  if ((dptr = find_dev_from_unit(uptr)) != NULL) {
    IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;
    IO_DEVICE *target = (IO_DEVICE *)iod->iod_target;

    if ((dptr->dctrl & DBG_DSTATE) != 0)
      DCstate("dc_svc() entry", iod->iod_indev, iod);

    switch (iod->iod_svcstate) {
      case IO_BDC_IDLE:
        return SCPE_OK;

      case IO_BDC_STARTR:
      case IO_BDC_STARTW:
        if ((dptr->dctrl & DBG_DTRACE) != 0)
          fprintf(DBGOUT,
                  "%s%s - Start %s on %s, current: %04X, last: %04X\r\n",
                  INTprefix, dptr->name,
                  iod->iod_svcstate == IO_BDC_STARTR ? "input" : "output",
                  target == NULL ? "no device" : target->iod_indev->name,
                  iod->iod_CWA, iod->iod_LWA);

        iod->iod_svcstate =
          iod->iod_svcstate == IO_BDC_STARTR ? IO_BDC_READING : IO_BDC_WRITING;
        sim_activate(uptr, DC_IO_WAIT);

        if ((dptr->dctrl & DBG_DSTATE) != 0)
          DCstate("dc_svc() - started", iod->iod_indev, iod);

        return SCPE_OK;

      case IO_BDC_READING:
        if (target != NULL) {
          if ((target->STATUS & IO_ST_EOP) != 0)
            goto ioDone;

          if (iod->iod_CWA == iod->iod_LWA) {
            /*
             * Transfer complete - complete status change and, optionally,
             * generate interrupt.
             */
            iod->iod_svcstate = IO_BDC_DONE;
            sim_activate(uptr, DC_EOP_WAIT);
            
            if ((dptr->dctrl & DBG_DSTATE) != 0)
              DCstate("dc_svc() - read complete", iod->iod_indev, iod);

            return SCPE_OK;
          }

          DCSTATUS(iod) &= ~(IO_1706_REPLY | IO_1706_REJECT);
          iod->iod_nextAddr = iod->iod_CWA + 1;

          status = fw_doBDCIO(target, &temp, FALSE, iod->iod_reg);

          switch (status) {
            case IO_REPLY:
              DCSTATUS(iod) |= IO_1706_REPLY;
              if (!IOStoreToMem(iod->iod_CWA, temp, TRUE)) {
                DCSTATUS(iod) |= IO_1706_PROT;
                /*** TODO: Signal protect fault ***/
              }
              iod->iod_CWA++;

              if ((dptr->dctrl & DBG_DTRACE) != 0)
                fprintf(DBGOUT,
                        "%s%s - Read %04X\r\n",
                        INTprefix, dptr->name, temp);
              break;

            case IO_REJECT:
            case IO_INTERNALREJECT:
              DCSTATUS(iod) |= IO_1706_REJECT;
              break;
          }
        } else DCSTATUS(iod) |= IO_1706_REJECT;
        sim_activate(uptr, DC_IO_WAIT);

        if ((dptr->dctrl & DBG_DSTATE) != 0)
          DCstate("dc_svc() - reading", iod->iod_indev, iod);

        return SCPE_OK;

      case IO_BDC_WRITING:
        if (target != NULL) {
          if ((target->STATUS & IO_ST_EOP) != 0)
            goto ioDone;

          if (iod->iod_CWA == iod->iod_LWA) {
            /*
             * Transfer complete - complete status change and, optionally,
             * generate interrupt.
             */
            iod->iod_svcstate = IO_BDC_DONE;
            sim_activate(uptr, DC_EOP_WAIT);
            
            if ((dptr->dctrl & DBG_DSTATE) != 0)
              DCstate("dc_svc() - write complete", iod->iod_indev, iod);
            
            return SCPE_OK;
          }

          DCSTATUS(iod) &= ~(IO_1706_REPLY | IO_1706_REJECT);
          iod->iod_nextAddr = iod->iod_CWA + 1;

          temp = LoadFromMem(iod->iod_CWA);
          status = fw_doBDCIO(target, &temp, TRUE, iod->iod_reg);

          switch (status) {
            case IO_REPLY:
              DCSTATUS(iod) |= IO_1706_REPLY;
              iod->iod_CWA++;
              break;

            case IO_REJECT:
            case IO_INTERNALREJECT:
              DCSTATUS(iod) |= IO_1706_REJECT;
              break;
          }
        } else DCSTATUS(iod) |= IO_1706_REJECT;
        sim_activate(uptr, DC_IO_WAIT);

        if ((dptr->dctrl & DBG_DSTATE) != 0)
          DCstate("dc_svc() - writing", iod->iod_indev, iod);

        return SCPE_OK;

      case IO_BDC_DONE:
        /*
         * The transfer has completed as far as the 1706-A is concerned.
         */
    ioDone:
        iod->iod_svcstate = IO_BDC_IDLE;

        DCSTATUS(iod) |= IO_ST_EOP;
        DCSTATUS(iod) &= ~IO_ST_BUSY;

        if (ISENABLED(iod, IO_DIR_EOP) && (iod->iod_equip != 0)) {
          DEVICE *dptr = iod->iod_indev;
          
          if ((dptr->dctrl & DBG_DINTR) != 0)
            fprintf(DBGOUT,
                    "%s%s - Generate EOP interrupt\r\n",
                    INTprefix, dptr->name);
          DCSTATUS(iod) |= IO_ST_INT;
          RaiseExternalInterrupt(dptr);
        }

        if ((dptr->dctrl & DBG_DSTATE) != 0)
          DCstate("dc_svc() - EOP set", iod->iod_indev, iod);

        return SCPE_OK;
    }
  }
  return SCPE_NXDEV;
}

/*
 * Reset routine
 */
t_stat dc_reset(DEVICE *dptr)
{
  IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

  DEVRESET(iod);

  DCSTATUS(iod) = IO_ST_READY;

  return SCPE_OK;
}

/*
 * Set the interrupt level for a buffered data channel.
 */
t_stat set_intr(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  IO_DEVICE *iod = (IO_DEVICE *)uptr->up7;
  t_value v;
  t_stat r;

  if (cptr == NULL)
    return SCPE_ARG;

  v = get_uint(cptr, DEV_RDX, 15, &r);
  if (r != SCPE_OK)
    return r;
  if (v == 0)
    return SCPE_ARG;

  iod->iod_equip = v;
  iod->iod_interrupt = 1 << v;
  return SCPE_OK;
}

/*
 * Display the current interrupt level.
 */
t_stat show_intr(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  IO_DEVICE *iod = (IO_DEVICE *)uptr->up7;

  if (iod->iod_equip != 0) {
    fprintf(st, "Interrupt: ");
    fprint_val(st, (t_value)iod->iod_equip, DEV_RDX, 8, PV_LEFT);
  } else fprintf(st, "Interrupt: None");
  return SCPE_OK;
}

/*
 * Display buffered data channel target device and equipment address
 */
t_stat show_target(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  IO_DEVICE *iod;

  if (uptr == NULL)
    return SCPE_IERR;

  if ((iod = (IO_DEVICE *)uptr->up8) != NULL) {
    DEVICE *dptr = iod->iod_indev;

    fprintf(st, "\n\tTarget: %s (%s), Equip: %d",
            sim_dname(dptr), iod->iod_model, iod->iod_equip);
  }
  return SCPE_OK;
}

/*
 * Check if I/O should be rejected. I/O allowed if:
 *
 * Reg.                 Write (OUT)                     Read (INP)
 *
 *  00          Not busy                        Not busy
 *  01          Not busy                        Always allowed
 *  02          Not busy                        Always allowed
 *  03          Not busy                        Always allowed
 */
t_bool DCreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  if (output || (reg == 0))
    return (DCSTATUS(iod) & IO_ST_BUSY) != 0;

  return FALSE;
}

/*
 * Start a buffered data channel transfer. Note that target may be NULL if
 * an attempt is made to access a device which is not connected to the
 * buffered data channel. We need to delay starting the transaction so that
 * there is sufficient time to grab the current bufferered data channel
 * status and terminate the transfer before starting the actual transfer.
 * The diagnostics check for this particular case.
 */
enum IOstatus DCxfer(IO_DEVICE *iod, IO_DEVICE *target, t_bool output)
{
  DEVICE *dptr = (DEVICE *)iod->iod_indev;

  iod->iod_LWA = LoadFromMem(IOAreg);
  iod->iod_CWA = iod->iod_nextAddr = ++IOAreg;
  iod->iod_target = target;
  if (target != NULL)
    iod->iod_reg = IOQreg & target->iod_rmask;

  DCSTATUS(iod) &= ~IO_ST_EOP;
  DCSTATUS(iod) |= IO_ST_BUSY;

  iod->iod_svcstate = output ? IO_BDC_STARTW : IO_BDC_STARTR;
  sim_activate(&dptr->units[0], DC_START_WAIT);

  if ((dptr->dctrl & DBG_DTRACE) != 0)
    fprintf(DBGOUT,
            "%s%s - starting %s transfer, cur: %04X, last: %04X\r\n",
            INTprefix, dptr->name, output ? "output" : "input",
            iod->iod_CWA, iod->iod_LWA);

  return IO_REPLY;
}

/*
 * Perform a buffered data channel input operation
 */
enum IOstatus DCin(IO_DEVICE *iod, uint8 reg)
{
  IO_DEVICE *target;
  enum IOstatus status;

  /*
   * If the "Continue" bit is set in Q, use the last I/O address and treat the
   * request as a direct input/output operation.
   */
  if ((IOQreg & IO_CONTINUE) != 0) {
    IOQreg = iod->iod_lastIO;
    reg = 0;
  } else iod->iod_lastIO = IOQreg;

  /*
   * The framework filters out INP requests for the status register.
   */
  switch (reg) {
    /*
     * Perform a direct input request from the target device.
     */
    case 0x00:
      /*
       * Find the target device to be used.
       */
      if ((target = fw_findChanDevice(iod, IOQreg)) == NULL)
        return IO_REJECT;

      if ((target->iod_indev->dctrl & DBG_DSTATE) != 0)
        if (target->iod_state != NULL)
          (*target->iod_state)("before direct in", target->iod_indev, target);

      status = fw_doIO(target->iod_indev, FALSE);

      if ((target->iod_indev->dctrl & DBG_DSTATE) != 0)
        if (target->iod_state != NULL)
          (*target->iod_state)("after direct in", target->iod_indev, target);

      return status;

    /*
     * Terminate buffer, 1706 Current Address.
     */
    case 0x01:
      iod->iod_svcstate = IO_BDC_IDLE;
      DCSTATUS(iod) &= ~IO_ST_BUSY;
      /* FALLTHROUGH */

    /*
     * 1706 Current Address. May be the next address depending on where we
     * are in the actual transfer sequence.
     */
    case 0x03:
      Areg = iod->iod_nextAddr;
      return IO_REPLY;
  }
  return IO_REJECT;
}

/*
 * Perform a buffered data channel output operation
 */
enum IOstatus DCout(IO_DEVICE *iod, uint8 reg)
{
  IO_DEVICE *target;
  enum IOstatus status;

  /*
   * If the "Continue" bit is set in Q, use the last I/O address and treat the
   * request as a direct input/output operation.
   */
  if ((IOQreg & IO_CONTINUE) != 0) {
    IOQreg = iod->iod_lastIO;
    reg = 0;
  } else iod->iod_lastIO = IOQreg;

  /*
   * Find the target device to be used. If the target device is not connected
   * to the buffered data channel, the REJECT will eventually be processed
   * in dc_svc().
   */
  target = fw_findChanDevice(iod, IOQreg);

  if ((target == NULL) && (reg == 0x00))
    return IO_REJECT;

  switch (reg) {
    /*
     * Perform a direct output request to the target device.
     */
    case 0x00:
      if ((target->iod_indev->dctrl & DBG_DSTATE) != 0)
        if (target->iod_state != NULL)
          (*target->iod_state)("before direct out", target->iod_indev, target);

      status = fw_doIO(target->iod_indev, TRUE);

      if ((target->iod_indev->dctrl & DBG_DSTATE) != 0)
        if (target->iod_state != NULL)
          (*target->iod_state)("after direct out", target->iod_indev, target);

      return status;

    /*
     * Command function to the 1706-A.
     */
    case 0x01:
      if ((IOAreg & IO_1706_EOP) != 0) {
        iod->OLDIENABLE = iod->IENABLE;
        if ((IOAreg & IO_1706_SET) != 0)
          iod->IENABLE |= IO_DIR_EOP;
        else iod->IENABLE &= ~IO_DIR_EOP;

        DCSTATUS(iod) &= ~(IO_ST_INT | IO_ST_EOP);
        rebuildPending();
      }
      return IO_REPLY;

    /*
     * Initiate buffered output on the 1706-A.
     */
    case 0x02:
      return DCxfer(iod, target, TRUE);

    /*
     * Initiate buffered input on the 1706-A.
     */
    case 0x03:
      return DCxfer(iod, target, FALSE);
  }
  return IO_REJECT;
}

/*
 * Build the buffered data channel tables.
 */
void buildDCtables(void)
{
  int i;
  uint8 chan;
  DEVICE *dptr;
  UNIT *uptr;

  dca_dev.numunits = 0;
  dcb_dev.numunits = 0;
  dcc_dev.numunits = 0;

  dca_dev.units[0].up7 = &DCAdev;
  dcb_dev.units[0].up7 = &DCBdev;
  dcc_dev.units[0].up7 = &DCCdev;

  for (i = 0; i < 16; i++) {
    if ((dptr = IOdev[i]) != NULL) {
      IO_DEVICE *iod = (IO_DEVICE *)dptr->ctxt;

      if (((chan = iod->iod_dc) != 0) &&
          ((iod->iod_flags & AQ_ONLY) == 0)) {
        dptr = dc_devices[IDX_FROM_CHAN(chan)];
        uptr = &dptr->units[dptr->numunits];
        if (dptr->numunits < IO_1706_DEVS) {
          uptr->up8 = iod;
          dptr->numunits++;
        }
      }
    }
  }
}

/*
 * Create bit map of interrupts asserted by the Buffered Data Channels.
 */
uint16 dcINTR(void)
{
  uint16 result = 0;

  if ((DCSTATUS(&DCAdev) & IO_ST_INT) != 0)
    result |= DCAdev.iod_interrupt;
  if ((DCSTATUS(&DCBdev) & IO_ST_INT) != 0)
    result |= DCBdev.iod_interrupt;
  if ((DCSTATUS(&DCCdev) & IO_ST_INT) != 0)
    result |= DCCdev.iod_interrupt;

  return result;
}

t_stat dc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    " The %D device is a 1706-A buffered data channel.\n"
    "1 Hardware Description\n"
    " The 1706-A consists of a controller which can control up to 8 other\n"
    " controllers to provide them with direct memory access. Up to 3\n"
    " 1706-A's may be connected to the CPU. All 3 buffered data channels are\n"
    " available in the simulator but only channel 1 (DCA) is connected to a\n"
    " peripheral (the magtape controller) and only if that controller is\n"
    " configured as a 1732-A. Unlike actual hardware, the simulator allows\n"
    " access to the magtape controller either through the A/Q channel or\n"
    " through a 1706-A.\n\n"
    " By default, the simulator does not assign an interrupt to a 1706-A. An\n"
    " interrupt may be assigned with the command:\n\n"
    "+sim> SET %D INTERRUPT=hexValue\n"
    "2 Equipment Address\n"
    " Unlike most peripherals, buffered data channels use private addresses\n"
    " outside the normal 1 - 15 address range.\n"
    "2 $Registers\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET commands\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}
