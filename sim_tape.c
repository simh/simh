/* sim_tape.c: simulator tape support library

   Copyright (c) 1993-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Ultimately, this will be a place to hide processing of various tape formats,
   as well as OS-specific direct hardware access.

   23-Jan-12    MP      Added support for Logical EOT detection while positioning
   05-Feb-11    MP      Refactored to prepare for SIM_ASYNC_IO support
                        Added higher level routines:
                            sim_tape_wreomrw    - erase remainder of tape & rewind
                            sim_tape_sprecsf    - skip records
                            sim_tape_spfilef    - skip files
                            sim_tape_sprecsr    - skip records rev
                            sim_tape_spfiler    - skip files rev
                            sim_tape_position   - general purpose position
                        These routines correspond to natural tape operations 
                        and will align better when physical tape support is 
                        included here.
   08-Jun-08    JDB     Fixed signed/unsigned warning in sim_tape_set_fmt
   23-Jan-07    JDB     Fixed backspace over gap at BOT
   22-Jan-07    RMS     Fixed bug in P7B format read reclnt rev (found by Rich Cornwell)
   15-Dec-06    RMS     Added support for small capacity tapes
   30-Aug-06    JDB     Added erase gap support
   14-Feb-06    RMS     Added variable tape capacity
   23-Jan-06    JDB     Fixed odd-byte-write problem in sim_tape_wrrecf
   17-Dec-05    RMS     Added write support for Paul Pierce 7b format
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-May-05    RMS     Added support for Pierce 7b format
   28-Jul-04    RMS     Fixed bug in writing error records (found by Dave Bryan)
                RMS     Fixed incorrect error codes (found by Dave Bryan)
   05-Jan-04    RMS     Revised for file I/O library
   25-Apr-03    RMS     Added extended file support
   28-Mar-03    RMS     Added E11 and TPC format support

   Public routines:

   sim_tape_attach      attach tape unit
   sim_tape_detach      detach tape unit
   sim_tape_attach_help help routine for attaching tapes
   sim_tape_rdrecf      read tape record forward
   sim_tape_rdrecr      read tape record reverse
   sim_tape_wrrecf      write tape record forward
   sim_tape_sprecf      space tape record forward
   sim_tape_sprecr      space tape record reverse
   sim_tape_wrtmk       write tape mark
   sim_tape_wreom       erase remainder of tape
   sim_tape_wreomrw     erase remainder of tape & rewind
   sim_tape_wrgap       write erase gap
   sim_tape_errecf      erase record forward
   sim_tape_errecr      erase record reverse
   sim_tape_sprecsf     space records forward
   sim_tape_spfilef     space files forward 
   sim_tape_sprecsr     space records reverse
   sim_tape_spfiler     space files reverse
   sim_tape_position    generalized position
   sim_tape_rewind      rewind
   sim_tape_reset       reset unit
   sim_tape_bot         TRUE if at beginning of tape
   sim_tape_eot         TRUE if at or beyond end of tape
   sim_tape_wrp         TRUE if write protected
   sim_tape_set_fmt     set tape format
   sim_tape_show_fmt    show tape format
   sim_tape_set_capac   set tape capacity
   sim_tape_show_capac  show tape capacity
   sim_tape_set_dens    set tape density
   sim_tape_show_dens   show tape density
   sim_tape_set_async   enable asynchronous operation
   sim_tape_clr_async   disable asynchronous operation
   aim_tape_test        unit test routine

*/

#include "sim_defs.h"
#include "sim_tape.h"
#include <ctype.h>

#if defined SIM_ASYNCH_IO
#include <pthread.h>
#endif

static struct sim_tape_fmt {
    const char          *name;                          /* name */
    int32               uflags;                         /* unit flags */
    t_addr              bot;                            /* bot test */
    t_addr              eom_remnant;                    /* potentially unprocessed data */
    } fmts[] = {
    { "SIMH",       0,       sizeof (t_mtrlnt) - 1, sizeof (t_mtrlnt) },
    { "E11",        0,       sizeof (t_mtrlnt) - 1, sizeof (t_mtrlnt) },
    { "TPC",        UNIT_RO, sizeof (t_tpclnt) - 1, sizeof (t_tpclnt) },
    { "P7B",        0,       0,                     0                 },
    { "AWS",        0,       0,                     0                 },
    { "TAR",        UNIT_RO, 0,                     0                 },
    { "ANSI",       UNIT_RO, 0,                     0                 },
    { "FIXED",      UNIT_RO, 0,                     0                 },
    { NULL,         0,       0,                     0                 }
    };

static const uint32 bpi [] = {                          /* tape density table, indexed by MT_DENS constants */
    0,                                                  /*   0 = MT_DENS_NONE -- density not set */
    200,                                                /*   1 = MT_DENS_200  -- 200 bpi NRZI */
    556,                                                /*   2 = MT_DENS_556  -- 556 bpi NRZI */
    800,                                                /*   3 = MT_DENS_800  -- 800 bpi NRZI */
    1600,                                               /*   4 = MT_DENS_1600 -- 1600 bpi PE */
    6250                                                /*   5 = MT_DENS_6250 -- 6250 bpi GCR */
    };

#define BPI_COUNT       (sizeof (bpi) / sizeof (bpi [0]))   /* count of density table entries */

static t_stat sim_tape_ioerr (UNIT *uptr);
static t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat);
static t_stat sim_tape_aws_wrdata (UNIT *uptr, uint8 *buf, t_mtrlnt bc);
static uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map, uint32 mapsize);
static t_stat sim_tape_validate_tape (UNIT *uptr);
static t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map);
static void sim_tape_data_trace (UNIT *uptr, const uint8 *data, size_t len, const char* txt, int detail, uint32 reason);
static t_stat tape_erase_fwd (UNIT *uptr, t_mtrlnt gap_size);
static t_stat tape_erase_rev (UNIT *uptr, t_mtrlnt gap_size);

struct tape_context {
    DEVICE              *dptr;              /* Device for unit (access to debug flags) */
    uint32              dbit;               /* debugging bit for trace */
    uint32              auto_format;        /* Format determined dynamically */
#if defined SIM_ASYNCH_IO
    int                 asynch_io;          /* Asynchronous Interrupt scheduling enabled */
    int                 asynch_io_latency;  /* instructions to delay pending interrupt */
    pthread_mutex_t     lock;
    pthread_t           io_thread;          /* I/O Thread Id */
    pthread_mutex_t     io_lock;
    pthread_cond_t      io_cond;
    pthread_cond_t      io_done;
    pthread_cond_t      startup_cond;
    int                 io_top;
    uint8               *buf;
    uint32              *bc;
    uint32              *fc;
    uint32              vbc;
    uint32              max;
    uint32              gaplen;
    uint32              bpi;
    uint32              *objupdate;
    TAPE_PCALLBACK      callback;
    t_stat              io_status;
#endif
    };
#define tape_ctx up8                        /* Field in Unit structure which points to the tape_context */

#if defined SIM_ASYNCH_IO
#define AIO_CALLSETUP                                                   \
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;       \
                                                                        \
if (ctx == NULL)                                                        \
    return sim_messagef (SCPE_IERR, "Bad Attach\n");                    \
if ((!callback) || !ctx->asynch_io)

#define AIO_CALL(op, _buf, _bc, _fc, _max, _vbc, _gaplen, _bpi, _obj, _callback)\
    if (ctx->asynch_io) {                                               \
        struct tape_context *ctx =                                      \
                      (struct tape_context *)uptr->tape_ctx;            \
                                                                        \
        pthread_mutex_lock (&ctx->io_lock);                             \
                                                                        \
        sim_debug_unit (ctx->dbit, uptr,                                \
      "sim_tape AIO_CALL(op=%d, unit=%d)\n", op, (int)(uptr-ctx->dptr->units));\
                                                                        \
        if (ctx->callback)                                              \
            abort(); /* horrible mistake, stop */                       \
        ctx->io_top = op;                                               \
        ctx->buf = _buf;                                                \
        ctx->bc = _bc;                                                  \
        ctx->fc = _fc;                                                  \
        ctx->max = _max;                                                \
        ctx->vbc = _vbc;                                                \
        ctx->gaplen = _gaplen;                                          \
        ctx->bpi = _bpi;                                                \
        ctx->objupdate = _obj;                                          \
        ctx->callback = _callback;                                      \
        pthread_cond_signal (&ctx->io_cond);                            \
        pthread_mutex_unlock (&ctx->io_lock);                           \
        }                                                               \
    else                                                                \
        if (_callback)                                                  \
            (_callback) (uptr, r);
#define TOP_DONE  0             /* close */
#define TOP_RDRF  1             /* sim_tape_rdrecf_a */
#define TOP_RDRR  2             /* sim_tape_rdrecr_a */
#define TOP_WREC  3             /* sim_tape_wrrecf_a */
#define TOP_WTMK  4             /* sim_tape_wrtmk_a */
#define TOP_WEOM  5             /* sim_tape_wreom_a */
#define TOP_WEMR  6             /* sim_tape_wreomrw_a */
#define TOP_WGAP  7             /* sim_tape_wrgap_a */
#define TOP_SPRF  8             /* sim_tape_sprecf_a */
#define TOP_SRSF  9             /* sim_tape_sprecsf_a */
#define TOP_SPRR 10             /* sim_tape_sprecr_a */
#define TOP_SRSR 11             /* sim_tape_sprecsr_a */
#define TOP_SPFF 12             /* sim_tape_spfilef */
#define TOP_SFRF 13             /* sim_tape_spfilebyrecf */
#define TOP_SPFR 14             /* sim_tape_spfiler */
#define TOP_SFRR 15             /* sim_tape_spfilebyrecr */
#define TOP_RWND 16             /* sim_tape_rewind_a */
#define TOP_POSN 17             /* sim_tape_position_a */

static void *
_tape_io(void *arg)
{
UNIT* volatile uptr = (UNIT*)arg;
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

    /* Boost Priority for this I/O thread vs the CPU instruction execution 
       thread which in general won't be readily yielding the processor when 
       this thread needs to run */
    sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

    sim_debug_unit (ctx->dbit, uptr, "_tape_io(unit=%d) starting\n", (int)(uptr-ctx->dptr->units));

    pthread_mutex_lock (&ctx->io_lock);
    pthread_cond_signal (&ctx->startup_cond);   /* Signal we're ready to go */
    while (1) {
        pthread_cond_wait (&ctx->io_cond, &ctx->io_lock);
        if (ctx->io_top == TOP_DONE)
            break;
        pthread_mutex_unlock (&ctx->io_lock);
        switch (ctx->io_top) {
            case TOP_RDRF:
                ctx->io_status = sim_tape_rdrecf (uptr, ctx->buf, ctx->bc, ctx->max);
                break;
            case TOP_RDRR:
                ctx->io_status = sim_tape_rdrecr (uptr, ctx->buf, ctx->bc, ctx->max);
                break;
            case TOP_WREC:
                ctx->io_status = sim_tape_wrrecf (uptr, ctx->buf, ctx->vbc);
                break;
            case TOP_WTMK:
                ctx->io_status = sim_tape_wrtmk (uptr);
                break;
            case TOP_WEOM:
                ctx->io_status = sim_tape_wreom (uptr);
                break;
            case TOP_WEMR:
                ctx->io_status = sim_tape_wreomrw (uptr);
                break;
            case TOP_WGAP:
                ctx->io_status = sim_tape_wrgap (uptr, ctx->gaplen);
                break;
            case TOP_SPRF:
                ctx->io_status = sim_tape_sprecf (uptr, ctx->bc);
                break;
            case TOP_SRSF:
                ctx->io_status = sim_tape_sprecsf (uptr, ctx->vbc, ctx->bc);
                break;
            case TOP_SPRR:
                ctx->io_status = sim_tape_sprecr (uptr, ctx->bc);
                break;
            case TOP_SRSR:
                ctx->io_status = sim_tape_sprecsr (uptr, ctx->vbc, ctx->bc);
                break;
            case TOP_SPFF:
                ctx->io_status = sim_tape_spfilef (uptr, ctx->vbc, ctx->bc);
                break;
            case TOP_SFRF:
                ctx->io_status = sim_tape_spfilebyrecf (uptr, ctx->vbc, ctx->bc, ctx->fc, ctx->max);
                break;
            case TOP_SPFR:
                ctx->io_status = sim_tape_spfiler (uptr, ctx->vbc, ctx->bc);
                break;
            case TOP_SFRR:
                ctx->io_status = sim_tape_spfilebyrecr (uptr, ctx->vbc, ctx->bc, ctx->fc);
                break;
            case TOP_RWND:
                ctx->io_status = sim_tape_rewind (uptr);
                break;
            case TOP_POSN:
                ctx->io_status = sim_tape_position (uptr, ctx->vbc, ctx->gaplen, ctx->bc, ctx->bpi, ctx->fc, ctx->objupdate);
                break;
            }
        pthread_mutex_lock (&ctx->io_lock);
        ctx->io_top = TOP_DONE;
        pthread_cond_signal (&ctx->io_done);
        sim_activate (uptr, ctx->asynch_io_latency);
    }
    pthread_mutex_unlock (&ctx->io_lock);

    sim_debug_unit (ctx->dbit, uptr, "_tape_io(unit=%d) exiting\n", (int)(uptr-ctx->dptr->units));

    return NULL;
}

/* This routine is called in the context of the main simulator thread before 
   processing events for any unit. It is only called when an asynchronous 
   thread has called sim_activate() to activate a unit.  The job of this 
   routine is to put the unit in proper condition to digest what may have
   occurred in the asynchronous thread.
   
   Since tape processing only handles a single I/O at a time to a 
   particular tape device, we have the opportunity to possibly detect 
   improper attempts to issue multiple concurrent I/O requests. */
static void _tape_completion_dispatch (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
TAPE_PCALLBACK callback = ctx->callback;

sim_debug_unit (ctx->dbit, uptr, "_tape_completion_dispatch(unit=%d, top=%d, callback=%p)\n", (int)(uptr-ctx->dptr->units), ctx->io_top, ctx->callback);

if (ctx->io_top != TOP_DONE)
    abort();                                            /* horribly wrong, stop */

if (ctx->asynch_io)
    pthread_mutex_lock (&ctx->io_lock);

if (ctx->callback) {
    ctx->callback = NULL;
    if (ctx->asynch_io)
        pthread_mutex_unlock (&ctx->io_lock);
    callback (uptr, ctx->io_status);
    }
else {
    if (ctx->asynch_io)
        pthread_mutex_unlock (&ctx->io_lock);
    }
}

static t_bool _tape_is_active (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_tape_is_active(unit=%d, top=%d)\n", (int)(uptr-ctx->dptr->units), ctx->io_top);
    return (ctx->io_top != TOP_DONE);
    }
return FALSE;
}

static t_bool _tape_cancel (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_tape_cancel(unit=%d, top=%d)\n", (int)(uptr-ctx->dptr->units), ctx->io_top);
    if (ctx->asynch_io) {
        pthread_mutex_lock (&ctx->io_lock);
        while (ctx->io_top != TOP_DONE)
            pthread_cond_wait (&ctx->io_done, &ctx->io_lock);
        pthread_mutex_unlock (&ctx->io_lock);
        }
    }
return FALSE;
}
#else
#define AIO_CALLSETUP                                                       \
    if (uptr->tape_ctx == NULL)                                             \
        return sim_messagef (SCPE_IERR, "Bad Attach\n");
#define AIO_CALL(op, _buf, _fc, _bc, _max, _vbc, _gaplen, _bpi, _obj, _callback) \
    if (_callback)                                                    \
        (_callback) (uptr, r);
#endif

typedef struct VOL1 {
    char type[3];               /* VOL  */
    char num;                   /* 1    */
    char ident[6];              /* <ansi <a> characters blank padded > */
    char accessibity;           /* blank */
    char reserved1[13];         /*      */
    char implement[13];         /*      */
    char owner[14];             /*      */
    char reserved2[28];         /*      */
    char standard;              /* 1,3 or 4  */
    } VOL1;

typedef struct HDR1 {       /* Also EOF1, EOV1 */
    char type[3];               /* HDR|EOF|EOV  */
    char num;                   /* 1    */
    char file_ident[17];        /* filename */
    char file_set[6];           /* label ident */
    char file_section[4];       /* 0001 */
    char file_sequence[4];      /* 0001 */
    char generation_number[4];  /* 0001 */
    char version_number[2];     /* 00 */
    char creation_date[6];      /* cyyddd */
    char expiration_date[6];
    char accessibility;         /* space */
    char block_count[6];        /* 000000 */
    char system_code[13];       /* */
    char reserved[7];           /* blank */
    } HDR1;

typedef struct HDR2 {       /* Also EOF2, EOV2 */
    char type[3];               /* HDR  */
    char num;                   /* 2    */
    char record_format;         /* F(fixed)|D(variable)|S(spanned) */
    char block_length[5];       /* label ident */
    char record_length[5];      /*  */
    char reserved_os1[21];      /* */
    char carriage_control;      /* A - Fortran CC, M - Record contained CC, space - CR/LF to be added */
    char reserved_os2[13];      /* */
    char buffer_offset[2];      /* */
    char reserved_std[28];      /* */
    } HDR2;

typedef struct HDR3 {       /* Also EOF3, EOV3 */
    char type[3];               /* HDR  */
    char num;                   /* 2    */
    char record_format;         /* F(fixed)|D(variable)|S(spanned) */
    char block_length[5];       /* label ident */
    char record_length[5];      /*  */
    char reserved_os[35];       /* */
    char buffer_offset[2];      /* */
    char reserved_std[28];      /* */
    } HDR3;

typedef struct HDR4 {       /* Also EOF4, EOV4 */
    char type[3];               /* HDR  */
    char num;                   /* 4    */
    char blank;                 /* blank */
    char extra_name[62];        /*  */
    char extra_name_used[2];    /* 99 */
    char unused[11];
    } HDR4;

typedef struct TAPE_RECORD {
    uint32 size;
    uint8 data[1];
    } TAPE_RECORD;

typedef struct MEMORY_TAPE {
    uint32 ansi_type;       /* ANSI-VMS, ANSI-RT11, ANSI-RSTS, ANSI-RSX11, etc. */
    uint32 file_count;      /* number of labeled files */
    uint32 record_count;    /* number of entries in the record array */
    uint32 array_size;      /* allocated size of records array */
    uint32 block_size;      /* tape block size */
    TAPE_RECORD **records;
    VOL1 vol1;
    } MEMORY_TAPE;

const char HDR3_RMS_STREAM[] = "HDR3020002040000" 
                               "0000000100000000" 
                               "0000000002000000" 
                               "0000000000000000" 
                               "0000            ";
const char HDR3_RMS_STMLF[] =  "HDR3020002050000" 
                               "0000000100000000" 
                               "0000000002000000" 
                               "0000000000000000" 
                               "0000            ";
const char HDR3_RMS_VAR[] =    "HDR3005C02020000" 
                               "0000000100000000" 
                               "0000000000000000" 
                               "0000000000000000" 
                               "0000            ";
const char HDR3_RMS_FIXED[] =  "HDR3020000010000" 
                               "0000000100000000" 
                               "0000000002000000" 
                               "0000000000000000" 
                               "0000            ";
const char HDR3_RMS_VARRSX[] = "HDR300000A020000" 
                               "0000000100000000" 
                               "0000000000000000" 
                               "0000000000000000" 
                               "0000            ";
const char HDR3_RMS_FIXRSX[] = "HDR3020008010000" 
                               "0000000100000000" 
                               "0000000000000000" 
                               "0000000000000000" 
                               "0000            ";

static struct ansi_tape_parameters {
    const char          *name;                  /* operating system */
    const char          *system_code;           /* */
    t_bool              nohdr2;                 /* no HDR2 records */
    t_bool              nohdr3;                 /* no HDR2 records */
    t_bool              fixed_text;             /*  */
    char                vol1_standard;          /* 3 or 4 */
    const char          *hdr3_fixed;            /* HDR3 template for Fixed format files */
    const char          *hdr3_lf_line_endings;  /* HDR3 template for text with LF line ending files */
    const char          *hdr3_crlf_line_endings;/* HDR3 template for text with CRLF line ending files */
    int                 skip_lf_line_endings;
    int                 skip_crlf_line_endings;
    t_bool              y2k_date_bug;
    t_bool              zero_record_length;
    char                record_format;
    char                carriage_control;
    } ansi_args[] = {     /* code       nohdr2 nohdr3 fixed_text lvl hdr3 fir fuxed    hdr3 fir lf      hdr3 for crlf  skLF CRLF Y2KDT  0RecLnt RFM  CC*/
        {"ANSI-VMS"      , "DECFILE11A", FALSE, FALSE, FALSE,    '3', HDR3_RMS_FIXED,  HDR3_RMS_STMLF,  HDR3_RMS_STREAM, 0,   0, FALSE, FALSE,   0,   0},
        {"ANSI-RSX11"    , "DECFILE11A", FALSE, FALSE, FALSE,    '4', HDR3_RMS_FIXRSX, HDR3_RMS_VARRSX, HDR3_RMS_VARRSX, 1,   2, FALSE, FALSE,   0,   0},
        {"ANSI-RT11"     , "DECRT11A",   TRUE,  TRUE,  TRUE,     '3', NULL,            NULL,            NULL,            0,   0, FALSE, FALSE,   0,   0},
        {"ANSI-RSTS"     , "DECRSTS/E",  FALSE, TRUE,  TRUE,     '3', NULL,            NULL,            NULL,            0,   0, TRUE,  TRUE,  'U', 'M'},
        {"ANSI-VAR"      , "DECRSTS/E",  FALSE, TRUE,  FALSE,    '3', NULL,            NULL,            NULL,            1,   2, TRUE,  FALSE, 'D', ' '},
        {NULL}
    };


