/* vax4xx_rz80.c: NCR 5380 SCSI controller

   Copyright (c) 2019, Matt Burke

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   rz           SCSI controller
*/

#include "vax_defs.h"
#include "sim_scsi.h"
#include "vax_rzdev.h"

#if defined(VAX_420)
#include "vax_ka420_rzrz_bin.h"
#else
#define BOOT_CODE_ARRAY NULL
#define BOOT_CODE_SIZE  0
#endif

#define RZ_NUMCT        2
#define RZ_NUMDR        8
#define RZ_CTLR         (RZ_NUMDR)

#define iflgs           u3
#define cnum            u4

/* Mode Register */

#define MODE_BLOCK      0x80                            /* DMA block mode */
#define MODE_TARG       0x40                            /* Target role */
#define MODE_PARCK      0x20                            /* Parity check enable */
#define MODE_INTPAR     0x10                            /* Interrupt on parity error */
#define MODE_INTEOP     0x08                            /* Interrupt on DMA end */
#define MODE_MONBSY     0x04                            /* Monitor BSY */
#define MODE_DMA        0x02                            /* Enable DMA transfer */
#define MODE_ARB        0x01                            /* Start arbitration */

/* Initiator Command Register */

#define ICMD_RST        0x80                            /* Assert RST */
#define ICMD_AIP        0x40                            /* Arbitration in progress */
#define ICMD_TEST       0x40                            /* Test mode */
#define ICMD_LA         0x20                            /* Lost arbitration */
#define ICMD_DIFF       0x20                            /* Differential enable */
#define ICMD_ACK        0x10                            /* Assert ACK */
#define ICMD_BSY        0x08                            /* Assert BSY */
#define ICMD_SEL        0x04                            /* Assert SEL */
#define ICMD_ATN        0x02                            /* Assert ATN */
#define ICMD_ENOUT      0x01                            /* Enable output */

/* Target Command Register */

#define TCMD_REQ        0x08                            /* Assert REQ */
#define TCMD_MSG        0x04                            /* Assert MSG */
#define TCMD_CD         0x02                            /* Assert C/D */
#define TCMD_IO         0x01                            /* Assert I/O */
#define TCMD_PHASE      0x07                            /* Phase bits */

/* Bus and Status Register */

#define STS_DMAEND      0x80                            /* DMA end */
#define STS_DMAREQ      0x40                            /* DMA request */
#define STS_PARERR      0x20                            /* Parity error */
#define STS_INTREQ      0x10                            /* Interrupt request */
#define STS_MATCH       0x08                            /* Phase match */
#define STS_BSYERR      0x04                            /* Busy error */
#define STS_ATN         0x02                            /* ATN asserted */
#define STS_ACK         0x01                            /* ACK asserted */

/* Current Bus Status Register */

#define CSTAT_RST       0x80                            /* RST asserted */
#define CSTAT_BSY       0x40                            /* BSY asserted */
#define CSTAT_REQ       0x20                            /* REQ asserted */
#define CSTAT_MSG       0x10                            /* MSG asserted */
#define CSTAT_CD        0x08                            /* C/D asserted */
#define CSTAT_IO        0x04                            /* I/O asserted */
#define CSTAT_SEL       0x02                            /* SEL asserted */
#define CSTAT_DBP       0x01                            /* Databus parity */
#define CSTAT_V_PHASE   2
#define CSTAT_M_PHASE   0x07                            /* Phase bits */

#define DBG_REG         0x0001                          /* registers */
#define DBG_CMD         0x0002                          /* display commands */
#define DBG_INT         0x0004                          /* display transfer requests */
#define DBG_DSK         0x0008                          /* display sim_disk activities */

#define PH_DATA_OUT     0
#define PH_DATA_IN      1
#define PH_COMMAND      2
#define PH_STATUS       3
#define PH_MSG_OUT      6
#define PH_MSG_IN       7

#define UA_SELECT       0

#define UNIT_V_DTYPE    (SCSI_V_UF + 0)                 /* drive type */
#define UNIT_M_DTYPE    0x1F
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

