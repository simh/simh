/* pdp10_cpu.c: PDP-10 CPU simulator

   Copyright (c) 1993-2017, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          KS10 central processor

   07-Sep-17    RMS     Fixed sim_eval declaration in history routine (COVERITY)
   14-Jan-17    RMS     Fixed bugs in 1-proceed
   09-Feb-16    RMS     Fixed nested indirects and executes (Tim Litt)
   25-Mar-12    RMS     Added missing parameters to prototypes (Mark Pizzolato)
   17-Jul-07    RMS     Fixed non-portable usage in SHOW HISTORY
   28-Apr-07    RMS     Removed clock initialization
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
                        Fixed warning in MOVNI
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   10-Nov-04    RMS     Added instruction history
   08-Oct-02    RMS     Revised to build dib_tab dynamically
                        Added SHOW IOSPACE
   30-Dec-01    RMS     Added old PC queue
   25-Dec-01    RMS     Cleaned up sim_inst declarations
   07-Dec-01    RMS     Revised to use new breakpoint package
   21-Nov-01    RMS     Implemented ITS 1-proceed hack
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   10-Aug-01    RMS     Removed register in declarations
   17-Jul-01    RMS     Moved function prototype
   19-May-01    RMS     Added workaround for TOPS-20 V4.1 boot bug
   29-Apr-01    RMS     Fixed modifier naming conflict
                        Fixed XCTR/XCTRI, UMOVE/UMOVEM, BLTUB/BLTBU for ITS
                        Added CLRCSH for ITS

   The 36b system family had six different implementions: PDP-6, KA10, KI10,
   L10, KL10 extended, and KS10.  This simulator implements the KS10.

   The register state for the KS10 is:

   AC[8][16]                    accumulators
   PC                           program counter
   flags<0:11>                  state flags
   pi_enb<1:7>                  enabled PI levels
   pi_act<1:7>                  active PI levels
   pi_prq<1:7>                  program PI requests
   apr_enb<0:7>                 enabled system flags
   apr_flg<0:7>                 system flags
   ebr                          executive base register
   ubr                          user base register
   hsb                          halt status block address
   spt                          SPT base
   cst                          CST base
   pur                          process use register
   cstm                         CST mask

   The PDP-10 had just two instruction formats: memory reference
   and I/O.

    000000000 0111 1 1111 112222222222333333
    012345678 9012 3 4567 890123456789012345
   +---------+----+-+----+------------------+
   |  opcode | ac |i| idx|     address      | memory reference
   +---------+----+-+----+------------------+

    000 0000000 111 1 1111 112222222222333333
    012 3456789 012 3 4567 890123456789012345
   +---+-------+---+-+----+------------------+
   |111|device |iop|i| idx|     address      | I/O
   +---+-------+---+-+----+------------------+

   This routine is the instruction decode routine for the PDP-10.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until an abort occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        MUUO instruction in executive mode
        pager error in interrupt sequence
        invalid vector table in interrupt sequence
        illegal instruction in interrupt sequence
        breakpoint encountered
        nested indirects exceeding limit
        nested XCT's exceeding limit
        I/O error in I/O simulator

   2. Interrupts.  PDP-10's have a seven level priority interrupt
      system.  Interrupt requests can come from internal sources,
      such as APR program requests, or external sources, such as
      I/O devices.  The requests are stored in pi_prq for program
      requests, pi_apr for other internal flags, and pi_ioq for
      I/O device flags.  Internal and device (but not program)
      interrupts must be enabled on a level by level basis.  When
      an interrupt is granted on a level, interrupts at that level
      and below are masked until the interrupt is dismissed.

      The I/O device interrupt system is taken from the PDP-11.
      int_req stores the interrupt requests for Unibus I/O devices.
      Routines in the Unibus adapter map requests in int_req to
      PDP-10 levels.  The Unibus adapter also calculates which
      device to get a vector from when a PDP-10 interrupt is granted.

   3. Arithmetic.  The PDP-10 is a 2's complement system.

   4. Adding I/O devices.  These modules must be modified:

        pdp10_defs.h    add device address and interrupt definitions
        pdp10_sys.c     add sim_devices table entry

   A note on ITS 1-proceed.  The simulator follows the implementation
   on the KS10, keeping 1-proceed as a side flag (its_1pr) rather than
   as flags<8>.  This simplifies the flag saving instructions, which
   don't have to clear flags<8> before saving it.  Instead, the page
   fail and interrupt code must restore flags<8> from its_1pr.  Unlike
   the KS10, the simulator will not lose the 1-proceed trap if the
   1-proceeded instructions clears 1-proceed.
*/

#include "pdp10_defs.h"
#include <setjmp.h>

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    a10         pc;
    a10         ea;
    d10         ir;
    d10         ac;
    } InstHistory;

extern a10 fe_xct;                                      /* Front-end forced XCT */
extern DEVICE pag_dev;
extern t_stat pag_reset (DEVICE *dptr);

d10 *M = NULL;                                          /* memory */
d10 acs[AC_NBLK * AC_NUM] = { 0 };                      /* AC blocks */
d10 *ac_cur, *ac_prv;                                   /* AC cur, prv (dyn) */
a10 epta, upta;                                         /* proc tbl addr (dyn) */
a10 saved_PC = 0;                                       /* scp: saved PC */
d10 pager_word = 0;                                     /* pager: error word */
a10 pager_PC = 0;                                       /* pager: saved PC */
int32 pager_flags = 0;                                  /* pager: trap flags */
t_bool pager_pi = FALSE;                                /* pager: in pi seq */
t_bool pager_tc = FALSE;                                /* pager: trap cycle */
d10 ebr = 0;                                            /* exec base reg */
d10 ubr = 0;                                            /* user base reg */
d10 hsb = 0;                                            /* halt status block */
d10 spt = 0;                                            /* TOPS20 paging regs */
d10 cst = 0;
d10 pur = 0;
d10 cstm = 0;
a10 dbr1 = 0;                                           /* ITS paging regs */
a10 dbr2 = 0;
a10 dbr3 = 0;
a10 dbr4 = 0;
d10 pcst = 0;                                           /* ITS PC sampling */
int32 pi_on = 0;                                        /* pi system enable */
int32 pi_enb = 0;                                       /* pi enabled levels */
int32 pi_act = 0;                                       /* pi active levels */
int32 pi_ioq = 0;                                       /* pi io requests */
int32 pi_apr = 0;                                       /* pi apr requests */
int32 pi_prq = 0;                                       /* pi prog requests */
int32 apr_enb = 0;                                      /* apr enables */
int32 apr_flg = 0;                                      /* apr flags */
int32 apr_lvl = 0;                                      /* apr level */
int32 qintr = 0;                                        /* interrupt pending */
int32 flags = 0;                                        /* flags */
int32 its_1pr = 0;                                      /* ITS 1-proceed */
int32 stop_op0 = 0;                                     /* stop on 0 */
int32 rlog = 0;                                         /* extend fixup log */
int32 ind_max = 0;                                      /* nested ind limit */
int32 xct_max = 0;                                      /* nested XCT limit */
a10 pcq[PCQ_SIZE] = { 0 };                              /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
jmp_buf save_env;
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */
int32 apr_serial = -1;                                  /* CPU Serial number */

/* Forward and external declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_serial (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

d10 adjsp (d10 val, a10 ea);
void ibp (a10 ea, int32 pflgs);
d10 ldb (a10 ea, int32 pflgs);
void dpb (d10 val, a10 ea, int32 pflgs);
void adjbp (int32 ac, a10 ea, int32 pflgs);
d10 add (d10 val, d10 mb);
d10 sub (d10 val, d10 mb);
void dadd (int32 ac, d10 *rs);
void dsub (int32 ac, d10 *rs);
int32 jffo (d10 val);
d10 lsh (d10 val, a10 ea);
d10 rot (d10 val, a10 ea);
d10 ash (d10 val, a10 ea);
void lshc (int32 ac, a10 ea);
void rotc (int32 ac, a10 ea);
void ashc (int32 ac, a10 ea);
void circ (int32 ac, a10 ea);
void blt (int32 ac, a10 ea, int32 pflgs);
void bltu (int32 ac, a10 ea, int32 pflgs, int dir);
a10 calc_ea (d10 inst, int32 prv);
a10 calc_ioea (d10 inst, int32 prv);
d10 calc_jrstfea (d10 inst, int32 pflgs);
void pi_dismiss (void);
void set_newflags (d10 fl, t_bool jrst);
extern t_bool aprid (a10 ea, int32 prv);
t_bool wrpi (a10 ea, int32 prv);
t_bool rdpi (a10 ea, int32 prv);
t_bool czpi (a10 ea, int32 prv);
t_bool copi (a10 ea, int32 prv);
t_bool wrapr (a10 ea, int32 prv);
t_bool rdapr (a10 ea, int32 prv);
t_bool czapr (a10 ea, int32 prv);
t_bool coapr (a10 ea, int32 prv);
int32 pi_eval (void);
int32 test_int (void);
void set_ac_display (d10 *acbase);

extern t_stat build_dib_tab (void);
extern t_stat show_iospace (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
extern void set_dyn_ptrs (void);
extern a10 conmap (a10 ea, int32 mode, int32 sw);
extern void fe_intr ();
extern void dfad (int32 ac, d10 *rs, int32 inv);
extern void dfmp (int32 ac, d10 *rs);
extern void dfdv (int32 ac, d10 *rs);
extern void dmul (int32 ac, d10 *rs);
extern void ddiv (int32 ac, d10 *rs);
extern void fix (int32 ac, d10 mb, t_bool rnd);
extern d10 fad (d10 val, d10 mb, t_bool rnd, int32 inv);
extern d10 fmp (d10 val, d10 mb, t_bool rnd);
extern t_bool fdv (d10 val, d10 mb, d10 *rs, t_bool rnd);
extern d10 fsc (d10 val, a10 ea);
extern d10 fltr (d10 mb);
extern int xtend (int32 ac, a10 ea, int32 pflgs);
extern void xtcln (int32 rlog);
extern d10 map (a10 ea, int32 prv);
extern d10 imul (d10 val, d10 mb);
extern t_bool idiv (d10 val, d10 mb, d10 *rs);
extern void mul (d10 val, d10 mb, d10 *rs);
extern t_bool divi (int32 ac, d10 mb, d10 *rs);
extern t_bool io710 (int32 ac, a10 ea);
extern t_bool io711 (int32 ac, a10 ea);
extern d10 io712 (a10 ea);
extern void io713 (d10 val, a10 ea);
extern void io714 (d10 val, a10 ea);
extern void io715 (d10 val, a10 ea);
extern t_bool io720 (int32 ac, a10 ea);
extern t_bool io721 (int32 ac, a10 ea);
extern d10 io722 (a10 ea);
extern void io723 (d10 val, a10 ea);
extern void io724 (d10 val, a10 ea);
extern void io725 (d10 val, a10 ea);
extern t_bool clrcsh (a10 ea, int32 prv);
extern t_bool clrpt (a10 ea, int32 prv);
extern t_bool wrubr (a10 ea, int32 prv);
extern t_bool wrebr (a10 ea, int32 prv);
extern t_bool wrhsb (a10 ea, int32 prv);
extern t_bool wrspb (a10 ea, int32 prv);
extern t_bool wrcsb (a10 ea, int32 prv);
extern t_bool wrpur (a10 ea, int32 prv);
extern t_bool wrcstm (a10 ea, int32 prv);
extern t_bool ldbr1 (a10 ea, int32 prv);
extern t_bool ldbr2 (a10 ea, int32 prv);
extern t_bool ldbr3 (a10 ea, int32 prv);
extern t_bool ldbr4 (a10 ea, int32 prv);
extern t_bool rdubr (a10 ea, int32 prv);
extern t_bool rdebr (a10 ea, int32 prv);
extern t_bool rdhsb (a10 ea, int32 prv);
extern t_bool rdspb (a10 ea, int32 prv);
extern t_bool rdcsb (a10 ea, int32 prv);
extern t_bool rdpur (a10 ea, int32 prv);
extern t_bool rdcstm (a10 ea, int32 prv);
extern t_bool sdbr1 (a10 ea, int32 prv);
extern t_bool sdbr2 (a10 ea, int32 prv);
extern t_bool sdbr3 (a10 ea, int32 prv);
extern t_bool sdbr4 (a10 ea, int32 prv);
extern t_bool rdtim (a10 ea, int32 prv);
extern t_bool rdint (a10 ea, int32 prv);
extern t_bool wrtim (a10 ea, int32 prv);
extern t_bool wrint (a10 ea, int32 prv);
extern t_bool rdpcst (a10 ea, int32 prv);
extern t_bool wrpcst (a10 ea, int32 prv);
extern t_bool spm (a10 ea, int32 prv);
extern t_bool lpmr (a10 ea, int32 prv);
extern int32 pi_ub_vec (int32 lvl, int32 *uba);
extern t_stat tim_set_mod (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATAD (PC, saved_PC, VASIZE, "program counter") },
    { ORDATAD (FLAGS, flags, 18, "processor flags (<13:17> unused") },
    { ORDATAD (AC0, acs[0], 36, "active register 0") },                       /* addr in memory */
    { ORDATAD (AC1, acs[1], 36, "active register 1") },                       /* modified at exit */
    { ORDATAD (AC2, acs[2], 36, "active register 2") },                       /* to SCP */
    { ORDATAD (AC3, acs[3], 36, "active register 3") },
    { ORDATAD (AC4, acs[4], 36, "active register 4") },
    { ORDATAD (AC5, acs[5], 36, "active register 5") },
    { ORDATAD (AC6, acs[6], 36, "active register 6") },
    { ORDATAD (AC7, acs[7], 36, "active register 7") },
    { ORDATAD (AC10, acs[10], 36, "active register 10") },
    { ORDATAD (AC11, acs[11], 36, "active register 11") },
    { ORDATAD (AC12, acs[12], 36, "active register 12") },
    { ORDATAD (AC13, acs[13], 36, "active register 13") },
    { ORDATAD (AC14, acs[14], 36, "active register 14") },
    { ORDATAD (AC15, acs[15], 36, "active register 15") },
    { ORDATAD (AC16, acs[16], 36, "active register 16") },
    { ORDATAD (AC17, acs[17], 36, "active register 17") },
    { ORDATAD (PFW, pager_word, 36, "pager word register") },
    { ORDATAD (EBR, ebr, EBR_N_EBR, "executive base register") },
    { FLDATAD (PGON, ebr, EBR_V_PGON, "paging enabled flag") },
    { FLDATAD (T20P, ebr, EBR_V_T20P, "TOPS-20 paging") },
    { ORDATAD (UBR, ubr, 36, "user base register") },
    { GRDATAD (CURAC, ubr, 8, 3, UBR_V_CURAC, "current AC block"), REG_RO },
    { GRDATAD (PRVAC, ubr, 8, 3, UBR_V_PRVAC, "previous AC block") },
    { ORDATAD (SPT, spt, 36, "shared pointer table") },
    { ORDATAD (CST, cst, 36, "core status table") },
    { ORDATAD (PUR, pur, 36, "process update register") },
    { ORDATAD (CSTM, cstm, 36, "CST mask") },
    { ORDATAD (HSB, hsb, 36, "halt status block address") },
    { ORDATAD (DBR1, dbr1, PASIZE, "descriptor base register 1 (ITS)") },
    { ORDATAD (DBR2, dbr2, PASIZE, "descriptor base register 2 (ITS)") },
    { ORDATAD (DBR3, dbr3, PASIZE, "descriptor base register 3 (ITS)") },
    { ORDATAD (DBR4, dbr4, PASIZE, "descriptor base register 4 (ITS)") },
    { ORDATAD (PCST, pcst, 36, "ITS PC sampling register") }, 
    { ORDATAD (PIENB, pi_enb, 7, "PI levels enabled") },
    { FLDATAD (PION, pi_on, 0, "PI system enable") },
    { ORDATAD (PIACT, pi_act, 7, "PI levels active") },
    { ORDATAD (PIPRQ, pi_prq, 7, "PI levels with program requests") },
    { ORDATAD (PIIOQ, pi_ioq, 7, "PI levels with I/O requests"), REG_RO },
    { ORDATAD (PIAPR, pi_apr, 7, "PI levels with APR requests"), REG_RO },
    { ORDATAD (APRENB, apr_enb, 8, "APR flags enabled") },
    { ORDATAD (APRFLG, apr_flg, 8, "APR flags active") },
    { ORDATAD (APRLVL, apr_lvl, 3, "PI level for APR interrupt") },
    { ORDATAD (RLOG, rlog, 10, "extend fix up log") },
    { FLDATAD (F1PR, its_1pr, 0, "ITS 1-proceed") },
    { BRDATAD (PCQ, pcq, 8, VASIZE, PCQ_SIZE, "PC prior to last jump or interrupt;                                             most recent PC change first"), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { DRDATAD (INDMAX, ind_max, 8, "indirect address nesting limit; if 0, no limit"), PV_LEFT },
    { DRDATAD (XCTMAX, xct_max, 8, "execute chaining limit; if 0, no limit"), PV_LEFT },
    { ORDATAD (WRU, sim_int_char, 8, "interrupt character") },
    { FLDATA (STOP_ILL, stop_op0, 0) },
    { BRDATAD (REG, acs, 8, 36, AC_NUM * AC_NBLK, "register sets") },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, 0,         "TOPS-10",         "TOPS-10", &tim_set_mod },
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, 0,         NULL     ,         "TOPS10",  &tim_set_mod },
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, UNIT_T20,  "TOPS-20",         "TOPS-20", &tim_set_mod },
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, UNIT_T20,  NULL,              "TOPS20",  &tim_set_mod },
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, UNIT_ITS, "ITS",              "ITS",     &tim_set_mod },
    { UNIT_KLAD+UNIT_ITS+UNIT_T20, UNIT_KLAD, "diagnostic mode", "KLAD",    &tim_set_mod },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "IOSPACE", NULL,
      NULL, &show_iospace },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "SERIAL", "SERIAL", &cpu_set_serial, &cpu_show_serial },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, PASIZE, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

