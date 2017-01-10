/* opcon.h: Interface to a real operator console

   Copyright (c) 2006-2016, Edward Groenenberg & Henk Gooijen

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

   Except as contained in this notice, the names of the author(s) shall not
   be used in advertising or otherwise to promote the sale, use or other
   dealings in this Software without prior written authorization from the
   author(s).

   27-Apr-16    EG      Rewrote, consoletask is now a separate process
   20-mar-14    EG      new oc_svc, oc_get_rotary, minor changes
   08-Feb-14    EG      Rewrite of original realcons.c & adapted for simh 4.0 
*/

#ifndef OC_DEFS
#define OC_DEFS 1
/*
 * Implementation notes are found in doc/opcon_doc.txt
*/

//#define DEBUG_OC 1 				/* enable/disable debug */

#ifndef MOD_1145                
#define MOD_1145	10
#endif
#ifndef MOD_1170
#define MOD_1170	12
#endif

#define INP1			0
#define INP2			1
#define INP3			2
#define INP4			3
#define INP5			4
#define SWR_00_07_PORT	     INP1	/* SWITCH REGISTER 7-0 */
#define SWR_08_15_PORT	     INP2	/* SWITCH REGISTER 15-8 */
#define SWR_16_22_PORT	     INP3	/* SWITCH REGISTER 16-22 */

/* 11/45 switches / ports, etc. */
#define SW_PL_1145	     0x80	/* key switch  bitfield */
#define SW_HE_1145	     0x01	/* HALT bitfield */

/* 11/70 switches / ports, etc. */
#define SW_PL_1170	     0x80	/* key switch bitfield */
#define SW_HE_1170	     0x40	/* HALT bitfield */

/* DISPLAY DATA rotary switch for 11/45 & 11/70 */
#define DSPD_BUS_REG	     0x00	/* BUS REG */
#define DSPD_DATA_PATHS	     0x01	/* DATA PATHS */
#define DSPD_DISP_REG	     0x02	/* DISPLAY REGISTER */
#define DSPD_MU_ADRS	     0x03	/* uADRS FPP/CPU */
#define DSPD_MASK	     0x03	/* mask for DSPA range */

/* DISPLAY ADDRESS rotary switch for 11/45 & 11/70 */
#define DSPA_PROGPHY	     0x00	/* PROG PHY */
#define DSPA_KERNEL_D	     0x01	/* KERNEL D */
#define DSPA_KERNEL_I	     0x02	/* KERNEL I */
#define DSPA_CONSPHY	     0x03	/* CONS PHY */
#define DSPA_SUPER_D	     0x04	/* SUPER D */
#define DSPA_SUPER_I	     0x05	/* SUPER I */
#define DSPA_USER_D	     0x06	/* USER D */
#define DSPA_USER_I	     0x07	/* USER I */
#define DSPA_MASK	     0x07	/* mask for DSPA range */
/* Ack_toggle flag definitions */
#define ACK_DEPO	     0x40
#define ACK_CONT	     0x08
#define ACK_LOAD	     0x04
#define ACK_START	     0x02
#define ACK_EXAM	     0x01
#define ACK_MASK	     0x4F

/* Definitions copied from pdp11_defs.h, including it directly causes errors. */
#define MMR0_MME	  0000001	/* 18 bit MMU enabled */
#define MMR3_M22E	      020	/* 22 bit MMU enabled */
#define MD_KER			0	/* protection mode - KERNEL */
#define MD_SUP			1	/* protection mode - SUPERVISOR */
#define MD_UND			2	/* protection mode - UNDEFINED */
#define MD_USR			3	/* protection mode - USER */

/* Shared function/status port LEDs definitions */
#define FSTS_RUN                0x80

/* STAT_1_OUTPORT 11/70 */
/* out3  [2] |  RUN  | MASTER| PAUSE |ADRSERR| PARERR|INDDATA|MMR0[1]|MMR0[0]*/
#define FSTS_1170_RUN           0x80
#define FSTS_1170_MASTER	0x40
#define FSTS_1170_PAUSE	        0x20
#define FSTS_1170_ADRSERR	0x10
#define FSTS_1170_PARERR	0x08
#define FSTS_1170_INDDATA	0x04
#define FSTS_1170_USER	        0x03
#define FSTS_1170_SUPER	        0x01		/*  value 0x02 is all 3 OFF */
#define FSTS_1170_KERNEL	0x00

