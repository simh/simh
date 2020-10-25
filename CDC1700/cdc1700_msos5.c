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

/* cdc1700_msos5.c: CDC1700 MSOS 5 trace and debugging support
 */

#include "cdc1700_defs.h"

extern uint16 M[], Preg, Areg, Qreg;

extern char INTprefix[];

extern uint16 doADDinternal(uint16, uint16);

/*
 * Information about monitor requests.
 */
#define RQ_SYSDIRREAD    0      /* System directory read (monitor only) */
#define RQ_READ          1      /* Normal read */
#define RQ_WRITE         2      /* Normal write */
#define RQ_STATUS        3      /* I/O request status */
#define RQ_FREAD         4      /* Formatted read */
#define RQ_EXIT          5      /* Unprotected exit */
#define RQ_FWRITE        6      /* Formatted write */
#define RQ_LOADER        7      /* Relocatable binary loader */
#define RQ_TIMER         8      /* Schedule program with delay */
#define RQ_SCHDLE        9      /* Schedule program */
#define RQ_SPACE        10      /* Allocate core */
#define RQ_CORE         11      /* Unprotected core bounds */
#define RQ_RELEAS       12      /* Release core */
#define RQ_GTFILE       13      /* Access permanent file in program library */
#define RQ_MOTION       14      /* Tape motion */
#define RQ_TIMPT1       15      /* Schedule directory program with delay */
#define RQ_INDIR        16      /* Indirect (use another parameter list) */
#define RQ_PTNCOR       17      /* Allocate partitioned core */
#define RQ_SYSCHD       18      /* Schedule directory program */
#define RQ_DIRCHD       19      /* Enable/Disable system directory scheduling */

/*
 * Masks for default fields in the  first parameter word.
 */
#define D               0x4000  /* Part 1 request indicator */
#define RQ              0x3E00  /* Request code */
#define X               0x0100  /* Relative/indirect indicator */
#define RP              0x00F0  /* Request priority */
#define CP              0x000F  /* Completion priority */

#define TELETYPE        0x04    /* Console TTY LU */

/*
 * Well-known locations within MSOS 5.
 */
#define LIBLU           0x00C2  /* Library LU */
#define CREXTB          0x00E9  /* Extended communications region */

#define LOG1A           28      /* Offset to LOG1A table address */

/*
 * Queueable requests have a completion address as the second parameter.
 * Note that INDIR requests may or may not be queueable depending on the
 * target parameter list.
 */
t_bool queueable[] = {
  TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, TRUE,
  TRUE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, FALSE
};

const char *indent[] = {
  "", " ", "  ", "   ", "    ", "     ", "      ", "       ", "        "
};

const char mode[] = { 'B', 'A' };
const char luchr[] = { ' ', 'R', 'I', '?' };
const char rel[] = { '0', '1' };
const char part1[] = { '0', '1' };
const char exitind[] = { '0', '1' };
const char units[] = {
  '0', '1', '2', '3', '?', '?', '?', '?',
  '?', '?', '?', '?', '?', '?', '?', '?'
};

const char *density[] = {
  "", "800 BPI", "556 BPI", "200 BPI", "1600 BPI", "???", "???", "???",
  "???", "???", "???", "???", "???", "???", "???", "???"
};

const char *action[] = {
  "", "BSR", "EOF", "REW", "UNL", "FSF", "BSF", "ADR",
  "???", "???", "???", "???", "???", "???", "???", "???"
};

uint32 seqno = 0;

#define END(s)  &s[strlen(s)]

/*
 * Character representation.
 */
