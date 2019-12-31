/* ibm1130_sca.c: IBM 1130 synchronous communications adapter emulation

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel
   Revision History

   2005.03.08 - Started

 * (C) Copyright 2005-2010, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

/******************************************************************************************************************
 * NOTES:
 *          This module sends raw bisync data over a standard TCP port. It's meant to be
 *          used with the emulated 2703 device in Hercules.
 *
 * Attach command:
 *
 * to establish an outgoing connection:
 *
 *      attach sca host                 connect to named host on default port (initial default port is 2703); or
 *      attach sca host:###             connect to named host on port ###. ### is also set as new default port number.
 *                                      >> The simulator waits until the connection is established
 *
 * or to set up for incoming connections:
 *
 *      attach sca -l dummy             listen for a connection on default port (initially 2703); Nonnumeric "dummy" argument is ignored; or
 *      attach sca -l ###               listen for a connection on the port ###. ### is also set as the new default port
 *                                      >> The simulator proceeds. When another simulator connects, the READY bit is set in the DSW.
 *
 *                                      If the SCA's autoanswer-enable bit has been set, an incoming connection causes an interrupt (untested)
 * Configuration commands:
 *
 *      set sca bsc                     set bisync mode (default)
 *      set sca str                     set synchronous transmit/recieve mode (NOT IMPLEMENTED)
 *
 *      set sca ###                     set simulated baud rate to ###, where ### is 600, 1200, 2000, 2400 or 4800 (4800 is default)
 *
 *      set sca half                    set simulated half-duplex mode
 *      set sca full                    set simulated full-duplex mode (note: 1130's SCA can't actually send and receive at the same time!)
 *
 *      deposit sca keepalive ###       send SYN packets every ### msec when suppressing SYN's, default is 0 (no keepalives)
 *
 * STR/BSC mode is selected by a toggle switch on the 1130, with the SET SCA BSC or SET SET STR command here.
 * Testable with in_bsc_mode() or in_str_mode() in this module. If necessary, the SET command can be configured
 * to call a routine when the mode is changed; or, we can just required the user to reboot the simulated 1130
 * when switching modes.
 *
 * STR MODE IS NOT IMPLEMENTED!
 *
 * The SCA adapter appears to know nothing of the protocols used by STR and BSC. It does handle the sync/idle
 * character specially, and between BSC and STR mode the timers are used differently. Also in STR mode it
 * can be set to a sychronization mode where it sends SYN's without program intervention.
 *
 * See definition of SCA_STATE for defintion of simulator states.
 *
 * Rather than trying to simulate the actual baud rates, we try to keep the character service interrupts
 * coming at about the same number of instruction intervals -- thus existing 1130 code should work correctly
 * but the effective data transfer rate will be much higher. The "timers" however are written to run on real wall clock
 * time, This may or may not work. If necessary they could be set to time out based on the number of calls to sca_svc
 * which occurs once per character send/receive time; For example, at 4800 baud and an 8 bit frame, we get
 * 600 characters/second, so the 3 second timer would expire in 1800 sca_svc calls. Well, that's something to
 * think about.
 *
 * To void blowing zillions of SYN characters across the network when the system is running but idle, we suppress
 * them. If 100 consecutive SYN's are sent, we flush the output buffer and stop sending SYN's
 * until some other character is sent, OR the line is turned around (e.g. INITR, INITW or an end-operation
 * CONTROL is issued), or the number of msec set by DEPOSIT SCS KEEPALIVE has passed, if a value has
 * been set. By default no keepalives are sent.
 *
 * Timer operations are not debugged. First, do timers automatically reset and re-interrupt if
 * left alone after they timeout the first time? Does XIO_SENSE_DEV really restart all running timers?
 * Does it touch the timer trigger (program timer?) How do 3 and 1.25 second timers really work
 * in BSC mode? Hard to tell from the FC manual.
 ******************************************************************************************************************/

#include "ibm1130_defs.h"
#include "sim_sock.h"                                       /* include path must include main simh directory */
#include <ctype.h>

#define DEBUG_SCA_FLUSH         0x0001                      /* debugging options */
#define DEBUG_SCA_TRANSMIT      0x0002
#define DEBUG_SCA_CHECK_INDATA  0x0004
#define DEBUG_SCA_RECEIVE_SYNC  0x0008
#define DEBUG_SCA_RECEIVE_DATA  0x0010
#define DEBUG_SCA_XIO_READ      0x0020
#define DEBUG_SCA_XIO_WRITE     0x0040
#define DEBUG_SCA_XIO_CONTROL   0x0080
#define DEBUG_SCA_XIO_INITW     0x0100
#define DEBUG_SCA_XIO_INITR     0x0200
#define DEBUG_SCA_XIO_SENSE_DEV 0x0400
#define DEBUG_SCA_TIMERS        0x0800
#define DEBUG_SCA_ALL           0xFFFF

/* #define DEBUG_SCA            (DEBUG_SCA_TIMERS|DEBUG_SCA_FLUSH|DEBUG_SCA_TRANSMIT|DEBUG_SCA_CHECK_INDATA|DEBUG_SCA_RECEIVE_SYNC|DEBUG_SCA_RECEIVE_DATA|DEBUG_SCA_XIO_INITR|DEBUG_SCA_XIO_INITW) */
#define DEBUG_SCA           (DEBUG_SCA_TIMERS|DEBUG_SCA_FLUSH|DEBUG_SCA_CHECK_INDATA|DEBUG_SCA_XIO_INITR|DEBUG_SCA_XIO_INITW)

#define SCA_DEFAULT_PORT        "2703"                      /* default socket, This is the number of the IBM 360's BSC device */

#define MAX_SYNS                 100                        /* number of consecutive syn's after which we stop buffering them */

/***************************************************************************************
 *  SCA
 ***************************************************************************************/

#define SCA_DSW_READ_RESPONSE                   0x8000      /* receive buffer full interrupt */
#define SCA_DSW_WRITE_RESPONSE                  0x4000      /* transmitter buffer empty interrupt */
#define SCA_DSW_CHECK                           0x2000      /* data overrun or character gap error */
#define SCA_DSW_TIMEOUT                         0x1000      /* timer interrupt, mode specific */
#define SCA_DSW_AUTOANSWER_REQUEST              0x0800      /* dataset is ringing and autoanswer is enabled */
#define SCA_DSW_BUSY                            0x0400      /* adapter is in either receive or transmit mode */
#define SCA_DSW_AUTOANSWER_ENABLED              0x0200      /* 1 when autoanswer mode has been enabled */
#define SCA_DSW_READY                           0x0100      /* Carrier detect? Connected and ready to rcv, xmit or sync */
#define SCA_DSW_RECEIVE_RUN                     0x0080      /* used in two-wire half-duplex STR mode only. "Slave" mode (?) */

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)

