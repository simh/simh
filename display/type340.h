/*
 * external interface for type340.c
 * Simulator Independent DEC Type 340 Graphic Display Processor Simulation
 */

/*
 * Copyright (c) 2018, Philip L. Budne
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the author shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization
 * from the authors.
 */

typedef unsigned int ty340word;

/*
 * Type340 status bits
 * MUST BE EXACT SAME VALUES AS USED IN PDP-10 CONI!!!
 */
#define ST340_VEDGE     04000
#define ST340_LPHIT     02000
#define ST340_HEDGE     01000
#define ST340_STOP_INT  00400

/* NOT same as PDP-10 CONI */
#define ST340_STOPPED   0400000

/*
 * calls from host into type340.c
 */
ty340word ty340_reset(void *);
ty340word ty340_status(void);
ty340word ty340_instruction(ty340word inst);
ty340word ty340_get_dac(void);
ty340word ty340_get_asr(void);
ty340word ty340_sense(ty340word);
void ty340_set_dac(ty340word addr);
void ty340_clear(ty340word addr);
void ty340_cycle(void);
void ty340_set_status(ty340word);
void ty342_set_grid(int, int);

/*
 * calls from type340.c into host simulator
 */
extern ty340word ty340_fetch(ty340word);
extern void ty340_store(ty340word, ty340word);
extern void ty340_lp_int(ty340word x, ty340word y);
extern void ty340_rfd(void);
