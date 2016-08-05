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

/* cdc1700_lp.c: 1740 and 1742-30 line printer support
 *               Simh devices: lp
 */
#include "cdc1700_defs.h"

#define COLUMNS         136

#define DEVTYPE_1740    IOtype_dev1             /* Device is 1740 */
#define DEVTYPE_1742    IOtype_dev2             /* Device is 1742-30 */

#define FUNCTION2       iod_writeR[3]           /* 1740 only */

/*
 * Printer mapping table. Maps from the 7-bit device character set to 8-bit
 * ASCII. If The mapping is 0xFF, the character is illegal and results in the
 * ALARM status bit being raised.
 */
uint8 LPmap[128] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  ' ',  '!',  '"',  '#',  '$',  '%',  '&',  '\'',
  '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
  '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
  '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
  '@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',
  'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
  'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
  'X',  'Y',  'Z',  '[',  '\\', ']',  '^',  '_',
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

extern char INTprefix[];

extern void fw_IOalarm(t_bool, DEVICE *, IO_DEVICE *, const char *);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_IOunderwayData(IO_DEVICE *, uint16);
extern void fw_IOcompleteData(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOunderwayEOP(IO_DEVICE *, uint16);
extern void fw_IOcompleteEOP(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_setForced(IO_DEVICE *, uint16);
extern void fw_clearForced(IO_DEVICE *, uint16);

extern void RaiseExternalInterrupt(DEVICE *);

extern t_bool doDirectorFunc(DEVICE *, t_bool);

extern t_stat checkReset(DEVICE *, uint8);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_stoponrej(UNIT *, int32, CONST char *, void *);
extern t_stat clr_stoponrej(UNIT *, int32, CONST char *, void *);

extern t_stat set_protected(UNIT *, int32, CONST char *, void *);
extern t_stat clear_protected(UNIT *, int32, CONST char *, void *);

extern t_stat set_equipment(UNIT *, int32, CONST char *, void *);

extern uint16 Areg, IOAreg;

extern t_bool IOFWinitialized;

t_stat lp_show_type(FILE *, UNIT *, int32, CONST void *);
t_stat lp_set_type(UNIT *, int32, CONST char *, void *);

t_stat lp_svc(UNIT *);
t_stat lp_reset(DEVICE *);

void LPstate(const char *, DEVICE *, IO_DEVICE *);
enum IOstatus LPin(IO_DEVICE *, uint8);
enum IOstatus LPout(IO_DEVICE *, uint8);

uint8 LPbuf[COLUMNS];

t_stat lp_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1740, 1742-30 Line Printer

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A
  (Bits 01-00)

       00               Write
       01               Director Function 1  Director Status
       11               Director Function 2

  Notes:
  1. The documentation is incorrect about the location of Director Status.
     Confirmed by the SMM17 LP1 diagnostic code.

  2. Device register 3 (Director Function 2) is only present on the 1740
     Controller.

  Operations:

  Director Function 1

    15                                       5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X | X |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                             |   |   |   |   |   |
                                             |   |   |   |   |   Clr Printer
                                             |   |   |   |   Clr Interrupts
                                             |   |   |   Data Interrupt Req.
                                             |   |   EOP Interrupt Req.
                                             |   Interrupt on Alarm
                                             Print (1742-30 only)

  Director Function 2 (1740 only)

    15  14  13          10   9   8   7   6   5   4   3   2   1   0 
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X |   | X | X | X | X |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
         |                   |   |   |   |   |   |   |   |   |   |
         |                   |   |   |   |   |   |   |   |   |   Print
         |                   |   |   |   |   |   |   |   |   Single Space
         |                   |   |   |   |   |   |   |   Double Space
         |                   |   |   |   |   |   |   Level 1
         |                   |   |   |   |   |   Level 2
         |                   |   |   |   |   Level 3
         |                   |   |   |   Level 4
         |                   |   |   Level 5
         |                   |   Level 6
         |                   Level 7
         Level 12

  Status Response:

  Director Status

    15                           8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                 |   |   |   |   |   |   |   |   |
                                 |   |   |   |   |   |   |   |   Ready
                                 |   |   |   |   |   |   |   Busy
                                 |   |   |   |   |   |   Interrupt
                                 |   |   |   |   |   Data
                                 |   |   |   |   End of Operation
                                 |   |   |   Alarm
                                 |   |   Error (1742-30 only)
                                 |   Protected
                                 Load Image (1742 only)
*/

IO_DEVICE LPdev = IODEV(NULL, "Line Printer", 1740, 4, 0xFF, 0,
                        fw_reject, LPin, LPout, NULL, NULL,
                        NULL, NULL, NULL, NULL,
                        0x7F, 4,
                        MASK_REGISTER0 | MASK_REGISTER1 | MASK_REGISTER3,
                        MASK_REGISTER1,
                        MASK_REGISTER0 | MASK_REGISTER2, MASK_REGISTER2,
                        0, 0, LPbuf);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_LPstate     iod_private
#define iod_LPbuffer    iod_private2
#define iod_LPcolumn    iod_private3
#define iod_LPccpend    iod_private4            /* 1742-30 only */
#define iod_LPoverwrite iod_private10           /* 1740 only */

/*
 * Current state of the device.
 */
#define IODP_LPNONE             0x0000
#define IODP_LPCHARWAIT         0x0001
#define IODP_LPPRINTWAIT        0x0002
#define IODP_LPCCWAIT           0x0003

/* LP data structures

   lp_dev       LP device descriptor
   lp_unit      LP unit descriptor
   lp_reg       LP register list
   lp_mod       LP modifiers list
*/

UNIT lp_unit = {
  UDATA(&lp_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), LP_OUT_WAIT
};

REG lp_reg_1740[] = {
  { HRDATAD(FUNCTION, LPdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(FUNCTION2, LPdev.FUNCTION2, 16, "Last VFC function issued") },
  { HRDATAD(STATUS, LPdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, LPdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

REG lp_reg_1742[] = {
  { HRDATAD(FUNCTION, LPdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, LPdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, LPdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

MTAB lp_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "TYPE", "TYPE={1740|1742}",
    &lp_set_type, &lp_show_type, NULL, "Display printer controller type" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation is I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)" },
  { 0 }
};

/*
 * LP debug flags
 */
#define DBG_V_CC        (DBG_SPECIFIC+0)/* Carriage control characters */

#define DBG_CC          (1 << DBG_V_CC)

DEBTAB lp_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "CC",          DBG_CC,         "Display carriage control requests" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DINTR | DBG_DLOC },
  { NULL }
};

DEVICE lp_dev = {
  "LP", &lp_unit, NULL, lp_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &lp_reset,
  NULL, NULL, NULL,
  &LPdev,
  DEV_DEBUG | DEV_DISABLE | DEV_OUTDEV | DEV_PROTECT, 0, lp_deb,
  NULL, NULL, &lp_help, NULL, NULL, NULL
};

t_stat lp_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  switch (LPdev.iod_type) {
    case DEVTYPE_1740:
      fprintf(st, "1740 Line Printer Controller");
      break;

    case DEVTYPE_1742:
      fprintf(st, "1742-30 Line Printer Controller");
      break;

    default:
      return SCPE_IERR;
  }
  return SCPE_OK;
}

t_stat lp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (!cptr)
    return SCPE_IERR;
  if ((uptr->flags & UNIT_ATT) != 0)
    return SCPE_ALATT;

  if (!strcmp(cptr, "1740")) {
    LPdev.iod_type = DEVTYPE_1740;
    LPdev.iod_model = "1740";
    lp_dev.registers = lp_reg_1740;
  } else {
    if (!strcmp(cptr, "1742") ||
        !strcmp(cptr, "1742-30")) {
      LPdev.iod_type = DEVTYPE_1742;
      LPdev.iod_model = "1742-30";
      lp_dev.registers = lp_reg_1742;
    } else return SCPE_ARG;
  }
  return SCPE_OK;
}

/*
 * Dump the current internal state of the LP device.
 */
const char *LPprivateState[4] = {
  "", "CHARWAIT", "PRINTWAIT", "CCWAIT"
};

void LPstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Func2: %04X, Sta: %04X, Ena: %04x, Count: %d, \
Private: %s%s\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->FUNCTION2, iod->STATUS, iod->IENABLE,
          iod->iod_LPcolumn,
          LPprivateState[iod->iod_LPstate],
          iod->iod_LPoverwrite ? ",Overwrite" : "");
}

/* Unit service */

t_stat lp_svc(UNIT *uptr)
{
  if ((lp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[LP: lp_svc() entry]\r\n", INTprefix);
    if ((lp_dev.dctrl & DBG_DSTATE) != 0)
      LPstate("svc_entry", &lp_dev, &LPdev);
  }

  switch (LPdev.iod_LPstate) {
    case IODP_LPCHARWAIT:
      /*
       * Generate an interrupt indicating that the device can accept more
       * characters.
       */
      LPdev.iod_LPstate = IODP_LPNONE;

      fw_IOcompleteData(FALSE, &lp_dev, &LPdev, 0xFFFF, "Output done");
      break;

    case IODP_LPPRINTWAIT:
      /*
       * Generate an interrupt indicating that the print/motion operation
       * has completed.
       */
      LPdev.iod_LPstate = IODP_LPNONE;
      if (LPdev.iod_type == DEVTYPE_1742)
        LPdev.iod_LPccpend = TRUE;

      fw_IOcompleteEOP(FALSE, &lp_dev, &LPdev, 0xFFFF, "EOP interrupt");
      break;

    case IODP_LPCCWAIT:
      /*
       * Generate an interrupt indicating that the motion operation has
       * completed.
       */
      LPdev.iod_LPstate = IODP_LPNONE;
      LPdev.iod_LPccpend = FALSE;

      fw_IOcompleteData(FALSE, &lp_dev, &LPdev, 0xFFFF, "Control Char. Done");
      break;

    default:
      return SCPE_IERR;
  }

  if ((lp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[LP: lp_svc() exit]\r\n", INTprefix);
    if ((lp_dev.dctrl & DBG_DSTATE)  != 0)
      LPstate("svc exit", &lp_dev, &LPdev);
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat lp_reset(DEVICE *dptr)
{
  t_stat r;

  if (LPdev.iod_type == IOtype_default) {
    /*
     * Setup the default device type.
     */
    LPdev.iod_type = DEVTYPE_1740;
    LPdev.iod_model = "1740";
    lp_dev.registers = lp_reg_1740;
  }

  if (IOFWinitialized)
    if ((dptr->flags & DEV_DIS) == 0)
      if ((r = checkReset(dptr, LPdev.iod_equip)) != SCPE_OK)
        return r;

  DEVRESET(&LPdev);

  LPdev.iod_LPcolumn = 0;
  if (LPdev.iod_type == DEVTYPE_1742)
    LPdev.iod_LPccpend = TRUE;

  fw_setForced(&LPdev, IO_ST_READY);

  LPdev.STATUS |= IO_ST_DATA | IO_ST_EOP;

  return SCPE_OK;
}

/* Perform I/O */

enum IOstatus LPin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The framework only passes IN operations for the data register.
   */
  return IO_REJECT;
}

enum IOstatus LPout(IO_DEVICE *iod, uint8 reg)
{
  uint8 *buffer = (uint8 *)iod->iod_LPbuffer;
  t_bool printwait = FALSE, changed;

  /*
   * 1742-30 does not have a register 3
   */
  if (reg == 0x03)
    if (LPdev.iod_type == DEVTYPE_1742)
      return IO_REJECT;

  if ((lp_dev.dctrl & DBG_DSTATE) != 0)
    LPstate("before", &lp_dev, &LPdev);

  switch (reg) {
    case 0x00:
      if (iod->iod_LPcolumn < COLUMNS) {
        if (iod->iod_type == DEVTYPE_1740) {
          uint8 ch1, ch2;

          ch1 = (Areg >> 8) & 0x7F;
          ch2 = Areg & 0x7F;

          if ((LPmap[ch1] == 0xFF) || (LPmap[ch2] == 0xFF)) {
            if ((lp_dev.dctrl & DBG_DTRACE) != 0)
              fprintf(DBGOUT,
                      "%sLP: Invalid code (0x%02x, 0x%02x) ==> [0x%02x, 0x%02x]\r\\n",
                      INTprefix, ch1, ch2, LPmap[ch1], LPmap[ch2]);
            fw_IOalarm(FALSE, &lp_dev, iod, "Invalid code");
            break;
          }
          /*
           * Put both characters in the print buffer.
           */
          buffer[iod->iod_LPcolumn++] = LPmap[ch1];
          buffer[iod->iod_LPcolumn++] = LPmap[ch2];
        }

        if (iod->iod_type == DEVTYPE_1742) {
          uint8 ch = LPmap[Areg & 0x7F];

          /*
           * If this is the first character after a "Print" command, it
           * controls the vertical motion of the paper.
           * TODO: Implement format tape operations.
           *       For now, all format tape operations generate a single
           *       space motion
           */
          if (LPdev.iod_LPccpend) {
            const char *ccontrol = "\n";
            
            if ((lp_dev.dctrl & DBG_CC) != 0)
              fprintf(DBGOUT, "%s[LP: CC: 0x%02X]\r\n", INTprefix, Areg);

            if ((Areg & 0x40) == 0) {
              switch (Areg & 0x03) {
                case 0x0:
                  ccontrol = "\r";
                  break;

                case 0x1:
                  ccontrol = "\n";
                  break;

                case 0x2:
                  ccontrol = "\n\n";
                  break;

                case 0x3:
                  ccontrol = "\n\n\n";
                  break;
              }
            } else {
              /*** TODO: implement format tape decoding ***/
            }

            if ((lp_unit.flags & UNIT_ATT) != 0) {
              if (fputs(ccontrol, lp_unit.fileref) == EOF) {
                perror("LP I/O error");
                clearerr(lp_unit.fileref);
              }
            }
            fw_IOunderwayData(&LPdev, 0);

            LPdev.iod_LPstate = IODP_LPCCWAIT;
            sim_activate(&lp_unit, LP_CC_WAIT);
            break;
          }
          
          /*
           * Put non-control characters in the print buffer.
           */
          if (ch != 0xFF)
            buffer[iod->iod_LPcolumn++] = ch;
        }

        fw_IOunderwayData(&LPdev, 0);
        
        LPdev.iod_LPstate = IODP_LPCHARWAIT;
        sim_activate(&lp_unit, lp_unit.wait);
      }
      break;

    case 0x01:
      changed = doDirectorFunc(&lp_dev, TRUE);

      if ((Areg & (IO_DIR_CINT | IO_DIR_CCONT)) != 0)
        LPdev.STATUS |= IO_ST_DATA | IO_ST_EOP;

      if (changed) {
        /*
         * The device interrupt mask has been explicitly changed. If the
         * interrupt on data was just set and the device can accept more
         * data, generate an interrupt.
         */
        if ((ICHANGED(&LPdev) & IO_DIR_DATA) != 0) {
          if ((LPdev.STATUS & IO_ST_DATA) != 0) {
            if ((lp_dev.dctrl & DBG_DINTR) != 0)
              fprintf(DBGOUT,
                      "%sLP: DATA Interrupt from mask change\r\n", INTprefix);
            RaiseExternalInterrupt(&lp_dev);
          }
        }
      }

      if (iod->iod_type == DEVTYPE_1742) {
        if ((Areg & IO_1742_PRINT) != 0) {
          LPdev.STATUS &= ~IO_ST_ALARM;
          if (iod->iod_LPcolumn != 0) {
            if ((lp_unit.flags & UNIT_ATT) != 0) {
              int i;
              
              for (i = 0; i < iod->iod_LPcolumn; i++) {
                if (putc(buffer[i], lp_unit.fileref) == EOF) {
                  perror("LP I/O error");
                  clearerr(lp_unit.fileref);
                }
              }
            }
          }
          fw_IOunderwayEOP(&LPdev, IO_ST_INT);

          LPdev.iod_LPstate = IODP_LPPRINTWAIT;
          sim_activate(&lp_unit, LP_PRINT_WAIT);
        }
      }
      break;

    case 0x3:
      if ((Areg & (IO_1740_MOTION | IO_1740_PRINT)) != 0) {
        /*
         * Here we need to print something, buffered data or vertical motion.
         * Note that we will try to do the "right" thing with respect to
         * stacked operations even though the physical hardware may not be
         * able to do so.
         */
        if ((Areg & IO_1740_PRINT) != 0) {
          LPdev.STATUS &= ~IO_ST_ALARM;
          if (iod->iod_LPcolumn != 0) {
            if ((lp_unit.flags & UNIT_ATT) != 0) {
              int i;

              if (iod->iod_LPoverwrite) {
                if (putc('\r', lp_unit.fileref) == EOF) {
                  perror("LP I/O error");
                  clearerr(lp_unit.fileref);
                }
              }

              for (i = 0; i < iod->iod_LPcolumn; i++) {
                if (putc(buffer[i], lp_unit.fileref) == EOF) {
                  perror("LP I/O error");
                  clearerr(lp_unit.fileref);
                }
              }
              iod->iod_LPoverwrite = TRUE;
            }
          }
          printwait = TRUE;
        }

        if ((Areg & IO_1740_MOTION) != 0) {
          /*** TODO: Implement format tape operations.
               For now, all operations generate a single space motion ***/
          if ((lp_unit.flags & UNIT_ATT) != 0) {
            if (putc('\n', lp_unit.fileref) == EOF) {
              perror("LP I/O error");
              clearerr(lp_unit.fileref);
            }
            if ((Areg & IO_1740_DSP) != 0) {
              if (putc('\n', lp_unit.fileref) == EOF) {
                perror("LP I/O error");
                clearerr(lp_unit.fileref);
              }
            }
          }
          iod->iod_LPoverwrite = FALSE;
          printwait = TRUE;
        }
        if (printwait) {
          fw_IOunderwayEOP(&LPdev, IO_ST_INT);

          LPdev.iod_LPstate = IODP_LPPRINTWAIT;
          sim_activate(&lp_unit, LP_PRINT_WAIT);
        }
      }
      break;

    default:
      if ((lp_dev.dctrl & DBG_DSTATE) != 0)
        fprintf(DBGOUT, "%sLP: REJECT response\r\n", INTprefix);
      return IO_REJECT;
  }
  if ((lp_dev.dctrl & DBG_DSTATE) != 0)
    LPstate("after", &lp_dev, &LPdev);

  return IO_REPLY;
}

t_stat lp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is either a 1740 or 1742 line printer controller.\n"
    "1 Hardware Description\n"
    " The %D device consists of either a 1740 or 1742 controller along with\n"
    " a physical line printer. The type of controller present may be changed\n"
    " by:\n\n"
    "+sim> SET %D TYPE=1740\n"
    "+sim> SET %D TYPE=1742\n\n"
    "2 Equipment Address\n"
    " Line printer controllers are typically set to equipment address 4. This\n"
    " address may be changed by:\n\n"
    "+sim> SET %D EQUIPMENT=hexValue\n\n"
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
