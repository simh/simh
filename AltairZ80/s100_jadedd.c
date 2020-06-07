/*  s100_jadedd.c: Jade Double D Disk Controller
  
    Created by Patrick Linstruth (patrick@deltecent.com)
    Based on s100_mdsa.c written by Mike Douglas
  
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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Patrick Linstruth shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Patrick Linstruth.
*/

/*
    The Jade Double Density Disk Controller "Double D" is a difficult device
    to emulate in SIMH due to the device having its own Z80 processor, I/O
    and memory address space.

    While the Double D is capable to loading many different operating systems,
    this emulator is centered around Digital Reasearch's CP/M 2 operating
    system as it was released by Jade.

    The process of booting CP/M with the DD is a bit more complicated than
    other controllers with a Western Digital FD FD179x. This is because
    the host is completely insulated from the FD179x. All interaction is
    done on the DD by the on-board Z80 processor.

    The process of loading CP/M starts with the DDBOOT PROM on the host
    system, typically at F000. The DDBOOT PROM contains the DD Boot
    Module that is injected onto the DD controller in memory bank 0. The
    host then resets the DD causing the on-board Z80 to execute the
    uploaded code. The DDBOOT PROM waits for the DD to complete its
    initialization program.

    The DD boot module loads the Disk Control Module (DCM) from track 0
    sectors 13-20 (1K) into memory bank 1. The DD boot module then jumps to
    the DCM's INIT vector at 0403H.

    The first job of the DCM INIT code is move itself from memory bank 1 to
    memory bank 0. The DCM, now executing from memory bank 0, loads the DD
    BIOS loader from track 0 sector 2 into the sector buffer. The BIOS loader
    program is then executed which reads the CP/M BIOS into memory bank 1.
    The Command Block in DCM is set to indicate the BIOS module size and
    the system load address. The DD then halts.

    When the DDBOOT PROM sees that the DD has halted, it checks for errors
    and then moves BIOS from memory bank 1 to the address stored in the
    Command Block. DDBOOT PROM then jumps to the BIOS cold start address.
  
    ** NOTE **
 
    This emulator does not actually execute Z80 code injected on the DD. The
    functionality of the code is only emulated. Changing the DD modules on the
    attached disk image, such as running DCMGEN, will not change the functionality
    of the DD emulator.
*/


/*
    The Double D is an intelligent S-100 based disk controller. It is capable
    of handling up to four full size (8") or mini (5") disk drives. Provisions
    have been made for double sided drives. Single and double sided drives may
    be mixed. The controller is capable of single density (FM) and double
    density (MFM) operation. It can be used in software polled as well as
    interrupt driven environments.

    The Double D contains an on-board Z80A microprocessor with 2K of static memory.
    The on-board processor runs simultaneously with and transparent to the S-100
    bus. All critic81 timing is handled on board; data transfers are fully buffered
    by sector in the on-board memory. The host system (8080, 8085, Z80, or ?) need
    only transfer commands and data through a block of static memory, which can be
    accessed from the bus. This architecture provides a high degree of timing
    independence from the host system. Also, since the disk controller program is
    contained on-board in ram, this board's operational characteristics are
    redefinable at any time during system operation.

    The host system communicates with the on-board processor thru the memory window.
    During a system boot, the control program must be loaded thru the memory window
    before the on-board processor can operate properly. It is entirely possible for
    the initial control program to be a small bootstrap which then loads a larger
    control program from disk. For reading and writing disk sectors, the host system
    must block move sector data through the memory window.

    The memory on the DD is allocated as follows:

    +--------------------------------------+
    |                                      |
    |           BANK 0 0000H-03FFH         |
    |                                      |
    +--------------------------------------+
    | 0000H-036FH        | DCM             |
    +--------------------+-----------------+
    | 0370H-037FH        | I/O BLOCK BEGIN |
    +--------------------+-----------------+
    | 0380H-03FFH        | SECTOR BUFFER   |
    +--------------------+-----------------+

    +--------------------------------------+
    |                                      |
    |           BANK 1 0400H-07FFH         |
    |                                      |
    +--------------------------------------+
    | 0000H-02FFH        |                 |
    +--------------------+-----------------+
    | 0300H-03FFH        | FORMAT BUFFER   |
    +--------------------+-----------------+
    | 0308H              | FORMAT PROGRAM  |
    +--------------------+-----------------+

    NOTE: Because the 5 upper address bits are not decoded, the 2K static memory
    block appears 32 times in the Z80A 64K address range. This allows internal
    programs to be assembled on any 2K boundary. Also note that the address
    selected for the memory window has no effect on the on-board processor or
    the on-board software.

    +------------------------------------+
    |                                    |
    |          I/O COMMAND BLOCK         |
    |             0370H-037FH            |
    |                                    |
    +------------------------------------+
    | 0370H       | CONTROL COMMAND      |
    +-------------+----------------------+
    | 0371H       | DRIVE NUMBER         |
    +-------------+----------------------+
    | 0372H       | LOGICAL TRACK NUMBER |
    +-------------+----------------------+
    | 0373H       | SECTOR NUMBER        |
    +-------------+----------------------+
    | 0374H       | FORMAT FLAGS         |
    +-------------+----------------------+
    | 0375H       | EIA CHARACTER        |
    +-------------+----------------------+
    | 0376H       | MODE SELECTS         |
    +-------------+----------------------+
    | 0377H       | CONTROLLER STATUS    |
    +-------------+----------------------+
    | 0378H-0379H | LOAD ADDRESS         |
    +-------------+----------------------+
    | 037AH-370BH | LOAD LENGTH          |
    +-------------+----------------------+

    +--------------------+
    |                    |
    |  CONTROL COMMANDS  |
    |                    |
    +--------------------+
    | 00H | LOG-ON DRIVE |
    +-----+--------------+
    | 01H | READ SECTOR  |
    +-----+--------------+
    | 02H | WRITE SECTOR |
    +-----+--------------+
    | 03H | FORMAT TRACK |
    +-----+--------------+
    | 04H | READ ADDRESS |
    +-----+--------------+
    | 05H | LIST OUTPUT  |
    +-----+--------------+
    | 06H | LIST STATUS  |
    +-----+--------------+
    | 07H | BACKGROUND   |
    +-----+--------------+

*/

/* #define DBG_MSG */

#include "altairz80_defs.h"
#include "sim_imd.h"

