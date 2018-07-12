/* sim_tmxr.h: terminal multiplexer definitions

   Copyright (c) 2001-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Based on the original DZ11 simulator by Thord Nilson, as updated by
   Arthur Krewat.

   10-Oct-12    MP      Added extended attach support for serial, per line 
                        listener and outgoing connections
   17-Jan-11    MP      Added buffered line capabilities
   20-Nov-08    RMS     Added three new standardized SHOW routines
   07-Oct-08    JDB     Added serial port support to TMXR, TMLN
   27-May-08    JDB     Added lnorder to TMXR structure,
                        added tmxr_set_lnorder and tmxr_set_lnorder
   14-May-08    JDB     Added dptr to TMXR structure
   04-Jan-04    RMS     Changed TMXR ldsc to be pointer to linedesc array
                        Added tmxr_linemsg, logging (from Mark Pizzolato)
   29-Dec-03    RMS     Added output stall support, increased buffer size
   22-Dec-02    RMS     Added break support (from Mark Pizzolato)
   20-Aug-02    RMS     Added tmxr_open_master, tmxr_close_master, tmxr.port
   30-Dec-01    RMS     Renamed tmxr_fstatus, added tmxr_fstats
   20-Oct-01    RMS     Removed tmxr_getchar, formalized buffer guard,
                        added tmxr_rqln, tmxr_tqln
*/

#ifndef SIM_TMXR_H_
#define SIM_TMXR_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef SIMH_SERHANDLE_DEFINED
#define SIMH_SERHANDLE_DEFINED 0
typedef struct SERPORT *SERHANDLE;
#endif

#include "sim_sock.h"

#define TMXR_V_VALID    15
#define TMXR_VALID      (1 << TMXR_V_VALID)
#define TMXR_MAXBUF     256                             /* buffer size */

#define TMXR_DTR_DROP_TIME 500                          /* milliseconds to drop DTR for 'pseudo' modem control */
#define TMXR_MODEM_RING_TIME 3                          /* seconds to wait for DTR for incoming connections */
#define TMXR_DEFAULT_CONNECT_POLL_INTERVAL 1            /* seconds between connection polls */

#define TMXR_DBG_XMT    0x0010000                        /* Debug Transmit Data */
#define TMXR_DBG_RCV    0x0020000                        /* Debug Received Data */
#define TMXR_DBG_RET    0x0040000                        /* Debug Returned Received Data */
#define TMXR_DBG_MDM    0x0080000                        /* Debug Modem Signals */
#define TMXR_DBG_CON    0x0100000                        /* Debug Connection Activities */
#define TMXR_DBG_ASY    0x0200000                        /* Debug Asynchronous Activities */
#define TMXR_DBG_TRC    0x0400000                        /* Debug trace routine calls */
#define TMXR_DBG_PXMT   0x0800000                        /* Debug Transmit Packet Data */
#define TMXR_DBG_PRCV   0x1000000                        /* Debug Received Packet Data */
#define TMXR_DBG_EXP    0x2000000                        /* Debug Expect Activities */
#define TMXR_DBG_SEND   0x4000000                        /* Debug Send Activities */

/* Modem Control Bits */

#define TMXR_MDM_DTR        0x01    /* Data Terminal Ready */
#define TMXR_MDM_RTS        0x02    /* Request To Send     */
#define TMXR_MDM_DCD        0x04    /* Data Carrier Detect */
#define TMXR_MDM_RNG        0x08    /* Ring Indicator      */
#define TMXR_MDM_CTS        0x10    /* Clear To Send       */
#define TMXR_MDM_DSR        0x20    /* Data Set Ready      */
#define TMXR_MDM_INCOMING   (TMXR_MDM_DCD|TMXR_MDM_RNG|TMXR_MDM_CTS|TMXR_MDM_DSR)  /* Settable Modem Bits */
#define TMXR_MDM_OUTGOING   (TMXR_MDM_DTR|TMXR_MDM_RTS)  /* Settable Modem Bits */

/* Unit flags */

