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
    size_t                  reg_query_size;
    unsigned long long      array_element_data;
    OperationalState        State;
    unsigned long long      simulation_time;
    pthread_mutex_t         lock;
    pthread_t               io_thread;
    int                     io_thread_running;
    pthread_mutex_t         io_lock;
    pthread_mutex_t         io_send_lock;
    int                     io_reg_query_pending;
    int                     io_waiting;
    char                    *io_response;
    size_t                  io_response_data;
    size_t                  io_response_size;
    pthread_cond_t          io_done;
    pthread_cond_t          startup_cond;
    PANEL_DISPLAY_PCALLBACK callback;
    pthread_t               callback_thread;
    int                     callback_thread_running;
    void                    *callback_context;
    int                     callbacks_per_second;
    int                     debug;
    char                    *simulator_version;
    int                     radix;
    FILE                    *Debug;
#if defined(_WIN32)
    HANDLE                  hProcess;
#else
    pid_t                   pidProcess;
#endif
    };

static const char *sim_prompt = "sim> ";
static const char *register_get_prefix = "show time";
static const char *register_get_echo = "# REGISTERS-DONE";
static const char *register_dev_echo = "# REGISTERS-FOR-DEVICE:";
static const char *register_ind_echo = "# REGISTER-INDIRECT:";
static const char *command_done_echo = "# COMMAND-DONE";
static int little_endian;
static void *_panel_reader(void *arg);
static void *_panel_callback(void *arg);
static void sim_panel_set_error (const char *fmt, ...);


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
    sim_panel_set_error ("Out of Memory");
return p;
}

static void _panel_debug (PANEL *p, int dbits, const char *fmt, const char *buf, int bufsize, ...)
{
if (p && p->Debug && (dbits & p->debug)) {
    int i;
    struct timespec time_now;
    va_list arglist;
    char timestamp[32];
    size_t obufsize = 10240 + 8*bufsize;
    char *obuf = (char *)_panel_malloc (obufsize);

    clock_gettime(CLOCK_REALTIME, &time_now);
    sprintf (timestamp, "%lld.%03d ", (long long)(time_now.tv_sec), (int)(time_now.tv_nsec/1000000));
    
    va_start (arglist, bufsize);
    vsnprintf (obuf, obufsize - 1, fmt, arglist);
    va_end (arglist);

    
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
    fprintf(p->Debug, "%s%s\n", timestamp, obuf);
    free (obuf);
    }
}

