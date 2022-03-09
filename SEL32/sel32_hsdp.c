/* sel32_hsdp.c: SEL-32 8064 High Speed Disk Processor

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "sel32_defs.h"

/* uncomment to use fast sim_activate times when running UTX */
/* UTX gets an ioi error for dm0801 if slow times are used */
/* dm0801 is not even a valid unit number for UDP controller */
#define FAST_FOR_UTX

#if NUM_DEVS_HSDP > 0

#define UNIT_HSDP   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

/* useful conversions */
/* Fill STAR value from cyl, trk, sec data */
#define CHS2STAR(c,h,s)         (((c<<16) & LMASK)|((h<<8) & 0xff00)|(s & 0xff))
/* convert STAR value to number of sectors */
#define STAR2SEC(star,spt,spc)  ((star&0xff)+(((star>>8)&0xff)*spt)+(((star>>16)&0xffff)*spc))
/* convert STAR value to number of heads or tracks */
#define STAR2TRK(star,tpc)      (((star>>16)&0xffff)*tpc+((star>>8)&0x0ff))
/* convert STAR value to number of cylinders */
#define STAR2CYL(star)          ((star>>16)&RMASK)
/* convert byte value to number of sectors mod sector size */
#define BYTES2SEC(bytes,ssize)  (((bytes) + (ssize-1)) >> 10)
/* get sectors per track for specified type */
#define SPT(type)               (hsdp_type[type].spt)
/* get sectors per cylinder for specified type */
#define SPC(type)               (hsdp_type[type].spt*hsdp_type[type].nhds)
/* get number of tracks for specified type */
#define TRK(type)               (hsdp_type[type].cyl*hsdp_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (hsdp_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (hsdp_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (hsdp_type[type].ssiz*4)
/* get disk capacity in bytes for specified type */
#define CAPB(type)              (CAP(type)*SSB(type))
/* get disk geometry as STAR value for specified type */
#define GEOM(type)              (CHS2STAR(CYL(type),HDS(type),SPT(type)))

/* INCH command information */
/*
WD 0 - Data address
WD 1 - Flags - 0 -36 byte count

Data - 224 word INCH buffer address (SST)
WD 1 Drive 0 Attribute register
WD 2 Drive 1 Attribute register
WD 3 Drive 2 Attribute register
WD 4 Drive 3 Attribute register
WD 5 Drive 4 Attribute register
WD 6 Drive 5 Attribute register
WD 7 Drive 6 Attribute register
WD 8 Drive 7 Attribute register

Drive attribute register bit assignments (DATR)
Byte 0 bits 0-7 - Flags
        Drive type
        bits 0&1 - 00=Undefined
                 - 01=MHD
                 - 10=Undefined
                 - 11=Undefined
        Optimized seeks
        bit  2&3 - 00=Optimize seeks and post IOCL status out of order
                 - 01=Optimize seeks and post IOCL status in order
                 - 10=Do not optimize seeks
                 - 11=Do not optimize seeks
        bit  4   - 0=Drive is present
                 - 1=Drive not present
        bit  5   - 0=Not Dual Port
                 - 1=Dual Port
        Sector Size
        bit  6&7 - 00=768 bytes
                   01=1024 bytes
                   10=2048 bytes
                   11=Unassigned
Byte 1 bits 8-15 - Sectors per track
Byte 2 bits 16-23 - Number of head
Byte 3 bits 24-31 - Reserved (zero)
*/

/*
Drive status bit assignments (DSR)
Byte 0 bits 0-7
        bit 00 - Seek End
            01 - Unit selected
            02 - Sector pulse counter bit 0
            03 - Sector pulse counter bit 1
            04 - Sector pulse counter bit 2
            05 - Sector pulse counter bit 3
            06 - Sector pulse counter bit 4
            07 - Sector pulse counter bit 5
Byte 1 bits 7-15
        bit 08 - Disc drive fault
            09 - Seek error
            10 - On cylinder
            11 - Unit Ready
            12 - Write protected
            13 - Drive busy
            14 - Reserved (zero)
            15 - Reserved (zero)
*/

/* Subchannel Target Register (STAR) */
/* byte 0 - Cylinder MS byte */
/* byte 1 - Cylinder LS byte */
/* byte 2 - Track count */
/* byte 3 - Sector count */

/* Mode Register (MODE) */
/* Bits 0-7 - bit assignments */

/* Bits 0-3 are for data recovery operations which can be */
/*      tried by the software */
/*  0 - Servo offset 0/1=disable/enable */
/*  1 - Servo offset polarity 0/1=positive/negative */
/*  2 - Data strobe offset 0/1=disable/enable */
/*  3 - Data strobe offset polarity 0/1=positive/negative */
/* Bit 4 enables sector ECC data to be read or written for */
/*     diagnostic commands */
/*  4 - Read/write ECC data 0/1=disable/enable */
/* Bit 5 controls the transfer of an ID during express bus */
/*     read commands */
/*  5 - Express bus ID 0/1=enable/disable */
/* Bit 6 enables auto-retry in accordance with the firmware */
/*     auto-retry algorithms */
/*  6 - Auto retry 0/1=enable/disable */
/* Bit 7 disables the subchannel from interacting with the */
/*     disc drive and is for diagnostic testing only */
/*  7 - Diagnostic mode 0/1=disable/enable */

/* Sense Buffer Register (SBR) */
/* The SBR contains subchannel error status information */
/* Byte 0
 * bit 00 Command rejected (CR)
 *     01 Intervention requested (IR)
 *     02 Unit select error (USEL)
 *     03 Equipment check (EQCK)
 *     04 Reserved (zero)
 *     05 Reserved (zero)
 *     06 Disc format error (DFER)
 *     07 Defective track encountered (DETR)
 * Byte 1
 * bit 08 Reserved (zero)
 *     09 At alternate track (AATT)
 *     10 Write protect error (WPER)
 *     11 Write lock error (WRL)
 *     12 Mode check (MOCK)
 *     13 Invalid address (INAD)
 *     14 Release fault (RELF)
 *     15 Chaining error (CHER)
 * Byte 2
 * bit 16 Revolution lost (REVL)
 *     17 Disc addressing or seek error
 *     18 Reserved (zero)
 *     19 Reserved (zero)
 *     20 ECC error in data (ECCD)
 *     21 Reserved (zero)
 *     22 Reserved (zero)
 *     23 Uncorrectable ECC error (UECC)
 * Byte 3
 * Not used
 *     */

/* 224 word Subchannel Storage Buffer (SST) */
/* 128 words reserved */
/*  66 words (33 DW) of program status queue (PSQ) */
/*   8 words of retry counters (1/channel) */
/*  22 words reserved */

/************************************/
/* track label definations 34 bytes */
    /* for track 0, write max cyl/head/sec values in 0-3 */
    /* otherwise write current values */
/*
0   short lcyl;         cylinder
2   char ltkn;          head or track number
3   char lid;           track label id (0xff means last track)
4   char lflg1;         track status flags
        bit 0           good trk
            1           alternate trk
            2           spare trk
            3           reserved trk
            4           defective trk
            5           last track
          6-7           n/u = 0
5   char lflg2;
        bit 0           write lock
            1           write protected
          2-7           n/u = 0
6   short lspar1;       n/u = 0
8   short lspar2;       n/u = 0
10  short ldef1;        defect #1 sec and byte position
    * for track 0 write DMAP
    * write sector number of cyl-4, hds-2, sec 0 value in 12-15
    * otherwise write current values
12  short ldef2;        defect #2 sec and byte position
14  short ldef3;        defect #3 sec and byte position
    * for track 0 write UMAP which is DMAP - 2 * SPT
    * write sector number of cyl-4, hds-4, sec 0 value in 16-19
    * otherwise write current values
16  short ladef1;       defect #1 abs position
18  short ladef2;       defect #2 abs position
20  short ladef3;       defect #3 abs position
22  short laltcyl;      alternate cylinder number or return cyl num
24  char lalttk;        alrernate track number or return track num
25  char ldscnt;        data sector count 16/20
26  char ldatrflg;      device attributes
        bit 0           n/u
            1           disk is mhd
            2           n/u
            3           n/u
            4           n/u
            5           dual ported
            6/7         00 768 bytes/blk
                        01 1024 bytes/blk
                        10 2048 bytes/blk
27  char ldatrscnt;     sectors per track (again)
28  char ldatrmhdc;     MHD head count
29  char ldatrfhdc;     FHD head count
30  uint32 lcrc;        Label CRC-32 value
 */

/*************************************/
/* sector label definations 34 bytes */
/*
0   short lcyl;         cylinder number
2   char lhd;           head number
3   char lsec;          sec # 0-15 or 0-19 for 16/20 format
4   char lflg1;         track/sector status flags
        bit 0           good sec
            1           alternate sec
            2           spare sec
            3           reserved sec
            4           defective sec
            5           last sec
          6-7           n/u = 0
5   char lflg2;
        bit 0           write lock
            1           write protected
          2-7           n/u = 0
6   short lspar1;       n/u = 0
8   short lspar2;       n/u = 0
10  short ldef1;        defect #1 sec and byte position
12  short ldef2;        defect #2 sec and byte position
14  short ldef3;        defect #3 sec and byte position
    * for track 0 write UMAP which is DMAP - 2 * SPT
    * write sector number of cyl-4, hds-4, sec 0 value in 16-19
    * otherwise write zeros
16  short lspar3;       n/u = 0
18  short lspar4;       n/u = 0
20  short lspar5;       n/u = 0
22  short laltcyl;      alternate cylinder number or return cyl num
24  char lalttk;        alrernate track number or return track num
25  char ldscnt;        data sector count 16/20
26  char ldatrflg;      device attributes
        bit 0           n/u
            1           disk is mhd
            2           n/u
            3           n/u
            4           n/u
            5           dual ported
            6/7         00 768 bytes/blk
                        01 1024 bytes/blk
                        10 2048 bytes/blk
27  char ldatrscnt;     sectors per track (again)
28  char ldatrmhdc;     MHD head count
29  char ldatrfhdc;     FHD head count
30  uint32 lcrc;        Label CRC-32 value
 */

/* track label / sector label definations */
/*
 0  short lcyl;         cylinder
 2  char ltkn;          track
 3  char lid;           sector id
 4  char lflg1;         track/sector status flags
        bit 0           good
            1           alternate
            2           spare
            3           reserved
            4           flaw
            5           last track
            6           start of alternate
 5  char lflg2;
 6  short lspar1;
 8  short lspar2;
10  short ldef1;
12  int ldeallp;        DMAP block number trk0
16  int lumapp;         UMAP block number sec1
20  short ladef3;
22  short laltcyl;
24  char lalttk;        sectors per track
25  char ldscnt;        number of heads
26  char ldatrflg;      device attributes
        bit 0           n/u
            1           disk is mhd
            2           n/u
            3           n/u
            4           n/u
            5           dual ported
            6/7         00 768 bytes/blk
                        01 1024 bytes/blk
                        10 2048 bytes/blk
27  char ldatrscnt;     sectors per track (again)
28  char ldatrmhdc;     MHD head count
29  char ldatrfhdc;     FHD head count
 */

#define CMD     u3
/* u3 */
/* in u3 is device command code and status */
#define DSK_CMDMSK      0x00ff                  /* Command being run */
#define DSK_STAR        0x0100                  /* STAR value in u4 */
#define DSK_WAITING     0x0200                  /* Doing NOP wait */
#define DSK_READDONE    0x0400                  /* Read finished, end channel */
#define DSK_ENDDSK      0x0800                  /* Sensed end of disk */
#define DSK_SEEKING     0x1000                  /* Disk is currently seeking */
#define DSK_READING     0x2000                  /* Disk is reading data */
#define DSK_WRITING     0x4000                  /* Disk is writing data */
#define DSK_BUSY        0x8000                  /* Disk is busy */
/* commands */
#define DSK_INCH        0x00                    /* Initialize channel */
#define DSK_INCH2       0xF0                    /* Fake while in srv Initialize channel */
#define DSK_WD          0x01                    /* Write data */
#define DSK_RD          0x02                    /* Read data */
#define DSK_NOP         0x03                    /* No operation */
#define DSK_SNS         0x04                    /* Sense */
#define DSK_SKC         0x07                    /* Seek cylinder, track, sector */
#define DSK_TIC         0x08                    /* Transfer in channel */
#define DSK_FMT         0x0B                    /* Format track */
#define DSK_RE          0x12                    /* Read express bus with ECC */
//#define DSK_LPL         0x13                    /* Lock protected label */
#define DSK_LMR         0x1F                    /* Load mode register */
#define DSK_RENO        0x22                    /* Read express bus with no ECC */
#define DSK_RES         0x23                    /* Reserve */
#define DSK_WSL         0x31                    /* Write sector label */
#define DSK_RSL         0x32                    /* Read sector label */
#define DSK_REL         0x33                    /* Release */
#define DSK_XEZ         0x37                    /* Rezero */
#define DSK_WTF         0x41                    /* Write track format */
#define DSK_RVL         0x42                    /* Read vendor label */
#define DSK_POR         0x43                    /* Priority Override */
#define DSK_IHA         0x47                    /* Increment head address */
//#define DSK_SRM         0x4F                    /* Set reserve track mode */
#define DSK_WTL         0x51                    /* Write track label */
#define DSK_RTL         0x52                    /* Read track label */
//#define DSK_XRM         0x5F                    /* Reset reserve track mode */
#define DSK_RAP         0xA2                    /* Read angular position */
//#define DSK_TESS        0xAB                  /* Test STAR (subchannel target address register) */
#define DSK_REC         0xB2                    /* Read ECC correction mask */
#define DSK_INC         0xFF                    /* Initialize Controller */

#define DAI    u4
/* u4 holds the current disk attribute value from the INCH command */
/* for the current drive. */
/* Holds the current cylinder, head(track), sector */

/* this attribute information is provided by the INCH command */
/* for each device and is saved.  It is reconstructed from */
/* the hsdp_t structure data for the assigned disk */
/*
bits 0-7 - Flags
        bits 0&1 - 00=Reserved, 01=MHD, 10=FHD, 11=MHD with FHD option
        bit  2   - 1=Cartridge module drive
        bit  3   - 0=Reserved
        bit  4   - 1=Drive not present
        bit  5   - 1=Dual Port
        bit  6   - 0=Reserved  00 768 byte sec
        bit  7   - 0=Reserved  01 1024 byte sec
bits 8-15 - sector count (sectors per track)(F16=16, F20=20)
bits 16-23 - MHD Head count (number of heads on MHD)
bits 24-31 - FHD head count (number of heads on FHD or
             number head on FHD option of mini-module)
*/

#define SNS     u5
/* u5 */
/* Sense byte 0  - mode register */
#define SNS_DROFF       0x80000000              /* Drive Carriage will be offset */
#define SNS_TRKOFF      0x40000000              /* Track offset: 0=positive, 1=negative */
#define SNS_RDTMOFF     0x20000000              /* Read timing offset = 1 */
#define SNS_RDSTRBT     0x10000000              /* Read strobe timing: 1=positive, 0=negative */
#define SNS_DIAGMOD     0x08000000              /* Diagnostic Mode ECC read/write 0/1=disab;e/enable */
#define SNS_XPBUS       0x04000000              /* Express Bus ID 0/1=enable/disable */
#define SNS_AUTORT      0x02000000              /* Auto retry 0/1=disable/enable */
#define SNS_DIAG        0x01000000              /* Diagnostic mode 0/1=disable/enable */

/* Sense byte 1 */
#define SNS_CMDREJ      0x800000                /* Command reject */
#define SNS_INTVENT     0x400000                /* Unit intervention required */
#define SNS_USELE       0x200000                /* Unit Select Error */
#define SNS_EQUCHK      0x100000                /* Equipment check */
#define SNS_RES2        0x080000                /* Reserved */
#define SNS_RES3        0x040000                /* Reserved */
#define SNS_DSKFERR     0x020000                /* Disk format error */
#define SNS_DEFTRK      0x010000                /* Defective track encountered */

