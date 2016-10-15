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

/* cdc1700_dis.c: CDC1700 disassembler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdc1700_defs.h"

extern uint16 Areg, Mreg, Preg, Qreg, RelBase;
extern uint16 LoadFromMem(uint16);
extern uint8 P[];
extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern t_stat disEffectiveAddr(uint16, uint16, uint16 *, uint16 *);
extern uint16 doADDinternal(uint16, uint16);

const char *opName[] = {
  "???", "JMP", "MUI", "DVI", "STQ", "RTJ", "STA", "SPA",
  "ADD", "SUB", "AND", "EOR", "LDA", "RAO", "LDQ", "ADQ"
};

const char *idxName[] = {
  "", ",I", ",Q", ",B"
};

const char *spcName[] = {
  "SLS", "???", "INP", "OUT", "EIN", "IIN", "SPB", "CPB",
  "???", "INA", "ENA", "NOP", "ENQ", "INQ", "EXI", "???"
};

const char *skpName[] = {
  "SAZ", "SAN", "SAP", "SAM", "SQZ", "SQN", "SQP", "SQM",
  "SWS", "SWN", "SOV", "SNO", "SPE", "SNP", "SPF", "SNF"
};

const char *interName[] = {
  "SET", "TRM", "TRQ", "TRB", "TRA", "AAM", "AAQ", "AAB",
  "CLR", "TCM", "TCQ", "TCB", "TCA", "EAM", "EAQ", "EAB",
  "SET", "TRM", "TRQ", "TRB", "TRA", "LAM", "LAQ", "LAB",
  "NOOP", NULL, NULL, NULL, NULL, "CAM", "CAQ", "CAB"
};

const char *destName[] = {
  "", "M", "Q", "Q,M", "A", "A,M", "A,Q", "A,Q,M"
};


const char *shiftName[] = {
  NULL, "QRS", "ARS", "LRS", NULL, "QLS", "ALS", "LLS"
};

/*
 * Generate a single line of text for an instruction. Format is:
 *
 * c xxxx yyyy zzzz     <instr>       <targ>
 *
 * where:
 *
 *       |P     Normal/Protected location
 *      xxxx    Memory address of instruction in hex
 *      yyyy    First word of instruction in hex
 *      zzzz    Second word of inctruction in hex, replaced by spaces if
 *              not present
 *      <instr> Disassmbled instruction
 *      <targ>  Optional target address and contents
 *
 * Returns:
 *      # of words consumed by the instruction
 */

