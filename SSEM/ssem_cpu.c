/* ssem_cpu.c: Manchester University SSEM (Small Scale Experimental Machine) 
                         CPU simulator

   Based on the SIMH package written by Robert M Supnik
 
   Copyright (c) 2006-2013, Gerardo Ospina

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   This is not a supported product, but the author welcomes bug reports and fixes.
   Mail to ngospina@gmail.com
  
   cpu          SSEM CPU

   The system state for the SSEM is:

   A[0]<0:31>         accumulator
   C[0]<0:31>         current instruction
   C[1]<0:31>         present instruction

   The SSEM has just one instruction format:

                        1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         |inst |                     |address  | 
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   SSEM instructions:

    <13:15>               operation

    000   0            C[0] <- S[n]
    001   1            C[0] <- C[0] + S[n]
    010   2            A[0] <- -S[n]
    011   3            S[n] <- A[0]
    100   4            A[0] <- A[0] - S[n]
    110   6            C[0] <- C[0] + 1  if (A[0] < 0)
    111   7            Stop the machine

    The SSEM has 32 32b words of memory.

   This routine is the instruction decode routine for the SSEM.
   It is called from the simulator control program to execute 
   instructions in simulated memory, starting at the simulated
   CI.  It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        Stop instruction
        breakpoint encountered

   2. Interrupts.  There are no interrupts.

   3. Non-existent memory.  All of memory always exists.

   4. Adding I/O devices.  The SSEM could not support additional
      I/O devices.
*/

#include "ssem_defs.h"

uint32 S[MEMSIZE] = { 0 };        /* storage (memory) */

int32  A[MEMSIZE] = { 0 };        /* A[0] accumulator */
uint32 C[MEMSIZE] = { 0, 0 };    /* C[0] current instruction */
                                /* C[1] present instruction */
uint32 Staticisor = 0;

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_one_inst (uint32 opc, uint32 ir);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX, MEMSIZE) };

REG cpu_reg[] = {
    { DRDATA (CI, C[0], 5), REG_VMAD },
    { HRDATA (A, A[0], 32), REG_VMIO },
    { HRDATA (PI, C[1], 32), REG_VMIO + REG_HRO },
    { HRDATA (LF, Staticisor, 32), REG_VMIO + REG_HRO },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_SSEM, 0, "Manchester University SSEM (Small Scale Experimental Machine)", "SSEM" },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 5, 1, 16, 32,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

t_stat sim_instr (void)
{
t_stat reason = 0;

sim_cancel_step ();                                     /* defang SCP step */

/* Main instruction fetch/decode loop */
do {

    if (sim_interval <= 0) {                            /* check clock queue */
#if !UNIX_PLATFORM
        if ((reason = sim_poll_kbd()) == SCPE_STOP) {   /* poll on platforms without reliable signalling */
            break;
        }
#endif
        if ((reason = sim_process_event ()))
            break;
        }
    
    if (sim_brk_summ &&                                 /* breakpoint? */
        sim_brk_test (*C, SWMASK ('E'))) {
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    /* Increment current instruction */
    *C = (*C + 1) & AMASK;

    /* Get present instruction */
    C[1] = Read (*C);                                   

    Staticisor = C[1] & IMASK;                          /* get instruction */
    sim_interval = sim_interval - 1;

    if ((reason = cpu_one_inst (*C, Staticisor))) {     /* one instr; error? */
        break;
        }

    if (sim_step && (--sim_step <= 0))                  /* do step count */
        reason = SCPE_STOP;

    } while (reason == 0);                              /* loop until halted */

return reason;
}

t_stat cpu_reset (DEVICE *dptr)
{
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = Read (addr);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
Write (addr, val);
return SCPE_OK;
}

/* Execute one instruction */

t_stat cpu_one_inst (uint32 opc, uint32 ir)
{
uint32 ea, op;
t_stat reason = 0;

op = I_GETOP (ir);                    /* opcode */
switch (op) {                        /* case on opcode */

    case OP_JUMP_INDIRECT:            /* C[0] <- S[ea] */
        ea = I_GETEA (ir);            /* address */
        *C =  Read(ea);
        break;

    case OP_JUMP_INDIRECT_RELATIVE:    /* C[0] <- C[0] + S[ea] */
        ea = I_GETEA (ir);            /* address */
        *C += Read(ea);
        break;

    case OP_LOAD_NEGATED:            /* A[0] <- -S[ea] */
        ea = I_GETEA (ir);            /* address */
        *A = -((int32)Read(ea));
        break;

    case OP_STORE:                    /* S[ea] <- A[0] */
        ea = I_GETEA (ir);            /* address */
        Write(ea, (uint32) *A);
        break;

    case OP_SUBSTRACT:                /* A[0] <- A[0] - S[ea] */
    case OP_UNDOCUMENTED:
        ea = I_GETEA (ir);            /* address */
        *A -= ((int32) Read(ea));
        break;

    case OP_TEST:                    /* C[0] <- C[0] + 1  if (A[0] < 0) */
        if (*A < 0){
            *C += 1;
        }
        break;

    case OP_STOP:                    /* Stop  the machine */
        reason = STOP_STOP;            /* stop simulation */
        break;
    }                                /* end switch */

return reason;
}

/* Support routines */

uint32 Read (uint32 ea)
{
return S[ea] & MMASK;
}

void Write (uint32 ea, uint32 dat)
{
S[ea] = dat & MMASK;
return;
}

