/* scp.h: simulator control program headers

   Copyright (c) 1993-2019, Robert M Supnik

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

   09-Oct-19    JDB     Added "detach_all" global declaration
   19-Jul-19    JDB     Added "sim_get_radix" extension hook
   13-Apr-19    JDB     Added extension hooks
                        Added "sim_prog_name" and "sim_ref_type" global variables
                        Added global routine declarations
   19-Mar-18    RMS     Added sim_trim_endspc
   06-Mar-18    RMS     Moved switch routine declaration from scp.c
                        Removed get_ipaddr declaration
   28-Dec-14    JDB     [4.0] Moved sim_load and sim_emax declarations from scp.c
   14-Dec-14    JDB     [4.0] Added sim_activate_time
                        [4.0] Changed sim_is_active to return t_bool
                        [4.0] Added data externals
   02-Jul-14    JDB     [4.0] Added sim_error_text
   26-Feb-13    JDB     [4.0] Moved EX_* and SSH_* constants from scp.c
   05-Dec-10    MP      Added macro invocation of sim_debug 
   09-Aug-06    JDB     Added assign_device and deassign_device
   14-Jul-06    RMS     Added sim_activate_abs
   06-Jan-06    RMS     Added fprint_stopped_gen
                        Changed arg type in sim_brk_test
   07-Feb-05    RMS     Added ASSERT command
   09-Sep-04    RMS     Added reset_all_p
   14-Feb-04    RMS     Added debug prototypes (from Dave Hittner)
   02-Jan-04    RMS     Split out from SCP
*/

#ifndef SIM_SCP_H_
#define SIM_SCP_H_     0

/* run_cmd parameters */

#define RU_RUN          0                               /* run */
#define RU_GO           1                               /* go */
#define RU_STEP         2                               /* step */
#define RU_CONT         3                               /* continue */
#define RU_BOOT         4                               /* boot */

/* exdep_cmd parameters */

#define EX_D            0                               /* deposit */
#define EX_E            1                               /* examine */
#define EX_I            2                               /* interactive */

/* brk_cmd parameters */

#define SSH_ST          0                               /* set */
#define SSH_SH          1                               /* show */
#define SSH_CL          2                               /* clear */

/* get_sim_opt parameters */

#define CMD_OPT_SW      001                             /* switches */
#define CMD_OPT_OF      002                             /* output file */
#define CMD_OPT_SCH     004                             /* search */
#define CMD_OPT_DFT     010                             /* defaults */

/* sim_ref_type flags */

#define REF_NONE        000                             /* no reference type */
#define REF_DEVICE      001                             /* device reference */
#define REF_UNIT        002                             /* unit reference */

/* Command processors */

t_stat reset_cmd (int32 flag, char *ptr);
t_stat exdep_cmd (int32 flag, char *ptr);
t_stat eval_cmd (int32 flag, char *ptr);
t_stat load_cmd (int32 flag, char *ptr);
t_stat run_cmd (int32 flag, char *ptr);
t_stat attach_cmd (int32 flag, char *ptr);
t_stat detach_cmd (int32 flag, char *ptr);
t_stat assign_cmd (int32 flag, char *ptr);
t_stat deassign_cmd (int32 flag, char *ptr);
t_stat save_cmd (int32 flag, char *ptr);
t_stat restore_cmd (int32 flag, char *ptr);
t_stat exit_cmd (int32 flag, char *ptr);
t_stat set_cmd (int32 flag, char *ptr);
t_stat show_cmd (int32 flag, char *ptr);
t_stat brk_cmd (int32 flag, char *ptr);
t_stat do_cmd (int32 flag, char *ptr);
t_stat assert_cmd (int32 flag, char *ptr);
t_stat help_cmd (int32 flag, char *ptr);
t_stat spawn_cmd (int32 flag, char *ptr);
t_stat echo_cmd (int32 flag, char *ptr);

/* Utility routines */

t_stat sim_process_event (void);
t_stat sim_activate (UNIT *uptr, int32 interval);
t_stat sim_activate_abs (UNIT *uptr, int32 interval);
const char *sim_error_text (t_stat stat);
t_stat sim_cancel (UNIT *uptr);
t_bool sim_is_active (UNIT *uptr);
int32 sim_activate_time (UNIT *uptr);
double sim_gtime (void);
uint32 sim_grtime (void);
int32 sim_qcount (void);
t_stat attach_unit (UNIT *uptr, char *cptr);
t_stat detach_unit (UNIT *uptr);
t_stat detach_all (int32 start_device, t_bool shutdown);
t_stat assign_device (DEVICE *dptr, char *cptr);
t_stat deassign_device (DEVICE *dptr);
t_stat reset_all (uint32 start_device);
t_stat reset_all_p (uint32 start_device);
const char *sim_dname (DEVICE *dptr);
const char *sim_uname (UNIT *uptr);
t_stat get_yn (char *ques, t_stat deflt);
char *sim_trim_endspc(char *cptr);
int sim_isspace (char c);
int sim_islower (char c);
int sim_isalpha (char c);
int sim_isprint (char c);
int sim_isdigit (char c);
int sim_isgraph (char c);
int sim_isalnum (char c);
int sim_strncasecmp (const char *string1, const char *string2, size_t len);
int sim_strcasecmp (const char *string1, const char *string2);
size_t sim_strlcat (char *dst, const char *src, size_t size);
size_t sim_strlcpy (char *dst, const char *src, size_t size);
#ifndef strlcpy
#define strlcpy(dst, src, size) sim_strlcpy((dst), (src), (size))
#endif
#ifndef strlcat
#define strlcat(dst, src, size) sim_strlcat((dst), (src), (size))
#endif
#ifndef strncasecmp
#define strncasecmp(str1, str2, len) sim_strncasecmp((str1), (str2), (len))
#endif
#ifndef strcasecmp
#define strcasecmp(str1, str2) sim_strcasecmp ((str1), (str2))
#endif
char *get_sim_opt (int32 opt, char *cptr, t_stat *st);
char *get_glyph (char *iptr, char *optr, char mchar);
char *get_glyph_nc (char *iptr, char *optr, char mchar);
t_value get_uint (char *cptr, uint32 radix, t_value max, t_stat *status);
char *get_range (DEVICE *dptr, char *cptr, t_addr *lo, t_addr *hi,
    uint32 rdx, t_addr max, char term);