#ifdef DBG_MSG
#define DBG_PRINT(args) sim_printf args
#else
#define DBG_PRINT(args)
#endif

extern uint32 PCX;
extern t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), const char* name, uint8 unmap);
extern int32 find_unit_index(UNIT *uptr);

#define JADE_MAX_ADAPTERS       1
#define JADE_MAX_DRIVES         4
#define JADE_SECTOR_SIZE        128
#define JADE_SPT_SD             26
#define JADE_SPT_DD             50
#define JADE_TRACKS             77
#define JADE_CAPACITY           (JADE_TRACKS*JADE_SPT_SD+36)*JADE_SECTOR_SIZE      /* Default Jade Disk Capacity */

#define JADE_PROM_BASE          0xf000
#define JADE_PROM_SIZE          1024
#define JADE_PROM_MASK          (JADE_PROM_SIZE-1)
#define JADE_MEM_SIZE           2048
#define JADE_MEM_MASK           (JADE_MEM_SIZE-1)
#define JADE_BANK_BASE          0xe000
#define JADE_BANK_SIZE          1024
#define JADE_BANK_MASK          (JADE_BANK_SIZE-1)
#define JADE_IO_SIZE            1
#define JADE_IO_BASE            0x43

#define DCM_SEC                 13  /* DCM SECTOR */

/******************************************************
* DRIVE TABLE AREA DEFINED                            :
******************************************************/

/*******( FLAG BIT DEFINITIONS )**********************/

#define DF_T1D  0x02    /* TRACK 1 DENSITY (1 = DOUBLE). */
#define DF_DTD  0x04    /* DATA TRACKS DENSITY (1 = DD). */
#define DF_TSD  0x08    /* TWO SIDED ( 1 = TWO SIDES).   */

/*******( DRIVE TABLE AREA )**************************/

typedef struct {
    uint8 spt;          /* SECTORS PER TRACK       */
    uint8 flg;          /* SIDE AND DENSITY FLAGS  */
} drvtbl_t;

typedef struct {
    uint32 mem_base;    /* Memory Base Address              */
    uint32 mem_size;    /* Memory Address space requirement */
    uint32 io_base;     /* I/O Base Address                 */
    uint32 io_size;     /* I/O Address Space requirement    */
    uint32 prom_base;   /* Memory Base Address              */
    uint32 prom_size;   /* Memory Address space requirement */
    uint8 pe;           /* PROM enable                      */
    uint8 mem_bank;     /* 0 or 1                           */
    uint8 mem_sys;      /* FALSE=OUT or TRUE=IN             */
    uint8 curdrv;       /* Currently selected drive         */
    drvtbl_t dt[JADE_MAX_DRIVES];
    UNIT *uptr[JADE_MAX_DRIVES];
} JADE_INFO;

static JADE_INFO jade_info_data =   {   JADE_BANK_BASE, JADE_BANK_SIZE,
                                        JADE_IO_BASE, JADE_IO_SIZE,
                                        JADE_PROM_BASE, JADE_PROM_SIZE,
                                        TRUE, 0, FALSE, 0,
                                        {   { JADE_SPT_SD, DF_T1D },
                                            { JADE_SPT_SD, DF_T1D },
                                            { JADE_SPT_SD, DF_T1D },
                                            { JADE_SPT_SD, DF_T1D } }
                                    };

static JADE_INFO *jade_info = &jade_info_data;

/* Jade DD BOOT PROM is 590 bytes and executes at F000 */
static uint8 jade_prom[JADE_PROM_SIZE] = {
    0xc3,0x12,0xf0,0xc3,0x3a,0xf0,0xc3,0xd7,0xf0,0xc3,0xf3,0xf0,0xc3,0x10,0xf1,0xc3,
    0x2f,0xf1,0x3e,0x03,0xd3,0x10,0x3e,0x15,0xd3,0x10,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x31,0x80,0x00,0xdb,0x43,0xe6,
    0x0e,0x07,0xf6,0xe0,0x67,0x2e,0x00,0x22,0x40,0x00,0x3e,0x01,0x32,0x42,0x00,0x32,
    0x43,0x00,0x3e,0x01,0xd3,0x43,0x01,0xc8,0x00,0xeb,0x21,0x86,0xf1,0xcd,0xa4,0xf0,
    0x3e,0x80,0xd3,0x43,0xe3,0xe3,0x3a,0x42,0x00,0x47,0xdb,0x43,0xa0,0xc2,0x6a,0xf0,
    0x3e,0x01,0xd3,0x43,0x2a,0x40,0x00,0x11,0x77,0x03,0x19,0x7e,0xe6,0x80,0xc2,0xb1,
    0xf0,0x7e,0xa7,0xc2,0xc5,0xf0,0x2a,0x40,0x00,0x11,0x78,0x03,0x19,0x5e,0x23,0x56,
    0x23,0x4e,0x23,0x46,0xd5,0x3e,0x03,0xd3,0x43,0x2a,0x40,0x00,0xcd,0xa4,0xf0,0x3e,
    0x01,0xd3,0x43,0xc9,0x7e,0x23,0xeb,0x77,0x23,0xeb,0x0b,0x78,0xb1,0xc2,0xa4,0xf0,
    0xc9,0x3a,0x43,0x00,0xa7,0xca,0x52,0xf0,0xaf,0x32,0x43,0x00,0x21,0x53,0xf1,0xcd,
    0x2f,0xf1,0xc3,0x52,0xf0,0x32,0x43,0x00,0x21,0x6e,0xf1,0xcd,0x2f,0xf1,0x3a,0x43,
    0x00,0xcd,0x3b,0xf1,0x76,0x00,0x00,0xdb,0x10,0xee,0x00,0xe6,0x01,0xc8,0x3e,0xff,
    0xc9,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xcd,0xd7,0xf0,0xca,0xf3,0xf0,0xdb,0x11,0xe6,0x7f,0xc9,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xdb,0x10,0xee,0x00,0xe6,0x02,0xca,0x10,0xf1,0x79,0xd3,0x11,0xc9,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7e,
    0xfe,0x24,0xc8,0x4f,0xcd,0x10,0xf1,0x23,0xc3,0x2f,0xf1,0xf5,0x0f,0x0f,0x0f,0x0f,
    0xcd,0x44,0xf1,0xf1,0xe6,0x0f,0xfe,0x0a,0xda,0x4d,0xf1,0xc6,0x07,0xc6,0x30,0x4f,
    0xc3,0x10,0xf1,0x0d,0x0a,0x0a,0x49,0x4e,0x53,0x45,0x52,0x54,0x20,0x53,0x59,0x53,
    0x54,0x45,0x4d,0x20,0x44,0x49,0x53,0x4b,0x45,0x54,0x54,0x45,0x20,0x24,0x0d,0x0a,
    0x0a,0x44,0x44,0x42,0x4f,0x4f,0x54,0x20,0x4c,0x4f,0x41,0x44,0x20,0x45,0x52,0x52,
    0x4f,0x52,0x20,0x2d,0x20,0x24,0x31,0x00,0x04,0xdb,0x40,0x0e,0x00,0xdb,0x00,0xe6,
    0x01,0xc2,0x10,0x00,0x0e,0xff,0xcd,0x50,0x00,0x3e,0x04,0xd3,0x00,0xcd,0x50,0x00,
    0x32,0x77,0x03,0xe6,0x80,0xca,0x26,0x00,0xaf,0xc3,0xb1,0x00,0x79,0xd3,0x05,0xd3,
    0x07,0xfd,0x21,0x37,0x00,0x3e,0x18,0xa9,0xd3,0x04,0xc3,0x34,0x00,0x2e,0x4c,0xcd,
    0x50,0x00,0xe6,0x04,0xc2,0x70,0x00,0x2d,0xca,0xaf,0x00,0xdb,0x08,0x11,0x0a,0x00,
    0xcd,0xba,0x00,0xc3,0x39,0x00,0x3e,0xd0,0xa9,0xd3,0x04,0xe3,0xe3,0xe3,0xe3,0xdb,
    0x04,0xa9,0xc9,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xdb,0x04,0xa9,0x32,
    0x77,0x03,0xfd,0xe3,0xed,0x45,0x11,0x28,0x00,0xcd,0xba,0x00,0x11,0x00,0x04,0x21,
    0x00,0x04,0xfd,0x21,0xa5,0x00,0x3e,0x0d,0xa9,0xd3,0x06,0x3e,0x98,0xa9,0xd3,0x04,
    0xdb,0x80,0xdb,0x07,0xa9,0x77,0x23,0x1b,0x7a,0xb3,0xc2,0x8a,0x00,0xdb,0x04,0xa9,
    0xe6,0x9c,0xc2,0xaa,0x00,0xcd,0x50,0x00,0xc3,0x03,0x04,0x3e,0x02,0xc3,0xb1,0x00,
    0x3e,0x04,0xc3,0xb1,0x00,0x3e,0x01,0x32,0x76,0x03,0xaf,0xd3,0x00,0xdb,0x10,0x76,
    0x3e,0xdc,0x3d,0x00,0xc2,0xbc,0x00,0x1b,0x7a,0xb3,0xc2,0xba,0x00,0xc9 
};

