/*

   Copyright (c) 2015-2017, John Forecast

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

/* cdc1700_dp.c: disk pack controller support
 *               Simh devices: dp0, dp1
 */
#include "cdc1700_defs.h"

#define ADDRSTATUS      iod_readR[2]

extern char INTprefix[];

extern void RaiseExternalInterrupt(DEVICE *);

extern t_bool doDirectorFunc(DEVICE *, t_bool);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_IOunderwayEOP(IO_DEVICE *, uint16);
extern void fw_IOcompleteEOP(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOalarm(t_bool, DEVICE *, IO_DEVICE *, const char *);
extern void fw_IOintr(t_bool, DEVICE *, IO_DEVICE *, uint16, uint16, uint16, const char *);

extern t_stat checkReset(DEVICE *, uint8);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_protected(UNIT *, int32, CONST char *, void *);
extern t_stat clear_protected(UNIT *, int32, CONST char *, void *);

extern t_stat set_equipment(UNIT *, int32, CONST char *, void *);

extern t_stat set_stoponrej(UNIT *, int32, CONST char *, void *);
extern t_stat clr_stoponrej(UNIT *, int32, CONST char *, void *);

extern uint16 LoadFromMem(uint16);
extern t_bool IOStoreToMem(uint16, uint16, t_bool);

extern uint16 M[], Areg, IOAreg;

extern t_bool IOFWinitialized;

extern t_bool ExecutionStarted;

extern UNIT cpu_unit;

static t_stat show_drive(FILE *, UNIT *, int32, CONST void *);

t_stat set_dp853(UNIT *, int32, CONST char *, void *);
t_stat set_dp854(UNIT *, int32, CONST char *, void *);

static t_stat show_addressing(FILE *, UNIT *, int32, CONST void *);

t_stat set_normal(UNIT *, int32, CONST char *, void *);
t_stat set_reverse(UNIT *, int32, CONST char *, void *);

/* Constants */

#define DP_NUMWD        (96)            /* words/sector */
#define DP_NUMBY        (DP_NUMWD * sizeof(uint16))
#define DP_NUMSC        (16)            /* sectors/track */
#define DP_NUMTR        (10)            /* tracks/cylinder */
#define DP_853CY        (100)           /* cylinders for 853 drive */
#define DP_854CY        (203)           /* cylinders for 854 drive */
#define DP853_SIZE      (DP_853CY * DP_NUMTR * DP_NUMSC * DP_NUMBY)
#define DP854_SIZE      (DP_854CY * DP_NUMTR * DP_NUMSC * DP_NUMBY)

#define DPLBA(i) \
  ((i->cylinder * DP_NUMSC * DP_NUMTR) + (i->head * DP_NUMSC) + i->sector)

#define DP_NUMDR        2               /* # drives */

struct dpio_unit {
  uint16                state;          /* Current state of the drive */
#define DP_IDLE         0x0000          /* Idle */
#define DP_XFER         0x0001          /* Control info transfer */
#define DP_SEEK         0x0002          /* Seeking */
#define DP_WRITE        0x0003          /* Write data */
#define DP_READ         0x0004          /* Read data */
#define DP_COMPARE      0x0005          /* Compare data */
#define DP_CHECKWORD    0x0006          /* Checkword check (NOOP) */
#define DP_WRITEADDR    0x0007          /* Write address (NOOP) */
  uint16                CWA;            /* Current memory address */
  uint16                LWA;            /* LWA + 1 for transfer */
  uint16                sectorRA;       /* Sector Record Address */
  uint16                cylinder;       /* Current cylinder # */
  uint16                head;           /* Current head # */
  uint16                sector;         /* Current sector # */
  uint16                buf[DP_NUMWD];  /* Sector buffer */
  t_bool                oncyl;          /* Unit on-cylinder status */
} DPunits[DP_NUMDR];

t_bool DPbusy = FALSE;                  /* Controller vs. unit busy */

enum dpio_status {
  DPIO_MORE,                            /* More I/O pending */
  DPIO_DONE,                            /* I/O processing completed */
  DPIO_PROTECT,                         /* Protect fault */
  DPIO_MISMATCH,                        /* Compare mismatch */
  DPIO_ADDRERR                          /* Addressing error */
};

t_stat dp_svc(UNIT *);
t_stat dp_reset(DEVICE *);
t_stat dp_attach(UNIT *, CONST char *);
t_stat dp_detach(UNIT *);

void DPstate(const char *, DEVICE *, IO_DEVICE *);
t_bool DPreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus DPin(IO_DEVICE *, uint8);
enum IOstatus DPout(IO_DEVICE *, uint8);
t_bool DPintr(IO_DEVICE *);

t_stat dp_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1738-B Disk Pack Controller

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A
  (Bits 02-00)

      001               Director Function    Director Status
      010               Load Address         Address Register Status
      011               Write
      100               Read
      101               Compare
      110               Checkword Check
      111               Write Address

  Operations:

  Director Function

    15                  10   9   8   7   6    5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X |   |   |   | X | X |   |   |   |   | X |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             |   |   |           |   |   |   |
                             |   |   |           |   |   |   Clr Interrupts
                             |   |   |           |   |   Ready and not Busy
                             |   |   |           |   |      Interrupt Req.
                             |   |   |           |   EOP Interrupt Req.
                             |   |   |           Interrupt on Alarm
                             |   |   Release
                             |   Unit Select
                             Unit Select Code

  Load Address, Checkword Check, Write Address or Address Register Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                           |   |           |   |           |
     +---------------------------+   +-----------+   +-----------+
                Cylinder                  Head           Sector
          853:  0-99                      0-9            0-15
          854:  0-202

  Write, Read or Compare

    15  14                                                       0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
         |                                                       |
         +-------------------------------------------------------+
                                 FWA - 1

  Status Response:

  Director Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
         |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
         |   |   |   |   |   |   |   |   |   |   |   |   |   |   Ready
         |   |   |   |   |   |   |   |   |   |   |   |   |   Busy
         |   |   |   |   |   |   |   |   |   |   |   |   Interrupt
         |   |   |   |   |   |   |   |   |   |   |   On Cylinder
         |   |   |   |   |   |   |   |   |   |   End of Operation
         |   |   |   |   |   |   |   |   |   Alarm
         |   |   |   |   |   |   |   |   No Compare
         |   |   |   |   |   |   |   Protected
         |   |   |   |   |   |   Checkword Error
         |   |   |   |   |   Lost Data
         |   |   |   |   Seek Error
         |   |   |   Address Error
         |   |   Defective Track
         |   Storage Parity Error
         Protect Fault

  Address Register Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                           |   |           |   |           |
     +---------------------------+   +-----------+   +-----------+
                Cylinder                  Head           Sector
          853:  0-99                      0-9            0-15
          854:  0-202
 */

IO_DEVICE DPdev = IODEV(NULL, "1738-B", 1738, 3, 0xFF, 0,
                        DPreject, DPin, DPout, NULL, NULL,
                        DPstate, DPintr, NULL, NULL, NULL, NULL,
                        0x7F, 8,
                        MASK_REGISTER1 | MASK_REGISTER2 | MASK_REGISTER3 | \
                        MASK_REGISTER4 | MASK_REGISTER5 | MASK_REGISTER6 | \
                        MASK_REGISTER7,
                        MASK_REGISTER1 | MASK_REGISTER2,
                        MASK_REGISTER0, MASK_REGISTER0,
                        0, 0, DPunits);

/* DP data structures

   dp_dev       DP device descriptor
   dp_unit      DP units
   dp_reg       DP register list
   dp_mod       DP modifier list
*/

UNIT dp_unit[] = {
  { UDATA(&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_854, DP854_SIZE),
    0, 0, 0, 0, 0, &DPunits[0]
  },
  { UDATA(&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_854, DP854_SIZE),
    0, 0, 0, 0, 0, &DPunits[1]
  },
};

REG dp_reg[] = {
  { HRDATAD(FUNCTION, DPdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, DPdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, DPdev.IENABLE, 16, "Interrupts enabled") },
  { HRDATAD(ADDRSTATUS, DPdev.ADDRSTATUS, 16, "Address register status") },
  { NULL }
};

MTAB dp_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "1738-B Disk Pack Controller" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VUN, 0, "DRIVE", NULL,
    NULL, &show_drive, NULL, "Display type of drive (853 or 854" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "853",
    &set_dp853, NULL, NULL, "Set drive type to 853" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "854",
    &set_dp854, NULL, NULL, "Set drive type to 854" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL,"PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)"},
  { MTAB_XTD|MTAB_VDV, 0, "ADDRESSING", NULL,
    NULL, &show_addressing, NULL, "Display disk addressing mode" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NORMAL",
    &set_normal, NULL, NULL, "Normal addressing mode: drive 0 then 1" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "REVERSE",
    &set_reverse, NULL, NULL, "Reverse addressing mode: drive 1 then 0" },
  { 0 }
};

