/* sel32_scsi.c: SEL-32 MFP SCSI Disk controller

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

#if NUM_DEVS_SCSI > 0

#define UNIT_SCSI   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

/* useful conversions */
/* Fill STAR value from cyl, trk, sec data */
#define CHS2STAR(c,h,s)         (((c<<16) & LMASK)|((h<<8) & 0xff00)|(s & 0xff))
/* convert STAR value to number of sectors */
#define STAR2SEC(star,spt,spc)  ((star&0xff)+(((star>>8)&0xff)*spt)+((star>>16)*spc))
/* convert STAR value to number of heads or tracks */
#define STAR2TRK(star,tpc)      ((star >> 16) * tpc + ((star >> 8) & 0x0ff))
/* convert STAR value to number of cylinders */
#define STAR2CYL(star)          ((star >> 16) & RMASK)
/* convert byte value to number of sectors mod sector size */
#define BYTES2SEC(bytes,ssize)  (((bytes) + (ssize-1)) >> 10)
/* get sectors per track for specified type */
#define SPT(type)               (scsi_type[type].spt)
/* get sectors per cylinderfor specified type */
#define SPC(type)               (scsi_type[type].spt*scsi_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (scsi_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (scsi_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (scsi_type[type].ssiz*4)
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
        bit  6   - 0=Blk size   00=768 byte blk
        bit  7   - 0=Blk size   01=1024 byte blk
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

/* track label / sector label definations */
/*
    short lcyl;         cylinder
    char ltkn;          track
    char lid;           sector id
    char lflg1;         track/sector status flags
        bit 0           good
            1           alternate
            2           spare
            3           reserved
            4           flaw
            5           last track
            6           start of alternate
    char lflg2;
    short lspar1;
    short lspar2;
    short ldef1;
    int ldeallp;        DMAP block number trk0
    int lumapp;         UMAP block number sec1
    short ladef3;
    short laltcyl;
    char lalttk;        sectors per track
    char ldscnt;        number of heads
    char ldatrflg;      device attributes
        bit 0           n/u
            1           disk is mhd
            2           n/u
            3           n/u
            4           n/u
            5           dual ported
            6/7         00 768 bytes/blk
                        01 1024 bytes/blk
                        10 2048 bytes/blk
    char ldatrscnt;     sectors per track (again)
    char ldatrmhdc;     MHD head count
    char ldatrfhdc;     FHD head count
 */

#define CMD     u3
/* u3 */
/* in u3 is device command code and status */
#define DSK_CMDMSK      0x00ff                  /* Command being run */
#define DSK_STAR        0x0100                  /* STAR value in u4 */
#define DSK_NU          0x0200                  /* Not used */
#define DSK_READDONE    0x0400                  /* Read finished, end channel */
#define DSK_ENDDSK      0x0800                  /* Sensed end of disk */
#define DSK_SEEKING     0x1000                  /* Disk is currently seeking */
#define DSK_READING     0x2000                  /* Disk is reading data */
#define DSK_WRITING     0x4000                  /* Disk is writing data */
#define DSK_BUSY        0x8000                  /* Disk is busy */
/* commands */
#define DSK_INCH        0x00                    /* Initialize channel */
#define DSK_INCH2       0xF0                    /* Initialize channel for processing */
#define DSK_WD          0x01                    /* Write data */
#define DSK_RD          0x02                    /* Read data */
#define DSK_NOP         0x03                    /* No operation */
#define DSK_SNS         0x04                    /* Sense */
#define DSK_SCK         0x07                    /* Seek cylinder, track, sector */
#define DSK_TIC         0x08                    /* Transfer in channel */
//#define DSK_FNSK        0x0B                    /* Format for no skip */
#define DSK_RBLK        0x13                    /* Reassign Block */
#define DSK_LMR         0x1F                    /* Load mode register */
#define DSK_RWD         0x23                    /* Rewind */
//#define DSK_WSL         0x31                    /* Write sector label */
//#define DSK_RSL         0x32                    /* Read sector label */
//#define DSK_REL         0x33                    /* Release */
#define DSK_XEZ         0x37                    /* Rezero */
//#define DSK_ADV         0x43                    /* Advance Record */
//#define DSK_IHA         0x47                    /* Increment head address */
//#define DSK_SRM         0x4F                    /* Set reserve track mode */
//#define DSK_WTL         0x51                    /* Write track label */
//#define DSK_RTL         0x52                    /* Read track label */
#define DSK_RCAP        0x53                    /* Read Capacity */
//#define DSK_XRM         0x5F                    /* Reset reserve track mode */
//#define DSK_RAP         0xA2                    /* Read angular positions */
#define DSK_RES         0xA3                    /* Reserve Unit */
//#define DSK_TESS        0xAB                    /* Test STAR (subchannel target address register) */
#define DSK_INQ         0xB3                    /* Inquiry */
#define DSK_REL         0xC3                    /* Release Unit */
#define DSK_TCMD        0xD3                    /* Transfer Command Packet (specifies CDB to send) */
//#define DSK_ICH         0xFF                    /* Initialize Controller */
#define DSK_FRE         0xF3                    /* Reserved */
#define DSK_SID         0x80                    /* MFP status command */

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
//#define SNS_RESERV      0x01000000              /* Reserved */
#define SNS_TCMD        0x01000000              /* Presessing CMD cmd chain */

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
#define SNS_ECCD        0x08                    /* ECC error iin data */
#define SNS_ECCT        0x04                    /* ECC error in track label */
#define SNS_RTAE        0x02                    /* Reserve track access error */
#define SNS_UESS        0x01                    /* Uncorrectable ECC error */

#define CHS     u6
/* u6 holds the current cyl, hd, sec for the drive */

/* this attribute information is provided by the INCH command */
/* for each device and is not used.  It is reconstructed from */
/* the disk_t structure data for the assigned disk */
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

/* INCH addr    up7 */

/* disk definition structure */
struct scsi_t
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

scsi_type[] =
{
    /* Class F Disc Devices */
    /* MPX SCSI disks for SCSI controller */
    {"SD150",   9, 192, 24,   963,   967, 0x40},   /*0  8820  150M  208872 sec */
    {"SD300",   9, 192, 32,  1405,  1409, 0x40},   /*1  8828  300M  396674 sec */
    {"SD700",  15, 192, 35,  1542,  1546, 0x40},   /*2  8833  700M  797129 sec */
    {"SD1200", 15, 192, 49,  1927,  1931, 0x40},   /*3  8835 1200M 1389584 sec */
    {"SD2400", 19, 192, 59,  2703,  2707, 0x40},   /*4  8842 2400M 2909128 sec */
    {"SH1200", 15, 192, 50,  1868,  1872, 0x40},   /*5  8832 1200M 1395014 sec */
    {"SH2550", 19, 192, 55,  2703,  2707, 0x40},   /*6  8834 2550M 2909128 sec */
    {"SH5150", 21, 192, 83,  3707,  3711, 0x40},   /*7  0000 5150M 5581145 sec */
    {"8820",    9, 256, 18,   963,   967, 0x41},   /*8  8820  150M  156654 sec */
    {"8821",    9, 256, 36,   963,   967, 0x41},   /*9  8828  300M  313308 sec */
    {"8833",   18, 256, 20,  1542,  1546, 0x41},   /*10 8833  700M  556560 sec */
    {"8835",   18, 256, 20,  1927,  1931, 0x41},   /*11 8835 1200M  695160 sec */
    /* For UTX */
    {NULL, 0}
};