const char *charRep[128] = {
  "<00>", "<01>", "<02?", "<03>", "<04>", "<05>", "<06>", "<07>",
  "<08>", "<09>", "<0A>", "<0B>", "<0C>", "<0D>", "<0E>", "<0F>",
  "<10>", "<11>", "<12>", "<13>", "<14>", "<15>", "<16>", "<17>",
  "<18>", "<19>", "<1A>", "<1B>", "<1C>", "<1D>", "<1E>", "<1F>",
  " ",  "!",  "\"",  "#",  "$",  "%",  "&",  "'",
  "(",  ")",  "*",  "+",  ",",  "-",  ".",  "/",
  "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",
  "8",  "9",  ":",  ";",  "<",  "=",  ">",  "?",
  "@",  "A",  "B",  "C",  "D",  "E",  "F",  "G",
  "H",  "I",  "J",  "K",  "L",  "M",  "N",  "O",
  "P",  "Q",  "R",  "S",  "T",  "U",  "V",  "W",
  "X",  "Y",  "Z",  "[",  "\\", "]",  "^",  "_",
  "<60>", "<61>", "<62>", "<63>", "<64>", "<65>", "<66>", "<67>",
  "<68>", "<69>", "<6A>", "<6B>", "<6C>", "<6D>", "<6E>", "<6F>",
  "<70>", "<71>", "<72>", "<73>", "<74>", "<75>", "<76>", "<77>",
  "<78>", "<79>", "<7A>", "<7B>", "<7C>", "<7D>", "<7E>", "<7F>",
};

/*
 * Check if a logical unit is a mass storage device.
 */
t_bool isMassStorage(uint16 lu)
{
  uint16 extbv4 = M[CREXTB];
  uint16 log1a = M[extbv4 + LOG1A];

  if ((lu > 0) && (lu <= M[log1a])) {
    uint16 physDev = M[log1a + lu];

    /*
     * Check if equipment class is 2 (Mass storage)
     */
    if ((M[physDev + 8] & 0x3800) == 0x1000)
      return TRUE;
  }
  return FALSE;
}

/*
 * Get the mass storage sector address associated with a read/write
 * request.
 */
uint32 getMSA(uint16 reqCode, uint16 param)
{
  if (reqCode == RQ_SYSDIRREAD)
    return M[param + 6];

  if ((M[param] & D) == 0) {
    uint16 sa = M[param + 5];

    if ((M[param] & X) == 0) {
      if ((sa & 0x8000) != 0) {
        sa &= 0x7FFF;
        return (M[sa + 1] << 15) | (M[sa + 2] & 0x7FFF);
      }
    } else {
      if ((sa & 0x8000) != 0) {
        sa = param + (sa & 0x7FFF);
        return (M[sa + 1] << 15) | (M[sa + 2] & 0x7FFF);
      }
    }
  }
  return (M[param + 6] << 15) | (M[param + 7] & 0x7FFF);
}

/*
 * Parameter conversion routines. Based on the assembly source code of MSOS 5
 * available on bitsavers.
 */

/*
 * Convert Logical Unit parameter to absolute value
 */
static uint16 luabs(uint16 param, uint16 lu, uint16 a)
{
  switch (a) {
    case ' ':
      break;

    case 'R':
      if ((lu & 0x200) != 0)
        lu |= 0xFC00;
      lu = doADDinternal(param, lu);
      break;

    case 'I':
      lu = M[lu];
      if ((lu & 0x8000) != 0)
        lu = doADDinternal(lu, 0x7FFF);
      break;

    case 3:
      lu = 0xFFFF;
      break;
  }
  return lu;
}

/*
 * Convert Starting Address parameter to absolute value
 */
static uint16 spabs(uint16 param)
{
  uint16 sa = M[param + 5];

  /*
   * If the D bit is set, the starting address must be absolute.
   */
  if ((M[param] & D) != 0)
    return sa;

  if ((M[param] & X) == 0) {
    if ((sa & 0x8000) != 0)
      sa = M[sa & 0x7FFF];
  } else {
    if ((sa & 0x8000) != 0)
      sa = param + (M[param + (sa & 0x7FFF)] & 0x7FFF);
    else sa = param + sa;
  }
  return sa;
}

/*
 * Convert Number of words to absolute value
 */
static uint16 npabs(uint16 param)
{
  uint16 nw = M[param + 4];

  /*
   * If the D bit is set, the number of words must be absolute.
   */
  if ((M[param] & D) != 0)
    return nw;

  if ((nw & 0x8000) != 0) {
    nw = nw + (((M[param] & X) != 0) ? (param & 0x7FFF) : 0);
    if ((nw & 0x8000) != 0)
      nw = doADDinternal(nw, 0x7FFF);
    nw = M[nw];
    if ((nw & 0x8000) != 0)
      nw = doADDinternal(nw, 0x7FFF);
  }
  return nw;
}

