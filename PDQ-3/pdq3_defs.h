/* pdq3_defs.h: PDQ3 simulator definitions 

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
   
   20131103 hv INT_CONR/CONT assignments incorrect in docs, must be swapped
   20141003 hv recommended warnings from VC++ and gcc added
*/
#ifndef _PDQ3_DEFS_H_
#define _PDQ3_DEFS_H_ 0

#include "sim_defs.h"                                   /* simulator defns */
#include "sim_sock.h"
#include "sim_tmxr.h"

/* constants */
#define NIL 0xfc00                          /* Pascal Microengine NIL value */
#define MSCW_SZ 4                           /* size of MSCW */
#define REAL_SZ 2                           /* size of real number (REAL*4) */
#define BSET_SZ 4080                        /* usable size of set in bits */
#define ISET_SZ 255                         /* size of set in words */ 
#define WORD_SZ 16                          /* size of machine word in bits */

#define OFF_SEGBASE 0                       /* offsets into SIB entry */
#define OFF_SEGLENG 1
#define OFF_SEGREFS 2
#define OFF_SEGADDR 3
#define OFF_SEGUNIT 4
#define OFF_PREVSP 5
#define OFF_SEGNAME 6
#define OFF_SEGLINK 10
#define OFF_SEGGLOBAL 11
#define OFF_SEGINIT 12
#define OFF_SEG13 13
#define OFF_SEGBACK 14

#define OFF_MSSTAT 0                        /* offsets into MSCW */
#define OFF_MSDYNL 1
#define OFF_MSIPC 2
#define OFFB_MSSEG 3
#define OFFB_MSFLAG 3

#define OFF_WAITQ 0                         /* offset into TIB */
#define OFF_QLINK 0
#define OFFB_PRIOR 1
#define OFFB_FLAGS 1
#define OFF_SPLOW 2
#define OFF_SPUPR 3
#define OFF_SP 4
#define OFF_MP 5
#define OFF_BP 6
#define OFF_IPC 7
#define OFF_SEGB 8
#define OFF_HANGP 9
#define OFF_IORSLT 10
#define OFF_SIBS 11

#define OFF_SEMCOUNT 0                      /* offset into SEMA variable */
#define OFF_SEMWAITQ 1

#define SSR_BERR 0x01                       /* bits of system status register */
#define SSR_TICK 0x02
#define SSR_INTVL 0x04
#define SSR_BIT3 0x08
#define SSR_PWRF 0x10
#define SSR_PRNT 0x20
#define SSR_INTEN 0x40
#define SSR_INIT 0x80

/* fixed interrupts */
#define INT_BERR  0                         /* interrupt levels */
#define INT_PWRF  1
#define INT_DMAFD 2
#define INT_CONR  3                         /* Appendix B.0.1 has CONT and CONR swapped, see Errata */
#define INT_CONT  4
#define INT_PRNT  5
#define INT_TICK  6
#define INT_INTVL 7

/* assignable QBUS interrupts, daisy-chained - highest prio = 8, lowest = 31 */
#define INT_QBUS8 8
#define INT_QBUS9 9
#define INT_QBUS10 10
#define INT_QBUS11 11
#define INT_QBUS12 12
#define INT_QBUS13 13
#define INT_QBUS14 14
#define INT_QBUS15 15
#define INT_QBUS16 16
#define INT_QBUS17 17
#define INT_QBUS18 18
#define INT_QBUS19 19
#define INT_QBUS20 20
#define INT_QBUS21 21
#define INT_QBUS22 22
#define INT_QBUS23 23
#define INT_QBUS24 24
#define INT_QBUS25 25
#define INT_QBUS26 26
#define INT_QBUS27 27
#define INT_QBUS28 28
#define INT_QBUS29 29
#define INT_QBUS30 30
#define INT_QBUS31 31

/* common unit user-defined attributes */ 
#define u_unitno u3