/* Data arrays */
    
const int32 pi_l2bit[8] = {
 0, 0100, 0040, 0020, 0010, 0004, 0002, 0001
 };

const int32 pi_m2lvl[128] = {
 0, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
 };

const d10 bytemask[64] = { 0,
 INT64_C(01), INT64_C(03), INT64_C(07), INT64_C(017), INT64_C(037), INT64_C(077),
 INT64_C(0177), INT64_C(0377), INT64_C(0777), INT64_C(01777), INT64_C(03777), INT64_C(07777),
 INT64_C(017777), INT64_C(037777), INT64_C(077777),
 INT64_C(0177777), INT64_C(0377777), INT64_C(0777777),
 INT64_C(01777777), INT64_C(03777777), INT64_C(07777777),
 INT64_C(017777777), INT64_C(037777777), INT64_C(077777777),
 INT64_C(0177777777), INT64_C(0377777777), INT64_C(0777777777),
 INT64_C(01777777777), INT64_C(03777777777), INT64_C(07777777777),
 INT64_C(017777777777), INT64_C(037777777777), INT64_C(077777777777),
 INT64_C(0177777777777), INT64_C(0377777777777), INT64_C(0777777777777),
 ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES,
 ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES,
 ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES, ONES
 };

static t_bool (*io700d[16])(a10, int32) = {
    &aprid, NULL, NULL, NULL, &wrapr, &rdapr, &czapr, &coapr,
    NULL, NULL, NULL, NULL, &wrpi, &rdpi, &czpi, &copi
    };
static t_bool (*io701d[16])(a10, int32) = {
    NULL, &rdubr, &clrpt, &wrubr, &wrebr, &rdebr, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
    };
static t_bool (*io702d[16])(a10, int32) = {
    &rdspb, &rdcsb, &rdpur, &rdcstm, &rdtim, &rdint, &rdhsb, NULL,
    &wrspb, &wrcsb, &wrpur, &wrcstm, &wrtim, &wrint, &wrhsb, NULL
    };
#define io700i io700d
static t_bool (*io701i[16])(a10, int32) = {
    &clrcsh, &rdubr, &clrpt, &wrubr, &wrebr, &rdebr, NULL, NULL,
    NULL, &rdpcst, NULL, &wrpcst, NULL, NULL, NULL, NULL
    };
static t_bool (*io702i[16])(a10, int32) = {
    &sdbr1, &sdbr2, &sdbr3, &sdbr4, &rdtim, &rdint, &rdhsb, &spm,
    &ldbr1, &ldbr2, &ldbr3, &ldbr4, &wrtim, &wrint, &wrhsb, &lpmr
    };

/* JRST classes and validation table */

#define JRST_U          1                               /* ok anywhere */
#define JRST_E          2                               /* ok exec mode */
#define JRST_UIO        3                               /* ok user I/O mode */
    
static t_stat jrst_tab[16] = {
    JRST_U, JRST_U, JRST_U, 0, JRST_E, JRST_U, JRST_E, JRST_E,
    JRST_UIO, 0, JRST_UIO, 0, JRST_E, JRST_U, 0, 0
    };

/* Address operations */

#define IM              ((d10) ea)
#define IMS             (((d10) ea) << 18)
#define JUMP(x)         PCQ_ENTRY, PC = ((a10) (x)) & AMASK
#define SUBJ(x)         CLRF (F_AFI | F_FPD | F_TR); JUMP (x)
#define INCPC           PC = INCA (PC)

/* AC operations */

#define AOBAC           AC(ac) = AOB (AC(ac))
#define SOBAC           AC(ac) = SOB (AC(ac))
#define G2AC            rs[0] = AC(ac), rs[1] = AC(P1)
#define S1AC            AC(ac) = rs[0]
#define S2AC            S1AC, AC(P1) = rs[1]
#define LAC             if (ac) AC(ac) = mb

/* Memory operations */

#define RD              mb = Read (ea, MM_OPND)
#define RDAC            AC(ac) = Read (ea, MM_OPND)
#define RM              mb = ReadM (ea, MM_OPND)
#define RMAC            AC(ac) = ReadM (ea, MM_OPND)
#define RDP             mb = Read (((a10) AC(ac)) & AMASK, MM_BSTK)
#define RD2             rs[0] = Read (ea, MM_OPND); \
                        rs[1] = Read (INCA (ea), MM_OPND)
#define WR              Write (ea, mb, MM_OPND)
#define WRAC            Write (ea, AC(ac), MM_OPND)
#define WRP(x)          Write (((a10) INCA (AC(ac))), (x), MM_BSTK)
#define WR1             Write (ea, rs[0], MM_OPND)
#define WR2             ReadM (INCA (ea), MM_OPND); \
                        Write (ea, rs[0], MM_OPND); \
                        Write (INCA (ea), rs[1], MM_OPND)

/* Tests and compares */

#define TL(a)           (TSTS (a) != 0)
#define TE(a)           ((a) == 0)
#define TLE(a)          (TL (a) || TE (a))
#define TGE(a)          (TSTS (a) == 0)
#define TN(a)           ((a) != 0)
#define TG(a)           (TGE (a) && TN (a))
#define CL(a)           ((TSTS (AC(ac) ^ a))? (a < AC(ac)): (AC(ac) < a))
#define CE(a)           (AC(ac) == a)
#define CLE(a)          (CL (a) || CE (a))
#define CGE(a)          (!CL (a))
#define CN(a)           (AC(ac) != a)
#define CG(a)           (CGE (a) && CN (a))

/* Word assemblies */

#define FLPC            XWD (flags, PC)
#define UUOWORD         (((d10) op) << INST_V_OP) | (((d10) ac) << INST_V_AC) | ea
#define APRHWORD        ((apr_flg << APR_V_FLG) | (apr_lvl & APR_M_LVL) | \
                        ((apr_flg & apr_enb)? APR_IRQ: 0))
#define APRWORD         ((apr_enb << (APR_V_FLG + 18)) | APRHWORD)
#define PIHWORD         ((pi_act << PI_V_ACT) | (pi_on << PI_V_ON) | \
                        (pi_enb << PI_V_ENB))
#define PIWORD          ((pi_prq << PI_V_PRQ) | PIHWORD)

/* Instruction operations */

#define CIBP            if (!TSTF (F_FPD)) { ibp (ea, pflgs); SETF (F_FPD); }
#define LDB             AC(ac) = ldb (ea, pflgs)
#define DPB             dpb (AC(ac), ea, pflgs)
#define FAD(s)          fad (AC(ac), s, FALSE, 0)
#define FADR(s)         fad (AC(ac), s, TRUE, 0)
#define FSB(s)          fad (AC(ac), s, FALSE, 1)
#define FSBR(s)         fad (AC(ac), s, TRUE, 1)
#define FMP(s)          fmp (AC(ac), s, FALSE)
#define FMPR(s)         fmp (AC(ac), s, TRUE)
#define FDV(s)          fdv (AC(ac), s, rs, FALSE)
#define FDVR(s)         fdv (AC(ac), s, rs, TRUE)
#define MOVN(s)         NEG (s); MOVNF(s)
#define MOVM(s)         ABS (s); MOVMF(s)
#define ADD(s)          add (AC(ac), s)
#define SUB(s)          sub (AC(ac), s)
#define IMUL(s)         imul (AC(ac), s)
#define IDIV(s)         idiv (AC(ac), s, rs)
#define MUL(s)          mul (AC(ac), s, rs)
#define DIV(s)          divi (ac, s, rs)
#define AOJ             AC(ac) = INC (AC(ac)); INCF (AC(ac))
#define AOS             RM; mb = INC (mb); WR; INCF (mb); LAC
#define SOJ             AC(ac) = DEC (AC(ac)); DECF (AC(ac))
#define SOS             RM; mb = DEC (mb); WR; DECF (mb); LAC
#define SETCA(s)        ~AC(ac) & DMASK
#define SETCM(s)        ~(s) & DMASK;
#define AND(s)          AC(ac) & (s)
#define ANDCA(s)        ~AC(ac) & (s)
#define ANDCM(s)        AC(ac) & ~(s)
#define ANDCB(s)        (~AC(ac) & ~(s)) & DMASK
#define IOR(s)          AC(ac) | (s)
#define ORCA(s)         (~AC(ac) | (s)) & DMASK
#define ORCM(s)         (AC(ac) | ~(s)) & DMASK
#define ORCB(s)         (~AC(ac) | ~(s)) & DMASK
#define XOR(s)          AC(ac) ^ (s)
#define EQV(s)          (~(AC(ac) ^ (s))) & DMASK
#define LL(s,d)         ((s) & LMASK) | ((d) & RMASK)
#define RL(s,d)         (((s) << 18) & LMASK) | ((d) & RMASK)
#define RR(s,d)         ((s) & RMASK) | ((d) & LMASK)
#define LR(s,d)         (((s) >> 18) & RMASK) | ((d) & LMASK)
#define LLO(s)          ((s) & LMASK) | RMASK
#define RLO(s)          (((s) << 18) & LMASK) | RMASK
#define RRO(s)          ((s) & RMASK) | LMASK
#define LRO(s)          (((s) >> 18) & RMASK) | LMASK
#define LLE(s)          ((s) & LMASK) | (((s) & LSIGN)? RMASK: 0)
#define RLE(s)          (((s) << 18) & LMASK) | (((s) & RSIGN)? RMASK: 0)
#define RRE(s)          ((s) & RMASK) | (((s) & RSIGN)? LMASK: 0)
#define LRE(s)          (((s) >> 18) & RMASK) | (((s) & LSIGN)? LMASK: 0)
#define TD_             RD
#define TS_             RD; mb = SWP (mb)
#define TL_             mb = IMS
#define TR_             mb = IM
#define T_Z             AC(ac) = AC(ac) & ~mb
#define T_O             AC(ac) = AC(ac) | mb
#define T_C             AC(ac) = AC(ac) ^ mb
#define T__E            if ((AC(ac) & mb) == 0) INCPC
#define T__N            if ((AC(ac) & mb) != 0) INCPC
#define T__A            INCPC
#define IOC             if (TSTF (F_USR) && !TSTF (F_UIO)) goto MUUO;
#define IO7(x,y)        IOC; fptr = ((Q_ITS)? x[ac]: y[ac]); \
                        if (fptr == NULL) goto MUUO; \
                        if (fptr (ea, MM_OPND)) INCPC; break;
#define IOA             IOC; if (!Q_ITS) ea = calc_ioea (inst, pflgs)
#define IOAM            IOC; ea = ((Q_ITS)? ((a10) Read (ea, MM_OPND)): \
                            calc_ioea (inst, pflgs))

/* Flag tests */

#define MOVNF(x)        if ((x) == MAXNEG) SETF (F_C1 | F_AOV | F_T1); \
                        else if ((x) == 0) SETF (F_C0 | F_C1)
#define MOVMF(x)        if ((x) == MAXNEG) SETF (F_C1 | F_AOV | F_T1)
#define INCF(x)         if ((x) == 0) SETF (F_C0 | F_C1); \
                        else if ((x) == MAXNEG) SETF (F_C1 | F_AOV | F_T1)
#define DECF(x)         if ((x) == MAXPOS) SETF (F_C0 | F_AOV | F_T1); \
                        else if ((x) != ONES) SETF (F_C0 | F_C1)