t_stat  scsi_preio(UNIT *uptr, uint16 chan);
t_stat  scsi_startcmd(UNIT *uptr, uint16 chan, uint8 cmd);
t_stat  scsi_haltio(UNIT *uptr);
t_stat  scsi_srv(UNIT *);
t_stat  scsi_boot(int32 unitnum, DEVICE *);
void    scsi_ini(UNIT *, t_bool);
t_stat  scsi_rschnlio(UNIT *uptr);
t_stat  scsi_reset(DEVICE *);
t_stat  scsi_attach(UNIT *, CONST char *);
t_stat  scsi_detach(UNIT *);
t_stat  scsi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  scsi_get_type(FILE * st, UNIT *uptr, int32 v, CONST void *desc);
t_stat  scsi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const   char  *scsi_description (DEVICE *dptr);

/* One buffer per unit */
//#define BUFFSIZE        (256)
#define BUFFSIZE        (512)
uint8   scsi_buf[NUM_DEVS_SCSI][NUM_UNITS_SCSI][BUFFSIZE];
uint8   scsi_pcmd[NUM_DEVS_SCSI][NUM_UNITS_SCSI];

/* channel program information */
CHANP   sba_chp[NUM_UNITS_SCSI] = {0};

MTAB    scsi_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &scsi_set_type, &scsi_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0}
};

UNIT    sba_unit[] = {
/* SET_TYPE(0) SD150 */
    {UDATA(&scsi_srv, UNIT_SCSI|SET_TYPE(0), 0), 0, UNIT_ADDR(0x7600)},  /* 0 */
    {UDATA(&scsi_srv, UNIT_SCSI|SET_TYPE(0), 0), 0, UNIT_ADDR(0x7608)},  /* 1 */
};

DIB     sba_dib = {
    scsi_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    scsi_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    scsi_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    scsi_rschnlio,  /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    scsi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sba_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sba_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_SCSI, /* uint8 numunits */                        /* number of units defined */
    0x38,           /* uint8 mask */                            /* 8 devices - device mask */
    0x7600,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE  sba_dev = {
    "SBA", sba_unit, NULL, scsi_mod,
    NUM_UNITS_SCSI, 16, 24, 4, 16, 32,
    NULL, NULL, &scsi_reset, &scsi_boot, &scsi_attach, &scsi_detach,
    /* ctxt is the DIB pointer */
    &sba_dib, DEV_BUF_NUM(0)|DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scsi_help, NULL, NULL, &scsi_description
};

#if NUM_DEVS_SCSI > 1

/* channel program information */
CHANP   sbb_chp[NUM_UNITS_SCSI] = {0};

UNIT    sbb_unit[] = {
/* SET_TYPE(0) DM150 */
    {UDATA(&scsi_srv, UNIT_SCSI|SET_TYPE(0), 0), 0, UNIT_ADDR(0x7640)},  /* 0 */
    {UDATA(&scsi_srv, UNIT_SCSI|SET_TYPE(0), 0), 0, UNIT_ADDR(0x7648)},  /* 1 */
};

//DIB sdb_dib = {scsi_preio, scsi_startcmd, NULL, NULL, NULL,
//scsi_ini, sdb_unit, sdb_chp, NUM_UNITS_SCSI, 0x0f, 0x0c00, 0, 0, 0};

DIB     sbb_dib = {
    scsi_preio,     /* t_stat (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    scsi_startcmd,  /* t_stat (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    scsi_haltio,    /* t_stat (*halt_io)(UNIT *uptr) */         /* Halt I/O */
    NULL,           /* t_stat (*stop_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* t_stat (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* t_stat (*rsctl_io)(UNIT *uptr) */        /* Reset Controller */
    scsi_rschnlio,  /* t_stat (*rschnl_io)(UNIT *uptr) */       /* Reset Channel */
    NULL,           /* t_stat (*iocl_io)(CHANP *chp, int32 tic_ok)) */  /* Process IOCL */
    scsi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sbb_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sbb_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NULL,           /* IOCLQ *ioclq_ptr */                      /* IOCL entries, 1 per UNIT */
    NUM_UNITS_SCSI, /* uint8 numunits */                        /* number of units defined */
    0x38,           /* uint8 mask */                            /* 2 devices - device mask */
    0x7600,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE  sbb_dev = {
    "SBB", sbb_unit, NULL, scsi_mod,
    NUM_UNITS_SCSI, 16, 24, 4, 16, 32,
    NULL, NULL, &scsi_reset, &scsi_boot, &scsi_attach, &scsi_detach,
    /* ctxt is the DIB pointer */
    &sbb_dib, DEV_BUF_NUM(1)|DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scsi_help, NULL, NULL, &scsi_description
};
#endif

/* convert sector disk address to star values (c,h,s) */
uint32 scsisec2star(uint32 daddr, int type)
{
    int32 sec = daddr % scsi_type[type].spt;    /* get sector value */
    int32 spc = scsi_type[type].nhds * scsi_type[type].spt; /* sec per cyl */
    int32 cyl = daddr / spc;                    /* cylinders */
    int32 hds = (daddr % spc) / scsi_type[type].spt;    /* heads */ 

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* start a disk operation */
t_stat scsi_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "scsi_preio CMD %08x unit=%02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "scsi_preio unit %02x chsa %04x OK\n", unit, chsa);
    return 0;                                   /* good to go */
}

t_stat scsi_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "scsi_startcmd unit %02x cmd %02x CMD %08x SNS %08x\n",
        unit, cmd, uptr->CMD, uptr->SNS);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        uptr->CMD |= DSK_BUSY;                  /* Flag we we are busy */
        return SNS_BSY;
    }
    if ((uptr->CMD & 0xff00) != 0) {            /* if any status info, we are busy */
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "scsi_startcmd enter unit=%02x cmd %02x\n", unit, cmd);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH 0x00 */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_startcmd starting INCH %06x cmd, chsa %04x MemBuf %08x cnt %04x\n",
            uptr->u4, chsa, chp->ccw_addr, chp->ccw_count);

        uptr->CMD |= DSK_INCH2;                 /* use 0xF0 for inch, just need int */
        /* leave the TCMD bit */
        uptr->SNS &= ~MASK24;                   /* clear all but old mode data */
#ifdef FAST_FOR_UTX
//zz    sim_activate(uptr, 100);                /* start things off */
        sim_activate(uptr, 30);                 /* start things off */
#else
        sim_activate(uptr, 100);                /* start things off */
#endif
        return 0;
        break;

    case DSK_SCK:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x1f */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register */
    case DSK_NOP:                               /* NOP 0x03 */
    case DSK_RCAP:                              /* Read Capacity 0x53 */
    /* Transfer Command Packet (specifies CDB to send) */
    case DSK_TCMD:                              /* Transfer command packet 0xD3 */
    case DSK_SID:                               /* channel Sense 0x80 */
        /* leave the TCMD bit */
        uptr->SNS &= ~MASK24;                   /* clear all but old mode data */
    case DSK_SNS:                               /* Sense 0x04 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_startcmd starting disk cmd %02x chsa %04x\n", cmd, chsa);
        sim_activate(uptr, 100);                /* start things off */
        return 0;
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "scsi_startcmd done with scsi_startcmd %02x chsa %04x SNS %08x\n",
        cmd, chsa, uptr->SNS);
    if (uptr->SNS & 0xff)                       /* any other cmd is error */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
#ifdef FAST_FOR_UTX
//zz    sim_activate(uptr, 100);                /* start things off */
        sim_activate(uptr, 20);                 /* start things off */
#else
        sim_activate(uptr, 100);                /* start things off */
#endif
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle processing of disk requests. */
t_stat scsi_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    /* get pointer to Dev Info Blk for this device */
    CHANP           *chp = find_chanp_ptr(chsa);    /* get channel prog pointer */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    int             unit = (uptr - dptr->units);
    int             bufnum = GET_DEV_BUF(dptr->flags);
    int             len=0;
    int             i;
    uint32          cap = CAP(type);
    uint32          mema;                       /* memory address */
    uint8           ch;
    int32           ssize = scsi_type[type].ssiz*4; /* Size of one sector in bytes */
    uint32          tstart = 0;                 /* Location of start of cyl/track/sect in data */
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_DETAIL, &sba_dev,
        "scsi_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, dptr,
        "scsi_srv cmd=%02x chsa %04x count %04x SNS %02x\n",
        cmd, chsa, chp->ccw_count, uptr->SNS);
    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_INCH2:                             /* use 0xF0 for inch, just need int */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv starting INCH cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chsa, chp->ccw_addr, chp->ccw_count);

