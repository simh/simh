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

#ifndef SIMH_SERHANDLE_DEFINED
#define SIMH_SERHANDLE_DEFINED 0
#if defined (_WIN32)                            /* Windows definitions */
typedef void *SERHANDLE;
#else                                           /* all other platforms */
typedef int SERHANDLE;
#endif
#endif

#include "sim_sock.h"

#define TMXR_V_VALID    15
#define TMXR_VALID      (1 << TMXR_V_VALID)
#define TMXR_MAXBUF     256                             /* buffer size */
#define TMXR_GUARD      12                              /* buffer guard */

#define TMXR_DTR_DROP_TIME 500                          /* milliseconds to drop DTR for 'pseudo' modem control */
#define TMXR_CONNECT_POLL_INTERVAL 1000                 /* milliseconds between connection polls */

#define TMXR_DBG_XMT    0x010000                         /* Debug Transmit Data */
#define TMXR_DBG_RCV    0x020000                         /* Debug Received Data */
#define TMXR_DBG_RET    0x040000                         /* Debug Returned Received Data */
#define TMXR_DBG_MDM    0x080000                         /* Debug Modem Signals */
#define TMXR_DBG_CON    0x100000                         /* Debug Connection Activities */
#define TMXR_DBG_ASY    0x200000                         /* Debug Asynchronous Activities */
#define TMXR_DBG_TRC    0x400000                         /* Debug trace routine calls */

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

typedef struct tmln TMLN;
typedef struct tmxr TMXR;

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
    int32               dstb;                           /* disable Tlnt bin */
    t_bool              notelnet;                       /* raw binary data (no telnet interpretation) */
    int32               rxbpr;                          /* rcv buf remove */
    int32               rxbpi;                          /* rcv buf insert */
    int32               rxcnt;                          /* rcv count */
    int32               txbpr;                          /* xmt buf remove */
    int32               txbpi;                          /* xmt buf insert */
    int32               txcnt;                          /* xmt count */
    int32               txdrp;                          /* xmt drop count */
    int32               txbsz;                          /* xmt buffer size */
    int32               txbfd;                          /* xmt buffered flag */
    t_bool              modem_control;                  /* line supports modem control behaviors */
    int32               modembits;                      /* modem bits which are currently set */
    FILE                *txlog;                         /* xmt log file */
    FILEREF             *txlogref;                      /* xmt log file reference */
    char                *txlogname;                     /* xmt log file name */
    char                rxb[TMXR_MAXBUF];               /* rcv buffer */
    char                rbr[TMXR_MAXBUF];               /* rcv break */
    char                *txb;                           /* xmt buffer */
    TMXR                *mp;                            /* back pointer to mux */
    char                *serconfig;                     /* line config */
    SERHANDLE           serport;                        /* serial port handle */
    t_bool              ser_connect_pending;            /* serial connection notice pending */
    SOCKET              connecting;                     /* Outgoing socket while connecting */
    char                *destination;                   /* Outgoing destination address:port */
    UNIT                *uptr;                          /* input polling unit (default to mp->uptr) */
    UNIT                *o_uptr;                        /* output polling unit (default to lp->uptr)*/
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
    uint32              last_poll_time;                 /* time of last connection poll */
    t_bool              notelnet;                       /* default telnet capability for incoming connections */
    t_bool              modem_control;                  /* multiplexer supports modem control behaviors */
    };

