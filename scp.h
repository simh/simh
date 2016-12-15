/* scp.h: simulator control program headers

   Copyright (c) 1993-2009, Robert M Supnik

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

#ifdef  __cplusplus
extern "C" {
#endif

/* run_cmd parameters */

#define RU_RUN          0                               /* run */
#define RU_GO           1                               /* go */
#define RU_STEP         2                               /* step */
#define RU_NEXT         3                               /* step or step/over */
#define RU_CONT         4                               /* continue */
#define RU_BOOT         5                               /* boot */

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

/* Command processors */

t_stat reset_cmd (int32 flag, CONST char *ptr);
t_stat exdep_cmd (int32 flag, CONST char *ptr);
t_stat eval_cmd (int32 flag, CONST char *ptr);
t_stat load_cmd (int32 flag, CONST char *ptr);
t_stat run_cmd (int32 flag, CONST char *ptr);
void run_cmd_message (const char *unechod_cmdline, t_stat r);
t_stat attach_cmd (int32 flag, CONST char *ptr);
t_stat detach_cmd (int32 flag, CONST char *ptr);
t_stat assign_cmd (int32 flag, CONST char *ptr);
t_stat deassign_cmd (int32 flag, CONST char *ptr);
t_stat save_cmd (int32 flag, CONST char *ptr);
t_stat restore_cmd (int32 flag, CONST char *ptr);
t_stat exit_cmd (int32 flag, CONST char *ptr);
t_stat set_cmd (int32 flag, CONST char *ptr);
t_stat show_cmd (int32 flag, CONST char *ptr);
t_stat set_default_cmd (int32 flg, CONST char *cptr);
t_stat pwd_cmd (int32 flg, CONST char *cptr);
t_stat dir_cmd (int32 flg, CONST char *cptr);
t_stat type_cmd (int32 flg, CONST char *cptr);
t_stat brk_cmd (int32 flag, CONST char *ptr);
t_stat do_cmd (int32 flag, CONST char *ptr);
t_stat goto_cmd (int32 flag, CONST char *ptr);
t_stat return_cmd (int32 flag, CONST char *ptr);
t_stat shift_cmd (int32 flag, CONST char *ptr);
t_stat call_cmd (int32 flag, CONST char *ptr);
t_stat on_cmd (int32 flag, CONST char *ptr);
t_stat noop_cmd (int32 flag, CONST char *ptr);
t_stat assert_cmd (int32 flag, CONST char *ptr);
t_stat send_cmd (int32 flag, CONST char *ptr);
t_stat expect_cmd (int32 flag, CONST char *ptr);
t_stat help_cmd (int32 flag, CONST char *ptr);
t_stat screenshot_cmd (int32 flag, CONST char *ptr);
t_stat spawn_cmd (int32 flag, CONST char *ptr);
t_stat echo_cmd (int32 flag, CONST char *ptr);

/* Allow compiler to help validate printf style format arguments */
#if !defined __GNUC__
#define GCC_FMT_ATTR(n, m)
#endif
#if !defined(GCC_FMT_ATTR)
#define GCC_FMT_ATTR(n, m) __attribute__ ((format (__printf__, n, m)))
#endif

/* Utility routines */

t_stat sim_process_event (void);
t_stat sim_activate (UNIT *uptr, int32 interval);
t_stat _sim_activate (UNIT *uptr, int32 interval);
t_stat sim_activate_abs (UNIT *uptr, int32 interval);
t_stat sim_activate_notbefore (UNIT *uptr, int32 rtime);
t_stat sim_activate_after (UNIT *uptr, uint32 usecs_walltime);
t_stat _sim_activate_after (UNIT *uptr, uint32 usecs_walltime);
t_stat sim_activate_after_abs (UNIT *uptr, uint32 usecs_walltime);
t_stat _sim_activate_after_abs (UNIT *uptr, uint32 usecs_walltime);
t_stat sim_cancel (UNIT *uptr);
t_bool sim_is_active (UNIT *uptr);
int32 sim_activate_time (UNIT *uptr);
t_stat sim_run_boot_prep (int32 flag);
double sim_gtime (void);
uint32 sim_grtime (void);
int32 sim_qcount (void);
t_stat attach_unit (UNIT *uptr, CONST char *cptr);
t_stat detach_unit (UNIT *uptr);
t_stat assign_device (DEVICE *dptr, const char *cptr);
t_stat deassign_device (DEVICE *dptr);
t_stat reset_all (uint32 start_device);
t_stat reset_all_p (uint32 start_device);
const char *sim_dname (DEVICE *dptr);
const char *sim_uname (UNIT *dptr);
t_stat get_yn (const char *ques, t_stat deflt);
int sim_isspace (char c);
int sim_islower (char c);
int sim_isalpha (char c);
int sim_isprint (char c);
int sim_isdigit (char c);
int sim_isgraph (char c);
int sim_isalnum (char c);
int sim_strncasecmp (const char *string1, const char *string2, size_t len);
CONST char *get_sim_opt (int32 opt, CONST char *cptr, t_stat *st);
const char *put_switches (char *buf, size_t bufsize, uint32 sw);
CONST char *get_glyph (const char *iptr, char *optr, char mchar);
CONST char *get_glyph_nc (const char *iptr, char *optr, char mchar);
CONST char *get_glyph_quoted (const char *iptr, char *optr, char mchar);
CONST char *get_glyph_cmd (const char *iptr, char *optr);
t_value get_uint (const char *cptr, uint32 radix, t_value max, t_stat *status);
CONST char *get_range (DEVICE *dptr, CONST char *cptr, t_addr *lo, t_addr *hi,
    uint32 rdx, t_addr max, char term);