void
sim_panel_set_debug_file (PANEL *panel, const char *debug_file)
{
if (!panel)
    return;
panel->Debug = fopen(debug_file, "w");
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

if (p->sock == INVALID_SOCKET) {
    sim_panel_set_error ("Invalid Socket for write");
    p->State = Error;
    return -1;
    }
pthread_mutex_lock (&p->io_send_lock);
while (len) {
    int bsent = sim_write_sock (p->sock, msg, len);
    if (bsent < 0) {
        sim_panel_set_error ("%s", sim_get_err_sock("Error writing to socket"));
        p->State = Error;
        pthread_mutex_unlock (&p->io_send_lock);
        return bsent;
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
_panel_sendf (PANEL *p, int wait_for_completion, char **response, const char *fmt, ...);

static int
_panel_register_query_string (PANEL *panel, char **buf, size_t *buf_size)
{
size_t i, j, buf_data, buf_needed = 0;
char *dev;

pthread_mutex_lock (&panel->io_lock);
buf_needed = 2 + strlen (register_get_prefix);  /* SHOW TIME */
for (i=0; i<panel->reg_count; i++) {
    buf_needed += 9 + strlen (panel->regs[i].name) + (panel->regs[i].device_name ? strlen (panel->regs[i].device_name) : 0);
    if (panel->regs[i].element_count > 0)
        buf_needed += 4 + 6 /* 6 digit register array index */;
    if (panel->regs[i].indirect)
        buf_needed += 12 + strlen (register_ind_echo) + strlen (panel->regs[i].name);
    }
buf_needed += 10 + strlen (register_get_echo); /* # REGISTERS-DONE */
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
sprintf (*buf + buf_data, "%s\r", register_get_prefix);
buf_data += strlen (*buf + buf_data);
dev = "";
for (i=j=0; i<panel->reg_count; i++) {
    char *reg_dev = panel->regs[i].device_name ? panel->regs[i].device_name : "";

    if (panel->regs[i].indirect)
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
        sprintf (*buf + buf_data, "%s%s%s\r", (i == 0)? "" : "\r", register_dev_echo, reg_dev);
        buf_data += strlen (*buf + buf_data);
        dev = reg_dev;
        j = 0;
        *buf_size = buf_needed;
        }
    if (panel->regs[i].element_count == 0) {
        if (j == 0)
            sprintf (*buf + buf_data, "E -H %s %s", dev, panel->regs[i].name);
        else
            sprintf (*buf + buf_data, ",%s", panel->regs[i].name);
        }
    else {
        if (j == 0)
            sprintf (*buf + buf_data, "E -H %s %s[0:%d]", dev, panel->regs[i].name, panel->regs[i].element_count-1);
        else
            sprintf (*buf + buf_data, ",%s[0:%d]", panel->regs[i].name, panel->regs[i].element_count-1);
        }
    ++j;
    buf_data += strlen (*buf + buf_data);
    }
if (buf_data && ((*buf)[buf_data-1] != '\r')) {
    strcpy (*buf + buf_data, "\r");
    buf_data += strlen (*buf + buf_data);
    }
for (i=j=0; i<panel->reg_count; i++) {
    char *reg_dev = panel->regs[i].device_name ? panel->regs[i].device_name : "";

    if (!panel->regs[i].indirect)
        continue;
    sprintf (*buf + buf_data, "%s%s\rE -H %s %s,$\r", register_ind_echo, panel->regs[i].name, reg_dev, panel->regs[i].name);
    buf_data += strlen (*buf + buf_data);
    }
strcpy (*buf + buf_data, register_get_echo);
buf_data += strlen (*buf + buf_data);
strcpy (*buf + buf_data, "\r");
buf_data += strlen (*buf + buf_data);
*buf_size = buf_data;
pthread_mutex_unlock (&panel->io_lock);
return 0;
}

static PANEL **panels = NULL;
static int panel_count = 0;

static void
_panel_cleanup (void)
{
while (panel_count)
    sim_panel_destroy (*panels);
}

static void
_panel_register_panel (PANEL *p)
{
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
size_t i, device_num;
char hostport[64];
union {int i; char c[sizeof (int)]; } end_test;

if (simulator_panel) {
    for (device_num=0; device_num < simulator_panel->device_count; ++device_num)
        if (simulator_panel->devices[device_num] == NULL)
            break;
    if (device_num == simulator_panel->device_count) {
        sim_panel_set_error ("No free panel devices slots available %s simulator.  All %d slots are used.", simulator_panel->path, (int)simulator_panel->device_count);
        return NULL;
        }
    p = (PANEL *)_panel_malloc (sizeof(*p));
    if (p == NULL)
        goto Error_Return;
    memset (p, 0, sizeof(*p));
    p->device_name = (char *)_panel_malloc (1 + strlen (device_name));
    if (p->device_name == NULL)
        goto Error_Return;
    strcpy (p->device_name, device_name);
    p->parent = simulator_panel;
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
        sim_panel_set_error ("Can't stat simulator configuration '%s': %s", sim_config, strerror(errno));
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
        sim_panel_set_error ("Can't open configuration file '%s': %s", sim_config, strerror(errno));
        goto Error_Return;
        }
    p->temp_config = (char *)_panel_malloc (strlen (sim_config) + 40);
    if (p->temp_config == NULL)
        goto Error_Return;
    sprintf (p->temp_config, "%s-Panel-%d", sim_config, getpid());
    fOut = fopen (p->temp_config, "w");
    if (fOut == NULL) {
        sim_panel_set_error ("Can't create temporary configuration file '%s': %s", p->temp_config, strerror(errno));
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
    sim_panel_set_debug_file (p, debug_file);
    sim_panel_set_debug_mode (p, DBG_XMT|DBG_RCV);
    _panel_debug (p, DBG_XMT|DBG_RCV, "Creating Simulator Process %s\n", NULL, 0, sim_path);
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
        }
    else { /* Creation Problem */
        sim_panel_set_error ("CreateProcess Error: %d", GetLastError());
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
        sim_panel_set_error ("fork() Error: %s", strerror(errno));
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
        sim_panel_set_error ("Can't connect to simulator Remote Console on port %s", p->hostport);
        }
    else {
        if (stat (sim_path, &statb) < 0)
            sim_panel_set_error ("Can't stat simulator '%s': %s", sim_path, strerror(errno));
        else
            sim_panel_set_error ("Can't connect to the %s simulator Remote Console on port %s, the simulator process may not have started or the simulator binary can't be found", sim_path, p->hostport);
        }
    goto Error_Return;
    }
_panel_debug (p, DBG_XMT|DBG_RCV, "Connected to simulator at %s after %dms\n", NULL, 0, p->hostport, i*100);
pthread_mutex_init (&p->io_lock, NULL);
pthread_mutex_init (&p->io_send_lock, NULL);
pthread_cond_init (&p->io_done, NULL);
pthread_cond_init (&p->startup_cond, NULL);
if (sizeof(mantra) != _panel_send (p, (char *)mantra, sizeof(mantra))) {
    sim_panel_set_error ("Error sending Telnet mantra (options): %s", sim_get_err_sock ("send"));
    goto Error_Return;
    }
if (1) {
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_mutex_lock (&p->io_lock);
    p->io_thread_running = 0;
    pthread_create (&p->io_thread, &attr, _panel_reader, (void *)p);
    pthread_attr_destroy(&attr);
    while (!p->io_thread_running)
        pthread_cond_wait (&p->startup_cond, &p->io_lock); /* Wait for thread to stabilize */
    pthread_mutex_unlock (&p->io_lock);
    pthread_cond_destroy (&p->startup_cond);
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
    if (_panel_sendf (p, 1, &p->simulator_version, "SHOW VERSION\r"))
        goto Error_Return;
    if (1) {
        int api_version = 0;
        char *c = strstr (p->simulator_version, "FrontPanel API Version");

        if ((!c) ||
            (1 != sscanf (c, "FrontPanel API Version %d", &api_version)) ||
            (api_version != SIM_FRONTPANEL_VERSION)) {
            sim_panel_set_error ("Inconsistent sim_frontpanel API version %d in simulator.  Version %d needed.-", api_version, SIM_FRONTPANEL_VERSION);
            goto Error_Return;
            }
        }
    if (1) {
        char *radix = NULL;

        if (_panel_sendf (p, 1, &radix, "SHOW %s RADIX\r", p->device_name ? p->device_name : "")) {
            free (radix);
            goto Error_Return;
            }
        sscanf (radix, "Radix=%d", &p->radix);
        free (radix);
        if ((p->radix != 16) && (p->radix != 8)) {
            sim_panel_set_error ("Unsupported Radix: %d%s%s.", p->radix, p->device_name ? " on device " : "", p->device_name ? p->device_name : "");
            goto Error_Return;
            }
        }
    }
_panel_register_panel (p);
return p;

Error_Return:
if (fIn)
    fclose (fIn);
if (fOut) {
    fclose (fOut);
    remove (p->temp_config);
    }
if (buf)
    free (buf);
if (1) {
    const char *err = sim_panel_get_error();
    char *errbuf = (char *)_panel_malloc (1 + strlen (err));

    strcpy (errbuf, err);               /* preserve error info while closing */
    sim_panel_destroy (p);
    sim_panel_set_error ("%s", errbuf);
    free (errbuf);
    }
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
    _panel_debug (panel, DBG_XMT|DBG_RCV, "Closing Panel %s\n", NULL, 0, panel->device_name? panel->device_name : panel->path);
    if (panel->devices) {
        size_t i;

        for (i=0; i<panel->device_count; i++) {
            if (panel->devices[i])
                sim_panel_destroy (panel->devices[i]);
            }
        free (panel->devices);
        panel->devices = NULL;
        }

    _panel_deregister_panel (panel);
    free (panel->path);
    free (panel->device_name);
    free (panel->config);
    if (panel->sock != INVALID_SOCKET) {
        SOCKET sock = panel->sock;
        int wait_count;

        /* First, wind down the automatic register queries */
        sim_panel_set_display_callback (panel, NULL, NULL, 0);
        /* Next, attempt a simulator shutdown */
        _panel_send (panel, "\005\rEXIT\r", 7);
        /* Wait for up to 2 seconds for a graceful shutdown */
        for (wait_count=0; panel->io_thread_running && (wait_count<20); ++wait_count)
            msleep (100);
        /* Now close the socket which should stop a pending read which hasn't completed */
        panel->sock = INVALID_SOCKET;
        sim_close_sock (sock);
        pthread_join (panel->io_thread, NULL);
        pthread_mutex_destroy (&panel->io_lock);
        pthread_mutex_destroy (&panel->io_send_lock);
        pthread_cond_destroy (&panel->io_done);
        }
#if defined(_WIN32)
    if (panel->hProcess) {
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
    if (panel->temp_config)
        remove (panel->temp_config);
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
    free (panel->simulator_version);
    if (panel->Debug)
        fclose (panel->Debug);
    sim_cleanup_sock ();
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
                     size_t element_count)
{
REG *regs, *reg;
char *response = NULL;
size_t i;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
regs = (REG *)_panel_malloc ((1 + panel->reg_count)*sizeof(*regs)); 
if (regs == NULL) {
    panel->State = Error;
    return -1;
    }
pthread_mutex_lock (&panel->io_lock);
memcpy (regs, panel->regs, panel->reg_count*sizeof(*regs));
reg = &regs[panel->reg_count];
memset (reg, 0, sizeof(*regs));
reg->name = (char *)_panel_malloc (1 + strlen (name));
if (reg->name == NULL) {
    panel->State = Error;
    free (regs);
    return -1;
    }
strcpy (reg->name, name);
reg->indirect = indirect;
for (i=0; i<strlen (reg->name); i++) {
    if (islower (reg->name[i]))
        reg->name[i] = toupper (reg->name[i]);
    }
if (device_name) {
    reg->device_name = (char *)_panel_malloc (1 + strlen (device_name));
    if (reg->device_name == NULL) {
        free (reg->name);
        panel->State = Error;
        free (regs);
        return -1;
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
        return -1;
        }
    sprintf (t1, "%s %s", regs[i].device_name ? regs[i].device_name : "", regs[i].name);
    sprintf (t2, "%s %s", reg->device_name ? reg->device_name : "", reg->name);
    if ((!strcmp (t1, t2)) && (reg->indirect == regs[i].indirect)) {
        sim_panel_set_error ("Duplicate Register Declaration");
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
reg->addr = addr;
reg->size = size;
reg->element_count = element_count;
pthread_mutex_unlock (&panel->io_lock);
/* Validate existence of requested register/array */
if (_panel_sendf (panel, 1, &response, "EXAMINE %s %s%s\r", device_name? device_name : "", name, (element_count > 0) ? "[0]" : "")) {
    free (reg->name);
    free (reg->device_name);
    free (regs);
    return -1;
    }
if (!strcmp ("Invalid argument\r\n", response)) {
    sim_panel_set_error ("Invalid Register: %s %s", device_name? device_name : "", name);
    free (response);
    free (reg->name);
    free (reg->device_name);
    free (regs);
    return -1;
    }
free (response);
if (element_count > 0) {
    if (_panel_sendf (panel, 1, &response, "EXAMINE %s %s[%d]\r", device_name? device_name : "", name, element_count-1)) {
        free (reg->name);
        free (reg->device_name);
        free (regs);
        return -1;
        }
    if (!strcmp ("Subscript out of range\r\n", response)) {
        sim_panel_set_error ("Invalid Register Array Dimension: %s %s[%d]", device_name? device_name : "", name, element_count-1);
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
pthread_mutex_unlock (&panel->io_lock);
/* Now build the register query string for the whole register list */
if (_panel_register_query_string (panel, &panel->reg_query, &panel->reg_query_size))
    return -1;
return 0;
}

int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        const char *device_name,
                        size_t size,
                        void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 0, 0);
}

int
sim_panel_add_register_array (PANEL *panel,
                              const char *name,
                              const char *device_name,
                              size_t element_count,
                              size_t size,
                              void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 0, element_count);
}


int
sim_panel_add_register_indirect (PANEL *panel,
                                 const char *name,
                                 const char *device_name,
                                 size_t size,
                                 void *addr)
{
return _panel_add_register (panel, name, device_name, size, addr, 1, 0);
}

int
sim_panel_get_registers (PANEL *panel, unsigned long long *simulation_time)
{
if ((!panel) || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->callback) {
    sim_panel_set_error ("Callback provides register data");
    return -1;
    }
if (!panel->reg_count) {
    sim_panel_set_error ("No registers specified");
    return -1;
    }
pthread_mutex_lock (&panel->io_lock);
if (panel->reg_query_size != _panel_send (panel, panel->reg_query, panel->reg_query_size)) {
    pthread_mutex_unlock (&panel->io_lock);
    return -1;
    }
++panel->io_reg_query_pending;
panel->io_waiting = 1;
while (panel->io_waiting)
    pthread_cond_wait (&panel->io_done, &panel->io_lock);
if (simulation_time)
    *simulation_time = panel->simulation_time;
pthread_mutex_unlock (&panel->io_lock);
return 0;
}

int
sim_panel_set_display_callback (PANEL *panel, 
                                PANEL_DISPLAY_PCALLBACK callback, 
                                void *context, 
                                int callbacks_per_second)
{
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
pthread_mutex_lock (&panel->io_lock);
panel->callback = callback;
panel->callback_context = context;
if (callbacks_per_second && (0 == panel->callbacks_per_second)) { /* Need to start callbacks */
    pthread_attr_t attr;

    panel->callbacks_per_second = callbacks_per_second;
    pthread_cond_init (&panel->startup_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_create (&panel->callback_thread, &attr, _panel_callback, (void *)panel);
    pthread_attr_destroy(&attr);
    while (!panel->callback_thread_running)
        pthread_cond_wait (&panel->startup_cond, &panel->io_lock); /* Wait for thread to stabilize */
    pthread_cond_destroy (&panel->startup_cond);
    }
if ((callbacks_per_second == 0) && panel->callbacks_per_second) { /* Need to stop callbacks */
    panel->callbacks_per_second = 0;
    pthread_mutex_unlock (&panel->io_lock);
    pthread_join (panel->callback_thread, NULL);
    pthread_mutex_lock (&panel->io_lock);
    }
pthread_mutex_unlock (&panel->io_lock);
return 0;
}

int
sim_panel_exec_halt (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't HALT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    if (1 != _panel_send (panel, "\005", 1))
        return -1;
    }
return 0;
}

int
sim_panel_exec_boot (PANEL *panel, const char *device)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't BOOT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 0, NULL, "BOOT %s\r", device))
    return -1;
panel->State = Run;
return 0;
}

int
sim_panel_exec_run (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't CONT simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 0, NULL, "CONT\r", 5))
    return -1;
panel->State = Run;
return 0;
}

int
sim_panel_exec_step (PANEL *panel)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't STEP simulator from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
    
if (5 != _panel_send (panel, "STEP\r", 5))
    return -1;
panel->State = Run;
return 0;
}

int
sim_panel_break_set (PANEL *panel, const char *condition)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't establish a breakpoint from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
    
if (_panel_sendf (panel, 1, NULL, "BREAK %s\r", condition))
    return -1;
return 0;
}

