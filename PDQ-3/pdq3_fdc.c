/* PDQ3_fdc.c: PDQ3 simulator Floppy disk device

   Work derived from Copyright (c) 2004-2012, Robert M. Supnik
   Copyright (c) 2013 Holger Veit

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of Robert M Supnik and Holger Veit 
   shall not be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik and Holger Veit.

   2013xxxx hv initial version
   20141003 hv compiler suggested warnings (vc++2013, gcc)
*/
#include "pdq3_defs.h"
#include "sim_imd.h"
#include <ctype.h>

/* FDC/DMA bit definitions */
/* declarations of FDC and DMA chip */
/* drive select register */
#define FDC_SEL_SIDE     0x80 /* 0=side0, 1=side1 */
#define FDC_SEL_SDEN     0x40 /* 0=DDEN, 1=SDEN */
#define FDC_SEL_UNIT3    0x08 /* 1=select */
#define FDC_SEL_UNIT2    0x04 /* 1=select */
#define FDC_SEL_UNIT1    0x02 /* 1=select */
#define FDC_SEL_UNIT0    0x01 /* 1=select */

/* command register */
#define FDC_BIT_HEADLOAD 0x08
#define FDC_BIT_VERIFY   0x04
#define FDC_BIT_STEP3    0x00
#define FDC_BIT_STEP6    0x01
#define FDC_BIT_STEP10   0x02
#define FDC_BIT_STEP15   0x03
#define FDC_BIT_UPDATE   0x10
#define FDC_BIT_MULTI    0x10
#define FDC_BIT_SIDESEL  0x08  
#define FDC_BIT_SIDECMP  0x02  
#define FDC_BIT_DATAMARK 0x01
#define FDC_BIT_INTIMM   0x08
#define FDC_BIT_INTIDX   0x04
#define FDC_BIT_INTN2R   0x02
#define FDC_BIT_INTR2N   0x01

#define FDC_RESTORE      0x00
#define FDC_SEEK         0x10
#define FDC_STEP         0x20
#define FDC_STEP_U       0x30
#define FDC_STEPIN       0x40
#define FDC_STEPIN_U     0x50
#define FDC_STEPOUT      0x60
#define FDC_STEPOUT_U    0x70
#define FDC_READSEC      0x80
#define FDC_READSEC_M    0x90
#define FDC_WRITESEC     0xa0
#define FDC_WRITESEC_M   0xb0
#define FDC_READADDR     0xc4
#define FDC_READTRK      0xe4
#define FDC_WRITETRK     0xf4
#define FDC_FORCEINT     0xd0
#define FDC_IDLECMD      0xff

#define FDC_CMDMASK      0xf0

/* status register */
#define FDC_ST1_NOTREADY      0x80
#define FDC_ST1_WRTPROT       0x40
#define FDC_ST1_HEADLOAD      0x20
#define FDC_ST1_SEEKERROR     0x10
#define FDC_ST1_CRCERROR      0x08
#define FDC_ST1_TRACK0        0x04
#define FDC_ST1_IDXPULSE      0x02
#define FDC_ST1_BUSY          0x01
#define FDC_ST2_NOTREADY      FDC_ST1_NOTREADY
#define FDC_ST2_WRTPROT       FDC_ST1_WRTPROT
#define FDC_ST2_TYPEWFLT      0x20
#define FDC_ST2_RECNOTFND     0x10
#define FDC_ST2_CRCERROR      FDC_ST1_CRCERROR
#define FDC_ST2_LOSTDATA      0x04
#define FDC_ST2_DRQ           0x02
#define FDC_ST2_BUSY          FDC_ST1_BUSY

/* DMA ctrl reg */
#define DMA_CTRL_AECE         0x40
#define DMA_CTRL_HBUS         0x20
#define DMA_CTRL_IOM          0x10
#define DMA_CTRL_TCIE         0x08
#define DMA_CTRL_TOIE         0x04
#define DMA_CTRL_DIE          0x02
#define DMA_CTRL_RUN          0x01

/* DMA status reg */
#define DMA_ST_BUSY           0x80
#define DMA_ST_AECE           DMA_CTRL_AECE
#define DMA_ST_HBUS           DMA_CTRL_HBUS
#define DMA_ST_IOM            DMA_CTRL_IOM
#define DMA_ST_TCZI           0x08
#define DMA_ST_TOI            0x04
#define DMA_ST_DINT           0x02
#define DMA_ST_BOW            0x01

/* FDC unit flags */
#define UNIT_V_FDC_WLK        (UNIT_V_UF + 0) /* write locked                             */
#define UNIT_FDC_WLK          (1 << UNIT_V_FDC_WLK)
#define UNIT_V_FDC_VERBOSE    (UNIT_V_UF + 1) /* verbose mode, i.e. show error messages   */
#define UNIT_FDC_VERBOSE      (1 << UNIT_V_FDC_VERBOSE)

/* FDC timing */
#define FDC_WAIT_STEP         3000
#define FDC_WAIT_READ         8000
#define FDC_WAIT_READNEXT     800
#define FDC_WAIT_WRITE        8000
#define FDC_WAIT_WRITENEXT    800
#define FDC_WAIT_FORCEINT     100
#define FDC_WAIT_IDXPULSE     16000

uint8 reg_fdc_cmd;    /* FC30 write */
uint8 reg_fdc_status; /* FC30 read */
int8  reg_fdc_track;  /* FC31 */
int8  reg_fdc_sector; /* FC32 */
int8  reg_fdc_data;   /* FC33 */
uint8 reg_fdc_drvsel; /* only combined */

uint8 reg_dma_ctrl;   /* FC38 writeonly */
uint8 reg_dma_status; /* FC39 */
uint8 reg_dma_cntl;   /* FC3A */
uint8 reg_dma_cnth;   /* FC3B */
uint8 reg_dma_addrl;  /* FC3C */
uint8 reg_dma_addrh;  /* FC3D */
uint8 reg_dma_addre;  /* FC3E */
uint8 reg_dma_id;     /* FC3F - unusable */
uint16 _reg_dma_cnt;  /* combined reg */
uint32 _reg_dma_addr; /* combined reg */

int8 fdc_selected;    /* currently selected drive, -1=none */
uint8 fdc_intpending; /* currently executing force interrupt command, 0=none */

