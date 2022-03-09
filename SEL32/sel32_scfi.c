/* sel32_scfi.c: SEL-32 SCFI SCSI Disk Controller

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

#if NUM_DEVS_SCFI > 0

#define UNIT_SCFI   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

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
#define SPT(type)               (scfi_type[type].spt)
/* get sectors per cylinder for specified type */
#define SPC(type)               (scfi_type[type].spt*scfi_type[type].nhds)
/* get number of tracks for specified type */
#define TRK(type)               (scfi_type[type].cyl*scfi_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (scfi_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (scfi_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (scfi_type[type].ssiz*4)
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

Memory attribute register layout
bits 0-7 - Flags
        bits 0&1 - 00=Reserved, 01=MHD, 10=FHD, 11=MHD with FHD option
        bit  2   - 1=Cartridge module drive
        bit  3   - 0=Reserved
        bit  4   - 1=Drive not present
        bit  5   - 1=Dual Port
        bit  6&7 - 0=Reserved
bits 8-15 - sector count (sectors per track)(F16=16, F20=20)
bits 16-23 - MHD Head count (number of heads on MHD)
bits 24-31 - FHD head count (number of heads on FHD or number head on FHD option of
    mini-module)
*/

/* 224 word INCH Buffer layout */
/* 128 word subchannel status storage (SST) */
/*  66 words of program status queue (PSQ) */
/*  26 words of scratchpad */
/*   4 words of label buffer registers */

#define CMD   u3
/* u3 */
/* in u3 is device command code and status */
#define DSK_CMDMSK      0x00ff                  /* Command being run */
#define DSK_STAR        0x0100                  /* STAR value in u4 */
#define DSK_NU2         0x0200                  /*                    */
#define DSK_READDONE    0x0400                  /* Read finished, end channel */
#define DSK_ENDDSK      0x0800                  /* Sensed end of disk */
#define DSK_SEEKING     0x1000                  /* Disk is currently seeking */
#define DSK_READING     0x2000                  /* Disk is reading data */
#define DSK_WRITING     0x4000                  /* Disk is writing data */
#define DSK_BUSY        0x8000                  /* Disk is busy */
/* commands */
#define DSK_INCH        0x00                    /* Initialize channel */
#define DSK_ICH         0xFF                    /* Initialize controller */
#define DSK_INCH2       0xF0                    /* Initialize channel for processing */
#define DSK_WD          0x01                    /* Write data */
#define DSK_RD          0x02                    /* Read data */
#define DSK_NOP         0x03                    /* No operation */
#define DSK_SNS         0x04                    /* Sense */
#define DSK_SCK         0x07                    /* Seek cylinder, track, sector */
#define DSK_TIC         0x08                    /* Transfer in channel */
#define DSK_FNSK        0x0B                    /* Format for no skip */
#define DSK_LPL         0x13                    /* Lock protected label */
#define DSK_LMR         0x1F                    /* Load mode register */
#define DSK_RES         0x23                    /* Reserve */
#define DSK_WSL         0x31                    /* Write sector label */
#define DSK_RSL         0x32                    /* Read sector label */
#define DSK_REL         0x33                    /* Release */
#define DSK_XEZ         0x37                    /* Rezero */
#define DSK_POR         0x43                    /* Priority Override */
#define DSK_IHA         0x47                    /* Increment head address */
#define DSK_SRM         0x4F                    /* Set reserve track mode */
#define DSK_WTL         0x51                    /* Write track label */
#define DSK_RTL         0x52                    /* Read track label */
#define DSK_XRM         0x5F                    /* Reset reserve track mode */
#define DSK_RAP         0xA2                    /* Read angular positions */
#define DSK_TESS        0xAB                    /* Test STAR (subchannel target address register) */
#define DSK_REC         0xB2                    /* Read ECC correction mask */
#define DSK_ICH         0xFF                    /* Initialize Controller */

#define STAR    u4
/* u4 - sector target address register (STAR) */
/* Holds the current cylinder, head(track), sector */
#define DISK_CYL        0xFFFF0000              /* cylinder mask */
#define DISK_TRACK      0x0000FF00              /* track mask */
#define DISK_SECTOR     0x000000ff              /* sector mask */

#define SNS     u5
/* u5 */
/* Sense byte 0  - mode register */
#define SNS_DROFF       0x80000000              /* Drive Carriage will be offset */
#define SNS_TRKOFF      0x40000000              /* Track offset: 0=positive, 1=negative */
#define SNS_RDTMOFF     0x20000000              /* Read timing offset = 1 */
#define SNS_RDSTRBT     0x10000000              /* Read strobe timing: 1=positive, 0=negative */
#define SNS_DIAGMOD     0x08000000              /* Diagnostic Mode ECC Code generation and checking */
#define SNS_RSVTRK      0x04000000              /* Reserve Track mode: 1=OK to write, 0=read only */
#define SNS_FHDOPT      0x02000000              /* FHD or FHD option = 1 */
#define SNS_RESERV      0x01000000              /* Reserved */

/* Sense byte 1 */
#define SNS_CMDREJ      0x800000                /* Command reject */
#define SNS_INTVENT     0x400000                /* Unit intervention required */
#define SNS_SPARE1      0x200000                /* Spare */
#define SNS_EQUCHK      0x100000                /* Equipment check */
#define SNS_DATCHK      0x080000                /* Data Check */
#define SNS_OVRRUN      0x040000                /* Data overrun/underrun */
#define SNS_DSKFERR     0x020000                /* Disk format error */
#define SNS_DEFTRK      0x010000                /* Defective track encountered */

/* Sense byte 2 */
#define SNS_LAST        0x8000                  /* Last track flag encountered */
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
#define SNS_BUCK        0x20                    /* Buffer check */
#define SNS_ECCS        0x10                    /* ECC error in sector label */
#define SNS_ECCD        0x08                    /* ECC error in data */
#define SNS_ECCT        0x04                    /* ECC error in track label */
#define SNS_RTAE        0x02                    /* Reserve track access error */
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
/* u6 holds the current cyl, hd, sec for the drive */

/* this attribute information is provided by the INCH command */
/* for each device and is not used.  It is reconstructed from */
/* the scfi_t structure data for the assigned disk */
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
bits 24-31 - FHD head count (number of heads on FHD or number head on FHD option of
    mini-module)
*/

/* Not Used     up7 */

/* disk definition structure */
struct scfi_t
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

