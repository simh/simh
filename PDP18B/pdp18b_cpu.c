/* pdp18b_cpu.c: 18b PDP CPU simulator

   Copyright (c) 1993-2003, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu		PDP-4/7/9/15 central processor

   18-Feb-03	RMS	Fixed three EAE bugs (found by Hans Pufal)
   05-Oct-02	RMS	Added DIBs, device number support
   25-Jul-02	RMS	Added DECtape support for PDP-4
   06-Jan-02	RMS	Revised enable/disable support
   30-Dec-01	RMS	Added old PC queue
   30-Nov-01	RMS	Added extended SET/SHOW support
   25-Nov-01	RMS	Revised interrupt structure
   19-Sep-01	RMS	Fixed bug in EAE (found by Dave Conroy)
   17-Sep-01	RMS	Fixed typo in conditional
   10-Aug-01	RMS	Removed register from declarations
   17-Jul-01	RMS	Moved function prototype
   27-May-01	RMS	Added second Teletype support, fixed bug in API
   18-May-01	RMS	Added PDP-9,-15 API option
   16-May-01	RMS	Fixed bugs in protection checks
   26-Apr-01	RMS	Added device enable/disable support
   25-Jan-01	RMS	Added DECtape support
   18-Dec-00	RMS	Added PDP-9,-15 memm init register
   30-Nov-00	RMS	Fixed numerous PDP-15 bugs
   14-Apr-99	RMS	Changed t_addr to unsigned

   The 18b PDP family has five distinct architectural variants: PDP-1,
   PDP-4, PDP-7, PDP-9, and PDP-15.  Of these, the PDP-1 is so unique
   as to require a different simulator.  The PDP-4, PDP-7, PDP-9, and
   PDP-15 are "upward compatible", with each new variant adding
   distinct architectural features and incompatibilities.

   The register state for the 18b PDP's is:

   all			AC<0:17>	accumulator
   all			MQ<0:17>	multiplier-quotient
   all			L		link flag
   all			PC<0:x>		program counter
   all			IORS		I/O status register
   PDP-7, PDP-9		EXTM		extend mode
   PDP-15		BANKM		bank mode
   PDP-7		USMD		trap mode
   PDP-9, PDP-15	USMD		user mode
   PDP-9, PDP-15	BR		bounds register
   PDP-15		XR		index register
   PDP-15		LR		limit register
*/

/* The PDP-4, PDP-7, and PDP-9 have five instruction formats: memory
   reference, load immediate, I/O transfer, EAE, and operate.  The PDP-15
   adds a sixth, index operate, and a seventh, floating point.  The memory
   reference format for the PDP-4, PDP-7, and PDP-9, and for the PDP-15
   in bank mode, is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |     op    |in|               address                | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The PDP-15 in page mode trades an address bit for indexing capability:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |     op    |in| X|             address               | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:3>	mnemonic	action

   00		CAL		JMS with MA = 20
   04		DAC		M[MA] = AC
   10 		JMS		M[MA] = L'mem'user'PC, PC = MA + 1
   14		DZM		M[MA] = 0
   20		LAC		AC = M[MA]
   24 		XOR		AC = AC ^ M[MA]
   30 		ADD		L'AC = AC + M[MA] one's complement
   34 		TAD		L'AC = AC + M[MA]
   40		XCT		M[MA] is executed as an instruction
   44 		ISZ		M[MA] = M[MA] + 1, skip if M[MA] == 0
   50 		AND		AC = AC & M[MA]
   54		SAD		skip if AC != M[MA]
   60 		JMP		PC = MA

   On the PDP-4, PDP-7, and PDP-9, and the PDP-15 in bank mode, memory
   reference instructions can access an address space of 32K words.  The
   address space is divided into four 8K word fields.  An instruction can
   directly address, via its 13b address, the entire current field.  On the
   PDP-4, PDP-7, and PDP-9, if extend mode is off, indirect addresses access
   the current field; if on (or a PDP-15), they can access all 32K.

   On the PDP-15 in page mode, memory reference instructions can access
   an address space of 128K words.  The address is divided into four 32K
   word blocks, each of which consists of eight 4K pages.  An instruction
   can directly address, via its 12b address, the current page.  Indirect
   addresses can access the current block.  Indexed and autoincrement
   addresses can access all 128K.

   On the PDP-4 and PDP-7, if an indirect address in in locations 00010-
   00017 of any field, the indirect address is incremented and rewritten
   to memory before use.  On the PDP-9 and PDP-15, only locations 00010-
   00017 of field zero autoincrement; special logic will redirect indirect
   references to 00010-00017 to field zero, even if (on the PDP-9) extend
   mode is off.
*/

/* The EAE format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1|  |  |  |  |  |  |  |  |  |  |  |  |  |  | EAE
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		 |  |  |  |  |  |  |  |  |  |  |  |  |  |
		 |  |  |  |  |  |  |  |  |  |  |  |  |  +- or SC (3)
		 |  |  |  |  |  |  |  |  |  |  |  |  +---- or MQ (3)
		 |  |  |  |  |  |  |  |  |  |  |  +------- compl MQ (3)
		 |  |  |  |  |  |  |  |  \______________/
		 |  |  |  |  |  |  |  |         |
		 |  |  |  |  |  \_____/         +--------- shift count
		 |  |  |  |  |     |
		 |  |  |  |  |     +---------------------- EAE command (3)
		 |  |  |  |  +---------------------------- clear AC (2)
		 |  |  |  +------------------------------- or AC (2)
		 |  |  +---------------------------------- load EAE sign (1)
		 |  +------------------------------------- clear MQ (1)
		 +---------------------------------------- load link (1)

   The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0  0|      device     | sdv |cl|  pulse | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified pulse to the
   specified I/O device and sub-device.  The I/O device may take data
   from the AC, return data to the AC, initiate or cancel operations,
   or skip on status.  On the PDP-4, PDP-7, and PDP-9, bits <4:5>
   were designated as subdevice bits but were never used; the PDP-15
   requires them to be zero.

   On the PDP-15, the floating point format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0  1|            subopcode              | floating point
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|                   address                        |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   Indirection is always single level.
*/

/* On the PDP-15, the index operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  1| subopcode |        immediate         | index operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The index operate instructions provide various operations on the
   index and limit registers.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  0|  |  |  |  |  |  |  |  |  |  |  |  |  | operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
		    |  |  |  |  |  |  |  |  |  |  |  |  |
		    |  |  |  |  |  |  |  |  |  |  |  |  +- CMA (3)
		    |  |  |  |  |  |  |  |  |  |  |  +---- CML (3)
		    |  |  |  |  |  |  |  |  |  |  +------- OAS (3)
		    |  |  |  |  |  |  |  |  |  +---------- RAL (3)
		    |  |  |  |  |  |  |  |  +------------- RAR (3)
		    |  |  |  |  |  |  |  +---------------- HLT (4)
		    |  |  |  |  |  |  +------------------- SMA (1)
		    |  |  |  |  |  +---------------------- SZA (1)
		    |  |  |  |  +------------------------- SNL (1)
		    |  |  |  +---------------------------- invert skip (1)
		    |  |  +------------------------------- rotate twice (2)
		    |  +---------------------------------- CLL (2)
		    +------------------------------------- CLA (2)

   The operate instruction can be microprogrammed to perform operations
   on the AC and link.

   The load immediate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  1|            immediate                 | LAW
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>	mnemonic	action

   76		LAW		AC = IR
*/