uint8 fdc_recbuf[1024];
uint32 fdc_recsize;

t_bool dma_isautoload;

/* externals */
extern UNIT cpu_unit;

/* forwards */
t_stat fdc_svc (UNIT *uptr);
t_stat fdc_reset (DEVICE *uptr);
t_stat fdc_attach(UNIT *uptr, CONST char *cptr);
t_stat fdc_detach(UNIT *uptr);
t_stat pdq3_diskCreate(FILE *fileref, const char *ctlr_comment);
t_stat pdq3_diskFormat(DISK_INFO *myDisk);
static void dma_reqinterrupt();

/* data structures */
typedef struct _drvdata {
  UNIT  *dr_unit;
  DISK_INFO *dr_imd;
  uint8 dr_ready;
  uint8 dr_head;
  uint8 dr_trk;
  uint8 dr_sec;
  uint8 dr_stepdir; /* 0=in, 1=out */
} DRVDATA;

DRVDATA fdc_drv[] = {
  { NULL, NULL, 0, },
  { NULL, NULL, 0, },
  { NULL, NULL, 0, },
  { NULL, NULL, 0, }
};

/* FDC data structures
   fdc_dev      FDC device descriptor
   fdc_unit     FDC unit descriptor
   fdc_mod      FDC modifier list
   fdc_reg      FDC register list
*/
IOINFO fdc_ioinfo = { NULL, FDC_IOBASE, 16, FDC_VEC, 2, fdc_read, fdc_write };
IOINFO fdc_ctxt = { &fdc_ioinfo };

UNIT fdc_unit[] = {
     { UDATA (&fdc_svc, UNIT_ATTABLE|UNIT_FIX|UNIT_BINK|UNIT_ROABLE|UNIT_DISABLE, 0), 0, },
     { UDATA (&fdc_svc, UNIT_ATTABLE|UNIT_FIX|UNIT_BINK|UNIT_ROABLE|UNIT_DISABLE, 0), 1, },
};
REG fdc_reg[] = {
  { HRDATA (FCMD,    reg_fdc_cmd, 8) },
  { HRDATA (FSTAT,   reg_fdc_status, 8) },
  { HRDATA (FTRK,    reg_fdc_track, 8) },
  { HRDATA (FSEC,    reg_fdc_sector, 8) },
  { HRDATA (FDATA,   reg_fdc_data, 8) },
  { HRDATA (FSEL,    reg_fdc_drvsel, 8) },

  { HRDATA (DCMD,    reg_dma_ctrl, 8) },
  { HRDATA (DSTAT,   reg_dma_status, 8) },
  { HRDATA (DCNTH,   reg_dma_cnth, 8) },
  { HRDATA (DCNTL,   reg_dma_cntl, 8) },
  { HRDATA (_DCNT,   _reg_dma_cnt, 16), REG_RO|REG_HIDDEN },
  { HRDATA (DADDRE,  reg_dma_addre, 8) },
  { HRDATA (DADDRH,  reg_dma_addrh, 8) },
  { HRDATA (DADDRL,  reg_dma_addrl, 8) },
  { HRDATA (_DADDR,  _reg_dma_addr, 18), REG_RO|REG_HIDDEN },
  { NULL }
};
MTAB fdc_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0,                 "IOBASE", "IOBASE", NULL, &show_iobase },
  { MTAB_XTD|MTAB_VDV, 0,                 "VECTOR", "VECTOR", NULL, &show_iovec },
  { MTAB_XTD|MTAB_VDV, 0,                 "PRIO",   "PRIO",   NULL, &show_ioprio },
  { UNIT_FDC_WLK,      0,                 "WRTENB", "WRTENB",   NULL },
  { UNIT_FDC_WLK,      UNIT_FDC_WLK,      "WRTLCK", "WRTLCK",   NULL },
  { 0 }
};
DEBTAB fdc_dflags[] = {
  { "CMD",   DBG_FD_CMD },
  { "READ",  DBG_FD_READ },
  { "WRITE", DBG_FD_WRITE },
  { "SVC",   DBG_FD_SVC },
  { "IMD",   DBG_FD_IMD },
  { "IMD2",  DBG_FD_IMD2 }, /* deep inspection */
  { "DMA",   DBG_FD_DMA },
  { "DMA2",  DBG_FD_DMA2 }, /* deep inspection */
  { 0, 0 }
};

DEVICE fdc_dev = {
    "FDC",      /*name*/
    fdc_unit,   /*units*/
    fdc_reg,    /*registers*/
    fdc_mod,    /*modifiers*/
    2,          /*numunits*/
    16,         /*aradix*/
    16,         /*awidth*/
    1,          /*aincr*/
    8,          /*dradix*/
    8,          /*dwidth*/
    NULL,       /*examine*/
    NULL,       /*deposit*/
    &fdc_reset, /*reset*/
    NULL,       /*boot. Note this is hidden, use BOOT CPU. */
    &fdc_attach,/*attach*/
    &fdc_detach,/*detach*/
    &fdc_ctxt,  /*ctxt*/
    DEV_DEBUG,  /*flags*/
    0,          /*dctrl*/
    fdc_dflags, /*debflags*/
    NULL,       /*msize*/
    NULL        /*lname*/
};

/* boot unit - not available through BOOT FDC cmd, use BOOT CPU instead */
t_stat fdc_boot(int32 unitnum, DEVICE *dptr) {
  if (unitnum < 0 || (uint32)unitnum > dptr->numunits)
    return SCPE_NXUN;
//  sim_printf("BOOT FDC%d\n",unitnum);
  return fdc_autoload(unitnum);
}

