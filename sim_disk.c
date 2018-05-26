/* sim_disk.c: simulator disk support library

   Copyright (c) 2011, Mark Pizzolato

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

   Except as contained in this notice, the names of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.



   This is the place which hides processing of various disk formats,
   as well as OS-specific direct hardware access.

   25-Jan-11    MP      Initial Implemementation

Public routines:

   sim_disk_attach           attach disk unit
   sim_disk_detach           detach disk unit
   sim_disk_attach_help      help routine for attaching disks
   sim_disk_rdsect           read disk sectors
   sim_disk_rdsect_a         read disk sectors asynchronously
   sim_disk_wrsect           write disk sectors
   sim_disk_wrsect_a         write disk sectors asynchronously
   sim_disk_unload           unload or detach a disk as needed
   sim_disk_reset            reset unit
   sim_disk_wrp              TRUE if write protected
   sim_disk_isavailable      TRUE if available for I/O
   sim_disk_size             get disk size
   sim_disk_set_fmt          set disk format
   sim_disk_show_fmt         show disk format
   sim_disk_set_capac        set disk capacity
   sim_disk_show_capac       show disk capacity
   sim_disk_set_async        enable asynchronous operation
   sim_disk_clr_async        disable asynchronous operation
   sim_disk_data_trace       debug support

Internal routines:

   sim_os_disk_open_raw      platform specific open raw device
   sim_os_disk_close_raw     platform specific close raw device
   sim_os_disk_size_raw      platform specific raw device size
   sim_os_disk_unload_raw    platform specific disk unload/eject
   sim_os_disk_rdsect        platform specific read sectors
   sim_os_disk_wrsect        platform specific write sectors

   sim_vhd_disk_open         platform independent open virtual disk file
   sim_vhd_disk_create       platform independent create virtual disk file
   sim_vhd_disk_create_diff  platform independent create differencing virtual disk file
   sim_vhd_disk_close        platform independent close virtual disk file
   sim_vhd_disk_size         platform independent virtual disk size
   sim_vhd_disk_rdsect       platform independent read virtual disk sectors
   sim_vhd_disk_wrsect       platform independent write virtual disk sectors


*/

#define _FILE_OFFSET_BITS 64    /* 64 bit file offset for raw I/O operations  */

#include "sim_defs.h"
#include "sim_disk.h"
#include "sim_ether.h"
#include <ctype.h>
#include <sys/stat.h>

#if defined SIM_ASYNCH_IO
#include <pthread.h>
#endif

struct disk_context {
    DEVICE              *dptr;              /* Device for unit (access to debug flags) */
    uint32              dbit;               /* debugging bit */
    uint32              sector_size;        /* Disk Sector Size (of the pseudo disk) */
    uint32              capac_factor;       /* Units of Capacity (2 = word, 1 = byte) */
    uint32              xfer_element_size;  /* Disk Bus Transfer size (1 - byte, 2 - word, 4 - longword) */
    uint32              storage_sector_size;/* Sector size of the containing storage */
    uint32              removable;          /* Removable device flag */
    uint32              is_cdrom;           /* Host system CDROM Device */
    uint32              media_removed;      /* Media not available flag */
    uint32              auto_format;        /* Format determined dynamically */
#if defined _WIN32
    HANDLE              disk_handle;        /* OS specific Raw device handle */
#endif
#if defined SIM_ASYNCH_IO
    int                 asynch_io;          /* Asynchronous Interrupt scheduling enabled */
    int                 asynch_io_latency;  /* instructions to delay pending interrupt */
    pthread_mutex_t     lock;
    pthread_t           io_thread;          /* I/O Thread Id */
    pthread_mutex_t     io_lock;
    pthread_cond_t      io_cond;
    pthread_cond_t      io_done;
    pthread_cond_t      startup_cond;
    int                 io_dop;
    uint8               *buf;
    t_seccnt            *rsects;
    t_seccnt            sects;
    t_lba               lba;
    DISK_PCALLBACK      callback;
    t_stat              io_status;
#endif
    };

#define disk_ctx up8                        /* Field in Unit structure which points to the disk_context */

#if defined SIM_ASYNCH_IO
#define AIO_CALLSETUP                                               \
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;   \
                                                                    \
if ((!callback) || !ctx->asynch_io)

#define AIO_CALL(op, _lba, _buf, _rsects, _sects,  _callback)   \
    if (ctx->asynch_io) {                                       \
        struct disk_context *ctx =                              \
                      (struct disk_context *)uptr->disk_ctx;    \
                                                                \
        pthread_mutex_lock (&ctx->io_lock);                     \
                                                                \
        sim_debug_unit (ctx->dbit, uptr,                        \
      "sim_disk AIO_CALL(op=%d, unit=%d, lba=0x%X, sects=%d)\n",\
                op, (int)(uptr-ctx->dptr->units), _lba, _sects);\
                                                                \
        if (ctx->callback)                                      \
            abort(); /* horrible mistake, stop */               \
        ctx->io_dop = op;                                       \
        ctx->lba = _lba;                                        \
        ctx->buf = _buf;                                        \
        ctx->sects = _sects;                                    \
        ctx->rsects = _rsects;                                  \
        ctx->callback = _callback;                              \
        pthread_cond_signal (&ctx->io_cond);                    \
        pthread_mutex_unlock (&ctx->io_lock);                   \
        }                                                       \
    else                                                        \
        if (_callback)                                          \
            (_callback) (uptr, r);


#define DOP_DONE  0             /* close */
#define DOP_RSEC  1             /* sim_disk_rdsect_a */
#define DOP_WSEC  2             /* sim_disk_wrsect_a */
#define DOP_IAVL  3             /* sim_disk_isavailable_a */

static void *
_disk_io(void *arg)
{
UNIT* volatile uptr = (UNIT*)arg;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

/* Boost Priority for this I/O thread vs the CPU instruction execution
   thread which in general won't be readily yielding the processor when
   this thread needs to run */
sim_os_set_thread_priority (PRIORITY_ABOVE_NORMAL);

sim_debug_unit (ctx->dbit, uptr, "_disk_io(unit=%d) starting\n", (int)(uptr-ctx->dptr->units));

pthread_mutex_lock (&ctx->io_lock);
pthread_cond_signal (&ctx->startup_cond);   /* Signal we're ready to go */
while (ctx->asynch_io) {
    pthread_cond_wait (&ctx->io_cond, &ctx->io_lock);
    if (ctx->io_dop == DOP_DONE)
        break;
    pthread_mutex_unlock (&ctx->io_lock);
    switch (ctx->io_dop) {
        case DOP_RSEC:
            ctx->io_status = sim_disk_rdsect (uptr, ctx->lba, ctx->buf, ctx->rsects, ctx->sects);
            break;
        case DOP_WSEC:
            ctx->io_status = sim_disk_wrsect (uptr, ctx->lba, ctx->buf, ctx->rsects, ctx->sects);
            break;
        case DOP_IAVL:
            ctx->io_status = sim_disk_isavailable (uptr);
            break;
        }
    pthread_mutex_lock (&ctx->io_lock);
    ctx->io_dop = DOP_DONE;
    pthread_cond_signal (&ctx->io_done);
    sim_activate (uptr, ctx->asynch_io_latency);
    }
pthread_mutex_unlock (&ctx->io_lock);

sim_debug_unit (ctx->dbit, uptr, "_disk_io(unit=%d) exiting\n", (int)(uptr-ctx->dptr->units));

return NULL;
}

/* This routine is called in the context of the main simulator thread before
   processing events for any unit. It is only called when an asynchronous
   thread has called sim_activate() to activate a unit.  The job of this
   routine is to put the unit in proper condition to digest what may have
   occurred in the asynchrconous thread.
  
   Since disk processing only handles a single I/O at a time to a
   particular disk device (due to using stdio for the SimH Disk format
   and stdio doesn't have an atomic seek+(read|write) operation),
   we have the opportunity to possibly detect improper attempts to
   issue multiple concurrent I/O requests. */
static void _disk_completion_dispatch (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
DISK_PCALLBACK callback = ctx->callback;

sim_debug_unit (ctx->dbit, uptr, "_disk_completion_dispatch(unit=%d, dop=%d, callback=%p)\n", (int)(uptr-ctx->dptr->units), ctx->io_dop, ctx->callback);

if (ctx->io_dop != DOP_DONE)
    abort();                                            /* horribly wrong, stop */

if (ctx->callback && ctx->io_dop == DOP_DONE) {
    ctx->callback = NULL;
    callback (uptr, ctx->io_status);
    }
}

static t_bool _disk_is_active (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_disk_is_active(unit=%d, dop=%d)\n", (int)(uptr-ctx->dptr->units), ctx->io_dop);
    return (ctx->io_dop != DOP_DONE);
    }
return FALSE;
}

static t_bool _disk_cancel (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_disk_cancel(unit=%d, dop=%d)\n", (int)(uptr-ctx->dptr->units), ctx->io_dop);
    if (ctx->asynch_io) {
        pthread_mutex_lock (&ctx->io_lock);
        while (ctx->io_dop != DOP_DONE)
            pthread_cond_wait (&ctx->io_done, &ctx->io_lock);
        pthread_mutex_unlock (&ctx->io_lock);
        }
    }
return FALSE;
}
#else
#define AIO_CALLSETUP
#define AIO_CALL(op, _lba, _buf, _rsects, _sects,  _callback)   \
    if (_callback)                                              \
        (_callback) (uptr, r);
#endif

/* Forward declarations */

static t_stat sim_vhd_disk_implemented (void);
static FILE *sim_vhd_disk_open (const char *rawdevicename, const char *openmode);
static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize);
static FILE *sim_vhd_disk_create_diff (const char *szVHDPath, const char *szParentVHDPath);
static FILE *sim_vhd_disk_merge (const char *szVHDPath, char **ParentVHD);
static int sim_vhd_disk_close (FILE *f);
static void sim_vhd_disk_flush (FILE *f);
static t_offset sim_vhd_disk_size (FILE *f);
static t_stat sim_vhd_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects);
static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects);
static t_stat sim_vhd_disk_clearerr (UNIT *uptr);
static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype);
static const char *sim_vhd_disk_get_dtype (FILE *f);
static t_stat sim_os_disk_implemented_raw (void);
static FILE *sim_os_disk_open_raw (const char *rawdevicename, const char *openmode);
static int sim_os_disk_close_raw (FILE *f);
static void sim_os_disk_flush_raw (FILE *f);
static t_offset sim_os_disk_size_raw (FILE *f);
static t_stat sim_os_disk_unload_raw (FILE *f);
static t_bool sim_os_disk_isavailable_raw (FILE *f);
static t_stat sim_os_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects);
static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects);
static t_stat sim_os_disk_info_raw (FILE *f, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom);
static char *HostPathToVhdPath (const char *szHostPath, char *szVhdPath, size_t VhdPathSize);
static char *VhdPathToHostPath (const char *szVhdPath, char *szHostPath, size_t HostPathSize);
static t_offset get_filesystem_size (UNIT *uptr);

struct sim_disk_fmt {
    const char          *name;                          /* name */
    int32               uflags;                         /* unit flags */
    int32               fmtval;                         /* Format type value */
    t_stat              (*impl_fnc)(void);              /* Implemented Test Function */
    };

static struct sim_disk_fmt fmts[] = {
    { "AUTO", 0, DKUF_F_AUTO, NULL},
    { "SIMH", 0, DKUF_F_STD,  NULL},
    { "RAW",  0, DKUF_F_RAW,  sim_os_disk_implemented_raw},
    { "VHD",  0, DKUF_F_VHD,  sim_vhd_disk_implemented},
    { NULL,   0, 0}
    };

/* Set disk format */

t_stat sim_disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 f;

if (uptr == NULL)
    return SCPE_IERR;
if (cptr == NULL)
    return SCPE_ARG;
for (f = 0; fmts[f].name; f++) {
    if (fmts[f].name && (strcmp (cptr, fmts[f].name) == 0)) {
        if ((fmts[f].impl_fnc) && (fmts[f].impl_fnc() != SCPE_OK))
            return SCPE_NOFNC;
        uptr->flags = (uptr->flags & ~DKUF_FMT) |
            (fmts[f].fmtval << DKUF_V_FMT) | fmts[f].uflags;
        return SCPE_OK;
        }
    }
return sim_messagef (SCPE_ARG, "Unknown disk format: %s\n", cptr);
}

/* Show disk format */

t_stat sim_disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 f = DK_GET_FMT (uptr);
size_t i;

for (i = 0; fmts[i].name; i++)
    if (fmts[i].fmtval == f) {
        fprintf (st, "%s format", fmts[i].name);
        return SCPE_OK;
        }
fprintf (st, "invalid format");
return SCPE_OK;
}

/* Set disk capacity */

t_stat sim_disk_set_capac (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_offset cap;
t_stat r;
DEVICE *dptr = find_dev_from_unit (uptr);

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
cap = (t_offset) get_uint (cptr, 10, sim_taddr_64? 2000000: 2000, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
uptr->capac = (t_addr)((cap * ((t_offset) 1000000))/((dptr->flags & DEV_SECTORS) ? 512 : 1));
return SCPE_OK;
}

/* Show disk capacity */

t_stat sim_disk_show_capac (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const char *cap_units = "B";
DEVICE *dptr = find_dev_from_unit (uptr);
t_offset capac = ((t_offset)uptr->capac)*((dptr->flags & DEV_SECTORS) ? 512 : 1);

if ((dptr->dwidth / dptr->aincr) == 16)
    cap_units = "W";
if (capac) {
    if (capac >= (t_offset) 1000000)
        fprintf (st, "capacity=%dM%s", (uint32) (capac / ((t_offset) 1000000)), cap_units);
    else if (uptr->capac >= (t_addr) 1000)
        fprintf (st, "capacity=%dK%s", (uint32) (capac / ((t_offset) 1000)), cap_units);
    else fprintf (st, "capacity=%d%s", (uint32) capac, cap_units);
    }
else fprintf (st, "undefined capacity");
return SCPE_OK;
}

/* Test for available */

t_bool sim_disk_isavailable (UNIT *uptr)
{
struct disk_context *ctx;
t_bool is_available;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return FALSE;
ctx = (struct disk_context *)uptr->disk_ctx;
switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
        is_available = TRUE;
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        is_available = TRUE;
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        if (sim_os_disk_isavailable_raw (uptr->fileref)) {
            if (ctx->media_removed) {
                int32 saved_switches = sim_switches;
                int32 saved_quiet = sim_quiet;
                char *path = (char *)malloc (1 + strlen (uptr->filename));

                sim_switches = 0;
                sim_quiet = 1;
                strcpy (path, uptr->filename);
                sim_disk_attach (uptr, path, ctx->sector_size, ctx->xfer_element_size, 
                                 FALSE, ctx->dbit, NULL, 0, 0);
                sim_quiet = saved_quiet;
                sim_switches = saved_switches;
                free (path);
                ctx->media_removed = 0;
                }
            }
        else
            ctx->media_removed = 1;
        is_available = !ctx->media_removed;
        break;
    default:
        is_available = FALSE;
        break;
    }
sim_debug_unit (ctx->dbit, uptr, "sim_disk_isavailable(unit=%d)=%s\n", (int)(uptr-ctx->dptr->units), is_available ? "true" : "false");
return is_available;
}

t_bool sim_disk_isavailable_a (UNIT *uptr, DISK_PCALLBACK callback)
{
t_bool r = FALSE;
AIO_CALLSETUP
    r = sim_disk_isavailable (uptr);
AIO_CALL(DOP_IAVL, 0, NULL, NULL, 0, callback);
return r;
}

/* Test for write protect */

t_bool sim_disk_wrp (UNIT *uptr)
{
return (uptr->flags & DKUF_WRP)? TRUE: FALSE;
}

/* Get Disk size */

t_offset sim_disk_size (UNIT *uptr)
{
t_offset physical_size, filesystem_size;
t_bool saved_quiet = sim_quiet;

switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
        physical_size = sim_fsize_ex (uptr->fileref);
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        physical_size = sim_vhd_disk_size (uptr->fileref);
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        physical_size = sim_os_disk_size_raw (uptr->fileref);
        break;
    default:
        return (t_offset)-1;
    }
sim_quiet = TRUE;
filesystem_size = get_filesystem_size (uptr);
sim_quiet = saved_quiet;
if ((filesystem_size == (t_offset)-1) ||
    (filesystem_size < physical_size))
    return physical_size;
return filesystem_size;
}

/* Enable asynchronous operation */

t_stat sim_disk_set_async (UNIT *uptr, int latency)
{
#if !defined(SIM_ASYNCH_IO)
char *msg = "Disk: can't operate asynchronously\r\n";
sim_printf ("%s", msg);
return SCPE_NOFNC;
#else
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
pthread_attr_t attr;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_set_async(unit=%d)\n", (int)(uptr-ctx->dptr->units));

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
    pthread_create (&ctx->io_thread, &attr, _disk_io, (void *)uptr);
    pthread_attr_destroy(&attr);
    pthread_cond_wait (&ctx->startup_cond, &ctx->io_lock); /* Wait for thread to stabilize */
    pthread_mutex_unlock (&ctx->io_lock);
    pthread_cond_destroy (&ctx->startup_cond);
    }
uptr->a_check_completion = _disk_completion_dispatch;
uptr->a_is_active = _disk_is_active;
uptr->cancel = _disk_cancel;
return SCPE_OK;
#endif
}

/* Disable asynchronous operation */

t_stat sim_disk_clr_async (UNIT *uptr)
{
#if !defined(SIM_ASYNCH_IO)
return SCPE_NOFNC;
#else
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

/* make sure device exists */
if (!ctx) return SCPE_UNATT;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_clr_async(unit=%d)\n", (int)(uptr-ctx->dptr->units));

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

/* Read Sectors */

static t_stat _sim_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
t_offset da;
uint32 err, tbc;
size_t i;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

sim_debug_unit (ctx->dbit, uptr, "_sim_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

da = ((t_offset)lba) * ctx->sector_size;
tbc = sects * ctx->sector_size;
if (sectsread)
    *sectsread = 0;
err = sim_fseeko (uptr->fileref, da, SEEK_SET);          /* set pos */
if (!err) {
    i = sim_fread (buf, ctx->xfer_element_size, tbc/ctx->xfer_element_size, uptr->fileref);
    if (i < tbc/ctx->xfer_element_size)                 /* fill */
        memset (&buf[i*ctx->xfer_element_size], 0, tbc-(i*ctx->xfer_element_size));
    err = ferror (uptr->fileref);
    if ((!err) && (sectsread))
        *sectsread = (t_seccnt)((i*ctx->xfer_element_size+ctx->sector_size-1)/ctx->sector_size);
    }
return err;
}

t_stat sim_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
t_stat r;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_seccnt sread = 0;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

if ((sects == 1) &&                                     /* Single sector reads */
    (lba >= (uptr->capac*ctx->capac_factor)/(ctx->sector_size/((ctx->dptr->flags & DEV_SECTORS) ? 512 : 1)))) {/* beyond the end of the disk */
    memset (buf, '\0', ctx->sector_size);               /* are bad block management efforts - zero buffer */
    if (sectsread)
        *sectsread = 1;
    return SCPE_OK;                                     /* return success */
    }

if ((0 == (ctx->sector_size & (ctx->storage_sector_size - 1))) ||   /* Sector Aligned & whole sector transfers */
    ((0 == ((lba*ctx->sector_size) & (ctx->storage_sector_size - 1))) &&
     (0 == ((sects*ctx->sector_size) & (ctx->storage_sector_size - 1))))) {
    switch (DK_GET_FMT (uptr)) {                        /* case on format */
        case DKUF_F_STD:                                /* SIMH format */
            return _sim_disk_rdsect (uptr, lba, buf, sectsread, sects);
        case DKUF_F_VHD:                                /* VHD format */
            r = sim_vhd_disk_rdsect (uptr, lba, buf, &sread, sects);
            break;
        case DKUF_F_RAW:                                /* Raw Physical Disk Access */
            r = sim_os_disk_rdsect (uptr, lba, buf, &sread, sects);
            break;
        default:
            return SCPE_NOFNC;
        }
    if (sectsread)
        *sectsread = sread;
    if (r != SCPE_OK)
        return r;
    sim_buf_swap_data (buf, ctx->xfer_element_size, (sread * ctx->sector_size) / ctx->xfer_element_size);
    return r;
    }
else { /* Unaligned and/or partial sector transfers */
    uint8 *tbuf = (uint8*) malloc (sects*ctx->sector_size + 2*ctx->storage_sector_size);
    t_lba sspsts = ctx->storage_sector_size/ctx->sector_size; /* sim sectors in a storage sector */
    t_lba tlba = lba & ~(sspsts - 1);
    t_seccnt tsects = sects + (lba - tlba);

    tsects = (tsects + (sspsts - 1)) & ~(sspsts - 1);
    if (sectsread)
        *sectsread = 0;
    if (tbuf == NULL)
        return SCPE_MEM;
    switch (DK_GET_FMT (uptr)) {                        /* case on format */
        case DKUF_F_STD:                                /* SIMH format */
            r = _sim_disk_rdsect (uptr, tlba, tbuf, &sread, tsects);
            break;
        case DKUF_F_VHD:                                /* VHD format */
            r = sim_vhd_disk_rdsect (uptr, tlba, tbuf, &sread, tsects);
            if (r == SCPE_OK)
                sim_buf_swap_data (tbuf, ctx->xfer_element_size, (sread * ctx->sector_size) / ctx->xfer_element_size);
            break;
        case DKUF_F_RAW:                                /* Raw Physical Disk Access */
            r = sim_os_disk_rdsect (uptr, tlba, tbuf, &sread, tsects);
            if (r == SCPE_OK)
                sim_buf_swap_data (tbuf, ctx->xfer_element_size, (sread * ctx->sector_size) / ctx->xfer_element_size);
            break;
        default:
            free (tbuf);
            return SCPE_NOFNC;
        }
    if (r == SCPE_OK) {
        memcpy (buf, tbuf + ((lba - tlba) * ctx->sector_size), sects * ctx->sector_size);
        if (sectsread) {
            *sectsread = sread - (lba - tlba);
            if (*sectsread > sects)
                *sectsread = sects;
            }
        }
    free (tbuf);
    return r;
    }
}

t_stat sim_disk_rdsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects, DISK_PCALLBACK callback)
{
t_stat r = SCPE_OK;
AIO_CALLSETUP
    r = sim_disk_rdsect (uptr, lba, buf, sectsread, sects);
AIO_CALL(DOP_RSEC, lba, buf, sectsread, sects, callback);
return r;
}

