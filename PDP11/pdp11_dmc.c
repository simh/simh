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

                  Also added shadow CSRs. The code was using the CSRs to check
                  the command being executed, but the driver could end up
                  changing the bits, so a shadow set is used to do this.
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

// TODO: Avoid need for manifests and newest runtime, compile with 2003
// TODO: Investigate line number and set parameters at the unit level (?)
// TODO: Multipoint. In this case perhaps don't need transmit port, allow all lines to connect to port on control node.
// TODO: Show active connections like DZ does, for multipoint.
// TODO: Test MOP.
// TODO: Implement actual DDCMP protocol and run over UDP.
// TODO: Allow NCP SHOW COUNTERS to work (think this is the base address thing). Since fixing how I get the addresses this should work now.

#include <time.h>
#include <ctype.h>

#include "pdp11_dmc.h"

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
    Initialised, /* after MASTER CLEAR */
    Running      /* after any transmit or receive buffer has been supplied */
} ControllerState;

typedef enum
{ 
    Idle,
    InputTransfer,
    OutputTransfer
} TransferState;

typedef enum
{
    Available, /* when empty or partially filled on read */
    ContainsData,
    TransferInProgress
} BufferState;

typedef struct
{
    int32 isPrimary;
    SOCKET socket; // socket used bidirectionally
    int receive_readable;
    char *receive_port;
    int transmit_writeable;
    char peer[CBUFSIZE];
    int transmit_is_loopback; /* if true the transmit socket is the loopback to the receive */
    int32 speed; /* bits per second in each direction, 0 for no limit */
    int last_second;
    int bytes_sent_in_last_second;
    int bytes_received_in_last_second;
    time_t last_connect_attempt;
} LINE;

