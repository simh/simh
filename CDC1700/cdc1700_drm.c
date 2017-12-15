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

/* cdc1700_drm.c: drum memory controller
 *                Simh device: drm
 */

/*
 * Notes:
 *
 * 1. The 1752 Drum Memory Subsystem consists of a 3600 RPM drum with 32
 *    sectors (96 words each) per track. There can be 64 - 1024 tracks
 *    depending on the model ordered.
 *
 *    There is 1 readable register which needs to be handled specially - the
 *    Sector Address Status. This register consists of 3 fields:
 *
 *      - Current sector address as read from the drum (0 - 31)
 *      - Current track address from last I/O request (zero on startup)
 *      - Core address compare - set if transfer is to last address of buffer
 *
 *    The SMM17 diagnostic for the 1752 uses this register to verify that
 *    the hardware is operational before allowing the diagnostic to run. The
 *    register fields will be implemented as follows:
 *
 *    1. Current sector address
 *
 *       Rather than use a repeating service routine, we will timestamp
 *       (using the instruction count) when the Sector Address Status register
 *       was last referenced or used as part of an I/O operation. When the
 *       register is next referenced, we will compute the number of sectors
 *       which have passed under the head (521 uSec/sector so 350 instructions
 *       assuming 1.5 microsecond/instruction) and update the sector address
 *       field appropriately. If an I/O is active, the sector address will
 *       reflect that used by the current I/O. This may result in a sudden
 *       change in value.
 *
 *    2. Current track address
 *
 *       This will be the last track address referenced by an I/O request.
 * 
 *    3. Core address compare
 *
 *       This bit is set when the current/next DMA request is to the last
 *       address of the buffer. Rather than simulate DMA word at a time,
 *       we simulate sector transfer at a time. We will set this bit if the
 *       last address of the buffer is somewhere within the current sector
 *       and also set the Core Address Status to be the last address of
 *       the buffer.
 *
 * 2. This is the first, and only, device driver which requires dynamic
 *    processing of the Director Status Register. The Sector Compare is only
 *    set when the requested sector is under the read head. The I/O framework
 *    did not require any changes to allow this to work.
 */

#include "cdc1700_defs.h"

#define SASTATUS        iod_readR[2]
#define CASTATUS        iod_readR[3]
#define DATASTATUS      iod_readR[4]

extern char INTprefix[];

extern void RaiseExternalInterrupt(DEVICE *);

