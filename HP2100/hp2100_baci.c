/* hp2100_baci.c: HP 12966A Buffered Asynchronous Communications Interface simulator

   Copyright (c) 2007-2019, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   BACI         12966A Buffered Asynchronous Communications Interface

   23-Jan-19    JDB     Removed DEV_MUX to avoid TMXR debug flags
   10-Jul-18    JDB     Revised I/O model
   01-Nov-17    JDB     Fixed serial output buffer overflow handling
   15-Mar-17    JDB     Trace flags are now global
                        Changed DEBUG_PRI calls to tprintfs
   10-Mar-17    JDB     Added IOBUS to the debug table
   17-Jan-17    JDB     Changed "hp_---sc" and "hp_---dev" to "hp_---_dib"
   02-Aug-16    JDB     "baci_poll_svc" now calls "tmxr_poll_conn" unilaterally
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Dec-14    JDB     Added casts for explicit downward conversions
   10-Jan-13    MP      Added DEV_MUX and additional DEVICE field values
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Removed DEV_NET to allow restoration of listening port
   28-Mar-11    JDB     Tidied up signal handling
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
   25-Nov-08    JDB     Revised for new multiplexer library SHOW routines
   11-Sep-08    JDB     Fixed STC,C losing interrupt request on BREAK
   07-Sep-08    JDB     Fixed IN_LOOPBACK conflict with netinet/in.h
                        Changed Telnet poll to connect immediately after reset or attach
   10-Aug-08    JDB     Added REG_FIT to register variables < 32-bit size
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   17-Jun-08    JDB     Moved fmt_char() function to hp2100_sys.c
   13-Jun-08    JDB     Cleaned up debug reporting for sim_activate calls
   16-Apr-08    JDB     Separated terminal I/O and Telnet poll for idle compatibility
   07-Dec-07    JDB     Created BACI device

   References:
   - HP 12966A Buffered Asynchronous Data Communications Interface Installation
       and Reference Manual (12966-90001, Jul-1982)
   - Western Digital Communications Products Handbook (Jun-1984)


   The 12966A BACI card supplanted the 12531C Teletype and 12880A CRT interfaces
   as the primary terminal connection for HP 1000 systems.  The main advantage
   of this card over the others was its 128-character FIFO memory.  While this
   allowed more efficient I/O than its interrupt-per-character predecessors, the
   most significant advantage was that block input from the 264x-series of CRT
   terminals was supported.  The 264x were the first HP-supported terminals to
   provide local editing and character storage, as well as mass storage via dual
   DC-100 minicartridge drives.  This support meant that input from the terminal
   could come in bursts at the full baud rate, which would overrun the older
   cards that needed a small intercharacter handling time.  Also, the older
   cards placed a substantial load on the CPU in high-baud-rate output
   applications.  Indeed, block output under RTE on a 1000 M-Series with a
   12880A CRT card would saturate the CPU at about 5700 baud.

   For a while, the BACI and the earlier cards were both supported as the system
   console interface, and RTE primary systems were generated with drivers for
   both cards.  The boot-time I/O reconfigurator would detect the presence of
   the BACI card and would dynamically select the correct driver (DVR05 vs.
   DVR00).  However, the 12880A card faded quickly as the 264x and later 262x
   terminals gained in popularity, and support for the 12880A was dropped in
   favor of the BACI.  This meant that later RTE primary systems could only be
   run on CPUs containing a BACI card.

   The simulation supports terminal and diagnostic modes.  The latter simulates
   the installation of the 12966-60003 diagnostic loopback connector on the
   card.

   Fifteen programmable baud rates were supported by the BACI.  We simulate
   these "realistic" rates by scheduling I/O service based on the appropriate
   number of 1000 E-Series instructions for the rate selected.  We also provide
   an "external rate" that is equivalent to 9600 baud, as most terminals were
   set to their maximum speeds.

   We support the 12966A connected to an HP terminal emulator via Telnet or a
   serial port.  Internally, we model the BACI as a terminal multiplexer with
   one line.  The simulation is complicated by the half-duplex nature of the
   card (there is only one FIFO, used selectively either for transmission or
   reception) and the double-buffered UART (a Western Digital TR1863A), which
   has holding registers as well as a shift registers for transmission and
   reception.  We model both sets of device registers.

   During an output operation, the first character output to the card passes
   through the FIFO and into the transmitter holding register.  Subsequent
   characters remain in the FIFO.  If the FIFO is then turned around by a mode
   switch from transmission to reception, the second character output becomes
   the first character input to the CPU, as the first character output remains
   in the THR.  Also, the FIFO counter reflects the combined state of the FIFO
   and the THR: it is incremented by a "shift in" to the FIFO and decremented by
   the "transmit complete" signal from the UART.  This has two implications:

    1. If the FIFO is turned around before the character in the THR is
       transmitted, the counter will not decrement when transmission is
       complete, so the FIFO will show as "empty" when the counter reads "1".

    2. The FIFO counter will indicate "half full" and "full" one character
       before the FIFO itself reaches those stages.

   The diagnostic hood connects the UART clock to a spare output register.  This
   allows the diagnostic to supply programmed clock pulses to the UART.  The
   serial transmit and receive lines from the UART are also available to the
   diagnostic.  Functional operation is checked by supplying or testing serial
   data while clocking the UART sixteen times for each bit.  This meant that we
   had to model the UART shift registers for faithful hardware simulation.

   The simulation provides both the "realistic timing" described above, as well
   as an "optimized (fast) timing" option.  Optimization makes three
   improvements:

    1. On output, characters in the FIFO are emptied into the line buffer as a
       block, rather than one character per service call, and on input, all of
       the characters available in the line buffer are loaded into the FIFO as a
       block.

    2. The ENQ/ACK handshake is done locally, without involving the terminal
       client.

    3. Input occurring during an output operation is delayed until the second or
       third consecutive ENQ/ACK handshake.

   During development, it was noted that a comparatively long time elapsed
   (approximately 30 milliseconds on a 3 GHz system) between the transmission of
   an ENQ and the reception of the ACK.  As the RTE BACI driver, DVR05, does
   three ENQ/ACKs at the end of each line, plus an additional ENQ/ACK every 33
   characters within a line, maximum throughput was about ten lines per second.
   The source of this delay is not understood but apparently lies within the
   terminal emulator, as it was observed with two emulators from two different
   companies.  Absorbing the ENQ and generating the ACK locally provided a
   dramatic improvement in output speed.

   However, as a result, RTE break-mode became effectively impossible, i.e.,
   striking a key during output no longer produced the break-mode prompt.  This
   was traced to the RTE driver.  DVR05 only checks for an input character
   during ENQ/ACK processing, and then only during the second and third
   end-of-line handshakes.  When the ENQ/ACKs were eliminated, break-mode also
   disappeared.

   The workaround is to save a character received during output and supply it
   during the second or third consecutive handshake.  This ensures that
   break-mode is recognized.  Because the driver tries to "cheat" the card by
   selecting receive mode before the ENQ has actually been transmitted (in order
   to save an interrupt), the FIFO counter becomes "off by one" and is reset
   with a master clear at the end of each handshake.  This would normally clear
   the UART receiving register, thereby losing the deferred character.  We work
   around this by skipping the register clear in "fast timing" mode.
*/

#include <ctype.h>

#include "hp2100_defs.h"
#include "hp2100_io.h"

#include "sim_tmxr.h"


/* Program limits */

#define FIFO_SIZE       128                             /* read/write buffer size */


/* Character constants */

#define ENQ             '\005'
#define ACK             '\006'


/* Unit flags */

#define UNIT_V_DIAG     (UNIT_V_UF + 0)                 /* diagnostic mode */
#define UNIT_V_FASTTIME (UNIT_V_UF + 1)                 /* fast timing mode */
#define UNIT_V_CAPSLOCK (UNIT_V_UF + 2)                 /* caps lock mode */

#define UNIT_DIAG       (1 << UNIT_V_DIAG)
#define UNIT_FASTTIME   (1 << UNIT_V_FASTTIME)
#define UNIT_CAPSLOCK   (1 << UNIT_V_CAPSLOCK)


/* Bit flags */

#define OUT_MR          0100000                         /* common master reset */

#define OUT_ENCM        0000040                         /* ID1: enable character mode */
#define OUT_ENCB        0000020                         /* ID1: enable CB */
#define OUT_ENCC        0000010                         /* ID1: enable CC */
#define OUT_ENCE        0000004                         /* ID1: enable CE */
#define OUT_ENCF        0000002                         /* ID1: enable CF */
#define OUT_ENSXX       0000001                         /* ID1: enable SBB/SCF */

#define OUT_DIAG        0000040                         /* ID2: diagnostic output */
#define OUT_REFCB       0000020                         /* ID2: reference CB */
#define OUT_REFCC       0000010                         /* ID2: reference CC */
#define OUT_REFCE       0000004                         /* ID2: reference CE */
#define OUT_REFCF       0000002                         /* ID2: reference CF */
#define OUT_REFSXX      0000001                         /* ID2: reference SBB/SCF */