int
sim_panel_break_clear (PANEL *panel, const char *condition)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't clear a breakpoint from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
    
if (_panel_sendf (panel, 1, NULL, "NOBREAK %s\r", condition))
    return -1;
return 0;
}

int
sim_panel_break_output_set (PANEL *panel, const char *condition)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't establish an output breakpoint from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
    
if (_panel_sendf (panel, 1, NULL, "EXPECT %s\r", condition))
    return -1;
return 0;
}

int
sim_panel_break_output_clear (PANEL *panel, const char *condition)
{
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->parent) {
    sim_panel_set_error ("Can't clear an output breakpoint from device front panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
    
if (_panel_sendf (panel, 1, NULL, "NOEXPECT %s\r", condition))
    return -1;
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

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 1, &response, "EXAMINE -H %s", name_or_addr)) {
    free (response);
    return -1;
    }
c = strchr (response, ':');
if (!c) {
    sim_panel_set_error (response);
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

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (little_endian)
    memcpy (&data, value, size);
else
    memcpy (((char *)&data) + sizeof(data)-size, value, size);
if (_panel_sendf (panel, 1, NULL, "DEPOSIT -H %s %llx", name_or_addr, data))
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

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (little_endian)
    memcpy (&address, addr, addr_size);
else
    memcpy (((char *)&address) + sizeof(address)-addr_size, addr, addr_size);
if (_panel_sendf (panel, 1, &response, (panel->radix == 16) ? "EXAMINE -H %llx" : "EXAMINE -H %llo", address)) {
    free (response);
    return -1;
    }
c = strchr (response, ':');
if (!c) {
    sim_panel_set_error (response);
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

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
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
if (_panel_sendf (panel, 1, NULL, (panel->radix == 16) ? "DEPOSIT -H %llx %llx" : "DEPOSIT -H %llo %llx", address, data))
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
if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 1, NULL, "DEPOSIT %s %s", name, value))
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
char *response = NULL, *status = NULL;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (_panel_sendf (panel, 1, &response, "ATTACH %s %s %s", switches, device, path)) {
    free (response);
    return -1;
    }