/* Write Sectors */

static t_stat _sim_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
t_offset da;
uint32 err, tbc;
size_t i;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

sim_debug_unit (ctx->dbit, uptr, "_sim_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

da = ((t_offset)lba) * ctx->sector_size;
tbc = sects * ctx->sector_size;
if (sectswritten)
    *sectswritten = 0;
err = sim_fseeko (uptr->fileref, da, SEEK_SET);          /* set pos */
if (!err) {
    i = sim_fwrite (buf, ctx->xfer_element_size, tbc/ctx->xfer_element_size, uptr->fileref);
    err = ferror (uptr->fileref);
    if ((!err) && (sectswritten))
        *sectswritten = (t_seccnt)((i*ctx->xfer_element_size+ctx->sector_size-1)/ctx->sector_size);
    }
return err;
}

t_stat sim_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint32 f = DK_GET_FMT (uptr);
t_stat r;
uint8 *tbuf = NULL;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

if (uptr->dynflags & UNIT_DISK_CHK) {
    DEVICE *dptr = find_dev_from_unit (uptr);
    uint32 capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (word: 2, byte: 1) */
    t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(ctx->sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
    t_lba sect;

    for (sect = 0; sect < sects; sect++) {
        t_lba offset;
        t_bool sect_error = FALSE;

        for (offset = 0; offset < ctx->sector_size; offset += sizeof(uint32)) {
            if (*((uint32 *)&buf[sect*ctx->sector_size + offset]) != (uint32)(lba + sect)) {
                sect_error = TRUE;
                break;
                }
            }
        if (sect_error) {
            uint32 save_dctrl = dptr->dctrl;
            FILE *save_sim_deb = sim_deb;

            sim_printf ("\n%s%d: Write Address Verification Error on lbn %d(0x%X) of %d(0x%X).\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)(lba+sect), (int)(lba+sect), (int)total_sectors, (int)total_sectors);
            dptr->dctrl = 0xFFFFFFFF;
            sim_deb = save_sim_deb ? save_sim_deb : stdout;
            sim_disk_data_trace (uptr, buf+sect*ctx->sector_size, lba+sect, ctx->sector_size,    "Found", TRUE, 1);
            dptr->dctrl = save_dctrl;
            sim_deb = save_sim_deb;
            }
        }
    }
if (f == DKUF_F_STD)
    return _sim_disk_wrsect (uptr, lba, buf, sectswritten, sects);
if ((0 == (ctx->sector_size & (ctx->storage_sector_size - 1))) ||   /* Sector Aligned & whole sector transfers */
    ((0 == ((lba*ctx->sector_size) & (ctx->storage_sector_size - 1))) &&
     (0 == ((sects*ctx->sector_size) & (ctx->storage_sector_size - 1))))) {

    if (sim_end || (ctx->xfer_element_size == sizeof (char)))
        switch (DK_GET_FMT (uptr)) {                            /* case on format */
            case DKUF_F_VHD:                                    /* VHD format */
                return sim_vhd_disk_wrsect  (uptr, lba, buf, sectswritten, sects);
            case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
                return sim_os_disk_wrsect  (uptr, lba, buf, sectswritten, sects);
            default:
                return SCPE_NOFNC;
            }

    tbuf = (uint8*) malloc (sects * ctx->sector_size);
    if (NULL == tbuf)
        return SCPE_MEM;
    sim_buf_copy_swapped (tbuf, buf, ctx->xfer_element_size, (sects * ctx->sector_size) / ctx->xfer_element_size);

    switch (DK_GET_FMT (uptr)) {                            /* case on format */
        case DKUF_F_VHD:                                    /* VHD format */
            r = sim_vhd_disk_wrsect (uptr, lba, tbuf, sectswritten, sects);
            break;
        case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
            r = sim_os_disk_wrsect (uptr, lba, tbuf, sectswritten, sects);
            break;
        default:
            r = SCPE_NOFNC;
            break;
        }
    }
else { /* Unaligned and/or partial sector transfers */
    t_lba sspsts = ctx->storage_sector_size/ctx->sector_size; /* sim sectors in a storage sector */
    t_lba tlba = lba & ~(sspsts - 1);
    t_seccnt tsects = sects + (lba - tlba);

    tbuf = (uint8*) malloc (sects*ctx->sector_size + 2*ctx->storage_sector_size);
    tsects = (tsects + (sspsts - 1)) & ~(sspsts - 1);
    if (sectswritten)
        *sectswritten = 0;
    if (tbuf == NULL)
        return SCPE_MEM;
    /* Partial Sector writes require a read-modify-write sequence for the partial sectors */
    if ((lba & (sspsts - 1)) ||
        (sects < sspsts))
        switch (DK_GET_FMT (uptr)) {                            /* case on format */
            case DKUF_F_VHD:                                    /* VHD format */
                sim_vhd_disk_rdsect (uptr, tlba, tbuf, NULL, sspsts);
                break;
            case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
                sim_os_disk_rdsect (uptr, tlba, tbuf, NULL, sspsts);
                break;
            default:
                r = SCPE_NOFNC;
                break;
            }
    if ((tsects > sspsts) &&
        ((sects + lba - tlba) & (sspsts - 1)))
        switch (DK_GET_FMT (uptr)) {                            /* case on format */
            case DKUF_F_VHD:                                    /* VHD format */
                sim_vhd_disk_rdsect (uptr, tlba + tsects - sspsts,
                                     tbuf + (tsects - sspsts) * ctx->sector_size,
                                     NULL, sspsts);
                break;
            case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
                sim_os_disk_rdsect (uptr, tlba + tsects - sspsts,
                                    tbuf + (tsects - sspsts) * ctx->sector_size,
                                    NULL, sspsts);
                break;
            default:
                r = SCPE_NOFNC;
                break;
            }
    sim_buf_copy_swapped (tbuf + (lba & (sspsts - 1)) * ctx->sector_size,
                          buf, ctx->xfer_element_size, (sects * ctx->sector_size) / ctx->xfer_element_size);
    switch (DK_GET_FMT (uptr)) {                            /* case on format */
        case DKUF_F_VHD:                                    /* VHD format */
            r = sim_vhd_disk_wrsect (uptr, tlba, tbuf, sectswritten, tsects);
            break;
        case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
            r = sim_os_disk_wrsect (uptr, tlba, tbuf, sectswritten, tsects);
            break;
        default:
            r = SCPE_NOFNC;
            break;
        }
    if ((r == SCPE_OK) && sectswritten) {
        *sectswritten -= (lba - tlba);
        if (*sectswritten > sects)
            *sectswritten = sects;
        }
    }
free (tbuf);
return r;
}

t_stat sim_disk_wrsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects, DISK_PCALLBACK callback)
{
t_stat r = SCPE_OK;
AIO_CALLSETUP
    r =  sim_disk_wrsect (uptr, lba, buf, sectswritten, sects);
AIO_CALL(DOP_WSEC, lba, buf, sectswritten, sects, callback);
return r;
}

t_stat sim_disk_unload (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* Simh */
    case DKUF_F_VHD:                                    /* VHD format */
        ctx->media_removed = 1;
        return sim_disk_detach (uptr);
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        ctx->media_removed = 1;
        return sim_os_disk_unload_raw (uptr->fileref);  /* remove/eject disk */
        break;
    default:
        return SCPE_NOFNC;
    }
}

/*
   This routine is called when the simulator stops and any time
   the asynch mode is changed (enabled or disabled)
*/
static void _sim_disk_io_flush (UNIT *uptr)
{
uint32 f = DK_GET_FMT (uptr);

#if defined (SIM_ASYNCH_IO)
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

sim_disk_clr_async (uptr);
if (sim_asynch_enabled)
    sim_disk_set_async (uptr, ctx->asynch_io_latency);
#endif
switch (f) {                                            /* case on format */
    case DKUF_F_STD:                                    /* Simh */
        fflush (uptr->fileref);
        break;
    case DKUF_F_VHD:                                    /* Virtual Disk */
        sim_vhd_disk_flush (uptr->fileref);
        break;
    case DKUF_F_RAW:                                    /* Physical */
        sim_os_disk_flush_raw (uptr->fileref);
        break;
        }
}

static t_stat _err_return (UNIT *uptr, t_stat stat)
{
free (uptr->filename);
uptr->filename = NULL;
free (uptr->disk_ctx);
uptr->disk_ctx = NULL;
return stat;
}

#pragma pack(push,1)
typedef struct _ODS1_HomeBlock
    {
    uint16  hm1_w_ibmapsize;
    uint32  hm1_l_ibmaplbn;
    uint16  hm1_w_maxfiles;
    uint16  hm1_w_cluster;
    uint16  hm1_w_devtype;
    uint16  hm1_w_structlev;
#define HM1_C_LEVEL1    0401
#define HM1_C_LEVEL2    0402
    uint8   hm1_t_volname[12];
    uint8   hm1_b_fill_1[4];
    uint16  hm1_w_volowner;
    uint16  hm1_w_protect;
    uint16  hm1_w_volchar;
    uint16  hm1_w_fileprot;
    uint8   hm1_b_fill_2[6];
    uint8   hm1_b_window;
    uint8   hm1_b_extend;
    uint8   hm1_b_lru_lim;
    uint8   hm1_b_fill_3[11];
    uint16  hm1_w_checksum1;
    uint8   hm1_t_credate[14];
    uint8   hm1_b_fill_4[382];
    uint32  hm1_l_serialnum;
    uint8   hm1_b_fill_5[12];
    uint8   hm1_t_volname2[12];
    uint8   hm1_t_ownername[12];
    uint8   hm1_t_format[12];
    uint8   hm1_t_fill_6[2];
    uint16  hm1_w_checksum2;
    } ODS1_HomeBlock;

    typedef struct _ODS2_HomeBlock
    {
    uint32 hm2_l_homelbn;
    uint32 hm2_l_alhomelbn;
    uint32 hm2_l_altidxlbn;
    uint8  hm2_b_strucver;
    uint8  hm2_b_struclev;
    uint16 hm2_w_cluster;
    uint16 hm2_w_homevbn;
    uint16 hm2_w_alhomevbn;
    uint16 hm2_w_altidxvbn;
    uint16 hm2_w_ibmapvbn;
    uint32 hm2_l_ibmaplbn;
    uint32 hm2_l_maxfiles;
    uint16 hm2_w_ibmapsize;
    uint16 hm2_w_resfiles;
    uint16 hm2_w_devtype;
    uint16 hm2_w_rvn;
    uint16 hm2_w_setcount;
    uint16 hm2_w_volchar;
    uint32 hm2_l_volowner;
    uint32 hm2_l_reserved;
    uint16 hm2_w_protect;
    uint16 hm2_w_fileprot;
    uint16 hm2_w_reserved;
    uint16 hm2_w_checksum1;
    uint32 hm2_q_credate[2];
    uint8  hm2_b_window;
    uint8  hm2_b_lru_lim;
    uint16 hm2_w_extend;
    uint32 hm2_q_retainmin[2];
    uint32 hm2_q_retainmax[2];
    uint32 hm2_q_revdate[2];
    uint8  hm2_r_min_class[20];
    uint8  hm2_r_max_class[20];
    uint8  hm2_r_reserved[320];
    uint32 hm2_l_serialnum;
    uint8  hm2_t_strucname[12];
    uint8  hm2_t_volname[12];
    uint8  hm2_t_ownername[12];
    uint8  hm2_t_format[12];
    uint16 hm2_w_reserved2;
    uint16 hm2_w_checksum2;
    } ODS2_HomeBlock;

typedef struct _ODS1_FileHeader
    {
    uint8   fh1_b_idoffset;
    uint8   fh1_b_mpoffset;
    uint16  fh1_w_fid_num;
    uint16  fh1_w_fid_seq;
    uint16  fh1_w_struclev;
    uint16  fh1_w_fileowner;
    uint16  fh1_w_fileprot;
    uint16  fh1_w_filechar;
    uint16  fh1_w_recattr;
    uint8   fh1_b_fill_1[494];
    uint16  fh1_w_checksum;
    } ODS1_FileHeader;

typedef struct _ODS2_FileHeader
    {
    uint8  fh2_b_idoffset;
    uint8  fh2_b_mpoffset;
    uint8  fh2_b_acoffset;
    uint8  fh2_b_rsoffset;
    uint16 fh2_w_seg_num;
    uint16 fh2_w_structlev;
    uint16 fh2_w_fid[3];
    uint16 fh2_w_ext_fid[3];
    uint16 fh2_w_recattr[16];
    uint32 fh2_l_filechar;
    uint16 fh2_w_remaining[228];
    } ODS2_FileHeader;

typedef union _ODS2_Retreval
    {
        struct 
            {
            unsigned fm2___fill   : 14;       /* type specific data               */
            unsigned fm2_v_format : 2;        /* format type code                 */
            } fm2_r_word0_bits;
        struct
            {
            unsigned fm2_v_exact    : 1;      /* exact placement specified        */
            unsigned fm2_v_oncyl    : 1;      /* on cylinder allocation desired   */
            unsigned fm2___fill     : 10;
            unsigned fm2_v_lbn      : 1;      /* use LBN of next map pointer      */
            unsigned fm2_v_rvn      : 1;      /* place on specified RVN           */
            unsigned fm2_v_format0  : 2;
            } fm2_r_map_bits0;
        struct
            {
            unsigned fm2_b_count1   : 8;      /* low byte described below         */
            unsigned fm2_v_highlbn1 : 6;      /* high order LBN                   */
            unsigned fm2_v_format1  : 2;
            unsigned fm2_w_lowlbn1  : 16;     /* low order LBN                    */
            } fm2_r_map_bits1;
        struct
            {
            struct
                {
                unsigned fm2_v_count2   : 14; /* count field                      */
                unsigned fm2_v_format2  : 2;
                unsigned fm2_l_lowlbn2  : 16; /* low order LBN                    */
                } fm2_r_map2_long0;
            uint16 fm2_l_highlbn2;            /* high order LBN                   */
            } fm2_r_map_bits2;
        struct
            {
            struct
                {
                unsigned fm2_v_highcount3 : 14; /* low order count field          */
                unsigned fm2_v_format3  : 2;
                unsigned fm2_w_lowcount3 : 16;  /* high order count field         */
                } fm2_r_map3_long0;
            uint32 fm2_l_lbn3;
            } fm2_r_map_bits3;
    } ODS2_Retreval;

typedef struct _ODS1_Retreval
    {
    uint8   fm1_b_ex_segnum;
    uint8   fm1_b_ex_rvn;
    uint16  fm1_w_ex_filnum;
    uint16  fm1_w_ex_filseq;
    uint8   fm1_b_countsize;
    uint8   fm1_b_lbnsize;
    uint8   fm1_b_inuse;
    uint8   fm1_b_avail;
    union {
        struct {
            uint8 fm1_b_highlbn;
            uint8 fm1_b_count;
            uint16 fm1_w_lowlbn;
            } fm1_s_fm1def1;
        struct {
            uint8 fm1_b_highlbn;
            uint8 fm1_b_count;
            uint16 fm1_w_lowlbn;
            } fm1_s_fm1def2;
        } fm1_pointers[4];
    } ODS1_Retreval;

typedef struct _ODS1_StorageControlBlock
    {
    uint8  scb_b_unused[3];
    uint8  scb_b_bitmapblks;
    struct _bitmapblk {
        uint16 scb_w_freeblks;
        uint16 scb_w_freeptr;
        } scb_r_blocks[1];
    } ODS1_SCB;


typedef struct _ODS2_StorageControlBlock
    {
    uint8  scb_b_strucver;   /* 1 */
    uint8  scb_b_struclev;   /* 2 */
    uint16 scb_w_cluster;
    uint32 scb_l_volsize;
    uint32 scb_l_blksize;
    uint32 scb_l_sectors;
    uint32 scb_l_tracks;
    uint32 scb_l_cylinder;
    uint32 scb_l_status;
    uint32 scb_l_status2;
    uint16 scb_w_writecnt;
    uint8  scb_t_volockname[12];
    uint32 scb_q_mounttime[2];
    uint16 scb_w_backrev;
    uint32 scb_q_genernum[2];
    uint8  scb_b_reserved[446];
    uint16 scb_w_checksum;
    } ODS2_SCB;
#pragma pack(pop)

static uint16
ODSChecksum (void *Buffer, uint16 WordCount)
    {
    int i;
    uint16 Sum = 0;
    uint16 CheckSum = 0;
    uint16 *Buf = (uint16 *)Buffer;

    for (i=0; i<WordCount; i++)
        CheckSum += Buf[i];
    return CheckSum;
    }


static t_offset get_ods2_filesystem_size (UNIT *uptr)
{
DEVICE *dptr;
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_offset temp_capac = 512 * (t_offset)0xFFFFFFFFu;  /* Make sure we can access the largest sector */
uint32 capac_factor;
ODS2_HomeBlock Home;
ODS2_FileHeader Header;
ODS2_Retreval *Retr;
ODS2_SCB Scb;
uint16 CheckSum1, CheckSum2;
uint32 ScbLbn = 0;
t_offset ret_val = (t_offset)-1;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return ret_val;
capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (word: 2, byte: 1) */
saved_capac = uptr->capac;
uptr->capac = (t_addr)(temp_capac/(capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));
if (sim_disk_rdsect (uptr, 512 / ctx->sector_size, (uint8 *)&Home, NULL, sizeof (Home) / ctx->sector_size))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Home, (uint16)((((char *)&Home.hm2_w_checksum1)-((char *)&Home.hm2_l_homelbn))/2));
CheckSum2 = ODSChecksum (&Home, (uint16)((((char *)&Home.hm2_w_checksum2)-((char *)&Home.hm2_l_homelbn))/2));
if ((Home.hm2_l_homelbn == 0) || 
    (Home.hm2_l_alhomelbn == 0) || 
    (Home.hm2_l_altidxlbn == 0) || 
    ((Home.hm2_b_struclev != 2) && (Home.hm2_b_struclev != 5)) || 
    (Home.hm2_b_strucver == 0) || 
    (Home.hm2_w_cluster == 0) || 
    (Home.hm2_w_homevbn == 0) || 
    (Home.hm2_w_alhomevbn == 0) || 
    (Home.hm2_w_ibmapvbn == 0) || 
    (Home.hm2_l_ibmaplbn == 0) || 
    (Home.hm2_w_resfiles >= Home.hm2_l_maxfiles) || 
    (Home.hm2_w_ibmapsize == 0) || 
    (Home.hm2_w_resfiles < 5) || 
    (Home.hm2_w_checksum1 != CheckSum1) ||
    (Home.hm2_w_checksum2 != CheckSum2))
    goto Return_Cleanup;
if (sim_disk_rdsect (uptr, (Home.hm2_l_ibmaplbn+Home.hm2_w_ibmapsize+1) * (512 / ctx->sector_size), 
                           (uint8 *)&Header, NULL, sizeof (Header) / ctx->sector_size))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Header, 255);
if (CheckSum1 != *(((uint16 *)&Header)+255)) /* Verify Checksum on BITMAP.SYS file header */
    goto Return_Cleanup;
Retr = (ODS2_Retreval *)(((uint16*)(&Header))+Header.fh2_b_mpoffset);
/* The BitMap File has a single extent, which may be preceeded by a placement descriptor */
if (Retr->fm2_r_word0_bits.fm2_v_format == 0)
    Retr = (ODS2_Retreval *)(((uint16 *)Retr)+1); /* skip placement descriptor */
switch (Retr->fm2_r_word0_bits.fm2_v_format)
    {
    case 1:
        ScbLbn = (Retr->fm2_r_map_bits1.fm2_v_highlbn1<<16)+Retr->fm2_r_map_bits1.fm2_w_lowlbn1;
        break;
    case 2:
        ScbLbn = (Retr->fm2_r_map_bits2.fm2_l_highlbn2<<16)+Retr->fm2_r_map_bits2.fm2_r_map2_long0.fm2_l_lowlbn2;
        break;
    case 3:
        ScbLbn = Retr->fm2_r_map_bits3.fm2_l_lbn3;
        break;
    }
Retr = (ODS2_Retreval *)(((uint16 *)Retr)+Retr->fm2_r_word0_bits.fm2_v_format+1);
if (sim_disk_rdsect (uptr, ScbLbn * (512 / ctx->sector_size), (uint8 *)&Scb, NULL, sizeof (Scb) / ctx->sector_size))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Scb, 255);
if (CheckSum1 != *(((uint16 *)&Scb)+255)) /* Verify Checksum on Storage Control Block */
    goto Return_Cleanup;
if ((Scb.scb_w_cluster != Home.hm2_w_cluster) || 
    (Scb.scb_b_strucver != Home.hm2_b_strucver) ||
    (Scb.scb_b_struclev != Home.hm2_b_struclev))
    goto Return_Cleanup;
if (!sim_quiet) {
    sim_printf ("%s%d: '%s' Contains ODS%d File system\n", sim_dname (dptr), (int)(uptr-dptr->units), uptr->filename, Home.hm2_b_struclev);
    sim_printf ("%s%d: Volume Name: %12.12s ", sim_dname (dptr), (int)(uptr-dptr->units), Home.hm2_t_volname);
    sim_printf ("Format: %12.12s ", Home.hm2_t_format);
    sim_printf ("Sectors In Volume: %u\n", Scb.scb_l_volsize);
    }
ret_val = ((t_offset)Scb.scb_l_volsize) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
return ret_val;
}

static t_offset get_ods1_filesystem_size (UNIT *uptr)
{
DEVICE *dptr;
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_offset temp_capac = 512 * (t_offset)0xFFFFFFFFu;  /* Make sure we can access the largest sector */
uint32 capac_factor;
ODS1_HomeBlock Home;
ODS1_FileHeader Header;
ODS1_Retreval *Retr;
uint8 scb_buf[512];
ODS1_SCB *Scb = (ODS1_SCB *)scb_buf;
uint16 CheckSum1, CheckSum2;
uint32 ScbLbn;
t_offset ret_val = (t_offset)-1;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return ret_val;
capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (word: 2, byte: 1) */
saved_capac = uptr->capac;
uptr->capac = (t_addr)(temp_capac/(capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));
if (sim_disk_rdsect (uptr, 512 / ctx->sector_size, (uint8 *)&Home, NULL, sizeof (Home) / ctx->sector_size))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Home, (uint16)((((char *)&Home.hm1_w_checksum1)-((char *)&Home.hm1_w_ibmapsize))/2));
CheckSum2 = ODSChecksum (&Home, (uint16)((((char *)&Home.hm1_w_checksum2)-((char *)&Home.hm1_w_ibmapsize))/2));
if ((Home.hm1_w_ibmapsize == 0) || 
    (Home.hm1_l_ibmaplbn == 0) || 
    (Home.hm1_w_maxfiles == 0) || 
    (Home.hm1_w_cluster != 1) || 
    ((Home.hm1_w_structlev != HM1_C_LEVEL1) && (Home.hm1_w_structlev != HM1_C_LEVEL2)) || 
    (Home.hm1_l_ibmaplbn == 0) || 
    (Home.hm1_w_checksum1 != CheckSum1) ||
    (Home.hm1_w_checksum2 != CheckSum2))
    goto Return_Cleanup;
