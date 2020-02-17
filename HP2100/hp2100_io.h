/* hp2100_io.h: HP 2100 device-to-CPU interface declarations

   Copyright (c) 2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   04-Sep-18    JDB     Added card_description and rom_description to the DIB
   10-Jul-18    JDB     Created


   This file contains declarations used by I/O devices to interface with
   the HP 21xx/1000 CPU.  It is required by any module that uses Device
   Information Blocks (DIBs), i.e., is addressed by an I/O select code.
*/



/* I/O devices - fixed select code assignments */

#define CPU                 000                 /* interrupt control */
#define OVF                 001                 /* overflow */
#define DMALT1              002                 /* DMA 1 alternate */
#define DMALT2              003                 /* DMA 2 alternate */
#define PWR                 004                 /* power fail */
#define MPPE                005                 /* memory protect/parity error */
#define DMA1                006                 /* DMA channel 1 */
#define DMA2                007                 /* DMA channel 2 */

/* I/O devices - variable select code assignment defaults */

#define PTR                 010                 /* 12597A-002 paper tape reader */
#define TTY                 011                 /* 12531C teleprinter */
#define PTP                 012                 /* 12597A-005 paper tape punch */
#define TBG                 013                 /* 12539C time-base generator */
#define LPS                 014                 /* 12653A line printer */
#define LPT                 015                 /* 12845A line printer */

#define MTD                 020                 /* 12559A data */
#define MTC                 021                 /* 12559A control */
#define DPD                 022                 /* 12557A data */
#define DPC                 023                 /* 12557A control */
#define DQD                 024                 /* 12565A data */
#define DQC                 025                 /* 12565A control */
#define DRD                 026                 /* 12610A data */
#define DRC                 027                 /* 12610A control */
#define MSD                 030                 /* 13181A data */
#define MSC                 031                 /* 13181A control */
#define IPLI                032                 /* 12566B link in */
#define IPLO                033                 /* 12566B link out */
#define DS                  034                 /* 13037A control */
#define BACI                035                 /* 12966A Buffered Async Comm Interface */
#define MPX                 036                 /* 12792A/B/C 8-channel multiplexer */
#define PIF                 037                 /* 12620A/12936A Privileged Interrupt Fence */
#define MUXL                040                 /* 12920A lower data */
#define MUXU                041                 /* 12920A upper data */
#define MUXC                042                 /* 12920A control */
#define DI_DA               043                 /* 12821A Disc Interface with Amigo disc devices */
#define DI_DC               044                 /* 12821A Disc Interface with CS/80 disc and tape devices */
#define MC1                 045                 /* 12566B Microcircuit Interface */
#define MC2                 046                 /* 12566B Microcircuit Interface */


#define SC_OPT              002                 /* start of optional devices */
#define SC_CRS              006                 /* start of devices that receive CRS */
#define SC_VAR              010                 /* start of variable assignments */

#define SC_MAX              077                 /* the maximum select code */
#define SC_MASK             077u                /* the mask for the select code */
#define SC_BASE             8                   /* the radix for the select code */


