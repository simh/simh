/* vax4nn_stddev.c: KA4nn standard devices

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   rom          bootstrap ROM (no registers)
   nvr          non-volatile ROM (no registers)
   clk          100Hz and TODR clock
*/

#include "vax_defs.h"

#define UNIT_V_NODELAY  (UNIT_V_UF + 0)                 /* ROM access equal to RAM access */
#define UNIT_NODELAY    (1u << UNIT_V_NODELAY)

#define CLKCSR_IMP      (CSR_IE)                        /* real-time clock */
#define CLKCSR_RW       (CSR_IE)
#define CLK_DELAY       5000                            /* 100 Hz */
#define TMXR_MULT       1                               /* 100 Hz */

uint32 *rom = NULL;                                     /* boot ROM */
uint8 *nvr = NULL;                                      /* non-volatile mem */
int32 clk_csr = 0;                                      /* control/status */
int32 clk_tps = 100;                                    /* ticks/second */
int32 tmr_int = 0;                                      /* interrupt */
int32 tmxr_poll = CLK_DELAY * TMXR_MULT;                /* term mux poll */
int32 tmr_poll = CLK_DELAY;                             /* pgm timer poll */

t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat rom_reset (DEVICE *dptr);
t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *rom_description (DEVICE *dptr);
t_stat nvr_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvr_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat nvr_reset (DEVICE *dptr);
t_stat nvr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat nvr_attach (UNIT *uptr, CONST char *cptr);
t_stat nvr_detach (UNIT *uptr);
const char *nvr_description (DEVICE *dptr);
t_stat clk_svc (UNIT *uptr);
t_stat clk_reset (DEVICE *dptr);
const char *clk_description (DEVICE *dptr);

extern int32 sysd_hlt_enb (void);
extern int32 nar_rd (int32 pa);

/* ROM data structures

   rom_dev      ROM device descriptor
   rom_unit     ROM units
   rom_reg      ROM register list
*/

UNIT rom_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK, ROMSIZE) };

REG rom_reg[] = {
    { NULL }
    };

MTAB rom_mod[] = {
    { UNIT_NODELAY, UNIT_NODELAY, "fast access", "NODELAY", NULL, NULL, NULL, "Disable calibrated ROM access speed" },
    { UNIT_NODELAY, 0, "1usec calibrated access", "DELAY",  NULL, NULL, NULL, "Enable calibrated ROM access speed" },
    { 0 }
    };

DEVICE rom_dev = {
    "ROM", &rom_unit, rom_reg, rom_mod,
    1, 16, ROMAWIDTH, 4, 16, 32,
    &rom_ex, &rom_dep, &rom_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, &rom_help, NULL, NULL,
    &rom_description
    };

/* NVR data structures

   nvr_dev      NVR device descriptor
   nvr_unit     NVR units
   nvr_reg      NVR register list
*/

UNIT nvr_unit =
    { UDATA (NULL, UNIT_FIX+UNIT_BINK, NVRSIZE) };

REG nvr_reg[] = {
    { NULL }
    };

DEVICE nvr_dev = {
    "NVR", &nvr_unit, nvr_reg, NULL,
    1, 16, NVRAWIDTH, 4, 16, 8,
    &nvr_ex, &nvr_dep, &nvr_reset,
    NULL, &nvr_attach, &nvr_detach,
    NULL, 0, 0, NULL, NULL, NULL, &nvr_help, NULL, NULL,
    &nvr_description
    };

/* CLK data structures

   clk_dev      CLK device descriptor
   clk_unit     CLK unit descriptor
   clk_reg      CLK register list
*/

UNIT clk_unit = { UDATA (&clk_svc, UNIT_IDLE, 0), CLK_DELAY };

