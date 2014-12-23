/* hp2100_defs.h: HP 2100 simulator definitions

   Copyright (c) 1993-2014, Robert M. Supnik

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

   15-Dec-14    JDB     Added "-Wunused-const-variable" to the suppression pragmas
   05-Feb-13    JDB     Added declaration for hp_fprint_stopped
   18-Mar-13    JDB     Added "-Wdangling-else" to the suppression pragmas
                        Removed redundant extern declarations
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   14-Dec-12    JDB     Added "-Wbitwise-op-parentheses" to the suppression pragmas
   12-May-12    JDB     Added pragmas to suppress logical operator precedence warnings
   10-Feb-12    JDB     Added hp_setsc, hp_showsc functions to support SC modifier
   28-Mar-11    JDB     Tidied up signal handling
   29-Oct-10    JDB     DMA channels renamed from 0,1 to 1,2 to match documentation
   27-Oct-10    JDB     Revised I/O signal enum values for concurrent signals
                        Revised I/O macros for new signal handling
   09-Oct-10    JDB     Added DA and DC device select code assignments
   07-Sep-08    JDB     Added POLL_FIRST to indicate immediate connection attempt
   15-Jul-08    JDB     Rearranged declarations with hp2100_cpu.h
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   25-Jun-08    JDB     Added PIF device
   17-Jun-08    JDB     Declared fmt_char() function
   26-May-08    JDB     Added MPX device
   24-Apr-08    JDB     Added I_MRG_I, I_JSB, I_JSB_I, and I_JMP instruction masks
   14-Apr-08    JDB     Changed TMR_MUX to TMR_POLL for idle support
                        Added POLLMODE, sync_poll() declaration
                        Added I_MRG, I_ISZ, I_IOG, I_STF, and I_SFS instruction masks
   07-Dec-07    JDB     Added BACI device
   10-Nov-07    JDB     Added 16/32-bit unsigned-to-signed conversions
   11-Jan-07    JDB     Added 12578A DMA byte packing to DMA structure
   28-Dec-06    JDB     Added CRS backplane signal as I/O pseudo-opcode
                        Added DMASK32 32-bit mask value
   21-Dec-06    JDB     Changed MEM_ADDR_OK for 21xx loader support
   12-Sep-06    JDB     Define NOTE_IOG to recalc interrupts after instr exec
                        Rename STOP_INDINT to NOTE_INDINT (not a stop condition)
   30-Dec-04    JDB     Added IBL_DS_HEAD head number mask
   19-Nov-04    JDB     Added STOP_OFFLINE, STOP_PWROFF stop codes
   25-Apr-04    RMS     Added additional IBL definitions
                        Added DMA EDT I/O pseudo-opcode
   25-Apr-03    RMS     Revised for extended file support
   24-Oct-02    RMS     Added indirect address interrupt
   08-Feb-02    RMS     Added DMS definitions
   01-Feb-02    RMS     Added terminal multiplexor support
   16-Jan-02    RMS     Added additional device support
   30-Nov-01    RMS     Added extended SET/SHOW support
   15-Oct-00    RMS     Added dynamic device numbers
   14-Apr-99    RMS     Changed t_addr to unsigned

   The author gratefully acknowledges the help of Jeff Moffat in answering
   questions about the HP2100; and of Dave Bryan in adding features and
   correcting errors throughout the simulator.
*/


#ifndef HP2100_DEFS_H_
#define HP2100_DEFS_H_ 0

#include "sim_defs.h"                                   /* simulator defns */


/* Required to quell clang precedence warnings */

#if defined (__GNUC__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wlogical-op-parentheses"
#pragma GCC diagnostic ignored "-Wbitwise-op-parentheses"
#pragma GCC diagnostic ignored "-Wdangling-else"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif


/* Simulator stop and notification codes */

#define STOP_RSRV       1                               /* must be 1 */
#define STOP_IODV       2                               /* must be 2 */
#define STOP_HALT       3                               /* HALT */
#define STOP_IBKPT      4                               /* breakpoint */
#define STOP_IND        5                               /* indirect loop */
#define NOTE_INDINT     6                               /* indirect intr */
#define STOP_NOCONN     7                               /* no connection */
#define STOP_OFFLINE    8                               /* device offline */
#define STOP_PWROFF     9                               /* device powered off */
#define NOTE_IOG        10                              /* I/O instr executed */

/* Memory */

