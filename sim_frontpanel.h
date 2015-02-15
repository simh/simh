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

*/

#ifndef SIM_FRONTPANEL_H_
#define SIM_FRONTPANEL_H_     0

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdlib.h>

typedef struct PANEL PANEL;

PANEL *
sim_panel_start_simulator (const char *sim_path,
                           const char *sim_config,
                           size_t device_panel_count);

PANEL *
sim_panel_add_device_panel (PANEL *simulator_panel,
                            const char *device_name);

int
sim_panel_destroy (PANEL *panel);

int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        size_t size,
                        void *addr);

int
sim_panel_get_registers (PANEL *panel);

typedef void (*PANEL_DISPLAY_PCALLBACK)(PANEL *panel, 
                                        void *context);

int
sim_panel_set_display_callback (PANEL *panel, 
                                PANEL_DISPLAY_PCALLBACK callback, 
                                void *context, 
                                int callbacks_per_second);

int
sim_panel_exec_halt (PANEL *panel);

int
sim_panel_exec_boot (PANEL *panel, const char *device);

int
sim_panel_exec_run (PANEL *panel);

int
sim_panel_exec_step (PANEL *panel);

int
sim_panel_set_register_value (PANEL *panel,
                              const char *name,
                              const char *value);

typedef enum {
    Halt,
    Run
    } OperationalState;

OperationalState
sim_panel_get_state (PANEL *panel);

const char *sim_panel_get_error (void);
void sim_panel_clear_error (void);

void
sim_panel_set_debug_file (PANEL *panel, const char *debug_file);

#define DBG_XMT         1   /* Transmit Data */
#define DBG_RCV         2   /* Receive Data */

void
sim_panel_set_debug_mode (PANEL *panel, int debug_bits);

void
sim_panel_flush_debug (PANEL *panel);

#ifdef  __cplusplus
}
#endif

#endif /* SIM_FRONTPANEL_H_ */