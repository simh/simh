/* pdp11_kmc.c: KMC11-A with COMM IOP-DUP microcode Emulation
  ------------------------------------------------------------------------------


   Initial implementation 2002 by Johnny Eriksson <bygg@stacken.kth.se>

   Adapted to SIMH 3.? by Robert M. A. Jarratt in 2013

   Completely rewritten by Timothe Litt <litt@acm.org> for SimH 4.0 in 2013.
   Implements all DDCMP functions, although the DUP11 doesn't currently support half-duplex.

   DUP separated into separate devices, the DUPs were created by Mark Pizzolato.

   This code is Copyright 2002, 2013 by the authors,all rights reserved.
   It is licensed under the standard SimH license.
  ------------------------------------------------------------------------------

  Modification history:

  05-Jun-13  TL   Massive rewrite to split KMC/DUP, add missing functions, and
                  restructure so TOPS-10/20 & RSX are happy.  Support multiple
                  KMCs.
  14-Apr-13  RJ   Took original sources into latest source code.
  15-Feb-02  JE   Massive changes/cleanups.
  23-Jan-02  JE   Modify for version 2.9.
  17-Jan-02  JE   First attempt.
------------------------------------------------------------------------------*/
/*
 * Loose ends, known problems etc:
 *
 *   This implementation  should do both full and half duplex DDCMP, but half duplex needs to be tested.
 *
 *  DUP: Unexplained code to generate SYNC and RCVEN based on RTS transitions
 *       prevents RTS from working; using hacked version.
 * Select final speed limit.
 */

#if defined (VM_PDP10)                          /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                          /* VAX version */
#include "vax_defs.h"

#else                                           /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#define KMC_RDX     8

#include <assert.h>
#include "pdp11_dup.h"
#include "pdp11_ddcmp.h"

#define DIM(x) (sizeof(x)/sizeof((x)[0]))
#define UNUSED_ARG(x) (void)(x)

/* Configuration macros
 */

/* From host VM: DUP_LINES is the maximum number of DUP lines on the Unibus.
 * The KMC may not control all of them, but the DUP API also uses the index in
 * IO space to identify the line. Default to max KDP supported.
 *
 * Note: It is perfectly reasonable to have MANY more DUP_LINES than a single KMC
 * can support.  A configuration can have multiple KMCs, or DUPs that are controlled
 * directly by the OS.  DUP_LINES allocates space for the KMC to keep track of lines
 * that some KMC instance can POTENTIALLY control.
 */
#ifndef DUP_LINES
#define DUP_LINES (MAX_LINE +1)
#endif

/* Number of KMC devices possible. */
#ifndef KMC_UNITS
#define KMC_UNITS 1
#endif

/* Number of KMC devices initially enabled. */
#ifndef INITIAL_KMCS
#define INITIAL_KMCS 1
#endif

#if INITIAL_KMCS > KMC_UNITS
#undef INITIAL_KMCS
#define INITIAL_KMCS KMC_UNITS
#endif

#if INITIAL_KMCS == 0
#undef INITIAL_KMCS
#define INITIAL_KMCS 1
#define KMC_DIS DEV_DIS
#else
#define KMC_DIS 0
#endif

/* Define DUP_RXRESYNC to disable/enable RCVEN at the expected times.
 * This is used to resynch the receiver, but I'm not sure if it would
 * cause the emulated DUP to lose data...especially with QSYNC
#define DUP_RXRESYNC 1
 */

/* End of configuration */

/* Architectural structures and macros */

/* Maximum line speed
 * KDP limit was about 19,200 BPS.
 * Used to pace BD consumption.  If too fast, too many messages can
 * be outstanding and the OS will reject ACKs.  *TODO* Pick a final limit.
 */
#define MAX_SPEED (1*1000*1000)

#define DFLT_SPEED (19200)


/* Transmission (or reception) time of a buffer of n
 * characters at speed bits/sec.  8 bit chars (sync line)
 * result in microseconds.
 */
#define XTIME(n,speed) (((n)*8*1000*1000)/(speed))

/* Queue elements
 * A queue is a double-linked list of element headers.
 * The head of the queue is an element without a body.
 * A queue is empty when the only element in the queue is
 * the head.
 * Each queue has an asociated count of the elements in the
 * queue; this simplifies knowing its state.
 * Each queue also has a maximum length.
 *
 * Queues are manipulated with initqueue, insqueue, and remqueue.
 */
struct queuehdr {
  struct queuehdr     *next;
  struct queuehdr     *prev;
};
typedef struct queuehdr QH;

/* bits, SEL0 */

#define SEL0_RUN    0100000                     /* Run bit. */
#define SEL0_MRC    0040000                     /* Master clear. */
#define SEL0_CWR    0020000                     /* CRAM write. */
#define SEL0_SLU    0010000                     /* Step Line Unit. */
#define SEL0_LUL    0004000                     /* Line Unit Loop. */
#define SEL0_RMO    0002000                     /* ROM output. */
#define SEL0_RMI    0001000                     /* ROM input. */
#define SEL0_SUP    0000400                     /* Step microprocessor. */
#define SEL0_RQI    0000200                     /* Request input. */
#define SEL0_IEO    0000020                     /* Interrupt enable output. */
#define SEL0_IEI    0000001                     /* Interrupt enable input. */

/* bits, SEL2 */

#define SEL2_OVR    0100000                     /* Completion queue overrun. */
#define SEL2_V_LINE 8                           /* Line number assigned by host */
#define SEL2_LINE   (0177 << SEL2_V_LINE)
#define   MAX_LINE   017                        /* Maximum line number allowed in BASE_IN */
#define   MAX_ACTIVE (MAX_LINE+1)
#define   UNASSIGNED_LINE (MAX_ACTIVE+1)
#define SEL2_RDO    0000200                     /* Ready for output transaction. */
#define SEL2_RDI    0000020                     /* Ready for input transaction. */
#define SEL2_IOT    0000004                     /* I/O type, 1 = rx, 0 = tx. */
#define SEL2_V_CMD        0                     /* Command code */
#define SEL2_CMD    0000003                     /* Command code
                                                 * IN are commands TO the KMC
                                                 * OUT are command completions FROM the KMC.
                                                 */
#  define CMD_BUFFIN      0                     /*   BUFFER IN */
#  define CMD_CTRLIN      1                     /*   CONTROL IN */
#  define CMD_BASEIN      3                     /*   BASE IN */
#  define CMD_BUFFOUT     0                     /*   BUFFER OUT */
#  define CMD_CTRLOUT     1                     /*   CONTROL OUT */

#define SEL2_II_RESERVED  (SEL2_OVR | 0354)     /* Reserved: 15, 7:5, 3:2 */
/* bits, SEL4 */

#define SEL4_CI_POLL    0377                    /* DUP polling interval, 50 usec units */

#define SEL4_ADDR    0177777                    /* Generic: Unibus address <15:0> */

/* bits, SEL6 */

#define SEL6_V_CO_XAD    14                     /* Unibus extended address bits */
#define SEL6_CO_XAD    (3u << SEL6_V_CO_XAD)

/* BASE IN */
#define SEL6_II_DUPCSR 0017770                  /* BASE IN: DUP CSR <12:3> */

/* BUFFER IN */
#define SEL6_BI_ENABLE 0020000                  /* BUFFER IN: Assign after KILL */
#define SEL6_BI_KILL   0010000                  /* BUFFER IN: Return all buffers */

/* BUFFER OUT */
#define SEL6_BO_EOM    0010000                  /* BUFFER OUT: End of message */

/* CONTROL OUT event codes */
#define SEL6_CO_ABORT  006                      /* Bit stuffing rx abort */
#define SEL6_CO_HCRC   010                      /* DDCMP Header CRC error */
#define SEL6_CO_DCRC   012                      /* DDCMP Data CRC/ BS frame CRC */
#define SEL6_CO_NOBUF  014                      /* No RX buffer available */
#define SEL6_CO_DSRCHG 016                      /* DSR changed (Initially OFF) */
#define SEL6_CO_NXM    020                      /* NXM */
#define SEL6_CO_TXU    022                      /* Transmitter underrun */
#define SEL6_CO_RXO    024                      /* Receiver overrun */
#define SEL6_CO_KDONE  026                      /* Kill complete */

/* CONTROL IN modifiers */
#define SEL6_CI_V_DDCMP 15                      /* Run DDCMP vs. bit-stuffing */
#define SEL6_CI_DDCMP   (1u << SEL6_CI_V_DDCMP)
#define SEL6_CI_V_HDX   13                      /* Half-duplex */
#define SEL6_CI_HDX     (1u << SEL6_CI_V_HDX)
#define SEL6_CI_V_ENASS 12                      /* Enable secondary station address filter */
#define SEL6_CI_ENASS   (1u << SEL6_CI_V_ENASS)
#define SEL6_CI_V_NOCRC  9
#define SEL6_CI_NOCRC   (1u << SEL6_CI_V_NOCRC)
#define SEL6_CI_V_ENABLE 8
#define SEL6_CI_ENABLE  (1u << SEL6_CI_V_ENABLE)
#define SEL6_CI_SADDR    0377

/* Buffer descriptor list bits */

#define BDL_LDS    0100000                      /* Last descriptor in list. */
#define BDL_RSY    0010000                      /* Resync transmitter. */
#define BDL_XAD    0006000                      /* Buffer address bits 17 & 16. */
#define BDL_S_XAD  (16-10)                      /* Shift to position XAX in address */
#define BDL_EOM    0001000                      /* End of message. */
#define BDL_SOM    0000400                      /* Start of message. */

#define KMC_CRAMSIZE 1024                       /* Size of CRAM (microcode control RAM). */
#define KMC_DRAMSIZE 1024
#define KMC_CYCLETIME 300                       /* Microinstruction cycle time, nsec */

#define MAXQUEUE 2                              /* Number of bdls that can be queued for tx and rx
                                                 * per line. (KDP ucode limits to 2)
                                                 */

struct buffer_list {                            /* BDL queue elements  */
      QH     hdr;
      uint32 ba;
};
typedef struct buffer_list BDL;

 struct workblock {
   t_bool    first;
   uint32    bda;
   uint16    bd[3];
   uint16    rcvc;
   uint32    ba;
 };
 typedef struct workblock WB;

/* Each DUP in the system can potentially be assigned to a KMC.
 * Since the total number of DUPs is relatively small, and in
 * most configurations all DUPs will be assigned, a dup structure
 * is allocated for all possible DUPs.  This structure is common
 * to ALL KMCs; a given DUP is assigned to at most one.
 */

struct dupstate {
    int32  kmc;                                 /* Controlling KMC */
    uint8  line;                                /* OS-assigned line number */
    int32  dupidx;                              /* DUP API Number amongst all DUP11's on Unibus (-1 == unassigned) */
    int32  linkstate;                           /* Line Link Status (i.e. 1 when DCD/DSR is on, 0 otherwise */
 #define LINK_DSR     1
 #define LINK_SEL     2
    uint16 ctrlFlags;
    uint32 dupcsr;
    uint32 linespeed;                           /* Effective line speed (bps) */
    BDL    bdq[MAXQUEUE*2];                     /* Queued TX and RX buffer lists */
    QH     bdqh;                                /* Free queue */
    int32  bdavail;

    QH     rxqh;                                /* Receive queue from host */
    int32  rxavail;
    WB     rx;
    uint32 rxstate;
/* States.  Note that these are ordered; there are < comparisions */
#define RXIDLE   0
#define RXBDL    1
#define RXBUF    2
#define RXDAT    3
#define RXLAST   4
#define RXFULL   5
#define RXNOBUF  6

    uint8  *rxmsg;
    uint16 rxmlen;
    uint16 rxdlen;
    uint16 rxused;

    QH     txqh;                                /* Transmit queue from host */
    int32  txavail;
    WB     tx;
    uint32 txstate;
/* States.  Note that these are ordered; there are < comparisions */
#define TXIDLE   0
#define TXDONE   1
#define TXRTS    2
#define TXSOM    3
#define TXHDR    4
#define TXHDRX   5
#define TXDATA   6
#define TXDATAX  7
#define TXMRDY   8
#define TXRDY    9
#define TXACT   10
#define TXKILL  11
#define TXKILR  12

    uint8  *txmsg;
    size_t  txmsize, txslen, txmlen;
    };

typedef struct dupstate dupstate;

/* State for every DUP that MIGHT be controlled.
 * A DUP can be controlled by at most one KMC.
 */
static dupstate dupState[DUP_LINES] = {{ 0 }};

/* Flags defining sim_debug conditions. */

#define DF_CMD   00001                          /* Trace commands. */
#define DF_BFO   00002                          /* Trace buffers out */
#define DF_CTO   00004                          /* Trace control out */
#define DF_QUE   00010                          /* Trace internal queues */
#define DF_RGR   00020                          /* Register reads */
#define DF_RGW   00040                          /* Register writes. */
#define DF_INF   00100                          /* Info */
#define DF_ERR   00200                          /* Error halts */
#define DF_PKT   00400                          /* Errors in packet (use DUP pkt trace for pkt data */
#define DF_INT   01000                          /* Interrupt delivery */
#define DF_BUF   02000                          /* Buffer service */

static DEBTAB kmc_debug[] = {
    {"CMD",   DF_CMD},
    {"BFO",   DF_BFO},
    {"CTO",   DF_CTO},
    {"QUE",   DF_QUE},
    {"RGR",   DF_RGR},
    {"RGW",   DF_RGW},
    {"INF",   DF_INF},
    {"ERR",   DF_ERR},
    {"PKT",   DF_PKT},
    {"BUF",   DF_BUF},
    {"INT",   DF_INT},
    {0}
};

/* These count the total pending interrupts for each vector
 * across all KMCs.
 */

static int32 AintPending = 0;
static int32 BintPending = 0;

/* Per-KMC state */

/* To help make the code more readable, by convention the symbol 'k'
 * is the number of the KMC that is the target of the current operation.
 * The global state variables below have a #define of the short form
 * of each name.  Thus, instead of kmc_upc[kmcnum][j], write upc[j].
 * For this to work, k, a uint32 must be in scope and valid.
 *
 * k can be found in several ways:
 *  k is the offset into any of the tables. 
 * Given a UNIT pointer k = txup->unit_kmc.
 * The KMC assigned to control a DUP is stored it its dupstate.
 *  k = dupState[dupno]->kmc; (-1 if not controlled by any KMC)
 * The DUP associated with a line is stored in line2dup.
 *  k = line2dup[line]->kmc
 * From the DEVICE pointer, dptr->units lists all the UNITs, and the
 * number of units is dptr->numunits.
 * From a CSR address:
 *  k = (PA - dib.ba) / IOLN_KMC
 *
 * Note that the UNIT arrays are indexed [line][kmc], so pointer
 * math is not the best way to find k.  (TX line 0 is the public
 * UNIT for each KMC.)
 *
 */

