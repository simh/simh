/* pdp11_dmc.c: DMC11/DMR11/DMP11/DMV11 Emulation
  ------------------------------------------------------------------------------

   Copyright (c) 2011, Robert M. A. Jarratt

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

  ------------------------------------------------------------------------------

  Modification history:

  25-Jan-13  RJ   Error checking for already attached in dmc_settype() fixed.
  25-Jan-13  RJ   If attach when already attached then detach first.
  23-Jan-13  RJ   Don't do anything if not attached. See https://github.com/simh/simh/issues/28
  23-Jan-13  RJ   Clock co-scheduling move to generic framework (from Mark Pizzolato)
  21-Jan-13  RJ   Added help.
  15-Jan-13  RJ   Contribution from Paul Koning:
                  Support for RSTS using the ROM INPUT (ROM I) command to get
                  the DMC11 to report DSR status.
                  Don't accept any data from the peer until a buffer has been
                  made available.
  04-Aug-13  MP   Massive Rewrite/Restructure to implement the DDCMP wire 
                  protocol for interoperation with other sync devices (DUP11, 
                  KDP, etc.)

  ------------------------------------------------------------------------------


I/O is done through sockets so that the remote system can be on the same 
host machine. The device starts polling for incoming connections when it 
receives its first read buffer. The device opens the connection for writing 
when it receives the first write buffer.

Transmit and receive buffers are added to their respective queues and the 
polling method in dmc_poll_svc() checks for input and sends any output.

Tested with two diagnostics. To run the diagnostics set the default 
directory to SYS$MAINTENANCE, run ESSAA and then configure it for the 
DMC-11 with the following commands:

The above commands can be put into a COM file in SYS$MAINTENANCE (works 
on VMS 3.0 but not 4.6, not sure why).

ATT DW780 SBI DW0 3 4
ATT DMC11 DW0 XMA0 760070 300 5
SELECT XMA0
(if putting these into a COM file to be executed by ESSAA add a "DS> " prefix)


The first is EVDCA which takes no parameters. Invoke it with the command 
R EVDCA. This diagnostic uses the DMC-11 loopback functionality and the 
transmit port is not used when LU LOOP is enabled. Seems to work only under 
later versions of VMS such as 4.6, does not work on 3.0.  It does not work
under VMS 3.x since in that environment, no receive buffers are ever made
available to the device while testing.

The second is EVDMC, invoke this with the command R EVDMC. For this I used 
the following commands inside the diagnostic:

RUN MODE=TRAN on one machine
RUN MODE=REC on the other

or using loopback mode:

SET TRANSMIT=CCITT/SIZE=25/COPY=5
SET EXPECT=CCITT/SIZE=25/COPY=5
RUN MODE=ACTIVE/LOOP=INT/STATUS/PASS=3

You can add /PASS=n to the above commands to get the diagnostic to send and 
receive more buffers.

The other test was to configure DECnet on VMS 4.6 and do SET HOST.
*/

// TODO: Test MOP.

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#if !defined(DMP_NUMDEVICE)
#define DMP_NUMDEVICE 1         /* Minimum number for array size DMP-11/DMV-11 devices */
#endif

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#include <assert.h>
#include "sim_tmxr.h"
#include "pdp11_ddcmp.h"

#define DMC_CONNECT_POLL    2   /* Seconds */

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

#if !defined(DMC_NUMDEVICE)
#define DMC_NUMDEVICE 8         /* default MAX # DMC-11 devices */
#endif

#if !defined(DMP_NUMDEVICE)
#define DMP_NUMDEVICE 8         /* default MAX # DMP-11/DMV-11 devices */
#endif

#define DMC_RDX                     8


/* DMC/DMR register SEL0 */
#define DMC_SEL0_V_ITYPE     0
#define DMC_SEL0_S_ITYPE     4
#define DMC_SEL0_M_ITYPE    (((1<<DMC_SEL0_S_ITYPE)-1)<<DMC_SEL0_V_ITYPE)
#define DMC_C_TYPE_XBACC     0      /* Transmit Buffer Address/Character Count Command and Response */
#define DMC_C_TYPE_CNTL      1      /* Control In Command and Control Out Response */
#define DMC_C_TYPE_HALT      2      /* Halt In Command */
#define DMC_C_TYPE_BASEIN    3      /* Base In Command */
#define DMC_C_TYPE_RBACC     4      /* Receive Buffer Address/Character Count Command and Response */
#define DMC_SEL0_V_RQI       5
#define DMC_SEL0_M_RQI      0x0020
#define DMC_SEL0_V_IEI       6
#define DMC_SEL0_M_IEI      0x0040
#define DMC_SEL0_V_RDI       7
#define DMC_SEL0_M_RDI      0x0080
#define DMC_SEL0_V_STEPUP    8
#define DMC_SEL0_M_STEPUP   0x0100
#define DMC_SEL0_V_ROMI      9
#define DMC_SEL0_M_ROMI     0x0200
#define DMC_SEL0_V_ROMO     10
#define DMC_SEL0_M_ROMO     0x0400
#define DMC_SEL0_V_LU_LOOP  11
#define DMC_SEL0_M_LU_LOOP  0x0800
#define DMC_SEL0_V_STEPLU   12
#define DMC_SEL0_M_STEPLU   0x1000
#define DMC_SEL0_V_UDIAG    13
#define DMC_SEL0_M_UDIAG    0x2000
#define DMC_SEL0_V_MCLEAR   14
#define DMC_SEL0_M_MCLEAR   0x4000
#define DMC_SEL0_V_RUN      15
#define DMC_SEL0_M_RUN      0x8000

/* DMP/DMV register SEL0 */
#define DMP_SEL0_V_IEI       0
#define DMP_SEL0_M_IEI      0x0001
#define DMP_SEL0_V_IEO       4
#define DMP_SEL0_M_IEO      0x0010
#define DMP_SEL0_V_RQI       7
#define DMP_SEL0_M_RQI      0x0080

/* DMC/DMR register SEL2 */
#define DMC_SEL2_V_CODE      0
#define DMC_SEL2_S_CODE      3
#define DMC_SEL2_M_CODE     (((1<<DMC_SEL2_S_CODE)-1)<<DMC_SEL2_V_CODE)
#define DMC_SEL2_V_IEO       6
#define DMC_SEL2_M_IEO      0x0040
#define DMC_SEL2_V_RDO       7
#define DMC_SEL2_M_RDO      0x0080

/* DMP/DMV register SEL2 */
#define DMP_SEL2_V_CODE      0
#define DMP_SEL2_S_CODE      3
#define DMP_SEL2_M_CODE     (((1<<DMP_SEL2_S_CODE)-1)<<DMP_SEL2_V_CODE)
#define DMP_C_TYPE_RBACC     0      /* Receive Buffer Address/Character Count Command and Response */
#define DMP_C_TYPE_CNTL      1      /* Control Command or Control Response */
#define DMP_C_TYPE_MODE      2      /* Mode Definition Command or Information Response */
#define DMP_C_TYPE_UNUSED    3      /* Receive Buffer disposition (unused) Response */
#define DMP_C_TYPE_XBACC     4      /* Transmit Buffer Address/Character Count Command and Response */
#define DMP_C_TYPE_RESERVED  5      /* Reserved/Unused code */
#define DMP_C_TYPE_SENT      6      /* Buffer disposition (sent but not acknowledged) Response */
#define DMP_C_TYPE_UNSENT    7      /* Buffer disposition (not sent) Response */
#define DMP_SEL2_V_22BIT     3
#define DMP_SEL2_M_22BIT    0x0008
#define DMP_SEL2_V_RDI       4
#define DMP_SEL2_M_RDI      0x0010
#define DMP_SEL2_V_RDO       7
#define DMP_SEL2_M_RDO      0x0080

/* DMC/DMR register SEL4 */
#define DMC_SEL4_V_CAR       0
#define DMC_SEL4_M_CAR      0x0001
#define DMC_SEL4_V_STN       1
#define DMC_SEL4_M_STN      0x0002
#define DMC_SEL4_V_CTS       2
#define DMC_SEL4_M_CTS      0x0004
#define DMC_SEL4_V_DSR       3
#define DMC_SEL4_M_DSR      0x0008
#define DMC_SEL4_V_HDX       4
#define DMC_SEL4_M_HDX      0x0010
#define DMC_SEL4_V_RTS       5
#define DMC_SEL4_M_RTS      0x0020
#define DMC_SEL4_V_DTR       6
#define DMC_SEL4_M_DTR      0x0040
#define DMC_SEL4_V_RI        7
#define DMC_SEL4_M_RI       0x0080

/* SEL2 */
#define DMP_TYPE_INPUT_MASK 0x0007

/* DMC/DMR register SEL6 */
/* Bit Flags used in a Control Out command */
#define DMC_SEL6_V_NAKTRSH   0      /* NAK Threshold */
#define DMC_SEL6_M_NAKTRSH  0x0001
#define DMC_SEL6_V_TIMEOUT   1
#define DMC_SEL6_M_TIMEOUT  0x0002
#define DMC_SEL6_V_NOBUF     2
#define DMC_SEL6_M_NOBUF    0x0004
#define DMC_SEL6_V_MAINTRCV  3
#define DMC_SEL6_M_MAINTRCV 0x0008
#define DMC_SEL6_V_LOSTDATA  4
#define DMC_SEL6_M_LOSTDATA 0x0010
#define DMC_SEL6_V_DISCONN   6
#define DMC_SEL6_M_DISCONN  0x0040
#define DMC_SEL6_V_STRTRCVD  7
#define DMC_SEL6_M_STRTRCVD 0x0080
#define DMC_SEL6_V_NXM       8
#define DMC_SEL6_M_NXM      0x0100
#define DMC_SEL6_V_HALTCOMP  9
#define DMC_SEL6_M_HALTCOMP 0x0200
/* Bit Flags used in a Control In command */
#define DMC_SEL6_V_MAINT     8      /* NAK Threshold */
#define DMC_SEL6_M_MAINT    0x0100
#define DMC_SEL6_V_HDX      10
#define DMC_SEL6_M_HDX      0x0400
#define DMC_SEL6_V_LONGSTRT 11
#define DMC_SEL6_M_LONGSTRT 0x0800


#define DSPDSR     0x22b3    /* KMC opcode to move line unit status to SEL2 */
#define DROPDTR    0xa40b    /* KMC opcode to drop DTR */
#define UINST_CNF  0x2296    /* KMC opcode to get config switches */
#define UINST_RROM 0x814d    /* KMC opcode to read DMC ROM */

#define SEL2_TYPEO_BIT 0
#define SEL2_RDO_BIT 7
#define SEL2_IEO_BIT 6
#define SEL2_OUT_IO_BIT 2
#define SEL2_LINE_BIT 8
#define SEL2_LINE_BIT_LENGTH 6
#define SEL2_PRIO_BIT 14
#define SEL2_PRIO_BIT_LENGTH 2

#define DMC_QUEUE_SIZE  7
#define DMR_QUEUE_SIZE 64
#define DMP_QUEUE_SIZE 64

struct csrs {
    uint16 *sel0;
    uint16 *sel2;
    uint16 *sel4;
    uint16 *sel6;
    uint16 *sel10;
    };

typedef struct csrs CSRS;

typedef enum {
    Uninitialised,  /* before MASTER CLEAR */
    Initialised,    /* after MASTER CLEAR */
    Running,        /* after any transmit or receive buffer has been supplied */
    Halted          /* after reciept of explicit halt input command */
    } ControllerState;

typedef enum {
    Idle,
    InputTransfer,
    OutputTransfer,
    OutputControl
    } TransferState;

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
    struct buffer_queue *queue;
    };
typedef struct queuehdr QH;

typedef struct buffer_queue {
    QH hdr;                             /* Forward/Back Buffer pointers */
    char * name;                        /* Queue name */
    size_t size;                        /* Maximum number of entries (0 means no limit) */
    size_t count;                       /* Current Used Count */
    struct dmc_controller *controller;  /* back pointer to the containing controller */
    } BUFFER_QUEUE;

/* Queue management */

/* Insert entry on queue after pred, if count < max.
 * Increment count.
 *   To insert at head of queue, specify &head for predecessor.
 *   To insert at tail, specify head.prev
 *
 * returns FALSE if queue is full.
 */

static t_bool insqueue (QH *entry, QH *pred)
{
if ((pred->queue->size > 0) && (pred->queue->count >= pred->queue->size))
    return FALSE;
assert (entry->queue == NULL);
entry->next = pred->next;
entry->prev = pred;
entry->queue = pred->queue;
pred->next->prev = entry;
pred->next = entry;
++pred->queue->count;
return TRUE;
}

/* Remove entry from queue.
 * Decrement count.
 *  To remove from head of queue, specify head.next.
 *  To remove form tail of queue, specify head.prev.
 *
 * returns NULL if queue is empty.
 */

