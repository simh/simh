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
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISe, ARISING FrOM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/
        
/* cdc1700_mt.c: 1732-A/B and 1732-3 magtape device support
 *               Simh devices: mt0, mt1, mt2, mt3
 */

/*
 * Notes:
 *
 * 1. When writing a tape record in direct mode (programmed I/O), the
 * documentation states "Whenever the computer breaks the continuity of the
 * computer word outputs, the controller initiates an End of Record sequence."
 *
 * Since there is no explicit command sequence to initiate an End of Record
 * operation, we need to estimate how many instructions to delay after a
 * data OUT operation before terminating the current record. The tape drives
 * operate at a maximum of 37.5 inches per second, so given the recording
 * density we can compute the maximum output data rate and hence the time
 * delay between 2 characters written to the tape. In addition, since we are
 * emulating a 1732 controller, we need to take into account the character
 * assembly operating mode where each data OUT instruction writes 2 chatacters
 * to the tape. If we assume an average execution time of 1 microsecond per
 * instruction (to keep the arithmetic simple), we get the following table:
 *
 *      Density (bpi)   Data Rate       Instr. Delay    Char Assembly Delay
 *
 *       200             7.50K char/sec 134 Instrs.     268 Instrs.
 *       556            20.85K char/sec  48 Instrs.      96 Instrs.
 *       800            30.00K char/sec  33 Instrs.      66 Instrs.
 *      1600            60.00K char/sec  16 Instrs.      33 Instrs.
 *
 * The emulation does not need to be very strict with regard to timing:
 *
 *      - Using instruction counts is not a very accurate representation of
 *        real time. 1784-2 instruction execution times range from 0.600 to
 *        12.8 uSec so timing can easily be off by almost a factor of 2.
 *        See definition of LOOSETIMING below.
 *
 *      - This does mean that SMM17 timing diagnostics may fail since SMM
 *        uses a timing loop of it's own.
 *
 * The PET diagnostic implies that the interrupt rate is too high for 1600 BPI
 * access to be supported in direct mode.
 *
 * 2. This driver supports 3 modes of access:
 *
 *      1 - Direct mode (programmed I/O)
 *      2 - Buffered mode (DSA or DMA mode)
 *      3 - 1706 buffered data channel access
 *
 * The buffered data channel access is only supported if the controller is
 * configured as a 1732-A (set mt type=1732-A).
 *
 */
#include <string.h>
#include "cdc1700_defs.h"
#include "sim_tape.h"

#define LOOSETIMING(t)  (((t) * 3)/ 2)

#define DEVTYPE_1732_A  IOtype_dev1             /* Controller is 1732-A */
#define DEVTYPE_1732_3  IOtype_dev2             /* Controller is 1732-3 */

#define STATUS2         iod_readR[2]
#define BUFFEREDIO      iod_writeR[3]
#define CURADDRESS      iod_readR[3]

extern char INTprefix[];

extern uint16 LoadFromMem(uint16);
extern t_bool IOStoreToMem(uint16, uint16, t_bool);

extern t_bool doDirectorFunc(DEVICE *, t_bool);
extern void fw_IOcompleteEOP(t_bool, DEVICE *, IO_DEVICE *, uint16, const char *);
extern void fw_IOunderwayEOP(IO_DEVICE *, uint16);
extern void fw_IOintr(t_bool, DEVICE *, IO_DEVICE *, uint16, uint16, uint16, const char *);
extern t_bool fw_reject(IO_DEVICE *, t_bool, uint8);
extern void fw_setForced(IO_DEVICE *, uint16);
extern void fw_clearForced(IO_DEVICE *, uint16);

extern void loadBootstrap(uint16 *, int, uint16, uint16);

extern t_stat checkReset(DEVICE *, uint8);

extern t_stat show_addr(FILE *, UNIT *, int32, CONST void *);

extern t_stat set_protected(UNIT *, int32, CONST char *, void *);
extern t_stat clear_protected(UNIT *, int32, CONST char *, void *);

extern t_stat set_stoponrej(UNIT *, int32, CONST char *, void *);
extern t_stat clr_stoponrej(UNIT *, int32, CONST char *, void *);

extern t_stat set_equipment(UNIT *, int32, CONST char *, void *);

extern void buildDCtables(void);

extern uint16 M[], Areg, IOAreg;
extern t_uint64 Instructions;

extern t_bool IOFWinitialized;

extern UNIT cpu_unit;

t_stat mt_show_transport(FILE *, UNIT *, int32, CONST void *);
t_stat mt_set_9track(UNIT *, int32, CONST char *, void *);
t_stat mt_set_7track(UNIT *, int32, CONST char *, void *);
t_stat mt_show_type(FILE *, UNIT *, int32, CONST void *);
t_stat mt_set_type(UNIT *, int32, CONST char *, void *);

#define DENS    u3

/*
 * Nine-track magnetic tape bootstrap
 */
static uint16 mtbootstrap9[] = {
  0x6819,                       /* 00:  STA*  $19   */
  0x6819,                       /* 01:  STA*  $19   */
  0xE000,                       /* 02:  LDQ+  $382  */
  0x0382,                       /*       0x1382 for 1706 No. 1 */
  0xC813,                       /* 04:  LDA*  $13   */
  0x03FE,                       /* 05:  OUT   $-1   */
  0x0DFE,                       /* 06:  INQ   $-1   */
  0xC811,                       /* 07:  LDA*  $11   */
  0x03FE,                       /* 08:  OUT   $-1   */
  0x0DFE,                       /* 09:  INQ   $-1   */
  0x0203,                       /* 0A:  INP   $03   */
  0x6C0F,                       /* 0B:  STA*  ($0F) */
  0xD80E,                       /* 0C:  RAO*  $0E   */
  0x18FC,                       /* 0D:  JMP*  $FC   */
  0x0D01,                       /* 0E:  INQ   $1    */
  0x0B00,                       /* 0F:  NOP         */
  0x02FE,                       /* 10:  INP   $-1   */
  0x0FCB,                       /* 11:  ALS   $0B   */
  0x0131,                       /* 12:  SAM   $1    */
  0x18F5,                       /* 13:  JMP*  $F5   */
  0xC804,                       /* 14:  LDA*  $04   */
  0x03FE,                       /* 15:  OUT   $-1   */
  0x1C03,                       /* 16:  JMP*  ($03) */
  0x044C,                       /* 17:        DATA  */
  0x0100,                       /* 18:              */
  0x0000,                       /* 19:              */
  0x0000                        /* 1A:              */
};
#define MTBOOTLEN9      (sizeof(mtbootstrap9) / sizeof(uint16))

#if 0
/*
 * Seven-track magnetic tape bootstrap
 */
static uint16 mtbootstrap7[] = {
  0x0500,                       /* 00:  IIN         */
  0x6824,                       /* 01:  STA*  $24   */
  0x6824,                       /* 02:  STA*  $24   */
  0xE000,                       /* 03:  LDQ+  $0382 */
  0x0382,                       /*       0x1382 for 1706 No. 1 */
  0xC81E,                       /* 05:  LDA*  $1E   */
  0x03FE,                       /* 06:  OUT   $-1   */
  0x0DFE,                       /* 07:  INQ   $-1   */
  0xC81C,                       /* 08:  LDA*  $1C   */
  0x03FE,                       /* 09:  OUT   $-1   */
  0x0DFE,                       /* 0A:  INQ   $-1   */
  0x0A00,                       /* 0B:  ENA   $00   */
  0x020D,                       /* 0C:  INP   $0D   */
  0x0FCA,                       /* 0D:  ALS   $0A   */
  0x0821,                       /* 0E:              */
  0x0A00,                       /* 0F:  ENA   $00   */
  0x02FE,                       /* 10:  INP   $-1   */
  0x0FC4,                       /* 11:  ALS   $04   */
  0x0869,                       /* 12:  EAM   M     */
  0x0A00,                       /* 13:  ENA   $00   */
  0x02FE,                       /* 14:  INP   $-1   */
  0x0F42,                       /* 15:  ARS   $02   */
  0x086C,                       /* 16:  EAM   A     */
  0x6C0F,                       /* 17:  STA*  ($0F) */
  0xD80E,                       /* 18:  RAO*  $0E   */
  0x18F1,                       /* 19:  JMP*  $F1   */
  0x0D01,                       /* 1A:              */
  0x0B00,                       /* 1B:              */
  0x02FE,                       /* 1C:              */
  0x0FCB,                       /* 1D:              */
  0x0131,                       /* 1E:              */
  0x18EA,                       /* 1F:              */
  0xC804,                       /* 20:              */
  0x03FE,                       /* 21:              */
  0x1C03,                       /* 22:              */
  0x0414,                       /* 23:              */
  0x0100,                       /* 24:              */
  0x0000,                       /* 25:              */
  0x0000                        /* 26:              */
};
#define MTBOOTLEN7      (sizeof(mtbootstrap7) / sizeof(uint16))
#endif

/*
 * SMM17 bootstraps
 */
