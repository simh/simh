/* pdp11_dmc.c: DMC11 Emulation
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

  ------------------------------------------------------------------------------


I/O is done through sockets so that the remote system can be on the same host machine. The device starts polling for
incoming connections when it receives its first read buffer. The device opens the connection for writing when it receives
the first write buffer.

Transmit and receive buffers are added to their respective queues and the polling method in dmc_svc() checks for input
and sends any output.

On the wire the format is a 2-byte block length followed by that number of bytes. Some of the diagnostics expect to receive
the same number of bytes in a buffer as were sent by the other end. Using sockets without a block length can cause the
buffers to coalesce and then the buffer lengths in the diagnostics fail. The block length is always sent in network byte
order.

Tested with two diagnostics. To run the diagnostics set the default directory to SYS$MAINTENANCE, run ESSAA and then
configure it for the DMC-11 with the following commands:

The above commands can be put into a COM file in SYS$MAINTENANCE (works on VMS 3.0 but not 4.6, not sure why).

ATT DW780 SBI DW0 3 4
ATT DMC11 DW0 XMA0 760070 300 5
SELECT XMA0
(if putting these into a COM file to be executed by ESSAA add a "DS> " prefix)


The first is EVDCA which takes no parameters. Invoke it with the command R EVDCA. This diagnostic uses the DMC-11 loopback
functionality and the transmit port is not used when LU LOOP is enabled. Seems to work only under later versions of VMS
such as 4.6, does not work on 3.0.

The second is EVDMC, invoke this with the command R EVDMC. For this I used the following commands inside the diagnostic:

RUN MODE=TRAN on one machine
RUN MODE=REC on the other (unless the one instance is configured with the ports looping back).

You can add /PASS=n to the above commands to get the diagnostic to send and receive more buffers.

The other test was to configure DECnet on VMS 4.6 and do SET HOST.
*/

// TODO: Test MOP.
// TODO: Implement actual DDCMP protocol.

#if defined (VM_PDP10)                                  /* PDP10 version */
#include "pdp10_defs.h"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#endif

#include "sim_tmxr.h"
#include "pdp11_ddcmp.h"

#define DMC_CONNECT_POLL    2   /* Seconds */

extern int32 IREQ (HLVL);
extern int32 tmxr_poll;                                 /* calibrated delay */
extern int32 clk_tps;                                   /* clock ticks per second */
extern int32 tmr_poll;                                  /* instructions per tick */

#if !defined(DMC_NUMDEVICE)
#define DMC_NUMDEVICE 8         /* MAX # DMC-11 devices */
#endif

#if !defined(DMP_NUMDEVICE)
#define DMP_NUMDEVICE 8         /* MAX # DMP-11/DMV-11 devices */
#endif

#define DMC_RDX                     8

#define TYPE_BACCI 0
#define TYPE_CNTLI 1
#define TYPE_BASEI 03
#define TYPE_HALT  2
#define TYPE_BACCO 0
#define TYPE_CNTLO 1

#define TYPE_DMP_MODE 2
#define TYPE_DMP_CONTROL 1
#define TYPE_DMP_RECEIVE 0
#define TYPE_DMP_TRANSMIT 4


/* SEL0 */
#define DMC_TYPE_INPUT_MASK 0x0003
#define DMC_IN_HALT_MASK 0x0002
#define DMC_IN_IO_MASK 0x0004
#define DMP_IEO_MASK 0x0010
#define DMC_RQI_MASK 0x0020
#define DMP_RQI_MASK 0x0080
#define DMC_RDYI_MASK 0x0080
#define DMC_IEI_MASK 0x0040
#define DMP_IEI_MASK 0x0001
#define ROMI_MASK 0x0200
#define LU_LOOP_MASK 0x0800
#define MASTER_CLEAR_MASK 0x4000
#define RUN_MASK 0x8000

    /* SEL2 */
#define DMP_IN_IO_MASK 0x0004
#define DMP_TYPE_INPUT_MASK 0x0007
#define TYPE_OUTPUT_MASK 0x0003
#define OUT_IO_MASK 0x0004
#define DMC_RDYO_MASK 0x0080
#define DMC_IEO_MASK 0x0040
#define DMP_RDYI_MASK 0x0010

/* BSEL6 */
#define HALT_COMP_MASK  0x0200
#define NXM_MASK        0x0100
#define START_RCVD_MASK 0x0080
#define DISCONNECT_MASK 0x0040
#define LOST_DATA_MASK  0x0010
#define MAINT_RCVD_MASK 0x0008
#define NOBUF_MASK      0x0004
#define TIMEOUT_MASK    0x0002
#define NACK_THRES_MASK 0x0001

#define DSPDSR 0x22b3       /* KMC opcode to move line unit status to SEL2 */

#define SEL0_RUN_BIT 15
#define SEL0_MCLR_BIT 14
#define SEL0_LU_LOOP_BIT 11
#define SEL0_ROMI_BIT 9
#define SEL0_RDI_BIT 7
#define SEL0_DMC_IEI_BIT 6
#define SEL0_DMP_IEI_BIT 0
#define SEL0_DMP_IEO_BIT 4
#define SEL0_DMC_RQI_BIT 5
#define SEL0_DMP_RQI_BIT 7
#define SEL0_IN_IO_BIT 2
#define SEL0_TYPEI_BIT 0

#define SEL2_TYPEO_BIT 0
#define SEL2_RDO_BIT 7
#define SEL2_IEO_BIT 6
#define SEL2_OUT_IO_BIT 2
#define SEL2_LINE_BIT 8
#define SEL2_LINE_BIT_LENGTH 6
#define SEL2_PRIO_BIT 14
#define SEL2_PRIO_BIT_LENGTH 2

#define SEL4_MDM_RI  0x80
#define SEL4_MDM_DTR 0x40
#define SEL4_MDM_RTS 0x20
#define SEL4_MDM_HDX 0x10
#define SEL4_MDM_DSR 0x08
#define SEL4_MDM_CTS 0x04
#define SEL4_MDM_STN 0x02
#define SEL4_MDM_CAR 0x01

#define SEL6_LOST_DATA_BIT 4
#define SEL6_DISCONNECT_BIT 6

#define BUFFER_QUEUE_SIZE 8


#define POLL 1000
#define TRACE_BYTES_PER_LINE 16

struct csrs {
    uint16 *sel0;
    uint16 *sel2;
    uint16 *sel4;
    uint16 *sel6;
    uint16 *sel10;
};

typedef struct csrs CSRS;

typedef enum
{
    Uninitialised,  /* before MASTER CLEAR */
    Initialised,    /* after MASTER CLEAR */
    Running,        /* after any transmit or receive buffer has been supplied */
    Halted          /* after reciept of explicit halt input command */
} ControllerState;

typedef enum
{ 
    Idle,
    InputTransfer,
    OutputTransferReceiveBuffer,
    OutputTransferTransmitBuffer,
    OutputControl
} TransferState;

typedef enum
{
    Available, /* when empty or partially filled on read */
    ContainsData,
    TransferInProgress
} BufferState;

typedef struct control
{
    struct control *next;       /* Link */
    uint16  sel6;               /* Control Out Status Flags */
} CONTROL_OUT;

typedef struct buffer
{
    uint32 address;           /* unibus address of the buffer */
    uint16 count;             /* size of the buffer passed to the device by the driver */
    uint8 *transfer_buffer;   /* the buffer into which data is received or from which it is transmitted*/
    int actual_bytes_transferred;/* the number of bytes from the actual block that have been read or written */
    struct buffer *next;      /* next buffer in the queue */
    BufferState state;        /* state of this buffer */
    int is_loopback;          /* loopback was requested when this buffer was queued */
    uint32 buffer_return_time;/* time to return buffer to host */
} BUFFER;