/* Sense byte 2 */
#define SNS_RES4        0x8000                  /* Reserved */
#define SNS_AATT        0x4000                  /* At Alternate track */
#define SNS_WPER        0x2000                  /* Write protection error */
#define SNS_WRL         0x1000                  /* Write lock error */
#define SNS_MOCK        0x0800                  /* Mode check */
#define SNS_INAD        0x0400                  /* Invalid memory address */
#define SNS_RELF        0x0200                  /* Release fault */
#define SNS_CHER        0x0100                  /* Chaining error */

/* Sense byte 3 */
#define SNS_REVL        0x80                    /* Revolution lost */
#define SNS_DADE        0x40                    /* Disc addressing or seek error */
#define SNS_RES5        0x20                    /* Reserved */
#define SNS_RES6        0x10                    /* Reserved */
#define SNS_ECCD        0x08                    /* ECC error in data */
#define SNS_RES7        0x04                    /* Reserved */
#define SNS_RES8        0x02                    /* Reserved */
#define SNS_UESS        0x01                    /* Uncorrectable ECC error */

#define SNS2    us9
/* us9 */
/* us9 holds bytes 4 & 5 of the status for the drive */

/* Sense byte 4 */
#define SNS_SEND        0x8000                  /* Seek End */
#define SNS_USEL        0x4000                  /* Unit Selected */
#define SNS_SPC0        0x2000                  /* Sector Pulse Count B0 */
#define SNS_SPC1        0x1000                  /* Sector Pulse Count B1 */
#define SNS_SPC2        0x0800                  /* Sector Pulse Count B2 */
#define SNS_SPC3        0x0400                  /* Sector Pulse Count B3 */
#define SNS_SPC4        0x0200                  /* Sector Pulse Count B4 */
#define SNS_SPC5        0x0100                  /* Sector Pulse Count B5 */

/* Sense byte 5 */
#define SNS_FLT         0x80                    /* Disk Drive fault */
#define SNS_SKER        0x40                    /* Seek error */
#define SNS_ONC         0x20                    /* On Cylinder */
#define SNS_UNR         0x10                    /* Unit Ready */
#define SNS_WRP         0x08                    /* Write Protected */
#define SNS_BUSY        0x04                    /* Drive is busy */
#define SNS_NU1         0x02                    /* Spare 1 */
#define SNS_NU2         0x01                    /* Spare 2 */

#define CHS     u6
/* u6 - sector target address register (STAR) */
/* Holds the current cylinder, head(track), sector */
#define DISK_CYL        0xFFFF0000              /* cylinder mask */
#define DISK_TRACK      0x0000FF00              /* track mask */
#define DISK_SECTOR     0x000000FF              /* sector mask */

/* Not Used     up7 */

#define LSC     us10
/* us10 */
/* us10 byte 0 unused */
/* us10 byte 1 holds logical sector count from track 0 byte 25 */

static uint8   obuf[1024], bbuf[1024];
static uint32  decc[512] = {0};

/* disk definition structure */
struct hsdp_t
{
    const char  *name;                          /* Device ID Name */
    uint16      nhds;                           /* Number of heads */
    uint16      ssiz;                           /* sector size in words */
    uint16      spt;                            /* # sectors per track(head) */
    uint16      ucyl;                           /* Number of cylinders used */
    uint16      cyl;                            /* Number of cylinders on disk */
    uint8       type;                           /* Device type code */
    /* bit 1 mhd */
    /* bits 6/7 = 0 768 byte blk */             /* not used on UDP/DPII */
    /*          = 1 1024 byte blk */            /* not used on UDP/DPII */
}

hsdp_type[] =
{
    /* Class F Disc Devices */
    /* For MPX */
    {"MH040",   5, 192, 20, 407, 411, 0x40},    /* 0  411   40M XXXX */
    {"MH080",   5, 192, 20, 819, 823, 0x40},    /* 1  823   80M 8138 */
    {"MH160",  10, 192, 20, 819, 823, 0x40},    /* 2  823  160M 8148 */
    {"MH300",  19, 192, 20, 819, 823, 0x40},    /* 3  823  300M 9346 */
    {"MH337",  10, 192, 45, 819, 823, 0x40},    /* 4  823  337M 8887 DP337 */
    {"MH600",  40, 192, 20, 839, 843, 0x40},    /* 5  843  600M 8155 */
    {"MH689",  16, 192, 54, 861, 865, 0x40},    /* 6  823  674M 8888 DP689 */
    /* For UTX */
    {"9342",   5, 256, 16, 819, 823, 0x41},     /* 7  823   80M 9342 MH080 */
    {"8148",  10, 256, 16, 819, 823, 0x41},     /* 8  823  160M 8146 MH160 */
    {"9346",  19, 256, 16, 819, 823, 0x41},     /* 9  823  300M 9344 MH300 */
    {"8858",  24, 256, 16, 707, 711, 0x41},     /* 10 711  340M 8858 DC340 */
    {"8887",  10, 256, 35, 819, 823, 0x41},     /* 11 823  337M 8887 DP337 */
    {"8155",  40, 256, 16, 839, 843, 0x41},     /* 12 843  600M 8155 MH600 */
    {"8888",  16, 256, 43, 861, 865, 0x41},     /* 13 823  674M 8888 DP689 */
    {NULL}
};

t_stat  hsdp_preio(UNIT *uptr, uint16 chan) ;
t_stat  hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
t_stat  hsdp_haltio(UNIT *uptr);
t_stat  hsdp_rsctl(UNIT *uptr);
t_stat  hsdp_iocl(CHANP *chp, int32 tic_ok);
t_stat  hsdp_srv(UNIT *);
t_stat  hsdp_boot(int32 unitnum, DEVICE *);
void    hsdp_ini(UNIT *, t_bool);
t_stat  hsdp_reset(DEVICE *);
t_stat  hsdp_attach(UNIT *, CONST char *);
t_stat  hsdp_detach(UNIT *);
t_stat  hsdp_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat  hsdp_get_type(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat  hsdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char  *hsdp_description (DEVICE *dptr);
extern  uint32  inbusy;
extern  uint32  outbusy;
extern  uint32  readfull(CHANP *chp, uint32 maddr, uint32 *word);
extern  int     irq_pend;                       /* go scan for pending int or I/O */
extern  UNIT    itm_unit;
extern  uint32  PSD[];                          /* PSD */
extern  uint32  cont_chan(uint16 chsa);

/* channel program information */
CHANP   dpa_chp[NUM_UNITS_HSDP] = {0};
/* IOCL queue */
IOCLQ   dpa_ioclq[NUM_UNITS_HSDP] = {0};

#define TRK_CACHE 10
/* track label queue */
struct _trk_data
{
    int32   age;
    uint32  track;
    uint8   label[30];
};

struct _trk_label
{
    struct  _trk_data   tkl[TRK_CACHE];
};

static struct _trk_label tkl_label[NUM_UNITS_HSDP] = {0};

MTAB            hsdp_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &hsdp_set_type, &hsdp_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0},
};

UNIT            dpa_unit[] = {
/* SET_TYPE(10) 8887 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x800)},  /* 0 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x802)},  /* 1 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x804)},  /* 2 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x806)},  /* 3 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x808)},  /* 4 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x80A)},  /* 5 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x80C)},  /* 6 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(10), 0), 0, UNIT_ADDR(0x80E)},  /* 7 */
};

