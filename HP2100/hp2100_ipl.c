/* hp2100_ipl.c: HP 12875A Processor Interconnect simulator

   Copyright (c) 2002-2016, Robert M. Supnik
   Copyright (c) 2017-2020, J. David Bryan

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
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   IPLI, IPLO   12875A Processor Interconnect

   14-Aug-20    JDB     Improved "wait_event" fallback logic
   26-Jul-20    JDB     Added CMD tracing for TSB SP/IOP commands and status
   27-Jun-20    JDB     Added SET INTERLOCK and instruction interlocking
   14-Feb-20    JDB     Added sys/stat.h include for FreeBSD
   09-Dec-19    JDB     Removed unneeded push/pop pragmas for Windows
   19-Nov-19    JDB     Imposed input polling order to fix Access printing hang
   18-Mar-19    JDB     Reordered SCP includes
   11-Jul-18    JDB     Revised I/O model
   22-May-18    JDB     Added process synchronization commands
   01-May-18    JDB     Removed ioCRS counter, as consecutive ioCRS calls are no longer made
   26-Mar-18    JDB     Converted from socket to shared memory connections
   28-Feb-18    JDB     Added the special IOP BBL
   13-Aug-17    JDB     Revised so that only IPLI boots
   19-Jul-17    JDB     Removed unused "ipl_stopioe" variable and register
   11-Jul-17    JDB     Renamed "ibl_copy" to "cpu_ibl"
   15-Mar-17    JDB     Trace flags are now global
                        Changed DEBUG_PRJ calls to tpprintfs
   10-Mar-17    JDB     Added IOBUS to the debug table
   27-Feb-17    JDB     ibl_copy no longer returns a status code
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   14-Sep-15    JDB     Exposed "ipl_edtdelay" via a REG_HIDDEN to allow user tuning
                        Corrected typos in comments and strings
   05-Jun-15    JDB     Merged 3.x and 4.x versions using conditionals
   11-Feb-15    MP      Revised ipl_detach and ipl_dscln for new sim_close_sock API
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   12-Dec-12    MP      Revised ipl_attach for new socket API
   25-Oct-12    JDB     Removed DEV_NET to allow restoration of listening ports
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added CARD_INDEX casts to dib.card_index
   07-Apr-11    JDB     A failed STC may now be retried
   28-Mar-11    JDB     Tidied up signal handling
   27-Mar-11    JDB     Consolidated reporting of consecutive CRS signals
   29-Oct-10    JDB     Revised for new multi-card paradigm
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   15-Jul-08    JDB     Revised EDT handler to refine completion delay conditions
   09-Jul-08    JDB     Revised ipl_boot to use ibl_copy
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   01-Mar-07    JDB     IPLI EDT delays DMA completion interrupt for TSB
                        Added debug printouts
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Oct-04    JDB     Fixed enable/disable from either device
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   21-Dec-03    RMS     Adjusted ipl_ptime for TSB (from Mike Gemeny)
   09-May-03    RMS     Added network device flag
   31-Jan-03    RMS     Links are full duplex (found by Mike Gemeny)

   References:
     - 12875A Processor Interconnect Kit Operating and Service Manual
         (12875-90002, January 1974)
     - 12566B[-001/2/3] Microcircuit Interface Kits Operating and Service Manual
         (12566-90015,  April 1976)

   The HP 12875A Processor Interconnect kit is used to communicate between the
   System Processor and the I/O Processor of a two-CPU HP 2000 Time-Shared BASIC
   system.  The kit consists of four identical 12566A Microcircuit Interfaces
   and two interconnecting cables.  One pair of interfaces is installed in
   adjacent I/O slots in each CPU, and the cables are used to connect the
   higher-priority (lower select code) interface in each computer to the
   lower-priority interface in the other computer.  This interconnection
   provides a full-duplex 16-bit parallel communication channel between the
   processors.  Each interface is actually a bi-directional, half-duplex line
   that is used in the primary direction for commands and in the reverse
   direction for status.

   Two instances of the HP2100 simulator are run to simulate the SP and IOP.
   Each simulator contains an Inbound Data interface assigned to the
   lower-numbered select code, and an Outbound Data interface assigned to the
   higher-numbered select code.  The IPLI and IPLO devices, respectively,
   simulate these interfaces, while the IPL device represents the combination.
   A shared memory area simulates the interconnecting cables.

   An essential aspect of TSB startup is that the IOP is running before the SP
   attempts to communicate with it.  If the IOP is not running or is otherwise
   non-responsive, the SP startup routine halts with an error message.  In
   hardware, this is accomplished by having the system operator start the IOP
   processor before the SP processor.  In simulation, however, starting the IOP
   instance before the SP instance does not guarantee that it will run
   uninterrupted.  In response to system load, the host operating system may
   block or preempt the IOP instance, resulting in a TSB startup failure.

   The IPL device provides two synchronization event mechanisms to ensure that
   system startup order is preserved, regardless of host system load.  The first
   provides simple WAIT and SIGNAL commands that may be placed in command files
   to cause one simulator instance to suspend until signaled by the other
   instance.  This cam ensure, for example, that one instance has loaded its
   communication program before the other instance begins to communicate.

   For finer-grained control, the second mechanism provides an instruction
   interlock.  This allows each instance to execute a given number of machine
   instructions before performing a rendezvous with the other instance.  With
   this mechanism, process preemption by the host does not allow one instance to
   get ahead of the other instance.

   Both mechanisms are implemented by host-platform events (semaphores) and may
   be used concurrently, if desired.  If events are unsupported, a fallback
   mechanism is employed that uses timed pauses instead of handshakes.  Without
   host system synchronization support, the simulated system's OS might work,
   but if a fallback was not available, then the simulator command files running
   on such systems would refuse to run.


   Implementation notes:

    1. The "IPL" ("InterProcessor Link") designation is used throughout this
       file for historical reasons, although HP designates this device as the
       Processor Interconnect Kit.
*/



#include <signal.h>

#include "sim_defs.h"
#include "sim_timer.h"
#include "sim_shmem.h"

#include "hp2100_defs.h"
#include "hp2100_io.h"



/* Process synchronization definitions */


/* Windows process synchronization */

#if defined (_WIN32) && ! defined (USE_FALLBACK)

#include <windows.h>

typedef HANDLE              EVENT;              /* the event type */

#define UNDEFINED_EVENT     NULL                /* the initial (undefined) event value */


/* UNIX process synchronization */

#elif defined (HAVE_SEMAPHORE) && ! defined (USE_FALLBACK)

#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

typedef sem_t               *EVENT;             /* the event type */

#define UNDEFINED_EVENT     SEM_FAILED          /* the initial (undefined) event value */
#define INFINITE            2000000             /* an "infinite" timeout period (in msec, about 33 minutes) */


/* Process synchronization stub */

#else

typedef uint32              EVENT;              /* the event type */

#define UNDEFINED_EVENT     0                   /* the initial (undefined) event value */
#define INFINITE            2000000             /* an "infinite" timeout period (in msec, about 33 minutes) */


#endif



/* Program constants */

#define CARD_COUNT          2                   /* count of cards supported */
#define DATA_MASK           0177u               /* characters use only 7 bits for data */


/* ATTACH mode switches */

#define SP                  SWMASK ('S')        /* SP switch */
#define IOP                 SWMASK ('I')        /* IOP switch */

#define LISTEN              SWMASK ('L')        /* listen switch (deprecated) */
#define CONNECT             SWMASK ('C')        /* connect switch (deprecated) */


/* Per-unit state variables */

#define ID                  u3                  /* session identifying number */


/* Unit flags */

#define UNIT_DIAG_SHIFT     (UNIT_V_UF + 0)     /* diagnostic mode */

#define UNIT_DIAG           (1u << UNIT_DIAG_SHIFT)


/* Unit references */

typedef enum {
    ipli,                                       /* inbound card index */
    iplo                                        /* outbound card index */
    } CARD_INDEX;

#define poll_unit           ipl_unit [ipli]     /* inbound card unit (poll unit) */
#define sync_unit           ipl_unit [iplo]     /* outbound card unit (synchronization unit) */


/* Device information block references */

#define ipli_dib            ipl_dib [ipli]      /* inbound card DIB */
#define iplo_dib            ipl_dib [iplo]      /* outbound card DIB */


/* Command accessors.

   Commands are issued from the SP to the IOP to inform the latter of changes in
   the operating system state and to request terminal services.  In some cases,
   the IOP responds with status to indicate whether or not the command was
   successful.  In a few cases, the IOP responds with a block of data that is
   transferred via DMA.  The IOP can send a few commands of its own to the SP
   that reflect availability of terminal data.

   Commands are sent on the outbound side of the output channel, and status is
   received on the inbound side.  Commands are received on the inbound side of
   the input channel, and status is returned on the outbound side.

   System Processor commands are encoded in 16-bit words, with an opcode
   designating the command in bits 15-13 and additional information in the
   remaining bits, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  opcode   | -   -   -   -   - |       unsigned integer        |  form 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  opcode   |    port number    |        ASCII character        |  form 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  opcode   |    port number    |       unsigned integer        |  form 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  opcode   |     device number     |     unsigned integer      |  form 4
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Opcode 7 (and, additionally for 2000 Access, opcode 6) uses an additional
   five bits as a subopcode to determine the command, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   x | -   -   -   -   -   -   -   - |     subopcode     |  form 5
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   x |    port number    | -   -   - |     subopcode     |  form 6
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   x |     device number     | -   - |     subopcode     |  form 7
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   x |      word count       | -   - |     subopcode     |  form 8
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Finally, opcode 7 subopcode 0 uses three extra bits to extend command
   decoding to a third level, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1 | -   -   - | extension | -   - | 0   0   0   0   0 |  form 9
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The commands differ from TSB version to version and are not proper subsets:

     Op Sub Ext  2000 Access Command            2000F Command                  2000B/C Command
     -- --- ---  -------------------------      -----------------------------  -----------------------------
      0  -       POC Process output character   OCR Process output character   OCR Process output character
      1  -       STE Start ENTER timing         STE Start ENTER timing         STE Start ENTER timing
      2  -       STP Subtype information        GTC Fetch next character       GTC Fetch next character
      3  -       PHS Phones timing parameter    PHO Phones timing              PHO Phones timing
      4  -       PCF Perform control function   SPE Baud rate info             SPE Baud rate info
      5  -       POS Process output string      SBP Save buffer pointer        SBP Save buffer pointer
      6  00      SBL Send buffer length         RBP Restore buffer pointer     RBP Restore buffer pointer
      6  01      WTP What terminal type         --                             --
      7  00  0   INI Initialize IOP             INI Initialize IOP             INI Initialize IOP
      7  00  1   KSN Cold dump request          --                             --
      7  00  2   SNP Send number of ports       --                             --
      7  00  3   SDT Send device table          --                             --
      7  00  4   SSD System shut down           --                             --
      7  00  5   SDC Send date code             --                             --
      7  00  6   --- (unused)                   --                             --
      7  00  7   --- (unused)                   --                             --

      7  01      UIR User is running            UIR User is running            UIR User is running
      7  02      UNR User not running           UNR User not running           UNR User not running
      7  03      IWT Input wait                 IWT Input wait                 IWT Input wait
      7  04      HUU Hang user up               HUU Hang user up               HUU Hang user up
      7  05      ULO User logged on             ULO User logged on             ULO User logged on
      7  06      ECO Echo-on                    ECO Echo-on                    ECO Echo-on
      7  07      ECF Echo-off                   ECF Echo-off                   ECF Echo-off
      7  10      TPO Tape mode                  TPO Tape mode on               TPO Tape mode on
      7  11      STR Start timed retries        ILI Illegal input              ILI Illegal input
      7  12      NUC New user called            NUC New user called            NUC New user called
      7  13      KTO Kill terminal output       KTO Kill terminal output       KTO Kill terminal output
      7  14      ALI Allow input                ALI Allow input                ALI Allow input
      7  15      OWT Output wait                OWT Output wait                OWT Output wait
      7  16      IBA Is buffer available        IBF Is buffer full             IBF Is buffer full
      7  17      ADV Allocate device            PSC Line printer select code   PSC Line printer select code
      7  20      RDV Release device             LPR Line printer request       LPR Line printer request
      7  21      ALB Allocate buffer            LPD Line printer disconnect    LPD Line printer disconnect
      7  22      XRB Transfer input buffer      LPS Line printer status        LPS Line printer status
      7  23      BKS Backspace terminal buffer  BKS Backspace terminal buffer  BKS Backspace terminal buffer
      7  24      KDO Kill device output         CHS Character size             CHS Character size
      7  25      FNC Fetch next character       STP Subtype info               STP Subtype info
      7  26      RJE RJE command                GRP Get receive parameter      WSP What baud rate
      7  27      ABT User is being aborted      ABT User is being aborted      WCS What character size
      7  30      PIS Process input string       WTP What terminal type         WTP What terminal type
      7  31      --- (unused)                   KSN Send core image            TKO Dump to line printer
      7  32      SCI Send core image            --- (unused)                   ABT User is being aborted
      7  33      RLB Release buffer             --- (unused)                   --- (unused)
      7  34      SSD System shutdown            --- (unused)                   --- (unused)
      7  35      SBP Save buffer pointer        --- (unused)                   --- (unused)
      7  36      RBP Restore buffer pointer     --- (unused)                   --- (unused)
      7  37      TCM Transmit console message   --- (unused)                   --- (unused)

When the IOP returns a status word, it is encoded as follows:

     Response  Meaning
     --------  ------------------------
        -3     No data available on RJE
        -2     End of file
        -1     Buffer not ready
         0     Operation successful
         1     Device not ready
         2     Device error
         3     Attention needed
         4     Read/write failure



   I/O  Processor commands are encoded in 16-bit words, with an opcode
   designating the command in bits 15-13 and additional information in the
   remaining bits, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  opcode   |    port number    |       unsigned integer        |  form 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Only 2000 Access uses opcode 7 as a subopcode indicator, with these forms:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1 | -   -   -   -   -   -   -   -   - |   subopcode   |  form 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1 |    port number    | -   -   -   - |   subopcode   |  form 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1 |     device number     | -   -   - |   subopcode   |  form 4
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1 |      word count       | -   -   - |   subopcode   |  form 5
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The commands differ from TSB version to version and are not proper subsets:

     Op  Sub  2000 Access Command               2000F Command        2000B/C Command
     --  ---  --------------------------------  -------------------  -------------------
      0   -   HVL Have a line                   HVL Have line        HVL Have line
      1   -   HVP Have a line - parity error    ABR User aborted     ABR User aborted
      2   -   HLL Have a line - lost character  BFL Buffer full      BFL Buffer full
      3   -   --- (unused)                      BFE Buffer empty     BFE Buffer empty
      4   -   --- (unused)                      ETO ENTER timed out  ETO ENTER timed out
      5   -   --- (unused)                      UHU User hung up     UHU User hung up
      6   -   --- (unused)                      --- (unused)         --- (unused)
      7   00  ABR User abort request            --- (unused)         --- (unused)
      7   01  BFL Buffer full                   --- (unused)         --- (unused)
      7   02  BFE Buffer empty                  --- (unused)         --- (unused)
      7   03  ETO ENTER timed out               --- (unused)         --- (unused)
      7   04  UHU User hung up                  --- (unused)         --- (unused)
      7   05  SCM Send console message          --- (unused)         --- (unused)
      7   06  ADR Allocate device for RJE       --- (unused)         --- (unused)
      7   07  RDR Release device from RJE       --- (unused)         --- (unused)
      7   10  WUU Wake user up                  --- (unused)         --- (unused)
      7   11  WRU Wake RJE up                   --- (unused)         --- (unused)
      7   12  --- (unused)                      --- (unused)         --- (unused)
      7   13  --- (unused)                      --- (unused)         --- (unused)
      7   14  --- (unused)                      --- (unused)         --- (unused)
      7   15  --- (unused)                      --- (unused)         --- (unused)
      7   16  --- (unused)                      --- (unused)         --- (unused)
      7   17  --- (unused)                      --- (unused)         --- (unused)
*/

#define CM_OPCODE_MASK      0160000u            /* operation code mask */
#define CM_EXTOP_MASK       0001600u            /* extended opcode mask */
#define CM_SUBOP_MASK       0000037u            /* subopcode mask */

#define CM_OPCODE_SHIFT     13
#define CM_EXTOP_SHIFT      7
#define CM_SUBOP_SHIFT      0

#define CM_OPCODE(c)        (((c) & CM_OPCODE_MASK) >> CM_OPCODE_SHIFT)
#define CM_EXTOP(c)         (((c) & CM_EXTOP_MASK)  >> CM_EXTOP_SHIFT)
#define CM_SUBOP(c)         (((c) & CM_SUBOP_MASK)  >> CM_SUBOP_SHIFT)


/* Command decoding.

   Some SP commands receive status or data back from the IOP in response, though
   most do not.  Additionally, some commands supply parameters or data to the
   IOP.  To decode and trace those commands successfully, the state of the
   command exchange must be tracked.  After sending the command opcode, there
   are 12 different exchanges that follow, as enumerated below:

      1. None
      2. Character received
      3. Binary data received
      4. DMA packed characters received
      5. DMA binary data received
      6. Status received
      7. Status received + DMA packed characters sent
      8. Status received + Binary data received + DMA packed characters received
      9. Binary data received + DMA packed characters received
     10. Binary data sent + DMA binary data received
     11. Binary data sent + Status received
     12. Binary data sent + Status received + DMA packed characters sent

   States are defined that encode each of possible exchanges.  For trace message
   clarity, separate states are provided for received decimal and octal binary
   data.  The trace routines set the next state from the entry state, as
   follows:

     Entry State     Next State     Response
     -------------   ------------   --------
     None            None               1
     Character       None               2
     Decimal         None               3
     Octal           None               3
     DMA_Chars       DMA_Chars          4
     DMA_Octal       DMA_Octal          5
     Status          None               6
     Status_DMAC     DMA_Chars          7
     Stat_Dec_DMAC   Decimal_DMAC       8
     Decimal_DMAC    DMA_Chars          9
     Octal_DMAB      DMA_Octal         10
     Dec_Status      Status            11
     Dec_Stat_DMAC   Status_DMAC       12

   All of the IOP-to-SP commands are opcode-only, except for the "Allocate
   device for RJE" command, which returns a status word.
*/