t_stat fdc_attach(UNIT *uptr, CONST char *cptr) {
  t_stat rc;
  int i = uptr->u_unitno;
  char header[4];

  sim_debug(DBG_FD_IMD, &fdc_dev, DBG_PCFORMAT1 "Attach FDC drive %d\n", DBG_PC, i);

  sim_cancel(uptr);
  if ((rc=attach_unit(uptr,cptr)) != SCPE_OK) return rc;
       
  fdc_drv[i].dr_unit = uptr;
  uptr->capac = sim_fsize(uptr->fileref);
  fdc_drv[i].dr_ready = 0;
  
  if (uptr->capac > 0) {
    fgets(header, 4, uptr->fileref);
    if (strncmp(header, "IMD", 3) != 0) {
      sim_printf("FDC: Only IMD disk images are supported\n");
      fdc_drv[i].dr_unit = NULL;
      return SCPE_OPENERR;
    }
  } else {
    /* create a disk image file in IMD format. */
    if (pdq3_diskCreate(uptr->fileref, "SIMH pdq3_fdc created") != SCPE_OK) {
      sim_printf("FDC: Failed to create IMD disk.\n");
      fdc_drv[i].dr_unit = NULL;
      return SCPE_OPENERR;
    }
    uptr->capac = sim_fsize(uptr->fileref);
  }
  sim_debug(DBG_FD_IMD, &fdc_dev, DBG_PCFORMAT2 "Attached to '%s', type=IMD, len=%d\n",
    DBG_PC, cptr, uptr->capac);
  fdc_drv[i].dr_imd = diskOpenEx(uptr->fileref, isbitset(uptr->flags,UNIT_FDC_VERBOSE), &fdc_dev, DBG_FD_IMD, DBG_FD_IMD2);
  if (fdc_drv[i].dr_imd == NULL) {
    sim_printf("FDC: IMD disk corrupt.\n");
    fdc_drv[i].dr_unit = NULL;
    return SCPE_OPENERR;
  }
  fdc_drv[i].dr_ready = 1;
  
  /* handle force interrupt to wait for disk change */
  if (isbitset(fdc_intpending,0x01)) {
    dma_reqinterrupt();
    clrbit(reg_fdc_status,FDC_ST1_BUSY);
    clrbit(fdc_intpending,0x01);
  }
  
  return SCPE_OK;
}

t_stat fdc_detach(UNIT *uptr) {
  t_stat rc;
  int i = uptr->u_unitno;
  
  sim_debug(DBG_FD_IMD, &fdc_dev, DBG_PCFORMAT1 "Detach FDC drive %d\n", DBG_PC, i);
  sim_cancel(uptr);
  rc = diskClose(&fdc_drv[i].dr_imd);
  fdc_drv[i].dr_ready = 0;
  
  /* handle force interrupt to wait for disk change */
  if (isbitset(fdc_intpending,0x02)) {
    cpu_raiseInt(INT_DMAFD);
    clrbit(reg_fdc_status,FDC_ST1_BUSY);
    clrbit(fdc_intpending,0x02);
  }
  
  if (rc != SCPE_OK) return rc;
  return detach_unit(uptr);  /* detach unit */
}

static t_stat fdc_start(UNIT *uptr,int time) {
  /* request service */
  sim_debug(DBG_FD_SVC, &fdc_dev, DBG_PCFORMAT2 "Start Service after %d ticks\n", DBG_PC, time);
  return sim_activate(uptr, time);
}

static t_stat fdc_stop(UNIT *uptr) {
  /* request service */
  sim_debug(DBG_FD_SVC, &fdc_dev, DBG_PCFORMAT2 "Cancel Service\n", DBG_PC);
  return sim_cancel(uptr);
}

static void fdc_update_rdonly(DRVDATA *curdrv) {
  /* read only drive? */  
  if (isbitset(curdrv->dr_unit->flags,UNIT_RO))
    setbit(reg_fdc_status,FDC_ST1_WRTPROT);
  else
    clrbit(reg_fdc_status,FDC_ST1_WRTPROT);
}

t_bool fdc_driveready(DRVDATA *curdrv) {
  /* some drive selected, and disk in drive? */
  if (curdrv==NULL || curdrv->dr_ready == 0) {
    setbit(reg_fdc_status,FDC_ST1_NOTREADY);
    clrbit(reg_fdc_status,FDC_ST1_BUSY);
    reg_fdc_cmd = FDC_IDLECMD;
    return FALSE;
  }
  
  /* drive is ready */
  clrbit(reg_fdc_status,FDC_ST1_NOTREADY);

  /* read only drive? */  
  fdc_update_rdonly(curdrv);
  return TRUE;
}

static t_bool fdc_istrk0(DRVDATA *curdrv,int8 trk) {
  curdrv->dr_trk = trk;
  if (trk <= 0) {
    setbit(reg_fdc_status,FDC_ST1_TRACK0);
    reg_fdc_track = 0;
    return TRUE;
  }
  return FALSE;
}

/* return true if invalid track (CRC error) */
static t_bool fdc_stepin(DRVDATA *curdrv, t_bool upd) {
  curdrv->dr_stepdir = FDC_STEPIN;
  curdrv->dr_trk++;
  if (upd) reg_fdc_track = curdrv->dr_trk;
  if (curdrv->dr_trk > FDC_MAX_TRACKS) {
    setbit(reg_fdc_status,FDC_ST1_CRCERROR);
    return TRUE;
  }
  return FALSE;
}

/* return true if trk0 reached */
static t_bool fdc_stepout(DRVDATA *curdrv, t_bool upd) {
   curdrv->dr_stepdir = FDC_STEPOUT;
   curdrv->dr_trk--;
   if (upd) reg_fdc_track = curdrv->dr_trk;
   return fdc_istrk0(curdrv, reg_fdc_track);
}

static void fdc_clr_st1_error() {
  clrbit(reg_fdc_status,FDC_ST1_NOTREADY|FDC_ST1_SEEKERROR|FDC_ST1_CRCERROR);
}

static void dma_interrupt(int bit) {
  if (isbitset(reg_dma_ctrl,bit)) {
    sim_debug(DBG_FD_DMA, & fdc_dev, DBG_PCFORMAT2 "Raise DMA/FDC interrupt\n", DBG_PC);
    cpu_raiseInt(INT_DMAFD);
  }
}

static t_bool dma_abort(t_bool fromfinish) {
  clrbit(reg_dma_status,DMA_ST_BUSY);
  clrbit(reg_dma_ctrl,DMA_CTRL_RUN);
  
  /* if autoload was finished, finally start the CPU.
   * note: autoload will read the first track, and then fail at end of track with an error */
  if (dma_isautoload) {
    sim_debug(DBG_FD_DMA, & fdc_dev, DBG_PCFORMAT2 "AUTOLOAD finished by end-of-track (DMA aborted)\n", DBG_PC);
    cpu_finishAutoload();
    dma_isautoload = FALSE;
  } else if (!fromfinish) {
    sim_debug(DBG_FD_DMA, & fdc_dev, DBG_PCFORMAT2 "Aborted transfer\n", DBG_PC);
  }
  return FALSE;
}