typedef struct {
    uint32 cnum;                                        /* ctrl number */
    uint8 odata;                                        /* output data */
    uint8 cdata;                                        /* current data */
    uint32 mode;                                        /* mode reg */
    uint32 icmd;                                        /* initiator cmd reg */
    uint32 tcmd;                                        /* target cmd reg */
    uint32 status;                                      /* status reg */
    uint32 cstat;
    uint32 selen;                                       /* select enable reg */
    uint32 dcount;                                      /* DMA count reg */
    uint32 daddr;                                       /* DMA addr reg */
    t_bool daddr_low;                                   /* DMA addr flag */
    uint32 ddir;                                        /* DMA dir */
    uint8 *buf;                                         /* unit buffer */
    int32 buf_ptr;                                      /* current buffer pointer */
    int32 buf_len;                                      /* current buffer length */
    SCSI_BUS bus;                                       /* SCSI bus state */
    } CTLR;

t_stat rz_svc (UNIT *uptr);
t_stat rz_isvc (UNIT *uptr);
t_stat rz_reset (DEVICE *dptr);
t_stat rz_attach (UNIT *uptr, CONST char *cptr);
t_stat rz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat rz_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rz_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void rz_update_status (CTLR *rz);
void rz_setint (CTLR *rz, uint32 flags);
void rz_clrint (CTLR *rz);
void rz_sw_reset (CTLR *rz);
void rz_ack (CTLR *rz);
int32 rz_parity (int32 val, int32 odd);
const char *rz_description (DEVICE *dptr);

/* RZ data structures

   rz_dev      RZ device descriptor
   rz_unit     RZ unit descriptor
   rz_reg      RZ register list
*/

CTLR rz_ctx = { 0 };

UNIT rz_unit[] = {
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_isvc, UNIT_DIS, 0) }
    };

REG rz_reg[] = {
    { FLDATAD ( INT,    int_req[IPL_SCA], INT_V_SCA, "interrupt pending flag") },
    { NULL }
    };

DEBTAB rz_debug[] = {
    { "REG",  DBG_REG,      "Register activity" },
    { "CMD",  DBG_CMD,      "Chip commands" },
    { "INT",  DBG_INT,      "Interrupts" },
    { "SCMD", SCSI_DBG_CMD, "SCSI commands" },
    { "SMSG", SCSI_DBG_MSG, "SCSI messages" },
    { "SBUS", SCSI_DBG_BUS, "SCSI bus activity" },
    { "SDSK", SCSI_DBG_DSK, "SCSI disk activity" },
    { 0 }
};

