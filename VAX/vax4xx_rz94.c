/* vax4xx_rz94.c: NCR 53C94 SCSI controller

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

/* command groups */

#define CMD_DISC        0x40                            /* disconnected state group */
#define CMD_TARG        0x20                            /* target state group */
#define CMD_INIT        0x10                            /* initiator state group */

/* status register */

#define STS_INT         0x80                            /* interrupt */
#define STS_GE          0x40                            /* gross error */
#define STS_PE          0x20                            /* parity error */
#define STS_TC          0x10                            /* terminal count */
#define STS_VGC         0x08                            /* valid group code */
#define STS_PH          0x07                            /* SCSI phase */
#define STS_CLR         0x10

/* interrupt register */

#define INT_SCSIRST     0x80                            /* SCSI reset */
#define INT_ILLCMD      0x40                            /* illegal command */
#define INT_DIS         0x20                            /* disconnect */
#define INT_BUSSV       0x10                            /* bus service */
#define INT_FC          0x08                            /* function complete */
#define INT_RSEL        0x04                            /* reselected */
#define INT_SELA        0x02                            /* selected with ATN */
#define INT_SEL         0x01                            /* selected */

/* configuration register 1 */

#define CFG1_SLOW       0x80                            /* slow cable mode */
#define CFG1_SRD        0x40                            /* disable SCSI reset int */
#define CFG1_PTST       0x20                            /* parity test */
#define CFG1_PEN        0x10                            /* parity enable */
#define CFG1_TEST       0x08                            /* chip test */
#define CFG1_MYID       0x07                            /* my bus id */

#define UNIT_V_DTYPE    (SCSI_V_UF + 0)                 /* drive type */
#define UNIT_M_DTYPE    0x1F
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

#define RZ_MAXFR        (1u << 16)                      /* max transfer */

uint32 rz_last_cmd = 0;
uint32 rz_txi = 0;                                      /* transfer count */
uint32 rz_txc = 0;                                      /* transfer counter */
uint8 rz_cfg1 = 0;                                      /* config 1 */
uint8 rz_cfg2 = 0;                                      /* config 2 */
uint8 rz_cfg3 = 0;                                      /* config 3 */
uint8 rz_int = 0;                                       /* interrupt */
uint8 rz_stat = 0;                                      /* status */
uint32 rz_seq = 0;
uint32 rz_dest = 1;
uint8 rz_fifo[16] = { 0 };
uint32 rz_fifo_t = 0;
uint32 rz_fifo_b = 0;
uint32 rz_fifo_c = 0;
uint32 rz_dma = 0;
uint32 rz_dir = 0;
uint8 *rz_buf;
SCSI_BUS rz_bus;

/* debugging bitmaps */

#define DBG_REG         0x0001                          /* trace read/write registers */
#define DBG_CMD         0x0002                          /* display commands */
#define DBG_INT         0x0004                          /* display transfer requests */

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

t_stat rz_svc (UNIT *uptr);
t_stat rz_reset (DEVICE *dptr);
void rz_sw_reset (void);
t_stat rz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
void rz_cmd (uint32 cmd);
t_stat rz_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rz_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *rz_description (DEVICE *dptr);


/* RZ data structures

   rz_dev       RZ device descriptor
   rz_unit      RZ unit list
   rz_reg       RZ register list
*/

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
    { UDATA (&rz_svc, UNIT_DIS, 0) }
    };

REG rz_reg[] = {
    { FLDATAD (INT, int_req[IPL_SC], INT_V_SC, "interrupt pending flag") },
    { NULL }
    };