DEBTAB dp_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "ERROR",       DBG_DERROR,     "Display device errors" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DINTR | DBG_DERROR | DBG_DLOC },
  { NULL }
};

DEVICE dp_dev = {
  "DP", dp_unit, dp_reg, dp_mod,
  DP_NUMDR, 10, 31, 1, 8, 8,
  NULL, NULL, &dp_reset,
  NULL, &dp_attach, &dp_detach,
  &DPdev,
  DEV_DEBUG | DEV_DISK | DEV_DISABLE | \
  DEV_DIS | DEV_INDEV | DEV_OUTDEV | DEV_PROTECT,
  0, dp_deb,
  NULL, NULL, &dp_help, NULL, NULL, NULL
};

/*
 * Display disk pack drive type
 */
static t_stat show_drive(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((uptr->flags & UNIT_854) != 0)
    fprintf(st, "854 drive");
  else fprintf(st, "853 drive");
  return SCPE_OK;
}

/*
 * Set drive type to 853. If execution has started, disallow device type
 * changes.
 */
t_stat set_dp853(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((uptr->flags & UNIT_854) != 0) {
    if ((uptr->flags & UNIT_ATT) != 0)
      return SCPE_ALATT;

    if (ExecutionStarted)
      return sim_messagef(SCPE_IERR, "Unable to change drive type after execution started\n");

    uptr->flags &= ~UNIT_854;
    uptr->capac = DP853_SIZE;
  }
  return SCPE_OK;
}

