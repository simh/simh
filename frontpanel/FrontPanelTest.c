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
#define usleep(n) Sleep(n/1000)
#else
#include <unistd.h>
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
unsigned int PC, SP, FP, AP, PSL, R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, atPC;
unsigned int PCQ[32];

int update_display = 1;

static void
DisplayCallback (PANEL *panel, unsigned long long simulation_time, void *context)
{
update_display = 1;
}

static void
DisplayRegisters (PANEL *panel)
{
char buf1[100], buf2[100], buf3[100], buf4[100];
static const char *states[] = {"Halt", "Run "};

if (!update_display)
    return;
update_display = 0;
buf1[sizeof(buf1)-1] = buf2[sizeof(buf2)-1] = buf3[sizeof(buf3)-1] = 0;
sprintf (buf1, "%s PC: %08X   SP: %08X   AP: %08X   FP: %08X  @PC: %08X\r\n", states[sim_panel_get_state (panel)], PC, SP, AP, FP, atPC);
sprintf (buf2, "PSL: %08X %s\r\n", PSL, "");
sprintf (buf3, "R0:%08X  R1:%08X  R2:%08X  R3:%08X   R4:%08X   R5:%08X\r\n", R0, R1, R2, R3, R4, R5);
sprintf (buf4, "R6:%08X  R7:%08X  R8:%08X  R9:%08X  R10:%08X  R11:%08X\r\n", R6, R7, R8, R9, R10, R11);
#if defined(_WIN32)
if (1) {
    static HANDLE out = NULL;
    CONSOLE_SCREEN_BUFFER_INFO info;
    static COORD origin;
    int written;

    if (out == NULL)
        out = GetStdHandle (STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo (out, &info);
    SetConsoleCursorPosition (out, origin);
    WriteConsoleA(out, buf1, strlen(buf1), &written, NULL);
    WriteConsoleA(out, buf2, strlen(buf2), &written, NULL);
    WriteConsoleA(out, buf3, strlen(buf3), &written, NULL);
    WriteConsoleA(out, buf4, strlen(buf4), &written, NULL);
    SetConsoleCursorPosition (out, info.dwCursorPosition);
    }
#else
#define ESC "\033"
#define CSI ESC "["
printf (CSI "s");   /* Save Cursor Position */
printf (CSI "H");   /* Position to Top of Screen (1,1) */
printf ("%s", buf1);
printf ("%s", buf2);
printf ("%s", buf3);
printf ("%s", buf4);
printf (CSI "s");   /* Restore Cursor Position */
printf ("\r\n");
#endif
}

static
void InitDisplay (void)
{
#if defined(_WIN32)
system ("cls");
#else
printf (CSI "H");   /* Position to Top of Screen (1,1) */
printf (CSI "2J");  /* Clear Screen */
#endif
printf ("\n\n\n\n");
printf ("^C to Halt, Commands: BOOT, CONT, EXIT\n");
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

int
main (int argc, char **argv)
{
FILE *f;
int debug = 0;

if ((argc > 1) && ((!strcmp("-d", argv[1])) || (!strcmp("-D", argv[1])) || (!strcmp("-debug", argv[1]))))
    debug = 1;
/* Create pseudo config file for a test */
if ((f = fopen (sim_config, "w"))) {
    if (debug) {
        fprintf (f, "set verbose\n");
        fprintf (f, "set log simulator.dbg\n");
        fprintf (f, "set debug -n -a log\n");
        fprintf (f, "set cpu conhalt\n");
        fprintf (f, "set remote telnet=2226\n");
        fprintf (f, "set rem-con debug=XMT;RCV\n");
        fprintf (f, "set remote notelnet\n");
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

InitDisplay();
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
    sim_panel_set_debug_mode (panel, DBG_XMT|DBG_RCV);
    }

tape = sim_panel_add_device_panel (panel, "TAPE DRIVE");

if (!tape) {
    printf ("Error adding tape device to simulator: %s\n", sim_panel_get_error());
    goto Done;
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
if (sim_panel_add_register (panel, "AP",  NULL, sizeof(SP), &AP)) {
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
    long deadbeef = 0xdeadbeef, beefdead = 0xbeefdead, addr200 = 0x00000200, beefdata;

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
if (sim_panel_set_display_callback (panel, &DisplayCallback, NULL, 5)) {
    printf ("Error setting automatic display callback: %s\n", sim_panel_get_error());
    goto Done;
    }
if (!sim_panel_get_registers (panel, NULL)) {
    printf ("Unexpected success getting register data: %s\n", sim_panel_get_error());
    goto Done;
    }
sim_panel_clear_error ();
if (!sim_panel_dismount (panel, "RL0")) {
    printf ("Unexpected success while dismounting media file from non mounted RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_mount (panel, "RL0", "-N", "TEST-RL.DSK")) {
    printf ("Error while mounting media file TEST-RL.DSK on RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_dismount (panel, "RL0")) {
    printf ("Error while dismounting media file from RL0: %s\n", sim_panel_get_error());
    goto Done;
    }
remove ("TEST-RL.DSK");
if (sim_panel_break_set (panel, "400")) {
    printf ("Unexpected establishing a breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_clear (panel, "400")) {
    printf ("Unexpected clearing a breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_set (panel, "\"32..31..30\"")) {
    printf ("Unexpected establishing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_break_output_clear (panel, "\"32..31..30\"")) {
    printf ("Unexpected clearing an output breakpoint: %s\n", sim_panel_get_error());
    goto Done;
    }
sim_panel_clear_error ();
while (1) {
    size_t i;
    char cmd[512];

    while (sim_panel_get_state (panel) == Halt) {
        DisplayRegisters (panel);
        printf ("SIM> ");
        if (!fgets (cmd, sizeof(cmd)-1, stdin))
            break;
        while (strlen(cmd) && isspace(cmd[strlen(cmd)-1]))
            cmd[strlen(cmd)-1] = '\0';
        while (isspace(cmd[0]))
            memmove (cmd, cmd+1, strlen(cmd));
        for (i=0; i<strlen(cmd); i++) {
            if (islower(cmd[i]))
                cmd[i] = toupper(cmd[i]);
            }
        if (!memcmp("BOOT", cmd, 4)) {
            if (sim_panel_exec_boot (panel, cmd + 4))
                break;
            }
        else if (!strcmp("STEP", cmd)) {
            if (sim_panel_exec_step (panel))
                break;
            }
        else if (!strcmp("CONT", cmd)) {
            if (sim_panel_exec_run (panel))
                break;
            }
        else if (!strcmp("EXIT", cmd))
            goto Done;
        else
            printf ("Huh? %s\r\n", cmd);
        }
    while (sim_panel_get_state (panel) == Run) {
        usleep (100);
        if (update_display)
            DisplayRegisters(panel);
        if (halt_cpu) {
            halt_cpu = 0;
            sim_panel_exec_halt (panel);
            }
        }
    }

Done:
sim_panel_destroy (panel);

/* Get rid of pseudo config file created above */
remove (sim_config);
}