extern t_bool doDirectorFunc(DEVICE *, t_bool);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_IOunderwayEOP2(IO_DEVICE *, uint16);
extern void fw_IOcompleteEOP2(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOalarm(t_bool, DEVICE *, IO_DEVICE *, const char *);
extern void fw_IOintr(t_bool, DEVICE *, IO_DEVICE *, uint16, uint16, uint16, const char *);

extern void rebuildPending(void);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_protected(UNIT *, int32, CONST char *, void *);
extern t_stat clear_protected(UNIT *, int32, CONST char *, void *);

extern t_stat set_equipment(UNIT *, int32, CONST char *, void *);

extern t_stat set_stoponrej(UNIT *, int32, CONST char *, void *);
extern t_stat clr_stoponrej(UNIT *, int32, CONST char *, void *);

extern uint16 LoadFromMem(uint16);
extern t_bool IOStoreToMem(uint16, uint16, t_bool);

extern uint16 M[], Areg, IOAreg;
extern t_uint64 Instructions;

extern t_bool IOFWinitialized;

extern UNIT cpu_unit;

t_stat drm_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/* Constants */

#define DRM_NUMWD       (96)            /* words/sector */
#define DRM_NUMBY       (DRM_NUMWD * sizeof(uint16))
#define DRM_NUMSC       (32)            /* sectors/track */
#define DRM_SIZE        (512 * DRM_NUMSC * DRM_NUMBY)
#define DRM_MINTRACKS   (64)            /* Min # of tracks supported */
#define DRM_MAXTRACKS   (1024)          /* Max # of tracks supported */

#define DRM_AUTOLOAD    (16)            /* Sectors to autoload */

/*
 * Drum address fields
 */
#define DRM_TRK_MASK    0x7FE0          /* Track address */
#define DRM_TRK_SHIFT   5               /*    and shift */
#define DRM_SEC_MASK    0x001F          /* Sector mask */

#define DRM_COMPARE     0x8000          /* Core address compare */

enum drmio_status {
  DRMIO_MORE,                           /* More I/O pending */
  DRMIO_DONE,                           /* I/O processing complete */
  DRMIO_PROTECT,                        /* Protect fault */
  DRMIO_ADDRERR                         /* Addressing error */
};

t_stat drm_svc(UNIT *);
t_stat drm_reset(DEVICE *);
t_stat drm_attach(UNIT *, CONST char *);
t_stat drm_detach(UNIT *);

t_stat drm_set_size(UNIT *, int32, CONST char *, void *);

void DRMstate(const char *, DEVICE *, IO_DEVICE *);
t_bool DRMreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus DRMin(IO_DEVICE *, uint8);
enum IOstatus DRMout(IO_DEVICE *, uint8);
void DRMclear(DEVICE *);
uint8 DRMdecode(IO_DEVICE *, t_bool, uint8);

/*
        1752-A/B/C/D Drum memory controller

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A
  (Bits 03-00)

      0000              Initiate Write Op    Illegal
      0001              Director Function    Director Status
      0010              Illegal              Sector Address Status
      0011              Director Function    Core Address Status
      0100              Initiate Read Op     Data Status
      0101              Director Function    Illegal
      0110              Illegal              Illegal
      0111              Director Function    Illegal
      1000              Load ISA             Illegal
      1001              Director Function    Illegal
      1010              Illegal              Illegal
      1011              Director Function    Illegal
      1100              Load Initial Addr    Illegal
      1101              Director Function    Illegal
      1110              Load Final Addr      Illegal
      1111              Director Function    Illegal

  Operations:

  Initiate Drum Write Operation

    15  14                                                       0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X | X | X | X | X | X | X | X |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

  Director Function

    15                                           4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X | X | X |   |   | X |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                                 |   |       |   |
                                                 |   |       |   Clr Controller
                                                 |   |       Clr Interrupts
                                                 |   EOP Interrupt Req.
                                                 Interrupt on Alarm

  Initiate Drum Read Operation

    15  14                                                       0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X | X | X | X | X | X | X | X | X | X | X |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

  Load Initial Sector Address

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | 0 |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
         |                                   |   |               |
         +-----------------------------------+   +---------------+
             Desired Initial Track Address        Desired Initial
                                                  Sector Addr - 1

  Load Initial Core Address, Load Final Core Address

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                                                           |
     +-----------------------------------------------------------+
                             Core Address

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
     |   |   |   |   |   |   |   |   |   |   |   |   Data
     |   |   |   |   |   |   |   |   |   |   |   End of Operation
     |   |   |   |   |   |   |   |   |   |   Alarm
     |   |   |   |   |   |   |   |   |   Lost Data
     |   |   |   |   |   |   |   |   Protected
     |   |   |   |   |   |   |   Checkword Error
     |   |   |   |   |   |   Protect Fault
     |   |   |   |   |   Guarded Address Enabled
     |   |   |   |   Timing Track Error
     |   |   |   Power Failure
     |   |   Sector Compare
     |   Guarded Address Error
     Sector Overrange Error

  Sector Address Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   |                                   |   |               |
     |   +-----------------------------------+   +---------------+
     |      Track Address Register Contents       Sector Address
     |                                            Register Contents
     Core Address Compare

  Core Address Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                                                           |
     +-----------------------------------------------------------+
                      Core Address Register Contents

  Data Status

    15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                                                           |
     +-----------------------------------------------------------+
                       Data Register Zero Contents

*/

IO_DEVICE DRMdev = IODEV(NULL, "1752", 1752, 2, 0xFF, 0,
                         DRMreject, DRMin, DRMout, NULL, NULL,
                         DRMstate, NULL, NULL, DRMclear, DRMdecode, NULL,
                         0x7F, 16,
                         MASK_REGISTER0 | MASK_REGISTER1 | MASK_REGISTER2 | \
                         MASK_REGISTER3 | MASK_REGISTER4 | MASK_REGISTER5 | \
                         MASK_REGISTER6 | MASK_REGISTER7 | MASK_REGISTER8 | \
                         MASK_REGISTER9 | MASK_REGISTER10 | MASK_REGISTER11 | \
                         MASK_REGISTER12 | MASK_REGISTER13 | \
                         MASK_REGISTER14 | MASK_REGISTER15,
                         MASK_REGISTER3 | MASK_REGISTER4,
                         MASK_REGISTER0 | MASK_REGISTER5 | MASK_REGISTER6 | \
                         MASK_REGISTER7 | MASK_REGISTER8 | MASK_REGISTER9 | \
                         MASK_REGISTER10 | MASK_REGISTER11 | \
                         MASK_REGISTER12 | MASK_REGISTER13 | \
                         MASK_REGISTER14 | MASK_REGISTER15,
                         MASK_REGISTER2 | MASK_REGISTER6 | MASK_REGISTER10,
                         0, 0, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_tracks      iod_private     /* # of tracks on device */
#define iod_compare     iod_private4    /* TRUE if DMA of last buffer word */
#define iod_isa         iod_private6    /* Initial sector address */
#define iod_ica         iod_private7    /* Initial core address */
#define iod_fca         iod_private8    /* Final core address */
#define iod_state       iod_private9    /* Current controller state */
#define iod_ca          iod_private11   /* Current DMA address */
#define iod_trk         iod_private12   /* Current track # */
#define iod_sec         iod_private13   /* Current sector # */

#define DRM_IDLE        0x00            /* Idle */
#define DRM_WRITE       0x01            /* Write data */
#define DRM_READ        0x02            /* Read data */

/* DRM data structures

   drm_dev      DRM device descriptor
   drm_unit     DRM unit descriptor
   drm_reg      DRM register list
   drm_mod      DRM modifier list
*/

UNIT drm_unit = {
  UDATA(&drm_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE, DRM_SIZE)
};

REG drm_reg[] = {
  { HRDATAD(FUNCTION, DRMdev.FUNCTION, 16, "Last director function issued") },
  { HRDATAD(STATUS, DRMdev.STATUS, 16, "Director status register") },
  { HRDATAD(IENABLE, DRMdev.IENABLE, 16, "Interrupts enabled") },
  /*** more ***/
  { NULL }
};

MTAB drm_mod[] = {
  { MTAB_XTD | MTAB_VDV, 0, "1752 Drum Memory Controller" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Set/Display equipment address" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "STOPONREJECT",
    &set_stoponrej, NULL, NULL, "Stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOSTOPONREJECT",
    &clr_stoponrej, NULL, NULL, "Don't stop simulation if I/O is rejected" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "PROTECT",
    &set_protected, NULL, NULL, "Device is protected (unimplemented)" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOPROTECT",
    &clear_protected, NULL, NULL, "Device is unprotected (unimplemented)" },
  { UNIT_DRMSIZE, 64, NULL, "64",
    &drm_set_size, NULL, NULL, "Set drum storage to 64 tracks" },
  { UNIT_DRMSIZE, 128, NULL, "128",
    &drm_set_size, NULL, NULL, "Set drum storage to 128 tracks" },
  { UNIT_DRMSIZE, 192, NULL, "192",
    &drm_set_size, NULL, NULL, "Set drum storage to 192 tracks" },
  { UNIT_DRMSIZE, 256, NULL, "256",
    &drm_set_size, NULL, NULL, "Set drum storage to 256 tracks" },
  { UNIT_DRMSIZE, 320, NULL, "320",
    &drm_set_size, NULL, NULL, "Set drum storage to 320 tracks" },
  { UNIT_DRMSIZE, 384, NULL, "384",
    &drm_set_size, NULL, NULL, "Set drum storage to 384 tracks" },
  { UNIT_DRMSIZE, 448, NULL, "448",
    &drm_set_size, NULL, NULL, "Set drum storage to 448 tracks" },
  { UNIT_DRMSIZE, 512, NULL, "512",
    &drm_set_size, NULL, NULL, "Set drum storage to 512 tracks" },
  { UNIT_DRMSIZE, 576, NULL, "576",
    &drm_set_size, NULL, NULL, "Set drum storage to 576 tracks" },
  { UNIT_DRMSIZE, 640, NULL, "640",
    &drm_set_size, NULL, NULL, "Set drum storage to 640 tracks" },
  { UNIT_DRMSIZE, 704, NULL, "704",
    &drm_set_size, NULL, NULL, "Set drum storage to 704 tracks" },
  { UNIT_DRMSIZE, 768, NULL, "768",
    &drm_set_size, NULL, NULL, "Set drum storage to 768 tracks" },
  { UNIT_DRMSIZE, 832, NULL, "832",
    &drm_set_size, NULL, NULL, "Set drum storage to 832 tracks" },
  { UNIT_DRMSIZE, 896, NULL, "896",
    &drm_set_size, NULL, NULL, "Set drum storage to 896 tracks" },
  { UNIT_DRMSIZE, 960, NULL, "960",
    &drm_set_size, NULL, NULL, "Set drum storage to 960 tracks" },
  { UNIT_DRMSIZE, 1024, NULL, "1024",
    &drm_set_size, NULL, NULL, "Set drum storage to 1024 tracks" },
  { 0 }
};

DEBTAB drm_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "ERROR",       DBG_DERROR,     "Display device errors" },
  { "LOCATION",    DBG_DLOC,       "Display address for I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "ALL", DBG_DTRACE | DBG_DSTATE | DBG_DINTR | DBG_DERROR | DBG_DLOC },
  { NULL }
};

DEVICE drm_dev = {
  "DRM", &drm_unit, drm_reg, drm_mod,
  1, 10, 31, 1, 8, 8,
  NULL, NULL, &drm_reset,
  NULL, &drm_attach, &drm_detach,
  &DRMdev,
  DEV_DEBUG | DEV_DISK | DEV_DISABLE | DEV_INDEV | DEV_OUTDEV | DEV_PROTECT,
  0, drm_deb,
  NULL, NULL, &drm_help, NULL, NULL, NULL
};

/*
 * Dump the current internal state of the DRM device.
 */
const char *DRMStateStr[] = {
  "Idle", "Write", "Read"
};

void DRMstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  fprintf(DBGOUT,
          "%s[%s %s: %s, Func: %04X, Sta: %04X, Ena: %04X]\r\n",
          INTprefix, dev->name, where, DRMStateStr[iod->iod_state],
          iod->FUNCTION, iod->STATUS, iod->IENABLE);
  fprintf(DBGOUT,
          "%s[%s: ISA: %04X, ICA: %04X, FCA: %04X, SAS: %04X, CAS: %04X]\r\n",
          INTprefix, dev->name,
          iod->iod_isa, iod->iod_ica, iod->iod_fca,
          iod->SASTATUS, iod->CASTATUS);
  fprintf(DBGOUT,
          "%s[%s: Trk: %03X, Sec: %02X, Cur: %04X, Comp: %c]\r\n",
          INTprefix, dev->name, iod->iod_trk, iod->iod_sec, iod->iod_ca,
          iod->iod_compare ? 'T' : 'F');
}