#define JADE_STAT_HLT_MSK   0x01
#define JADE_STAT_HALT      0x00
#define JADE_STAT_MEM_MSK   0x0e

#define CMD_SIN             0x01    /* Switch DD bank 0 into system */
#define CMD_MD0             0x01    /* Select DD bank 0             */
#define CMD_MD1             0x03    /* Select DD bank 1             */
#define CMD_SOT             0x00    /* Switch DD mem out of system  */
#define CMD_INT             0x02    /* Isssue DD Z80A interrupt     */
#define CMD_BGN             0x80    /* Reset Z80 and execute        */

#define DC_LOG              0x00    /* Log on diskette              */
#define DC_RDS              0x01    /* Read sector                  */
#define DC_WRS              0x02    /* Write sector                 */
#define DC_FMT              0x03    /* Format track                 */
#define DC_ADR              0x04    /* Address                      */
#define DC_LST              0x05    /* List character               */
#define DC_LCK              0x06    /* List status check            */
#define DC_IDL              0x07    /* Idle                         */

#define DD_CBT            0x0370    /* Command block (bank 0)       */
#define DD_BUF            0x0380    /* Sector buffer (bank 0)       */
#define DD_FBF            0x0300    /* Format buffer (bank 1)       */
#define DD_FPS            0x0308    /* Format program (bank 1)      */
#define DD_DPB            0x03a0    /* ID Sec DPB                   */
#define DD_DDF            0x03b1    /* ID Sec flags                 */

/*******( STATUS BIT DEFINITIONS )*****************/

#define CS_DNR  0x80   /* DRIVE NOT READY         */
#define CS_WRP  0x40   /* WRITE PROTECTED         */
#define CS_BT5  0x20   /* NOT ASSIGNED            */
#define CS_RNF  0x10   /* RECORD NOT FOUND        */
#define CS_CRC  0x08   /* CRC ERROR               */
#define CS_LDE  0x04   /* LOST DATA ERROR         */
#define CS_HME  0x02   /* DRIVE HOME ERROR        */
#define CS_TSD  0x01   /* TWO SIDES FLAG (FORMAT) */
#define CS_NOE  0x00   /* NO ERROR                */


/*
** 2 banks of 1K RAM on Jade DD
*/
static uint8 jade_mem[JADE_MEM_SIZE];
static uint8 *bank0=jade_mem;
static uint8 *bank1=jade_mem + JADE_BANK_SIZE;
static uint8 *sbuf=jade_mem+DD_BUF;

#define WORD(lsb,msb) (lsb + (msb << 8))
#define MEM_BANK_OFFSET(a) ((int)((uint8 *) a - bank0))

/* Double D - DCM Command Block */

#define CB_CMD 0           /* DCM command         */
#define CB_DRV 1           /* Drive number        */
#define CB_TRK 2           /* Track number        */
#define CB_SEC 3           /* Sector number       */
#define CB_SP0 4           /* Spare byte 0        */
#define CB_CHR 5           /* Character list      */
#define CB_MOD 6           /* Mode controls       */
#define CB_STS 7           /* Command status      */
#define CB_LAD 8           /* Load address (WORD) */
#define CB_LNG 10          /* Load length  (WORD) */

uint8 *cb = jade_mem + DD_CBT;

#define ID_LBL  0                 /* ID SECTOR LABEL */
#define ID_BLK  ID_LBL+0x20       /* ID BLOCK AREA   */
#define ID_SPT  ID_BLK            /* ID SECT PER TRK */
#define ID_FLG  ID_BLK+0x11       /* DISKETTE FLAGS  */
#define ID_FLD  0                 /* 3740 FLAGS      */