#if 0
        cpu_dev.dctrl |= DEBUG_INST;            /* start instruction trace */
#endif
        /* mema has IOCD word 1 contents.  For the MFP (scsi processor) */
        /* a pointer to the INCH buffer. The INCH buffer address must be */
        /* set for the parent channel as well as all other devices on the */
        /* channel.  Call set_inch() to do this for us. Just return OK and */
        /* channel software will use the status buffer addr */

        /* now call set_inch() function to write and test inch buffer addresses */
        /* 1-256 wd buffer is provided for 128 status dbl words */
        i = set_inch(uptr, mema, 128);          /* new address of 33 entries */
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv cmd INCH chsa %04x chsa %06x count %04x completed\n",
            chsa, mema, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    /* 3 status wds are to be returned */
    /* Wd 1 MMXXXXXX board model # assume 00 00 08 02*/
    /* Wd 2 MMXXXXXX board firmware model # assume 00 00 08 02*/
    /* Wd 3 MMXXXXXX board firmware revision # assume 00 00 00 14*/
    case DSK_SID: /* 0x80 */                    /* this is really for the MFP controller */
        /* send 12 byte Status ID data */
        /* Word 0 */        /* board mod 4324724 = 0x0041fd74 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);             /* write byte 0 */
        ch = 0x41;
        chan_write_byte(chsa, &ch);             /* write byte 1 */
        ch = 0xfd;
        chan_write_byte(chsa, &ch);             /* write byte 2 */
        ch = 0x74;
        chan_write_byte(chsa, &ch);             /* write byte 3 */

        /* Word 1 */        /* firmware 4407519 = 0x004340df */
        ch = 0x00;
        chan_write_byte(chsa, &ch);             /* write byte 4 */
        ch = 0x43;
        chan_write_byte(chsa, &ch);             /* write byte 5 */
        ch = 0x40;
        chan_write_byte(chsa, &ch);             /* write byte 6 */
        ch = 0xdf;
        chan_write_byte(chsa, &ch);             /* write byte 7 */

        /* Word 2 */        /* firmware rev 4259588 = 0x0040ff04 */
        ch = 0x00;
        chan_write_byte(chsa, &ch);             /* write byte 8 */
        ch = 0x40;
        chan_write_byte(chsa, &ch);             /* write byte 9 */
        ch = 0xff;
        chan_write_byte(chsa, &ch);             /* write byte 10 */
        ch = 0x04;
        chan_write_byte(chsa, &ch);             /* write byte 11 */

        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "scsi_srv SID chan %02x: chnend|devend\n", chsa);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x4 */
        sim_debug(DEBUG_CMD, dptr, "scsi_startcmd CMD sense\n");

        /* bytes 0,1 - Cyl entry from CHS reg */
        ch = (uptr->CHS >> 24) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense CHS b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->CHS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense CHS b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from CHS reg */
        ch = (uptr->CHS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense CHS b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from CHS reg */
        ch = (uptr->CHS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense CHS b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 4 - mode reg, byte 0 of SNS */
        /* skip the TCMD bit */
        ch = (uptr->SNS >> 24) & 0xfe;          /* return the sense data */
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
        ch = (uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv sense unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if (uptr->STAR == uptr->CHS) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "scsi_srv seek on sector unit=%02x %06x %06x\n",
                    unit, uptr->STAR, uptr->CHS);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                /* we have already seeked to the required sector */
                /* we do not need to seek again, so move on */
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
                return SCPE_OK;
                break;
            } else {
                /* we have wasted enough time, we there */
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "scsi_srv seek over on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR, uptr->CHS);
                uptr->CHS = uptr->STAR;         /* we are there */
#ifdef FAST_FOR_UTX
                sim_activate(uptr, 20);         /* start things off */
#else
                sim_activate(uptr, 40);
#endif
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* Read in 1-4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (i == 0) {
                    sim_debug(DEBUG_DETAIL, dptr,
                        "scsi_srv seek error unit=%02x star %02x %02x %02x %02x\n",
                        unit, buf[0], buf[1], buf[2], buf[3]);
                    /* we have error, bail out */
                    uptr->CMD &= LMASK;           /* remove old status bits & cmd */
                    uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                    break;
                }
                /* done reading, see how many we read */
                if (i == 1) {
                    /* UTX wants to set seek STAR to zero */
                    buf[0] = buf[1] = buf[2] = buf[3] = 0;
                    break;
                }
                /* just read the next byte */
            }
        }
        /* else the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        sim_debug(DEBUG_DETAIL, dptr,
            "scsi_srv seek unit=%02x star %02x%02x%02x%02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);

        sim_debug(DEBUG_DETAIL, dptr,
            "scsi_srv SEEK %08x sector %06x (%d) unit=%02x\n",
            uptr->CMD, uptr->STAR, uptr->STAR, unit);

        /* Check if seek valid */
        if (uptr->STAR >= CAP(type)) {
            sim_debug(DEBUG_CMD, dptr,
                "scsi_srv seek ERROR sector %06x unit=%02x\n",
                uptr->STAR, unit);

            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK; /* set error status */

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* calc the new sector address of data */
        /* calculate file position in bytes of requested sector */
        /* file offset in bytes */
        tstart = uptr->STAR * SSB(type);

        sim_debug(DEBUG_DETAIL, dptr,
            "scsi_srv seek start %04x sector %06x\n",
            tstart, uptr->STAR);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* seek r/w sec */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            sim_debug(DEBUG_DETAIL, dptr, "scsi_srv Error on seek to %08x\n", tstart);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }

        /* Check if already on correct cylinder */
        /* if not, do a delay to slow things down */
        if (uptr->STAR != uptr->CHS) {
            /* Do a fake seek to kill time */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_DETAIL, dptr,
                "scsi_srv seeking unit=%02x to sector %06x\n",
                unit, uptr->STAR);
#ifdef FAST_FOR_UTX
            /* making this value 40 or so create volume mount error on boot */
            sim_activate(uptr, 20);             /* start things off */
#else
            sim_activate(uptr, 40);