/* Memory */
#define MEMSIZE         65536                           /* memory size in bytes */
#define MAXMEMSIZE      (65535 * 2)                    /* maximum memory size in bytes */
#define memorysize      uptr->capac

/* CPU Unit flags */
#define UNIT_V_PDQ3    (UNIT_V_UF + 0)
#define UNIT_V_MSIZE   (UNIT_V_UF + 1)
#define UNIT_V_PASEXC  (UNIT_V_UF + 2)


#define UNIT_PDQ3       (1u << UNIT_V_PDQ3)
#define UNIT_MSIZE      (1u << UNIT_V_MSIZE)
#define UNIT_PASEXC     (1u << UNIT_V_PASEXC)

#define Q_PDQ3          (cpu_unit.flags & UNIT_PDQ3)
#define Q_MSIZE         (cpu_unit.flags & UNIT_MSIZE)
#define Q_PASEXC        (cpu_unit.flags & UNIT_PASEXC)

#define setbit(reg,val) reg |= (val)
#define clrbit(reg,val) reg &= ~(val)
#define isbitset(reg, val) (reg & (val))
#define isbitclr(reg, val) ((reg & (val)) == 0)

/* debug flags */
#define DBG_NONE       0x0000

#define DBG_FD_CMD     0x0001
#define DBG_FD_READ    0x0002
#define DBG_FD_WRITE   0x0004
#define DBG_FD_SVC     0x0008
#define DBG_FD_IMD     0x0010
#define DBG_FD_IMD2    0x0020 /* deep inspection */
#define DBG_FD_DMA     0x0040
#define DBG_FD_DMA2    0x0080 /* deep inspection */

//define DBG_CPU_TRACE  0x0001 unused
#define DBG_CPU_INT    0x0001
#define DBG_CPU_INT2   0x0002 /* deep inspection */
#define DBG_CPU_READ   0x0004
#define DBG_CPU_WRITE  0x0008
#define DBG_CPU_FETCH  0x0010
#define DBG_CPU_PUSH   0x0020
#define DBG_CPU_POP    0x0040
#define DBG_CPU_PICK   0x0080
#define DBG_CPU_STACK  (DBG_CPU_PUSH|DBG_CPU_POP|DBG_CPU_PICK)
#define DBG_CPU_CONC   0x0100
#define DBG_CPU_CONC2  0x0200 /* deep inspection */
#define DBG_CPU_CONC3  0x0400 /* even deeper inspection */

#define DBG_CON_READ   0x0001
#define DBG_CON_WRITE  0x0002
#define DBG_CON_SVC    0x0004

#define DBG_TIM_READ   0x0001
#define DBG_TIM_WRITE  0x0002
#define DBG_TIM_SVC    0x0004

#define DBG_PCFORMAT0  "[%04x:%04x] "
#define DBG_PCFORMAT1  " [%04x:%04x] "
#define DBG_PCFORMAT2  "  [%04x:%04x] "
#define DBG_PC         reg_segb,ADDR_OFF(PCX)
#define DBG_PC2        reg_segb,reg_ipc

/* calibration timers */
#define TMR_CONPOLL 1

/* IDLE timer. This is supposed to run at 100 Hz; the CPU runs at
 * 1.25MHz, i.e. the interval is 12500. */
#define TMR_IDLE 0
#define TMR_IDLECNT 12500

/* console sio data rates */
#define CON_POLLUNIT 0
#define CON_TERMUNIT 1
#define CON_POLLFIRST 1 /* immediate */
#define CON_POLLRATE 100
#define CON_TPS 100
#define CON_TERMRATE 100

/* floppy size */
#define FDC_MAX_TRACKS  77

/* IMD anachronism */
#ifndef MAX_COMMENT_LEN
#define MAX_COMMENT_LEN 256
#endif