/*                         BM SIZ TOT AL U */
/* DF0B, 1,  8, 20, 192, 1, 1712,  54760, SF336 */
/* DF0C, 1,  8, 20, 192, 1, 4082, 130612, SG102 */
/* DF0D, 1,  8, 20, 192, 1, 3491, 111705, SG654 */
/* */
/* DF0B, 1,  8, 20, 192, 1, 1711,  54752, SG038 */
/* DF0C, 1, 16, 20, 192, 1, 2732,  87424, SG120 */
/* DF0D, 1,  8, 20, 192, 1, 3491, 111680, SG076 */
/* DF0E, 1, 16, 20, 192, 1, 2732,  87424, SG121 */
scfi_type[] =
{
    /* Class F Disc Devices */
    /* MPX SCSI disks for SCFI controller */
    {"MH1GB",  1, 192, 40, 34960, 34960, 0x40},   /*0 69920 1000M */
    {"SG038",  1, 192, 20, 21900, 21900, 0x40},   /*1 21900   38M */
    {"SG120",  1, 192, 40, 34970, 34970, 0x40},   /*2 69940 1200M */
    {"SG076",  1, 192, 20, 46725, 46725, 0x40},   /*3 46725  760M */
    {"SG121",  1, 192, 20, 34970, 34970, 0x40},   /*4 69940 1210M */
    {"SD150",  9, 192, 24,   963,   967, 0x40},   /*5  8820  150M  208872 sec */
    {"SD300",  9, 192, 32,  1405,  1409, 0x40},   /*6  8828  300M  396674 sec */
    {"SD700", 15, 192, 35,  1542,  1546, 0x40},   /*7  8833  700M  797129 sec */
    {"SD1200",15, 192, 49,  1927,  1931, 0x40},   /*8  8835 1200M 1389584 sec */
    {NULL, 0}
};

t_stat  scfi_preio(UNIT *uptr, uint16 chan);
t_stat  scfi_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd);
t_stat  scfi_haltio(UNIT *uptr);
t_stat  scfi_iocl(CHANP *chp, int32 tic_ok);
t_stat  scfi_srv(UNIT *uptr);
t_stat  scfi_boot(int32 unitnum, DEVICE *dptr);
void    scfi_ini(UNIT *, t_bool);
t_stat  scfi_rschnlio(UNIT *uptr);
t_stat  scfi_reset(DEVICE *);
t_stat  scfi_attach(UNIT *, CONST char *);
t_stat  scfi_detach(UNIT *);
t_stat  scfi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  scfi_get_type(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
t_stat  scfi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const   char  *scfi_description (DEVICE *dptr);
extern  uint32  inbusy;
extern  uint32  outbusy;
extern  uint32  readfull(CHANP *chp, uint32 maddr, uint32 *word);
extern  int     irq_pend;                       /* go scan for pending int or I/O */
extern  UNIT    itm_unit;
extern  uint32  PSD[];                          /* PSD */
extern  uint32  cont_chan(uint16 chsa);

/* channel program information */
CHANP           sda_chp[NUM_UNITS_SCFI] = {0};

MTAB            scfi_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &scfi_set_type, &scfi_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0},
};

UNIT            sda_unit[] = {
/* SET_TYPE(2) SG120 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x400)},  /* 0 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x410)},  /* 1 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x420)},  /* 2 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x430)},  /* 3 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x440)},  /* 4 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x450)},  /* 5 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x460)},  /* 6 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x470)},  /* 7 */
};

DIB             sda_dib = {
    scfi_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    scfi_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    scfi_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    scfi_rschnlio,  /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    scfi_iocl,      /* t_stat (*iocl_io)(CHANP *chp, int32 tik_ok)) */  /* Process IOCL */
    scfi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sda_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sda_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_SCFI, /* uint8 numunits */                        /* number of units defined */
    0x70,           /* uint8 mask */                            /* 8 devices - device mask */
    0x0400,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0},            /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          sda_dev = {
    "SDA", sda_unit, NULL/*sda_reg*/, scfi_mod,
    NUM_UNITS_SCFI, 16, 24, 4, 16, 32,
    NULL, NULL, &scfi_reset, &scfi_boot, &scfi_attach, &scfi_detach,
    /* ctxt is the DIB pointer */
    &sda_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scfi_help, NULL, NULL, &scfi_description
};

#if NUM_DEVS_SCFI > 1
/* channel program information */
CHANP           sdb_chp[NUM_UNITS_SCFI] = {0};

UNIT            sdb_unit[] = {
/* SET_TYPE(0) DM1GB */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC00)},  /* 0 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC10)},  /* 1 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC20)},  /* 2 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC30)},  /* 3 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC40)},  /* 4 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC50)},  /* 5 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC60)},  /* 6 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC70)},  /* 7 */
};

DIB             sdb_dib = {
    scfi_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    scfi_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    scfi_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    scfi_rschnlio,  /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    scfi_iocl,      /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    scfi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sdb_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sdb_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_SCFI, /* uint8 numunits */                        /* number of units defined */
    0x70,           /* uint8 mask */                            /* 16 devices - device mask */
    0x0C00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0},            /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          sdb_dev = {
    "SDB", sdb_unit, NULL, /*sdb_reg*/, scfi_mod,
    NUM_UNITS_SCFI, 16, 24, 4, 16, 32,
    NULL, NULL, &scfi_reset, &scfi_boot, &scfi_attach, &scfi_detach,
    /* ctxt is the DIB pointer */
    &sdb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scfi_help, NULL, NULL, &scfi_description
};
#endif

/* convert sector disk address to star values (c,h,s) */
uint32 scfisec2star(uint32 daddr, int type)
{
    uint32 sec = daddr % scfi_type[type].spt;   /* get sector value */
    uint32 spc = scfi_type[type].nhds * scfi_type[type].spt; /* sec per cyl */
    uint32 cyl = daddr / spc;                   /* cylinders */
    uint32 hds = (daddr % spc) / scfi_type[type].spt;   /* heads */

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* start a disk operation */
t_stat scfi_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_DETAIL, dptr,
        "scfi_preio CMD %08x unit %02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }

    sim_debug(DEBUG_DETAIL, dptr,
        "scfi_preio unit %02x chsa %04x OK\n", unit, chsa);
    return SCPE_OK;                             /* good to go */
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
t_stat  scfi_iocl(CHANP *chp, int32 tic_ok)
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
                "scfi_iocl iocd bad address chsa %02x caw %06x\n",
                chsa, chp->chan_caw);
            chp->ccw_addr = chp->chan_caw;      /* set the bad iocl address */
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid iocd addr */
            uptr->SNS |= SNS_INAD;              /* invalid address status */
            return 1;                           /* error return */
        }
    }