if (sim_disk_rdsect (uptr, (((Home.hm1_l_ibmaplbn << 16) + ((Home.hm1_l_ibmaplbn >> 16) & 0xFFFF)) + Home.hm1_w_ibmapsize + 1) * (512 / ctx->sector_size),
                           (uint8 *)&Header, NULL, sizeof (Header) / ctx->sector_size))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Header, 255);
if (CheckSum1 != *(((uint16 *)&Header)+255)) /* Verify Checksum on BITMAP.SYS file header */
    goto Return_Cleanup;

Retr = (ODS1_Retreval *)(((uint16*)(&Header))+Header.fh1_b_mpoffset);
ScbLbn = (Retr->fm1_pointers[0].fm1_s_fm1def1.fm1_b_highlbn<<16)+Retr->fm1_pointers[0].fm1_s_fm1def1.fm1_w_lowlbn;
if (sim_disk_rdsect (uptr, ScbLbn * (512 / ctx->sector_size), (uint8 *)Scb, NULL, 512 / ctx->sector_size))
    goto Return_Cleanup;
if (Scb->scb_b_bitmapblks < 127)
    ret_val = (((t_offset)Scb->scb_r_blocks[Scb->scb_b_bitmapblks].scb_w_freeblks << 16) + Scb->scb_r_blocks[Scb->scb_b_bitmapblks].scb_w_freeptr) * 512;
else
    ret_val = (((t_offset)Scb->scb_r_blocks[0].scb_w_freeblks << 16) + Scb->scb_r_blocks[0].scb_w_freeptr) * 512;
if (!sim_quiet) {
    sim_printf ("%s%d: '%s' Contains an ODS1 File system\n", sim_dname (dptr), (int)(uptr-dptr->units), uptr->filename);
    sim_printf ("%s%d: Volume Name: %12.12s ", sim_dname (dptr), (int)(uptr-dptr->units), Home.hm1_t_volname);
    sim_printf ("Format: %12.12s ", Home.hm1_t_format);
    sim_printf ("Sectors In Volume: %u\n", (uint32)(ret_val / 512));
    }

Return_Cleanup:
uptr->capac = saved_capac;
return ret_val;
}

typedef struct ultrix_disklabel {
    uint32  pt_magic;       /* magic no. indicating part. info exits */
    uint32  pt_valid;       /* set by driver if pt is current */
    struct  pt_info {
        uint32  pi_nblocks; /* no. of sectors */
        uint32  pi_blkoff;  /* block offset for start */
        } pt_part[8];
    } ultrix_disklabel;

#define PT_MAGIC        0x032957        /* Partition magic number */
#define PT_VALID        1               /* Indicates if struct is valid */

static t_offset get_ultrix_filesystem_size (UNIT *uptr)
{
DEVICE *dptr;
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_offset temp_capac = 512 * (t_offset)0xFFFFFFFFu;  /* Make sure we can access the largest sector */
uint32 capac_factor;
uint8 sector_buf[512];
ultrix_disklabel *Label = (ultrix_disklabel *)(sector_buf + sizeof (sector_buf) - sizeof (ultrix_disklabel));
t_offset ret_val = (t_offset)-1;
int i;
uint32 max_lbn = 0, max_lbn_partnum = 0;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return ret_val;
capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (word: 2, byte: 1) */
saved_capac = uptr->capac;
uptr->capac = (t_addr)(temp_capac/(capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));
if (sim_disk_rdsect (uptr, 31 * (512 / ctx->sector_size), sector_buf, NULL, 512 / ctx->sector_size))
    goto Return_Cleanup;

if ((Label->pt_magic != PT_MAGIC) || 
    (Label->pt_valid != PT_VALID))
    goto Return_Cleanup;

for (i = 0; i < 8; i++) {
    uint32 end_lbn = Label->pt_part[i].pi_blkoff + Label->pt_part[i].pi_nblocks;
    if (end_lbn > max_lbn) {
        max_lbn = end_lbn;
        max_lbn_partnum = i;
        }
    }
if (!sim_quiet) {
    sim_printf ("%s%d: '%s' Contains Ultrix partitions\n", sim_dname (dptr), (int)(uptr-dptr->units), uptr->filename);
    sim_printf ("Partition with highest sector: %c, Sectors On Disk: %u\n", 'a' + max_lbn_partnum, max_lbn);
    }
ret_val = ((t_offset)max_lbn) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
return ret_val;
}

typedef t_offset (*FILESYSTEM_CHECK)(UNIT *uptr);

static t_offset get_filesystem_size (UNIT *uptr)
{
static FILESYSTEM_CHECK checks[] = {
    &get_ods2_filesystem_size,
    &get_ods1_filesystem_size,
    &get_ultrix_filesystem_size,
    NULL
    };
t_offset ret_val;
int i;

for (i = 0; checks[i] != NULL; i++) {
    ret_val = checks[i] (uptr);
    if (ret_val != (t_offset)-1)
        break;
    }
return ret_val;
}

t_stat sim_disk_attach (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_element_size, t_bool dontautosize,
                        uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay)
{
struct disk_context *ctx;
DEVICE *dptr;
char tbuf[4*CBUFSIZE];
FILE *(*open_function)(const char *filename, const char *mode) = sim_fopen;
FILE *(*create_function)(const char *filename, t_offset desiredsize) = NULL;
t_offset (*size_function)(FILE *file);
t_stat (*storage_function)(FILE *file, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom) = NULL;
t_bool created = FALSE, copied = FALSE;
t_bool auto_format = FALSE;
t_offset capac, filesystem_capac;

if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if (sim_switches & SWMASK ('F')) {                      /* format spec? */
    char gbuf[CBUFSIZE];
    cptr = get_glyph (cptr, gbuf, 0);                   /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return SCPE_2FARG;
    if ((sim_disk_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK) ||
        (DK_GET_FMT (uptr) == DKUF_F_AUTO))
        return sim_messagef (SCPE_ARG, "Invalid Override Disk Format: %s\n", gbuf);
    sim_switches = sim_switches & ~(SWMASK ('F'));      /* Record Format specifier already processed */
    auto_format = TRUE;
    }
if (sim_switches & SWMASK ('D')) {                      /* create difference disk? */
    char gbuf[CBUFSIZE];
    FILE *vhd;

    sim_switches = sim_switches & ~(SWMASK ('D'));
    cptr = get_glyph_nc (cptr, gbuf, 0);                /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return SCPE_2FARG;
    vhd = sim_vhd_disk_create_diff (gbuf, cptr);
    if (vhd) {
        sim_vhd_disk_close (vhd);
        return sim_disk_attach (uptr, gbuf, sector_size, xfer_element_size, dontautosize, dbit, dtype, pdp11tracksize, completion_delay);
        }
    return sim_messagef (SCPE_ARG, "Unable to create differencing VHD: %s\n", gbuf);
    }
if (sim_switches & SWMASK ('C')) {                      /* create vhd disk & copy contents? */
    char gbuf[CBUFSIZE];
    FILE *vhd;
    int saved_sim_switches = sim_switches;
    int32 saved_sim_quiet = sim_quiet;
    uint32 capac_factor;
    t_stat r;

    sim_switches = sim_switches & ~(SWMASK ('C'));
    cptr = get_glyph_nc (cptr, gbuf, 0);                /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return SCPE_2FARG;
    sim_switches |= SWMASK ('R') | SWMASK ('E');
    sim_quiet = TRUE;
    /* First open the source of the copy operation */
    r = sim_disk_attach (uptr, cptr, sector_size, xfer_element_size, dontautosize, dbit, dtype, pdp11tracksize, completion_delay);
    sim_quiet = saved_sim_quiet;
    if (r != SCPE_OK) {
        sim_switches = saved_sim_switches;
        return sim_messagef (r, "Can't open source VHD: %s\n", cptr);
        }
    sim_messagef (SCPE_OK, "%s%d: creating new virtual disk '%s'\n", sim_dname (dptr), (int)(uptr-dptr->units), gbuf);
    capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (word: 2, byte: 1) */
    vhd = sim_vhd_disk_create (gbuf, ((t_offset)uptr->capac)*capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1));
    if (!vhd) {
        return sim_messagef (r, "%s%d: can't create virtual disk '%s'\n", sim_dname (dptr), (int)(uptr-dptr->units), gbuf);
        }
    else {
        uint8 *copy_buf = (uint8*) malloc (1024*1024);
        t_lba lba;
        t_seccnt sectors_per_buffer = (t_seccnt)((1024*1024)/sector_size);
        t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
        t_seccnt sects = sectors_per_buffer;

        if (!copy_buf) {
            sim_vhd_disk_close(vhd);
            (void)remove (gbuf);
            return SCPE_MEM;
            }
        for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects) {
            sim_messagef (SCPE_OK, "%s%d: Copied %dMB.  %d%% complete.\r", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000), (int)((((float)lba)*100)/total_sectors));
            sects = sectors_per_buffer;
            if (lba + sects > total_sectors)
                sects = total_sectors - lba;
            r = sim_disk_rdsect (uptr, lba, copy_buf, NULL, sects);
            if (r == SCPE_OK) {
                uint32 saved_unit_flags = uptr->flags;
                FILE *save_unit_fileref = uptr->fileref;

                sim_disk_set_fmt (uptr, 0, "VHD", NULL);
                uptr->fileref = vhd;
                r = sim_disk_wrsect (uptr, lba, copy_buf, NULL, sects);
                uptr->fileref = save_unit_fileref;
                uptr->flags = saved_unit_flags;
                }
            }
        if (r == SCPE_OK)
            sim_messagef (SCPE_OK, "\n%s%d: Copied %dMB. Done.\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)(((t_offset)lba*sector_size)/1000000));
        else
            sim_messagef (r, "\n%s%d: Error copying: %s.\n", sim_dname (dptr), (int)(uptr-dptr->units), sim_error_text (r));
        if ((r == SCPE_OK) && (sim_switches & SWMASK ('V'))) {
            uint8 *verify_buf = (uint8*) malloc (1024*1024);

            if (!verify_buf) {
                sim_vhd_disk_close(vhd);
                (void)remove (gbuf);
                free (copy_buf);
                return SCPE_MEM;
                }
            for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects) {
                sim_messagef (SCPE_OK, "%s%d: Verified %dMB.  %d%% complete.\r", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000), (int)((((float)lba)*100)/total_sectors));
                sects = sectors_per_buffer;
                if (lba + sects > total_sectors)
                    sects = total_sectors - lba;
                r = sim_disk_rdsect (uptr, lba, copy_buf, NULL, sects);
                if (r == SCPE_OK) {
                    uint32 saved_unit_flags = uptr->flags;
                    FILE *save_unit_fileref = uptr->fileref;

                    sim_disk_set_fmt (uptr, 0, "VHD", NULL);
                    uptr->fileref = vhd;
                    r = sim_disk_rdsect (uptr, lba, verify_buf, NULL, sects);
                    uptr->fileref = save_unit_fileref;
                    uptr->flags = saved_unit_flags;
                    if (r == SCPE_OK) {
                        if (0 != memcmp (copy_buf, verify_buf, 1024*1024))
                            r = SCPE_IOERR;
                        }
                    }
                }
            if (!sim_quiet) {
                if (r == SCPE_OK)
                    sim_messagef (r, "\n%s%d: Verified %dMB. Done.\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)(((t_offset)lba*sector_size)/1000000));
                else {
                    t_lba i;
                    uint32 save_dctrl = dptr->dctrl;
                    FILE *save_sim_deb = sim_deb;

                    for (i = 0; i < (1024*1024/sector_size); ++i)
                        if (0 != memcmp (copy_buf+i*sector_size, verify_buf+i*sector_size, sector_size))
                            break;
                    sim_printf ("\n%s%d: Verification Error on lbn %d.\n", sim_dname (dptr), (int)(uptr-dptr->units), lba+i);
                    dptr->dctrl = 0xFFFFFFFF;
                    sim_deb = stdout;
                    sim_disk_data_trace (uptr,   copy_buf+i*sector_size, lba+i, sector_size, "Expected", TRUE, 1);
                    sim_disk_data_trace (uptr, verify_buf+i*sector_size, lba+i, sector_size,    "Found", TRUE, 1);
                    dptr->dctrl = save_dctrl;
                    sim_deb = save_sim_deb;
                    }
                }
            free (verify_buf);
            }
        free (copy_buf);
        sim_vhd_disk_close (vhd);
        sim_disk_detach (uptr);
        if (r == SCPE_OK) {
            created = TRUE;
            copied = TRUE;
            tbuf[sizeof(tbuf)-1] = '\0';
            strncpy (tbuf, gbuf, sizeof(tbuf)-1);
            cptr = tbuf;
            sim_disk_set_fmt (uptr, 0, "VHD", NULL);
            sim_switches = saved_sim_switches;
            }
        else
            return r;
        /* fall through and open/return the newly created & copied vhd */
        }
    }
else
    if (sim_switches & SWMASK ('M')) {                 /* merge difference disk? */
        char gbuf[CBUFSIZE], *Parent = NULL;
        FILE *vhd;

        sim_switches = sim_switches & ~(SWMASK ('M'));
        get_glyph_nc (cptr, gbuf, 0);                  /* get spec */
        vhd = sim_vhd_disk_merge (gbuf, &Parent);
        if (vhd) {
            t_stat r;

            sim_vhd_disk_close (vhd);
            r = sim_disk_attach (uptr, Parent, sector_size, xfer_element_size, dontautosize, dbit, dtype, pdp11tracksize, completion_delay);
            free (Parent);
            return r;
            }
        return SCPE_ARG;
        }

switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_AUTO:                                   /* SIMH format */
        auto_format = TRUE;
        if (NULL != (uptr->fileref = sim_vhd_disk_open (cptr, "rb"))) { /* Try VHD */
            sim_disk_set_fmt (uptr, 0, "VHD", NULL);    /* set file format to VHD */
            sim_vhd_disk_close (uptr->fileref);         /* close vhd file*/
            uptr->fileref = NULL;
            open_function = sim_vhd_disk_open;
            size_function = sim_vhd_disk_size;
            break;
            }
        if (NULL != (uptr->fileref = sim_os_disk_open_raw (cptr, "rb"))) {
            sim_disk_set_fmt (uptr, 0, "RAW", NULL);    /* set file format to RAW */
            sim_os_disk_close_raw (uptr->fileref);      /* close raw file*/
            open_function = sim_os_disk_open_raw;
            size_function = sim_os_disk_size_raw;
            storage_function = sim_os_disk_info_raw;
            uptr->fileref = NULL;
            break;
            }
        sim_disk_set_fmt (uptr, 0, "SIMH", NULL);       /* set file format to SIMH */
        open_function = sim_fopen;
        size_function = sim_fsize_ex;
        break;
    case DKUF_F_STD:                                    /* SIMH format */
        open_function = sim_fopen;
        size_function = sim_fsize_ex;
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        open_function = sim_vhd_disk_open;
        create_function = sim_vhd_disk_create;
        size_function = sim_vhd_disk_size;
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        open_function = sim_os_disk_open_raw;
        size_function = sim_os_disk_size_raw;
        storage_function = sim_os_disk_info_raw;
        break;
    default:
        return SCPE_IERR;
    }
uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char));/* alloc name buf */
uptr->disk_ctx = ctx = (struct disk_context *)calloc(1, sizeof(struct disk_context));
if ((uptr->filename == NULL) || (uptr->disk_ctx == NULL))
    return _err_return (uptr, SCPE_MEM);
strncpy (uptr->filename, cptr, CBUFSIZE);               /* save name */
ctx->sector_size = (uint32)sector_size;                 /* save sector_size */
ctx->capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (word: 2, byte: 1) */
ctx->xfer_element_size = (uint32)xfer_element_size;     /* save xfer_element_size */
ctx->dptr = dptr;                                       /* save DEVICE pointer */
ctx->dbit = dbit;                                       /* save debug bit */
ctx->media_removed = 0;                                 /* default present */
sim_debug_unit (ctx->dbit, uptr, "sim_disk_attach(unit=%d,filename='%s')\n", (int)(uptr-ctx->dptr->units), uptr->filename);
ctx->auto_format = auto_format;                         /* save that we auto selected format */
ctx->storage_sector_size = (uint32)sector_size;         /* Default */
if ((sim_switches & SWMASK ('R')) ||                    /* read only? */
    ((uptr->flags & UNIT_RO) != 0)) {
    if (((uptr->flags & UNIT_ROABLE) == 0) &&           /* allowed? */
        ((uptr->flags & UNIT_RO) == 0))
        return _err_return (uptr, SCPE_NORO);           /* no, error */
    uptr->fileref = open_function (cptr, "rb");         /* open rd only */
    if (uptr->fileref == NULL)                          /* open fail? */
        return _err_return (uptr, SCPE_OPENERR);        /* yes, error */
    uptr->flags = uptr->flags | UNIT_RO;                /* set rd only */
    sim_messagef (SCPE_OK, "%s%d: unit is read only\n", sim_dname (dptr), (int)(uptr-dptr->units));
    }
else {                                                  /* normal */
    uptr->fileref = open_function (cptr, "rb+");        /* open r/w */
    if (uptr->fileref == NULL) {                        /* open fail? */
        if ((errno == EROFS) || (errno == EACCES)) {    /* read only? */
            if ((uptr->flags & UNIT_ROABLE) == 0)       /* allowed? */
                return _err_return (uptr, SCPE_NORO);   /* no error */
            uptr->fileref = open_function (cptr, "rb"); /* open rd only */
            if (uptr->fileref == NULL)                  /* open fail? */
                return _err_return (uptr, SCPE_OPENERR);/* yes, error */
            uptr->flags = uptr->flags | UNIT_RO;        /* set rd only */
            sim_messagef (SCPE_OK, "%s%d: unit is read only\n", sim_dname (dptr), (int)(uptr-dptr->units));
            }
        else {                                          /* doesn't exist */
            if (sim_switches & SWMASK ('E'))            /* must exist? */
                return _err_return (uptr, SCPE_OPENERR); /* yes, error */
            if (create_function)
                uptr->fileref = create_function (cptr, ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1));/* create new file */
            else
                uptr->fileref = open_function (cptr, "wb+");/* open new file */
            if (uptr->fileref == NULL)                  /* open fail? */
                return _err_return (uptr, SCPE_OPENERR);/* yes, error */
            sim_messagef (SCPE_OK, "%s%d: creating new file\n", sim_dname (dptr), (int)(uptr-dptr->units));
            created = TRUE;
            }
        }                                               /* end if null */
    }                                                   /* end else */
if (DK_GET_FMT (uptr) == DKUF_F_VHD) {
    if ((created) && dtype)
        sim_vhd_disk_set_dtype (uptr->fileref, dtype);
    if (dtype && strcmp (dtype, sim_vhd_disk_get_dtype (uptr->fileref))) {
        char cmd[32];

        sprintf (cmd, "%s%d %s", dptr->name, (int)(uptr-dptr->units), sim_vhd_disk_get_dtype (uptr->fileref));
        set_cmd (0, cmd);
        }
    }
uptr->flags = uptr->flags | UNIT_ATT;
uptr->pos = 0;

/* Get Device attributes if they are available */
if (storage_function)
    storage_function (uptr->fileref, &ctx->storage_sector_size, &ctx->removable, &ctx->is_cdrom);

if ((created) && (!copied)) {
    t_stat r = SCPE_OK;
    uint8 *secbuf = (uint8 *)calloc (128, ctx->sector_size);     /* alloc temp sector buf */

    /*
       On a newly created disk, we write a zero sector to the last and the
       first sectors.  This serves 3 purposes:
         1) it avoids strange allocation delays writing newly allocated
            storage at the end of the disk during simulator operation
         2) it allocates storage for the whole disk at creation time to
            avoid strange failures which may happen during simulator execution
            if the containing disk is full
         3) it leaves a Simh Format disk at the intended size so it may
            subsequently be autosized with the correct size.
    */
    if (secbuf == NULL)
        r = SCPE_MEM;
    if (r == SCPE_OK) { /* Write all blocks */
        t_lba lba;
        t_lba total_lbas = (t_lba)((((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1))/ctx->sector_size);

        for (lba = 0; (r == SCPE_OK) && (lba < total_lbas); lba += 128) { 
            t_seccnt sectors = ((lba + 128) <= total_lbas) ? 128 : total_lbas - lba;

            r = sim_disk_wrsect (uptr, lba, secbuf, NULL, sectors);
            }
        }
    free (secbuf);
    if (r != SCPE_OK) {
        sim_disk_detach (uptr);                         /* report error now */
        (void)remove (cptr);                            /* remove the created file */
        return SCPE_OPENERR;
        }
    if (sim_switches & SWMASK ('I')) {                  /* Initialize To Sector Address */
        uint8 *init_buf = (uint8*) malloc (1024*1024);
        t_lba lba, sect;
        uint32 capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (word: 2, byte: 1) */
        t_seccnt sectors_per_buffer = (t_seccnt)((1024*1024)/sector_size);
        t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
        t_seccnt sects = sectors_per_buffer;

        if (!init_buf) {
            sim_disk_detach (uptr);                         /* report error now */
            (void)remove (cptr);
            return SCPE_MEM;
            }
        for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects) {
            sects = sectors_per_buffer;
            if (lba + sects > total_sectors)
                sects = total_sectors - lba;
            for (sect = 0; sect < sects; sect++) {
                t_lba offset;
                for (offset = 0; offset < sector_size; offset += sizeof(uint32))
                    *((uint32 *)&init_buf[sect*sector_size + offset]) = (uint32)(lba + sect);
                }
            r = sim_disk_wrsect (uptr, lba, init_buf, NULL, sects);
            if (r != SCPE_OK) {
                free (init_buf);
                sim_disk_detach (uptr);                         /* report error now */
                (void)remove (cptr);                            /* remove the created file */
                return SCPE_OPENERR;
                }
            sim_messagef (SCPE_OK, "%s%d: Initialized To Sector Address %dMB.  %d%% complete.\r", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000), (int)((((float)lba)*100)/total_sectors));
            }
        sim_messagef (SCPE_OK, "%s%d: Initialized To Sector Address %dMB.  100%% complete.\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000));
        free (init_buf);
        }
    if (pdp11tracksize)
        sim_disk_pdp11_bad_block (uptr, pdp11tracksize, sector_size/sizeof(uint16));
    }