static MEMORY_TAPE *ansi_create_tape (const char *label, uint32 block_size, uint32 ansi_type);
static MEMORY_TAPE *memory_create_tape (void);
static void memory_free_tape (void *vtape);
static void sim_tape_add_ansi_entry (const char *directory, 
                                     const char *filename,
                                     t_offset FileSize,
                                     const struct stat *filestat,
                                     void *context);
static t_bool memory_tape_add_block (MEMORY_TAPE *tape, uint8 *block, uint32 size);
static t_stat sim_export_tape (UNIT *uptr, const char *export_file);
static int tape_classify_file_contents (FILE *f, size_t *max_record_size, t_bool *lf_line_endings, t_bool *crlf_line_endings);


/* Enable asynchronous operation */

t_stat sim_tape_set_async (UNIT *uptr, int latency)
{
#if !defined(SIM_ASYNCH_IO)
return sim_messagef (SCPE_NOFNC, "Tape: can't operate asynchronously\r\n");
#else
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
pthread_attr_t attr;

ctx->asynch_io = sim_asynch_enabled;
ctx->asynch_io_latency = latency;
if (ctx->asynch_io) {
    pthread_mutex_init (&ctx->io_lock, NULL);
    pthread_cond_init (&ctx->io_cond, NULL);
    pthread_cond_init (&ctx->io_done, NULL);
    pthread_cond_init (&ctx->startup_cond, NULL);
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_mutex_lock (&ctx->io_lock);
    pthread_create (&ctx->io_thread, &attr, _tape_io, (void *)uptr);
    pthread_attr_destroy(&attr);
    pthread_cond_wait (&ctx->startup_cond, &ctx->io_lock); /* Wait for thread to stabilize */
    pthread_mutex_unlock (&ctx->io_lock);
    pthread_cond_destroy (&ctx->startup_cond);
    }
uptr->a_check_completion = _tape_completion_dispatch;
uptr->a_is_active = _tape_is_active;
uptr->cancel = _tape_cancel;
return SCPE_OK;
#endif
}

/* Disable asynchronous operation */

t_stat sim_tape_clr_async (UNIT *uptr)
{
#if !defined(SIM_ASYNCH_IO)
return SCPE_NOFNC;
#else
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

/* make sure device exists */
if (!ctx) return SCPE_UNATT;

if (ctx->asynch_io) {
    pthread_mutex_lock (&ctx->io_lock);
    ctx->asynch_io = 0;
    pthread_cond_signal (&ctx->io_cond);
    pthread_mutex_unlock (&ctx->io_lock);
    pthread_join (ctx->io_thread, NULL);
    pthread_mutex_destroy (&ctx->io_lock);
    pthread_cond_destroy (&ctx->io_cond);
    pthread_cond_destroy (&ctx->io_done);
    }
return SCPE_OK;
#endif
}

/* 
   This routine is called when the simulator stops and any time
   the asynch mode is changed (enabled or disabled)
*/
static void _sim_tape_io_flush (UNIT *uptr)
{
#if defined (SIM_ASYNCH_IO)
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

sim_tape_clr_async (uptr);
if (sim_asynch_enabled)
    sim_tape_set_async (uptr, ctx->asynch_io_latency);
#endif
if (MT_GET_FMT (uptr) < MTUF_F_ANSI)
    fflush (uptr->fileref);
}

static const char *_sim_tape_format_name (UNIT *uptr)
{
int32 f = MT_GET_FMT (uptr);

if (f == MTUF_F_ANSI)
    return ansi_args[MT_GET_ANSI_TYP (uptr)].name;
else
    return fmts[f].name;
}

/* Attach tape unit */

t_stat sim_tape_attach (UNIT *uptr, CONST char *cptr)
{
DEVICE *dptr;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
return sim_tape_attach_ex (uptr, cptr, ((dptr->flags & DEV_DEBUG) || (dptr->debflags)) ? MTSE_DBG_API : 0, 0);
}

t_stat sim_tape_attach_ex (UNIT *uptr, const char *cptr, uint32 dbit, int completion_delay)
{
struct tape_context *ctx;
uint32 objc;
DEVICE *dptr;
char gbuf[CBUFSIZE];
char export_file[CBUFSIZE] = "";
uint32 recsize = 0;
t_stat r;
t_bool auto_format = FALSE;
t_bool had_debug = (sim_deb != NULL);
uint32 starting_dctrl = uptr->dctrl;
int32 saved_switches = sim_switches;
MEMORY_TAPE *tape = NULL;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if (sim_switches & SWMASK ('F')) {                      /* format spec? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return sim_messagef (SCPE_2FARG, "Missing Format specifier and/or filename to attach\n");
    if (sim_tape_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK)
        return sim_messagef (SCPE_ARG, "Invalid Tape Format: %s\n", gbuf);
    sim_switches = sim_switches & ~(SWMASK ('F'));      /* Record Format specifier already processed */
    auto_format = TRUE;
    }
if (sim_switches & SWMASK ('B')) {                  /* Record Size (blocking factor)? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return sim_messagef (SCPE_2FARG, "Missing Record Size and filename to attach\n");
    recsize = (uint32) get_uint (gbuf, 10, 65536, &r);
    if ((r != SCPE_OK) || (recsize == 0))
        return sim_messagef (SCPE_ARG, "Invalid Tape Record Size: %s\n", gbuf);
    uptr->recsize = recsize;
    sim_switches = sim_switches & ~(SWMASK ('B'));      /* Record Blocking Factor */
    }
else {
    if ((MT_GET_FMT (uptr) == MTUF_F_TAR) && (uptr->recsize == 0))
        uptr->recsize = TAR_DFLT_RECSIZE;
    }
if (sim_switches & SWMASK ('X'))
    cptr = get_glyph_nc (cptr, export_file, 0);     /* get spec */
if ((MT_GET_FMT (uptr) == MTUF_F_TPC) ||
    (MT_GET_FMT (uptr) == MTUF_F_TAR) ||
    (MT_GET_FMT (uptr) >= MTUF_F_ANSI))
    sim_switches |= SWMASK ('R');                       /* Force ReadOnly attach for TPC, TAR and ANSI tapes */
if (sim_switches & SWMASK ('X'))
    cptr = get_glyph_nc (cptr, export_file, 0);         /* get export file spec */
switch (MT_GET_FMT (uptr)) {
    case MTUF_F_ANSI:
        if (1) {
            const char *ocptr = cptr;
            char label[CBUFSIZE] = "simh";
            int file_errors = 0;

            if ((MT_GET_ANSI_TYP (uptr) == MTAT_F_RT11)  ||
                (MT_GET_ANSI_TYP (uptr) == MTAT_F_RSX11) ||
                (MT_GET_ANSI_TYP (uptr) == MTAT_F_RSTS))
                uptr->recsize = 512;
            if (uptr->recsize == 0)
                uptr->recsize = 2048;
            else {
                if ((uptr->recsize < 512) || (uptr->recsize % 512))
                    return sim_messagef (SCPE_ARG, "Block size of %u is below or not a multiple of the required minimum ANSI size of 512.\n", uptr->recsize); 
                }
            tape = ansi_create_tape (label, uptr->recsize, MT_GET_ANSI_TYP (uptr));
            uptr->fileref = (FILE *)tape;
            if (!uptr->fileref)
                return SCPE_MEM;
            while (*cptr != 0) {                                    /* do all mods */
                uint32 initial_file_count = tape->file_count;

                cptr = get_glyph_nc (cptr, gbuf, ',');              /* get filename */
                r = sim_dir_scan (gbuf, sim_tape_add_ansi_entry, tape);
                if (r != SCPE_OK)
                    sim_messagef (SCPE_ARG, "file not found: %s\n", gbuf);
                if (tape->file_count == initial_file_count)
                    ++file_errors;
                }
            if ((tape->file_count > 0) && (file_errors == 0)) {
                r = SCPE_OK;
                memory_tape_add_block (tape, NULL, 0);  /* Tape Mark */
                uptr->flags |= UNIT_ATT;
                uptr->filename = (char *)malloc (strlen (ocptr) + 1);
                strcpy (uptr->filename, ocptr);
                uptr->tape_eom = tape->record_count;
                }
            else {
                r = SCPE_ARG;
                memory_free_tape (uptr->fileref);
                uptr->fileref = NULL;
                cptr = ocptr;
                }
            }
        break;

    case MTUF_F_FIXED:
        if (1) {
            FILE *f;
            size_t max_record_size;
            t_bool lf_line_endings;
            t_bool crlf_line_endings;
            uint8 *block = NULL;
            int error = FALSE;
            static const uint8 ascii2ebcdic[128] = {
                0000,0001,0002,0003,0067,0055,0056,0057,
                0026,0005,0045,0013,0014,0015,0016,0017,
                0020,0021,0022,0023,0074,0075,0062,0046,
                0030,0031,0077,0047,0034,0035,0036,0037,
                0100,0117,0177,0173,0133,0154,0120,0175,
                0115,0135,0134,0116,0153,0140,0113,0141,
                0360,0361,0362,0363,0364,0365,0366,0367,
                0370,0371,0172,0136,0114,0176,0156,0157,
                0174,0301,0302,0303,0304,0305,0306,0307,
                0310,0311,0321,0322,0323,0324,0325,0326,
                0327,0330,0331,0342,0343,0344,0345,0346,
                0347,0350,0351,0112,0340,0132,0137,0155,
                0171,0201,0202,0203,0204,0205,0206,0207,
                0210,0211,0221,0222,0223,0224,0225,0226,
                0227,0230,0231,0242,0243,0244,0245,0246,
                0247,0250,0251,0300,0152,0320,0241,0007};

            tape = memory_create_tape ();
            uptr->fileref = (FILE *)tape;
            if (!uptr->fileref)
                return SCPE_MEM;
            f = fopen (cptr, "rb");
            if (f == NULL) {
                r = sim_messagef (SCPE_OPENERR, "Can't open: %s - %s\n", cptr, strerror (errno));
                break;
                }
            tape_classify_file_contents (f, &max_record_size, &lf_line_endings, &crlf_line_endings);
            if ((!lf_line_endings) && (!crlf_line_endings)) {   /* binary file? */
                if (uptr->recsize == 0) {
                    r = sim_messagef (SCPE_ARG, "Binary file %s must specify a record size with -B\n", cptr);
                    fclose (f);
                    break;
                    }
                block = (uint8 *)malloc (uptr->recsize);
                tape->block_size = uptr->recsize;
                while (!feof(f) && !error) {
                    size_t data_read = fread (block, 1, tape->block_size, f);
                    if (data_read > 0)
                        error = memory_tape_add_block (tape, block, data_read);
                    }
                }
            else {                                              /* text file */
                if (uptr->recsize == 0)
                    uptr->recsize = max_record_size;
                if (uptr->recsize < max_record_size) {
                    r = sim_messagef (SCPE_ARG, "Text file: %s has lines longer than %d\n", cptr, (int)uptr->recsize);
                    fclose (f);
                    break;
                    }
                tape->block_size = uptr->recsize;
                block = (uint8 *)calloc (1, uptr->recsize + 3);
                while (!feof(f) && !error) {
                    if (fgets ((char *)block, uptr->recsize + 3, f)) {
                        size_t len = strlen ((char *)block);

                        while ((len > 0) && 
                               ((block[len - 1] == '\r') || (block[len - 1] == '\n')))
                            --len;
                        memset (block + len, ' ', uptr->recsize - len);
                        if (sim_switches & SWMASK ('C')) {
                            uint32 i;

                            for (i=0; i<uptr->recsize; i++)
                                block[i] = ascii2ebcdic[block[i]];
                            }
                        error = memory_tape_add_block (tape, block, uptr->recsize);
                        }
                    else
                        error = ferror (f);
                    }
                }
            free (block);
            fclose (f);
            r = SCPE_OK;
            memory_tape_add_block (tape, NULL, 0);  /* Tape Mark */
            memory_tape_add_block (tape, NULL, 0);  /* Tape Mark */
            uptr->flags |= UNIT_ATT;
            uptr->filename = (char *)malloc (strlen (cptr) + 1);
            strcpy (uptr->filename, cptr);
            uptr->tape_eom = tape->record_count;
            }
        break;

    case MTUF_F_TAR:
        if (uptr->recsize == 0)
            uptr->recsize = TAR_DFLT_RECSIZE;       /* Apply default block size */
        /* fall through */
    default:
        r = attach_unit (uptr, (CONST char *)cptr);         /* attach unit */
        break;
    }
if (r != SCPE_OK) {                                     /* error? */
    switch (MT_GET_FMT (uptr)) {
        case MTUF_F_ANSI:
        case MTUF_F_TAR:
        case MTUF_F_FIXED:
            r = sim_messagef (r, "Error opening %s format internal tape image generated from: %s\n", _sim_tape_format_name (uptr), cptr);
            break;
        default:
            r = sim_messagef (r, "Error opening %s format tape image: %s - %s\n", _sim_tape_format_name (uptr), cptr, strerror(errno));
            break;
        }
    if (auto_format)    /* format was specified at attach time? */
        sim_tape_set_fmt (uptr, 0, "SIMH", NULL);   /* restore default format */
    uptr->recsize = 0;
    uptr->tape_eom = 0;
    return r;
    }

if ((sim_switches & SWMASK ('D')) && !had_debug) {
    sim_switches |= SWMASK ('E');
    sim_switches &= ~(SWMASK ('D') | SWMASK ('R') | SWMASK ('F'));
    sim_set_debon (0, "STDOUT");
    sim_switches = saved_switches;
    }
if (sim_switches & SWMASK ('D'))
    uptr->dctrl = MTSE_DBG_STR | MTSE_DBG_DAT;

uptr->tape_ctx = ctx = (struct tape_context *)calloc(1, sizeof(struct tape_context));
ctx->dptr = dptr;                                       /* save DEVICE pointer */
ctx->dbit = dbit;                                       /* save debug bit */
ctx->auto_format = auto_format;                         /* save that we auto selected format */

switch (MT_GET_FMT (uptr)) {                            /* case on format */

    case MTUF_F_TPC:                                    /* TPC */
        objc = sim_tape_tpc_map (uptr, NULL, 0);        /* get # objects */
        if (objc == 0) {                                /* tape empty? */
            sim_tape_detach (uptr);
            r = SCPE_FMT;                               /* yes, complain */
            }
        uptr->filebuf = calloc (objc + 1, sizeof (t_addr));
        if (uptr->filebuf == NULL) {                    /* map allocated? */
            sim_tape_detach (uptr);
            r = SCPE_MEM;                               /* no, complain */
            }
        uptr->hwmark = objc + 1;                        /* save map size */
        sim_tape_tpc_map (uptr, (t_addr *) uptr->filebuf, objc);/* fill map */
        break;

    case MTUF_F_TAR:                                    /* TAR */
        uptr->hwmark = (t_addr)sim_fsize (uptr->fileref);
        break;

    default:
        break;
        }

if (r == SCPE_OK) {

    sim_tape_validate_tape (uptr);

    sim_tape_rewind (uptr);

#if defined (SIM_ASYNCH_IO)
    sim_tape_set_async (uptr, completion_delay);
#endif
    uptr->io_flush = _sim_tape_io_flush;
    }

if ((sim_switches & SWMASK ('D')) && !had_debug)
    sim_set_deboff (0, "");
if (sim_switches & SWMASK ('D'))
    uptr->dctrl = starting_dctrl;
if ((r == SCPE_OK) && (sim_switches & SWMASK ('X')))
    r = sim_export_tape (uptr, export_file);
return r;
}

/* Detach tape unit */

t_stat sim_tape_detach (UNIT *uptr)
{
struct tape_context *ctx;
uint32 f;
t_stat r;
t_bool auto_format = FALSE;

if (uptr == NULL)
    return SCPE_IERR;
if (!(uptr->flags & UNIT_ATT))
    return SCPE_UNATT;

ctx = (struct tape_context *)uptr->tape_ctx;
f = MT_GET_FMT (uptr);

if (uptr->io_flush)
    uptr->io_flush (uptr);                              /* flush buffered data */
if (ctx)
    auto_format = ctx->auto_format;

sim_tape_clr_async (uptr);

MT_CLR_INMRK (uptr);                                    /* Not within a TAR tapemark */
if (MT_GET_FMT (uptr) >= MTUF_F_ANSI) {
    memory_free_tape ((void *)uptr->fileref);
    uptr->fileref = NULL;
    uptr->flags &= ~UNIT_ATT;
    r = SCPE_OK;
    }
else
    r = detach_unit (uptr);                             /* detach unit */
if (r != SCPE_OK)
    return r;
switch (f) {                                            /* case on format */

    case MTUF_F_TPC:                                    /* TPC */
        if (uptr->filebuf)                              /* free map */
            free (uptr->filebuf);
        uptr->filebuf = NULL;
        break;

    default:
        break;
        }
uptr->hwmark = 0;
uptr->recsize = 0;
uptr->tape_eom = 0;
uptr->pos = 0;
MT_CLR_PNU (uptr);
MT_CLR_INMRK (uptr);                                    /* Not within a TAR tapemark */
free (uptr->tape_ctx);
uptr->tape_ctx = NULL;
uptr->io_flush = NULL;
if (auto_format)    /* format was determined or specified at attach time? */
    sim_tape_set_fmt (uptr, 0, "SIMH", NULL);   /* restore default format */
return SCPE_OK;
}

t_stat sim_tape_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s Tape Attach Help\n\n", dptr->name);
if (0 == (uptr-dptr->units)) {
    if (dptr->numunits > 1) {
        uint32 i;

        for (i=0; i < dptr->numunits; ++i)
            if (dptr->units[i].flags & UNIT_ATTABLE)
                fprintf (st, "  sim> ATTACH {switches} %s%d tapefile\n\n", dptr->name, i);
        }
    else
        fprintf (st, "  sim> ATTACH {switches} %s tapefile\n\n", dptr->name);
    }
else
    fprintf (st, "  sim> ATTACH {switches} %s tapefile\n\n", dptr->name);
fprintf (st, "Attach command switches\n");
fprintf (st, "    -R          Attach Read Only.\n");
fprintf (st, "    -E          Must Exist (if not specified, the default behavior is to\n");
fprintf (st, "                attempt to create the indicated virtual tape file).\n");
fprintf (st, "    -F          Open the indicated tape container in a specific format\n");
fprintf (st, "                (default is SIMH, alternatives are E11, TPC, P7B, AWS, TAR,\n");
fprintf (st, "                ANSI-VMS, ANSI-RT11, ANSI-RSX11, ANSI-RSTS, ANSI-VAR, FIXED)\n");
fprintf (st, "    -B          For TAR format tapes, the record size for data read from the\n");
fprintf (st, "                specified file.  This record size will be used for all but \n");
fprintf (st, "                possibly the last record which will be what remains unread.\n");
fprintf (st, "                The default TAR record size is 10240.  For FIXED format tapes\n");
fprintf (st, "                -B specifies the record size for binary data or the maximum \n");
fprintf (st, "                record size for text data\n");
fprintf (st, "    -V          Display some summary information about the record structure\n");
fprintf (st, "                observed in the tape image observed during the attach\n");
fprintf (st, "                validation pass\n");
fprintf (st, "    -L          Display detailed record size counts observed during attach\n");
fprintf (st, "                validation pass\n");
fprintf (st, "    -D          Causes the internal tape structure information to be displayed\n");
fprintf (st, "                while the tape image is scanned.\n");
fprintf (st, "    -C          Causes FIXED format tape data sets derived from text files to\n");
fprintf (st, "                be converted from ASCII to EBCDIC.\n");
fprintf (st, "    -X          Extract a copy of the attached tape and convert it to a SIMH\n");
fprintf (st, "                format tape image.\n\n");
fprintf (st, "Notes:  ANSI-VMS, ANSI-RT11, ANSI-RSTS, ANSI-RSX11, ANSI-VAR formats allows\n");
fprintf (st, "        one or several files to be presented to as a read only ANSI Level 3\n");
fprintf (st, "        labeled tape with file labels that make each individual file\n");
fprintf (st, "        accessible directly as files on the tape.\n\n");
fprintf (st, "        FIXED format will present the contents of a file (text or binary) as\n");
fprintf (st, "        fixed sized records/blocks with ascii text data optionally converted\n");
fprintf (st, "        to EBCDIC.\n\n");
fprintf (st, "Examples:\n\n");
fprintf (st, "  sim> ATTACH %s -F ANSI-VMS Hobbyist-USE-ONLY-VA.TXT\n", dptr->name);
fprintf (st, "  sim> ATTACH %s -F ANSI-RSX11 *.TXT,*.ini,*.exe\n", dptr->name);
fprintf (st, "  sim> ATTACH %s -FX ANSI-RSTS RSTS.tap *.TXT,*.SAV\n", dptr->name);
fprintf (st, "  sim> ATTACH %s -F ANSI-RT11 *.TXT,*.TSK\n", dptr->name);
fprintf (st, "  sim> ATTACH %s -FB FIXED 80 SOMEFILE.TXT\n\n", dptr->name);
return SCPE_OK;
}