/* Emulator error halt codes
 * These are mostl error conditions that produce undefined
 * results in the hardware.  To help with debugging, unique
 * codes are provided here.
 */

#define HALT_STOP      0                        /* Run bit cleared */
#define HALT_MRC       1                        /* Master clear */
#define HALT_BADRES    2                        /* Resume without initialization */
#define HALT_LINE      3                        /* Line number out of range */
#define HALT_BADCMD    4                        /* Undefined command received */
#define HALT_BADCSR    5                        /* BASE IN had non-zero MBZ */
#define HALT_RCVOVF    6                        /* Too many receive buffers assigned */
#define HALT_MTRCV     7                        /* Receive buffer descriptor has zero size */
#define HALT_XMTOVF    8                        /* Too many transmit buffers assigned */
#define HALT_XSOM      9                        /* Transmission didn't start with SOM */
#define HALT_XSOM2    10                        /* Data buffer didn't start with SOM */
#define HALT_BADUC    11                        /* No or unrecognized microcode loaded */

/* KMC event notifications are funneled through the small number of CSRs.
 * Since the CSRs may not be available when an event happens, events are
 * queued in these structures.  An event is represented by the values to
 * be exposed in BSEL2, BSEL4, and BSEL6.  
 *
 * Queue overflow is signalled by setting the overflow bit in the entry
 * at the tail of the completion queue at the time a new entry fails to
 * be inserted.  Note that the line number in that entry may not be
 * the line whose event was lost.  Effectively, that makes this a fatal
 * error.
 *
 * The KMC microcode uses a queue depth of 29.
 */

#define CQUEUE_MAX (29)

struct  cqueue {
  QH                  hdr;
  uint16              bsel2, bsel4, bsel6;
};
typedef struct cqueue CQ;

/* CSRs.  These are known as SELn as words and BSELn as bytes */
static uint16    kmc_sel0[KMC_UNITS];           /* CSR0 - BSEL 1,0 */
#define              sel0 kmc_sel0[k]
static uint16    kmc_sel2[KMC_UNITS];           /* CSR2 - BSEL 3,2 */
#define              sel2 kmc_sel2[k]
static uint16    kmc_sel4[KMC_UNITS];           /* CSR4 - BSEL 5,4 */
#define              sel4 kmc_sel4[k]
static uint16    kmc_sel6[KMC_UNITS];           /* CSR6 - BSEL 7,6 */
#define              sel6 kmc_sel6[k]

/* Microprocessor state - subset  exposed to the host */
static uint16    kmc_upc[KMC_UNITS];            /* Micro PC */
#define              upc kmc_upc[k]
static uint16    kmc_mar[KMC_UNITS];            /* Micro Memory Address Register */
#define              mar kmc_mar[k]
static uint16    kmc_mna[KMC_UNITS];            /* Maintenance Address Register */
#define              mna kmc_mna[k]
static uint16    kmc_mni[KMC_UNITS];            /* Maintenance Instruction Register */
#define              mni kmc_mni[k]
static uint16    kmc_ucode[KMC_UNITS][KMC_CRAMSIZE];
#define              ucode kmc_ucode[k]
static uint16    kmc_dram[KMC_UNITS][KMC_DRAMSIZE];
#define              dram kmc_dram[k]

static dupstate *kmc_line2dup[KMC_UNITS][MAX_ACTIVE];
#define              line2dup kmc_line2dup[k]

/* General state booleans */
static int       kmc_gflags[KMC_UNITS];         /* Miscellaneous gflags */
#define              gflags kmc_gflags[k]
#   define             FLG_INIT  000001         /* Master clear has been done once.
                                                 * Data structures trustworthy.
                                                 */
#   define             FLG_AINT  000002         /* Pending KMC "A" (INPUT) interrupt */
#   define             FLG_BINT  000004         /* Pending KMC "B" (OUTPUT) interrupt */
#   define             FLG_UCINI 000010         /* Ucode initialized, ucode structures and ints OK */

/* Completion queue elements, header  and freelist */
static CQ        kmc_cqueue[KMC_UNITS][CQUEUE_MAX];
#define              cqueue kmc_cqueue[k]

static QH        kmc_cqueueHead[KMC_UNITS];
#define              cqueueHead kmc_cqueueHead[k]
static int32     kmc_cqueueCount[KMC_UNITS];
#define              cqueueCount kmc_cqueueCount[k]

static QH        kmc_freecqHead[KMC_UNITS];
#define              freecqHead kmc_freecqHead[k]
static int32     kmc_freecqCount[KMC_UNITS];
#define              freecqCount kmc_freecqCount[k]

/* *** End of per-KMC state *** */

/* Forward declarations: simulator interface */


static t_stat kmc_reset(DEVICE * dptr);
static t_stat kmc_readCsr(int32* data, int32 PA, int32 access);
static t_stat kmc_writeCsr(int32 data, int32 PA, int32 access);
static void kmc_doMicroinstruction (int32 k, uint16 instr);
static t_stat kmc_txService(UNIT * txup);
static t_stat kmc_rxService(UNIT * rxup);

#if KMC_UNITS > 1
static t_stat kmc_setDeviceCount (UNIT *txup, int32 val, char *cptr, void *desc);
static t_stat kmc_showDeviceCount (FILE *st, UNIT *txup, int32 val, void *desc);
#endif
static t_stat kmc_setLineSpeed (UNIT *txup, int32 val, char *cptr, void *desc);
static t_stat kmc_showLineSpeed (FILE *st, UNIT *txup, int32 val, void *desc);
static t_stat kmc_showStatus (FILE *st, UNIT *up, int32 v, void *dp);

static t_stat kmc_help (FILE *st, struct sim_device *dptr,
                        struct sim_unit *uptr, int32 flag, char *cptr); 
static char *kmc_description (DEVICE *dptr);

/* Global data */

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                         /* calibrated delay */

static int32 kmc_AintAck (void);
static int32 kmc_BintAck (void);

#define IOLN_KMC        010

static DIB kmc_dib = {
    IOBA_AUTO,                                  /* ba - Base address */
    IOLN_KMC * INITIAL_KMCS,                    /* lnt - Length */
    &kmc_readCsr,                               /* rd - read IO */
    &kmc_writeCsr,                              /* wr - write IO */
    2 * INITIAL_KMCS,                           /* vnum - number of Interrupt vectors */
    IVCL (KMCA),                                /* vloc - vector locator */
    VEC_AUTO,                                   /* vec - auto */
    {&kmc_AintAck,                              /* ack - iack routines */
     &kmc_BintAck},
    IOLN_KMC,                                   /* IO space per unit */
};

/* One UNIT for each (possible) active line for transmit, and another for receive */
static UNIT tx_units[MAX_ACTIVE][KMC_UNITS]; /* Line 0 is primary unit.  txup references */
#define unit_kmc      u3
#define unit_line     u4
#define unit_htime    u5

/* Timers - in usec */

#define RXPOLL_DELAY   (1*1000)                 /* Poll for a new message */
#define RXBDL_DELAY    (10*1000)                /* Grace period for host to supply a BDL */
#define RXNEWBD_DELAY  (10)                     /* Delay changing descriptors */
#define RXSTART_DELAY  (50)                     /* Host provided BDL to receive start */


static UNIT rx_units[MAX_ACTIVE][KMC_UNITS];    /* Secondary unit, used for RX.  rxup references */

/* Timers - in usec */

#define TXSTART_DELAY  (10)                     /* TX BUFFER IN to TX start */
#define TXDONE_DELAY   (10)                     /* Completion to to poll for next bd */
#define TXCTS_DELAY    (100*1000)               /* Polling for CTS to appear */
#define TXDUP_DELAY    (1*1000*1000)            /* Wait for DUP to accept data (SNH) */

static BITFIELD kmc_sel0_decoder[] = {
    BIT (IEI),
    BITNCF (3),
    BIT (IEO),
    BIT (RQI),
    BITNCF (2),
    BIT (SUP),
    BIT (RMI),
    BIT (RMO),
    BIT (LUL),
    BIT (SLU),
    BIT (CWR),
    BIT (MRC),
    BIT (RUN),
    ENDBITS
};
static BITFIELD kmc_sel2_decoder[] = {
    BITF (CMD,2),
    BIT (IOT),
    BITNCF (1),
    BIT (RDI),
    BITNCF (2),
    BIT (RDO),
    BITFFMT (LINE,7,"%u"),
    BIT (CQOVF),
    ENDBITS
};
static REG kmc_reg[] = {
    { BRDATADF (SEL0, kmc_sel0, KMC_RDX, 16, KMC_UNITS, "Initialization/control", kmc_sel0_decoder) },
    { BRDATADF (SEL2, kmc_sel2, KMC_RDX, 16, KMC_UNITS, "Command/line", kmc_sel2_decoder) },
    { ORDATA (SEL4, kmc_sel4, 16) },
    { ORDATA (SEL6, kmc_sel6, 16) },
    { NULL },
    };

MTAB kmc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "ADDRESS",
        &set_vec, &show_vec, NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NMO, 0, "SPEED", "SPEED=dup=bps",
        &kmc_setLineSpeed, &kmc_showLineSpeed, NULL, "Line speed (bps)" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 1, "STATUS", NULL, NULL, &kmc_showStatus, NULL, "Display KMC status" },
#if KMC_UNITS > 1
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DEVICES", "DEVICES=n",
        &kmc_setDeviceCount, &kmc_showDeviceCount, NULL, "Display number of KMC devices enabled" },
#endif
    { 0 },
    };

DEVICE kmc_dev = {
    "KDP", 
    tx_units[0], 
    kmc_reg,                                    /* Register decode tables */
    kmc_mod,                                    /* Modifier table */
    INITIAL_KMCS,                               /* Number of units */
    KMC_RDX,                                    /* Address radix */
    13,                                         /* Address width: 18 - <17:13> are 1s, omits UBA */
    1,                                          /* Address increment */
    KMC_RDX,                                    /* Data radix */
    8,                                          /* Data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* Deposit routine */
    &kmc_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &kmc_dib,                                   /* context */
    DEV_UBUS | KMC_DIS                          /* Flags */
             | DEV_DISABLE
             | DEV_DEBUG,
    0,                                          /* debug control */
    kmc_debug,                                  /* debug flag table */
    NULL,                                       /* memory size routine */
    NULL,                                       /* logical name */
    &kmc_help,                                  /* help routine */
    NULL,                                       /* attach help routine */
    NULL,                                       /* help context */
    &kmc_description                            /* Device description routine */
};

/* Forward declarations: not referenced in simulator data */

static void kmc_masterClear(int32 k);
static void kmc_startUcode (int32 k);
static void kmc_dispatchInputCmd(int32 k);

/* Control functions */
static void kmc_baseIn (int32 k, dupstate *d, uint16 cmdsel2, uint8 line);
static void kmc_ctrlIn (int32 k, dupstate *d, int line);

/* Receive functions */
void kmc_rxBufferIn(dupstate *d, int32 ba, uint16 sel6v);
static void kdp_receive(int32 dupidx, int count);

/* Transmit functions */
static void kmc_txBufferIn(dupstate *d, int32 ba, uint16 sel6v);
static void kmc_txComplete (int32 dupidx, int status);
static t_bool kmc_txNewBdl(dupstate *d);
static t_bool kmc_txNewBd(dupstate *d);
static t_bool kmc_txAppendBuffer(dupstate *d);

/* Completions */
static void kmc_processCompletions (int32 k);

static void kmc_ctrlOut (int32 k, uint8 code, uint16 rx, uint8 line, uint32 bda);
static void kmc_modemChange (int32 dupidx);
static t_bool kmc_updateDSR (dupstate *d);

static t_bool kmc_bufferAddressOut (int32 k, uint16 flags, uint16 rx, uint8 line, uint32 bda);

/* Buffer descriptor list utilities */
static int32 kmc_updateBDCount(uint32 bda, uint16 *bd);

/* Errors */
static void kmc_halt (int32 k, int error);

/* Interrupt management */
static void kmc_updints(int32 k);
static int32 kmc_AintAck (void);
static int32 kmc_BintAck (void);

/* DUP access */

/* Debug support */
static t_bool kmc_printBufferIn (int32 k, DEVICE *dev, uint8 line, t_bool rx,
                                 int32 count, int32 ba, uint16 sel6v);
static t_bool kmc_printBDL(int32 k, uint32 dbits, DEVICE *dev, uint8 line, int32 ba, int prbuf);

/* Environment */
static const char *kmc_verifyUcode (int32 k);

/* Queue management */
static void initqueue (QH *head, int32 *count, int32 max, void *list, size_t size);
/* Convenience for initqueue() calls */
#    define MAX_LIST_SIZE(q)                    DIM(q),    (q),       sizeof(q[0])
#    define INIT_HDR_ONLY                       0,         NULL,      0

static t_bool insqueue (QH *entry, QH *pred, int32 *count, int32 max);
static void *remqueue (QH *entry, int32 *count);


/*
 * Reset KMC device.  This resets ALL the KMCs:
 */

static t_stat kmc_reset(DEVICE* dptr) {
    int32 k;
    size_t i;

    if (sim_switches & SWMASK ('P')) {
        for (i = 0; i < DIM (dupState); i++) {
            dupstate *d = &dupState[i];
            d->kmc = -1;
            d->dupidx = -1;
            d->linespeed = DFLT_SPEED;
        }
    }

    for (k = 0; ((uint32)k) < kmc_dev.numunits; k++) {
        sim_debug (DF_INF, dptr, "KMC%d: Reset\n", k);

        /* One-time initialization of UNITs, one/direction/line */
        for (i = 0; i < MAX_ACTIVE; i++) {
            if (!tx_units[i][k].action) {
                memset (&tx_units[i][k], 0, sizeof tx_units[0][0]);
                memset (&rx_units[i][k], 0, sizeof tx_units[0][0]);

                tx_units[i][k].action = &kmc_txService;
                tx_units[i][k].flags = 0;
                tx_units[i][k].capac = 0;
                tx_units[i][k].unit_kmc = k;
                tx_units[i][k].unit_line = i;

                rx_units[i][k].action = &kmc_rxService;
                rx_units[i][k].flags = 0;
                rx_units[i][k].capac = 0;
                rx_units[i][k].unit_kmc = k;
                rx_units[i][k].unit_line = i;
            }
        }
        kmc_masterClear (k); /* If previously running, halt */

        if (sim_switches & SWMASK ('P'))
            gflags &= ~FLG_INIT;

        if (!(gflags & FLG_INIT)) { /* Power-up reset */
            sel0 = 0x00aa;
            sel2 = 0xa5a5;
            sel4 = 0xdead;
            sel6 = 0x5a5a;
            memset (ucode, 0xcc, sizeof ucode);
            memset (dram, 0xdd, sizeof dram);
            gflags |= FLG_INIT;
            gflags &= ~FLG_UCINI;
        }
    }

    return auto_config (dptr->name, ((dptr->flags & DEV_DIS)? 0: dptr->numunits));  /* auto config */
}

   
/*
 * Read registers:
 */