/*
 * Load and validate drum address in the Initial Sector Address Register
 */
static t_bool LoadDrumAddress(void)
{
  uint16 track = (DRMdev.iod_isa & DRM_TRK_MASK) >> DRM_TRK_SHIFT;

  if (track >= DRMdev.iod_tracks)
    return FALSE;

  /*
   * The sector field initially holds (sector # - 1) and needs to be
   * incremented without touching the track #.
   */
  DRMdev.iod_trk = track;
  DRMdev.iod_sec = (DRMdev.iod_isa + 1) & DRM_SEC_MASK;
  return TRUE;
}

/*
 * Set up a drum I/O operation using the currently set parameters
 */
static void StartDrumIO(t_bool wr)
{
  if (LoadDrumAddress()) {
    DRMdev.iod_compare =(DRMdev.iod_fca >= DRMdev.CASTATUS) &&
      (DRMdev.iod_fca < (DRMdev.CASTATUS + DRM_NUMWD));
    DRMdev.iod_ca = DRMdev.iod_ica;
    DRMdev.CASTATUS = DRMdev.iod_compare ? DRMdev.iod_ica : DRMdev.iod_fca;

    fw_IOunderwayEOP2(&DRMdev, IO_ST_DATA);

    if ((drm_dev.dctrl & DBG_DTRACE) != 0) {
      fprintf(DBGOUT,
              "%sDRM - Start %s I/O, Trk: %03X, Sec: %02X, Start: %04X, End: %04X\r\n",
              INTprefix, wr ? "Write" : "Read",
              DRMdev.iod_trk, DRMdev.iod_sec, DRMdev.iod_ica, DRMdev.iod_fca);
    }

    DRMdev.iod_state = wr ? DRM_WRITE : DRM_READ;
    sim_activate(&drm_unit, DRM_ACCESS_WAIT);
    return;
  }

  /*
   * Generate a sector overrange error and possible interrupt.
   */
  DRMdev.STATUS &= ~IO_ST_DATA;
  DRMdev.STATUS |= IO_1752_OVERR;
  fw_IOalarm(FALSE, &drm_dev, &DRMdev, "Invalid track #");
}