/*
** The FORMAT sector buffer is the following format
**
** DB    'FORMAT!'
** DB    'S' or 'D'
** LXI   SECTOR LIST ADDRESS
** MVI   E,SECTORS
**
*/
#define FMT_HDR 0          /* 'FORMAT!'   */
#define FMT_DEN 7          /* 'S' or 'D'  */
#define FMT_LST 8          /* Sector List */
#define FMT_SEC 12         /* Sectors     */

uint8 *fmt = jade_mem + JADE_BANK_SIZE + DD_FBF;

/* Local function prototypes */
static t_stat jade_reset(DEVICE *jade_dev);
static t_stat jade_svc(UNIT *uptr);
static t_stat jade_attach(UNIT *uptr, CONST char *cptr);
static t_stat jade_detach(UNIT *uptr);
static t_stat jade_boot(int32 unitno, DEVICE *dptr);
static t_stat jade_set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat jade_set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat jade_show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat jade_set_prom(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static uint32 calculate_jade_sec_offset(uint8 track, uint8 sector, uint8 flg);
static void showsector(uint8 drive, uint8 isRead, uint8 *buf);
static void showcb(void);
static uint8 JADE_In(uint32 Addr);
static uint8 JADE_Out(uint32 Addr, int32 data);
static uint8 PROM_Boot(JADE_INFO *info);
static void DCM_Init(void);
static void DCM_DBSLdr(void);
static uint8 DCM_Execute(void);
static uint8 DCM_Logon(uint8 drive);
static uint8 DCM_ReadSector(uint8 drive, uint8 track, uint8 sector, uint8 *buffer);
static uint8 DCM_WriteSector(uint8 drive, uint8 track, uint8 sector, uint8 *buffer);
static uint8 DCM_Format(uint8 drive, uint8 track);
static const char* jade_description(DEVICE *dptr);

static int32 jadedev(int32 Addr, int32 rw, int32 data);
static int32 jadeprom(int32 Addr, int32 rw, int32 data);
static int32 jademem(int32 Addr, int32 rw, int32 data);

static UNIT jade_unit[JADE_MAX_DRIVES] = {
    { UDATA (jade_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, JADE_CAPACITY), 10000 },
    { UDATA (jade_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, JADE_CAPACITY), 10000 },
    { UDATA (jade_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, JADE_CAPACITY), 10000 },
    { UDATA (jade_svc, UNIT_FIX + UNIT_ATTABLE + UNIT_DISABLE + UNIT_ROABLE, JADE_CAPACITY), 10000 }
};

static REG jade_reg[] = {
    { NULL }
};

#define JADE_NAME  "Jade Double D Controller"
#define JADE_SNAME "JADEDD"

static const char* jade_description(DEVICE *dptr) {
    return JADE_NAME;
}

/*
** These definitions should probably be in s100_jade.h
** so they can be included in other modules.
*/
#define UNIT_V_JADE_VERBOSE      (UNIT_V_UF + 0)                      /* VERBOSE / QUIET */
#define UNIT_JADE_VERBOSE        (1 << UNIT_V_JADE_VERBOSE)
#define UNIT_V_JADE_WPROTECT     (UNIT_V_UF + 1)                      /* WRTENB / WRTPROT */
#define UNIT_JADE_WPROTECT       (1 << UNIT_V_JADE_WPROTECT)

/*
** These definitions should probably be in altairz80_sio.h
** so they can be included in other modules, like this one.
*/
#define UNIT_V_SIO_SLEEP    (UNIT_V_UF + 7)     /* sleep after keyboard status check            */
#define UNIT_SIO_SLEEP      (1 << UNIT_V_SIO_SLEEP)

static MTAB jade_mod[] = {
    { MTAB_XTD|MTAB_VDV,    0,                      "IOBASE",  "IOBASE",
        &jade_set_iobase, &show_iobase, NULL, "Sets Jade Double D IO base address"   },
    { MTAB_XTD|MTAB_VDV,    0,                  "MEMBASE",  "MEMBASE",
        &jade_set_membase, &jade_show_membase, NULL, "Sets Jade Double D memory block base address"   },
    { MTAB_XTD | MTAB_VDV,  1,     NULL,           "PROM",         &jade_set_prom,
        NULL, NULL, "Enable Jade Double D boot PROM"},
    { MTAB_XTD | MTAB_VDV,  0,      NULL,           "NOPROM",      &jade_set_prom,
        NULL, NULL, "Disable Jade Double D boot PROM"   },
    { UNIT_JADE_VERBOSE,   0,                    "QUIET",    "QUIET",
        NULL, NULL, NULL, "No verbose messages for unit " JADE_NAME "n"                 },
    { UNIT_JADE_VERBOSE,   UNIT_JADE_VERBOSE, "VERBOSE",  "VERBOSE",
        NULL, NULL, NULL, "Verbose messages for unit " JADE_NAME "n"                    },
    { UNIT_JADE_WPROTECT,  0,                      "WRTENB",    "WRTENB",  NULL, NULL, NULL,
        "Enables " JADE_NAME "n for writing"                 },
    { UNIT_JADE_WPROTECT,  UNIT_JADE_WPROTECT,  "WRTPROT",    "WRTPROT",  NULL, NULL, NULL,
        "Protects " JADE_NAME "n from writing"                },
    { 0 }
};

/* Debug flags */
#define ERROR_MSG           (1 << 0)
#define SEEK_MSG            (1 << 1)
#define CMD_MSG             (1 << 2)
#define RD_DATA_MSG         (1 << 3)
#define WR_DATA_MSG         (1 << 4)
#define STATUS_MSG          (1 << 5)
#define RD_DATA_DETAIL_MSG  (1 << 6)
#define WR_DATA_DETAIL_MSG  (1 << 7)

/* Debug Flags */
static DEBTAB jade_dt[] = {
    { "ERROR",      ERROR_MSG,          "Error messages"        },
    { "SEEK",       SEEK_MSG,           "Seek messages"         },
    { "CMD",        CMD_MSG,            "Command messages"      },
    { "READ",       RD_DATA_MSG,        "Read messages"         },
    { "WRITE",      WR_DATA_MSG,        "Write messages"        },
    { "STATUS",     STATUS_MSG,         "Status messages"       },
    { "RDDETAIL",   RD_DATA_DETAIL_MSG, "Read detail messages"  },
    { "WRDETAIL",   WR_DATA_DETAIL_MSG, "Write detail messags"  },
    { NULL,         0                                           }
};

DEVICE jade_dev = {
    JADE_SNAME,                           /* name */
    jade_unit,                            /* unit */
    jade_reg,                             /* registers */
    jade_mod,                             /* modifiers */
    JADE_MAX_DRIVES,                      /* # units */
    10,                                   /* address radix */
    31,                                   /* address width */
    1,                                    /* addr increment */
    JADE_MAX_DRIVES,                      /* data radix */
    JADE_MAX_DRIVES,                      /* data width */
    NULL,                                 /* examine routine */
    NULL,                                 /* deposit routine */
    &jade_reset,                          /* reset routine */
    &jade_boot,                           /* boot routine */
    &jade_attach,                         /* attach routine */
    &jade_detach,                         /* detach routine */
    &jade_info_data,                      /* context */
    (DEV_DISABLE | DEV_DIS | DEV_DEBUG),  /* flags */
    ERROR_MSG,                            /* debug control */
    jade_dt,                              /* debug flags */
    NULL,                                 /* mem size routine */
    NULL,                                 /* logical name */
    NULL,                                 /* help */
    NULL,                                 /* attach help */
    NULL,                                 /* context for help */
    &jade_description                     /* description */
};

/* Reset routine */
t_stat jade_reset(DEVICE *dptr)
{
    uint8 i;
    JADE_INFO *pInfo = (JADE_INFO *)dptr->ctxt;

    if(dptr->flags & DEV_DIS) { /* Disconnect I/O Ports */
        sim_map_resource(pInfo->prom_base, pInfo->prom_size, RESOURCE_TYPE_MEMORY, &jadeprom, "jadeprom", TRUE);
        sim_map_resource(pInfo->mem_base, pInfo->mem_size, RESOURCE_TYPE_MEMORY, &jademem, "jademem", TRUE);
        sim_map_resource(pInfo->io_base, pInfo->io_size, RESOURCE_TYPE_IO, &jadedev, "jadedev", TRUE);
    } else {
        if(pInfo->pe) {
            if(sim_map_resource(pInfo->prom_base, pInfo->prom_size, RESOURCE_TYPE_MEMORY, &jadeprom, "jadeprom", FALSE) != 0) {
                sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME ": Error mapping MEM resource at 0x%04x\n", pInfo->prom_base);
                return SCPE_ARG;
            }
        }
        if(sim_map_resource(pInfo->mem_base, pInfo->mem_size, RESOURCE_TYPE_MEMORY, &jademem, "jademem", FALSE) != 0) {
            sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME ": Error mapping MEM resource at 0x%04x\n", pInfo->mem_base);
            return SCPE_ARG;
        }
        /* Connect I/O Ports at base address */
        if(sim_map_resource(pInfo->io_base, pInfo->io_size, RESOURCE_TYPE_IO, &jadedev, "jadedev", FALSE) != 0) {
            sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME ": Error mapping I/O resource at 0x%02x\n", pInfo->io_base);
            return SCPE_ARG;
        }
    }

    pInfo->curdrv = 0;

    /* Reset Registers and Interface Controls */
    for (i=0; i < JADE_MAX_DRIVES; i++) {
        if (jade_info->uptr[i] == NULL) {
            jade_info->uptr[i] = &jade_dev.units[i];
        }
    }

    sim_debug(STATUS_MSG, &jade_dev, JADE_SNAME ": reset controller.\n");

    return SCPE_OK;
}