/*
 * Set drive type to 854. If execution has started, disallow device type
 * changes.
 */
t_stat set_dp854(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((uptr->flags & UNIT_854) == 0) {
    if ((uptr->flags & UNIT_ATT) != 0)
      return SCPE_ALATT;

    if (ExecutionStarted)
      return sim_messagef(SCPE_IERR, "Unable to change drive type after execution started\n");

    uptr->flags |= UNIT_854;
    uptr->capac = DP854_SIZE;
  }
  return SCPE_OK;
}

/*
 * Display the device addressing mode
 */
static t_stat show_addressing(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((dp_dev.flags & DEV_REVERSE) == 0)
    fprintf(st, "Addressing: Normal");
  else fprintf(st, "Addressing: Reverse");
  return SCPE_OK;
}

/*
 * Set device to normal addressing.
 */
t_stat set_normal(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  dp_dev.flags &= ~DEV_REVERSE;
  return SCPE_OK;
}

/*
 * Set device to reverse addressing.
 */
t_stat set_reverse(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  dp_dev.flags |= DEV_REVERSE;
  return SCPE_OK;
}

/*
 * Dump the current internal state of the DP device.
 */
const char *DPstateStr[] = {
  "Idle", "Xfer", "Seek", "Write", "Read", "Compare", "Checkword", "WriteAddr"
};

void DPstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Sta: %04X, Ena: %04X, Sel: %s, Busy: %s]\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->STATUS, iod->IENABLE,
          iod->iod_unit == NULL ? "None" :
          (iod->iod_unit == dev->units ? "0" : "1"),
          DPbusy ? "Yes" : "No");
  if ((dp_unit[0].flags & UNIT_ATT) != 0)
    fprintf(DBGOUT,
            "%s[0: State: %s, Cur: %04X, Last: %04X, RA: %04X, Oncyl: %s]\r\n",
            INTprefix, DPstateStr[DPunits[0].state], DPunits[0].CWA,
            DPunits[0].LWA, DPunits[0].sectorRA,
            DPunits[0].oncyl ? "Yes" : "No");
  if ((dp_unit[1].flags & UNIT_ATT) != 0)
    fprintf(DBGOUT,
            "%s[1: State: %s, Cur: %04X, Last: %04X, RA: %04X, Oncyl: %s]\r\n",
            INTprefix, DPstateStr[DPunits[1].state], DPunits[1].CWA,
            DPunits[1].LWA, DPunits[1].sectorRA,
            DPunits[1].oncyl ? "Yes" : "No");
}

/*
 * Determine if a non-standard interrupt condition is present.
 */