int32 tmxr_poll_conn (TMXR *mp);
t_stat tmxr_reset_ln (TMLN *lp);
t_stat tmxr_detach_ln (TMLN *lp);
int32 tmxr_getc_ln (TMLN *lp);
void tmxr_poll_rx (TMXR *mp);
t_stat tmxr_putc_ln (TMLN *lp, int32 chr);
void tmxr_poll_tx (TMXR *mp);
int32 tmxr_send_buffered_data (TMLN *lp);
t_stat tmxr_open_master (TMXR *mp, char *cptr);
t_stat tmxr_close_master (TMXR *mp);
t_stat tmxr_attach_ex (TMXR *mp, UNIT *uptr, char *cptr, t_bool async);
t_stat tmxr_detach (TMXR *mp, UNIT *uptr);
t_stat tmxr_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
char *tmxr_line_attach_string(TMLN *lp);
t_stat tmxr_set_modem_control_passthru (TMXR *mp);
t_stat tmxr_clear_modem_control_passthru (TMXR *mp);
t_stat tmxr_set_get_modem_bits (TMLN *lp, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits);
t_stat tmxr_set_config_line (TMLN *lp, char *config);
t_stat tmxr_set_line_unit (TMXR *mp, int line, UNIT *uptr_poll);
t_stat tmxr_set_line_output_unit (TMXR *mp, int line, UNIT *uptr_poll);
t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr);
t_stat tmxr_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat tmxr_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void tmxr_msg (SOCKET sock, char *msg);
void tmxr_linemsg (TMLN *lp, char *msg);
void tmxr_linemsgf (TMLN *lp, const char *fmt, ...);
void tmxr_fconns (FILE *st, TMLN *lp, int32 ln);
void tmxr_fstats (FILE *st, TMLN *lp, int32 ln);
t_stat tmxr_set_log (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat tmxr_set_nolog (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat tmxr_show_log (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tmxr_dscln (UNIT *uptr, int32 val, char *cptr, void *desc);
int32 tmxr_rqln (TMLN *lp);
int32 tmxr_tqln (TMLN *lp);
t_stat tmxr_set_lnorder (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat tmxr_show_lnorder (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tmxr_show_summ (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tmxr_show_cstat (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tmxr_show_lines (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat tmxr_show_open_devices (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc);
t_stat tmxr_activate (UNIT *uptr, int32 interval);
t_stat tmxr_activate_after (UNIT *uptr, int32 usecs_walltime);
t_stat tmxr_clock_coschedule (UNIT *uptr, int32 interval);
t_stat tmxr_change_async (void);
t_stat tmxr_startup (void);
t_stat tmxr_shutdown (void);
t_stat tmxr_start_poll (void);
t_stat tmxr_stop_poll (void);
void _tmxr_debug (uint32 dbits, TMLN *lp, const char *msg, char *buf, int bufsize);
extern FILE *sim_deb;                                   /* debug file */
#define tmxr_debug(dbits, lp, msg, buf, bufsize) if (sim_deb && (lp)->mp->dptr && ((dbits) & (lp)->mp->dptr->dctrl)) _tmxr_debug (dbits, lp, msg, buf, bufsize); else (void)0
#define tmxr_debug_return(lp, val) if (sim_deb && (val) && (lp)->mp->dptr && (TMXR_DBG_RET & (lp)->mp->dptr->dctrl)) sim_debug (TMXR_DBG_RET, (lp)->mp->dptr, "Ln%d: 0x%x\n", (int)((lp)-(lp)->mp->ldsc), val); else (void)0
#define tmxr_debug_trace(mp, msg) if (sim_deb && (mp)->dptr && (TMXR_DBG_TRC & (mp)->dptr->dctrl)) sim_debug (TMXR_DBG_TRC, mp->dptr, "%s\n", (msg)); else (void)0
#define tmxr_debug_trace_line(lp, msg) if (sim_deb && (lp)->mp && (lp)->mp->dptr && (TMXR_DBG_TRC & (lp)->mp->dptr->dctrl)) sim_debug (TMXR_DBG_TRC, (lp)->mp->dptr, "Ln%d:%s\n", (int)((lp)-(lp)->mp->ldsc), (msg)); else (void)0
#define tmxr_debug_connect(mp, msg) if (sim_deb && (mp)->dptr && (TMXR_DBG_CON & (mp)->dptr->dctrl)) sim_debug (TMXR_DBG_CON, mp->dptr, "%s\n", (msg)); else (void)0
#define tmxr_debug_connect_line(lp, msg) if (sim_deb && (lp)->mp && (lp)->mp->dptr && (TMXR_DBG_CON & (lp)->mp->dptr->dctrl)) sim_debug (TMXR_DBG_CON, (lp)->mp->dptr, "Ln%d:%s\n", (int)((lp)-(lp)->mp->ldsc), (msg)); else (void)0

#if defined(SIM_ASYNCH_IO) && defined(SIM_ASYNCH_MUX)
#define tmxr_attach(mp, uptr, cptr) tmxr_attach_ex(mp, uptr, cptr, TRUE)
#if (!defined(NOT_MUX_USING_CODE))
#define sim_activate tmxr_activate
#define sim_activate_after tmxr_activate_after
#define sim_clock_coschedule tmxr_clock_coschedule 
#endif
#else
#define tmxr_attach(mp, uptr, cptr) tmxr_attach_ex(mp, uptr, cptr, FALSE)
#endif


#endif /* _SIM_TMXR_H_ */