static t_stat kmc_readCsr (int32* data, int32 PA, int32 access) {
    int32 k;

    k = ((PA-((DIB *)kmc_dev.ctxt)->ba) / IOLN_KMC);

    switch ((PA >> 1) & 03) {
    case 00:
        *data = sel0;
        break;
    case 01:
        *data = sel2;
        break;
    case 02:
        if ((sel0 & SEL0_RMO) && (sel0 & SEL0_RMI)) {
            *data = mni;
        } else {
            *data = sel4;
        }
        break;
    case 03:
        if (sel0 & SEL0_RMO) {
            if (sel0 & SEL0_RMI) {
                *data = mni;
            } else {
                *data = ucode[mna];
            }
        } else {
            *data = sel6;
        }
        break;
    }
    
    sim_debug (DF_RGR, &kmc_dev, "KMC%u CSR rd: addr=0%06o  SEL%d, data=%06o 0x%04x access=%d\n",
              k, PA, PA & 07, *data, *data, access);
    return SCPE_OK;
}

/*
 * Write registers:
 */

static t_stat kmc_writeCsr (int32 data, int32 PA, int32 access) {
    uint32 changed;
    int reg = PA & 07;
    int sel = (PA >> 1) & 03;
    int32 k;

    k = ((PA-((DIB *)kmc_dev.ctxt)->ba) / IOLN_KMC);

    if (access == WRITE) {
        sim_debug (DF_RGW, &kmc_dev, "KMC%u CSR wr: addr=0%06o  SEL%d, data=%06o 0x%04x\n",
                  k, PA, reg, data, data);
    } else {
        sim_debug (DF_RGW, &kmc_dev, "KMC%u CSR wr: addr=0%06o BSEL%d, data=%06o 0x%04x\n",
                  k, PA, reg, data, data);
    }

    switch (sel) {
    case 00: /* SEL0 */
        if (access == WRITEB) {
            data = (PA & 1)
                ? (((data & 0377) << 8) | (sel0 & 0377))
                : ((data & 0377) | (sel0 & 0177400));
        }
        changed = sel0 ^ data;
        sel0 = (uint16)data;
        if (sel0 & SEL0_MRC) {
            if (((sel0 & SEL0_RUN) == 0) && (changed & SEL0_RUN)) {
                kmc_halt (k, HALT_MRC);
            }
            kmc_masterClear(k);
            break;
        }
        if (!(data & SEL0_RUN)) {
            if (data & SEL0_RMO) {
                if ((changed & SEL0_CWR) && (data & SEL0_CWR)) { /* CWR rising */
                    ucode[mna] = sel6;
                    sel4 = ucode[mna];          /* Copy contents to sel 4 */
                }
            } else {
                if (changed & SEL0_RMO) {       /* RMO falling */
                    sel4 = mna;
                }
            }
            if ((data & SEL0_RMI) && (changed & SEL0_RMI)) {
                mni = sel6;
            }
            if ((data & SEL0_SUP) && (changed & SEL0_SUP)) {
                if (data & SEL0_RMI) {
                    kmc_doMicroinstruction(k, mni);
                } else {
                    kmc_doMicroinstruction(k, ucode[upc++]);
                }
            }
        }
        if (changed & SEL0_RUN) {	            /* Changing the run bit? */
            if (sel0 & SEL0_RUN) {
                kmc_startUcode (k);
            } else {
                kmc_halt (k, HALT_STOP);
            }
        }
        if (changed & (SEL0_IEI | SEL0_IEO))
            kmc_updints (k);

        if ((sel0 & SEL0_RUN)) {
            if ((sel0 & SEL0_RQI) && !(sel2 & SEL2_RDO))
                sel2 = (sel2 & 0xFF00) | SEL2_RDI; /* Clear command bits too */
            kmc_updints(k);
	    }
        break;
    case 01: /* SEL2 */
        if (access == WRITEB) {
            data = (PA & 1)
                ? (((data & 0377) << 8) | (sel2 & 0377))
                : ((data & 0377) | (sel2 & 0177400));
        }
        if (sel0 & SEL0_RUN) {
            /* Handle commands in and out.
             * Output takes priority, but after servicing an
             * output, an input request must be serviced even
             * if another output command is ready.
             */
            if ((sel2 & SEL2_RDO) && (!(data & SEL2_RDO))) {
                sel2 = (uint16)data;            /* RDO clearing, RDI can't be set */
                if (sel0 & SEL0_RQI) {
                    sel2 = (sel2 & 0xFF00) | SEL2_RDI;
                    kmc_updints(k);
                } else
                    kmc_processCompletions(k);
            } else {
                if ((sel2 & SEL2_RDI) && (!(data & SEL2_RDI))) {
                    sel2 = (uint16)data;        /* RDI clearing,  RDO can't be set */
                    kmc_dispatchInputCmd(k);    /* Can set RDO */
                    if ((sel0 & SEL0_RQI) && !(sel2 & SEL2_RDO))
                        sel2 = (sel2 & 0xFF00) | SEL2_RDI;
                    kmc_updints(k);
                } else {
                    sel2 = (uint16)data;
                }
            }
        } else {
            sel2 = (uint16)data;
        }
        break;
    case 02: /* SEL4 */
        mna = data & (KMC_CRAMSIZE -1);
        sel4 = (uint16)data;
        break;
    case 03: /* SEL6 */
        if (sel0 & SEL0_RMI) {
            mni = (uint16)data;
        }
        sel6 = (uint16)data;
        break;
    }

    return SCPE_OK;
}

/* Microengine simulator
 *
 * This simulates a small subset of the KMC11-A's instruction set.
 * This is necessary because the operating systems force microprocessor
 * to execute these instructions in order to load (and extract) state.
 * These are implemented here so that the OS tools are happy, and to
 * give their error logging tools something to do.
 */

static void kmc_doMicroinstruction (int32 k, uint16 instr) {
 switch (instr) {
 case 0041222: /* MOVE <MEM><BSEL2> */
	sel2 = (sel2 & ~0xFF) |  (dram[mar%KMC_DRAMSIZE] & 0xFF);
	break;
 case 0055222: /* MOVE <MRM><BSEL2><MARINC> */
	sel2 = (sel2 & ~0xFF) |  (dram[mar%KMC_DRAMSIZE] & 0xFF);
	mar = (mar +1)%KMC_DRAMSIZE;
	break;
 case 0122440: /* MOVE <BSEL2><MEM> */
        dram[mar%KMC_DRAMSIZE] = sel2 & 0xFF;
	break;
 case 0136440: /* MOVE <BSEL2><MEM><MARINC> */
        dram[mar%KMC_DRAMSIZE] = sel2 & 0xFF;
	mar = (mar +1)%KMC_DRAMSIZE;
	break;
 case 0121202: /* MOVE <NPR><BSEL2> */
 case 0021002: /* MOVE <IBUS 0><BSEL2> */
	sel2 = (sel2 & ~0xFF) |  0;   
	break;
 default:
   if ((instr & 0160000) == 0000000) { /* MVI */
     switch (instr & 0174000) {
     case 0010000: /* Load MAR LOW */
       mar = (mar & 0xFF00) | (instr & 0xFF);
       break;
     case 0004000: /* Load MAR HIGH */
       mar = (mar & 0x00FF) | ((instr & 0xFF)<<8);
       break;
     default: /* MVI NOP / MVI INC */
       break;
     }
     break;
   }
   if ((instr & 0163400) == 0100400) {
     upc = ((instr & 0014000) >> 3) | (instr & 0377);
     sim_debug (DF_INF, &kmc_dev, "KMC%u microcode start uPC %04o\n", k, upc);
     break;
   }
 }
 return;
}

/* Transmitter service.
 *
 * Each line has a TX unit.  This thread handles message transmission.
 *
 * A host-supplied buffer descriptor list is walked and the data handed off
 * to the line.  Because the DUP emulator can't abort a transmission in progress,
 * and wants to receive formatted messages, the data from host memory is
 * accumulated into an intermediate buffer.
 *
 * TX BUFFER OUT completions are delivered as the data is retrieved, which happens
 * at approximately the 'line speed'; this does not indicate that the data is on
 * the wire.  Only a DDCMP ACK confirms receipt.
 *
 * TX CONTROL OUT completions are generated for errors.
 *
 * The buffer descriptor flags for resync, start and end of message are obeyed.
 *
 * The transmitter asserts RTS at the beginning of a message, and clears it when
 * idle.
 *
 * No more than one message is on the wire at any time.
 *
 * The DUP may be speed limited, delivering transmit completions at line speed.
 * In that case, the KDP and DUP limits will interact.
 *
 * This thread wakes:
 *   o When a new buffer descriptor is delivered by the host
 *   o When a state machine timeout expires
 *   o When a DUP transmit completion occurs.
 * 
 */

static t_stat kmc_txService (UNIT *txup) {
    int32 k = txup->unit_kmc;
    dupstate *d = line2dup[txup->unit_line];
    t_bool more;

    assert ((k >= 0) && (k < (int32) kmc_dev.numunits) && (d->kmc == k) &&
             (d->line == txup->unit_line));

    /* Provide the illusion of progress. */
    upc = 1 + ((upc + 1) % (KMC_CRAMSIZE -1));

    /* Process one buffer descriptor per cycle
     * CAUTION: this switch statement uses fall-through cases.
     */

/* State control macros.
 *
 * All exit the current statement (think "break" or "continue")
 *
 * Change to newstate and process immediately
 */

#define TXSTATE(newstate) {    \
    d->txstate = newstate;     \
    more = FALSE;              \
    continue;            }

/* Change to newstate after a delay of time. */
#define TXDELAY(newstate,time) {       \
                txup->wait = time;     \
                d->txstate = newstate; \
                more = FALSE;          \
                break;         }

/* Stop processing an return to IDLE.
 * This will NOT check for more work - it is used
 * primarily in error conditions.
 */
#define TXSTOP {             \
        d->txstate = TXIDLE; \
        more = FALSE;        \
        break; }

    do {
        more = TRUE;

        if (d->txstate > TXRTS) {
            sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: transmit service %s state = %u\n",
                       k, txup->unit_line, (more? "continued": "activated"), d->txstate);
        }

        switch (d->txstate) {
        case TXDONE:                            /* Resume from completions */
            d->txstate = TXIDLE;

        case TXIDLE:                            /* Check for new BD */
            if (!kmc_txNewBdl(d)) {
                TXSTOP;
            }
            d->txmlen = 
                d->txslen = 0;
            d->txstate = TXRTS;

            if (dup_set_RTS (d->dupidx, TRUE) != SCPE_OK) {
                sim_debug (DF_CTO, &kmc_dev, "KMC%u line %u: dup: %d DUP CSR NXM\n",
                           k, d->line, d->dupidx);
                kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, 0);
            }

            
        case TXRTS:                             /* Wait for CTS */
            if (dup_get_CTS (d->dupidx) <= 0) {
                TXDELAY (TXRTS, TXCTS_DELAY);
            }

            d->txstate = TXSOM;

            sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: transmitting bdl=%06o\n",
                       k, txup->unit_line, d->tx.bda);
        case TXSOM:                             /* Start assembling a message */
            if (!(d->tx.bd[2] & BDL_SOM)) {
                sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: TX BDL not SOM\n", k, d->line);
                kmc_halt (k, HALT_XSOM);
                TXSTOP;
            }

            if (d->tx.bd[2] & BDL_RSY) {
                static const uint8 resync[8] = { DDCMP_SYN, DDCMP_SYN, DDCMP_SYN, DDCMP_SYN,
                                                 DDCMP_SYN, DDCMP_SYN, DDCMP_SYN, DDCMP_SYN, };
                if (!d->txmsg || (d->txmsize < sizeof (resync))) {
                    d->txmsize = 8 + sizeof (resync);
                    d->txmsg = (uint8 *)realloc (d->txmsg, 8 + sizeof (resync));
                }
                memcpy (d->txmsg, resync, sizeof (resync));
                d->txmlen =
                    d->txslen = sizeof (resync);
            }

            d->txstate = TXHDR;

        case TXHDR:                             /* Assemble the header */
            if (!kmc_txAppendBuffer(d)) {       /* NXM - Try next list */
                TXDELAY (TXDONE, TXDONE_DELAY);
            }
            TXDELAY (TXHDRX, XTIME (d->tx.bd[1], d->linespeed));

        case TXHDRX:                            /* Report header descriptor done */
            if (!kmc_bufferAddressOut (k, 0, 0, d->line, d->tx.bda)) {
                TXDELAY (TXDONE, TXDONE_DELAY);
            }
            if (!(d->tx.bd[2] & BDL_EOM)) {
                if (kmc_txNewBd(d)) {
                    TXSTATE (TXHDR);
                }
                /* Not EOM, no more BDs - underrun or NXM.  In theory
                 * this should have waited on char time before declaring
                 * underrun, but no sensible OS would live that dangerously.
                 */
                TXDELAY (TXDONE, TXDONE_DELAY);
            }

            /* EOM. Control messages are always complete */
            if (d->txmsg[d->txslen+0] == DDCMP_ENQ) {
                TXSTATE (TXRDY);
            }

            /* EOM expecting data to follow.
             * However, if the OS computes and includes HRC in a data/MOP message, this can
             * be the last descriptor.  In that case, this is EOM.
             */ 
            if (d->tx.bd[2] & BDL_LDS) {
                TXSTATE (TXMRDY);
                break;
            }

            /* Data sent in a separate descriptor */

            if (!kmc_txNewBd(d)) {
                TXDELAY (TXDONE, TXDONE_DELAY);
            }

            if (!(d->tx.bd[2] & BDL_SOM)) {
                kmc_halt (k, HALT_XSOM2);
                sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: TX BDL not SOM\n", k, d->line);
                TXSTOP;
            }
            d->txstate = TXDATA;

        case TXDATA:                            /* Assemble data/maint payload */
            if (!kmc_txAppendBuffer(d)) {       /* NXM */
                TXDELAY (TXDONE, TXDONE_DELAY);
            }
            TXDELAY (TXDATAX, XTIME (d->tx.bd[1], d->linespeed));

        case TXDATAX:                           /* Report BD completion */
            if (!kmc_bufferAddressOut (k, 0, 0, d->line, d->tx.bda)) {
                TXDELAY (TXDONE, TXDONE_DELAY);
            }
            if (d->tx.bd[2] & BDL_EOM) {
                TXSTATE (TXRDY);
            }
            if (!kmc_txNewBd(d)) {
                TXDELAY (TXDONE, TXDONE_DELAY);
            }
            TXSTATE (TXDATA);

            /* These states hand-off the message to the DUP.
             * txService suspends until transmit complete.
             * Note that txComplete can happen within the calls to the DUP.
             */
        case TXMRDY:                            /* Data with OS-embedded HCRC */
            d->txstate = TXACT; 
            assert (d->txmsg[d->txslen + 0] != DDCMP_ENQ);
            assert (((d->txmlen - d->txslen) > 8) &&    /* Data, length should match count */
                    (((size_t)(((d->txmsg[d->txslen + 2] & 077) << 8) | d->txmsg[d->txslen + 1])) ==
                                                             (d->txmlen - (d->txslen + 8))));
            if (!dup_put_msg_bytes (d->dupidx, d->txmsg + d->txslen, d->txmlen - d->txslen, TRUE, TRUE)) {
                sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
                TXDELAY (TXMRDY, TXDUP_DELAY);
            }
            more = FALSE;
            break;

        case TXRDY:                             /* Control or DATA with KDP-CRCH */
            d->txstate = TXACT;                 /* Note that DUP can complete before returning */
            if (d->txmsg[d->txslen + 0] == DDCMP_ENQ) { /* Control message */
                assert ((d->txmlen - d->txslen) == 6);
                if (!dup_put_msg_bytes (d->dupidx, d->txmsg, d->txslen + 6, TRUE, TRUE)) {
                    sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
                    TXDELAY (TXRDY, TXDUP_DELAY);
                }
                more = FALSE;
                break;
            }

            assert (((d->txmlen - d->txslen) > 6) &&    /* Data, length should match count */
                    (((size_t)(((d->txmsg[d->txslen + 2] & 077) << 8) | d->txmsg[d->txslen + 1])) ==
                                                             (d->txmlen - (d->txslen + 6))));
            if (!dup_put_msg_bytes (d->dupidx, d->txmsg, d->txslen + 6, TRUE, TRUE)) {
                sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
                TXDELAY (TXRDY, TXDUP_DELAY);
            }
            if (!dup_put_msg_bytes (d->dupidx, d->txmsg + d->txslen + 6, d->txmlen - (d->txslen + 6), FALSE, TRUE)) {
                sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
                 TXDELAY (TXRDY, TXDUP_DELAY);
            }
            more = FALSE;
            break;

            /* Active should never be reached as txService is not
             * scheduled while transmission is in progress.
             * txComplete will reset txstate based on the final descriptor.
             */
        default:
        case TXACT:
            sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: kmc_txService called while active\n", k, d->line);
            TXSTOP;
        }
    } while (more);
