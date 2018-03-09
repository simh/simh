/* i1401_cpu.c: IBM 1401 CPU simulator

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

   13-Mar-17    RMS     Fixed MTF length checking (COVERITY)
   30-Jan-15    RMS     Fixed treatment of overflow (Ken Shirriff)
   08-Oct-12    RMS     Clear storage and branch preserves B register (Van Snyder)
   19-Mar-11    RMS     Reverted multiple tape indicator implementation
   20-Jan-11    RMS     Fixed branch on EOT indicator per hardware (Van Snyder)
   07-Nov-10    RMS     Fixed divide not to clear word marks in quotient
   24-Apr-10    RMS     Revised divide algorithm (Van Snyder)
   11-Jul-08    RMS     Added missing A magtape modifier (Van Snyder)
                        Fixed tape indicator implementation (Bob Abeles)
                        Fixed bug in ZA and ZS (Bob Abeles)
   07-Jul-07    RMS     Removed restriction on load-mode binary tape
   28-Jun-07    RMS     Added support for SS overlap modifiers
   22-May-06    RMS     Fixed format error in CPU history (Peter Schorn)
   06-Mar-06    RMS     Fixed bug in divide (Van Snyder)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   01-Sep-05    RMS     Removed error stops in MCE
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-Jun-05    RMS     Fixed SSB-SSG clearing on RESET (Ralph Reinke)
   14-Nov-04    WVS     Added column binary support, debug support
   06-Nov-04    RMS     Added instruction history
   12-Jul-03    RMS     Moved ASCII/BCD tables to included file
                        Revised fetch to model hardware
                        Removed length checking in fetch phase
   16-Mar-03    RMS     Fixed mnemonic, instruction lengths, and reverse
                        scan length check bug for MCS
                        Fixed MCE bug, BS off by 1 if zero suppress
                        Fixed chaining bug, D lost if return to SCP
                        Fixed H branch, branch occurs after continue
                        Added check for invalid 8 character MCW, LCA
   03-Jun-03    RMS     Added 1311 support
   22-May-02    RMS     Added multiply and divide
   30-Dec-01    RMS     Added old PC queue
   30-Nov-01    RMS     Added extended SET/SHOW support
   10-Aug-01    RMS     Removed register in declarations
   07-Dec-00    RMS     Fixed bugs found by Charles Owen
                        -- 4,7 char NOPs are legal
                        -- 1 char B is chained BCE
                        -- MCE moves whole char after first
   14-Apr-99    RMS     Changed t_addr to unsigned

   The register state for the IBM 1401 is:

   IS           I storage address register (PC)
   AS           A storage address register (address of first operand)
   BS           B storage address register (address of second operand)
   ind[0:63]    indicators
   SSA          sense switch A
   IOCHK        I/O check
   PRCHK        process check

   The IBM 1401 is a variable instruction length, decimal data system.
   Memory consists of 4000, 8000, 12000, or 16000 BCD characters, each
   containing six bits of data and a word mark.  There are no general
   registers; all instructions are memory to memory, using explicit
   addresses or an address pointer from a prior instruction.

   BCD numeric data consists of the low four bits of a character (DIGIT),
   encoded as X, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, X, X, X, X, X.  The high
   two bits (ZONE) encode the sign of the data as +, +, -, +.  Character
   data uses all six bits of a character.  Numeric and character fields are
   delimited by a word mark.  Fields are typically processed in descending
   address order (low-order data to high-order data).

   The 1401 encodes a decimal address, and an index register number, in
   three characters:

        character               zone                    digit
        addr + 0                <1:0> of thousands      hundreds
        addr + 1                index register #        tens
        addr + 2                <3:2> of thousands      ones

   Normally the digit values 0, 11, 12, 13, 14, 15 are illegal in addresses.
   However, in indexing, digits are passed through the adder, and illegal
   values are normalized to legal counterparts.

   The 1401 has six instruction formats:

        op                      A and B addresses, if any, from AS and BS
        op d                    A and B addresses, if any, from AS and BS
        op aaa                  B address, if any, from BS
        op aaa d                B address, if any, from BS
        op aaa bbb
        op aaa bbb d

   where aaa is the A address, bbb is the B address, and d is a modifier.
   The opcode has word mark set; all other characters have word mark clear.

   This routine is the instruction decode routine for the IBM 1401.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        illegal addresses or instruction formats
        I/O error in I/O simulator

   2. Interrupts.  The 1401 has no interrupt structure.

   3. Non-existent memory.  On the 1401, references to non-existent
      memory halt the processor.

   4. Adding I/O devices.  These modules must be modified:

        i1401_cpu.c     add device dispatching code to iodisp
        i1401_sys.c     add sim_devices table entry
*/

#include "i1401_defs.h"
#include "i1401_dat.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = saved_IS

#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    uint16              is;
    uint16              ilnt;
    uint8               inst[MAX_L];
    } InstHistory;

/* These macros validate addresses.  If an addresses error is detected,
   they return an error status to the caller.  These macros should only
   be used in a routine that returns a t_stat value.
*/

#define MM(x)           x = x - 1; \
                        if (x < 0) { \
                            x = BA + MAXMEMSIZE - 1; \
                            reason = STOP_WRAP; \
                            break; \
                            }
                    
#define PP(x)           x = x + 1; \
                        if (ADDR_ERR (x)) { \
                            x = BA + (x % MAXMEMSIZE); \
                            reason = STOP_WRAP; \
                            break; \
                            }

#define BRANCH          if (ADDR_ERR (AS)) { \
                            reason = STOP_INVBR; \
                            break; \
                            } \
                        if (cpu_unit.flags & XSA) \
                            BS = IS; \
                        else BS = BA + 0; \
                        PCQ_ENTRY; \
                        IS = AS;

#define BRANCH_CS       if (ADDR_ERR (AS)) { \
                            reason = STOP_INVBR; \
                            break; \
                            } \
                        PCQ_ENTRY; \
                        IS = AS;


uint8 M[MAXMEMSIZE] = { 0 };                            /* main memory */
int32 saved_IS = 0;                                     /* saved IS */
int32 AS = 0;                                           /* AS */
int32 BS = 0;                                           /* BS */
int32 D = 0;                                            /* modifier */
int32 as_err = 0, bs_err = 0;                           /* error flags */
int32 hb_pend = 0;                                      /* halt br pending */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 ind[64] = { 0 };                                  /* indicators */
int32 ssa = 1;                                          /* sense switch A */
int32 prchk = 0;                                        /* process check stop */
int32 iochk = 0;                                        /* I/O check stop */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */
t_bool conv_old = 0;                                    /* old conversions */

extern int32 sim_emax;

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_conv (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_conv (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
int32 store_addr_h (int32 addr);
int32 store_addr_t (int32 addr);
int32 store_addr_u (int32 addr);
int32 div_add (int32 ap, int32 bp);
int32 div_sub (int32 ap, int32 bp);
void div_sign (int32 dvrc, int32 dvdc, int32 qp, int32 rp);
t_stat iomod (int32 ilnt, int32 mod, const int32 *tptr);
t_stat iodisp (int32 dev, int32 unit, int32 flag, int32 mod);

extern t_stat read_card (int32 ilnt, int32 mod);
extern t_stat punch_card (int32 ilnt, int32 mod);
extern t_stat select_stack (int32 mod);
extern t_stat carriage_control (int32 mod);
extern t_stat write_line (int32 ilnt, int32 mod);
extern t_stat inq_io (int32 flag, int32 mod);
extern t_stat mt_io (int32 unit, int32 flag, int32 mod);
extern t_stat dp_io (int32 fnc, int32 flag, int32 mod);
extern t_stat mt_func (int32 unit, int32 flag, int32 mod);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX + UNIT_BCD + STDOPT, MAXMEMSIZE)
    };

