/*
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

/* ibm1130_defs.h: IBM-1130 simulator definitions
 */

#include "sim_defs.h"						/* main SIMH defns (include path should include .., or make a copy) */
#include "sim_console.h"					/* more SIMH defns (include path should include .., or make a copy) */

#include <setjmp.h>
#include <assert.h>
#include <stdlib.h>

#if defined(VMS)
	#  include <unistd.h>  					/* to pick up 'unlink' */
#endif

#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#define MAX(a,b)  (((a) >= (b)) ? (a) : (b))

#ifndef _WIN32
   int strnicmp (const char *a, const char *b, size_t n);
   int strcmpi  (const char *a, const char *b);
#endif

/* #define GUI_SUPPORT		uncomment to compile the GUI extensions. It's defined in the windows ibm1130.mak makefile */

/* ------------------------------------------------------------------------ */
/* Architectural constants */

#define MAXMEMSIZE	(32768)						/* 32Kwords */
#define INIMEMSIZE 	(16384)						/* 16Kwords */
#define MEMSIZE		(cpu_unit.capac)

#define ILL_ADR_FLAG	0x40000000				/* an impossible 1130 address */

/* ------------------------------------------------------------------------ */
/* Global state */

extern int cgi;								/* TRUE if we are running as a CGI program */
extern int cgiwritable;						/* TRUE if we can write the disk images back to the image file in CGI mode */
extern t_bool sim_gui;

extern uint16 M[];							/* core memory, up to 32Kwords (note: don't even think about trying 64K) */
extern uint16 ILSW[];						/* interrupt level status words */
extern int32  IAR;							/* instruction address register */
extern int32  prev_IAR;						/* instruction address register at start of current instruction */
extern int32  SAR, SBR;						/* storage address/buffer registers */
extern int32  OP, TAG, CCC;					/* instruction decoded pieces */
extern int32  CES;							/* console entry switches */
extern int32  ACC, EXT;						/* accumulator and extension */
extern int32  ARF;							/* arithmetic factor register, a nonaddressable internal CPU register */
extern int32  RUNMODE;						/* processor run/step mode */
extern int32  ipl;							/* current interrupt level (-1 = not handling irq) */
extern int32  iplpending;					/* interrupted IPL's */
extern int32  tbit;							/* trace flag (causes level 5 IRQ after each instr) */
extern int32  V, C;							/* condition codes */
extern int32  wait_state;					/* wait state (waiting for an IRQ) */
extern int32  wait_lamp;					/* alternate indicator to light the wait lamp on the GUI */
extern int32  int_req;						/* sum of interrupt request levels active */
extern int32  int_lamps;					/* accumulated version of int_req - gives lamp persistence */
extern int32  int_mask;						/* current active interrupt mask (ipl sensitive) */
extern int32  mem_mask;
extern int32  cpu_dsw;						/* CPU device status word */
extern int32  con_dsw;						/* has program stop and int run bits */
extern t_bool running;
extern t_bool power;
extern t_bool cgi;							/* TRUE if we are running as a CGI program */
extern t_bool cgiwritable;					/* TRUE if we can write to the disk image file in CGI mode */
extern t_stat reason;						/* CPU execution loop control */

#define WAIT_OP			 1		/* wait state causes: wait instruction, invalid instruction*/
#define WAIT_INVALID_OP  2

#define MODE_SS				3				/* RUNMODE values. SS and SMC are not implemented in this simulator */
#define MODE_SMC			2
#define MODE_INT_RUN		1
#define MODE_RUN			0
#define MODE_SI				-1
#define MODE_DISP			-2
#define MODE_LOAD			-3

/* ------------------------------------------------------------------------ */
/* debugging																*/
/* ------------------------------------------------------------------------ */

#define ENABLE_DEBUG_PRINT
#define ENABLE_DEBUG_TO_LOG

#ifdef ENABLE_DEBUG_PRINT
#  define DEBUG_PRINT debug_print
#else
#  ifdef ENABLE_DEBUG_TO_LOG
#      define DEBUG_PRINT trace_io
#  else
#      define DEBUG_PRINT if (0) debug_print
#  endif
#endif

void debug_print(const char *fmt, ...);

/* ------------------------------------------------------------------------ */
/* memory IO routines */

int32 ReadW  (int32 a); 
void  WriteW (int32 a, int32 d);

/* ------------------------------------------------------------------------ */
/* handy macros */

#define CLRBIT(v,b)    ((v) &= ~(b))
#define SETBIT(v,b)    ((v) |= (b))
#define BETWEEN(v,a,b) (((v) >= (a)) && ((v) <= (b)))

/* ------------------------------------------------------------------------ */
/* Simulator stop codes */

