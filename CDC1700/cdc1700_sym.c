/*

   Copyright (c) 2015-2016, John Forecast

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
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

/* cdc1700_sym.c: symbolic assembler input for "deposit" command
 */

#include "cdc1700_defs.h"
#include <ctype.h>

extern UNIT cpu_unit;

extern uint16 doADDinternal(uint16, uint16);

/*
 * Symbol tables
 */
#define I_DATA          0x10000         /* Data transmission */
#define I_ARITH         0x20000         /* Arithmetic */
#define I_LOG           0x30000         /* Logical */
#define I_JUMP          0x40000         /* Jumps */
#define I_REG           0x50000         /* Register reference */
#define I_SKIP          0x60000         /* Skip */
#define I_INTER         0x70000         /* Inter-register */
#define I_SHIFT         0x80000         /* Shift */
#define I_MASK          0xF0000

#define I_DMASK         0xFFFF

/*
 * Modifiers for I_REG addressing.
 */
#define I_NONE          0x000000        /* No argument expected */
#define I_REL           0x100000        /* 8-bit relative address */
#define I_ABS           0x200000        /* 8-bit absolute value */
#define I_SIGNED        0x300000        /* 8-bit signed value */
#define I_MASK2         0x300000

#define I_NOARG         I_REG + I_NONE

static const char *opcode[] = {
  "ADQ", "LDQ", "RAO", "LDA",
  "EOR", "AND", "SUB", "ADD",
  "SPA", "STA", "RTJ", "STQ",
  "DVI", "MUI", "JMP", "SLS",
  "SAZ", "SAN", "SAP", "SAM",
  "SQZ", "SQN", "SQP", "SQM",
  "SWS", "SWN", "SOV", "SNO",
  "SPE", "SNP", "SPF", "SNF",
  "INP", "OUT", "EIN", "IIN",
  "ECA", "DCA", "SPB", "CPB",
  "AAM", "AAQ", "AAB", "CLR",
  "TCM", "TCQ", "TCB", "TCA",
  "EAM", "EAQ", "EAB", "SET",
  "TRM", "TRQ", "TRB", "TRA",
  "LAM", "LAQ", "LAB", "CAM",
  "CAQ", "CAB", "INA", "ENA",
  "NOP", "ENQ", "INQ", "EXI",
  "QRS", "ARS", "LRS", "QLS", 
  "ALS", "LLS", "ECA", "DCA",
  NULL
};