typedef enum {                                  /* response states */
    None,                                       /*   None */
    Character,                                  /*   Character in */
    Decimal,                                    /*   Decimal data in */
    Octal,                                      /*   Octal data in */
    Status,                                     /*   DMA packed characters in */
    DMA_Chars,                                  /*   DMA binary data in */
    DMA_Octal,                                  /*   Status in */
    Status_DMAC,                                /*   Status in + DMA packed characters out */
    Stat_Dec_DMAC,                              /*   Status in + Binary data in + DMA packed characters in */
    Decimal_DMAC,                               /*   Decimal data in + DMA packed characters in */
    Octal_DMAB,                                 /*   Octal data out + DMA binary data in */
    Dec_Status,                                 /*   Decimal data out + Status in */
    Dec_Stat_DMAC                               /*   Decimal data out + Status in + DMA packed characters out */
    } RESPONSE;


/* Command descriptor table.

   Command decoding is driven by a table whose entries describe the command
   format.  As mentioned above, command words contain an opcode field, an
   optional port or device number field, and an optional subopcode field.  Table
   entries describe how to separate and label the fields, as well as what
   responses are expected.

   A mask field describes how to isolate the first operand (port or device
   number).  By implication, if there is a second operand, it fills the
   remaining bits of the word.  The presence of the operands are indicated by
   the presence of operand labels -- an empty string indicates that the
   corresponding operand is not present.  An entry with a NULL command name is
   not assigned.

   The table is organized into several sections.  The first and largest section
   contains entries for the 2000 Access SP commands.  Entries 0-6 correspond
   directly to opcodes 0-6.  Entry 7 corresponds to opcode 7 and is unused.
   Entries 10-47 (octal) correspond to opcode 7, subopcodes 0-37 (octal).

   The second section contains entries for the 2000 Access opcode 7 subopcode 0
   extensions.  Entries 50-57 (octal) correspond to extension codes 0-7.

   Section three corresponds to the 2000 Access IOP commands.  Entries 60-62
   (octal) correspond to opcodes 0-2.  Entries 63-67 are unused, and entries
   70-101 (octal) correspond to opcode 7 subopcodes 0-11 (octal).

   The SP commands unique to 2000F appear in section four.  They do not
   correspond directly to any opcode/subopcode pattern but instead are
   referenced by the 2000F remapping table.  Consequently, the order is
   irrelevant.

   As the unique 2000F IOP commands are fully implemented in 2000 Access but
   with different code points, a section five is not needed.  Remapping the
   2000F IOP codes to their counterparts in the Access code space is sufficient.


   Implementation notes:

    1. Access defines two SP opcodes as subopcode groups.  Opcode 7 introduces a
       subopcode group in both Access and F, while opcode 6 is a subopcode group
       only in Access.  However, the group has only two members, and one of them
       (opcode 6 subopcode 0, "Send buffer length") isn't actually used by the
       SP.  So Entry 6 in the table corresponds to opcode 6 subopcode 1.
*/

#define SUBOP_OPCODE    007                     /* opcode for subopcode commands */
#define SUBOP_OFFSET    010                     /* table index offset of subopcode commands */
#define EXTOP_OFFSET    040                     /* table index offset of extended opcode commands */
#define IOP_OFFSET      060                     /* table index offset of IOP commands */

typedef struct {                                /* command descriptor */
    RESPONSE response;                          /*   response format */
    uint32   mask;                              /*   operand mask */
    char     *high_label;                       /*   first operand label */
    char     *low_label;                        /*   second operand label */
    char     *name;                             /*   command name */
    } DESCRIPTOR;

static DESCRIPTOR cmd [] = {
/*    Response       Mask    High Label  Low Label      Command Name                   Index + [sub]opcode */
/*    -------------  ------  ----------  -------------  ---------------------------    ------------------- */

/* 2000 Access SP primary entries */

    { None,          017400, " port ",   " character ", "Process output character"  }, /* 000 + 00 */
    { None,          017400, " port ",   " seconds ",   "Start ENTER timing"        }, /* 000 + 01 */
    { None,          017400, " port ",   " type code ", "Subtype information"       }, /* 000 + 02 */
    { None,          000000, "",         " seconds ",   "Phones timing"             }, /* 000 + 03 */
    { Status,        017600, " device ", " control ",   "Perform control function"  }, /* 000 + 04 */
    { Status_DMAC,   017400, " port ",   " count ",     "Process output string"     }, /* 000 + 05 */
    { Decimal,       017400, " port ",   "",            "What terminal type"        }, /* 000 + 06 */
    { None,          000000, "",         "",            NULL                        }, /* 000 + 07 */

/* 2000 Access SP secondary entries */

    { None,          017400, " count ",  "",            "Initialize IOP"            }, /* 010 + 00 */
    { None,          017400, " port ",   "",            "User is running"           }, /* 010 + 01 */
    { None,          017400, " port ",   "",            "User not running"          }, /* 010 + 02 */
    { None,          017400, " port ",   "",            "Input wait"                }, /* 010 + 03 */
    { None,          017400, " port ",   "",            "Hang user up"              }, /* 010 + 04 */
    { None,          017400, " port ",   "",            "User logged on"            }, /* 010 + 05 */
    { None,          017400, " port ",   "",            "Echo on"                   }, /* 010 + 06 */
    { None,          017400, " port ",   "",            "Echo off"                  }, /* 010 + 07 */
    { None,          017400, " port ",   "",            "Tape mode on"              }, /* 010 + 10 */
    { None,          017600, " device ", "",            "Start timed retries"       }, /* 010 + 11 */
    { None,          017400, " port ",   "",            "New user called"           }, /* 010 + 12 */
    { None,          017400, " port ",   "",            "Kill terminal output"      }, /* 010 + 13 */
    { None,          017400, " port ",   "",            "Allow input"               }, /* 010 + 14 */
    { None,          017400, " port ",   "",            "Output wait"               }, /* 010 + 15 */
    { Status,        017400, " port ",   "",            "Is buffer available"       }, /* 010 + 16 */
    { Dec_Status,    017600, " device ", "",            "Allocate device"           }, /* 010 + 17 */
    { Status,        017600, " device ", "",            "Release device"            }, /* 010 + 20 */
    { Dec_Stat_DMAC, 017600, " device ", "",            "Allocate buffer"           }, /* 010 + 21 */
    { Stat_Dec_DMAC, 017600, " device ", "",            "Transfer input buffer"     }, /* 010 + 22 */
    { None,          017400, " port ",   "",            "Backspace terminal buffer" }, /* 010 + 23 */
    { None,          017600, " device ", "",            "Kill device output"        }, /* 010 + 24 */
    { Character,     017400, " port ",   "",            "Fetch next character"      }, /* 010 + 25 */
    { Status_DMAC,   017600, " count ",  "",            "RJE command"               }, /* 010 + 26 */
    { None,          017400, " port ",   "",            "User is being aborted"     }, /* 010 + 27 */
    { Dec_Stat_DMAC, 017400, " port ",   "",            "Process input string"      }, /* 010 + 30 */
    { None,          000000, "",         "",            NULL                        }, /* 010 + 31 */
    { Octal_DMAB,    017600, " count ",  "",            "Send core image"           }, /* 010 + 32 */
    { None,          017400, " port ",   "",            "Release buffer"            }, /* 010 + 33 */
    { None,          000000, "",         "",            "System shutdown"           }, /* 010 + 34 */
    { None,          017400, " port ",   "",            "Save buffer pointer"       }, /* 010 + 35 */
    { None,          017400, " port ",   "",            "Restore buffer pointer"    }, /* 010 + 36 */
    { DMA_Chars,     017400, " port ",   "",            "Transmit console message"  }, /* 010 + 37 */

/* 2000 Access SP extension entries */

    { Decimal,       000000, "",         "",            "Initialize IOP"            }, /* 010 + 40 */
    { None,          000000, "",         "",            "Cold dump request"         }, /* 010 + 41 */
    { Decimal,       000000, "",         "",            "Send number of ports"      }, /* 010 + 42 */
    { DMA_Octal,     000000, "",         "",            "Send device table"         }, /* 010 + 43 */
    { None,          000000, "",         "",            "System shut down"          }, /* 010 + 44 */
    { Decimal,       000000, "",         "",            "Send date code"            }, /* 010 + 45 */
    { None,          000000, "",         "",            NULL                        }, /* 010 + 46 */
    { None,          000000, "",         "",            NULL                        }, /* 010 + 47 */

/* 2000 Access IOP primary entries */

    { None,          017400, " port ",   " seconds ",   "Have a line"               }, /* 060 + 00 */
    { None,          017400, " port ",   " seconds ",   "Have a line (parity)"      }, /* 060 + 01 */
    { None,          017400, " port ",   " seconds ",   "Have a line (lost)"        }, /* 060 + 02 */
    { None,          000000, "",         "",            NULL                        }, /* 060 + 03 */
    { None,          000000, "",         "",            NULL                        }, /* 060 + 04 */
    { None,          000000, "",         "",            NULL                        }, /* 060 + 05 */
    { None,          000000, "",         "",            NULL                        }, /* 060 + 06 */
    { None,          000000, "",         "",            NULL                        }, /* 060 + 07 */

/* 2000 Access IOP secondary entries */

    { None,          017400, " port ",   "",            "User abort request"        }, /* 070 + 00 */
    { None,          017400, " port ",   "",            "Buffer full"               }, /* 070 + 01 */
    { None,          017400, " port ",   "",            "Buffer empty"              }, /* 070 + 02 */
    { None,          017400, " port ",   "",            "ENTER timed out"           }, /* 070 + 03 */
    { None,          017400, " port ",   "",            "User hung up"              }, /* 070 + 04 */
    { None,          017600, " count ",  "",            "Send console message"      }, /* 070 + 05 */
    { Status,        017600, " device ", "",            "Allocate device for RJE"   }, /* 070 + 06 */
    { None,          017600, " device ", "",            "Release device from RJE"   }, /* 070 + 07 */
    { None,          017600, " device ", "",            "Wake user up"              }, /* 070 + 10 */
    { None,          000000, "",         "",            "Wake RJE up"               }, /* 070 + 11 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 12 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 13 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 14 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 15 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 16 */
    { None,          000000, "",         "",            NULL                        }, /* 070 + 17 */

/* 2000F SP remapping entries */

    { None,          017400, " port ",   " rate code ", "Baud rate"                 }, /* 110 + 00 */
    { Status,        017400, " port ",   "",            "Illegal input"             }, /* 110 + 01 */
    { Status,        017400, " port ",   "",            "Is buffer full"            }, /* 110 + 02 */
    { None,          017600, " device ", "",            "Line printer select code"  }, /* 110 + 03 */
    { Octal,         017400, " port ",   "",            "Line printer request"      }, /* 110 + 04 */
    { None,          000000, "",         "",            "Line printer disconnect"   }, /* 110 + 05 */
    { Octal,         000000, "",         "",            "Line printer status"       }, /* 110 + 06 */
    { None,          017400, " port ",   "",            "Character size"            }, /* 110 + 07 */
    { None,          017400, " port ",   "",            "Subtype info"              }, /* 110 + 10 */
    { Octal,         017400, " port ",   "",            "Get receive parameter"     }, /* 110 + 11 */
    { Octal,         017400, " port ",   "",            "What terminal type"        }  /* 110 + 12 */
    };

static const uint32 remap_2000F [] = {                  /* remap from 2000 Access to 2000F opcodes */
    0000, 0001, 0035, 0003, 0110, 0045, 0046, 0007,     /*   SP remapping entries 000-007 */
    0010, 0011, 0012, 0013, 0014, 0015, 0016, 0017,     /*   SP remapping entries 010-017 */
    0020, 0111, 0022, 0023, 0024, 0025, 0112, 0113,     /*   SP remapping entries 020-027 */
    0114, 0115, 0116, 0033, 0117, 0120, 0121, 0037,     /*   SP remapping entries 030-037 */
    0122, 0042, 0007, 0007, 0007, 0007, 0007, 0007,     /*   SP remapping entries 040-047 */
    0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,     /*   SP remapping entries 050-057 */

    0060, 0070, 0071, 0072, 0073, 0074, 0066, 0067      /*   IOP remapping entries 060-067 */
    };


#define STATUS_BIAS     3                       /* bias for using status as an index */

static char *status_names [] = {                /* names for the status returns */
    "No data available on RJE or LT",           /*   -3 */
    "End of file",                              /*   -2 */
    "Buffer not ready",                         /*   -1 */
    "Operation successful",                     /*    0 */
    "Device not ready",                         /*    1 */
    "Device error",                             /*    2 */
    "Attention needed",                         /*    3 */
    "Read/write failure"                        /*    4 */
    };


 /* IPL card state */

typedef struct {
    HP_WORD    output_word;                     /* output word register */
    HP_WORD    input_word;                      /* input word register */
    FLIP_FLOP  command;                         /* command flip-flop */
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    } CARD_STATE;

static CARD_STATE ipl [CARD_COUNT];             /* per-card state */


/* IPL I/O device state.

   The 12566B Microcircuit Interface provides a 16-bit Data Out bus and a 16-bit
   Data In bus, as well as an outbound Device Command signal and an inbound
   Device Flag signal to indicate data availability.  The output and input
   states are modelled by a pair of structures that also contain Boolean flags
   to indicate cable connectivity.

   The two interface cards provided each may be connected in one of four
   possible ways:

    1. No connection (the I/O cable is not connected).

    2. Loopback connection (a loopback connector is in place).

    3. Cross connection (an I/O cable connects one card to the other card in the
       same machine).

    4. Processor interconnection (an I/O cable connects a card in one machine to
       a card in the other machine).

   In simulation, these four connection states are modelled by setting input and
   output pointers (accessors) to point at the appropriate state structures, as
   follows:

    1. The input and output accessors point at separate local input and output
       state structures.

    2. The input and output accessors point at a single local state structure.

    3. The input and output accessors of one card point at the separate local
       state structures of the other card.

    4. The input and output accessors of one card point at the separate shared
       state structures of the other card.

   Connection is accomplished by having an output accessor and an input accessor
   point at the same state structure.  Graphically, the four possibilities are:

     1. No connection:

                             +------------------+
        card [n].output -->  |     Data Out     |
                             +------------------+
                             |  Device Command  |
                             +------------------+

                             +------------------+
        card [n].input  -->  |     Data In      |
                             +------------------+
                             |   Device Flag    |
                             +------------------+


     2. Loopback connection:

                             +------------------+------------------+
        card [n].output -->  |     Data Out     |     Data In      |  <-- card [n].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+


     3. Cross connection:

                             +------------------+------------------+
        card [0].output -->  |     Data Out     |     Data In      |  <-- card [1].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [0].input  -->  |     Data In      |     Data Out     |  <-- card [1].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+


     4. Processor interconnection:

                             +------------------+------------------+
        card [0].output -->  |     Data Out     |     Data In      |  <-- card [1].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [0].input  -->  |     Data In      |     Data Out     |  <-- card [1].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+

                             +------------------+------------------+
        card [1].output -->  |     Data Out     |     Data In      |  <-- card [0].input
                             +------------------+------------------+
                             |  Device Command  |   Device Flag    |
                             +------------------+------------------+

                             +------------------+------------------+
        card [1].input  -->  |     Data In      |     Data Out     |  <-- card [0].output
                             +------------------+------------------+
                             |   Device Flag    |  Device Command  |
                             +------------------+------------------+

   In all but case 1, two accessors point at the same structure but with
   different views.
*/

typedef struct {
    t_bool   cable_connected;                   /* TRUE if the inbound cable is connected */
    t_bool   device_flag_in;                    /* external DEVICE FLAG signal state */
    HP_WORD  data_in;                           /* external DATA IN signal bus */
    } INPUT_STATE, *INPUT_STATE_PTR;

typedef struct {
    t_bool   cable_connected;                   /* TRUE if the outbound cable is connected */
    t_bool   device_command_out;                /* external DEVICE COMMAND signal state */
    HP_WORD  data_out;                          /* external DATA OUT signal bus */
    } OUTPUT_STATE, *OUTPUT_STATE_PTR;

typedef struct {                                /* the normal ("forward direction") state view */
    INPUT_STATE   input;
    OUTPUT_STATE  output;
    } FORWARD_STATE;

typedef struct {                                /* the cross-connected ("reverse direction") state view */
    OUTPUT_STATE  output;
    INPUT_STATE   input;
    } REVERSE_STATE;

typedef union {                                 /* the state may be accessed in either direction */
    FORWARD_STATE  forward;
    REVERSE_STATE  reverse;
    } IO_STATE, *IO_STATE_PTR;

typedef struct {
    INPUT_STATE_PTR   input;                    /* the input accessor */
    OUTPUT_STATE_PTR  output;                   /* the output accessor */
    } STATE_PTRS;


