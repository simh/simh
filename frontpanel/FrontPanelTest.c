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
char *sim_path = 
#if defined(_WIN32)
            "vax.exe";
#else
            "vax";
#endif

char *sim_config = 
            "VAX-PANEL.ini";

/* Registers visible on the Front Panel */
unsigned int PC, SP, FP, AP, R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11;

static void
DisplayCallback (PANEL *panel, void *context)
{
char buf1[100], buf2[100], buf3[100];
static char *states[] = {"Halt", "Run "};

buf1[sizeof(buf1)-1] = buf2[sizeof(buf2)-1] = buf3[sizeof(buf3)-1] = 0;
sprintf (buf1, "%s PC: %08X   SP: %08X   AP: %08X   FP: %08X\r\n", states[sim_panel_get_state (panel)], PC, SP, AP, FP);
sprintf (buf2, "R0:%08X  R1:%08X  R2:%08X  R3:%08X   R4:%08X   R5:%08X\r\n", R0, R1, R2, R3, R4, R5);
sprintf (buf3, "R6:%08X  R7:%08X  R8:%08X  R9:%08X  R10:%08X  R11:%08X\r\n", R6, R7, R8, R9, R10, R11);
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
printf ("\n\n\n");
printf ("^C to Halt, Commands: BOOT, CONT, EXIT\n");
}

volatile int halt_cpu = 0;

void halt_handler (int sig)
{
signal (SIGINT, halt_handler);      /* Re-establish handler for some platforms that implement ONESHOT signal dispatch */
halt_cpu = 1;
return;
}

int
main (int argc, char **argv)
{
PANEL *panel;
FILE *f;

/* Create pseudo config file for a test */
if ((f = fopen (sim_config, "w"))) {
    fprintf (f, "set cpu autoboot\n");
    fprintf (f, "set cpu 64\n");
    fprintf (f, "set cpu conhalt\n");
    fprintf (f, "set console telnet=buffered\n");
    fprintf (f, "set console telnet=1923\n");
    fclose (f);
    }

InitDisplay();
signal (SIGINT, halt_handler);
panel = sim_panel_start_simulator (sim_path,
                                   sim_config);

if (!panel) {
    printf ("Error starting simulator: %s\n", sim_panel_get_error());
    goto Done;
    }

if (sim_panel_add_register (panel, "PC",  sizeof(PC), &PC)) {
    printf ("Error adding register 'PC': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "SP",  sizeof(SP), &SP)) {
    printf ("Error adding register 'SP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "FP",  sizeof(FP), &FP)) {
    printf ("Error adding register 'FP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "AP",  sizeof(SP), &AP)) {
    printf ("Error adding register 'AP': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R0",  sizeof(R0), &R0)) {
    printf ("Error adding register 'R0': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R1",  sizeof(R1), &R1)) {
    printf ("Error adding register 'R1': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R2",  sizeof(R2), &R2)) {
    printf ("Error adding register 'R2': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R3",  sizeof(R3), &R3)) {
    printf ("Error adding register 'R3': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R4",  sizeof(R4), &R4)) {
    printf ("Error adding register 'R4': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R5",  sizeof(R5), &R5)) {
    printf ("Error adding register 'R5': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R6",  sizeof(R6), &R6)) {
    printf ("Error adding register 'R6': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R7",  sizeof(R7), &R7)) {
    printf ("Error adding register 'R7': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R8",  sizeof(R8), &R8)) {
    printf ("Error adding register 'R8': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R9",  sizeof(R9), &R9)) {
    printf ("Error adding register 'R9': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R10",  sizeof(R10), &R10)) {
    printf ("Error adding register 'R10': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_add_register (panel, "R11",  sizeof(R11), &R11)) {
    printf ("Error adding register 'R11': %s\n", sim_panel_get_error());
    goto Done;
    }
if (sim_panel_get_registers (panel)) {
    printf ("Error getting register data: %s\n", sim_panel_get_error());
    goto Done;
    }
DisplayCallback (panel, NULL);
if (sim_panel_set_display_callback (panel, &DisplayCallback, NULL, 5)) {
    printf ("Error setting automatic display callback: %s\n", sim_panel_get_error());
    goto Done;
    }
if (!sim_panel_get_registers (panel)) {
    printf ("Unexpected success getting register data: %s\n", sim_panel_get_error());
    goto Done;
    }
while (1) {
    size_t i;
    char cmd[512];

    while (sim_panel_get_state (panel) == Halt) {
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
        if (!strcmp("STEP", cmd)) {
            if (sim_panel_exec_step (panel))
                break;
            }
        if (!strcmp("CONT", cmd)) {
            if (sim_panel_exec_run (panel))
                break;
            }
        if (!strcmp("EXIT", cmd))
            goto Done;
        }
    while (sim_panel_get_state (panel) == Run) {
        usleep (100);
        if (halt_cpu) {
            halt_cpu = 0;
            sim_panel_exec_halt (panel);
            }
        }
    }

Done:
sim_panel_stop_simulator (panel);

/* Get rid of pseudo config file created above */
remove (sim_config);
}