#define PUSHF           if (LRZ (AC(ac)) == 0) SETF (F_T2)
#define POPF            if (LRZ (AC(ac)) == RMASK) SETF (F_T2)
#define DMOVNF          if (rs[1] == 0) { MOVNF (rs[0]); }

t_value pdp10_pc_value (void)
{
return (t_value)pager_PC;
}

t_stat sim_instr (void)
{
a10 PC;                                                 /* set by setjmp */
int abortval = 0;                                       /* abort value */
t_stat r;

/* Restore register state */

if ((r = build_dib_tab ()) != SCPE_OK)                  /* build, chk dib_tab */
    return r;
pager_PC = PC = saved_PC & AMASK;                       /* load local PC */
set_dyn_ptrs ();                                        /* set up local ptrs */
pager_tc = FALSE;                                       /* not in trap cycle */
pager_pi = FALSE;                                       /* not in pi sequence */
rlog = 0;                                               /* not in extend */
pi_eval ();                                             /* eval pi system */
if (!Q_ITS)                                             /* ~ITS, clr 1-proc */
    its_1pr = 0;

/* Abort handling

   Aborts may come from within the simulator to stop simulation (values > 0),
   for page fails (values < 0), or for an interrupt check (value = 0).
*/

abortval = setjmp (save_env);                           /* set abort hdlr */
if ((abortval > 0) || pager_pi) {                       /* stop or pi err? */
    if (pager_pi && (abortval == PAGE_FAIL))
        abortval = STOP_PAGINT;                         /* stop for pi err */
    saved_PC = pager_PC & AMASK;                        /* failing instr PC */
    set_ac_display (ac_cur);                            /* set up AC display */
    pcq_r->qptr = pcq_p;                                /* update pc q ptr */
    return abortval;                                    /* return to SCP */
    }

/* Page fail - checked against KS10 ucode
   All state variables MUST be declared global for GCC optimization to work
*/

else if (abortval == PAGE_FAIL) {                       /* page fail */
    d10 mb;
    if (rlog)                                           /* clean up extend */
        xtcln (rlog);
    rlog = 0;                                           /* clear log */
    if (pager_tc)                                       /* trap? get flags */
        flags = pager_flags;
    if (T20PAG) {                                       /* TOPS-20 paging? */
        WriteP (upta + UPT_T20_PFL, pager_word);        /* write page fail wd */
        WriteP (upta + UPT_T20_OFL, XWD (flags, 0));
        WriteP (upta + UPT_T20_OPC, pager_PC);
        mb = ReadP (upta + UPT_T20_NPC);
        }
    else {
        a10 ea;                                         /* TOPS-10 or ITS */
        if (Q_ITS) {                                    /* ITS? */
            ea = epta + EPT_ITS_PAG + (pi_m2lvl[pi_act] * 3);
            if (its_1pr)                                /* store 1-proc */
                flags = flags | F_1PR;
            its_1pr = 0;                                /* clear 1-proc */
            }
        else ea = upta + UPT_T10_PAG;
        WriteP (ea, pager_word);                        /* write page fail wd */
        WriteP (ADDA (ea, 1), XWD (flags, pager_PC));
        mb = ReadP (ADDA (ea, 2));
        }
    JUMP (mb);                                          /* set new PC */
    set_newflags (mb, FALSE);                           /* set new flags */
    pi_eval ();                                         /* eval pi system */
    }
else PC = pager_PC;                                     /* intr, restore PC */

/* Main instruction fetch/decode loop: check clock queue, intr, trap, bkpt */

for ( ;; ) {                                            /* loop until ABORT */
int32 op, ac, i, st, xr, xct_cnt, its_2pr, pflgs;
a10 ea;
d10 inst, mb, indrct, rs[2];
t_bool (*fptr)(int32, int32);

pager_PC = PC;                                          /* update pager PC */
pager_tc = FALSE;                                       /* not in trap cycle */
pflgs = 0;                                              /* not in PXCT */
xct_cnt = 0;                                            /* count XCT's */
if (sim_interval <= 0) {                                /* check clock queue */
    /* make sure all useful state is in simh registers while processing events */
    saved_PC = pager_PC & AMASK;                        /* failing instr PC */
    set_ac_display (ac_cur);                            /* set up AC display */
    pcq_r->qptr = pcq_p;                                /* update pc q ptr */

    if ((i = sim_process_event ()))                     /* error?  stop sim */
        ABORT (i);
    if (fe_xct)
        qintr = -1;
    else
        pi_eval ();                                     /* eval pi system */
    }

/* PI interrupt (Unibus or system flags).
   On the KS10, only JSR and XPCW are allowed as interrupt instructions.
   Because of exec mode addressing, and unconditional processing of flags,
   they are explicitly emulated here.  Note that the KS microcode does not
   perform an EA calc on interrupt instructions, which this emulation does.
   This is an implementation restriction of the KS.  The KS does not restrict
   the CONSOLE EXECUTE function which is merged into this path in SimH.  

   On a keep-alive failure, the console (fe) forces the CPU 'XCT' the 
   instruction at exec 71.  This is close enough to an interrupt that it is
   treated as one here.  TOPS-10 and TOPS-20 use JSR or XPCW, which are 
   really the only sensible instructions, as diagnosing a KAF requires the
   PC/FLAGS of the fault.
   On a reload-request from the OS, the fe loads the bootstrap code and sets
   saved_PC.  Here, the CPU is partially reset and redirected.  (Preserving
   PC history, among other things.)  The FE has already reset IO.
*/

if (qintr) {
    int32 vec, uba;
    pager_pi = TRUE;                                    /* flag in pi seq */
    if (fe_xct) {                                       /* Console forced execute? */
        qintr = 0;
        if (fe_xct == 1) {                              /* Forced reload */
            PC = saved_PC;                              /* Bootstrap PC */
            pager_pi = FALSE;
            ebr = ubr = 0;                              /* Exec mode, paging & PI off */
            pag_reset (&pag_dev);
            pi_on = pi_enb = pi_act= pi_prq =
                apr_enb = apr_flg = apr_lvl = its_1pr = 0;
            rlog = 0;
            set_newflags (0, FALSE);
            fe_xct = 0;
            continue;
        }
        inst = ReadE(fe_xct);                           /* Exec address of instruction */
    } else if ((vec = pi_ub_vec (qintr, &uba))) {       /* Unibus interrupt? */
        mb = ReadP (epta + EPT_UBIT + uba);             /* get dispatch table */
        if (mb == 0)                                    /* invalid? stop */
            ABORT (STOP_ZERINT);
        inst = ReadE ((((a10) mb) + (vec / 4)) & AMASK);
        if (inst == 0)
            ABORT (STOP_ZERINT);
        }
    else inst = ReadP (epta + EPT_PIIT + (2 * qintr));
    op = GET_OP (inst);                                 /* get opcode */
    ac = GET_AC (inst);                                 /* get ac */
    if (its_1pr && Q_ITS) {                             /* 1-proc set? */
        flags = flags | F_1PR;                          /* store 1-proc */
        its_1pr = 0;                                    /* clear 1-proc */
        }
    if (op == OP_JSR) {                                 /* JSR? */
        d10 flpc = FLPC;

        set_newflags (0, FALSE);                        /* set new flags */
        ea = calc_ea (inst, MM_CUR);                    /* calc ea, cur mode */
        WriteE (ea, flpc);                              /* save flags+PC, exec */
        JUMP (INCA (ea));                               /* PC = ea + 1 */
        }
    else if ((op == OP_JRST) && (ac == AC_XPCW)) {      /* XPCW? */
        d10 flz = XWD (flags, 0);

        set_newflags (0, FALSE);                        /* set exec flags */
        ea = calc_ea (inst, MM_CUR);                    /* calc ea, cur mode */
        WriteE (ea, flz);                               /* write flags, exec */
        WriteE (ADDA (ea, 1), PC);                      /* write PC, exec */
        rs[0] = ReadE (ADDA (ea, 2));                   /* read new flags */
        rs[1] = ReadE (ADDA (ea, 3));                   /* read new PC */
        JUMP (rs[1]);                                   /* set new PC */
        set_newflags (rs[0], FALSE);                    /* set new flags */
        }
    else {
        fe_xct = 0;
        ABORT (STOP_ILLINT);                            /* invalid instr */
        }
    if (fe_xct)
        fe_xct = 0;
    else {
        pi_act = pi_act | pi_l2bit[qintr];              /* set level active */
        pi_eval ();                                     /* eval pi system */
        }
    pager_pi = FALSE;                                   /* end of sequence */
    if (sim_interval)                                   /* charge for instr */
        sim_interval--;
    continue;
    }                                                   /* end if interrupt */
            
/* Traps fetch and execute an instruction from the current mode process table.
   On the KS10, the fetch of the next instruction has started, and a page fail
   trap on the instruction fetch takes precedence over the trap.  During a trap,
   flags are cleared before the execute, but if the execute aborts, they must
   be restored.  Also, the MUUO processor needs to know whether we are in a
   trap sequence.  Hence, trap in progress is recorded in pflgs, and the
   traps for pager restoration are recorded in pager_flags.
*/

if (TSTF (F_T1 | F_T2) && PAGING) {
    Read (pager_PC = PC, MM_CUR);                       /* test fetch */
    pager_tc = TRUE;                                    /* in a trap sequence */
    pager_flags = flags;                                /* save flags */
    ea = (TSTF (F_USR)? upta + UPT_TRBASE: epta + EPT_TRBASE)
        + GET_TRAPS (flags);
    inst = ReadP (ea);                                  /* get trap instr */
    CLRF (F_T1 | F_T2);                                 /* clear flags */
    }

/* Test for instruction breakpoint */

else {
    if (sim_brk_summ &&
        sim_brk_test (PC, SWMASK ('E'))) {              /* breakpoint? */
        ABORT (STOP_IBKPT);                             /* stop simulation */
        }

/* Ready (at last) to get an instruction */

    inst = Read (pager_PC = PC, MM_CUR);                /* get instruction */
    INCPC;  
    sim_interval = sim_interval - 1;
    }

its_2pr = its_1pr;                                      /* save 1-proc flag */

/* Execute instruction.  XCT and PXCT also return here. */

XCT:
op = GET_OP (inst);                                     /* get opcode */
ac = GET_AC (inst);                                     /* get AC */
for (indrct = inst, i = 0; ; i++) {                     /* calc eff addr */
    ea = GET_ADDR (indrct);
    xr = GET_XR (indrct);
    if (xr)
        ea = (ea + ((a10) XR (xr, MM_EA))) & AMASK;
    if (TST_IND (indrct)) {                             /* indirect? */
        if (i != 0) {                                   /* not first cycle? */
            int32 t = test_int ();                      /* test for intr */
            if (t != 0)                                 /* err or intr? */
                ABORT (t);
            if ((ind_max != 0) && (i >= ind_max))       /* limit exceeded? */
                ABORT (STOP_IND);
            }
        indrct = Read (ea, MM_EA);                      /* fetch indirect */
        }
    else break;
    }
if (hst_lnt) {                                          /* history enabled? */
    hst_p = (hst_p + 1);                                /* next entry */
    if (hst_p >= hst_lnt)
        hst_p = 0;
    hst[hst_p].pc = pager_PC | HIST_PC;
    hst[hst_p].ea = ea;
    hst[hst_p].ir = inst;
    hst[hst_p].ac = AC(ac);
    }
switch (op) {                                           /* case on opcode */

/* UUO's (0000 - 0077) - checked against KS10 ucode */

case 0000:  if (stop_op0) {
                ABORT (STOP_ILLEG);
                }
            goto MUUO;
case 0001:                                              /* local UUO's */
case 0002:
case 0003:
case 0004:
case 0005:
case 0006:
case 0007:
case 0010:
case 0011:
case 0012:
case 0013:
case 0014:
case 0015:
case 0016:
case 0017:
case 0020:
case 0021:
case 0022:
case 0023:
case 0024:
case 0025:
case 0026:
case 0027:
case 0030:
case 0031:
case 0032:
case 0033:
case 0034:
case 0035:
case 0036:
case 0037:  Write (040, UUOWORD, MM_CUR);               /* store op, ac, ea */
            inst = Read (041, MM_CUR);                  /* get new instr */
            goto XCT;

/* case 0040 - 0077: MUUO's, handled by default at end of case */

/* Floating point, bytes, multiple precision (0100 - 0177) */

/* case 0100:   MUUO                                  *//* UJEN */
/* case 0101:   MUUO                                  *//* unassigned */
case 0102:  if (Q_ITS && !TSTF (F_USR)) {               /* GFAD (KL), XCTRI (ITS) */
                inst = Read (ea, MM_OPND);
                pflgs = pflgs | ac;
                goto XCT;
                }
            goto MUUO;
case 0103:  if (Q_ITS && !TSTF (F_USR)) {               /* GFSB (KL), XCTR (ITS) */
                inst = Read (ea, MM_OPND);
                pflgs = pflgs | ac;
                goto XCT;
                }
            goto MUUO;
/* case 0104:   MUUO                                  *//* JSYS (T20) */
case 0105:  AC(ac) = adjsp (AC(ac), ea); break;         /* ADJSP */
/* case 0106:   MUUO                                  *//* GFMP (KL)*/
/* case 0107:   MUUO                                  *//* GFDV (KL) */
case 0110:  RD2; dfad (ac, rs, 0); break;               /* DFAD */
case 0111:  RD2; dfad (ac, rs, 1); break;               /* DFSB */
case 0112:  RD2; dfmp (ac, rs); break;                  /* DFMP */
case 0113:  RD2; dfdv (ac, rs); break;                  /* DFDV */
case 0114:  RD2; dadd (ac, rs); break;                  /* DADD */
case 0115:  RD2; dsub (ac, rs); break;                  /* DSUB */
case 0116:  RD2; dmul (ac, rs); break;                  /* DMUL */
case 0117:  RD2; ddiv (ac, rs); break;                  /* DDIV */
case 0120:  RD2; S2AC; break;                           /* DMOVE */
case 0121:  RD2; DMOVN (rs); S2AC; DMOVNF; break;       /* DMOVN */
case 0122:  RD; fix(ac, mb, 0); break;                  /* FIX */
case 0123:  st = xtend (ac, ea, pflgs);                 /* EXTEND */
            rlog = 0;                                   /* clear log */
            switch (st) {
            case XT_SKIP:
                INCPC;
            case XT_NOSK:
                break;
            default:
                goto MUUO;
                }
            break;
case 0124:  G2AC; WR2; break;                           /* DMOVEM */
case 0125:  G2AC; DMOVN (rs); WR2; DMOVNF; break;       /* DMOVNM */
case 0126:  RD; fix (ac, mb, 1); break;                 /* FIXR */
case 0127:  RD; AC(ac) = fltr (mb); break;              /* FLTR */
/* case 0130:   MUUO                                  *//* UFA */
/* case 0131:   MUUO                                  *//* DFN */
case 0132:  AC(ac) = fsc (AC(ac), ea); break;           /* FSC */
case 0133:  if (!ac)                                    /* IBP */
                ibp (ea, pflgs);
            else adjbp (ac, ea, pflgs); break;
case 0134:  CIBP; LDB; CLRF (F_FPD); break;             /* ILBP */
case 0135:  LDB; break;                                 /* LDB */
case 0136:  CIBP; DPB; CLRF (F_FPD); break;             /* IDBP */
case 0137:  DPB; break;                                 /* DPB */
case 0140:  RD; AC(ac) = FAD (mb); break;               /* FAD */
/* case 0141:   MUUO                                  *//* FADL */
case 0142:  RM; mb = FAD (mb); WR; break;               /* FADM */
case 0143:  RM; AC(ac) = FAD (mb); WRAC; break;         /* FADB */
case 0144:  RD; AC(ac) = FADR (mb); break;              /* FADR */
case 0145:  AC(ac) = FADR (IMS); break;                 /* FADRI */
case 0146:  RM; mb = FADR (mb); WR; break;              /* FADRM */
case 0147:  RM; AC(ac) = FADR (mb); WRAC; break;        /* FADRB */
case 0150:  RD; AC(ac) = FSB (mb); break;               /* FSB */
/* case 0151:   MUUO                                  *//* FSBL */
case 0152:  RM; mb = FSB (mb); WR; break;               /* FSBM */
case 0153:  RM; AC(ac) = FSB (mb); WRAC; break;         /* FSBB */
case 0154:  RD; AC(ac) = FSBR (mb); break;              /* FSBR */
case 0155:  AC(ac) = FSBR (IMS);  break;                /* FSBRI */
case 0156:  RM; mb = FSBR (mb); WR; break;              /* FSBRM */
case 0157:  RM; AC(ac) = FSBR (mb); WRAC; break;        /* FSBRB */
case 0160:  RD; AC(ac) = FMP (mb); break;               /* FMP */
/* case 0161:   MUUO                                  *//* FMPL */
case 0162:  RM; mb = FMP (mb); WR; break;               /* FMPM */
case 0163:  RM; AC(ac) = FMP (mb); WRAC; break;         /* FMPB */
case 0164:  RD; AC(ac) = FMPR (mb); break;              /* FMPR */
case 0165:  AC(ac) = FMPR (IMS); break;                 /* FMPRI */
case 0166:  RM; mb = FMPR (mb); WR; break;              /* FMPRM */
case 0167:  RM; AC(ac) = FMPR (mb); WRAC; break;        /* FMPRB */
case 0170:  RD; if (FDV (mb)) S1AC; break;              /* FDV */
/* case 0171:   MUUO                                  *//* FDVL */
case 0172:  RM; if (FDV (mb)) WR1; break;               /* FDVM */
case 0173:  RM; if (FDV (mb)) { S1AC; WRAC; } break;    /* FDVB */
case 0174:  RD; if (FDVR (mb)) S1AC; break;             /* FDVR */
case 0175:  if (FDVR (IMS)) S1AC; break;                /* FDVRI */
case 0176:  RM; if (FDVR (mb)) WR1; break;              /* FDVRM */
case 0177:  RM; if (FDVR (mb)) { S1AC; WRAC; } break;   /* FDVRB */

/* Move, arithmetic, shift, and jump (0200 - 0277)

   Note that instructions which modify the flags and store a
   result in memory must prove the writeability of the result
   location before modifying the flags.  Also, 0247 and 0257,
   if not implemented, are nops, not MUUO's.
*/

case 0200:  RDAC; break;                                /* MOVE */
case 0201:  AC(ac) = ea; break;                         /* MOVEI */
case 0202:  WRAC; break;                                /* MOVEM */
case 0203:  RM; LAC; break;                             /* MOVES */
case 0204:  RD; AC(ac) = SWP (mb); break;               /* MOVS */
case 0205:  AC(ac) = IMS; break;                        /* MOVSI */
case 0206:  mb = SWP (AC(ac)); WR; break;               /* MOVSM */
case 0207:  RM; mb = SWP (mb); WR; LAC; break;          /* MOVSS */
case 0210:  RD; AC(ac) = MOVN (mb); break;              /* MOVN */
case 0211:  AC(ac) = NEG (IM);                          /* MOVNI */
            if (AC(ac) == 0) SETF (F_C0 | F_C1);
            break;
case 0212:  RM; mb = MOVN (AC(ac)); WR; break;          /* MOVNM */
case 0213:  RM; mb = MOVN (mb); WR; LAC; break;         /* MOVNS */
case 0214:  RD; AC(ac) = MOVM (mb); break;              /* MOVM */
case 0215:  AC(ac) = ea; break;                         /* MOVMI */
case 0216:  RM; mb = MOVM (AC(ac)); WR; break;          /* MOVMM */
case 0217:  RM; mb = MOVM (mb); WR; LAC; break;         /* MOVMS */
case 0220:  RD; AC(ac) = IMUL (mb); break;              /* IMUL */
case 0221:  AC(ac) = IMUL (IM); break;                  /* IMULI */
case 0222:  RM; mb = IMUL (mb); WR; break;              /* IMULM */
case 0223:  RM; AC(ac) = IMUL (mb); WRAC; break;        /* IMULB */
case 0224:  RD; MUL (mb); S2AC; break;                  /* MUL */
case 0225:  MUL (IM); S2AC; break;                      /* MULI */
case 0226:  RM; MUL (mb); WR1; break;                   /* MULM */
case 0227:  RM; MUL (mb); WR1; S2AC; break;             /* MULB */
case 0230:  RD; if (IDIV (mb)) S2AC; break;             /* IDIV */
case 0231:  if (IDIV (IM)) S2AC; break;                 /* IDIVI */
case 0232:  RM; if (IDIV (mb)) WR1; break;              /* IDIVM */
case 0233:  RM; if (IDIV (mb)) { WR1; S2AC; } break;    /* IDIVB */
case 0234:  RD; if (DIV (mb)) S2AC; break;              /* DIV */
case 0235:  if (DIV (IM)) S2AC; break;                  /* DIVI */
case 0236:  RM; if (DIV (mb)) WR1; break;               /* DIVM */
case 0237:  RM; if (DIV (mb)) { WR1; S2AC; } break;     /* DIVB */
case 0240:  AC(ac) = ash (AC(ac), ea); break;           /* ASH */
case 0241:  AC(ac) = rot (AC(ac), ea); break;           /* ROT */
case 0242:  AC(ac) = lsh (AC(ac), ea); break;           /* LSH */
case 0243:  AC(P1) = jffo (AC(ac));                     /* JFFO */
            if (AC(ac)) JUMP (ea);
            break;
case 0244:  ashc (ac, ea); break;                       /* ASHC */
case 0245:  rotc (ac, ea); break;                       /* ROTC */
case 0246:  lshc (ac, ea); break;                       /* LSHC */
case 0247:  if (Q_ITS) circ (ac, ea); break;            /* (ITS) CIRC */
case 0250:  RM; WRAC; AC(ac) = mb; break;               /* EXCH */
case 0251:  blt (ac, ea, pflgs); break;                 /* BLT */
case 0252:  AOBAC; if (TGE (AC(ac))) JUMP (ea); break;  /* AOBJP */
case 0253:  AOBAC; if (TL (AC(ac))) JUMP (ea); break;   /* AOBJN */
/* case 0254: *//* shown later                        *//* JRST */
case 0255:  if (flags & (ac << 14)) {                   /* JFCL */
                JUMP (ea);
                CLRF (ac << 14);
                }
            break;
case 0256:  if (xct_cnt++ != 0) {                       /* XCT: not first? */
                int32 t = test_int ();                  /* test for intr */
                if (t != 0)                             /* intr or err? */
                    ABORT (t);
                 if ((xct_max != 0) && (xct_cnt >= xct_max))
                    ABORT (STOP_XCT);
                }
            inst = Read (ea, MM_OPND);                  /* get opnd */
            if (ac && !TSTF (F_USR) && !Q_ITS)          /* PXCT? */
                pflgs = pflgs | ac;
            goto XCT;
case 0257:  if (Q_ITS) goto MUUO;                       /* MAP */
            AC(ac) = map (ea, MM_OPND);
            break;
case 0260:  WRP (FLPC); AOBAC;                          /* PUSHJ */
            SUBJ (ea); PUSHF; break;
case 0261:  RD; WRP (mb); AOBAC; PUSHF; break;          /* PUSH */
case 0262:  RDP; WR; SOBAC; POPF; break;                /* POP */
case 0263:  RDP; JUMP (mb); SOBAC; POPF; break;         /* POPJ */
case 0264:  Write (ea, FLPC, MM_OPND);                  /* JSR */
            SUBJ (INCR (ea)); break;
case 0265:  AC(ac) = FLPC; SUBJ (ea); break;            /* JSP */
case 0266:  WRAC; AC(ac) = XWD (ea, PC);                /* JSA */
            JUMP (INCR (ea)); break;
case 0267:  AC(ac) = Read ((a10) LRZ (AC(ac)), MM_OPND);/* JRA */
            JUMP (ea); break;
case 0270:  RD; AC(ac) = ADD (mb); break;               /* ADD */
case 0271:  AC(ac) = ADD (IM); break;                   /* ADDI */
case 0272:  RM; mb = ADD (mb); WR; break;               /* ADDM */
case 0273:  RM; AC(ac) = ADD (mb); WRAC; break;         /* ADDB */
case 0274:  RD; AC(ac) = SUB (mb); break;               /* SUB */
case 0275:  AC(ac) = SUB (IM); break;                   /* SUBI */
case 0276:  RM; mb = SUB (mb); WR; break;               /* SUBM */
case 0277:  RM; AC(ac) = SUB (mb); WRAC; break;         /* SUBB */

/* Compare, jump, skip instructions (0300 - 0377) - checked against KS10 ucode */

case 0300:  break;                                      /* CAI */
case 0301:  if (CL (IM)) INCPC; break;                  /* CAIL */
case 0302:  if (CE (IM)) INCPC; break;                  /* CAIE */
case 0303:  if (CLE (IM)) INCPC; break;                 /* CAILE */
case 0304:  INCPC; break;                               /* CAIA */
case 0305:  if (CGE (IM)) INCPC; break;                 /* CAIGE */
case 0306:  if (CN (IM)) INCPC; break;                  /* CAIN */
case 0307:  if (CG (IM)) INCPC; break;                  /* CAIG */
case 0310:  RD; break;                                  /* CAM */
case 0311:  RD; if (CL (mb)) INCPC; break;              /* CAML */
case 0312:  RD; if (CE (mb)) INCPC; break;              /* CAME */
case 0313:  RD; if (CLE (mb)) INCPC; break;             /* CAMLE */
case 0314:  RD; INCPC; break;                           /* CAMA */
case 0315:  RD; if (CGE (mb)) INCPC; break;             /* CAMGE */
case 0316:  RD; if (CN (mb)) INCPC; break;              /* CAMN */
case 0317:  RD; if (CG (mb)) INCPC; break;              /* CAMG */
case 0320:  break;                                      /* JUMP */
case 0321:  if (TL (AC(ac))) JUMP (ea); break;          /* JUMPL */
case 0322:  if (TE (AC(ac))) JUMP (ea); break;          /* JUMPE */
case 0323:  if (TLE( AC(ac))) JUMP (ea); break;         /* JUMPLE */
case 0324:  JUMP (ea); break;                           /* JUMPA */
case 0325:  if (TGE (AC(ac))) JUMP (ea); break;         /* JUMPGE */
case 0326:  if (TN (AC(ac))) JUMP (ea); break;          /* JUMPN */
case 0327:  if (TG (AC(ac))) JUMP (ea); break;          /* JUMPG */
case 0330:  RD; LAC; break;                             /* SKIP */
case 0331:  RD; LAC; if (TL (mb)) INCPC; break;         /* SKIPL */
case 0332:  RD; LAC; if (TE (mb)) INCPC; break;         /* SKIPE */
case 0333:  RD; LAC; if (TLE (mb)) INCPC; break;        /* SKIPLE */
case 0334:  RD; LAC; INCPC; break;                      /* SKIPA */
case 0335:  RD; LAC; if (TGE (mb)) INCPC; break;        /* SKIPGE */
case 0336:  RD; LAC; if (TN (mb)) INCPC; break;         /* SKIPN */
case 0337:  RD; LAC; if (TG (mb)) INCPC; break;         /* SKIPG */
case 0340:  AOJ; break;                                 /* AOJ */
case 0341:  AOJ; if (TL (AC(ac))) JUMP (ea); break;     /* AOJL */
case 0342:  AOJ; if (TE (AC(ac))) JUMP (ea); break;     /* AOJE */
case 0343:  AOJ; if (TLE (AC(ac))) JUMP (ea); break;    /* AOJLE */
case 0344:  AOJ; JUMP(ea);                              /* AOJA */
            if (Q_ITS &&                                /* ITS idle? */
                TSTF (F_USR) && (pager_PC == 017) &&    /* user mode, loc 17? */
                (ac == 0) && (ea == 017))               /* AOJA 0,17? */
                sim_idle (0, FALSE);
            break;
case 0345:  AOJ; if (TGE (AC(ac))) JUMP (ea); break;    /* AOJGE */
case 0346:  AOJ; if (TN (AC(ac))) JUMP (ea); break;     /* AOJN */
case 0347:  AOJ; if (TG (AC(ac))) JUMP (ea); break;     /* AOJG */
case 0350:  AOS; break;                                 /* AOS */
case 0351:  AOS; if (TL (mb)) INCPC; break;             /* AOSL */
case 0352:  AOS; if (TE (mb)) INCPC; break;             /* AOSE */
case 0353:  AOS; if (TLE (mb)) INCPC; break;            /* AOSLE */
case 0354:  AOS; INCPC; break;                          /* AOSA */
case 0355:  AOS; if (TGE (mb)) INCPC; break;            /* AOSGE */
case 0356:  AOS; if (TN (mb)) INCPC; break;             /* AOSN */
case 0357:  AOS; if (TG (mb)) INCPC; break;             /* AOSG */
case 0360:  SOJ; break;                                 /* SOJ */
case 0361:  SOJ; if (TL (AC(ac))) JUMP (ea); break;     /* SOJL */
case 0362:  SOJ; if (TE (AC(ac))) JUMP (ea); break;     /* SOJE */
case 0363:  SOJ; if (TLE (AC(ac))) JUMP (ea); break;    /* SOJLE */
case 0364:  SOJ; JUMP(ea); break;                       /* SOJA */
case 0365:  SOJ; if (TGE (AC(ac))) JUMP (ea); break;    /* SOJGE */
case 0366:  SOJ; if (TN (AC(ac))) JUMP (ea); break;     /* SOJN */
case 0367:  SOJ; if (TG (AC(ac))) JUMP (ea);            /* SOJG */
            if (ea == pager_PC) {                       /* to self? */
                if ((ac == 6) && (ea == 1) &&           /* SOJG 6,1? */
                    TSTF (F_USR) && Q_T10)              /* T10, user mode? */
                    sim_idle (0, FALSE);                /* idle */
                else if ((ac == 2) && (ea == 3) &&      /* SOJG 2,3? */
                    !TSTF (F_USR) && Q_T20)             /* T20, mon mode? */
                    sim_idle (0, FALSE);                /* idle */
                }                    
            break;
case 0370:  SOS; break;                                 /* SOS */
case 0371:  SOS; if (TL (mb)) INCPC; break;             /* SOSL */
case 0372:  SOS; if (TE (mb)) INCPC; break;             /* SOSE */
case 0373:  SOS; if (TLE (mb)) INCPC; break;            /* SOSLE */
case 0374:  SOS; INCPC; break;                          /* SOSA */
case 0375:  SOS; if (TGE (mb)) INCPC; break;            /* SOSGE */
case 0376:  SOS; if (TN (mb)) INCPC; break;             /* SOSN */
case 0377:  SOS; if (TG (mb)) INCPC; break;             /* SOSG */

/* Boolean instructions (0400 - 0477) - checked against KS10 ucode

   Note that for boolean B, the initial read checks writeability of
   the memory operand; hence, it is safe to modify the AC.
*/

case 0400:  AC(ac) = 0; break;                          /* SETZ */
case 0401:  AC(ac) = 0; break;                          /* SETZI */
case 0402:  mb = 0; WR; break;                          /* SETZM */
case 0403:  mb = 0; WR; AC(ac) = 0; break;              /* SETZB */
case 0404:  RD; AC(ac) = AND (mb); break;               /* AND */
case 0405:  AC(ac) = AND (IM); break;                   /* ANDI */
case 0406:  RM; mb = AND (mb); WR; break;               /* ANDM */
case 0407:  RM; AC(ac) = AND (mb); WRAC; break;         /* ANDB */
case 0410:  RD; AC(ac) = ANDCA (mb); break;             /* ANDCA */
case 0411:  AC(ac) = ANDCA (IM); break;                 /* ANDCAI */
case 0412:  RM; mb = ANDCA (mb); WR; break;             /* ANDCAM */
case 0413:  RM; AC(ac) = ANDCA (mb); WRAC; break;       /* ANDCAB */
case 0414:  RDAC; break;                                /* SETM */
case 0415:  AC(ac) = ea; break;                         /* SETMI */
case 0416:  RM; WR; break;                              /* SETMM */
case 0417:  RMAC; WRAC; break;                          /* SETMB */
case 0420:  RD; AC(ac) = ANDCM (mb); break;             /* ANDCM */
case 0421:  AC(ac) = ANDCM (IM); break;                 /* ANDCMI */
case 0422:  RM; mb = ANDCM (mb); WR; break;             /* ANDCMM */
case 0423:  RM; AC(ac) = ANDCM (mb); WRAC; break;       /* ANDCMB */
case 0424:  break;                                      /* SETA */
case 0425:  break;                                      /* SETAI */
case 0426:  WRAC; break;                                /* SETAM */
case 0427:  WRAC; break;                                /* SETAB */
case 0430:  RD; AC(ac) = XOR (mb); break;               /* XOR */
case 0431:  AC(ac) = XOR (IM); break;                   /* XORI */
case 0432:  RM; mb = XOR (mb); WR; break;               /* XORM */
case 0433:  RM; AC(ac) = XOR (mb); WRAC; break;         /* XORB */
case 0434:  RD; AC(ac) = IOR (mb); break;               /* IOR */
case 0435:  AC(ac) = IOR (IM); break;                   /* IORI */
case 0436:  RM; mb = IOR (mb); WR; break;               /* IORM */
case 0437:  RM; AC(ac) = IOR (mb); WRAC; break;         /* IORB */
case 0440:  RD; AC(ac) = ANDCB (mb); break;             /* ANDCB */
case 0441:  AC(ac) = ANDCB (IM); break;                 /* ANDCBI */
case 0442:  RM; mb = ANDCB (mb); WR; break;             /* ANDCBM */
case 0443:  RM; AC(ac) = ANDCB (mb); WRAC; break;       /* ANDCBB */
case 0444:  RD; AC(ac) = EQV (mb); break;               /* EQV */
case 0445:  AC(ac) = EQV (IM); break;                   /* EQVI */
case 0446:  RM; mb = EQV (mb); WR; break;               /* EQVM */
case 0447:  RM; AC(ac) = EQV (mb); WRAC; break;         /* EQVB */
case 0450:  RD; AC(ac) = SETCA (mb); break;             /* SETCA */
case 0451:  AC(ac) = SETCA (IM); break;                 /* SETCAI */
case 0452:  RM; mb = SETCA (mb); WR; break;             /* SETCAM */
case 0453:  RM; AC(ac) = SETCA (mb); WRAC; break;       /* SETCAB */
case 0454:  RD; AC(ac) = ORCA (mb); break;              /* ORCA */
case 0455:  AC(ac) = ORCA (IM); break;                  /* ORCAI */
case 0456:  RM; mb = ORCA (mb); WR; break;              /* ORCAM */
case 0457:  RM; AC(ac) = ORCA (mb); WRAC; break;        /* ORCAB */
case 0460:  RD; AC(ac) = SETCM (mb); break;             /* SETCM */
case 0461:  AC(ac) = SETCM (IM); break;                 /* SETCMI */
case 0462:  RM; mb = SETCM (mb); WR; break;             /* SETCMM */
case 0463:  RM; AC(ac) = SETCM (mb); WRAC; break;       /* SETCMB */
case 0464:  RD; AC(ac) = ORCM (mb); break;              /* ORCM */
case 0465:  AC(ac) = ORCM (IM); break;                  /* ORCMI */
case 0466:  RM; mb = ORCM (mb); WR; break;              /* ORCMM */
case 0467:  RM; AC(ac) = ORCM (mb); WRAC; break;        /* ORCMB */
case 0470:  RD; AC(ac) = ORCB (mb); break;              /* ORCB */
case 0471:  AC(ac) = ORCB (IM); break;                  /* ORCBI */
case 0472:  RM; mb = ORCB (mb); WR; break;              /* ORCBM */
case 0473:  RM; AC(ac) = ORCB (mb); WRAC; break;        /* ORCBB */
case 0474:  AC(ac) = ONES; break;                       /* SETO */
case 0475:  AC(ac) = ONES; break;                       /* SETOI */
case 0476:  mb = ONES; WR; break;                       /* SETOM */
case 0477:  mb = ONES; WR; AC(ac) = ONES; break;        /* SETOB */

/* Halfword instructions (0500 - 0577) - checked against KS10 ucode */

case 0500:  RD; AC(ac) = LL (mb, AC(ac)); break;        /* HLL */
case 0501:  AC(ac) = LL (IM, AC(ac)); break;            /* HLLI */
case 0502:  RM; mb = LL (AC(ac), mb); WR; break;        /* HLLM */
case 0503:  RM; mb = LL (mb, mb); WR; LAC; break;       /* HLLS */
case 0504:  RD; AC(ac) = RL (mb, AC(ac)); break;        /* HRL */
case 0505:  AC(ac) = RL (IM, AC(ac)); break;            /* HRLI */
case 0506:  RM; mb = RL (AC(ac), mb); WR; break;        /* HRLM */
case 0507:  RM; mb = RL (mb, mb); WR; LAC; break;       /* HRLS */
case 0510:  RD; AC(ac) = LLZ (mb); break;               /* HLLZ */
case 0511:  AC(ac) = LLZ (IM); break;                   /* HLLZI */
case 0512:  mb = LLZ (AC(ac)); WR; break;               /* HLLZM */
case 0513:  RM; mb = LLZ (mb); WR; LAC; break;          /* HLLZS */
case 0514:  RD; AC(ac) = RLZ (mb); break;               /* HRLZ */
case 0515:  AC(ac) = RLZ (IM); break;                   /* HRLZI */
case 0516:  mb = RLZ (AC(ac)); WR; break;               /* HRLZM */
case 0517:  RM; mb = RLZ (mb); WR; LAC; break;          /* HRLZS */
case 0520:  RD; AC(ac) = LLO (mb); break;               /* HLLO */
case 0521:  AC(ac) = LLO (IM); break;                   /* HLLOI */
case 0522:  mb = LLO (AC(ac)); WR; break;               /* HLLOM */
case 0523:  RM; mb = LLO (mb); WR; LAC; break;          /* HLLOS */
case 0524:  RD; AC(ac) = RLO (mb); break;               /* HRLO */
case 0525:  AC(ac) = RLO (IM); break;                   /* HRLOI */
case 0526:  mb = RLO (AC(ac)); WR; break;               /* HRLOM */
case 0527:  RM; mb = RLO (mb); WR; LAC; break;          /* HRLOS */
case 0530:  RD; AC(ac) = LLE (mb); break;               /* HLLE */
case 0531:  AC(ac) = LLE (IM); break;                   /* HLLEI */
case 0532:  mb = LLE (AC(ac)); WR; break;               /* HLLEM */
case 0533:  RM; mb = LLE (mb); WR; LAC; break;          /* HLLES */
case 0534:  RD; AC(ac) = RLE (mb); break;               /* HRLE */
case 0535:  AC(ac) = RLE (IM); break;                   /* HRLEI */
case 0536:  mb = RLE (AC(ac)); WR; break;               /* HRLEM */
case 0537:  RM; mb = RLE (mb); WR; LAC; break;          /* HRLES */
case 0540:  RD; AC(ac) = RR (mb, AC(ac)); break;        /* HRR */
case 0541:  AC(ac) = RR (IM, AC(ac)); break;            /* HRRI */
case 0542:  RM; mb = RR (AC(ac), mb); WR; break;        /* HRRM */
case 0543:  RM; mb = RR (mb, mb); WR; LAC; break;       /* HRRS */
case 0544:  RD; AC(ac) = LR (mb, AC(ac)); break;        /* HLR */
case 0545:  AC(ac) = LR (IM, AC(ac)); break;            /* HLRI */
case 0546:  RM; mb = LR (AC(ac), mb); WR; break;        /* HLRM */
case 0547:  RM; mb = LR (mb, mb); WR; LAC; break;       /* HLRS */
case 0550:  RD; AC(ac) = RRZ (mb); break;               /* HRRZ */
case 0551:  AC(ac) = RRZ (IM); break;                   /* HRRZI */
case 0552:  mb = RRZ (AC(ac)); WR; break;               /* HRRZM */
case 0553:  RM; mb = RRZ(mb); WR; LAC; break;           /* HRRZS */
case 0554:  RD; AC(ac) = LRZ (mb); break;               /* HLRZ */
case 0555:  AC(ac) = LRZ (IM); break;                   /* HLRZI */
case 0556:  mb = LRZ (AC(ac)); WR; break;               /* HLRZM */
case 0557:  RM; mb = LRZ (mb); WR; LAC; break;          /* HLRZS */
case 0560:  RD; AC(ac) = RRO (mb); break;               /* HRRO */
case 0561:  AC(ac) = RRO (IM); break;                   /* HRROI */
case 0562:  mb = RRO (AC(ac)); WR; break;               /* HRROM */
case 0563:  RM; mb = RRO (mb); WR; LAC; break;          /* HRROS */
case 0564:  RD; AC(ac) = LRO (mb); break;               /* HLRO */
case 0565:  AC(ac) = LRO (IM); break;                   /* HLROI */
case 0566:  mb = LRO (AC(ac)); WR; break;               /* HLROM */
case 0567:  RM; mb = LRO (mb); WR; LAC; break;          /* HLROS */
case 0570:  RD; AC(ac) = RRE (mb); break;               /* HRRE */
case 0571:  AC(ac) = RRE (IM); break;                   /* HRREI */
case 0572:  mb = RRE (AC(ac)); WR; break;               /* HRREM */
case 0573:  RM; mb = RRE (mb); WR; LAC; break;          /* HRRES */
case 0574:  RD; AC(ac) = LRE (mb); break;               /* HLRE */
case 0575:  AC(ac) = LRE (IM); break;                   /* HLREI */
case 0576:  mb = LRE (AC(ac)); WR; break;               /* HLREM */
case 0577:  RM; mb = LRE (mb); WR; LAC; break;          /* HLRES */

/* Test instructions (0600 - 0677) - checked against KS10 ucode
   In the KS10 ucode, TDN and TSN do not fetch an operand; the Processor
   Reference Manual describes them as NOPs that reference memory.
*/

case 0600:  break;                                      /* TRN */
case 0601:  break;                                      /* TLN */
case 0602:  TR_; T__E; break;                           /* TRNE */
case 0603:  TL_; T__E; break;                           /* TLNE */
case 0604:  T__A; break;                                /* TRNA */
case 0605:  T__A; break;                                /* TLNA */
case 0606:  TR_; T__N; break;                           /* TRNN */
case 0607:  TL_; T__N; break;                           /* TLNN */
case 0610:  TD_; break;                                 /* TDN */
case 0611:  TS_; break;                                 /* TSN */
case 0612:  TD_; T__E; break;                           /* TDNE */
case 0613:  TS_; T__E; break;                           /* TSNE */
case 0614:  TD_; T__A; break;                           /* TDNA */
case 0615:  TS_; T__A; break;                           /* TSNA */
case 0616:  TD_; T__N; break;                           /* TDNN */
case 0617:  TS_; T__N; break;                           /* TSNN */
case 0620:  TR_; T_Z; break;                            /* TRZ */
case 0621:  TL_; T_Z; break;                            /* TLZ */
case 0622:  TR_; T__E; T_Z; break;                      /* TRZE */
case 0623:  TL_; T__E; T_Z; break;                      /* TLZE */
case 0624:  TR_; T__A; T_Z; break;                      /* TRZA */
case 0625:  TL_; T__A; T_Z; break;                      /* TLZA */
case 0626:  TR_; T__N; T_Z; break;                      /* TRZN */
case 0627:  TL_; T__N; T_Z; break;                      /* TLZN */
case 0630:  TD_; T_Z; break;                            /* TDZ */
case 0631:  TS_; T_Z; break;                            /* TSZ */
case 0632:  TD_; T__E; T_Z; break;                      /* TDZE */
case 0633:  TS_; T__E; T_Z; break;                      /* TSZE */
case 0634:  TD_; T__A; T_Z; break;                      /* TDZA */
case 0635:  TS_; T__A; T_Z; break;                      /* TSZA */
case 0636:  TD_; T__N; T_Z; break;                      /* TDZN */
case 0637:  TS_; T__N; T_Z; break;                      /* TSZN */
case 0640:  TR_; T_C; break;                            /* TRC */
case 0641:  TL_; T_C; break;                            /* TLC */
case 0642:  TR_; T__E; T_C; break;                      /* TRCE */
case 0643:  TL_; T__E; T_C; break;                      /* TLCE */
case 0644:  TR_; T__A; T_C; break;                      /* TRCA */
case 0645:  TL_; T__A; T_C; break;                      /* TLCA */
case 0646:  TR_; T__N; T_C; break;                      /* TRCN */
case 0647:  TL_; T__N; T_C; break;                      /* TLCN */
case 0650:  TD_; T_C; break;                            /* TDC */
case 0651:  TS_; T_C; break;                            /* TSC */
case 0652:  TD_; T__E; T_C; break;                      /* TDCE */
case 0653:  TS_; T__E; T_C; break;                      /* TSCE */
case 0654:  TD_; T__A; T_C; break;                      /* TDCA */
case 0655:  TS_; T__A; T_C; break;                      /* TSCA */
case 0656:  TD_; T__N; T_C; break;                      /* TDCN */
case 0657:  TS_; T__N; T_C; break;                      /* TSCN */
case 0660:  TR_; T_O; break;                            /* TRO */
case 0661:  TL_; T_O; break;                            /* TLO */
case 0662:  TR_; T__E; T_O; break;                      /* TROE */
case 0663:  TL_; T__E; T_O; break;                      /* TLOE */
case 0664:  TR_; T__A; T_O; break;                      /* TROA */
case 0665:  TL_; T__A; T_O; break;                      /* TLOA */
case 0666:  TR_; T__N; T_O; break;                      /* TRON */
case 0667:  TL_; T__N; T_O; break;                      /* TLON */
case 0670:  TD_; T_O; break;                            /* TDO */
case 0671:  TS_; T_O; break;                            /* TSO */
case 0672:  TD_; T__E; T_O; break;                      /* TDOE */
case 0673:  TS_; T__E; T_O; break;                      /* TSOE */
case 0674:  TD_; T__A; T_O; break;                      /* TDOA */
case 0675:  TS_; T__A; T_O; break;                      /* TSOA */
case 0676:  TD_; T__N; T_O; break;                      /* TDON */
case 0677:  TS_; T__N; T_O; break;                      /* TSON */

/* I/O instructions (0700 - 0777)

   Only the defined I/O instructions have explicit case labels;
   the rest default to unimplemented (monitor UUO).  Note that   
   710-715 and 720-725 have different definitions under ITS and
   use normal effective addresses instead of the special address
   calculation required by TOPS-10 and TOPS-20.
*/

case 0700:  IO7 (io700i, io700d); break;                /* I/O 0 */
case 0701:  IO7 (io701i, io701d); break;                /* I/O 1 */
case 0702:  IO7 (io702i, io702d); break;                /* I/O 2 */
case 0704:  IOC; AC(ac) = Read (ea, OPND_PXCT); break;  /* UMOVE */
case 0705:  IOC; Write (ea, AC(ac), OPND_PXCT); break;  /* UMOVEM */
case 0710:  IOA; if (io710 (ac, ea)) INCPC; break;      /* TIOE, IORDI */
case 0711:  IOA; if (io711 (ac, ea)) INCPC; break;      /* TION, IORDQ */
case 0712:  IOAM; AC(ac) = io712 (ea); break;           /* RDIO, IORD */
case 0713:  IOAM; io713 (AC(ac), ea); break;            /* WRIO, IOWR */
case 0714:  IOA; io714 (AC(ac), ea); break;             /* BSIO, IOWRI */
case 0715:  IOA; io715 (AC(ac), ea); break;             /* BCIO, IOWRQ */
case 0716:  IOC; bltu (ac, ea, pflgs, 0); break;        /* BLTBU */
case 0717:  IOC; bltu (ac, ea, pflgs, 1); break;        /* BLTUB */
case 0720:  IOA; if (io720 (ac, ea)) INCPC; break;      /* TIOEB, IORDBI */
case 0721:  IOA; if (io721 (ac, ea)) INCPC; break;      /* TIONB, IORDBQ */
case 0722:  IOAM; AC(ac) = io722 (ea); break;           /* RDIOB, IORDB */
case 0723:  IOAM; io723 (AC(ac), ea); break;            /* WRIOB, IOWRB */
case 0724:  IOA; io724 (AC(ac), ea); break;             /* BSIOB, IOWRBI */
case 0725:  IOA; io725 (AC(ac), ea); break;             /* BCIOB, IOWRBQ */

/* If undefined, monitor UUO - checked against KS10 ucode
   The KS10 implements a much more limited version of MUUO flag handling.
   In the KS10, the trap ucode checks for opcodes 000-077.  If the opcode
   is in that range, the trap flags are not cleared.  Instead, the MUUO
   microcode stores the flags with traps cleared, and uses the trap flags
   to determine how to vector.  Thus, MUUO's >= 100 will vector incorrectly.
*/

default:
MUUO:
    if (T20PAG) {                                       /* TOPS20 paging? */
        int32 tf = (op << (INST_V_OP - 18)) | (ac << (INST_V_AC - 18));
        WriteP (upta + UPT_MUUO, XWD (                  /* store flags,,op+ac */
            flags & ~(F_T2 | F_T1), tf));               /* traps clear */
        WriteP (upta + UPT_MUPC, PC);                   /* store PC */
        WriteP (upta + UPT_T20_UEA, ea);                /* store eff addr */
        WriteP (upta + UPT_T20_CTX, UBRWORD);           /* store context */
        }
    else {                                              /* TOPS10/ITS */
        WriteP (upta + UPT_MUUO, UUOWORD);              /* store instr word */
        WriteP (upta + UPT_MUPC, XWD (                  /* store flags,,PC */
            flags & ~(F_T2 | F_T1), PC));               /* traps clear */
        WriteP (upta + UPT_T10_CTX, UBRWORD);           /* store context */
        }
    ea = upta + (TSTF (F_USR)? UPT_UNPC: UPT_ENPC) +
        (pager_tc? UPT_NPCT: 0);                        /* calculate vector */
    mb = ReadP (ea);                                    /* new flags, PC */
    JUMP (mb);                                          /* set new PC */
    if (TSTF (F_USR))                                   /* set PCU */
        mb = mb | XWD (F_UIO, 0);
    set_newflags (mb, FALSE);                           /* set new flags */
    break;

/* JRST - checked against KS10 ucode
   Differences from the KS10: the KS10
        - (JRSTF, JEN) refetches the base instruction from PC - 1
        - (XJEN) dismisses interrupt before reading the new flags and PC
        - (XPCW) writes the old flags and PC before reading the new
   ITS microcode includes extended JRST's, although they are not used
*/

case 0254:                                              /* JRST */
    i = jrst_tab[ac];                                   /* get subop flags */
    if ((i == 0) || ((i == JRST_E) && TSTF (F_USR)) ||
        ((i == JRST_UIO) && TSTF (F_USR) && !TSTF (F_UIO)))
        goto MUUO;                                      /* not legal */
    switch (ac) {                                       /* case on subopcode */

    case 000:                                           /* JRST 0 = jump */
    case 001:                                           /* JRST 1 = portal */
        JUMP (ea);
        break;

    case 002:                                           /* JRST 2 = JRSTF */
        mb = calc_jrstfea (inst, pflgs);                /* recalc addr w flgs */
        JUMP (ea);                                      /* set new PC */
        set_newflags (mb, TRUE);                        /* set new flags */
        break;

    case 004:                                           /* JRST 4 = halt */
        JUMP (ea);                                      /* old_PC = halt + 1 */
        pager_PC = PC;                                  /* force right PC */
        ABORT (STOP_HALT);                              /* known to be exec */
        break;

    case 005:                                           /* JRST 5 = XJRSTF */
        RD2;                                            /* read doubleword */
        JUMP (rs[1]);                                   /* set new PC */
        set_newflags (rs[0], TRUE);                     /* set new flags */
        break;

    case 006:                                           /* JRST 6 = XJEN */
        RD2;                                            /* read doubleword */
        pi_dismiss ();                                  /* page ok, dismiss */
        JUMP (rs[1]);                                   /* set new PC */
        set_newflags (rs[0], FALSE);                    /* known to be exec */
        break;

    case 007:                                           /* JRST 7 = XPCW */
        ea = ADDA (i = ea, 2);                          /* new flags, PC */
        RD2;                                            /* read, test page fail */
        ReadM (INCA (i), MM_OPND);                      /* test PC write */
        Write (i, XWD (flags, 0), MM_OPND);             /* write flags */
        Write (INCA (i), PC, MM_OPND);                  /* write PC */
        JUMP (rs[1]);                                   /* set new PC */
        set_newflags (rs[0], FALSE);                    /* known to be exec */
        break;

    case 010:                                           /* JRST 10 = dismiss */
        pi_dismiss ();                                  /* dismiss int */
        JUMP (ea);                                      /* set new PC */
        break;

    case 012:                                           /* JRST 12 = JEN */
        mb = calc_jrstfea (inst, pflgs);                /* recalc addr w flgs */
        JUMP (ea);                                      /* set new PC */
        set_newflags (mb, TRUE);                        /* set new flags */
        pi_dismiss ();                                  /* dismiss int */
        break;

    case 014:                                           /* JRST 14 = SFM */
        Write (ea, XWD (flags, 0), MM_OPND);
        break;  

    case 015:                                           /* JRST 15 = XJRST */
        if (!T20PAG)                                    /* only in TOPS20 paging */
            goto MUUO;
        JUMP (Read (ea, MM_OPND));                      /* jump to M[ea] */
        break;
        }                                               /* end case subop */
    break;
    }                                                   /* end case op */

if (its_2pr) {                                          /* 1-proc trap? */
    its_1pr = its_2pr = 0;                              /* clear trap */
    if (Q_ITS) {                                        /* better be ITS */
        WriteP (upta + UPT_1PO, FLPC);                  /* wr old flgs, PC */
        mb = ReadP (upta + UPT_1PN);                    /* rd new flgs, PC */
        JUMP (mb);                                      /* set PC */
        set_newflags (mb, FALSE);                       /* set new flags */
        }
    }                                                   /* end if 2-proc */
}                                                       /* end for */

/* Should never get here */

ABORT (STOP_UNKNOWN);
}