/* IPL synchronizer states.

   One mechanism that synchronizes two simulator instances is implemented by an
   interlocked gate that is in one of three basic states: Locked, Unlocking, or
   Unlocked.  When operating in the synchronous mode, each instance schedules
   the IPLO unit to rendezvous with the other instance after a preset number of
   machine instructions have been executed.  The first instance that arrives at
   the gate locks it and then waits in a loop for the other unit to arrive.
   When the other instance arrives, it begins unlocking the gate but waits for
   an acknowledgement from the first instance before leaving the gate unlocked.
   Then both instances reschedule their respective IPLO units, and the process
   repeats.

   To allow efficient implementation, the wait loop starts out compute-bound but
   then shifts to event waits after a short time.  The shift is noted by a state
   change from Locked to Locked_Wait (or Unlocking to Unlocking_Wait).  This is
   required because the event waits must be signaled but the compute waits must
   not.

   The allowed state transitions are:

     Transition                    Cause
     ---------------------------   -----------------------------------------
     Unlocked -> Locked            First instance arrives at the rendezvous
     Locked -> Locked_Wait         Second instance is slow to arrive
     Locked -> Unlocking           Second instance arrives at the rendezvous
     Locked -> Unlocked            Abort before second instance arrives
     Locked_Wait -> Unlocking      Second instance arrives at the rendezvous
     Locked_Wait -> Unlocked       Abort before second instance arrives
     Unlocking -> Unlocking_Wait   First instance is slow to confirm
     Unlocking -> Unlocked         First instance confirms resumption


   Implementation notes:

    1. The Wait states must be +1 numerically from the corresponding non-wait
       states.
*/

typedef enum {                                  /* the CPU execution interlock state */
    Unlocked,                                   /*   the gate is unlocked */
    Unlocking,                                  /*   the gate is unlocking */
    Unlocking_Wait,                             /*   the gate is unlocking and waiting */
    Locked,                                     /*   the gate is locked */
    Locked_Wait                                 /*   the gate is locked and waiting */
    } GATE_STATE;

static const char *gate_state_names [] = {      /* gate state names corresponding to GATE_STATE */
    "Unlocked",
    "Unlocking",
    "Unlocking and waiting",
    "Locked",
    "Locked and waiting" };


/* IPL shared memory region */

typedef enum {                                  /* the version of Time-Shared BASIC currently running */
    HP_2000BC,                                  /*   HP 2000B, C, or C' (C-prime) */
    HP_2000F,                                   /*   HP 2000F */
    HP_2000_Access                              /*   HP 2000 Access */
    } OS_VERSION;

typedef struct {                                /* the shared memory region */
    GATE_STATE  gate;                           /*   the state of the CPU interlock gate */
    uint32      count;                          /*   the count of instructions to execute before rendezvous */
    OS_VERSION  tsb_version;                    /*   the version of TSB that is running */
    IO_STATE    dev_bus [CARD_COUNT];           /*   the IPL I/O device state */
    } SHARED_REGION;


/* IPL interface state */

static t_bool cpu_is_iop    = FALSE;            /* TRUE if this is the IOP instance, FALSE if SP instance */
static int32  poll_wait     = 50;               /* maximum poll wait time (in event ticks) */
static int32  edt_delay     = 0;                /* EDT delay (in milliseconds) */
static int32  fallback_wait = 2;                /* sleep time if semaphores are not supported (in seconds) */

static char   event_name [PATH_MAX];            /* the event name; last character specifies which event */
static uint32 event_error  = 0;                 /* the host OS error code from a failed process sync call */
static t_bool wait_aborted = FALSE;             /* TRUE if the user aborted a SET IPL WAIT command */
static EVENT  sync_id      = UNDEFINED_EVENT;   /* the synchronization wait event */
static EVENT  lock_id      = UNDEFINED_EVENT;   /* the lock wait event */
static EVENT  unlock_id    = UNDEFINED_EVENT;   /* the unlock wait event */

static SHMEM         *shared_id;                /* a pointer to the shared memory identifier */
static SHARED_REGION local_region;              /* the local I/O device state */
static SHARED_REGION *shared_ptr;               /* a pointer to the shared I/O device state */

static uint32 sync_avg  = 0;                    /* average interlock wait time  */
static uint32 sync_max  = 0;                    /* maximum interlock wait time */
static uint32 sync_cnt  = 0;                    /* count of interlock calls */
static float  sync_mean = 0.0;                  /* running average interlock wait time */

static STATE_PTRS io_ptrs [CARD_COUNT] = {              /* the card accessors pointing at the local state */
    { &local_region.dev_bus [ipli].forward.input,       /*   card [0].input */
      &local_region.dev_bus [ipli].forward.output },    /*   card [0].output */

    { &local_region.dev_bus [iplo].forward.input,       /*   card [1].input */
      &local_region.dev_bus [iplo].forward.output }     /*   card [1].output */
    };


/* IPL I/O interface routines */

static INTERFACE ipl_interface;


/* IPL interface local SCP support routines */

static t_stat ipl_set_diag  (UNIT *uptr, int32 value, char  *cptr, void *desc);
static t_stat ipl_set_sync  (UNIT *uptr, int32 value, char  *cptr, void *desc);
static t_stat ipl_show_sync (FILE *st,   UNIT  *uptr, int32 value, void *desc);


/* IPL device local SCP support routines */

static t_stat ipl_reset  (DEVICE *dptr);
static t_stat ipl_attach (UNIT *uptr, char *cptr);
static t_stat ipl_detach (UNIT *uptr);
static t_stat ipl_boot   (int32 unitno, DEVICE *dptr);


/* IPL device local utility routines */

static t_stat card_service (UNIT *uptr);
static t_stat sync_service (UNIT *uptr);

static t_stat   wait_at_gate  (EVENT event_id, GATE_STATE initial, GATE_STATE final);
static void     release_wait  (EVENT event_id, GATE_STATE initial, GATE_STATE final);
static void     activate_unit (UNIT *uptr, int32 wait_time);
static void     wru_handler   (int signal);
static RESPONSE trace_command (CARD_INDEX card, HP_WORD command, RESPONSE response);
static RESPONSE trace_status  (CARD_INDEX card, HP_WORD status,  RESPONSE response);


/* Host-specific process synchronization routines */

static uint32 create_event       (const char *name, EVENT *event);
static uint32 destroy_event      (const char *name, EVENT *event);
static uint32 wait_event         (EVENT event, uint32 wait_in_ms, t_bool *signaled);
static uint32 signal_event       (EVENT event);


/* IPL SCP data structures */

/* Device information blocks */

static DIB ipl_dib [CARD_COUNT] = {
    { &ipl_interface,                                   /* the device's I/O interface function pointer */
      IPLI,                                             /* the device's select code (02-77) */
      0,                                                /* the card index */
      "12875A Processor Interconnect Lower Data PCA",   /* the card description */
      "12992K Processor Interconnect Loader" },         /* the ROM description */

    { &ipl_interface,                                   /* the device's I/O interface function pointer */
      IPLO,                                             /* the device's select code (02-77) */
      1,                                                /* the card index */
      "12875A Processor Interconnect Upper Data PCA",   /* the card description */
      NULL }                                            /* the ROM description */
    };


/* Unit lists */

static UNIT ipl_unit [CARD_COUNT] = {
    { UDATA (&card_service, UNIT_ATTABLE, 0) }, /* the IPLI unit handles I/O for both cards */
    { UDATA (&sync_service, UNIT_ATTABLE, 0) }  /* the IPLO unit handles CPU interlocking */
    };


/* Register lists.

   Five registers are hidden from the user.  The EDTDELAY value sets the number
   of milliseconds to suspend the simulator after an IOP-to-SP data transfer
   completes; see the notes for the "ipl_interface" routine for details.  The
   EVTERR value is set to the host system error code if an event operation
   fails.  AVG and MAX hold the interlock synchronizer's average and maximum
   loop iteration counts while waiting for the other instance to respond.  CNT
   is the total number of interlock calls made.  These last three values are
   reset to zero when a RESET -P IPL command is issued.
*/

static REG ipli_reg [] = {
/*    Macro   Name      Location                Width  Offset         Flags         */
/*    ------  --------  ----------------------  -----  ------  -------------------- */
    { ORDATA (IBUF,     ipl [ipli].input_word,   16)                                },
    { ORDATA (OBUF,     ipl [ipli].output_word,  16)                                },
    { FLDATA (CTL,      ipl [ipli].control,              0)                         },
    { FLDATA (FLG,      ipl [ipli].flag,                 0)                         },
    { FLDATA (FBF,      ipl [ipli].flag_buffer,          0)                         },
    { DRDATA (TIME,     poll_wait,               24),          PV_LEFT              },
    { DRDATA (WAIT,     fallback_wait,           24),          PV_LEFT              },
    { DRDATA (EDTDELAY, edt_delay,               32),          PV_LEFT | REG_HIDDEN },
    { DRDATA (EVTERR,   event_error,             32),          PV_LEFT | REG_HRO    },
    { DRDATA (AVG,      sync_avg,                32),          PV_LEFT | REG_HRO    },
    { DRDATA (MAX,      sync_max,                32),          PV_LEFT | REG_HRO    },
    { DRDATA (CNT,      sync_cnt,                32),          PV_LEFT | REG_HRO    },

      DIB_REGS (ipli_dib),

    { NULL }
    };

static REG iplo_reg [] = {
/*    Macro   Name      Location                Width  Offset         Flags         */
/*    ------  --------  ----------------------  -----  ------  -------------------- */
    { ORDATA (IBUF,     ipl [iplo].input_word,   16)                                },
    { ORDATA (OBUF,     ipl [iplo].output_word,  16)                                },
    { FLDATA (CTL,      ipl [iplo].control,              0)                         },
    { FLDATA (FLG,      ipl [iplo].flag,                 0)                         },
    { FLDATA (FBF,      ipl [iplo].flag_buffer,          0)                         },

      DIB_REGS (iplo_dib),

    { NULL }
    };


/* Modifier lists */

typedef enum {                                  /* Synchronization SET command values */
    Interlock,                                  /*   SET IPL INTERLOCK */
    Signal,                                     /*   SET IPL SIGNAL */
    Wait                                        /*   SET IPL WAIT */
    } SYNC_MODE;

static MTAB ipl_mod [] = {
/*    Mask Value  Match Value  Print String       Match String   Validation      Display  Descriptor */
/*    ----------  -----------  -----------------  -------------  --------------  -------  ---------- */
    { UNIT_DIAG,  UNIT_DIAG,  "diagnostic mode",  "DIAGNOSTIC",  &ipl_set_diag,  NULL,    NULL       },
    { UNIT_DIAG,  0,          "link mode",        "LINK",        &ipl_set_diag,  NULL,    NULL       },

/*    Entry Flags           Value       Print String  Match String  Validation      Display          Descriptor        */
/*    --------------------  ----------  ------------  ------------  --------------  ---------------  ----------------- */
    { MTAB_XDV | MTAB_NMO,  Interlock,  "INTERLOCK",  "INTERLOCK",  &ipl_set_sync,  &ipl_show_sync,  NULL              },
    { MTAB_XDV,             Signal,     NULL,         "SIGNAL",     &ipl_set_sync,  NULL,            NULL              },
    { MTAB_XDV,             Wait,       NULL,         "WAIT",       &ipl_set_sync,  NULL,            NULL              },

    { MTAB_XDV,              2u,        "SC",         "SC",         &hp_set_dib,    &hp_show_dib,    (void *) &ipl_dib },
    { MTAB_XDV | MTAB_NMO,  ~2u,        "DEVNO",      "DEVNO",      &hp_set_dib,    &hp_show_dib,    (void *) &ipl_dib },
    { 0 }
    };


/* Debugging trace lists */

static DEBTAB ipli_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "PSERV", TRACE_PSERV },                   /* trace periodic unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };

static DEBTAB iplo_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "STATE", TRACE_STATE },                   /* trace state changes */
    { "PSERV", TRACE_PSERV },                   /* trace periodic unit service scheduling calls and entries */
    { "XFER",  TRACE_XFER  },                   /* trace data transmissions */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptors */

DEVICE ipli_dev = {
    "IPL",                                      /* device name (logical name "IPLI") */
    &poll_unit,                                 /* unit array */
    ipli_reg,                                   /* register array */
    ipl_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    16,                                         /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ipl_reset,                                 /* reset routine */
    &ipl_boot,                                  /* boot routine */
    &ipl_attach,                                /* attach routine */
    &ipl_detach,                                /* detach routine */
    &ipli_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    ipli_deb,                                   /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

DEVICE iplo_dev = {
    "IPLO",                                     /* device name */
    &sync_unit,                                 /* unit array */
    iplo_reg,                                   /* register array */
    ipl_mod,                                    /* modifier array */
    1,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    16,                                         /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &ipl_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    &ipl_attach,                                /* attach routine */
    &ipl_detach,                                /* detach routine */
    &iplo_dib,                                  /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    iplo_deb,                                   /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dptrs [CARD_COUNT] = {           /* device pointer array */
    &ipli_dev,
    &iplo_dev
    };



/* IPL I/O interface routines */



/* Microcircuit interface.

   In the link mode, the IPLI and IPLO devices are linked via a shared memory
   region to the corresponding cards in another CPU instance.  If only one or
   the other device is in the diagnostic mode, we simulate the attachment of a
   loopback connector to that device.  If both devices are in the diagnostic
   mode, we simulate the attachment of the interprocessor cable between IPLI and
   IPLO in this machine.


   Implementation notes:

    1. When tracing commands and status words, commands from this simulator
       instance are sent on the outbound side of the output (higher select code)
       card, and status is returned on the inbound side of the same card.
       Commands from the other instance are received on the inbound side of the
       input (lower select code) card, and status is returned on the outbound
       side of the same card.

    2. Command tracing is meaningless unless an HP 2000 Time-Shared BASIC
       operating system is running.  Testing for the shared memory allocation
       that simulates the interconnecting cables indicates whether command
       tracing is meaningful.

    3. 2000 Access has a race condition that manifests itself by an apparently
       normal boot and operational system console but no PLEASE LOG IN response
       to terminals connected to the multiplexer.  The frequency of occurrence
       is higher on multiprocessor host systems, where the SP and IOP instances
       may execute concurrently.

       The cause is this code in the SP disc loader (source files S2883, S7900,
       S79X0, S79X3, and S79XX):

         LDA SDVTR     REQUEST
         JSB IOPMA,I     DEVICE TABLE
         [...]
         STC DMAHS,C   TURN ON DMA
         SFS DMAHS     WAIT FOR
         JMP *-1         DEVICE TABLE
         STC CH2,C     SET CORRECT
         CLC CH2         FLAG DIRECTION

       DMA completion causes the SFS instruction to skip the JMP.  The STC/CLC
       pair at the end normally would cause a Processor Interconnect interrupt
       and a second Request Device Table command to be recognized by the IOP,
       except that the IOP DMA setup routine DMAXF (in source file SD61)
       specifies an end-of-block CLC that holds off the interconnect interrupt,
       and the DMA interrupt completion routine DMCMP ends with a STC,C that
       clears the interconnect flag.

       The SP program executes four instructions between DMA completion and the
       CLC.  The IOP program executes 34 instructions between the DMA completion
       interrupt and the STC,C that resets the Processor Interconnect.  In
       hardware, the two CPUs are essentially interlocked by the DMA transfer,
       and DMA completion occurs almost simultaneously in each machine.
       Therefore, the STC/CLC in the SP is guaranteed to occur before the STC,C
       in the IOP, and the Processor Interconnect interrupt never occurs.  Under
       simulation, and especially on multi-core hosts, that guarantee does not
       hold.  If host load preemption causes the STC/CLC to occur after the
       STC,C, then the IOP starts a second device table DMA transfer, which the
       SP is not expecting.  Consequently, the IOP never processes the
       subsequent Start Timesharing command, and the multiplexer does not
       respond to user logon requests.

       This situation can be avoided by using the SET IPL INTERLOCK command to
       synchronize execution of the SP and IOP instances.  The interlock value
       is critical; it cannot be more than 16 instructions to allow for the
       worst-case preemption scenario.  That occurs when the SP instance does
       not receive the last DMA input word during a poll that occurs at the last
       instruction of the SP's interlock quantum, and then the IOP outputs the
       last DMA word with the first instruction of its quantum.  The IOP will
       then execute a full quantum (16 instructions) and rendezvous with the SP.
       If the SP blocks immediately after rendezvous, the IOP can then execute a
       second full quantum (another 16 instructions) before the SP is able to
       pick up the last word and execute its CLC.  To avoid this, two interlock
       times must be less than the critical instruction path length of 34
       instructions.

       Synchronization must remain active at least until the IOP has completed
       its initialization.  If synchronization events are not supported on the
       host platform, the simulator employs a workaround that decreases the
       incidence of the problem: the DMA output completion interrupt is delayed
       to allow the other SIMH instance a chance to process its own DMA input
       completion interrupt first.  This improves the race condition by delaying
       the IOP until the SP has a chance to receive the last word, recognize its
       own DMA input completion, drop out of the SFS loop, and execute the
       STC/CLC.  The delay is initially set to one millisecond but is exposed
       via a hidden IPLI register, EDTDELAY, that allows the user to lengthen
       the delay if necessary.

       Using this fallback mechanism instead of CPU synchronization only
       improves the condition.  It does not solve it because delaying the IOP
       does not guarantee that the SP will actually execute.  It is possible
       that a higher-priority host process will preempt the SP, and that at the
       delay expiration, the SP still has not executed the STC/CLC.  Still, in
       testing, the incidence dropped dramatically, so the problem is much less
       intrusive.
*/

static SIGNALS_VALUE ipl_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const char * const iotype [] = { "Status", "Command" };
const CARD_INDEX card = (CARD_INDEX) dibptr->card_index;    /* set the card selector */
static RESPONSE response [CARD_COUNT] = { None, None };     /* expected TSB command responses */

INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };
t_bool         irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            ipl [card].flag_buffer = CLEAR;             /* reset the flag buffer */
            ipl [card].flag        = CLEAR;             /*   and flag flip-flops */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            ipl [card].flag_buffer = SET;               /* set the flag buffer flip-flop */
            break;


        case ioENF:                                     /* Enable Flag */
            if (ipl [card].flag_buffer == SET)          /* if the flag buffer flip-flop is set */
                ipl [card].flag = SET;                  /*   then set the flag flip-flop */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (ipl [card].flag == CLEAR)               /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (ipl [card].flag == SET)                 /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = ipl [card].input_word;     /* get the return data */

            tpprintf (dptrs [card], TRACE_CSRW, "%s input word is %06o\n",
                      iotype [card ^ 1], ipl [card].input_word);

            if (TRACINGP (dptrs [card], TRACE_CMD) && shared_ptr != NULL)
                if (card == iplo)
                    response [card] = trace_status (card, outbound.value, response [card]);
                else
                    response [card] = trace_command (card, outbound.value, response [card]);
            break;


        case ioIOO:                                     /* I/O data output */
            ipl [card].output_word = inbound_value;     /* clear supplied status */

            io_ptrs [card].output->data_out = ipl [card].output_word;   /* place the word on the data bus */

            tpprintf (dptrs [card], TRACE_CSRW, "%s output word is %06o\n",
                      iotype [card], ipl [card].output_word);

            if (TRACINGP (dptrs [card], TRACE_CMD) && shared_ptr != NULL)
                if (card == iplo)
                    response [card] = trace_command (card, inbound_value, response [card]);
                else
                    response [card] = trace_status (card, inbound_value, response [card]);
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            ipl [card].flag_buffer = SET;               /* set the flag buffer flip-flop */
            ipl [card].output_word = 0;                 /*   and clear the output register */

            io_ptrs [card].output->data_out = 0;
            break;


        case ioCRS:                                     /* Control Reset */
            ipl [card].control = CLEAR;                 /* clear the control flip-flop */
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            ipl [card].control = CLEAR;                 /* clear the control flip-flop */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            ipl [card].control = SET;                   /* set the control flip-flop */

            io_ptrs [card].output->device_command_out = TRUE;   /* assert Device Command */

            tpprintf (dptrs [card], TRACE_XFER, "Word %06o sent to link\n",
                      ipl [card].output_word);

            sim_cancel (&poll_unit);                    /* reschedule the poll immediately */
            activate_unit (&poll_unit, 1);              /*   as we're expecting a response */
            break;


        case ioEDT:                                     /* end data transfer */
            response [card] = None;                     /* clear data response */

            if (cpu_is_iop                              /* if this is the IOP instance */
              && inbound_signals & ioIOO                /*   and the card is doing output */
              && card == ipli                           /*     on the input card */
              && edt_delay > 0) {                       /*       and a delay is specified */
                sim_os_ms_sleep (edt_delay);            /*         then delay DMA completion */

                tprintf (ipli_dev, TRACE_CMD, "Delayed DMA completion interrupt for %d msec\n",
                         edt_delay);
                }
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (ipl [card].control & ipl [card].flag)   /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (ipl [card].control & ipl [card].flag    /* if the control and flag */
              & ipl [card].flag_buffer)                 /*   and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;    /*     then conditionally assert IRQ */

            if (ipl [card].flag == SET)                 /* if the flag flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            ipl [card].flag_buffer = CLEAR;             /* clear the flag buffer flip-flop */
            break;


        case ioIEN:                                     /* Interrupt Enable */
            irq_enabled = TRUE;                         /* permit IRQ to be asserted */
            break;


        case ioPRH:                                         /* Priority High */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;                  /*   then assert it unconditionally */
            break;


        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}



