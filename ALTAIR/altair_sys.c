/* altair_sys.c: MITS Altair system interface

   Copyright (c) 1997-2005, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.
*/

#include <ctype.h>
#include "altair_defs.h"

extern DEVICE cpu_dev;
extern DEVICE dsk_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern DEVICE sio_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE lpt_dev;
extern unsigned char M[];
extern int32 saved_PC;

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "Altair 8800";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &sio_dev,
    &ptr_dev,
    &ptp_dev,
    &dsk_dev,
    NULL
};

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Unknown I/O Instruction",
    "HALT instruction",
    "Breakpoint",
    "Invalid Opcode"
};

static const char *opcode[] = {
"NOP", "LXI B", "STAX B", "INX B",                      /* 000-003 */
"INR B", "DCR B", "MVI B", "RLC",                       /* 004-007 */
"???", "DAD B", "LDAX B", "DCX B",                      /* 010-013 */
"INR C", "DCR C", "MVI C", "RRC",                       /* 014-017 */
"???", "LXI D", "STAX D", "INX D",                      /* 020-023 */
"INR D", "DCR D", "MVI D", "RAL",                       /* 024-027 */
"???", "DAD D", "LDAX D", "DCX D",                      /* 030-033 */
"INR E", "DCR E", "MVI E", "RAR",                       /* 034-037 */
"???", "LXI H", "SHLD", "INX H",                        /* 040-043 */
"INR H", "DCR H", "MVI H", "DAA",                       /* 044-047 */
"???", "DAD H", "LHLD", "DCX H",                        /* 050-053 */
"INR L", "DCR L", "MVI L", "CMA",                       /* 054-057 */
"???", "LXI SP", "STA", "INX SP",                       /* 060-063 */
"INR M", "DCR M", "MVI M", "STC",                       /* 064-067 */
"???", "DAD SP", "LDA", "DCX SP",                       /* 070-073 */
"INR A", "DCR A", "MVI A", "CMC",                       /* 074-077 */
"MOV B,B", "MOV B,C", "MOV B,D", "MOV B,E",             /* 100-103 */
"MOV B,H", "MOV B,L", "MOV B,M", "MOV B,A",             /* 104-107 */
"MOV C,B", "MOV C,C", "MOV C,D", "MOV C,E",             /* 110-113 */
"MOV C,H", "MOV C,L", "MOV C,M", "MOV C,A",             /* 114-117 */
"MOV D,B", "MOV D,C", "MOV D,D", "MOV D,E",             /* 120-123 */
"MOV D,H", "MOV D,L", "MOV D,M", "MOV D,A",             /* 124-127 */
"MOV E,B", "MOV E,C", "MOV E,D", "MOV E,E",             /* 130-133 */
"MOV E,H", "MOV E,L", "MOV E,M", "MOV E,A",             /* 134-137 */
"MOV H,B", "MOV H,C", "MOV H,D", "MOV H,E",             /* 140-143 */
"MOV H,H", "MOV H,L", "MOV H,M", "MOV H,A",             /* 144-147 */
"MOV L,B", "MOV L,C", "MOV L,D", "MOV L,E",             /* 150-153 */
"MOV L,H", "MOV L,L", "MOV L,M", "MOV L,A",             /* 154-157 */
"MOV M,B", "MOV M,C", "MOV M,D", "MOV M,E",             /* 160-163 */
"MOV M,H", "MOV M,L", "HLT", "MOV M,A",                 /* 164-167 */
"MOV A,B", "MOV A,C", "MOV A,D", "MOV A,E",             /* 170-173 */
"MOV A,H", "MOV A,L", "MOV A,M", "MOV A,A",             /* 174-177 */
"ADD B", "ADD C", "ADD D", "ADD E",                     /* 200-203 */
"ADD H", "ADD L", "ADD M", "ADD A",                     /* 204-207 */
"ADC B", "ADC C", "ADC D", "ADC E",                     /* 210-213 */
"ADC H", "ADC L", "ADC M", "ADC A",                     /* 214-217 */
"SUB B", "SUB C", "SUB D", "SUB E",                     /* 220-223 */
"SUB H", "SUB L", "SUB M", "SUB A",                     /* 224-227 */
"SBB B", "SBB C", "SBB D", "SBB E",                     /* 230-233 */
"SBB H", "SBB L", "SBB M", "SBB A",                     /* 234-237 */
"ANA B", "ANA C", "ANA D", "ANA E",                     /* 240-243 */
"ANA H", "ANA L", "ANA M", "ANA A",                     /* 244-247 */
"XRA B", "XRA C", "XRA D", "XRA E",                     /* 250-253 */
"XRA H", "XRA L", "XRA M", "XRA A",                     /* 254-257 */
"ORA B", "ORA C", "ORA D", "ORA E",                     /* 260-263 */
"ORA H", "ORA L", "ORA M", "ORA A",                     /* 264-267 */
"CMP B", "CMP C", "CMP D", "CMP E",                     /* 270-273 */
"CMP H", "CMP L", "CMP M", "CMP A",                     /* 274-277 */
"RNZ", "POP B", "JNZ", "JMP",                           /* 300-303 */
"CNZ", "PUSH B", "ADI", "RST 0",                        /* 304-307 */
"RZ", "RET", "JZ", "???",                               /* 310-313 */
"CZ", "CALL", "ACI", "RST 1",                           /* 314-317 */
"RNC", "POP D", "JNC", "OUT",                           /* 320-323 */
"CNC", "PUSH D", "SUI", "RST 2",                        /* 324-327 */
"RC", "???", "JC", "IN",                                /* 330-333 */
"CC", "???", "SBI", "RST 3",                            /* 334-337 */
"RPO", "POP H", "JPO", "XTHL",                          /* 340-343 */
"CPO", "PUSH H", "ANI", "RST 4",                        /* 344-347 */
"RPE", "PCHL", "JPE", "XCHG",                           /* 350-353 */
"CPE", "???", "XRI", "RST 5",                           /* 354-357 */
"RP", "POP PSW", "JP", "DI",                            /* 360-363 */
"CP", "PUSH PSW", "ORI", "RST 6",                       /* 364-367 */
"RM", "SPHL", "JM", "EI",                               /* 370-373 */
"CM", "???", "CPI", "RST 7",                            /* 374-377 */
 };

