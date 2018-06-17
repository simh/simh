/*  m68kcpmsim.h: CP/M for Motorola 68000 definitions

 Copyright (c) 2014, Peter Schorn

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

 Except as contained in this notice, the name of Peter Schorn shall not
 be used in advertising or otherwise to promote the sale, use or other dealings
 in this Software without prior written authorization from Peter Schorn.

 Based on work by David W. Schultz http://home.earthlink.net/~david.schultz (c) 2014
 */

#ifndef M68KSIM__HEADER
#define M68KSIM__HEADER

#include "altairz80_defs.h"

unsigned int m68k_cpu_read_byte(unsigned int address);
unsigned int m68k_cpu_read_byte_raw(unsigned int address);
unsigned int m68k_cpu_read_word(unsigned int address);
unsigned int m68k_cpu_read_long(unsigned int address);
void m68k_cpu_write_byte(unsigned int address, unsigned int value);
void m68k_cpu_write_byte_raw(unsigned int address, unsigned int value);
void m68k_cpu_write_word(unsigned int address, unsigned int value);
void m68k_cpu_write_long(unsigned int address, unsigned int value);
void m68k_cpu_pulse_reset(void);
void m68k_cpu_set_fc(unsigned int fc);
int  m68k_cpu_irq_ack(int level);

t_stat sim_instr_m68k(void);
void m68k_cpu_reset(void);
void m68k_clear_memory(void);
void m68k_CPUToView(void);
void m68k_viewToCPU(void);
t_stat m68k_hdsk_boot(const int32 unitno, DEVICE *dptr,
                      const uint32 verboseMessage, const int32 hdskNumber);

#define M68K_MAX_RAM        0xffffff        // highest address of 16MB of RAM
#define M68K_MAX_RAM_LOG2   24              // 24 bit addresses

#endif /* M68KSIM__HEADER */