/* Single word integer routines */

/* Integer add

   Truth table for integer add

        case    a       b       r       flags
        1       +       +       +       none
        2       +       +       -       AOV + C1
        3       +       -       +       C0 + C1
        4       +       -       -       -
        5       -       +       +       C0 + C1
        6       -       +       -       -
        7       -       -       +       AOV + C0
        8       -       -       -       C0 + C1
*/

d10 add (d10 a, d10 b)
{
d10 r;

r = (a + b) & DMASK;
if (TSTS (a & b)) {                                     /* cases 7,8 */
    if (TSTS (r))                                       /* case 8 */
        SETF (F_C0 | F_C1);
    else SETF (F_C0 | F_AOV | F_T1);                    /* case 7 */
    return r;
    }
if (!TSTS (a | b)) {                                    /* cases 1,2 */
    if (TSTS (r))                                       /* case 2 */
        SETF (F_C1 | F_AOV | F_T1);
    return r;                                           /* case 1 */
    }
if (!TSTS (r))                                          /* cases 3,5 */
    SETF (F_C0 | F_C1); 
return r;
}

/* Integer subtract - actually ac + ~op + 1 */

d10 sub (d10 a, d10 b)
{
d10 r;

r = (a - b) & DMASK;
if (TSTS (a & ~b)) {                                    /* cases 7,8 */
    if (TSTS (r))                                       /* case 8 */
        SETF (F_C0 | F_C1);
    else SETF (F_C0 | F_AOV | F_T1);                    /* case 7 */
    return r;
    }
if (!TSTS (a | ~b)) {                                   /* cases 1,2 */
    if (TSTS (r))                                       /* case 2 */
        SETF (F_C1 | F_AOV | F_T1);
    return r;                                           /* case 1 */
    }
if (!TSTS (r))                                          /* cases 3,5 */
    SETF (F_C0 | F_C1);
return r;
}