/*
 * Increment the drum sector address.
 */
static void DrumIOIncSector(void)
{
  DRMdev.iod_sec = (DRMdev.iod_sec + 1) & DRM_SEC_MASK;
  if (DRMdev.iod_sec == 0)
    DRMdev.iod_trk++;
}

/*
 * Initiate a read operation on the drum.
 */
static enum drmio_status DrumIORead(UNIT *uptr)
{
  uint16 buf[DRM_NUMWD];
  uint32 lba = (DRMdev.iod_trk << DRM_TRK_SHIFT) | DRMdev.iod_sec;
  int i;

  if (DRMdev.iod_trk >= DRMdev.iod_tracks)
    return DRMIO_ADDRERR;

  /*
   * Report any error in the underlying container infrastructure as an
   * address error.
   */
  if (sim_fseeko(uptr->fileref, lba * DRM_NUMBY, SEEK_SET) ||
      (sim_fread(buf, sizeof(uint16), DRM_NUMWD, uptr->fileref) != DRM_NUMWD))
    return DRMIO_ADDRERR;

  for (i = 0; i < DRM_NUMWD; i++) {
    /*** TODO: fix protect check ***/
    if (!IOStoreToMem(DRMdev.iod_ca, buf[i], TRUE))
      return DRMIO_PROTECT;

    DRMdev.DATASTATUS = buf[i];

    if (DRMdev.iod_ca++ == DRMdev.iod_fca) {
      DRMdev.CASTATUS = DRMdev.iod_ca;
      DrumIOIncSector();
      return DRMIO_DONE;
    }
  }
  DrumIOIncSector();
  if ((drm_dev.dctrl & DBG_DTRACE) != 0)
    fprintf(DBGOUT,
            "%sDRM - Continue Read I/O, Trk: %03X, Sec: %02X, Cur: %04X, End: %04X\r\n",
            INTprefix, DRMdev.iod_trk, DRMdev.iod_sec,
            DRMdev.iod_ca, DRMdev.iod_fca);