t_bool DPintr(IO_DEVICE *iod)
{
  return (ISENABLED(iod, IO_1738_RBINT) &&
          ((DEVSTATUS(iod) & (IO_ST_READY | IO_ST_BUSY)) == IO_ST_READY));
}

/*
 * Load and validate disk address in the A register
 */
static t_bool LoadDiskAddress(UNIT *uptr, struct dpio_unit *iou, uint16 state)
{
  uint16 numcy = ((uptr->flags & UNIT_854) != 0) ? DP_854CY : DP_853CY;

  iou->oncyl = FALSE;
  DPdev.ADDRSTATUS = iou->sectorRA = IOAreg;

  /*
   * Split the disk address into separate fields.
   */
  iou->cylinder = (IOAreg >> 8) & 0xFF;
  iou->head = (IOAreg >> 4) & 0xF;
  iou->sector = IOAreg & 0xF;

  if ((iou->cylinder >= numcy) || (iou->head >= DP_NUMTR))
    return FALSE;

  iou->state = state;
  return TRUE;
}

/*
 * Set up a disk I/O operation with the A register containing FWA - 1.
 */
static void StartDPDiskIO(UNIT *uptr, struct dpio_unit *iou, uint16 state)
{
  iou->LWA = LoadFromMem(IOAreg);
  iou->CWA = ++IOAreg;

  DPbusy = TRUE;

  DPdev.STATUS &= IO_ST_READY | IO_ST_PROT | IO_1738_ONCYL;
  fw_IOunderwayEOP(&DPdev, 0);

  if ((dp_dev.dctrl & DBG_DTRACE) != 0)
    fprintf(DBGOUT,
            "%sDP - Start I/O, current: %04X, last: %04X, state: %d\r\n",
            INTprefix, iou->CWA, iou->LWA, state);

  if (iou->CWA == iou->LWA)  {
    /*
     * This is an empty I/O request, complete it immediately.
     */
    DPbusy = FALSE;

    if ((dp_dev.dctrl & DBG_DTRACE) != 0)
      fprintf(DBGOUT, "%sDP - Empty I/O request\r\n", INTprefix);

    fw_IOcompleteEOP(FALSE, &dp_dev, &DPdev, 0xFFFF, "Null transfer complete");
    return;
  }

  iou->state = state;
  sim_activate(uptr, DP_IO_WAIT);
}

/*
 * Increment sector # and update Sector Record Address.
 */
static void DPDiskIOIncSector(struct dpio_unit *iou)
{
  if (++iou->sector >= DP_NUMSC) {
    iou->sector = 0;
    if (++iou->head >= DP_NUMTR) {
      iou->head = 0;
      iou->cylinder++;
    }
  }
  iou->sectorRA = ((iou->cylinder << 8) | (iou->head << 4)) | iou->sector;
  DPdev.ADDRSTATUS = iou->sectorRA;
}

/*
 * Initiate a read operation on a disk.
 */
static enum dpio_status DPDiskIORead(UNIT *uptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  uint16 numcy = ((uptr->flags & UNIT_854) != 0) ? DP_854CY : DP_853CY;
  uint32 lba = DPLBA(iou);
  int i;

  if (iou->cylinder >= numcy)
    return DPIO_ADDRERR;

  /*
   * Report any error in the underlying container infrastructure as an
   * address error.
   */
  if (sim_fseeko(uptr->fileref, lba * DP_NUMBY, SEEK_SET) ||
      (sim_fread(iou->buf, sizeof(uint16), DP_NUMWD, uptr->fileref) != DP_NUMWD))
    return DPIO_ADDRERR;

  for (i = 0; i < DP_NUMWD; i++) {
    /*** TODO: fix protect check ***/
    if (!IOStoreToMem(iou->CWA, iou->buf[i], TRUE))
      return DPIO_PROTECT;

    iou->CWA++;
    if (iou->CWA == iou->LWA) {
      DPDiskIOIncSector(iou);
      return DPIO_DONE;
    }
  }
  DPDiskIOIncSector(iou);
  return DPIO_MORE;
}

/*
 * Initiate a write operation on a disk.
 */
