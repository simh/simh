/* ibm1130_defs.h: IBM-1130 simulator definitions
 */

#include "sim_defs.h"						/* main SIMH defns (include path should include .., or make a copy) */
#include <setjmp.h>
#include <assert.h>

#if defined(VMS)
	#  include <unistd.h>  					/* to pick up 'unlink' */
#endif

#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#define MAX(a,b)  (((a) >= (b)) ? (a) : (b))

// #define ENABLE_GUI		// uncomment to compile the GUI extensions

/* ------------------------------------------------------------------------ */
/* Architectural constants */

#define MAXMEMSIZE	(32768)						/* 32Kwords */
#define INIMEMSIZE 	(16384)						/* 16Kwords */
#define MEMSIZE		(cpu_unit.capac)

#define UNIT_MSIZE	(1 << (UNIT_V_UF + 7))		/* flag for memory size setting */

#define ILL_ADR_FLAG	0x40000000				/* an impossible 1130 address */

/* ------------------------------------------------------------------------ */
/* Global state */

extern uint16 M[];				/* core memory, up to 32Kwords */
extern uint16 ILSW[];			/* interrupt level status words */
extern int32  IAR;				/* instruction address register */
extern int32  CES;				/* console entry switches */
extern int32  ACC, EXT;			/* accumulator and extension */
extern int32  ipl;				/* current interrupt level (-1 = not handling irq) */
extern int32  iplpending;		/* bitfield: interrupted IPL's */
extern int32  tbit;				/* trace flag (causes level 5 IRQ after each instr) */
extern int32  V, C;				/* condition codes: overflow, carry */
extern int32  wait_state;		/* wait state (waiting for an IRQ or processor halted) */
extern int32  int_req;			/* bitfield: interrupt request levels active */
extern int32  int_mask;			/* current active interrupt mask (ipl sensitive) */
extern int32  SR;				/* switch register */
extern int32  DR;				/* display register */
extern int32  wait_enable;		/* wait state enable */
extern int32  mem_mask;			/* mem_mask - valid address mask (memsize-1) */
extern int32  ibkpt_addr;		/* breakpoint addr */
extern int32  sim_int_char;

#define WAIT_OP			 1		/* wait state causes: wait instruction, invalid instruction*/
#define WAIT_INVALID_OP  2

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

#define IORETURN(f,v)	((f)? (v): SCPE_OK)	/* cond error return */

#define INT_REQ_0		0x01				/* bits for interrupt levels (ipl, iplpending, int_req, int_mask) */
#define INT_REQ_1		0x02
#define INT_REQ_2		0x04
#define INT_REQ_3		0x08
#define INT_REQ_4		0x10
#define INT_REQ_5		0x20

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

#define ILSW_0_1442_CARD			0x8000			/* not actually used */

#define ILSW_1_SCA					0x8000
#define ILSW_1_1132_PRINTER			0x4000

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
#define ILSW_4_SAC_BIT_12			0x0008
#define ILSW_4_SAC_BIT_13			0x0004
#define ILSW_4_SAC_BIT_14			0x0002
#define ILSW_4_SAC_BIT_15			0x0001

#define ILSW_5_INT_RUN				0x8000
#define ILSW_5_PROGRAM_STOP			0x8000
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

//* console DSW bits

#define CON_DSW_PROGRAM_STOP			0x8000
#define CON_DSW_INT_RUN					0x4000

/* prototypes: xio handlers */

void xio_1131_console	(int32 addr, int32 func, int32 modify);				// console keyboard and printer
void xio_1142_card		(int32 addr, int32 func, int32 modify);				// standard card reader/punch
void xio_1134_papertape	(int32 addr, int32 func, int32 modify);				// paper tape reader/punch
void xio_disk			(int32 addr, int32 func, int32 modify, int drv);	// internal CPU disk
void xio_1627_plotter	(int32 addr, int32 func, int32 modify);				// XY plotter
void xio_1132_printer	(int32 addr, int32 func, int32 modify);				// standard line printer
void xio_1131_switches	(int32 addr, int32 func, int32 modify);				// console buttons & switches
void xio_1231_optical	(int32 addr, int32 func, int32 modify);				// optical mark page reader
void xio_2501_card		(int32 addr, int32 func, int32 modify);				// alternate high-speed card reader
void xio_1131_synch		(int32 addr, int32 func, int32 modify);				// synchronous communications adapter
void xio_system7		(int32 addr, int32 func, int32 modify);				// system/7 interprocessor IO link
void xio_1403_printer	(int32 addr, int32 func, int32 modify);				// alternate high-speed printer
void xio_2250_display	(int32 addr, int32 func, int32 modify);				// vector display processor
void xio_error 			(char *msg);

void   bail (char *msg);
t_stat load_cr_boot (int drv);
t_stat cr_boot (int unitno);
void   calc_ints (void);							/* recalculate interrupt bitmask */
void   trace_io (char *fmt, ...);					/* debugging printout */
void   panic (char *msg);							/* bail out of simulator */
char  *upcase(char *str);

/* GUI interface routines */
void   remark_cmd (char *remark);
void   stuff_cmd (char *cmd);
t_bool keyboard_is_locked (void);
void   forms_check (int set);						/* device notification to console lamp display */
void   print_check (int set);
void   keyboard_selected (int select);				
void   disk_ready (int ready);
void   disk_unlocked (int unlocked);

#ifdef ENABLE_GUI
#  define GUI_BEGIN_CRITICAL_SECTION begin_critical_section();
#  define GUI_END_CRITICAL_SECTION   end_critical_section();
   void begin_critical_section (void);
   void end_critical_section   (void);
#else
#  define GUI_BEGIN_CRITICAL_SECTION
#  define GUI_END_CRITICAL_SECTION
#endif
