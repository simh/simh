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

/* cdc1700_cd.c: cartridge disk drive controller support
 *               Simh devices: cd0, cd1, cd2, cd3, cd4, cd5, cd6, cd7
 */
#include "cdc1700_defs.h"

#define CYLADRSTATUS    iod_readR[2]
#define CWA             iod_readR[3]
#define CWSTATUS        iod_readR[4]
#define DCYLSTATUS      iod_readR[5]
#define BUFLEN          iod_buflen

extern char INTprefix[];

extern void RaiseExternalInterrupt(DEVICE *);

extern t_bool doDirectorFunc(DEVICE *, t_bool);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_IOunderwayEOP2(IO_DEVICE *, uint16);
extern void fw_IOcompleteEOP2(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOalarm(t_bool, DEVICE *, IO_DEVICE *, const char *);
extern void fw_IOintr(t_bool, DEVICE *, IO_DEVICE *, uint16, uint16, uint16, const char *);

extern void rebuildPending(void);

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

t_stat set_cd856_2(UNIT *, int32, CONST char *, void *);
t_stat set_cd856_4(UNIT *, int32, CONST char *, void *);

static t_stat show_addressing(FILE *, UNIT *, int32, CONST void *);

t_stat set_cartfirst(UNIT *, int32, CONST char *, void *);
t_stat set_fixedfirst(UNIT *, int32, CONST char *, void *);

t_stat cd_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/* Constants */

#define CD_NUMWD        (96)            /* words/sector */
#define CD_NUMBY        (CD_NUMWD * sizeof(uint16))
#define CD_NUMSC        (29)            /* sectors/cylinder */
#define CD_856_2CY      (203)           /* cylinders for 856-2 drive */
#define CD_856_4CY      (406)           /* cylinders for 856-4 drive */
#define CD_SURF         (2)             /* # of surfaces */
#define CD856_2_SIZE    (CD_SURF * CD_856_2CY * CD_NUMSC * CD_NUMBY)
#define CD856_4_SIZE    (CD_SURF * CD_856_4CY * CD_NUMSC * CD_NUMBY)

#define CDLBA(i) \
  ((i->surface * i->maxcylinder * CD_NUMSC) + (i->cylinder * CD_NUMSC) + i->sector)

/*
 * Disk address fields
 */
#define CD_CYL_MASK     0xFF80          /* Cylinder address mask */
#define CD_CYL_SHIFT    7               /*    and shift */
#define CD_SURF_MASK    0x0040          /* Surface mask */
#define CD_SURF_SHIFT   6               /*    and shift */
#define CD_DISK_MASK    0x0020          /* Disk mask */
#define CD_DISK_SHIFT   5               /*    and shift */
#define CD_SECTOR_MASK  0x001F          /* Sector mask */

#define CD_CHECKWD_MASK 0x0FFF          /* Checkword value mask */

#define CD_SEEK_COMP0   0x0001          /* Drive 0 seek complete */
#define CD_SEEK_COMP1   0x0002          /* Drive 1 seek complete */
#define CD_SEEK_COMP2   0x0004          /* Drive 2 seek complete */
#define CD_SEEK_COMP3   0x0008          /* Drive 3 seek complete */
#define CD_SEEK_COMP \
  (CD_SEEK_COMP0 | CD_SEEK_COMP1 | CD_SEEK_COMP2 | CD_SEEK_COMP3)

#define CD_NUMDR        4               /* # drives */

struct cdio_unit {
  char                  name[4];        /* Drive name */
  uint16                state;          /* Current status of the drive */
#define CD_IDLE         0x0000          /* Idle */
#define CD_SEEK         0x0001          /* Seeking */
#define CD_WRITE        0x0002          /* Write data */
#define CD_READ         0x0003          /* Read data */
#define CD_COMPARE      0x0004          /* Compare data */
#define CD_CHECKWORD    0x0005          /* Checkword check (NOOP) */
#define CD_WRITEADDR    0x0006          /* Write address */
#define CD_RTZS         0x0007          /* Return to zero seek */
  uint16                buf[CD_NUMWD];  /* Sector buffer */
  uint16                maxcylinder;    /* Max cylinder # */
  uint16                cylinder;       /* Current cylinder */
  uint16                sector;         /* Current sector */
  uint8                 surface;        /* Current surface */
  uint8                 disk;           /* Current physical disk */
  uint8                 requested;      /* Current requested disk */
#define CD_NONE         0xFF
  uint16                sectorAddr;     /* Current sector address */
  UNIT                  *ondrive[2];    /* Units which are part of drive */
  UNIT                  *active;        /* Currently active unit */
  uint16                seekComplete;   /* Drive seek complete mask */
  t_bool                oncyl;          /* Unit on-cylinder status */
  t_bool                busy;           /* Drive busy status */
} CDunits[CD_NUMDR];

enum cdio_status {
  CDIO_MORE,                            /* More I/O pending */
  CDIO_DONE,                            /* I/O processing completed */
  CDIO_PROTECT,                         /* Protect fault */
  CDIO_MISMATCH,                        /* Compare mismatch */
  CDIO_ADDRERR                          /* Addressing error */
};

t_stat cd_svc(UNIT *);
t_stat cd_reset(DEVICE *);
t_stat cd_attach(UNIT *, CONST char *);
t_stat cd_detach(UNIT *);

void CDstate(const char *, DEVICE *, IO_DEVICE *);
t_bool CDreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus CDin(IO_DEVICE *, uint8);
enum IOstatus CDout(IO_DEVICE *, uint8);
t_bool CDintr(IO_DEVICE *);

/*
        1733-2 Cartridge Disk Drive Controller

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A
  (Bits 02-00)

      000               Load Buffer          Clear Controller
      001               Director Function    Director Status
      010               Load Address         Cylinder Address Status
      011               Write                Current Word Address Status
      100               Read                 Checkword Status
      101               Compare              Drive Cylinder Status
      110               Checkword Check      Illegal
      111               Write Address        Illegal

  Operations:

  Load Buffer

    15  14                                                       0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                                                           |
     +-----------------------------------------------------------+
                            Buffer Length

  Director Function

    15                  10   9   8   7   6    5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X |   |   |   |   | X | X |   |   |   |   | X |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                         |   |   |   |           |   |   |   |
                         +---+   |   |           |   |   |   Clr Interrupts
                           |     |   |           |   |   Ready and not Busy
                           |     |   |           |   |      Interrupt Req.
                           |     |   |           |   EOP Interrupt Req.
                           |     |   |           Interrupt on Alarm
                           |     |   Unit de-select
                           |     Unit Select
                           Unit Select Code

  Load Address, Checkword Check, Write Address or Cylinder Address Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                               |   |   |   |               |
     +-------------------------------+   |   |   +---------------+
                  Cylinder               |   |        Sector
             856-2:  0-202               |   |         0-28
             856-4:  0-405               |   Disk
                                         Surface (0 - top, 1 - bottom)

  Write, Read or Compare

    15  14                                                       0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                                                           |
     +-----------------------------------------------------------+
                                  FWA

  Status Response:

  Director Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
     |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   Ready
     |   |   |   |   |   |   |   |   |   |   |   |   |   |   Busy
     |   |   |   |   |   |   |   |   |   |   |   |   |   Interrupt
     |   |   |   |   |   |   |   |   |   |   |   |   On Cylinder
     |   |   |   |   |   |   |   |   |   |   |   End of Operation
     |   |   |   |   |   |   |   |   |   |   Alarm
     |   |   |   |   |   |   |   |   |   No Compare
     |   |   |   |   |   |   |   |   Protected
     |   |   |   |   |   |   |   Checkword Error
     |   |   |   |   |   |   Lost Data
     |   |   |   |   |   Address Error
     |   |   |   |   Controller Seek Error
     |   |   |   Single Density
     |   |   Storage Parity Error
     |   Protect Fault
     Drive Seek Error


  Cylinder Address Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                               |   |   |   |               |
     +-------------------------------+   |   |   +---------------+
                  Cylinder               |   |        Sector
             856-2:  0-202               |   |         0-28
             856-4:  0-405               |   Disk
                                         Surface (0 - top, 1 - bottom)

  Checkword Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 0 | 0 | 0 | 0 |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                     |                                           |
                     +-------------------------------------------+
                        Checkword from last sector operated on


  Drive Cylinder Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   | X | X | X |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                               |               |   |   |   |
     +-------------------------------+               |   |   |   Seek Compl. 0
           True Cylinder Address                     |   |   Seek Compl. 1
                                                     |   Seek Compl. 2
                                                     Seek Compl. 3
 */

IO_DEVICE CDdev = IODEV(NULL, "1733-2", 1733, 3, 0xFF, 0,
                        CDreject, CDin, CDout, NULL, NULL,
                        CDstate, CDintr, NULL, NULL,
                        0x7F, 8,
                        MASK_REGISTER0 | MASK_REGISTER1 | MASK_REGISTER2 | \
                        MASK_REGISTER3 | MASK_REGISTER4 | MASK_REGISTER5 | \
                        MASK_REGISTER6 | MASK_REGISTER7,
                        MASK_REGISTER1 | MASK_REGISTER2 | MASK_REGISTER3 | \
                        MASK_REGISTER4 | MASK_REGISTER5,
                        MASK_REGISTER6 | MASK_REGISTER7, 0,
                        0, 0, CDunits);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_drive       iod_private2    /* Currently selected drive */
#define iod_buflen      iod_private3    /* Remaining buffer length */

/* CD data structures

   cd_dev       CD device descriptor
   cd_unit      CD units
   cd_reg       CD register list
   cd_mod       CD modifier list
*/

UNIT cd_unit[] = {
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[0], &cd_unit[1]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[0], &cd_unit[0]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[1], &cd_unit[3]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[1], &cd_unit[2]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[2], &cd_unit[5]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[2], &cd_unit[4]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[3], &cd_unit[7]
  },
  { UDATA(&cd_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_856_4, CD856_4_SIZE),
    0, 0, 0, 0, 0, &CDunits[3], &cd_unit[6]
  }
};

