/*************************************************************************
 *                                                                       *
 * $Id: tx0_sys_orig.c 2065 2009-02-25 15:05:00Z hharte $                *
 *                                                                       *
 * Copyright (c) 2009-2012 Howard M. Harte.                              *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     TX-0 simulator interface                                          *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#include "tx0_defs.h"
#include <ctype.h>

typedef struct {
    int32 opr;
    const char *mnemonic;
    const char *desc;
} OPMAP;

const OPMAP opmap_orig [] = {
    { 0700000, "cll", "Clear the left nine digital positions of the AC" },
    { 0640000, "clr", "Clear the right nine digital positions of the AC" },
    { 0620000, "ios", "In-Out Stop" },
    { 0630000, "hlt", "Halt the computer" },
    { 0607000, "p7h", "Punch holes 1-6 in flexo tape Also punch a 7th hole on tape" },
    { 0606000, "p6h", "Punch holes 1-6 in flexo tape" },
    { 0604000, "pnt", "Print one flexowrite charater" },
    { 0601000, "r1c", "Read one line of flexo tape" },
    { 0603000, "r3c", "Read three lines of flexo tape" },
    { 0602000, "dis", "Intesnsify a point on the scope from x,y in AC" },
    { 0600400, "shr", "Shift the AC right one place" },
    { 0600600, "cyr", "Cycle the AC right one digital position (AC17 -> AC0)" },
    { 0600200, "mlr", "Store the contents of the MBR in the LR" },
    { 0600100, "pen", "Read the light pen flip flops 1 and 2 into AC0 and AC1" },
    { 0600004, "tac", "Insert a one in each digital position of the AC whereever there is a one in the corresponding digital position of the TAC" },
    { 0600040, "com", "Complement every digit in the accumulator" },
    { 0600020, "pad", "Partial add AC to MBR" },
    { 0600010, "cry", "Partial add the 18 digits of the AC to the corresponding 18 digits of the carry" },
    { 0600001, "amb", "Store the contents of the AC in the MBR" },
    { 0600003, "tbr", "Store the contents of the TBR in the MBR" },
    { 0600002, "lmb", "Store the contents of the LR in the MBR" },
/*      Combined Operate Class Commands */
    { 0740000, "cla", "Clear the AC" },
    { 0600031, "cyl", "Cycle the AC left one digital position" },
    { 0740040, "clc", "Clear and complement AC" },
    { 0622000, "dis", "Display (note IOS must be included for in-out cmds)" },
    { 0760000, "ios+cll+clr", "In out stop with AC cleared" },
    { 0627600, "ios+p7h+cyr", "Punch 7 holes and cycle AC right" },
    { 0626600, "ios+p6h+cyr", "Punch 6 holes and cycle AC right" },
    { 0766000, "ios+cll+clr+p6h", "Clear the AC and punch a blank space on tape" },
    { 0624600, "ios+pnt+cyr", "Print and cycle AC right" },
    { 0627021, "ios+p7h+amb+pad", "Punch 7 holes and leave AC cleared" },
    { 0626021, "ios+p6h+amb+pad", "Punch 6 holes and leave AC cleared" },
    { 0624021, "ios+pnt+amb+pad", "Print and leave AC cleared" },
    { 0741000, "cll+clr+ric", "Clear AC and start PETR running (note computer hasn't stopped to wait for information" },
    { 0601031, "ric+amb+pad+cry", "Start PETR running and cycle AC left" },
    { 0601600, "ric+cyr", "Start PETR running and cycle right" },
    { 0763000, "cll+clr+ios+r3c", "Clear AC and read 3 lines of tape" },
    { 0761000, "cll+clr+ios+ric", "Clear AC and read one line of tape" },
    { 0761031, "cll+clr+ios+ric+pad+cry", "Read 1 line of tape and cycle AC left" },
    { 0761600, "cll+clr+ios+ric+cyr", "Read 1 line of tape and cycle right" },
    { 0740004, "cll+clr+tac", "Put contents of TAC in AC" },
    { 0600030, "pad+cry", "Full-add the MBR and AC and leave sum in AC" },
    { 0740022, "cll+clr+lmb+pad", "Clear the AC - store LR contents in memory buffer register add memory buffer to AC - i.e., store live reg. contents in AC (LAC)" },
    { 0600201, "amb+mlr", "Store contents of AC in MBR, store contents of MBR in LR i.e., store contents of AC in LR. (ALR)" },
    { 0600022, "lmb+pad", "Store the contents of LR in MBR, partial add AC and MBR i.e., partial add LR to AC. (LPD)" },
/*  { 0600200, "mlr", "Since MLR alone will ahve a clear MBR, this is really clear LR (LRO)" }, */
    { 0600032, "lmb+pad+cry", "Full-add the LR to the AC (LAD)" },
    { 0740023, "cll+clr+tbr+pad", "Store contents of TBR in AC" },
    { 0000000, NULL, NULL }
};

/* Use scp.c provided fprintf function */
#define fprintf Fprintf
#define fputs(_s,f) Fprintf(f,"%s",_s)
#define fputc(_c,f) Fprintf(f,"%c",_c)

t_stat fprint_sym_orig (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 i, inst, op;

inst = val[0];

/* Instruction decode */

op = (inst >> 16) & 3;

switch (op) {
    case 0: 
        fprintf (of, "sto %06o", inst & 0177777);              /* opcode */
        break;
    case 1:
        fprintf (of, "add %06o", inst & 0177777);              /* opcode */
        break;
    case 2:
        fprintf (of, "trn %06o", inst & 0177777);              /* opcode */
        break;
    case 3:
        for(i=0;opmap_orig[i].opr != 0;i++) {
            if(inst == opmap_orig[i].opr) {
                fprintf (of, "opr %s (%s)", opmap_orig[i].mnemonic, opmap_orig[i].desc);              /* opcode */
            }
        }
        break;
}

return SCPE_OK;
}

