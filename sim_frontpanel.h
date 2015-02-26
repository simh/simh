/* sim_frontpanel.h: simulator frontpanel API definitions

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

   15-Jan-15    MP      Initial implementation

   This module defines interface between a front panel application and a simh
   simulator.  Facilities provide ways to gather information from and to 
   observe and control the state of a simulator.

   Any application which wants to use this API needs to:
      1) include this file in the application code
      2) compile sim_frontpanel.c and sim_sock.c from the top level directory 
         of the simh source.
      3) link the sim_frontpanel and sim_sock object modules and libpthreads 
         into the application.
      4) Use a simh simulator built from the same version of simh that the
         sim_frontpanel and sim_sock modules came from.

*/

#ifndef SIM_FRONTPANEL_H_
#define SIM_FRONTPANEL_H_     0

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#if !defined(__VAX)         /* Supported platform */

#define SIM_FRONTPANEL_VERSION   1

/**

    sim_panel_start_simulator       A starts a simulator with a particular 
                                    configuration

        sim_path            the path to the simulator binary
        sim_config          the configuration to run the simulator with
        device_panel_count  the number of sub panels for connected devices

    Note 1: - The path specified must be either a fully specified path or 
              it could be merey the simulator name if the simulator binary
              is located in the current PATH.
            - The simulator binary must be built from the same version 
              simh source code that the frontpanel API was acquired fron 
              (the API and the simh framework must speak the same language) 

    Note 2: - Configuration file specified should contain device setup 
              statements (enable, disable, CPU types and attach commands).
              It should not start a simulator running.

 */

typedef struct PANEL PANEL;

PANEL *
sim_panel_start_simulator (const char *sim_path,
                           const char *sim_config,
                           size_t device_panel_count);

/**

    sim_panel_add_device_panel - creates a sub panel associated 
                                 with a specific simulator panel

        simulator_panel     the simulator panel to connect to
        device_name         the simulator's name for the device

 */
PANEL *
sim_panel_add_device_panel (PANEL *simulator_panel,
                            const char *device_name);

/**

    sim_panel_destroy   to shutdown a panel or sub panel.

    Note: destroying a simulator panel will also destroy any 
          related sub panels

 */
int
sim_panel_destroy (PANEL *panel);

/**

   The frontpanel API exposes the state of a simulator via access to 
   simh register variables that the simulator and its devices define.
   These registers certainly include any architecturally described 
   registers (PC, PSL, SP, etc.), but also include anything else
   the simulator uses as internal state to implement the running 
   simulator.

   The registers that a particular frontpanel application mught need 
   access to are described by the application by calling: 
   
   sim_panel_add_register

        name         the name the simulator knows this register by
        device_name  the device this register is part of.  Defaults to
                     the device of the panel (in a device panel) or the
                     default device in the simulator (usually the CPU).
        size         the size (in local storage) of the buffer which will
                     receive the data in the simulator's register
        addr         a pointer to the location of the buffer which will 
                     be loaded with the data in the simulator's register

 */
int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        const char *device_name,
                        size_t size,
                        void *addr);

/**

    A panel application has a choice of two different methods of getting 
    the values contained in the set of registers it has declared interest in via
    the sim_panel_add_register API.
    
       1)  The values can be polled (when ever it is desired) by calling
           sim_panel_get_registers().
       2)  The panel can call sim_panel_set_display_callback() to specify a
           callback routine and a periodic rate that the callback routine
           should be called.  The panel API will make a best effort to deliver
           the current register state at the desired rate.


   Note 1: The buffers described in a panel's register set will be dynamically
           revised as soon as data is available from the simulator.  The 
           callback routine merely serves as a notification that a complete 
           register set has arrived.

 */
int
sim_panel_get_registers (PANEL *panel, unsigned long long *simulation_time);

/**


 */
typedef void (*PANEL_DISPLAY_PCALLBACK)(PANEL *panel, 
                                        unsigned long long simulation_time,
                                        void *context);

int
sim_panel_set_display_callback (PANEL *panel, 
                                PANEL_DISPLAY_PCALLBACK callback, 
                                void *context, 
                                int callbacks_per_second);

/**

    When a front panel application needs to change the running
    state of a simulator one of the following routines should 
    be called:  
    
    sim_panel_exec_halt     - Stop instruction execution
    sim_panel_exec_boot     - Boot a simulator from a specific device
    sim_panel_exec_run      - Start/Resume a simulator running instructions
    sim_panel_exec_step     - Have a simulator execute a single step

 */
int
sim_panel_exec_halt (PANEL *panel);

int
sim_panel_exec_boot (PANEL *panel, const char *device);

int
sim_panel_exec_run (PANEL *panel);

int
sim_panel_exec_step (PANEL *panel);

/**
   sim_panel_set_register_value

        name        the name of a simulator register which is to receive 
                    a new value
        value       the new value in character string form.  The string 
                    must be in the native/natural radix that the simulator 
                    uses when referencing that register

 */
int
sim_panel_set_register_value (PANEL *panel,
                              const char *name,
                              const char *value);


typedef enum {
    Halt,       /* Simulation is halted (instructions not being executed) */
    Run,        /* Simulation is executing instructions */
    Error       /* Panel simulator is in an error state and should be */
                /* closed (destroyed).  sim_panel_get_error might help */
                /* explain why */
    } OperationalState;

OperationalState
sim_panel_get_state (PANEL *panel);

/**

    All APIs routines which return an int return 0 for 
    success and -1 for an error.  
    
    sim_panel_get_error     - the details of the most recent error
    sim_panel_clear_error   - clears the error buffer

 */

const char *sim_panel_get_error (void);
void sim_panel_clear_error (void);

/**

    The panek<->simulator wire protocol can be traced if protocol problems arise.
    
    sim_panel_set_debug_file    - Specifies the log file to record debug traffic
    sim_panel_set_debug_mode    - Specifies the debug detail to be recorded
    sim_panel_flush_debug       - Flushes debug output to disk

 */
void
sim_panel_set_debug_file (PANEL *panel, const char *debug_file);

#define DBG_XMT         1   /* Transmit Data */
#define DBG_RCV         2   /* Receive Data */

void
sim_panel_set_debug_mode (PANEL *panel, int debug_bits);

void
sim_panel_flush_debug (PANEL *panel);

#endif /* !defined(__VAX) */

#ifdef  __cplusplus
}
#endif

#endif /* SIM_FRONTPANEL_H_ */