/* This routine is the instruction decode routine for the 18b PDP's.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

	HALT instruction
	breakpoint encountered
	unimplemented instruction and STOP_INST flag set
	nested XCT's
	I/O error in I/O simulator

   2. Interrupts.  Interrupt requests are maintained in the int_hwre
      array.  int_hwre[0:3] corresponds to API levels 0-3; int_hwre[4]
      holds PI requests.

   3. Arithmetic.  The 18b PDP's implements both 1's and 2's complement
      arithmetic for signed numbers.  In 1's complement arithmetic, a
      negative number is represented by the complement (XOR 0777777) of
      its absolute value.  Addition of 1's complement numbers requires
      propagating the carry out of the high order bit back to the low
      order bit.

   4. Adding I/O devices.  Three modules must be modified:

	pdp18b_defs.h	add interrupt request definition
	pdp18b_sys.c	add sim_devices table entry
*/

#include "pdp18b_defs.h"

#define PCQ_SIZE	64				/* must be 2**n */
#define PCQ_MASK	(PCQ_SIZE - 1)
#define PCQ_ENTRY	pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC
#define UNIT_V_NOEAE	(UNIT_V_UF)			/* EAE absent */
#define UNIT_V_NOAPI	(UNIT_V_UF+1)			/* API absent */
#define UNIT_V_MSIZE	(UNIT_V_UF+2)			/* dummy mask */
#define UNIT_NOEAE	(1 << UNIT_V_NOEAE)
#define UNIT_NOAPI	(1 << UNIT_V_NOAPI)

#define UNIT_MSIZE	(1 << UNIT_V_MSIZE)
#if defined (PDP4)
#define EAE_DFLT	UNIT_NOEAE
#else
#define EAE_DFLT	0
#endif
#if defined (PDP4) || defined (PDP7)
#define API_DFLT	UNIT_NOAPI
#else
#define API_DFLT	UNIT_NOAPI			/* for now */
#endif

int32 M[MAXMEMSIZE] = { 0 };				/* memory */
int32 saved_LAC = 0;					/* link'AC */
int32 saved_MQ = 0;					/* MQ */
int32 saved_PC = 0;					/* PC */
int32 iors = 0;						/* IORS */
int32 ion = 0;						/* int on */
int32 ion_defer = 0;					/* int defer */
int32 int_pend = 0;					/* int pending */
int32 int_hwre[API_HLVL+1] = { 0 };			/* int requests */
int32 api_enb = 0;					/* API enable */
int32 api_req = 0;					/* API requests */
int32 api_act = 0;					/* API active */
int32 memm = 0;						/* mem mode */
#if defined (PDP15)
int32 memm_init = 1;					/* mem init */
#else
int32 memm_init = 0;
#endif
int32 usmd = 0;						/* user mode */
int32 usmdbuf = 0;					/* user mode buffer */
int32 trap_pending = 0;					/* trap pending */
int32 emir_pending = 0;					/* emir pending */
int32 rest_pending = 0;					/* restore pending */
int32 BR = 0;						/* mem mgt bounds */
int32 nexm = 0;						/* nx mem flag */
int32 prvn = 0;						/* priv viol flag */
int32 SC = 0;						/* shift count */
int32 eae_ac_sign = 0;					/* EAE AC sign */
int32 SR = 0;						/* switch register */
int32 XR = 0;						/* index register */
int32 LR = 0;						/* limit register */
int32 stop_inst = 0;					/* stop on rsrv inst */
int32 xct_max = 16;					/* nested XCT limit */
#if defined (PDP15)
int32 pcq[PCQ_SIZE] = { 0 };				/* PC queue */
#else
int16 pcq[PCQ_SIZE] = { 0 };				/* PC queue */
#endif
int32 pcq_p = 0;					/* PC queue ptr */
REG *pcq_r = NULL;					/* PC queue reg ptr */

extern int32 sim_int_char;
extern int32 sim_interval;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ;	/* breakpoint info */
extern DEVICE *sim_devices[];
extern FILE *sim_log;

t_bool build_dev_tab (void);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 upd_iors (void);
int32 api_eval (int32 *pend);

extern clk (int32 pulse, int32 AC);

int32 (*dev_tab[DEV_MAX])(int32 pulse, int32 AC);	/* device dispatch */

int32 (*dev_iors[DEV_MAX])(void);			/* IORS dispatch */

static const int32 api_ffo[256] = {
 8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  };

static const int32 api_vec[API_HLVL][32] = {
 { ACH_PWRFL },						/* API 0 */
 { ACH_DTA, ACH_MTA, ACH_DRM, ACH_RF, ACH_RP, ACH_RB },	/* API 1 */
 { ACH_PTR, ACH_LPT, ACH_LPT },				/* API 2 */
 { ACH_CLK, ACH_TTI1, ACH_TTO1 }  };			/* API 3 */