static uint16 smm17boot9[] = {
  0x68FE,                       /* xFE0: MTBOOT STA*    *-1             */
  0xE000,                       /* xFE1:        LDQ     =N$WESD         */
  0x0382,                       /* xFE2: EQUIP  $382                    */
  0xC000,                       /* xFE3:        LDA     =N$44C          */
  0x044C,
  0x03FE,                       /* xFE5:        OUT     -1              */
  0x09B3,                       /* xFE6:        INA     -$400-$44C      */
  0x0DFE,                       /* xFE7:        INQ     -1              */
  0x03FE,                       /* xFE8:        OUT     -1              */
  0x0F42,                       /* xFE9:        ARS     2               */
  0x03FE,                       /* xFEA:        OUT     -1              */
  0x0DFE,                       /* xFEB:        INQ     -1              */
  0x02FE,                       /* xFEC: MT1    INP     -1              */
  0x6CF1,                       /* xFED:        STA*    (MTBOOT-1)      */
  0x0102,                       /* xFEE:        SAZ     ENDBT-*-1       */
  0xD8EF,                       /* xFEF:        RAO*    MTBOOT-1        */
  0x18FB,                       /* xFF0:        JMP*    MT1             */
  0x1007                        /* xFF1: ENDBT  JMP-    QL ENTRY        */
};
#define SMM17BOOTLEN9   (sizeof(smm17boot9) / sizeof(uint16))

#if 0
static uint16 smm17boot7[] = {
  0x68FE,                       /* xFE0: MTBOOT STA*    *-1             */
  0xE000,                       /* xFE1:        LDQ     =N$WESD         */
  0x0382,                       /* xFE2: EQUIP  $382                    */
  0xC000,                       /* xFE3:        LDA     =N$405          */
  0x0405,
  0x03FE,                       /* xFE5:        OUT     -1              */
  0x09FB,                       /* xFE6:        INA     -4              */
  0x0DFE,                       /* xFE7:        INQ     -1              */
  0x03FE,                       /* xFE8:        OUT     -1              */
  0x0F42,                       /* xFE9:        ARS     2               */
  0x03FE,                       /* xFEA:        OUT     -1              */
  0x0DFE,                       /* xFEB:        INQ     -1              */
  0x0A00,                       /* xFEC:        ENA     0               */
  0x1807,                       /* xFED:        JMP-    MT2             */
  0x02FE,                       /* xFEE: MT1    INP     -1              */
  0x0F42,                       /* xFEF:        ARS     2               */
  0xBCEE,                       /* xFF0:        EOR*    (MTBOOT-1)      */
  0x010A,                       /* xFF1:        SAZ     ENDBT-*-1       */
  0x7CEC,                       /* xFF2:        SPA*    (MTBOOT-1)      */
  0xD8EB,                       /* xFF3:        RAO*    MTBOOT-1        */
  0x02FE,                       /* xFF4: MT2    INP     -1              */
  0x0FCA,                       /* xFF5:        ALS     10              */
  0x7CE8,                       /* xFF6:        SPA*    (MTBOOT-1)      */
  0x02FE,                       /* xFF7:        INP     -1              */
  0x0FC4,                       /* xFF8:        ALS     4               */
  0xBCE5,                       /* xFF9:        EOR*    (MTBOOT-1)      */
  0x7CE4,                       /* xFFA:        SPA*    (MTBOOT-1)      */
  0x18F2,                       /* xFFB:        JMP*    MT1             */
  0x1007                        /* xFFC:        JMP-    QL ENTRY        */
};
#define SMM17BOOTLEN7   (sizeof(smm17boot7) / sizeof(uint16))
#endif

/*
 * Shared I/O buffer. Note that this is larger than the max possible memory
 * so the only way to handle such large records is to use non-DMA with
 * dynamic processing of the data.
 */
#define MTSIZ           131072
uint8 MTbuf[MTSIZ];
t_mtrlnt MToffset, MTremain;
static enum  { MT_IDLE, MT_READING, MT_WRITING, MT_READTMO, MT_WRITETMO, MT_DSADONE } MTmode;

t_stat mt_svc(UNIT *);
t_stat mt_reset(DEVICE *);
t_stat mt_boot(int32, DEVICE *);
t_stat mt_attach(UNIT *, CONST char *);
t_stat mt_detach(UNIT *);
t_stat mt_vlock(UNIT *, int32 val, CONST char *cptr, void *desc);

void MTstate(const char *, DEVICE *, IO_DEVICE *);
void MTclear(DEVICE *);
t_bool MTreject(IO_DEVICE *, t_bool, uint8);
enum IOstatus MTin(IO_DEVICE *, uint8);
enum IOstatus MTout(IO_DEVICE *, uint8);
enum IOstatus MTBDCin(IO_DEVICE *, uint16 *, uint8);
enum IOstatus MTBDCout(IO_DEVICE *, uint16 *, uint8);

t_stat mt_help(FILE *, DEVICE *, UNIT *, int32, const char *);

/*
        1732-3 Magnetic Tape Controller

   Addresses
                                Computer Instruction
   Q Register         Output From A        Input to A

      00                Write                Read
      01                Control Function     Director Status 1
      10                Unit Select          Director Status 2
      11                Buffered I/O         Current Address

  Operations:

  Control Function

    15              11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X |   |   |   |   | X | X |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                         |           |           |   |   |   |   |
                         +-----------+           |   |   |   |   Clr Controller
                               |                 |   |   |   Clr Interrupts
                               |                 |   |   Data Interrupt Req.
                               |                 |   Interrupt on EOP
                               |                 Interrupt on Alarm
                               |
                               Motion Control:
                                   0001 Write Motion
                                   0010 Read Motion
                                   0011 Backspace
                                   0101 Write File Mark/Tape Mark
                                   0110 Search File Mark/Tape Mark Forward
                                   0111 Search File Mark/Tape Mark Backward
                                   1000 Rewind Load
                                   1100 Rewind Unload (1732-A only)

  Unit Select

    15          12  11  10   9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X |   |   |   |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                 |   |   |   |   |   |   |   |   |   |   |   |   |
                 |   |   |   |   +---+   |   |   |   |   |   |   Character Mode
                 |   |   |   |     |     |   |   |   |   |   BCD mode
                 |   |   |   |     |     |   |   |   |   Binary mode
                 |   |   |   |     |     |   |   |   Select 800 BPI
                 |   |   |   |     |     |   |   Select 556 BPI
                 |   |   |   |     |     |   Select 1600 BPI (1732-A 200 BPI)
                 |   |   |   |     |     Assembly/Disassembly
                 |   |   |   |     Tape Unit (0-3)
                 |   |   |   (1732-A only, additional unit select bit)
                 |   |   Select Tape Unit
                 |   Deselect Tape Unit
                 Select Low Read Threshold (1732-3 only)

  Status Response:

  Director Status 1

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
     |   |   |   |   |   |   |   Parity Error
     |   |   |   |   |   |   End of Tape
     |   |   |   |   |   Beginning of Tape
     |   |   |   |   File Mark
     |   |   |   Controller Active
     |   |   Fill
     |   Storage Parity Error (1732-3 only)
     Protect Fault (1732-3 only)

  Director Status 2 

    15                       9   8   7   6   5   4   3   2   1   0
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
   | X | X | X | X | X | X |   |   |   |   |   |   |   |   |   |   |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             |   |   |   |   |   |   |   |   |   |
                             |   |   |   |   |   |   |   |   |   556 BPI
                             |   |   |   |   |   |   |   |   800 BPI
                             |   |   |   |   |   |   |   1600 BPI (1732-3 only)
                             |   |   |   |   |   |   Seven Track
                             |   |   |   |   |   Write Enable
                             |   |   |   |   PE - Warning
                             |   |   |   PE - Lost Data
                             |   |   PE - transport
                             |   ID - Abort
                             Low Read Threshold

*/

IO_DEVICE MTdev = IODEV(NULL, "Magtape", 1732, 7, 0xFF, 0,
                        MTreject, MTin, MTout, MTBDCin, MTBDCout,
                        MTstate, NULL, NULL, MTclear, NULL, NULL,
                        0x7F, 4,
                        MASK_REGISTER0 | MASK_REGISTER1 | MASK_REGISTER2 | \
                        MASK_REGISTER3,
                        MASK_REGISTER1 | MASK_REGISTER2 | MASK_REGISTER3,
                        0, 0, 0, 1, NULL);

/*
 * Define usage for "private" IO_DEVICE data areas.
 */
#define iod_mode        iod_private             /* operating mode */
#define iod_delay       iod_private3            /* current delay reason */
#define iod_wasWriting  iod_private4            /* writing was in progress */
#define iod_reason      iod_private5            /* reason for EOP */
#define iod_CWA         iod_readR[3]            /* current DSA address */
#define iod_LWA         iod_private6            /* last word address */
#define iod_DSApending  iod_private10           /* DSA request pending */
#define iod_FWA         iod_private11           /* first word address */

/*
 * Define delay functions other than the standard motion commands. The low
 * 7 bits are available, zero is reserved to mean no pending delay.
 */
#define IO_DELAY_RDATA  0x01                    /* Delay IO_ST_DATA for read*/
#define IO_DELAY_WDATA  0x02                    /*    and write */
#define IO_DELAY_RTMO   0x03                    /* Read record timeout */
#define IO_DELAY_WTMO   0x04                    /* Write record timeout */
#define IO_DELAY_EOP    0x05                    /* EOP delay */
#define IO_DSA_READ     0x06                    /* DSA Read operation */
#define IO_DSA_WRITE    0x07                    /* DSA Write operation  */
#define IO_LOCAL_MASK   0x7F

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT units
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