MTAB rz_mod[] = {
    { SCSI_WLK,         0, NULL, "WRITEENABLED",
      &scsi_set_wlk, NULL, NULL, "Write enable disk drive" },
    { SCSI_WLK,  SCSI_WLK, NULL, "LOCKED",
      &scsi_set_wlk, NULL, NULL, "Write lock disk drive"  },
    { MTAB_XTD|MTAB_VUN, 0, "WRITE", NULL,
      NULL, &scsi_show_wlk, NULL, "Display drive writelock status" },
    { MTAB_XTD|MTAB_VUN, RZ23_DTYPE, NULL, "RZ23",
      &rz_set_type, NULL, NULL, "Set RZ23 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ23L_DTYPE, NULL, "RZ23L",
      &rz_set_type, NULL, NULL, "Set RZ23L Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ24_DTYPE, NULL, "RZ24",
      &rz_set_type, NULL, NULL, "Set RZ24 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ24L_DTYPE, NULL, "RZ24L",
      &rz_set_type, NULL, NULL, "Set RZ24L Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ25_DTYPE, NULL, "RZ25",
      &rz_set_type, NULL, NULL, "Set RZ25 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ25L_DTYPE, NULL, "RZ25L",
      &rz_set_type, NULL, NULL, "Set RZ25L Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ26_DTYPE, NULL, "RZ26",
      &rz_set_type, NULL, NULL, "Set RZ26 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ26L_DTYPE, NULL, "RZ26L",
      &rz_set_type, NULL, NULL, "Set RZ26L Disk Type" },
    { MTAB_XTD|MTAB_VUN, RZ55_DTYPE, NULL, "RZ55",
      &rz_set_type, NULL, NULL, "Set RZ55 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRD40_DTYPE, NULL, "CDROM",
      &rz_set_type, NULL, NULL, "Set RRD40 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRD40_DTYPE, NULL, "RRD40",
      &rz_set_type, NULL, NULL, "Set RRD40 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRD42_DTYPE, NULL, "RRD42",
      &rz_set_type, NULL, NULL, "Set RRD42 Disk Type" },
    { MTAB_XTD|MTAB_VUN, RRW11_DTYPE, NULL, "RRW11",
      &rz_set_type, NULL, NULL, "Set RRW11 Disk Type" },
    { MTAB_XTD|MTAB_VUN, CDW900_DTYPE, NULL, "CDW900",
      &rz_set_type, NULL, NULL, "Set SONY CDW-900E Disk Type" },
    { MTAB_XTD|MTAB_VUN, XR1001_DTYPE, NULL, "XR1001",
      &rz_set_type, NULL, NULL, "Set JVC XR-W1001 Disk Type" },
    { MTAB_XTD|MTAB_VUN, TZK50_DTYPE, NULL, "TZK50",
      &rz_set_type, NULL, NULL, "Set DEC TZK50 Tape Type" },
    { MTAB_XTD|MTAB_VUN, TZ30_DTYPE, NULL, "TZ30",
      &rz_set_type, NULL, NULL, "Set DEC TZ30 Tape Type" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, RZU_DTYPE, NULL, "RZUSER",
      &rz_set_type, NULL, NULL, "Set RZUSER=size Disk Type" },
    { MTAB_XTD|MTAB_VUN, 0, "TYPE", NULL,
      NULL, &rz_show_type, NULL, "Display device type" },
    { SCSI_NOAUTO, SCSI_NOAUTO, "noautosize", "NOAUTOSIZE", NULL, NULL, NULL, "Disables disk autosize on attach" },
    { SCSI_NOAUTO,           0, "autosize",   "AUTOSIZE",   NULL, NULL, NULL, "Enables disk autosize on attach" },
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &scsi_set_fmt, &scsi_show_fmt, NULL, "Set/Display unit format" },
    { 0 }
    };

static const char *drv_types[] = {
    "RZ23",
    "RZ23L",
    "RZ24",
    "RZ24L",
    "RZ25",
    "RZ25L",
    "RZ26",
    "RZ26L",
    "RZ55",
    "CDROM",
    "RRD40",
    "RRD42",
    "RRW11",
    "CDW900",
    "XR1001",
    "TZK50",
    "TZ30",
    "RZUSER"
    };

DEVICE rz_dev = {
    "RZ", rz_unit, rz_reg, rz_mod,
    RZ_NUMDR + 1, DEV_RDX, 31, 1, DEV_RDX, 8,
    NULL, NULL, &rz_reset,
    NULL, &rz_attach, &scsi_detach,
    NULL, DEV_DEBUG | DEV_DISK | DEV_SECTORS | RZ_FLAGS,
    0, rz_debug, NULL, NULL, &rz_help, NULL, NULL,
    &rz_description
    };

/* RZB data structures

   rzb_dev      RZB device descriptor
   rzb_unit     RZB unit descriptor
   rzb_reg      RZB register list
*/

CTLR rzb_ctx = { 1 };

DIB rzb_dib = {
    RZ_ROM_INDEX, BOOT_CODE_ARRAY, BOOT_CODE_SIZE
    };

UNIT rzb_unit[] = {
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE+
            (RZ23_DTYPE << UNIT_V_DTYPE), RZ_SIZE (RZ23)) },
    { UDATA (&rz_isvc, UNIT_DIS, 0) }
    };

REG rzb_reg[] = {
    { FLDATAD ( INT,    int_req[IPL_SCB], INT_V_SCB, "interrupt pending flag") },
    { NULL }
    };