/* XXX @TODO Pascal error codes (Raise()) */
#define PASERROR_SYSTEM 0
#define PASERROR_VALRANGE 1
#define PASERROR_NOSEG 2
#define PASERROR_PROCERR 3
#define PASERROR_STKOVFL 4
#define PASERROR_INTOVFL 5
#define PASERROR_DIVZERO  6
#define PASERROR_MEMERR 7
#define PASERROR_USERBRK 8
#define PASERROR_SYSIO 9
#define PASERROR_USERIO 10
#define PASERROR_UNIMPL 11
#define PASERROR_FPERR 12
#define PASERROR_STRINGOVFL 13
#define PASERROR_HALT 14

/* simh  error codes */
#define STOP_IBKPT   1
#define STOP_MEM     2
#define STOP_ERROP   3
#define STOP_ERRADR  4
#define STOP_ERRIO   5
#define STOP_IMPL    6
#define STOP_BPT     7
#define STOP_DBGPRE  8
#define STOP_DBGPOST 9
#define STOP_PASEXC 10

/* IO addresses and vectors */
#define CON_IOBASE    0xfc10
#define CON_RCV_VEC   0x0012
#define CON_XMT_VEC   0x000e
#define CON_PRT_VEC   0x0016
#define SES_IOBASE    0xfc18
#define SES_BERR_VEC  0x0002
#define SES_PWRF_VEC  0x0006
#define SSR_IOBASE    0xfc24
#define TIM_IOBASE    0xfc20
#define TIM_TICK_VEC  0x001a
#define TIM_INTVL_VEC 0x001e
#define FDC_IOBASE    0xfc30
#define FDC_VEC       0x000a
#define CPU_SERIALNO  0xf5ff /* is part of ROM */
#define ROM_BASE      0xfc68
#define ROM           0xf400
#define ROM_SIZE      0x01ff /* excluding serial number */

/* address calculations */
#define ADDRMASK_SEG 0xffff0000
#define ADDRMASK_OFF 0x0000ffff
#define ADDR_16bit(a) ((a) & 0x0000ffff)
#define ADDR_SEG(a) (((a)>>16) & ADDRMASK_OFF)
#define ADDR_OFF(a) ((a) & ADDRMASK_OFF)
#define MAKE_BADDR(s,o) ((ADDR_16bit(s)<<16) | ADDR_16bit(o))
#define MAKE_WADDR(a) MAKE_BADDR(NIL,ADDR_OFF(a))
#define ADDR_ISWORD(a) (ADDR_SEG(a) == NIL)

/* opcode table */
#define OP_ERROR -1
#define OP_NULL  0
#define OP_UB    1
#define OP_W     2
#define OP_B     3
#define OP_DBB   4
#define OP_UBB   5
#define OP_BUB   6
#define OP_SB    7
#define OP_DBUB  8
#define OP_UBUB  9
#define OP_UBDBUB 10
#define OP_DB    11
#define OP_SW    12
#define OP_AB    13

typedef struct _optable { 
  const char* name;
  int16 flags;
} OPTABLE;
extern OPTABLE optable[];

/* debug support */
#define DEBUG_OPDBGFILE       "opcode.dbg"
#define DEBUG_MINOPCODE       0
#define DEBUG_MAXOPCODE       0xe8
#define DEBUG_VALIDOP(op)     (optable[op].flags >= 0)
#define DEBUG_PRE             0x01
#define DEBUG_POST            0x02
extern t_stat dbg_init();
extern t_stat dbg_check(t_value data,uint8 prepost);
extern t_stat dbg_dump_tib(FILE* fd, uint16 base);
extern t_stat dbg_dump_queue(FILE* fd, const char* qname, uint16 q);
extern t_stat dbg_dump_mscw(FILE* fd, uint16 base);
extern t_stat dbg_dump_seg(FILE* fd, uint16 segptr);
extern t_stat dbg_dump_segtbl(FILE* fd);
extern t_stat dbg_segtrack(uint16 segbase);
extern t_stat dbg_procenter(uint16 segbase, uint16 procno, uint16 mscw, uint16 osegb);
extern t_stat dbg_procleave();
extern void dbg_enable();
extern t_stat dbg_calltree(FILE* fd);
extern t_stat dbg_enteralias(const char* key, const char* value);
extern t_stat dbg_listalias(FILE*);
/* floating point */
typedef union flcvt {
  float f;
  uint16 i[2];
} T_FLCVT;