#define MT_NUMDR        4                       /* # drives */

UNIT mt_unit[] = {
  { UDATA(&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
  { UDATA(&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
  { UDATA(&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
  { UDATA(&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+UNIT_7TRACK, 0) },
};

REG mt_reg_1732_A[] = {
  { HRDATAD(FUNCTION, MTdev.FUNCTION, 16, "Last director status issued") },
  { HRDATAD(STATUS, MTdev.STATUS, 16, "Director status register") },
  { HRDATAD(UNITSEL, MTdev.iod_writeR[2], 16, "Last Unit Select issued") },
  { HRDATAD(STATUS2, MTdev.STATUS2, 16, "Transport status register") },
  { HRDATAD(IENABLE, MTdev.IENABLE, 16, "Interrupts enabled") },
  { NULL }
};

REG mt_reg_1732_3[] = {
  { HRDATAD(FUNCTION, MTdev.FUNCTION, 16, "Last director status issued") },
  { HRDATAD(STATUS, MTdev.STATUS, 16, "Director status register") },
  { HRDATAD(UNITSEL, MTdev.iod_writeR[2], 16, "Last Unit Select issued") },
  { HRDATAD(STATUS2, MTdev.STATUS2, 16, "Transport status register") },
  { HRDATAD(IENABLE, MTdev.IENABLE, 16, "Interrupts enabled") },
  { HRDATAD(BUFFEREDIO, MTdev.BUFFEREDIO, 16, "Last Buffered I/O issued") },
  { HRDATAD(CURADDRESS, MTdev.CURADDRESS, 16, "Current DSA address") },
  { HRDATAD(LASTADDRESS, MTdev.iod_LWA, 16, "Last DSA address") },
  { NULL }
};

MTAB mt_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "TYPE", "TYPE={1732-A|1732-3}",
    &mt_set_type, &mt_show_type, NULL, "Set/Display magtape controller type" },
  { MTAB_XTD|MTAB_VDV, 0, "EQUIPMENT", "EQUIPMENT=hexAddress",
    &set_equipment, &show_addr, NULL, "Set/Display equipment address" },
  { MTUF_WLK, 0, "write enabled", "WRITEENABLED",
    &mt_vlock, NULL, NULL, "Mark transport as write enabled" },
  { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED",
    &mt_vlock, NULL, NULL, "Mark transport as writed locked" },
  { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
    &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, "Define tape format" },
  { MTAB_XTD|MTAB_VUN, 0, "CAPACITY", "CAPACITY",
    &sim_tape_set_capac, &sim_tape_show_capac, NULL, "Specify tape capacity" },
  { MTAB_XTD|MTAB_VUN, 0, "TRANSPORT", NULL,
    NULL, &mt_show_transport, NULL, "Display type of tape transport" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "9TRACK",
    &mt_set_9track, NULL, NULL, "Set drive as 9-track transport" },
  { MTAB_XTD|MTAB_VUN, 0, NULL, "7TRACK",
    &mt_set_7track, NULL, NULL, "Set drive as 7-track transport" },
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

/*
 * MT debug flags
 */
#define DBG_V_OPS       (DBG_SPECIFIC+0)/* Trace operations */
#define DBG_V_READ      (DBG_SPECIFIC+1)/* Dump read records */
#define DBG_V_RDATA     (DBG_SPECIFIC+2)/* Read through reg. 0 */
#define DBG_V_WDATA     (DBG_SPECIFIC+3)/* Write through reg. 0 */
#define DBG_V_MTIO      (DBG_SPECIFIC+4)/* Trace library routine calls */
#define DBG_V_DENS      (DBG_SPECIFIC+5)/* Trace density select changes */
#define DBG_V_SELECT    (DBG_SPECIFIC+6)/* Trace drive select/de-select */
#define DBG_V_RDSA      (DBG_SPECIFIC+7)/* Read data after DSA transfer */
#define DBG_V_WDSA      (DBG_SPECIFIC+8)/* Write data before DSA transfer */

#define DBG_OPS         (1 << DBG_V_OPS)
#define DBG_READ        (1 << DBG_V_READ)
#define DBG_RDATA       (1 << DBG_V_RDATA)
#define DBG_WDATA       (1 << DBG_V_WDATA)
#define DBG_MTIO        (1 << DBG_V_MTIO)
#define DBG_DENS        (1 << DBG_V_DENS)
#define DBG_SELECT      (1 << DBG_V_SELECT)
#define DBG_RDSA        (1 << DBG_V_RDSA)
#define DBG_WDSA        (1 << DBG_V_WDSA)

DEBTAB mt_deb[] = {
  { "TRACE",       DBG_DTRACE,     "Trace device I/O requests" },
  { "STATE",       DBG_DSTATE,     "Display device state changes" },
  { "INTR",        DBG_DINTR,      "Display device interrupt requests" },
  { "LOCATION",    DBG_DLOC,       "Display address of I/O instructions" },
  { "FIRSTREJ",    DBG_DFIRSTREJ,  "Suppress display of 2nd ... I/O rejects" },
  { "OPS",         DBG_OPS,        "Trace tape transport operations" },
  { "READ",        DBG_READ,       "Dump read records" },
  { "RDATA",       DBG_RDATA,      "Dump programmed I/O read data" },
  { "WDATA",       DBG_WDATA,      "Dump programmed I/O write data" },
  { "MTIO",        DBG_MTIO,       "Trace tape library routine calls" },
  { "DENS",        DBG_DENS,       "Trace denisty select changes" },
  { "SELECT",      DBG_SELECT,     "Trace transport select/de-select" },
  { "RDSA",        DBG_RDSA,       "Dump buffer after DSA read" },
  { "WDSA",        DBG_WDSA,       "Dump buffer before DSA write" },
  { NULL }
};

DEVICE mt_dev = {
  "MT", mt_unit, NULL, mt_mod,
  MT_NUMDR, 10, 31, 1, 8, 8,
  NULL, NULL, &mt_reset,
  &mt_boot, &mt_attach, &mt_detach,
  &MTdev,
  DEV_DEBUG | DEV_TAPE | DEV_DISABLE | DEV_INDEV | DEV_OUTDEV | DEV_PROTECT,
  0, mt_deb,
  NULL, NULL, &mt_help, NULL, NULL, NULL
};

/* MT trace routine */

void mt_trace(UNIT *uptr, const char *what, t_stat st, t_bool xfer)
{
  int32 u = uptr - mt_dev.units;
  const char *status = NULL;

  switch (st) {
    case MTSE_OK:
      status = "OK";
      break;

    case MTSE_TMK:
      status = "Tape Mark";
      break;

    case MTSE_UNATT:
      status = "Unattached";
      break;

    case MTSE_IOERR:
      status = "IO Error";
      break;

    case MTSE_INVRL:
      status = "Invalid Record Length";
      break;

    case MTSE_FMT:
      status = "Invalid Format";
      break;

    case MTSE_BOT:
      status = "Beginning Of Tape";
      break;

    case MTSE_EOM:
      status = "End Of Medium";
      break;

    case MTSE_RECE:
      status = "Error In Record";
      break;

    case MTSE_WRP:
      status = "Write Protected";
      break;

    case MTSE_LEOT:
      status = "Logical end of tape";
      break;

    case MTSE_RUNAWAY:
      status = "Tape runaway";
      break;
  }

  if (status != NULL) {
    if (xfer)
      fprintf(DBGOUT, "MT%d: %s, bytes %d - %s\r\n", u, what, MTremain, status)\
        ;
    else fprintf(DBGOUT, "MT%d: %s - %s\r\n", u, what, status);
  } else fprintf(DBGOUT, "MT%d: %s\r\n", u, what);
  if ((mt_dev.dctrl & DBG_DLOC) != 0)
    fprintf(DBGOUT, "MT%d: Inst: %llu\r\n", u, Instructions);
}

/* MT trace routine (DSA mode) */

void mt_DSAtrace(UNIT *uptr, const char *what)
{
  int32 u = uptr - mt_dev.units;

  fprintf(DBGOUT, "MT%d: DSA %s - CWA: 0x%04X, LWA: 0x%04X\r\n",
          u, what, MTdev.iod_CWA, MTdev.iod_LWA);
}

/* Tape library routine trace */

void mtio_trace(UNIT *uptr, const char *what, t_stat st, t_bool lvalid, t_mtrlnt len)
{
  int32 u = uptr - mt_dev.units;
  t_bool bot = FALSE, eot = FALSE;
  const char *status = "Unknown";

  if (st != MTSE_UNATT) {
    bot = sim_tape_bot(uptr);
    eot = sim_tape_eot(uptr);
  }

  switch (st) {
    case MTSE_OK:
      status = "OK";
      break;

    case MTSE_TMK:
      status = "Tape mark";
      break;

    case MTSE_UNATT:
      status = "Unattached";
      break;

    case MTSE_IOERR:
      status = "IO error";
      break;

    case MTSE_INVRL:
      status = "Invalid record length";
      break;

    case MTSE_FMT:
      status = "Invalid format";
      break;

    case MTSE_BOT:
      status = "Beginning of tape";
      break;

    case MTSE_EOM:
      status = "End of medium";
      break;

    case MTSE_RECE:
      status = "Error in record";
      break;

    case MTSE_WRP:
      status = "Write protected";
      break;

    case MTSE_LEOT:
      status = "Logical end of tape";
      break;

    case MTSE_RUNAWAY:
      status = "Tape runaway";
      break;
  }
  fprintf(DBGOUT, "MT%d: MTIO [%s %s] %s - %s\r\n",
          u, bot ? "BOT" : "", eot ? "EOT" : "", what, status);
  if (lvalid)
    fprintf(DBGOUT,
            "MT%d: MTIO Record len: %u, Mode: 0x%04X\r\n",
            u, len, MTdev.iod_mode);
}


/* Dump MT buffer */

char chars[128] = {
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', '!', '"', '#', '$', '%', '&', '\'',
  '(', ')', '*', '+', ',', '-', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', ':', ';', '<', '=', '>', '?',
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
  'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
  'X', 'Y', 'Z', '[', '\\', '|', '^', '_',
  ' ', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
  'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w',
  'x', 'y', 'z', '{', ' ', '}', '~', ' '
};

void mt_dump(void)
{
  t_mtrlnt offset = 0, count = MTremain;
  char msg[80], text[16];

  if (MTremain > 0) {
    fprintf(DBGOUT, "Dump of MTbuf:\r\n");

    while (count > 0) {
      t_mtrlnt remain = count >= 10 ? 10 : count;
      t_mtrlnt i;

      msg[0] = '\0';

      for (i = 0; i < remain; i++) {
        char temp[8];

        text[i] = chars[MTbuf[offset] & 0x7F];

        sprintf(temp, "0x%02x", MTbuf[offset++]);
        if (msg[0] != '\0')
          strcat(msg, " ");
        strcat(msg, temp);
      }
      text[remain] = '\0';

      fprintf(DBGOUT, "%-55s%s\r\n", msg, text);

      count -= remain;
    }
  }
}

void mt_DSAdump(uint16 lwa, t_bool rw)
{
  uint16 cwa = MTdev.iod_FWA;
  int idx;
  char msg[80], text[16], temp[8];

  fprintf(DBGOUT, "Dump of DSA %s buffer (FWA: %04X, LWA: %04X):\r\n",
          rw ? "write" : "read", cwa, lwa);

  msg[0] = '\0';
  idx = 0;

  while (cwa != lwa) {
    text[idx++] = chars[(M[cwa] >> 8) & 0x7F];
    text[idx++] = chars[M[cwa] & 0x7F];

    sprintf(temp, "0x%04X", M[cwa]);
    if (msg[0] != '\0')
      strcat(msg, " ");
    strcat(msg, temp);

    if (idx == 10) {
      text[idx++] = '\0';
      fprintf(DBGOUT, "%-55s%s\r\n", msg, text);
      msg[0] = '\0';
      idx = 0;
    }
    cwa++;
  }

  if (idx != 0) {
    text[idx++] = '\0';
    fprintf(DBGOUT, "%-55s%s\r\n", msg, text);
  }
}

/*
 * Dump the current internal state of the MT device.
 */
const char *MTstateStr[] = {
  "Idle", "Reading", "Writing", "Read Timeout", "Write Timeout", "DSA Done"
};

void MTstate(const char *where, DEVICE *dev, IO_DEVICE *iod)
{
  char device[16];

  strcpy(device, "None");
  if (iod->iod_unit != NULL) {
    int32 u = iod->iod_unit - dev->units;

    sprintf(device, "MT%u", u);
  }

  fprintf(DBGOUT,
          "%s[%s %s: Func: %04X, Sta: %04X, Sta2: %04X, Ena: %04X]\r\n",
          INTprefix, dev->name, where,
          iod->FUNCTION, iod->STATUS, iod->STATUS2, iod->IENABLE);
  fprintf(DBGOUT,
          "%s[%s %s: Sel: %s, %s%s]\r\n",
          INTprefix, dev->name, where, device, MTstateStr[MTmode],
          iod->iod_wasWriting ? ", Was writing" : "");
}

void mt_data(UNIT *uptr, t_bool output, uint16 data)
{
  int32 u = uptr - mt_dev.units;

  fprintf(DBGOUT, "MT%d: %s - 0x%04x\r\n", u, output ? "wrote" : "read", data);
}

t_stat mt_show_type(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  switch (MTdev.iod_type) {
    case DEVTYPE_1732_A:
      fprintf(st, "1732-A Magnetic Tape Controller");
      break;

    case DEVTYPE_1732_3:
      fprintf(st, "1732-3 Magnetic Tape Controller");
      break;

    default:
      return SCPE_IERR;
  }
  return SCPE_OK;
}

t_stat mt_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (!cptr)
    return SCPE_IERR;
  if ((uptr->flags & UNIT_ATT) != 0)
    return SCPE_ALATT;

  if (!strcmp(cptr, "1732-A")) {
    MTdev.iod_type = DEVTYPE_1732_A;
    MTdev.iod_model = "1732-A";
    MTdev.iod_flags &= ~AQ_ONLY;
    mt_dev.registers = mt_reg_1732_A;
    buildDCtables();
  } else {
    if (!strcmp(cptr, "1732-3")) {
      MTdev.iod_type = DEVTYPE_1732_3;
      MTdev.iod_model = "1732-3";
      MTdev.iod_flags |= AQ_ONLY;
      mt_dev.registers = mt_reg_1732_3;
      buildDCtables();
    } else return SCPE_ARG;
  }
  return SCPE_OK;
}

/*
 * Display magtape transport
 */
t_stat mt_show_transport(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if (MTdev.iod_type == DEVTYPE_1732_A) {
    if ((uptr->flags & UNIT_7TRACK) != 0)
      fprintf(st, "7-track 608 transport");
    else fprintf(st, "9-track 609 transport");
  } else {
    if ((uptr->flags & UNIT_7TRACK) != 0)
      fprintf(st, "7-track 6173 transport");
    else fprintf(st, "9-track 6193 transport");
  }
  return SCPE_OK;
}

/*
 * Set drive to 9-track transport.
 */
t_stat mt_set_9track(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((uptr->flags & UNIT_ATT) != 0)
    return SCPE_ALATT;

  uptr->flags &= ~UNIT_7TRACK;
  return SCPE_OK;
}

/*
 * Set drive to 7-track transport.
 */
t_stat mt_set_7track(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (uptr == NULL)
    return SCPE_IERR;

  if ((uptr->flags & UNIT_ATT) != 0)
    return SCPE_ALATT;

  uptr->flags |= UNIT_7TRACK;
  return SCPE_OK;
}

/*
 * Compute the delay time between new data being available from tape. This
 * will be dependent on the density of the tape and the speed of the drive
 * (in this case we assume 37.5 inches per sec).
 */
int32 mt_densityTimeout(t_bool loose)
{
  int32 result = MT_200_WAIT;

  switch (MTdev.STATUS2 & (IO_ST2_556 | IO_ST2_800)) {
    case 0:
      result = MT_200_WAIT;
      break;

    case IO_ST2_556:
      result = MT_556_WAIT;
      break;

    case IO_ST2_800:
      result = MT_800_WAIT;
      break;
  }

  if (MTdev.iod_type == DEVTYPE_1732_3)
    if ((MTdev.STATUS2 & IO_ST2_1600) != 0)
      result = MT_1600_WAIT;

  if ((MTdev.iod_mode & IO_1732_ASSEM) != 0)
    result *= 2;

  return loose ? LOOSETIMING(result) : result;
}

/* Unit service */

t_stat mt_svc(UNIT *uptr)
{
  uint16 mask = IO_1732_STMSK;
  uint16 delay = MTdev.iod_delay;
  uint16 result;
  t_stat status;
  t_mtrlnt temp;
  int32 tmo;

  if ((mt_dev.dctrl & DBG_OPS) != 0)
    mt_trace(uptr, "mt_svc", (t_stat)-1, FALSE);

  MTdev.iod_delay = 0;

  /*
   * Check for local (internal) delays.
   */
  if ((delay & IO_LOCAL_MASK) != 0) {
    switch (delay) {
      case IO_DELAY_RDATA:
        fw_IOintr(FALSE, &mt_dev, &MTdev, IO_ST_DATA, 0, 0xFFFF, "Read Ready");
        tmo = mt_densityTimeout(TRUE);
        MTdev.iod_event = Instructions + tmo;

        MTdev.iod_delay = IO_DELAY_RTMO;
        sim_activate(uptr, tmo);

        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "Delayed IO_ST_DATA for read", (t_stat)-1, FALSE);
        break;

      case IO_DELAY_WDATA:
        fw_IOintr(FALSE, &mt_dev, &MTdev, IO_ST_DATA, 0, 0xFFFF, "Write Ready");
        tmo = mt_densityTimeout(TRUE);
        MTdev.iod_event = Instructions + tmo;

        MTdev.iod_delay = IO_DELAY_WTMO;
        sim_activate(uptr, tmo);

        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "Delayed IO_ST_DATA for write", (t_stat)-1, FALSE);
        break;

      case IO_DELAY_RTMO:
        MTmode = MT_READTMO;

        /*
         * Drop DATA and schedule EOP completion
         */
        MTdev.STATUS &= ~IO_ST_DATA;
        MTdev.iod_delay = IO_DELAY_EOP;

        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "Read buffer timed out", (t_stat)-1, FALSE);

        if (MTremain != 0) {
          MTdev.STATUS |= IO_ST_ALARM | IO_ST_LOST;
          MTdev.iod_reason = "Read timed out - data lost";
          MTdev.iod_oldienable = MTdev.iod_ienable;
          MTdev.iod_ienable &= ~IO_DIR_DATA;
        } else MTdev.iod_reason = "Read timed out";

        MTremain = 0;
        sim_activate(uptr, MT_EOP_WAIT);
        break;

      case IO_DELAY_WTMO:
        MTmode = MT_WRITETMO;
        status = sim_tape_wrrecf(uptr, MTbuf, MToffset);

        if ((mt_dev.dctrl & DBG_MTIO) != 0)
          mtio_trace(uptr, "wrrecf", status, TRUE, MToffset);

        /*
         * Drop DATA and schedule EOP completion
         */
        MTdev.STATUS &= ~IO_ST_DATA;
        MTdev.iod_delay = IO_DELAY_EOP;
        MTdev.iod_reason = "Write timed out";
        sim_activate(uptr, MT_EOP_WAIT);

        if ((mt_dev.dctrl & DBG_OPS) != 0) {
          MTremain = MToffset;
          mt_trace(uptr, "Write buffer timed out", status, TRUE);
        }
        break;

      case IO_DELAY_EOP:
        MTmode = MT_IDLE;
        fw_IOcompleteEOP(FALSE, &mt_dev, &MTdev, ~IO_1732_ACTIVE, MTdev.iod_reason);
        MTdev.iod_reason = NULL;

        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "Delayed EOP", (t_stat)-1, FALSE);
        break;

      case IO_DSA_READ:
        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_DSAtrace(uptr, "read");

        if (MTdev.iod_CWA == MTdev.iod_LWA) {
          /*
           * DSA read transfer complete.
           */
          MTmode = MT_DSADONE;
          MTdev.iod_delay = IO_DELAY_EOP;
          MTdev.iod_reason = "DSA read complete";
          sim_activate(uptr, MT_EOP_WAIT);

          if ((mt_dev.dctrl & DBG_OPS) != 0)
            mt_trace(uptr, "DSA read complete", (t_stat)-1, FALSE);
          if ((mt_dev.dctrl & DBG_RDSA) != 0)
            mt_DSAdump(MTdev.iod_LWA, FALSE);
          break;
        }

        /*
         * If there is no data available, terminate the transfer
         */
        if (MTremain == 0) {
          /*
           * DSA read complete - no more data.
           */
          MTmode = MT_DSADONE;
          MTdev.iod_delay = IO_DELAY_EOP;
          MTdev.iod_reason = "DSA read complete - no data";
          sim_activate(uptr, MT_EOP_WAIT);

          if ((mt_dev.dctrl & DBG_OPS) != 0)
            mt_trace(uptr, "DSA read complete - no data", (t_stat)-1, FALSE);
          if ((mt_dev.dctrl & DBG_RDSA) != 0)
            mt_DSAdump(MTdev.iod_CWA, FALSE);
          break;
        }

        if ((MTdev.iod_mode & IO_1732_ASSEM) != 0) {
          if (MTremain >= 2) {
            result = (MTbuf[MToffset] << 8) | MTbuf[MToffset + 1];
            MToffset += 2;
            MTremain -= 2;
          } else {
            MTdev.STATUS |= IO_1732_FILL;
            result = MTbuf[MToffset] << 8;
            MToffset++;
            MTremain--;
          }
        } else {
          result = MTbuf[MToffset];
          MToffset++;
          MTremain--;
        }

        if ((uptr->flags & UNIT_7TRACK) != 0)
          result &= 0x3F3F;

        if (!IOStoreToMem(MTdev.iod_CWA, result, TRUE)) {
          /*** TODO: generate device protect error ***/
        }
        MTdev.iod_CWA++;
        MTdev.iod_delay = IO_DSA_READ;
        sim_activate(uptr, mt_densityTimeout(FALSE));
        break;

      case IO_DSA_WRITE:
        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_DSAtrace(uptr, "write");

        if (MTdev.iod_CWA == MTdev.iod_LWA) {
          /*
           * DSA write transfer complete.
           */
          status = sim_tape_wrrecf(uptr, MTbuf, MToffset);

          if ((mt_dev.dctrl & DBG_MTIO) != 0)
            mtio_trace(uptr, "wrrecf", status, TRUE, MToffset);

          MTmode = MT_DSADONE;
          MTdev.iod_delay = IO_DELAY_EOP;
          MTdev.iod_reason = "DSA write complete";
          sim_activate(uptr, MT_EOP_WAIT);

          if ((mt_dev.dctrl & DBG_OPS) != 0)
            mt_trace(uptr, "DSA write complete", (t_stat)-1, FALSE);
          break;
        }

        result = LoadFromMem(MTdev.iod_CWA);

        if ((uptr->flags & UNIT_7TRACK) != 0)
          result &= 0x3F3F;

        MTdev.iod_CWA++;

        if ((MTdev.iod_mode & IO_1732_ASSEM) != 0) {
          MTbuf[MToffset] = (result >> 8) & 0xFF;
          MTbuf[MToffset + 1] = result & 0xFF;
          MToffset += 2;
        } else {
          MTbuf[MToffset] = result & 0xFF;
          MToffset += 1;
        }

        MTdev.iod_delay = IO_DSA_WRITE;
        sim_activate(uptr, mt_densityTimeout(FALSE));
        break;
    }
    return SCPE_OK;
  }

  /*
   * Check if we need to write a tape mark before processing the request.
   */
  if (MTdev.iod_wasWriting)
    switch (delay) {
      case IO_1732_BACKSP:
      case IO_1732_REWL:
      case IO_1732A_REWU:
        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "Forced TM (BACKSP, REWL, REWU)", (t_stat)-1, FALSE);
        status = sim_tape_wrtmk(uptr);

        if ((mt_dev.dctrl & DBG_MTIO) != 0)
          mtio_trace(uptr, "wrtmk", status, FALSE, 0);
        break;
    }

  /*
   * Command specific processing
   */
  switch (delay) {
    /*
     * The following commands normally do not set "end of operation". "read
     * motion" does set "end of operation" if a tape mark or end of tape
     * is detected.
     */
    case IO_1732_READ:
      MTremain = 0;
      status = sim_tape_rdrecf(uptr, MTbuf, &MTremain, MTSIZ);

      if ((mt_dev.dctrl & DBG_MTIO) != 0)
        mtio_trace(uptr, "rdrecf", status, TRUE, MTremain);

      switch (status) {
        case MTSE_OK:
          break;

        case MTSE_TMK:
          MTdev.STATUS |= IO_ST_ALARM | IO_1732_FMARK;
          break;

        case MTSE_EOM:
          MTdev.STATUS |= IO_ST_ALARM | IO_1732_EOT;
          break;

        case MTSE_RECE:
         MTdev.STATUS |= IO_ST_ALARM | IO_ST_PARITY;
         MTremain = 0;
         break;
      }
      MToffset = 0;

      if ((MTdev.STATUS & (IO_1732_FMARK | IO_1732_EOT | IO_ST_PARITY)) == 0)
        mask &= ~IO_ST_EOP;

      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "READ", status, TRUE);
      if ((mt_dev.dctrl & DBG_READ) != 0)
        mt_dump();

      if (MTremain > 0) {
        if (MTdev.iod_DSApending) {
          MTdev.iod_DSApending = FALSE;
          MTdev.iod_delay = IO_DSA_READ;
          sim_activate(uptr, mt_densityTimeout(FALSE));
          if ((mt_dev.dctrl & DBG_OPS) != 0) {
            int32 u = uptr - mt_dev.units;
            
            fprintf(DBGOUT,
                    "[MT%d: DSA Read started, CWA: 0x%04X, LWA: 0x%04X, Mode: 0x%X\r\n",
                    u, MTdev.iod_CWA, MTdev.iod_LWA, MTdev.iod_mode);
          }
          return SCPE_OK;
        }
        MTdev.iod_delay = IO_DELAY_RDATA;
        sim_activate(uptr, MT_MIN_WAIT);
        return SCPE_OK;
      }
      MTmode = MT_IDLE;
      break;

    case IO_1732_WRITE:
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "WRITE", (t_stat)-1, FALSE);

      if (MTdev.iod_DSApending) {
        MTdev.iod_DSApending = FALSE;
        MTdev.iod_delay = IO_DSA_WRITE;

        if ((mt_dev.dctrl & DBG_WDSA) != 0)
          mt_DSAdump(MTdev.iod_LWA, TRUE);

        sim_activate(uptr, mt_densityTimeout(FALSE));
        if ((mt_dev.dctrl & DBG_OPS) != 0) {
          int32 u = uptr - mt_dev.units;

          fprintf(DBGOUT,
                  "[MT%d: DSA Write started, CWA: 0x%04X, LWA: 0x%04X, Mode: 0x%X\r\n",
                  u, MTdev.iod_CWA, MTdev.iod_LWA, MTdev.iod_mode);
        }
        return SCPE_OK;
      }
      MTdev.iod_delay = IO_DELAY_WDATA;
      sim_activate(uptr, MT_MIN_WAIT);
      return SCPE_OK;

    case IO_1732A_REWU:
      status = sim_tape_rewind(uptr);

      if ((mt_dev.dctrl & DBG_MTIO) != 0)
        mtio_trace(uptr, "rewind & unload", status, FALSE, 0);

      MTdev.STATUS |= IO_1732_BOT;
      mt_detach(uptr);
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "REWU", status, FALSE);
      
      mask &= ~IO_ST_EOP;
      break;

    /*
     * The following commands set "end of operation" when the command
     * completes.
     */
    case IO_1732_BACKSP:
      status = sim_tape_sprecr(uptr, &temp);

      if ((mt_dev.dctrl & DBG_MTIO) != 0)
        mtio_trace(uptr, "sprecr", status, FALSE, 0);

      if (status == MTSE_TMK)
        MTdev.STATUS |= IO_1732_FMARK;
      if (sim_tape_bot(uptr))
        MTdev.STATUS |= IO_1732_BOT;
      if (sim_tape_eot(uptr))
        MTdev.STATUS |= IO_1732_EOT;
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "BACKSP", status, FALSE);
      break;

    case IO_1732_WFM:
      status = sim_tape_wrtmk(uptr);

      if ((mt_dev.dctrl & DBG_MTIO) != 0)
        mtio_trace(uptr, "wrtmk", status, FALSE, 0);