typedef struct
{
    char * name;
    BUFFER queue[BUFFER_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    struct dmc_controller *controller; /* back pointer to the containing controller */
} BUFFER_QUEUE;

typedef enum
{
    DMC,
    DMR,
    DMP
} DEVTYPE;

typedef struct dmc_controller {
    CSRS *csrs;
    DEVICE *device;
    UNIT *unit;
    int index;                  /* Index in controller array */
    ControllerState state;
    TransferState transfer_state; /* current transfer state (type of transfer) */
    int transfer_type;
    int transfer_in_io; // remembers IN I/O setting at start of input transfer as host changes it during transfer!
    int connect_poll_interval;
    TMLN *line;
    BUFFER_QUEUE *receive_queue;
    BUFFER_QUEUE *transmit_queue;
    CONTROL_OUT *control_out;
    DEVTYPE dev_type;
    uint32 in_int;
    uint32 out_int;
    uint32 *baseaddr;
    uint16 *basesize;
    uint8 *modem;
    uint32 buffers_received_from_net;
    uint32 buffers_transmitted_to_net;
    uint32 receive_buffer_output_transfers_completed;
    uint32 transmit_buffer_output_transfers_completed;
    uint32 receive_buffer_input_transfers_completed;
    uint32 transmit_buffer_input_transfers_completed;
    uint32 control_out_operations_completed;
    uint32 byte_wait;                           /* rcv/xmt byte delay */
} CTLR;

#define ctlr up7                        /* Unit back pointer to controller */

t_stat dmc_rd(int32* data, int32 PA, int32 access);
t_stat dmc_wr(int32  data, int32 PA, int32 access);
t_stat dmc_svc(UNIT * uptr);
t_stat dmc_poll_svc(UNIT * uptr);
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
t_stat dmc_settype (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showtype (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setstats (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showstats (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_showqueues (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setconnectpoll (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showconnectpoll (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat dmc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *dmc_description (DEVICE *dptr);
char *dmp_description (DEVICE *dptr);
int dmc_is_attached(UNIT* uptr);
int dmc_is_dmc(CTLR *controller);
int dmc_is_rqi_set(CTLR *controller);
int dmc_is_rdyi_set(CTLR *controller);
int dmc_is_iei_set(CTLR *controller);
int dmc_is_ieo_set(CTLR *controller);
uint8 dmc_get_modem(CTLR *controller);
void dmc_set_modem_dtr(CTLR *controller);
void dmc_clr_modem_dtr(CTLR *controller);
void dmc_process_command(CTLR *controller);
t_bool dmc_buffer_fill_receive_buffers(CTLR *controller);
void dmc_start_transfer_receive_buffer(CTLR *controller);
int dmc_buffer_send_transmit_buffers(CTLR *controller);
void dmc_buffer_queue_init(CTLR *controller, BUFFER_QUEUE *q, char *name);
void dmc_buffer_queue_init_all(CTLR *controller);
BUFFER *dmc_buffer_queue_head(BUFFER_QUEUE *q);
int dmc_buffer_queue_full(BUFFER_QUEUE *q);
void dmc_start_transfer_transmit_buffer(CTLR *controller);
void dmc_queue_control_out(CTLR *controller, uint16 sel6);

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_REG  0x0002                                 /* trace read/write registers */
#define DBG_WRN  0x0004                                 /* display warnings */
#define DBG_INF  0x0008                                 /* display informational messages (high level trace) */
#define DBG_DAT  (TMXR_DBG_PXMT|TMXR_DBG_PRCV)          /* display data buffer contents */
#define DBG_DTS  0x0020                                 /* display data summary */
#define DBG_MDM  TMXR_DBG_MDM                           /* modem related transitions */
#define DBG_CON  TMXR_DBG_CON                           /* display socket open/close, connection establishment */
#define DBG_INT  0x0040                                 /* display interrupt activites */


DEBTAB dmc_debug[] = {
    {"TRACE",   DBG_TRC},
    {"WARN",    DBG_WRN},
    {"REG",     DBG_REG},
    {"INFO",    DBG_INF},
    {"DATA",    DBG_DAT},
    {"DATASUM", DBG_DTS},
    {"MODEM",   DBG_MDM},
    {"CONNECT", DBG_CON},
    {"INT", DBG_INT},
    {0}
};

UNIT dmc_units[DMC_NUMDEVICE+1];            /* One per device and an I/O polling unit */

UNIT dmc_unit_template = { UDATA (&dmc_svc, UNIT_ATTABLE, 0) };
UNIT dmc_poll_unit_template = { UDATA (&dmc_poll_svc, UNIT_DIS, 0) };

UNIT dmp_units[DMP_NUMDEVICE+1];            /* One per device and an I/O polling unit */

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

TMLN dmc_ldsc[DMC_NUMDEVICE];               /* line descriptors */
TMXR dmc_desc = { 1, NULL, 0, dmc_ldsc };   /* mux descriptor */

uint32 dmc_ini_summary = 0;         /* In Command Interrupt Summary for all controllers */
uint32 dmc_outi_summary = 0;        /* Out Command Interrupt Summary for all controllers */

BUFFER_QUEUE dmc_receive_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_transmit_queues[DMC_NUMDEVICE];

TMLN dmp_ldsc[DMC_NUMDEVICE];               /* line descriptors */
TMXR dmp_desc = { 1, NULL, 0, dmp_ldsc };   /* mux descriptor */

BUFFER_QUEUE dmp_receive_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_transmit_queues[DMP_NUMDEVICE];

REG dmc_reg[] = {
    { GRDATA (RXINT,      dmc_ini_summary, DEV_RDX, 32, 0) },
    { GRDATA (TXINT,     dmc_outi_summary, DEV_RDX, 32, 0) },
    { BRDATA (SEL0,              dmc_sel0, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL2,              dmc_sel2, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL4,              dmc_sel4, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL6,              dmc_sel6, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SPEED,            dmc_speed, DEV_RDX, 32, DMC_NUMDEVICE) },
    { BRDATA (PEER,              dmc_peer, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE) },
    { BRDATA (PORT,              dmc_port, DEV_RDX,  8, DMC_NUMDEVICE*CBUFSIZE) },
    { BRDATA (BASEADDR,      dmc_baseaddr, DEV_RDX, 32, DMC_NUMDEVICE) },
    { BRDATA (BASESIZE,      dmc_basesize, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (MODEM,            dmc_modem, DEV_RDX,  8, DMC_NUMDEVICE) },
    { NULL }  };

REG dmp_reg[] = {
    { BRDATA (SEL0,              dmp_sel0, DEV_RDX, 16, DMP_NUMDEVICE) },
    { BRDATA (SEL2,              dmp_sel2, DEV_RDX, 16, DMP_NUMDEVICE) },
    { BRDATA (SEL4,              dmp_sel4, DEV_RDX, 16, DMP_NUMDEVICE) },
    { BRDATA (SEL6,              dmp_sel6, DEV_RDX, 16, DMP_NUMDEVICE) },
    { BRDATA (SPEED,            dmp_speed, DEV_RDX, 32, DMP_NUMDEVICE) },
    { BRDATA (PEER,              dmp_peer, DEV_RDX,  8, DMP_NUMDEVICE*CBUFSIZE) },
    { BRDATA (PORT,              dmp_port, DEV_RDX,  8, DMP_NUMDEVICE*CBUFSIZE) },
    { BRDATA (BASEADDR,      dmp_baseaddr, DEV_RDX, 32, DMP_NUMDEVICE) },
    { BRDATA (BASESIZE,      dmp_basesize, DEV_RDX, 16, DMP_NUMDEVICE) },
    { BRDATA (MODEM,            dmp_modem, DEV_RDX,  8, DMP_NUMDEVICE) },
    { NULL }  };

extern DEVICE dmc_dev;

MTAB dmc_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmc_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source depends on LINEMODE" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN|MTAB_VALR,0, "TYPE", "TYPE={DMR,DMC}" ,&dmc_settype, &dmc_showtype, NULL, "Set/Display device type"  },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, NULL, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 },
};

extern DEVICE dmp_dev;

MTAB dmp_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmp_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source depends on LINEMODE" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, NULL, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 },
};

extern DEVICE dmv_dev;

MTAB dmv_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dmc_setnumdevices, &dmc_shownumdevices, (void *) &dmv_dev, "Display number of devices" },
    { MTAB_XTD|MTAB_VUN,          0, "PEER", "PEER=address:port",
        &dmc_setpeer, &dmc_showpeer, NULL, "Display destination/source depends on LINEMODE" },
    { MTAB_XTD|MTAB_VUN,          0, "SPEED", "SPEED=bits/sec (0=unrestricted)" ,
        &dmc_setspeed, &dmc_showspeed, NULL, "Display rate limit" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "QUEUES", "QUEUES",
        NULL, &dmc_showqueues, NULL, "Display Queue state" },
    { MTAB_XTD|MTAB_VUN,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, NULL, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 },
};

#define IOLN_DMC        010

DIB dmc_dib = { IOBA_AUTO, IOLN_DMC, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta}, IOLN_DMC };

#define IOLN_DMP        010

DIB dmp_dib = { IOBA_AUTO, IOLN_DMP, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta }, IOLN_DMC};

#define IOLN_DMV        020

DIB dmv_dib = { IOBA_AUTO, IOLN_DMV, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_ininta, &dmc_outinta }, IOLN_DMC};

DEVICE dmc_dev =
    { "DMC", dmc_units, dmc_reg, dmc_mod, 2, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmc_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0, dmc_debug,
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
    { "DMP", dmp_units, dmp_reg, dmp_mod, 2, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmp_description };

DEVICE dmv_dev =
    { "DMV", dmp_units, dmp_reg, dmv_mod, 2, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_QBUS | DEV_DEBUG, 0, dmc_debug,
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
    for (i=0; i<DMC_NUMDEVICE + DMP_NUMDEVICE; i++)
    {
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
    CTLR *controller = dmc_get_controller_from_unit(uptr);

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
    t_stat status = SCPE_OK;
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

t_stat dmc_showtype (FILE* st, UNIT* uptr, int32 val, void* desc)
{
    DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
    int32 dmc = (int32)(uptr-dptr->units);

    CTLR *controller = dmc_get_controller_from_unit(uptr);
    switch (controller->dev_type)
    {
    case DMC:
        {
            fprintf(st, "type=DMC");
            break;
        }
    case DMR:
        {
            fprintf(st, "type=DMR");
            break;
        }
    case DMP:
        {
            fprintf(st, "type=%s", (UNIBUS) ? "DMP" : "DMV");
            break;
        }
    default:
        {
            fprintf(st, "type=???");
            break;
        }
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
    if (strcmp (gbuf,"DMC")==0)
    {
        controller->dev_type = DMC;
    }
    else if (strcmp (gbuf,"DMR")==0)
    {
        controller->dev_type = DMR;
    }
    else
    {
        status = SCPE_ARG;
    }

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

    return SCPE_OK;
}

void dmc_showqueue (FILE* st, BUFFER_QUEUE *queue, t_bool detail)
{
    size_t i;
    int states[3] = {0,0,0};
    static const char *state_names[] = {"Available", "ContainsData", "TransferInProgress"};

    fprintf (st, "%s Queue:\n", queue->name);
    fprintf (st, "   Count: %d\n", queue->count);
    fprintf (st, "   Head:  %d\n", queue->head);
    fprintf (st, "   Tail:  %d\n", queue->tail);
    for (i=0; i<sizeof(queue->queue)/sizeof(queue->queue[0]); ++i)
    {
        ++states[queue->queue[i].state];
    }
    fprintf (st, "   Available:            %d\n", states[Available]);
    fprintf (st, "   Contains Data:        %d\n", states[ContainsData]);
    fprintf (st, "   Transfer In Progress: %d\n", states[TransferInProgress]);
    if (detail)
    {
        for (i=0; i<sizeof(queue->queue)/sizeof(queue->queue[0]); ++i)
        {
            fprintf (st, "%s.queue[%d]\n", queue->name, i);
            fprintf (st, "   address:                  0x%08X\n", queue->queue[i].address);
            fprintf (st, "   count:                    0x%04X\n", queue->queue[i].count);
            fprintf (st, "   actual_bytes_transferred: 0x%04X\n", queue->queue[i].actual_bytes_transferred);
            fprintf (st, "   state:                    %s\n", state_names[queue->queue[i].state]);
            if (queue->queue[i].is_loopback)
                fprintf (st, "   is_loopback:              %s\n", queue->queue[i].is_loopback ? "true" : "false");
            if (queue->queue[i].buffer_return_time)
                fprintf (st, "   buffer_return_time:       0x%08X\n", queue->queue[i].buffer_return_time);
        }
    }
}

t_stat dmc_showqueues (FILE* st, UNIT* uptr, int32 val, void* desc)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    static const char *states[] = {"Uninitialised", "Initialised", "Running", "Halted"};
    static const char *tstates[] = {"Idle", "InputTransfer", "OutputTransferReceiveBuffer", "OutputTransferTransmitBuffer", "OutputControl"};

    dmc_showstats (st, uptr, val, desc);
    fprintf (st, "State: %s\n", states[controller->state]);
    fprintf (st, "TransferState: %s\n", tstates[controller->transfer_state]);
    dmc_showqueue (st, controller->transmit_queue, TRUE);
    dmc_showqueue (st, controller->receive_queue, TRUE);
    if (controller->control_out)
    {
        CONTROL_OUT *control;

        fprintf (st, "Control Out Queue:\n");
        for (control=controller->control_out; control; control=control->next)
        {
            fprintf (st, "   SEL6:  0x%04X\n", control->sel6);
        }
    }
    return SCPE_OK;
}

t_stat dmc_setstats (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    controller->receive_buffer_output_transfers_completed = 0;
    controller->transmit_buffer_output_transfers_completed = 0;
    controller->receive_buffer_input_transfers_completed = 0;
    controller->transmit_buffer_input_transfers_completed = 0;
    controller->control_out_operations_completed = 0;

    printf("Statistics reset\n" );

    return status;
}

t_stat dmc_showconnectpoll (FILE* st, UNIT* uptr, int32 val, void* desc)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    fprintf(st, "connectpoll=%d", controller->connect_poll_interval);
    return SCPE_OK;
}

t_stat dmc_setconnectpoll (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    if (!cptr) return SCPE_IERR;
    if (sscanf(cptr, "%d", &controller->connect_poll_interval) != 1)
    {
        status = SCPE_ARG;
    }

    return status;
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

    for (j=0; j<2; j++)
    {
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
    if ((r != SCPE_OK) || (newln == (dptr->numunits - 1)))
        return r;
    if (newln == 0)
        return SCPE_ARG;
    for (i=dptr->numunits; i<(uint32)newln; ++i)
    {
        dptr->units[i] = dmc_unit_template;
        dptr->units[i].ctlr = &dmc_ctrls[(dptr == &dmc_dev) ? i : i+DMC_NUMDEVICE];
    }
    dibptr->lnt = newln * addrlnt;                      /* set length */
    dptr->numunits = newln + 1;
    dptr->units[newln] = dmc_poll_unit_template;
    mp->lines = newln;
    return dmc_reset ((DEVICE *)desc);                  /* setup devices and auto config */
}

t_stat dmc_shownumdevices (FILE *st, UNIT *uptr, int32 val, void *desc)
{
    DEVICE *dptr = (UNIBUS) ? find_dev_from_unit (uptr) : &dmv_dev;

    fprintf (st, "lines=%d", dptr->numunits-1);
    return SCPE_OK;
}


t_stat dmc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
    char devname[16];

    sprintf(devname, "%s11", (dptr == &dmc_dev) ? "DMC" : ((UNIBUS) ? "DMP" : "DMV"));
    fprintf(st, "The %s is a synchronous serial point-to-point communications device.\n", devname);
    fprintf(st, "A real %s transports data using DDCMP, the emulated device makes a\n", devname);
    fprintf(st, "TCP/IP connection to another emulated device and sends length-prefixed\n");
    fprintf(st, "messages across the connection, each message representing a single buffer\n");
    fprintf(st, "passed to the %s. The %s can be used for point-to-point DDCMP\n", devname, devname);
    fprintf(st, "connections carrying DECnet and other types of networking, e.g. from ULTRIX\n");
    fprintf(st, "or DSM.\n\n");
    fprintf(st, "A total of %d %s devices can be simulated concurrently. The number\n", (dptr == &dmc_dev) ? DMC_NUMDEVICE : DMP_NUMDEVICE, devname);
    fprintf(st, "of simulated %s devices or lines can be specified with command:\n", devname);
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s LINES=n\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "To set the host and port to which data is to be transmitted use the\n");
    fprintf(st, "following command (required for PRIMARY and SECONDARY, secondary will check\n");
    fprintf(st, "it is receiving from the configured primary):\n");
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s0 PEER=host:port\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "The device must be attached to a receive port, use the ATTACH command\n");
    fprintf(st, "specifying the receive port number, even if the line mode is primary.\n");
    fprintf(st, "\n");
    fprintf(st, "The minimum interval between attempts to connect to the other side is set\n");
    fprintf(st, "using the following command:\n");
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s0 CONNECTPOLL=n\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "Where n is the number of seconds. The default is 30 seconds.\n");
    fprintf(st, "\n");
    fprintf(st, "If you want to experience the actual data rates of the physical hardware you\n");
    fprintf(st, "can set the bit rate of the simulated line can be set using the following\n");
    fprintf(st, "command:\n\n");
    fprintf(st, "   sim> SET %s0 SPEED=n\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "Where n is the number of data bits per second that the simulated line runs\n");
    fprintf(st, "at.  In practice this is implemented as a delay in reading the bytes from\n");
    fprintf(st, "the socket.  Use a value of zero to run at full speed with no artificial\n");
    fprintf(st, "throttling.\n");
    fprintf(st, "\n");
    fprintf(st, "To configure two simulators to talk to each other use the following example:\n");
    fprintf(st, "\n");
    fprintf(st, "Machine 1\n");
    fprintf(st, "   sim> SET %s ENABLE\n", dptr->name);
    fprintf(st, "   sim> SET %s0 PEER=LOCALHOST:2222\n", dptr->name);
    fprintf(st, "   sim> ATTACH %s0 1111\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "Machine 2\n");
    fprintf(st, "   sim> SET %s ENABLE\n", dptr->name);
    fprintf(st, "   sim> SET %s0 PEER= LOCALHOST:1111\n", dptr->name);
    fprintf(st, "   sim> ATTACH %s0 2222\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "Debugging\n");
    fprintf(st, "=========\n");
    fprintf(st, "The simulator has a number of debug options, these are:\n");
    fprintf(st, "        REG      Shows whenever a CSR is read or written and the current value.\n");
    fprintf(st, "        INFO     Shows higher-level tracing only.\n");
    fprintf(st, "        WARN     Shows any warnings.\n");
    fprintf(st, "        TRACE    Shows more detailed trace information.\n");
    fprintf(st, "        DATA     Shows the actual data sent and received.\n");
    fprintf(st, "        DATASUM  Brief summary of each received and transmitted buffer.\n");
    fprintf(st, "                 Ignored if DATA is set.\n");
    fprintf(st, "        SOCKET   Shows socket opens and closes.\n");
    fprintf(st, "        CONNECT  Shows sockets actually connecting.\n");
    fprintf(st, "\n");
    fprintf(st, "To get a full trace use\n");
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s DEBUG\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "However it is recommended to use the following when sending traces:\n");
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s DEBUG=REG;INFO;WARN\n", dptr->name);
    fprintf(st, "\n");
    return SCPE_OK;
}

t_stat dmc_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr)
{
    fprintf (st, "The communication line performs input and output through a TCP session\n");
    fprintf (st, "connected to a user-specified port.  The ATTACH command specifies the");
    fprintf (st, "listening port to be used when the incoming connection is established:\n\n");
    fprintf (st, "   sim> ATTACH %sn {interface:}port        set up listening port\n\n", dptr->name);
    fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
    fprintf (st, "other TCP/IP activities.\n\n");
    return SCPE_OK;
}

void dmc_setinint(CTLR *controller)
{
    if (!dmc_is_iei_set(controller))
        return;
    if (!controller->in_int)
        sim_debug(DBG_INT, controller->device, "SET_INT(RX:%d)\n", controller->index);
    controller->in_int = 1;
    dmc_ini_summary |= (1u << controller->index);
    SET_INT(DMCRX);
}

void dmc_clrinint(CTLR *controller)
{
    controller->in_int = 0;
    if (dmc_ini_summary & (1u << controller->index))
        sim_debug(DBG_INT, controller->device, "CLR_INT(RX:%d)\n", controller->index);
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
    if (!controller->out_int)
        sim_debug(DBG_INT, controller->device, "SET_INT(TX:%d)\n", controller->index);
    controller->out_int = 1;
    dmc_outi_summary |= (1u << controller->index);
    SET_INT(DMCTX);
}

void dmc_clroutint(CTLR *controller)
{
    controller->out_int = 0;
    if (dmc_outi_summary & (1u << controller->index))
        sim_debug(DBG_INT, controller->device, "CLR_INT(TX:%d)\n", controller->index);
    dmc_outi_summary &= ~(1u << controller->index);
    if (!dmc_outi_summary)
        CLR_INT(DMCTX);
    else
        SET_INT(DMCTX);
    CLR_INT(DMCTX);
}

int dmc_getsel(int addr)
{
    return (addr >> 1) & ((UNIBUS) ? 03 : 07);
}

uint16 dmc_bitfld(int data, int start_bit, int length)
{
    uint16 ans = data >> start_bit;
    uint32 mask = (1 << (length))-1;
    ans &= mask;
    return ans;
}

void dmc_dumpregsel0(CTLR *controller, int trace_level, char * prefix, uint16 data)
{
    char *type_str = "";
    uint16 type = dmc_bitfld(data, SEL0_TYPEI_BIT, 2);

    if (dmc_is_dmc(controller))
    {
        if (dmc_is_rqi_set(controller))
        {
            if (type==TYPE_BACCI)
                type_str = "BA/CC I";
            else if (type==TYPE_CNTLI)
                type_str = "CNTL I";
            else if (type==TYPE_BASEI)
                type_str = "BASE I";
            else
                type_str = "?????";
        }

        sim_debug(
            trace_level,
            controller->device,
            "%s SEL0 (0x%04x) %s%s%s%s%s%s%s%s%s\n",
            prefix,
            data,
            dmc_bitfld(data, SEL0_RUN_BIT, 1) ? "RUN " : "",
            dmc_bitfld(data, SEL0_MCLR_BIT, 1) ? "MCLR " : "",
            dmc_bitfld(data, SEL0_LU_LOOP_BIT, 1) ? "LU LOOP " : "",
            dmc_bitfld(data, SEL0_ROMI_BIT, 1) ? "ROMI " : "",
            dmc_bitfld(data, SEL0_RDI_BIT, 1) ? "RDI " : "",
            dmc_bitfld(data, SEL0_DMC_IEI_BIT, 1) ? "IEI " : "",
            dmc_bitfld(data, SEL0_DMC_RQI_BIT, 1) ? "RQI " : "",
            dmc_bitfld(data, SEL0_IN_IO_BIT, 1) ? "IN I/O " : "",
            type_str
            );
    }
    else
    {
        sim_debug(
            trace_level,
            controller->device,
            "%s SEL0 (0x%04x) %s%s%s%s%s%s\n",
            prefix,
            data,
            dmc_bitfld(data, SEL0_RUN_BIT, 1) ? "RUN " : "",
            dmc_bitfld(data, SEL0_MCLR_BIT, 1) ? "MCLR " : "",
            dmc_bitfld(data, SEL0_LU_LOOP_BIT, 1) ? "LU LOOP " : "",
            dmc_bitfld(data, SEL0_DMP_RQI_BIT, 1) ? "RQI " : "",
            dmc_bitfld(data, SEL0_DMP_IEO_BIT, 1) ? "IEO " : "",
            dmc_bitfld(data, SEL0_DMP_IEI_BIT, 1) ? "IEI " : ""
            );
    }
}

void dmc_dumpregsel2(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
    char *type_str = "";
    uint16 type = dmc_bitfld(data, SEL2_TYPEO_BIT, 2);

    if (type==TYPE_BACCO)
        type_str = "BA/CC O";
    else if (type==TYPE_CNTLO)
        type_str = "CNTL O";
    else
        type_str = "?????";

    sim_debug(
        trace_level,
        controller->device,
        "%s SEL2 (0x%04x) PRIO=%d LINE=%d %s%s%s%s\n",
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
    if (dmc_is_rdyi_set(controller))
    {
        sim_debug(
            trace_level,
            controller->device,
            "%s SEL4 (0x%04x) %s%s%s%s%s%s%s\n",
            prefix,
            data,
            dmc_bitfld(data, /* SEL4_MDM_RI */ 7, 1) ? "RI " : "",
            dmc_bitfld(data, /* SEL4_MDM_RTS */ 6, 1) ? "RTS " : "",
            dmc_bitfld(data, /* SEL4_MDM_HDX */ 4, 1) ? "HDX " : "",
            dmc_bitfld(data, /* SEL4_MDM_DSR */ 3, 1) ? "DSR " : "",
            dmc_bitfld(data, /* SEL4_MDM_CTS */ 2, 1) ? "CTS " : "",
            dmc_bitfld(data, /* SEL4_MDM_STN */ 1, 1) ? "STN " : "",
            dmc_bitfld(data, /* SEL4_MDM_CAR */ 0, 1) ? "CAR " : "");
    }
    else
    {
        sim_debug(
            trace_level,
            controller->device,
            "%s SEL4 (0x%04x)\n",
            prefix,
            data);
    }
}

void dmc_dumpregsel6(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
    if (dmc_is_rdyi_set(controller))
    {
        sim_debug(
            trace_level,
            controller->device,
            "%s SEL6 (0x%04x) %s%s\n",
            prefix,
            data,
            dmc_bitfld(data, SEL6_LOST_DATA_BIT, 1) ? "LOST_DATA " : "",
            dmc_bitfld(data, SEL6_DISCONNECT_BIT, 1) ? "DISCONNECT " : "");
    }
    else
    {
        sim_debug(
            trace_level,
            controller->device,
            "%s SEL6 (0x%04x)\n",
            prefix,
            data);
    }
}

void dmc_dumpregsel10(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
    sim_debug(
        trace_level,
        controller->device,
        "%s SEL10 (0x%04x) %s\n",
        prefix,
        data,
        dmc_bitfld(data, SEL6_LOST_DATA_BIT, 1) ? "LOST_DATA " : "");
}

uint16 dmc_getreg(CTLR *controller, int reg)
{
    uint16 ans = 0;
    switch (dmc_getsel(reg))
    {
    case 00:
        ans = *controller->csrs->sel0;
        dmc_dumpregsel0(controller, DBG_REG, "Getting", ans);
        break;
    case 01:
        ans = *controller->csrs->sel2;
        dmc_dumpregsel2(controller, DBG_REG, "Getting", ans);
        break;
    case 02:
        ans = *controller->csrs->sel4;
        dmc_dumpregsel4(controller, DBG_REG, "Getting", ans);
        break;
    case 03:
        ans = *controller->csrs->sel6;
        dmc_dumpregsel6(controller, DBG_REG, "Getting", ans);
        break;
    case 04:
        ans = *controller->csrs->sel10;
        dmc_dumpregsel10(controller, DBG_REG, "Getting", ans);
        break;
    default:
        {
            sim_debug(DBG_WRN, controller->device, "dmc_getreg(). Invalid register %d", reg);
        }
    }

    return ans;
}

void dmc_setreg(CTLR *controller, int reg, uint16 data)
{
    char *trace = "Setting";
    switch (dmc_getsel(reg))
    {
    case 00:
        dmc_dumpregsel0(controller, DBG_REG, trace, data);
        *controller->csrs->sel0 = data;
        break;
    case 01:
        dmc_dumpregsel2(controller, DBG_REG, trace, data);
        *controller->csrs->sel2 = data;
        break;
    case 02:
        dmc_dumpregsel4(controller, DBG_REG, trace, data);
        *controller->csrs->sel4 = data;
        break;
    case 03:
        dmc_dumpregsel6(controller, DBG_REG, trace, data);
        *controller->csrs->sel6 = data;
        break;
    case 04:
        dmc_dumpregsel10(controller, DBG_REG, trace, data);
        *controller->csrs->sel10 = data;
        break;
    default:
        {
            sim_debug(DBG_WRN, controller->device, "dmc_setreg(). Invalid register %d", reg);
        }
    }
}

int dmc_is_master_clear_set(CTLR *controller)
{
    return *controller->csrs->sel0 & MASTER_CLEAR_MASK;
}

int dmc_is_lu_loop_set(CTLR *controller)
{
    return *controller->csrs->sel0 & LU_LOOP_MASK;
}

int dmc_is_rqi_set(CTLR *controller)
{
    int ans = 0;
    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel0 & DMC_RQI_MASK;
    }
    else
    {
        ans = *controller->csrs->sel0 & DMP_RQI_MASK;
    }
    return ans;
}

int dmc_is_rdyi_set(CTLR *controller)
{
    int ans = 0;
    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel0 & DMC_RDYI_MASK;
    }
    else
    {
        ans = *controller->csrs->sel2 & DMP_RDYI_MASK;
    }
    return ans;
}

int dmc_is_iei_set(CTLR *controller)
{
    int ans = 0;
    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel0 & DMC_IEI_MASK;
    }
    else
    {
        ans = *controller->csrs->sel0 & DMP_IEI_MASK;
    }

    return ans;
}

