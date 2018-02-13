/* FrontPanelTest.c: simulator frontpanel API sample

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

   This module demonstrates the use of the interface between a front panel 
   application and a simh simulator.  Facilities provide ways to gather 
   information from and to observe and control the state of a simulator.

*/

/* This program provides a basic test of the simh_frontpanel API. */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "sim_frontpanel.h"
#include <signal.h>

#if defined(_WIN32)
#include <windows.h>
#include <winerror.h>
#define usleep(n) Sleep(n/1000)
#else
#include <unistd.h>
#if defined(HAVE_NCURSES)
#include <ncurses.h>
#define fgets(buf, n, f) (OK == getnstr(buf, n))
#define printf my_printf
static void my_printf (const char *fmt, ...)
{
va_list arglist;
int len;
static char *buf = NULL;
static int buf_size = 0;
char *c;

while (1) {
    va_start (arglist, fmt);
    len = vsnprintf (buf, buf_size, fmt, arglist);
    va_end (arglist);
    if (len < 0)
        return;
    if (len < buf_size)
        break;
    buf = realloc (buf, len + 2);
    buf_size = len + 1;
    buf[buf_size] = '\0';
    }
while ((c = strstr (buf, "\r\n")))
    memmove (c, c + 1, strlen (c));
printw ("%s", buf);
}
#endif /* HAVE_NCURSES */
#endif
const char *sim_path = 
#if defined(_WIN32)
            "vax.exe";
#else
            "vax";
#endif

const char *sim_config = 
            "VAX-PANEL.ini";

/* Registers visible on the Front Panel */
static unsigned int PC, SP, FP, AP, PSL, R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, atPC;
static unsigned int PCQ[32];

int PSL_bits[32];
int PC_bits[32];
int PC_indirect_bits[32];
int PCQ_3_bits[32];
unsigned long long simulation_time;

int update_display = 1;

int debug = 0;


static void
DisplayCallback (PANEL *panel, unsigned long long sim_time, void *context)
{
simulation_time = sim_time;
update_display = 1;
}

static void
DisplayRegisters (PANEL *panel, int get_pos, int set_pos)
{
char buf1[100], buf2[100], buf3[100], buf4[100];
static const char *states[] = {"Halt", "Run "};

buf1[sizeof(buf1)-1] = buf2[sizeof(buf2)-1] = buf3[sizeof(buf3)-1] = buf4[sizeof(buf4)-1] = 0;
sprintf (buf1, "%4s PC: %08X   SP: %08X   AP: %08X   FP: %08X  @PC: %08X\n", states[sim_panel_get_state (panel)], PC, SP, AP, FP, atPC);
sprintf (buf2, "PSL: %08X                               Instructions Executed: %lld\n", PSL, simulation_time);
sprintf (buf3, "R0:%08X  R1:%08X  R2:%08X  R3:%08X   R4:%08X   R5:%08X\n", R0, R1, R2, R3, R4, R5);
sprintf (buf4, "R6:%08X  R7:%08X  R8:%08X  R9:%08X  R10:%08X  R11:%08X\n", R6, R7, R8, R9, R10, R11);
#if defined(_WIN32)
if (1) {
    static HANDLE out = NULL;
    static CONSOLE_SCREEN_BUFFER_INFO info;
    static COORD origin = {0, 0};
    int written;

    if (out == NULL)
        out = GetStdHandle (STD_OUTPUT_HANDLE);
    if (get_pos)
        GetConsoleScreenBufferInfo (out, &info);
    SetConsoleCursorPosition (out, origin);
    WriteConsoleA(out, buf1, strlen(buf1), &written, NULL);
    WriteConsoleA(out, buf2, strlen(buf2), &written, NULL);
    WriteConsoleA(out, buf3, strlen(buf3), &written, NULL);
    WriteConsoleA(out, buf4, strlen(buf4), &written, NULL);
    if (set_pos)
        SetConsoleCursorPosition (out, info.dwCursorPosition);
    }
#else
if (1) {
#if defined(HAVE_NCURSES)
    static int row, col;

    if (get_pos)
        getyx (stdscr, row, col);
    wmove (stdscr, 0, 0);
#else
#define ESC "\033"
#define CSI ESC "["
    if (get_pos)
        printf (CSI "s");   /* Save Cursor Position */
    printf (CSI "H");   /* Position to Top of Screen (1,1) */
#endif /* HAVE_NCURSES */
    printf ("%s", buf1);
    printf ("%s", buf2);
    printf ("%s", buf3);
    printf ("%s", buf4);
#if defined(HAVE_NCURSES)
    if (set_pos)
        wmove (stdscr, row, col);   /* Restore Cursor Position */
    wrefresh (stdscr);
#else
    if (set_pos)
        printf (CSI "s");   /* Restore Cursor Position */
    printf ("\r\n");
#endif /* HAVE_NCURSES */
    }
#endif
}