static void sim_tape_data_trace(UNIT *uptr, const uint8 *data, size_t len, const char* txt, int detail, uint32 reason)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (ctx == NULL)
    return;
if (sim_deb && ((uptr->dctrl | ctx->dptr->dctrl) & reason))
    sim_data_trace(ctx->dptr, uptr, (detail ? data : NULL), "", len, txt, reason);
}

static int sim_tape_seek (UNIT *uptr, t_addr pos)
{
if (MT_GET_FMT (uptr) < MTUF_F_ANSI)
    return sim_fseek (uptr->fileref, pos, SEEK_SET);
return 0;
}

static t_offset sim_tape_size (UNIT *uptr)
{
if (MT_GET_FMT (uptr) < MTUF_F_ANSI)
    return sim_fsize_ex (uptr->fileref);
return uptr->tape_eom;
}

/* Read record length forward (internal routine).

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length

   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -----------------------------------------------------
   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   updated if a gap precedes, else unchanged and PNU set
   tape mark            updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the forward
   direction.  On return, status is MTSE_OK and the tape is positioned at the
   first data byte if a record was encountered, or status is an MTSE error code
   giving the reason that the operation did not succeed and the tape position is
   as indicated above.

   The ANSI standards for magnetic tape recording (X3.22, X3.39, and X3.54) and
   the equivalent ECMA standard (ECMA-62) specify a maximum erase gap length of
   25 feet (7.6 meters).  While gaps of any length may be written, gaps longer
   than this are non-standard and may indicate that an unrecorded or erased tape
   is being read.

   If the tape density has been set via a previous "sim_tape_set_dens" call,
   then the length is monitored when skipping over erase gaps.  If the length
   reaches 25 feet, motion is terminated, and MTSE_RUNAWAY status is returned.
   Runaway status is also returned if an end-of-medium marker or the physical
   end of file is encountered while spacing over a gap; however, MTSE_EOM is
   returned if the tape is positioned at the EOM or EOF on entry.

   If the density has not been set, then a gap of any length is skipped, and
   MTSE_RUNAWAY status is never returned.  In effect, erase gaps present in the
   tape image file will be transparent to the caller.

   Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.
   Because gaps may be partially overwritten with data records, gap metadata
   must be examined marker-by-marker.  To reduce the number of file read calls,
   a buffer of metadata elements is used.  The buffer size is initially
   established at 256 elements but may be set to any size desired.  To avoid a
   large read for the typical case where an erase gap is not present, the first
   read is of a single metadatum marker.  If that is a gap marker, then
   additional buffered reads are performed.

   See the notes at "tape_erase_fwd" regarding the erase gap implementation.


   Implementation notes:

    1. For programming convenience, erase gap processing is performed for both
       SIMH standard and E11 tape formats, although the latter will never
       contain erase gaps, as the "tape_erase_fwd" call takes no action for the
       E11 format.

    2. The "feof" call cannot return a non-zero value on the first pass through
       the loop, because the "sim_fseek" call resets the internal end-of-file
       indicator.  Subsequent passes only occur if an erase gap is present, so
       a non-zero return indicates an EOF was seen while reading through a gap.

    3. The "runaway_counter" cannot decrement to zero (or below) in the presence
       of an error that terminates the gap-search loop.  Therefore, the test
       after the loop exit need not check for error status.

    4. The dynamic start/stop test of the HP 3000 magnetic tape diagnostic
       heavily exercises the erase gap scanning code.  Sample test execution
       times for various buffer sizes on a 2 GHz host platform are:

         buffer size    execution time
         (elements)     (CPU seconds)
         -----------    --------------
               1             7200
              32              783
             128              237
             256              203
             512              186
            1024              171

    5. Because an erase gap may precede the logical end-of-medium, represented
       either by the physical end-of-file or by an EOM marker, the "position not
       updated" flag is set only if the tape is positioned at the EOM when the
       routine is entered.  If at least one gap marker precedes the EOM, then
       the PNU flag is not set.  This ensures that a backspace-and-retry
       sequence will work correctly in both cases.
*/

static t_stat sim_tape_rdlntf (UNIT *uptr, t_mtrlnt *bc)
{
uint8    c;
t_bool   all_eof;
uint32   f = MT_GET_FMT (uptr);
t_mtrlnt sbc;
t_tpclnt tpcbc;
t_awshdr awshdr;
size_t   rdcnt;
t_mtrlnt buffer [256];                                  /* local tape buffer */
t_addr saved_pos;
uint32   bufcntr, bufcap;                               /* buffer counter and capacity */
int32    runaway_counter, sizeof_gap;                   /* bytes remaining before runaway and bytes per gap */
t_stat   status = MTSE_OK;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */
*bc = 0;

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

if ((uptr->tape_eom) && 
    (uptr->pos >= uptr->tape_eom)) {
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    return MTSE_EOM;                                    /*     and quit with I/O error status */
    }

if (sim_tape_seek (uptr, uptr->pos)) {                  /* set the initial tape position; if it fails */
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    return sim_tape_ioerr (uptr);                       /*     and quit with I/O error status */
    }

switch (f) {                                       /* otherwise the read method depends on the tape format */

    case MTUF_F_STD:
    case MTUF_F_E11:
        runaway_counter = 25 * 12 * bpi [MT_DENS (uptr->dynflags)]; /* set the largest legal gap size in bytes */

        if (runaway_counter == 0) {                     /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            runaway_counter = INT_MAX;                  /*     to allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        do {                                            /* loop until a record, gap, or error is seen */
            if (bufcntr == bufcap) {                    /* if the buffer is empty then refill it */
                if (feof (uptr->fileref)) {             /* if we hit the EOF while reading a gap */
                    if (sizeof_gap > 0)                 /*   then if detection is enabled */
                        status = MTSE_RUNAWAY;          /*     then report a tape runaway */
                    else                                /*   otherwise report the physical EOF */
                        status = MTSE_EOM;              /*     as the end-of-medium */
                    break;
                    }

                else if (bufcap == 0)                   /* otherwise if this is the initial read */
                    bufcap = 1;                         /*   then start with just one marker */

                else                                    /* otherwise reset the capacity */
                    bufcap = sizeof (buffer)            /*   to the full size of the buffer */
                               / sizeof (buffer [0]);

                bufcap = sim_fread (buffer,             /* fill the buffer */
                                    sizeof (t_mtrlnt),  /*   with tape metadata */
                                    bufcap,
                                    uptr->fileref);

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    if (bufcntr == 0)                   /*   then if this is the initial read */
                        MT_SET_PNU (uptr);              /*     then set position not updated */

                    status = sim_tape_ioerr (uptr);     /* report the error and quit */
                    break;
                    }

                else if (bufcap == 0                    /* otherwise if positioned at the physical EOF */
                  || buffer [0] == MTR_EOM)             /*   or at the logical EOM */
                    if (bufcntr == 0) {                 /*     then if this is the initial read */
                        MT_SET_PNU (uptr);              /*       then set position not updated */
                        status = MTSE_EOM;              /*         and report the end-of-medium and quit */
                        uptr->tape_eom = uptr->pos;
                        break;
                        }

                    else {                              /*     otherwise some gap has already been skipped */
                        if (sizeof_gap > 0)             /*       so if detection is enabled */
                            status = MTSE_RUNAWAY;      /*         then report a tape runaway */
                        else                            /*       otherwise report the physical EOF */
                            status = MTSE_EOM;          /*         as the end-of-medium */
                        break;
                        }

                else                                    /* otherwise reset the index */
                    bufcntr = 0;                        /*   to the start of the buffer */
                }

            *bc = buffer [bufcntr++];                   /* store the metadata marker value */

            if (*bc == MTR_EOM) {                       /* if an end-of-medium marker is seen */
                if (sizeof_gap > 0)                     /*   then if detection is enabled */
                    status = MTSE_RUNAWAY;              /*     then report a tape runaway */
                else                                    /*   otherwise report the physical EOF */
                    status = MTSE_EOM;                  /*     as the end-of-medium */
                break;
                }

            uptr->pos += sizeof (t_mtrlnt);             /* space over the marker */

            if (*bc == MTR_TMK) {                       /* if the value is a tape mark */
                status = MTSE_TMK;                      /*   then quit with tape mark status */
                break;
                }

            else if (*bc == MTR_GAP)                    /* otherwise if the value is a full gap */
                runaway_counter -= sizeof_gap;          /*   then decrement the gap counter */

            else if (*bc == MTR_FHGAP) {                        /* otherwise if the value if a half gap */
                uptr->pos = uptr->pos - sizeof (t_mtrlnt) / 2;  /*   then back up and resync */

                if (sim_tape_seek (uptr, uptr->pos)) {          /* set the tape position; if it fails */
                    status = sim_tape_ioerr (uptr);             /*   then quit with I/O error status */
                    break;
                    }

                bufcntr = bufcap;                       /* mark the buffer as invalid to force a read */

                *bc = (t_mtrlnt)MTR_GAP;                /* reset the marker */
                runaway_counter -= sizeof_gap / 2;      /*   and decrement the gap counter */
                }

            else {                                                      /* otherwise it's a record marker */
                saved_pos = uptr->pos;                          /* Save data position */
                sbc = MTR_L (*bc);                              /* extract the record length */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt)       /* position to the start */
                  + (f == MTUF_F_STD ? (sbc + 1) & ~1 : sbc);   /*   of the record */
                }
            }
        while (*bc == MTR_GAP && runaway_counter > 0);  /* continue until data or runaway occurs */

        if (runaway_counter <= 0)                       /* if a tape runaway occurred */
            status = MTSE_RUNAWAY;                      /*   then report it */

        if (status == MTSE_OK) {        /* Validate the reverse record size for data records */
            t_mtrlnt rev_lnt;

            if (sim_tape_seek (uptr, uptr->pos - sizeof (t_mtrlnt))) {  /*   then seek to the end of record size; if it fails */
                status = sim_tape_ioerr (uptr);         /*     then quit with I/O error status */
                break;
                }

            (void)sim_fread (&rev_lnt,                  /* get the reverse length */
                             sizeof (t_mtrlnt),
                             1,
                             uptr->fileref);

            if (ferror (uptr->fileref)) {               /* if a file I/O error occurred */
                status = sim_tape_ioerr (uptr);         /* report the error and quit */
                break;
                }
            if (rev_lnt != *bc) {           /* size mismatch? */
                status = MTSE_INVRL;
                uptr->pos -= (sizeof (t_mtrlnt) + *bc + sizeof (t_mtrlnt));
                MT_SET_PNU (uptr);                      /* pos not upd */
                break;
                }
            if (sim_tape_seek (uptr, saved_pos))        /*   then seek back to the beginning of the data; if it fails */
                status = sim_tape_ioerr (uptr);         /*     then quit with I/O error status */
            }
        break;                                          /* otherwise the operation succeeded */

    case MTUF_F_TPC:
        (void)sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = (t_mtrlnt)tpcbc;                          /* save rec lnt */

        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);                          /* pos not upd */
            status = sim_tape_ioerr (uptr);
            }
        else {
            if ((feof (uptr->fileref)) ||               /* eof? */
                ((tpcbc == TPC_EOM) && 
                 (sim_fsize (uptr->fileref) == (uint32)sim_ftell (uptr->fileref)))) {
                MT_SET_PNU (uptr);                          /* pos not upd */
                status = MTSE_EOM;
                }
            else {
                uptr->pos += sizeof (t_tpclnt);             /* spc over reclnt */
                if (tpcbc == TPC_TMK)                       /* tape mark? */
                    status = MTSE_TMK;
                else
                    uptr->pos = uptr->pos + ((tpcbc + 1) & ~1); /* spc over record */
                }
            }
        break;

    case MTUF_F_P7B:
        for (sbc = 0, all_eof = 1; ; sbc++) {           /* loop thru record */
            (void)sim_fread (&c, sizeof (uint8), 1, uptr->fileref);

            if (ferror (uptr->fileref)) {               /* error? */
                MT_SET_PNU (uptr);                      /* pos not upd */
                status = sim_tape_ioerr (uptr);
                break;
                }
            else if (feof (uptr->fileref)) {            /* eof? */
                if (sbc == 0)                           /* no data? eom */
                    status = MTSE_EOM;
                break;                                  /* treat like eor */
                }
            else if ((sbc != 0) && (c & P7B_SOR))       /* next record? */
                break;
            else if ((c & P7B_DPAR) != P7B_EOF)
                all_eof = 0;
            }

        if (status == MTSE_OK) {
            *bc = sbc;                                      /* save rec lnt */
            (void)sim_tape_seek (uptr, uptr->pos);          /* for read */
            uptr->pos = uptr->pos + sbc;                    /* spc over record */
            if (all_eof) {                                  /* tape mark? */
                status = MTSE_TMK;
                *bc = 0;
                }
            }
        break;

    case MTUF_F_AWS:
        saved_pos = (t_addr)sim_ftell (uptr->fileref);
        memset (&awshdr, 0, sizeof (awshdr));
        rdcnt = sim_fread (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
        if (ferror (uptr->fileref)) {           /* error? */
            MT_SET_PNU (uptr);                  /* pos not upd */
            status = sim_tape_ioerr (uptr);
            break;
            }
        if ((feof (uptr->fileref)) ||           /* eof? */
            (rdcnt < 3)) {
            uptr->tape_eom = uptr->pos;
            MT_SET_PNU (uptr);                  /* pos not upd */
            status = MTSE_EOM;
            break;
            }
        uptr->pos += sizeof (t_awshdr);         /* spc over AWS header */
        if (awshdr.rectyp == AWS_TMK)           /* tape mark? */
            status = MTSE_TMK;
        else {
            if (awshdr.rectyp != AWS_REC) {     /* Unknown record type */
                MT_SET_PNU (uptr);              /* pos not upd */
                uptr->tape_eom = uptr->pos;
                status = MTSE_INVRL;
                break;
                }
            else
                status = MTSE_OK;
            }
        /* tape data record (or tapemark) */
        *bc = (t_mtrlnt)awshdr.nxtlen;          /* save rec lnt */
        uptr->pos += awshdr.nxtlen;             /* spc over record */
        memset (&awshdr, 0, sizeof (t_awslnt));
        saved_pos = (t_addr)sim_ftell (uptr->fileref);/* save record data address */
        (void)sim_tape_seek (uptr, uptr->pos); /* for read */
        rdcnt = sim_fread (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
        if ((rdcnt == 3) && 
            ((awshdr.prelen != *bc) || ((awshdr.rectyp != AWS_REC) && (awshdr.rectyp != AWS_TMK)))) {
            status = MTSE_INVRL;
            uptr->tape_eom = uptr->pos;
            uptr->pos = saved_pos - sizeof (t_awslnt);
            }
        else
            (void)sim_tape_seek (uptr, saved_pos); /* Move back to the data */
        break;

    case MTUF_F_TAR:
        if (uptr->pos < uptr->hwmark) {
            if ((uptr->hwmark - uptr->pos) >= uptr->recsize)
                *bc = (t_mtrlnt)uptr->recsize;              /* TAR record size */
            else
                *bc = (t_mtrlnt)(uptr->hwmark - uptr->pos); /* TAR remnant last record */
            (void)sim_tape_seek (uptr, uptr->pos); 
            uptr->pos += *bc;
            MT_CLR_INMRK (uptr);
            }
        else {
            if (MT_TST_INMRK (uptr))
                status = MTSE_EOM;
            else {
                status = MTSE_TMK;
                MT_SET_INMRK (uptr);
                }
            }
        break;

    case MTUF_F_ANSI:
    case MTUF_F_FIXED:
        if (1) {
            MEMORY_TAPE *tape = (MEMORY_TAPE *)uptr->fileref;

            if (uptr->pos >= tape->record_count) 
                status = MTSE_EOM;
            else {
                if (tape->records[uptr->pos]->size == 0)
                    status = MTSE_TMK;
                else
                    *bc = tape->records[uptr->pos]->size;
                ++uptr->pos;
                }
            }
        break;

    default:
        status = MTSE_FMT;
    }

return status;
}

static t_stat sim_tape_rdrlfwd (UNIT *uptr, t_mtrlnt *bc)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat status;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */

status = sim_tape_rdlntf (uptr, bc);                    /* read the record length */

sim_debug_unit (MTSE_DBG_STR, uptr, "rd_lntf: st: %d, lnt: %d, pos: %" T_ADDR_FMT "u\n", status, *bc, uptr->pos);

return status;
}

/* Read record length reverse (internal routine).

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to returned record length
   Outputs:
        status  =       operation status

   exit condition       tape position
   ------------------   -------------------------------------------
   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   tape runaway         updated
   data record          updated, sim_fread will read record forward

   This routine is called to set up a record read or spacing in the reverse
   direction.  On return, status is MTSE_OK and the tape is positioned at the
   first data byte if a record was encountered, or status is an MTSE error code
   giving the reason that the operation did not succeed and the tape position is
   as indicated above.


   Implementation notes:

    1. The "sim_fread" call cannot return 0 in the absence of an error
       condition.  The preceding "sim_tape_bot" test ensures that "pos" >= 4, so
       "sim_fseek" will back up at least that far, so "sim_fread" will read at
       least one element.  If the call returns zero, an error must have
       occurred, so the "ferror" call must succeed.

    2. See the notes at "sim_tape_rdlntf" and "tape_erase_fwd" regarding tape
       runaway and the erase gap implementation, respectively.
*/