int dmc_is_ieo_set(CTLR *controller)
{
    int ans = 0;
    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel2 & DMC_IEO_MASK;
    }
    else
    {
        ans = *controller->csrs->sel0 & DMP_IEO_MASK;
    }

    return ans;
}

int dmc_is_in_io_set(CTLR *controller)
{
    int ans = 0;
    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel0 & DMC_IN_IO_MASK;
    }
    else
    {
        ans = !*controller->csrs->sel2 & DMP_IN_IO_MASK;
    }

    return ans;
}

int dmc_is_rdyo_set(CTLR *controller)
{
    return *controller->csrs->sel2 & DMC_RDYO_MASK;
}

void dmc_set_rdyi(CTLR *controller)
{
    if (dmc_is_dmc(controller))
    {
        dmc_setreg(controller, 0, *controller->csrs->sel0 | DMC_RDYI_MASK);
        dmc_setreg(controller, 4, *controller->modem);
        dmc_setreg(controller, 6, *controller->modem & SEL4_MDM_DTR);
    }
    else
    {
        dmc_setreg(controller, 2, *controller->csrs->sel2 | DMP_RDYI_MASK);
    }

    dmc_setinint(controller);
}

void dmc_clear_rdyi(CTLR *controller)
{
    if (dmc_is_dmc(controller))
    {
        dmc_setreg(controller, 0, *controller->csrs->sel0 & ~DMC_RDYI_MASK);
    }
    else
    {
        dmc_setreg(controller, 2, *controller->csrs->sel2 & ~DMP_RDYI_MASK);
    }
}