  return DRMIO_MORE;
}

/*
 * Initiate a write operation on the drum.
 */
static enum drmio_status DrumIOWrite(UNIT *uptr)
{
  uint16 buf[DRM_NUMWD];
  uint32 lba = (DRMdev.iod_trk << DRM_TRK_SHIFT) | DRMdev.iod_sec;
  t_bool done = FALSE;
  int i;

  if (DRMdev.iod_trk >= DRMdev.iod_tracks)
    return DRMIO_ADDRERR;

  memset(buf, 0, sizeof(buf));

  for (i = 0; i < DRM_NUMWD; i++) {
    DRMdev.DATASTATUS = buf[i] = LoadFromMem(DRMdev.iod_ca);
    if (DRMdev.iod_ca++ == DRMdev.iod_fca) {
      DRMdev.CASTATUS = DRMdev.iod_ca;
      done = TRUE;
      break;
    }
  }

  /*
   * Report any error in the underlying container infrastructure as an
   * address error.
   */
  if (sim_fseeko(uptr->fileref, lba * DRM_NUMBY, SEEK_SET) ||
      (sim_fwrite(buf, sizeof(uint16), DRM_NUMWD, uptr->fileref) != DRM_NUMWD))
    return DRMIO_ADDRERR;

  DrumIOIncSector();
  if (((drm_dev.dctrl & DBG_DTRACE) != 0) && !done)
    fprintf(DBGOUT,
            "%sDRM - Continue Write I/O, Trk: %03X, Sec: %02X, Cur: %04X, End: %04X\r\n",
            INTprefix, DRMdev.iod_trk, DRMdev.iod_sec,
            DRMdev.iod_ca, DRMdev.iod_fca);