typedef enum {              /*  ms     m = mode (0 = idle, 1 = send, 2 = receive), s = substate */
    SCA_STATE_IDLE          = 0x00, /* nothing happening */
    SCA_STATE_TURN_SEND     = 0x10, /* line turning around to the send state */
    SCA_STATE_SEND_SYNC     = 0x11, /* sca is sending syncs */
    SCA_STATE_SEND1         = 0x12, /* have issued write response, waiting for write command */
    SCA_STATE_SEND2         = 0x13, /* write command issued, "sending" byte */
    SCA_STATE_TURN_RECEIVE  = 0x20, /* line turnaround to the receive state */
    SCA_STATE_RECEIVE_SYNC  = 0x21, /* sca is receiving syncs */
    SCA_STATE_RECEIVE_SYNC2 = 0x22, /* bsc mode, waiting for 2nd SYN */
    SCA_STATE_RECEIVE_SYNC3 = 0x23, /* bsc mode, waiting for 1st non-SYN */
    SCA_STATE_RECEIVE1      = 0x24, /* "receiving" a byte */
    SCA_STATE_RECEIVE2      = 0x25, /* read response issued, "receiving" next byte */
} SCA_STATE;

#define in_send_state()    (sca_state & 0x10)
#define in_receive_state() (sca_state & 0x20)

static t_stat sca_svc           (UNIT *uptr);               /* prototypes */
static t_stat sca_reset         (DEVICE *dptr);
static t_stat sca_attach        (UNIT *uptr, CONST char *cptr);
static t_stat sca_detach        (UNIT *uptr);
static void sca_start_timer     (int n, int msec_now);
static void sca_halt_timer      (int n);
static void sca_toggle_timer    (int n, int msec_now);
                                                            /* timer states, chosen so any_timer_running can be calculated by oring states of all 3 timers */
typedef enum {SCA_TIMER_INACTIVE = 0, SCA_TIMER_RUNNING = 1, SCA_TIMER_INHIBITED = 2, SCA_TIMER_TIMEDOUT = 4} SCA_TIMER_STATE;

#define TIMER_3S        0       /* 3 second timer             index into sca_timer_xxx arrays */
#define TIMER_125S      1       /* 1.25 second timer */
#define TIMER_035S      2       /* 0.35 second timer */

static uint16 sca_dsw    = 0;                               /* device status word */
static uint32 sca_cwait  = 275;                             /* inter-character wait */
static uint32 sca_iwait  = 2750;                            /* idle wait */
static uint32 sca_state  = SCA_STATE_IDLE;
static uint8  sichar     = 0;                               /* sync/idle character */
static uint8  rcvd_char  = 0;                               /* most recently received character */
static uint8  sca_frame  = 8;
static char sca_port[3*CBUFSIZE];                             /* listening port */
static int32  sca_keepalive = 0;                            /* keepalive SYN packet period in msec, default = 0 (disabled) */
static SCA_TIMER_STATE sca_timer_state[3];                  /* current timer state */
static int    sca_timer_endtime[3];                         /* clocktime when timeout is to occur if state is RUNNING */
static int    sca_timer_timeleft[3];                        /* time left in msec if state is INHIBITED */
static t_bool any_timer_running = FALSE;                    /* TRUE if at least one timer is running */
static int    sca_timer_msec[3] = {3000, 1250, 350};        /* timebase in msec for the three timers: 3 sec, 1.25 sec, 0.35 sec */
static t_bool sca_timer_trigger;                            /* if TRUE, the "timer trigger" is set, the 0.35s timer is running and the 3 sec and 1.25 sec timers are inhibited */
static int    sca_nsyns  = 0;                               /* number of consecutively sent SYN's */
static int    idles_since_last_write = 0;                   /* used to detect when software has ceased sending data */
static SOCKET sca_lsock = INVALID_SOCKET;
static SOCKET sca_sock = INVALID_SOCKET;

#define SCA_SENDBUF_SIZE    145                             /* maximum number of bytes to buffer for transmission */
#define SCA_RCVBUF_SIZE     256                             /* max number of bytes to read from socket at a time */
#define SCA_SEND_THRESHHOLD 140                             /* number of bytes to buffer before initiating packet send */
#define SCA_IDLE_THRESHHOLD   3                             /* maximum number of unintentional idles to buffer before initiating send */

static uint8  sca_sendbuf[SCA_SENDBUF_SIZE];                /* bytes pending to write to socket */
static uint8  sca_rcvbuf[SCA_RCVBUF_SIZE];                  /* bytes received from socket, to be given to SCA */
static int    sca_n2send = 0;                               /* number of bytes queued for transmission */
static int    sca_nrcvd = 0;                                /* number of received bytes in buffer */
static int    sca_rcvptr = 0;                               /* index of next byte to take from rcvbuf */

#define UNIT_V_BISYNC     (UNIT_V_UF + 0)                   /* BSC (bisync) mode */
#define UNIT_V_BAUD       (UNIT_V_UF + 1)                   /* 3 bits for baud rate encoding */
#define UNIT_V_FULLDUPLEX (UNIT_V_UF + 4)
#define UNIT_V_AUTOANSWER (UNIT_V_UF + 5)
#define UNIT_V_LISTEN     (UNIT_V_UF + 6)                   /* listen socket mode */

#define UNIT_BISYNC       (1u << UNIT_V_BISYNC)
#define UNIT_BAUDMASK     (7u << UNIT_V_BAUD)
#define UNIT_BAUD600      (0u << UNIT_V_BAUD)
#define UNIT_BAUD1200     (1u << UNIT_V_BAUD)
#define UNIT_BAUD2000     (2u << UNIT_V_BAUD)
#define UNIT_BAUD2400     (3u << UNIT_V_BAUD)
#define UNIT_BAUD4800     (4u << UNIT_V_BAUD)
#define UNIT_FULLDUPLEX   (1u << UNIT_V_FULLDUPLEX)
#define UNIT_AUTOANSWER   (1u << UNIT_V_AUTOANSWER)
#define UNIT_LISTEN       (1u << UNIT_V_LISTEN)

t_stat sca_set_baud (UNIT *uptr, int32 value, CONST char *cptr, void *desc);