DEVICE rzb_dev = {
    "RZB", rzb_unit, rzb_reg, rz_mod,
    RZ_NUMDR + 1, DEV_RDX, 31, 1, DEV_RDX, 8,
    NULL, NULL, &rz_reset,
    NULL, &rz_attach, &scsi_detach,
    &rzb_dib, DEV_DEBUG | DEV_DISK | DEV_SECTORS | RZB_FLAGS,
    0, rz_debug, NULL, NULL,
    &rz_help, NULL                                      /* help and attach_help routines */
    };

static DEVICE *rz_devmap[RZ_NUMCT] = {
    &rz_dev, &rzb_dev
    };

static CTLR *rz_ctxmap[RZ_NUMCT] = {
    &rz_ctx, &rzb_ctx
    };

/* RZ routines

   rz_rd       I/O page read
   rz_wr       I/O page write
   rz_svc      process event
   rz_reset    process reset
*/

int32 rz_rd (int32 pa)
{
int32 ctlr = (pa >> 8) & 1;
CTLR *rz = rz_ctxmap[ctlr];
DEVICE *dptr = rz_devmap[ctlr];
int32 rg = (pa >> 2) & 0x1F;
int32 data = 0;
int32 len;

if (dptr->flags & DEV_DIS)                              /* disabled? */
    return 0;

switch (rg) {

    case 0:                                             /* SCS_CUR_DATA */
        if ((rz->icmd & ICMD_ENOUT) || (rz->icmd & ICMD_AIP)) /* initiator controlling bus */
            data = rz->odata;
        else if (rz->bus.target >= 0) {
            len = scsi_read (&rz->bus, &rz->cdata, 0);  /* receive current byte */
            data = rz->cdata;
            }
        else {                                          /* bus idle */
            data = 0;
            if (rz->mode & MODE_PARCK) {                /* parity checking enabled? */
                if (rz->mode & MODE_INTPAR) {           /* interrupt too? */
                    sim_debug (DBG_INT, dptr, "Delayed: Parity Error\n");
                    rz_setint (rz, STS_PARERR);
                    }
                else
                    rz->status = rz->status | STS_PARERR; /* signal parity error */
                }
            }
        break;

    case 1:                                             /* SCS_INI_CMD */
        data = rz->icmd;
        break;

    case 2:                                             /* SCS_MODE */
        data = rz->mode;
        break;

    case 3:                                             /* SCS_TAR_CMD */
        data = rz->tcmd;
        break;

    case 4:                                             /* SCS_CUR_STAT */
        if (rz->icmd & ICMD_RST)
            data = CSTAT_RST;
        else {
            if (rz->mode & MODE_TARG)                   /* target mode? */
                data = ((rz->tcmd & 0xF) << CSTAT_V_PHASE);
            else {                                      /* initiator mode? */
                data = (rz->bus.phase << CSTAT_V_PHASE);
                if (rz->icmd & ICMD_SEL)
                    data = data | CSTAT_SEL;
                if (rz->bus.target >= 0)                /* target selected? */
                    data = data | CSTAT_BSY;
                if ((rz->bus.req) && ((rz->icmd & ICMD_ACK) == 0))
                    data = data | CSTAT_REQ;
                }
            if (rz->icmd & ICMD_ENOUT)
                data = data | (rz_parity (rz->odata, 1));
            else if (rz->bus.target >= 0) {
                len = scsi_read (&rz->bus, &rz->cdata, 0);  /* receive current byte */
                data = data | (rz_parity (rz->cdata, 1));
                }
            }
        break;

    case 5:                                             /* SCS_STATUS */
        rz->status = rz->status & ~(STS_ACK | STS_ATN | STS_MATCH);
        if ((rz->icmd & ICMD_RST) == 0) {
            if ((rz->mode & MODE_TARG) == 0) {          /* initiator mode */
                if (rz->icmd & ICMD_ACK)
                    rz->status = rz->status | STS_ACK;
                if (rz->bus.atn)
                    rz->status = rz->status | STS_ATN;
                if ((rz->tcmd & TCMD_PHASE) == rz->bus.phase) /* bus phase match? */
                    rz->status = rz->status | STS_MATCH;
                }
            }
        data = rz->status;
        break;

    case 6:                                             /* SCS_IN_DATA */
        if (rz->bus.target >= 0) {                      /* target selected? */
            len = scsi_read (&rz->bus, &rz->cdata, 0);  /* receive current byte */
            data = rz->cdata;
            }
        else data = 0;
        break;

    case 7:                                             /* SCS_RESET */
        rz->status = rz->status & ~(STS_INTREQ | STS_PARERR | STS_BSYERR);
        rz_clrint (rz);
        data = 0;
        break;

    case 8:                                             /* SCD_ADR */
        data = rz->daddr & DCNT_MASK;
        break;

    case 16:                                            /* SCD_CNT */
        data = rz->dcount & DCNT_MASK;
        break;

    case 17:                                            /* SCD_DIR */
        data = rz->ddir;
        break;

    default:
        data = 0;
        break;
        }

sim_debug (DBG_REG, dptr, "reg %d read, value = %X, PC = %08X\n", rg, data, fault_PC);
return data;
}

