/* hp2100_defs.h: HP 2100 simulator definitions

   Copyright (c) 1993-2008, Robert M. Supnik

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


#ifndef _HP2100_DEFS_H_
#define _HP2100_DEFS_H_ 0

#include "sim_defs.h"                                   /* simulator defns */


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


/* I/O backplane signals.

   The IOSIG declarations mirror the I/O backplane signals.  These are sent to
   the device I/O signal handlers for action.  Normally, only one signal may be
   sent at a time.  However, the ioCLF signal may be added (arithmetically) to
   another signal; the handlers will process the other signal first and then the
   CLF signal.

   Implementation notes:

    1. The first valid signal must have a value > 0, and ioCLF must be
       enumerated last, so that adding ioCLF produces a result > ioCLF.

    2. The signals are structured so that all those that might change the
       interrupt state are enumerated after ioSIR.  The handlers will detect
       this and add an ioSIR signal automatically.

    3. In hardware, the POPIO signal is asserted concurrently with the CRS
       signal.  Under simulation, ioPOPIO implies ioCRS, so the handlers are
       structured to fall from POPIO handling into CRS handling.  It is not
       necessary to send both signals for a PRESET.

    4. In hardware, the SIR signal is generated unconditionally every T5 period
       to time the setting of the IRQ flip-flop.  Under simulation, ioSIR is
       sent to set the PRL, IRQ, and SRQ signals as indicated by the interface
       logic.  It is necessary to send ioSIR only when that logic indicates a
       change in one or more of the three signals.

    5. In hardware, the ENF signal is unconditionally generated every T2 period
       to time the setting of the flag flip-flop and to reset the IRQ flip-flop.
       If the flag buffer flip-flip is set, then flag will be set by ENF.  If
       the flag buffer is clear, ENF will not affect flag.  Under simulation,
       ioENF is sent to set the flag buffer and flag flip-flops.  For those
       interfaces where this action is identical to that provided by STF, the
       ioENF handler may simply fall into the ioSTF handler.

    6. The ioSKF signal is never sent to an I/O device.  Rather, it is returned
       from the device if the SFC or SFS condition is true.

    7. A device will receive ioNONE when a HLT instruction is executed, and the
       H/C bit is clear (i.e., no CLF generated).
*/

typedef enum { CLEAR, SET } FLIP_FLOP;                  /* flip-flop type and values */

typedef enum { ioNONE,                                  /* no signal asserted */
               ioSKF,                                   /* skip on flag */
               ioSFC,                                   /* skip if flag is clear */
               ioSFS,                                   /* skip if flag is set */
               ioIOI,                                   /* I/O data input */
               ioIOO,                                   /* I/O data output */
               ioEDT,                                   /* end data transfer */
               ioSIR,                                   /* set interrupt request */
               ioIAK,                                   /* interrupt acknowledge */
               ioCRS,                                   /* control reset */
               ioPOPIO,                                 /* power-on preset to I/O */
               ioCLC,                                   /* clear control flip-flop */
               ioSTC,                                   /* set control flip-flop */
               ioENF,                                   /* enable flag */
               ioSTF,                                   /* set flag flip-flop */
               ioCLF } IOSIG;                           /* clear flag flip-flop */

/* I/O devices - fixed assignments */

#define CPU             000                             /* interrupt control */
#define OVF             001                             /* overflow */
#define DMALT0          002                             /* DMA 0 alternate */
#define DMALT1          003                             /* DMA 1 alternate */
#define PWR             004                             /* power fail */
#define PRO             005                             /* parity/mem protect */
#define DMA0            006                             /* DMA channel 0 */
#define DMA1            007                             /* DMA channel 1 */
#define OPTDEV          DMALT0                          /* start of optional devices */
#define VARDEV          (DMA1 + 1)                      /* start of var assign */
#define M_NXDEV         (INT_M (CPU) | INT_M (OVF) | \
                         INT_M (DMALT0) | INT_M (DMALT1))
#define M_FXDEV         (M_NXDEV | INT_M (PWR) | INT_M (PRO) | \
                         INT_M (DMA0) | INT_M (DMA1))

/* I/O devices - variable assignment defaults */

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

/* IBL assignments */