/* Logical shift */

d10 lsh (d10 val, a10 ea)
{
int32 sc = LIT8 (ea);

if (sc > 35)
    return 0;
if (ea & RSIGN)
    return (val >> sc);
return ((val << sc) & DMASK);
}

/* Rotate */

d10 rot (d10 val, a10 ea)
{
int32 sc = LIT8 (ea) % 36;

if (sc == 0)
    return val;
if (ea & RSIGN)
    sc = 36 - sc;
return (((val << sc) | (val >> (36 - sc))) & DMASK);
}

/* Double word integer instructions */

/* Double add - see case table for single add */

void dadd (int32 ac, d10 *rs)
{
d10 r;
int32 p1 = ADDAC (ac, 1);

AC(p1) = CLRS (AC(p1)) + CLRS (rs[1]);                  /* add lo */
r = (AC(ac) + rs[0] + (TSTS (AC(p1))? 1: 0)) & DMASK;   /* add hi+cry */
if (TSTS (AC(ac) & rs[0])) {                            /* cases 7,8 */
    if (TSTS (r))                                       /* case 8 */
        SETF (F_C0 | F_C1);
    else SETF (F_C0 | F_AOV | F_T1);                    /* case 7 */
    }
else if (!TSTS (AC(ac) | rs[0])) {                      /* cases 1,2 */
    if (TSTS (r))                                       /* case 2 */
        SETF (F_C1 | F_AOV | F_T1);
    }
else if (!TSTS (r))                                     /* cases 3,5 */
    SETF (F_C0 | F_C1);
AC(ac) = r;
AC(p1) = TSTS (r)? SETS (AC(p1)): CLRS (AC(p1));
return;
} 

