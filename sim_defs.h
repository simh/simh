/* sim_defs.h: simulator definitions

   Copyright (c) 1993-2001, Robert M Supnik

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

   27-Sep-01	RMS	Added queue count prototype
   17-Sep-01	RMS	Removed multiple console support
   07-Sep-01	RMS	Removed conditional externs on function prototypes
   31-Aug-01	RMS	Changed int64 to t_int64 for Windoze
   17-Jul-01	RMS	Added additional function prototypes
   27-May-01	RMS	Added multiple console support
   15-May-01	RMS	Increased string buffer size
   25-Feb-01	RMS	Revisions for V2.6
   15-Oct-00	RMS	Editorial revisions for V2.5
   11-Jul-99	RMS	Added unsigned int data types
   14-Apr-99	RMS	Converted t_addr to unsigned
   04-Oct-98	RMS	Additional definitions for V2.4

   The interface between the simulator control package (SCP) and the
   simulator consists of the following routines and data structures

	sim_name		simulator name string
	sim_devices[]		array of pointers to simulated devices
	sim_PC			pointer to saved PC register descriptor
	sim_interval		simulator interval to next event
	sim_stop_messages[]	array of pointers to stop messages
	sim_instr()		instruction execution routine
	sim_load()		binary loader routine
	sim_emax		maximum number of words in an instruction

   In addition, the simulator must supply routines to print and parse
   architecture specific formats

	print_sym		print symbolic output
	parse_sym		parse symbolic input
*/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifndef TRUE
#define TRUE		1
#define FALSE		0
#endif

/* Length specific integer declarations */

#define int8		char
#define int16		short
#define int32		int
typedef int		t_stat;				/* status */
typedef int		t_bool;				/* boolean */
typedef unsigned int8	uint8;
typedef unsigned int16	uint16;
typedef unsigned int32	uint32, t_addr;			/* address */
#if defined (USE_INT64) && defined (_WIN32)
#define t_int64 __int64					/* for Windows */
#elif defined (USE_INT64) && defined (VMS) && defined (__ALPHA)
#define t_int64 __int64					/* for AVMS */
#elif defined (USE_INT64) && defined (__ALPHA) && defined (__unix__)
#define t_int64 long					/* for DUNIX */
#elif defined (USE_INT64)
#define t_int64 long long				/* for GCC */
#endif
#if defined (t_int64)
typedef unsigned t_int64	t_uint64, t_value;	/* value */
typedef t_int64 		t_svalue;		/* signed value */
#else
typedef unsigned int32	t_value;
typedef int32 		t_svalue;
#endif

/* System independent definitions */

typedef int32		t_mtrlnt;			/* magtape rec lnt */
#define MTRF(x)		((x) & (1u << 31))		/* record error flg */
#define MTRL(x)		((x) & ((1u << 31) - 1))	/* record length */
#define FLIP_SIZE	(1 << 16)			/* flip buf size */
#if !defined (PATH_MAX)					/* usually in limits */
#define PATH_MAX	512
#endif
#define CBUFSIZE	(128 + PATH_MAX)		/* string buf size */
#define CONS_SIZE	4096				/* console buffer */

/* Simulator status codes

   0			ok
   1 - (SCPE_BASE - 1)	simulator specific
   SCPE_BASE - n	general
*/