if (sim_switches & SWMASK ('K')) {
    t_stat r = SCPE_OK;
    t_lba lba, sect;
    uint32 capac_factor = ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (word: 2, byte: 1) */
    t_seccnt sectors_per_buffer = (t_seccnt)((1024*1024)/sector_size);
    t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
    t_seccnt sects = sectors_per_buffer;
    uint8 *verify_buf = (uint8*) malloc (1024*1024);

    if (!verify_buf) {
        sim_disk_detach (uptr);                         /* report error now */
        return SCPE_MEM;
        }
    for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects) {
        sects = sectors_per_buffer;
        if (lba + sects > total_sectors)
            sects = total_sectors - lba;
        r = sim_disk_rdsect (uptr, lba, verify_buf, NULL, sects);
        if (r == SCPE_OK) {
            for (sect = 0; sect < sects; sect++) {
                t_lba offset;
                t_bool sect_error = FALSE;

                for (offset = 0; offset < sector_size; offset += sizeof(uint32)) {
                    if (*((uint32 *)&verify_buf[sect*sector_size + offset]) != (uint32)(lba + sect)) {
                        sect_error = TRUE;
                        break;
                        }
                    }
                if (sect_error) {
                    uint32 save_dctrl = dptr->dctrl;
                    FILE *save_sim_deb = sim_deb;

                    sim_printf ("\n%s%d: Verification Error on lbn %d(0x%X) of %d(0x%X).\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)(lba+sect), (int)(lba+sect), (int)total_sectors, (int)total_sectors);
                    dptr->dctrl = 0xFFFFFFFF;
                    sim_deb = stdout;
                    sim_disk_data_trace (uptr, verify_buf+sect*sector_size, lba+sect, sector_size,    "Found", TRUE, 1);
                    dptr->dctrl = save_dctrl;
                    sim_deb = save_sim_deb;
                    }
                }
            }
        sim_messagef (SCPE_OK, "%s%d: Verified containing Sector Address %dMB.  %d%% complete.\r", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000), (int)((((float)lba)*100)/total_sectors));
        }
    sim_messagef (SCPE_OK, "%s%d: Verified containing Sector Address %dMB.  100%% complete.\n", sim_dname (dptr), (int)(uptr-dptr->units), (int)((((float)lba)*sector_size)/1000000));
    free (verify_buf);
    uptr->dynflags |= UNIT_DISK_CHK;
    }

filesystem_capac = get_filesystem_size (uptr);
capac = size_function (uptr->fileref);
if (capac && (capac != (t_offset)-1)) {
    if (dontautosize) {
        t_addr saved_capac = uptr->capac;

        if ((filesystem_capac != (t_offset)-1) &&
            (filesystem_capac > (((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)))) {
            if (!sim_quiet) {
                uptr->capac = (t_addr)(filesystem_capac/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));
                sim_printf ("%s%d: The file system on the disk %s is larger than simulated device (%s > ", sim_dname (dptr), (int)(uptr-dptr->units), cptr, sprint_capac (dptr, uptr));
                uptr->capac = saved_capac;
                sim_printf ("%s)\n", sprint_capac (dptr, uptr));
                }
            sim_disk_detach (uptr);
            return SCPE_OPENERR;
            }
        if ((capac < (((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1))) && (DKUF_F_STD != DK_GET_FMT (uptr))) {
            if (!sim_quiet) {
                uptr->capac = (t_addr)(capac/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));
                sim_printf ("%s%d: non expandable disk %s is smaller than simulated device (%s < ", sim_dname (dptr), (int)(uptr-dptr->units), cptr, sprint_capac (dptr, uptr));
                uptr->capac = saved_capac;
                sim_printf ("%s)\n", sprint_capac (dptr, uptr));
                }
            sim_disk_detach (uptr);
            return SCPE_OPENERR;
            }
        }
    else {
        if ((filesystem_capac != (t_offset)-1) &&           /* Known file system data size AND */
            (filesystem_capac > capac))                     /* Data size greater than container size? */
            capac = filesystem_capac;                       /* Use file system data size */
        if (((filesystem_capac != (t_offset)-1) &&           /* Known file system data size AND */
             (capac > (((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)))) || /* Data > current size */
            (DKUF_F_STD != DK_GET_FMT (uptr)))              /* OR ! autosizeable disk */
            uptr->capac = (t_addr)(capac/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));  /* update current size */
        }
    }

#if defined (SIM_ASYNCH_IO)
sim_disk_set_async (uptr, completion_delay);
#endif
uptr->io_flush = _sim_disk_io_flush;

return SCPE_OK;
}

t_stat sim_disk_detach (UNIT *uptr)
{
struct disk_context *ctx;
int (*close_function)(FILE *f);
FILE *fileref;
t_bool auto_format;

if ((uptr == NULL) || !(uptr->flags & UNIT_ATT))
    return SCPE_NOTATT;

ctx = (struct disk_context *)uptr->disk_ctx;
fileref = uptr->fileref;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_detach(unit=%d,filename='%s')\n", (int)(uptr-ctx->dptr->units), uptr->filename);

switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* Simh */
        close_function = fclose;
        break;
    case DKUF_F_VHD:                                    /* Virtual Disk */
        close_function = sim_vhd_disk_close;
        break;
    case DKUF_F_RAW:                                    /* Physical */
        close_function = sim_os_disk_close_raw;
        break;
    default:
        return SCPE_IERR;
        }
if (!(uptr->flags & UNIT_ATTABLE))                      /* attachable? */
    return SCPE_NOATT;
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (NULL == find_dev_from_unit (uptr))
    return SCPE_OK;
auto_format = ctx->auto_format;

if (uptr->io_flush)
    uptr->io_flush (uptr);                              /* flush buffered data */

sim_disk_clr_async (uptr);

uptr->flags &= ~(UNIT_ATT | UNIT_RO);
uptr->dynflags &= ~(UNIT_NO_FIO | UNIT_DISK_CHK);
free (uptr->filename);
uptr->filename = NULL;
uptr->fileref = NULL;
free (uptr->disk_ctx);
uptr->disk_ctx = NULL;
uptr->io_flush = NULL;
if (auto_format)
    sim_disk_set_fmt (uptr, 0, "AUTO", NULL);           /* restore file format */
if (close_function (fileref) == EOF)
    return SCPE_IOERR;
return SCPE_OK;
}

t_stat sim_disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s Disk Attach Help\n\n", dptr->name);

fprintf (st, "Disk container files can be one of 3 different types:\n\n");
fprintf (st, "    SIMH   A disk is an unstructured binary file of the size appropriate\n");
fprintf (st, "           for the disk drive being simulated\n");
fprintf (st, "    VHD    Virtual Disk format which is described in the \"Microsoft\n");
fprintf (st, "           Virtual Hard Disk (VHD) Image Format Specification\".  The\n");
fprintf (st, "           VHD implementation includes support for 1) Fixed (Preallocated)\n");
fprintf (st, "           disks, 2) Dynamically Expanding disks, and 3) Differencing disks.\n");
fprintf (st, "    RAW    platform specific access to physical disk or CDROM drives\n\n");
fprintf (st, "Virtual (VHD) Disks  supported conform to \"Virtual Hard Disk Image Format\n");
fprintf (st, "Specification\", Version 1.0 October 11, 2006.\n");
fprintf (st, "Dynamically expanding disks never change their \"Virtual Size\", but they don't\n");
fprintf (st, "consume disk space on the containing storage until the virtual sectors in the\n");
fprintf (st, "disk are actually written to (i.e. a 2GB Dynamic disk container file with only\n");
fprintf (st, "30MB of data will initially be about 30MB in size and this size will grow up to\n");
fprintf (st, "2GB as different sectors are written to.  The VHD format contains metadata\n");
fprintf (st, "which describes the drive size and the simh device type in use when the VHD\n");
fprintf (st, "was created.  This metadata is therefore available whenever that VHD is\n");
fprintf (st, "attached to an emulated disk device in the future so the device type and\n");
fprintf (st, "size can be automatically be configured.\n\n");

if (0 == (uptr-dptr->units)) {
    if (dptr->numunits > 1) {
        uint32 i;

        for (i=0; i < dptr->numunits; ++i)
            if (dptr->units[i].flags & UNIT_ATTABLE)
                fprintf (st, "  sim> ATTACH {switches} %s%d diskfile\n", dptr->name, i);
        }
    else
        fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", dptr->name);
    }
else
    fprintf (st, "  sim> ATTACH {switches} %s diskfile\n\n", dptr->name);
fprintf (st, "\n%s attach command switches\n", dptr->name);
fprintf (st, "    -R          Attach Read Only.\n");
fprintf (st, "    -E          Must Exist (if not specified an attempt to create the indicated\n");
fprintf (st, "                disk container will be attempted).\n");
fprintf (st, "    -F          Open the indicated disk container in a specific format (default\n");
fprintf (st, "                is to autodetect VHD defaulting to simh if the indicated\n");
fprintf (st, "                container is not a VHD).\n");
fprintf (st, "    -I          Initialize newly created disk so that each sector contains its\n");
fprintf (st, "                sector address\n");
fprintf (st, "    -K          Verify that the disk contents contain the sector address in each\n");
fprintf (st, "                sector.  Whole disk checked at attach time and each sector is\n");
fprintf (st, "                checked when written.\n");
fprintf (st, "    -C          Create a VHD and copy its contents from another disk (simh, VHD,\n");
fprintf (st, "                or RAW format). Add a -V switch to verify a copy operation.\n");
fprintf (st, "    -V          Perform a verification pass to confirm successful data copy\n");
fprintf (st, "                operation.\n");
fprintf (st, "    -X          When creating a VHD, create a fixed sized VHD (vs a Dynamically\n");
fprintf (st, "                expanding one).\n");
fprintf (st, "    -D          Create a Differencing VHD (relative to an already existing VHD\n");
fprintf (st, "                disk)\n");
fprintf (st, "    -M          Merge a Differencing VHD into its parent VHD disk\n");
fprintf (st, "    -O          Override consistency checks when attaching differencing disks\n");
fprintf (st, "                which have unexpected parent disk GUID or timestamps\n\n");
fprintf (st, "    -U          Fix inconsistencies which are overridden by the -O switch\n");
fprintf (st, "    -Y          Answer Yes to prompt to overwrite last track (on disk create)\n");
fprintf (st, "    -N          Answer No to prompt to overwrite last track (on disk create)\n");
fprintf (st, "Examples:\n");
fprintf (st, "  sim> show rq\n");
fprintf (st, "    RQ, address=20001468-2000146B*, no vector, 4 units\n");
fprintf (st, "    RQ0, 159MB, not attached, write enabled, RD54, autosize, SIMH format\n");
fprintf (st, "    RQ1, 159MB, not attached, write enabled, RD54, autosize, SIMH format\n");
fprintf (st, "    RQ2, 159MB, not attached, write enabled, RD54, autosize, SIMH format\n");
fprintf (st, "    RQ3, 409KB, not attached, write enabled, RX50, autosize, SIMH format\n");
fprintf (st, "  sim> atta rq0 RA81.vhd\n");
fprintf (st, "  sim> show rq0\n");
fprintf (st, "  RQ0, 456MB, attached to RA81.vhd, write enabled, RA81, autosize, VHD format\n");
fprintf (st, "  sim> set rq2 ra92\n");
fprintf (st, "  sim> att rq2 -f vhd RA92.vhd\n");
fprintf (st, "  RQ2: creating new file\n");
fprintf (st, "  sim> sho rq2\n");
fprintf (st, "  RQ2, 1505MB, attached to RA92.vhd, write enabled, RA92, autosize, VHD format\n");
fprintf (st, "  sim> ! dir RA92.vhd\n");
fprintf (st, "   Volume in drive H is New Volume\n");
fprintf (st, "   Volume Serial Number is F8DE-510C\n\n");
fprintf (st, "   Directory of H:\\Data\n\n");
fprintf (st, "  04/14/2011  12:57 PM             5,120 RA92.vhd\n");
fprintf (st, "                 1 File(s)          5,120 bytes\n");
fprintf (st, "  sim> atta rq3 -d RA92-1-Diff.vhd RA92.vhd\n");
fprintf (st, "  sim> atta rq3 -c RA92-1.vhd RA92.vhd\n");
fprintf (st, "  RQ3: creating new virtual disk 'RA92-1.vhd'\n");
fprintf (st, "  RQ3: Copied 1505MB.  99%% complete.\n");
fprintf (st, "  RQ3: Copied 1505MB. Done.\n");
fprintf (st, "  sim> sh rq3\n");
fprintf (st, "  RQ3, 1505MB, attached to RA92-1.vhd, write enabled, RA92, autosize, VHD format\n");
fprintf (st, "  sim>  ! dir RA92*\n");
fprintf (st, "   Volume in drive H is New Volume\n");
fprintf (st, "   Volume Serial Number is F8DE-510C\n\n");
fprintf (st, "   Directory of H:\\Data\n\n");
fprintf (st, "  04/14/2011  01:12 PM             5,120 RA92-1.vhd\n");
fprintf (st, "  04/14/2011  12:58 PM             5,120 RA92.vhd\n");
fprintf (st, "                 2 File(s)         10,240 bytes\n");
fprintf (st, "  sim> sho rq2\n");
fprintf (st, "  RQ2, 1505MB, not attached, write enabled, RA92, autosize, VHD format\n");
fprintf (st, "  sim> set rq2 ra81\n");
fprintf (st, "  sim> set rq2 noauto\n");
fprintf (st, "  sim> sho rq2\n");
fprintf (st, "  RQ2, 456MB, not attached, write enabled, RA81, noautosize, VHD format\n");
fprintf (st, "  sim> set rq2 format=simh\n");
fprintf (st, "  sim> sho rq2\n");
fprintf (st, "  RQ2, 456MB, not attached, write enabled, RA81, noautosize, SIMH format\n");
fprintf (st, "  sim> atta rq2 -c RA81-Copy.vhd VMS055.dsk\n");
fprintf (st, "  RQ2: creating new virtual disk 'RA81-Copy.vhd'\n");
fprintf (st, "  RQ2: Copied 456MB.  99%% complete.\n");
fprintf (st, "  RQ2: Copied 456MB. Done.\n");
fprintf (st, "  sim> sho rq2\n");
fprintf (st, "  RQ2, 456MB, attached to RA81-Copy.vhd, write enabled, RA81, noautosize, VHD format\n");
return SCPE_OK;
}

t_bool sim_disk_vhd_support (void)
{
return SCPE_OK == sim_vhd_disk_implemented ();
}

t_bool sim_disk_raw_support (void)
{
return SCPE_OK == sim_os_disk_implemented_raw ();
}

t_stat sim_disk_reset (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_reset(unit=%d)\n", (int)(uptr-ctx->dptr->units));

_sim_disk_io_flush(uptr);
AIO_VALIDATE;
AIO_UPDATE_QUEUE;
return SCPE_OK;
}

t_stat sim_disk_perror (UNIT *uptr, const char *msg)
{
int saved_errno = errno;

if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
    case DKUF_F_VHD:                                    /* VHD format */
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
#if defined(_WIN32)
        saved_errno = GetLastError ();
#endif
        perror (msg);
        sim_printf ("%s %s: %s\n", sim_uname(uptr), msg, sim_get_os_error_text (saved_errno));
        break;
    default:
        ;
    }
return SCPE_OK;
}

t_stat sim_disk_clearerr (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
        clearerr (uptr->fileref);
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        sim_vhd_disk_clearerr (uptr);
        break;
    default:
        ;
    }
return SCPE_OK;
}


/* Factory bad block table creation routine

   This routine writes a DEC standard 144 compliant bad block table on the
   last track of the specified unit as described in: 
      EL-00144_B_DEC_STD_144_Disk_Standard_for_Recording_and_Handling_Bad_Sectors_Nov76.pdf
   The bad block table consists of 10 repetitions of the same table, 
   formatted as follows:

        words 0-1       pack id number
        words 2-3       cylinder/sector/surface specifications
         :
        words n-n+1     end of table (-1,-1)

   Inputs:
        uptr    =       pointer to unit
        sec     =       number of sectors per surface
        wds     =       number of words per sector
   Outputs:
        sta     =       status code
*/

t_stat sim_disk_pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
int32 i;
t_addr da;
uint16 *buf;
DEVICE *dptr;
char *namebuf, *c;
uint32 packid;
t_stat stat = SCPE_OK;

if ((sec < 2) || (wds < 16))
    return SCPE_ARG;
if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_UNATT;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if (uptr->flags & UNIT_RO)
    return SCPE_RO;
if (!get_yn ("Overwrite last track? [N]", FALSE))
    return SCPE_OK;
if ((buf = (uint16 *) malloc (wds * sizeof (uint16))) == NULL)
    return SCPE_MEM;
namebuf = uptr->filename;
if ((c = strrchr (namebuf, '/')))
    namebuf = c+1;
if ((c = strrchr (namebuf, '\\')))
    namebuf = c+1;
if ((c = strrchr (namebuf, ']')))
    namebuf = c+1;
packid = eth_crc32(0, namebuf, strlen (namebuf));
buf[0] = (uint16)packid;
buf[1] = (uint16)(packid >> 16) & 0x7FFF;   /* Make sure MSB is clear */
buf[2] = buf[3] = 0;
for (i = 4; i < wds; i++)
    buf[i] = 0177777u;
da = (uptr->capac*((dptr->flags & DEV_SECTORS) ? 512 : 1)) - (sec * wds);
for (i = 0; (stat == SCPE_OK) && (i < sec) && (i < 10); i++, da += wds)
    if (ctx)
        stat = sim_disk_wrsect (uptr, (t_lba)(da/wds), (uint8 *)buf, NULL, 1);
    else {
        if (sim_fseek (uptr->fileref, da, SEEK_SET)) {
            stat = SCPE_IOERR;
            break;
            }
        if (wds != sim_fwrite (buf, sizeof (uint16), wds, uptr->fileref))
            stat = SCPE_IOERR;
        }
free (buf);
return stat;
}

void sim_disk_data_trace(UNIT *uptr, const uint8 *data, size_t lba, size_t len, const char* txt, int detail, uint32 reason)
{
DEVICE *dptr = find_dev_from_unit (uptr);

if (sim_deb && (dptr->dctrl & reason)) {
    char pos[32];

    sprintf (pos, "lbn: %08X ", (unsigned int)lba);
    sim_data_trace(dptr, uptr, (detail ? data : NULL), pos, len, txt, reason);
    }
}


/* OS Specific RAW Disk I/O support */

#if defined _WIN32

static void _set_errno_from_status (DWORD dwStatus)
{
switch (dwStatus) {
    case ERROR_FILE_NOT_FOUND:    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:     case ERROR_NO_MORE_FILES:
    case ERROR_BAD_NET_NAME:      case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:      case ERROR_FILENAME_EXCED_RANGE:
        errno = ENOENT;
        return;
    case ERROR_INVALID_ACCESS:    case ERROR_INVALID_DATA:
    case ERROR_INVALID_FUNCTION:  case ERROR_INVALID_PARAMETER:
    case ERROR_NEGATIVE_SEEK:
        errno = EINVAL;
        return;
    case ERROR_ARENA_TRASHED:     case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_INVALID_BLOCK:     case ERROR_NOT_ENOUGH_QUOTA:
        errno = ENOMEM;
        return;
    case ERROR_TOO_MANY_OPEN_FILES:
        errno = EMFILE;
        return;
    case ERROR_ACCESS_DENIED:     case ERROR_CURRENT_DIRECTORY:
    case ERROR_LOCK_VIOLATION:    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_CANNOT_MAKE:       case ERROR_FAIL_I24:
    case ERROR_DRIVE_LOCKED:      case ERROR_SEEK_ON_DEVICE:
    case ERROR_NOT_LOCKED:        case ERROR_LOCK_FAILED:
        errno = EACCES;
        return;
    case ERROR_ALREADY_EXISTS:    case ERROR_FILE_EXISTS:
        errno = EEXIST;
        return;
    case ERROR_INVALID_HANDLE:    case ERROR_INVALID_TARGET_HANDLE:
    case ERROR_DIRECT_ACCESS_HANDLE:
        errno = EBADF;
        return;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        return;
    case ERROR_BAD_ENVIRONMENT:
        errno = E2BIG;
        return;
    case ERROR_BAD_FORMAT:
        errno = ENOEXEC;
        return;
    case ERROR_NOT_SAME_DEVICE:
        errno = EXDEV;
        return;
    case ERROR_BROKEN_PIPE:
        errno = EPIPE;
        return;
    case ERROR_DISK_FULL:
        errno = ENOSPC;
        return;
    case ERROR_WAIT_NO_CHILDREN:  case ERROR_CHILD_NOT_COMPLETE:
        errno = ECHILD;
        return;
    case ERROR_NO_PROC_SLOTS:     case ERROR_MAX_THRDS_REACHED:
    case ERROR_NESTING_NOT_ALLOWED:
        errno = EAGAIN;
        return;
    }
if ((dwStatus >= ERROR_WRITE_PROTECT) && (dwStatus <= ERROR_SHARING_BUFFER_EXCEEDED)) {
    errno = EACCES;
    return;
    }
if ((dwStatus >= ERROR_INVALID_STARTING_CODESEG) && (dwStatus <= ERROR_INFLOOP_IN_RELOC_CHAIN)) {
    errno = ENOEXEC;
    return;
    }
errno = EINVAL;
}
#if defined(__GNUC__) && defined(HAVE_NTDDDISK_H)
#include <ddk/ntddstor.h>
#include <ddk/ntdddisk.h>
#else
#include <winioctl.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif
WINBASEAPI BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize);
#if defined(__cplusplus)
    }
#endif