#undef TXSTATE
#undef TXDELAY
#undef TXSTOP

    if (d->txstate == TXIDLE) {
        assert (!d->txavail);
        if (dup_set_RTS (d->dupidx, FALSE) != SCPE_OK) {
            sim_debug (DF_CTO, &kmc_dev, "KMC%u line %u: dup: %d DUP CSR NXM\n",
                       k, d->line, d->dupidx);
            kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, 0);
        }
    } else {
        if (d->txstate != TXACT)
            sim_activate_after(txup, txup->wait);
    }
    return SCPE_OK;
}

/* Receiver service
 *
 * Each line has an RX unit.  This service thread accepts incoming
 * messages from the DUP, and delivers the data into buffer descriptors
 * provided by the host.
 *
 * Polling is not necessary if the DUP provides input notification; otherwise
 * it's at a rate of a few character times at 19,200 - the maximum line speed.
 *
 * Once a message has been accepted, the thread looks for a host-provided
 * buffer descriptor.  If one doesn't appear in a reasonable time (in hardware,
 * it's one character time - this code is more generous), the message is dropped
 * and a RX CONTROL OUT is issued.
 *
 * Then the message is parsed, validated and delivered to the host buffer.
 *
 * Any CRC errors generate RX CONTROL OUTs.  RX BUFFER OUTs are generated for each
 * descriptor filled, and at of message for the last descriptor used.
 *
 * If the line is configured for Secondary Station address matching, the
 * message is discarded if the address does not match (and the CRC validated).
 *
 * After generating a RX BUFFER OUT, the thread suspends prior to filling the
 * next descriptor.
 *
 * If reception is killed, the RX state machine is reset by the RX BUFFER IN.
 *
 * This thread wakes:
 *   o When a RX BUFFER IN delivers a buffer
 *   o When timeouts between states expire.
 *   o When the DUP notifies it of a new message received.
 */

static t_stat kmc_rxService (UNIT *rxup) {
    int32 k = rxup->unit_kmc;
    dupstate *d = line2dup[rxup->unit_line];
    BDL *bdl;
    t_stat r;
    uint16 xrem, seglen;

    assert ((k >= 0) && (k < (int32) kmc_dev.numunits) && (d->kmc == k) &&
             (d->line == rxup->unit_line));

    if (d->rxstate > RXBDL) {
        sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: receive service activated state = %u\n",
                   k, rxup->unit_line, d->rxstate);
    }

    /* Provide the illusion of progress. */
    upc = 1 + ((upc + 1) % (KMC_CRAMSIZE -1));

    rxup->wait = RXPOLL_DELAY;

    /* CAUTION: This switch statement uses fall-through cases 
     *
     * The KILL logic in kmc_rxBufferIn tracks these states.
     */

    switch (d->rxstate) {
    case RXIDLE:
        rxup->wait = RXPOLL_DELAY;

        r = dup_get_packet (d->dupidx, (const uint8 **)&d->rxmsg, &d->rxmlen);
        if (r == SCPE_LOST) {
            kmc_updateDSR (d);
            break;
        }
        if ((r != SCPE_OK) || (d->rxmsg == NULL)) {  /* No packet? */
            rxup->wait = tmxr_poll;
            break;
        }

        if (!(d->ctrlFlags & SEL6_CI_ENABLE)) {
            break;
        }

        while (d->rxmlen && (d->rxmsg[0] == DDCMP_SYN)) {
            d->rxmsg++;
            d->rxmlen--;
        }
        if (d->rxmlen < 8) {
            break;
        }

        if (!((d->rxmsg[0] == DDCMP_SOH) ||
              (d->rxmsg[0] == DDCMP_ENQ) ||
              (d->rxmsg[0] == DDCMP_DLE))) {

         /* Toggling RCVEN causes the DUP receiver to resync.
          */
#ifdef DUP_RXRESYNC
            dup_set_RCVEN (d->dupidx, FALSE);
            dup_set_RCVEN (d->dupidx, TRUE);
#endif
            break;
        }

        d->rxstate = RXBDL;
        d->rxused   = 0;

        if (DEBUG_PRS (kmc_dev)) {
            if (d->rxmsg[0] == DDCMP_ENQ) {
                static const char *const ctlnames [] = {
                    "00", "ACK", "NAK", "REP", "04", "05", "STRT", "STACK" };
                uint8 type = d->rxmsg[1];

                sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: receiving %s\n",
                           k, rxup->unit_line, 
                           ((type >= DIM (ctlnames))? "UNKNOWN" : ctlnames[type]));
            } else {
                sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: receiving %s len=%u\n",
                           k, rxup->unit_line,
                           ((d->rxmsg[0] == DDCMP_SOH)? "DATA" : "MAINT"), d->rxmlen);
            }
        }

    case RXBDL:
        if (!(bdl = (BDL *)remqueue(d->rxqh.next, &d->rxavail))) {
            rxup->wait = RXBDL_DELAY;
            d->rxstate = RXNOBUF;
            break;
        }
        d->rx.bda = bdl->ba;
        ASSURE (insqueue (&bdl->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));

        sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: receiving bdl=%06o\n",
                   k, rxup->unit_line, d->rx.bda);

        if (Map_ReadW (d->rx.bda, 3*2, d->rx.bd)) {
            kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, d->line, d->rx.bda);
            d->rxstate = RXIDLE;
            break;
        }            
        d->rxstate = RXBUF;

    case RXBUF:
        d->rx.ba = ((d->rx.bd[2] & BDL_XAD) << BDL_S_XAD) | d->rx.bd[0];
        if (d->rx.bd[1] == 0) {
            sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: RX buffer descriptor size is zero\n", k, d->line);
            kmc_halt (k, HALT_MTRCV);
            d->rxstate = RXIDLE;
            break;
        }
        d->rx.rcvc = 0;
        d->rxdlen = 0;
        d->rxstate = RXDAT;

    case RXDAT:
    more:
        if (d->rxused < 8) {
            seglen = 6 - d->rxused;
        } else {
            seglen = d->rxmlen - (d->rxused +2);
        }
        if (seglen > d->rx.bd[1]) {
            seglen = d->rx.bd[1];
        }
        assert (seglen > 0);

        xrem = (uint16)Map_WriteB (d->rx.ba, seglen, d->rxmsg + d->rxused);
        if (xrem != 0) {
            uint16 bd[3];
            memcpy (bd, &d->rx.bd, sizeof bd);
            seglen -= xrem;
            d->rx.rcvc += seglen;
            bd[1] = d->rx.rcvc;
            kmc_updateBDCount (d->rx.bda, bd);  /* Unchecked because already reporting NXM */
            kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, d->line, d->rx.bda);
            d->rxstate = RXIDLE;
            break;
        }
        d->rx.ba += seglen;
        d->rx.rcvc += seglen;
        d->rxused += seglen;

        if (d->rxused == 6) {
            if (0 != ddcmp_crc16 (0, d->rxmsg, 8)) {
                sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: HCRC Error for %d byte packet\n",
                          k, d->line, d->rxmlen);
#ifdef DUP_RXRESYNC
                dup_set_RCVEN (d->dupidx, FALSE);
                dup_set_RCVEN (d->dupidx, TRUE);
#endif
                kmc_ctrlOut (k, SEL6_CO_HCRC, SEL2_IOT, d->line, d->rx.bda);
                d->rxstate = RXIDLE;
                break;
            }
            d->rxused += 2;
            d->linkstate &= ~LINK_SEL;
            if (d->rxmsg[2] & 0x80) {
                d->linkstate |= LINK_SEL;
            }
            if (d->ctrlFlags & SEL6_CI_ENASS) { /* Note that spec requires first bd >= 6 if SS match enabled */
                if (!(d->rxmsg[5] == (d->ctrlFlags & SEL6_CI_SADDR))) { /* Also include SELECT? */
                    ASSURE ((bdl = (BDL *)remqueue(d->bdqh.prev, &d->bdavail)) != NULL);
                    assert (bdl->ba == d->rx.bda);
                    ASSURE (insqueue (&bdl->hdr, &d->rxqh, &d->rxavail, MAXQUEUE));
                    d->rxstate = RXIDLE;
                    break;
                }
            }
            d->rxdlen = ((d->rxmsg[2] &~ 0300) << 8) | d->rxmsg[1];
        }
        if (((d->rxused == 8) && (d->rxmsg[0] == DDCMP_ENQ)) ||
            (((d->rxused - 8) == d->rxdlen) && (d->rxmsg[0] != DDCMP_ENQ))) { /* End of message */

            /* Issue completion after the nominal reception delay */

            rxup->wait = XTIME (d->rx.rcvc+2, d->linespeed);
            d->rxstate = RXLAST;
            break;
        }
        if (d->rx.rcvc < d->rx.bd[1]) {
            goto more;
        }

        /* This descriptor is full.  No need to update its bc.
         * Issue the completion after the nominal reception delay.
         */
        d->rxstate = RXFULL;
        rxup->wait = XTIME (d->rx.bd[1], d->linespeed);
        break;

    case RXLAST:
        /* End of message.  Update final BD count, check data CRC & report either
         * BUFFER OUT (with EOM) or error CONTROL OUT (NXM or DCRC).
         */
        d->rx.bd[1] = d->rx.rcvc;
        if (kmc_updateBDCount (d->rx.bda, d->rx.bd)) {
            kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, d->line, d->rx.bda);
        } else {
            if ((d->rxmsg[0] != DDCMP_ENQ) &&
                (0 !=  ddcmp_crc16 (0, d->rxmsg +8, d->rxdlen+2))) {
                sim_debug (DF_PKT, &kmc_dev, "KMC%u line %u: DCRC Error for %d byte packet\n",
                           k, d->line, d->rxmlen);
#ifdef DUP_RXRESYNC
                dup_set_RCVEN (d->dupidx, FALSE);
                dup_set_RCVEN (d->dupidx, TRUE);
#endif
                kmc_ctrlOut (k, SEL6_CO_DCRC, SEL2_IOT, d->line, d->rx.bda);
            } else {
                kmc_bufferAddressOut (k, SEL6_BO_EOM, SEL2_IOT, d->line, d->rx.bda);
#ifdef DUP_RXRESYNC
                if (d->rxmsg[2] & 0x40) {       /* QSYNC? (Next message uses short sync) */
                    dup_set_RCVEN (d->dupidx, FALSE);
                    dup_set_RCVEN (d->dupidx, TRUE);
                }
#endif
            }
        }
        rxup->wait = RXNEWBD_DELAY;
        d->rxstate = RXIDLE;
        break;

    case RXFULL:
        kmc_bufferAddressOut (k, 0, SEL2_IOT, d->line, d->rx.bda);

        /* Advance to next descriptor */

        if (d->rx.bd[2] & BDL_LDS) {
            d->rxstate = RXBDL;
        } else {
            d->rx.bda += 3*2;
            if (Map_ReadW (d->rx.bda, 3*2, d->rx.bd)) {
                kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, d->line, d->rx.bda);
                d->rxstate = RXIDLE;
                break;
            }
            sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: receiving bd=%06o\n",
                       k, rxup->unit_line, d->rx.bda);
            d->rx.rcvc = 0;                     /* Set here in case of kill */
            d->rxstate = RXBUF;
        }
        rxup->wait = RXNEWBD_DELAY;
       break;
    
    case RXNOBUF:
        kmc_ctrlOut (k, SEL6_CO_NOBUF, SEL2_IOT, d->line, 0);
        d->rxstate = RXIDLE;
        break;

    default:
        assert (FALSE);
    }

    if ((d->rxstate != RXIDLE) || d->rxavail) {
        if (rxup->wait == tmxr_poll)
            sim_clock_coschedule(rxup, tmxr_poll);
        else
            sim_activate_after(rxup, rxup->wait);
    }

    return SCPE_OK;
}