/* Double subtract - see comments for single subtract */

void dsub (int32 ac, d10 *rs)
{
d10 r;
int32 p1 = ADDAC (ac, 1);

AC(p1) = CLRS (AC(p1)) - CLRS (rs[1]);                  /* sub lo */
r = (AC(ac) - rs[0] - (TSTS (AC(p1))? 1: 0)) & DMASK;   /* sub hi,borrow */
if (TSTS (AC(ac) & ~rs[0])) {                           /* cases 7,8 */
    if (TSTS (r))                                       /* case 8 */
        SETF (F_C0 | F_C1);
    else SETF (F_C0 | F_AOV | F_T1);                    /* case 7 */
    }
else if (!TSTS (AC(ac) | ~rs[0])) {                     /* cases 1,2 */
    if (TSTS (r))                                       /* case 2 */
        SETF (F_C1 | F_AOV | F_T1);
    }
else if (!TSTS (r))                                     /* cases 3,5 */
    SETF (F_C0 | F_C1);
AC(ac) = r;
AC(p1) = (TSTS (r)? SETS (AC(p1)): CLRS (AC(p1))) & DMASK;
return;
} 


/* Logical shift combined */

void lshc (int32 ac, a10 ea)
{
int32 p1 = ADDAC (ac, 1);
int32 sc = LIT8 (ea);

if (sc > 71)
    AC(ac) = AC(p1) = 0;
else if (ea & RSIGN) {
    if (sc >= 36) {
        AC(p1) = AC(ac) >> (sc - 36);
        AC(ac) = 0;
        }
    else {
        AC(p1) = ((AC(p1) >> sc) | (AC(ac) << (36 - sc))) & DMASK;
        AC(ac) = AC(ac) >> sc;
        }
    }
else {
    if (sc >= 36) {
        AC(ac) = (AC(p1) << (sc - 36)) & DMASK;
        AC(p1) = 0;
        }
    else {
        AC(ac) = ((AC(ac) << sc) | (AC(p1) >> (36 - sc))) & DMASK;
        AC(p1) = (AC(p1) << sc) & DMASK;
        }
    }
return;
}