REG cpu_reg[] = {
    { DRDATA (IS, saved_IS, 14), PV_LEFT },
    { DRDATA (AS, AS, 14), PV_LEFT },
    { DRDATA (BS, BS, 14), PV_LEFT },
    { FLDATA (ASERR, as_err, 0) },
    { FLDATA (BSERR, bs_err, 0) },
    { ORDATA (D, D, 7) },
    { FLDATA (SSA, ssa, 0) },
    { FLDATA (SSB, ind[IN_SSB], 0) },
    { FLDATA (SSC, ind[IN_SSC], 0) },
    { FLDATA (SSD, ind[IN_SSD], 0) },
    { FLDATA (SSE, ind[IN_SSE], 0) },
    { FLDATA (SSF, ind[IN_SSF], 0) },
    { FLDATA (SSG, ind[IN_SSG], 0) },
    { FLDATA (EQU, ind[IN_EQU], 0) },
    { FLDATA (UNEQ, ind[IN_UNQ], 0) },
    { FLDATA (HIGH, ind[IN_HGH], 0) },
    { FLDATA (LOW, ind[IN_LOW], 0) },
    { FLDATA (OVF, ind[IN_OVF], 0) },
    { FLDATA (IOCHK, iochk, 0) },
    { FLDATA (PRCHK, prchk, 0) },
    { FLDATA (HBPEND, hb_pend, 0) },
    { BRDATA (IND, ind, 8, 32, 64), REG_HIDDEN + PV_LEFT },
    { BRDATA (ISQ, pcq, 10, 14, PCQ_SIZE), REG_RO+REG_CIRC },
    { DRDATA (ISQP, pcq_p, 6), REG_HRO },
    { ORDATA (WRU, sim_int_char, 8) },
    { FLDATA (CONVOLD, conv_old, 0), REG_HIDDEN },
    { NULL }
    };

MTAB cpu_mod[] = {
    { XSA, XSA, "XSA", "XSA", NULL },
    { XSA, 0, "no XSA", "NOXSA", NULL },
    { HLE, HLE, "HLE", "HLE", NULL },
    { HLE, 0, "no HLE", "NOHLE", NULL },
    { BBE, BBE, "BBE", "BBE", NULL },
    { BBE, 0, "no BBE", "NOBBE", NULL },
    { MA, MA, "MA", 0, NULL },
    { MA, 0, "no MA", 0, NULL },
    { MR, MR, "MR", "MR", NULL },
    { MR, 0, "no MR", "NOMR", NULL },
    { EPE, EPE, "EPE", "EPE", NULL },
    { EPE, 0, "no EPE", "NOEPE", NULL },
    { MDV, MDV, "MDV", "MDV", NULL },
    { MDV, 0, "no MDV", "NOMDV", NULL },
    { UNIT_MSIZE, 4000, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8000, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12000, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16000, NULL, "16K", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CONVERSIONS", "NEWCONVERSIONS",
      &cpu_set_conv, &cpu_show_conv },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, NULL, "OLDCONVERSIONS",
      &cpu_set_conv, NULL },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 14, 1, 8, 7,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG
    };

/* Tables */

/* Opcode table - length, dispatch, and option flags.  This table is
   used by the symbolic input routine to validate instruction lengths  */

const int32 op_table[64] = {
    0,                                                  /* 00: illegal */
    L1 | L2 | L4 | L5,                                  /* read */
    L1 | L2 | L4 | L5,                                  /* write */
    L1 | L2 | L4 | L5,                                  /* write and read */
    L1 | L2 | L4 | L5,                                  /* punch */
    L1 | L4,                                            /* read and punch */
    L1 | L2 | L4 | L5,                                  /* write and read */
    L1 | L2 | L4 | L5,                                  /* write, read, punch */
    L1,                                                 /* 10: read feed */
    L1,                                                 /* punch feed */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ | MA,                    /* modify address */
    L1 | L4 | L7 | AREQ | BREQ | MDV,                   /* multiply */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* 20: illegal */
    L1 | L4 | L7 | BREQ | NOWM,                         /* clear storage */
    L1 | L4 | L7 | AREQ | BREQ,                         /* subtract */
    0,                                                  /* illegal */
    L5 | IO,                                            /* magtape */
    L1 | L8 | BREQ,                                     /* branch wm or zone */
    L1 | L8 | BREQ | BBE,                               /* branch if bit eq */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ,                         /* 30: move zones */
    L1 | L4 | L7 | AREQ | BREQ,                         /* move supress zero */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ | NOWM,                  /* set word mark */
    L1 | L4 | L7 | AREQ | BREQ | MDV,                   /* divide */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* 40: illegal */
    0,                                                  /* illegal */
    L2 | L5,                                            /* select stacker */
    L1 | L4 | L7 | L8 | BREQ | MLS | IO,                /* load */
    L1 | L4 | L7 | L8 | BREQ | MLS | IO,                /* move */
    HNOP | L1 | L2 | L4 | L5 | L7 | L8,                 /* nop */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ | MR,                    /* move to record */
    L1 | L4 | AREQ | MLS,                               /* 50: store A addr */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ,                         /* zero and subtract */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0,                                                  /* 60: illegal */
    L1 | L4 | L7 | AREQ | BREQ,                         /* add */
    L1 | L4 | L5 | L8,                                  /* branch */
    L1 | L4 | L7 | AREQ | BREQ,                         /* compare */
    L1 | L4 | L7 | AREQ | BREQ,                         /* move numeric */
    L1 | L4 | L7 | AREQ | BREQ,                         /* move char edit */
    L2 | L5,                                            /* carriage control */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | MLS,                          /* 70: store B addr */
    0,                                                  /* illegal */
    L1 | L4 | L7 | AREQ | BREQ,                         /* zero and add */
    HNOP | L1 | L2 | L4 | L5 | L7 | L8,                 /* halt */
    L1 | L4 | L7 | AREQ | BREQ,                         /* clear word mark */
    0,                                                  /* illegal */
    0,                                                  /* illegal */
    0                                                   /* illegal */
    };

const int32 len_table[9] = { 0, L1, L2, 0, L4, L5, 0, L7, L8 };

/* Address character conversion tables.  Illegal characters are marked by
   the flag BA but also contain the post-adder value for indexing  */

const int32 hun_table[64] = {
    BA+000, 100, 200, 300, 400, 500, 600, 700,
    800, 900, 000, BA+300, BA+400, BA+500, BA+600, BA+700,
    BA+1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700,
    1800, 1900, 1000, BA+1300, BA+1400, BA+1500, BA+1600, BA+1700,
    BA+2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700,
    2800, 2900, 2000, BA+2300, BA+2400, BA+2500, BA+2600, BA+2700,
    BA+3000, 3100, 3200, 3300, 3400, 3500, 3600, 3700,
    3800, 3900, 3000, BA+3300, BA+3400, BA+3500, BA+3600, BA+3700
    };

const int32 ten_table[64] = {
    BA+00, 10, 20, 30, 40, 50, 60, 70,
    80, 90, 00, BA+30, BA+40, BA+50, BA+60, BA+70,
    X1+00, X1+10, X1+20, X1+30, X1+40, X1+50, X1+60, X1+70,
    X1+80, X1+90, X1+00, X1+30, X1+40, X1+50, X1+60, X1+70,
    X2+00, X2+10, X2+20, X2+30, X2+40, X2+50, X2+60, X2+70,
    X2+80, X2+90, X2+00, X2+30, X2+40, X2+50, X2+60, X2+70,
    X3+00, X3+10, X3+20, X3+30, X3+40, X3+50, X3+60, X3+70,
    X3+80, X3+90, X3+00, X3+30, X3+40, X3+50, X3+60, X3+70
    };

const int32 one_table[64] = {
    BA+0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 0, BA+3, BA+4, BA+5, BA+6, BA+7,
    BA+4000, 4001, 4002, 4003, 4004, 4005, 4006, 4007,
    4008, 4009, 4000, BA+4003, BA+4004, BA+4005, BA+4006, BA+4007,
    BA+8000, 8001, 8002, 8003, 8004, 8005, 8006, 8007,
    8008, 8009, 8000, BA+8003, BA+8004, BA+8005, BA+8006, BA+8007,
    BA+12000, 12001, 12002, 12003, 12004, 12005, 12006, 12007,
    12008, 12009, 12000, BA+12003, BA+12004, BA+12005, BA+12006, BA+12007
    };