/* STAT_2_OUTPORT 11/70 */
/* out2  [1] |       |       |       | PARHI | PARLO | 22BIT | 18BIT | 16BIT */
#define FSTS_1170_PARHI	        0x10
#define FSTS_1170_PARLO	        0x08
#define FSTS_1170_22BIT	        0x04
#define FSTS_1170_18BIT	        0x02
#define FSTS_1170_16BIT	        0x01

/* STAT_1_OUTPORT 11/45 (11/50 & 11/55)	*/
/* out6  [5] |  RUN  | MASTER|ADRSERR| PAUSE |       |INDATA |MMR0[1]|MMR0[0] */
#define FSTS_1145_RUN           0x80
#define FSTS_1145_MASTER        0x40
#define FSTS_1145_ADRSERR       0x20
#define FSTS_1145_PAUSE	        0x10
#define FSTS_1145_INDDATA       0x04
#define FSTS_1145_USER	        0x03
#define FSTS_1145_SUPER	        0x01		/*  value 0x02 is all 3 OFF */
#define FSTS_1145_KERNEL        0x00

/* STAT_2_OUTPORT 11/45, 11/50 & 11/55  --> not used */


/* index values for data array */
#define DISP_SHFR	0	/* data paths (shiftr); normal setting  */
#define DISP_BR		1	/* read/write data                      */
#define DISP_FPP	2	/* uAdrs/FPP                            */
#define DISP_DR		3	/* Display Register                     */
#define DISP_BDV	4	/* non-standard BDV                     */

/* index values for address array */
#define ADDR_KERNI	0	/* Kernel I */
#define ADDR_KERND	1	/* Kernel D */
#define ADDR_SUPRI	2	/* Super  I */
#define ADDR_SUPRD	3	/* Super  D */
#define ADDR_ILLI	4	/* Not used */
#define ADDR_ILLD	5	/* Not used */
#define ADDR_USERI	6	/* User   I */
#define ADDR_USERD	7	/* User   D */
#define ADDR_PRGPA	8	/* Prog PA  */
#define ADDR_CONPA	9	/* Cons PA  */

/* OC controlblock */
struct oc_st {
  t_bool first_exam;		/* flag: first EXAM action */
  t_bool first_dep;		/* flag: first DEP action */
  t_bool ind_addr;		/* flag: indirect data access */
  t_bool inv_addr;		/* flag: invalid address (out of range )*/
  uint32 act_addr;		/* used address for EXAM/DEP */
  uint8  HALT;			/* HALT switch modes */
  uint8  PORT1;			/* status register 1 */
  uint8  PORT2;			/* status register 2 */
  uint32 A[10];			/* Address Mux array */
  uint16 D[5];			/* Data Mux array */
  uint8  S[5];			/* switches and toggles retrieved state */
  };

#ifndef OPCON_SIMH

/* defines & declarations for simh only part */
extern uint32 cpu_model;
extern struct oc_st oc_ctl;

int pthread_create(pthread_t *a, const pthread_attr_t *b, void *(*c)(void*), void *d);
int pthread_join(pthread_t a, void **d);

/* function prototypes simh integration */

t_stat oc_attach (UNIT *uptr, char *cptr);
t_stat oc_detach (UNIT *uptr);
char  *oc_description (DEVICE *dptr);
t_stat oc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat oc_reset (DEVICE *dptr);
t_stat oc_show (FILE *st, UNIT *uptr, int32 flag, void *desc);
t_stat oc_svc (UNIT *uptr);
t_stat oc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat oc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);

/* function prototypes OC */
void  *oc_thread(void *oc_end);
void   oc_clear_halt (void);
uint16 oc_extract_data (void);
uint32 oc_extract_address (void);
t_bool oc_get_console (char *cptr);
t_bool oc_get_halt (void);
int    oc_get_rotary (void);
int    oc_get_SWR (void);
int    oc_halt_status (void);
void   oc_mmu (void);
void   oc_port1 (uint8 flag, t_bool action);
void   oc_port2 (uint8 flag, t_bool action);
char  *oc_read_line_p (char *prompt, char *cptr, int32 size, FILE *stream);
void   oc_ringprot (int value);
void   oc_master (t_bool flag);
t_bool oc_poll (int channel, int amount);
void   oc_send_A (void);
void   oc_send_AD (void);
void   oc_send_ADS (void);
void   oc_send_D (void);
void   oc_send_status (void);
void   oc_toggle_ack (uint8 mask);
void   oc_toggle_clear (void);
void   oc_wait (t_bool flag);

#endif

#endif /* OC_DEFS */