/* IPL interface local SCP support routines */



/* Set the diagnostic or link mode.

   This validation routine is entered with the "value" parameter set to zero if
   the unit is to be set into the link (normal) mode or non-zero if the unit is
   to be set into the diagnostic mode.  The character and descriptor pointers
   are not used.

   In addition to setting or clearing the UNIT_DIAG flag, the I/O state pointers
   are set to point at the appropriate state structure.  The selected pointer
   configuration depends on whether none, one, or both the IPLI and IPLO devices
   are in diagnostic mode.

   If both devices are in diagnostic mode, the pointers are set to point at
   their respective state structures but with the input and output pointers
   reversed.  This simulates connecting one of the interprocessor cables between
   the two cards within the same CPU, permitting the Processor Interconnect
   Cable Diagnostic to be run.

   If only one of the devices is in diagnostic mode, the pointers are set to
   point at the device's state structure with the input and output pointers
   reversed.  This simulates connected a loopback connector to the card,
   permitting the General Register Diagnostic to be run.

   If a device is in link mode, that device's pointers are set to point at the
   corresponding parts of the device's state structure.  This simulates a card
   with no cable connected.

   If the device is attached, setting it into diagnostic mode will detach it
   first.
*/

static t_stat ipl_set_diag (UNIT *uptr, int32 value, char *cptr, void *desc)
{
const IO_STATE_PTR isp = local_region.dev_bus;          /* pointer to the local bus state */

if (value) {                                            /* if this is an entry into diagnostic mode */
    ipl_detach (uptr);                                  /*   then detach it first */
    uptr->flags |= UNIT_DIAG;                           /*     before setting the flag */
    }

else                                                    /* otherwise this is an entry into link mode */
    uptr->flags &= ~UNIT_DIAG;                          /*   so clear the flag */

if (poll_unit.flags & sync_unit.flags & UNIT_DIAG) {        /* if both devices are now in diagnostic mode */
    io_ptrs [ipli].input  = &isp [iplo].reverse.input;      /*   then connect the cable */
    io_ptrs [ipli].output = &isp [ipli].forward.output;     /*     so that the outputs of one card */
    io_ptrs [iplo].input  = &isp [ipli].reverse.input;      /*       are connected to the inputs of the other card */
    io_ptrs [iplo].output = &isp [iplo].forward.output;     /*         and vice versa */

    io_ptrs [ipli].output->cable_connected = TRUE;          /* indicate that the cable */
    io_ptrs [iplo].output->cable_connected = TRUE;          /*   has been connected between the cards */
    }

else {                                                      /* otherwise */
    if (poll_unit.flags & UNIT_DIAG) {                      /*   if the input card is in diagnostic mode */
        io_ptrs [ipli].input  = &isp [ipli].reverse.input;  /*     then loop the card outputs */
        io_ptrs [ipli].output = &isp [ipli].forward.output; /*       back to the inputs and vice versa */
        io_ptrs [ipli].output->cable_connected = TRUE;      /*         and indicate that the card is connected */
        }

    else {                                                  /*   otherwise the card is in link mode */
        io_ptrs [ipli].input  = &isp [ipli].forward.input;  /*     so point at the card state */
        io_ptrs [ipli].output = &isp [ipli].forward.output; /*       in the normal direction */
        io_ptrs [ipli].output->cable_connected = FALSE;     /*         and indicate that the card is not connected */
        }

    if (sync_unit.flags & UNIT_DIAG) {                      /* otherwise */
        io_ptrs [iplo].input  = &isp [iplo].reverse.input;  /*   if the output card is in diagnostic mode */
        io_ptrs [iplo].output = &isp [iplo].forward.output; /*     then loop the card outputs */
        io_ptrs [iplo].output->cable_connected = TRUE;      /*       back to the inputs and vice versa */
        }                                                   /*         and indicate that the card is connected */

    else {
        io_ptrs [iplo].input  = &isp [iplo].forward.input;  /*   otherwise the card is in link mode */
        io_ptrs [iplo].output = &isp [iplo].forward.output; /*     so point at the card state */
        io_ptrs [iplo].output->cable_connected = FALSE;     /*       in the normal direction */
        }                                                   /*         and indicate that the card is not connected */
    }

return SCPE_OK;
}


/* Synchronize the simulator instance.

   This validation routine is entered with the "uptr" parameter pointing
   at the input unit and the "value" parameter set to the selected
   synchronization command.  The character and descriptor pointers are not used.

   This routine is called for the following commands:

     SET IPL INTERLOCK=<n>
     SET IPL SIGNAL
     SET IPL WAIT

   Setting a non-zero interlock value establishes instruction synchronization
   between the simulator instances and starts the synchronizer.  The count gives
   the number of instructions that are executed before a rendezvous is
   attempted.  If the count is set to 0, the synchronizer is stopped, and the
   instances execute freely.

   The WAIT command causes the simulator to wait until the event signal is
   received from the other instance.  The SIGNAL command sends the event signal
   to the other instance.

   For all three commands, if the shared memory area used to communicate between
   the instances has not been created yet by attaching the IPL device, the
   routine returns "Unit not attached" status.  If the unit is attached but
   event creation failed, "Command not allowed" status is returned.  If the
   signal or wait function returned an execution error, the routine returns
   "Command not completed" to indicate that the error code register should be
   checked.

   To permit the user to abort the WAIT command, waits of 100 milliseconds each
   are performed in a loop.  At each pass through the loop, the keyboard is
   polled for a CTRL+E, and the wait is abandoned if one if seen.  If the user
   aborts the wait, "Command not completed" status is returned.

   If the unit is attached but the synchronization events are not defined, the
   WAIT command falls back to a timed wait, and the SIGNAL command falls back to
   doing nothing.  This allows at least a chance that these commands will
   suffice on systems that does not support synchronization.  If the INTERLOCK
   command is called, it will return a "Command not allowed" error, as there is
   no practicable fallback mechanism for instruction synchronization.


   Implementation notes:

    1. The "wait_event" routine returns TRUE if the event is signaled and FALSE
       if it times out while waiting.

    2. UNIX systems do not pass CTRL+E through the keyboard interface but
       instead signal SIGINT.  To catch these, a SIGINT handler is installed
       that sets the "wait_aborted" variable TRUE if it is called.  The variable
       is checked in the loop, and the wait is abandoned if it is set.

    3. The console must be changed to non-blocking mode in order to obtain the
       CTRL+E keystroke without requiring a newline terminator.  Bracketing
       calls to "sim_ttrun" and "sim_ttcmd" are used to do this, even though
       they also drop the simulator priority, which is unnecessary.
*/

static t_stat ipl_set_sync (UNIT *uptr, int32 value, char *cptr, void *desc)
{
typedef void (*SIG_HANDLER) (int);              /* signal handler function type */
const uint32  wait_time = 100;                  /* the wait time in milliseconds */
const uint32  count_base = 10;                  /* the radix for the interlock count */
const t_value count_max = UINT_MAX;             /* the maximum interlock count value */
SIG_HANDLER   prior_handler;
t_bool        signaled;
t_value       count;
t_stat        status = SCPE_OK;

if (shared_ptr == NULL)                                 /* if shared memory has not been allocated */
    status = SCPE_UNATT;                                /*   then report that the unit must be attached first */

else switch ((SYNC_MODE) value) {                       /* otherwise dispatch on the selected command */

    case Interlock:                                     /* SET IPL INTERLOCK */
        if (lock_id == UNDEFINED_EVENT                  /* if the lock event */
          || unlock_id == UNDEFINED_EVENT)              /*   or unlock event has not been defined yet */
            status = SCPE_NOFNC;                        /*     then the command is not allowed */

        else if (cptr == NULL || *cptr == '\0')         /* otherwise if the expected value is missing */
            status = SCPE_MISVAL;                       /*   then report the error */

        else {                                          /* otherwise an interlock count is present */
            count = get_uint (cptr, count_base,         /*   so parse the supplied value */
                              count_max, &status);

            if (status == SCPE_OK) {                    /* if it is valid */
                shared_ptr->count = (uint32) count;     /*   then set the new control value */

                if (count == 0) {                                   /* if asynchronous mode is specified */
                    release_wait (lock_id, Locked, Unlocked);       /*   then release the lock and unlock events */
                    release_wait (unlock_id, Unlocking, Unlocked);  /*     in case the other instance is waiting */

                    shared_ptr->gate = Unlocked;        /* reset to the initial state */
                    sim_cancel (&sync_unit);            /*   and stop the synchronizer */

                    sync_unit.wait = 0;                 /* indicate asynchronous mode */

                    tprintf (iplo_dev, TRACE_PSERV, "Synchronizer stopped\n");
                    }

                else {                                  /* otherwise synchronous mode is specified */
                    if (sync_unit.wait == 0)            /* report if changing modes */
                        tprintf (iplo_dev, TRACE_PSERV, "Synchronizer started\n");

                    sync_unit.wait = (int32) count;                 /* record the interlock time */
                    sim_activate_abs (&sync_unit, sync_unit.wait);  /*   and start the synchronizer */
                    }
                }
            }
        break;


    case Signal:                                        /* SET IPL SIGNAL */
        if (sync_id == UNDEFINED_EVENT)                 /* if the event has not been defined yet */
            if (uptr->flags & UNIT_ATT) {               /*   but the unit is currently attached */
                status = SCPE_OK;                       /*     then fall back to an emulated SIGNAL */

                tprintf (iplo_dev, TRACE_STATE, "Event signal emulated\n");
                }

            else                                        /*   otherwise */
                status = SCPE_NOFNC;                    /*     the command is not allowed */

        else {                                          /* otherwise */
            event_error = signal_event (sync_id);       /*   signal the event */

            if (event_error == 0)                       /* if signaling succeeded */
                status = SCPE_OK;                       /*     then report command success */
            else                                        /*   otherwise */
                status = SCPE_INCOMP;                   /*     report that the command did not complete */
            }
        break;


    case Wait:
        if (sync_id == UNDEFINED_EVENT)                 /* if the event has not been defined yet */
            if (uptr->flags & UNIT_ATT) {               /*   but the unit is currently attached */
                sim_os_sleep (fallback_wait);           /*     then fall back to an emulated WAIT */
                status = SCPE_OK;                       /*       and then return success */

                tprintf (iplo_dev, TRACE_STATE, "Event wait emulated\n");
                }

            else                                        /*   otherwise */
                status = SCPE_NOFNC;                    /*     the command is not allowed */

        else {                                          /* otherwise */
            wait_aborted = FALSE;                       /*   clear the abort flag */

            prior_handler = signal (SIGINT, wru_handler);   /* install our WRU handler in place of the current one */

            if (prior_handler == SIG_ERR)               /* if installation failed */
                status = SCPE_SIGERR;                   /*   then report an error */

            else {                                      /* otherwise */
                status = sim_ttrun ();                  /*   switch the console to non-blocking mode */

                if (status != SCPE_OK)                  /* if the switch failed */
                   return status;                       /*   then report the error and quit */

                do {                                                /* otherwise */
                    event_error = wait_event (sync_id, wait_time,   /*   wait for the event */
                                              &signaled);           /*     to be signaled */

                    if (signaled == FALSE) {            /* if the wait timed out instead */
                        status = sim_os_poll_kbd ();    /*   then check for a CTRL+E keypress */

                        if (status >= SCPE_KFLAG)       /* if a regular key was pressed */
                            status = SCPE_OK;           /*   then ignore it */
                        }
                    }
                while (! (signaled                      /* continue to wait until the event is signaled */
                  || wait_aborted                       /*   or the wait is aborted by the user */
                  || status != SCPE_OK                  /*     or an SCP error occurs */
                  || event_error != 0));                /*       or an event error occurs */

                if (wait_aborted || status == SCPE_STOP     /* if the wait was aborted by the user */
                  || status == SCPE_OK && event_error != 0) /*   or an event error occurred */
                    status = SCPE_INCOMP;                   /*     then report that the command did not complete */

                sim_ttcmd ();                           /* restore the console to blocking mode */
                }

            if (status != SCPE_SIGERR)                  /* if the signal handler was set up properly */
                signal (SIGINT, prior_handler);         /*   then restore the prior handler */
            }
        break;

    default:                                            /* any other command value */
        status = SCPE_IERR;                             /*   results in an internal error */
        break;
    }

return status;                                          /* return the operation status */
}


/* Show the interlock count.

   This display routine is called to show the number of instructions executed
   before a rendezvous occurs.  The output stream is passed in the "st"
   parameter, and the other parameters are ignored.  If the count is zero, then
   the instance is executing asynchronously.  Otherwise, the count is printed.


   Implementation notes:

    1. The return status from display routines is ignored, so we must print any
       error message text here before returning.  As a consequence, a command
       file with an invalid SHOW command will continue to execute.
*/

static t_stat ipl_show_sync (FILE *st, UNIT *uptr, int32 value, void *desc)
{
if (shared_ptr == NULL)                                 /* if shared memory has not been allocated */
    fprintf (st, "%s\n", sim_error_text (SCPE_UNATT));  /*   then report that the unit must be attached first */

else if (sync_unit.wait == 0)                           /* otherwise if the interlock unit is not active */
    fputs ("Asynchronous execution\n", st);             /*   then we are operating asynchronously */

else                                                    /* otherwise report the count */
    fprintf (st, "Synchronous execution, interlock = %u\n", shared_ptr->count);

return SCPE_OK;
}



/* IPL device local SCP support routines */



/* Reset the IPL.

   This routine is called for a RESET, RESET IPLI, or RESET IPLO command.  It is
   the simulation equivalent of the POPIO signal, which is asserted by the front
   panel PRESET switch.

   For a power-on reset, the logical name "IPLI" is assigned to the first
   processor interconnect card, so that it may referenced either as that name or
   as "IPL" for use when a SET command affects both interfaces.  The interlock
   statistics are also reset.


   Implementation notes:

    1. The IPLI logical name cannot point to a static string.  Instead, it must
       be dynamically allocated because the user might DEASSIGN it, and that
       command will attempt to free the memory area.
*/