/* Rotate combined */

void rotc (int32 ac, a10 ea)
{
int32 p1 = ADDAC (ac, 1);
int32 sc = LIT8 (ea) % 72;
d10 t = AC(ac);

if (sc == 0)
    return;
if (ea & RSIGN)
    sc = 72 - sc;
if (sc >= 36) {
    AC(ac) = ((AC(p1) << (sc - 36)) | (t >> (72 - sc))) & DMASK;
    AC(p1) = ((t << (sc - 36)) | (AC(p1) >> (72 - sc))) & DMASK;
    }
else {
    AC(ac) = ((t << sc) | (AC(p1) >> (36 - sc))) & DMASK;
    AC(p1) = ((AC(p1) << sc) | (t >> (36 - sc))) & DMASK;
    }
return;
}

/* Arithmetic shifts */

d10 ash (d10 val, a10 ea)
{
int32 sc = LIT8 (ea);
d10 sign = TSTS (val);
d10 fill = sign? ONES: 0;
d10 so;

if (sc == 0)
    return val;
if (sc > 35)                                            /* cap sc at 35 */
    sc = 35;
if (ea & RSIGN)
    return (((val >> sc) | (fill << (36 - sc))) & DMASK);
so = val >> (35 - sc);                                  /* bits lost left + sign */
if (so != (sign? bytemask[sc + 1]: 0))
    SETF (F_AOV | F_T1);
return (sign | ((val << sc) & MMASK));
}

void ashc (int32 ac, a10 ea)
{
int32 sc = LIT8 (ea);
int32 p1 = ADDAC (ac, 1);
d10 sign = TSTS (AC(ac));
d10 fill = sign? ONES: 0;
d10 so;

if (sc == 0)
    return;
if (sc > 70)                                            /* cap sc at 70 */
    sc = 70;
AC(ac) = CLRS (AC(ac));                                 /* clear signs */
AC(p1) = CLRS (AC(p1));
if (ea & RSIGN) {
    if (sc >= 35) {                                     /* right 36..70 */
        AC(p1) = ((AC(ac) >> (sc - 35)) | (fill << (70 - sc))) & DMASK;
        AC(ac) = fill;
        }
    else {
        AC(p1) = sign |                                 /* right 1..35 */
            (((AC(p1) >> sc) | (AC(ac) << (35 - sc))) & MMASK);
        AC(ac) = ((AC(ac) >> sc) | (fill << (35 - sc))) & DMASK;
        }
    }
else {
    if (sc >= 35) {                                     /* left 36..70 */
        so = AC(p1) >> (70 - sc);                       /* bits lost left */
        if ((AC(ac) != (sign? MMASK: 0)) ||
            (so != (sign? bytemask[sc - 35]: 0)))
            SETF (F_AOV | F_T1);
        AC(ac) = sign | ((AC(p1) << (sc - 35)) & MMASK);
        AC(p1) = sign;
        }
    else {
        so = AC(ac) >> (35 - sc);                       /* bits lost left */
        if (so != (sign? bytemask[sc]: 0))
            SETF (F_AOV | F_T1);
        AC(ac) = sign |
            (((AC(ac) << sc) | (AC(p1) >> (35 - sc))) & MMASK);
        AC(p1) = sign | ((AC(p1) << sc) & MMASK);
        }
    }
return;
}

/* Effective address routines */

/* Calculate effective address - used by byte instructions, extended
   instructions, and interrupts to get a different mapping context from
   the main loop.  prv is either EABP_PXCT or MM_CUR.
*/

a10 calc_ea (d10 inst, int32 prv)
{
int32 i, ea, xr;
d10 indrct;

for (indrct = inst, i = 0; ; i++) {
    ea = GET_ADDR (indrct);
    xr = GET_XR (indrct);
    if (xr)
        ea = (ea + ((a10) XR (xr, prv))) & AMASK;
    if (TST_IND (indrct)) {                             /* indirect? */
        if (i != 0) {                                   /* not first cycle? */
            int32 t = test_int ();                      /* test for intr */
            if (t != 0)                                 /* intr or error? */
                ABORT (t);
            if ((ind_max != 0) && (i >= ind_max))       /* limit exceeded? */
                ABORT (STOP_IND);
            }
        indrct = Read (ea, prv);
        }
    else break;
    }
return ea;
}

/* Calculate I/O effective address.  Cases:
   - No index or indirect, return addr from instruction
   - Index only, index >= 0, return 36b sum of addr + index
   - Index only, index <= 0, return 18b sum of addr + index
   - Indirect, calculate 18b sum of addr + index, return
                entire word fetch (single level)
*/

a10 calc_ioea (d10 inst, int32 pflgs)
{
int32 xr;
a10 ea;

xr = GET_XR (inst);
ea = GET_ADDR (inst);
if (TST_IND (inst)) {                                   /* indirect? */
    if (xr)
        ea = (ea + ((a10) XR (xr, MM_EA))) & AMASK;
    ea = (a10) Read (ea, MM_EA);
    }
else if (xr) {                                          /* direct + idx? */
    ea = ea + ((a10) XR (xr, MM_EA));
    if (TSTS (XR (xr, MM_EA)))
        ea = ea & AMASK;
    }
return ea;
}

/* Calculate JRSTF effective address.  This routine preserves
   the left half of the effective address, to be the new flags.
*/

d10 calc_jrstfea (d10 inst, int32 pflgs)
{
int32 i, xr;
d10 mb;

for (i = 0; ; i++) {
    mb = inst;
    xr = GET_XR (inst);
    if (xr)
        mb = (mb & AMASK) + XR (xr, MM_EA);
    if (TST_IND (inst)) {                               /* indirect? */
        if (i != 0) {                                   /* not first cycle? */
            int32 t = test_int ();                      /* test for intr */
            if (t != 0)                                 /* intr or error? */
                ABORT (t);
            if ((ind_max != 0) && (i >= ind_max))       /* limit exceeded? */
                ABORT (STOP_IND);
            }
        inst = Read (((a10) mb) & AMASK, MM_EA);
        }
    else break;
    }
return (mb & DMASK);
}

/* Byte pointer routines */

/* Increment byte pointer - checked against KS10 ucode */

void ibp (a10 ea, int32 pflgs)
{
int32 p, s;
d10 bp;

bp = ReadM (ea, MM_OPND);                               /* get byte ptr */
p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
p = p - s;                                              /* adv P */
if (p < 0) {                                            /* end of word? */
    bp = (bp & LMASK) | (INCR (bp));                    /* incr addr */
    p = (36 - s) & 077;                                 /* reset P */
    }
bp = PUT_P (bp, p);                                     /* store new P */
Write (ea, bp, MM_OPND);                                /* store byte ptr */
return;
}

/* Load byte */

d10 ldb (a10 ea, int32 pflgs)
{
a10 ba;
int32 p, s;
d10 bp, wd;

bp = Read (ea, MM_OPND);                                /* get byte ptr */
p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
ba = calc_ea (bp, MM_EABP);                             /* get addr of byte */
wd = Read (ba, MM_BSTK);                                /* read word */
wd = (wd >> p);                                         /* align byte */
wd = wd & bytemask[s];                                  /* mask to size */
return wd;
}

/* Deposit byte - must use read and write to get page fail correct */

void dpb (d10 val, a10 ea, int32 pflgs)
{
a10 ba;
int32 p, s;
d10 bp, wd, mask;

bp = Read (ea, MM_OPND);                                /* get byte ptr */
p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
ba = calc_ea (bp, MM_EABP);                             /* get addr of byte */
wd = Read (ba, MM_BSTK);                                /* read word */
mask = bytemask[s] << p;                                /* shift mask, val */
val = val << p;
wd = (wd & ~mask) | (val & mask);                       /* insert byte */
Write (ba, wd & DMASK, MM_BSTK);
return;
}

/* Adjust byte pointer - checked against KS10 ucode 
   The KS10 divide checks if the bytes per word = 0, which is a simpler
   formulation of the processor reference manual check.
*/

void adjbp (int32 ac, a10 ea, int32 pflgs)
{
int32 p, s;
d10 bp, newby, left, byadj, bywrd, val, wdadj;

val = AC(ac);                                           /* get adjustment */
bp = Read (ea, MM_OPND);                                /* get byte pointer */
p = GET_P (bp);                                         /* get p */
s = GET_S (bp);                                         /* get s */
if (s) {
    left = (36 - p) / s;                                /* bytes to left of p */
    bywrd = left + (p / s);                             /* bytes per word */
    if (bywrd == 0) {                                   /* zero bytes? */
        SETF (F_AOV | F_T1 | F_DCK);                    /* set flags */
        return;                                         /* abort operation */
        }
    newby = left + SXT (val);                           /* adjusted byte # */
    wdadj = newby / bywrd;                              /* word adjustment */
    byadj = (newby >= 0)? newby % bywrd: -((-newby) % bywrd);
    if (byadj <= 0) {
        byadj = byadj + bywrd;                          /* make adj positive */
        wdadj = wdadj - 1;
        }
    p = (36 - ((int32) byadj) * s) - ((36 - p) % s);    /* new p */
    bp = (PUT_P (bp, p) & LMASK) | ((bp + wdadj) & RMASK);
    }
AC(ac) = bp;            
return;
}

/* Block transfer - checked against KS10 ucode
   The KS10 uses instruction specific recovery code in page fail
   to set the AC properly for restart.  Lacking this mechanism,
   the simulator must test references in advance.
   The clocking test guarantees forward progress under single step.
*/

void blt (int32 ac, a10 ea, int32 pflgs)
{
a10 srca = (a10) LRZ (AC(ac));
a10 dsta = (a10) RRZ (AC(ac));
a10 lnt = ea - dsta + 1;
d10 srcv;
int32 flg, t;

AC(ac) = XWD (srca + lnt, dsta + lnt);
for (flg = 0; dsta <= ea; flg++) {                      /* loop */
    if (flg && (t = test_int ())) {                     /* timer event? */
        AC(ac) = XWD (srca, dsta);                      /* AC for intr */
        ABORT (t);
        }
    if (AccViol (srca & AMASK, MM_BSTK, PTF_RD)) {      /* src access viol? */
        AC(ac) = XWD (srca, dsta);                      /* AC for page fail */
        Read (srca & AMASK, MM_BSTK);                   /* force trap */
        }
    if (AccViol (dsta & AMASK, MM_OPND, PTF_WR)) {      /* dst access viol? */
        AC(ac) = XWD (srca, dsta);                      /* AC for page fail */
        ReadM (dsta & AMASK, MM_OPND);                  /* force trap */
        }
    srcv = Read (srca & AMASK, MM_BSTK);                /* read */
    Write (dsta & AMASK, srcv, MM_OPND);                /* write */
    srca = srca + 1;                                    /* incr addr */
    dsta = dsta + 1;
    }
return;
}

/* I/O block transfers - byte to Unibus (0) and Unibus to byte (1) */

#define BYTE1           INT64_C(0776000000000)
#define BYTE2           INT64_C(0001774000000)
#define BYTE3           INT64_C(0000003770000)
#define BYTE4           INT64_C(0000000007760)
/* unused               0000000000017 */

void bltu (int32 ac, a10 ea, int32 pflgs, int dir)
{
a10 srca = (a10) LRZ (AC(ac));
a10 dsta = (a10) RRZ (AC(ac));
a10 lnt = ea - dsta + 1;
d10 srcv, dstv;
int32 flg, t;

AC(ac) = XWD (srca + lnt, dsta + lnt);
for (flg = 0; dsta <= ea; flg++) {                      /* loop */
    if (flg && (t = test_int ())) {                     /* timer event? */
        AC(ac) = XWD (srca, dsta);                      /* AC for intr */
        ABORT (t);
        }
    if (AccViol (srca & AMASK, MM_BSTK, PTF_RD)) {      /* src access viol? */
        AC(ac) = XWD (srca, dsta);                      /* AC for page fail */
        Read (srca & AMASK, MM_BSTK);                   /* force trap */
        }
    if (AccViol (dsta & AMASK, MM_OPND, PTF_WR)) {      /* dst access viol? */
        AC(ac) = XWD (srca, dsta);                      /* AC for page fail */
        ReadM (dsta & AMASK, MM_OPND);                  /* force trap */
        }
    srcv = Read (srca & AMASK, MM_BSTK);                /* read */
    if (dir) dstv = ((srcv << 10) & BYTE1) | ((srcv >> 6) & BYTE2) |
        ((srcv << 12) & BYTE3) | ((srcv >> 4) & BYTE4);
    else dstv = ((srcv & BYTE1) >> 10) | ((srcv & BYTE2) << 6) |
        ((srcv & BYTE3) >> 12) | ((srcv & BYTE4) << 4);
    Write (dsta & AMASK, dstv, MM_OPND);                /* write */
    srca = srca + 1;                                    /* incr addr */
    dsta = dsta + 1;
    }
return;
}

/* Utility routine to test for I/O event and interrupt */

