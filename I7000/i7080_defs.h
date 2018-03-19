/* i7080_defs.h: IBM 7080 simulator definitions

   Copyright (c) 2006-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                           /* simulator defns */

#include "i7000_defs.h"

/* Memory */
#define MEM_ADDR_OK(x)  ((x) < MEMSIZE)
extern uint8            M[MAXMEMSIZE];
extern uint32           EMEMSIZE;       /* Size of emulated memory */

/* Issue a command to a channel */
int chan_cmd(uint16 dev, uint16 cmd, uint32 addr);
/* Map device to channel */
int chan_mapdev(uint16 dev);
/* Process the CHR 3 13 command and abort all channel activity */
void chan_chr_13();
uint32 load_addr(int loc);
void store_addr(uint32 addr, int loc);

/* Opcode definitions. */
#define OP_TR           CHR_1
#define OP_SEL          CHR_2
#define OP_CTL          CHR_3
#define OP_CMP          CHR_4
#define OP_SPR          CHR_5
#define OP_ADM          CHR_6
#define OP_UNL          CHR_7
#define OP_LOD          CHR_8
#define OP_TMT          CHR_9
#define OP_TRS          CHR_O
#define OP_NOP          CHR_A
#define OP_SET          CHR_B
#define OP_SHR          CHR_C
#define OP_LEN          CHR_D
#define OP_RND          CHR_E
#define OP_ST           CHR_F
#define OP_ADD          CHR_G
#define OP_RAD          CHR_H
#define OP_TRA          CHR_I
#define OP_HLT          CHR_J
#define OP_TRH          CHR_K
#define OP_TRE          CHR_L
#define OP_TRP          CHR_M
#define OP_TRZ          CHR_N
#define OP_SUB          CHR_P
#define OP_RSU          CHR_Q
#define OP_WR           CHR_R
#define OP_RWW          CHR_S
#define OP_SGN          CHR_T
#define OP_RCV          CHR_U
#define OP_MPY          CHR_V
#define OP_DIV          CHR_W
#define OP_NTR          CHR_X
#define OP_RD           CHR_Y
#define OP_WRE          CHR_Z
#define OP_AAM          CHR_QUOT
#define OP_CTL2         CHR_COM
#define OP_LDA          CHR_EQ
#define OP_ULA          CHR_STAR
#define OP_SND          CHR_SLSH
#define OP_BLM          CHR_DOL
#define OP_SBZ          CHR_RPARN
#define OP_TZB          CHR_DOT
#define OP_CTL3         CHR_LPARN
#define OP_SMT          CHR_TRM

/* Channel options */
#define CHAN_NOREC      0001            /* Don't stop at record */
#define CHAN_8BIT       0002            /* Send 8 bit data */
#define CHAN_SNS        0010            /* Issue sense command */
#define CHAN_CTL        0020            /* Issue control command */
#define CHAN_ZERO       0040            /* Zero memory after write */
#define CHAN_SKIP       0040            /* Don't read data */
#define CHAN_END        0100            /* Last location */
#define CHAN_RECCNT     0200            /* Last was set record counter */
#define CHAN_CMD        0400            /* Opcode in high order bits */

#define CHAN_AFULL      01000           /* A buffer has data */
#define CHAN_BFULL      02000           /* B buffer has data */
#define CHAN_BFLAG      04000           /* Write/read B buffer */

