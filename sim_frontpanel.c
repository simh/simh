/* sim_frontpanel.c: simulator frontpanel API

   Copyright (c) 2015, Mark Pizzolato

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
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.

   05-Feb-15    MP      Initial implementation
   01-Apr-15    MP      Added register indirect, mem_examine and mem_deposit
   03-Apr-15    MP      Added logic to pass simulator startup messages in
                        panel error text if the connection to the simulator
                        shuts down while it is starting.
   04-Apr-15    MP      Added mount and dismount routines to connect and 
                        disconnect removable media

   This module provides interface between a front panel application and a simh
   simulator.  Facilities provide ways to gather information from and to 
   observe and control the state of a simulator.

   The details of the 'wire protocol' are internal to the API interfaces 
   provided here and described in sim_frontpanel.h.  These details are subject 
   to change from one sim_frontpanel version to the next, while all efforts
   will be made to retain any prior sim_frontpanel API interfaces.

*/

#ifdef  __cplusplus
extern "C" {
#endif

#include "sim_frontpanel.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "sim_sock.h"

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#include <winerror.h>
#define sleep(n) Sleep(n*1000)
#define msleep(n) Sleep(n)
#define strtoull _strtoui64
#define CLOCK_REALTIME 0
int clock_gettime(int clk_id, struct timespec *tp)
{
unsigned long long now, unixbase;

unixbase = 116444736;
unixbase *= 1000000000;
GetSystemTimeAsFileTime((FILETIME*)&now);
now -= unixbase;
tp->tv_sec = (long)(now/10000000);
tp->tv_nsec = (now%10000000)*100;
return 0;
}
#else /* NOT _WIN32 */
#include <unistd.h>
#define msleep(n) usleep(1000*n)
#include <sys/wait.h>
#if defined (__APPLE__)
#define HAVE_STRUCT_TIMESPEC 1   /* OSX defined the structure but doesn't tell us */
#endif

/* on HP-UX, CLOCK_REALTIME is enum, not preprocessor define */
#if !defined(CLOCK_REALTIME) && !defined(__hpux)
#define CLOCK_REALTIME 1
#define NEED_CLOCK_GETTIME 1
#if !defined(HAVE_STRUCT_TIMESPEC)
#define HAVE_STRUCT_TIMESPEC 1
#if !defined(_TIMESPEC_DEFINED)
#define _TIMESPEC_DEFINED
struct timespec {
    long   tv_sec;
    long   tv_nsec;
};
#endif /* _TIMESPEC_DEFINED */
#endif /* HAVE_STRUCT_TIMESPEC */
#if defined(NEED_CLOCK_GETTIME)
int clock_gettime(int clk_id, struct timespec *tp)
{
struct timeval cur;
struct timezone foo;

gettimeofday (&cur, &foo);
tp->tv_sec = cur.tv_sec;
tp->tv_nsec = cur.tv_usec*1000;
return 0;
}
#endif /* defined(NEED_CLOCK_GETTIME) */
#endif /* !defined(CLOCK_REALTIME) && !defined(__hpux) */

#endif /* NOT _WIN32 */

typedef struct {
    char *name;
    char *device_name;
    void *addr;
    size_t size;
    int indirect;
    size_t element_count;
    int *bits;
    size_t bit_count;
    } REG;

struct PANEL {
    PANEL                   *parent;        /* Device Panels can have parent panels */
    char                    *path;          /* simulator path */
    char                    *config;
    char                    *device_name;   /* device name */
    char                    *temp_config;
    char                    hostport[64];
    size_t                  device_count;
    PANEL                   **devices;
    SOCKET                  sock;
    size_t                  reg_count;
    REG                     *regs;
    char                    *reg_query;
    int                     new_register;
    size_t                  reg_query_size;
    unsigned long long      array_element_data;
    volatile OperationalState State;
    unsigned long long      simulation_time;
    unsigned long long      simulation_time_base;
    pthread_t               io_thread;
    int                     io_thread_running;
    pthread_mutex_t         io_lock;
    pthread_mutex_t         io_send_lock;
    pthread_mutex_t         io_command_lock;
    int                     command_count;
    int                     io_waiting;
    char                    *io_response;
    char                    *halt_reason;
    size_t                  io_response_data;
    size_t                  io_response_size;
    const char              *completion_string;
    pthread_cond_t          io_done;
    pthread_cond_t          startup_done;
    PANEL_DISPLAY_PCALLBACK callback;
    pthread_t               callback_thread;
    int                     callback_thread_running;
    void                    *callback_context;
    int                     usecs_between_callbacks;
    pthread_t               debugflush_thread;
    int                     debugflush_thread_running;
    unsigned int            sample_frequency;
    unsigned int            sample_dither_pct;
    unsigned int            sample_depth;
    int                     debug;
    char                    *simulator_version;
    int                     radix;
    FILE                    *Debug;
#if defined(_WIN32)
    HANDLE                  hProcess;
    DWORD                   dwProcessId;
#else
    pid_t                   pidProcess;
#endif
    };

/*
 * Thread synchronization model:
 *
 *  Mutex:               Role:
 *   io_lock             Serialize access to panel state variables
 *                        acquired and released in application threads: 
 *                                                  _panel_register_query_string,
 *                                                  _panel_establish_register_bits_collection,
 *                                                  _panel_sendf
 *                        acquired and released in internal threads: 
 *                                                  _panel_callback
 *                                                  _panel_reader
 *   io_send_lock        Serializes writes to a panel's sockets so that complete 
 *                       command/request data can be delivered before another 
 *                       thread attempts to write to the socket.
 *                        acquired and released in: _panel_send
 *   io_command_lock     To serialize frontpanel application command requests
 *                        acquired and released in: _panel_get_registers, 
 *                                                  _panel_sendf_completion
 *
 *  Condition Var:  Sync Mutex:  Purpose & Duration:
 *   io_done        io_lock
 *   startup_done   io_lock      Indicate background thread setup is complete.
 *                               Once signaled, it is immediately destroyed.
 */

static const char *sim_prompt = "sim> ";
static const char *register_repeat_prefix = "repeat every ";
static const char *register_repeat_stop = "repeat stop";
static const char *register_repeat_stop_all = "repeat stop all";
static const char *register_repeat_units = " usecs ";
static const char *register_get_prefix = "show time";
static const char *register_collect_prefix = "collect ";
static const char *register_collect_mid1 = " samples every ";
static const char *register_collect_mid2 = " cycles dither ";
static const char *register_collect_mid3 = " percent ";
static const char *register_get_postfix = "sampleout";
static const char *register_get_start = "# REGISTERS-START";
static const char *register_get_end = "# REGISTERS-DONE";
static const char *register_repeat_start = "# REGISTERS-REPEAT-START";
static const char *register_repeat_end = "# REGISTERS-REPEAT-DONE";
static const char *register_dev_echo = "# REGISTERS-FOR-DEVICE:";
static const char *register_ind_echo = "# REGISTER-INDIRECT:";
static const char *command_status = "ECHO Status:%STATUS%-%TSTATUS%";
static const char *command_done_echo = "# COMMAND-DONE";
static int little_endian;
static void *_panel_reader(void *arg);
static void *_panel_callback(void *arg);
static void *_panel_debugflusher(void *arg);
static int sim_panel_set_error (PANEL *p, const char *fmt, ...);
static pthread_key_t panel_thread_id;

#define TN_IAC          0xFFu /* -1 */                  /* protocol delim */
#define TN_DONT         0xFEu /* -2 */                  /* dont */
#define TN_DO           0xFDu /* -3 */                  /* do */
#define TN_WONT         0xFCu /* -4 */                  /* wont */
#define TN_WILL         0xFBu /* -5 */                  /* will */

#define TN_BIN            0                             /* bin */
#define TN_ECHO           1                             /* echo */
#define TN_SGA            3                             /* sga */
#define TN_CR           015                             /* carriage return */
#define TN_LF           012                             /* line feed */
#define TN_LINE          34                             /* line mode */

static unsigned char mantra[] = {
    TN_IAC, TN_WILL, TN_LINE,
    TN_IAC, TN_WILL, TN_SGA,
    TN_IAC, TN_WILL, TN_ECHO,
    TN_IAC, TN_WILL, TN_BIN,
    TN_IAC, TN_DO, TN_BIN
    };

static void *
_panel_malloc (size_t size)
{
void *p = malloc (size);

if (p == NULL)
    sim_panel_set_error (NULL, "Out of Memory");
return p;
}

/* Allow compiler to help validate printf style format arguments */
#if !defined __GNUC__
#define GCC_FMT_ATTR(n, m)
#endif
#if !defined(GCC_FMT_ATTR)
#define GCC_FMT_ATTR(n, m) __attribute__ ((format (__printf__, n, m)))
#endif

static void __panel_debug (PANEL *p, int dbits, const char *fmt, const char *buf, int bufsize, ...) GCC_FMT_ATTR(3, 6);
#define _panel_debug(p, dbits, fmt, buf, bufsize, ...) do { if (p && p->Debug && ((dbits) & p->debug)) __panel_debug (p, dbits, fmt, buf, bufsize, ##__VA_ARGS__);} while (0)

static void __panel_vdebug (PANEL *p, int dbits, const char *fmt, const char *buf, int bufsize, va_list arglist)
{
size_t obufsize = 10240 + 9*bufsize;

while (p && p->Debug && (dbits & p->debug)) {
    int i, len;
    struct timespec time_now;
    char timestamp[32];
    char threadname[50];
    char *obuf = (char *)_panel_malloc (obufsize);

    clock_gettime(CLOCK_REALTIME, &time_now);
    sprintf (timestamp, "%lld.%03d ", (long long)(time_now.tv_sec), (int)(time_now.tv_nsec/1000000));
    sprintf (threadname, "%s:%s ", p->parent ? p->device_name : "CPU", (pthread_getspecific (panel_thread_id)) ? (char *)pthread_getspecific (panel_thread_id) : ""); 
    
    obuf[obufsize - 1] = '\0';
    len = vsnprintf (obuf, obufsize - 1, fmt, arglist);
    if (len < 0)
        return;
    /* If the formatted result didn't fit into the buffer, then grow the buffer and try again */
    if (len >= (int)(obufsize - 9*bufsize)) {
        obufsize = len + 1 + 9*bufsize;
        free (obuf);
        continue;
        }

    for (i=0; i<bufsize; ++i) {
        switch ((unsigned char)buf[i]) {
            case TN_CR:
                sprintf (&obuf[strlen (obuf)], "_TN_CR_");
                break;
            case TN_LF:
                sprintf (&obuf[strlen (obuf)], "_TN_LF_");
                break;
            case TN_IAC:
                sprintf (&obuf[strlen (obuf)], "_TN_IAC_");
                switch ((unsigned char)buf[i+1]) {
                    case TN_IAC:
                        sprintf (&obuf[strlen (obuf)], "_TN_IAC_"); ++i;
                        break;
                    case TN_DONT:
                        sprintf (&obuf[strlen (obuf)], "_TN_DONT_"); ++i;
                        break;
                    case TN_DO:
                        sprintf (&obuf[strlen (obuf)], "_TN_DO_"); ++i;
                        break;
                    case TN_WONT:
                        sprintf (&obuf[strlen (obuf)], "_TN_WONT_"); ++i;
                        break;
                    case TN_WILL:
                        sprintf (&obuf[strlen (obuf)], "_TN_WILL_"); ++i;
                        break;
                    default:
                        sprintf (&obuf[strlen (obuf)], "_0x%02X_", (unsigned char)buf[i+1]); ++i;
                        break;
                    }
                switch ((unsigned char)buf[i+1]) {
                    case TN_BIN:
                        sprintf (&obuf[strlen (obuf)], "_TN_BIN_"); ++i;
                        break;
                    case TN_ECHO:
                        sprintf (&obuf[strlen (obuf)], "_TN_ECHO_"); ++i;
                        break;
                    case TN_SGA:
                        sprintf (&obuf[strlen (obuf)], "_TN_SGA_"); ++i;
                        break;
                    case TN_LINE:
                        sprintf (&obuf[strlen (obuf)], "_TN_LINE_"); ++i;
                        break;
                    default:
                        sprintf (&obuf[strlen (obuf)], "_0x%02X_", (unsigned char)buf[i+1]); ++i;
                        break;
                    }
                    break;
            default:
                if (isprint((u_char)buf[i]))
                    sprintf (&obuf[strlen (obuf)], "%c", buf[i]);
                else {
                    sprintf (&obuf[strlen (obuf)], "_");
                    if ((buf[i] >= 1) && (buf[i] <= 26))
                        sprintf (&obuf[strlen (obuf)], "^%c", 'A' + buf[i] - 1);
                    else
                        sprintf (&obuf[strlen (obuf)], "\\%03o", (u_char)buf[i]);
                    sprintf (&obuf[strlen (obuf)], "_");
                    }
                break;
            }
        }
    fprintf(p->Debug, "%s%s%s\n", timestamp, threadname, obuf);
    free (obuf);
    break;
    }
}

static void __panel_debug (PANEL *p, int dbits, const char *fmt, const char *buf, int bufsize, ...)
{
va_list arglist;

va_start (arglist, bufsize);
__panel_vdebug (p, dbits, fmt, buf, bufsize, arglist);
va_end (arglist);
}

void
sim_panel_debug (PANEL *panel, const char *fmt, ...)
{
va_list arglist;

va_start (arglist, fmt);
__panel_vdebug (panel, DBG_APP, fmt, NULL, 0, arglist);
va_end (arglist);
}


static void *
_panel_debugflusher(void *arg)
{
PANEL *p = (PANEL*)arg;
int flush_interval = 15;
int sleeps = 0;

pthread_setspecific (panel_thread_id, "debugflush");

pthread_mutex_lock (&p->io_lock);
p->debugflush_thread_running = 1;
pthread_mutex_unlock (&p->io_lock);
pthread_cond_signal (&p->startup_done);   /* Signal we're ready to go */
msleep (100);
pthread_mutex_lock (&p->io_lock);
while (p->sock != INVALID_SOCKET) {
    pthread_mutex_unlock (&p->io_lock);
    msleep (1000);
    pthread_mutex_lock (&p->io_lock);
    if (0 == (sleeps++)%flush_interval)
        sim_panel_flush_debug (p);
    }
pthread_mutex_unlock (&p->io_lock);
pthread_mutex_lock (&p->io_lock);
pthread_setspecific (panel_thread_id, NULL);
p->debugflush_thread_running  = 0;
pthread_mutex_unlock (&p->io_lock);
return NULL;
}


static void
_set_debug_file (PANEL *panel, const char *debug_file)
{
if (!panel)
    return;
panel->Debug = fopen(debug_file, "w");
if (panel->Debug)
    setvbuf (panel->Debug, NULL, _IOFBF, 65536);
}

void
sim_panel_set_debug_mode (PANEL *panel, int debug_bits)
{
if (panel)
    panel->debug = debug_bits;
}

void
sim_panel_flush_debug (PANEL *panel)
{
if (!panel)
    return;
if (panel->Debug)
    fflush (panel->Debug);
}


static int
_panel_send (PANEL *p, const char *msg, int len)
{
int sent = 0;

if (p->sock == INVALID_SOCKET)
    return sim_panel_set_error (p, "Invalid Socket for write");
pthread_mutex_lock (&p->io_send_lock);
while (len) {
    int bsent = sim_write_sock (p->sock, msg, len);
    if (bsent < 0) {
        pthread_mutex_unlock (&p->io_send_lock);
        return sim_panel_set_error (p, "%s", sim_get_err_sock("Error writing to socket"));
        }
    _panel_debug (p, DBG_XMT, "Sent %d bytes: ", msg, bsent, bsent);
    len -= bsent;
    msg += bsent;
    sent += bsent;
    }
pthread_mutex_unlock (&p->io_send_lock);
return sent;
}

static int
_panel_sendf (PANEL *p, int *completion_status, char **response, const char *fmt, ...);

static int
_panel_sendf_completion (PANEL *p, char **response, const char *completion, const char *fmt, ...);

static int
_panel_register_query_string (PANEL *panel, char **buf, size_t *buf_size)
{
size_t i, j, buf_data, buf_needed = 0, reg_count = 0, bit_reg_count = 0;
const char *dev;

pthread_mutex_lock (&panel->io_lock);
buf_needed = 3 + 7 +                        /* EXECUTE */
             strlen (register_get_start) +  /* # REGISTERS-START */
             strlen (register_get_prefix);  /* SHOW TIME */
for (i=0; i<panel->reg_count; i++) {
    if (panel->regs[i].bits)
        ++bit_reg_count;
    else {
        ++reg_count;
        buf_needed += 10 + strlen (panel->regs[i].name) + (panel->regs[i].device_name ? strlen (panel->regs[i].device_name) : 0);
        if (panel->regs[i].element_count > 0)
            buf_needed += 4 + 6 /* 6 digit register array index */;
        if (panel->regs[i].indirect)
            buf_needed += 12 + strlen (register_ind_echo) + strlen (panel->regs[i].name);
        }
    }
if (bit_reg_count)
    buf_needed += 2 + strlen (register_get_postfix);
buf_needed += 10 + strlen (register_get_end);    /* # REGISTERS-DONE */
if (buf_needed > *buf_size) {
    free (*buf);
    *buf = (char *)_panel_malloc (buf_needed);
    if (!*buf) {
        panel->State = Error;
        pthread_mutex_unlock (&panel->io_lock);
        return -1;
        }
    *buf_size = buf_needed;
    }
buf_data = 0;
if (reg_count) {
    sprintf (*buf + buf_data, "EXECUTE %s;%s;", register_get_start, register_get_prefix);
    buf_data += strlen (*buf + buf_data);
    }
dev = "";
for (i=j=0; i<panel->reg_count; i++) {
    const char *reg_dev = panel->regs[i].device_name ? panel->regs[i].device_name : "";

    if ((panel->regs[i].indirect) || (panel->regs[i].bits))
        continue;
    if (strcmp (dev, reg_dev)) {/* devices are different */
        char *tbuf;

        buf_needed += 4 + strlen (register_dev_echo) + strlen (reg_dev);   /* # REGISTERS-for-DEVICE:XXX */
        tbuf = (char *)_panel_malloc (buf_needed);
        if (tbuf == NULL) {
            panel->State = Error;
            pthread_mutex_unlock (&panel->io_lock);
            return -1;
            }
        strcpy (tbuf, *buf);
        free (*buf);
        *buf = tbuf;
        sprintf (*buf + buf_data, "%s%s%s;", (i == 0)? "" : ";", register_dev_echo, reg_dev);
        buf_data += strlen (*buf + buf_data);
        dev = reg_dev;
        j = 0;
        *buf_size = buf_needed;
        }
    if (panel->regs[i].element_count == 0) {
        if (j == 0)
            sprintf (*buf + buf_data, "E -16 %s %s", dev, panel->regs[i].name);
        else
            sprintf (*buf + buf_data, ",%s", panel->regs[i].name);
        }
    else {
        if (j == 0)
            sprintf (*buf + buf_data, "E -16 %s %s[0:%d]", dev, panel->regs[i].name, (int)(panel->regs[i].element_count-1));
        else
            sprintf (*buf + buf_data, ",%s[0:%d]", panel->regs[i].name, (int)(panel->regs[i].element_count-1));
        }
    ++j;
    buf_data += strlen (*buf + buf_data);
    }
if (buf_data && ((*buf)[buf_data-1] != ';')) {
    strcpy (*buf + buf_data, ";");
    buf_data += strlen (*buf + buf_data);
    }
for (i=j=0; i<panel->reg_count; i++) {
    const char *reg_dev = panel->regs[i].device_name ? panel->regs[i].device_name : "";

    if ((!panel->regs[i].indirect) || (panel->regs[i].bits))
        continue;
    sprintf (*buf + buf_data, "%s%s;E -16 %s %s,$;", register_ind_echo, panel->regs[i].name, reg_dev, panel->regs[i].name);
    buf_data += strlen (*buf + buf_data);
    }
if (bit_reg_count) {
    strcpy (*buf + buf_data, register_get_postfix);
    buf_data += strlen (*buf + buf_data);
    strcpy (*buf + buf_data, ";");
    buf_data += strlen (*buf + buf_data);
    }
strcpy (*buf + buf_data, register_get_end);
buf_data += strlen (*buf + buf_data);
strcpy (*buf + buf_data, "\r");
buf_data += strlen (*buf + buf_data);
*buf_size = buf_data;
pthread_mutex_unlock (&panel->io_lock);
return 0;
}

static int
_panel_establish_register_bits_collection (PANEL *panel)
{
size_t i, buf_data, buf_needed = 0, reg_count = 0, bit_reg_count = 0;
int cmd_stat, bits_count = 0;
char *buf, *response = NULL;

pthread_mutex_lock (&panel->io_lock);
for (i=0; i<panel->reg_count; i++) {
    if (panel->regs[i].bits)
        buf_needed += 9 + strlen (panel->regs[i].name) + (panel->regs[i].device_name ? strlen (panel->regs[i].device_name) : 0);
    }
buf = (char *)_panel_malloc (buf_needed);
if (!buf) {
    panel->State = Error;
    pthread_mutex_unlock (&panel->io_lock);
    return -1;
    }
*buf = '\0';
buf_data = 0;
for (i=0; i<panel->reg_count; i++) {
    if (panel->regs[i].bits) {
        ++bits_count;
        sprintf (buf + buf_data, "%s%s", (bits_count != 1) ? "," : "", panel->regs[i].indirect ? "-I " : "");
        buf_data += strlen (buf + buf_data);
        if (panel->regs[i].device_name) {
            sprintf (buf + buf_data, "%s ", panel->regs[i].device_name);
            buf_data += strlen (buf + buf_data);
            }
        sprintf (buf + buf_data, "%s", panel->regs[i].name);
        buf_data += strlen (buf + buf_data);
        }
    }
pthread_mutex_unlock (&panel->io_lock);
if (_panel_sendf (panel, &cmd_stat, &response, "%s%u%s%u%s%u%s%s\r", register_collect_prefix, panel->sample_depth, 
                                                                     register_collect_mid1, panel->sample_frequency,
                                                                     register_collect_mid2, panel->sample_dither_pct,
                                                                     register_collect_mid3, buf)) {
    sim_panel_set_error (NULL, "Error establishing bit data collection:%s", response);
    free (response);
    free (buf);
    return -1;
    }
free (response);
free (buf);
return 0;
}

static PANEL **panels = NULL;
static int panel_count = 0;
static char *sim_panel_error_buf = NULL;
static size_t sim_panel_error_bufsize = 0;


static void
_panel_cleanup (void)
{
while (panel_count)
    sim_panel_destroy (*panels);
}

static void
_panel_register_panel (PANEL *p)
{
if (panel_count == 0)
    pthread_key_create (&panel_thread_id, free);
if (!pthread_getspecific (panel_thread_id))
    pthread_setspecific (panel_thread_id, p->device_name ? p->device_name : "PanelCreator");
++panel_count;
panels = (PANEL **)realloc (panels, sizeof(*panels)*panel_count);
panels[panel_count-1] = p;
if (panel_count == 1)
    atexit (&_panel_cleanup);
}

static void
_panel_deregister_panel (PANEL *p)
{
int i;

for (i=0; i<panel_count; i++) {
    if (panels[i] == p) {
        int j;
        for (j=i+1; j<panel_count; j++) 
            panels[j-1] = panels[j];
        --panel_count;
        if (panel_count == 0) {
            free (panels);
            panels = NULL;
            pthread_setspecific (panel_thread_id, NULL);
            pthread_key_delete (panel_thread_id);
            }
        break;
        }
    }
}


static PANEL *
_sim_panel_create (const char *sim_path,
                   const char *sim_config,
                   size_t device_panel_count,
                   PANEL *simulator_panel,
                   const char *device_name,
                   const char *debug_file)
{
PANEL *p = NULL;
FILE *fIn = NULL;
FILE *fOut = NULL;
struct stat statb;
char *buf = NULL;
int port;
int cmd_stat;
size_t i, device_num;
char hostport[64];
union {int i; char c[sizeof (int)]; } end_test;

if (sim_panel_error_buf == NULL) {  /* Preallocate an error message buffer */
    sim_panel_error_bufsize = 2048;
    sim_panel_error_buf = (char *) malloc (sim_panel_error_bufsize);
    if (sim_panel_error_buf == NULL) {
        sim_panel_error_buf = (char *)"sim_panel_set_error(): Out of Memory\n";
        sim_panel_error_bufsize = 0;
        return NULL;
        }
    }
if (simulator_panel) {
    for (device_num=0; device_num < simulator_panel->device_count; ++device_num)
        if (simulator_panel->devices[device_num] == NULL)
            break;
    if (device_num == simulator_panel->device_count) {
        sim_panel_set_error (NULL, "No free panel devices slots available %s simulator.  All %d slots are used.", simulator_panel->path, (int)simulator_panel->device_count);
        return NULL;
        }
    p = (PANEL *)_panel_malloc (sizeof(*p));
    if (p == NULL)
        goto Error_Return;
    memset (p, 0, sizeof(*p));
    _panel_register_panel (p);
    p->device_name = (char *)_panel_malloc (1 + strlen (device_name));
    if (p->device_name == NULL)
        goto Error_Return;
    strcpy (p->device_name, device_name);
    p->parent = simulator_panel;
    p->Debug = p->parent->Debug;
    strcpy (p->hostport, simulator_panel->hostport);
    p->sock = INVALID_SOCKET;
    }
else {
    end_test.i = 1;                             /* test endian-ness */
    little_endian = (end_test.c[0] != 0);
    sim_init_sock ();
    for (port=1024; port < 2048; port++) {
        SOCKET sock;

        sprintf (hostport, "%d", port);
        sock = sim_connect_sock_ex (NULL, hostport, NULL, NULL, SIM_SOCK_OPT_NODELAY | SIM_SOCK_OPT_BLOCKING);
        if (sock != INVALID_SOCKET) {
            int sta = 0;
            while (!sta) {
                msleep (10);
                sta = sim_check_conn (sock, 1);
                }
            sim_close_sock (sock);
            if (sta == -1)
                break;
            }
        else
            break;
        }
    if (stat (sim_config, &statb) < 0) {
        sim_panel_set_error (NULL, "Can't stat simulator configuration '%s': %s", sim_config, strerror(errno));
        goto Error_Return;
        }
    buf = (char *)_panel_malloc (statb.st_size+1);
    if (buf == NULL)
        goto Error_Return;
    buf[statb.st_size] = '\0';
    p = (PANEL *)_panel_malloc (sizeof(*p));
    if (p == NULL)
        goto Error_Return;
    memset (p, 0, sizeof(*p));
    _panel_register_panel (p);
    p->sock = INVALID_SOCKET;
    p->path = (char *)_panel_malloc (strlen (sim_path) + 1);
    if (p->path == NULL)
        goto Error_Return;
    strcpy (p->path, sim_path);
    p->config = (char *)_panel_malloc (strlen (sim_config) + 1);
    if (p->config == NULL)
        goto Error_Return;
    strcpy (p->config, sim_config);
    fIn = fopen (sim_config, "r");
    if (fIn == NULL) {
        sim_panel_set_error (NULL, "Can't open configuration file '%s': %s", sim_config, strerror(errno));
        goto Error_Return;
        }
    p->temp_config = (char *)_panel_malloc (strlen (sim_config) + 40);
    if (p->temp_config == NULL)
        goto Error_Return;
    sprintf (p->temp_config, "%s-Panel-%d", sim_config, getpid());
    fOut = fopen (p->temp_config, "w");
    if (fOut == NULL) {
        sim_panel_set_error (NULL, "Can't create temporary configuration file '%s': %s", p->temp_config, strerror(errno));
        goto Error_Return;
        }
    fprintf (fOut, "# Temporary FrontPanel generated simh configuration file\n");
    fprintf (fOut, "# Original Configuration File: %s\n", p->config);
    fprintf (fOut, "# Simulator Path: %s\n", sim_path);
    while (fgets (buf, statb.st_size, fIn))
        fputs (buf, fOut);
    free (buf);
    buf = NULL;
    fclose (fIn);
    fIn = NULL;
    fprintf (fOut, "set remote notelnet\n");
    if (device_panel_count)
        fprintf (fOut, "set remote connections=%d\n", (int)device_panel_count+1);
    fprintf (fOut, "set remote -u telnet=%s\n", hostport);
    fprintf (fOut, "set remote master\n");
    fprintf (fOut, "exit\n");
    fclose (fOut);
    fOut = NULL;
    }
if (debug_file) {
    _set_debug_file (p, debug_file);
    sim_panel_set_debug_mode (p, DBG_XMT|DBG_RCV);
    _panel_debug (p, DBG_XMT|DBG_RCV, "Creating Simulator Process %s\n", NULL, 0, sim_path);

    if (stat (p->temp_config, &statb) < 0) {
        sim_panel_set_error (NULL, "Can't stat temporary simulator configuration '%s': %s", p->temp_config, strerror(errno));
        goto Error_Return;
        }
    buf = (char *)_panel_malloc (statb.st_size+1);
    if (buf == NULL)
        goto Error_Return;
    buf[statb.st_size] = '\0';
    fIn = fopen (p->temp_config, "r");
    if (fIn == NULL) {
        sim_panel_set_error (NULL, "Can't open temporary configuration file '%s': %s", p->temp_config, strerror(errno));
        goto Error_Return;
        }
    _panel_debug (p, DBG_XMT|DBG_RCV, "Using Temporary Configuration File '%s' containing:", NULL, 0, p->temp_config);
    i = 0;
    while (fgets (buf, statb.st_size, fIn)) {
        ++i;
        buf[strlen(buf) - 1] = '\0';
        _panel_debug (p, DBG_XMT|DBG_RCV, "Line %2d: %s", NULL, 0, (int)i, buf);
        }
    free (buf);
    buf = NULL;
    fclose (fIn);
    fIn = NULL;
    }
if (!simulator_panel) {
#if defined(_WIN32)
    char cmd[2048];
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFO StartupInfo;

    sprintf(cmd, "%s%s%s %s%s%s", strchr (sim_path, ' ') ? "\"" : "", sim_path, strchr (sim_path, ' ') ? "\"" : "", strchr (p->temp_config, ' ') ? "\"" : "", p->temp_config, strchr (p->temp_config, ' ') ? "\"" : "");

    memset (&ProcessInfo, 0, sizeof(ProcessInfo));
    memset (&StartupInfo, 0, sizeof(StartupInfo));
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &StartupInfo, &ProcessInfo)) {
        CloseHandle (ProcessInfo.hThread);
        p->hProcess = ProcessInfo.hProcess;
        p->dwProcessId = ProcessInfo.dwProcessId;
        }
    else { /* Creation Problem */
        sim_panel_set_error (NULL, "CreateProcess Error: %d", GetLastError());
        goto Error_Return;
        }
#else
    p->pidProcess = fork();
    if (p->pidProcess == 0) {
        close (0); close (1); close (2);        /* make sure not to pass the open standard handles */
        dup (dup (open ("/dev/null", O_RDWR))); /* open standard handles to /dev/null */
        if (execlp (sim_path, sim_path, p->temp_config, NULL, NULL)) {
            perror ("execl");
            exit(errno);
            }
        }
    if (p->pidProcess < 0) {
        p->pidProcess = 0;
        sim_panel_set_error (NULL, "fork() Error: %s", strerror(errno));
        goto Error_Return;
        }
#endif
    strcpy (p->hostport, hostport);
    }