const int32 bin_to_bcd[16] = {
    10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

const int32 bcd_to_bin[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 3, 4, 5, 6, 7
    };

/* Indicator resets - a 1 marks an indicator that resets when tested */

static const int32 ind_table[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 00 - 07 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 10 - 17 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 20 - 27 */
    0, 1, 1, 0, 1, 0, 0, 0,                             /* 30 - 37 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 40 - 47 */
    0, 0, 1, 0, 1, 0, 0, 0,                             /* 50 - 57 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 60 - 67 */
    0, 0, 1, 0, 0, 0, 0, 0                              /* 70 - 77 */
    };

/* Character collation table for compare with HLE option */

static const int32 col_table[64] = {
    000, 067, 070, 071, 072, 073, 074, 075,
    076, 077, 066, 024, 025, 026, 027, 030,
    023, 015, 056, 057, 060, 061, 062, 063,
    064, 065, 055, 016, 017, 020, 021, 022,
    014, 044, 045, 046, 047, 050, 051, 052,
    053, 054, 043, 007, 010, 011, 012, 013,
    006, 032, 033, 034, 035, 036, 037, 040,
    041, 042, 031, 001, 002, 003, 004, 005
    };

/* Summing table for two decimal digits, converted back to BCD
   Also used for multiplying two decimal digits, converted back to BCD,
   with carry forward
*/
    
static const int32 sum_table[100] = {
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE,
    BCD_ZERO, BCD_ONE, BCD_TWO, BCD_THREE, BCD_FOUR,
    BCD_FIVE, BCD_SIX, BCD_SEVEN, BCD_EIGHT, BCD_NINE
    };

static const int32 cry_table[100] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9
    };

/* Legal modifier tables */

static const int32 r_mod[] = {
    BCD_C, -1
    };
static const int32 p_mod[] = {
    BCD_C, -1
    };
static const int32 w_mod[] = {
    BCD_S, BCD_SQUARE, -1
    };
static const int32 ss_mod[] = {
    BCD_ONE, BCD_TWO, BCD_FOUR, BCD_EIGHT,
    BCD_DOLLAR, BCD_DECIMAL, BCD_SQUARE, -1
    };
static const int32 mtf_mod[] = {
    BCD_A, BCD_B, BCD_E,
    BCD_M, BCD_R, BCD_U, -1
    };

