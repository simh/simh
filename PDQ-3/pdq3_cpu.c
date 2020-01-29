/*
   Work derived from Copyright (c) 2004-2012, Robert M. Supnik
   PDQ-3 related code Copyright (c) 2013 Holger Veit

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
   
   2013xxxx hv initial version (written up to the level to be tested against bootloader)
   20130826 hv fix problem in SPR(-1): taskswitch mustn't modify sp afterwards
   20130901 hv CXG didn't store correct callers seg# in MSCW
   20130903 hv fix wrong stackspace calculation in createmscw
   20130908 hv Segment refcount was not decremented correctly in RPU
   20130914 hv Degredation in SRS instruction
   20130914 hv SPR didn't set registers correctly; mustn't adjust modified SP again
   20130921 hv Rewrite of memory/io handling to allow adding of QBUS devices,
               prevent buserror by cpu_ex prefetch and attempts to write to HDT rombased TIB
   20130921 hv WAIT/SIGNAL enque used the wrong queue address
   20130927 hv Defect in WAIT overwriting semaphore queue with its own address
   20131012 hv Previous fix was wrong: WAIT mustn't store qhead back unless it changed
   20131019 hv WAIT doesn't always discard argument, error in specification
   20131019 hv SIGNAL has same problem as WAIT when storing back qhead
   20131103 hv Interrupt idle loop didn't process simh queue
   20131110 hv A really hard one: INT 3 (RCV CONSOLE) incremented waiter sema, because
               interrupt changed reg_intpending within a WAIT. Need to latch interrupt before
               execution and process afterwards
   20141003 hv compiler suggested warnings (vc++2013, gcc)
*/

#include "pdq3_defs.h"
#include <math.h>

/* some simulator publics */
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_boot(int32 unitnum, DEVICE *dptr);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_size (FILE *st, UNIT *uptr, int32 val, const void *desc);

/* some forwards */
static t_stat Raise(uint16 err);
static uint16 Pop();
static void Push(uint16 val);
static int16 PopS();
static void PushS(int16 val);
static uint16 createMSCW(uint16 ptbl, uint8 procno, uint16 stat, uint8 segno, uint16 osegb);
static uint16 enque(uint16 qhead, uint16 qtask); /* return new qhead */
static uint16 deque(uint16 qhead, uint16 *qtask); /* return tail of queue */
static t_stat DoSIGNAL(uint16 sem);
static uint16 Get(t_addr addr);
static void Put(t_addr addr, uint16 val);
static uint16 GetSIB(uint8 segno);
static t_stat cpu_set_flag(UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat cpu_set_noflag(UNIT *uptr, int32 value, CONST char *cptr, void *desc);

static t_stat ssr_read(t_addr ioaddr, uint16 *data);
static t_stat ssr_write(t_addr ioaddr, uint16 data);
static t_stat ses_read(t_addr ioaddr, uint16 *data);
static t_stat cpu_readserial(t_addr dummy, uint16 *data);
static t_stat rom_baseread(t_addr dummy, uint16 *data);
static t_stat rom_ignore(t_addr ea, uint16 data);

/* the CPU registers */
uint16 reg_ipc; /* point to current instruction within reg_segb segment */
uint16 reg_sp;
uint16 reg_splow;
uint16 reg_spupr;
uint16 reg_mp;
uint16 reg_bp;
uint16 reg_segb; /* point to current code segment */
uint16 reg_ctp;
uint16 reg_rq;
uint16 reg_ssv;
uint16 reg_lm;
uint16 reg_lsv;
uint32 reg_intpending;
uint32 reg_intlatch;
uint16 reg_ssr = 0; /* system status register */
uint16 reg_ses = 0; /* system environment switch */
uint16 reg_cpuserial = 0; /* CPU serial number */

/* PC address of currently executed instruction */
t_addr PCX;

uint16 reg_fc68 = 0;    /* location of HDT boot ROM */
uint16 reg_romsize = 0; /* size of HDT boot ROM */

/* possible hack to enforce DMA being initialized to 0x2000 (word address)
 * the boot code from Don Maslin's PDQ-3 floppy implies it is run from 0x2000 */
uint32 reg_dmabase = 0x2000; 

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, MEMSIZE) };
REG cpu_reg[] = {
  /* must be at location 0: This is to display the combined segb:ipc address,
     refers to start of currently executed instruction. Refer to STATE to
     see actual IPC value */
  { HRDATA (PC,     PCX, 32), REG_RO|REG_HIDDEN },
  
  { HRDATA (SEGB,   reg_segb, 16) },
  { HRDATA (IPC,    reg_ipc, 16) },

  { HRDATA (SP,     reg_sp, 16) },
  { HRDATA (SPLOW,  reg_splow, 16) },
  { HRDATA (SPUPR,  reg_spupr, 16) },

  { HRDATA (MP,     reg_mp, 16) },
  { HRDATA (BP,     reg_bp, 16) },
  { HRDATA (CTP,    reg_ctp, 16) },
  { HRDATA (RQ,     reg_rq, 16) },
  { HRDATA (SSV,    reg_ssv, 16) },

  { HRDATA (_LM,    reg_lm, 16), REG_HIDDEN },
  { HRDATA (_LSV,   reg_lsv, 16), REG_HIDDEN },
  { HRDATA (_SSR,   reg_ssr, 8), REG_HIDDEN },
  { HRDATA (_SES,   reg_ses, 8), REG_HIDDEN },
  { HRDATA (_INT,   reg_intpending, 32), REG_HIDDEN }, 
  { HRDATA (_FC68,  reg_fc68, 16 ), REG_RO|REG_HIDDEN },
  { HRDATA (_INITLOC,reg_dmabase, 17 ), REG_RO|REG_HIDDEN },

  { HRDATA (_ROMSZ, reg_romsize, 16 ), REG_RO|REG_HIDDEN },
  { HRDATA (_CPUSERIAL, reg_cpuserial, 16), REG_HIDDEN },

  { NULL }
};
MTAB cpu_mod[] = {
  { UNIT_MSIZE,                  0,          NULL,     "32K",   &cpu_set_size,         NULL },
  { UNIT_MSIZE,                  1,          NULL,     "64K",   &cpu_set_size,         NULL },
  { UNIT_PASEXC,       UNIT_PASEXC, "halt on EXC",     "EXC",   &cpu_set_flag,         NULL },
  { UNIT_PASEXC,                 0,      "no EXC",      NULL,            NULL,         NULL },
  { MTAB_XTD|MTAB_VDV, UNIT_PASEXC,          NULL,   "NOEXC", &cpu_set_noflag,         NULL },
  { MTAB_XTD|MTAB_VDV,           0,      "IOBASE",  "IOBASE",            NULL, &show_iobase },
  { MTAB_XTD|MTAB_VDV,           0,      "VECTOR",  "VECTOR",            NULL,  &show_iovec },
  { MTAB_XTD|MTAB_VDV,           0,        "PRIO",    "PRIO",            NULL, &show_ioprio },
  { 0 }
};

DEBTAB cpu_dflags[] = {
  { "INT",   DBG_CPU_INT },
  { "INT2",  DBG_CPU_INT2 },
  { "WRITE", DBG_CPU_WRITE },
  { "READ",  DBG_CPU_READ },
  { "FETCH", DBG_CPU_FETCH },
  { "STACK", DBG_CPU_STACK },
  { "CONC",  DBG_CPU_CONC },
  { "CONC2", DBG_CPU_CONC2 },
  { "CONC3", DBG_CPU_CONC3 },
  { 0, 0 }
};

IOINFO cpu_ioinfo1 = { NULL,         SSR_IOBASE,   1,        SES_BERR_VEC, 0,  ssr_read, ssr_write };
IOINFO cpu_ioinfo2 = { &cpu_ioinfo1, SES_IOBASE,   1,        SES_PWRF_VEC, 1,  ses_read, rom_ignore };
IOINFO cpu_ioinfo3 = { &cpu_ioinfo2, ROM_BASE,     1,        0,            -1, rom_baseread, rom_ignore };
IOINFO cpu_ioinfo4 = { &cpu_ioinfo3, ROM,          ROM_SIZE, 0,            -1, rom_read, rom_ignore };
IOINFO cpu_ioinfo5 = { &cpu_ioinfo4, CPU_SERIALNO, 1,        0,            -1, cpu_readserial, rom_ignore };
DEVCTXT cpu_ctxt = { &cpu_ioinfo5 };