void dmc_set_rdyo(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | DMC_RDYO_MASK);

    dmc_setoutint(controller);
}

uint8 dmc_get_modem(CTLR *controller)
{
int32 modem_bits;

tmxr_set_get_modem_bits (controller->line, 0, 0, &modem_bits);
*controller->modem &= ~(SEL4_MDM_CTS|SEL4_MDM_CAR|SEL4_MDM_RI|SEL4_MDM_DSR);
*controller->modem |= (modem_bits&TMXR_MDM_DCD) ? SEL4_MDM_CAR : 0;
*controller->modem |= (modem_bits&TMXR_MDM_CTS) ? SEL4_MDM_CTS : 0;
*controller->modem |= (modem_bits&TMXR_MDM_DSR) ? SEL4_MDM_DSR : 0;
*controller->modem |= (modem_bits&TMXR_MDM_RNG) ? SEL4_MDM_RI : 0;
return (*controller->modem);
}

void dmc_set_modem_dtr(CTLR *controller)
{
    if (dmc_is_attached(controller->unit) && (!(SEL4_MDM_DTR & *controller->modem)))
    {
        tmxr_set_get_modem_bits (controller->line, TMXR_MDM_DTR|TMXR_MDM_RTS, 0, NULL);
        *controller->modem |= SEL4_MDM_DTR|SEL4_MDM_RTS;
        controller->line->rcve = 1;
    }
}