#define STOP_WAIT			1				/* wait, no events */
#define STOP_INVALID_INSTR	2				/* bad instruction */
#define STOP_IBKPT			3				/* simulator breakpoint */
#define STOP_INCOMPLETE		4				/* simulator coding not complete here */
#define STOP_POWER_OFF		5				/* no power */
#define STOP_DECK_BREAK		6				/* !BREAK in deck file */
#define STOP_PHASE_BREAK	7				/* phase load break */
#define STOP_CRASH			8				/* program has crashed badly */
#define STOP_TIMED_OUT		9				/* simulation time limit exceeded */
#define STOP_IMMEDIATE		10				/* simulator stop key pressed (immediate stop) */
#define STOP_BREAK			11				/* simulator break key pressed */
#define STOP_STEP			12				/* step count expired */
#define STOP_OTHER			13				/* other reason, probably error returned by sim_process_event() */
#define STOP_PRINT_CHECK	14				/* stop due to printer check (used by CGI version) */

#define IORETURN(f,v)	((f)? (v): SCPE_OK)	/* cond error return */

#define INT_REQ_5		0x01				/* bits for interrupt levels (ipl, iplpending, int_req, int_mask) */
#define INT_REQ_4		0x02
#define INT_REQ_3		0x04
#define INT_REQ_2		0x08
#define INT_REQ_1		0x10
#define INT_REQ_0		0x20

#define XIO_UNUSED		0x00				/* XIO commands */
#define XIO_WRITE		0x01
#define XIO_READ		0x02
#define XIO_SENSE_IRQ	0x03
#define XIO_CONTROL		0x04
#define XIO_INITW		0x05
#define XIO_INITR		0x06
#define XIO_SENSE_DEV	0x07

#define XIO_FAILED		0x20				/* fake function to record error */

/* ILSW bits - set by appropriate device whenever an interrupt is outstanding */

#define ILSW_0_1442_CARD			0x8000			/* ILSW 0 is not really defined on the 1130 */

#define ILSW_1_1132_PRINTER			0x8000			/* had these backwards! */
#define ILSW_1_SCA					0x4000

#define ILSW_2_1131_DISK			0x8000

#define ILSW_2_2310_DRV_1			0x4000
#define ILSW_2_2310_DRV_2			0x2000
#define ILSW_2_2310_DRV_3			0x1000
#define ILSW_2_2310_DRV_4			0x0800			/* can have 2310 or 2311 */

#define ILSW_2_2311_DRV_1_DISK_1	0x4000
#define ILSW_2_2311_DRV_1_DISK_2	0x2000
#define ILSW_2_2311_DRV_1_DISK_3	0x1000
#define ILSW_2_2311_DRV_1_DISK_4	0x0800

#define ILSW_2_2311_DRV_1_DISK_5	0x0400
#define ILSW_2_2311_DRV_2_DISK_1	0x0200
#define ILSW_2_2311_DRV_2_DISK_2	0x0100
#define ILSW_2_2311_DRV_2_DISK_3	0x0080
#define ILSW_2_2311_DRV_2_DISK_4	0x0040
#define ILSW_2_2311_DRV_2_DISK_5	0x0020

#define ILSW_2_SAC_BIT_11			0x0010
#define ILSW_2_SAC_BIT_12			0x0008
#define ILSW_2_SAC_BIT_13			0x0004
#define ILSW_2_SAC_BIT_14			0x0002
#define ILSW_2_SAC_BIT_15			0x0001

#define ILSW_3_1627_PLOTTER			0x8000
#define ILSW_3_SAC_BIT_01			0x4000
#define ILSW_3_SAC_BIT_02			0x2000
#define ILSW_3_SAC_BIT_03			0x1000
#define ILSW_3_2250_DISPLAY			0x0800
#define ILSW_3_SYSTEM7				0x0800
#define ILSW_3_SAC_BIT_05			0x0400
#define ILSW_3_SAC_BIT_06			0x0200
#define ILSW_3_SAC_BIT_07			0x0100
#define ILSW_3_SAC_BIT_08			0x0080
#define ILSW_3_SAC_BIT_09			0x0040
#define ILSW_3_SAC_BIT_10			0x0020
#define ILSW_3_SAC_BIT_11			0x0010
#define ILSW_3_SAC_BIT_12			0x0008
#define ILSW_3_SAC_BIT_13			0x0004
#define ILSW_3_SAC_BIT_14			0x0002
#define ILSW_3_SAC_BIT_15			0x0001

#define ILSW_4_1134_TAPE			0x8000
#define ILSW_4_1055_TAPE			0x8000
#define ILSW_4_CONSOLE				0x4000
#define ILSW_4_1442_CARD			0x2000
#define ILSW_4_2501_CARD			0x1000
#define ILSW_4_1403_PRINTER			0x0800
#define ILSW_4_1231_MARK			0x0400
#define ILSW_4_SAC_BIT_06			0x0200
#define ILSW_4_SAC_BIT_07			0x0100
#define ILSW_4_SAC_BIT_08			0x0080
#define ILSW_4_SAC_BIT_09			0x0040
#define ILSW_4_SAC_BIT_10			0x0020
#define ILSW_4_SAC_BIT_11			0x0010
#define ILSW_4_T2741_TERMINAL		0x0010	/* APL\1130 nonstandard serial interface uses this bit */
#define ILSW_4_SAC_BIT_12			0x0008
#define ILSW_4_SAC_BIT_13			0x0004
#define ILSW_4_SAC_BIT_14			0x0002
#define ILSW_4_SAC_BIT_15			0x0001