struct _device_type {
    int32 Type;
    const char *desc;
    } DeviceTypes[] = {
        {FILE_DEVICE_8042_PORT,             "8042_PORT"},
        {FILE_DEVICE_ACPI,                  "ACPI"},
        {FILE_DEVICE_BATTERY,               "BATTERY"},
        {FILE_DEVICE_BEEP,                  "BEEP"},
#ifdef FILE_DEVICE_BLUETOOTH
        {FILE_DEVICE_BLUETOOTH,             "BLUETOOTH"},
#endif
        {FILE_DEVICE_BUS_EXTENDER,          "BUS_EXTENDER"},
        {FILE_DEVICE_CD_ROM,                "CD_ROM"},
        {FILE_DEVICE_CD_ROM_FILE_SYSTEM,    "CD_ROM_FILE_SYSTEM"},
        {FILE_DEVICE_CHANGER,               "CHANGER"},
        {FILE_DEVICE_CONTROLLER,            "CONTROLLER"},
#ifdef FILE_DEVICE_CRYPT_PROVIDER
        {FILE_DEVICE_CRYPT_PROVIDER,        "CRYPT_PROVIDER"},
#endif
        {FILE_DEVICE_DATALINK,              "DATALINK"},
        {FILE_DEVICE_DFS,                   "DFS"},
        {FILE_DEVICE_DFS_FILE_SYSTEM,       "DFS_FILE_SYSTEM"},
        {FILE_DEVICE_DFS_VOLUME,            "DFS_VOLUME"},
        {FILE_DEVICE_DISK,                  "DISK"},
        {FILE_DEVICE_DISK_FILE_SYSTEM,      "DISK_FILE_SYSTEM"},
        {FILE_DEVICE_DVD,                   "DVD"},
        {FILE_DEVICE_FILE_SYSTEM,           "FILE_SYSTEM"},
#ifdef FILE_DEVICE_FIPS
        {FILE_DEVICE_FIPS,                  "FIPS"},
#endif
        {FILE_DEVICE_FULLSCREEN_VIDEO,      "FULLSCREEN_VIDEO"},
#ifdef FILE_DEVICE_INFINIBAND
        {FILE_DEVICE_INFINIBAND,            "INFINIBAND"},
#endif
        {FILE_DEVICE_INPORT_PORT,           "INPORT_PORT"},
        {FILE_DEVICE_KEYBOARD,              "KEYBOARD"},
        {FILE_DEVICE_KS,                    "KS"},
        {FILE_DEVICE_KSEC,                  "KSEC"},
        {FILE_DEVICE_MAILSLOT,              "MAILSLOT"},
        {FILE_DEVICE_MASS_STORAGE,          "MASS_STORAGE"},
        {FILE_DEVICE_MIDI_IN,               "MIDI_IN"},
        {FILE_DEVICE_MIDI_OUT,              "MIDI_OUT"},
        {FILE_DEVICE_MODEM,                 "MODEM"},
        {FILE_DEVICE_MOUSE,                 "MOUSE"},
        {FILE_DEVICE_MULTI_UNC_PROVIDER,    "MULTI_UNC_PROVIDER"},
        {FILE_DEVICE_NAMED_PIPE,            "NAMED_PIPE"},
        {FILE_DEVICE_NETWORK,               "NETWORK"},
        {FILE_DEVICE_NETWORK_BROWSER,       "NETWORK_BROWSER"},
        {FILE_DEVICE_NETWORK_FILE_SYSTEM,   "NETWORK_FILE_SYSTEM"},
        {FILE_DEVICE_NETWORK_REDIRECTOR,    "NETWORK_REDIRECTOR"},
        {FILE_DEVICE_NULL,                  "NULL"},
        {FILE_DEVICE_PARALLEL_PORT,         "PARALLEL_PORT"},
        {FILE_DEVICE_PHYSICAL_NETCARD,      "PHYSICAL_NETCARD"},
        {FILE_DEVICE_PRINTER,               "PRINTER"},
        {FILE_DEVICE_SCANNER,               "SCANNER"},
        {FILE_DEVICE_SCREEN,                "SCREEN"},
        {FILE_DEVICE_SERENUM,               "SERENUM"},
        {FILE_DEVICE_SERIAL_MOUSE_PORT,     "SERIAL_MOUSE_PORT"},
        {FILE_DEVICE_SERIAL_PORT,           "SERIAL_PORT"},
        {FILE_DEVICE_SMARTCARD,             "SMARTCARD"},
        {FILE_DEVICE_SMB,                   "SMB"},
        {FILE_DEVICE_SOUND,                 "SOUND"},
        {FILE_DEVICE_STREAMS,               "STREAMS"},
        {FILE_DEVICE_TAPE,                  "TAPE"},
        {FILE_DEVICE_TAPE_FILE_SYSTEM,      "TAPE_FILE_SYSTEM"},
        {FILE_DEVICE_TERMSRV,               "TERMSRV"},
        {FILE_DEVICE_TRANSPORT,             "TRANSPORT"},
        {FILE_DEVICE_UNKNOWN,               "UNKNOWN"},
        {FILE_DEVICE_VDM,                   "VDM"},
        {FILE_DEVICE_VIDEO,                 "VIDEO"},
        {FILE_DEVICE_VIRTUAL_DISK,          "VIRTUAL_DISK"},
#ifdef FILE_DEVICE_VMBUS
        {FILE_DEVICE_VMBUS,                 "VMBUS"},
#endif
        {FILE_DEVICE_WAVE_IN,               "WAVE_IN"},
        {FILE_DEVICE_WAVE_OUT,              "WAVE_OUT"},
#ifdef FILE_DEVICE_WPD
        {FILE_DEVICE_WPD,                   "WPD"},
#endif
        {0,                                 NULL}};

static const char *_device_type_name (int DeviceType)
{
int i;

for (i=0; DeviceTypes[i].desc; i++)
    if (DeviceTypes[i].Type == DeviceType)
        return DeviceTypes[i].desc;
return "Unknown";
}

static t_stat sim_os_disk_implemented_raw (void)
{
return sim_toffset_64 ? SCPE_OK : SCPE_NOFNC;
}

static FILE *sim_os_disk_open_raw (const char *rawdevicename, const char *openmode)
{
HANDLE Handle;
DWORD DesiredAccess = 0;
uint32 is_cdrom;
char *tmpname = (char *)malloc (2 + strlen (rawdevicename));

if (tmpname == NULL)
    return NULL;
if (strchr (openmode, 'r'))
    DesiredAccess |= GENERIC_READ;
if (strchr (openmode, 'w') || strchr (openmode, '+'))
    DesiredAccess |= GENERIC_WRITE;
/* SCP Command Line parsing replaces \\ with \ presuming this is an 
   escape sequence.  This only affecdts RAW device names and UNC paths.
   We handle the RAW device name case here by prepending paths beginning 
   with \.\ with an extra \. */
if ((!memcmp ("\\.\\", rawdevicename, 3)) ||
    (!memcmp ("/./", rawdevicename, 3))) {
    *tmpname = '\\';
    strcpy (tmpname + 1, rawdevicename);
    }
else
    strcpy (tmpname, rawdevicename);
Handle = CreateFileA (tmpname, DesiredAccess, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS|FILE_FLAG_WRITE_THROUGH, NULL);
free (tmpname);
if (Handle != INVALID_HANDLE_VALUE) {
    if ((sim_os_disk_info_raw ((FILE *)Handle, NULL, NULL, &is_cdrom)) || 
        ((DesiredAccess & GENERIC_WRITE) && is_cdrom)) {
        CloseHandle (Handle);
        errno = EACCES;
        return NULL;
        }
    return (FILE *)Handle;
    }
_set_errno_from_status (GetLastError ());
return NULL;
}

static int sim_os_disk_close_raw (FILE *f)
{
if (!CloseHandle ((HANDLE)f)) {
    _set_errno_from_status (GetLastError ());
    return EOF;
    }
return 0;
}

static void sim_os_disk_flush_raw (FILE *f)
{
FlushFileBuffers ((HANDLE)f);
}

static t_offset sim_os_disk_size_raw (FILE *Disk)
{
DWORD IoctlReturnSize;
LARGE_INTEGER Size;

if (GetFileSizeEx((HANDLE)Disk, &Size))
    return (t_offset)(Size.QuadPart);
#ifdef IOCTL_STORAGE_READ_CAPACITY
if (1) {
    STORAGE_READ_CAPACITY S;

    ZeroMemory (&S, sizeof (S));
    S.Version = sizeof (STORAGE_READ_CAPACITY);
    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_STORAGE_READ_CAPACITY,      /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &S,                      /* output buffer */
                         (DWORD) sizeof(S),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        return (t_offset)(S.DiskLength.QuadPart);
    }
#endif
#ifdef IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
if (1) {
    DISK_GEOMETRY_EX G;

    ZeroMemory (&G, sizeof (G));
    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &G,                      /* output buffer */
                         (DWORD) sizeof(G),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        return (t_offset)(G.DiskSize.QuadPart);
    }
#endif
#ifdef IOCTL_DISK_GET_DRIVE_GEOMETRY
if (1) {
    DISK_GEOMETRY G;

    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_DISK_GET_DRIVE_GEOMETRY,    /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &G,                      /* output buffer */
                         (DWORD) sizeof(G),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        return (t_offset)(G.Cylinders.QuadPart*G.TracksPerCylinder*G.SectorsPerTrack*G.BytesPerSector);
    }
#endif
_set_errno_from_status (GetLastError ());
return (t_offset)-1;
}

static t_stat sim_os_disk_unload_raw (FILE *Disk)
{
#ifdef IOCTL_STORAGE_EJECT_MEDIA
DWORD BytesReturned;
uint32 Removable = FALSE;

sim_os_disk_info_raw (Disk, NULL, &Removable, NULL);
if (Removable) {
    if (!DeviceIoControl((HANDLE)Disk,                  /* handle to disk */
                         IOCTL_STORAGE_EJECT_MEDIA,     /* dwIoControlCode */
                         NULL,                          /* lpInBuffer */
                         0,                             /* nInBufferSize */
                         NULL,                          /* lpOutBuffer */
                         0,                             /* nOutBufferSize */
                         (LPDWORD) &BytesReturned,      /* number of bytes returned */
                         (LPOVERLAPPED) NULL)) {        /* OVERLAPPED structure */
        _set_errno_from_status (GetLastError ());
        return SCPE_IOERR;
        }
    }
return SCPE_OK;
#else
return SCPE_NOFNC;
#endif
}

static t_bool sim_os_disk_isavailable_raw (FILE *Disk)
{
#ifdef IOCTL_STORAGE_EJECT_MEDIA
DWORD BytesReturned;
uint32 Removable = FALSE;

sim_os_disk_info_raw (Disk, NULL, &Removable, NULL);
if (Removable) {
    if (!DeviceIoControl((HANDLE)Disk,                  /* handle to disk */
                         IOCTL_STORAGE_CHECK_VERIFY,    /* dwIoControlCode */
                         NULL,                          /* lpInBuffer */
                         0,                             /* nInBufferSize */
                         NULL,                          /* lpOutBuffer */
                         0,                             /* nOutBufferSize */
                         (LPDWORD) &BytesReturned,      /* number of bytes returned */
                         (LPOVERLAPPED) NULL)) {        /* OVERLAPPED structure */
        _set_errno_from_status (GetLastError ());
        return FALSE;
        }
    }
#endif
return TRUE;
}

static t_stat sim_os_disk_info_raw (FILE *Disk, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom)
{
DWORD IoctlReturnSize;
STORAGE_DEVICE_NUMBER Device;

ZeroMemory (&Device, sizeof (Device));
DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                IOCTL_STORAGE_GET_DEVICE_NUMBER,  /* dwIoControlCode */
                NULL,                             /* lpInBuffer */
                0,                                /* nInBufferSize */
                (LPVOID) &Device,                 /* output buffer */
                (DWORD) sizeof(Device),           /* size of output buffer */
                (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                (LPOVERLAPPED) NULL);             /* OVERLAPPED structure */

if (sector_size)
    *sector_size = 512;
if (removable)
    *removable = 0;
if (is_cdrom)
    *is_cdrom = (Device.DeviceType == FILE_DEVICE_CD_ROM) || (Device.DeviceType == FILE_DEVICE_DVD);
#ifdef IOCTL_STORAGE_READ_CAPACITY
if (1) {
    STORAGE_READ_CAPACITY S;

    ZeroMemory (&S, sizeof (S));
    S.Version = sizeof (STORAGE_READ_CAPACITY);
    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_STORAGE_READ_CAPACITY,      /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &S,                      /* output buffer */
                         (DWORD) sizeof(S),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        if (sector_size)
            *sector_size = S.BlockLength;
    }
#endif
#ifdef IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
if (1) {
    DISK_GEOMETRY_EX G;

    ZeroMemory (&G, sizeof (G));
    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &G,                      /* output buffer */
                         (DWORD) sizeof(G),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        if (sector_size)
            *sector_size = G.Geometry.BytesPerSector;
    }
#endif
#ifdef IOCTL_DISK_GET_DRIVE_GEOMETRY
if (1) {
    DISK_GEOMETRY G;

    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_DISK_GET_DRIVE_GEOMETRY,    /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &G,                      /* output buffer */
                         (DWORD) sizeof(G),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        if (sector_size)
            *sector_size = G.BytesPerSector;
    }
#endif
#ifdef IOCTL_STORAGE_GET_HOTPLUG_INFO
if (1) {
    STORAGE_HOTPLUG_INFO H;

    ZeroMemory (&H, sizeof (H));
    if (DeviceIoControl((HANDLE)Disk,                      /* handle to volume */
                         IOCTL_STORAGE_GET_HOTPLUG_INFO,   /* dwIoControlCode */
                         NULL,                             /* lpInBuffer */
                         0,                                /* nInBufferSize */
                         (LPVOID) &H,                      /* output buffer */
                         (DWORD) sizeof(H),                /* size of output buffer */
                         (LPDWORD) &IoctlReturnSize,       /* number of bytes returned */
                         (LPOVERLAPPED) NULL))             /* OVERLAPPED structure */
        if (removable)
            *removable = H.MediaRemovable;
    }
#endif
return SCPE_OK;
}

static t_stat sim_os_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
OVERLAPPED pos;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
long long addr;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

addr = ((long long)lba) * ctx->sector_size;
memset (&pos, 0, sizeof (pos));
pos.Offset = (DWORD)addr;
pos.OffsetHigh = (DWORD)(addr >> 32);
if (ReadFile ((HANDLE)(uptr->fileref), buf, sects * ctx->sector_size, (LPDWORD)sectsread, &pos)) {
    if (sectsread)
        *sectsread /= ctx->sector_size;
    return SCPE_OK;
    }
_set_errno_from_status (GetLastError ());
return SCPE_IOERR;
}

static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
OVERLAPPED pos;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
long long addr;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

addr = ((long long)lba) * ctx->sector_size;
memset (&pos, 0, sizeof (pos));
pos.Offset = (DWORD)addr;
pos.OffsetHigh = (DWORD)(addr >> 32);
if (WriteFile ((HANDLE)(uptr->fileref), buf, sects * ctx->sector_size, (LPDWORD)sectswritten, &pos)) {
    if (sectswritten)
        *sectswritten /= ctx->sector_size;
    return SCPE_OK;
    }
_set_errno_from_status (GetLastError ());
return SCPE_IOERR;
}

#elif defined (__linux) || defined (__linux__) || defined (__sun) || defined (__sun__) || defined (__hpux) || defined (_AIX)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(HAVE_SYS_IOCTL)
#include <sys/ioctl.h>
#endif
#if defined(HAVE_LINUX_CDROM)
#include <linux/cdrom.h>
#endif

static t_stat sim_os_disk_implemented_raw (void)
{
return sim_toffset_64 ? SCPE_OK : SCPE_NOFNC;
}

static FILE *sim_os_disk_open_raw (const char *rawdevicename, const char *openmode)
{
int mode = 0;
int fd;

if (strchr (openmode, 'r') && (strchr (openmode, '+') || strchr (openmode, 'w')))
    mode = O_RDWR;
else
    if (strchr (openmode, 'r'))
        mode = O_RDONLY;
#ifdef O_LARGEFILE
mode |= O_LARGEFILE;
#endif
#ifdef O_DSYNC
mode |= O_DSYNC;
#endif
fd = open (rawdevicename, mode, 0);
if (fd < 0)
    return (FILE *)NULL;
return (FILE *)((long)fd);
}

static int sim_os_disk_close_raw (FILE *f)
{
return close ((int)((long)f));
}

static void sim_os_disk_flush_raw (FILE *f)
{
fsync ((int)((long)f));
}

static t_offset sim_os_disk_size_raw (FILE *f)
{
t_offset pos, size;

pos = (t_offset)lseek ((int)((long)f), (off_t)0, SEEK_CUR);
size = (t_offset)lseek ((int)((long)f), (off_t)0, SEEK_END);
if (pos != (t_offset)-1)
    (void)lseek ((int)((long)f), (off_t)pos, SEEK_SET);
return size;
}

static t_stat sim_os_disk_unload_raw (FILE *f)
{
#if defined(CDROM_GET_CAPABILITY) && defined(CDROMEJECT) && defined(CDROMEJECT_SW)
if (ioctl ((int)((long)f), CDROM_GET_CAPABILITY, NULL) < 0)
    return SCPE_OK;
if (ioctl((int)((long)f), CDROM_LOCKDOOR, 0) < 0)
    return SCPE_IOERR;
if (ioctl((int)((long)f), CDROMEJECT) < 0)
    return SCPE_IOERR;
#endif
return SCPE_OK;
}

static t_bool sim_os_disk_isavailable_raw (FILE *Disk)
{
#if defined(CDROMSTART) && defined(CDROM_GET_CAPABILITY)
if (ioctl ((int)((long)Disk), CDROM_GET_CAPABILITY, NULL) < 0)
    return TRUE;
switch (ioctl((int)((long)Disk), CDROM_DRIVE_STATUS, CDSL_NONE)) {
    case CDS_NO_INFO:
    case CDS_NO_DISC:
    case CDS_TRAY_OPEN:
    case CDS_DRIVE_NOT_READY:
    default: /* error */
        return FALSE;
    case CDS_DISC_OK:
        return TRUE;
    }
#endif
return TRUE;
}

static t_stat sim_os_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
off_t addr;
ssize_t bytesread;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

addr = ((off_t)lba) * ctx->sector_size;
bytesread = pread((int)((long)uptr->fileref), buf, sects * ctx->sector_size, addr);
if (bytesread < 0) {
    if (sectsread)
        *sectsread = 0;
    return SCPE_IOERR;
    }
if (sectsread)
    *sectsread = bytesread / ctx->sector_size;
return SCPE_OK;
}

static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
off_t addr;
ssize_t byteswritten;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr-ctx->dptr->units), lba, sects);

addr = ((off_t)lba) * ctx->sector_size;
byteswritten = pwrite((int)((long)uptr->fileref), buf, sects * ctx->sector_size, addr);
if (byteswritten < 0) {
    if (sectswritten)
        *sectswritten = 0;
    return SCPE_IOERR;
    }
if (sectswritten)
    *sectswritten = byteswritten / ctx->sector_size;
return SCPE_OK;
}

static t_stat sim_os_disk_info_raw (FILE *f, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom)
{
if (sector_size) {
#if defined(BLKSSZGET)
    if (ioctl ((int)((long)f), BLKSSZGET, sector_size) < 0)
#endif
        *sector_size = 512;
    }
if (removable)
    *removable = 0;
if (is_cdrom) {
#if defined(CDROM_GET_CAPABILITY)
    int cd_cap = ioctl ((int)((long)f), CDROM_GET_CAPABILITY, NULL);

    if (cd_cap < 0)
        *is_cdrom = 0;
    else {
        *is_cdrom = 1;
        if (removable)
            *removable = 1;
        if (sector_size)
            *sector_size = 2048;
        }
#else
    *is_cdrom = 0;
#endif
    }
return SCPE_OK;
}

#else
/*============================================================================*/
/*                        Non-implemented versions                            */
/*============================================================================*/

static t_stat sim_os_disk_implemented_raw (void)
{
return SCPE_NOFNC;
}

static FILE *sim_os_disk_open_raw (const char *rawdevicename, const char *openmode)
{
return NULL;
}

static int sim_os_disk_close_raw (FILE *f)
{
return EOF;
}

static void sim_os_disk_flush_raw (FILE *f)
{
}

static t_offset sim_os_disk_size_raw (FILE *f)
{
return (t_offset)-1;
}

static t_stat sim_os_disk_unload_raw (FILE *f)
{
return SCPE_NOFNC;
}

static t_bool sim_os_disk_isavailable_raw (FILE *Disk)
{
return FALSE;
}

static t_stat sim_os_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
return SCPE_NOFNC;
}

static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
return SCPE_NOFNC;
}

static t_stat sim_os_disk_info_raw (FILE *f, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom)
{
return SCPE_NOFNC;
}

#endif

/* OS Independent Disk Virtual Disk (VHD) I/O support */

#if (defined (VMS) && !(defined (__ALPHA) || defined (__ia64)))
#define DONT_DO_VHD_SUPPORT  /* VAX/VMS compilers don't have 64 bit integers */
#endif

#if defined (DONT_DO_VHD_SUPPORT)

/*============================================================================*/
/*                        Non-implemented version                             */
/*   This is only for hody systems which don't have 64 bit integer types      */
/*============================================================================*/

static t_stat sim_vhd_disk_implemented (void)
{
return SCPE_NOFNC;
}

static FILE *sim_vhd_disk_open (const char *vhdfilename, const char *openmode)
{
return NULL;
}

static FILE *sim_vhd_disk_merge (const char *szVHDPath, char **ParentVHD)
{
return NULL;
}

static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize)
{
return NULL;
}

static FILE *sim_vhd_disk_create_diff (const char *szVHDPath, const char *szParentVHDPath)
{
return NULL;
}

static int sim_vhd_disk_close (FILE *f)
{
return -1;
}

static void sim_vhd_disk_flush (FILE *f)
{
}

static t_offset sim_vhd_disk_size (FILE *f)
{
return (t_offset)-1;
}

static t_stat sim_vhd_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_clearerr (UNIT *uptr)
{
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype)
{
return SCPE_NOFNC;
}

static const char *sim_vhd_disk_get_dtype (FILE *f)
{
return NULL;
}

#else

/*++
    This code follows the details specified in the "Virtual Hard Disk Image
    Format Specification", Version 1.0 October 11, 2006.  This format
    specification is available for anyone to implement under the
    "Microsoft Open Specification Promise" described at:
        http://www.microsoft.com/interop/osp/default.mspx.
--*/

typedef t_uint64    uint64;
typedef t_int64     int64;