UNIT sca_unit = {                                                       /* default settings */
    UDATA (sca_svc, UNIT_ATTABLE|UNIT_BISYNC|UNIT_BAUD4800|UNIT_FULLDUPLEX, 0),
};

REG sca_reg[] = {                                                       /* DEVICE STATE/SETTABLE PARAMETERS: */
    { HRDATA (SCADSW,    sca_dsw,    16) },                             /* device status word */
    { DRDATA (SICHAR,    sichar,      8), PV_LEFT },                    /* sync/idle character */
    { DRDATA (RCVDCHAR,  rcvd_char,   8), PV_LEFT },                    /* most recently received character */
    { DRDATA (FRAME,     sca_frame,   8), PV_LEFT },                    /* frame bits (6, 7 or 8) */
    { DRDATA (SCASTATE,  sca_state,  32), PV_LEFT },                    /* current state */
    { DRDATA (CTIME,     sca_cwait,  32), PV_LEFT },                    /* inter-character wait */
    { DRDATA (ITIME,     sca_iwait,  32), PV_LEFT },                    /* idle wait (polling interval for socket connects) */
    { BRDATA (SCASOCKET, sca_port,   8, 8, sizeof(sca_port)) },         /* listening port number */
    { DRDATA (KEEPALIVE, sca_keepalive, 32), PV_LEFT },                 /* keepalive packet period in msec */
    { NULL }  };

MTAB sca_mod[] = {                                                      /* DEVICE OPTIONS */
    { UNIT_BISYNC,      0,                  "STR",  "STR",  NULL },     /* mode option */
    { UNIT_BISYNC,      UNIT_BISYNC,        "BSC",  "BSC",  NULL },
    { UNIT_BAUDMASK,    UNIT_BAUD600,       "600",  "600",  sca_set_baud },     /* data rate option */
    { UNIT_BAUDMASK,    UNIT_BAUD1200,      "1200", "1200", sca_set_baud },
    { UNIT_BAUDMASK,    UNIT_BAUD2000,      "2000", "2000", sca_set_baud },
    { UNIT_BAUDMASK,    UNIT_BAUD2400,      "2400", "2400", sca_set_baud },
    { UNIT_BAUDMASK,    UNIT_BAUD4800,      "4800", "4800", sca_set_baud },
    { UNIT_FULLDUPLEX,  0,                  "HALF", "HALF", NULL },     /* duplex option (does this matter?) */
    { UNIT_FULLDUPLEX,  UNIT_FULLDUPLEX,    "FULL", "FULL", NULL },
    { 0 }  };

DEVICE sca_dev = {
    "SCA", &sca_unit, sca_reg, sca_mod,
    1, 16, 16, 1, 16, 16,
    NULL, NULL, sca_reset,
    NULL, sca_attach, sca_detach
};

/*********************************************************************************************
 * sca_set_baud - set baud rate handler (SET SCA.BAUD nnn)
 *********************************************************************************************/

t_stat sca_set_baud (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
    uint32 newbits;

    switch (value) {
        case 600:       newbits = UNIT_BAUD600;     break;
        case 1200:      newbits = UNIT_BAUD1200;    break;
        case 2000:      newbits = UNIT_BAUD2000;    break;
        case 2400:      newbits = UNIT_BAUD2400;    break;
        case 4800:      newbits = UNIT_BAUD4800;    break;
        default:        return SCPE_ARG;
    }

    CLRBIT(sca_unit.flags,  UNIT_BAUDMASK);
    SETBIT(sca_unit.flags, newbits);

    sca_cwait = 1320000 / value;            /* intercharacter wait time in instructions (roughly) */

    return SCPE_OK;
}

/*********************************************************************************************
 * HANDY MACROS 
 *********************************************************************************************/

#define in_bsc_mode()           (sca_unit.flags  & UNIT_BISYNC)                 /* TRUE if user selected BSC mode */
#define in_str_mode()           ((sca_unit.flags & UNIT_BISYNC) == 0)           /* TRUE if user selected STR mode */

/*********************************************************************************************
 * mstring - allocate a copy of a string
 *********************************************************************************************/

char *mstring (const char *str)
{
    int len;
    char *m;

    len = strlen(str)+1;
    if ((m = (char *)malloc(len)) == NULL) {
        printf("Out of memory!");
        return (char *)"?";     /* this will of course cause trouble if it's subsequently freed */
    }
    strcpy(m, str);
    return m;
}

/*********************************************************************************************
 * sca_socket_error - call when there is an error reading from or writing to socket
 *********************************************************************************************/

static void sca_socket_error (void)
{
    char name[4*CBUFSIZE];

    /* print diagnostic? */
    printf("SCA socket error, closing connection\n");

    /* tell 1130 that connection was lost */
    CLRBIT(sca_dsw, SCA_DSW_READY);

    if (sca_sock != INVALID_SOCKET) {
        /* close socket, prepare to listen again if in listen mode. It's a "master" socket if it was an outgoing connection */
        sim_close_sock(sca_sock);
        sca_sock = INVALID_SOCKET;

        if (sca_unit.filename != NULL)                              /* reset filename string in unit record */
            free(sca_unit.filename);

        if (sca_unit.flags & UNIT_LISTEN) {
            name[sizeof (name) - 1] = '\0';
            snprintf(name, sizeof (name) - 1, "(Listening on port %s)", sca_port);
            sca_unit.filename = mstring(name);
            printf("%s\n", name);
        }
        else
            sca_unit.filename = mstring("(connection failed)");
    }

    /* clear buffers */
    sca_nrcvd = sca_rcvptr = sca_n2send = sca_nsyns = 0;
}

/*********************************************************************************************
 * sca_transmit_byte, sca_flush - send data buffering mechanism
 *********************************************************************************************/

static void sca_flush (void)
{
    int nbytes;

    if (sca_n2send > 0) {
#if (DEBUG_SCA & DEBUG_SCA_FLUSH)
        printf("* SCA_FLUSH %d byte%s\n", sca_n2send, (sca_n2send == 1) ? "" : "s");
#endif

        if (sca_sock != INVALID_SOCKET) {
            nbytes = sim_write_sock(sca_sock, (char *) sca_sendbuf, sca_n2send);

            if (nbytes == SOCKET_ERROR)
                sca_socket_error();
            else if (nbytes != sca_n2send)
                printf("SOCKET BLOCKED -- NEED TO REWRITE IBM1130_SCA.C");

            /* I'm going to assume that SCA communications on the 1130 will consist entirely */
            /* of back and forth exchanges of short records, and so we should never stuff the pipe so full that */
            /* it blocks. If it does ever block, we'll have to come up with an asychronous buffering mechanism. */
        }

        sca_n2send = 0;                     /* mark buffer cleared */
    }
}

