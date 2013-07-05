/* pdp11_kmc.c: KMC11-A with COMM IOP-DUP microcode Emulation
  ------------------------------------------------------------------------------


   Written 2002 by Johnny Eriksson <bygg@stacken.kth.se>

   Adapted to SIMH 3.? by Robert M. A. Jarratt in 2013

   Enhanced, and largely rewritten by Timothe Litt <litt@acm.org> in 2013,
   building on previous work.

   This code is Copyright 2002, 2013 by the authors,all rights reserved.
   It is licensed under the standard SimH license.
  ------------------------------------------------------------------------------

  Modification history:

  05-Jun-13  TL   Massive rewrite to split KMC/DUP, add missing functions, and
                  restructure so TOPS-10/20 are happy.
  14-Apr-13  RJ   Took original sources into latest source code.
  15-Feb-02  JE   Massive changes/cleanups.
  23-Jan-02  JE   Modify for version 2.9.
  17-Jan-02  JE   First attempt.
------------------------------------------------------------------------------*/
/*
 * Loose ends, known problems etc:
 *
 *   We don't do anything but full-duplex DDCMP.
 *   TODO marks implementation issues.
 *
 *   The DUP packet API complicates things, making completions and buffer flush
 *   occurr at inopportune times.
 * Move descriptor walks to service routine.
 *
*/

/* The KMC11-A is a general purpose microprocessor that is used in several
 * DEC products.  It came in versions that had ROM microcode, which were
 * used in such devices as the DMC1 and DMR11 communications devices.
 *
 * In each case, it is a Unibus master and operates under OS control as
 * an asynchronous IO processor.  In some versions, it uses a private
 * communications bus to an associated unit, typically a line card.  In
 * others, it controls some other device over the Unibus, accessing the
 * slave devices CSRs as though it were a CPU doing polled IO.
 *
 * It also was produced with RAM microcode, and served as a general-purpose
 * DMA engine.  Microcodes exist for this version to allow it to:
 *  * Provide DMA for DZ11 asynchronous lines (COMM IOP-DZ)
 *  * Provide DMA for line printers
 *  * Provide DMA and message framing for DUP-11 sync lines (COMM IOP-DUP)
 *
 * The device was also embedded in other products, such as the DX20 Massbus
 * to IBM channel adapter.
 *
 * This is an emulation of one of those products: COMM IOP-DUP.  It does
 * not execute the KMC microcode, but rather provides a functional emulation.
 *
 * Some of the microcode operators are emulated because system loaders and
 * OS diagnostics would execute single instructions to initialize or dump
 * the device.
 *
 * The KMC11-B is an enhanced version of the KMC11-A.  Note that microcode
 * loading is handled differently in that version.
 */

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#define KMC_RDX     8

#include <assert.h>
#include "pdp11_dup.h"
#include "pdp11_ddcmp.h"

#define DIM(x) (sizeof(x)/sizeof((x)[0]))

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

static char *kmc_description (DEVICE *dptr);

extern int32 IREQ (HLVL);

/* bits, SEL0 */

#define SEL0_RUN    0100000  /* Run bit. */
#define SEL0_MRC    0040000  /* Master clear. */
#define SEL0_CWR    0020000  /* CRAM write. */
#define SEL0_SLU    0010000  /* Step Line Unit. */
#define SEL0_LUL    0004000  /* Line Unit Loop. */
#define SEL0_RMO    0002000  /* ROM output. */
#define SEL0_RMI    0001000  /* ROM input. */
#define SEL0_SUP    0000400  /* Step microprocessor. */
#define SEL0_RQI    0000200  /* Request input. */
#define SEL0_IEO    0000020  /* Interrupt enable output. */
#define SEL0_IEI    0000001  /* Interrupt enable input. */

/* bits, SEL2 */

#define SEL2_OVR    0100000  /* Completion queue overrun. */
#define SEL2_V_LINE 8        /* Line number assigned by host */
#define SEL2_LINE   (0177 << SEL2_V_LINE)
#define   MAX_LINE   017     /* Maximum line number allowed in BASE_IN */
#define SEL2_RDO    0000200  /* Ready for output transaction. */
#define SEL2_RDI    0000020  /* Ready for input transaction. */
#define SEL2_IOT    0000004  /* I/O type, 1 = rx, 0 = tx. */
#define SEL2_V_CMD        0  /* Command code */
#define SEL2_CMD    0000003  /* Command code
                              * IN are commands TO the KMC
                              * OUT are command completions FROM the KMC.
                              */
#  define CMD_BUFFIN      0  /*   BUFFER IN */
#  define CMD_CTRLIN      1  /*   CONTROL IN */
#  define CMD_BASEIN      3  /*   BASE IN */
#  define CMD_BUFFOUT     0  /*   BUFFER OUT */
#  define CMD_CTRLOUT     1  /*   CONTROL OUT */

#define SEL2_II_RESERVED  (SEL2_OVR | 0354) /* Reserved: 15, 7:5, 3:2 */
/* bits, SEL4 */

#define SEL4_CI_POLL    0377  /* DUP polling interval, 50 usec units */

#define SEL4_ADDR    0177777  /* Generic: Unibus address <15:0>

/* bits, SEL6 */

#define SEL6_V_CO_XAD    14      /* Unibus extended address bits */
#define SEL6_CO_XAD    (3u << SEL6_V_CO_XAD)

/* BASE IN */
#define SEL6_II_DUPCSR 0017770   /* BASE IN: DUP CSR <12:3> */

/* BUFFER IN */
#define SEL6_BI_ENABLE 0020000   /* BUFFER IN: Assign after KILL */
#define SEL6_BI_KILL   0010000   /* BUFFER IN: Return all buffers */

/* BUFFER OUT */
#define SEL6_BO_EOM    0010000   /* BUFFER OUT: End of message */

/* CONTROL OUT event codes */
#define SEL6_CO_ABORT  006   /* Bit stuffing rx abort */
#define SEL6_CO_HCRC   010   /* DDCMP Header CRC error */
#define SEL6_CO_DCRC   012   /* DDCMP Data CRC/ BS frame CRC */
#define SEL6_CO_NOBUF  014   /* No RX buffer available */
#define SEL6_CO_DSRCHG 016   /* DSR changed (Initially OFF) */
#define SEL6_CO_NXM    020   /* NXM */
#define SEL6_CO_TXU    022   /* Transmitter underrun */
#define SEL6_CO_RXO    024   /* Receiver overrun */
#define SEL6_CO_KDONE  026   /* Kill complete */

/* CONTROL IN modifiers */
#define SEL6_CI_V_DDCMP 15   /* Run DDCMP vs. bit-stuffing */
#define SEL6_CI_DDCMP   (1u << SEL6_CI_V_DDCMP)
#define SEL6_CI_V_HDX   13   /* Half-duplex */
#define SEL6_CI_HDX     (1u << SEL6_CI_V_HDX)
#define SEL6_CI_V_ENASS 12   /* Enable secondary station address filter */
#define SEL6_CI_ENASS   (1u << SEL6_CI_V_ENASS)
#define SEL6_CI_V_NOCRC  9
#define SEL6_CI_NOCRC   (1u << SEL6_CI_V_NOCRC)
#define SEL6_CI_V_ENABLE 8
#define SEL6_CI_ENABLE  (1u << SEL6_CI_V_ENABLE)
#define SEL6_CI_SADDR    0377

/* Buffer descriptor list bits */

#define BDL_LDS    0100000  /* Last descriptor in list. */
#define BDL_RSY    0010000  /* Resync transmitter. */
#define BDL_XAD    0006000  /* Buffer address bits 17 & 16. */
#define BDL_EOM    0001000  /* End of message. */
#define BDL_SOM    0000400  /* Start of message. */

#define KMC_CRAMSIZE 1024   /* Size of CRAM (microcode control RAM). */
#define KMC_DRAMSIZE  1024
#define KMC_CYCLETIME 300   /* Microinstruction cycle time, nsec */

#define MAXQUEUE 16     /* Number of rx bdl's we can handle. */

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

/* TODO: Remove 4 debug lines */
#undef KMC_UNITS
#define KMC_UNITS 2
#undef INITIAL_KMCS
#define INITIAL_KMCS 2
/* TODO: adjust this */

/* Interval at which to run service task for each DUP
 * 417 uSec is roughly the byte rate at 19,200 b/sec
 */
#ifndef KMC_POLLTIME
#define KMC_POLLTIME 417
#endif