void rz_wr (int32 pa, int32 data, int32 access)
{
int32 ctlr = (pa >> 8) & 1;
CTLR *rz = rz_ctxmap[ctlr];
DEVICE *dptr = rz_devmap[ctlr];
UNIT *uptr = dptr->units + RZ_CTLR;
int32 rg = (pa >> 2) & 0x1F;
int32 i;

if (dptr->flags & DEV_DIS)                              /* disabled? */
    return;

switch (rg) {

    case 0:                                             /* SCS_OUT_DATA */
        rz->odata = data;
        break;

    case 1:                                             /* SCS_INI_CMD */
        if ((rz->mode & MODE_TARG) == 0) {              /* initiator mode */
            if ((data ^ rz->icmd) & ICMD_ATN) {
                if (data & ICMD_ATN)                    /* setting ATN */
                    scsi_set_atn (&rz->bus);
                else                                    /* clearing ATN */
                    scsi_release_atn (&rz->bus);
                }
            if ((data ^ rz->icmd) & ICMD_ACK) {
                if (data & ICMD_ACK) {                  /* setting ACK */
                    if (rz->bus.target >= 0)
                        rz_ack (rz);
                    }
                }
            }
        if (data & ICMD_ENOUT) {
            if ((data & ICMD_SEL) && (rz->bus.target < 0)) { /* setting SEL */
                if ((rz->odata == rz->selen) && (rz->selen != 0)) {
                    rz_setint (rz, 0);              /* selecting ourselves */
                    break;
                    }
                for (i = 0; i < RZ_NUMDR; i++) {
                    if ((rz->odata & (1u << i)) && (i != RZ_SCSI_ID)) {
                        scsi_select (&rz->bus, i);
                        break;
                        }
                    }
                }
            }
#if 0
        if ((data ^ rz->icmd) & ICMD_SEL) {
            if ((data & ICMD_SEL) == 0) {               /* clearing SEL */
                if (rz->bus.target < 0)
                    scsi_release (&rz->bus);
                }
            }
#endif
        if ((data ^ rz->icmd) & ICMD_RST) {
            if (data & ICMD_RST) {                      /* setting RST */
                rz_sw_reset (rz);
                rz->icmd = ICMD_RST;
                rz->status = STS_INTREQ;
                sim_debug (DBG_INT, dptr, "Delayed: Bus reset asserted\n");
                rz_setint (rz, 0);
                }
            else {                                      /* clearing RST */
                rz->icmd = data;
                sim_debug (DBG_INT, dptr, "Delayed: Bus reset cleared\n");
                rz_setint (rz, 0);
                }
            }
        else rz->icmd = data;
        break;

    case 2:                                             /* SCS_MODE */
        if (data & MODE_ARB) {                          /* start arbitration */
            rz->status = rz->status & ~(STS_INTREQ | STS_PARERR | STS_BSYERR);
            rz_clrint (rz);
            if (scsi_arbitrate (&rz->bus, RZ_SCSI_ID)) {
                rz->icmd = rz->icmd | ICMD_AIP;
                rz->icmd = rz->icmd & ~ICMD_LA;
                }
            else {
                rz->icmd = rz->icmd & ~ICMD_AIP;
                rz->icmd = rz->icmd | ICMD_LA;
                }
            }
        if (data & MODE_MONBSY) {                       /* enable BSY monitor */
            if (rz->bus.target < 0) {                   /* BSY set? */
                sim_debug (DBG_INT, dptr, "Delayed: Busy error\n");
                rz_setint (rz, STS_BSYERR);             /* no, error */
                }
            }
        if ((data & MODE_DMA) == 0)                     /* clearing DMA */
            rz->status = rz->status & ~STS_DMAEND;
        rz->mode = data;
        if (((rz->icmd & ICMD_BSY) == 0) && (rz->bus.target < 0))
            rz->mode = rz->mode & ~MODE_DMA;            /* DMA can only be set when BSY is set */
        rz_update_status (rz);
        break;

    case 3:                                             /* SCS_TAR_CMD */
        rz->tcmd = data & 0xF;
        if ((rz->mode & MODE_TARG) == 0)                /* not target mode */
            rz_update_status (rz);
        break;

    case 4:                                             /* SCS_SEL_ENA */
        rz->selen = data;
        break;

    case 5:                                             /* SCS_DMA_SEND */
        uptr = dptr->units + rz->bus.target;
        sim_activate (uptr, 50);
        break;

    case 6:                                             /* SCS_DMA_TRCV */
        break;

    case 7:                                             /* SCS_DMA_IRCV */
        uptr = dptr->units + rz->bus.target;
        sim_activate (uptr, 50);
        break;

    case 8:                                             /* SCD_ADR */
        if (access == L_BYTE) {
            if (rz->daddr_low) {
                rz->daddr = rz->daddr | (data & BMASK);
                rz->daddr_low = FALSE;
                }
            else {
                rz->daddr = ((data & 0x3F) << 8);
                rz->daddr_low = TRUE;
                }
            }
        else rz->daddr = data & DCNT_MASK;
        break;

    case 16:                                            /* SCD_CNT */
        rz->dcount = data & DCNT_MASK;
        break;

    case 17:                                            /* SCD_DIR */
        rz->ddir = data;
        break;
        }

sim_debug (DBG_REG, dptr, "reg %d write, value = %X, PC = %08X\n", rg, data, fault_PC);
SET_IRQL;
}