REG clk_reg[] = {
    { HRDATAD (CSR,          clk_csr,        16, "control/status register") },
    { FLDATAD (INT,          tmr_int,         0, "interrupt request") },
    { FLDATAD (IE,           clk_csr,  CSR_V_IE, "interrupt enable flag (CSR<6>)") },
    { DRDATAD (TIME,   clk_unit.wait,        24, "initial poll interval"), REG_NZ + PV_LEFT },
    { DRDATAD (POLL,        tmr_poll,        24, "calibrated poll interval"), REG_NZ + PV_LEFT + REG_HRO },
    { DRDATAD (TPS,          clk_tps,         8, "ticks per second (100)"), REG_NZ + PV_LEFT },
#if defined (SIM_ASYNCH_IO)
    { DRDATAD (ASYNCH,            sim_asynch_enabled,         1, "asynch I/O enabled flag"), PV_LEFT },
    { DRDATAD (LATENCY,           sim_asynch_latency,        32, "desired asynch interrupt latency"), PV_LEFT },
    { DRDATAD (INST_LATENCY, sim_asynch_inst_latency,        32, "calibrated instruction latency"), PV_LEFT },
#endif
    { NULL }
    };

DEVICE clk_dev = {
    "CLK", &clk_unit, clk_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &clk_reset,
    NULL, NULL, NULL,
    NULL, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, 
    &clk_description
    };

/* ROM: read only memory - stored in a buffered file
   Register space access routines see ROM twice

   ROM access has been 'regulated' to about 1Mhz to avoid issues
   with testing the interval timers in self-test.  Specifically,
   the VAX boot ROM (ka4xx.bin) contains code which presumes that
   the VAX runs at a particular slower speed when code is running
   from ROM (which is not cached).  These assumptions are built
   into instruction based timing loops. As the host platform gets
   much faster than the original VAX, the assumptions embedded in
   these code loops are no longer valid.

   Code has been added to the ROM implementation to limit CPU speed
   to about 500K instructions per second.  This heads off any future
   issues with the embedded timing loops.
*/

int32 rom_rd (int32 pa)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;
int32 val = rom[rg];

if (rom_unit.flags & UNIT_NODELAY)
    return val;

return sim_rom_read_with_delay (val);
}

void rom_wr_B (int32 pa, int32 val)
{
int32 rg = ((pa - ROMBASE) & ROMAMASK) >> 2;
int32 sc = (pa & 3) << 3;

rom[rg] = ((val & 0xFF) << sc) | (rom[rg] & ~(0xFF << sc));
return;
}

/* ROM examine */

t_stat rom_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if ((vptr == NULL) || (addr & 03))
    return SCPE_ARG;
if (addr >= ROMSIZE)
    return SCPE_NXM;
*vptr = rom[addr >> 2];
return SCPE_OK;
}

/* ROM deposit */

t_stat rom_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr & 03)
    return SCPE_ARG;
if (addr >= ROMSIZE)
    return SCPE_NXM;
rom[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* ROM reset */

t_stat rom_reset (DEVICE *dptr)
{
if (rom == NULL)
    rom = (uint32 *) calloc (ROMSIZE >> 2, sizeof (uint32));
if (rom == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

t_stat rom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Read-only memory (ROM)\n\n");
fprintf (st, "The boot ROM consists of a single unit, simulating the 256KB boot ROM.  It has\n");
fprintf (st, "no registers.  The boot ROM is loaded with a binary byte stream using the \n");
fprintf (st, "LOAD -r command:\n\n");
fprintf (st, "   LOAD -r KA410.BIN      load ROM image KA410.BIN\n\n");
fprintf (st, "When the simulator starts running (via the BOOT command), if the ROM has\n");
fprintf (st, "not yet been loaded, an attempt will be made to automatically load the\n");
fprintf (st, "ROM image from the file ka410.bin in the current working directory.\n");
fprintf (st, "If that load attempt fails, then a copy of the missing ROM file is\n");
fprintf (st, "written to the current directory and the load attempt is retried.\n\n");
fprintf (st, "ROM accesses a use a calibrated delay that slows ROM-based execution to\n");
fprintf (st, "about 500K instructions per second.  This delay is required to make the\n");
fprintf (st, "power-up self-test routines run correctly on very fast hosts.\n");
fprint_set_help (st, dptr);
return SCPE_OK;
}

const char *rom_description (DEVICE *dptr)
{
return "read-only memory";
}

/* NVR: non-volatile RAM - stored in a buffered file */

int32 nvr_rd (int32 pa)
{
int32 rg = (pa - NVRBASE) >> 2;
int32 val;

val = nvr[rg];
if (rg < 0x40)                                          /* network addr */
    val = val | (nar_rd (pa) << 8);
return val;
}

void nvr_wr (int32 pa, int32 val, int32 lnt)
{
int32 rg = (pa - NVRBASE) >> 2;
nvr[rg] = (val & BMASK);
}

/* NVR examine */

t_stat nvr_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if ((vptr == NULL) || (addr & 03))
    return SCPE_ARG;
if (addr >= NVRSIZE)
    return SCPE_NXM;
*vptr = nvr[addr >> 2];
return SCPE_OK;
}

/* NVR deposit */

t_stat nvr_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;

if (addr & 03)
    return SCPE_ARG;
if (addr >= NVRSIZE)
    return SCPE_NXM;
nvr[addr >> 2] = (uint32) val;
return SCPE_OK;
}