REG cd_reg[] = {
  { HRDATAD(FUNCTION, CDdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, CDdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, CDdev.IENABLE, 16, "Interrupts enabled") },
  /*** more ***/
  { NULL }
};

MTAB cd_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "1733-2 Cartridge Disk Drive Controller" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Display equipment address" },
  { MTAB_XTD|MTAB_VUN, 0, "DRIVE", NULL,
    NULL, &show_drive, NULL, "Display type of drive (856-2 or 856-4)" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "856-2",
    &set_cd856_2, NULL, NULL, "Set drive type to 856-2" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "856-4",
    &set_cd856_4, NULL, NULL, "Set drive type to 856-4" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation if I/O is rejected" },
  /*** should protect be per-unit? ***/
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)"  },
  { MTAB_XTD|MTAB_VDV, 0, "ADDRESSING", NULL,
    NULL, &show_addressing, NULL, "Show disk addressing mode" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "CARTFIRST",
    &set_cartfirst, NULL, NULL, "Set cartridge as logical disk 0" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "FIXEDFIRST",
    &set_fixedfirst, NULL, NULL, "Set fixec disk as logical disk 0" },
  { 0 }
};

DEBTAB cd_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "ERROR",       DBG_DERROR,     "Display device errors" },
  { "LOCATION",    DBG_DLOC,       "Display address for I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DINTR | DBG_DERROR | DBG_DLOC },
  { NULL }
};