t_stat sim_decode_quoted_string (const char *iptr, uint8 *optr, uint32 *osize);
char *sim_encode_quoted_string (const uint8 *iptr, uint32 size);
void fprint_buffer_string (FILE *st, const uint8 *buf, uint32 size);
t_value strtotv (CONST char *cptr, CONST char **endptr, uint32 radix);
int Fprintf (FILE *f, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
t_stat sim_set_memory_load_file (const unsigned char *data, size_t size);
int Fgetc (FILE *f);
t_stat fprint_val (FILE *stream, t_value val, uint32 rdx, uint32 wid, uint32 fmt);
t_stat sprint_val (char *buf, t_value val, uint32 rdx, uint32 wid, uint32 fmt);
t_stat sim_print_val (t_value val, uint32 radix, uint32 width, uint32 format);
const char *sim_fmt_secs (double seconds);
const char *sim_fmt_numeric (double number);
const char *sprint_capac (DEVICE *dptr, UNIT *uptr);
char *read_line (char *cptr, int32 size, FILE *stream);
void fprint_reg_help (FILE *st, DEVICE *dptr);
void fprint_set_help (FILE *st, DEVICE *dptr);
void fprint_show_help (FILE *st, DEVICE *dptr);
CTAB *find_cmd (const char *gbuf);
DEVICE *find_dev (const char *ptr);
DEVICE *find_unit (const char *ptr, UNIT **uptr);
DEVICE *find_dev_from_unit (UNIT *uptr);
t_stat sim_register_internal_device (DEVICE *dptr);
void sim_sub_args (char *in_str, size_t in_str_size, char *do_arg[]);
REG *find_reg (CONST char *ptr, CONST char **optr, DEVICE *dptr);
CTAB *find_ctab (CTAB *tab, const char *gbuf);
C1TAB *find_c1tab (C1TAB *tab, const char *gbuf);
SHTAB *find_shtab (SHTAB *tab, const char *gbuf);
t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr);
BRKTAB *sim_brk_fnd (t_addr loc);
uint32 sim_brk_test (t_addr bloc, uint32 btyp);
void sim_brk_clrspc (uint32 spc, uint32 btyp);
void sim_brk_npc (uint32 cnt);
void sim_brk_setact (const char *action);
const char *sim_brk_message(void);
t_stat sim_send_input (SEND *snd, uint8 *data, size_t size, uint32 after, uint32 delay);
t_stat sim_show_send_input (FILE *st, const SEND *snd);
t_bool sim_send_poll_data (SEND *snd, t_stat *stat);
t_stat sim_send_clear (SEND *snd);
t_stat sim_set_expect (EXPECT *exp, CONST char *cptr);
t_stat sim_set_noexpect (EXPECT *exp, const char *cptr);
t_stat sim_exp_set (EXPECT *exp, const char *match, int32 cnt, uint32 after, int32 switches, const char *act);
t_stat sim_exp_clr (EXPECT *exp, const char *match);
t_stat sim_exp_clrall (EXPECT *exp);
t_stat sim_exp_show (FILE *st, CONST EXPECT *exp, const char *match);
t_stat sim_exp_showall (FILE *st, const EXPECT *exp);
t_stat sim_exp_check (EXPECT *exp, uint8 data);
CONST char *match_ext (CONST char *fnam, const char *ext);
t_stat show_version (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat set_dev_debug (DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
t_stat show_dev_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char *sim_error_text (t_stat stat);
t_stat sim_string_to_stat (const char *cptr, t_stat *cond);
t_stat sim_cancel_step (void);
void sim_printf (const char *fmt, ...) GCC_FMT_ATTR(1, 2);
void sim_perror (const char *msg);
t_stat sim_messagef (t_stat stat, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
void sim_data_trace(DEVICE *dptr, UNIT *uptr, const uint8 *data, const char *position, size_t len, const char *txt, uint32 reason);
void sim_debug_bits_hdr (uint32 dbits, DEVICE* dptr, const char *header, 
    BITFIELD* bitdefs, uint32 before, uint32 after, int terminate);
void sim_debug_bits (uint32 dbits, DEVICE* dptr, BITFIELD* bitdefs,
    uint32 before, uint32 after, int terminate);
#if defined (__DECC) && defined (__VMS) && (defined (__VAX) || (__DECC_VER < 60590001))
#define CANT_USE_MACRO_VA_ARGS 1
#endif
#if defined(__cplusplus)
#ifdef CANT_USE_MACRO_VA_ARGS
#define _sim_debug sim_debug
void sim_debug (uint32 dbits, void* dptr, const char *fmt, ...) GCC_FMT_ATTR(3, 4);
#else
void _sim_debug (uint32 dbits, void* dptr, const char *fmt, ...) GCC_FMT_ATTR(3, 4);
#define sim_debug(dbits, dptr, ...) do { if (sim_deb && dptr && ((dptr)->dctrl & dbits)) _sim_debug (dbits, dptr, __VA_ARGS__);} while (0)
#endif
#else
#ifdef CANT_USE_MACRO_VA_ARGS
#define _sim_debug sim_debug
void sim_debug (uint32 dbits, DEVICE* dptr, const char *fmt, ...) GCC_FMT_ATTR(3, 4);
#else
void _sim_debug (uint32 dbits, DEVICE* dptr, const char *fmt, ...) GCC_FMT_ATTR(3, 4);
#define sim_debug(dbits, dptr, ...) do { if (sim_deb && dptr && ((dptr)->dctrl & dbits)) _sim_debug (dbits, dptr, __VA_ARGS__);} while (0)
#endif
#endif
void fprint_stopped_gen (FILE *st, t_stat v, REG *pc, DEVICE *dptr);
#define SCP_HELP_FLAT   (1u << 31)       /* Force flat help when prompting is not possible */
#define SCP_HELP_ONECMD (1u << 30)       /* Display one topic, do not prompt */
#define SCP_HELP_ATTACH (1u << 29)       /* Top level topic is ATTACH help */
t_stat scp_help (FILE *st, DEVICE *dptr,
                 UNIT *uptr, int32 flag, const char *help, const char *cptr, ...);
t_stat scp_vhelp (FILE *st, DEVICE *dptr,
                  UNIT *uptr, int32 flag, const char *help, const char *cptr, va_list ap);
t_stat scp_helpFromFile (FILE *st, DEVICE *dptr,
                         UNIT *uptr, int32 flag, const char *help, const char *cptr, ...);
t_stat scp_vhelpFromFile (FILE *st, DEVICE *dptr,
                          UNIT *uptr, int32 flag, const char *help, const char *cptr, va_list ap);

/* Global data */

extern DEVICE *sim_dflt_dev;
extern int32 sim_interval;
extern int32 sim_switches;
extern int32 sim_quiet;
extern int32 sim_step;
extern t_stat sim_last_cmd_stat;                        /* Command Status */
extern FILE *sim_log;                                   /* log file */
extern FILEREF *sim_log_ref;                            /* log file file reference */
extern FILE *sim_deb;                                   /* debug file */
extern FILEREF *sim_deb_ref;                            /* debug file file reference */
extern int32 sim_deb_switches;                          /* debug display flags */
extern struct timespec sim_deb_basetime;                /* debug base time for relative time output */
extern DEVICE **sim_internal_devices;
extern uint32 sim_internal_device_count;
extern UNIT *sim_clock_queue;
extern int32 sim_is_running;
extern t_bool sim_processing_event;                     /* Called from sim_process_event */
extern char *sim_prompt;                                /* prompt string */
extern const char *sim_savename;                        /* Simulator Name used in Save/Restore files */
extern t_value *sim_eval;
extern volatile int32 stop_cpu;
extern uint32 sim_brk_types;                            /* breakpoint info */
extern uint32 sim_brk_dflt;
extern uint32 sim_brk_summ;
extern uint32 sim_brk_match_type;
extern t_addr sim_brk_match_addr;
extern BRKTYPTAB *sim_brk_type_desc;                      /* type descriptions */
extern FILE *stdnul;
extern t_bool sim_asynch_enabled;
#if defined(SIM_ASYNCH_IO)
int sim_aio_update_queue (void);
void sim_aio_activate (ACTIVATE_API caller, UNIT *uptr, int32 event_time);
#endif

/* VM interface */

extern char sim_name[];
extern DEVICE *sim_devices[];
extern REG *sim_PC;
extern const char *sim_stop_messages[];
extern t_stat sim_instr (void);
extern t_stat sim_load (FILE *ptr, CONST char *cptr, CONST char *fnam, int flag);
extern int32 sim_emax;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);
extern t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val,
    int32 sw);

/* The per-simulator init routine is a weak global that defaults to NULL
   The other per-simulator pointers can be overrriden by the init routine */

WEAK extern void (*sim_vm_init) (void);
extern char *(*sim_vm_read) (char *ptr, int32 size, FILE *stream);
extern void (*sim_vm_post) (t_bool from_scp);
extern CTAB *sim_vm_cmd;
extern void (*sim_vm_sprint_addr) (char *buf, DEVICE *dptr, t_addr addr);
extern void (*sim_vm_fprint_addr) (FILE *st, DEVICE *dptr, t_addr addr);
extern t_addr (*sim_vm_parse_addr) (DEVICE *dptr, CONST char *cptr, CONST char **tptr);
extern t_bool (*sim_vm_fprint_stopped) (FILE *st, t_stat reason);
extern t_value (*sim_vm_pc_value) (void);
extern t_bool (*sim_vm_is_subroutine_call) (t_addr **ret_addrs);

#ifdef  __cplusplus
}
#endif

#endif