  return done ? DRMIO_DONE : DRMIO_MORE;
}

/*
 * Perform read/write sector operations from within the unit service routine.
 */
void DrumIO(UNIT *uptr, uint8 iotype)
{
  const char *error = "Unknown";
  enum drmio_status status = DRMIO_ADDRERR;

  switch (iotype) {
    case DRM_WRITE:
      status = DrumIOWrite(uptr);
      break;

    case DRM_READ:
      status = DrumIORead(uptr);
      break;
  }

  /*
   * Update the sector address status register if the I/O was successful.
   * Note that since we perform sector at a time I/O, we assert the "Core
   * Address Compare" bit for the entire period.
   */
  if ((status == DRMIO_MORE) || (status == DRMIO_DONE)) {
    DRMdev.iod_compare =(DRMdev.iod_fca >= DRMdev.iod_ca) &&
      (DRMdev.iod_fca < (DRMdev.iod_ca + DRM_NUMWD));
  }

  switch (status) {
    case DRMIO_MORE:
      sim_activate(uptr, DRM_SECTOR_WAIT);
      break;

    case DRMIO_PROTECT:
      DRMdev.STATUS |= IO_1752_PROTF;
      error = "Protection Fault";
      goto err;

    case DRMIO_ADDRERR:
      DRMdev.STATUS |= IO_1752_OVERR;
      error = "Address Error";
  err:
      DRMdev.iod_compare = FALSE;
      DRMdev.iod_state = DRM_IDLE;

      if ((drm_dev.dctrl & DBG_DERROR) != 0)
        fprintf(DBGOUT,
                "%sDRM - Read/Write failed - %s\r\n",
                INTprefix, error);

      fw_IOalarm(FALSE, &drm_dev, &DRMdev, "Alarm");
      break;

    case DRMIO_DONE:
      DRMdev.iod_compare = FALSE;
      DRMdev.iod_event = Instructions;
      DRMdev.iod_state = DRM_IDLE;

      if ((drm_dev.dctrl & DBG_DTRACE) != 0)
        fprintf(DBGOUT,
                "%sDRM - Read/Write transfer complete\r\n", INTprefix);

      DRMdev.STATUS |= IO_ST_DATA;
      fw_IOcompleteEOP2(FALSE, &drm_dev, &DRMdev, 0xFFFF, "Transfer complete");
      break;
  }
}

/* Unit service */