#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define VA_N_SIZE       15                              /* virtual addr size */
#define VASIZE          (1 << VA_N_SIZE)
#define VAMASK          077777                          /* virt addr mask */
#define PA_N_SIZE       20                              /* phys addr size */
#define PASIZE          (1 << PA_N_SIZE)
#define PAMASK          (PASIZE - 1)                    /* phys addr mask */

/* Architectural constants */

#define SIGN32          020000000000                    /* 32b sign */
#define DMASK32         037777777777                    /* 32b data mask/maximum value */
#define DMAX32          017777777777                    /* 32b maximum signed value */
#define SIGN            0100000                         /* 16b sign */
#define DMASK           0177777                         /* 16b data mask/maximum value */
#define DMAX            0077777                         /* 16b maximum signed value */
#define DMASK8          0377                            /* 8b data mask/maximum value */

/* Portable conversions (sign-extension, unsigned-to-signed) */

#define SEXT(x)         ((int32) (((x) & SIGN)? ((x) | ~DMASK): ((x) & DMASK)))

#define INT16(u)        ((u) > DMAX   ? (-(int16) (DMASK   - (u)) - 1) : (int16) (u))
#define INT32(u)        ((u) > DMAX32 ? (-(int32) (DMASK32 - (u)) - 1) : (int32) (u))

/* Timers */

#define TMR_CLK         0                               /* clock */
#define TMR_POLL        1                               /* input polling */

#define POLL_RATE       100                             /* poll 100 times per second */
#define POLL_FIRST      1                               /* first poll is "immediate" */
#define POLL_WAIT       15800                           /* initial poll ~ 10 msec. */

typedef enum { INITIAL, SERVICE } POLLMODE;             /* poll synchronization modes */

/* I/O instruction sub-opcodes */

#define soHLT           0                               /* halt */
#define soFLG           1                               /* set/clear flag */
#define soSFC           2                               /* skip on flag clear */
#define soSFS           3                               /* skip on flag set */
#define soMIX           4                               /* merge into A/B */
#define soLIX           5                               /* load into A/B */
#define soOTX           6                               /* output from A/B */
#define soCTL           7                               /* set/clear control */

/* I/O devices - fixed select code assignments */

#define CPU             000                             /* interrupt control */
#define OVF             001                             /* overflow */
#define DMALT1          002                             /* DMA 1 alternate */
#define DMALT2          003                             /* DMA 2 alternate */
#define PWR             004                             /* power fail */
#define PRO             005                             /* parity/mem protect */
#define DMA1            006                             /* DMA channel 1 */
#define DMA2            007                             /* DMA channel 2 */

/* I/O devices - variable select code assignment defaults */

#define PTR             010                             /* 12597A-002 paper tape reader */
#define TTY             011                             /* 12531C teleprinter */
#define PTP             012                             /* 12597A-005 paper tape punch */
#define CLK             013                             /* 12539C time-base generator */
#define LPS             014                             /* 12653A line printer */
#define LPT             015                             /* 12845A line printer */
#define MTD             020                             /* 12559A data */
#define MTC             021                             /* 12559A control */
#define DPD             022                             /* 12557A data */
#define DPC             023                             /* 12557A control */
#define DQD             024                             /* 12565A data */
#define DQC             025                             /* 12565A control */
#define DRD             026                             /* 12610A data */
#define DRC             027                             /* 12610A control */
#define MSD             030                             /* 13181A data */
#define MSC             031                             /* 13181A control */
#define IPLI            032                             /* 12566B link in */
#define IPLO            033                             /* 12566B link out */
#define DS              034                             /* 13037A control */
#define BACI            035                             /* 12966A Buffered Async Comm Interface */
#define MPX             036                             /* 12792A/B/C 8-channel multiplexer */
#define PIF             037                             /* 12620A/12936A Privileged Interrupt Fence */
#define MUXL            040                             /* 12920A lower data */
#define MUXU            041                             /* 12920A upper data */
#define MUXC            042                             /* 12920A control */
#define DI_DA           043                             /* 12821A Disc Interface with Amigo disc devices */
#define DI_DC           044                             /* 12821A Disc Interface with CS/80 disc and tape devices */

#define OPTDEV          002                             /* start of optional devices */
#define CRSDEV          006                             /* start of devices that receive CRS */
#define VARDEV          010                             /* start of variable assignments */
#define MAXDEV          077                             /* end of select code range */

/* IBL assignments */