DIB             dpa_dib = {
    hsdp_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    hsdp_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    hsdp_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    hsdp_haltio,    /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    hsdp_rsctl,     /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    NULL,           /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    hsdp_iocl,      /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    hsdp_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    dpa_unit,       /* UNIT* units */                           /* Pointer to units structure */
    dpa_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    dpa_ioclq,      /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_HSDP, /* uint8 numunits */                        /* number of units defined */
    0x0F,           /* uint8 mask */                            /* 8 devices - device mask */
    0x0800,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          dpa_dev = {
    "DPA", dpa_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    /* ctxt is the DIB pointer */
    &dpa_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &hsdp_help, NULL, NULL, &hsdp_description
};

#if NUM_DEVS_HSDP > 1
/* channel program information */
CHANP   dpb_chp[NUM_UNITS_HSDP] = {0};
/* IOCL queue */
IOCPQ   dpb_ioclq[NUM_UNITS_HSDP] = {0};

UNIT            dpb_unit[] = {
/* SET_TYPE(3) DM300 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC00)},  /* 0 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC02)},  /* 1 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC04)},  /* 2 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC06)},  /* 3 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC08)},  /* 4 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0A)},  /* 5 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0C)},  /* 6 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0E)},  /* 7 */
};


DIB             dpb_dib = {
    hsdp_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    hsdp_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    hsdp_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    hsdp_haltio,    /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    hsdp_rsctl,     /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    NULL,           /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    hsdp_iocl,      /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    hsdp_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    dpb_unit,       /* UNIT* units */                           /* Pointer to units structure */
    dpb_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    dpb_ioclq,      /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_HSDP, /* uint8 numunits */                        /* number of units defined */
    0x0F,           /* uint8 mask */                            /* 8 devices - device mask */
    0x0C00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          dpb_dev = {
    "DPB", dpb_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    /* ctxt is the DIB pointer */
    &dpb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &hsdp_help, NULL, NULL, &hsdp_description
};
#endif

uint32 dple_ecc32(uint8 *str, int32 len)
{
    int i, j;
    uint32 ch, ecc = 0;
    uint32 pmask = 0x7e11f439;                  /* SEL LE poly mask */

    ecc = (~ecc & MASK32);                      /* initialize ecc to all bits (~0) */
    for (j=0; j<len; j++) {
        ch = (uint32)(str[j]) & 0xff;           /* get a char from string */
        for (i=0; i<8; i++) {
            if ((ecc ^ ch) & BIT31) {           /* bit set? */
                ecc >>= 1;                      /* just shift out the bit */
                ecc ^= pmask;                   /* eor with poly mask */
            } else
                ecc >>= 1;                      /* just shift out the bit */
            ch >>= 1;                           /* next bit */
        }
    }
    return (~ecc & MASK32);                     /* return ecc value */
}

uint32 dpbe_ecc32(uint8 *str, int32 len)
{
    int i, j;
    uint32 ch, ecc = 0;
    uint32 pmask = 0x9C2F887E;                  /* SEL BE poly mask */

    ecc = (~ecc & MASK32);                      /* initialize ecc to all bits (~0) */
    for (j=0; j<len; j++) {
        ch = (uint32)(str[j] << 24) & 0xff000000; /* get a char from string */
        for (i=0; i<8; i++) {
            if ((ecc ^ ch) & BIT0) {            /* bit set? */
                ecc <<= 1;                      /* just shift out the bit */
                ecc ^= pmask;                   /* eor with poly mask */
            } else
                ecc <<= 1;                      /* just shift out the bit */
            ch <<= 1;                           /* next bit */
        }
    }
    return (~ecc & MASK32);                     /* return ecc value */
}

/* convert sector disk address to star values (c,h,s) */
uint32 hsdpsec2star(uint32 daddr, int type)
{
    int32 sec = daddr % hsdp_type[type].spt;    /* get sector value */
    int32 spc = hsdp_type[type].nhds * hsdp_type[type].spt; /* sec per cyl */
    int32 cyl = daddr / spc;                    /* cylinders */
    int32 hds = (daddr % spc) / hsdp_type[type].spt;    /* heads */ 

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* read alternate track label and return new STAR */
uint32 get_dpatrk(UNIT *uptr, uint32 star, uint8 buf[])
{
    uint32  nstar, tstart, offset;
    uint32  sec=0, trk=0, cyl=0;
    int     type = GET_TYPE(uptr->flags);
    DEVICE  *dptr = get_dev(uptr);
    int     unit = (uptr - dptr->units);        /* get the UNIT number */
    int     len, i, cn, found = -1;

    /* zero the Track Label Buffer */
    for (i = 0; i < 30; i++)
        buf[i] = 0;

    /* get file offset in sectors */
    tstart = STAR2SEC(star, SPT(type), SPC(type));
    /* convert sector number back to chs value to sync disk for diags */
    nstar = hsdpsec2star(tstart, type);

    cyl = (nstar >> 16) & 0xffff;               /* get the cylinder */
    trk = (nstar >> 8) & 0xff;                  /* get the track */
    sec = nstar & 0xff;                         /* save sec if any */

    /* get track number */
    tstart = (cyl * HDS(type)) + trk;
    sim_debug(DEBUG_EXP, dptr, "get_dpatrk RTL cyl %4x(%d) trk %x ec# %06x\n",
        cyl, cyl, trk, tstart);

    /* calc offset in file to track label */
    offset = CAPB(type) + (tstart * 30);

    /* see if track label is in cache */
    for (cn=0; cn<TRK_CACHE; cn++) {
        if (offset == tkl_label[unit].tkl[cn].track) {
            /* we found it, copy data to buf */
            for (i=0; i<30; i++)
                buf[i] = tkl_label[unit].tkl[cn].label[i];
            found = cn;
            tkl_label[unit].tkl[cn].age++;
            sim_debug(DEBUG_EXP, dptr, "get_dpatrk found in Cache to %06x\n", offset);
            break;
        }
    }

    /* see if found in cache */
    if (found == -1) {
        /* file offset in bytes */
        sim_debug(DEBUG_EXP, dptr, "get_dpatrk RTL SEEK on seek to %06x\n", offset);

        /* seek to the location where we will r/w track label */
        if ((sim_fseek(uptr->fileref, offset, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "get_dpatrk RTL, Error on seek to %04x\n", offset);
            return 0;
        }

        /* read in a track label from disk */
        if ((len=sim_fread(buf, 1, 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "get_dpatrk Error %08x on read %04x of diskfile cyl %04x hds %02x sec 00\n",
                len, 30, cyl, trk);
            return 0;
        }
    }

    /* now write track label data to log */
    sim_debug(DEBUG_CMD, dptr, "Dpatrk %08x label", nstar);
    for (i = 0; i < 30; i++) {
        if (i == 16)
            sim_debug(DEBUG_CMD, dptr, "\nDpatrl %08x label", nstar);
         sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
    }
    sim_debug(DEBUG_CMD, dptr, "\n");

    if (buf[4] == 0x08) {                       /* see if defective track */
        uptr->SNS |= SNS_DEFTRK;                /* flag as defective */
        tstart = nstar;                         /* save orginal track */
        /* get the alternate track address */
        cyl = (buf[22] << 8) | buf[23];         /* get the cylinder */
        trk = buf[24];                          /* get the track */
        nstar = CHS2STAR(cyl, trk, sec);
        sim_debug(DEBUG_CMD, dptr,
            "Track %08x is defective, new track %08x\n", tstart, nstar);
    }
    /* see if we had it in our cache */
    if (found == -1) {
        /* not in our cache, save the new track label */
        int32 na = 0;
        for (cn=0; cn<TRK_CACHE; cn++) {
            /* see if in use yet */
            if (tkl_label[unit].tkl[cn].age == 0) {
                na = cn;                        /* use this one */
                break;
            }
            if (tkl_label[unit].tkl[cn].age > na)
                continue;                       /* older */
            /* this is less used, so replace it */
            na = cn;
        }
        /* use na entry */
        for (i=0; i<30; i++)
            tkl_label[unit].tkl[na].label[i] = buf[i];
        tkl_label[unit].tkl[na].age = 1;
        tkl_label[unit].tkl[cn].track = offset;
    }
    return nstar;                               /* return track address */
}

/* start a disk operation */
t_stat hsdp_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);    /* get the UNIT number */
    DIB*        dibp = (DIB *)dptr->ctxt;       /* get the DIB pointer */
    int32       cnt;

    sim_debug(DEBUG_CMD, dptr, "hsdp_preio CMD %08x unit %02x\n", uptr->CMD, unit);
    if ((cnt = IOCLQ_Num(&dibp->ioclq_ptr[unit])) >= (IOCLQ_SIZE)) {
        sim_debug(DEBUG_CMD, dptr, "hsdp_preio CMD %08x unit %02x IOCLQ cnt %02x Full\n",
            uptr->CMD, unit, cnt);
        return SNS_BSY;                         /* IOCLQ is full, return busy */
    }
    if ((uptr->CMD & 0xff) != 0) {              /* just return if busy */
        sim_debug(DEBUG_CMD, dptr, "hsdp_preio CMD %08x unit %02x IOCLQ cnt %02x Busy\n",
            uptr->CMD, unit, cnt);
        return SNS_SMS;                         /* busy, but IOCLQ is not full */
    }

    sim_debug(DEBUG_CMD, dptr, "hsdp_preio unit %02x chsa %04x OK not busy\n", unit, chsa);
    return SCPE_OK;                             /* not busy and IOCLQ not full */
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
t_stat  hsdp_iocl(CHANP *chp, int32 tic_ok)
{
    uint32      word1 = 0;
    uint32      word2 = 0;
    int32       docmd = 0;
    UNIT        *uptr = chp->unitptr;           /* get the unit ptr */
    uint16      chan = get_chan(chp->chan_dev); /* our channel */
    uint16      chsa = chp->chan_dev;           /* our chan/sa */
    uint16      devstat = 0;
    DEVICE      *dptr = get_dev(uptr);

    /* check for valid iocd address if 1st iocd */
    if (chp->chan_info & INFO_SIOCD) {          /* see if 1st IOCD in channel prog */
        if (chp->chan_caw & 0x3) {              /* must be word bounded */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_iocl iocd bad address chsa %02x caw %06x\n",
                chsa, chp->chan_caw);
            chp->ccw_addr = chp->chan_caw;      /* set the bad iocl address */
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid iocd addr */
            uptr->SNS |= SNS_INAD;              /* invalid address status */
            return 1;                           /* error return */
        }
    }
loop:
    sim_debug(DEBUG_EXP, dptr,
        "hsdp_iocl @%06x entry chan_status[%04x] %04x SNS %08x\n",
        chp->chan_caw, chan, chp->chan_status, uptr->SNS);

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR) {      /* check channel error status */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl ERROR1 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* return error */
    }

    /* Read in first CCW */
    if (readfull(chp, chp->chan_caw, &word1) != 0) { /* read word1 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl ERROR2 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Read in second CCW */
    if (readfull(chp, chp->chan_caw+4, &word2) != 0) { /* read word2 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl ERROR3 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_iocl @%06x read ccw chan %02x IOCD wd 1 %08x wd 2 %08x SNS %08x\n",
        chp->chan_caw, chan, word1, word2, uptr->SNS);

    chp->chan_caw = (chp->chan_caw & 0xfffffc) + 8; /* point to next IOCD */

    /* Check if we had data chaining in previous iocd */
    /* if we did, use previous cmd value */
    if (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
       (chp->ccw_flags & FLAG_DC)) {            /* last IOCD have DC set? */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_iocl @%06x DO DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
    } else
        chp->ccw_cmd = (word1 >> 24) & 0xff;    /* set new command from IOCD wd 1 */

    if (!MEM_ADDR_OK(word1 & MASK24)) {         /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;        /* bad, program check */
        uptr->SNS |= SNS_INAD;                  /* invalid address status */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl bad IOCD1 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    chp->ccw_count = word2 & 0xffff;            /* get 16 bit byte count from IOCD WD 2 */

    /* validate the commands for the disk */
    switch (chp->ccw_cmd) {
    case DSK_WD: case DSK_RD: case DSK_INCH: case DSK_NOP: case DSK_INC:
    case DSK_SKC: case DSK_XEZ: case DSK_LMR: case DSK_WSL: case DSK_RSL:
    case DSK_IHA: case DSK_WTL: case DSK_RTL: case DSK_RAP: case DSK_WTF:  
    case DSK_FMT: case DSK_RE: case DSK_RENO: case DSK_REL: case DSK_RES:
    case DSK_RVL: case DSK_POR: case DSK_REC: case DSK_TIC:
    case DSK_SNS:   
        break;
    default:
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid cmd */
        uptr->SNS |= SNS_CMDREJ;                /* cmd rejected */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl bad cmd %02x chan_status[%04x] %04x SNS %08x\n",
            chp->ccw_cmd, chan, chp->chan_status, uptr->SNS);
        return 1;                               /* error return */
    }

    if (chp->chan_info & INFO_SIOCD) {          /* see if 1st IOCD in channel prog */
        /* 1st command can not be a TIC */
        if (chp->ccw_cmd == CMD_TIC) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid tic */
            uptr->SNS |= SNS_CMDREJ;            /* cmd rejected status */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_iocl TIC bad cmd chan_status[%04x] %04x\n",
                chan, chp->chan_status);
            return 1;                           /* error return */
        }
    }

    /* TIC can't follow TIC or be first in command chain */
    /* diags send bad commands for testing.  Use all of op */
    if (chp->ccw_cmd == CMD_TIC) {
        if (tic_ok) {
            if (((word1 & MASK24) == 0) || (word1 & 0x3)) {
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_iocl tic cmd bad address chan %02x tic caw %06x IOCD wd 1 %08x\n",
                    chan, chp->chan_caw, word1);
                chp->chan_status |= STATUS_PCHK; /* program check for invalid tic */
                chp->chan_caw = word1 & MASK24; /* get new IOCD address */
                uptr->SNS |= SNS_CMDREJ;        /* cmd rejected status */
                uptr->SNS |= SNS_INAD;          /* invalid address status */
                return 1;                       /* error return */
            }
            tic_ok = 0;                         /* another tic not allowed */
            chp->chan_caw = word1 & MASK24;     /* get new IOCD address */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_iocl tic cmd ccw chan %02x tic caw %06x IOCD wd 1 %08x\n",
                chan, chp->chan_caw, word1);
            goto loop;                          /* restart the IOCD processing */
        }
        chp->chan_caw = word1 & MASK24;         /* get new IOCD address */
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid tic */
        uptr->SNS |= SNS_CMDREJ;                /* cmd rejected status */
        if (((word1 & MASK24) == 0) || (word1 & 0x3))
            uptr->SNS |= SNS_INAD;              /* invalid address status */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl TIC ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Check if we had data chaining in previous iocd */
    if ((chp->chan_info & INFO_SIOCD) ||        /* see if 1st IOCD in channel prog */
        (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
        ((chp->ccw_flags & FLAG_DC) == 0))) {    /* last IOCD have DC set? */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_iocl @%06x DO CMD No DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
        docmd = 1;                              /* show we have a command */
    }

    /* Set up for this command */
    chp->ccw_flags = (word2 >> 16) & 0xf000;    /* get flags from bits 0-3 of WD 2 of IOCD */
    chp->chan_status = 0;                       /* clear status for next IOCD */
    /* make a 24 bit address */
    chp->ccw_addr = word1 & MASK24;             /* set the data/seek address */

    /* validate parts of IOCD2 that are reserved */    
    if (word2 & 0x0fff0000) {                   /* bits 4-15 must be zero */
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid iocd */
        sim_debug(DEBUG_EXP, dptr,
            "hsdp_iocl IOCD2 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* DC can only be used with a read/write cmd */
    if (chp->ccw_flags & FLAG_DC) {
        if ((chp->ccw_cmd != DSK_RD) && (chp->ccw_cmd != DSK_WD)) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid DC */
            uptr->SNS |= SNS_CHER;              /* chaining error */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_iocl DC ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
            return 1;                           /* error return */
        }
    }

    chp->chan_byte = BUFF_BUSY;                 /* busy & no bytes transferred yet */

    sim_debug(DEBUG_XIO, dptr,
        "hsdp_iocl @%06x read docmd %01x addr %06x count %04x chan %04x ccw_flags %04x\n",
        chp->chan_caw, docmd, chp->ccw_addr, chp->ccw_count, chan, chp->ccw_flags);

    if (docmd) {                                /* see if we need to process a command */
        DIB *dibp = dib_unit[chp->chan_dev];    /* get the DIB pointer */
 
        uptr = chp->unitptr;                    /* get the unit ptr */
        if (dibp == 0 || uptr == 0) {
            chp->chan_status |= STATUS_PCHK;    /* program check if it is */
            return 1;                           /* if none, error */
        }

        sim_debug(DEBUG_XIO, dptr,
            "hsdp_iocl @%06x before start_cmd chan %04x status %04x count %04x SNS %08x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count, uptr->u5);

        /* call the device startcmd function to process the current command */
        /* just replace device status bits */
        chp->chan_info &= ~INFO_CEND;           /* show chan_end not called yet */
        devstat = dibp->start_cmd(uptr, chan, chp->ccw_cmd);
        chp->chan_status = (chp->chan_status & 0xff00) | devstat;
        chp->chan_info &= ~INFO_SIOCD;          /* show not first IOCD in channel prog */

        sim_debug(DEBUG_XIO, dptr,
            "hsdp_iocl @%06x after start_cmd chan %04x status %08x count %04x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count);

        /* see if bad status */
        if (chp->chan_status & (STATUS_ATTN|STATUS_ERROR)) {
            chp->chan_status |= STATUS_CEND;    /* channel end status */
            chp->ccw_flags = 0;                 /* no flags */
            chp->chan_byte = BUFF_NEXT;         /* have main pick us up */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_iocl bad status chsa %04x status %04x cmd %02x\n",
                chsa, chp->chan_status, chp->ccw_cmd);
            /* done with command */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "hsdp_iocl ERROR return chsa %04x status %08x\n",
                chp->chan_dev, chp->chan_status);
            return 1;                           /* error return */
        } else

        /* NOTE this code needed for MPX 1.X to run! */
        /* see if command completed */
        /* we have good status */
        if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
            uint16  chsa = GET_UADDR(uptr->u3); /* get channel & sub address */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_XIO, dptr,
                "hsdp_iocl @%06x FIFO #%1x cmd complete chan %04x status %04x count %04x\n",
                chp->chan_caw, FIFO_Num(chsa), chan, chp->chan_status, chp->ccw_count);
        }
    }
    /* the device processor returned OK (0), so wait for I/O to complete */
    /* nothing happening, so return */
    sim_debug(DEBUG_XIO, dptr,
        "hsdp_iocl @%06x return, chan %04x status %04x count %04x irq_pend %1x\n",
        chp->chan_caw, chan, chp->chan_status, chp->ccw_count, irq_pend);
    return 0;                                   /* good return */
}

t_stat hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int32       unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd chsa %04x unit %02x cmd %02x CMD %08x\n",
        chsa, unit, cmd, uptr->CMD);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd unit %02x not attached\n", unit);
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd unit %02x busy\n", unit);
        uptr->CMD |= DSK_BUSY;                  /* Flag we are busy */
        return SNS_BSY;
    }
    uptr->SNS2 |= SNS_USEL;                     /* unit selected */
    sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD continue unit=%02x cmd %02x\n", unit, cmd);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH 0x0 */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting INCH %06x cmd, chsa %04x MemBuf %08x cnt %04x\n",
            uptr->u4, chsa, chp->ccw_addr, chp->ccw_count);

        uptr->SNS &= ~SNS_CMDREJ;               /* not rejected yet */
        uptr->CMD |= DSK_INCH2;                 /* use 0xF0 for inch, just need int */
#ifdef FAST_FOR_UTX
//      sim_activate(uptr, 20);                 /* start things off */
        sim_activate(uptr, 30);                 /* start things off */
#else
        sim_activate(uptr, 250);                /* start things off */
//      sim_activate(uptr, 500);                /* start things off */
#endif
        return SCPE_OK;                         /* good to go */
        break;

    case DSK_INC:                               /* 0xFF Initialize controller */
        if ((cmd == DSK_INC) &&
            (chp->ccw_count != 0x20))           /* count must be 32 to be valid */
            break;
    case DSK_NOP:                               /* NOP 0x03 */
    case DSK_SKC:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x1f */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register */
    case DSK_WSL:                               /* WSL 0x31 */
    case DSK_RSL:                               /* RSL 0x32 */
    case DSK_IHA:                               /* 0x47 Increment head address */
    case DSK_WTL:                               /* WTL 0x51 */
    case DSK_RTL:                               /* RTL 0x52 */
    case DSK_RVL:                               /* 0x42 Read vendor label */
    case DSK_WTF:                               /* 0x41 Write track format */
    case DSK_RAP:                               /* 0xA2 Read angular positions */
    case DSK_FMT:                               /* 0x0B Format for no skip */
    case DSK_RES:                               /* 0x23 Reserve */
    case DSK_REL:                               /* 0x33 Release */
        uptr->SNS &= ~MASK24;                   /* clear data  & leave mode */
        uptr->SNS2 = (SNS_UNR|SNS_ONC|SNS_USEL);/* reset status to on cyl & ready */
    case DSK_SNS:                               /* Sense 0x04 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting disk cmd %02x chsa %04x\n",
            cmd, chsa);
#ifdef FAST_FOR_UTX
//      sim_activate(uptr, 20);                 /* start things off */
//      sim_activate(uptr, 30);                 /* start things off */
        sim_activate(uptr, 25);                 /* start things off */
#else
        sim_activate(uptr, 250);                /* start things off */
//      sim_activate(uptr, 500);                /* start things off */
#endif
        return SCPE_OK;                         /* good to go */
        break;

    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd done with hsdp_startcmd %02x chsa %04x SNS %08x\n",
        cmd, chsa, uptr->SNS);
    /* diags want the chan addr to point at bad command?? */
    chp->chan_caw -= 8;                         /* backup iocd address for diags */
    uptr->SNS |= SNS_CMDREJ;                    /* cmd rejected */
    return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* return error */
}

/* Handle haltio transfers for disk */
t_stat  hsdp_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         cmd = uptr->CMD & DSK_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, dptr, "hsdp_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    /* stop any I/O and post status and return error status */
    sim_debug(DEBUG_CMD, dptr,
        "hsdp_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
    if ((uptr->CMD & DSK_CMDMSK) != 0) {        /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        sim_cancel(uptr);                       /* clear the input timer */
        chp->ccw_count = 0;                     /* zero the count */
        chp->chan_caw = 0;                      /* zero iocd address for diags */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* stop any chaining */
        uptr->CMD &= LMASK;                     /* make non-busy */
        uptr->SNS2 |= (SNS_ONC|SNS_UNR);        /* on cylinder & ready */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* force end */
        return CC2BIT | SCPE_IOERR;             /* busy return */
    } else {
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_haltio HIO I/O not busy chsa %04x cmd = %02x\n", chsa, cmd);
        uptr->CMD &= LMASK;                     /* make non-busy */
        uptr->SNS2 |= (SNS_ONC|SNS_UNR);        /* on cylinder & ready */
        return CC1BIT | SCPE_OK;                /* not busy */
    }
}

/* Handle rsctl command for disk */
t_stat  hsdp_rsctl(UNIT *uptr) {
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         cmd = uptr->CMD & DSK_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    if ((uptr->CMD & DSK_CMDMSK) != 0) {    /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_rsctl RSCTL chsa %04x cmd %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        sim_cancel(uptr);                   /* clear the input timer */
        chp->ccw_count = 0;                 /* zero the count */
        chp->chan_caw = 0;                  /* zero iocd address for diags */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);/* stop any chaining */
    }
    uptr->CMD &= LMASK;                     /* make non-busy */
    uptr->SNS2 |= (SNS_ONC|SNS_UNR);        /* on cylinder & ready */
    sim_debug(DEBUG_CMD, dptr,
        "hsdp_rsctl RSCTL I/O not busy chsa %04x cmd %02x\n", chsa, cmd);
    return SCPE_OK;                         /* not busy */
}

/* Handle processing of hsdp requests. */
t_stat hsdp_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    CHANP           *chp = find_chanp_ptr(chsa);    /* get channel prog pointer */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    uint32          tcyl=0, trk=0, cyl=0, sec=0, tempt=0;
    int             unit = (uptr - dptr->units);
    int             len = chp->ccw_count;
    int             i,j,k;
    uint32          mema, ecc, cecc, tstar;         /* memory address */
    uint8           ch;
    uint16          ssize = hsdp_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32          tstart;
    uint8           lbuf[32];
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        if (cmd != DSK_SNS) {                   /* we are completed with unit check status */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);

#ifdef FOR_TESTING_DIAGS
    /* see if mode reg is 1 (diag mode), if so we are done */
    if ((uptr->SNS >> 24) & 1) {                /* get mode value */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chp->ccw_count = 0;
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        return SCPE_OK;
    }
