/* s100_z80.h

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   11/07/25 Initial version

*/

#ifndef S100_Z80_H_
#define S100_Z80_H_

#include "s100_cpu.h"

#define LDA_INSTRUCTION         0x3e                        /* op-code for LD A,<8-bit value> instruction   */
#define UNIT_NO_OFFSET_1        0x37                        /* LD A,<unitno>                                */
#define UNIT_NO_OFFSET_2        0xb4                        /* LD a,80h | <unitno>                          */
#define LDB_INSTRUCTION         0x06                        /* op-code for LD B,<8-bit value> instruction   */
#define START_SECTOR_OFFSET     0x57                        /* LD B,<start_sector_offset>                   */

#define CPU_INDEX_8080          4                           /* index of default PC register */

/* simulator stop codes */
#define STOP_IBKPT              1                           /* breakpoint   (program counter)               */
#define STOP_MEM                2                           /* breakpoint   (memory access)                 */
#define STOP_INSTR              3                           /* breakpoint   (instruction access)            */
#define STOP_OPCODE             4                           /* invalid operation encountered (8080, Z80, 8086) */
#define STOP_HALT               5                           /* HALT                                         */

#define UNIT_Z80_V_OPSTOP       (UNIT_V_UF+0)               /* stop on invalid operation                    */
#define UNIT_Z80_OPSTOP         (1 << UNIT_Z80_V_OPSTOP)
#define UNIT_Z80_V_STOPONHALT   (UNIT_V_UF+1)               /* stop simulation on HALT                      */
#define UNIT_Z80_STOPONHALT     (1 << UNIT_Z80_V_STOPONHALT)

#define PLURAL(x) (x), (x) == 1 ? "" : "s"

extern ChipType z80_chiptype;
extern REG *z80_pc_reg;

extern t_stat z80_instr(void);
extern t_value z80_pc_value(void);
extern t_stat z80_parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw);
extern t_bool z80_is_pc_a_subroutine_call (t_addr **ret_addrs);
extern int32 z80_dasm(char *S, const uint32 *val, const int32 addr);
extern t_stat z80_cmd_reg(int32 flag, CONST char *cptr);
extern t_bool z80_is_pc_a_subroutine_call(t_addr **ret_addrs);
extern t_stat z80_show_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);

#endif
