/* PDP11Panel.c: simulator frontpanel API client for the local PDP-11/70 RT-11 setup

   Based on frontpanel/FrontPanelTest.c (simh frontpanel API sample,
   Copyright (c) 2015, Mark Pizzolato), trimmed down and adapted to
   drive the PDP-11 simulator instead of VAX.

   This program starts BIN/pdp11 with configs/rt11-panel.ini (CPU type,
   RK0 attach - no boot/run), connects to it via the simh frontpanel API,
   and displays PC, PSW, SP and R0-R5 live while the guest console is
   reachable via telnet on the configured port.
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "sim_frontpanel.h"
#include <signal.h>

#if defined(_WIN32)
#include <windows.h>
#define usleep(n) Sleep((n)/1000)
#else
#include <unistd.h>
#endif

#define ESC "\033"
#define CSI ESC "["
#define CONSOLE_PORT "9999"

static char *sim_path;
static char *sim_config = "PDP11-PANEL.ini";

static unsigned short PC, PSW, SP, R0, R1, R2, R3, R4, R5;
unsigned long long simulation_time;
int update_display = 1;

static void DisplayCallback (PANEL *panel, unsigned long long sim_time, void *context)
{
simulation_time = sim_time;
update_display = 1;
}

static void DisplayRegisters (PANEL *panel, int get_pos, int set_pos)
{
char buf1[100], buf2[100];
static const char *states[] = {"Halt", "Run "};

if (panel == NULL)
    return;
sprintf (buf1, "%4s  PC: %06o   PSW: %06o   SP: %06o   Instructions: %llu\n",
         states[sim_panel_get_state (panel)], PC, PSW, SP, simulation_time);
sprintf (buf2, "R0: %06o  R1: %06o  R2: %06o  R3: %06o  R4: %06o  R5: %06o\n",
         R0, R1, R2, R3, R4, R5);
if (get_pos)
    printf (CSI "s");
printf (CSI "H");
printf ("%s", buf1);
printf ("%s", buf2);
if (set_pos)
    printf (CSI "u");
printf ("\r\n");
}

static void CleanupDisplay (void)
{
(void)remove (sim_config);
}

static void InitDisplay (void)
{
printf (CSI "H" CSI "2J");
printf ("\n\n\n");
printf ("Guest console: telnet localhost " CONSOLE_PORT "\n");
printf ("^C to Halt, Commands: BOOT, CONT, STEP, EXAMINE <reg>, BREAK <addr>, NOBREAK <addr>, EXIT\n");
atexit (CleanupDisplay);
}

static volatile int halt_cpu = 0;
static PANEL *panel;

static void halt_handler (int sig)
{
signal (SIGINT, halt_handler);
halt_cpu = 1;
}

static int panel_setup (void)
{
FILE *f;

if ((f = fopen (sim_config, "w")) == NULL) {
    printf ("Error creating panel config file %s\n", sim_config);
    return -1;
    }
fprintf (f, "set cpu 11/70\n");
fprintf (f, "set rk0 enabled\n");
fprintf (f, "attach rk0 disks/rt11-v4.dsk\n");
fprintf (f, "set console telnet=buffered\n");
fprintf (f, "set console -u telnet=" CONSOLE_PORT "\n");
fclose (f);

signal (SIGINT, halt_handler);
panel = sim_panel_start_simulator (sim_path, sim_config, 0);
if (!panel) {
    printf ("Error starting simulator %s with config %s: %s\n", sim_path, sim_config, sim_panel_get_error ());
    return -1;
    }

#define ADDREG(name, var) \
    if (sim_panel_add_register (panel, name, NULL, sizeof (var), &(var))) { \
        printf ("Error adding register '%s': %s\n", name, sim_panel_get_error ()); \
        return -1; \
        }

ADDREG ("PC", PC)
ADDREG ("PSW", PSW)
ADDREG ("SP", SP)
ADDREG ("R0", R0)
ADDREG ("R1", R1)
ADDREG ("R2", R2)
ADDREG ("R3", R3)
ADDREG ("R4", R4)
ADDREG ("R5", R5)

if (sim_panel_get_registers (panel, NULL)) {
    printf ("Error getting register data: %s\n", sim_panel_get_error ());
    return -1;
    }
if (sim_panel_set_display_callback_interval (panel, &DisplayCallback, NULL, 100000)) {
    printf ("Error setting display callback: %s\n", sim_panel_get_error ());
    return -1;
    }
return 0;
}

static int match_command (const char *command, const char *string, const char **arg)
{
int match_chars = 0;
size_t i;

while (isspace ((unsigned char)*string))
    ++string;
for (i = 0; i < strlen (command); i++) {
    if (command[i] == (islower ((unsigned char)string[i]) ? toupper ((unsigned char)string[i]) : string[i]))
        continue;
    if (string[i] == '\0')
        break;
    if ((!isspace ((unsigned char)string[i])) || (i == 0))
        return 0;
    break;
    }
while (isspace ((unsigned char)string[i]))
    ++i;
if (arg)
    *arg = &string[i];
return (i > 0) && (arg ? 1 : (string[i] == '\0'));
}

int main (int argc, char **argv)
{
int was_halted = 1, i;
char *c;

sim_path = (char *)malloc (strlen (argv[0]) + 10);
strcpy (sim_path, argv[0]);
c = strrchr (sim_path, '/');
if (c == NULL)
    c = strrchr (sim_path, '\\');
if (c == NULL)
    strcpy (sim_path, "pdp11");
else
    strcpy (c + 1, "pdp11");

InitDisplay ();
if (panel_setup ())
    goto Done;

DisplayRegisters (panel, 1, 1);
while (1) {
    char cmd[512];
    const char *arg;

    while (sim_panel_get_state (panel) == Halt) {
        sim_panel_get_registers (panel, &simulation_time);
        if (!was_halted) {
            const char *haltmsg = sim_panel_halt_text (panel);
            DisplayRegisters (panel, 0, 1);
            if (*haltmsg)
                printf ("%s\n", haltmsg);
            }
        was_halted = 1;
        printf ("PANEL> ");
        if (!fgets (cmd, sizeof (cmd) - 1, stdin))
            break;
        while (strlen (cmd) && isspace ((unsigned char)cmd[strlen (cmd) - 1]))
            cmd[strlen (cmd) - 1] = '\0';
        DisplayRegisters (panel, 1, 1);
        if (match_command ("BOOT", cmd, &arg)) {
            if (sim_panel_exec_boot (panel, (*arg) ? arg : "RK0"))
                printf ("Error booting: %s\n", sim_panel_get_error ());
            }
        else if (match_command ("BREAK ", cmd, &arg)) {
            if (sim_panel_break_set (panel, arg))
                printf ("Error setting breakpoint '%s': %s\n", arg, sim_panel_get_error ());
            }
        else if (match_command ("NOBREAK ", cmd, &arg)) {
            if (sim_panel_break_clear (panel, arg))
                printf ("Error clearing breakpoint '%s': %s\n", arg, sim_panel_get_error ());
            }
        else if (match_command ("STEP", cmd, NULL)) {
            if (sim_panel_exec_step (panel))
                printf ("Error stepping: %s\n", sim_panel_get_error ());
            }
        else if (match_command ("CONT", cmd, NULL)) {
            if (sim_panel_exec_run (panel))
                printf ("Error continuing: %s\n", sim_panel_get_error ());
            }
        else if (match_command ("EXAMINE ", cmd, &arg)) {
            int value;

            if (sim_panel_gen_examine (panel, arg, sizeof (value), &value))
                printf ("Error EXAMINE %s: %s\n", arg, sim_panel_get_error ());
            else
                printf ("%s: %06o\n", arg, value);
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
            DisplayRegisters (panel, 0, 0);
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
sim_panel_destroy (&panel);
return 0;
}
