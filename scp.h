/* sim_timer.h: simulator timer library headers

   Copyright (c) 1993-2004, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   14-Feb-04	RMS	Added debug prototypes (from Dave Hittner)
   02-Jan-04	RMS	Split out from SCP
*/

#ifndef _SIM_SCP_H_
#define _SIM_SCP_H_	0

t_stat sim_process_event (void);
t_stat sim_activate (UNIT *uptr, int32 interval);
t_stat sim_cancel (UNIT *uptr);
int32 sim_is_active (UNIT *uptr);
double sim_gtime (void);
uint32 sim_grtime (void);
int32 sim_qcount (void);
t_stat attach_unit (UNIT *uptr, char *cptr);
t_stat detach_unit (UNIT *uptr);
t_stat reset_all (uint32 start_device);
char *sim_dname (DEVICE *dptr);
t_stat get_yn (char *ques, t_stat deflt);
char *get_glyph (char *iptr, char *optr, char mchar);
char *get_glyph_nc (char *iptr, char *optr, char mchar);
t_value get_uint (char *cptr, uint32 radix, t_value max, t_stat *status);
char *get_range (DEVICE *dptr, char *cptr, t_addr *lo, t_addr *hi,
	uint32 rdx, t_addr max, char term);
t_stat get_ipaddr (char *cptr, uint32 *ipa, uint32 *ipp);
t_value strtotv (char *cptr, char **endptr, uint32 radix);
t_stat fprint_val (FILE *stream, t_value val, uint32 rdx, uint32 wid, uint32 fmt);
DEVICE *find_dev_from_unit (UNIT *uptr);
REG *find_reg (char *ptr, char **optr, DEVICE *dptr);
BRKTAB *sim_brk_fnd (t_addr loc);
t_bool sim_brk_test (t_addr bloc, int32 btyp);
char *match_ext (char *fnam, char *ext);
t_stat sim_cancel_step (void);
void sim_debug_u16 (uint32 dbits, DEVICE* dptr, const char* const* bitdefs,
	uint16 before, uint16 after, int terminate);
void sim_debug (uint32 dbits, DEVICE* dptr, const char* fmt, ...);

#endif