/* all data transferred */
static void dma_finish() {
  setbit(reg_dma_status,DMA_ST_TCZI);
  dma_abort(TRUE);
  dma_interrupt(DMA_CTRL_TCIE);
  sim_debug(DBG_FD_DMA, & fdc_dev, DBG_PCFORMAT2 "Finished transfer\n", DBG_PC);
}

/* request interrupt from FDC */
static void dma_reqinterrupt() {
  setbit(reg_dma_status,DMA_ST_DINT);
  dma_interrupt(DMA_CTRL_DIE);
}

static void dma_fix_regs() {
  reg_dma_cntl = _reg_dma_cnt & 0xff;
  reg_dma_cnth = _reg_dma_cnt>>8;
  reg_dma_addre = (_reg_dma_addr>>16) & 0x03;
  reg_dma_addrh = (_reg_dma_addr>>8) & 0xff;
  reg_dma_addrl = _reg_dma_addr & 0xff;
}

/* return true if successfully transferred */
static t_bool dma_transfer_to_ram(uint8 *buf, int bufsize) {
  t_bool rc = TRUE;
  int i;
  uint16 data;
  t_addr tstart = _reg_dma_addr/2;
  int cnt = _reg_dma_cnt ^ 0xffff;
  int xfersz = bufsize > cnt ? cnt : bufsize;
  
  sim_debug(DBG_FD_DMA, &fdc_dev, DBG_PCFORMAT2 "Transfer to RAM $%x...$%x\n",
    DBG_PC, _reg_dma_addr/2,(_reg_dma_addr + xfersz - 1)/2);
  for (i=0; i<xfersz; i += 16) {
    sim_debug(DBG_FD_DMA2, &fdc_dev, DBG_PCFORMAT1 "$%04x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
      DBG_PC, tstart, buf[i+0],buf[i+1], buf[i+2],buf[i+3], buf[i+4],buf[i+5], buf[i+6],buf[i+7], 
      buf[i+8],buf[i+9], buf[i+10],buf[i+11], buf[i+12],buf[i+13], buf[i+14],buf[i+15]);
    tstart += 8;
  }
  
  if (isbitclr(reg_dma_ctrl,DMA_CTRL_IOM))
    sim_printf("Warning: wrong IOM direction for DMA transfer to RAM\n");
  
  for (i=0; i<bufsize; i++) {
    data = buf[i];
//    sim_printf("addr=%04x data=%02x\n",_reg_dma_addr, data);
    
    if (WriteB(0, _reg_dma_addr++, data, FALSE) != SCPE_OK) {
      (void)dma_abort(FALSE);
      setbit(reg_dma_status,DMA_ST_TOI);
      dma_interrupt(DMA_CTRL_TOIE);
      return FALSE; /* write fault */
    }
    _reg_dma_cnt++;
    if (_reg_dma_cnt == 0) /* all data done? */
      break;
  }
  if (_reg_dma_cnt == 0) { /* all data done? */
    dma_finish();
    rc = FALSE;
  }
  
  dma_fix_regs();
  return rc;
}

/* return true if successfully transferred */
static t_bool dma_transfer_from_ram(uint8 *buf, int bufsize) {
  t_bool rc = TRUE;
  int i;
  uint16 data;
  uint32 tstart = _reg_dma_addr/2;
  int cnt = _reg_dma_cnt ^ 0xffff;
  int xfersz = bufsize > cnt ? cnt : bufsize;
  
  sim_debug(DBG_FD_DMA, &fdc_dev, DBG_PCFORMAT2 "Transfer from RAM $%x...$%x\n",
    DBG_PC, _reg_dma_addr/2, (_reg_dma_addr + xfersz - 1)/2);

  if (isbitset(reg_dma_ctrl,DMA_CTRL_IOM))
    sim_printf("Warning: wrong IOM direction for DMA transfer from RAM\n");
  
  for (i=0; i<bufsize; i++) {
    if (ReadB(0, _reg_dma_addr++, &data, FALSE)) {
      (void)dma_abort(FALSE);
      setbit(reg_dma_status,DMA_ST_TOI);
      dma_interrupt(DMA_CTRL_TOIE);
      return FALSE; /* write fault */
    }
    buf[i] = data & 0xff;
    _reg_dma_cnt++;
    if (_reg_dma_cnt == 0) /* all data done? */
      break;
  }
  for (i=0; i<xfersz; i += 16) {
    sim_debug(DBG_FD_DMA2, &fdc_dev, DBG_PCFORMAT1 "$%04x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
      DBG_PC, tstart, buf[i+0],buf[i+1], buf[i+2],buf[i+3], buf[i+4],buf[i+5], buf[i+6],buf[i+7], 
      buf[i+8],buf[i+9], buf[i+10],buf[i+11], buf[i+12],buf[i+13], buf[i+14],buf[i+15]);
    tstart += 8;
  }
  
  if (_reg_dma_cnt == 0) { /* all data done? */
    dma_finish();
    rc = FALSE;
  }

  dma_fix_regs();
  return rc;
}

/* return true if read satisfied, false if error */
static t_bool fdc_readsec(DRVDATA *curdrv) {
  uint32 flags;
  
  /* does sector exist? */
  if (sectSeek(curdrv->dr_imd, curdrv->dr_trk, curdrv->dr_head)) {
    setbit(reg_fdc_status,FDC_ST2_RECNOTFND);
    return FALSE;
  }

  /* get sector size available */
  fdc_recsize = curdrv->dr_imd->track[curdrv->dr_trk][curdrv->dr_head].sectsize;

  /* clear errors. Also clear LOSTDATA bit due to aliasing to TRACK00 bit from previous seek operation */
  clrbit(reg_fdc_status,FDC_ST2_NOTREADY|FDC_ST2_LOSTDATA|FDC_ST2_WRTPROT);

  if (sectRead(curdrv->dr_imd, curdrv->dr_trk, curdrv->dr_head, curdrv->dr_sec,
     fdc_recbuf, fdc_recsize, &flags, &fdc_recsize)) {
    setbit(reg_fdc_status,FDC_ST2_RECNOTFND);
    return FALSE;
  }
  if (isbitset(flags,IMD_DISK_IO_ERROR_CRC)) {
    setbit(reg_fdc_status,FDC_ST2_CRCERROR);
    return FALSE; /* terminate read */
  }
  if (isbitset(flags,IMD_DISK_IO_DELETED_ADDR_MARK))
    setbit(reg_fdc_status,FDC_ST2_TYPEWFLT);

  /* trigger DMA transfer */
  if (!dma_transfer_to_ram(fdc_recbuf, fdc_recsize))
    return FALSE;

  /* now finished */
  return TRUE;
}