t_stat drm_svc(UNIT *uptr)
{
  if ((drm_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[DRM: drm_svc() entry]\r\n", INTprefix);
    if ((drm_dev.dctrl & DBG_DSTATE) != 0)
      DRMstate("svc_entry", &drm_dev, &DRMdev);
  }

  switch (DRMdev.iod_state) {
    case DRM_IDLE:
      /*
       * Unit is idle, nothing to do.
       */
      break;

    case DRM_WRITE:
    case DRM_READ:
      DrumIO(uptr, DRMdev.iod_state);
      break;
  }

  if ((drm_dev.dctrl & DBG_DTRACE) != 0) {
    fprintf(DBGOUT, "%s[DRM: drm_svc() exit]\r\n", INTprefix);
    if ((drm_dev.dctrl & DBG_DSTATE) != 0)
      DRMstate("svc_exit", &drm_dev, &DRMdev);
  }
  return SCPE_OK;
}

/* Reset routine */

t_stat drm_reset(DEVICE *dptr)
{
  DRMdev.STATUS = 0;
  if ((drm_unit.flags & UNIT_ATT) != 0)
    DRMdev.STATUS |= IO_ST_READY | IO_ST_DATA;

  DRMdev.SASTATUS =
    DRMdev.CASTATUS =
    DRMdev.DATASTATUS = 0;

  DRMdev.iod_trk =
    DRMdev.iod_sec = 0;

  DRMdev.iod_event = Instructions;

  return SCPE_OK;
}

/* Attach routine */

t_stat drm_attach (UNIT *uptr, CONST char *cptr)
{
  t_addr capac = uptr->capac;
  t_stat r;
  t_offset tracks;

  r = attach_unit(uptr, cptr);
  if (r != SCPE_OK)
    return r;

  /*
   * If this ia a newly created file, set the drum size appropriately.
   */
  if (sim_fsize_ex(uptr->fileref) == 0)
    sim_set_fsize(uptr->fileref, capac);

  /*
   * If we are attaching to an existing file, make sure that its size:
   *
   *   - is a multiple of 3072 words
   *   - is between 64 and 1024 tracks (each of 3072 words)
   *   - is a multiple of 64 tracks
   */
  tracks = sim_fsize_ex(uptr->fileref) / (DRM_NUMSC * DRM_NUMBY);
  if (((sim_fsize_ex(uptr->fileref) % (DRM_NUMSC * DRM_NUMBY)) != 0) ||
      (tracks < DRM_MINTRACKS) ||
      (tracks > DRM_MAXTRACKS) ||
      ((tracks & (DRM_MINTRACKS - 1)) != 0)) {
    detach_unit(uptr);
    uptr->capac = capac;
    return sim_messagef(SCPE_OPENERR, "Invalid file size");
  }
  DRMdev.STATUS = IO_ST_READY | IO_ST_DATA;
  DRMdev.iod_tracks = (uint16)tracks;
  DRMdev.iod_event = Instructions;

  return SCPE_OK;
}

/* Detach routine */

t_stat drm_detach(UNIT *uptr)
{
  sim_cancel(uptr);
  DRMdev.STATUS &= ~(IO_ST_READY | IO_ST_DATA);
  return detach_unit(uptr);
}

/*
 * Change drum capacity
 */
t_stat drm_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if ((uptr->flags & UNIT_ATT) != 0)
    return SCPE_ALATT;

  drm_unit.capac = val * DRM_NUMSC * DRM_NUMBY;
  DRMdev.iod_tracks = val;
  return SCPE_OK;
}

/* Check if I/O should be rejected */

t_bool DRMreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  if (output) {
    if (reg != 0x1)
      return (DRMdev.STATUS & IO_ST_BUSY) != 0;
  }
  return FALSE;
}

/* Perform I/O */

enum IOstatus DRMin(IO_DEVICE *iod, uint8 reg)
{
  /*
   * The I/O framework passes input requests for the Director Status register
   * and the Sector Address Status register so that we can return values
   * which are dependent on the rotational position of the drum.
   */
  
  /*
   * Update the current sector value.
   */
  if (DRMdev.iod_state == DRM_IDLE) {
    t_uint64 sectors = (Instructions - DRMdev.iod_event) / DRM_SECTOR_WAIT;
    
    if (sectors != 0) {
      DRMdev.iod_sec = (DRMdev.iod_sec + sectors) & DRM_SEC_MASK;
      DRMdev.iod_event += sectors * DRM_SECTOR_WAIT;
    }
  }