#endif

    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_INC:                               /* 0xFF Initialize controller */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd CONT INC %06x chsa %04x addr %06x count %04x completed\n",
            chp->chan_inch_addr, chsa, mema, chp->ccw_count);
        /* to use this inch method, byte count must be 0x20 */
        if (len != 0x20) {
                /* we have invalid count, error, bail out */
                uptr->SNS |= SNS_CMDREJ;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
        }
        /* read all 32 bytes, stopping every 4 bytes to make words */
        /* the 8 words have drive data for each unit */
        /* WARNING 8 drives must be defined for this controller */
        /* so we will not have a map fault */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv CONT INC data:");
        for (i=0; i < 32; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_CMD, dptr, "\nhsdp_srv CONT INC data:");
            sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
            if (((i+1)%4) == 0) {               /* see if we have a word yet */
#ifndef FOR_TESTING
                int dn = i/4;                   /* get drive number */
//              UNIT *up = uptr0[dn];           /* get our unit pointer */
                UNIT *uptr0 = dptr->units;      /* get unit 0 pointer */
#endif
                /* drive attribute registers */
                /* may want to use this later */
                /* clear warning errors */
                tstart = (buf[i-3]<<24) | (buf[i-2]<<16)
                    | (buf[i-1]<<8) | (buf[i]);
#ifndef FOR_TESTING
                uptr0[dn].DAI = tstart;         /* save drive attribute register */
                /* set mode data from last byte */
                uptr0[dn].SNS &= MASK24;        /* clear old mode data */
                uptr0[dn].SNS |= (buf[i] << 24);   /* save mode value */
#endif
            }
        }
        sim_debug(DEBUG_CMD, dptr, "\n");

        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd INC chsa %04x chsa %06x count %04x mode %08x completed\n",
            chsa, mema, chp->ccw_count, uptr->DAI);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_INCH2:                             /* use 0xF0 for inch, just need int */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chp->chan_inch_addr, chsa, chp->ccw_addr, chp->ccw_count);

        /* mema has IOCD word 1 contents.  For the disk processor it contains */
        /* a pointer to the INCH buffer followed by 8 drive attribute words that */
        /* contains the flags, sector count, MHD head count, and FHD count */
        /* len has the byte count from IOCD wd2 and should be 0x24 (36) */
        /* the INCH buffer address must be set for the parent channel as well */
        /* as all other devices on the channel.  Call set_inch() to do this for us */
        /* just return OK and channel software will use u4 as status buffer addr */

        if (len != 36) {
            /* we have invalid count, error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* read all 36 bytes, stopping every 4 bytes to make words */
        /* the first word has the inch buffer address */
        /* the next 8 words have drive data for each unit */
        /* WARNING 8 drives must be defined for this controller */
        /* so we will not have a map fault */
        for (i=0; i < 36; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (((i+1)%4) == 0) {               /* see if we have a word yet */
                if (i == 3) {
                    /* inch buffer address */
                    mema = (buf[0]<<24) | (buf[1]<<16) |
                        (buf[2]<<8) | (buf[3]);
                    sim_debug(DEBUG_CMD, dptr, "Inch buffer %08x", mema);
                } else {
#ifndef FOR_TESTING
                    int dn = (i-4)/4;           /* get drive number */
                    UNIT *uptr0 = dptr->units;  /* get unit 0 pointer */
#endif
                    /* drive attribute registers */
                    /* may want to use this later */
                    /* clear warning errors */
                    tstart = (buf[i-3]<<24) | (buf[i-2]<<16)
                        | (buf[i-1]<<8) | (buf[i]);
                    if (i == 23)
                        sim_debug(DEBUG_CMD, dptr, "\nInch buffer %08x", mema);
#ifndef FOR_TESTING
                    uptr0[dn].DAI = tstart;     /* save drive attribute register */
                    /* set mode data from last byte */
                    uptr0[dn].SNS &= MASK24;    /* clear old mode data */
                    uptr0[dn].SNS |= (buf[i] << 24);    /* save mode value */
#endif
                    sim_debug(DEBUG_CMD, dptr, " %08x", tstart);
                }
            }
        }
        sim_debug(DEBUG_CMD, dptr, "\n");

        /* now call set_inch() function to write and test inch buffer addresses */
        /* 1-224 wd buffer is provided, status is 128 words offset from start */
        mema += (128*4);                        /* offset to inch buffers */
        i = set_inch(uptr, mema, 33);           /* new address of 33 entries */
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd INCH %06x chsa %04x addr %06x count %04x mode %08x completed\n",
            chp->chan_inch_addr, chsa, mema, chp->ccw_count, tcyl);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        if ((uptr->CMD & DSK_WAITING) == 0) {
            /* Do a fake wait to kill some time */
            uptr->CMD |= DSK_WAITING;           /* show waiting for NOP */
            sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd NOP stalling for 50 cnts\n");
//          sim_activate(uptr, 250);            /* start waiting */
//          sim_activate(uptr, 50);             /* start waiting */
            sim_activate(uptr, 350);             /* start waiting */
            break;
        }
        /* NOP drop through after wait */
    case DSK_RES:                               /* 0x23 Reserve */
    case DSK_REL:                               /* 0x33 Release */
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RAP:                               /* 0xA2 Read angular positions */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        /* get STAR (target sector) data in STAR */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        trk = (uptr->CHS >> 8) & 0xff;          /* get trk/head */
        sec = uptr->CHS & 0xff;                 /* set sec */

//      ch = ((sec * 2) % SPT(type)) & 0x3f;    /* get index cnt */
        ch = ((2*SPT(type))-1) & 0x3f;          /* get index cnt */
        uptr->SNS2 = (uptr->SNS2 & 0xc0ff) | ((((uint32)ch) & 0x3f) << 8);
        sim_debug(DEBUG_CMD, dptr,
           "hsdp_srv RAP %02x cyl %04x trk %02x sec %02x\n",
           ch, cyl&0xffff, trk, sec);

        if (chan_write_byte(chsa, &ch)) {       /* put a byte to memory */
            sim_debug(DEBUG_CMD, dptr,
                "HSDP RAP %02x for addr /%04x/%02x/%02x\n",
                ch, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
            if (chp->chan_status & STATUS_PCHK) { /* test for memory error */
                uptr->SNS |= SNS_INAD;          /* invalid address */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            } else
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
            break;
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_IHA:                               /* 0x47 Increment head address */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        /* get STAR (target sector) data in STAR */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        trk = (uptr->CHS >> 8) & 0xff;          /* get trk/head */
        sec = 0;                                /* set sec to zero for this head */

        sim_debug(DEBUG_CMD, dptr,
           "hsdp_srv IHA cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, sec, unit);

        /* Check if head increment valid */
        trk += 1;                               /* increment the head # */
        if (trk >= hsdp_type[type].nhds) {      /* see if too big */
            trk = 0;                            /* back to trk 0 */
            cyl += 1;                           /* next cylinder */
            if (cyl >= hsdp_type[type].cyl) {   /* see if too big */
                /* set new STAR value using new values */
                uptr->CHS = CHS2STAR(cyl, trk, sec);
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_srv IHA ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                    cyl, trk, sec, unit);
                uptr->SNS |= SNS_DADE;          /* set error status */
                uptr->SNS2 |= (SNS_SKER|SNS_SEND);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* error */
                break;
            }
        }

        /* set new STAR value using new values */
        uptr->CHS = CHS2STAR(cyl, trk, sec);
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv IHA unit=%02x STAR %08x %04x/%02x/%02x\n",
            unit, uptr->CHS, cyl, trk, sec);
        /* get alternate track if this one is defective */
//sim_debug(DEBUG_CMD, dptr, "Dpatrk1 %08x label\n", uptr->CHS);
        tempt = get_dpatrk(uptr, uptr->CHS, lbuf);
        /* file offset in bytes to std or alt track */
        tstart = STAR2SEC(tempt, SPT(type), SPC(type)) * SSB(type);

        if ((tempt == 0) && (uptr->CHS != 0)) {
            /* we have error */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_srv IHA get_dpatrk return error tempt %06x tstart %06x CHS %08x\n",
                tempt, tstart, uptr->CHS);
            goto iha_error;
        }

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
iha_error:
            uptr->SNS |= SNS_DADE;              /* set error status */
            uptr->SNS2 |= (SNS_SKER|SNS_SEND);
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv IHA error on seek to %04x\n", tstart);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_REC: /* 0xB2 */                     /* Read ECC correction code */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD REC Read ECC\n");
        /* count must be 4, if not prog check */
        if (len != 4) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv REC bad count unit=%02x count%04x CHS %08x\n",
                unit, len, uptr->CHS);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }
        /* create offset and mask */
        ecc = dple_ecc32(obuf, ssize);          /* calc ecc for original sector */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv DEC old obuf data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                obuf[1016], obuf[1017], obuf[1018], obuf[1019],
                obuf[1020], obuf[1021], obuf[1022], obuf[1023]);
        cecc = dple_ecc32(bbuf, ssize);         /* calc ecc for bad sector */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv DEC bad bbuf data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                bbuf[1016], bbuf[1017], bbuf[1018], bbuf[1019],
                bbuf[1020], bbuf[1021], bbuf[1022], bbuf[1023]);
        mema = 0;
        for (i=0, j=0; i<ssize; i++) {
            tcyl = bbuf[i]^obuf[i];             /* see if byte are different */
            if (tcyl != 0) {
                j = i;                          /* save ending byte */
                mema = (mema << 8) | tcyl;      /* put in next error */
            }
        }
        /* here mema has 1 or 2 bytes of error bits */
        /* j has byte index of last bad bit */
        k = ((int)ssize) - (j + 1);             /* make into byte# from end */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv REC rb# %04x mema %04x ECC %08x bad ECC %08x\n",
            k, mema, ecc, cecc);
        /* find starting bit */
        for (i=0; i<8; i++) {
            if (mema & 1) {
                sec = i;                        /* starting bit index */
                break;
            }
            mema >>= 1;                         /* move mask right */
        }
        tcyl = (k * 8) + sec;                   /* starting bit# */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv REC sb# %04x byte# %04x mask %06x start %08x\n",
            sec, k, mema, tcyl);
        /* 16 bit sector offset and 9 of 16 bit mask */
        /* tcyl - fake 14 bit offset */
        /* mema - fake 9 bit mask */
        buf[0] = (tcyl & 0x3f00) >> 8;          /* upper 6 bits */
        buf[1] = tcyl & 0xff;                   /* lower 8 bits */
        buf[2] = (mema & 0x100) >> 8;           /* upper 1 bits */
        buf[3] = mema & 0xff;                   /* lower 8 bits */
        /* write the offset and mask data */
        for (i=0; i<4; i++) {
            ch = buf[i];                        /* get a char from buffer */
            if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv DEC read %04x bytes of %04x\n",
                    i, chp->ccw_count);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                else
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                return SCPE_OK;
            }
        }
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv wrote DEC offset %04x mask %04x CHS %08x\n",
            tcyl & 0x3fff, mema & 0x1ff, uptr->CHS);
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SNS: /* 0x04 */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD sense\n");

        /* count must be 12 or 14, if not prog check */
        if (len != 12 && len != 14) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv Sense bad count unit=%02x count%04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }
        /* bytes 0,1 - Cyl entry from CHS reg */
        ch = (uptr->CHS >> 24) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense CHS b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->CHS >> 16) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense CHS b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from CHS reg */
        ch = (uptr->CHS >> 8) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense CHS b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from CHS reg */
        ch = (uptr->CHS) & 0xff;
        sec = ch;                           /* save sec num for later */
#ifndef FOR_TESTING
        /* get STAR (target sector) data in STAR */
        cyl = STAR2CYL(uptr->CHS);          /* get current cyl */
        trk = (uptr->CHS >> 8) & 0xff;      /* get trk/head */
        if ((trk == (hsdp_type[type].nhds-1)) && /* see if last trk */
            ((cyl == hsdp_type[type].cyl-1)) &&  /* see if last cyl */
            (sec == 0)) {                   /* sec is zero */
            ch = 0;                         /* show last track */
        }
#endif
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense CHS b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 4 - mode reg, byte 0 of SNS */
        ch = (uptr->SNS >> 24) & 0xff;      /* return the sense data */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
        ch = (uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv sense unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 8-11 - drive mode register entries from assigned hsdp */
        ch = hsdp_type[type].type & 0xff;       /* type byte */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv datr unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = hsdp_type[type].spt & 0xff;        /* get sectors per track */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv datr unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = hsdp_type[type].nhds & 0xff;       /* get # MHD heads */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv datr unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
#ifdef FOR_TESTING
        ch = (uptr->SNS >> 24) & 0xff;          /* get mode data */
#else
        ch = 0;                                 /* no FHD heads */
#endif
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv datr unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 12 & 13 are optional, so check if read done */
        /* TODO add drive status bits here */
        if ((test_write_byte_end(chsa)) == 0) {
            /* bytes 12 & 13 contain drive related status */
            uptr->SNS2 |= (SNS_SEND|SNS_USEL);  /* selected & seek end */
            /* bits 2-7 have sector pulse count */
            ch = ((sec * 2) % SPT(type)) & 0x3f;/* get index cnt */
            uptr->SNS2 = (uptr->SNS2 & 0xc0ff) | ((((uint32)ch) & 0x3f) << 8);
            ch = (uptr->SNS2 >> 8) & 0xff;      /* seek end and unit selected for now */
            sim_debug(DEBUG_CMD, dptr, "hsdp_srv dsr unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);

            ch = 0x30;                          /* drive on cylinder and ready for now */
            uptr->SNS2 |= (SNS_ONC|SNS_UNR);    /* on cylinder & ready */
            ch = uptr->SNS2 & 0xff;             /* drive on cylinder and ready for now */
            sim_debug(DEBUG_CMD, dptr, "hsdp_srv dsr unit=%02x 2 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);
        }
        uptr->SNS &= 0xff000000;                /* reset status */
        uptr->SNS2 = 0;                         /* reset status */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SKC:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* file offset in bytes to std or alt track */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type)) * SSB(type);
            /* we are on cylinder, seek is done */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv seek on cylinder to %04x/%02x/%02x bytes %06x\n",
                (uptr->CHS >> 16) & 0xffff, (uptr->CHS >> 8) & 0xff,
                uptr->CHS & 0xff, tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS2 |= (SNS_SEND|SNS_ONC);   /* On cylinder & seek done */
            /* we have already seeked to the required sector */
            /* we do not need to seek again, so move on */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
            break;
        }

        /* not seeking, so start a new seek */
        /* set buf data to current STAR values */
        tcyl = cyl = STAR2CYL(uptr->CHS);       /* get current cyl */
        buf[0] = (cyl >> 8) & 0xff;             /* split cylinder */
        buf[1] = cyl & 0xff;
        buf[2] = (uptr->CHS >> 8) & 0xff;       /* get trk/head */
        buf[3] = uptr->CHS & 0xff;              /* get sec */

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv current STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        if (len > 4) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv SEEK bad count unit=%02x count%04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }

        /* Read in 1-4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                if (i == 0) {
                    sim_debug(DEBUG_CMD, dptr,
                        "hsdp_srv seek error unit=%02x star %02x %02x %02x %02x\n",
                        unit, buf[0], buf[1], buf[2], buf[3]);
                    /* we have error, bail out */
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    uptr->SNS |= SNS_DADE;      /* Disc addressing or seek error */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    chp->ccw_count = len;       /* restore count, huh? */
                    return SCPE_OK;
                    break;
                }
                /* just read the next byte */
                /* done reading, see how many we read */
                if (i == 1) {
                    /* UTX wants to set seek STAR to zero */
                    buf[0] = buf[1] = buf[2] = buf[3] = 0;
                    break;
                }
            }
        }
        /* else the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        tstar = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = STAR2CYL(tstar);                  /* get the cylinder */
        trk = buf[2];                           /* get the track */
        sec = buf[3];                           /* get sec */

        /* see if we need to incr to next track for alt sec support */
        if (uptr->LSC != SPT(type)) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv LSC0 %02x B4 test/incr cyl %04x trk %02x sec %02x\n",
                uptr->LSC, (tstar>>16)&0xffff, (tstar>>8)&0xff, tstar&0xff);
            if ((int)(tstar&0xff) >= (int)(SPT(type)-1)) {
                tstar &= 0xffffff00;            /* clear sector number */
                tstar += 0x00000100;            /* bump head # */
                if (((tstar>>8)&0xff) >= (HDS(type))) {
                    tstar &= 0xffff00ff;        /* clear head number */
                    tstar += 0x00010000;        /* bump cyl # */
                }
            }
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv LSC0 %02x AF test/incr cyl %04x trk %02x sec %02x\n",
                uptr->LSC, (tstar>>16)&0xffff, (tstar>>8)&0xff, tstar&0xff);