int32 oplen[256] = {
1,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,0,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,3,3,3,1,2,1,1,1,3,0,3,3,2,1,1,1,3,2,3,1,2,1,1,0,3,2,3,0,2,1,
1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1,1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1 };

/* This is the binary loader.  The input file is considered to be
   a string of literal bytes with no format special format. The
   load starts at the current value of the PC.
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 i, addr = 0, cnt = 0;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
addr = saved_PC;
while ((i = getc (fileref)) != EOF) {
    M[addr] = i;
    addr++;
    cnt++;
}                                                       /* end while */
sim_printf ("%d Bytes loaded.\n", cnt);
return (SCPE_OK);
}

/* Symbolic output

   Inputs:
        *of   = output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        status  =       error code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, c1, c2, inst, adr;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
c1 = (val[0] >> 8) & 0177;
c2 = val[0] & 0177;
if (sw & SWMASK ('A')) {
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
}
if (sw & SWMASK ('C')) {
    fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
}
if (!(sw & SWMASK ('M'))) return SCPE_ARG;
inst = val[0];
fprintf (of, "%s", opcode[inst]);
if (oplen[inst] == 2) {
    if (strchr(opcode[inst], ' ') != NULL)
        fprintf (of, ",");
    else fprintf (of, " ");
    fprintf (of, "%o", val[1]);
}
if (oplen[inst] == 3) {
    adr = val[1] & 0xFF;
    adr |= (val[2] << 8) & 0xff00;
    if (strchr(opcode[inst], ' ') != NULL)
        fprintf (of, ",");
    else fprintf (of, " ");
    fprintf (of, "%o", adr);
}
return -(oplen[inst] - 1);
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, i = 0, j, r;
char gbuf[CBUFSIZE];

memset (gbuf, 0, sizeof (gbuf));
cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (uint32) cptr[0];
    return SCPE_OK;
}
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = ((uint32) cptr[0] << 8) + (uint32) cptr[1];
    return SCPE_OK;
}

/* An instruction: get opcode (all characters until null, comma,
   or numeric (including spaces).
*/

while (i < sizeof (gbuf) - 4) {
    if (*cptr == ',' || *cptr == '\0' ||
         sim_isdigit(*cptr))
            break;
    gbuf[i] = toupper(*cptr);
    cptr++;
    i++;
}

/* Allow for RST which has numeric as part of opcode */

if (toupper(gbuf[0]) == 'R' &&
    toupper(gbuf[1]) == 'S' &&
    toupper(gbuf[2]) == 'T') {
    gbuf[i] = toupper(*cptr);
    cptr++;
    i++;
}

/* Allow for 'MOV' which is only opcode that has comma in it. */

if (toupper(gbuf[0]) == 'M' &&
    toupper(gbuf[1]) == 'O' &&
    toupper(gbuf[2]) == 'V') {
    gbuf[i] = toupper(*cptr);
    cptr++;
    i++;
    gbuf[i] = toupper(*cptr);
    cptr++;
    i++;
}

/* kill trailing spaces if any */
gbuf[i] = '\0';
sim_trim_endspc (gbuf);

/* find opcode in table */
for (j = 0; j < 256; j++) {
    if (strcmp(gbuf, opcode[j]) == 0)
        break;
}
if (j > 255)                                            /* not found */
    return sim_messagef (SCPE_ARG, "No such opcode: %s\n", gbuf);

val[0] = j;                                             /* store opcode */
if (oplen[j] < 2)                                       /* if 1-byter we are done */
    return SCPE_OK;
if (*cptr == ',') cptr++;
cptr = get_glyph(cptr, gbuf, 0);                        /* get address */
sscanf(gbuf, "%o", &r);
if (oplen[j] == 2) {
    val[1] = r & 0xFF;
    return (-1);
}
val[1] = r & 0xFF;
val[2] = (r >> 8) & 0xFF;
return (-2);
}
