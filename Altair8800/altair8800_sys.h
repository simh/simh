/* altair8800_sys.h

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

#ifndef _ALTAIR8800_SYS_H
#define _ALTAIR8800_SYS_H

#include "sim_defs.h"

#define SIM_EMAX            6

extern DEVICE bus_dev;
extern DEVICE cpu_dev;
extern DEVICE ssw_dev;
extern DEVICE simh_dev;
extern DEVICE z80_dev;
extern DEVICE ram_dev;
extern DEVICE bram_dev;
extern DEVICE rom_dev;
extern DEVICE po_dev;
extern DEVICE mdsk_dev;
extern DEVICE m2sio0_dev;
extern DEVICE m2sio1_dev;
extern DEVICE sio_dev;
extern DEVICE sbc200_dev;
extern DEVICE tarbell_dev;
extern DEVICE vfii_dev;

extern char memoryAccessMessage[256];
extern char instructionMessage[256];

extern int32 sys_find_unit_index(UNIT* uptr);

extern void sys_set_cpu_instr(t_stat (*routine)(void));
extern void sys_set_cpu_pc(REG *reg);
extern void sys_set_cpu_pc_value(t_value (*routine)(void));
extern void sys_set_cpu_parse_sym(t_stat (*routine)(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw));
extern void sys_set_cpu_dasm(int32 (*routine)(char *S, const uint32 *val, const int32 addr));
extern void sys_set_cpu_is_subroutine_call(t_bool (*routine)(t_addr **ret_addrs));
extern char *sys_strupr(const char *str);
extern uint8 sys_floorlog2(unsigned int n);

#endif