#endif
        } else {
            /* we are on cylinder/track/sector, so go on */
            sim_debug(DEBUG_DETAIL, dptr,
                "scsi_srv calc sect addr seek start %08x sector %06x\n",
                tstart, uptr->STAR);
            uptr->CHS = uptr->STAR;             /* set new sector position */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        }
        return SCPE_OK;

    case DSK_XEZ:                               /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "RD REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        uptr->STAR = 0;                         /* set STAR to 0, 0, 0 */
        uptr->CHS = 0;                          /* set current CHS to 0, 0, 0 */
        uptr->CMD &= LMASK;                     /* remove old cmd */
        uptr->CMD |= DSK_SCK;                   /* show as seek command */
        tstart = 0;                             /* byte offset is 0 */

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "scsi_srv Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
        /* we are on cylinder/track/sector zero, so go on */
        sim_debug(DEBUG_DETAIL, dptr, "scsi_srv done seek trk 0\n");
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        return SCPE_OK;
        break;

    case DSK_LMR:
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x\n", unit);
        /* Read in 1 character of mode data */
        if (chan_read_byte(chsa, &buf[0])) {
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x old %x new %x\n",
            unit, (uptr->SNS)&0xff, buf[0]);
        uptr->CMD &= LMASK;                     /* remove old cmd */
        /* leave the TCMD bit */
        uptr->SNS &= (MASK24 | SNS_TCMD);       /* clear old mode data */
        /* do not change TCMD bit */
        uptr->SNS |= ((buf[0]&0xfe) << 24);     /* save mode value */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_RD:                                /* 0x02 Read Data */
        if (uptr->SNS & SNS_TCMD) {
            /* we need to process a read TCMD data */
            int cnt = scsi_buf[bufnum][unit][4];    /* byte count of status to send */
            uint32  cyl = CYL(type);            /* number of cylinders */
            uint32  spt = SPT(type);            /* sectors per track */
            uint32  ssb = SSB(type);            /* sector size in bytes */
            int     bcnt;

            /* cnt has # bytes to return (0xf0) */
            uint8   pagecode = scsi_buf[bufnum][unit][2] & 0x3f;   /* get page code */
            uint8   pagecont = (scsi_buf[bufnum][unit][2] & 0xc0) >> 6; /* get page control */

            ch = scsi_buf[bufnum][unit][0];     /* return TCMD cmd */
            uptr->SNS &= ~SNS_TCMD;             /* show not presessing TCMD cmd chain */
            sim_debug(DEBUG_CMD, dptr,
                "scsi_srv processing TCMD read cmd %02x, chsa %04x tcma %06x cnt %04x\n",
                ch, chsa, chp->ccw_addr, chp->ccw_count);

            switch (ch) {
            case 0x25:                          /* read capacity */
                sim_debug(DEBUG_CMD, dptr,
                    "scsi_srv TCMD read call DSK_RCAP cmd %02x, chsa %04x tcma %06x cnt %04x\n",
                    ch, chsa, chp->ccw_addr, chp->ccw_count);
                goto read_cap;                  /* use IOCL cmd processing */
                break;

            case 0x28:                          /* read 10 byte cmd */
                /* blk is in bytes 2-5, sects is in 7-8 */
                sim_debug(DEBUG_CMD, dptr,
                    "scsi_srv TCMD call read cmd %02x, chsa %04x tcma %06x cnt %04x\n",
                    ch, chsa, chp->ccw_addr, chp->ccw_count);
                tstart = (scsi_buf[bufnum][unit][2] << 24) |    /* get sector address */
                    (scsi_buf[bufnum][unit][3] << 16) |
                    (scsi_buf[bufnum][unit][4] << 8) |
                    (scsi_buf[bufnum][unit][5]);
                bcnt = (scsi_buf[bufnum][unit][8] << 8) |   /* get transfer block count */
                    (scsi_buf[bufnum][unit][9]);
                sim_debug(DEBUG_CMD, dptr,
                    "scsi_srv TCMD call read DATA cmd %02x, chsa %04x buf addr %08x SA %08x cnt %02x\n",
                    ch, chsa, chp->ccw_addr, tstart, bcnt); 

                /* convert sect address to chs value */
                uptr->CHS = tstart;
                /* get byte address for seek */
                tstart = tstart * SSB(type);

                /* just seek to the location where we will r/w data */
                if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                    sim_debug(DEBUG_EXP, dptr, "scsi_srv read TCMD Error on seek to %04x\n", tstart);
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                }
                /* we are on cylinder/track/sector, get data to write */
                sim_debug(DEBUG_DETAIL, dptr, "scsi_srv TCMD done seek\n");
                tstart = uptr->CHS;             /* get sector offset back */
                goto doread;                    /* use IOCL cmd processing */
                break;

            case 0x1a:                          /* mode sense */
                for (i=0; i<cnt; i++) {
                    buf[i] = 0;                 /* clear buffer */
                }
                /* test for "special" mpx status request */
                if (cnt == 0x18 && scsi_buf[bufnum][unit][2] == 0x03) {
                    /* set some sense data from SH.DCSCI driver code */
                    buf[0] = 0xf0;              /* page length */
                    buf[4] = 0x81;              /* savable and page type 1 */
                    buf[8] = 0x91;
                    buf[12] = 0xf4;
                    buf[17] = (uint8)HDS(type); /* # of heads */
                    buf[23] = (uint8)SPT(type); /* Sect/track */
                    goto merge;                 /* go output data and return */
                }
                /* this is most likely UTX calling */
                pagecode = scsi_buf[bufnum][unit][2] & 0x3f;   /* get page code */
                pagecont = (scsi_buf[bufnum][unit][2] & 0xc0) >> 6; /* get page control */
                /* pagecont = 0 return current values */
                /* pagecont = 1 return changable values */
                /* pagecont = 2 return default values */
                /* pagecont = 3 return saved values */
                sim_debug(DEBUG_CMD, dptr,
                    "scsi_srv TCMD read call MOD SEN cmd %02x pgcd %02x pgco %1x chsa %04x tcma %06x cnt %04x\n",
                    ch, pagecode, pagecont, chsa, chp->ccw_addr, chp->ccw_count);
                buf[0] = 0xf0;                  /* page length */
                if (pagecode == 3) {
                    buf[2] = 0;                 /* 0x80 if write protected */
                    buf[3] = 0;                 /* block descriptor length */
//                  buf[4] = 0x83;              /* savable and page type 3 */
                    buf[4] = 0x03;              /* not savable and page type 3 */
                    buf[5] = 22;                /* 22 data bytes follow */
                    buf[6] = 0;                 /* tracks per zone ub */
                    buf[7] = 1;                 /* tracks per zone lb */
                    buf[8] = 0;                 /* alt sec per zone ub */
                    buf[9] = 1;                 /* alt sec per zone lb */
                    buf[10] = 0;                /* alt trks per zone ub */
                    buf[11] = 0;                /* alt trks per zone lb */
                    buf[12] = 0;                /* alt trks per unit ub */
                    buf[13] = 0;                /* alt trks per unit lb */
                    buf[14] = (uint8)((spt & 0xff00) >> 8); /* Sect/track */
                    buf[15] = (uint8)((spt & 0x00ff));  /* Sect/track */
                    buf[16] = (uint8)((ssb & 0xff00) >> 8); /* Sect size */
                    buf[17] = (uint8)((ssb & 0x00ff));  /* Sect size */
                    buf[18] = 0;                /* interleave ub */
                    buf[19] = 0;                /* interleave lb */
                    buf[20] = 0;                /* track skew factor ub */
                    buf[21] = 0;                /* track skew factor lb */
                    buf[22] = 0;                /* cyl skew factor ub */
                    buf[23] = 0;                /* cyl skew factor lb */
//                  buf[24] |= 0x80;            /* soft sectoring */
                    buf[24] |= 0x40;            /* hard sectoring */
//                  buf[24] |= 0x20;            /* drive removable */
//                  buf[24] |= 0x08;            /* inhibit save */
                    goto merge;                 /* go output data and return */
                }
                if (pagecode == 4) {
                    /* num cyl */
                    buf[2] = 0;                 /* 0x80 if write protected */
                    buf[3] = 0;                 /* block descriptor length */
//                  buf[4] = 0x84;              /* savable and page type 4 */
                    buf[4] = 0x04;              /* not savable and page type 4 */
                    buf[5] = 18;                /* 18 data bytes follow */
                    buf[6] = (uint8)((cyl & 0xff0000) >> 16);
                    buf[7] = (uint8)((cyl & 0x00ff00) >> 8);
                    buf[8] = (uint8)((cyl & 0x0000ff));
                    buf[9] = (uint8)HDS(type);  /* # of heads */
                    goto merge;                 /* go output data and return */
                }

            case 0x12:                          /* inquiry */
                /* size is 0x24 = 36 bytes */
                /* ssize has sector size in bytes */
                for (i=0; i<cnt; i++) {
                    buf[i] = 0;                 /* clear buffer */
                }
                /* set some sense data from SH.DCSCI driver code */
                buf[0] = 0xf0;                  /* page length */
                buf[4] = 0x81;                  /* savable and page type 1 */
                buf[8] = 0x91;
                buf[12] = 0xf4;
                buf[17] = (uint8)HDS(type);     /* # of heads */
                buf[23] = (uint8)SPT(type);     /* Sect/track */

merge:
                /* output response data */
                for (i=0; i<cnt; i++) {
                    if (chan_write_byte(chsa, &buf[i])) {
                        /* we have error, bail out */
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                        return SCPE_OK;
                    }
                }
                sim_debug(DEBUG_DETAIL, dptr,
                "scsi_srv TCMD inq read data chsa=%02x data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                chsa, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
                sim_debug(DEBUG_DETAIL, dptr,
                "scsi_srv TCMD inq read data chsa=%02x data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                chsa, buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
                sim_debug(DEBUG_DETAIL, dptr,
                "scsi_srv TCMD inq read data chsa=%02x data %02x%02x%02x%02x %02x%02x%02x%02x\n",
                chsa, buf[16], buf[17], buf[18], buf[19], buf[20], buf[21], buf[22], buf[23]);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                return SCPE_OK;
                break;

            case 0x00:                          /* test unit ready */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                sim_debug(DEBUG_CMD, dptr, "scsi_srv test unit ready cmd %02x unit %02x\n", ch, unit);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                return SCPE_OK;
                break;

            default:                            /* bad or unsupported scsi command */
                sim_debug(DEBUG_CMD, dptr, "invalid scsi read command %02x unit %02x\n", ch, unit);
                uptr->SNS |= SNS_CMDREJ;
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
        }

doread:
        /* normal disk read starts here */
        /* tstart has start of sector address in bytes */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "SCSI READ starting unit=%02x CMD %08x count %04x\n",
                unit, uptr->CMD, chp->ccw_count);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            tstart = uptr->CHS;                 /* get sector offset */

            sim_debug(DEBUG_CMD, dptr,
                "SCSI READ reading CMD %08x chsa %04x tstart %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, tstart, chp->ccw_addr, chp->ccw_count);

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart*SSB(type), SEEK_SET)) != 0) {  /* seek r/w sec */
                sim_debug(DEBUG_DETAIL, dptr,
                    "scsi_srv READ, Error on seek to %08x\n", tstart*SSB(type));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }

            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile sector %06x\n",
                    len, ssize, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr, "scsi_srv after READ chsa %04x count %04x\n",
                chsa, chp->ccw_count);

            /* process the sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    sim_debug(DEBUG_DATA, dptr,
                        "SCSI Read %04x bytes leaving %04x from diskfile sector %06x\n",
                        i, chp->ccw_count, tstart);
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    return SCPE_OK;
                }
            }

            sim_debug(DEBUG_CMD, dptr,
                "SCSI READ %04x bytes leaving %4x to be read to %06x from diskfile sector %06x\n",
                ssize, chp->ccw_count, chp->ccw_addr+4, tstart);

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_DATA, dptr,
                    "SCSI Read complete for read from diskfile sector %06x\n",
                    uptr->CHS);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            uptr->CHS = tstart;                 /* new position */
            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "SCSI Read reached EOM for read from disk @ sector %06x\n",
                    tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_DATA, dptr,
                "SCSI sector read complete, %x bytes to go from diskfile sector %06x\n",
                chp->ccw_count, uptr->CHS);
