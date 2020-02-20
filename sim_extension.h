/* sim_extension.h: SCP extension routines declarations

   Copyright (c) 2019-2020, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   31-Dec-19    JDB     First release version
   18-Mar-19    JDB     Created file
*/



#include "sim_tmxr.h"



/* Extension remappings.

   These remappings cause the insertion of shims when a VM being compiled
   includes this declaration module.  The shims are not wanted when compiling
   the extension module itself.


   Implementation notes:

    1. When the extensions are being compiled, the module includes the
       "sim_serial.h" file, which includes the "sim_rs232.h" file.  Therefore,
       we do not want to include the latter file a second time and so exclude
       it when compiling the extensions module.
*/

#if ! defined (COMPILING_EXTENSIONS)

  #include "sim_rs232.h"

  #define sim_putchar        ex_sim_putchar
  #define sim_putchar_s      ex_sim_putchar_s
  #define sim_poll_kbd       ex_sim_poll_kbd

  #define sim_brk_test       ex_sim_brk_test

  #define tmxr_attach_unit   ex_tmxr_attach_unit
  #define tmxr_detach_unit   ex_tmxr_detach_unit
  #define tmxr_detach_line   ex_tmxr_detach_line
  #define tmxr_control_line  ex_tmxr_control_line
  #define tmxr_line_status   ex_tmxr_line_status
  #define tmxr_poll_conn     ex_tmxr_poll_conn
  #define tmxr_line_free     ex_tmxr_line_free
  #define tmxr_mux_free      ex_tmxr_mux_free

  #define sim_instr          vm_sim_instr
  #define sim_vm_init        vm_sim_vm_init
  #define sim_vm_cmd         vm_sim_vm_cmd

#endif


/* Last SCP error code */

#define SCPE_LAST           (SCPE_KFLAG >> 1)   /* not really, but this will do */


/* Redefinition of the modem signal type */

typedef RS232_SIGNAL TMCKT;                     /* declare a local name for the RS-232 signals type */


/* Global extension routines */

t_stat ex_sim_putchar   (int32 c);
t_stat ex_sim_putchar_s (int32 c);
t_stat ex_sim_poll_kbd  (void);
uint32 ex_sim_brk_test  (t_addr location, uint32 type);

t_stat ex_tmxr_attach_unit   (TMXR *mp, UNIT *pptr, UNIT *uptr, char *cptr);
t_stat ex_tmxr_detach_unit   (TMXR *mp, UNIT *pptr, UNIT *uptr);
t_stat ex_tmxr_detach_line   (TMXR *mp, UNIT *uptr);
t_stat ex_tmxr_control_line  (TMLN *lp, TMCKT control);
TMCKT  ex_tmxr_line_status   (TMLN *lp);
int32  ex_tmxr_poll_conn     (TMXR *mp);
t_bool ex_tmxr_line_free     (TMLN *lp);
t_bool ex_tmxr_mux_free      (TMXR *mp);


extern void   (*vm_sim_vm_init) (void);
extern CTAB   *vm_sim_vm_cmd;
extern t_stat vm_sim_instr      (void);

extern UNIT *vm_console_input_unit;             /* console input unit pointer */
extern UNIT *vm_console_output_unit;            /* console output unit pointer */