for (i=0; i<100; i++) {          /* Allow up to 10 seconds waiting for simulator to start up */
    p->sock = sim_connect_sock_ex (NULL, p->hostport, NULL, NULL, SIM_SOCK_OPT_NODELAY | SIM_SOCK_OPT_BLOCKING);
    if (p->sock == INVALID_SOCKET)
        msleep (100);
    else
        break;
    }
if (p->sock == INVALID_SOCKET) {
    if (simulator_panel) {
        sim_panel_set_error (NULL, "Can't connect to simulator Remote Console on port %s", p->hostport);
        }
    else {
        if (stat (sim_path, &statb) < 0)
            sim_panel_set_error (NULL, "Can't stat simulator '%s': %s", sim_path, strerror(errno));
        else
            sim_panel_set_error (NULL, "Can't connect to the %s simulator Remote Console on port %s, the simulator process may not have started or the simulator binary can't be found", sim_path, p->hostport);
        }
    goto Error_Return;
    }
_panel_debug (p, DBG_XMT|DBG_RCV, "Connected to simulator on %s after %dms", NULL, 0, p->hostport, (int)i*100);
pthread_mutex_init (&p->io_lock, NULL);
pthread_mutex_init (&p->io_send_lock, NULL);
pthread_mutex_init (&p->io_command_lock, NULL);
pthread_cond_init (&p->io_done, NULL);
pthread_cond_init (&p->startup_done, NULL);
if (sizeof(mantra) != _panel_send (p, (char *)mantra, sizeof(mantra))) {
    sim_panel_set_error (NULL, "Error sending Telnet mantra (options): %s", sim_get_err_sock ("send"));
    goto Error_Return;
    }