typedef struct _VHD_Footer {
    /*
    Cookies are used to uniquely identify the original creator of the hard disk
    image. The values are case-sensitive.  Microsoft uses the "conectix" string
    to identify this file as a hard disk image created by Microsoft Virtual
    Server, Virtual PC, and predecessor products. The cookie is stored as an
    eight-character ASCII string with the "c" in the first byte, the "o" in
    the second byte, and so on.
    */
    char Cookie[8];
    /*
    This is a bit field used to indicate specific feature support. The following
    table displays the list of features.
    Any fields not listed are reserved.

    Feature Value:
       No features enabled     0x00000000
       Temporary               0x00000001
       Reserved                0x00000002

       No features enabled.
              The hard disk image has no special features enabled in it.
       Temporary.
              This bit is set if the current disk is a temporary disk. A
              temporary disk designation indicates to an application that
              this disk is a candidate for deletion on shutdown.
       Reserved.
              This bit must always be set to 1.
       All other bits are also reserved and should be set to 0.
    */
    uint32 Features;
    /*
    This field is divided into a major/minor version and matches the version of
    the specification used in creating the file. The most-significant two bytes
    are for the major version. The least-significant two bytes are the minor
    version.  This must match the file format specification. For the current
    specification, this field must be initialized to 0x00010000.
    The major version will be incremented only when the file format is modified
    in such a way that it is no longer compatible with older versions of the
    file format.
    */
    uint32 FileFormatVersion;
    /*
    This field holds the absolute byte offset, from the beginning of the file,
    to the next structure. This field is used for dynamic disks and differencing
    disks, but not fixed disks. For fixed disks, this field should be set to
    0xFFFFFFFF.
    */
    uint64 DataOffset;
    /*
    This field stores the creation time of a hard disk image. This is the number
    of seconds since January 1, 2000 12:00:00 AM in UTC/GMT.
    */
    uint32 TimeStamp;
    /*
    This field is used to document which application created the hard disk. The
    field is a left-justified text field. It uses a single-byte character set.
    If the hard disk is created by Microsoft Virtual PC, "vpc " is written in
    this field. If the hard disk image is created by Microsoft Virtual Server,
    then "vs  " is written in this field.
    Other applications should use their own unique identifiers.
    */
    char CreatorApplication[4];
    /*
    This field holds the major/minor version of the application that created
    the hard disk image.  Virtual Server 2004 sets this value to 0x00010000 and
    Virtual PC 2004 sets this to 0x00050000.
    */
    uint32 CreatorVersion;
    /*
    This field stores the type of host operating system this disk image is
    created on.
       Host OS type    Value
       Windows         0x5769326B (Wi2k)
       Macintosh       0x4D616320 (Mac )
    */
    uint8 CreatorHostOS[4];
    /*
    This field stores the size of the hard disk in bytes, from the perspective
    of the virtual machine, at creation time. This field is for informational
    purposes.
    */
    uint64 OriginalSize;
    /*
    This field stores the current size of the hard disk, in bytes, from the
    perspective of the virtual machine.
    This value is same as the original size when the hard disk is created.
    This value can change depending on whether the hard disk is expanded.
    */
    uint64 CurrentSize;
    /*
    This field stores the cylinder, heads, and sectors per track value for the
    hard disk.
       Disk Geometry field          Size (bytes)
       Cylinder                     2
       Heads                        1
       Sectors per track/cylinder   1

    When a hard disk is configured as an ATA hard disk, the CHS values (that is,
    Cylinder, Heads, Sectors per track) are used by the ATA controller to
    determine the size of the disk. When the user creates a hard disk of a
    certain size, the size of the hard disk image in the virtual machine is
    smaller than that created by the user. This is because CHS value calculated
    from the hard disk size is rounded down. The pseudo-code for the algorithm
    used to determine the CHS values can be found in the appendix of this
    document.
    */
    uint32 DiskGeometry;
    /*
       Disk Type field              Value
       None                         0
       Reserved (deprecated)        1
       Fixed hard disk              2
       Dynamic hard disk            3
       Differencing hard disk       4
       Reserved (deprecated)        5
       Reserved (deprecated)        6
    */
    uint32 DiskType;
    /*
    This field holds a basic checksum of the hard disk footer. It is just a
    one's complement of the sum of all the bytes in the footer without the
    checksum field.
    If the checksum verification fails, the Virtual PC and Virtual Server
    products will instead use the header. If the checksum in the header also
    fails, the file should be assumed to be corrupt. The pseudo-code for the
    algorithm used to determine the checksum can be found in the appendix of
    this document.
    */
    uint32 Checksum;
    /*
    Every hard disk has a unique ID stored in the hard disk. This is used to
    identify the hard disk. This is a 128-bit universally unique identifier
    (UUID). This field is used to associate a parent hard disk image with its
    differencing hard disk image(s).
    */
    uint8 UniqueID[16];
    /*
    This field holds a one-byte flag that describes whether the system is in
    saved state. If the hard disk is in the saved state the value is set to 1.
    Operations such as compaction and expansion cannot be performed on a hard
    disk in a saved state.
    */
    uint8 SavedState;
    /*
    This field contains zeroes. It is 427 bytes in size.
    */
    uint8 Reserved1[11];
    /*
    This field is an extension to the VHD spec and includes a simh drive type
    name as a nul terminated string.
    */
    uint8 DriveType[16];
    /*
    This field contains zeroes. It is 400 bytes in size.
    */
    uint8 Reserved[400];
    } VHD_Footer;

/*
For dynamic and differencing disk images, the "Data Offset" field within
the image footer points to a secondary structure that provides additional
information about the disk image. The dynamic disk header should appear on
a sector (512-byte) boundary.
*/
typedef struct _VHD_DynamicDiskHeader {
    /*
    This field holds the value "cxsparse". This field identifies the header.
    */
    char Cookie[8];
    /*
    This field contains the absolute byte offset to the next structure in the
    hard disk image. It is currently unused by existing formats and should be
    set to 0xFFFFFFFF.
    */
    uint64 DataOffset;
    /*
    This field stores the absolute byte offset of the Block Allocation Table
    (BAT) in the file.
    */
    uint64 TableOffset;
    /*
    This field stores the version of the dynamic disk header. The field is
    divided into Major/Minor version. The least-significant two bytes represent
    the minor version, and the most-significant two bytes represent the major
    version. This must match with the file format specification. For this
    specification, this field must be initialized to 0x00010000.
    The major version will be incremented only when the header format is
    modified in such a way that it is no longer compatible with older versions
    of the product.
    */
    uint32 HeaderVersion;
    /*
    This field holds the maximum entries present in the BAT. This should be
    equal to the number of blocks in the disk (that is, the disk size divided
    by the block size).
    */
    uint32 MaxTableEntries;
    /*
    A block is a unit of expansion for dynamic and differencing hard disks. It
    is stored in bytes. This size does not include the size of the block bitmap.
    It is only the size of the data section of the block. The sectors per block
    must always be a power of two. The default value is 0x00200000 (indicating a
    block size of 2 MB).
    */
    uint32 BlockSize;
    /*
    This field holds a basic checksum of the dynamic header. It is a one's
    complement of the sum of all the bytes in the header without the checksum
    field.
    If the checksum verification fails the file should be assumed to be corrupt.
    */
    uint32 Checksum;
    /*
    This field is used for differencing hard disks. A differencing hard disk
    stores a 128-bit UUID of the parent hard disk. For more information, see
    "Creating Differencing Hard Disk Images" later in this paper.
    */
    uint8 ParentUniqueID[16];
    /*
    This field stores the modification time stamp of the parent hard disk. This
    is the number of seconds since January 1, 2000 12:00:00 AM in UTC/GMT.
    */
    uint32 ParentTimeStamp;
    /*
    This field should be set to zero.
    */
    uint32 Reserved0;
    /*
    This field contains a Unicode string (UTF-16) of the parent hard disk
    filename.
    */
    char ParentUnicodeName[512];
    /*
    These entries store an absolute byte offset in the file where the parent
    locator for a differencing hard disk is stored. This field is used only for
    differencing disks and should be set to zero for dynamic disks.
    */
    struct VHD_ParentLocator {
        /*
        The platform code describes which platform-specific format is used for the
        file locator. For Windows, a file locator is stored as a path (for example.
        "c:\disksimages\ParentDisk.vhd"). On a Macintosh system, the file locator
        is a binary large object (blob) that contains an "alias." The parent locator
        table is used to support moving hard disk images across platforms.
        Some current platform codes include the following:
           Platform Code        Description
           None (0x0)
           Wi2r (0x57693272)    [deprecated]
           Wi2k (0x5769326B)    [deprecated]
           W2ru (0x57327275)    Unicode pathname (UTF-16) on Windows relative to the differencing disk pathname.
           W2ku (0x57326B75)    Absolute Unicode (UTF-16) pathname on Windows.
           Mac (0x4D616320)     (Mac OS alias stored as a blob)
           MacX(0x4D616358)     A file URL with UTF-8 encoding conforming to RFC 2396.
        */
        uint8 PlatformCode[4];
        /*
        This field stores the number of 512-byte sectors needed to store the parent
        hard disk locator.
        */
        uint32 PlatformDataSpace;
        /*
        This field stores the actual length of the parent hard disk locator in bytes.
        */
        uint32 PlatformDataLength;
        /*
        This field must be set to zero.
        */
        uint32 Reserved;
        /*
        This field stores the absolute file offset in bytes where the platform
        specific file locator data is stored.
        */
        uint64 PlatformDataOffset;
        /*
        This field stores the absolute file offset in bytes where the platform
        specific file locator data is stored.
        */
        } ParentLocatorEntries[8];
    /*
    This must be initialized to zeroes.
    */
    char Reserved[256];
    } VHD_DynamicDiskHeader;

#define VHD_BAT_FREE_ENTRY (0xFFFFFFFF)
#define VHD_DATA_BLOCK_ALIGNMENT ((uint64)4096)    /* Optimum when underlying storage has 4k sectors */

#define VHD_DT_Fixed                 2  /* Fixed hard disk */
#define VHD_DT_Dynamic               3  /* Dynamic hard disk */
#define VHD_DT_Differencing          4  /* Differencing hard disk */

static uint32 NtoHl(uint32 value);

static uint64 NtoHll(uint64 value);

typedef struct VHD_IOData *VHDHANDLE;

static t_stat ReadFilePosition(FILE *File, void *buf, size_t bufsize, size_t *bytesread, uint64 position)
{
uint32 err = sim_fseeko (File, (t_offset)position, SEEK_SET);
size_t i;

if (bytesread)
    *bytesread = 0;
if (!err) {
    i = fread (buf, 1, bufsize, File);
    err = ferror (File);
    if ((!err) && bytesread)
        *bytesread = i;
    }
return (err ? SCPE_IOERR : SCPE_OK);
}

static t_stat WriteFilePosition(FILE *File, void *buf, size_t bufsize, size_t *byteswritten, uint64 position)
{
uint32 err = sim_fseeko (File, (t_offset)position, SEEK_SET);
size_t i;

if (byteswritten)
    *byteswritten = 0;
if (!err) {
    i = fwrite (buf, 1, bufsize, File);
    err = ferror (File);
    if ((!err) && byteswritten)
        *byteswritten = i;
    }
return (err ? SCPE_IOERR : SCPE_OK);
}

static uint32
CalculateVhdFooterChecksum(void *data,
                           size_t size)
{
uint32 sum = 0;
uint8 *c = (uint8 *)data;

while (size--)
    sum += *c++;
return ~sum;
}

#if defined(_WIN32) || defined (__ALPHA) || defined (__ia64) || defined (VMS)
#ifndef __BYTE_ORDER__
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#endif
#endif
#ifndef __BYTE_ORDER__
#define __BYTE_ORDER__ UNKNOWN
#endif
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static uint32
NtoHl(uint32 value)
{
uint8 *l = (uint8 *)&value;
return (uint32)l[3] | ((uint32)l[2]<<8) | ((uint32)l[1]<<16) | ((uint32)l[0]<<24);
}

static uint64
NtoHll(uint64 value)
{
uint8 *l = (uint8 *)&value;
uint64 highresult = (uint64)l[3] | ((uint64)l[2]<<8) | ((uint64)l[1]<<16) | ((uint64)l[0]<<24);
uint32 lowresult = (uint64)l[7] | ((uint64)l[6]<<8) | ((uint64)l[5]<<16) | ((uint64)l[4]<<24);
return (highresult << 32) | lowresult;
}
#elif  __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static uint32
NtoHl(uint32 value)
{
return value;
}

static uint64
NtoHll(uint64 value)
{
return value;
}
#else
static uint32
NtoHl(uint32 value)
{
uint8 *l = (uint8 *)&value;

if (sim_end)
    return l[3] | (l[2]<<8) | (l[1]<<16) | (l[0]<<24);
return value;
}

static uint64
NtoHll(uint64 value)
{
uint8 *l = (uint8 *)&value;

if (sim_end) {
    uint64 highresult = l[3] | (l[2]<<8) | (l[1]<<16) | (l[0]<<24);
    uint32 lowresult = l[7] | (l[6]<<8) | (l[5]<<16) | (l[4]<<24);
    return (highresult << 32) | lowresult;
    }
return value;
}
#endif

static
int
GetVHDFooter(const char *szVHDPath,
             VHD_Footer *sFooter,
             VHD_DynamicDiskHeader *sDynamic,
             uint32 **aBAT,
             uint32 *ModifiedTimeStamp,
             char *szParentVHDPath,
             size_t ParentVHDPathSize)
{
FILE *File = NULL;
uint64 position;
uint32 sum, saved_sum;
int Return = 0;
VHD_Footer sHeader;
struct stat statb;

memset(sFooter, '\0', sizeof(*sFooter));
if (sDynamic)
    memset(sDynamic, '\0', sizeof(*sDynamic));
if (aBAT)
    *aBAT = NULL;
File = sim_fopen (szVHDPath, "rb");
if (!File) {
    Return = errno;
    goto Return_Cleanup;
    }
if (ModifiedTimeStamp) {
    if (fstat (fileno (File), &statb)) {
        Return = errno;
        goto Return_Cleanup;
        }
    else
        *ModifiedTimeStamp = NtoHl ((uint32)(statb.st_mtime-946684800));
    }
position = sim_fsize_ex (File);
if (((int64)position) == -1) {
    Return = errno;
    goto Return_Cleanup;
    }
position -= sizeof(*sFooter);
if (ReadFilePosition(File,
                     sFooter,
                     sizeof(*sFooter),
                     NULL,
                     position)) {
    Return = errno;
    goto Return_Cleanup;
    }
saved_sum = NtoHl(sFooter->Checksum);
sFooter->Checksum = 0;
sum = CalculateVhdFooterChecksum(sFooter, sizeof(*sFooter));
sFooter->Checksum = NtoHl(saved_sum);
if ((sum != saved_sum) || (memcmp("conectix", sFooter->Cookie, sizeof(sFooter->Cookie)))) {
    Return = EINVAL;                                    /* File Corrupt */
    goto Return_Cleanup;
    }
if (ReadFilePosition(File,
                     &sHeader,
                     sizeof(sHeader),
                     NULL,
                     (uint64)0)) {
    Return = errno;
    goto Return_Cleanup;
    }
if ((NtoHl(sFooter->DiskType) != VHD_DT_Dynamic) &&
    (NtoHl(sFooter->DiskType) != VHD_DT_Differencing) &&
    (NtoHl(sFooter->DiskType) != VHD_DT_Fixed)) {
    Return = EINVAL;                                    /* File Corrupt */
    goto Return_Cleanup;
    }
if (((NtoHl(sFooter->DiskType) == VHD_DT_Dynamic) ||
     (NtoHl(sFooter->DiskType) == VHD_DT_Differencing)) &&
     memcmp(sFooter, &sHeader, sizeof(sHeader))) {
    Return = EINVAL;                                    /* File Corrupt */
    goto Return_Cleanup;
    }
if ((sDynamic) &&
    ((NtoHl(sFooter->DiskType) == VHD_DT_Dynamic) ||
     (NtoHl(sFooter->DiskType) == VHD_DT_Differencing))) {
    if (ReadFilePosition(File,
                         sDynamic,
                         sizeof (*sDynamic),
                         NULL,
                         NtoHll (sFooter->DataOffset))) {
        Return = errno;
        goto Return_Cleanup;
        }
    saved_sum = NtoHl (sDynamic->Checksum);
    sDynamic->Checksum = 0;
    sum = CalculateVhdFooterChecksum (sDynamic, sizeof(*sDynamic));
    sDynamic->Checksum = NtoHl (saved_sum);
    if ((sum != saved_sum) || (memcmp ("cxsparse", sDynamic->Cookie, sizeof (sDynamic->Cookie)))) {
        Return = errno;
        goto Return_Cleanup;
        }
    if (aBAT) {
        *aBAT = (uint32*) malloc(512*((sizeof(**aBAT)*NtoHl(sDynamic->MaxTableEntries)+511)/512));
        if (ReadFilePosition(File,
                             *aBAT,
                             sizeof (**aBAT)*NtoHl(sDynamic->MaxTableEntries),
                             NULL,
                             NtoHll (sDynamic->TableOffset))) {
            Return = EINVAL;                            /* File Corrupt */
            goto Return_Cleanup;
            }
        }
    if (szParentVHDPath && ParentVHDPathSize) {
        VHD_Footer sParentFooter;

        memset (szParentVHDPath, '\0', ParentVHDPathSize);
        if (NtoHl (sFooter->DiskType) == VHD_DT_Differencing) {
            size_t i, j;

            for (j=0; j<8; ++j) {
                uint8 *Pdata;
                uint32 PdataSize;
                char ParentName[512];
                char CheckPath[512];
                uint32 ParentModificationTime;

                if ('\0' == sDynamic->ParentLocatorEntries[j].PlatformCode[0])
                    continue;
                memset (ParentName, '\0', sizeof(ParentName));
                memset (CheckPath, '\0', sizeof(CheckPath));
                PdataSize = NtoHl(sDynamic->ParentLocatorEntries[j].PlatformDataSpace);
                Pdata = (uint8*) calloc (1, PdataSize+2);
                if (!Pdata)
                    continue;
                if (ReadFilePosition(File,
                                     Pdata,
                                     PdataSize,
                                     NULL,
                                     NtoHll (sDynamic->ParentLocatorEntries[j].PlatformDataOffset))) {
                    free (Pdata);
                    continue;
                    }
                for (i=0; i<NtoHl(sDynamic->ParentLocatorEntries[j].PlatformDataLength); i+=2)
                    if ((Pdata[i] == '\0') && (Pdata[i+1] == '\0')) {
                        ParentName[i/2] = '\0';
                        break;
                        }
                    else
                        ParentName[i/2] = Pdata[i] ? Pdata[i] : Pdata[i+1];
                free (Pdata);
                if (0 == memcmp (sDynamic->ParentLocatorEntries[j].PlatformCode, "W2ku", 4))
                    strncpy (CheckPath, ParentName, sizeof (CheckPath)-1);
                else
                    if (0 == memcmp (sDynamic->ParentLocatorEntries[j].PlatformCode, "W2ru", 4)) {
                        const char *c;

                        if ((c = strrchr (szVHDPath, '\\'))) {
                            memcpy (CheckPath, szVHDPath, c-szVHDPath+1);
                            strncpy (CheckPath+strlen(CheckPath), ParentName, sizeof (CheckPath)-(strlen (CheckPath)+1));
                            }
                        }
                VhdPathToHostPath (CheckPath, CheckPath, sizeof (CheckPath));
                if (0 == GetVHDFooter(CheckPath,
                                      &sParentFooter,
                                      NULL,
                                      NULL,
                                      &ParentModificationTime,
                                      NULL,
                                      0)) {
                    if ((0 == memcmp (sDynamic->ParentUniqueID, sParentFooter.UniqueID, sizeof (sParentFooter.UniqueID))) &&
                        ((sDynamic->ParentTimeStamp == ParentModificationTime) ||
                         ((NtoHl(sDynamic->ParentTimeStamp)-NtoHl(ParentModificationTime)) == 3600) ||
                         (sim_switches & SWMASK ('O'))))
                         strncpy (szParentVHDPath, CheckPath, ParentVHDPathSize);
                    else {
                        if (0 != memcmp (sDynamic->ParentUniqueID, sParentFooter.UniqueID, sizeof (sParentFooter.UniqueID)))
                            sim_printf ("Error Invalid Parent VHD '%s' for Differencing VHD: %s\n", CheckPath, szVHDPath);
                        else
                            sim_printf ("Error Parent VHD '%s' has been modified since Differencing VHD: %s was created\n", CheckPath, szVHDPath);
                        Return = EINVAL;                /* File Corrupt/Invalid */
                        }
                    break;
                    }
                else {
                    struct stat statb;

                    if (0 == stat (CheckPath, &statb)) {
                        sim_printf ("Parent VHD '%s' corrupt for Differencing VHD: %s\n", CheckPath, szVHDPath);
                        Return = EBADF;                /* File Corrupt/Invalid */
                        break;
                        }
                    }
                }
            if (!*szParentVHDPath) {
                if (Return != EINVAL)                   /* File Not Corrupt? */
                    sim_printf ("Missing Parent VHD for Differencing VHD: %s\n", szVHDPath);
                Return = EBADF;
                }
            }
        }
    }
Return_Cleanup:
if (File)
    fclose(File);
if (aBAT && (0 != Return)) {
    free (*aBAT);
    *aBAT = NULL;
    }
return errno = Return;
}

struct VHD_IOData {
    VHD_Footer Footer;
    VHD_DynamicDiskHeader Dynamic;
    uint32 *BAT;
    FILE *File;
    char ParentVHDPath[512];
    struct VHD_IOData *Parent;
    };

static t_stat sim_vhd_disk_implemented (void)
{
return SCPE_OK;
}