#define IBL_V_SEL       14                              /* ROM select <15:14> */
#define IBL_M_SEL       03
#define IBL_PTR         0000000                         /* ROM 0: 12992K paper tape reader (PTR) */
#define IBL_DP          0040000                         /* ROM 1: 12992A 7900 disc (DP) */
#define IBL_DQ          0060000                         /* ROM 1: 12992A 2883 disc (DQ) */
#define IBL_MS          0100000                         /* ROM 2: 12992D 7970 tape (MS) */
#define IBL_DS          0140000                         /* ROM 3: 12992B 7905/06/20/25 disc (DS) */
#define IBL_MAN         0010000                         /* RPL/manual boot <13:12> */
#define IBL_V_DEV       6                               /* select code <11:6> */
#define IBL_OPT         0000070                         /* options in <5:3> */
#define IBL_DP_REM      0000001                         /* DP removable <0:0> */
#define IBL_DS_HEAD     0000003                         /* DS head number <1:0> */
#define IBL_LNT         64                              /* boot ROM length in words */
#define IBL_MASK        (IBL_LNT - 1)                   /* boot length mask */
#define IBL_DPC         (IBL_LNT - 2)                   /* DMA ctrl word */
#define IBL_END         (IBL_LNT - 1)                   /* last location */

typedef uint16 BOOT_ROM [IBL_LNT];                      /* boot ROM data */


/* I/O backplane signals.

   The IOSIGNAL declarations mirror the hardware I/O backplane signals.  A set
   of one or more signals forms an IOCYCLE that is sent to a device IOHANDLER
   for action.  The CPU and DMA dispatch one signal set to the target device
   handler per I/O cycle.  A CPU cycle consists of either one or two signals; if
   present, the second signal will be CLF.  A DMA cycle consists of from two to
   five signals.  In addition, a front-panel PRESET or power-on reset dispatches
   two or three signals, respectively.

   In hardware, signals are assigned to one or more specific I/O T-periods, and
   some signals are asserted concurrently.  For example, a programmed STC sc,C
   instruction asserts the STC and CLF signals together in period T4.  Under
   simulation, signals are ORed to form an I/O cycle; in this example, the
   signal handler would receive an IOCYCLE value of "ioSTC | ioCLF".

   Hardware allows parallel action for concurrent signals.  Under simulation, a
   "concurrent" set of signals is processed sequentially by the signal handler
   in order of ascending numerical value.  Although assigned T-periods differ
   between programmed I/O and DMA I/O cycles, a single processing order is used.
   The order of execution generally follows the order of T-period assertion,
   except that ioSIR is processed after all other signals that may affect the
   interrupt request chain.

   Implementation notes:

    1. The ioCLF signal must be processed after ioSFS/ioSFC to ensure that a
       true skip test generates ioSKF before the flag is cleared, and after
       ioIOI/ioIOO/ioSTC/ioCLC to meet the requirement that executing an
       instruction having the H/C bit set is equivalent to executing the same
       instruction with the H/C bit clear and then a CLF instruction.

    2. The ioSKF signal is never sent to an I/O handler.  Rather, it is returned
       from the handler if the SFC or SFS condition is true.  If the condition
       is false, ioNONE is returned instead.  As these two values are returned
       in the 16-bit data portion of the returned value, their assigned values
       must be <= 100000 octal.

    3. An I/O handler will receive ioCRS as a result of a CLC 0 instruction,
       ioPOPIO and ioCRS as a result of a RESET command, and ioPON, ioPOPIO, and
       ioCRS as a result of a RESET -P command.

    4. An I/O handler will receive ioNONE when a HLT instruction is executed
       that has the H/C bit clear (i.e., no CLF generated).

    5. In hardware, the SIR signal is generated unconditionally every T5 period
       to time the setting of the IRQ flip-flop.  Under simulation, ioSIR
       indicates that the I/O handler must set the PRL, IRQ, and SRQ signals as
       required by the interface logic.  ioSIR must be included in the I/O cycle
       if any of the flip-flops affecting these signals are changed and the
       interface supports interrupts or DMA transfers.

    6. In hardware, the ENF signal is unconditionally generated every T2 period
       to time the setting of the flag flip-flop and to reset the IRQ flip-flop.
       If the flag buffer flip-flip is set, then flag will be set by ENF.  If
       the flag buffer is clear, ENF will not affect flag.  Under simulation,
       ioENF is sent to set the flag buffer and flag flip-flops.  For those
       interfaces where this action is identical to that provided by STF, the
       ioENF handler may simply fall into the ioSTF handler.

    7. In hardware, the PON signal is asserted continuously while the CPU is
       operating.  Under simulation, ioPON is asserted only at simulator
       initialization or when processing a RESET -P command.
*/