/*
 * Convert completion address to absolute value
 */
static char *cpabs(uint16 param, char *buf)
{
  uint16 ca = M[param + 1];

  /* 
   * Only absolutize the completion address if one is specified.
   */
  if (ca != 0) {
    /*
     * If the D bit is set, the completion address must be absolute.
     */
    if ((M[param] & D) == 0) {
      if ((ca & 0x8000) != 0) {
        /*
         * If negative, System directory reference
         */
        sprintf(buf, "SYSDIR(%u)", ca & 0x7FFF);
        return buf;
      } else {
        if ((M[param] & X) != 0) {
          if ((param & 0x8000) == 0)
            param = doADDinternal(param, 0x8000);
          ca = doADDinternal(ca, param);
        }
      }
      if ((ca & 0x8000) != 0)
        ca = doADDinternal(ca, 0x7FFF);
    }
  }
  sprintf(buf, "$%04X", ca);
  return buf;
}

/*
 * Describe motion parameters
 */
static void motion(uint16 param, char *d)
{
  uint16 commands = M[param + 4];

  if ((commands & 0xF) != 0)
    sprintf(END(d), "    Density   = %s\r\n", density[commands & 0xF]);

  if ((commands & 0x8000) == 0) {
    if ((commands & 0xF000) != 0) {
      sprintf(END(d), "    Actions   = %s", action[(commands & 0xF000) >> 12]);
      if ((commands & 0xF00) != 0) {
        sprintf(END(d), ",%s", action[(commands & 0xF00) >> 8]);
        if ((commands & 0xF0) != 0)
          sprintf(END(d), ",%s", action[(commands & 0xF0) >> 4]);
      }
      sprintf(END(d), "\r\n");
    }
  } else {
    sprintf(END(d), "    Repeat   = %s, %u times\r\n",
            action[(commands & 0x7000) >> 12], commands & 0xFFF);
  }
}

/*
 * Generate a test representation of a write to the console teletype. If the
 * text is too long to fix (> 50 chars) it will be truncated.
 */
#define MAXTEXT         50

char *textRep(uint16 start, uint16 len)
{
  int i;
  static char text[64];
  size_t text_space = sizeof (text) - 1;

  text[0] = '\0';

  for (i = 0; (i < (2 * len)) && (text_space >= MAXTEXT); i++) {
    uint16 ch = M[start];

    if ((i & 1) == 0)
      ch >>= 8;
    else start++;
    ch &= 0x7F;

    strncpy(&text[strlen(text)], charRep[ch], text_space);
    text_space -= strlen(charRep[ch]);
  }
  return text;
}

/*
 * Dump MSOS5 request information. 
 */