int32 rz_parity (int32 val, int32 odd)
{
for ( ; val != 0; val = val >> 1) {
    if (val & 1)
        odd = odd ^ 1;
    }
return odd;
}

void rz_ack (CTLR *rz)
{
uint32 len;
uint32 old_phase;

old_phase = rz->bus.phase;
switch (rz->bus.phase) {

    case PH_MSG_OUT:
    case PH_COMMAND:
    case PH_DATA_OUT:
        if (rz->bus.phase == PH_DATA_OUT)
            rz->buf_ptr = 0;                            /* TODO: fix this in sim_scsi.c */
        rz->buf[rz->buf_ptr++] = rz->odata;
        scsi_write (&rz->bus, &rz->buf[0], rz->buf_ptr);  /* send next byte */
        break;

    case PH_DATA_IN:
    case PH_STATUS:
    case PH_MSG_IN:
        len = scsi_read (&rz->bus, &rz->cdata, 1);      /* receive next byte */
        break;
        }
if (old_phase != rz->bus.phase)                         /* new phase? */
    rz->buf_ptr = 0;                                    /* reset buffer */
if (old_phase == PH_MSG_IN)                             /* message in just processed? */
    scsi_release (&rz->bus);                            /* accept message */
rz_update_status (rz);
}

void rz_update_status (CTLR *rz)
{
DEVICE *dptr = rz_devmap[rz->cnum];

if ((rz->tcmd & TCMD_PHASE) == rz->bus.phase)
    rz->status = rz->status | STS_MATCH;
else {
    rz->status = rz->status & ~STS_MATCH;
    if ((rz->mode & MODE_DMA) && (rz->bus.req)) {
        sim_debug (DBG_INT, dptr, "Immediate: Phase mismatch\n");
        if (rz->cnum == 0)
            SET_INT (SCB);
        else
            SET_INT (SCA);
        rz->status = rz->status | STS_INTREQ;
        }
    }
if ((rz->mode & MODE_MONBSY) && (rz->bus.target < 0)) {  /* monitoring BSY? */
    sim_debug (DBG_INT, dptr, "Delayed: Busy error\n");
    rz_setint (rz, STS_BSYERR);
    }
}