MTAB rz_mod[] = {
    { SCSI_WLK,               0,  NULL, "WRITEENABLED", 
      &scsi_set_wlk, NULL, NULL, "Write enable disk drive" },
    { SCSI_WLK,        SCSI_WLK,  NULL, "LOCKED", 
      &scsi_set_wlk, NULL, NULL, "Write lock disk drive"  },
    { MTAB_XTD|MTAB_VUN, 0, "WRITE", NULL,
      NULL, &scsi_show_wlk, NULL,  "Display drive writelock status" },
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

DEVICE rz_dev = {
    "RZ", rz_unit, rz_reg, rz_mod,
    9, DEV_RDX, 8, 1, DEV_RDX, 8,
    NULL, NULL, &rz_reset,
    NULL, &scsi_attach, &scsi_detach,
    NULL, DEV_DISABLE | DEV_DEBUG | DEV_DISK | DEV_SECTORS,
    0, rz_debug, NULL, NULL, &rz_help, NULL, NULL,
    &rz_description
    };

/* Register names for Debug tracing */
static const char *rz_rd_regs[] =
    {"TX L", "TX H", "FIFO", "CMD ",
     "STAT", "INT ", "SEQ ", "FFLG",
     "CFG1", "RSVD", "RSVD", "CFG2",
     "CFG3", "RSVD", "RSVD", "RSVD" };
static const char *rz_wr_regs[] = 
    {"TX L", "TX H", "FIFO", "CMD ",
     "DST ", "TMO ", "SYNP", "SYNO",
     "CFG1", "CLK ", "TEST", "CFG2",
     "CFG3", "RSVD", "RSVD", "FFOB" };

uint8 rz_fifo_rd (void)
{
if (rz_fifo_c) {
    rz_fifo_t &= 0xF;
    rz_fifo_c--;
    return rz_fifo[rz_fifo_t++];
    }
else
    return rz_fifo[rz_fifo_b];
}

void rz_fifo_wr (uint8 data)
{
if (rz_fifo_c < 16) {
    rz_fifo[rz_fifo_b++] = data;
    rz_fifo_b &= 0xF;
    rz_fifo_c++;
    }
else {
    rz_fifo[rz_fifo_b] = data;
    rz_stat |= STS_GE;                                  /* gross error */
    }
}

void rz_fifo_reset (void)
{
rz_fifo_c = 0;
rz_fifo_t = rz_fifo_b = 0;
rz_fifo[rz_fifo_b] = 0;
}

/* IO dispatch routines, I/O addresses 177601x0 - 177601x7 */

int32 rz_rd (int32 pa)
{
int32 data = 0;

if (pa == 0x200C0000) {
    return rz_dma;
    }
if (pa == 0x200C000C) {
    return rz_dir;
    }

switch ((pa >> 2) & 0xF) {                              /* case on PA<2:1> */

    case 0:                                             /* transfer counter LSB */
        data = rz_txc & 0xFF;
        break;

    case 1:                                             /* transfer counter MSB */
        data = (rz_txc >> 8) & 0xFF;
        break;

    case 2:                                             /* FIFO */
        data = rz_fifo_rd ();
        break;

    case 3:                                             /* command */
        data = rz_last_cmd;
        break;

    case 4:                                             /* status */
        data = rz_stat | rz_bus.phase;
        break;

    case 5:                                             /* interrupt */
        data = rz_int;
        if (rz_stat & STS_INT) {
            rz_stat &= STS_CLR;
            rz_int = 0;
            }
        break;

    case 6:                                             /* sequence step */
        data = rz_seq;
        break;

    case 7:                                             /* FIFO flags/seq step */
        data = (rz_seq << 5) | rz_fifo_c;
        break;

    case 8:                                             /* config 1 */
        data = rz_cfg1;
        break;

    case 11:                                            /* config 2 */
        data = rz_cfg2;
        break;

    case 12:                                            /* config 3 */
        data = rz_cfg3;
        break;

    default:                                            /* NCR reserved */
        data = 0;
        break;
        }

sim_debug (DBG_REG, &rz_dev, "rz_rd(PA=0x%08X [%s], data=0x%X) at %08X\n", pa, rz_rd_regs[(pa >> 2) & 0xF], data, fault_PC);

SET_IRQL;
return data;
}

void rz_wr (int32 pa, int32 data, int32 access)
{
sim_debug (DBG_REG, &rz_dev, "rz_wr(PA=0x%08X [%s], access=%d, data=0x%X) at %08X\n", pa, rz_wr_regs[(pa >> 2) & 0xF], access, data, fault_PC);

if (pa == 0x200C0000) {
    rz_dma = data;
    return;
    }
if (pa == 0x200C000C) {
    rz_dir = data;
    return;
    }

switch ((pa >> 2) & 0xF) {                              /* case on PA<2:1> */

    case 0:                                             /* transfer count LSB */
        rz_txi = (rz_txi & ~0xFF) | (data & 0xFF);
        break;

    case 1:                                             /* transfer count MSB */
        rz_txi = (rz_txi & ~0xFF00) | ((data << 8) & 0xFF00);
        break;

    case 2:                                             /* FIFO */
        rz_fifo_wr (data);
        break;

    case 3:                                             /* command */
        rz_cmd (data);
        break;

    case 4:                                             /* destination bus ID */
        rz_dest = data & 0x7;
        break;

    case 5:                                             /* select timeout - NI */
        break;

    case 6:                                             /* sync period - NI*/
        break;

    case 7:                                             /* sync offset - NI? */
        break;

    case 8:                                             /* config 1 */
        rz_cfg1 = data;
        break;

    case 9:                                             /* clock conversion - NI */
        break;

    case 10:                                            /* test - NI? */
        break;

    case 11:                                            /* config 2 */
        rz_cfg2 = data;
        break;

    case 12:                                            /* config 3 */
        rz_cfg3 = data;
        break;

    case 15:                                            /* FIFO bottom */
        break;

    default:                                            /* NCR reserved */
        break;
        }

SET_IRQL;
}

/* Unit service routine */

t_stat rz_svc (UNIT *uptr)
{
rz_stat |= STS_INT;
SET_INT (SC);
return SCPE_OK;
}

void rz_setint (uint32 flag)
{
rz_int |= flag;
sim_activate (&rz_unit[8], 50);
}

void rz_cmd (uint32 cmd)
{
uint32 ini = (rz_cfg1 & CFG1_MYID);
uint32 tgt = rz_dest;
uint32 state = scsi_state (&rz_bus, ini);
uint32 txc, i, old_phase;

if ((cmd & CMD_DISC) && (state != SCSI_DISC)) {         /* check cmd validity */
    sim_debug (DBG_CMD, &rz_dev, "disconnected cmd when not disconnected\n");
    rz_setint (INT_ILLCMD);
    return;
    }
if ((cmd & CMD_TARG) && (state != SCSI_TARG)) {
    sim_debug (DBG_CMD, &rz_dev, "target cmd when not target\n");
    rz_setint (INT_ILLCMD);
    return;
    }
if ((cmd & CMD_INIT) && (state != SCSI_INIT)) {
    sim_debug (DBG_CMD, &rz_dev, "initiator cmd when not initiator\n");
    rz_setint (INT_ILLCMD);
    return;
    }

switch (cmd & 0x7f) {

    case 0:                                         /* NOP */
        sim_debug (DBG_CMD, &rz_dev, "NOP\n");
        if (cmd & 0x80) {                           /* DMA */
            rz_stat &= ~STS_TC;
            rz_txc = (rz_txi == 0) ? RZ_MAXFR : rz_txi;
            }
        break;

    case 1:                                         /* flush FIFO */
        sim_debug (DBG_CMD, &rz_dev, "flush fifo\n");
        rz_fifo_reset ();
        rz_int |= INT_FC;
        break;

    case 2:                                         /* reset chip */
        sim_debug (DBG_CMD, &rz_dev, "sw reset\n");
        rz_sw_reset ();
        break;

    case 3:                                         /* reset SCSI */
        sim_debug (DBG_CMD, &rz_dev, "SCSI reset\n");
        scsi_reset (&rz_bus);
        if ((rz_cfg1 & CFG1_SRD) == 0) {
            rz_setint (INT_SCSIRST);
            }
        break;

    case 0x20:                                      /* send message */
        sim_debug (DBG_CMD, &rz_dev, "send message\n");
        break;

    case 0x21:                                      /* send status */
        sim_debug (DBG_CMD, &rz_dev, "send status\n");
        break;

    case 0x22:                                      /* send data */
        sim_debug (DBG_CMD, &rz_dev, "send data\n");
        break;

    case 0x23:                                      /* disconnect sequence */
        sim_debug (DBG_CMD, &rz_dev, "disconnect sequence\n");
        break;

    case 0x24:                                      /* terminate sequence */
        sim_debug (DBG_CMD, &rz_dev, "terminate sequence\n");
        break;

    case 0x25:                                      /* target cmd complete sequence */
        sim_debug (DBG_CMD, &rz_dev, "target cmd complete sequence\n");
        break;

    case 0x27:                                      /* disconnect */
        sim_debug (DBG_CMD, &rz_dev, "disconnect\n");
        break;

    case 0x28:                                      /* rcv message seq */
        sim_debug (DBG_CMD, &rz_dev, "rcv message seq\n");
        break;

    case 0x29:                                      /* rcv cmd */
        sim_debug (DBG_CMD, &rz_dev, "rcv cmd\n");
        break;

    case 0x30:                                      /* rcv data */
        sim_debug (DBG_CMD, &rz_dev, "rcv data\n");
        break;

    case 0x31:                                      /* rcv cmd seq */
        sim_debug (DBG_CMD, &rz_dev, "rcv cmd seq\n");
        break;

    case 0x40:                                      /* reselect */
        sim_debug (DBG_CMD, &rz_dev, "reselect\n");
        break;

    case 0x41:                                          /* select without ATN */
        sim_debug (DBG_CMD, &rz_dev, "select without atn\n");
        rz_seq = 0;
        if (!scsi_arbitrate (&rz_bus, ini)) {
            rz_seq = 0;
            rz_int |= INT_DIS;                          /* disconnect */
            sim_activate (&rz_unit[8], 100);
            break;
            }
        if (!scsi_select (&rz_bus, tgt)) {
            rz_seq = 0;
            rz_int |= INT_DIS;                          /* disconnect */
            scsi_release (&rz_bus);
            sim_activate (&rz_unit[8], 100);
            break;
            }
        rz_seq = 2;
        for (i = 0; rz_fifo_c > 0; i++)
            rz_buf[i] = rz_fifo_rd ();
        scsi_write (&rz_bus, &rz_buf[0], i);
        if (scsi_state (&rz_bus, tgt) == SCSI_DISC) {
            rz_seq = 3;
            rz_int |= INT_DIS;
            }
        else {
            rz_seq = 4;
            rz_int |= (INT_BUSSV | INT_FC);
            }
        sim_activate (&rz_unit[8], 50);
        break;

    case 0x42:                                          /* select with ATN */
        sim_debug (DBG_CMD, &rz_dev, "select with atn\n");
        rz_seq = 0;
        if (!scsi_arbitrate (&rz_bus, ini)) {
            rz_int |= INT_DIS;                          /* disconnect */
            sim_activate (&rz_unit[8], 100);
            break;
            }
        scsi_set_atn (&rz_bus);
        if (!scsi_select (&rz_bus, tgt)) {
            rz_seq = 0;
            rz_int |= INT_DIS;                          /* disconnect */
            scsi_release (&rz_bus);
            sim_activate (&rz_unit[8], 100);
            break;
            }
        for (i = 0; rz_fifo_c > 0; i++)
            rz_buf[i] = rz_fifo_rd ();
        scsi_write (&rz_bus, &rz_buf[0], i);
        rz_seq = 2;
        if (scsi_state (&rz_bus, tgt) == SCSI_DISC) {
            rz_seq = 3;
            rz_int |= INT_DIS;
            }
        else {
            rz_seq = 4;
            rz_int |= (INT_BUSSV | INT_FC);
            }
        sim_activate (&rz_unit[8], 50);
        break;

    case 0x43:                                          /* select with ATN and stop */
        sim_debug (DBG_CMD, &rz_dev, "select with atn and stop\n");
        if (!scsi_arbitrate (&rz_bus, ini)) {
            rz_seq = 0;
            rz_int |= INT_DIS;                          /* disconnect */
            sim_activate (&rz_unit[8], 100);
            break;
            }
        scsi_set_atn (&rz_bus);
        if (!scsi_select (&rz_bus, tgt)) {
            rz_seq = 0;
            rz_int |= INT_DIS;                          /* disconnect */
            scsi_release (&rz_bus);
            sim_activate (&rz_unit[8], 100);
            break;
            }
        rz_buf[0] = rz_fifo_rd ();
        scsi_write (&rz_bus, &rz_buf[0], 1);            /* send one message byte */
        if (scsi_state (&rz_bus, tgt) == SCSI_DISC) {   /* disconnected? */
            rz_seq = 0;
            rz_int |= INT_DIS;
            }
        else {
            scsi_set_atn (&rz_bus);                     /* keep ATN asserted */
            rz_seq = 1;
            rz_int |= (INT_BUSSV | INT_FC);             /* continue */
            }
        sim_activate (&rz_unit[8], 50);
        break;

    case 0x44:                                          /* enable selection/reselection */
        sim_debug (DBG_CMD, &rz_dev, "enable selection/reselection\n");
        break;
    
    case 0x46:                                          /* select with ATN3 */
        sim_debug (DBG_CMD, &rz_dev, "select with atn3\n");
        scsi_set_atn (&rz_bus);
        if (!scsi_select (&rz_bus, tgt)) {
            rz_int |= INT_DIS;                          /* disconnect */
            scsi_release (&rz_bus);
            }
        sim_activate (&rz_unit[8], 50);
        break;

    case 0x1A:                                          /* set ATN */
        sim_debug (DBG_CMD, &rz_dev, "set atn\n");
        scsi_set_atn (&rz_bus);
        if (rz_bus.phase == 6) {                        /* TODO: check this */
            rz_int |= (INT_BUSSV | INT_FC);
            sim_activate (&rz_unit[8], 50);
            }
        break;
    
    case 0x1B:                                          /* reset ATN */
        sim_debug (DBG_CMD, &rz_dev, "reset atn\n");
        scsi_release_atn (&rz_bus);
        break;
    
    case 0x10:
        sim_debug (DBG_CMD, &rz_dev, "transfer information\n");
        if (cmd & 0x80) {                               /* DMA */
            rz_stat &= ~STS_TC;
            rz_txc = (rz_txi == 0) ? RZ_MAXFR : rz_txi;
            }
        old_phase = rz_bus.phase;
        switch (rz_bus.phase) {
            case SCSI_DATO:                             /* data out */
            case SCSI_CMD:                              /* command */
            case SCSI_MSGO:                             /* message out */
                if (rz_bus.phase == 6)
                    scsi_release_atn (&rz_bus);
                if (cmd & 0x80) {                       /* DMA */
                    RZ_READB (rz_dma, rz_txc, &rz_buf[0]);
                    txc = scsi_write (&rz_bus, &rz_buf[0], rz_txc);
                    rz_txc -= txc;
                    if (rz_txc == 0)
                        rz_stat |= STS_TC;
                    }
                else {
                    for (txc = 0; rz_fifo_c > 0; txc++)
                        rz_buf[txc] = rz_fifo_rd ();
                    txc = scsi_write (&rz_bus, &rz_buf[0], txc);
                    }
                break;
                
            case SCSI_DATI:                             /* data in */
            case SCSI_STS:                              /* status */
            case SCSI_MSGI:                             /* message in */
                if (cmd & 0x80) {                       /* DMA */
                    while ((rz_bus.phase == old_phase) && (rz_txc != 0)) {
                        txc = scsi_read (&rz_bus, &rz_buf[0], rz_txc);
                        RZ_WRITEB (rz_dma, txc, &rz_buf[0]);
                        rz_txc -= txc;
                        }
                    if (rz_txc == 0)
                        rz_stat |= STS_TC;
                    }
                else {
                    txc = scsi_read (&rz_bus, &rz_buf[0], 1);
                    rz_fifo_wr (rz_buf[0]);
                    }
                break;
                }
        rz_seq = 0;
        if (scsi_state (&rz_bus, tgt) == SCSI_DISC) {
            rz_int |= INT_DIS;
            }
        else {
            if (rz_bus.req)
                rz_int |= INT_BUSSV;
            if (rz_bus.phase == SCSI_MSGI)
                rz_int |= INT_FC;
            }
            sim_activate (&rz_unit[8], 50);
        break;

    case 0x11:
        sim_debug (DBG_CMD, &rz_dev, "initiator command complete\n");
        txc = 0;
        txc += scsi_read (&rz_bus, &rz_buf[0], 1);
        txc += scsi_read (&rz_bus, &rz_buf[1], 1);
        rz_fifo_wr (rz_buf[0]);
        rz_fifo_wr (rz_buf[1]);
        rz_seq = 0;
        rz_int |= INT_FC;
        sim_activate (&rz_unit[8], 50);
        break;
    
    case 0x12:
        sim_debug (DBG_CMD, &rz_dev, "message accepted\n");
        scsi_release (&rz_bus);
        rz_seq = 0;
        rz_int |= INT_DIS;
        sim_activate (&rz_unit[8], 50);
        break;

    case 0x18:
        sim_debug (DBG_CMD, &rz_dev, "transfer pad\n");
        if (cmd & 0x80) {                               /* DMA */
            rz_stat &= ~STS_TC;
            rz_txc = (rz_txi == 0) ? RZ_MAXFR : rz_txi;
            }
        old_phase = rz_bus.phase;
        switch (rz_bus.phase) {
            case SCSI_DATO:                             /* data out */
            case SCSI_CMD:                              /* command */
            case SCSI_MSGO:                             /* message out */
                if (rz_bus.phase == 6)
                    scsi_release_atn (&rz_bus);
                rz_buf[0] = 0;
                for (; ((rz_bus.phase == old_phase) && (rz_txc > 0)); rz_txc--)
                    scsi_write (&rz_bus, &rz_buf[0], 1);
                if (rz_txc == 0)
                    rz_stat |= STS_TC;
                break;
                
            case SCSI_DATI:                             /* data in */
            case SCSI_STS:                              /* status */
            case SCSI_MSGI:                             /* message in */
                for (; ((rz_bus.phase == old_phase) && (rz_txc > 0)); rz_txc--)
                    txc = scsi_read (&rz_bus, &rz_buf[0], 1);
                if (rz_txc == 0)
                    rz_stat |= STS_TC;
                break;
                }
        rz_seq = 0;
        if (scsi_state (&rz_bus, tgt) == SCSI_DISC)
            rz_int |= INT_DIS;
        else {
            if (rz_bus.req)
                rz_int |= INT_BUSSV;
            /* if (rz_bus.phase == SCSI_MSGI) */
                rz_int |= INT_FC;
            }
            sim_activate (&rz_unit[8], 50);
        break;

    default:
        sim_debug (DBG_CMD, &rz_dev, "unknown command %X\n", cmd);
        break;
        }
if (cmd > 0)
    rz_last_cmd = cmd;
}

void rz_sw_reset ()
{
uint32 i;

for (i = 0; i < 9; i++)
    sim_cancel (&rz_unit[i]);
rz_txc = 0;
rz_cfg1 = rz_cfg1 & 0x7;
rz_cfg2 = 0;
rz_cfg3 = 0;
rz_stat = 0;
rz_seq = 0;
rz_int = 0;
rz_dest = 0;
rz_fifo_reset ();
CLR_INT (SC);
scsi_reset (&rz_bus);
}

t_stat rz_reset (DEVICE *dptr)
{
uint32 i;
uint32 dtyp;
UNIT *uptr;
t_stat r;

if (rz_buf == NULL)
    rz_buf = (uint8 *)calloc (RZ_MAXFR, sizeof(uint8));
if (rz_buf == NULL)
    return SCPE_MEM;
r = scsi_init (&rz_bus, RZ_MAXFR);                      /* init SCSI bus */
if (r != SCPE_OK)
    return r;
rz_bus.dptr = dptr;                                     /* set bus device */
for (i = 0; i < 8; i++) {
    uptr = dptr->units + i;
    if (i == RZ_SCSI_ID)                                /* initiator ID? */
        uptr->flags = UNIT_DIS;                         /* disable unit */
    scsi_add_unit (&rz_bus, i, &rz_unit[i]);
    dtyp = GET_DTYPE (rz_unit[i].flags);
    scsi_set_unit (&rz_bus, &rz_unit[i], &rzdev_tab[dtyp]);
    scsi_reset_unit (&rz_unit[i]);
    }
rz_sw_reset ();
return SCPE_OK;
}

/* Set unit type (and capacity if user defined) */

t_stat rz_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
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
scsi_set_unit (&rz_bus, uptr, &rzdev_tab[val]);
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
fprintf (st, "NCR 53C94 SCSI Controller (%s)\n\n", dptr->name);
fprintf (st, "The %s controller simulates the NCR 53C94 SCSI controller connected\n", dptr->name);
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

const char *rz_description (DEVICE *dptr)
{
return "NCR 53C94 SCSI controller";
}