t_stat sim_instr (void)
{
int32 IS, ilnt, flags;
int32 op, xa, t, wm, ioind, dev, unit;
int32 a, b, i, k, asave, bsave;
int32 carry, lowprd, sign, ps;
int32 quo, qs;
int32 qzero, qawm, qbody, qsign, qdollar, qaster, qdecimal;
t_stat reason, r1, r2;

/* Restore saved state */

IS = saved_IS;
if (as_err)                                             /* flag bad addresses */
    AS = AS | BA;
if (bs_err)
    BS = BS | BA;
as_err = bs_err = 0;                                    /* reset error flags */
reason = 0;

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */

    if (hb_pend) {                                      /* halt br pending? */
        hb_pend = 0;                                    /* clear flag */
        BRANCH;                                         /* execute branch */
        }

    saved_IS = IS;                                      /* commit prev instr */
    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        }

    if (sim_brk_summ && sim_brk_test (IS, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    sim_interval = sim_interval - 1;

/* Instruction fetch - 1401 fetch works as follows:

   - Each character fetched enters the B register.  This register is not
     visible; the variable t represents the B register.
   - Except for the first and last cycles, each character fetched enters
     the A register.  This register is not visible; the variable D represents
     the A register, because this is the instruction modifier for 2, 5, and 8
     character instructions.
   - At the start of the second cycle (first address character), the A-address
     register and, for most instructions, the B-address register, are cleared
     to blanks.  The simulator represents addresses in binary and creates the
     effect of blanks (address is bad) if less than three A-address characters
     are found.  Further, the simulator accumulates only the A-address, and
     replicates it to the B-address at the appropriate point.
   - At the start of the fifth cycle (fourth address character), the B-address
     register is cleared to blanks.  Again, the simulator creates the effect of
     blanks (address is bad) if less than three B-address characters are found.

   The 1401 does not explicitly check for valid instruction lengths.  Most 2,
   3, 5, 6 character instructions will be invalid because the A-address or
   B-address (or both) are invalid.
*/

    if ((M[IS] & WM) == 0) {                            /* I-Op: WM under op? */
        reason = STOP_NOWM;                             /* no, error */
        break;
        }
    op = M[IS] & CHAR;                                  /* get opcode */
    flags = op_table[op];                               /* get op flags */
    if ((flags == 0) || (flags & ALLOPT & ~cpu_unit.flags)) {
        reason = STOP_NXI;                              /* illegal inst? */
        break;
        }
    if (op == OP_SAR)                                   /* SAR? save ASTAR */
        BS = AS;
    PP (IS);

    if ((t = M[IS]) & WM)                               /* I-1: WM? 1 char inst */
        goto CHECK_LENGTH;
    D = ioind = t;                                      /* could be D char, % */
    AS = hun_table[t];                                  /* could be A addr */
    PP (IS);                                            /* if %xy, BA is set */

    if ((t = M[IS]) & WM) {                             /* I-2: WM? 2 char inst */
        AS = AS | BA;                                   /* ASTAR bad */
        if (!(flags & MLS))
            BS = AS;
        goto CHECK_LENGTH;
        }
    D = dev = t;                                        /* could be D char, dev */
    AS = AS + ten_table[t];                             /* build A addr */
    PP (IS);

    if ((t = M[IS]) & WM) {                             /* I-3: WM? 3 char inst */
        AS = AS | BA;                                   /* ASTAR bad */
        if (!(flags & MLS))
            BS = AS;
        goto CHECK_LENGTH;
        }
    D = unit = t;                                       /* could be D char, unit */
    if (unit == BCD_ZERO)                               /* convert unit to binary */
        unit = 0;
    AS = AS + one_table[t];                             /* finish A addr */
    xa = (AS >> V_INDEX) & M_INDEX;                     /* get index reg */
    if (xa && (ioind != BCD_PERCNT) && (cpu_unit.flags & XSA)) { /* indexed? */
        AS = AS + hun_table[M[xa] & CHAR] + ten_table[M[xa + 1] & CHAR] +
            one_table[M[xa + 2] & CHAR];
        AS = (AS & INDEXMASK) % MAXMEMSIZE;
        }
    if (!(flags & MLS))                                 /* not MLS? B = A */
        BS = AS;
    PP (IS);

    if ((t = M[IS]) & WM)                               /* I-4: WM? 4 char inst */
        goto CHECK_LENGTH;
    if ((op == OP_B) && (t == BCD_BLANK))               /* BR + space? */
         goto CHECK_LENGTH;
    D = t;                                              /* could be D char */
    BS = hun_table[t];                                  /* could be B addr */
    PP (IS);

    if ((t = M[IS]) & WM) {                             /* I-5: WM? 5 char inst */
        BS = BS | BA;                                   /* BSTAR bad */
        goto CHECK_LENGTH;
        }
    D = t;                                              /* could be D char */
    BS = BS + ten_table[t];                             /* build B addr */
    PP (IS);

    if ((t = M[IS]) & WM) {                             /* I-6: WM? 6 char inst */
        BS = BS | BA;                                   /* BSTAR bad */
        goto CHECK_LENGTH;
        }
    D = t;                                              /* could be D char */
    BS = BS + one_table[t];                             /* finish B addr */
    xa = (BS >> V_INDEX) & M_INDEX;                     /* get index reg */
    if (xa && (cpu_unit.flags & XSA)) {                 /* indexed? */
        BS = BS + hun_table[M[xa] & CHAR] + ten_table[M[xa + 1] & CHAR] +
            one_table[M[xa + 2] & CHAR];
        BS = (BS & INDEXMASK) % MAXMEMSIZE;
        }
    PP (IS);

    if (flags & NOWM)                                   /* I-7: SWM? done */
        goto CHECK_LENGTH;
    if ((t = M[IS]) & WM)                               /* WM? 7 char inst */
        goto CHECK_LENGTH;
    D = t;                                              /* last char is D */
    while (((t = M[IS]) & WM) == 0) {                   /* I-8: repeats until WM */
        D = t;                                          /* last char is D */
        PP (IS);
        }
    if (reason)                                         /* addr err on last? */
        break;

CHECK_LENGTH:
    if ((flags & BREQ) && ADDR_ERR (BS)) {              /* valid B? */
        reason = STOP_INVB;
        break;
        }
    if ((flags & AREQ) && ADDR_ERR (AS)) {              /* valid A? */
        reason = STOP_INVA;
        break;
        }
    ilnt = IS - saved_IS;                               /* get lnt */
    if (hst_lnt) {                                      /* history enabled? */
        hst_p = (hst_p + 1);                            /* next entry */
        if (hst_p >= hst_lnt)
            hst_p = 0;
        hst[hst_p].is = saved_IS;                       /* save IS */
        hst[hst_p].ilnt = ilnt;
        for (i = 0; (i < MAX_L) && (i < ilnt); i++)
            hst[hst_p].inst[i] = M[saved_IS + i];
        }
    if (DEBUG_PRS (cpu_dev)) {
        fprint_val (sim_deb, saved_IS, 10, 5, PV_RSPC);
        fprintf (sim_deb, ": " );
        for (i = 0; i < sim_emax; i++)
            sim_eval[i] = 0;
        for (i = 0, k = saved_IS; i < sim_emax; i++, k++) {
            if (cpu_ex (&sim_eval[i], k, &cpu_unit, 0) != SCPE_OK)
                break;
            }
        fprint_sym (sim_deb, saved_IS, sim_eval, &cpu_unit, SWMASK('M'));
        fprintf (sim_deb, "\n" );
        }
    switch (op) {                                       /* case on opcode */    

/* Move/load character instructions                     A check B check

   MCW          copy A to B, preserving B WM,           here    fetch
                until either A or B WM
   LCA          copy A to B, overwriting B WM,          here    fetch
                until A WM

   Instruction lengths:

   1            chained A and B
   2,3          invalid A-address
   4            chained B address
   5,6          invalid B-address - checked in fetch
   7            normal
   8+           normal + modifier
*/

    case OP_MCW:                                        /* move char */
        if ((ilnt >= 4) && (ioind == BCD_PERCNT)) {     /* I/O form? */
            reason = iodisp (dev, unit, MD_NORM, D);    /* dispatch I/O */
            break;
            }
        if (ADDR_ERR (AS)) {                            /* check A addr */
            reason = STOP_INVA;
            break;
            }
        do {
            wm = M[AS] | M[BS];
            M[BS] = (M[BS] & WM) | (M[AS] & CHAR);      /* move char */
            MM (AS);                                    /* decr pointers */
            MM (BS);
            } while ((wm & WM) == 0);                   /* stop on A,B WM */
        break;

    case OP_LCA:                                        /* load char */
        if ((ilnt >= 4) && (ioind == BCD_PERCNT)) {     /* I/O form? */
            reason = iodisp (dev, unit, MD_WM, D);
            break;
            }
        if (ADDR_ERR (AS)) {                            /* check A addr */
            reason = STOP_INVA;
            break;
            }
        do {
            wm = M[BS] = M[AS];                         /* move char + wmark */
            MM (AS);                                    /* decr pointers */
            MM (BS);
            } while ((wm & WM) == 0);                   /* stop on A WM */
        break;

/* Other move instructions                              A check B check

   MCM          copy A to B, preserving B WM,           fetch   fetch
                until record or group mark
   MCS          copy A to B, clearing B WM, until A WM; fetch   fetch
                reverse scan and suppress leading zeroes
   MN           copy A char digit to B char digit,      fetch   fetch
                preserving B zone and WM
   MZ           copy A char zone to B char zone,        fetch   fetch
                preserving B digit and WM

   Instruction lengths:

   1            chained
   2,3          invalid A-address - checked in fetch
   4            self (B-address = A-address)
   5,6          invalid B-address - checked in fetch
   7            normal
   8+           normal + ignored modifier
*/

    case OP_MCM:                                        /* move to rec/group */
        do {
            t = M[AS];
            M[BS] = (M[BS] & WM) | (M[AS] & CHAR);      /* move char */
            PP (AS);                                    /* incr pointers */
            PP (BS);
            } while (((t & CHAR) != BCD_RECMRK) && (t != (BCD_GRPMRK + WM)));
        break;

    case OP_MCS:                                        /* move suppress zero */
        bsave = BS;                                     /* save B start */
        qzero = 1;                                      /* set suppress */
        do {
            wm = M[AS];
            M[BS] = M[AS] & ((BS != bsave)? CHAR: DIGIT);/* copy char */
            MM (AS);                                    /* decr pointers */
            MM (BS);
            } while ((wm & WM) == 0);                   /* stop on A WM */
        if (reason)                                     /* addr err? stop */
            break;
        do {
            PP (BS);                                    /* adv B */
            t = M[BS];                                  /* get B, cant be WM */
            if ((t == BCD_ZERO) || (t == BCD_COMMA)) {
                if (qzero)
                    M[BS] = 0;
                }
            else if ((t == BCD_BLANK) || (t == BCD_MINUS)) ;
            else if (((t == BCD_DECIMAL) && (cpu_unit.flags & EPE)) ||
                (t <= BCD_NINE))
                qzero = 0;
            else qzero = 1;
            } while (BS < bsave);
        PP (BS);                                        /* BS end is B+1 */
        break;  

    case OP_MN:                                         /* move numeric */
        M[BS] = (M[BS] & ~DIGIT) | (M[AS] & DIGIT);     /* move digit */
        MM (AS);                                        /* decr pointers */
        MM (BS);
        break;

    case OP_MZ:                                         /* move zone */
        M[BS] = (M[BS] & ~ZONE) | (M[AS] & ZONE);       /* move high bits */
        MM (AS);                                        /* decr pointers */
        MM (BS);
        break;

/* Branch instruction                                   A check     B check

   Instruction lengths:

   1            branch if B char equals d, chained      if branch   here
   2,3          invalid B-address                       if branch   here
   4            unconditional branch                    if branch
   5            branch if indicator[d] is set           if branch
   6            invalid B-address                       if branch   here
   7            branch if B char equals d,              if branch   here
                d is last character of B-address
   8            branch if B char equals d               if branch   here
*/

    case OP_B:                                          /* branch */
        if (ilnt == 4) {                                /* uncond branch? */
            BRANCH;
            }
        else if (ilnt == 5) {                           /* branch on ind? */
            if (ind[D]) {                               /* test indicator */
                BRANCH;
                }
            if (ind_table[D])                           /* reset if needed */
                ind[D] = 0;
            }
        else {                                          /* branch char eq */
            if (ADDR_ERR (BS)) {                        /* validate B addr */
                reason = STOP_INVB;
                break;
                }
            if ((M[BS] & CHAR) == D) {                  /* char equal? */
                BRANCH;
                }
            else {
                MM (BS);
                }
            }
        break;

/* Other branch instructions                            A check     B check

   BWZ          branch if (d<0>: B char WM)             if branch   fetch
                (d<1>: B char zone = d zone)
   BBE          branch if B char & d non-zero           if branch   fetch

   Instruction lengths:
   1    chained
   2,3  invalid A-address and B-address
   4    self (B-address = A-address, d = last character of A-address)
   5,6  invalid B-address
   7    normal, d = last character of B-address
   8+   normal
*/

    case OP_BWZ:                                        /* branch wm or zone */
        if (((D & 1) && (M[BS] & WM)) ||                /* d1? test wm */
            ((D & 2) && ((M[BS] & ZONE) == (D & ZONE)))) { /* d2? test zone */
            BRANCH;
            }
        else {                                          /* decr pointer */
            MM (BS);
            }
        break;

    case OP_BBE:                                        /* branch if bit eq */
        if (M[BS] & D & CHAR) {                         /* any bits set? */
            BRANCH;
            }
        else {                                          /* decr pointer */
            MM (BS);
            }
        break;

/* Arithmetic instructions                              A check     B check

   ZA           move A to B, normalizing A sign,        fetch       fetch
                preserving B WM, until B WM
   ZS           move A to B, complementing A sign,      fetch       fetch
                preserving B WM, until B WM
   A            add A to B                              fetch       fetch
   S            subtract A from B                       fetch       fetch
   C            compare A to B                          fetch       fetch

   Instruction lengths:

   1            chained
   2,3          invalid A-address
   4            self (B-address = A-address)
   5,6          invalid B-address
   7            normal
   8+           normal + ignored modifier

   Despite their names, ZA and ZS are not arithmetic instructions, but copies
   with zone stripping.  The adder is not used, so BCD conversions do not occur.
*/

    case OP_ZA: case OP_ZS:                             /* zero and add/sub */
        a = i = 0;                                      /* clear flags */
        do {
            if (a & WM)                                 /* A word mark? */
                wm = M[BS] = (M[BS] & WM) | BCD_ZERO;
            else {
                a = M[AS];                              /* get A char */
                t = a & DIGIT;                          /* zap zone bits */
                wm = M[BS] = (M[BS] & WM) | t;          /* store digit */
                MM (AS);
                }
            if (i == 0)
                i = M[BS] = M[BS] |
                ((((a & ZONE) == BBIT) ^ (op == OP_ZS))? BBIT: ZONE);
            MM (BS);
            } while ((wm & WM) == 0);                   /* stop on B WM */
        break;

    case OP_A: case OP_S:                               /* add/sub */
        bsave = BS;                                     /* save sign pos */
        a = M[AS];                                      /* get A digit/sign */
        b = M[BS];                                      /* get B digit/sign */
        MM (AS);
        qsign = ((a & ZONE) == BBIT) ^ ((b & ZONE) == BBIT) ^ (op == OP_S);
        t = bcd_to_bin[a & DIGIT];                      /* get A binary */
        t = bcd_to_bin[b & DIGIT] + (qsign? 10 - t: t); /* sum A + B */
        carry = (t >= 10);                              /* get carry */
        b = (b & ~DIGIT) | sum_table[t];                /* get result */
        if (qsign && ((b & BBIT) == 0))                 /* normalize sign */
            b = b | ZONE;
        M[BS] = b;                                      /* store result */
        MM (BS);
        if (b & WM) {                                   /* b wm? done */
            if ((qsign != 0) && (carry == 0))           /* eff sub and no carry? */
                M[bsave] = WM + ((b & ZONE) ^ ABIT) + sum_table[10 - t];
            if ((qsign == 0) && (carry != 0))           /* eff add and carry */
                 ind[IN_OVF] = 1;                       /* overflow */
             break;
            }
        do {
            if (a & WM)                                 /* A WM? char = 0 */
                a = WM;
            else {
                a = M[AS];                              /* else get A */
                MM (AS);
                }
            b = M[BS];                                  /* get B */
            t = bcd_to_bin[a & DIGIT];                  /* get A binary */
            t = bcd_to_bin[b & DIGIT] + (qsign? 9 - t: t) + carry;
            carry = (t >= 10);                          /* get carry */
            if ((b & WM) && (qsign == 0)) {             /* last, no recomp? */
                M[BS] = WM + sum_table[t] +             /* zone add */
                    (((a & ZONE) + b + (carry? ABIT: 0)) & ZONE);
                if (carry != 0)                         /* carry out? */
                    ind[IN_OVF] = 1;                    /* ovflo if carry */
                }
            else M[BS] = (b & WM) + sum_table[t];       /* normal add */
            MM (BS);
            } while ((b & WM) == 0);                    /* stop on B WM */
        if (reason)                                     /* address err? */
            break;
        if (qsign && (carry == 0)) {                    /* recompl, no carry? */
            M[bsave] = M[bsave] ^ ABIT;                 /* XOR sign */
            for (carry = 1; bsave != BS; --bsave) {     /* rescan */
                t = 9 - bcd_to_bin[M[bsave] & DIGIT] + carry;
                carry = (t >= 10);
                M[bsave] = (M[bsave] & ~DIGIT) | sum_table[t];
                }
            }
        break;

    case OP_C:                                          /* compare */
        if (ilnt != 1) {                                /* if not chained */
            ind[IN_EQU] = 1;                            /* clear indicators */
            ind[IN_UNQ] = ind[IN_HGH] = ind[IN_LOW] = 0;
            }
        do {
            a = M[AS];                                  /* get characters */
            b = M[BS];
            wm = a | b;                                 /* get word marks */
            if ((a & CHAR) != (b & CHAR)) {             /* unequal? */
                ind[IN_EQU] = 0;                        /* set indicators */
                ind[IN_UNQ] = 1;
                ind[IN_HGH] = col_table[b & CHAR] > col_table [a & CHAR];
                ind[IN_LOW] = ind[IN_HGH] ^ 1;
                }
            MM (AS);                                    /* decr pointers */
            MM (BS);
            } while ((wm & WM) == 0);                   /* stop on A, B WM */
        if ((a & WM) && !(b & WM)) {                    /* short A field? */
            ind[IN_EQU] = ind[IN_LOW] = 0;
            ind[IN_UNQ] = ind[IN_HGH] = 1;
            }
        if (!(cpu_unit.flags & HLE))                    /* no HLE? */
            ind[IN_EQU] = ind[IN_LOW] = ind[IN_HGH] = 0;
        break;

/* I/O instructions                                     A check     B check

   R            read a card                             if branch
   W            write to line printer                   if branch
   WR           write and read                          if branch
   P            punch a card                            if branch
   RP           read and punch                          if branch
   WP           write and punch                         if branch
   WRP          write read and punch                    if branch
   RF           read feed (nop)
   PF           punch feed (nop)
   SS           select stacker                          if branch
   CC           carriage control                        if branch

   Instruction lengths:

   1            normal
   2,3          normal, with modifier
   4            branch; modifier, if any, is last character of branch address
   5            branch + modifier
   6+           normal, with modifier
*/

    case OP_R:                                          /* read */
        if ((reason = iomod (ilnt, D, r_mod)))          /* valid modifier? */
            break;
        reason = read_card (ilnt, D);                   /* read card */
        BS = CDR_BUF + CDR_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        break;

    case OP_W:                                          /* write */
        if ((reason = iomod (ilnt, D, w_mod)))          /* valid modifier? */
            break;
        reason = write_line (ilnt, D);                  /* print line */
        BS = LPT_BUF + LPT_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        break;

    case OP_P:                                          /* punch */
        if ((reason = iomod (ilnt, D, p_mod)))          /* valid modifier? */
            break;
        reason = punch_card (ilnt, D);                  /* punch card */
        BS = CDP_BUF + CDP_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        break;

    case OP_WR:                                         /* write and read */
        if ((reason = iomod (ilnt, D, w_mod)))          /* valid modifier? */
            break;
        reason = write_line (ilnt, D);                  /* print line */
        r1 = read_card (ilnt, D);                       /* read card */
        BS = CDR_BUF + CDR_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        if (reason == SCPE_OK)                          /* merge errors */
            reason = r1;
        break;

    case OP_WP:                                         /* write and punch */
        if ((reason = iomod (ilnt, D, w_mod)))          /* valid modifier? */
            break;
        reason = write_line (ilnt, D);                  /* print line */
        r1 = punch_card (ilnt, D);                      /* punch card */
        BS = CDP_BUF + CDP_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        if (reason == SCPE_OK)                          /* merge errors */
            reason = r1;
        break;

    case OP_RP:                                         /* read and punch */
        if ((reason = iomod (ilnt, D, NULL)))           /* valid modifier? */
            break;
        reason = read_card (ilnt, D);                   /* read card */
        r1 = punch_card (ilnt, D);                      /* punch card */
        BS = CDP_BUF + CDP_WIDTH;  
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        if (reason == SCPE_OK)                          /* merge errors */
            reason = r1;
        break;

    case OP_WRP:                                        /* write, read, punch */
        if ((reason = iomod (ilnt, D, w_mod)))          /* valid modifier? */
            break;
        reason = write_line (ilnt, D);                  /* print line */
        r1 = read_card (ilnt, D);                       /* read card */
        r2 = punch_card (ilnt, D);                      /* punch card */
        BS = CDP_BUF + CDP_WIDTH;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        if (reason == SCPE_OK)                          /* merge errors */
            reason = (r1 == SCPE_OK)? r2: r1;
        break;

    case OP_SS:                                         /* select stacker */
        if ((reason = iomod (ilnt, D, ss_mod)))         /* valid modifier? */
            break;
        if ((reason = select_stack (D)))                /* sel stack, error? */
            break;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        break;

    case OP_CC:                                         /* carriage control */
        if ((reason = carriage_control (D)))            /* car ctrl, error? */
            break;
        if ((ilnt == 4) || (ilnt == 5)) {               /* check for branch */
            BRANCH;
            }
        break;

/* MTF - magtape functions - must be at least 4 characters

   Instruction lengths:

   1-3          invalid I/O address - checked here
   4            normal, d-character is unit
   5            normal, d-character is last character
   6+           normal, d-character is last character
*/

    case OP_MTF:                                        /* magtape function */
        if (ilnt < 4) {                                 /* too short? */
            reason = STOP_INVL;
            break;
            }
      if (ioind != BCD_PERCNT) {                        /* valid dev addr? */
            reason = STOP_INVA;
            break;
            }
        if ((reason = iomod (ilnt, D, mtf_mod)))        /* valid modifier? */
            break;

        if (dev == IO_MT)                               /* BCD? */
            reason = mt_func (unit, 0, D);
        else if (dev == IO_MTB)                         /* binary? */
            reason = mt_func (unit, MD_BIN, D);
        else reason = STOP_INVA;                        /* wrong device */
        break;                                          /* can't branch */

    case OP_RF: case OP_PF:                             /* read, punch feed */
        break;                                          /* nop's */

/* Move character and edit

   Control flags
        qsign           sign of A field (0 = +, 1 = minus)
        qawm            A field WM seen and processed
        qzero           zero suppression enabled
        qbody           in body (copying A field characters)
        qdollar         EPE only; $ seen in body
        qaster          EPE only; * seen in body
        qdecimal        EPE only; . seen on first rescan

   MCE operates in one to three scans, the first of which has three phases

        1       right to left   qbody = 0, qawm = 0 => right status
                                qbody = 1, qawm = 0 => body
                                qbody = 0, qawm = 1 => left status
        2       left to right
        3       right to left, extended print end only

   The first A field character is masked to its digit part, all others
   are copied intact            

   Instruction lengths:

   1            chained
   2,3          invalid A-address - checked in fetch
   4            self (B-address = A-address)
   5,6          invalid B-address - checked in fetch
   7            normal
   8+           normal + ignored modifier
*/

    case OP_MCE:                                        /* edit */
        a = M[AS];                                      /* get A char */
        b = M[BS];                                      /* get B char */
        t = a & DIGIT;                                  /* get A digit */
        MM (AS);
        qsign = ((a & ZONE) == BBIT);                   /* get A field sign */
        qawm = qzero = qbody = 0;                       /* clear other flags */
        qdollar = qaster = qdecimal = 0;                /* clear EPE flags */

/* Edit pass 1 - from right to left, under B field control

        *       in status or !epe, skip B; else, set qaster, repl with A
        $       in status or !epe, skip B; else, set qdollar, repl with A
        0       in right status or body, if !qzero, set A WM; set qzero, repl with A
                else, if !qzero, skip B; else, if (!B WM) set B WM 
        blank   in right status or body, repl with A; else, skip B
        C,R,-   in status, blank B; else, skip B
        ,       in status, blank B, else, skip B
        &       blank B
*/

    do {
        b = M[BS];                                      /* get B char */
        M[BS] = M[BS] & ~WM;                            /* clr WM */
        switch (b & CHAR) {                             /* case on B char */

        case BCD_ASTER:                                 /* * */
            if (!qbody || qdollar || !(cpu_unit.flags & EPE))
                break;
            qaster = 1;                                 /* flag */
            goto A_CYCLE;                               /* take A cycle */

        case BCD_DOLLAR:                                /* $ */
            if (!qbody || qaster || !(cpu_unit.flags & EPE))
                break;
            qdollar = 1;                                /* flag */
            goto A_CYCLE;                               /* take A cycle */

        case BCD_ZERO:                                  /* 0 */
            if (qawm) {                                 /* left status? */
                if (!qzero)                             /* first? set WM */
                    M[BS] = M[BS] | WM;
                qzero = 1;                              /* flag suppress */
                break;
                }
            if (!qzero)                                 /* body, first? WM */
                t = t | WM;
            qzero = 1;                                  /* flag suppress */
            goto A_CYCLE;                               /* take A cycle */

        case BCD_BLANK:                                 /* blank */
            if (qawm)                                   /* left status? */
                break;
        A_CYCLE:
            M[BS] = t;                                  /* copy char */
            if (a & WM) {                               /* end of A field? */
                qbody = 0;                              /* end body */
                qawm = 1;                               /* start left status */
                }
            else {
                qbody = 1;                              /* in body */
                a = M[AS];                              /* next A */
                MM (AS);
                t = a & CHAR;                           /* use A char */
                }
            break;

        case BCD_C: case BCD_R: case BCD_MINUS:         /* C, R, - */
            if (!qsign && !qbody)                       /* + & status? blank */
                M[BS] = BCD_BLANK;
            break;

        case BCD_COMMA:                                 /* , */
            if (!qbody)                                 /* status? blank */
                M[BS] = BCD_BLANK;
            break;

        case BCD_AMPER:                                 /* & */
            M[BS] = BCD_BLANK;                          /* blank */
            break;
            }                                           /* end switch */

        MM (BS);                                        /* decr B pointer */
        } while ((b & WM) == 0);                        /* stop on B WM */

    if (reason)                                         /* address err? */
        break;
    if (!qzero)                                         /* rescan? */
        break;

/* Edit pass 2 - from left to right, suppressing zeroes */

    do {
        b = M[++BS];                                    /* get B char */
        switch (b & CHAR) {                             /* case on B char */

        case BCD_ONE: case BCD_TWO: case BCD_THREE:
        case BCD_FOUR: case BCD_FIVE: case BCD_SIX:
        case BCD_SEVEN: case BCD_EIGHT: case BCD_NINE:
            qzero = 0;                                  /* turn off supr */
            break;

        case BCD_ZERO: case BCD_COMMA:                  /* 0 or , */
            if (qzero && !qdecimal)                     /* if supr, blank */
                M[BS] = qaster? BCD_ASTER: BCD_BLANK;
            break;

        case BCD_BLANK:                                 /* blank */
            if (qaster)                                 /* if EPE *, repl */
                M[BS] = BCD_ASTER;
            break;

        case BCD_DECIMAL:                               /* . */
            if (qzero && (cpu_unit.flags & EPE))        /* flag for EPE */
                qdecimal = 1;
            break;

        case BCD_PERCNT: case BCD_WM: case BCD_BS:
        case BCD_TS: case BCD_MINUS:
            break;                                      /* ignore */

        default:                                        /* other */
            qzero = 1;                                  /* restart supr */
            break;
            }                                           /* end case */
        } while ((b & WM) == 0);

    M[BS] = M[BS] & ~WM;                                /* clear B WM */
    if (!qdollar && !(qdecimal && qzero)) {             /* rescan again? */
        BS++;                                           /* BS = addr WM + 1 */
        break;
        }
    if (qdecimal && qzero)                              /* no digits? clr $ */
        qdollar = 0;

/* Edit pass 3 (extended print only) - from right to left */

    for (;; ) {                                         /* until chars */
        b = M[BS];                                      /* get B char */
        if ((b == BCD_BLANK) && qdollar) {              /* blank & flt $? */
            M[BS] = BCD_DOLLAR;                         /* insert $ */
            break;                                      /* exit for */
            }
        if (b == BCD_DECIMAL) {                         /* decimal? */
            M[BS] = qaster? BCD_ASTER: BCD_BLANK;
            break;                                      /* exit for */
            }
        if ((b == BCD_ZERO) && !qdollar)                /* 0 & ~flt $ */
            M[BS] = qaster? BCD_ASTER: BCD_BLANK;
        BS--;
        }                                               /* end for */
    break;                                              /* done at last! */     

/* Multiply.  Comments from the PDP-10 based simulator by Len Fehskens.

   Multiply, with variable length operands, is necessarily done the same
   way you do it with paper and pencil, except that partial products are
   added into the incomplete final product as they are computed, rather
   than at the end.  The 1401 multiplier format allows the product to
   be developed in place, without scratch storage.

   The A field contains the multiplicand, length LD.  The B field must be
   LD + 1 + length of multiplier.  Locate the low order multiplier digit,
   and at the same time zero out the product field.  Then compute the sign
   of the result.

   Instruction lengths:

   1            chained
   2,3          invalid A-address - checked in fetch
   4            self (B-address = A-address)
   5,6          invalid B-address - checked in fetch
   7            normal
   8+           normal + ignored modifier
*/

    case OP_MUL:
        asave = AS;                                     /* save AS, BS */
        bsave = lowprd = BS;
        do {
            a = M[AS];                                  /* get mpcd char */
            M[BS] = BCD_ZERO;                           /* zero prod */
            MM (AS);                                    /* decr pointers */
            MM (BS);
            } while ((a & WM) == 0);                    /* until A WM */
        if (reason)                                     /* address err? */
            break;
        M[BS] = BCD_ZERO;                               /* zero hi prod */
        MM (BS);                                        /* addr low mpyr */
        sign = ((M[asave] & ZONE) == BBIT) ^ ((M[BS] & ZONE) == BBIT);

/* Outer loop on multiplier (BS) and product digits (ps),
   inner loop on multiplicand digits (AS).
   AS and ps cannot produce an address error.
*/

    do {
        ps = bsave;                                     /* ptr to prod */
        AS = asave;                                     /* ptr to mpcd */
        carry = 0;                                      /* init carry */
        b = M[BS];                                      /* get mpyr char */
        do {
            a = M[AS];                                  /* get mpcd char */
            t = (bcd_to_bin[a & DIGIT] *                /* mpyr * mpcd */
                 bcd_to_bin[b & DIGIT]) +               /* + c + partial prod */
                 carry + bcd_to_bin[M[ps] & DIGIT];
            carry = cry_table[t];
            M[ps] = (M[ps] & WM) | sum_table[t];
            MM (AS);
            ps--;
            } while ((a & WM) == 0);                    /* until mpcd done */
        M[BS] = (M[BS] & WM) | BCD_ZERO;                /* zero mpyr just used */
        t = bcd_to_bin[M[ps] & DIGIT] + carry;          /* add carry to prod */
        M[ps] = (M[ps] & WM) | sum_table[t];            /* store */
        bsave--;                                        /* adv prod ptr */
        MM (BS);                                        /* adv mpyr ptr */
        } while ((b & WM) == 0);                        /* until mpyr done */
    M[lowprd] = M[lowprd] | ZONE;                       /* assume + */
    if (sign)                                           /* if minus, B only */
        M[lowprd] = M[lowprd] & ~ABIT;
    break;      

/* Divide.  Comments from the PDP-10 based simulator by Len Fehskens.

   Divide is done, like multiply, pretty much the same way you do it with
   pencil and paper; successive subtraction of the divisor from a substring
   of the dividend while counting up the corresponding quotient digit.

   Let LS be the length of the divisor, LD the length of the dividend:
   - AS points to the low order divisor digit.
   - BS points to the high order dividend digit.
   - The low order dividend digit is identified by sign (zone) bits.
   - To the left of the dividend is a (zero) field of length LS + 1.
   So the quotient starts as BS - LS - 1.
   The divide process starts with a subdividend that begins at BS - LS
   and ends at BS.  (Note that the subdividend is one digit wider than
   the divisor, to allow for borrows during the divide process.)  This
   means that non-zero digits in the "zero" field to the left of the
   dividend CAN affect the divide.

   Start by computing the length of the divisor and testing for divide
   by zero.

   Instruction lengths:

   1            chained
   2,3          invalid A-address - checked in fetch
   4            self (B-address = A-address)
   5,6          invalid B-address - checked in fetch
   7            normal
   8+           normal + ignored modifier
*/

    case OP_DIV:
        asave = AS;
        t = 0;                                          /* assume all 0's */
        do {                                            /* scan divisor */
            a = M[AS];                                  /* get dvr char */
            if ((bcd_to_bin[a & DIGIT]) != 0)           /* mark non-zero */
                t = 1;
            MM (AS);
            }
        while ((a & WM) == 0);
        if (reason)                                     /* address err? */
            break;
        if (t == 0) {                                   /* div by zero? */
            ind[IN_OVF] = 1;                            /* set ovf indic */
            qs = bsave = BS;                            /* quo, dividend */
            do {
                b = M[bsave];                           /* find end divd */
                PP (bsave);                             /* marked by zone */
                } while ((b & ZONE) == 0);
            if (reason)                                 /* address err? */
                break;
            if (ADDR_ERR (qs)) {                        /* address err? */
                reason = STOP_WRAP;                     /* address wrap? */
                break;
                }
            div_sign (M[asave], b, qs - 1, bsave - 1);  /* set signs */
            BS = (BS - 2) - (asave - (AS + 1));         /* final bs */
            break;
            }
        bsave = BS;                                     /* end subdivd */
        qs = BS - (asave - AS) - 1;                     /* quo start */

/* Divide loop - done with subroutines to keep the code clean.
   In the loop,
        
   asave =      low order divisor (constant)
   bsave =      low order subdividend (increments)
   qs   =       current quotient digit (increments)
*/

    do {
        quo = 0;                                        /* clear quo digit */
        if (ADDR_ERR (qs) || ADDR_ERR (bsave)) {
            reason = STOP_WRAP;                         /* address wrap? */
            break;
            }
        b = M[bsave];                                   /* save low divd */
        do {
            t = div_sub (asave, bsave);                 /* subtract */
            quo++;                                      /* incr quo digit */
            } while (t == 0);                           /* until borrow */
        div_add (asave, bsave);                         /* restore */
        quo--;
        if (quo > 9)                                    /* overflow? */
            ind[IN_OVF] = 1;                            /* set ovf indic */
        M[qs] = (M[qs] & WM) | sum_table[quo];          /* store quo digit */
        bsave++;                                        /* adv divd, quo */
        qs++;
        } while ((b & ZONE) == 0);                      /* until B sign */
    if (reason)                                         /* address err? */
        break;

/* At this point,

   AS   =       high order divisor - 1
   asave =      unit position of divisor
   b    =       unit character of dividend
   bsave =      unit position of remainder + 1
   qs   =       unit position of quotient + 1
*/

    div_sign (M[asave], b, qs - 1, bsave - 1);          /* set signs */
    BS = qs - 2;                                        /* BS = quo 10's pos */
    break;

/* Word mark instructions                               A check    B check

   SWM          set WM on A char and B char             fetch      fetch
   CWM          clear WM on A char and B char           fetch      fetch

   Instruction lengths:

   1            chained
   2,3          invalid A-address
   4            one operand (B-address = A-address)
   5,6          invalid B-address
   7            two operands (SWM cannot be longer than 7)
   8+           two operands + ignored modifier
*/

    case OP_SWM:                                        /* set word mark */
        M[BS] = M[BS] | WM;                             /* set A field mark */
        M[AS] = M[AS] | WM;                             /* set B field mark */
        MM (AS);                                        /* decr pointers */
        MM (BS);
        break;

    case OP_CWM:                                        /* clear word mark */
        M[BS] = M[BS] & ~WM;                            /* clear A field mark */
        M[AS] = M[AS] & ~WM;                            /* clear B field mark */
        MM (AS);                                        /* decr pointers */
        MM (BS);
        break;

/* Clear storage instruction                            A check    B check

   CS           clear from B down to nearest hundreds   if branch  fetch
                address

   Instruction lengths:

   1            chained
   2,3          invalid A-address and B-address
   4            one operand (B-address = A-address)
   5,6          invalid B-address
   7            branch
   8+           one operand, branch ignored

   Note that clear storage and branch does not overwrite the B register,
   unlike all other branches
*/

    case OP_CS:                                         /* clear storage */
        t = (BS / 100) * 100;                           /* lower bound */
        while (BS >= t)                                 /* clear region */
            M[BS--] = 0;
        if (BS < 0)                                     /* wrap if needed */
            BS = BS + MEMSIZE;
        if (ilnt == 7) {                                /* branch variant? */
            BRANCH_CS;                                  /* special branch */
            }
        break;

/* Modify address instruction                           A check    B check

   MA           add A addr and B addr, store at B addr  fetch      fetch

   Instruction lengths:
   1            chained
   2,3          invalid A-address and B-address
   4            self (B-address = A-address)
   5,6          invalid B-address
   7            normal
   8+           normal + ignored modifier
*/

    case OP_MA:                                         /* modify address */
        a = one_table[M[AS] & CHAR]; MM (AS);           /* get A address */
        a = a + ten_table[M[AS] & CHAR]; MM (AS);
        a = a + hun_table[M[AS] & CHAR]; MM (AS);
        b = one_table[M[BS] & CHAR]; MM (BS);           /* get B address */
        b = b + ten_table[M[BS] & CHAR]; MM (BS);
        b = b + hun_table[M[BS] & CHAR]; MM (BS);
        t = ((a + b) & INDEXMASK) % MAXMEMSIZE;         /* compute sum */
        M[BS + 3] = (M[BS + 3] & WM) | store_addr_u (t);
        M[BS + 2] = (M[BS + 2] & (WM + ZONE)) | store_addr_t (t);
        M[BS + 1] = (M[BS + 1] & WM) | store_addr_h (t);
        if (((a % 4000) + (b % 4000)) >= 4000)          /* carry? */
            BS = BS + 2;
        break;

/* Store address instructions                           A-check     B-check

   SAR          store A* at A addr                      fetch
   SBR          store B* at A addr                      fetch

   Instruction lengths:
   1            chained
   2,3          invalid A-address
   4            normal
   5+           B-address overwritten from instruction;
                invalid address ignored
*/

    case OP_SAR: case OP_SBR:                           /* store A, B reg */
        M[AS] = (M[AS] & WM) | store_addr_u (BS);
        MM (AS);
        M[AS] = (M[AS] & WM) | store_addr_t (BS);
        MM (AS);
        M[AS] = (M[AS] & WM) | store_addr_h (BS);
        MM (AS);
        break;

/* NOP - no validity checking, all instructions length ok */

    case OP_NOP:                                        /* nop */
        break;

/* HALT - unless length = 4 (branch), no validity checking; all lengths ok */

    case OP_H:                                          /* halt */
        if (ilnt == 4)                                  /* set pending branch */
            hb_pend = 1;
        reason = STOP_HALT;                             /* stop simulator */
        saved_IS = IS;                                  /* commit instruction */
        break;

    default:
        reason = STOP_NXI;                              /* unimplemented */
        break;
        }                                               /* end switch */
    }                                                   /* end while */

/* Simulation halted */

as_err = ADDR_ERR (AS);                                 /* get addr err flags */
bs_err = ADDR_ERR (BS);
AS = AS & ADDRMASK;                                     /* clean addresses */
BS = BS & ADDRMASK;
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}                                                       /* end sim_instr */

