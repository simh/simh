/* i7094_cpu.c: IBM 7094 CPU simulator

   Copyright (c) 2003-2011, Robert M. Supnik

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

   cpu          7094 central processor

   31-Dec-11    RMS     Select traps have priority over protect traps
                        Added SRI, SPI
                        Fixed user mode and relocation from CTSS RPQ documentation
   16-Jul-10    RMS     Fixed user mode protection (Dave Pitts)
                        Fixed issues in storage nullification mode
   28-Apr-07    RMS     Removed clock initialization
   29-Oct-06    RMS     Added additional expanded core instructions
   17-Oct-06    RMS     Fixed the fix in halt IO wait loop
   16-Jun-06    RMS     Fixed bug in halt IO wait loop

   The register state for the 7094 is:

   AC<S,Q,P,1:35>       accumulator
   MQ<S,1:35>           multiplier-quotient register
   SI<S,1:35>           storage indicators
   KEYS<0:35>           front panel keys (switches)
   IC<0:14>             instruction counter (called PC here)
   XR<0:14>[8]          index registers (XR[0] is always 0)
   SSW<0:5>             sense switches
   SLT<0:3>             sense lights
   OVF                  AC overflow
   MQO                  MQ overflow
   DVC                  divide check
   IOC                  I/O check
   TTRAP                transfer trap mode
   CTRAP                copy trap mode (for 709 compatibility)
   FTRAP                floating trap mode (off is 704 compatibility)
   STRAP                select trap mode
   STORN                storage nullifcation mode
   MULTI                multi-tag mode (7090 compatibility)

   CTSS required a set of special features: memory extension (to 65K),
   protection, and relocation.  Additional state:

   USER                 user mode
   RELOCM               relocation mode
   USER_BUF             user mode buffer
   RELOC_BUF            relocation buffer
   INST_BASE            instruction memory select (A vs B core)
   DATA_BASE            data memory select (A vs B core)
   IND_RELOC<0:6>       relocation value (block number)
   IND_START<0:6>       start address block
   IND_LIMIT<0:6>       limit address block

   The 7094 had five instruction formats: memory reference,
   memory reference with count, convert, decrement, and immediate.

      00000000011 11 1111 112 222222222333333
     S12345678901 23 4567 890 123456789012345
    +------------+--+----+---+---------------+
    |   opcode   |ND|0000|tag|     address   | memory reference
    +------------+--+----+---+---------------+

      00000000011 111111 112 222222222333333
     S12345678901 234567 890 123456789012345
    +------------+------+---+---------------+
    |   opcode   | count|tag|     address   | memory reference
    +------------+------+---+---------------+ with count

      000000000 11111111 11 2 222222222333333
     S123456789 01234567 89 0 123456789012345
    +----------+--------+--+-+---------------+
    |  opcode  | count  |00|X|    address    | convert
    +----------+--------+--+-+---------------+

      00 000000011111111 112 222222222333333
     S12 345678901234567 890 123456789012345
    +---+---------------+---+---------------+
    |opc|   decrement   |tag|     address   | decrement
    +---+---------------+---+---------------+

      00000000011 111111 112222222222333333
     S12345678901 234567 890123456789012345
    +------------+------+------------------+
    |   opcode   |000000|   immediate      | immediate
    +------------+------+------------------+

   This routine is the instruction decode routine for the 7094.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until a stop condition occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        illegal instruction
        illegal I/O operation for device
        illegal I/O operation for channel
        breakpoint encountered
        nested XEC's exceeding limit
        divide check
        I/O error in I/O simulator

   2. Data channel traps.  The 7094 is a channel-based system.
      Channels can generate traps for errors and status conditions.
      Channel trap state:

        ch_flags[0..7]  flags for channels A..H
        chtr_enab       channel trap enables
        chtr_inht       channel trap inhibit due to trap (cleared by RCT)
        chtr_inhi       channel trap inhibit due to XEC, ENB, RCT, LRI,
                        LPI, SEA, SEB (cleared after one instruction)

      Channel traps are summarized in variable chtr_pend.

   3. Arithmetic.  The 7094 uses signed magnitude arithmetic for
      integer and floating point calculations, and 2's complement
      arithmetic for indexing calculations.

   4. Adding I/O devices.  These modules must be modified:

        i7094_defs.h    add device definitions
        i7094_io.c      add device address mapping
        i7094_sys.c     add sim_devices table entry
*/

#include "i7094_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = (PC | inst_base)

#define HIST_MIN        64
#define HIST_MAX        (2 << 18)
#define HIST_CH_C       1                               /* include channel */
#define HIST_CH_I       2                               /* include IO */

#define HALT_IO_LIMIT   ((2 << 18) + 1)                 /* max wait to stop */

#define EAMASK          (mode_storn? A704_MASK: AMASK)  /* eff addr mask */

t_uint64 *M = NULL;                                     /* memory */
t_uint64 AC = 0;                                        /* AC */
t_uint64 MQ = 0;                                        /* MQ */
t_uint64 SI = 0;                                        /* indicators */
t_uint64 KEYS = 0;                                      /* storage keys */
uint32 PC = 0;                                          /* PC (IC) */
uint32 oldPC = 0;                                       /* prior PC */
uint32 XR[8] = { 0 };                                   /* index registers */
uint32 SSW = 0;                                         /* sense switches */
uint32 SLT = 0;                                         /* sense lights */
uint32 ch_req = 0;                                      /* channel requests */
uint32 chtr_pend = 0;                                   /* chan trap pending */
uint32 chtr_inht = 0;                                   /* chan trap inhibit trap */
uint32 chtr_inhi = 0;                                   /* chan trap inhibit inst */
uint32 chtr_enab = 0;                                   /* chan trap enables */
uint32 mode_ttrap = 0;                                  /* transfer trap mode */
uint32 mode_ctrap = 0;                                  /* copy trap mode */
uint32 mode_strap = 0;                                  /* select trap mode */
uint32 mode_ftrap = 0;                                  /* floating trap mode */
uint32 mode_storn = 0;                                  /* storage nullification */
uint32 mode_multi = 0;                                  /* multi-index mode */
uint32 ind_ovf = 0;                                     /* overflow */
uint32 ind_mqo = 0;                                     /* MQ overflow */
uint32 ind_dvc = 0;                                     /* divide check */
uint32 ind_ioc = 0;                                     /* IO check */
uint32 cpu_model = I_9X|I_94;                           /* CPU type */
uint32 mode_user = 0;                                   /* (CTSS) user mode */
uint32 mode_reloc = 0;                                  /* (CTSS) relocation mode */
uint32 user_buf = 0;                                    /* (CTSS) user mode buffer */
uint32 reloc_buf = 0;                                   /* (CTSS) reloc mode buffer */
uint32 ind_reloc = 0;                                   /* (CTSS) relocation */
uint32 ind_start = 0;                                   /* (CTSS) prot start */
uint32 ind_limit = 0;                                   /* (CTSS) prot limit */
uint32 inst_base = 0;                                   /* (CTSS) inst A/B sel */
uint32 data_base = 0;                                   /* (CTSS) data A/B sel */
uint32 xec_max = 16;                                    /* XEC chain limit */
uint32 ht_pend = 0;                                     /* HTR pending */
uint32 ht_addr = 0;                                     /* HTR address */
uint32 stop_illop = 1;                                  /* stop on ill op */
uint32 cpu_astop = 0;                                   /* address stop */

uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
uint32 hst_ch = 0;                                      /* channel history */
InstHistory *hst = NULL;                                /* instruction history */

extern uint32 ch_sta[NUM_CHAN];
extern uint32 ch_flags[NUM_CHAN];
extern DEVICE mt_dev[NUM_CHAN];
extern DEVICE ch_dev[NUM_CHAN];

