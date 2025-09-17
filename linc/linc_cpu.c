/* linc_cpu.c: LINC CPU simulator

   Copyright (c) 2025, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.
*/

#include "linc_defs.h"


/* Debug */
#define DBG_CPU   0001
#define DBG_INT   0002

#define INSN_ENI  00010
#define INSN_NOP  00016
#define INSN_OPR  00500
#define INSN_MTP  00700
#define INSN_JMP  06000

#define X(_X)  ((_X) & XMASK)
#define C03    (C & BMASK)

/* CPU state. */
static uint16 P;
static uint16 C;
static uint16 S;
static uint16 B;
static uint16 A;
static uint16 L;
static uint16 Z;
static uint16 R;
static uint16 LSW, RSW, SSW;
static uint16 SAM[16];
static uint16 XL[12];
static int paused;
static int IBZ;
static int OVF;
static int INTREQ;
static int ENI = 0;
static int PINFF;
static int DO = 0;

static t_stat stop_reason;

typedef struct {
  uint16 P;
  uint16 C;
  uint16 S;
  uint16 B;
  uint16 A;
  uint16 L;
} HISTORY;
static HISTORY *history = NULL;
static uint32 history_i, history_m, history_n;

/* Function declaration. */
static t_stat cpu_ex(t_value *vptr, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_dep(t_value val, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_reset(DEVICE *dptr);
static t_stat cpu_set_hist(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat cpu_show_hist(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat linc_boot(int32 flag, CONST char *ptr);
static t_stat linc_do(int32 flag, CONST char *ptr);

static UNIT cpu_unit = { UDATA(NULL, UNIT_FIX + UNIT_BINK, MEMSIZE) };

REG cpu_reg[] = {
  { ORDATAD(P,   P,   10, "Program Location") },
  { ORDATAD(C,   C,   12, "Control Register") },
  { ORDATAD(A,   A,   12, "Accumulator") },
  { ORDATAD(L,   L,    1, "Link") },
  { ORDATAD(Z,   Z,   12, "?") },
  { ORDATAD(R,   R,    6, "Relay Register") },
  { ORDATAD(S,   S,   12, "Memory Address") },
  { ORDATAD(B,   B,   12, "Memory Buffer") },
  { ORDATAD(LSW, LSW, 12, "Left Switches") },
  { ORDATAD(RSW, RSW, 12, "Right Switches") },
  { ORDATAD(SSW, SSW,  6, "Sense Switches") },

  { FLDATAD(paused, paused, 1, "Paused") },
  { FLDATAD(IBZ,    IBZ,    1, "Interblock zone") },
  { FLDATAD(OVF,    OVF,    1, "Overflow") },
  { FLDATAD(INTREQ, INTREQ, 1, "Interrupt") },
  { FLDATAD(ENI,    ENI,    1, "Interrupt Enable") },
  { FLDATAD(PIN,    PINFF,  1, "Pause Interrupt") },

  { BRDATAD(SAM,    SAM,    8, 8, 16, "Sampled analog inputs") },
  { BRDATAD(XL,     XL,     8, 1, 12, "External levels") },
  { NULL }
};

static MTAB cpu_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
    &cpu_set_hist, &cpu_show_hist },
  { 0 }
};

static DEBTAB cpu_deb[] = {
  { "CPU", DBG_CPU },
  { "INTERRUPT", DBG_INT },
  { NULL, 0 }
};

DEVICE cpu_dev = {
  "CPU", &cpu_unit, cpu_reg, cpu_mod,
  0, 8, 11, 1, 8, 12,
  &cpu_ex, &cpu_dep, &cpu_reset,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, cpu_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static CTAB linc_cmd[] = {
  { "BOOT", &linc_boot, 0,
    "BOOT {unit}                boot simulator\n"
    "BOOT TAPE{n} RCG={blocks}  boot tape from specified blocks\n", NULL, &run_cmd_message },
  { "DO", &linc_do, 0,
    "DO {unit}                boot simulator\n"
    "DO                       execute instruction in LSW and RSW\n", NULL, NULL },
  { NULL }
};

static void cpu_ndxp(int flag)
{
  if (flag)
    P = X(P + 1);
}

static void cpu_ndxc()
{
  C = (C & ~BMASK) | ((C + 1) & BMASK);
}

static void cpu_set_S(uint16 addr)
{
  S = addr & WMASK;
}

static void cpu_set_B(uint16 data)
{
  B = data & WMASK;
}

static void cpu_4ndxb()
{
  cpu_set_B(B + 4);
}

static void cpu_4ndxa()
{
  A = (A + 4) & WMASK;
}

static void cpu_mem_read(void)
{
  cpu_set_B(M[S & AMASK]);
  sim_interval--;
  if (sim_brk_summ && sim_brk_test(S & AMASK, SWMASK('R')))
    stop_reason = STOP_RBKPT;
}

static void cpu_mem_modify(void)
{
  M[S & AMASK] = B;
  if (sim_brk_summ && sim_brk_test(S & AMASK, SWMASK('W')))
    stop_reason = STOP_WBKPT;
}

static void cpu_mem_write(void)
{
  sim_interval--;
  cpu_mem_modify();
}

static void cpu_insn_addr()
{
  if (!DO) {
    cpu_set_S(P);
    cpu_ndxp(1);
  }
}

static void cpu_insn_read()
{
  if (!DO)
    cpu_mem_read();
}

static void cpu_fetch()
{
  cpu_insn_addr();
  cpu_insn_read();
}

static int cpu_halfword(void)
{
  switch (C & 07740) {
  case 01300: //LDH
  case 01340: //STH
  case 01400: //SHD
    return 1;
  default:
    return 0;
  }
}

static void cpu_index(void)
{
  uint16 tmp;
  if (C & IMASK) {
    if (cpu_halfword()) {
      B += HMASK;
      tmp = B >> 12;
    } else {
      tmp = 1;
    }
    cpu_set_B((B & 06000) | X(B + tmp));
    cpu_mem_modify();
  }
}

static void cpu_indexing(void)
{
  uint16 a = C03;
  if (a == 0) {
    cpu_insn_addr();
    if ((C & IMASK) == 0) {
      cpu_insn_read();
      cpu_set_S(B);
    }
  } else {
    cpu_set_S(a);
    cpu_mem_read();
    cpu_index();
    cpu_set_S(B);
  }
}

static void
cpu_misc(void)
{
  switch (C) {
  case 00000: //HLT
    stop_reason = STOP_HALT;
    break;
  case 00002: //PDP
    sim_debug(DBG_CPU, &cpu_dev, "This is not a PDP-12.\n");
    break;
  case 00005: //ZTA
    A = Z >> 1;
    break;
  case 00010: //ENI
    sim_debug(DBG_INT, &cpu_dev, "Interrupt enabled.\n");
    ENI = 1;
    break;
  case 00011: //CLR
    A = L = Z = 0;
    break;
  case 00012: //DIN
    sim_debug(DBG_INT, &cpu_dev, "Interrupt disabled.\n");
    ENI = 0;
    break;
  case 00013: //Write gate on.
    break;
  case 00014: //ATR
    R = A & RMASK;
    break;
  case 00015: //RTA
    A = R & RMASK;
    break;
  case 00016: //NOP
    break;
  case 00017: //COM
    A = (~A) & WMASK;
    break;
  }
}

static void cpu_set(void)
{
  cpu_fetch();
  if ((C & IMASK) == 0) {
    cpu_set_S(B);
    cpu_mem_read();
  }
  cpu_set_S(C03);
  cpu_mem_write();
}

static void cpu_sam(void)
{
  // sample analog input C03
  // 0-7 are pots, 10-17 are high speed inputs
  // i=0 wait 24 microseconds, i=1 do not wait
  if ((C & IMASK) == 0)
    sim_interval -= 3;
  A = SAM[C03];
  if (A & 0200) /* One's complement +/-177. */
    A |= 07400;
}

static void cpu_dis(void)
{
  cpu_set_S(C03);
  cpu_mem_read();
  cpu_index();
  sim_debug(DBG_CPU, &cpu_dev, "DIS α=%02o B=%04o A=%04o\n", S, B, A);
  dpy_dis(B >> 11, B & DMASK, A & DMASK);
}

static void cpu_xsk(void)
{
  cpu_set_S(C03);
  cpu_mem_read();
  cpu_index();
  cpu_ndxp(X(B) == 01777);
}

static void cpu_rol(void)
{
  C = (C & ~BMASK) | (~B & BMASK);
  while (C03 != 017) {
    if (C & IMASK) {
      A = (A << 1) | L;
      L = A >> 12;
    } else {
      A = (A << 1) | (A >> 11);
    }
    A &= WMASK;
    cpu_ndxc();
  }
}

static void cpu_ror(void)
{
  C = (C & ~BMASK) | (~B & BMASK);
  while (C03 != 017) {
    Z = (Z >> 1) | ((A & 1) << 11);
    if (C & IMASK) {
      A |= L << 12;
      L = A & 1;
      A = A >> 1;
    } else {
      A = (A >> 1) | (A << 11);
      A &= WMASK;
    }
    cpu_ndxc();
  }
}

static void cpu_scr(void)
{
  C = (C & ~BMASK) | (~B & BMASK);
  while (C03 != 017) {
    Z = (Z >> 1) | ((A & 1) << 11);
    if (C & IMASK)
      L = A & 1;
    A = (A & 04000) | (A >> 1);
    cpu_ndxc();
  }
}

int cpu_skip(void)
{
  int flag;
  switch (C & 057) {
  case 000: case 001: case 002: case 003: case 004: case 005: case 006: case 007:
  case 010: case 011: case 012: case 013: //SXL
    flag = XL[C03];
    break;
  case 015: //KST
    flag = kbd_struck();
    break;
  case 040: case 041: case 042: case 043: case 044: case 045: //SNS
    flag = SSW & (1 << (C & 7));
    break;
  case 046: //PIN
    flag = PINFF;
    sim_debug(DBG_INT, &cpu_dev, "Pause interrupt enabled.\n");
    PINFF = 0;
    break;
  case 050: //AZE
    flag = (A == 0) || (A == WMASK);
    break;
  case 051: //APO
    flag = (A & 04000) == 0;
    break;
  case 052: //LZE
    flag = L == 0;
    break;
  case 053: //IBZ
    flag = IBZ;
    sim_debug(DBG_CPU, &cpu_dev, "IBZ%s => %d\n", C & IMASK ? " i" : "", flag);
    break;
  case 054: //OVF
    flag = OVF;
    break;
  case 055: //ZZZ
    flag = (Z & 1) == 0;
    break;
  default:
    flag = 0;
    break;
  }
  if (C & IMASK)
    flag = !flag;
  return flag;
}

static void cpu_opr(void)
{
  switch (C03) {
  case 000: case 001: case 002: case 003: case 004: case 005: case 006: case 007:
  case 010: case 011: case 012: case 013:
    if (C & IMASK)
      ; //Pause.
    break;
  case 015: //KBD
    A = kbd_key(C & IMASK);
    break;
  case 016: //RSW
    A = RSW;
    break;
  case 017: //LSW
    A = LSW;
    break;
  }
}

static void cpu_lmb(void)
{
  /* Lower memory bank. */
  sim_debug(DBG_CPU, &cpu_dev, "This is not micro-LINC 300.\n");
}

static void cpu_umb(void)
{
  /* Upper memory bank. */
  sim_debug(DBG_CPU, &cpu_dev, "This is not micro-LINC 300.\n");
}

static void cpu_tape(void)
{
  cpu_fetch();
  tape_op();
}

static void cpu_lda(void)
{
  cpu_mem_read();
  A = B;
}

static void cpu_sta(void)
{
  cpu_set_B(A);
  /* Do not write immediate value if executing out of switches. */
  if (!DO || (C & IMASK) == 0)
    cpu_mem_write();
}

static void cpu_ada(void)
{
  cpu_mem_read();
  OVF = ~(A ^ B);
  A += B;
  A += A >> 12;
  A &= WMASK;
  OVF &= (A ^ B) & 04000;
}

static void cpu_adm(void)
{
  cpu_ada();
  cpu_set_B(A);
  cpu_mem_modify();
}

static void cpu_lam(void)
{
  cpu_mem_read();
  A += L;
  L = A >> 12;
  A &= WMASK;
  A += B;
  if (A & 010000)
    L = 1;
  A &= WMASK;
  cpu_set_B(A);
  cpu_mem_modify();
}

static void cpu_mul(void)
{
  uint32 factor, product;
  cpu_mem_read();

  C &= ~BMASK;
  L = (A ^ B) >> 11;
  if (A & HMASK)
    A ^= WMASK;
  if (B & HMASK)
    B ^= WMASK;
  Z = B;
  cpu_set_B(A);
  factor = B;
  product = A = 0;
  while (C03 < 12) {
    if (Z & 1)
      product += factor;
    Z >>= 1;
    factor <<= 1;
    cpu_ndxc();
  }
  if (S & HMASK)
    A = product >> 11;
  else
    A = product & 03777;
  if (L)
    A ^= 07777;
}

static void cpu_ldh(void)
{
  cpu_mem_read();
  if ((S & HMASK) == 0)
    B >>= 6;
  A = B & RMASK;
}

static void cpu_sth(void)
{
  cpu_mem_read();
  if (S & HMASK)
    cpu_set_B((A & RMASK) | (B & LMASK));
  else
    cpu_set_B((A << 6) | (B & RMASK));
  cpu_mem_modify();
}

static void cpu_shd(void)
{
  cpu_mem_read();
  if ((S & HMASK) == 0)
    B >>= 6;
  cpu_ndxp((A & RMASK) != (B & RMASK));
}

static void cpu_sae(void)
{
  cpu_mem_read();
  cpu_ndxp(A == B);
}

static void cpu_sro(void)
{
  cpu_mem_read();
  cpu_ndxp((B & 1) == 0);
  cpu_set_B((B >> 1) | (B << 11));
  cpu_mem_modify();
}

static void cpu_bcl(void)
{
  cpu_mem_read();
  A &= ~B;
}

static void cpu_bse(void)
{
  cpu_mem_read();
  A |= B;
}

static void cpu_bco(void)
{
  cpu_mem_read();
  A ^= B;
}

static void cpu_dsc(void)
{
  cpu_mem_read();
  Z = B;

  cpu_set_S(1);
  cpu_mem_read();
  sim_debug(DBG_CPU, &crt_dev, "DSC B=%04o A=%04o\n", B, A);

  C &= ~BMASK;
  while (C03 < 12) {
    if (C03 == 0 || C03 == 6) {
      A &= 07740;
      cpu_4ndxb();
    }
    if (Z & 1)
      dpy_dis(B >> 11, B & DMASK, A & DMASK);
    Z >>= 1;
    cpu_4ndxa();
    cpu_ndxc();
  }
  cpu_mem_write();
}

static void cpu_add(void)
{
  cpu_set_S(X(C));
  cpu_ada();
}

static void cpu_stc(void)
{
  cpu_set_S(X(C));
  cpu_set_B(A);
  A = 0;
  cpu_mem_write();
}

static void cpu_jmp(void)
{
  uint16 tmp = P;
  P = X(C);
  if (P != 0) {
    cpu_set_B(INSN_JMP | tmp);
    cpu_set_S(0);
    cpu_mem_write();
  }
}

static void
cpu_insn(void)
{
  /* Cycle 0, or I. */
  cpu_fetch();
  if (!DO)
    C = B;

  /* Cycle 1, or X. */
  if ((C & 07000) == 01000)
    cpu_indexing();

  if (history) {
    history[history_i].P = P;
    history[history_i].C = C;
    history[history_i].S = S;
  }

  /* Cycle 2, or O. */

  /* Cycle 3, or E. */
  switch (C & 07740) {
  case 00000:
    cpu_misc();
    break;
  case 00040:
    cpu_set();
    break;
  case 00100:
    cpu_sam();
    break;
  case 00140:
    cpu_dis();
    break;
  case 00200:
    cpu_xsk();
    break;
  case 00240:
    cpu_rol();
    break;
  case 00300:
    cpu_ror();
    break;
  case 00340:
    cpu_scr();
    break;
  case 00400:
  case 00440:
    cpu_ndxp(cpu_skip());
    break;
  case 00500:
  case 00540:
    cpu_opr();
    break;
  case 00600:
    cpu_lmb();
    break;
  case 00640:
    cpu_umb();
    break;
  case 00700:
  case 00740:
    cpu_tape();
    break;
  case 01000:
    cpu_lda();
    break;
  case 01040:
    cpu_sta();
    break;
  case 01100:
    cpu_ada();
    break;
  case 01140:
    cpu_adm();
    break;
  case 01200:
    cpu_lam();
    break;
  case 01240:
    cpu_mul();
    break;
  case 01300:
    cpu_ldh();
    break;
  case 01340:
    cpu_sth();
    break;
  case 01400:
    cpu_shd();
    break;
  case 01440:
    cpu_sae();
    break;
  case 01500:
    cpu_sro();
    break;
  case 01540:
    cpu_bcl();
    break;
  case 01600:
    cpu_bse();
    break;
  case 01640:
    cpu_bco();
    break;
  case 01740:
    cpu_dsc();
    break;
  case 02000: case 02040: case 02100: case 02140: case 02200: case 02240: case 02300: case 02340:
  case 02400: case 02440: case 02500: case 02540: case 02600: case 02640: case 02700: case 02740:
  case 03000: case 03040: case 03100: case 03140: case 03200: case 03240: case 03300: case 03340:
  case 03400: case 03440: case 03500: case 03540: case 03600: case 03640: case 03700: case 03740:
    cpu_add();
    break;
  case 04000: case 04040: case 04100: case 04140: case 04200: case 04240: case 04300: case 04340:
  case 04400: case 04440: case 04500: case 04540: case 04600: case 04640: case 04700: case 04740:
  case 05000: case 05040: case 05100: case 05140: case 05200: case 05240: case 05300: case 05340:
  case 05400: case 05440: case 05500: case 05540: case 05600: case 05640: case 05700: case 05740:
    cpu_stc();
    break;
  case 06000: case 06040: case 06100: case 06140: case 06200: case 06240: case 06300: case 06340:
  case 06400: case 06440: case 06500: case 06540: case 06600: case 06640: case 06700: case 06740:
  case 07000: case 07040: case 07100: case 07140: case 07200: case 07240: case 07300: case 07340:
  case 07400: case 07440: case 07500: case 07540: case 07600: case 07640: case 07700: case 07740:
    cpu_jmp();
    break;
  }

  if (history) {
    history[history_i].B = B;
    history[history_i].A = A;
    history[history_i].L = L;
    history_i = (history_i + 1) % history_m;
    if (history_n < history_m)
      history_n++;
  }
}

t_stat cpu_do(void)
{
  t_stat stat;

  DO = 1;
  C = LSW;
  cpu_set_B(RSW);
  cpu_insn();
  DO = 0;

  sim_interval = 1;
  /* Can not return from DO until the instruction is done,
     i.e. not paused. */
  while (paused) {
    AIO_CHECK_EVENT;
    if (sim_interval <= 0) {
      if ((stat = sim_process_event()) != SCPE_OK)
        return stat;
    }
    sim_interval--;
  }
  return SCPE_OK;
}

static int jmp_or_eni(void)
{
  return (C & 06000) == INSN_JMP || (C == INSN_ENI);
}

static int mtp_or_opr(void)
{
  return (C & 07700) == INSN_MTP || (C & 07700) == INSN_OPR;
}

static void cpu_interrupt(void)
{
  if (!INTREQ)
    return;
  if (!ENI)
    return;

  sim_debug(DBG_INT, &cpu_dev, "Interrupt requested and enabled.\n");

  if (jmp_or_eni()) {
    sim_debug(DBG_INT, &cpu_dev, "Interrupt not taken after JMP or ENI.\n");
    return;
  }

  if (paused) {
    if (!mtp_or_opr()) {
      sim_debug(DBG_INT, &cpu_dev, "Pause only interrupted for MTP or OPR.\n");
      return;
    }
    if (PINFF)
      return;
    sim_debug(DBG_INT, &cpu_dev, "Pause interrupted.\n");
    PINFF = 1;
    paused = 0;
  }

  sim_debug(DBG_INT, &cpu_dev, "Interrupt taken.\n");

  cpu_set_S(021);
  cpu_mem_read();
  C = B;
  if (history) {
    history[history_i].P = 07777;
    history[history_i].C = C;
    history[history_i].S = S;
  }

  ENI = 0; /* Except for OPR. */
  if ((C & 06000) == INSN_JMP)
    cpu_jmp();
  else if ((C & 07700) == INSN_OPR) {
    ENI = 1; /* OPR doesn't disable interrupts. */
    cpu_opr();
  } else if (C == INSN_NOP)
    ;
  else
    sim_debug(DBG_INT, &cpu_dev, "Invalid interrupt instruction.\n");

  if (history) {
    history[history_i].B = B;
    history[history_i].A = A;
    history_i = (history_i + 1) % history_m;
    if (history_n < history_m)
      history_n++;
  }
}

t_stat sim_instr(void)
{
  t_stat stat;

  if ((stat = build_dev_tab()) != SCPE_OK)
    return stat;

  /* Stepping is based on sim_step, not sim_interval.  The latter is
     approximately memory cycles, not instructions. */
  sim_cancel_step();

  /* Because we check sim_step before cpu_insn. */
  if (sim_step)
    sim_step++;

  stop_reason = 0;
  paused = 0;
  PINFF = 0;
  ENI = 0;

  for (;;) {
    AIO_CHECK_EVENT;
    if (sim_interval <= 0) {
      if ((stat = sim_process_event()) != SCPE_OK)
        return stat;
    }

    if (sim_brk_summ && sim_brk_test(P, SWMASK('E')))
      return STOP_IBKPT;

    /* Can not return from a STEP until the instruction is done,
       i.e. not paused. */
    if (!paused && sim_step != 0) {
      if (--sim_step == 0)
        return SCPE_STEP;
    }

    if (paused)
      sim_interval--;
    else
      cpu_insn();

    cpu_interrupt();

    if (stop_reason)
      return stop_reason;
  }

  return SCPE_OK;
}

static t_stat cpu_ex(t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
  if (vptr == NULL)
    return SCPE_ARG;
  if (ea >= MEMSIZE)
    return SCPE_NXM;
  *vptr = M[ea];
  return SCPE_OK;
}

static t_stat cpu_dep(t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
  if (ea >= MEMSIZE)
    return SCPE_NXM;
  M[ea] = val & WMASK;
  return SCPE_OK;
}

static t_stat
cpu_set_hist(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat r;
  uint32 x;

  if (cptr == NULL)
    return SCPE_ARG;

  x = get_uint (cptr, 10, 1000000, &r);
  if (r != SCPE_OK)
    return r;

  history = (HISTORY *)calloc (x, sizeof (*history));
  if (history == NULL)
    return SCPE_MEM;

  history_m = x;
  history_n = 0;
  history_i = 0;
  return SCPE_OK;
}

static t_stat
cpu_show_hist(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  t_value insn;
  uint32 i, j;

  fprintf (st, "P___ C___ S___ B___ A___ L\n");

  if (history_i >= history_n)
    j = history_i - history_n;
  else
    j = history_m + history_i - history_n;

  for (i = 0; i < history_n; i++) {
    if (history[j].P == 07777)
      fprintf (st, "---- ");
    else
      fprintf (st, "%04o ", history[j].P);
    fprintf (st, "%04o %04o %04o %04o %d  ",
             history[j].C,
             history[j].S,
             history[j].B,
             history[j].A,
             history[j].L);
    insn = history[j].C;
    fprint_sym(st, history[j].P, &insn, NULL, SWMASK('M'));
    fputc('\n', st);
    j = (j + 1) % history_m;
  }

  return SCPE_OK;
}

static t_stat
cpu_reset(DEVICE *dptr)
{
  sim_brk_types = SWMASK('E') | SWMASK('R') | SWMASK('W');
  sim_brk_dflt = SWMASK('E');
  sim_vm_cmd = linc_cmd;
  return SCPE_OK;
}

static t_stat linc_boot(int32 flag, CONST char *cptr)
{
  char dev[CBUFSIZE], arg[CBUFSIZE];
  char bbuf[CBUFSIZE], gbuf[CBUFSIZE];
  t_value block;
  t_stat stat;

  /* Is it BOOT TAPE? */
  cptr = get_glyph(cptr, dev, 0);
  if (*dev == 0)
    return SCPE_ARG;
  if (strncmp(dev, "TAPE", 4) != 0)
    return run_cmd(RU_BOOT, dev);

  /* Yes.  Is there an argument after? */
  if (*cptr == 0)
    return run_cmd(RU_BOOT, dev);

  bbuf[0] = 0;
  strcpy(gbuf, "20");
  while (*cptr) {
    cptr = get_glyph(cptr, arg, 0);
    if (strncmp(arg, "RDC=", 4) == 0)
      LSW = 0700, strcpy(bbuf, arg + 4);
    else if (strncmp(arg, "RCG=", 4) == 0)
      LSW = 0701, strcpy(bbuf, arg + 4);
    else if (strncmp(arg, "START=", 6) == 0)
      LSW = 0701, strcpy(gbuf, arg + 6);
    else
      return SCPE_ARG;
  }

  if (*bbuf == 0)
    return SCPE_ARG;

  /* It's a BOOT TAPE RDC= or RCG=, so start from switches. */
  block = get_uint(bbuf, 8, ~0, &stat);
  if (stat != SCPE_OK)
    return stat;

  RSW = block;
  stat = cpu_do();
  if (stat != SCPE_OK)
    return stat;
  return run_cmd(RU_GO, gbuf);
}

static t_stat linc_do(int32 flag, CONST char *cptr)
{
  /* With arguments, regular DO to execute script. */
  if (*cptr != 0)
    return do_cmd(flag, cptr);

  /* No arguments, push the DO button on the LINC control panel. */
  return cpu_do();
}