static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype)
{
VHDHANDLE hVHD  = (VHDHANDLE)f;
int Status = 0;

memset (hVHD->Footer.DriveType, '\0', sizeof hVHD->Footer.DriveType);
memcpy (hVHD->Footer.DriveType, dtype, ((1+strlen (dtype)) < sizeof (hVHD->Footer.DriveType)) ? (1+strlen (dtype)) : sizeof (hVHD->Footer.DriveType));
hVHD->Footer.Checksum = 0;
hVHD->Footer.Checksum = NtoHl (CalculateVhdFooterChecksum (&hVHD->Footer, sizeof(hVHD->Footer)));

if (NtoHl (hVHD->Footer.DiskType) == VHD_DT_Fixed) {
    if (WriteFilePosition(hVHD->File,
                          &hVHD->Footer,
                          sizeof(hVHD->Footer),
                          NULL,
                          NtoHll (hVHD->Footer.CurrentSize)))
        Status = errno;
    goto Cleanup_Return;
    }
else {
    uint64 position;

    position = sim_fsize_ex (hVHD->File);
    if (((int64)position) == -1) {
        Status = errno;
        goto Cleanup_Return;
        }
    position -= sizeof(hVHD->Footer);
    /* Update both copies on a dynamic disk */
    if (WriteFilePosition(hVHD->File,
                          &hVHD->Footer,
                          sizeof(hVHD->Footer),
                          NULL,
                          (uint64)0)) {
        Status = errno;
        goto Cleanup_Return;
        }
    if (WriteFilePosition(hVHD->File,
                          &hVHD->Footer,
                          sizeof(hVHD->Footer),
                          NULL,
                          position)) {
        Status = errno;
        goto Cleanup_Return;
        }
    }
Cleanup_Return:
if (Status)
    return SCPE_IOERR;
return SCPE_OK;
}

static const char *sim_vhd_disk_get_dtype (FILE *f)
{
VHDHANDLE hVHD  = (VHDHANDLE)f;

return (char *)(&hVHD->Footer.DriveType[0]);
}

static FILE *sim_vhd_disk_open (const char *szVHDPath, const char *DesiredAccess)
    {
    VHDHANDLE hVHD = (VHDHANDLE) calloc (1, sizeof(*hVHD));
    int NeedUpdate = FALSE;
    int Status;

    if (!hVHD)
        return (FILE *)hVHD;
    Status = GetVHDFooter (szVHDPath,
                           &hVHD->Footer,
                           &hVHD->Dynamic,
                           &hVHD->BAT,
                           NULL,
                           hVHD->ParentVHDPath,
                           sizeof (hVHD->ParentVHDPath));
    if (Status)
        goto Cleanup_Return;
    if (NtoHl (hVHD->Footer.DiskType) == VHD_DT_Differencing) {
        uint32 ParentModifiedTimeStamp;
        VHD_Footer ParentFooter;
        VHD_DynamicDiskHeader ParentDynamic;

        hVHD->Parent = (VHDHANDLE)sim_vhd_disk_open (hVHD->ParentVHDPath, "rb");
        if (!hVHD->Parent) {
            Status = errno;
            goto Cleanup_Return;
            }
        Status = GetVHDFooter (hVHD->ParentVHDPath,
                               &ParentFooter,
                               &ParentDynamic,
                               NULL,
                               &ParentModifiedTimeStamp,
                               NULL,
                               0);
        if (Status)
            goto Cleanup_Return;
        if ((0 != memcmp (hVHD->Dynamic.ParentUniqueID, ParentFooter.UniqueID, sizeof (ParentFooter.UniqueID))) || 
            (ParentModifiedTimeStamp != hVHD->Dynamic.ParentTimeStamp)) {
            if (sim_switches & SWMASK ('O')) {                      /* OVERRIDE consistency checks? */
                if ((sim_switches & SWMASK ('U')) &&                /* FIX (UPDATE) consistency checks AND */
                    (strchr (DesiredAccess, '+'))) {                /* open for write/update? */
                    memcpy (hVHD->Dynamic.ParentUniqueID, ParentFooter.UniqueID, sizeof (ParentFooter.UniqueID));
                    hVHD->Dynamic.ParentTimeStamp = ParentModifiedTimeStamp;
                    hVHD->Dynamic.Checksum = 0;
                    hVHD->Dynamic.Checksum = NtoHl (CalculateVhdFooterChecksum (&hVHD->Dynamic, sizeof(hVHD->Dynamic)));
                    NeedUpdate = TRUE;
                    }
                }
            else {
                Status = EBADF;
                goto Cleanup_Return;
                }
            }
        }
    if (hVHD->Footer.SavedState) {
        Status = EAGAIN;                                /* Busy */
        goto Cleanup_Return;
        }
    hVHD->File = sim_fopen (szVHDPath, DesiredAccess);
    if (!hVHD->File) {
        Status = errno;
        goto Cleanup_Return;
        }
Cleanup_Return:
    if (Status) {
        sim_vhd_disk_close ((FILE *)hVHD);
        hVHD = NULL;
        }
    else {
        if (NeedUpdate) {                               /* Update Differencing Disk Header? */
            if (WriteFilePosition(hVHD->File,
                                  &hVHD->Dynamic,
                                  sizeof (hVHD->Dynamic),
                                  NULL,
                                  NtoHll (hVHD->Footer.DataOffset))) {
                sim_vhd_disk_close ((FILE *)hVHD);
                hVHD = NULL;
                }
            }
        }
    errno = Status;
    return (FILE *)hVHD;
    }

static t_stat
WriteVirtualDiskSectors(VHDHANDLE hVHD,
                        uint8 *buf,
                        t_seccnt sects,
                        t_seccnt *sectswritten,
                        uint32 SectorSize,
                        t_lba lba);

static FILE *sim_vhd_disk_merge (const char *szVHDPath, char **ParentVHD)
    {
    VHDHANDLE hVHD = (VHDHANDLE) calloc (1, sizeof(*hVHD));
    VHDHANDLE Parent = NULL;
    int Status;
    uint32 SectorSize, SectorsPerBlock, BlockSize, BlockNumber, BitMapBytes, BitMapSectors, BlocksToMerge, NeededBlock;
    uint64 BlockOffset;
    size_t BytesRead;
    t_seccnt SectorsWritten;
    void *BlockData = NULL;

    if (!hVHD)
        return (FILE *)hVHD;
    if (0 != (Status = GetVHDFooter (szVHDPath,
                                     &hVHD->Footer,
                                     &hVHD->Dynamic,
                                     &hVHD->BAT,
                                     NULL,
                                     hVHD->ParentVHDPath,
                                     sizeof (hVHD->ParentVHDPath))))
        goto Cleanup_Return;
    if (NtoHl (hVHD->Footer.DiskType) != VHD_DT_Differencing) {
        Status = EINVAL;
        goto Cleanup_Return;
        }
    if (hVHD->Footer.SavedState) {
        Status = EAGAIN;                                /* Busy */
        goto Cleanup_Return;
        }
    SectorSize = 512;
    BlockSize = NtoHl (hVHD->Dynamic.BlockSize);
    BlockData = malloc (BlockSize*SectorSize);
    if (NULL == BlockData) {
        Status = errno;
        goto Cleanup_Return;
        }
    Parent = (VHDHANDLE)sim_vhd_disk_open (hVHD->ParentVHDPath, "rb+");
    if (!Parent) {
        Status = errno;
        goto Cleanup_Return;
        }
    hVHD->File = sim_fopen (szVHDPath, "rb");
    if (!hVHD->File) {
        Status = errno;
        goto Cleanup_Return;
        }
    SectorsPerBlock = NtoHl (hVHD->Dynamic.BlockSize)/SectorSize;
    BitMapBytes = (7+(NtoHl (hVHD->Dynamic.BlockSize)/SectorSize))/8;
    BitMapSectors = (BitMapBytes+SectorSize-1)/SectorSize;
    for (BlockNumber=BlocksToMerge=0; BlockNumber< NtoHl (hVHD->Dynamic.MaxTableEntries); ++BlockNumber) {
        if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY)
            continue;
        ++BlocksToMerge;
        }
    sim_messagef (SCPE_OK, "Merging %s\ninto %s\n", szVHDPath, hVHD->ParentVHDPath);
    for (BlockNumber=NeededBlock=0; BlockNumber < NtoHl (hVHD->Dynamic.MaxTableEntries); ++BlockNumber) {
        uint32 BlockSectors = SectorsPerBlock;

        if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY)
            continue;
        ++NeededBlock;
        BlockOffset = SectorSize*((uint64)(NtoHl (hVHD->BAT[BlockNumber]) + BitMapSectors));
        if (((uint64)BlockNumber*SectorsPerBlock + BlockSectors) > ((uint64)NtoHll (hVHD->Footer.CurrentSize))/SectorSize)
            BlockSectors = (uint32)(((uint64)NtoHll (hVHD->Footer.CurrentSize))/SectorSize - (BlockNumber*SectorsPerBlock));
        if (ReadFilePosition(hVHD->File,
                             BlockData,
                             SectorSize*BlockSectors,
                             &BytesRead,
                             BlockOffset))
            break;
        if (WriteVirtualDiskSectors (Parent,
                                     (uint8*)BlockData,
                                     BlockSectors,
                                     &SectorsWritten,
                                     SectorSize,
                                     SectorsPerBlock*BlockNumber))
            break;
        sim_messagef (SCPE_OK, "Merged %dMB.  %d%% complete.\r", (int)((((float)NeededBlock)*SectorsPerBlock)*SectorSize/1000000), (int)((((float)NeededBlock)*100)/BlocksToMerge));
        hVHD->BAT[BlockNumber] = VHD_BAT_FREE_ENTRY;
        }
    if (BlockNumber < NtoHl (hVHD->Dynamic.MaxTableEntries)) {
        Status = errno;
        }
    else {
        Status = 0;
        sim_messagef (SCPE_OK, "Merged %dMB.  100%% complete.\n", (int)((((float)NeededBlock)*SectorsPerBlock)*SectorSize/1000000));
        fclose (hVHD->File);
        hVHD->File = NULL;
        (void)remove (szVHDPath);
        *ParentVHD = (char*) malloc (strlen (hVHD->ParentVHDPath)+1);
        strcpy (*ParentVHD, hVHD->ParentVHDPath);
        }
Cleanup_Return:
    free (BlockData);
    if (hVHD->File)
        fclose (hVHD->File);
    if (Status) {
        free (hVHD->BAT);
        free (hVHD);
        hVHD = NULL;
        sim_vhd_disk_close ((FILE *)Parent);
        }
    else {
        free (hVHD->BAT);
        free (hVHD);
        hVHD = Parent;
        }
    errno = Status;
    return (FILE *)hVHD;
    }

static int sim_vhd_disk_close (FILE *f)
{
VHDHANDLE hVHD = (VHDHANDLE)f;

if (NULL != hVHD) {
    if (hVHD->Parent)
        sim_vhd_disk_close ((FILE *)hVHD->Parent);
    free (hVHD->BAT);
    if (hVHD->File) {
        fflush (hVHD->File);
        fclose (hVHD->File);
        }
    free (hVHD);
    return 0;
    }
return -1;
}

static void sim_vhd_disk_flush (FILE *f)
{
VHDHANDLE hVHD = (VHDHANDLE)f;

if ((NULL != hVHD) && (hVHD->File))
    fflush (hVHD->File);
}

static t_offset sim_vhd_disk_size (FILE *f)
{
VHDHANDLE hVHD = (VHDHANDLE)f;

return (t_offset)(NtoHll (hVHD->Footer.CurrentSize));
}

#include <stdlib.h>
#include <time.h>
static void
_rand_uuid_gen (void *uuidaddr)
{
int i;
uint8 *b = (uint8 *)uuidaddr;
uint32 timenow = (uint32)time (NULL);

memcpy (uuidaddr, &timenow, sizeof (timenow));
srand ((unsigned)timenow);
for (i=4; i<16; i++) {
    b[i] = (uint8)rand();
    }
}

#if defined (_WIN32)
static void
uuid_gen (void *uuidaddr)
{
static
RPC_STATUS
(RPC_ENTRY *UuidCreate_c) (void *);

if (!UuidCreate_c) {
    HINSTANCE hDll;
    hDll = LoadLibraryA("rpcrt4.dll");
    UuidCreate_c = (RPC_STATUS (RPC_ENTRY *) (void *))GetProcAddress(hDll, "UuidCreate");
    }
if (UuidCreate_c)
    UuidCreate_c(uuidaddr);
else
    _rand_uuid_gen (uuidaddr);
}
#elif defined (HAVE_DLOPEN)
#include <dlfcn.h>

static void
uuid_gen (void *uuidaddr)
{
void (*uuid_generate_c) (void *) = NULL;
void *handle;

#define S__STR_QUOTE(tok) #tok
#define S__STR(tok) S__STR_QUOTE(tok)
    handle = dlopen("libuuid." S__STR(HAVE_DLOPEN), RTLD_NOW|RTLD_GLOBAL);
    if (handle)
        uuid_generate_c = (void (*)(void *))((size_t)dlsym(handle, "uuid_generate"));
if (uuid_generate_c)
    uuid_generate_c(uuidaddr);
else
    _rand_uuid_gen (uuidaddr);
    if (handle)
        dlclose(handle);
}
#else
static void
uuid_gen (void *uuidaddr)
{
_rand_uuid_gen (uuidaddr);
}
#endif

static VHDHANDLE
CreateVirtualDisk(const char *szVHDPath,
                  uint32 SizeInSectors,
                  uint32 BlockSize,
                  t_bool bFixedVHD)
{
VHD_Footer Footer;
VHD_DynamicDiskHeader Dynamic;
uint32 *BAT = NULL;
time_t now;
uint32 i;
FILE *File = NULL;
uint32 Status = 0;
uint32 BytesPerSector = 512;
uint64 SizeInBytes = ((uint64)SizeInSectors)*BytesPerSector;
uint64 TableOffset;
uint32 MaxTableEntries;
VHDHANDLE hVHD = NULL;

if (SizeInBytes > ((uint64)(1024*1024*1024))*2040) {
    Status = EFBIG;
    goto Cleanup_Return;
    }
File = sim_fopen (szVHDPath, "rb");
if (File) {
    fclose (File);
    File = NULL;
    Status = EEXIST;
    goto Cleanup_Return;
    }
File = sim_fopen (szVHDPath, "wb");
if (!File) {
    Status = errno;
    goto Cleanup_Return;
    }

memset (&Footer, 0, sizeof(Footer));
memcpy (Footer.Cookie, "conectix", 8);
Footer.Features = NtoHl (0x00000002);;
Footer.FileFormatVersion = NtoHl (0x00010000);;
Footer.DataOffset = NtoHll (bFixedVHD ? ((long long)-1) : (long long)(sizeof(Footer)));
time (&now);
Footer.TimeStamp = NtoHl ((uint32)(now-946684800));
memcpy (Footer.CreatorApplication, "simh", 4);
Footer.CreatorVersion = NtoHl (0x00040000);
memcpy (Footer.CreatorHostOS, "Wi2k", 4);
Footer.OriginalSize = NtoHll (SizeInBytes);
Footer.CurrentSize = NtoHll (SizeInBytes);
uuid_gen (Footer.UniqueID);
Footer.DiskType = NtoHl (bFixedVHD ? VHD_DT_Fixed : VHD_DT_Dynamic);
Footer.DiskGeometry = NtoHl (0xFFFF10FF);
if (1) { /* CHS Calculation */
    uint32 totalSectors = (uint32)(SizeInBytes/BytesPerSector);/* Total data sectors present in the disk image */
    uint32 cylinders;                                          /* Number of cylinders present on the disk */
    uint32 heads;                                              /* Number of heads present on the disk */
    uint32 sectorsPerTrack;                                    /* Sectors per track on the disk */
    uint32 cylinderTimesHeads;                                 /* Cylinders x heads */

    if (totalSectors > 65535 * 16 * 255)
        totalSectors = 65535 * 16 * 255;

    if (totalSectors >= 65535 * 16 * 63) {
        sectorsPerTrack = 255;
        heads = 16;
        cylinderTimesHeads = totalSectors / sectorsPerTrack;
        }
    else {
        sectorsPerTrack = 17;
        cylinderTimesHeads = totalSectors / sectorsPerTrack;

        heads = (cylinderTimesHeads + 1023) / 1024;

        if (heads < 4)
            heads = 4;
        if (cylinderTimesHeads >= (heads * 1024) || heads > 16)
            {
            sectorsPerTrack = 31;
            heads = 16;
            cylinderTimesHeads = totalSectors / sectorsPerTrack;
            }
        if (cylinderTimesHeads >= (heads * 1024))
            {
            sectorsPerTrack = 63;
            heads = 16;
            cylinderTimesHeads = totalSectors / sectorsPerTrack;
            }
        }
    cylinders = cylinderTimesHeads / heads;
    Footer.DiskGeometry = NtoHl ((cylinders<<16)|(heads<<8)|sectorsPerTrack);
    }
Footer.Checksum = NtoHl (CalculateVhdFooterChecksum(&Footer, sizeof(Footer)));

if (bFixedVHD) {
    if (WriteFilePosition(File,
                          &Footer,
                          sizeof(Footer),
                          NULL,
                          SizeInBytes))
        Status = errno;
    goto Cleanup_Return;
    }

/* Dynamic Disk */
memset (&Dynamic, 0, sizeof(Dynamic));
memcpy (Dynamic.Cookie, "cxsparse", 8);
Dynamic.DataOffset = NtoHll ((uint64)0xFFFFFFFFFFFFFFFFLL);
TableOffset = NtoHll(Footer.DataOffset)+sizeof(Dynamic);
Dynamic.TableOffset = NtoHll (TableOffset);
Dynamic.HeaderVersion = NtoHl (0x00010000);
if (0 == BlockSize)
    BlockSize = 2*1024*1024;
Dynamic.BlockSize = NtoHl (BlockSize);
MaxTableEntries = (uint32)((SizeInBytes+BlockSize-1)/BlockSize);
Dynamic.MaxTableEntries = NtoHl (MaxTableEntries);
Dynamic.Checksum = NtoHl (CalculateVhdFooterChecksum(&Dynamic, sizeof(Dynamic)));
BAT = (uint32*) malloc (BytesPerSector*((MaxTableEntries*sizeof(*BAT)+BytesPerSector-1)/BytesPerSector));
memset (BAT, 0, BytesPerSector*((MaxTableEntries*sizeof(*BAT)+BytesPerSector-1)/BytesPerSector));
for (i=0; i<MaxTableEntries; ++i)
    BAT[i] = VHD_BAT_FREE_ENTRY;

if (WriteFilePosition(File,
                      &Footer,
                      sizeof(Footer),
                      NULL,
                      0)) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition(File,
                      &Dynamic,
                      sizeof(Dynamic),
                      NULL,
                      NtoHll(Footer.DataOffset))) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition(File,
                      BAT,
                      BytesPerSector*((MaxTableEntries*sizeof(*BAT)+BytesPerSector-1)/BytesPerSector),
                      NULL,
                      NtoHll(Dynamic.TableOffset))) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition(File,
                      &Footer,
                      sizeof(Footer),
                      NULL,
                      NtoHll(Dynamic.TableOffset)+BytesPerSector*((MaxTableEntries*sizeof(*BAT)+BytesPerSector-1)/BytesPerSector))) {
    Status = errno;
    goto Cleanup_Return;
    }

Cleanup_Return:
free (BAT);
if (File)
    fclose (File);
if (Status) {
    if (Status != EEXIST)
        (void)remove (szVHDPath);
    }
else {
    hVHD = (VHDHANDLE)sim_vhd_disk_open (szVHDPath, "rb+");
    if (!hVHD)
        Status = errno;
    }
errno = Status;
return hVHD;
}

#if defined(__CYGWIN__) || defined(VMS) || defined(__APPLE__) || defined(__linux) || defined(__linux__) || defined(__unix__)
#include <unistd.h>
#endif
static void
ExpandToFullPath (const char *szFileSpec,
                  char *szFullFileSpecBuffer,
                  size_t BufferSize)
{
char *c;
#ifdef _WIN32
for (c = strchr (szFullFileSpecBuffer, '/'); c; c = strchr (szFullFileSpecBuffer, '/'))
    *c = '\\';
GetFullPathNameA (szFileSpec, (DWORD)BufferSize, szFullFileSpecBuffer, NULL);
for (c = strchr (szFullFileSpecBuffer, '\\'); c; c = strchr (szFullFileSpecBuffer, '\\'))
    *c = '/';
#else
char buffer[PATH_MAX];
char *wd = getcwd(buffer, PATH_MAX);

if ((szFileSpec[0] != '/') || (strchr (szFileSpec, ':')))
    snprintf (szFullFileSpecBuffer, BufferSize, "%s/%s", wd, szFileSpec);
else
    strncpy (szFullFileSpecBuffer, szFileSpec, BufferSize);
if ((c = strstr (szFullFileSpecBuffer, "]/")))
    memmove (c+1, c+2, strlen(c+2)+1);
memset (szFullFileSpecBuffer + strlen (szFullFileSpecBuffer), 0, BufferSize - strlen (szFullFileSpecBuffer));
#endif
}

static char *
HostPathToVhdPath (const char *szHostPath,
                   char *szVhdPath,
                   size_t VhdPathSize)
{
char *c, *d;

strncpy (szVhdPath, szHostPath, VhdPathSize-1);
if ((szVhdPath[1] == ':') && islower(szVhdPath[0]))
    szVhdPath[0] = toupper(szVhdPath[0]);
szVhdPath[VhdPathSize-1] = '\0';
if ((c = strrchr (szVhdPath, ']'))) {
    *c = '\0';
    if (!(d = strchr (szVhdPath, '[')))
        return d;
    *d = '/';
    while ((d = strchr (d, '.')))
        *d = '/';
    *c = '/';
    }
while ((c = strchr (szVhdPath, '/')))
    *c = '\\';
for (c = strstr (szVhdPath, "\\.\\"); c; c = strstr (szVhdPath, "\\.\\"))
    memmove (c, c+2, strlen(c+2)+1);
for (c = strstr (szVhdPath, "\\\\"); c; c = strstr (szVhdPath, "\\\\"))
    memmove (c, c+1, strlen(c+1)+1);
while ((c = strstr (szVhdPath, "\\..\\"))) {
    *c = '\0';
    d = strrchr (szVhdPath, '\\');
    if (d)
        memmove (d, c+3, strlen(c+3)+1);
    else
        return d;
    }
memset (szVhdPath + strlen (szVhdPath), 0, VhdPathSize - strlen (szVhdPath));
return szVhdPath;
}

static char *
VhdPathToHostPath (const char *szVhdPath,
                   char *szHostPath,
                   size_t HostPathSize)
{
char *c;
char *d = szHostPath;

strncpy (szHostPath, szVhdPath, HostPathSize-1);
szHostPath[HostPathSize-1] = '\0';
#if defined(VMS)
c = strchr (szVhdPath, ':');
if (*(c+1) != '\\')
    return NULL;
*(c+1) = '[';
d = strrchr (c+2, '\\');
if (d) {
    *d = ']';
    while ((d = strrchr (c+2, '\\')))
        *d = '.';
    }
else
    return NULL;
#else
while ((c = strchr (d, '\\')))
    *c = '/';
#endif
memset (szHostPath + strlen (szHostPath), 0, HostPathSize - strlen (szHostPath));
return szHostPath;
}