DEVICE cpu_dev = {
  "CPU",       /*name*/
  &cpu_unit,   /*units*/
  cpu_reg,     /*registers*/
  cpu_mod,     /*modifiers*/
  1,           /*numunits*/
  16,          /*aradix*/
  32,          /*awidth*/
  1,           /*aincr*/
  16,          /*dradix*/
  16,          /*dwidth*/
  &cpu_ex,     /*examine*/
  &cpu_dep,    /*deposit*/
  &cpu_reset,  /*reset*/
  &cpu_boot,   /*boot*/
  NULL,        /*attach*/
  NULL,        /*detach*/
  &cpu_ctxt,   /*ctxt*/
  DEV_DEBUG,   /*flags*/
  0,           /*dctrl*/
  cpu_dflags,  /*debflags*/
  NULL,        /*msize*/
  NULL         /*lname*/
};

/* return start address of proctbl of current code segment */
static uint16 GetPtbl() {
  uint16 ptbl;
  Read(reg_segb, 0, &ptbl, DBG_NONE);
  return reg_segb + ptbl;
}

/* return segment base of segment */
static uint16 GetSegbase(uint8 segno) {
  uint16 data, sib;
  sib = GetSIB(segno);
  Read(sib,OFF_SEGBASE, &data, DBG_NONE);
  return data;
}

/* get segment# from code segment:
 * this is the first byte of the proctbl at the end of the code segment 
 *(the second byte is the proc count) */
static uint8 GetSegno() {
  uint16 data;
  uint16 ptbl = GetPtbl();
  ReadB(ptbl, 0, &data, DBG_NONE); /* get first byte from proctbl */
  return (uint8)data;
}

/* set SEGB and return address of proc tbl (optimization for segb + segb[0] ) */
static uint16 SetSEGB(uint8 segno) {
  /* obtain pointer to SIB for segno */
  uint16 sib = GetSIB(segno); 

  /* set SEGB and get pointer to proc tbl */
  Read(sib, OFF_SEGBASE, &reg_segb, DBG_NONE);
  return GetPtbl();
}

static void AdjustRefCount(uint8 segno, int incr) {
  uint16 sib = GetSIB(segno);
  uint16 ref = Get(sib + OFF_SEGREFS);
  Put(sib + OFF_SEGREFS,  ref + incr);
  //sim_printf("ref(%x) %s = %d\n",segno,incr>0 ? "increment":"decrement", ref+incr);  
}

/* save CPU regs into TIB */
static void save_to_tib() {
  Write(reg_ctp, OFF_SP, reg_sp, DBG_NONE);
  Write(reg_ctp, OFF_MP, reg_mp, DBG_NONE);
  Write(reg_ctp, OFF_BP, reg_bp, DBG_NONE);
  Write(reg_ctp, OFF_IPC, reg_ipc, DBG_NONE);
  Write(reg_ctp, OFF_SEGB, reg_segb, DBG_NONE);
}

/* restore CPU regs from TIB */
static void restore_from_tib() {
  Read(reg_ctp, OFF_SP, &reg_sp, DBG_NONE);
  Read(reg_ctp, OFF_SPLOW, &reg_splow, DBG_NONE);
  Read(reg_ctp, OFF_SPUPR, &reg_spupr, DBG_NONE);
  Read(reg_ctp, OFF_MP, &reg_mp, DBG_NONE);
  Read(reg_ctp, OFF_BP, &reg_bp, DBG_NONE);
  Read(reg_ctp, OFF_IPC, &reg_ipc, DBG_NONE);
  Read(reg_ctp, OFF_SEGB, &reg_segb, DBG_NONE);
}

/* initialize registers for boot */
void cpu_setRegs(uint16 newctp, uint16 newssv, uint16 newrq) 
{
  reg_ctp = newctp;
  reg_ssv = newssv;
  reg_rq = newrq;
  restore_from_tib();
//  if (sim_deb) dbg_dump_tib(sim_deb, reg_ctp);

  /* initialize the simh PC */
  PCX = MAKE_BADDR(reg_segb,reg_ipc);
}

/* this is a dummy routine to ignore invalid writes to the ROM 
 * which occur during context switch from HDT to boot loader */
static t_stat rom_ignore(t_addr ea, uint16 data) {
  return SCPE_OK;
}

/* this is the central point to explain the various methods of booting.
 * 1. boot from ROM:
 *    if (0xfc68) == 0 then try method 2
 *    else 
 *      MR := (0xfc68)
 *      CTP := (MR) -> set TIB
 *      SSV := (MR+1) -> segment dictionary pointer
 *      RQ := (MR+2) -> request queue
 *      SEGB := CTP->segb
 *      load SP, MP, BP, IPC from TIB
 *      run
 * 2. boot from floppy:
 *    load first track via autoload at reg_dmabase
 *    CTP := reg_dmabase
 *    if CTP->sibvec == NIL then
 *      SSV := unknown
 *      RQ := CTP->waitq
 *      SEGB := CTP->segb
 *      load SP, MP, BP, IPC from TIB
 *    else
 *      SSV := CTP->sibvec
 *      RQ := CTP->waitq
 *      SEGB := ((SEGB)) // double deref
 *      load SP, MP, BP, IPC from TIB
 *      run
 *
 * This is not fully compliant with W9693_PasIII_OSRef_Jul82, but matches
 * the different boot sectors I found.
 */
t_stat cpu_boot(int32 unitnum, DEVICE *dptr) {
  t_stat rc;
  uint16 ctp, ssv, rq;
//  sim_printf("BOOT CPU\n");
  cpu_reset(dptr);
  dbg_init();

  /* boot from external ROM? */
  if (reg_fc68 != 0) {
//    sim_printf("Booting from HDT ROM\n");
    /* cf. WD9593_PasIII_OSRef_Jul82.pdf */
    Read(reg_fc68, 0, &ctp, DBG_NONE);
    Read(reg_fc68, 1, &ssv, DBG_NONE);
    Read(reg_fc68, 2, &rq, DBG_NONE);
    cpu_setRegs(ctp, ssv, rq);
  } else {
    /* autoload the 1st track into meory at reg_dmabase */
    if ((rc = fdc_boot(0, &fdc_dev)) != SCPE_OK) return rc;
  } 
  return SCPE_OK;
}

void cpu_finishAutoload() {
  uint16 ssv, rq, sbase;
  uint16 ctp = reg_dmabase;
  Read(ctp, OFF_SIBS, &ssv, DBG_NONE);
  Read(ctp, OFF_WAITQ, &rq, DBG_NONE);
  cpu_setRegs(ctp, ssv, rq);
  if (ssv != NIL) {
    Read(reg_segb, 0, &sbase, DBG_NONE);    /* reg_segb is a pointer into sibvec, sbase points to SIB */
    Read(sbase, OFF_SEGBASE, &reg_segb, DBG_NONE); /* reg_segb is segbase from SIB entry */
  }
}

/* CPU reset */
t_stat cpu_reset (DEVICE *dptr) {
  extern void pdq3_vm_init (void);
  pdq3_vm_init();
  //  sim_printf("CPU RESET\n");
  sim_brk_types = SWMASK('E')|SWMASK('R')|SWMASK('W');
  sim_brk_dflt = SWMASK('E');
  
  /* initialize I/O system, and register standard IO handlers */
  pdq3_ioinit();
  add_ioh(((DEVCTXT*)dptr->ctxt)->ioi);

  /* reset important CPU registers */
  reg_ctp = NIL;
  reg_intpending = 0;
  reg_intlatch = 0;
  PCX = reg_ipc = 0;

  return SCPE_OK;
}