#define OUT_STBITS      0000040                         /* ID3: number of stop bits */
#define OUT_ECHO        0000020                         /* ID3: enable echo */
#define OUT_PARITY      0000010                         /* ID3: enable parity */
#define OUT_PAREVEN     0000004                         /* ID3: even parity or odd */

#define OUT_XMIT        0000400                         /* ID4: transmit or receive */
#define OUT_CA          0000200                         /* ID4: CA on */
#define OUT_CD          0000100                         /* ID4: CD on */
#define OUT_SXX         0000040                         /* ID4: SBA/SCA on */
#define OUT_DCPC        0000020                         /* ID4: DCPC on */

#define OUT_CSC         0000040                         /* ID5: clear special char interrupt */
#define OUT_CBH         0000020                         /* ID5: clear buffer half-full interrupt */
#define OUT_CBF         0000010                         /* ID5: clear buffer full interrupt */
#define OUT_CBE         0000004                         /* ID5: clear buffer empty interrupt */
#define OUT_CBRK        0000002                         /* ID5: clear break interrupt */
#define OUT_COVR        0000001                         /* ID5: clear overrun/parity interrupt */

#define OUT_SPFLAG      0000400                         /* ID6: special character */

#define OUT_IRQCLR      (OUT_CBH | OUT_CBF | OUT_CBE | OUT_CBRK | OUT_COVR)


#define IN_VALID        0100000                         /* received data: character valid */
#define IN_SPFLAG       0040000                         /* received data: is special character */

#define IN_DEVINT       0100000                         /* status: device interrupt */
#define IN_SPCHAR       0040000                         /* status: special char has been recd */
#define IN_SPARE        0010000                         /* status: spare receiver state */
#define IN_TEST         0004000                         /* status: unprocessed serial data line */
#define IN_BUFHALF      0001000                         /* status: buffer is half full */
#define IN_BUFFULL      0000400                         /* status: buffer is full */
#define IN_BUFEMPTY     0000200                         /* status: buffer is empty */
#define IN_BREAK        0000100                         /* status: break detected */
#define IN_OVRUNPE      0000040                         /* status: overrun or parity error */
#define IN_CB           0000020                         /* status: CB is on */
#define IN_CC           0000010                         /* status: CC is on */
#define IN_CE           0000004                         /* status: CE is on */
#define IN_CF           0000002                         /* status: CF is on */
#define IN_SXX          0000001                         /* status: SBB/SCF is on */

#define IN_MODEM        (IN_CB | IN_CC | IN_CE | IN_CF | IN_SXX)
#define IN_DIAG         (IN_DEVINT | IN_SPARE | IN_TEST | IN_MODEM)
#define IN_STDIRQ       (IN_DEVINT | IN_SPCHAR | IN_BREAK | IN_OVRUNPE)
#define IN_FIFOIRQ      (IN_BUFEMPTY | IN_BUFHALF | IN_BUFFULL)


/* Packed starting bit numbers */

#define OUT_V_ID        12                              /* common output word ID */
#define OUT_V_DATA       0                              /* ID 0: output data character */
#define OUT_V_CHARSIZE   0                              /* ID 3: character size */
#define OUT_V_BAUDRATE   0                              /* ID 4: baud rate */
#define OUT_V_SPCHAR     0                              /* ID 6: special character */

#define IN_V_CHARCNT     8                              /* data: char count in buffer */
#define IN_V_DATA        0                              /* data: input character */
#define IN_V_IRQCLR      5                              /* status: interrupt status clear */


/* Packed bit widths */

#define OUT_W_ID        3
#define OUT_W_DATA      8
#define OUT_W_CHARSIZE  2
#define OUT_W_BAUDRATE  4
#define OUT_W_SPCHAR    8

#define IN_W_CHARCNT    6
#define IN_W_DATA       8

/* Packed bit masks */

#define OUT_M_ID        ((1 << OUT_W_ID)       - 1)
#define OUT_M_DATA      ((1 << OUT_W_DATA)     - 1)
#define OUT_M_CHARSIZE  ((1 << OUT_W_CHARSIZE) - 1)
#define OUT_M_BAUDRATE  ((1 << OUT_W_BAUDRATE) - 1)
#define OUT_M_SPCHAR    ((1 << OUT_W_SPCHAR)   - 1)

#define IN_M_CHARCNT    ((1 << IN_W_CHARCNT) - 1)
#define IN_M_DATA       ((1 << IN_W_DATA)    - 1)

/* Packed field masks */

#define OUT_ID          (OUT_M_ID       << OUT_V_ID)
#define OUT_DATA        (OUT_M_DATA     << OUT_V_DATA)
#define OUT_CHARSIZE    (OUT_M_CHARSIZE << OUT_V_CHARSIZE)
#define OUT_BAUDRATE    (OUT_M_BAUDRATE << OUT_V_BAUDRATE)
#define OUT_SPCHAR      (OUT_M_SPCHAR   << OUT_V_SPCHAR)

#define IN_CHARCNT      (IN_M_CHARCNT << IN_V_CHARCNT)
#define IN_DATA         (IN_M_DATA    << IN_V_DATA)


/* Command helpers */

#define TO_CHARCNT(c)   (((c) << IN_V_CHARCNT) & IN_CHARCNT)

#define GET_ID(i)       (((i) & OUT_ID) >> OUT_V_ID)
#define GET_BAUDRATE(b) (((b) & OUT_BAUDRATE) >> OUT_V_BAUDRATE)

#define IO_MODE         (baci_icw & OUT_XMIT)
#define XMIT            OUT_XMIT
#define RECV            0

#define CLEAR_HR         0                              /* UART holding register clear value */
#define CLEAR_R         -1                              /* UART register clear value */


/* Unit references */

#define baci_term       baci_unit[0]                    /* terminal I/O unit */
#define baci_poll       baci_unit[1]                    /* line polling unit */


/* Interface state */

typedef struct {
    FLIP_FLOP  control;                         /* control flip-flop */
    FLIP_FLOP  flag;                            /* flag flip-flop */
    FLIP_FLOP  flag_buffer;                     /* flag buffer flip-flop */
    FLIP_FLOP  srq;                             /* SRQ flip-flop */
    FLIP_FLOP  lockout;                         /* interrupt lockout flip-flop */
    } CARD_STATE;

static CARD_STATE baci;                         /* per-card state */


/* BACI state variables */

static uint16 baci_ibuf = 0;                                   /* status/data in */
static uint16 baci_obuf = 0;                                   /* command/data out */
static uint16 baci_status = 0;                                 /* current status */

static uint16 baci_edsiw = 0;                                  /* enable device status word */
static uint16 baci_dsrw = 0;                                   /* device status reference word */
static uint16 baci_cfcw = 0;                                   /* character frame control word */
static uint16 baci_icw = 0;                                    /* interface control word */
static uint16 baci_isrw = 0;                                   /* interrupt status reset word */

static uint32 baci_fput = 0;                                   /* FIFO buffer add index */
static uint32 baci_fget = 0;                                   /* FIFO buffer remove index */
static uint32 baci_fcount = 0;                                 /* FIFO buffer counter */
static uint32 baci_bcount = 0;                                 /* break counter */

static uint8 baci_fifo [FIFO_SIZE];                            /* read/write buffer FIFO */
static uint8 baci_spchar [256];                                /* special character RAM */

static uint16 baci_uart_thr = CLEAR_HR;                        /* UART transmitter holding register */
static uint16 baci_uart_rhr = CLEAR_HR;                        /* UART receiver holding register */
static  int32 baci_uart_tr  = CLEAR_R;                         /* UART transmitter register */
static  int32 baci_uart_rr  = CLEAR_R;                         /* UART receiver register */
static uint32 baci_uart_clk = 0;                               /* UART transmit/receive clock */

static t_bool baci_enq_seen = FALSE;                           /* ENQ seen flag */
static uint32 baci_enq_cntr = 0;                               /* ENQ seen counter */


/* BACI local SCP support routines */

static INTERFACE baci_interface;

/* BACI local routines */

static int32 service_time  (uint32 control_word);
static void  update_status (void);
static void  master_reset  (void);

static uint16 fifo_get   (void);
static void   fifo_put   (uint8 ch);
static void   clock_uart (void);

/* BACI local SCP support routines */

static t_stat baci_term_svc (UNIT *uptr);
static t_stat baci_poll_svc (UNIT *uptr);
static t_stat baci_reset (DEVICE *dptr);
static t_stat baci_attach (UNIT *uptr, CONST char *cptr);
static t_stat baci_detach (UNIT *uptr);


/* BACI SCP data structures */


/* Terminal multiplexer library descriptors */

static TMLN baci_ldsc [] = {                    /* line descriptors */
    { 0 }
    };

