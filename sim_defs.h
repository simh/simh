/* sim_defs.h: simulator definitions

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

   05-Jan-03	RMS	Added hidden switch definitions, device dyn memory support,
			parameters for function pointers, case sensitive SET support
   22-Dec-02	RMS	Added break flag
   08-Oct-02	RMS	Increased simulator error code space
			Added Telnet errors
			Added end of medium support
			Added help messages to CTAB
			Added flag and context fields to DEVICE
			Added restore flag masks
			Revised 64b definitions
   02-May-02	RMS	Removed log status codes
   22-Apr-02	RMS	Added magtape record length error
   30-Dec-01	RMS	Generalized timer package, added circular arrays
   07-Dec-01	RMS	Added breakpoint package
   01-Dec-01	RMS	Added read-only unit support, extended SET/SHOW features,
			improved error messages
   24-Nov-01	RMS	Added unit-based registers
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

#ifndef _SIM_DEFS_H_
#define _SIM_DEFS_H_	0

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
#if defined (USE_INT64)					/* 64b */
#if defined (WIN32)					/* Windows */
#define t_int64 __int64
#elif defined (__ALPHA) && defined (VMS)		/* Alpha VMS */
#define t_int64 __int64
#elif defined (__ALPHA) && defined (__unix__)		/* Alpha UNIX */
#define t_int64 long
#else							/* default GCC */
#define t_int64 long long
#endif							/* end OS's */
typedef unsigned t_int64	t_uint64, t_value;	/* value */
typedef t_int64 		t_svalue;		/* signed value */
#else							/* 32b */
typedef unsigned int32	t_value;
typedef int32 		t_svalue;
#endif							/* end else 64b */

/* System independent definitions */

typedef uint32		t_mtrlnt;			/* magtape rec lnt */
#define MTR_TMK		0x00000000			/* tape mark */
#define MTR_EOM		0xFFFFFFFF			/* end of medium */
#define MTR_ERF		0x80000000			/* error flag */
#define MTRF(x)		((x) & MTR_ERF)			/* record error flg */
#define MTRL(x)		((x) & ~MTR_ERF)		/* record length */
#define MT_SET_PNU(u)	(u)->flags = (u)->flags | UNIT_PNU
#define MT_CLR_PNU(u)	(u)->flags = (u)->flags & ~UNIT_PNU
#define MT_TST_PNU(u)	((u)->flags & UNIT_PNU)

#define FLIP_SIZE	(1 << 16)			/* flip buf size */
#if !defined (PATH_MAX)					/* usually in limits */
#define PATH_MAX	512
#endif
#define CBUFSIZE	(128 + PATH_MAX)		/* string buf size */

/* Extended switch definitions (bits >= 26) */

#define SIM_SW_HIDE	(1u << 26)			/* enable hiding */
#define SIM_SW_REST	(1u << 27)			/* attach/restore */

/* Simulator status codes

   0			ok
   1 - (SCPE_BASE - 1)	simulator specific
   SCPE_BASE - n	general
*/

#define SCPE_OK		0				/* normal return */
#define SCPE_BASE	64				/* base for messages */
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
#define SCPE_NORO	(SCPE_BASE + 27)		/* rd only not ok */
#define SCPE_INVSW	(SCPE_BASE + 28)		/* invalid switch */
#define SCPE_MISVAL	(SCPE_BASE + 29)		/* missing value */
#define SCPE_2FARG	(SCPE_BASE + 30)		/* too few arguments */
#define SCPE_2MARG	(SCPE_BASE + 31)		/* too many arguments */
#define SCPE_NXDEV	(SCPE_BASE + 32)		/* nx device */
#define SCPE_NXUN	(SCPE_BASE + 33)		/* nx unit */
#define SCPE_NXREG	(SCPE_BASE + 34)		/* nx register */
#define SCPE_NXPAR	(SCPE_BASE + 35)		/* nx parameter */
#define SCPE_NEST	(SCPE_BASE + 36)		/* nested DO */
#define SCPE_IERR	(SCPE_BASE + 37)		/* internal error */
#define SCPE_MTRLNT	(SCPE_BASE + 38)		/* tape rec lnt error */
#define SCPE_LOST	(SCPE_BASE + 39)		/* Telnet conn lost */
#define SCPE_TTMO	(SCPE_BASE + 40)		/* Telnet conn timeout */
#define SCPE_KFLAG	0010000				/* tti data flag */
#define SCPE_BREAK	0020000				/* tti break flag */

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
	int32		numunits;			/* #units */
	int32		aradix;				/* address radix */
	int32		awidth;				/* address width */
	int32		aincr;				/* addr increment */
	int32		dradix;				/* data radix */
	int32		dwidth;				/* data width */
	t_stat		(*examine)(t_value *v, t_addr a, struct unit *up,
				int32 sw);		/* examine routine */
	t_stat		(*deposit)(t_value v, t_addr a, struct unit *up,
				int32 sw);		/* deposit routine */
	t_stat		(*reset)(struct device *dp);	/* reset routine */
	t_stat		(*boot)(int32 u, struct device *dp);
							/* boot routine */
	t_stat		(*attach)(struct unit *up, char *cp);
							/* attach routine */
	t_stat		(*detach)(struct unit *up);	/* detach routine */
	void		*ctxt;				/* context */
	int32		flags;				/* flags */
	t_stat		(*msize)(struct unit *up, int32 v, char *cp, void *dp);
							/* mem size routine */
};

/* Device flags */

#define DEV_V_DIS	0				/* dev enabled */
#define DEV_V_DISABLE	1				/* dev disable-able */
#define DEV_V_DYNM	2				/* mem size dynamic */
#define	DEV_V_UF	12				/* user flags */
#define DEV_V_RSV	31				/* reserved */