#define SCPE_OK		0				/* normal return */
#define SCPE_BASE	32				/* base for messages */
#define SCPE_NXM	(SCPE_BASE + 0)			/* nxm */
#define SCPE_UNATT	(SCPE_BASE + 1)			/* no file */
#define SCPE_IOERR 	(SCPE_BASE + 2)			/* I/O error */
#define SCPE_CSUM	(SCPE_BASE + 3)			/* loader cksum */
#define SCPE_FMT	(SCPE_BASE + 4)			/* loader format */
#define SCPE_NOATT	(SCPE_BASE + 5)			/* not attachable */
#define SCPE_OPENERR	(SCPE_BASE + 6)			/* open error */
#define SCPE_MEM	(SCPE_BASE + 7)			/* alloc error */
#define SCPE_ARG	(SCPE_BASE + 8)			/* argument error */
#define SCPE_STEP	(SCPE_BASE + 9)			/* step expired */
#define SCPE_UNK	(SCPE_BASE + 10)		/* unknown command */
#define SCPE_RO		(SCPE_BASE + 11)		/* read only */
#define SCPE_INCOMP	(SCPE_BASE + 12)		/* incomplete */
#define SCPE_STOP	(SCPE_BASE + 13)		/* sim stopped */
#define SCPE_EXIT	(SCPE_BASE + 14)		/* sim exit */
#define SCPE_TTIERR	(SCPE_BASE + 15)		/* console tti err */
#define SCPE_TTOERR	(SCPE_BASE + 16)		/* console tto err */
#define SCPE_EOF	(SCPE_BASE + 17)		/* end of file */
#define SCPE_REL	(SCPE_BASE + 18)		/* relocation error */
#define SCPE_NOPARAM	(SCPE_BASE + 19)		/* no parameters */
#define SCPE_ALATT	(SCPE_BASE + 20)		/* already attached */
#define SCPE_TIMER	(SCPE_BASE + 21)		/* hwre timer err */
#define SCPE_SIGERR	(SCPE_BASE + 22)		/* signal err */
#define SCPE_TTYERR	(SCPE_BASE + 23)		/* tty setup err */
#define SCPE_SUB	(SCPE_BASE + 24)		/* subscript err */
#define SCPE_NOFNC	(SCPE_BASE + 25)		/* func not imp */
#define SCPE_UDIS	(SCPE_BASE + 26)		/* unit disabled */
#define SCPE_LOGON	(SCPE_BASE + 27)		/* logging enabled */
#define SCPE_LOGOFF	(SCPE_BASE + 28)		/* logging disabled */
#define SCPE_KFLAG	01000				/* tti data flag */

/* Print value format codes */

#define PV_RZRO		0				/* right, zero fill */
#define PV_RSPC		1				/* right, space fill */
#define PV_LEFT		2				/* left justify */

/* Default timing parameters */

#define KBD_POLL_WAIT	5000				/* keyboard poll */
#define SERIAL_IN_WAIT	100				/* serial in time */
#define SERIAL_OUT_WAIT	10				/* serial output */
#define NOQUEUE_WAIT	10000				/* min check time */

/* Convert switch letter to bit mask */

#define SWMASK(x) (1u << (((int) (x)) - ((int) 'A')))

/* String match */

#define MATCH_CMD(ptr,cmd) strncmp ((ptr), (cmd), strlen (ptr))

/* Device data structure */

struct device {
	char		*name;				/* name */
	struct unit 	*units;				/* units */
	struct reg	*registers;			/* registers */
	struct mtab	*modifiers;			/* modifiers */
	int		numunits;			/* #units */
	int		aradix;				/* address radix */
	int		awidth;				/* address width */
	int		aincr;				/* addr increment */
	int		dradix;				/* data radix */
	int		dwidth;				/* data width */
	t_stat		(*examine)();			/* examine routine */
	t_stat		(*deposit)();			/* deposit routine */
	t_stat		(*reset)();			/* reset routine */
	t_stat		(*boot)();			/* boot routine */
	t_stat		(*attach)();			/* attach routine */
	t_stat		(*detach)();			/* detach routine */
};

/* Unit data structure

   Parts of the unit structure are device specific, that is, they are
   not referenced by the simulator control package and can be freely
   used by device simulators.  Fields starting with 'buf', and flags
   starting with 'UF', are device specific.  The definitions given here
   are for a typical sequential device.
*/

struct unit {
	struct unit	*next;				/* next active */
	t_stat		(*action)();			/* action routine */
	char		*filename;			/* open file name */
	FILE		*fileref;			/* file reference */
	void		*filebuf;			/* memory buffer */
	t_addr		hwmark;				/* high water mark */
	int32		time;				/* time out */
	int32		flags;				/* flags */
	t_addr		capac;				/* capacity */
	t_addr		pos;				/* file position */
	int32		buf;				/* buffer */
	int32		wait;				/* wait */
	int32		u3;				/* device specific */
	int32		u4;				/* device specific */
};

/* Unit flags */

#define UNIT_ATTABLE	000001				/* attachable */
#define UNIT_RO		000002				/* read only */
#define UNIT_FIX	000004				/* fixed capacity */
#define UNIT_SEQ	000010				/* sequential */
#define UNIT_ATT	000020				/* attached */
#define UNIT_BINK	000040				/* K = power of 2 */
#define UNIT_BUFABLE	000100				/* bufferable */
#define UNIT_MUSTBUF	000200				/* must buffer */
#define UNIT_BUF	000400				/* buffered */
#define UNIT_DISABLE	001000				/* disable-able */
#define UNIT_DIS	002000				/* disabled */
#define UNIT_V_UF	12				/* device specific */