/* externals */
extern DEVICE cpu_dev;
extern UNIT   cpu_unit;
extern DEVICE con_dev;
extern UNIT   con_unit[];
extern DEVICE fdc_dev;
extern UNIT   fdc_unit[];
extern DEVICE timer_dev;
extern UNIT   timer_unit[];
extern t_addr PCX; /* PC at the begin of execution */
extern uint16 reg_segb;
extern uint32 reg_dmabase;
extern uint16 reg_mp;
extern uint16 reg_bp;
extern uint16 reg_sp;
extern uint16 reg_splow;
extern uint16 reg_spupr;
extern uint16 reg_ctp;
extern uint16 reg_rq;
extern uint16 reg_ipc;
extern uint16 reg_fc68;
extern uint16 reg_romsize;
extern uint16 reg_ssv;
extern uint16 reg_ssr;
extern uint16 reg_cpuserial;
extern uint32 reg_intpending;

extern t_stat Read(t_addr base, t_addr woffset, uint16 *data, uint32 dctrl);
extern t_stat Write(t_addr base, t_addr boffset, uint16 data, uint32 dctrl);
extern t_stat ReadB(t_addr base, t_addr boffset, uint16 *data, uint32 dctrl);
extern t_stat WriteB(t_addr base, t_addr boffset, uint16 data, uint32 dctrl);
extern t_stat ReadEx(t_addr base, t_addr woffset, uint16 *data);
extern t_stat ReadBEx(t_addr base, t_addr boffset, uint16 *data);

extern t_stat rom_read(t_addr base, uint16 *data);
extern t_stat rom_write(t_addr base, uint16 data);
extern t_stat fprint_sym_m (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);

extern t_stat con_read(t_addr ioaddr, uint16 *data);
extern t_stat con_write(t_addr ioaddr, uint16 data);
extern t_stat con_binit();
extern t_stat fdc_boot(int32 unitnum, DEVICE *dptr);
extern t_stat fdc_read(t_addr ioaddr, uint16 *data);
extern t_stat fdc_write(t_addr ioaddr, uint16 data);
extern t_stat fdc_autoload(int unitnum);
extern t_stat fdc_binit();
extern t_stat tim_read(t_addr ioaddr, uint16 *data);
extern t_stat tim_write(t_addr ioaddr, uint16 data);

extern void cpu_assertInt(int level, t_bool tf);
extern t_stat cpu_raiseInt(int level);
extern t_stat cpu_setIntVec(uint16 vector,int level);
extern void   cpu_setRegs(uint16 ctp, uint16 ssv, uint16 rq);
extern void   cpu_finishAutoload();
extern t_stat cpu_buserror();

typedef t_stat (*IOREAD)(t_addr ioaddr, uint16 *data);
typedef t_stat (*IOWRITE)(t_addr ioaddr, uint16 data);

typedef struct _ioinfo {
  struct _ioinfo* next;
  uint16 iobase;
  uint16 iosize;
  uint16 qvector;
  uint16 qprio;
  IOREAD read;
  IOWRITE write;
} IOINFO;

typedef struct _devctxt {
  IOINFO* ioi;
} DEVCTXT;

extern t_stat pdq3_ioinit();
extern t_stat add_ioh(IOINFO* ioi);
extern t_stat del_ioh(IOINFO* ioi);
extern t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern t_stat set_iovec(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_iovec(FILE *st, UNIT *uptr, int value, CONST void *desc);
extern t_stat set_ioprio(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_ioprio(FILE *st, UNIT *uptr, int value, CONST void *desc);

#endif