/*
 * master clear a KMC
 *
 * Master clear initializes the hardware, but does not clear any RAM.
 * This includes the CSRs, which are a dual-ported RAM structure.
 *
 * There is no guarantee that any data structures are initialized.
 */

static void kmc_masterClear(int32 k) {

    if (sim_deb) {
        DEVICE *dptr = find_dev_from_unit (&tx_units[0][k]);

        sim_debug (DF_INF, dptr, "KMC%d: Master clear\n", k);
    }

    if (sel0 & SEL0_RUN) {
        kmc_halt (k, HALT_MRC);
    }

    /* Clear SEL1 (maint reg) - done by HW reset.
     * Clear IE (HW doesn't, but it simplifies
     * things & every user to date writes zeroes with MRC.
     */
    sel0 &= SEL0_MRC | (0x00FF & ~(SEL0_IEO | SEL0_IEI));
    upc = 0;
    mar = 0;
    mna = 0;
    mni = 0;

    kmc_updints (k);
}

/* Initialize the KMC state that is done by microcode */

static void kmc_startUcode (int32 k) {
    int i;
    const char *uname;

    if ((uname = kmc_verifyUcode (k)) == NULL) {
        sim_debug (DF_INF, &kmc_dev, "KMC%u: microcode not loaded, won't run\n", k);
        kmc_halt (k, HALT_BADUC);
        return;
    }

    sim_debug (DF_INF, &kmc_dev, "KMC%u started %s microcode at uPC %04o\n",
              k, uname, upc);

    if (upc != 0) {                             /* Resume from cleared RUN */
        if (gflags & FLG_UCINI) {
            for (i = 0; i < MAX_ACTIVE; i++) {
                UNIT *up = &tx_units[i][k];

                if (up->unit_htime) {
                    sim_activate (up, up->unit_htime);
                }
                up = &rx_units[i][k];
                if (up->unit_htime) {
                    sim_activate (up, up->unit_htime);
                }
            }
            return;
        }
        kmc_halt (k, HALT_BADRES);
        return;
    }

    /* upc == 0: microcode initialization */

    upc = 1;

    /* CSRs */

    sel0 &= 0xFF00;
    sel2 = 0;
    sel4 = 0;
    sel6 = 0;

    /* Line data */

    /* Initialize OS mapping to least likely device. (To avoid validating everywhere.) */

    for (i = 0; i < MAX_ACTIVE; i++) {
        line2dup[i] = &dupState[DUP_LINES-1];
    }

    /* Initialize all the DUP structures, releasing any assigned to this KMC.
     *
     * Only touch the devices if they have previously been assigned to this KMC.
     */

    for (i = 0; i < DUP_LINES; i++) {
        dupstate *d = dupState + i;

        if ((d->kmc == k) && (d->dupidx != -1)) {
            /* Make sure no callbacks are issued.
             * Hardware initialization is done by BASE IN.
             */
            dup_set_callback_mode (i, NULL, NULL, NULL);
        }
        /* Initialize DUP state if dup is unassigned or previously assigned
         * to this KMC.  This releases the DUP, so restarting ucode will
         * release devices, allowing another KMC to assign them if desired.
         * This is a level of cooperation that the real devices don't have,
         * but it helps catch configuration errors and keeps these data
         * structures consistent.  Don't deassign a device currently owned
         * by another kmc!
         */
        if ((d->kmc == k) || (d->kmc == -1)) {
            d->dupidx = -1;
            d->kmc = -1;
            d->line = UNASSIGNED_LINE;

            initqueue (&d->rxqh, &d->rxavail, INIT_HDR_ONLY);
            initqueue (&d->txqh, &d->txavail, INIT_HDR_ONLY);
            initqueue (&d->bdqh, &d->bdavail, MAX_LIST_SIZE(d->bdq));
            
            d->rxstate = RXIDLE;
            d->txstate = TXIDLE;
        }
    }

    /* Completion queue */

    initqueue(&cqueueHead, &cqueueCount, INIT_HDR_ONLY);
    initqueue(&freecqHead, &freecqCount, MAX_LIST_SIZE(cqueue));

    gflags |= FLG_UCINI;
    kmc_updints (k);
    return;
}
 
/*
 * Perform an input command
 *
 * The host must request ownership of the CSRs by setting RQI.
 * If enabled, it gets an interrupt when RDI sets, allowing it
 * to write the command.  RDI and RDO are mutually exclusive.
 *
 * The microcode sets RDI by writing the entire BSEL2 with just RDI.
 * This works because RDO can not be set at the same time as RDI.
 * 
 * Mark P found that VMS drivers rely on this and BIS the command code.
 * The COM IOP-DUP manual does say that all bits of BSEL2 are
 * cleared on completion of a command.  However, an output command could
 * leave other bits on.
 *
 * Input commands are processed by the KMC when the host
 * clears RDI.  Upon completion of a command, 'all bits of bsel2
 * are cleared by the KMC'. This is not implemented literally, since
 * the processing of a command can result in an immediate completion,
 * setting RDO and the other registers. 
 * Thus, although all bits are cleared before dispatching, RDO
 * and the other other bits of BSEL2 may be set for a output command
 * due to a completion if the host has cleared RQI.
 */

static void kmc_dispatchInputCmd(int32 k) {
    uint8 line;
    int32 ba;
    int16 cmdsel2 = sel2;
    dupstate* d;
    
    line = (cmdsel2 & SEL2_LINE) >> SEL2_V_LINE;

    sel2 &= ~0xFF;                              /* Clear BSEL2. */
    if (sel0 & SEL0_RQI)                        /* If RQI was left on, grant the input request */
        sel2 |= SEL2_RDI;                       /* here so any generated completions will block. */

    if (line > MAX_LINE) {
        sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: Line number is out of range\n", k, line);
        kmc_halt (k, HALT_LINE);
        return;
    }
    d = line2dup[line];
    ba = ((sel6 & SEL6_CO_XAD) << (16-SEL6_V_CO_XAD)) | sel4;
    
    sim_debug (DF_CMD, &kmc_dev, "KMC%u line %u: INPUT COMMAND sel2=%06o sel4=%06o sel6=%06o ba=%06o\n", k, line,
              cmdsel2, sel4, sel6, ba);
    
    switch (cmdsel2 & (SEL2_IOT | SEL2_CMD)) {
    case CMD_BUFFIN:		                    /* TX BUFFER IN */
        kmc_txBufferIn(d, ba, sel6);
        break;
    case CMD_CTRLIN:			                /* CONTROL IN. */
    case SEL2_IOT | CMD_CTRLIN:
        kmc_ctrlIn (k, d, line);
        break;

    case CMD_BASEIN:			                /* BASE IN. */
        kmc_baseIn (k, d, cmdsel2, line);
        break;

    case (SEL2_IOT | CMD_BUFFIN):			    /* Buffer in, receive buffer for us... */
        kmc_rxBufferIn(d, ba ,sel6);
        break;
    default:
        kmc_halt (k, HALT_BADCMD);
        break;
    }

    return;
}
/* Process BASE IN command
 *
 *  BASE IN assigns a line number to a DUP device, and marks it
 *  assigned by a KMC.  The CSR address is expressed as bits <12:3>
 *  only.  <17:13> are all set for IO page addresses and added by ucode.
 *  The DUP has 8 registers, so <2:1> must be zero.  The other bits are
 *  reserved and must be zero.  
 *
 * There is no way  to release a line, short of re-starting the microcode.
 *
 */
static void kmc_baseIn (int32 k, dupstate *d, uint16 cmdsel2, uint8 line) {
    uint32 csraddress;
    int32 dupidx;

    /* Verify DUP is enabled and at specified address */

    /* Ucode clears low three bits of CSR address, ors 1s into the high 3.
     * CSR address here may include bits >17 (e.g. UBA number)
     *
     * The check for reserved bits is not done by the ucode, and can be
     * removed.  It's here for debugging any cases where this code is at fault.
     */

    csraddress = sel6 & SEL6_II_DUPCSR;

    if ((sel4 != 0) || (cmdsel2 & SEL2_II_RESERVED)) {
        sim_debug (DF_ERR, &kmc_dev, "KMC%u: BASE IN reserved bits set\n");
        kmc_halt (k, HALT_BADCSR);
        return;
    }
    csraddress |= IOPAGEBASE;

    dupidx = dup_csr_to_linenum (sel6);
    if ((dupidx < 0) || (((size_t) dupidx) >= DIM(dupState))) { /* Call this a NXM so OS can recover */
        sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: BASE IN %06o 0x%05x is not an enabled DUP\n", 
                   k, line, csraddress, csraddress);
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, line, 0);
        return;
    }
    d = &dupState[dupidx];
    if ((d->kmc != -1) && (d->kmc != k)) {
        sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: BASE IN %06o 0x%05x is already assigned to KMC%u\n",
                   k, line, csraddress, csraddress,
                   d->kmc);
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, line, 0);
        return;
    }

    /* OK to take ownership.
     * Master clear the DUP. NXM will cause a control-out.
     *
     * The microcode takes no special action to clear DTR.
     */

    d->dupcsr = csraddress;
    d->kmc = k;
    line2dup[line] = d;
    d->line = line;

    /* 
     * Jumper W3 Installed causes RTS,DTR, and SecTxD to be cleared on Device Reset or bus init.
     * Jumper W3 is installed in factory DUPs, and in the KS10 config.
     * Make sure the DUP emulation enables this option.
     */
    dup_set_W3_option (dupidx, 1);
    /*
     * Reset the DUP device.  This will clear DTR and RTS.
     */
    if (dup_reset_dup (dupidx) != SCPE_OK) {
        sim_debug (DF_CTO, &kmc_dev, "KMC%u line %u: BASE IN dup %d DUP TXCSR NXM\n",
                   k, line, dupidx);
        d->kmc = -1;
        return;
    }

    /* Hardware requires a 2 usec delay before next access to DUP.
     */

    d->dupidx = dupidx;

    sim_debug (DF_INF, &kmc_dev, "KMC%u line %u: BASE IN DUP%u address=%06o 0x%05x assigned\n", 
                  k, line, d->dupidx,csraddress, csraddress);
    return;
}

/* Process CONTROL IN command
 *
 * CONTROL IN establishes the characteristics of each communication line
 * controlled by the KMC.  At least one CONTROL IN must be issued for each
 * DUP that is to communicate.
 *
 * CONTROL IN writes the DUP CSRs to configure it for the selected mode.
 *
 * Not implemented:
 *  o Polling count (no mapping to emulator) (SEL4_CI_POLL)
 */

static void kmc_ctrlIn (int32 k, dupstate *d, int line) {
    t_stat r;

    if (DEBUG_PRS (&kmc_dev)) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u line %u: CONTROL IN ", k, line);
        if (!(sel6 & SEL6_CI_ENABLE)) {
            sim_debug (DF_CMD, &kmc_dev, "line disabled\n");
        } else {
            sim_debug (DF_CMD, &kmc_dev, "enabled for %s in %s duplex", 
                      (sel6 & SEL6_CI_DDCMP)? "DDCMP":"Bit-stuffing",
                      (sel6 & SEL6_CI_HDX)? "half" : "full");
            if (sel6 & SEL6_CI_ENASS) {
                sim_debug (DF_CMD, &kmc_dev, " SS:%u",
                          (sel6 & SEL6_CI_SADDR), line);
            }
            sim_debug (DF_CMD, &kmc_dev, "\n");
        }
    }

    /* BSEL4 has the polling count, which isn't needed for emulation */

    d->linkstate &= ~(LINK_DSR|LINK_SEL);/* Initialize modem state reporting. */      

    d->ctrlFlags = sel6;

    /* Initialize DUP interface in the appropriate mode
     * DTR will be turned on.
     */
    r = dup_setup_dup (d->dupidx, (sel6 & SEL6_CI_ENABLE), 
                                  (sel6 & SEL6_CI_DDCMP), 
                                  (sel6 & SEL6_CI_NOCRC), 
                                  (sel6 & SEL6_CI_HDX), 
                                  (sel6 & SEL6_CI_ENASS) ? (sel6 & SEL6_CI_SADDR) : 0);

    /* If setup succeeded, enable packet callbacks.
     *
     * If setup failed, generate a CONTROL OUT.
     */
    if (r == SCPE_OK) {
        dup_set_callback_mode (d->dupidx, kdp_receive, kmc_txComplete, kmc_modemChange);
    } else {
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, 0);
        sim_debug (DF_CTO, &kmc_dev, "KMC%u line %u: CONTROL IN dup %d DUP CSR NXM\n",
                   k, line, d->dupidx);
    }

    return;
}

/* Process RX BUFFER IN command
 *
 * RX BUFFER IN delivers a buffer descriptor list from the host to
 * the KMC.  It may also deassign a buffer descriptor list.
 *
 * A buffer descriptor list is a sequential list of three word blocks in 
 * host memory space.  Each 3-word block points to and describes the
 * boundaries of a single buffer, which is also in host CPU memory.  
 *
 * A buffer descriptor list is defined by its starting address.  The
 * end of a buffer descriptor list is marked in the list.  A maximum of
 * MAXQUEUE transmit and MAXQUEUE receive lists can be assigned to each
 * COMM IOP-DUP communications line. 
 *
 * The starting address of a buffer descriptor list must be word aligned.
 *
 * The buffers in this list will be filled with data received from the
 * line and returned to the host by RX BUFFER OUT completions.
 *
 * Buffer descriptor lists are deassigned through use of the Kill bit
 * and also reassigned when this bit is used in conjuntion with the
 * Buffer Enable bit.  When Kill is set, all buffer descriptor lists for
 * the specified direction (RX or TX) are released.  If the enable bit is
 * also set, a new buffer descriptor list is assigned after the old lists
 * are deassigned.  In any case, RX kill places the associated DUP in sync
 * search mode.  TX kill brings the line to a mark hold state.
 *
 * Note that Buffer Enable is ignored unless Kill is also set.
 *
 */