#ifdef FAST_FOR_UTX
//zz        sim_activate(uptr, 10);             /* start things off */
            sim_activate(uptr, 15);             /* start things off */
#else
            sim_activate(uptr, 10);             /* wait to read next sector */
//BAD4MPX   sim_activate(uptr, 40);             /* wait to read next sector */
#endif
            break;
        }
        break;

    case DSK_WD:            /* Write Data */
        if (uptr->SNS & SNS_TCMD) {
            /* we need to process a write TCMD data */
//?         int cnt = scsi_buf[bufnum][unit][4];    /* byte count of status to read */
            /* cnt has # bytes to read */
            int cnt = chp->ccw_count;           /* byte count of status to read */

            ch = scsi_buf[bufnum][unit][0];     /* return TCMD cmd */
            uptr->SNS &= ~SNS_TCMD;             /* show not presessing TCMD cmd chain */
            sim_debug(DEBUG_CMD, dptr,
                "scsi_srv processing TCMD write cmd %02x, chsa %04x tcma %06x cnt %04x\n",
                ch, chsa, chp->ccw_addr, chp->ccw_count);

            switch (ch) {

            case 0x4:                           /* write 6 byte cmd, format disk  */
                if (scsi_buf[bufnum][unit][2] == 10) {
                    if ((--scsi_buf[bufnum][unit][3]) > 0) {
                        uptr->SNS |= SNS_TCMD;   /* show still presessing TCMD cmd chain */
                        sim_activate(uptr, 200000);  /* wait a while */
                        return SCPE_OK;
                        break;
                    }
                    scsi_buf[bufnum][unit][2] = 0;  /* show done */
                }
                /* MPX cmd data 04 18 00 00 00 00 */
                /* Format unit */
                len = 0;
                for (i=0; i<cnt; i++) {
                    if (chan_read_byte(chsa, &ch)) {/* get a byte from memory */
                        /* if error on reading 1st byte, we are done writing */
                        if (i == 0) {
                            uptr->CMD &= LMASK; /* remove old status bits & cmd */
                            sim_debug(DEBUG_CMD, dptr,
                                "SCSI Read %04x bytes from MPX buffer\n", len);
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                            return SCPE_OK;
                        }
                        ch = 0;                 /* finish out the sector with zero */
                        len++;                  /* show we have no more data to write */
                    }
                    buf2[i] = ch;               /* save the char */
                }
                sim_debug(DEBUG_CMD, dptr, "SCSI CMD 4 %04x bytes read from MPX buffer\n", cnt);
                sim_debug(DEBUG_CMD, dptr,
                "SCSI Disk format buf2 %02x%02x%02x%02x\n", buf2[0], buf2[1], buf2[2], buf2[3]);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                return SCPE_OK;
                break;

            case 0x15:                          /* write 6 byte cmd, format disk  */
                /* MODE select */
                len = 0;
                for (i=0; i<cnt; i++) {
                    if (chan_read_byte(chsa, &ch)) {/* get a byte from memory */
                        /* if error on reading 1st byte, we are done writing */
                        if (i == 0) {
                            uptr->CMD &= LMASK; /* remove old status bits & cmd */
                            sim_debug(DEBUG_CMD, dptr,
                                "SCSI Read %04x bytes from MPX buffer\n", len);
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                            return SCPE_OK;
                        }
                        ch = 0;                 /* finish out the sector with zero */
                        len++;                  /* show we have no more data to write */
                    }
                    buf2[i] = ch;               /* save the char */
                }
                sim_debug(DEBUG_CMD, dptr, "SCSI Format %04x bytes to status buffer\n", cnt);
                sim_debug(DEBUG_CMD, dptr,
        "SCSI Disk format buf2 %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                buf2[0], buf2[1], buf2[2], buf2[3], buf2[4], buf2[5], buf2[6], buf2[7],
                buf2[8], buf2[9], buf2[10], buf2[11], buf2[12], buf2[13], buf2[14], buf2[15]);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                return SCPE_OK;
                break;

            case 0x2a:                          /* write 10 byte cmd */
                tstart = (scsi_buf[bufnum][unit][2] << 24) |    /* get sector address */
                    (scsi_buf[bufnum][unit][3] << 16) |
                    (scsi_buf[bufnum][unit][4] << 8) |
                    (scsi_buf[bufnum][unit][5]);
                sim_debug(DEBUG_CMD, dptr,
                    "scsi_srv TCMD call write DATA cmd %02x, chsa %04x addr %08x data %08x %08x\n",
                    ch, chsa, chp->ccw_addr, RMW(chp->ccw_addr), RMW(chp->ccw_addr+4)); 

                /* convert sect address to chs value */
                uptr->CHS = tstart;             /* set seek sector address */
                /* get byte address for seek */
                tstart = tstart * SSB(type);

                /* just seek to the location where we will r/w data */
                if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                    sim_debug(DEBUG_EXP, dptr, "scsi_srv TCMD Error on seek to %04x\n", tstart);
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                }
                /* we are on cylinder/track/sector, get data to write */
                sim_debug(DEBUG_DETAIL, dptr, "scsi_srv TCMD done seek\n");
                tstart = uptr->CHS;             /* get sector offset */
                goto dowrite;
                break;

            default:                            /* bad or unsupported scsi command */
                sim_debug(DEBUG_CMD, dptr, "invalid scsi write command %02x unit %02x\n", ch, unit);
                uptr->SNS |= SNS_CMDREJ;
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
            break;
        }