t_value strtotv (char *cptr, char **endptr, uint32 radix);
t_stat fprint_val (FILE *stream, t_value val, uint32 rdx, uint32 wid, uint32 fmt);
CTAB *find_cmd (char *gbuf);
DEVICE *find_dev (char *ptr);
DEVICE *find_unit (char *ptr, UNIT **uptr);
DEVICE *find_dev_from_unit (UNIT *uptr);
REG *find_reg (char *ptr, char **optr, DEVICE *dptr);
CTAB *find_ctab (CTAB *tab, char *gbuf);
C1TAB *find_c1tab (C1TAB *tab, char *gbuf);
SHTAB *find_shtab (SHTAB *tab, char *gbuf);
BRKTAB *sim_brk_fnd (t_addr loc);
uint32 sim_brk_test (t_addr bloc, uint32 btyp);
void sim_brk_clrspc (uint32 spc);
char *match_ext (char *fnam, char *ext);
t_stat sim_cancel_step (void);
void sim_debug_u16 (uint32 dbits, DEVICE* dptr, const char* const* bitdefs,
    uint16 before, uint16 after, int terminate);
void sim_debug (uint32 dbits, DEVICE* dptr, const char* fmt, ...);
void fprint_stopped_gen (FILE *st, t_stat v, REG *pc, DEVICE *dptr);
void sim_printf (const char *fmt, ...);
t_stat sim_messagef (t_stat stat, const char *fmt, ...);
char *get_sim_sw(char *cptr);
t_stat sim_brk_clr (t_addr loc, int32 sw);
void sim_brk_clract (void);
char *sim_brk_getact (char *buf, int32 size);
char *read_line (char *ptr, int32 size, FILE *stream);

/* Global data */

extern DEVICE *sim_dflt_dev;
extern int32 sim_interval;
extern int32 sim_switches;
extern int32 sim_quiet;
extern int32 sim_step;
extern FILE *sim_log;                                   /* log file */
extern FILE *sim_deb;                                   /* debug file */
extern UNIT *sim_clock_queue;
extern int32 sim_is_running;
extern t_value *sim_eval;
extern volatile int32 stop_cpu;
extern uint32 sim_brk_types;                            /* breakpoint info */
extern uint32 sim_brk_dflt;
extern uint32 sim_brk_summ;
extern char *sim_brk_act;                               /* breakpoint actions pointer */
extern char *sim_prog_name;                             /* executable program name */
extern uint32 sim_ref_type;                             /* reference type */

/* Extension interface */

extern char *sim_vm_release;
extern void (*sub_args) (char *iptr, char *optr, int32 len, char *args []);
extern int32 (*sim_get_radix) (const char *cptr, int32 switches, int32 default_radix);

/* VM interface */

extern char sim_name[];
extern DEVICE *sim_devices[];
extern REG *sim_PC;
extern const char *sim_stop_messages[];
extern t_stat sim_instr (void);
extern t_stat sim_load (FILE *ptr, char *cptr, char *fnam, int flag);
extern int32 sim_emax;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);
extern t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val,
    int32 sw);

/* The per-simulator init routine is a weak global that defaults to NULL
   The other per-simulator pointers can be overrriden by the init routine */

extern void (*sim_vm_init) (void);
extern char* (*sim_vm_read) (char *ptr, int32 size, FILE *stream);
extern void (*sim_vm_post) (t_bool from_scp);
extern CTAB *sim_vm_cmd;
extern void (*sim_vm_fprint_addr) (FILE *st, DEVICE *dptr, t_addr addr);
extern t_addr (*sim_vm_parse_addr) (DEVICE *dptr, char *cptr, char **tptr);
extern t_bool (*sim_vm_fprint_stopped) (FILE *st, t_stat reason);

/* vsnprintf hassles for various compilers - missing in old DEC C */

#if defined (__DECC) && defined (__VMS) && (defined (__VAX) || (__CRTL_VER <= 70311000))

#define VSNPRINTF(a,b,c,d)      vsprintf (a, c, d)
#define STACKBUFSIZE            16384

#else

#define VSNPRINTF(a,b,c,d)      vsnprintf (a, b, c, d)
#define STACKBUFSIZE            2048

#endif

/* V4 compatibility */

#define sim_activate_time(u)    sim_is_active (u)
void sim_perror (char *msg);
extern t_bool (*sim_vm_is_subroutine_call) (t_addr **ret_addrs);

#endif
