/* pdp18b_defs.h: 18b PDP simulator definitions

   Copyright (c) 1993-2000, Robert M Supnik

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

   14-Apr-99	RMS	Changed t_addr to unsigned
   02-Jan-96	RMS	Added fixed head and moving head disks
   31-Dec-95	RMS	Added memory management
   19-Mar-95	RMS	Added dynamic memory size

   The author gratefully acknowledges the help of Craig St. Clair and
   Deb Tevonian in locating archival material about the 18b PDP's, and of
   Al Kossow and Max Burnet in making documentation and software available.
*/

#include "sim_defs.h"					/* simulator defns */

/* Models: only one should be defined

   model memory	CPU options		I/O options

   PDP4	   8K	none			Type 65 KSR-28 Teletype (Baudot)
					integral paper tape reader
					Type 75 paper tape punch
					integral real time clock
					Type 62 line printer (Hollerith)

   PDP7	   32K	Type 177 EAE		Type 649 KSR-33 Teletype
		Type 148 mem extension	Type 444 paper tape reader
					Type 75 paper tape punch
					integral real time clock
					Type 647B line printer (sixbit)
					Type 24 serial drum

   PDP9	   32K	KE09A EAE		KSR-33 Teletype
	   	KG09B mem extension	PC09A paper tape reader and punch
		KP09A power detection	integral real time clock
		KX09A mem protection	Type 647D/E line printer (sixbit)
					RF09/RS09 fixed head disk
					TC59 magnetic tape

   PDP15  128K	KE15 EAE		KSR-35 Teletype
		KF15 power detection	PC15 paper tape reader and punch
		KM15 mem protection	KW15 real time clock
		??KT15 mem relocation	LP15 line printer
					RP15 disk pack
					RF15/RF09 fixed head disk
					TC59D magnetic tape

   ??Indicates not implemented.  The PDP-4 manual refers to both an EAE
   ??and a memory extension control; there is no documentation on either.
*/

#if !defined (PDP4) && !defined (PDP7) && !defined (PDP9) && !defined (PDP15)
#define PDP9		0				/* default to PDP-9 */
#endif

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_HALT	2				/* HALT */
#define STOP_IBKPT	3				/* breakpoint */
#define STOP_XCT	4				/* nested XCT's */

/* Peripheral configuration */

#if defined (PDP4)
#define ADDRSIZE	13
#define KSR28		0				/* Baudot terminal */
#define TYPE62		0				/* Hollerith printer */
#elif defined (PDP7)
#define ADDRSIZE	15
#define TYPE647		0				/* sixbit printer */
#define DRM		0				/* drum */
#elif defined (PDP9)
#define ADDRSIZE	15
#define TYPE647		0				/* sixbit printer */
#define RF		0				/* fixed head disk */
#define MTA		0				/* magtape */
#elif defined (PDP15)
#define ADDRSIZE	17
#define LP15		0				/* ASCII printer */
#define RF		0				/* fixed head disk */
#define RP		0				/* disk pack */
#define MTA		0				/* magtape */
#endif

/* Memory */

#define ADDRMASK	((1 << ADDRSIZE) - 1)		/* address mask */
#define IAMASK		077777				/* ind address mask */
#define BLKMASK		(ADDRMASK & (~IAMASK))		/* block mask */
#define MAXMEMSIZE	(1 << ADDRSIZE)			/* max memory size */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define MEM_ADDR_OK(x)	(((t_addr) (x)) < MEMSIZE)

/* Architectural constants */

#define DMASK		0777777				/* data mask */
#define LINK		(DMASK + 1)			/* link */
#define LACMASK		(LINK | DMASK)			/* link + data */
#define SIGN		0400000				/* sign bit */

/* IOT subroutine return codes */

#define IOT_V_SKP	18				/* skip */
#define IOT_V_REASON	19				/* reason */
#define IOT_SKP		(1 << IOT_V_SKP)
#define IOT_REASON	(1 << IOT_V_REASON)

#define IORETURN(f,v)	((f)? (v): SCPE_OK)		/* stop on error */

/* Interrupt system

   The interrupt system can be modelled on either the flag driven system
   of the PDP-4 and PDP-7 or the API driven system of the PDP-9 and PDP-15.
   If flag based, API is hard to implement; if API based, IORS requires
   extra code for implementation.  I've chosen an API based model.

   Interrupt system, priority is left to right.

   <30:28> =	priority 0
   <27:20> =	priority 1
   <19:14> =	priority 2
   <13:10> =	priority 3
   <9:4> =	PI only
   <3> =	priority 4 (software)
   <2> =	priority 5 (software)
   <1> =	priority 6 (software)
   <0> =	priority 7 (software)
*/