loop:
    sim_debug(DEBUG_EXP, dptr,
        "scfi_iocl @%06x entry PSD %08x chan_status[%04x] %04x\n",
        chp->chan_caw, PSD[0], chan, chp->chan_status);

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR) {      /* check channel error status */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl ERROR1 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* return error */
    }

    /* Read in first CCW */
    if (readfull(chp, chp->chan_caw, &word1) != 0) { /* read word1 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl ERROR2 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Read in second CCW */
    if (readfull(chp, chp->chan_caw+4, &word2) != 0) { /* read word2 from memory */
        chp->chan_status |= STATUS_PCHK;        /* memory read error, program check */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl ERROR3 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    sim_debug(DEBUG_CMD, dptr,
        "scfi_iocl @%06x read ccw chan %02x IOCD wd 1 %08x wd 2 %08x\n",
        chp->chan_caw, chan, word1, word2);

    chp->chan_caw = (chp->chan_caw & 0xfffffc) + 8; /* point to next IOCD */

    /* Check if we had data chaining in previous iocd */
    /* if we did, use previous cmd value */
    if (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
       (chp->ccw_flags & FLAG_DC)) {            /* last IOCD have DC set? */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_iocl @%06x DO DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
    } else
        chp->ccw_cmd = (word1 >> 24) & 0xff;    /* set new command from IOCD wd 1 */

    if (!MEM_ADDR_OK(word1 & MASK24)) {         /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;        /* bad, program check */
        uptr->SNS |= SNS_INAD;                  /* invalid address status */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl bad IOCD1 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    chp->ccw_count = word2 & 0xffff;            /* get 16 bit byte count from IOCD WD 2 */

    /* validate the commands for the disk */
    switch (chp->ccw_cmd) {
    case DSK_WD: case DSK_RD: case DSK_INCH: case DSK_NOP: case DSK_ICH:
    case DSK_SCK: case DSK_XEZ: case DSK_LMR: case DSK_WSL: case DSK_RSL:
    case DSK_IHA: case DSK_WTL: case DSK_RTL: case DSK_RAP: case DSK_TESS:  
    case DSK_FNSK: case DSK_REL: case DSK_RES: case DSK_POR: case DSK_TIC:
    case DSK_REC:
    case DSK_SNS:   
        break;
    default:
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid cmd */
        uptr->SNS |= SNS_CMDREJ;                /* cmd rejected */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl bad cmd chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    if (chp->chan_info & INFO_SIOCD) {          /* see if 1st IOCD in channel prog */
        /* 1st command can not be a TIC or NOP */
        if ((chp->ccw_cmd == DSK_NOP) || (chp->ccw_cmd == CMD_TIC)) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid tic */
            uptr->SNS |= SNS_CMDREJ;            /* cmd rejected status */
            sim_debug(DEBUG_EXP, dptr,
                "scfi_iocl TIC/NOP bad cmd chan_status[%04x] %04x\n",
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
                    "scfi_iocl tic cmd bad address chan %02x tic caw %06x IOCD wd 1 %08x\n",
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
                "scfi_iocl tic cmd ccw chan %02x tic caw %06x IOCD wd 1 %08x\n",
                chan, chp->chan_caw, word1);
            goto loop;                          /* restart the IOCD processing */
        }
        chp->chan_caw = word1 & MASK24;         /* get new IOCD address */
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid tic */
        uptr->SNS |= SNS_CMDREJ;                /* cmd rejected status */
        if (((word1 & MASK24) == 0) || (word1 & 0x3))
            uptr->SNS |= SNS_INAD;              /* invalid address status */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl TIC ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* Check if we had data chaining in previous iocd */
    if ((chp->chan_info & INFO_SIOCD) ||        /* see if 1st IOCD in channel prog */
        (((chp->chan_info & INFO_SIOCD) == 0) && /* see if 1st IOCD in channel prog */
        ((chp->ccw_flags & FLAG_DC) == 0))) {   /* last IOCD have DC set? */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_iocl @%06x DO CMD No DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
        docmd = 1;                              /* show we have a command */
    }

    /* Set up for this command */
    chp->ccw_flags = (word2 >> 16) & 0xf000;    /* get flags from bits 0-4 of WD 2 of IOCD */
    chp->chan_status = 0;                       /* clear status for next IOCD */
    /* make a 24 bit address */
    chp->ccw_addr = word1 & MASK24;             /* set the data/seek address */

    /* validate parts of IOCD2 that are reserved */    
    if (word2 & 0x0fff0000) {                   /* bits 5-15 must be zero */
        chp->chan_status |= STATUS_PCHK;        /* program check for invalid iocd */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_iocl IOCD2 chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                               /* error return */
    }

    /* DC can only be used with a read/write cmd */
    if (chp->ccw_flags & FLAG_DC) {
        if ((chp->ccw_cmd != DSK_RD) && (chp->ccw_cmd != DSK_WD)) {
            chp->chan_status |= STATUS_PCHK;    /* program check for invalid DC */
            uptr->SNS |= SNS_CHER;              /* chaining error */
            sim_debug(DEBUG_EXP, dptr,
                "scfi_iocl DC ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
            return 1;                           /* error return */
        }
    }

    chp->chan_byte = BUFF_BUSY;                 /* busy & no bytes transferred yet */

    sim_debug(DEBUG_XIO, dptr,
        "scfi_iocl @%06x read docmd %01x addr %06x count %04x chan %04x ccw_flags %04x\n",
        chp->chan_caw, docmd, chp->ccw_addr, chp->ccw_count, chan, chp->ccw_flags);

    if (docmd) {                                /* see if we need to process a command */
        DIB *dibp = dib_unit[chp->chan_dev];    /* get the DIB pointer */
 
        uptr = chp->unitptr;                    /* get the unit ptr */
        if (dibp == 0 || uptr == 0) {
            chp->chan_status |= STATUS_PCHK;    /* program check if it is */
            return 1;                           /* if none, error */
        }

        sim_debug(DEBUG_XIO, dptr,
            "scfi_iocl @%06x before start_cmd chan %04x status %04x count %04x SNS %08x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count, uptr->u5);

        /* call the device startcmd function to process the current command */
        /* just replace device status bits */
        chp->chan_info &= ~INFO_CEND;           /* show chan_end not called yet */
        devstat = dibp->start_cmd(uptr, chan, chp->ccw_cmd);
        chp->chan_status = (chp->chan_status & 0xff00) | devstat;
        chp->chan_info &= ~INFO_SIOCD;          /* show not first IOCD in channel prog */

        sim_debug(DEBUG_XIO, dptr,
            "scfi_iocl @%06x after start_cmd chan %04x status %08x count %04x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count);

        /* see if bad status */
        if (chp->chan_status & (STATUS_ATTN|STATUS_ERROR)) {
            chp->chan_status |= STATUS_CEND;    /* channel end status */
            chp->ccw_flags = 0;                 /* no flags */
            chp->chan_byte = BUFF_NEXT;         /* have main pick us up */
            sim_debug(DEBUG_EXP, dptr,
                "scfi_iocl bad status chsa %04x status %04x cmd %02x\n",
                chsa, chp->chan_status, chp->ccw_cmd);
            /* done with command */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "scfi_iocl ERROR return chsa %04x status %08x\n",
                chp->chan_dev, chp->chan_status);
            return 1;                           /* error return */
        }
        /* NOTE this code needed for MPX 1.X to run! */
        /* see if command completed */
        /* we have good status */
        if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
            uint16  chsa = GET_UADDR(uptr->u3); /* get channel & sub address */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_XIO, dptr,
                "scfi_iocl @%06x FIFO #%1x cmd complete chan %04x status %04x count %04x\n",
                chp->chan_caw, FIFO_Num(chsa), chan, chp->chan_status, chp->ccw_count);
        }
    }
    /* the device processor returned OK (0), so wait for I/O to complete */
    /* nothing happening, so return */
    sim_debug(DEBUG_XIO, dptr,
        "scfi_iocl @%06x return, chan %04x status %04x count %04x irq_pend %1x\n",
        chp->chan_caw, chan, chp->chan_status, chp->ccw_count, irq_pend);
    return 0;                                   /* good return */
}

t_stat scfi_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int32       unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "scfi_startcmd chsa %04x unit %02x cmd %02x CMD %08x\n",
        chsa, unit, cmd, uptr->CMD);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        sim_debug(DEBUG_EXP, dptr, "scfi_startcmd unit %02x not attached\n", unit);
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        sim_debug(DEBUG_EXP, dptr, "scfi_startcmd unit %02x busy\n", unit);
        uptr->CMD |= DSK_BUSY;                  /* Flag we are busy */
        return SNS_BSY;
    }
    uptr->SNS2 |= SNS_USEL;                     /* unit selected */
    sim_debug(DEBUG_CMD, dptr,
        "scfi_startcmd CMD continue unit=%02x cmd %02x iocla %06x cnt %04x\n",
        unit, cmd, chp->chan_caw, chp->ccw_count);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH cmd 0x0 */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_startcmd starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chp->chan_inch_addr, chsa, chp->ccw_addr, chp->ccw_count);

        uptr->SNS &= ~SNS_CMDREJ;               /* not rejected yet */
        uptr->CMD |= DSK_INCH2;                 /* use 0xF0 for inch, just need int */