/* Register data structure */

struct reg {
	char		*name;				/* name */
	void		*loc;				/* location */
	int		radix;				/* radix */
	int		width;				/* width */
	int		offset;				/* starting bit */
	int		depth;				/* save depth */
	int32		flags;				/* flags */
};

#define REG_FMT		003				/* see PV_x */
#define REG_RO		004				/* read only */
#define REG_HIDDEN	010				/* hidden */
#define REG_NZ		020				/* must be non-zero */
#define REG_HRO		(REG_RO + REG_HIDDEN)		/* hidden, read only */

/* Command table */

struct ctab {
	char		*name;				/* name */
	t_stat		(*action)();			/* action routine */
	int		arg;				/* argument */
};

/* Modifier table */

struct mtab {
	int32		mask;				/* mask or radix */
	int32		match;				/* match or max */
	char		*pstring;			/* print string */
	char		*mstring;			/* match string */
	t_stat		(*valid)();			/* validation routine */
};

/* Search table */

struct schtab {
	int		logic;				/* logical operator */
	int		bool;				/* boolean operator */
	t_value		mask;				/* mask for logical */
	t_value		comp;				/* comparison for boolean */
};

/* The following macros define structure contents */

#define UDATA(act,fl,cap) NULL,act,NULL,NULL,NULL,0,0,(fl),(cap),0,0

#if defined (__STDC__) || defined (_WIN32)
#define ORDATA(nm,loc,wd) #nm, &(loc), 8, (wd), 0, 1
#define DRDATA(nm,loc,wd) #nm, &(loc), 10, (wd), 0, 1
#define HRDATA(nm,loc,wd) #nm, &(loc), 16, (wd), 0, 1
#define FLDATA(nm,loc,pos) #nm, &(loc), 2, 1, (pos), 1
#define GRDATA(nm,loc,rdx,wd,pos) #nm, &(loc), (rdx), (wd), (pos), 1
#define BRDATA(nm,loc,rdx,wd,dep) #nm, (loc), (rdx), (wd), 0, (dep)
#else
#define ORDATA(nm,loc,wd) "nm", &(loc), 8, (wd), 0, 1
#define DRDATA(nm,loc,wd) "nm", &(loc), 10, (wd), 0, 1
#define HRDATA(nm,loc,wd) "nm", &(loc), 16, (wd), 0, 1
#define FLDATA(nm,loc,pos) "nm", &(loc), 2, 1, (pos), 1
#define GRDATA(nm,loc,rdx,wd,pos) "nm", &(loc), (rdx), (wd), (pos), 1
#define BRDATA(nm,loc,rdx,wd,dep) "nm", (loc), (rdx), (wd), 0, (dep)
#endif

/* Typedefs for principal structures */

typedef struct device DEVICE;
typedef struct unit UNIT;
typedef struct reg REG;
typedef struct ctab CTAB;
typedef struct mtab MTAB;
typedef struct schtab SCHTAB;

/* Function prototypes */

t_stat sim_process_event (void);
t_stat sim_activate (UNIT *uptr, int32 interval);
t_stat sim_cancel (UNIT *uptr);
int32 sim_is_active (UNIT *uptr);
double sim_gtime (void);
uint32 sim_grtime (void);
int32 sim_qcount (void);
t_stat attach_unit (UNIT *uptr, char *cptr);
t_stat detach_unit (UNIT *uptr);
t_stat reset_all (int start_device);
size_t fxread (void *bptr, size_t size, size_t count, FILE *fptr);
size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr);
t_stat get_yn (char *ques, t_stat deflt);
char *get_glyph (char *iptr, char *optr, char mchar);
char *get_glyph_nc (char *iptr, char *optr, char mchar);
t_value get_uint (char *cptr, int radix, t_value max, t_stat *status);
t_value strtotv (char *cptr, char **endptr, int radix);
int32 sim_rtc_init (int32 time);
int32 sim_rtc_calb (int32 ticksper);
t_stat sim_poll_kbd (void);
t_stat sim_putchar (int32 out);