static t_bool fdc_writesec(DRVDATA *curdrv) {
  uint32 flags;
  
  /* write protect? */
  if (imdIsWriteLocked(curdrv->dr_imd)) {
    dma_abort(FALSE);
    setbit(reg_fdc_status,FDC_ST2_WRTPROT);
    return FALSE;
  }
   
  /* is sector available? */    
  if (sectSeek(curdrv->dr_imd, curdrv->dr_trk, curdrv->dr_head)) {
    setbit(reg_fdc_status,FDC_ST2_RECNOTFND);
    return FALSE;
  }
  /* clear errors. Also clear LOSTDATA bit due to aliasing to TRACK00 bit from previous seek operation */
  clrbit(reg_fdc_status,FDC_ST2_NOTREADY|FDC_ST2_LOSTDATA|FDC_ST2_WRTPROT);

  /* get sector size */
  fdc_recsize = curdrv->dr_imd->track[curdrv->dr_trk][curdrv->dr_head].sectsize;

  /* get from ram into write buffer */
  if (!dma_transfer_from_ram(fdc_recbuf, fdc_recsize))
    return FALSE;

  if (sectWrite(curdrv->dr_imd, curdrv->dr_trk, curdrv->dr_head, curdrv->dr_sec,
     fdc_recbuf, fdc_recsize, &flags, &fdc_recsize)) {
    setbit(reg_fdc_status,FDC_ST2_RECNOTFND);
    return FALSE;
  }
  if (isbitset(flags,IMD_DISK_IO_ERROR_GENERAL)) {
    setbit(reg_fdc_status,FDC_ST2_TYPEWFLT);
    return FALSE;
  }
  if (isbitset(flags,IMD_DISK_IO_ERROR_WPROT)) {
    setbit(reg_fdc_status,FDC_ST2_WRTPROT);
    return FALSE;
  }
  /* advance to next sector for multiwrite */
  if (isbitset(reg_fdc_cmd,FDC_BIT_MULTI)) { /* multi bit */
    curdrv->dr_sec++;
    reg_fdc_sector++;
  }
  
  /* now finished */
  return TRUE;
}

static t_bool fdc_rwerror() {
 /* note: LOSTDATA cannot occur */
 return isbitset(reg_fdc_status,FDC_ST2_TYPEWFLT|FDC_ST2_RECNOTFND|FDC_ST2_CRCERROR /*|FDC_ST2_LOSTDATA*/);
}

static t_stat fdc_set_notready(uint8 cmd)
{
  switch (cmd & FDC_CMDMASK) {
  default:
  case FDC_RESTORE:
  case FDC_SEEK:
  case FDC_STEP: /* single step in current direction, no update */
  case FDC_STEPIN: /* single step towards center */
  case FDC_STEPOUT: /* single step towards edge of disk */
  case FDC_STEP_U:
  case FDC_STEPIN_U:
  case FDC_STEPOUT_U:
    setbit(reg_fdc_status,FDC_ST1_SEEKERROR);
    break;
 
  case FDC_READSEC_M: 
  case FDC_READSEC: /* type II: read a sector via DMA */
    setbit(reg_fdc_status,FDC_ST2_CRCERROR);
    break;
  case FDC_WRITESEC_M:
  case FDC_WRITESEC: /* type II: read a sector via DMA */
    setbit(reg_fdc_status,FDC_ST2_TYPEWFLT);
    break;
  }
  return SCPE_OK;
}

static t_stat fdc_restartmulti(DRVDATA *curdrv,int wait) {
  /* advance to next sector for multiread */
  sim_debug(DBG_FD_SVC, &fdc_dev, "  Restarting FDC_SVC for multiple R/W\n");
  curdrv->dr_sec++;
  reg_fdc_sector++;
  /* restart service for multi-sector */
  return fdc_start(curdrv->dr_unit, wait);
}

/* process the FDC commands, and restart, if necessary */
t_stat fdc_svc(UNIT *uptr) {
  DRVDATA *curdrv = fdc_selected==-1 ? NULL : &fdc_drv[fdc_selected];
  t_bool rdy = fdc_driveready(curdrv);
  t_bool um_flg; /* update or multi bit */

  sim_debug(DBG_FD_SVC,&fdc_dev, DBG_PCFORMAT2 "Calling FDC_SVC for unit=%x cmd=%x\n",
    DBG_PC, fdc_selected, reg_fdc_cmd);
  
  if (reg_fdc_cmd == FDC_IDLECMD) return SCPE_OK;

  if (!rdy) return fdc_set_notready(reg_fdc_cmd & FDC_CMDMASK);
  
  um_flg = isbitset(reg_fdc_cmd,FDC_BIT_UPDATE);
  switch (reg_fdc_cmd & FDC_CMDMASK) {
  case FDC_RESTORE:
    fdc_istrk0(curdrv,0);
    curdrv->dr_stepdir = FDC_STEPOUT;
    break;
  case FDC_SEEK:
    if (reg_fdc_track > reg_fdc_data) {
      if (fdc_stepout(curdrv, TRUE)) break;
      return fdc_start(curdrv->dr_unit,FDC_WAIT_STEP);
    } else if (reg_fdc_track < reg_fdc_data) {
      if (fdc_stepin(curdrv, TRUE)) break;
      return fdc_start(curdrv->dr_unit,FDC_WAIT_STEP);
    }
    /* found position */
    fdc_clr_st1_error();
    break;
  case FDC_STEP: /* single step in current direction, no update */
  case FDC_STEP_U:
    if (curdrv->dr_stepdir == FDC_STEPIN) {
      if (fdc_stepin(curdrv, um_flg)) break;
    } else
      fdc_stepout(curdrv, um_flg);
    fdc_clr_st1_error();
    break;
  case FDC_STEPIN: /* single step towards center */
  case FDC_STEPIN_U:
    if (fdc_stepin(curdrv, um_flg)) break;
    fdc_clr_st1_error();
    break;
  case FDC_STEPOUT: /* single step towards edge of disk */
  case FDC_STEPOUT_U:
    if (fdc_stepin(curdrv, um_flg)) break;
    fdc_clr_st1_error();
    break;
 
  case FDC_READSEC_M: 
  case FDC_READSEC: /* type II: read a sector via DMA */
    if (!fdc_readsec(curdrv) || fdc_rwerror()) {
      dma_abort(TRUE);
      break;
    }
    if (isbitset(reg_dma_status,DMA_ST_BUSY) && um_flg)
      return fdc_restartmulti(curdrv,FDC_WAIT_READNEXT);
    break;
  case FDC_WRITESEC_M:
  case FDC_WRITESEC: /* type II: read a sector via DMA */
    if (!fdc_writesec(curdrv) || fdc_rwerror()) {
      dma_abort(TRUE);
      break;
    }
    if (isbitset(reg_dma_status,DMA_ST_BUSY) && um_flg)
      return fdc_restartmulti(curdrv,FDC_WAIT_WRITENEXT);
    break;
  default:
    sim_printf("fdc_svc: Fix me - command not yet implemented: cmd=0x%x\n", reg_fdc_cmd);
  }
       
  clrbit(reg_fdc_status,FDC_ST1_BUSY);
  reg_fdc_cmd = FDC_IDLECMD;
  return SCPE_OK;
}

