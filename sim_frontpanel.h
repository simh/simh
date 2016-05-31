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
   01-Apr-15    MP      Added register indirect, mem_examine and mem_deposit
   03-Apr-15    MP      Added logic to pass simulator startup messages in
                        panel error text if the connection to the simulator
                        shuts down while it is starting.
   04-Apr-15    MP      Added mount and dismount routines to connect and 
                        disconnect removable media

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

#if !defined(__VAX)         /* Unsupported platform */

#define SIM_FRONTPANEL_VERSION   2

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

PANEL *
sim_panel_start_simulator_debug (const char *sim_path,
                                 const char *sim_config,
                                 size_t device_panel_count,
                                 const char *debug_file);

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
   sim_panel_add_register_array
and
   sim_panel_add_register_indirect

        name         the name the simulator knows this register by
        device_name  the device this register is part of.  Defaults to
                     the device of the panel (in a device panel) or the
                     default device in the simulator (usually the CPU).
        element_count number of elements in the register array
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

int
sim_panel_add_register_array (PANEL *panel,
                              const char *name,
                              const char *device_name,
                              size_t element_count,
                              size_t size,
                              void *addr);

int
sim_panel_add_register_indirect (PANEL *panel,
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


   Note 1: The buffers described in a panel's register set will be 
           dynamically revised as soon as data is available from the 
           simulator.  The callback routine merely serves as a notification 
           that a complete register set has arrived.
   Note 2: The callback routine should, in general, not run for a long time
           or frontpanel interactions with the simulator may be disrupted.  
           Setting a flag, signaling an event or posting a message are 
           reasonable activities to perform in a callback routine.

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

    When a front panel application wants to describe conditions that 
    should stop instruction execution an execution or an output
    should be used.  To established or clear a breakpoint, one of 
    the following routines should be called:  
    
    sim_panel_break_set          - Establish a simulation breakpoint
    sim_panel_break_clear        - Cancel/Delete a previously defined
                                   breakpoint
    sim_panel_break_output_set   - Establish a simulator output 
                                   breakpoint
    sim_panel_break_output_clear - Cancel/Delete a previously defined
                                   output breakpoint
    
    Note: Any breakpoint switches/flags must be located at the 
          beginning of the condition string

 */

int
sim_panel_break_set (PANEL *panel, const char *condition);

int
sim_panel_break_clear (PANEL *panel, const char *condition);

int
sim_panel_break_output_set (PANEL *panel, const char *condition);

int
sim_panel_break_output_clear (PANEL *panel, const char *condition);


/**

    When a front panel application needs to change or access
    memory or a register one of the following routines should 
    be called:  
    
    sim_panel_gen_examine        - Examine register or memory
    sim_panel_gen_deposit        - Deposit to register or memory
    sim_panel_mem_examine        - Examine memory location
    sim_panel_mem_deposit        - Deposit to memory location
    sim_panel_set_register_value - Deposit to a register or memory 
                                   location

 */


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
                       void *value);
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
                       const void *value);

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
                       void *value);

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
                       const void *value);

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
                              const char *value);

/**

    When a front panel application may needs to change the media
    in a simulated removable media device one of the following 
    routines should be called:

    sim_panel_mount    - mounts the indicated media file on a device
    sim_panel_dismount - dismounts the currently mounted media file 
                         from a device

 */

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
                 const char *path);

/**
   sim_panel_dismount

        device      the name of a simulator device/unit

 */
int
sim_panel_dismount (PANEL *panel,
                    const char *device);


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

    An API which returns an error (-1), will not change the panel state.
    
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