if (1) {
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_mutex_lock (&p->io_lock);
    p->io_thread_running = 0;
    pthread_create (&p->io_thread, &attr, _panel_reader, (void *)p);
    while (!p->io_thread_running)
        pthread_cond_wait (&p->startup_done, &p->io_lock); /* Wait for thread to stabilize */
    if ((p->Debug) && (p->parent == NULL)) {
        p->debugflush_thread_running = 0;
        pthread_create (&p->debugflush_thread, &attr, _panel_debugflusher, (void *)p);
        while (!p->debugflush_thread_running)
            pthread_cond_wait (&p->startup_done, &p->io_lock); /* Wait for thread to stabilize */
        }
    pthread_mutex_unlock (&p->io_lock);
    pthread_attr_destroy(&attr);
    pthread_cond_destroy (&p->startup_done);
    }
if (simulator_panel) {
    simulator_panel->devices[device_num] = p;
    }
else {
    if (device_panel_count) {
        p->devices = (PANEL **)_panel_malloc (device_panel_count*sizeof(*p->devices));
        if (p->devices == NULL)
            goto Error_Return;
        memset (p->devices, 0, device_panel_count*sizeof(*p->devices));
        p->device_count = device_panel_count;
        }
    if (p->State == Error)
        goto Error_Return;
    /* Validate sim_frontpanel API version */
    if (_panel_sendf (p, &cmd_stat, &p->simulator_version, "SHOW VERSION\r"))
        goto Error_Return;
    if (1) {
        int api_version = 0;
        char *c = strstr (p->simulator_version, "FrontPanel API Version");

        if ((!c) ||
            (1 != sscanf (c, "FrontPanel API Version %d", &api_version)) ||
            (api_version != SIM_FRONTPANEL_VERSION)) {
            sim_panel_set_error (NULL, "Inconsistent sim_frontpanel API version %d in simulator.  Version %d needed.-", api_version, SIM_FRONTPANEL_VERSION);
            goto Error_Return;
            }
        }
    if (1) {
        char *radix = NULL;

        if (_panel_sendf (p, &cmd_stat, &radix, "SHOW %s RADIX\r", p->device_name ? p->device_name : "")) {
            free (radix);
            goto Error_Return;
            }
        sscanf (radix, "Radix=%d", &p->radix);
        free (radix);
        if ((p->radix != 16) && (p->radix != 8)) {
            sim_panel_set_error (NULL, "Unsupported Radix: %d%s%s.", p->radix, p->device_name ? " on device " : "", p->device_name ? p->device_name : "");
            goto Error_Return;
            }
        }
    }