/* A partially filled buffer (during a read from the socket) will have block_len_bytes_read = 1 or actual_bytes_transferred < actual_block_len */
typedef struct buffer
{
    uint32 address;           /* unibus address of the buffer */
    uint16 count;             /* size of the buffer passed to the device by the driver */
    uint16 actual_block_len;  /* actual length of the received block */
    uint8 *transfer_buffer;   /* the buffer into which data is received or from which it is transmitted*/
    int block_len_bytes_read; /* the number of bytes read so far for the block length */
    int actual_bytes_transferred;    /* the number of bytes from the actual block that have been read or written so far*/
    struct buffer *next;      /* next buffer in the queue */
    BufferState state;        /* state of this buffer */
    int is_loopback;          /* loopback was requested when this buffer was queued */
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

typedef struct
{
    int started;
    clock_t start_time;
    clock_t cumulative_time;
} TIMER;

typedef struct
{
    TIMER between_polls_timer;
    TIMER poll_timer;
    uint32 poll_count;

} UNIT_STATS;

typedef enum
{
    DMC,
    DMR,
    DMP
} DEVTYPE;

struct dmc_controller {
    CSRS *csrs;
    CSRS *shadow_csrs;
    DEVICE *device;
    UNIT *unit;
    int index;                  /* Index in controller array */
    ControllerState state;
    TransferState transfer_state; /* current transfer state (type of transfer) */
    int transfer_type;
    int transfer_in_io; // remembers IN I/O setting at start of input transfer as host changes it during transfer!
    LINE *line;
    BUFFER_QUEUE *receive_queue;
    BUFFER_QUEUE *transmit_queue;
    UNIT_STATS *stats;
    SOCKET master_socket;
    int32 connect_poll_interval;
    DEVTYPE dev_type;
    uint32 rxi;
    uint32 txi;
    uint32 buffers_received_from_net;
    uint32 buffers_transmitted_to_net;
    uint32 receive_buffer_output_transfers_completed;
    uint32 transmit_buffer_output_transfers_completed;
    uint32 receive_buffer_input_transfers_completed;
    uint32 transmit_buffer_input_transfers_completed;
};

typedef struct dmc_controller CTLR;

t_stat dmc_rd(int32* data, int32 PA, int32 access);
t_stat dmc_wr(int32  data, int32 PA, int32 access);
t_stat dmc_svc(UNIT * uptr);
t_stat dmc_reset (DEVICE * dptr);
t_stat dmc_attach (UNIT * uptr, char * cptr);
t_stat dmc_detach (UNIT * uptr);
int32 dmc_rxint (void);
int32 dmc_txint (void);
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
t_stat dmc_setconnectpoll (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showconnectpoll (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat dmc_setlinemode (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat dmc_showlinemode (FILE* st, UNIT* uptr, int32 val, void* desc);
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
void dmc_process_command(CTLR *controller);
int dmc_buffer_fill_receive_buffers(CTLR *controller);
void dmc_start_transfer_receive_buffer(CTLR *controller);
int dmc_buffer_send_transmit_buffers(CTLR *controller);
void dmc_buffer_queue_init(CTLR *controller, BUFFER_QUEUE *q, char *name);
void dmc_buffer_queue_init_all(CTLR *controller);
BUFFER *dmc_buffer_queue_head(BUFFER_QUEUE *q);
int dmc_buffer_queue_full(BUFFER_QUEUE *q);
void dmc_buffer_queue_get_stats(BUFFER_QUEUE *q, int *available, int *contains_data, int *transfer_in_progress);
void dmc_start_transfer_transmit_buffer(CTLR *controller);
void dmc_error_and_close_socket(CTLR *controller, char *format);
void dmc_close_socket(CTLR *controller, char *reason);
void dmc_close_receive(CTLR *controller, char *reason, char *from);
void dmc_close_transmit(CTLR *controller, char *reason);
int dmc_get_socket(CTLR *controller, int forRead);
int dmc_get_receive_socket(CTLR *controller, int forRead);
int dmc_get_transmit_socket(CTLR *controller, int is_loopback, int forRead);
void dmc_line_update_speed_stats(LINE *line);

DEBTAB dmc_debug[] = {
    {"TRACE",  DBG_TRC},
    {"WARN",   DBG_WRN},
    {"REG",    DBG_REG},
    {"INFO",   DBG_INF},
    {"DATA",   DBG_DAT},
    {"DATASUM",DBG_DTS},
    {"SOCKET", DBG_SOK},
    {"CONNECT", DBG_CON},
    {0}
};

UNIT dmc_units[DMC_NUMDEVICE];

UNIT dmc_unit_template = { UDATA (&dmc_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE, 0) };

UNIT dmp_units[DMP_NUMDEVICE];

CSRS dmc_csrs[DMC_NUMDEVICE];
uint16 dmc_sel0[DMC_NUMDEVICE];
uint16 dmc_sel2[DMC_NUMDEVICE];
uint16 dmc_sel4[DMC_NUMDEVICE];
uint16 dmc_sel6[DMC_NUMDEVICE];
CSRS dmc_shadow_csrs[DMC_NUMDEVICE];
uint16 dmc_shadow_sel0[DMC_NUMDEVICE];
uint16 dmc_shadow_sel2[DMC_NUMDEVICE];
uint16 dmc_shadow_sel4[DMC_NUMDEVICE];
uint16 dmc_shadow_sel6[DMC_NUMDEVICE];

CSRS dmp_csrs[DMP_NUMDEVICE];
uint16 dmp_sel0[DMC_NUMDEVICE];
uint16 dmp_sel2[DMC_NUMDEVICE];
uint16 dmp_sel4[DMC_NUMDEVICE];
uint16 dmp_sel6[DMC_NUMDEVICE];
uint16 dmp_sel10[DMC_NUMDEVICE];
CSRS dmp_shadow_csrs[DMP_NUMDEVICE];
uint16 dmp_shadow_sel0[DMC_NUMDEVICE];
uint16 dmp_shadow_sel2[DMC_NUMDEVICE];
uint16 dmp_shadow_sel4[DMC_NUMDEVICE];
uint16 dmp_shadow_sel6[DMC_NUMDEVICE];
uint16 dmp_shadow_sel10[DMC_NUMDEVICE];

LINE dmc_line[DMC_NUMDEVICE];

LINE dmc_line_template = 
    { 0, INVALID_SOCKET };

uint32 dmc_rxi_summary = 0;         /* Receive Interrupt Summary for all controllers */
uint32 dmc_txi_summary = 0;         /* Transmit Interrupt Summary for all controllers */

BUFFER_QUEUE dmc_receive_queues[DMC_NUMDEVICE];
BUFFER_QUEUE dmc_transmit_queues[DMC_NUMDEVICE];

LINE dmp_line[DMP_NUMDEVICE];

BUFFER_QUEUE dmp_receive_queues[DMP_NUMDEVICE];
BUFFER_QUEUE dmp_transmit_queues[DMP_NUMDEVICE];

UNIT_STATS dmc_stats[DMC_NUMDEVICE];
UNIT_STATS dmp_stats[DMP_NUMDEVICE];

REG dmc_reg[] = {
    { BRDATA (SEL0, dmc_sel0, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL2, dmc_sel2, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL4, dmc_sel4, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL6, dmc_sel6, DEV_RDX, 16, DMC_NUMDEVICE) },
    { GRDATA (RXINT, dmc_rxi_summary, DEV_RDX, 32, 0) },
    { GRDATA (TXINT, dmc_txi_summary, DEV_RDX, 32, 0) },
    { BRDATA (SHADOWSEL0, dmc_shadow_sel0, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL2, dmc_shadow_sel2, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL4, dmc_shadow_sel4, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL6, dmc_shadow_sel6, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (LINES, dmc_line, 16, 8, sizeof(dmc_line)), REG_HRO},
    { NULL }  };

REG dmp_reg[] = {
    { BRDATA (SEL0, dmp_sel0, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL2, dmp_sel2, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL4, dmp_sel4, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SEL6, dmp_sel6, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL0, dmp_shadow_sel0, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL2, dmp_shadow_sel2, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL4, dmp_shadow_sel4, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (SHADOWSEL6, dmp_shadow_sel6, DEV_RDX, 16, DMC_NUMDEVICE) },
    { BRDATA (LINES, dmp_line, 16, 8, sizeof(dmp_line)), REG_HRO},
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
    { MTAB_XTD|MTAB_VUN,          0, "LINEMODE", "LINEMODE={PRIMARY|SECONDARY}",
        &dmc_setlinemode, &dmc_showlinemode, NULL, "Display the connection orientation" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
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
    { MTAB_XTD|MTAB_VUN,          0, "LINEMODE", "LINEMODE={PRIMARY|SECONDARY}",
        &dmc_setlinemode, &dmc_showlinemode, NULL, "Display the connection orientation" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
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
    { MTAB_XTD|MTAB_VUN,          0, "LINEMODE", "LINEMODE={PRIMARY|SECONDARY}",
        &dmc_setlinemode, &dmc_showlinemode, NULL, "Display the connection orientation" },
    { MTAB_XTD|MTAB_VUN|MTAB_NMO, 0, "STATS", "STATS",
        &dmc_setstats, &dmc_showstats, NULL, "Display statistics" },
    { MTAB_XTD|MTAB_VUN,          0, "CONNECTPOLL", "CONNECTPOLL=seconds",
        &dmc_setconnectpoll, &dmc_showconnectpoll, NULL, "Display connection poll interval" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,        020, "ADDRESS", "ADDRESS",
        &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,          1, "VECTOR", "VECTOR",
        &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 },
};

#define IOLN_DMC        010

DIB dmc_dib = { IOBA_AUTO, IOLN_DMC, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_rxint, &dmc_txint} };

#define IOLN_DMP        010

DIB dmp_dib = { IOBA_AUTO, IOLN_DMP, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_rxint, &dmc_txint }};

#define IOLN_DMV        020

DIB dmv_dib = { IOBA_AUTO, IOLN_DMV, &dmc_rd, &dmc_wr, 2, IVCL (DMCRX), VEC_AUTO, {&dmc_rxint, &dmc_txint }};

DEVICE dmc_dev =
    { "DMC", dmc_units, dmc_reg, dmc_mod, 1, DMC_RDX, 8, 1, DMC_RDX, 8,
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
    { "DMP", dmp_units, dmp_reg, dmp_mod, 1, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmp_description };

DEVICE dmv_dev =
    { "DMV", dmp_units, dmp_reg, dmv_mod, 1, DMC_RDX, 8, 1, DMC_RDX, 8,
    NULL, NULL, &dmc_reset, NULL, &dmc_attach, &dmc_detach,
    &dmp_dib, DEV_DISABLE | DEV_DIS | DEV_QBUS | DEV_DEBUG, 0, dmc_debug,
    NULL, NULL, &dmc_help, &dmc_help_attach, NULL, &dmp_description };

CTLR dmc_ctrls[DMC_NUMDEVICE + DMP_NUMDEVICE];

void dmc_reset_unit_stats(UNIT_STATS *s)
{
    s->between_polls_timer.started = FALSE;
    s->poll_timer.started = FALSE;
    s->poll_count = 0;
}

int dmc_timer_started(TIMER *t)
{
    return t->started;
}

void dmc_timer_start(TIMER *t)
{
    t->start_time = clock();
    t->cumulative_time = 0;
    t->started = TRUE;
}

void dmc_timer_stop(TIMER *t)
{
    clock_t end_time = clock();
    t->cumulative_time += end_time - t->start_time;
}

void dmc_timer_resume(TIMER *t)
{
    t->start_time = clock();
}

double dmc_timer_cumulative_seconds(TIMER *t)
{
    return (double)t->cumulative_time/CLOCKS_PER_SEC;
}

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
    int i;
    CTLR *ans = NULL;
    for (i = 0; i < DMC_NUMDEVICE + DMP_NUMDEVICE; i++)
    {
        if (dmc_ctrls[i].unit == unit)
        {
            ans = &dmc_ctrls[i];
            break;
        }
    }

    return ans;
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
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    if (controller->line->peer[0])
    {
        fprintf(st, "peer=%s", controller->line->peer);
    }
    else
    {
        fprintf(st, "peer=unspecified");
    }

    return SCPE_OK;
}

t_stat dmc_setpeer (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    char host[CBUFSIZE], port[CBUFSIZE];
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    if (!cptr) return SCPE_IERR;
    if (dmc_is_attached(uptr)) return SCPE_ALATT;
    status = sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL);
    if (status != SCPE_OK)
        return status;
    if (host[0] == '\0')
        return SCPE_ARG;
    strncpy(controller->line->peer, cptr, sizeof(controller->line->peer)-1);

    return status;
}

t_stat dmc_showspeed (FILE* st, UNIT* uptr, int32 val, void* desc)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    if (controller->line->speed > 0)
    {
        fprintf(st, "speed=%d bits/sec", controller->line->speed);
    }
    else
    {
        fprintf(st, "speed=0 (unrestricted)");
    }

    return SCPE_OK;
}

t_stat dmc_setspeed (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    if (!cptr) return SCPE_IERR;
    if (dmc_is_attached(uptr)) return SCPE_ALATT;
    if (sscanf(cptr, "%d", &controller->line->speed) != 1)
    {
        status = SCPE_ARG;
    }

    return status;
}

t_stat dmc_showtype (FILE* st, UNIT* uptr, int32 val, void* desc)
{
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

    if (!cptr) return SCPE_2FARG;
    if (dmc_is_attached(uptr)) return SCPE_ALATT;
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
    TIMER *poll_timer = &controller->stats->poll_timer;
    TIMER *between_polls_timer = &controller->stats->between_polls_timer;
    uint32 poll_count = controller->stats->poll_count;

    if (dmc_timer_started(between_polls_timer) && poll_count > 0)
    {
        fprintf(st, "Average time between polls=%f (sec)\n", dmc_timer_cumulative_seconds(between_polls_timer)/poll_count);
    }
    else
    {
        fprintf(st, "Average time between polls=n/a\n");
    }

    if (dmc_timer_started(poll_timer) && poll_count > 0)
    {
        fprintf(st, "Average time within poll=%f (sec)\n", dmc_timer_cumulative_seconds(poll_timer)/poll_count);
    }
    else
    {
        fprintf(st, "Average time within poll=n/a\n");
    }

    fprintf(st, "Buffers received from the network=%d\n", controller->buffers_received_from_net);
    fprintf(st, "Buffers sent to the network=%d\n", controller->buffers_transmitted_to_net);
    fprintf(st, "Output transfers completed for receive buffers=%d\n", controller->receive_buffer_output_transfers_completed);
    fprintf(st, "Output transfers completed for transmit buffers=%d\n", controller->transmit_buffer_output_transfers_completed);
    fprintf(st, "Input transfers completed for receive buffers=%d\n", controller->receive_buffer_input_transfers_completed);
    fprintf(st, "Input transfers completed for transmit buffers=%d\n", controller->transmit_buffer_input_transfers_completed);

    return SCPE_OK;
}

t_stat dmc_setstats (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    dmc_reset_unit_stats(controller->stats);

    controller->receive_buffer_output_transfers_completed = 0;
    controller->transmit_buffer_output_transfers_completed = 0;
    controller->receive_buffer_input_transfers_completed = 0;
    controller->transmit_buffer_input_transfers_completed = 0;

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

t_stat dmc_showlinemode (FILE* st, UNIT* uptr, int32 val, void* desc)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    fprintf(st, "linemode=%s", controller->line->isPrimary? "PRIMARY" : "SECONDARY");
    return SCPE_OK;
}

t_stat dmc_setlinemode (UNIT* uptr, int32 val, char* cptr, void* desc)
{
    t_stat status = SCPE_OK;
    CTLR *controller = dmc_get_controller_from_unit(uptr);

    if (!cptr) return SCPE_IERR;
    if (dmc_is_attached(uptr)) return SCPE_ALATT;

    if (MATCH_CMD(cptr, "PRIMARY") == 0)
    {
        controller->line->isPrimary = 1;
    }
    else if (MATCH_CMD(cptr, "SECONDARY") == 0)
    {
        controller->line->isPrimary = 0;
    }
    else
    {
        status = SCPE_ARG;
    }

    return status;
}

/* SET LINES processor */

t_stat dmc_setnumdevices (UNIT *uptr, int32 val, char *cptr, void *desc)
{
    int32 newln;
    uint32 i;
    t_stat r;
    DEVICE *dptr = (DEVICE *)desc;
    int maxunits = (&dmc_dev == dptr) ? DMC_NUMDEVICE : DMP_NUMDEVICE;
    DIB *dibptr = (DIB *)dptr->ctxt;
    int addrlnt = (UNIBUS) ? IOLN_DMC : IOLN_DMV;

    for (i=0; i<dptr->numunits; i++)
        if (dptr->units[i].flags&UNIT_ATT)
            return SCPE_ALATT;
    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, maxunits, &r);
    if ((r != SCPE_OK) || (newln == dptr->numunits))
        return r;
    if (newln == 0)
        return SCPE_ARG;
    dibptr->lnt = newln * addrlnt;                      /* set length */
    dptr->numunits = newln;
    return dmc_reset ((DEVICE *)desc);                  /* setup devices and auto config */
}

t_stat dmc_shownumdevices (FILE *st, UNIT *uptr, int32 val, void *desc)
{
    DEVICE *dptr = (UNIBUS) ? find_dev_from_unit (uptr) : &dmv_dev;

    fprintf (st, "lines=%d", dptr->numunits);
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
    fprintf(st, "The line mode of the two ends of a link must be set. One end must always\n");
    fprintf(st, "be primary and one end always secondary, setting both to primary or both\n");
    fprintf(st, "to secondary will not work. If there are firewall problems at one side,\n");
    fprintf(st, "set that side to be primary as the primary always initiates the TCP/IP\n");
    fprintf(st, "connection.\n");
    fprintf(st, "\n");
    fprintf(st, "   sim> SET %s0 LINEMODE= {PRIMARY|SECONDARY}\n", dptr->name);
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
    fprintf(st, "   sim> SET %s0 LINEMODE=PRIMARY\n", dptr->name);
    fprintf(st, "   sim> SET %s0 PEER=LOCALHOST:2222\n", dptr->name);
    fprintf(st, "   sim> ATTACH %s0 1111\n", dptr->name);
    fprintf(st, "\n");
    fprintf(st, "Machine 2\n");
    fprintf(st, "   sim> SET %s ENABLE\n", dptr->name);
    fprintf(st, "   sim> SET %s0 LINEMODE=SECONDARY\n", dptr->name);
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
    //tmxr_attach_help (st, dptr, uptr, flag, cptr);
    fprintf (st, "The communication line performs input and output through a TCP session\n");
    fprintf (st, "connected to a user-specified port.  The ATTACH command specifies the");
    fprintf (st, "port to be used:\n\n");
    fprintf (st, "   sim> ATTACH %sn {interface:}port        set up listening port\n\n", dptr->name);
    fprintf (st, "where port is a decimal number between 1 and 65535 that is not being used for\n");
    fprintf (st, "other TCP/IP activities. An ATTACH is required even if in PRIMARY mode. \n\n");
    return SCPE_OK;
}

void dmc_setrxint(CTLR *controller)
{
    controller->rxi = 1;
    dmc_rxi_summary |= (1u << controller->index);
    SET_INT(DMCRX);
}

void dmc_clrrxint(CTLR *controller)
{
    controller->rxi = 0;
    dmc_rxi_summary &= ~(1u << controller->index);
    if (!dmc_rxi_summary)
        CLR_INT(DMCRX);
    else
        SET_INT(DMCRX);
}

void dmc_settxint(CTLR *controller)
{
    controller->txi = 1;
    dmc_txi_summary |= (1u << controller->index);
    SET_INT(DMCTX);
}

void dmc_clrtxint(CTLR *controller)
{
    controller->txi = 0;
    dmc_txi_summary &= ~(1u << controller->index);
    if (!dmc_txi_summary)
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
    sim_debug(trace_level, controller->device, "%s SEL4 (0x%04x)\n", prefix, data);
}

void dmc_dumpregsel6(CTLR *controller, int trace_level, char *prefix, uint16 data)
{
    sim_debug(
        trace_level,
        controller->device,
        "%s SEL6 (0x%04x) %s\n",
        prefix,
        data,
        dmc_bitfld(data, SEL6_LOST_DATA_BIT, 1) ? "LOST_DATA " : "");
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

uint16 dmc_getreg(CTLR *controller, int reg, int ext)
{
    uint16 ans = 0;
    switch (dmc_getsel(reg))
    {
    case 00:
        ans = *controller->csrs->sel0;
        if (ext) dmc_dumpregsel0(controller, DBG_REG, "Getting", ans);
        break;
    case 01:
        ans = *controller->csrs->sel2;
        if (ext) dmc_dumpregsel2(controller, DBG_REG, "Getting", ans);
        break;
    case 02:
        ans = *controller->csrs->sel4;
        if (ext) dmc_dumpregsel4(controller, DBG_REG, "Getting", ans);
        break;
    case 03:
        ans = *controller->csrs->sel6;
        if (ext) dmc_dumpregsel6(controller, DBG_REG, "Getting", ans);
        break;
    case 04:
        ans = *controller->csrs->sel10;
        if (ext) dmc_dumpregsel10(controller, DBG_REG, "Getting", ans);
        break;
    default:
        {
            sim_debug(DBG_WRN, controller->device, "dmc_getreg(). Invalid register %d", reg);
        }
    }

    return ans;
}

void dmc_setreg(CTLR *controller, int reg, uint16 data, int ext)
{
    char *trace = (ext) ? "Writing" : "Setting";
    switch (dmc_getsel(reg))
    {
    case 00:
        dmc_dumpregsel0(controller, DBG_REG, trace, data);
        *controller->csrs->sel0 = data;
        if (!ext)
        {
            *controller->shadow_csrs->sel0 = data;
        }
        break;
    case 01:
        dmc_dumpregsel2(controller, DBG_REG, trace, data);
        *controller->csrs->sel2 = data;
        if (!ext)
        {
            *controller->shadow_csrs->sel2 = data;
        }
        break;
    case 02:
        dmc_dumpregsel4(controller, DBG_REG, trace, data);
        *controller->csrs->sel4 = data;
        if (!ext)
        {
            *controller->shadow_csrs->sel4 = data;
        }
        break;
    case 03:
        dmc_dumpregsel6(controller, DBG_REG, trace, data);
        *controller->csrs->sel6 = data;
        if (!ext)
        {
            *controller->shadow_csrs->sel6 = data;
        }
        break;
    case 04:
        dmc_dumpregsel10(controller, DBG_REG, trace, data);
        *controller->csrs->sel10 = data;
        if (!ext)
        {
            *controller->shadow_csrs->sel10 = data;
        }
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
int dmc_is_out_io_set(CTLR *controller)
{
    int ans = *controller->shadow_csrs->sel2 & OUT_IO_MASK;
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
        dmc_setreg(controller, 0, *controller->csrs->sel0 | DMC_RDYI_MASK, 0);
    }
    else
    {
        dmc_setreg(controller, 2, *controller->csrs->sel2 | DMP_RDYI_MASK, 0);
    }

    if (dmc_is_iei_set(controller))
    {
        dmc_setrxint(controller);
    }
}

void dmc_clear_rdyi(CTLR *controller)
{
    if (dmc_is_dmc(controller))
    {
        dmc_setreg(controller, 0, *controller->csrs->sel0 & ~DMC_RDYI_MASK, 0);
    }
    else
    {
        dmc_setreg(controller, 2, *controller->csrs->sel2 & ~DMP_RDYI_MASK, 0);
    }
}

void dmc_set_rdyo(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | DMC_RDYO_MASK, 0);

    if (dmc_is_ieo_set(controller))
    {
        dmc_settxint(controller);
    }
}

void dmc_set_lost_data(CTLR *controller)
{
    dmc_setreg(controller, 6, *controller->csrs->sel6 | LOST_DATA_MASK, 0);
}

void dmc_clear_master_clear(CTLR *controller)
{
    dmc_setreg(controller, 0, *controller->csrs->sel0 & ~MASTER_CLEAR_MASK, 0);
}

void dmc_set_run(CTLR *controller)
{
    dmc_setreg(controller, 0, *controller->csrs->sel0 | RUN_MASK, 0);
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
int dmc_get_output_transfer_type(CTLR *controller)
{
    return *controller->shadow_csrs->sel2 & TYPE_OUTPUT_MASK;
}
void dmc_set_type_output(CTLR *controller, int type)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | (type & TYPE_OUTPUT_MASK), 0);
}

void dmc_set_out_io(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 | OUT_IO_MASK, 0);
}

void dmc_clear_out_io(CTLR *controller)
{
    dmc_setreg(controller, 2, *controller->csrs->sel2 & ~OUT_IO_MASK, 0);
}

void dmc_process_master_clear(CTLR *controller)
{
    sim_debug(DBG_INF, controller->device, "Master clear\n");
    dmc_clear_master_clear(controller);
    dmc_close_socket(controller, "Master clear"); /* to resynch both ends */
    controller->state = Initialised;
    dmc_setreg(controller, 0, 0, 0);
    if (controller->dev_type == DMR)
    {
         /* DMR-11 indicates microdiagnostics complete when this is set */
        dmc_setreg(controller, 2, 0x8000, 0);
    }
    else
    {
        /* preserve contents of BSEL3 if DMC-11 */
        dmc_setreg(controller, 2, *controller->csrs->sel2 & 0xFF00, 0);
    }
    if (controller->dev_type == DMP)
    {
        dmc_setreg(controller, 4, 077, 0);
    }
    else
    {
        dmc_setreg(controller, 4, 0, 0);
    }

    if (controller->dev_type == DMP)
    {
        dmc_setreg(controller, 6, 0305, 0);
    }
    else
    {
        dmc_setreg(controller, 6, 0, 0);
    }
    dmc_buffer_queue_init_all(controller);

    controller->transfer_state = Idle;
    dmc_set_run(controller);

    sim_cancel (controller->unit);                                  /* stop poll */
    sim_clock_coschedule (controller->unit, tmxr_poll);             /* reactivate */
}

void dmc_start_input_transfer(CTLR *controller)
{
    int ok = 1;
    int type = dmc_get_input_transfer_type(controller);

    /* if this is a BA/CC I then check that the relevant queue has room first */
    if (type == TYPE_BACCI)
    {
        ok = (dmc_is_in_io_set(controller) && !dmc_buffer_queue_full(controller->receive_queue))
            ||
            (!dmc_is_in_io_set(controller) && !dmc_buffer_queue_full(controller->transmit_queue));
    }

    if (ok)
    {
        sim_debug(DBG_INF, controller->device, "Starting input transfer\n");
        controller->transfer_state = InputTransfer;
        controller->transfer_type = type;
        controller->transfer_in_io = dmc_is_in_io_set(controller);
        dmc_set_rdyi(controller);
    }
    else
    {
        sim_debug(DBG_WRN, controller->device, "Input transfer request not granted as queue is full\n");
    }
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

    dmc_setreg(controller, 4, addr & 0xFFFF, 0);
    dmc_setreg(controller, 6, (((addr & 0x30000)) >> 2) | count, 0);
    controller->transfer_state = OutputTransfer;
    dmc_set_type_output(controller, TYPE_BACCO);
    dmc_set_rdyo(controller);
}

void dmc_start_control_output_transfer(CTLR *controller)
{
    sim_debug(DBG_INF, controller->device, "Starting control output transfer\n");
    controller->transfer_state = OutputTransfer;
    dmc_set_type_output(controller, TYPE_CNTLO);
    dmc_set_rdyo(controller);
}

t_stat dmc_svc(UNIT* uptr)
{
    CTLR *controller;
    TIMER *poll_timer;
    TIMER *between_polls_timer;

    controller = dmc_get_controller_from_unit(uptr);

    poll_timer = &controller->stats->poll_timer;
    between_polls_timer = &controller->stats->between_polls_timer;

    if (dmc_timer_started(between_polls_timer))
    {
        dmc_timer_stop(between_polls_timer);
    }

    if (dmc_timer_started(poll_timer))
    {
        dmc_timer_resume(poll_timer);
    }
    else
    {
        dmc_timer_start(poll_timer);
    }

    if (dmc_is_attached(controller->unit))
    {
        dmc_line_update_speed_stats(controller->line);

        dmc_buffer_fill_receive_buffers(controller);
        if (controller->transfer_state == Idle) dmc_start_transfer_receive_buffer(controller);

        dmc_buffer_send_transmit_buffers(controller);
        if (controller->transfer_state == Idle) dmc_start_transfer_transmit_buffer(controller);
    }

    /* resubmit service timer */
    sim_clock_coschedule (controller->unit, tmxr_poll);

    dmc_timer_stop(poll_timer);
    if (dmc_timer_started(between_polls_timer))
    {
        dmc_timer_resume(between_polls_timer);
    }
    else
    {
        dmc_timer_start(between_polls_timer);
    }
    controller->stats->poll_count++;

    return SCPE_OK;
}

void dmc_line_update_speed_stats(LINE *line)
{
    clock_t current = clock();
    int current_second = current / CLOCKS_PER_SEC;
    if (current_second != line->last_second)
    {
        line->bytes_received_in_last_second = 0;
        line->bytes_sent_in_last_second = 0;
        line->last_second = current_second;
    }
}

/* given the number of  bytes sent/received in the last second, the number of bytes to send or receive and the line speed, calculate how many bytes can be sent/received now */
int dmc_line_speed_calculate_byte_length(int bytes_in_last_second, int num_bytes, int speed)
{
    int ans;

    if (speed == 0)
    {
        ans = num_bytes;
    }
    else
    {
        int clocks_this_second = clock() % CLOCKS_PER_SEC;
        int allowable_bytes_to_date = ((speed/8) * clocks_this_second)/CLOCKS_PER_SEC;
        int allowed_bytes = allowable_bytes_to_date - bytes_in_last_second;
        if (allowed_bytes < 0)
        {
            allowed_bytes = 0;
        }

        if (num_bytes > allowed_bytes)
        {
            ans = allowed_bytes;
        }
        else
        {
            ans = num_bytes;
        }
//sim_debug(DBG_WRN, dmc_ctrls[0].device, "Bytes in last second %4d, clocks this sec %3d allowable bytes %4d, requested %4d allowed %4d\n", bytes_in_last_second, clocks_this_second, allowable_bytes_to_date, num_bytes, ans);
    }

    return ans;
}

void dmc_buffer_trace_line(int tracelevel, CTLR *controller, uint8 *buf, int length, char *prefix)
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

void dmc_buffer_trace(CTLR *controller, uint8 *buf, int length, char *prefix, uint32 address)
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
        q->queue[new_buffer].actual_block_len = 0;
        q->queue[new_buffer].transfer_buffer = NULL;
        q->queue[new_buffer].block_len_bytes_read = 0;
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

void dmc_buffer_queue_get_stats(BUFFER_QUEUE *q, int *available, int *contains_data, int *transfer_in_progress)
{
    BUFFER *buf = dmc_buffer_queue_head(q);
    *available = 0;
    *contains_data = 0;
    *transfer_in_progress = 0;

    while (buf != NULL)
    {
        switch (buf->state)
        {
        case Available:
            {
                (*available)++;
                break;
            }

        case ContainsData:
            {
                (*contains_data)++;
                break;
            }

        case TransferInProgress:
            {
                (*transfer_in_progress)++;
                break;
            }
        }

        buf = buf->next;
    }
}

t_stat dmc_open_master_socket(CTLR *controller, char *port)
{
    t_stat ans;
    ans = SCPE_OK;
    if (controller->master_socket == INVALID_SOCKET)
    {
        controller->master_socket = sim_master_sock(port, &ans);
        if (controller->master_socket == INVALID_SOCKET)
        {
            sim_debug(DBG_WRN, controller->device, "Failed to open master socket on port %s\n", port);
            ans = SCPE_OPENERR;
        }
        else
        {
            printf ("%s-11 %s%d listening on port %s\n", (controller->dev_type == DMC) ? "DMC" : ((controller->dev_type == DMR) ? "DMR" : ((UNIBUS) ? "DMP" : "DMV")), controller->device->name, (int)(controller->unit-controller->device->units), port);
        }
    }

    return ans;
}

t_stat dmc_close_master_socket(CTLR *controller)
{
    sim_close_sock (controller->master_socket, TRUE);
    controller->master_socket = INVALID_SOCKET;
    return SCPE_OK;
}

// Gets the bidirectional socket and handles arbitration of determining which socket to use.
int dmc_get_socket(CTLR *controller, int forRead)
{
    int ans = 0;
    if (controller->line->isPrimary)
    {
        ans = dmc_get_transmit_socket(controller, 0, forRead); // TODO: After change to single socket, loopback may not work.
    }
    else
    {
        ans = dmc_get_receive_socket(controller, forRead); // TODO: After change to single socket, loopback may not work.
    }
    return ans;
}

int dmc_get_receive_socket(CTLR *controller, int forRead)
{
    int ans = 0;
    if (controller->line->socket == INVALID_SOCKET)
    {
        char *ipaddr;
        //sim_debug(DBG_SOK, controller->device, "Trying to open receive socket\n");
        controller->line->socket = sim_accept_conn (controller->master_socket, &ipaddr); /* poll connect */
        if (controller->line->socket != INVALID_SOCKET)
        {
            char host[sizeof(controller->line->peer)];

            if (sim_parse_addr (controller->line->peer, host, sizeof(host), NULL, NULL, 0, NULL, ipaddr))
            {
                sim_debug(DBG_WRN, controller->device, "Received connection from unexpected source IP %s. Closing the connection.\n", ipaddr);
                dmc_close_receive(controller, "Unathorized connection", ipaddr);
            }
            else
            {
                sim_debug(DBG_SOK, controller->device, "Opened receive socket %d\n", controller->line->socket);
                controller->line->receive_readable = FALSE;
            }
            free(ipaddr);
        }
    }

    if (controller->line->socket != INVALID_SOCKET)
    {
        int readable = sim_check_conn(controller->line->socket, forRead);
        if (readable == 0) /* Still opening */
        {
            // Socket is still being opened, or is open but there is no data ready to be read.
            ans = 0;
        }
        else if (readable == -1) /* Failed to open */
        {
            dmc_close_receive(controller, "failed to connect", NULL);
            ans = 0;
        }
        else /* connected */
        {
            if (!controller->line->receive_readable)
            {
                sim_debug(DBG_CON, controller->device, "Receive socket is now readable\n");
            }
            controller->line->receive_readable = TRUE;
            ans = 1;
        }
    }

    return ans;
}

int dmc_get_transmit_socket(CTLR *controller, int is_loopback, int forRead)
{
    int ans = 0;
    /* close transmit socket if there is a change in the loopback setting */
    if (is_loopback ^ controller->line->transmit_is_loopback)
    {
        dmc_close_transmit(controller, "loopback change");
    }

    if (controller->line->socket == INVALID_SOCKET && ((int32)(time(NULL) - controller->line->last_connect_attempt)) > controller->connect_poll_interval)
    {
        char host_port_buf[CBUFSIZE];
        char *host_port = host_port_buf;

        controller->line->transmit_is_loopback = is_loopback;
        
        controller->line->last_connect_attempt = time(NULL);
        if (is_loopback)
        {
            if (strrchr(controller->line->receive_port, ':'))
            {
                host_port = controller->line->receive_port;
            }
            else
            {
                sprintf(host_port_buf, "localhost:%s", controller->line->receive_port);
            }
        }
        else
        {
            host_port = controller->line->peer;
        }

        sim_debug(DBG_SOK, controller->device, "Trying to open transmit socket to address:port %s\n", host_port);
        controller->line->last_connect_attempt = time(NULL);
        controller->line->socket = sim_connect_sock(host_port, NULL, NULL);
        if (controller->line->socket != INVALID_SOCKET)
        {
            sim_debug(DBG_SOK, controller->device, "Opened transmit socket to port %s\n", host_port);
            controller->line->transmit_writeable = FALSE;
        }
    }

    if (controller->line->socket != INVALID_SOCKET)
    {
        int writeable = sim_check_conn(controller->line->socket, forRead);
        if (writeable == 0) /* Still opening */
        {
            //sim_debug(DBG_SOK, controller->device, "Waiting for transmit socket to become writeable\n");
            ans = 0;
        }
        else if (writeable == -1) /* Failed to open */
        {
            dmc_close_transmit(controller, "failed to connect");
            ans = 0;
        }
        else /* connected */
        {
            if (!controller->line->transmit_writeable)
            {
                sim_debug(DBG_CON, controller->device, "Transmit socket is now writeable\n");
            }
            controller->line->transmit_writeable = TRUE;
            ans = 1;
        }
    }

    return ans;
}

void dmc_error_and_close_socket(CTLR *controller, char *format)
{
    int err = WSAGetLastError(); 
    char errmsg[80];
    sprintf(errmsg, format, err);
    dmc_close_socket(controller, errmsg);
}

void dmc_close_socket(CTLR *controller, char *reason)
{
    if (controller->line->isPrimary)
    {
        dmc_close_transmit(controller, reason);
    }
    else
    {
        dmc_close_receive(controller, reason, NULL);
    }
}

void dmc_close_receive(CTLR *controller, char *reason, char *from)
{
    if (controller->line->socket != INVALID_SOCKET)
    {
        sim_debug(DBG_SOK, controller->device, "Closing receive socket on port %s, reason: %s%s%s\n", controller->line->receive_port, reason, from ? " from " : "", from ? from : "");
        sim_close_sock(controller->line->socket, FALSE);
        controller->line->socket = INVALID_SOCKET;

        if (controller->line->receive_readable)
        {
            sim_debug(DBG_CON, controller->device, "Readable receive socket closed, reason: %s\n", reason);
        }
        controller->line->receive_readable = FALSE;
    }
}

void dmc_close_transmit(CTLR *controller, char *reason)
{
    if (controller->line->socket != INVALID_SOCKET)
    {
        sim_debug(DBG_SOK, controller->device, "Closing transmit socket to port %s, socket %d, reason: %s\n", controller->line->peer, controller->line->socket, reason);
        sim_close_sock(controller->line->socket, FALSE);
        controller->line->socket = INVALID_SOCKET;

        if (controller->line->transmit_writeable)
        {
            sim_debug(DBG_CON, controller->device, "Writeable transmit socket closed, reason: %s\n", reason);
        }
        controller->line->transmit_writeable = FALSE;
    }
}

/* returns true if some data was received */
int dmc_buffer_fill_receive_buffers(CTLR *controller)
{
    int ans = FALSE;
    SOCKET socket;
    if (controller->state == Running)
    {
        BUFFER *buffer = dmc_buffer_queue_find_first_available(controller->receive_queue);
        while (buffer != NULL && buffer->state == Available)
        {
            if (dmc_get_socket(controller, TRUE))
            {
                int bytes_read = 0;
                int lost_data = 0;

                socket = controller->line->socket;
                /* read block length and allocate buffer */
                if ((size_t)buffer->block_len_bytes_read < sizeof(buffer->actual_block_len))
                {
                    char *start_addr = ((char *)&buffer->actual_block_len) + buffer->block_len_bytes_read;
                    bytes_read = sim_read_sock(socket, start_addr, sizeof(buffer->actual_block_len) - buffer->block_len_bytes_read);
                    if (bytes_read >= 0)
                    {
                        buffer->block_len_bytes_read += bytes_read;
                        if (buffer->block_len_bytes_read == sizeof(buffer->actual_block_len))
                        {
                            buffer->actual_block_len = ntohs(buffer->actual_block_len);
                            if (buffer->actual_block_len > buffer->count)
                            {
                                sim_debug(DBG_WRN, controller->device, "LOST DATA, buffer available has %d bytes, but the block is %d bytes\n", buffer->count, buffer->actual_block_len);
                                dmc_setreg(controller, 4, 0, 0);
                                dmc_setreg(controller, 6, 0, 0);
                                dmc_set_lost_data(controller);
                                dmc_start_control_output_transfer(controller);
                                lost_data = 1;
                                dmc_error_and_close_socket(controller, "oversized packet");
                            }

                            if (buffer->actual_block_len > 0)
                            {
                                buffer->transfer_buffer = (uint8 *)malloc(buffer->actual_block_len); /* read full buffer regardless, so bad buffer is flushed */
                            }
                        }
                    }
                }
                else
                {
                    lost_data = buffer->actual_block_len > buffer->count; /* need to preserve this variable if need more than one attempt to read the buffer */
                }

                /* read the actual block */
                if (buffer->block_len_bytes_read == sizeof(buffer->actual_block_len))
                {
                    bytes_read = 0;
                    if (buffer->actual_block_len > 0)
                    {
                        int bytes_to_read = dmc_line_speed_calculate_byte_length(controller->line->bytes_received_in_last_second, buffer->actual_block_len - buffer->actual_bytes_transferred, controller->line->speed);
                        if (bytes_to_read > 0)
                        {
                            bytes_read = sim_read_sock(controller->line->socket, (char *)(buffer->transfer_buffer + buffer->actual_bytes_transferred), bytes_to_read);
                        }
                    }

                    if (bytes_read >= 0)
                    {
                        buffer->actual_bytes_transferred += bytes_read;
                        controller->line->bytes_received_in_last_second += bytes_read;

                        if (buffer->actual_bytes_transferred >= buffer->actual_block_len)
                        {
                            dmc_buffer_trace(controller, buffer->transfer_buffer, buffer->actual_bytes_transferred, "REC ", buffer->address);
                            controller->buffers_received_from_net++;
                            buffer->state = ContainsData;
                            if (!lost_data)
                            {
                                Map_WriteB(buffer->address, buffer->actual_bytes_transferred, buffer->transfer_buffer);
                            }
                            else
                            {
                                buffer->actual_block_len = 0; /* so an empty buffer is returned to the driver */
                            }

                            if (buffer->actual_block_len > 0)
                            {
                                free(buffer->transfer_buffer);
                                buffer->transfer_buffer = NULL;
                            }

                            ans = TRUE;
                        }
                    }
                }

                /* Only close the socket if there was an error or no more data */
                if (bytes_read < 0)
                {
                    dmc_error_and_close_socket(controller, "read error, code=%d");
                    break;
                }

                /* if buffer is incomplete do not try to read any more buffers and continue filling this one later */
                if (buffer->state == Available)
                {
                    break; /* leave buffer available and continue filling it later */
                }
            }
            else
            {
                break;
            }

            buffer = buffer ->next;
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
    while (buffer != NULL)
    {
        if (dmc_get_socket(controller, FALSE)) // TODO: , buffer->is_loopback);
        {
            int bytes = 0;
            int bytes_to_send;
            uint16 block_len;
            int total_buffer_len = (buffer->count > 0) ? buffer->count + sizeof(block_len) : 0;

            /* only send the buffer if it actually has some data, sometimes get zero length buffers - don't send these */
            if (total_buffer_len > 0)
            {
                if (buffer->transfer_buffer == NULL)
                {
                    int n;
                    /* construct buffer and include block length bytes */
                    buffer->transfer_buffer = (uint8 *)malloc(total_buffer_len);
                    block_len = htons(buffer->count);
                    memcpy(buffer->transfer_buffer, (char *)&block_len, sizeof(block_len));
                    n = Map_ReadB(buffer->address, buffer->count, buffer->transfer_buffer + sizeof(block_len));
                    if (n > 0)
                    {
                        sim_debug(DBG_WRN, controller->device, "DMA error\n");
                    }
                }

                bytes_to_send = dmc_line_speed_calculate_byte_length(controller->line->bytes_sent_in_last_second, buffer->count + sizeof(block_len) - buffer->actual_bytes_transferred, controller->line->speed);
                if (bytes_to_send > 0)
                {
                    bytes = sim_write_sock (controller->line->socket, (char *)(buffer->transfer_buffer + buffer->actual_bytes_transferred), bytes_to_send);
                    if (bytes >= 0)
                    {
                        buffer->actual_bytes_transferred += bytes;
                        controller->line->bytes_sent_in_last_second += bytes;
                    }

                    if (buffer->actual_bytes_transferred >= total_buffer_len || bytes < 0)
                    {
                        dmc_buffer_trace(controller, buffer->transfer_buffer+sizeof(block_len), buffer->count, "TRAN", buffer->address);
                        free(buffer->transfer_buffer);
                    }
                }
            }

            if (buffer->actual_bytes_transferred >= total_buffer_len)
            {
                controller->buffers_transmitted_to_net++;
                buffer->state = ContainsData; // so won't try to transmit again
                ans = TRUE;
            }
            else if (bytes < 0)
            {
                int err = WSAGetLastError (); 
                char errmsg[80];
                sprintf(errmsg, "write failure, code=%d", err);

                dmc_close_transmit(controller, errmsg);
                break;
            }
            else
            {
                break; /* poll again later to send more bytes */
            }

        }
        else
        {
            break;
        }

        buffer = buffer ->next;
    }

    return ans;
}

void dmc_start_transfer_receive_buffer(CTLR *controller)
{
    BUFFER *head = dmc_buffer_queue_head(controller->receive_queue);
    if (head != NULL)
    {
        if (head->state == ContainsData)
        {
            head->state = TransferInProgress;
            dmc_start_data_output_transfer(controller, head->address, head->actual_block_len, TRUE);
        }
    }
}

void dmc_start_transfer_transmit_buffer(CTLR *controller)
{
    BUFFER *head = dmc_buffer_queue_head(controller->transmit_queue);
    if (head != NULL)
    {
        if (head->state == ContainsData)
        {
            head->state = TransferInProgress;
            dmc_start_data_output_transfer(controller, head->address, head->count, FALSE);
        }
    }
}

void dmc_check_for_output_transfer_completion(CTLR *controller)
{
    if (!dmc_is_rdyo_set(controller))
    {
        sim_debug(DBG_INF, controller->device, "Output transfer completed\n");
        controller->transfer_state = Idle;
        if (dmc_get_output_transfer_type(controller) == TYPE_BACCO)
        {
            if (dmc_is_out_io_set(controller))
            {
                dmc_buffer_queue_release_head(controller->receive_queue);
                controller->receive_buffer_output_transfers_completed++;
            }
            else
            {
                dmc_buffer_queue_release_head(controller->transmit_queue);
                controller->transmit_buffer_output_transfers_completed++;
            }
        }
        dmc_process_command(controller); // check for any input transfers
    }
}

void dmc_process_input_transfer_completion(CTLR *controller)
{
    if (dmc_is_dmc(controller))
    {
        if (!dmc_is_rqi_set(controller))
        {
            uint16 sel4 = *controller->csrs->sel4;
            uint16 sel6 = *controller->csrs->sel6;
            dmc_clear_rdyi(controller);
            if (controller->transfer_type == TYPE_BASEI)
            {
                uint32 baseaddr = ((sel6 >> 14) << 16) | sel4;
                uint16 count = sel6 & 0x3FFF;
                sim_debug(DBG_INF, controller->device, "Completing Base In input transfer, base address=0x%08x count=%d\n", baseaddr, count);
            }
            else if (controller->transfer_type == TYPE_BACCI)
            {
                uint32 addr = ((sel6 >> 14) << 16) | sel4;
                uint16 count = sel6 & 0x3FFF;
                if (controller->transfer_in_io != dmc_is_in_io_set(controller))
                {
                    sim_debug(DBG_TRC, controller->device, "IN IO MISMATCH\n");
                }

                controller->transfer_in_io = dmc_is_in_io_set(controller); // using evdmc the flag is set when the transfer completes - not when it starts, evdca seems to set in only at the start of the transfer - clearing it when it completes
                controller->state = Running;
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

            controller->transfer_state = Idle;
        }
    }
    else
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
    }
    else
    {
        if (controller->transfer_state == InputTransfer)
        {
            dmc_process_input_transfer_completion(controller);
        }
        else if (controller->transfer_state == OutputTransfer)
        {
            dmc_check_for_output_transfer_completion(controller);
        }
        else if (dmc_is_rqi_set(controller))
        {
            dmc_start_input_transfer(controller);
        }
        else if (dmc_is_dmc (controller) &&
                 *controller->csrs->sel0 & ROMI_MASK &&
                 *controller->csrs->sel6 == DSPDSR)
            /* DMC-11 or DMR-11, see if ROMI bit is set.  If so, if SEL6 is
               0x22b3 (read line status instruction), set the DTR bit in SEL2.  */
        {
            dmc_setreg (controller, 2, 0x800, 0);
        }
    }
}

t_stat dmc_rd(int32 *data, int32 PA, int32 access)
{
    CTLR *controller = dmc_get_controller_from_address(PA);
    sim_debug(DBG_TRC, controller->device, "dmc_rd(), addr=0x%x access=%d\n", PA, access);
    *data = dmc_getreg(controller, PA, 1);

    return SCPE_OK;
}

t_stat dmc_wr(int32 data, int32 PA, int32 access)
{
    CTLR *controller = dmc_get_controller_from_address(PA);
    int reg = PA & 07;
    uint16 oldValue = dmc_getreg(controller, PA, 0);
    if (access == WRITE)
    {
        sim_debug(DBG_TRC, controller->device, "dmc_wr(), addr=0x%08x, SEL%d, data=0x%04x\n", PA, reg, data);
    }
    else
    {
        sim_debug(DBG_TRC, controller->device, "dmc_wr(), addr=0x%08x, BSEL%d, data=%04x\n", PA, reg, data);
    }

    if (access == WRITE)
    {
        if (PA & 1)
        {
            sim_debug(DBG_WRN, controller->device, "dmc_wr(), Unexpected non-16-bit write access to SEL%d\n", reg);
        }
        dmc_setreg(controller, PA, data, 1);
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

        dmc_setreg(controller, PA, (oldValue & ~mask) | (data & mask), 1);
    }

    if (dmc_is_attached(controller->unit) && (dmc_getsel(reg) == 0 || dmc_getsel(reg) == 1))
    {
        dmc_process_command(controller);
    }

    return SCPE_OK;
}

int32 dmc_rxint (void)
{
    int i;
    int32 ans = 0; /* no interrupt request active */
    for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++)
    {
        CTLR *controller = &dmc_ctrls[i];
        if (controller->rxi != 0)
        {
            DIB *dib = (DIB *)controller->device->ctxt;
            ans = dib->vec + (8 * (int)(controller->unit - controller->device->units));
            dmc_clrrxint(controller);
            break;
        }
    }

    return ans;
    }

int32 dmc_txint (void)
{
    int i;
    int32 ans = 0; /* no interrupt request active */
    for (i=0; i<DMC_NUMDEVICE+DMP_NUMDEVICE; i++)
    {
        CTLR *controller = &dmc_ctrls[i];
        if (controller->txi != 0)
        {
            DIB *dib = (DIB *)controller->device->ctxt;
            ans = dib->vec + 4 + (8 * (int)(controller->unit - controller->device->units));
            dmc_clrtxint(controller);
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
        dmc_shadow_csrs[i].sel0 = &dmc_shadow_sel0[i];
        dmc_shadow_csrs[i].sel2 = &dmc_shadow_sel2[i];
        dmc_shadow_csrs[i].sel4 = &dmc_shadow_sel4[i];
        dmc_shadow_csrs[i].sel6 = &dmc_shadow_sel6[i];
        controller = &dmc_ctrls[i];
        controller->csrs = &dmc_csrs[i];
        controller->shadow_csrs = &dmc_shadow_csrs[i];
        controller->line = &dmc_line[i];
        controller->receive_queue = &dmc_receive_queues[i];
        controller->transmit_queue = &dmc_transmit_queues[i];;
        controller->stats = &dmc_stats[i];
        controller->device = &dmc_dev;
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
        dmp_shadow_csrs[i].sel0 = &dmp_shadow_sel0[i];
        dmp_shadow_csrs[i].sel2 = &dmp_shadow_sel2[i];
        dmp_shadow_csrs[i].sel4 = &dmp_shadow_sel4[i];
        dmp_shadow_csrs[i].sel6 = &dmp_shadow_sel6[i];
        dmp_shadow_csrs[i].sel10 = &dmp_shadow_sel10[i];
        controller = &dmc_ctrls[i+DMC_NUMDEVICE];
        controller->csrs = &dmp_csrs[i];
        controller->shadow_csrs = &dmp_shadow_csrs[i];
        controller->line = &dmp_line[i];
        controller->receive_queue = &dmp_receive_queues[i];
        controller->transmit_queue = &dmp_transmit_queues[i];;
        controller->stats = &dmp_stats[i];
        controller->device = (UNIBUS) ? &dmp_dev : &dmv_dev;
        controller->dev_type = DMP;
        controller->unit = &controller->device->units[i];
        controller->index = i + DMC_NUMDEVICE;
    }
    if (0 == dmc_units[0].flags)         /* First Time Initializations */
    {
        for (i=0; i < DMC_NUMDEVICE; i++)
        {
            controller = &dmc_ctrls[i];
            controller->state = Initialised;
            controller->transfer_state = Idle;
            controller->master_socket = INVALID_SOCKET;
            controller->connect_poll_interval = 30;
            controller->dev_type = DMC;
            dmc_line[i].socket = INVALID_SOCKET;
            dmc_dev.units[i] = dmc_unit_template;
        }
        for (i=0; i < DMP_NUMDEVICE; i++)
        {
            controller = &dmc_ctrls[i+DMC_NUMDEVICE];
            controller->state = Initialised;
            controller->transfer_state = Idle;
            controller->master_socket = INVALID_SOCKET;
            controller->connect_poll_interval = 30;
            dmp_line[i].socket = INVALID_SOCKET;
            dmp_dev.units[i] = dmc_unit_template;
        }
    }

    ans = auto_config (dptr->name, (dptr->flags & DEV_DIS) ? 0 : dptr->numunits);

    if (!(dptr->flags & DEV_DIS))
    {
        for (i = 0; i < DMC_NUMDEVICE + DMP_NUMDEVICE; i++)
        {
            if (dmc_ctrls[i].device == dptr)
            {
                controller = &dmc_ctrls[i];
                dmc_buffer_queue_init_all(controller);
                dmc_clrrxint(controller);
                dmc_clrtxint(controller);
                for (j=0; j<dptr->numunits; j++)
                    sim_cancel (&dptr->units[j]); /* stop poll */
            }
        }
    }

    return ans;
}

t_stat dmc_attach (UNIT *uptr, char *cptr)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    t_stat ans = SCPE_OK;

    if (dmc_is_attached(uptr))
    {
        dmc_detach(uptr);
    }

    ans = dmc_open_master_socket(controller, cptr);
    if (ans == SCPE_OK)
    {
        controller->line->socket = INVALID_SOCKET;
        uptr->flags = uptr->flags | UNIT_ATT; /* set unit attached flag */
        uptr->filename = (char *)malloc(strlen(cptr)+1);
        strcpy(uptr->filename, cptr);
        controller->line->receive_port = uptr->filename;
        dmc_reset_unit_stats(controller->stats);
    }

    return ans;
}

t_stat dmc_detach (UNIT *uptr)
{
    CTLR *controller = dmc_get_controller_from_unit(uptr);
    dmc_error_and_close_socket(controller, "Detach");
    dmc_close_master_socket(controller);
    uptr->flags = uptr->flags & ~UNIT_ATT; /* clear unit attached flag */
    free(uptr->filename);
    uptr->filename = NULL;
    sim_cancel(uptr);

    return SCPE_OK;
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