/*********************************************************************************************
 * sca_transmit_byte - buffer a byte to be send to the socket
 *********************************************************************************************/

static void sca_transmit_byte (uint8 b)
{
    uint32 curtime;
    static uint32 last_syn_time, next_syn_time;

#if (DEBUG_SCA & DEBUG_SCA_TRANSMIT)
    printf("* SCA_TRANSMIT: %02x\n", b);
#endif

    /* write a byte to the socket. Let's assume an 8 bit frame in all cases.
     * We buffer them up, then send the packet when (a) it fills up to a certain point
     * and/or (b) some time has passed? We handle (b) by:
     *      checking in sva_svc if several sca_svc calls are made w/o any XIO_WRITES, and
     *      flushing send buffer on line turnaround, timeouts, or any other significant state change
     */

    /* on socket error, call sca_socket_error(); */

    if (b == sichar) {
        if (sca_nsyns >= MAX_SYNS) {            /* we're suppressing SYN's */
            if (sca_keepalive > 0) {            /* we're sending keepalives, though... check to see if it's time */
                curtime = sim_os_msec();
                if (curtime >= next_syn_time || curtime < last_syn_time) {      /*  check for < last_syn_time because sim_os_msec() can wrap when OS has been running a long time */
                    sca_sendbuf[sca_n2send++] = b;
                    sca_sendbuf[sca_n2send++] = b;                              /* send 2 of them */
                    sca_flush();
                    last_syn_time = curtime;
                    next_syn_time = last_syn_time + sca_keepalive;
                }
            }
            return;
        }

        if (++sca_nsyns == MAX_SYNS) {          /* we've sent a bunch of SYN's, time to stop sending them */
            sca_sendbuf[sca_n2send] = b;        /* send last one */
            sca_flush();
            last_syn_time = sim_os_msec();      /* remember time, and note time to send next one */
            next_syn_time = last_syn_time + sca_keepalive;
            return;
        }
    }
    else
        sca_nsyns = 0;

    sca_sendbuf[sca_n2send] = b;                /* store character */

    if (++sca_n2send >= SCA_SEND_THRESHHOLD)
        sca_flush();                            /* buffer is full, send it immediately */
}

/*********************************************************************************************
 * sca_interrupt (utility routine) - set a bit in the device status word and initiate an interrupt
 *********************************************************************************************/

static void sca_interrupt (int bit)
{
    sca_dsw |= bit;                     /* set device status word bit(s) */
    SETBIT(ILSW[1], ILSW_1_SCA);        /* set interrupt level status word bit */

    calc_ints();                        /* udpate simulator interrupt status (not really needed if within xio handler, since ibm1130_cpu calls it after xio handler) */
}

/*********************************************************************************************
 * sca_reset - reset the SCA device
 *********************************************************************************************/

static t_stat sca_reset (DEVICE *dptr)
{
    /* flush any pending data */
    sca_flush();
    sca_nrcvd = sca_rcvptr = sca_n2send = sca_nsyns = 0;

    /* reset sca activity */
    sca_state = SCA_STATE_IDLE;
    CLRBIT(sca_dsw, SCA_DSW_BUSY | SCA_DSW_AUTOANSWER_ENABLED | SCA_DSW_RECEIVE_RUN | SCA_DSW_READ_RESPONSE | SCA_DSW_WRITE_RESPONSE | SCA_DSW_CHECK | SCA_DSW_TIMEOUT | SCA_DSW_AUTOANSWER_REQUEST);
    sca_timer_state[0] = sca_timer_state[1] = sca_timer_state[2] = SCA_TIMER_INACTIVE;
    any_timer_running = FALSE;
    sca_timer_trigger = FALSE;

    if (sca_unit.flags & UNIT_ATT)                  /* if unit is attached (or listening) */
        sim_activate(&sca_unit, sca_iwait);         /* poll for service. Must do this here as BOOT clears activity queue before resetting all devices */

    return SCPE_OK;
}

/*********************************************************************************************
 * sca_attach - attach the SCA device
 *********************************************************************************************/

static t_stat sca_attach (UNIT *uptr, CONST char *cptr)
{
    char host[CBUFSIZE], port[CBUFSIZE];
    t_bool do_listen;
    char name[4*CBUFSIZE];
    t_stat r;

    do_listen = sim_switches & SWMASK('L');     /* -l means listen mode */

    if (sca_unit.flags & UNIT_ATT)              /* if already attached, detach */
        detach_unit(&sca_unit);

    if (do_listen) {                            /* if listen mode, string specifies port number (only; otherwise it's a dummy argument) */
        if (sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), SCA_DEFAULT_PORT, NULL))
            return SCPE_ARG;
        if ((0 == strcmp(port, cptr)) && (0 == strcmp(port, "dummy")))
            strlcpy(port, SCA_DEFAULT_PORT, sizeof (port));

        snprintf(sca_port, sizeof (sca_port) - 1, "%s%s%s:%s", strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "", port);

        /* else if nondigits specified, ignore... but the command has to have something there otherwise the core scp */
        /* attach_cmd() routine complains "too few arguments". */

        sca_lsock = sim_master_sock(sca_port, &r);
        if (r != SCPE_OK)
            return r;
        if (sca_lsock == INVALID_SOCKET)
            return SCPE_OPENERR;
        
        SETBIT(sca_unit.flags, UNIT_LISTEN);    /* note that we are listening, not yet connected */

        name[sizeof (name) - 1] = '\0';
        snprintf(name, sizeof (name) - 1, "(Listening on port %s)", sca_port);
        sca_unit.filename = mstring(name);
        printf("%s\n", sca_unit.filename);

    }
    else {
        while (*cptr && *cptr <= ' ')
            cptr++;

        if (! *cptr)
            return SCPE_2FARG;

        if (sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), SCA_DEFAULT_PORT, NULL))
            return SCPE_ARG;
        if ((0 == strcmp(cptr, port)) && (0 == strcmp(host, ""))) {
            strlcpy(host, port, sizeof (host));
            strlcpy(port, SCA_DEFAULT_PORT, sizeof (port));
        }

        snprintf(sca_port, sizeof (sca_port) - 1, "%s%s%s:%s", strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "", port);

        if ((sca_sock = sim_connect_sock(sca_port, NULL, NULL)) == INVALID_SOCKET)
            return SCPE_OPENERR;

        /* sim_connect_sock() sets socket to nonblocking before initiating the connect, so
         * the connect is pending when it returns. For outgoing connections, the attach command should wait
         * until the connection succeeds or fails. We use "sim_check_conn" to wait and find out which way it goes...
         */

        while (0 == sim_check_conn(sca_sock, 0))/* wait for connection to complete or fail */
            sim_os_ms_sleep(1000);

        if (1 == sim_check_conn(sca_sock, 0)) { /* sca_sock appears in "writable" set -- connect completed */
            name[sizeof (name) - 1] = '\0';
            snprintf(name, sizeof (name) - 1, "%s%s%s:%s", strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "", port);
            sca_unit.filename = mstring(name);
            SETBIT(sca_dsw, SCA_DSW_READY);
        }
        else {                                  /* sca_sock appears in "error" set -- connect failed */
            sim_close_sock(sca_sock);
            sca_sock = INVALID_SOCKET;
            return SCPE_OPENERR;
        }
    }
    
    /* set up socket connect or listen. on success, set UNIT_ATT.
     * If listen mode, set UNIT_LISTEN. sca_svc will poll for connection
     * If connect mode, set dsw SCA_DSW_READY to indicate connection is up
     */
    
    SETBIT(sca_unit.flags, UNIT_ATT);                   /* record successful socket binding */

    sca_state = SCA_STATE_IDLE;
    sim_activate(&sca_unit, sca_iwait);                 /* start polling for service */

    sca_n2send = 0;                                     /* clear buffers */
    sca_nrcvd  = 0;
    sca_rcvptr = 0;
    sca_nsyns  = 0;

    return SCPE_OK;
}