/* I/O backplane signals.

   The INBOUND_SIGNAL and OUTBOUND_SIGNAL declarations mirror the hardware
   signals that are received and asserted, respectively, by the interfaces on
   the I/O backplane.  A set of one or more signals forms an INBOUND_SET or
   OUTBOUND_SET that is sent to or returned from a device interface.  Under
   simulation, the CPU and DMA dispatch one INBOUND_SET to the target device
   interface per I/O cycle.  The interface returns an OUTBOUND_SET and a
   data value combined into a SIGNALS_VALUE structure to the caller.

   In addition, some signals must be asserted asynchronously, e.g., in response
   to an event service call.  The "io_assert" function, provided by the CPU
   module, provides asynchronous assertion for the ENF, SIR, PON, POPIO,
   CRS, and IAK signals.

   In hardware, signals are assigned to one or more specific I/O T-periods, and
   some signals are asserted concurrently.  For example, a programmed STC sc,C
   instruction asserts the STC and CLF signals together in period T4.  Under
   simulation, signals are ORed to form an I/O cycle; in this example, the
   signal handler would receive an INBOUND_SET value of "ioSTC | ioCLF".

   Hardware allows parallel action for concurrent signals.  Under simulation, a
   "concurrent" set of signals is processed sequentially by the signal handler
   in order of ascending numerical value.  Although assigned T-periods differ
   between programmed I/O and DMA I/O cycles, a single processing order is used.
   The order of execution generally follows the order of T-period assertion,
   except that SIR is processed after all other signals that may affect the
   interrupt request chain.


   Implementation notes:

    1. The enumerations describe signals.  A set of signals normally would be
       modeled as an unsigned integer, as a set may contain more than one
       signal.  However, we define a set as the enumeration, as the "gdb"
       debugger has special provisions for an enumeration of discrete bit values
       and will display the set in "ORed" form.

    2. The null signal ("ioNONE") cannot be defined as an enumeration constant
       because the C language has a single name space for all enumeration
       constants, so separate "no inbound signals" and "no outbound signals"
       identifiers would be required, and because including them would require
       handlers for them in "switch" statements, which is undesirable.
       Therefore, we define "ioNONE" as an explicit integer 0, so that it is
       compatible with both enumerations.

    3. The ioCLF signal must be processed after ioSFS/ioSFC to ensure that a
       true skip test generates ioSKF before the flag is cleared, and after
       ioIOI/ioIOO/ioSTC/ioCLC to meet the requirement that executing an
       instruction having the H/C bit set is equivalent to executing the same
       instruction with the H/C bit clear and then a CLF instruction.

    4. The ioENF signal must be processed after ioSTF and ioPOPIO to ensure that
       the flag buffer flip-flop has been set before it is used to set the flag
       flip-flop.

    5. The ioPRH signal must be processed after ioIEN.  The latter sets an
       internal "interrupts are enabled" flag that must be tested before
       interrupts are asserted by PRH.

    6. An I/O handler will receive ioCRS as a result of a CLC 0 instruction,
       ioPOPIO and ioCRS as a result of a RESET command, and ioPON, ioPOPIO, and
       ioCRS as a result of a RESET -P command.

    7. An I/O handler will receive ioNONE when a HLT instruction is executed
       that has the H/C bit clear (i.e., no CLF generated).

    8. In hardware, the SIR signal is generated unconditionally every T5 period
       to time the setting of the IRQ flip-flop.  Under simulation, ioSIR
       indicates that the I/O handler must set the PRL, IRQ, and SRQ signals as
       required by the interface logic.  ioSIR will be included in the I/O cycle
       if any of the flip-flops affecting these signals are changed and the
       interface supports interrupts or DMA transfers.

    9. In hardware, the ENF signal is unconditionally generated every T2 period
       to time the setting of the flag flip-flop and to reset the IRQ flip-flop.
       If the flag buffer flip-flip is set, then flag will be set by ENF.  If
       the flag buffer is clear, ENF will not affect flag.  Under simulation,
       ioENF is sent to set the flag flip-flop after the flag buffer flip-flop
       is set explicitly.

   10. In hardware, the PON signal is asserted continuously while the CPU is
       operating.  Under simulation, ioPON is asserted only at simulator
       initialization or when processing a RESET -P command.

   11. To avoid polling each interface when IEN or PRH asserts from a prior
       denied condition, interfaces return two conditional signals: cnIRQ and
       cnPRL.  These are identical in meaning to the ioIRQ and ioPRL signals,
       except their assertions do not depend on IEN or PRH.  To identify an
       interrupting interface rapidly, the CPU keeps these conditional signals
       in the "interrupt_request_set" and "priority_holdoff_set" arrays.  These
       bit vectors are examined when a higher-priority interface reasserts PRL
       or when the interrupt system is reenabled.

   12. An interface will assert cnVALID if the conditional PRL and IRQ were
       determined.  If cnVALID is not asserted by the interface, then the states
       of the cnPRL and cnIRQ signals cannot be inferred from their presence or
       absence in the oubound signal set.  The cnVALID pseudo-signal is required
       because although most interfaces determine the PRL and IRQ states in
       response to an SIR assertion, not all do.  In particular, the 12936A
       Privileged Interrupt Fence determines PRL in response to an IOO signal.
*/