static t_stat ipl_reset (DEVICE *dptr)
{
UNIT *uptr = dptr->units;
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */
CARD_INDEX card = (CARD_INDEX) dibptr->card_index;      /* card number */

hp_enbdis_pair (dptr, dptrs [card ^ 1]);                /* ensure that the pair state is consistent */

if (sim_switches & SWMASK ('P')) {                      /* initialization reset? */
    ipl [card].input_word = 0;                          /* clear the */
    ipl [card].output_word = 0;                         /*   data registers */

    if (card == ipli) {                                 /* if this is the input card */
        sync_avg  = 0;                                  /*   then clear */
        sync_max  = 0;                                  /*     the interlock */
        sync_cnt  = 0;                                  /*       statistics */
        sync_mean = 0.0;

        if (dptr->lname == NULL)                        /* if the logical name has not been assigned yet */
            dptr->lname = strdup ("IPLI");              /*   then allocate and initialize it */
        }
    }

io_assert (dptr, ioa_POPIO);                            /* PRESET the device */

if (uptr->flags & UNIT_ATT)                             /* if the link is active for this card */
    if (card == ipli)                                   /*   then if this is the input card */
        activate_unit (uptr, poll_wait);                /*     then continue to poll for input at the idle rate */

    else {                                              /* otherwise */
        sync_unit.wait = shared_ptr->count;             /*   reestablish the interlock count */

        if (shared_ptr->count == 0)                     /* if the mode is asynchronous */
            sim_cancel (uptr);                          /*   then cancel the synchronizer */
        else                                            /* otherwise */
            sim_activate_abs (uptr, shared_ptr->count); /*   schedule the synchronizer */
        }

else                                                    /* otherwise the link is inactive */
    sim_cancel (&poll_unit);                            /*   so stop input polling */

return SCPE_OK;
}


/* Attach one end of the interconnecting cables.

   This routine connects the IPL device pair to a shared memory region.  This
   simulates connecting one end of the processor interconnect kit cables to the
   card pair in this CPU.  The command is:

     ATTACH [ -S | -I ] [ -E ] IPL <code>

   ...where <code> is a user-selected decimal number between 1 and 65535 that
   uniquely identifies the instance pair to interconnect.  The -S or -I switch
   indicates whether this instance is acting as the System Processor or the I/O
   Processor.  The -E switch indicates that the command should succeed even if
   the synchronization events cannot be created (e.g., are unsupported on the
   host system).  The command will be rejected if either device is in diagnostic
   mode, or if the <code> is omitted, malformed, or out of range.

   For backward compatibility with prior IPL implementations that used network
   interconnections, the following commands are also accepted:

     ATTACH [ -L ] [ -E ] [ IPLI | IPLO] <port-1>
     ATTACH   -C   [ -E ] [ IPLI | IPLO] <port-2>

   For these commands, -L or no switch indicates the SP instance, and -C
   indicates the IOP instance.  <port-1> and <port-2> used to indicate the
   network port numbers to use, but now it serves only to supply the code
   number from the lesser of the two values.

   Local memory is allocated to hold the code number string, which serves as the
   "attached file name" for the SCP SHOW command.  If memory allocation fails,
   the command is rejected.

   This routine creates a shared memory region and three numbered events (or
   semaphores) that are used to coordinate a data exchange with the other
   simulator instance.  If -S or -I is specified, then creation occurs after the
   ATTACH command is given for either the IPLI or IPLO device, and both devices
   are marked as attached.  If -L or -C is specified, then both devices must be
   attached before creation occurs using the lower port number.

   Object names that identify the shared memory region and synchronization
   events are derived from the <code> (or lower <port>) number and <event>
   number:

     /HP 2100-MEM-<code>
     /HP 2100-EVT-<code>-<event>

   Event number 1 is used for the SIGNAL and WAIT commands.  Event numbers 2 and
   3 are used by the instruction interlock service routine.

   Each simulator instance must use the same <code> (or <port> pair) when
   attaching for the interconnection to occur.  This permits multiple instance
   pairs to operate simultaneously and independently, if desired.

   Once shared memory is allocated, pointers to the region for the SP and IOP
   instances are set so that the output card pointer of one instance and the
   input card pointer of the other instance reference the same memory structure.
   This accomplishes the interconnection, as a write to one instance's card will
   be seen by a read from the other instance's card.

   If the shared memory allocation succeeds but the process synchronization
   event creations fail, "Command not completed" is printed on the console to
   indicate that interconnection without synchronization is permitted.  If the
   -E switch is specified, command file execution will continue; otherwise, the
   error will cause command files to abort.


   Implementation notes:

    1. The implementation supports process synchronization only on the local
       system.

    2. The object names begin with slashes to conform to POSIX requirements to
       guarantee that multiple instances to refer to the same shared memory
       region.  Omitting the slash results in implementation-defined behavior on
       POSIX systems.

    3. The shared memory region is automatically initialized to zero when it is
       originally allocated.

    4. Zeroing the "tsb_version" field of the shared memory area on creation is
       equivalent to setting it to the "HP_2000BC" enumeration value.
*/

static t_stat ipl_attach (UNIT *uptr, char *cptr)
{
t_stat       status;
int32        id_number;
size_t       last_index;
char         object_name [PATH_MAX];
char         *tptr, *zptr;
IO_STATE_PTR isp;
UNIT         *optr;

if ((poll_unit.flags | sync_unit.flags) & UNIT_DIAG)    /* if either unit is in diagnostic mode */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (uptr->flags & UNIT_ATT)                        /* otherwise if the unit is currently attached */
    ipl_detach (uptr);                                  /*   then detach it first */

id_number = (int32) strtotv (cptr, &zptr, 10);          /* parse the command for the ID number */

if (cptr == zptr || *zptr != '\0' || id_number == 0)    /* if the parse failed or extra characters or out of range */
    return SCPE_ARG;                                    /*   then reject the attach with an invalid argument error */

else {                                                  /* otherwise a single number was specified */
    tptr = (char *) malloc (strlen (cptr) + 1);         /*   so allocate a string buffer to hold the ID */

    if (tptr == NULL)                                   /* if the allocation failed */
        return SCPE_MEM;                                /*   then reject the attach with an out-of-memory error */

    else {                                              /* otherwise */
        strcpy (tptr, cptr);                            /*   copy the ID number to the buffer */
        uptr->filename = tptr;                          /*     and assign it as the attached object name */

        uptr->flags |= UNIT_ATT;                        /* set the unit attached flag */
        uptr->ID = id_number;                           /*   and save the ID number */

        activate_unit (&poll_unit, poll_wait);          /* activate the poll unit using the initial wait */
        }

    if ((sim_switches & (SP | IOP)) == 0)               /* if this is not a single-device attach */
        if (poll_unit.ID == 0 || sync_unit.ID == 0)     /*   then if both devices have not been attached yet */
            return SCPE_OK;                             /*     then we've done all we can do */

        else if (poll_unit.ID < sync_unit.ID)           /*   otherwise */
            id_number = poll_unit.ID;                   /*     determine */
        else                                            /*       the lower */
            id_number = sync_unit.ID;                   /*         ID number */

    else {                                              /* otherwise this is a single-device attach */
        if (uptr == &poll_unit)                         /*   so if we are attaching the input unit */
            optr = &sync_unit;                          /*     then point at the output unit */
        else                                            /*   otherwise we are attaching the output unit */
            optr = &poll_unit;                          /*     so point at the input unit */

        optr->filename = tptr;                          /* assign the ID as the attached object name */

        optr->flags |= UNIT_ATT;                        /* set the unit attached flag */
        optr->ID = id_number;                           /*   and save the ID number */
        }

    sprintf (object_name, "/%s-MEM-%d",                 /* generate the shared memory area name */
             sim_name, id_number);

    status = sim_shmem_open (object_name, sizeof (SHARED_REGION),   /* allocate the shared memory area */
                             &shared_id, (void **) &shared_ptr);

    if (status != SCPE_OK) {                            /* if the allocation failed */
        ipl_detach (uptr);                              /*   then detach this unit */
        return status;                                  /*     and report the error */
        }

    else {                                              /* otherwise */
        isp = shared_ptr->dev_bus;                      /*   point at the shared I/O device state */

        cpu_is_iop = ((sim_switches & (CONNECT | IOP)) != 0);   /* -C or -I imply that this is the I/O Processor */

        if (cpu_is_iop) {                                       /* if this is the IOP instance */
            io_ptrs [ipli].input  = &isp [iplo].reverse.input;  /*   then cross-connect */
            io_ptrs [ipli].output = &isp [iplo].reverse.output; /*     the input and output */
            io_ptrs [iplo].input  = &isp [ipli].reverse.input;  /*       interface cards to the */
            io_ptrs [iplo].output = &isp [ipli].reverse.output; /*         SP interface cards */

            if (cpu_configuration & CPU_IOP)                /* if IOP firmware is installed */
                shared_ptr->tsb_version = HP_2000_Access;   /*   then Access is being run */
            }

        else {                                                  /* otherwise this is the SP instance */
            io_ptrs [ipli].input  = &isp [ipli].forward.input;  /*   so connect */
            io_ptrs [ipli].output = &isp [ipli].forward.output; /*     the interface cards */
            io_ptrs [iplo].input  = &isp [iplo].forward.input;  /*       to the I/O cables */
            io_ptrs [iplo].output = &isp [iplo].forward.output; /*         directly */

            if ((cpu_configuration & CPU_FP)                /* if floating-point firmware is installed in the SP */
              && shared_ptr->tsb_version != HP_2000_Access) /*   and Access is not being run */
                shared_ptr->tsb_version = HP_2000F;         /*     then 2000F is being run */
            }

        io_ptrs [ipli].output->cable_connected = TRUE;  /* indicate that the cables to the other set */
        io_ptrs [iplo].output->cable_connected = TRUE;  /*   have been connected */

        sync_unit.wait = shared_ptr->count;             /* save the unit activation time */

        if (shared_ptr->count > 0)                              /* if the count has been set by the other */
            sim_activate_abs (&sync_unit, shared_ptr->count);   /*   simulator then start the synchronizer */
        }

    sprintf (event_name, "/%s-EVT-%d-1",                /* generate the process synchronization event name */
             sim_name, id_number);

    last_index = strlen (event_name) - 1;               /* get the index of the event digit */

    event_error = create_event (event_name, &sync_id);  /* create the first event */

    if (event_error == 0) {                                 /* if creation succeeded */
        event_name [last_index]++;                          /*   then increment the event digit */
        event_error = create_event (event_name, &lock_id);  /*     and create the second event */
        }

    if (event_error == 0) {                                     /* if creation succeeded */
        event_name [last_index]++;                              /*   then increment the event digit */
        event_error = create_event (event_name, &unlock_id);    /*     and create the third event */
        }

    if (event_error == 0)                               /* if event creation succeeded */
        return SCPE_OK;                                 /*   then report a successful attach */

    else if (sim_switches & SWMASK ('E')) {             /* otherwise if fallback is enabled */
        cputs (sim_error_text (SCPE_INCOMP));           /*   then report that */
        cputc ('\n');                                   /*     the command did not complete */
        return SCPE_OK;                                 /*       but return success */
        }

    else                                                /* otherwise */
        return SCPE_INCOMP;                             /*   report that the command did not complete */
    }
}


/* Detach the interconnecting cables.

   This routine disconnects the IPL device pair from the shared memory region.
   This simulates disconnecting the processor interconnect kit cables from the
   card pair in this CPU.  The command is:

     DETACH IPL

   For backward compatibility with prior IPL implementations that used network
   interconnections, the following commands are also accepted:

     DETACH IPLI
     DETACH IPLO

   In either case, the shared memory region and process synchronization events
   are destroyed, and the card state pointers are reset to point at the local
   memory structure.  If a single ATTACH was done, a single DETACH will detach
   both devices and free the allocated "file name" memory.  The input poll and
   synchronizer are also stopped.

   If the event destruction failed, the routine returns "Command not completed"
   status to indicate that the error code register should be checked.


   Implementation notes:

    1. Deallocation of the shared memory region and destruction of the
       synchronization events occur only when the second of the two simulator
       instances detaches, i.e., when there are no more attached processes
       remaining.

    2. The attached "file name" of both units point to the same memory
       allocation, so only one deallocation is required.
*/

static t_stat ipl_detach (UNIT *uptr)
{
IO_STATE_PTR isp = local_region.dev_bus;                /* a pointer to the local bus state */
size_t       last_index;
UNIT         *optr;

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    if (sim_switches & SIM_SW_REST)                     /*   then if this is a restoration call */
        return SCPE_OK;                                 /*     then return success */
    else                                                /*   otherwise this is a manual request */
        return SCPE_UNATT;                              /*     so complain that the unit is not attached */

if (poll_unit.filename == sync_unit.filename) {         /* if both units are attached to the same object */
    if (uptr == &poll_unit)                             /*   then if we are detaching the input unit */
        optr = &sync_unit;                              /*     then point at the output unit */
    else                                                /*   otherwise we are detaching the output unit */
        optr = &poll_unit;                              /*     so point at the input unit */

    optr->filename = NULL;                              /* clear the other unit's attached object name */

    optr->flags &= ~UNIT_ATT;                           /* clear the other unit's attached flag */
    optr->ID = 0;                                       /*   and the ID number */
    }

free (uptr->filename);                                  /* free the memory holding the ID number */
uptr->filename = NULL;                                  /*   and clear the attached object name */

uptr->flags &= ~UNIT_ATT;                               /* clear the unit attached flag */
uptr->ID = 0;                                           /*   and the ID number */

sim_cancel (&poll_unit);                                /* cancel the poll */
sim_cancel (&sync_unit);                                /*   and the synchronizer */

sync_unit.wait = 0;                                     /* enter asynchronous mode */

io_ptrs [ipli].output->cable_connected = FALSE;         /* disconnect the cables */
io_ptrs [iplo].output->cable_connected = FALSE;         /*   from both cards */

io_ptrs [ipli].input  = &isp [ipli].forward.input;      /* restore local control */
io_ptrs [ipli].output = &isp [ipli].forward.output;     /*   over the I/O state */
io_ptrs [iplo].input  = &isp [iplo].forward.input;      /*     for both cards */
io_ptrs [iplo].output = &isp [iplo].forward.output;

if (shared_ptr != NULL) {                               /* if shared memory has been allocated */
    ipl_set_sync (uptr, Interlock, "0", NULL);          /*   then disable interlocking and release any waits */

    sim_shmem_close (shared_id);                        /* deallocate the shared memory region */
    shared_ptr = NULL;                                  /*   and clear the region pointer */
    }

last_index = strlen (event_name) - 1;                   /* get the index of the event digit */

event_name [last_index] = '1';
event_error = destroy_event (event_name, &sync_id);     /* destroy the first event */

if (event_error == 0) {                                 /* if destruction succeeded */
    event_name [last_index]++;                          /*   then increment the event digit */
    event_error = destroy_event (event_name, &lock_id); /*     and destroy the second event */
    }

if (event_error == 0) {                                     /* if destruction succeeded */
    event_name [last_index]++;                              /*   then increment the event digit */
    event_error = destroy_event (event_name, &unlock_id);   /*     and destroy the second event */
    }

if (event_error == 0)                                   /* if the destruction succeeded */
    return SCPE_OK;                                     /*   then report success */
else                                                    /* otherwise */
    return SCPE_INCOMP;                                 /*   report that the command did not complete */
}


/* Processor interconnect bootstrap loaders (special BBL and 12992K).

   The special Basic Binary Loader (BBL) used by the 2000 Access system loads
   absolute binary programs into memory from either the processor interconnect
   interface or the paper tape reader interface.  Two program entry points are
   provided.  Starting the loader at address x7700 loads from the processor
   interconnect, while starting at address x7750 loads from the paper tape
   reader.  The S register setting does not affect loader operation.

   For a 2100/14/15/16 CPU, entering a LOAD IPLI or BOOT IPLI command loads the
   special BBL into memory and executes the processor interconnect portion
   starting at x7700.  Loader execution ends with one of the following halt
   instructions:

     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 55 - the program load address would overlay the loader.
     * HLT 77 - the end of input with successful read; A = the paper tape select
                code, B = the processor interconnect select code.

   The 12992K boot loader ROM reads an absolute program from the processor
   interconnect or paper tape interfaces into memory.  The S register setting
   does not affect loader operation.  Loader execution ends with one of the
   following halt instructions:

     * HLT 11 - a checksum error occurred; A/B = the calculated/tape value.
     * HLT 55 - the program load address would overlay the ROM loader.
     * HLT 77 - the end of tape was reached with a successful read.


   Implementation notes:

    1. After the BMDL has been loaded into memory, the paper tape portion may be
       executed manually by setting the P register to the starting address
       (x7750).

    2. For compatibility with the "cpu_copy_loader" routine, the BBL device I/O
       instructions address select code 10.

    3. For 2000B, C, and F versions that use dual CPUs, the I/O Processor is
       loaded with the standard BBL configured for the select codes of the
       processor interconnect interface.  2000 Access must use the special BBL
       because the paper tape reader is connected to the IOP in this version; in
       prior versions, it was connected to the System Processor and could use
       the paper-tape portion of the BMDL that was installed in the SP.
*/