#ifdef FAST_FOR_UTX
        sim_activate(uptr, 20);                 /* start things off */
#else
        sim_activate(uptr, 250);                /* start things off */
#endif
        return SCPE_OK;                         /* good to go */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        if ((cmd == DSK_NOP) &&
            (chp->chan_info & INFO_SIOCD)) {    /* is NOP 1st IOCD? */
            chp->chan_caw -= 8;                 /* backup iocd address for diags */
            break;                              /* yes, can't be 1st */
        }
    case DSK_ICH:                               /* 0xFF Initialize controller */
    case DSK_SCK:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x37 */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register 0x1F */
    case DSK_WSL:                               /* WSL 0x31 */
    case DSK_RSL:                               /* RSL 0x32 */
    case DSK_IHA:                               /* 0x47 Increment head address */
    case DSK_WTL:                               /* WTL 0x51 */
    case DSK_RTL:                               /* RTL 0x52 */
    case DSK_RAP:                               /* 0xA2 Read angular positions */
    case DSK_TESS:                              /* TESS 0xAB Test STAR */
    case DSK_FNSK:                              /* 0x0B Format for no skip */
    case DSK_REC:                               /* 0xB2 Read ECC correction mask */
    case DSK_RES:                               /* 0x23 Reserve */
    case DSK_REL:                               /* 0x33 Release */
        uptr->SNS &= ~MASK24;                   /* clear data  & leave mode */
        uptr->SNS2 = (SNS_UNR|SNS_ONC|SNS_USEL);/* reset status to on cyl & ready */
    case DSK_SNS:                               /* Sense 0x04 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_startcmd starting disk cmd %02x chsa %04x\n",
            cmd, chsa);
#ifdef FAST_FOR_UTX
        sim_activate(uptr, 20);                 /* start things off */
#else
        sim_activate(uptr, 250);                /* start things off */
#endif
        return SCPE_OK;                         /* good to go */
        break;
    }

    sim_debug(DEBUG_EXP, dptr,
        "scfi_startcmd done with bad disk cmd %02x chsa %04x SNS %08x\n",
        cmd, chsa, uptr->SNS);
    uptr->SNS |= SNS_CMDREJ;                    /* cmd rejected */
    return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* return error */
}

/* Handle haltio transfers for disk */
t_stat  scfi_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         cmd = uptr->CMD & DSK_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, dptr,
        "scfi_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & DSK_CMDMSK) != 0) {        /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n",
            chsa, cmd, chp->ccw_count);
        /* stop any I/O and post status and return error status */
        sim_cancel(uptr);                       /* clear the input timer */
        chp->ccw_count = 0;                     /* zero the count */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);   /* stop any chaining */
        uptr->CMD &= LMASK;                     /* make non-busy */
        uptr->SNS2 |= (SNS_ONC|SNS_UNR);        /* on cylinder & ready */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* force end */
        return CC1BIT | SCPE_IOERR;             /* DIAGS want just an interrupt */
    }
    uptr->CMD &= LMASK;                         /* make non-busy */
    uptr->SNS2 |= (SNS_ONC|SNS_UNR);            /* on cylinder & ready */
    sim_debug(DEBUG_CMD, dptr,
        "scfi_haltio HIO I/O not busy chsa %04x cmd = %02x\n", chsa, cmd);
    return CC1BIT | SCPE_OK;                    /* not busy return */
}

/* Handle processing of disk requests. */
t_stat scfi_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    CHANP           *chp = find_chanp_ptr(chsa);/* get channel prog pointer */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    uint32          tcyl=0, trk=0, cyl=0, sec=0;
    int             unit = (uptr - dptr->units);
    int             len = chp->ccw_count;
    int             i;
    uint32          mema;                       /* memory address */
    uint8           ch;
    uint16          ssize = scfi_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32          tstart;
    uint8           buf[1024];
    uint8           buf2[1024];

    sim_debug(DEBUG_CMD, dptr,
        "scfi_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS) {                   /* we are completed with unit check status */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
    }

    sim_debug(DEBUG_CMD, dptr,
        "scfi_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);

    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_ICH:                               /* 0xFF Initialize controller */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv cmd CONT ICH %06x chsa %04x addr %06x count %04x completed\n",
            chp->chan_inch_addr, chsa, mema, chp->ccw_count);
        if (len == 0x14) {
            /* read all 20 bytes, stopping every 4 bytes to make words */
            /* the first word has the inch buffer address */
            /* the next 4 words have drive data for each unit */
            /* WARNING 4 drives must be defined for this controller */
            /* so we will not have a map fault */
            for (i=0; i < 20; i++) {
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
                        mema = (buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | (buf[3]);
                        sim_debug(DEBUG_CMD, dptr,
                            "scfi_srv cmd CONT ICH %06x chsa %04x mema %06x completed\n",
                            chp->chan_inch_addr, chsa, mema);
                    } else {
                        /* drive attribute registers */
                        /* may want to use this later */
                        /* clear warning errors */
                        tstart = (buf[i-3]<<24) | (buf[i-2]<<16) | (buf[i-1]<<8) | (buf[i]);
                        sim_debug(DEBUG_CMD, dptr,
                            "scfi_srv cmd CONT ICH %06x chsa %04x data %06x completed\n",
                            chp->chan_inch_addr, chsa, tstart);
                    }
                }
            }
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
            break;
        }

        /* to use this inch method, byte count must be 896 */
        if (len != 896) {
                /* we have invalid count, error, bail out */
                uptr->SNS |= SNS_CMDREJ;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
        }
        /* now call set_inch() function to write and test inch buffer addresses */
        /* 1-224 wd buffer is provided, status is 128 words offset from start */
        mema += (128*4);                        /* offset to inch buffers */
        tstart = set_inch(uptr, mema, 33);      /* new address of 33 entries */
        if ((tstart == SCPE_MEM) || (tstart == SCPE_ARG)) { /* any error */
            /* we have error, bail out */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

    case DSK_INCH2:                             /* use 0xF0 for inch, just need int */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chp->chan_inch_addr, chsa, chp->ccw_addr, chp->ccw_count);

        /* mema has IOCD word 1 contents.  For the disk processor it contains */
        /* a pointer to the INCH buffer followed by 8 drive attribute words that */
        /* contains the flags, sector count, MHD head count, and FHD count */
        /* len has the byte count from IOCD wd2 and should be 0x24 (36) */
        /* the INCH buffer address must be set for the parent channel as well */
        /* as all other devices on the channel.  Call set_inch() to do this for us */
        /* just return OK and channel software will use u4 as status buffer addr */

        /* see if New SCFI controller */
        if (len == 4) {
            /* get just the INCH addr */
            for (i=0; i < 4; i++) {
                if (chan_read_byte(chsa, &buf[i])) {
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;      /* invalid address */
                    /* we have error, bail out */
                    uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                    uptr->SNS |= SNS_CMDREJ;
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    break;
                }
            }
            /* inch buffer address */
            mema = (buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | (buf[3]);
            goto gohere;
        }

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
                if (i == 3)
                    /* inch buffer address */
                    mema = (buf[0]<<24) | (buf[1]<<16) |
                        (buf[2]<<8) | (buf[3]);
                else
                    /* drive attribute registers */
                    /* may want to use this later */
                    /* clear warning errors */
                    tstart = (buf[i-3]<<24) | (buf[i-2]<<16)
                        | (buf[i-1]<<8) | (buf[i]);
            }
        }