//#define DO_DYNAMIC_DEBUG
#ifdef DO_DYNAMIC_DEBUG
// cpu_dev.dctrl |= DEBUG_INST|DEBUG_TRAP|DEBUG_CMD|DEBUG_DETAIL;   /* start instruction trace */
#endif
        }

        sim_debug(DEBUG_CMD, dptr,
           "hsdp_srv NEW SEEK cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (cyl >= hsdp_type[type].cyl ||
            trk >= hsdp_type[type].nhds ||
            buf[3] >= uptr->LSC) {

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv seek ERROR LSC %02x cyl %04x trk %02x sec %02x unit=%02x\n",
                uptr->LSC, cyl, trk, buf[3], unit);

            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_DADE;              /* set error status */
            uptr->SNS2 |= (SNS_SKER|SNS_SEND);

            /* set new STAR value */
            uptr->CHS = CHS2STAR(cyl, trk, buf[3]);

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* get alternate track if this one is defective */
        tempt = get_dpatrk(uptr, tstar, lbuf);

        if ((tempt == 0) && (tstar != 0)) {
            /* we have error */
            sim_debug(DEBUG_EXP, dptr,
                "hsdp_srv SEEK get_dpatrk return error tempt %06x STAR %08x\n",
                tempt, tstar);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* file offset in bytes to std or alt track */
        tstart = STAR2SEC(tempt, SPT(type), SPC(type)) * SSB(type);
        /* set new STAR value using new values */
        uptr->CHS = tstar;

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv seek start %04x cyl %04x trk %02x sec %02x CHS %08x\n",
            tstart, cyl, trk, buf[3], uptr->CHS);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* seek home */
            sim_debug(DEBUG_CMD, dptr, "hsdp_srv Error on seek to %08x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* do a delay to slow things down */
        if (STAR2CYL(uptr->CHS) != tcyl) {
            k = ((int)tcyl - (int)cyl);
            if (k < 0)
                k = -k;
        } else {
            k = 20;
        }
        /* Do a fake seek to kill time */
        uptr->CMD |= DSK_SEEKING;               /* show we are seeking */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv seeking unit=%02x to %04x/%02x/%02x from cyl %04x (%04x)\n",
            unit, cyl, trk, buf[3], tcyl, k);
#ifdef FAST_FOR_UTX
            sim_activate(uptr, 15);
//          sim_activate(uptr, 20);             /* start things off */
//          sim_activate(uptr, 20+k);           /* start us off */
#else
//          sim_activate(uptr, 150);            /* start things off */
            sim_activate(uptr, 200+k);          /* start us off */
//          sim_activate(uptr, 400+k);          /* start us off */
#endif
        break;

    case DSK_XEZ:   /* 0x37 */                  /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "RD REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        uptr->CHS = 0;                          /* set current CHS to 0, 0, 0 */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        uptr->CMD |= DSK_SKC;                   /* show as seek command */
        tstart = 0;                             /* byte offset is 0 */

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        /* we are on cylinder/track/sector zero, so go on */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv done seek trk 0\n");
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        break;

    case DSK_LMR:   /* 0x1F */
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x\n", unit);
        /* Read in 1 character of mode data */
       if (chan_read_byte(chsa, &buf[0])) {
            if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                uptr->SNS |= SNS_INAD;          /* invalid address */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            else
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
            break;
        }
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x old %02x new %02x\n",
            unit, (uptr->SNS>>24)&0xff, buf[0]);
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        uptr->SNS &= MASK24;                    /* clear old mode data */
        uptr->SNS |= (buf[0] << 24);            /* save mode value */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_FMT:                               /* 0x0B Format for no skip */
        /* buffer must be on halfword boundry if not STATUS_PCHK and SNS_CMDREJ status */
//      chp->chan_status |= STATUS_PCHK;        /* program check for invalid cmd */
//      uptr->SNS |= SNS_CMDREJ;                /* cmd rejected */
        /* byte count can not exceed 20160 for the track */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "HSDP Format starting CMD %08x chsa %04x buffer %06x count %04x\n",
            uptr->CMD, chsa, chp->ccw_addr, chp->ccw_count);
        sim_debug(DEBUG_CMD, dptr, "Format %x label", uptr->CHS);
        /* now read sector label data */
        len = chp->ccw_count;
        for (i = 0; i < len; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have write error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
            if ((i%16) == 0)
                sim_debug(DEBUG_CMD, dptr, "\nFormat %x label", uptr->CHS);
            sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_CMD, dptr, "\n");
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_RD:                                /* Read Data */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ starting CMD %08x chsa %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, chp->ccw_addr, chp->ccw_count);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            /* get alternate track if this one is defective */
            tempt = get_dpatrk(uptr, uptr->CHS, lbuf);
            /* file offset in bytes to std or alt track */
            tstart = STAR2SEC(tempt, SPT(type), SPC(type)) * SSB(type);

            if ((tempt == 0) && (uptr->CHS != 0)) {
                /* we have error */
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_srv READ get_dpatrk return error tempt %06x tstart %06x\n", tempt, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_DADE;          /* set error status */
                uptr->SNS2 |= (SNS_SKER|SNS_SEND);
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv READ error on seek to %04x\n", tstart);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            uptr->SNS &= ~SNS_DEFTRK;           /* remove defective flag */
            /* see if spare track */
            if (lbuf[4] & 0x20) {               /* see if spare track */
                uptr->SNS |= SNS_DADE;          /* disk addr error */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_srv READ get_dpatrk return spare tempt %06x tstart %06x\n", tempt, tstart);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                break;
            }

            /* see if reserved track */
            if (lbuf[4] & 0x10) {               /* see if reserved track */
                uptr->SNS |= SNS_MOCK;          /* mode check error */
                uptr->SNS |= SNS_RES8;          /* reserved track access error */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                break;
            }

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv READ, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ reading CMD %08x chsa %04x tstart %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, tstart, chp->ccw_addr, chp->ccw_count);

            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, ssize, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv after READ chsa %04x buffer %06x count %04x\n",
                chsa, chp->ccw_addr, chp->ccw_count);
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv READ data %02x%02x%02x%02x %02x%02x%02x%02x "
                "%02x%02x%02x%02x %02x%02x%02x%02x\n",
                buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
                buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);

            uptr->CHS++;                        /* next sector number */
            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    sim_debug(DEBUG_CMD, dptr,
                        "HSDP Read %04x bytes leaving %04x from diskfile /%04x/%02x/%02x\n",
                        i, chp->ccw_count, ((uptr->CHS)>>16)&0xffff,
                        ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                    else
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    return SCPE_OK;
                }
            }

            /* get current sector offset */
            j = STAR2SEC(tempt, SPT(type), SPC(type));  /* current sector */
            i = ((CYL(type) - 3) * HDS(type)) * SPT(type);  /* diag start */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv after READ j %04x i %04x j-i %04x CAP %06x DIAG %06x\n",
                j, i, j-i, CAP(type), (((CYL(type) - 3) * HDS(type)) * SPT(type)));    /* diag start */
            if (j >= i) {                       /* only do diag sectors */
                cecc = dple_ecc32(buf, ssize);  /* calc ecc for sector */
                sim_debug(DEBUG_CMD, dptr,
                    "ECC j %02x i %02x data calc Old %08x Cur %08x cyl %04x hds %02x sec %02x\n",
                    j, i, decc[j-i], cecc, STAR2CYL(tempt), ((tempt) >> 8)&0xff, (tempt&0xff));
                if ((decc[j-i] != 0) && (cecc != decc[j-i])) {     /* test against old */
                    /* checksum error */
                    sim_debug(DEBUG_CMD, dptr,
                        "ECC j %02x i %02x data error Old %08x New %08x cyl %04x hds %02x sec %02x\n",
                        j, i, decc[j-i], cecc, STAR2CYL(tempt), ((tempt) >> 8)&0xff, (tempt&0xff));
                    uptr->SNS |= SNS_ECCD;      /* data ECC error */
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_CHECK|STATUS_EXPT);
                    return SCPE_OK;
                }
            }

            /* see if this is a read ECC from diag */
            /* mode byte will be 0x08 and remaining count will be 4 */
            if ((uptr->SNS & SNS_DIAGMOD) && (chp->ccw_count == 4)) {
                for (i=0; i<ssize; i++)
                    obuf[i] = buf[i];           /* save buffer */
                ecc = dple_ecc32(buf, ssize);   /* calc ecc for sector */
                sim_debug(DEBUG_CMD, dptr,
                    "Reading ECC %08x cyl %04x hds %02x sec %02x\n",
                    ecc, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                /* set ECC value here */
                for (i=0; i<4; i++) {
                    ch = (ecc >> ((3-i)*8)) & 0xff; /* get a char from buffer */
                    if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                        if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                            uptr->SNS |= SNS_INAD;  /* invalid address */
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                        return SCPE_OK;
                    }
                }
                sim_debug(DEBUG_CMD, dptr,
                    "Read ECC %04x for diags 4 bytes to ECC REG cyl %04x hds %02x sec %02x\n",
                    ecc, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
            }

            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ %04x bytes leaving %4x to be read to %06x from diskfile %04x/%02x/%02x\n",
                ssize, chp->ccw_count, chp->ccw_addr,
                ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);

            /* see if we need to incr to next track for alt sec support */
            if (uptr->LSC != SPT(type)) {
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv LSC %02x B4 test/incr cyl %04x trk %02x sec %02x\n",
                    uptr->LSC, (uptr->CHS>>16)&0xffff, (uptr->CHS>>8)&0xff, uptr->CHS&0xff);
                if ((uptr->CHS&0xff) >= (SPT(type)-1)) {
                    uptr->CHS &= 0xffffff00;    /* clear sector number */
                    uptr->CHS += 0x00000100;    /* bump head # */
                    if (((uptr->CHS>>8)&0xff) >= (HDS(type))) {
                        uptr->CHS &= 0xffff00ff;    /* clear head number */
                        uptr->CHS += 0x00010000;    /* bump cyl # */
                    }
                }
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv LSC %02x AF test/incr cyl %04x trk %02x sec %02x\n",
                    uptr->LSC, (uptr->CHS>>16)&0xffff, (uptr->CHS>>8)&0xff, uptr->CHS&0xff);
            }

            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP Read reached EOM for read from disk @ %04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP Read complete for read from disk @ %04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "HSDP sector read complete, %x bytes to go from diskfile /%04x/%02x/%02x\n",
                chp->ccw_count, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
#ifdef FAST_FOR_UTX
//          sim_activate(uptr, 10);             /* wait to read next sector */
            sim_activate(uptr, 15);             /* wait to read next sector */
//          sim_activate(uptr, 20);             /* wait to read next sector */
#else
            sim_activate(uptr, 150);            /* wait to read next sector */
//          sim_activate(uptr, 300);            /* wait to read next sector */
#endif
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        break;

    case DSK_WD:            /* Write Data */
        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            sim_debug(DEBUG_CMD, dptr,
                "HSDP WRITE starting unit=%02x CMD %08x write %04x from %06x to %03x/%02x/%02x\n",
                unit, uptr->CMD, chp->ccw_count, chp->ccw_addr,
                ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);

            if (uptr->SNS & 0xf0000000) {       /* see if any mode bit 0-3 is set */
                uptr->SNS |= SNS_MOCK;          /* mode check error */
                chp->chan_status |= STATUS_PCHK; /* channel prog check */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                break;
            }
            uptr->CMD |= DSK_WRITING;           /* write to disk starting */
        }
        if (uptr->CMD & DSK_WRITING) {          /* see if we are writing data */
            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* file offset in bytes */
            tstart = tstart * SSB(type);

            /* get alternate track if this one is defective */
            tempt = get_dpatrk(uptr, uptr->CHS, lbuf);
            /* file offset in bytes to std or alt track */
            tstart = STAR2SEC(tempt, SPT(type), SPC(type)) * SSB(type);

            if ((tempt == 0) && (uptr->CHS != 0)) {
                /* we have error */
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_srv WRITE get_dpatrk return error tempt %06x tstart %06x\n", tempt, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_DADE;          /* set error status */
                uptr->SNS2 |= (SNS_SKER|SNS_SEND);
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv WRITE error on seek to %04x\n", tstart);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            uptr->SNS &= ~SNS_DEFTRK;           /* remove defective flag */
            /* see if spare track */
            if (lbuf[4] & 0x20) {               /* see if spare track */
                uptr->SNS |= SNS_DADE;          /* disk addr error */
                chp->chan_status |= STATUS_PCHK; /* channel prog check */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                break;
            }
            /* see if reserved track */
            if (lbuf[4] & 0x10) {               /* see if reserved track */
                uptr->SNS |= SNS_MOCK;          /* mode check error */
                uptr->SNS |= SNS_RES8;          /* reserved track access error */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                break;
            }

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv WRITE, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }

            /* process the next sector of data */
            tcyl = 0;                           /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {    /* get a byte from memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    /* if error on reading 1st byte, we are done writing */
                    if ((i == 0) || (chp->chan_status & STATUS_PCHK)) {
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "HSDP Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                            ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                        if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                        else
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                        return SCPE_OK;
                    }
                    ch = 0;                     /* finish out the sector with zero */
                    tcyl++;                     /* show we have no more data to write */
                }
                buf2[i] = ch;                   /* save the char */
            }

            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            /* write the sector to disk */
            if ((i=sim_fwrite(buf2, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on write %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    i, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv after WRITE buffer %06x count %04x\n",
                chp->ccw_addr, chp->ccw_count);
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv WRITE data %02x%02x%02x%02x %02x%02x%02x%02x "
                "%02x%02x%02x%02x %02x%02x%02x%02x\n",
                buf2[0], buf2[1], buf2[2], buf2[3], buf2[4], buf2[5], buf2[6], buf2[7],
                buf2[8], buf2[9], buf2[10], buf2[11], buf2[12], buf2[13], buf2[14], buf2[15]);
            sim_debug(DEBUG_DATA, dptr,
                "hsdp_srv after WRITE CAP %06x DIAG %06x\n",
                CAP(type), (((CYL(type) - 3) * HDS(type)) * SPT(type)));    /* diag start */

            /* get current sector offset */
            j = STAR2SEC(tempt, SPT(type), SPC(type));  /* current sector */
            i = ((CYL(type) - 3) * HDS(type)) * SPT(type);  /* diag start */
            sim_debug(DEBUG_DATA, dptr,
                "hsdp_srv after WRITE j %04x i %04x j-i %04x CAP %06x DIAG %06x\n",
                j, i, j-i, CAP(type), (((CYL(type) - 3) * HDS(type)) * SPT(type)));    /* diag start */
            if (j >= i) {                       /* only do diag sectors */
                cecc = dple_ecc32(buf2, ssize); /* calc ecc for sector */
                sim_debug(DEBUG_DATA, dptr,
                    "ECC j %02x i %02x data write Old %08x Cur %08x cyl %04x hds %02x sec %02x\n",
                    j, i, decc[j-i], cecc, STAR2CYL(tempt), ((tempt) >> 8)&0xff, (tempt&0xff));
                decc[j-i] = cecc;               /* set new ecc */
            }
            j = j-i;                            /* save index */

            /* see if this is a write ECC from diag */
            /* mode byte will be 0x08 and remaining count will be 4 */
            if ((uptr->SNS & SNS_DIAGMOD) && (chp->ccw_count == 4)) {
                for (i=0; i<ssize; i++)
                    bbuf[i] = buf2[i];          /* save bad buffer */
                cecc = dple_ecc32(buf2, ssize); /* calc ecc for sector */
                ecc = 0;
                sim_debug(DEBUG_CMD, dptr,
                    "Writing decc[%04x] ECC %08x cyl %04x hds %02x sec %02x\n",
                    j, cecc, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                /* set ECC value here */
                for (i=0; i<4; i++) {
                    if (chan_read_byte(chsa, &ch)) {/* get a byte from memory */
                        if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                            uptr->SNS |= SNS_INAD;  /* invalid address */
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                        return SCPE_OK;
                    }
                    /* get an ECC byte */
                    buf[i] = ch;                /* put a char to buffer */
                    ecc |= ((ch & 0xff) << ((3-i)*8));
                }
                tcyl++;                         /* show we have no more data to write */
                sim_debug(DEBUG_CMD, dptr,
                    "Write decc[%04x] ECC=%08x from diags, calc ECC=%08x cyl %04x hds %02x sec %02x\n",
                    j, ecc, cecc, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                decc[j] = ecc;                  /* set new ecc from diag */
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK WR to sec end %04x bytes end %04x to diskfile cyl %04x hds %02x sec %02x\n",
                len, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));

            uptr->CHS++;                        /* next sector number */
            if (tcyl != 0) {                    /* see if done with write command */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }

            /* see if we need to incr to next track for alt sec support */
            if (uptr->LSC != SPT(type)) {
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv LSC2 %02x B4 test/incr cyl %04x trk %02x sec %02x\n",
                    uptr->LSC, (uptr->CHS>>16)&0xffff, (uptr->CHS>>8)&0xff, uptr->CHS&0xff);
                if ((uptr->CHS&0xff) >= (SPT(type)-1)) {
                    uptr->CHS &= 0xffffff00;    /* clear sector number */
                    uptr->CHS += 0x00000100;    /* bump track # */
                    if (((uptr->CHS>>8)&0xff) >= (HDS(type))) {
                        uptr->CHS &= 0xffff00ff;    /* clear head number */
                        uptr->CHS += 0x00010000;    /* bump cyl # */
                    }
                }
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv LSC2 %02x AF test/incr cyl %04x trk %02x sec %02x\n",
                    uptr->LSC, (uptr->CHS>>16)&0xffff, (uptr->CHS>>8)&0xff, uptr->CHS&0xff);
            }

            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP Write reached EOM for write to disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_CMD, dptr,
                    "DISK Write complete for read from diskfile %04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;               /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