DEVICE cd_dev = {
  "CDD", cd_unit, cd_reg, cd_mod,
  CD_NUMDR * 2, 10, 31, 1, 8, 8,
  NULL, NULL, &cd_reset,
  NULL, &cd_attach, &cd_detach,
  &CDdev,
  DEV_DEBUG | DEV_DISK | DEV_DISABLE | DEV_INDEV | DEV_OUTDEV | DEV_PROTECT,
  0, cd_deb,
  NULL, NULL, &cd_help, NULL, NULL, NULL
};

/*
 * Display cartridge drive type
 */
static t_stat show_drive(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  int32 fixed, u;
  
  if (uptr == NULL)
    return SCPE_IERR;

  u = uptr - cd_dev.units;
  fixed = u & 0x01;
  if ((cd_dev.flags & DEV_FIXED) != 0)
    fixed ^= 0x01;

  fprintf(st, "drive %u, %s, %s", u >> 1,
          ((uptr->flags & UNIT_856_4) != 0) ? "856-4" : "856-2",
          (fixed != 0) ? "Fixed" : "Cartridge");
  return SCPE_OK;
}

/*
 * Set drive type to 856-2. If execution has started, disallow device type
 * changes. Note that the drive contains 2 physical disks and they must
 * both be changed together.
 */
t_stat set_cd856_2(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  UNIT *uptr2;

  if (uptr == NULL)
    return SCPE_IERR;

  uptr2 = (UNIT *)uptr->up8;

  if ((uptr->flags & UNIT_856_4) != 0) {
    if (((uptr->flags & UNIT_ATT) != 0) || ((uptr2->flags & UNIT_ATT) != 0))
      return SCPE_ALATT;

    if (ExecutionStarted)
      return sim_messagef(SCPE_IERR, "Unable to change drive type after execution started\n");

    uptr->flags &= ~UNIT_856_4;
    uptr->capac = CD856_2_SIZE;
    uptr2->flags &= ~UNIT_856_4;
    uptr2->capac = CD856_2_SIZE;
  }
  return SCPE_OK;
}

/*
 * Set drive type to 856-4. If execution has started, disallow device type
 * changes. Note that the drive contains 2 physical disks and they must
 * both be changed together.
 */
t_stat set_cd856_4(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  UNIT *uptr2;

  if (uptr == NULL)
    return SCPE_IERR;

  uptr2 = (UNIT *)uptr->up8;

  if ((uptr->flags & UNIT_856_4) == 0) {
    if (((uptr->flags & UNIT_ATT) != 0) || ((uptr2->flags & UNIT_ATT) != 0))
      return SCPE_ALATT;

    if (ExecutionStarted)
      return sim_messagef(SCPE_IERR, "Unable to change drive type after execution started\n");

    uptr->flags |= UNIT_856_4;
    uptr->capac = CD856_4_SIZE;
    uptr2->flags |= UNIT_856_4;
    uptr2->capac = CD856_4_SIZE;
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

  if ((cd_dev.flags & DEV_FIXED) == 0)
    fprintf(st, "Addressing: Cartridge first");
  else fprintf(st, "Addressing: Fixed first");
  return SCPE_OK;
}

/*
 * Set device to "Cartridge first" addressing
 */
t_stat set_cartfirst(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  cd_dev.flags &= ~DEV_FIXED;
  return SCPE_OK;
}

/*
 * Set device to "Fixed first" addressing
 */
t_stat set_fixedfirst(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  cd_dev.flags |= DEV_FIXED;
  return SCPE_OK;
}

/*
 * Dump the current internal state of the CD device.
 */
const char *CDstateStr[] = {
  "Idle", "Seek", "Write", "Read", "Compare", "Checkword", "WriteAddr", "RTZS"
};

void CDstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  struct cdio_unit *iou = (struct cdio_unit *)iod->iod_drive;
  int fixed, i;
  const char *active = "None";

  fixed = ((cd_dev.flags & DEV_FIXED) != 0) ? 0 : 1;

  if (iou != NULL) {
    if (iou->active != NULL) {
      if (iou->active == iou->ondrive[0])
        active = "0";
      if (iou->active == iou->ondrive[1])
        active = "1";
    }
  }

  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Sta: %04X, Ena: %04X, Sel: %s,%s]\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->STATUS, iod->IENABLE,
          iou != NULL ? iou->name : "None", active);
  fprintf(DBGOUT,
          "%s[%s: CAS: %04X, CWA: %04X, CWS: %04X, DCS: %04X, LEN: %04X]\r\n",
          INTprefix, dev->name,
          iod->CYLADRSTATUS, iod->CWA, iod->CWSTATUS,
          iod->DCYLSTATUS, iod->BUFLEN);

  for (i = 0; i < CD_NUMDR; i++) {
    UNIT *uptr = &cd_unit[i * 2];
    UNIT *uptr2 = &cd_unit[(i * 2) + 1];

    iou = &CDunits[i];

    if (((uptr->flags & UNIT_ATT) != 0) || ((uptr2->flags & UNIT_ATT) != 0)) {
      /*** more to print ***/
      fprintf(DBGOUT,
              "%s[%d: State: %s, OnCyl: %s, Busy: %s]\r\n",
              INTprefix, i, CDstateStr[iou->state], 
              iou->oncyl ? "Yes" : "No",
              iou->busy ? "Yes" : "No");
      if ((uptr->flags & UNIT_ATT) != 0)
        fprintf(DBGOUT,
                "%s   %s attached\r\n", INTprefix,
                fixed == 0 ? "Fixed" : "Cartridge");
      if ((uptr2->flags & UNIT_ATT) != 0)
        fprintf(DBGOUT,
                "%s   %s attached\r\n", INTprefix,
                fixed == 1 ? "Fixed" : "Cartridge");
    }
  }
}