static VHDHANDLE
CreateDifferencingVirtualDisk(const char *szVHDPath,
                              const char *szParentVHDPath)
{
uint32 BytesPerSector = 512;
VHDHANDLE hVHD = NULL;
VHD_Footer ParentFooter;
VHD_DynamicDiskHeader ParentDynamic;
uint32 ParentTimeStamp;
uint32 Status = 0;
char *RelativeParentVHDPath = NULL;
char *FullParentVHDPath = NULL;
char *RelativeParentVHDPathUnicode = NULL;
char *FullParentVHDPathUnicode = NULL;
char *FullVHDPath = NULL;
char *TempPath = NULL;
size_t i, RelativeMatch, UpDirectories, LocatorsWritten = 0;
int64 LocatorPosition;

if ((Status = GetVHDFooter (szParentVHDPath,
                            &ParentFooter,
                            &ParentDynamic,
                            NULL,
                            &ParentTimeStamp,
                            NULL,
                            0)))
    goto Cleanup_Return;
hVHD = CreateVirtualDisk (szVHDPath,
                          (uint32)(NtoHll(ParentFooter.CurrentSize)/BytesPerSector),
                          NtoHl(ParentDynamic.BlockSize),
                          FALSE);
if (!hVHD) {
    Status = errno;
    goto Cleanup_Return;
    }
LocatorPosition = ((sizeof (hVHD->Footer) + BytesPerSector - 1)/BytesPerSector + (sizeof (hVHD->Dynamic) + BytesPerSector - 1)/BytesPerSector)*BytesPerSector;
hVHD->Dynamic.Checksum = 0;
RelativeParentVHDPath = (char*) calloc (1, BytesPerSector+2);
FullParentVHDPath = (char*) calloc (1, BytesPerSector+2);
RelativeParentVHDPathUnicode = (char*) calloc (1, BytesPerSector+2);
FullParentVHDPathUnicode = (char*) calloc (1, BytesPerSector+2);
FullVHDPath = (char*) calloc (1, BytesPerSector+2);
TempPath = (char*) calloc (1, BytesPerSector+2);
ExpandToFullPath (szParentVHDPath, TempPath, BytesPerSector);
HostPathToVhdPath (TempPath, FullParentVHDPath, BytesPerSector);
for (i=0; i < strlen (FullParentVHDPath); i++)
    hVHD->Dynamic.ParentUnicodeName[i*2+1] = FullParentVHDPath[i];  /* Big Endian Unicode */
for (i=0; i < strlen (FullParentVHDPath); i++)
    FullParentVHDPathUnicode[i*2] = FullParentVHDPath[i];           /* Little Endian Unicode */
ExpandToFullPath (szVHDPath, TempPath, BytesPerSector);
HostPathToVhdPath (TempPath, FullVHDPath, BytesPerSector);
for (i=0, RelativeMatch=UpDirectories=0; i<strlen(FullVHDPath); i++)
    if (FullVHDPath[i] == '\\') {
        if (memcmp (FullVHDPath, FullParentVHDPath, i+1))
            ++UpDirectories;
        else
            RelativeMatch = i;
        }
if (RelativeMatch) {
    char UpDir[4] = "..\\";

    UpDir[2] = FullParentVHDPath[RelativeMatch];
    if (UpDirectories)
        for (i=0; i<UpDirectories; i++)
            strcpy (RelativeParentVHDPath+strlen (RelativeParentVHDPath), UpDir);
    else
        strcpy (RelativeParentVHDPath+strlen (RelativeParentVHDPath), UpDir+1);
    strcpy (RelativeParentVHDPath+strlen (RelativeParentVHDPath), &FullParentVHDPath[RelativeMatch+1]);
    }
for (i=0; i < strlen(RelativeParentVHDPath); i++)
    RelativeParentVHDPathUnicode[i*2] = RelativeParentVHDPath[i];
hVHD->Dynamic.ParentTimeStamp = ParentTimeStamp;
memcpy (hVHD->Dynamic.ParentUniqueID, ParentFooter.UniqueID, sizeof (hVHD->Dynamic.ParentUniqueID));
/* There are two potential parent locators on current vhds */
memcpy (hVHD->Dynamic.ParentLocatorEntries[0].PlatformCode, "W2ku", 4);
hVHD->Dynamic.ParentLocatorEntries[0].PlatformDataSpace = NtoHl (BytesPerSector);
hVHD->Dynamic.ParentLocatorEntries[0].PlatformDataLength = NtoHl ((uint32)(2*strlen(FullParentVHDPath)));
hVHD->Dynamic.ParentLocatorEntries[0].Reserved = 0;
hVHD->Dynamic.ParentLocatorEntries[0].PlatformDataOffset = NtoHll (LocatorPosition+LocatorsWritten*BytesPerSector);
++LocatorsWritten;
if (RelativeMatch) {
    memcpy (hVHD->Dynamic.ParentLocatorEntries[1].PlatformCode, "W2ru", 4);
    hVHD->Dynamic.ParentLocatorEntries[1].PlatformDataSpace = NtoHl (BytesPerSector);
    hVHD->Dynamic.ParentLocatorEntries[1].PlatformDataLength = NtoHl ((uint32)(2*strlen(RelativeParentVHDPath)));
    hVHD->Dynamic.ParentLocatorEntries[1].Reserved = 0;
    hVHD->Dynamic.ParentLocatorEntries[1].PlatformDataOffset = NtoHll (LocatorPosition+LocatorsWritten*BytesPerSector);
    ++LocatorsWritten;
    }
hVHD->Dynamic.TableOffset = NtoHll (((LocatorPosition+LocatorsWritten*BytesPerSector + VHD_DATA_BLOCK_ALIGNMENT - 1)/VHD_DATA_BLOCK_ALIGNMENT)*VHD_DATA_BLOCK_ALIGNMENT);
hVHD->Dynamic.Checksum = 0;
hVHD->Dynamic.Checksum = NtoHl (CalculateVhdFooterChecksum (&hVHD->Dynamic, sizeof(hVHD->Dynamic)));
hVHD->Footer.Checksum = 0;
hVHD->Footer.DiskType = NtoHl (VHD_DT_Differencing);
memcpy (hVHD->Footer.DriveType, ParentFooter.DriveType, sizeof (hVHD->Footer.DriveType));
hVHD->Footer.Checksum = NtoHl (CalculateVhdFooterChecksum (&hVHD->Footer, sizeof(hVHD->Footer)));

if (WriteFilePosition (hVHD->File,
                       &hVHD->Footer,
                       sizeof (hVHD->Footer),
                       NULL,
                       0)) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition (hVHD->File,
                       &hVHD->Dynamic,
                       sizeof (hVHD->Dynamic),
                       NULL,
                       NtoHll (hVHD->Footer.DataOffset))) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition (hVHD->File,
                       hVHD->BAT,
                       BytesPerSector*((NtoHl (hVHD->Dynamic.MaxTableEntries)*sizeof(*hVHD->BAT)+BytesPerSector-1)/BytesPerSector),
                       NULL,
                       NtoHll (hVHD->Dynamic.TableOffset))) {
    Status = errno;
    goto Cleanup_Return;
    }
if (WriteFilePosition (hVHD->File,
                       &hVHD->Footer,
                       sizeof (hVHD->Footer),
                       NULL,
                       NtoHll (hVHD->Dynamic.TableOffset)+BytesPerSector*((NtoHl (hVHD->Dynamic.MaxTableEntries)*sizeof(*hVHD->BAT)+BytesPerSector-1)/BytesPerSector))) {
    Status = errno;
    goto Cleanup_Return;
    }
if (hVHD->Dynamic.ParentLocatorEntries[0].PlatformDataLength)
    if (WriteFilePosition (hVHD->File,
                           FullParentVHDPathUnicode,
                           BytesPerSector,
                           NULL,
                           NtoHll (hVHD->Dynamic.ParentLocatorEntries[0].PlatformDataOffset))) {
        Status = errno;
        goto Cleanup_Return;
        }
if (hVHD->Dynamic.ParentLocatorEntries[1].PlatformDataLength)
    if (WriteFilePosition (hVHD->File,
                           RelativeParentVHDPathUnicode,
                           BytesPerSector,
                           NULL,
                           NtoHll (hVHD->Dynamic.ParentLocatorEntries[1].PlatformDataOffset))) {
        Status = errno;
        goto Cleanup_Return;
        }

Cleanup_Return:
free (RelativeParentVHDPath);
free (FullParentVHDPath);
free (RelativeParentVHDPathUnicode);
free (FullParentVHDPathUnicode);
free (FullVHDPath);
free (TempPath);
sim_vhd_disk_close ((FILE *)hVHD);
hVHD = NULL;
if (Status) {
    if ((EEXIST != Status) && (ENOENT != Status))
        (void)remove (szVHDPath);
    }
else {
    hVHD = (VHDHANDLE)sim_vhd_disk_open (szVHDPath, "rb+");
    if (!hVHD)
        Status = errno;
    }
errno = Status;
return hVHD;
}

static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize)
{
return (FILE *)CreateVirtualDisk (szVHDPath, (uint32)(desiredsize/512), 0, (sim_switches & SWMASK ('X')));
}

static FILE *sim_vhd_disk_create_diff (const char *szVHDPath, const char *szParentVHDPath)
{
return (FILE *)CreateDifferencingVirtualDisk (szVHDPath, szParentVHDPath);
}

static t_stat
ReadVirtualDiskSectors(VHDHANDLE hVHD,
                       uint8 *buf,
                       t_seccnt sects,
                       t_seccnt *sectsread,
                       uint32 SectorSize,
                       t_lba lba)
{
uint64 BlockOffset = ((uint64)lba)*SectorSize;
uint32 BlocksRead = 0;
uint32 SectorsInRead;
size_t BytesRead = 0;

if (!hVHD || (hVHD->File == NULL)) {
    errno = EBADF;
    return SCPE_IOERR;
    }
if ((BlockOffset + sects*SectorSize) > (uint64)NtoHll (hVHD->Footer.CurrentSize)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
if (NtoHl (hVHD->Footer.DiskType) == VHD_DT_Fixed) {
    if (ReadFilePosition(hVHD->File,
                         buf,
                         sects*SectorSize,
                         &BytesRead,
                         BlockOffset)) {
        if (sectsread)
            *sectsread = (t_seccnt)(BytesRead/SectorSize);
        return SCPE_IOERR;
        }
    if (sectsread)
        *sectsread /= SectorSize;
    return SCPE_OK;
    }
/* We are now dealing with a Dynamically expanding or differencing disk */
while (sects) {
    uint32 SectorsPerBlock = NtoHl (hVHD->Dynamic.BlockSize)/SectorSize;
    uint64 BlockNumber = lba/SectorsPerBlock;
    uint32 BitMapBytes = (7+(NtoHl (hVHD->Dynamic.BlockSize)/SectorSize))/8;
    uint32 BitMapSectors = (BitMapBytes+SectorSize-1)/SectorSize;

    SectorsInRead = SectorsPerBlock - lba%SectorsPerBlock;
    if (SectorsInRead > sects)
        SectorsInRead = sects;
    if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY) {
        if (!hVHD->Parent)
            memset (buf, 0, SectorSize*SectorsInRead);
        else {
            if (ReadVirtualDiskSectors(hVHD->Parent,
                                       buf,
                                       SectorsInRead,
                                       NULL,
                                       SectorSize,
                                       lba)) {
                if (sectsread)
                    *sectsread = BlocksRead;
                return FALSE;
                }
            }
        }
    else {
        BlockOffset = SectorSize*((uint64)(NtoHl (hVHD->BAT[BlockNumber]) + lba%SectorsPerBlock + BitMapSectors));
        if (ReadFilePosition(hVHD->File,
                             buf,
                             SectorsInRead*SectorSize,
                             NULL,
                             BlockOffset)) {
            if (sectsread)
                *sectsread = BlocksRead;
            return SCPE_IOERR;
            }
        }
    sects -= SectorsInRead;
    buf = (uint8 *)(((char *)buf) + SectorSize*SectorsInRead);
    lba += SectorsInRead;
    BlocksRead += SectorsInRead;
    }
if (sectsread)
    *sectsread = BlocksRead;
return SCPE_OK;
}

static t_stat sim_vhd_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
VHDHANDLE hVHD = (VHDHANDLE)uptr->fileref;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

return ReadVirtualDiskSectors(hVHD, buf, sects, sectsread, ctx->sector_size, lba);
}

static t_stat sim_vhd_disk_clearerr (UNIT *uptr)
{
VHDHANDLE hVHD = (VHDHANDLE)uptr->fileref;

clearerr (hVHD->File);
return SCPE_OK;
}

static t_bool
BufferIsZeros(void *Buffer, size_t BufferSize)
{
size_t i;
char *c = (char *)Buffer;

for (i=0; i<BufferSize; ++i)
    if (c[i])
        return FALSE;
return TRUE;
}

static t_stat
WriteVirtualDiskSectors(VHDHANDLE hVHD,
                        uint8 *buf,
                        t_seccnt sects,
                        t_seccnt *sectswritten,
                        uint32 SectorSize,
                        t_lba lba)
{
uint64 BlockOffset = ((uint64)lba)*SectorSize;
uint32 BlocksWritten = 0;
uint32 SectorsInWrite;
size_t BytesWritten = 0;

if (!hVHD || !hVHD->File) {
    errno = EBADF;
    return SCPE_IOERR;
    }
if ((BlockOffset + sects*SectorSize) > (uint64)NtoHll(hVHD->Footer.CurrentSize)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
if (NtoHl(hVHD->Footer.DiskType) == VHD_DT_Fixed) {
    if (WriteFilePosition(hVHD->File,
                          buf,
                          sects*SectorSize,
                          &BytesWritten,
                          BlockOffset)) {
        if (sectswritten)
            *sectswritten = (t_seccnt)(BytesWritten/SectorSize);
        return SCPE_IOERR;
        }
    if (sectswritten)
        *sectswritten /= SectorSize;
    return SCPE_OK;
    }
/* We are now dealing with a Dynamically expanding or differencing disk */
while (sects) {
    uint32 SectorsPerBlock = NtoHl(hVHD->Dynamic.BlockSize)/SectorSize;
    uint64 BlockNumber = lba/SectorsPerBlock;
    uint32 BitMapBytes = (7+(NtoHl(hVHD->Dynamic.BlockSize)/SectorSize))/8;
    uint32 BitMapSectors = (BitMapBytes+SectorSize-1)/SectorSize;

    if (BlockNumber >= NtoHl(hVHD->Dynamic.MaxTableEntries)) {
        if (sectswritten)
            *sectswritten = BlocksWritten;
        return SCPE_EOF;
        }
    SectorsInWrite = 1;
    if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY) {
        uint8 *BitMap = NULL;
        uint32 BitMapBufferSize = VHD_DATA_BLOCK_ALIGNMENT;
        uint8 *BitMapBuffer = NULL;
        void *BlockData = NULL;
        uint8 *BATUpdateBufferAddress;
        uint32 BATUpdateBufferSize;
        uint64 BATUpdateStorageAddress;

        if (!hVHD->Parent && BufferIsZeros(buf, SectorSize))
            goto IO_Done;
        /* Need to allocate a new Data Block. */
        BlockOffset = sim_fsize_ex (hVHD->File);
        if (((int64)BlockOffset) == -1)
            return SCPE_IOERR;
        if (BitMapSectors*SectorSize > BitMapBufferSize)
            BitMapBufferSize = BitMapSectors*SectorSize;
        BitMapBuffer = (uint8 *)calloc(1, BitMapBufferSize + SectorSize*SectorsPerBlock);
        if (BitMapBufferSize > BitMapSectors*SectorSize)
            BitMap = BitMapBuffer + BitMapBufferSize-BitMapBytes;
        else
            BitMap = BitMapBuffer;
        memset(BitMap, 0xFF, BitMapBytes);
        BlockOffset -= sizeof(hVHD->Footer);
        if (0 == (BlockOffset & ~(VHD_DATA_BLOCK_ALIGNMENT-1)))
            {  // Already aligned, so use padded BitMapBuffer
            if (WriteFilePosition(hVHD->File,
                                  BitMapBuffer,
                                  BitMapBufferSize + SectorSize*SectorsPerBlock,
                                  NULL,
                                  BlockOffset)) {
                free (BitMapBuffer);
                return SCPE_IOERR;
                }
            BlockOffset += BitMapBufferSize;
            }
        else
            {
            // align the data portion of the block to the desired alignment
            // compute the address of the data portion of the block
            BlockOffset += BitMapSectors*SectorSize;
            // round up this address to the desired alignment
            BlockOffset += VHD_DATA_BLOCK_ALIGNMENT-1;
            BlockOffset &= ~(VHD_DATA_BLOCK_ALIGNMENT-1);
            BlockOffset -= BitMapSectors*SectorSize;
            if (WriteFilePosition(hVHD->File,
                                  BitMap,
                                  SectorSize * (BitMapSectors + SectorsPerBlock),
                                  NULL,
                                  BlockOffset)) {
                free (BitMapBuffer);
                return SCPE_IOERR;
                }
            BlockOffset += BitMapSectors*SectorSize;
            }
        free(BitMapBuffer);
        BitMapBuffer = BitMap = NULL;
        /* the BAT block address is the beginning of the block bitmap */
        BlockOffset -= BitMapSectors*SectorSize;
        hVHD->BAT[BlockNumber] = NtoHl((uint32)(BlockOffset/SectorSize));
        BlockOffset += SectorSize * (SectorsPerBlock + BitMapSectors);
        if (WriteFilePosition(hVHD->File,
                              &hVHD->Footer,
                              sizeof(hVHD->Footer),
                              NULL,
                              BlockOffset))
            goto Fatal_IO_Error;
        /* Since a large VHD can have a pretty large BAT, and we've only changed one longword bat entry
           in the current BAT, we write just the aligned sector which contains the updated BAT entry */
        BATUpdateBufferAddress = (uint8 *)hVHD->BAT - (size_t)NtoHll(hVHD->Dynamic.TableOffset) + 
            (size_t)((((size_t)&hVHD->BAT[BlockNumber]) - (size_t)hVHD->BAT + (size_t)NtoHll(hVHD->Dynamic.TableOffset)) & ~(VHD_DATA_BLOCK_ALIGNMENT-1));
        /* If the starting of the BAT isn't on a VHD_DATA_BLOCK_ALIGNMENT boundary and we've just updated 
           a BAT entry early in the array, the buffer computed address might be before the start of the
           BAT table.  If so, only write the BAT data needed */
        if (BATUpdateBufferAddress < (uint8 *)hVHD->BAT) {
            BATUpdateBufferAddress = (uint8 *)hVHD->BAT;
            BATUpdateBufferSize = (uint32)((((size_t)&hVHD->BAT[BlockNumber]) - (size_t)hVHD->BAT) + 512) & ~511;
            BATUpdateStorageAddress = NtoHll(hVHD->Dynamic.TableOffset);
            }
        else {
            BATUpdateBufferSize = VHD_DATA_BLOCK_ALIGNMENT;
            BATUpdateStorageAddress = NtoHll(hVHD->Dynamic.TableOffset) + BATUpdateBufferAddress - ((uint8 *)hVHD->BAT);
            }
        /* If the total BAT is smaller than one VHD_DATA_BLOCK_ALIGNMENT, then be sure to only write out the BAT data */
        if ((size_t)(BATUpdateBufferAddress - (uint8 *)hVHD->BAT + BATUpdateBufferSize) > 512*((sizeof(*hVHD->BAT)*NtoHl(hVHD->Dynamic.MaxTableEntries) + 511)/512))
            BATUpdateBufferSize = (uint32)(512*((sizeof(*hVHD->BAT)*NtoHl(hVHD->Dynamic.MaxTableEntries) + 511)/512) - (BATUpdateBufferAddress - ((uint8 *)hVHD->BAT)));
        if (WriteFilePosition(hVHD->File,
                              BATUpdateBufferAddress,
                              BATUpdateBufferSize,
                              NULL,
                              BATUpdateStorageAddress))
            goto Fatal_IO_Error;
        if (hVHD->Parent)
            { /* Need to populate data block contents from parent VHD */
            uint32 BlockSectors = SectorsPerBlock;

            BlockData = malloc(SectorsPerBlock*SectorSize);

            if (((lba/SectorsPerBlock)*SectorsPerBlock + BlockSectors) > ((uint64)NtoHll (hVHD->Footer.CurrentSize))/SectorSize)
                BlockSectors = (uint32)(((uint64)NtoHll (hVHD->Footer.CurrentSize))/SectorSize - (lba/SectorsPerBlock)*SectorsPerBlock);
            if (ReadVirtualDiskSectors(hVHD->Parent,
                                       (uint8*) BlockData,
                                       BlockSectors,
                                       NULL,
                                       SectorSize,
                                       (lba/SectorsPerBlock)*SectorsPerBlock))
                goto Fatal_IO_Error;
            if (WriteVirtualDiskSectors(hVHD,
                                        (uint8*) BlockData,
                                        BlockSectors,
                                        NULL,
                                        SectorSize,
                                        (lba/SectorsPerBlock)*SectorsPerBlock))
                goto Fatal_IO_Error;
            free(BlockData);
            }
        continue;
Fatal_IO_Error:
        free (BitMap);
        free (BlockData);
        fclose (hVHD->File);
        hVHD->File = NULL;
        return SCPE_IOERR;
        }
    else {
        BlockOffset = 512*((uint64)(NtoHl(hVHD->BAT[BlockNumber]) + lba%SectorsPerBlock + BitMapSectors));
        SectorsInWrite = SectorsPerBlock - lba%SectorsPerBlock;
        if (SectorsInWrite > sects)
            SectorsInWrite = sects;
        if (WriteFilePosition(hVHD->File,
                              buf,
                              SectorsInWrite*SectorSize,
                              NULL,
                              BlockOffset)) {
            if (sectswritten)
                *sectswritten = BlocksWritten;
            return SCPE_IOERR;
            }
        }
IO_Done:
    sects -= SectorsInWrite;
    buf = (uint8 *)(((char *)buf) + SectorsInWrite*SectorSize);
    lba += SectorsInWrite;
    BlocksWritten += SectorsInWrite;
    }
if (sectswritten)
    *sectswritten = BlocksWritten;
return SCPE_OK;
}

static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
VHDHANDLE hVHD = (VHDHANDLE)uptr->fileref;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

return WriteVirtualDiskSectors(hVHD, buf, sects, sectswritten, ctx->sector_size, lba);
}
#endif