typedef enum { ioNONE  = 0000000,                       /* -- -- -- -- -- no signal asserted */
               ioPON   = 0000001,                       /* T2 T3 T4 T5 T6 power on normal */
               ioENF   = 0000002,                       /* T2 -- -- -- -- enable flag */
               ioIOI   = 0000004,                       /* -- -- T4 T5 -- I/O data input (CPU)
                                                           T2 T3 -- -- -- I/O data input (DMA) */
               ioIOO   = 0000010,                       /* -- T3 T4 -- -- I/O data output */
               ioSKF   = 0000020,                       /* -- T3 T4 T5 -- skip on flag */
               ioSFS   = 0000040,                       /* -- T3 T4 T5 -- skip if flag is set */
               ioSFC   = 0000100,                       /* -- T3 T4 T5 -- skip if flag is clear */
               ioSTC   = 0000200,                       /* -- -- T4 -- -- set control flip-flop (CPU)
                                                           -- T3 -- -- -- set control flip-flop (DMA) */
               ioCLC   = 0000400,                       /* -- -- T4 -- -- clear control flip-flop (CPU)
                                                           -- T3 T4 -- -- clear control flip-flop (DMA) */
               ioSTF   = 0001000,                       /* -- T3 -- -- -- set flag flip-flop */
               ioCLF   = 0002000,                       /* -- -- T4 -- -- clear flag flip-flop (CPU)
                                                           -- T3 -- -- -- clear flag flip-flop (DMA) */
               ioEDT   = 0004000,                       /* -- -- T4 -- -- end data transfer */
               ioCRS   = 0010000,                       /* -- -- -- T5 -- control reset */
               ioPOPIO = 0020000,                       /* -- -- -- T5 -- power-on preset to I/O */
               ioIAK   = 0040000,                       /* -- -- -- -- T6 interrupt acknowledge */
               ioSIR   = 0100000 } IOSIGNAL;            /* -- -- -- T5 -- set interrupt request */


typedef uint32 IOCYCLE;                                 /* a set of signals forming one I/O cycle */

#define IOIRQSET        (ioSTC | ioCLC | ioENF | \
                         ioSTF | ioCLF | ioIAK | \
                         ioCRS | ioPOPIO | ioPON)       /* signals that may affect interrupt state */


/* I/O structures */

typedef enum { CLEAR, SET } FLIP_FLOP;                  /* flip-flop type and values */

typedef struct dib DIB;                                 /* incomplete definition */

typedef uint32 IOHANDLER (DIB     *dibptr,              /* I/O signal handler prototype */
                          IOCYCLE signal_set,
                          uint32  stat_data);

struct dib {                                            /* Device information block */
    IOHANDLER  *io_handler;                             /* pointer to device's I/O signal handler */
    uint32     select_code;                             /* device's select code */
    uint32     card_index;                              /* device's card index for state variables */
    };


/* I/O signal and status macros.

   The following macros are useful in I/O signal handlers and unit service
   routines.  The parameter definition symbols employed are:

     I = an IOCYCLE value
     E = a t_stat error status value
     D = a uint16 data value
     C = a uint32 combined status and data value
     P = a pointer to a DIB structure
     B = a Boolean test value

   Implementation notes:

    1. The IONEXT macro isolates the next signal in sequence to process from the
       I/O cycle I.

    2. The IOADDSIR macro adds an ioSIR signal to the I/O cycle I if it
       contains signals that might change the interrupt state.

    3. The IORETURN macro forms the combined status and data value to be
       returned by a handler from the t_stat error code E and the 16-bit data
       value D.

    4. The IOSTATUS macro isolates the t_stat error code from a combined status
       and data value value C.

    5. The IODATA macro isolates the 16-bit data value from a combined status
       and data value value C.

    6. The IOPOWERON macro calls signal handler P->H with DIB pointer P to
       process a power-on reset action.

    7. The IOPRESET macro calls signal handler P->H with DIB pointer P to
       process a front-panel PRESET action.

    8. The IOERROR macro returns t_stat error code E from a unit service routine
       if the Boolean test B is true.
*/

#define IONEXT(I)       (IOSIGNAL) ((I) & (IOCYCLE) (- (int32) (I)))        /* extract next I/O signal to handle */
#define IOADDSIR(I)     ((I) & IOIRQSET ? (I) | ioSIR : (I))                /* add SIR if IRQ state might change */

