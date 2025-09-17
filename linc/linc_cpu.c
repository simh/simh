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
#define DBG_CPU         0001

#define WMASK  07777  /* Full word. */
#define HMASK  04000  /* H bit; half word select. */
#define AMASK  03777  /* Full memory address. */
#define XMASK  01777  /* X part; low memory address. */
#define DMASK  00777  /* Display coordinate. */
#define LMASK  07700  /* Left half word. */
#define RMASK  00077  /* Right half word; character. */
#define IMASK  00020  /* Index bit. */
#define BMASK  00017  /* Beta; index register. */

#define X(_X)  ((_X) & XMASK)

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
static int PINFF;
static int interrupt;
static int interrupt_enable = 0;

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

static UNIT cpu_unit = { UDATA(NULL, UNIT_FIX + UNIT_BINK, MEMSIZE) };

REG cpu_reg[] = {
  { ORDATAD(P,   P,   10, "Program Location") },
  { ORDATAD(C,   C,   12, "Control Register") },
  { ORDATAD(A,   A,   12, "Accumulator") },
  { ORDATAD(L,   L,    1, "Link") },
  { ORDATAD(Z,   Z,   12, "?") },
  { ORDATAD(R,   R,    6, "Relay Register") },
  { ORDATAD(S,   S,   12, "Memroy Address") },
  { ORDATAD(B,   B,   12, "Memory Buffer") },
  { ORDATAD(LSW, LSW, 12, "Left Switches") },
  { ORDATAD(RSW, RSW, 12, "Right Switches") },
  { ORDATAD(SSW, SSW,  6, "Sense Switches") },
  { FLDATAD(paused, paused, 1, "Paused") },
  { FLDATAD(IBZ, IBZ,  1, "Interblock zone") },
  { FLDATAD(OVF, OVF,  1, "Overflow") },
  { BRDATAD(SAM, SAM,  8, 8, 16, "Sampled analog inputs") },
  { BRDATAD(XL,  XL,   8, 1, 12, "External levels") },
  { NULL }
};

static MTAB cpu_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
    &cpu_set_hist, &cpu_show_hist },
  { 0 }
};

static DEBTAB cpu_deb[] = {
  { "CPU", DBG_CPU },
  { NULL, 0 }
};

DEVICE cpu_dev = {
  "CPU", &cpu_unit, cpu_reg, cpu_mod,
  0, 8, 11, 1, 8, 12,
  &cpu_ex, &cpu_dep, &cpu_reset,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, cpu_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static void pcinc(int flag)
{
  if (flag)
    P = X(P + 1);
}

static void memaddr(uint16 addr)
{
  S = addr & WMASK;
}

static void membuf(uint16 data)
{
  B = data & WMASK;
}

static void memrd(void)
{
  B = M[S & AMASK];
  sim_interval--;
  if (sim_brk_summ && sim_brk_test(S & AMASK, SWMASK('R')))
    stop_reason = STOP_RBKPT;
}

static void memmd(void)
{
  M[S & AMASK] = B;
  if (sim_brk_summ && sim_brk_test(S & AMASK, SWMASK('W')))
    stop_reason = STOP_WBKPT;
}

static void memwr(void)
{
  sim_interval--;
  memmd();
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
    membuf((B & 06000) | X(B + tmp));
    memmd();
  }
}

static void cpu_indexing(void)
{
  uint16 a = C & BMASK;
  if (a == 0)
    {
      pcinc(1);
      memaddr(P);
      if ((C & IMASK) == 0) {
        memrd();
        memaddr(B);
      }
    }
  else
    {
      memaddr(a);
      memrd();
      cpu_index();
      memaddr(B);
    }
}