/* Forward and external declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_bool ReadI (uint32 va, t_uint64 *dat);
t_bool Read (uint32 va, t_uint64 *dat);
t_bool Write (uint32 va, t_uint64 dat);
void WriteTA (uint32 pa, uint32 addr);
void WriteTAD (uint32 pa, uint32 addr, uint32 decr);
void TrapXfr (uint32 newpc);
t_bool fp_trap (uint32 spill);
t_bool prot_trap (uint32 decr);
t_bool sel_trap (uint32 va);
t_bool cpy_trap (uint32 va);
uint32 get_xri (uint32 tag);
uint32 get_xrx (uint32 tag);
void put_xr (uint32 tag, uint32 dat);
t_stat cpu_fprint_one_inst (FILE *st, uint32 pc, uint32 rpt, uint32 ea,
    t_uint64 ir, t_uint64 ac, t_uint64 mq, t_uint64 si, t_uint64 opnd);

extern uint32 chtr_eval (uint32 *decr);
extern void op_add (t_uint64 sr);
extern void op_mpy (t_uint64 ac, t_uint64 sr, uint32 sc);
extern t_bool op_div (t_uint64 sr, uint32 sc);
extern uint32 op_fad (t_uint64 sr, t_bool norm);
extern uint32 op_fmp (t_uint64 sr, t_bool norm);
extern uint32 op_fdv (t_uint64);
extern uint32 op_dfad (t_uint64 shi, t_uint64 slo, t_bool norm);
extern uint32 op_dfmp (t_uint64 shi, t_uint64 slo, t_bool norm);
extern uint32 op_dfdv (t_uint64 shi, t_uint64 slo);
extern void op_als (uint32 ea);
extern void op_ars (uint32 ea);
extern void op_lls (uint32 ea);
extern void op_lrs (uint32 ea);
extern void op_lgl (uint32 ea);
extern void op_lgr (uint32 ea);
extern t_stat op_pse (uint32 ea);
extern t_stat op_mse (uint32 ea);
extern t_stat ch_op_ds (uint32 ch, uint32 ds, uint32 unit);
extern t_stat ch_op_nds (uint32 ch, uint32 ds, uint32 unit);
extern t_stat ch_op_start (uint32 ch, uint32 clc, t_bool reset);
extern t_stat ch_op_store (uint32 ch, t_uint64 *dat);
extern t_stat ch_op_store_diag (uint32 ch, t_uint64 *dat);
extern t_stat ch_proc (uint32 ch);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, STDMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, PC, ASIZE) },
    { ORDATA (AC, AC, 38) },
    { ORDATA (MQ, MQ, 36) },
    { ORDATA (SI, SI, 36) },
    { ORDATA (KEYS, KEYS, 36) },
    { ORDATA (XR1, XR[1], 15) },
    { ORDATA (XR2, XR[2], 15) },
    { ORDATA (XR3, XR[3], 15) },
    { ORDATA (XR4, XR[4], 15) },
    { ORDATA (XR5, XR[5], 15) },
    { ORDATA (XR6, XR[6], 15) },
    { ORDATA (XR7, XR[7], 15) },
    { FLDATA (SS1, SSW, 5) },
    { FLDATA (SS2, SSW, 4) },
    { FLDATA (SS3, SSW, 3) },
    { FLDATA (SS4, SSW, 2) },
    { FLDATA (SS5, SSW, 1) },
    { FLDATA (SS6, SSW, 0) },
    { FLDATA (SL1, SLT, 3) },
    { FLDATA (SL2, SLT, 2) },
    { FLDATA (SL3, SLT, 1) },
    { FLDATA (SL4, SLT, 0) },
    { FLDATA (OVF, ind_ovf, 0) },
    { FLDATA (MQO, ind_mqo, 0) },
    { FLDATA (DVC, ind_dvc, 0) },
    { FLDATA (IOC, ind_ioc, 0) },
    { FLDATA (TTRAP, mode_ttrap, 0) },
    { FLDATA (CTRAP, mode_ctrap, 0) },
    { FLDATA (STRAP, mode_strap, 0) },
    { FLDATA (FTRAP, mode_ftrap, 0) },
    { FLDATA (STORN, mode_storn, 0) },
    { FLDATA (MULTI, mode_multi, 0) },
    { ORDATA (CHREQ, ch_req, NUM_CHAN) },
    { FLDATA (CHTR_PEND, chtr_pend, 0) },
    { FLDATA (CHTR_INHT, chtr_inht, 0) },
    { FLDATA (CHTR_INHI, chtr_inhi, 0) },
    { ORDATA (CHTR_ENAB, chtr_enab, 30) },
    { FLDATA (USERM, mode_user, 0) },
    { FLDATA (RELOCM, mode_reloc, 0) },
    { FLDATA (USERBUF, user_buf, 0) },
    { FLDATA (RELOCBUF, reloc_buf, 0) },
    { FLDATA (IMEM, inst_base, BCORE_V) },
    { FLDATA (DMEM, data_base, BCORE_V) },
    { GRDATA (RELOC, ind_reloc, 8, VA_N_BLK, VA_V_BLK) },
    { GRDATA (START, ind_start, 8, VA_N_BLK, VA_V_BLK) },
    { GRDATA (LIMIT, ind_limit, 8, VA_N_BLK, VA_V_BLK) },
    { ORDATA (OLDPC, oldPC, ASIZE), REG_RO },
    { BRDATA (PCQ, pcq, 8, ASIZE, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { FLDATA (HTPEND, ht_pend, 0) },
    { ORDATA (HTADDR, ht_addr, ASIZE) },
    { DRDATA (XECMAX, xec_max, 8), PV_LEFT + REG_NZ },
    { ORDATA (WRU, sim_int_char, 8) },
    { FLDATA (STOP_ILL, stop_illop, 0) },
    { ORDATA (MODEL, cpu_model, 4), REG_HRO },
    { NULL }
    };

MTAB cpu_mod[] = {
    { MTAB_XTD | MTAB_VDV, I_9X|I_94|I_CT, "MODEL", "CTSS",
      &cpu_set_model, &cpu_show_model, NULL },
    { MTAB_XTD | MTAB_VDV, I_9X|I_94, NULL, "7094",
      &cpu_set_model, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, I_9X, NULL, "7090",
      &cpu_set_model, NULL, NULL }, 
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, PASIZE, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG
    };

/* Instruction decode table */

const uint8 op_flags[1024] = {
 I_XN      , 0         , 0         , 0         ,        /* +000 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_XN|I_9X , I_XN      , 0         ,        /* +020 */
 I_XN      , 0         , I_XN      , I_XN      ,
 I_XN      , I_XN      , I_XN      , I_XN      ,
 0         , 0         , 0         , 0         ,
 I_XN|I_9X , I_9X      , I_XN|I_9X , I_9X      ,        /* +040 */
 I_9X      , 0         , I_XN|I_9X , 0         ,
 0         , I_9X      , 0         , 0         ,
 I_9X      , I_9X      , I_9X      , I_9X      ,
 I_XN      , I_XN      , I_XN      , I_XN      ,        /* +060 */
 I_XN      , I_XN      , I_XN      , I_XN      ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_XN|I_CT , 0         , 0         ,        /* +100 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , I_9X      , I_9X      , I_9X      ,
 I_XN      , 0         , 0         , 0         ,        /* +120 */
 0         , 0         , 0         , 0         ,
 0         , I_9X      , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , 0         , 0         , 0         ,        /* +140 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , I_XN|I_9X , I_XN|I_9X , 0         ,        /* +160 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , 0         , 0         ,        /* +200 */
 I_XNR     , I_XNR     , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, I_XNR     , 0         , 0         ,        /* +220 */
 I_XNR|I_9X, I_XNR     , I_XNR|I_9X, I_XNR     ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, I_XNR     , 0         , 0         ,        /* +240 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XND|I_94, 0         , 0         ,        /* +260 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XND|I_94, I_XNR     , I_XND|I_94,        /* +300 */
 I_XNR|I_9X, I_XND|I_94, I_XNR|I_9X, I_XND|I_94,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , I_XNR|I_9X, 0         ,        /* +320 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , 0         , 0         ,        /* +340 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , I_XNR     , 0         , 0         ,        /* +360 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XNR|I_9X, I_XNR     , 0         ,        /* +400 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* +420 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, I_XNR|I_9X, I_XNR|I_9X, I_XNR|I_94,        /* +440 */
 I_XNR|I_9X, I_XNR|I_9X, I_XNR|I_9X, 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , 0         , 0         , 0         ,        /* +460 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , I_XNR     , 0         ,        /* +500 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , I_XNR     , 0         ,        /* +520 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_R       , I_R       , 0         , 0         ,
 I_XN      , I_XN      , I_XN      , I_XN      ,        /* +540 */
 I_XN      , I_XN      , I_XN      , I_XN      ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , I_XNR|I_CT, 0         ,        /* +560 */
 I_XNR     , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_XN      , I_XN      , 0         ,        /* +600 */
 I_XN|I_9X , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , I_XNR     , I_XNR     , 0         ,        /* +620 */
 0         , I_XNR|I_9X, 0         , 0         ,
 I_XNR|I_9X, 0         , 0         , 0         ,
 I_R       , 0         , I_R|I_94  , 0         ,
 I_XN      , I_XN      , I_XN      , I_XN      ,        /* +640 */
 I_XN|I_9X , I_XN|I_9X , I_XN|I_9X , I_XN|I_9X ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* +660 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , 0         , 0         , 0         ,        /* +700 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* +720 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* +740 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , I_94      , 0         ,
 I_X       , 0         , I_X       , I_X       ,        /* +760 */
 I_X       , I_X       , I_X       , I_X       ,
 I_X       , I_X       , I_X       , 0         ,
 0         , 0         , 0         , 0         ,

 I_XN      , 0         , 0         , 0         ,        /* -000 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_XN|I_9X , I_XN      , 0         ,        /* -020 */
 I_XN      , 0         , I_XN      , I_XN      ,
 I_XN      , I_XN      , I_XN      , I_XN      ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -040 */
 0         , 0         , I_9X      , 0         ,
 0         , I_9X      , 0         , 0         ,
 I_9X      , I_9X      , I_9X      , I_9X      ,
 I_XN|I_9X , I_XN|I_9X , I_XN|I_9X , I_XN|I_9X ,        /* -060 */
 I_XN|I_9X , I_XN|I_9X , I_XN|I_9X , I_XN|I_9X ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_XN|I_CT , 0         , 0         ,        /* -100 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , I_9X      , I_9X      , I_9X      ,
 I_XN      , 0         , 0         , 0         ,        /* -120 */
 0         , 0         , 0         , 0         ,
 I_9X      , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN|I_9X , 0         , 0         , 0         ,        /* -140 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , I_9X      , I_9X      , I_9X      ,
 0         , 0         , 0         , 0         ,        /* -160 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , 0         , 0         ,        /* -200 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -220 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XND|I_94, I_XND|I_94, 0         , 0         ,        /* -240 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XND|I_94, 0         , 0         ,        /* -260 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XND|I_94, I_XNR     , I_XND|I_94,        /* -300 */
 I_XNR|I_9X, I_XND|I_94, I_XNR|I_9X, I_XND|I_94,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , 0         , 0         ,        /* -320 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , 0         , 0         , 0         ,        /* -340 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -360 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , 0         , 0         ,        /* -400 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -420 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -440 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -460 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR     , I_XNR     , 0         , 0         ,        /* -500 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , 0         , 0         ,        /* -520 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_R       , I_R       , 0         , 0         ,
 I_XN      , I_XN      , I_XN      , I_XN      ,        /* -540 */
 I_XN      , I_XN      , I_XNR     , I_XN      ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -560 */
 I_XNR|I_CT, 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XN      , I_CT      , I_XNR|I_9X, I_XN|I_94 ,        /* -600 */
 I_CT      , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_XNR|I_9X, 0         , 0         , 0         ,        /* -620 */
 0         , I_XNR     , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_R       , 0         , I_R|I_94  , 0         ,
 I_XN      , I_XN      , I_XN      , I_XN      ,        /* -640 */
 I_XN|I_9X , I_XN|I_9X , I_XN|I_9X , I_XN|I_9X ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -660 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 I_9X      , 0         , 0         , 0         ,        /* -700 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -720 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,        /* -740 */
 0         , 0         , 0         , 0         ,
 0         , 0         , 0         , 0         ,
 0         , 0         , I_94      , 0         ,
 I_X       , I_X|I_CT  , 0         , I_X       ,        /* -760 */
 0         , I_X       , 0         , 0         ,
 0         , 0         , I_X       , I_X       ,
 I_9X      , 0         , 0         , 0
 };