static enum dpio_status DPDiskIOWrite(UNIT *uptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  uint16 numcy = ((uptr->flags & UNIT_854) != 0) ? DP_854CY : DP_853CY;
  uint32 lba = DPLBA(iou);
  t_bool fill = FALSE;
  int i;

  if (iou->cylinder >= numcy)
    return DPIO_ADDRERR;

  for (i = 0; i < DP_NUMWD; i++) {
    if (!fill) {
      iou->buf[i] = LoadFromMem(iou->CWA);
      iou->CWA++;
      if (iou->CWA == iou->LWA)
        fill = TRUE;
    } else iou->buf[i] = 0;
  }

  /*
   * Report any error in the underlying container infrastructure as an
   * address error.
   */
  if (sim_fseeko(uptr->fileref, lba * DP_NUMBY, SEEK_SET) ||
      (sim_fwrite(iou->buf, sizeof(uint16), DP_NUMWD, uptr->fileref) != DP_NUMWD))
    return DPIO_ADDRERR;

  DPDiskIOIncSector(iou);
  return fill ? DPIO_DONE : DPIO_MORE;
}

/*
 * Initiate a compare operation on a disk.
 */
static enum dpio_status DPDiskIOCompare(UNIT *uptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  uint16 numcy = ((uptr->flags & UNIT_854) != 0) ? DP_854CY : DP_853CY;
  uint32 lba = DPLBA(iou);
  int i;

  if (iou->cylinder >= numcy)
    return DPIO_ADDRERR;

  /*
   * Report any error in the underlying container infrastructure as an
   * address error.
   */
  if (sim_fseeko(uptr->fileref, lba * DP_NUMBY, SEEK_SET) ||
      (sim_fread(iou->buf, sizeof(uint16), DP_NUMWD, uptr->fileref) != DP_NUMWD))
    return DPIO_ADDRERR;

  for (i = 0; i < DP_NUMWD; i++) {
    if (iou->buf[i] != LoadFromMem(iou->CWA))
      return DPIO_MISMATCH;

    iou->CWA++;
    if (iou->CWA == iou->LWA) {
      DPDiskIOIncSector(iou);
      return DPIO_DONE;
    }
  }
  DPDiskIOIncSector(iou);
  return DPIO_MORE;
}

/*
 * Perform read/write/compare sector operations from within the unit
 * service routine.
 */
void DPDiskIO(UNIT *uptr, uint16 iotype)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  const char *error = "Unknown";
  enum dpio_status status = DPIO_ADDRERR;

  switch (iotype) {
    case DP_WRITE:
      status = DPDiskIOWrite(uptr);
      break;

    case DP_READ:
      status = DPDiskIORead(uptr);
      break;

    case DP_COMPARE:
      status = DPDiskIOCompare(uptr);
      break;
  }

  switch (status) {
    case DPIO_MORE:
      sim_activate(uptr, DP_IO_WAIT);
      break;

    case DPIO_PROTECT:
      DPdev.STATUS |= IO_1738_SPROT;
      error = "Protection Fault";
      goto err;

    case DPIO_ADDRERR:
      DPdev.STATUS |= IO_1738_ADDRERR;
      error = "Address Error";
  err:
      iou->state = DP_IDLE;

      DPbusy = FALSE;

      if ((dp_dev.dctrl & DBG_DERROR) != 0)
        fprintf(DBGOUT,
                "%sDP - Read/Write/Compare failed - %s\r\n",
                INTprefix, error);

      fw_IOalarm(FALSE, &dp_dev, &DPdev, "Alarm");
      break;

    case DPIO_MISMATCH:
      DPdev.STATUS |= IO_1738_NOCOMP;
      /* FALLTHROUGH */

    case DPIO_DONE:
      iou->state = DP_IDLE;

      DPbusy = FALSE;

      if ((dp_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT,
                "%sDP - Read/Write/Compare transfer complete\r\n", INTprefix);

      fw_IOcompleteEOP(TRUE, &dp_dev, &DPdev, 0xFFFF, "Transfer complete");
      break;
  }
}

/* Unit service */