/* Memory examine */
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw) {
  uint16 data;
  t_addr off = ADDR_OFF(addr);
  t_addr seg = ADDR_SEG(addr);
  if (seg==0) seg = NIL;
  addr = MAKE_BADDR(seg,off);

//  sim_printf("Examine: addr=%x seg=%x off=%x\n",addr,seg,off);
//  sim_printf("sw=%x, isword=%d\n",sw, ADDR_ISWORD(addr));
  if (ADDR_ISWORD(addr) || (sw & SWMASK('W'))) {
//                        sim_printf("addr=%x seg=%x off=%x\n",addr,seg,off);
    if (off >= memorysize ||
        ReadEx(off, 0, &data) != SCPE_OK) return SCPE_IOERR;
  } else if (!ADDR_ISWORD(addr) || (sw & SWMASK('B'))) {
    if ((seg*2 + off) >= (2*memorysize) || 
        ReadBEx(seg, off, &data) != SCPE_OK) return SCPE_IOERR;
  }
  if (vptr)
    *vptr = data;
  return SCPE_OK;
}

/* Memory deposit */
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
  t_addr off = ADDR_OFF(addr);
  t_addr seg = ADDR_SEG(addr);
  if (seg==0) seg = NIL;
  addr = MAKE_BADDR(seg,off);
  
  if (ADDR_ISWORD(addr) || (sw & SWMASK('W'))) {
    if (off >= memorysize || 
        Write(off, 0, val, 0) != SCPE_OK) return SCPE_ARG;
  } else {
    if (!ADDR_ISWORD(addr) || (sw & SWMASK('B'))) {
      if ((seg*2 + off) >= (2*memorysize) || 
          WriteB(seg, off, val, 0)) return SCPE_ARG;
    }
  }
  return SCPE_OK;
}

t_stat cpu_buserror() {
  reg_ssr |= SSR_BERR;
  return cpu_raiseInt(INT_BERR);
}

static t_stat ssr_read(t_addr ioaddr, uint16 *data) {
  *data = reg_ssr & ~(SSR_PRNT|SSR_BIT3);
  return SCPE_OK;
}

static t_stat ssr_write(t_addr ioaddr, uint16 data) {
  if (isbitset(data,SSR_BERR)) {
    clrbit(reg_ssr,SSR_BERR);
    sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Clear BERR\n", DBG_PC);
  }
  if (isbitset(data,SSR_TICK)) {
    clrbit(reg_ssr,SSR_TICK);
    sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Acknowledge TICK\n", DBG_PC);
 }
  if (isbitset(data,SSR_INTVL)) {
    clrbit(reg_ssr,SSR_INTVL);
    sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Acknowledge INTVL\n", DBG_PC);
 }
  if (isbitset(data,SSR_BIT3))
    sim_printf("Warning: Attempt to set SSR bit 3\n");
  if (isbitset(data,SSR_PWRF)) {
    clrbit(reg_ssr,SSR_PWRF);
    sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Acknowledge PWRF\n", DBG_PC);
  }
  clrbit(reg_ssr,SSR_PRNT|SSR_INTEN);
  setbit(reg_ssr,data & (SSR_PRNT|SSR_INTEN));
  sim_debug(DBG_CPU_INT, &cpu_dev, DBG_PCFORMAT2 "%sable Interrupt system\n",
    DBG_PC, isbitset(reg_ssr,SSR_INTEN) ? "En" : "Dis");

  if (data & SSR_INIT) {
    sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Bus Reset BINIT\n", DBG_PC);
    /* @TODO: send binit also to a future HDC */
    fdc_binit();
    con_binit();
  }
  return SCPE_OK;
}

static t_stat ses_read(t_addr ioaddr, uint16 *data)
{
  *data = reg_ses;
//  sim_printf("ses is %x\n",reg_ses);
  return SCPE_OK;
}

static t_stat cpu_readserial(t_addr dummy, uint16 *data)
{
  *data = reg_cpuserial;
  return SCPE_OK;
}

static t_stat rom_baseread(t_addr dummy, uint16 *data)
{
  *data = reg_fc68;
  return SCPE_OK;
}

/*************************************************************************************
 * Interrupt handling
 ************************************************************************************/
static uint16 int_vectors[32] = {
  0x0002, /* INT_BERR */
  0x0006, /* INT_PWRF */
  0x000a, /* INT_DMAFD */
  0x000e, /* INT_CONR */
  0x0012, /* INT_CONT */
  0x0016, /* INT_PRNT */
  0x001a, /* INT_SCLK */
  0x001e, /* INT_INTVL */
  NIL
};

t_bool cpu_isIntEnabled() {
  return reg_ssr & SSR_INTEN;
}

/* latch interrupts */
void cpu_assertInt(int level, t_bool tf) {
  uint16 bit = 1 << level;
  if (tf)
    setbit(reg_intlatch, bit);
  else
    clrbit(reg_intlatch, bit);
  sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "%sssert Interrupt Level %d\n",
            DBG_PC, tf?"A":"Dea", level);    
}

t_stat cpu_raiseInt(int level) {
  if (level > 15) {
    sim_printf("Implementation error: raiseInt with level>15! Need fix\n");
    exit(1);
  }
  
  if (!cpu_isIntEnabled()) return STOP_ERRIO; /* interrupts disabled, or invalid vector */
  
  cpu_assertInt(level, TRUE);
  return SCPE_OK;
}

static void cpu_ackInt(int level) {
  uint16 bit = 1<<level;
  clrbit(reg_intpending, bit);

  /* disable SSR_INTEN */
  sim_debug(DBG_CPU_INT2, &cpu_dev, DBG_PCFORMAT1 "Ack interrupt level %d\n",
    DBG_PC, level);
    
  clrbit(reg_ssr, SSR_INTEN);
}

t_stat cpu_setIntVec(uint16 vec, int level) {
  if (level <0 || level >31) return SCPE_ARG;
  int_vectors[level] = vec;
  return SCPE_OK;
}

static int getIntLevel() {
  int i;
  uint32 bit = 1;
  for (i=0; i<31; i++) {
    if (reg_intpending & bit) return i;
    bit <<= 1;
  }
  return -1;
}

static t_stat cpu_processInt() {
  int level = getIntLevel(); /* obtain highest pending interupt */
  uint16 vector, sem;
  t_stat rc;
  
  if (level == -1) return SCPE_OK; /* don't signal: spurious interrupt */

  vector = int_vectors[level];
  if (vector == NIL) return SCPE_OK;

  save_to_tib(); /* save current context into ctp */
  reg_rq = enque(reg_rq, reg_ctp); /* put current task into rq queue */

  reg_ctp = NIL; /* set no active task (marker for int processing in SIGNAL) */
  sem = Get(vector); /* get semaphore from interrupt vector */
  sim_debug(DBG_CPU_INT, &cpu_dev, DBG_PCFORMAT2 "processInt: level=%d vector=$%04x sema=$%04x\n",
            DBG_PC,level,vector,sem);

  cpu_ackInt(level); /* acknowledge this interrupt */
  rc = DoSIGNAL(sem); /* process SIGNAL, i.e. check semaphore at interrupt vector */
  return rc;
}

/*************************************************************************************
 * instruction interpreter
 ************************************************************************************/

static uint8 UB() {
  uint16 val;
  ReadB(reg_segb, reg_ipc++, &val, DBG_CPU_FETCH);
  return val & 0xff;
}
static uint16 W() {
  uint16 high, data;
  if (ReadB(reg_segb, reg_ipc++, &data, DBG_CPU_FETCH) != SCPE_OK)
    return data;
  if (ReadB(reg_segb, reg_ipc++, &high, DBG_CPU_FETCH) != SCPE_OK)
    return high;
  data |= (high << 8);
  return data;
}
static uint16 DB() {
  return UB();
}
static uint16 SB() {
  uint16 data;
  ReadB(reg_segb, reg_ipc++, &data, DBG_CPU_FETCH);
  if (data & 0x80) data |= 0xff80;
  return data;
}
static uint16 B() {
  uint16 high, data;
  if (ReadB(reg_segb, reg_ipc++, &high, DBG_CPU_FETCH) != SCPE_OK)
    return high;
  if (high & 0x80) {
    if (ReadB(reg_segb, reg_ipc++, &data, DBG_CPU_FETCH) != SCPE_OK)
      return high;
    data |= ((high & 0x7f) << 8);
    return data;
  } else
    return high;
}
static void Put(t_addr addr, uint16 val) {
  Write(0, addr, val, DBG_CPU_WRITE);
}
static uint16 Get(t_addr addr) {
  uint16 val;
  Read(0, addr, &val, DBG_CPU_READ);
  return val;
}

