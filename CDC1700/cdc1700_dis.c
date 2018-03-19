/*

   Copyright (c) 2015-2017, John Forecast

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
 * Enhanced instruction set mnemonics
 */
char enhRegChar[] = {
  ' ', '1', '2', '3', '4', 'Q', 'A', 'I'
};

const char *enhIdxName[] = {
  "", ",1", ",2", ",3", ",4", ",Q", ",A", ",I"
};

char enhSkipType[] = {
  'Z', 'N', 'P', 'M'
};

char enhSkipReg[] = {
  '4', '1', '2', '3'
};

const char *enhMiscName0[] = {
  "???", "LMM", "LRG", "SRG", "SIO", "SPS", "DMI", "CBP",
  "GPE", "GPO", "ASC", "APM", "PM0", "PM1"
};
#define ENH_MAXMISC0    0xD

const char *enhMiscName1[] = {
  "LUB", "LLB", "EMS", "WPR", "RPR", "ECC"
};
#define ENH_MAXMISC1    0x5

const char *enhFldName[] = {
  "???", "???", "SFZ", "SFN", "LFA", "SFA", "CLF", "SEF"
};

/*
 * Generate a single line of text for an instruction. Format is:
 *
 * c xxxx yyyy zzzz     <instr>       <targ>
 *
 * or if enhanced instruction set is enabled
 *
 * c xxxx yyyy zzzz aaaa    <instr>       <targ>
 *
 * where:
 *
 *       |P     Normal/Protected location
 *      xxxx    Memory address of instruction in hex
 *      yyyy    First word of instruction in hex
 *      zzzz    Second word of instruction in hex, replaced by spaces if
 *              not present
 *      aaaa    Third word of instruction in hex, replaced by spaces if
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
  char optional[8], optional2[8], temp[8], decoded[64], enhInstr[8];
  const char *mode, *spc, *shift, *inter, *dest;
  uint16 instr = LoadFromMem(addr);
  uint16 instr2, enhMode;
  uint16 delta = instr & OPC_ADDRMASK;
  uint8 more = 0, isconst = 0, enhRB;
  uint16 t;
  t_bool enhValid = FALSE, enhChar = FALSE;

  strcpy(optional, "    ");
  strcpy(optional2, "    ");
  strcpy(decoded, "UNDEF");

  if ((instr & OPC_MASK) != 0) {
    if ((instr & MOD_RE) == 0)
      mode = delta == 0 ? "+    " : "-    ";
    else mode = "*    ";

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
          isconst = 1;
        break;
    }

    if (delta == 0) {
      consumed++;
      sprintf(optional, "%04X", LoadFromMem(addr + 1));
      sprintf(temp, "$%04X", LoadFromMem(addr + 1));
    } else sprintf(temp, "$%02X", delta);

    sprintf(decoded, "%s%s%s%s%s%s%s",
            opName[(instr & OPC_MASK) >> 12],
            mode,
            isconst ? "=" : "",
            (instr & MOD_IN) != 0 ? "(" : "",
            temp,
            (instr & MOD_IN) != 0 ? ")" : "",
            idxName[(instr & (MOD_I1 | MOD_I2)) >> 8]);
  } else {
    spc = spcName[(instr & OPC_SPECIALMASK) >> 8];

    switch (instr & OPC_SPECIALMASK) {
      case OPC_IIN:
      case OPC_EIN:
      case OPC_SPB:
      case OPC_CPB:
        switch (INSTR_SET) {
          case INSTR_ORIGINAL:
            /*
             * Character addressing enable/disable is only available as
             * an extension to the original set.
             */
            switch (instr) {
              case OPC_ECA:
                sprintf(decoded, "%s", "ECA");
                break;

              case OPC_DCA:
                sprintf(decoded, "%s", "DCA");
                break;

              default:
                sprintf(decoded, "%s", spc);
                break;
            }
            break;

          case INSTR_BASIC:
            if (delta == 0) {
              sprintf(decoded, "%s", spc);
            } else {
              sprintf(decoded, "%s", "NOP  [ Possible enhanced instruction");
            }
            break;

          case INSTR_ENHANCED:
            if (delta != 0) {
              switch (instr & OPC_SPECIALMASK) {
                case OPC_IIN:
                  if (((instr & OPC_FLDF3A) != OPC_FLDRSV1) &&
                      ((instr & OPC_FLDF3A) != OPC_FLDRSV2)) {
                    instr2 = LoadFromMem(addr + 1);
                    delta = instr2 & OPC_ADDRMASK;

                    if (((instr2 & OPC_FLDSTR) - (instr2 & OPC_FLDLTH)) >= 0) {
                      consumed++;

                      if ((instr & MOD_ENHRE) == 0)
                        mode = delta == 0 ? "+    " : "-    ";
                      else mode = "*    ";

                      sprintf(optional, "%04X", instr2);
                      if (delta == 0) {
                        consumed++;
                        sprintf(optional2, "%04X", LoadFromMem(addr + 2));
                        sprintf(temp, "$%04X", LoadFromMem(addr + 2));
                      } else sprintf(temp, "$%02X", delta);

                      sprintf(decoded, "%s%s%s%s%s,%d,%d%s",
                              enhFldName[instr & OPC_FLDF3A],
                              mode,
                              (instr & MOD_ENHIN) != 0 ? "(" : "",
                              temp,
                              (instr & MOD_ENHIN) != 0 ? ")" : "",
                              (instr2 & OPC_FLDSTR) >> 12,
                              ((instr2 & OPC_FLDLTH) >> 8) + 1,
                              enhIdxName[(instr & MOD_ENHRA) >> 3]);
                      break;
                    }
                  }
                  strcpy(decoded, "UNDEF");
                  targ = FALSE;
                  break;

                case OPC_EIN:
                  instr2 = LoadFromMem(addr + 1);
                  enhMode = (instr2 & OPC_ENHF5) >> 8;
                  enhRB = instr & MOD_ENHRB;

                  switch (instr2 & OPC_ENHF4) {
                    case OPC_STOSJMP:
                      if (enhMode == 0) {
                        enhValid = TRUE;
                        if (enhRB == REG_NOREG)
                          strcpy(enhInstr, "SJE");
                        else sprintf(enhInstr, "SJ%c", enhRegChar[enhRB]);
                      }
                      break;

                    case OPC_STOADD:
                      if ((enhMode == 0) && (enhRB != REG_NOREG)) {
                        enhValid = TRUE;
                        sprintf(enhInstr, "AR%c", enhRegChar[enhRB]);
                      }
                      break;

                    case OPC_STOSUB:
                      if ((enhMode == 0) && (enhRB != REG_NOREG)) {
                        enhValid = TRUE;
                        sprintf(enhInstr, "SB%c", enhRegChar[enhRB]);
                      }
                      break;

                    case OPC_STOAND:
                      if (enhRB != REG_NOREG)
                        switch (enhMode) {
                          case WORD_REG:
                            enhValid = TRUE;
                            sprintf(enhInstr, "AN%c", enhRegChar[enhRB]);
                            break;

                          case WORD_MEM:
                            enhValid = TRUE;
                            sprintf(enhInstr, "AM%c", enhRegChar[enhRB]);
                            break;
                        }
                      break;

                    case OPC_STOLOADST:
                      switch (enhMode) {
                        case WORD_REG:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            sprintf(enhInstr, "LR%c", enhRegChar[enhRB]);
                          }
                          break;

                        case WORD_MEM:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            sprintf(enhInstr, "SR%c", enhRegChar[enhRB]);
                          }
                          break;

                        case CHAR_REG:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            enhChar = TRUE;
                            strcpy(enhInstr, "LCA");
                          }
                          break;

                        case CHAR_MEM:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            enhChar = TRUE;
                            strcpy(enhInstr, "SCA");
                          }
                          break;
                      }
                      break;

                    case OPC_STOOR:
                      if (enhRB != REG_NOREG)
                        switch (enhMode) {
                          case WORD_REG:
                            enhValid = TRUE;
                            sprintf(enhInstr, "OR%c", enhRegChar[enhRB]);
                            break;

                          case WORD_MEM:
                            enhValid = TRUE;
                            sprintf(enhInstr, "OM%c", enhRegChar[enhRB]);
                            break;
                        }
                      break;

                    case OPC_STOCRE:
                      switch (enhMode) {
                        case WORD_REG:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            sprintf(enhInstr, "C%cE", enhRegChar[enhRB]);
                          }
                          break;

                        case CHAR_REG:
                          if (enhRB != REG_NOREG) {
                            enhValid = TRUE;
                            enhChar = TRUE;
                            strcpy(enhInstr, "CCE");
                          }
                          break;
                      }
                      break;
                  }
                  if (enhValid) {
                    delta = instr2 & OPC_ADDRMASK;
                    consumed++;

                    if ((instr & MOD_ENHRE) == 0)
                      mode = delta == 0 ? "+    " : "-    ";
                    else mode = "*    ";

                    sprintf(optional, "%04X", instr2);
                    if (delta == 0) {
                      consumed++;
                      sprintf(optional2, "%04X", LoadFromMem(addr + 2));
                      sprintf(temp, "$%04X", LoadFromMem(addr + 2));
                    } else sprintf(temp, "$%02X", delta);

                    if (!enhChar) {
                      if ((delta == 0) &&
                          (instr & (MOD_ENHRE | MOD_ENHIN)) == 0)
                        isconst = 1;

                      sprintf(decoded, "%s%s%s%s%s%s%s",
                              enhInstr,
                              mode,
                              isconst ? "=" : "",
                              (instr & MOD_ENHIN) != 0 ? "(" : "",
                              temp,
                              (instr & MOD_ENHIN) != 0 ? ")" : "",
                              enhIdxName[(instr & MOD_ENHRA) >> 3]);
                    } else {
                      sprintf(decoded, "%s%s%s%s%s%s%s",
                              enhInstr,
                              mode,
                              (instr & MOD_ENHIN) != 0 ? "(" : "",
                              temp,
                              (instr & MOD_ENHIN) != 0 ? ")" : "",
                              enhIdxName[enhRB],
                              enhIdxName[(instr & MOD_ENHRA) >> 3]);
                    }
                  } else {
                    strcpy(decoded, "UNDEF");
                    targ = FALSE;
                  }
                  break;

                case OPC_SPB:
                  if ((instr & OPC_DRPMBZ) == 0) {
                    char reg = enhRegChar[(instr & OPC_DRPRA) >> 5];
                    uint8 sk = instr & OPC_DRPSK;

                    sprintf(decoded, "D%cP     $%1X", reg, sk);
                    break;
                  }
                  break;

                case OPC_CPB:
                  if ((instr & OPC_ENHXFRF2A) == 0) {
                    if ((instr & (OPC_ENHXFRRA | OPC_ENHXFRRB)) != 0) {
                      char ra = enhRegChar[(instr & OPC_ENHXFRRA) >> 5];
                      char rb = enhRegChar[instr & OPC_ENHXFRRB];

                      sprintf(decoded, "XF%c     %c", ra, rb);
                      break;
                    }
                  }
                  strcpy(decoded, "UNDEF");
                  targ = FALSE;
                  break;
              }
            } else {
              sprintf(decoded, "%s", spc);
              targ = FALSE;
            }
            break;
        }
        break;

      case OPC_NOP:
        switch (INSTR_SET) {
          case INSTR_ORIGINAL:
            sprintf(decoded, "%s", spc);
            break;

          case INSTR_BASIC:
            if (delta != 0) {
              sprintf(decoded, "%s", "NOP  [ Possible enhanced instruction");
            } else sprintf(decoded, "%s", spc);
            break;

          case INSTR_ENHANCED:
            if (delta != 0) {
              uint8 miscFN = instr & OPC_MISCF3;
              char reg = enhRegChar[(instr & OPC_MISCRA) >> 5];

              targ = FALSE;
              if ((instr & OPC_MISCRA) == 0) {
                if (miscFN <= ENH_MAXMISC0)
                  spc = enhMiscName0[miscFN];
                else spc = "UNDEF";
                sprintf(decoded, "%s", spc);
              } else {
                if (miscFN <= ENH_MAXMISC1) {
                  spc = enhMiscName1[miscFN];
                  sprintf(decoded, "%s     %c", spc, reg);
                } else strcpy(decoded, "UNDEF");
              }
            } else sprintf(decoded, "%s", spc);
            break;
        }
        break;

      case OPC_EXI:
        sprintf(decoded, "%s     $%02X", spc, delta);
        break;

      case OPC_SKIPS:
        sprintf(decoded, "%s     $%01X",
                skpName[(instr & OPC_SKIPMASK) >> 4], instr & OPC_SKIPCOUNT);
        break;

      case OPC_SLS:
        if (delta != 0) {
          switch (INSTR_SET) {
            case INSTR_BASIC:
              sprintf(decoded, "%s", "NOP  [ Possible enhanced instruction");
              break;

            case INSTR_ORIGINAL:
              sprintf(decoded, "%s     $%02X", spc, delta);
              break;

            case INSTR_ENHANCED:
              {
                char reg = enhSkipReg[(instr & OPC_ENHSKIPREG) >> 6];
                char type = enhSkipType[(instr & OPC_ENHSKIPTY) >> 4];
                uint8 sk = instr & OPC_ENHSKIPCNT;

                sprintf(decoded, "S%c%c     $%1X", reg, type, sk);
              }
              break;
          }
          break;
        }

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
    if (INSTR_SET == INSTR_ENHANCED)
      sprintf(buf, "%c %04X %04X %s %s         %s",
              prot, addr, instr, optional, optional2, decoded);
    else
      sprintf(buf, "%c %04X %04X %s         %s",
              prot, addr, instr, optional, decoded);
  } else {
    if (INSTR_SET == INSTR_ENHANCED)
      sprintf(buf, "%c %04X %s %s               %s",
              prot, instr, optional, optional2, decoded);
    else
      sprintf(buf, "%c %04X %s               %s",
              prot, instr, optional, decoded);
  }

  if (targ) {
    const char *rel = "";
    t_bool indJmp = FALSE;
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
            taddr = doADDinternal(addr, EXTEND8(instr & OPC_MODMASK));
            taddr2 = taddr;
            if ((sim_switches & SWMASK('R')) != 0)
              taddr2 -= RelBase;
            more = 1;
            break;

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
        switch (more) {
          case 1:
            sprintf(buf, "[ => %04X%s %s {%04X}", taddr2, rel,
                    P[MEMADDR(taddr)] ? "(P)" : "",
                    LoadFromMem(taddr));
            break;

          case 2:
            sprintf(buf, "[ => %04X%s (B:%04X%s) %s {%04X}", 
                    taddr2, rel, base, rel,
                    P[MEMADDR(taddr)] ? "(P)" : "",
                    LoadFromMem(taddr));
            break;
        }
      }
    }
  }
  return consumed;
}