#ifdef FAST_FOR_UTX
//          sim_activate(uptr, 10);             /* keep writing */
            sim_activate(uptr, 15);             /* keep writing */
#else
            sim_activate(uptr, 150);            /* wait to read next sector */
//          sim_activate(uptr, 300);            /* wait to read next sector */
#endif
            break;
         }
         uptr->CMD &= LMASK;                    /* remove old status bits & cmd */
         break;

    case DSK_RSL:                               /* RSL 0x32 */
        /* Read sector label zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */
        /* zero the Sector Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        len = chp->ccw_count;                   /* get number of sectors per track */
        mema = uptr->CHS+(len/30);              /* save address */

        sim_debug(DEBUG_CMD, dptr, "before RSL Sector %x len %x\n", uptr->CHS, len);

        /* read a 30 byte sector label for each sector on track */
        /* for 16 sectors per track, that is 480 bytes */
        /* for 20 sectors per track, that is 600 bytes */
        for (j=0; j<SPT(type); j++) {
            uint32  seeksec;

            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            cyl = (uptr->CHS >> 16) & 0xffff;   /* get the cylinder */
            trk = (uptr->CHS >> 8) & 0xff;      /* get the track */
            sec = uptr->CHS & 0xff;             /* get sec */
            seeksec = tstart;                   /* save sector number */

            sim_debug(DEBUG_EXP, dptr, "hsdp_srv RSL cyl %04x trk %02x sec %02x sector# %06x\n",
                cyl, trk, sec, seeksec);

            /* seek sector label area after end of track label area */
            tstart = CAPB(type) + (CYL(type)*HDS(type)*30) + (tstart*30);

            /* file offset in bytes to sector label */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv RSL SEEK on seek to %08x\n", tstart);

            /* seek to the location where we will read sector label */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error seeking sector label area at sect %06x offset %08x\n",
                    seeksec, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }

            /* read in a sector label from disk */
            if (sim_fread(buf, 1, 30, uptr->fileref) != 30) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, 30, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }

            sim_debug(DEBUG_CMD, dptr, "Sector %x label", uptr->CHS);
            /* now write sector label data */
            for (i = 0; i < 30; i++) {
                if (chan_write_byte(chsa, &buf[i])) {
                    /* we have write error, bail out */
                    uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                    sim_debug(DEBUG_CMD, dptr, "\n");
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                    break;
                }
                if (i == 16)
                    sim_debug(DEBUG_CMD, dptr, "\nSector %x label", uptr->CHS);
                sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
            }
            sim_debug(DEBUG_CMD, dptr, "\n");

            /* leave STAR "unnormalized" for diags */
            uptr->CHS++;                        /* bump to next track */
            if ((uptr->CHS & 0xff) == SPC(type))
                break;                          /* stop at last sector */
            len -= 30;                          /* count 1 sector label size */
            if (len > 0)
                continue;
            break;                              /* done */
        }

        uptr->CHS = mema;                       /* restore address */

        sim_debug(DEBUG_CMD, dptr, "after RSL Sector %x len %x\n", uptr->CHS, chp->ccw_count);

        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RSL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_WTF:                               /* 0x41 Write track format */
    case DSK_WSL:                               /* WSL 0x31 */
        len = chp->ccw_count;                   /* get number bytes to read */
        mema = uptr->CHS;                       /* save address */

        sim_debug(DEBUG_CMD, dptr, "before WSL/WTF Sector %x len %x\n", uptr->CHS, len);

        /* read a 30 byte sector label for each sector on track */
        /* for 16 sectors per track, that is 480 bytes */
        /* for 20 sectors per track, that is 600 bytes */
        for (j=0; j<SPT(type); j++) {
            uint32  seeksec;

            sim_debug(DEBUG_CMD, dptr, "Sector %x label", uptr->CHS);
            /* now read sector label data */
            for (i = 0; i < 30; i++) {
                if (chan_read_byte(chsa, &buf[i])) {
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    /* we have read error, bail out */
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                    break;
                }
                if ((i%16) == 0)
                    sim_debug(DEBUG_CMD, dptr, "\nSector %x label", uptr->CHS);
                sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
            }
            sim_debug(DEBUG_CMD, dptr, "\n");

            /* see if user trying to set invalid bit pattern */
            if ((buf[4] & 0x48) == 0x48) {      /* see if setting defective alternate trk */
                uptr->SNS |= SNS_DSKFERR;       /* disk formating error */
                uptr->CHS = mema;               /* restore address */
                chp->ccw_count = len;           /* restore number bytes to read */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                return SCPE_OK;
                break;
            }

            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            cyl = (uptr->CHS >> 16) & 0xffff;   /* get the cylinder */
            trk = (uptr->CHS >> 8) & 0xff;      /* get the track */
            sec = uptr->CHS & 0xff;             /* get sec */
            seeksec = tstart;                   /* save sector number */

            sim_debug(DEBUG_EXP, dptr, "hsdp_srv WSL/WTF cyl %04x trk %02x sec %02x sector# %06x\n",
                cyl, trk, sec, seeksec);

            /* seek sector label area after end of track label area */
            tstart = CAPB(type) + (CYL(type)*HDS(type)*30) + (tstart*30);

            /* file offset in bytes to sector label */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv WSL/WTF SEEK on seek to %08x\n", tstart);

            /* seek to the location where we will write sector label */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error seeking sector label area at sect %06x offset %08x\n",
                    seeksec, tstart);
                uptr->CHS = mema;               /* restore address */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }

            /* write sector label to disk */
            if (sim_fwrite(buf, 1, 30, uptr->fileref) != 30) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on write %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, 30, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                uptr->CHS = mema;               /* restore address */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }

            /* leave STAR "unnormalized" for diags */
            uptr->CHS++;                        /* bump to next sector */
            if ((uptr->CHS & 0xff) == SPC(type))
                break;                          /* stop at last sector */
            len -= 30;                          /* count 1 sector label size */
            if (len > 0)
                continue;
            break;                              /* done */
        }

        uptr->CHS = mema;                       /* restore address */

        sim_debug(DEBUG_CMD, dptr, "after WSL/WTF Sector %x len %x\n", uptr->CHS, chp->ccw_count);

        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd WSL/WTF done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RVL:                               /* 0x42 Read vendor label */
        /* Read track label to get defect information */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */

        mema = uptr->CHS & 0xffffff00;          /* zero sector for trk read */

        /* get file offset in sectors */
        tstart = STAR2SEC(mema, SPT(type), SPC(type));

        /* convert sector number back to chs value to sync disk for diags */
        mema = hsdpsec2star(tstart, type);
        cyl = (mema >> 16) & 0xffff;            /* get the cylinder */
        trk = (mema >> 8) & 0xff;               /* get the track */

        /* get track number */
        tstart = (cyl * HDS(type)) + trk;
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv RVL cyl %4x(%d) trk %x sec# %06x\n",
            cyl, cyl, trk, tstart);

        /* calc offset in file to track label */
        tstart = CAPB(type) + (tstart * 30);

        /* file offset in bytes */
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv RVL SEEK on seek to %06x\n", tstart);

        /* seek to the location where we will r/w track label */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv RVL, Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }

        /* read in a track label from disk */
        if ((len=sim_fread(buf, 1, 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                len, 30, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* clear out area for bad sector errors to show no errors on track */
        /* error info in bytes 10-24 */
        for (i=10; i<25; i++)
            buf[i] = 0;

        if (buf[4] == 0x08) {                   /* see if defective track */
            uptr->SNS |= SNS_DEFTRK;            /* flag as defective */
            sim_debug(DEBUG_CMD, dptr, "Track %08x is defective\n", uptr->CHS);
        }

        if (buf[4] == 0x40) {                   /* see if alternate track */
            uptr->SNS |= SNS_AATT;              /* flag as alternate */
            sim_debug(DEBUG_CMD, dptr, "Track %08x is alternate\n", uptr->CHS);
        }

        /* now write track label data to memory */
        sim_debug(DEBUG_CMD, dptr, "Track %08x label", uptr->CHS);
        for (i = 0; i < 30; i++) {
            if (chan_write_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_CMD, dptr, "\nTrack %08x label", uptr->CHS);
            sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_CMD, dptr, "\n");

        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RVL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;


    case DSK_RTL:                               /* RTL 0x52 */
        /* Read track zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */

        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        uptr->CHS &= 0xffffff00;                /* zero sector for trk read */
        mema = uptr->CHS;

        /* get file offset in sectors */
        tstart = STAR2SEC(mema, SPT(type), SPC(type));

        /* convert sector number back to chs value to sync disk for diags */
        mema = hsdpsec2star(tstart, type);
        cyl = (mema >> 16) & 0xffff;            /* get the cylinder */
        trk = (mema >> 8) & 0xff;               /* get the track */

        /* get track number */
        tstart = (cyl * HDS(type)) + trk;
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv RTL cyl %4x(%d) trk %x sec# %06x\n",
            cyl, cyl, trk, tstart);

        /* calc offset in file to track label */
        tstart = CAPB(type) + (tstart * 30);

        /* file offset in bytes */
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv RTL SEEK on seek to %06x\n", tstart);

        /* seek to the location where we will r/w track label */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv RTL, Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }

        /* read in a track label from disk */
        if ((len=sim_fread(buf, 1, 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                len, 30, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        if (buf[4] == 0x08) {                   /* see if defective track */
            uptr->SNS |= SNS_DEFTRK;            /* flag as defective */
            sim_debug(DEBUG_CMD, dptr, "Track %08x is defective\n", uptr->CHS);
        }

        if (buf[4] == 0x40) {                   /* see if alternate track */
            uptr->SNS |= SNS_AATT;              /* flag as alternate */
            sim_debug(DEBUG_CMD, dptr, "Track %08x is alternate\n", uptr->CHS);
        }

        /* now write track label data to memory */
        sim_debug(DEBUG_CMD, dptr, "Track %08x label", uptr->CHS);
        for (i = 0; i < 30; i++) {
            if (chan_write_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_CMD, dptr, "\nTrack %08x label", uptr->CHS);
            sim_debug(DEBUG_CMD, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_CMD, dptr, "\n");

        /* see if we are operating in sector replacement mode */
        /* if specified sector count will be 1 less than physical sector count */
        if (uptr->CHS == 0)                     /* see if trk 0 label read */
            uptr->LSC = buf[25];                /* save logical sector count from label */
        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RTL done LSC %02x chsa %04x count %04x completed\n",
            uptr->LSC, chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_WTL:                               /* WTL 0x51 */
        /* Write track zero to set disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */

        sim_debug(DEBUG_EXP, dptr, "hsdp_srv WTL start cnt %04x CHS %08x\n",
            chp->ccw_count, uptr->CHS);

        /* get file offset in sectors */
        tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
        /* convert sector number back to chs value to sync disk for diags */
        uptr->CHS = hsdpsec2star(tstart, type);
        uptr->CHS &= 0xffffff00;                /* zero sector for trk read */
        mema = uptr->CHS;

        cyl = (uptr->CHS >> 16) & 0xffff;       /* get the cylinder */
        trk = (uptr->CHS >> 8) & 0xff;          /* get the track */

        /* get track number */
        tstart = (cyl * HDS(type)) + trk;
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv WTL cyl %4x trk %x track# %06x CHS %08x\n",
            cyl, trk, tstart, uptr->CHS);

        /* calc offset in file to track label */
        tstart = CAPB(type) + (tstart * 30);

        /* file offset in bytes */
        sim_debug(DEBUG_EXP, dptr, "hsdp_srv WTL SEEK on seek to %06x\n", tstart);

        /* seek to the location where we will write track label */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv WTL, Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }

        sim_debug(DEBUG_EXP, dptr, "Track %08x label", uptr->CHS);
        /* now read track label data from memory */
        for (i = 0; i < 30; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have read error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_EXP, dptr, "\nTrack %08x label", uptr->CHS);
            sim_debug(DEBUG_EXP, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_EXP, dptr, "\n");

        /* see if user trying to set invalid bit pattern */
        if ((buf[4] & 0x48) == 0x48) {          /* see if setting defective alternate trk */
            uptr->SNS |= SNS_DSKFERR;           /* disk formating error */
            uptr->CHS = mema;                   /* restore address */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            break;
        }

        /* write out a track label to disk */
        if ((len=sim_fwrite(buf, 1, 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "Error %08x on write %04x of diskfile cyl %04x hds %02x sec %02x\n",
                len, 30, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
            uptr->CHS = mema;                   /* restore address */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* clear cache entry for this track */
        /* see if track label is in cache */
        for (i=0; i<TRK_CACHE; i++) {
            if (tstart == tkl_label[unit].tkl[i].track) {
                /* we found it, clear the entry */
                tkl_label[unit].tkl[i].age = 0;
                tkl_label[unit].tkl[i].track = 0;
                sim_debug(DEBUG_EXP, dptr, "WTL clearing Cache to %06x\n", tstart);
                break;
            }
        }

        uptr->CHS = mema;                       /* restore address */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd WTL chsa %04x count %04x completed CHS %08x\n",
            chsa, chp->ccw_count, uptr->CHS);

        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    default:
        sim_debug(DEBUG_CMD, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|STATUS_PCHK); /* return prog check */
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv done cmd %02x chsa %04x chs %04x/%02x/%02x\n",
        cmd, chsa, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
    return SCPE_OK;
}

/* initialize the disk */
void hsdp_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     unit = (uptr - dptr->units);        /* get the UNIT number */
    int     i = GET_TYPE(uptr->flags);
    int     cn;

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */
    uptr->CMD &= LMASK;                         /* clear out the flags but leave ch/sa */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* size in sectors */
    sim_cancel(uptr);                           /* stop any timer */
    /* reset track label cache */
    for (cn=0; cn<TRK_CACHE; cn++) {
        tkl_label[unit].tkl[cn].track = 0;
        tkl_label[unit].tkl[cn].age = 0;
    }

    sim_debug(DEBUG_EXP, dptr, "DPA init device %s on unit DPA%.1x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

/* handle rschnlio cmds for hsdp */
t_stat  hsdp_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & DSK_CMDMSK;

    sim_debug(DEBUG_EXP, dptr, "hsdp_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    hsdp_ini(uptr, 0);                          /* reset the unit */
    return SCPE_OK;
}

t_stat hsdp_reset(DEVICE *dptr)
{
    int     cn, unit;

    for(unit=0; unit < NUM_UNITS_HSDP; unit++) {
        for (cn=0; cn<TRK_CACHE; cn++) {
            tkl_label[unit].tkl[cn].track = 0;
            tkl_label[unit].tkl[cn].age = 0;
        }
    }
    /* add more reset code here */
    return SCPE_OK;
}

/* The dmap pointer is placed by the vendor or diag into the */
/* track zero label in word 3 of the 30 byte label. */
/* The disk address in track 0 label is the last sector of the disk. */
/* The vendor reserves the last cylinder, SEL diags reserve the next */
/* two, so the last track of the user area is CYL-4/HDS-1/0 */
/* The vender places the flaw information in the track and is the VDT */
/* The previous track has the media defect table and is the MDT. */
/* It is at MDT = VDT-SPT or CYL-4/HDS-2/0 */
/* The media defect table is pointed to by track 0 label in word 3 */
/* The next lower track contains the UTX media map (UMAP) and is pointed */
/* to by word 3 of sector label 1 and is placed there by the UTX prep program */
/* Add track and sector labels to disk */
int hsdp_label(UNIT *uptr, int use_strep) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      trk, cyl, sec;
    uint32      ssize = SSB(type);              /* disk sector size in bytes */
    uint32      tsize = SPT(type);              /* get track size in sectors */
    uint32      tot_tracks = TRK(type);         /* total tracks on disk */
    uint32      tot_sectors = CAP(type);        /* total number of sectors on disk */
    uint32      cap = CAP(type);                /* disk capacity in sectors */
    uint32      CHS;                            /* cyl, hds, sec format */
    uint8       label[34];                      /* track/sector label */
    int32       i, j;
                /* get sector address of vendor defect table VDT */
                /* put data = 0xf0000004 0xf4000000 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);
                /* get sector address of utx diag map (DMAP) track 0 pointer */
                /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                /* 0x9a000000 + (cyl-1), 0xf4000000 */
    int32       daddr = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);
                /* make logical */
    int32       logda = daddr*(SPT(type)-1)/(SPT(type));

                /* get sector address of utx flaw map sec 1 pointer */
                /* use this address for sec 1 label pointer */
    int32       uaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-3) * SPT(type);
                /* make logical */
    int32       logua = uaddr*(SPT(type)-1)/(SPT(type));

    /* write 30 byte track labels for all tracks on disk */
    /* tot_tracks entries will be created starting at end of disk */
    /* seek first sector after end of disk data */
    if ((sim_fseek(uptr->fileref, CAPB(type), SEEK_SET)) != 0) {
        sim_debug(DEBUG_CMD, dptr,
            "Error seeking track label area at sect %06x offset %06x\n",
            CAP(type), CAPB(type));
        return 1;
    }
    /* write track labels */
    for (i=0; i<(int)tot_tracks; i++) {

        /* zero the Track Label Buffer */
        for (j = 0; j < 30; j++)
            label[j] = 0;

        sec = i * SPT(type);                    /* get track address in sectors */
        /* convert sector number to CHS value for label */
        CHS = hsdpsec2star(sec, type);          /* get current CHS value */

        /* set buf data to current CHS values */
        if (CHS == 0) {                         /* write last address on trk 0 */
            cyl = CYL(type)-1;                  /* lcyl  cyl upper 8 bits */
            trk = HDS(type)-1;                  /* ltkn  trk */
            sec = SPT(type)-1;                  /* lid   sector ID */
        } else {
            /* write current address on other tracks */
            cyl = (CHS >> 16) & 0xffff;         /* get the cylinder */
            trk = (CHS >> 8) & 0xff;            /* get the track */
            sec = (CHS) & 0xff;                 /* get the sector */
        }

        sim_debug(DEBUG_CMD, dptr, "hsdp_label WTL STAR %08x disk geom %08x\n",
            CHS, GEOM(type));

        /* set buf data to current STAR values */
        label[0] = (cyl >> 8) & 0xff;           /* lcyl  cyl upper 8 bits */
        label[1] = cyl & 0xff;                  /* lcyl  cyl lower 8 bits */
        label[2] = trk & 0xff;                  /* ltkn  trk */
        label[3] = sec & 0xff;                  /* lid   sector ID */
        label[4] = 0x80;                        /* show good sector */
        if (i == (tot_tracks-1)) {              /* last track? */
            label[3] = 0xff;                    /* lid   show as last track label */
            label[4] |= 0x04;                   /* set last track flag */
        }

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_label WTL star %02x %02x %02x %02x\n",
            label[0], label[1], label[2], label[3]);

        /* prepare track 0 label on disk */
        if (CHS == 0) {                         /* only write dmap address in trk 0 */
            /* daddr has dmap value for track zero label */
            /* this logical or physical address must be there, */
            /* otherwise prep crashes with bad diag flaw map pointer */
            if (use_strep) {
                /* output logical diag defect map address of disk */
                label[12] = (logda >> 24) & 0xff;   /* ldeallp DMAP pointer */
                label[13] = (logda >> 16) & 0xff;
                label[14] = (logda >> 8) & 0xff;
                label[15] = (logda) & 0xff;
                printf("hsdp_label WTL logda@daddr %08x -> %08x\r\n", logda, 0);
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_label WTL logda@daddr %08x -> %08x\n", logda, 0);
            } else {
                /* output physical diag defect map address of disk */
                label[12] = (daddr >> 24) & 0xff;   /* ldeallp DMAP pointer */
                label[13] = (daddr >> 16) & 0xff;
                label[14] = (daddr >> 8) & 0xff;
                label[15] = (daddr) & 0xff;
                printf("hsdp_label WTL daddr@daddr %08x -> %08x\r\n", daddr, 0);
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_label WTL daddr@daddr %08x -> %08x\n", vaddr, 0);
            }
            /* if this is removed, UTX is unable to create newfs */
            /* get preposterous size 0 error message */
            /* address must be logical/physical */
            /* uaddr has umap value for track zero label */
            if (use_strep) {
                /* output logical umap address */
                label[16] = (logua >> 24) & 0xff;   /* lumapp UMAP lofical pointer */
                label[17] = (logua >> 16) & 0xff;
                label[18] = (logua >> 8) & 0xff;
                label[19] = (logua) & 0xff;
            } else {
                /* output physical umap address */
                label[16] = (uaddr >> 24) & 0xff;   /* lumapp UMAP physical pointer */
                label[17] = (uaddr >> 16) & 0xff;
                label[18] = (uaddr >> 8) & 0xff;
                label[19] = (uaddr) & 0xff;
            }
        }

        /* write vaddr to track label for dmap */
        if ((i*SPT(type)) == daddr) {               /* get track address in sectors */
            /* output physical vendor defect map address of disk */
            label[12] = (vaddr >> 24) & 0xff;   /* Vaddr pointer vdt */
            label[13] = (vaddr >> 16) & 0xff;
            label[14] = (vaddr >> 8) & 0xff;
            label[15] = (vaddr) & 0xff;
            printf("hsdp_label WTL vaddr@vaddr %08x -> %08x\r\n", vaddr, vaddr);
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_label WTL vaddr@vaddr %08x -> %08x\n", vaddr, vaddr);
        }

        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area too.  Byte 27 is sectors/track and byte 28 is number of heads. */
        /* Byte 26 is mode. Byte 25 is copy of byte 27. */
        label[25] = SPT(type) & 0xff;               /* save sectors per track */
        if ((use_strep) && (i == 0)) {
            label[25] = (SPT(type)-1) & 0xff;
        }
        uptr->LSC = label[25];                      /* save logical/physical sector count from label */
        label[26] = hsdp_type[type].type & 0xfd;    /* zero bits 6 in type byte */
        label[27] = label[25];                      /* same as label[25] */
        label[28] = HDS(type) & 0xff;

        if ((sim_fwrite((char *)&label, sizeof(uint8), 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "Error writing track label to sect %06x offset %06x\n",
                cap+(i*tsize), cap*ssize+(i*tsize*ssize));
            return 1;
        }
    }

    /* write 30 byte sector labels for all sectors on disk */
    /* tot_sector entries will be created starting at end of disk */
    /* plus the track label area size.  seek first sector after end */
    /* of disk track label area */
    if ((sim_fseek(uptr->fileref, CAPB(type)+TRK(type)*30, SEEK_SET)) != 0) {
        sim_debug(DEBUG_CMD, dptr,
            "Error seeking sector label area at sect %06x offset %06x\n",
            CAP(type)+TRK(type), CAPB(type)+TRK(type)*30);
        return 1;
    }

    /* zero the Sector Label Buffer */
    for (j = 0; j < 30; j++)
        label[j] = 0;

    /* convert sector number to CHS value for label */
    /* write sector labels */
    for (i=0; i<(int)tot_sectors; i++) {

        CHS = hsdpsec2star(i, type);            /* get current CHS value */

        /* set buf data to current CHS values */
        /* write current address on other tracks */
        cyl = (CHS >> 16) & 0xffff;             /* get the cylinder */
        trk = (CHS >> 8) & 0xff;                /* get the track */
        sec = (CHS) & 0xff;                     /* get the sector */

        sim_debug(DEBUG_CMD, dptr, "hsdp_label WSL STAR %08x disk geom %08x\n",
            CHS, GEOM(type));

        /* set buf data to current STAR values */
        label[0] = (cyl >> 8) & 0xff;           /* lcyl  cyl upper 8 bits */
        label[1] = cyl & 0xff;                  /* lcyl  cyl lower 8 bits */
        label[2] = trk & 0xff;                  /* ltkn  trk */
        label[3] = sec & 0xff;                  /* lid   sector ID */
        label[4] = 0x80;                        /* show good sector */

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_label WSL star %02x %02x %02x %02x\n",
            label[0], label[1], label[2], label[3]);

        label[12] = 0;
        label[13] = 0;
        label[14] = 0;
        label[15] = 0;
        label[16] = 0;
        label[17] = 0;
        label[18] = 0;
        label[19] = 0;

        /* if this is written, UTX will not be able to do a newfs */
        /* gets preposterous size 0 error */
        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area too.  Byte 27 is sectors/track and byte 28 is number of heads. */
        /* Byte 26 is mode. Byte 25 is copy of byte 27. */
        label[25] = hsdp_type[type].spt & 0xff;
        /* The UDP/DPII controllers do not use these bits, so UTX keys */
        /* on these bits to determine type of controller.  Bit 31 is set */
        /* for a HSDP and not set for the UDP/DPII */
        label[26] = hsdp_type[type].type & 0xfd;    /* zero bits 6 & leave 7 in type byte */
        label[27] = hsdp_type[type].spt & 0xff;
        label[28] = hsdp_type[type].nhds & 0xff;

        if ((sim_fwrite((char *)&label, sizeof(uint8), 30, uptr->fileref)) != 30) {
            sim_debug(DEBUG_CMD, dptr,
                "Error writing sector label to sect %06x offset %06x\n",
                i, CAPB(type)+TRK(type)*30+i*ssize);
            return 1;
        }
    }

    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    return SCPE_OK;                             /* good to go */
}

/* create the disk file for the specified device */
int hsdp_format(UNIT *uptr) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      ssize = SSB(type);              /* disk sector size in bytes */
    uint32      tsize = SPT(type);              /* get track size in sectors */
    uint32      csize = SPC(type);              /* get cylinder size in sectors */
    uint32      cyl = CYL(type);                /* get # cylinders */
    uint32      cap = CAP(type);                /* disk capacity in sectors */
    uint32      cylv = cyl;                     /* number of cylinders */
    uint8       *buff;
    int         i;
    int         use_st_format = 1;              /* assume we use s/t replacement */
    t_stat      oldsw = sim_switches;           /* save switches */

                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;          /* last sector of disk */

                /* get sector address of vendor defect table VDT */
                /* put data = 0xf0000004 0xf4000000 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);
                /* get sector address of utx diag map (DMAP) track 0 pointer */
                /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                /* 0x9a000000 + (cyl-1), 0xf4000000 */
    int32       daddr = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);
                /* make logical */
    int32       logda = daddr*(SPT(type)-1)/(SPT(type));

    int32       uaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-3) * SPT(type);

                /* NULL vendor flaw map */
    uint32      vmap[2] = {0xf0000004, 0xf4000000};

                /* NULL diag flaw map */
    uint32      pdmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | (cap-1), 0xf4000000};

    uint32      dmap[4] = {0xf0000000 | (((cap-1)*(SPT(type)-1))/(SPT(type))),
                    0x8a000000 | logda,
                    0x9a000000 | (((cap-1)*(SPT(type)-1))/(SPT(type))), 0xf4000000};

    /* see if -i or -n specified on attach command */
    if (!(sim_switches & SWMASK('N')) && !(sim_switches & SWMASK('I'))) {
        sim_switches = 0;                       /* simh tests 'N' & 'Y' switches */
        /* see if user wants to initialize the disk */
        if (!get_yn("Initialize disk? [Y] ", TRUE)) {
            sim_switches = oldsw;
            return 1;
        }
    }
    if (!get_yn("Use Sector/Track replacement format? [N] ", FALSE)) {
        use_st_format = 0;                      /* do not use s/t replacement */
    }
    sim_switches = oldsw;                       /* restore switches */

    /* get physical sector address of media defect table */
    /* VDT  286965 (819/9/0) 0x460f5 for 8887 - 823/10/35 */ 
    /* MDT  286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
    /* FMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 
    /* UMAP 286860 (819/6/0) 0x4608c for 8887 - 823/10/35 */ 

    /* get logical sector address of media defect table */
    /* VDT  278766 (819/9/0) 0x440ee for 8887 - 823/10/34 */ 
    /* MDT  278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
    /* FMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 
    /* UMAP 278664 (819/6/0) 0x44088 for 8887 - 823/10/34 Sec 0 ptr */ 

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }

    /* get buffer for track data in bytes */
    if ((buff = (uint8 *)calloc(csize*ssize, sizeof(uint8))) == 0) {
        detach_unit(uptr);
        return SCPE_ARG;
    }
    sim_debug(DEBUG_CMD, dptr,
        "Creating disk file of trk size %04x bytes, capacity %d\n",
        tsize*ssize, cap*ssize);

    /* write zeros to each track of the disk */
    for (cyl = 0; cyl < cylv; cyl++) {
        if ((sim_fwrite(buff, 1, csize*ssize, uptr->fileref)) != csize*ssize) {
            sim_debug(DEBUG_CMD, dptr,
            "Error on write to diskfile cyl %04x\n", cyl);
            free(buff);                         /* free cylinder buffer */
            buff = NULL;
            return 1;
        }
        if ((cyl % 100) == 0)
            fputc('.', stderr);
    }
    fputc('\r', stderr);
    fputc('\n', stderr);
    free(buff);                                 /* free cylinder buffer */
    buff = NULL;

    /* byte swap the buffers for dmap and umap */
    for (i=0; i<2; i++) {
        vmap[i] = (((vmap[i] & 0xff) << 24) | ((vmap[i] & 0xff00) << 8) |
            ((vmap[i] & 0xff0000) >> 8) | ((vmap[i] >> 24) & 0xff));
    }
    for (i=0; i<4; i++) {
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }
    for (i=0; i<4; i++) {
        pdmap[i] = (((pdmap[i] & 0xff) << 24) | ((pdmap[i] & 0xff00) << 8) |
            ((pdmap[i] & 0xff0000) >> 8) | ((pdmap[i] >> 24) & 0xff));
    }

    /* now seek to end of disk and write the dmap data */
    /* setup dmap pointed to by track label 0 wd[3] = (cyl-4) * spt + (spt - 1) */

    /* write dmap data to last sector on disk */
    if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
        sim_debug(DEBUG_CMD, dptr,
        "Error on last sector seek to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&pdmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing DMAP to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }

    /* seek to vendor label area VMAP */
    if ((sim_fseek(uptr->fileref, vaddr*ssize, SEEK_SET)) != 0) { /* seek VMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on vendor map seek to sect %06x offset %06x\n",
        vaddr, vaddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&vmap, sizeof(uint32), 2, uptr->fileref)) != 2) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing VMAP to sect %06x offset %06x\n",
        vaddr, vaddr*ssize);
        return 1;
    }

    /* write DMAP to daddr that is the address in trk 0 label */
    if ((sim_fseek(uptr->fileref, daddr*ssize, SEEK_SET)) != 0) { /* seek DMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on diag map seek to sect %06x offset %06x\n",
        daddr, daddr*ssize);
        return 1;
    }
    if (use_st_format) {
        if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
            sim_debug(DEBUG_CMD, dptr,
            "Error writing LDMAP to sect %06x offset %06x\n",
            daddr, daddr*ssize);
            return 1;
        }
    } else
    {
        if ((sim_fwrite((char *)&pdmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
            sim_debug(DEBUG_CMD, dptr,
            "Error writing DMAP to sect %06x offset %06x\n",
            daddr, daddr*ssize);
            return 1;
        }
    }
    printf("Disk %s has %x (%d) cyl, %x (%d) hds, %x (%d) sec\r\n",
        hsdp_type[type].name, CYL(type), CYL(type), HDS(type), HDS(type),
        SPT(type), SPT(type));
    printf("writing to vmap sec %x (%d) bytes %x (%d)\r\n",
        vaddr, vaddr, (vaddr)*ssize, (vaddr)*ssize);
    printf("writing to dmap %x (%d) %x (%d) dmap to %x (%d) %x (%d)\r\n",
       cap-1, cap-1, (cap-1)*ssize, (cap-1)*ssize,
       daddr, daddr, daddr*ssize, daddr*ssize);
    printf("writing to umap sec %x (%d) bytes %x (%d)\r\n",
        uaddr, uaddr, (uaddr)*ssize, (uaddr)*ssize);

    /* create labels for disk */
    i = hsdp_label(uptr, use_st_format);        /* label disk */

    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    return SCPE_OK;
}

/* attach the selected file to the disk */
t_stat hsdp_attach(UNIT *uptr, CONST char *file)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    CHANP           *chp = find_chanp_ptr(chsa);    /* get channel prog pointer */
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = 0;
    t_stat          r, s;
    uint32          ssize;                      /* sector size in bytes */
    uint32          info, good;
    uint8           buff[1024];
    int             i, j;
    int             use_st_format = 0;

                    /* last sector address of disk (cyl * hds * spt) - 1 */
    int32           laddr = CAP(type) - 1;      /* last sector of disk */
                    /* get sector address of utx diag map (DMAP) track 0 pointer */
                    /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                    /* 0x9a000000 + (cyl-1), 0xf4000000 */
    int32           daddr = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);
    int32           umapaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-3) * SPT(type);
                    /* defect map */
    uint32          dmap[4] = {0xf0000000 | (CAP(type)-1), 0x8a000000 | daddr,
                        0x9a000000 | (CAP(type)-1), 0xf4000000};

    for (i=0; i<4; i++) {                       /* byte swap data for last sector */
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }

    /* see if valid disk entry */
    if (hsdp_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    if (dptr->flags & DEV_DIS) {
        fprintf(sim_deb,
            "ERROR===ERROR\nHSDP device %s disabled on system, aborting\r\n",
            dptr->name);
        printf("ERROR===ERROR\nHSDP device %s disabled on system, aborting\r\n",
            dptr->name);
        return SCPE_UDIS;                       /* device disabled */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    uptr->capac = CAP(type);                    /* size in sectors */
    ssize = SSB(type);                          /* disk sector size in bytes */
    for (i=0; i<(int)ssize; i++)
        buff[i] = 0;                            /* zero the buffer */

    sim_debug(DEBUG_CMD, dptr,
        "Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        hsdp_type[type].name, hsdp_type[type].cyl, hsdp_type[type].nhds, 
        hsdp_type[type].spt, ssize, uptr->capac); /* hsdp capacity */

    printf("Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\r\n",
        hsdp_type[type].name, hsdp_type[type].cyl, hsdp_type[type].nhds,
        hsdp_type[type].spt, ssize, uptr->capac); /* hsdp capacity */

    /* see if -i or -n specified on attach command */
    if ((sim_switches & SWMASK('N')) || (sim_switches & SWMASK('I'))) {
        goto fmt;                               /* user wants new disk */
    }

    /* seek to end of disk */
    if ((sim_fseek(uptr->fileref, 0, SEEK_END)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "HSDP Disk attach SEEK end failed\n");
        printf("Disk attach SEEK end failed\r\n");
        goto fmt;                               /* not setup, go format */
    }

    s = ftell(uptr->fileref);                   /* get current file position */
    if (s == 0) {
        sim_debug(DEBUG_CMD, dptr, "HSDP Disk attach ftell failed s=%06d\n", s);
        printf("HSDP Disk attach ftell failed s=%06d\r\n", s);
        goto fmt;                                   /* not setup, go format */
    }

    if (((int)s/(int)ssize) < ((int)CAP(type))) {   /* full sized disk? */
        j = (CAP(type) - (s/ssize));            /* get # sectors to write */
        sim_debug(DEBUG_CMD, dptr,
            "Disk attach for MPX 1.X needs %04d more sectors added to disk\n", j);
        printf("Disk attach for MPX 1.X needs %04d more sectors added to disk\r\n", j);
        /* must be MPX 1.X disk, extend to MPX 3.X size */
        /* write sectors of zero to end of disk to fill it out */
        for (i=0; i<j; i++) {
            if ((r = sim_fwrite(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
                sim_debug(DEBUG_CMD, dptr, "Disk attach fread ret = %04d\n", r);
                printf("Disk attach fread ret = %04d\r\n", r);
                goto fmt;                       /* not setup, go format */
            }
        }
        s = ftell(uptr->fileref);               /* get current file position */
        sim_debug(DEBUG_CMD, dptr,
            "Disk attach MPX 1.X file extended & sized secs %06d bytes %06d\n", s/ssize, s);
        printf("Disk attach MPX 1.X  file extended & sized secs %06d bytes %06d\r\n", s/ssize, s);
    }

    /* seek last sector of disk */
    if ((sim_fseek(uptr->fileref, (CAP(type)-1)*ssize, SEEK_SET)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "HSDP Disk attach SEEK last sector failed\n");
        printf("HSDP Disk attach SEEK last sector failed\r\n");
        goto fmt;
    }

    /* see if there is disk size-1 in last sector of disk, if not add it */
    if ((r = sim_fread(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        sim_debug(DEBUG_CMD, dptr, "HSDP Disk format fread error = %04d\n", r);
        printf("HSDP Disk format fread error = %04d\r\n", r);
add_size:
        /* write dmap data to last sector on disk for mpx 1.x */
        if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
            sim_debug(DEBUG_CMD, dptr,
                "Disk Error on last sector seek to sect %06d offset %06d bytes\n",
                (CAP(type)-1), (CAP(type)-1)*ssize);
            printf("Disk Error on last sector seek to sect %06d offset %06d bytes\r\n",
                (CAP(type)-1), (CAP(type)-1)*ssize);
            goto fmt;
        }
        if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
            sim_debug(DEBUG_CMD, dptr,
                "Disk Error writing DMAP to sect %06x offset %06d bytes\n",
                (CAP(type)-1), (CAP(type)-1)*ssize);
            printf("Disk Error writing DMAP to sect %06x offset %06d bytes\r\n",
                (CAP(type)-1), (CAP(type)-1)*ssize);
            goto fmt;
        }

        /* seek last sector of disk */
        if ((sim_fseek(uptr->fileref, (CAP(type))*ssize, SEEK_SET)) != 0) {
            sim_debug(DEBUG_CMD, dptr, "Disk attach SEEK last sector failed\n");
            printf( "Disk attach SEEK last sector failed\r\n");
            goto fmt;
        }
        s = ftell(uptr->fileref);           /* get current file position */
        sim_debug(DEBUG_CMD, dptr,
            "HSDP Disk attach MPX file extended & sized secs %06d bytes %06d\n", s/ssize, s);
        printf("HSDP Disk attach MPX file extended & sized secs %06d bytes %06d\r\n", s/ssize, s);
        goto ldone;
    } else {
        /* if not disk size, go add it in for MPX, error if UTX */
        if ((buff[0] | buff[1] | buff[2] | buff[3]) == 0) {
            sim_debug(DEBUG_CMD, dptr,
                "HSDP Disk format0 buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
                buff[0], buff[1], buff[2], buff[3]);
            goto add_size;
        }
    }

    info = (buff[0]<<24) | (buff[1]<<16) | (buff[2]<<8) | buff[3];
    good = 0xf0000000 | (CAP(type)-1);
    /* check for 0xf0ssssss where ssssss is disk size-1 in sectors */
    if (info != good) {
        sim_debug(DEBUG_CMD, dptr,
            "Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
            buff[0], buff[1], buff[2], buff[3]);
        printf("Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\r\n",
            buff[0], buff[1], buff[2], buff[3]);
fmt:
        /* format the drive */
        if (hsdp_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }

ldone:
    /* see if disk has labels already, seek to sector past end of disk  */
    if ((sim_fseek(uptr->fileref, CAP(type)*ssize, SEEK_SET)) != 0) { /* seek end */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }
    i = SCPE_OK;
    /* see if disk has labels already, seek to sector past end of disk  */
    if ((r = sim_fread(buff, sizeof(uint8), 30, uptr->fileref) != 30)) {
        use_st_format = 1;
        /* the disk does not have labels, add them on */
        /* create labels for disk */
        sim_debug(DEBUG_CMD, dptr,
            "File %s attached to %s creating labels\n",
            file, hsdp_type[type].name);
        printf("File %s attached to %s creating labels\r\n",
            file, hsdp_type[type].name);
        if (!get_yn("Use Sector/Track replacement format for labels? [Y] ", TRUE)) {
            use_st_format = 0;
        }
        /* create labels for disk */
        i = hsdp_label(uptr, use_st_format);    /* label disk */
        if (i != 0) {
            detach_unit(uptr);                  /* detach if error */
            return SCPE_FMT;                    /* error */
        }
    }

    /* see if disk has labels already, seek to sector past end of disk  */
    if ((sim_fseek(uptr->fileref, CAP(type)*ssize, SEEK_SET)) != 0) { /* seek end */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }
    /* see if disk has labels already, read label for track 0  */
    if ((r = sim_fread(buff, sizeof(uint8), 30, uptr->fileref) != 30)) {
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }
    uptr->LSC = buff[25];                       /* save logical sector count from label */

    /* UTX map (NUMP) does not insert an F4 after the replacement tracks */
    /* so do it after the tracks are defined to stop halt on bootup */
    /* utxmap + 32 + 88 + (3*spare) + 1 */
    /* spare count is at utxmap + 8w (32) */
    /* get umap sector address from umapaddr */
    info = (buff[16]<<24) | (buff[17]<<16) | (buff[18]<<8) | buff[19];
    daddr = umapaddr * ssize;                   /* byte offset in file */
    if ((sim_fseek(uptr->fileref, umapaddr*ssize, SEEK_SET)) != 0) { /* seek umap */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }
    /* read umap into buff */
    if ((r = sim_fread(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }
    info = (buff[0]<<24) | (buff[1]<<16) | (buff[2]<<8) | buff[3];
    good = 0x4e554d50;                          /* NUMP */
    if (info == good) {
        /* we have a UTX umap, so fixup the data */
        if (buff[35] <= SPT(type))
            i = (127) + (buff[35]*12);          /* byte offset to where to put 0xf4 */
        else
            i = 127;                            /* only 1 track of replacement */
        buff[i] = 0xf4;                         /* set stop for UTX search */
        if ((sim_fseek(uptr->fileref, umapaddr*ssize, SEEK_SET)) != 0) { /* seek umap */
            detach_unit(uptr);                  /* detach if error */
            return SCPE_FMT;                    /* error */
        }
        if ((r = sim_fwrite(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
            detach_unit(uptr);                  /* detach if error */
            return SCPE_FMT;                    /* error */
        }
    }

    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    if (uptr->LSC == SPT(type)) {
        /* physical sectoring */
    sim_debug(DEBUG_CMD, dptr,
        "HSDP PHY %02x Attach %s cyl %d hds %d pspt %d pspc %d cap sec %d cap bytes %d\n",
        uptr->LSC, hsdp_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),
        CAP(type), CAPB(type));
    printf("HSDP PHY %02x Attach %s cyl %d hds %d pspt %d pspc %d cap sec %d cap bytes %d\r\n",
        uptr->LSC, hsdp_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),
        CAP(type), CAPB(type));
    } else {
        /* logical sectoring */
    sim_debug(DEBUG_CMD, dptr,
        "HSDP LSF %02x Attach %s cyl %d hds %d lspt %d lspc %d cap sec %d cap bytes %d\n",
        uptr->LSC, hsdp_type[type].name, CYL(type), HDS(type), SPT(type)-1, (SPT(type)-1)*HDS(type),
        (CYL(type)*HDS(type)*(SPT(type)-1)), (CYL(type)*HDS(type)*(SPT(type)-1))*ssize);
    printf("HSDP LSF %02x Attach %s cyl %d hds %d lspt %d lspc %d cap sec %d cap bytes %d\r\n",
        uptr->LSC, hsdp_type[type].name, CYL(type), HDS(type), SPT(type)-1, (SPT(type)-1)*HDS(type),
        (CYL(type)*HDS(type)*(SPT(type)-1)), (CYL(type)*HDS(type)*(SPT(type)-1))*ssize);
    }

    sim_debug(DEBUG_CMD, dptr,
        "HSDP File %s attached to %s\n",
        file, hsdp_type[type].name);
    printf("HSDP File %s attached to %s\r\n",
        file, hsdp_type[type].name);

    /* check for valid configured disk */
    /* must have valid DIB and Channel Program pointer */
    dibp = (DIB *)dptr->ctxt;                   /* get the DIB pointer */
    if ((dib_unit[chsa] == NULL) || (dibp == NULL) || (chp == NULL)) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nHSDP device %s not configured on system, aborting\n",
            dptr->name);
        printf("ERROR===ERROR\nHSDP device %s not configured on system, aborting\r\n",
            dptr->name);
        detach_unit(uptr);                      /* detach if error */
        return SCPE_UNATT;                      /* error */
    }
    set_devattn(chsa, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat hsdp_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat hsdp_boot(int32 unit_num, DEVICE * dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr, "HSDP Boot dev/unit %x\n", GET_UADDR(uptr->CMD));

    /* see if device disabled */
    if (dptr->flags & DEV_DIS) {
        printf("ERROR===ERROR\r\nHSDP device %s disabled on system, aborting\r\n",
            dptr->name);
        return SCPE_UDIS;                       /* device disabled */
    }

    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr, "HSDP Boot attach error dev/unit %04x\n",
            GET_UADDR(uptr->CMD));
        printf("HSDP Boot attach error dev/unit %04x\n", GET_UADDR(uptr->CMD));
        return SCPE_UNATT;                      /* attached? */
    }
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */

    /* now boot the disk */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return chan_boot(GET_UADDR(uptr->CMD), dptr); /* boot the ch/sa */
}

/* Disk option setting commands */
/* set the disk type attached to unit */
t_stat hsdp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; hsdp_type[i].name != 0; i++) {
        if (strcmp(hsdp_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear old type */
            uptr->flags |= SET_TYPE(i);         /* set new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat hsdp_get_type(FILE *st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
            return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(hsdp_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat hsdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    int i;
    fprintf (st, "SEL 8064 High Speed Disk Processor\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; hsdp_type[i].name != 0; i++) {
        fprintf(st, "%s", hsdp_type[i].name);
        if (hsdp_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; hsdp_type[i].name != 0; i++) {
        int32   size = CAPB(i);                 /* disk capacity in bytes */
        size /= 1024;                           /* make KB */
        size = (10 * size) / 1024;              /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            hsdp_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *hsdp_description (DEVICE *dptr)
{
    return "SEL 8064 High Speed Disk Processor";
}

#endif