static void Putb(t_addr base, t_addr idx, uint16 val) {
  WriteB(base, idx, val, DBG_CPU_WRITE);
}

static uint8 Getb(t_addr addr,t_addr idx) {
  uint16 val;
  ReadB(addr, idx, &val, DBG_CPU_READ);
  return val & 0xff;
}

static uint16 TraverseMSstat(uint16 db) {
  int i;
  uint16 lm = reg_mp;
  for(i=1; i<=db; i++) lm = Get(lm + OFF_MSSTAT);
  return lm;
}

static uint16 Tos() {
  uint16 val;
  if (reg_sp >= reg_spupr) { Raise(PASERROR_STKOVFL); return 0; }
  Read(0,reg_sp,&val, DBG_CPU_PICK);
  return val;
}

static uint16 Pick(int i) {
  uint16 val;
  if ((reg_sp+i) >= reg_spupr) { Raise(PASERROR_STKOVFL); return 0; }
  Read(0,reg_sp+i,&val, DBG_CPU_PICK);
  return val;
}

static uint16 Pop() {
  uint16 val;
  if ((reg_sp+1) > reg_spupr) { Raise(PASERROR_STKOVFL); return 0; }
  Read(0,reg_sp++,&val, DBG_CPU_POP);
  return val;
}

static void Push(uint16 val) {
  if (reg_sp < reg_splow) Raise(PASERROR_STKOVFL);
  else
    Write(0,--reg_sp,val,DBG_CPU_PUSH);
}

static int16 PopS() {
  return (int16)Pop();
}

static void PushS(int16 val) {
  Push((uint16)val);
}

static float PopF() {
  T_FLCVT t;
  t.i[1] = Pop();
  t.i[0] = Pop();
//  sim_printf("POPF: %.6e\n",t.f);
  return t.f;
};

static void PushF(float f) {
  T_FLCVT t;
  t.f = f;
//  sim_printf("PUSHF: %.6e\n",t.f);
  Push(t.i[0]);
  Push(t.i[1]);
}

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

static uint16 masks[] = {
0x0000,
0x0001, 0x0003, 0x0007, 0x000f,
0x001f, 0x003f, 0x007f, 0x00ff,
0x01ff, 0x03ff, 0x07ff, 0x0fff,
0x1fff, 0x3fff, 0x7fff, 0xffff
};
/* to produce a mask for a bit field <start:nbits>, 
 * e.g. <3:5> 0000000011111000 == 0x00f8
 */
static uint16 GetMask(int lowbit,int nbits) {
  return masks[nbits] << lowbit;
}

/* get address of SIB entry of segment */
static uint16 GetSIB(uint8 segno) {
  return segno < 128 ?
    Get(reg_ssv + segno) :
    Get(Get(reg_ctp + OFF_SIBS) + segno - 128);
}

/* do a CXG instruction into segment SEGNO to procedure procno */
static void DoCXG(uint8 segno, uint8 procno) {
  uint16 ptbl;
  uint8 osegno = (uint8)GetSegno(); /* obtain segment of caller to be set into MSCW */
  uint16 osegb = reg_segb;
  
//  sim_printf("CXG: seg=%d proc=%d, osegno=%d\n",segno,procno,osegno);
  ptbl = SetSEGB(segno); /* get ptbl of new segment */
  AdjustRefCount(segno,1);
  
//  sim_printf("CXG: ptbl=%x, reg_segb=%x\n",ptbl,reg_segb);
  reg_ipc = createMSCW(ptbl, procno, reg_bp, osegno, osegb); /* call new segment */
  sim_interval--;
}

static t_stat Raise(uint16 err) {

  /* HALT on Pascal Exception? */
  if (Q_PASEXC) return STOP_PASEXC;
  
  /* push error code
   * attention: potential double fault: STKOVFL */
  if (err==PASERROR_STKOVFL)
    Write(0,reg_sp,err,TRUE);
  else
    Push(err);
  sim_debug(DBG_CPU_INT, &cpu_dev, DBG_PCFORMAT2 "Raised Pascal Exception #%d\n",DBG_PC,err);
  
  /* call OS trap handler
   * Note: if an exception occurs in boot loader (CHK instruction for CPU serial),
   * this goes to nirvana because HALTUNIT is not yet linked correctly */
  DoCXG(2,2);
  return SCPE_OK;
}

static int GetBit(t_addr base, int bitno)
{
  int wnum = bitno / WORD_SZ;
  int bnum = bitno % WORD_SZ;
  uint16 bitmask = 1 << bnum;
  return (Get(base + wnum) & bitmask) ? 1 : 0;
}

static uint16 createMSCW(uint16 ptbl, uint8 procno, uint16 stat, uint8 segno, uint16 osegb) {
  uint16 procstart = Get(ptbl - procno); /* word index into segment */
  uint16 datasz = Get(reg_segb + procstart); /* word index */
  dbg_segtrack(reg_segb);
//  sim_printf("createMSCW: ptbl=%x procno=%d stat=%x segno=%x\n",ptbl,procno,stat,segno);
  
  if (reg_sp < reg_splow || (datasz+MSCW_SZ) > (reg_sp-reg_splow)) { /* verify enough space on stack */
//    sim_printf("Stk overflow in mscw: sp=%x spl=%x ds=%d dsm=%d sp-spl=%d\n",reg_sp,reg_splow,datasz,datasz+MSCW_SZ, reg_sp-reg_splow);
    Raise(PASERROR_STKOVFL); return reg_ipc;
  }
  reg_sp = reg_sp - MSCW_SZ - datasz; /* allocate space on stack for local data and MSCW */

  reg_lm = reg_mp; /* save old MP */
  reg_mp = reg_sp; /* set MP to new stack base */
  Put(reg_mp+OFF_MSDYNL,reg_lm); /* set the dyn link to point to previous MSCW */
  Put(reg_mp+OFF_MSIPC, reg_ipc); /* save the IPC of the caller, already adjusted to point to next instr */
  Put(reg_mp+OFF_MSSTAT,stat); /* set the static link */
  Put(reg_mp+OFFB_MSSEG, segno); /* set the segment # of the caller */
  dbg_procenter(reg_segb, procno, reg_sp, osegb);
  return (procstart+1) * 2; // new reg_ipc, byte address in segement */
}

/* for context switching */
//uint16 qtask = NIL;

/* put qtask into priority queue */
static uint16 enque(uint16 qhead, uint16 qtask) {
  uint16 t1 = qhead;
  uint16 t2 = NIL;
  uint8 qtaskprio = Getb(qtask+OFFB_PRIOR,0);
  sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Enque: qhead=$%04x qtask=$%04x\n", DBG_PC, qhead, qtask);
  while (t1 != NIL) { /* loop until end of queue found */
    if (Getb(t1+OFFB_PRIOR,0) < qtaskprio) break; /* exit loop if priority less than qitem */
    t2 = t1; t1 = Get(t1+OFF_WAITQ); /* otherwise store pointer to current element, advance to next item */
  }
  /* now t1 points to an item (or NIL) with a prio less than qtask, t2 points to previous item */
  sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Enque: t1=$%04x t2=$%04x\n", DBG_PC, t1, t2);

  Put(qtask+OFF_QLINK, t1); /* append this item */
  if (t2 == NIL) qhead = qtask; /* if no higher prio item present, qtask becomes new head */
  else Put(t2+OFF_QLINK, qtask); /* otherwise prepend higher prio list */
  sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Enqueue: DONE qhead=$%04x qtask=$%04x\n",DBG_PC, qhead, qtask);
  return qhead; /* return the new qhead */
}