#if 0
      MTdev.STATUS |= IO_ST_ALARM | IO_1732_FMARK;
#endif
      if (sim_tape_eot(uptr))
        MTdev.STATUS |= IO_1732_EOT;
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "WFM", status, FALSE);
      break;

    case IO_1732_SFWD:
      status = MTSE_OK;
      while (!sim_tape_eot(uptr)) {
        status = sim_tape_sprecf(uptr, &temp);
        
        if ((mt_dev.dctrl & DBG_MTIO) != 0)
          mtio_trace(uptr, "sprecf", status, FALSE, 0);
        
        if (status == MTSE_TMK)
          MTdev.STATUS |= IO_1732_FMARK;

        if (status != MTSE_OK)
          break;
      }
      if (sim_tape_bot(uptr))
        MTdev.STATUS |= IO_1732_BOT;
      if (sim_tape_eot(uptr))
        MTdev.STATUS |= IO_1732_EOT;
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "SFWD", status, FALSE);
      break;

     case IO_1732_SBACK:
       status = MTSE_OK;
       while (!sim_tape_bot(uptr)) {
         status = sim_tape_sprecr(uptr, &temp);

         if ((mt_dev.dctrl & DBG_MTIO) != 0)
           mtio_trace(uptr, "sprecr", status, FALSE, 0);

         if (status == MTSE_TMK)
           MTdev.STATUS |= IO_1732_FMARK;

         if (status != MTSE_OK)
           break;
       }
       if (sim_tape_bot(uptr))
         MTdev.STATUS |= IO_1732_BOT;
       if (sim_tape_eot(uptr))
         MTdev.STATUS |= IO_1732_EOT;
       if ((mt_dev.dctrl & DBG_OPS) != 0)
         mt_trace(uptr, "SBACK", status, FALSE);
       break;

    case IO_1732_REWL:
      status = sim_tape_rewind(uptr);

      if ((mt_dev.dctrl & DBG_MTIO) != 0)
        mtio_trace(uptr, "rewind", status, FALSE, 0);
      
      MTdev.STATUS |= IO_1732_BOT;
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_trace(uptr, "REWL", status, FALSE);
      break;
  }

  /*
   * If we are at a tape mark or end of tape, no data is available.
   */
  if ((MTdev.STATUS & (IO_1732_FMARK | IO_1732_EOT)) != 0)
    mask &= ~IO_ST_DATA;

  /*
   * Controller is no longer active.
   */
  mask &= ~IO_1732_ACTIVE;

  /*
   * I/O is now complete.
   */
  fw_IOcompleteEOP(FALSE, &mt_dev, &MTdev, mask, "Operation Complete");
  return SCPE_OK;
}

