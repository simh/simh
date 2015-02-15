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

#if defined(_WIN32)
#include <process.h>
#define sleep(n) Sleep(n*1000)
#define msleep(n) Sleep(n)
#define strtoull _strtoui64
#else
#include <unistd.h>
#define msleep(n) usleep(1000*n)
#include <sys/wait.h>
#endif

#include "sim_sock.h"

typedef struct {
    char *name;
    void *addr;
    size_t size;
    } REG;

struct PANEL {
    PANEL                   *parent;        /* Device Panels can have parent panels */
    char                    *name;          /* simulator path or device name */
    char                    *config;
    char                    *temp_config;
    char                    hostport[64];
    size_t                  device_count;
    PANEL                   **devices;
    SOCKET                  sock;
    int                     reg_count;
    REG                     *regs;
    char                    *reg_query;
    size_t                  reg_query_size;
    OperationalState        State;
    pthread_mutex_t         lock;
    pthread_t               io_thread;
    int                     io_thread_running;
    pthread_mutex_t         io_lock;
    pthread_mutex_t         io_send_lock;
    int                     io_waiting;
    pthread_cond_t          io_done;
    pthread_cond_t          startup_cond;
    PANEL_DISPLAY_PCALLBACK callback;
    pthread_t               callback_thread;
    int                     callback_thread_running;
    void                    *callback_context;
    int                     callbacks_per_second;
#if defined(_WIN32)
    HANDLE                  hProcess;
#else
    pid_t                   pidProcess;
#endif
    };

static const char *sim_prompt = "sim> ";
static const char *register_get_echo = "# REGISTERS-DONE";
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

static int
_panel_send (PANEL *p, const char *msg, int len)
{
int sent = 0;

if (p->sock == INVALID_SOCKET) {
    sim_panel_set_error ("Invalid Socket for write");
    return -1;
    }
pthread_mutex_lock (&p->io_send_lock);
while (len) {
    int bsent = sim_write_sock (p->sock, msg, len);
    if (bsent < 0) {
        sim_panel_set_error ("%s", sim_get_err_sock("Error writing to socket"));
        pthread_mutex_unlock (&p->io_send_lock);
        return bsent;
        }
    len -= bsent;
    msg += bsent;
    sent += bsent;
    }
pthread_mutex_unlock (&p->io_send_lock);
return sent;
}

static int
_panel_sendf (PANEL *p, int wait_for_completion, const char *fmt, ...);