static
void CleanupDisplay (void)
{
#if (!defined(_WIN32)) && defined(HAVE_NCURSES)
endwin ();
#endif
}

static
void InitDisplay (void)
{
#if defined(_WIN32)
system ("cls");
#else
#if defined(HAVE_NCURSES)
int max_height = 0, max_width = 0;

initscr ();
wclear (stdscr);
scrollok (stdscr, 1);
getmaxyx(stdscr, max_height, max_width);
setscrreg(5, max_height - 1);
#else /* HAVE_NCURSES */
printf (CSI "H");   /* Position to Top of Screen (1,1) */
printf (CSI "2J");  /* Clear Screen */
#endif
#endif
printf ("\n\n\n\n");
printf ("^C to Halt, Commands: BOOT, CONT, EXIT, BREAK, NOBREAK, EXAMINE, HISTORY\n");
#if (!defined(_WIN32)) && defined(HAVE_NCURSES)
wrefresh (stdscr);
#endif
atexit (CleanupDisplay);
}

volatile int halt_cpu = 0;
PANEL *panel, *tape;

void halt_handler (int sig)
{
signal (SIGINT, halt_handler);      /* Re-establish handler for some platforms that implement ONESHOT signal dispatch */
halt_cpu = 1;
sim_panel_flush_debug (panel);
return;
}