/* perform a task switch. If no task ready to run, wait for an interrupt */
static t_stat taskswitch6() {
  uint16 vector, sem;
  int level, kbdc;
  t_stat rc = SCPE_OK;
//  int kbdc;
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "Taskswitch6: ctp=$%04x rq=$%04x\n",DBG_PC, reg_ctp, reg_rq);

  while (reg_rq == NIL) { /* no task ready to run? */
    if (reg_intpending) { /* wait for an interrupt */
      sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Taskswitch6: reg_intpending=%08x\n",DBG_PC, reg_intpending);
      reg_ctp = NIL; /* set no active task */
      level = getIntLevel(); /* obtain highest pending interupt */
      ASSURE(level >= 0); /* won't happen, as reg_intpending is known to be true */
      vector = int_vectors[level];
      sem = Get(vector);
      sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Taskswitch6: SIGNAL sem=$%04x\n",DBG_PC, sem);
      rc = DoSIGNAL(sem);
      return rc;
    } else {
      kbdc = sim_poll_kbd(); /* check keyboard */
      if (kbdc == SCPE_STOP) return kbdc; /* handle CTRL-E */
      /* process timer */
      if (sim_interval <= 0) {
        if ((rc = sim_process_event()) != SCPE_OK)
          return rc;
      }
      sim_idle(TMR_IDLE, TRUE);
    }
  }

  reg_rq = deque(reg_rq, &reg_ctp); /* get first task from ready queue */
  restore_from_tib(); /* restore registers from TIB */
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "Taskswitch6: DONE newTIB=$%04x\n", DBG_PC2, reg_ctp);

  /* continue processing in this context */
  return rc;
}

static t_stat taskswitch5() {
  t_stat rc;
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "Taskswitch5: reg_rq=$%04x\n",DBG_PC, reg_rq);
  save_to_tib(); /* save current context into ctp */
  rc = taskswitch6(); /* and switch to highest task in ready queue */
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "Taskswitch5: DONE\n",DBG_PC2);
  return rc;
}

static uint16 deque(uint16 qhead, uint16* qtask) {
  uint16 newhead;
  *qtask = qhead; /* store first element of queue */
  newhead = Get(qhead+OFF_QLINK); /* discard first element from queue, and return new qhead address */
  sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "Dequeue: qtask=$%04x newhead=$%04x\n",DBG_PC,*qtask,newhead);
  return newhead;
}

static t_stat DoSIGNAL(uint16 sem) {
  t_stat rc = SCPE_OK;
  uint16 qtask, qhead;
  uint16 wqaddr = sem + OFF_SEMWAITQ; /* address of wait queue */
  
  uint16 count = Get(sem+OFF_SEMCOUNT); /* get count value from semaphore*/
  uint16 wait = Get(wqaddr); /* get top of wait queue */
  
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "SIGNAL: Sem=$%x(count=%d wait=$%x)\n",
            DBG_PC,sem,count,wait);
  if (count == 0) {
    if (wait != NIL) { /* queue is not empty */
      qhead = deque(wait, &qtask); /* extract head of queue (qtask), and store tail back */
      Put(wqaddr, qhead); 

      sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "SIGNAL: dequeued qtask=$%x\n",DBG_PC, qtask);
      reg_rq = enque(reg_rq, qtask); /* put qtask into rq queue */
      sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "SIGNAL: reg_rq=$%x, reg_ctp=$%x\n", DBG_PC, reg_rq, reg_ctp);
      
      if (reg_ctp == NIL) { /* no current task (marker for int processing */
        sim_interval--; /* consume time */
        return taskswitch6(); /* and switch task */
      }
      if (Getb(reg_ctp+OFFB_PRIOR,0) < Getb(qtask+OFFB_PRIOR,0)) { /* is qtask higher prio than current task? */
        reg_rq = enque(reg_rq, reg_ctp); /* yes, put current task back into ready queue */
        rc = taskswitch5(); /* save context in TIB, and switch to new task from ready queue */
      } else {
        /* else: nothing is waiting on this semaphore, discard argument, and continue */
        reg_sp++;
        sim_interval--;
      }
      return rc;
    }
  }
  /* count is > 0, or sem has no waiters */
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "SIGNAL: Sem=$%x(count=%d): increment\n",DBG_PC, sem, count);
  Put(sem+OFF_SEMCOUNT,count+1);
  if (reg_ctp == NIL) { /* if no active task, get one from ready queue */
    sim_interval--;
    return taskswitch6(); 
  }
  reg_sp++;
  sim_interval--;
  return rc;
}

static t_stat DoWAIT(uint16 sem) {
  uint16 qhead; 
  uint16 wqaddr = sem + OFF_SEMWAITQ;
  t_stat rc;

  uint16 count = Get(sem + OFF_SEMCOUNT); /* get count of semaphore */
  sim_debug(DBG_CPU_CONC, &cpu_dev, DBG_PCFORMAT1 "WAIT: Sem=$%04x(count=%d)\n",DBG_PC,sem, count);
  if (count == 0) {
//    sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "WAIT: Semaphore %x has count 0: do a task switch\n",DBG_PC,sem);
    
    qhead = enque(Get(wqaddr), reg_ctp); /* have current task wait on semaphore */
    Put(wqaddr, qhead);
//    sim_debug(DBG_CPU_CONC3, &cpu_dev, DBG_PCFORMAT0 "WAIT: new qhead=%x\n",DBG_PC, qhead);
    
    rc = taskswitch5(); /* save context in TIB, and switch to new task from ready queue */
    sim_interval--;
    sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "WAIT: DONE, switch to newTIB=$%04x\n",DBG_PC, reg_ctp);
    return rc;
  } else {
    sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "WAIT: Sem=$%04x(count=%d): decrement\n", DBG_PC, sem, count);
    Put(sem+OFF_SEMCOUNT,count-1);
  }
  sim_interval--;
  sim_debug(DBG_CPU_CONC2, &cpu_dev, DBG_PCFORMAT0 "WAIT: DONE, continue\n",DBG_PC);
  return SCPE_OK;
}

static uint8 HiByte(uint16 reg) {
  return (reg>>8) & 0xff;
}

static uint8 LoByte(uint16 reg) {
  return reg & 0xff;
}