/*
 * Determine if a non-standard interrupt condition is present.
 */
t_bool CDintr(IO_DEVICE *iod)
{
  return (ISENABLED(iod, IO_1733_RBINT) &&
          ((DEVSTATUS(iod) & (IO_ST_READY | IO_ST_BUSY)) == IO_ST_READY));
}

/*
 * Load and validate disk address in the A register
 */
static t_bool LoadDiskAddress(UNIT *uptr, struct cdio_unit *iou, uint16 state)
{
  uint16 numcy = ((uptr->flags & UNIT_856_4) != 0) ? CD_856_4CY : CD_856_2CY;
  uint16 current = iou->cylinder;

  /*
   * Abort immediately if the disk address is invalid
   */
  if ((((IOAreg & CD_CYL_MASK) >> CD_CYL_SHIFT) >= numcy) ||
      ((IOAreg & CD_SECTOR_MASK) >= CD_NUMSC))
    return FALSE;

  CDdev.CYLADRSTATUS = iou->sectorAddr = IOAreg;

  iou->maxcylinder = numcy;

  /*
   * Split the address into separate fields.
   */
  iou->cylinder = (IOAreg & CD_CYL_MASK) >> CD_CYL_SHIFT;
  iou->sector = IOAreg & CD_SECTOR_MASK;
  iou->surface = (IOAreg & CD_SURF_MASK) >> CD_SURF_SHIFT;
  iou->disk = iou->requested = (IOAreg & CD_DISK_MASK) >> CD_DISK_SHIFT;
  if ((cd_dev.flags & DEV_FIXED) != 0)
    iou->disk ^= 0x01;

  iou->active = iou->ondrive[iou->disk];

  CDdev.DCYLSTATUS &= ~iou->seekComplete;

  /*
   * This optimization is undocumented but is inferred from the MSOS
   * device driver.
   */
  if (ISENABLED(&CDdev, IO_DIR_EOP)) {
    /*
     * If we are already at the requested cylinder, bypass the seek and leave
     * on-cylinder status set.
     */
    if (iou->cylinder == current) {
      CDdev.STATUS |= IO_1733_ONCYL;
      iou->oncyl = TRUE;
      return TRUE;
    }
  }

  CDdev.STATUS &= ~IO_1733_ONCYL;

  iou->busy = TRUE;
  iou->oncyl = FALSE;
  iou->state = state;
  return TRUE;
}

/*
 * Set up a disk I/O operation with the A register containing FWA.
 */
static void StartCDDiskIO(UNIT *uptr, struct cdio_unit *iou, uint16 state)
{
  CDdev.CWA = IOAreg;

  CDdev.STATUS &= IO_ST_READY | IO_1733_ONCYL | IO_ST_PROT | IO_1733_SINGLE;

  fw_IOunderwayEOP2(&CDdev, 0);

  if ((cd_dev.dctrl & DBG_DTRACE) != 0)
    fprintf(DBGOUT,
            "%sCD - Start I/O, cur: %04X, len: %04X, state: %s\r\n",
            INTprefix, CDdev.CWA, CDdev.BUFLEN, CDstateStr[state]);

  CDdev.DCYLSTATUS &= ~iou->seekComplete;

  iou->state = state;
  sim_activate(uptr, CD_IO_WAIT);
}

/*
 * Increment sector # and update sector address. Note that I/O occurs on
 * side 0 followed by side 1 before moving to the next cylinder.
 */
void CDDiskIOIncSector(struct cdio_unit *iou)
{
  if (iou->disk != CD_NONE) {
    if (++iou->sector >= CD_NUMSC) {
      iou->sector = 0;
      iou->surface ^= 1;
      if (iou->surface == 0)
        iou->cylinder++;
    }
    iou->sectorAddr =
      ((iou->cylinder << CD_CYL_SHIFT) | (iou->surface << CD_SURF_SHIFT) |
       (iou->disk << CD_DISK_SHIFT)) | iou->sector;
    CDdev.CYLADRSTATUS = iou->sectorAddr;
  }
}