/* store addr_x - convert address to BCD character in x position

   Inputs:
        addr    =       address to convert
   Outputs:
        char    =       converted address character
*/

int32 store_addr_h (int32 addr)
{
int32 thous;

thous = (addr / 1000) & 03;
return  bin_to_bcd[(addr % 1000) / 100] | (thous << V_ZONE);
}

int32 store_addr_t (int32 addr)
{
return bin_to_bcd[(addr % 100) / 10];
}

int32 store_addr_u (int32 addr)
{
int32 thous;

thous = (addr / 1000) & 014;
return bin_to_bcd[addr % 10] | (thous << (V_ZONE - 2));
}

/* div_add - add string for divide */

int32 div_add (int32 ap, int32 bp)
{
int32 a, b, c, r;

c = 0;                                                  /* init carry */
do {
    a = M[ap];                                          /* get operands */
    b = M[bp];
    r = bcd_to_bin[b & DIGIT] +                         /* sum digits + c */
        bcd_to_bin[a & DIGIT] + c;
    c = (r >= 10);                                      /* set carry out */
    M[bp] = (M[bp] & WM) | sum_table[r];                /* store result */
    ap--;
    bp--;
    } while ((a & WM) == 0);
return c;
}

/* div_sub - substract string for divide */

int32 div_sub (int32 ap, int32 bp)
{
int32 a, b, c, r;

c = 0;                                                  /* init borrow */
do {
    a = M[ap];                                          /* get operands */
    b = M[bp];
    r = bcd_to_bin[b & DIGIT] -                         /* a - b - borrow */
        bcd_to_bin[a & DIGIT] - c;
    c = (r < 0);                                        /* set borrow out */
    M[bp] = (M[bp] & WM) | sum_table[r + 10];           /* store result */
    ap--;
    bp--;
    } while ((a & WM) == 0);
b = M[bp];                                              /* borrow position */
if (bcd_to_bin[b & DIGIT] != 0) {                       /* non-zero? */
    r = bcd_to_bin[b & DIGIT] - c;                      /* subtract borrow */
    M[bp] = (M[bp] & WM) | sum_table[r];                /* store result */
    return 0;                                           /* subtract worked */
    }
return c;                                               /* return borrow */
}