/* Reset routine */

t_stat mt_reset(DEVICE *dptr)
{
  t_stat r;

  if (MTdev.iod_type == IOtype_default) {
    /*
     * Setup the default device type.
     */
    MTdev.iod_type = DEVTYPE_1732_A;
    MTdev.iod_model = "1732-A";
    MTdev.iod_flags &= ~AQ_ONLY;
    mt_dev.registers = mt_reg_1732_A;
    buildDCtables();
  }

  if (IOFWinitialized)
    if ((dptr->flags & DEV_DIS) == 0)
      if ((r = checkReset(dptr, MTdev.iod_equip)) != SCPE_OK)
        return r;

  DEVRESET(&MTdev);

  MTdev.STATUS = 0;
  MTdev.STATUS2 = 0;

  MTdev.iod_mode = 0;
  MTdev.iod_unit = NULL;
  MTdev.iod_delay = 0;
  MTdev.iod_wasWriting = FALSE;
  MTdev.iod_CWA = MTdev.iod_LWA = 0;
  MTdev.iod_DSApending = FALSE;
  MTmode = MT_IDLE;

  return SCPE_OK;
}

/* Boot routine */

t_stat mt_boot(int32 unitno, DEVICE *dptr)
{
  if (unitno != 0) {
    sim_printf("Can only boot from drive 0\n");
    return SCPE_ARG;
  }

  if ((sim_switches & SWMASK('S')) != 0) {
    /*
     * Special bootstrap for System Maintenance Monitor (SMM17)
     */
    uint16 base, equip;

    base = ((cpu_unit.capac - 1) & 0xF000) | 0xFE0;
    loadBootstrap(smm17boot9, SMM17BOOTLEN9, base, base);

    /*
     * Compute the equipment address to use and patch it into memory.
     */
    equip = (MTdev.iod_equip << 7) | 2;
    if ((sim_switches & SWMASK('D')) != 0)
      equip |= 0x1000;

    M[base + 2] = equip;

    return SCPE_OK;
  }

  loadBootstrap(mtbootstrap9, MTBOOTLEN9, 0, 0);

  /*
   * Set A register according to the amount of memory installed.
   */
  Areg = 0x5000;
  if (cpu_unit.capac < 32768)
    Areg = 0x4000;
  if (cpu_unit.capac < 24576)
    Areg = 0x2000;

  return SCPE_OK;
}