gohere:
        /* now call set_inch() function to write and test inch buffer addresses */
        /* 1-224 wd buffer is provided, status is 128 words offset from start */
        mema += (128*4);                        /* offset to inch buffers */
        i = set_inch(uptr, mema, 33);           /* new address of 33 entries */
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) { /* any error */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv cmd INCH %06x chsa %04x addr %06x count %04x completed\n",
            chp->chan_inch_addr, chsa, mema, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RES:                               /* 0x23 Reserve */
    case DSK_REL:                               /* 0x33 Release */
    case DSK_POR:                               /* 0x43 Priority Override */
    case DSK_REC:                               /* 0xB2 Read ECC correction mask */
    case DSK_TESS:                              /* 0xAB Test STAR (subchannel target address register) */
    case DSK_FNSK:                              /* 0x0B Format for no skip */
    case DSK_RAP:                               /* 0xA2 Read angular positions */
    case DSK_IHA:                               /* 0x47 Increment head address */
    case DSK_RSL:                               /* RSL 0x32 */
    case DSK_WSL:                               /* WSL 0x31 write sector labels */
    case DSK_RTL:                               /* RTL 0x52 */
    case DSK_WTL:                               /* WTL 0x51 */
    case DSK_NOP:                               /* NOP 0x03 */
        /* diags want chan prog check and cmd reject if 1st cmd of IOCL */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x04 */                     /* Sense */
        sim_debug(DEBUG_CMD, dptr, "scfi_startcmd CMD sense\n");

        /* count must be 12 or 14, if not prog check */
        if (len != 12 && len != 14) {
            sim_debug(DEBUG_CMD, dptr,
                "scfi_srv Sense bad count unit=%02x count%04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }
        /* bytes 0,1 - Cyl entry from CHS reg */
        ch = (uptr->CHS >> 24) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense CHS b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->CHS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense CHS b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from CHS reg */
        ch = (uptr->CHS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense CHS b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from CHS reg */
        ch = (uptr->CHS) & 0xff;
        sec = ch;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense CHS b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 4 - mode reg, byte 0 of SNS  */
        ch = (uptr->SNS >> 24) & 0xff;          /* return the sense data */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
        ch = (uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 8-11 - drive mode register entries from assigned disk */
        ch = scfi_type[type].type & 0x40;       /* zero bits 0, 2-7 in type byte */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv datr unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = scfi_type[type].spt & 0xff;        /* get sectors per track */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv datr unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = scfi_type[type].nhds & 0xff;       /* get # MHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv datr unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = 0;                                 /* no FHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv datr unit=%02x 4 %02x\n",
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
            sim_debug(DEBUG_DETAIL, dptr, "scfi_srv dsr unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);

            uptr->SNS2 |= (SNS_ONC|SNS_UNR);    /* on cylinder & ready */
            ch = uptr->SNS2 & 0xff;             /* drive on cylinder and ready for now */
            sim_debug(DEBUG_DETAIL, dptr, "scfi_srv dsr unit=%02x 2 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);
        }
        uptr->SNS &= 0xff000000;                /* reset status */
        uptr->SNS2 = 0;                         /* reset status */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if (STAR2CYL(uptr->STAR) == STAR2CYL(uptr->CHS)) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr,
                    "scfi_srv seek on cylinder unit %02x new %04x old %04x\n",
                    unit, uptr->STAR>>16, uptr->CHS>>16);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS2 |= (SNS_SEND|SNS_ONC);   /* On cylinder & seek done */
                /* we have already seeked to the required sector */
                /* we do not need to seek again, so move on */
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
                break;
            } else {
                /* we have wasted enough time, we are there */
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "scfi_srv seek over on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR >> 16, uptr->CHS >> 16);
                uptr->CHS = uptr->STAR;         /* we are there */
#ifdef FAST_FOR_UTX
                sim_activate(uptr, 15);         /* start things off */
#else
                sim_activate(uptr, 150);        /* start things off */
#endif
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* set buf data to current STAR values */
        tcyl = STAR2CYL(uptr->CHS);       /* get current cyl */

        /* the value is really a sector offset for the disk */
        /* but will treat as c/h/s for processing */
        /* the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv current STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, (uptr->CHS >> 24) & 0xff, (uptr->CHS >> 16) & 0xff,
            (uptr->CHS >> 8) & 0xff, (uptr->CHS) & 0xff);

        if (len != 4) {
            sim_debug(DEBUG_EXP, dptr,
                "scfi_srv SEEK bad count unit %02x count %04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }

        /* Read in 4 character required seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                if (i == 0) {
                    sim_debug(DEBUG_DETAIL, dptr,
                        "scfi_srv seek error unit=%02x\n", unit);
                    /* we have error, bail out */
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    uptr->SNS |= SNS_DADE;      /* Disc addressing or seek error */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    chp->ccw_count = len;       /* restore count, huh? */
                    return SCPE_OK;
                    break;
                }
                /* just read the next byte */
            }
        }
        chp->ccw_count = len;                   /* restore count for diag, huh? */
        /* the value is really a sector offset for the disk */
        /* but will treat as c/h/s for processing */
        /* the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = STAR2CYL(uptr->STAR);             /* get the cylinder */
        trk = buf[2];                           /* get the track */
        sec = buf[3];                           /* get sec */

        sim_debug(DEBUG_DETAIL, dptr,
           "scfi_srv NEW SEEK cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (uptr->STAR >= CAP(type)) {

            sim_debug(DEBUG_EXP, dptr,
                "scfi_srv seek ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                cyl, trk, buf[3], unit);

            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_DADE;              /* set error status */
            uptr->SNS2 |= (SNS_SKER|SNS_SEND);

            /* set new STAR value, even if invalid */
            uptr->CHS = uptr->STAR;

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* calc the new sector address of data */
        /* calculate file position in bytes of requested sector */
        /* file offset in bytes */
        tstart = uptr->STAR * SSB(type);
        uptr->CHS = uptr->STAR;

        sim_debug(DEBUG_DETAIL, dptr,
            "scfi_srv seek start %06x trk %04x sec %02x\n",
            tstart, trk, buf[3]);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "scfi_srv Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                   /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* Check if already on correct cylinder */
        /* if not, do a delay to slow things down */
        if (tcyl != cyl) {
            int diff = ((int)tcyl - (int)cyl);
            if (diff < 0)
                diff = -diff;
            /* Do a fake seek to kill time */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_EXP, dptr,
                "scfi_srv seeking unit=%02x to %04x/%02x/%02x from cyl %04x (%04x)\n",
                unit, cyl, trk, buf[3], tcyl, cyl);
#ifdef FAST_FOR_UTX
            sim_activate(uptr, 15);             /* start things off */
#else
            sim_activate(uptr, 200+diff);       /* start us off */
#endif
        } else {
            /* we are on cylinder/track/sector, so go on */
            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv done seeking to %04x cyl %04x trk %02x sec %02x\n",
                tstart, cyl, trk, buf[3]);
            /* set new STAR value */
            uptr->CHS = uptr->STAR;
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        }
        break;

    case DSK_XEZ:   /* 0x37 */                  /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "XEZ REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        tcyl = STAR2CYL(uptr->CHS);             /* get current cyl */
        uptr->STAR = 0;                         /* set STAR to 0, 0, 0 */
        uptr->CHS = 0;                          /* set current CHS to 0, 0, 0 */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        uptr->CMD |= DSK_SCK;                   /* show as seek command */
        tstart = 0;                             /* byte offset is 0 */

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "scfi_srv Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        /* we are on cylinder/track/sector zero, so go on */
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv done seek trk 0\n");
        uptr->CMD |= DSK_SEEKING;               /* show we are seeking */
        sim_debug(DEBUG_EXP, dptr,
            "scfi_srv XEZ seeking unit=%02x to cyl 0000 from cyl %04x\n",
            unit, tcyl);
        sim_activate(uptr, 15);                 /* start things off */
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
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x old %x new %x\n",
            unit, (uptr->SNS)&0xff, buf[0]);
        uptr->CMD &= LMASK;                     /* remove old cmd */
        uptr->SNS &= MASK24;                    /* clear old mode data */
        uptr->SNS |= (buf[0] << 24);            /* save mode value */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_RD:                                /* Read Data command 0x02 */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "DISK READ starting CMD %08x chsa %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, chp->ccw_addr, chp->ccw_count);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            /* get file offset in sectors */
            tstart = uptr->CHS;
            /* file offset in bytes */
            tstart = tstart * SSB(type);

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "scfi_srv READ, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK READ reading CMD %08x chsa %04x tstart %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, tstart, chp->ccw_addr, chp->ccw_count);

            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_EXP, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, ssize, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv after READ chsa %04x buffer %06x count %04x\n",
                chsa, chp->ccw_addr, chp->ccw_count);
            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv after READ buffer %06x count %04x data %02x%02x%02x%02x %02x%02x%02x%02x\n",