/* div_sign - set signs for divide */

void div_sign (int32 dvrc, int32 dvdc, int32 qp, int32 rp)
{
int32 sign = dvrc & ZONE;                               /* divisor sign */

M[rp] = M[rp] | ZONE;                                   /* assume rem pos */
if (sign == BBIT)                                       /* if dvr -, rem - */
    M[rp] = M[rp] & ~ABIT;
M[qp] = M[qp] | ZONE;                                   /* assume quo + */
if (((dvdc & ZONE) == BBIT) ^ (sign == BBIT))           /* dvr,dvd diff? */
    M[qp] = M[qp] & ~ABIT;                              /* make quo - */
return;
}

/* iomod - check on I/O modifiers

   Inputs:
        ilnt    =       instruction length
        mod     =       modifier character
        tptr    =       pointer to table of modifiers, end is -1
   Output:
        status  =       SCPE_OK if ok, STOP_INVM if invalid
*/

t_stat iomod (int32 ilnt, int32 mod, const int32 *tptr)
{
if ((ilnt != 2) && (ilnt != 5) && (ilnt < 8))
    return SCPE_OK;
if (tptr == NULL)
    return STOP_INVM;
do {
    if (mod == *tptr++)
        return SCPE_OK;
    } while (*tptr >= 0);
return STOP_INVM;
}