/* Attach routine */
t_stat jade_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;
    unsigned int i = 0;

    r = attach_unit(uptr, cptr);    /* attach unit  */

    if(r != SCPE_OK) {              /* error?       */
        sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME ": ATTACH error=%d\n", r);
        return r;
    }

    /* Determine length of this disk */
    if(sim_fsize(uptr->fileref) != 0) {
        uptr->capac = sim_fsize(uptr->fileref);
    } else {
        uptr->capac = JADE_CAPACITY;
    }

    DBG_PRINT(("JADE: ATTACH uptr->capac=%d\n", uptr->capac));

    for (i = 0; i < JADE_MAX_DRIVES; i++) {
        if(jade_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= JADE_MAX_DRIVES) {
        return SCPE_ARG;
    }

    /* Default for new file is DSK */
    uptr->u3 = IMAGE_TYPE_DSK;

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && (strncmp(header, "CPT", 3) == 0)) {
            sim_printf(JADE_SNAME ": CPT images not yet supported\n");
            uptr->u3 = IMAGE_TYPE_CPT;
            jade_detach(uptr);
            return SCPE_OPENERR;
        } else {
            uptr->u3 = IMAGE_TYPE_DSK;
        }
    }

    if (uptr->flags & UNIT_JADE_VERBOSE) {
        sim_printf(JADE_SNAME "%d: attached to '%s', type=%s, len=%d\n", i, cptr,
            uptr->u3 == IMAGE_TYPE_CPT ? "CPT" : "DSK",
            uptr->capac);
    }

    return SCPE_OK;
}


/* Detach routine */
t_stat jade_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    for (i = 0; i < JADE_MAX_DRIVES; i++) {
        if(jade_dev.units[i].fileref == uptr->fileref) {
            break;
        }
    }

    if (i >= JADE_MAX_DRIVES) {
        return SCPE_ARG;
    }

    DBG_PRINT(("Detach JADE%d\n", i));

    r = detach_unit(uptr);  /* detach unit */

    if (r != SCPE_OK) {
        return r;
    }

    jade_dev.units[i].fileref = NULL;

    if (uptr->flags & UNIT_JADE_VERBOSE) {
        sim_printf(JADE_SNAME "%d: detached.\n", i);
    }

    return SCPE_OK;
}

/*
** Verify that iobase is within valid range
** before calling set_iobase
*/
static t_stat jade_set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 newba;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;

    newba = get_uint (cptr, 16, 0xFF, &r);
    if (r != SCPE_OK)
        return r;

    if ((newba > 0x43) || (newba < 0x40)) {
        sim_printf(JADE_SNAME ": Valid options are 40,41,42,43\n");
        return SCPE_ARG;
    }

    return set_iobase(uptr, val, cptr, desc);
}