/* Attach routine */

t_stat mt_attach(UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  r = sim_tape_attach(uptr, cptr);
  if (r != SCPE_OK)
    return r;

  uptr->flags &= ~UNIT_WPROT;
  if (sim_switches & SWMASK('R'))
    uptr->flags |= UNIT_WPROT;

  uptr->DENS = IO_ST2_800;

  /*
   * If this units is currently selected, make it accessible.
   */
  if (MTdev.iod_unit == uptr) {
    MTdev.STATUS2 = uptr->DENS & (IO_ST2_556 | IO_ST2_800);
    if ((uptr->flags & UNIT_WPROT) != 0)
      MTdev.STATUS2 &= ~IO_ST2_WENABLE;
    else MTdev.STATUS2 |= IO_ST2_WENABLE;
    if ((uptr->flags & UNIT_7TRACK) != 0)
      MTdev.STATUS2 |= IO_ST2_7TRACK;
    else MTdev.STATUS2 &= ~IO_ST2_7TRACK;
    fw_setForced(&MTdev, IO_ST_READY);
  }
  return r;
}

/* Detach routine */

t_stat mt_detach(UNIT *uptr)
{
  t_stat st;

  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;

  sim_cancel(uptr);

  st = sim_tape_detach(uptr);
  if (st == MTSE_OK) {
    if (MTdev.iod_unit == uptr)
      fw_clearForced(&MTdev, IO_ST_READY);
  }
  return st;
}

/* Write lock/enable routine */

t_stat mt_vlock(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (((uptr->flags & UNIT_ATT) != 0) && (val || sim_tape_wrp(uptr)))
    uptr->flags |= UNIT_WPROT;
  else uptr->flags &= ~UNIT_WPROT;

  return SCPE_OK;
}

/*
 * Perform a "Clear Controller" operation. Basically this is similar to a
 * device reset except it does not forget the currently selected transport.
 */
void MTclear(DEVICE *dptr)
{
  UNIT *uptr;

  DEVRESET(&MTdev);

  MTdev.STATUS = 0;
  MTdev.STATUS2 = 0;

  MTdev.iod_mode = 0;
  MTdev.iod_delay = 0;
  MTdev.iod_wasWriting = FALSE;
  MTmode = MT_IDLE;

  if ((uptr = MTdev.iod_unit) != NULL) {
    fw_setForced(&MTdev, IO_ST_READY);

    MTdev.STATUS2 = uptr->DENS & (IO_ST2_556 | IO_ST2_800);
    if ((uptr->flags & UNIT_WPROT) != 0)
      MTdev.STATUS2 &= ~IO_ST2_WENABLE;
    else MTdev.STATUS2 |= IO_ST2_WENABLE;
    if ((uptr->flags & UNIT_7TRACK) != 0)
      MTdev.STATUS2 |= IO_ST2_7TRACK;
    else MTdev.STATUS2 &= ~IO_ST2_7TRACK;
  }
}