t_stat rz_svc (UNIT *uptr)
{
CTLR *rz = rz_ctxmap[uptr->cnum];
DEVICE *dptr = rz_devmap[uptr->cnum];
int32 dma_len;
uint32 old_phase;

old_phase = rz->bus.phase;
if (rz->dcount == 0)
    dma_len = DMA_SIZE;                                 /* full buffer */
else
    dma_len = ((rz->dcount ^ DCNT_MASK) + 1) & DCNT_MASK; /* 2's complement */
if (rz->ddir == 1) {                                    /* DMA in */
    dma_len = scsi_read (&rz->bus, &rz->buf[0], dma_len);
    ddb_WriteB (rz->daddr, dma_len, &rz->buf[0]);
    }
else {                                                  /* DMA out */
    ddb_ReadB (rz->daddr, dma_len, &rz->buf[0]);
    dma_len = scsi_write (&rz->bus, &rz->buf[0], dma_len);
    }
rz->buf_len = 0;
rz->dcount = (rz->dcount + dma_len) & DCNT_MASK;        /* increment toward zero */
dma_len = ((rz->dcount ^ DCNT_MASK) + 1) & DCNT_MASK;   /* 2's complement */
if (rz->ddir == 1) {                                    /* DMA in */
    if (old_phase == PH_MSG_IN)                         /* message in just processed? */
        scsi_release (&rz->bus);                        /* accept message */
    }
else {
    if ((rz->bus.phase == SCSI_STS) && (dma_len == 2)) { /* VMS driver expects this */
        rz->dcount = (rz->dcount + 1) & DCNT_MASK;      /* increment toward zero */
        dma_len--;                                      /* decrement remaining xfer */
        }
    }
if (dma_len == 0) {
    sim_debug (DBG_INT, dptr, "Service: DMA done\n");
    if (rz->cnum == 0)
        SET_INT (SCB);
    else
        SET_INT (SCA);
    rz->status = rz->status | STS_INTREQ | STS_DMAEND;
    }
rz_update_status (rz);
return SCPE_OK;
}

t_stat rz_isvc (UNIT *uptr)
{
CTLR *rz = rz_ctxmap[uptr->cnum];
DEVICE *dptr = rz_devmap[uptr->cnum];

sim_debug (DBG_INT, dptr, "Service: flags = %X\n", uptr->iflgs);
if (rz->cnum == 0)
    SET_INT (SCB);
else
    SET_INT (SCA);
rz->status = rz->status | STS_INTREQ | uptr->iflgs;
uptr->iflgs = 0;
return SCPE_OK;
}

void rz_setint (CTLR *rz, uint32 flags)
{
DEVICE *dptr = rz_devmap[rz->cnum];
UNIT *uptr = dptr->units + RZ_CTLR;

uptr->iflgs |= flags;
if (!sim_is_active (uptr))
    sim_activate (uptr, 50);
}

void rz_clrint (CTLR *rz)
{
DEVICE *dptr = rz_devmap[rz->cnum];

sim_debug (DBG_INT, dptr, "Immediate: Clear int\n");
if (rz->cnum == 0)
    CLR_INT (SCB);
else
    CLR_INT (SCA);
rz->status = rz->status & ~STS_INTREQ;
}

void rz_sw_reset (CTLR *rz)
{
DEVICE *dptr;
UNIT *uptr;
uint32 i;

dptr = rz_devmap[rz->cnum];
for (i = 0; i < (RZ_NUMDR + 1); i++) {
    uptr = dptr->units + i;
    sim_cancel (uptr);
    uptr->iflgs = 0;
    }
rz_clrint (rz);
rz->cdata = 0;
rz->mode = 0;
rz->icmd = 0;
rz->tcmd = 0;
rz->status = 0;
rz->cstat = 0;
rz->selen = 0;
rz->dcount = 0;
rz->daddr = 0;
rz->daddr_low = FALSE;
rz->ddir = 0;
rz->buf_ptr = 0;
scsi_reset (&rz->bus);
}