return p;

Error_Return:
if (fIn)
    fclose (fIn);
if (fOut) {
    fclose (fOut);
    (void)remove (p->temp_config);
    }
if (buf)
    free (buf);
if (1) {
    const char *err = sim_panel_get_error();
    char *errbuf = (char *)_panel_malloc (1 + strlen (err));

    strcpy (errbuf, err);               /* preserve error info while closing */
    sim_panel_destroy (p);
    sim_panel_set_error (NULL, "%s", errbuf);
    free (errbuf);
    }
if (!simulator_panel)
    sim_cleanup_sock();
return NULL;
}

PANEL *
sim_panel_start_simulator_debug (const char *sim_path,
                                 const char *sim_config,
                                 size_t device_panel_count,
                                 const char *debug_file)
{
return _sim_panel_create (sim_path, sim_config, device_panel_count, NULL, NULL, debug_file);
}

PANEL *
sim_panel_start_simulator (const char *sim_path,
                           const char *sim_config,
                           size_t device_panel_count)
{
return sim_panel_start_simulator_debug (sim_path, sim_config, device_panel_count, NULL);
}

PANEL *
sim_panel_add_device_panel_debug (PANEL *simulator_panel,
                                  const char *device_name,
                                  const char *debug_file)
{
return _sim_panel_create (NULL, NULL, 0, simulator_panel, device_name, debug_file);
}

PANEL *
sim_panel_add_device_panel (PANEL *simulator_panel,
                            const char *device_name)
{
return sim_panel_add_device_panel_debug (simulator_panel, device_name, NULL);
}

int
sim_panel_destroy (PANEL *panel)
{
REG *reg;

if (panel) {
    _panel_debug (panel, DBG_XMT|DBG_RCV, "Closing Panel %s", NULL, 0, panel->device_name? panel->device_name : panel->path);
    if (panel->devices) {
        size_t i;

        for (i=0; i<panel->device_count; i++) {
            if (panel->devices[i]) {
                sim_panel_destroy (panel->devices[i]);
                panel->devices[i] = NULL;
                }
            }
        free (panel->devices);
        panel->devices = NULL;
        }

    if (panel->sock != INVALID_SOCKET) {
        SOCKET sock = panel->sock;
        int wait_count;

        /* First, wind down the automatic register queries */
        sim_panel_set_display_callback_interval (panel, NULL, NULL, 0);
        /* Next, attempt a simulator shutdown only with the master panel */
        if (panel->parent == NULL) {
            if (panel->State == Run)
                sim_panel_exec_halt (panel);
            _panel_send (panel, "EXIT\r", 5);
            }
        /* Wait for up to 2 seconds for a graceful shutdown */
        panel->sock = INVALID_SOCKET;
        for (wait_count=0; panel->io_thread_running && (wait_count<20); ++wait_count)
            msleep (100);
        /* Now close the socket which should stop a pending read that hasn't completed */
        sim_close_sock (sock);
        pthread_join (panel->io_thread, NULL);
        }
    if ((panel->Debug) && (panel->parent == NULL))
        pthread_join (panel->debugflush_thread, NULL);
    pthread_mutex_destroy (&panel->io_lock);
    pthread_mutex_destroy (&panel->io_send_lock);
    pthread_mutex_destroy (&panel->io_command_lock);
    pthread_cond_destroy (&panel->io_done);
#if defined(_WIN32)
    if (panel->hProcess) {
        GenerateConsoleCtrlEvent (CTRL_BREAK_EVENT, panel->dwProcessId);
        msleep (200);
        TerminateProcess (panel->hProcess, 0);
        WaitForSingleObject (panel->hProcess, INFINITE);
        CloseHandle (panel->hProcess);
        }
#else
    if (panel->pidProcess) {
        int status;

        if (!kill (panel->pidProcess, 0)) {
            kill (panel->pidProcess, SIGTERM);
            msleep (200);
            if (!kill (panel->pidProcess, 0))
                kill (panel->pidProcess, SIGKILL);
            }
        waitpid (panel->pidProcess, &status, 0);
        }
#endif
    free (panel->path);
    free (panel->device_name);
    free (panel->config);
    if (panel->temp_config)
        (void)remove (panel->temp_config);
    free (panel->temp_config);
    reg = panel->regs;
    while (panel->reg_count--) {
        free (reg->name);
        free (reg->device_name);
        reg++;
        }
    free (panel->regs);
    free (panel->reg_query);
    free (panel->io_response);
    free (panel->halt_reason);
    free (panel->simulator_version);
    if ((panel->Debug) && (!panel->parent))
        fclose (panel->Debug);
    if (!panel->parent)
        sim_cleanup_sock ();
    _panel_deregister_panel (panel);
    free (panel);
    }
return 0;
}

OperationalState
sim_panel_get_state (PANEL *panel)
{
if (!panel)
    return Halt;
return panel->State;
}

static int
_panel_add_register (PANEL *panel,
                     const char *name,
                     const char *device_name,
                     size_t size,
                     void *addr,
                     int indirect,
                     size_t element_count,
                     int *bits,
                     size_t bit_count)
{
REG *regs, *reg;
char *response = NULL, *c;
unsigned long long data;
size_t i;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if ((bit_count != 0) && (panel->sample_depth == 0)) {
    sim_panel_set_error (NULL, "sim_panel_set_sampling_parameters() must be called first");
    return -1;
    }
regs = (REG *)_panel_malloc ((1 + panel->reg_count)*sizeof(*regs)); 
if (regs == NULL)
    return sim_panel_set_error (panel, "_panel_add_register(): Out of Memory\n");
pthread_mutex_lock (&panel->io_lock);
memcpy (regs, panel->regs, panel->reg_count*sizeof(*regs));
reg = &regs[panel->reg_count];
memset (reg, 0, sizeof(*regs));
reg->name = (char *)_panel_malloc (1 + strlen (name));
if (reg->name == NULL) {
    panel->State = Error;
    pthread_mutex_unlock (&panel->io_lock);
    sim_panel_set_error (NULL, "_panel_add_register(): Out of Memory\n");
    free (regs);
    return -1;
    }
strcpy (reg->name, name);
reg->indirect = indirect;
reg->addr = addr;
reg->size = size;
reg->element_count = element_count;
reg->bits = bits;
reg->bit_count = bit_count;
for (i=0; i<strlen (reg->name); i++) {
    if (islower (reg->name[i]))
        reg->name[i] = toupper (reg->name[i]);
    }
if (device_name) {
    reg->device_name = (char *)_panel_malloc (1 + strlen (device_name));
    if (reg->device_name == NULL) {
        free (reg->name);
        free (regs);
        pthread_mutex_unlock (&panel->io_lock);
        return sim_panel_set_error (panel, "_panel_add_register(): Out of Memory\n");
        }
    strcpy (reg->device_name, device_name);
    for (i=0; i<strlen (reg->device_name); i++) {
        if (islower (reg->device_name[i]))
            reg->device_name[i] = toupper (reg->device_name[i]);
        }
    }
for (i=0; i<panel->reg_count; i++) {
    char *t1 = (char *)_panel_malloc (2 + strlen (regs[i].name) + (regs[i].device_name? strlen (regs[i].device_name) : 0));
    char *t2 = (char *)_panel_malloc (2 + strlen (reg->name) + (reg->device_name? strlen (reg->device_name) : 0));

    if ((t1 == NULL) || (t2 == NULL)) {
        free (t1);
        free (t2);
        free (reg->name);
        free (reg->device_name);
        panel->State = Error;
        free (regs);
        pthread_mutex_unlock (&panel->io_lock);
        return sim_panel_set_error (NULL, "_panel_add_register(): Out of Memory\n");
        }
    sprintf (t1, "%s %s", regs[i].device_name ? regs[i].device_name : "", regs[i].name);
    sprintf (t2, "%s %s", reg->device_name ? reg->device_name : "", reg->name);
    if ((!strcmp (t1, t2)) && 
        (reg->indirect == regs[i].indirect) && 
        ((reg->bits == NULL) == (regs[i].bits == NULL))) {
        pthread_mutex_unlock (&panel->io_lock);
        sim_panel_set_error (NULL, "Duplicate Register Declaration");
        free (t1);
        free (t2);
        free (reg->name);
        free (reg->device_name);
        free (regs);
        return -1;
        }
    free (t1);
    free (t2);
    }
pthread_mutex_unlock (&panel->io_lock);
/* Validate existence of requested register/array */
if (_panel_sendf (panel, &cmd_stat, &response, "EXAMINE -H %s %s%s\r", device_name? device_name : "", name, (element_count > 0) ? "[0]" : "")) {
    free (reg->name);
    free (reg->device_name);
    free (regs);
    return -1;
    }
c = strchr (response, ':');
if ((!strcmp ("Invalid argument\r\n", response)) || (!c)) {
    sim_panel_set_error (NULL, "Invalid Register: %s %s", device_name? device_name : "", name);
    free (response);
    free (reg->name);
    free (reg->device_name);
    free (regs);
    return -1;
    }
data = strtoull (c + 1, NULL, 16);
free (response);
if (element_count > 0) {
    if (_panel_sendf (panel, &cmd_stat, &response, "EXAMINE %s %s[%d]\r", device_name? device_name : "", name, element_count-1)) {
        free (reg->name);
        free (reg->device_name);
        free (regs);
        return -1;
        }
    if (!strcmp ("Subscript out of range\r\n", response)) {
        sim_panel_set_error (NULL, "Invalid Register Array Dimension: %s %s[%d]", device_name? device_name : "", name, element_count-1);
        free (response);
        free (reg->name);
        free (reg->device_name);
        free (regs);
        return -1;
        }
    free (response);
    }
pthread_mutex_lock (&panel->io_lock);
++panel->reg_count;
free (panel->regs);
panel->regs = regs;
panel->new_register = 1;
pthread_mutex_unlock (&panel->io_lock);
/* Now build the register query string for the whole register list */
if (_panel_register_query_string (panel, &panel->reg_query, &panel->reg_query_size))
    return -1;
if (bits) {
    for (i=0; i<bit_count; i++)
        bits[i] = (data & (1LL<<i)) ? panel->sample_depth : 0;
    if (_panel_establish_register_bits_collection (panel))
        return -1;
    }
return 0;
}