t_stat dp_svc(UNIT *uptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;

  if ((dp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[DP: dp_svc() entry]\r\n", INTprefix);
    if ((dp_dev.dctrl & DBG_DSTATE) != 0)
      DPstate("svc_entry", &dp_dev, &DPdev);
  }

  switch (iou->state) {
    case DP_IDLE:
      /*
       * Unit is idle, nothing to do.
       */
      break;

    case DP_XFER:
      /*
       * Transfer of positioning information is complete.
       */
      iou->state = DP_SEEK;
      sim_activate(uptr, DP_SEEK_WAIT);

      /*
       * If this is the currently selected unit, update controller status
       * and possibly ganerate an interrupt.
       */
      if (DPdev.iod_unit == uptr) {
        DPdev.STATUS |= IO_ST_EOP;
        if ((dp_dev.dctrl & DBG_DTRACE) != 0)
          fprintf(DBGOUT,
                  "%sDP - Load Address positioning transfer complete\r\n",
                  INTprefix);

        fw_IOintr(FALSE, &dp_dev, &DPdev, 0, 0, 0xFFFF, "Load address");
      }
      break;

    case DP_SEEK:
      iou->state = DP_IDLE;
      iou->oncyl = TRUE;

      DPdev.STATUS &= ~IO_ST_BUSY;

      /*
       * If this is the currently selected unit, update controller status
       * and possibly generate an interrupt.
       */
      if (DPdev.iod_unit == uptr) {
        DPdev.STATUS |= IO_1738_ONCYL;

        if ((dp_dev.dctrl & DBG_DTRACE) != 0)
          fprintf(DBGOUT, "%sDP - Seek complete\r\n", INTprefix);

        fw_IOintr(TRUE, &dp_dev, &DPdev, 0, 0, 0xFFFF, "Seek complete");
      }
      break;

    case DP_WRITE:
    case DP_READ:
    case DP_COMPARE:
      DPDiskIO(uptr, iou->state);
      break;

    case DP_CHECKWORD:
      iou->state = DP_IDLE;
      iou->oncyl = TRUE;

      /*
       * Set Sector Record Address to the start of the next track.
       */
      iou->sector = 0;
      if (++iou->head >= DP_NUMTR) {
        iou->head = 0;
        iou->cylinder++;
      }
      iou->sectorRA = ((iou->cylinder << 8) | (iou->head << 4)) | iou->sector;
      DPdev.ADDRSTATUS = iou->sectorRA;

      DPdev.STATUS |= IO_ST_EOP | IO_1738_ONCYL;
      DPdev.STATUS &= ~IO_ST_BUSY;
      DPbusy = FALSE;

      if ((dp_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT, "%sDP - Checkword transfer complete\r\n", INTprefix);

      if ((DPdev.STATUS & (IO_ST_READY | IO_ST_BUSY)) == IO_ST_READY)
        fw_IOintr(TRUE, &dp_dev, &DPdev, 0, 0, 0xFFFF, "Checkword transfer");

      fw_IOintr(FALSE, &dp_dev, &DPdev, 0, 0, 0xFFFF, "Checkword");
      break;

    case DP_WRITEADDR:
      break;
  }

  if ((dp_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[DP: dp_svc() exit]\r\n", INTprefix);
    if ((dp_dev.dctrl & DBG_DSTATE) != 0)
      DPstate("svc_exit", &dp_dev, &DPdev);
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat dp_reset(DEVICE *dptr)
{
  t_stat r;

  if (IOFWinitialized)
    if ((dptr->flags & DEV_DIS) == 0)
      if ((r = checkReset(dptr, DPdev.iod_equip)) != SCPE_OK)
        return r;

  DPbusy = FALSE;

  /*
   * Cancel any existing unit selection.
   */
  DPdev.iod_unit = NULL;

  /*
   * Clear on-cylinder status
   */
  DPdev.STATUS &= ~IO_1738_ONCYL;

  return SCPE_OK;
}

/* Attach routine */

t_stat dp_attach(UNIT *uptr, CONST char *cptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  t_addr capac = ((uptr->flags & UNIT_854) != 0) ? DP854_SIZE : DP853_SIZE;
  t_stat r;

  uptr->capac = capac;
  r = attach_unit(uptr, cptr);
  if (r != SCPE_OK)
    return r;

  /*
   * If this is a newly created file, set the drive size appropriately.
   */
  if (sim_fsize_ex(uptr->fileref) == 0)
    sim_set_fsize(uptr->fileref, capac);

  if (sim_fsize_ex(uptr->fileref) != capac) {
    if (ExecutionStarted) {
      detach_unit(uptr);
      return sim_messagef(SCPE_OPENERR, "Unable to autosize drive after execution started\n");
    }
    /*
     * This is not the correct size according the drive type but this is the
     * first attach. Force the drive to match the size of the disk.
     */
    switch (sim_fsize_ex(uptr->fileref)) {
      case DP854_SIZE:
        uptr->capac = DP854_SIZE;
        uptr->flags |= UNIT_854;
        break;

      case DP853_SIZE:
        uptr->capac = DP853_SIZE;
        uptr->flags &= ~UNIT_854;
        break;

      default:
        detach_unit(uptr);
        return sim_messagef(SCPE_OPENERR, "Unsupported disk size\n");
    }
  }
  /*
   * Set unit to cylinder 0, head 0, sector 0 and indicate on-cylinder.
   */
  iou->cylinder = 0;
  iou->head = 0;
  iou->sector = 0;
  iou->oncyl = TRUE;

  return SCPE_OK;
}

/* Detach routine */

t_stat dp_detach(UNIT *uptr)
{
  struct dpio_unit *iou = (struct dpio_unit *)uptr->up7;
  t_stat stat;

  sim_cancel(uptr);
  stat = detach_unit(uptr);
  iou->oncyl = FALSE;

  return stat;
}

/* Check if I/O should be rejected */

t_bool DPreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  if (output) {
    switch (reg) {
      /*
       * Director function
       */
      case 0x01:
        /*** TODO: Check protect status ***/
        return DPbusy;

      /*
       * Write Address - always unsupported
       */
      case 0x07:
        return TRUE;

      /*
       * Write/Checkword Check
       */
      case 0x03:
      case 0x06:
        /*** TODO: Check protect status ***/
        /* FALLTHROUGH */

      /*
       * Load Address/Read/Compare
       */
      case 0x02:
      case 0x04:
      case 0x05:
        return ((DEVSTATUS(iod) &
                 (IO_ST_READY | IO_ST_BUSY | IO_1738_ONCYL)) !=
                (IO_ST_READY | IO_1738_ONCYL));
    }
  }
  return FALSE;
}

/* Perform I/O */

enum IOstatus DPin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * All input requests should be handled by the I/O framework.
   */
  return IO_REJECT;
}

enum IOstatus DPout(IO_DEVICE *iod, uint8 reg)
{
  UNIT *uptr;
  struct dpio_unit *iou;

  switch (reg) {
    /*
     * Director function
     */
    case 0x01:
      /*
       * Reject the request if both select and release are set
       */
      if ((IOAreg & (IO_1738_USEL | IO_1738_REL)) == (IO_1738_USEL | IO_1738_REL))
        return IO_REJECT;

      if (doDirectorFunc(&dp_dev, TRUE)) {
        /*
         * The device interrupt mask has been explicitly changed. If the
         * device state is such that an interrupt can occur, generate it now.
         */

        /*
         * Note: don't check for "Ready and not Busy Interrupt" here since
         * it's defined as "Next Ready and not Busy", i.e. defer until the
         * next opportunity.
         */
        if ((ICHANGED(&DPdev) & IO_DIR_EOP) != 0) {
          if ((DPdev.STATUS & IO_ST_EOP) != 0) {
            if ((dp_dev.dctrl & DBG_DINTR) != 0)
              fprintf(DBGOUT,
                      "%sDP: Mask change EOP interrupt\r\n", INTprefix);
            RaiseExternalInterrupt(&dp_dev);
          }
        }
      }

      /*
       * Handle select/release.
       */
      if ((IOAreg & (IO_1738_USEL | IO_1738_REL)) != 0) {
        uint16 unit = (IOAreg & IO_1738_USC) >> 9;

        if ((dp_dev.flags & DEV_REVERSE) != 0)
          unit ^= 1;

        DPdev.STATUS &= ~IO_ST_READY;
        if ((IOAreg & IO_1738_USEL) != 0) {
          DPdev.iod_unit = &dp_unit[unit];
          if ((dp_unit[unit].flags & UNIT_ATT) != 0) {
            DPdev.STATUS |= IO_ST_READY;
            iou = (struct dpio_unit *)dp_unit[unit].up7;
            if (iou->oncyl) {
              DPdev.STATUS |= IO_1738_ONCYL;
              DPdev.ADDRSTATUS = iou->sectorRA;
            }
            if ((iou->state == DP_XFER) || (iou->state == DP_SEEK) || DPbusy)
              DPdev.STATUS |= IO_ST_BUSY;
          }
        }

        if ((IOAreg & IO_1738_REL) != 0) {
          /*** TODO: check protect conditions ***/
          DPdev.iod_unit = NULL;
          DPdev.STATUS &= ~(IO_1738_ONCYL | IO_ST_BUSY);
          if (DPbusy)
            DPdev.STATUS |= IO_ST_BUSY;
        }
      }
      break;

    /*
     * Load address
     */
    case 0x02:
      if ((uptr = DPdev.iod_unit) != NULL) {
        iou = (struct dpio_unit *)uptr->up7;

        if (LoadDiskAddress(uptr, iou, DP_XFER)) {
          DPdev.STATUS &= IO_ST_READY | IO_ST_PROT;
          DPdev.STATUS |= IO_ST_BUSY;
          sim_activate(uptr, DP_XFER_WAIT);
        } else {
          if ((dp_dev.dctrl & DBG_DERROR) != 0)
            fprintf(DBGOUT,
                    "%sDP: Bad Load Address (%04X)\r\n", INTprefix, Areg);

          fw_IOintr(FALSE, &dp_dev, &DPdev, IO_1738_ADDRERR | IO_ST_EOP | IO_ST_ALARM, 0, 0xFFFF, "Bad load address");
        }
      } else return IO_REJECT;
      break;

    /*
     * Write
     */
    case 0x03:
      if ((uptr = DPdev.iod_unit) != NULL) {
        iou = (struct dpio_unit *)uptr->up7;

        StartDPDiskIO(uptr, iou, DP_WRITE);
      } else return IO_REJECT;
      break;

    /*
     * Read
     */
    case 0x04:
      if ((uptr = DPdev.iod_unit) != NULL) {
        iou = (struct dpio_unit *)uptr->up7;

        StartDPDiskIO(uptr, iou, DP_READ);
      } else return IO_REJECT;
      break;

    /*
     * Compare
     */
    case 0x05:
      if ((uptr = DPdev.iod_unit) != NULL) {
        iou = (struct dpio_unit *)uptr->up7;

        StartDPDiskIO(uptr, iou, DP_COMPARE);
      } else return IO_REJECT;
      break;

    /*
     * Checkword check
     */
    case 0x06:
      if ((uptr = DPdev.iod_unit) != NULL) {
        iou = (struct dpio_unit *)uptr->up7;
        if (LoadDiskAddress(uptr, iou, DP_CHECKWORD)) {
          DPdev.STATUS &= IO_ST_READY | IO_ST_PROT | IO_1738_ONCYL;
          DPdev.STATUS |= IO_ST_BUSY;
          DPbusy = TRUE;
          sim_activate(uptr, DP_XFER_WAIT);
        } else {
          if ((dp_dev.dctrl & DBG_DERROR) != 0)
            fprintf(DBGOUT,
                    "%sDP: Bad Checkword Address (%04X)\r\n",
                    INTprefix, Areg);

          fw_IOintr(FALSE, &dp_dev, &DPdev, IO_1738_ADDRERR | IO_ST_EOP | IO_ST_ALARM, 0, 0xFFFF, "Bad checkword");
        }
      } else return IO_REJECT;
      break;
  }
  return IO_REPLY;
}

/*
 * Autoload support
 */
t_stat DPautoload(void)
{
  UNIT *uptr = &dp_unit[(dp_dev.flags & DEV_REVERSE) == 0 ? 0 : 1];

  if ((uptr->flags & UNIT_ATT) != 0) {
    uint32 i;

    for (i = 0; i < DP_NUMSC; i++) {
      t_offset offset = i * DP_NUMBY;
      void *buf = &M[i * DP_NUMWD];

      if (sim_fseeko(uptr->fileref, offset, SEEK_SET) ||
          (sim_fread(buf, sizeof(uint16), DP_NUMWD, uptr->fileref) != DP_NUMWD))
        return SCPE_IOERR;
    }
    return SCPE_OK;
  }
  return SCPE_UNATT;
}

t_stat dp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1738-B disk pack controller.\n"
    "1 Hardware Description\n"
    " The 1738-B consists of a controller with up to 2 attached disk drives.\n"
    " The controller includes a jumper which controls which drive is\n"
    " addressed as logical disk 0:\n\n"
    "+sim> SET %D NORMAL\n"
    "+sim> SET %D REVERSE\n\n"
    " Each physical drive may be configured as a 853 or 854:\n\n"
    "+853 drive: 1536000 words per drive\n"
    "+854 drive: 3118080 words per drive\n\n"
    " The configuration may be changed by:\n\n"
    "+sim> SET %U 853\n"
    "+sim> SET %U 854\n"
    "2 Equipment Address\n"
    " Disk controllers are typically set to equipment address 3. This address\n"
    " may be changed by:\n\n"
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