static t_stat sim_tape_rdlntr (UNIT *uptr, t_mtrlnt *bc)
{
uint8    c;
t_bool   all_eof;
uint32   f = MT_GET_FMT (uptr);
t_addr   ppos;
t_mtrlnt sbc;
t_tpclnt tpcbc;
t_awshdr awshdr;
size_t   rdcnt;
t_mtrlnt buffer [256];                                  /* local tape buffer */
uint32   bufcntr, bufcap;                               /* buffer counter and capacity */
int32    runaway_counter, sizeof_gap;                   /* bytes remaining before runaway and bytes per gap */
t_stat   status = MTSE_OK;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */
*bc = 0;

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then quit with an error */

if (sim_tape_bot (uptr))                                /* if the unit is positioned at the BOT */
    return MTSE_BOT;                                    /*   then reading backward is not possible */

switch (f) {                                            /* otherwise the read method depends on the tape format */

    case MTUF_F_STD:
    case MTUF_F_E11:
        runaway_counter = 25 * 12 * bpi [MT_DENS (uptr->dynflags)]; /* set the largest legal gap size in bytes */

        if (runaway_counter == 0) {                     /* if tape density has not been not set */
            sizeof_gap = 0;                             /*   then disable runaway detection */
            runaway_counter = INT_MAX;                  /*     to allow gaps of any size */
            }

        else                                            /* otherwise */
            sizeof_gap = sizeof (t_mtrlnt);             /*   set the size of the gap */

        bufcntr = 0;                                    /* force an initial read */
        bufcap = 0;                                     /*   but of just one metadata marker */

        do {                                            /* loop until a record, gap, or error is seen */
            if (bufcntr == 0) {                         /* if the buffer is empty then refill it */
                if (sim_tape_bot (uptr)) {              /* if the search has backed into the BOT */
                    status = MTSE_BOT;                  /*   then quit with an error */
                    break;
                    }

                else if (bufcap == 0)                   /* otherwise if this is the initial read */
                    bufcap = 1;                         /*   then start with just one marker */

                else if (uptr->pos < sizeof (buffer))   /* otherwise if less than a full buffer remains */
                    bufcap = (uint32) uptr->pos         /*   then reduce the capacity accordingly */
                               / sizeof (t_mtrlnt);

                else                                    /* otherwise reset the capacity */
                    bufcap = sizeof (buffer)            /*   to the full size of the buffer */
                               / sizeof (buffer [0]);

                if (sim_tape_seek (uptr,                /* seek back to the location */
                                   uptr->pos - bufcap * sizeof (t_mtrlnt))) {  /* corresponding to the start */
                                                        /* of the buffer; if it fails */
                    status = sim_tape_ioerr (uptr);     /*         and fail with I/O error status */
                    break;
                    }

                bufcntr = sim_fread (buffer, sizeof (t_mtrlnt), /* fill the buffer */
                                     bufcap, uptr->fileref);    /*   with tape metadata */

                if (ferror (uptr->fileref)) {           /* if a file I/O error occurred */
                    status = sim_tape_ioerr (uptr);     /*   then report the error and quit */
                    break;
                    }
                }

            *bc = buffer [--bufcntr];                   /* store the metadata marker value */

            uptr->pos = uptr->pos - sizeof (t_mtrlnt);  /* backspace over the marker */

            if (*bc == MTR_TMK) {                       /* if the marker is a tape mark */
                status = MTSE_TMK;                      /*   then quit with tape mark status */
                break;
                }

            else if (*bc == MTR_GAP)                    /* otherwise if the marker is a full gap */
                runaway_counter -= sizeof_gap;          /*   then decrement the gap counter */

            else if ((*bc & MTR_M_RHGAP) == MTR_RHGAP   /* otherwise if the marker */
              || *bc == MTR_RRGAP) {                    /*   is a half gap */
                uptr->pos = uptr->pos + sizeof (t_mtrlnt) / 2;/* then position forward to resync */
                bufcntr = 0;                            /* mark the buffer as invalid to force a read */

                *bc = (t_mtrlnt)MTR_GAP;                /* reset the marker */
                runaway_counter -= sizeof_gap / 2;      /*   and decrement the gap counter */
                }

            else {                                      /* otherwise it's a record marker */
                sbc = MTR_L (*bc);                      /* extract the record length */
                uptr->pos = uptr->pos - sizeof (t_mtrlnt)/* position to the start */
                  - (f == MTUF_F_STD ? (sbc + 1) & ~1 : sbc);/*   of the record */

                if (sim_tape_seek (uptr,                /* seek to the start of the data area; if it fails */
                               uptr->pos + sizeof (t_mtrlnt))) {/* then return with I/O error status */
                    status = sim_tape_ioerr (uptr);
                    break;
                    }
                }
            }
        while (*bc == MTR_GAP && runaway_counter > 0);  /* continue until data or runaway occurs */

        if (runaway_counter <= 0)                       /* if a tape runaway occurred */
            status = MTSE_RUNAWAY;                      /*   then report it */

        break;                                          /* otherwise the operation succeeded */

    case MTUF_F_TPC:
        ppos = sim_tape_tpc_fnd (uptr, (t_addr *) uptr->filebuf); /* find prev rec */
        (void)sim_tape_seek (uptr, ppos);               /* position */
        (void)sim_fread (&tpcbc, sizeof (t_tpclnt), 1, uptr->fileref);
        *bc = (t_mtrlnt)tpcbc;                          /* save rec lnt */

        if (ferror (uptr->fileref))                     /* error? */
            status = sim_tape_ioerr (uptr);
        else if (feof (uptr->fileref))                  /* eof? */
            status = MTSE_EOM;
        else {
            uptr->pos = ppos;                           /* spc over record */
            if (*bc == MTR_TMK)                         /* tape mark? */
                status = MTSE_TMK;
            else
                (void)sim_tape_seek (uptr, uptr->pos + sizeof (t_tpclnt));
            }
        break;

    case MTUF_F_P7B:
        if (1) {
#define BUF_SZ 512
            uint8 buf[BUF_SZ];
            t_addr buf_offset = uptr->pos;
            size_t bytes_in_buf = 0;
            size_t read_size;

            for (sbc = 1, all_eof = 1; (t_addr) sbc <= uptr->pos ; sbc++) {
                if (bytes_in_buf == 0) {                /* Need to Fill Buffer */
                    if (buf_offset < BUF_SZ) {
                        read_size = (size_t)buf_offset;
                        buf_offset = 0;
                        }
                    else {
                        read_size = BUF_SZ;
                        buf_offset -= BUF_SZ;
                        }
                    (void)sim_tape_seek (uptr, buf_offset);
                    bytes_in_buf = sim_fread (buf, sizeof (uint8), read_size, uptr->fileref);
                    if (ferror (uptr->fileref)) {       /* error? */
                        status = sim_tape_ioerr (uptr);
                        break;
                        }
                    if (feof (uptr->fileref)) {         /* eof? */
                        status = MTSE_EOM;
                        break;
                        }
                    }
                c = buf[--bytes_in_buf];
                if ((c & P7B_DPAR) != P7B_EOF)
                    all_eof = 0;
                if (c & P7B_SOR)                        /* start of record? */
                    break;
                }

            if (status == MTSE_OK) {
                uptr->pos = uptr->pos - sbc;            /* update position */
                *bc = sbc;                              /* save rec lnt */
                (void)sim_tape_seek (uptr, uptr->pos);  /* for next read */
                if (all_eof)                            /* tape mark? */
                    status = MTSE_TMK;
                }
            break;
            }

    case MTUF_F_AWS:
        *bc = 0;
        status = MTSE_OK;
        (void)sim_tape_seek (uptr, uptr->pos);          /* position */
        while (1) {
            if (sim_tape_bot (uptr)) {                  /* if we start at BOT */
                status = MTSE_BOT;                      /*   then we're done */
                break;
                }
            memset (&awshdr, 0, sizeof (awshdr));
            rdcnt = sim_fread (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
            if (ferror (uptr->fileref)) {               /* error? */
                status = sim_tape_ioerr (uptr);
                break;
                }
            if (feof (uptr->fileref)) {                 /* eof? */
                if ((uptr->pos > sizeof (t_awshdr)) &&
                    (uptr->pos >= sim_fsize (uptr->fileref))) {
                    uptr->tape_eom = uptr->pos;
                    (void)sim_tape_seek (uptr, uptr->pos - sizeof (t_awshdr));/* position */
                    continue;
                    }
                status = MTSE_EOM;
                break;
                }
            if ((rdcnt != 3) || 
                ((awshdr.rectyp != AWS_REC) && 
                 (awshdr.rectyp != AWS_TMK))) {
                status = MTSE_INVRL;
                }
            break;
            }
        if (status != MTSE_OK)
            break;
        if (awshdr.prelen == 0)
            status = MTSE_TMK;
        else {
            if ((uptr->tape_eom > 0) && 
                (uptr->pos >= uptr->tape_eom) && 
                (awshdr.rectyp == AWS_TMK)) {
                status = MTSE_TMK;
                *bc = 0;                                /* save rec lnt */
                }
            else {
                status = MTSE_OK;
                *bc = (t_mtrlnt)awshdr.prelen;          /* save rec lnt */
                }
            }
        uptr->pos -= sizeof (t_awshdr);                 /* position to the start of the record */
        uptr->pos -= *bc;                               /* Including the data length */
        if (sim_tape_seek (uptr,                        /* seek to the start of the data area; if it fails */
                           uptr->pos + sizeof (t_awshdr))) {
            status = sim_tape_ioerr (uptr);             /* then return with I/O error status */
            break;
            }
        break;

     case MTUF_F_TAR:
         if (uptr->pos == uptr->hwmark) {
             if (MT_TST_INMRK (uptr)) {
                 status = MTSE_TMK;
                 MT_CLR_INMRK (uptr);
                 }
             else {
                 if (uptr->hwmark % uptr->recsize)
                     *bc = (t_mtrlnt)(uptr->hwmark % uptr->recsize);
                 else
                     *bc = (t_mtrlnt)uptr->recsize;
                 }
             }
         else
             *bc = (t_mtrlnt)uptr->recsize;
         if (*bc) {
             uptr->pos -= *bc;
             (void)sim_tape_seek (uptr, uptr->pos); 
             }
        break;

    case MTUF_F_ANSI:
    case MTUF_F_FIXED:
        if (1) {
            MEMORY_TAPE *tape = (MEMORY_TAPE *)uptr->fileref;

            --uptr->pos;
            if (tape->records[uptr->pos]->size == 0)
                status = MTSE_TMK;
            else
                *bc = tape->records[uptr->pos]->size;
            }
        break;

   default:
        status = MTSE_FMT;
        }

return status;
}

static t_stat sim_tape_rdrlrev (UNIT *uptr, t_mtrlnt *bc)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat status;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */

status = sim_tape_rdlntr (uptr, bc);                    /* read the record length */

sim_debug_unit (MTSE_DBG_STR, uptr, "rd_lntr: st: %d, lnt: %d, pos: %" T_ADDR_FMT "u\n", status, *bc, uptr->pos);

return status;
}

/* Read record forward

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned record length
        max     =       maximum record size
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   invalid record       unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_rdrecf (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt i, tbc, rbc;
t_addr opos;
t_stat st;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_rdrecf(unit=%d, buf=%p, max=%d)\n", (int)(uptr-ctx->dptr->units), buf, max);

opos = uptr->pos;                                       /* old position */
st = sim_tape_rdrlfwd (uptr, &tbc);                     /* read rec lnt */
if (st != MTSE_OK) {
    *bc = 0;
    return st;
    }
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max) {                                        /* rec out of range? */
    MT_SET_PNU (uptr);
    uptr->pos = opos;
    return MTSE_INVRL;
    }
if (f < MTUF_F_ANSI) {
    i = (t_mtrlnt) sim_fread (buf, sizeof (uint8), rbc, uptr->fileref); /* read record */
    if (ferror (uptr->fileref)) {                           /* error? */
        MT_SET_PNU (uptr);
        uptr->pos = opos;
        return sim_tape_ioerr (uptr);
        }
    }
else {
    MEMORY_TAPE *tape = (MEMORY_TAPE *)uptr->fileref;

    memcpy (buf, tape->records[uptr->pos - 1]->data, rbc);
    i = rbc;
    }
for ( ; i < rbc; i++)                                   /* fill with 0's */
    buf[i] = 0;
if (f == MTUF_F_P7B)                                    /* p7b? strip SOR */
    buf[0] = buf[0] & P7B_DPAR;
sim_tape_data_trace(uptr, buf, rbc, "Record Read", (uptr->dctrl | ctx->dptr->dctrl) & MTSE_DBG_DAT, MTSE_DBG_STR);
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

t_stat sim_tape_rdrecf_a (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max, TAPE_PCALLBACK callback)
{
t_stat r = SCPE_OK;
AIO_CALLSETUP
    r = sim_tape_rdrecf (uptr, buf, bc, max);
AIO_CALL(TOP_RDRF, buf, bc, NULL, max, 0, 0, 0, NULL, callback);
return r;
}


/* Read record reverse

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       pointer to returned record length
        max     =       maximum record size
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   invalid record       unchanged
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_rdrecr (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt i, rbc, tbc;
t_stat st;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_rdrecr(unit=%d, buf=%p, max=%d)\n", (int)(uptr-ctx->dptr->units), buf, max);

st = sim_tape_rdrlrev (uptr, &tbc);                     /* read rec lnt */
if (st != MTSE_OK) {
    *bc = 0;
    return st;
    }
*bc = rbc = MTR_L (tbc);                                /* strip error flag */
if (rbc > max)                                          /* rec out of range? */
    return MTSE_INVRL;
if (f < MTUF_F_ANSI) {
    i = (t_mtrlnt) sim_fread (buf, sizeof (uint8), rbc, uptr->fileref); /* read record */
    if (ferror (uptr->fileref))                             /* error? */
        return sim_tape_ioerr (uptr);
    }
else {
    MEMORY_TAPE *tape = (MEMORY_TAPE *)uptr->fileref;

    memcpy (buf, tape->records[uptr->pos]->data, rbc);
    i = rbc;
    }
for ( ; i < rbc; i++)                                   /* fill with 0's */
    buf[i] = 0;
if (f == MTUF_F_P7B)                                    /* p7b? strip SOR */
    buf[0] = buf[0] & P7B_DPAR;
sim_tape_data_trace(uptr, buf, rbc, "Record Read Reverse", (uptr->dctrl | ctx->dptr->dctrl) & MTSE_DBG_DAT, MTSE_DBG_STR);
return (MTR_F (tbc)? MTSE_RECE: MTSE_OK);
}

t_stat sim_tape_rdrecr_a (UNIT *uptr, uint8 *buf, t_mtrlnt *bc, t_mtrlnt max, TAPE_PCALLBACK callback)
{
t_stat r = SCPE_OK;
AIO_CALLSETUP
    r = sim_tape_rdrecr (uptr, buf, bc, max);
AIO_CALL(TOP_RDRR, buf, bc, NULL, max, 0, 0, 0, NULL, callback);
return r;
}

/* Write record forward

   Inputs:
        uptr    =       pointer to tape unit
        buf     =       pointer to buffer
        bc      =       record length
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   write protect        unchanged
   write error          unchanged, PNU set
   data record          updated
*/

t_stat sim_tape_wrrecf (UNIT *uptr, uint8 *buf, t_mtrlnt bc)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
uint32 f = MT_GET_FMT (uptr);
t_mtrlnt sbc;
t_stat status = MTSE_OK;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_wrrecf(unit=%d, buf=%p, bc=%d)\n", (int)(uptr-ctx->dptr->units), buf, bc);

sim_tape_data_trace(uptr, buf, bc, "Record Write", (uptr->dctrl | ctx->dptr->dctrl) & MTSE_DBG_DAT, MTSE_DBG_STR);
MT_CLR_PNU (uptr);
sbc = MTR_L (bc);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (sim_tape_wrp (uptr))                                /* write prot? */
    return MTSE_WRP;
if (sbc == 0)                                           /* nothing to do? */
    return MTSE_OK;
if (sim_tape_seek (uptr, uptr->pos))                    /* set pos */
    return MTSE_IOERR;
switch (f) {                                            /* case on format */

    case MTUF_F_STD:                                    /* standard */
        sbc = MTR_L ((bc + 1) & ~1);                    /* pad odd length */
        /* fall through into the E11 handler */
    case MTUF_F_E11:                                    /* E11 */
        (void)sim_fwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
        (void)sim_fwrite (buf, sizeof (uint8), sbc, uptr->fileref);
        (void)sim_fwrite (&bc, sizeof (t_mtrlnt), 1, uptr->fileref);
        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);
            return sim_tape_ioerr (uptr);
            }
        uptr->pos = uptr->pos + sbc + (2 * sizeof (t_mtrlnt));  /* move tape */
        break;

    case MTUF_F_P7B:                                    /* Pierce 7B */
        buf[0] = buf[0] | P7B_SOR;                      /* mark start of rec */
        (void)sim_fwrite (buf, sizeof (uint8), sbc, uptr->fileref);
        (void)sim_fwrite (buf, sizeof (uint8), 1, uptr->fileref); /* delimit rec */
        if (ferror (uptr->fileref)) {                   /* error? */
            MT_SET_PNU (uptr);
            return sim_tape_ioerr (uptr);
            }
        uptr->pos = uptr->pos + sbc;                    /* move tape */
        break;
    case MTUF_F_AWS:                                    /* AWS */
        status = sim_tape_aws_wrdata (uptr, buf, bc);
        if (status != MTSE_OK)
            return status;
        break;
        }
if (uptr->pos > uptr->tape_eom)
    uptr->tape_eom = uptr->pos;         /* update EOM as needed */
sim_tape_data_trace(uptr, buf, sbc, "Record Written", (uptr->dctrl | ctx->dptr->dctrl) & MTSE_DBG_DAT, MTSE_DBG_STR);
return MTSE_OK;
}

t_stat sim_tape_wrrecf_a (UNIT *uptr, uint8 *buf, t_mtrlnt bc, TAPE_PCALLBACK callback)
{
t_stat r = SCPE_OK;
AIO_CALLSETUP
    r = sim_tape_wrrecf (uptr, buf, bc);
AIO_CALL(TOP_WREC, buf, 0, NULL, 0, bc, 0, 0, NULL, callback);
return r;
}

/* Write AWS metadata (and possibly data) forward (internal routine) */