#define DEV_DIS		(1 << DEV_V_DIS)
#define DEV_DISABLE	(1 << DEV_V_DISABLE)
#define DEV_DYNM	(1 << DEV_V_DYNM)

#define DEV_UFMASK	(((1u << DEV_V_RSV) - 1) & ~((1u << DEV_V_UF) - 1))
#define DEV_RFLAGS	(DEV_UFMASK|DEV_DIS)		/* restored flags */

/* Unit data structure

   Parts of the unit structure are device specific, that is, they are
   not referenced by the simulator control package and can be freely
   used by device simulators.  Fields starting with 'buf', and flags
   starting with 'UF', are device specific.  The definitions given here
   are for a typical sequential device.
*/

struct unit {
	struct unit	*next;				/* next active */
	t_stat		(*action)(struct unit *up);	/* action routine */
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

#define UNIT_V_UF	12				/* device specific */
#define UNIT_V_RSV	31				/* reserved!! */

#define UNIT_ATTABLE	000001				/* attachable */
#define UNIT_RO		000002				/* read only */
#define UNIT_FIX	000004				/* fixed capacity */
#define UNIT_SEQ	000010				/* sequential */
#define UNIT_ATT	000020				/* attached */
#define UNIT_BINK	000040				/* K = power of 2 */
#define UNIT_BUFABLE	000100				/* bufferable */
#define UNIT_MUSTBUF	000200				/* must buffer */
#define UNIT_BUF	000400				/* buffered */
#define UNIT_ROABLE	001000				/* read only ok */
#define UNIT_DISABLE	002000				/* disable-able */
#define UNIT_DIS	004000				/* disabled */

#define UNIT_UFMASK	(((1u << UNIT_V_RSV) - 1) & ~((1u << UNIT_V_UF) - 1))
#define UNIT_RFLAGS	(UNIT_UFMASK|UNIT_DIS)		/* restored flags */

/* Register data structure */

struct reg {
	char		*name;				/* name */
	void		*loc;				/* location */
	int32		radix;				/* radix */
	int32		width;				/* width */
	int32		offset;				/* starting bit */
	int32		depth;				/* save depth */
	int32		flags;				/* flags */
	int32		qptr;				/* circ q ptr */
};

#define REG_FMT		0003				/* see PV_x */
#define REG_RO		0004				/* read only */
#define REG_HIDDEN	0010				/* hidden */
#define REG_NZ		0020				/* must be non-zero */
#define REG_UNIT	0040				/* in unit struct */
#define REG_CIRC	0100				/* circular array */
#define REG_HRO		(REG_RO | REG_HIDDEN)		/* hidden, read only */

/* Command table */

struct ctab {
	char		*name;				/* name */
	t_stat		(*action)();			/* action routine */
	int32		arg;				/* argument */
	char		*help;				/* help string */
};

/* Modifier table - only extended entries have disp, reg, or flags */

struct mtab {
	int32		mask;				/* mask or radix */
	int32		match;				/* match or max */
	char		*pstring;			/* print string */
	char		*mstring;			/* match string */
	t_stat		(*valid)(struct unit *up, int32 v, char *cp, void *dp);
							/* validation routine */
	t_stat		(*disp)(FILE *st, struct unit *up, int32 v, void *dp);
							/* display routine */
	void		*desc;				/* value descriptor */
							/* REG * if MTAB_VAL */
							/* int * if not */
};

#define	MTAB_XTD	(1u << UNIT_V_RSV)		/* ext entry flag */
#define MTAB_VDV	001				/* valid for dev */
#define MTAB_VUN	002				/* valid for unit */
#define MTAB_VAL	004				/* takes a value */
#define MTAB_NMO	010				/* only if named */
#define MTAB_NC		020				/* no UC conversion */

/* Search table */

struct schtab {
	int32		logic;				/* logical operator */
	int32		bool;				/* boolean operator */
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
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
	#nm, &(loc), (rdx), (wd), (off), (dep), ((fl) | REG_UNIT)
#else
#define ORDATA(nm,loc,wd) "nm", &(loc), 8, (wd), 0, 1
#define DRDATA(nm,loc,wd) "nm", &(loc), 10, (wd), 0, 1
#define HRDATA(nm,loc,wd) "nm", &(loc), 16, (wd), 0, 1
#define FLDATA(nm,loc,pos) "nm", &(loc), 2, 1, (pos), 1
#define GRDATA(nm,loc,rdx,wd,pos) "nm", &(loc), (rdx), (wd), (pos), 1
#define BRDATA(nm,loc,rdx,wd,dep) "nm", (loc), (rdx), (wd), 0, (dep)
#define URDATA(nm,loc,rdx,wd,off,dep,fl) \
	"nm", &(loc), (rdx), (wd), (off), (dep), ((fl) | REG_UNIT)
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
char *get_range (char *cptr, t_addr *lo, t_addr *hi, int32 rdx,
	t_addr max, char term);
t_stat get_ipaddr (char *cptr, uint32 *ipa, uint32 *ipp);
t_value strtotv (char *cptr, char **endptr, int radix);
t_stat fprint_val (FILE *stream, t_value val, int32 rdx, int32 wid, int32 fmt);
DEVICE *find_dev_from_unit (UNIT *uptr);
REG *find_reg (char *ptr, char **optr, DEVICE *dptr);
int32 sim_rtc_init (int32 time);
int32 sim_rtc_calb (int32 ticksper);
int32 sim_rtcn_init (int32 time, int32 tmr);
int32 sim_rtcn_calb (int32 time, int32 tmr);
t_stat sim_poll_kbd (void);
t_stat sim_putchar (int32 out);
t_bool sim_brk_test (t_addr bloc, int32 btyp);
int sim_os_sleep (unsigned int sec);

#endif