int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        const char *device_name,
                        size_t size,
                        void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 0, 0, NULL, 0);
}

int
sim_panel_add_register_bits (PANEL *panel,
                             const char *name,
                             const char *device_name,
                             size_t bit_width,
                             int *bits)
{
return _panel_add_register (panel, name, device_name, 0, NULL, 0, 0, bits, bit_width);
}

int
sim_panel_add_register_array (PANEL *panel,
                              const char *name,
                              const char *device_name,
                              size_t element_count,
                              size_t size,
                              void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 0, element_count, NULL, 0);
}


int
sim_panel_add_register_indirect (PANEL *panel,
                                 const char *name,
                                 const char *device_name,
                                 size_t size,
                                 void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 1, 0, NULL, 0);
}

int
sim_panel_add_register_indirect_bits (PANEL *panel,
                                      const char *name,
                                      const char *device_name,
                                      size_t bit_width,
                                      int *bits)
{
return _panel_add_register (panel, name, device_name, 0, NULL, 1, 0, bits, bit_width);
}

static int
_panel_get_registers (PANEL *panel, int calledback, unsigned long long *simulation_time)
{
if ((!panel) || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if ((!calledback) && (panel->callback)) {
    sim_panel_set_error (NULL, "Callback provides register data");
    return -1;
    }
if (!panel->reg_count) {
    sim_panel_set_error (NULL, "No registers specified");
    return -1;
    }
pthread_mutex_lock (&panel->io_command_lock);
pthread_mutex_lock (&panel->io_lock);
if (panel->reg_query_size != _panel_send (panel, panel->reg_query, panel->reg_query_size)) {
    pthread_mutex_unlock (&panel->io_lock);
    pthread_mutex_unlock (&panel->io_command_lock);
    return -1;
    }
if (panel->io_response_data)
    _panel_debug (panel, DBG_RCV, "Receive Data Discarded: ", panel->io_response, panel->io_response_data);
panel->io_response_data = 0;
panel->io_waiting = 1;
while (panel->io_waiting)
    pthread_cond_wait (&panel->io_done, &panel->io_lock);
if (simulation_time)
    *simulation_time = panel->simulation_time;
pthread_mutex_unlock (&panel->io_lock);
pthread_mutex_unlock (&panel->io_command_lock);
return 0;
}

int
sim_panel_get_registers (PANEL *panel, unsigned long long *simulation_time)
{
return _panel_get_registers (panel, (panel->State == Halt), simulation_time);
}

int
sim_panel_set_display_callback_interval (PANEL *panel, 
                                         PANEL_DISPLAY_PCALLBACK callback, 
                                         void *context, 
                                         int usecs_between_callbacks)
{
if (!panel) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
pthread_mutex_lock (&panel->io_lock);                               /* acquire access */
panel->callback = callback;
panel->callback_context = context;
if (usecs_between_callbacks && (0 == panel->usecs_between_callbacks)) { /* Need to start/enable callbacks */
    pthread_attr_t attr;

    _panel_debug (panel, DBG_THR, "Starting callback thread, Interval: %d usecs", NULL, 0, usecs_between_callbacks);
    panel->usecs_between_callbacks = usecs_between_callbacks;
    pthread_cond_init (&panel->startup_done, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create (&panel->callback_thread, &attr, _panel_callback, (void *)panel);
    pthread_attr_destroy(&attr);
    while (!panel->callback_thread_running)
        pthread_cond_wait (&panel->startup_done, &panel->io_lock);  /* Wait for thread to stabilize */
    pthread_cond_destroy (&panel->startup_done);
    }
if ((usecs_between_callbacks == 0) && panel->usecs_between_callbacks) { /* Need to stop callbacks */
    _panel_debug (panel, DBG_THR, "Shutting down callback thread", NULL, 0);
    panel->usecs_between_callbacks = 0;                             /* flag disabled */
    pthread_mutex_unlock (&panel->io_lock);                         /* allow access */
    pthread_join (panel->callback_thread, NULL);                    /* synchronize with thread rundown */
    pthread_mutex_lock (&panel->io_lock);                           /* reacquire access */
    }
pthread_mutex_unlock (&panel->io_lock);
return 0;
}

int
sim_panel_set_sampling_parameters_ex (PANEL *panel,
                                      unsigned int sample_frequency,
                                      unsigned int sample_dither_pct,
                                      unsigned int sample_depth)
{
if (sample_frequency == 0) {
    sim_panel_set_error (NULL, "Invalid sample frequency value: %u", sample_frequency);
    return -1;
    }
if (sample_dither_pct > 25) {
    sim_panel_set_error (NULL, "Invalid sample dither percentage value: %u", sample_dither_pct);
    return -1;
    }
if (sample_depth == 0) {
    sim_panel_set_error (NULL, "Invalid sample depth value: %u", sample_depth);
    return -1;
    }
panel->sample_frequency = sample_frequency;
panel->sample_dither_pct = sample_dither_pct;
panel->sample_depth = sample_depth;
return 0;
}

int
sim_panel_set_sampling_parameters (PANEL *panel,
                                   unsigned int sample_frequency,
                                   unsigned int sample_depth)
{
return sim_panel_set_sampling_parameters_ex (panel,
                                             sample_frequency,
                                             5,
                                             sample_depth);
}

int
sim_panel_exec_halt (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't HALT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    if (_panel_sendf_completion (panel, NULL, sim_prompt, "\005")) {
        _panel_debug (panel, DBG_THR, "Error trying to HALT running simulator: %s", NULL, 0, sim_panel_get_error ());
        return -1;
        }
    if (panel->State == Run) {
        _panel_debug (panel, DBG_THR, "Unable to HALT running simulator", NULL, 0);
        return -1;
        }
    }
return 0;
}

const char *
sim_panel_halt_text (PANEL *panel)
{
if (!panel || !panel->halt_reason)
    return "";
return panel->halt_reason;
}


int
sim_panel_exec_boot (PANEL *panel, const char *device)
{
int cmd_stat;
char *response, *simtime;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't BOOT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
/* A BOOT or RUN command will restart the simulator's time base. */
/* We account for that so that the frontpanel application sees ever */
/* increasing time values when register data is delivered. */
if (_panel_sendf (panel, &cmd_stat, &response, "SHOW TIME\r"))
    return -1;
if ((simtime = strstr (response, "Time:"))) {
    panel->simulation_time = strtoull (simtime + 5, NULL, 10);
    panel->simulation_time_base += panel->simulation_time;
    }
free (response);
if (_panel_sendf_completion (panel, NULL, "Simulator Running...", "BOOT %s\r", device)) {
    _panel_debug (panel, DBG_THR, "Unable to BOOT simulator: %s", NULL, 0, sim_panel_get_error());
    return -1;
    }
return 0;
}

int
sim_panel_exec_start (PANEL *panel)
{
int cmd_stat;
char *response, *simtime;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't RUN simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
/* A BOOT or RUN command will restart the simulator's time base. */
/* We account for that so that the frontpanel application sees ever */
/* increasing time values when register data is delivered. */
if (_panel_sendf (panel, &cmd_stat, &response, "SHOW TIME\r")) {
    _panel_debug (panel, DBG_THR, "Unable to send SHOW TIME command while starting simulator: %s", NULL, 0, sim_panel_get_error());
    return -1;
    }
if ((simtime = strstr (response, "Time:"))) {
    panel->simulation_time = strtoull (simtime + 5, NULL, 10);
    panel->simulation_time_base += panel->simulation_time;
    }
free (response);
panel->simulation_time_base += panel->simulation_time;
if (_panel_sendf_completion (panel, NULL, "Simulator Running...", "RUN\r", 5)) {
    _panel_debug (panel, DBG_THR, "Unable to start simulator: %s", NULL, 0, sim_panel_get_error());
    return -1;
    }
return 0;
}

int
sim_panel_exec_run (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't CONT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (_panel_sendf_completion (panel, NULL, "Simulator Running...", "CONT\r"))
    return -1;
return 0;
}

int
sim_panel_exec_step (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't STEP simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (_panel_sendf_completion (panel, NULL, sim_prompt, "STEP")) {
    _panel_debug (panel, DBG_THR, "Error trying to STEP running simulator: %s", NULL, 0, sim_panel_get_error ());
    return -1;
    }
return 0;
}

int
sim_panel_break_set (PANEL *panel, const char *condition)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't establish a breakpoint from device front panel");
    return -1;
    }
if ((_panel_sendf (panel, &cmd_stat, &response, "BREAK %s\r", condition)) ||
    (*response)) {
    sim_panel_set_error (NULL, "Error establishing breakpoint at '%s': %s", condition, response ? response : "");
    free (response);
    return -1;
    }
free (response);
return 0;
}

int
sim_panel_break_clear (PANEL *panel, const char *condition)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't clear a breakpoint from device front panel");
    return -1;
    }
if ((_panel_sendf (panel, &cmd_stat, &response, "NOBREAK %s\r", condition)) ||
    (*response)) {
    sim_panel_set_error (NULL, "Error clearing breakpoint at '%s': %s", condition, response ? response : "");
    free (response);
    return -1;
    }
free (response);
return 0;
}

int
sim_panel_break_output_set (PANEL *panel, const char *condition)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't establish an output breakpoint from device front panel");
    return -1;
    }
if ((_panel_sendf (panel, &cmd_stat, &response, "EXPECT %s\r", condition)) ||
    (*response)) {
    sim_panel_set_error (NULL, "Error establishing output breakpoint for '%s': %s", condition, response ? response : "");
    free (response);
    return -1;
    }
free (response);
return 0;
}

int
sim_panel_break_output_clear (PANEL *panel, const char *condition)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error (NULL, "Can't clear an output breakpoint from device front panel");
    return -1;
    }
if ((_panel_sendf (panel, &cmd_stat, &response, "NOEXPECT %s\r", condition)) ||
    (*response)) {
    sim_panel_set_error (NULL, "Error clearing output breakpoint for '%s': %s", condition, response ? response : "");
    free (response);
    return -1;
    }
free (response);
return 0;
}