/*
 * Initiate a read operation on a disk.
 */
static enum cdio_status CDDiskIORead(UNIT *uptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  uint32 lba = CDLBA(iou);
  int i;

  if (iou->cylinder >= iou->maxcylinder)
    return CDIO_ADDRERR;

  CDdev.DCYLSTATUS &= ~CD_CYL_MASK;
  CDdev.DCYLSTATUS |= (iou->cylinder << CD_CYL_SHIFT);

  sim_fseeko(uptr->fileref, lba * CD_NUMBY, SEEK_SET);
  sim_fread(iou->buf, sizeof(uint16), CD_NUMWD, uptr->fileref);

  for (i = 0; i < CD_NUMWD; i++) {
    /*** TODO: fix protect check ***/
    if (!IOStoreToMem(CDdev.CWA, iou->buf[i], TRUE))
      return CDIO_PROTECT;

    CDdev.CWA++;
    if (--CDdev.BUFLEN == 0) {
      CDDiskIOIncSector(iou);
      return CDIO_DONE;
    }
  }
  CDDiskIOIncSector(iou);
  return CDIO_MORE;
}

/*
 * Initiate a write operation on a disk.
 */
static enum cdio_status CDDiskIOWrite(UNIT *uptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  uint32 lba = CDLBA(iou);
  t_bool fill = FALSE;
  int i;

  if (iou->cylinder >= iou->maxcylinder)
    return CDIO_ADDRERR;

  for (i = 0; i < CD_NUMWD; i++) {
    if (!fill) {
      iou->buf[i] = LoadFromMem(CDdev.CWA);
      CDdev.CWA++;
      if (--CDdev.BUFLEN == 0)
        fill = TRUE;
    } else iou->buf[i] = 0;
  }

  CDdev.DCYLSTATUS &= ~CD_CYL_MASK;
  CDdev.DCYLSTATUS |= (iou->cylinder << CD_CYL_SHIFT);

  sim_fseeko(uptr->fileref, lba * CD_NUMBY, SEEK_SET);
  sim_fwrite(iou->buf, sizeof(uint16), CD_NUMWD, uptr->fileref);
  CDDiskIOIncSector(iou);
  return fill ? CDIO_DONE : CDIO_MORE;
}

/*
 * Initiate a compare operation on a disk.
 */
static enum cdio_status CDDiskIOCompare(UNIT *uptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  uint32 lba = CDLBA(iou);
  int i;

  if (iou->cylinder >= iou->maxcylinder)
    return CDIO_ADDRERR;

  CDdev.DCYLSTATUS &= ~CD_CYL_MASK;
  CDdev.DCYLSTATUS |= (iou->cylinder << CD_CYL_SHIFT);

  sim_fseeko(uptr->fileref, lba * CD_NUMBY, SEEK_SET);
  sim_fread(iou->buf, sizeof(uint16), CD_NUMWD, uptr->fileref);

  for (i = 0; i < CD_NUMWD; i++) {
    if (iou->buf[i] != LoadFromMem(CDdev.CWA))
      return CDIO_MISMATCH;

    CDdev.CWA++;
    if (--CDdev.BUFLEN == 0) {
      CDDiskIOIncSector(iou);
      return CDIO_DONE;
    }
  }
  CDDiskIOIncSector(iou);
  return CDIO_MORE;
}

/*
 * Perform read/write/compare sector operations from within the unit
 * service routine.
 */
void CDDiskIO(UNIT *uptr, uint16 iotype)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  const char *error = "Unknown";
  enum cdio_status status;

  switch (iotype) {
    case CD_WRITE:
      status = CDDiskIOWrite(uptr);
      break;

    case CD_READ:
      status = CDDiskIORead(uptr);
      break;

    case CD_COMPARE:
      status = CDDiskIOCompare(uptr);
      break;
  }

  /*
   * Update the drive cylinder and cylinder address status registers if
   * the I/O was successful
   */
  if ((status == CDIO_MORE) || (status == CDIO_DONE)) {
    CDdev.CYLADRSTATUS =
      (iou->cylinder << CD_CYL_SHIFT) | (iou->surface << CD_SURF_SHIFT) |
      (iou->requested << CD_DISK_SHIFT) | iou->sector;
  }

  switch (status) {
    case CDIO_MORE:
      sim_activate(uptr, CD_IO_WAIT);
      break;

    case CDIO_PROTECT:
      CDdev.STATUS |= IO_1733_SPROT;
      error = "Protection Fault";
      goto err;

    case CDIO_ADDRERR:
      CDdev.STATUS |= IO_1733_ADDRERR;
      error = "Address Error";
  err:
      iou->state = CD_IDLE;

      if ((cd_dev.dctrl & DBG_DERROR) != 0)
        fprintf(DBGOUT,
                "%sCD - ReadWrite/Compare failed - %s\r\n",
                INTprefix, error);

      fw_IOalarm(FALSE, &cd_dev, &CDdev, "Alarm");
      break;

    case CDIO_MISMATCH:
      CDdev.STATUS |= IO_1733_NOCOMP;
      /* FALLTHROUGH */

    case CDIO_DONE:
      iou->state = CD_IDLE;

      if ((cd_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT,
                "%sCD - Read/Write/Compare transfer complete\r\n", INTprefix);

      fw_IOcompleteEOP2(TRUE, &cd_dev, &CDdev, 0xFFFF, "Transfer complete");
      break;
  }
}