void dmc_clr_modem_dtr(CTLR *controller)
{
    tmxr_set_get_modem_bits (controller->line, 0, TMXR_MDM_DTR|TMXR_MDM_RTS, NULL);
    *controller->modem &= ~(SEL4_MDM_DTR|SEL4_MDM_CTS);
    controller->line->rcve = 0;
}

void dmc_set_lost_data(CTLR *controller)
{
    dmc_setreg(controller, 6, *controller->csrs->sel6 | LOST_DATA_MASK);
}

void dmc_clear_master_clear(CTLR *controller)
{
    dmc_setreg(controller, 0, *controller->csrs->sel0 & ~MASTER_CLEAR_MASK);
}

void dmc_set_run(CTLR *controller)
{
    dmc_setreg(controller, 0, *controller->csrs->sel0 | RUN_MASK);
}

int dmc_get_input_transfer_type(CTLR *controller)
{
    int ans = 0;

    if (dmc_is_dmc(controller))
    {
        ans = *controller->csrs->sel0 & DMC_TYPE_INPUT_MASK;
    }
    else
    {
        ans = *controller->csrs->sel2 & DMP_TYPE_INPUT_MASK;
    }

    return ans;
}

void dmc_set_type_output(CTLR *controller, int type)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | (type & TYPE_OUTPUT_MASK));
}

void dmc_set_out_io(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | OUT_IO_MASK);
}

void dmc_clear_out_io(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 & ~OUT_IO_MASK);
}

void dmc_process_master_clear(CTLR *controller)
{
    CONTROL_OUT *control;

    sim_debug(DBG_INF, controller->device, "Master clear\n");
    dmc_clear_master_clear(controller);
    dmc_clr_modem_dtr(controller);
    controller->state = Initialised;
    while ((control = controller->control_out))
    {
        controller->control_out = control->next;
        free (control);
    }
    controller->control_out = NULL;
    dmc_setreg(controller, 0, 0);
    if (controller->dev_type == DMR)
    {
         /* DMR-11 indicates microdiagnostics complete when this is set */
        dmc_setreg(controller, 2, 0x8000);
    }
    else
    {
        /* preserve contents of BSEL3 if DMC-11 */
        dmc_setreg(controller, 2, *controller->csrs->sel2 & 0xFF00);
    }
    if (controller->dev_type == DMP)
    {
        dmc_setreg(controller, 4, 077);
    }
    else
    {
        dmc_setreg(controller, 4, 0);
    }

    if (controller->dev_type == DMP)
    {
        dmc_setreg(controller, 6, 0305);
    }
    else
    {
        dmc_setreg(controller, 6, 0);
    }
    dmc_buffer_queue_init_all(controller);

    controller->transfer_state = Idle;
    dmc_set_run(controller);

}

void dmc_start_input_transfer(CTLR *controller)
{
    sim_debug(DBG_INF, controller->device, "Starting input transfer\n");
    controller->transfer_state = InputTransfer;
    dmc_set_rdyi(controller);
}

void dmc_start_data_output_transfer(CTLR *controller, uint32 addr, int16 count, int is_receive)
{
    if (is_receive)
    {
        sim_debug(DBG_INF, controller->device, "Starting data output transfer for receive, address=0x%08x, count=%d\n", addr, count);
        dmc_set_out_io(controller);
    }
    else
    {
        sim_debug(DBG_INF, controller->device, "Starting data output transfer for transmit, address=0x%08x, count=%d\n", addr, count);
        dmc_clear_out_io(controller); 
    }

    dmc_setreg(controller, 4, addr & 0xFFFF);
    dmc_setreg(controller, 6, (((addr & 0x30000)) >> 2) | count);
    controller->transfer_state = (is_receive) ? OutputTransferReceiveBuffer : OutputTransferTransmitBuffer;
    dmc_set_type_output(controller, TYPE_BACCO);
    dmc_set_rdyo(controller);
}

void dmc_start_control_output_transfer(CTLR *controller)
{
    if ((!controller->control_out) || 
        (controller->transfer_state != Idle) || 
        (dmc_is_rdyo_set(controller)))
        return;
    sim_debug(DBG_INF, controller->device, "Starting control output transfer: SEL6 = 0x%04X\n", controller->control_out->sel6);
    controller->transfer_state = OutputControl;
    dmc_clear_out_io(controller);
    dmc_set_type_output(controller, TYPE_CNTLO);
    dmc_setreg (controller, 6, controller->control_out->sel6);
    dmc_set_rdyo(controller);
}

