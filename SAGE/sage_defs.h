/* sage_defs.h: simulator header file for sage-II system

   Copyright (c) 2009-2010 Holger Veit

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
   Holger Veit BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   10-Jan-10    HV      Defines for certain chip bits/registers
   04-Oct-09    HV      Initial version
*/

#ifndef SAGE_DEFS_H_
#define SAGE_DEFS_H_

#include "sim_defs.h"
#include "m68k_cpu.h"

/* don't define this yet, won't work */
#undef SAGE_IV

#define UNIT_CPU_V_ROM      UNIT_CPU_V_FREE             /* has switchable ROM */
#define UNIT_CPU_ROM        (1 << UNIT_CPU_V_ROM)

#define SAGEMEM             (128*1024)

#define ROMBASE             0xfe0000                    /* base address of ROM */
#ifdef SAGE_IV
#define ROMSIZE             0x004000                    /* size of ROM (4K words) */
#else
#define ROMSIZE             0x002000                    /* size of ROM (4K words) */
#endif

/* simh timers */
#define TMR_RTC1        0
#define TMR_RTC2        1
#define TMR_CONS        2
#define TMR_INT         3

/* definitions for certain chips */
#include "chip_defs.h"

/* PIC base address */
#define U73_ADDR    0xffc041
extern t_stat       sage_raiseint(int level); /* sage specific interrupt handler */

/* 8255 for dip switches and floppy control */
#define U22_ADDR    0xffc021
extern uint32       *u22_portc; /* exposed for use by FD device */
#define U22C_FRES   0x80
#define U22C_PCRMP  0x40
#define U22C_MOT    0x20
#define U22C_SL1    0x10
#define U22C_SL0    0x08
#define U22C_FDIE   0x04
#define U22C_RDY    0x02
#define U22C_TC     0x01

/* 8253 timer units */
#define U75_ADDR    0xffc001
#define U74_ADDR    0xffc081
#define TIMER2C0_PICINT 6
#define TIMER2C2_PICINT 0

/* FDC */
#define U21_ADDR    0xffc051
extern I8272 u21;
#define FDC_AUTOINT 6

/* LP port */
#define U39_ADDR    0xffc061
#define LP_PICINT   5
#define SI_PICINT   7

#define U39B_FDI    0x01
#define U39B_WP     0x02
#define U39B_RG     0x04
#define U39B_CD     0x08
#define U39B_BUSY   0x10
#define U39B_PAPER  0x20
#define U39B_SEL    0x40
#define U39B_FAULT  0x80
#define U39C_PRES   0x01
#define U39C_SC     0x02
#define U39C_SI     0x04
#define U39C_LEDR   0x08
#define U39C_STROBE 0x10
#define U39C_PRIME  0x20
#define U39C_RCNI   0x40
#define U39C_RMI    0x80


/* SIO port */
#define U58_ADDR            0xffc031
#define SIORX_PICINT    1
#define SIOTX_PICINT    3

/* CONS port */
#define U57_ADDR            0xffc071
#define CONSRX_AUTOINT  5
#define CONSTX_PICINT   2

/* unimplemented */
#define IEEEBASE            0xffc011                    /* IEEE-488 interface (TMS9914) */

/* winchester board: not yet */
#define S2651d              0xffc401                    /* aux serial 4 */
#define S2651d_DATA         (S2651d+0)                  /* RW data port aux 4 */
#define S2651d_STATUS       (S2651d+2)                  /* R status aux 4 */ 
#define S2651d_MODE         (S2651d+4)                  /* W mode aux 4 */  
#define S2651d_CTRL         (S2651d+6)                  /* W mode aux 4 */  

#define S2651c              0xffc441                    /* aux serial 3 */
#define S2651c_DATA         (S2651c+0)                  /* RW data port aux 3 */
#define S2651c_STATUS       (S2651c+2)                  /* R status aux 3 */ 
#define S2651c_MODE         (S2651c+4)                  /* W mode aux 3 */  
#define S2651c_CTRL         (S2651c+6)                  /* W mode aux 3 */  

#define S2651b              0xffc481                    /* aux serial 2 */
#define S2651b_DATA         (S2651b+0)                  /* RW data port aux 2 */
#define S2651b_STATUS       (S2651b+2)                  /* R status aux 2 */ 
#define S2651b_MODE         (S2651b+4)                  /* W mode aux 2 */  
#define S2651b_CTRL         (S2651b+6)                  /* W mode aux 2 */  

#define S2651a              0xff4c1                     /* aux serial 1 */
#define S2651a_DATA         (S2651a+0)                  /* RW data port aux 1 */
#define S2651a_STATUS       (S2651a+2)                  /* R status aux 1 */ 
#define S2651a_MODE         (S2651a+4)                  /* W mode aux 1 */  
#define S2651a_CTRL         (S2651a+6)                  /* W mode aux 1 */  

/* must be included at the end */
#include "m68k_cpu.h"

#endif