/**

   sim_panel_gen_examine

        name_or_addr the name the simulator knows this register by
        size         the size (in local storage) of the buffer which will
                     receive the data returned when examining the simulator
        value        a pointer to the buffer which will be loaded with the
                     data returned when examining the simulator
 */

int
sim_panel_gen_examine (PANEL *panel, 
                       const char *name_or_addr,
                       size_t size,
                       void *value)
{
char *response = NULL, *c;
unsigned long long data = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (_panel_sendf (panel, &cmd_stat, &response, "EXAMINE -H %s", name_or_addr)) {
    free (response);
    return -1;
    }
c = strchr (response, ':');
if (!c) {
    sim_panel_set_error (NULL, "response: %s", response);
    free (response);
    return -1;
    }
data = strtoull (c + 1, NULL, 16);
if (little_endian)
    memcpy (value, &data, size);
else
    memcpy (value, ((char *)&data) + sizeof(data)-size, size);
free (response);
return 0;
}

/**

   sim_panel_get_history

        count        the number of instructions to return
        size         the size (in local storage) of the buffer which will
                     receive the data returned when examining the simulator
        buffer       a pointer to the buffer which will be loaded with the
                     instruction history returned from the simulator
 */

int
sim_panel_get_history (PANEL *panel, 
                       int count,
                       size_t size,
                       char *buffer)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (_panel_sendf (panel, &cmd_stat, &response, "SHOW HISTORY=%d", count)) {
    free (response);
    return -1;
    }
strncpy (buffer, response, size);
free (response);
return 0;
}

int
sim_panel_device_debug_mode (PANEL *panel, 
                             const char *device,
                             int set_unset,
                             const char *mode_bits)
{
char *response = NULL;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if ((device != NULL) &&
    ((_panel_sendf (panel, &cmd_stat, &response, "SHOW %s", device) ||
     (cmd_stat)))) {
    sim_panel_set_error (NULL, "Can't %s Debug Mode: '%s' on Device '%s': %s", 
                               set_unset ? "Enable" : "Disable", mode_bits ? mode_bits : "", device, response);
    free (response);
    return -1;
    }
free (response);
response = NULL;
if (_panel_sendf (panel, &cmd_stat, &response, "%sDEBUG %s %s", 
                         set_unset ? "" : "NO", device ? device : "", mode_bits ? mode_bits : "") ||
    (cmd_stat)) {
    sim_panel_set_error (NULL, "Can't %s Debug Mode: '%s' on Device '%s': %s", 
                               set_unset ? "Enable" : "Disable", mode_bits ? mode_bits : "", device, response);
    free (response);
    return -1;
    }
free (response);
return 0;
}

/**

   sim_panel_gen_deposit

        name_or_addr the name the simulator knows this register by
        size         the size (in local storage) of the buffer which
                     contains the data to be deposited into the simulator
        value        a pointer to the buffer which contains the data to 
                     be deposited into the simulator
 */

int
sim_panel_gen_deposit (PANEL *panel, 
                       const char *name_or_addr,
                       size_t size,
                       const void *value)
{
unsigned long long data = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (little_endian)
    memcpy (&data, value, size);
else
    memcpy (((char *)&data) + sizeof(data)-size, value, size);
if (_panel_sendf (panel, &cmd_stat, NULL, "DEPOSIT -H %s %llx", name_or_addr, data))
    return -1;
return 0;
}

/**

   sim_panel_mem_examine

        addr_size    the size (in local storage) of the buffer which 
                     contains the memory address of the data to be examined
                     in the simulator
        addr         a pointer to the buffer containing the memory address
                     of the data to be examined in the simulator
        value_size   the size (in local storage) of the buffer which will
                     receive the data returned when examining the simulator
        value        a pointer to the buffer which will be loaded with the
                     data returned when examining the simulator
 */

int
sim_panel_mem_examine (PANEL *panel, 
                       size_t addr_size,
                       const void *addr,
                       size_t value_size,
                       void *value)
{
char *response = NULL, *c;
unsigned long long data = 0, address = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (little_endian)
    memcpy (&address, addr, addr_size);
else
    memcpy (((char *)&address) + sizeof(address)-addr_size, addr, addr_size);
if (_panel_sendf (panel, &cmd_stat, &response, (panel->radix == 16) ? "EXAMINE -H %llx" : "EXAMINE -H %llo", address)) {
    free (response);
    return -1;
    }
c = strchr (response, ':');
if (!c) {
    sim_panel_set_error (NULL, "%s", response);
    free (response);
    return -1;
    }
data = strtoull (c + 1, NULL, 16);
if (little_endian)
    memcpy (value, &data, value_size);
else
    memcpy (value, ((char *)&data) + sizeof(data)-value_size, value_size);
free (response);
return 0;
}

/**

   sim_panel_mem_deposit

        addr_size    the size (in local storage) of the buffer which 
                     contains the memory address of the data to be deposited
                     into the simulator
        addr         a pointer to the buffer containing the memory address
                     of the data to be deposited into the simulator
        value_size   the size (in local storage) of the buffer which will
                     contains the data to be deposited into the simulator
        value        a pointer to the buffer which contains the data to be
                     deposited into the simulator
 */

 int
sim_panel_mem_deposit (PANEL *panel, 
                       size_t addr_size,
                       const void *addr,
                       size_t value_size,
                       const void *value)
{
unsigned long long data = 0, address = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (little_endian) {
    memcpy (&data, value, value_size);
    memcpy (&address, addr, addr_size);
    }
else {
    memcpy (((char *)&data) + sizeof(data)-value_size, value, value_size);
    memcpy (((char *)&address) + sizeof(address)-addr_size, addr, addr_size);
    }
if (_panel_sendf (panel, &cmd_stat, NULL, (panel->radix == 16) ? "DEPOSIT -H %llx %llx" : "DEPOSIT -H %llo %llx", address, data))
    return -1;
return 0;
}

/**

   sim_panel_mem_deposit_instruction

        addr_size    the size (in local storage) of the buffer which 
                     contains the memory address of the data to be deposited
                     into the simulator
        addr         a pointer to the buffer containing the memory address
                     of the data to be deposited into the simulator
        instruction  a pointer to the buffer that contains the mnemonic 
                     instruction to be deposited at the indicated address
 */

 int
sim_panel_mem_deposit_instruction (PANEL *panel, 
                                   size_t addr_size,
                                   const void *addr,
                                   const char *instruction)
{
unsigned long long address = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (little_endian)
    memcpy (&address, addr, addr_size);
else
    memcpy (((char *)&address) + sizeof(address)-addr_size, addr, addr_size);
if (_panel_sendf (panel, &cmd_stat, NULL, (panel->radix == 16) ? "DEPOSIT -H %llx %s" : "DEPOSIT -H %llo %s", address, instruction))
    return -1;
return 0;
}

/**
   sim_panel_set_register_value

        name        the name of a simulator register or a memory address
                    which is to receive a new value
        value       the new value in character string form.  The string 
                    must be in the native/natural radix that the simulator 
                    uses when referencing that register

 */

int
sim_panel_set_register_value (PANEL *panel,
                              const char *name,
                              const char *value)
{
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error (NULL, "Not Halted");
    return -1;
    }
if (_panel_sendf (panel, &cmd_stat, NULL, "DEPOSIT %s %s", name, value))
    return -1;
return 0;
}

/**
   sim_panel_mount

        device      the name of a simulator device/unit
        switches    any switches appropriate for the desire attach
        path        the path on the local system to be attached

 */
int
sim_panel_mount (PANEL *panel,
                 const char *device,
                 const char *switches,
                 const char *path)
{
char *response = NULL;
OperationalState OrigState;
int stat = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
OrigState = panel->State;
if (OrigState == Run)
    sim_panel_exec_halt (panel);
do {
    if (_panel_sendf (panel, &cmd_stat, &response, "ATTACH %s %s %s", switches, device, path)) {
        stat = -1;
        break;
        }
    if (cmd_stat) {
        sim_panel_set_error (NULL, response);
        stat = -1;
        }
    } while (0);
if (OrigState == Run)
    sim_panel_exec_run (panel);
free (response);
return stat;
}

/**
   sim_panel_dismount

        device      the name of a simulator device/unit

 */
int
sim_panel_dismount (PANEL *panel,
                    const char *device)
{
char *response = NULL;
OperationalState OrigState;
int stat = 0;
int cmd_stat;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error (NULL, "Invalid Panel");
    return -1;
    }
OrigState = panel->State;
if (OrigState == Run)
    sim_panel_exec_halt (panel);
do {
    if (_panel_sendf (panel, &cmd_stat, &response, "DETACH %s", device)) {
        stat = -1;
        break;
        }
    if (cmd_stat) {
        sim_panel_set_error (NULL, "%s", response);
        stat = -1;
        }
    } while (0);
if (OrigState == Run)
    sim_panel_exec_run (panel);
free (response);
return stat;
}


static void *
_panel_reader(void *arg)
{
PANEL *p = (PANEL*)arg;
REG *r = NULL;
int sched_policy;
struct sched_param sched_priority;
char buf[4096];
int buf_data = 0;
int processing_register_output = 0;
int io_wait_done = 0;

/* 
   Boost Priority for this response processing thread to quickly digest 
   arriving data.
 */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);
pthread_setspecific (panel_thread_id, "reader");
_panel_debug (p, DBG_THR, "Starting", NULL, 0);

buf[buf_data] = '\0';
pthread_mutex_lock (&p->io_lock);
if (!p->parent) {
    while (1) {
        int new_data = sim_read_sock (p->sock, &buf[buf_data], sizeof(buf)-(buf_data+1));

        if (new_data <= 0) {
            sim_panel_set_error (NULL, "%s after reading %d bytes: %s", sim_get_err_sock("Unexpected socket read"), buf_data, buf);
            _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
            p->State = Error;
            break;
            }
        _panel_debug (p, DBG_RCV, "Startup receive of %d bytes: ", &buf[buf_data], new_data, new_data);
        buf_data += new_data;
        buf[buf_data] = '\0';
        if (!memcmp (mantra, buf, sizeof (mantra))) {   /* strip initial telnet mantra from input stream */
            memmove (buf, buf + sizeof (mantra), 1 + buf_data - sizeof (mantra));
            buf_data -= sizeof (mantra);
            }
        if ((size_t)buf_data < strlen (sim_prompt))
            continue;
        if (!strcmp (sim_prompt, &buf[buf_data - strlen (sim_prompt)])) {
            memmove (buf, &buf[buf_data - strlen (sim_prompt)], strlen (sim_prompt) + 1);
            buf_data = strlen (sim_prompt);
            break;
            }
        }
    }