typedef enum {                                  /* I/O T-Period    Description                   */
                                                /* ==============  ============================= */
    ioPON   = 000000000001,                     /* T2 T3 T4 T5 T6  Power On Normal */
    ioIOI   = 000000000002,                     /* -- -- T4 T5 --  I/O Data Input (CPU) */
                                                /* T2 T3 -- -- --  I/O Data Input (DMA) */
    ioIOO   = 000000000004,                     /* -- T3 T4 -- --  I/O Data Output */
    ioSFS   = 000000000010,                     /* -- T3 T4 T5 --  Skip if Flag is Set */
    ioSFC   = 000000000020,                     /* -- T3 T4 T5 --  Skip if Flag is Clear */
    ioSTC   = 000000000040,                     /* -- -- T4 -- --  Set Control flip-flop (CPU) */
                                                /* -- T3 -- -- --  Set Control flip-flop (DMA) */
    ioCLC   = 000000000100,                     /* -- -- T4 -- --  Clear Control flip-flop (CPU) */
                                                /* -- T3 T4 -- --  Clear Control flip-flop (DMA) */
    ioSTF   = 000000000200,                     /* -- T3 -- -- --  Set Flag flip-flop */
    ioCLF   = 000000000400,                     /* -- -- T4 -- --  Clear Flag flip-flop (CPU) */
                                                /* -- T3 -- -- --  Clear Flag flip-flop (DMA) */
    ioEDT   = 000000001000,                     /* -- -- T4 -- --  End Data Transfer */
    ioCRS   = 000000002000,                     /* -- -- -- T5 --  Control Reset */
    ioPOPIO = 000000004000,                     /* -- -- -- T5 --  Power-On Preset to I/O */
    ioIAK   = 000000010000,                     /* -- -- -- -- T6  Interrupt Acknowledge */

    ioENF   = 000000020000,                     /* T2 -- -- -- --  Enable Flag */
    ioSIR   = 000000040000,                     /* -- -- -- T5 --  Set Interrupt Request */
    ioIEN   = 000000100000,                     /* T2 T3 T4 T5 T6  Interrupt system Enable */
    ioPRH   = 000000200000                      /* T2 T3 T4 T5 T6  Priority High */
    } INBOUND_SIGNAL;

typedef INBOUND_SIGNAL      INBOUND_SET;        /* a set of INBOUND_SIGNALs */


typedef enum {                                  /* I/O T-Period    Description       */
                                                /* ==============  ================= */
    ioSKF   = 000000000001,                     /* -- T3 T4 T5 --  skip on flag */
    ioPRL   = 000000000002,                     /* T2 T3 T4 T5 T6  priority low */
    ioFLG   = 000000000004,                     /* -- -- T4 T5 --  flag */
    ioIRQ   = 000000000010,                     /* -- -- T4 T5 --  interrupt request */
    ioSRQ   = 000000000020,                     /* T2 T3 T4 T5 T6  service request */

    cnIRQ   = 000000000040,                     /* conditional interrupt request */
    cnPRL   = 000000000100,                     /* conditional priority low */
    cnVALID = 000000000200                      /* conditional signals are valid */
    } OUTBOUND_SIGNAL;

typedef OUTBOUND_SIGNAL     OUTBOUND_SET;       /* a set of OUTBOUND_SIGNALs */


#define ioNONE              0                   /* a universal "no signals are asserted" value */


typedef struct {                                /* the I/O interface return structure */
    OUTBOUND_SET  signals;                      /*   the outbound signal set */
    HP_WORD       value;                        /*   the outbound value */
    } SIGNALS_VALUE;


/* I/O backplane signal assertions */

typedef enum {                                  /* assertions passed to the "io_assert" call */
    ioa_ENF,                                    /*   Enable Flag */
    ioa_SIR,                                    /*   Set Interrupt Request */
    ioa_PON,                                    /*   Power On Normal */
    ioa_POPIO,                                  /*   Power-On Preset to I/O */
    ioa_CRS,                                    /*   Control Reset */
    ioa_IAK                                     /*   Interrupt Acknowledge */
    } IO_ASSERTION;