#define INT_V_PWRFL	30				/* powerfail */
#define INT_V_DTA	27				/* DECtape */
#define INT_V_MTA	26				/* magtape */
#define INT_V_DRM	25				/* drum */
#define INT_V_RF	24				/* fixed head disk */
#define INT_V_RP	23				/* disk pack */
#define INT_V_PTR	19				/* paper tape reader */
#define INT_V_LPT	18				/* line printer */
#define INT_V_LPTSPC	17				/* line printer spc */
#define INT_V_CLK	13				/* clock */
#define INT_V_TTI	9				/* keyboard */
#define INT_V_TTO	8				/* terminal */
#define INT_V_PTP	7				/* paper tape punch */
#define INT_V_SW4	3				/* software 4 */
#define INT_V_SW5	2				/* software 5 */
#define INT_V_SW6	1				/* software 6 */
#define INT_V_SW7	0				/* software 7 */

#define INT_PWRFL	(1 << INT_V_PWRFL)
#define INT_DTA		(1 << INT_V_DTA)
#define INT_MTA		(1 << INT_V_MTA)
#define INT_DRM		(1 << INT_V_DRM)
#define INT_RF		(1 << INT_V_RF)
#define INT_RP		(1 << INT_V_RP)
#define INT_PTR		(1 << INT_V_PTR)
#define INT_LPT		(1 << INT_V_LPT)
#define INT_LPTSPC	(1 << INT_V_LPTSPC)
#define INT_CLK		(1 << INT_V_CLK)
#define INT_TTI		(1 << INT_V_TTI)
#define INT_TTO		(1 << INT_V_TTO)
#define INT_PTP		(1 << INT_V_PTP)
#define INT_SW4		(1 << INT_V_SW4)
#define INT_SW5		(1 << INT_V_SW5)
#define INT_SW6		(1 << INT_V_SW6)
#define INT_SW7		(1 << INT_V_SW7)

/* I/O status flags for the IORS instruction

   bit	PDP-4		PDP-7		PDP-9		PDP-15

   0	intr on		intr on		intr on		intr on
   1	tape rdr flag*	tape rdr flag*	tape rdr flag*	tape rdr flag*
   2	tape pun flag*	tape pun flag*	tape pun flag*	tape pun flag*
   3	keyboard flag*	keyboard flag*	keyboard flag*	keyboard flag*
   4	type out flag*	type out flag*	type out flag*	type out flag*
   5	display flag*	display flag*	light pen flag*	light pen flag*
   6	clk ovflo flag*	clk ovflo flag*	clk ovflo flag*	clk ovflo flag*
   7	clk enable flag	clk enable flag	clk enable flag	clk enable flag
   8	mag tape flag*	mag tape flag*	tape rdr empty*	tape rdr empty*
   9	card rdr col*	*		tape pun empty	tape pun empty
   10	card rdr ~busy			DECtape flag*	DECtape flag*
   11	card rdr error			magtape flag*	magtape flag*
   12	card rdr EOF					disk pack flag*
   13	card pun row*			DECdisk flag*	DECdisk flag*
   14	card pun error					lpt flag*
   15	lpt flag*	lpt flag*	lpt flag*
   16	lpt space flag*	lpt error flag	lpt error flag
   17			drum flag*	drum flag*
*/

#define IOS_ION		0400000				/* interrupts on */
#define IOS_PTR		0200000				/* tape reader */
#define IOS_PTP		0100000				/* tape punch */
#define IOS_TTI		0040000				/* keyboard */
#define IOS_TTO		0020000				/* terminal */
#define IOS_LPEN	0010000				/* light pen */
#define IOS_CLK		0004000				/* clock */
#define IOS_CLKON	0002000				/* clock enable */
#define IOS_DTA		0000200				/* DECtape */
#define IOS_RP		0000040				/* disk pack */
#define IOS_RF		0000020				/* fixed head disk */
#define IOS_DRM		0000001				/* drum */
#if defined (PDP4) || defined (PDP7)
#define IOS_MTA		0001000				/* magtape */
#define IOS_LPT		0000004				/* line printer */
#define IOS_LPT1	0000002				/* line printer stat */
#elif defined (PDP9)
#define IOS_PTRERR	0001000				/* reader empty */
#define IOS_PTPERR	0000400				/* punch empty */
#define IOS_MTA		0000100				/* magtape */
#define IOS_LPT		0000004				/* line printer */
#define IOS_LPT1	0000002				/* line printer stat */
#elif defined (PDP15)
#define IOS_PTRERR	0001000				/* reader empty */
#define IOS_PTPERR	0000400				/* punch empty */
#define IOS_MTA		0000100				/* magtape */
#define IOS_LPT		0000010				/* line printer */
#endif