static TMXR baci_desc = {                       /* multiplexer descriptor */
    1,                                          /* number of terminal lines */
    0,                                          /* listening port (reserved) */
    0,                                          /* master socket  (reserved) */
    baci_ldsc,                                  /* line descriptor array */
    NULL,                                       /* line connection order */
    NULL                                        /* multiplexer device (derived internally) */
    };


/* Unit list.

   Two units are used: one to handle character I/O via the multiplexer library,
   and another to poll for connections and input.  The character I/O service
   routine runs only when there are characters to read or write.  It operates at
   the approximate baud rate of the terminal (in CPU instructions per second) in
   order to be compatible with the OS drivers.  The line poll must run
   continuously, but it can operate much more slowly, as the only requirement is
   that it must not present a perceptible lag to human input.  To be compatible
   with CPU idling, it is co-scheduled with the master poll timer, which uses a
   ten millisecond period.
*/

static UNIT baci_unit[] = {
    { UDATA (&baci_term_svc, UNIT_ATTABLE | UNIT_FASTTIME, 0) },    /* terminal I/O unit */
    { UDATA (&baci_poll_svc, UNIT_DIS, POLL_FIRST) }                /* line poll unit */
    };


/* Device information block */

static DIB baci_dib = {
    &baci_interface,                                            /* the device's I/O interface function pointer */
    BACI,                                                       /* the device's select code (02-77) */
    0,                                                          /* the card index */
    "12966A Buffered Asynchronous Communications Interface",    /* the card description */
    NULL                                                        /* the ROM description */
    };


/* Register list */

static REG baci_reg [] = {
/*    Macro   Name      Location              Radix  Width  Offset    Depth           Flags     */
/*    ------  --------  --------------------  -----  -----  ------  ----------  --------------- */
    { ORDATA (IBUF,     baci_ibuf,                    16),                      REG_FIT | REG_X },
    { ORDATA (OBUF,     baci_obuf,                    16),                      REG_FIT | REG_X },
    { GRDATA (STATUS,   baci_status,            2,    16,     0),               REG_FIT         },

    { ORDATA (EDSIW,    baci_edsiw,                   16),                      REG_FIT         },
    { ORDATA (DSRW,     baci_dsrw,                    16),                      REG_FIT         },
    { ORDATA (CFCW,     baci_cfcw,                    16),                      REG_FIT         },
    { ORDATA (ICW,      baci_icw,                     16),                      REG_FIT         },
    { ORDATA (ISRW,     baci_isrw,                    16),                      REG_FIT         },

    { DRDATA (FIFOPUT,  baci_fput,                     8)                                       },
    { DRDATA (FIFOGET,  baci_fget,                     8)                                       },
    { DRDATA (FIFOCNTR, baci_fcount,                   8)                                       },
    { DRDATA (BRKCNTR,  baci_bcount,                  16)                                       },

    { BRDATA (FIFO,     baci_fifo,              8,     8,           FIFO_SIZE), REG_A           },
    { BRDATA (SPCHAR,   baci_spchar,            8,     1,           256)                        },

    { ORDATA (UARTTHR,  baci_uart_thr,                16),                      REG_FIT | REG_X },
    { ORDATA (UARTTR,   baci_uart_tr,                 16),                      REG_NZ  | REG_X },
    { ORDATA (UARTRHR,  baci_uart_rhr,                16),                      REG_FIT | REG_X },
    { ORDATA (UARTRR,   baci_uart_rr,                 16),                      REG_NZ  | REG_X },
    { DRDATA (UARTCLK,  baci_uart_clk,                16)                                       },

    { DRDATA (CTIME,    baci_term.wait,               19)                                       },

    { FLDATA (ENQFLAG,  baci_enq_seen,                        0),               REG_HRO         },
    { DRDATA (ENQCNTR,  baci_enq_cntr,                16),                      REG_HRO         },

    { FLDATA (LKO,      baci.lockout,                         0)                                },
    { FLDATA (CTL,      baci.control,                         0)                                },
    { FLDATA (FLG,      baci.flag,                            0)                                },
    { FLDATA (FBF,      baci.flag_buffer,                     0)                                },
    { FLDATA (SRQ,      baci.srq,                             0)                                },

      DIB_REGS (baci_dib),

    { NULL }
    };


/* Modifier list */