static const int32 opc_val[] = {
  OPC_ADQ + I_ARITH, OPC_LDQ + I_DATA, OPC_RAO + I_ARITH, OPC_LDA + I_DATA,
  OPC_EOR + I_LOG, OPC_AND + I_LOG, OPC_SUB + I_ARITH, OPC_ADD + I_ARITH,
  OPC_SPA + I_DATA, OPC_STA + I_DATA, OPC_RTJ + I_JUMP, OPC_STQ + I_DATA,
  OPC_DVI + I_ARITH, OPC_MUI + I_ARITH, OPC_JMP + I_JUMP, OPC_SLS + I_NOARG,
  OPC_SAZ + I_SKIP, OPC_SAN + I_SKIP, OPC_SAP + I_SKIP, OPC_SAM + I_SKIP,
  OPC_SQZ + I_SKIP, OPC_SQN + I_SKIP, OPC_SQP + I_SKIP, OPC_SQM + I_SKIP,
  OPC_SWS + I_SKIP, OPC_SWN + I_SKIP, OPC_SOV + I_SKIP, OPC_SNO + I_SKIP,
  OPC_SPE + I_SKIP, OPC_SNP + I_SKIP, OPC_SPF + I_SKIP, OPC_SNF + I_SKIP,
  OPC_INP + I_REG + I_REL, OPC_OUT + I_REG + I_REL, OPC_EIN + I_NOARG, OPC_IIN + I_NOARG,
  OPC_ECA + I_NOARG, OPC_DCA + I_NOARG, OPC_SPB + I_NOARG, OPC_CPB + I_NOARG,
  OPC_AAM + I_INTER, OPC_AAQ + I_INTER, OPC_AAB + I_INTER, OPC_CLR + I_INTER,
  OPC_TCM + I_INTER, OPC_TCQ + I_INTER, OPC_TCB + I_INTER, OPC_TCA + I_INTER,
  OPC_EAM + I_INTER, OPC_EAQ + I_INTER, OPC_EAB + I_INTER, OPC_SET + I_INTER,
  OPC_TRM + I_INTER, OPC_TRQ + I_INTER, OPC_TRB + I_INTER, OPC_TRA + I_INTER,
  OPC_LAM + I_INTER, OPC_LAQ + I_INTER, OPC_LAB + I_INTER, OPC_CAM + I_INTER,
  OPC_CAQ + I_INTER, OPC_CAB + I_INTER, OPC_INA + I_REG + I_SIGNED, OPC_ENA + I_REG + I_SIGNED,
  OPC_NOP + I_NOARG, OPC_ENQ + I_REG + I_SIGNED, OPC_INQ + I_REG + I_SIGNED, OPC_EXI + I_REG + I_ABS,
  OPC_QRS + I_SHIFT, OPC_ARS + I_SHIFT, OPC_LRS + I_SHIFT, OPC_QLS + I_SHIFT,
  OPC_ALS + I_SHIFT, OPC_LLS + I_SHIFT, OPC_ECA + I_NOARG, OPC_DCA + I_NOARG
};

/*
 * Register (and pseudo-register) names.
 */
static const char *regname[] = {
  "A", "Q", "M", "I", "B",
  NULL
};

/*
 * Usage value for each usage type (0 means invalid).
 */
static uint16 instIndex[] = {
  0x0000, MOD_I1, 0x0000, MOD_I2, MOD_I1 | MOD_I2
};

static uint16 instInter[] = {
  MOD_D_A, MOD_D_Q, MOD_D_M, 0x0000, 0x0000
};

#define NEXTSYMBOL(mchar)            \
  cptr = get_glyph(cptr, gbuf, mchar); \
  for (j = 0; (regname[j] != NULL) && (strcmp(regname[j], gbuf) != 0); j++); \
  if (regname[j] == NULL) return SCPE_ARG