static t_stat sim_tape_aws_wrdata (UNIT *uptr, uint8 *buf, t_mtrlnt bc)
{
t_awshdr awshdr;
size_t   rdcnt;
t_bool   replacing_record;

memset (&awshdr, 0, sizeof (t_awshdr));
if (sim_tape_seek (uptr, uptr->pos))        /* set pos */
    return MTSE_IOERR;
rdcnt = sim_fread (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
if (ferror (uptr->fileref)) {               /* error? */
    MT_SET_PNU (uptr);                      /* pos not upd */
    return sim_tape_ioerr (uptr);
    }
if ((!sim_tape_bot (uptr)) && 
    (((feof (uptr->fileref)) && (rdcnt < 3)) || /* eof? */
     ((awshdr.rectyp != AWS_REC) && (awshdr.rectyp != AWS_TMK)))) {
    MT_SET_PNU (uptr);                      /* pos not upd */
    return MTSE_INVRL;
    }
if (sim_tape_seek (uptr, uptr->pos))        /* set pos */
    return MTSE_IOERR;
replacing_record = (awshdr.nxtlen == (t_awslnt)bc) && (awshdr.rectyp == (bc ? AWS_REC : AWS_TMK));
awshdr.nxtlen = (t_awslnt)bc;
awshdr.rectyp = (bc) ? AWS_REC : AWS_TMK;
(void)sim_fwrite (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
if (bc)
    (void)sim_fwrite (buf, sizeof (uint8), bc, uptr->fileref);
uptr->pos += sizeof (awshdr) + bc;
if ((!replacing_record) || (bc == 0)) {
    awshdr.prelen = bc;
    awshdr.nxtlen = 0;
    awshdr.rectyp = AWS_TMK;
    (void)sim_fwrite (&awshdr, sizeof (t_awslnt), 3, uptr->fileref);
    if (!replacing_record)
        sim_set_fsize (uptr->fileref, uptr->pos + sizeof (awshdr));
    }
if (uptr->pos > uptr->tape_eom)
    uptr->tape_eom = uptr->pos;                     /* Update EOM if we're there */
return MTSE_OK;
}

/* Write metadata forward (internal routine) */

static t_stat sim_tape_wrdata (UNIT *uptr, uint32 dat)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

MT_CLR_PNU (uptr);
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return MTSE_UNATT;
if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
if (sim_tape_wrp (uptr))                                /* write prot? */
    return MTSE_WRP;
(void)sim_tape_seek (uptr, uptr->pos);                  /* set pos */
(void)sim_fwrite (&dat, sizeof (t_mtrlnt), 1, uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    MT_SET_PNU (uptr);
    return sim_tape_ioerr (uptr);
    }
sim_debug_unit (MTSE_DBG_STR, uptr, "wr_lnt: lnt: %d, pos: %" T_ADDR_FMT "u\n", dat, uptr->pos);
uptr->pos = uptr->pos + sizeof (t_mtrlnt);              /* move tape */
if (uptr->pos > uptr->tape_eom)
    uptr->tape_eom = uptr->pos;                         /* update EOM */
return MTSE_OK;
}

/* Write tape mark */

t_stat sim_tape_wrtmk (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_wrtmk(unit=%d)\n", (int)(uptr-ctx->dptr->units));
if (MT_GET_FMT (uptr) == MTUF_F_P7B) {                  /* P7B? */
    uint8 buf = P7B_EOF;                                /* eof mark */
    return sim_tape_wrrecf (uptr, &buf, 1);             /* write char */
    }
if (MT_GET_FMT (uptr) == MTUF_F_AWS)                    /* AWS? */
    return sim_tape_aws_wrdata (uptr, NULL, 0);
return sim_tape_wrdata (uptr, MTR_TMK);
}

t_stat sim_tape_wrtmk_a (UNIT *uptr, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_wrtmk (uptr);
AIO_CALL(TOP_WTMK, NULL, NULL, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}

/* Write end of medium */

t_stat sim_tape_wreom (UNIT *uptr)
{
t_stat result;
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_wreom(unit=%d)\n", (int)(uptr-ctx->dptr->units));
if (sim_tape_wrp (uptr))                                /* write prot? */
    return MTSE_WRP;
if (MT_GET_FMT (uptr) == MTUF_F_P7B)                    /* cant do P7B */
    return MTSE_FMT;
if (MT_GET_FMT (uptr) == MTUF_F_AWS) {
    sim_set_fsize (uptr->fileref, uptr->pos);
    result = MTSE_OK;
    }
else {
    result = sim_tape_wrdata (uptr, MTR_EOM);           /* write the EOM marker */
    uptr->pos = uptr->pos - sizeof (t_mtrlnt);              /* restore original tape position */
    }
MT_SET_PNU (uptr);                                      /* indicate that position was not updated */

return result;
}

t_stat sim_tape_wreom_a (UNIT *uptr, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_wreom (uptr);
AIO_CALL(TOP_WEOM, NULL, NULL, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}

/* Write end of medium-rewind */

t_stat sim_tape_wreomrw (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat r;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_wreomrw(unit=%d)\n", (int)(uptr-ctx->dptr->units));
r = sim_tape_wreom (uptr);
if (r == MTSE_OK)
    r = sim_tape_rewind (uptr);
return r;
}

t_stat sim_tape_wreomrw_a (UNIT *uptr, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_wreomrw (uptr);
AIO_CALL(TOP_WEMR, NULL, NULL, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}


/* Erase a gap in the forward direction (internal routine).

   An erase gap is written in the forward direction on the tape unit specified
   by "uptr" for the number of bytes specified by "bc".  The status of the
   operation is returned, and the file position is altered as follows:

     Exit Condition       File Position
     ------------------   ------------------
     unit unattached      unchanged
     unsupported format   unchanged
     write protected      unchanged
     read error           unchanged, PNU set
     write error          unchanged, PNU set
     gap written          updated

   If the requested byte count equals the metadatum size, then the routine
   succeeds only if it can overlay a single metadatum (i.e., a tape mark, an
   end-of-medium marker, or an existing erase gap marker); otherwise, the file
   position is not altered, PNU is set, and MTSE_INVRL (invalid record length)
   status is returned.

   An erase gap is represented in the tape image file by a special metadata
   value repeated throughout the gap.  The value is chosen so that it is still
   recognizable even if it has been "cut in half" by a subsequent data overwrite
   that does not end on a metadatum-sized boundary.  In addition, a range of
   metadata values are reserved for detection in the reverse direction.

   This implementation supports erasing gaps in the middle of a populated tape
   image and will always produce a valid image.  It also produces valid images
   when overwriting gaps with data records, with one exception: a data write
   that leaves only two bytes of gap remaining will produce an invalid tape.
   This limitation is deemed acceptable, as it is analogous to the existing
   limitation that data records cannot overwrite other data records without
   producing an invalid tape.

   To write an erase gap, the implementation uses one of two approaches,
   depending on whether or not the current tape position is at EOM.  Erasing at
   EOM presents no special difficulties; gap metadata markers are written for
   the prescribed number of bytes.  If the tape is not at EOM, then erasing must
   take into account the existing record structure to ensure that a valid tape
   image is maintained.

   The general approach is to erase for the nominal number of bytes but to
   increase that length, if necessary, to ensure that a partially overwritten
   data record at the end of the gap can be altered to maintain validity.
   Because the smallest legal tape record requires space for two metadata
   markers plus two data bytes, an erasure that would leave less than that
   is increased to consume the entire record.  Otherwise, the final record is
   truncated by rewriting the leading and trailing length words appropriately.

   When reading in either direction, gap metadata markers are ignored (skipped)
   until a record length header, EOF marker, EOM marker, or physical EOF is
   encountered.  Thus, tape images containing gap metadata are transparent to
   the calling simulator (unless tape runaway support is enabled -- see the
   notes at "sim_tape_rdlntf" for details).

   The permissibility of data record lengths that are not multiples of the
   metadatum size presents a difficulty when reading.  If such an "odd length"
   record is written over a gap, half of a metadata marker will exist
   immediately after the trailing record length.

   This condition is detected when reading forward by the appearance of a
   "reversed" marker.  The value appears reversed because the value is made up
   of half of one marker and half of the next.  This is handled by seeking
   forward two bytes to resync (the stipulation above that the overwrite cannot
   leave only two bytes of gap means that at least one "whole" metadata marker
   will follow).  Reading in reverse presents a more complex problem, because
   half of the marker is from the preceding trailing record length marker and
   therefore could be any of a range of values.  However, that range is
   restricted by the SIMH tape specification requirement that record length
   metadata values must have bits 30:24 set to zero.  This allows unambiguous
   detection of the condition.

   The value chosen for gap metadata and the values reserved for "half-gap"
   detection are:

     0xFFFFFFFE            - primary gap value
     0xFFFEFFFF            - reserved (indicates half-gap in forward reads)
     0xFFFF0000:0xFFFF00FF - reserved (indicates half-gap in reverse reads)
     0xFFFF8000:0xFFFF80FF - reserved (indicates half-gap in reverse reads)

   If the current tape format supports erase gaps, then this routine will write
   a gap of the requested size.  If the format does not, then no action will be
   taken, and MTSE_OK status will be returned.  This allows a device simulator
   that supports writing erase gaps to use the same code without worrying about
   the tape format currently selected by the user.  A request for an erase gap
   of zero length also succeeds with no action taken.


   Implementation notes:

    1. Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.
*/

static t_stat tape_erase_fwd (UNIT *uptr, t_mtrlnt gap_size)
{
size_t   xfer;
t_stat   st;
t_mtrlnt meta, sbc, new_len, rec_size;
uint32   file_size, marker_count;
int32    gap_needed = (int32) gap_size;                 /* the gap remaining to be allocated from the tape */
uint32   gap_alloc = 0;                                 /* the gap currently allocated from the tape */
const t_addr gap_pos = uptr->pos;                       /* the file position where the gap will start */
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */
const uint32 meta_size = sizeof (t_mtrlnt);             /* the number of bytes per metadatum */
const uint32 min_rec_size = 2 + sizeof (t_mtrlnt) * 2;  /* the smallest data record size */

MT_CLR_PNU (uptr);

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if (gap_size == 0 || format != MTUF_F_STD)         /* otherwise if zero length or gaps aren't supported */
    return MTSE_OK;                                     /*   then take no action */

file_size = sim_fsize (uptr->fileref);                  /* get the file size */

if (sim_tape_seek (uptr, uptr->pos)) {                  /* position the tape; if it fails */
    MT_SET_PNU (uptr);                                  /*   then set position not updated */
    return sim_tape_ioerr (uptr);                       /*     and quit with I/O error status */
    }

/* Read tape records and allocate them to the gap until the amount required is
   consumed.

   Read the next metadatum from tape:
    - EOF or EOM: allocate remainder of bytes needed.
    - TMK or GAP: allocate sizeof(metadatum) bytes.
    - Reverse GAP: allocate sizeof(metadatum) / 2 bytes.
    - Data record: see below.

   Loop until the bytes needed = 0.
*/

do {
    xfer = sim_fread (&meta, meta_size, 1, uptr->fileref);  /* read a metadatum */

    if (ferror (uptr->fileref)) {                       /* read error? */
        uptr->pos = gap_pos;                            /* restore original position */
        MT_SET_PNU (uptr);                              /* position not updated */
        return sim_tape_ioerr (uptr);                   /* translate error */
        }

    else if (xfer != 1 && feof (uptr->fileref) == 0) {  /* otherwise if a partial metadatum was read */
        uptr->pos = gap_pos;                            /*   then restore the original position */
        MT_SET_PNU (uptr);                              /* set the position-not-updated flag */
        return MTSE_INVRL;                              /*   and return an invalid record length error */
        }

    else                                                /* otherwise we had a good read */
        uptr->pos = uptr->pos + meta_size;              /*   so move the tape over the datum */

    if (feof (uptr->fileref) || (meta == MTR_EOM)) {    /* at eof or eom? */
        gap_alloc = gap_alloc + gap_needed;             /* allocate remainder */
        gap_needed = 0;
        }

    else if ((meta == MTR_GAP) || (meta == MTR_TMK)) {  /* gap or tape mark? */
        gap_alloc = gap_alloc + meta_size;              /* allocate marker space */
        gap_needed = gap_needed - meta_size;            /* reduce requirement */
        }

    else if (gap_size == meta_size) {                   /* otherwise if the request is for a single metadatum */
        uptr->pos = gap_pos;                            /*   then restore the original position */
        MT_SET_PNU (uptr);                              /* set the position-not-updated flag */
        return MTSE_INVRL;                              /*   and return an invalid record length error */
        }

    else if (meta == MTR_FHGAP) {                       /* half gap? */
        uptr->pos = uptr->pos - meta_size / 2;          /* backup to resync */

        if (sim_tape_seek (uptr, uptr->pos))            /* position the tape; if it fails */
            return sim_tape_ioerr (uptr);               /*   then quit with I/O error status */

        gap_alloc = gap_alloc + meta_size / 2;          /* allocate marker space */
        gap_needed = gap_needed - meta_size / 2;        /* reduce requirement */
        }

    else if (uptr->pos + MTR_L (meta) + meta_size > file_size) {    /* rec len out of range? */
        gap_alloc = gap_alloc + gap_needed;                         /* presume overwritten tape */
        gap_needed = 0;                                             /* allocate remainder */
        }

/* Allocate a data record:
    - Determine record size in bytes (including metadata)
    - If record size - bytes needed < smallest allowed record size,
      allocate entire record to gap, else allocate needed amount and
      truncate data record to reflect remainder.
*/

    else {                                              /* data record */
        sbc = MTR_L (meta);                             /* get record data length */
        rec_size = ((sbc + 1) & ~1) + meta_size * 2;    /* overall size in bytes */

        if (rec_size < gap_needed + min_rec_size) {         /* rec too small? */
            uptr->pos = uptr->pos - meta_size + rec_size;   /* position past record */

            if (sim_tape_seek (uptr, uptr->pos))            /* position the tape; if it fails */
                return sim_tape_ioerr (uptr);               /*   then quit with I/O error status */

            gap_alloc = gap_alloc + rec_size;               /* allocate record */
            gap_needed = gap_needed - rec_size;             /* reduce requirement */
            }

        else {                                              /* record size OK */
            uptr->pos = uptr->pos - meta_size + gap_needed; /* position to end of gap */
            new_len = MTR_F (meta) | (sbc - gap_needed);    /* truncate to new len */
            st = sim_tape_wrdata (uptr, new_len);           /* write new rec len */

            if (st != MTSE_OK) {                            /* write OK? */
                uptr->pos = gap_pos;                        /* restore orig pos */
                return st;                                  /* PNU was set by wrdata */
                }

            uptr->pos = uptr->pos + sbc - gap_needed;       /* position to end of data */
            st = sim_tape_wrdata (uptr, new_len);           /* write new rec len */

            if (st != MTSE_OK) {                            /* write OK? */
                uptr->pos = gap_pos;                        /* restore orig pos */
                return st;                                  /* PNU was set by wrdata */
                }

            gap_alloc = gap_alloc + gap_needed;             /* allocate remainder */
            gap_needed = 0;
            }
        }
    }
while (gap_needed > 0);                                 /* loop until all of the gap has been allocated */

uptr->pos = gap_pos;                                    /* reposition to gap start */

if (gap_alloc & (meta_size - 1)) {                      /* gap size "odd?" */
    st = sim_tape_wrdata (uptr, MTR_FHGAP);             /* write half gap marker */

    if (st != MTSE_OK) {                                /* write OK? */
        uptr->pos = gap_pos;                            /* restore orig pos */
        return st;                                      /* PNU was set by wrdata */
        }

    uptr->pos = uptr->pos - meta_size / 2;              /* realign position */
    gap_alloc = gap_alloc - 2;                          /* decrease gap to write */
    }

marker_count = gap_alloc / meta_size;                   /* count of gap markers */

do {
    st = sim_tape_wrdata (uptr, MTR_GAP);               /* write gap markers */

    if (st != MTSE_OK) {                                /* write OK? */
        uptr->pos = gap_pos;                            /* restore orig pos */
        return st;                                      /* PNU was set by wrdata */
        }
    }
while (--marker_count > 0);

return MTSE_OK;
}

/* Erase a gap in the reverse direction (internal routine).

   An erase gap is written in the reverse direction on the tape unit specified
   by "uptr" for the number of bytes specified by "bc".  The status of the
   operation is returned, and the file position is altered as follows:

     Exit Condition       File Position
     ------------------   ------------------
     unit unattached      unchanged
     unsupported format   unchanged
     write protected      unchanged
     read error           unchanged, PNU set
     write error          unchanged, PNU set
     gap written          updated

   If the requested byte count equals the metadatum size, then the routine
   succeeds only if it can overlay a single metadatum (i.e., a tape mark or an
   existing erase gap marker); otherwise, the file position is not altered, and
   MTSE_INVRL (invalid record length) status is returned.


   Implementation notes:

    1. Erase gaps are currently supported only in SIMH (MTUF_F_STD) tape format.

    2. Erasing a record in the reverse direction currently succeeds only if the
       gap requested occupies the same space as the record located immediately
       before the current file position.  This limitation may be lifted in a
       future update.

    3. The "sim_fread" call cannot return 0 in the absence of an error
       condition.  The preceding "sim_tape_bot" test ensures that "pos" >= 4, so
       "sim_fseek" will back up at least that far, so "sim_fread" will read at
       least one element.  If the call returns zero, an error must have
       occurred, so the "ferror" call must succeed.
*/

static t_stat tape_erase_rev (UNIT *uptr, t_mtrlnt gap_size)
{
const uint32 format = MT_GET_FMT (uptr);                /* the tape format */
const uint32 meta_size = sizeof (t_mtrlnt);             /* the number of bytes per metadatum */
t_stat   status;
t_mtrlnt rec_size, metadatum;
t_addr   gap_pos;
size_t   xfer;

MT_CLR_PNU (uptr);                                      /* clear the position-not-updated flag */

if ((uptr->flags & UNIT_ATT) == 0)                      /* if the unit is not attached */
    return MTSE_UNATT;                                  /*   then we cannot proceed */

else if (sim_tape_wrp (uptr))                           /* otherwise if the unit is write protected */
    return MTSE_WRP;                                    /*   then we cannot write */

else if ((gap_size == 0) || (format != MTUF_F_STD))     /* otherwise if the gap length is zero or unsupported */
    return MTSE_OK;                                     /*   then take no action */

gap_pos = uptr->pos;                                    /* save the starting position */

if (gap_size == meta_size) {                            /* if the request is for a single metadatum */
    if (sim_tape_bot (uptr))                            /*   then if the unit is positioned at the BOT */
        return MTSE_BOT;                                /*     then erasing backward is not possible */
    else                                                /*   otherwise */
        uptr->pos -= meta_size;                         /*     back up the file pointer */

    if (sim_tape_seek (uptr, uptr->pos))                /* position the tape; if it fails */
        return sim_tape_ioerr (uptr);                   /*   then quit with I/O error status */

    (void)sim_fread (&metadatum, meta_size, 1, uptr->fileref);/* read a metadatum */

    if (ferror (uptr->fileref))                             /* if a file I/O error occurred */
        return sim_tape_ioerr (uptr);                       /*   then report the error and quit */

    else if (metadatum == MTR_TMK)                          /* otherwise if a tape mark is present */
        if (sim_tape_seek (uptr, uptr->pos))                /*   then reposition the tape; if it fails */
            return sim_tape_ioerr (uptr);                   /*     then quit with I/O error status */

        else {                                              /*   otherwise */
            metadatum = MTR_GAP;                            /*     replace it with an erase gap marker */

            xfer = sim_fwrite (&metadatum, meta_size,   /* write the gap marker */
                               1, uptr->fileref);

            if (ferror (uptr->fileref) || (xfer == 0))  /* if a file I/O error occurred */
                return sim_tape_ioerr (uptr);           /* report the error and quit */
            else                                        /* otherwise the write succeeded */
                status = MTSE_OK;                       /*   so return success */
            }

    else if (metadatum == MTR_GAP)                      /* otherwise if a gap already exists */
        status = MTSE_OK;                               /*   then take no additional action */

    else {                                              /* otherwise a data record is present */
        uptr->pos = gap_pos;                            /*   so restore the starting position */
        return MTSE_INVRL;                              /*     and fail with invalid record length status */
        }
    }

else {                                                  /* otherwise it's an erase record request */
    status = sim_tape_rdlntr (uptr, &rec_size);         /*   so get the length of the preceding record */

    if ((status == MTSE_OK) &&                          /* if the read succeeded */
        (gap_size == rec_size + 2 * meta_size)) {       /*   and the gap will exactly overlay the record */
        gap_pos = uptr->pos;                            /*     then save the gap start position */

        status = tape_erase_fwd (uptr, gap_size);       /* erase the record */

        if (status == MTSE_OK)                          /* if the gap write succeeded */
            uptr->pos = gap_pos;                        /*   the reposition back to the start of the gap */
        }

    else {                                              /* otherwise the read failed or is the wrong size */
        uptr->pos = gap_pos;                            /*   so restore the starting position */

        if (status != MTSE_OK)                          /* if the record was not found */
            return status;                              /*   then return the failure reason */
        else                                            /* otherwise the record is the wrong size */
            return MTSE_INVRL;                          /*   so report an invalid record length */
        }
    }

return status;                                          /* return the status of the erase operation */
}

/* Write an erase gap.

   An erase gap is written in on the tape unit specified by "uptr" for the
   length specified by "gap_size" in tenths of an inch, and the status of the
   operation is returned.  The tape density must have been set via a previous
   sim_tape_set_dens call; if it has not, then no action is taken, and
   MTSE_IOERR is returned.

   If the requested gap length is zero, or the tape format currently selected
   does not support erase gaps, the call succeeds with no action taken.  This
   allows a device simulator that supports writing erase gaps to use the same
   code without worrying about the tape format currently selected by the user.

   Because SIMH tape images do not carry physical parameters (e.g., recording
   density), overwriting a tape image file containing a gap is problematic if
   the density setting is not the same as that used during recording.  There is
   no way to establish a gap of a certain length unequivocally in an image file,
   so this implementation establishes a gap of a certain number of bytes that
   reflect the desired gap length at the tape density in bits per inch used
   during writing.
*/

t_stat sim_tape_wrgap (UNIT *uptr, uint32 gaplen)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
const uint32 density = bpi [MT_DENS (uptr->dynflags)];  /* the tape density in bits per inch */
const uint32 byte_length = (gaplen * density) / 10;     /* the size of the requested gap in bytes */

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */

sim_debug_unit (ctx->dbit, uptr, "sim_tape_wrgap(unit=%d, gaplen=%u)\n", (int)(uptr-ctx->dptr->units), gaplen);

if (density == 0)                                       /* if the density has not been set */
    return MTSE_IOERR;                                  /*   then report an I/O error */
else                                                    /* otherwise */
    return tape_erase_fwd (uptr, byte_length);          /*   erase the requested gap size in bytes */
}

t_stat sim_tape_wrgap_a (UNIT *uptr, uint32 gaplen, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_wrgap (uptr, gaplen);
AIO_CALL(TOP_RDRR, NULL, NULL, NULL, 0, 0, gaplen, 0, NULL, callback);
return r;
}

/* Erase a record forward.

   An erase gap is written in the forward direction on the tape unit specified
   by "uptr" for a length corresponding to a record containing the number of
   bytes specified by "bc", and the status of the operation is returned.  The
   resulting gap will occupy "bc" bytes plus the size of the record length
   metadata.  This function may be used to erase a record of length "n" in place
   by requesting a gap of length "n".  After erasure, the tape will be
   positioned at the end of the gap.

   If a length of 0 is specified, then the metadatum marker at the current tape
   position will be erased.  If the tape is not positioned at a metadatum
   marker, the routine fails with MTSE_INVRL, and the tape position is
   unchanged.
*/

t_stat sim_tape_errecf (UNIT *uptr, t_mtrlnt bc)
{
const t_mtrlnt meta_size = sizeof (t_mtrlnt);           /* the number of bytes per metadatum */
const t_mtrlnt gap_size = bc + 2 * meta_size;           /* the requested gap size in bytes */

if (bc == 0)                                            /* if a zero-length erase is requested */
    return tape_erase_fwd (uptr, meta_size);            /*   then erase a metadatum marker */
else                                                    /* otherwise */
    return tape_erase_fwd (uptr, gap_size);             /*   erase the requested gap */
}

/* Erase a record reverse.

   An erase gap is written in the reverse direction on the tape unit specified
   by "uptr" for a length corresponding to a record containing the number of
   bytes specified by "bc", and the status of the operation is returned.  The
   resulting gap will occupy "bc" bytes plus the size of the record length
   metadata.  This function may be used to erase a record of length "n" in place
   by requesting a gap of length "n".  After erasure, the tape will be
   positioned at the start of the gap.

   If a length of 0 is specified, then the metadatum marker preceding the
   current tape position will be erased.  If the tape is not positioned after a
   metadatum marker, the routine fails with MTSE_INVRL, and the tape position is
   unchanged.
*/

t_stat sim_tape_errecr (UNIT *uptr, t_mtrlnt bc)
{
const t_mtrlnt meta_size = sizeof (t_mtrlnt);           /* the number of bytes per metadatum */
const t_mtrlnt gap_size = bc + 2 * meta_size;           /* the requested gap size in bytes */

if (bc == 0)                                            /* if a zero-length erase is requested */
    return tape_erase_rev (uptr, meta_size);            /*   then erase a metadatum marker */
else                                                    /* otherwise */
    return tape_erase_rev (uptr, gap_size);             /*   erase the requested gap */
}

/* Space record forward

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to size of record skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_sprecf (UNIT *uptr, t_mtrlnt *bc)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_sprecf(unit=%d)\n", (int)(uptr-ctx->dptr->units));

st = sim_tape_rdrlfwd (uptr, bc);                       /* get record length */
*bc = MTR_L (*bc);
return st;
}

t_stat sim_tape_sprecf_a (UNIT *uptr, t_mtrlnt *bc, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_sprecf (uptr, bc);
AIO_CALL(TOP_SPRF, NULL, bc, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}

/* Space records forward

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of records to skip
        skipped =       pointer to number of records actually skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_sprecsf (UNIT *uptr, uint32 count, uint32 *skipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;
t_mtrlnt tbc;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_sprecsf(unit=%d, count=%d)\n", (int)(uptr-ctx->dptr->units), count);

*skipped = 0;
while (*skipped < count) {                              /* loopo */
    st = sim_tape_sprecf (uptr, &tbc);                  /* spc rec */
    if (st != MTSE_OK)
        return st;
    *skipped = *skipped + 1;                            /* # recs skipped */
    }
return MTSE_OK;
}

t_stat sim_tape_sprecsf_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_sprecsf (uptr, count, skipped);
AIO_CALL(TOP_SRSF, NULL, skipped, NULL, 0, count, 0, 0, NULL, callback);
return r;
}