int disassem(char *buf, uint16 addr, t_bool dbg, t_bool targ, t_bool exec)
{
  int consumed = 1;
  char prot = ISPROTECTED(addr) ? 'P' : ' ';
  char mode[8], optional[8], temp[8], decoded[64];
  const char *spc, *shift, *inter, *dest;
  uint16 instr = LoadFromMem(addr);
  uint16 delta = instr & OPC_ADDRMASK;
  uint16 t;

  strcpy(optional, "    ");
  strcpy(decoded, "UNDEF");

  if ((instr & OPC_MASK) != 0) {
    strcpy(decoded, opName[(instr & OPC_MASK) >> 12]);

    if ((instr & MOD_RE) == 0)
      strcpy(mode, delta == 0 ? "+    " : "-    ");
    else strcpy(mode, "*    ");

    switch (instr & OPC_MASK) {
      case OPC_ADQ:
      case OPC_LDQ:
      case OPC_LDA:
      case OPC_EOR:
      case OPC_AND:
      case OPC_SUB:
      case OPC_ADD:
      case OPC_DVI:
      case OPC_MUI:
        if (ISCONSTANT(instr))
          strcat(mode, "=");
        break;
    }

    if (delta == 0) {
      consumed++;
      sprintf(optional, "%04X", LoadFromMem(addr + 1));
      sprintf(temp, "$%04X", LoadFromMem(addr + 1));
    } else sprintf(temp, "$%02X", delta);

    strcat(decoded, mode);
    if ((instr & MOD_IN) != 0)
      strcat(decoded, "(");
    strcat(decoded, temp);
    if ((instr & MOD_IN) != 0)
      strcat(decoded, ")");

    strcat(decoded, idxName[(instr & (MOD_I1 | MOD_I2)) >> 8]);
  } else {
    spc = spcName[(instr & OPC_SPECIALMASK) >> 8];

    switch (instr & OPC_SPECIALMASK) {
      case OPC_EIN:
      case OPC_IIN:
      case OPC_SPB:
      case OPC_CPB:
      case OPC_NOP:
        sprintf(decoded, "%s", spc);
        break;

      case OPC_EXI:
        sprintf(decoded, "%s     $%02X", spc, instr & OPC_MODMASK);
        break;

      case OPC_SKIPS:
        sprintf(decoded, "%s     $%01X",
                skpName[(instr & OPC_SKIPMASK) >> 4], instr & OPC_SKIPCOUNT);
        break;

      case OPC_SLS:
      case OPC_INP:
      case OPC_OUT:
      case OPC_INA:
      case OPC_ENA:
      case OPC_ENQ:
      case OPC_INQ:
        sprintf(decoded, "%s     $%02X", spc, delta);
        break;

      case OPC_INTER:
        t = instr & (MOD_LP | MOD_XR | MOD_O_A | MOD_O_Q | MOD_O_M);
        inter = interName[t >> 3];
        dest = destName[instr & (MOD_D_A | MOD_D_Q | MOD_D_M)];
        if (inter != NULL)
          sprintf(decoded, "%s     %s", inter, dest);
        break;

      case OPC_SHIFTS:
        shift = shiftName[(instr & OPC_SHIFTMASK) >> 5];
        if (shift != NULL)
          sprintf(decoded, "%s     $%X", shift, instr & OPC_SHIFTCOUNT);
        break;
    }
  }

  if (dbg) {
    sprintf(buf, "%c %04X %04X %s         %s",
            prot, addr, instr, optional, decoded);
  } else {
    sprintf(buf, "%c %04X %s               %s",
            prot, instr, optional, decoded);
  }
  if (targ) {
    const char *rel = "";
    t_bool indJmp = FALSE;
    uint8 more = 0;
    uint16 taddr, taddr2,  base;

    switch (instr & OPC_MASK) {
      case OPC_ADQ:
      case OPC_LDQ:
      case OPC_RAO:
      case OPC_LDA:
      case OPC_EOR:
      case OPC_AND:
      case OPC_SUB:
      case OPC_ADD:
      case OPC_SPA:
      case OPC_STA:
      case OPC_STQ:
      case OPC_DVI:
      case OPC_MUI:
        if (((instr & (MOD_IN | MOD_I1 | MOD_I2)) == 0) || exec)
          if (disEffectiveAddr(addr, instr, &base, &taddr) == SCPE_OK) {
            more = cpu_dev.dctrl & DBG_FULL ? 2 : 1;
            taddr2 = taddr;
            if (((instr & (MOD_RE | MOD_IN | MOD_I1 | MOD_I2)) == MOD_RE) &&
                ((sim_switches & SWMASK('R')) != 0)) {
              taddr2 -= RelBase;
              rel = "*";
            }
          }
        break;

      case OPC_JMP:
        if (((instr & (MOD_IN | MOD_I1 | MOD_I2)) == MOD_IN) & !dbg) {
          if (disEffectiveAddr(addr, instr & ~MOD_IN, &base, &taddr) == SCPE_OK) {
            taddr2 = taddr;
            indJmp = TRUE;
            if (((instr & MOD_RE) != 0) && ((sim_switches & SWMASK('R')) != 0)) {
              taddr2 -= RelBase;
              rel = "*";
            }
            break;
          }
        }
        /* FALLTHROUGH */

      case OPC_RTJ:
        if (((instr & (MOD_IN | MOD_I1 | MOD_I2)) != 0) & !dbg)
          break;

        if (disEffectiveAddr(addr, instr, &base, &taddr) == SCPE_OK) {
          more = cpu_dev.dctrl & DBG_FULL ? 2 : 1;
          taddr2 = taddr;
          if (((instr & (MOD_RE | MOD_IN | MOD_I1 | MOD_I2)) == MOD_RE) &&
              ((sim_switches & SWMASK('R')) != 0)) {
            taddr2 -= RelBase;
            rel = "*";
          }
        }
        break;

      case OPC_SPECIAL:
        switch (instr & OPC_SPECIALMASK) {
          case OPC_SLS:
            break;

          case OPC_SKIPS:
            taddr = doADDinternal(MEMADDR(addr + 1), instr & OPC_SKIPCOUNT);
            taddr2 = taddr;
            if ((sim_switches & SWMASK('R')) != 0)
              taddr2 -= RelBase;
            more = 1;
            break;

          case OPC_SPB:
          case OPC_CPB:
            if (exec) {
              taddr = Qreg;
              taddr2 = taddr;
              if ((sim_switches & SWMASK('R')) != 0)
                taddr2 -= RelBase;
              more = 1;
            }
            break;

          case OPC_INP:
          case OPC_OUT:
          case OPC_EIN:
          case OPC_IIN:
          case OPC_INTER:
          case OPC_INA:
          case OPC_ENA:
          case OPC_NOP:
          case OPC_ENQ:
          case OPC_INQ:
          case OPC_EXI:
          case OPC_SHIFTS:
            break;
        }
        break;
    }

    if (more || indJmp) {
      int i, count = 48 - strlen(buf);

      for (i = 0; i < count; i++)
        strcat(buf, " ");

      buf += strlen(buf);
      if (indJmp) {
        sprintf(buf, "[ => (%04X%s)", taddr2, rel);
      } else {
        if (more == 1)
          sprintf(buf, "[ => %04X%s %s {%04X}", taddr2, rel,
                  P[MEMADDR(taddr)] ? "(P)" : "",
                  LoadFromMem(taddr));
        else sprintf(buf, "[ => %04X%s (B:%04X%s) %s {%04X}", 
                     taddr2, rel, base, rel,
                     P[MEMADDR(taddr)] ? "(P)" : "",
                     LoadFromMem(taddr));
      }
    }
  }
  return consumed;
}
