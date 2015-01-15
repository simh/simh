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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

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

typedef enum {
    Idle,
    InputTransfer,
    OutputTransfer,
    Halted,
    Running
    } OperationalState;

typedef struct sim_frontpanel PANEL;

typedef void (*PANEL_DISPLAY_PCALLBACK)(PANEL *panel, 
                                        void *context);

PANEL *
sim_panel_start_simulator (const char *sim_path,
                           const char *sim_config);

int
sim_panel_stop_simulator (PANEL *panel);

int
sim_panel_add_register (PANEL *panel,
                        const char *name,
                        int radix,
                        size_t size,
                        void *addr);

int
sim_panel_get_registers (PANEL *panel);

int
sim_panel_set_display_callback (PANEL *panel, 
                                PANEL_DISPLAY_PCALLBACK callback, 
                                void *context, 
                                int callbacks_per_second);

int
sim_panel_exec_halt (PANEL *panel);

int
sim_panel_exec_run (PANEL *panel);

int
sim_panel_exec_step (PANEL *panel);

const char *sim_panel_get_error (void);
void sim_panel_clear_error (void);
int sim_panel_set_error (const char *fmt, ...);


#ifdef  __cplusplus
}
#endif

#endif /* SIM_FRONTPANEL_H_ */