#define IORETURN(E,D)   ((uint32) ((E) << 16 | (D) & DMASK))                /* form I/O handler return value */
#define IOSTATUS(C)     ((t_stat) ((C) >> 16) & DMASK)                      /* extract I/O status from combined value */
#define IODATA(C)       ((uint16) ((C) & DMASK))                            /* extract data from combined value */

#define IOPOWERON(P)    (P)->io_handler ((P), ioPON | ioPOPIO | ioCRS, 0)   /* send power-on signals to handler */
#define IOPRESET(P)     (P)->io_handler ((P), ioPOPIO | ioCRS, 0)           /* send PRESET signals to handler */
#define IOERROR(B,E)    ((B) ? (E) : SCPE_OK)                               /* stop on I/O error if enabled */


/* I/O signal logic macros.

   The following macros implement the logic for the SKF, PRL, IRQ, and SRQ
   signals.  Both standard and general logic macros are provided.  The parameter
   definition symbols employed are:

     S = a uint32 select code value
     B = a Boolean test value
     N = a name of a structure containing the standard flip-flops

   Implementation notes:

    1. The setSKF macro sets the Skip on Flag signal in the return data value if
       the Boolean value B is true.

    2. The setPRL macro sets the Priority Low signal for select code S to the
       Boolean value B.

    3. The setIRQ macro sets the Interrupt Request signal for select code S to
       the Boolean value B.

    4. The setSRQ macro sets the Service Request signal for select code S to the
       Boolean value B.

    5. The PRL macro returns the Priority Low signal for select code S as a
       Boolean value.

    6. The IRQ macro returns the Interrupt Request signal for select code S as a
       Boolean value.

    7. The SRQ macro returns the Service Request signal for select code S as a
       Boolean value.

    8. The setstdSKF macro sets Skip on Flag signal in the return data value if
       the flag state in structure N matches the current skip test condition.

    9. The setstdPRL macro sets the Priority Low signal for the select code
       referenced by "dibptr" using the standard logic and the control and flag
       states in structure N.

   10. The setstdIRQ macro sets the Interrupt Request signal for the select code
       referenced by "dibptr" using the standard logic and the control, flag,
       and flag buffer states in structure N.

   11. The setstdSRQ macro sets the Service Request signal for the select code
       referenced by "dibptr" using the standard logic and the flag state in
       structure N.
*/

#define BIT_V(S)        ((S) & 037)                                     /* convert select code to bit position */
#define BIT_M(S)        (1u << BIT_V (S))                               /* convert select code to bit mask */

#define setSKF(B)       stat_data = IORETURN (SCPE_OK, (uint16) ((B) ? ioSKF : ioNONE))

#define setPRL(S,B)     dev_prl[(S)/32] = dev_prl[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))
#define setIRQ(S,B)     dev_irq[(S)/32] = dev_irq[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))
#define setSRQ(S,B)     dev_srq[(S)/32] = dev_srq[(S)/32] & ~BIT_M (S) | (((B) & 1) << BIT_V (S))

#define PRL(S)          ((dev_prl[(S)/32] >> BIT_V (S)) & 1)
#define IRQ(S)          ((dev_irq[(S)/32] >> BIT_V (S)) & 1)
#define SRQ(S)          ((dev_srq[(S)/32] >> BIT_V (S)) & 1)

#define setstdSKF(N)    setSKF ((signal == ioSFC) && !N.flag || \
                                (signal == ioSFS) && N.flag)

#define setstdPRL(N)    setPRL (dibptr->select_code, !(N.control & N.flag));
#define setstdIRQ(N)    setIRQ (dibptr->select_code, N.control & N.flag & N.flagbuf);
#define setstdSRQ(N)    setSRQ (dibptr->select_code, N.flag);


/* CPU state */

extern uint32 SR;                                       /* S register (for IBL) */
extern uint32 dev_prl [2], dev_irq [2], dev_srq [2];    /* I/O signal vectors */

/* CPU functions */

extern t_stat ibl_copy       (const BOOT_ROM rom, int32 dev);
extern void   hp_enbdis_pair (DEVICE *ccp, DEVICE *dcp);

/* System functions */

extern const char *fmt_char   (uint8 ch);
extern t_stat      hp_setsc   (UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat      hp_showsc  (FILE *st, UNIT *uptr, int32 val, void *desc);
extern t_stat      hp_setdev  (UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat      hp_showdev (FILE *st, UNIT *uptr, int32 val, void *desc);
extern t_bool      hp_fprint_stopped (FILE *st, t_stat reason);

/* Device-specific functions */

extern int32 sync_poll (POLLMODE poll_mode);

#endif