/* I/O macros.

   The following macros are useful in device interface signal handlers and unit
   service routines.  The parameter definition symbols employed are:

     P = a priority set value
     S = an INBOUND_SET or OUTBOUND_SET value
     L = an INBOUND_SIGNAL value

   A priority set is an unsigned value, where each bit represents an assertion
   of some nature (e.g., I/O signals, interrupt requests, etc.), and the
   position of the bit represents its priority, which decreases from LSB to MSB.
   The IOPRIORITY macro isolates the highest-priority bit from the set.  It does
   this by ANDing the value with its two's complement; only the lowest-order bit
   will differ.  For example (bits are numbered here from the LSB):

     priority set :  ...0 0 1 1 0 1 0 0 0 0 0 0  (bits 6, 8, and 9 are asserted)
     one's compl  :  ...1 1 0 0 1 0 1 1 1 1 1 1
     two's compl  :  ...1 1 0 0 1 1 0 0 0 0 0 0
     ANDed value  :  ...0 0 0 0 0 1 0 0 0 0 0 0  (bit 6 is highest priority)

   If the request set indicates requests by 0 bits, rather than 1 bits, the
   IOPRIORITY macro must be called with the one's complement of the bits.

   The IONEXTSIG macro isolates the next inbound signal in sequence to process
   from the inbound signal set S.

   The IOCLEARSIG macro removes the processed signal L from the inbound signal
   set S.


   Implementation notes:

    1. The IOPRIORITY macro implements two's complement explicitly, rather than
       using a signed negation, to be compatible with machines using a
       sign-magnitude integer format.  "gcc" and "clang" optimize the complement
       and add to a single NEG instruction on x86 machines.
*/

#define IOPRIORITY(P)       ((P) & ~(P) + 1)

#define IONEXTSIG(S)        ((INBOUND_SIGNAL) IOPRIORITY (S))
#define IOCLEARSIG(S,L)     S = (INBOUND_SIGNAL) ((S) ^ (L))


/* I/O structures.

   The Device Information Block (DIB) allows devices to be relocated in the
   machine's I/O space.  Each DIB contains a pointer to the device interface
   routine, a value corresponding to the location of the interface card in the
   CPU's I/O card cage (which determines the card's select code), a card index
   that is non-zero if the interface routine services multiple cards, a pointer
   to a string that describes the card, and an optional pointer to a string
   that describes the 1000-series boot loader ROM that boots from the device
   associated with the interface card.  The card description is printed by the
   SHOW CPU IOCAGE command, while the ROM description is printed by the SHOW CPU
   ROMS command.  All devices should have card descriptions, but only those
   devices having boot loader ROMs will have ROM descriptions.  The latter will
   be NULL if no there is no associated ROM.


   Implementation notes:

    1. The select_code and card_index fields could be smaller than the defined
       32-bit sizes, but IA-32 processors execute instructions with 32-bit
       operands much faster than those with 16- or 8-bit operands.

    2. The DIB_REGS macro provides hidden register entries needed to save and
       restore the state of a DIB.  Only the potentially variable fields are
       referenced.  In particular, the "io_interface" field must not be saved,
       as the address of the device's interface routine may change from version
       to version of the simulator.

    3. The Windows SDK defines an INTERFACE macro.  Because "sim_defs.h"
       includes "windows.h", a name clash occurs, even though we never use any
       Windows features.  So we undefine INTERFACE here to repurpose it for our
       own use.
*/

#undef INTERFACE                                /* remove any previous definition */

typedef struct dib DIB;                         /* an incomplete definition */

typedef SIGNALS_VALUE INTERFACE                 /* the I/O device interface function prototype */
    (const DIB    *dibptr,                      /*   a pointer to the constant device information block */
     INBOUND_SET  inbound_signals,              /*   a set of inbound signals */
     HP_WORD      inbound_value);               /*   a 16-bit inbound value */

struct dib {                                    /* the Device Information Block */
    INTERFACE   *io_interface;                  /*   the controller I/O interface function pointer */
    uint32      select_code;                    /*   the device's select code (02-77) */
    uint32      card_index;                     /*   the card index if multiple interfaces are supported */
    const char  *card_description;              /*   the card description (model number and name) */
    const char  *rom_description;               /*   the 1000 boot loader ROM description (model number and name) */
    };


#define DIB_REGS(dib) \
/*    Macro   Name     Location                    Width  Flags              */ \
/*    ------  -------  --------------------------  -----  -----------------  */ \
    { ORDATA (DIBSC,   dib.select_code,             32),  PV_LEFT | REG_HRO }