/* Space record reverse

   Inputs:
        uptr    =       pointer to tape unit
        bc      =       pointer to size of records skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated
*/

t_stat sim_tape_sprecr (UNIT *uptr, t_mtrlnt *bc)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_sprecr(unit=%d)\n", (int)(uptr-ctx->dptr->units));

if (MT_TST_PNU (uptr)) {
    MT_CLR_PNU (uptr);
    *bc = 0;
    return MTSE_OK;
    }
st = sim_tape_rdrlrev (uptr, bc);                       /* get record length */
*bc = MTR_L (*bc);
return st;
}

t_stat sim_tape_sprecr_a (UNIT *uptr, t_mtrlnt *bc, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_sprecr (uptr, bc);
AIO_CALL(TOP_SPRR, NULL, bc, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}

/* Space records reverse

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of records to skip
        skipped =       pointer to number of records actually skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated
*/

t_stat sim_tape_sprecsr (UNIT *uptr, uint32 count, uint32 *skipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;
t_mtrlnt tbc;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_sprecsr(unit=%d, count=%d)\n", (int)(uptr-ctx->dptr->units), count);

*skipped = 0;
while (*skipped < count) {                              /* loopo */
    st = sim_tape_sprecr (uptr, &tbc);                  /* spc rec rev */
    if (st != MTSE_OK)
        return st;
    *skipped = *skipped + 1;                            /* # recs skipped */
    }
return MTSE_OK;
}

t_stat sim_tape_sprecsr_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_sprecsr (uptr, count, skipped);
AIO_CALL(TOP_SRSR, NULL, skipped, NULL, 0, count, 0, 0, NULL, callback);
return r;
}

/* Space files forward by record

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of files to skip
        skipped =       pointer to number of files actually skipped
        recsskipped =   pointer to number of records skipped
        check_leot =    flag to detect and stop skip between two successive tape marks
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_spfilebyrecf (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, t_bool check_leot)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;
t_bool last_tapemark = FALSE;
uint32 filerecsskipped;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_spfilebyrecf(unit=%d, count=%d, check_leot=%d)\n", (int)(uptr-ctx->dptr->units), count, check_leot);

if (check_leot) {
    t_mtrlnt rbc;

    st = sim_tape_rdrlrev (uptr, &rbc);
    last_tapemark = (MTSE_TMK == st);
    if ((st == MTSE_OK) || (st == MTSE_TMK))
        sim_tape_rdrlfwd (uptr, &rbc);
    }
*skipped = 0;
*recsskipped = 0;
while (*skipped < count) {                              /* loopo */
    while (1) {
        st = sim_tape_sprecsf (uptr, 0x1ffffff, &filerecsskipped);/* spc recs */
        *recsskipped += filerecsskipped;
        if (st != MTSE_OK)
            break;
        }
    if (st == MTSE_TMK) {
        *skipped = *skipped + 1;                        /* # files skipped */
        if (check_leot && (filerecsskipped == 0) && last_tapemark) {
            uint32 filefileskipped;
            sim_tape_spfilebyrecr (uptr, 1, &filefileskipped, &filerecsskipped);
            *skipped = *skipped - 1;                    /* adjust # files skipped */
            return MTSE_LEOT;
            }
        last_tapemark = TRUE;
        }
    else
        return st;
    }
return MTSE_OK;
}

t_stat sim_tape_spfilebyrecf_a (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, t_bool check_leot, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_spfilebyrecf (uptr, count, skipped, recsskipped, check_leot);
AIO_CALL(TOP_SFRF, NULL, skipped, recsskipped, check_leot, count, 0, 0, NULL, callback);
return r;
}

/* Space files forward

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of files to skip
        skipped =       pointer to number of files actually skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   read error           unchanged, PNU set
   end of file/medium   unchanged, PNU set
   tape mark            updated
   data record          updated
   data record error    updated
*/

t_stat sim_tape_spfilef (UNIT *uptr, uint32 count, uint32 *skipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
uint32 totalrecsskipped;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_spfilef(unit=%d, count=%d)\n", (int)(uptr-ctx->dptr->units), count);

return sim_tape_spfilebyrecf (uptr, count, skipped, &totalrecsskipped, FALSE);
}

t_stat sim_tape_spfilef_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_spfilef (uptr, count, skipped);
AIO_CALL(TOP_SPFF, NULL, skipped, NULL, 0, count, 0, 0, NULL, callback);
return r;
}

/* Space files reverse by record

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of files to skip
        skipped =       pointer to number of files actually skipped
        recsskipped =   pointer to number of records skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated
*/

t_stat sim_tape_spfilebyrecr (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat st;
uint32 filerecsskipped;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_spfilebyrecr(unit=%d, count=%d)\n", (int)(uptr-ctx->dptr->units), count);

*skipped = 0;
*recsskipped = 0;
while (*skipped < count) {                              /* loopo */
    while (1) {
        st = sim_tape_sprecsr (uptr, 0x1ffffff, &filerecsskipped);/* spc recs rev */
        *recsskipped += filerecsskipped;
        if (st != MTSE_OK)
            break;
        }
    if (st == MTSE_TMK)
        *skipped = *skipped + 1;                        /* # files skipped */
    else
        return st;
    }
return MTSE_OK;
}

t_stat sim_tape_spfilebyrecr_a (UNIT *uptr, uint32 count, uint32 *skipped, uint32 *recsskipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_spfilebyrecr (uptr, count, skipped, recsskipped);
AIO_CALL(TOP_SPFR, NULL, skipped, recsskipped, 0, count, 0, 0, NULL, callback);
return r;
}

/* Space files reverse

   Inputs:
        uptr    =       pointer to tape unit
        count   =       count of files to skip
        skipped =       pointer to number of files actually skipped
   Outputs:
        status  =       operation status

   exit condition       position

   unit unattached      unchanged
   beginning of tape    unchanged
   read error           unchanged
   end of file          unchanged
   end of medium        updated
   tape mark            updated
   data record          updated
*/

t_stat sim_tape_spfiler (UNIT *uptr, uint32 count, uint32 *skipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
uint32 totalrecsskipped;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_spfiler(unit=%d, count=%d)\n", (int)(uptr-ctx->dptr->units), count);

return sim_tape_spfilebyrecr (uptr, count, skipped, &totalrecsskipped);
}

t_stat sim_tape_spfiler_a (UNIT *uptr, uint32 count, uint32 *skipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_spfiler (uptr, count, skipped);
AIO_CALL(TOP_SPFR, NULL, skipped, NULL, 0, count, 0, 0, NULL, callback);
return r;
}

/* Rewind tape */

t_stat sim_tape_rewind (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

if (uptr->flags & UNIT_ATT) {
    if (ctx == NULL)                                    /* if not properly attached? */
        return sim_messagef (SCPE_IERR, "Bad Attach\n");/*   that's a problem */
    sim_debug_unit (ctx->dbit, uptr, "sim_tape_rewind(unit=%d)\n", (int)(uptr-ctx->dptr->units));
    }
uptr->pos = 0;
if (uptr->flags & UNIT_ATT) {
    (void)sim_tape_seek (uptr, uptr->pos);
    }
MT_CLR_PNU (uptr);
MT_CLR_INMRK (uptr);                                    /* Not within a TAR tapemark */
return MTSE_OK;
}

t_stat sim_tape_rewind_a (UNIT *uptr, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_rewind (uptr);
AIO_CALL(TOP_RWND, NULL, NULL, NULL, 0, 0, 0, 0, NULL, callback);
return r;
}

/* Position Tape */

t_stat sim_tape_position (UNIT *uptr, uint32 flags, uint32 recs, uint32 *recsskipped, uint32 files, uint32 *filesskipped, uint32 *objectsskipped)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;
t_stat r = MTSE_OK;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_position(unit=%d, flags=0x%X, recs=%d, files=%d)\n", (int)(uptr-ctx->dptr->units), flags, recs, files);

*recsskipped = *filesskipped = *objectsskipped = 0;
if (flags & MTPOS_M_REW)
    r = sim_tape_rewind (uptr);
if (r != MTSE_OK)
    return r;
if (flags & MTPOS_M_OBJ) {
    uint32 objs = recs;
    uint32 skipped;
    uint32 objsremaining = objs;

    while (*objectsskipped < objs) {                       /* loopo */
        if (flags & MTPOS_M_REV)                        /* reverse? */
            r = sim_tape_sprecsr (uptr, objsremaining, &skipped);
        else
            r = sim_tape_sprecsf (uptr, objsremaining, &skipped);
        objsremaining = objsremaining - (skipped + ((r == MTSE_TMK) ? 1 : 0));
        if ((r == MTSE_TMK) || (r == MTSE_OK))
            *objectsskipped = *objectsskipped + skipped + ((r == MTSE_TMK) ? 1 : 0);
        else
            return r;
        }
    r = MTSE_OK;
    }
else {
    uint32 fileskiprecs;

    if (flags & MTPOS_M_REV)                            /* reverse? */
        r = sim_tape_spfilebyrecr (uptr, files, filesskipped, &fileskiprecs);
    else
        r = sim_tape_spfilebyrecf (uptr, files, filesskipped, &fileskiprecs, (flags & MTPOS_M_DLE));
    if (r != MTSE_OK)
        return r;
    if (flags & MTPOS_M_REV)                            /* reverse? */
        r = sim_tape_sprecsr (uptr, recs, recsskipped);
    else
        r = sim_tape_sprecsf (uptr, recs, recsskipped);
    if (r == MTSE_TMK)
        *filesskipped = *filesskipped + 1;
    *objectsskipped = fileskiprecs + *filesskipped + *recsskipped;
    }
return r;
}

t_stat sim_tape_position_a (UNIT *uptr, uint32 flags, uint32 recs, uint32 *recsskipped, uint32 files, uint32 *filesskipped, uint32 *objectsskipped, TAPE_PCALLBACK callback)
{
t_stat r = MTSE_OK;
AIO_CALLSETUP
    r = sim_tape_position (uptr, flags, recs, recsskipped, files, filesskipped, objectsskipped);
AIO_CALL(TOP_POSN, NULL, recsskipped, filesskipped, 0, flags, recs, files, objectsskipped, callback);
return r;
}

/* Reset tape */

t_stat sim_tape_reset (UNIT *uptr)
{
struct tape_context *ctx = (struct tape_context *)uptr->tape_ctx;

MT_CLR_PNU (uptr);
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;

if (ctx == NULL)                                        /* if not properly attached? */
    return sim_messagef (SCPE_IERR, "Bad Attach\n");    /*   that's a problem */
sim_debug_unit (ctx->dbit, uptr, "sim_tape_reset(unit=%d)\n", (int)(uptr-ctx->dptr->units));

_sim_tape_io_flush(uptr);
AIO_VALIDATE(uptr);
AIO_UPDATE_QUEUE;
return SCPE_OK;
}

/* Test for BOT */

t_bool sim_tape_bot (UNIT *uptr)
{
uint32 f = MT_GET_FMT (uptr);

return ((uptr->pos <= fmts[f].bot) && (!MT_TST_INMRK (uptr))) ? TRUE: FALSE;
}

/* Test for end of tape */

t_bool sim_tape_eot (UNIT *uptr)
{
return (uptr->capac && (uptr->pos >= uptr->capac))? TRUE: FALSE;
}

/* Test for write protect */

t_bool sim_tape_wrp (UNIT *uptr)
{
return ((uptr->flags & MTUF_WRP) || (uptr->flags & UNIT_RO) || (MT_GET_FMT (uptr) == MTUF_F_TPC))? TRUE: FALSE;
}

/* Process I/O error */

static t_stat sim_tape_ioerr (UNIT *uptr)
{
sim_printf ("%s: Magtape library I/O error: %s\n", sim_uname (uptr), strerror (errno));
clearerr (uptr->fileref);
return MTSE_IOERR;
}

/* Set tape format */

t_stat sim_tape_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 f;

if (uptr == NULL)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (cptr == NULL)
    return SCPE_ARG;
for (f = 0; fmts[f].name; f++) {
    if (MATCH_CMD(fmts[f].name, cptr) == 0) {
        uint32 a = 0;

        if (f == MTUF_F_ANSI) {
            for (a = 0; ansi_args[a].name; a++)
                if (MATCH_CMD(ansi_args[a].name, cptr) == 0)
                    break;
            if (ansi_args[a].name == NULL)
                return sim_messagef (SCPE_ARG, "Unknown ANSI tape format: %s\n", cptr);
            }
        uptr->flags &= ~UNIT_RO;
        uptr->flags |= fmts[f].uflags;
        uptr->dynflags &= ~UNIT_M_TAPE_FMT;
        uptr->dynflags |= (f << UNIT_V_TAPE_FMT);
        uptr->dynflags &= ~UNIT_M_TAPE_ANSI;
        uptr->dynflags |= (a << UNIT_V_TAPE_ANSI);
        return SCPE_OK;
        }
    }
return sim_messagef (SCPE_ARG, "Unknown tape format: %s\n", cptr);
}

/* Show tape format */

t_stat sim_tape_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s format", _sim_tape_format_name (uptr));
return SCPE_OK;
}

/* Map a TPC format tape image */

static uint32 sim_tape_tpc_map (UNIT *uptr, t_addr *map, uint32 mapsize)
{
t_addr tpos, leot;
t_addr tape_size;
t_tpclnt bc, last_bc = TPC_EOM;
uint32 had_double_tape_mark = 0;
size_t i;
uint32 objc, sizec;
uint32 *countmap = NULL;
uint8 *recbuf = NULL;
DEVICE *dptr = find_dev_from_unit (uptr);

if ((uptr == NULL) || (uptr->fileref == NULL))
    return 0;
countmap = (uint32 *)calloc (65536, sizeof(*countmap));
recbuf = (uint8 *)malloc (65536);
tape_size = (t_addr)sim_fsize (uptr->fileref);
sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: tape_size: %" T_ADDR_FMT "u\n", tape_size);
for (objc = 0, sizec = 0, tpos = 0;; ) {
    (void)sim_tape_seek (uptr, tpos);
    i = sim_fread (&bc, sizeof (bc), 1, uptr->fileref);
    if (i == 0)     /* past or at eof? */
        break;
    if (bc > 65535) /* Range check length value to satisfy Coverity */
        break;
    if (countmap[bc] == 0)
        sizec++;
    ++countmap[bc];
    if (map && (objc < mapsize))
        map[objc] = tpos;
    if (bc) {
        sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: %d byte count at pos: %" T_ADDR_FMT "u\n", bc, tpos);
        if (map && sim_deb && (dptr->dctrl & MTSE_DBG_STR)) {
            (void)sim_fread (recbuf, 1, bc, uptr->fileref);
            sim_data_trace(dptr, uptr, (((uptr->dctrl | dptr->dctrl) & MTSE_DBG_DAT) ? recbuf : NULL), "", bc, "Data Record", MTSE_DBG_STR);
            }
        }
    else
        sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: tape mark at pos: %" T_ADDR_FMT "u\n", tpos);
    objc++;
    tpos = tpos + ((bc + 1) & ~1) + sizeof (t_tpclnt);
    if ((bc == 0) && (last_bc == 0)) {  /* double tape mark? */
        had_double_tape_mark = objc;
        leot = tpos;
        }
    last_bc = bc;
    }
sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: objc: %u, different record sizes: %u\n", objc, sizec);
for (i=0; i<65535; i++) {
    if (countmap[i]) {
        if (i == 0)
            sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: summary - %u tape marks\n", countmap[i]);
        else
            sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: summary - %u %d byte record%s\n", countmap[i], (int)i, (countmap[i] > 1) ? "s" : "");
        }
    }
if (((last_bc != TPC_EOM) && 
     (tpos > tape_size) &&
     (!had_double_tape_mark))    ||
    (!had_double_tape_mark)      ||
    ((objc == countmap[0]) && 
     (countmap[0] != 2))) {     /* Unreasonable format? */
    if (last_bc != TPC_EOM)
        sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: ERROR unexpected EOT byte count: %d\n", last_bc);
    if (tpos > tape_size)
        sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: ERROR next record position %" T_ADDR_FMT "u beyond EOT: %" T_ADDR_FMT "u\n", tpos, tape_size);
    if (objc == countmap[0])
        sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: ERROR tape cnly contains tape marks\n");
    free (countmap);
    free (recbuf);
    return 0;
    }

if ((last_bc != TPC_EOM) && (tpos > tape_size)) {
    sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: WARNING unexpected EOT byte count: %d, double tape mark before %" T_ADDR_FMT "u provides logical EOT\n", last_bc, leot);
    objc = had_double_tape_mark;
    tpos = leot;
    }
if (map)
    map[objc] = tpos;
sim_debug_unit (MTSE_DBG_STR, uptr, "tpc_map: OK objc: %d\n", objc);
free (countmap);
free (recbuf);
return objc;
}

static 
const char *sim_tape_error_text (t_stat stat)
{
const char *mtse_errors[] = {
    "no error",
    "tape mark",
    "unattached",
    "I/O error",
    "invalid record length",
    "invalid format",
    "beginning of tape",
    "end of medium",
    "error in record",
    "write protected",
    "Logical End Of Tape",
    "tape runaway"
    };
static char msgbuf[64];

if (stat <= MTSE_MAX_ERR)
    return mtse_errors[stat];
sprintf(msgbuf, "Error %d", stat);
return msgbuf;
}

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

