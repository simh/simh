/*	altairZ80_defs.h: MITS Altair simulator definitions
		Written by Peter Schorn, 2001-2002
		Based on work by Charles E Owen ((c) 1997, Commercial use prohibited)
*/

#include "sim_defs.h"													/* simulator definitions					*/

/* Memory */
#define MAXMEMSIZE			65536									/* max memory size								*/
#define ADDRMASK				(MAXMEMSIZE - 1)			/* address mask										*/
#define bootrom_size		256										/* size of boot rom								*/
#define MAXBANKS				8											/* max number of memory banks			*/
#define MAXBANKSLOG2		3											/* log2 of MAXBANKS								*/
#define BANKMASK				(MAXBANKS-1)					/* bank mask											*/

#define UNIT_V_OPSTOP		(UNIT_V_UF)						/* Stop on Invalid OP?						*/
#define UNIT_OPSTOP			(1 << UNIT_V_OPSTOP)
#define UNIT_V_CHIP	 		(UNIT_V_UF+1)					/* 8080 or Z80										*/
#define UNIT_CHIP				(1 << UNIT_V_CHIP)
#define UNIT_V_MSIZE		(UNIT_V_UF+2)					/* Memory Size										*/
#define UNIT_MSIZE			(1 << UNIT_V_MSIZE)
#define UNIT_V_BANKED		(UNIT_V_UF+3)					/* Banked memory is used					*/
#define UNIT_BANKED			(1 << UNIT_V_BANKED)
#define UNIT_V_ROM			(UNIT_V_UF+4)					/* ROM exists											*/
#define UNIT_ROM				(1 << UNIT_V_ROM)

#define PCformat	"\n[%04xh] "
#define message1(p1)					sprintf(messageBuffer,PCformat p1,PCX);						printMessage()
#define message2(p1,p2)				sprintf(messageBuffer,PCformat p1,PCX,p2);				printMessage()
#define message3(p1,p2,p3)		sprintf(messageBuffer,PCformat p1,PCX,p2,p3);			printMessage()
#define message4(p1,p2,p3,p4)	sprintf(messageBuffer,PCformat p1,PCX,p2,p3,p4);	printMessage()

/*	The Default is to use "inline". In this case the wrapper functions for
		GetBYTE and PutBYTE need to be created. Otherwise they are not needed
		and the calls map to the original functions.																*/
#ifdef NO_INLINE
#define INLINE
#define GetBYTEWrapper GetBYTE
#define PutBYTEWrapper PutBYTE
#else
#if defined(__DECC) && defined(VMS)
#define INLINE __inline
#else
#define INLINE inline
#endif
#endif