/*
 * Check if I/O should be rejected. I/O allowed if:
 *
 * Reg.                 Write (OUT)                     Read (INP)
 *
 *  00          Ready and writing active        Ready and data available
 *  01          Controller dependent            Always allowed
 *              Always allow Clear Interrupts/Controller
 *              1732-A: Reject if controller active
 *              1732-3: Always allow
 *  10          Controller active clear         Always allowed
 *  11          Controller busy and EOP clear   Always allowed
 *
 * If a data I/O (register 0) is performed after the tape motion has timed
 * out, we need to generate an ALARM + LOST data status.
 */
t_bool MTreject(IO_DEVICE *iod, t_bool output, uint8 reg)
{
  switch (reg) {
    case 0:
      if (output)
        return ((iod->STATUS & (IO_ST_DATA | IO_ST_READY)) !=
                (IO_ST_DATA | IO_ST_READY)) || (MTmode != MT_WRITING);

      return ((iod->STATUS & (IO_ST_DATA | IO_ST_READY)) !=
              (IO_ST_DATA | IO_ST_READY)) || (MTremain == 0);

    case 1:
      if (output) {
        if (MTdev.iod_type == DEVTYPE_1732_A)
          if ((IOAreg & (IO_DIR_CCONT | IO_DIR_CINT)) == 0)
            return ((iod->STATUS & IO_1732_ACTIVE) != 0);
      }
      break;
      
    case 2:
      if (output)
        return ((iod->STATUS & IO_1732_ACTIVE) != 0);
      break;

    case 3:
      if (MTdev.iod_type != DEVTYPE_1732_3)
        return TRUE;
      if (output)
        return ((iod->STATUS & (IO_ST_EOP | IO_ST_BUSY)) == IO_ST_BUSY);
      break;
  }
  return FALSE;
}

/* Perform an input operation on a selected drive. This can be performed
   by issuing a command directly to the device or via a 1706 */

enum IOstatus doMTIn(UNIT *uptr, uint16 *data, t_bool via1706)
{
  uint16 result;

  /*
   * Reject the request if we are not reading or data is not available
   */
  if ((MTmode != MT_READING) || ((MTdev.STATUS & IO_ST_DATA) == 0))
    return IO_REJECT;

  sim_cancel(uptr);

  if ((MTdev.iod_mode & IO_1732_ASSEM) != 0) {
    if (MTremain >= 2) {
      result = (MTbuf[MToffset] << 8) | MTbuf[MToffset + 1];
      MToffset += 2;
      MTremain -= 2;
    } else {
      MTdev.STATUS |= IO_1732_FILL;
      result = MTbuf[MToffset] << 8;
      MToffset++;
      MTremain--;
    }
  } else {
    result = MTbuf[MToffset];
    MToffset++;
    MTremain--;
  }
  if ((uptr->flags & UNIT_7TRACK) != 0)
    result &= 0x3F3F;

  if ((mt_dev.dctrl & DBG_RDATA) != 0)
    mt_data(uptr, FALSE, result);

  fw_IOintr(FALSE, &mt_dev, &MTdev, 0, IO_ST_DATA, 0xFFFF, NULL);

  if (MTremain != 0) {
    MTdev.iod_delay = IO_DELAY_RDATA;
    sim_activate(uptr, (int32)(MTdev.iod_event - Instructions));
  } else {
    MTmode = MT_IDLE;
    MTdev.STATUS |= IO_ST_EOP;
    MTdev.STATUS &= ~(IO_1732_ACTIVE | IO_ST_BUSY);
    if ((mt_dev.dctrl & DBG_OPS) != 0)
      mt_trace(uptr, "Consumed read buffer", (t_stat)-1, FALSE);
  }

  *data = result;
  return IO_REPLY;
}

/* Perform an output operation on a selected drive. This can be performed
   by issuing a command directly to the device or via a 1706 */

enum IOstatus doMTOut(UNIT *uptr, uint16 *data, t_bool via1706)
{
  uint16 temp = *data;
  t_mtrlnt need = ((MTdev.iod_mode & IO_1732_ASSEM) != 0) ? 2 : 1;

  /*
   * Reject the request if we are not writing or data cannot be written.
   */
  if ((MTmode != MT_WRITING) || (MTdev.STATUS & IO_ST_DATA) == 0)
    return IO_REJECT;

  sim_cancel(uptr);

  if ((uptr->flags & UNIT_7TRACK) != 0)
    temp &= 0x3F3F;

  if (MTremain < need)
    return IO_REJECT;

  if ((MTdev.iod_mode & IO_1732_ASSEM) != 0) {
    MTbuf[MToffset] = (temp >> 8) & 0xFF;
    MTbuf[MToffset + 1] = temp & 0xFF;
    MToffset += 2;
    MTremain -= 2;
  } else {
    MTbuf[MToffset] = temp & 0xFF;
    MToffset += 1;
    MTremain -= 1;
  }

  if ((mt_dev.dctrl & DBG_WDATA) != 0)
    mt_data(uptr, TRUE, temp);

  fw_IOintr(FALSE, &mt_dev, &MTdev, 0, IO_ST_DATA, 0xFFFF, NULL);
  MTdev.iod_delay = IO_DELAY_WDATA;
  sim_activate(uptr, (int32)(MTdev.iod_event - Instructions));

  return IO_REPLY;
}

/* Perform control function */

enum IOstatus doMTFunction(DEVICE *dev)
{
  UNIT *uptr;
  t_stat st;

  /*
   * Handle commands in the following order:
   *
   * 1. Handle clears
   * 2. Handle interrupt selections
   * 3. Handle motion control
   */
  switch (IOAreg & IO_1732_MOTION) {
    case 0:
    case IO_1732_WRITE:
    case IO_1732_READ:
    case IO_1732_BACKSP:
    case IO_1732_WFM:
    case IO_1732_SFWD:
    case IO_1732_SBACK:
    case IO_1732_REWL:
      break;

    case IO_1732A_REWU:
      if (MTdev.iod_type == DEVTYPE_1732_3)
        return IO_REJECT;
      break;

    default:
      return IO_REJECT;
  }

  if (doDirectorFunc(&mt_dev, TRUE)) {
    /*
     * The device interrupt mask has been explicitly changed. If the device
     * state is such that an interrupt can occur, generate it now.
     */
    fw_IOintr(FALSE, &mt_dev, &MTdev, 0, 0, 0xFFFF, "Mask change interrupt");
  }

  /*
   * All done if there is no motion control requested.
   */
  if ((IOAreg & IO_1732_MOTION) == 0)
    return IO_REPLY;

  /*
   * Drive must be selected to perform a motion operation
   */
  if ((uptr = MTdev.iod_unit) == NULL)
    return IO_REJECT;

  /*
   * We now know we have a valid motion command.
   */
  MTdev.iod_wasWriting = MTmode == MT_WRITING;

  /*
   * If we are currently writing to the tape, terminate the current
   * record before initiating the new tape motion command.
   */
  if (MTmode == MT_WRITING) {
    st = sim_tape_wrrecf(uptr, MTbuf, MToffset);

    if ((mt_dev.dctrl & DBG_MTIO) != 0)
      mtio_trace(uptr, "wrrecf", st, TRUE, MToffset);

    MTmode = MT_IDLE;
    MTdev.STATUS &= ~IO_1732_ACTIVE;
  }

  /*
   * Clear ALARM, LOST data, FILL and any position information on a motion
   * operation
   */
  if ((IOAreg & IO_1732_MOTION) != 0) {
    MTdev.STATUS &= ~IO_ST_ALARM;
    MTdev.STATUS &= ~(IO_ST_LOST | IO_1732_FILL);
    MTdev.STATUS &= ~(IO_1732_FMARK | IO_1732_EOT | IO_1732_BOT);
  }

  switch (IOAreg & IO_1732_MOTION) {
    case IO_1732_READ:
      MTmode = MT_READING;
      goto active;

    case IO_1732_WRITE:
      MTmode = MT_WRITING;
      MToffset = 0;
      MTremain = MTSIZ;
      goto active;

    case IO_1732_BACKSP:
    case IO_1732_WFM:
    case IO_1732_SFWD:
    case IO_1732_SBACK:
  active:
      MTdev.STATUS |= IO_1732_ACTIVE;
      break;

    case IO_1732_REWL:
      if (!MTdev.iod_wasWriting && sim_tape_bot(uptr)) {
        /*
         * If we are currently standing at the load point, complete the
         * request immediately. Diagnostic 0F (BD2) relies on this
         * behaviour.
         */
        MTdev.STATUS |= IO_1732_BOT;
        if ((mt_dev.dctrl & DBG_OPS) != 0)
          mt_trace(uptr, "REWL", (t_stat)-1, FALSE);

        fw_IOcompleteEOP(FALSE, &mt_dev, &MTdev, 0xFFFF, "Rewind complete");
        return IO_REPLY;
      }
      /* FALLTHROUGH */

    case IO_1732A_REWU:
      break;
  }

  /*
   * Mark I/O underway and activate a delayed operation.
   */
  fw_IOunderwayEOP(&MTdev, 0);

  sim_cancel(uptr);
  MTdev.iod_delay = Areg & IO_1732_MOTION;
  sim_activate(uptr, MT_MOTION_WAIT);
  return IO_REPLY;
}

/* Perform I/O */