p->io_thread_running = 1;
pthread_mutex_unlock (&p->io_lock);
pthread_cond_signal (&p->startup_done);   /* Signal we're ready to go */
msleep (100);
pthread_mutex_lock (&p->io_lock);
while ((p->sock != INVALID_SOCKET) &&
       (p->State != Error)) {
    int new_data;
    char *s, *e, *eol;

    if (NULL == strchr (buf, '\n')) {
        pthread_mutex_unlock (&p->io_lock);
        new_data = sim_read_sock (p->sock, &buf[buf_data], sizeof(buf)-(buf_data+1));
        pthread_mutex_lock (&p->io_lock);
        if (new_data <= 0) {
            sim_panel_set_error (NULL, "%s", sim_get_err_sock("Unexpected socket read"));
            _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
            p->State = Error;
            break;
            }
        _panel_debug (p, DBG_RCV, "Received %d bytes: ", &buf[buf_data], new_data, new_data);
        buf_data += new_data;
        buf[buf_data] = '\0';
        }
    s = buf;
    while ((eol = strchr (s, '\n'))) {
        /* Line to process */
        *eol++ = '\0';
        while ((*s) && (s[strlen(s)-1] == '\r'))
            s[strlen(s)-1] = '\0';
        if (processing_register_output) {
            e = strchr (s, ':');
            if (e) {
                size_t i;
                char smp_dev[32], smp_reg[32], smp_ind[32];
                unsigned int bit;

                *e++ = '\0';
                if (!strcmp("Time", s)) {
                    p->simulation_time = strtoull (e, NULL, 10);
                    s = eol;
                    while (isspace(0xFF & (*s)))
                        ++s;
                    continue;                                   /* process next line */
                    }
                if ((*s == '}') && 
                    (3 == sscanf (s, "}%s %s %s", smp_dev, smp_reg, smp_ind))) {   /* Register bit Sample Data? */
                    r = NULL;
                    for (i=0; i<p->reg_count; i++) {
                        if (p->regs[i].bits == NULL)
                            continue;
                        if ((!strcmp (smp_reg, p->regs[i].name)) && 
                            ((!p->device_name) || (!strcmp (smp_dev, p->device_name)))) {
                            r = &p->regs[i];
                            break;
                            }
                        }
                    if (r) {
                        for (bit = 0; bit < r->bit_count; bit++) {
                            int val = (int)strtol (e, &e, 10);
                            r->bits[bit] = val;
                            if (*e == ',')
                                ++e;
                            else
                                break;
                            }
                        s = eol;
                        }
                    while (isspace(0xFF & (*s)))
                        ++s;
                    r = NULL;
                    continue;                                   /* process next line */
                    }
                if (!strncmp (s + strlen (sim_prompt), register_ind_echo, strlen (register_ind_echo) - 1)) {
                    e = s + strlen (sim_prompt) + strlen (register_ind_echo);
                    r = NULL;
                    for (i=0; i<p->reg_count; i++) {
                        if (p->regs[i].indirect && (!strcmp(p->regs[i].name, e))) {
                            r = &p->regs[i];
                            break;
                            }
                        }
                    s = eol;
                    while (isspace(0xFF & (*s)))
                        ++s;
                    if (r)
                        continue;                               /* process next line */
                    }
                if (r) {
                    if (strcmp (s, r->name)) {
                        unsigned long long data;

                        data = strtoull (e, NULL, 16);
                        if (little_endian)
                            memcpy (r->addr, &data, r->size);
                        else
                            memcpy (r->addr, ((char *)&data) + sizeof(data)-r->size, r->size);
                        r = NULL;
                        }
                    s = eol;
                    while (isspace(0xFF & (*s)))
                        ++s;
                    continue;                               /* process next line */
                    }
                for (i=0; i<p->reg_count; i++) {
                    if (p->regs[i].element_count == 0) {
                        if (!strcmp(p->regs[i].name, s)) {
                            unsigned long long data;

                            data = strtoull (e, NULL, 16);
                            if (little_endian)
                                memcpy (p->regs[i].addr, &data, p->regs[i].size);
                            else
                                memcpy (p->regs[i].addr, ((char *)&data) + sizeof(data)-p->regs[i].size, p->regs[i].size);
                            break;
                            }
                        }
                    else {
                        size_t name_len = strlen (p->regs[i].name);

                        if ((0 == memcmp (p->regs[i].name, s, name_len)) && (s[name_len] == '[')) {
                            size_t array_index = (size_t)atoi (s + name_len + 1);
                            size_t end_index = array_index;
                            char *end = strchr (s + name_len + 1, '[');

                            if (end)
                                end_index = (size_t)atoi (end + 1);
                            if (strcmp (e, " same as above")) 
                                p->array_element_data = strtoull (e, NULL, 16);
                            while (array_index <= end_index) {
                                if (little_endian)
                                    memcpy ((char *)(p->regs[i].addr) + (array_index * p->regs[i].size), &p->array_element_data, p->regs[i].size);
                                else
                                    memcpy ((char *)(p->regs[i].addr) + (array_index * p->regs[i].size), ((char *)&p->array_element_data) + sizeof(p->array_element_data)-p->regs[i].size, p->regs[i].size);
                                ++array_index;
                                }
                            break;
                            }
                        }
                    }
                if (i != p->reg_count) {
                    s = eol;
                    while (isspace(0xFF & (*s)))
                        ++s;
                    continue;
                    }
                --e;
                *e = ':';
                /* Unexpected Register Data Found (or other output containing a : character) */
                }
            }
        if ((strlen (s) > strlen (sim_prompt)) && (!strcmp (s + strlen (sim_prompt), register_repeat_end))) {
            _panel_debug (p, DBG_RCV, "*Repeat Block Complete (Accumulated Data = %d)", NULL, 0, (int)p->io_response_data);
            if (p->callback) {
                pthread_mutex_unlock (&p->io_lock);
                p->callback (p, p->simulation_time_base + p->simulation_time, p->callback_context);
                pthread_mutex_lock (&p->io_lock);
                }
            processing_register_output = 0;
            p->io_response_data = 0;
            p->io_response[p->io_response_data] = '\0';
            goto Start_Next_Line;
            }
        if ((strlen (s) > strlen (sim_prompt)) && 
            ((!strcmp (s + strlen (sim_prompt), register_repeat_start)) ||
             (!strcmp (s + strlen (sim_prompt), register_get_start)))) {
            _panel_debug (p, DBG_RCV, "*Repeat/Register Block Starting", NULL, 0);
            processing_register_output = 1;
            goto Start_Next_Line;
            }
        if ((strlen (s) > strlen (sim_prompt)) && 
            (!strcmp (s + strlen (sim_prompt), register_get_end))) {
            _panel_debug (p, DBG_RCV, "*Register Block Complete", NULL, 0);
            p->io_waiting = 0;
            processing_register_output = 0;
            pthread_cond_signal (&p->io_done);
            goto Start_Next_Line;
            }
        if ((strlen (s) > strlen (sim_prompt)) && (!strcmp (s + strlen (sim_prompt), command_done_echo))) {
            _panel_debug (p, DBG_RCV, "*Received Command Complete", NULL, 0);
            p->io_waiting = 0;
            pthread_cond_signal (&p->io_done);
            goto Start_Next_Line;
            }
        /* Non Register Data Found (echo of EXAMINE or other commands and/or command output) */
        if (p->io_response_data + strlen (s) + 3 > p->io_response_size) {
            char *t = (char *)_panel_malloc (p->io_response_data + strlen (s) + 3);

            if (t == NULL) {
                _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
                p->State = Error;
                break;
                }
            memcpy (t, p->io_response, p->io_response_data);
            free (p->io_response);
            p->io_response = t;
            p->io_response_size = p->io_response_data + strlen (s) + 3;
            }
        _panel_debug (p, DBG_RCV, "Receive Data Accumulated: '%s'", NULL, 0, s);
        strcpy (p->io_response + p->io_response_data, s);
        p->io_response_data += strlen(s);
        strcpy (p->io_response + p->io_response_data, "\r\n");
        p->io_response_data += 2;
        if ((!p->parent) && 
            (p->completion_string) && 
            (!memcmp (s, p->completion_string, strlen (p->completion_string)))) {
            _panel_debug (p, DBG_RCV, "Match with potentially coalesced additional data: '%s'", NULL, 0, p->completion_string);
            if (eol < &buf[buf_data])
                memset (s + strlen (s), ' ', eol - (s + strlen (s)));
            break;
            }
Start_Next_Line:
        s = eol;
        while (isspace(0xFF & (*s)))
            ++s;
        }
    memmove (buf, s, buf_data - (s - buf) + 1);
    buf_data = strlen (buf);
    if (buf_data)
        _panel_debug (p, DBG_RSP, "Remnant Buffer Contents: '%s'", NULL, 0, buf);
    if ((!p->parent) && 
        (p->completion_string) && 
        (!memcmp (buf, p->completion_string, strlen (p->completion_string)))) {
        _panel_debug (p, DBG_RCV, "*Received Command Complete - Match: '%s'", NULL, 0, p->completion_string);
        io_wait_done = 1;
        }
    if (!memcmp ("Simulator Running...", buf, 20)) {
        _panel_debug (p, DBG_RSP, "State transitioning to Run", NULL, 0);
        p->State = Run;
        buf_data -= 20;
        if (buf_data) {
            memmove (buf, buf + 20, buf_data + 1);
            _panel_debug (p, DBG_RSP, "Remnant Buffer Contents: '%s'", NULL, 0, buf);
            }
        else
            buf[buf_data] = '\0';
        if (io_wait_done) {                     /* someone waiting for this? */
            _panel_debug (p, DBG_RCV, "*Match Command Complete - Match signaling waiting thread", NULL, 0);
            io_wait_done = 0;
            p->io_waiting = 0;
            p->completion_string = NULL;
            pthread_cond_signal (&p->io_done);
            /* Let this state transition propagate to the interested thread(s) */
            /* before processing remaining buffered data */
            pthread_mutex_unlock (&p->io_lock);
            msleep (100);
            pthread_mutex_lock (&p->io_lock);
            }
        }
    if ((p->State == Run) && (!strcmp (buf, sim_prompt))) {
        _panel_debug (p, DBG_RSP, "State transitioning to Halt: io_wait_done: %d", NULL, 0, io_wait_done);
        p->State = Halt;
        free (p->halt_reason);
        p->halt_reason = (char *)_panel_malloc (1 + strlen (p->io_response));
        if (p->halt_reason == NULL) {
            _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
            p->State = Error;
            break;
            }
        strcpy (p->halt_reason, p->io_response);
        }
    if (io_wait_done) {
        _panel_debug (p, DBG_RCV, "*Match Command Complete - Match signaling waiting thread", NULL, 0);
        io_wait_done = 0;
        p->io_waiting = 0;
        p->completion_string = NULL;
        pthread_cond_signal (&p->io_done);
        }
    }
if (p->io_waiting) {
    _panel_debug (p, DBG_THR, "Receive: restarting waiting thread while exiting", NULL, 0);
    p->io_waiting = 0;
    pthread_cond_signal (&p->io_done);
    }
_panel_debug (p, DBG_THR, "Exiting", NULL, 0);
pthread_setspecific (panel_thread_id, NULL);
p->io_thread_running = 0;
pthread_mutex_unlock (&p->io_lock);
return NULL;
}

static void *
_panel_callback(void *arg)
{
PANEL *p = (PANEL*)arg;
int sched_policy;
struct sched_param sched_priority;
char *buf = NULL;
size_t buf_data = 0;
unsigned int callback_count = 0;
int cmd_stat;

/* 
   Boost Priority for timer thread so it doesn't compete 
   with compute bound activities.
 */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);
pthread_setspecific (panel_thread_id, "callback");
_panel_debug (p, DBG_THR, "Starting", NULL, 0);