struct buffer_list {    /* BDL queue elements  */
      QH     hdr;
      uint32 ba;
};
typedef struct buffer_list BDL;

 struct workblock {
   struct dupstate *dup;
   t_bool    first;
   uint32    bda;
   uint16    bd[3];
   uint16    rcvc;
#define      xmtc rcvc
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
    int32  kmc;              /* Controlling KMC */
    int32  line;             /* OS-assigned line number */
    int32  dupidx;           /* DUP API Number amongst all DUP11's on Unibus (-1 == unassigned) */
    int32  modemstate;       /* Line Link Status (i.e. 1 when DCD/DSR is on, 0 otherwise */
 #define MDM_DSR    1
    uint16 ctrlFlags;
    uint32 dupcsr;

    BDL    bdq[MAXQUEUE*2];   /* Queued TX and RX buffer lists */
    QH     bdqh;              /* Free queue */
    int32  bdavail;

    QH     rxqh;              /* Receive queue from host */
    int32  rxavail;
    QH     txqh;              /* Transmit queue form host */
    int32  txavail;
    WB     tx;
    uint32 txstate;
#define TXIDLE 0
#define TXSOM  1
#define TXHDR  2
#define TXDATA 4
#define TXACT  5
#define TXKILL 6
#define TXKILR 7

    uint8  *txmsg;
    size_t  txmsize, txmlen;
    };

typedef struct dupstate dupstate;

/* State for every DUP that MIGHT be controlled.
 * A DUP can be controlled by at most one KMC.
 */
dupstate dupState[DUP_LINES] = { 0 };

/* Flags defining sim_debug conditions. */

#define DF_CMD    0001  /* Print commands. */
#define DF_TX     0002  /* Print tx done. */
#define DF_RX     0004  /* Print rx done. */
#define DF_DATA   0010  /* Print data. */
#define DF_QUEUE  0020  /* Print rx/tx queue changes. */
#define DF_TRC    0040  /* Detailed trace. */
#define DF_INF    0100  /* Info */

DEBTAB kmc_debug[] = {
    {"CMD",   DF_CMD},
    {"TX",    DF_TX},
    {"RX",    DF_RX},
    {"DATA",  DF_DATA},
    {"QUEUE", DF_QUEUE},
    {"TRC",   DF_TRC},
    {"INF",   DF_INF},
    {0}
};

/* These count the total pending interrupts for each vector
 * across all KMCs.
 */

int32 AintPending = 0;
int32 BintPending = 0;

/* Per-KMC state */

/* To help make the code more readable, by convention the symbol 'k'
 * is the number of the KMC that is the target of the current operation.
 * The global state variables below have a #define of the short form
 * of each name.  Thus, instead of kmc_upc[kmcnum][j], write upc[j].
 * For this to work, k, a uint32 must be in scope and valid.
 *
 * k can be found in several ways:
 *  k is the offset into any of the tables.  E.g. given a UNIT pointer,
 *  k = uptr - kc_units.
 * The KMC assigned to control a DUP is stored it its dupstate.
 *  k = dupState[ndupno]->kmc; (-1 if not controlled by any KMC)
 * The DUP associated with a line is stored in line2dup.
 *  k = line2dup[line]->kmc
 * From the DEVICE pointer, dptr->units lists all the UNITs, and the
 * number of units is dptr->numunits.
 * From a CSR address:
 *  k = (PA - dib.ba) / IOLN_KMC
 *
 */

/* Emulator error halt codes
 * These are mostl error conditions that produce undefined
 * results in the hardware.  To help with debugging, unique
 * codes are provided here.
 */

#define HALT_STOP      0       /* Run bit cleared */
#define HALT_MRC       1       /* Master clear */
#define HALT_BADRES    2       /* Resume without initialization */
#define HALT_LINE      3       /* Line number out of range */
#define HALT_BADCMD    4       /* Undefined command received */
#define HALT_BADCSR    5       /* BASE IN had non-zero MBZ */
#define HALT_BADDUP    6       /* DUP not configured and enabled */
#define HALT_DUPALC    7       /* DUP assigned to another KMC */
#define HALT_RCVOVF    8       /* Too many receive buffers assigned */
#define HALT_MTRCV     9       /* Receive buffer descriptor has zero size */
#define HALT_XMTOVF   10       /* Too many transmit buffers assigned */
#define HALT_XSOM     11       /* Transmission didn't start with SOM */
#define HALT_XSOM2    12       /* Data buffer didn't start with SOM */
#define HALT_BADUC    13       /* No or unrecognized microcode loaded */

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
uint16    kmc_sel0[KMC_UNITS];  /* CSR0 - BSEL 1,0 */
#define       sel0 kmc_sel0[k]
uint16    kmc_sel2[KMC_UNITS];  /* CSR2 - BSEL 3,2 */
#define       sel2 kmc_sel2[k]
uint16    kmc_sel4[KMC_UNITS];  /* CSR4 - BSEL 5,4 */
#define    sel4 kmc_sel4[k]
uint16    kmc_sel6[KMC_UNITS];  /* CSR6 - BSEL 7,6 */
#define    sel6 kmc_sel6[k]

/* Microprocessor state - subset  exposed to the host */
uint16    kmc_upc[KMC_UNITS];   /* Micro PC */
#define       upc kmc_upc[k]
uint16    kmc_mar[KMC_UNITS];   /* Micro Memory Address Register */
#define       mar kmc_mar[k]
uint16    kmc_mna[KMC_UNITS];   /* Maintenance Address Register */
#define       mna kmc_mna[k]
uint16    kmc_mni[KMC_UNITS];   /* Maintenance Instruction Register */
#define       mni kmc_mni[k]
uint16    kmc_ucode[KMC_UNITS][KMC_CRAMSIZE];
#define       ucode kmc_ucode[k]
uint16    kmc_dram[KMC_UNITS][KMC_DRAMSIZE];
#define       dram kmc_dram[k]

dupstate *kmc_line2dup[KMC_UNITS][MAX_LINE+1];
#define       line2dup kmc_line2dup[k]

/* General state booleans */
int       kmc_gflags[KMC_UNITS]; /* Miscellaneous gflags */
#define       gflags kmc_gflags[k]
#   define    FLG_INIT  000001  /* Master clear has been done once.
                                 * Data structures trustworthy.
                                 */
#   define    FLG_AINT  000002  /* Pending KMC "A" (INPUT) interrupt */
#   define    FLG_BINT  000004  /* Pending KMC "B" (OUTPUT) interrupt */
#   define    FLG_UCINI 000010  /* Ucode initialized */

/* Completion queue elements, header  and freelist */
CQ        kmc_cqueue[KMC_UNITS][CQUEUE_MAX];
#define       cqueue kmc_cqueue[k]

QH        kmc_cqueueHead[KMC_UNITS];
#define       cqueueHead kmc_cqueueHead[k]
int32     kmc_cqueueCount[KMC_UNITS];
#define       cqueueCount kmc_cqueueCount[k]

QH        kmc_freecqHead[KMC_UNITS];
#define       freecqHead kmc_freecqHead[k]
int32     kmc_freecqCount[KMC_UNITS];
#define       freecqCount kmc_freecqCount[k]

/* *** End of per-KMC state *** */

/* Forward declarations: simulator interface */


static t_stat kmc_reset(DEVICE * dptr);
static t_stat kmc_readCsr(int32* data, int32 PA, int32 access);
static t_stat kmc_writeCsr(int32 data, int32 PA, int32 access);
static void kmc_doMicroinstruction( int32 k, uint16 instr );
static t_stat kmc_eventService(UNIT * uptr);

static t_stat kmc_setDeviceCount (UNIT *uptr, int32 val, char *cptr, void *desc);
static t_stat kmc_showDeviceCount (FILE *st, UNIT *uptr, int32 val, void *desc);
static t_stat kmc_showStatus ( FILE *st, UNIT *up, int32 v, void *dp);

/* Global data */

static int32 kmc_AintAck (void);
static int32 kmc_BintAck (void);

#define IOLN_KMC        010

DIB kmc_dib = { IOBA_AUTO,                  /* ba - Base address */
                IOLN_KMC * INITIAL_KMCS,    /* lnt - Length */
                &kmc_readCsr,               /* rd - read IO */
                &kmc_writeCsr,              /* wr - write IO */
                2 * INITIAL_KMCS,           /* vnum - number of Interrupt vectors */
                IVCL (KMCA),                /* vloc - vector locator */
                VEC_AUTO,                   /* vec - auto */
                {&kmc_AintAck,              /* ack - iack routines */
                 &kmc_BintAck} };