static void
cpu_misc(void)
{
  switch (C) {
  case 00000: //HLT
    stop_reason = STOP_HALT;
    break;
  case 00005: //ZTA
    A = Z >> 1;
    break;
  case 00010: //ENI
    interrupt_enable = 1;
    break;
  case 00011: //CLR
    A = L = Z = 0;
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
  memaddr(P);
  memrd();
  pcinc(1);
  if ((C & IMASK) == 0) {
    memaddr(B);
    memrd();
  }
  memaddr(C & BMASK);
  memwr();
}

static void cpu_sam(void)
{
  // sample analog input C & BMASK
  // 0-7 are pots, 10-17 are high speed inputs
  // i=0 wait 24 microseconds, i=1 do not wait
  if ((C & IMASK) == 0)
    sim_interval -= 3;
  A = SAM[C & BMASK];
  if (A & 0200) /* One's complement +/-177. */
    A |= 07600;
}

static void cpu_dis(void)
{
  memaddr(C & BMASK);
  memrd();
  cpu_index();
  sim_debug(DBG_CPU, &cpu_dev, "DIS α=%02o B=%04o A=%04o\n", S, B, A);
  dpy_dis(B >> 11, B & DMASK, A & DMASK);
}

static void cpu_xsk(void)
{
  memaddr(C & BMASK);
  memrd();
  cpu_index();
  pcinc(X(B) == 01777);
}

static void cpu_rol(void)
{
  while (C & BMASK) {
    if (C & IMASK) {
      A = (A << 1) | L;
      L = A >> 12;
    } else {
      A = (A << 1) | (A >> 11);
    }
    A &= WMASK;
    C--;
  }
}

static void cpu_ror(void)
{
  while (C & BMASK) {
    Z = (Z >> 1) | ((A & 1) << 11);
    if (C & IMASK) {
      A |= L << 12;
      L = A & 1;
      A = A >> 1;
    } else {
      A = (A >> 1) | (A << 11);
      A &= WMASK;
    }
    C--;
  }
}

static void cpu_scr(void)
{
  while (C & BMASK) {
    Z = (Z >> 1) | ((A & 1) << 11);
    if (C & IMASK)
      L = A & 1;
    A = (A & 04000) | (A >> 1);
    C--;
  }
}

int cpu_skip(void)
{
  int flag = 0;
  switch (C & 057) {
  case 000: case 001: case 002: case 003: case 004: case 005: case 006: case 007:
  case 010: case 011: case 012: case 013: //SXL
    flag = XL[C & BMASK];
    break;
  case 015: //KST
    flag = kbd_struck();
    break;
  case 040: case 041: case 042: case 043: case 044: case 045: //SNS
    flag = SSW & (1 << (C & 7));
    break;
  case 046: //PIN
    flag = PINFF;
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
  switch (C & BMASK) {
  case 000: case 001: case 002: case 003: case 004: case 005: case 006: case 007:
  case 010: case 011: case 012: case 013: case 014:
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
  //Instruction field.
}

static void cpu_umb(void)
{
  //Data field.
}

static void cpu_tape(void)
{
  memaddr(P);
  memrd();
  pcinc(1);
  tape_op();
}

static void cpu_lda(void)
{
  memrd();
  A = B;
}

static void cpu_sta(void)
{
  membuf(A);
  memwr();
}

static void cpu_ada(void)
{
  memrd();
  OVF = ~(A ^ B);
  A += B;
  A += A >> 12;
  A &= WMASK;
  OVF &= (A ^ B) & 04000;
}

static void cpu_adm(void)
{
  cpu_ada();
  membuf(A);
  memmd();
}

static void cpu_lam(void)
{
  memrd();
  A += L;
  L = A >> 12;
  A &= WMASK;
  A += B;
  if (A & 010000)
    L = 1;
  A &= WMASK;
  membuf(A);
  memmd();
}

static int32 sign_extend(uint16 x)
{
  if (x & 04000)
    return -(x ^ WMASK);
  else
    return x;
}

static void cpu_mul(void)
{
  int32 product;
  memrd();
  // C used for counting.
  product = sign_extend(A) * sign_extend(B);
  if (product == 0 && ((A ^ B) & HMASK))
    product = 037777777;
  else if (product < 0)
    product--;
  if (S & HMASK)
    A = (product >> 11) & WMASK;
  else if ((A ^ B) & HMASK)
    A = 04000 | (product & 03777);
  else
    A = product & 03777;
  L = A >> 11;
}

static void cpu_ldh(void)
{
  memrd();
  if ((S & HMASK) == 0)
    B >>= 6;
  A = B & RMASK;
}

static void cpu_sth(void)
{
  memrd();
  if (S & HMASK)
    membuf((A & RMASK) | (B & LMASK));
  else
    membuf((A << 6) | (B & RMASK));
  memmd();
}

static void cpu_shd(void)
{
  memrd();
  if ((S & HMASK) == 0)
    B >>= 6;
  pcinc((A & RMASK) != (B & RMASK));
}

static void cpu_sae(void)
{
  memrd();
  pcinc(A == B);
}

static void cpu_sro(void)
{
  memrd();
  pcinc((B & 1) == 0);
  membuf((B >> 1) | (B << 11));
  memmd();
}

static void cpu_bcl(void)
{
  memrd();
  A &= ~B;
}

static void cpu_bse(void)
{
  memrd();
  A |= B;
}

static void cpu_bco(void)
{
  memrd();
  A ^= B;
}

static void cpu_dsc(void)
{
  int i, j;
  uint16 data;

  memrd();
  data = B;

  // C used for counting.
  memaddr(1);
  memrd();
  sim_debug(DBG_CPU, &crt_dev, "DSC B=%04o A=%04o\n", B, A);
  for (j = 0; j < 2; j++) {
    membuf(B + 4);
    A &= 07740;
    for (i = 0; i < 6; i++) {
      if (data & 1)
        dpy_dis(B >> 11, B & DMASK, A & DMASK);
      data >>= 1;
      A += 4;
    }
  }
  memmd();
}

static void cpu_add(void)
{
  memaddr(X(C));
  cpu_ada();
}

static void cpu_stc(void)
{
  memaddr(X(C));
  membuf(A);
  A = 0;
  memwr();
}

static void cpu_jmp(void)
{
  uint16 tmp = P;
  P = X(C);
  if (P != 0) {
    membuf(06000 | tmp);
    memaddr(0);
    memwr();
  }
}

static void
cpu_insn(void)
{
  /* Cycle 0, or I. */
  memaddr(P);
  memrd();
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
  pcinc(1);

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
    pcinc(cpu_skip());
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

  /* Check for interrupts. */
  if ((C & 06000) != 06000 && C != 00010 && interrupt && interrupt_enable) {
    interrupt = interrupt_enable = 0;
    memaddr(021);
    memrd();
    C = B;
    cpu_jmp();
  }
}

t_stat sim_instr(void)
{
  t_stat stat;

  if ((stat = build_dev_tab()) != SCPE_OK)
    return stat;

  stop_reason = 0;
  paused = 0;

  for (;;) {
    AIO_CHECK_EVENT;
    if (sim_interval <= 0) {
      if ((stat = sim_process_event()) != SCPE_OK)
        return stat;
    }

    if (sim_brk_summ && sim_brk_test(P, SWMASK('E')))
      return STOP_IBKPT;

#if 0
    if (paused)
      sim_debug(DBG_CPU, &cpu_dev, "Paused\n");
#endif
    if (paused)
      sim_interval--;
    else
      cpu_insn();

    if (sim_step != 0) {
      if (--sim_step == 0)
        return SCPE_STEP;
    }

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
    if (stop_cpu) {                 /* Control-C (SIGINT) */
      stop_cpu = FALSE;
      break;                        /* abandon remaining output */
    }
    fprintf (st, "%04o %04o %04o %04o %04o %d  ",
             history[j].P,
             history[j].C,
             history[j].S,
             history[j].B,
             history[j].A,
             history[j].L);
    insn = history[j].C;
    fprint_sym (st, history[j].P, &insn, NULL, SWMASK ('M'));
    fputc ('\n', st);
    j = (j + 1) % history_m;
  }

  return SCPE_OK;
}

static t_bool pc_is_a_subroutine_call(t_addr **ret_addrs)
{
  return FALSE;
}

static t_stat
cpu_reset(DEVICE *dptr)
{
  sim_brk_types = SWMASK('E') | SWMASK('R') | SWMASK('W');
  sim_brk_dflt = SWMASK('E');
  sim_vm_is_subroutine_call = &pc_is_a_subroutine_call;
  return SCPE_OK;
}