/* iodisp - dispatch load or move to I/O routine

   Inputs:
        dev     =       device number
        unit    =       unit number
        flag    =       move (MD_NORM) vs load (MD_WM)
        mod     =       modifier
*/

t_stat iodisp (int32 dev, int32 unit, int32 flag, int32 mod)
{
if (dev == IO_INQ)                                      /* inq terminal? */
    return inq_io (flag, mod);
if (dev == IO_DP)                                       /* disk pack? */
    return dp_io (unit, flag, mod);
if (dev == IO_MT)                                       /* magtape? */
    return mt_io (unit, flag, mod);
if (dev == IO_MTB)                                      /* binary magtape? */
    return mt_io (unit, flag | MD_BIN, mod);
return STOP_NXD;                                        /* not implemented */
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < 64; i++) {                              /* clr indicators */
    if ((i < IN_SSB) || (i > IN_SSG))                   /* except SSB-SSG */
        ind[i] = 0;
    }
ind[IN_UNC] = 1;                                        /* ind[0] always on */
AS = 0;                                                 /* clear AS */
BS = 0;                                                 /* clear BS */
as_err = 1;
bs_err = 1;
D = 0;                                                  /* clear D */
hb_pend = 0;                                            /* no halt br */
pcq_r = find_reg ("ISQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & (WM + CHAR);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
M[addr] = val & (WM + CHAR);
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val % 1000) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
    mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    M[i] = 0;
if (MEMSIZE > 4000)
    cpu_unit.flags = cpu_unit.flags | MA;
else cpu_unit.flags = cpu_unit.flags & ~MA;
return SCPE_OK;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].ilnt = 0;
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
int32 i, k, di, lnt;
CONST char *cptr = (CONST char *) desc;
t_value sim_eval[MAX_L + 1];
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
fprintf (st, "IS     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->ilnt) {                                      /* instruction? */
        fprintf (st, "%05d  ", h->is);
        for (i = 0; i < h->ilnt; i++)
            sim_eval[i] = h->inst[i];
        sim_eval[h->ilnt] = WM;
        if ((fprint_sym (st, h->is, sim_eval, &cpu_unit, SWMASK ('M'))) > 0) {
            fprintf (st, "(undefined)");
            for (i = 0; i < h->ilnt; i++)
                fprintf (st, " %02o", h->inst[i]);
            }
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

/* Set conversions */

t_stat cpu_set_conv (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
conv_old = val;
return SCPE_OK;
}

/* Show conversions */

t_stat cpu_show_conv (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (conv_old)
    fputs ("Old (pre-3.5-1) conversions\n", st);
else fputs ("New conversions\n", st);
return SCPE_OK;
}