static void *remqueue (QH *entry)
{
if (entry->queue->count == 0)
    return NULL;
entry->prev->next = entry->next;
entry->next->prev = entry->prev;
--entry->queue->count;
entry->next = entry->prev = NULL;
entry->queue = NULL;
return (void *)entry;
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

#define DIM(x) (sizeof(x)/sizeof((x)[0]))
/* Convenience for initqueue() calls */
#    define MAX_LIST_SIZE(q)                    DIM(q),    (q),       sizeof(q[0])
#    define INIT_HDR_ONLY                       0,         NULL,      0

static void initqueue (QH *head, struct buffer_queue *queue, size_t max, void *list, size_t size)
{
head->next = head->prev = head;
head->queue = queue;
head->queue->count = 0;
head->queue->size = max;
if (list == NULL)
    return;
while (insqueue ((QH *)list, head->prev))
    list = (QH *)(((char *)list)+size);
return;
}

typedef enum {
    Receive,                /* Receive Buffer */
    TransmitData,           /* Transmit Buffer */
    TransmitControl         /* Transmit Control Packet */
    } BufferType;

typedef struct buffer {
    QH hdr;                   /* queue linkage */
    BufferType type;          /* 0 = Receive Buffer, 1 = Transmit Buffer */
    uint32 address;           /* unibus address of the buffer (or 0 for DDCMP control messages) */
    uint16 count;             /* size of the buffer passed to the device by the driver */
    uint8 *transfer_buffer;   /* the buffer into which data is received or from which it is transmitted*/
    int actual_bytes_transferred;/* the number of bytes from the actual block that have been read or written */
    uint32 buffer_return_time;/* time to return buffer to host */
    } BUFFER;

typedef enum {
    Halt,       /* initially */
    IStart,     /* Initiating Start */
    AStart,     /* Acknowledging Start */
    Run,        /* Start ACK received */
    Maintenance,/* off-line maintenance messages */
    All         /* Not settable, but match for any state in state table */
    } DDCMP_LinkState;

typedef struct {
    uint8 R;                /* number of the highest sequential data message received . 
                               Sent in the RESP field of data messages, ACK messages,
                               and NAK messages as acknowledgment to the other station. */
    uint8 N;                /* Number of the highest sequential data message 
                               transmitted.  Sent in the NUM field of REP 
                               messages. N is the number assigned to the last 
                               user transmit request which has been transmitted 
                               (sent in the NUM field of that data message).*/
    uint8 A;                /* Number of the highest sequential data message acknowledged.
                               Received in the RESP field of data messages, ACK messages, 
                               and NAK messages.*/
    uint8 T;                /* Number of the next data message to be transmitted.
                               When sending new data messages T will have the 
                               value N+l. When retransmitting T will be set back
                               to A+l and will advance to N+l.*/
    uint8 X;                /* Number of the last data message that has been 
                               transmitted.  When a new data message has been 
                               completely transmitted X will have the value N. 
                               When retransmitting and receiving acknowledgments 
                               asynchronously with respect to transmission, X will 
                               have some value less than or equal to N. */
    t_bool SACK;            /* Send ACK flag.  This flag is set when either R 
                               is incremented, meaning a new sequential data 
                               message has been received which requires an ACK 
                               reply, or a REP message is received which requires 
                               an ACK reply. The SACK flag is cleared when 
                               sending either a DATA message with the latest RESP 
                               field information, an ACK with the latest RESP 
                               field information, or when the SNAK flag is set. */
                 /* 
                    The SACK and SNAK flags are mutually exclusive. At most one 
                    will be set at a given time. The events that set the SACK 
                    flag, R is increment~d or a REP is received requiring an ACK, 
                    also clear the SNAK flag. Similarly, the events that set the 
                    SNAK flag, reasons for send ing a NAK (see 5.3.7), also clear 
                    the SACK flag. Setting or clearing a flag that is already set 
                    or cleared respectively has no effect. For the SNAK flag, a 
                    reason variable (or field) is also maintained which is 
                    overwritten with the latest NAK error reason.
                    Whenever the SNAK flag is set the NAK reason variable is set 
                    to the reason for the NAK. */
    t_bool SNAK;            /* Send NAK flag.  This flag is set when a receive 
                               error occurs that requires a NAK reply. It is 
                               cleared when a NAK message is sent with the 
                               latest RESP information, or when the SACK flag
                               is set.*/
    t_bool SREP;            /* Send REP flag.  This flag is set when a reply 
                               timer expires in the running state and a REP 
                               should be sent. It is independent of the SACK 
                               and SNAK flags.*/
    uint8 *rcv_pkt;         /* Current Rcvd Packet buffer */
    uint16 rcv_pkt_size;    /* Current Rcvd Packet size */
    BUFFER *xmt_buffer;     /* Current Transmit Buffer */
    BUFFER *xmt_done_buffer;/* Just Completed Transmit Buffer */
    uint8 nak_reason;       /*  */
    DDCMP_LinkState state;  /* Current State */
    t_bool TimerRunning;    /* Timer Running Flag */
    t_bool TimeRemaining;   /* Seconds remaining before timeout (when timer running) */
    t_bool Scanning;        /* Event Scanning in progress */
    uint32 ScanningEvents;  /* Event Mask while scanning */
    t_bool RecurseScan;     /* Scan was attempted while scanning */
    uint32 RecurseEventMask;/* Mask for recursive Scan */
#define DDCMP_EVENT_XMIT_DONE 0x001
#define DDCMP_EVENT_PKT_RCVD  0x002
#define DDCMP_EVENT_TIMER     0x004
#define DDCMP_EVENT_MAINTMODE 0x008
    } DDCMP;

typedef struct control {
    struct control *next;       /* Link */
    uint16  sel6;               /* Control Out Status Flags */
    } CONTROL_OUT;

typedef enum {
    DMC,
    DMR,
    DMP
    } DEVTYPE;

typedef struct dmc_controller {
    CSRS *csrs;
    DEVICE *device;
    UNIT *unit;
    int index;                      /* Index in controller array */
    ControllerState state;
    TransferState transfer_state;   /* current transfer state (type of transfer) */
    DDCMP link;
    int transfer_type;              /* Input Command setting at start of input transfer as host changes it during transfer! */
    TMLN *line;
    BUFFER_QUEUE *rcv_queue;        /* Receive Buffer Queue */
    BUFFER_QUEUE *completion_queue; /* Transmit and Recieve Buffers waiting to pass to driver */
    BUFFER_QUEUE *xmt_queue;        /* Control and/or Data packets pending transmission */
    BUFFER_QUEUE *ack_wait_queue;   /* Data packets awaiting acknowledgement */
    BUFFER_QUEUE *free_queue;       /* Unused Buffer Queue */
    BUFFER **buffers;               /* Buffers */
    CONTROL_OUT *control_out;
    DEVTYPE dev_type;
    uint32 in_int;
    uint32 out_int;
    uint32 dmc_wr_delay;
    uint32 *baseaddr;
    uint16 *basesize;
    uint8 *modem;
    int32 *corruption_factor;
    uint32 buffers_received_from_net;
    uint32 buffers_transmitted_to_net;
    uint32 receive_buffer_output_transfers_completed;
    uint32 transmit_buffer_output_transfers_completed;
    uint32 receive_buffer_input_transfers_completed;
    uint32 transmit_buffer_input_transfers_completed;
    uint32 control_out_operations_completed;
    uint32 ddcmp_control_packets_received;
    uint32 ddcmp_control_packets_sent;
    uint32 byte_wait;                           /* rcv/xmt byte delay */
    } CTLR;

/* 
    DDCMP implementation follows the DDCMP protocol specification documented in the 
    "DECNET DIGITAL NETWORK ARCHITECTURE Digital Data Communications Message Protocol"
    Version 4.0, March 1, 1978.
    
    available on http://bitsavers.org/pdf/dec/decnet/AA-D599A-TC_DDCMP4.0_Mar78.pdf

 */

typedef void (*DDCMP_LinkAction_Routine)(CTLR *controller);

typedef t_bool (*DDCMP_Condition_Routine)(CTLR *controller);

t_bool ddcmp_UserHalt             (CTLR *controller);
t_bool ddcmp_UserStartup          (CTLR *controller);
t_bool ddcmp_UserMaintenanceMode  (CTLR *controller);
t_bool ddcmp_ReceiveStack         (CTLR *controller);
t_bool ddcmp_ReceiveStrt          (CTLR *controller);
t_bool ddcmp_TimerRunning         (CTLR *controller);
t_bool ddcmp_TimerNotRunning      (CTLR *controller);
t_bool ddcmp_TimerExpired         (CTLR *controller);
t_bool ddcmp_ReceiveMaintMessage  (CTLR *controller);
t_bool ddcmp_ReceiveAck           (CTLR *controller);
t_bool ddcmp_ReceiveNak           (CTLR *controller);
t_bool ddcmp_ReceiveRep           (CTLR *controller);
t_bool ddcmp_NUMEqRplus1          (CTLR *controller);   /* (NUM == R+1) */
t_bool ddcmp_NUMGtRplus1          (CTLR *controller);   /* (NUM > R+1) */
t_bool ddcmp_ReceiveDataMsg       (CTLR *controller);   /* Receive Data Message */
t_bool ddcmp_ReceiveMaintMsg      (CTLR *controller);   /* Receive Maintenance Message */
t_bool ddcmp_ALtRESPleN           (CTLR *controller);   /* (A < RESP <= N) */
t_bool ddcmp_ALeRESPleN           (CTLR *controller);   /* (A <= RESP <= N) */
t_bool ddcmp_RESPleAOrRESPgtN     (CTLR *controller);   /* (RESP <= A) OR (RESP > N) */
t_bool ddcmp_TltNplus1            (CTLR *controller);   /* T < N + 1 */
t_bool ddcmp_TeqNplus1            (CTLR *controller);   /* T == N + 1 */
t_bool ddcmp_ReceiveMessageError  (CTLR *controller);   
t_bool ddcmp_NumEqR               (CTLR *controller);   /* (NUM == R) */
t_bool ddcmp_NumNeR               (CTLR *controller);   /* (NUM != R) */
t_bool ddcmp_TransmitterIdle      (CTLR *controller);
t_bool ddcmp_TramsmitterBusy      (CTLR *controller);
t_bool ddcmp_SACKisSet            (CTLR *controller);
t_bool ddcmp_SACKisClear          (CTLR *controller);
t_bool ddcmp_SNAKisSet            (CTLR *controller);
t_bool ddcmp_SNAKisClear          (CTLR *controller);
t_bool ddcmp_SREPisSet            (CTLR *controller);
t_bool ddcmp_SREPisClear          (CTLR *controller);
t_bool ddcmp_UserSendMessage      (CTLR *controller);
t_bool ddcmp_LineConnected        (CTLR *controller);
t_bool ddcmp_LineDisconnected     (CTLR *controller);
t_bool ddcmp_DataMessageSent      (CTLR *controller);
t_bool ddcmp_REPMessageSent       (CTLR *controller);

void ddcmp_StartTimer             (CTLR *controller);
void ddcmp_StopTimer              (CTLR *controller);
void ddcmp_ResetVariables         (CTLR *controller);
void ddcmp_SendStrt               (CTLR *controller);
void ddcmp_SendStack              (CTLR *controller);
void ddcmp_SendAck                (CTLR *controller);
void ddcmp_SendNak                (CTLR *controller);
void ddcmp_SendRep                (CTLR *controller);
void ddcmp_SetSACK                (CTLR *controller);
void ddcmp_ClearSACK              (CTLR *controller);
void ddcmp_SetSNAK                (CTLR *controller);
void ddcmp_ClearSNAK              (CTLR *controller);
void ddcmp_SetSREP                (CTLR *controller);
void ddcmp_ClearSREP              (CTLR *controller);
void ddcmp_IncrementR             (CTLR *controller);
void ddcmp_SetAeqRESP             (CTLR *controller);
void ddcmp_SetTequalAplus1        (CTLR *controller);
void ddcmp_IncrementT             (CTLR *controller);
void ddcmp_SetNAKreason3          (CTLR *controller);
void ddcmp_SetNAKreason2          (CTLR *controller);
void ddcmp_NAKMissingPackets      (CTLR *controller);
void ddcmp_IfTleAthenSetTeqAplus1 (CTLR *controller);
void ddcmp_IfAltXthenStartTimer   (CTLR *controller);
void ddcmp_IfAgeXthenStopTimer    (CTLR *controller);
void ddcmp_Ignore                 (CTLR *controller);
void ddcmp_GiveBufferToUser       (CTLR *controller);
void ddcmp_CompleteAckedTransmits (CTLR *controller);
void ddcmp_ReTransmitMessageT     (CTLR *controller);
void ddcmp_NotifyDisconnect       (CTLR *controller);
void ddcmp_NotifyStartRcvd        (CTLR *controller);
void ddcmp_NotifyMaintRcvd        (CTLR *controller);
void ddcmp_SendDataMessage        (CTLR *controller);
void ddcmp_SendMaintMessage       (CTLR *controller);
void ddcmp_SetXSetNUM             (CTLR *controller);

typedef struct _ddcmp_state_table {
    int RuleNumber;
    DDCMP_LinkState State;
    DDCMP_Condition_Routine Conditions[10];
    DDCMP_LinkState NewState;
    DDCMP_LinkAction_Routine Actions[10];
    } DDCMP_STATETABLE;

DDCMP_STATETABLE DDCMP_TABLE[] = {
    { 0, All,         {ddcmp_UserHalt},            Halt,           {ddcmp_StopTimer}},
    { 1, Halt,        {ddcmp_UserStartup,
                       ddcmp_LineConnected},       IStart,         {ddcmp_ResetVariables,
                                                                    ddcmp_SendStrt,
                                                                    ddcmp_StopTimer}},
    { 2, Halt,        {ddcmp_UserMaintenanceMode}, Maintenance,    {ddcmp_ResetVariables}},
    { 3, Halt,        {ddcmp_ReceiveMaintMsg},     Maintenance,    {ddcmp_ResetVariables, 
                                                                    ddcmp_NotifyMaintRcvd}},
    { 4, IStart,      {ddcmp_TimerNotRunning},     IStart,         {ddcmp_StartTimer}},
    { 5, IStart,      {ddcmp_ReceiveStack},        Run,            {ddcmp_SetSACK,
                                                                    ddcmp_StopTimer}},
    { 6, IStart,      {ddcmp_ReceiveStrt},         AStart,         {ddcmp_SendStack, 
                                                                    ddcmp_StartTimer}},
    { 7, IStart,      {ddcmp_TimerExpired},        IStart,         {ddcmp_ResetVariables, 
                                                                    ddcmp_SendStrt, 
                                                                    ddcmp_StartTimer}},
    { 8, IStart,      {ddcmp_ReceiveMaintMsg},     Maintenance,    {ddcmp_ResetVariables, 
                                                                    ddcmp_NotifyMaintRcvd}},
    { 9, IStart,      {ddcmp_ReceiveMessageError}, IStart,         {ddcmp_Ignore}},
    {10, IStart,      {ddcmp_UserSendMessage,
                       ddcmp_TimerNotRunning},     IStart,         {ddcmp_StartTimer}},
    {11, IStart,      {ddcmp_LineDisconnected},    Halt,           {ddcmp_StopTimer}},
    {12, AStart,      {ddcmp_LineDisconnected},    Halt,           {ddcmp_StopTimer}},
    {13, AStart,      {ddcmp_ReceiveAck},          Run,            {ddcmp_StopTimer}},
    {14, AStart,      {ddcmp_ReceiveDataMsg},      Run,            {ddcmp_StopTimer}},
    {15, AStart,      {ddcmp_ReceiveStack},        Run,            {ddcmp_SetSACK, 
                                                                    ddcmp_StopTimer}},
    {16, AStart,      {ddcmp_TimerExpired},        AStart,         {ddcmp_SendStack, 
                                                                    ddcmp_StartTimer}},
    {17, AStart,      {ddcmp_ReceiveMaintMsg},     Maintenance,    {ddcmp_ResetVariables, 
                                                                    ddcmp_NotifyMaintRcvd}},
    {18, AStart,      {ddcmp_ReceiveMessageError}, AStart,         {ddcmp_Ignore}},
    {19, Run,         {ddcmp_LineDisconnected},    Halt,           {ddcmp_StopTimer,
                                                                    ddcmp_NotifyDisconnect,
                                                                    ddcmp_NotifyStartRcvd}},
    {20, Run,         {ddcmp_ReceiveStrt},         Halt,           {ddcmp_NotifyStartRcvd}},
    {21, Run,         {ddcmp_ReceiveMaintMsg},     Maintenance,    {ddcmp_ResetVariables, 
                                                                    ddcmp_NotifyMaintRcvd}},
    {22, Run,         {ddcmp_ReceiveStack},        Run,            {ddcmp_SetSACK}},
    {23, Run,         {ddcmp_ReceiveDataMsg,
                       ddcmp_NUMGtRplus1},         Run,            {ddcmp_NAKMissingPackets}},
    {24, Run,         {ddcmp_ReceiveDataMsg,
                       ddcmp_NUMEqRplus1},         Run,            {ddcmp_GiveBufferToUser}},
    {25, Run,         {ddcmp_ReceiveMessageError}, Run,            {ddcmp_SetSNAK}},
    {26, Run,         {ddcmp_ReceiveRep,
                       ddcmp_NumEqR},              Run,            {ddcmp_SetSACK}},
    {27, Run,         {ddcmp_ReceiveRep,
                       ddcmp_NumNeR},              Run,            {ddcmp_SetSNAK, 
                                                                    ddcmp_SetNAKreason3}},
    {28, Run,         {ddcmp_ReceiveDataMsg,
                       ddcmp_ALtRESPleN},          Run,            {ddcmp_CompleteAckedTransmits, 
                                                                    ddcmp_SetAeqRESP, 
                                                                    ddcmp_IfTleAthenSetTeqAplus1, 
                                                                    ddcmp_IfAgeXthenStopTimer}},
    {29, Run,         {ddcmp_ReceiveAck,
                       ddcmp_ALtRESPleN},          Run,            {ddcmp_CompleteAckedTransmits, 
                                                                    ddcmp_SetAeqRESP, 
                                                                    ddcmp_IfTleAthenSetTeqAplus1, 
                                                                    ddcmp_IfAgeXthenStopTimer}},
    {30, Run,         {ddcmp_ReceiveAck,
                       ddcmp_RESPleAOrRESPgtN},    Run,            {ddcmp_Ignore}},
    {31, Run,         {ddcmp_ReceiveDataMsg,
                       ddcmp_RESPleAOrRESPgtN},    Run,            {ddcmp_Ignore}},
    {32, Run,         {ddcmp_ReceiveNak,
                       ddcmp_ALeRESPleN},          Run,            {ddcmp_CompleteAckedTransmits, 
                                                                    ddcmp_SetAeqRESP, 
                                                                    ddcmp_SetTequalAplus1, 
                                                                    ddcmp_StopTimer}},
    {33, Run,         {ddcmp_ReceiveNak,
                       ddcmp_RESPleAOrRESPgtN},    Run,            {ddcmp_Ignore}},
    {34, Run,         {ddcmp_TimerExpired},        Run,            {ddcmp_SetSREP}},
    {35, Run,         {ddcmp_TransmitterIdle,
                       ddcmp_SNAKisSet},           Run,            {ddcmp_SendNak, 
                                                                    ddcmp_ClearSNAK}},
    {36, Run,         {ddcmp_TransmitterIdle,
                       ddcmp_SNAKisClear,
                       ddcmp_SREPisSet},           Run,            {ddcmp_SendRep, 
                                                                    ddcmp_ClearSREP}},
    {37, Run,         {ddcmp_TransmitterIdle,
                       ddcmp_SNAKisClear,
                       ddcmp_SREPisClear,
                       ddcmp_TltNplus1},           Run,            {ddcmp_ReTransmitMessageT, 
                                                                    ddcmp_IncrementT, 
                                                                    ddcmp_ClearSACK}},
    {38, Run,         {ddcmp_UserSendMessage,
                       ddcmp_TeqNplus1,
                       ddcmp_TransmitterIdle,
                       ddcmp_SNAKisClear,
                       ddcmp_SREPisClear},         Run,            {ddcmp_SendDataMessage, 
                                                                    ddcmp_ClearSACK}},
    {39, Run,         {ddcmp_TransmitterIdle,
                       ddcmp_SNAKisClear,
                       ddcmp_SREPisClear,
                       ddcmp_SACKisSet,
                       ddcmp_TeqNplus1},           Run,            {ddcmp_SendAck, 
                                                                    ddcmp_ClearSACK}},
    {40, Run,         {ddcmp_DataMessageSent},     Run,            {ddcmp_SetXSetNUM, 
                                                                    ddcmp_IfAltXthenStartTimer, 
                                                                    ddcmp_IfAgeXthenStopTimer}},
    {41, Run,         {ddcmp_REPMessageSent},      Run,            {ddcmp_StartTimer}},
    {42, Maintenance, {ddcmp_ReceiveMaintMsg},     Maintenance,    {ddcmp_GiveBufferToUser}},
    {43, Maintenance, {ddcmp_UserSendMessage,
                       ddcmp_TransmitterIdle},     Maintenance,    {ddcmp_SendMaintMessage}},
    {44, All}           /* End of Table */
    };

typedef struct _ddcmp_condition_name {
    DDCMP_Condition_Routine Condition;
    const char *Name;
    } DDCMP_CONDITION_NAME;

#define NAME(name) {ddcmp_##name,#name}
DDCMP_CONDITION_NAME ddcmp_Condition_Names[] = {
    NAME(UserHalt),
    NAME(UserStartup),
    NAME(UserMaintenanceMode),
    NAME(ReceiveStack),
    NAME(ReceiveStrt),
    NAME(TimerRunning),
    NAME(TimerNotRunning),
    NAME(TimerExpired),
    NAME(ReceiveMaintMessage),
    NAME(ReceiveAck),
    NAME(ReceiveNak),
    NAME(ReceiveRep),
    NAME(NUMEqRplus1),
    NAME(NUMGtRplus1),
    NAME(ReceiveDataMsg),
    NAME(ReceiveMaintMsg),
    NAME(ALtRESPleN),
    NAME(ALeRESPleN),
    NAME(RESPleAOrRESPgtN),
    NAME(TltNplus1),
    NAME(TeqNplus1),
    NAME(ReceiveMessageError),
    NAME(NumEqR),
    NAME(NumNeR),
    NAME(TransmitterIdle),
    NAME(TramsmitterBusy),
    NAME(SACKisSet),
    NAME(SACKisClear),
    NAME(SNAKisSet),
    NAME(SNAKisClear),
    NAME(SREPisSet),
    NAME(SREPisClear),
    NAME(UserSendMessage),
    NAME(LineConnected),
    NAME(LineDisconnected),
    NAME(DataMessageSent),
    NAME(REPMessageSent),
    {NULL, NULL}
    };

char *ddcmp_conditions(DDCMP_Condition_Routine *Conditions)
{
static char buf[512];
DDCMP_CONDITION_NAME *Name;

buf[0] = '\0';
while (*Conditions)
    {
    for (Name = ddcmp_Condition_Names; Name->Condition != NULL; ++Name) {
        if (Name->Condition == *Conditions)
            break;
        }
    ++Conditions;
    if (Name->Name)
        sprintf (&buf[strlen(buf)], "%s%s", Name->Name, *Conditions ? " && " : "");
    }
return buf;
}

typedef struct _ddcmp_action_name {
    DDCMP_LinkAction_Routine Action;
    const char *Name;
    } DDCMP_ACTION_NAME;

DDCMP_ACTION_NAME ddcmp_Actions[] = {
    NAME(StartTimer),
    NAME(StopTimer),
    NAME(ResetVariables),
    NAME(SendStrt),
    NAME(SendStack),
    NAME(SendAck),
    NAME(SendNak),
    NAME(SendRep),
    NAME(SetSACK),
    NAME(ClearSACK),
    NAME(SetSNAK),
    NAME(ClearSNAK),
    NAME(SetSREP),
    NAME(ClearSREP),
    NAME(IncrementR),
    NAME(SetAeqRESP),
    NAME(SetTequalAplus1),
    NAME(IncrementT),
    NAME(SetNAKreason3),
    NAME(SetNAKreason2),
    NAME(NAKMissingPackets),
    NAME(IfTleAthenSetTeqAplus1),
    NAME(IfAltXthenStartTimer),
    NAME(IfAgeXthenStopTimer),
    NAME(Ignore),
    NAME(GiveBufferToUser),
    NAME(CompleteAckedTransmits),
    NAME(ReTransmitMessageT),
    NAME(NotifyDisconnect),
    NAME(NotifyStartRcvd),
    NAME(NotifyMaintRcvd),
    NAME(SendDataMessage),
    NAME(SendMaintMessage),
    NAME(SetXSetNUM),
    {NULL, NULL}
    };

char *ddcmp_actions(DDCMP_LinkAction_Routine *Actions)
{
static char buf[512];
DDCMP_ACTION_NAME *Name;

buf[0] = '\0';
while (*Actions)
    {
    for (Name = ddcmp_Actions; Name->Action != NULL; ++Name) {
        if (Name->Action == *Actions)
            break;
        }
    ++Actions;
    if (Name->Name)
        sprintf (&buf[strlen(buf)], "%s%s", Name->Name, *Actions ? " + " : "");
    }
return buf;
}


DDCMP_LinkState NewState;
DDCMP_LinkAction_Routine Actions[10];

#define ctlr up7                        /* Unit back pointer to controller */

void ddcmp_dispatch               (CTLR *controller, uint32 EventMask);


t_stat dmc_rd (int32* data, int32 PA, int32 access);
t_stat dmc_wr (int32  data, int32 PA, int32 access);
t_stat dmc_svc (UNIT * uptr);
t_stat dmc_poll_svc (UNIT * uptr);
t_stat dmc_timer_svc (UNIT * uptr);
t_stat dmc_reset (DEVICE * dptr);
t_stat dmc_attach (UNIT * uptr, char * cptr);
t_stat dmc_detach (UNIT * uptr);
int32 dmc_ininta   (void);
int32 dmc_outinta (void);
t_stat dmc_setnumdevices (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dmc_shownumdevices (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat dmc_setpeer (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showpeer (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setspeed (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showspeed (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setcorrupt (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat dmc_showcorrupt (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat dmc_set_microdiag (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_show_microdiag (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_settype (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showtype (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setstats (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showstats (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_showqueues (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setconnectpoll (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showconnectpoll (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_showddcmp (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat dmc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *dmc_description (DEVICE *dptr);
char *dmp_description (DEVICE *dptr);
int dmc_is_attached (UNIT* uptr);
int dmc_is_dmc (CTLR *controller);
int dmc_is_rqi_set (CTLR *controller);
int dmc_is_rdyi_set (CTLR *controller);
int dmc_is_iei_set (CTLR *controller);
int dmc_is_ieo_set (CTLR *controller);
uint32 dmc_get_addr (CTLR *controller);
void dmc_set_addr (CTLR *controller, uint32 addr);
uint16 dmc_get_count (CTLR *controller);
void dmc_set_count (CTLR *controller, uint16 count);
uint8 dmc_get_modem (CTLR *controller);
void dmc_set_modem_dtr (CTLR *controller);
void dmc_clr_modem_dtr (CTLR *controller);
void dmc_process_command (CTLR *controller);
t_bool dmc_buffer_fill_receive_buffers (CTLR *controller);
void dmc_start_transfer_buffer (CTLR *controller);
void dmc_buffer_queue_init (CTLR *controller, BUFFER_QUEUE *q, char *name, size_t size, BUFFER *buffers);
void dmc_buffer_queue_init_all (CTLR *controller);
BUFFER *dmc_buffer_queue_head (BUFFER_QUEUE *q);
BUFFER *dmc_buffer_allocate (CTLR *controller);
t_bool dmc_transmit_queue_empty  (CTLR *controller);
void dmc_ddcmp_start_transmitter (CTLR *controller);
void dmc_queue_control_out (CTLR *controller, uint16 sel6);

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_REG  0x0002                                 /* trace programatic read/write registers */
#define DBG_RGC  0x0004                                 /* internal read/write registers changes */
#define DBG_WRN  0x0008                                 /* display warnings */
#define DBG_INF  0x0010                                 /* display informational messages (high level trace) */
#define DBG_DTS  (DDCMP_DBG_PXMT|DDCMP_DBG_PRCV)        /* display data summary */
#define DBG_DAT  (DBG_DTS|DDCMP_DBG_PDAT)               /* display data buffer contents */
#define DBG_MDM  0x0040                                 /* modem related transitions */
#define DBG_CON  TMXR_DBG_CON                           /* display socket open/close, connection establishment */
#define DBG_INT  0x0080                                 /* display interrupt activites */


DEBTAB dmc_debug[] = {
    {"TRACE",   DBG_TRC},
    {"WARN",    DBG_WRN},
    {"REG",     DBG_REG},
    {"INTREG",  DBG_RGC},
    {"INFO",    DBG_INF},
    {"DATA",    DBG_DAT},
    {"DATASUM", DBG_DTS},
    {"MODEM",   DBG_MDM},
    {"CONNECT", DBG_CON},
    {"INT",     DBG_INT},
    {0}
    };

UNIT dmc_units[DMC_NUMDEVICE+2];            /* One per device plus an I/O polling unit and a timing unit */

UNIT dmc_unit_template = { UDATA (&dmc_svc, UNIT_ATTABLE, 0) };
UNIT dmc_poll_unit_template = { UDATA (&dmc_poll_svc, UNIT_DIS, 0) };
UNIT dmc_timer_unit_template = { UDATA (&dmc_timer_svc, UNIT_DIS, 0) };

UNIT dmp_units[DMP_NUMDEVICE+2];            /* One per device plus an I/O polling unit and a timing unit */

CSRS dmc_csrs[DMC_NUMDEVICE];
uint16 dmc_sel0[DMC_NUMDEVICE];
uint16 dmc_sel2[DMC_NUMDEVICE];
uint16 dmc_sel4[DMC_NUMDEVICE];
uint16 dmc_sel6[DMC_NUMDEVICE];

uint32 dmc_speed[DMC_NUMDEVICE];
char dmc_peer[DMC_NUMDEVICE][CBUFSIZE];
char dmc_port[DMC_NUMDEVICE][CBUFSIZE];
uint32 dmc_baseaddr[DMC_NUMDEVICE];
uint16 dmc_basesize[DMC_NUMDEVICE];
uint8 dmc_modem[DMC_NUMDEVICE];
t_bool dmc_microdiag[DMC_NUMDEVICE];
int32 dmc_corruption[DMC_NUMDEVICE];

CSRS dmp_csrs[DMP_NUMDEVICE];
uint16 dmp_sel0[DMC_NUMDEVICE];
uint16 dmp_sel2[DMC_NUMDEVICE];
uint16 dmp_sel4[DMC_NUMDEVICE];
uint16 dmp_sel6[DMC_NUMDEVICE];
uint16 dmp_sel10[DMC_NUMDEVICE];

uint32 dmp_speed[DMP_NUMDEVICE];
char dmp_peer[DMP_NUMDEVICE][CBUFSIZE];
char dmp_port[DMP_NUMDEVICE][CBUFSIZE];
uint32 dmp_baseaddr[DMP_NUMDEVICE];
uint16 dmp_basesize[DMP_NUMDEVICE];
uint8 dmp_modem[DMP_NUMDEVICE];
int32 dmp_corruption[DMC_NUMDEVICE];

TMLN dmc_ldsc[DMC_NUMDEVICE];               /* line descriptors */
TMXR dmc_desc = { 1, NULL, 0, dmc_ldsc };   /* mux descriptor */
uint32 dmc_connect_poll = DMC_CONNECT_POLL; /* seconds between polls when no connection */

uint32 dmc_ini_summary = 0;         /* In Command Interrupt Summary for all controllers */
uint32 dmc_outi_summary = 0;        /* Out Command Interrupt Summary for all controllers */

BUFFER_QUEUE dmc_rcv_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_completion_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_xmt_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_ack_wait_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_free_queues[DMC_NUMDEVICE];
BUFFER *dmc_buffers[DMC_NUMDEVICE];

TMLN dmp_ldsc[DMC_NUMDEVICE];               /* line descriptors */
TMXR dmp_desc = { 1, NULL, 0, dmp_ldsc };   /* mux descriptor */
uint32 dmp_connect_poll;                    /* seconds between polls when no connection */

BUFFER_QUEUE dmp_rcv_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_completion_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_xmt_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_ack_wait_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_free_queues[DMP_NUMDEVICE];
BUFFER *dmp_buffers[DMC_NUMDEVICE];

REG dmc_reg[] = {
    { GRDATAD (RXINT,      dmc_ini_summary, DEV_RDX, 32, 0,                     "input interrupt summary") },
    { GRDATAD (TXINT,     dmc_outi_summary, DEV_RDX, 32, 0,                     "output interrupt summary") },
    { GRDATAD (POLL,      dmc_connect_poll, DEV_RDX, 32, 0,                     "connect poll interval") },
    { BRDATAD (SEL0,              dmc_sel0, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 0 CSR") },
    { BRDATAD (SEL2,              dmc_sel2, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 2 CSR") },
    { BRDATAD (SEL4,              dmc_sel4, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 4 CSR") },
    { BRDATAD (SEL6,              dmc_sel6, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 6 CSR") },
    { BRDATAD (SPEED,            dmc_speed, DEV_RDX, 32, DMC_NUMDEVICE,         "line speed") },
    { BRDATAD (CORRUPT,     dmc_corruption, DEV_RDX, 32, DMC_NUMDEVICE,         "data corruption factor (0.1%)") },
    { BRDATAD (DIAG,         dmc_microdiag, DEV_RDX,  1, DMC_NUMDEVICE,         "Microdiagnostic Enabled") },
    { BRDATAD (PEER,              dmc_peer, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE, "peer address:port") },
    { BRDATAD (PORT,              dmc_port, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE, "listen port") },
    { BRDATAD (BASEADDR,      dmc_baseaddr, DEV_RDX, 32, DMC_NUMDEVICE,         "program set base address") },
    { BRDATAD (BASESIZE,      dmc_basesize, DEV_RDX, 16, DMC_NUMDEVICE,         "program set base size") },
    { BRDATAD (MODEM,            dmc_modem, DEV_RDX,  8, DMC_NUMDEVICE,         "modem control bits") },
    { NULL }  };

REG dmp_reg[] = {
    { GRDATAD (RXINT,      dmc_ini_summary, DEV_RDX, 32, 0,                     "input interrupt summary") },
    { GRDATAD (TXINT,     dmc_outi_summary, DEV_RDX, 32, 0,                     "output interrupt summary") },
    { GRDATAD (POLL,      dmp_connect_poll, DEV_RDX, 32, 0,                     "connect poll interval") },
    { BRDATAD (SEL0,              dmp_sel0, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 0 CSR") },
    { BRDATAD (SEL2,              dmp_sel2, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 2 CSR") },
    { BRDATAD (SEL4,              dmp_sel4, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 4 CSR") },
    { BRDATAD (SEL6,              dmp_sel6, DEV_RDX, 16, DMC_NUMDEVICE,         "Select 6 CSR") },
    { BRDATAD (SEL10,            dmp_sel10, DEV_RDX, 16, DMP_NUMDEVICE,         "Select 10 CSR") },
    { BRDATAD (SPEED,            dmp_speed, DEV_RDX, 32, DMC_NUMDEVICE,         "line speed") },
    { BRDATAD (CORRUPT,     dmp_corruption, DEV_RDX, 32, DMC_NUMDEVICE,         "data corruption factor (0.1%)") },
    { BRDATAD (PEER,              dmp_peer, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE, "peer address:port") },
    { BRDATAD (PORT,              dmp_port, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE, "listen port") },
    { BRDATAD (BASEADDR,      dmp_baseaddr, DEV_RDX, 32, DMC_NUMDEVICE,         "program set base address") },
    { BRDATAD (BASESIZE,      dmp_basesize, DEV_RDX, 16, DMC_NUMDEVICE,         "program set base size") },
    { BRDATAD (MODEM,            dmp_modem, DEV_RDX,  8, DMP_NUMDEVICE,         "modem control bits") },
    { NULL }  };

extern DEVICE dmc_dev;

MTAB dmc_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmc_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN,          0, "MICRODIAG", "MICRODIAG={ENABLE,DISABLE}" ,
        &dmc_set_microdiag, &dmc_show_microdiag, NULL, "MicroDiagnostic Enable" },
#if !defined (VM_PDP10)
    { MTAB_XTD|MTAB_VUN|MTAB_VALR,0, "TYPE", "TYPE={DMR,DMC}" ,&dmc_settype, &dmc_showtype, NULL, "Set/Display device type"  },
#endif
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display/Clear statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "DDCMP", "DDCMP",
        NULL, &dmc_showddcmp, NULL, "Display DDCMP state information" },
    { MTAB_XTD|MTAB_VDV,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, (void *) &dmc_connect_poll, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR|MTAB_NMO, 0, "CORRUPT", "CORRUPTION=factor (0=uncorrupted)" ,
      &dmc_setcorrupt, &dmc_showcorrupt, NULL, "Display corruption factor (0.1% of packets)" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 } };

extern DEVICE dmp_dev;

MTAB dmp_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmp_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display/Clear statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "DDCMP", "DDCMP",
        NULL, &dmc_showddcmp, NULL, "Display DDCMP state information" },
    { MTAB_XTD|MTAB_VDV,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, (void *) &dmp_connect_poll, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR|MTAB_NMO, 0, "CORRUPTION", "CORRUPTION=factor (0=uncorrupted)" ,
      &dmc_setcorrupt, &dmc_showcorrupt, NULL, "Display corruption factor (0.1% of packets)" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 } };

extern DEVICE dmv_dev;

MTAB dmv_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmv_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display/Clear statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "DDCMP", "DDCMP",
        NULL, &dmc_showddcmp, NULL, "Display DDCMP state information" },
    { MTAB_XTD|MTAB_VDV,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, (void *) &dmp_connect_poll, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 } };

#define IOLN_DMC        010

DIB dmc_dib = { IOBA_AUTO, IOLN_DMC, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta}, IOLN_DMC };

#define IOLN_DMP        010

DIB dmp_dib = { IOBA_AUTO, IOLN_DMP, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta }, IOLN_DMP};

#define IOLN_DMV        020

DIB dmv_dib = { IOBA_AUTO, IOLN_DMV, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta }, IOLN_DMV};

DEVICE dmc_dev =
    { 
#if defined (VM_PDP10)
    "DMR",
#else
    "DMC", 
#endif
    dmc_units, dmc_reg, dmc_mod, 3, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmc_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_DONTAUTO, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmc_description };

/*
   We have two devices defined here (dmp_dev and dmv_dev) which have the 
   same units.  This would normally never be allowed since two devices can't
   actually share units.  This problem is avoided in this case since both 
   devices start out as disabled and the logic in dmc_reset allows only 
   one of these devices to be enabled at a time.  The DMP device is allowed 
   on Unibus systems and the DMV device Qbus systems.
   This monkey business is necessary due to the fact that although both
   the DMP and DMV devices have almost the same functionality and almost
   the same register programming interface, they are different enough that
   they fall in different priorities in the autoconfigure address and vector
   rules.
 */
DEVICE dmp_dev =
    { "DMP", dmp_units, dmp_reg, dmp_mod, 3, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_DONTAUTO, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmp_description };

DEVICE dmv_dev =
    { "DMV", dmp_units, dmp_reg, dmv_mod, 3, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_QBUS | DEV_DEBUG | DEV_DONTAUTO, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmp_description };

CTLR dmc_ctrls[DMC_NUMDEVICE + DMP_NUMDEVICE];

int dmc_is_attached(UNIT* uptr)
{
return uptr->flags & UNIT_ATT;
}

int dmc_is_dmc(CTLR *controller)
{
return controller->dev_type != DMP;
}

CTLR *dmc_get_controller_from_unit(UNIT *unit)
{
return (CTLR *)unit->ctlr;
}

CTLR* dmc_get_controller_from_address(uint32 address)
{
int i;

for (i=0; i<DMC_NUMDEVICE + DMP_NUMDEVICE; i++) {
    DIB *dib = (DIB *)dmc_ctrls[i].device->ctxt;
    if ((address >= dib->ba) && (address < (dib->ba + dib->lnt)))
        return &dmc_ctrls[(address - dib->ba) >> ((UNIBUS) ? 3 : 4)];
    }
/* not found */
return 0;
}

t_stat dmc_showpeer (FILE* st, UNIT* uptr, int32 val, void* desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
char *peer = ((dptr == &dmc_dev)? &dmc_peer[dmc][0] : &dmp_peer[dmc][0]);

if (peer[0])
    fprintf(st, "peer=%s", peer);
else
    fprintf(st, "peer=unspecified");
return SCPE_OK;
}

t_stat dmc_setpeer (UNIT* uptr, int32 val, char* cptr, void* desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
char *peer = ((dptr == &dmc_dev)? &dmc_peer[dmc][0] : &dmp_peer[dmc][0]);
t_stat status = SCPE_OK;
char host[CBUFSIZE], port[CBUFSIZE];

if ((!cptr) || (!*cptr))
    return SCPE_ARG;
if (dmc_is_attached(uptr))
    return SCPE_ALATT;
status = sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL);
if (status != SCPE_OK)
    return status;
if (host[0] == '\0')
    return SCPE_ARG;
strncpy(peer, cptr, CBUFSIZE-1);
return status;
}

t_stat dmc_showspeed (FILE* st, UNIT* uptr, int32 val, void* desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
uint32 *speeds = ((dptr == &dmc_dev)? dmc_speed : dmp_speed);

if (speeds[dmc] > 0)
    fprintf(st, "speed=%d bits/sec", speeds[dmc]);
else
    fprintf(st, "speed=0 (unrestricted)");
return SCPE_OK;
}


t_stat dmc_setspeed (UNIT* uptr, int32 val, char* cptr, void* desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
uint32 *speeds = ((dptr == &dmc_dev)? dmc_speed : dmp_speed);
t_stat r;
int32 newspeed;

if (cptr == NULL)
    return SCPE_ARG;
newspeed = (int32) get_uint (cptr, 10, 100000000, &r);
if (r != SCPE_OK)
    return r;
speeds[dmc] = newspeed;
return SCPE_OK;
}

t_stat dmc_show_microdiag (FILE* st, UNIT* uptr, int32 val, void* desc)
{
int32 dmc = (int32)(uptr-dmc_dev.units);

fprintf(st, "MicroDiag=%s", dmc_microdiag[dmc] ? "enabled" : "disabled");
return SCPE_OK;
}

/* Manage the corruption troll's appetite, in units of milli-gulps.
 *
 * See ddcmp_feedCorruptionTroll for usage.
 */
t_stat dmc_setcorrupt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
int32 *hunger = (dptr == &dmc_dev) ? &dmc_corruption[dmc] : &dmp_corruption[dmc];
t_stat r;
int32 appetite;

if ((cptr == NULL) || (*cptr == '\0'))
    return SCPE_ARG;

appetite = (int32) get_uint (cptr, 10, 999, &r);
if (r != SCPE_OK)
    return r;

*hunger = appetite;

return SCPE_OK;
}

/* Display the corruption troll's appetite */

t_stat dmc_showcorrupt (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
int32 *hunger = (dptr == &dmc_dev) ? &dmc_corruption[dmc] : &dmp_corruption[dmc];

if (*hunger)
    fprintf(st, "Corruption=%d milligulps (%.1f%% of messages processed)", 
             *hunger, ((double)*hunger)/10.0);
else
    fprintf(st, "No Corruption");

return SCPE_OK;
}

t_stat dmc_set_microdiag(UNIT* uptr, int32 val, char* cptr, void* desc)
{
int32 dmc = (int32)(uptr-dmc_dev.units);
char gbuf[CBUFSIZE];

if ((cptr == NULL) || (*cptr == '\0'))
    return SCPE_ARG;
get_glyph (cptr, gbuf, 0);
if (MATCH_CMD (gbuf, "ENABLE") == 0)
    dmc_microdiag[dmc] = TRUE;
else
    if (MATCH_CMD (gbuf, "DISABLE") == 0)
        dmc_microdiag[dmc] = FALSE;
    else
        return SCPE_ARG;
return SCPE_OK;
}

t_stat dmc_showtype (FILE* st, UNIT* uptr, int32 val, void* desc)
{
CTLR *controller = dmc_get_controller_from_unit(uptr);

switch (controller->dev_type) {
    case DMC:
        fprintf(st, "type=DMC");
        break;
    case DMR:
        fprintf(st, "type=DMR");
        break;
    case DMP:
        fprintf(st, "type=%s", (UNIBUS) ? "DMP" : "DMV");
        break;
    default:
        fprintf(st, "type=???");
        break;
    }
return SCPE_OK;
}

t_stat dmc_settype (UNIT* uptr, int32 val, char* cptr, void* desc)
{
char gbuf[80];
t_stat status = SCPE_OK;
CTLR *controller = dmc_get_controller_from_unit(uptr);

if ((!cptr) || (!*cptr))
    return SCPE_2FARG;
if (dmc_is_attached(uptr))
    return SCPE_ALATT;
cptr = get_glyph (cptr, gbuf, 0);
if (strcmp (gbuf,"DMC") == 0)
    controller->dev_type = DMC;
else 
    if (strcmp (gbuf,"DMR") == 0)
        controller->dev_type = DMR;
    else
        status = SCPE_ARG;

return status;
}

t_stat dmc_showstats (FILE* st, UNIT* uptr, int32 val, void* desc)
{
CTLR *controller = dmc_get_controller_from_unit(uptr);
    
fprintf (st, "%s%d\n", controller->device->name, (int)(uptr-controller->device->units));

tmxr_fstats (st, controller->line, -1);

fprintf(st, "Buffers received from the network=%d\n", controller->buffers_received_from_net);
fprintf(st, "Buffers sent to the network=%d\n", controller->buffers_transmitted_to_net);
fprintf(st, "Output transfers completed for receive buffers=%d\n", controller->receive_buffer_output_transfers_completed);
fprintf(st, "Output transfers completed for transmit buffers=%d\n", controller->transmit_buffer_output_transfers_completed);
fprintf(st, "Input transfers completed for receive buffers=%d\n", controller->receive_buffer_input_transfers_completed);
fprintf(st, "Input transfers completed for transmit buffers=%d\n", controller->transmit_buffer_input_transfers_completed);
fprintf(st, "Control Out operations processed=%d\n", controller->control_out_operations_completed);
fprintf(st, "DDCMP control packets received=%d\n", controller->ddcmp_control_packets_received);
fprintf(st, "DDCMP control packets sent=%d\n", controller->ddcmp_control_packets_sent);

return SCPE_OK;
}

void dmc_showqueue (FILE* st, BUFFER_QUEUE *queue, t_bool detail)
{
size_t i;

fprintf (st, "%s Queue:\n", queue->name);
fprintf (st, "    Size: %d\n", (int)queue->size);
fprintf (st, "   Count: %d\n", (int)queue->count);
if (detail) {
    BUFFER *buffer = (BUFFER *)queue->hdr.next;
    size_t same;

    for (i=same=0; i<queue->count; ++i) {
        if ((i == 0) || 
            (0 != memcmp(((char *)buffer)+sizeof(QH), ((char *)(buffer->hdr.prev))+sizeof(QH), sizeof(*buffer)-sizeof(QH)))) {
            if (same > 0)
                fprintf (st, "%s.queue[%d] thru %s.queue[%d] same as above\n", queue->name, (int)(i-same), queue->name, (int)(i-1));
            fprintf (st, "%s.queue[%d] at %p\n", queue->name, (int)i, buffer);
            fprintf (st, "   address:                  0x%08X\n", buffer->address);
            fprintf (st, "   count:                    0x%04X\n", buffer->count);
            fprintf (st, "   actual_bytes_transferred: 0x%04X\n", buffer->actual_bytes_transferred);
            if (buffer->buffer_return_time)
                fprintf (st, "   buffer_return_time:       0x%08X\n", buffer->buffer_return_time);
            if (strcmp(queue->name, "transmit") == 0) {
                uint8 *msg = buffer->transfer_buffer;
                static const char *const flags [4] = { "..", ".Q", "S.", "SQ" };
                static const char *const nak[18] = { "", " (HCRC)", " (DCRC)", " (REPREPLY)", /* 0-3 */
                                                     "", "", "", "",                          /* 4-7 */
                                                     " (NOBUF)", " (RXOVR)", "", "",          /* 8-11 */
                                                     "", "", "", "",                          /* 12-15 */
                                                     " (TOOLONG)", " (HDRFMT)" };             /* 16-17 */
                const char *flag = flags[msg[2]>>6];
                int msg2 = msg[2] & 0x3F;

                switch (msg[0]) {
                    case DDCMP_SOH:   /* Data Message */
                        fprintf (st, "Data Message, Count: %d, Num: %d, Flags: %s, Resp: %d, HDRCRC: %s, DATACRC: %s\n", (msg2 << 8)|msg[1], msg[4], flag, msg[3], 
                                                    (0 == ddcmp_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == ddcmp_crc16 (0, msg+8, 2+((msg2 << 8)|msg[1]))) ? "OK" : "BAD");
                        break;
                    case DDCMP_ENQ:   /* Control Message */
                        fprintf (st, "Control: Type: %d ", msg[1]);
                        switch (msg[1]) {
                            case DDCMP_CTL_ACK: /* ACK */
                                fprintf (st, "(ACK) ACKSUB: %d, Flags: %s, Resp: %d\n", msg2, flag, msg[3]);
                                break;
                            case DDCMP_CTL_NAK: /* NAK */
                                fprintf (st, "(NAK) Reason: %d%s, Flags: %s, Resp: %d\n", msg2, ((msg2 > 17)? "": nak[msg2]), flag, msg[3]);
                                break;
                            case DDCMP_CTL_REP: /* REP */
                                fprintf (st, "(REP) REPSUB: %d, Num: %d, Flags: %s\n", msg2, msg[4], flag);
                                break;
                            case DDCMP_CTL_STRT: /* STRT */
                                fprintf (st, "(STRT) STRTSUB: %d, Flags: %s\n", msg2, flag);
                                break;
                            case DDCMP_CTL_STACK: /* STACK */
                                fprintf (st, "(STACK) STCKSUB: %d, Flags: %s\n", msg2, flag);
                                break;
                            default: /* Unknown */
                                fprintf (st, "(Unknown=0%o)\n", msg[1]);
                                break;
                            }
                        if (buffer->count != DDCMP_HEADER_SIZE)
                            fprintf (st, "Unexpected Control Message Length: %d expected %d\n", buffer->count, DDCMP_HEADER_SIZE);
                    }
                }
            same = 0;
            }
        else
            ++same;
        buffer = (BUFFER *)buffer->hdr.next;
        }
    if (same > 0)
        fprintf (st, "%s.queue[%d] thru %s.queue[%d] same as above\n", queue->name, (int)(i-same), queue->name, (int)(i-1));
    }
}

t_stat dmc_showqueues (FILE* st, UNIT* uptr, int32 val, void* desc)
{
CTLR *controller     = dmc_get_controller_from_unit(uptr);
static const char *states[] = {"Uninitialised", "Initialised", "Running", "Halted"};
static const char *tstates[] = {"Idle", "InputTransfer", "OutputTransfer", "OutputControl"};

dmc_showstats (st, uptr, val, desc);
fprintf (st, "State: %s\n", states[controller->state]);
fprintf (st, "TransferState: %s\n", tstates[controller->transfer_state]);
dmc_showqueue (st, controller->completion_queue, TRUE);
dmc_showqueue (st, controller->xmt_queue, TRUE);
dmc_showqueue (st, controller->ack_wait_queue, TRUE);
dmc_showqueue (st, controller->rcv_queue, TRUE);
dmc_showqueue (st, controller->free_queue, TRUE);
if (controller->control_out) {
    CONTROL_OUT *control;

    fprintf (st, "Control Out Queue:\n");
    for (control=controller->control_out; control; control=control->next)
        fprintf (st, "   SEL6:  0x%04X\n", control->sel6);
    }
return SCPE_OK;
}

t_stat dmc_setstats (UNIT* uptr, int32 val, char* cptr, void* desc)
{
CTLR *controller = dmc_get_controller_from_unit(uptr);

controller->receive_buffer_output_transfers_completed = 0;
controller->transmit_buffer_output_transfers_completed = 0;
controller->receive_buffer_input_transfers_completed = 0;
controller->transmit_buffer_input_transfers_completed = 0;
controller->control_out_operations_completed = 0;
controller->ddcmp_control_packets_received = 0;
controller->ddcmp_control_packets_sent = 0;

sim_printf("Statistics reset\n");

return SCPE_OK;
}

t_stat dmc_showconnectpoll (FILE* st, UNIT* uptr, int32 val, void* desc)
{
uint32 poll_interval = *((uint32 *)desc);

fprintf(st, "connectpoll=%u", poll_interval);
return SCPE_OK;
}

t_stat dmc_setconnectpoll (UNIT* uptr, int32 val, char* cptr, void* desc)
{
t_stat status = SCPE_OK;
uint32 *poll_interval = ((uint32 *)desc);
uint32 newpoll;

if (!cptr) 
    return SCPE_IERR;
newpoll = (int32) get_uint (cptr, 10, 1800, &status);
if ((status != SCPE_OK) || (newpoll == (*poll_interval)))
    return status;
*poll_interval = newpoll;
return tmxr_connection_poll_interval ((poll_interval == &dmc_connect_poll) ? &dmc_desc : &dmp_desc, newpoll);
}

/* SET LINES processor */

t_stat dmc_setnumdevices (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 newln;
uint32 i, j;
t_stat r;
DEVICE *dptr = (DEVICE *)desc;
TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
int maxunits = (&dmc_dev == dptr) ? DMC_NUMDEVICE : DMP_NUMDEVICE;
DIB *dibptr = (DIB *)dptr->ctxt;
int addrlnt = (UNIBUS) ? IOLN_DMC : IOLN_DMV;

for (j=0; j<2; j++) {
    dptr = (j == 0) ? &dmc_dev : &dmp_dev;
    for (i=0; i<dptr->numunits; i++)
        if (dptr->units[i].flags&UNIT_ATT)
            return SCPE_ALATT;
    }
dptr = (DEVICE *)desc;
for (i=0; i<dptr->numunits; i++)
    if (dptr->units[i].flags&UNIT_ATT)
        return SCPE_ALATT;
if (cptr == NULL)
    return SCPE_ARG;
newln = (int32) get_uint (cptr, 10, maxunits, &r);
if ((r != SCPE_OK) || (newln == (int32)(dptr->numunits - 2)))
    return r;
if (newln == 0)
    return SCPE_ARG;
sim_cancel (dptr->units + dptr->numunits - 2);
sim_cancel (dptr->units + dptr->numunits - 1);
for (i=dptr->numunits-2; i<(uint32)newln; ++i) {
    dptr->units[i] = dmc_unit_template;
    dptr->units[i].ctlr = &dmc_ctrls[(dptr == &dmc_dev) ? i : i+DMC_NUMDEVICE];
    }
dibptr->lnt = newln * addrlnt;                      /* set length */
dptr->numunits = newln + 2;
dptr->units[newln] = dmc_poll_unit_template;
dptr->units[newln].ctlr = &dmc_ctrls[(dptr == &dmc_dev) ? 0 : DMC_NUMDEVICE];
dptr->units[newln+1] = dmc_timer_unit_template;
dptr->units[newln+1].ctlr = &dmc_ctrls[(dptr == &dmc_dev) ? 0 : DMC_NUMDEVICE];
mp->lines = newln;
mp->uptr = dptr->units + newln;                     /* Identify polling unit */
return dmc_reset ((DEVICE *)desc);                  /* setup devices and auto config */
}

t_stat dmc_shownumdevices (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr = (UNIBUS) ? find_dev_from_unit (uptr) : &dmv_dev;

fprintf (st, "lines=%d", dptr->numunits-2);
return SCPE_OK;
}

t_stat dmc_showddcmp (FILE* st, UNIT* uptr, int32 val, void* desc)
{
CTLR *controller = dmc_get_controller_from_unit(uptr);
static const char *states[] = {"Halt", "IStart", "AStart", "Run", "Maintenance"};

fprintf (st, "%s%d\n", controller->device->name, (int)(uptr-controller->device->units));

fprintf(st, "DDCMP Link State: %s\n", states[controller->link.state]);
fprintf(st, "               R: %d\n", controller->link.R);
fprintf(st, "               N: %d\n", controller->link.N);
fprintf(st, "               A: %d\n", controller->link.A);
fprintf(st, "               T: %d\n", controller->link.T);
fprintf(st, "               X: %d\n", controller->link.X);
fprintf(st, "            SACK: %s\n", controller->link.SACK ? "true" : "false");
fprintf(st, "            SNAK: %s\n", controller->link.SNAK ? "true" : "false");
fprintf(st, "            SREP: %s\n", controller->link.SREP ? "true" : "false");
fprintf(st, "         rcv_pkt: %p\n", controller->link.rcv_pkt);
fprintf(st, "    rcv_pkt_size: %d\n", controller->link.rcv_pkt_size);
fprintf(st, "      xmt_buffer: %p\n", controller->link.xmt_buffer);
fprintf(st, "      nak_reason: %d\n", controller->link.nak_reason);
fprintf(st, "    TimerRunning: %s\n", controller->link.TimerRunning ? "true" : "false");
fprintf(st, "   TimeRemaining: %d\n", controller->link.TimeRemaining);
return SCPE_OK;
}

t_stat dmc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The %1s is a communication subsystem which consists of a microprocessor\n"
    " based, intelligent synchronous communications controller which employs\n"
    " the DIGITAL Data Communications Message Protocol (DDCMP).\n"
    "1 Hardware Description\n"
    " The %1s consists of a microprocessor module and a synchronous line unit\n"
    " module.\n"
#if !defined (VM_PDP10)
    "2 Models\n"
    " There were a number of microprocessor DDCMP devices produced.\n"
    "3 DMC11\n"
    " The original kmc11 microprocessor board with DMC microcode and a sync\n"
    " line unit.\n"
    "3 DMR11\n"
    " The more advanced kmc11 microprocessor board with DMR microcode and a sync\n"
    " line unit.\n"
    "3 DMP11\n"
    " A newly designed Unibus board with a more complete programming interface\n"
    " and a sync line unit.\n"
    "3 DMV11\n"
    " A Qbus version of the DMP11 with some more advanced refinements and a sync\n"
    " line unit.\n"
#endif
    "2 $Registers\n"
    "\n"
    " These registers contain the emulated state of the device.  These values\n"
    " don't necessarily relate to any detail of the original device being\n"
    " emulated but are merely internal details of the emulation.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n"
    "3 Lines\n"
    " A maximum of %2s %1s devices can be emulated concurrently in the %S\n"
    " simulator. The number of simulated %1s devices or lines can be\n"
    " specified with command:\n"
    "\n"
    "+sim> SET %D LINES=n\n"
    "3 Peer\n"
    " To set the host and port to which data is to be transmitted use the\n"
    " following command:\n"
    "\n"
    "+sim> SET %U PEER=host:port\n"
    "3 Connectpoll\n"
    " The minimum interval between attempts to connect to the other side is set\n"
    " using the following command:\n"
    "\n"
    "+sim> SET %U CONNECTPOLL=n\n"
    "\n"
    " Where n is the number of seconds. The default is %3s seconds.\n"
    "3 Speed\n"
    " If you want to experience the actual data rates of the physical hardware\n"
    " you can set the bit rate of the simulated line can be set using the\n"
    " following command:\n"
    "\n"
    "+sim> SET %U SPEED=n\n"
    "\n"
    " Where n is the number of data bits per second that the simulated line\n"
    " runs at.  In practice this is implemented as a delay while transmitting\n"
    " bytes to the socket.  Use a value of zero to run at full speed with no\n"
    " artificial throttling.\n"
#if !defined (VM_PDP10)
    "3 Type\n"
    " The type of device being emulated can be changed with the following\n"
    " command:\n"
    "\n"
    "+sim> SET %U TYPE={DMR,DMC}\n"
    "\n"
    " A SET TYPE command should be entered before the device is attached to a\n"
    " listening port.\n"
#endif
    "3 Corruption\n"
    " Corruption Troll - the DDCMP emulation includes a process that will\n"
    " intentionally drop or corrupt some messages.  This emulates the\n"
    " less-than-perfect communications lines encountered in the real world,\n"
    " and enables network monitoring software to see non-zero error counters.\n"
    "\n"
    " The troll selects messages with a probablility selected by the SET %U\n"
    " CORRUPT command.  The units are 0.1%%; that is, a value of 1 means that\n"
    " every message has a 1/1000 chance of being selected to be corrupted\n"
    " or discarded.\n"
     /****************************************************************************/
#define DMC_HLP_ATTACH "Configuration Attach"
    "2 Attach\n"
    " The device must be attached to a receive port, use the ATTACH command\n"
    " specifying the receive port number.\n"
    "\n"
    "+sim> ATTACH %U port\n"
    "\n"
    " The Peer host:port value must be specified before the attach command.\n"
    " The default connection uses TCP transport between the local system and\n"
    " the peer.  Alternatively, UDP can be used by specifying UDP on the\n"
    " ATTACH command:\n"
    "\n"
    "+sim> ATTACH %U port,UDP\n"
    "\n"
    "2 Examples\n"
    " To configure two simulators to talk to each other use the following\n"
    " example:\n"
    " \n"
    " Machine 1\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %U PEER=LOCALHOST:2222\n"
    "+sim> ATTACH %U 1111\n"
    " \n"
    " Machine 2\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %U PEER=LOCALHOST:1111\n"
    "+sim> ATTACH %U 2222\n"
    "\n"
    "1 Monitoring\n"
    " The %D device and %U line configuration and state can be displayed with\n"
    " one of the available show commands.\n"
    "2 $Show commands\n"
    "1 Diagnostics\n"
    " Corruption Troll - the DDCMP emulation includes a process that will\n"
    " intentionally drop or corrupt some messages.  This emulates the\n"
    " less-than-perfect communications lines encountered in the real world,\n"
    " and enables network monitoring software to see non-zero error counters.\n"
    "\n"
    " The troll selects messages with a probablility selected by the SET\n"
    " CORRUPT command.  The units are 0.1%%; that is, a value of 1 means that\n"
    " every message has a 1/1000 chance of being selected to be corrupted\n"
    " or discarded.\n"
    "1 Restrictions\n"
    " Real hardware synchronous connections could operate in Multi-Point mode.\n"
    " Multi-Point mode was a way of sharing a single wire with multiple\n"
    " destination systems or devices.  Multi-Point mode is not currently\n"
    " emulated by this or other simulated synchronous devices.\n"
    "\n"
    " In real hardware, the DMC11 spoke a version of DDCMP which peer devices\n"
    " needed to be aware of.  The DMR11, DMP11, and DMV11 boards have\n"
    " configuration switches or programatic methods to indicate that the peer\n"
    " device was a DMC11.  The emulated devices all speak the same level of\n"
    " DDCMP so no special remote device awareness need be considered.\n"
    "1 Implementation\n"
    " A real %1s transports data using DDCMP via a synchronous connection, the\n"
    " emulated device makes a TCP/IP connection to another emulated device which\n"
    " either speaks DDCMP over the TCP connection directly, or interfaces to a\n"
    " simulated computer where the operating system speaks the DDCMP protocol on\n"
    " the wire.\n"
    "\n"
    " The %1s can be used for point-to-point DDCMP connections carrying\n"
    " DECnet and other types of networking, e.g. from ULTRIX or DSM.\n"
    "1 Debugging\n"
    " The simulator has a number of debug options, these are:\n"
    "\n"
    "++REG     Shows whenever a CSR is programatically read or written\n"
    "++++and the current value.\n"
    "++INTREG  Shows internal register value changes.\n"
    "++INFO    Shows higher-level tracing only.\n"
    "++WARN    Shows any warnings.\n"
    "++TRACE   Shows more detailed trace information.\n"
    "++DATASUM Brief summary of each received and transmitted buffer.\n"
    "++++Ignored if DATA is set.\n"
    "++DATA    Shows the actual data sent and received.\n"
    "++MODEM   Shows modem signal transitions details.\n"
    "++CONNECT Shows sockets connection activity.\n"
    "++INT     Shows Interrupt activity.\n"
    "\n"
    " To get a full trace use\n"
    "\n"
    "+sim> SET %D DEBUG\n"
    "\n"
    " However it is recommended to use the following when sending traces:\n"
    "\n"
    "+sim> SET %D DEBUG=REG;INFO;WARN\n"
    "\n"
    "1 Related Devices\n"
    " The %D can facilitate communication with other simh simulators which\n"
    " have emulated synchronous network devices available.  These include\n"
    " the following:\n"
    "\n"
    "++DUP11*       Unibus PDP11 simulators\n"
    "++DPV11*       Qbus PDP11 simulators\n"
    "++KDP11*       Unibus PDP11 simulators and PDP10 simulators\n"
    "++DMR11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMC11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMP11        Unibus PDP11 simulators and Unibus VAX simulators\n"
    "++DMV11        Qbus VAX simulators\n"
    "\n"
    "++* Indicates systems which have OS provided DDCMP implementations.\n"
    ;
char devname[16];
char devcount[16];
char connectpoll[16];

sprintf (devname, "%s11" , dptr->name);
sprintf (devcount, "%d", (dptr == &dmc_dev) ? DMC_NUMDEVICE : DMP_NUMDEVICE);
sprintf (connectpoll, "%d", DMC_CONNECT_POLL);

return scp_help (st, dptr, uptr, flag, helpString, cptr, devname, devcount, connectpoll);
}

t_stat dmc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
return dmc_help (st, dptr, uptr, flag, DMC_HLP_ATTACH);
}

void dmc_setinint(CTLR *controller)
{
if (!dmc_is_iei_set(controller))
    return;
if (!controller->in_int) {
    sim_debug(DBG_INT, controller->device, "SET_INT(RX:%s%d) summary=0x%x\n", controller->device->name, controller->index, dmc_ini_summary);
    }
controller->in_int = 1;
dmc_ini_summary |= (1u << controller->index);
SET_INT(DMCRX);
}

void dmc_clrinint(CTLR *controller)
{
controller->in_int = 0;
if (dmc_ini_summary & (1u << controller->index)) {
    sim_debug(DBG_INT, controller->device, "CLR_INT(RX:%s%d) summary=0x%x\n", controller->device->name, controller->index, dmc_ini_summary);
    }
dmc_ini_summary &= ~(1u << controller->index);
if (!dmc_ini_summary)
    CLR_INT(DMCRX);
else
    SET_INT(DMCRX);
}

void dmc_setoutint(CTLR *controller)
{
if (!dmc_is_ieo_set(controller))
    return;
if (!controller->out_int) {
    sim_debug(DBG_INT, controller->device, "SET_INT(TX:%s%d) summary=0x%x\n", controller->device->name, controller->index, dmc_outi_summary );
    }
controller->out_int = 1;
dmc_outi_summary |= (1u << controller->index);
SET_INT(DMCTX);
}

void dmc_clroutint(CTLR *controller)
{
controller->out_int = 0;
if (dmc_outi_summary & (1u << controller->index)) {
    sim_debug(DBG_INT, controller->device, "CLR_INT(TX:%s%d) summary=0x%x\n", controller->device->name, controller->index, dmc_outi_summary);
    }
dmc_outi_summary &= ~(1u << controller->index);
if (!dmc_outi_summary)
    CLR_INT(DMCTX);
else
    SET_INT(DMCTX);
}

int dmc_getsel(int addr)
{
return (addr >> 1) & ((UNIBUS) ? 03 : 07);
}

uint16 dmc_bitfld(int data, int start_bit, int length)
{
uint16 ans = (uint16)(data >> start_bit);
uint32 mask = (1 << (length))-1;
ans &= mask;
return ans;
}

void dmc_dumpregsel0(CTLR *controller, int trace_level, char * prefix, uint16 data)
{
char *type_str = "";
uint16 type = dmc_bitfld(data, DMC_SEL0_V_ITYPE, DMC_SEL0_S_ITYPE);
static char *dmc_types[] = {"XMT BA/CC", "CNTL IN", "HALT", "BASE IN", "RCV BA/CC", "?????",  "?????", "?????"};

if (dmc_is_dmc(controller)) {
    if (dmc_is_rqi_set(controller)) {
        type_str = dmc_types[type];
        }

    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL0 (0x%04x) %s%s%s%s%s%s%s%s%s\n",
        controller->device->name, controller->index,
        prefix,
        data,
        dmc_bitfld(data, DMC_SEL0_V_RUN, 1) ? "RUN " : "",
        dmc_bitfld(data, DMC_SEL0_V_MCLEAR, 1) ? "MCLR " : "",
        dmc_bitfld(data, DMC_SEL0_V_LU_LOOP, 1) ? "LU LOOP " : "",
        dmc_bitfld(data, DMC_SEL0_V_ROMI, 1) ? "ROMI " : "",
        dmc_bitfld(data, DMC_SEL0_V_STEPUP, 1) ? "STEPUP " : "",
        dmc_bitfld(data, DMC_SEL0_V_RDI, 1) ? "RDI " : "",
        dmc_bitfld(data, DMC_SEL0_V_IEI, 1) ? "IEI " : "",
        dmc_bitfld(data, DMC_SEL0_V_RQI, 1) ? "RQI " : "",
        type_str
        );
    }
else {
    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL0 (0x%04x) %s%s%s%s%s%s\n",
        controller->device->name, controller->index,
        prefix,
        data,
        dmc_bitfld(data, DMC_SEL0_V_RUN, 1) ? "RUN " : "",
        dmc_bitfld(data, DMC_SEL0_V_MCLEAR, 1) ? "MCLR " : "",
        dmc_bitfld(data, DMC_SEL0_V_LU_LOOP, 1) ? "LU LOOP " : "",
        dmc_bitfld(data, DMP_SEL0_V_RQI, 1) ? "RQI " : "",
        dmc_bitfld(data, DMP_SEL0_V_IEO, 1) ? "IEO " : "",
        dmc_bitfld(data, DMP_SEL0_V_IEI, 1) ? "IEI " : ""
        );
    }
}

void dmc_dumpregsel2(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
char *type_str = "";
uint16 type = dmc_bitfld(data, DMC_SEL2_V_CODE, DMC_SEL2_S_CODE);
static char *dmc_types[] = {"XMT BA/CC OUT", "CNTL OUT", "?????", "?????", "RCV BA/CC OUT", "?????",  "?????", "?????"};

type_str = dmc_types[type];

sim_debug(
    trace_level,
    controller->device,
    "%s%d: %s SEL2 (0x%04x) PRIO=%d LINE=%d %s%s%s%s\n",
    controller->device->name, controller->index,
    prefix,
    data,
    dmc_bitfld(data, SEL2_PRIO_BIT, SEL2_PRIO_BIT_LENGTH),
    dmc_bitfld(data, SEL2_LINE_BIT, SEL2_LINE_BIT_LENGTH),
    dmc_bitfld(data, SEL2_RDO_BIT, 1) ? "RDO " : "",
    dmc_bitfld(data, SEL2_IEO_BIT, 1) ? "IEO " : "",
    dmc_bitfld(data, SEL2_OUT_IO_BIT, 1) ? "OUT I/O " : "",
    type_str
    );
}

void dmc_dumpregsel4(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
if (dmc_is_rdyi_set(controller)) {
    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL4 (0x%04x) %s%s%s%s%s%s%s%s\n",
        controller->device->name, controller->index,
        prefix,
        data,
        dmc_bitfld(data, DMC_SEL4_V_RI, 1) ? "RI " : "",
        dmc_bitfld(data, DMC_SEL4_V_DTR, 1) ? "DTR " : "",
        dmc_bitfld(data, DMC_SEL4_V_RTS, 1) ? "RTS " : "",
        dmc_bitfld(data, DMC_SEL4_V_HDX, 1) ? "HDX " : "",
        dmc_bitfld(data, DMC_SEL4_V_DSR, 1) ? "DSR " : "",
        dmc_bitfld(data, DMC_SEL4_V_CTS, 1) ? "CTS " : "",
        dmc_bitfld(data, DMC_SEL4_V_STN, 1) ? "STN " : "",
        dmc_bitfld(data, DMC_SEL4_V_CAR, 1) ? "CAR " : "");
    }
else {
    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL4 (0x%04x)\n",
        controller->device->name, controller->index,
        prefix,
        data);
    }
}

void dmc_dumpregsel6(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
if (dmc_is_rdyi_set(controller)) {
    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL6 (0x%04x) %s%s\n",
        controller->device->name, controller->index,
        prefix,
        data,
        dmc_bitfld(data, DMC_SEL6_M_LOSTDATA, 1) ? "LOST_DATA " : "",
        dmc_bitfld(data, DMC_SEL6_M_DISCONN, 1) ? "DISCONNECT " : "");
    }
else {
    sim_debug(
        trace_level,
        controller->device,
        "%s%d: %s SEL6 (0x%04x)\n",
        controller->device->name, controller->index,
        prefix,
        data);
    }
}

void dmc_dumpregsel10(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
sim_debug(
    trace_level,
    controller->device,
    "%s%d: %s SEL10 (0x%04x) %s\n",
    controller->device->name, controller->index,
    prefix,
    data,
    dmc_bitfld(data, DMC_SEL6_M_LOSTDATA, 1) ? "LOST_DATA " : "");
}

uint16 dmc_getreg(CTLR *controller, int reg, int ctx)
{
uint16 ans = 0;

switch (dmc_getsel(reg)) {
    case 00:
        ans = *controller->csrs->sel0;
        dmc_dumpregsel0(controller, ctx, "Getting", ans);
        break;
    case 01:
        ans = *controller->csrs->sel2;
        dmc_dumpregsel2(controller, ctx, "Getting", ans);
        break;
    case 02:
        ans = *controller->csrs->sel4;
        dmc_dumpregsel4(controller, ctx, "Getting", ans);
        break;
    case 03:
        ans = *controller->csrs->sel6;
        dmc_dumpregsel6(controller, ctx, "Getting", ans);
        break;
    case 04:
        ans = *controller->csrs->sel10;
        dmc_dumpregsel10(controller, ctx, "Getting", ans);
        break;
    default:
        sim_debug(DBG_WRN, controller->device, "%s%d: dmc_getreg(). Invalid register %d", controller->device->name, controller->index, reg);
        break;
    }

return ans;
}

void dmc_setreg(CTLR *controller, int reg, uint16 data, int ctx)
{
char *trace = "Setting";

switch (dmc_getsel(reg)) {
    case 00:
        dmc_dumpregsel0(controller, ctx, trace, data);
        *controller->csrs->sel0 = data;
        break;
    case 01:
        dmc_dumpregsel2(controller, ctx, trace, data);
        *controller->csrs->sel2 = data;
        break;
    case 02:
        dmc_dumpregsel4(controller, ctx, trace, data);
        *controller->csrs->sel4 = data;
        break;
    case 03:
        dmc_dumpregsel6(controller, ctx, trace, data);
        *controller->csrs->sel6 = data;
        break;
    case 04:
        dmc_dumpregsel10(controller, ctx, trace, data);
        *controller->csrs->sel10 = data;
        break;
    default:
        sim_debug(DBG_WRN, controller->device, "%s%d: dmc_setreg(). Invalid register %d", controller->device->name, controller->index, reg);
    }
}

int dmc_is_master_clear_set(CTLR *controller)
{
return *controller->csrs->sel0 & DMC_SEL0_M_MCLEAR;
}

int dmc_is_lu_loop_set(CTLR *controller)
{
if (dmc_is_dmc(controller))
    return ((*controller->csrs->sel0 & DMC_SEL0_M_LU_LOOP) != 0);
else
    return FALSE;
}

int dmc_is_run_set(CTLR *controller)
{
return *controller->csrs->sel0 & DMC_SEL0_M_RUN;
}

int dmc_is_rqi_set(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = *controller->csrs->sel0 & DMC_SEL0_M_RQI;
else
    ans = *controller->csrs->sel0 & DMP_SEL0_M_RQI;
return ans;
}

int dmc_is_rdyi_set(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = *controller->csrs->sel0 & DMC_SEL0_M_RDI;
else
    ans = *controller->csrs->sel2 & DMP_SEL2_M_RDI;
return ans;
}

int dmc_is_iei_set(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = *controller->csrs->sel0 & DMC_SEL0_M_IEI;
else
    ans = *controller->csrs->sel0 & DMP_SEL0_M_IEI;
return ans;
}

int dmc_is_ieo_set(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = *controller->csrs->sel2 & DMC_SEL2_M_IEO;
else
    ans = *controller->csrs->sel0 & DMP_SEL0_M_IEO;
return ans;
}

int dmc_is_in_io(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = ((*controller->csrs->sel0 & DMC_SEL0_M_ITYPE) == DMC_C_TYPE_RBACC);
else
    ans = ((*controller->csrs->sel2 & DMP_SEL2_M_CODE) == DMP_C_TYPE_RBACC);
return ans;
}

int dmc_is_rdyo_set(CTLR *controller)
{
return *controller->csrs->sel2 & DMC_SEL2_M_RDO;
}

void dmc_set_rdyi(CTLR *controller)
{
if (dmc_is_dmc(controller)) {
    dmc_setreg(controller, 0, *controller->csrs->sel0 | DMC_SEL0_M_RDI, DBG_RGC);
    dmc_setreg(controller, 4, *controller->modem | 0x0800, DBG_RGC);
    dmc_setreg(controller, 6, *controller->modem & DMC_SEL4_M_DTR, DBG_RGC);
    }
else
    dmc_setreg(controller, 2, *controller->csrs->sel2 | DMP_SEL2_M_RDI, DBG_RGC);
dmc_setinint(controller);
}

void dmc_clear_rdyi(CTLR *controller)
{
if (dmc_is_dmc(controller))
    dmc_setreg(controller, 0, *controller->csrs->sel0 & ~(DMC_SEL0_M_RDI|DMC_SEL0_M_ITYPE), DBG_RGC);
else
    dmc_setreg(controller, 2, *controller->csrs->sel2 & ~DMP_SEL2_M_RDI, DBG_RGC);
}

void dmc_set_rdyo(CTLR *controller)
{
dmc_setreg(controller, 2, *controller->csrs->sel2 | DMC_SEL2_M_RDO, DBG_RGC);

dmc_setoutint(controller);
}

uint32 dmc_get_addr(CTLR *controller)
{
if (dmc_is_dmc(controller) || (!(*controller->csrs->sel2 & DMP_SEL2_M_22BIT)))
    return ((uint32)dmc_getreg(controller, 4, DBG_RGC))  | ((((uint32)dmc_getreg(controller, 6, DBG_RGC) & 0xc000)) << 2);
else
    return ((uint32)dmc_getreg(controller, 4, DBG_RGC))  | (((uint32)dmc_getreg(controller, 6, DBG_RGC) & 0x3FF) << 16);
}

void dmc_set_addr(CTLR *controller, uint32 addr)
{
if (dmc_is_dmc(controller) || (!(*controller->csrs->sel2 & DMP_SEL2_M_22BIT))) {
    dmc_setreg(controller, 4, addr & 0xFFFF, DBG_RGC);
    dmc_setreg(controller, 6, (uint16)(((addr >> 16) << 14) | (*controller->csrs->sel6 & 0x3FFF)) , DBG_RGC);
    }
else {
    dmc_setreg(controller, 4, addr & 0xFFFF, DBG_RGC);
    dmc_setreg(controller, 6, ((addr >> 16) & 0x3F), DBG_RGC);
    }
}

uint16 dmc_get_count(CTLR *controller)
{
if (dmc_is_dmc(controller) || (!(*controller->csrs->sel2 & DMP_SEL2_M_22BIT)))
    return dmc_getreg(controller, 6, DBG_RGC) & 0x3FFF;
else
    return dmc_getreg(controller, 010, DBG_RGC) & 0x3FFF;
}

void dmc_set_count(CTLR *controller, uint16 count)
{
if (dmc_is_dmc(controller) || (!(*controller->csrs->sel2 & DMP_SEL2_M_22BIT)))
    dmc_setreg(controller, 6, (*controller->csrs->sel6 & 0xc000) | (0x3FFF & count), DBG_RGC);
else
    dmc_setreg(controller, 010, (*controller->csrs->sel10 & 0xc000) | (0x3FFF & count), DBG_RGC);
}

uint8 dmc_get_modem(CTLR *controller)
{
int32 modem_bits;

tmxr_set_get_modem_bits (controller->line, 0, 0, &modem_bits);
*controller->modem &= ~(DMC_SEL4_M_CTS|DMC_SEL4_M_CAR|DMC_SEL4_M_RI|DMC_SEL4_M_DSR|DMC_SEL4_M_DTR|DMC_SEL4_M_RTS);
*controller->modem |= (modem_bits&TMXR_MDM_DCD) ? DMC_SEL4_M_CAR : 0;
*controller->modem |= (modem_bits&TMXR_MDM_CTS) ? DMC_SEL4_M_CTS : 0;
*controller->modem |= (modem_bits&TMXR_MDM_DSR) ? DMC_SEL4_M_DSR : 0;
*controller->modem |= (modem_bits&TMXR_MDM_RNG) ? DMC_SEL4_M_RI : 0;
*controller->modem |= (modem_bits&TMXR_MDM_DTR) ? DMC_SEL4_M_DTR : 0;
*controller->modem |= (modem_bits&TMXR_MDM_RTS) ? DMC_SEL4_M_RTS : 0;
return (*controller->modem);
}

void dmc_set_modem_dtr(CTLR *controller)
{
if (dmc_is_attached(controller->unit) && (!(DMC_SEL4_M_DTR & *controller->modem))) {
    sim_debug(DBG_MDM, controller->device, "%s%d: DTR State Change to UP(ON)\n", controller->device->name, controller->index);
    tmxr_set_get_modem_bits (controller->line, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
    dmc_get_modem(controller);
    controller->line->rcve = 1;
    }
}

void dmc_clr_modem_dtr(CTLR *controller)
{
if (*controller->modem & DMC_SEL4_M_DTR) {
    sim_debug(DBG_MDM, controller->device, "%s%d: DTR State Change to DOWN(OFF)\n", controller->device->name, controller->index);
    }
tmxr_set_get_modem_bits (controller->line, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);
dmc_get_modem(controller);
}

void dmc_set_lost_data(CTLR *controller)
{
dmc_setreg(controller, 6, *controller->csrs->sel6 | DMC_SEL6_M_LOSTDATA, DBG_RGC);
}

void dmc_clear_master_clear(CTLR *controller)
{
dmc_setreg(controller, 0, *controller->csrs->sel0 & ~DMC_SEL0_M_MCLEAR, DBG_RGC);
}

void dmc_set_run(CTLR *controller)
{
dmc_setreg(controller, 0, *controller->csrs->sel0 | DMC_SEL0_M_RUN, DBG_RGC);
}

int dmc_get_input_transfer_type(CTLR *controller)
{
int ans = 0;

if (dmc_is_dmc(controller))
    ans = *controller->csrs->sel0 & DMC_SEL0_M_ITYPE;
else
    ans = *controller->csrs->sel2 & DMP_SEL2_M_CODE;
return ans;
}

void dmc_set_type_output(CTLR *controller, int type)
{
dmc_setreg(controller, 2, (*controller->csrs->sel2 & ~DMC_SEL2_M_CODE) | (type & DMC_SEL2_M_CODE), DBG_RGC);
}

void dmc_process_master_clear(CTLR *controller)
{
CONTROL_OUT *control;

sim_debug(DBG_INF, controller->device, "%s%d: Master clear\n", controller->device->name, controller->index);
dmc_clear_master_clear(controller);
dmc_clr_modem_dtr(controller);
controller->link.state = Halt;
controller->state = Initialised;
while ((control = controller->control_out)) {
    controller->control_out = control->next;
    free (control);
    }
controller->control_out = NULL;
dmc_setreg(controller, 0, 0, DBG_RGC);
if (controller->dev_type == DMR) {
    if (dmc_is_attached(controller->unit)) {
        /* Indicates microdiagnostics complete */
        if (((*controller->csrs->sel0 & DMC_SEL0_M_UDIAG) != 0) ^
            (dmc_microdiag[controller->index]))
            dmc_setreg(controller, 2, 0x8000, DBG_RGC);/* Microdiagnostics Complete */
        else
            dmc_setreg(controller, 2, 0x4000, DBG_RGC); /* Microdiagnostics Inhibited */
        }
    else {
        /* Indicate M8203 (Line Unit) test failed */
        dmc_setreg(controller, 2, 0x0200, DBG_RGC);
        }
    }
else {
    /* preserve contents of BSEL3 if DMC-11 */
    dmc_setreg(controller, 2, *controller->csrs->sel2 & 0xFF00, DBG_RGC);
    }
if (controller->dev_type == DMP)
    dmc_setreg(controller, 4, 077, DBG_RGC);
else
    dmc_setreg(controller, 4, 0, DBG_RGC);

if (controller->dev_type == DMP)
    dmc_setreg(controller, 6, 0305, DBG_RGC);
else
    dmc_setreg(controller, 6, 0, DBG_RGC);
dmc_buffer_queue_init_all(controller);

controller->transfer_state = Idle;
if (dmc_is_attached(controller->unit))
    dmc_set_run(controller);
}

void dmc_start_input_transfer(CTLR *controller)
{
sim_debug(DBG_INF, controller->device, "%s%d: Starting input transfer\n", controller->device->name, controller->index);
controller->transfer_state = InputTransfer;
dmc_set_rdyi(controller);
}

void dmc_start_control_output_transfer(CTLR *controller)
{
if ((!controller->control_out) || 
    (controller->transfer_state != Idle) || 
    (dmc_is_rdyo_set(controller)))
    return;
sim_debug(DBG_INF, controller->device, "%s%d: Starting control output transfer: SEL6 = 0x%04X\n", controller->device->name, controller->index, controller->control_out->sel6);
controller->transfer_state = OutputControl;
dmc_setreg (controller, 6, controller->control_out->sel6, DBG_RGC);
dmc_set_type_output(controller, DMC_C_TYPE_CNTL);
dmc_set_rdyo(controller);
}

void dmc_complete_transmit(CTLR *controller)
{
BUFFER *buffer = controller->link.xmt_buffer;
if (!buffer)
    return;
controller->link.xmt_done_buffer = buffer;
ddcmp_dispatch (controller, DDCMP_EVENT_XMIT_DONE);
controller->link.xmt_done_buffer = NULL;
buffer = (BUFFER *)remqueue (&buffer->hdr);
buffer->actual_bytes_transferred = buffer->count;
controller->buffers_transmitted_to_net++;
if (buffer->type == TransmitData) {
    buffer->actual_bytes_transferred = buffer->count - (DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE);
    ASSURE (insqueue (&buffer->hdr, controller->ack_wait_queue->hdr.prev));
    }
else {
    ASSURE (insqueue (&buffer->hdr, &controller->free_queue->hdr));
    }
controller->link.xmt_buffer = NULL;
}

t_stat dmc_svc(UNIT* uptr)
{
CTLR *controller = dmc_get_controller_from_unit(uptr);
DEVICE *dptr = controller->device;

sim_debug(DBG_TRC, dptr, "dmc_svc(%s%d)\n", controller->device->name, controller->index);

/* Perform delayed register actions */
if (controller->dmc_wr_delay) {
    controller->dmc_wr_delay = 0;
    dmc_process_command(controller);
    if (controller->link.xmt_buffer) {
        sim_activate_notbefore (uptr, controller->link.xmt_buffer->buffer_return_time);
        return SCPE_OK;
        }
    }
if (dmc_is_attached(controller->unit)) {
    /* Speed limited transmissions are completed here after the appropriate service delay */
    if (controller->link.xmt_buffer) {
        dmc_complete_transmit(controller);
        dmc_ddcmp_start_transmitter (controller);
        }

    dmc_start_control_output_transfer(controller);

    if (ddcmp_UserSendMessage (controller))
        ddcmp_dispatch (controller, 0);
    if (controller->transfer_state == Idle)
        dmc_start_transfer_buffer(controller);

    dmc_buffer_fill_receive_buffers(controller);
    if (controller->transfer_state == Idle)
        dmc_start_transfer_buffer(controller);
    /* Execution of this routine normally is triggered by programatic access by the
       simulated system's device driver to the device registers or the arrival of 
       traffic from the network.  Network traffic is handled by the dmc_poll_svc 
       which may call this routine as needed.  Direct polling here is extra overhead 
       and is only scheduled when needed.
     */
    if ((controller->completion_queue->count) ||    /* if completion queue not empty? */
        (controller->control_out)             ||    /*    or pending control outs? */
        (controller->transfer_state != Idle))       /*    or registers are busy */
        sim_activate (uptr, tmxr_poll);             /* wake up periodically until these don't exist */
    }

return SCPE_OK;
}

t_stat dmc_poll_svc (UNIT *uptr)
{
DEVICE *dptr = ((CTLR *)uptr->ctlr)->device;
CTLR *controller;
TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
int32 dmc, active, attached;

sim_debug(DBG_TRC, dptr, "dmc_poll_svc()\n");

dmc = tmxr_poll_conn(mp);
if (dmc >= 0) {                                 /* new connection? */
    controller = (CTLR *)dptr->units[dmc].ctlr;
    dmc_get_modem (controller);
    sim_debug(DBG_MDM, dptr, "dmc_poll_svc(dmc=%d) - Connection State Change to UP(ON)\n", dmc);
    ddcmp_dispatch (controller, 0);
    sim_activate (controller->unit, tmxr_poll);     /* be sure to wake up soon to continue processing */
    }
tmxr_poll_rx (mp);
tmxr_poll_tx (mp);
for (dmc=active=attached=0; dmc < mp->lines; dmc++) {
    CTLR *controller = (CTLR *)dptr->units[dmc].ctlr;
    TMLN *lp = controller->line;
    uint8 old_modem, new_modem;

    controller = (CTLR *)dptr->units[dmc].ctlr;
    if (controller->unit->flags & UNIT_ATT)
        ++attached;
    if (lp->conn)
        ++active;
    old_modem = *controller->modem;
    new_modem = dmc_get_modem (controller);
    if ((old_modem & DMC_SEL4_M_CAR) && 
        (!(new_modem & DMC_SEL4_M_CAR))) {
        sim_debug(DBG_MDM, controller->device, "dmc_poll_svc(dmc=%d) - Connection State Change to %s\n", dmc, (new_modem & DMC_SEL4_M_CAR) ? "UP(ON)" : "DOWN(OFF)");
        ddcmp_dispatch (controller, 0);
        sim_activate (controller->unit, tmxr_poll);     /* wake up soon to finish processing */
        }
    if ((lp->xmte && tmxr_tpbusyln(lp)) || 
        (lp->xmte && controller->link.xmt_buffer) ||
        ddcmp_UserSendMessage(controller)) {
        if (tmxr_tpbusyln(lp)) {
            sim_debug(DBG_DAT, controller->device, "dmc_poll_svc(dmc=%d) - Packet Transmission of remaining %d bytes restarting...\n", dmc, tmxr_tpqln (lp));
            }
        dmc_svc (&controller->device->units[dmc]);  /* Flush pending output */
        }
    if (dmc_buffer_fill_receive_buffers(controller))
        dmc_svc (&controller->device->units[dmc]);  /* return ACKed transmit packets */
    }
if (active)
    sim_clock_coschedule (uptr, tmxr_poll);         /* reactivate */
else {
    for (dmc=0; dmc < mp->lines; dmc++) {
        uint32 *speeds = (dptr == &dmc_dev) ? dmc_speed : dmp_speed;
        CTLR *controller = (CTLR *)dptr->units[dmc].ctlr;

        if (speeds[dmc]/8)
            controller->byte_wait = (tmr_poll*clk_tps)/(speeds[dmc]/8);
        else
            controller->byte_wait = 0;
        }
    if (attached)
        sim_activate_after (uptr, DMC_CONNECT_POLL*1000000);/* periodic check for connections */
    }
return SCPE_OK;
}

t_stat dmc_timer_svc (UNIT *uptr)
{
int32 dmc, active;
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
CTLR *controller;

sim_debug(DBG_TRC, dptr, "dmc_timer_svc()\n");

for (dmc=active=0; dmc < mp->lines; dmc++) {
    controller = (CTLR *)dptr->units[dmc].ctlr;
    if (controller->link.TimerRunning) {
        if (controller->link.TimeRemaining > 0) {
            --controller->link.TimeRemaining;
            if (controller->link.TimeRemaining > 0)
                ++active;
            else
                ddcmp_dispatch (controller, DDCMP_EVENT_TIMER);  /* Timer Expiring Event */
            }
        }
    }
if (active)
    sim_activate_after (uptr, 1000000);         /* Per Second Event trigger */
return SCPE_OK;
}

void dmc_buffer_queue_init(CTLR *controller, BUFFER_QUEUE *q, char *name, size_t size, BUFFER *buffers)
{
q->name = name;
initqueue (&q->hdr, q, size, buffers, sizeof(*buffers));
q->controller = controller;
}

t_bool dmc_transmit_queue_empty(CTLR *controller)
{
return (controller->xmt_queue->count == 0);
}

void dmc_buffer_queue_init_all(CTLR *controller)
{
size_t queue_size = (controller->dev_type == DMC) ? DMC_QUEUE_SIZE : 
                                                    ((controller->dev_type == DMR) ? DMR_QUEUE_SIZE : 
                                                                                        DMP_QUEUE_SIZE);
queue_size *= 2;
*controller->buffers = (BUFFER *)realloc(*controller->buffers, queue_size*sizeof(**controller->buffers));
memset (*controller->buffers, 0, queue_size*sizeof(**controller->buffers));
dmc_buffer_queue_init(controller, controller->rcv_queue,        "receive",    0, NULL);
dmc_buffer_queue_init(controller, controller->completion_queue, "completion", 0, NULL);
dmc_buffer_queue_init(controller, controller->xmt_queue,        "transmit",   0, NULL);
dmc_buffer_queue_init(controller, controller->ack_wait_queue,   "ack wait",   0, NULL);
dmc_buffer_queue_init(controller, controller->free_queue,       "free",       queue_size, *controller->buffers);
}

BUFFER *dmc_buffer_allocate(CTLR *controller)
{
BUFFER *buffer = (BUFFER *)remqueue (controller->free_queue->hdr.next);

if (!buffer) {
    fprintf (stdout, "DDCMP Buffer allocation failure.\n");
    fprintf (stdout, "This is a fatal error which should never happen.\n");
    fprintf (stdout, "Please submit this output when you report this bug.\n");
    dmc_showqueues (stdout, controller->unit, 0, NULL);
    dmc_showstats (stdout, controller->unit, 0, NULL);
    dmc_showddcmp (stdout, controller->unit, 0, NULL);
    if (sim_log) {
        fprintf (sim_log, "DDCMP Buffer allocation failure.\n");
        fprintf (sim_log, "This is a fatal error which should never happen.\n");
        fprintf (sim_log, "Please submit this output when you report this bug.\n");
        dmc_showqueues (sim_log, controller->unit, 0, NULL);
        dmc_showstats (sim_log, controller->unit, 0, NULL);
        dmc_showddcmp (sim_log, controller->unit, 0, NULL);
        fflush (sim_log);
        }
    if (sim_deb) {
        fprintf (sim_deb, "DDCMP Buffer allocation failure.\n");
        fprintf (sim_deb, "This is a fatal error which should never happen.\n");
        fprintf (sim_deb, "Please submit this output when you report this bug.\n");
        dmc_showqueues (sim_deb, controller->unit, 0, NULL);
        dmc_showstats (sim_deb, controller->unit, 0, NULL);
        dmc_showddcmp (sim_deb, controller->unit, 0, NULL);
        fflush (sim_deb);
        }
    return buffer;
    }
buffer->address = 0;
buffer->count = 0;
free (buffer->transfer_buffer);
buffer->transfer_buffer = NULL;
buffer->actual_bytes_transferred = 0;
buffer->type = TransmitControl;         /* Default */
buffer->buffer_return_time = 0;
return buffer;
}

BUFFER *dmc_buffer_queue_add(BUFFER_QUEUE *q, uint32 address, uint16 count, BufferType type)
{
BUFFER *buffer = dmc_buffer_allocate(q->controller);

if (buffer) {
    buffer->type = type;
    buffer->address = address;
    buffer->count = count;
    ASSURE (insqueue (&buffer->hdr, q->hdr.prev)); /* Insert at tail */
    sim_debug(DBG_INF, q->controller->device, "%s%d: Queued %s buffer address=0x%08x count=%d\n", q->controller->device->name, q->controller->index, q->name, address, count);
    }
else {
    sim_debug(DBG_WRN, q->controller->device, "%s%d: Failed to queue %s buffer address=0x%08x, queue full\n", q->controller->device->name, q->controller->index, q->name, address);
    // TODO: Report error here.
    }
return buffer;
}


BUFFER *dmc_buffer_queue_head(BUFFER_QUEUE *q)
{
return ((q->hdr.next == &q->hdr) ? NULL : (BUFFER *)q->hdr.next);
}

void dmc_queue_control_out(CTLR *controller, uint16 sel6)
{
CONTROL_OUT *control = calloc(1, sizeof(*control));
CONTROL_OUT *last = NULL;

control->sel6 = sel6;
if (controller->control_out) {
    last = controller->control_out;
    while (last->next)
        last = last->next;
    last->next = control;
    }
else
    controller->control_out = control;
}

/* returns true if some data was received */
t_bool dmc_buffer_fill_receive_buffers(CTLR *controller)
{
int ans = FALSE;

if (controller->state == Running) {
    BUFFER *buffer = dmc_buffer_queue_head(controller->rcv_queue);

    while (buffer) {
        ddcmp_tmxr_get_packet_ln (controller->line, (const uint8 **)&controller->link.rcv_pkt, &controller->link.rcv_pkt_size, *controller->corruption_factor);
        if (!controller->link.rcv_pkt)
            break;
        ans = TRUE;
        controller->buffers_received_from_net++;
        controller->ddcmp_control_packets_received += (controller->link.rcv_pkt[0] == DDCMP_ENQ) ? 1 : 0;
        ddcmp_dispatch(controller, DDCMP_EVENT_PKT_RCVD);
        buffer = dmc_buffer_queue_head(controller->rcv_queue);
        }
    }
return ans;
}

void dmc_start_transfer_buffer(CTLR *controller)
{
BUFFER *head = dmc_buffer_queue_head(controller->completion_queue);
uint16 count;

if ((!head) ||
    (controller->transfer_state != Idle) ||
    (dmc_is_rdyo_set(controller)))
    return;
count = (uint16)head->actual_bytes_transferred;
switch (head->type) {
    case Receive:
        sim_debug(DBG_INF, controller->device, "%s%d: Starting data output transfer for receive, address=0x%08x, count=%d\n", controller->device->name, controller->index, head->address, count);
        dmc_set_type_output(controller, DMC_C_TYPE_RBACC);
        break;
    case TransmitData:
        sim_debug(DBG_INF, controller->device, "%s%d: Starting data output transfer for transmit, address=0x%08x, count=%d\n", controller->device->name, controller->index, head->address, count);
        dmc_set_type_output(controller, DMC_C_TYPE_XBACC);
        break;
    default:
        break;
    }
dmc_set_addr(controller, head->address);
dmc_set_count(controller, count);
controller->transfer_state = OutputTransfer;
dmc_set_rdyo(controller);
}

void dmc_check_for_output_transfer_completion(CTLR *controller)
{
BUFFER *buffer;

if ((dmc_is_rdyo_set(controller)) ||
    (controller->transfer_state != OutputTransfer))
    return;
sim_debug(DBG_INF, controller->device, "%s%d: Output transfer completed\n", controller->device->name, controller->index);
buffer = (BUFFER *)remqueue (controller->completion_queue->hdr.next);
ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
controller->transmit_buffer_output_transfers_completed++;
controller->transfer_state = Idle;
dmc_process_command(controller); // check for any other transfers
}

void dmc_check_for_output_control_completion(CTLR *controller)
{
CONTROL_OUT *control = controller->control_out;

if ((dmc_is_rdyo_set(controller)) ||
    (controller->transfer_state != OutputControl))
    return;

dmc_dumpregsel6(controller, DBG_INF, "Output command completed:", control->sel6);

controller->transfer_state = Idle;
controller->control_out = control->next;
free(control);
controller->control_out_operations_completed++;
dmc_process_command(controller); // check for any other transfers
}

void dmc_process_input_transfer_completion(CTLR *controller)
{
if (dmc_is_dmc(controller)) {
    if (dmc_is_run_set(controller) && 
        (!dmc_is_rqi_set(controller))) {
        /* Time to decode input command arguments */
        uint16 sel6 = *controller->csrs->sel6;
        uint32 addr = dmc_get_addr(controller);
        uint16 count = dmc_get_count(controller);

        controller->transfer_type = dmc_get_input_transfer_type(controller);
        dmc_clear_rdyi(controller);
        switch (controller->transfer_type) {
            case DMC_C_TYPE_BASEIN:
                *controller->baseaddr = dmc_get_addr(controller);
                *controller->basesize = dmc_get_count(controller);
                sim_debug(DBG_INF, controller->device, "%s%d: Completing Base In input transfer, base address=0x%08x count=%d\n", controller->device->name, controller->index, *controller->baseaddr, *controller->basesize);
                break;
            case DMC_C_TYPE_XBACC:
                if (1) {
                    BUFFER *buffer = dmc_buffer_queue_add(controller->xmt_queue, addr, count, TransmitData);
                    int n;

                    /* construct buffer - leaving room for DDCMP header and CRCs */
                    buffer->transfer_buffer = (uint8 *)realloc (buffer->transfer_buffer, buffer->count + DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE);
                    memset (buffer->transfer_buffer, 0, DDCMP_HEADER_SIZE);
                    n = Map_ReadB (buffer->address, buffer->count, buffer->transfer_buffer + DDCMP_HEADER_SIZE);
                    if (n > 0) {
                        sim_debug(DBG_WRN, controller->device, "%s%d: DMA error\n", controller->device->name, controller->index);
                        }
                    buffer->count += DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE;
                    controller->transmit_buffer_input_transfers_completed++;
                    }
                if (controller->state != Running) {
                    controller->state = Running;
                    dmc_set_modem_dtr (controller);
                    }
                ddcmp_dispatch (controller, 0);
                break;
            case DMC_C_TYPE_RBACC:
                dmc_buffer_queue_add(controller->rcv_queue, addr, count, Receive);
                dmc_buffer_fill_receive_buffers(controller);
                controller->receive_buffer_input_transfers_completed++;
                ddcmp_dispatch (controller, 0);
                break;
            case DMC_C_TYPE_CNTL:
                sim_debug(DBG_INF, controller->device, "%s%d: Control In Command Received(%s MODE%s%s)\n", 
                                                       controller->device->name, controller->index,
                                                       (sel6&DMC_SEL6_M_MAINT) ? "MAINTENANCE" : "DDCMP", 
                                                       (sel6&DMC_SEL6_M_HDX) ? ", Half Duples" : "", 
                                                       (sel6&DMC_SEL6_M_LONGSTRT) ? ", Long Start Timer" : "");
                dmc_set_modem_dtr (controller);
                controller->transfer_state = Idle;
                ddcmp_dispatch (controller, (sel6&DMC_SEL6_M_MAINT) ? DDCMP_EVENT_MAINTMODE : 0);
                return;
            case DMC_C_TYPE_HALT:
                sim_debug(DBG_INF, controller->device, "%s%d: Halt Command Received\n", controller->device->name, controller->index);
                controller->state = Halted;
                ddcmp_dispatch(controller, 0);
                dmc_clr_modem_dtr(controller);
                dmc_queue_control_out(controller, DMC_SEL6_M_HALTCOMP);
                return;
            }
        controller->transfer_state = Idle;
        dmc_process_command (controller);
        }
    }
else {  /* DMP */
    if (!dmc_is_rdyi_set(controller)) {
        uint16 sel6 = *controller->csrs->sel6;

        if (controller->transfer_type == DMP_C_TYPE_MODE) {
            uint16 mode = sel6 & DMP_TYPE_INPUT_MASK;
            char * duplex = (mode & 1) ? "Full-Duplex" : "Half-Duplex";
            char * config;

            if (mode & 4)
                config = "Point-to-point";
            else
                config = (mode & 2) ? "Tributary station" : "Control Station";

            sim_debug(DBG_INF, controller->device, "%s%d: Completing Mode input transfer, %s %s\n", controller->device->name, controller->index, duplex, config);
            }
        else 
            if (controller->transfer_type == DMP_C_TYPE_CNTL) {
                sim_debug(DBG_WRN, controller->device, "%s%d: Control command (not processed yet)\n", controller->device->name, controller->index);
                }
            else 
                if (controller->transfer_type == DMP_C_TYPE_RBACC) {
                    sim_debug(DBG_WRN, controller->device, "%s%d: Receive Buffer command (not processed yet)\n", controller->device->name, controller->index);
                    }
                else 
                    if (controller->transfer_type == DMP_C_TYPE_XBACC) {
                        sim_debug(DBG_WRN, controller->device, "%s%d: Transmit Buffer command (not processed yet)\n", controller->device->name, controller->index);
                        }
                    else {
                        sim_debug(DBG_WRN, controller->device, "%s%d: Unrecognised command code %hu\n", controller->device->name, controller->index, controller->transfer_type);
                        }

        controller->transfer_state = Idle;
        }
    }
}

typedef enum {
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE
} CompareOP;

/* DDCMP Modulo 256 compare - from Rob Jarratt */
static int Mod256Cmp(uint8 a, uint8 b, size_t queue_size)
{
int ans;
int abdiff = (int)b - (int)a;
int badiff = (int)a - (int)b;
 
if (abdiff == 0)
    ans = 0;
else 
    if (abdiff < 0)
        ans = -1;
    else
        ans = 1;
 
if (abs(badiff) <= (int)queue_size)
    ans = -1 * ans;
 
return ans;
}

t_bool ddcmp_compare (uint8 a, CompareOP Op, uint8 b, CTLR *controller)
{
int cmp = Mod256Cmp(a & 0xFF, b & 0xFF, controller->free_queue->size);

switch (Op) {
    case EQ:
        return (cmp == 0);
    case NE:
        return (cmp != 0);
    case LE:
        if (cmp == 0)
            return TRUE;
    case LT:
        return (cmp < 0);
    case GE:
        if (cmp == 0)
            return TRUE;
    case GT:
        return (cmp > 0);
    default:    /* Never happens */
        return FALSE;
    }
}

void ddcmp_StartTimer             (CTLR *controller)
{
controller->link.TimerRunning = TRUE;
controller->link.TimeRemaining = DDCMP_PACKET_TIMEOUT;
sim_activate_after (controller->device->units+(controller->device->numunits-1), controller->link.TimeRemaining*1000000);
}
void ddcmp_StopTimer              (CTLR *controller)
{
controller->link.TimerRunning = FALSE;
}
void ddcmp_ResetVariables         (CTLR *controller)
{
size_t data_packets = 0;
BUFFER *buffer;

controller->link.R = 0;
controller->link.N = 0;
controller->link.A = 0;
controller->link.T = 1;
controller->link.X = 0;
controller->link.SACK = FALSE;
controller->link.SNAK = FALSE;
controller->link.SREP = FALSE;
controller->link.state = Halt;
controller->link.nak_reason = 0;
/* Move any ack wait packets back to the transmit queue so they get 
   resent when the link is restored */
while (controller->ack_wait_queue->count) {
    buffer = (BUFFER *)remqueue (controller->ack_wait_queue->hdr.prev);
    memset (buffer->transfer_buffer, 0, DDCMP_HEADER_SIZE);
    ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
    }
/* Also make sure that the transmit queue has no control packets in it 
   and that any non transmit buffer(s) have zeroed headers so they will
   be properly constructed when the link comes up */
buffer = (BUFFER *)controller->xmt_queue->hdr.next;
while (controller->xmt_queue->count - data_packets) {
    if (((BUFFER *)controller->xmt_queue->hdr.next)->transfer_buffer[0] == DDCMP_ENQ) {
        BUFFER *buffer_next = (BUFFER *)buffer->hdr.next;

        buffer = (BUFFER *)remqueue (&buffer->hdr);
        ASSURE (insqueue (&buffer->hdr, &controller->free_queue->hdr));
        buffer = buffer_next;
        continue;
        }
    ++data_packets;
    memset (buffer->transfer_buffer, 0, DDCMP_HEADER_SIZE);
    buffer = (BUFFER *)buffer->hdr.next;
    }
}
void ddcmp_SendStrt               (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_allocate(controller);

buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
buffer->count = DDCMP_HEADER_SIZE;
ddcmp_build_start_packet (buffer->transfer_buffer);
ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SendStack              (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_allocate(controller);

buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
buffer->count = DDCMP_HEADER_SIZE;
ddcmp_build_start_ack_packet (buffer->transfer_buffer);
ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SendAck                (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_allocate(controller);

buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
buffer->count = DDCMP_HEADER_SIZE;
ddcmp_build_ack_packet (buffer->transfer_buffer, controller->link.R, DDCMP_FLAG_SELECT);
ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SendNak                (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_allocate(controller);

buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
buffer->count = DDCMP_HEADER_SIZE;
ddcmp_build_nak_packet (buffer->transfer_buffer, controller->link.nak_reason, controller->link.R, DDCMP_FLAG_SELECT);
ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SendRep                (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_allocate(controller);

buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
buffer->count = DDCMP_HEADER_SIZE;
ddcmp_build_rep_packet (buffer->transfer_buffer, controller->link.N, DDCMP_FLAG_SELECT);
ASSURE (insqueue (&buffer->hdr, &controller->xmt_queue->hdr));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SetSACK                (CTLR *controller)
{
controller->link.SACK = TRUE;
}
void ddcmp_ClearSACK              (CTLR *controller)
{
controller->link.SACK = FALSE;
}
void ddcmp_SetSNAK                (CTLR *controller)
{
controller->link.SNAK = TRUE;
}
void ddcmp_ClearSNAK              (CTLR *controller)
{
controller->link.SNAK = FALSE;
}
void ddcmp_SetSREP                (CTLR *controller)
{
controller->link.SREP = TRUE;
}
void ddcmp_ClearSREP              (CTLR *controller)
{
controller->link.SREP = FALSE;
}
void ddcmp_IncrementR             (CTLR *controller)
{
controller->link.R++;
}
void ddcmp_SetAeqRESP             (CTLR *controller)
{
controller->link.A = controller->link.rcv_pkt[DDCMP_RESP_OFFSET];
}
void ddcmp_SetTequalAplus1        (CTLR *controller)
{
controller->link.T = controller->link.A + 1;
}
void ddcmp_IncrementT             (CTLR *controller)
{
controller->link.T++;
}
void ddcmp_SetNAKreason3          (CTLR *controller)
{
controller->link.nak_reason = 3;
}
void ddcmp_SetNAKreason2          (CTLR *controller)
{
controller->link.nak_reason = 2;
}
void ddcmp_NAKMissingPackets      (CTLR *controller)
{
uint8 R = controller->link.R;
QH *qh = &controller->xmt_queue->hdr;

while (ddcmp_compare (controller->link.rcv_pkt[DDCMP_NUM_OFFSET], GE, R, controller)) {
    BUFFER *buffer = dmc_buffer_allocate(controller);
    buffer->transfer_buffer = (uint8 *)malloc (DDCMP_HEADER_SIZE);
    buffer->count = DDCMP_HEADER_SIZE;
    ddcmp_build_nak_packet (buffer->transfer_buffer, 2, R, DDCMP_FLAG_SELECT);
    R = R + 1;
    ASSURE (insqueue (&buffer->hdr, qh));
    qh = &buffer->hdr;
    }
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_IfTleAthenSetTeqAplus1 (CTLR *controller)
{
if (ddcmp_compare (controller->link.T, LE, controller->link.A, controller))
    controller->link.T = controller->link.A + 1;
}
void ddcmp_IfAltXthenStartTimer   (CTLR *controller)
{
if (ddcmp_compare (controller->link.A, LT, controller->link.X, controller))
    ddcmp_StartTimer (controller);
}
void ddcmp_IfAgeXthenStopTimer    (CTLR *controller)
{
if (ddcmp_compare (controller->link.A, GE, controller->link.X, controller))
    ddcmp_StopTimer (controller);
}
void ddcmp_Ignore                 (CTLR *controller)
{
}
void ddcmp_GiveBufferToUser       (CTLR *controller)
{
BUFFER *buffer = (BUFFER *)remqueue (controller->rcv_queue->hdr.next);

if (!buffer) {
    ddcmp_SetSNAK (controller);
    controller->link.nak_reason = 8;    /* buffer temporarily unavailable */
    return;
    }

buffer->actual_bytes_transferred = controller->link.rcv_pkt_size - (DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE);
Map_WriteB(buffer->address, buffer->actual_bytes_transferred, controller->link.rcv_pkt + DDCMP_HEADER_SIZE);
ASSURE (insqueue (&buffer->hdr, controller->completion_queue->hdr.prev));     /* Insert at tail */
ddcmp_IncrementR (controller);
ddcmp_SetSACK (controller);
}
void ddcmp_CompleteAckedTransmits (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->ack_wait_queue);

while (buffer != NULL) {
    if ((!buffer->transfer_buffer) || 
        (!controller->link.rcv_pkt) ||
        ddcmp_compare (buffer->transfer_buffer[DDCMP_NUM_OFFSET], GT, controller->link.rcv_pkt[DDCMP_RESP_OFFSET], controller))
        break;
    buffer = (BUFFER *)remqueue (&buffer->hdr);
    ASSURE (insqueue (&buffer->hdr, controller->completion_queue->hdr.prev)); /* Insert at tail */
    buffer = dmc_buffer_queue_head(controller->ack_wait_queue);
    }
}
void ddcmp_ReTransmitMessageT (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->ack_wait_queue);
size_t i;

for (i=0; i < controller->ack_wait_queue->count; ++i) {
    if ((!buffer->transfer_buffer) || 
        ddcmp_compare (buffer->transfer_buffer[DDCMP_NUM_OFFSET], NE, controller->link.T, controller)) {
        buffer = (BUFFER *)buffer->hdr.next;
        continue;
        }
    ddcmp_build_data_packet (buffer->transfer_buffer, buffer->count - (DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE), DDCMP_FLAG_SELECT|DDCMP_FLAG_QSYNC, controller->link.T, controller->link.R);
    buffer = (BUFFER *)remqueue (&buffer->hdr);
    ASSURE (insqueue (&buffer->hdr, controller->xmt_queue->hdr.prev)); /* Insert at tail */
    break;
    }
}
void ddcmp_NotifyDisconnect       (CTLR *controller)
{
dmc_queue_control_out(controller, DMC_SEL6_M_DISCONN);
}
void ddcmp_NotifyStartRcvd        (CTLR *controller)
{
dmc_queue_control_out(controller, DMC_SEL6_M_STRTRCVD);
}
void ddcmp_NotifyMaintRcvd        (CTLR *controller)
{
dmc_queue_control_out(controller, DMC_SEL6_M_MAINTRCV);
}
void ddcmp_SendDataMessage        (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->xmt_queue);

ddcmp_build_data_packet (buffer->transfer_buffer, buffer->count - (DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE), DDCMP_FLAG_SELECT|DDCMP_FLAG_QSYNC, controller->link.N + 1, controller->link.R);
controller->link.N += 1;
controller->link.T = controller->link.N + 1;
controller->link.SACK = 0;
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SendMaintMessage       (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->xmt_queue);

ddcmp_build_maintenance_packet (buffer->transfer_buffer, buffer->count - (DDCMP_HEADER_SIZE + DDCMP_CRC_SIZE));
dmc_ddcmp_start_transmitter (controller);
}
void ddcmp_SetXSetNUM             (CTLR *controller)
{
controller->link.X = controller->link.xmt_done_buffer->transfer_buffer[DDCMP_NUM_OFFSET];
}

/* Conditions/Events */

t_bool ddcmp_UserHalt             (CTLR *controller)
{
return (controller->state == Halted);
}
t_bool ddcmp_UserStartup          (CTLR *controller)
{
return (*controller->modem & DMC_SEL4_M_DTR);
}
t_bool ddcmp_UserMaintenanceMode  (CTLR *controller)
{
return (controller->link.ScanningEvents & DDCMP_EVENT_MAINTMODE);
}
t_bool ddcmp_ReceiveStack         (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_ENQ) &&
        (controller->link.rcv_pkt[1] == DDCMP_CTL_STACK));
}
t_bool ddcmp_ReceiveStrt          (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_ENQ) &&
        (controller->link.rcv_pkt[1] == DDCMP_CTL_STRT));
}
t_bool ddcmp_TimerRunning         (CTLR *controller)
{
return (controller->link.TimerRunning);
}
t_bool ddcmp_TimerNotRunning      (CTLR *controller)
{
return (!controller->link.TimerRunning);
}
t_bool ddcmp_TimerExpired         (CTLR *controller)
{
return (controller->link.ScanningEvents & DDCMP_EVENT_TIMER);
}
t_bool ddcmp_ReceiveMaintMessage  (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) && 
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_DLE));
}
t_bool ddcmp_ReceiveAck           (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_ENQ) &&
        (controller->link.rcv_pkt[1] == DDCMP_CTL_ACK));
}
t_bool ddcmp_ReceiveNak           (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_ENQ) &&
        (controller->link.rcv_pkt[1] == DDCMP_CTL_NAK));
}
t_bool ddcmp_ReceiveRep           (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
        controller->link.rcv_pkt &&
        (controller->link.rcv_pkt[0] == DDCMP_ENQ) &&
        (controller->link.rcv_pkt[1] == DDCMP_CTL_REP));
}
t_bool ddcmp_NUMEqRplus1          (CTLR *controller)
{
t_bool breturn = (controller->link.rcv_pkt &&
                  ddcmp_compare (controller->link.rcv_pkt[DDCMP_NUM_OFFSET], EQ, controller->link.R + 1, controller));
return breturn;
}
t_bool ddcmp_NUMGtRplus1          (CTLR *controller)
{
t_bool breturn = (controller->link.rcv_pkt &&
                  ddcmp_compare (controller->link.rcv_pkt[DDCMP_NUM_OFFSET], GT, controller->link.R + 1, controller));
return breturn;
}
t_bool ddcmp_ReceiveDataMsg       (CTLR *controller)
{
t_bool breturn = ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
                  controller->link.rcv_pkt &&
                  (controller->link.rcv_pkt[0] == DDCMP_SOH));
if (breturn)
    return breturn;
return breturn;
}
t_bool ddcmp_ReceiveMaintMsg      (CTLR *controller)
{
t_bool breturn = ((controller->link.ScanningEvents & DDCMP_EVENT_PKT_RCVD) &&
                  controller->link.rcv_pkt &&
                  (controller->link.rcv_pkt[0] == DDCMP_DLE));
if (breturn)
    return breturn;
return breturn;
}
t_bool ddcmp_ALtRESPleN           (CTLR *controller)
{
return (controller->link.rcv_pkt &&
        ddcmp_compare (controller->link.A, LT, controller->link.rcv_pkt[DDCMP_RESP_OFFSET], controller) &&
        ddcmp_compare (controller->link.rcv_pkt[DDCMP_RESP_OFFSET], LE, controller->link.N, controller));
}
t_bool ddcmp_ALeRESPleN           (CTLR *controller)
{
return (controller->link.rcv_pkt &&
        ddcmp_compare (controller->link.A, LE, controller->link.rcv_pkt[DDCMP_RESP_OFFSET], controller) &&
        ddcmp_compare (controller->link.rcv_pkt[DDCMP_RESP_OFFSET], LE, controller->link.N, controller));
}
t_bool ddcmp_RESPleAOrRESPgtN     (CTLR *controller)
{
return (controller->link.rcv_pkt &&
        (ddcmp_compare (controller->link.rcv_pkt[DDCMP_RESP_OFFSET], LE, controller->link.A, controller) ||
         ddcmp_compare (controller->link.rcv_pkt[DDCMP_RESP_OFFSET], GT, controller->link.N, controller)));
}
t_bool ddcmp_TltNplus1            (CTLR *controller)
{
return (ddcmp_compare (controller->link.T, LT, controller->link.N + 1, controller));
}
t_bool ddcmp_TeqNplus1            (CTLR *controller)
{
return (ddcmp_compare (controller->link.T, EQ, controller->link.N + 1, controller));
}
t_bool ddcmp_ReceiveMessageError  (CTLR *controller)
{
if (controller->link.rcv_pkt &&
    ((0 != ddcmp_crc16 (0, controller->link.rcv_pkt, 8)) ||
     ((controller->link.rcv_pkt[0] != DDCMP_ENQ) &&
      (0 != ddcmp_crc16 (0, controller->link.rcv_pkt+8, controller->link.rcv_pkt_size-8)))))
      return TRUE;
return FALSE;
}
t_bool ddcmp_NumEqR               (CTLR *controller)
{
return (controller->link.rcv_pkt &&
        ddcmp_compare (controller->link.rcv_pkt[DDCMP_NUM_OFFSET], EQ, controller->link.R, controller));
}
t_bool ddcmp_NumNeR               (CTLR *controller)
{
return (controller->link.rcv_pkt &&
        ddcmp_compare (controller->link.rcv_pkt[DDCMP_NUM_OFFSET], NE, controller->link.R, controller));
}
t_bool ddcmp_TransmitterIdle      (CTLR *controller)
{
return (NULL == controller->link.xmt_buffer);
}
t_bool ddcmp_TramsmitterBusy      (CTLR *controller)
{
return !ddcmp_TransmitterIdle(controller);
}
t_bool ddcmp_SACKisSet            (CTLR *controller)
{
return (controller->link.SACK);
}
t_bool ddcmp_SACKisClear          (CTLR *controller)
{
return (!(controller->link.SACK));
}
t_bool ddcmp_SNAKisSet            (CTLR *controller)
{
return (controller->link.SNAK);
}
t_bool ddcmp_SNAKisClear          (CTLR *controller)
{
return (!(controller->link.SNAK));
}
t_bool ddcmp_SREPisSet            (CTLR *controller)
{
return (controller->link.SREP);
}
t_bool ddcmp_SREPisClear          (CTLR *controller)
{
return (!(controller->link.SREP));
}
t_bool ddcmp_UserSendMessage      (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->xmt_queue);

return (buffer && (buffer->transfer_buffer[0] == 0));
}
t_bool ddcmp_LineConnected        (CTLR *controller)
{
return (*controller->modem & DMC_SEL4_M_CAR);
}
t_bool ddcmp_LineDisconnected     (CTLR *controller)
{
return (!(*controller->modem & DMC_SEL4_M_CAR));
}
t_bool ddcmp_DataMessageSent      (CTLR *controller)
{
t_bool breturn = ((controller->link.ScanningEvents & DDCMP_EVENT_XMIT_DONE) &&
                    controller->link.xmt_done_buffer && 
                    (controller->link.xmt_done_buffer->transfer_buffer[0] == DDCMP_SOH));
if (breturn)
    return breturn;
return breturn;
}
t_bool ddcmp_REPMessageSent       (CTLR *controller)
{
return ((controller->link.ScanningEvents & DDCMP_EVENT_XMIT_DONE) &&
        controller->link.xmt_done_buffer && 
        (controller->link.xmt_done_buffer->transfer_buffer[0] == DDCMP_ENQ) && 
        (controller->link.xmt_done_buffer->transfer_buffer[1] == DDCMP_CTL_REP));
}

void ddcmp_dispatch(CTLR *controller, uint32 EventMask)
{
DDCMP_STATETABLE *table;
static const char *states[] = {"Halt", "IStart", "AStart", "Run", "Maintenance"};

if (controller->link.Scanning) {
    if (!controller->link.RecurseScan) {
        controller->link.RecurseScan = TRUE;
        controller->link.RecurseEventMask |= EventMask;
        }
    return;
    }
controller->link.Scanning = TRUE;
controller->link.ScanningEvents |= EventMask;
for (table=DDCMP_TABLE; table->Conditions[0] != NULL; ++table) {
    if ((table->State == controller->link.state) ||
        (table->State == All)) {
        t_bool match = TRUE;
        DDCMP_Condition_Routine *cond = table->Conditions;
        DDCMP_LinkAction_Routine *action = table->Actions;

        while (match && *cond != NULL) {
            match = (*cond)(controller);
            ++cond;
            }
        if (!match)
            continue;
        sim_debug (DBG_INF, controller->device, "%s%d: ddcmp_dispatch(%X) - %s conditions matching for rule %d(%s), initiating actions (%s)\n", controller->device->name, controller->index, EventMask, states[table->State], table->RuleNumber, ddcmp_conditions(table->Conditions), ddcmp_actions(table->Actions));
        while (*action != NULL) {
            (*action)(controller);
            ++action;
            }
        if (table->NewState != controller->link.state) {
            sim_debug (DBG_INF, controller->device, "%s%d: ddcmp_dispatch(%X) - state changing from %s to %s\n", controller->device->name, controller->index, EventMask, states[controller->link.state], states[table->NewState]);
            controller->link.state = table->NewState;
            }
        }
    }
controller->link.Scanning = FALSE;
controller->link.ScanningEvents &= ~EventMask;
if (controller->link.RecurseScan) {
    controller->link.RecurseScan = FALSE;
    EventMask = controller->link.RecurseEventMask;
    controller->link.RecurseEventMask = 0;
    ddcmp_dispatch (controller, EventMask);
    dmc_svc (controller->unit);
    }
}


void dmc_ddcmp_start_transmitter (CTLR *controller)
{
BUFFER *buffer = dmc_buffer_queue_head(controller->xmt_queue);
t_stat r;

if ((controller->link.xmt_buffer) ||        /* if Already Transmitting */
    (!buffer) ||                            /*    or nothing pending */
    (!controller->line->conn))              /*    or not connected */
    return;                                 /* Do nothing */
while (buffer) {
    if (buffer->transfer_buffer[0] == 0)
        return;
    /* Need to make sure we dynamically compute the packet CRCs since header details can change */
    r = ddcmp_tmxr_put_packet_crc_ln (controller->line, buffer->transfer_buffer, buffer->count, *controller->corruption_factor);
    if (r == SCPE_OK) {
        controller->link.xmt_buffer = buffer;
        controller->ddcmp_control_packets_sent += (buffer->transfer_buffer[0] == DDCMP_ENQ) ? 1 : 0;
        if (controller->byte_wait) {  /* Speed limited? */
            buffer->buffer_return_time = buffer->count*controller->byte_wait + sim_grtime();
            sim_activate (controller->unit, buffer->count*controller->byte_wait);
            break;
            }
        dmc_complete_transmit (controller);
        buffer = dmc_buffer_queue_head(controller->xmt_queue);
        continue;
        }
    break;
    }
}

void dmc_process_command(CTLR *controller)
{
if (dmc_is_master_clear_set(controller)) {
    dmc_process_master_clear(controller);
    return;
    }
if (controller->transfer_state == InputTransfer) {
    dmc_process_input_transfer_completion(controller);
    return;
    }
if (controller->transfer_state == OutputTransfer) {
    dmc_check_for_output_transfer_completion(controller);
    return;
    }
if (controller->transfer_state == OutputControl) {
    dmc_check_for_output_control_completion(controller);
    return;
    }
/* transfer_state Idle */
if (dmc_is_rqi_set(controller)) {
    dmc_start_input_transfer(controller);
    return;
    }
if (dmc_is_dmc (controller) &&
    (*controller->csrs->sel0 & DMC_SEL0_M_ROMI) &&
    (!dmc_is_run_set(controller)) &&
    (*controller->csrs->sel0 & DMC_SEL0_M_STEPUP)) {
    /* DMC-11 or DMR-11, with RUN off and step and ROMI bits set.  */
    switch (*controller->csrs->sel6) {
        case DSPDSR:    /* 0x22b3 (read line status instruction), set the DTR bit in SEL2.  */
            dmc_setreg (controller, 2, 0x800, DBG_RGC);
            break;
        case DROPDTR:   /* 0xa40b (drop DTR instruction) - VMS Driver uses this  */
            dmc_clr_modem_dtr (controller);
            break;
        case UINST_CNF: /* 0x2296 (get configuration switches) - VMS Driver uses this  */
            dmc_setreg (controller, 6, 0x0006, DBG_RGC);
            break;
        case UINST_RROM:/* 0x814d (read DMC ROM) - VMS Driver uses this  */
            dmc_setreg (controller, 6, 0x0391, DBG_RGC); /* Not Low Speed uCode value (0x390) */
            break;
        default:
            sim_debug(DBG_WRN, controller->device, "%s%d: dmc_process_command(). Unknown Microcode instruction 0x%04x", controller->device->name, controller->index, *controller->csrs->sel6);
            break;
        }
    *controller->csrs->sel0 &= ~DMC_SEL0_M_STEPUP;
    controller->transfer_state = Idle;
    }
else {
    if (dmc_is_run_set(controller)) {
        dmc_start_control_output_transfer(controller);
        dmc_start_transfer_buffer(controller);
        }
    }
if (tmxr_get_line_loopback (controller->line) ^ dmc_is_lu_loop_set (controller)) {
    sim_debug(DBG_INF, controller->device, "%s%d: %s loopback mode\n", controller->device->name, controller->index, dmc_is_lu_loop_set (controller) ? "Enabling" : "Disabling");
    tmxr_set_line_loopback (controller->line, dmc_is_lu_loop_set (controller));
    }
}

t_stat dmc_rd(int32 *data, int32 PA, int32 access)
{
CTLR *controller = dmc_get_controller_from_address(PA);
int reg = PA & ((UNIBUS) ? 07 : 017);

*data = dmc_getreg(controller, PA, DBG_REG);
if (access == READ) {
    sim_debug(DBG_REG, controller->device, "dmc_rd(%s%d), addr=0x%08x, SEL%d, data=0x%04x\n", controller->device->name, controller->index, PA, reg, *data);
    }
else {
    sim_debug(DBG_REG, controller->device, "dmc_rd(%s%d), addr=0x%08x, BSEL%d, data=%02x\n", controller->device->name, controller->index, PA, reg, *data & 0xFF);
    }

return SCPE_OK;
}

t_stat dmc_wr(int32 data, int32 PA, int32 access)
{
CTLR *controller = dmc_get_controller_from_address(PA);
int reg = PA & ((UNIBUS) ? 07 : 017);
uint16 oldValue = dmc_getreg(controller, PA, 0);

if (access == WRITE) {
    sim_debug(DBG_REG, controller->device, "dmc_wr(%s%d), addr=0x%08x, SEL%d, newdata=0x%04x, olddata=0x%04x\n", controller->device->name, controller->index, PA, reg, data, oldValue);
    }
else {
    sim_debug(DBG_REG, controller->device, "dmc_wr(%s%d), addr=0x%08x, BSEL%d, newdata=%02x, olddata=0x%02x\n", controller->device->name, controller->index, PA, reg, data & 0xFF, ((PA&1) ? oldValue>>8 : oldValue) & 0xFF);
    }

if (access == WRITE) {
    if (PA & 1) {
        sim_debug(DBG_WRN, controller->device, "dmc_wr(%s%d), Unexpected non-16-bit write access to SEL%d\n", controller->device->name, controller->index, reg);
        }
    dmc_setreg(controller, PA, (uint16)data, DBG_REG);
    }
else {
    uint16 mask;
    if (PA & 1) {
        mask = 0xFF00;
        data = data << 8;
        }
    else {
        mask = 0x00FF;
        }

    dmc_setreg(controller, PA, (oldValue & ~mask) | (data & mask), DBG_REG);
    }

if ((dmc_getsel(reg) == 0) || (dmc_getsel(reg) == 1)) {/* writes to SEL0 and SEL2 are actionable */
    if (0 == controller->dmc_wr_delay) {    /* Not waiting? */
        controller->dmc_wr_delay = 10;      /* Wait a bit before acting on the changed register */
        sim_activate_abs (controller->unit, controller->dmc_wr_delay);
        }
    }

return SCPE_OK;
}

int32 dmc_ininta (void)
{
int i;
int32 ans = 0; /* no interrupt request active */

for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++) {
    CTLR *controller = &dmc_ctrls[i];
    if (controller->in_int != 0) {
        DIB *dib = (DIB *)controller->device->ctxt;
        ans = dib->vec + (8 * (int)(controller->unit - controller->device->units));
        dmc_clrinint(controller);
        sim_debug(DBG_INT, controller->device, "RXINTA Device %d - Vector: 0x%x(0%3o)\n", (int)(controller->unit-controller->device->units), ans, ans);
        break;
        }
    }

return ans;
}

int32 dmc_outinta (void)
{
int i;
int32 ans = 0; /* no interrupt request active */

for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++) {
    CTLR *controller = &dmc_ctrls[i];
    if (controller->out_int != 0) {
        DIB *dib = (DIB *)controller->device->ctxt;
        ans = dib->vec + 4 + (8 * (int)(controller->unit - controller->device->units));
        dmc_clroutint(controller);
        sim_debug(DBG_INT, controller->device, "TXINTA Device %d - Vector: 0x%x(0%3o)\n", (int)(controller->unit-controller->device->units), ans, ans);
        break;
        }
    }

return ans;
}

t_stat dmc_reset (DEVICE *dptr)
{
t_stat ans = SCPE_OK;
CTLR *controller;
uint32 i, j;

sim_debug(DBG_TRC, dptr, "dmc_reset(%s)\n", dptr->name);

dmc_desc.packet = TRUE;
dmc_desc.buffered = 16384;
dmp_desc.packet = TRUE;
dmp_desc.buffered = 16384;
/* Connect structures together */
for (i=0; i < DMC_NUMDEVICE; i++) {
    dmc_csrs[i].sel0 = &dmc_sel0[i];
    dmc_csrs[i].sel2 = &dmc_sel2[i];
    dmc_csrs[i].sel4 = &dmc_sel4[i];
    dmc_csrs[i].sel6 = &dmc_sel6[i];
    controller = &dmc_ctrls[i];
    controller->csrs = &dmc_csrs[i];
    controller->line = &dmc_desc.ldsc[i];
    controller->rcv_queue = &dmc_rcv_queues[i];
    controller->completion_queue = &dmc_completion_queues[i];
    controller->xmt_queue = &dmc_xmt_queues[i];
    controller->ack_wait_queue = &dmc_ack_wait_queues[i];
    controller->free_queue = &dmc_free_queues[i];
    controller->buffers = &dmc_buffers[i];
    controller->device = &dmc_dev;
    controller->baseaddr = &dmc_baseaddr[i];
    controller->basesize = &dmc_basesize[i];
    controller->modem = &dmc_modem[i];
    controller->corruption_factor = &dmc_corruption[i];
    controller->unit = &controller->device->units[i];
    controller->unit->ctlr = (void *)controller;
    controller->index = i;
    }
tmxr_connection_poll_interval (&dmc_desc, dmc_connect_poll);
for (i=0; i < DMP_NUMDEVICE; i++) {
    dmp_csrs[i].sel0 = &dmp_sel0[i];
    dmp_csrs[i].sel2 = &dmp_sel2[i];
    dmp_csrs[i].sel4 = &dmp_sel4[i];
    dmp_csrs[i].sel6 = &dmp_sel6[i];
    dmp_csrs[i].sel10 = &dmp_sel10[i];
    controller = &dmc_ctrls[i+DMC_NUMDEVICE];
    controller->csrs = &dmp_csrs[i];
    controller->line = &dmp_desc.ldsc[i];
    controller->rcv_queue = &dmp_rcv_queues[i];
    controller->completion_queue = &dmp_completion_queues[i];
    controller->xmt_queue = &dmp_xmt_queues[i];
    controller->ack_wait_queue = &dmp_ack_wait_queues[i];
    controller->free_queue = &dmp_free_queues[i];
    controller->buffers = &dmp_buffers[i];
    controller->device = (UNIBUS) ? &dmp_dev : &dmv_dev;
    controller->dev_type = DMP;
    controller->baseaddr = &dmp_baseaddr[i];
    controller->basesize = &dmp_basesize[i];
    controller->modem = &dmp_modem[i];
    controller->corruption_factor = &dmp_corruption[i];
    controller->unit = &controller->device->units[i];
    controller->unit->ctlr = (void *)controller;
    controller->index = i + DMC_NUMDEVICE;
    }
tmxr_connection_poll_interval (&dmp_desc, dmp_connect_poll);
if (0 == dmc_units[0].flags) {       /* First Time Initializations */
    for (i=0; i < DMC_NUMDEVICE; i++) {
        controller = &dmc_ctrls[i];
        controller->state = Uninitialised;
        controller->transfer_state = Idle;
        controller->control_out = NULL;
        *controller->modem = 0;
#if defined (VM_PDP10)
        controller->dev_type = DMR;
#else
        controller->dev_type = DMC;
#endif
        dmc_dev.units[i] = dmc_unit_template;
        controller->unit->ctlr = (void *)controller;
        dmc_microdiag[i] = TRUE;
        dmc_corruption[i] = 0;
        }
    tmxr_set_modem_control_passthru (&dmc_desc);   /* We always want Modem Control */
    dmc_units[dmc_dev.numunits-2] = dmc_poll_unit_template;
    dmc_units[dmc_dev.numunits-2].ctlr = dmc_units[0].ctlr;
    dmc_units[dmc_dev.numunits-1] = dmc_timer_unit_template;
    dmc_units[dmc_dev.numunits-1].ctlr = dmc_units[0].ctlr;
    dmc_desc.notelnet = TRUE;                      /* We always want raw tcp socket */
    dmc_desc.dptr = &dmc_dev;                      /* Connect appropriate device */
    dmc_desc.uptr = dmc_units+dmc_desc.lines;      /* Identify polling unit */
    for (i=0; i < DMP_NUMDEVICE; i++) {
        controller = &dmc_ctrls[i+DMC_NUMDEVICE];
        controller->state = Uninitialised;
        controller->transfer_state = Idle;
        controller->control_out = NULL;
        *controller->modem = 0;
        dmp_dev.units[i] = dmc_unit_template;
        controller->unit->ctlr = (void *)controller;
        dmp_corruption[i] = 0;
        }
    tmxr_set_modem_control_passthru (&dmp_desc);   /* We always want Modem Control */
    dmp_units[dmp_dev.numunits-2] = dmc_poll_unit_template;
    dmp_units[dmp_dev.numunits-2].ctlr = dmp_units[0].ctlr;
    dmp_units[dmp_dev.numunits-1] = dmc_timer_unit_template;
    dmp_units[dmp_dev.numunits-1].ctlr = dmp_units[0].ctlr;
    dmp_desc.notelnet = TRUE;                      /* We always want raw tcp socket */
    dmp_desc.dptr = &dmp_dev;                      /* Connect appropriate device */
    dmp_desc.uptr = dmp_units+dmp_desc.lines;      /* Identify polling unit */
    }

ans = auto_config (dptr->name, (dptr->flags & DEV_DIS) ? 0 : dptr->numunits - 2);

if (!(dptr->flags & DEV_DIS)) {
    int32 attached = 0;

    for (i = 0; i < DMC_NUMDEVICE + DMP_NUMDEVICE; i++) {
        if (dmc_ctrls[i].device == dptr) {
            BUFFER *buffer;

            controller = &dmc_ctrls[i];
            /* Avoid memory leaks by moving all buffers back to the free queue
               and then freeing any allocated transfer buffers for each buffer
               on the free queue */
            while (controller->ack_wait_queue->count) {
                buffer = (BUFFER *)remqueue (controller->ack_wait_queue->hdr.next);
                ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
                }
            while (controller->completion_queue->count) {
                buffer = (BUFFER *)remqueue (controller->completion_queue->hdr.next);
                ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
                }
            while (controller->rcv_queue->count) {
                buffer = (BUFFER *)remqueue (controller->rcv_queue->hdr.next);
                ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
                }
            while (controller->xmt_queue->count) {
                buffer = (BUFFER *)remqueue (controller->xmt_queue->hdr.next);
                ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
                }
            for (j = 0; j < controller->free_queue->size; j++) {
                buffer = (BUFFER *)remqueue (controller->free_queue->hdr.next);
                free (buffer->transfer_buffer);
                buffer->transfer_buffer = NULL;
                ASSURE (insqueue (&buffer->hdr, controller->free_queue->hdr.prev));
                }
            dmc_buffer_queue_init_all(controller);
            dmc_clrinint(controller);
            dmc_clroutint(controller);
            for (j=0; j<dptr->numunits-1; j++) {
                sim_cancel (&dptr->units[j]); /* stop poll */
                if (dptr->units[j].flags & UNIT_ATT)
                    ++attached;
                }
            dmc_process_master_clear(controller);
            }
        }
    if (attached)
        sim_activate_after (dptr->units+(dptr->numunits-2), DMC_CONNECT_POLL*1000000);/* start poll */
    }

return ans;
}

t_stat dmc_attach (UNIT *uptr, char *cptr)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
t_stat ans = SCPE_OK;
char *peer = ((dptr == &dmc_dev)? &dmc_peer[dmc][0] : &dmp_peer[dmc][0]);
char *port = ((dptr == &dmc_dev)? &dmc_port[dmc][0] : &dmp_port[dmc][0]);
char attach_string[1024];

if (!cptr || !*cptr)
    return SCPE_ARG;
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
if (!peer[0]) {
    sim_printf ("Peer must be specified before attach\n");
    return SCPE_ARG;
    }
sprintf (attach_string, "Line=%d,Connect=%s,%s", dmc, peer, cptr);
ans = tmxr_open_master (mp, attach_string);                 /* open master socket */
if (ans != SCPE_OK)
    return ans;
strncpy (port, cptr, CBUFSIZE-1);
uptr->filename = (char *)malloc (strlen(port)+1);
strcpy (uptr->filename, port);
uptr->flags |= UNIT_ATT;
sim_activate_after (dptr->units+(dptr->numunits-2), DMC_CONNECT_POLL*1000000);/* start poll */
return ans;
}

t_stat dmc_detach (UNIT *uptr)
{
DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
int32 dmc = (int32)(uptr-dptr->units);
TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
TMLN *lp = &mp->ldsc[dmc];
int32 i, attached;
t_stat r;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
sim_cancel (uptr);
uptr->flags &= ~UNIT_ATT;
for (i=attached=0; i<mp->lines; i++)
    if (dptr->units[i].flags & UNIT_ATT)
        ++attached;
if (!attached) {
    sim_cancel (dptr->units+mp->lines);              /* stop poll on last detach */
    sim_cancel (dptr->units+(mp->lines+1));          /* stop timer on last detach */
    }
r = tmxr_detach_ln (lp);
free (uptr->filename);
uptr->filename = NULL;
return r;
}

char *dmc_description (DEVICE *dptr)
{
#if defined (VM_PDP10)
return "DMR11 Synchronous network controller";
#else
return "DMC11 Synchronous network controller";
#endif
}

char *dmp_description (DEVICE *dptr)
{
return (UNIBUS) ? "DMP11 Synchronous network controller"
                : "DMV11 Synchronous network controller";
}