UNIT kmc_units[KMC_UNITS];

BITFIELD kmc_sel0_decoder[] = {
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
BITFIELD kmc_sel2_decoder[] = {
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
REG kmc_reg[] = {
    { BRDATADF ( SEL0, kmc_sel0, KMC_RDX, 16, KMC_UNITS, "Initialization/control", kmc_sel0_decoder) },
    { BRDATADF ( SEL2, kmc_sel2, KMC_RDX, 16, KMC_UNITS, "Command/line", kmc_sel2_decoder) },
    { ORDATA ( SEL4, kmc_sel4, 16) },
    { ORDATA ( SEL6, kmc_sel6, 16) },
    { NULL },
    };

MTAB kmc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 010, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "ADDRESS",
        &set_vec, &show_vec, NULL, "Interrupt vector" },
    { MTAB_XTD|MTAB_VUN, 1, "STATUS", NULL, NULL, &kmc_showStatus, NULL, "Display KMC status" },
#if KMC_UNITS > 1
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "DEVICES", "DEVICES=n",
        &kmc_setDeviceCount, &kmc_showDeviceCount, NULL, "Display number of KMC devices enabled" },
#endif
    { 0 },
    };

DEVICE kmc_dev = {
    "KDP", 
    kmc_units, 
    kmc_reg,                        /* Register decode tables */
    kmc_mod,                        /* Modifier table */
    KMC_UNITS,                      /* Number of units */
    KMC_RDX,                        /* Address radix */
    13,                             /* Address width: 18 - <17:13> are 1s, omits UBA */
    1,                              /* Address increment */
    KMC_RDX,                        /* Data radix */
    8,                              /* Data width */
    NULL,                           /* examine routine */
    NULL,                           /* Deposit routine */
    &kmc_reset,                     /* reset routine */
    NULL,                           /* boot routine */
    NULL,                           /* attach routine */
    NULL,                           /* detach routine */
    &kmc_dib,                       /* context */
    DEV_UBUS | DEV_DIS              /* Flags */
             | DEV_DISABLE
             | DEV_DEBUG,
    0,                              /* debug control */
    kmc_debug,                      /* debug flag table */
    NULL,                           /* memory size routine */
    NULL,                           /* logical name */
    NULL,                           /* help routine */
    NULL,                           /* attach help routine */
    NULL,                           /* help context */
    &kmc_description                /* Device description routine */
};

/* Forward declarations: not referenced in simulator data */

static void kmc_masterClear(UNIT *uptr);
static void kmc_startUcode (uint32 k);
static void kmc_dispatchInputCmd(int32 k);

/* Control functions */
static void kmc_baseIn (int32 k, dupstate *d, uint16 cmdsel2, int line, uint32 ba );
static void kmc_ctrlIn (int32 k, dupstate *d, int line, uint32 ba );

/* Receive functions */
void kmc_rxBufferIn(dupstate *d, int32 ba, uint32 sel6v);
static void kdp_receive(int dupidx, uint8* data, int count);
static t_bool kmc_rxWriteViaBDL( WB *wb, uint8 *data, int count );

/* Transmit functions */
static void kmc_txBufferIn(dupstate *d, int32 ba, uint32 sel6v);
static void kmc_txStartDup(dupstate *d);
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

/* Debug support */
static t_bool kmc_printBufferIn (int32 k, DEVICE *dev, int32 line, char *dir,
                                 int32 count, int32 ba, uint16 sel6v);
static t_bool kmc_printBDL(int32 k, uint32 dbits, DEVICE *dev, uint8 line, int32 ba, int prbuf);

/* Environment */
static const char *kmc_verifyUcode ( int32 k );

/* Queue management */
static void initqueue (QH *head, int32 *count, int32 max, void *list, size_t size );
/* Convenience for initqueue() calls */
#    define MAX_LIST_SIZE(q)                    DIM(q),    (q),       sizeof(q[0])
#    define INIT_HDR_ONLY                       0,         NULL,      0

static t_bool insqueue (QH *entry, QH *pred, int32 *count, int32 max);
static void *remqueue (QH *entry, int32 *count);


/*
 * Reset KMC device.  This resets ALL the KMCs:
 */