/* Initial Binary Loader.

   HP 1000-series CPUs contain from one to four bootstrap loader ROMs that
   contain the 64-word initial binary loaders for the associated devices.  The
   loader program to use is selected by setting the S-register as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | -   - |      select code      | -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   ...and then pressing the front panel IBL button to copy the program into
   main memory for execution.

   Bits 15-14 select one of four loader ROM sockets on the CPU board that may
   contain ROMs, and bits 11-6 specify the select code of the associated
   interface card.  If the specified socket does, the contents of the ROM are
   copied into the upper 64 words of memory and configured to use the specified
   select code.  The unspecified bits of the S register are available for use by
   the bootstrap program.

   The 21xx CPUs store their initial binary loaders in the last 64 words of
   available memory.  This memory is protected by a LOADER ENABLE switch on the
   front panel.  When the switch is off (disabled), main memory effectively ends
   64 locations earlier, i.e., the loader area is treated as non-existent.
   Because these are core machines, the loader is retained when system power is
   off.

   In simulation, we define a BOOT_LOADER structure containing three indicies
   and a 64-word array that holds the bootstrap program.  The start_index gives
   the array index of the first program word to execute, the dma_index gives the
   array index of the DMA control word that specifies the interface select code,
   and the fwa_index gives the array index of the word containing the negative
   starting address of the loader in memory; this is used to ensure that the
   bootstrap does not overlay itself when reading from the device.

   A LOADER_ARRAY consists of two BOOT_LOADER structures: one for 21xx-series
   machines, and one for 1000-series machines.  If either BOOT_LOADER is not
   applicable, e.g., because the CPU series does not support booting from the
   given device, then the associated start_index is set to IBL_NA.  If the boot
   loader exists but does not use DMA and/or does not configure a starting
   address word, the associated indicies are set to IBL_NA.
*/

#define IBL_WIDTH           6                           /* loader ROM address width */
#define IBL_MASK            ((1u << IBL_WIDTH) - 1)     /* loader ROM address mask (2 ** 6 - 1) */
#define IBL_MAX             ((1u << IBL_WIDTH) - 1)     /* loader ROM address maximum (2 ** 6 - 1) */
#define IBL_SIZE            (IBL_MAX + 1)               /* loader ROM size in words */

#define IBL_START           0                           /* ROM array index of the program start */
#define IBL_DMA             (IBL_MAX - 1)               /* ROM array index of the DMA configuration word */
#define IBL_FWA             (IBL_MAX - 0)               /* ROM array index of the negative starting address */
#define IBL_NA              (IBL_MAX + 1)               /* "not-applicable" ROM array index */

#define IBL_S_CLEAR         0000000u                    /* cpu_copy_loader mask to clear the S register */
#define IBL_S_NOCLEAR       0177777u                    /* cpu_copy_loader mask to preserve the S register */
#define IBL_S_NOSET         0000000u                    /* cpu_copy_loader mask to preserve the S register */

#define IBL_ROM_MASK        0140000u                    /* ROM socket selector mask */
#define IBL_SC_MASK         0007700u                    /* device select code mask */
#define IBL_USER_MASK       ~(IBL_ROM_MASK | IBL_SC_MASK)

#define IBL_ROM_SHIFT       14
#define IBL_SC_SHIFT        6

#define IBL_ROM(s)          (((s) & IBL_ROM_MASK) >> IBL_ROM_SHIFT)
#define IBL_SC(s)           (((s) & IBL_SC_MASK)  >> IBL_SC_SHIFT)

#define IBL_TO_SC(c)        ((c) << IBL_SC_SHIFT & IBL_SC_MASK)


typedef struct {
    uint32       start_index;                   /* the array index of the start of the program */
    uint32       dma_index;                     /* the array index of the DMA configuration word */
    uint32       fwa_index;                     /* the array index of the negative starting address */
    MEMORY_WORD  loader [IBL_SIZE];             /* the 64-word bootstrap loader program */
    } BOOT_LOADER;

typedef BOOT_LOADER LOADER_ARRAY [2];           /* array (21xx, 1000) of bootstrap loaders */


/* CPU global utility routine declarations */

extern uint32 cpu_copy_loader (const LOADER_ARRAY boot, uint32 sc, HP_WORD sr_clear, HP_WORD sr_set);
extern t_bool cpu_io_stop     (UNIT *uptr);


/* I/O subsystem global utility routine declarations */

extern void io_assert (DEVICE *dptr, IO_ASSERTION assertion);


/* Main memory global utility routine declarations */

extern HP_WORD mem_examine  (uint32 address);
extern void    mem_deposit  (uint32 address, HP_WORD value);