static const LOADER_ARRAY ipl_loaders = {
    {                               /* HP 21xx 2000/Access special Basic Binary Loader */
      000,                          /*   loader starting index */
      IBL_NA,                       /*   DMA index */
      073,                          /*   FWA index */
      { 0163774,                    /*   77700:  PI    LDA 77774,I               Processor Interconnect start */
        0027751,                    /*   77701:        JMP 77751                 */
        0107700,                    /*   77702:  START CLC 0,C                   */
        0002702,                    /*   77703:        CLA,CCE,SZA               */
        0063772,                    /*   77704:        LDA 77772                 */
        0002307,                    /*   77705:        CCE,INA,SZA,RSS           */
        0027760,                    /*   77706:        JMP 77760                 */
        0017736,                    /*   77707:        JSB 77736                 */
        0007307,                    /*   77710:        CMB,CCE,INB,SZB,RSS       */
        0027705,                    /*   77711:        JMP 77705                 */
        0077770,                    /*   77712:        STB 77770                 */
        0017736,                    /*   77713:        JSB 77736                 */
        0017736,                    /*   77714:        JSB 77736                 */
        0074000,                    /*   77715:        STB 0                     */
        0077771,                    /*   77716:        STB 77771                 */
        0067771,                    /*   77717:        LDB 77771                 */
        0047773,                    /*   77720:        ADB 77773                 */
        0002040,                    /*   77721:        SEZ                       */
        0102055,                    /*   77722:        HLT 55                    */
        0017736,                    /*   77723:        JSB 77736                 */
        0040001,                    /*   77724:        ADA 1                     */
        0177771,                    /*   77725:        STB 77771,I               */
        0037771,                    /*   77726:        ISZ 77771                 */
        0000040,                    /*   77727:        CLE                       */
        0037770,                    /*   77730:        ISZ 77770                 */
        0027717,                    /*   77731:        JMP 77717                 */
        0017736,                    /*   77732:        JSB 77736                 */
        0054000,                    /*   77733:        CPB 0                     */
        0027704,                    /*   77734:        JMP 77704                 */
        0102011,                    /*   77735:        HLT 11                    */
        0000000,                    /*   77736:        NOP                       */
        0006600,                    /*   77737:        CLB,CME                   */
        0103700,                    /*   77740:        STC 0,C                   */
        0102300,                    /*   77741:        SFS 0                     */
        0027741,                    /*   77742:        JMP 77741                 */
        0106400,                    /*   77743:        MIB 0                     */
        0002041,                    /*   77744:        SEZ,RSS                   */
        0127736,                    /*   77745:        JMP 77736,I               */
        0005767,                    /*   77746:        BLF,CLE,BLF               */
        0027740,                    /*   77747:        JMP 77740                 */
        0163775,                    /*   77750:  PTAPE LDA 77775,I               Paper tape start */
        0043765,                    /*   77751:  CONFG ADA 77765                 */
        0073741,                    /*   77752:        STA 77741                 */
        0043766,                    /*   77753:        ADA 77766                 */
        0073740,                    /*   77754:        STA 77740                 */
        0043767,                    /*   77755:        ADA 77767                 */
        0073743,                    /*   77756:        STA 77743                 */
        0027702,                    /*   77757:  EOT   JMP 77702                 */
        0063777,                    /*   77760:        LDA 77777                 */
        0067776,                    /*   77761:        LDB 77776                 */
        0102077,                    /*   77762:        HLT 77                    */
        0027702,                    /*   77763:        JMP 77702                 */
        0000000,                    /*   77764:        NOP                       */
        0102300,                    /*   77765:        SFS 0                     */
        0001400,                    /*   77766:        OCT 1400                  */
        0002500,                    /*   77767:        OCT 2500                  */
        0000000,                    /*   77770:        OCT 0                     */
        0000000,                    /*   77771:        OCT 0                     */
        0177746,                    /*   77772:        DEC -26                   */
        0100100,                    /*   77773:        ABS -PI                   */
        0077776,                    /*   77774:        DEF *+2                   */
        0077777,                    /*   77775:        DEF *+2                   */
        0000010,                    /*   77776:  PISC  OCT 10                    */
        0000010 } },                /*   77777:  PTRSC OCT 10                    */

    {                               /* HP 1000 Loader ROM (12992K) */
      IBL_START,                    /*   loader starting index */
      IBL_DMA,                      /*   DMA index */
      IBL_FWA,                      /*   FWA index */
      { 0107700,                    /*   77700:  ST    CLC 0,C            ; intr off */
        0002401,                    /*   77701:        CLA,RSS            ; skip in */
        0063756,                    /*   77702:  CN    LDA M11            ; feed frame */
        0006700,                    /*   77703:        CLB,CCE            ; set E to rd byte */
        0017742,                    /*   77704:        JSB READ           ; get #char */
        0007306,                    /*   77705:        CMB,CCE,INB,SZB    ; 2's comp */
        0027713,                    /*   77706:        JMP *+5            ; non-zero byte */
        0002006,                    /*   77707:        INA,SZA            ; feed frame ctr */
        0027703,                    /*   77710:        JMP *-3            */
        0102077,                    /*   77711:        HLT 77B            ; stop */
        0027700,                    /*   77712:        JMP ST             ; next */
        0077754,                    /*   77713:        STA WC             ; word in rec */
        0017742,                    /*   77714:        JSB READ           ; get feed frame */
        0017742,                    /*   77715:        JSB READ           ; get address */
        0074000,                    /*   77716:        STB 0              ; init csum */
        0077755,                    /*   77717:        STB AD             ; save addr */
        0067755,                    /*   77720:  CK    LDB AD             ; check addr */
        0047777,                    /*   77721:        ADB MAXAD          ; below loader */
        0002040,                    /*   77722:        SEZ                ; E =0 => OK */
        0027740,                    /*   77723:        JMP H55            */
        0017742,                    /*   77724:        JSB READ           ; get word */
        0040001,                    /*   77725:        ADA 1              ; cont checksum */
        0177755,                    /*   77726:        STA AD,I           ; store word */
        0037755,                    /*   77727:        ISZ AD             */
        0000040,                    /*   77730:        CLE                ; force wd read */
        0037754,                    /*   77731:        ISZ WC             ; block done? */
        0027720,                    /*   77732:        JMP CK             ; no */
        0017742,                    /*   77733:        JSB READ           ; get checksum */
        0054000,                    /*   77734:        CPB 0              ; ok? */
        0027702,                    /*   77735:        JMP CN             ; next block */
        0102011,                    /*   77736:        HLT 11             ; bad csum */
        0027700,                    /*   77737:        JMP ST             ; next */
        0102055,                    /*   77740:  H55   HLT 55             ; bad address */
        0027700,                    /*   77741:        JMP ST             ; next */
        0000000,                    /*   77742:  RD    NOP                */
        0006600,                    /*   77743:        CLB,CME            ; E reg byte ptr */
        0103710,                    /*   77744:        STC RDR,C          ; start reader */
        0102310,                    /*   77745:        SFS RDR            ; wait */
        0027745,                    /*   77746:        JMP *-1            */
        0106410,                    /*   77747:        MIB RDR            ; get byte */
        0002041,                    /*   77750:        SEZ,RSS            ; E set? */
        0127742,                    /*   77751:        JMP RD,I           ; no, done */
        0005767,                    /*   77752:        BLF,CLE,BLF        ; shift byte */
        0027744,                    /*   77753:        JMP RD+2           ; again */
        0000000,                    /*   77754:  WC    000000             ; word count */
        0000000,                    /*   77755:  AD    000000             ; address */
        0177765,                    /*   77756:  M11   DEC -11            ; feed count */
        0000000,                    /*   77757:        NOP                */
        0000000,                    /*   77760:        NOP                */
        0000000,                    /*   77761:        NOP                */
        0000000,                    /*   77762:        NOP                */
        0000000,                    /*   77763:        NOP                */
        0000000,                    /*   77764:        NOP                */
        0000000,                    /*   77765:        NOP                */
        0000000,                    /*   77766:        NOP                */
        0000000,                    /*   77767:        NOP                */
        0000000,                    /*   77770:        NOP                */
        0000000,                    /*   77771:        NOP                */
        0000000,                    /*   77772:        NOP                */
        0000000,                    /*   77773:        NOP                */
        0000000,                    /*   77774:        NOP                */
        0000000,                    /*   77775:        NOP                */
        0000000,                    /*   77776:        NOP                */
        0100100 } }                 /*   77777:  MAXAD ABS -ST            ; max addr */
    };


/* Device boot routine.

   This routine is called directly by the BOOT IPLI and LOAD IPLI commands to
   copy the device bootstrap into the upper 64 words of the logical address
   space.  It is also called indirectly by a BOOT CPU or LOAD CPU command when
   the specified HP 1000 loader ROM socket contains a 12992K ROM.

   When called in response to a BOOT IPLI or LOAD IPLI command, the "unitno"
   parameter indicates the unit number specified in the BOOT command or is zero
   for the LOAD command, and "dptr" points at the IPLI device structure.
   Depending on the current CPU model, the special BBL or 12992K loader ROM will
   be copied into memory and configured for the IPLI select code.  If the CPU is
   a 1000, the S register will be set as it would be by the front-panel
   microcode.

   When called for a BOOT/LOAD CPU command, the "unitno" parameter indicates the
   select code to be used for configuration, and "dptr" will be NULL.  As above,
   the special BBL or 12992K loader ROM will be copied into memory and
   configured for the specified select code.  The S register is assumed to be
   set correctly on entry and is not modified.

   For the 12992K boot loader ROM, the S register will be set as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | 0   0 |   IPLI select code    | 0   0   0   0   0   0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

static t_stat ipl_boot (int32 unitno, DEVICE *dptr)
{
static const HP_WORD ipl_ptx = 074u;                    /* the index of the pointer to the IPL select code */
static const HP_WORD ptr_ptx = 075u;                    /* the index of the pointer to the PTR select code */
static const HP_WORD ipl_scx = 076u;                    /* the index of the IPL select code */
static const HP_WORD ptr_scx = 077u;                    /* the index of the PTR select code */
uint32 start;
uint32 ptr_sc, ipl_sc;
DEVICE *ptr_dptr;

ptr_dptr = find_dev ("PTR");                            /* get a pointer to the paper tape reader device */

if (ptr_dptr == NULL)                                   /* if the paper tape device is not present */
    return SCPE_IERR;                                   /*   then something is seriously wrong */
else                                                    /* otherwise */
    ptr_sc = ((DIB *) ptr_dptr->ctxt)->select_code;     /*   get the select code from the device's DIB */

if (dptr == NULL)                                       /* if we are being called for a BOOT/LOAD CPU command */
    ipl_sc = (uint32) unitno;                           /*   then get the select code from the "unitno" parameter */
else                                                    /* otherwise */
    ipl_sc = ipli_dib.select_code;                      /*   use the device select code from the DIB */

start = cpu_copy_loader (ipl_loaders, ipl_sc,           /* copy the boot loader to memory */
                         IBL_S_NOCLEAR, IBL_S_NOSET);   /*   but do not alter the S register */

if (start == 0)                                         /* if the copy failed */
    return SCPE_NOFNC;                                  /*   then reject the command */

else {                                                                  /* otherwise */
    if (mem_examine (start + ptr_scx) <= SC_MAX) {                      /*   if this is the special BBL */
        mem_deposit (start + ipl_ptx, (HP_WORD) start + ipl_scx);       /*     then configure */
        mem_deposit (start + ptr_ptx, (HP_WORD) start + ptr_scx);       /*       the pointers */
        mem_deposit (start + ipl_scx, (HP_WORD) ipli_dib.select_code);  /*         and select codes */
        mem_deposit (start + ptr_scx, (HP_WORD) ptr_sc);                /*           for the loader */
        }

    return SCPE_OK;                                     /* the boot loader was successfully copied */
    }
}



/* IPL device local utility routines */



/* Processor Interconnect service routine.

   This routine is scheduled when the IPL is attached or an ioSTC signal is
   received and is entered to check the Device Flag signals of the two interface
   cards. The order in which the cards are checked is significant, as a prior
   command sent via the output card must be acknowledged before a command from
   the input card is recognized.

   When a card's Device Flag signal is asserted, the routine saves the input
   data present on the Data In bus and then sets the Flag Buffer flip-flop.  It
   then asserts the ENF signal to set the Flag flip-flop and then denies the
   Device Command Out signal.  If the output card is responding, the next poll
   time is lengthened to ensure that the card status is read before checking the
   input card for a command.  Otherwise, the current poll time is reused.

   If a flag signal was not seen, the current poll time is doubled, with upper
   limits set by the interlock count and "poll_wait" setting.  The poll is then
   rescheduled if the cards are attached.  Otherwise, the service call was for a
   diagnostic test, where periodic polling is not done.


   Implementation notes:

    1. The "uptr" parameter always points at the input card unit; the output
       card unit has no separate service routine, as it is serviced here
       concurrently with the input unit.

    2. For rapid response during block data transfers, the poll wait wants to be
       as short as possible.  However, for reduced overhead, the poll wait wants
       to be as long as possible.  We solve this conflict by using an adaptable
       poll wait.  It starts at a delay of one event tick and doubles each time
       a poll does not encounter an input condition until it reaches a preset
       maximum limit.  This gives good response to continuous transfers but
       reduces the overhead between transfers.

    3. If the IPL is operating synchronously, the poll wait is limited to
       one-half of the interlock time.  This ensures that a pending event will
       be seen and processing begun within one execution quantum of its
       occurrence.  That is, if an instance transmits a word, the other instance
       will receive and begin processing that word no later than 1.5 quanta
       after transmission.

    4. The CARD_INDEX enumeration is (ipli, iplo), so to loop in the reverse
       order, we must cast the terminating condition to an "int" comparison, as
       the integer type used to represent enumerations is implementation-
       dependent.  In particular, it may be an unsigned type.

    5. The routine may be entered when the unit is not attached to manipulate
       the local card stateinstead of the remote card on the other end of the
       interonnecting cable.  In this case, the shared memory region will not
       exist, so the associated pointer must be validated befure use.

    6. The other instance may have changed the interlock count without our
       knowledge.  Therefore, we check the interlock condition on entry and
       schedule the synchronization unit if interlocking has been enabled.  If
       interlocking is disabled, we clear the synchronization unit's wait time
       to indicate asynchronous operation (the unit will be inactivated after
       its next entry).

    7. 2000 Access has a race condition that causes a user program's printer or
       paper tape punch output to stop for no apparent reason.  The race occurs
       more often when a large amount of output is generated quickly.

       The cause is this code in subroutine #IPAL in the SP main program source
       (STSB) that is used to send output data to a non-shareable device
       controlled by the IOP:

         LDA ERTMP     SEND
         IOR ALB         REQUEST
         JSB SDVRP,I     CODE
         LDB #IPAL     RETRIEVE
         INB             BUFFER
         LDA B,I           LENGTH
         JSB SDVRP,I   SEND TO IOP
         SFS CH2       WAIT FOR
         JMP *-1         ACKNOWLEDGEMENT
         CLF 0         INHIBIT INTERRUPTS
         LIA CH2       RETRIEVE RESPONSE

       If the Allocate Buffer ("ALB") request is refused by the IOP because all
       output buffers are in use, the resulting "no buffer available" response
       will cause the SP to issue a Release Buffers command to the IOP and then
       suspend the user's program.  When the line printer completes its
       operation on the current output buffer, the IOP releases it and then
       indicates buffer availability by sending a Wake Up User command to the SP
       to retry the allocation request.

       The problem occurs when the line printer finishes a line just as the
       Allocate Buffer request is made.  That request sends two words: the ALB
       request code and the buffer length.  After receiving the second word, the
       IOP finds all buffers are in use and denies the request.  If the line
       printer completion then arrives, the IOP immediately sends a Wake Up User
       command to indicate that a buffer is now available.  If the command
       arrives between the time the SP sends the buffer length word and the time
       it retrieves the response, the user program will hang.  This occurs
       because the "no buffer available" response subsequently causes the SP to
       set a flag to indicate that the user has been suspended for buffer
       availability.  If the Wake Up User command arrives before that flag is
       set, the command is ignored.  So the SP is waiting for the IOP to issue
       the command, but the IOP has already issued it.  This leaves the
       suspension in force until the user aborts the program by pressing the
       BREAK key.

       Nominally, there are only about nine instructions executed between the
       STC CH2 (within the SDVRP subroutine) that causes an IOP to accept the
       buffer length word and the SFS CH2 (above) that detects that the IOP's
       acknowledgement has arrived.  Once detected, the CLF 0 instruction turns
       the interrupt system off so that commands received from the IOP are
       deferred until after the user suspension flag is set.

       However, the SP's interrupt system is on during the above SFS CH2/JMP *-1
       loop, and the Processor Interconnect has the highest I/O interrupt
       priority.  So if, say, a time-base generator interrupt occurs during the
       SFS loop, several dozen instructions may be executed before control
       returns to the loop.  If, during that time, the IOP command arrives, the
       resulting higher-priority interrupt is handled before the loop return,
       and the command is ignored because the "user is suspended" flag has not
       been set.

       We work around this problem by arranging the card service routine to
       ensure that the IOP status response is picked up before an IOP command if
       they both are seen during the same input poll.  However, there is no
       general way to ensure that the response is processed by the SP program
       before a pending IOP command is recognized.  That is because the SP does
       not read the responses of some commands it sends, so simply holding off
       input card commands until the output card data register is read will not
       work.

       The best we can do to reduce the frequency of the race condition is to
       delay IOP command recognition after a status response arrives.  We do
       this by rescheduling the poll using a delay of ten times the normal
       maximum poll delay to give any intervening interrupt handlers time to
       complete.  The next STC directed to either card clears the delay and
       reschedules the poll for immediate entry, providing rapid response when
       block data is being transferred in either direction across the Processor
       Interconnect.
*/