#define TMUF_V_NOASYNCH   (UNIT_V_UF + 12)              /* Asynch Disabled unit */
#define TMUF_NOASYNCH     (1u << TMUF_V_NOASYNCH)       /* This flag can be defined */
                                                        /* statically in a unit's flag field */
                                                        /* This will disable the unit from */
                                                        /* supporting asynchronmous mux behaviors */
/* Receive line speed limits */

#define TMLN_SPD_50_BPS     200000 /* usec per character */
#define TMLN_SPD_75_BPS     133333 /* usec per character */
#define TMLN_SPD_110_BPS     90909 /* usec per character */
#define TMLN_SPD_134_BPS     74626 /* usec per character */
#define TMLN_SPD_150_BPS     66666 /* usec per character */
#define TMLN_SPD_300_BPS     33333 /* usec per character */
#define TMLN_SPD_600_BPS     16666 /* usec per character */
#define TMLN_SPD_1200_BPS     8333 /* usec per character */
#define TMLN_SPD_1800_BPS     5555 /* usec per character */
#define TMLN_SPD_2000_BPS     5000 /* usec per character */
#define TMLN_SPD_2400_BPS     4166 /* usec per character */
#define TMLN_SPD_3600_BPS     2777 /* usec per character */
#define TMLN_SPD_4800_BPS     2083 /* usec per character */
#define TMLN_SPD_7200_BPS     1388 /* usec per character */
#define TMLN_SPD_9600_BPS     1041 /* usec per character */
#define TMLN_SPD_19200_BPS     520 /* usec per character */
#define TMLN_SPD_38400_BPS     260 /* usec per character */
#define TMLN_SPD_57600_BPS     173 /* usec per character */
#define TMLN_SPD_76800_BPS     130 /* usec per character */
#define TMLN_SPD_115200_BPS     86 /* usec per character */



typedef struct tmln TMLN;
typedef struct tmxr TMXR;
struct loopbuf {
    int32               bpr;                          /* xmt buf remove */
    int32               bpi;                          /* xmt buf insert */
    int32               size;
    };

struct tmln {
    int                 conn;                           /* line connected flag */
    SOCKET              sock;                           /* connection socket */
    char                *ipad;                          /* IP address */
    SOCKET              master;                         /* line specific master socket */
    char                *port;                          /* line specific listening port */
    int32               sessions;                       /* count of tcp connections received */
    uint32              cnms;                           /* conn time */
    int32               tsta;                           /* Telnet state */
    int32               rcve;                           /* rcv enable */
    int32               xmte;                           /* xmt enable */
    int32               dstb;                           /* disable Telnet binary mode */
    t_bool              notelnet;                       /* raw binary data (no telnet interpretation) */
    uint8               *telnet_sent_opts;              /* Telnet Options which we have sent a DON'T/WON'T */
    int32               rxbpr;                          /* rcv buf remove */
    int32               rxbpi;                          /* rcv buf insert */
    int32               rxbsz;                          /* rcv buffer size */
    int32               rxcnt;                          /* rcv count */
    int32               rxpcnt;                         /* rcv packet count */
    int32               txbpr;                          /* xmt buf remove */
    int32               txbpi;                          /* xmt buf insert */
    int32               txcnt;                          /* xmt count */
    int32               txpcnt;                         /* xmt packet count */
    int32               txdrp;                          /* xmt drop count */
    int32               txstall;                        /* xmt stall count */
    int32               txbsz;                          /* xmt buffer size */
    int32               txbfd;                          /* xmt buffered flag */
    t_bool              modem_control;                  /* line supports modem control behaviors */
    t_bool              port_speed_control;             /* line programmatically sets port speed */
    int32               modembits;                      /* modem bits which are currently set */
    FILE                *txlog;                         /* xmt log file */
    FILEREF             *txlogref;                      /* xmt log file reference */
    char                *txlogname;                     /* xmt log file name */
    char                *rxb;                           /* rcv buffer */
    char                *rbr;                           /* rcv break */
    char                *txb;                           /* xmt buffer */
    uint8               *rxpb;                          /* rcv packet buffer */
    uint32              rxpbsize;                       /* rcv packet buffer size */
    uint32              rxpboffset;                     /* rcv packet buffer offset */
    uint32              rxbps;                          /* rcv bps speed (0 - unlimited) */
    double              bpsfactor;                      /* receive speed factor (scaled to usecs) */
#define USECS_PER_SECOND 1000000.0
    uint32              rxdeltausecs;                   /* rcv inter character min time (usecs) */
    double              rxnexttime;                     /* min time for next receive character */
    uint32              txbps;                          /* xmt bps speed (0 - unlimited) */
    uint32              txdeltausecs;                   /* xmt inter character min time (usecs) */
    double              txnexttime;                     /* min time for next transmit character */
    t_bool              txdone;                         /* sent data complete indicator - private */
    uint8               *txpb;                          /* xmt packet buffer */
    uint32              txpbsize;                       /* xmt packet buffer size */
    uint32              txppsize;                       /* xmt packet packet size */
    uint32              txppoffset;                     /* xmt packet buffer offset */
    TMXR                *mp;                            /* back pointer to mux */
    char                *serconfig;                     /* line config */
    SERHANDLE           serport;                        /* serial port handle */
    t_bool              ser_connect_pending;            /* serial connection notice pending */
    SOCKET              connecting;                     /* Outgoing socket while connecting */
    char                *destination;                   /* Outgoing destination address:port */
    t_bool              loopback;                       /* Line in loopback mode */
    t_bool              halfduplex;                     /* Line in half-duplex mode */
    t_bool              datagram;                       /* Line is datagram packet oriented */
    t_bool              packet;                         /* Line is packet oriented */
    int32               lpbpr;                          /* loopback buf remove */
    int32               lpbpi;                          /* loopback buf insert */
    int32               lpbcnt;                         /* loopback buf used count */
    int32               lpbsz;                          /* loopback buffer size */
    char                *lpb;                           /* loopback buffer */
    UNIT                *uptr;                          /* input polling unit (default to mp->uptr) */
    UNIT                *o_uptr;                        /* output polling unit (default to lp->uptr)*/
    DEVICE              *dptr;                          /* line specific device */
    EXPECT              expect;                         /* Expect rules */
    SEND                send;                           /* Send input state */
    };