static MTAB baci_mod[] = {
/*    Mask Value     Match Value    Print String        Match String  Validation   Display  Descriptor */
/*    -------------  -------------  ------------------  ------------  -----------  -------  ---------- */
    { UNIT_DIAG,     UNIT_DIAG,     "diagnostic mode",  "DIAGNOSTIC", NULL,        NULL,    NULL       },
    { UNIT_DIAG,     0,             "terminal mode",    "TERMINAL",   NULL,        NULL,    NULL       },

    { UNIT_FASTTIME, UNIT_FASTTIME, "fast timing",      "FASTTIME",   NULL,        NULL,    NULL       },
    { UNIT_FASTTIME, 0,             "realistic timing", "REALTIME",   NULL,        NULL,    NULL       },

    { UNIT_CAPSLOCK, UNIT_CAPSLOCK, "CAPS LOCK down",   "CAPSLOCK",   NULL,        NULL,    NULL       },
    { UNIT_CAPSLOCK, 0,             "CAPS LOCK up",     "NOCAPSLOCK", NULL,        NULL,    NULL       },

/*    Entry Flags          Value  Print String  Match String  Validation       Display           Descriptor          */
/*    -------------------  -----  ------------  ------------  ---------------  ----------------  ------------------- */
    { MTAB_XDV | MTAB_NC,   0,    "LOG",        "LOG",        &tmxr_set_log,   &tmxr_show_log,   (void *) &baci_desc },
    { MTAB_XDV | MTAB_NC,   0,     NULL,        "NOLOG",      &tmxr_set_nolog, NULL,             (void *) &baci_desc },

    { MTAB_XDV | MTAB_NMO,  1,    "CONNECTION", NULL,         NULL,            &tmxr_show_cstat, (void *) &baci_desc },
    { MTAB_XDV | MTAB_NMO,  0,    "STATISTICS", NULL,         NULL,            &tmxr_show_cstat, (void *) &baci_desc },
    { MTAB_XDV,             0,    NULL,         "DISCONNECT", &tmxr_dscln,     NULL,             (void *) &baci_desc },

    { MTAB_XDV,             1u,   "SC",         "SC",         &hp_set_dib,     &hp_show_dib,     (void *) &baci_dib  },
    { MTAB_XDV | MTAB_NMO, ~1u,   "DEVNO",      "DEVNO",      &hp_set_dib,     &hp_show_dib,     (void *) &baci_dib  },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB baci_deb [] = {
    { "CMDS",  DEB_CMDS    },
    { "CPU",   DEB_CPU     },
    { "BUF",   DEB_BUF     },
    { "XFER",  DEB_XFER    },
    { "IOBUS", TRACE_IOBUS },                   /* interface I/O bus signals and data words */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE baci_dev = {
    "BACI",                                     /* device name */
    baci_unit,                                  /* unit array */
    baci_reg,                                   /* register array */
    baci_mod,                                   /* modifier array */
    2,                                          /* number of units */
    10,                                         /* address radix */
    31,                                         /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    8,                                          /* data width */
    &tmxr_ex,                                   /* examine routine */
    &tmxr_dep,                                  /* deposit routine */
    &baci_reset,                                /* reset routine */
    NULL,                                       /* boot routine */
    &baci_attach,                               /* attach routine */
    &baci_detach,                               /* detach routine */
    &baci_dib,                                  /* device information block */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    baci_deb,                                   /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL,                                       /* logical device name */
    NULL,                                       /* help routine */
    NULL,                                       /* help attach routine*/
    (void *) &baci_desc                         /* help context */
    };


/* BACI interface.

   The BACI processes seven types of output words and supplies two types of
   input words.  Output word type is identified by an ID code in bits 14-12.
   Input word type is determined by the state of the control flip-flop.

   The card has the usual control, flag buffer, flag, and SRQ flip-flops.
   However, they have the following unusual characteristics:

    - STC is not required to transfer a character.
    - Flag is not set after character transfer completes.
    - FLAG and SRQ are decoupled and are set independently.

   An interrupt lockout flip-flop is used to prevent the generation of multiple
   interrupts until the cause of the first interrupt is identified and cleared
   by the CPU.


   Implementation notes:

    1. The STC handler checks to see if it was invoked for STC SC or STC SC,C.
       In the latter case, the check for new interrupt requests is deferred
       until after the CLF.  Otherwise, the flag set by the interrupt check
       would be cleared, and the interrupt would be lost.

    2. POPIO and CRS are ORed together on the interface card.  In simulation, we
       skip processing for POPIO because CRS is always asserted with POPIO
       (though the reverse is not true), and we don't need to call master_reset
       twice in succession.
*/

static SIGNALS_VALUE baci_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
const char * const hold_or_clear = (inbound_signals & ioCLF ? ",C" : "");
uint8              ch;
uint32             mask;
INBOUND_SIGNAL     signal;
INBOUND_SET        working_set = inbound_signals;
SIGNALS_VALUE      outbound    = { ioNONE, 0 };
t_bool             irq_enabled = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            baci.flag_buffer = CLEAR;                   /* reset the flag buffer */
            baci.flag        = CLEAR;                   /*   and flag flip-flops */
            baci.srq         = CLEAR;                   /* clear SRQ */

            tprintf (baci_dev, DEB_CMDS, "[CLF] Flag and SRQ cleared\n");

            update_status ();                           /* FLG might set when SRQ clears */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            baci.flag_buffer = SET;                     /* set the flag buffer flip-flop */

            baci.lockout = SET;                         /* set lockout */
            baci.srq = SET;                             /* set SRQ */

            tprintf (baci_dev, DEB_CMDS, "[STF] Flag, SRQ, and lockout set\n");
            break;


        case ioENF:                                     /* Enable Flag */
            if (baci.flag_buffer == SET)                /* if the flag buffer flip-flop is set */
                baci.flag = SET;                        /*   then set the flag flip-flop */

            baci.lockout = SET;                         /* set lockout */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (baci.flag == CLEAR)                     /* if the flag flip-flop is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (baci.flag == SET)                       /* if the flag flip-flop is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                         /* I/O data input */
            if (baci.control) {                             /* control set? */
                baci_ibuf = TO_CHARCNT (baci_fcount);       /* get FIFO count */

                if (IO_MODE == RECV)                        /* receiving? */
                    baci_ibuf = baci_ibuf | fifo_get ();    /* add char and validity flag */

                outbound.value = baci_ibuf;                 /* return received data */

                tprintf (baci_dev, DEB_CPU, "[LIx%s] Received data = %06o\n",
                         hold_or_clear, baci_ibuf);
                }

            else {                                          /* control clear? */
                outbound.value = baci_status;               /* return status */

                tprintf (baci_dev, DEB_CPU, "[LIx%s] Status = %06o\n",
                         hold_or_clear, baci_status);
                }
            break;


        case ioIOO:                                     /* I/O data output */
            baci_obuf = (uint16) inbound_value;         /* get data value */

            tprintf (baci_dev, DEB_CPU, "[OTx%s] Command = %06o\n",
                     hold_or_clear, baci_obuf);

            if (baci_obuf & OUT_MR) {                   /* master reset? */
                master_reset ();                        /* do before processing */
                working_set |= ioSIR;                   /* assert the Set Interrupt Request backplane signal */

                tprintf (baci_dev, DEB_CMDS, "[OTx%s] Master reset\n", hold_or_clear);
                }

            switch (GET_ID (baci_obuf)) {                   /* isolate ID code */

                case 0:                                     /* transmit data */
                    if (IO_MODE == XMIT) {                  /* transmitting? */
                        ch = baci_obuf & OUT_DATA;          /* mask to character */
                        fifo_put (ch);                      /* queue character */

                        if (baci_term.flags & UNIT_ATT) {           /* attached to network? */
                            if (TRACING (baci_dev, DEB_CMDS) &&     /* debugging? */
                                (sim_is_active (&baci_term) == 0))  /* service stopped? */
                                hp_trace (&baci_dev, DEB_CMDS, "[OTx%s] Terminal service scheduled, "
                                                               "time = %d\n", hold_or_clear, baci_term.wait);

                            if (baci_fcount == 1)                   /* first char to xmit? */
                                sim_activate_abs (&baci_term,       /* start service with full char time */
                                                  baci_term.wait);
                            else
                                sim_activate (&baci_term,           /* start service if not running */
                                              baci_term.wait);
                            }
                        }
                    break;

                case 1:                                     /* enable device status interrupt */
                    baci_edsiw = baci_obuf;                 /* load new enable word */
                    update_status ();                       /* may have enabled an interrupt */
                    break;

                case 2:                                     /* device status reference */
                    if ((baci_term.flags & UNIT_DIAG) &&    /* diagnostic mode? */
                        (baci_dsrw & OUT_DIAG) &&           /*   and last DIAG was high? */
                        !(baci_obuf & OUT_DIAG) &&          /*   and new DIAG is low? */
                        !(baci_icw & OUT_BAUDRATE))         /*   and clock is external? */
                        clock_uart ();                      /* pulse UART clock */

                    baci_dsrw = baci_obuf;                  /* load new reference word */
                    update_status ();                       /* clocking UART may interrupt */
                    break;

                case 3:                                     /* character frame control */
                    baci_cfcw = baci_obuf;                  /* load new frame word */
                    break;

                case 4:                                             /* interface control */
                    if ((baci_icw ^ baci_obuf) & OUT_BAUDRATE) {    /* baud rate change? */
                        baci_term.wait = service_time (baci_obuf);  /* set service time to match rate */

                        if (baci_term.flags & UNIT_DIAG)            /* diagnostic mode? */
                            if (baci_obuf & OUT_BAUDRATE) {         /* internal baud rate requested? */
                                sim_activate (&baci_term,           /* activate I/O service */
                                              baci_term.wait);

                                tprintf (baci_dev, DEB_CMDS, "[OTx%s] Terminal service scheduled, "
                                                             "time = %d\n", hold_or_clear, baci_term.wait);
                                }

                            else {                              /* external rate */
                                sim_cancel (&baci_term);        /* stop I/O service */

                                tprintf (baci_dev, DEB_CMDS, "[OTx%s] Terminal service stopped\n",
                                         hold_or_clear);
                                }
                        }

                    baci_icw = baci_obuf;                   /* load new reference word */
                    update_status ();                       /* loopback may change status */
                    break;

                case 5:                                     /* interrupt status reset */
                    baci_isrw = baci_obuf;                  /* load new reset word */

                    mask = (baci_isrw & OUT_IRQCLR) <<      /* form reset mask */
                           IN_V_IRQCLR;                     /*   for common irqs */

                    if (baci_isrw & OUT_CSC)                /* add special char mask bit */
                        mask = mask | IN_SPCHAR;            /*   if requested */

                    baci_status = baci_status & ~mask;      /* clear specified status bits */
                    break;

                case 6:                                     /* special character */
                    baci_spchar [baci_obuf & OUT_SPCHAR] =  /* set special character entry */
                        ((baci_obuf & OUT_SPFLAG) != 0);
                    break;
                }
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            break;                                      /* POPIO and CRS are ORed on the interface */


        case ioCRS:                                     /* Control Reset */
            master_reset ();                            /* issue master reset */

            tprintf (baci_dev, DEB_CMDS, "[CRS] Master reset\n");
            break;


        case ioCLC:                                     /* Clear Control flip-flop */
            baci.control = CLEAR;                       /* clear the control flip-flop */

            tprintf (baci_dev, DEB_CMDS, "[CLC%s] Control cleared\n", hold_or_clear);
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            baci.control = SET;                         /* set the control flip-flop */
            baci.lockout = CLEAR;                       /*   and clear lockout */

            tprintf (baci_dev, DEB_CMDS, "[STC%s] Control set and lockout cleared\n", hold_or_clear);

            if (!(inbound_signals & ioCLF))             /* STC without ,C ? */
                update_status ();                       /* clearing lockout might interrupt */
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (baci.control & baci.flag)               /* if the control and flag flip-flops are set */
                outbound.signals |= cnVALID;            /*   then deny PRL */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */

            if (baci.control & baci.flag & baci.flag_buffer)    /* if the control, flag, and flag buffer flip-flops are set */
                outbound.signals |= cnIRQ | cnVALID;            /*   then conditionally assert IRQ */

            if (baci.srq == SET)                        /* if the SRQ flip-flop is set */
                outbound.signals |= ioSRQ;              /*   then assert SRQ */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            baci.flag_buffer = CLEAR;                   /* clear the flag buffer flip-flop */
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


        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* BACI terminal service.

   The terminal service routine is used to transmit and receive characters.

   In terminal mode, it is started when a character is ready for output or when
   the line poll routine determines that there are characters ready for input
   and stopped when there are no more characters to output or input.  When the
   terminal is quiescent, this routine does not run.

   In diagnostic mode, it is started whenever an internal baud rate is set and
   stopped when the external clock is requested.  In this mode, the routine will
   be called without an attached socket, so character I/O will be skipped.

   Because there is only one FIFO, the card is half-duplex and must be
   configured for transmit or receive mode.  The UART, though, is double-
   buffered, so it may transmit and receive simultaneously.  We implement both
   the UART shift and holding registers for each mode.

   If a character is received by the UART while the card is in transmit mode, it
   will remain in the receiver holding register (RHR).  When the mode is
   reversed, the RHR contents will be unloaded into the FIFO.  Conversely,
   transmit mode enables the output of the FIFO to be unloaded into the
   transmitter holding register (THR).  Characters received or transmitted pass
   through the receiver register (RR) or transmitter register (TR),
   respectively.  They are not strictly necessary in terminal transactions but
   are critical to diagnostic operations.

   The UART signals an overrun if a complete character is received while the RHR
   still contains the previous character.  The BACI does not use this signal,
   though; an overrun is only indicated if the FIFO is full, and another
   character is received.

   In "fast timing" mode, we defer the recognition of a received character until
   the card is put into receive mode for the second or third consecutive ENQ/ACK
   handshake.  This improves RTE break-mode recognition.  "Realistic timing"
   mode behaves as the hardware does: a character present in the RHR is unloaded
   into the FIFO as soon as receive mode is set.

   Fast timing mode also enables internal ENQ/ACK handshaking.  We allow one
   character time for the RTE driver to turn the card around, as otherwise the
   ACK may not be seen by the driver.  Also, the local ACK is supplied after any
   received characters, as the driver detects operator attention only when the
   first character after an ENQ is not an ACK.

   Finally, fast timing enables buffer combining.  For output, all characters
   present in the FIFO are unloaded into the line buffer before initiating a
   packet send.  For input, all characters present in the line buffer are loaded
   into the FIFO.  This reduces network traffic and decreases simulator overhead
   (there is only one service routine entry per block, rather than one per
   character).

   In fast output mode, it is imperative that not less than 1500 instructions
   elapse between the first character load to the FIFO and the initiation of
   transmission.  The RTE driver must have enough time to output the maximum
   number of contiguous characters (33) and reset the interrupt status flags
   before the service routine is entered.  Because all of the characters are
   transmitted as a block, the FIFO empty flag will be set by the service
   routine.  If the driver has not yet exited at that time, the buffer-empty
   interrupt will be cleared when the interrupt status reset is done.  The
   symptom will be a 3.8-second pause in output until the driver times out.

   To avoid this, the OTx output character handler does an absolute schedule for
   the first character to ensure that a full character time is used.


   Implementation notes:

    1. The terminal multiplexer library "tmxr_putc_ln" routine returns
       SCPE_STALL if it is called when the transmit buffer is full.  When the
       last character is added to the buffer, the routine returns SCPE_OK but
       also changes the "xmte" field of the terminal multiplexer line (TMLN)
       structure from 1 to 0 to indicate that further calls will be rejected.
       The "xmte" value is set back to 1 when the tranmit buffer empties.

       This presents two approaches to handling buffer overflows: either call
       "tmxr_putc_ln" unconditionally and test for SCPE_STALL on return, or call
       "tmxr_putc_ln" only if "xmte" is 1.  The former approach adds a new
       character to the transmit buffer as soon as space is available, while the
       latter adds a new character only when the buffer has completely emptied.
       With either approach, transmission must be rescheduled after a delay to
       allow the buffer to drain.

       It would seem that the former approach is more attractive, as it would
       allow the simulated I/O operation to complete more quickly.  However,
       there are two mitigating factors.  First, the library attempts to write
       the entire transmit buffer in one host system call, so there is usually
       no time difference between freeing one buffer character and freeing the
       entire buffer (barring host system buffer congestion).  Second, the
       routine increments a "character dropped" counter when returning
       SCPE_STALL status.  However, the characters actually would not be lost,
       as the SCPE_STALL return would schedule retransmission when buffer space
       is available, .  This would lead to erroneous reporting in the SHOW
       <unit> STATISTICS command.

       Therefore, we adopt the latter approach and reschedule transmission if
       the "xmte" field is 0.  Note that the "tmxr_poll_tx" routine still must
       be called in this case, as it is responsible for transmitting the buffer
       contents and therefore freeing space in the buffer.

    2. The "tmxr_putc_ln" library routine returns SCPE_LOST if the line is not
       connected.  We ignore this error so that an OS may output an
       initialization "welcome" message even when the terminal is not connected.
       This permits the simulation to continue while ignoring the output.
*/

static t_stat baci_term_svc (UNIT *uptr)
{
uint32 data_bits, data_mask;
const t_bool fast_timing = (baci_term.flags & UNIT_FASTTIME) != 0;
const t_bool is_attached = (baci_term.flags & UNIT_ATT) != 0;
t_stat status = SCPE_OK;
t_bool recv_loop = TRUE;
t_bool xmit_loop = (baci_ldsc [0].xmte != 0);           /* TRUE if the transmit buffer is not full */


/* Transmission */

if (baci_ldsc [0].xmte == 0)                            /* if the transmit buffer is full */
    tprintf (baci_dev, DEB_XFER, "Transmission stalled for full buffer\n");


while (xmit_loop && (baci_uart_thr & IN_VALID)) {       /* valid character in UART? */
    data_bits = 5 + (baci_cfcw & OUT_CHARSIZE);         /* calculate number of data bits */
    data_mask = (1 << data_bits) - 1;                   /* generate mask for data bits */
    baci_uart_tr = baci_uart_thr & data_mask;           /* mask data into transmitter register */

    if ((baci_uart_tr == ENQ) && fast_timing) {         /* char is ENQ and fast timing? */
        baci_enq_seen = TRUE;                           /* set flag instead of transmitting */
        baci_enq_cntr = baci_enq_cntr + 1;              /* bump ENQ counter */
        recv_loop = FALSE;                              /* skip recv to allow time before ACK */

        tprintf (baci_dev, DEB_XFER, "Character ENQ absorbed internally, "
                                     "ENQ count = %d\n", baci_enq_cntr);
        }

    else {                                              /* character is not ENQ or not fast timing */
        baci_enq_cntr = 0;                              /* reset ENQ counter */

        if (is_attached) {                              /* attached to network? */
            status = tmxr_putc_ln (baci_ldsc,           /* transmit the character */
                                   baci_uart_tr);

            if (status == SCPE_OK)                      /* transmitted OK? */
                tprintf (baci_dev, DEB_XFER, "Character %s transmitted from the UART\n",
                                             fmt_char ((uint8) baci_uart_tr));

            else {
                tprintf (baci_dev, DEB_XFER, "Character %s transmission failed with status %d\n",
                                             fmt_char ((uint8) baci_uart_tr), status);

                if (status == SCPE_LOST)                /* if the line is not connected */
                    status = SCPE_OK;                   /*   then ignore the output */
                }
            }
        }

    if (status == SCPE_OK) {                            /* transmitted OK? */
        baci_uart_tr = CLEAR_R;                         /* clear transmitter register */

        if (IO_MODE == XMIT) {                          /* transmit mode? */
            baci_fcount = baci_fcount - 1;              /* decrement occupancy counter */
            baci_uart_thr = fifo_get ();                /* get next char into UART */
            update_status ();                           /* update FIFO status */
            }

        else                                            /* receive mode */
            baci_uart_thr = CLEAR_HR;                   /* clear holding register */

        xmit_loop = (fast_timing && ! baci_enq_seen     /* loop if fast mode and char not ENQ */
                      && baci_ldsc [0].xmte != 0);      /*   and buffer space is available */
        }

    else                                                /* otherwise transmission failed */
        xmit_loop = FALSE;                              /*   so drop out of the loop */
    }


/* Deferred reception */

if (recv_loop &&                                        /* ok to process? */
    baci_uart_rhr && (IO_MODE == RECV) &&               /*   and deferred char in RHR in recv mode? */
    (!baci_enq_seen || (baci_enq_cntr >= 2))) {         /*   and either no ENQ or at least 2nd ENQ? */

    baci_uart_rhr = baci_uart_rhr & ~IN_VALID;          /* clear valid bit */

    tprintf (baci_dev, DEB_XFER, "Deferred character %s processed\n",
             fmt_char ((uint8) baci_uart_rhr));

    fifo_put ((uint8) baci_uart_rhr);                   /* move deferred character to FIFO */
    baci_uart_rhr = CLEAR_HR;                           /* clear RHR */
    update_status ();                                   /* update FIFO status */
    }


/* Reception */

while (recv_loop) {                                     /* OK to process? */
    baci_uart_rr = tmxr_getc_ln (baci_ldsc);            /* get a new character */

    if (baci_uart_rr == 0)                              /* if there are no more characters available */
        break;                                          /*   then quit the reception loop */

    if (baci_uart_rr & SCPE_BREAK) {                    /* break detected? */
        baci_status = baci_status | IN_BREAK;           /* set break status */

        tprintf (baci_dev, DEB_XFER, "Break detected\n");
        }

    data_bits = 5 + (baci_cfcw & OUT_CHARSIZE);             /* calculate number of data bits */
    data_mask = (1 << data_bits) - 1;                       /* generate mask for data bits */
    baci_uart_rhr = (uint16) (baci_uart_rr & data_mask);    /* mask data into holding register */
    baci_uart_rr = CLEAR_R;                                 /* clear receiver register */

    tprintf (baci_dev, DEB_XFER, "Character %s received by the UART\n",
             fmt_char ((uint8) baci_uart_rhr));

    if (baci_term.flags & UNIT_CAPSLOCK)                    /* caps lock mode? */
        baci_uart_rhr = (uint16) toupper (baci_uart_rhr);   /* convert to upper case if lower */

    if (baci_cfcw & OUT_ECHO)                           /* echo wanted? */
        tmxr_putc_ln (baci_ldsc, baci_uart_rhr);        /* send it back */

    if ((IO_MODE == RECV) && !baci_enq_seen) {          /* receive mode and not ENQ/ACK? */
        fifo_put ((uint8) baci_uart_rhr);               /* put data in FIFO */
        baci_uart_rhr = CLEAR_HR;                       /* clear RHR */
        update_status ();                               /* update FIFO status (may set flag) */

        recv_loop = fast_timing && baci.flag_buffer == CLEAR;   /* loop if fast mode and no IRQ */
        }

    else {                                              /* xmit or ENQ/ACK, leave char in RHR */
        baci_uart_rhr = baci_uart_rhr | IN_VALID;       /* set character valid bit */
        recv_loop = FALSE;                              /* terminate loop */
        }
    }


/* Housekeeping */

if (recv_loop && baci_enq_seen) {                       /* OK to process and ENQ seen? */
    baci_enq_seen = FALSE;                              /* reset flag */

    tprintf (baci_dev, DEB_XFER, "Character ACK generated internally\n");

    fifo_put (ACK);                                     /* fake ACK from terminal */
    update_status ();                                   /* update FIFO status */
    }

if (is_attached)                                        /* attached to network? */
    tmxr_poll_tx (&baci_desc);                          /* output any accumulated chars */

if ((baci_uart_thr & IN_VALID) || baci_enq_seen ||      /* more to transmit? */
    tmxr_rqln (baci_ldsc))                              /*  or more to receive? */
    sim_activate (uptr, uptr->wait);                    /* reschedule service */
else
    tprintf (baci_dev, DEB_CMDS, "Terminal service stopped\n");

return status;
}


/* BACI line poll service.

   This service routine is used to poll for connections and incoming characters.
   If characters are available, the terminal I/O service routine is scheduled.
   It starts when the line is attached and stops when the line is detached.


   Implementation notes:

    1. Even though there is only one line, we poll for new connections
       unconditionally.  This is so that "tmxr_poll_conn" will report "All
       connections busy" to a second Telnet connection.  Otherwise, the user's
       client would connect but then would be silently unresponsive.
*/

static t_stat baci_poll_svc (UNIT *uptr)
{
if (tmxr_poll_conn (&baci_desc) >= 0)                   /* if new connection is established */
    baci_ldsc [0].rcve = 1;                             /*   then enable line to receive */

tmxr_poll_rx (&baci_desc);                              /* poll for input */

if (tmxr_rqln (baci_ldsc))                              /* chars available? */
    sim_activate (&baci_term, baci_term.wait);          /* activate I/O service */

if (uptr->wait == POLL_FIRST)                           /* first poll? */
    uptr->wait = hp_sync_poll (INITIAL);                /* initial synchronization */
else                                                    /* not first */
    uptr->wait = hp_sync_poll (SERVICE);                /* continue synchronization */

sim_activate (uptr, uptr->wait);                        /* continue polling */

return SCPE_OK;
}


/* Simulator reset routine */

static t_stat baci_reset (DEVICE *dptr)
{
io_assert (&baci_dev, ioa_POPIO);                       /* PRESET the device */

baci_ibuf = 0;                                          /* clear input buffer */
baci_obuf = 0;                                          /* clear output buffer */
baci_uart_rhr = CLEAR_HR;                               /* clear receiver holding register */

baci_enq_seen = FALSE;                                  /* reset ENQ seen flag */
baci_enq_cntr = 0;                                      /* clear ENQ counter */

baci_term.wait = service_time (baci_icw);               /* set terminal I/O time */

if (baci_term.flags & UNIT_ATT) {                       /* device attached? */
    baci_poll.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&baci_poll, baci_poll.wait);          /* start line poll immediately */
    }
else
    sim_cancel (&baci_poll);                            /* else stop line poll */

return SCPE_OK;
}


/* Attach line */

static t_stat baci_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status = SCPE_OK;

status = tmxr_attach (&baci_desc, uptr, cptr);          /* attach to socket */

if (status == SCPE_OK) {
    baci_poll.wait = POLL_FIRST;                        /* set up poll */
    sim_activate (&baci_poll, baci_poll.wait);          /* start line poll immediately */
    }
return status;
}