static t_stat card_service (UNIT *uptr)
{
static uint32 delta = 0;                                /* accumulated time between receptions */
CARD_INDEX card;
t_stat status = SCPE_OK;

tprintf (ipli_dev, TRACE_PSERV, "Poll delay %d service entered\n",
         uptr->wait);

if (shared_ptr != NULL)                                 /* if the shared memory region exists */
    if (shared_ptr->count == 0)                         /*   then if interlocking is disabled */
        sync_unit.wait = 0;                             /*     then ensure that we are in asynchronous mode */

    else if (sync_unit.wait == 0)                       /*   otherwise if we should be interlocked but are not */
        activate_unit (&sync_unit, 1);                  /*     then activate the synchronizer immediately */

delta = delta + uptr->wait;                             /* update the accumulated time */

for (card = iplo; (int) card >= (int) ipli; card--)     /* process IPLO then IPLI in descending order */
    if (io_ptrs [card].input->device_flag_in == TRUE) { /* if the Device Flag is asserted */
        io_ptrs [card].input->device_flag_in = FALSE;   /*   then clear it */

        ipl [card].input_word = io_ptrs [card].input->data_in;  /* read the data input lines */

        tpprintf (dptrs [card], TRACE_XFER, "Word %06o delta %u received from link\n",
                  ipl [card].input_word, delta);

        ipl [card].flag_buffer = SET;                   /* set the flag buffer */
        io_assert (dptrs [card], ioa_ENF);              /*   and flag flip-flops */

        io_ptrs [card].output->device_command_out = FALSE;  /* reset Device Command */

        delta = 0;                                      /* clear the accumulated time */

        if (card == iplo) {                             /* if the output card received a status reply */
            uptr->wait = poll_wait * 10;                /*   then schedule a longer wait to allow for status pickup */
            break;                                      /*     before checking for an inbound command */
            }
        }

if (delta > 0) {                                        /* if both Device Flags were denied */
    uptr->wait = uptr-> wait * 2;                       /*   then double the wait time for the next check */

    if (sync_unit.wait > 0                              /* if operating synchronously and the poll wait */
     && uptr->wait >= (int32) shared_ptr->count / 2)    /*   is longer than half an execution quantum */
        uptr->wait = (int32) shared_ptr->count / 2;     /*     then shorten it to guarantee a response */

    if (uptr->wait > poll_wait)                         /* if the new time is greater than the maximum time */
        uptr->wait = poll_wait;                         /*   then limit it to the maximum */

    if (io_ptrs [ipli].input->cable_connected == FALSE  /* if the interconnecting cable is not present */
      && cpu_io_stop (uptr))                            /*   and the I/O error stop is enabled */
        status = STOP_NOCONN;                           /*     then report the disconnection */
    }

if (uptr->flags & UNIT_ATT)                             /* if the link is active */
    activate_unit (uptr, uptr->wait);                   /*   then continue to poll for input */

return status;                                          /* return the event service status */
}


/* Simulator interlock service routine.

   This routine is scheduled when instruction interlocking between two simulator
   instance is desired.  The service time is the count of instructions to be
   executed between rendezvous attempts.  When interlocking is disabled,
   servicing is canceled.

   On entry, the unit is reactivated if interlocking is still enabled.  Then the
   gate in the shared memory area is locked with an atomic operation.  If the
   gate was unlocked at the time, this instance  waits until the other instance
   unlocks the gate.  Then the unlocking is acknowledged, releasing the other
   instance to continue execution.  The service routine then returns to allow
   another set of instructions to be executed before the next interlock check.

   If the gate was locked at the time of the check, the other instance is
   released from its lock loop.  Then the routine waits until the other instance
   has confirmed that it has dropped out of its wait loop.  This prevents the
   situation of this instance unlocking and then reentering and locking the gate
   while the other instance has been preempted in its wait loop, which leads to
   deadlock, as both instances are waiting at the locked gate.


   Implementation notes:

    1. Interlocking may be canceled by the other instance while our service
       routine is scheduled.  So we may be entered with the interlock count
       equal to zero.  If this occurs, the routine exits without rescheduling.
*/

static t_stat sync_service (UNIT *uptr)
{
t_stat status;
volatile GATE_STATE *gate_ptr = &shared_ptr->gate;      /* must be volatile to ensure an updated value */

tprintf (iplo_dev, TRACE_PSERV, "Synchronizer delay %d service entered with gate %s\n",
         uptr->wait, gate_state_names [*gate_ptr]);

if (shared_ptr->count == 0) {                           /* if synchronization was canceled */
    sync_unit.wait = 0;                                 /*   then enter asynchronous mode */

    tprintf (iplo_dev, TRACE_PSERV, "Synchronizer stopped\n");

    return SCPE_OK;                                     /* return immediately */
    }

else                                                    /* otherwise */
    activate_unit (uptr, shared_ptr->count);            /*   reactivate for the next cycle */

if (sim_shmem_atomic_cas ((int32 *) &shared_ptr->gate,  /* if the gate is unlocked */
                          Unlocked, Locked)) {          /*   then lock it and continue */

    tprintf (iplo_dev, TRACE_PSERV, "Synchronizer locked\n");

    status = wait_at_gate (lock_id, Locked, Unlocking); /* wait until the gate is unlocked */
    release_wait (unlock_id, Unlocking, Unlocked);      /*  and then acknowledge the unlock */
    }

else {                                                  /* otherwise the gate was locked on entry */
    tprintf (iplo_dev, TRACE_PSERV, "Synchronizer unlocking\n");

    release_wait (lock_id, Locked, Unlocking);              /* unlock the other instance */
    status = wait_at_gate (unlock_id, Unlocking, Unlocked); /*   and wait until the unlock is acknowledged */
    }

return status;                                          /* return the service status */
}


/* Wait until a release event occurs.

   This routine is called to suspend execution of the current instance until the
   other instance signals that it is to continue.  If, when the routine is
   called, the gate is still in the state specified by the "initial" parameter,
   execution enters a compute-bound loop to wait for the state to change.  This
   permits a speedy exit as soon as the gate value changes.  However, if the
   change does not occur within a specified number of iterations, the loop is
   changed to a timed event wait.  This permits the instance to yield processor
   time, which is especially important if the other iteration is competing for
   the same processor.

   If the wait times out, the keyboard is polled for a CTRL+E.  If one is seen,
   the user has aborted the wait, and the simulator is stopped.  If any other
   key is pressed, or no key is pressed, the loop continues until a signal is
   received from the other instance.

   If the loop terminates due to the gate status changing, the loop statistics
   are updated, and the routine returns SCPE_OK.  If the loop was aborted, then
   the gate state is checked.  If the abort occurred after the loop transitioned
   to event waiting but before the other instance had a chance to signal, then
   no cleanup action is required, and the gate is reset to the "Unlocked" state.
   However, if the other instance has signaled (or is about to) by changing the
   gate to the "final" parameter state value, then an event wait is performed to
   absorb the signal.  This wait will always occur, so a wait timeout is not
   used.

   An abort may occur for four reasons.  First, the user may abort with CTRL+E.
   Second, an event error may occur.  Third, the other instance may terminate
   synchronous operation by executing a SET IPL INTERLOCK=0 command.   Fourth,
   the other instance may disconnect its cables (by detaching the device).
   After cleaning up as necessary, the reason for the abort is returned.


   Implementation notes:

    1. The gate state must include an indication of whether the wait has
       transitioned from a compute-bound loop to a timed-wait loop, so that the
       other instance knows whether to signal the event as well as changing the
       gate state.  This is done by incrementing the initial gate state value.
       The state values are arranged so that the event-wait states are single
       increments of the possible initial values (e.g., the "Locked_Wait" state
       value is one more than the "Locked" state value).

    2. The gate must be declared "volatile" to ensure that it is reloaded for
       each iteration of the wait loop.  We cannot declare the variable itself
       volatile, as the atomic compare-and-swap routine takes a non-volatile
       variable as a parameter.

    3. Detaching the cable also unlocks the gate, so no special test for a
       disconnected cable is required.

    4. It would be nice to call "sim_poll_kbd" to poll for a user abort so that
       a CTRL+E produces the "scp>" prompt, rather than aborting execution in
       addition to the wait.  However, that routine supplies REPLY strings, and
       so a wait with a pending REPLY will lose characters.  So we have to use
       "sim_os_poll_kbd" which does not affect REPLYs but also causes an
       immediate simulator stop.
*/

static t_stat wait_at_gate (EVENT event_id, GATE_STATE initial, GATE_STATE final)
{
const uint32 wait_limit = 2000;                 /* the count at which to shift to event waiting */
const uint32 wait_time  = 100;                  /* the event wait time in milliseconds */
t_bool signaled   = FALSE;
uint32 iterations = 0;
t_stat status     = SCPE_OK;
volatile GATE_STATE *gate_ptr = &shared_ptr->gate;      /* must be volatile to ensure an updated value */

while (*gate_ptr == initial) {                          /* while waiting for the gate to leave the initial state */
    iterations = iterations + 1;                        /*   increment the iteration counter */

    if (iterations == wait_limit                            /* if the wait limit has been reached */
      && sim_shmem_atomic_cas ((int32 *) &shared_ptr->gate, /*   and changing from the initial */
                               initial, initial + 1))       /*     to the waiting state succeeds */
        do {                                                /*       then wait in a loop */
            event_error = wait_event (event_id, wait_time,  /*         for the event */
                                      &signaled);           /*           to be signaled */

            if (event_error)                        /* if the wait failed */
                status = SCPE_IERR;                 /*   then indicate an internal error */

            else if (signaled == FALSE) {           /* otherwise if the wait timed out */
                iterations = iterations + 1;        /*   then increment the iteration counter */
                status = sim_os_poll_kbd ();        /*     and check the keyboard for a user stop */

                if (status >= SCPE_KFLAG)           /* if a key was pressed */
                    status = SCPE_OK;               /*   then ignore it */
                else if (stop_cpu)                  /* otherwise if a signal was received */
                    status = SCPE_STOP;             /*   then stop the wait */
                }
            }
        while (! signaled && status == SCPE_OK);    /* loop if not signaled and no error */
    }

tprintf (iplo_dev, TRACE_STATE, "Synchronizer %s with gate %s\n",
         (iterations <= wait_limit ? "resumed"
                                   : (signaled ? "signaled" : "aborted")),
         gate_state_names [*gate_ptr]);

if (status == SCPE_OK) {                            /* if the gate is transitioning */
    if (iterations > sync_max)                      /*   then reset the maximum iteration count */
        sync_max = iterations;                      /*     if this pass was larger */

    sync_cnt = sync_cnt + 1;                        /* increment the pass counter */

    sync_mean = sync_mean                           /* calculate the running */
      + ((float) iterations - sync_avg) / sync_cnt; /*   average wait time */

    sync_avg = (uint32) sync_mean;                  /* round it and save for inspection */
    }

else if (sim_shmem_atomic_cas ((int32 *) &shared_ptr->gate,     /* otherwise the wait was aborted */
                               initial + 1, Unlocked) == FALSE  /*   and if not yet waiting for the event */
  && *gate_ptr == final)                                        /*     but the gate was transitioning */
    event_error = wait_event (event_id, INFINITE,               /*       then wait for the signal */
                              &signaled);                       /*         that must occur */

tprintf (iplo_dev, TRACE_STATE, "Synchronizer transitioning after %u iterations with gate %s\n",
         iterations, gate_state_names [*gate_ptr]);

return status;
}


/* Release the gate.

   This routine is called to release the other instance after it has arrived at
   the rendezvous point.  If the other instance is in its compute-bound loop, as
   indicated by the gate equalling the "initial" parameter state value, then the
   gate is simply set to the "final" parameter state value.  However, if it is
   in its event-wait loop, indicated by the gate equalling the state after the
   initial state, then in addition the event specified by the "event_id"
   parameter is signaled.  When the routine returns, the rendezvous is complete,
   and both instances continue to execute machine instructions.


   Implementation notes:

    1. While the gate state transitions are implemented by atomic operations,
       there is a potential race point between the change from the "initial with
       wait" state to the "final" state and the signaling of the event.  If the
       wait loop in the other instance aborts at that point, the gate state will
       indicate that a signal is coming, but it has not actually occurred yet.

       To address this, the "wait_at_gate" routine always waits for the signal
       in this case, so that the signal is absorbed, regardless of whether it
       occurs before or after the wait call.
*/

static void release_wait (EVENT event_id, GATE_STATE initial, GATE_STATE final)
{
volatile GATE_STATE *gate_ptr = &shared_ptr->gate;      /* must be volatile to ensure an updated value */

if (sim_shmem_atomic_cas ((int32 *) &shared_ptr->gate,      /* if the other process */
                          initial, final) == FALSE)         /*   is waiting */
    if (sim_shmem_atomic_cas ((int32 *) &shared_ptr->gate,  /*     for the release event */
                              initial + 1, final)) {        /*       to occur before proceeding */
        event_error = signal_event (event_id);              /*         then signal the event */

        tprintf (iplo_dev, TRACE_STATE, "Synchronizer signaling the release event with gate %s\n",
                 gate_state_names [*gate_ptr]);
        }

    else {                                              /* otherwise the gate state is unexpected */
        *gate_ptr = final;                              /*   so transition immediately to the final state */

        tprintf (iplo_dev, TRACE_STATE, "Synchronizer gate should be %s or %s but is %s\n",
                 gate_state_names [initial], gate_state_names [initial + 1],
                 gate_state_names [*gate_ptr]);
        }

tprintf (iplo_dev, TRACE_STATE, "Synchronizer releasing with gate %s\n",
         gate_state_names [*gate_ptr]);

return;
}


/* Activate a unit.

   The specified unit is added to the event queue with the delay specified by
   the unit wait field.


   Implementation notes:

    1. This routine may be called with wait = 0, which will expire immediately
       and enter the service routine with the next sim_process_event call.
       Activation is required in this case to allow the service routine to
       return an error code to stop the simulation.  If the service routine was
       called directly, any returned error would be lost.
*/

static void activate_unit (UNIT *uptr, int32 wait_time)
{
static const char *unit_name [CARD_COUNT] = { "Poll", "Synchronizer" };
const CARD_INDEX card = (CARD_INDEX) (uptr == &sync_unit);  /* set card selector */

tpprintf (dptrs [card], TRACE_PSERV, "%s delay %u service scheduled\n",
          unit_name [card], wait_time);

uptr->wait = wait_time;                                 /* save the specified wait */
sim_activate (uptr, uptr->wait);                        /*   and activate the unit */

return;
}


/* Handler for the CTRL+E signal.

   This handler is installed while executing a SET IPL WAIT command.  It is
   called if the user presses CTRL+E on a UNIX host to abort the wait command.


   Implementation notes:

    1. On Windows hosts, SIGINT is bound to CTRL+C and cannot be remapped as it
       can under UNIX.  So CTRL+E is delivered through the keyboard poll as a
       normal character.  Moreover, in raw mode, CTRL+C is also delivered
       through the keyboard poll, and this handler is never called.
*/

static void wru_handler (int signal)
{
wait_aborted = TRUE;                                    /* the user has aborted the event wait */
return;
}


/* Trace a TSB command.

   This routine is called to decode and trace a command issued by the SP or IOP
   instance running HP 2000 Time-Shared BASIC.  It also traces command
   parameters sent on the same channel.

   On entry, the "card" parameter indicates which I/O card is handling the
   command, "command" contains the command or parameter word, and "response"
   contains the current response state.  If the response state is "None", then
   "command" represents a command word.  Otherwise, it represents a command
   parameter word sent after the command.

   For the command case, the "card" and "cpu_is_iop" values determine whether an
   SP command or an IOP command is being issued.  On the SP instance, SP
   commands are sent on the output card, and IOP commands are received on the
   input card.  On the IOP instance, the commands are reversed.

   The entry in the command descriptor table is determined initially by decoding
   the primary opcode.  If the command uses subopcodes (opcode = 7), the index
   is adjusted by decoding the subopcode and adding it and the table offset.  If
   the command comes from the IOP, the index is adjusted to the IOP section of
   the descriptor table.

   If the TSB version is not Access, the index is remapped to point at the
   corresponding 2000F descriptor entry.  Otherwise, if the Access opcode has an
   opcode extension field, then adjust the index by decoding the extension and
   adding it and the extension section offset.

   With the table index finalized, if the command name is undefined, the command
   is unused, and nothing is printed.  Otherwise, the mask value is examined to
   determine the size and alignment of the first and second operand fields.  If
   the second operand is not defined, the operand value is cleared in case the
   command contains extraneous bits.

   Finally, the decoded command and operands are printed.  The "Process output
   character" command (opcode 0) is the only one whose second operand is a
   character rather than a numeric value, so a separate print statement is used
   for this case.  For the other cases, inclusion or omission of the first and
   second operand labels and values is accomplished by changing the precision of
   the corresponding format specifications.  After printing, the next expected
   response is returned to permit any following command parameter or returned
   status values to be printed properly.

   Command parameters and DMA transfers are handled as directed by the
   "response" value.  DMA transfers continue until ended when the EDT signal
   arrives at the interface.  Entering the routine with an unexpected response
   produces an appropriate diagnostic before returning "None" to reset for the
   next expected command.


   Implementation notes:

    1. DMA character transfers must be masked to 7 bits.  Some transfers, such
       as that following a "Process output string" command to print the program
       name, include characters with the high bit set.  These are stripped off
       for ease of trace interpretation.
*/