if (_panel_sendf (panel, 1, &status, "ECHO %%STATUS%%")) {
    free (response);
    free (status);
    return -1;
    }
if (!status || (strcmp (status, "00000000\r\n"))) {
    sim_panel_set_error (response);
    free (response);
    free (status);
    return -1;
    }
free (response);
free (status);
return 0;
}

/**
   sim_panel_dismount

        device      the name of a simulator device/unit

 */
int
sim_panel_dismount (PANEL *panel,
                    const char *device)
{
char *response = NULL, *status = NULL;

if (!panel || (panel->State == Error)) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (_panel_sendf (panel, 1, &response, "DETACH %s", device)) {
    free (response);
    return -1;
    }
if (_panel_sendf (panel, 1, &status, "ECHO %%STATUS%%")) {
    free (response);
    free (status);
    return -1;
    }
if (!status || (strcmp (status, "00000000\r\n"))) {
    sim_panel_set_error (response);
    free (response);
    free (status);
    return -1;
    }
free (response);
free (status);
return 0;
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

/* 
   Boost Priority for this response processing thread to quickly digest 
   arriving data.
 */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

buf[buf_data] = '\0';
pthread_mutex_lock (&p->io_lock);
if (!p->parent) {
    while (1) {
        int new_data = sim_read_sock (p->sock, &buf[buf_data], sizeof(buf)-(buf_data+1));

        if (new_data <= 0) {
            sim_panel_set_error ("%s after reading %d bytes: %s", sim_get_err_sock("Unexpected socket read"), buf_data, buf);
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
pthread_cond_signal (&p->startup_cond);   /* Signal we're ready to go */
msleep (100);
pthread_mutex_lock (&p->io_lock);
while ((p->sock != INVALID_SOCKET) &&
       (p->State != Error)) {
    int new_data;
    char *s, *e, *eol;

    pthread_mutex_unlock (&p->io_lock);
    new_data = sim_read_sock (p->sock, &buf[buf_data], sizeof(buf)-(buf_data+1));
    if (new_data <= 0) {
        pthread_mutex_lock (&p->io_lock);
        sim_panel_set_error ("%s", sim_get_err_sock("Unexpected socket read"));
        _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
        p->State = Error;
        break;
        }
    _panel_debug (p, DBG_RCV, "Received %d bytes: ", &buf[buf_data], new_data, new_data);
    buf_data += new_data;
    buf[buf_data] = '\0';
    s = buf;
    while ((eol = strchr (s, '\n'))) {
        /* Line to process */
        *eol++ = '\0';
        while ((*s) && (s[strlen(s)-1] == '\r'))
            s[strlen(s)-1] = '\0';
        e = strchr (s, ':');
        if (e) {
            size_t i;

            *e++ = '\0';
            if (!strcmp("Time", s)) {
                p->simulation_time = strtoull (e, NULL, 10);
                s = eol;
                while (isspace(0xFF & (*s)))
                    ++s;
                continue;
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
                    continue;
                }
            if (r) {
                if (strcmp (s, r->name)) {
                    unsigned long long data;

                    data = strtoull (e, NULL, 16);
                    if (little_endian)
                        memcpy (p->regs[i].addr, &data, p->regs[i].size);
                    else
                        memcpy (p->regs[i].addr, ((char *)&data) + sizeof(data)-p->regs[i].size, p->regs[i].size);
                    r = NULL;
                    }
                s = eol;
                while (isspace(0xFF & (*s)))
                    ++s;
                continue;
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

                    if ((0 == memcmp (p->regs[i].name, s, name_len), s) &&
                        (s[name_len] == '[')) {
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
        if (!strcmp (s + strlen (sim_prompt), register_get_echo)) {
            pthread_mutex_lock (&p->io_lock);
            --p->io_reg_query_pending;
            if (p->callback) {
                pthread_mutex_unlock (&p->io_lock);
                p->callback (p, p->simulation_time, p->callback_context);
                }
            else {
                p->io_waiting = 0;
                pthread_cond_signal (&p->io_done);
                pthread_mutex_unlock (&p->io_lock);
                }
            }
        else {
            pthread_mutex_lock (&p->io_lock);
            if (!strcmp (s + strlen (sim_prompt), command_done_echo)) {
                p->io_waiting = 0;
                pthread_cond_signal (&p->io_done);
                }
            else {
                /* Non Register Data Found (echo of EXAMINE or other commands and/or command output) */
                if (p->io_waiting) {
                    char *t;

                    if (p->io_response_data + strlen (s) + 3 > p->io_response_size) {
                        t = (char *)_panel_malloc (p->io_response_data + strlen (s) + 3);
                        if (t == NULL) {
                            _panel_debug (p, DBG_RCV, "%s", NULL, 0, sim_panel_get_error());
                            p->State = Error;
                            pthread_mutex_unlock (&p->io_lock);
                            break;
                            }
                        memcpy (t, p->io_response, p->io_response_data);
                        free (p->io_response);
                        p->io_response = t;
                        p->io_response_size = p->io_response_data + strlen (s) + 3;
                        }
                    strcpy (p->io_response + p->io_response_data, s);
                    p->io_response_data += strlen(s);
                    strcpy (p->io_response + p->io_response_data, "\r\n");
                    p->io_response_data += 2;
                    }
                }
            pthread_mutex_unlock (&p->io_lock);
            }
        s = eol;
        while (isspace(0xFF & (*s)))
            ++s;
        }
    pthread_mutex_lock (&p->io_lock);
    if ((p->State == Run) && (!strcmp (s, sim_prompt))) {
        p->State = Halt;
        }
    memmove (buf, s, strlen (s)+1);
    buf_data = strlen (buf);
    if (!strcmp("Simulator Running...", buf)) {
        p->State = Run;
        buf_data = 0;
        buf[0] = '\0';
        }
    }
if (p->io_waiting) {
    _panel_debug (p, DBG_RCV, "Receive: restarting waiting thread while exiting", NULL, 0);
    p->io_waiting = 0;
    pthread_cond_signal (&p->io_done);
    }
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

/* 
   Boost Priority for timer thread so it doesn't compete 
   with compute bound activities.
 */
pthread_getschedparam (pthread_self(), &sched_policy, &sched_priority);
++sched_priority.sched_priority;
pthread_setschedparam (pthread_self(), sched_policy, &sched_priority);

pthread_mutex_lock (&p->io_lock);
p->callback_thread_running = 1;
pthread_mutex_unlock (&p->io_lock);
pthread_cond_signal (&p->startup_cond);   /* Signal we're ready to go */
msleep (100);
pthread_mutex_lock (&p->io_lock);
while ((p->sock != INVALID_SOCKET) && 
       (p->callbacks_per_second) &&
       (p->State != Error)) {
    int rate = p->callbacks_per_second;

    pthread_mutex_unlock (&p->io_lock);

    ++callback_count;
    if (1 == callback_count%rate) {     /* once a second update the query string */
        _panel_register_query_string (p, &buf, &buf_data);
        }
    msleep (1000/rate);
    pthread_mutex_lock (&p->io_lock);
    if (((p->State == Run) || ((p->State == Halt) && (0 == callback_count%(5*rate)))) &&
        (p->io_reg_query_pending == 0)) {
        ++p->io_reg_query_pending;
        pthread_mutex_unlock (&p->io_lock);
        if (buf_data != _panel_send (p, buf, buf_data)) {
            pthread_mutex_lock (&p->io_lock);
            break;
            }
        pthread_mutex_lock (&p->io_lock);
        }
    else
        _panel_debug (p, DBG_XMT, "Waiting for prior register query completion", NULL, 0);
    }
p->callback_thread_running = 0;
pthread_mutex_unlock (&p->io_lock);
free (buf);
return NULL;
}

static char *sim_panel_error_buf = NULL;
static size_t sim_panel_error_bufsize = 0;

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

static void sim_panel_set_error (const char *fmt, ...)
{
va_list arglist;
int len;

if (sim_panel_error_bufsize == 0) {
    sim_panel_error_bufsize = 2048;
    sim_panel_error_buf = (char *) malloc (sim_panel_error_bufsize);
    if (sim_panel_error_buf == NULL) {
        sim_panel_error_buf = (char *)"sim_panel_set_error(): Out of Memory\n";
        sim_panel_error_bufsize = 0;
        return;
        }
    }
sim_panel_error_buf[sim_panel_error_bufsize-1] = '\0';

while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
    len = vsnprintf (sim_panel_error_buf, sim_panel_error_bufsize-1, fmt, arglist);
    va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || (len >= (int)(sim_panel_error_bufsize-1))) {
        free (sim_panel_error_buf);
        sim_panel_error_bufsize = sim_panel_error_bufsize * 2;
        while ((int)sim_panel_error_bufsize < len + 1)
            sim_panel_error_bufsize = sim_panel_error_bufsize * 2;
        sim_panel_error_buf = (char *) malloc (sim_panel_error_bufsize);
        if (sim_panel_error_buf == NULL) {
            sim_panel_error_buf = (char *)"sim_panel_set_error(): Out of Memory\n";
            sim_panel_error_bufsize = 0;
            return;
            }
        sim_panel_error_buf[sim_panel_error_bufsize-1] = '\0';
        continue;
        }
    break;
    }

return;
}

static int
_panel_sendf (PANEL *p, int wait_for_completion, char **response, const char *fmt, ...)
{
char stackbuf[1024];
int bufsize = sizeof(stackbuf);
char *buf = stackbuf;
int len;
int post_fix_len = wait_for_completion ? 5 + strlen (command_done_echo): 1;
va_list arglist;
int ret;

while (1) {                                         /* format passed string, args */
    va_start (arglist, fmt);
    len = vsnprintf (buf, bufsize-1, fmt, arglist);
    va_end (arglist);

/* If the formatted result didn't fit into the buffer, then grow the buffer and try again */

    if ((len < 0) || ((len + post_fix_len) >= bufsize-1)) {
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
    strcat (buf, "\r");                 /* Make sure command line is terminated */
    ++len;
    }

if (wait_for_completion) {
    strcat (buf, command_done_echo);
    strcat (buf, "\r");
    pthread_mutex_lock (&p->io_lock);
    p->io_response_data = 0;
    }

ret = (strlen (buf) == _panel_send (p, buf, strlen (buf))) ? 0 : -1;

if (wait_for_completion) {
    if (!ret) {                                     /* Sent OK? */
        p->io_waiting = 1;
        while (p->io_waiting)
            pthread_cond_wait (&p->io_done, &p->io_lock); /* Wait for completion */
        if (response) {
            *response = (char *)_panel_malloc (p->io_response_data + 1);
            if (0 == memcmp (buf, p->io_response + strlen (sim_prompt), len)) {
                memcpy (*response, p->io_response + strlen (sim_prompt) + len + 1, p->io_response_data + 1 - (strlen (sim_prompt) + len + 1));
                }
            else
                memcpy (*response, p->io_response, p->io_response_data + 1);
            }
        p->io_response_data = 0;
        p->io_response[0] = '\0';
        }
    pthread_mutex_unlock (&p->io_lock);
    }

if (buf != stackbuf)
    free (buf);
return ret;
}


#ifdef  __cplusplus
}
#endif