static t_stat DoInstr(void) {
  t_stat rc = SCPE_OK;
  uint16 opcode, db, b, src, dst, inx, len0, len1, hi,lo;
  uint16 t1, t2, t3, t4, t5, min1, max1, ptbl, osegb;
  int16 ts1, ts2, w;
  uint8 ub1, ub2;
  uint8 segno, osegno, procno;
  float tf1, tf2;
  int i;

  /* set PCX: current instr in progress */
  PCX = MAKE_BADDR(reg_segb,reg_ipc);

  /* process breakpoints */
  if (sim_brk_summ && sim_brk_test(PCX, SWMASK('E'))) {
    return STOP_IBKPT;
  }
  
  /* get opcode */
  opcode = UB();
  
  if (dbg_check(opcode, DEBUG_PRE)) {
    reg_ipc = ADDR_OFF(PCX); /* restore PC for potential redo */
    return STOP_DBGPRE;
  }
  
  switch (opcode) {
  case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
  case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
  case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
  case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
    /* SLDCi */
    Push(opcode & 0x1f);
    break;
  case 0x98: /* LDCN */
    Push(NIL);
    break;
  case 0x80: /* LDCB */
    Push(UB());
    break;
  case 0x81: /* LDCI */
    Push(W());
    break;
  case 0x82: /* LCA */
    Push(reg_segb + B());
    break;
  case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
  case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    /* SLDLi */
    Push(Get(reg_mp + MSCW_SZ + (opcode & 0x0f)));
    break;
  case 0x87: /* LDL */
    Push(Get(reg_mp + MSCW_SZ -1 + B()));
    break;
  case 0x84: /* LLA */
    Push(reg_mp + MSCW_SZ -1 + B());
    break;
  case 0xa4: /* STL */
    Put(reg_mp + MSCW_SZ -1 + B(), Pop());
    break;
  case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
  case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
    /* SLDOi */
    Push(Get(reg_bp + MSCW_SZ + (opcode & 0x0f)));
    break;
  case 0x85: /* LDO */
    Push(Get(reg_bp + MSCW_SZ -1 + B()));
    break;
  case 0x86: /* LAO */
    Push(reg_bp + MSCW_SZ -1 + B()); 
    break;
  case 0xa5: /* SRO */
    Put(reg_bp + MSCW_SZ -1 + B(),Pop());
    break;
  case 0x89: /* LOD */
    reg_lm = TraverseMSstat(DB());
    Push(Get(reg_lm + MSCW_SZ -1 + B()));
    break;
  case 0x88: /* LDA */
    reg_lm = TraverseMSstat(DB());
    Push(reg_lm + MSCW_SZ -1 + B()); 
    break;
  case 0xa6: /* STR */
    reg_lm = TraverseMSstat(DB());
    Put(reg_lm + MSCW_SZ -1 + B(),Pop());
    break;
  case 0xc4: /* STO */
    t1 = Pop(); Put(Pop(),t1);
    break;
  case 0x9a: /* LDE */
    t2 = GetSegbase(UB()); Push(Get(t2 + B()));
    break;
  case 0x9b: /* LAE */
    ub1 = UB();
    Push(GetSegbase(ub1) + B());
    break;
  case 0xd9: /* STE */
    ub1 = UB();
    Put(GetSegbase(ub1) + B(), Pop());
    break;
  case 0x83: /* LDC */
    b = B(); ub1 = UB();
    src = reg_segb + b + ub1;
    for (i=1; i<=ub1; i++) Put(reg_sp-i,Get(src-i));
    reg_sp -= ub1; 
    break;
  case 0xd0: /* LDM */
    ub1 = UB(); src = Pop() + ub1;
    for (i=1; i<=ub1; i++) Put(reg_sp-i,Get(src-i));
    reg_sp -= ub1;
    break;
  case 0x8e: /* STM */
    ub1 = UB(); dst = Get(reg_sp+ub1);
    for (i=0; i<=(ub1-1); i++) Put(dst+i,Pick(i));
    reg_sp += (ub1+1);
    break;
  case 0xa7: /* LDB */
    b = Pop(); 
    Push(Getb(Pop(), b));
    break;
  case 0xc8: /* STB */
    ub1 = Pop() & 0xff; /* index */ b = Pop(); /* byteaddr */
    Putb(Pop(), b, ub1);
    break;
  case 0xc5: /* MOV */
    b = B(); src = Pop(); dst = Pop();
    for (i=0; i<=(b-1); i++) Put(dst+i,Get(src+i));
    break;
  case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
    /* SINDi */
    Push(Get(Pop() + (opcode & 0x07)));
    break;
  case 0xe6: /* IND */
    Push(Get(Pop() + B()));
    break;
  case 0xe7: /* INC */
    Push(Pop() + B());
    break;
  case 0xd7: /* IXA */
    b = B(); t1 = Pop(); Push(Pop() + t1*b);
    break;
  case 0xd8: /* IXP */
    ub1 = UB(); ub2 = UB(); inx = Pop();
    Push(Pop() + inx / ub1);
    Push(ub2); Push((inx % ub1) * ub2);
    break;
  case 0xc9: /* LDP */
    t1 = Pop(); /*start*/ t2 = Pop(); /*nbits*/
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * (sp+2) is an address not a value, so must be dereferenced first */
    Push((Get(Pop() /*addr*/) & GetMask(t1,t2)) >> t1);
    break;
  case 0xca: /* STP */
    t4 = Pop(); /*data*/ t1 = Pop(); /*start*/ t2 = Pop(); /*nbits*/
    t3 = Pop(); /*addr*/ t5 = Get(t3);
    clrbit(t5,GetMask(t1,t2)); t4 = (t4 & masks[t2]) << t1;
    Put(t3, t5 | t4);
    break;
  case 0xa1: /* LAND */
    Push(Pop() & Pop());
    break;
  case 0xa0: /* LOR */
    Push(Pop() | Pop());
    break;
  case 0xe5: /* LNOT */
    Push(~Pop());
    break;
  case 0x9f: /* BNOT */
    Push((~Pop()) & 1);
    break;
  case 0xb4: /* LEUSW */
    t1 = Pop(); t2 = Pop() <= t1 ? 1 : 0;
    Push(t2);
    break;
  case 0xb5: /* GEUSW */
    t1 = Pop(); t2 = Pop() >= t1 ? 1 : 0;
    Push(t2);
    break;
  case 0xe0: /* ABI */
    ts1 = PopS();
    PushS(ts1 < 0 ? -ts1 : ts1);
    break;
  case 0xe1: /* NGI */
    PushS(-PopS());
    break;
  case 0xe2: /* DUP1 */
    Push(Tos());
    break;
  case 0xa2: /* ADI */
    PushS(PopS() + Pop()); 
    break;
  case 0xa3: /* SBI */
    ts1 = PopS(); PushS(PopS() - ts1); 
    break;
  case 0x8c: /* MPI */
    PushS(Pop() * Pop()); 
    break;
  case 0x8d: /* DVI */
    ts1 = PopS(); if (ts1 == 0) { Raise(PASERROR_DIVZERO); break; }
    ts2 = PopS() / ts1;
    PushS(ts2);
    break;
  case 0x8f: /* MODI */
    ts1 = PopS(); if (ts1 <= 0) { Raise(PASERROR_DIVZERO); /* XXX */ break; }
    ts2 = Pop() % ts1;
    PushS(ts2); 
    break;
  case 0xcb: /* CHK */
    t1 = Tos(); t2 = Pick(1); t3 = Pick(2);
    if (t2 <= t3 && t3 <= t1)
      reg_sp += 2;
    else 
      Raise(PASERROR_VALRANGE);
    break;
  case 0xb0: /* EQUI */
    t1 = PopS()==PopS() ? 1 : 0;
    Push(t1);
    break;
  case 0xb1: /* NEQI */
    t1 = PopS()==PopS() ? 0 : 1;
    Push(t1);
    break;
  case 0xb2: /* LEQI */
    ts1 = PopS(); t2 = PopS() <= ts1 ? 1 : 0;
    Push(t2);
    break;
  case 0xb3: /* GEQI */
    ts1 = PopS(); t2 = PopS() >= ts1 ? 1 : 0;
    Push(t2);
    break;
  case 0xcc: /* FLT */
    t1 = PopS();
    PushF((float)t1); 
    break;
  case 0xbe: /* TNC */
    tf1 = PopF();
    PushS((int16)tf1);
    break;
  case 0xbf: /* RND */
    tf1 = PopF();
    PushS((int16)(tf1+0.5)); 
    break;
  case 0xe3: /* ABR */
    PushF((float)fabs(PopF()));
    break;
  case 0xe4: /* NGR */
    PushF(-PopF());
    break;
  case 0xc0: /* ADR */
    tf1 = PopF();
    PushF(tf1 + PopF());
    break;
  case 0xc1: /* SBR */
    tf1 = PopF(); PushF(PopF() - tf1);
    break;
  case 0xc2: /* MPR */
    tf1 = PopF();
    PushF(tf1 * PopF());
    break;
  case 0xc3: /* DVR */
    tf1 = PopF(); if (tf1 == 0) { Raise(PASERROR_DIVZERO); break; }
    tf2 = PopF();
    PushF(tf2 / tf1);
    break;
  case 0xcd: /* EQUREAL */
    tf1 = PopF();
    t1 = tf1==PopF() ? 1 : 0;
    Push(t1);
    break;
  case 0xce: /* LEQREAL */
    tf1 = PopF(); tf2 = PopF();
    t1 = tf2 <= tf1 ? 1 : 0;
    Push(t1);
    break;  
  case 0xcf: /* GEQREAL */
    tf1 = PopF(); tf2 = PopF();
    Push(tf2 >= tf1 ? 1 : 0);
    break;
  case 0xc6: /* DUP2 */
    Push(Pick(1)); Push(Pick(1)); 
    break;
  case 0xc7: /* ADJ */
    ub1 = UB(); len0 = Tos(); src = reg_sp+1; dst = reg_sp + len0 - ub1 +1;
    if (len0 > ub1) {
      for (i=1; i<=ub1; i++) Put(dst + ub1 -i, Get(src + ub1 - i));
    } else {
      for (i=0; i<len0; i++) Put(dst + i, Get(src + i));
      for (i=len0; i<ub1; i++) Put(dst+i,0);
    }
    reg_sp += (len0-ub1+1);
    break;
  case 0xbc: /* SRS */
    hi = Tos(); lo = Pick(1);
    t1 = hi - lo;
    if (hi <= (BSET_SZ-1) && lo <= (BSET_SZ-1)) {
      if (lo > hi) {
        reg_sp++; Put(reg_sp,0);
      } else {
        len0 = hi / WORD_SZ +1;
        reg_sp -= (len0-1); Put(reg_sp,len0);
        for (i=0; i<len0; i++) /* build an empty set */
          Put(reg_sp+1 + i, 0);
        /* @TODO this does an awful lot of memory R/Ws: optimize! */
        for (i=0; i<(len0*WORD_SZ); i++) {
          if (lo <= i && i <= hi) { /* attempt to minimize the number of R/Ws */
            t2 = reg_sp + 1 + (i / WORD_SZ);
            src = Get(t2);
            setbit(src, 1 << (i % WORD_SZ));
            Put(t2, src);
          }
        }
      }
    } else
      Raise(PASERROR_VALRANGE);
    break;
  case 0xda: /* INN */
    len0 = Tos(); ts1 = (int16)Pick(len0 + 1);
    t2 = (0 <= ts1 && ts1 <= (len0*WORD_SZ -1)) ? GetBit(reg_sp+1, ts1) : 0;
    Put(reg_sp + len0 + 1,t2);
    reg_sp += (len0+1);
    break;
  case 0xdb: /* UNI */
    len0 = Tos(); len1 = Pick(len0 + 1);
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    if (len1 >= len0) {
      src = reg_sp + 1; dst = reg_sp + len0 + 2;
      for (i=0; i<len0; i++) Put(dst+i, Get(dst+i) | Get(src+i));
      reg_sp += (len0+1);
    } else {
      src = reg_sp + len0 + 2; dst = reg_sp + 1;
      for (i=0; i<len1; i++) Put(dst+i, Get(dst+i) | Get(src+i));
      src = reg_sp + len0; dst = reg_sp + len0 + len1 + 1;
      for (i=0; i<= len0; i++) Put(dst-i, Get(src-i));
      reg_sp += (len1+1);
    }
    break;
  case 0xdc: /* INT */
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    len0 = Tos(); len1 = Pick(len0 + 1);
    if (len0==0) {
      reg_sp += (len1+1); Put(reg_sp,0);
    } else if (len1==0) {
      reg_sp += (len0+1);
    } else if (len1 > len0) {
      src = reg_sp+1; dst = reg_sp+len0 + 2;
      for (i=0; i<len0; i++) Put(dst+i,Get(dst+i) & Get(src+i));
      for (i=len0; i<len1; i++) Put(dst+i,0);
      reg_sp += (len0+1);
    } else {      
      dst = reg_sp+ len0 + 2; src = reg_sp +1;
      for (i=0; i<len1; i++) Put(dst+i,Get(dst+i) & Get(src+i));
      reg_sp += (len0+1);
    }
    break;
  case 0xdd: /* DIF */
    len0 = Tos(); len1 = Pick(len0 + 1);
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    if (len0==0) {
      reg_sp++;
    } else if (len1==0) {
      reg_sp += (len0+1);
    } else if (len1 > len0) {
      src = reg_sp + 1; dst = reg_sp + len0 + 2;
      for (i=0; i<len0; i++) Put(dst+i, Get(dst+i) & ~Get(src+i));
      reg_sp += (len0+1);
    } else {
      dst = reg_sp + len0 + 2; src = reg_sp + 1;
      for (i=0; i<len1; i++) Put(dst+i,Get(dst+i) & ~Get(src+i));
      reg_sp += (len0+1);
    }
    break;
  case 0xb6: /* EQUPWR */
    len0 = Tos(); len1 = Pick(len0 + 1); i=0;
    min1 = MIN(len0,len1); max1 = MAX(len0,len1);
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    src = reg_sp + 1; dst = reg_sp + len0 + 2;
    while (i<min1) {
      if (Get(src+i) != Get(dst+i)) break;
      i++;
    }
    if (len0 > len1) {
      while (i < max1) {
        if (Get(src+i) != 0) break;
        i++;
      } 
    } else if (len1 > len0) {
      while (i < max1) {
        if (Get(dst+i) != 0) break;
        i++;
      }      
    }
    reg_sp += (len0+len1+1); Put(reg_sp,(i >= max1 ? 1 : 0));
    break;
  case 0xb7: /* LEQPWR */
    len0 = Tos(); len1 = Pick(len0 + 1); i=0;
    min1 = MIN(len0,len1); max1 = MAX(len0,len1);
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    src = reg_sp + 1; dst = reg_sp + len0 + 2;
    while (i<min1) {
      t1 = Get(src+i); if (t1 != (Get(dst+i) | t1)) break;
      i++;
    }
    if (len0 > len1) {
      while (i < max1) {
        if (Get(src+i) != 0) break;
        i++;
      } 
    } else i = max1;
    reg_sp += (len0+len1+1); Put(reg_sp,(i >= max1 ? 1 : 0));
    break;
  case 0xb8: /* GEQPWR */
    len0 = Tos(); len1 = Pick(len0 + 1); i=0;
    min1 = MIN(len0,len1); max1 = MAX(len0,len1);
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * src and dst are not addresses on stack (^p) but addresses OF stack */
    src = reg_sp + 1; dst = reg_sp + len0 + 2;
    while (i<min1) {
      t1 = Get(src+i); if (t1 != (Get(dst+i) | t1)) break;
      i++;
    }
    if (len0 < len1) {
      while (i < max1) {
        if (Get(src+i) != 0) break;
        i++;
      } 
    } else i = max1;
    reg_sp += (len0+len1+1); Put(reg_sp,(i >= max1 ? 1 : 0));
    break;
  case 0xb9: /* EQUBYT */
    b = B(); src = Pop(); dst = Pop(); i = 0;
    while (i < b && Getb(src,i) == Getb(dst,i)) i++;
    t1 = i >= b ? 1 : 0;
    Push(t1);
    break;
  case 0xba: /* LEQBYT */
    b = B(); src = Pop(); dst = Pop(); i = 0;
    while (i < b && Getb(src,i) <= Getb(dst,i)) i++;
    Push(i >= b ? 1 : 0);
    break;
  case 0xbb: /* GEQBYT */
    b = B(); src = Pop(); dst = Pop(); i = 0;
    while (i < b && Getb(src,i) >= Getb(dst,i)) i++;
    Push(i >= b ? 1 : 0);
    break;
  case 0x8a: /* UJP */
    b = SB(); reg_ipc += b;
    break;
  case 0xd4: /* FJP */
    b = SB(); t1 = Pop();
    if ((t1 & 1)==0)
      reg_ipc += b;
    break;
  case 0xd2: /* EFJ */
    b = SB(); t1 = Pop(); t2 = Pop();
    if (t2 != t1)
      reg_ipc += b;
    break;
  case 0xd3: /* NFJ */
    b = SB(); t1 = Pop(); t2 = Pop();
    if (t2 == t1)
      reg_ipc += b;
    break;
  case 0x8b: /* UJPL */
    w = W(); reg_ipc += w;
    break;
  case 0xd5: /* FJPL */
    w = W(); t1 = Pop();
    if ((t1 & 1)== 0)
      reg_ipc += w;
    break;
  case 0xd6: /* XJP */
    b = B(); t1 = Pop();
    t2 = Get(reg_segb + b);
    if (t2 <= t1 && Get(reg_segb + b + 1) >= t1)
      reg_ipc += Get(reg_segb + b + 2 + (t1-t2));
    break;
  case 0x90: /* CPL */
    procno = UB();
    ptbl = GetPtbl();
    reg_ipc = createMSCW(ptbl, procno, reg_mp, 0, reg_segb);
    break;
  case 0x91: /* CPG */
    procno = UB();
    ptbl = GetPtbl();
    reg_ipc = createMSCW(ptbl, procno, reg_bp, 0, reg_segb);
    break;
  case 0x92: /* CPI */
    db = DB(); procno = UB();
    ptbl = GetPtbl();
    /* Bogus warning: WD9693_PasIII_OSref_Jul82 is wrong here:
     * msstat is preserved, CPI page 46 does not set it */
    reg_ipc = createMSCW(ptbl, procno, Get(reg_mp+OFF_MSSTAT), 0, reg_segb);
    reg_lm = reg_mp;
    for (i=1; i<= db; i++)
        reg_lm = Get(reg_lm+OFF_MSSTAT);
    Put(reg_mp+OFF_MSSTAT,reg_lm); /* fix stat link */
    break;
  case 0x93: /* CXL */
    segno = UB(); procno = UB();
    osegno = GetSegno();
    osegb = reg_segb;
    ptbl = SetSEGB(segno);
    AdjustRefCount(segno, 1);
    reg_ipc = createMSCW(ptbl, procno, reg_mp, osegno, osegb);
    break;
  case 0x94: /* CXG */
    ub1 = UB(); ub2 = UB();
    DoCXG(ub1, ub2);
    break;
  case 0x95: /* CXI */
    segno = UB(); db = DB(); procno = UB();
    osegno = GetSegno();
    osegb = reg_segb;
    ptbl = SetSEGB(segno);
    AdjustRefCount(segno, 1);
    reg_ipc = createMSCW(ptbl, procno, reg_mp, osegno, osegb);
    reg_lm = reg_mp;
    for (i=1; i<= db; i++) reg_lm = Get(reg_lm+OFF_MSSTAT);  
    Put(reg_mp+OFF_MSSTAT,reg_lm); /* fix stat link */
    break;
  case 0x97: /* CPF */
    t1 = Pop(); reg_lm = Pop();
    segno = HiByte(t1);
    procno = LoByte(t1);
    osegno = GetSegno();
    osegb = reg_segb;
    ptbl = SetSEGB(segno);
    AdjustRefCount(segno, 1);
    reg_ipc = createMSCW(ptbl, procno, reg_lm, osegno, osegb);
    break;
  case 0x96: /* RPU */
    dbg_procleave();
    b = B(); reg_sp = reg_mp; reg_lm = reg_mp;
    reg_mp = Get(reg_lm+OFF_MSDYNL);
    reg_ipc = Get(reg_lm+OFF_MSIPC);
    segno = Getb(reg_lm+OFFB_MSSEG,0);
    if (segno) {
      osegno = GetSegno();
      AdjustRefCount(osegno, -1);
      (void)SetSEGB(segno);
    }
    reg_sp += (b + MSCW_SZ);
    break;
  case 0x99: /* LSL */
    db = DB(); reg_lm = reg_mp;
    for (i=1; i<= db; i++) reg_lm = Get(reg_lm+OFF_MSSTAT);
    Push(reg_lm);
    break;
  case 0xde: /* SIGNAL */
    t1 = Pick(0);
    rc = DoSIGNAL(t1);
    break;
  case 0xdf: /* WAIT */
    t1 = Pop();
    DoWAIT(t1); break;
  case 0x9d: /* LPR */
    w = Tos();
    if (w >= 0)
      save_to_tib(); 
    if (w == -3) Put(reg_sp, reg_rq);
    else if (w == -2) Put(reg_sp, reg_ssv);
    else if (w == -1) Put(reg_sp, reg_ctp);
    else if (w > 0) Put(reg_sp, Get(reg_ctp + w));
    break;
  case 0xd1: /* SPR */
    t1 = Tos();
    w = (int16)Pick(1);
    if (w >= -1)
      save_to_tib();
    if (w == -3) {
      reg_rq = t1;
    } else if (w == -2) {
      reg_ssv = t1;
    } else if (w == -1) { 
//      sim_printf("SPR Taskswitch reg_ctp=%x\n",t1);
      reg_rq = t1;
      taskswitch5();
//      sim_printf("SPR Taskswitch done reg_ctp=%x reg_rq=%x\n",reg_ctp,reg_rq);
      break; /* mustn't fall through reg_sp +=2 */
    } else if (w >= 1) {
      switch (w) {
      case OFF_SP: reg_sp = t1; break;
      case OFF_MP: reg_mp = t1; break;
      case OFF_BP: reg_bp = t1; break;
      case OFF_IPC: reg_ipc = t1; break;
      case OFF_SEGB: reg_segb = t1; break;
      default: Put(reg_ctp + w, t1); break;
      }
    }
    if (w >= -1)
      save_to_tib();
    if (w != OFF_SP) reg_sp += 2; /* mustn't change modified SP again */
    break;
  case 0x9e: /* BPT */
    Raise(PASERROR_USERBRK);
    return STOP_BPT;
//    break;
  case 0x9c: /* NOP */
    break;
  case 0xbd: /* SWAP */
    t1 = Tos();
    Put(reg_sp, Pick(1));
    Put(reg_sp+1, t1);
    break;
  default:
//    Raise(PASERROR_UNIMPL);
    return STOP_IMPL;
  }
  
  if (rc != SCPE_OK) return rc;

  /* set new PCX */
  PCX = MAKE_BADDR(reg_segb,reg_ipc);
  if (dbg_check(opcode, DEBUG_POST)) return STOP_DBGPOST;

  /* count cycles */
  sim_interval--;

  return SCPE_OK;
}