static RESPONSE trace_command (CARD_INDEX card, HP_WORD command, RESPONSE response)
{
uint32 index, operand_1, operand_2;

switch (response) {                                     /* dispatch on the current response state */

    case None:                                          /* no prior response; command expected */
        index = CM_OPCODE (command);                    /* decode the primary opcode */

        if (index == SUBOP_OPCODE)                      /* if this command contains a subopcode */
            index = CM_SUBOP (command) + SUBOP_OFFSET;  /*   then decode it and offset to the proper section */

        if (cpu_is_iop ^ (card == ipli))                /* if an IOP command is expected */
            index = index + IOP_OFFSET;                 /*   then offset to the IOP section */

        if (shared_ptr->tsb_version == HP_2000F)        /* if running 2000F TSB */
            index = remap_2000F [index];                /*   then remap to the equivalent 2000F command */

        else if (index == SUBOP_OFFSET)                         /* otherwise if this is an extended command */
            index = index + CM_EXTOP (command) + EXTOP_OFFSET;  /*   then offset to the extension section */

        if (cmd [index].name != NULL) {                         /* if the entry is assigned */
            if (cmd [index].mask == 0176000) {                  /*   then if it has a 6-bit first operand */
                operand_1 = (command & cmd [index].mask) >> 7;  /*     then mask and shift it into place */
                operand_2 = command & 0177;                     /*       and mask the second operand to 7 bits */
                }

            else {                                              /* otherwise */
                operand_1 = (command & cmd [index].mask) >> 8;  /*   mask and shift the first operand into place */
                operand_2 = command & 0377;                     /*     and mask the second operand to 8 bits */
                }

            if (cmd [index].low_label [0] == '\0')      /* if there is no second operand */
                operand_2 = 0;                          /*   then clear the value of extraneous bits */

            if (index == 0)                                                 /* if this is the POC command */
                hp_trace (dptrs [card], TRACE_CMD, "%s command%s%u%s%s\n",  /*   then format a character operand */
                          cmd [index].name,
                          cmd [index].high_label,
                          operand_1,
                          cmd [index].low_label,
                          fmt_char (operand_2));
            else                                                        /* otherwise format a numeric operand */
                hp_trace (dptrs [card], TRACE_CMD, "%s command%s%.*u%s%.*u\n",
                          cmd [index].name,                             /* print the command name */
                          cmd [index].high_label,                       /* print the first operand label */
                          (cmd [index].high_label [0] != '\0'),         /*   and the operand value */
                          operand_1,                                    /*     if the label is not empty */
                          cmd [index].low_label,                        /* print the second operand label */
                          (cmd [index].low_label [0] != '\0'),          /*   and the operand value */
                          operand_2);                                   /*     if the label is not empty */
            }

        return cmd [index].response;


    case DMA_Octal:
        hp_trace (dptrs [card], TRACE_CMD, "DMA transfer %06o sent\n", command);
        return DMA_Octal;


    case DMA_Chars:
        hp_trace (dptrs [card], TRACE_CMD, "DMA transfer %06o (%s, %s) sent\n",
                  command,
                  fmt_char (UPPER_BYTE (command) & DATA_MASK),
                  fmt_char (LOWER_BYTE (command) & DATA_MASK));
        return DMA_Chars;


    case Dec_Status:
    case Dec_Stat_DMAC:
        hp_trace (dptrs [card], TRACE_CMD, "Sent data is %d\n", SEXT16 (command));

        if (response == Dec_Status)                     /* after printing a decimal value */
            return Status;                              /*   the next word will be status */
        else                                            /*     or status followed by */
            return Status_DMAC;                         /*       a DMA character transfer */


    case Octal_DMAB:
        hp_trace (dptrs [card], TRACE_CMD, "Sent data is %06o\n", command);
        return DMA_Octal;


    case Character:
    case Decimal:
    case Octal:
    case Status:
    case Status_DMAC:
    case Stat_Dec_DMAC:
    case Decimal_DMAC:
        hp_trace (dptrs [card], TRACE_CMD, "Unexpected data %06o sent\n", command);
        break;
    }                                                   /* all cases are handled */

return None;                                            /* return None here to catch the "others" case */
}


/* Trace a TSB status return.

   This routine is called to decode and trace status or data words returned by
   the SP or IOP instance running HP 2000 Time-Shared BASIC.

   On entry, the "card" parameter indicates which I/O card is returning the
   data, "status" contains the status or data word, and "response" contains the
   current response state.  If the response state is "None", then "status"
   represents an unexpected return.  Otherwise, it represents a status or data
   word sent in response to a command.

   Status, data words, and DMA transfers are handled as directed by the
   "response" value.  DMA transfers continue until ended when the EDT signal
   arrives at the interface.  Entering the routine with an unexpected response
   produces an appropriate diagnostic before returning "None" to reset for the
   next expected command.


   Implementation notes:

    1. The Access commands Echo On and Echo Off return the updated receive
       parameter with the echo bit added or removed from the IOP to the SP.
       This is contrary to the "Internal Maintenance Specifications" section of
       the "HP 2000 Computer System Sources and Listings Documentation"
       (22687-90020), which makes no mention of return values for these
       commands.  The SP ignores the values, but tracing will print "Unexpected
       value returned" for these commands.

    2. DMA character transfers must be masked to 7 bits.  Some transfers, such
       as that following a "Process output string" command to print the program
       name, include characters with the high bit set.  These are stripped off
       for ease of trace interpretation.
*/

static RESPONSE trace_status (CARD_INDEX card, HP_WORD status, RESPONSE response)
{
int32 value;

switch (response) {                                     /* dispatch on the current response state */

    case Character:
        hp_trace (dptrs [card], TRACE_CMD, "Returned character is %s\n",
                  fmt_char ((uint8) status));
        break;


    case Decimal:
    case Decimal_DMAC:
        hp_trace (dptrs [card], TRACE_CMD, "Returned data is %d\n", SEXT16 (status));

        if (response == Decimal_DMAC)                   /* after printing a decimal value */
            return DMA_Chars;                           /*   a DMA character transfer follows */
        break;


    case Octal:
        hp_trace (dptrs [card], TRACE_CMD, "Returned data is %06o\n", status);
        break;


    case DMA_Octal:
        hp_trace (dptrs [card], TRACE_CMD, "DMA transfer %06o returned\n", status);
        return DMA_Octal;


    case DMA_Chars:
        hp_trace (dptrs [card], TRACE_CMD, "DMA transfer %06o (%s, %s) returned\n",
                  status,
                  fmt_char (UPPER_BYTE (status) & DATA_MASK),
                  fmt_char (LOWER_BYTE (status) & DATA_MASK));
        return DMA_Chars;


    case Status:
    case Status_DMAC:
    case Stat_Dec_DMAC:
        value = SEXT16 (status);

        if (value >= -3 && value <= 4)
            hp_trace (dptrs [card], TRACE_CMD, "Returned status is %s\n",
                      status_names [value + STATUS_BIAS]);
        else
            hp_trace (dptrs [card], TRACE_CMD, "Returned status is %d\n", value);

        if (response == Status_DMAC && value == 0)
            return DMA_Chars;

        else if (response == Stat_Dec_DMAC && value == 0)
            return Decimal_DMAC;
        break;


    case None:
        hp_trace (dptrs [card], TRACE_CMD, "Unexpected data %06o returned\n", status);
        break;


    case Octal_DMAB:
    case Dec_Status:
    case Dec_Stat_DMAC:
        break;                                          /* these responses only occur on output */
    }                                                   /* all cases are handled */

return None;                                            /* return None here to catch the "others" case */
}



/* Process synchronization routines */



#if defined (_WIN32) && ! defined (USE_FALLBACK)

/* Windows process synchronization */


/* Create a synchronization event.

   This routine creates a synchronization event using the supplied name and
   returns the event handle to the caller.  If creation succeeds, the routine
   returns 0.  Otherwise, the error value is returned.

   The event is created with these attributes: no security, automatic reset, and
   initially non-signaled.
*/

static uint32 create_event (const char *name, EVENT *event)
{
uint32 error = 0;

*event = CreateEvent (NULL, FALSE, FALSE, name);        /* create an auto-reset, initially not-signaled event */

if (*event == NULL) {                                   /* if event creation failed */
    error = (uint32) GetLastError ();                   /*   then get the error code */

    tprintf (iplo_dev, TRACE_STATE, "Creation with identifier \"%s\" failed with error %u\n",
             name, error);
    }

else                                                    /* otherwise the creation succeeded */
    tprintf (iplo_dev, TRACE_STATE, "Created event %p with identifier \"%s\"\n",
             (void *) *event, name);

return error;                                           /* return the operation status */
}


/* Destroy a synchronization event.

   This routine destroys the synchronization event specified by the supplied
   event handle.  If destruction succeeds, the event handle is invalidated, and
   the routine returns 0.  Otherwise, the error value is returned.

   The event name parameter is not used but is present for interoperability.
*/

static uint32 destroy_event (const char *name, EVENT *event)
{
BOOL   status;
uint32 error = 0;

if (*event != NULL) {                                   /* if the event exists */
    status = CloseHandle (*event);                      /*   then close it */

    if (status == FALSE) {                              /* if the close failed */
        error = (uint32) GetLastError ();               /*   then get the error code */

        tprintf (iplo_dev, TRACE_STATE, "Destruction of event %p failed with error %u\n",
                 (void *) *event, error);
        }

    else                                                /* otherwise the destruction succeeded */
        tprintf (iplo_dev, TRACE_STATE, "Destroyed event %p\n",
                 (void *) *event);

    *event = NULL;                                      /* reset the event handle */
    }

return error;                                           /* return the operation status */
}


/* Wait for a synchronization event.

   This routine waits for a synchronization event to be signaled or for the
   supplied maximum wait time to elapse.  If the event identified by the
   supplied handle is signaled, the routine returns 0 and sets the "signaled"
   flag to TRUE.  If the timeout expires without the event being signaled, the
   routine returns 0 with the "signaled" flag set to FALSE.  If the event wait
   fails, the routine returns the error value.


   Implementation notes:

    1. The maximum wait time may be zero to test the signaled state and return
       immediately, or may be set to "INFINITE" to wait forever.  The latter is
       not recommended, as it provides the user with no means to cancel the wait
       and return to the SCP prompt.
*/

static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
const DWORD wait_time = (DWORD) wait_in_ms;             /* interval wait time in milliseconds */
DWORD  status;
uint32 error = 0;

status = WaitForSingleObject (event, wait_time);        /* wait for the event, but not forever */

if (status == WAIT_FAILED) {                            /* if the wait failed */
    error = (uint32) GetLastError ();                   /*   then get the error code */

    tprintf (iplo_dev, TRACE_STATE, "Wait for event %p failed with error %u\n",
             (void *) event, error);
    }

else {                                                  /* otherwise the wait completed */
    *signaled = (status != WAIT_TIMEOUT);               /*   so set the flag TRUE if the wait did not time out */

    tprintf (iplo_dev, TRACE_STATE, "Event %p wait %s\n",
             (void *) event, (*signaled ? "signaled" : "timed out"));
    }

return error;                                           /* return the operation status */
}


/* Signal the synchronization event.

   This routine signals a the synchronization event specified by the supplied
   event handle.  If signaling succeeds, the routine returns 0.  Otherwise, the
   error value is returned.
*/

static uint32 signal_event (EVENT event)
{
BOOL status;
uint32 error = 0;

status = SetEvent (event);                              /* signal the event */

if (status == FALSE) {                                  /* if the call failed */
    error = (uint32) GetLastError ();                   /*   then get the error code */

    tprintf (iplo_dev, TRACE_STATE, "Signal of event %p failed with error %u\n",
             (void *) event, error);
    }

else                                                    /* otherwise the signal succeeded */
    tprintf (iplo_dev, TRACE_STATE, "Event %p signaled\n",
             (void *) event);

return error;                                           /* return the operation status */
}



#elif defined (HAVE_SEMAPHORE) && ! defined (USE_FALLBACK)

/* UNIX process synchronization */


/* Create the synchronization event.

   This routine creates a synchronization event using the supplied name and
   returns an event object  to the caller.  If creation succeeds, the routine
   returns 0.  Otherwise, the error value is returned.

   Systems that define the semaphore functions but implement them as stubs will
   return ENOSYS.  We handle this case by enabling fallback to the unimplemented
   behavior, i.e., emulating a process wait by a timed pause and delaying EDT
   to avoid a race condition.  Error returns are reported back to the caller in
   either case.

   Regarding the choice of event name, the Single UNIX Standard says:

     If [the] name begins with the <slash> character, then processes calling
     sem_open() with the same value of name shall refer to the same semaphore
     object, as long as that name has not been removed.  If name does not begin
     with the <slash> character, the effect is implementation-defined.

   Therefore, event names passed to this routine should begin with a slash
   character.

   The event is created as initially not-signaled.


   Implementation notes:

    1. We enable the EDT delay after an IOP-to-SP data transfer completes to
       help ameliorate the race condition that would otherwise occur.  See the
       notes in the "ipl_interface" routine for details.
*/

static uint32 create_event (const char *name, EVENT *event)
{
*event = sem_open (name, O_CREAT, S_IRWXU, 0);          /* create an initially not-signaled event */

if (*event == SEM_FAILED) {                             /* if event creation failed */
    if (errno == ENOSYS) {                              /*   then if the function is not implemented */
        edt_delay = 1;                                  /*     then enable the EDT delay workaround */

        tprintf (iplo_dev, TRACE_STATE, "sem_open is unsupported on this system; using fallback\n");
        }

    else                                                /*   otherwise it is an unexpected error */
        tprintf (iplo_dev, TRACE_STATE, "Creation with identifier \"%s\" failed with error %u\n",
                 name, errno);

    return (uint32) errno;                              /* return the error code to indicate failure */
    }

else {                                                  /* otherwise the creation succeeded */
    tprintf (iplo_dev, TRACE_STATE, "Created event %p with identifier \"%s\"\n",
             (void *) *event, name);
    return 0;                                           /*   so return success */
    }
}


/* Destroy the synchronization event.

   This routine destroys the synchronization event specified by the supplied
   event name.  If destruction succeeds, the event object is invalidated, and
   the routine returns 0.  Otherwise, the error value is returned.


   Implementation notes:

    1. If the other simulator instance destroys the event first, our
       "sem_unlink" call will fail with ENOENT.  This is an expected error, and
       the routine returns success in this case.
*/

static uint32 destroy_event (const char *name, EVENT *event)
{
int status;

if (*event != SEM_FAILED) {                             /* if the event exists */
    status = sem_unlink (name);                         /*   then delete it */

    if (status != 0 && errno != ENOENT) {               /* if the deletion failed */
        tprintf (iplo_dev, TRACE_STATE, "Destruction of event %p failed with error %u\n",
                 (void *) *event, errno);

        return (uint32) errno;                          /*   then return the error code */
        }

    else                                                /* otherwise the deletion succeeded */
        tprintf (iplo_dev, TRACE_STATE, "Destroyed event %p\n",
                 (void *) *event);

    *event = SEM_FAILED;                                /* reset the event handle */
    }

return 0;                                               /* return success */
}


/* Wait for the synchronization event.

   This routine waits for a synchronization event to be signaled or for the
   supplied maximum wait time to elapse.  If the event identified by the
   supplied event object is signaled, the routine returns 0 and sets the
   "signaled" flag to TRUE.  If the timeout expires without the event being
   signaled, the routine returns 0 with the "signaled" flag set to FALSE.  If
   the event wait fails, the routine returns the error value.


   Implementation notes:

    1. The maximum wait time may be zero to test the signaled state and return
       immediately, or may be set to a large value to wait forever.  The latter
       is not recommended, as it provides the user with no means to cancel the
       wait and return to the SCP prompt.
*/

static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
const time_t wait_s  = (time_t) (wait_in_ms / 1000);            /* interval wait time in seconds */
const long   wait_ns = (long) (wait_in_ms % 1000) * 1000000;    /* interval wait time in nanoseconds */
struct timespec until_time;
int    status;

if (clock_gettime (CLOCK_REALTIME, &until_time)) {      /* get the current time; if it failed */
    tprintf (iplo_dev, TRACE_STATE, "Wait for event %p failed with clock error %u\n",
             (void *) event, errno);

    return (uint32) errno;                              /*   then return the error number */
    }

else {                                                  /* otherwise */
    until_time.tv_sec  = until_time.tv_sec  + wait_s;   /*   set the (absolute) */
    until_time.tv_nsec = until_time.tv_nsec + wait_ns;  /*     timeout expiration */

    if (until_time.tv_nsec >= 1000000000) {             /* if the nanosecond count overflowed */
        until_time.tv_nsec -= 1000000000;               /*   then rescale it */
        until_time.tv_sec  += 1;                        /*     to fit */
        }
    }

status = sem_timedwait (event, &until_time);            /* wait for the event, but not forever */

*signaled = (status == 0);                              /* set the flag TRUE if the wait did not time out */

if (status)                                             /* if the wait terminated */
    if (errno == ETIMEDOUT || errno == EINTR)           /*   then note if it timed out or was manually aborted */
        tprintf (iplo_dev, TRACE_STATE, "Event %p wait timed out\n",
                 (void *) event);

    else {                                              /*   otherwise it's an unexpected error */
        tprintf (iplo_dev, TRACE_STATE, "Wait for event %p failed with error %u\n",
                 (void *) event, errno);

        return (uint32) errno;                          /*     so return the error code */
        }

else                                                    /* otherwise the event is signaled */
    tprintf (iplo_dev, TRACE_STATE, "Event %p wait signaled\n",
             (void *) event);

return 0;                                               /* return success */
}


/* Signal the synchronization event.

   This routine signals a the synchronization event specified by the supplied
   event handle.  If signaling succeeds, the routine returns 0.  Otherwise, the
   error value is returned.
*/

static uint32 signal_event (EVENT event)
{
int status;

status = sem_post (event);                              /* signal the event */

if (status) {                                           /* if the call failed */
    tprintf (iplo_dev, TRACE_STATE, "Signal of event %p failed with error %u\n",
             (void *) event, errno);

    return (uint32) errno;                              /*   then return the error code */
    }

else                                                    /* otherwise the event was signaled */
    tprintf (iplo_dev, TRACE_STATE, "Event %p signaled\n",
             (void *) event);

return 0;                                               /* return success */
}



#else

/* Process synchronization stubs.

   The stubs generally return failure to inform the caller that host support
   for the expected behavior is not available.  It is up to the caller to supply
   a fallback mechanism, if desired.  An exception is the "destroy_event"
   function.  This returns success to indicate that the events no longer exist
   (and indeed never existed).


   Implementation notes:

    1. We enable the EDT delay after an IOP-to-SP data transfer completes to
       help ameliorate the race condition that would otherwise occur.  See the
       notes in the "ipl_interface" routine for details.
*/

static uint32 create_event (const char *name, EVENT *event)
{
tprintf (iplo_dev, TRACE_STATE, "Synchronization is unsupported on this system; using fallback\n");

edt_delay = 1;                                          /* enable the EDT delay workaround */
return 1;                                               /*   and return failure */
}


static uint32 destroy_event (const char *name, EVENT *event)
{
return 0;                                               /* return success */
}


static uint32 wait_event (EVENT event, uint32 wait_in_ms, t_bool *signaled)
{
return 1;                                               /* return failure */
}


static uint32 signal_event (EVENT event)
{
return 1;                                               /* return failure */
}


#endif