/*********************************************************************************************
 * sca_detach - detach the SCA device
 *********************************************************************************************/

static t_stat sca_detach (UNIT *uptr)
{
    if ((sca_unit.flags & UNIT_ATT) == 0)
        return SCPE_OK;

    sca_flush();

    sca_state = SCA_STATE_IDLE;                 /* stop processing during service calls */
    sim_cancel(&sca_unit);                      /* stop polling for service */

    CLRBIT(sca_dsw, SCA_DSW_READY);             /* indicate offline */

    if (sca_sock != INVALID_SOCKET) {           /* close connected socket */
        sim_close_sock(sca_sock);
        sca_sock = INVALID_SOCKET;
    }
    if (sca_lsock != INVALID_SOCKET) {          /* close listening socket */
        sim_close_sock(sca_lsock);
        sca_lsock = INVALID_SOCKET;
    }
    
    free(sca_unit.filename);
    sca_unit.filename = NULL;

    CLRBIT(sca_unit.flags, UNIT_ATT|UNIT_LISTEN);

    return SCPE_OK;
}

/*********************************************************************************************
 * sca_check_connect - see if an incoming socket connection has com
 *********************************************************************************************/

static void sca_check_connect (void)
{
    char *connectaddress;

    if ((sca_sock = sim_accept_conn(sca_lsock, &connectaddress)) == INVALID_SOCKET)
        return;

    printf("(SCA connection from %s)\n", connectaddress);

    if (sca_unit.filename != NULL)
        free(sca_unit.filename);

    sca_unit.filename = connectaddress;

    SETBIT(sca_dsw, SCA_DSW_READY);                     /* indicate active connection  */

    if (sca_dsw & SCA_DSW_AUTOANSWER_ENABLED)           /* if autoanswer was enabled, I guess we should give them an interrupt. Untested. */
        sca_interrupt(SCA_DSW_AUTOANSWER_REQUEST);
}

/*********************************************************************************************
 *  sca_check_indata - try to fill receive buffer from socket
 *********************************************************************************************/

static void sca_check_indata (void)
{
    int nbytes;

    sca_rcvptr = 0;                             /* reset pointers and count */
    sca_nrcvd  = 0;

#ifdef FAKE_SCA

    nbytes = 5;                                 /* FAKE: receive SYN SYN SYN SYN DLE ACK0 */
    sca_rcvbuf[0] = 0x32;
    sca_rcvbuf[1] = 0x32;
    sca_rcvbuf[2] = 0x32;
    sca_rcvbuf[3] = 0x10;
    sca_rcvbuf[4] = 0x70;

#else
                                                /* read socket; 0 is returned if no data is available */
    nbytes = sim_read_sock(sca_sock, (char *) sca_rcvbuf, SCA_RCVBUF_SIZE);

#endif

    if (nbytes < 0)
        sca_socket_error();
    else                                        /* zero or more */
        sca_nrcvd = nbytes;

#if (DEBUG_SCA & DEBUG_SCA_CHECK_INDATA)
    if (sca_nrcvd > 0)
        printf("* SCA_CHECK_INDATA %d byte%s\n", sca_nrcvd, (sca_nrcvd == 1) ? "" : "s");
#endif
}

/*********************************************************************************************
 * sca_svc - handled scheduled event. This will presumably be scheduled frequently, and can check
 * for incoming data, reasonableness of initiating a write interrupt, timeouts etc.
 *********************************************************************************************/