static int
_panel_register_query_string (PANEL *panel, char **buf, size_t *buf_size)
{
int i;
size_t buf_data, buf_needed = 0;

pthread_mutex_lock (&panel->io_lock);
for (i=0; i<panel->reg_count; i++)
    buf_needed += 7 + strlen (panel->regs[i].name);
buf_needed += 10 + strlen (register_get_echo); /* # REGISTERS-DONE */
if (buf_needed > *buf_size) {
    free (*buf);
    *buf = (char *)_panel_malloc (buf_needed);
    if (!*buf) {
        pthread_mutex_unlock (&panel->io_lock);
        return -1;
        }
    *buf_size = buf_needed;
    }
buf_data = 0;
#if SEPARATE_REGISTERS
for (i=0; i<panel->reg_count; i++) {
    sprintf (*buf + buf_data, "E -H %s\r", panel->regs[i].name);
    buf_data += strlen (*buf + buf_data);
    }
#else
sprintf (*buf + buf_data, "E -H ");
buf_data += strlen (*buf + buf_data);
for (i=0; i<panel->reg_count; i++) {
    sprintf (*buf + buf_data, "%s%s", (i>0) ? "," : "", panel->regs[i].name);
    buf_data += strlen (*buf + buf_data);
    }
strcpy (*buf + buf_data, "\r");
buf_data += strlen (*buf + buf_data);
#endif
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


PANEL *
sim_panel_start_simulator (const char *sim_path,
                           const char *sim_config,
                           size_t device_panel_count)
{
PANEL *p = NULL;
FILE *f = NULL;
struct stat statb;
char *buf = NULL;
int port, i;
char hostport[64];
union {int i; char c[sizeof (int)]; } end_test;

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
p->name = (char *)_panel_malloc (strlen (sim_path) + 1);
if (p->name == NULL)
    goto Error_Return;
strcpy (p->name, sim_path);
p->config = (char *)_panel_malloc (strlen (sim_config) + 1);
if (p->config == NULL)
    goto Error_Return;
strcpy (p->config, sim_config);
f = fopen (sim_config, "rb");
if (f == NULL) {
    sim_panel_set_error ("Can't open configuration file '%s': %s", sim_config, strerror(errno));
    goto Error_Return;
    }
if (statb.st_size != fread (buf, 1, statb.st_size, f)) {
    sim_panel_set_error ("Can't read complete configuration file '%s': %s", sim_config, strerror(errno));
    goto Error_Return;
    }
fclose (f);
f = NULL;
p->temp_config = (char *)_panel_malloc (strlen (sim_config) + 40);
if (p->temp_config == NULL)
    goto Error_Return;
sprintf (p->temp_config, "%s-Panel-%d", sim_config, getpid());
f = fopen (p->temp_config, "w");
if (f == NULL) {
    sim_panel_set_error ("Can't create temporary configuration file '%s': %s", p->temp_config, strerror(errno));
    goto Error_Return;
    }
fprintf (f, "# Temporary FrontPanel generated simh configuration file\n");
fprintf (f, "# Original Configuration File: %s\n", p->config);
fprintf (f, "# Simulator Path: %s\n", sim_path);
fprintf (f, "%s\n", buf);
free (buf);
buf = NULL;
fprintf (f, "set remote -u telnet=%s\n", hostport);
if (device_panel_count)
    fprintf (f, "set remote connections=%d\n", (int)device_panel_count+1);
fprintf (f, "set remote master\n");
fclose (f);
f = NULL;
if (1) {
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
        close (0); close (1); close (2); /* make sure not to pass the open standard handles */
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
    }
for (i=0; i<5; i++) {
    p->sock = sim_connect_sock_ex (NULL, hostport, NULL, NULL, SIM_SOCK_OPT_NODELAY | SIM_SOCK_OPT_BLOCKING);
    if (p->sock == INVALID_SOCKET)
        msleep (100);
    else
        break;
    }
if (p->sock == INVALID_SOCKET) {
    sim_panel_set_error ("Can't connect to simulator Remote Console on port %s", hostport);
    goto Error_Return;
    }
strcpy (p->hostport, hostport);
if (1) {
    pthread_attr_t attr;

    pthread_mutex_init (&p->io_lock, NULL);
    pthread_mutex_init (&p->io_send_lock, NULL);
    pthread_cond_init (&p->io_done, NULL);
    pthread_cond_init (&p->startup_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_mutex_lock (&p->io_lock);
    pthread_create (&p->io_thread, &attr, _panel_reader, (void *)p);
    pthread_attr_destroy(&attr);
    while (!p->io_thread_running)
        pthread_cond_wait (&p->startup_cond, &p->io_lock); /* Wait for thread to stabilize */
    pthread_mutex_unlock (&p->io_lock);
    pthread_cond_destroy (&p->startup_cond);
    }
if (sizeof(mantra) != _panel_send (p, (char *)mantra, sizeof(mantra))) {
    sim_panel_set_error ("Error sending Telnet mantra (options): %s", sim_get_err_sock ("send"));
    goto Error_Return;
    }
if (device_panel_count) {
    p->devices = (PANEL **)_panel_malloc (device_panel_count*sizeof(*p->devices));
    if (p->devices == NULL)
        goto Error_Return;
    memset (p->devices, 0, device_panel_count*sizeof(*p->devices));
    p->device_count = device_panel_count;
    }
_panel_register_panel (p);
return p;

Error_Return:
if (f)
    fclose (f);
if (buf)
    free (buf);
sim_panel_destroy (p);
return NULL;
}

PANEL *
sim_panel_add_device_panel (PANEL *simulator_panel,
                            const char *device_name)
{
size_t i, device_num;
PANEL *p = NULL;

if (!simulator_panel) {
    sim_panel_set_error ("Invalid Panel");
    return NULL;
    }
for (device_num=0; device_num < simulator_panel->device_count; ++device_num)
    if (simulator_panel->devices[device_num] == NULL)
        break;
if (device_num == simulator_panel->device_count) {
    sim_panel_set_error ("No free panel devices slots available %s simulator.  All %d slots are used.", simulator_panel->name, (int)simulator_panel->device_count);
    return NULL;
    }
p = (PANEL *)_panel_malloc (sizeof(*p));
if (p == NULL)
    goto Error_Return;
memset (p, 0, sizeof(*p));
p->parent = simulator_panel;
p->sock = INVALID_SOCKET;
for (i=0; i<5; i++) {
    p->sock = sim_connect_sock_ex (NULL, simulator_panel->hostport, NULL, NULL, SIM_SOCK_OPT_NODELAY | SIM_SOCK_OPT_BLOCKING);
    if (p->sock == INVALID_SOCKET)
        msleep (100);
    else
        break;
    }
if (p->sock == INVALID_SOCKET) {
    sim_panel_set_error ("Can't connect to simulator Remote Console on port %s", simulator_panel->hostport);
    goto Error_Return;
    }
strcpy (p->hostport, simulator_panel->hostport);
if (1) {
    pthread_attr_t attr;

    pthread_mutex_init (&p->io_lock, NULL);
    pthread_mutex_init (&p->io_send_lock, NULL);
    pthread_cond_init (&p->io_done, NULL);
    pthread_cond_init (&p->startup_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_mutex_lock (&p->io_lock);
    pthread_create (&p->io_thread, &attr, _panel_reader, (void *)p);
    pthread_attr_destroy(&attr);
    while (!p->io_thread_running)
        pthread_cond_wait (&p->startup_cond, &p->io_lock); /* Wait for thread to stabilize */
    pthread_mutex_unlock (&p->io_lock);
    pthread_cond_destroy (&p->startup_cond);
    }
if (sizeof(mantra) != _panel_send (p, (char *)mantra, sizeof(mantra))) {
    sim_panel_set_error ("Error sending Telnet mantra (options): %s", sim_get_err_sock ("send"));
    goto Error_Return;
    }
simulator_panel->devices[device_num] = p;
_panel_register_panel (p);
return p;

Error_Return:
sim_panel_destroy (p);
return NULL;
}

int
sim_panel_destroy (PANEL *panel)
{
REG *reg;

if (panel) {
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
    free (panel->name);
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
        reg++;
        }
    free (panel->regs);
    free (panel->reg_query);
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

int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        size_t size,
                        void *addr)
{
REG *regs, *reg;

if (!panel) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
regs = (REG *)_panel_malloc ((1 + panel->reg_count)*sizeof(*regs));
if (regs == NULL)
    return -1;
pthread_mutex_lock (&panel->io_lock);
memcpy (regs, panel->regs, panel->reg_count*sizeof(*regs));
reg = &regs[panel->reg_count];
memset (reg, 0, sizeof(*regs));
reg->name = (char *)_panel_malloc (1 + strlen (name));
if (reg->name == NULL) {
    free (regs);
    return -1;
    }
strcpy (reg->name, name);
reg->addr = addr;
reg->size = size;
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
sim_panel_get_registers (PANEL *panel)
{
if (!panel) {
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
panel->io_waiting = 1;
while (panel->io_waiting)
    pthread_cond_wait (&panel->io_done, &panel->io_lock);
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
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
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
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 0, "BOOT %s\r", device))
    return -1;
panel->State = Run;
return 0;
}

int
sim_panel_exec_run (PANEL *panel)
{
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->State == Run) {
    sim_panel_set_error ("Not Halted");
    return -1;
    }
if (_panel_sendf (panel, 0, "CONT\r", 5))
    return -1;
panel->State = Run;
return 0;
}

int
sim_panel_exec_step (PANEL *panel)
{
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
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
sim_panel_set_register_value (PANEL *panel,
                              const char *name,
                              const char *value)
{
if (!panel) {
    sim_panel_set_error ("Invalid Panel");
    return -1;
    }
if (panel->callback) {
    sim_panel_set_error ("Callback provides register data");
    return -1;
    }
if (_panel_sendf (panel, 1, "D %s %s", name, value))
    return -1;
return 0;
}

static void *
_panel_reader(void *arg)
{
PANEL *p = (PANEL*)arg;
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

pthread_mutex_lock (&p->io_lock);
p->io_thread_running = 1;
pthread_cond_signal (&p->startup_cond);   /* Signal we're ready to go */
while (p->sock != INVALID_SOCKET) {
    int new_data;
    char *s, *e, *eol;

    pthread_mutex_unlock (&p->io_lock);
    new_data = sim_read_sock (p->sock, &buf[buf_data], sizeof(buf)-(buf_data+1));
    if (new_data <= 0)
        break;
    buf_data += new_data;
    buf[buf_data] = '\0';
    s = buf;
    while ((eol = strchr (s, '\r'))) {
        /* Line to process */
        *eol++ = '\0';
        e = strchr (s, ':');
        if (e) {
            int i;

            *e++ = '\0';
            for (i=0; i<p->reg_count; i++) {
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
            if (i == p->reg_count) {
                /* Unexpected Register Data Found */
                }
            }
        else {
            if (!strcmp (s + strlen (sim_prompt), register_get_echo)) {
                if (p->callback) {
                    p->callback (p, p->callback_context);
                    }
                else {
                    pthread_mutex_lock (&p->io_lock);
                    p->io_waiting = 0;
                    pthread_cond_signal (&p->io_done);
                    pthread_mutex_unlock (&p->io_lock);
                    }
                }
            else {
                if (!strcmp (s + strlen (sim_prompt), command_done_echo)) {
                    pthread_mutex_lock (&p->io_lock);
                    p->io_waiting = 0;
                    pthread_cond_signal (&p->io_done);
                    pthread_mutex_unlock (&p->io_lock);
                    }
                else {
                    /* Non Register Data Found (echo of EXAMINE or other commands) */
                    }
                }
            }
        s = eol;
        while (isspace(*s))
            ++s;
        }
    pthread_mutex_lock (&p->io_lock);
    if ((p->State == Run) && (!strcmp (s, sim_prompt))) {
        p->State = Halt;
        }
    memmove (buf, s, strlen (s)+1);
    buf_data = strlen (buf);
    if (!strcmp("Simulator Running", buf)) {
        p->State = Run;
        buf_data = 0;
        buf[0] = '\0';
        }
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
pthread_cond_signal (&p->startup_cond);   /* Signal we're ready to go */
while ((p->sock != INVALID_SOCKET) && 
       (p->callbacks_per_second)) {
    int rate = p->callbacks_per_second;
    pthread_mutex_unlock (&p->io_lock);

    ++callback_count;
    if (1 == callback_count%rate) {
        _panel_register_query_string (p, &buf, &buf_data);
        }
    msleep (1000/rate);
    if ((p->State == Run) || (0 == callback_count%(5*rate)))
        if (buf_data != _panel_send (p, buf, buf_data)) {
            break;
            }
    pthread_mutex_lock (&p->io_lock);
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
_panel_sendf (PANEL *p, int wait_for_completion, const char *fmt, ...)
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
        buf = (char *) _panel_malloc (bufsize);
        if (buf == NULL)
            return -1;
        buf[bufsize-1] = '\0';
        continue;
        }
    break;
    }

strcat (buf, "\r");                     /* Make sure command line is terminated */

if (wait_for_completion) {
    strcat (buf, command_done_echo);
    strcat (buf, "\r");
    pthread_mutex_lock (&p->io_lock);
    }

ret = (strlen (buf) == _panel_send (p, buf, strlen (buf))) ? 0 : -1;

if (wait_for_completion) {
    if (!ret) {                                     /* Sent OK? */
        p->io_waiting = 1;
        while (p->io_waiting)
            pthread_cond_wait (&p->io_done, &p->io_lock); /* Wait for completion */
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