//              chp->ccw_addr, chp->ccw_count, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
                chp->ccw_addr, chp->ccw_count, buf[1016], buf[1017], buf[1018], buf[1019],
                buf[1020], buf[1021], buf[1022], buf[1023]);

            uptr->CHS++;                        /* next sector number */
            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    sim_debug(DEBUG_CMD, dptr,
                        "SCFI Read %04x bytes leaving %04x from diskfile %04x/%02x/%02x\n",
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

            sim_debug(DEBUG_CMD, dptr,
                "SCFI READ %04x bytes leaving %4x to be read to %06x from diskfile %04x/%02x/%02x\n",
                ssize, chp->ccw_count, chp->ccw_addr,
                ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);

            /* get sector offset */
            tstart = uptr->CHS;

            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_EXP, dptr,
                    "DISK Read reached EOM for read from disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_CMD, dptr,
                    "DISK Read complete for read from diskfile %04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK sector read complete, %x bytes to go from diskfile %04x/%02x/%02x\n",
                chp->ccw_count, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
#ifdef FAST_FOR_UTX
            sim_activate(uptr, 15);             /* start things off */
#else
            sim_activate(uptr, 150);            /* wait to read next sector */
#endif
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        break;

    case DSK_WD:                                /* Write Data command 0x01 */
        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            sim_debug(DEBUG_CMD, dptr,
                "DISK WRITE starting unit=%02x CMD %08x write %04x from %06x to %03x/%02x/%02x\n",
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
            tstart = uptr->CHS;
            /* file offset in bytes */
            tstart = tstart * SSB(type);

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "scfi_srv WRITE, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }

            /* process the next sector of data */
            tcyl = 0;                           /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {/* get a byte from memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    /* if error on reading 1st byte, we are done writing */
                    if ((i == 0) || (chp->chan_status & STATUS_PCHK)) {
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "DISK Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
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
            tstart = uptr->CHS;

            /* write the sector to disk */
            if ((i=sim_fwrite(buf2, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_EXP, dptr,
                    "Error %08x on write %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    i, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv after WRITE buffer %06x count %04x data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                chp->ccw_addr, chp->ccw_count, buf2[1016], buf2[1017], buf2[1018], buf2[1019],
                buf2[1020], buf2[1021], buf2[1022], buf2[1023]);

            sim_debug(DEBUG_CMD, dptr,
                "DISK WR to sec end %04x bytes end %04x to diskfile cyl %04x hds %02x sec %02x\n",
                len, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));

            uptr->CHS++;                        /* next sector number */
            if (tcyl != 0) {                    /* see if done with write command */
                sim_debug(DEBUG_CMD, dptr,
                    "DISK WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            /* get sector offset */
            tstart = uptr->CHS;

            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_EXP, dptr,
                    "DISK Write reached EOM for write to disk @ %04x/%02x/%02x\n",
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
            sim_activate(uptr, 15);             /* start things off */
#else
            sim_activate(uptr, 150);            /* wait to read next sector */
#endif
            break;
         }
         uptr->CMD &= LMASK;                    /* remove old status bits & cmd */
         break;

    default:
        sim_debug(DEBUG_EXP, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|STATUS_PCHK); /* return Prog Check */
        break;
    }
    sim_debug(DEBUG_DETAIL, dptr,
        "scfi_srv done cmd %02x chsa %04x chs %04x/%02x/%02x\n",
        cmd, chsa, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
    return SCPE_OK;
}

/* handle rschnlio cmds for disk */
t_stat  scfi_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & DSK_CMDMSK;

    sim_debug(DEBUG_EXP, dptr,
        "scfi_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    scfi_ini(uptr, 0);                          /* reset the unit */
    return SCPE_OK;
}

/* initialize the disk */
void scfi_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     i = GET_TYPE(uptr->flags);

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */
    uptr->STAR = 0;                             /* set STAR to cyl/hd/sec = 0 */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* size in sectors */
    sim_cancel(uptr);                           /* stop any timers */

    sim_debug(DEBUG_EXP, &sda_dev, "SDA init device %s on unit SDA%04x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

t_stat scfi_reset(DEVICE *dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* create the disk file for the specified device */
int scfi_format(UNIT *uptr) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      ssize = SSB(type);              /* disk sector size in bytes */
    uint32      tsize = SPT(type);              /* get track size in sectors */
    uint32      csize = SPC(type);              /* get cylinder size in sectors */
    uint32      cyl = CYL(type);                /* get # cylinders */
    uint32      cap = CAP(type);                /* disk capacity in sectors */
    uint32      cylv = cyl;                     /* number of cylinders */
    uint8       *buff;
    int32       i;
    t_stat      oldsw = sim_switches;           /* save switches */

                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;          /* last sector of disk */

                /* make up dummy defect map */
    uint32      dmap[4] = {0xf0000000 | (cap-1), 0x8a000000,
                    0x9a000000 | (cap-1), 0xf4000000};


    /* see if -i or -n specified on attach command */
    if (!(sim_switches & SWMASK('N')) && !(sim_switches & SWMASK('I'))) {
        sim_switches = 0;                       /* simh tests 'N' & 'Y' switches */
        /* see if user wants to initialize the disk */
        if (!get_yn("Initialize disk? [Y] ", TRUE)) {
            sim_switches = oldsw;
            return 1;
        }
        sim_switches = oldsw;                   /* restore switches */
    }

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
            sim_debug(DEBUG_EXP, dptr,
                "Error on write to diskfile cyl %04x\n", cyl);
            free(buff);                         /* free cylinder buffer */
            buff = 0;
            return 1;
        }
        if ((cyl % 100) == 0)
            fputc('.', stderr);
    }
    fputc('\r', stderr);
    fputc('\n', stderr);
    free(buff);                                 /* free cylinder buffer */
    buff = 0;

    /* byte swap the buffer for dmap */
    for (i=0; i<4; i++) {
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }

    /* now seek to end of disk and write the dmap data to last sector */
    /* write dmap data to last sector on disk */
    if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
        sim_debug(DEBUG_EXP, dptr,
        "Error on last sector seek to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_EXP, dptr,
        "Error writing DMAP to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }

    printf("Disk %s has %x (%d) cyl, %x (%d) hds, %x (%d) sec\r\n",
        scfi_type[type].name, CYL(type), CYL(type), HDS(type), HDS(type),
        SPT(type), SPT(type));

    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    return 0;                                   /* good or error */
}

/* attach the selected file to the disk */
t_stat scfi_attach(UNIT *uptr, CONST char *file)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    CHANP           *chp = find_chanp_ptr(chsa);/* get channel prog pointer */
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = 0;
    t_stat          r, s;
    uint32          ssize;                      /* sector size in bytes */
    uint32          info, good;
    uint8           buff[1024];
    int             i, j;

                    /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32          laddr = CAP(type) - 1;      /* last sector of disk */
                    /* get sector address of utx diag map (DMAP) track 0 pointer */
                    /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                    /* 0x9a000000 + (cyl-1), 0xf4000000 */
                    /* defect map */
    uint32          dmap[4] = {0xf0000000 | (CAP(type)-1), 0x8a000000,
                        0x9a000000 | (CAP(type)-1), 0xf4000000};

    for (i=0; i<4; i++) {                       /* byte swap data for last sector */
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }

    /* see if valid disk entry */
    if (scfi_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    if (dptr->flags & DEV_DIS) {
        fprintf(sim_deb,
            "ERROR===ERROR\nSCFI Disk device %s disabled on system, aborting\r\n",
            dptr->name);
        printf("ERROR===ERROR\nSCFI Disk device %s disabled on system, aborting\r\n",
            dptr->name);
        return SCPE_UDIS;                       /* device disabled */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    uptr->capac = CAP(type);                    /* disk capacity in sectors */
    ssize = SSB(type);                          /* get sector size in bytes */
    for (i=0; i<(int)ssize; i++)
        buff[i] = 0;                            /* zero the buffer */

    sim_debug(DEBUG_CMD, dptr,
        "SCFI Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        scfi_type[type].name, scfi_type[type].cyl, scfi_type[type].nhds,
        scfi_type[type].spt, ssize, uptr->capac); /* disk capacity */
    printf("SCFI Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\r\n",
        scfi_type[type].name, scfi_type[type].cyl, scfi_type[type].nhds,
        scfi_type[type].spt, ssize, uptr->capac); /* disk capacity */

    /* see if -i or -n specified on attach command */
    if ((sim_switches & SWMASK('N')) || (sim_switches & SWMASK('I'))) {
        goto fmt;                               /* user wants new disk */
    }

    /* seek to end of disk */
    if ((sim_fseek(uptr->fileref, 0, SEEK_END)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "SCFI Disk attach SEEK end failed\n");
        printf( "SCFI Disk attach SEEK end failed\r\n");
        goto fmt;                               /* not setup, go format */
    }

    s = ftell(uptr->fileref);                   /* get current file position */
    if (s == 0) {
        sim_debug(DEBUG_CMD, dptr, "SCFI Disk attach ftell failed s=%06d\n", s);
        printf("SCFI Disk attach ftell failed s=%06d\r\n", s);
        goto fmt;                               /* not setup, go format */
    }
    sim_debug(DEBUG_CMD, dptr,
            "SCFI Disk attach ftell value s=%06d b=%06d CAP %06d\n", s/ssize, s, CAP(type));
    printf("SCFI Disk attach ftell value s=%06d b=%06d CAP %06d\r\n", s/ssize, s, CAP(type));

    if (((int)s/(int)ssize) < ((int)CAP(type))) {   /* full sized disk? */
        j = (CAP(type) - (s/ssize));            /* get # sectors to write */
        sim_debug(DEBUG_CMD, dptr,
            "SCFI Disk attach for MPX 1.X needs %04d more sectors added to disk\n", j);
        printf("SCFI Disk attach for MPX 1.X needs %04d more sectors added to disk\r\n", j);
        /* must be MPX 1.X disk, extend to MPX 3.X size */
        /* write sectors of zero to end of disk to fill it out */
        for (i=0; i<j; i++) {
            if ((r = sim_fwrite(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
                sim_debug(DEBUG_CMD, dptr, "SCFI Disk attach fread ret = %04d\n", r);
                printf("SCFI Disk attach fread ret = %04d\r\n", r);
                goto fmt;                       /* not setup, go format */
            }
        }
        s = ftell(uptr->fileref);               /* get current file position */
        sim_debug(DEBUG_CMD, dptr,
            "SCFI Disk attach MPX 1.X file extended & sized secs %06d bytes %06d\n",
            s/ssize, s);
        printf("SCFI Disk attach MPX 1.X  file extended & sized secs %06d bytes %06d\r\n",
            s/ssize, s);
    }

    /* seek last sector of disk */
    if ((sim_fseek(uptr->fileref, (CAP(type)-1)*ssize, SEEK_SET)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "SCFI Disk attach SEEK last sector failed\n");
        printf("SCFI Disk attach SEEK last sector failed\r\n");
        goto fmt;
    }

    /* see if there is disk size-1 in last sector of disk, if not add it */
    if ((r = sim_fread(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        sim_debug(DEBUG_CMD, dptr, "SCFI Disk format fread error = %04d\n", r);
        printf("SCFI Disk format fread error = %04d\r\n", r);
add_size:
        if (ssize == 768) {
            /* assume we have MPX 1x, and go on */
            /* write dmap data to last sector on disk for mpx 1.x */
            if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
                sim_debug(DEBUG_CMD, dptr,
                    "SCFI Error on last sector seek to sect %06d offset %06d bytes\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                printf("SCFI Error on last sector seek to sect %06d offset %06d bytes\r\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                goto fmt;
            }
            if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
                sim_debug(DEBUG_CMD, dptr,
                    "SCFI Error writing DMAP to sect %06x offset %06d bytes\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                printf("SCFI Error writing DMAP to sect %06x offset %06d bytes\r\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                goto fmt;
            }

            /* seek last sector of disk */
            if ((sim_fseek(uptr->fileref, (CAP(type))*ssize, SEEK_SET)) != 0) {
                sim_debug(DEBUG_CMD, dptr, "SCFI Disk attach SEEK last sector failed\n");
                printf( "SCFI Disk attach SEEK last sector failed\r\n");
                goto fmt;
            }
            s = ftell(uptr->fileref);           /* get current file position */
            sim_debug(DEBUG_CMD, dptr,
                "SCFI Disk attach MPX file extended & sized secs %06d bytes %06d\n", s/ssize, s);
            printf("SCFI Disk attach MPX file extended & sized secs %06d bytes %06d\r\n", s/ssize, s);
            goto ldone;
        } else {
            /* error if UTX */
            detach_unit(uptr);                  /* if error, abort */
            return SCPE_FMT;                    /* error */
        }
    } else {
        /* if not disk size, go add it in for MPX, error if UTX */
        if ((buff[0] | buff[1] | buff[2] | buff[3]) == 0) {
            sim_debug(DEBUG_CMD, dptr,
                "SCFI Disk format0 buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
                buff[0], buff[1], buff[2], buff[3]);
            goto add_size;
        }
    }

    info = (buff[0]<<24) | (buff[1]<<16) | (buff[2]<<8) | buff[3];
    good = 0xf0000000 | (CAP(type)-1);
    /* check for 0xf0ssssss where ssssss is disk size-1 in sectors */
    if (info != good) {
        sim_debug(DEBUG_CMD, dptr,
            "SCFI Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
            buff[0], buff[1], buff[2], buff[3]);
        printf(
            "SCFI Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\r\n",
            buff[0], buff[1], buff[2], buff[3]);
fmt:
        /* format the drive */
        if (scfi_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }
ldone:
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    sim_debug(DEBUG_CMD, dptr,
        "SCFI Attach %s %04x cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        scfi_type[type].name, chsa, CYL(type), HDS(type), SPT(type), SPC(type),
        CAP(type), CAPB(type));
    printf("SCFI Attach %s %04x cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\r\n",
        scfi_type[type].name, chsa, CYL(type), HDS(type), SPT(type), SPC(type),
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, dptr,
       "SCFI File %s at chsa %04x attached to %s is ready\n",
        file, chsa, scfi_type[type].name);
    printf("SCFI File %s at chsa %04x attached to %s is ready\r\n",
        file, chsa, scfi_type[type].name);

    /* check for valid configured disk */
    /* must have valid DIB and Channel Program pointer */
    dibp = (DIB *)dptr->ctxt;                   /* get the DIB pointer */
    if ((dib_unit[chsa] == NULL) || (dibp == NULL) || (chp == NULL)) {
        sim_debug(DEBUG_EXP, dptr,
            "ERROR===ERROR\nSCFI device %s not configured on system, aborting\n",
            dptr->name);
        printf("ERROR===ERROR\nSCFI device %s not configured on system, aborting\r\n",
            dptr->name);
        detach_unit(uptr);                      /* detach if error */
        return SCPE_UNATT;                      /* error */
    }
    set_devattn(chsa, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat scfi_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat scfi_boot(int32 unit_num, DEVICE *dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr,
        "SCFI Disk Boot dev/unit %x\n", GET_UADDR(uptr->CMD));

    /* see if device disabled */
    if (dptr->flags & DEV_DIS) {
        printf("ERROR===ERROR\r\nSCFI Disk device %s disabled on system, aborting\r\n",
            dptr->name);
        return SCPE_UDIS;                       /* device disabled */
    }

    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr,
            "SCFI Disk Boot attach error dev/unit %04x\n", GET_UADDR(uptr->CMD));
        printf("SCFI Disk Boot attach error dev/unit %04x\n", GET_UADDR(uptr->CMD));
        return SCPE_UNATT;                      /* attached? */
    }
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */

    /* now boot the disk */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return chan_boot(GET_UADDR(uptr->CMD), dptr);   /* boot the ch/sa */
}

/* Disk option setting commands */
/* set the disk type attached to unit */
t_stat scfi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; scfi_type[i].name != 0; i++) {
        if (strcmp(scfi_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear the old UNIT type */
            uptr->flags |= SET_TYPE(i);         /* set the new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat scfi_get_type(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(scfi_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat scfi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    int i;
    fprintf (st, "SEL-32 SCFI Disk Processor\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; scfi_type[i].name != 0; i++) {
        fprintf(st, "%s", scfi_type[i].name);
        if (scfi_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; scfi_type[i].name != 0; i++) {
        int32   size = CAPB(i);                 /* disk capacity in bytes */
        size /= 1024;                           /* make KB */
        size = (10 * size) / 1024;              /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            scfi_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *scfi_description (DEVICE *dptr)
{
    return "SEL-32 SCFI Disk Processor";
}

#endif