/* Unit service */

t_stat cd_svc(UNIT *uptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  const char *why;

  if ((cd_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[CD: cd_svc() entry]\r\n", INTprefix);
    if ((cd_dev.dctrl & DBG_DSTATE) != 0)
      CDstate("svc_entry", &cd_dev, &CDdev);
  }

  switch (iou->state) {
    case CD_IDLE:
      /*
       * Unit is idle, nothing to do.
       */
      break;

    case CD_RTZS:
      why = "RTZS done";

      iou->cylinder = 0;
      iou->surface = 0;
      iou->disk = (cd_dev.flags & DEV_FIXED) != 0 ? 1 : 0;
      iou->sector = 0;
      iou->sectorAddr = iou->disk << CD_DISK_SHIFT;
      goto seekdone;

    case CD_SEEK:
      why = "Seek complete";

  seekdone:
      iou->state = CD_IDLE;
      iou->busy = FALSE;
      iou->oncyl = TRUE;

      CDdev.DCYLSTATUS &= ~CD_CYL_MASK;
      CDdev.DCYLSTATUS |= (iou->cylinder << CD_CYL_SHIFT) | iou->seekComplete;

      /*
       * If this is the currently selected drive, update controller status
       * and possibly generate an interrupt.
       */
      if (CDdev.iod_drive == iou) {
        CDdev.STATUS |= IO_1733_ONCYL;

        if ((cd_dev.dctrl & DBG_DTRACE) != 0)
          fprintf(DBGOUT, "%sCD - %s\r\n", INTprefix, why);

        if ((CDdev.STATUS & IO_ST_BUSY) == 0)
          fw_IOcompleteEOP2(FALSE, &cd_dev, &CDdev, 0xFFFF, why);
      }
      break;

    case CD_WRITE:
    case CD_READ:
    case CD_COMPARE:
      CDDiskIO(uptr, iou->state);
      break;

    case CD_WRITEADDR:
      why = "Write Address";
      goto WriteAddrDone;

    case CD_CHECKWORD:
      why = "Checkword Check";

  WriteAddrDone:
      iou->state = CD_IDLE;
      iou->oncyl = TRUE;
      iou->busy = FALSE;

      /*
       * Set sector address to the start of this track.
       */
      iou->sector = 0;
      CDdev.CYLADRSTATUS = iou->sectorAddr =
        (iou->cylinder << CD_CYL_SHIFT) | (iou->surface << CD_SURF_SHIFT) |
        (iou->disk << CD_DISK_SHIFT) | iou->sector;

      CDdev.STATUS |= IO_ST_EOP | IO_1733_ONCYL;
      CDdev.STATUS &= ~IO_ST_BUSY;

      if ((cd_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT, "%sCD - %s complete\r\n", INTprefix, why);

      fw_IOintr(TRUE, &cd_dev, &CDdev, 0, 0, 0xFFFF, why);
      break;
  }

  if ((cd_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[CD: cd_svc() exit]\r\n", INTprefix);
    if ((cd_dev.dctrl & DBG_DSTATE) != 0)
      CDstate("svc_exit", &cd_dev, &CDdev);
  }
  return SCPE_OK;
}

/* Reset routine */

static t_stat CDreset(DEVICE *dptr)
{
  struct cdio_unit *iou;
  int i;

  DEVRESET(&CDdev);

  if ((cd_dev.dctrl & DBG_DTRACE) != 0)
    fprintf(DBGOUT, "CD - Reset\r\n");

  CDunits[0].ondrive[0] = &cd_unit[0];
  CDunits[0].ondrive[1] = &cd_unit[1];
  CDunits[1].ondrive[0] = &cd_unit[2];
  CDunits[1].ondrive[1] = &cd_unit[3];
  CDunits[2].ondrive[0] = &cd_unit[4];
  CDunits[2].ondrive[1] = &cd_unit[5];
  CDunits[3].ondrive[0] = &cd_unit[6];
  CDunits[3].ondrive[1] = &cd_unit[7];

  for (i = 0; i < CD_NUMDR; i++) {
    /*
     * Cancel any I/O in progress
     */
    sim_cancel(&cd_unit[i * 2]);
    sim_cancel(&cd_unit[(i * 2) + 1]);

    CDunits[i].name[0] = '0' + i;
    CDunits[i].name[1] = '\0';

    CDunits[i].state = CD_IDLE;
    CDunits[i].disk = CD_NONE;
    CDunits[i].busy = FALSE;
    CDunits[i].oncyl = FALSE;
    if (((CDunits[i].ondrive[0]->flags & UNIT_ATT) != 0) ||
        ((CDunits[i].ondrive[1]->flags & UNIT_ATT) != 0))
      CDunits[i].oncyl = TRUE;

    CDunits[i].seekComplete = 1 << i;
  }

  CDdev.STATUS = 0;
  if ((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL)
    if (((iou->ondrive[0]->flags & UNIT_ATT) != 0) ||
        ((iou->ondrive[1]->flags & UNIT_ATT) != 0))
      CDdev.STATUS |= IO_ST_READY;

  CDdev.CYLADRSTATUS =
    CDdev.CWA =
    CDdev.CWSTATUS =
    CDdev.DCYLSTATUS =
    CDdev.BUFLEN = 0;

  return SCPE_OK;
}

t_stat cd_reset(DEVICE *dptr)
{
  t_stat r;

  if (IOFWinitialized)
    if ((dptr->flags & DEV_DIS) == 0)
      if ((r = checkReset(dptr, CDdev.iod_equip) == SCPE_OK)) {
        r = CDreset(dptr);

    /*
     * Cancel any selected drive.
     */
    CDdev.iod_drive = NULL;
  }
  return r;
}


/* Attach routine */

t_stat cd_attach(UNIT *uptr, CONST char *cptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  const char *drivetype = ((uptr->flags & UNIT_856_4) != 0) ? "856-4" : "856-2";
  t_addr capac = ((uptr->flags & UNIT_856_4) != 0) ? CD856_4_SIZE : CD856_2_SIZE;
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
    detach_unit(uptr);
    uptr->capac = capac;
    return sim_messagef(SCPE_OPENERR, "Incorrect file size");
  }
  /*
   * Set unit to cylinder 0, surface 0, sector 0 and indicate not on-cylinder,
   */
  iou->cylinder = 0;
  iou->surface = 0;
  iou->sector = 0;
  iou->oncyl = FALSE;

  return SCPE_OK;
}

/* Detach routine */

t_stat cd_detach(UNIT *uptr)
{
  struct cdio_unit *iou = (struct cdio_unit *)uptr->up7;
  t_stat stat;

  sim_cancel(uptr);
  stat = detach_unit(uptr);

  iou->oncyl = FALSE;
  if (iou->disk != CD_NONE)
    if (iou->ondrive[iou->disk] == uptr)
      iou->disk = CD_NONE;

  return SCPE_OK;
}

/* Check if I/O should be rejected */

t_bool CDreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  struct cdio_unit *iou = (struct cdio_unit *)iod->iod_drive;

  if (output) {
    switch (reg) {
      /*
       * Director function
       */
      case 0x01:
        /*** TODO: Check protect status ***/
        return (CDdev.STATUS & IO_ST_BUSY) != 0;

      /*
       * Load Buffer/Write/Checkword Check/Write Address
       */
      case 0x00:
      case 0x03:
      case 0x06:
      case 0x07:
        /*** TODO: Check protect status ***/
        /* FALLTHROUGH */

      /*
       * Load Address/Read/Compare
       */
      case 0x02:
      case 0x04:
      case 0x05:
        return ((DEVSTATUS(iod) &
                 (IO_ST_READY | IO_ST_BUSY | IO_1733_ONCYL)) !=
                (IO_ST_READY | IO_1733_ONCYL));
    }
  }
  return FALSE;
}

/* Perform I/O */

enum IOstatus CDin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * All input requests other than Clear Controller should be handled by
   * the I/O framework.
   */
  if (reg == 0) {
    struct cdio_unit *iou;

    CDreset(&cd_dev);

    if ((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) {
      int first = ((cd_dev.flags & DEV_FIXED) != 0) ? 1 : 0;

      if ((iou->ondrive[first]->flags & UNIT_ATT) != 0)
        iou->active = iou->ondrive[first];
      else iou->active = iou->ondrive[first ^ 0x01];

      iou->busy = TRUE;
      iou->state = CD_RTZS;
      sim_activate(iou->active, CD_RTZS_WAIT);
    }
    return IO_REPLY;
  }
  return IO_REJECT;
}

enum IOstatus CDout(IO_DEVICE *iod, uint8 reg)
{
  UNIT *uptr;
  struct cdio_unit *iou;

  switch (reg) {
    /*
     * Load Buffer
     */
    case 0x00:
      CDdev.BUFLEN = IOAreg;
      CDdev.STATUS &= IO_ST_READY | IO_1733_ONCYL | IO_ST_PROT | IO_1733_SINGLE;
      break;

    /*
     * Director function
     */
    case 0x01:
      /*
       * Clear interrupt active and end of operation
       */
      CDdev.STATUS &= ~(IO_ST_INT | IO_ST_EOP);

      /*
       * Changing the device interrupt mask does not cause an interrupt if
       * any of the newly masked conditions are true.
       */
      doDirectorFunc(&cd_dev, TRUE);

      /*
       * Handle select/de-select.
       */
      if ((IOAreg & (IO_1733_USEL | IO_1733_UDSEL)) != 0) {
        uint16 unit = (IOAreg & IO_1733_USC) >> 9;
        struct cdio_unit *iou = &CDunits[unit];

        if ((IOAreg & IO_1733_UDSEL) != 0) {
          /*** TODO: Check protect conditions ***/
        }

        if ((IOAreg & IO_1733_USEL) != 0) {
          CDdev.iod_drive = NULL;
          CDdev.STATUS &= ~(IO_1733_ONCYL | IO_ST_BUSY | IO_ST_READY);
          
          if (((iou->ondrive[0]->flags & UNIT_ATT) != 0) ||
              ((iou->ondrive[1]->flags & UNIT_ATT) != 0)) {
            CDdev.iod_drive = iou;
            CDdev.STATUS |= IO_ST_READY;

            if (iou->active == NULL) {
              int first = ((cd_dev.flags & DEV_FIXED) != 0) ? 1 : 0;

              if ((iou->ondrive[first]->flags & UNIT_ATT) != 0)
                iou->active = iou->ondrive[first];
              else iou->active = iou->ondrive[first ^ 0x01];
            }

            if (iou->oncyl) {
              CDdev.STATUS |= IO_1733_ONCYL;
              CDdev.CYLADRSTATUS = iou->sectorAddr;
            }
          }         
        }
      }
      break;

    /*
     * Load Address
     */
    case 0x02:
      if (((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) &&
          ((uptr = iou->active) != NULL) && !iou->busy && iou->oncyl ) {
        if (LoadDiskAddress(uptr, iou, CD_SEEK)) {
          CDdev.STATUS &= IO_ST_READY | IO_1733_ONCYL | IO_ST_PROT | IO_1733_SINGLE;
          /*
           * If IO_1733_ONCYL is set, we must already be at the requested
           * cylinder and no seek will be required.
           */
          if ((CDdev.STATUS & IO_1733_ONCYL) != 0)
            break;

          sim_activate(uptr, CD_SEEK_WAIT);
        } else {
          if ((cd_dev.dctrl & DBG_DERROR) != 0)
            fprintf(DBGOUT,
                    "%sCD - Bad Load Address (%04X)\r\n", INTprefix, Areg);
          
          fw_IOintr(FALSE, &cd_dev, &CDdev, IO_1733_ADDRERR | IO_ST_EOP |IO_ST_ALARM, 0, 0xFFFF, "Bad load address");
        }
      } else return IO_REJECT;
      break;

    /*
     * Write
     */
    case 0x03:
      if (((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) &&
          ((uptr = iou->active) != NULL) &&
          ((uptr->flags & UNIT_ATT) != 0)) {
        StartCDDiskIO(uptr, iou, CD_WRITE);
      } else return IO_REJECT;
      break;

    /*
     * Read
     */
    case 0x04:
      if (((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) &&
          ((uptr = iou->active) != NULL) &&
          ((uptr->flags & UNIT_ATT) != 0)) {
        StartCDDiskIO(uptr, iou, CD_READ);
      } else return IO_REJECT;
      break;

    /*
     * Compare
     */
    case 0x05:
      if (((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) &&
          ((uptr = iou->active) != NULL) &&
          ((uptr->flags & UNIT_ATT) != 0)) {
        StartCDDiskIO(uptr, iou, CD_COMPARE);
      } else return IO_REJECT;
      break;

    /*
     * Checkword check
     */
    case 0x06:
      if (((iou = (struct cdio_unit *)CDdev.iod_drive) != NULL) &&
          ((uptr = iou->active) != NULL) &&
          ((uptr->flags & UNIT_ATT) != 0)) {
        if (LoadDiskAddress(uptr, iou, CD_CHECKWORD)) {
          CDdev.STATUS &= IO_ST_READY | IO_1733_ONCYL | IO_ST_PROT | IO_1733_SINGLE;
          CDdev.STATUS |= IO_ST_BUSY;
          sim_activate(uptr, CD_SEEK_WAIT);
        } else {
          if ((cd_dev.dctrl & DBG_DERROR) != 0)
            fprintf(DBGOUT,
                    "%sCD: Bad Checkword Address (%04X)\r\n",
                    INTprefix, Areg);

          fw_IOintr(FALSE, &cd_dev, &CDdev, IO_1733_ADDRERR | IO_ST_EOP | IO_ST_ALARM, 0, 0xFFFF, "Bad checkword");
        }
      } else return IO_REJECT;
      break;
  }
  rebuildPending();
  return IO_REPLY;
}

/*
 * Autoload support
 */
t_stat CDautoload(void)
{
  UNIT *uptr = &cd_unit[(cd_dev.flags & DEV_FIXED) ==0 ? 0 : 1];

  if ((uptr->flags & UNIT_ATT) != 0) {
    uint32 i;

    for (i = 0; i < CD_NUMSC; i++) {
      t_offset offset = i * CD_NUMBY;
      void * buf = &M[i * CD_NUMWD];

      sim_fseeko(uptr->fileref, offset, SEEK_SET);
      if (sim_fread(buf, sizeof(uint16), CD_NUMWD, uptr->fileref) != CD_NUMWD)
        return SCPE_IOERR;
    }
    return SCPE_OK;
  }
  return SCPE_UNATT;
}

t_stat cd_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1733-2 cartridge disk drive controller.\n"
    "1 Hardware Description\n"
    " The 1733-2 consists of a controller with up to 4 attached disk drives.\n"
    " Each drive consists of 2 logical disks; a removeable disk pack and a\n"
    " fixed disk. The controller includes a jumper which controls which disk\n"
    " is addressed as logical disk 0:\n\n"
    "+sim> SET %D CARTFIRST\n"
    "+sim> SET %D FIXEDFIRST\n\n"
    " Each physical drive may be configured as a 856-2 or 856-4 and both the\n"
    " fixed and removeable disks must be the same size.\n\n"
    "+856-2 drive: 1130304 words per disk\n"
    "+856-4 drive: 2271744 words per disk\n\n"
    " The configuration may be changed by referencing either of the logical\n"
    " disks present on a drive:\n\n"
    "+sim> SET %U 856-2\n"
    "+sim> SET %U 856-4\n\n"
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
