/* altair_defs.h: MITS Altair simulator definitions

   Copyright (c) 1997,
   Charles E Owen
   Commercial use prohibited

*/

#include "sim_defs.h"					/* simulator defns */

/* Memory */

#define MAXMEMSIZE	65536				/* max memory size */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define ADDRMASK	(MAXMEMSIZE - 1)		/* address mask */
#define MEM_ADDR_OK(x)	(x < MEMSIZE)

/* Simulator stop codes */

#define STOP_RSRV	1				/* must be 1 */
#define STOP_HALT   2               /* HALT */
#define STOP_IBKPT	3				/* breakpoint */
#define STOP_OPCODE 4