struct tmxr {
    int32               lines;                          /* # lines */
    char                *port;                          /* listening port */
    SOCKET              master;                         /* master socket */
    TMLN                *ldsc;                          /* line descriptors */
    int32               *lnorder;                       /* line connection order */
    DEVICE              *dptr;                          /* multiplexer device */
    UNIT                *uptr;                          /* polling unit (connection) */
    char                logfiletmpl[FILENAME_MAX];      /* template logfile name */
    int32               txcount;                        /* count of transmit bytes */
    int32               buffered;                       /* Buffered Line Behavior and Buffer Size Flag */
    int32               sessions;                       /* count of tcp connections received */
    uint32              poll_interval;                  /* frequency of connection polls (seconds) */
    uint32              last_poll_time;                 /* time of last connection poll */
    uint32              ring_start_time;                /* time ring signal was raised */
    char                *ring_ipad;                     /* incoming connection address awaiting DTR */
    SOCKET              ring_sock;                      /* incoming connection socket awaiting DTR */
    t_bool              notelnet;                       /* default telnet capability for incoming connections */
    t_bool              modem_control;                  /* multiplexer supports modem control behaviors */
    t_bool              port_speed_control;             /* multiplexer programmatically sets port speed */
    t_bool              packet;                         /* Lines are packet oriented */
    t_bool              datagram;                       /* Lines use datagram packet transport */
    };