static t_stat sim_tape_validate_tape (UNIT *uptr)
{
t_addr saved_pos = uptr->pos;
uint32 record_in_file = 0;
uint32 data_total = 0;
uint32 tapemark_total = 0;
uint32 record_total = 0;
uint32 unique_record_sizes = 0;
uint32 remaining_data = 0;
uint32 gaps = 0;
uint32 gap_bytes = 0;
uint32 *rec_sizes = NULL;
t_stat r = SCPE_OK;
t_stat r_f;
t_stat r_r;
t_stat r_s;
uint8 *buf_f = NULL;
uint8 *buf_r = NULL;
t_mtrlnt bc_f;
t_mtrlnt bc_r;
t_mtrlnt bc_s;
t_mtrlnt bc;
t_addr pos_f;
t_addr pos_r;
t_addr pos_fa;
t_addr pos_sa;
t_mtrlnt max = MTR_MAXLEN;

if (!(uptr->flags & UNIT_ATT))
    return SCPE_UNATT;
buf_f = (uint8 *)calloc (1, max);
if (buf_f == NULL)
    return SCPE_MEM;
buf_r = (uint8 *)calloc (1, max);
if (buf_r == NULL) {
    free (buf_f);
    return SCPE_MEM;
    }
rec_sizes = (uint32 *)calloc (max + 1, sizeof (*rec_sizes));
if (rec_sizes == NULL) {
    free (buf_f);
    free (buf_r);
    return SCPE_MEM;
    }

r = sim_tape_rewind (uptr);
while (r == SCPE_OK) {
    if (stop_cpu) { /* SIGINT? */
        stop_cpu = FALSE;
        break;
        }
    pos_f = uptr->pos;
    r_f = sim_tape_rdrecf (uptr, buf_f, &bc_f, max);
    pos_fa = uptr->pos;
    switch (r_f) {
    case MTSE_OK:                                   /* no error */
    case MTSE_TMK:                                  /* tape mark */
        if (r_f == MTSE_OK)
            ++record_total;
        else
            ++tapemark_total;
        data_total += bc_f;
        if (bc_f != 0) {
            if (rec_sizes[bc_f] == 0)
                ++unique_record_sizes;
            ++rec_sizes[bc_f];
            }
        r_r = sim_tape_rdrecr (uptr, buf_r, &bc_r, max);
        pos_r = uptr->pos;
        if (r_r != r_f) {
            sim_printf ("Forward Record Read returned: %s, Reverse read returned: %s\n", sim_tape_error_text (r_f), sim_tape_error_text (r_r));
            r = MAX(r_f, r_r);
            break;
            }
        if (bc_f != bc_r) {
            sim_printf ("Forward Record Read record length: %d, Reverse read record length: %d\n", bc_f, bc_r);
            r = MTSE_RECE;
            break;
            }
        if (0 != memcmp (buf_f, buf_r, bc_f)) {
            sim_printf ("%d byte record contents differ when read forward amd backwards start from position %" T_ADDR_FMT "u\n", bc_f, pos_f);
            r = MTSE_RECE;
            break;
            }
        memset (buf_f, 0, bc_f);
        memset (buf_r, 0, bc_r);
        if (pos_f != pos_r) {
            if (MT_GET_FMT (uptr) == MTUF_F_STD) {
                ++gaps;
                gap_bytes += (uint32)(pos_r - pos_f);
                }
            else {
                sim_printf ("Unexpected tape file position between forward and reverse record read: (%" T_ADDR_FMT "u, %" T_ADDR_FMT "u)\n", pos_f, pos_r);
                r = MTSE_RECE;
                break;
                }
            }
        r_s = sim_tape_sprecf (uptr, &bc_s);
        pos_sa = uptr->pos;
        if (r_s != r_f) {
            sim_printf ("Unexpected Space Record Status: %s vs %s\n", sim_tape_error_text (r_s), sim_tape_error_text (r_f));
            r = MAX(r_s, r_f);
            break;
            }
        if (bc_s != bc_f) {
            sim_printf ("Unexpected Space Record Length: %d vs %d\n", bc_s, bc_f);
            r = MTSE_RECE;
            break;
            }
        if (pos_fa != pos_sa) {
            sim_printf ("Unexpected tape file position after forward and skip record: (%" T_ADDR_FMT "u, %" T_ADDR_FMT "u)\n", pos_fa, pos_sa);
            break;
            }
        r = SCPE_OK;
        break;
    case MTSE_INVRL:                                /* invalid rec lnt */
    case MTSE_FMT:                                  /* invalid format */
    case MTSE_BOT:                                  /* beginning of tape */
    case MTSE_RECE:                                 /* error in record */
    case MTSE_WRP:                                  /* write protected */
    case MTSE_LEOT:                                 /* Logical End Of Tape */
    case MTSE_RUNAWAY:                              /* tape runaway */
    default:
        r = r_f;
        break;
    case MTSE_EOM:                                  /* end of medium */
        r = r_f;
        break;
        }
    }
uptr->tape_eom = uptr->pos;
if ((!stop_cpu) &&
    ((r != MTSE_EOM) || (sim_switches & SWMASK ('V')) || (sim_switches & SWMASK ('L')) ||
     ((uint32)(sim_tape_size (uptr) - (t_offset)uptr->pos) > fmts[MT_GET_FMT (uptr)].eom_remnant) ||
     (unique_record_sizes > 2 * tapemark_total))) {
    remaining_data = (uint32)(sim_tape_size (uptr) - (t_offset)uptr->tape_eom);
    sim_messagef (SCPE_OK, "Tape Image %s'%s' scanned as %s format.\n", ((MT_GET_FMT (uptr) == MTUF_F_ANSI) ? "made from " : ""), uptr->filename, (MT_GET_FMT (uptr) == MTUF_F_ANSI) ? ansi_args[MT_GET_ANSI_TYP (uptr)].name : fmts[MT_GET_FMT (uptr)].name);
    sim_messagef (SCPE_OK, "%s %u bytes of tape data (%u records, %u tapemarks)\n",
                           (r != MTSE_EOM) ? "After processing" : "contains", data_total, record_total, tapemark_total);
    if ((record_total > 0) && (sim_switches & SWMASK ('L'))) {
        sim_messagef (SCPE_OK, "Comprising %d different sized records (in record size order):\n", unique_record_sizes);
        for (bc = 0; bc <= max; bc++) {
            if (rec_sizes[bc])
                sim_messagef (SCPE_OK, "%8u %u byte record%s\n", rec_sizes[bc], (uint32)bc, (rec_sizes[bc] != 1) ? "s" : "");
            }
        if (gaps)
            sim_messagef (SCPE_OK, "%8u gap%s totalling %u bytes %s seen\n", gaps, (gaps != 1) ? "s" : "", gap_bytes, (gaps != 1) ? "were" : "was");
        }
    if (r != MTSE_EOM)
        sim_messagef (SCPE_OK, "Read Tape Record Returned Unexpected Status: %s\n", sim_tape_error_text (r));
    if (remaining_data > fmts[MT_GET_FMT (uptr)].eom_remnant)
        sim_messagef (SCPE_OK, "%u bytes of unexamined data remain in the tape image file\n", remaining_data);
    }
if ((!stop_cpu) && 
    (unique_record_sizes > 2 * tapemark_total)) {
    sim_messagef (SCPE_OK, "A potentially unreasonable number of record sizes(%u) vs tape marks (%u) have been found\n", unique_record_sizes, tapemark_total);
    sim_messagef (SCPE_OK, "The tape format (%s) might not be correct for the '%s' tape image\n", fmts[MT_GET_FMT (uptr)].name, uptr->filename);
    }

free (buf_f);
free (buf_r);
free (rec_sizes);
uptr->pos = saved_pos;
(void)sim_tape_seek (uptr, uptr->pos);
return SCPE_OK;
}

/* Find the preceding record in a TPC file */

static t_addr sim_tape_tpc_fnd (UNIT *uptr, t_addr *map)
{
uint32 lo, hi, p;


if (map == NULL)
    return 0;
lo = 0;
hi = uptr->hwmark - 1;
do {
    p = (lo + hi) >> 1;
    if (uptr->pos == map[p])
        return ((p == 0)? map[p]: map[p - 1]);
    else {
        if (uptr->pos < map[p])
            hi = p - 1;
        else
            lo = p + 1;
        }
    }
while (lo <= hi);
return ((p == 0)? map[p]: map[p - 1]);
}

/* Set tape capacity */

t_stat sim_tape_set_capac (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_addr cap;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
cap = (t_addr) get_uint (cptr, 10, sim_taddr_64? 2000000: 2000, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
uptr->capac = cap * ((t_addr) 1000000);
return SCPE_OK;
}

/* Show tape capacity */

t_stat sim_tape_show_capac (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (uptr->capac) {
    if (uptr->capac >= (t_addr) 1000000)
        fprintf (st, "capacity=%dMB", (uint32) (uptr->capac / ((t_addr) 1000000)));
    else {
        if (uptr->capac >= (t_addr) 1000)
            fprintf (st, "capacity=%dKB", (uint32) (uptr->capac / ((t_addr) 1000)));
        else
            fprintf (st, "capacity=%dB", (uint32) uptr->capac);
        }
    }
else
    fprintf (st, "unlimited capacity");
return SCPE_OK;
}

/* Set the tape density.

   Set the density of the specified tape unit either to the value supplied or to
   the value represented by the supplied character string.

   If "desc" is NULL, then "val" must be set to one of the MT_DENS_* constants
   in sim_tape.h other than MT_DENS_NONE; the supplied value is used as the tape
   density, and the character string is ignored.  Otherwise, "desc" must point
   at an int32 value containing a set of allowed densities constructed as a
   bitwise OR of the appropriate MT_*_VALID values.  In this case, the string
   pointed to by "cptr" will be parsed for a decimal value corresponding to the
   desired density in bits per inch and validated against the set of allowed
   values.

   In either case, SCPE_ARG is returned if the density setting is not valid or
   allowed.  If the setting is OK, the new density is set into the unit
   structure, and SCPE_OK is returned.
*/

t_stat sim_tape_set_dens (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 density, new_bpi;
t_stat result = SCPE_OK;

if (uptr == NULL)                                               /* if the unit pointer is null */
    return SCPE_IERR;                                           /*   then the caller has screwed up */

else if (desc == NULL)                                          /* otherwise if a validation set was not supplied */
    if (val > 0 && val < (int32) BPI_COUNT)                     /*   then if a valid density code was supplied */
        uptr->dynflags = (uptr->dynflags & ~MTVF_DENS_MASK)     /*     then insert the code */
                           | (val << UNIT_V_DF_TAPE);           /*       in the unit flags */
    else                                                        /*   otherwise the code is invalid */
        return SCPE_ARG;                                        /*     so report a bad argument */

else {                                                          /* otherwise a validation set was supplied */
    if (cptr == NULL || *cptr == 0)                             /*   but if no value is present */
        return SCPE_MISVAL;                                     /*     then report a missing value */

    new_bpi = (uint32) get_uint (cptr, 10, UINT_MAX, &result);  /* convert the string value */

    if (result != SCPE_OK)                                      /* if the conversion failed */
        result = SCPE_ARG;                                      /*   then report a bad argument */

    else for (density = 0; density < BPI_COUNT; density++)      /* otherwise validate the density */
        if (new_bpi == bpi [density]                            /* if it matches a value in the list */
          && ((1 << density) & *(const int32 *) desc)) {        /*   and it's an allowed value */
            uptr->dynflags = (uptr->dynflags & ~MTVF_DENS_MASK) /*     then store the index of the value */
                               | density << UNIT_V_DF_TAPE;     /*       in the unit flags */
            return SCPE_OK;                                     /*         and return success */
            }

    result = SCPE_ARG;                                          /* if no match, then report a bad argument */
    }

return result;                                                  /* return the result of the operation */
}

/* Show the tape density */

t_stat sim_tape_show_dens (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 tape_density;

if (uptr == NULL)                                       /* if the unit pointer is null */
    return SCPE_IERR;                                   /*   then the caller has screwed up */

else {                                                  /* otherwise get the density */
    tape_density = bpi [MT_DENS (uptr->dynflags)];      /*   of the tape from the unit flags */

    if (tape_density)                                   /* if it's set */
        fprintf (st, "density=%d bpi", tape_density);   /*   then report it */
    else                                                /* otherwise */
        fprintf (st, "density not set");                /*   it was never set by the caller */
    }

return SCPE_OK;
}

/* list supported densities

   translates the mask of supported densities to a string list in the form: 
   
           "(800|1600|6250)"

   this string may be useful to construct a MTAB help string for a 
   SET <unit> DENSITY= command.

*/

t_stat sim_tape_density_supported (char *string, size_t string_size, int32 valid_bits)
{
uint32 density;
int32 count;

strlcpy (string, "", string_size);
if ((!valid_bits) || (valid_bits >> BPI_COUNT))
    return SCPE_ARG;
for (density = count = 0; density < BPI_COUNT; density++) {
    if (valid_bits & (1 << density)) {
        char density_str[20];

        ++count;
        if (count == 1)
            strlcat (string, "{", string_size);
        else
            strlcat (string, "|", string_size);
        sprintf (density_str, "%d", bpi[density]);
        strlcat (string, density_str, string_size);
        }
    }
if ((count == 1) && (string_size > 1))
    memmove (string, string + 1, strlen (string));
else
    strlcat (string, "}", string_size);
return SCPE_OK;
}

static DEBTAB tape_debug[] = {
  {"TRACE",     MTSE_DBG_API,       "API Trace"},
  {"DATA",      MTSE_DBG_DAT,       "Tape Data"},
  {"POS",       MTSE_DBG_POS,       "Positioning Activities"},
  {"STR",       MTSE_DBG_STR,       "Tape Structure"},
  {0}
};

t_stat sim_tape_add_debug (DEVICE *dptr)
{
if (DEV_TYPE(dptr) != DEV_TAPE)
    return SCPE_OK;
return sim_add_debug_flags (dptr, tape_debug);
}

static t_bool p7b_parity_inited = FALSE;
static uint8 p7b_odd_parity[64];
static uint8 p7b_even_parity[64];

static t_stat sim_tape_test_create_tape_files (UNIT *uptr, const char *filename, int files, int records, int max_size)
{
FILE *fSIMH = NULL;
FILE *fE11 = NULL;
FILE *fTPC = NULL;
FILE *fP7B = NULL;
FILE *fAWS = NULL;
FILE *fAWS2 = NULL;
FILE *fAWS3 = NULL;
FILE *fTAR = NULL;
FILE *fTAR2 = NULL;
int i, j, k;
t_tpclnt tpclnt;
t_mtrlnt mtrlnt;
t_awslnt awslnt;
t_awslnt awslnt_last = 0;
t_awslnt awsrec_typ = AWS_REC;
char name[256];
t_stat stat = SCPE_OPENERR;
uint8 *buf = NULL;
t_stat aws_stat;
int32 saved_switches = sim_switches;

srand (0);                      /* All devices use the same random sequence for file data */
if (max_size == 0)
    max_size = 65535;
if (!p7b_parity_inited) {
    for (i=0; i < 64; i++) {
        int bit_count = 0;

        for (j=0; j<6; j++) {
            if (i & (1 << j))
                ++bit_count;
            }
        p7b_odd_parity[i] = i | ((~bit_count & 1) << 6);
        p7b_even_parity[i] = i | ((bit_count & 1) << 6);
        }
    p7b_parity_inited = TRUE;
    }
buf = (uint8 *)malloc (65536);
if (buf == NULL)
    return SCPE_MEM;
sprintf (name, "%s.simh", filename);
fSIMH = fopen (name, "wb");
if (fSIMH  == NULL)
    goto Done_Files;
sprintf (name, "%s.e11", filename);
fE11 = fopen (name, "wb");
if (fE11  == NULL)
    goto Done_Files;
sprintf (name, "%s.tpc", filename);
fTPC = fopen (name, "wb");
if (fTPC  == NULL)
    goto Done_Files;
sprintf (name, "%s.p7b", filename);
fP7B = fopen (name, "wb");
if (fP7B  == NULL)
    goto Done_Files;
sprintf (name, "%s.tar", filename);
fTAR = fopen (name, "wb");
if (fTAR  == NULL)
    goto Done_Files;
sprintf (name, "%s.2.tar", filename);
fTAR2 = fopen (name, "wb");
if (fTAR2  == NULL)
    goto Done_Files;
sprintf (name, "%s.aws", filename);
fAWS = fopen (name, "wb");
if (fAWS  == NULL)
    goto Done_Files;
sprintf (name, "%s.2.aws", filename);
fAWS2 = fopen (name, "wb");
if (fAWS2  == NULL)
    goto Done_Files;
sprintf (name, "%s.3.aws", filename);
fAWS3 = fopen (name, "wb");
if (fAWS3  == NULL)
    goto Done_Files;
sprintf (name, "aws %s.aws.tape", filename);
(void)remove (name);
sim_switches = SWMASK ('F') | (sim_switches & SWMASK ('D')) | SWMASK ('N');
if (sim_switches & SWMASK ('D'))
    uptr->dctrl = MTSE_DBG_STR | MTSE_DBG_DAT;
aws_stat = sim_tape_attach_ex (uptr, name, (saved_switches & SWMASK ('D')) ? MTSE_DBG_STR | MTSE_DBG_DAT: 0, 0);
sim_switches = saved_switches;
stat = SCPE_OK;
for (i=0; i<files; i++) {
    int rec_size = 1 + (rand () % max_size);

    awslnt = mtrlnt = tpclnt = rec_size;
    for (j=0; j<records; j++) {
        awsrec_typ = AWS_REC;
        if (sim_switches & SWMASK ('V'))
            sim_printf ("Writing %d byte record\n", rec_size);
        for (k=0; k<rec_size; k++)
            buf[k] = rand () & 0xFF;
        (void)sim_fwrite (&mtrlnt,       sizeof (mtrlnt),       1, fSIMH);
        (void)sim_fwrite (&mtrlnt,       sizeof (mtrlnt),       1, fE11);
        (void)sim_fwrite (&tpclnt,       sizeof (tpclnt),       1, fTPC);
        (void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS);
        (void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS);
        (void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS);
        if (i == 0) {
            (void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS3);
            (void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS3);
            (void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS3);
            }
        awslnt_last = awslnt;
        (void)sim_fwrite (buf, 1, rec_size, fSIMH);
        (void)sim_fwrite (buf, 1, rec_size, fE11);
        (void)sim_fwrite (buf, 1, rec_size, fTPC);
        (void)sim_fwrite (buf, 1, rec_size, fAWS);
        if (i == 0)
            (void)sim_fwrite (buf, 1, rec_size, fAWS3);
        stat = sim_tape_wrrecf (uptr, buf, rec_size);
        if (MTSE_OK != stat)
            goto Done_Files;
        if (rec_size & 1) {
            (void)sim_fwrite (&tpclnt, 1, 1, fSIMH);
            (void)sim_fwrite (&tpclnt, 1, 1, fTPC);
            }
        (void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
        (void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fE11);
        for (k=0; k<rec_size; k++)
            buf[k] = p7b_odd_parity[buf[k] & 0x3F]; /* Only 6 data bits plus parity */
        buf[0] |= P7B_SOR;
        (void)sim_fwrite (buf, 1, rec_size, fP7B);
        }
    awslnt_last = awslnt;
    mtrlnt = tpclnt = awslnt = 0;
    awsrec_typ = AWS_TMK;
    (void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
    (void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fE11);
    (void)sim_fwrite (&tpclnt, sizeof (tpclnt), 1, fTPC);
    buf[0] = P7B_SOR | P7B_EOF;
    (void)sim_fwrite (buf, 1, 1, fP7B);
    (void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS);
    (void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS);
    (void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS);
    if (i == 0) {
        (void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS3);
        (void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS3);
        (void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS3);
        }
    awslnt_last = 0;
    stat = sim_tape_wrtmk (uptr);
    if (MTSE_OK != stat)
        goto Done_Files;
    if (i == 0) {
        mtrlnt = MTR_GAP;
        for (j=0; j<rec_size; j++)
            (void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
        mtrlnt = 0;
        }
    }
mtrlnt = tpclnt = 0;
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fE11);
(void)sim_fwrite (&tpclnt, sizeof (tpclnt), 1, fTPC);
awslnt_last = awslnt;
awsrec_typ = AWS_TMK;
(void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS);
(void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS);
(void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS);
mtrlnt = 0xffffffff;
tpclnt = 0xffff;
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fE11);
(void)sim_fwrite (&tpclnt, sizeof (tpclnt), 1, fTPC);
(void)sim_fwrite (buf, 1, 1, fP7B);
/* Write an unmatched record delimiter (aka garbage) at 
   the end of the SIMH, E11 and AWS files */
mtrlnt = 25;
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fSIMH);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fE11);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fAWS);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fAWS);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, fAWS);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, uptr->fileref);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, uptr->fileref);
(void)sim_fwrite (&mtrlnt, sizeof (mtrlnt), 1, uptr->fileref);
for (j=0; j<records; j++) {
    memset (buf, j, 10240);
    (void)sim_fwrite (buf, 1, 10240, fTAR);
    (void)sim_fwrite (buf, 1, 10240, fTAR2);
    }
memset (buf, j, 10240);
(void)sim_fwrite (buf, 1, 5120, fTAR2);
for (j=0; j<3; j++) {
    awslnt_last = awslnt = 0;
    awsrec_typ = AWS_TMK;
    (void)sim_fwrite (&awslnt,       sizeof (awslnt),       1, fAWS2);
    (void)sim_fwrite (&awslnt_last,  sizeof (awslnt_last),  1, fAWS2);
    (void)sim_fwrite (&awsrec_typ,   sizeof (awsrec_typ),   1, fAWS2);
    }
Done_Files:
if (fSIMH)
    fclose (fSIMH);
if (fE11)
    fclose (fE11);
if (fTPC)
    fclose (fTPC);
if (fP7B)
    fclose (fP7B);
if (fAWS)
    fclose (fAWS);
if (fAWS2)
    fclose (fAWS2);
if (fAWS3)
    fclose (fAWS3);
if (fTAR)
    fclose (fTAR);
if (fTAR2)
    fclose (fTAR2);
free (buf);
sim_tape_detach (uptr);
if (stat == SCPE_OK) {
    char name1[CBUFSIZE], name2[CBUFSIZE];

    sprintf (name1, "\"%s.aws\"", filename);
    sprintf (name2, "\"%s.aws.tape\"", filename);
    sim_switches = SWMASK ('F');
    if (sim_cmp_string (name1, name2))
        stat = 1;
    }
sim_switches = saved_switches;
return stat;
}

static t_stat sim_tape_test_process_tape_file (UNIT *uptr, const char *filename, const char *format, t_awslnt recsize)
{
char args[256];
char str_recsize[16] = "";
t_stat stat;

if (recsize) {
    sim_switches |= SWMASK ('B');
    sprintf (str_recsize, " %d", (int)recsize);
    }
if (NULL == strchr (filename, '*'))
    sprintf (args, "%s%s %s.%s", format, str_recsize, filename, format);
else
    sprintf (args, "%s%s %s", format, str_recsize, filename);
sim_tape_detach (uptr);
sim_switches |= SWMASK ('F') | SWMASK ('L');    /* specific-format and detailed record report */
stat = sim_tape_attach_ex (uptr, args, 0, 0);
if (stat != SCPE_OK)
    return stat;
sim_tape_detach (uptr);
sim_switches = 0;
return SCPE_OK;
}

static t_stat sim_tape_test_remove_tape_files (UNIT *uptr, const char *filename)
{
char name[256];

sprintf (name, "%s.simh", filename);
(void)remove (name);
sprintf (name, "%s.2.simh", filename);
(void)remove (name);
sprintf (name, "%s.e11", filename);
(void)remove (name);
sprintf (name, "%s.2.e11", filename);
(void)remove (name);
sprintf (name, "%s.tpc", filename);
(void)remove (name);
sprintf (name, "%s.2.tpc", filename);
(void)remove (name);
sprintf (name, "%s.p7b", filename);
(void)remove (name);
sprintf (name, "%s.2.p7b", filename);
(void)remove (name);
sprintf (name, "%s.aws", filename);
(void)remove (name);
sprintf (name, "%s.2.aws", filename);
(void)remove (name);
sprintf (name, "%s.3.aws", filename);
(void)remove (name);
sprintf (name, "%s.tar", filename);
(void)remove (name);
sprintf (name, "%s.2.tar", filename);
(void)remove (name);
sprintf (name, "%s.3.tar", filename);
(void)remove (name);
return SCPE_OK;
}