/* Instruction execution routine */

t_stat sim_instr (void)
{
t_stat reason = SCPE_OK;
t_uint64 IR, SR, t, t1, t2, sr1;
uint32 op, fl, tag, tagi, addr, ea;
uint32 ch, dec, xr, xec_cnt, trp;
uint32 i, j, sc, s1, s2, spill;
t_bool tracing;

/* Restore register state */

ch_set_map ();                                          /* set dispatch map */
if (!(cpu_model & (I_94|I_CT)))                         /* ~7094? MTM always on */
    mode_multi = 1;
inst_base = inst_base & ~AMASK;                         /* A/B sel is 1b */
data_base = data_base & ~AMASK;
ind_reloc = ind_reloc & VA_BLK;                         /* canonical form */
ind_start = ind_start & VA_BLK;
ind_limit = (ind_limit & VA_BLK) | VA_OFF;
chtr_pend = chtr_eval (NULL);                           /* eval chan traps */
tracing = ((hst_lnt != 0) || DEBUG_PRS (cpu_dev));

if (ht_pend) {                                          /* HTR pending? */
    oldPC = (PC - 1) & AMASK;
    ht_pend = 0;                                        /* clear flag */
    PCQ_ENTRY;
    if (mode_ttrap) {                                   /* trap? */
        WriteTA (TRAP_STD_SAV, oldPC);                  /* save PC */
        TrapXfr (TRAP_TRA_PC);                          /* trap */
        }
    else PC = ht_addr;                                  /* branch */
    }

/* Main instruction fetch/decode loop */

while (reason == SCPE_OK) {                             /* loop until error */

    if (cpu_astop) {                                    /* debug stop? */
        cpu_astop = 0;
        reason = SCPE_STOP;
        break;
        }

    if (sim_interval <= 0) {                            /* intv cnt expired? */
        if ((reason = sim_process_event ()))            /* process events */
            break;
        chtr_pend = chtr_eval (NULL);                   /* eval chan traps */
        }

    for (i = 0; ch_req && (i < NUM_CHAN); i++) {        /* loop thru channels */
        if (ch_req & REQ_CH (i)) {                      /* channel request? */
            if ((reason = ch_proc (i)))
                break;
            }
        chtr_pend = chtr_eval (NULL);
        if (reason)                                     /* error? */
            break;
        }

    if (chtr_pend) {                                    /* channel trap? */
        addr = chtr_eval (&trp);                        /* get trap info, clr */
        chtr_inht = 1;                                  /* inhibit traps */
        chtr_pend = 0;                                  /* no trap pending */
        WriteTAD (addr, PC, trp);                       /* wr trap addr,flag */
        IR = ReadP (addr + 1);                          /* get trap instr */
        oldPC = PC;                                     /* save current PC */
        }

    else {      
        if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {  /* breakpoint? */
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
            }
        if (chtr_inhi) {                                /* 1 cycle inhibit? */
            chtr_inhi = 0;                              /* clear */
            chtr_pend = chtr_eval (NULL);               /* re-evaluate */
            }
        else if (cpu_model & I_CT) {                    /* CTSS? */
            mode_user = user_buf;                       /* load modes from buffers */
            mode_reloc = reloc_buf;
            }
        oldPC = PC;                                     /* save current PC */
        PC = (PC + 1) & EAMASK;                         /* increment PC */
        if (!ReadI (oldPC, &IR))                        /* get inst; trap? */
            continue;
        }

    sim_interval = sim_interval - 1;
    xec_cnt = 0;                                        /* clear XEC cntr */

    XEC:

    tag = GET_TAG (IR);                                 /* get tag */
    addr = (uint32) IR & EAMASK;                        /* get base addr */

/* Decrement format instructions */

    if (IR & INST_T_DEC) {                              /* decrement type? */
        op = GET_OPD (IR);                              /* get opcode */
        dec = GET_DEC (IR);                             /* get decrement */
        xr = get_xrx (tag);                             /* get xr, upd MTM */
        if (tracing) {                                  /* trace or history? */
            if (hst_lnt)                                /* history enabled? */
                cpu_ent_hist (oldPC|HIST_PC, xr, IR, 0);
            if (DEBUG_PRS (cpu_dev))
                cpu_fprint_one_inst (sim_deb, oldPC|HIST_PC, 0, xr,
                    IR, AC, MQ, SI, 0);
            }
        switch (op) {

        case 01:                                        /* TXI */
            put_xr (tag, xr + dec);                     /* xr += decr */
            PCQ_ENTRY;
            if (mode_ttrap) {                           /* trap? */
                WriteTA (TRAP_STD_SAV, oldPC);          /* save PC */
                TrapXfr (TRAP_TRA_PC);                  /* trap */
                }
            else PC = addr;                             /* branch */
            break;      

        case 02:                                        /* TIX */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (xr > dec) {                             /* if xr > decr */
                put_xr (tag, xr - dec);                 /* xr -= decr */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = addr;                         /* branch */
                }
            break;      

        case 03:                                        /* TXH */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (xr > dec) {                             /* if xr > decr */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = addr;                         /* branch */
                }
            break;

        case 05:                                        /* STR */
            WriteTA (TRAP_STD_SAV, PC);                 /* save inst+1 */
            PCQ_ENTRY;
            PC = TRAP_STR_PC;                           /* branch to 2 */
            break;

        case 06:                                        /* TNX */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (xr > dec)                               /* if xr > decr */
                put_xr (tag, xr - dec);
            else {                                      /* xr -= decr */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = addr;                         /* branch */
                }
            break;      

        case 07:                                        /* TXL */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (xr <= dec) {                            /* if xr <= decr */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = addr;                         /* branch */
                }
            break;
            }
        }                                               /* end if */

/* Normal format instructions */

    else {
        op = GET_OPC (IR);                              /* get opcode */
        fl = op_flags[op];                              /* get flags */
        if (fl & I_MODEL & ~cpu_model) {                /* invalid for model? */
            if (stop_illop)                             /* possible stop */
                reason = STOP_ILLEG;
            continue;
            }
        if (tag && (fl & I_X))                          /* tag and indexable? */
            ea = (addr - get_xri (tag)) & EAMASK;       /* do indexing */
        else ea = addr;
        if (TST_IND (IR) && (fl & I_N)) {               /* indirect? */
            if (!ReadI (ea, &SR))                       /* get ind; trap? */
                continue;
            addr = (uint32) SR & EAMASK;                /* get address */
            tagi = GET_TAG (SR);                        /* get tag */
            if (tagi)                                   /* tag? */
                ea = (addr - get_xri (tagi)) & EAMASK;  /* do indexing */
            else ea = addr;
            }
        if ((fl & I_R) && !Read (ea, &SR))              /* read opnd; trap? */
            continue;
        else if (fl & I_D) {                            /* double prec? */
            if ((ea & 1) && fp_trap (TRAP_F_ODD))
                continue;
            if (!Read (ea, &SR))                        /* SR gets high */
                continue;
            if (!Read (ea | 1, &sr1))                   /* "sr1" gets low */
                continue;
            }
        if (tracing) {                                  /* tracing or history? */
            if (hst_lnt)                                /* history enabled? */
                cpu_ent_hist (oldPC|HIST_PC, ea, IR, SR);
            if (DEBUG_PRS (cpu_dev))
                cpu_fprint_one_inst (sim_deb, oldPC|HIST_PC, 0, ea,
                    IR, AC, MQ, SI, SR);
            }
        switch (op) {                                   /* case on opcode */

/* Positive instructions */

        case 00000:                                     /* HTR */
        case 01000:                                     /* also -HTR */
            if (prot_trap (0))                          /* user mode? */
                break;
            ht_pend = 1;                                /* transfer pending */
            ht_addr = ea;                               /* save address */
            reason = STOP_HALT;                         /* halt if I/O done */
            break;

        case 00020:                                     /* TRA */
        case 01020:                                     /* also -TRA */
            PCQ_ENTRY;
            if (mode_ttrap) {                           /* trap? */
                WriteTA (TRAP_STD_SAV, oldPC);          /* save PC */
                TrapXfr (TRAP_TRA_PC);                  /* trap */
                }
            else PC = ea;                               /* branch */
            break;

        case 00021:                                     /* TTR */
            PCQ_ENTRY;
            PC = ea;                                    /* branch, no trap */
            break;

        case 00040:                                     /* TLQ */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            s1 = (AC & AC_S)? 1: 0;                     /* get AC, MQ sign, */
            s2 = (MQ & SIGN)? 1: 0;                     /* magnitude */
            t1 = AC & AC_MMASK;
            t2 = MQ & MMASK;                            /* signs differ? */
            if ((s1 != s2)? s2:                         /* y, br if MQ- */
                ((t1 != t2) && (s2 ^ (t1 > t2)))) {     /* n, br if sgn-^AC>MQ */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00041:                                     /* IIA */
            SI = SI ^ (AC & DMASK);
            break;

        case 00042:                                     /* TIO */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((SI & AC) == (AC & DMASK)) {            /* if ind on */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00043:                                     /* OAI */
            SI = SI | (AC & DMASK);
            break;

        case 00044:                                     /* PAI */
            SI = AC & DMASK;
            break;

        case 00046:                                     /* TIF */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((SI & AC) == 0) {                       /* if ind off */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00051:                                     /* IIR */
            SI = SI ^ (IR & RMASK);
            break;

        case 00054:                                     /* RFT */
            t = IR & RMASK;
            if ((SI & t) == 0)                          /* if ind off, skip */
                PC = (PC + 1) & EAMASK;
            break;

        case 00055:                                     /* SIR */
            SI = SI | (IR & RMASK);
            break;

        case 00056:                                     /* RNT */
            t = IR & RMASK;
            if ((SI & t) == t)                          /* if ind on, skip */
                PC = (PC + 1) & EAMASK;
            break;

        case 00057:                                     /* RIR */
            SI = SI & ~(IR & RMASK);
            break;

        case 00074:                                     /* TSX */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (tag)                                    /* save -inst loc */
                put_xr (tag, ~oldPC + 1);
            PCQ_ENTRY;
            if (mode_ttrap)                             /* trap? */
                TrapXfr (TRAP_TRA_PC);
            else PC = ea;                               /* branch */
            break;

        case 00100:                                     /* TZE */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((AC & AC_MMASK) == 0) {                 /* if AC Q,P,1-35 = 0 */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00101:                                     /* (CTSS) TIA */
            if (prot_trap (0))                          /* not user mode? */
                break;
            if (mode_ttrap) {                           /* trap? */
                WriteTA (TRAP_STD_SAV, oldPC);          /* save PC */
                TrapXfr (TRAP_TRA_PC);                  /* trap */
                }
            else {
                PCQ_ENTRY;
                PC = ea;
                inst_base = 0;
                }
            break;

        case 00114: case 00115: case 00116: case 00117: /* CVR */
            sc = GET_CCNT (IR);
            SR = ea;
            while (sc) {
                ea = (uint32) ((AC & 077) + SR) & EAMASK;
                if (!Read (ea, &SR))
                    break;
                AC = (AC & AC_S) | ((AC >> 6) & 0017777777777) |
                    (SR & INT64_C(0770000000000));
                sc--;
                }
            if ((sc == 0) && (IR & INST_T_CXR1))
                put_xr (1, (uint32) SR);
            break;

        case 00120:                                     /* TPL */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((AC & AC_S) == 0) {                     /* if AC + */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00131:                                     /* XCA */
            t = MQ;
            MQ = (AC & MMASK) | ((AC & AC_S)? SIGN: 0);
            AC = (t & MMASK) | ((t & SIGN)? AC_S: 0);
            break;

        case 00140:                                     /* TOV */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (ind_ovf)  {                             /* if overflow */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                ind_ovf = 0;                            /* clear overflow */
                }
            break;

        case 00161:                                     /* TQO */
            if (!mode_ftrap) {                          /* only in 704 mode */
                if (mode_ttrap)
                    WriteTA (TRAP_STD_SAV, oldPC);
                if (ind_mqo)  {                         /* if MQ overflow */
                    PCQ_ENTRY;
                    if (mode_ttrap)                     /* trap? */
                        TrapXfr (TRAP_TRA_PC);
                    else PC = ea;                       /* branch */
                    ind_mqo = 0;                        /* clear overflow */
                    }
                }
            break;

        case 00162:                                     /* TQP */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((MQ & SIGN) == 0)  {                    /* if MQ + */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00200:                                     /* MPY */
            op_mpy (0, SR, 043);
            break;

        case 00204:                                     /* VLM */
        case 00205:                                     /* for diagnostic */
            sc = GET_VCNT (IR);
            op_mpy (0, SR, sc);
            break;

        case 00220:                                     /* DVH */
            if (op_div (SR, 043)) {
                ind_dvc = 1;
                if (!prot_trap (0))
                    reason = STOP_DIVCHK;
                }
            break;

        case 00221:                                     /* DVP */
            if (op_div (SR, 043))
                ind_dvc = 1;
            break;

        case 00224:                                     /* VDH */
        case 00226:                                     /* for diagnostic */
            sc = GET_VCNT (IR);
            if (op_div (SR, sc)) {
                ind_dvc = 1;
                if (!prot_trap (0))
                    reason = STOP_DIVCHK;
                }
            break;

        case 00225:                                     /* VDP */
        case 00227:                                     /* for diagnostic */
            sc = GET_VCNT (IR);
            if (op_div (SR, sc))
                ind_dvc = 1;
            break;

        case 00240:                                     /* FDH */
            spill = op_fdv (SR);
            if (spill == TRAP_F_DVC) {
                ind_dvc = 1;
                if (!prot_trap (0))
                    reason = STOP_DIVCHK;
                }
            else if (spill)
                fp_trap (spill);
            break;

        case 00241:                                     /* FDP */
            spill = op_fdv (SR);
            if (spill == TRAP_F_DVC)
                ind_dvc = 1;
            else if (spill)
                fp_trap (spill);
            break;

        case 00260:                                     /* FMP */
            spill = op_fmp (SR, 1);                     /* MQ * SR */
            if (spill)
                fp_trap (spill);
            break;

        case 00261:                                     /* DFMP */
            spill = op_dfmp (SR, sr1, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00300:                                     /* FAD */
            spill = op_fad (SR, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00301:                                     /* DFAD */
            spill = op_dfad (SR, sr1, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00302:                                     /* FSB */
            spill = op_fad (SR ^ SIGN, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00303:                                     /* DFSB */
            spill = op_dfad (SR ^ SIGN, sr1, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00304:                                     /* FAM */
            spill = op_fad (SR & ~SIGN, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00305:                                     /* DFAM */
            spill = op_dfad (SR & ~SIGN, sr1, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00306:                                     /* FSM */
            spill = op_fad (SR | SIGN, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00307:                                     /* DFSM */
            spill = op_dfad (SR | SIGN, sr1, 1);
            if (spill)
                fp_trap (spill);
            break;

        case 00320:                                     /* ANS */
            SR = AC & SR;
            Write (ea, SR);
            break;

        case 00322:                                     /* ERA */
            AC = (AC ^ SR) & DMASK;                     /* AC S,Q cleared */
            break;

        case 00340:                                     /* CAS */
            s1 = (AC & AC_S)? 1: 0;                     /* get AC, MQ signs, */
            s2 = (SR & SIGN)? 1: 0;
            t1 = AC & AC_MMASK;                         /* magnitudes */
            t2 = SR & MMASK;
            if (s1 ^ s2) {                              /* diff signs? */
                if (s1)                                 /* AC < mem? skip 2 */
                    PC = (PC + 2) & EAMASK;
                }
            else if (t1 == t2)                          /* equal? skip 1 */
                PC = (PC + 1) & EAMASK;
            else if ((t1 < t2) ^ s1)                    /* AC < mem, AC +, or */
                PC = (PC + 2) & EAMASK;                 /* AC > mem, AC -? */
            break;

        case 00361:                                     /* ACL */
            t = (AC + SR) & DMASK;                      /* AC P,1-35 + SR */
            if (t < SR)                                 /* end around carry */
                t = (t + 1) & DMASK;
            AC = (AC & (AC_S | AC_Q)) | t;              /* preserve AC S,Q */
            break;

        case 00400:                                     /* ADD */
            op_add (SR);
            break;

        case 00401:                                     /* ADM */
            op_add (SR & MMASK);
            break;

        case 00402:                                     /* SUB */
            op_add (SR ^ SIGN);
            break;

        case 00420:                                     /* HPR */
            if (prot_trap (0))                          /* user mode? */
                break;
            reason = STOP_HALT;                         /* halt if I/O done */
            break;

        case 00440:                                     /* IIS */
            SI = SI ^ SR;
            break;

        case 00441:                                     /* LDI */
            SI = SR;
            break;

        case 00442:                                     /* OSI */
            SI = SI | SR;
            break;

        case 00443:                                     /* DLD */
            AC = (SR & MMASK) | ((SR & SIGN)? AC_S: 0); /* normal load */
            if (!Read (ea | 1, &SR))                    /* second load */
                break;
            MQ = SR;
            if (ea & 1)                                 /* trap after exec */
                fp_trap (TRAP_F_ODD);
            break;

        case 00444:                                     /* OFT */
            if ((SI & SR) == 0)                         /* skip if ind off */
                PC = (PC + 1) & EAMASK;
            break;

        case 00445:                                     /* RIS */
            SI = SI & ~SR;
            break;

        case 00446:                                     /* ONT */
            if ((SI & SR) == SR)                        /* skip if ind on */
                PC = (PC + 1) & EAMASK;
            break;

        case 00460:                                     /* LDA (704) */
            cpy_trap (PC);
            break;

        case 00500:                                     /* CLA */
            AC = (SR & MMASK) | ((SR & SIGN)? AC_S: 0);
            break;

        case 00502:                                     /* CLS */
            AC = (SR & MMASK) | ((SR & SIGN)? 0: AC_S);
            break;

        case 00520:                                     /* ZET */
            if ((SR & MMASK) == 0)                      /* skip if M 1-35 = 0 */
                PC = (PC + 1) & EAMASK;
            break;

        case 00522:                                     /* XEC */
            if (xec_cnt++ >= xec_max) {                 /* xec chain limit? */
                reason = STOP_XEC;                      /* stop */
                break;
                }
            IR = SR;                                    /* operand is new inst */
            chtr_inhi = 1;                              /* delay traps */
            chtr_pend = 0;                              /* no trap now */
            goto XEC;                                   /* start over */

        case 00534:                                     /* LXA */
            if (tag)                                    /* M addr -> xr */
                put_xr (tag, (uint32) SR);
            break;

        case 00535:                                     /* LAC */
            if (tag)                                    /* -M addr -> xr */
                put_xr (tag, NEG ((uint32) SR));
            break;

        case 00560:                                     /* LDQ */
            MQ = SR;
            break;

        case 00562:                                     /* (CTSS) LRI */
            if (prot_trap (0))                          /* user mode? */
                break;
            ind_reloc = ((uint32) SR) & VA_BLK;
            reloc_buf = 1;                              /* set mode buffer */
            chtr_inhi = 1;                              /* delay traps */
            chtr_pend = 0;                              /* no trap now */
            break;

        case 00564:                                     /* ENB */
            if (prot_trap (0))                          /* user mode? */
                break;
            chtr_enab = (uint32) SR;                    /* set enables */
            chtr_inht = 0;                              /* clear inhibit */
            chtr_inhi = 1;                              /* 1 cycle delay */
            chtr_pend = 0;                              /* no traps now */
            break;

        case 00600:                                     /* STZ */
            Write (ea, 0);
            break;

        case 00601:                                     /* STO */
            SR = (AC & MMASK) | ((AC & AC_S)? SIGN: 0);
            Write (ea, SR);
            break;

        case 00602:                                     /* SLW */
            Write (ea, AC & DMASK);
            break;

        case 00604:                                     /* STI */
            Write (ea, SI);
            break;

        case 00621:                                     /* STA */
            SR = (SR & ~AMASK) | (AC & AMASK);
            Write (ea, SR);
            break;

        case 00622:                                     /* STD */
            SR = (SR & ~XMASK) | (AC & XMASK);
            Write (ea, SR);
            break;

        case 00625:                                     /* STT */
            SR = (SR & ~TMASK) | (AC & TMASK);
            Write (ea, SR);
            break;

        case 00630:                                     /* STP */
            SR = (SR & ~PMASK) | (AC & PMASK);
            Write (ea, SR);
            break;

        case 00634:                                     /* SXA */
            SR = (SR & ~AMASK) |                        /* xr -> M addr */
                ((t_uint64) get_xrx (tag));
            Write (ea, SR);
            break;

        case 00636:                                     /* SCA */
            SR = (SR & ~AMASK) |                        /* -xr -> M addr */
                ((t_uint64) (NEG (get_xrx (tag)) & AMASK));
            Write (ea, SR);
            break;

        case 00700:                                     /* CPY (704) */
            cpy_trap (PC);
            break;

        case 00734:                                     /* PAX */
            if (tag)                                    /* AC addr -> xr */
                put_xr (tag, (uint32) AC);
            break;

        case 00737:                                     /* PAC */
            if (tag)                                    /* -AC addr -> xr */
                put_xr (tag, NEG ((uint32) AC));
            break;

        case 00754:                                     /* PXA */
            AC = get_xrx (tag);                         /* xr -> AC */
            break;

        case 00756:                                     /* PCA */
            AC = NEG (get_xrx (tag)) & AMASK;           /* -xr -> AC */
            break;

        case 00760:                                     /* PSE */
            reason = op_pse (ea);
            break;

        case 00761:                                     /* NOP */
            break;

        case 00763:                                     /* LLS */
            op_lls (ea);
            break;

        case 00765:                                     /* LRS */
            op_lrs (ea);
            break;

        case 00767:                                     /* ALS */
            op_als (ea);
            break;

        case 00771:                                     /* ARS */
            op_ars (ea);
            break;

        case 00774:                                     /* AXT */
            if (tag)                                    /* IR addr -> xr */
                put_xr (tag, addr);
            break;

/* Negative instructions */

        case 01021:                                     /* ESNT */
            if (prot_trap (0))                          /* user mode? */
                break;
            mode_storn = 1;                             /* enter nullification */
            PCQ_ENTRY;
            PC = ea;                                    /* branch, no trap */
            break;

        case 01042:                                     /* RIA */
            SI = SI & ~AC;
            break;

        case 01046:                                     /* PIA */
            AC = SI;
            break;

        case 01051:                                     /* IIL */
            SI = SI ^ ((IR & RMASK) << 18);
            break;

        case 01054:                                     /* LFT */
            t = (IR & RMASK) << 18;
            if ((SI & t) == 0)                          /* if ind off, skip */
                PC = (PC + 1) & EAMASK;
            break;

        case 01055:                                     /* SIL */
            SI = SI | ((IR & RMASK) << 18);
            break;

        case 01056:                                     /* LNT */
            t = (IR & RMASK) << 18;
            if ((SI & t) == t)                          /* if ind on, skip */
                PC = (PC + 1) & EAMASK;
            break;

        case 01057:                                     /* RIL */
            SI = SI & ~((IR & RMASK) << 18);
            break;

        case 01100:                                     /* TNZ */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((AC & AC_MMASK) != 0) {                 /* if AC != 0 */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 01101:                                     /* (CTSS) TIB */
            if (prot_trap (0))                          /* user mode? */
                break;
            if (mode_ttrap) {                           /* trap? */
                WriteTA (TRAP_STD_SAV, oldPC);          /* save PC */
                TrapXfr (TRAP_TRA_PC);                  /* trap */
                }
            else {
                PCQ_ENTRY;
                PC = ea;
                inst_base = BCORE_BASE;
                }
            break;

        case 01114: case 01115: case 01116: case 01117: /* CAQ */
            sc = GET_CCNT (IR);
            SR = ea;
            while (sc) {
                ea = (uint32) ((MQ >> 30) + SR) & EAMASK;
                if (!Read (ea, &SR))
                    break;
                MQ = ((MQ << 6) & DMASK) | (MQ >> 30);
                AC = (AC & AC_S) | ((AC + SR) & AC_MMASK);
                sc--;
                }
            if ((sc == 0) && (IR & INST_T_CXR1))
                put_xr (1, (uint32) SR);
            break;

        case 01120:                                     /* TMI */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if ((AC & AC_S) != 0)  {                    /* if AC - */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 01130:                                     /* XCL */
            t = MQ;
            MQ = AC & DMASK;
            AC = t;
            break;

        case 01140:                                     /* TNO */
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (!ind_ovf)  {                            /* if no overflow */
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            ind_ovf = 0;                                /* clear overflow */
            break;

        case 01154: case 01155: case 01156: case 01157: /* CRQ */
            sc = GET_CCNT (IR);
            SR = ea;
            while (sc) {
                ea = (uint32) ((MQ >> 30) + SR) & EAMASK;
                if (!Read (ea, &SR))
                    break;
                MQ = ((MQ << 6) & DMASK) | (SR >> 30);
                sc--;
                }
            if ((sc == 0) && (IR & INST_T_CXR1))
                put_xr (1, (uint32) SR);
            break;

        case 01200:                                     /* MPR */
            op_mpy (0, SR, 043);
            if (MQ & B1)
                AC = (AC & AC_S) | ((AC + 1) & AC_MMASK);
            break;

        case 01240:                                     /* DFDH */
            spill = op_dfdv (SR, sr1);
            if (spill == TRAP_F_DVC) {
                ind_dvc = 1;
                if (!prot_trap (0))
                    reason = STOP_DIVCHK;
                }
            else if (spill)
                fp_trap (spill);
            break;

        case 01241:                                     /* DFDP */
            spill = op_dfdv (SR, sr1);
            if (spill == TRAP_F_DVC)
                ind_dvc = 1;
            else if (spill)
                fp_trap (spill);
            break;

        case 01260:                                     /* UFM */
            spill = op_fmp (SR, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01261:                                     /* DUFM */
            spill = op_dfmp (SR, sr1, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01300:                                     /* UFA */
            spill = op_fad (SR, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01301:                                     /* DUFA */
            spill = op_dfad (SR, sr1, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01302:                                     /* UFS */
            spill = op_fad (SR ^ SIGN, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01303:                                     /* DUFS */
            spill = op_dfad (SR ^ SIGN, sr1, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01304:                                     /* UAM */
            spill = op_fad (SR & ~SIGN, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01305:                                     /* DUAM */
            spill = op_dfad (SR & ~SIGN, sr1, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01306:                                     /* USM */
            spill = op_fad (SR | SIGN, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01307:                                     /* DUSM */
            spill = op_dfad (SR | SIGN, sr1, 0);
            if (spill)
                fp_trap (spill);
            break;

        case 01320:                                     /* ANA */
            AC = AC & SR;
            break;

        case 01340:                                     /* LAS */
            t = AC & AC_MMASK;                          /* AC Q,P,1-35 */
            if (t < SR)
                PC = (PC + 2) & EAMASK;
            else if (t == SR)
                PC = (PC + 1) & EAMASK;
            break;

        case 01400:                                     /* SBM */
            op_add (SR | SIGN);
            break;
        
        case 01500:                                     /* CAL */
            AC = SR;
            break;

        case 01501:                                     /* ORA */
            AC = AC | SR;
            break;

        case 01520:                                     /* NZT */
            if ((SR & MMASK) != 0)
                PC = (PC + 1) & EAMASK;
            break;

        case 01534:                                     /* LXD */
            if (tag)                                    /* M decr -> xr */
                put_xr (tag, GET_DEC (SR));
            break;

        case 01535:                                     /* LDC */
            if (tag)                                    /* -M decr -> xr */
                put_xr (tag, NEG (GET_DEC (SR)));
            break;

        case 01564:                                     /* (CTSS) LPI */
            if (prot_trap (0))                          /* user mode? */
                break;
            ind_start = ((uint32) SR) & VA_BLK;
            ind_limit = (GET_DEC (SR) & VA_BLK) | VA_OFF;
            user_buf = 1;                               /* set mode buffer */
            chtr_inhi = 1;                              /* delay traps */
            chtr_pend = 0;                              /* no trap now */
            break;

        case 01600:                                     /* STQ */
            Write (ea, MQ);
            break;

        case 01601:                                     /* SRI (CTSS) */
            SR = ind_reloc & VA_BLK;
            /* add reloc mode in bit 1 */
            Write (ea, SR);
            break;

        case 01602:                                     /* ORS */
            SR = SR | (AC & DMASK);
            Write (ea, SR);
            break;

        case 01603:                                     /* DST */
            SR = (AC & MMASK) | ((AC & AC_S)? SIGN: 0);
            if (!Write (ea, SR))
                break;
            Write ((ea + 1) & EAMASK, MQ);
            break;

        case 01604:                                     /* SPI (CTSS) */
            SR = (((t_uint64) (ind_limit & VA_BLK)) << INST_V_DEC) |
                ((t_uint64) (ind_start & VA_BLK));
            /* add prot mode in bit 2 */
            Write (ea, SR);
            break;

        case 01620:                                     /* SLQ */
            SR = (SR & RMASK) | (MQ & LMASK);
            Write (ea, SR);
            break;

        case 01625:                                     /* STL */
            SR = (SR & ~AMASK) | PC;
            Write (ea, SR);
            break;

        case 01634:                                     /* SXD */
            SR = (SR & ~XMASK) |                        /* xr -> M decr */
                (((t_uint64) get_xrx (tag)) << INST_V_DEC);
            Write (ea, SR);
            break;

        case 01636:                                     /* SCD */
            SR = (SR & ~XMASK) |                        /* -xr -> M decr */
                (((t_uint64) (NEG (get_xrx (tag)) & AMASK)) << INST_V_DEC);
            Write (ea, SR);
            break;

        case 01700:                                     /* CAD (704) */
            cpy_trap (PC);
            break;

        case 01734:                                     /* PDX */
            if (tag)                                    /* AC decr -> xr */
                put_xr (tag, GET_DEC (AC));
            break;

        case 01737:                                     /* PDC */
            if (tag)                                    /* -AC decr -> xr */
                put_xr (tag, NEG (GET_DEC (AC)));
            break;

        case 01754:                                     /* PXD */
            AC = ((t_uint64) get_xrx (tag)) << INST_V_DEC;
            break;                                      /* xr -> AC decr */

        case 01756:                                     /* PCD */
            AC = ((t_uint64) (NEG (get_xrx (tag)) & AMASK)) << INST_V_DEC;
            break;                                      /* -xr -> AC decr */

        case 01760:                                     /* MSE */
            reason = op_mse (ea);
            break;

        case 01761:                                     /* (CTSS) ext core */
            if (prot_trap (0))                          /* user mode? */
                break;
            if (ea == 041)                              /* SEA? */
                data_base = 0;
            else if (ea == 042)                         /* SEB? */
                data_base = BCORE_BASE;
            else if (ea == 043) {                       /* IFT? */
                if (inst_base == 0)
                    PC = (PC + 1) & EAMASK;
                }
            else if (ea == 044) {                       /* EFT? */
                if (data_base == 0)
                    PC = (PC + 1) & EAMASK;
                }
            else if (stop_illop)
                reason = STOP_ILLEG;
            break;

        case 01763:                                     /* LGL */
            op_lgl (ea);
            break;

        case 01765:                                     /* LGR */
            op_lgr (ea);
            break;

        case 01773:                                     /* RQL */
            sc = (ea & SCMASK) % 36;
            if (sc)
                MQ = ((MQ << sc) | (MQ >> (36 - sc))) & DMASK;
            break;

        case 01774:                                     /* AXC */
            if (tag)                                    /* -IR addr -> xr */
                put_xr (tag, NEG (addr));
            break;

/* IO instructions */

        case 00022: case 00024: case 00026:             /* TRCx */
        case 01022: case 01024: case 01026:     
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = ((op & 077) - 00022) | ((op >> 9) & 01);
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (!BIT_TST (chtr_enab, CHTR_V_TRC + ch) &&
                (ch_flags[ch] & CHF_TRC)) {
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                ch_flags[ch] = ch_flags[ch] & ~CHF_TRC;
                chtr_pend = chtr_eval (NULL);           /* eval chan traps */
                }
            break;

        case 00027: case 01027:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = 6 + ((op >> 9) & 01);
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (!BIT_TST (chtr_enab, CHTR_V_TRC + ch) &&
                (ch_flags[ch] & CHF_TRC)) {
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                ch_flags[ch] = ch_flags[ch] & ~CHF_TRC;
                chtr_pend = chtr_eval (NULL);           /* eval chan traps */
                }
            break;

        case 00030: case 00031: case 00032: case 00033: /* TEFx */
        case 01030: case 01031: case 01032: case 01033:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = ((op & 03) << 1) | ((op >> 9) & 01);
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (!BIT_TST (chtr_enab, CHTR_V_CME + ch) &&
                (ch_flags[ch] & CHF_EOF)) {
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                ch_flags[ch] = ch_flags[ch] & ~CHF_EOF;
                chtr_pend = chtr_eval (NULL);           /* eval chan traps */
                }
            break;

        case 00060: case 00061: case 00062: case 00063: /* TCOx */
        case 00064: case 00065: case 00066: case 00067:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = op & 07;
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (ch_sta[ch] != CHXS_IDLE) {
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;      

        case 01060: case 01061: case 01062: case 01063: /* TCNx */
        case 01064: case 01065: case 01066: case 01067:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = op & 07;
            if (mode_ttrap)
                WriteTA (TRAP_STD_SAV, oldPC);
            if (ch_sta[ch] == CHXS_IDLE) {
                PCQ_ENTRY;
                if (mode_ttrap)                         /* trap? */
                    TrapXfr (TRAP_TRA_PC);
                else PC = ea;                           /* branch */
                }
            break;

        case 00540: case 00541: case 00542: case 00543: /* RCHx */
        case 01540: case 01541: case 01542: case 01543:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = ((op & 03) << 1) | ((op >> 9) & 01);
            reason = ch_op_start (ch, ea, TRUE);
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00544: case 00545: case 00546: case 00547: /* LCHx */
        case 01544: case 01545: case 01546: case 01547:
            if (prot_trap (0))                          /* user mode? */
                break;
            ch = ((op & 03) << 1) | ((op >> 9) & 01);
            reason = ch_op_start (ch, ea, FALSE);
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00640: case 00641: case 00642: case 00643: /* SCHx */
        case 01640: case 01641: case 01642: case 01643:
            ch = ((op & 03) << 1) | ((op >> 9) & 01);
            if ((reason = ch_op_store (ch, &SR)) == SCPE_OK)
                Write (ea, SR);
            break;

        case 00644: case 00645: case 00646: case 00647: /* SCDx */
        case 01644: case 01645: case 01646: case 01647:
            ch = ((op & 03) << 1) | ((op >> 9) & 01);
            if ((reason = ch_op_store_diag (ch, &SR)) == SCPE_OK)
                Write (ea, SR);
            break;

        case 00762:                                     /* RDS */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_ds (ch, CHSL_RDS, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00764:                                     /* BSR */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_BSR, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00766:                                     /* WRS */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_ds (ch, CHSL_WRS, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00770:                                     /* WEF */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_WEF, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00772:                                     /* REW */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_REW, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 01764:                                     /* BSF */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_BSF, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 01772:                                     /* RUN */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_RUN, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        case 00776:                                     /* SDN */
            if (sel_trap (0) || prot_trap (PC))         /* select takes priority */
                break;
            ch = GET_U_CH (IR);
            reason = ch_op_nds (ch, CHSL_SDN, GET_U_UNIT (ea));
            chtr_pend = chtr_eval (NULL);               /* eval chan traps */
            break;

        default:
            if (stop_illop)
                reason = STOP_ILLEG;
            break;
            }
        }                                               /* end else */

    if (reason) {                                       /* reason code? */
        if (reason == ERR_STALL) {                      /* stall? */
            PC = oldPC;                                 /* back up PC */
            reason = 0;
            }
        else if (reason == STOP_HALT) {                 /* halt? wait for IO */
            t_stat r;
            for (i = 0; (i < HALT_IO_LIMIT) && !ch_qidle (); i++) {
                sim_interval = 0;
                if ((r = sim_process_event ()))         /* process events */
                    return r;
                chtr_pend = chtr_eval (NULL);           /* eval chan traps */
                while (ch_req) {                        /* until no ch req */
                    for (j = 0; j < NUM_CHAN; j++) {    /* loop thru channels */
                        if (ch_req & REQ_CH (j)) {      /* channel request? */
                            if ((r = ch_proc (j)))
                                return r;
                            }
                        chtr_pend = chtr_eval (NULL);
                        }
                    }                                   /* end while ch_req */
                }                                       /* end for wait */
            if (chtr_pend)                              /* trap? cancel HALT */
                reason = 0;
            }                                           /* end if HALT */
        }                                               /* end if reason */
    }                                                   /* end while */

pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Get index register for indexing */

uint32 get_xri (uint32 tag)
{
tag = tag & INST_M_TAG;

if (tag) {
    if (mode_multi) {
        uint32 r = 0;
        if (tag & 1)
            r = r | XR[1];
        if (tag & 2)
            r = r | XR[2];
        if (tag & 4)
            r = r | XR[4];
        return r & EAMASK;
        }
    return XR[tag] & EAMASK;
    }
return 0;
}

/* Get index register for instruction execution

   Instructions which are 'executing directly' on index registers rewrite
   the index register value.  In multi-tag mode, this causes all registers
   involved in the OR function to receive the OR'd value. */

uint32 get_xrx (uint32 tag)
{
tag = tag & INST_M_TAG;

if (tag) {
    if (mode_multi) {
        uint32 r = 0;
        if (tag & 1)
            r = r | XR[1];
        if (tag & 2)
            r = r | XR[2];
        if (tag & 4)
            r = r | XR[4];
        put_xr (tag, r);
        return r & EAMASK;
        }
    return XR[tag] & EAMASK;
    }
return 0;
}

/* Store index register */

void put_xr (uint32 tag, uint32 dat)
{
tag = tag & INST_M_TAG;
dat = dat & EAMASK;

if (tag) {
    if (mode_multi) {
        if (tag & 1)
            XR[1] = dat;
        if (tag & 2)
            XR[2] = dat;
        if (tag & 4)
            XR[4] = dat;
        }
    else XR[tag] = dat;
    }
return;
}

/* Floating point trap */

t_bool fp_trap (uint32 spill)
{
if (mode_ftrap) {
    WriteTAD (TRAP_STD_SAV, PC, spill);
    PCQ_ENTRY;
    PC = TRAP_FP_PC;
    return TRUE;
    }
else {
    if (spill & TRAP_F_AC)
        ind_ovf = 1;
    if (spill & TRAP_F_MQ)
        ind_mqo = 1;
    }
return FALSE;
}

/* (CTSS) Protection trap */

t_bool prot_trap (uint32 decr)
{
if (mode_user) {
    WriteTAD (TRAP_PROT_SAV, PC, decr);
    PCQ_ENTRY;
    PC = TRAP_PROT_PC;
    return TRUE;
    }
return FALSE;
}

/* Store trap address and decrement, with A/B select flags; clear A/B, user mode */

void WriteTAD (uint32 pa, uint32 addr, uint32 decr)
{
t_uint64 mem;

if (inst_base)
    decr |= TRAP_F_BINST;
if (data_base)
    decr |= TRAP_F_BDATA;
mem = ReadP (pa) & ~(XMASK | AMASK);
mem |= (((t_uint64) (decr & AMASK)) << INST_V_DEC) |
    ((t_uint64) (addr & AMASK));
WriteP (pa, mem);
mode_ctrap = 0;
mode_strap = 0;
mode_storn = 0;
mode_user = user_buf = 0;
mode_reloc = reloc_buf = 0;
inst_base = 0;
data_base = 0;
return;
}

/* Copy trap */

t_bool cpy_trap (uint32 va)
{
if (mode_ctrap) {
    WriteTA (TRAP_704_SAV, va);
    PCQ_ENTRY;
    TrapXfr (TRAP_CPY_PC);
    return TRUE;
    }
return FALSE;
}

/* Select trap */

t_bool sel_trap (uint32 va)
{
if (mode_strap) {
    WriteTA (TRAP_704_SAV, va);
    PCQ_ENTRY;
    TrapXfr (TRAP_SEL_PC);
    return TRUE;
    }
return FALSE;
}

/* Store trap address - do not alter state yet (might be TRA) */

void WriteTA (uint32 pa, uint32 dat)
{
t_uint64 mem;

mem = ReadP (pa) & ~AMASK;
mem |= (dat & AMASK);
WriteP (pa, mem);
return;
}

/* Set trap PC - second half of address-only trap */

void TrapXfr (uint32 newpc)
{
PC = newpc;
mode_ctrap = 0;
mode_strap = 0;
mode_storn = 0;
mode_user = user_buf = 0;
mode_reloc = reloc_buf = 0;
inst_base = 0;
data_base = 0;
return;
}

/* Read instruction and indirect */

t_bool ReadI (uint32 va, t_uint64 *val)
{
if (mode_reloc)
    va = (va + ind_reloc) & AMASK;
if (mode_user && ((va < ind_start) || (va > ind_limit))) {
    prot_trap (0);
    return FALSE;
    }
*val = M[va | inst_base];
return TRUE;
}

/* Read */

t_bool Read (uint32 va, t_uint64 *val)
{
if (mode_reloc)
    va = (va + ind_reloc) & AMASK;
if (mode_user && ((va < ind_start) || (va > ind_limit))) {
    prot_trap (0);
    return FALSE;
    }
*val = M[va | data_base];
return TRUE;
}

/* Write */

t_bool Write (uint32 va, t_uint64 dat)
{
if (mode_reloc)
    va = (va + ind_reloc) & AMASK;
if (mode_user && ((va < ind_start) || (va > ind_limit))) {
    prot_trap (0);
    return FALSE;
    }
M[va | data_base] = dat;
return TRUE;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
ind_ovf = 0;
ind_mqo = 0;
ind_dvc = 0;
ind_ioc = 0;
ind_reloc = 0;
ind_start = 0;
ind_limit = 0;
mode_ttrap = 0;
mode_ctrap = 0;
mode_strap = 0;
mode_ftrap = 1;
mode_storn = 0;
if (cpu_model & (I_94|I_CT))
    mode_multi = 0;
else mode_multi = 1;
mode_user = user_buf = 0;
mode_reloc = reloc_buf = 0;
inst_base = 0;
data_base = 0;
ch_req = 0;
chtr_pend = chtr_enab = 0;
chtr_inht = chtr_inhi = 0;
ht_pend = 0;
SLT = 0;
XR[0] = 0;
if (M == NULL)
    M = (t_uint64 *) calloc (MAXMEMSIZE, sizeof (t_uint64));
if (M == NULL)
    return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
if (vptr == NULL)
    return SCPE_ARG;
if ((sw & (SWMASK ('A') | SWMASK ('B')))? (ea > AMASK): (ea >= MEMSIZE))
    return SCPE_NXM;
if (sw & SWMASK ('B'))
    ea = ea | BCORE_BASE;
*vptr = M[ea] & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
if ((sw & (SWMASK ('A') | SWMASK ('B')))? (ea > AMASK): (ea >= MEMSIZE))
    return SCPE_NXM;
if (sw & SWMASK ('B'))
    ea = ea | BCORE_BASE;
M[ea] = val & DMASK;
return SCPE_OK;
}

/* Set model */

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
UNIT *chuptr = mt_dev[CHRONO_CH].units + CHRONO_UNIT;
extern DEVICE clk_dev;

cpu_model = val;
if (val & I_CT) {
    uptr->capac = MAXMEMSIZE;
    detach_unit (uptr);
    chuptr->flags &= ~UNIT_ATTABLE;
    clk_dev.flags &= ~DEV_DIS;
    }
else {
    uptr->capac = STDMEMSIZE;
    chuptr->flags |= UNIT_ATTABLE;
    }
if (!(cpu_model & I_94))
    mode_multi = 1;
return SCPE_OK;
}

/* Show CTSS */

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (cpu_model & I_CT)
    fputs ("CTSS", st);
else if (cpu_model & I_94)
    fputs ("7094", st);
else fputs ("7090", st);
return SCPE_OK;
}

/* Insert history entry */

static uint32 inst_io_tab[32] = {
    0, 0, 0, 0,          0, 0, 0, 0,                        /* 0000 - 0377 */
    0, 0, 0, 0x000000FF, 0, 0, 0, 0x45540000,               /* 0400 - 0777 */
    0, 0, 0, 0,          0, 0, 0, 0,                        /* 1000 - 1400 */
    0, 0, 0, 0x000000FF, 0, 0, 0, 0                         /* 1400 - 1777 */
    };

void cpu_ent_hist (uint32 pc, uint32 ea, t_uint64 ir, t_uint64 opnd)
{
int32 prv_p;

if (pc & HIST_PC) {
    if ((pc == hst[hst_p].pc) && (ir == hst[hst_p].ir)) {   /* repeat last? */
        hst[hst_p].rpt++;
        return;
        }
    prv_p = hst_p? hst_p - 1: hst_lnt - 1;
    if ((pc == hst[prv_p].pc) && (ir == hst[prv_p].ir)) {   /* 2 line loop? */
        hst[prv_p].rpt++;
        return;
        }
    if (hst_ch & HIST_CH_I) {                               /* IO only? */
        uint32 op = GET_OPC (ir);                           /* get opcode */
        if ((ir & INST_T_DEC) ||
            !(inst_io_tab[op / 32] & (1u << (op & 037))))
            return;
        }
    }
hst_p = (hst_p + 1);                                        /* next entry */
if (hst_p >= hst_lnt)
    hst_p = 0;
hst[hst_p].pc = pc;
hst[hst_p].ir = ir;
hst[hst_p].ac = AC;
hst[hst_p].mq = MQ;
hst[hst_p].si = SI;
hst[hst_p].ea = ea;
hst[hst_p].opnd = opnd;
hst[hst_p].rpt = 0;
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
    hst_lnt = hst_ch = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
        return SCPE_MEM;
    hst_lnt = lnt;
    if (sim_switches & SWMASK ('I'))
        hst_ch = HIST_CH_I|HIST_CH_C;
    else if (sim_switches & SWMASK ('C'))
        hst_ch = HIST_CH_C;
    else hst_ch = 0;
    }
return SCPE_OK;
}

/* Print one instruction */

t_stat cpu_fprint_one_inst (FILE *st, uint32 pc, uint32 rpt, uint32 ea,
    t_uint64 ir, t_uint64 ac, t_uint64 mq, t_uint64 si, t_uint64 opnd)
{
int32 ch;
t_value sim_eval;

sim_eval = ir;
if (pc & HIST_PC) {                                     /* instruction? */
    fputs ("CPU ", st);
    fprintf (st, "%05o ", (int)(pc & AMASK));
    if (rpt == 0)
        fprintf (st, "       ");
    else if (rpt < 1000000)
        fprintf (st, "%6d ", rpt);
    else fprintf (st, "%5dM ", rpt / 1000000);
    fprint_val (st, ac, 8, 38, PV_RZRO);
    fputc (' ', st);
    fprint_val (st, mq, 8, 36, PV_RZRO);
    fputc (' ', st);
    fprint_val (st, si, 8, 36, PV_RZRO);
    fputc (' ', st);
    if (ir & INST_T_DEC)
        fprintf (st, "       ");
    else fprintf (st, "%05o  ", ea);
    if (fprint_sym (st, pc & AMASK, &sim_eval, &cpu_unit, SWMASK ('M')) > 0) {
        fputs ("(undefined) ", st);
        fprint_val (st, ir, 8, 36, PV_RZRO);
        }
    else if (!(ir & INST_T_DEC) && (op_flags[GET_OPC (ir)] & I_R)) {
        fputs (" [", st);
        fprint_val (st, opnd, 8, 36, PV_RZRO);
        fputc (']', st);
        }
    fputc ('\n', st);                                   /* end line */
    }                                                   /* end if instruction */
else if ((ch = HIST_CH (pc))) {                         /* channel? */
    fprintf (st, "CH%c ", 'A' + ch - 1);
    fprintf (st, "%05o  ", (int)(pc & AMASK));
    fputs ("                                              ", st);
    fprintf (st, "%05o  ", (int)(ea & AMASK));
    if (fprint_sym (st, pc & AMASK, &sim_eval, &cpu_unit,
        (ch_dev[ch - 1].flags & DEV_7909)? SWMASK ('N'): SWMASK ('I')) > 0) {
        fputs ("(undefined) ", st);
        fprint_val (st, ir, 8, 36, PV_RZRO);
        }
    fputc ('\n', st);                                   /* end line */
    }                                                   /* end else channel */
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 k, di, lnt;
const char *cptr = (const char *) desc;
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
fprintf (st, "    PC    repeat AC            MQ           SI           EA     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    cpu_fprint_one_inst (st, h->pc, h->rpt, h->ea, h->ir, h->ac, h->mq, h->si, h->opnd);
    }                                                   /* end for */
return SCPE_OK;
}