/* Detach line */

static t_stat baci_detach (UNIT *uptr)
{
t_stat status;

baci_ldsc [0].rcve = 0;                                 /* disable line reception */
sim_cancel (&baci_poll);                                /* stop line poll */
status = tmxr_detach (&baci_desc, uptr);                /* detach socket */
return status;
}


/* Local routines */


/* Master reset.

   This is the programmed card master reset, not the simulator reset routine.
   Master reset normally clears the UART registers.  However, if we are in "fast
   timing" mode, the receiver holding register may hold a deferred character.
   In this case, we do not clear the RHR, unless we are called from the
   simulator reset routine.

   The HP BACI manual states that master reset "Clears Service Request (SRQ)."
   An examination of the schematic, though, shows that it sets SRQ instead.
*/

static void master_reset (void)
{
baci_fput = baci_fget = 0;                              /* clear FIFO indexes */
baci_fcount = 0;                                        /* clear FIFO counter */
memset (baci_fifo, 0, sizeof (baci_fifo));              /* clear FIFO data */

baci_uart_thr = CLEAR_HR;                               /* clear transmitter holding register */

if (!(baci_term.flags & UNIT_FASTTIME))                 /* real time mode? */
    baci_uart_rhr = CLEAR_HR;                           /* clear receiver holding register */

baci_uart_tr = CLEAR_R;                                 /* clear transmitter register */
baci_uart_rr = CLEAR_R;                                 /* clear receiver register */

baci_uart_clk = 0;                                      /* clear UART clock */
baci_bcount   = 0;                                      /* clear break counter */

baci.control = CLEAR;                                   /* clear control */
baci.flag = baci.flag_buffer = SET;                     /* set flag and flag buffer */
baci.srq = SET;                                         /* set SRQ */
baci.lockout = SET;                                     /* set lockout flip-flop */

baci_edsiw = 0;                                         /* clear interrupt enables */
baci_dsrw = 0;                                          /* clear status reference */
baci_cfcw = baci_cfcw & ~OUT_ECHO;                      /* clear echo flag */
baci_icw = baci_icw & OUT_BAUDRATE;                     /* clear interface control */

if (baci_term.flags & UNIT_DIAG) {                      /* diagnostic mode? */
    baci_status = baci_status & ~IN_MODEM | IN_SPARE;   /* clear loopback status, set BA */
    baci_ldsc [0].xmte = 1;                             /* enable transmitter */
    }

return;
}