enum IOstatus MTin(IO_DEVICE *iod, uint8 reg)
{
  UNIT *uptr = MTdev.iod_unit;

  /*
   * The framework only passes INP operations for the data register (0x380).
   */
  if (uptr != NULL) {
    if (((MTdev.STATUS & IO_ST_READY) != 0) && (MTremain != 0)) {
      return doMTIn(uptr, &Areg, FALSE);
    }
  }
  return IO_REJECT;
}

enum IOstatus MTout(IO_DEVICE *iod, uint8 reg)
{
  UNIT *uptr = MTdev.iod_unit;
  uint16 unit;

  switch (reg) {
    case 0x00:
      if (uptr != NULL) {
        if ((MTdev.STATUS & IO_ST_READY) != 0)
          return doMTOut(uptr, &Areg, FALSE);
      }
      return IO_REJECT;

    case 0x01:
      return doMTFunction(MTdev.iod_outdev);

    case 0x02:
      /*
       * Get the unit number for select
       */
      unit = MTdev.iod_type == DEVTYPE_1732_3 ? IO_1732_UNIT : IO_1732A_UNIT;
      unit = (unit & Areg) >> 7;

      /*
       * Check for invalid bit combinations.
       */
      if ((Areg & IO_1732_PARITY) == IO_1732_PARITY)
        return IO_REJECT;

      if ((Areg & IO_1732_DESEL) != 0)
        if ((Areg & ~IO_1732_DESEL) != 0)
          return IO_REJECT;

      if ((Areg & IO_1732_SEL) != 0) {
        /*
         * Check for illegal unit select.
         */
        if (unit >= mt_dev.numunits)
          return IO_REJECT;
      }

      switch (Areg & (IO_1732_1600 | IO_1732_556 | IO_1732_800)) {
        case IO_1732_1600:      /* IO_1732A_200 on 1732-A */
        case IO_1732_556:
        case IO_1732_800:
          if (uptr != NULL)
            if ((mt_dev.dctrl & DBG_DENS) != 0) {
              DEVICE *dptr = find_dev_from_unit(uptr);
              int32 u = uptr - dptr->units;
              
              fprintf(DBGOUT,
                      "MT%d: Density changed to %04X\r\n",
                      u, Areg & (IO_1732_1600 | IO_1732_556 | IO_1732_800));
            }
          /* FALLTHROUGH */

        case 0:                         /* No change in density */
          break;

        default:
          return IO_REJECT;
      }

      /*
       * Process the select/deselect operation.
       */
      if ((Areg & IO_1732_DESEL) != 0) {
        /*** TODO: Implement protected device support ***/
        if ((mt_dev.dctrl & DBG_SELECT) != 0)
          if (MTdev.iod_unit != NULL) {
            DEVICE *dptr = find_dev_from_unit(uptr);
            int32 u = uptr - dptr->units;
            
            fprintf(DBGOUT, "MT%d - Deselected\r\n", u);
          }

        MTdev.iod_unit = NULL;
        fw_clearForced(&MTdev, IO_ST_READY);
        MTdev.STATUS2 = 0;
        return IO_REPLY;
      }

      if ((Areg & IO_1732_SEL) != 0) {
        MTdev.iod_unit = NULL;
        MTdev.STATUS &= ~(IO_1732_STCINT | IO_1732_FMARK | IO_1732_EOT);
        fw_clearForced(&MTdev, IO_ST_READY);
        
        uptr = &mt_unit[unit];

        if ((uptr->flags & UNIT_ATT) != 0) {
          MTdev.iod_unit = uptr;
          fw_setForced(&MTdev, IO_ST_READY);

          if (sim_tape_bot(uptr))
            MTdev.STATUS |= IO_1732_BOT;
          if (sim_tape_eot(uptr))
            MTdev.STATUS |= IO_1732_EOT;
        }
        if ((mt_dev.dctrl & DBG_SELECT) != 0)
          fprintf(DBGOUT, "MT%d Selected\r\n", unit);

        MTdev.STATUS2 = 0;
      }

      /*
       * Remember the current mode of operation.
       */
      MTdev.iod_mode = Areg;

      if ((uptr = MTdev.iod_unit) != NULL) {
        /*
         * If this operation modifies the density, remember it for later.
         */
        if ((Areg & (IO_1732_1600 | IO_1732_556 | IO_1732_800)) != 0) {
          if ((uptr->flags & UNIT_7TRACK) != 0) {
            uptr->DENS &= ~(IO_ST2_556 | IO_ST2_800 | IO_ST2_1600);
            if ((Areg & IO_1732_556) != 0)
              uptr->DENS |= IO_ST2_556;
            if ((Areg & IO_1732_800) != 0)
              uptr->DENS |= IO_ST2_800;
            if (MTdev.iod_type == DEVTYPE_1732_3)
              if ((Areg & IO_1732_1600) != 0)
                uptr->DENS |= IO_ST2_1600;
          }
        }
        /*
         * Make sure STATUS2 values are consistent with actual drive status.
         */
        MTdev.STATUS2 = uptr->DENS & (IO_ST2_556 | IO_ST2_800);
        if ((uptr->flags & UNIT_WPROT) != 0)
          MTdev.STATUS2 &= ~IO_ST2_WENABLE;
        else MTdev.STATUS2 |= IO_ST2_WENABLE;
        if ((uptr->flags & UNIT_7TRACK) != 0)
          MTdev.STATUS2 |= IO_ST2_7TRACK;
        else MTdev.STATUS2 &= ~IO_ST2_7TRACK;
      }
      return IO_REPLY;

    case 0x03:
      if ((uptr == NULL) || (MTdev.iod_type == DEVTYPE_1732_A))
        return IO_REJECT;
      MTdev.iod_LWA = LoadFromMem(IOAreg);
      MTdev.iod_CWA = MTdev.iod_FWA = ++IOAreg;
      MTdev.iod_DSApending = TRUE;
      if ((mt_dev.dctrl & DBG_OPS) != 0)
        mt_DSAtrace(uptr, "setup");

      return IO_REPLY;
  }
  return IO_REJECT;
}

/* Perform I/O initiated through a 1706 buffered data channel */

enum IOstatus MTBDCin(IO_DEVICE *iod, uint16 *data, uint8 reg)
{
  UNIT *uptr = MTdev.iod_unit;

  if ((mt_dev.dctrl & DBG_DTRACE) != 0) {
    int32 u = uptr - mt_dev.units;

    fprintf(DBGOUT,
            "%sMT%d: BDC input to register %d\r\n",
            INTprefix, u, reg);
  }

  /*
   * The framework only passes INP operations for the data register (0x380).
   */
  if (uptr != NULL) {
    if ((MTdev.STATUS & IO_ST_DATA) != 0)
      if (((MTdev.STATUS & IO_ST_READY) != 0) && (MTremain != 0))
        return doMTIn(uptr, data, TRUE);
  }
  return IO_REJECT;
}

enum IOstatus MTBDCout(IO_DEVICE *iod, uint16 *data, uint8 reg)
{
  UNIT *uptr = MTdev.iod_unit;

  if ((mt_dev.dctrl & DBG_DTRACE) != 0) {
    int32 u = uptr - mt_dev.units;

    fprintf(DBGOUT,
            "%sMT%d: BDC output, %04X from register %d\r\n",
            INTprefix, u, IOAreg, reg);
  }

  switch (reg) {
    case 0x00:
      if (uptr != NULL) {
        if ((MTdev.STATUS & IO_ST_READY) != 0)
          return doMTOut(uptr, data, TRUE);
      }
      return IO_REJECT;

    case 0x01:
      return doMTFunction(MTdev.iod_outdev);

    case 0x02:
      break;
  }
  return IO_REJECT;
}

t_stat mt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is either a 1732-A or 1732-3 magtape controller.\n"
    "1 Hardware Description\n"
    " The %D device consists of either a 1732-A or 1732-3 controller along\n"
    " with 4 tape transports. The type con controller present may be changed\n"
    " by:\n\n"
    "+sim> SET %D 1732-A\n"
    "+sim> SET %D 1732-3\n\n"
    " The first 3 transports (MT0, MT1, MT2) are 9-track drives and MT3 is a\n"
    " 7-track drive. The type of a transport may be changed with:\n\n"
    "+sim> SET %U 9TRACK\n"
    "+sim> SET %U 7TRACK\n\n"
    " Each drive may be individually write-locked or write-enabled with:\n\n"
    "+sim> SET %U LOCKED\n"
    "+sim> SET %U WRITEENABLED\n\n"
    " The 1732-A controller can only perform I/O 1 or 2 bytes at a time. In\n"
    " order to use DMA it must be coupled with a 1706-A. Due to the lack of\n"
    " DMA it can only support 200, 556 and 800 BPI on 9-track transports.\n\n"
    " The 1732-3 is a newer controller which has DMA capability built in. It\n"
    " loses the ability to handle 200 BPI tape but adds the ability to access\n"
    " 1600 BPI phase encoded tapes.\n"
    "2 Equipment Address\n"
    " Magtape controllers are typically set to equipment address 7. This\n"
    " address may be changed by:\n\n"
    "+sim> SET %D EQUIPMENT=hexValue\n\n"
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device. These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation. STATUS and\n"
    " STATUS2 always contains the current status of the device as it would be\n"
    " read by an application program.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}