int32 test_int (void)
{
int32 t;

if (sim_interval <= 0) {                                /* check queue */
    if ((t = sim_process_event ()))                     /* IO event? */
        return t;
    if (pi_eval ())                                     /* interrupt? */
        return (INTERRUPT);
    }
else sim_interval--;                                    /* count clock */
return 0;
}

/* Adjust stack pointer

   The reference manual says to trap on:
   1) E < 0, left changes from + to -
   2) E >= 0, left changes from - to +
   This is the same as trap on:
   1) E and left result have same signs
   2) initial value and left result have different signs
 */

d10 adjsp (d10 val, a10 ea)
{
d10 imm = ea;
d10 left, right;

left = ADDL (val, imm);
right = ADDR (val, imm);
if (TSTS ((val ^ left) & (~left ^ RLZ (imm))))
    SETF (F_T2);
return (left | right);
}

/* Jump if find first ones
   Takes advantage of 7 bit find first table for priority interrupts.
*/

int32 jffo (d10 val)
{
int32 i, by;

if ((val & DMASK) == 0)
    return 0;
for (i = 0; i <= 28; i = i + 7) {                       /* scan five bytes */
    by = (int32) ((val >> (29 - i)) & 0177);
    if (by)
        return (pi_m2lvl[by] + i - 1);
    }
return 35;                                              /* must be bit 35 */
}

/* Circulate - ITS only instruction

   Bits rotated out of AC are rotated into the opposite end of AC+1 - why?
   No attempt is made to optimize this instruction.
*/

void circ (int32 ac, int32 ea)
{
int32 sc = LIT8 (ea) % 72;
int32 p1 = ADDAC (ac,1);
int32 i;
d10 val;

if (sc == 0)                                            /* any shift? */
    return;
if (ea & RSIGN)                                         /* if right, make left */
    sc = 72 - sc;
for (i = 0; i < sc; i++) {                              /* one bit at a time */
    val = TSTS (AC(ac));                                /* shift out */
    AC(ac) = ((AC(ac) << 1) | (AC(p1) & 1)) & DMASK;
    AC(p1) = (AC(p1) >> 1) | val;                       /* shift in */
    }
return;
}

/* Arithmetic processor (APR)

   The APR subsystem includes miscellaneous interrupts that are individually
   maskable but which interrupt on a single, selectable level

   Instructions for the arithmetic processor:
        APRID                   read system id  
        WRAPR (CONO APR)        write system flags
        RDAPR (CONI APR)        read system flags
        (CONSO APR)             test system flags
        (CONSZ APR)             test system flags
*/

t_bool aprid (a10 ea, int32 prv)
{
d10 value = (Q_ITS)? UC_AIDITS: UC_AIDDEC;

if( (apr_serial == -1) || (!Q_ITS && apr_serial < 4096) )
    value |= (Q_ITS)? UC_SERITS: UC_SERDEC;
else
    value |= apr_serial;

Write (ea, value, prv);
return FALSE;
}

/* Checked against KS10 ucode */

t_bool wrapr (a10 ea, int32 prv)
{
int32 bits = APR_GETF (ea);

apr_lvl = ea & APR_M_LVL;
if (ea & APR_SENB)                                      /* set enables? */
    apr_enb = apr_enb | bits;
if (ea & APR_CENB)                                      /* clear enables? */
    apr_enb = apr_enb & ~bits;
if (ea & APR_CFLG) {                                    /* clear flags? */
    if ((bits & APRF_TIM) && (apr_flg & APRF_TIM))
        sim_rtcn_tick_ack (30, 0);
    apr_flg = apr_flg & ~bits;
    }
if (ea & APR_SFLG)                                      /* set flags? */
    apr_flg = apr_flg | bits;
if (apr_flg & APRF_ITC) {                               /* interrupt console? */
    fe_intr ();                                         /* explicit callout */
    apr_flg = apr_flg & ~APRF_ITC;                      /* interrupt clears */
    }
pi_eval ();                                             /* eval pi system */
return FALSE;
}

t_bool rdapr (a10 ea, int32 prv)
{
Write (ea, (d10) APRWORD, prv);
return FALSE;
}

t_bool czapr (a10 ea, int32 prv)
{
return ((APRHWORD & ea)? FALSE: TRUE);
}

t_bool coapr (a10 ea, int32 prv)
{
return ((APRHWORD & ea)? TRUE: FALSE);
}

/* Routine to change the processor flags, called from JRST, MUUO, interrupt.
   If jrst is TRUE, must munge flags for executive security.
   Because the KS10 lacks the public flag, these checks are simplified.
*/

void set_newflags (d10 newf, t_bool jrst)
{
int32 fl = (int32) LRZ (newf);

if (jrst && TSTF (F_USR)) {                             /* if in user now */
    fl = fl | F_USR;                                    /* can't clear user */
    if (!TSTF (F_UIO))                                  /* if !UIO, can't set */
        fl = fl & ~F_UIO;
    }
if (Q_ITS && (fl & F_1PR)) {                            /* ITS 1-proceed? */
    its_1pr = 1;                                        /* set flag */
    fl = fl & ~F_1PR;                                   /* vanish bit */
    }
flags = fl & F_MASK;                                    /* set new flags */
set_dyn_ptrs ();                                        /* set new ptrs */
return;
}

/* Priority interrupt system (PI)

   The priority interrupt system has three sources of requests
        (pi_apr)                system flags - synthesized on the fly
        (pi_ioq)                I/O interrupts - synthesized on the fly
        pi_prq                  program requests
   APR and I/O requests are masked with the PI enable mask; the program
   requests are not.  If priority interrupts are enabled, and there is
   a request at a level exceeding the currently active level, then an
   interrupt occurs.

   Instructions for the priority interrupt system:
        WRPI (CONO PI)          write pi system
        RDPI (CONI PI)          read pi system
        (CONSO PI)              test pi system
        (CONSZ PI)              test pi system

   Routines for the priority interrupt system:
        pi_eval         return level number of highest interrupt
        pi_dismiss              dismiss highest outstanding interrupt

   Checked against KS10 ucode - KS10 UUO's if <18:21> are non-zero
*/

t_bool wrpi (a10 ea, int32 prv)
{
int32 lvl = ea & PI_M_LVL;

if (ea & PI_INIT)
    pi_on = pi_enb = pi_act = pi_prq = 0;
if (ea & PI_CPRQ)                                       /* clear prog reqs? */
    pi_prq = pi_prq & ~lvl;
if (ea & PI_SPRQ)                                       /* set prog reqs? */
    pi_prq = pi_prq | lvl;
if (ea & PI_SENB)                                       /* enable levels? */
    pi_enb = pi_enb | lvl;
if (ea & PI_CENB)                                       /* disable levels? */
    pi_enb = pi_enb & ~lvl;
if (ea & PI_SON)                                        /* enable pi? */
    pi_on = 1;
if (ea & PI_CON)                                        /* disable pi? */
    pi_on = 0;
pi_eval ();                                             /* eval pi system */
return FALSE;
}

t_bool rdpi (a10 ea, int32 prv)
{
Write (ea, (d10) PIWORD, prv);
return FALSE;
}

t_bool czpi (a10 ea, int32 prv)
{
return ((PIHWORD & ea)? FALSE: TRUE);
}

t_bool copi (a10 ea, int32 prv)
{
return ((PIHWORD & ea)? TRUE: FALSE);
}

/* Priority interrupt evaluation

   The Processor Reference Manuals says that program interrupt
   requests occur whether the corresponding level is enabled or
   not.  However, the KS10, starting with microcode edit 47,
   masked program requests under the enable mask, just like APR
   and I/O requests.  This is not formally documented but appears
   to be necessary for the TOPS20 console port to run correclty.
*/

int32 pi_eval (void)
{
int32 reqlvl, actlvl;
extern int32 pi_ub_eval ();

qintr = 0;
if (pi_on) {
    pi_apr = (apr_flg & apr_enb)? pi_l2bit[apr_lvl]: 0;
    pi_ioq = pi_ub_eval ();
    reqlvl = pi_m2lvl[((pi_apr | pi_ioq | pi_prq) & pi_enb)];
    actlvl = pi_m2lvl[pi_act];
    if ((actlvl == 0) || (reqlvl < actlvl))
        qintr = reqlvl;
    }
return qintr;
}

void pi_dismiss (void)
{
pi_act = pi_act & ~pi_l2bit[pi_m2lvl[pi_act]];          /* clr left most bit */
pi_eval ();                                             /* eval pi system */
return;
}

/*
 * This sequence of instructions is a mix that hopefully
 * represents a resonable instruction set that is a close 
 * estimate to the normal calibrated result.
 */

static const char *pdp10_clock_precalibrate_commands[] = {
    "-m 100 ADDM 0,110",
    "-m 101 ADDI 0,1",
    "-m 102 JRST 100",
    "PC 100",
    NULL};

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
flags = 0;                                              /* clear flags */
its_1pr = 0;                                            /* clear 1-proceed */
ebr = ubr = 0;                                          /* clear paging */
pi_enb = pi_act = pi_prq = 0;                           /* clear PI */
apr_enb = apr_flg = apr_lvl = 0;                        /* clear APR */
pcst = 0;                                               /* clear PC samp */
rlog = 0;                                               /* clear reg log */
hsb = (Q_ITS)? UC_HSBITS: UC_HSBDEC;                    /* set HSB */
set_dyn_ptrs ();
set_ac_display (ac_cur);
pi_eval ();
if (M == NULL)
    M = (d10 *) calloc (MAXMEMSIZE, sizeof (d10));
if (M == NULL)
    return SCPE_MEM;
sim_vm_pc_value = &pdp10_pc_value;
sim_vm_is_subroutine_call = &cpu_is_pc_a_subroutine_call;
sim_clock_precalibrate_commands = pdp10_clock_precalibrate_commands;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else
    return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

static const char *cpu_next_caveats =
"The NEXT command in the PDP10 simulator currently will enable stepping\n"
"across subroutine calls which are initiated by the PUSHJ, JSP, JSA and\n"
"JRA instructions.  This stepping works by dynamically establishing\n"
"breakpoints at the 10 memory addresses immediately following the\n"
"instruction which initiated the subroutine call.  These dynamic\n"
"breakpoints are automatically removed once the simulator returns to the\n"
"sim> prompt for any reason. If the called routine returns somewhere\n"
"other than one of these locations due to a trap, stack unwind or any\n"
"other reason, instruction execution will continue until some other\n"
"reason causes execution to stop.\n";

t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs)
{
#define MAX_SUB_RETURN_SKIP 10
static t_addr returns[MAX_SUB_RETURN_SKIP+1] = {0};
static t_bool caveats_displayed = FALSE;
a10 ea;
d10 inst, indrct;
int32 i, pflgs = 0;
t_addr adn, max_returns = MAX_SUB_RETURN_SKIP;
int32 xr;

if (!caveats_displayed) {
    caveats_displayed = TRUE;
    sim_printf ("%s", cpu_next_caveats);
    }
if (SCPE_OK != get_aval ((saved_PC & AMASK), &cpu_dev, &cpu_unit))  /* get data */
    return FALSE;
inst = sim_eval[0];
switch (GET_OP(inst))
    {
    case 0260:              /* PUSHJ */
    case 0265:              /* JSP */
    case 0266:              /* JSA */
    case 0267:              /* JRA */
        for (indrct = inst, i = 0; i < ind_max; i++) {/* calc eff addr */
            ea = GET_ADDR (indrct);
            xr = GET_XR (indrct);
            if (xr)
                ea = (ea + ((a10) XR (xr, MM_EA))) & AMASK;
            if (TST_IND (indrct))
                indrct = Read (ea, MM_EA);
            else break;
            }
        if (i >= ind_max)
            return FALSE;                       /* too many ind? stop */
        returns[0] = (saved_PC & AMASK) + (1 - fprint_sym (stdnul, (saved_PC & AMASK), sim_eval, &cpu_unit, SWMASK ('M')));
        if (((t_addr)ea > returns[0]) && ((ea - returns[0]) < max_returns))
            max_returns = (t_addr)(ea - returns[0]);
        for (adn=1; adn<max_returns; adn++)
            returns[adn] = returns[adn-1] + 1;  /* Possible skip return */
        returns[adn] = 0;                       /* Make sure the address list ends with a zero */
        *ret_addrs = returns;
        return TRUE;
    default:
        return FALSE;
    }
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
if (vptr == NULL)
    return SCPE_ARG;
if (ea < AC_NUM)
    *vptr = AC(ea) & DMASK;
else {
    if (sw & SWMASK ('V')) {
        ea = conmap (ea, PTF_CON, sw);
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    *vptr = M[ea] & DMASK;
    }
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
if (ea < AC_NUM)
    AC(ea) = val & DMASK;
else {
    if (sw & SWMASK ('V')) {
        ea = conmap (ea, PTF_CON | PTF_WR, sw);
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    M[ea] = val & DMASK;
    }
return SCPE_OK;
}

/* Set current AC pointers for SCP */

void set_ac_display (d10 *acbase)
{
REG *rptr;
int i;

rptr = find_reg ("AC0", NULL, &cpu_dev);
if (rptr == NULL)
    return;
for (i = 0; i < AC_NUM; i++, rptr++)
    rptr->loc = (void *) (acbase + i);
return;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, HIST_MAX, &r);
if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
    return SCPE_ARG;
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
        return SCPE_MEM;
    hst_lnt = lnt;
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 k, di, lnt;
CONST char *cptr = (CONST char *) desc;
t_stat r;
InstHistory *h;

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0))
        return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0)
    di = di + hst_lnt;
fprintf (st, "PC      AC            EA      IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        fprintf (st, "%06o  ", h->pc & AMASK);
        fprint_val (st, h->ac, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprintf (st, "%06o  ", h->ea);
        sim_eval[0] = h->ir;
        if ((fprint_sym (st, h->pc & AMASK, sim_eval, &cpu_unit, SWMASK ('M'))) > 0) {
            fputs ("(undefined) ", st);
            fprint_val (st, h->ir, 8, 36, PV_RZRO);
            }
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

/* Set serial */

t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 lnt;
t_stat r;

if (cptr == NULL) {
    apr_serial = -1;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, 077777, &r);
if ((r != SCPE_OK) || (lnt <= 0) || (!Q_ITS && lnt < 4096))
    return SCPE_ARG;
apr_serial = lnt & 077777;
return SCPE_OK;
}

/* Show serial */

t_stat cpu_show_serial (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Serial: " );
if( (apr_serial == -1) || (!Q_ITS && apr_serial < 4096) ) {
    fprintf (st, "%d (default)", (Q_ITS)? UC_SERITS: UC_SERDEC);
    return SCPE_OK;
    }
fprintf (st, "%d", apr_serial);
return SCPE_OK;
}