/* Update status.

   In diagnostic mode, several of the modem output lines are looped back to the
   input lines.  Also, CD is tied to BB (received data), which is presented on
   the TEST status bit via an inversion.  Echo mode couples BB to BA
   (transmitted data), which is presented on the SPARE status bit.

   If a modem line interrupt condition is present and enabled, the DEVINT status
   bit is set.  Other potential "standard" interrupt sources are the special
   character, break detected, and overrun/parity error bits.  If DCPC transfers
   are not selected, then the FIFO interrupts (buffer empty, half-full, and
   full) and the "data ready" condition (i.e., receive and character modes
   enabled and FIFO not empty) also produces an interrupt request.

   An interrupt request will set the card flag unless either the lockout or SRQ
   flip-flops are set.  SRQ will set if DCPC mode is enabled and there is room
   (transmit mode) or data (receive mode) in the FIFO.
*/

static void update_status (void)
{
if (baci_term.flags & UNIT_DIAG) {                      /* diagnostic mode? */
    baci_status = baci_status & ~IN_DIAG;               /* clear loopback flags */

    if (baci_icw & OUT_SXX)                             /* SCA to SCF and CF */
        baci_status = baci_status | IN_SXX | IN_CF;
    if ((baci_icw & OUT_CA) && (baci_fcount < 128))     /* CA to CC and CE */
        baci_status = baci_status | IN_CC | IN_CE;
    if (baci_icw & OUT_CD)                              /* CD to CB */
        baci_status = baci_status | IN_CB;
    else {
        baci_status = baci_status | IN_TEST;            /* BB is inversion of CD */
        if (baci_cfcw & OUT_ECHO)
            baci_status = baci_status | IN_SPARE;       /* BB couples to BA with echo */
        }

    if (!(baci_cfcw & OUT_ECHO) && (baci_uart_tr & 1))  /* no echo and UART TR set? */
        baci_status = baci_status | IN_SPARE;           /* BA to SPARE */
    }

if (baci_edsiw & (baci_status ^ baci_dsrw) & IN_MODEM)  /* device interrupt? */
    baci_status = baci_status | IN_DEVINT;              /* set flag */

if ((baci_status & IN_STDIRQ) ||                        /* standard interrupt? */
    !(baci_icw & OUT_DCPC) &&                           /* or under program control */
     (baci_status & IN_FIFOIRQ) ||                      /*   and FIFO interrupt? */
     (IO_MODE == RECV) &&                               /* or receiving */
     (baci_edsiw & OUT_ENCM) &&                         /*   and char mode */
     (baci_fget != baci_fput)) {                        /*   and FIFO not empty? */

    if (baci.lockout)                                   /* interrupt lockout? */
        tprintf (baci_dev, DEB_CMDS, "Lockout prevents flag set, status = %06o\n",
                 baci_status);

    else if (baci.srq)                                  /* SRQ set? */
        tprintf (baci_dev, DEB_CMDS, "SRQ prevents flag set, status = %06o\n",
                 baci_status);

    else {
        baci.flag_buffer = SET;                         /* set the flag buffer */
        io_assert (&baci_dev, ioa_ENF);                 /*   and flag flip-flops */

        tprintf (baci_dev, DEB_CMDS, "Flag and lockout set, status = %06o\n",
                 baci_status);
        }
    }

if ((baci_icw & OUT_DCPC) &&                            /* DCPC enabled? */
    ((IO_MODE == XMIT) && (baci_fcount < 128) ||        /*   and xmit and room in FIFO */
     (IO_MODE == RECV) && (baci_fcount > 0)))           /*   or recv and data in FIFO? */
    if (baci.lockout)                                   /* interrupt lockout? */
        tprintf (baci_dev, DEB_CMDS, "Lockout prevents SRQ set, status = %06o\n",
                 baci_status);

    else {
        baci.srq = SET;                                 /* set the SRQ flip-flop */
        io_assert (&baci_dev, ioa_SIR);                 /*   and assert SRQ */

        tprintf (baci_dev, DEB_CMDS, "SRQ set, status = %06o\n",
                 baci_status);
        }

return;
}