dowrite:
        /* tstart has file offset in sectors */
        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            uptr->CMD |= DSK_WRITING;           /* write to disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "SCSI WRITE starting unit=%02x CMD %02x write %4x from %06x to sector %06x\n",
                unit, uptr->CMD, chp->ccw_count, chp->ccw_addr, uptr->CHS);
        }
        if (uptr->CMD & DSK_WRITING) {          /* see if we are writing data */
            tstart = uptr->CHS;                 /* get sector offset */

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart*SSB(type), SEEK_SET)) != 0) {  /* seek r/w sec */
                sim_debug(DEBUG_EXP, dptr,
                    "scsi_srv WRITE, Error on seek to %08x\n", tstart*SSB(type));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }

            /* process the next sector of data */
            len = 0;                            /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {/* get a byte from memory */
                    /* if error on reading 1st byte, we are done writing */
                    if (i == 0) {
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "SCSI Wrote %04x bytes to diskfile sector %06x\n",
                            ssize, tstart);
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                        return SCPE_OK;
                    }
                    ch = 0;                     /* finish out the sector with zero */
                    len++;                      /* show we have no more data to write */
                }
                buf2[i] = ch;                   /* save the char */
            }

            /* write the sector to disk */
            if ((i=sim_fwrite(buf2, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on write %04x bytes to diskfile sector %06x\n",
                    i, ssize, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (len != 0) {                     /* see if done with write command */
                sim_debug(DEBUG_DATA, dptr,
                    "SCSI WroteB %04x bytes to diskfile sector %06x\n",
                    ssize, tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            sim_debug(DEBUG_CMD, dptr,
                "SCSI WR to sec end %04x bytes end %04x to diskfile sector %06x\n",
                len, ssize, tstart);

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            uptr->CHS = tstart;                 /* save new sector */
            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "SCSI Write reached EOM for write to disk @ sector %06x\n",
                    uptr->CHS);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
#ifdef FAST_FOR_UTX
//zz        sim_activate(uptr, 10);             /* start things off */
            sim_activate(uptr, 20);             /* start things off */
#else
            sim_activate(uptr, 10);             /* keep writing */
//BAD4MPX   sim_activate(uptr, 40);             /* keep writing */
#endif
            break;
        }
        break;

    case DSK_RCAP:                              /* Read Capacity 0x53 */
        /* return 8 bytes */
        /* wd 1 disk size in sectors */
        /* wd 2 is sector size in bytes */
        /* cap has disk capacity */
read_cap:                                       /* merge point from TCMD processing */
#if 0
        cpu_dev.dctrl |= DEBUG_INST;            /* start instruction trace */
#endif
        for (i=0; i<4; i++) {
            /* I think they want cap-1, not cap?????????? */
            /* verified that MPX wants cap-1, else J.VFMT aborts */
            ch = ((cap-1) >> ((3-i)*8)) & 0xff; /* use cap-1 */
            if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
        }
        /* ssize has sector size in bytes */
        for (i=0; i<4; i++) {
            ch = (((uint32)ssize) >> ((3-i)*8)) & 0xff;
            if (chan_write_byte(chsa, &ch)) {
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
        }

        /* command is completed */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv cmd RCAP chsa %04x capacity %06x secsize %03x completed\n",
            chsa, cap, ssize);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    /* Transfer Command Packet (specifies CDB to send) */
    /* address points to CDB */
    case DSK_TCMD:                              /* Transfer command packet 0xD3 */
        {
        uint32  mema;                           /* memory address */
        int32   i;

        uptr->SNS &= ~SNS_TCMD;                 /* show not presessing TCMD cmd chain */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv starting TCMD cmd, chsa %04x tcma %06x cnt %04x\n",
            chsa, chp->ccw_addr, chp->ccw_count);

#if 0
        cpu_dev.dctrl |= DEBUG_INST;            /* start instruction trace */
#endif
        /* mema has IOCD word 1 contents. */
        /* len has the byte count from IOCD wd2 */
        len = chp->ccw_count;                   /* TCMD command count */

        for (i=0; i < len; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
        }
        sim_debug(DEBUG_DETAIL, dptr,
    "scsi_srv TCMD data chsa=%02x data %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            chsa, buf[0], buf[1], buf[2], buf[3], buf[4],
            buf[5], buf[6], buf[7], buf[8], buf[9]);

        /* save the CMD packet */
        for (i=0; i < len; i++) {
            scsi_buf[bufnum][unit][i] = buf[i]; /* save the cmd */
        }
        scsi_pcmd[bufnum][unit] = buf[0];       /* save the cmd */

        /* if this a disk format, do a big wait */
        if ((buf[0] == 4) && (buf[1] == 0x18)) {
            scsi_buf[bufnum][unit][2] = 10;     /* show Processing Format cmd */
            scsi_buf[bufnum][unit][3] = 10;     /* show Processing cmd */
        }

        /* see if just test unit ready */
        if ((buf[0] == 0) && (len == 6))
            uptr->SNS &= ~SNS_TCMD;             /* clear TCMD flag */
        else
            uptr->SNS |= SNS_TCMD;              /* show Processing CMD cmd chain */

        /* command is completed */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scsi_srv cmd TCMD chsa %04x addr %06x count %04x completed\n",
            chsa, mema, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        }
        return SCPE_OK;
        break;

    default:
        sim_debug(DEBUG_EXP, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        return SNS_CHNEND|STATUS_PCHK;
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "scsi_srv done cmd %02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);
    return SCPE_OK;
}

/* initialize the disk */
void scsi_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     i = GET_TYPE(uptr->flags);

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */
    uptr->STAR = 0;                             /* set STAR to cyl/hd/sec = 0 */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    uptr->SNS = 0;                              /* clear any status */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* disk size in sectors */
    sim_cancel(uptr);                           /* stop any timers */

    sim_debug(DEBUG_EXP, &sba_dev, "SCSI init device %s on unit SBA%04x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

/* handle rschnlio cmds for scsi */
t_stat  scsi_rschnlio(UNIT *uptr) {
    DEVICE  *dptr = get_dev(uptr);
    uint16  chsa = GET_UADDR(uptr->CMD);
    int     cmd = uptr->CMD & DSK_CMDMSK;

    sim_debug(DEBUG_EXP, dptr,
        "scsi_rschnl chsa %04x cmd = %02x\n", chsa, cmd);
    scsi_ini(uptr, 0);                          /* reset the unit */
    return SCPE_OK;
}

t_stat scsi_reset(DEVICE *dptr)
{
    UNIT    *uptr = dptr->units;
    uint16  chsa = GET_UADDR(uptr->CMD);

    /* add reset code here */
    sim_debug(DEBUG_EXP, dptr,
        "scsi_reset chsa %04x\n", chsa);
    return SCPE_OK;
}

/* create the disk file for the specified device */
int scsi_format(UNIT *uptr) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    int32       ssize = scsi_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32      tsize = scsi_type[type].spt;    /* get track size in sectors */
    uint32      csize = scsi_type[type].nhds * tsize;   /* get cylinder size in sectors */
    uint32      cyl = scsi_type[type].cyl;      /* get # cyl */
    uint32      cap = scsi_type[type].cyl * csize;  /* disk capacity in sectors */
    uint32      cylv = cyl;                     /* number of cylinders */
    uint8       *buff;
    int         i;
    t_stat      oldsw = sim_switches;           /* save switches */

                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;          /* last sector of disk */

                /* last track address of disk (cyl * hds * spt) - spt */
    uint32      ltaddr = CAP(type)-SPT(type);   /* last sector of disk */

                /* get sector address of vendor defect table VDT */
                /* put data = 0xf0000000 0xf4000000 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);

                /* get sector address of utx diag map (DMAP) track 0 pointer */
                /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                /* 0x9a000000 + (cyl-1), 0xf4000008 */
    int32       daddr = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);

                /* get sector address of utx flaw data (1 track long) */
                /* set trace data to zero */
    int32       faddr = (CYL(type)-4) * SPC(type) + (HDS(type)-3) * SPT(type);

                /* get sector address of utx flaw map sec 1 pointer */
                /* use this address for sec 1 label pointer */
    int32       uaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-4) * SPT(type);

                /* last user block available */
    int32       luaddr = (CYL(type)-4) * SPC(type);

                /* make up a UMAP with the partiton data for 9346 disk */
    uint32      umap[256] =
                {
                    /* try to makeup a utx dmap */
                    0x4e554d50,(cap-1),(uint32)(luaddr-1),0,0,0,0,0xe10,
                    0,0x5320,0,0x4e60,0x46,(uint32)luaddr,0,0xd360,
                    0x88,0x186b0,0x13a,0xd100,0x283,0,0,0,
                    0,0x22c2813e,0,0x06020000,0xf4,0,0x431b1c,0,
                };

                /* vendor flaw map in vaddr */
    uint32      vmap[2] = {0xf0000004, 0xf4000000};

                /* defect map */
    uint32      dmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | (cap-1), 0xf4000000};

                /* utx flaw map */
    uint32      fmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | ltaddr, 0xf4000000};

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