t_stat fdc_binit() {
  fdc_selected = -1;
  fdc_intpending = 0;
  
  /* reset FDC registers */
  reg_fdc_cmd = FDC_IDLECMD;  /* invalid command, used as idle marker */
  reg_fdc_status = 0;
  reg_fdc_track = 0;
  reg_fdc_sector = 1;
  reg_fdc_data = 1;
  reg_fdc_drvsel = 0;

  /* reset DMA registers */
  reg_dma_ctrl = DMA_CTRL_AECE | DMA_CTRL_HBUS | DMA_CTRL_IOM;
  reg_dma_status = DMA_ST_AECE | DMA_ST_HBUS | DMA_ST_IOM;
  _reg_dma_cnt = 0x0001;
  /* hack: initialize boot code to load ad 0x2000(word address). 
    * However, DMA is based on byte addresses, so multiply with 2 */
  _reg_dma_addr = reg_dmabase*2;
  reg_dma_id = 0;

  dma_fix_regs();
  return SCPE_OK;
}

t_stat fdc_reset (DEVICE *dptr) {
  int i;
  DEVCTXT* ctxt = (DEVCTXT*)dptr->ctxt;
//  sim_printf("RESET FDC\n");

  if (dptr->flags & DEV_DIS)
    del_ioh(ctxt->ioi);
  else
    add_ioh(ctxt->ioi);

  for (i=0; i<4; i++) {
    DRVDATA *cur = &fdc_drv[i];
    cur->dr_unit = &fdc_unit[i];
    cur->dr_trk = 0;
    cur->dr_sec = 1;
    cur->dr_head = 0;
    cur->dr_stepdir = 0;
  }
  return fdc_binit();
}

/* select drive, according to select register */
static DRVDATA *fdc_select() {
  DRVDATA *curdrv = NULL;

  if (isbitset(reg_fdc_drvsel,FDC_SEL_UNIT0)) fdc_selected = 0;
  else if (isbitset(reg_fdc_drvsel,FDC_SEL_UNIT1)) fdc_selected = 1;
  else if (isbitset(reg_fdc_drvsel,FDC_SEL_UNIT2)) fdc_selected = 2;
  else if (isbitset(reg_fdc_drvsel,FDC_SEL_UNIT3)) fdc_selected = 3;
  else fdc_selected = -1;
 
  if (fdc_selected >= 0)  {
    curdrv = &fdc_drv[fdc_selected];

    fdc_update_rdonly(curdrv); /* update R/O flag */
    curdrv->dr_head = isbitset(reg_fdc_drvsel,FDC_SEL_SIDE) ? 1 : 0;
    curdrv->dr_unit = &fdc_unit[fdc_selected];
  }
  return curdrv;
}

static const char *cmdlist[] = {
  "Restore","Seek","Step","Step+Upd","StepIn","StepIn+Upd",
  "StepOut","StepOut+Upd","Read","Read+Multi","Write","WriteMulti",
  "ReadAddr","ForceInt","ReadTrack","WriteTrack"
};

static void debug_fdccmd(uint16 cmd) {
  char buf[200];
  uint16 dsel = cmd >> 8, cr = (cmd >> 4) & 0x0f;

  buf[0] = 0;
  if (cmd & 0xff00) {
    strcat(buf,"DSR=[");
    strcat(buf,dsel & FDC_SEL_SIDE ? "SIDE1" : "SIDE0");
    if (dsel & FDC_SEL_SDEN) strcat(buf,",SDEN");
    strcat(buf,",UNIT");
    if (dsel & FDC_SEL_UNIT3) strcat(buf,"3");
    else if (dsel & FDC_SEL_UNIT2) strcat(buf,"2");
    else if (dsel & FDC_SEL_UNIT1) strcat(buf,"1");
    else if (dsel & FDC_SEL_UNIT0) strcat(buf,"0");
    strcat(buf,"] ");
  }
  strcat(buf,"CR=[");
  strcat(buf,cmdlist[cr]);
  if (cr < 8) {
    if (cmd & FDC_BIT_HEADLOAD) strcat(buf,"+Load");
    if (cmd & FDC_BIT_VERIFY) strcat(buf,"+Vrfy");
    cmd &= FDC_BIT_STEP15;
    if (cmd == FDC_BIT_STEP3) strcat(buf,"+Step3");
    else if (cmd == FDC_BIT_STEP6) strcat(buf,"+Step6");
    else if (cmd == FDC_BIT_STEP10) strcat(buf,"+Step10");
    else if (cmd == FDC_BIT_STEP15) strcat(buf,"+Step15");
  } else
    switch (cr) {
    case 8: case 9:
    case 0xa: case 0xb:
      strcat(buf, cmd & FDC_BIT_SIDESEL ? "+SideSel1" : "+SideSel0");
      strcat(buf, cmd & FDC_BIT_SIDECMP ? "+SideCmp1" : "+SideCmp0");
      if (cr > 9)
        strcat(buf, cmd & FDC_BIT_DATAMARK ? "+DelMark" : "+DataMark");
    default:
      break;
    case 0x0f:
      if (cmd & FDC_BIT_INTIMM) strcat(buf,"+IMM");
      if (cmd & FDC_BIT_INTIDX) strcat(buf,"+IDX");
      if (cmd & FDC_BIT_INTN2R) strcat(buf,"+N2R");
      if (cmd & FDC_BIT_INTR2N) strcat(buf,"+R2N");
    }
  strcat(buf,"]");
  sim_debug(DBG_FD_CMD, &fdc_dev, DBG_PCFORMAT2 "Command: %s\n", DBG_PC,buf);  
}