static t_stat sca_svc (UNIT *uptr)
{
    t_bool timeout;
    int msec_now;
    int i;

    /* if not connected, and if in wait-for-connection mode, check for connection attempt */
    if ((sca_unit.flags & UNIT_LISTEN) && ! (sca_dsw & SCA_DSW_READY))
        sca_check_connect();

    if (any_timer_running) {
        msec_now = sim_os_msec();

        timeout = FALSE;
        for (i = 0; i < 3; i++) {
            if (sca_timer_state[i] == SCA_TIMER_RUNNING && msec_now >= sca_timer_endtime[i]) {
                timeout = TRUE;
                sca_timer_state[i] = SCA_TIMER_TIMEDOUT;
#if (DEBUG_SCA & DEBUG_SCA_TIMERS)
                printf("+ SCA_TIMER %d timed out\n", i);
#endif

                if (i == TIMER_035S && sca_timer_trigger) {
                    sca_timer_trigger = FALSE;                                      /* uninhibit the other two timers */
                    sca_toggle_timer(TIMER_3S,   msec_now);
                    sca_toggle_timer(TIMER_125S, msec_now);
                }
            }
        }

        if (timeout)
            sca_interrupt(SCA_DSW_TIMEOUT);

        any_timer_running = (sca_timer_state[0]| sca_timer_state[1] | sca_timer_state[2]) & SCA_TIMER_RUNNING;
    }

    if (sca_dsw & SCA_DSW_READY) {              /* if connected */

        /* if rcvd data buffer is empty, and if in one of the receive states, checÄk for arrival of received data */
        if (in_receive_state() && sca_rcvptr >= sca_nrcvd)
            sca_check_indata();

        switch (sca_state) {
            case SCA_STATE_IDLE:
                break;

            case SCA_STATE_TURN_SEND:
                /* has enough time gone by yet? if so... */
                sca_state = SCA_STATE_SEND1;
                sca_interrupt(SCA_DSW_WRITE_RESPONSE);
                break;

            case SCA_STATE_SEND_SYNC:
                sca_transmit_byte(sichar);
                break;

            case SCA_STATE_SEND1:
                sca_transmit_byte(sichar);          /* character interval has passed with no character written? character gap check */
                sca_interrupt(SCA_DSW_CHECK);       /* send an idle character (maybe? for socket IO maybe send nothing) and interrupt */

                if (idles_since_last_write >= 0) {
                    if (++idles_since_last_write >= SCA_IDLE_THRESHHOLD) {
                        sca_flush();
                        idles_since_last_write = -1;    /* don't send a short packet again until real data gets written again */
                        sca_nsyns = 0;                  /* resume sending syns if output starts up again */
                    }
                }
                break;

            case SCA_STATE_SEND2:
                sca_state = SCA_STATE_SEND1;            /* character has been sent. Schedule another transmit */
                sca_interrupt(SCA_DSW_WRITE_RESPONSE);
                break;

            case SCA_STATE_TURN_RECEIVE:
                /* has enough time gone by yet? if so... */
                sca_state = SCA_STATE_RECEIVE_SYNC;     /* assume a character is coming in */
                break;

            case SCA_STATE_RECEIVE_SYNC:
                if (sca_rcvptr < sca_nrcvd) {
                    rcvd_char = sca_rcvbuf[sca_rcvptr++];
#if (DEBUG_SCA & DEBUG_SCA_RECEIVE_SYNC)
                    printf("* SCA rcvd %02x %s\n", rcvd_char, (rcvd_char == sichar) ? "sync1" : "ignored");
#endif
                    if (in_bsc_mode()) {
                        if (rcvd_char == sichar)            /* count the SYN but don't interrupt */
                            sca_state = SCA_STATE_RECEIVE_SYNC2;
                    }
                }
                break;

            case SCA_STATE_RECEIVE_SYNC2:
                if (sca_rcvptr < sca_nrcvd) {
                    rcvd_char = sca_rcvbuf[sca_rcvptr++];
#if (DEBUG_SCA & DEBUG_SCA_RECEIVE_SYNC)
                    printf("* SCA rcvd %02x %s\n", rcvd_char, (rcvd_char == sichar) ? "sync2" : "ignored");
#endif
                    if (in_bsc_mode()) {
                        if (rcvd_char == sichar)            /* count the SYN but don't interrupt */
                            sca_state = SCA_STATE_RECEIVE_SYNC3;
                    }
                }
                break;

            case SCA_STATE_RECEIVE_SYNC3:
            case SCA_STATE_RECEIVE1:
                if (sca_rcvptr < sca_nrcvd) {
                    rcvd_char = sca_rcvbuf[sca_rcvptr++];

                    if (sca_state == SCA_STATE_RECEIVE_SYNC3 && rcvd_char == sichar) {
                        /* we're ready for data, but we're still seeing SYNs */
#if (DEBUG_SCA & DEBUG_SCA_RECEIVE_SYNC)
                        printf("* SCA rcvd %02x extra sync\n", rcvd_char);
#endif
                    }
                    else {
#if (DEBUG_SCA & DEBUG_SCA_RECEIVE_DATA)
                        printf("* SCA rcvd %02x\n", rcvd_char);
#endif
                        sca_interrupt(SCA_DSW_READ_RESPONSE);
                        sca_state = SCA_STATE_RECEIVE2;
                    }
                }
                /* otherwise remain in state until data becomes available */
                break;

            case SCA_STATE_RECEIVE2:                    /* if we are still in this state when another service interval has passed */
                if (sca_rcvptr < sca_nrcvd) {
                    rcvd_char = sca_rcvbuf[sca_rcvptr++];

                    sca_interrupt(SCA_DSW_CHECK);       /* overrun error */
                    sca_state = SCA_STATE_RECEIVE1;     /* another character will come soon */
                }
                break;

            default:
                printf("Simulator error: unknown state %d in sca_svc\n", sca_state);
                sca_state = SCA_STATE_IDLE;
                break;
        }
    }
                                                /* schedule service again */
    sim_activate(&sca_unit, (sca_state == SCA_STATE_IDLE) ? sca_iwait : sca_cwait);

    return SCPE_OK;
}

/*********************************************************************************************
 * sca_toggle_timer - toggle a given timer's running/inhibited state for XIO_CONTROL
 *********************************************************************************************/

static void sca_toggle_timer (int n, int msec_now)
{
    if (sca_timer_state[n] == SCA_TIMER_RUNNING && sca_timer_trigger) {
        sca_timer_state[n] = SCA_TIMER_INHIBITED;
        sca_timer_timeleft[n] = sca_timer_endtime[n] - msec_now;        /* save time left */
#if (DEBUG_SCA & DEBUG_SCA_TIMERS)
        printf("+ SCA_TIMER %d inhibited\n", n);
#endif
    }
    else if (sca_timer_state[n] == SCA_TIMER_INHIBITED && ! sca_timer_trigger) {
        sca_timer_state[n] = SCA_TIMER_RUNNING;
        sca_timer_endtime[n] = sca_timer_timeleft[n] + msec_now;        /* compute new endtime */
#if (DEBUG_SCA & DEBUG_SCA_TIMERS)
        printf("+ SCA_TIMER %d uninhibited\n", n);
#endif
    }
}

static void sca_start_timer (int n, int msec_now)
{
    sca_timer_state[n]   = SCA_TIMER_RUNNING;
    sca_timer_endtime[n] = sca_timer_msec[n] + msec_now;
    any_timer_running    = TRUE;
#if (DEBUG_SCA & DEBUG_SCA_TIMERS)
    printf("+ SCA_TIMER %d started\n", n);
#endif
}

static void sca_halt_timer (int n)
{
#if (DEBUG_SCA & DEBUG_SCA_TIMERS)
    if (sca_timer_state[n] != SCA_TIMER_INACTIVE)
        printf("+ SCA_TIMER %d stopped\n", n);
#endif

    sca_timer_state[n] = SCA_TIMER_INACTIVE;
}