#if 0
    /* see if user wants to initialize the disk */
    if (!get_yn("Initialize disk? [Y] ", TRUE)) {
        return 1;
    }
#endif

    /* VDT  249264 (819/18/0) 0x3cdb0 for 9346 - 823/19/16 vaddr */
    /* MDT  249248 (819/17/0) 0x3cda0 for 9346 - 823/19/16 daddr */
    /* DMAP 249232 (819/16/0) 0x3cd90 for 9346 - 823/19/16 faddr */
    /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 uaddr */

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
    }

    /* get buffer for track data */
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
        fmap[i] = (((fmap[i] & 0xff) << 24) | ((fmap[i] & 0xff00) << 8) |
            ((fmap[i] & 0xff0000) >> 8) | ((fmap[i] >> 24) & 0xff));
    }
    for (i=0; i<256; i++) {
        umap[i] = (((umap[i] & 0xff) << 24) | ((umap[i] & 0xff00) << 8) |
            ((umap[i] & 0xff0000) >> 8) | ((umap[i] >> 24) & 0xff));
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
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
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
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing DMAP to sect %06x offset %06x\n",
        daddr, daddr*ssize);
        return 1;
    }

    /* write dummy DMAP to faddr */
    if ((sim_fseek(uptr->fileref, faddr*ssize, SEEK_SET)) != 0) { /* seek DMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on media flaw map seek to sect %06x offset %06x\n",
        faddr, faddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing flaw map to sect %06x offset %06x\n",
        faddr, faddr*ssize);
        return 1;
    }

    /* write UTX umap to uaddr */
    if ((sim_fseek(uptr->fileref, uaddr*ssize, SEEK_SET)) != 0) { /* seek UMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on umap seek to sect %06x offset %06x\n",
        uaddr, uaddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&umap, sizeof(uint32), 256, uptr->fileref)) != 256) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing UMAP to sect %06x offsewt %06x\n",
        uaddr, uaddr*ssize);
        return 1;
    }

    printf("SCSI Disk %s has %x (%d) cyl, %x (%d) hds, %x (%d) sec\r\n",
        scsi_type[type].name, CYL(type), CYL(type), HDS(type), HDS(type),
        SPT(type), SPT(type));
    printf("writing to vmap sec %x (%d) bytes %x (%d)\n",
        vaddr, vaddr, (vaddr)*ssize, (vaddr)*ssize);
    printf("writing to flaw map sec %x (%d) bytes %x (%d)\n",
        faddr, faddr, (faddr)*ssize, (faddr)*ssize);
    printf("writing dmap to %x %d %x %d dmap to %x %d %x %d\n",
       cap-1, cap-1, (cap-1)*ssize, (cap-1)*ssize,
       daddr, daddr, daddr*ssize, daddr*ssize);
    printf("writing to umap sec %x (%d) bytes %x (%d)\n",
        uaddr, uaddr, (uaddr)*ssize, (uaddr)*ssize);

    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    return 0;
}

/* attach the selected file to the disk */
t_stat scsi_attach(UNIT *uptr, CONST char *file) {
    uint16          chsa = GET_UADDR(uptr->CMD);
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = 0;
    t_stat          r, s;
    uint32          ssize;                      /* sector size in bytes */
    uint32          info, good,zmap = 0x5a4d4150;   /* ZMAP */
    uint8           buff[1024];
    int             i, j;

                    /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32          laddr = CAP(type) - 1;      /* last sector of disk */
                    /* defect map */
    uint32          dmap[4] = {0xf0000000 | (CAP(type)-1), 0x8a000000,
                        0x9a000000 | (CAP(type)-1), 0xf4000000};

    for (i=0; i<4; i++) {                       /* byte swap data for last sector */
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }

    uptr->SNS = 0;                              /* clear any status */

    /* see if valid disk entry */
    if (scsi_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    if (dptr->flags & DEV_DIS) {
        fprintf(sim_deb,
            "ERROR===ERROR\nSCSI Disk device %s disabled on system, aborting\r\n",
            dptr->name);
        printf("ERROR===ERROR\nSCSI Disk device %s disabled on system, aborting\r\n",
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

    sim_debug(DEBUG_CMD, dptr, "SCSI Disk %s %04x cyl %d hds %d sec %d ssiz %d capacity %d\n",
        scsi_type[type].name, chsa, scsi_type[type].cyl, scsi_type[type].nhds, 
        scsi_type[type].spt, ssize, uptr->capac); /* disk capacity */
    printf("SCSI Disk %s %04x cyl %d hds %d sec %d ssiz %d capacity %d\r\n",
        scsi_type[type].name, chsa, scsi_type[type].cyl, scsi_type[type].nhds, 
        scsi_type[type].spt, ssize, uptr->capac); /* disk capacity */

    /* see if -i or -n specified on attach command */
    if ((sim_switches & SWMASK('N')) || (sim_switches & SWMASK('I'))) {
        goto fmt;                               /* user wants new disk */
    }

    /* seek to end of disk */
    if ((sim_fseek(uptr->fileref, 0, SEEK_END)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach SEEK end failed\n");
        printf( "SCSI Disk attach SEEK end failed\r\n");
        goto fmt;                               /* not setup, go format */
    }

    s = ftell(uptr->fileref);                   /* get current file position */
    if (s == 0) {
        sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach ftell failed s=%06d\n", s);
        printf("SCSI Disk attach ftell failed s=%06d\r\n", s);
        goto fmt;                               /* not setup, go format */
    }
    sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach ftell value s=%06d b=%06d CAP %06d\n", s/ssize, s, CAP(type));
    printf("SCSI Disk attach ftell value s=%06d b=%06d CAP %06d\r\n", s/ssize, s, CAP(type));

    if (((int)s/(int)ssize) < ((int)CAP(type))) {   /* full sized disk? */
        j = (CAP(type) - (s/ssize));            /* get # sectors to write */
        sim_debug(DEBUG_CMD, dptr,
            "SCSI Disk attach for MPX 1.X needs %04d more sectors added to disk\n", j);
        printf("SSFI Disk attach for MPX 1.X needs %04d more sectors added to disk\r\n", j);
        /* must be MPX 1.X disk, extend to MPX 3.X size */
        /* write sectors of zero to end of disk to fill it out */
        for (i=0; i<j; i++) {
            if ((r = sim_fwrite(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
                sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach fread ret = %04d\n", r);
                printf("SCSI Disk attach fread ret = %04d\r\n", r);
                goto fmt;                       /* not setup, go format */
            }
        }
        s = ftell(uptr->fileref);               /* get current file position */
        sim_debug(DEBUG_CMD, dptr,
            "SCSI Disk attach MPX 1.X file extended & sized secs %06d bytes %06d\n", s/ssize, s);
        printf("SCSI Disk attach MPX 1.X  file extended & sized secs %06d bytes %06d\r\n", s/ssize, s);
    }

    /* seek last sector of disk */
    if ((sim_fseek(uptr->fileref, (CAP(type)-1)*ssize, SEEK_SET)) != 0) {
        sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach SEEK last sector failed\n");
        printf("SCSI Disk attach SEEK last sector failed\r\n");
        goto fmt;
    }

    /* see if there is disk size-1 in last sector of disk, if not add it */
    if ((r = sim_fread(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        sim_debug(DEBUG_CMD, dptr, "SCSI Disk format fread error = %04d\n", r);
        printf("SCSI Disk format fread error = %04d\r\n", r);
add_size:
        if (ssize == 768) {
            /* assume we have MPX 1x, and go on */
            /* write dmap data to last sector on disk for mpx 1.x */
            if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
                sim_debug(DEBUG_CMD, dptr,
                    "SCSI Error on last sector seek to sect %06d offset %06d bytes\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                printf("SCSI Error on last sector seek to sect %06d offset %06d bytes\r\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                goto fmt;
            }
            if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
                sim_debug(DEBUG_CMD, dptr,
                    "SCSI Error writing DMAP to sect %06x offset %06d bytes\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                printf("SCSI Error writing DMAP to sect %06x offset %06d bytes\r\n",
                    (CAP(type)-1), (CAP(type)-1)*ssize);
                goto fmt;
            }

            /* seek last sector of disk */
            if ((sim_fseek(uptr->fileref, (CAP(type))*ssize, SEEK_SET)) != 0) {
                sim_debug(DEBUG_CMD, dptr, "SCSI Disk attach SEEK last sector failed\n");
                printf( "SCSI Disk attach SEEK last sector failed\r\n");
                goto fmt;
            }
            s = ftell(uptr->fileref);           /* get current file position */
            sim_debug(DEBUG_CMD, dptr,
                "SCSI Disk attach MPX file extended & sized secs %06d bytes %06d\n", s/ssize, s);
            printf("SCSI Disk attach MPX file extended & sized secs %06d bytes %06d\r\n", s/ssize, s);
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
                "SCSI Disk format0 buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
                buff[0], buff[1], buff[2], buff[3]);
            goto add_size;
        }
    }

    /* the last sector is used by UTX for a ZMAP, so if there we are good to go */
    info = (buff[0]<<24) | (buff[1]<<16) | (buff[2]<<8) | buff[3];
    good = 0xf0000000 | (CAP(type)-1);
    /* check for 0xf0ssssss where ssssss is disk size-1 in sectors */
    if ((info != good) && (info != zmap)) {
        sim_debug(DEBUG_CMD, dptr,
            "SCSI Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
            buff[0], buff[1], buff[2], buff[3]);
        printf("SCSI Disk format error buf0 %02x buf1 %02x buf2 %02x buf3 %02x\r\n",
            buff[0], buff[1], buff[2], buff[3]);
fmt:
        /* format the drive */
        if (scsi_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }
ldone:
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_FMT;                        /* error */
    }

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    sim_debug(DEBUG_CMD, dptr,
        "SCSI Attach %s %04x cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        scsi_type[type].name, chsa, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    printf("SCSI Attach %s %04x cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\r\n",
        scsi_type[type].name, chsa, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, dptr, "SCSI File %s at chsa %04x attached to %s is ready\n",
        file, chsa, scsi_type[type].name);
    printf("SCSI File %s at chsa %04x attached to %s is ready\r\n",
        file, chsa, scsi_type[type].name);

    /* check for valid configured disk */
    /* must have valid DIB and Channel Program pointer */
    dibp = (DIB *)dptr->ctxt;                   /* get the DIB pointer */
    if ((dib_unit[chsa] == NULL) || (dibp == NULL) || (dibp->chan_prg == NULL)) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nSCSI device %s not configured on system, aborting\n",
            dptr->name);
        printf("ERROR===ERROR\nSCSI device %s not configured on system, aborting\n",
            dptr->name);
        detach_unit(uptr);                      /* detach if error */
        return SCPE_UNATT;                      /* error */
    }
    set_devattn(chsa, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat scsi_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= LMASK;                         /* no cmd and flags */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* Handle haltio transfers for disk */
t_stat  scsi_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         cmd = uptr->CMD & DSK_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

//  sim_debug(DEBUG_EXP, dptr,
    sim_debug(DEBUG_DETAIL, dptr,
        "scsi_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & DSK_CMDMSK) != 0) {        /* is unit busy */
        sim_debug(DEBUG_EXP, dptr,
            "scsi_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n",
            chsa, cmd, chp->ccw_count);
        sim_cancel(uptr);                       /* clear the input timer */
    } else {
        sim_debug(DEBUG_DETAIL, dptr,
            "scsi_haltio HIO I/O not busy chsa %04x cmd = %02x\n", chsa, cmd);
    }
    /* stop any I/O and post status and return error status */
    chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);       /* stop any chaining */
    uptr->CMD &= LMASK;                         /* no cmd and flags */
    uptr->SNS &= ~MASK24;                       /* clear all but old mode data */
    sim_debug(DEBUG_EXP, dptr,
        "scsi_haltio HIO I/O stop chsa %04x cmd = %02x CHS %08x STAR %08x\n",
        chsa, cmd, uptr->CHS, uptr->STAR);
    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);      /* force end */
//  chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* force end */
    return SCPE_IOERR;
}

/* boot from the specified disk unit */
t_stat scsi_boot(int32 unit_num, DEVICE *dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr, "SCSI Disk Boot dev/unit %04x\n", GET_UADDR(uptr->CMD));

    /* see if device disabled */
    if (dptr->flags & DEV_DIS) {
        printf("ERROR===ERROR\r\nSCSI Disk device %s disabled on system, aborting\r\n",
            dptr->name);
        return SCPE_UDIS;                       /* device disabled */
    }

    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr, "SCSI Disk Boot attach error dev/unit %04x\n",
            GET_UADDR(uptr->CMD));
        printf("SCSI Disk Boot attach error dev/unit %04x\n", GET_UADDR(uptr->CMD));
        return SCPE_UNATT;                      /* attached? */
    }

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        printf("SCSI Disk Boot Error on seek to 0\n");
    }

    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */
    return chan_boot(GET_UADDR(uptr->CMD), dptr);    /* boot the ch/sa */
}

/* Disk option setting commands */
t_stat scsi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; scsi_type[i].name != 0; i++) {
        if (strcmp(scsi_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear the old UNIT type */
            uptr->flags |= SET_TYPE(i);         /* set the new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat scsi_get_type(FILE * st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(scsi_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat scsi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
    int i;
    fprintf (st, "SEL-32 MFP SCSI Bus Disk Controller\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; scsi_type[i].name != 0; i++) {
        fprintf(st, "%s", scsi_type[i].name);
        if (scsi_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; scsi_type[i].name != 0; i++) {
        int32   size = CAPB(i);                 /* disk capacity in bytes */
        size /= 1024;                           /* make KB */
        size = (10 * size) / 1024;              /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            scsi_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *scsi_description (DEVICE *dptr)
{
    return "SEL-32 MFP SCSI Disk Controller";
}

#endif