static t_stat fdc_docmd(uint16 data) {
  UNIT *uptr;
  DRVDATA *curdrv = fdc_select(); 
  if (curdrv== NULL) return SCPE_IOERR;

  debug_fdccmd(data);
  uptr = curdrv->dr_unit;

  if (!fdc_driveready(curdrv)) {
    sim_debug(DBG_FD_CMD,&fdc_dev, DBG_PCFORMAT2 "fdc_docmd: drive not ready\n", DBG_PC);
    return SCPE_OK;
  }
  
  reg_fdc_cmd = data & 0xff;
  switch (data & FDC_CMDMASK) {
  /* type I commands */
  case FDC_RESTORE:
  case FDC_SEEK:
  case FDC_STEP:
  case FDC_STEP_U:
  case FDC_STEPIN:
  case FDC_STEPIN_U:
  case FDC_STEPOUT:
  case FDC_STEPOUT_U:
    setbit(reg_fdc_status, FDC_ST1_BUSY);
    return fdc_start(uptr,FDC_WAIT_STEP);
    
  /* type II commands */
  case FDC_READSEC:
  case FDC_READSEC_M:
    curdrv->dr_sec = reg_fdc_sector; /* sector to start */
    setbit(reg_fdc_status, FDC_ST2_BUSY);
    return fdc_start(uptr,FDC_WAIT_READ);
  case FDC_WRITESEC:
  case FDC_WRITESEC_M:
    curdrv->dr_sec = reg_fdc_sector; /* sector to start */
    setbit(reg_fdc_status, FDC_ST2_BUSY);
    return fdc_start(uptr,FDC_WAIT_WRITE);

  /* type III commands */ 
  default:
    sim_printf("fdc_docmd: Fix me - command not yet implemented: cmd=0x%x\n", reg_fdc_cmd);
    setbit(reg_fdc_status, FDC_ST2_BUSY);
    return SCPE_NOFNC;
    
  /* type IV command */
   case FDC_FORCEINT:
     if (isbitset(data,0x01)) { /* int on transition from not-ready to ready */
       fdc_stop(uptr);
     } else if (isbitset(data,0x06)) { /* int on transition from ready to not-ready, or vice versa */
       /* handle in fdc_detach */
       fdc_intpending |= reg_fdc_cmd;
       return SCPE_OK;
     } else if (isbitset(data,0x08)) { /* immediate int */
       dma_reqinterrupt();
       return SCPE_OK; /* don't reset BUSY */
     } else { /* terminate */
       fdc_stop(uptr);
       /* successful cmd clears errors */
       clrbit(reg_fdc_status,FDC_ST2_TYPEWFLT|FDC_ST2_RECNOTFND|FDC_ST2_CRCERROR|FDC_ST2_LOSTDATA);
     }
     /* reset busy bit */
     clrbit(reg_fdc_status,FDC_ST1_BUSY);
  }
  return SCPE_OK;
}

void dma_docmd(uint16 data) {
  reg_dma_ctrl = data & 0xff;
  reg_dma_status &= 0x8f;
  reg_dma_status |= (reg_dma_ctrl & 0x70);
  
  if (isbitset(reg_dma_ctrl,DMA_CTRL_RUN))
    setbit(reg_dma_status,DMA_ST_BUSY);
}

/* setup FDC/DMA to read first track into low memory */
t_stat fdc_autoload(int unitnum) {
  int unitbit = 1 << unitnum;
  sim_debug(DBG_FD_CMD, &fdc_dev, DBG_PCFORMAT2 "Autoload Unit=%d\n", DBG_PC, unitnum);
  dma_isautoload = TRUE;

  /* note: this is partly in microcode/ROM. The DMA cntrlr itself does not set the
   * FDC register for multi_read */
  fdc_reset(&fdc_dev);
  dma_docmd(DMA_CTRL_RUN|DMA_CTRL_DIE|DMA_CTRL_TCIE|DMA_CTRL_IOM|DMA_CTRL_HBUS|DMA_CTRL_AECE);

  reg_fdc_drvsel = FDC_SEL_SDEN | unitbit;
  return fdc_docmd(FDC_READSEC_M);
}

static t_bool fd_reg16bit[] = {
  FALSE,FALSE,FALSE,FALSE,
  TRUE, TRUE, TRUE, TRUE,
  FALSE,FALSE,FALSE,FALSE,
  FALSE,FALSE,FALSE,FALSE
};

t_stat fdc_write(t_addr ioaddr, uint16 data) {
  int io = ioaddr & 15;
  sim_debug(DBG_FD_WRITE, &fdc_dev, DBG_PCFORMAT0 "%s write %04x to IO=$%04x\n", 
    DBG_PC, fd_reg16bit[io] ? "Byte":"Word", data, ioaddr);
  switch (io) {
  case 4: /* cmd + drvsel */
    reg_fdc_drvsel = (data >> 8) & 0xff;
  case 0: /* cmd writeonly */
    fdc_docmd(data);
    break;
  case 5: /* track + drvsel */
    reg_fdc_drvsel = (data >> 8) & 0xff;
  case 1: /* track */
    reg_fdc_track = data & 0xff;
    break;
  case 6: /* sector + drvsel */
    reg_fdc_drvsel = (data >> 8) & 0xff;
  case 2: /* sector */
    reg_fdc_sector = data & 0xff;
    break;
  case 7: /* data + drvsel */
    reg_fdc_drvsel = (data >> 8) & 0xff;
  case 3: /* data */
    reg_fdc_data = data & 0xff;
    break;
  case 8: /* dma ctrl */
    dma_docmd(data);
    break;
  case 9: /* dma status */
    if (isbitset(reg_dma_status,DMA_ST_BUSY))
      sim_printf("Warning: DMA: write status while BUSY\n");
    reg_dma_status = data & 0x8f;
    break;
  case 0x0a: /* count low */
    reg_dma_cntl = data & 0xff;
    break;
  case 0x0b: /* count high */
    reg_dma_cnth = data & 0xff;
    break;
  case 0x0c: /* addr low */
    reg_dma_addrl = data & 0xff;
    break;
  case 0x0d: /* addr high */
    reg_dma_addrh = data & 0xff;
    break;
  case 0x0e: /* addr ext */
    reg_dma_addre = data & 0x03;
    break;
  case 0x0f: /* ID register */
    reg_dma_id = data & 0xff;
    break;
  }
  _reg_dma_cnt = (reg_dma_cnth << 8) | reg_dma_cntl;
  if (_reg_dma_cnt) clrbit(reg_dma_status,DMA_ST_TCZI);
  _reg_dma_addr = (((uint32)reg_dma_addre)<<16) | (((uint32)reg_dma_addrh)<<8) | reg_dma_addrl;

  (void)fdc_select();
  return SCPE_OK;
}