/*
** Verify that membase is within valid range
** before calling set_membase
*/
static t_stat jade_set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    uint32 newba;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;

    newba = get_uint (cptr, 16, 0xFFFF, &r);
    if (r != SCPE_OK)
        return r;

    if ((newba > 0xFC00) || (newba < 0xE000) || (newba % jade_info->mem_size)) {
        sim_printf(JADE_SNAME ": Valid options are E000,E400,E800,EC00,F000,F400,F800,FC00\n");
        return SCPE_ARG;
    }

    return set_membase(uptr, val, cptr, desc);
}

/* Show Base Address routine */
t_stat jade_show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr;
    JADE_INFO *pInfo;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    pInfo = (JADE_INFO *) dptr->ctxt;

    fprintf(st, "MEM=0x%04X-0x%04X", pInfo->mem_base, pInfo->mem_base+pInfo->mem_size-1);

    if(pInfo->pe)
        fprintf(st, ", PROM=0x%04X-0x%04X", pInfo->prom_base, pInfo->prom_base+pInfo->prom_size-1);

    return SCPE_OK;
}

static t_stat jade_set_prom(UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    JADE_INFO *pInfo = (JADE_INFO *)uptr->dptr->ctxt;

    jade_info->pe = (uint8) value;

    /*
    ** Map/Unmap PROM
    */
    sim_map_resource(pInfo->prom_base, pInfo->prom_size, RESOURCE_TYPE_MEMORY, &jadeprom, "jadeprom", !value);

    if (uptr->flags & UNIT_JADE_VERBOSE) {
        sim_printf(JADE_SNAME ": PROM %s\n", (value) ? "enabled" : "disabled");
    }

    return SCPE_OK;
}

static t_stat jade_boot(int32 unitno, DEVICE *dptr)
{

    JADE_INFO *pInfo = (JADE_INFO *)dptr->ctxt;

    if (pInfo->uptr[0]->flags & UNIT_JADE_VERBOSE) {
        sim_printf(JADE_SNAME ": Booting Controller at 0x%04x\n", pInfo->prom_base);
    }

    *((int32 *) sim_PC->loc) = pInfo->prom_base;

    return SCPE_OK;
}

static t_stat jade_svc(UNIT *uptr)
{
    return SCPE_OK;
}

static void showsector(uint8 drive, uint8 isRead, uint8 *buf) {
    int32 i;

    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &jade_dev, JADE_SNAME "%d: %s sector:\n\t", drive, (isRead) ? "Read" : "Write");
    for (i=0; i < JADE_SECTOR_SIZE; i++) {
        sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &jade_dev, "%02X ", *(buf+i));
        if (((i+1) & 0xf) == 0) {
            sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &jade_dev, "\n\t");
        }
    }
    sim_debug(RD_DATA_DETAIL_MSG|WR_DATA_DETAIL_MSG, &jade_dev, "\n");
}

static void showcb()
{
    DBG_PRINT((JADE_SNAME
        " cmd=0x%02X drv=%d trk=%02d sec=%02d mod=0x%02X sts=0x%02X lad=%04X lng=%04X\n",
        cb[CB_CMD], cb[CB_DRV], cb[CB_TRK], cb[CB_SEC], cb[CB_MOD], cb[CB_STS], cb[CB_LAD] + (cb[CB_LAD+1] << 8), cb[CB_LNG] + (cb[CB_LNG+1] << 8)));
}

static int32 jadedev(int32 Addr, int32 rw, int32 data)
{
    if (rw == 0) { /* Read */
        return(JADE_In(Addr));
    } else {       /* Write */
        return(JADE_Out(Addr, data));
    }
}

/*
** The SYSTEM TRACKS have a different layout than the diskettes distributed
** by DIGITAL RESEARCH. Those modules residing on the SYSTEM TRACKS which
** often need to be modified for a specific system are on track 0, which is in
** single density. CCP and BOOS, which are not modified by the user, are
** on track 1 in double density. All data tracks are in single density
** such that the DOUBLE D distribution diskette can be read and modified
** on most 8" single density CP/M systems.
**
** Track 0, sector 1, is used by the Jade DD to store a disk identity label.
**
** The identity label consists of the following:
**
** 00H-20H "Jade DD ..." ; Diskette ID Label
** 20H-31H               ; ID Block Area
** 32H                   ; Diskette Flags
** 33H                   ; 3740 flags
**
*/
static uint32 calculate_jade_sec_offset(uint8 track, uint8 sector, uint8 flg)
{
    uint32 offset;

    /*
    ** Calculate track offset
    */
    if (track<2) {
        offset=track*JADE_SPT_SD*JADE_SECTOR_SIZE;
    }
    else {
        offset=JADE_SPT_SD*JADE_SECTOR_SIZE; /* Track 0 */
        offset+=((flg&DF_T1D) ? JADE_SPT_DD : JADE_SPT_SD) * JADE_SECTOR_SIZE; /* Track 1 */
        offset+=(track-2)*((flg&DF_DTD) ? JADE_SPT_DD : JADE_SPT_SD) * JADE_SECTOR_SIZE; /* Track 2-77 */
    }

    /*
    ** Add sector offset to track offset
    */
    offset += (sector-1)*JADE_SECTOR_SIZE;

    return (offset);
}

/*
** JADE DD Status Port
**
** The Disk Processor Status Port is an S-100 input port which allows
** the host processor to examine the current state of the Disk Processor.
** The port responds to occurrence of a pDBIN, sINP, and matching port
** address. The following states can be determined by reading this port.
**
** 1. On-board processor state (Run/Halt)
** 2. Address of 1K memory window
**
** Bit 0      0=HALT, 1=RUN
** Bit 1-3    000:E000-E3FF
**            001:E400-E7FF
**            001:E800-EBFF
**            001:EC00-EEFF
**            001:F000-F3FF
**            001:F400-F7FF
**            001:F800-FBFF
**            001:FC00-FEFF
*/
static uint8 JADE_In(uint32 Addr)
{
    uint8 cData;

    cData = JADE_STAT_HALT;     /* Assume processor is in HALT* state */
    cData |= (jade_info->mem_base >> 9) & JADE_STAT_MEM_MSK;

    sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": IN %02x Data %02x\n", Addr & 0xFF, cData & 0xFF);

    return (cData);
}