int panel_setup ()
{
FILE *f;

/* Create pseudo config file for a test */
if ((f = fopen (sim_config, "w"))) {
    if (debug) {
        fprintf (f, "set verbose\n");
        fprintf (f, "set debug -n -a -p simulator.dbg\n");
        fprintf (f, "set cpu simhalt\n");
        fprintf (f, "set remote telnet=2226\n");
        fprintf (f, "set rem-con debug=XMT;RCV;MODE;REPEAT;CMD\n");
        fprintf (f, "set remote notelnet\n");
        fprintf (f, "set cpu history=128\n");
        }
    fprintf (f, "set cpu autoboot\n");
    fprintf (f, "set cpu 64\n");
    fprintf (f, "set console telnet=buffered\n");
    fprintf (f, "set console -u telnet=1927\n");
    /* Start a terminal emulator for the console port */
#if defined(_WIN32)
    fprintf (f, "set env PATH=%%PATH%%;%%ProgramFiles%%\\PuTTY;%%ProgramFiles(x86)%%\\PuTTY\n");
    fprintf (f, "! start PuTTY telnet://localhost:1927\n");
#elif defined(__linux) || defined(__linux__)
    fprintf (f, "! nohup xterm -e 'telnet localhost 1927' &\n");
#elif defined(__APPLE__)
    fprintf (f, "! osascript -e 'tell application \"Terminal\" to do script \"telnet localhost 1927; exit\"'\n");
#endif
    fclose (f);
    }

signal (SIGINT, halt_handler);
panel = sim_panel_start_simulator_debug (sim_path,
                                         sim_config,
                                         2,
                                         debug? "frontpanel.dbg" : NULL);

if (!panel) {
    printf ("Error starting simulator %s with config %s: %s\n", sim_path, sim_config, sim_panel_get_error());
    goto Done;
    }

if (debug) {
    sim_panel_set_debug_mode (panel, DBG_XMT|DBG_RCV|DBG_REQ|DBG_RSP|DBG_THR|DBG_APP);
    }
sim_panel_debug (panel, "Starting Debug\n");
if (1) {
    tape = sim_panel_add_device_panel (panel, "TAPE DRIVE");

    if (!tape) {
        printf ("Error adding tape device to simulator: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (debug) {
        sim_panel_set_debug_mode (tape, DBG_XMT|DBG_RCV|DBG_REQ|DBG_RSP|DBG_THR|DBG_APP);
        }
    }
if (1) {
    unsigned int noop_noop_noop_halt = 0x00010101, addr400 = 0x00000400, pc_value;
    int mstime = 0;

    if (sim_panel_mem_deposit (panel, sizeof(addr400), &addr400, sizeof(noop_noop_noop_halt), &noop_noop_noop_halt)) {
        printf ("Error setting 00000000 to %08X: %s\n", noop_noop_noop_halt, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_gen_deposit (panel, "PC", sizeof(addr400), &addr400)) {
        printf ("Error setting PC to %08X: %s\n", addr400, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_exec_start (panel)) {
        printf ("Error starting simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    while ((sim_panel_get_state (panel) == Run) &&
           (mstime < 1000)) {
        usleep (100000);
        mstime += 100;
        }
    if (sim_panel_get_state (panel) != Halt) {
        printf ("Unexpected execution state not Halt: %d\n", sim_panel_get_state (panel));
        goto Done;
        }
    pc_value = 0;
    if (sim_panel_gen_examine (panel, "PC", sizeof(pc_value), &pc_value)) {
        printf ("Unexpected error getting PC value: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (pc_value != addr400 + 4) {
        printf ("Unexpected error getting PC value: %08X, expected: %08X\n", pc_value, addr400 + 4);
        goto Done;
        }
    }

if (sim_panel_add_register_array (panel, "PCQ",  NULL, sizeof(PCQ)/sizeof(PCQ[0]), sizeof(PCQ[0]), &PCQ)) {
    printf ("Error adding register array 'PCQ': %s\n", sim_panel_get_error());
    goto Done;
    }
if (!sim_panel_add_register (panel, "ZPC",  NULL, sizeof(PC), &PC)) {
    printf ("Unexpected success adding non-existent register 'ZPC'\n");
    goto Done;
    }
if (sim_panel_add_register (panel, "PC",  NULL, sizeof(PC), &PC)) {
    printf ("Error adding register 'PC': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register_indirect (panel, "PC",  NULL, sizeof(atPC), &atPC)) {
    printf ("Error adding register indirect 'PC': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "SP",  NULL, sizeof(SP), &SP)) {
    printf ("Error adding register 'SP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "FP",  "CPU", sizeof(FP), &FP)) {
    printf ("Error adding register 'FP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "AP",  NULL, sizeof(AP), &AP)) {
    printf ("Error adding register 'AP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R0",  NULL, sizeof(R0), &R0)) {
    printf ("Error adding register 'R0': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R1",  NULL, sizeof(R1), &R1)) {
    printf ("Error adding register 'R1': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R2",  NULL, sizeof(R2), &R2)) {
    printf ("Error adding register 'R2': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R3",  NULL, sizeof(R3), &R3)) {
    printf ("Error adding register 'R3': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R4",  NULL, sizeof(R4), &R4)) {
    printf ("Error adding register 'R4': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R5",  NULL, sizeof(R5), &R5)) {
    printf ("Error adding register 'R5': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R6",  NULL, sizeof(R6), &R6)) {
    printf ("Error adding register 'R6': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R7",  NULL, sizeof(R7), &R7)) {
    printf ("Error adding register 'R7': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R8",  NULL, sizeof(R8), &R8)) {
    printf ("Error adding register 'R8': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R9",  NULL, sizeof(R9), &R9)) {
    printf ("Error adding register 'R9': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R10",  NULL, sizeof(R10), &R10)) {
    printf ("Error adding register 'R10': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R11",  NULL, sizeof(R11), &R11)) {
    printf ("Error adding register 'R11': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "PSL",  NULL, sizeof(PSL), &PSL)) {
    printf ("Error adding register 'PSL': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_get_registers (panel, NULL)) {
    printf ("Error getting register data: %s\n", sim_panel_get_error());
    goto Done;
    }
if (1) {
    unsigned int deadbeef = 0xdeadbeef, beefdead = 0xbeefdead, addr200 = 0x00000200, beefdata;

    if (sim_panel_set_register_value (panel, "R0", "DEADBEEF")) {
        printf ("Error setting R0 to DEADBEEF: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_gen_deposit (panel, "R1", sizeof(deadbeef), &deadbeef)) {
        printf ("Error setting R1 to DEADBEEF: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_mem_deposit (panel, sizeof(addr200), &addr200, sizeof(deadbeef), &deadbeef)) {
        printf ("Error setting 00000200 to DEADBEEF: %s\n", sim_panel_get_error());
        goto Done;
        }
    beefdata = 0;
    if (sim_panel_gen_examine (panel, "200", sizeof(beefdata), &beefdata)) {
        printf ("Error getting contents of memory location 200: %s\n", sim_panel_get_error());
        goto Done;
        }
    beefdata = 0;
    if (sim_panel_mem_examine (panel, sizeof (addr200), &addr200, sizeof (beefdata), &beefdata)) {
        printf ("Error getting contents of memory location 200: %s\n", sim_panel_get_error());
        goto Done;
        }
    beefdata = 0;
    if (!sim_panel_gen_examine (panel, "20000000", sizeof(beefdata), &beefdata)) {
        printf ("Unexpected success getting contents of memory location 20000000: %s\n", sim_panel_get_error());
        goto Done;
        }
    }
if (sim_panel_get_registers (panel, NULL)) {
    printf ("Error getting register data: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_set_display_callback_interval (panel, &DisplayCallback, NULL, 200000)) {
    printf ("Error setting automatic display callback: %s\n", sim_panel_get_error());
    goto Done;
    }
sim_panel_clear_error ();
if (!sim_panel_dismount (panel, "RL0")) {
    printf ("Unexpected success while dismounting media file from non mounted RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_mount (panel, "RL0", "-NQ", "TEST-RL.DSK")) {
    printf ("Error while mounting media file TEST-RL.DSK on RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_dismount (panel, "RL0")) {
    printf ("Error while dismounting media file from RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
(void)remove ("TEST-RL.DSK");
if (sim_panel_break_set (panel, "400")) {
    printf ("Unexpected error establishing a breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_clear (panel, "400")) {
    printf ("Unexpected error clearing a breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_set (panel, "\"32..31..30\"")) {
    printf ("Unexpected error establishing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_clear (panel, "\"32..31..30\"")) {
    printf ("Unexpected error clearing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_set (panel, "-P \"Normal operation not possible.\" SHOW QUEUE")) {
    printf ("Unexpected error establishing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_set (panel, "-P \"Device? [XQA0]: \"")) {
    printf ("Unexpected error establishing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_set (panel, "-P \"(1..15): \" SEND \"4\\r\"; GO")) {
    printf ("Unexpected error establishing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (!sim_panel_set_sampling_parameters_ex (panel, 0, 0, 199)) {
    printf ("Unexpected success setting sampling parameters to 0, 0, 199\n");
    goto Done;
    }
if (!sim_panel_set_sampling_parameters_ex (panel, 199, 0, 0)) {
    printf ("Unexpected success setting sampling parameters to 199, 0, 0\n");
    goto Done;
    }
if (!sim_panel_add_register_bits (panel, "PSL",  NULL, 32, PSL_bits)) {
    printf ("Unexpected success setting PSL bits before setting sampling parameters\n");
    goto Done;
    }
if (!sim_panel_set_sampling_parameters_ex (panel, 500, 40, 100)) {
    printf ("Unexpected success setting sampling parameters to 500, 40, 100\n");
    goto Done;
    }
if (sim_panel_set_sampling_parameters_ex (panel, 500, 10, 100)) {
    printf ("Unexpected error setting sampling parameters to 500, 10, 100: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register_indirect_bits (panel, "PC",  NULL, 32, PC_indirect_bits)) {
    printf ("Error adding register 'PC' indirect bits: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register_bits (panel, "PSL",  NULL, 32, PSL_bits)) {
    printf ("Error adding register 'PSL' bits: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register_bits (panel, "PC",  NULL, 32, PC_bits)) {
    printf ("Error adding register 'PSL' bits: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register_bits (panel, "PCQ[3]",  NULL, 32, PCQ_3_bits)) {
    printf ("Error adding register 'PCQ[3]' bits: %s\n", sim_panel_get_error());
    goto Done;
    }
if (1) {
    unsigned int noop_noop_noop_halt = 0x00010101, brb_self = 0x0000FE11, addr400 = 0x00000400, pc_value;
    int mstime;

    if (sim_panel_mem_deposit (panel, sizeof(addr400), &addr400, sizeof(noop_noop_noop_halt), &noop_noop_noop_halt)) {
        printf ("Error setting %08X to %08X: %s\n", addr400, noop_noop_noop_halt, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_gen_deposit (panel, "PC", sizeof(addr400), &addr400)) {
        printf ("Error setting PC to %08X: %s\n", addr400, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_exec_run(panel)) {
        printf ("Error starting simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (!sim_panel_get_registers (panel, NULL)) {
        printf ("Unexpected success getting register data: %s\n", sim_panel_get_error());
        goto Done;
        }
    mstime = 0;
    while ((sim_panel_get_state (panel) == Run) &&
           (mstime < 1000)) {
        usleep (100000);
        mstime += 100;
        }
    if (sim_panel_get_state (panel) != Halt) {
        printf ("Unexpected execution state not Halt\n");
        goto Done;
        }
    pc_value = 0;
    if (sim_panel_gen_examine (panel, "PC", sizeof(pc_value), &pc_value)) {
        printf ("Unexpected error getting PC value: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (pc_value != addr400 + 4) {
        printf ("Unexpected PC value after HALT: %08X, expected: %08X\n", pc_value, addr400 + 4);
        goto Done;
        }
    if (sim_panel_gen_deposit (panel, "PC", sizeof(addr400), &addr400)) {
        printf ("Error setting PC to %08X: %s\n", addr400, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_exec_step (panel)) {
        printf ("Error executing a single step: %s\n", sim_panel_get_error());
        goto Done;
        }
    pc_value = 0;
    if (sim_panel_gen_examine (panel, "PC", sizeof(pc_value), &pc_value)) {
        printf ("Unexpected error getting PC value: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (pc_value != addr400 + 1) {
        printf ("Unexpected PC value after STEP: %08X, expected: %08X\n", pc_value, addr400 + 1);
        goto Done;
        }
    if (sim_panel_mem_deposit (panel, sizeof(addr400), &addr400, sizeof(brb_self), &brb_self)) {
        printf ("Error setting %08X to %08X: %s\n", addr400, brb_self, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_gen_deposit (panel, "PC", sizeof(addr400), &addr400)) {
        printf ("Error setting PC to %08X: %s\n", addr400, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_exec_run(panel)) {
        printf ("Error starting simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    mstime = 0;
    while ((sim_panel_get_state (panel) == Run) &&
           (mstime < 1000)) {
        usleep (100000);
        mstime += 100;
        }
    if (sim_panel_exec_halt (panel)) {
        printf ("Error executing halt: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_get_state (panel) != Halt) {
        printf ("State not Halt after successful Halt\n");
        goto Done;
        }
    if (sim_panel_device_debug_mode (panel, "DZ", 1, NULL)) {
        printf ("Can't enable Debug for DZ device: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_device_debug_mode (panel, "DZ", 0, "REG")) {
        printf ("Can't enable REG Debug for DZ device: %s\n", sim_panel_get_error());
        goto Done;
        }
    if (!sim_panel_device_debug_mode (panel, "DZ", 0, "REGZZZ")) {
        printf ("Unexpected success disabling REGZZZ Debug for DZ device\n");
        goto Done;
        }
    if (!sim_panel_device_debug_mode (panel, "ZZZDZ", 1, NULL)) {
        printf ("Unexpected success enabling Debug for ZZZDZ device\n");
        goto Done;
        }
    if (sim_panel_device_debug_mode (panel, "DZ", 0, NULL)) {
        printf ("Can't disable All Debug for DZ device: %s\n", sim_panel_get_error());
        goto Done;
        }
    }
sim_panel_clear_error ();
return 0;

Done:
sim_panel_destroy (panel);
panel = NULL;

/* Get rid of pseudo config file created above */
(void)remove (sim_config);
return -1;
}

int
match_command (const char *command, const char *string, const char **arg)
{
int match_chars = 0;
size_t i;

while (isspace (*string))
    ++string;
for (i=0; i < strlen (command); i++) {
    if (command[i] == (islower (string[i]) ? toupper (string[i]) : string[i]))
        continue;
    if (string[i] == '\0')
        break;
    if ((!isspace (string[i])) || (i == 0))
        return 0;
    break;
    }
while (isspace (string[i]))
    ++i;
if (arg)
    *arg = &string[i];
return (i > 0) && (arg ? 1 : (string[i] == '\0'));
}

struct execution_breakpoint {
    unsigned int addr;
    const char *desc;
    const char *extra;
    } breakpoints[] = {
        {0x2004EAD3, "test 52 failure path"},
        {0x2004E6EC, "Test 52: de_programmable_timers.lis line 228 - Generic Error Dispatch",                       "SHOW HIST=10; EX SYSD STATE"},
        {0x2004E7F9, "Test 52: de_programmable_timers.lis line 381 - Interrupt Did Not Occur",                      "SHOW HIST=10; EX SYSD STATE"},
        {0x2004E97C, "Test 53: Subtest 05 - clock failed to tick within at least 100 ms. - de_toy.lis line 232",    "SHOW HIST=10; EX SYSD STATE"},
        {0x2004E9BB, "Test 53: Subtest 07 - Time of year clock is not ticking - de_toy.lis line 274",               "SHOW HIST=10; EX SYSD STATE"},
        {0x2004E9D3, "Test 53: Subtest 08 - Time of year clock is not ticking - de_toy.lis line 295",               "SHOW HIST=10; EX SYSD STATE"},
        {0x2004EA2D, "Test 53: Subtest 09 - Running Slow - de_toy.lis line 359",                                    "SHOW HIST=10; EX SYSD STATE"},
        {0x2004EA39, "Test 53: Subtest 0A - Running Fast - de_toy.lis line 366",                                    "SHOW HIST=10; EX SYSD STATE"},
        {0x0, NULL}
    };

int
main (int argc, char **argv)
{
int was_halted = 1, i;

if ((argc > 1) && ((!strcmp("-d", argv[1])) || (!strcmp("-D", argv[1])) || (!strcmp("-debug", argv[1]))))
    debug = 1;

if (panel_setup())
    goto Done;
if (1) {
    struct {
        unsigned int addr;
        const char *instr;
        } long_running_program[] = {
            {0x2000,  "MOVL #7FFFFFFF,R0"},
            {0x2007,  "MOVL #7FFFFFFF,R1"},
            {0x200E,  "SOBGTR R1,200E"},
            {0x2011,  "SOBGTR R0,2007"},
            {0x2014,  "HALT"},
            {0,NULL}
        };
    int i;

    sim_panel_debug (panel, "Testing sim_panel_exec_halt and sim_panel_destroy() () with simulator in Run State");
    for (i=0; long_running_program[i].instr; i++)
        if (sim_panel_mem_deposit_instruction (panel, sizeof(long_running_program[i].addr), 
                                               &long_running_program[i].addr, long_running_program[i].instr)) {
            printf ("Error setting depositing instruction '%s' into memory at location %XR0: %s\n", 
                    long_running_program[i].instr, long_running_program[i].addr, sim_panel_get_error());
            goto Done;
            }
    if (sim_panel_gen_deposit (panel, "PC", sizeof(long_running_program[0].addr), &long_running_program[0].addr)) {
        printf ("Error setting PC to %X: %s\n", long_running_program[0].addr, sim_panel_get_error());
        goto Done;
        }
    if (sim_panel_exec_start (panel)) {
        printf ("Error starting simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    usleep (100000);    /* .1 seconds */
    sim_panel_debug (panel, "Testing sim_panel_exec_halt");
    if (sim_panel_exec_halt (panel)) {
        printf ("Error halting simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    sim_panel_debug (panel, "Testing sim_panel_exec_run");
    if (sim_panel_exec_run (panel)) {
        printf ("Error resuming simulator execution: %s\n", sim_panel_get_error());
        goto Done;
        }
    usleep (2000000);   /* 2 Seconds */
    sim_panel_debug (panel, "Shutting down while simulator is running");
    sim_panel_destroy (panel);
    }
sim_panel_clear_error ();
InitDisplay ();
if (panel_setup ())
    goto Done;
for (i=0; breakpoints[i].addr; i++) {
    char buf[120];

    sprintf (buf, "%08X;SHOW QUEUE%s%s", breakpoints[i].addr, breakpoints[i].extra ? ";" : "", breakpoints[i].extra ? breakpoints[i].extra : "");
    if (sim_panel_break_set (panel, buf)) {
        printf ("Error establishing breakpoint at %s: %s\n", breakpoints[i].desc, sim_panel_get_error());
        goto Done;
        }
    }
sim_panel_debug (panel, "Testing with Command interface");
DisplayRegisters(panel, 1, 1);
while (1) {
    char cmd[512];
    const char *arg;

    while (sim_panel_get_state (panel) == Halt) {
        sim_panel_debug (panel, "Halted - Getting registers...");
        sim_panel_get_registers (panel, &simulation_time);
        if (!was_halted) {
            const char *haltmsg = sim_panel_halt_text (panel);
            const char *bpt;
            unsigned int Bpt_PC;

            DisplayRegisters (panel, 0, 1);
            if (*haltmsg)
                printf ("%s", haltmsg);
            if ((bpt = strstr (haltmsg, "Breakpoint, PC: "))) {
                sscanf (bpt, "Breakpoint, PC: %X", &Bpt_PC);
                for (i=0; breakpoints[i].addr; i++) {
                    if (Bpt_PC == breakpoints[i].addr) {
                        printf ("Breakpoint at: %08X %s\n", breakpoints[i].addr, breakpoints[i].desc);
                        break;
                        }
                    }
                }
            }
        was_halted = 1;
        printf ("SIM> ");
        if (!fgets (cmd, sizeof(cmd)-1, stdin))
            break;
        while (strlen(cmd) && isspace(cmd[strlen(cmd)-1]))
            cmd[strlen(cmd)-1] = '\0';
        DisplayRegisters (panel, 1, 1);
        if (match_command ("BOOT", cmd, &arg)) {
            if (sim_panel_exec_boot (panel, arg))
                break;
            }
        else if (match_command ("BREAK ", cmd, &arg)) {
            if (sim_panel_break_set (panel, arg))
                printf("Error Setting Breakpoint '%s': %s\n", arg, sim_panel_get_error ());
            }
        else if (match_command ("NOBREAK ", cmd, &arg)) {
            if (sim_panel_break_clear (panel, arg))
                printf("Error Clearing Breakpoint '%s': %s\n", arg, sim_panel_get_error ());
            }
        else if (match_command ("STEP", cmd, NULL)) {
            if (sim_panel_exec_step (panel))
                break;
            }
        else if (match_command ("CONT", cmd, NULL)) {
            if (sim_panel_exec_run (panel))
                break;
            }
        else if (match_command ("EXAMINE ", cmd, &arg)) {
            int value;

            if (sim_panel_gen_examine (panel, arg, sizeof (value), &value))
                printf("Error EXAMINE %s: %s\n", arg, sim_panel_get_error ());
            else
                printf("%s: %08X\n", arg, value);
            }
        else if (match_command ("HISTORY ", cmd, &arg)) {
            char history[10240];
            int count = atoi (arg);

            history[sizeof (history) - 1] = '\0';
            if (sim_panel_get_history (panel, count, sizeof (history) -1, history))
                printf("Error retrieving instruction history: %s\n", sim_panel_get_error ());
            else
                printf("%s\n", history);
            }
        else if (match_command ("DEBUG ", cmd, &arg)) {
            if (arg[0] == '-') {
                if (sim_panel_device_debug_mode (panel, NULL, 1, arg))
                    printf("Error setting debug mode: %s\n", sim_panel_get_error ());
                }
            else {
                if (sim_panel_device_debug_mode (panel, arg, 1, NULL))
                    printf("Error setting debug mode: %s\n", sim_panel_get_error ());
                }
            }
        else if ((match_command ("EXIT", cmd, NULL)) || (match_command ("QUIT", cmd, NULL)))
            goto Done;
        else {
            DisplayRegisters (panel, 0, 1);
            printf ("Huh? %s\r\n", cmd);
            }
        }
    while (sim_panel_get_state (panel) == Run) {
        usleep (100000);
        if (update_display) {
            update_display = 0;
            DisplayRegisters(panel, 0, 0);
            }
        was_halted = 0;
        if (halt_cpu) {
            halt_cpu = 0;
            sim_panel_exec_halt (panel);
            }
        }
    }

Done:
DisplayRegisters (panel, 0, 1);
sim_panel_destroy (panel);

/* Get rid of pseudo config file created earlier */
(void)remove (sim_config);
}