/* Calculate service time from baud rate.

   Service times are based on 1580 instructions per millisecond, which is the
   1000 E-Series execution speed.  The "external clock" rate uses the 9600 baud
   rate, as most real terminals were set to their maximum rate.

   Note that the RTE driver has a race condition that will trip if the service
   time is less than 1500 instructions.  Therefore, these times cannot be
   shortened arbitrarily.
*/

static int32 service_time (uint32 control_word)
{
/*           Baud Rates 0- 7 :   ext.,     50,     75,    110,  134.5,    150,   300,   600, */
/*           Baud Rates 8-15 :    900,   1200,   1800,   2400,   3600,   4800,  7200,  9600  */
static const int32 ticks [] = {  1646, 316000, 210667, 143636, 117472, 105333, 52667, 26333,
                                17556,  13667,   8778,   6583,   4389,   3292,  2194, 1646 };

return ticks [GET_BAUDRATE (control_word)];             /* return service time for indicated rate */
}


/* FIFO manipulation routines.

   The BACI is a half-duplex device that has a single 128-byte FIFO that is used
   for both transmitting and receiving.  Whether the FIFO is connected to the
   input or output of the UART is determined by the XMIT bit in word 4.  A
   separate 8-bit FIFO up/down counter is used to track the number of bytes
   available.  FIFO operations are complicated slightly by the UART, which is
   double-buffered.

   The FIFO is modeled as a circular 128-byte array.  Separate get and put
   indexes track the current data extent.  A FIFO character counter is used to
   derive empty, half-full, and full status indications, and counts greater than
   128 are possible.

   In the transmit mode, an OTA/B with word type 0 generates SI (shift in) to
   load the FIFO and increment the FIFO counter.  When the UART is ready for a
   character, THRE (UART transmitter holding register empty) and OR (FIFO output
   ready) generate THRL (transmitter holding register load) and SO (FIFO shift
   out) to unload the FIFO into the UART.  When transmission of the character
   over the serial line is complete, TRE (UART transmitter register empty)
   decrements the FIFO counter.

   In the receive mode, the UART sets DR (data received) when has obtained a
   character, which generates SI (FIFO shift in) to load the FIFO and increment
   the FIFO counter.  This also clocks PE (UART parity error) and IR (FIFO input
   ready) into the overrun/parity error flip-flop.  An LIA/B with control set
   and with OR (FIFO output ready) set, indicating valid data is available,
   generates SO (FIFO shift out) to unload the FIFO and decrement the FIFO
   counter.

   Presuming an empty FIFO and UART, double-buffering in the transmit mode means
   that the first byte deposited into the FIFO is removed and loaded into the
   UART transmitter holding register.  Even though the FIFO is actually empty,
   the FIFO counter remains at 1, because FIFO decrement does not occur until
   the UART actually transmits the data byte.  The intended mode of operation is
   to wait until the buffer-empty interrupt occurs, which will happen when the
   final character is transmitted from the UART, before switching the BACI into
   receive mode.  The counter will match the FIFO contents properly, i.e., will
   be zero, when the UART transmission completes.

   However, during diagnostic operation, FIFO testing will take this "extra"
   count into consideration.  For example, after a master reset, if ten bytes
   are written to the FIFO in transmit mode, the first byte will pass through to
   the UART transmitter holding register, and the next nine bytes will fill the
   FIFO.  The first byte read in receive mode will be byte 2, not byte 1; the
   latter remains in the UART.  After the ninth byte is read, OR (FIFO output
   ready) will drop, resetting the valid data flip-flop and inhibiting any
   further FIFO counter decrement pulses.  The counter will remain at 1 until
   another master reset is done.

   The same situation occurs in the RTE driver during ENQ/ACK handshakes.  The
   driver sets the card to transmit mode, sends an ENQ, waits for a short time
   for the character to "bubble through" the FIFO and into the UART transmitter
   holding register, and then switches the card to receive mode to await the
   interrupt from the reception of the ACK.  This is done to avoid the overhead
   of the interrupt after the ENQ is transmitted.  However, switching the card
   into receive mode before the ENQ is actually transmitted means that the FIFO
   counter will not decrement when that occurs, leaving the counter in an "off
   by one" configuration.  To remedy this, the driver does a master reset after
   the ACK is received.

   Therefore, for proper operation, we must simulate both the UART
   double-buffering and the decoupling of the FIFO and FIFO character counter.
*/


/* Get a character from the FIFO.

   In receive mode, getting a character from the FIFO decrements the character
   counter concurrently.  In transmit mode, the counter must not be decremented
   until the character is actually sent; in this latter case, the caller is
   responsible for decrementing.  Attempting to get a character when the FIFO is
   empty returns the last valid data and does not alter the FIFO indexes.

   Because the FIFO counter may indicate more characters than are actually in
   the FIFO, the count is not an accurate indicator of FIFO fill status.  We
   account for this by examining the get and put indexes.  If these are equal,
   then the FIFO is either empty or exactly full.  We differentiate by examining
   the FIFO counter and seeing if it is >= 128, indicating an (over)full
   condition.  If it is < 128, then the FIFO is empty, even if the counter is
   not 0.
*/

static uint16 fifo_get (void)
{
uint16 data;

data = baci_fifo [baci_fget];                           /* get character */

if ((baci_fget != baci_fput) || (baci_fcount >= 128)) { /* FIFO occupied? */
    if (IO_MODE == RECV)                                /* receive mode? */
        baci_fcount = baci_fcount - 1;                  /* decrement occupancy counter */

    tprintf (baci_dev, DEB_BUF, "Character %s get from FIFO [%d], "
                                "character counter = %d\n",
                                fmt_char ((uint8) data), baci_fget, baci_fcount);

    baci_fget = (baci_fget + 1) % FIFO_SIZE;            /* bump index modulo array size */

    if (baci_spchar [data])                             /* is it a special character? */
        data = data | IN_SPFLAG;                        /* set flag */

    data = data | IN_VALID;                             /* set valid flag in return */
    }

else                                                    /* FIFO empty */
    tprintf (baci_dev, DEB_BUF, "Attempted get on empty FIFO, "
                                "character count = %d\n", baci_fcount);

if (baci_fcount == 0)                                   /* count now zero? */
    baci_status = baci_status | IN_BUFEMPTY;            /* set buffer empty flag */

update_status ();                                       /* update FIFO status */

return data;                                            /* return character */
}


/* Put a character into the FIFO.

   In transmit mode, available characters are unloaded from the FIFO into the
   UART transmitter holding register as soon as the THR is empty.  That is,
   given an empty FIFO and THR, a stored character will pass through the FIFO
   and into the THR immediately.  Otherwise, the character will remain in the
   FIFO.  In either case, the FIFO character counter is incremented.

   In receive mode, characters are only unloaded from the FIFO explicitly, so
   stores always load the FIFO and increment the counter.
*/