/*********************************************************************************************
 * sca_start_transmit - initiate transmit mode, from XIO_INITR or XIO_CONTROL (sync mode)
 *********************************************************************************************/

void sca_start_transmit (int32 iocc_addr, int32 modify)
{
    sca_flush();
    sca_nsyns = 0;                          /* reset SYN suppression */

    /* Address bits are used to reset DSW conditions. */

    if (modify & 0x40)                      /* bit 9. If set, reset all conditions */
        iocc_addr = 0xD800;

    iocc_addr &= 0xD800;                    /* look at just bits 0, 1, 3 and 4 */
    if (iocc_addr) {                        /* if set, these bits clear DSW conditions */
        CLRBIT(sca_dsw, iocc_addr);
        CLRBIT(ILSW[1], ILSW_1_SCA);        /* and I assume clear the interrupt condition too? (Seems unlikely that INITW would */
    }                                       /* be used in an interrupt service routine before SENSE, but who knows?) */

    if (! in_send_state()) {
        sca_state = SCA_STATE_TURN_SEND;    /* begin line turnaround */
    }
    else {
        sca_state = SCA_STATE_SEND1;        /* line is  */
        sca_interrupt(SCA_DSW_WRITE_RESPONSE);
    }

    SETBIT(sca_dsw, SCA_DSW_BUSY);          /* SCA is now busy, in transmit mode */

    sim_cancel(&sca_unit);
    sim_activate(&sca_unit, sca_cwait);     /* start polling frequently */
}

/*********************************************************************************************
 * xio_sca - handle SCA XIO command
 *********************************************************************************************/