t_stat rz_reset (DEVICE *dptr)
{
int32 ctlr, i;
uint32 dtyp;
CTLR *rz;
UNIT *uptr;
t_stat r;

for (i = 0, ctlr = -1; i < RZ_NUMCT; i++) {             /* find ctrl num */
    if (rz_devmap[i] == dptr)
        ctlr = i;
    }
if (ctlr < 0)                                           /* not found??? */
    return SCPE_IERR;
rz = rz_ctxmap[ctlr];
if (rz->buf == NULL)
    rz->buf = (uint8 *)calloc (DMA_SIZE, sizeof(uint8));
if (rz->buf == NULL)
    return SCPE_MEM;
r = scsi_init (&rz->bus, DMA_SIZE);                     /* init SCSI bus */
if (r != SCPE_OK)
    return r;
rz->bus.dptr = dptr;                                    /* set bus device */
for (i = 0; i < (RZ_NUMDR + 1); i++) {                  /* init units */
    uptr = dptr->units + i;
    uptr->cnum = ctlr;                                  /* set ctrl index */
    if (i == RZ_SCSI_ID)                                /* initiator ID? */
        uptr->flags = UNIT_DIS;                         /* disable unit */
    if (i < RZ_NUMDR) {
        scsi_add_unit (&rz->bus, i, uptr);
        dtyp = GET_DTYPE (uptr->flags);
        scsi_set_unit (&rz->bus, uptr, &rzdev_tab[dtyp]);
        scsi_reset_unit (uptr);
        }
    }
rz_sw_reset (rz);
return SCPE_OK;
}

/* Set unit type (and capacity if user defined) */

t_stat rz_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
CTLR *rz = rz_ctxmap[uptr->cnum];
uint32 cap;
uint32 max = sim_toffset_64? RZU_EMAXC: RZU_MAXC;
t_stat r;

if ((val < 0) || ((val != RZU_DTYPE) && cptr))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (cptr) {
    cap = (uint32) get_uint (cptr, 10, 0xFFFFFFFF, &r);
    if ((sim_switches & SWMASK ('L')) == 0)
        cap = cap * 1954;
    if ((r != SCPE_OK) || (cap < RZU_MINC) || (cap > max))
        return SCPE_ARG;
    rzdev_tab[val].lbn = cap;
    }
uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (val << UNIT_V_DTYPE);
uptr->capac = (t_addr)rzdev_tab[val].lbn;
scsi_set_unit (&rz->bus, uptr, &rzdev_tab[val]);
scsi_reset_unit (uptr);
return SCPE_OK;
}

/* Show unit type */

t_stat rz_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s", rzdev_tab[GET_DTYPE (uptr->flags)].name);
return SCPE_OK;
}

t_stat rz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "NCR 5380 SCSI Controller (%s)\n\n", dptr->name);
fprintf (st, "The %s controller simulates the NCR 5380 SCSI controller connected\n", dptr->name);
fprintf (st, "to a bus with up to 7 target devices.\n");
if (dptr->flags & DEV_DISABLE)
    fprintf (st, "Initially the %s controller is disabled.\n", dptr->name);
else
    fprintf (st, "The %s controller cannot be disabled.\n", dptr->name);
fprintf (st, "SCSI target device %s%d is reserved for the initiator and cannot\n", dptr->name, RZ_SCSI_ID);
fprintf (st, "be enabled\n");
fprintf (st, "Each target on the SCSI bus can be set to one of several types:\n");
fprint_set_help (st, dptr);
fprintf (st, "Configured options can be displayed with:\n\n");
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
scsi_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

t_stat rz_attach (UNIT *uptr, CONST char *cptr)
{
return scsi_attach_ex (uptr, cptr, drv_types);
}

const char *rz_description (DEVICE *dptr)
{
return "NCR 5380 SCSI controller";
}