static t_stat sim_tape_test_density_string (void)
{
char buf[128];
int32 valid_bits = 0;
t_stat stat;

if ((SCPE_ARG != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) ||
    (strcmp (buf, "")))
    return stat;
valid_bits = MT_556_VALID;
if ((SCPE_OK != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) || 
    (strcmp (buf, "556")))
    return sim_messagef (SCPE_ARG, "stat was: %s, got string: %s\n", sim_error_text (stat), buf);
valid_bits = MT_800_VALID | MT_1600_VALID;
if ((SCPE_OK != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) || 
    (strcmp (buf, "{800|1600}")))
    return sim_messagef (SCPE_ARG, "stat was: %s, got string: %s\n", sim_error_text (stat), buf);
valid_bits = MT_800_VALID | MT_1600_VALID | MT_6250_VALID;
if ((SCPE_OK != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) || 
    (strcmp (buf, "{800|1600|6250}")))
    return sim_messagef (SCPE_ARG, "stat was: %s, got string: %s\n", sim_error_text (stat), buf);
valid_bits = MT_200_VALID | MT_800_VALID | MT_1600_VALID | MT_6250_VALID;
if ((SCPE_OK != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) || 
    (strcmp (buf, "{200|800|1600|6250}")))
    return sim_messagef (SCPE_ARG, "stat was: %s, got string: %s\n", sim_error_text (stat), buf);
valid_bits = MT_NONE_VALID | MT_800_VALID | MT_1600_VALID | MT_6250_VALID;
if ((SCPE_OK != (stat = sim_tape_density_supported (buf, sizeof (buf), valid_bits))) || 
    (strcmp (buf, "{0|800|1600|6250}")))
    return sim_messagef (SCPE_ARG, "stat was: %s, got string: %s\n", sim_error_text (stat), buf);
return SCPE_OK;
}

#include <setjmp.h>

t_stat sim_tape_test (DEVICE *dptr)
{
int32 saved_switches = sim_switches;
SIM_TEST_INIT;

sim_printf ("\nTesting %s device sim_tape APIs\n", sim_uname(dptr->units));

SIM_TEST(sim_tape_test_density_string ());

SIM_TEST(sim_tape_test_remove_tape_files (dptr->units, "TapeTestFile1"));

SIM_TEST(sim_tape_test_create_tape_files (dptr->units, "TapeTestFile1", 2, 5, 4096));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.*", "ansi-vms", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.*", "ansi-rsx11", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.*", "ansi-rt11", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.*", "ansi-rsts", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.*", "ansi-var", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "tar", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "aws", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.3", "aws", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.2", "aws", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1.2", "tar", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "aws", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "p7b", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "tpc", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "e11", 0));

sim_switches = saved_switches;
SIM_TEST(sim_tape_test_process_tape_file (dptr->units, "TapeTestFile1", "simh", 0));

SIM_TEST(sim_tape_test_remove_tape_files (dptr->units, "TapeTestFile1"));

return SCPE_OK;
}

static void ansi_date (time_t datetime, char date[6], t_bool y2k_date_bug)
    {
    struct tm *lt;
    char buf[20];

    lt = localtime (&datetime);
    if (y2k_date_bug)
        sprintf (buf, " %c%c%03d", '0' + (lt->tm_year / 10), 
                                   '0' + (lt->tm_year % 10), 
                                   lt->tm_yday + 1);
    else
        sprintf (buf, "%c%02d%03d", (lt->tm_year < 100) ? ' ' : '0' + (lt->tm_year/100 - 1), 
                                    lt->tm_year % 100, 
                                    lt->tm_yday + 1);
    memcpy (date, buf, 6);
    }

/* 
 * This isn't quite ANSI 'a' since several ANSI allowed characters
 * are either illegal file names on many DEC systems or are confusing
 * to OS file name parsers.
 */
static void to_ansi_a (char *out, const char *in, size_t size)
    {
    memset (out, ' ', size);
    while (size--) {
        if (isupper (*in) || isdigit (*in))
            *(out++) = *in++;
        else {
            if (*in == '\0')
                break;
            if (islower (*in)) {
                *(out++) = toupper (*in);
                ++in;
                }
            else {
                if (strchr ("-.$_/", *in))
                    *(out++) = *in++;
                else
                    ++in;
                }
            }
        }
    }

static void ansi_make_VOL1 (VOL1 *vol, const char *ident, uint32 ansi_type)
    {
    memset (vol, ' ', sizeof (*vol));
    memcpy (vol->type, "VOL", 3);
    vol->num = '1';
    to_ansi_a (vol->ident, ident, sizeof (vol->ident));
    vol->standard = ansi_args[ansi_type].vol1_standard;
    }

static void ansi_make_HDR1 (HDR1 *hdr1, VOL1 *vol, HDR4 *hdr4, const char *filename, uint32 ansi_type)
    {
    const char *fn;
    struct stat statb;
    char extra_name_used[3] = "00";
    char *fn_cpy, *c, *ext;

    memset (&statb, 0, sizeof (statb));
    (void)stat (filename, &statb);
    if (!(fn = strrchr (filename, '/')) && !(fn = strrchr (filename, '\\')))
        fn = filename;
    else
        ++fn;                                   /* skip over slash or backslash */
    fn_cpy = (char *)malloc (strlen (fn) + 1);
    strcpy (fn_cpy, fn);
    fn = fn_cpy;
    ext = strrchr (fn_cpy, '.');
    if (ext) {
        while (((c = strchr (fn_cpy, '.')) != NULL) && 
               (c != ext))
            *c = '_';                              /* translate extra .'s to _ */
        }
    memset (hdr1, ' ', sizeof (*hdr1));
    memcpy (hdr1->type, "HDR", 3);
    hdr1->num = '1';
    memset (hdr4, ' ', sizeof (*hdr1));
    memcpy (hdr4->type, "HDR", 3);
    hdr4->num = '4';
    to_ansi_a (hdr1->file_ident, fn, sizeof (hdr1->file_ident));
    if (strlen (fn) > 17) {
        to_ansi_a (hdr4->extra_name, fn + 17, sizeof (hdr4->extra_name));
        sprintf (extra_name_used, "%02d", (int)(strlen (fn) - 17));
        }
    memcpy (hdr4->extra_name_used, extra_name_used, 2);
    memcpy (hdr1->file_set, vol->ident, sizeof (hdr1->file_set));
    memcpy (hdr1->file_section, "0001", 4);
    memcpy (hdr1->file_sequence, "0001", 4);
    memcpy (hdr1->generation_number, "0001", 4);    /* generation_number and version_number */
    memcpy (hdr1->version_number, "00", 2);         /* combine to produce VMS version # ;1 here */
    ansi_date (statb.st_mtime, hdr1->creation_date, ansi_args[ansi_type].y2k_date_bug);
    memcpy (hdr1->expiration_date, " 00000", 6);
    memcpy (hdr1->block_count, "000000", 6);
    to_ansi_a (hdr1->system_code, ansi_args[ansi_type].system_code, sizeof (hdr1->system_code));
    free (fn_cpy);
    }

static void ansi_make_HDR2 (HDR2 *hdr, t_bool fixed_record, size_t block_size, size_t record_size, uint32 ansi_type)
    {
    char size[12];
    struct ansi_tape_parameters *ansi = &ansi_args[ansi_type];

    memset (hdr, ' ', sizeof (*hdr));
    memcpy (hdr->type, "HDR", 3);
    hdr->num = '2';
    hdr->record_format = ansi->record_format ? ansi->record_format : (fixed_record ? 'F' : 'D');
    sprintf (size, "%05d", (int)block_size);
    memcpy (hdr->block_length, size, sizeof (hdr->block_length));
    sprintf (size, "%05d", (ansi->zero_record_length)? 0 : (int)record_size);
    memcpy (hdr->record_length, size, sizeof (hdr->record_length));
    hdr->carriage_control = ansi->carriage_control ? ansi->carriage_control : (fixed_record ? 'M' : ' ');
    memcpy (hdr->buffer_offset, "00", 2);
    }

static void ansi_fill_text_buffer (FILE *f, char *buf, size_t buf_size, size_t record_skip_ending, t_bool fixed_text)
    {
    long start;
    char *tmp = (char *)calloc (2 + buf_size, sizeof (*buf));
    size_t offset = 0;

    while (1) {
        size_t rec_size;
        char rec_size_str[16];

        start = ftell (f);
        if (start < 0)
            break;
        if (!fgets (tmp, buf_size, f))
            break;
        rec_size = strlen (tmp);
        if (!fixed_text) {
            if (rec_size >= record_skip_ending)
                rec_size -= record_skip_ending;
            if ((rec_size + 4) > (int)(buf_size - offset)) { /* room for record? */
                (void)fseek (f, start, SEEK_SET);
                break;
                }
            sprintf (rec_size_str, "%04u", (int)(rec_size + 4));
            memcpy (buf + offset, rec_size_str, 4);
            memcpy (buf + offset + 4, tmp, rec_size);
            offset += 4 + rec_size;
            }
        else {
            size_t move_size;

            if ((tmp[rec_size - 2] != '\r') &&
                (tmp[rec_size - 1] == '\n')) {
                memcpy (&tmp[rec_size - 1], "\r\n", 3);
                rec_size += 1;
                }
            if (offset + rec_size < buf_size)
                move_size = rec_size;
            else
                move_size = buf_size - offset;
            /* We've got a line that stradles a block boundary */
            memcpy (buf + offset, tmp, move_size);
            offset += move_size;
            if (offset == buf_size) {
                (void)fseek (f, start + move_size, SEEK_SET);
                break;
                }
            }
        }
    if (buf_size > offset) {
        if (fixed_text)
            memset (buf + offset, 0, buf_size - offset);
        else
            memset (buf + offset, '^', buf_size - offset);
        }
    free (tmp);
    }

static t_bool memory_tape_add_block (MEMORY_TAPE *tape, uint8 *block, uint32 size)
{
TAPE_RECORD *rec;

if (tape->array_size <= tape->record_count) {
    TAPE_RECORD **new_records;
    new_records = (TAPE_RECORD **)realloc (tape->records, (tape->array_size + 1000) * sizeof (*tape->records));
    if (new_records == NULL)
        return TRUE;                /* no memory error */
    tape->records = new_records;
    memset (tape->records + tape->array_size, 0, 1000 * sizeof (*tape->records));
    tape->array_size += 1000;
    }
rec = (TAPE_RECORD *)malloc (sizeof (*rec) + size);
if (rec == NULL)
    return TRUE;                    /* no memory error */
rec->size = size;
memcpy (rec->data, block, size);
tape->records[tape->record_count++] = rec;
return FALSE;
}

static void memory_free_tape (void *vtape)
{
uint32 i;
MEMORY_TAPE *tape = (MEMORY_TAPE *)vtape;

for (i=0; i<tape->record_count; i++) {
    free (tape->records[i]);
    tape->records[i] = NULL;
    }
free (tape->records);
free (tape);
}

MEMORY_TAPE *memory_create_tape (void)
{
MEMORY_TAPE *tape = (MEMORY_TAPE *)calloc (1, sizeof (*tape));

if (NULL == tape)
    return tape;
tape->ansi_type = -1;
return tape;
}

static int tape_classify_file_contents (FILE *f, size_t *max_record_size, t_bool *lf_line_endings, t_bool *crlf_line_endings)
{
long pos = -1;
long last_cr = -1;
long last_lf = -1;
long line_start = 0;
int chr;
long non_print_chars = 0;
long lf_lines = 0;
long crlf_lines = 0;

*max_record_size = 0;
*lf_line_endings = FALSE;
*crlf_line_endings = FALSE;
rewind (f);
while (EOF != (chr = fgetc (f))) {
    ++pos;
    if (!isprint (chr) && (chr != '\r') && (chr != '\n') && (chr != '\t') && (chr != '\f'))
        ++non_print_chars;
    if (chr == '\r')
        last_cr = pos;
    if (chr == '\n') {
        long line_size;

        if (last_cr == (pos - 1)) {
            ++crlf_lines;
            line_size = (pos - (line_start - 2));
            }
        else {
            ++lf_lines;
            line_size = (pos - (line_start - 1));
            }
        if ((line_size + 4) > (long)(*max_record_size + 4))
            *max_record_size = line_size + 4;
        line_start = pos + 1;
        last_lf = pos;
        }
    }
rewind (f);
if (non_print_chars)
    *max_record_size = 512;
else {
    if ((crlf_lines > 0) && (lf_lines == 0)) {
        *lf_line_endings = FALSE;
        *crlf_line_endings = TRUE;
        }
    else {
        if ((lf_lines > 0) && (crlf_lines == 0)) {
            *lf_line_endings = TRUE;
            *crlf_line_endings = FALSE;
            }
        }
    }
return 0;
}

MEMORY_TAPE *ansi_create_tape (const char *label, uint32 block_size, uint32 ansi_type)
{
MEMORY_TAPE *tape = memory_create_tape ();

if (NULL == tape)
    return tape;
tape->block_size = block_size;
tape->ansi_type = ansi_type;
ansi_make_VOL1 (&tape->vol1, label, ansi_type);
memory_tape_add_block (tape, (uint8 *)&tape->vol1, sizeof (tape->vol1));
return tape;
}

static int ansi_add_file_to_tape (MEMORY_TAPE *tape, const char *filename)
{
FILE *f;
struct stat statb;
struct ansi_tape_parameters *ansi = &ansi_args[tape->ansi_type];
uint8 *block = NULL;
size_t max_record_size;
t_bool lf_line_endings;
t_bool crlf_line_endings;
char file_sequence[5];
int block_count = 0;
char block_count_string[17];
int error = FALSE;
HDR1 hdr1;
HDR2 hdr2;
HDR3 hdr3;
HDR4 hdr4;

f = fopen (filename, "rb");
if (f == NULL) {
    sim_printf ("Can't open: %s - %s\n", filename, strerror(errno));
    return errno;
    }
memset (&statb, 0, sizeof (statb));
if (fstat (fileno (f), &statb)) {
    sim_printf ("Can't stat: %s\n", filename);
    fclose (f);
    return -1;
    }
if (S_IFDIR & statb.st_mode) {
    sim_printf ("Can't put a directory on tape: %s\n", filename);
    fclose (f);
    return -1;
    }
if (!(S_IFREG & statb.st_mode)) {
    sim_printf ("Can't put a non regular file on tape: %s\n", filename);
    fclose (f);
    return -1;
    }
tape_classify_file_contents (f, &max_record_size, &lf_line_endings, &crlf_line_endings);
ansi_make_HDR1 (&hdr1, &tape->vol1, &hdr4, filename, tape->ansi_type);
sprintf (file_sequence, "%04d", 1 + tape->file_count);
memcpy (hdr1.file_sequence, file_sequence, sizeof (hdr1.file_sequence));
if (ansi->fixed_text)
    max_record_size = 512;
ansi_make_HDR2 (&hdr2, !lf_line_endings && !crlf_line_endings, tape->block_size, (tape->ansi_type > MTUF_F_ANSI) ? 512 : max_record_size, tape->ansi_type);

if (!ansi->nohdr3) {               /* Need HDR3? */
    if (!lf_line_endings && !crlf_line_endings)         /* Binary File? */
        memcpy (&hdr3, ansi->hdr3_fixed, sizeof (hdr3));
    else {                                              /* Text file */
        if ((lf_line_endings) && !(ansi->fixed_text))
            memcpy (&hdr3, ansi->hdr3_lf_line_endings, sizeof (hdr3));
        else
            memcpy (&hdr3, ansi->hdr3_crlf_line_endings, sizeof (hdr3));
        }
    }
memory_tape_add_block (tape, (uint8 *)&hdr1, sizeof (hdr1));
if (!ansi->nohdr2)
    memory_tape_add_block (tape, (uint8 *)&hdr2, sizeof (hdr2));
if (!ansi->nohdr3)
    memory_tape_add_block (tape, (uint8 *)&hdr3, sizeof (hdr3));
if ((0 != memcmp (hdr4.extra_name_used, "00", 2)) && !ansi->nohdr3 && !ansi->nohdr2)
    memory_tape_add_block (tape, (uint8 *)&hdr4, sizeof (hdr4));
memory_tape_add_block (tape, NULL, 0);        /* Tape Mark */
rewind (f);
block = (uint8 *)calloc (tape->block_size, 1);
while (!feof(f) && !error) {
    size_t data_read = tape->block_size;

    if (lf_line_endings || crlf_line_endings)       /* text file? */
        ansi_fill_text_buffer (f, (char *)block, tape->block_size, 
                               crlf_line_endings ? ansi->skip_crlf_line_endings : ansi->skip_lf_line_endings, 
                               ansi->fixed_text);
    else
        data_read = fread (block, 1, tape->block_size, f);
    if (data_read > 0)
        error = memory_tape_add_block (tape, block, data_read);
    if (!error)
        ++block_count;
    }
fclose (f);
free (block);
memory_tape_add_block (tape, NULL, 0);        /* Tape Mark */
memcpy (hdr1.type, "EOF", sizeof (hdr1.type));
memcpy (hdr2.type, "EOF", sizeof (hdr2.type));
memcpy (hdr3.type, "EOF", sizeof (hdr3.type));
memcpy (hdr4.type, "EOF", sizeof (hdr4.type));
sprintf (block_count_string, "%06d", block_count);
memcpy (hdr1.block_count, block_count_string, sizeof (hdr1.block_count));
memory_tape_add_block (tape, (uint8 *)&hdr1, sizeof (hdr1));
if (!ansi->nohdr2)
    memory_tape_add_block (tape, (uint8 *)&hdr2, sizeof (hdr2));
if (!ansi->nohdr3)
    memory_tape_add_block (tape, (uint8 *)&hdr3, sizeof (hdr3));
if ((0 != memcmp (hdr4.extra_name_used, "00", 2)) && !ansi->nohdr3 && !ansi->nohdr2)
    memory_tape_add_block (tape, (uint8 *)&hdr4, sizeof (hdr4));
memory_tape_add_block (tape, NULL, 0);        /* Tape Mark */
if (sim_switches & SWMASK ('V'))
    sim_messagef (SCPE_OK, "%17.17s%62.62s\n\t%d blocks of data\n", hdr1.file_ident, hdr4.extra_name, block_count);
++tape->file_count;
return error;
}

static void sim_tape_add_ansi_entry (const char *directory, 
                                     const char *filename,
                                     t_offset FileSize,
                                     const struct stat *filestat,
                                     void *context)
{
MEMORY_TAPE *tape = (MEMORY_TAPE *)context;
char FullPath[PATH_MAX + 1];

sprintf (FullPath, "%s%s", directory, filename);

(void)ansi_add_file_to_tape (tape, FullPath);
}

/* export an existing tape to a SIMH tape image */
static t_stat sim_export_tape (UNIT *uptr, const char *export_file)
{
t_stat r;
FILE *f;
t_addr saved_pos = uptr->pos;
uint8 *buf = NULL;
t_mtrlnt bc, sbc;
t_mtrlnt max = MTR_MAXLEN;

if ((export_file == NULL) || (*export_file == '\0'))
    return sim_messagef (SCPE_ARG, "Missing tape export file specification\n");
f = fopen (export_file, "wb");
if (f == NULL)
    return sim_messagef (SCPE_OPENERR, "Can't open SIMH tape image file: %s - %s\n", export_file, strerror (errno));

buf = (uint8 *)calloc (max, 1);
if (buf == NULL) {
    fclose (f);
    return SCPE_MEM;
    }
r = sim_tape_rewind (uptr);
while (r == SCPE_OK) {
    r = sim_tape_rdrecf (uptr, buf, &bc, max);
    switch (r) {
        case MTSE_OK:
            sbc = ((bc + 1) & ~1);              /* word alignment for SIMH format data */
            if ((1   != sim_fwrite (&bc, sizeof (bc),   1, f)) ||
                (sbc != sim_fwrite (buf, 1,           sbc, f))         ||
                (1   != sim_fwrite (&bc, sizeof (bc),   1, f)))
                r = sim_messagef (SCPE_IOERR, "Error writing file: %s - %s\n", export_file, strerror (errno));
            else
                r = SCPE_OK;
            break;

        case MTSE_TMK:
            bc = 0;
            if (1 != sim_fwrite (&bc, sizeof (bc), 1, f))
                r = sim_messagef (SCPE_IOERR, "Error writing file: %s - %s\n", export_file, strerror (errno));
            else
                r = SCPE_OK;
            break;

        default:
            break;
        }
    }
if (r == MTSE_EOM)
    r = SCPE_OK;
free (buf);
fclose (f);
uptr->pos = saved_pos;
return r;
}