#define IBL_V_SEL       14                              /* ROM select */
#define IBL_M_SEL       03
#define IBL_PTR         0000000                         /* PTR */
#define IBL_DP          0040000                         /* disk: DP */
#define IBL_DQ          0060000                         /* disk: DQ */
#define IBL_MS          0100000                         /* option 0: MS */
#define IBL_DS          0140000                         /* option 1: DS */
#define IBL_MAN         0010000                         /* RPL/man boot */
#define IBL_V_DEV       6                               /* dev in <11:6> */
#define IBL_OPT         0000070                         /* options in <5:3> */
#define IBL_DP_REM      0000001                         /* DP removable */
#define IBL_DS_HEAD     0000003                         /* DS head number */
#define IBL_LNT         64                              /* boot ROM length */
#define IBL_MASK        (IBL_LNT - 1)                   /* boot length mask */
#define IBL_DPC         (IBL_LNT - 2)                   /* DMA ctrl word */
#define IBL_END         (IBL_LNT - 1)                   /* last location */

typedef uint16 BOOT_ROM [IBL_LNT];                      /* boot ROM data */

/* Dynamic device information table */

typedef uint32 IODISP (uint32 select_code, IOSIG signal, uint32 data);  /* I/O signal dispatch function */

typedef struct {
    uint32 devno;                                       /* device select code */
    IODISP *iot;                                        /* pointer to I/O signal dispatcher */
    } DIB;

/* I/O macros */

#define IOBASE(S)       ((S) > ioCLF ? (S) - ioCLF : (S))   /* base signal from compound signal */

#define INT_V(x)        ((x) & 037)                         /* device bit position */
#define INT_M(x)        (1u << INT_V (x))                   /* device bit mask */

#define setSKF(B)       data = (uint32) ((B) ? ioSKF : ioNONE)

#define setPRL(S,B)     dev_prl[(S)/32] = dev_prl[(S)/32] & ~INT_M (S) | (((B) & 1) << INT_V (S))
#define setIRQ(S,B)     dev_irq[(S)/32] = dev_irq[(S)/32] & ~INT_M (S) | (((B) & 1) << INT_V (S))
#define setSRQ(S,B)     dev_srq[(S)/32] = dev_srq[(S)/32] & ~INT_M (S) | (((B) & 1) << INT_V (S))

#define setstdSKF(N)    setSKF ((base_signal == ioSFC) && !N ## _flag || \
                                (base_signal == ioSFS) && N ## _flag)

#define setstdPRL(S,N)  setPRL ((S), !(N ## _control & N ## _flag));
#define setstdIRQ(S,N)  setIRQ ((S), N ## _control & N ## _flag & N ## _flagbuf);
#define setstdSRQ(S,N)  setSRQ ((S), N ## _flag);

#define PRL(S)          ((dev_prl[(S)/32] >> INT_V (S)) & 1)
#define IRQ(S)          ((dev_irq[(S)/32] >> INT_V (S)) & 1)
#define SRQ(S)          ((dev_srq[(S)/32] >> INT_V (S)) & 1)

#define IOT_V_REASON    16
#define IORETURN(F,V)   ((F) ? (V) : SCPE_OK)           /* stop on I/O error */

/* CPU state */

extern uint32 SR;                                       /* S register (for IBL) */
extern uint32 dev_prl [2], dev_irq [2], dev_srq [2];    /* I/O signal vectors */

/* Simulator state */

extern FILE *sim_deb;
extern FILE *sim_log;
extern int32 sim_step;
extern int32 sim_switches;

/* CPU functions */

extern t_stat ibl_copy       (const BOOT_ROM rom, int32 dev);
extern void   hp_enbdis_pair (DEVICE *ccp, DEVICE *dcp);

/* System functions */

extern t_stat      fprint_sym (FILE *ofile, t_addr addr, t_value *val, UNIT *uptr, int32 sw);
extern const char *fmt_char   (uint8 ch);
extern t_stat      hp_setdev  (UNIT *uptr, int32 val, char *cptr, void *desc);
extern t_stat      hp_showdev (FILE *st, UNIT *uptr, int32 val, void *desc);

/* Standard device functions */

extern int32 sync_poll (POLLMODE poll_mode);

#endif