static uint8 JADE_Out(uint32 Addr, int32 Data)
{
    sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": OUT %02x Data %02x\n", Addr & 0xFF, Data & 0xFF);

    switch (Data) {
        case CMD_SOT:    /* Bank 0 out of system */
            sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": Z80 system memory out\n");
            jade_info->mem_sys = FALSE;
            break;

        case CMD_SIN|CMD_MD0:    /* Request memory bank 0 */
            sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": Z80 system memory in\n");
            jade_info->mem_sys = TRUE;
            sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": selected memory bank 0\n");
            jade_info->mem_bank = 0;
            break;

        case CMD_MD1:    /* Select memory bank 1 */
            sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": selected memory bank 1\n");
            jade_info->mem_bank = 1;
            break;

        case CMD_INT:    /* Interrupt Z80 */
            sim_debug(CMD_MSG, &jade_dev, JADE_SNAME ": Z80 interrupt\n");
            cb[CB_STS] = DCM_Execute();    /* Save status in command block */
            break;

        case CMD_BGN:    /* Reset and Execute */
            /*
            ** Card has been reset and the host boot PROM 
            ** has loaded the DCM injector module onto the DD.
            ** This modules reads the DCM from track 0 starting
            ** at sector 13 into memory bank 1. After the DCM
            ** is loaded, it is executed by the DD's on-board
            ** Z80 processor.
            */
            cb[CB_STS] = PROM_Boot(jade_info);

            break;

        default:
            break;
    }

    return(Data);
}

/*
** This doesn't really do anything for us other
** than have the DCM available to the host through
** the DD's bank0 memory window.
*/
static uint8 PROM_Boot(JADE_INFO *info)
{
    uint8 sec = DCM_SEC;
    uint16 d = JADE_BANK_SIZE;
    uint8 *h = bank1;
    uint8 sts;

    while(d) {
        if ((sts = DCM_ReadSector(0, 0, sec, h)) != CS_NOE) {
            return(sts);
        }

        h += JADE_SECTOR_SIZE;
        d -= JADE_SECTOR_SIZE;
        sec++;
    }

    DCM_Init();

    return (CS_NOE);
}

/*
** Again, regarding the DCM, this doesn't really do anything
** for us other than be able to view from the host the DCM in
** in memory bank 0 that was loaded from disk and load the
** Boot Loader Transient (BLT) module into the sector buffer.
** The BLT is emulated with DCM_DBSLdr() function.
*/
static void DCM_Init()
{
    /* Move Bank 1 to Bank 0 */
    memcpy(bank0, bank1, JADE_BANK_SIZE);

    /* Read the BLT from T0/S2 */
    cb[CB_TRK] = 0;
    cb[CB_SEC] = 2;

    if (DCM_ReadSector(0, cb[CB_TRK], cb[CB_SEC], sbuf) == CS_NOE) {

        /*
        ** Simulate the BLT loaded into the sector buffer
        */
        DCM_DBSLdr();
    }
}

/*****************************************************
; THE BIOS LOADER IS READ INTO THE DCM SECTOR BUFFER *
; AFTER DCM HAS INITIALIZED. THE BIOS LOADER PROGRAM *
; IS THEN EXECUTED WHICH READS THE DDBIOS MODULE     *
; INTO BANK 1. THE COMMAND BLOCK (IN DCM) IS SET TO  *
; INDICATE DDBIOS MODULE SIZE AND THE SYSTEM LOAD    *
; ADDRESS. THE BIOS LOADER PROGRAM IS GENERATED BY   *
; MOVCPM.COM AS THE COLD START LOADER (900-97F HEX). *
; THIS MODULE IS PROVIDED FOR REFERENCE PURPOSES.    *
;****************************************************/

#define LNG_1K    1024
#define SEC_BG    4                    /* First BIOS sector */

static void DCM_DBSLdr()
{
    uint8 sec = SEC_BG;
    uint16 d = LNG_1K;
    uint8 *h = bank1;

    while(d) {
        if (DCM_ReadSector(0, 0, sec, h) != CS_NOE) {
            return;
        }

        h += JADE_SECTOR_SIZE;
        d -= JADE_SECTOR_SIZE;
        sec++;
    }

    /*
    ** The first DDBIOS instruction is "JMP INIT"
    ** To accommodate various memory sizes, we will use
    ** the MSB of INIT to determine the load location for DDBIOS.
    */
    cb[CB_LAD] = 0;                            /* LSB */
    cb[CB_LAD+1] = *(bank1+2);                 /* MSB */
    cb[CB_LNG] = (LNG_1K & 0xff);              /* LSB */
    cb[CB_LNG+1] = ((LNG_1K & 0xff00) >> 8);   /* MSB */

    showcb();
}

/********************************************************
* THIS FUNCTION GAINS CONTROL AFTER THE DISK CONTROLLER *
* IS INTERRUPTED FROM THE HALT CONDITION BY THE HOST    *
* ISSUING A CMD_INT OUTPUT COMMAND.                     *
*                                                       *
* THIS FUNCTION HANDLES TO THE INDIVIDUAL COMMAND       *
* ROUTINES.                                             *
********************************************************/
static uint8 DCM_Execute()
{
    uint8 sts;

    showcb();

    switch (cb[CB_CMD]) {
        case DC_LOG:
            sts = DCM_Logon(cb[CB_DRV]);
            break;

        case DC_RDS:
            sts = DCM_ReadSector(cb[CB_DRV], cb[CB_TRK], cb[CB_SEC], sbuf);
            break;

        case DC_WRS:
            sts = DCM_WriteSector(cb[CB_DRV], cb[CB_TRK], cb[CB_SEC], sbuf);
            break;

        case DC_FMT:
            sts = DCM_Format(cb[CB_DRV], cb[CB_TRK]);
            break;

        case DC_ADR:
        case DC_LCK:
            sts = 0xff;
            break;

        case DC_LST:
        case DC_IDL:
            sts = cb[CB_STS];   /* Do not change status */
            break;

        default:
            sts = 0;
            break;
    }

    return(sts);
}