/* CPU data structures

   cpu_dev	CPU device descriptor
   cpu_unit	CPU unit
   cpu_reg	CPU register list
   cpu_mod	CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK + EAE_DFLT + API_DFLT,
		MAXMEMSIZE) };

REG cpu_reg[] = {
	{ ORDATA (PC, saved_PC, ADDRSIZE) },
	{ ORDATA (AC, saved_LAC, 18) },
	{ FLDATA (L, saved_LAC, 18) },
#if !defined (PDP4)
	{ ORDATA (MQ, saved_MQ, 18) },
	{ ORDATA (SC, SC, 6) },
	{ FLDATA (EAE_AC_SIGN, eae_ac_sign, 18) },
#endif
	{ ORDATA (SR, SR, 18) },
	{ ORDATA (IORS, iors, 18), REG_RO },
	{ BRDATA (INT, int_hwre, 8, 32, API_HLVL+1), REG_RO },
	{ FLDATA (ION, ion, 0) },
	{ ORDATA (ION_DELAY, ion_defer, 2) },
#if defined (PDP7) 
	{ FLDATA (TRAPM, usmd, 0) },
	{ FLDATA (TRAPP, trap_pending, 0) },
	{ FLDATA (EXTM, memm, 0) },
	{ FLDATA (EXTM_INIT, memm_init, 0) },
	{ FLDATA (EMIRP, emir_pending, 0) },
#endif
#if defined (PDP9)
	{ FLDATA (APIENB, api_enb, 0) },
	{ ORDATA (APIREQ, api_req, 8) },
	{ ORDATA (APIACT, api_act, 8) },
	{ ORDATA (BR, BR, ADDRSIZE) },
	{ FLDATA (USMD, usmd, 0) },
	{ FLDATA (USMDBUF, usmdbuf, 0) },
	{ FLDATA (NEXM, nexm, 0) },
	{ FLDATA (PRVN, prvn, 0) },
	{ FLDATA (TRAPP, trap_pending, 0) },
	{ FLDATA (EXTM, memm, 0) },
	{ FLDATA (EXTM_INIT, memm_init, 0) },
	{ FLDATA (EMIRP, emir_pending, 0) },
	{ FLDATA (RESTP, rest_pending, 0) },
	{ FLDATA (PWRFL, int_hwre[API_PWRFL], INT_V_PWRFL) },
#endif
#if defined (PDP15)
	{ FLDATA (APIENB, api_enb, 0) },
	{ ORDATA (APIREQ, api_req, 8) },
	{ ORDATA (APIACT, api_act, 8) },
	{ ORDATA (XR, XR, 18) },
	{ ORDATA (LR, LR, 18) },
	{ ORDATA (BR, BR, ADDRSIZE) },
	{ FLDATA (USMD, usmd, 0) },
	{ FLDATA (USMDBUF, usmdbuf, 0) },
	{ FLDATA (NEXM, nexm, 0) },
	{ FLDATA (PRVN, prvn, 0) },
	{ FLDATA (TRAPP, trap_pending, 0) },
	{ FLDATA (BANKM, memm, 0) },
	{ FLDATA (BANKM_INIT, memm_init, 0) },
	{ FLDATA (RESTP, rest_pending, 0) },
	{ FLDATA (PWRFL, int_hwre[API_PWRFL], INT_V_PWRFL) },
#endif
	{ BRDATA (PCQ, pcq, 8, ADDRSIZE, PCQ_SIZE), REG_RO+REG_CIRC },
	{ ORDATA (PCQP, pcq_p, 6), REG_HRO },
	{ FLDATA (STOP_INST, stop_inst, 0) },
	{ DRDATA (XCT_MAX, xct_max, 8), PV_LEFT + REG_NZ },
	{ ORDATA (WRU, sim_int_char, 8) },
	{ NULL }  };

MTAB cpu_mod[] = {
#if !defined (PDP4)
	{ UNIT_NOEAE, UNIT_NOEAE, "no EAE", "NOEAE", NULL },
	{ UNIT_NOEAE, 0, "EAE", "EAE", NULL },
#else
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
#endif
#if defined (PDP9) || defined (PDP15)
	{ UNIT_NOAPI, UNIT_NOAPI, "no API", "NOAPI", NULL },
	{ UNIT_NOAPI, 0, "API", "API", NULL },
#endif
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
#if (MAXMEMSIZE > 8192)
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
#endif
#if (MAXMEMSIZE > 32768)
	{ UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
	{ UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
	{ UNIT_MSIZE, 81920, NULL, "80K", &cpu_set_size },
	{ UNIT_MSIZE, 98304, NULL, "96K", &cpu_set_size },
	{ UNIT_MSIZE, 114688, NULL, "112K", &cpu_set_size },
	{ UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
#endif
	{ 0 }  };

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, ADDRSIZE, 1, 8, 18,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL };

t_stat sim_instr (void)
{
int32 PC, LAC, MQ;
int32 api_int, api_cycle, skp;
int32 iot_data, device, pulse;
t_stat reason;
extern UNIT clk_unit;

#define JMS_WORD(t)	(((LAC & 01000000) >> 1) | ((memm & 1) << 16) | \
			 (((t) & 1) << 15) | ((PC) & 077777))
#define INCR_ADDR(x)	(((x) & epcmask) | (((x) + 1) & damask))
#define SEXT(x)		((int) (((x) & 0400000)? (x) | ~0777777: (x) & 0777777))

/* The following macros implement addressing.  They account for autoincrement
   addressing, extended addressing, and memory protection, if it exists.

   CHECK_AUTO_INC		check auto increment
   INDIRECT			indirect addressing
   CHECK_INDEX			check indexing
   CHECK_ADDR_R			check address for read
   CHECK_ADDR_W			check address for write

   On the PDP-4 and PDP-7,
	There are autoincrement locations in every field.  If a field
		does not exist, it is impossible to generate an
		autoincrement reference (all instructions are CAL).
	Indirect addressing range is determined by extend mode.
	There is no indexing.
	There is no memory protection, nxm reads zero and ignores writes.
*/

#if defined (PDP4) || defined (PDP7)
#define CHECK_AUTO_INC \
	if ((IR & 017770) == 010) M[MA] = (M[MA] + 1) & 0777777
#define INDIRECT \
	MA = memm? M[MA] & IAMASK: (MA & epcmask) | (M[MA] & damask)
#define CHECK_INDEX 			/* no indexing capability */
#define CHECK_ADDR_R(x)			/* no read protection */
#define CHECK_ADDR_W(x) \
	if (!MEM_ADDR_OK (x)) break
#endif

/* On the PDP-9,
	The autoincrement registers are in field zero only.  Regardless
		of extend mode, indirect addressing through 00010-00017
		will access absolute locations 00010-00017.
	Indirect addressing range is determined by extend mode.  If
		extend mode is off, and autoincrementing is used, the
		resolved address is in bank 0 (KG09B maintenance manual).
	There is no indexing.
	Memory protection is implemented for foreground/background operation.
*/

#if defined (PDP9)
#define CHECK_AUTO_INC \
	if ((IR & 017770) == 010) { \
	    MA = MA & 017; \
	    M[MA] = (M[MA] + 1) & 0777777;  }
#define INDIRECT \
	MA = memm? M[MA] & IAMASK: (MA & epcmask) | (M[MA] & damask)
#define CHECK_ADDR_R(x) \
	if (usmd) { \
	    if (!MEM_ADDR_OK (x)) { \
		nexm = prvn = trap_pending = 1; \
		break;  } \
	    if ((x) < BR) { \
		prvn = trap_pending = 1;  \
		break;  }  }  \
	if (!MEM_ADDR_OK (x)) nexm = 1
#define CHECK_INDEX 			/* no indexing capability */
#define CHECK_ADDR_W(x) \
	CHECK_ADDR_R (x); \
	if (!MEM_ADDR_OK (x)) break
#endif

/*  On the PDP-15,
	The autoincrement registers are in page zero only.  Regardless
		of bank mode, indirect addressing through 00010-00017
		will access absolute locations 00010-00017.
	Indirect addressing range is determined by autoincrementing.
	Indexing is available if bank mode is off.
	Memory protection is implemented for foreground/background operation.
*/

#if defined (PDP15)
#define CHECK_AUTO_INC \
	if ((IR & damask & ~07) == 00010) { \
	    MA = MA & 017; \
	    M[MA] = (M[MA] + 1) & 0777777;  }
#define INDIRECT \
	if (rest_pending) { \
	    rest_pending = 0; \
	    LAC = ((M[MA] << 1) & 01000000) | (LAC & 0777777); \
	    memm = (M[MA] >> 16) & 1; \
	    usmd = (M[MA] >> 15) & 1; }  \
	MA = ((IR & damask & ~07) != 00010)? \
	    (PC & BLKMASK) | (M[MA] & IAMASK): (M[MA] & ADDRMASK); \
	damask = memm? 017777: 07777; \
	epcmask = ADDRMASK & ~damask
#define CHECK_INDEX \
	if ((IR & 0010000) && (memm == 0)) MA = (MA + XR) & ADDRMASK
#define CHECK_ADDR_R(x) \
	if (usmd) { \
	    if (!MEM_ADDR_OK (x)) { \
		nexm = prvn = trap_pending = 1; \
		break;  } \
	    if ((x) < BR) { \
		prvn = trap_pending = 1;  \
		break;  }  }  \
	if (!MEM_ADDR_OK (x)) nexm = 1
#define CHECK_ADDR_W(x) \
	CHECK_ADDR_R (x); \
	if (!MEM_ADDR_OK (x)) break
#endif

/* Restore register state */