int32 tmxr_poll_conn (TMXR *mp);
t_stat tmxr_reset_ln (TMLN *lp);
t_stat tmxr_detach_ln (TMLN *lp);
int32 tmxr_input_pending_ln (TMLN *lp);
int32 tmxr_getc_ln (TMLN *lp);
t_stat tmxr_get_packet_ln (TMLN *lp, const uint8 **pbuf, size_t *psize);
t_stat tmxr_get_packet_ln_ex (TMLN *lp, const uint8 **pbuf, size_t *psize, uint8 frame_byte);
void tmxr_poll_rx (TMXR *mp);
t_stat tmxr_putc_ln (TMLN *lp, int32 chr);
t_stat tmxr_put_packet_ln (TMLN *lp, const uint8 *buf, size_t size);
t_stat tmxr_put_packet_ln_ex (TMLN *lp, const uint8 *buf, size_t size, uint8 frame_byte);
void tmxr_poll_tx (TMXR *mp);
int32 tmxr_send_buffered_data (TMLN *lp);
t_stat tmxr_open_master (TMXR *mp, CONST char *cptr);
t_stat tmxr_close_master (TMXR *mp);
t_stat tmxr_connection_poll_interval (TMXR *mp, uint32 seconds);
t_stat tmxr_attach_ex (TMXR *mp, UNIT *uptr, CONST char *cptr, t_bool async);
t_stat tmxr_detach (TMXR *mp, UNIT *uptr);
t_stat tmxr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
char *tmxr_line_attach_string(TMLN *lp);
t_stat tmxr_set_modem_control_passthru (TMXR *mp);
t_stat tmxr_clear_modem_control_passthru (TMXR *mp);
t_stat tmxr_set_port_speed_control (TMXR *mp);
t_stat tmxr_clear_port_speed_control (TMXR *mp);
t_stat tmxr_set_line_port_speed_control (TMXR *mp, int line);
t_stat tmxr_clear_line_port_speed_control (TMXR *mp, int line);
t_stat tmxr_set_get_modem_bits (TMLN *lp, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits);
t_stat tmxr_set_line_loopback (TMLN *lp, t_bool enable_loopback);
t_bool tmxr_get_line_loopback (TMLN *lp);
t_stat tmxr_set_line_halfduplex (TMLN *lp, t_bool enable_loopback);
t_bool tmxr_get_line_halfduplex (TMLN *lp);
t_stat tmxr_set_line_speed (TMLN *lp, CONST char *speed);
t_stat tmxr_set_config_line (TMLN *lp, CONST char *config);
t_stat tmxr_set_line_unit (TMXR *mp, int line, UNIT *uptr_poll);
t_stat tmxr_set_line_output_unit (TMXR *mp, int line, UNIT *uptr_poll);
t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr);
t_stat tmxr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat tmxr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void tmxr_msg (SOCKET sock, const char *msg);
void tmxr_linemsg (TMLN *lp, const char *msg);
void tmxr_linemsgf (TMLN *lp, const char *fmt, ...);
void tmxr_linemsgvf (TMLN *lp, const char *fmt, va_list args);
void tmxr_fconns (FILE *st, const TMLN *lp, int32 ln);
void tmxr_fstats (FILE *st, const TMLN *lp, int32 ln);
t_stat tmxr_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tmxr_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tmxr_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tmxr_dscln (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
int32 tmxr_rqln (const TMLN *lp);
int32 tmxr_tqln (const TMLN *lp);
int32 tmxr_tpqln (const TMLN *lp);
int32 tmxr_txdone_ln (TMLN *lp);
t_bool tmxr_tpbusyln (const TMLN *lp);
t_stat tmxr_set_lnorder (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tmxr_show_lnorder (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tmxr_show_summ (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tmxr_show_cstat (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tmxr_show_lines (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat tmxr_show_open_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* desc);
t_stat tmxr_activate (UNIT *uptr, int32 interval);
t_stat tmxr_activate_abs (UNIT *uptr, int32 interval);
t_stat tmxr_activate_after (UNIT *uptr, uint32 usecs_walltime);
t_stat tmxr_activate_after_abs (UNIT *uptr, uint32 usecs_walltime);
t_stat tmxr_clock_coschedule (UNIT *uptr, int32 interval);
t_stat tmxr_clock_coschedule_abs (UNIT *uptr, int32 interval);
t_stat tmxr_clock_coschedule_tmr (UNIT *uptr, int32 tmr, int32 ticks);
t_stat tmxr_clock_coschedule_tmr_abs (UNIT *uptr, int32 tmr, int32 ticks);
t_stat tmxr_change_async (void);
t_stat tmxr_locate_line_send (const char *dev_line, SEND **snd);
t_stat tmxr_locate_line_expect (const char *dev_line, EXPECT **exp);
t_stat tmxr_locate_line (const char *dev_line, TMLN **lp);
const char *tmxr_send_line_name (const SEND *snd);
const char *tmxr_expect_line_name (const EXPECT *exp);
t_stat tmxr_startup (void);
t_stat tmxr_shutdown (void);
t_stat tmxr_start_poll (void);
t_stat tmxr_stop_poll (void);
void _tmxr_debug (uint32 dbits, TMLN *lp, const char *msg, char *buf, int bufsize);
#define tmxr_debug(dbits, lp, msg, buf, bufsize) do {if (sim_deb && (lp)->mp && (lp)->mp->dptr && ((dbits) & (lp)->mp->dptr->dctrl)) _tmxr_debug (dbits, lp, msg, buf, bufsize); } while (0)
#define tmxr_debug_msg(dbits, lp, msg) do {if (sim_deb && (lp)->mp && (lp)->mp->dptr && ((dbits) & (lp)->mp->dptr->dctrl)) sim_debug (dbits, (lp)->mp->dptr, "%s", msg); } while (0)
#define tmxr_debug_return(lp, val) do {if (sim_deb && (val) && (lp)->mp && (lp)->mp->dptr && (TMXR_DBG_RET & (lp)->mp->dptr->dctrl)) { if ((lp)->rxbps) sim_debug (TMXR_DBG_RET, (lp)->mp->dptr, "Ln%d: 0x%x - Next after: %.0f\n", (int)((lp)-(lp)->mp->ldsc), val, (lp)->rxnexttime); else sim_debug (TMXR_DBG_RET, (lp)->mp->dptr, "Ln%d: 0x%x\n", (int)((lp)-(lp)->mp->ldsc), val); } } while (0)
#define tmxr_debug_trace(mp, msg) do {if (sim_deb && (mp)->dptr && (TMXR_DBG_TRC & (mp)->dptr->dctrl)) sim_debug (TMXR_DBG_TRC, mp->dptr, "%s\n", (msg)); } while (0)
#define tmxr_debug_trace_line(lp, msg) do {if (sim_deb && (lp)->mp && (lp)->mp->dptr && (TMXR_DBG_TRC & (lp)->mp->dptr->dctrl)) sim_debug (TMXR_DBG_TRC, (lp)->mp->dptr, "Ln%d:%s\n", (int)((lp)-(lp)->mp->ldsc), (msg)); } while (0)
#define tmxr_debug_connect(mp, msg) do {if (sim_deb && (mp)->dptr && (TMXR_DBG_CON & (mp)->dptr->dctrl)) sim_debug (TMXR_DBG_CON, mp->dptr, "%s\n", (msg)); } while (0)
#define tmxr_debug_connect_line(lp, msg) do {if (sim_deb && (lp)->mp && (lp)->mp->dptr && (TMXR_DBG_CON & (lp)->mp->dptr->dctrl)) sim_debug (TMXR_DBG_CON, (lp)->mp->dptr, "Ln%d:%s\n", (int)((lp)-(lp)->mp->ldsc), (msg)); } while (0)
t_stat tmxr_add_debug (DEVICE *dptr);

#if defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_IO)
#undef SIM_ASYNCH_MUX
#endif /* defined(SIM_ASYNCH_MUX) && !defined(SIM_ASYNCH_IO) */

#if defined(SIM_ASYNCH_MUX)
#define tmxr_attach(mp, uptr, cptr) tmxr_attach_ex(mp, uptr, cptr, TRUE)
#else
#define tmxr_attach(mp, uptr, cptr) tmxr_attach_ex(mp, uptr, cptr, FALSE)
#endif
#if (!defined(NOT_MUX_USING_CODE))
#define sim_activate tmxr_activate
#define sim_activate_abs tmxr_activate_abs
#define sim_activate_after tmxr_activate_after
#define sim_activate_after_abs tmxr_activate_after_abs
#define sim_clock_coschedule tmxr_clock_coschedule 
#define sim_clock_coschedule_abs tmxr_clock_coschedule_abs
#define sim_clock_coschedule_tmr tmxr_clock_coschedule_tmr
#define sim_clock_coschedule_tmr_abs tmxr_clock_coschedule_tmr_abs
#endif


#ifdef  __cplusplus
}
#endif

#endif /* _SIM_TMXR_H_ */