void MSOS5request(uint16 param, uint16 depth)
{
  uint16 reqCode = (M[param] & RQ) >> 9;
  char partOne = part1[(M[param] & D) >> 14];
  char relative = rel[(M[param] & X) >> 8];
  uint16 completion = M[param + 1];
  const char *request;
  char parameters[128], details[512];
  char luadr;
  uint16 lu, abslu, abss, abswd, i;
  uint32 sector;
  t_bool secondary = FALSE;

  parameters[0] = '\0';
  details[0] = '\0';

  if (depth == 0) {
    /*
     * Check for INDIR request with 15-bit addressing.
     */
    if ((M[param] & 0x8000) != 0) {
      fprintf(DBGOUT, "%sMSOS5(%06u): [RQ: $%04X]%sINDIR  $%04X,0\r\n",
              INTprefix, seqno++, param, indent[depth & 0x7],
              M[param] & 0x7FFF);
      MSOS5request(M[param] & 0x7FFF, depth + 1);
      return;
    }
  }

  if ((M[param] & 0x8000) != 0) {
    /*
     * Secondary scheduler call
     */
    secondary = TRUE;
    reqCode = RQ_SCHDLE;
  }
  /*
   * Check for invalid monitor requests
   */
  if (reqCode > RQ_DIRCHD) {
    fprintf(DBGOUT, "%sUnknown MSOS5 request (code %u)\r\n",
            INTprefix, reqCode);
    return;
  }

  if (queueable[reqCode]) {
    char temp[16];

    if (secondary)
      sprintf(details, "    Compl    = $%04X\r\n", M[param + 1]);
    else sprintf(details, "    Compl    = %s\r\n", cpabs(param, temp));
  }

  switch (reqCode) {
    case RQ_SYSDIRREAD:
      request = "*SYSDIRREAD*";
      goto rw;

    case RQ_READ:
      request = "READ";
      goto rw;

    case RQ_WRITE:
      request = "WRITE";
      goto rw;

    case RQ_STATUS:
      request = "STATUS";
      luadr = luchr[(M[param + 1] & 0xC00) >> 10];
      lu = M[param + 1] & 0x3FF;
      sprintf(parameters, "%u, 0, %c, 0, %c", lu, luadr, partOne);

      sprintf(END(details), "    LU       = %u\r\n", luabs(param, lu, luadr));
      break;

    case RQ_FREAD:
      request = "FREAD";
      goto rw;

    case RQ_EXIT:
      request = "EXIT";
      /* No parameters */
      break;

    case RQ_FWRITE:
      request = "FWRITE";
  rw:
      luadr = luchr[(M[param + 3] & 0xC00) >> 10];
      lu = M[param + 3] & 0x3FF;
      sprintf(parameters, "%u, $%04X, $%04X, %u, %c, %u, %u, %c, %c, %c",
              lu, completion, M[param + 5], M[param + 4],
              mode[(M[param + 3] & 0x1000) >> 12],
              (M[param] & RP) >> 4, M[param] & CP,
              luadr, relative, partOne);

      if (reqCode == RQ_SYSDIRREAD) {
        abslu = M[LIBLU];
        abss = completion;
      } else {
        abslu = luabs(param, lu, luadr);
        abss = spabs(param);
      }
      abswd = npabs(param);

      sprintf(END(details), "    LU       = %u\r\n", abslu);
      sprintf(END(details), "    Start    = $%04X\r\n", abss);
      sprintf(END(details), "    Words    = %u ($%04X)\r\n", abswd, abswd);

      if (isMassStorage(abslu))
        sprintf(END(details), "    MSA      = $%08X\r\n", getMSA(reqCode, param));

      /*
       * If this a write to the console teletype, generate a partial
       * representation of the text being written so that we can correlate
       * our current location with the output.
       */
      if (abslu == TELETYPE) {
        if ((M[param + 3] & 0x1000) != 0) {
          if ((reqCode == RQ_WRITE) || (reqCode == RQ_FWRITE)) {
            sprintf(END(details), "    Text     = %s\r\n",
                    textRep(spabs(param), npabs(param)));
          }
        }
      }
      break;

    case RQ_LOADER:
      request = "LOADER";
      sprintf(parameters, "[A: %04X, Q: %04X, lu: %u, t: %u, tna: %04X]",
              Areg, Qreg, (Areg & 0xFFF0) >> 4, Areg & 0xF, Qreg);
      break;

    case RQ_TIMER:
      request = "TIMER";
      sprintf(parameters, "$%04X, %u, %c, %u, %c, %c",
              M[param + 1], M[param] & 0xF,
              relative, M[param + 2],
              units[(M[param] & 0xF0) >> 4], partOne);
      break;

    case RQ_SCHDLE:
      request = secondary ? "Secondary SCHDLE" : "SCHDLE";
      sprintf(parameters, "$%04X, %u, %c, %c",
              M[param + 1], M[param] & CP, relative, partOne);
      break;

    case RQ_SPACE:
      request = "SPACE";
      sprintf(parameters, "%u, $%04X, %u, %u, %c, %c",
              M[param + 4], M[param + 1],
              (M[param] & RP) >> 4, M[param] & CP, relative, partOne);
      break;

    case RQ_CORE:
      request = "CORE";
      sprintf(parameters, "[A: %04X, Q: %04X]", Areg, Qreg);
      break;

    case RQ_RELEAS:
      request = "RELEAS";
      sprintf(parameters, "$%04X, %c, %c, %c",
              M[param + 1], exitind[M[param] & 0x01], relative, partOne);
      break;

    case RQ_GTFILE:
      request = "GTFILE";
      sprintf(parameters, "$%04X, $%04X, $%04X, $%04X, $%04X, %c, %u, %u, %c",
              M[param + 1], M[param + 7], M[param + 5],
              M[param + 4], M[param + 6], relative,
              (M[param] & RP) >> 4, M[param] & CP, partOne);

      /*
       * The reference manual does not correctly document the GTFILE request.
       * According to the MSOS 5.0 source code, there is a 10th parameter
       * which is used in calculating the address of the name block.
       */
      i = M[param + 7];
      if ((i & 0x8000) == 0) {
        i = doADDinternal(M[param + 10], i);
        if ((M[param] & D) == 0) {
          i = doADDinternal(i, 0x8000);
          i &= 0x7FFF;
        }
      } else i &= 0x7FFF;

      sector = (M[param + 8] << 16) | M[param + 9];

      if (sector != 0)
        sprintf(END(details), "    Sector   = %u\r\n", sector);
      else sprintf(END(details), "    Name     = %c%c%c%c%c%c\r\n",
                   (M[i] >> 8) & 0xFF, M[i] & 0xFF,
                   (M[i + 1] >> 8) & 0xFF, M[i + 1] & 0xFF,
                   (M[i + 2] >> 8) & 0xFF, M[i + 2] & 0xFF);
      break;

    case RQ_MOTION:
      request = "MOTION";
      luadr = luchr[(M[param + 3] & 0xC00) >> 10];
      lu = M[param + 3] & 0x3FF;
      sprintf(parameters, "%u, $%04X, %u, %u, %u, %u, %u, %u, %c, %c, %c, %c",
              lu, M[param + 1],
              (M[param + 4] & 0xF000) >> 12,
              (M[param + 4] & 0xF00) >> 8,
              (M[param + 4] & 0xF0) >> 4, M[param + 4] & 0xF,
              (M[param] & RP) >> 4, M[param] & CP,
              luadr, relative, partOne,
              mode[(M[param + 3] & 0x1000) >> 12]);

      sprintf(END(details), "    LU       = %u\r\n", luabs(param, lu, luadr));
      motion(param, details);
      break;

    case RQ_TIMPT1:
      request = "TIMPT1";
      sprintf(parameters, "$%04X, %u, 0, %u, %c",
              M[param + 1], M[param] & 0xF,
              M[param + 2], units[(M[param] & 0xF0) >> 4]);
      break;

    case RQ_INDIR:
      fprintf(DBGOUT, "%sMSOS5(%06u): [RQ: $%04X]%sINDIR  $%04X,1\r\n",
              INTprefix, seqno++, param, indent[depth & 0x7], M[param + 1]);
      MSOS5request(M[param + 1], depth + 1);
      return;

    case RQ_PTNCOR:
      request = "PTNCOR";
      sprintf(parameters, "%u, $%04X, %u, %u, %u, %c, %c",
              M[param + 4], M[param + 1], M[param + 5],
              (M[param] & RP) >> 4, M[param] & CP,
              relative, partOne);
      break;

    case RQ_SYSCHD:
      request = "SYSCHD";
      sprintf(parameters, "$%04X, %u",
              M[param + 1], M[param & 0xF]);
      break;

    case RQ_DIRCHD:
      switch (M[param] & 0xFF) {
        case 0:
          request = "ENSCHD";
          sprintf(parameters, "$%04X", M[param + 1]);
          break;

        case 0xFF:
          request = "DISCHD";
          sprintf(parameters, "$%04X", M[param + 1]);
          break;

        default:
          request = "SYSCHD";
          strcpy(parameters, "Invalid directory scheduling code");
          break;
      }
      break;

    default:
      request = "*Unknown*";
      sprintf(parameters, "Request code: %d", (M[param] & 0x3E00) >> 9);
      break;
  }
  fprintf(DBGOUT, "%sMSOS5(%06u): [RQ: $%04X]%s%s  %s\r\n",
          INTprefix, seqno++, param, indent[depth & 0x7], request, parameters);
  if (details[0] != '\0')
    fprintf(DBGOUT, "%s\r\n", details);
}