/* NVR reset */

t_stat nvr_reset (DEVICE *dptr)
{
if (nvr == NULL) {
    nvr = (uint8 *) calloc (NVRSIZE >> 2, sizeof (uint32));
    nvr_unit.filebuf = nvr;
    }
if (nvr == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

t_stat nvr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Non-volatile Memory (NVR)\n\n");
fprintf (st, "The NVR simulates %d bytes of battery-backed up memory.\n", NVRSIZE);
fprintf (st, "When the simulator starts, NVR is cleared to 0, and the battery-low indicator\n");
fprintf (st, "is set.  Alternately, NVR can be attached to a file.  This allows the NVR\n");
fprintf (st, "state to be preserved across simulator runs.  Successfully attaching an NVR\n");
fprintf (st, "image clears the battery-low indicator.\n\n");
return SCPE_OK;
}

/* NVR attach */

t_stat nvr_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
else {
    uptr->hwmark = (uint32) uptr->capac;
    wtc_set_valid ();
    }
return r;
}

/* NVR detach */

t_stat nvr_detach (UNIT *uptr)
{
t_stat r;

r = detach_unit (uptr);
if ((uptr->flags & UNIT_ATT) == 0) {
    uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
    wtc_set_invalid ();
    }
return r;
}

const char *nvr_description (DEVICE *dptr)
{
return "non-volatile memory";
}

/* Clock MxPR routines

   iccs_rd/wr   interval timer
*/

int32 iccs_rd (void)
{
return (clk_csr & CLKCSR_IMP);
}

void iccs_wr (int32 data)
{
if ((data & CSR_IE) == 0)
    tmr_int = 0;
if (data & CSR_DONE)
    sim_rtcn_tick_ack (20, TMR_CLK);
clk_csr = (clk_csr & ~CLKCSR_RW) | (data & CLKCSR_RW);
return;
}

/* Clock routines

   clk_svc          process event (clock tick)
   clk_reset        process reset
   clk_description  return device description
*/

t_stat clk_svc (UNIT *uptr)
{
int32 t;

if (clk_csr & CSR_IE)
    tmr_int = 1;
t = sim_rtcn_calb (clk_tps, TMR_CLK);                   /* calibrate clock */
sim_activate_after (uptr, 1000000/clk_tps);             /* reactivate unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
AIO_SET_INTERRUPT_LATENCY(tmr_poll*clk_tps);            /* set interrrupt latency */
return SCPE_OK;
}

/* Reset routine */

t_stat clk_reset (DEVICE *dptr)
{
int32 t;

clk_csr = 0;
tmr_int = 0;
t = sim_rtcn_init_unit (&clk_unit, clk_unit.wait, TMR_CLK);/* init 100Hz timer */
sim_activate_after (&clk_unit, 1000000/clk_tps);        /* activate 100Hz unit */
tmr_poll = t;                                           /* set tmr poll */
tmxr_poll = t * TMXR_MULT;                              /* set mux poll */
return SCPE_OK;
}

const char *clk_description (DEVICE *dptr)
{
return "100hz clock tick";
}

/* Dummy I/O space functions */

int32 ReadIO (uint32 pa, int32 lnt)
{
return 0;
}

void WriteIO (uint32 pa, int32 val, int32 lnt)
{
return;
}

int32 ReadIOU (uint32 pa, int32 lnt)
{
return 0;
}

void WriteIOU (uint32 pa, int32 val, int32 lnt)
{
return;
}