  switch (reg) {
    case 0x01:
      /*
       * Director Status
       */
      if (DRMdev.iod_sec == (DRMdev.iod_isa & DRM_SEC_MASK))
        DRMdev.STATUS |= IO_1752_SECCMP;
      else DRMdev.STATUS &= ~IO_1752_SECCMP;
      Areg = DRMdev.STATUS;
      return IO_REPLY;

    case 0x02:
      /*
       * Sector Address Status
       */
      DRMdev.SASTATUS = (DRMdev.iod_trk << DRM_TRK_SHIFT) | DRMdev.iod_sec;
      if (DRMdev.iod_compare)
        DRMdev.SASTATUS |= DRM_COMPARE;
      Areg = DRMdev.SASTATUS;
      return IO_REPLY;
  }
  return IO_REJECT;
}

enum IOstatus DRMout(IO_DEVICE *iod, uint8 reg)
{
  switch (reg) {
    case 0x00:
      /*
       * Initiate Drum Write Operation
       */
      StartDrumIO(TRUE);
      break;

    case 0x01:
      /*
       * Director function.
       */
      doDirectorFunc(&drm_dev, FALSE);
      break;

    case 0x04:
      /*
       * Initiate Drum Read Operation
       */
      StartDrumIO(FALSE);
      break;

    case 0x08:
      /*
       * Load Initial Sector Address Register.
       */
      DRMdev.iod_isa = IOAreg;
      DRMdev.iod_trk = (IOAreg & DRM_TRK_MASK) >> DRM_TRK_SHIFT;
      break;

    case 0x0C:
      /*
       * Load Initial Core Address Register
       */
      DRMdev.iod_ica = DRMdev.CASTATUS = IOAreg;
      DRMdev.iod_compare = DRMdev.iod_ica == DRMdev.iod_fca;
      break;

    case 0x0E:
      /*
       * Load Final Core Address Register
       */
      DRMdev.iod_fca = IOAreg;
      DRMdev.iod_compare = DRMdev.iod_ica == DRMdev.iod_fca;
      break;
  }
  /*
   * Any non-rejected output clears EOP interrupt.
   */
  if ((DRMdev.STATUS & IO_ST_EOP) != 0)
    DRMdev.STATUS &= ~(IO_ST_INT | IO_ST_EOP);
  rebuildPending();

  return IO_REPLY;
}

/*
 * Device clear routine. Clear controller operation from a director
 * function - same as device reset but don't clear CASTATUS and DATASTATUS
 */
void DRMclear(DEVICE *dptr)
{
  DRMdev.STATUS = 0;
  if ((drm_unit.flags & UNIT_ATT) != 0)
    DRMdev.STATUS |= IO_ST_READY | IO_ST_DATA;

  DRMdev.SASTATUS = 0;

  DRMdev.iod_trk =
    DRMdev.iod_sec = 0;

  DRMdev.iod_event = Instructions;
}

/*
 * Address decode routine. If bit 0 of an output  register address is set,
 * clear bits 1 - 7 since they are ignored.
 */
uint8 DRMdecode(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  if (output && ((reg & 0x01) != 0))
    reg &= 0x01;
  return reg;
}

/*
 * Autoload support
 */
t_stat DRMautoload(void)
{
  UNIT *uptr = &drm_unit;

  if ((uptr->flags & UNIT_ATT) != 0) {
    uint32 i;

    for (i = 0; i < DRM_AUTOLOAD; i++) {
      t_offset offset = i * DRM_NUMBY;
      void *buf = &M[i * DRM_NUMBY];

      if (sim_fseeko(uptr->fileref, offset, SEEK_SET) ||
          (sim_fread(buf, sizeof(uint16), DRM_NUMWD, uptr->fileref) != DRM_NUMWD))
        return SCPE_IOERR;
    }
    return SCPE_OK;
  }
  return SCPE_UNATT;
}

t_stat drm_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1752 drum memory controller.\n"
    "1 Hardware Description\n"
    " The 1752-1/2/3/4 consists of a controller and field expandable drum\n"
    " storage from 196608 to 1572864 words. A custom order may provide\n"
    " additional storage up to 3145728 words.\n"
    "2 Equipment Address\n"
    " Drum controllers are typically set to equipment address 2. This address\n"
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