#define ILSW_5_INT_RUN_PROGRAM_STOP 0x8000	/* this replaces both ILSW_5_INT_RUN and ILSW_5_PROGRAM_STOP */
#define ILSW_5_SAC_BIT_01			0x4000
#define ILSW_5_SAC_BIT_02			0x2000
#define ILSW_5_SAC_BIT_03			0x1000
#define ILSW_5_SAC_BIT_04			0x0800
#define ILSW_5_SAC_BIT_05			0x0400
#define ILSW_5_SAC_BIT_06			0x0200
#define ILSW_5_SAC_BIT_07			0x0100
#define ILSW_5_SAC_BIT_08			0x0080
#define ILSW_5_SAC_BIT_09			0x0040
#define ILSW_5_SAC_BIT_10			0x0020
#define ILSW_5_SAC_BIT_11			0x0010
#define ILSW_5_SAC_BIT_12			0x0008
#define ILSW_5_SAC_BIT_13			0x0004
#define ILSW_5_SAC_BIT_14			0x0002
#define ILSW_5_SAC_BIT_15			0x0001

/* CPU  DSW bits */

#define CPU_DSW_PROGRAM_STOP			0x8000
#define CPU_DSW_INT_RUN					0x4000

/* prototypes: xio handlers */

void xio_1131_console	(int32 addr, int32 func, int32 modify);				/* console keyboard and printer */
void xio_1442_card		(int32 addr, int32 func, int32 modify);				/* standard card reader/punch */
void xio_1134_papertape	(int32 addr, int32 func, int32 modify);				/* paper tape reader/punch */
void xio_disk			(int32 addr, int32 func, int32 modify, int drv);	/* internal CPU disk */
void xio_1627_plotter	(int32 addr, int32 func, int32 modify);				/* XY plotter */
void xio_1132_printer	(int32 addr, int32 func, int32 modify);				/* standard line printer */
void xio_1131_switches	(int32 addr, int32 func, int32 modify);				/* console buttons & switches */
void xio_1231_optical	(int32 addr, int32 func, int32 modify);				/* optical mark page reader */
void xio_2501_card		(int32 addr, int32 func, int32 modify);				/* alternate high-speed card reader */
void xio_sca			(int32 addr, int32 func, int32 modify);				/* synchronous communications adapter */
void xio_system7		(int32 addr, int32 func, int32 modify);				/* system/7 interprocessor IO link */
void xio_1403_printer	(int32 addr, int32 func, int32 modify);				/* alternate high-speed printer */
void xio_2250_display	(int32 addr, int32 func, int32 modify);				/* vector display processor */
void xio_t2741_terminal (int32 addr, int32 func, int32 modify);				/* IO selectric via nonstandard serial interface for APL */
void xio_error 			(const char *msg);

void   bail (const char *msg);
t_stat load_cr_boot (int32 drv, int switches);
t_stat cr_boot (int32 unitno, DEVICE *dptr);
t_stat cr_rewind (void);
t_stat cr_detach (UNIT *uptr);
void   calc_ints (void);							/* recalculate interrupt bitmask */
void   trace_io (const char *fmt, ...);				/* debugging printout */
void   trace_both (const char *fmt, ...);			/* debugging printout */
void   scp_panic (const char *msg);					/* bail out of simulator */
char  *upcase(char *str);
void   break_simulation (t_stat reason);			/* let a device halt the simulation */
char   hollerith_to_ascii (uint16 hol);				/* for debugging use only */
t_bool gdu_active (void);
void   remark_cmd (char *remark);
void   stuff_cmd (char *cmd);
t_bool stuff_and_wait (char *cmd, int timeout, int delay);
void   update_gui (t_bool force);
void   sim_init (void);
t_stat register_cmd (const char *name, t_stat (*action)(int32 flag, CONST char *ptr), int arg, const char *help);
t_stat basic_attach (UNIT *uptr, CONST char *cptr);
CONST char * quotefix (CONST char *cptr, char * buf);

/* GUI interface routines */
t_bool keyboard_is_busy (void);
void   forms_check (int set);						/* device notification to console lamp display */
void   print_check (int set);
void   keyboard_selected (int select);				
void   disk_ready (int ready);
void   disk_unlocked (int unlocked);
void   gui_run(int running);
char   *read_cmdline (char *ptr, int size, FILE *stream);

#ifdef GUI_SUPPORT
#  define GUI_BEGIN_CRITICAL_SECTION begin_critical_section();
#  define GUI_END_CRITICAL_SECTION   end_critical_section();
   void begin_critical_section (void);
   void end_critical_section   (void);
#else
#  define GUI_BEGIN_CRITICAL_SECTION
#  define GUI_END_CRITICAL_SECTION
#endif