pthread_mutex_lock (&p->io_lock);
p->callback_thread_running = 1;
pthread_mutex_unlock (&p->io_lock);
pthread_cond_signal (&p->startup_done);   /* Signal we're ready to go */
msleep (100);
pthread_mutex_lock (&p->io_lock);
while ((p->sock != INVALID_SOCKET) && 
       (p->usecs_between_callbacks) &&
       (p->State != Error)) {
    int interval = p->usecs_between_callbacks;
    int new_register = p->new_register;

    p->new_register = 0;
    pthread_mutex_unlock (&p->io_lock);

    if (new_register)           /* need to get and send updated register info */
        _panel_register_query_string (p, &buf, &buf_data);

    /* twice a second activities:                                               */
    /*  1) update the query string if it has changed                            */
    /*     (only really happens at startup)                                     */
    /*  2) update register state by polling if the simulator is halted          */
    msleep (500);
    pthread_mutex_lock (&p->io_lock);
    if (new_register) {
        size_t repeat_data = strlen (register_repeat_prefix) +  /* prefix */
                             20                              +  /* max int width */
                             strlen (register_repeat_units)  +  /* units and spacing */
                             buf_data                        +  /* command contents */
                             1                               +  /* ; */
                             strlen (register_repeat_start)  +  /* auto repeat begin */
                             1                               +  /* ; */
                             strlen (register_repeat_end)    +  /* auto repeat completion */
                             1                               +  /* carriage return */
                             1;                                 /* NUL */
        char *repeat = (char *)malloc (repeat_data);
        char *c;

        c = strstr (buf, register_get_start);       /* remove register_get_start string and anything before it */
        if (c) {                                    /* always true */
            buf_data -= (c - buf) + strlen (register_get_start);
            c += strlen (register_get_start);
            }
        sprintf (repeat, "%s%d%s%s%*.*s", register_repeat_prefix, 
                                     p->usecs_between_callbacks, 
                                     register_repeat_units, 
                                     register_repeat_start,
                                     (int)buf_data, (int)buf_data, c);
        pthread_mutex_unlock (&p->io_lock);
        c = strstr (repeat, register_get_end);      /* remove register_done_echo string and */
        if (c)                                      /* always true */
            strcpy (c, register_repeat_end);        /* replace it with the register_repeat_end string */
        if (_panel_sendf (p, &cmd_stat, NULL, "%s", repeat)) {
            pthread_mutex_lock (&p->io_lock);
            free (repeat);
            break;
            }
        pthread_mutex_lock (&p->io_lock);
        free (repeat);
        }
    /* when halted, we directly poll the halted system to get updated */
    /* register state which may have changed due to panel activities */
    if (p->State == Halt) {
        pthread_mutex_unlock (&p->io_lock);
        if (_panel_get_registers (p, 1, NULL)) {
            pthread_mutex_lock (&p->io_lock);
            break;
            }
        if (p->callback)
            p->callback (p, p->simulation_time_base + p->simulation_time, p->callback_context);
        pthread_mutex_lock (&p->io_lock);
        }
    }
pthread_mutex_unlock (&p->io_lock);
/* stop any established repeating activity in the simulator */
if (p->parent == NULL) {        /* Top level panel? */
    _panel_debug (p, DBG_THR, "Stopping All Repeats before exiting", NULL, 0);
    _panel_sendf (p, &cmd_stat, NULL, "%s", register_repeat_stop_all);
    }
else {
    _panel_debug (p, DBG_THR, "Stopping Repeats before exiting", NULL, 0);
    _panel_sendf (p, &cmd_stat, NULL, "%s", register_repeat_stop);
    }
pthread_mutex_lock (&p->io_lock);
_panel_debug (p, DBG_THR, "Exiting", NULL, 0);
pthread_setspecific (panel_thread_id, NULL);
p->callback_thread_running = 0;
pthread_mutex_unlock (&p->io_lock);
free (buf);
return NULL;
}

const char *sim_panel_get_error (void)
{
return (sim_panel_error_buf ? sim_panel_error_buf : "");
}

void sim_panel_clear_error (void)
{
if (sim_panel_error_bufsize)
    free (sim_panel_error_buf);
sim_panel_error_buf = NULL;
sim_panel_error_bufsize = 0;
}

#if defined (_WIN32)
#define vsnprintf _vsnprintf
#endif

static int sim_panel_set_error (PANEL *p, const char *fmt, ...)
{
va_list arglist;
int len;

if (p) {
    pthread_mutex_lock (&p->io_lock);
    p->State = Error;
    pthread_mutex_unlock (&p->io_lock);
    }
if (sim_panel_error_bufsize == 0) {
    sim_panel_error_bufsize = 2048;
    sim_panel_error_buf = (char *) malloc (sim_panel_error_bufsize);
    if (sim_panel_error_buf == NULL) {
        sim_panel_error_buf = (char *)"sim_panel_set_error(): Out of Memory\n";
        sim_panel_error_bufsize = 0;
        return -1;
        }
    }
sim_panel_error_buf[sim_panel_error_bufsize-1] = '\0';

while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
    len = vsnprintf (sim_panel_error_buf, sim_panel_error_bufsize-1, fmt, arglist);
    va_end (arglist);

    if (len < 0)        /* Format encoding error? */
        break;
    /* If the formatted result didn't fit into the buffer, then grow the buffer and try again */
    if (len >= (int)(sim_panel_error_bufsize-1)) {
        free (sim_panel_error_buf);
        sim_panel_error_bufsize = sim_panel_error_bufsize * 2;
        while ((int)sim_panel_error_bufsize < len + 2)
            sim_panel_error_bufsize = sim_panel_error_bufsize * 2;
        sim_panel_error_buf = (char *) malloc (sim_panel_error_bufsize);
        if (sim_panel_error_buf == NULL) {
            sim_panel_error_buf = (char *)"sim_panel_set_error(): Out of Memory\n";
            sim_panel_error_bufsize = 0;
            return -1;
            }
        sim_panel_error_buf[sim_panel_error_bufsize-1] = '\0';
        continue;
        }
    break;
    }
return -1;
}

static int
_panel_vsendf_completion (PANEL *p, int *completion_status, char **response, const char *completion_string, const char *fmt, va_list arglist)
{
char stackbuf[1024];
int bufsize = sizeof(stackbuf);
char *buf = stackbuf;
int len, status_echo_len = 0, sent_len;
int post_fix_len = completion_status ? 7 + sizeof (command_done_echo) + sizeof (command_status) : 1;
int ret;

if (completion_status && completion_string)         /* one or the other, but */
    return -1;                                      /* not both */

while (1) {                                         /* format passed string, args */
    len = vsnprintf (buf, bufsize-1, fmt, arglist);

    if (len < 0)
        return sim_panel_set_error (NULL, "Format encoding error while processing '%s'", fmt);

    /* If the formatted result didn't fit into the buffer, then grow the buffer and try again */
    if ((len + post_fix_len) >= bufsize-1) {
        if (buf != stackbuf)
            free (buf);
        bufsize = bufsize * 2;
        if (bufsize < (len + post_fix_len + 2))
            bufsize = len + post_fix_len + 2;
        buf = (char *) _panel_malloc (bufsize);
        if (buf == NULL)
            return -1;
        buf[bufsize-1] = '\0';
        continue;
        }
    break;
    }

if (len && (buf[len-1] != '\r')) {
    strcpy (&buf[len], "\r");           /* Make sure command line is terminated */
    ++len;
    }

pthread_mutex_lock (&p->io_command_lock);
++p->command_count;
if (completion_status || completion_string) {
    if (completion_status) {
        sprintf (&buf[len], "%s\r%s\r", command_status, command_done_echo);
        status_echo_len = strlen (&buf[len]);
        }
    pthread_mutex_lock (&p->io_lock);
    p->completion_string = completion_string;
    if (p->io_response_data)
        _panel_debug (p, DBG_RCV, "Receive Data Discarded: ", p->io_response, p->io_response_data);
    p->io_response_data = 0;
    p->io_waiting = 1;
    }

_panel_debug (p, DBG_REQ, "Command %d Request%s: %*.*s", NULL, 0, p->command_count, completion_status ? " (with response)" : "", len, len, buf);
ret = ((len + status_echo_len) == (sent_len = _panel_send (p, buf, len + status_echo_len))) ? 0 : -1;

if (completion_status || completion_string) {
    if (ret) {                                      /* Send failed? */
        p->completion_string = NULL;
        p->io_waiting = 0;
        }
    else {                                          /* Sent OK? */
        char *tresponse = NULL;

        while (p->io_waiting)
            pthread_cond_wait (&p->io_done, &p->io_lock); /* Wait for completion */
        tresponse = (char *)_panel_malloc (p->io_response_data + 1);
        if (0 == memcmp (buf, p->io_response + strlen (sim_prompt), len)) {
            char *eol, *status;
            memcpy (tresponse, p->io_response + strlen (sim_prompt) + len + 1, p->io_response_data + 1 - (strlen (sim_prompt) + len + 1));
            if (completion_status) {
                *completion_status = -1;
                status = strstr (tresponse, command_status);
                if (status) {
                    *(status - strlen (sim_prompt)) = '\0';
                    status += strlen (command_status) + 2;
                    eol = strchr (status, '\r');
                    if (eol)
                        *eol = '\0';
                    sscanf (status, "Status:%08X-", completion_status);
                    }
                }
            }
        else
            memcpy (tresponse, p->io_response, p->io_response_data + 1);
        if (response) {
            *response = tresponse;
            if (completion_status)
                _panel_debug (p, DBG_RSP, "Command %d Response(Status=%d): '%s'", NULL, 0, p->command_count, *completion_status, *response);
            else
                _panel_debug (p, DBG_RSP, "Command %d Response - Match '%s': '%s'", NULL, 0, p->command_count, completion_string, *response);
            }
        else {
            free (tresponse);
            if (p->io_response_data) {
                if (completion_status)
                    _panel_debug (p, DBG_RSP, "Discarded Unwanted Command %d Response Data(Status=%d):", p->io_response, p->io_response_data, p->command_count, *completion_status);
                else
                    _panel_debug (p, DBG_RSP, "Discarded Unwanted Command %d Response Data - Match '%s':", p->io_response, p->io_response_data, p->command_count, completion_string);
                }
            }
        }
    p->completion_string = NULL;
    p->io_response_data = 0;
    p->io_response[0] = '\0';
    pthread_mutex_unlock (&p->io_lock);
    }
else {
    if (ret)
        sim_panel_set_error (p, "Unexpected send length: %d, expected: %d", sent_len, len + status_echo_len);
    }
pthread_mutex_unlock (&p->io_command_lock);

if (buf != stackbuf)
    free (buf);
return ret;
}

static int
_panel_sendf (PANEL *p, int *completion_status, char **response, const char *fmt, ...)
{
va_list arglist;
int status;

va_start (arglist, fmt);
status = _panel_vsendf_completion (p, completion_status, response, NULL, fmt, arglist);
va_end (arglist);
return status;
}

static int
_panel_sendf_completion (PANEL *p, char **response, const char *completion, const char *fmt, ...)
{
va_list arglist;
int status;

va_start (arglist, fmt);
status = _panel_vsendf_completion (p, NULL, response, completion, fmt, arglist);
va_end (arglist);
return status;
}


#ifdef  __cplusplus
}
#endif