void xio_sca (int32 iocc_addr, int32 func, int32 modify)
{
    char msg[80];
    int i, msec_now;

    switch (func) {
        case XIO_READ:                              /* ***** XIO_READ - store last-received character to memory */
#if (DEBUG_SCA & DEBUG_SCA_XIO_READ)
            printf("SCA RD  addr %04x mod %02x rcvd_char %02x\n", iocc_addr, modify, rcvd_char);
#endif
            if (modify & 0x03) {                    /* bits 14 and 15 */
#if (DEBUG_SCA & DEBUG_SCA_XIO_READ)
                printf("(rd diag)\n");
#endif
                /* if either of low two modifier bits is set, reads diagnostic words. whatever that is */
            }
            else {
                WriteW(iocc_addr, rcvd_char << 8);  /* always return last received character */

                /* note: in read mode, read w/o interrupt (or two reads after an interrupt) causes a check
                 * so here we have to check the current state and possibly cause an interrupt
                 * Failure to have read before another arrives (overrun) also causes a check.
                 */

                if (sca_state == SCA_STATE_RECEIVE2)/* XIO_READ should only be done (and only once) after a character interrupt */
                    sca_state = SCA_STATE_RECEIVE1; /* assume another character is coming in -- wait for it */
                else
                    sca_interrupt(SCA_DSW_CHECK);
            }
            break;

        case XIO_WRITE:                             /* ***** XIO_WRITE - transfer character from memory to output shift register */
#if (DEBUG_SCA & DEBUG_SCA_XIO_WRITE)
            printf("SCA WRT addr %04x (%04x) mod %02x\n", iocc_addr, M[iocc_addr & mem_mask], modify);
#endif
            if (modify & 0x01) {                    /* bit 15 */
                /* clear audible alarm trigger */
#if (DEBUG_SCA & DEBUG_SCA_XIO_WRITE)
                printf("(clr audible alarm trigger)\n");
#endif
            }
            /* else? or can they all operate in parallel? */
            if (modify & 0x02) {                    /* bit 14 */
                /* set audible alarm trigger */
#if (DEBUG_SCA & DEBUG_SCA_XIO_WRITE)
                printf("(set audible alarm trigger)\n");
#endif
            }
            /* else? */
            if (modify & 0x04) {                    /* bit 13 */
#if (DEBUG_SCA & DEBUG_SCA_XIO_WRITE)
                printf("(set SYN)\n");
#endif
                /* set sync/idle character */
                sichar = (uint8) (ReadW(iocc_addr) >> 8);
                sca_nsyns = 0;                      /* reset SYN suppression */
            }
            /* else? does presence of mod bit preclude sending a character? */ 
            if ((modify & 0x07) == 0) {             /* no modifiers */
                /* transmit character --
                 * note: in write mode, failure to write soon enough after a write response interrupt causes a check
                 * Also, writing w/o interrupt (or two writes after an interrupt) causes a check
                 * so again, here we have to check the state to be sure that a write is appropriate
                 *
                 * note that in this simulator, we transmit the character immediately on XIO_WRITE. Therefore,
                 * there is no need to delay an end-operation function (XIO_CONTROL bit 13) until after a character time
                 */

                idles_since_last_write = 0;

                switch (sca_state) {
                    case SCA_STATE_SEND_SYNC:
                    case SCA_STATE_SEND1:
                        sca_transmit_byte((uint8) (M[iocc_addr & mem_mask] >> 8));
                        sca_state = SCA_STATE_SEND2;
                        sim_cancel(&sca_unit);
                        sim_activate(&sca_unit, sca_cwait);     /* schedule service after character write time */
                        break;

                    case SCA_STATE_SEND2:
                        sca_interrupt(SCA_DSW_CHECK);           /* write issued while a character is in progress out? write overrun */
                        break;

                    case SCA_STATE_IDLE:                        /* wrong time to issue a write, incorrect sca state */
                    default:
                        sca_flush();
                        sca_interrupt(SCA_DSW_CHECK);           /* ??? or does this just perform a line turnaround and start transmission? */
                        break;
                }

            }
            break;

        case XIO_CONTROL:                           /* ***** XIO_CONTROL - manipulate interface state */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
            printf("SCA CTL addr %04x mod %02x\n", iocc_addr, modify);
#endif
            if (modify & 0x80) {                    /* bit 8 */
                /* enable auto answer */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(enable autoanswer)\n");
#endif
                SETBIT(sca_unit.flags, UNIT_AUTOANSWER);
                SETBIT(sca_dsw, SCA_DSW_AUTOANSWER_ENABLED);
            }

            if (modify & 0x40) {                    /* bit 9 */
                /* disable auto answer */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(disable autoanswer)\n");
#endif
                CLRBIT(sca_unit.flags, UNIT_AUTOANSWER);
                CLRBIT(sca_dsw, SCA_DSW_AUTOANSWER_ENABLED);
            }

            if (modify & 0x20) {                    /* bit 10 */
                /* toggle timers, inhibit->run or run->inhibit */
#if (DEBUG_SCA & (DEBUG_SCA_XIO_CONTROL|DEBUG_SCA_TIMERS))
                printf("(toggle timers)\n");
#endif
                msec_now = sim_os_msec();

                if (in_bsc_mode()) {
                    sca_timer_trigger = ! sca_timer_trigger;    /* toggle the timer trigger */
                    if (sca_timer_trigger)                      /* if we've just set it, we're stopping the other timers and  */
                        sca_start_timer(TIMER_035S, msec_now);  /* starting the 0.35 sec timer */
                    else
                        sca_halt_timer(TIMER_035S);
                }

                sca_toggle_timer(TIMER_3S, msec_now);           /* toggle the 3 sec and 1.35 sec timers accordingly */
                sca_toggle_timer(TIMER_125S, msec_now);

                any_timer_running = (sca_timer_state[0]| sca_timer_state[1] | sca_timer_state[2]) & SCA_TIMER_RUNNING;
            }

            if (modify & 0x10) {                    /* bit 11 */
                /* enable sync mode. See references to this in FC manual
                 * In STR mode only, sends a constant stream of SYN's without any CPU intervention
                 * In BSC mode, appears to start the 1.25 second timer and otherwise act like INITW?
                 */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(enable sync mode)\n");
#endif

                if (in_bsc_mode()) {                /* in bsc mode start the 1.25 second timer. not sure what resets it?!? */
                    if (! in_send_state())          /* also may cause a line turnaround */
                        sca_start_transmit(iocc_addr, 0);

                    sca_start_timer(TIMER_125S, sim_os_msec());
                }
            }

            if (modify & 0x08) {                    /* bit 12 */
                /* diagnostic mode. What does this do?!? */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(diag mode)\n");
#endif
            }

            if (modify & 0x04) {                    /* bit 13 */
                /* end operation (reset adapter. See references to this in FC manual). In transmit mode the real adapter delays this
                 * function until current character has been sent. We don't need to do that as the character got buffered for transmission
                 * immediately on XIO_WRITE.
                 */

#if (DEBUG_SCA & (DEBUG_SCA_XIO_CONTROL|DEBUG_SCA_XIO_INITR|DEBUG_SCA_XIO_INITW))
                printf("(end operation)\n");
#endif
                sca_state = SCA_STATE_IDLE;
                sca_timer_state[0] = sca_timer_state[1] = sca_timer_state[2] = SCA_TIMER_INACTIVE;
                any_timer_running = FALSE;
                sca_timer_trigger = FALSE;
                sca_nsyns = 0;                      /* reset SYN suppression */
                CLRBIT(sca_dsw, SCA_DSW_BUSY);
            }

            if (modify & 0x02) {                    /* bit 14 */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(6 bit frame)\n");
#endif
                /* set six bit character frame. This is reset to 8 bits at every line turnaround */
                sca_frame = 6;
            }

            if (modify & 0x01) {                    /* bit 15 */
#if (DEBUG_SCA & DEBUG_SCA_XIO_CONTROL)
                printf("(7 bit frame)\n");
#endif
                /* set seven bit character frame. This is reset to 8 bits at every line turnaround */
                sca_frame = 7;
            }

            sca_flush();
            break;

        case XIO_INITW:                             /* ***** XIO_INITW - put SCA in transmit mode */
#if (DEBUG_SCA & DEBUG_SCA_XIO_INITW)
            printf("SCA INITW addr %04x mod %02x\n", iocc_addr, modify);
#endif
            /* enter transmit mode. Causes line turnaround. Resets frame to 8 bits. */
            /* (may cause syncing, may involve a fixed timeout?) */
            sca_frame = 8;
            sca_start_transmit(iocc_addr, modify);  /* this code pulled out to a subroutine cuz transmit can be started from XIO_CONTROL too */
            break;

        case XIO_INITR:                             /* ***** XIO_INITR - put SCA in receive mode */
#if (DEBUG_SCA & DEBUG_SCA_XIO_INITR)
            printf("SCA INITR addr %04x mod %02x\n", iocc_addr, modify);
#endif
            sca_flush();

            sca_nrcvd = sca_rcvptr = 0;             /* discard any data previously read! */
            sca_nsyns = 0;                          /* reset SYN suppression */

            /* enter receive mode. Causes line turnaround (e.g. resets to 8 bit frame). Modifier bits are used here too */
            /* (may cause syncing, may involve a fixed timeout?) */

            sca_frame = 8;
            if (! in_receive_state())
                sca_state = SCA_STATE_TURN_RECEIVE; /* begin line turnaround */
            else
                sca_state = SCA_STATE_RECEIVE_SYNC;

            SETBIT(sca_dsw, SCA_DSW_BUSY);          /* SCA is now busy, in receive mode */

            if (in_bsc_mode())                      /* in BSC mode, start the 3 second timer when we enter receive mode */
                sca_start_timer(TIMER_3S, sim_os_msec());

            break;

        case XIO_SENSE_DEV:                         /* ***** XIO_SENSE_DEV - read device status word */
#if (DEBUG_SCA & DEBUG_SCA_XIO_SENSE_DEV)
            printf("SCA SNS mod %02x dsw %04x\n", modify, sca_dsw);
#endif
            ACC = sca_dsw;                          /* return DSW in accumulator */

            if (modify & 0x01) {                    /* bit 15: reset interrupts */
#if (DEBUG_SCA & DEBUG_SCA_XIO_SENSE_DEV)
                printf("(reset interrupts)\n");
#endif
                CLRBIT(sca_dsw, SCA_DSW_READ_RESPONSE | SCA_DSW_WRITE_RESPONSE | SCA_DSW_CHECK | SCA_DSW_TIMEOUT | SCA_DSW_AUTOANSWER_REQUEST);
                CLRBIT(ILSW[1], ILSW_1_SCA);
            }

            if (modify & 0x02) {                    /* bit 14: restart running timers */
#if (DEBUG_SCA & (DEBUG_SCA_XIO_SENSE_DEV|DEBUG_SCA_TIMERS))
                printf("(restart timers)\n");
#endif
                msec_now = sim_os_msec();           /* restart "any running timer?" All three, really? */
                for (i = 0; i < 3; i++)
                    if (sca_timer_state[i] == SCA_TIMER_RUNNING || sca_timer_state[i] == SCA_TIMER_TIMEDOUT)
                        sca_start_timer(i, msec_now);
            }
            break;

        default:
            sprintf(msg, "Invalid SCA XIO function %x", func);
            xio_error(msg);
    }
}