t_stat sim_instr(void)
{
  t_stat rc = SCPE_OK;
  
  /* mandatory idling */
  sim_rtcn_init(TMR_IDLECNT, TMR_IDLE);
  sim_set_idle(&cpu_unit, 10, NULL, NULL);

  while (rc == SCPE_OK) {

    /* set PCX of instruction in progress */
    PCX = MAKE_BADDR(reg_segb,reg_ipc);

    /* process timer */
    if (sim_interval <= 0) {
      if ((rc = sim_process_event()) != SCPE_OK)
        break;
    }

    /* effectively latch interrupts now:
     * there is a known CPU bug: interrupts are latched here.
     * If the following instruction will disable interrupts,
     * the interrupt is processed anyway
     */

    /* if reg_ctp is NIL, CPU waits for interrupt or autoload done.
     * handle time by NOP cycles
     */
    if (reg_ctp != NIL) {
      if ((rc = DoInstr()) != SCPE_OK) break;
    }
    else {
      sim_idle(TMR_IDLE, TRUE);
    }

    /* process interrupts 
     * CPU latches interrupt request now, and after instr
     * execution, will process them. Note: this is a known bug in CPU, because
     * if the instruction disables interrupts, the interrupt is processed anyway */
    if (cpu_isIntEnabled()) {
      reg_intpending |= reg_intlatch;
      if (reg_intpending) {
        if ((rc = cpu_processInt()) != SCPE_OK) {
          sim_printf("processint returns %d\n",rc); fflush(stdout);
          break;
        }
      }
    }

  }
  return rc;
}

static t_stat cpu_set_flag(UNIT *uptr, int32 value, CONST char *cptr, void *desc) {
  uptr->flags |= value;
  return SCPE_OK;
}

static t_stat cpu_set_noflag(UNIT *uptr, int32 value, CONST char *cptr, void *desc) {
  uptr->flags &= ~value;
  return SCPE_OK;
}