t_stat dmc_svc(UNIT* uptr)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    if (dmc_is_attached(controller->unit))
    {
        dmc_start_control_output_transfer(controller);

        dmc_buffer_send_transmit_buffers(controller);
        if (controller->transfer_state == Idle)
            dmc_start_transfer_transmit_buffer(controller);

        dmc_buffer_fill_receive_buffers(controller);
        if (controller->transfer_state == Idle)
            dmc_start_transfer_receive_buffer(controller);

        sim_activate (uptr, tmxr_poll);
    }

    return SCPE_OK;
}

static t_stat dmc_poll_svc (UNIT *uptr)
{
    DEVICE *dptr = ((CTLR *)uptr->ctlr)->device;
    int maxunits = (&dmc_dev == dptr) ? DMC_NUMDEVICE : DMP_NUMDEVICE;
    DIB *dibptr = (DIB *)dptr->ctxt;
    int addrlnt = (UNIBUS) ? IOLN_DMC : IOLN_DMV;
    TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
    int32 dmc, active, attached;

    sim_debug(DBG_TRC, dptr, "dmc_poll_svc()\n");

    dmc = tmxr_poll_conn(mp);
    if (dmc >= 0)                                   /* new connection? */
    {
        dmc_get_modem ((CTLR *)dptr->units[dmc].ctlr);
    }
    tmxr_poll_rx (mp);
    tmxr_poll_tx (mp);
    for (dmc=active=attached=0; dmc < mp->lines; dmc++)
    {
        TMLN *lp = &mp->ldsc[dmc];
        CTLR *controller = (CTLR *)dptr->units[dmc].ctlr;
        uint8 old_modem, new_modem;

        if (dptr->units[dmc].flags & UNIT_ATT)
            ++attached;
        if (mp->ldsc[dmc].conn)
            ++active;
        old_modem = *controller->modem;
        new_modem = dmc_get_modem (controller);
        if ((old_modem & SEL4_MDM_DSR) && 
            (!(new_modem & SEL4_MDM_DSR)))
            dmc_queue_control_out(controller, DISCONNECT_MASK);
        if (lp->xmte && tmxr_tpbusyln(lp))
        {
            sim_debug(DBG_DAT, dptr, "dmc_poll_svc(dmc=%d) - Packet Transmission of remaining %d bytes restarting...\n", dmc, tmxr_tpqln (lp));
            dmc_svc (&dptr->units[dmc]);              /* Flush pending output */
        }
        dmc_buffer_fill_receive_buffers(controller);
    }
    if (active)
        sim_clock_coschedule (uptr, tmxr_poll);     /* reactivate */
    else
    {
        for (dmc=0; dmc < mp->lines; dmc++)
        {
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

void dmc_buffer_trace_line(int tracelevel, CTLR *controller, const uint8 *buf, int length, char *prefix)
{
    char hex[TRACE_BYTES_PER_LINE*3+1];
    char ascii[TRACE_BYTES_PER_LINE+1];
    int i;
    hex[0] = 0;
    ascii[TRACE_BYTES_PER_LINE] = 0;

    for (i = 0; i<TRACE_BYTES_PER_LINE; i++)
    {
        if (i>=length)
        {
            strcat(hex, "   ");
            ascii[i] = ' ';
        }
        else
        {
            char hexByte[4];
            sprintf(hexByte, "%02X ", buf[i]);
            strcat(hex, hexByte);
            if (isprint(buf[i]))
            {
                ascii[i] = (char)buf[i];
            }
            else
            {
                ascii[i] = '.';
            }
        }
    }

    sim_debug(tracelevel, controller->device, "%s %s  %s\n", prefix, hex, ascii);
}

void dmc_buffer_trace(CTLR *controller, const uint8 *buf, int length, char *prefix, uint32 address)
{
    int i;
    if (length >= 0 && controller->device->dctrl & DBG_DAT)
    {
        sim_debug(DBG_DAT, controller->device, "%s Buffer address 0x%08x (%d bytes)\n", prefix, address, length);
        for(i = 0; i < length / TRACE_BYTES_PER_LINE; i++)
        {
            dmc_buffer_trace_line(DBG_DAT, controller, buf + i*TRACE_BYTES_PER_LINE, TRACE_BYTES_PER_LINE, prefix);
        }

        if (length %TRACE_BYTES_PER_LINE > 0)
        {
            dmc_buffer_trace_line(DBG_DAT, controller, buf + length/TRACE_BYTES_PER_LINE, length % TRACE_BYTES_PER_LINE, prefix);
        }
    }
    else if (length >= 0 && controller->device->dctrl & DBG_DTS)
    {
        char prefix2[80];
        sprintf(prefix2, "%s (len=%d)", prefix, length);
        dmc_buffer_trace_line(DBG_DTS, controller, buf, (length > TRACE_BYTES_PER_LINE)? TRACE_BYTES_PER_LINE : length, prefix2);
    }
}

void dmc_buffer_queue_init(CTLR *controller, BUFFER_QUEUE *q, char *name)
{
    q->name = name;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->controller = controller;
}

void dmc_buffer_queue_init_all(CTLR *controller)
{
    dmc_buffer_queue_init(controller, controller->receive_queue, "receive");
    dmc_buffer_queue_init(controller, controller->transmit_queue, "transmit");
}

int dmc_buffer_queue_full(BUFFER_QUEUE *q)
{
    return q->count > BUFFER_QUEUE_SIZE;
}

void dmc_buffer_queue_add(BUFFER_QUEUE *q, uint32 address, uint16 count)
{
    if (!dmc_buffer_queue_full(q))
    {
        int new_buffer = 0;
        if (q->count > 0)
        {
            int last_buffer = q->tail;
            new_buffer = (q->tail +1) % BUFFER_QUEUE_SIZE;

            /* Link last buffer to the new buffer */
            q->queue[last_buffer].next = &q->queue[new_buffer];
        }
        else
        {
            q->head = 0;
            new_buffer = 0;
        }

        q->tail = new_buffer;
        q->queue[new_buffer].address = address;
        q->queue[new_buffer].count = count;
        q->queue[new_buffer].transfer_buffer = NULL;
        q->queue[new_buffer].actual_bytes_transferred = 0;
        q->queue[new_buffer].next = NULL;
        q->queue[new_buffer].state = Available;
        q->queue[new_buffer].is_loopback = dmc_is_lu_loop_set(q->controller);
        q->count++;
        sim_debug(DBG_INF, q->controller->device, "Queued %s buffer address=0x%08x count=%d\n", q->name, address, count);
    }
    else
    {
        sim_debug(DBG_WRN, q->controller->device, "Failed to queue %s buffer address=0x%08x, queue full\n", q->name, address);
        // TODO: Report error here.
    }
}

void dmc_buffer_queue_release_head(BUFFER_QUEUE *q)
{
    if (q->count > 0)
    {
        free(q->queue[q->head].transfer_buffer);
        q->queue[q->head].transfer_buffer = NULL;
        q->queue[q->head].state = Available;
        q->head = (q->head + 1) % BUFFER_QUEUE_SIZE;
        q->count--;
    }
    else
    {
        sim_debug(DBG_INF, q->controller->device, "Failed to release %s buffer, queue already empty\n", q->name);
    }
}

BUFFER *dmc_buffer_queue_head(BUFFER_QUEUE *q)
{
    BUFFER *ans = NULL;
    if (q->count >0)
    {
        ans = &q->queue[q->head];
    }

    return ans;
}

BUFFER *dmc_buffer_queue_find_first_available(BUFFER_QUEUE *q)
{
    BUFFER *ans = dmc_buffer_queue_head(q);
    while (ans != NULL)
    {
        if (ans->state == Available)
        {
            break;
        }

        ans = ans->next;
    }

    return ans;
}

BUFFER *dmc_buffer_queue_find_first_contains_data(BUFFER_QUEUE *q)
{
    BUFFER *ans = dmc_buffer_queue_head(q);
    while (ans != NULL)
    {
        if (ans->state == ContainsData)
        {
            break;
        }

        ans = ans->next;
    }

    return ans;
}

void dmc_queue_control_out(CTLR *controller, uint16 sel6)
{
    CONTROL_OUT *control = calloc(1, sizeof(*control));
    CONTROL_OUT *last = NULL;

    control->sel6 = sel6;
    if (controller->control_out)
    {
        last = controller->control_out;
        while (last->next)
        {
            last = last->next;
        }
        last->next = control;
    }
    else
        controller->control_out = control;
}

/* returns true if some data was received */
t_bool dmc_buffer_fill_receive_buffers(CTLR *controller)
{
    int ans = FALSE;

    if (controller->state == Running)
    {
        BUFFER *buffer = dmc_buffer_queue_find_first_available(controller->receive_queue);

        while ((buffer) && (buffer->state == Available))
        {
            const uint8 *pbuf;
            size_t size;

            tmxr_get_packet_ln (controller->line, &pbuf, &size);
            if (!pbuf)
                break;
            buffer->actual_bytes_transferred = size;
            controller->buffers_received_from_net++;
            dmc_buffer_trace (controller, pbuf, buffer->actual_bytes_transferred, "REC ", buffer->address);
            buffer->is_loopback = FALSE;
            buffer->state = ContainsData;
            Map_WriteB (buffer->address, buffer->actual_bytes_transferred, (uint8 *)pbuf);
            ans = TRUE;
            buffer = buffer->next;
        }
    }
    return ans;
}

/* returns true if some data was actually sent */
int dmc_buffer_send_transmit_buffers(CTLR *controller)
{
    int ans = FALSE;

    /* when transmit buffer is queued it is marked as available, not as ContainsData */
    BUFFER *buffer = dmc_buffer_queue_find_first_available(controller->transmit_queue);
    while ((buffer != NULL) && (buffer->state == Available))
    {
        t_stat r;

        /* only send the buffer if it actually has some data, sometimes get zero length buffers - don't send these */
        if (buffer->count > 0)
        {
            if (buffer->transfer_buffer == NULL)
            {
                int n;
                /* construct buffer */
                buffer->transfer_buffer = (uint8 *)malloc (buffer->count);
                n = Map_ReadB (buffer->address, buffer->count, buffer->transfer_buffer);
                if (n > 0)
                {
                    sim_debug(DBG_WRN, controller->device, "DMA error\n");
                }
            }

            r = tmxr_put_packet_ln (controller->line, buffer->transfer_buffer, buffer->count);
            if (r == SCPE_OK)
            {
                buffer->actual_bytes_transferred = buffer->count;
                dmc_buffer_trace (controller, buffer->transfer_buffer, buffer->count, "TRAN", buffer->address);
                free (buffer->transfer_buffer);
                buffer->transfer_buffer = NULL;
                controller->buffers_transmitted_to_net++;
                buffer->state = ContainsData; // so won't try to transmit again
                ans = TRUE;
                if (controller->byte_wait)
                {
                    buffer->buffer_return_time = sim_grtime() + controller->byte_wait*buffer->count;
                    sim_activate_notbefore(controller->unit, buffer->buffer_return_time);
                }
            }
            else
                break; /* poll again later to send more bytes */
        }
        else
            buffer->state = ContainsData; // so won't try to transmit again


        if (controller->byte_wait)
            break; /* Pause until service routine completes buffer return */
        buffer = buffer->next;
    }

    if (ans && (!controller->byte_wait))
        dmc_svc (controller->unit);
    return ans;
}

void dmc_start_transfer_receive_buffer(CTLR *controller)
{
    BUFFER *head = dmc_buffer_queue_head(controller->receive_queue);

    if ((!head) ||
        (controller->transfer_state != Idle) ||
        (dmc_is_rdyo_set(controller)))
        return;
    if (head->state == ContainsData)
    {
        head->state = TransferInProgress;
        dmc_start_data_output_transfer(controller, head->address, head->actual_bytes_transferred, TRUE);
    }
}

void dmc_start_transfer_transmit_buffer(CTLR *controller)
{
    BUFFER *head = dmc_buffer_queue_head(controller->transmit_queue);

    if ((!head) ||
        (controller->transfer_state != Idle) ||
        (dmc_is_rdyo_set(controller)))
        return;
    if (head->state == ContainsData)
    {
        head->state = TransferInProgress;
        dmc_start_data_output_transfer(controller, head->address, head->count, FALSE);
    }
}

void dmc_check_for_output_transfer_completion(CTLR *controller)
{
    if ((dmc_is_rdyo_set(controller)) ||
        ((controller->transfer_state != OutputTransferReceiveBuffer) &&
         (controller->transfer_state != OutputTransferTransmitBuffer)))
        return;
    sim_debug(DBG_INF, controller->device, "Output transfer completed\n");
    dmc_buffer_queue_release_head((controller->transfer_state == OutputTransferReceiveBuffer) ? controller->receive_queue : controller->transmit_queue);
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
    if (dmc_is_dmc(controller))
    {
        if (!dmc_is_rqi_set(controller))
        {
            uint16 sel4 = *controller->csrs->sel4;
            uint16 sel6 = *controller->csrs->sel6;
            controller->transfer_type = dmc_get_input_transfer_type(controller);
            dmc_clear_rdyi(controller);
            if (controller->transfer_type == TYPE_BASEI)
            {
                *controller->baseaddr = ((sel6 >> 14) << 16) | sel4;
                *controller->basesize = sel6 & 0x3FFF;
                sim_debug(DBG_INF, controller->device, "Completing Base In input transfer, base address=0x%08x count=%d\n", *controller->baseaddr, *controller->basesize);
            }
            else if (controller->transfer_type == TYPE_BACCI)
            {
                uint32 addr = ((sel6 >> 14) << 16) | sel4;
                uint16 count = sel6 & 0x3FFF;
                controller->transfer_in_io = dmc_is_in_io_set(controller);
                if (controller->state != Running)
                {
                    controller->state = Running;
                    dmc_set_modem_dtr (controller);
                }
                if (controller->transfer_in_io)
                {
                    dmc_buffer_queue_add(controller->receive_queue, addr, count);
                    dmc_buffer_fill_receive_buffers(controller);
                    controller->receive_buffer_input_transfers_completed++;
                }
                else
                {
                    dmc_buffer_queue_add(controller->transmit_queue, addr, count);
                    dmc_buffer_send_transmit_buffers(controller);
                    controller->transmit_buffer_input_transfers_completed++;
                }
            }
            else if (controller->transfer_type == TYPE_HALT)
            {
                sim_debug(DBG_INF, controller->device, "Halt Command Received\n");
                controller->state = Halted;
                dmc_clr_modem_dtr(controller);
                dmc_queue_control_out(controller, HALT_COMP_MASK);
                return;
            }
            controller->transfer_state = Idle;
            dmc_process_command (controller);
        }
    }
    else    /* DMP */
    {
        if (!dmc_is_rdyi_set(controller))
        {
            uint16 sel6 = *controller->csrs->sel6;
            if (controller->transfer_type == TYPE_DMP_MODE)
            {
                uint16 mode = sel6 & DMP_TYPE_INPUT_MASK;
                char * duplex = (mode & 1) ? "Full-Duplex" : "Half-Duplex";
                char * config;
                if (mode & 4)
                {
                    config = "Point-to-point";
                }
                else
                {
                    config = (mode & 2) ? "Tributary station" : "Control Station";
                }

                sim_debug(DBG_INF, controller->device, "Completing Mode input transfer, %s %s\n", duplex, config);
            }
            else if (controller->transfer_type == TYPE_DMP_CONTROL)
            {
                sim_debug(DBG_WRN, controller->device, "Control command (not processed yet)\n");
            }
            else if (controller->transfer_type == TYPE_DMP_RECEIVE)
            {
                sim_debug(DBG_WRN, controller->device, "Receive Buffer command (not processed yet)\n");
            }
            else if (controller->transfer_type == TYPE_DMP_TRANSMIT)
            {
                sim_debug(DBG_WRN, controller->device, "Transmit Buffer command (not processed yet)\n");
            }
            else
            {
                sim_debug(DBG_WRN, controller->device, "Unrecognised command code %hu\n", controller->transfer_type);
            }

            controller->transfer_state = Idle;
        }
    }
}

void dmc_process_command(CTLR *controller)
{
    if (dmc_is_master_clear_set(controller))
    {
        dmc_process_master_clear(controller);
        return;
    }
    if (controller->transfer_state == InputTransfer)
    {
        dmc_process_input_transfer_completion(controller);
        return;
    }
    if ((controller->transfer_state == OutputTransferReceiveBuffer) ||
        (controller->transfer_state == OutputTransferTransmitBuffer))
    {
        dmc_check_for_output_transfer_completion(controller);
        return;
    }
    if (controller->transfer_state == OutputControl)
    {
        dmc_check_for_output_control_completion(controller);
        return;
    }
    /* transfer_state Idle */
    if (dmc_is_rqi_set(controller))
    {
        dmc_start_input_transfer(controller);
        return;
    }
    if (dmc_is_dmc (controller) &&
        *controller->csrs->sel0 & ROMI_MASK &&
        *controller->csrs->sel6 == DSPDSR)
    /* DMC-11 or DMR-11, see if ROMI bit is set.  If so, if SEL6 is
        0x22b3 (read line status instruction), set the DTR bit in SEL2.  */
    {
        dmc_setreg (controller, 2, 0x800);
    }
    else
    {
        dmc_start_control_output_transfer(controller);
        dmc_start_transfer_transmit_buffer(controller);
        dmc_start_transfer_receive_buffer(controller);
    }
}

t_stat dmc_rd(int32 *data, int32 PA, int32 access)
{
    CTLR *controller = dmc_get_controller_from_address(PA);
    sim_debug(DBG_TRC, controller->device, "dmc_rd(), addr=0x%x access=%d\n", PA, access);
    *data = dmc_getreg(controller, PA);

    return SCPE_OK;
}

t_stat dmc_wr(int32 data, int32 PA, int32 access)
{
    CTLR *controller = dmc_get_controller_from_address(PA);
    int reg = PA & ((UNIBUS) ? 07 : 017);
    uint16 oldValue = dmc_getreg(controller, PA);

    if (access == WRITE)
    {
        sim_debug(DBG_TRC, controller->device, "dmc_wr(), addr=0x%08x, SEL%d, data=0x%04x\n", PA, reg, data);
    }
    else
    {
        sim_debug(DBG_TRC, controller->device, "dmc_wr(), addr=0x%08x, BSEL%d, data=%02x\n", PA, reg, data);
    }

    if (access == WRITE)
    {
        if (PA & 1)
        {
            sim_debug(DBG_WRN, controller->device, "dmc_wr(), Unexpected non-16-bit write access to SEL%d\n", reg);
        }
        dmc_setreg(controller, PA, data);
    }
    else
    {
        uint16 mask;
        if (PA & 1)
        {
            mask = 0xFF00;
            data = data << 8;
        }
        else
        {
            mask = 0x00FF;
        }

        dmc_setreg(controller, PA, (oldValue & ~mask) | (data & mask));
    }

    if (dmc_is_attached(controller->unit) && (dmc_getsel(reg) == 0 || dmc_getsel(reg) == 1))
    {
        dmc_process_command(controller);
    }

    return SCPE_OK;
}

int32 dmc_ininta (void)
{
    int i;
    int32 ans = 0; /* no interrupt request active */
    for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++)
    {
        CTLR *controller = &dmc_ctrls[i];
        if (controller->in_int != 0)
        {
            DIB *dib = (DIB *)controller->device->ctxt;
            ans = dib->vec + (8 * (int)(controller->unit - controller->device->units));
            dmc_clrinint(controller);
            sim_debug(DBG_INT, controller->device, "RXINTA Device %d - Vector: 0x%x\n", (int)(controller->unit-controller->device->units), ans);
            break;
        }
    }

    return ans;
    }

int32 dmc_outinta (void)
{
    int i;
    int32 ans = 0; /* no interrupt request active */
    for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++)
    {
        CTLR *controller = &dmc_ctrls[i];
        if (controller->out_int != 0)
        {
            DIB *dib = (DIB *)controller->device->ctxt;
            ans = dib->vec + 4 + (8 * (int)(controller->unit - controller->device->units));
            dmc_clroutint(controller);
            sim_debug(DBG_INT, controller->device, "TXINTA Device %d - Vector: 0x%x\n", (int)(controller->unit-controller->device->units), ans);
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

    /* Connect structures together */
    for (i=0; i < DMC_NUMDEVICE; i++)
    {
        dmc_csrs[i].sel0 = &dmc_sel0[i];
        dmc_csrs[i].sel2 = &dmc_sel2[i];
        dmc_csrs[i].sel4 = &dmc_sel4[i];
        dmc_csrs[i].sel6 = &dmc_sel6[i];
        controller = &dmc_ctrls[i];
        controller->csrs = &dmc_csrs[i];
        controller->line = &dmc_desc.ldsc[i];
        controller->receive_queue = &dmc_receive_queues[i];
        controller->transmit_queue = &dmc_transmit_queues[i];
        controller->device = &dmc_dev;
        controller->baseaddr = &dmc_baseaddr[i];
        controller->basesize = &dmc_basesize[i];
        controller->modem = &dmc_modem[i];
        controller->unit = &controller->device->units[i];
        controller->index = i;
    }
    for (i=0; i < DMP_NUMDEVICE; i++)
    {
        dmp_csrs[i].sel0 = &dmp_sel0[i];
        dmp_csrs[i].sel2 = &dmp_sel2[i];
        dmp_csrs[i].sel4 = &dmp_sel4[i];
        dmp_csrs[i].sel6 = &dmp_sel6[i];
        dmp_csrs[i].sel10 = &dmp_sel10[i];
        controller = &dmc_ctrls[i+DMC_NUMDEVICE];
        controller->csrs = &dmp_csrs[i];
        controller->line = &dmp_desc.ldsc[i];
        controller->receive_queue = &dmp_receive_queues[i];
        controller->transmit_queue = &dmp_transmit_queues[i];
        controller->device = (UNIBUS) ? &dmp_dev : &dmv_dev;
        controller->dev_type = DMP;
        controller->baseaddr = &dmp_baseaddr[i];
        controller->basesize = &dmp_basesize[i];
        controller->modem = &dmp_modem[i];
        controller->unit = &controller->device->units[i];
        controller->unit->ctlr = (void *)controller;
        controller->index = i + DMC_NUMDEVICE;
    }
    if (0 == dmc_units[0].flags)         /* First Time Initializations */
    {
        for (i=0; i < DMC_NUMDEVICE; i++)
        {
            controller = &dmc_ctrls[i];
            controller->state = Uninitialised;
            controller->transfer_state = Idle;
            controller->control_out = NULL;
            *controller->modem = 0;
            controller->dev_type = DMC;
            dmc_dev.units[i] = dmc_unit_template;
            controller->unit->ctlr = (void *)controller;
        }
        tmxr_set_modem_control_passthru (&dmc_desc);   /* We always want Modem Control */
        dmc_units[dmc_dev.numunits-1] = dmc_poll_unit_template;
        dmc_units[dmc_dev.numunits-1].ctlr = dmc_units[0].ctlr;
        dmc_desc.notelnet = TRUE;                      /* We always want raw tcp socket */
        dmc_desc.dptr = &dmc_dev;                      /* Connect appropriate device */
        dmc_desc.uptr = dmc_units+dmc_desc.lines;      /* Identify polling unit */
        for (i=0; i < DMP_NUMDEVICE; i++)
        {
            controller = &dmc_ctrls[i+DMC_NUMDEVICE];
            controller->state = Uninitialised;
            controller->transfer_state = Idle;
            controller->control_out = NULL;
            *controller->modem = 0;
            dmp_dev.units[i] = dmc_unit_template;
            controller->unit->ctlr = (void *)controller;
        }
        tmxr_set_modem_control_passthru (&dmp_desc);   /* We always want Modem Control */
        dmp_units[dmp_dev.numunits-1] = dmc_poll_unit_template;
        dmp_units[dmp_dev.numunits-1].ctlr = dmp_units[0].ctlr;
        dmp_desc.notelnet = TRUE;                      /* We always want raw tcp socket */
        dmp_desc.dptr = &dmp_dev;                      /* Connect appropriate device */
        dmp_desc.uptr = dmp_units+dmp_desc.lines;      /* Identify polling unit */
    }

    ans = auto_config (dptr->name, (dptr->flags & DEV_DIS) ? 0 : dptr->numunits - 1);

    if (!(dptr->flags & DEV_DIS))
    {
        for (i = 0; i < DMC_NUMDEVICE + DMP_NUMDEVICE; i++)
        {
            if (dmc_ctrls[i].device == dptr)
            {
                controller = &dmc_ctrls[i];
                dmc_buffer_queue_init_all(controller);
                dmc_clrinint(controller);
                dmc_clroutint(controller);
                for (j=0; j<dptr->numunits-1; j++)
                    sim_cancel (&dptr->units[j]); /* stop poll */
            }
        }
        sim_activate_after (dptr->units+dptr->numunits-1, DMC_CONNECT_POLL*1000000);/* start poll */
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
    if (!peer[0])
    {
        printf ("Peer must be specified before attach\n");
        if (sim_log)
            fprintf (sim_log, "Peer must be specified before attach\n");
        return SCPE_ARG;
    }
    sprintf (attach_string, "Line=%d,Buffered=16384,Connect=%s,%s", dmc, peer, cptr);
    ans = tmxr_open_master (mp, attach_string);                 /* open master socket */
    if (ans != SCPE_OK)
        return ans;
    strncpy (port, cptr, CBUFSIZE-1);
    uptr->filename = (char *)malloc (strlen(port)+1);
    strcpy (uptr->filename, port);
    uptr->flags |= UNIT_ATT;
    sim_activate_after (dptr->units+mp->lines, DMC_CONNECT_POLL*1000000);/* start poll */
    return ans;
}

t_stat dmc_detach (UNIT *uptr)
{
    DEVICE *dptr = (UNIBUS) ? ((&dmc_dev == find_dev_from_unit(uptr)) ? &dmc_dev : &dmp_dev) : &dmv_dev;
    int32 dmc = (int32)(uptr-dptr->units);
    TMXR *mp = (dptr == &dmc_dev) ? &dmc_desc : &dmp_desc;
    TMLN *lp = &mp->ldsc[dmc];
    int32 i, attached;

    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    sim_cancel (uptr);
    uptr->flags &= ~UNIT_ATT;
    for (i=attached=0; i<mp->lines; i++)
        if (dptr->units[i].flags & UNIT_ATT)
            ++attached;
    if (!attached)
        sim_cancel (dptr->units+mp->lines);              /* stop poll on last detach */
    free (uptr->filename);
    uptr->filename = NULL;
    return tmxr_detach_ln (lp);
}

char *dmc_description (DEVICE *dptr)
    {
    return "DMC11 Synchronous network controller";
    }

char *dmp_description (DEVICE *dptr)
    {
    return (UNIBUS) ? "DMP11 Synchronous network controller"
                    : "DMV11 Synchronous network controller";
    }