#if defined (PDP15)
int32 epcmask, damask;

damask = memm? 017777: 07777;				/* set dir addr mask */
epcmask = ADDRMASK & ~damask;				/* extended PC mask */

#else
#define damask	017777					/* direct addr mask */
#define epcmask	(ADDRMASK & ~damask)			/* extended PC mask */
#endif

if (build_dev_tab ()) return SCPE_STOP;			/* build, chk tables */
PC = saved_PC & ADDRMASK;				/* load local copies */
LAC = saved_LAC & 01777777;
MQ = saved_MQ & 0777777;
reason = 0;
sim_rtc_init (clk_unit.wait);				/* init calibration */
if (cpu_unit.flags & UNIT_NOAPI) api_enb = api_req = api_act = 0;
api_int = api_eval (&int_pend);				/* eval API */
api_cycle = 0;						/* not API cycle */

/* Main instruction fetch/decode loop: check trap and interrupt */

while (reason == 0) {					/* loop until halted */
int32 IR, MA, esc, t, xct_count;
int32 link_init, fill;

if (sim_interval <= 0) {				/* check clock queue */
	if (reason = sim_process_event ()) break;
	api_int = api_eval (&int_pend);  }		/* eval API */

/* Protection traps work like interrupts, with these quirks:

   PDP-7		extend mode forced on, M[0] = PC, PC = 2
   PDP-9		extend mode ???, M[0/20] = PC, PC = 0/21
   PDP-15		bank mode unchanged, M[0/20] = PC, PC = 0/21
*/

#if defined (PDP7)
if (trap_pending) {					/* trap pending? */
	PCQ_ENTRY;					/* save old PC */
	M[0] = JMS_WORD (1);				/* save state */
	PC = 2;						/* fetch next from 2 */
	ion = 0;					/* interrupts off */
	memm = 1;					/* extend on */
	emir_pending = trap_pending = 0;		/* emir, trap off */
	usmd = 0;  }					/* protect off */
#endif
#if defined (PDP9) || defined (PDP15)
if (trap_pending) {					/* trap pending? */
	PCQ_ENTRY;					/* save old PC */
	MA = ion? 0: 020;				/* save in 0 or 20 */
	M[MA] = JMS_WORD (1);				/* save state */
	PC = MA + 1;					/* fetch next */
	ion = 0;					/* interrupts off */
	emir_pending = rest_pending = trap_pending = 0;	/* emir,rest,trap off */
	usmd = 0;  }					/* protect off */

/* PDP-9 and PDP-15 automatic priority interrupt (API) */

if (api_int && !ion_defer) {				/* API intr? */
	int32 i, lvl = api_int - 1;			/* get req level */
	api_act = api_act | (0200 >> lvl);		/* set level active */
	if (lvl >= API_HLVL) {				/* software req? */
	    MA = ACH_SWRE + lvl - API_HLVL;		/* vec = 40:43 */
	    api_req = api_req & ~(0200 >> lvl);  }	/* remove request */
	else {
	    MA = 0;					/* assume fails */
	    for (i = 0; i < 32; i++) {			/* loop hi to lo */
	    if ((int_hwre[lvl] >> i) & 1) {		/* int req set? */
		MA = api_vec[lvl][i];			/* get vector */
		break;  }  }  }				/* and stop */
	if (MA == 0) {					/* bad channel? */
	    reason = STOP_API;				/* API error */
	    break;  }
	api_int = api_eval (&int_pend);			/* no API int */
	api_cycle = 1;					/* in API cycle */
	emir_pending = rest_pending = 0;		/* emir, restore off */
	xct_count = 0;
	goto xct_instr;  }

/* Standard program interrupt */

if (!(api_enb && api_act) && ion && !ion_defer && int_pend) {
#else
if (ion && !ion_defer && int_pend) {			/* interrupt? */
#endif
	PCQ_ENTRY;					/* save old PC */
	M[0] = JMS_WORD (usmd);				/* save state */
	PC = 1;						/* fetch next from 1 */
	ion = 0;					/* interrupts off */
#if !defined (PDP15)					/* except PDP-15, */
	memm = 0;					/* extend off */
#endif
	emir_pending = rest_pending = 0;		/* emir, restore off */
	usmd = 0;  }					/* protect off */

/* Breakpoint */

if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {	/* breakpoint? */
	reason = STOP_IBKPT;				/* stop simulation */
	break;  }

/* Fetch, decode instruction */

#if defined (PDP9) || defined (PDP15)
if (usmd) {						/* user mode? */
	if (!MEM_ADDR_OK (PC)) {			/* nxm? */
	    nexm = prvn = trap_pending = 1;		/* abort fetch */
	    continue;  }
	if (PC < BR) {					/* bounds viol? */
	    prvn = trap_pending = 1;			/* abort fetch */
	    continue;  }  }
else if (!MEM_ADDR_OK (PC)) nexm = 1;			/* flag nxm */
if (!ion_defer) usmd = usmdbuf;				/* no IOT? load usmd */
#endif
xct_count = 0;						/* track nested XCT's */
MA = PC;						/* fetch at PC */
PC = INCR_ADDR (PC);					/* increment PC */

xct_instr:						/* label for XCT */
IR = M[MA];						/* fetch instruction */
if (ion_defer) ion_defer = ion_defer - 1;		/* count down defer */
if (sim_interval) sim_interval = sim_interval - 1;
MA = (MA & epcmask) | (IR & damask);			/* effective address */
switch ((IR >> 13) & 037) {				/* decode IR<0:4> */

/* LAC: opcode 20 */

case 011:						/* LAC, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 010:						/* LAC, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	LAC = (LAC & 01000000) | M[MA];
	break;

/* DAC: opcode 04 */

case 003:						/* DAC, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 002:						/* DAC, dir */
	CHECK_INDEX;
	CHECK_ADDR_W (MA);
	M[MA] = LAC & 0777777;
	break;

/* DZM: opcode 14 */

case 007:						/* DZM, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 006:						/* DZM, direct */
	CHECK_INDEX;
	CHECK_ADDR_W (MA);
	M[MA] = 0;
	break;

/* AND: opcode 50 */

case 025:						/* AND, ind */
	CHECK_AUTO_INC;
	INDIRECT;
case 024:						/* AND, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	LAC = LAC & (M[MA] | 01000000);
	break;

/* XOR: opcode 24 */

case 013:						/* XOR, ind */
	CHECK_AUTO_INC;
	INDIRECT;
case 012:						/* XOR, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	LAC = LAC ^ M[MA];
	break;

/* ADD: opcode 30 */

case 015:						/* ADD, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 014:						/* ADD, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	t = (LAC & 0777777) + M[MA];
	if (t > 0777777) t = (t + 1) & 0777777;		/* end around carry */
	if (((~LAC ^ M[MA]) & (LAC ^ t)) & 0400000)	/* overflow? */
		LAC = 01000000 | t;			/* set link */
	else LAC = (LAC & 01000000) | t;
	break;

/* TAD: opcode 34 */

case 017:						/* TAD, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 016:						/* TAD, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	LAC = (LAC + M[MA]) & 01777777;
	break;

/* ISZ: opcode 44 */

case 023:						/* ISZ, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 022:						/* ISZ, dir */
	CHECK_INDEX;
	CHECK_ADDR_W (MA);
	M[MA] = (M[MA] + 1) & 0777777;
	if (M[MA] == 0) PC = INCR_ADDR (PC);
	break;

/* SAD: opcode 54 */

case 027:						/* SAD, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 026:						/* SAD, dir */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	if ((LAC & 0777777) != M[MA]) PC = INCR_ADDR (PC);
	break;

/* XCT: opcode 40 */

case 021:						/* XCT, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 020:						/* XCT, dir  */
	CHECK_INDEX;
	CHECK_ADDR_R (MA);
	if (usmd && (xct_count != 0)) {			/* trap and chained? */
	    prvn = trap_pending = 1;
	    break;  }
	if (xct_count >= xct_max) {			/* too many XCT's? */
	    reason = STOP_XCT;
	    break;  }
	xct_count = xct_count + 1;			/* count XCT's */
#if defined (PDP9)
	ion_defer = 1;					/* defer intr */
#endif
	goto xct_instr;					/* go execute */

/* CAL: opcode 00 

   On the PDP-4 and PDP-7, CAL (I) is exactly the same as JMS (I) 20
   On the PDP-9 and PDP-15, CAL clears user mode
   On the PDP-9 and PDP-15 with API, CAL activates level 4
   On the PDP-15, CAL goes to absolute 20, regardless of mode
*/

case 001: case 000:					/* CAL */
	t = usmd;
#if defined (PDP15)
	MA = 020;
#else
	MA = (memm? 0: PC & epcmask) | 020;		/* MA = 20 */
#endif
#if defined (PDP9) || defined (PDP15)
	usmd = 0;					/* clear user mode */
	if ((cpu_unit.flags & UNIT_NOAPI) == 0) {	/* if API, act lvl 4 */
	    api_act = api_act | 010;
	    api_int = api_eval (&int_pend);  }
#endif
	if (IR & 0020000) { INDIRECT;  }		/* indirect? */
	CHECK_ADDR_W (MA);
	PCQ_ENTRY;
	M[MA] = JMS_WORD (t);				/* save state */
	PC = INCR_ADDR (MA);
	break;

/* JMS: opcode 010 */

case 005:						/* JMS, indir */
	CHECK_AUTO_INC;
	INDIRECT;
case 004:						/* JMS, dir */
	CHECK_INDEX;
	CHECK_ADDR_W (MA);
	PCQ_ENTRY;
	M[MA] = JMS_WORD (usmd);			/* save state */
	PC = INCR_ADDR (MA);
	break;

/* JMP: opcode 60

   Restore quirks:
	On the PDP-7 and PDP-9, EMIR can only clear extend
	On the PDP-15, any I triggers restore, but JMP I is conventional
*/

case 031:						/* JMP, indir */
CHECK_AUTO_INC;					/* check auto inc */
#if defined (PDP7) || defined (PDP9)
	if (emir_pending && (((M[MA] >> 16) & 1) == 0)) memm = 0;
#endif
#if defined (PDP9)
	if (rest_pending) {				/* restore pending? */
	    LAC = ((M[MA] << 1) & 01000000) | (LAC & 0777777);
	    memm = (M[MA] >> 16) & 1;
	    usmd = (M[MA] >> 15) & 1;  }
#endif
	INDIRECT;					/* complete indirect */
	emir_pending = rest_pending = 0;
case 030:						/* JMP, dir */
	CHECK_INDEX;
	PCQ_ENTRY;					/* save old PC */
	PC = MA;
	break;

/* OPR: opcode 74 */

case 037:						/* OPR, indir */
	LAC = (LAC & 01000000) | IR;			/* LAW */
	break;

case 036:						/* OPR, dir */
	skp = 0;					/* assume no skip */
	switch ((IR >> 6) & 017) {			/* decode IR<8:11> */
	case 0:	 					/* nop */
	    break;
	case 1: 					/* SMA */
	    if ((LAC & 0400000) != 0) skp = 1;
	    break;
	case 2: 					/* SZA */
	    if ((LAC & 0777777) == 0) skp = 1;
	    break;
	case 3:						/* SZA | SMA */
	    if (((LAC & 0777777) == 0) || ((LAC & 0400000) != 0))
		skp = 1; 
	    break;
	case 4: 					/* SNL */
	    if (LAC >= 01000000) skp = 1;
	    break;
	case 5:						/* SNL | SMA */
	    if (LAC >= 0400000) skp = 1;
	    break;
	case 6:						/* SNL | SZA */
	    if ((LAC >= 01000000) || (LAC == 0)) skp = 1;
	    break;
	case 7:						/* SNL | SZA | SMA */
	    if ((LAC >= 0400000) || (LAC == 0)) skp = 1;
	    break;
	case 010:					/* SKP */
	    skp = 1;
	    break;
	case 011: 					/* SPA */
	    if ((LAC & 0400000) == 0) skp = 1;
	    break;
	case 012: 					/* SNA */
	    if ((LAC & 0777777) != 0) skp = 1;
	    break;
	case 013:					/* SNA & SPA */
	    if (((LAC & 0777777) != 0) && ((LAC & 0400000) == 0))
		skp = 1;
	    break;
	case 014: 					/* SZL */
	    if (LAC < 01000000) skp = 1;
	    break;
	case 015:					/* SZL & SPA */
	    if (LAC < 0400000) skp = 1;
	    break;
	case 016:					/* SZL & SNA */
	    if ((LAC < 01000000) && (LAC != 0)) skp = 1;
	    break;
	case 017:					/* SZL & SNA & SPA */
	    if ((LAC < 0400000) && (LAC != 0)) skp = 1;
	    break;  }					/* end switch skips */

/* OPR, continued */

	switch (((IR >> 9) & 014) | (IR & 03)) {	/* IR<5:6,16:17> */
	case 0:						/* NOP */
	    break;
	case 1:						/* CMA */
	    LAC = LAC ^ 0777777;
	    break;
	case 2:						/* CML */
	    LAC = LAC ^ 01000000;
	    break;
	case 3:						/* CML CMA */
	    LAC = LAC ^ 01777777;
	    break;
	case 4:						/* CLL */
	    LAC = LAC & 0777777;
	    break;
	case 5:						/* CLL CMA */
	    LAC = (LAC & 0777777) ^ 0777777;
	    break;
	case 6:						/* CLL CML = STL */
	    LAC = LAC | 01000000;
	    break;
	case 7:						/* CLL CML CMA */
	    LAC = (LAC | 01000000) ^ 0777777;
	    break;
	case 010:					/* CLA */
	    LAC = LAC & 01000000;
	    break;
	case 011:					/* CLA CMA = STA */
	    LAC = LAC | 0777777;
	    break;
	case 012:					/* CLA CML */
	    LAC = (LAC & 01000000) ^ 01000000;
	    break;
	case 013:					/* CLA CML CMA */
	    LAC = (LAC | 0777777) ^ 01000000;
	    break;
	case 014:					/* CLA CLL */
	    LAC = 0;
	    break;
	case 015:					/* CLA CLL CMA */
	    LAC = 0777777;
	    break;
	case 016:					/* CLA CLL CML */
	    LAC = 01000000;
	    break;
	case 017:					/* CLA CLL CML CMA */
	    LAC = 01777777;
	    break;  }					/* end decode */

/* OPR, continued */

	if (IR & 0000004) {				/* OAS */
#if defined (PDP9) || defined (PDP15)
	    if (usmd) prvn = trap_pending = 1;
	    else
#endif
		LAC = LAC | SR;  }

	switch (((IR >> 8) & 04) | ((IR >> 3) & 03)) {	/* decode IR<7,13:14> */
	case 1:						/* RAL */
	    LAC = ((LAC << 1) | (LAC >> 18)) & 01777777;
		break;
	case 2:						/* RAR */
	    LAC = ((LAC >> 1) | (LAC << 18)) & 01777777;
	    break;
	case 3:						/* RAL RAR */
#if defined (PDP15)					/* PDP-15 */
	    LAC = (LAC + 1) & 01777777;			/* IAC */
#else							/* PDP-4,-7,-9 */
	    reason = stop_inst;				/* undefined */
#endif
	    break;
	case 5:						/* RTL */
	    LAC = ((LAC << 2) | (LAC >> 17)) & 01777777;
	    break;
	case 6:						/* RTR */
	    LAC = ((LAC >> 2) | (LAC << 17)) & 01777777;
	    break;
	case 7:						/* RTL RTR */
#if defined (PDP15)					/* PDP-15 */
	    LAC = ((LAC >> 9) & 0777) | ((LAC & 0777) << 9) |
		(LAC & 01000000);			/* BSW */
#else							/* PDP-4,-7,-9 */
	    reason = stop_inst;				/* undefined */
#endif
	    break;  }					/* end switch rotate */

	if (IR & 0000040) {				/* HLT */
	    if (usmd) prvn = trap_pending = 1;
	    else reason = STOP_HALT;  }
	if (skp && !prvn) PC = INCR_ADDR (PC);		/* if skip, inc PC */
	break;						/* end OPR */

/* EAE: opcode 64 

   The EAE is microprogrammed to execute variable length signed and
   unsigned shift, multiply, divide, and normalize.  Most commands are
   controlled by a six bit step counter (SC).  In the hardware, the step
   counter is complemented on load and then counted up to zero; timing
   guarantees an initial increment, which completes the two's complement
   load.  In the simulator, the SC is loaded normally and then counted
   down to zero; the read SC command compensates.
*/

case 033: case 032:					/* EAE */
	if (cpu_unit.flags & UNIT_NOEAE) break;		/* disabled? */
	if (IR & 0020000)				/* IR<4>? AC0 to L */
	    LAC = ((LAC << 1) & 01000000) | (LAC & 0777777);
	if (IR & 0010000) MQ = 0;			/* IR<5>? clear MQ */
	if ((IR & 0004000) && (LAC & 0400000))		/* IR<6> and minus? */
	    eae_ac_sign = 01000000;			/* set eae_ac_sign */
	else eae_ac_sign = 0;				/* if not, unsigned */
	if (IR & 0002000) MQ = (MQ | LAC) & 0777777;	/* IR<7>? or AC */
	else if (eae_ac_sign) LAC = LAC ^ 0777777;	/* if not, |AC| */
	if (IR & 0001000) LAC = LAC & 01000000;		/* IR<8>? clear AC */
	link_init = LAC & 01000000;			/* link temporary */
	fill = link_init? 0777777: 0;			/* fill = link */
	esc = IR & 077;					/* get eff SC */

	switch ((IR >> 6) & 07) {			/* case on IR<9:11> */
	case 0:						/* setup */
	    if (IR & 04) MQ = MQ ^ 0777777;		/* IR<15>? ~MQ */
	    if (IR & 02) LAC = LAC | MQ;		/* IR<16>? or MQ */
	    if (IR & 01) LAC = LAC | ((-SC) & 077);	/* IR<17>? or SC */
	    break;

	case 1:						/* multiply */
	    CHECK_ADDR_R (PC);				/* validate PC */
	    MA = M[PC];					/* get next word */
	    PC = INCR_ADDR (PC);			/* increment PC */
	    if (eae_ac_sign) MQ = MQ ^ 0777777;		/* EAE AC sign? ~MQ */
	    LAC = LAC & 0777777;			/* clear link */
	    SC = esc;					/* init SC */
	    do {					/* loop */
		if (MQ & 1) LAC = LAC + MA;		/* MQ<17>? add */
		MQ = (MQ >> 1) | ((LAC & 1) << 17);
		LAC = LAC >> 1;				/* shift AC'MQ right */
		SC = (SC - 1) & 077;  }			/* decrement SC */
	    while (SC != 0);				/* until SC = 0 */
	    if (eae_ac_sign ^ link_init) {		/* result negative? */
		LAC = LAC ^ 0777777;
		MQ = MQ ^ 0777777;  }
	    break;

/* EAE, continued

   Divide uses a non-restoring divide.  This code duplicates the PDP-7
   algorithm, except for its use of two's complement arithmetic instead
   of 1's complement.

   The quotient is generated in one's complement form; therefore, the
   quotient is complemented if the input operands had the same sign
   (that is, if the quotient is positive).
*/

	case 3:						/* divide */
	    CHECK_ADDR_R (PC);				/* validate PC */
	    MA = M[PC];					/* get next word */
	    PC = INCR_ADDR (PC);			/* increment PC */
	    if (eae_ac_sign) MQ = MQ ^ 0777777;		/* EAE AC sign? ~MQ */
	    if ((LAC & 0777777) >= MA) {		/* overflow? */
		LAC = (LAC - MA) | 01000000;		/* set link */
		break;  }
	    LAC = LAC & 0777777;			/* clear link */
	    t = 0;					/* init loop */
	    SC = esc;					/* init SC */
	    do {					/* loop */
		if (t) LAC = (LAC + MA) & 01777777;
		else LAC = (LAC - MA) & 01777777;
		t = (LAC >> 18) & 1;			/* quotient bit */
		if (SC > 1) LAC =			/* skip if last */
		    ((LAC << 1) | (MQ >> 17)) & 01777777;
		MQ = ((MQ << 1) | t) & 0777777;		/* shift in quo bit */
		SC = (SC - 1) & 077;  }			/* decrement SC */
	    while (SC != 0);				/* until SC = 0 */
	    if (t) LAC = (LAC + MA) & 01777777;
	    if (eae_ac_sign) LAC = LAC ^ 0777777;	/* sgn rem = sgn divd */
	    if ((eae_ac_sign ^ link_init) == 0) MQ = MQ ^ 0777777;
	    break;

/* EAE, continued

   EAE shifts, whether left or right, fill from the link.  If the
   operand sign has been copied to the link, this provides correct
   sign extension for one's complement numbers.
*/

	case 4:						/* normalize */
#if defined (PDP15)
	    if (!usmd) ion_defer = 2;			/* free cycles */
#endif
	    for (SC = esc; ((LAC & 0400000) == ((LAC << 1) & 0400000)); ) {
		LAC = (LAC << 1) | ((MQ >> 17) & 1);
		MQ = (MQ << 1) | (link_init >> 18);
		SC = (SC - 1) & 077;
		if (SC == 0) break;  }
	    LAC = link_init | (LAC & 0777777);		/* trim AC, restore L */
	    MQ = MQ & 0777777;				/* trim MQ */
	    SC = SC & 077;				/* trim SC */
	    break;

	case 5:						/* long right shift */
	    if (esc < 18) {
		MQ = ((LAC << (18 - esc)) | (MQ >> esc)) & 0777777;
		LAC = ((fill << (18 - esc)) | (LAC >> esc)) & 01777777;  }
	    else {
	    	if (esc < 36) MQ =
		    ((fill << (36 - esc)) | (LAC >> (esc - 18))) & 0777777;
		else MQ = fill;
		LAC = link_init | fill;  }
	    SC = 0;					/* clear step count */
	    break;

	case 6:						/* long left shift */
	    if (esc < 18) {
		LAC = link_init |
		    (((LAC << esc) | (MQ >> (18 - esc))) & 0777777);
		MQ = ((MQ << esc) | (fill >> (18 - esc))) & 0777777;  }
	    else {
	    	if (esc < 36) LAC = link_init | 
		     (((MQ << (esc - 18)) | (fill >> (36 - esc))) & 0777777);
		else LAC = link_init | fill;
		MQ = fill;  }
	    SC = 0;					/* clear step count */
	    break;

	case 7:						/* AC left shift */
	    if (esc < 18) LAC = link_init |
		(((LAC << esc) | (fill >> (18 - esc))) & 0777777);
	    else LAC = link_init | fill;
	    SC = 0;					/* clear step count */
	    break;  }					/* end switch IR */
	break;						/* end case EAE */

/* PDP-15 index operates: opcode 72 */

case 035:						/* index operates */
#if defined (PDP15)
	t = (IR & 0400)? IR | 0777000: IR & 0377;	/* sext immediate */
	switch ((IR >> 9) & 017) {			/* case on IR<5:8> */
	case 000:					/* AAS */
	    LAC = (LAC & 01000000) | ((LAC + t) & 0777777);
	    if (SEXT (LAC & 0777777) >= SEXT (LR))
		PC = INCR_ADDR (PC);
	case 001:					/* PAX */
	    XR = LAC & 0777777;
	    break;
	case 002:					/* PAL */
	    LR = LAC & 0777777;
	    break;
	case 003:					/* AAC */
	    LAC = (LAC & 01000000) | ((LAC + t) & 0777777);
	    break;
	case 004:					/* PXA */
	    LAC = (LAC & 01000000) | XR;
	    break;
	case 005:					/* AXS */
	    XR = (XR + t) & 0777777;
	    if (SEXT (XR) >= SEXT (LR)) PC = INCR_ADDR (PC);
	    break;
	case 006:					/* PXL */
	    LR = XR;
	    break;
	case 010:					/* PLA */
	    LAC = (LAC & 01000000) | LR;
	    break;
	case 011:					/* PLX */
	    XR = LR;
	    break;
	case 014:					/* CLAC */
	    LAC = LAC & 01000000;
	    break;
	case 015:					/* CLX */
	    XR = 0;
	    break;
	case 016:					/* CLLR */
	    LR = 0;
	    break;
	case 017:					/* AXR */
	    XR = (XR + t) & 0777777;
	    break;  }					/* end switch IR */
	break;						/* end case */
#endif

/* IOT: opcode 70 

   The 18b PDP's have different definitions of various control IOT's.

   IOT		PDP-4		PDP-7		PDP-9		PDP-15

   700002	IOF		IOF		IOF		IOF
   700042	ION		ION		ION		ION
   700062	undefined	ITON		undefined	undefined
   701701	undefined	undefined	MPSK		MPSK
   701741	undefined	undefined	MPSNE		MPSNE
   701702	undefined	undefined	MPCV		MPCV
   701742	undefined	undefined	MPEU		MPEU
   701704	undefined	undefined	MPLD		MPLD
   701744	undefined	undefined	MPCNE		MPCNE
   703201	undefined	undefined	PFSF		PFSF
   703301	undefined	TTS		TTS		TTS
   703341	undefined	SKP7		SKP7		SPCO
   703302	undefined	CAF		CAF		CAF
   703304	undefined	undefined	DBK		DBK
   703344	undefined	undefined	DBR		DBR
   705501	undefined	undefined	SPI		SPI
   705502	undefined	undefined	RPL		RPL
   705504	undefined	undefined	ISA		ISA
   707701	undefined	SEM		SEM		undefined
   707741	undefined	undefined	undefined	SKP15
   707761	undefined	undefined	undefined	SBA
   707702	undefined	EEM		EEM		undefined
   707742	undefined	EMIR		EMIR		RES
   707762	undefined	undefined	undefined	DBA
   707704	undefined	LEM		LEM		undefined
   707764	undefined	undefined	undefined	EBA
*/

case 034:						/* IOT */
#if defined (PDP15)
	if (IR & 0010000) {				/* floating point? */
/*	    PC = fp15 (PC, IR);				/* process */
	    break;  }
#endif
	if (usmd) {					/* user mode? */
	    prvn = trap_pending = 1;			/* trap */
	    break;  }
	device = (IR >> 6) & 077;			/* device = IR<6:11> */
	pulse = IR & 067;				/* pulse = IR<12:17> */
	if (IR & 0000010) LAC = LAC & 01000000;		/* clear AC? */
	iot_data = LAC & 0777777;			/* AC unchanged */

/* PDP-4 system IOT's */

#if defined (PDP4)
	switch (device) {				/* decode IR<6:11> */
	case 0:						/* CPU and clock */
	    if (pulse == 002) ion = 0;			/* IOF */
	    else if (pulse == 042) ion = ion_defer = 1;	/* ION */
	    else iot_data = clk (pulse, iot_data);
	    break;
#endif

/* PDP-7 system IOT's */

#if defined (PDP7)
	switch (device) {				/* decode IR<6:11> */
	case 0:						/* CPU and clock */
	    if (pulse == 002) ion = 0;			/* IOF */
	    else if (pulse == 042) ion = ion_defer = 1;	/* ION */
	    else if (pulse == 062)			/* ITON */
		usmd = ion = ion_defer = 1;
	    else iot_data = clk (pulse, iot_data);
	    break;
	case 033:					/* CPU control */
	    if ((pulse == 001) || (pulse == 041)) PC = INCR_ADDR (PC);
	    else if (pulse == 002) reset_all (0);	/* CAF */
	    break;
	case 077:					/* extended memory */
	    if ((pulse == 001) && memm) PC = INCR_ADDR (PC);
	    else if (pulse == 002) memm = 1;		/* EEM */
	    else if (pulse == 042)			/* EMIR */
		memm = emir_pending = 1;		/* ext on, restore */
	    else if (pulse == 004) memm = 0;		/* LEM */
	    break;
#endif

/* PDP-9 and PDP-15 system IOT's */

#if defined (PDP9) || defined (PDP15)
	ion_defer = 1;					/* delay interrupts */
	switch (device) {				/* decode IR<6:11> */
	case 000:					/* CPU and clock */
	    if (pulse == 002) ion = 0;			/* IOF */
	    else if (pulse == 042) ion = 1;		/* ION */
	    else iot_data = clk (pulse, iot_data);
	    break;
	case 017:					/* mem protection */
	    if ((pulse == 001) && prvn) PC = INCR_ADDR (PC);
	    else if ((pulse == 041) && nexm) PC = INCR_ADDR (PC);
	    else if (pulse == 002) prvn = 0;
	    else if (pulse == 042) usmdbuf = 1;
	    else if (pulse == 004) BR = LAC & BRMASK;
	    else if (pulse == 044) nexm = 0;
	    break;
	case 032:					/* power fail */
	    if ((pulse == 001) && (TST_INT (PWRFL)))
		 PC = INCR_ADDR (PC);
	    break;
	case 033:					/* CPU control */
	    if ((pulse == 001) || (pulse == 041)) PC = INCR_ADDR (PC);
	    else if (pulse == 002) reset_all (0);	/* CAF */
	    else if (pulse == 044) rest_pending = 1; /* DBR */
	    if (((cpu_unit.flags & UNIT_NOAPI) == 0) && (pulse & 004)) {
		int32 t = api_ffo[api_act & 0377];
		api_act = api_act & ~(0200 >> t);  }
	    break;
	case 055:					/* API control */
	    if (cpu_unit.flags & UNIT_NOAPI) reason = stop_inst;
	    else if (pulse == 001) {			/* SPI */
		if (((LAC & SIGN) && api_enb) ||
		    ((LAC & 0377) > api_act))
		    iot_data = iot_data | IOT_SKP;  }
	    else if (pulse == 002) {			/* RPL */
		iot_data = iot_data | (api_enb << 17) |
		    (api_req << 8) | api_act;  }
	    else if (pulse == 004) {			/* ISA */
		api_enb = (iot_data & SIGN)? 1: 0;
		api_req = api_req | ((LAC >> 8) & 017);
		api_act = api_act | (LAC & 0377);  }
	    break;
#endif
#if defined (PDP9)
	case 077:					/* extended memory */
	    if ((pulse == 001) && memm) PC = INCR_ADDR (PC);
	    else if (pulse == 002) memm = 1;		/* EEM */
	    else if (pulse == 042)			/* EMIR */
		memm = emir_pending = 1;		/* ext on, restore */
	    else if (pulse == 004) memm = 0;		/* LEM */
	    break;
#endif
#if defined (PDP15)
	case 077:					/* bank addressing */
	    if ((pulse == 041) || ((pulse == 061) && memm))
		 PC = INCR_ADDR (PC);			/* SKP15, SBA */
	    else if (pulse == 042) rest_pending = 1;	/* RES */
	    else if (pulse == 062) memm = 0;		/* DBA */
	    else if (pulse == 064) memm = 1;		/* EBA */
	    damask = memm? 017777: 07777;		/* set dir addr mask */
	    epcmask = ADDRMASK & ~damask;		/* extended PC mask */
	    break;
#endif

/* IOT, continued */

	default:					/* devices */
	    if (dev_tab[device])			/* defined? */
		iot_data = dev_tab[device] (pulse, iot_data);
	    else reason = stop_inst;			/* stop on flag */
	    break;  }					/* end switch device */
	LAC = LAC | (iot_data & 0777777);
	if (iot_data & IOT_SKP) PC = INCR_ADDR (PC);
	if (iot_data >= IOT_REASON) reason = iot_data >> IOT_V_REASON;
	api_int = api_eval (&int_pend);			/* eval API */
	break;						/* end case IOT */
	}						/* end switch opcode */
if (api_cycle) {					/* API cycle? */
	api_cycle = 0;					/* cycle over */
	usmd = 0;					/* exit user mode */
	trap_pending = prvn = 0;  }			/* no priv viol */
}							/* end while */

/* Simulation halted */

saved_PC = PC & ADDRMASK;				/* save copies */
saved_LAC = LAC & 01777777;
saved_MQ = MQ & 0777777;
iors = upd_iors ();					/* get IORS */
pcq_r->qptr = pcq_p;					/* update pc q ptr */
return reason;
}

/* Evaluate API */

int32 api_eval (int32 *pend)
{
int32 i, hi;

for (i = *pend = 0; i < API_HLVL+1; i++) {		/* any intr? */
	if (int_hwre[i]) *pend = 1;  }
if (api_enb == 0) return 0;				/* off? no req */
api_req = api_req & ~0360;				/* clr req<0:3> */
for (i = 0; i < API_HLVL; i++) {			/* loop thru levels */
	if (int_hwre[i])				/* req on level? */
	    api_req = api_req | (0200 >> i);  }		/* set api req */
hi = api_ffo[api_req & 0377];				/* find hi req */
if (hi < api_ffo[api_act & 0377]) return (hi + 1);
return 0;
}

/* Process IORS instruction */

int32 upd_iors (void)
{
int32 d, p;

d = (ion? IOS_ION: 0);					/* ION */
for (p = 0; dev_iors[p] != NULL; p++) {			/* loop thru table */
	d = d | dev_iors[p]();  }			/* OR in results */
return d;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
SC = 0;
eae_ac_sign = 0;
ion = ion_defer = 0;
CLR_INT (PWRFL);
api_enb = api_req = api_act = 0;
BR = 0;
usmd = usmdbuf = 0;
memm = memm_init;
nexm = prvn = trap_pending = 0;
emir_pending = rest_pending = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = M[addr] & 0777777;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
M[addr] = val & 0777777;
return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
	return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
	return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
return SCPE_OK;
}

/* Change device number for a device */

t_stat set_devno (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newdev;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
newdev = get_uint (cptr, 8, DEV_MAX - 1, &r);		/* get new */
if ((r != SCPE_OK) || (newdev == dibp->dev)) return r;
dibp->dev = newdev;					/* store */
return SCPE_OK;
}

/* Show device number for a device */

t_stat show_devno (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL) return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
fprintf (st, "devno=%02o", dibp->dev);
if (dibp-> num > 1) fprintf (st, "-%2o", dibp->dev + dibp->num - 1);
return SCPE_OK;
}

/* CPU device handler - should never get here! */

int32 bad_dev (int32 pulse, int32 AC)
{
return (SCPE_IERR << IOT_V_REASON) | AC;		/* broken! */
}

/* Build device dispatch table */

t_bool build_dev_tab (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j, p;
static const uint8 std_dev[] =
#if defined (PDP4)
	{ 000 };
#elif defined (PDP7)
	{ 000, 033, 077 };
#else
	{ 000, 017, 033, 055, 077 };
#endif

for (i = 0; i < DEV_MAX; i++) {				/* clr tables */
	dev_tab[i] = NULL;
	dev_iors[i] = NULL;  }
for (i = 0; i < ((uint32) sizeof (std_dev)); i++)	/* std entries */
	dev_tab[std_dev[i]] = &bad_dev;
for (i = p =  0; (dptr = sim_devices[i]) != NULL; i++) {	/* add devices */
	dibp = (DIB *) dptr->ctxt;			/* get DIB */
	if (dibp && !(dptr->flags & DEV_DIS)) {		/* enabled? */
	    if (dibp->iors) dev_iors[p++] = dibp->iors;	/* if IORS, add */
	    for (j = 0; j < dibp->num; j++) {		/* loop thru disp */
		if (dibp->dsp[j]) {			/* any dispatch? */
		    if (dev_tab[dibp->dev + j]) {	/* already filled? */
			printf ("%s device number conflict at %02o\n",
			    dptr->name, dibp->dev + j);
			if (sim_log) fprintf (sim_log,
			    "%s device number conflict at %02o\n",
			    dptr->name, dibp->dev + j);
			 return TRUE;  }
		    dev_tab[dibp->dev + j] = dibp->dsp[j];	/* fill */
		    }					/* end if dsp */
		}					/* end for j */
	    }						/* end if enb */
	}						/* end for i */
return FALSE;
}