static t_stat kmc_reset(DEVICE* dptr) {
    UNIT *uptr = dptr->units;
    uint32 k, dupidx;

    if (sim_switches & SWMASK ('P')) {
        for (dupidx = 0; dupidx < DIM (dupState); dupidx++) {
            dupstate *d = &dupState[dupidx];
            d->kmc = -1;
            d->dupidx = -1;
        }
    }

    for (k = 0; k < KMC_UNITS; k++, uptr++) {
        sim_debug(DF_INF, dptr, "KMC%d: Reset\n", k);

        /* One-time initialization  of UNIT */
        if( !uptr->action ) {
            memset (uptr, 0, sizeof *uptr);
            uptr->action = &kmc_eventService;
            uptr->flags = 0;
            uptr->capac = 0;
        }

        kmc_masterClear (uptr); /* If previously running, halt */

        if (sim_switches & SWMASK ('P'))
            gflags &= ~FLG_INIT;

        if (!(gflags & FLG_INIT) ) { /* Power-up reset */
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

    return auto_config (dptr->name, ((dptr->flags & DEV_DIS)? 0: dptr->numunits ));  /* auto config */
}

   
/*
 * KMC11, read registers:
 */

t_stat kmc_readCsr(int32* data, int32 PA, int32 access) {
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
        if ( (sel0 & SEL0_RMO) && (sel0 & SEL0_RMI)) {
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
    
    sim_debug(DF_TRC, &kmc_dev, "KMC%u CSR rd: addr=0%06o  SEL%d, data=%06o 0x%04x access=%d\n",
              k, PA, PA & 07, *data, *data, access);
    return SCPE_OK;
}

/*
 * KMC11, write registers:
 */

static t_stat kmc_writeCsr(int32 data, int32 PA, int32 access) {
    uint32 changed;
    int reg = PA & 07;
    int sel = (PA >> 1) & 03;
    int32 k;

    k = ((PA-((DIB *)kmc_dev.ctxt)->ba) / IOLN_KMC);

    if (access == WRITE) {
        sim_debug(DF_TRC, &kmc_dev, "KMC%u CSR wr: addr=0%06o  SEL%d, data=%06o 0x%04x\n",
                  k, PA, reg, data, data);
    } else {
        sim_debug(DF_TRC, &kmc_dev, "KMC%u CSR wr: addr=0%06o BSEL%d, data=%06o 0x%04x\n",
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
        sel0 = data;
        if (sel0 & SEL0_MRC) {
            if (((sel0 & SEL0_RUN) == 0) && (changed & SEL0_RUN)) {
                kmc_halt (k, HALT_MRC);
            }
            kmc_masterClear(&kmc_units[k]);
            break;
        }
        if( !(data & SEL0_RUN) ) {
            if (data & SEL0_RMO) {
                if ((changed & SEL0_CWR) && (data & SEL0_CWR)) { /* CWR rising */
                    ucode[mna] = sel6;
                    sel4 = ucode[mna]; /* Copy contents to sel 4 */
                }
            } else {
                if (changed & SEL0_RMO) { /* RMO falling */
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
        if (changed & SEL0_RUN) {	/* Changing the run bit? */
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
                sel2 |= SEL2_RDI;
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
             * output, we must service an input request even
             * if we have another output command ready.
             */
            if ((sel2 & SEL2_RDO) && (!(data & SEL2_RDO))) {
                sel2 = data;                  /* RDO clearing, RDI can't be set */
                if (sel0 & SEL0_RQI) {
                    sel2 |= SEL2_RDI;
                    kmc_updints(k);
                } else
                    kmc_processCompletions(k);
            } else {
                if ((sel2 & SEL2_RDI) && (!(data & SEL2_RDI))) {
                    sel2 = data;              /* RDI clearing,  RDO can't be set */
                    kmc_dispatchInputCmd(k);          /* Can set RDO */
                    if ((sel0 & SEL0_RQI) && !(sel2 & SEL2_RDO))
                        sel2 |= SEL2_RDI;
                    kmc_updints(k);
                } else {
                    sel2 = data;
                }
            }
        } else {
            sel2 = data;
        }
        break;
    case 02: /* SEL4 */
        mna = data & (KMC_CRAMSIZE -1);
        sel4 = data;
        break;
    case 03: /* SEL6 */
        if ( sel0 & SEL0_RMI) {
            mni = data;
        }
        sel6 = data;
        break;
    }

    return SCPE_OK;
}

static void kmc_doMicroinstruction( int32 k, uint16 instr ) {
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
     sim_debug (DF_CMD, &kmc_dev, "KMC%u microcode start uPC %04o\n", k, upc);
     break;
   }
 }
 return;
}

/*
 * KMC11 service routine:
 */

static t_stat kmc_eventService (UNIT* uptr) {
    int dupidx;
    int32 k = uptr - kmc_units;

    assert ( (k >= 0) && (k < KMC_UNITS) );

    /* Service all the DUPs assigned to this KMC */

    for (dupidx = 0; dupidx < DUP_LINES; dupidx++) {
        dupstate *d = &dupState[dupidx];

        /* Only process enabled lines */

        if ((d->kmc != k) || !(d->ctrlFlags & SEL6_CI_ENABLE)) {
            continue;
        }

        kmc_updateDSR (d);

        /* Feed transmitter */
        
        /* Poll receiver */
    }

    /* Provide the illusion of progress. */
    upc = 1 + ((upc + ((KMC_POLLTIME*1000)/KMC_CYCLETIME)) % (KMC_CRAMSIZE -1));

    sim_activate_after(uptr, KMC_POLLTIME);
    return SCPE_OK;
}

/*
 * master clear a KMC
 * Master clear initializes the logic, but does not clear any RAM.
 * This includes the CSRs, which are a dual-ported RAM structure.
 *
 * There is no guarantee that any data structures are initialized.
 */

static void kmc_masterClear(UNIT *uptr) {
    uint32 k;

    k = uptr - kmc_units;

    if (sim_deb) {
        DEVICE *dptr = find_dev_from_unit (uptr);
        sim_debug(DF_INF, dptr, "KMC%d: Master clear\n", k);
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

static void kmc_startUcode (uint32 k) {
    int i;
    const char *uname;

    if ((uname = kmc_verifyUcode (k)) == NULL) {
        sim_debug (DF_INF, &kmc_dev, "KMC%u: microcode not loaded, won't run\n", k);
        kmc_halt (k, HALT_BADUC);
        return;
    }

    sim_debug(DF_INF, &kmc_dev, "KMC%u started %s microcode at uPC %04o\n",
              k, uname, upc);

    if (upc != 0) { /* Resume from cleared RUN */
        if (gflags & FLG_UCINI) {
            sim_activate_after (&kmc_units[k], KMC_POLLTIME);
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

    for (i = 0; i <= MAX_LINE; i++ ) {
        line2dup[i] = &dupState[DUP_LINES-1];
    }

    /* Initialize all the DUP structures, releasing any assigned to this KMC.
     *
     * Only touch the devices if they have previously been assigned to this KMC.
     */

    for (i = 0; i < DUP_LINES; i++) {
        dupstate *d = dupState + i;

        d->line = MAX_LINE;
        if ((d->kmc == k) && (d->dupidx != -1)) {
            dup_set_DTR (i, FALSE);
            dup_set_callback_mode (i, NULL, NULL, NULL);
        }
        d->dupidx = -1;
        d->kmc = -1;
        initqueue( &d->rxqh, &d->rxavail, INIT_HDR_ONLY );
        initqueue( &d->txqh, &d->txavail, INIT_HDR_ONLY );
        initqueue( &d->bdqh, &d->bdavail, MAX_LIST_SIZE(d->bdq) );

        d->txstate = TXIDLE;
    }

    /* Completion queue */

    initqueue( &cqueueHead, &cqueueCount, INIT_HDR_ONLY);
    initqueue( &freecqHead, &freecqCount, MAX_LIST_SIZE(cqueue));

    sim_activate_after (&kmc_units[k], KMC_POLLTIME);

    gflags |= FLG_UCINI;
    return;
}
 
/*
 * Perform an input command
 *
 * The host must request ownership of the CSRs by setting RQI.
 * If enabled, it gets an interrupt when RDI sets, allowing it
 * to write the command.  RDI and RDO are mutually exclusive.
 *
 * Input commands are processed by the KMC when the host
 * clears RDI.  Upon completion of a command, 'all bits of bsel2
 * are cleared by the KMC'.  We don't implement this literally, since
 * the processing of a command can result in an immediate completion,
 * setting RDO and the other registers. 
 * Thus, although all bits are cleared before dispatching, RDO
 * and the other other bits of BSEL2 may be set for a output command
 * due to a completion if the host has cleared RQI.
 */

static void kmc_dispatchInputCmd(int32 k) {
    int line;
    int32 ba;
    int16 cmdsel2 = sel2;
    dupstate* d;
    
    line = (cmdsel2 & SEL2_LINE) >> SEL2_V_LINE;

    sel2 &= ~0xFF;                          /* Clear BSEL2. */
    if (sel0 & SEL0_RQI)                    /* If RQI was left on, grant the input request */
        sel2 |= SEL2_RDI;                   /* here so any generated completions will block. */

    if (line > MAX_LINE) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u line%u: Line number is out of range\n", k, line);
        kmc_halt (k, HALT_LINE);
        return;
    }
    d = line2dup[line];
    ba = ((sel6 & SEL6_CO_XAD) << (16-SEL6_V_CO_XAD)) | sel4;
    
    sim_debug(DF_CMD, &kmc_dev, "KMC%u line%u: INPUT COMMAND sel2=%06o sel4=%06o sel6=%06o ba=%06o\n", k, line,
              cmdsel2, sel4, sel6, ba);
    
    switch (cmdsel2 & (SEL2_IOT | SEL2_CMD)) {
    case CMD_BUFFIN:		                /* TX BUFFER IN */
        kmc_txBufferIn(d, ba, sel6);
        break;
    case CMD_CTRLIN:			            /* CONTROL IN. */
    case SEL2_IOT | CMD_CTRLIN:
        kmc_ctrlIn (k, d, line, ba);
        break;

    case CMD_BASEIN:			            /* BASE IN. */
        kmc_baseIn (k, d, cmdsel2, line, ba);
        break;

    case (SEL2_IOT | CMD_BUFFIN):			/* Buffer in, receive buffer for us... */
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
 *  only.  <17:13> are all set for IO page addresses.  The DUP 
 *  has 8 registers, so <2:1> must be zero.  The other bits are reserved
 *  and must be zero.  
 */
static void kmc_baseIn (int32 k, dupstate *d, uint16 cmdsel2, int line, uint32 ba ) {
    uint16 csr;
    uint32 csraddress;

    /* Verify DUP is enabled and at specified address */

    csraddress = sel6;
    if ((csraddress & ~SEL6_II_DUPCSR) || (sel4 != 0) ||
                       (cmdsel2 & SEL2_II_RESERVED)) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u: reserved bits set in BASE IN\n");
        kmc_halt (k, HALT_BADCSR);
        return;
    }
    csraddress += IOPAGEBASE;

    /* Verify that the DUP is on-line and that its CSRs can be read.
     * The hardware would probably discover this later.
     */
    if (Map_ReadW (csraddress, 2, &csr)) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u line%u: %060 0x%x DUP CSR0 NXM\n",
                   k, line, csraddress, csraddress);
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, line, 0); /* KMC would fail differently. */
        return;                                /* This will cause OS to take action. */
    }
    ba = dup_csr_to_linenum (sel6);
    if ((ba < 0) || (ba >= DIM(dupState))) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u line%u: %06o 0x%x is not an enabled DUP\n", 
                   k, line, csraddress, csraddress);
        kmc_halt (k, HALT_BADDUP);
        return;
    }
    d = &dupState[ba];
    if ((d->kmc != -1) && (d->kmc != k)) {
        sim_debug (DF_CMD, &kmc_dev, "KMC%u line%u: %06o 0x%x is already assigned to KMC%u\n",
                   k, line, csraddress, csraddress,
                   d->kmc);
        kmc_halt (k, HALT_DUPALC);
        return;
    }
    d->dupidx = ba;
    d->line = line;
    d->dupcsr = csraddress;
    d->kmc = k;
    line2dup[line] = d;
    sim_debug(DF_CMD, &kmc_dev, "KMC%u line%u: DUP%u address=%06o 0x%x assigned\n", 
                  k, line, d->dupidx,csraddress, csraddress);
    return;
}

/* Process CONTROL IN command
 *
 * CONTROL IN establishes the characteristics of each communication line
 * controlled by the KMC.  At least one CONTROL IN must be issued for each
 * DUP that is to communicate.
 *
 * CONTROL IN is also used to enable/disable a line, which also sets/clears DTR.
 */

static void kmc_ctrlIn (int32 k, dupstate *d, int line, uint32 ba ) {
    sim_debug(DF_CMD, &kmc_dev, "KMC%u line%u: DUP%d %s in %s duplex", 
              k, line, d->dupidx,
              (sel6 & SEL6_CI_DDCMP)? "DDCMP":"Bit-stuffing",
              (sel6 & SEL6_CI_HDX)? "half" : "full");
    if (sel6 & SEL6_CI_ENASS)
        sim_debug(DF_CMD, &kmc_dev, " SS:%u",
                  (sel6 & SEL6_CI_SADDR), line);
    sim_debug(DF_CMD, &kmc_dev, "  %s\n",
              (sel6 & SEL6_CI_ENABLE)? "enabled":"disabled");
        
    dup_set_DDCMP (d->dupidx, TRUE);
    d->modemstate &= ~MDM_DSR;           /* Initialize modem state reporting. */      
    d->ctrlFlags = sel6;
    if (sel6 & SEL6_CI_ENABLE) {
        dup_set_DTR (d->dupidx, TRUE);
        dup_set_callback_mode (d->dupidx, kdp_receive, kmc_txComplete, kmc_modemChange);
    } else {
        dup_set_DTR (d->dupidx, FALSE);
        dup_set_callback_mode (d->dupidx, NULL, kmc_txComplete, kmc_modemChange);
    }
    return;
}
   /*
    * RX BUFFER IN
    */

void kmc_rxBufferIn(dupstate *d, int32 ba, uint32 sel6v) {
    int32 k = d->kmc;
    BDL *qe;
    uint32 bda = 0;
    
    if (d->line == -1)
        return;

    if (!kmc_printBufferIn (k, &kmc_dev, d->line, "RX", d->rxavail, ba, sel6v))
        return;
    
    if (sel6v & SEL6_BI_KILL) {
        uint16 bd[3];
        
        /* Kill all current RX buffers.  The DUP currently provides the entire message in
         * one completion.  So this is fairly simple.  TODO: Ought to tell the DUP to resync.
         */
        if (d->rxavail) {
            qe = (BDL *)(d->rxqh.next);
            bda = qe->ba;
            /* Update bytes done should be done before the kill.  TOPS-10 clears the UBA map
             * before requesting it.  But it doesn't look at bd.  So don't report NXM.
             * We don't necessarily have the bd cached in this case, so we re-read it here.
             */
            if (!Map_ReadW (bda, 2*3, bd)) {
                bd[1] = 0;
                if (kmc_updateBDCount (bda, bd)) { ;/*
                                              kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, bda);
                                              return;*/
                }
            }
        }
        while ((qe = (BDL *)remqueue (d->rxqh.next, &d->rxavail)) != NULL) {
            assert (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
        }
        if (!(sel6v & SEL6_BI_ENABLE)) {
            kmc_ctrlOut (k, SEL6_CO_KDONE, SEL2_IOT, d->line, 0);
            return;
        }
    }
    
    if ((qe = (BDL *)remqueue (d->bdqh.next, &d->bdavail)) == NULL) {
        kmc_halt (k, HALT_RCVOVF);
        sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: Too many receive buffers from  hostd\n", k, d->line);
        return;
    }
    qe->ba = ba;
    assert (insqueue( &qe->hdr, d->rxqh.prev, &d->rxavail, MAXQUEUE));
    
    if (sel6v & SEL6_BI_KILL) { /* ENABLE is set too */
        kmc_ctrlOut (k, SEL6_CO_KDONE, SEL2_IOT, d->line, bda);
    }
    
    return;
}

static void kdp_receive(int dupidx, uint8* data, int count) {
    int32 k;
    dupstate* d;
    uint8 msgtyp;
    WB wb;

    assert ((dupidx >= 0) && (dupidx < DIM(dupState)));
    d = &dupState[dupidx];
    assert( dupidx == d->dupidx );
    k = d->kmc;
    
    wb.first = TRUE;
    wb.dup = d;
    wb.bd[1] = 0;
    wb.bd[2] = BDL_LDS;
    
    /* Flush messages too short to have a header, or with invalid msgtyp */
    if( count < 8 )
        return;
    
    msgtyp = data[0];
    if ( !((msgtyp == DDCMP_SOH) || (msgtyp == DDCMP_ENQ) || (msgtyp == DDCMP_DLE)) )
        return;
    
    
    /* Validate hcrc */
    
    if (0 != ddcmp_crc16 (0, data, 8)) {
        sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: HCRC Error for %d byte packet\n", k, d->line, count);
        if (!kmc_rxWriteViaBDL (&wb, data, 6))
            return;
        kmc_ctrlOut (k, SEL6_CO_HCRC, SEL2_IOT, d->line, wb.bda);
        return;
    }
    
    if (d->ctrlFlags & SEL6_CI_ENASS) {
        if (!(data[5] == (d->ctrlFlags & SEL6_CI_SADDR))) { /* Also include SELECT? */
            return;
        }
    }

    if (!kmc_rxWriteViaBDL (&wb, data, 6))
        return;
    
    if (msgtyp != DDCMP_ENQ) {
    /* The DUP has framed this mesage, so the length had better match
     * what's in the header.
     */
        assert( (((data[2] &~ 0300) << 8) | data[1]) == (count -10) );
        
        if (!kmc_rxWriteViaBDL (&wb, data+8, count -(8+2)))
            return;
        
        if (0 != ddcmp_crc16 (0, data+8, count-8)) {
            sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: data CRC error for %d byte packet\n", k, d->line, count);
            kmc_ctrlOut (k, SEL6_CO_HCRC, SEL2_IOT, d->line, wb.bda);
            return;
        }
    }
    
    wb.bd[1] = wb.rcvc;
    if (kmc_updateBDCount (wb.bda, wb.bd)) {
        kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, d->line, wb.bda);
        return;
    }
    
    if (!kmc_bufferAddressOut (k, SEL6_BO_EOM, SEL2_IOT, d->line, wb.bda))
        return;
    
    return;
}

/*
 * Here with a framed message to be delivered.
 * The DUP has ensured that the data starts with a DDCMP MSGTYP,
 * and that the entire message (thru BCC2) is in the data.
 *
 * In real hardware, the bytes would be processed one at a time, allowing
 * an OS time to provide a new BDL if necessary.  We can't do this with
 * the kdp_receive callback - without a lot of extra complexity.  So
 * for this approximation, any lack of buffer is treated as an RX overrun.
 */

static t_bool kmc_rxWriteViaBDL( WB *wb, uint8 *data, int count ) {
    int32 k = wb->dup->kmc;

    while (count != 0) {
        int seglen = count;
        int32 xrem;
        if ( wb->bd[1] == 0 ) {
            BDL    *bdl;
            
            if (wb->bd[2] & BDL_LDS) {
                dupstate *d = wb->dup;
                
                if (!wb->first) {
                    wb->bd[1] = wb->rcvc;
                    if (kmc_updateBDCount (wb->bda, wb->bd)) {
                        kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, wb->dup->line, wb->bda);
                        return FALSE;
                    }
                    if (!kmc_bufferAddressOut (k, 0, SEL2_IOT, wb->dup->line, wb->bda))
                        return FALSE;
                    wb->first = FALSE;
                }
                /* Get the first available buffer descriptor list, return to free queue */
                if (!(bdl = (BDL *)remqueue(d->rxqh.next, &d->rxavail))) {
                    kmc_ctrlOut(k, SEL6_CO_NOBUF, SEL2_IOT, d->line, 0 );
                    return FALSE;
                }
                wb->bda = bdl->ba;
                assert( insqueue (&bdl->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)) );
            } else {
                wb->bda += (3 * 2);
            }
            
            if (Map_ReadW (wb->bda, 3*2, wb->bd)) {
                kmc_ctrlOut(k, SEL6_CO_NXM, SEL2_IOT, wb->dup->line, wb->bda);
                return FALSE;
            }
            
            wb->ba = ((wb->bd[2] & 06000) << 6) | wb->bd[0];
            if( wb->bd[2] == 0) {
                kmc_halt (k, HALT_MTRCV);
                sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: RX buffer descriptor size is zero\n", k, wb->dup->line);
                return FALSE;
            }
            wb->rcvc = 0;
        }
        if (seglen > wb->bd[2] )
            seglen = wb->bd[2];
        xrem = Map_WriteB (wb->ba, seglen, data);
        if (xrem != 0) {
            uint16 bd[3];
            memcpy (bd, &wb->bd, sizeof bd);
            seglen -= xrem;
            wb->rcvc += seglen;
            bd[1] = wb->rcvc;
            kmc_updateBDCount (wb->bda, bd); /* Unchecked because already reporting NXM */
            kmc_ctrlOut (k, SEL6_CO_NXM, SEL2_IOT, wb->dup->line, wb->bda);
            return FALSE;
        }
        wb->ba += seglen;
        wb->rcvc += seglen;
        count -= seglen;
        data += seglen;
    }
    return TRUE;
}

/* Transmit */
/*
 * Here with a bdl for some new transmit buffers.
 * This has some timing/completion queue issues in
 * degenerate cases.  For now, assemble the whole
 * message for the DUP and complete when we're told.
 * If the DUP refuses a message, we drop it as we should
 * only be offering a message when the DUP is idle.  So
 * Any problem represents a a hard error.
 */

void kmc_txBufferIn(dupstate *d, int32 ba, uint32 sel6v) {
    int32 k = d->kmc;
    BDL *qe;

    if (d->line == -1)
        return;

    if (!kmc_printBufferIn (k, &kmc_dev, d->line, "TX", d->txavail, ba, sel6v))
        return;
    
    if (sel6v & SEL6_BI_KILL) {
        /* Kill all current TX buffers.  We can't abort the DUP in simulation, so
         * anything pending will stop when complete.  The queue is reset here because
         * the kill & replace option has to be able to enqueue the replacement BDL.
         * If a tx is active, the DUP will issue a completion, which will report completion.
         */
        while ((qe = (BDL *)remqueue (d->txqh.next, &d->txavail)) != NULL) {
            assert (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
        }
        if (d->txstate == TXIDLE) {
            if (!(sel6v & SEL6_BI_ENABLE)) {
                kmc_ctrlOut (k, SEL6_CO_KDONE, 0, d->line, 0);
                return;
            }
        } else {
            if (sel6v & SEL6_BI_ENABLE)
                d->txstate = TXKILR;
            else {
                d->txstate = TXKILL;
                return;
            }
        }
    }
    
    if (!(qe = (BDL *)remqueue (d->bdqh.next, &d->bdavail))) {
        kmc_halt (k, HALT_XMTOVF);
        sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: Too many transmit buffers from host\n", k, d->line);
        return;
    }
    qe->ba = ba;
    assert (insqueue (&qe->hdr, d->txqh.prev, &d->txavail, MAXQUEUE));
    if (d->txstate == TXIDLE)
        kmc_txStartDup(d);
   
    return;
}


/*
 * Try to start DUP output.  Does nothing if output is already in progress,
 * or if there are no packets in the output queue.
 */
static void kmc_txStartDup(dupstate *d) {
    int32 k = d->kmc;
    
    while (TRUE) {
        switch (d->txstate){
        case TXIDLE:
            if (!kmc_txNewBdl(d))
                return;
            d->txmlen = 0;
            d->txstate = TXSOM;
        case TXSOM:
            if (!(d->tx.bd[2] & BDL_SOM)) {
                kmc_halt (k, HALT_XSOM);
                sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: TX BDL not SOM\n", k, d->line);
                return;
            }
            d->txstate = TXHDR;
        case TXHDR:
            if (!kmc_txAppendBuffer(d))
                return;
            if (!(d->tx.bd[2] & BDL_EOM)) {
                if (!kmc_txNewBd(d))
                    return;
                continue;
            }
            if (d->txmsg[0] != DDCMP_ENQ) {
                /* If the OS computes and includes HRC, this can
                 * be the last descriptor.  In that case, this is EOM.
                 */
                if (d->tx.bd[2] & BDL_LDS) {
                    assert (d->tx.bd[2] & BDL_EOM);
                    assert (d->txmlen > 6);
                    d->txstate = TXACT;
                    if (!dup_put_msg_bytes (d->dupidx, d->txmsg, d->txmlen, TRUE, TRUE)) {
                        sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
                    }
                    return;
                }
                /* Data sent in a separate descriptor */
                d->txstate = TXDATA;
                d->tx.first = TRUE;
                d->tx.bda += 6;
                if (!kmc_txNewBd(d))
                    return;
                if (!(d->tx.bd[2] & BDL_SOM)) {
                    kmc_halt (k, HALT_XSOM2);
                    sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: TX BDL not SOM\n", k, d->line);
                    return;
                }
                continue;
            }
            assert (d->txmlen == 6);
            d->txstate = TXACT;
            if (!dup_put_ddcmp_packet (d->dupidx, d->txmsg, d->txmlen)) {
                sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
            }
            return;
        case TXDATA:
            if (!kmc_txAppendBuffer(d))
                return;
            if (!(d->tx.bd[2] & BDL_EOM)) {
                if (!kmc_txNewBd(d))
                    return;
                continue;
            }
            d->txstate = TXACT;
            if (!dup_put_ddcmp_packet (d->dupidx, d->txmsg, d->txmlen)) {
                sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: DUP%d refused TX packet\n", k, d->line, d->dupidx);
            }
            return;
        default:
        case TXACT:
            sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line%u: dup_start_output called while active\n", k, d->line);
            return;
        }
    }
}

/* Transmit complete callback from the DUP
 * Called with the last byte of a packet has been transmitted.
 * Handle any deferred kill and start the next message.
 */

static void kmc_txComplete (int32 dupidx, int status) {
    dupstate *d;
    int32 k;

    assert ((dupidx >= 0) && (dupidx < DIM(dupState)));

    d = &dupState[dupidx];
    k = d->kmc;

    if (status) { /* Failure is probably due to modem state change */
        kmc_updateDSR(d); /* Change does not stop transmission or release buffer */
    }
    d->txmlen = 0;
    if ((d->txstate == TXKILL) || (d->txstate == TXKILR)) {
        /* If we could kill a partial transmission, would update bd here */
        d->txstate = TXIDLE;
        kmc_ctrlOut (k, SEL6_CO_KDONE, 0, d->line, d->tx.bda);
    } else {
        if (d->tx.bd[2] & BDL_LDS)
            d->txstate = TXIDLE;
        else 
            d->txstate = TXSOM;
    }
    kmc_txStartDup(d);
}

/* Obtain a new buffer descriptor list from those queued by the host */

static t_bool kmc_txNewBdl(dupstate *d) {
    int32 k = d->kmc;
    BDL *qe;
    
    if (!(qe = (BDL *)remqueue (d->txqh.next, &d->txavail))) {
        return FALSE;
    }
    d->tx.bda = qe->ba;
    assert (insqueue (&qe->hdr, d->bdqh.prev, &d->bdavail, DIM(d->bdq)));
    
    d->tx.first = TRUE;
    d->tx.bd[1] = 0;
    return kmc_txNewBd(d);
}

/* Obtain a new TX buffer descriptor.
 *
 * Release the current BD if there is one.
 * If the current BD is the last of a list, request a new BDL.
 */

static t_bool kmc_txNewBd(dupstate *d) {
    int32 k = d->kmc;

    if (d->tx.first)
        d->tx.first = FALSE;
    else {
        d->tx.bd[1] = d->tx.xmtc;
        if (kmc_updateBDCount (d->tx.bda, d->tx.bd)) {
            kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, d->tx.bda);
            return FALSE;
        }
        if (!kmc_bufferAddressOut (k, 0, 0, d->line, d->tx.bda))
            return FALSE;
        if (d->tx.bd[2] & BDL_LDS) {
            if (!kmc_txNewBdl(d)) {
                /* TODO??xmit_underrun notice?*/
                return FALSE;
            }
            d->tx.first = FALSE;
        } else
            d->tx.bda += 6;
    }
    if (Map_ReadW (d->tx.bda, 2*3, d->tx.bd)) {
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, d->tx.bda);
        return FALSE;
    }
    
    d->tx.ba = ((d->tx.bd[2] & 0006000) << 6) | d->tx.bd[0];
    d->tx.xmtc = 0;
    return TRUE;
}

/* Append data from a host buffer to the current message, as
 * the DUP prefers to get the entire message in one swell foop.
 */

static t_bool kmc_txAppendBuffer(dupstate *d) {
    int32 k = d->kmc;
    int32 rem;

    if (!d->txmsg || (d->txmsize < d->txmlen+d->tx.bd[1])) {
        d->txmsize = d->txmlen+d->tx.bd[1];
        d->txmsg = (uint8 *)realloc(d->txmsg, d->txmsize);
        assert( d->txmsg );
    }
    rem = Map_ReadB (d->tx.ba, d->tx.bd[1], d->txmsg+d->txmlen);
    d->tx.bd[1] -= rem;
    rem += kmc_updateBDCount (d->tx.bda, d->tx.bd);
    if (rem) {
        kmc_ctrlOut (k, SEL6_CO_NXM, 0, d->line, d->tx.bda);
        return FALSE;
    }
    d->txmlen += d->tx.bd[1];
    if (!kmc_bufferAddressOut (k, 0, 0, d->line, d->tx.bda))
        return FALSE;
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

    if (sel2 & (SEL2_RDO | SEL2_RDI)) /* CSRs available? */
        return;

    if (!(qe = (CQ *)remqueue (cqueueHead.next, &cqueueCount))) {
        return;
    }

    assert( insqueue( &qe->hdr, freecqHead.prev, &freecqCount, CQUEUE_MAX ) );
    sel2 = qe->bsel2;
    sel4 = qe->bsel4;
    sel6 = qe->bsel6;

    sim_debug (DF_CMD, &kmc_dev, "KMC%u line%u: %s %s sel2=%06o sel4=%06o sel6=%06o\n",
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

  sim_debug (DF_QUEUE, &kmc_dev, "KMC%u line%u: enqueue %s CONTROL OUT Code=%02o Address=%06o\n",
             k, line, rx? "RX":"TX", code, bda);

  if (!(qe = (CQ *)remqueue( freecqHead.next, &freecqCount ))) {
      sim_debug (DF_QUEUE, &kmc_dev, "KMC%u line%u: Completion queue overflow\n", k, line);
      /* Set overflow status in last entry of queue */
      qe = (CQ *)cqueueHead.prev;
      qe->bsel2 |= SEL2_OVR;
    return;
  }
  qe->bsel2 = ((line << SEL2_V_LINE) & SEL2_LINE) | rx | CMD_CTRLOUT;
  qe->bsel4 = bda & 0177777;
  qe->bsel6 = ((bda & 0600000) >> (16-SEL6_V_CO_XAD)) | code;
  assert (insqueue( &qe->hdr, cqueueHead.prev, &cqueueCount, CQUEUE_MAX ));
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

  assert ((dupidx >= 0) && (dupidx < DIM(dupState)));
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
    status = status? MDM_DSR : 0;
    if (status != (d->modemstate & MDM_DSR)) {
        d->modemstate = (d->modemstate &~MDM_DSR) | status;
        kmc_ctrlOut (k, SEL6_CO_DSRCHG, 0, d->line, 0);
        return TRUE;
    }
    return FALSE;
}

/* Queue a BUFFER ADDRESS OUT command to the host.
 * flags are applied to bsel6 (e.g. receive EOM).
 * rx is TRUE for a receive buffer, false for transmit.
 * line is the line number assigned by BASE IN
 * bda is the address of the buffer descriptor that has been processed.
 *
 * Returns FALSE if the completion queue is full (a fatal error)
 */

static t_bool kmc_bufferAddressOut (int32 k, uint16 flags, uint16 rx, uint8 line, uint32 bda) {
    CQ *qe;

    sim_debug (DF_QUEUE, &kmc_dev, "KMC%u line%u: enqueue %s BUFFER OUT Flags=%06o Address=%06o\n",
               k, line, rx? "RX":"TX", flags, bda);
    
    if (!kmc_printBDL(k, DF_QUEUE, &kmc_dev, line, bda, 2))
        return FALSE;
    if (!(qe = (CQ *)remqueue( freecqHead.next, &freecqCount ))) {
        sim_debug (DF_QUEUE, &kmc_dev, "KMC%u line%u: Completion queue overflow\n", k, line);
        /* Set overflow status in last entry of queue */
        qe = (CQ *)cqueueHead.prev;
        qe->bsel2 |= SEL2_OVR;
        return FALSE;
    }
    qe->bsel2 = ((line << SEL2_V_LINE) & SEL2_LINE) | rx | CMD_BUFFOUT;
    qe->bsel4 = bda & 0177777;
    qe->bsel6 = ((bda & 0600000) >> (16-SEL6_V_CO_XAD)) | flags;
    assert (insqueue( &qe->hdr, cqueueHead.prev, &cqueueCount, CQUEUE_MAX ));

    kmc_processCompletions(k);
    return TRUE;
}

/* The KMC can only do byte NPRs, even for word quantities.
 * We shortcut by writng words when updating the buffer descriptor's
 * count field.  The UBA does not do a RPW cycle when byte 0 (of 4)
 * on a -10 is written.  (It can, but the OS doesn't program it that
 * way.  Thus, if the count word is in the left half-word, updating
 * it will trash the 3rd word of that buffer descriptor.  The hardware
 * works, so I suspect that the KMC always writes the whole descriptor.
 * That isn't sufficient: if the count is in the right half, writing
 * the whole descriptor would trash the first word of the following
 * descriptor.  So, I suspect that the KMC must read the next descriptor
 * (if the current one doesn't have LAST set) before updating the count.
 * This means the cached copy corrects any trashing.  Read before write
 * also would tend to minimize the chance of under-run, so it's a reasonable
 * guess.  This is, er, awkward to emulate.  As a compromise, we always
 * write the count.  We write the 3rd word iff the count is in the LH.
 * This is guaranteed to restore the third word if it was trashed and
 * not trash the following descriptor otherwise.  
 */

static int32 kmc_updateBDCount(uint32 bda, uint16 *bd) {
  
  return Map_WriteW (bda+2, (((bda+2) & 2)? 2 : 4), &bd[1]);
}

/* Halt a KMC.  This happens for some errors that the real KMC
 * may not detect, as well as when RUN is cleared.
 * The kmc is halted & interrupts are disabled.
 */

static void kmc_halt (int32 k, int error) {
    if (error){
        sel0 &= ~(SEL0_IEO|SEL0_IEI);
    }
    sel0 &= ~SEL0_RUN;

    kmc_updints (k);
    sim_cancel ( &kmc_units[k] );
    sim_debug (DF_INF, &kmc_dev, "KMC%u: Halted at uPC %04o reason=%d\n", k, upc, error);
    return;
}
/*
 * Update interrupts pending.
 *
 * Since the interrupt request is shared across all KMCs
 * (a simplification), we keep pending flags per KMC,
 * and a global request count across KMCs fot the UBA.
 * The UBA will clear the global request flag when it grants
 * an interrupt; thus for set we always set the global flag
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
            sim_debug(DF_TRC, &kmc_dev, "KMC%u: set input interrupt pending\n", k);
            gflags |= FLG_AINT;
            AintPending++;
        }
        SET_INT(KMCA);
    } else {
        if (gflags & FLG_AINT) {
            sim_debug(DF_TRC, &kmc_dev, "KMC%u: cleared pending input interrupt\n", k);
            gflags &= ~FLG_AINT;
            if (--AintPending == 0) {
                CLR_INT(KMCA);
            }
        }
    }

    if ((sel0 & SEL0_IEO) && (sel2 & SEL2_RDO)) {
        if (!(gflags & FLG_BINT)) {
            sim_debug(DF_TRC, &kmc_dev, "KMC%u: set output interrupt\n", k);
            gflags |= FLG_BINT;
            BintPending++;
        }
        SET_INT(KMCB);
    } else {
        if (gflags & FLG_BINT) {
            sim_debug(DF_TRC, &kmc_dev, "KKMC%u: clear output interrupt\n", k);
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
 * A given KMC should never have both input and output
 * pending at the same time.
 */

static int32 kmc_AintAck (void) {
    int32 vec = 0; /* no interrupt request active */
    int32 k;

    for (k = 0; k < DIM (kmc_gflags); k++) {
        if (gflags & FLG_AINT) {
            vec = kmc_dib.vec + (k*010);
            gflags &= ~FLG_AINT;
            if (--AintPending == 0) {
                CLR_INT(KMCA);
            }
        }
    }

    sim_debug(DF_TRC, &kmc_dev, "KMC%u input (A) interrupt ack vector %030\n", k, vec);

    return vec;
}

static int32 kmc_BintAck (void) {
    int32 vec = 0; /* no interrupt request active */
    int32 k;

    for (k = 0; k < DIM (kmc_gflags); k++) {
        if (gflags & FLG_BINT) {
            vec = kmc_dib.vec + 4 + (k*010);
            gflags &= ~FLG_BINT;
            if (--BintPending == 0) {
                CLR_INT(KMCB);
            }
        }
    }

    sim_debug(DF_TRC, &kmc_dev, "KMC%u output (B) interrupt ack vector %03o\n", k, vec);

    return vec;
}

/* Debug: Log a BUFFER IN or BUFFER OUT command.
 * returns FALSE if print encounters a NXM as (a) it's fatal and 
 * (b) only one completion per bdl.
 */

static t_bool kmc_printBufferIn (int32 k, DEVICE *dev, int32 line, char *dir, 
                                 int32 count, int32 ba, uint16 sel6v) {
    t_bool kill = ((sel6v & (SEL6_BI_KILL|SEL6_BI_ENABLE)) == SEL6_BI_KILL);

    sim_debug(DF_CMD, &kmc_dev, "KMC%u line %u %s BUFER IN%s\n", k, line, dir, (kill? "(Buffer kill)": ""));

    if (kill) /* Just kill doesn't use BDL, may NXM if attempt to dump */
        return TRUE;

    /* Kill and replace supplies new BDL */

    if (!kmc_printBDL(k, DF_CMD, dev, line, ba, 1))
        return FALSE;

    sim_debug(DF_QUEUE, &kmc_dev, "KMC%u line %u: %s BUFFER IN %d, bdas=%06o 0x%04X\n", k, line, dir, count, ba, ba);

    return TRUE;
}
/*
 * Debug: Dump a BDL and a sample of its buffer.
 */

static t_bool kmc_printBDL(int32 k, uint32 dbits, DEVICE *dev, uint8 line, int32 ba, int prbuf) {
    uint16 bd[3];
    int32 dp;

    if (!DEBUG_PRJ(dev,dbits) )
        return TRUE;

    for (;;) {
        if (Map_ReadW (ba, 3*2, bd) != 0) {
            kmc_ctrlOut(k, SEL6_CO_NXM, 0, line, ba );
            sim_debug(dbits,dev, "KMC%u line%u: NXM reading descriptor addr=%06o\n", k, line, ba);
            return FALSE;
        }
        
        sim_debug(dbits, dev, "  bd[0] = %06o 0x%04X\n", bd[0], bd[0]);
        sim_debug(dbits, dev, "  bd[1] = %06o 0x%04X\n", bd[1], bd[1]);
        sim_debug(dbits, dev, "  bd[2] = %06o 0x%04X\n", bd[2], bd[2]);
        
        if (prbuf) {
            uint8 buf[20];
            if (bd[1] > sizeof buf)
                bd[1] = sizeof buf;
            
            dp = bd[0] | ((bd[2] & 06000) << 6);
            if (Map_ReadB (dp, bd[1], buf) != 0) {
                kmc_ctrlOut(k, SEL6_CO_NXM, 0, line, dp );
                sim_debug(dbits, dev, "KMC%u line%u: NXM reading buffer %060\n", k, line, dp);
                return FALSE;
            }
            
            for (dp = 0; dp < bd[1]; dp++) {
                sim_debug(dbits, dev, " %02x", buf[dp]);
            }
            sim_debug(dbits, dev, "\r\n");
        }
        if ((bd[2] & BDL_LDS) || (prbuf & 2))
            break;
        ba += 6;
    }
    return TRUE;
}

/* Verify that the microcode image is one we know how to emulate.
 * As far as I know, there was COMM IOP-DUP V1.0 and one patch, V1.0A.
 * This is the patched version, which I have verified by rebuilding the
 * V1.0A microcode from sources and computing its CRC from the binary.
 *
 * The reason for this check is that there ARE other KMC microcodes.
 * If some software thinks it's loading one of those, the results
 * could be catastrophic - and hard to diagnose.  (COMM IOP-DZ has
 * a similar function; but there were other, stranger microcodes.
 */

static const char *kmc_verifyUcode ( int32 k ) {
    int i, n;
    uint16 crc = 'T' << 8 | 'L';
    uint8 w[2];
    static const struct {
        uint16 crc;
        const char *name;
    } known[] = {
        { 0xc3cd, "COMM IOP-DUP V1.0A" },
        { 0x1a38, "COMM IOP-DUP RSX" },
    };

    for (i = 0, n = 0; i < DIM (ucode); i++ ) {
        if (ucode[i] != 0 )
            n++;
        w[0] = ucode[i] >> 8;
        w[1] = ucode[i] & 0xFF;
        crc = ddcmp_crc16 (crc, &w, sizeof w);
    }
    if (n < ((3 * DIM (ucode))/4)) {
        sim_debug (DF_CMD, &kmc_dev, "KDP%u: Microcode not loaded\n", k );
        return NULL;
    }
    for (i = 0; i < DIM (known); i++) {
        if (crc == known[i].crc) {
            sim_debug (DF_CMD, &kmc_dev, "KDP%u: %s microdcode loaded\n", k, known[i].name );
            return known[i].name;
        }
    }
    sim_debug (DF_CMD, &kmc_dev, "KDP%u: Unknown microdcode loaded\n", k);
    return NULL;
}
/* Initialize a queue to empty.
 * Optionally, adds a list of elements to the queue.
 * max, list and size are only used if list is non-NULL.
 *
 * Convenience macros:
 *   MAX_LIST_SIZE(q) specifies max, list and size for an array of elements q
 *   INIT_HDR_ONLY provides placeholders for these arguments when only the
 * header and count are to be initialized.
 */

static void initqueue (QH *head, int32 *count, int32 max, void *list, size_t size ) {
    head->next = head->prev = head;
    *count = 0;
    if (list == NULL)
        return;
    while (insqueue( (QH *)list, head->prev, count, max ))
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

static void *remqueue (QH *entry, int32 *count)
{
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
 * The uptr is that of unit zero.
 */

#if KMC_UNITS > 1
static t_stat kmc_setDeviceCount (UNIT *uptr, int32 val, char *cptr, void *desc) {
    int32 newln;
    uint32 dupidx;
    t_stat r;
    DEVICE *dptr = &kmc_dev;

    for (dupidx = 0; dupidx < DIM (dupState); dupidx++) {
        dupstate *d = &dupState[dupidx];
        if ((d->kmc != -1) ||  (d->dupidx != -1)) {
            return SCPE_ALATT;
        }
    }
    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, KMC_UNITS, &r);
    if ((r != SCPE_OK) || (newln == dptr->numunits))
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
static t_stat kmc_showDeviceCount (FILE *st, UNIT *uptr, int32 val, void *desc) {
    DEVICE *dev = find_dev_from_unit(uptr);

    if (dev->flags & DEV_DIS) {
            fprintf (st, "Disabled");
            return SCPE_OK;
    }

    fprintf (st, "devices=%d", dev->numunits);
    return SCPE_OK;
}
#endif

/* Show KMC status */

t_stat kmc_showStatus ( FILE *st, UNIT *up, int32 v,  void *dp) {
    int32 k = up - kmc_units;
    int32 line;
    t_bool first = TRUE;
    DEVICE *dev = find_dev_from_unit(up);
    const char *ucname;

    if (dev->flags & DEV_DIS) {
            fprintf (st, "Disabled");
            return SCPE_OK;
    }

    ucname = kmc_verifyUcode (k);

    if (!(sel0 & SEL0_RUN)) {
        fprintf (st, "%s halted at uPC %04o", 
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
            } else {
                fprintf (st, "\n");
            }
            fprintf (st, "     %3u %3u %06o %-8s %3s %s %s %s",
                     line, d->dupidx, d->dupcsr,
                     (d->ctrlFlags & SEL6_CI_ENABLE)? "enabled": "disabled",
                     (d->modemstate & MDM_DSR)? "DSR" : "OFF",
                     (d->ctrlFlags & SEL6_CI_DDCMP)? "DDCMP" : "Bit-Stuff",
                     (d->ctrlFlags & SEL6_CI_HDX)? "HDX " : "FDX",
                     (d->ctrlFlags & SEL6_CI_NOCRC)? "NOCRC": "");
            if (d->ctrlFlags & SEL6_CI_ENASS)
                fprintf (st, " SS (%u) ", d->ctrlFlags & SEL6_CI_SADDR);
        }
    }
    if (first)
        fprintf (st, "     No DUPs assigned");

    return SCPE_OK;
}

/* Description of this device.
 * Conventionally last function in the file.
 */
static char *kmc_description (DEVICE *dptr) {
    return "KMC11-A Synchronous line controller supporting only COMM IOP/DUP microcode";
}