void kmc_rxBufferIn(dupstate *d, int32 ba, uint16 sel6v) {
    int32 k = d->kmc;
    BDL *qe;
    uint32 bda = 0;
    UNIT *rxup;

    if (d->line == UNASSIGNED_LINE)
        return;

    assert ((k >= 0) && (((unsigned int)k) < kmc_dev.numunits) && (d->dupidx != -1));

    rxup = &rx_units[d->line][k];

    if (!kmc_printBufferIn (k, &kmc_dev, d->line, TRUE, d->rxavail, ba, sel6v))
        return;
    
    if (sel6v & SEL6_BI_KILL) {
        /* Kill all current RX buffers.
         * Resync the DUP receiver.
         */
#ifdef DUP_RXRESYNC
        dup_set_RCVEN (d->dupidx, FALSE);
        dup_set_RCVEN (d->dupidx, TRUE);
#endif
        if ((d->rxstate >= RXBUF) && (d->rxstate < RXFULL)) {
            /* A bd is open (active).
             * Updating bytes received should be done before the kill.  TOPS-10 clears the UBA map
             * before requesting it.  But it doesn't look at bd.  So don't report NXM.
             * In these states, the bd is always cached.
             */
            d->rx.bd[1] = d->rx.rcvc;
            kmc_updateBDCount (d->rx.bda, d->rx.bd);
            bda = d->rx.bda;
        } else {
            bda = 0;
        }
        d->rxstate = RXIDLE;
        sim_cancel (rxup);
        while ((qe = (BDL *)remqueue (d->rxqh.next, &d->rxavail)) != NULL) {
            ASSURE (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
        }
        if (!(sel6v & SEL6_BI_ENABLE)) {
            kmc_ctrlOut (k, SEL6_CO_KDONE, SEL2_IOT, d->line, bda);
            return;
        }
    }

    /* Add new buffer to available for RX queue */

    if ((qe = (BDL *)remqueue (d->bdqh.next, &d->bdavail)) == NULL) {
        sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: Too many receive buffers from  hostd\n", k, d->line);
        kmc_halt (k, HALT_RCVOVF);
        return;
    }
    qe->ba = ba;
    ASSURE (insqueue (&qe->hdr, d->rxqh.prev, &d->rxavail, MAXQUEUE));
    
    if (sel6v & SEL6_BI_KILL) {                 /* KILL & Replace - ENABLE is set too */
        kmc_ctrlOut (k, SEL6_CO_KDONE, SEL2_IOT, d->line, bda);
    }

    /* Start receiver if necessary */

    if ((d->rxstate == RXIDLE) && !sim_is_active (rxup)) {
        sim_activate_after (rxup, RXSTART_DELAY);
    }
    return;
}

/* Message available callback
 *
 * The DUP calls this routine when a new message is available 
 * to be read.
 *
 * If the line's receive thread is idle, it is called to start the
 * receive process.  If the thread is busy, the message will be
 * retrieved when the current receive process completes.
 *
 * This notification avoids the need for the receive thread to
 * periodically poll for an incoming message when idle.
 *
 * The data and count arguments are unused - the function signature
 * requires them for other modes.
 */

static void kdp_receive(int32 dupidx, int count) {
    int32 k;
    dupstate* d;
    UNIT *rxup;
    UNUSED_ARG (count);

    assert ((dupidx >= 0) && (dupidx < DIM(dupState)));
    d = &dupState[dupidx];
    assert (dupidx == d->dupidx);
    k = d->kmc;
    rxup = &rx_units[d->line][k];

    if (d->rxstate == RXIDLE){
        sim_cancel (rxup);
        sim_activate_after (rxup, RXNEWBD_DELAY);
    }
    return;
}

/* Process TX BUFFER IN command
 *
 * TX BUFFER IN delivers a buffer descriptor list from the host to
 * the KMC.  It may also deassign a buffer descriptor list.
 *
 * For a complete description of buffer descriptor list, see
 * RX BUFFER IN.
 *
 * The buffers in this list contain data to be transmitted to the
 * line, and are returned to the host by TX BUFFER OUT completions.
 *
 * TX buffer descriptors include control flags that indicate start
 * and end of message.  These determine when the DUP is told start/
 * stop accumulating data for and to insert CRCs.  A resync flag
 * indicates when sync characters must be inserted (after half-duplex
 * line turn-around or CRC errors.)
 *
 */

void kmc_txBufferIn(dupstate *d, int32 ba, uint16 sel6v) {
    int32 k = d->kmc;
    BDL *qe;

    if (d->line == UNASSIGNED_LINE)
        return;

    assert ((k >= 0) && (((unsigned int)k) < kmc_dev.numunits) && (d->dupidx != -1));

    if (!kmc_printBufferIn (k, &kmc_dev, d->line, FALSE, d->txavail, ba, sel6v))
        return;
    
    if (sel6v & SEL6_BI_KILL) {
      /* Kill all current TX buffers.  The DUP can't abort transmission in simulation, so
       * anything pending will stop when complete.  The queue is reset here because
       * the kill & replace option has to be able to enqueue the replacement BDL.
       * If a tx is active, the DUP will issue a completion, which will report
       * completion of the kill.  A partial message that has been buffered, but
       * not handed to the DUP can be stopped here.
       */
        while ((qe = (BDL *)remqueue (d->txqh.next, &d->txavail)) != NULL) {
            ASSURE (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
        }
        if (d->txstate < TXACT) {               /* DUP is idle */
            sim_cancel (&tx_units[d->line][k]); /* Stop tx bdl walker */
            d->txstate = TXIDLE;

            if (!(sel6v & SEL6_BI_ENABLE)) {
                kmc_ctrlOut (k, SEL6_CO_KDONE, 0, d->line, 0);
                return;
            }
            /* Continue to replace buffer */
        } else {                                /* DUP is transmitting, defer */
            if (sel6v & SEL6_BI_ENABLE)         /* Replace now, kill done later */
                d->txstate = TXKILR;
            else {
                d->txstate = TXKILL;
                return;
            }
        }
    }
    
    if (!(qe = (BDL *)remqueue (d->bdqh.next, &d->bdavail))) {
        sim_debug (DF_ERR, &kmc_dev, "KMC%u line %u: Too many transmit buffers from host\n", k, d->line);
        kmc_halt (k, HALT_XMTOVF);
        return;
    }
    qe->ba = ba;
    ASSURE (insqueue (&qe->hdr, d->txqh.prev, &d->txavail, MAXQUEUE));
    if (d->txstate == TXIDLE) {
        UNIT *txup = &tx_units[d->line][k];   
        if (!sim_is_active (txup)) {
            txup->wait = TXSTART_DELAY; 
            sim_activate_after (txup, txup->wait);
        }
    }
   
    return;
}

/* Transmit complete callback from the DUP
 *
 * Called when the last byte of a packet has been transmitted.
 *
 * Handle any deferred kill and schedule the next message.
 *
 * Note that this may be called from within the txService when the
 * DUP is not speed limited.  The unconditional activation ensures
 * that txService will not be called recursively.
 */

static void kmc_txComplete (int32 dupidx, int status) {
    dupstate *d;
    UNIT *txup;
    int32 k;

    assert ((dupidx >= 0) && (((size_t)dupidx) < DIM(dupState)));

    d = &dupState[dupidx];
    k = d->kmc;
    txup = &tx_units[d->line][k];

    if (status) {                               /* Failure is probably due to modem state change */
        kmc_updateDSR(d);                       /* Change does not stop transmission or release buffer */
    }

    if (d->txstate < TXACT) {                   /* DUP shouldn't call when KMC is not sending */
        sim_debug (DF_BUF, &kmc_dev, "KMC%u line %u: tx completion while inactive\n",
                   k, d->line);
        return;
    }

    d->txmlen =
        d->txslen = 0;
    if ((d->txstate == TXKILL) || (d->txstate == TXKILR)) {
        /* If DUP could kill a partial transmission, would update bd here */
        d->txstate = TXDONE;
        kmc_ctrlOut (k, SEL6_CO_KDONE, 0, d->line, d->tx.bda);
    } else {
        if (d->tx.bd[2] & BDL_LDS)
            d->txstate = TXDONE;
        else 
            d->txstate = TXSOM;
    }
    sim_cancel (txup);
    sim_activate_after (txup, TXDONE_DELAY);

    return;
}

/* Obtain a new buffer descriptor list from those queued by the host */

static t_bool kmc_txNewBdl(dupstate *d) {
    BDL *qe;
    
    if (!(qe = (BDL *)remqueue (d->txqh.next, &d->txavail))) {
        return FALSE;
    }
    d->tx.bda = qe->ba;
    ASSURE (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
    
    d->tx.first = TRUE;
    d->tx.bd[1] = 0;
    return kmc_txNewBd(d);
}

/* Obtain a new TX buffer descriptor.
 *
 * If the current descriptor is last, obtain a new list.
 * (The new list case will recurse.)
 *
 */

static t_bool kmc_txNewBd(dupstate *d) {
    int32 k = d->kmc;

    if (d->tx.first)
        d->tx.first = FALSE;
    else {
        if (d->tx.bd[2] & BDL_LDS) {
            if (!kmc_txNewBdl(d)) {
                kmc_ctrlOut (k, SEL6_CO_TXU, 0, d->line, d->tx.bda);
                return FALSE;
            }
            return TRUE;
        }
        d->tx.bda += 6;
    }
    if (Map_ReadW (d->tx.bda, 2*3, d->tx.bd)) {
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, d->tx.bda);
        return FALSE;
    }
    
    d->tx.ba = ((d->tx.bd[2] & BDL_XAD) << BDL_S_XAD) | d->tx.bd[0];
    return TRUE;
}

/* Append data from a host buffer to the current message, as
 * the DUP prefers to get the entire message in one swell foop.
 */

static t_bool kmc_txAppendBuffer(dupstate *d) {
    int32 k = d->kmc;
    uint16 rem;

    if (!d->txmsg || (d->txmsize < d->txmlen+d->tx.bd[1])) {
        d->txmsize = d->txmlen+d->tx.bd[1];
        d->txmsg = (uint8 *)realloc(d->txmsg, d->txmsize);
        assert (d->txmsg);
    }
    rem = (uint16)Map_ReadB (d->tx.ba, d->tx.bd[1], d->txmsg+d->txmlen);
    d->tx.bd[1] -= rem;
    rem += (uint16)kmc_updateBDCount (d->tx.bda, d->tx.bd);
    if (rem) {
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, d->tx.bda);
        return FALSE;
    }
    d->txmlen += d->tx.bd[1];
    return TRUE;
}

/* Try to deliver a completion (OUTPUT command)
 *
 * Because the same CSRs are used for delivering commands to the KMC,
 * the RDO and RDI bits, along with RQI arbitrate access to the CSRs.
 *
 * The KMC prioritizes completions over taking new commands.
 *
 * Thus, if RDO is set, the host has not taken the previous completion
 * data from the CSRs.  Output is not possible until the host clears RDO.
 *
 * If RDO is clear, RDI indicates that the host owns the CSRs, and
 * should be writing a command to them.  Output is not possible.
 *
 * If neither is set, the KMC takes ownership of the CSRs and updates
 * them from the queue before setting RDO.  
 *
 * There is aditional prioitization of RDI/RDO in the logic that detects
 * RDO clearing.  If RQI has been set by the host before clearing RDO,
 * the KMC guarantees that RDI will set even if more completions are
 * pending.
 */

static void kmc_processCompletions (int32 k) {
    CQ *qe;

    if (sel2 & (SEL2_RDO | SEL2_RDI))           /* CSRs available? */
        return;

    if (!(qe = (CQ *)remqueue (cqueueHead.next, &cqueueCount))) {
        return;
    }

    ASSURE (insqueue (&qe->hdr, freecqHead.prev, &freecqCount, CQUEUE_MAX));
    sel2 = qe->bsel2;
    sel4 = qe->bsel4;
    sel6 = qe->bsel6;

    sim_debug (DF_QUE, &kmc_dev, "KMC%u line %u: %s %s delivered: sel2=%06o sel4=%06o sel6=%06o\n",
                k, ((sel2 & SEL2_LINE)>>SEL2_V_LINE), 
                (sel2 & SEL2_IOT)? "RX":"TX",
                ((sel2 & SEL2_CMD) == CMD_BUFFOUT)? "BUFFER OUT":"CONTROL OUT",
                sel2, sel4, sel6);

    sel2 |= SEL2_RDO;

    kmc_updints (k);

    return;
}

/* Queue a CONTROL OUT command to the host.
 *
 * All but one of these release one or more buffers to the host.
 *
 * code is the event (usually error)
 * rx is TRUE for a receive buffer, false for transmit.
 * line is the line number assigned by BASE IN
 * bda is the address of the buffer descriptor that has been processed.
 *
 * Returns FALSE if the completion queue is full (a fatal error)
 */

static void kmc_ctrlOut (int32 k, uint8 code, uint16 rx, uint8 line, uint32 bda)
{
  CQ *qe;

  if (DEBUG_PRS (kmc_dev)) {
      static const char *const codenames[] = {
          "Undef", "Abort", "HCRC", "DCRC", "NoBfr", "DSR", "NXM", "TXU", "RXO", "KillDun" };
      unsigned int idx = code;
      idx = ((code < 06) || (code > 026))? 0: ((code/2)-2);

      sim_debug (DF_CTO, &kmc_dev, "KMC%u line %u: %s CONTROL OUT Code=%02o (%s) Address=%06o\n",
                 k, line, rx? "RX":"TX", code, codenames[idx], bda);
  }

  if (!(qe = (CQ *)remqueue (freecqHead.next, &freecqCount))) {
      sim_debug (DF_QUE, &kmc_dev, "KMC%u line %u: Completion queue overflow\n", k, line);
      /* Set overflow status in last entry of queue */
      qe = (CQ *)cqueueHead.prev;
      qe->bsel2 |= SEL2_OVR;
    return;
  }
  qe->bsel2 = ((line << SEL2_V_LINE) & SEL2_LINE) | rx | CMD_CTRLOUT;
  qe->bsel4 = bda & 0177777;
  qe->bsel6 = ((bda >> (16-SEL6_V_CO_XAD)) & SEL6_CO_XAD) | code;
  ASSURE (insqueue (&qe->hdr, cqueueHead.prev, &cqueueCount, CQUEUE_MAX));
  kmc_processCompletions(k);
  return;
}

/* DUP device callback for modem state change.
 * The DUP device provides this callback whenever
 * any modem control signal changes state.
 *
 * The timing is not exact with respect to the data
 * stream.

 * This can be used for HDX as well as DSR CHANGE>
 *
 */
static void kmc_modemChange (int32 dupidx) {
  dupstate *d;

  assert ((dupidx >= 0) && (((size_t)dupidx) < DIM(dupState)));
  d = &dupState[dupidx];

  if (d->dupidx != -1) {
    kmc_updateDSR (d);
  }
  return;
}

/* Check for and report DSR changes to the host.
 * DSR is assumed false initially by the host.
 * DSR Change Control-Out reports each change.
 * No value is provided; the report simply toggles
 * the host's view of the state.
 *
 * This is the ONLY CONTROL OUT that does not release
 * a buffer.
 *
 * Returns TRUE if a change occurred.
 */
static t_bool kmc_updateDSR (dupstate *d) {
    int32 k = d->kmc;
    int32 status;

    status = dup_get_DSR(d->dupidx);
    status = status? LINK_DSR : 0;
    if (status ^ (d->linkstate & LINK_DSR)) {
        d->linkstate = (d->linkstate &~LINK_DSR) | status;
        kmc_ctrlOut (k, SEL6_CO_DSRCHG, 0, d->line, 0);
        return TRUE;
    }
    return FALSE;
}

/* Queue a BUFFER ADDRESS OUT command to the host.
 *
 * flags are applied to BSEL6 (e.g. receive EOM).
 * rx is TRUE for a receive buffer, false for transmit.
 * line is the line number assigned by BASE IN
 * bda is the address of the buffer descriptor that has been processed.
 *
 * Returns FALSE if the completion queue is full (a fatal error)
 */

static t_bool kmc_bufferAddressOut (int32 k, uint16 flags, uint16 rx, uint8 line, uint32 bda) {
    CQ *qe;

    sim_debug (DF_BFO, &kmc_dev, "KMC%u line %u: %s BUFFER OUT Flags=%06o Address=%06o\n",
               k, line, rx? "RX": "TX", flags, bda);
    
    if (!kmc_printBDL(k, DF_BFO, &kmc_dev, line, bda, rx? 6: 2))
        return FALSE;
    if (!(qe = (CQ *)remqueue (freecqHead.next, &freecqCount))) {
        sim_debug (DF_QUE, &kmc_dev, "KMC%u line %u: Completion queue overflow\n", k, line);
        /* Set overflow status in last entry of queue */
        qe = (CQ *)cqueueHead.prev;
        qe->bsel2 |= SEL2_OVR;
        return FALSE;
    }
    qe->bsel2 = ((line << SEL2_V_LINE) & SEL2_LINE) | rx | CMD_BUFFOUT;
    qe->bsel4 = bda & 0177777;
    qe->bsel6 = ((bda >> (16-SEL6_V_CO_XAD)) & SEL6_CO_XAD) | flags;
    ASSURE (insqueue (&qe->hdr, cqueueHead.prev, &cqueueCount, CQUEUE_MAX));

    kmc_processCompletions(k);
    return TRUE;
}

/* The UBA does not do a RPW cycle when byte 0 (of 4)
 * on a -10 is written.  (It can, but the OS doesn't program it that
 * way.  Thus, if the count word is in the left half-word, updating
 * it will trash the 3rd word of that buffer descriptor.  The hardware
 * works.  The KMC microcode does a word NPR write.   At this writing
 * how the hardware works is a mystery.  The UBA documentation is quite
 * clear that a write is done; the prints show zeros muxed into the bits.
 * The OS is NOT setting 'read reverse'.  The KMC ucode is not doing
 * any magic.  And the OS will kill the KMC if the 3rd word of the BD
 * is trashed.  Unless there is an undiscovered ECO to the KS10, there
 * is no explanation for the fact that the KDP does work on that machine.
 * Certainly, if the UBA always did RPW cycles, this would work, and
 * probably no one would notice the performance loss.
 *
 * For now, this code writes the count, and also the 3rd word if the
 * count is in byte 0.  It's not the right solution, but it works for now.
 * This does an extra write if the UBA trashed the following word, and not
 * otherwise.  Note that any BDL with two buffers is guaranteed to
 * run into this issue.  If the first BD is in byte 0, its count is OK, but
 * the following BD will start in byte 2, putting its count in byte 0 of
 * the following word, causing the write to step on that bd's flags.
 */

static int32 kmc_updateBDCount(uint32 bda, uint16 *bd) {
  
  return Map_WriteW (bda+2, (((bda+2) & 2)? 2 : 4), &bd[1]);
}

/* Halt a KMC.  This happens for some errors that the real KMC
 * may not detect, as well as when RUN is cleared.
 * The kmc is halted & interrupts are disabled.
 */

static void kmc_halt (int32 k, int error) {
    int line;

    if (error){
        sel0 &= ~(SEL0_IEO|SEL0_IEI);
    }
    sel0 &= ~SEL0_RUN;

    kmc_updints (k);
    for (line = 0; line < MAX_ACTIVE; line++) {
        UNIT *up = &tx_units[line][k];

        if (sim_is_active (up)) {
            up->unit_htime = sim_activate_time (up);
            sim_cancel (up);
        } else {
            up->unit_htime = 0;
        }
        up = &rx_units[line][k];
        if (sim_is_active (up)) {
            up->unit_htime = sim_activate_time (up);
            sim_cancel (up);
        } else {
            up->unit_htime = 0;
        }
    }
    sim_debug (DF_INF, &kmc_dev, "KMC%u: Halted at uPC %04o reason=%d\n", k, upc, error);
    return;
}
/*
 * Update interrupts pending.
 *
 * Since the interrupt request is shared across all KMCs
 * (a simplification), pending flags are kept per KMC,
 * and a global request count across KMCs for the UBA.
 * The UBA will clear the global request flag when it grants
 * an interrupt; thus for set the global flag is always set
 * if this KMC has a request.  This doesn't quite match "with IEI
 * set, only one interrupt is generated for each setting of RDI."
 * An extra interrupt, however, should be harmless.
 *
 * Since interrupts are generated by microcode, do not touch the interrupt
 * system unless microcode initialization has run.
 */

static void kmc_updints(int32 k) {
    if (!(gflags & FLG_UCINI)) {
        return;
    }

    if ((sel0 & SEL0_IEI) && (sel2 & SEL2_RDI)) {
        if (!(gflags & FLG_AINT)) {
            sim_debug (DF_INT, &kmc_dev, "KMC%u: set input interrupt pending\n", k);
            gflags |= FLG_AINT;
            AintPending++;
        }
        SET_INT(KMCA);
    } else {
        if (gflags & FLG_AINT) {
            sim_debug (DF_INT, &kmc_dev, "KMC%u: cleared pending input interrupt\n", k);
            gflags &= ~FLG_AINT;
            if (--AintPending == 0) {
                CLR_INT(KMCA);
            }
        }
    }

    if ((sel0 & SEL0_IEO) && (sel2 & SEL2_RDO)) {
        if (!(gflags & FLG_BINT)) {
            sim_debug (DF_INT, &kmc_dev, "KMC%u: set output interrupt\n", k);
            gflags |= FLG_BINT;
            BintPending++;
        }
        SET_INT(KMCB);
    } else {
        if (gflags & FLG_BINT) {
            sim_debug (DF_INT, &kmc_dev, "KKMC%u: clear output interrupt\n", k);
            gflags &= ~FLG_BINT;
            if (--BintPending == 0) {
                CLR_INT(KMCB);
            }
        }
    }
    return;
}

/* Interrupt acknowledge service.
 * When the UBA grants an interrupt request, it
 * requests the vector number from the device.
 *
 * These routines return the vector number from the
 * interrupting KMC.  Lower numbered KMCs have
 * priority over higher numbered KMCs.
 *
 * Any one  KMC should never have both input and output
 * pending at the same time.
 */

static int32 kmc_AintAck (void) {
    int32 vec = 0; /* no interrupt request active */
    int32 k;

    for (k = 0; ((size_t)k) < DIM (kmc_gflags); k++) {
        if (gflags & FLG_AINT) {
            vec = kmc_dib.vec + (k*010);
            gflags &= ~FLG_AINT;
            if (--AintPending == 0) {
                CLR_INT(KMCA);
            }
            break;
        }
    }

    if (vec)
        sim_debug (DF_INT, &kmc_dev, "KMC%u input (A) interrupt ack vector %03o\n", k, vec);
    else
        sim_debug (DF_INT, &kmc_dev, "KMC  input (A) passive release\n");

    return vec;
}

static int32 kmc_BintAck (void) {
    int32 vec = 0;                              /* no interrupt request active */
    int32 k;

    for (k = 0; ((size_t)k) < DIM (kmc_gflags); k++) {
        if (gflags & FLG_BINT) {
            vec = kmc_dib.vec + 4 + (k*010);
            gflags &= ~FLG_BINT;
            if (--BintPending == 0) {
                CLR_INT(KMCB);
            }
            break;
        }
    }

    if (vec)
        sim_debug (DF_INT, &kmc_dev, "KMC%u output (B) interrupt ack vector %03o\n", k, vec);
    else
        sim_debug (DF_INT, &kmc_dev, "KMC  output (B) passive release\n");

    return vec;
}

/* Debug: Log a BUFFER IN or BUFFER OUT command.
 * returns FALSE if print encounters a NXM as (a) it's fatal and 
 * (b) only one completion per bdl.
 */

static t_bool kmc_printBufferIn (int32 k, DEVICE *dev, uint8 line, t_bool rx, 
                                 int32 count, int32 ba, uint16 sel6v) {
    t_bool kill = ((sel6v & (SEL6_BI_KILL|SEL6_BI_ENABLE)) == SEL6_BI_KILL);
    const char *dir = rx? "RX": "TX";

    sim_debug (DF_CMD, &kmc_dev, "KMC%u line %u: %s BUFFER IN%s %d, bdl=%06o 0x%04x\n", k, line, dir,
            (kill? "(Buffer kill)": ((sel6v & SEL6_BI_KILL)? "(Kill & replace)":  "")),
            count, ba, ba);

    if (kill) /* Just kill doesn't use BDL, may NXM if attempt to dump */
        return TRUE;

    /* Kill and replace supplies new BDL */

    if (!kmc_printBDL(k, DF_CMD, dev, line, ba, rx? 5: 1))
        return FALSE;

    sim_debug (DF_QUE, &kmc_dev, "KMC%u line %u: %s BUFFER IN %d, bdl=%06o 0x%04X\n", k, line, dir, count, ba, ba);

    return TRUE;
}
/*
 * Debug: Dump a BDL and a sample of its buffer.
 *
 * prbuf: non-zero to access buffer
 *        Bit 1 set to print just one descriptor (BFO), clear to do entire list (BFI)
 *        Bit 2 set if rx (rx bfi doesn't print data)
 */

static t_bool kmc_printBDL(int32 k, uint32 dbits, DEVICE *dev, uint8 line, int32 ba, int prbuf) {
    uint16 bd[3];
    int32 dp;

    if (!DEBUG_PRJ(dev,dbits))
        return TRUE;

    for (;;) {
        if (Map_ReadW (ba, 3*2, bd) != 0) {
            kmc_ctrlOut (k, SEL6_CO_NXM, 0, line, ba);
            sim_debug (dbits,dev, "KMC%u line %u: NXM reading descriptor addr=%06o\n", k, line, ba);
            return FALSE;
        }
        
        dp = bd[0] | ((bd[2] & BDL_XAD) << BDL_S_XAD);
        sim_debug (dbits, dev, "  bd[0] = %06o 0x%04X Adr=%06o\n", bd[0], bd[0], dp);
        sim_debug (dbits, dev, "  bd[1] = %06o 0x%04X Len=%u.\n", bd[1], bd[1], bd[1]);
        sim_debug (dbits, dev, "  bd[2] = %06o 0x%04X", bd[2], bd[2]);
        if (bd[2] & BDL_LDS) {
            sim_debug (dbits, dev, " Last");
        }
        if (bd[2] & BDL_RSY) {
            sim_debug (dbits, dev, " Rsync");
        }
        if (bd[2] & BDL_EOM) {
            sim_debug (dbits, dev, " XEOM");
        }
        if (bd[2] & BDL_SOM) {
            sim_debug (dbits, dev, " XSOM");
        }
        sim_debug (dbits, dev, "\n");

        if (prbuf) {
            uint8 buf[20];
            if (bd[1] > sizeof buf)
                bd[1] = sizeof buf;

            if (Map_ReadB (dp, bd[1], buf) != 0) {
                kmc_ctrlOut (k, SEL6_CO_NXM, 0, line, dp);
                sim_debug (dbits, dev, "KMC%u line %u: NXM reading buffer %06o\n", k, line, dp);
                return FALSE;
            }
            
            if (prbuf != 5) {                   /* Don't print RX buffer in */                
                for (dp = 0; dp < bd[1] ; dp++) {
                    sim_debug (dbits, dev, " %02x", buf[dp]);
                }
                sim_debug (dbits, dev, "\r\n");
            }
        }
        if ((bd[2] & BDL_LDS) || (prbuf & 2))   /* Last, or 1 only */
            break;
        ba += 6;
    }
    return TRUE;
}

/* Verify that the microcode image is one that this code knows how to emulate.
 * As far as I know, there was COMM IOP-DUP V1.0 and one patch, V1.0A.
 * This is the patched version, which I have verified by rebuilding the
 * V1.0A microcode from sources and computing its CRC from the binary.
 *
 * RSX DECnet has a different binary; the version is unknown.
 *
 * The reason for this check is that there ARE other KMC microcodes.
 * If some software thinks it's loading one of those, the results
 * could be catastrophic - and hard to diagnose.  (COMM IOP-DZ has
 * a similar function; but there are other, stranger microcodes.
 */

static const char *kmc_verifyUcode (int32 k) {
    size_t i, n;
    uint16 crc = 'T' << 8 | 'L';
    uint8 w[2];
    static const struct {
        uint16 crc;
        const char *name;
    } known[] = {
        { 0xc3cd, "COMM IOP-DUP V1.0A" },
        { 0x1a38, "COMM IOP-DUP RSX" },
    };

    for (i = 0, n = 0; i < DIM (ucode); i++) {
        if (ucode[i] != 0)
            n++;
        w[0] = ucode[i] >> 8;
        w[1] = ucode[i] & 0xFF;
        crc = ddcmp_crc16 (crc, &w, sizeof w);
    }
    if (n < ((3 * DIM (ucode))/4)) {
        sim_debug (DF_INF, &kmc_dev, "KMC%u: Microcode not loaded\n", k);
        return NULL;
    }
    for (i = 0; i < DIM (known); i++) {
        if (crc == known[i].crc) {
            sim_debug (DF_INF, &kmc_dev, "KMC%u: %s microcode loaded\n", k, known[i].name);
            return known[i].name;
        }
    }
    sim_debug (DF_INF, &kmc_dev, "KMC%u: Unknown microcode loaded\n", k);
    return NULL;
}

/* Initialize a queue to empty.
 *
 * Optionally, adds a list of elements to the queue.
 * max, list and size are only used if list is non-NULL.
 *
 * Convenience macros:
 *   MAX_LIST_SIZE(q) specifies max, list and size for an array of elements q
 *   INIT_HDR_ONLY provides placeholders for these arguments when only the
 * header and count are to be initialized.
 */

static void initqueue (QH *head, int32 *count, int32 max, void *list, size_t size) {
    head->next = head->prev = head;
    *count = 0;
    if (list == NULL)
        return;
    while (insqueue ((QH *)list, head->prev, count, max))
        list = (QH *)(((char *)list)+size);
    return;
}

/* Insert entry on queue after pred, if count < max.
 * Increment count.
 *   To insert at head of queue, specify &head for predecessor.
 *   To insert at tail, specify head.pred
 *
 * returns FALSE if queue is full.
 */

static t_bool insqueue (QH *entry, QH *pred, int32 *count, int32 max) {
    if (*count >= max)
        return FALSE;
    entry-> next = pred->next;
    entry->prev = pred;
    pred->next->prev = entry;
    pred->next = entry;
    ++*count;
    return TRUE;
}

/* Remove entry from queue.
 * Decrement count.
 *  To remove from head of queue, specify head.next.
 *  To remove form tail of queue, specify head.pred.
 *
 * returns FALSE if queue is empty.
 */

static void *remqueue (QH *entry, int32 *count) {
    if (*count <= 0)
        return NULL;
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    --*count;
    return (void *)entry;
}

/* Simulator UI functions */

/* SET KMC DEVICES processor
 *
 * Adjusts the size of I/O space and number of vectors to match the specified
 * number of KMCs.
 * The txup is that of unit zero.
 */

#if KMC_UNITS > 1
static t_stat kmc_setDeviceCount (UNIT *txup, int32 val, char *cptr, void *desc) {
    int32 newln;
    uint32 dupidx;
    t_stat r;
    DEVICE *dptr = find_dev_from_unit(txup);
    
    if (cptr == NULL)
        return SCPE_ARG;

    for (dupidx = 0; dupidx < DIM (dupState); dupidx++) {
        dupstate *d = &dupState[dupidx];
        if ((d->kmc != -1) ||  (d->dupidx != -1)) {
            return SCPE_ALATT;
        }
    }
    newln = (int32) get_uint (cptr, 10, KMC_UNITS, &r);
    if ((r != SCPE_OK) || (newln == (int32)dptr->numunits))
        return r;
    if (newln == 0)
        return SCPE_ARG;
    kmc_dib.lnt = newln * IOLN_KMC;             /* set length */
    kmc_dib.vnum = newln * 2;                   /* set vectors */
    dptr->numunits = newln;
    return kmc_reset (dptr);                    /* setup devices and auto config */
}
#endif

/* Report number of configured KMCs */

#if KMC_UNITS > 1
static t_stat kmc_showDeviceCount (FILE *st, UNIT *txup, int32 val, void *desc) {
    DEVICE *dev = find_dev_from_unit(txup);

    if (dev->flags & DEV_DIS) {
            fprintf (st, "Disabled");
            return SCPE_OK;
    }

    fprintf (st, "devices=%d", dev->numunits);
    return SCPE_OK;
}
#endif

/* Set line speed
 *
 * This is the speed at which the KMC processed data for/from a DUP.
 * This limits the rate at which buffer descriptors are processed, even
 * if the actual DUP speed is faster.  If the DUP speed is slower, of
 * course the DUP wins.  This limit ensures that the infinite speed DUPs
 * of simulation won't overrun the host's ability to process buffers.
 *
 * The speed range is expressed as line bits/sec for user convenience.
 * The limits are 300 bps (a sync line won't realistically run slower than
 * that), and 1,000,000 bps - the maximum speed of a DMR11.  The KDP's
 * practical limit was about 19,200 BPS.  The higher limit is a rate that
 * the typicaly host software could handle, even if the line couldn't.
 *
 * Note that the DUP line speed can also be set.
 *
 * To allow setting the speed before a DUP has been assigned to a KMC by
 * the OS, the set (and show) commands reference the DUP and apply to any
 * potential use of that DUP by a KMC.
 */

static t_stat kmc_setLineSpeed (UNIT *txup, int32 val, char *cptr, void *desc) {
    dupstate *d;
    int32 dupidx, newspeed;
    char gbuf[CBUFSIZE];
    t_stat r;

    if (!cptr || !*cptr)
        return SCPE_ARG;

    cptr = get_glyph (cptr, gbuf, '=');         /* get next glyph */
    if (*cptr == 0)                             /* should be speed */
        return SCPE_2FARG;
    dupidx = (int32) get_uint (gbuf, 10, DUP_LINES, &r); /* Parse dup # */
    if ((r != SCPE_OK) || (dupidx < 0))         /* error? */
        return SCPE_ARG;

    cptr = get_glyph (cptr, gbuf, 0);           /* get next glyph */
    if (*cptr != 0)                             /* should be end */
        return SCPE_2MARG;
    cptr = gbuf;
    if (!strcmp (cptr, "DUP"))
        cptr += 3;
    newspeed = (int32) get_uint (cptr, 10, MAX_SPEED, &r);
    if ((r != SCPE_OK) || (newspeed < 300))     /* error? */
        return SCPE_ARG;

    d = &dupState[dupidx];
    d->linespeed = newspeed;
    return SCPE_OK;
}

static t_stat kmc_showLineSpeed (FILE *st, UNIT *txup, int32 val, void *desc) {
    int dupidx;

    fprintf (st, "DUP KMC Line   Speed\n"
                 "--- --- ---- --------\n");

    for (dupidx =0; dupidx < DUP_LINES; dupidx++) {
        dupstate *d = &dupState[dupidx];

        fprintf (st, "%3u ", dupidx);

        if (d->kmc == -1) {
            fprintf (st, " -   - ");
        } else {
            fprintf (st, "%3u %3u", d->kmc, d->line);
        }
        fprintf (st, " %8u\n", d->linespeed);
    }

    return SCPE_OK;
}

/* Show KMC status */

t_stat kmc_showStatus (FILE *st, UNIT *up, int32 v,  void *dp) {
    int32 k = up->unit_kmc;
    int32 line;
    t_bool first = TRUE;
    DEVICE *dev = find_dev_from_unit(up);
    const char *ucname;

    if ((dev->flags & DEV_DIS) || (((uint32)k) >= dev->numunits)) {
        fprintf (st, "KMC%u  Disabled\n", k);
        return SCPE_OK;
    }

    ucname = kmc_verifyUcode (k);

    fprintf (st, "KMC%u  ", k);

    if (!(sel0 & SEL0_RUN)) {
        fprintf (st, "%s halted at uPC %04o\n", 
                 (ucname?ucname: "(No or unknown microcode)"), upc);
        return SCPE_OK;
    }

    fprintf (st, "%s is running at uPC %04o\n",
             (ucname?ucname: "(No or unknown microcode)"), upc);

    if (!(gflags & FLG_UCINI)) {
        return SCPE_OK;
    }

    for (line = 0; line <= MAX_LINE; line++) {
        dupstate *d = line2dup[line];
        if (d->kmc == k) {
            if (first) {
                fprintf (st, "     Line DUP   CSR   State\n");
                first = FALSE;
            }
            fprintf (st, "     %3u %3u %06o %-8s %3s %s %s %s",
                     line, d->dupidx, d->dupcsr,
                     (d->ctrlFlags & SEL6_CI_ENABLE)? "enabled": "disabled",
                     (d->linkstate & LINK_DSR)? "DSR" : "OFF",
                     (d->ctrlFlags & SEL6_CI_DDCMP)? "DDCMP" : "Bit-Stuff",
                     (d->ctrlFlags & SEL6_CI_HDX)? "HDX " : "FDX",
                     (d->ctrlFlags & SEL6_CI_NOCRC)? "NOCRC": "");
            if (d->ctrlFlags & SEL6_CI_ENASS)
                fprintf (st, " SS (%u) ", d->ctrlFlags & SEL6_CI_SADDR);
            fprintf (st, "\n");
        }
    }
    if (first)
        fprintf (st, "     No DUPs assigned\n");

    return SCPE_OK;
}

/* Help for this device.
 *
 */

static t_stat kmc_help (FILE *st, struct sim_device *dptr,
                         struct sim_unit *uptr, int32 flag, char *cptr) {
    const char *const text =
" The KMC11-A is a general purpose microprocessor that is used in\n"
" several DEC products.  The KDP is an emulation of one of those\n"
" products: COMM IOP-DUP.\n"
"\n"
" The COMM IOP-DUP microcode controls and supervises 1 - 16 DUP-11\n"
" synchronous communications line interfaces, providing scatter/gather\n"
" DMA, message framing, modem control, CRC validation, receiver\n"
" resynchronization, and address recognition.\n"
"\n"
" The DUP-11 lines are assigned to the KMC11 by the (emulated) operating\n"
" system, but SimH must be told how to connect them.  See the DUP HELP\n"
" for details.\n"
"1 Hardware Description\n"
" The KMC11-A microprocessor is a 16-bit Harvard architecture machine\n"
" optimized for data movement, character processing, address arithmetic\n"
" and other functions necessary for controlling I/O devices.  It resides\n"
" on the UNIBUS and operates in parallel with the host CPU with a cycle\n"
" time of 300 nsec.  It contains a 1024 word writable control store that\n"
" is loaded by the host, 1024 words of data memory, 16 8-bit scratchpad\n"
" registers, and 8 bytes of RAM that are dual-ported between the KMC11\n"
" and UNIBUS I/O space.  It also has a timer and various internal busses\n"
" and registers.\n"
"\n"
" Seven of the eight bytes of dual-ported RAM have no fixed function;\n"
" they are defined by the microcode.  The eighth register allows the\n"
" host to control the KMC11: the host can start, stop, examine state and\n"
" load microcode using this register.\n"
"\n"
" The microprocessor is capable of initiating DMA (NPR) UNIBUS cycles to\n"
" any UNIBUS address (memory and I/O space).  It can interrupt the host\n"
" via one of two interrupt vectors.\n"
"\n"
" The microcodes operate other UNIBUS devices by reading and writing\n"
" their CSRs with UNIBUS DMA transactions, typically on a\n"
" character-by-character basis.  There is no direct connection between\n"
" the KMC11 and the peripherals that it controls.  The controlled\n"
" devices do not generate interrupts; all interrupts are generated by\n"
" the KMC11, which monitors the devices by polling their CSRs.\n"
"\n"
" By presenting the character-oriented peripherals to the host as\n"
" message-oriented devices, the KMC11 reduces the host's overhead in\n"
" operating the peripherals, relaxes the required interrupt response\n"
" times and increases the potential I/O throughput of a system.\n"
"\n"
" The hardware also has a private bus that can be used to control\n"
" dedicated peripherals (such as a DMC11 synchronous line unit) without\n"
" UNIBUS transactions.  This feature is not emulated.\n"
"\n"
" This emulation does not execute the KMC microcode, but rather provides\n"
" a functional emulation.\n"
"\n"
" However, some of the microcode operators are emulated because system\n"
" loaders and OS diagnostics execute single instructions to initialize\n"
" or diagnose the device.\n"
"2 $Registers\n"
"2 Related devices\n"
" Other versions of the KMC11 have ROM microcode, which are used in such\n"
" devices as the DMC11 and DMR11 communications devices.  This emulation\n"
" does not support those versions.\n"
"\n"
" Microcodes, not supported by this emulation, exist which control other\n"
" UNIBUS peripherals in a similar manner.  These include:\n"
"\n"
"+DMA for DZ11 asynchronous lines (COMM IOP-DZ)\n"
"+DMA for line printers\n"
"+Arpanet IMP interface (AN22 on the KS10/TOPS-20)\n"
"\n"
" The KMC11 was also embedded in other products, such as the DX20 Massbus\n"
" to IBM channel adapter.\n"
"\n"
" The KMC11-B is an enhanced version of the KMC11-A.  Note that microcode\n"
" loading is handled differently in that version, which is NOT emulated.\n"
"1 Configuration\n"
" Most configuration of KDP lines is done by the host OS and by SimH\n"
" configuration of the DUP11 lines.\n"
"\n"
#if KMC_TROLL
" The KDP has two configurable parameters.\n"
#else
" The KDP has one configurable parameter.\n"
#endif
" Line speed - this is the speed at which each communication line\n"
" operates.  The DUP11's line speed should be set to 'unlimited' to\n"
" avoid unpredictable interactions.\n"
#if KMC_TROLL
" Troll - the KDP emulation includes a process that will intentionally\n"
" drop or corrupt some messages.  This emulates the less-than-perfect\n"
" communications lines encountered in the real world, and enables\n"
" network monitoring software to see non-zero error counters.\n"
"\n"
" The troll selects messages with a probablility selected by the SET\n"
" TROLL command.  The units are 0.1%%; that is, a value of 1 means that\n"
" every message has a 1/1000 chance of being selected.\n"
#endif
"2 $Set commands\n"
#if KMC_UNITS > 1
" SET KDP DEVICES=n enables emulation of up to %1s KMC11s.\n"
#endif
"2 $Show commands\n"
"1 Operation\n"
" A KDP device consists of one or more DUP11s controlled by a KMC11.\n"
" The association of DUP11s to KMC11s is determined by the host OS.\n"
"\n"
" For RSX DECnet, use NCP:\n"
" +SET LINE KDP-kdp-line CSR address\n"
" +SET LINE KDP-kdp-line UNIT CSR address\n"
" where 'kdp' is the KDP number and 'line' is the line number on\n"
" that kdp.  'address' is the I/O page offset of the CSR; e.g.\n"
" 760050 is entered as 160050.\n"
"\n"
" For TOPS-10/20, the addresses are fixed.\n"
"\n"
" For VMS...\n"
"\n"
" Although the microcode is not directly executed by the emulated KMC11,\n"
" the correct microcode must be loaded by the host operating system.\n"
;
    char kmc_units[10];

    sprintf (kmc_units, "%u", KMC_UNITS);

    return scp_help (st, dptr, uptr, flag, text, cptr, kmc_units);
}

/* Description of this device.
 * Conventionally last function in the file.
 */
static char *kmc_description (DEVICE *dptr) {
    return "KMC11-A Synchronous line controller supporting only COMM IOP/DUP microcode";
}