/******************************************************
; LOG.ON IS THE SUBROUTINE THAT READS THE IDENTITY    *
; SECTOR FROM THE DISKETTE AND MAKES THE NEEDED       *
; ENTRYS INTO THE DRIVE TABLE.  THE SECTOR DATA IS    *
; ALSO LEFT IN THE SECTOR BUFFER FOR BIOS TO FINISH   *
; THE LOG-ON OPERATION.                               *
;*****************************************************/
static uint8 DCM_Logon(uint8 drive)
{
    uint8 sts;

    sts = DCM_ReadSector(drive, 0, 1, sbuf);

    if (!strncmp((char *) sbuf, "Jade DD ", 8)) {
       jade_info->dt[drive].spt = sbuf[ID_SPT];
       jade_info->dt[drive].flg = sbuf[ID_FLG];
       if (jade_info->uptr[drive]->flags & UNIT_JADE_VERBOSE) {
           sim_printf(JADE_SNAME "%d: JADE ID Found: '%.32s' SPT=%0d FLG=0x%02X\n", drive, sbuf, jade_info->dt[drive].spt, jade_info->dt[drive].flg);
       }
    }
    else {
       jade_info->dt[drive].spt = JADE_SPT_SD;
       jade_info->dt[drive].flg = ID_FLD;
       if (jade_info->uptr[drive]->flags & UNIT_JADE_VERBOSE) {
           sim_printf(JADE_SNAME "%d: JADE ID Not Found: SPT=%0d FLG=0x%02X\n", drive, jade_info->dt[drive].spt, jade_info->dt[drive].flg);
       }
    }

    return(sts);
}

/******************************************************
; RD.SEC IS THE SUBROUTINE THAT INTERACTS WITH THE    *
; 179X-02 DURING READ SECTOR OPERATIONS. THIS SECTION *
; INITIATES THE DISK TRANSFER, SERVICES THE CONTROLLER*
; CHIP DURING DATA TRANSFER, AND TERMINATES OPERATION *
; WHEN FINISHED.  ERROR DETECTION IS IMPLEMENTED AND  *
; RETRIES ARE EXRCUTED IF DATA ERRORS ARE DETECTED.   *
;*****************************************************/
static uint8 DCM_ReadSector(uint8 drive, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 offset;

    jade_info->curdrv = drive;

    cb[CB_TRK] = track;
    cb[CB_SEC] = sector;

    /*
    ** Make sure drive is ready
    */
    if (jade_info->uptr[drive]->fileref == NULL) {
        if (jade_info->uptr[drive]->flags & UNIT_JADE_VERBOSE) {
            sim_printf(JADE_SNAME "%d: Drive Not Ready\n", drive);
        }
        return CS_DNR;
    }

    offset = calculate_jade_sec_offset(track, sector, jade_info->dt[drive].flg);

    if (sim_fseek(jade_info->uptr[drive]->fileref, offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME "%d: RDSEC sim_fseek error.\n", drive);
        return CS_RNF;
    }

    if (sim_fread(buffer, 1, JADE_SECTOR_SIZE, jade_info->uptr[drive]->fileref) != JADE_SECTOR_SIZE) {
        sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME "%d: RDSEC sim_fread error.\n", drive);
        return CS_CRC;
    }

    showsector(drive, TRUE, buffer);

    return CS_NOE;
}

/******************************************************
; WR.SEC  SUBROUTINE  INTERACTS  WITH  THE  FD179X-02 *
; DURING WRITE SECTOR OPERATIONS. THIS SECTION        *
; INITIATES THE DISK TRANSFER, SERVICES THE CONTROLLER*
; CHIP, AND TERMINATES THE OPERATION. ERROR DETECTION *
; IS IMPLEMENTED.                                     *
;*****************************************************/
static uint8 DCM_WriteSector(uint8 drive, uint8 track, uint8 sector, uint8 *buffer)
{
    uint32 offset;

    jade_info->curdrv = drive;

    cb[CB_TRK] = track;
    cb[CB_SEC] = sector;

    /*
    ** Make sure drive is ready
    */
    if (jade_info->uptr[drive]->fileref == NULL) {
        return CS_DNR;
    }

    /*
    ** Check if drive is write protected
    */
    if (jade_info->uptr[drive]->flags & UNIT_JADE_WPROTECT) {
        return CS_WRP;
    }

    offset = calculate_jade_sec_offset(track, sector, jade_info->dt[drive].flg);

    if (sim_fseek(jade_info->uptr[drive]->fileref, offset, SEEK_SET) != 0) {
        sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME "%d: WRSEC sim_fseek error.\n", drive);
        return CS_RNF;
    }

    if (sim_fwrite(buffer, 1, JADE_SECTOR_SIZE, jade_info->uptr[drive]->fileref) != JADE_SECTOR_SIZE) {
        sim_debug(ERROR_MSG, &jade_dev, JADE_SNAME "%d: WRSEC sim_fwrite error.\n", drive);
        return CS_CRC;
    }

    showsector(drive, FALSE, buffer);

    return CS_NOE;
}

/******************************************************
; WR.TRK IS THE SUBROUTINE WHICH INITIATES A FORMAT   *
; TRACK COMMAND (WRITE-TRACK 179X-02 TYPE 3).  THE    *
; FORMATTING BYTE STREAM IS PROVIDED BY A PROGRAM     *
; WHICH MUST BE PRESENT IN THE FORMAT BUFFER.         *
;*****************************************************/
static uint8 DCM_Format(uint8 drive, uint8 track)
{
    uint8 sbuf[JADE_SECTOR_SIZE];
    uint8 sector;
    uint8 sts = 0;

    memset(sbuf, 0xe5, sizeof(sbuf));

    jade_info->dt[drive].flg = 0;
    
    /*
    ** Are we formatting double density?
    */
    jade_info->dt[drive].flg |= (fmt[FMT_DEN] == 'D') ? DF_DTD : 0;

    /*
    ** If track 1 is being formatted 50 sectors, set DF_T1D flag
    */
    jade_info->dt[drive].flg |= (fmt[FMT_SEC] == 50) ? DF_T1D : 0;

    for (sector=1; sector<=fmt[FMT_SEC]; sector++) {
        sts = DCM_WriteSector(drive, track, sector, sbuf);
    }

    return(sts);
}

static int32 jadeprom(int32 Addr, int32 rw, int32 Data)
{
    return(jade_prom[Addr & JADE_PROM_MASK]);
}

static int32 jademem(int32 Addr, int32 rw, int32 Data)
{
    int32 offset = (Addr & JADE_BANK_MASK) + (jade_info->mem_bank * JADE_BANK_SIZE);

    /*
    ** Need to select bank
    */
    if (rw) {
       jade_mem[offset] = Data & 0xff;
    }

    return(jade_mem[offset]);
}