t_stat fdc_read(t_addr ioaddr, uint16 *data) {
  switch (ioaddr & 15) {
  case 0: /* status readonly */
  case 4:
    *data = reg_fdc_status;
    break;
  case 1: /* track */
  case 5:
    *data = reg_fdc_track;
    break;
  case 2: /* sector */
  case 6:
    *data = reg_fdc_sector;
    break;
  case 3: /* data */
  case 7:
    *data = reg_fdc_data;
    break;
  case 8: /* read nothing */
    *data = 0; 
    break;
  case 9:
    *data = reg_dma_status;
    break;
  case 0x0a: /* byte low */
    *data = reg_dma_cntl;
    break;
  case 0x0b:
    *data = reg_dma_cnth;
    break;
  case 0x0c:
    *data = reg_dma_addrl;
    break;
  case 0x0d:
    *data = reg_dma_addrh;
    break;
  case 0x0e:
    *data = reg_dma_addre;
    break;
  default: /* note: ID register 0xfc3f is unusable because RE is tied to VCC */
    *data = reg_dma_id;
    break;
  }
  sim_debug(DBG_FD_READ, &fdc_dev, DBG_PCFORMAT1 "Byte read %02x from IO=$%04x\n",
    DBG_PC, *data, ioaddr);
  return SCPE_OK;
}

/*
 * Create an ImageDisk (IMD) file.  This function just creates the comment header, and allows
 * the user to enter a comment.  After the IMD is created, it must be formatted with a format
 * program on the simulated operating system, ie CP/M, CDOS, 86-DOS.
 *
 * If the IMD file already exists, the user will be given the option of overwriting it.
 */
t_stat pdq3_diskCreate(FILE *fileref, const char *ctlr_comment) {
    DISK_INFO *myDisk = NULL;
    char *comment;
    char *curptr;
    uint8 answer;
    int32 len, remaining;

    if(fileref == NULL) {
        return (SCPE_OPENERR);
    }

    if(sim_fsize(fileref) != 0) {
        sim_printf("PDQ3_IMD: Disk image already has data, do you want to overwrite it? ");
        answer = getchar();

        if((answer != 'y') && (answer != 'Y')) {
            return (SCPE_OPENERR);
        }
    }

    if((curptr = comment = (char *)calloc(1, MAX_COMMENT_LEN)) == 0) {
        sim_printf("PDQ3_IMD: Memory allocation failure.\n");
        return (SCPE_MEM);
    }

    sim_printf("PDQ3_IMD: Enter a comment for this disk.\n"
           "PDQ3_IMD: Terminate with a '.' on an otherwise blank line.\n");
    remaining = MAX_COMMENT_LEN;
    do {
        sim_printf("IMD> ");
        fgets(curptr, remaining - 3, stdin);
        if (strcmp(curptr, ".\n") == 0) {
            remaining = 0;
        } else {
            len = strlen(curptr) - 1;
            if (curptr[len] != '\n')
                len++;
            remaining -= len;
            curptr += len;
            *curptr++ = 0x0d;
            *curptr++ = 0x0a;
        }
    } while (remaining > 4);
    *curptr = 0x00;

    /* rewind to the beginning of the file. */
    rewind(fileref);

    /* Erase the contents of the IMD file in case we are overwriting an existing image. */
    sim_set_fsize(fileref, ftell (fileref));

    fprintf(fileref, "IMD SIMH %s %s\n", __DATE__, __TIME__);
    fputs(comment, fileref);
    free(comment);
    fprintf(fileref, "%s\n", ctlr_comment);
    fputc(0x1A, fileref); /* EOF marker for IMD comment. */
    fflush(fileref);

    if((myDisk = diskOpen(fileref, 0)) == NULL) {
        sim_printf("PDQ3_IMD: Error opening disk for format.\n");
        return(SCPE_OPENERR);
    }

    if(pdq3_diskFormat(myDisk) != SCPE_OK) {
        sim_printf("PDQ3_IMD: error formatting disk.\n");
    }

    return diskClose(&myDisk);
}

t_stat pdq3_diskFormat(DISK_INFO *myDisk) {
    uint8 i = 0;
    uint8 sector_map[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
    uint32 flags;

    sim_printf("PDQ3_IMD: Formatting disk in PDQ3 format.\n");

    /* format first track as 26 sectors with 128 bytes */
    if((trackWrite(myDisk, 0, 0, 26, 128, sector_map, IMD_MODE_500K_FM, 0xE5, &flags)) != 0) {
      sim_printf("PDQ3_IMD: Error formatting track %d\n", i);
      return SCPE_IOERR;
    }    
    sim_printf(".");

    /* format the remaining tracks as 26 sectors with 256 bytes */
    for(i=1;i<77;i++) {
        if((trackWrite(myDisk, i, 0, 26, 256, sector_map, IMD_MODE_500K_MFM, 0xE5, &flags)) != 0) {
            sim_printf("PDQ3_IMD: Error formatting track %d\n", i);
            return SCPE_IOERR;
        } else {
            putchar('.');
        }
    }

    sim_printf("\nPDQ3_IMD: Format Complete.\n");
    return SCPE_OK;
}