static void fifo_put (uint8 ch)
{
uint32 index = 0;
t_bool pass_thru;

pass_thru = (IO_MODE == XMIT) &&                        /* pass thru if XMIT and THR empty */
            !(baci_uart_thr & IN_VALID);

if (pass_thru)                                          /* pass char thru to UART */
    baci_uart_thr = ch | IN_VALID;                      /* and set valid character flag */

else {                                                  /* RECV or THR occupied */
    index = baci_fput;                                  /* save current index */
    baci_fifo [baci_fput] = ch;                         /* put char in FIFO */
    baci_fput = (baci_fput + 1) % FIFO_SIZE;            /* bump index modulo array size */
    }

baci_fcount = baci_fcount + 1;                          /* increment occupancy counter */

if (pass_thru)
    tprintf (baci_dev, DEB_BUF, "Character %s put to UART transmitter holding register, "
                                "character counter = 1\n", fmt_char (ch));
else
    tprintf (baci_dev, DEB_BUF, "Character %s put to FIFO [%d], "
                                "character counter = %d\n", fmt_char (ch), index, baci_fcount);

if ((IO_MODE == RECV) && (baci_spchar [ch]))            /* receive mode and special character? */
    baci_status = baci_status | IN_SPCHAR;              /* set special char seen flag */

if (baci_fcount == 64)                                  /* FIFO half full? */
    baci_status = baci_status | IN_BUFHALF;

else if (baci_fcount == 128)                            /* FIFO completely full? */
    baci_status = baci_status | IN_BUFFULL;

else if (baci_fcount > 128)                             /* FIFO overrun? */
    baci_status = baci_status | IN_OVRUNPE;

update_status ();                                       /* update FIFO status */

return;
}


/* Clock the UART.

   In the diagnostic mode, the DIAG output is connected to the EXT CLK input.
   If the baud rate of the Interface Control Word is set to "external clock,"
   then raising and lowering the DIAG output will pulse the UART transmitter and
   receiver clock lines, initiating transmission or reception of serial data.
   Sixteen pulses are needed to shift one bit through the UART.

   The diagnostic hood ties CD to BB (received data), so bits presented to CD
   via the Interface Control Word can be clocked into the UART receiver register
   (RR).  Similarly, the UART transmitter register (TR) shifts data onto BA
   (transmitted data), and the hood ties BA to SPARE, so transmitted bits are
   presented to the SPARE bit in the status word.

   "baci_uart_clk" contains the number of clock pulses remaining for the current
   character transfer.  Calling this routine with "baci_uart_clk" = 0 initiates
   a transfer.  The value will be a multiple of 16 and will account for the
   start bit, the data bits, the optional parity bit, and the stop bits.  The
   transfer terminates when the count reaches zero (or eight, if 1.5 stop bits
   is selected during transmission).

   Every sixteen pulses when the lower four bits of the clock count are zero,
   the transmitter or receiver register will be shifted to present or receive a
   new serial bit.  The registers are initialized to all ones for proper
   handling of the stop bits.

   A break counter is maintained and incremented whenever a space (0) condition
   is seen on the serial line.  After 160 clock times (10 bits) of continuous
   zero data, the "break seen" status is set.

   This routine is not used in terminal mode.
*/

static void clock_uart (void)
{
uint32 uart_bits, data_bits, data_mask, parity, bit_low, i;

if (baci_uart_clk > 0) {                                /* transfer in progress? */
    bit_low = (baci_icw & OUT_CD);                      /* get current receive bit */

    if ((baci_uart_clk & 017) == 0)                     /* end of a bit? */
        if (IO_MODE == XMIT)                            /* transmit? */
            baci_uart_tr = baci_uart_tr >> 1;           /* shift new bit onto line */
        else                                            /* receive? */
            baci_uart_rr = (baci_uart_rr >> 1) &        /* shift new bit in */
                           (bit_low ? ~D16_SIGN : ~0u); /* (inverted sense) */

    if (bit_low) {                                      /* another low bit? */
        baci_bcount = baci_bcount + 1;                  /* update break counter */

        if (baci_bcount == 160) {                       /* break held long enough? */
            baci_status = baci_status | IN_BREAK;       /* set break flag */

            tprintf (baci_dev, DEB_XFER, "Break detected\n");
            }
        }

    else                                                /* high bit? */
        baci_bcount = 0;                                /* reset break counter */

    baci_uart_clk = baci_uart_clk - 1;                  /* decrement clocks remaining */

    if ((IO_MODE == XMIT) &&                            /* transmit mode? */
        ((baci_uart_clk == 0) ||                        /* and end of character? */
         (baci_uart_clk == 8) &&                        /*   or last stop bit */
         (baci_cfcw & OUT_STBITS) &&                    /*     and extra stop bit requested */
         ((baci_cfcw & OUT_CHARSIZE) == 0))) {          /*     and 1.5 stop bits used? */

        baci_uart_clk = 0;                              /* clear clock count */

        baci_fcount = baci_fcount - 1;                  /* decrement occupancy counter */
        baci_uart_thr = fifo_get ();                    /* get next char into THR */
        update_status ();                               /* update FIFO status */

        tprintf (baci_dev, DEB_XFER, "UART transmitter empty, "
                                     "holding register = %06o\n", baci_uart_thr);
        }

    else if ((IO_MODE == RECV) &&                       /* receive mode? */
             (baci_uart_clk == 0)) {                    /* and end of character? */

        data_bits = 5 + (baci_cfcw & OUT_CHARSIZE);     /* calculate number of data bits */
        data_mask = (1 << data_bits) - 1;               /* generate mask for data bits */

        uart_bits = data_bits +                         /* calculate UART bits as data bits */
                    ((baci_cfcw & OUT_PARITY) != 0) +   /*   plus parity bit if used */
                    ((baci_cfcw & OUT_STBITS) != 0);    /*   plus extra stop bit if used */

        baci_uart_rhr = (uint16) (baci_uart_rr >> (16 - uart_bits));    /* position data to right align */
        baci_uart_rr = CLEAR_R;                                         /* clear receiver register */

        tprintf (baci_dev, DEB_XFER, "UART receiver = %06o (%s)\n",
                 baci_uart_rhr, fmt_char ((uint8) (baci_uart_rhr & data_mask)));

        fifo_put ((uint8) (baci_uart_rhr & data_mask)); /* put data in FIFO */
        update_status ();                               /* update FIFO status */

        if (baci_cfcw & OUT_PARITY) {                   /* parity present? */
            data_mask = data_mask << 1 | 1;             /* widen mask to encompass parity */
            uart_bits = baci_uart_rhr & data_mask;      /* get data plus parity */

            parity = (baci_cfcw & OUT_PAREVEN) == 0;    /* preset for even/odd parity */

            for (i = 0; i < data_bits + 1; i++) {       /* calc parity of data + parity bit */
                parity = parity ^ uart_bits;            /* parity calculated in LSB */
                uart_bits = uart_bits >> 1;
                }

            if (parity & 1) {                           /* parity error? */
                baci_status = baci_status | IN_OVRUNPE; /* report it */

                tprintf (baci_dev, DEB_XFER, "Parity error detected\n");
                }
            }
        }
    }

if ((baci_uart_clk == 0) &&                             /* start of transfer? */
    ((IO_MODE == RECV) ||                               /* and receive mode */
     (baci_uart_thr & IN_VALID))) {                     /*   or character ready to transmit? */

    data_bits = 5 + (baci_cfcw & OUT_CHARSIZE);         /* calculate number of data bits */

    uart_bits = data_bits +                             /* calculate UART bits as data bits */
                ((baci_cfcw & OUT_PARITY) != 0) +       /*   plus parity bit if used */
                2 + ((baci_cfcw & OUT_STBITS) != 0);    /*   plus start/stop and extra stop if used */

    baci_uart_clk = 16 * uart_bits;                     /* calculate clocks pulses expected */

    if (IO_MODE == XMIT) {                              /* transmit mode? */
        data_mask = (1 << data_bits) - 1;               /* generate mask for data bits */
        baci_uart_tr = baci_uart_thr & data_mask;       /* mask data into holding register */

        if (baci_cfcw & OUT_PARITY) {                   /* add parity to this transmission? */
            uart_bits = baci_uart_tr;                   /* copy data bits */
            parity = (baci_cfcw & OUT_PAREVEN) == 0;    /* preset for even/odd parity */

            for (i = 0; i < data_bits; i++) {           /* calculate parity of data */
                parity = parity ^ uart_bits;            /* parity calculated in LSB */
                uart_bits = uart_bits >> 1;
                }

            data_mask = data_mask << 1 | 1;             /* extend mask for the parity bit */
            baci_uart_tr = baci_uart_tr |               /* include parity in transmission register */
                           (parity & 1) << data_bits;   /* (mask to parity bit and position it) */
            }

        baci_uart_tr = (~data_mask | baci_uart_tr) << 2 | 1;    /* form serial data stream */

        tprintf (baci_dev, DEB_XFER, "UART transmitter = %06o (%s), "
                                     "clock count = %d\n", baci_uart_tr & D16_MASK,
                                     fmt_char ((uint8) (baci_uart_thr & data_mask)),
                                     baci_uart_clk);
        }

    else {
        baci_uart_rr = CLEAR_R;                         /* clear receiver register */

        tprintf (baci_dev, DEB_XFER, "UART receiver empty, "
                                     "clock count = %d\n", baci_uart_clk);
        }
    }

return;
}