t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
  int32 i, j, l, rdx;
  t_bool neg;
  t_value temp;
  t_stat r;
  char gbuf[CBUFSIZE], mode;
  const char *cptr2;

  while (isspace(*cptr))
    cptr++;

  if ((sw & SWMASK('A')) || ((*cptr == '\'') && cptr++)) {
    /* ASCII character */
    if (cptr[0] == 0)
      return SCPE_ARG;
    val[0] = (t_value)cptr[0] | 0200;
    return SCPE_OK;
  }

  if ((sw & SWMASK('C')) || ((*cptr == '"') && cptr++)) {
    /* Packed ASCII characters (2 to a word) */
    if (cptr[0] == 0)

    val[0] = (((t_value)cptr[0] | 0200) << 8) | ((t_value)cptr[1] | 0200);
    return SCPE_OK;
  }

  cptr = get_glyph(cptr, gbuf, 0);
  l = strlen(gbuf);
  if ((gbuf[l - 1] == '*') || (gbuf[l - 1] == '-') || (gbuf[l - 1] == '+')) {
    mode = gbuf[l - 1];
    gbuf[l - 1] = '\0';
  } else mode = 0;

  for (i = 0; (opcode[i] != NULL) && (strcmp(opcode[i], gbuf) != 0); i++);
  if (opcode[i] == NULL)
    return SCPE_ARG;

  val[0] = opc_val[i] & I_DMASK;
  while (isspace(*cptr))
    cptr++;

  neg = FALSE;
  rdx = 10;

  switch (opc_val[i] & I_MASK) {
    case I_DATA:
    case I_ARITH:
    case I_LOG:
      if (*cptr == '=') {
        cptr++;

        if (*cptr == '-') {
          neg = TRUE;
          cptr++;
        }

        if (*cptr == '$') {
          rdx = 16;
          cptr++;
        }
        temp = get_uint(cptr, rdx, MAXNEG, &r);
        if (r != SCPE_OK)
          return r;
        if (neg) {
          if (temp > MAXPOS)
            return SCPE_ARG;
          temp = (~temp) & 0xFFFF;
        }

        if ((mode == '*') || (mode == '-'))
          return SCPE_ARG;

        /*
         * Constant addressing mode always occupies 2 words.
         */
        val[1] = temp;
        return -1;
      }
      /* FALLTHROUGH */

    case I_JUMP:
      if (*cptr == '(') {
        cptr++;

        if (*cptr == '$') {
          rdx = 16;
          cptr++;
        }
        temp = strtotv(cptr, &cptr2, rdx);
        if ((cptr == cptr2) || (*cptr2++ != ')'))
          return SCPE_ARG;
        cptr = (char *)cptr2;
        val[0] |= MOD_IN;
      } else {
        if (*cptr == '$') {
          rdx = 16;
          cptr++;
        }
        temp = strtotv(cptr, &cptr2, rdx);
        if (cptr == cptr2)
          return SCPE_ARG;
        cptr = (char *)cptr2;
      }

      if (mode == '*') {
        temp = doADDinternal(temp, ~addr);
        if (CANEXTEND8(temp))
          temp &= 0xFF;
        val[0] |= MOD_RE;
      }

      if ((mode == '-') && ((temp & 0xFF00) != 0))
        return SCPE_ARG;

      /*
       * Check for indexing modifier
       */
      if (*cptr++ == ',') {
        NEXTSYMBOL(0);
        if (instIndex[j] == 0)
          return SCPE_ARG;

        val[0] |= instIndex[j];
      }

      if (((temp & 0xFF00) != 0) || (mode == '+')) {
        val[1] = temp;
        return -1;
      }
      val[0] |= temp;
      return SCPE_OK;

    case I_REG:
      switch (opc_val[i] & I_MASK2) {
        case I_NONE:
          return SCPE_OK;

        case I_REL:
        case I_SIGNED:
          if (*cptr == '-') {
            neg = TRUE;
            cptr++;
          }
          if (*cptr == '$') {
            rdx = 16;
            cptr++;
          }
          temp = get_uint(cptr, rdx, 127, &r);
          if (r != SCPE_OK)
            return r;
          if (neg)
            temp = (~temp) & 0xFF;
          val[0] |= temp;
          return SCPE_OK;

        case I_ABS:
          if (*cptr == '$') {
            rdx = 16;
            cptr++;
          }
          temp = get_uint(cptr, rdx, 255, &r);
          if (r != SCPE_OK)
            return r;
          val[0] |= temp;
          return SCPE_OK;
      }
      break;

    case I_SKIP:
      if (*cptr == '$') {
        rdx = 16;
        cptr++;
      }
      temp = get_uint(cptr, rdx, 15, &r);
      if (r != SCPE_OK)
        return r;
      val[0] |= temp;
      return SCPE_OK;

    case I_INTER:
      if (*cptr != 0) {
        do {
          NEXTSYMBOL(',');
          if (instInter[j] == 0)
            return SCPE_ARG;
          val[0] |= instInter[j];
        } while (*cptr != 0);
      }
      return SCPE_OK;

    case I_SHIFT:
      if (*cptr == '$') {
        rdx = 16;
        cptr++;
      }
      temp = get_uint(cptr, rdx, 31, &r);
      if (r != SCPE_OK)
        return r;
      val[0] |= temp;
      return SCPE_OK;
  }
  return SCPE_ARG;
}
