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
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.



   This is the place which hides processing of various disk formats,
   as well as OS-specific direct hardware access.

   25-Jan-11    MP      Initial Implementation

Public routines:

   sim_disk_attach           attach disk unit
   sim_disk_attach_ex        attach disk unit extended parameters
   sim_disk_attach_ex2       attach disk unit additional extended parameters
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
   sim_disk_set_autosize     MTAB set autosize
   sim_disk_show_autosize    MTAB display autosize
   sim_disk_set_autozap      MTAB set autozap
   sim_disk_show_autozap     MTAB display autozap
   sim_disk_set_async        enable asynchronous operation
   sim_disk_clr_async        disable asynchronous operation
   sim_disk_data_trace       debug support
   sim_disk_set_drive_type   MTAB validator routine
   sim_disk_set_drive_type_by_name device reset initialization
   sim_disk_show_drive_type  MTAB display routine
   sim_disk_get_drive_type_set_string set command arguments for the specified unit
   sim_disk_test             unit test routine

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
   sim_vhd_CHS               platform independent virtual disk size CHS value
   sim_vhd_disk_parent_path  platform independent differencing virtual disk parent path
   sim_vhd_disk_rdsect       platform independent read virtual disk sectors
   sim_vhd_disk_wrsect       platform independent write virtual disk sectors

   sim_disk_find_type        locate DRVTYP of named disk type

*/

#define _FILE_OFFSET_BITS 64    /* 64 bit file offset for raw I/O operations  */

#include "sim_defs.h"
#include "sim_disk.h"
#include "sim_ether.h"
#include "sim_scsi.h"

#include "sim_scp_private.h"

#define DKUF_F_AUTO      0                              /* Auto detect format format */
#define DKUF_F_STD       1                              /* SIMH format */
#define DKUF_F_RAW       2                              /* Raw Physical Disk Access */
#define DKUF_F_VHD       3                              /* VHD format */

#define DKUF_E_AUTO      0                              /* Auto detect encoding */
#define DKUF_E_DLD9      1                              /* KLH10 packed 36bit little endian word */
#define DKUF_E_DBD9      2                              /* KLH10 packed 36bit big endian word */

#define DK_GET_FMT(u)   (((u)->flags >> DKUF_V_FMT) & DKUF_M_FMT)
#define DK_GET_ENC(u)   (((u)->flags >> DKUF_V_ENC) & DKUF_M_ENC)

#if defined SIM_ASYNCH_IO
#include <pthread.h>
#endif

static t_bool sim_disk_check_attached_container (const char *filename, UNIT **auptr);


/* Newly created SIMH (and possibly RAW) disk containers       */
/* will have this data as the last 512 bytes of the container  */
/* It will not be considered part of the data in the container */
/* Previously existing containers will have this appended to   */
/* the end of the container if they are opened for write       */
struct simh_disk_footer {
    uint8       Signature[4];           /* must be 'simh' */
    uint8       CreatingSimulator[64];  /* name of simulator */
    uint8       DriveType[16];
    uint32      SectorSize;
    uint32      SectorCount;
    uint32      ElementEncodingSize;
    uint8       CreationTime[28];       /* Result of ctime() */
    uint8       FooterVersion;          /* Initially 0 */
#define FOOTER_VERSION  1
    uint8       AccessFormat;           /* 1 - SIMH, 2 - RAW */
    uint8       Reserved[342];          /* Currently unused */
    uint32      Geometry;               /* CHS (Cylinders, Heads and Sectors) */
    uint32      DataWidth;              /* Data Width in the Transfer Size */
    uint32      MediaID;                /* Media ID */
    uint8       DeviceName[16];         /* Name of the Device when created */
    uint32      Highwater[2];           /* Size before footer addition or furthest container point written */
    uint32      Unused;                 /* Currently unused */
    uint32      Checksum;               /* CRC32 of the prior 508 bytes */
    };

/* OS Independent Disk Virtual Disk (VHD) I/O support */

#if (defined (VMS) && !(defined (__ALPHA) || defined (__ia64)))
#define DONT_DO_VHD_SUPPORT  /* VAX/VMS compilers don't have 64 bit integers */
#endif

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
#elif  __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static uint32
NtoHl(uint32 value)
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
#endif

struct disk_context {
    t_offset            container_size;     /* Size of the data portion (of the pseudo disk) */
    t_offset            highwater;          /* Furthest written sector in the disk */
    DEVICE              *dptr;              /* Device for unit (access to debug flags) */
    uint32              dbit;               /* debugging bit */
    uint32              sector_size;        /* Disk Sector Size (of the pseudo disk) */
    uint32              capac_factor;       /* Units of Capacity (8 = quadword, 2 = word, 1 = byte) */
    uint32              xfer_encode_size;   /* Disk Bus Transfer size (1 - byte, 2 - word, 4 - longword) */
    uint32              storage_sector_size;/* Sector size of the containing storage */

    uint32              removable;          /* Removable device flag */
    uint32              media_id;           /* MediaID of the container */
    uint32              is_cdrom;           /* Host system CDROM Device */
    uint32              media_removed;      /* Media not available flag */
    uint32              auto_format;        /* Format determined dynamically */
    uint32              read_count;         /* Number of read operations performed */
    uint32              write_count;        /* Number of write operations performed */
    uint32              data_ileave;        /* Data sectors interleaved in container */
    uint32              data_ileave_skew;   /* Data sectors track skew in container */
    DRVTYP              *initial_drvtyp;    /* Unit Drive Type before any autosize */
    t_addr              initial_capac;      /* Unit Capacity before any autosize */
    struct simh_disk_footer
                        *footer;
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
                op, (int)(uptr - ctx->dptr->units), _lba, _sects);\
                                                                \
        if (ctx->callback)      /* horrible mistake, stop */    \
            SIM_SCP_ABORT ("AIO_CALL error");                   \
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

sim_debug_unit (ctx->dbit, uptr, "_disk_io(unit=%d) starting\n", (int)(uptr - ctx->dptr->units));

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

sim_debug_unit (ctx->dbit, uptr, "_disk_io(unit=%d) exiting\n", (int)(uptr - ctx->dptr->units));

return NULL;
}

/* This routine is called in the context of the main simulator thread before
   processing events for any unit. It is only called when an asynchronous
   thread has called sim_activate() to activate a unit.  The job of this
   routine is to put the unit in proper condition to digest what may have
   occurred in the asynchronous thread.

   Since disk processing only handles a single I/O at a time to a
   particular disk device (due to using stdio for the SimH Disk format
   and stdio doesn't have an atomic seek+(read|write) operation),
   we have the opportunity to possibly detect improper attempts to
   issue multiple concurrent I/O requests. */
static void _disk_completion_dispatch (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
DISK_PCALLBACK callback = ctx->callback;

sim_debug_unit (ctx->dbit, uptr, "_disk_completion_dispatch(unit=%d, dop=%d, callback=%p)\n", (int)(uptr - ctx->dptr->units), ctx->io_dop, (void *)(ctx->callback));

if (ctx->io_dop != DOP_DONE)
    SIM_SCP_ABORT ("_disk_completion_dispatch()"); /* horribly wrong, stop */

if (ctx->callback && ctx->io_dop == DOP_DONE) {
    ctx->callback = NULL;
    callback (uptr, ctx->io_status);
    }
}

static t_bool _disk_is_active (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_disk_is_active(unit=%d, dop=%d)\n", (int)(uptr - ctx->dptr->units), ctx->io_dop);
    return (ctx->io_dop != DOP_DONE);
    }
return FALSE;
}

static t_bool _disk_cancel (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

if (ctx) {
    sim_debug_unit (ctx->dbit, uptr, "_disk_cancel(unit=%d, dop=%d)\n", (int)(uptr - ctx->dptr->units), ctx->io_dop);
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
static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize, DRVTYP *drvtyp);
static FILE *sim_vhd_disk_create_diff (const char *szVHDPath, const char *szParentVHDPath);
static FILE *sim_vhd_disk_merge (const char *szVHDPath, char **ParentVHD);
static int sim_vhd_disk_close (FILE *f);
static void sim_vhd_disk_flush (FILE *f);
static t_offset sim_vhd_disk_size (FILE *f);
static uint32 sim_vhd_CHS (FILE *f);
static const char *sim_vhd_disk_parent_path (FILE *f);
static t_stat sim_vhd_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects);
static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects);
static t_stat sim_vhd_disk_clearerr (UNIT *uptr);
static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype, uint32 SectorSize, uint32 xfer_encode_size, uint32 media_id, const char *device_name, uint32 data_width, DRVTYP *drvtyp);
static const char *sim_vhd_disk_get_dtype (FILE *f, uint32 *SectorSize, uint32 *xfer_encode_size, char sim_name[64], time_t *creation_time, uint32 *media_id, char device_name[16], uint32 *data_width);
static DRVTYP *sim_disk_find_type (UNIT *uptr, const char *dtype);
uint32 sim_disk_drvtype_geometry (DRVTYP *drvtyp, uint32 totalSectors);
static uint32 sim_SectorsToCHS (uint32 totalSectors);
static t_stat sim_os_disk_implemented_raw (void);
static FILE *sim_os_disk_open_raw (const char *rawdevicename, const char *openmode);
static int sim_os_disk_close_raw (FILE *f);
static void sim_os_disk_flush_raw (FILE *f);
static t_offset sim_os_disk_size_raw (FILE *f);
static t_stat sim_os_disk_unload_raw (FILE *f);
static t_bool sim_os_disk_isavailable_raw (FILE *f);
static t_stat sim_os_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects);
static t_stat sim_os_disk_read (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *bytesread, uint32 bytes);
static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects);
static t_stat sim_os_disk_write (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *byteswritten, uint32 bytes);
static t_stat sim_os_disk_info_raw (FILE *f, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom);
static char *HostPathToVhdPath (const char *szHostPath, char *szVhdPath, size_t VhdPathSize);
static char *VhdPathToHostPath (const char *szVhdPath, char *szHostPath, size_t HostPathSize);
static t_offset get_filesystem_size (UNIT *uptr, t_bool *isreadonly);

struct sim_disk_fmt {
    const char          *name;                          /* name */
    int32               uflags;                         /* unit flags */
    int32               fmtval;                         /* Format type value */
    uint32              encode;                         /* Data Encode Default - 0 means take from attach parameter */
    t_stat              (*impl_fnc)(void);              /* Implemented Test Function */
    };

static struct sim_disk_fmt fmts[] = {
    { "AUTO detect", 0, DKUF_F_AUTO,     0,                 NULL},
    { "SIMH",        0, DKUF_F_STD,      0,                 NULL},
    { "RAW",         0, DKUF_F_RAW,      0,                 sim_os_disk_implemented_raw},
    { "VHD",         0, DKUF_F_VHD,      0,                 sim_vhd_disk_implemented},
    { NULL,          0, 0,               0,                 NULL}
    };

/* Set disk format */

t_stat sim_disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 f;

if (uptr == NULL)
    return SCPE_IERR;
if ((cptr == NULL) || (*cptr == '\0'))
    return SCPE_ARG;
for (f = 0; fmts[f].name; f++) {
    if (fmts[f].name && (MATCH_CMD (cptr, fmts[f].name) == 0)) {
        if ((fmts[f].impl_fnc) && (fmts[f].impl_fnc() != SCPE_OK))
            return SCPE_NOFNC;
        uptr->flags = (uptr->flags & ~DKUF_FMT) |
            (fmts[f].fmtval << DKUF_V_FMT) | fmts[f].uflags;
        if (fmts[f].fmtval == DKUF_F_AUTO)
            uptr->flags = (uptr->flags & ~DKUF_ENC) | (DKUF_E_AUTO << DKUF_V_ENC);
        return SCPE_OK;
        }
    }
if (MATCH_CMD (cptr, "DLD9") == 0) {
    if (DK_GET_FMT (uptr) == DKUF_F_AUTO)
        uptr->flags = (uptr->flags & ~DKUF_FMT) |
            (DKUF_F_STD << DKUF_V_FMT) | fmts[f].uflags;
    uptr->flags = (uptr->flags & ~DKUF_ENC) | (DKUF_E_DLD9 << DKUF_V_ENC);
    return SCPE_OK;
    }
if (MATCH_CMD (cptr, "DBD9") == 0) {
    if (DK_GET_FMT (uptr) == DKUF_F_AUTO)
        uptr->flags = (uptr->flags & ~DKUF_FMT) |
            (DKUF_F_STD << DKUF_V_FMT) | fmts[f].uflags;
    uptr->flags = (uptr->flags & ~DKUF_ENC) | (DKUF_E_DBD9 << DKUF_V_ENC);
    return SCPE_OK;
    }
return sim_messagef (SCPE_ARG, "Unknown disk format: %s\n", cptr);
}

/* Show disk format */

static const char *sim_disk_fmt (UNIT *uptr)
{
int32 f = DK_GET_FMT (uptr);
static char fmt_buf[32];
static const char *encodings[] = {"", "DLD9", "DBD9", ""};
size_t i;

for (i = 0; fmts[i].name; i++)
    if (fmts[i].fmtval == f) {
        snprintf (fmt_buf, sizeof (fmt_buf), "%s%s%s", fmts[i].name, (DK_GET_ENC (uptr) > DKUF_E_AUTO) ? "-" : "", encodings[DK_GET_ENC (uptr)]);
        return fmt_buf;
        }
return "invalid";
}

t_stat sim_disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s format", sim_disk_fmt (uptr));
return SCPE_OK;
}

const char *_disk_tranfer_encoding (uint32 val)
{
static char encoding[128];

switch (val) {
    case 0:
        snprintf (encoding, sizeof (encoding), "Unexpected packing/encoding missing (i.e. 0)");
        break;
    case 1:
    case 2:
    case 4:
    case 8:
        snprintf (encoding, sizeof (encoding), "%u bytes in and out", val);
        break;
    case DK_ENC_LL_DLD9:
        snprintf (encoding, sizeof (encoding), "DLD9: 36bits on disk (little endian order) to 64bits in memory");
        break;
    case DK_ENC_LL_DBD9:
        snprintf (encoding, sizeof (encoding), "DBD9: 36bits on disk (big endian order) to 64bits in memory");
        break;
    default:
        snprintf (encoding, sizeof (encoding), "Unexpected encoding: %u bits on disk packed %s endian order to %u bits in memory %s endian order",
                                    (val >> DK_ENC_XFR_IN) & 0x7F, ((val >> DK_ENC_XFR_IN) & DK_ENC_X_LSB) ? "little" : "big",
                                    (val >> DK_ENC_XFR_OUT) & 0x7F, ((val >> DK_ENC_XFR_OUT) & DK_ENC_X_LSB) ? "little" : "big");
        break;
    }
return encoding;
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
                sim_disk_attach (uptr, path, ctx->sector_size, ctx->xfer_encode_size,
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
sim_debug_unit (ctx->dbit, uptr, "sim_disk_isavailable(unit=%d)=%s\n", (int)(uptr - ctx->dptr->units), is_available ? "true" : "false");
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

t_stat sim_disk_set_all_noautosize (int32 flag, CONST char *cptr)
{
DEVICE *dptr;
uint32 dev, unit, count = 0;
int32 saved_sim_show_message = sim_show_message;

sim_show_message = FALSE;
for (dev = 0; (dptr = sim_devices[dev]) != NULL; dev++) {
    t_bool device_disabled = ((dptr->flags & DEV_DIS) != 0);

    if ((DEV_TYPE (dptr) != DEV_DISK) &&
        (DEV_TYPE (dptr) != DEV_SCSI))                          /* If not a sim_disk device? */
        continue;                                               /*   skip this device */

    if (device_disabled)
        dptr->flags &= ~DEV_DIS;                                /* Temporarily enable device */
    ++count;
    for (unit = 0; unit < dptr->numunits; unit++) {
        char cmd[CBUFSIZE];
        t_bool unit_disabled = ((dptr->units[unit].flags & UNIT_DIS) != 0);

        if (unit_disabled &&                                    /* disabled and */
            ((dptr->units[unit].flags & UNIT_DISABLE) == 0))    /* can't be enabled? */
             continue;                                          /*  Not a drive unit, so skip. */

        if (unit_disabled)
            dptr->units[unit].flags &= ~UNIT_DIS;               /* Temporarily enable unit */
        sprintf (cmd, "%s %sAUTOSIZE", sim_uname (&dptr->units[unit]), (flag != 0) ? "NO" : "");
        set_cmd (0, cmd);
        if (unit_disabled)
            dptr->units[unit].flags |= ~UNIT_DIS;               /* leave unit disabled again */
        }
    if (device_disabled)
        dptr->flags |= DEV_DIS;                                 /* leave device the way we found it */
    }
sim_show_message = saved_sim_show_message;
if (count == 0)
    return sim_messagef (SCPE_ARG, "No disk devices support autosizing\n");
return SCPE_OK;
}

/* Set disk autosize */

t_stat sim_disk_set_autosize (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr == NULL)
    return SCPE_IERR;
if ((uptr->drvtyp != NULL) &&
    (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI) &&
    (uptr->drvtyp->devtype == SCSI_TAPE))
    return sim_messagef (SCPE_NOFNC, "%s: Autosizing Tapes is not supported\n", sim_uname (uptr));
if (cptr != NULL)
    return sim_messagef (SCPE_ARG, "%s: Unexpected autosize argument: %s\n", sim_uname (uptr), cptr);
if (((uptr->flags & UNIT_ATT) != 0) && ((uptr->drvtyp == NULL) || ((uptr->drvtyp->flags & DRVFL_DETAUTO) == 0)))
    return sim_messagef (SCPE_ALATT, "%s: Disk already attached, autosizing not changed\n", sim_uname (uptr));
if (val ^ ((uptr->flags & DKUF_NOAUTOSIZE) != 0))
    return SCPE_OK;
if (val)
    uptr->flags &= ~DKUF_NOAUTOSIZE;
else
    uptr->flags |= DKUF_NOAUTOSIZE;
return SCPE_OK;
}

/* Show disk autosize */

t_stat sim_disk_show_autosize (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if ((uptr->drvtyp != NULL) &&
    (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI) &&
    (uptr->drvtyp->devtype == SCSI_TAPE))
    return SCPE_NOFNC;
fprintf (st, "%sautosize", ((uptr->flags & DKUF_NOAUTOSIZE) != 0) ? "no" : "");
return SCPE_OK;
}

t_stat sim_disk_set_all_autozap (int32 flag, CONST char *cptr)
{
DEVICE *dptr;
uint32 dev, unit, count = 0;
int32 saved_sim_show_message = sim_show_message;

sim_show_message = FALSE;
for (dev = 0; (dptr = sim_devices[dev]) != NULL; dev++) {
    t_bool device_disabled = ((dptr->flags & DEV_DIS) != 0);

    if ((DEV_TYPE (dptr) != DEV_DISK) &&
        (DEV_TYPE (dptr) != DEV_SCSI))                          /* If not a sim_disk device? */
        continue;                                               /*   skip this device */

    if (device_disabled)
        dptr->flags &= ~DEV_DIS;                                /* Temporarily enable device */
    ++count;
    for (unit = 0; unit < dptr->numunits; unit++) {
        char cmd[CBUFSIZE];
        t_bool unit_disabled = ((dptr->units[unit].flags & UNIT_DIS) != 0);

        if (unit_disabled &&                                    /* disabled and */
            ((dptr->units[unit].flags & UNIT_DISABLE) == 0))    /* can't be enabled? */
             continue;                                          /*  Not a drive unit, so skip. */

        if (unit_disabled)
            dptr->units[unit].flags &= ~UNIT_DIS;               /* Temporarily enable unit */
        sprintf (cmd, "%s %sAUTOZAP", sim_uname (&dptr->units[unit]), (flag != 0) ? "" : "NO");
        set_cmd (0, cmd);
        if (unit_disabled)
            dptr->units[unit].flags |= ~UNIT_DIS;               /* leave unit disabled again */
        }
    if (device_disabled)
        dptr->flags |= DEV_DIS;                                 /* leave device the way we found it */
    }
sim_show_message = saved_sim_show_message;
if (count == 0)
    return sim_messagef (SCPE_ARG, "No disk devices in the %s simulator support autozap\n", sim_name);
return SCPE_OK;
}

/* Set disk autozap */

t_stat sim_disk_set_autozap (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr == NULL)
    return SCPE_IERR;
if ((uptr->drvtyp != NULL) &&
    (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI) &&
    (uptr->drvtyp->devtype == SCSI_TAPE))
    return sim_messagef (SCPE_NOFNC, "%s: Autozapping Tapes is not supported\n", sim_uname (uptr));
if (cptr != NULL)
    return sim_messagef (SCPE_ARG, "%s: Unexpected autozap argument: %s\n", sim_uname (uptr), cptr);
if (val ^ ((uptr->flags & DKUF_AUTOZAP) == 0))
    return SCPE_OK;
if (val)
    uptr->flags |= DKUF_AUTOZAP;
else
    uptr->flags &= ~DKUF_AUTOZAP;
return SCPE_OK;
}

/* Show disk autozap */

t_stat sim_disk_show_autozap (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if ((uptr->drvtyp != NULL) &&
    (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI) &&
    (uptr->drvtyp->devtype == SCSI_TAPE))
    return SCPE_NOFNC;
fprintf (st, "%sautozap", ((uptr->flags & DKUF_AUTOZAP) != 0) ? "" : "no" );
return SCPE_OK;
}


/* Test for write protect */

t_bool sim_disk_wrp (UNIT *uptr)
{
return (uptr->flags & DKUF_WRP)? TRUE: FALSE;
}

/* Get Disk size */

t_offset sim_disk_size (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_offset physical_size, filesystem_size;
t_bool saved_quiet = sim_quiet;

if ((uptr->flags & UNIT_ATT) == 0)
    return (t_offset)-1;
physical_size = ctx->container_size;
sim_quiet = TRUE;
filesystem_size = get_filesystem_size (uptr, NULL);
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
return sim_messagef (SCPE_NOFNC, "Disk: cannot operate asynchronously\n");
#else
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
pthread_attr_t attr;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_set_async(unit=%d)\n", (int)(uptr - ctx->dptr->units));

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

sim_debug_unit (ctx->dbit, uptr, "sim_disk_clr_async(unit=%d)\n", (int)(uptr - ctx->dptr->units));

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

sim_debug_unit (ctx->dbit, uptr, "_sim_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

da = ((t_offset)lba) * ctx->sector_size;
tbc = sects * ctx->sector_size;
if (sectsread)
    *sectsread = 0;
while (tbc) {
    size_t sectbytes;

    clearerr (uptr->fileref);
    err = sim_fseeko (uptr->fileref, da, SEEK_SET);          /* set pos */
    if (err)
        return SCPE_IOERR;
    i = sim_fread (buf, 1, tbc, uptr->fileref);
    if (i < tbc)                 /* fill */
        memset (&buf[i], 0, tbc-i);
    if ((i == 0) &&             /* Reading at or past EOF? */
        feof (uptr->fileref))
        i = tbc;                /* return 0's which have already been filled in buffer */
    sectbytes = (i / ctx->sector_size) * ctx->sector_size;
    if (i > sectbytes)
        sectbytes += ctx->sector_size;
    if (sectsread)
        *sectsread += sectbytes / ctx->sector_size;
    err = ferror (uptr->fileref);
    if (err)
        return SCPE_IOERR;
    tbc -= sectbytes;
    if ((tbc == 0) || (i == 0))
        return SCPE_OK;
    da += sectbytes;
    buf += sectbytes;
    }
return SCPE_OK;
}

t_stat sim_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
t_stat r;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint32 f = DK_GET_FMT (uptr);
t_seccnt sread = 0;
uint8 *tbuf = NULL;
uint8 *rbuf;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

ctx->read_count++;                                      /* record read operation */
if ((sects == 1) &&                                     /* Single sector reads */
    (lba >= (uptr->capac*ctx->capac_factor)/(ctx->sector_size/((ctx->dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)))) {/* beyond the end of the disk */
    memset (buf, '\0', ctx->sector_size);               /* are bad block management efforts - zero buffer */
    if (sectsread)
        *sectsread = 1;
    return SCPE_OK;                                     /* return success */
    }

if ((0 == (ctx->sector_size & (ctx->storage_sector_size - 1))) ||   /* Sector Aligned & whole sector transfers */
    ((0 == ((lba*ctx->sector_size) & (ctx->storage_sector_size - 1))) &&
     (0 == ((sects*ctx->sector_size) & (ctx->storage_sector_size - 1)))) ||
    (f == DKUF_F_STD) || (f == DKUF_F_VHD)) {                       /* or SIMH or VHD formats */
        if (ctx->xfer_encode_size > DK_ENC_LONGLONG) {
            tbuf = (uint8*) malloc (ctx->sector_size * sects);
            if (tbuf == NULL)
                return SCPE_MEM;
            rbuf = tbuf;
            }
        else
            rbuf = buf;
    switch (f) {                                        /* case on format */
        case DKUF_F_STD:                                /* SIMH format */
            r = _sim_disk_rdsect (uptr, lba, rbuf, &sread, sects);
            break;
        case DKUF_F_VHD:                                /* VHD format */
            r = sim_vhd_disk_rdsect (uptr, lba, rbuf, &sread, sects);
            break;
        case DKUF_F_RAW:                                /* Raw Physical Disk Access */
            r = sim_os_disk_rdsect (uptr, lba, rbuf, &sread, sects);
            break;
        default:
            free (tbuf);
            return SCPE_NOFNC;
        }
    if (sectsread)
        *sectsread = sread;
    if (ctx->xfer_encode_size > DK_ENC_LONGLONG) {
        uint32 sbits = (ctx->xfer_encode_size >> DK_ENC_XFR_IN) & 0x7F;
        t_bool sLSB = (((ctx->xfer_encode_size >> DK_ENC_XFR_IN) & DK_ENC_X_LSB) != 0);
        uint32 dbits = (ctx->xfer_encode_size >> DK_ENC_XFR_OUT) & 0x7F;
        t_bool dLSB = (((ctx->xfer_encode_size >> DK_ENC_XFR_OUT) & DK_ENC_X_LSB) != 0);
        uint32 scount = ((sread * ctx->sector_size) * 8) / sbits;

        sim_buf_pack_unpack (rbuf,      /* source buffer pointer */
                             buf,       /* destination buffer pointer */
                             sbits,     /* source buffer element size in bits */
                             sLSB,      /* source numbered using LSB ordering */
                             scount,    /* count of source elements */
                             dbits,     /* interesting bits of each destination element */
                             dLSB);     /* destination numbered using LSB ordering */
        }
    else
        sim_buf_swap_data (buf, ctx->xfer_encode_size, (sread * ctx->sector_size) / ctx->xfer_encode_size);
    free (tbuf);
    return r;
    }
else { /* Unaligned and/or partial sector transfers in RAW mode */
    size_t tbufsize = sects * ctx->sector_size + 2 * ctx->storage_sector_size;
    uint8 *tbuf = (uint8*) malloc (tbufsize);
    t_offset ssaddr = (lba * (t_offset)ctx->sector_size) & ~(t_offset)(ctx->storage_sector_size -1);
    uint32 soffset = (uint32)((lba * (t_offset)ctx->sector_size) - ssaddr);
    uint32 bytesread;

    if (sectsread)
        *sectsread = 0;
    if (tbuf == NULL)
        return SCPE_MEM;
    r = sim_os_disk_read (uptr, ssaddr, tbuf, &bytesread, tbufsize & ~(ctx->storage_sector_size - 1));
    sread = (bytesread - soffset) / ctx->sector_size;
    if (sread > sects)
        sread = sects;
    if (sectsread)
        *sectsread = sread;
    if (ctx->xfer_encode_size > DK_ENC_LONGLONG) {
        uint32 sbits = (ctx->xfer_encode_size >> DK_ENC_XFR_IN) & 0x7F;
        t_bool sLSB = (((ctx->xfer_encode_size >> DK_ENC_XFR_IN) & DK_ENC_X_LSB) != 0);
        uint32 dbits = (ctx->xfer_encode_size >> DK_ENC_XFR_OUT) & 0x7F;
        t_bool dLSB = (((ctx->xfer_encode_size >> DK_ENC_XFR_OUT) & DK_ENC_X_LSB) != 0);
        uint32 scount = ((sread * ctx->sector_size) * 8) / sbits;

        sim_buf_pack_unpack (tbuf + soffset,    /* source buffer pointer */
                             buf,               /* destination buffer pointer */
                             sbits,             /* source buffer element size in bits */
                             sLSB,              /* source numbered using LSB ordering */
                             scount,            /* count of source elements */
                             dbits,             /* interesting bits of each destination element */
                             dLSB);             /* destination numbered using LSB ordering */
        }
    else
        sim_buf_copy_swapped (buf, tbuf + soffset, ctx->xfer_encode_size, (sread * ctx->sector_size) / ctx->xfer_encode_size);
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

sim_debug_unit (ctx->dbit, uptr, "_sim_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

da = ((t_offset)lba) * ctx->sector_size;
tbc = sects * ctx->sector_size;
if (sectswritten)
    *sectswritten = 0;
err = sim_fseeko (uptr->fileref, da, SEEK_SET);          /* set pos */
if (err)
    return SCPE_IOERR;
i = sim_fwrite (buf, ctx->xfer_encode_size, tbc/ctx->xfer_encode_size, uptr->fileref);
if (sectswritten)
    *sectswritten += (t_seccnt)((i * ctx->xfer_encode_size + ctx->sector_size - 1)/ctx->sector_size);
err = ferror (uptr->fileref);
if (err)
    return SCPE_IOERR;
return SCPE_OK;
}

t_stat sim_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint32 f = DK_GET_FMT (uptr);
t_stat r;
uint8 *tbuf = NULL;
t_seccnt written = 0;

sim_debug_unit (ctx->dbit, uptr, "sim_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

if (sectswritten)
    *sectswritten = 0;
ctx->write_count++;                                     /* record write operation */
if (uptr->dynflags & UNIT_DISK_CHK) {
    DEVICE *dptr = find_dev_from_unit (uptr);
    uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */
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

            sim_printf ("\n%s: Write Address Verification Error on lbn %d(0x%X) of %d(0x%X).\n", sim_uname (uptr), (int)(lba+sect), (int)(lba+sect), (int)total_sectors, (int)total_sectors);
            dptr->dctrl = 0xFFFFFFFF;
            sim_deb = save_sim_deb ? save_sim_deb : stdout;
            sim_disk_data_trace (uptr, buf+sect*ctx->sector_size, lba+sect, ctx->sector_size,    "Found", TRUE, 1);
            dptr->dctrl = save_dctrl;
            sim_deb = save_sim_deb;
            }
        }
    }
switch (f) {                                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
        r = _sim_disk_wrsect (uptr, lba, buf, &written, sects);
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        if (!sim_end && (ctx->xfer_encode_size != sizeof (char))) {
            tbuf = (uint8*) malloc (sects * ctx->sector_size);
            if (NULL == tbuf)
                return SCPE_MEM;
            sim_buf_copy_swapped (tbuf, buf, ctx->xfer_encode_size, (sects * ctx->sector_size) / ctx->xfer_encode_size);
            buf = tbuf;
            }
        r = sim_vhd_disk_wrsect  (uptr, lba, buf, &written, sects);
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        break;                                          /* handle below */
    default:
        return SCPE_NOFNC;
    }
if (f == DKUF_F_RAW) {
    if ((0 == (ctx->sector_size & (ctx->storage_sector_size - 1))) ||   /* Sector Aligned & whole sector transfers */
        ((0 == ((lba*ctx->sector_size) & (ctx->storage_sector_size - 1))) &&
         (0 == ((sects*ctx->sector_size) & (ctx->storage_sector_size - 1))))) {

        if (!sim_end && (ctx->xfer_encode_size != sizeof (char))) {
            tbuf = (uint8*) malloc (sects * ctx->sector_size);
            if (NULL == tbuf)
                return SCPE_MEM;
            sim_buf_copy_swapped (tbuf, buf, ctx->xfer_encode_size, (sects * ctx->sector_size) / ctx->xfer_encode_size);
            buf = tbuf;
            }

        r = sim_os_disk_wrsect (uptr, lba, buf, &written, sects);
        }
    else { /* Unaligned and/or partial sector transfers in RAW mode */
        size_t tbufsize = sects * ctx->sector_size + 2 * ctx->storage_sector_size;
        t_offset ssaddr = (lba * (t_offset)ctx->sector_size) & ~(t_offset)(ctx->storage_sector_size -1);
        t_offset sladdr = ((lba + sects) * (t_offset)ctx->sector_size) & ~(t_offset)(ctx->storage_sector_size -1);
        uint32 soffset = (uint32)((lba * (t_offset)ctx->sector_size) - ssaddr);
        uint32 byteswritten;

        tbuf = (uint8*) malloc (tbufsize);
        if (tbuf == NULL)
            return SCPE_MEM;
        /* Partial Sector writes require a read-modify-write sequence for the partial sectors */
        if (soffset)
            sim_os_disk_read (uptr, ssaddr, tbuf, NULL, ctx->storage_sector_size);
        sim_os_disk_read (uptr, sladdr, tbuf + (size_t)(sladdr - ssaddr), NULL, ctx->storage_sector_size);
        sim_buf_copy_swapped (tbuf + soffset,
                              buf, ctx->xfer_encode_size, (sects * ctx->sector_size) / ctx->xfer_encode_size);
        r = sim_os_disk_write (uptr, ssaddr, tbuf, &byteswritten, (soffset + (sects * ctx->sector_size) + ctx->storage_sector_size - 1) & ~(ctx->storage_sector_size - 1));
        written = byteswritten / ctx->sector_size;
        if (written > sects)
            written = sects;
        }
    }
free (tbuf);
if (sectswritten)
    *sectswritten = written;
if (written > 0) {
    t_offset da = ((t_offset)lba) * ctx->sector_size;
    t_offset end_write = da + (written * ctx->sector_size);

    if (ctx->highwater < end_write)
        ctx->highwater = end_write;
    }
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
        sim_os_disk_unload_raw (uptr->fileref);         /* remove/eject disk */
        return sim_disk_detach (uptr);
        break;
    default:
        return SCPE_NOFNC;
    }
}

t_stat sim_disk_erase (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint8 *buf;
t_lba lba;

if (uptr->flags & UNIT_ATT)
    return SCPE_UNATT;

buf = (uint8 *)calloc (1, ctx->storage_sector_size);
if (buf == NULL)
    return SCPE_MEM;
for (lba = 0; lba < ctx->container_size / ctx->sector_size; lba++)
    sim_disk_wrsect (uptr, lba, buf, NULL, 1);          /* write sector */
free (buf);
return SCPE_OK;
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

static t_stat _sim_disk_rdsect_interleave (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects, uint16 sectpertrack, uint16 interleave, uint16 skew, uint16 offset)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_lba sectno = lba, psa;
t_stat status;

if (sectsread)
    *sectsread = 0;

do {
    uint16 i, track, sector;

    /*
     * Map an LBA address into a physical sector address
     */
    track = sectno / sectpertrack;
    i = (sectno % sectpertrack) * interleave;
    if (i >= sectpertrack)
        i++;
    sector = (i + (track * skew)) % sectpertrack;

    psa = sector + (track * sectpertrack) + offset;

    status = sim_disk_rdsect(uptr, psa, buf, NULL, 1);
    sects--;
    buf += ctx->sector_size;
    sectno++;
    if (sectsread)
        *sectsread += 1;
    } while ((sects != 0) && (status == SCPE_OK));

return status;
}

/*
 * Version of sim_disk_rdsect() specifically for filesystem detection of DEC
 * file systems. The routine handles regular DEC disks (physsectsz == 0) and
 * RX01/RX02 disks (physsectsz == 128 or == 256) which ignore track 0,
 * interleave physical sectors 2:1 for the remaining tracks and have a skew
 * 6 sectors at the end of a track.
 */
#define RX0xNSECT               26                      /* 26 sectors/track */
#define RX0xINTER               2                       /* 2 sector interleave */
#define RX0xISKEW               6                       /* 6 sectors interleave per track */

static t_stat _DEC_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects, uint32 physsectsz)
{
if (physsectsz == 0)            /* Use device natural sector size */
    return sim_disk_rdsect(uptr, lba, buf, sectsread, sects);

return _sim_disk_rdsect_interleave(uptr, lba, buf, sectsread, sects, RX0xNSECT, RX0xINTER, RX0xISKEW, RX0xNSECT);
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
    uint16 CheckSum = 0;
    uint16 *Buf = (uint16 *)Buffer;

    for (i=0; i<WordCount; i++)
        CheckSum += Buf[i];
    return CheckSum;
    }


static t_offset get_ods2_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_offset temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
ODS2_HomeBlock Home;
ODS2_FileHeader Header;
ODS2_Retreval *Retr;
ODS2_SCB Scb;
uint16 CheckSum1, CheckSum2;
uint32 ScbLbn = 0;
t_offset ret_val = (t_offset)-1;
t_seccnt sects_read;

saved_capac = uptr->capac;
uptr->capac = (t_addr)temp_capac;
if ((_DEC_rdsect (uptr, 512 / ctx->sector_size, (uint8 *)&Home, &sects_read, sizeof (Home) / ctx->sector_size, physsectsz)) ||
    (sects_read != (sizeof (Home) / ctx->sector_size)))
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
if ((_DEC_rdsect (uptr, (Home.hm2_l_ibmaplbn+Home.hm2_w_ibmapsize+1) * (512 / ctx->sector_size),
                  (uint8 *)&Header, &sects_read, sizeof (Header) / ctx->sector_size, physsectsz)) ||
    (sects_read != (sizeof (Header) / ctx->sector_size)))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Header, 255);
if (CheckSum1 != *(((uint16 *)&Header)+255)) /* Verify Checksum on BITMAP.SYS file header */
    goto Return_Cleanup;
Retr = (ODS2_Retreval *)(((uint16*)(&Header))+Header.fh2_b_mpoffset);
/* The BitMap File has a single extent, which may be preceded by a placement descriptor */
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
if ((_DEC_rdsect (uptr, ScbLbn * (512 / ctx->sector_size), (uint8 *)&Scb, &sects_read, sizeof (Scb) / ctx->sector_size, physsectsz)) ||
    (sects_read != (sizeof (Scb) / ctx->sector_size)))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Scb, 255);
if (CheckSum1 != *(((uint16 *)&Scb)+255)) /* Verify Checksum on Storage Control Block */
    goto Return_Cleanup;
if ((Scb.scb_w_cluster != Home.hm2_w_cluster) ||
    (Scb.scb_b_strucver != Home.hm2_b_strucver) ||
    (Scb.scb_b_struclev != Home.hm2_b_struclev))
    goto Return_Cleanup;
sim_messagef (SCPE_OK, "%s: '%s' Contains ODS%d File system\n", sim_uname (uptr), sim_relative_path (uptr->filename), Home.hm2_b_struclev);
sim_messagef (SCPE_OK, "%s: Volume Name: %12.12s Format: %12.12s Sectors In Volume: %u\n",
                                   sim_uname (uptr), Home.hm2_t_volname, Home.hm2_t_format, Scb.scb_l_volsize);
ret_val = ((t_offset)Scb.scb_l_volsize) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}

static t_offset get_ods1_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
ODS1_HomeBlock Home;
ODS1_FileHeader Header;
ODS1_Retreval *Retr;
uint8 scb_buf[512];
ODS1_SCB *Scb = (ODS1_SCB *)scb_buf;
uint16 CheckSum1, CheckSum2;
uint32 ScbLbn;
t_offset ret_val = (t_offset)-1;
t_seccnt sects_read;

saved_capac = uptr->capac;
uptr->capac = temp_capac;
if ((_DEC_rdsect (uptr, 512 / ctx->sector_size, (uint8 *)&Home, &sects_read, sizeof (Home) / ctx->sector_size, physsectsz)) ||
    (sects_read != (sizeof (Home) / ctx->sector_size)))
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
if ((_DEC_rdsect (uptr, (((Home.hm1_l_ibmaplbn << 16) + ((Home.hm1_l_ibmaplbn >> 16) & 0xFFFF)) + Home.hm1_w_ibmapsize + 1) * (512 / ctx->sector_size),
                  (uint8 *)&Header, &sects_read, sizeof (Header) / ctx->sector_size, physsectsz)) ||
    (sects_read != (sizeof (Header) / ctx->sector_size)))
    goto Return_Cleanup;
CheckSum1 = ODSChecksum (&Header, 255);
if (CheckSum1 != *(((uint16 *)&Header)+255)) /* Verify Checksum on BITMAP.SYS file header */
    goto Return_Cleanup;

Retr = (ODS1_Retreval *)(((uint16*)(&Header))+Header.fh1_b_mpoffset);
ScbLbn = (Retr->fm1_pointers[0].fm1_s_fm1def1.fm1_b_highlbn<<16)+Retr->fm1_pointers[0].fm1_s_fm1def1.fm1_w_lowlbn;
if ((_DEC_rdsect (uptr, ScbLbn * (512 / ctx->sector_size), (uint8 *)Scb, &sects_read, 512 / ctx->sector_size, physsectsz)) ||
    (sects_read != (512 / ctx->sector_size)))
    goto Return_Cleanup;
if (Scb->scb_b_bitmapblks < 127)
    ret_val = (((t_offset)Scb->scb_r_blocks[Scb->scb_b_bitmapblks].scb_w_freeblks << 16) + Scb->scb_r_blocks[Scb->scb_b_bitmapblks].scb_w_freeptr) * 512;
else
    ret_val = (((t_offset)Scb->scb_r_blocks[0].scb_w_freeblks << 16) + Scb->scb_r_blocks[0].scb_w_freeptr) * 512;
sim_messagef (SCPE_OK, "%s: '%s' Contains an ODS1 File system\n", sim_uname (uptr), sim_relative_path (uptr->filename));
sim_messagef (SCPE_OK, "%s: Volume Name: %12.12s Format: %12.12s Sectors In Volume: %u\n",
                                sim_uname (uptr), Home.hm1_t_volname, Home.hm1_t_format, (uint32)(ret_val / 512));
Return_Cleanup:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
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

static t_offset get_ultrix_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 sector_buf[512];
ultrix_disklabel *Label = (ultrix_disklabel *)(sector_buf + sizeof (sector_buf) - sizeof (ultrix_disklabel));
t_offset ret_val = (t_offset)-1;
int i;
uint32 max_lbn = 0, max_lbn_partnum = 0;
t_seccnt sects_read;

saved_capac = uptr->capac;
uptr->capac = temp_capac;
if ((_DEC_rdsect (uptr, 31 * (512 / ctx->sector_size), sector_buf, &sects_read, 512 / ctx->sector_size, physsectsz)) ||
    (sects_read != (512 / ctx->sector_size)))
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
sim_messagef (SCPE_OK, "%s: '%s' Contains Ultrix partitions\n", sim_uname (uptr), sim_relative_path (uptr->filename));
sim_messagef (SCPE_OK, "Partition with highest sector: %c, Sectors On Disk: %u\n", 'a' + max_lbn_partnum, max_lbn);
ret_val = ((t_offset)max_lbn) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}


/* ISO 9660 Volume Recognizer - Structure Info gathered from: https://wiki.osdev.org/ISO_9660 */

typedef struct ISO_9660_Volume_Descriptor {
    uint8   Type;                       // Volume Descriptor type code (0, 1, 2, 3 and 255)
    uint8   Identifier[5];              // Always 'CD001'.
    uint8   Version;                    // Volume Descriptor Version (0x01).
    uint8   Data[2041];                 // Depends on the volume descriptor type.
    } ISO_9660_Volume_Descriptor;

typedef struct ISO_9660_Primary_Volume_Descriptor {
    uint8   Type;                       // Always 0x01 for a Primary Volume Descriptor.
    uint8   Identifier[5];              // Always 'CD001'.
    uint8   Version;                    // Always 0x01.
    uint8   Unused;                     // Always 0x00.
    uint8   SystemIdentifier[32];       // The name of the system that can act upon sectors 0x00-0x0F for the volume.
    uint8   VolumeIdentifier[32];       // Identification of this volume.
    uint8   UnusedField[8];             // All zeros.
    uint32  VolumeSpaceSize[2];         // Number of Logical Blocks in which the volume is recorded
    uint8   UnusedField2[32];           // All zeroes.
    uint16  VolumeSetSize[2];           // The size of the set in this logical volume (number of disks).
    uint16  VolumeSequenceNumber[2];    // The number of this disk in the Volume Set.
    uint16  LogicalBlockSize[2];        // The size in bytes of a logical block. NB: This means that a logical block on a CD could be something other than 2 KiB!
    uint32  PathTableSize[2];           // The size in bytes of the path table.
    uint32  LocationTypeLPathTable;     // LBA location of the path table. The path table pointed to contains only little-endian values.
    uint32  LocationOptTypeLPathTable;  // LBA location of the optional path table. The path table pointed to contains only little-endian values. Zero means that no optional path table exists.
    uint32  LocationTypeMPathTable;     // LBA location of the path table. The path table pointed to contains only big-endian values.
    uint32  LocationOptTypeMPathTable;  // LBA location of the optional path table. The path table pointed to contains only big-endian values. Zero means that no optional path table exists.
    uint8   DirectoryEntryRootDirectory[34];// Note that this is not an LBA address, it is the actual Directory Record, which contains a single byte Directory Identifier (0x00), hence the fixed 34 byte size.
    uint8   VolumeSetIdentifier[128];   // Identifier of the volume set of which this volume is a member.
    uint8   PublisherIdentifier[128];   // The volume publisher. For extended publisher information, the first byte should be 0x5F, followed by the filename of a file in the root directory. If not specified, all bytes should be 0x20.
    uint8   DataPreparerIdentifier[128];// The identifier of the person(s) who prepared the data for this volume. For extended preparation information, the first byte should be 0x5F, followed by the filename of a file in the root directory. If not specified, all bytes should be 0x20.
    uint8   ApplicationIdentifier[128]; // Identifies how the data are recorded on this volume. For extended information, the first byte should be 0x5F, followed by the filename of a file in the root directory. If not specified, all bytes should be 0x20.
    uint8   CopyrightFileIdentifier[37];// Filename of a file in the root directory that contains copyright information for this volume set. If not specified, all bytes should be 0x20.
    uint8   AbstractFileIdentifier[37]; // Filename of a file in the root directory that contains abstract information for this volume set. If not specified, all bytes should be 0x20.
    uint8   BibliographicFileIdentifier[37];// Filename of a file in the root directory that contains bibliographic information for this volume set. If not specified, all bytes should be 0x20.
    uint8   VolumeCreationDateTime[17]; // The date and time of when the volume was created.
    uint8   VolumeModificationDateTime[17];// The date and time of when the volume was modified.
    uint8   VolumeExpirationDateTime[17];// The date and time after which this volume is considered to be obsolete. If not specified, then the volume is never considered to be obsolete.
    uint8   VolumeEffectiveDateTime[17];// The date and time after which the volume may be used. If not specified, the volume may be used immediately.
    uint8   FileStructureVersion;       // The directory records and path table version (always 0x01).
    uint8   Unused2;                    // Always 0x00.
    uint8   ApplicationUsed[512];       // Contents not defined by ISO 9660.
    uint8   Reserved[653];              // Reserved by ISO.
    } ISO_9660_Primary_Volume_Descriptor;

static t_offset get_iso9660_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 sector_buf[2048];
ISO_9660_Volume_Descriptor *Desc = (ISO_9660_Volume_Descriptor *)sector_buf;
uint8 primary_buf[2048];
ISO_9660_Primary_Volume_Descriptor *Primary = NULL;
t_lba sectfactor = sizeof (*Desc) / ctx->sector_size;
t_offset ret_val = (t_offset)-1;
t_offset cur_pos = 32768;           /* Beyond the boot area of an ISO 9660 image */
t_seccnt sectsread;
int read_count = 0;

saved_capac = uptr->capac;
uptr->capac = temp_capac;

while (sim_disk_rdsect(uptr, (t_lba)(sectfactor * cur_pos / sizeof (*Desc)), (uint8 *)Desc, &sectsread, sectfactor) == DKSE_OK) {
    if ((sectsread != sectfactor)               ||
        (Desc->Version != 1)                    ||
        (0 != memcmp (Desc->Identifier, "CD001", sizeof (Desc->Identifier))))
        break;
    if (Desc->Type == 1) {  /* Primary Volume Descriptor */
        Primary = (ISO_9660_Primary_Volume_Descriptor *)primary_buf;
        *Primary = *(ISO_9660_Primary_Volume_Descriptor *)Desc;
        }
    cur_pos += sizeof (*Desc);
    ++read_count;
    if ((Desc->Type == 255) ||
        (read_count >= 32)) {
        ret_val = ctx->container_size;
        sim_messagef (SCPE_OK, "%s: '%s' Contains an ISO 9660 filesystem\n", sim_uname (uptr), sim_relative_path (uptr->filename));
        if (Primary) {
            char VolId[sizeof (Primary->VolumeIdentifier) + 1];

            memcpy (VolId, Primary->VolumeIdentifier, sizeof (Primary->VolumeIdentifier));
            VolId[sizeof (Primary->VolumeIdentifier)] = '\0';
            sim_messagef (SCPE_OK, "%s: Volume Identifier: %s   Containing %u %u Byte Sectors\n", sim_uname (uptr), sim_trim_endspc (VolId), (uint32)(ctx->container_size / Primary->LogicalBlockSize[1 - sim_end]), (uint32)Primary->LogicalBlockSize[1 - sim_end]);
            }
        break;
        }
    }
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr) || (ret_val != (t_offset)-1);
return ret_val;
}

/* 2.11 BSD Volume Recognizer - Structure Info gathered from: the 2.11 BSD disklabel section 5 man page */

#define BSD_DISKMAGIC           ((uint32) 0x82564557)   /* The disk label magic number */
#define BSD_211_MAXPARTITIONS   8

typedef struct BSD_211_disklabel {
    uint32  d_magic;        /* the magic number */
    uint8   d_type;         /* drive type */
    uint8   d_subtype;      /* controller/d_type specific */
    char    d_typename[16]; /* type name, e.g. "eagle" */
    /*
     * d_packname contains the pack identifier and is returned when
     * the disklabel is read off the disk or in-core copy.
     * d_boot0 is the (optional) name of the primary (block 0) bootstrap
     * as found in /mdec.  This is returned when using
     * getdiskbyname(3) to retrieve the values from /etc/disktab.
     */
    char    d_packname[16];     /* pack identifier */
                                /* disk geometry: */
    uint16  d_secsize;          /* # of bytes per sector */
    uint16  d_nsectors;         /* # of data sectors per track */
    uint16  d_ntracks;          /* # of tracks per cylinder */
    uint16  d_ncylinders;       /* # of data cylinders per unit */
    uint16  d_secpercyl;        /* # of data sectors per cylinder */
    uint32  d_secperunit;       /* # of data sectors per unit */
    /*
     * Spares (bad sector replacements) below
     * are not counted in d_nsectors or d_secpercyl.
     * Spare sectors are assumed to be physical sectors
     * which occupy space at the end of each track and/or cylinder.
     */
    uint16  d_sparespertrack;   /* # of spare sectors per track */
    uint16  d_sparespercyl;     /* # of spare sectors per cylinder */
    /*
     * Alternate cylinders include maintenance, replacement,
     * configuration description areas, etc.
     */
    uint16  d_acylinders;       /* # of alt. cylinders per unit */

        /* hardware characteristics: */
    /*
     * d_interleave, d_trackskew and d_cylskew describe perturbations
     * in the media format used to compensate for a slow controller.
     * Interleave is physical sector interleave, set up by the formatter
     * or controller when formatting.  When interleaving is in use,
     * logically adjacent sectors are not physically contiguous,
     * but instead are separated by some number of sectors.
     * It is specified as the ratio of physical sectors traversed
     * per logical sector.  Thus an interleave of 1:1 implies contiguous
     * layout, while 2:1 implies that logical sector 0 is separated
     * by one sector from logical sector 1.
     * d_trackskew is the offset of sector 0 on track N
     * relative to sector 0 on track N-1 on the same cylinder.
     * Finally, d_cylskew is the offset of sector 0 on cylinder N
     * relative to sector 0 on cylinder N-1.
     */
    uint16  d_rpm;              /* rotational speed */
    uint8   d_interleave;       /* hardware sector interleave */
    uint8   d_trackskew;        /* sector 0 skew, per track */
    uint8   d_cylskew;          /* sector 0 skew, per cylinder */
    uint8   d_headswitch;       /* head swith time, usec */
    uint16  d_trkseek;          /* track-to-track seek, msec */
    uint16  d_flags;            /* generic flags */
#define NDDATA 5
    uint32  d_drivedata[NDDATA]; /* drive-type specific information */
#define NSPARE 5
    uint32  d_spare[NSPARE];    /* reserved for future use */
    uint32  d_magic2;           /* the magic number (again) */
    uint16  d_checksum;         /* xor of data incl. partitions */

            /* filesystem and partition information: */
    uint16  d_npartitions;      /* number of partitions in following */
    uint8   d_bbsize;           /* size of boot area at sn0, bytes */
    uint8   d_sbsize;           /* max size of fs superblock, bytes */
    struct  {                   /* the partition table */
        uint32  p_size;         /* number of sectors in partition */
        uint32  p_offset;       /* starting sector */
        uint16  p_fsize;        /* filesystem basic fragment size */
        uint8   p_fstype;       /* filesystem type, see below */
        uint8   p_frag;         /* filesystem fragments per block */
        } d_partitions[BSD_211_MAXPARTITIONS];/* actually may be more */
} BSD_211_disklabel;


static t_offset get_BSD_211_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 sector_buf[512];
BSD_211_disklabel *Label = (BSD_211_disklabel *)sector_buf;
t_offset ret_val = (t_offset)-1;
uint16 i;
uint32 max_lbn = 0, max_lbn_partnum = 0;
t_seccnt sects_read;
uint16 sum = 0;
uint16 *pdata;
#define WORDSWAP(l) (((l >> 16) & 0xFFFF) | ((l & 0xFFFF) << 16))

saved_capac = uptr->capac;
uptr->capac = temp_capac;
if ((_DEC_rdsect (uptr, 1, sector_buf, &sects_read, 512 / ctx->sector_size, physsectsz)) ||
    (sects_read != (512 / ctx->sector_size)))
    goto Return_Cleanup;

/* Confirm the Label magic numbers */
if ((WORDSWAP(Label->d_magic) != BSD_DISKMAGIC) ||
    (WORDSWAP(Label->d_magic2) != BSD_DISKMAGIC))
    goto Return_Cleanup;

/* Verify the label checksum */
if (Label->d_npartitions > BSD_211_MAXPARTITIONS)
    goto Return_Cleanup;

pdata = (uint16 *)Label;
for (sum = 0, pdata = (uint16 *)Label; pdata < (uint16 *)&Label->d_partitions[Label->d_npartitions]; pdata++)
    sum ^= *pdata;

if (sum != 0)
    goto Return_Cleanup;

/* Walk through the partitions */
for (i = 0; i < Label->d_npartitions; i++) {
    uint32 end_lbn = WORDSWAP (Label->d_partitions[i].p_offset) + WORDSWAP (Label->d_partitions[i].p_size);
    if (end_lbn > max_lbn) {
        max_lbn = end_lbn;
        max_lbn_partnum = i;
        }
    }
sim_messagef (SCPE_OK, "%s: '%s' Contains BSD 2.11 partitions\n", sim_uname (uptr), sim_relative_path (uptr->filename));
sim_messagef (SCPE_OK, "Partition with highest sector: %c, Sectors On Disk: %u\n", 'a' + max_lbn_partnum, max_lbn);
ret_val = ((t_offset)max_lbn) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}

/* NetBSD Volume Recognizer - Structure Info gathered from: the NetBSD disklabel section 5 man page */

#define NETBSD_MAXPARTITIONS   22

typedef struct NetBSD_disklabel {
    uint32  d_magic;        /* the magic number */
    uint16  d_type;         /* drive type */
    uint16  d_subtype;      /* controller/d_type specific */
    char    d_typename[16]; /* type name, e.g. "eagle" */
    /*
     * d_packname contains the pack identifier and is returned when
     * the disklabel is read off the disk or in-core copy.
     * d_boot0 is the (optional) name of the primary (block 0) bootstrap
     * as found in /mdec.  This is returned when using
     * getdiskbyname(3) to retrieve the values from /etc/disktab.
     */
    char    d_packname[16];     /* pack identifier */
                                /* disk geometry: */
    uint32  d_secsize;          /* # of bytes per sector */
    uint32  d_nsectors;         /* # of data sectors per track */
    uint32  d_ntracks;          /* # of tracks per cylinder */
    uint32  d_ncylinders;       /* # of data cylinders per unit */
    uint32  d_secpercyl;        /* # of data sectors per cylinder */
    uint32  d_secperunit;       /* # of data sectors per unit */
    /*
     * Spares (bad sector replacements) below
     * are not counted in d_nsectors or d_secpercyl.
     * Spare sectors are assumed to be physical sectors
     * which occupy space at the end of each track and/or cylinder.
     */
    uint16  d_sparespertrack;   /* # of spare sectors per track */
    uint16  d_sparespercyl;     /* # of spare sectors per cylinder */
    /*
     * Alternate cylinders include maintenance, replacement,
     * configuration description areas, etc.
     */
    uint32  d_acylinders;       /* # of alt. cylinders per unit */

        /* hardware characteristics: */
    /*
     * d_interleave, d_trackskew and d_cylskew describe perturbations
     * in the media format used to compensate for a slow controller.
     * Interleave is physical sector interleave, set up by the formatter
     * or controller when formatting.  When interleaving is in use,
     * logically adjacent sectors are not physically contiguous,
     * but instead are separated by some number of sectors.
     * It is specified as the ratio of physical sectors traversed
     * per logical sector.  Thus an interleave of 1:1 implies contiguous
     * layout, while 2:1 implies that logical sector 0 is separated
     * by one sector from logical sector 1.
     * d_trackskew is the offset of sector 0 on track N
     * relative to sector 0 on track N-1 on the same cylinder.
     * Finally, d_cylskew is the offset of sector 0 on cylinder N
     * relative to sector 0 on cylinder N-1.
     */
    uint16  d_rpm;              /* rotational speed */
    uint16  d_interleave;       /* hardware sector interleave */
    uint16  d_trackskew;        /* sector 0 skew, per track */
    uint16  d_cylskew;          /* sector 0 skew, per cylinder */
    uint32  d_headswitch;       /* head swith time, usec */
    uint32  d_trkseek;          /* track-to-track seek, msec */
    uint32  d_flags;            /* generic flags */
#define NDDATA 5
    uint32  d_drivedata[NDDATA]; /* drive-type specific information */
#define NSPARE 5
    uint32  d_spare[NSPARE];    /* reserved for future use */
    uint32  d_magic2;           /* the magic number (again) */
    uint16  d_checksum;         /* xor of data incl. partitions */

            /* filesystem and partition information: */
    uint16  d_npartitions;      /* number of partitions in following */
    uint32  d_bbsize;           /* size of boot area at sn0, bytes */
    uint32  d_sbsize;           /* max size of fs superblock, bytes */
    struct  {                   /* the partition table */
        uint32  p_size;         /* number of sectors in partition */
        uint32  p_offset;       /* starting sector */
        uint32  p_fsize;        /* filesystem basic fragment size */
        uint8   p_fstype;       /* filesystem type, see below */
        uint8   p_frag;         /* filesystem fragments per block */
        union {
            uint16 cpg;         /* UFS: FS cylinders per group */
            uint16 sgs;         /* LFS: FS segment shift */
            } __partition_u1;
#define p_cpg   __partition_ul.cpg
#define p_sgs   __partition_ul.sgs
        } d_partitions[NETBSD_MAXPARTITIONS];/* actually may be more */
} NetBSD_disklabel;


static t_offset get_NetBSD_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 sector_buf[512];
NetBSD_disklabel *Label = (NetBSD_disklabel *)(&sector_buf[64]);
t_offset ret_val = (t_offset)-1;
uint16 i = sizeof (NetBSD_disklabel);
uint32 max_lbn = 0, max_lbn_partnum = 0;
t_seccnt sects_read;
uint16 sum = 0;
uint16 *pdata;

saved_capac = uptr->capac;
uptr->capac = temp_capac;
if ((_DEC_rdsect (uptr, 0, (uint8 *)sector_buf, &sects_read, 512 / ctx->sector_size, physsectsz)) ||
    (sects_read != (512 / ctx->sector_size)))
    goto Return_Cleanup;

/* Confirm the Label magic numbers */
if ((Label->d_magic != BSD_DISKMAGIC) ||
    (Label->d_magic2 != BSD_DISKMAGIC))
    goto Return_Cleanup;

/* Verify the label checksum */
if (Label->d_npartitions > NETBSD_MAXPARTITIONS)
    goto Return_Cleanup;

pdata = (uint16 *)Label;
for (sum = 0, pdata = (uint16 *)Label; pdata < (uint16 *)&Label->d_partitions[Label->d_npartitions]; pdata++)
    sum ^= *pdata;

if (sum != 0)
    goto Return_Cleanup;

/* Walk through the partitions */
for (i = 0; i < Label->d_npartitions; i++) {
    uint32 end_lbn = Label->d_partitions[i].p_offset + Label->d_partitions[i].p_size;
    if (end_lbn > max_lbn) {
        max_lbn = end_lbn;
        max_lbn_partnum = i;
        }
    }
sim_messagef (SCPE_OK, "%s: '%s' Contains NET/Open BSD partitions\n", sim_uname (uptr), sim_relative_path (uptr->filename));
sim_messagef (SCPE_OK, "Partition with highest sector: %c, Sectors On Disk: %u\n", 'a' + max_lbn_partnum, max_lbn);
ret_val = ((t_offset)max_lbn) * 512;

Return_Cleanup:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}


#pragma pack(push,1)
/*
 * The first logical block of device cluster 1 is either:
 *      1. MFD label entry (RSTS versions through 7.x)
 *      2. Disk Pack label (RSTS version 8.0 and later)
 */
typedef struct _RSTS_MFDLABEL {
    uint16  ml_ulnk;
    uint16  ml_mbm1;
    uint16  ml_reserved1;
    uint16  ml_reserved2;
    uint16  ml_pcs;
    uint16  ml_pstat;
    uint16  ml_packid[2];
    } RSTS_MFDLABEL;

typedef struct _RSTS_PACKLABEL {
    uint16  pk_mb01;
    uint16  pk_mbm1;
    uint16  pk_mdcn;
    uint16  pk_plvl;
#define PK_LVL0         0000
#define PK_LVL11        0401
#define PK_LVL12        0402
    uint16  pk_ppcs;
    uint16  pk_pstat;
#define PK_UC_NEW       0020000
    uint16  pk_packid[2];
    uint16  pk_tapgvn[2];
    uint16  pk_bckdat;
    uint16  pk_bcktim;
    } RSTS_PACKLABEL;

typedef union _RSTS_ROOT {
    RSTS_MFDLABEL  rt_mfd;
    RSTS_PACKLABEL rt_pack;
    uint8          rt_block[512];
    } RSTS_ROOT;

typedef struct _RSTS_MFDBLOCKETTE {
    uint16  mb_ulnk;
    uint16  mb_mbm1;
    uint16  mb_reserved1;
    uint16  mb_reserved2;
    uint16  mb_reserved3;
    uint16  mb_malnk;
    uint16  mb_lppn;
    uint16  mb_lid;
#define MB_ID           0051064
    } RSTS_MFDBLOCKETTE;
#define IS_VALID_RSTS_MFD(b) \
     ((((b)->mb_ulnk == 0) || ((b)->mb_ulnk == 1)) &&                         \
      ((b)->mb_mbm1 == 0177777) &&                                            \
      ((b)->mb_reserved1 == 0) &&                                             \
      ((b)->mb_reserved2 == 0) &&                                             \
      ((b)->mb_reserved3 == 0) &&                                             \
      ((b)->mb_lppn == 0177777) &&                                            \
      ((b)->mb_lid == MB_ID))

typedef struct _RSTS_GFDBLOCKETTE {
    uint16  gb_ulnk;
    uint16  gb_mbm1;
    uint16  gb_reserved1;
    uint16  gb_reserved2;
    uint16  gb_reserved3;
    uint16  gb_reserved4;
    uint16  gb_lppn;
    uint16  gb_lid;
#define GB_ID           0026264
    } RSTS_GFDBLOCKETTE;
#define IS_VALID_RSTS_GFD(b, g) \
     ((((b)->gb_ulnk == 0) || ((b)->gb_ulnk == 1)) &&                         \
      ((b)->gb_mbm1 == 0177777) &&                                            \
      ((b)->gb_reserved1 == 0) &&                                             \
      ((b)->gb_reserved2 == 0) &&                                             \
      ((b)->gb_reserved3 == 0) &&                                             \
      ((b)->gb_reserved4 == 0) &&                                             \
      ((b)->gb_lppn == (((g) << 8) | 0377)) &&                                \
      ((b)->gb_lid == GB_ID))

typedef struct _RSTS_UFDBLOCKETTE {
    uint16  ub_ulnk;
    uint16  ub_mbm1;
    uint16  ub_reserved1;
    uint16  ub_reserved2;
    uint16  ub_reserved3;
    uint16  ub_reserved4;
    uint16  ub_lppn;
    uint16  ub_lid;
#define UB_ID           0102064
    } RSTS_UFDBLOCKETTE;
#define IS_VALID_RSTS_UFD(b, g, u) \
     (((b)->ub_mbm1 == 0177777) &&                                            \
      ((b)->ub_reserved1 == 0) &&                                             \
      ((b)->ub_reserved2 == 0) &&                                             \
      ((b)->ub_reserved3 == 0) &&                                             \
      ((b)->ub_reserved4 == 0) &&                                             \
      ((b)->ub_lppn == (((g) << 8) | (u))) &&                                 \
      ((b)->ub_lid == UB_ID))

typedef struct _RSTS_UNAME {
    uint16  un_ulnk;
    uint16  un_unam;
    uint16  un_reserved1;
    uint16  un_reserved2;
    uint16  un_ustat;
    uint16  un_uacnt;
    uint16  un_uaa;
    uint16  un_uar;
    } RSTS_UNAME;

typedef struct _RSTS_FNAME {
    uint16  fn_ulnk;
    uint16  fn_unam[3];
    uint16  fn_ustat;
    uint16  fn_uacnt;
    uint16  fn_uaa;
    uint16  fn_uar;
    } RSTS_FNAME;

typedef struct _RSTS_ACNT {
    uint16  ac_ulnk;
    uint16  ac_udla;
    uint16  ac_usiz;
    uint16  ac_udc;
    uint16  ac_utc;
    uint16  ac_urts[2];
    uint16  ac_uclus;
    } RSTS_ACNT;

typedef struct _RSTS_RETR {
    uint16  rt_ulnk;
    uint16  rt_uent[7];
#define RT_ENTRIES      7
    } RSTS_RETR;

typedef struct _RSTS_DCMAP {
    uint16  dc_clus;
#define DC_MASK         0077777
    uint16  dc_map[7];
    }  RSTS_DCMAP;

/*
 * Directory link definitions
 */
#define DL_USE          0000001
#define DL_BAD          0000002
#define DL_CHE          0000004
#define DL_CLN          0000010
#define DL_ENO          0000760
#define DL_CLO          0007000
#define DL_BLO          0170000

#define DLSH_ENO         4
#define DLSH_CLO         9
#define DLSH_BLO        12

#define BLOCKETTE_SZ    (8 * sizeof(uint16))
#define MAP_OFFSET      (31 * BLOCKETTE_SZ)

#define SATT0           0073374
#define SATT1           0076400
#define SATT2           0075273

#pragma pack(pop)

typedef struct _rstsContext {
    UNIT        *uptr;
    int         dcshift;
    int         pcs;
    char        packid[8];
    t_seccnt    sects;
    RSTS_DCMAP  map;
} rstsContext;

static char rad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

static void r50Asc(uint16 val, char *buf)
{
buf[2] = rad50[val % 050];
val /= 050;
buf[1] = rad50[val % 050];
buf[0] = rad50[val / 050];
}

static t_stat rstsValidateClusterSize(uint16 size, uint16 minSize)
{
int i;

/*
 * Check that the cluster size is a power of 2 and greater than or equal
 * to some location dependent value.
 */
if (size >= minSize)
    for (i = 0; i < 16; i++)
        if (size == (1 << i))
            return SCPE_OK;

return SCPE_IOERR;
}

static t_stat rstsReadBlock(rstsContext *context, uint16 cluster, uint16 block, void *buf)
{
t_lba blk = (cluster << context->dcshift) + block;
t_seccnt sects_read;

if ((sim_disk_rdsect(context->uptr, blk * context->sects, (uint8 *)buf, &sects_read, context->sects) == SCPE_OK) &&
    (sects_read == context->sects))
    return SCPE_OK;

return SCPE_IOERR;
}

static t_stat rstsReadBlockette(rstsContext *context, uint16 link, void *buf)
{
uint16 block = (link & DL_BLO) >> DLSH_BLO;
uint16 dcn = (link & DL_CLO) >> DLSH_CLO;
uint16 blockette = (link & DL_ENO) >> DLSH_ENO;
uint8 temp[512];

if ((dcn != 7) && (blockette != 31) &&
    (block <= (context->map.dc_clus & DC_MASK))) {
    if (rstsReadBlock(context, context->map.dc_map[dcn], block, temp) == SCPE_OK) {
        memcpy(buf, &temp[blockette * BLOCKETTE_SZ], BLOCKETTE_SZ);
        return SCPE_OK;
        }
    }
return SCPE_IOERR;
}

static t_stat rstsFind01UFD(rstsContext *context, uint16 *ufd, uint16 *level)
{
uint16 dcs = 1 << context->dcshift;
RSTS_ROOT root;
uint16 buf[256];

if (rstsReadBlock(context, 1, 0, &root) == SCPE_OK) {
    /*
     * First validate fields which are common to both the MFD label and
     * Pack label - we'll use Pack label offsets here.
     */
    if ((root.rt_pack.pk_mbm1 == 0177777) &&
        (rstsValidateClusterSize(root.rt_pack.pk_ppcs, dcs) == SCPE_OK)) {
        char ch, *tmp = &context->packid[1];
        uint16 mfd, gfd;

        context->pcs = root.rt_pack.pk_ppcs;

        r50Asc(root.rt_pack.pk_packid[0], &context->packid[0]);
        r50Asc(root.rt_pack.pk_packid[1], &context->packid[3]);
        context->packid[6] = '\0';

        /*
         * The Pack ID must consist of 1 - 6 alphanumeric characters
         * padded at the end with spaces.
         */
        if (!isalnum(context->packid[0]))
            return SCPE_IOERR;

        while ((ch = *tmp++) != 0) {
            if (!isalnum(ch)) {
                if (ch != ' ')
                    return SCPE_IOERR;

                while (*tmp)
                    if (*tmp++ != ' ')
                        return SCPE_IOERR;
                break;
                }
            }

        /*
         * Determine the pack revision level and, therefore, the path to
         * [0,1]satt.sys which will allow us to determine the size of the
         * pack used by RSTS.
         */
        if ((root.rt_pack.pk_pstat & PK_UC_NEW) == 0) {
            uint16 link = root.rt_mfd.ml_ulnk;
            RSTS_UNAME uname;

            /*
             * Old format used by RSTS up through V07.x
             */
            if (dcs > 16)
                return SCPE_IOERR;

            *level = PK_LVL0;

            memcpy(&context->map, &root.rt_block[MAP_OFFSET], BLOCKETTE_SZ);

            /*
             * Scan the MFD name entries looking for [0,1]. Note there will
             * always be at least 1 entry.
             */
            do {
                if (rstsReadBlockette(context, link, &uname) != SCPE_OK)
                    break;

                if (uname.un_unam == ((0 << 8) | 1)) {
                    *ufd = uname.un_uar;
                    return SCPE_OK;
                    }
                } while ((link = uname.un_ulnk) != 0);
            }
        else {
            /*
             * New format used by RSTS V08 and later
             */
            switch (root.rt_pack.pk_plvl) {
                case PK_LVL11:
                    if (dcs > 16)
                        return SCPE_IOERR;
                    break;

                case PK_LVL12:
                    if (dcs > 64)
                        return SCPE_IOERR;
                    break;

                default:
                    return SCPE_IOERR;
                }
            *level = root.rt_pack.pk_plvl;

            mfd = root.rt_pack.pk_mdcn;

            if (rstsReadBlock(context, mfd, 0, buf) == SCPE_OK) {
                if (IS_VALID_RSTS_MFD((RSTS_MFDBLOCKETTE *)buf)) {
                    if (rstsReadBlock(context, mfd, 1, buf) == SCPE_OK)
                        if ((gfd = buf[0]) != 0)
                            if (rstsReadBlock(context, gfd, 0, buf) == SCPE_OK)
                                if (IS_VALID_RSTS_GFD((RSTS_GFDBLOCKETTE *)buf, 0)) {
                                    if (rstsReadBlock(context, gfd, 1, buf) == SCPE_OK)
                                        if ((*ufd = buf[1]) != 0)
                                            return SCPE_OK;
                                    }
                    }
                }
            }
        }
    }
return SCPE_IOERR;
}

static t_stat rstsLoadAndScanSATT(rstsContext *context, uint16 uaa, uint16 uar, t_offset *result)
{
uint8 bitmap[8192];
int i, j;
RSTS_ACNT acnt;
RSTS_RETR retr;

if (uar != 0) {
    if (rstsReadBlockette(context, uaa, &acnt) == SCPE_OK) {
        uint16 blocks = acnt.ac_usiz;
        uint16 offset = 0;

        if ((rstsValidateClusterSize(acnt.ac_uclus, context->pcs) != SCPE_OK) ||
            (blocks > 16))
            return SCPE_IOERR;

        memset(bitmap, 0xFF, sizeof(bitmap));

        if (blocks != 0) {
            do {
                int i, j;
                uint16 fcl;

                if (rstsReadBlockette(context, uar, &retr) != SCPE_OK)
                    return SCPE_IOERR;

                for (i = 0; i < RT_ENTRIES; i++) {
                    if ((fcl = retr.rt_uent[i]) == 0)
                        goto scanBitmap;

                    for (j = 0; j < acnt.ac_uclus; j++) {
                        if ((blocks == 0) || (offset >= sizeof(bitmap)))
                            goto scanBitmap;

                        if (rstsReadBlock(context, fcl, j, &bitmap[offset]) != SCPE_OK)
                            return SCPE_IOERR;

                        offset += 512;
                        blocks--;
                        }
                    }
                } while ((uar = retr.rt_ulnk) != 0);

        scanBitmap:
            for (i = sizeof(bitmap) - 1; i != 0; i--)
                if (bitmap[i] != 0xFF) {
                    blocks = i * 8;
                    for (j = 7; j >= 0; j--)
                        if ((bitmap[i] & (1 << j)) == 0) {
                            blocks += j + 1;
                            goto scanDone;
                            }
                    }
        scanDone:
            *result = (t_offset)(blocks + 1) * context->pcs;
            return SCPE_OK;
            }
        }
    }
return SCPE_IOERR;
}

static t_offset get_rsts_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 buf[512];
t_offset ret_val = (t_offset)-1;
rstsContext context;

saved_capac = uptr->capac;
uptr->capac = temp_capac;

context.uptr = uptr;
context.sects = 512 / ctx->sector_size;

/*
 * Check all possible device cluster sizes
 */
for (context.dcshift = 0; context.dcshift < 8; context.dcshift++) {
    uint16 ufd, level;

    /*
     * We need to find [0,1]SATT.SYS to compute the actual size of the disk.
     * First find the DCN of the [0,1] UFD.
     */
    if (rstsFind01UFD(&context, &ufd, &level) == SCPE_OK) {
        if (rstsReadBlock(&context, ufd, 0, buf) == SCPE_OK) {
            if (IS_VALID_RSTS_UFD((RSTS_UFDBLOCKETTE *)buf, 0, 1)) {
                uint16 link = ((RSTS_UFDBLOCKETTE *)buf)->ub_ulnk;
                RSTS_FNAME fname;

                memcpy(&context.map, &buf[MAP_OFFSET], BLOCKETTE_SZ);

                /*
                 * Scan the UFD looking for SATT.SYS - the allocation bitmap
                 */
                do {
                    if (rstsReadBlockette(&context, link, &fname) != SCPE_OK)
                        break;

                    if ((fname.fn_unam[0] == SATT0) &&
                        (fname.fn_unam[1] == SATT1) &&
                        (fname.fn_unam[2] == SATT2)) {
                        if (rstsLoadAndScanSATT(&context, fname.fn_uaa, fname.fn_uar, &ret_val) == SCPE_OK) {
                            const char *fmt = "???";

                            ret_val *= 512;

                            switch (level) {
                                case PK_LVL0:
                                    fmt = "0.0";
                                    break;

                                case PK_LVL11:
                                    fmt = "1.1";
                                    break;

                                case PK_LVL12:
                                    fmt = "1.2";
                                    break;
                                }

                            sim_messagef(SCPE_OK, "%s: '%s' Contains a RSTS File system\n", sim_uname (uptr), sim_relative_path (uptr->filename));
                            sim_messagef(SCPE_OK, "%s: Pack ID: %6.6s Revision Level: %3s Pack Clustersize: %d\n",
                                                                  sim_uname (uptr), context.packid, fmt, context.pcs);
                            sim_messagef(SCPE_OK, "%s: Last Unallocated Sector In File System: %u\n", sim_uname (uptr), (uint32)((ret_val / 512) - 1));
                            goto cleanup_done;
                            }
                        }
                    } while ((link = fname.fn_ulnk) != 0);
                }
            }
        }
    }
cleanup_done:
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}

#pragma pack(push,1)
typedef struct _RT11_HomeBlock {
    uint8   hb_b_bbtable[130];
    uint8   hb_b_unused1[2];
    uint8   hb_b_initrestore[38];
    uint8   hb_b_bup[18];
    uint8   hb_b_unused2[260];
    uint16  hb_w_reserved1;
    uint16  hb_w_reserved2;
    uint8   hb_b_unused3[14];
    uint16  hb_w_clustersize;
    uint16  hb_w_firstdir;
    uint16  hb_w_sysver;
#define HB_C_SYSVER_V3A 36521
#define HB_C_SYSVER_V04 36434
#define HB_C_SYSVER_V05 36435
    uint8   hb_b_volid[12];
    uint8   hb_b_owner[12];
    uint8   hb_b_sysid[12];
#define HB_C_SYSID      "DECRT11A    "
#define HB_C_VMSSYSID   "DECVMSEXCHNG"
    uint8   hb_b_unused4[2];
    uint16  hb_w_checksum;
    } RT11_HomeBlock;

typedef struct _RT11_DirHeader {
    uint16  dh_w_count;
    uint16  dh_w_next;
    uint16  dh_w_highest;
#define DH_C_MAXSEG     31
    uint16  dh_w_extra;
    uint16  dh_w_start;
    } RT11_DirHeader;

typedef struct _RT11_DirEntry {
    uint16  de_w_status;
#define DE_C_PRE        0000020
#define DE_C_TENT       0000400
#define DE_C_EMPTY      0001000
#define DE_C_PERM       0002000
#define DE_C_EOS        0004000
#define DE_C_READ       0040000
#define DE_C_PROT       0100000
    uint16  de_w_fname1;
    uint16  de_w_fname2;
    uint16  de_w_ftype;
    uint16  de_w_length;
    uint16  de_w_jobchannel;
    uint16  de_w_creationdate;
    } RT11_DirEntry;
#pragma pack(pop)

#define RT11_MAXPARTITIONS      256             /* Max partitions supported */
#define RT11_HOME                 1             /* Home block # */

#define RT11_NOPART             0
#define RT11_SINGLEPART         1
#define RT11_MULTIPART          2

static int rt11_get_partition_type(RT11_HomeBlock *home, int part)
{
if (strncmp((char *)&home->hb_b_sysid, HB_C_SYSID, strlen(HB_C_SYSID)) == 0) {
    uint16 type = home->hb_w_sysver;

    if (part == 0) {
        if ((type == HB_C_SYSVER_V3A) || (type == HB_C_SYSVER_V04))
            return RT11_SINGLEPART;
        }

    if (type == HB_C_SYSVER_V05)
        return RT11_MULTIPART;
    }

if (strncmp((char *)&home->hb_b_sysid, HB_C_VMSSYSID, strlen(HB_C_VMSSYSID)) == 0)
    return RT11_SINGLEPART;

return RT11_NOPART;
}

static t_offset get_rt11_filesystem_size (UNIT *uptr, uint32 physsectsz, t_bool *isreadonly)
{
t_addr saved_capac;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
t_addr temp_capac = (sim_toffset_64 ? (t_addr)0xFFFFFFFFu : (t_addr)0x7FFFFFFFu);  /* Make sure we can access the largest sector */
uint8 sector_buf[1024];
RT11_HomeBlock Home;
t_seccnt sects_read;
RT11_DirHeader *dir_hdr = (RT11_DirHeader *)sector_buf;
int partitions = 0;
int part;
uint32 base;
uint32 dir_sec;
uint16 dir_seg;
uint16 version = 0;
t_offset ret_val = (t_offset)-1;

saved_capac = uptr->capac;
uptr->capac = temp_capac;

for (part = 0; part < RT11_MAXPARTITIONS; part++) {
    uint16 seg_highest = 0;
    int type;

    /*
     * RX01/RX02 media can only have a single partition
     */
    if ((part != 0) && (physsectsz != 0))
      break;

    base = part << 16;

    if (_DEC_rdsect(uptr, (base + RT11_HOME) * (512 / ctx->sector_size), (uint8 *)&Home, &sects_read, 512 / ctx->sector_size, physsectsz) ||
        (sects_read != (512 / ctx->sector_size)))
        goto Return_Cleanup;

    type = rt11_get_partition_type(&Home, part);

    if (type != RT11_NOPART) {
        uint16 highest = 0;
        uint8 seg_seen[DH_C_MAXSEG + 1];

        memset(seg_seen, 0, sizeof(seg_seen));

        partitions++;

        dir_seg = 1;
        do {
            int offset = sizeof(RT11_DirHeader);
            int dir_size = sizeof(RT11_DirEntry);
            uint16 cur_blk;

            if (seg_seen[dir_seg]++ != 0)
                goto Next_Partition;

            dir_sec = Home.hb_w_firstdir + ((dir_seg - 1) * 2);

            if ((_DEC_rdsect(uptr, (base + dir_sec) * (512 / ctx->sector_size), sector_buf, &sects_read, 1024 / ctx->sector_size, physsectsz)) ||
                (sects_read != (1024 / ctx->sector_size)))
                goto Return_Cleanup;

            if (dir_seg == 1) {
                seg_highest = dir_hdr->dh_w_highest;
                if (seg_highest > DH_C_MAXSEG)
                    goto Next_Partition;
                }
            dir_size += dir_hdr->dh_w_extra;
            cur_blk = dir_hdr->dh_w_start;

            while ((1024 - offset) >= dir_size) {
                RT11_DirEntry *dir_entry = (RT11_DirEntry *)&sector_buf[offset];

                if (dir_entry->de_w_status & DE_C_EOS)
                    break;

                /*
                 * Within each directory segment the bas address should never
                 * decrease.
                 */
                if (((cur_blk + dir_entry->de_w_length) & 0xFFFF) < cur_blk)
                    goto Next_Partition;

                cur_blk += dir_entry->de_w_length;
                offset += dir_size;
                }
            if (cur_blk > highest)
                highest = cur_blk;
            dir_seg = dir_hdr->dh_w_next;

            if (dir_seg > seg_highest)
                goto Next_Partition;
            } while (dir_seg != 0);

        ret_val = (t_offset)((base + highest) * (t_offset)512);
        version = Home.hb_w_sysver;

        if (type == RT11_SINGLEPART)
          break;
        }
Next_Partition:
    ;
    }

Return_Cleanup:
if (partitions) {
    const char *parttype;

    switch (version) {
        case HB_C_SYSVER_V3A:
            parttype = "V3A";
            break;

        case HB_C_SYSVER_V04:
            parttype = "V04";
            break;

        case HB_C_SYSVER_V05:
            parttype = "V05";
            break;

        default:
            parttype = "???";
            break;
        }
    sim_messagef (SCPE_OK, "%s: '%s' Contains RT11 partitions\n", sim_uname (uptr), sim_relative_path (uptr->filename));
    sim_messagef (SCPE_OK, "%d valid partition%s, Type: %s, Sectors On Disk: %u\n", partitions, partitions == 1 ? "" : "s", parttype, (uint32)(ret_val / 512));
    }
uptr->capac = saved_capac;
if (isreadonly)
    *isreadonly = sim_disk_wrp (uptr);
return ret_val;
}

t_offset pseudo_filesystem_size = 0;        /* Dummy file system check return used during testing */

typedef t_offset (*FILESYSTEM_CHECK)(UNIT *uptr, uint32, t_bool *);

static t_offset get_filesystem_size (UNIT *uptr, t_bool *isreadonly)
{
static FILESYSTEM_CHECK checks[] = {
    &get_ods2_filesystem_size,
    &get_ods1_filesystem_size,
    &get_ultrix_filesystem_size,
    &get_iso9660_filesystem_size,
    &get_rsts_filesystem_size,
    &get_BSD_211_filesystem_size,
    &get_NetBSD_filesystem_size,
    &get_rt11_filesystem_size,          /* This should be the last entry
                                           in the table to reduce the
                                           possibility of matching an RT-11
                                           container file stored in another
                                           filesystem */
    NULL
    };
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint32 saved_sector_size = ctx->sector_size;
t_offset ret_val = (t_offset)-1;
int i;

if (isreadonly != NULL)
    *isreadonly = FALSE;

if (pseudo_filesystem_size != 0) {      /* Dummy file system size mechanism? */
    sim_messagef (SCPE_OK, "%s: '%s' Pseudo File System containing %u %d byte sectors\n", sim_uname (uptr), uptr->filename, (uint32)(pseudo_filesystem_size / ctx->sector_size), ctx->sector_size);
    return pseudo_filesystem_size;
    }

for (i = 0; checks[i] != NULL; i++)
    if ((ret_val = checks[i] (uptr, 0, isreadonly)) != (t_offset)-1) {
        /* ISO files that haven't already been determined to be ISO 9660
         * which contain a known file system are also marked read-only
         * now.  This fits early DEC distribution CDs that were created
         * before ISO 9660 was standardized and operating support was added.
         */
        if ((isreadonly != NULL)          &&
            (*isreadonly == FALSE)        &&
            (NULL != match_ext (uptr->filename, "ISO")))
            *isreadonly = TRUE;
        return ret_val;
        }
/*
 * The only known interleaved disk devices have either 256 byte
 * or 128 byte sector sizes.  If additional interleaved file
 * system scenarios with different sector sizes come up they
 * should be added here.
 */

for (i = 0; checks[i] != NULL; i++) {
    ctx->sector_size = 256;
    if ((ret_val = checks[i] (uptr, ctx->sector_size, isreadonly)) != (t_offset)-1)
        break;
    ctx->sector_size = 128;
    if ((ret_val = checks[i] (uptr, ctx->sector_size, isreadonly)) != (t_offset)-1)
        break;
    }
if (ret_val != (t_offset)-1) {
    ctx->data_ileave = RX0xINTER;
    ctx->data_ileave_skew = RX0xISKEW;
    if (ctx->sector_size != saved_sector_size)
        sim_messagef (SCPE_OK, "%s: with an unexpected sector size of %u bytes instead of %u bytes\n",
                               sim_uname (uptr), ctx->sector_size, saved_sector_size);
    }
ctx->sector_size = saved_sector_size;
return ret_val;
}

static t_stat store_disk_footer (UNIT *uptr, const char *dtype);

static t_stat get_disk_footer (UNIT *uptr, struct disk_context **pctx)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
struct simh_disk_footer *f = (struct simh_disk_footer *)calloc (1, sizeof (*f));
t_offset container_size;
t_offset sim_fsize_ex (FILE *fptr);
uint32 bytesread;

if (f == NULL)
    return SCPE_MEM;
if (pctx != NULL)
    *pctx = ctx;
sim_debug_unit (ctx->dbit, uptr, "get_disk_footer(%s)\n", sim_uname (uptr));
switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_STD:                                    /* SIMH format */
        container_size = sim_fsize_ex (uptr->fileref);
        if ((container_size != (t_offset)-1) && (container_size > (t_offset)sizeof (*f)) &&
            (sim_fseeko (uptr->fileref, container_size - sizeof (*f), SEEK_SET) == 0) &&
            (sizeof (*f) == sim_fread (f, 1, sizeof (*f), uptr->fileref)))
            break;
        free (f);
        f = NULL;
        break;
    case DKUF_F_RAW:                                    /* RAW format */
        container_size = sim_os_disk_size_raw (uptr->fileref);
        if ((container_size != (t_offset)-1) && (container_size > (t_offset)sizeof (*f)) &&
            (sim_os_disk_read (uptr, container_size - sizeof (*f), (uint8 *)f, &bytesread, sizeof (*f)) == SCPE_OK) &&
            (bytesread == sizeof (*f)))
            break;
        free (f);
        f = NULL;
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        if (1) {
            time_t creation_time;

            /* Construct a pseudo simh disk footer*/
            memcpy (f->Signature, "simh", 4);
            f->FooterVersion = FOOTER_VERSION;
            memset (f->DriveType, 0, sizeof (f->DriveType));
            strlcpy ((char *)f->DriveType, sim_vhd_disk_get_dtype (uptr->fileref, &f->SectorSize, &f->ElementEncodingSize, (char *)f->CreatingSimulator, &creation_time, &f->MediaID, (char *)f->DeviceName, &f->DataWidth), sizeof (f->DriveType));
            if ((f->DriveType[0] == 0) && (uptr->drvtyp != NULL))
                strlcpy ((char *)f->DriveType, uptr->drvtyp->name, sizeof (f->DriveType));
            if (ctx->sector_size == 0)
                ctx->sector_size = f->SectorSize;
            if (ctx->media_id == 0)
                ctx->media_id = f->MediaID;
            if (ctx->xfer_encode_size == 0)
                ctx->xfer_encode_size = f->ElementEncodingSize;
            f->Geometry = NtoHl (sim_vhd_CHS (uptr->fileref));
            f->DataWidth = NtoHl (f->DataWidth);
            f->SectorSize = NtoHl (f->SectorSize);
            f->MediaID = NtoHl (f->MediaID);
            f->ElementEncodingSize = NtoHl (f->ElementEncodingSize);
            if ((f->SectorSize == 0)                  ||      /* Old or mangled format VHD footer */
                (NtoHl (f->SectorSize) == 0x00020000) ||
                (NtoHl (f->MediaID) == 0)             ||
                ((sim_disk_find_type (uptr, (char *)f->DriveType) != NULL) &&
                 (NtoHl (f->Geometry) != sim_disk_drvtype_geometry (sim_disk_find_type (uptr, (char *)f->DriveType), NtoHl (f->SectorCount))))) {
                sim_vhd_disk_set_dtype (uptr->fileref, uptr->drvtyp ? uptr->drvtyp->name : (char *)f->DriveType, ctx->sector_size, ctx->xfer_encode_size, ctx->media_id, uptr->dptr->name, uptr->dptr->dwidth, uptr->drvtyp);
                sim_vhd_disk_get_dtype (uptr->fileref, &f->SectorSize, &f->ElementEncodingSize, (char *)f->CreatingSimulator, NULL, &f->MediaID, (char *)f->DeviceName, &f->DataWidth);
                f->DataWidth = NtoHl (f->DataWidth);
                f->SectorSize = NtoHl (f->SectorSize);
                f->MediaID = NtoHl (f->MediaID);
                f->ElementEncodingSize = NtoHl (f->ElementEncodingSize);
                }
            memset (f->CreationTime, 0, sizeof (f->CreationTime));
            strlcpy ((char*)f->CreationTime, ctime (&creation_time), sizeof (f->CreationTime));
            container_size = sim_vhd_disk_size (uptr->fileref);
            if ((f->SectorSize != 0) && (NtoHl (f->SectorSize) <= 65536)) /* Range check for Coverity sake */
                f->SectorCount = NtoHl ((uint32)(container_size / NtoHl (f->SectorSize)));
            container_size += sizeof (*f);      /* Adjust since it is removed below */
            f->AccessFormat = DKUF_F_VHD;
            f->Checksum = NtoHl (eth_crc32 (0, f, sizeof (*f) - sizeof (f->Checksum)));
            }
        break;
    default:
        free (f);
        return SCPE_IERR;
    }
if (f) {
    if (f->Checksum != NtoHl (eth_crc32 (0, f, sizeof (*f) - sizeof (f->Checksum)))) {
        sim_debug_unit (ctx->dbit, uptr, "No footer found on %s format container: %s\n", sim_disk_fmt (uptr), uptr->filename);
        free (f);
        f = NULL;
        }
    else {
        /* We've got a valid footer, but it may need to be corrected or have missing pieces added */
        if (((f->MediaID == 0) && (((uptr->drvtyp != NULL) ? uptr->drvtyp->MediaId : 0) != 0)) ||
            ((sim_disk_find_type (uptr, (char *)f->DriveType) != NULL) &&
             (NtoHl (f->Geometry) != sim_disk_drvtype_geometry (sim_disk_find_type (uptr, (char *)f->DriveType), NtoHl (f->SectorCount)))) ||
            ((NtoHl (f->ElementEncodingSize) == 1) &&   /* Encoding still 1 and SCSI disk/cdrom drive */
             ((0 == memcmp (f->DriveType, "RZ", 2)) ||
              (0 == memcmp (f->DriveType, "RR", 2))))) {
            f->ElementEncodingSize = NtoHl (2);
            if ((uptr->flags & UNIT_RO) == 0) {
                DEVICE *dptr = uptr->dptr;
                t_addr saved_capac = uptr->capac;
                uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */

                uptr->capac = (t_addr)(((t_offset)NtoHl(f->SectorCount) * NtoHl(f->SectorSize)) / capac_factor);
                store_disk_footer (uptr, (char *)f->DriveType);
                uptr->capac = saved_capac;
                *f = *ctx->footer;                  /* copy updated footer */
                }
            }
        if ((uptr->flags & UNIT_RO) == 0) {
            if ((NtoHl (f->SectorSize) != 512) &&
                (f->FooterVersion == 0)) {
                /* remove and rebuild early version original footer for non 512 byte sector containers */
                char *filename = strdup (uptr->filename);
                int32 saved_switches = sim_switches;
                uint32 saved_flags;

                uptr->flags |= UNIT_ATT;            /* mark as attached so detach works */
                sim_disk_detach (uptr);
                saved_flags = uptr->flags;
                sim_switches |= SWMASK ('Q');       /* silently */
                sim_switches |= SWMASK ('Z');       /* stripping trailing 0's */
                sim_disk_info_cmd (1, filename);    /* remove existing metadata */
                uptr->flags &= ~DKUF_NOAUTOSIZE;    /* enable autosize on unit */
                uptr->dptr->attach (uptr, filename);/* attach in autosize mode */
                uptr->flags &= ~UNIT_ATT;           /* mark as unattached for now */
                uptr->flags |= (saved_flags & DKUF_NOAUTOSIZE); /* restore autosize setting */
                sim_switches = saved_switches;
                ctx = (struct disk_context *)uptr->disk_ctx;/* attach repopulates the context */
                if (pctx != NULL)
                    *pctx = ctx;
                *f = *ctx->footer;                  /* copy updated footer */
                free (filename);
                }
            }
        free (ctx->footer);
        ctx->footer = f;
        if (NtoHl (f->MediaID) != 0)
            ctx->media_id = NtoHl (f->MediaID);
        ctx->highwater = (((t_offset)NtoHl (f->Highwater[0])) << 32) | ((t_offset)NtoHl (f->Highwater[1]));
        container_size -= sizeof (*f);
        sim_debug_unit (ctx->dbit, uptr, "Footer: %s - %s\n"
            "   Simulator:           %s\n"
            "   DriveType:           %s\n"
            "   SectorSize:          %u\n"
            "   SectorCount:         %u\n"
            "   TransferElementSize: %s\n"
            "   FooterVersion:       %u\n"
            "   AccessFormat:        %u\n"
            "   CreationTime:        %s",
            sim_uname (uptr), uptr->filename,
            f->CreatingSimulator, f->DriveType, NtoHl(f->SectorSize), NtoHl (f->SectorCount),
            _disk_tranfer_encoding (NtoHl (f->ElementEncodingSize)), f->FooterVersion, f->AccessFormat, f->CreationTime);
        if (f->DeviceName[0] != '\0')
            sim_debug_unit (ctx->dbit, uptr,
                "   DeviceName:          %s\n", (char *)f->DeviceName);
        if (f->DataWidth != 0)
            sim_debug_unit (ctx->dbit, uptr,
                "   DataWidth:           %d bits\n", NtoHl(f->DataWidth));
        if (f->MediaID != 0)
            sim_debug_unit (ctx->dbit, uptr,
                "   MediaID:             0x%08X (%s)\n", NtoHl(f->MediaID), sim_disk_decode_mediaid (NtoHl(f->MediaID)));
        sim_debug_unit (ctx->dbit, uptr,
            "   HighwaterSector:     %u\n", (uint32)(ctx->highwater/ctx->sector_size));
        }
    }
sim_debug_unit (ctx->dbit, uptr, "Container Size: %u sectors %u bytes each\n", (uint32)(container_size/ctx->sector_size), ctx->sector_size);
ctx->container_size = container_size;
return SCPE_OK;
}

static t_stat store_disk_footer (UNIT *uptr, const char *dtype)
{
DEVICE *dptr;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
struct stat statb;
struct simh_disk_footer *f;
struct simh_disk_footer *old_f = ctx->footer;
time_t now = time (NULL);
t_offset total_sectors;
t_offset highwater;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if (uptr->flags & UNIT_RO)
    return SCPE_RO;
if (sim_stat (uptr->filename, &statb))
    memset (&statb, 0, sizeof (statb));
f = (struct simh_disk_footer *)calloc (1, sizeof (*f));
f->AccessFormat = DK_GET_FMT (uptr);
total_sectors = (((t_offset)uptr->capac) * ctx->capac_factor * ((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)) / ctx->sector_size;
memcpy (f->Signature, "simh", 4);
f->FooterVersion = FOOTER_VERSION;
memset (f->CreatingSimulator, 0, sizeof (f->CreatingSimulator));
strlcpy ((char *)f->CreatingSimulator, sim_name, sizeof (f->CreatingSimulator));
memset (f->DriveType, 0, sizeof (f->DriveType));
strlcpy ((char *)f->DriveType, dtype, sizeof (f->DriveType));
f->SectorSize = NtoHl (ctx->sector_size);
f->SectorCount = NtoHl ((uint32)total_sectors);
f->ElementEncodingSize = NtoHl (ctx->xfer_encode_size);
memset (f->CreationTime, 0, sizeof (f->CreationTime));
strlcpy ((char*)f->CreationTime, ctime (&now), sizeof (f->CreationTime));
memset (f->DeviceName, 0, sizeof (f->DeviceName));
strlcpy ((char*)f->DeviceName, dptr->name, sizeof (f->DeviceName));
f->MediaID = (uptr->drvtyp != NULL) ? NtoHl (uptr->drvtyp->MediaId) : 0;
f->DataWidth = NtoHl (uptr->dptr->dwidth);
f->Geometry = NtoHl (sim_disk_drvtype_geometry (sim_disk_find_type (uptr, (char *)f->DriveType), (uint32)total_sectors));
highwater = sim_fsize_name_ex (uptr->filename);
/* Align Initial Highwater to a sector boundary */
highwater = ((highwater + ctx->sector_size - 1) / ctx->sector_size) * ctx->sector_size;
f->Highwater[0] = NtoHl ((uint32)(highwater >> 32));
f->Highwater[1] = NtoHl ((uint32)(highwater & 0xFFFFFFFF));
f->Checksum = NtoHl (eth_crc32 (0, f, sizeof (*f) - sizeof (f->Checksum)));
if ((old_f != NULL) &&
    (f->DataWidth           == old_f->DataWidth) &&
    (f->SectorSize          == old_f->SectorSize) &&
    (f->MediaID             == old_f->MediaID) &&
    (f->ElementEncodingSize == old_f->ElementEncodingSize) &&
    (f->Geometry            == old_f->Geometry) &&
    (f->FooterVersion       == old_f->FooterVersion)) /* Unchanged? */
    free(f);
else {
    free (ctx->footer);
    ctx->footer = f;
    switch (f->AccessFormat) {
        case DKUF_F_STD:                                    /* SIMH format */
            if (sim_fseeko ((FILE *)uptr->fileref, total_sectors * ctx->sector_size, SEEK_SET) == 0) {
                sim_fwrite (f, sizeof (*f), 1, (FILE *)uptr->fileref);
                fclose ((FILE *)uptr->fileref);
                sim_set_file_times (uptr->filename, statb.st_atime, statb.st_mtime);
                uptr->fileref = sim_fopen (uptr->filename, "rb+");
                }
            break;
        case DKUF_F_VHD:                                    /* VHD format */
            break;
        case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
            sim_os_disk_write (uptr, total_sectors * ctx->sector_size, (uint8 *)f, NULL, sizeof (*f));
            sim_os_disk_close_raw (uptr->fileref);
            sim_set_file_times (uptr->filename, statb.st_atime, statb.st_mtime);
            uptr->fileref = sim_os_disk_open_raw (uptr->filename, "rb+");
            break;
        default:
            break;
        }
    }
return SCPE_OK;
}

static t_stat update_disk_footer (UNIT *uptr)
{
DEVICE *dptr;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
struct stat statb;
struct simh_disk_footer *f;
t_offset total_sectors;
t_offset highwater;
t_offset footer_highwater;

if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if (uptr->flags & UNIT_RO)
    return SCPE_RO;
if (ctx == NULL)
    return SCPE_IERR;
if ((uptr->drvtyp != NULL) &&
    ((uptr->drvtyp->flags & DRVFL_DETAUTO) != 0) &&
    ((uptr->flags & DKUF_NOAUTOSIZE) == 0))
    store_disk_footer (uptr, uptr->drvtyp->name);
f = ctx->footer;
if (f == NULL)
    return SCPE_IERR;

footer_highwater = (((t_offset)NtoHl (f->Highwater[0])) << 32) | ((t_offset)NtoHl (f->Highwater[1]));
if (ctx->highwater <= footer_highwater)
    return SCPE_OK;

if (sim_stat (uptr->filename, &statb))
    memset (&statb, 0, sizeof (statb));
total_sectors = (((t_offset)uptr->capac) * ctx->capac_factor * ((dptr->flags & DEV_SECTORS) ? 512 : 1)) / ctx->sector_size;
highwater = ctx->highwater;
f->Highwater[0] = NtoHl ((uint32)(highwater >> 32));
f->Highwater[1] = NtoHl ((uint32)(highwater & 0xFFFFFFFF));
f->Checksum = NtoHl (eth_crc32 (0, f, sizeof (*f) - sizeof (f->Checksum)));
switch (DK_GET_FMT (uptr)) {
    case DKUF_F_STD:                                    /* SIMH format */
        if (sim_fseeko ((FILE *)uptr->fileref, total_sectors * ctx->sector_size, SEEK_SET) == 0) {
            sim_fwrite (f, sizeof (*f), 1, (FILE *)uptr->fileref);
            fclose ((FILE *)uptr->fileref);
            sim_set_file_times (uptr->filename, statb.st_atime, statb.st_mtime);
            uptr->fileref = sim_fopen (uptr->filename, "rb+");
            }
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        sim_os_disk_write (uptr, total_sectors * ctx->sector_size, (uint8 *)f, NULL, sizeof (*f));
        sim_os_disk_close_raw (uptr->fileref);
        sim_set_file_times (uptr->filename, statb.st_atime, statb.st_mtime);
        uptr->fileref = sim_os_disk_open_raw (uptr->filename, "rb+");
        break;
    default:
        break;
    }
return SCPE_OK;
}

t_stat sim_disk_attach (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_encode_size, t_bool dontchangecapac,
                        uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay)
{
return sim_disk_attach_ex (uptr, cptr, sector_size, xfer_encode_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay, NULL);
}

t_stat sim_disk_attach_ex (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_encode_size, t_bool dontchangecapac,
                           uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay, const char **drivetypes)
{
return sim_disk_attach_ex2 (uptr, cptr, sector_size, xfer_encode_size, dontchangecapac,
                            dbit, dtype, pdp11tracksize, completion_delay, drivetypes, 0);
}

t_stat sim_disk_attach_ex2 (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_encode_size, t_bool dontchangecapac,
                            uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay, const char **drivetypes,
                            size_t reserved_sectors)
{
struct disk_context *ctx;
DEVICE *dptr;
char tbuf[4*CBUFSIZE];
FILE *(*open_function)(const char *filename, const char *mode) = sim_fopen;
FILE *(*create_function)(const char *filename, t_offset desiredsize, DRVTYP *drvtyp) = NULL;
t_stat (*storage_function)(FILE *file, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom) = NULL;
t_bool created = FALSE, copied = FALSE, autosized = FALSE;
t_bool auto_format = FALSE;
UNIT *auptr;
DRVTYP *size_settable_drive_type = NULL;
t_offset container_size, filesystem_size, current_unit_size;
size_t tmp_size = 1;
DRVTYP *drvtypes = NULL;

if (uptr->flags & UNIT_DIS)                             /* disabled? */
    return SCPE_UDIS;
if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_NOATT;
if ((uptr->flags & DKUF_NOAUTOSIZE) != 0) {             /* unit autosize disabled? */
    dontchangecapac = TRUE;
    drivetypes = NULL;
    }
else {
    if (drivetypes == NULL) {                           /* Drive type list unspecified? */
        int i;

        drvtypes = (DRVTYP *)dptr->type_ctx;            /* Use device specific types (if any) */
        if (drvtypes != NULL) {
            for (i = 0; drvtypes[i].name; i++) {
                if (drvtypes[i].flags & DRVFL_SETSIZE) {
                    size_settable_drive_type = &drvtypes[i];
                    break;
                    }
                }
            }
        }
    }
switch (xfer_encode_size) {
    default:
        return sim_messagef (SCPE_ARG, "Unsupported transfer element size: %u\n", (uint32)xfer_encode_size);
    case 1: case 2: case 4: case 8:
        break;
    }
if ((sector_size % xfer_encode_size) != 0)
    return sim_messagef (SCPE_ARG, "Invalid sector size: %u - must be a multiple of the transfer element size %u\n", (uint32)sector_size, (uint32)xfer_encode_size);
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
    if (sim_disk_check_attached_container (cptr, &auptr))
        return sim_messagef (SCPE_ALATT, "'%s' is already attach to %s\n", cptr, sim_uname (auptr));
    vhd = sim_vhd_disk_create_diff (gbuf, cptr);
    if (vhd) {
        sim_vhd_disk_close (vhd);
        return sim_disk_attach (uptr, gbuf, sector_size, xfer_encode_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay);
        }
    return sim_messagef (SCPE_ARG, "Unable to create differencing VHD: %s - %s\n", gbuf, strerror (errno));
    }
if (sim_switches & SWMASK ('C')) {                      /* create new disk container & copy contents? */
    char gbuf[CBUFSIZE];
    const char *dest_fmt = ((DK_GET_FMT (uptr) == DKUF_F_AUTO) || (DK_GET_FMT (uptr) == DKUF_F_VHD)) ? "VHD" : "SIMH";
    FILE *dest;
    int saved_sim_switches = sim_switches;
    int32 saved_sim_quiet = sim_quiet;
    DRVTYP *saved_drvtyp = uptr->drvtyp;
    t_addr saved_capac = uptr->capac;
    DRVTYP *source_drvtyp = NULL;
    uint32 saved_noautosize = (uptr->flags & DKUF_NOAUTOSIZE);
    t_addr target_capac = saved_capac;
    t_addr source_capac;
    uint32 capac_factor;
    t_stat r;

    sim_switches = sim_switches & ~(SWMASK ('C'));
    cptr = get_glyph_nc (cptr, gbuf, 0);                /* get spec */
    if (*cptr == 0)                                     /* must be more */
        return sim_messagef (SCPE_2FARG, "Missing Copy container source specification\n");
    sim_switches |= SWMASK ('R') | SWMASK ('E');
    sim_quiet = TRUE;
    sim_disk_set_fmt (uptr, 0, "AUTO", NULL);   /* autodetect the source container format */
    uptr->flags &= ~DKUF_NOAUTOSIZE;            /* autosize the source container */
    /* First open the source of the copy operation */
    r = sim_disk_attach_ex (uptr, cptr, sector_size, xfer_encode_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay, NULL);
    sim_quiet = saved_sim_quiet;
    uptr->flags |= saved_noautosize;
    if (r != SCPE_OK) {
        sim_switches = saved_sim_switches;
        return sim_messagef (r, "%s: Cannot open copy source: %s - %s\n", sim_uname (uptr), cptr, sim_error_text (r));
        }
    source_drvtyp = uptr->drvtyp;
    source_capac = uptr->capac;
    if (saved_noautosize) {
        uptr->drvtyp = saved_drvtyp;
        uptr->capac = saved_capac;
        }
    sim_messagef (SCPE_OK, "%s: Creating new %s format '%s' %s disk container\n", sim_uname (uptr), dest_fmt, gbuf, uptr->drvtyp->name);
    sim_messagef (SCPE_OK, "%s: copying from '%s' %s%s%s\n", sim_uname (uptr), cptr, source_drvtyp ? "a " : "", source_drvtyp ? source_drvtyp->name : "", source_drvtyp ? " disk container." : "");
    capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */
    target_capac = uptr->capac;
    if (strcmp ("VHD", dest_fmt) == 0)
        dest = sim_vhd_disk_create (gbuf, ((t_offset)uptr->capac)*capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1), uptr->drvtyp);
    else
        dest = sim_fopen (gbuf, "wb+");
    if (!dest) {
        sim_disk_detach (uptr);
        return sim_messagef (r, "%s: Cannot create %s disk container '%s'\n", sim_uname (uptr), dest_fmt, gbuf);
        }
    else {
        uint8 *copy_buf = (uint8*) malloc (1024*1024);
        t_lba lba;
        t_seccnt sectors_per_buffer = (t_seccnt)((1024*1024)/sector_size);
        t_lba total_sectors = (t_lba)((target_capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
        t_seccnt sects = sectors_per_buffer;
        t_seccnt sects_read;

        if (!copy_buf) {
            if (strcmp ("VHD", dest_fmt) == 0)
                sim_vhd_disk_close (dest);
            else
                fclose (dest);
            (void)remove (gbuf);
            sim_disk_detach (uptr);
            return SCPE_MEM;
            }
        errno = 0;
        sim_messagef (SCPE_OK, "%s: Copying %u sectors each %u bytes in size\n", sim_uname (uptr), (uint32)total_sectors, (uint32)sector_size);
        if (source_capac > target_capac) {
            sim_messagef (SCPE_OK, "%s: The source %s%scontainer is %u sectors larger than\n",
                sim_uname (uptr), source_drvtyp ? source_drvtyp->name : "", source_drvtyp ? " " : "", (t_lba)(((source_capac - target_capac)*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1))));
            sim_messagef (SCPE_OK, "%s: the destination %s%sdisk container.\n",
                sim_uname (uptr), uptr->drvtyp ? uptr->drvtyp->name : "", uptr->drvtyp ? " " : "");
            sim_messagef (SCPE_OK, "%s: These additional sectors will be unavailable in the target drive\n", sim_uname (uptr));
            }
        for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects_read) {
            uptr->capac = source_capac;
            sects = sectors_per_buffer;
            if (lba + sects > total_sectors)
                sects = total_sectors - lba;
            r = sim_disk_rdsect (uptr, lba, copy_buf, &sects_read, sects);
            if ((r == SCPE_OK) && (sects_read > 0)) {
                uint32 saved_unit_flags = uptr->flags;
                FILE *saved_unit_fileref = uptr->fileref;
                t_seccnt sects_written;

                sim_disk_set_fmt (uptr, 0, dest_fmt, NULL);
                uptr->fileref = dest;
                uptr->capac = target_capac;
                r = sim_disk_wrsect (uptr, lba, copy_buf, &sects_written, sects_read);
                uptr->fileref = saved_unit_fileref;
                uptr->flags = saved_unit_flags;
                if (sects_read != sects_written)
                    r = SCPE_IOERR;
                sim_messagef (SCPE_OK, "%s: Copied %u/%u sectors.  %d%% complete.\r", sim_uname (uptr), (uint32)(lba + sects_read), (uint32)total_sectors, (int)((((float)lba)*100)/total_sectors));
                }
            }
        if ((errno == ERANGE) &&    /* If everything was read before the end of the disk */
            (sects_read == 0))
            r = SCPE_OK;            /* That's OK */
        if (r == SCPE_OK)
            sim_messagef (SCPE_OK, "\n%s: Copied %u sectors. Done.\n", sim_uname (uptr), (uint32)total_sectors);
        else
            sim_messagef (r, "\n%s: Error copying: %s.\n", sim_uname (uptr), sim_error_text (r));
        if ((r == SCPE_OK) && (sim_switches & SWMASK ('V'))) {
            uint8 *verify_buf = (uint8*) malloc (1024*1024);
            t_seccnt sects_read, verify_read;

            if (!verify_buf) {
                if (strcmp ("VHD", dest_fmt) == 0)
                    sim_vhd_disk_close (dest);
                else
                    fclose (dest);
                (void)remove (gbuf);
                free (copy_buf);
                sim_disk_detach (uptr);
                return SCPE_MEM;
                }
            for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects_read) {
                sim_messagef (SCPE_OK, "%s: Verified %u/%u sectors.  %d%% complete.\r", sim_uname (uptr), (uint32)lba, (uint32)total_sectors, (int)((((float)lba)*100)/total_sectors));
                uptr->capac = source_capac;
                sects = sectors_per_buffer;
                if (lba + sects > total_sectors)
                    sects = total_sectors - lba;
                r = sim_disk_rdsect (uptr, lba, copy_buf, &sects_read, sects);
                if (r == SCPE_OK) {
                    uint32 saved_unit_flags = uptr->flags;
                    FILE *saved_unit_fileref = uptr->fileref;

                    sim_disk_set_fmt (uptr, 0, dest_fmt, NULL);
                    uptr->fileref = dest;
                    uptr->capac = target_capac;
                    r = sim_disk_rdsect (uptr, lba, verify_buf, &verify_read, sects_read);
                    uptr->fileref = saved_unit_fileref;
                    uptr->flags = saved_unit_flags;
                    if (r == SCPE_OK) {
                        if ((sects_read != verify_read) ||
                            (0 != memcmp (copy_buf, verify_buf, verify_read*sector_size)))
                            r = SCPE_IOERR;
                        }
                    }
                if (r != SCPE_OK)
                    break;
                }
            if (!sim_quiet) {
                if (r == SCPE_OK)
                    sim_messagef (r, "\n%s: Verified %u sectors. Done.\n", sim_uname (uptr), (uint32)total_sectors);
                else {
                    t_lba i;
                    uint32 save_dctrl = dptr->dctrl;
                    FILE *save_sim_deb = sim_deb;

                    for (i = 0; i < sects_read; ++i)
                        if (0 != memcmp (copy_buf+i*sector_size, verify_buf+i*sector_size, sector_size))
                            break;
                    sim_printf ("\n%s: Verification Error on lbn %d.\n", sim_uname (uptr), lba+i);
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
        if (strcmp ("VHD", dest_fmt) == 0) {
            sim_vhd_disk_set_dtype (dest, (char *)uptr->drvtyp->name, sector_size, xfer_encode_size, uptr->drvtyp->MediaId, uptr->dptr->name, uptr->dptr->dwidth, uptr->drvtyp);
            sim_vhd_disk_close (dest);
            }
        else
            fclose (dest);
        sim_disk_detach (uptr);
        if (r == SCPE_OK) {
            created = TRUE;
            copied = TRUE;
            strlcpy (tbuf, gbuf, sizeof(tbuf)-1);
            cptr = tbuf;
            sim_disk_set_fmt (uptr, 0, dest_fmt, NULL);
            sim_switches = saved_sim_switches;
            }
        else
            return r;
        /* fall through and open/return the newly created & copied disk container */
        }
    }
else {
    if (sim_switches & SWMASK ('M')) {                 /* merge difference disk? */
        char gbuf[CBUFSIZE], *Parent = NULL;
        FILE *vhd;

        sim_switches = sim_switches & ~(SWMASK ('M'));
        get_glyph_nc (cptr, gbuf, 0);                  /* get spec */
        vhd = sim_vhd_disk_merge (gbuf, &Parent);
        if (vhd) {
            t_stat r;

            sim_vhd_disk_close (vhd);
            r = sim_disk_attach (uptr, Parent, sector_size, xfer_encode_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay);
            free (Parent);
            return r;
            }
        return SCPE_ARG;
        }
    }
if ((uptr->drvtyp != NULL) && (uptr->drvtyp->flags & DRVFL_RO))
    sim_switches |= SWMASK ('R');
if (sim_disk_check_attached_container (cptr, &auptr))
    return sim_messagef (SCPE_ALATT, "'%s' is already attach to %s\n", cptr, sim_uname (auptr));

switch (DK_GET_FMT (uptr)) {                            /* case on format */
    case DKUF_F_AUTO:                                   /* SIMH format */
        auto_format = TRUE;
        if (NULL != (uptr->fileref = sim_vhd_disk_open (cptr, "rb"))) { /* Try VHD */
            sim_disk_set_fmt (uptr, 0, "VHD", NULL);    /* set file format to VHD */
            sim_vhd_disk_close (uptr->fileref);         /* close vhd file*/
            uptr->fileref = NULL;
            open_function = sim_vhd_disk_open;
            break;
            }
        while (tmp_size < sector_size)
            tmp_size <<= 1;
        if (tmp_size ==  sector_size) {                     /* Power of 2 sector size can do RAW */
            if (NULL != (uptr->fileref = sim_os_disk_open_raw (cptr, "rb"))) {
                sim_disk_set_fmt (uptr, 0, "RAW", NULL);    /* set file format to RAW */
                sim_os_disk_close_raw (uptr->fileref);      /* close raw file*/
                open_function = sim_os_disk_open_raw;
                storage_function = sim_os_disk_info_raw;
                uptr->fileref = NULL;
                break;
                }
            }
        sim_disk_set_fmt (uptr, 0, "SIMH", NULL);       /* set file format to SIMH */
        open_function = sim_fopen;
        break;
    case DKUF_F_STD:                                    /* SIMH format */
        if (NULL != (uptr->fileref = sim_vhd_disk_open (cptr, "rb"))) { /* Try VHD first */
            sim_disk_set_fmt (uptr, 0, "VHD", NULL);    /* set file format to VHD */
            sim_vhd_disk_close (uptr->fileref);         /* close vhd file*/
            uptr->fileref = NULL;
            open_function = sim_vhd_disk_open;
            auto_format = TRUE;
            break;
            }
        open_function = sim_fopen;
        break;
    case DKUF_F_VHD:                                    /* VHD format */
        open_function = sim_vhd_disk_open;
        create_function = sim_vhd_disk_create;
        storage_function = sim_os_disk_info_raw;
        break;
    case DKUF_F_RAW:                                    /* Raw Physical Disk Access */
        if (NULL != (uptr->fileref = sim_vhd_disk_open (cptr, "rb"))) { /* Try VHD first */
            sim_disk_set_fmt (uptr, 0, "VHD", NULL);    /* set file format to VHD */
            sim_vhd_disk_close (uptr->fileref);         /* close vhd file*/
            uptr->fileref = NULL;
            open_function = sim_vhd_disk_open;
            auto_format = TRUE;
            break;
            }
        open_function = sim_os_disk_open_raw;
        storage_function = sim_os_disk_info_raw;
        break;
    default:
        return SCPE_IERR;
    }
uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char));/* alloc name buf */
uptr->disk_ctx = ctx = (struct disk_context *)calloc(1, sizeof(struct disk_context));
if ((uptr->filename == NULL) || (uptr->disk_ctx == NULL))
    return _err_return (uptr, SCPE_MEM);
strlcpy (uptr->filename, cptr, CBUFSIZE);               /* save name */
ctx->sector_size = (uint32)sector_size;                 /* save sector_size */
ctx->capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (quadword: 8, word: 2, byte: 1) */
ctx->xfer_encode_size = (uint32)xfer_encode_size;       /* save xfer_encode_size */
ctx->media_id = (uptr->drvtyp != NULL) ?
                    uptr->drvtyp->MediaId : 0;          /* save initial device type media id */
ctx->dptr = dptr;                                       /* save DEVICE pointer */
ctx->dbit = dbit;                                       /* save debug bit */
ctx->media_removed = 0;                                 /* default present */
ctx->initial_drvtyp = uptr->drvtyp;                     /* save original drive type */
ctx->initial_capac = uptr->capac;                       /* save original capacity */
sim_debug_unit (ctx->dbit, uptr, "sim_disk_attach(unit=%d,filename='%s')\n", (int)(uptr - ctx->dptr->units), uptr->filename);
ctx->auto_format = auto_format;                         /* save that we auto selected format */
ctx->storage_sector_size = (uint32)sector_size;         /* Default */
if ((sim_switches & SWMASK ('R')) ||                    /* read only? */
    ((uptr->flags & UNIT_RO) != 0)) {
    if (((uptr->flags & UNIT_ROABLE) == 0) &&           /* allowed? */
        ((uptr->flags & UNIT_RO) == 0))
        return sim_messagef (_err_return (uptr, SCPE_NORO), "%s: Read Only operation not allowed\n", /* no, error */
                                                        sim_uname (uptr));
    uptr->fileref = open_function (cptr, "rb");         /* open rd only */
    if (uptr->fileref == NULL)                          /* open fail? */
        return sim_messagef (_err_return (uptr, SCPE_OPENERR), "%s: Can't open '%s': %s\n", /* yes, error */
                                            sim_uname (uptr), cptr, strerror (errno));
    uptr->flags = uptr->flags | UNIT_RO;                /* set rd only */
    sim_messagef (SCPE_OK, "%s: Unit is read only\n", sim_uname (uptr));
    }
else {                                                  /* normal */
    uptr->fileref = open_function (cptr, "rb+");        /* open r/w */
    if (uptr->fileref == NULL) {                        /* open fail? */
        if ((errno == EROFS) || (errno == EACCES)) {    /* read only? */
            if ((uptr->flags & UNIT_ROABLE) == 0)       /* allowed? */
                return sim_messagef (_err_return (uptr, SCPE_NORO), "%s: Read Only operation not allowed\n", /* no, error */
                                                                sim_uname (uptr));
            uptr->fileref = open_function (cptr, "rb"); /* open rd only */
            if (uptr->fileref == NULL)                  /* open fail? */
                return sim_messagef (_err_return (uptr, SCPE_OPENERR), "%s: Can't open '%s': %s\n", /* yes, error */
                                                    sim_uname (uptr), cptr, strerror (errno));
            uptr->flags = uptr->flags | UNIT_RO;        /* set rd only */
            sim_messagef (SCPE_OK, "%s: Unit is read only\n", sim_uname (uptr));
            }
        else {                                          /* other error? */
            if ((sim_switches & SWMASK ('E')) ||        /* must exist? */
                (errno != ENOENT))                      /* or must not re-create? */
                return sim_messagef (_err_return (uptr, SCPE_OPENERR), "%s: Cannot open '%s' - %s\n",
                                     sim_uname (uptr), cptr, strerror (errno));
            if (create_function)
                uptr->fileref = create_function (cptr, ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1), uptr->drvtyp);/* create new file */
            else
                uptr->fileref = open_function (cptr, "wb+");/* open new file */
            if (uptr->fileref == NULL)                  /* open fail? */
                return sim_messagef (_err_return (uptr, SCPE_OPENERR), "%s: Cannot create '%s' - %s\n",
                                     sim_uname (uptr), cptr, strerror (errno));
            sim_messagef (SCPE_OK, "%s: Creating new file: %s\n", sim_uname (uptr), cptr);
            created = TRUE;
            }
        }                                               /* end if null */
    }                                                   /* end else */
(void)get_disk_footer (uptr, &ctx);
if ((DK_GET_FMT (uptr) == DKUF_F_VHD) || (ctx->footer)) {
    uint32 container_sector_size = 0, container_xfer_encode_size = 0, container_sectors = 0;
    char created_name[64];
    const char *container_dtype = ctx->footer ? (char *)ctx->footer->DriveType : sim_vhd_disk_get_dtype (uptr->fileref, &container_sector_size, &container_xfer_encode_size, created_name, NULL, NULL, NULL, NULL);

    if (ctx->footer) {
        container_sector_size = NtoHl (ctx->footer->SectorSize);
        container_sectors = NtoHl (ctx->footer->SectorCount);
        xfer_encode_size = NtoHl (ctx->footer->ElementEncodingSize);
        strlcpy (created_name, (char *)ctx->footer->CreatingSimulator, sizeof (created_name));
        }
    if (dtype) {
        char cmd[64];
        t_stat r = SCPE_OK;

        if ((strcmp (container_dtype, dtype) == 0) ||
            (((container_sector_size == 0) || (container_sector_size == ctx->sector_size) ||
              ((ctx->data_ileave == 0) && (ctx->sector_size % container_sector_size) == 0)) &&
             ((container_xfer_encode_size == 0) || (container_xfer_encode_size == ctx->xfer_encode_size)))) {
            if (strcmp (container_dtype, dtype) != 0) {
                t_bool can_autosize = ((drivetypes != NULL) || (drvtypes != NULL) || (!dontchangecapac));

                if (can_autosize) {
                    int32 saved_show_message = sim_show_message;
                    int32 saved_switches = sim_switches;
                    uint32 saved_RO_flags = (uptr->flags & UNIT_RO);

                    snprintf (cmd, sizeof (cmd), "%s %s", sim_uname (uptr), container_dtype);
                    set_cmd (0, "NOMESSAGE");
                    r = set_cmd (0, cmd);
                    uptr->flags |= saved_RO_flags;    /* While autosizing, retain the unit Read Only state */
                    sim_show_message = saved_show_message;
                    if (r != SCPE_OK) {
                        if (size_settable_drive_type != NULL) {
                            sim_switches |= SWMASK ('L');   /* LBN size */
                            snprintf (cmd, sizeof (cmd), "%s %s=%u", sim_uname (uptr), size_settable_drive_type->name, (uint32)(ctx->container_size / ctx->sector_size));
                            r = set_cmd (0, cmd);
                            uptr->flags |= saved_RO_flags;  /* restore unit Read Only state after size adjustment */
                            }
                        else
                            r = sim_messagef (SCPE_INCOMPDSK, "%s: Cannot set to drive type %s\n", sim_uname (uptr), container_dtype);
                        }
                    sim_switches = saved_switches;
                    }
                current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);
                if (ctx->container_size > current_unit_size) {
                    t_lba current_unit_sectors = (t_lba)((dptr->flags & DEV_SECTORS) ? uptr->capac : (uptr->capac*ctx->capac_factor)/ctx->sector_size);

                    if ((uptr->flags & UNIT_RO) != 0)                   /* Not Opening read only? */
                        r = sim_messagef (SCPE_OK, "%s: Read Only access to inconsistent drive type allowed\n", sim_uname (uptr));
                    else {
                        r = sim_messagef (SCPE_INCOMPDSK, "%s: Too large container having %u sectors, drive has: %u sectors\n", sim_uname (uptr), container_sectors, current_unit_sectors);
                        if ((uptr->flags & UNIT_ROABLE) == 0) {
                            r = sim_messagef (SCPE_INCOMPDSK, "%s: Drive type doesn't support Read Only attach\n", sim_uname (uptr));
                            }
                        else {
                            r = sim_messagef (SCPE_INCOMPDSK, "%s: '%s' can only be attached Read Only\n", sim_uname (uptr), cptr);
                            }
                        }
                    }
                }
            else { /* Type already matches, Need to confirm compatibility */
                t_lba current_unit_sectors = (t_lba)((dptr->flags & DEV_SECTORS) ? uptr->capac : (uptr->capac*ctx->capac_factor)/ctx->sector_size);

                if ((container_sector_size != 0) && (sector_size != container_sector_size))
                    r = sim_messagef (SCPE_OPENERR, "%s: Incompatible Container Sector Size %d\n", sim_uname (uptr), container_sector_size);
                else {
                    if (dontchangecapac &&
                        ((((t_lba)(ctx->container_size/sector_size) > current_unit_sectors)) ||
                         ((container_sectors != 0) && (container_sectors != current_unit_sectors)))) {
                         r = sim_messagef (SCPE_OK, "%s: Container has %u sectors, drive has: %u sectors\n", sim_uname (uptr), container_sectors, current_unit_sectors);
                         if (container_sectors != 0)
                             r = sim_messagef (SCPE_INCOMPDSK, "%s: %s container created by the %s simulator is incompatible with the %s device on the %s simulator\n", sim_uname (uptr), container_dtype, created_name, uptr->dptr->name, sim_name);
                         else
                             r = sim_messagef (SCPE_INCOMPDSK, "%s: disk container %s is incompatible with the %s device\n", sim_uname (uptr), uptr->filename, uptr->dptr->name);
                         if ((uptr->flags & UNIT_RO) != 0)
                             r = sim_messagef (SCPE_OK, "%s: Read Only access to incompatible %s container '%s' allowed\n", sim_uname (uptr), container_dtype, uptr->filename);
                         if (container_sectors < current_unit_sectors) {
                             r = sim_messagef (SCPE_BARE_STATUS (r), "%s: Since the container is smaller than the drive, this might be useful:\n", sim_uname (uptr));
                             r = sim_messagef (SCPE_BARE_STATUS (r), "%s:   sim> ATTACH %s -C New-%s %s\n", sim_uname (uptr), sim_uname (uptr), uptr->filename, uptr->filename);
                             }
                        }
                    }
                if ((r == SCPE_OK) && ((uptr->drvtyp == NULL) || (strcmp (container_dtype, uptr->drvtyp->name) != 0))) {
                    int32 saved_show_message = sim_show_message;

                    snprintf (cmd, sizeof (cmd), "%s %s", sim_uname (uptr), container_dtype);
                    set_cmd (0, "NOMESSAGE");
                    set_cmd (0, cmd);
                    sim_show_message = saved_show_message;
                    }
                }
            if (r == SCPE_OK)
                autosized = TRUE;
            }
        else
            r = sim_messagef (SCPE_INCOMPDSK, "%s: %s container created by the %s simulator is incompatible with the %s device on the %s simulator\n", sim_uname (uptr), container_dtype, created_name, uptr->dptr->name, sim_name);
        if (r != SCPE_OK) {
            uptr->flags |= UNIT_ATT;
            sim_disk_detach (uptr);                         /* report error now */
            sprintf (cmd, "%s%d %s", dptr->name, (int)(uptr-dptr->units), dtype);/* restore original dtype */
            set_cmd (0, cmd);
            return r;
            }
        }
    }
uptr->flags |= UNIT_ATT;
uptr->pos = 0;

/* Get Device attributes if they are available */
if (storage_function)
    storage_function (uptr->fileref, &ctx->storage_sector_size, &ctx->removable, &ctx->is_cdrom);

if ((created) && (!copied)) {
    t_stat r = SCPE_OK;
    uint8 *secbuf = (uint8 *)calloc (128, ctx->sector_size);     /* alloc temp sector buf */

    /*
       On a newly created disk, we write zeros to the whole disk.
       This serves 3 purposes:
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
    if (pdp11tracksize)
        sim_disk_pdp11_bad_block (uptr, pdp11tracksize, sector_size/sizeof(uint16));
    }
if (sim_switches & SWMASK ('I')) {                  /* Initialize To Sector Address */
    t_stat r = SCPE_OK;
    size_t init_buf_size = 1024*1024;
    uint8 *init_buf = (uint8*) calloc (init_buf_size, 1);
    t_lba lba, sect;
    uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */
    t_seccnt sectors_per_buffer = (t_seccnt)((init_buf_size)/sector_size);
    t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? 512 : 1)));
    t_seccnt sects = sectors_per_buffer;

    if (!init_buf) {
        sim_disk_detach (uptr);                     /* report error now */
        (void)remove (cptr);
        return SCPE_MEM;
        }
    if (sector_size < ctx->storage_sector_size)
        sectors_per_buffer = 1;                     /* exercise more of write logic with small I/Os */
    sim_messagef (SCPE_OK, "Initializing %u sectors each %u bytes with the sector address\n", (uint32)total_sectors, (uint32)sector_size);
    for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects) {
        t_seccnt sects_written;

        sects = sectors_per_buffer;
        if (lba + sects > total_sectors)
            sects = total_sectors - lba;
        for (sect = 0; sect < sects; sect++) {
            t_lba offset;

            if (xfer_encode_size <= sizeof (uint32)) {
                for (offset = 0; offset < sector_size; offset += sizeof(uint32))
                    *((uint32 *)&init_buf[sect*sector_size + offset]) = (uint32)(lba + sect);
                }
            else {
                for (offset = 0; offset < sector_size; offset += xfer_encode_size)
                    *((t_uint64 *)&init_buf[sect*sector_size + offset]) = (t_uint64)(lba + sect);
                }
            }
        r = sim_disk_wrsect (uptr, lba, init_buf, &sects_written, sects);
        if ((r != SCPE_OK) || (sects != sects_written)) {
            free (init_buf);
            sim_disk_detach (uptr);                         /* report error now */
            (void)remove (cptr);                            /* remove the created file */
            return sim_messagef (SCPE_OPENERR, "Error initializing each sector with its address: %s\n",
                                               (r == SCPE_OK) ? sim_error_text (r) : "sectors written not what was requested");
            }
        sim_messagef (SCPE_OK, "%s: Set To Sector Address %u/%u sectors.  %d%% complete.\r", sim_uname (uptr), (uint32)(lba + sects_written), (uint32)total_sectors, (int)((((float)lba)*100)/total_sectors));
        }
    sim_messagef (SCPE_OK, "%s: Initialized To Sector Address %u sectors.  100%% complete.       \n", sim_uname (uptr), (uint32)total_sectors);
    free (init_buf);
    }
if (sim_switches & SWMASK ('K')) {
    t_stat r = SCPE_OK;
    t_lba lba, sect;
    uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (word: 2, byte: 1) */
    t_seccnt sectors_per_buffer = (t_seccnt)((1024*1024)/sector_size);
    t_lba total_sectors = (t_lba)((uptr->capac*capac_factor)/(sector_size/((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)));
    t_seccnt sects = sectors_per_buffer;
    t_seccnt sects_verify;
    uint8 *verify_buf = (uint8*) malloc (1024*1024);

    if (!verify_buf) {
        sim_disk_detach (uptr);                     /* report error now */
        return SCPE_MEM;
        }
    if (sector_size < ctx->storage_sector_size)
        sectors_per_buffer = 1;                     /* exercise more of read logic with small I/Os */
    for (lba = 0; (lba < total_sectors) && (r == SCPE_OK); lba += sects_verify) {
        sects = sectors_per_buffer;
        if (lba + sects > total_sectors)
            sects = total_sectors - lba;
        r = sim_disk_rdsect (uptr, lba, verify_buf, &sects_verify, sects);
        if (r == SCPE_OK) {
            if (sects != sects_verify)
                sim_printf ("\n%s: Verification Error when reading lbn %d(0x%X) of %d(0x%X) Requested %u sectors, read %u sectors.\n",
                            sim_uname (uptr), (int)lba, (int)lba, (int)total_sectors, (int)total_sectors, sects, sects_verify);
            for (sect = 0; sect < sects_verify; sect++) {
                t_lba offset;
                t_bool sect_error = FALSE;

                if (xfer_encode_size <= sizeof (uint32)) {
                    for (offset = 0; offset < sector_size; offset += sizeof(uint32)) {
                        if (*((uint32 *)&verify_buf[sect*sector_size + offset]) != (uint32)(lba + sect)) {
                            sect_error = TRUE;
                            break;
                            }
                        }
                    }
                else {
                    for (offset = 0; offset < sector_size; offset += xfer_encode_size) {
                        if (*((t_uint64 *)&verify_buf[sect*sector_size + offset]) != (t_uint64)(lba + sect)) {
                            sect_error = TRUE;
                            break;
                            }
                        }
                    }
                if (sect_error) {
                    uint32 save_dctrl = dptr->dctrl;
                    FILE *save_sim_deb = sim_deb;

                    sim_printf ("\n%s: Verification Error on lbn %d(0x%X) of %d(0x%X).\n", sim_uname (uptr), (int)(lba+sect), (int)(lba+sect), (int)total_sectors, (int)total_sectors);
                    dptr->dctrl = 0xFFFFFFFF;
                    sim_deb = stdout;
                    sim_disk_data_trace (uptr, verify_buf+sect*sector_size, lba+sect, sector_size,    "Found", TRUE, 1);
                    dptr->dctrl = save_dctrl;
                    sim_deb = save_sim_deb;
                    }
                }
            }
        sim_messagef (SCPE_OK, "%s: Verified Sector Address %u/%u sectors.  %d%% complete.\r", sim_uname (uptr), (uint32)lba, (uint32)total_sectors, (int)((((float)lba)*100)/total_sectors));
        }
    sim_messagef (SCPE_OK, "%s: Verified Sector Address %u sectors.  100%% complete.         \n", sim_uname (uptr), (uint32)lba);
    free (verify_buf);
    uptr->dynflags |= UNIT_DISK_CHK;
    }

if (get_disk_footer (uptr, &ctx) != SCPE_OK) {
    sim_disk_detach (uptr);
    return SCPE_OPENERR;
    }
filesystem_size = get_filesystem_size (uptr, NULL);
if (filesystem_size != (t_offset)-1)
    filesystem_size += reserved_sectors * sector_size;
container_size = sim_disk_size (uptr);
if ((filesystem_size == (t_offset)-1) &&
    (ctx->footer != NULL))                      /* The presence of metadata means we already */
    filesystem_size = ctx->container_size;      /* know the interesting disk size */
current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);
if (container_size && (container_size != (t_offset)-1) &&
    (container_size != current_unit_size) &&
    (filesystem_size != current_unit_size)) { /* need to autosize */
    t_addr saved_capac = uptr->capac;
    const char *saved_drvtyp = (uptr->drvtyp != NULL) ? uptr->drvtyp->name : dtype;
    char cmd[CBUFSIZE];

    if (!created && (ctx->footer == NULL) && (filesystem_size == (t_offset)-1)) {
        if (container_size != current_unit_size) {  /* container doesn't precisely matches unit size */
            sim_messagef (SCPE_OK, "%s: Amount of data in use in disk container '%s' cannot be determined, skipping autosizing\n", sim_uname (uptr), cptr);
            if (container_size > current_unit_size) {
                t_stat r = SCPE_FSSIZE;
                char *capac1;

                uptr->capac = (t_addr)(container_size/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)));
                capac1 = strdup (sprint_capac (dptr, uptr));
                uptr->capac = saved_capac;
                r = sim_messagef (r, "%s: The disk container '%s' is larger than simulated device (%s > %s)\n",
                                    sim_uname (uptr), cptr, capac1, sprint_capac (dptr, uptr));
                free (capac1);
                sim_disk_detach (uptr);
                return r;
                }
            }
        /* Fall through and skip autosizing for a container we can't determine anything about but happens to be the same size as the unit */
        }
    else {      /* Active autosizing */
        /* Prefer capacity change over drive type change for the same drive type container */
        if ((!dontchangecapac) &&
            (((uptr->flags & UNIT_RO) != 0) ||
             ((ctx->footer != NULL) &&
              ((uptr->drvtyp == NULL) ||
               (strcasecmp (uptr->drvtyp->name, (char *)ctx->footer->DriveType) == 0))))) { /* autosize by changing capacity */
            if (filesystem_size != (t_offset)-1) {              /* Known file system data size AND */
                if (filesystem_size >= container_size) {        /*    Data size >= container size? */
                    container_size = filesystem_size +          /*       Use file system data size */
                                 (pdp11tracksize * sector_size);/*       plus any bad block data beyond the file system */
                    autosized = TRUE;
                    }
                }
            else {                                              /* Unrecognized file system */
                if (container_size < current_unit_size)         /*     Use MAX of container or current device size */
                    if ((DKUF_F_VHD != DK_GET_FMT (uptr)) &&    /*     when size can be expanded */
                        (0 == (uptr->flags & UNIT_RO))) {
                        container_size = current_unit_size;     /*     Use MAX of container or current device size */
                        autosized = TRUE;
                        }
                }
            uptr->capac = (t_addr)(container_size/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1)));  /* update current size */
            current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);
            }
        else {                              /* autosize by potentially changing drive type */
            if ((drivetypes != NULL) || (drvtypes != NULL)) {   /* Available drive type list? */
                const char **saved_drivetypes = drivetypes;
                DRVTYP *saved_drvtypes = drvtypes;
                const char *drive;

                /* Walk through all potential drive types (if any) until we find one at least the right size */
                for (drive = (drivetypes != NULL) ? *drivetypes : drvtypes->name;
                     drive != NULL;
                     drive = (drivetypes != NULL) ? *++drivetypes : (++drvtypes)->name) {
                    t_stat st;
                    int32 saved_switches = sim_switches;
                    uint32 saved_RO = (uptr->flags & UNIT_RO);

                    if ((drvtypes != NULL) &&
                        (DRVFL_GET_IFTYPE(drvtypes) == DRVFL_TYPE_SCSI) && (drvtypes->devtype == SCSI_TAPE))
                        continue;
                    uptr->flags &= ~UNIT_ATT;       /* temporarily mark as un-attached */
                    if ((size_settable_drive_type != NULL) &&
                        (strcasecmp (size_settable_drive_type->name, drive) == 0))
                        snprintf (cmd, sizeof (cmd), "%s %s=%u", sim_uname (uptr), drive, (uint32)(filesystem_size / size_settable_drive_type->sectsize));
                    else
                        snprintf (cmd, sizeof (cmd), "%s %s", sim_uname (uptr), drive);
                    sim_switches |= SWMASK ('L');
                    st = set_cmd (0, cmd);
                    sim_switches = saved_switches;  /* restore switches */
                    uptr->flags |= UNIT_ATT;        /* restore attached indicator */
                    uptr->flags &= ~UNIT_RO;        /* restore RO indicator */
                    uptr->flags |= saved_RO;
                    if (st == SCPE_OK)
                        current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);
                    if (filesystem_size != (t_offset)-1) {  /* File System found? */
                        if (current_unit_size >= filesystem_size)
                            break;
                        }
                    else {                                  /* No file system case */
                        if (current_unit_size == container_size) {
                            autosized = TRUE;
                            break;
                            }
                        }
                    }
                drivetypes = saved_drivetypes;
                drvtypes = saved_drvtypes;
                }
            if (filesystem_size <= current_unit_size)
                autosized = TRUE;
            }
        /* After potentially changing the drive type, are we OK now? */
        if (dontchangecapac &&
            (filesystem_size != (t_offset)-1) &&
            (filesystem_size > current_unit_size)) {
            t_stat r = ((uptr->flags & UNIT_RO) == 0) ? SCPE_FSSIZE : SCPE_OK;
            char *capac1;

            uptr->capac = (t_addr)(filesystem_size/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)));
            capac1 = strdup (sprint_capac (dptr, uptr));
            uptr->capac = saved_capac;
            r = sim_messagef (r, "%s: The file system in the disk container %s is larger than simulated device (%s > %s)\n",
                                sim_uname (uptr), cptr, capac1, sprint_capac (dptr, uptr));
            free (capac1);
            if ((uptr->flags & UNIT_RO) == 0)
                sim_disk_detach (uptr);
            sprintf (cmd, "%s %s", sim_uname (uptr), saved_drvtyp);
            set_cmd (0, cmd);
            return r;
            }
        }
    if ((container_size != current_unit_size)) {
        if (container_size <= current_unit_size) {
            if (DKUF_F_VHD == DK_GET_FMT (uptr)){
                t_stat r = SCPE_INCOMPDSK;
                const char *container_dtype = ctx->footer ? (const char *)ctx->footer->DriveType : "";
                char *capac1;

                uptr->capac = (t_addr)(container_size/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)));
                capac1 = strdup (sprint_capac (dptr, uptr));
                uptr->capac = saved_capac;
                r = sim_messagef (r, "%s: non expandable %s%sdisk container '%s' is smaller than simulated device (%s < %s)\n",
                                    sim_uname (uptr), container_dtype, (*container_dtype != '\0') ? " " : "", cptr, capac1, sprint_capac (dptr, uptr));
                free (capac1);
                sim_disk_detach (uptr);
                return r;
                }
            }
        else { /* (container_size > current_unit_size) */
            if (0 == (uptr->flags & UNIT_RO)) {
                t_stat r = SCPE_OK;
                int32 saved_switches = sim_switches;
                const char *container_dtype;
                char *capac1;

                uptr->capac = (t_addr)(container_size/(ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1)));
                capac1 = strdup (sprint_capac (dptr, uptr));
                uptr->capac = saved_capac;
                sim_disk_detach (uptr);
                sim_switches = SWMASK ('R');
                r = sim_disk_attach_ex (uptr, cptr, sector_size, xfer_encode_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay, NULL);
                container_dtype = ctx->footer ? (const char *)ctx->footer->DriveType : "";
                sim_switches = saved_switches;
                if (r == SCPE_OK)
                    r = sim_messagef (SCPE_OK, "%s: %s%sdisk container '%s' is larger than simulated device (%s > %s) Read Only Forced\n",
                            sim_uname (uptr), container_dtype, (*container_dtype != '\0') ? " " : "", cptr,
                            capac1, sprint_capac (dptr, uptr));
                free (capac1);
                return r;
                }
            }
        }
    }

if ((uptr->flags & UNIT_RO) == 0) {     /* Opened Read/Write? */
    t_bool isreadonly;
    int32 saved_quiet = sim_quiet;

    sim_quiet = 1;
    get_filesystem_size (uptr, &isreadonly);
    if (isreadonly) {                     /* ReadOny File System? */
        sim_disk_detach (uptr);
        sim_switches |= SWMASK ('R');
        sim_disk_attach_ex2 (uptr, cptr, sector_size, xfer_encode_size, dontchangecapac,
                            dbit, dtype, pdp11tracksize, completion_delay, drivetypes,
                            reserved_sectors);
        }
    sim_quiet = saved_quiet;
    }
if (dtype && ((uptr->drvtyp == NULL) ? TRUE : ((uptr->drvtyp->flags & DRVFL_DETAUTO) == 0)) && ((uptr->flags & DKUF_NOAUTOSIZE) == 0) &&
    (created                                                                                  ||
     ((ctx->footer == NULL) && (autosized || (current_unit_size == container_size)))          ||
     (!created && (ctx->container_size == 0) && (ctx->footer == NULL))))
    store_disk_footer (uptr, (uptr->drvtyp == NULL) ? dtype : uptr->drvtyp->name);

#if defined (SIM_ASYNCH_IO)
sim_disk_set_async (uptr, completion_delay);
#endif
uptr->io_flush = _sim_disk_io_flush;

if (uptr->flags & UNIT_BUFABLE) {                       /* buffer in memory? */
    t_seccnt sectsread;
    t_stat r = SCPE_OK;

    if (uptr->flags & UNIT_MUSTBUF) {                   /* dyn alloc? */
        uptr->filebuf = calloc ((size_t)current_unit_size,
                                    ctx->xfer_encode_size);       /* allocate */
        uptr->filebuf2 = calloc ((size_t)current_unit_size,
                                    ctx->xfer_encode_size);       /* allocate copy */
        if ((uptr->filebuf == NULL) ||                  /* either failed? */
            (uptr->filebuf2 == NULL)) {
            sim_disk_detach (uptr);
            return SCPE_MEM;                            /* memory allocation error */
            }
        }
    sim_messagef (SCPE_OK, "%s: buffering file in memory\n", sim_uname (uptr));
    r = sim_disk_rdsect (uptr, 0, (uint8 *)uptr->filebuf, &sectsread, (t_seccnt)(ctx->container_size / ctx->sector_size));
    if (r != SCPE_OK)
        return sim_disk_detach (uptr);
    uptr->hwmark = (sectsread * ctx->sector_size) / ctx->xfer_encode_size;
    memcpy (uptr->filebuf2, uptr->filebuf, (size_t)ctx->container_size);/* save initial contents */
    uptr->flags |= UNIT_BUF;                            /* mark as buffered */
    if ((uptr->hwmark * ctx->xfer_encode_size) < current_unit_size)/* Make sure the container on disk has all the data (zero fill as needed) */
        sim_disk_wrsect (uptr, 0, (uint8 *)uptr->filebuf, NULL, (t_seccnt)(current_unit_size / ctx->sector_size));
    }
if (DK_GET_FMT (uptr) != DKUF_F_STD)
    uptr->dynflags |= UNIT_NO_FIO;
return SCPE_OK;
}

t_stat sim_disk_detach (UNIT *uptr)
{
struct disk_context *ctx;
int (*close_function)(FILE *f);
FILE *fileref;
t_bool auto_format;
char *autozap_filename = NULL;

if (uptr == NULL)
    return SCPE_IERR;
sim_cancel (uptr);
if (!(uptr->flags & UNIT_ATT))
    return SCPE_UNATT;

ctx = (struct disk_context *)uptr->disk_ctx;
fileref = uptr->fileref;            /* save local copy used after unit cleanup */

sim_debug_unit (ctx->dbit, uptr, "sim_disk_detach(unit=%d,filename='%s')\n", (int)(uptr - ctx->dptr->units), uptr->filename);

/* Save close function to call after unit cleanup */
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

if ((uptr->flags & UNIT_BUF) && (uptr->filebuf)) {
    uint32 cap = (uptr->hwmark + uptr->dptr->aincr - 1) / uptr->dptr->aincr;
    t_offset current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((uptr->dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);

    if (((uptr->flags & UNIT_RO) == 0) &&
        (memcmp (uptr->filebuf, uptr->filebuf2, (size_t)current_unit_size) != 0)) {
        sim_messagef (SCPE_OK, "%s: writing buffer to file: %s\n", sim_uname (uptr), uptr->filename);
        sim_disk_wrsect (uptr, 0, (uint8 *)uptr->filebuf, NULL, (cap + ctx->sector_size - 1) / ctx->sector_size);
        }
    uptr->flags = uptr->flags & ~UNIT_BUF;
    }
free (uptr->filebuf);                                   /* free buffers */
uptr->filebuf = NULL;
free (uptr->filebuf2);
uptr->filebuf2 = NULL;

update_disk_footer (uptr);                              /* Update meta data if highwater has changed */
fileref = uptr->fileref;                                /* update local copy used after unit cleanup */

auto_format = ctx->auto_format;                         /* save for update after unit cleanup */

if (uptr->io_flush)
    uptr->io_flush (uptr);                              /* flush buffered data */

sim_disk_clr_async (uptr);

uptr->flags &= ~(UNIT_ATT | UNIT_RO);
uptr->dynflags &= ~(UNIT_NO_FIO | UNIT_DISK_CHK);
if ((uptr->flags & DKUF_AUTOZAP) != 0)
    autozap_filename = strdup (uptr->filename);
free (uptr->filename);
uptr->filename = NULL;
uptr->fileref = NULL;
free (ctx->footer);
ctx->footer = NULL;
uptr->drvtyp = ctx->initial_drvtyp;                     /* restore drive type */
uptr->capac = ctx->initial_capac;                       /* restore drive size */
free (uptr->disk_ctx);
uptr->disk_ctx = NULL;
uptr->io_flush = NULL;

if (auto_format)
    sim_disk_set_fmt (uptr, 0, "AUTO", NULL);           /* restore file format */

if (close_function (fileref) == EOF) {
    free (autozap_filename);
    return SCPE_IOERR;
    }
if (autozap_filename) {
    t_bool saved_sim_show_message = sim_show_message;

    sim_show_message = FALSE;
    sim_disk_info_cmd (1, autozap_filename);
    free (autozap_filename);
    sim_show_message = saved_sim_show_message;
    }
return SCPE_OK;
}

t_stat sim_disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
static struct example_fields {
    const char *dname;
    const char *dtype;
    const char *dsize;
    const char *dtype2;
    const char *dsize2;
    const char *dtype3;
    const char *dsize3;
    const char *dtype4;
    const char *dsize4;
    } ex_data[] = {
        {"RQ", "RD54", "159MB", "RX50", "409KB", "RA81", "456MB", "RA92", "1505MB"},
        {"RP", "RM03",  "33MW", "RM03",  "33MW", "RP07", "258MW", "RM03",  "15MW"},
        {"RP", "RM03",  "39MW", "RM03",  "39MW", "RP07", "110MW", "RM03",  "15MW"},
    };
struct example_fields *ex = &ex_data[0];

if (strcmp (dptr->name, "RP") == 0)
    ex = &ex_data[1];
if (strstr (sim_name, "-10")) {
    ex = &ex_data[2];
    if (strstr (sim_name, "PDP") == NULL)
        ex->dname = "RPA";
    }

fprintf (st, "%s Disk Attach Help\n\n", dptr->name);

fprintf (st, "Disk container files can be one of several different types:\n\n");
//if (strstr (sim_name, "-10") == NULL) {
    fprintf (st, "    SIMH   A disk is an unstructured binary file of the size appropriate\n");
    fprintf (st, "           for the disk drive being simulated accessed by C runtime APIs\n");
    fprintf (st, "    VHD    Virtual Disk format which is described in the \"Microsoft\n");
    fprintf (st, "           Virtual Hard Disk (VHD) Image Format Specification\".  The\n");
    fprintf (st, "           VHD implementation includes support for 1) Fixed (Preallocated)\n");
    fprintf (st, "           disks, 2) Dynamically Expanding disks, and 3) Differencing disks.\n");
    fprintf (st, "    RAW    platform specific access to physical disk or CDROM drives\n\n");
//    }
//else {
//    fprintf (st, "   SIMH     A disk is an unstructured binary file of 64bit integers\n"
//                 "            access by C runtime APIs\n");
//    fprintf (st, "   VHD      A disk is an unstructured binary file of 64bit integers\n"
//                 "            contained in a VHD container\n");
//    fprintf (st, "   RAW      A disk is an unstructured binary file of 64bit integers\n"
//                 "            accessed by direct read/write APIs\n");
//    fprintf (st, "   DLD9     Packed little endian words (Compatible with KLH10)\n");
//    fprintf (st, "   DBD9     Packed big endian words (Compatible with KLH10)\n");
//    fprintf (st, "   VHD-DLD9 Packed little endian words stored in a VHD container\n");
//    fprintf (st, "   VHD-DBD9 Packed big endian words stored in a VHD container\n\n");
//    }
fprintf (st, "Virtual (VHD) Disk  support conforms to the \"Virtual Hard Disk Image Format\n");
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

if (dptr->numunits > 1) {
    uint32 i, attachable_count = 0, out_count = 0, skip_count;

    for (i=0; i < dptr->numunits; ++i)
        if ((dptr->units[i].flags & UNIT_ATTABLE) &&
            !(dptr->units[i].flags & UNIT_DIS))
            ++attachable_count;
    for (i=0; (i < dptr->numunits) && (out_count < 2); ++i)
        if ((dptr->units[i].flags & UNIT_ATTABLE) &&
            !(dptr->units[i].flags & UNIT_DIS)) {
            fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", sim_uname (&dptr->units[i]));
            ++out_count;
            }
    if (attachable_count > 4) {
        fprintf (st, "       .\n");
        fprintf (st, "       .\n");
        fprintf (st, "       .\n");
        }
    skip_count = attachable_count - 2;
    for (i=0; i < dptr->numunits; ++i)
        if ((dptr->units[i].flags & UNIT_ATTABLE) &&
            !(dptr->units[i].flags & UNIT_DIS)) {
            if (skip_count == 0)
                fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", sim_uname (&dptr->units[i]));
            else
                --skip_count;
            }
    }
else
    fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", dptr->name);
fprintf (st, "\n%s attach command switches\n", dptr->name);
fprintf (st, "    -R          Attach Read Only.\n");
fprintf (st, "    -E          Must Exist (if not specified an attempt to create the indicated\n");
fprintf (st, "                disk container will be attempted).\n");
fprintf (st, "    -F          Open the indicated disk container in a specific format (default\n");
fprintf (st, "                is to autodetect VHD defaulting to RAW if the indicated\n");
fprintf (st, "                container is not a VHD).\n");
fprintf (st, "    -I          Initialize newly created disk so that each sector contains its\n");
fprintf (st, "                sector address\n");
fprintf (st, "    -K          Verify that the disk contents contain the sector address in each\n");
fprintf (st, "                sector.  Whole disk checked at attach time and each sector is\n");
fprintf (st, "                checked when written.\n");
fprintf (st, "    -C          Create a disk container and copy its contents from another disk\n");
fprintf (st, "                (simh, VHD, or RAW format).  The current (or specified with -F)\n");
fprintf (st, "                container format will be the format of the created container.\n");
fprintf (st, "                AUTO or VHD will create a VHD container, SIMH will create a.\n");
fprintf (st, "                SIMH container. Add a -V switch to verify a copy operation.\n");
fprintf (st, "                Note: A copy will be performed between dissimilar sized\n");
fprintf (st, "                containers.  Copying from a larger container to a smaller\n");
fprintf (st, "                one will produce a truncated result.\n");
fprintf (st, "    -V          Perform a verification pass to confirm successful data copy\n");
fprintf (st, "                operation.\n");
fprintf (st, "    -X          When creating a VHD, create a fixed sized VHD (vs a Dynamically\n");
fprintf (st, "                expanding one).\n");
fprintf (st, "    -D          Create a Differencing VHD (relative to an already existing VHD\n");
fprintf (st, "                disk)\n");
fprintf (st, "    -M          Merge a Differencing VHD into its parent VHD disk\n");
fprintf (st, "    -O          Override consistency checks when attaching differencing disks\n");
fprintf (st, "                which have unexpected parent disk GUID or timestamps\n");
fprintf (st, "    -U          Fix inconsistencies which are overridden by the -O switch\n");
if (strstr (sim_name, "-10") == NULL) {
    fprintf (st, "    -Y          Answer Yes to prompt to overwrite last track (on disk create)\n");
    fprintf (st, "    -N          Answer No to prompt to overwrite last track (on disk create)\n");
    }
fprintf (st, "\n\n");
fprintf (st, "Examples:\n");
fprintf (st, "  sim> show %s\n", ex->dname);
fprintf (st, "    %s, address=20001468-2000146B*, no vector, 4 units\n", ex->dname);
fprintf (st, "    %s0, %s, not attached, write enabled, %s, autosize, AUTO detect format\n", ex->dname, ex->dsize, ex->dtype);
fprintf (st, "    %s1, %s, not attached, write enabled, %s, autosize, AUTO detect format\n", ex->dname, ex->dsize, ex->dtype);
fprintf (st, "    %s2, %s, not attached, write enabled, %s, autosize, AUTO detect format\n", ex->dname, ex->dsize, ex->dtype);
fprintf (st, "    %s3, %s, not attached, write enabled, %s, autosize, AUTO detect format\n", ex->dname, ex->dsize2, ex->dtype2);
fprintf (st, "  sim> # attach an existing VHD and determine its size and type automatically\n");
fprintf (st, "  sim> attach %s0 %s.vhd\n", ex->dname, ex->dtype3);
fprintf (st, "  sim> show %s0\n", ex->dname);
fprintf (st, "  %s0, %s, attached to %s.vhd, write enabled, %s, autosize, VHD format\n", ex->dname, ex->dsize3, ex->dtype3, ex->dtype3);
fprintf (st, "  sim> # create a new %s drive type VHD\n", ex->dtype4);
fprintf (st, "  sim> set %s2 %s\n", ex->dname, ex->dtype4);
fprintf (st, "  sim> attach %s2 -f vhd %s.vhd\n", ex->dname, ex->dtype4);
fprintf (st, "  %s2: creating new file\n", ex->dname);
fprintf (st, "  sim> show %s2\n", ex->dname);
fprintf (st, "  %s2, %s, attached to %s.vhd, write enabled, %s, autosize, VHD format\n", ex->dname, ex->dsize4, ex->dtype4, ex->dtype4);
fprintf (st, "  sim> # examine the size consumed by the %s VHD file\n", ex->dsize4);
fprintf (st, "  sim> dir %s.vhd\n", ex->dtype4);
fprintf (st, "   Directory of H:\\Data\n\n");
fprintf (st, "  04/14/2011  12:57 PM             5,120 %s.vhd\n", ex->dtype4);
fprintf (st, "                 1 File(s)          5,120 bytes\n");
fprintf (st, "  sim> # create a differencing vhd (%s-1-Diff.vhd) with %s.vhd as parent\n", ex->dtype4, ex->dtype4);
fprintf (st, "  sim> attach %s3 -d %s-1-Diff.vhd %s.vhd\n", ex->dname, ex->dtype4, ex->dtype4);
fprintf (st, "  sim> # create a VHD (%s-1.vhd) which is a copy of an existing disk\n", ex->dtype4);
fprintf (st, "  sim> attach %s3 -c %s-1.vhd %s.vhd\n", ex->dname, ex->dtype4, ex->dtype4);
fprintf (st, "  %s3: Creating new VHD format '%s-1.vhd' %s disk container.\n", ex->dname, ex->dtype4, ex->dtype4);
fprintf (st, "  %s3: copying from '%s.vhd' a %s container\n", ex->dname, ex->dtype4, ex->dtype4);
fprintf (st, "  %s3: Copied %s.  99%% complete.\n", ex->dname, ex->dsize4);
fprintf (st, "  %s3: Copied %s. Done.\n", ex->dname, ex->dsize4);
fprintf (st, "  sim> show %s3\n", ex->dname);
fprintf (st, "  %s3, %s, attached to %s-1.vhd, write enabled, %s, autosize, VHD format\n", ex->dname, ex->dsize4, ex->dtype4, ex->dtype4);
fprintf (st, "  sim> dir %s*\n", ex->dtype4);
fprintf (st, "   Directory of H:\\Data\n\n");
fprintf (st, "  04/14/2011  01:12 PM             5,120 %s-1.vhd\n", ex->dtype4);
fprintf (st, "  04/14/2011  12:58 PM             5,120 %s.vhd\n", ex->dtype4);
fprintf (st, "                 2 File(s)         10,240 bytes\n");
fprintf (st, "  sim> show %s2\n", ex->dname);
fprintf (st, "  %s2, %s, not attached, write enabled, %s, autosize, VHD format\n", ex->dname, ex->dsize4, ex->dtype4);
fprintf (st, "  sim> set %s2 %s\n", ex->dname, ex->dtype3);
fprintf (st, "  sim> set %s2 noauto\n", ex->dname);
fprintf (st, "  sim> show %s2\n", ex->dname);
fprintf (st, "  %s2, %s, not attached, write enabled, %s, noautosize, VHD format\n", ex->dname, ex->dsize3, ex->dtype3);
fprintf (st, "  sim> set %s2 format=simh\n", ex->dname);
fprintf (st, "  sim> show %s2\n", ex->dname);
fprintf (st, "  %s2, %s, not attached, write enabled, %s, noautosize, SIMH format\n", ex->dname, ex->dsize3, ex->dtype3);
fprintf (st, "  sim> # create a VHD from an existing SIMH format disk\n");
fprintf (st, "  sim> attach %s2 -c %s-Copy.vhd XYZZY.dsk\n", ex->dname, ex->dtype3);
fprintf (st, "  %s3: copying from '%s.vhd' a %s container.\n", ex->dname, ex->dtype3, ex->dtype3);
fprintf (st, "  %s2: creating new virtual disk '%s-Copy.vhd'\n", ex->dname, ex->dtype3);
fprintf (st, "  %s2: Copied %s.  99%% complete.\n", ex->dname, ex->dsize3);
fprintf (st, "  %s2: Copied %s. Done.\n", ex->dname, ex->dsize3);
fprintf (st, "  sim> show %s2\n", ex->dname);
fprintf (st, "  %s2, %s, attached to %s-Copy.vhd, write enabled, %s, noautosize, VHD format\n", ex->dname, ex->dsize3, ex->dtype3, ex->dtype3);
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

sim_debug_unit (ctx->dbit, uptr, "sim_disk_reset(unit=%d)\n", (int)(uptr - ctx->dptr->units));

_sim_disk_io_flush(uptr);
AIO_VALIDATE(uptr);
AIO_UPDATE_QUEUE;
return SCPE_OK;
}

t_stat sim_disk_perror (UNIT *uptr, const char *msg)
{
int saved_errno = errno;

if (!(uptr->flags & UNIT_ATTABLE))                      /* not attachable? */
    return SCPE_NOATT;
#if defined(_WIN32)
saved_errno = GetLastError ();
#endif
perror (msg);
sim_printf ("%s %s: %s\n", sim_uname(uptr), msg, sim_get_os_error_text (saved_errno));
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
        if ((size_t)wds != sim_fwrite (buf, sizeof (uint16), wds, uptr->fileref))
            stat = SCPE_IOERR;
        }
free (buf);
return stat;
}

void sim_disk_data_trace(UNIT *uptr, const uint8 *data, size_t lba, size_t len, const char* txt, int detail, uint32 reason)
{
DEVICE *dptr = find_dev_from_unit (uptr);

if (sim_deb && ((uptr->dctrl | dptr->dctrl) & reason)) {
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
   escape sequence.  This only affects RAW device names and UNC paths.
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
long long addr = ((long long)lba) * ctx->sector_size;
DWORD bytestoread = sects * ctx->sector_size;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);
if (sectsread)
    *sectsread = 0;
memset (&pos, 0, sizeof (pos));
while (bytestoread) {
    DWORD bytesread;
    DWORD sectorbytes;

    pos.Offset = (DWORD)addr;
    pos.OffsetHigh = (DWORD)(addr >> 32);
    if (!ReadFile ((HANDLE)(uptr->fileref), buf, bytestoread, &bytesread, &pos)) {
        if (ERROR_HANDLE_EOF == GetLastError ()) {  /* Return 0's for reads past EOF */
            memset (buf, 0, bytestoread);
            if (sectsread)
                *sectsread += bytestoread / ctx->sector_size;
            return SCPE_OK;
            }
        _set_errno_from_status (GetLastError ());
        return SCPE_IOERR;
        }
    sectorbytes = (bytesread / ctx->sector_size) * ctx->sector_size;
    if (bytesread > sectorbytes) {
        memset (buf + bytesread, 0, bytestoread - bytesread);
        sectorbytes += ctx->sector_size;
        }
    if (sectsread)
        *sectsread += sectorbytes / ctx->sector_size;
    bytestoread -= sectorbytes;
    buf +=  sectorbytes;
    addr += sectorbytes;
    }
return SCPE_OK;
}

static t_stat sim_os_disk_read (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *bytesread, uint32 bytes)
{
OVERLAPPED pos;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_read(unit=%d, addr=0x%X, bytes=%u)\n", (int)(uptr - ctx->dptr->units), (uint32)addr, bytes);

memset (&pos, 0, sizeof (pos));
pos.Offset = (DWORD)addr;
pos.OffsetHigh = (DWORD)(addr >> 32);
if (ReadFile ((HANDLE)(uptr->fileref), buf, (DWORD)bytes, (LPDWORD)bytesread, &pos))
    return SCPE_OK;
if (ERROR_HANDLE_EOF == GetLastError ()) {  /* Return 0's for reads past EOF */
    memset (buf, 0, bytes);
    if (bytesread)
        *bytesread = bytes;
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
DWORD byteswritten;
DWORD bytestowrite = sects * ctx->sector_size;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

if (sectswritten)
    *sectswritten = 0;
addr = ((long long)lba) * ctx->sector_size;
memset (&pos, 0, sizeof (pos));
while (bytestowrite) {
    DWORD sectorbytes;

    pos.Offset = (DWORD)addr;
    pos.OffsetHigh = (DWORD)(addr >> 32);
    if (!WriteFile ((HANDLE)(uptr->fileref), buf, bytestowrite, &byteswritten, &pos)) {
        _set_errno_from_status (GetLastError ());
        return SCPE_IOERR;
        }
    if (sectswritten)
        *sectswritten += byteswritten / ctx->sector_size;
    sectorbytes = (byteswritten / ctx->sector_size) * ctx->sector_size;
    bytestowrite -= sectorbytes;
    if (bytestowrite == 0)
        break;
    buf += sectorbytes;
    addr += sectorbytes;
    }
return SCPE_OK;
}

static t_stat sim_os_disk_write (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *byteswritten, uint32 bytes)
{
OVERLAPPED pos;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_write(unit=%d, lba=0x%X, bytes=%u)\n", (int)(uptr - ctx->dptr->units), (uint32)addr, bytes);

memset (&pos, 0, sizeof (pos));
pos.Offset = (DWORD)addr;
pos.OffsetHigh = (DWORD)(addr >> 32);
if (WriteFile ((HANDLE)(uptr->fileref), buf, bytes, (LPDWORD)byteswritten, &pos))
    return SCPE_OK;
_set_errno_from_status (GetLastError ());
return SCPE_IOERR;
}

#elif defined (__linux) || defined (__linux__) || defined (__APPLE__)|| defined (__sun) || defined (__sun__) || defined (__hpux) || defined (_AIX)

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
    return sim_messagef (SCPE_OK, "Apparent CDROM can't unlock door: %s\n", strerror (errno));
if (ioctl((int)((long)f), CDROMEJECT) < 0)
    return sim_messagef (SCPE_OK, "Apparent CDROM can't eject: %s\n", strerror (errno));
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
off_t addr = ((off_t)lba) * ctx->sector_size;
ssize_t bytesread;
size_t bytestoread = sects * ctx->sector_size;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

if (sectsread)
    *sectsread = 0;
while (bytestoread) {
    size_t sectorbytes;

    bytesread = pread((int)((long)uptr->fileref), buf, bytestoread, addr);
    if (bytesread < 0) {
        return SCPE_IOERR;
        }
    if ((size_t)bytesread < bytestoread) {/* read zeros at/past EOF */
        memset (buf + bytesread, 0, bytestoread - bytesread);
        bytesread = bytestoread;
        }
    sectorbytes = (bytesread / ctx->sector_size) * ctx->sector_size;
    if ((size_t)bytesread > sectorbytes)
        sectorbytes += ctx->sector_size;
    if (sectsread)
        *sectsread += sectorbytes / ctx->sector_size;
    bytestoread -= sectorbytes;
    buf += sectorbytes;
    addr += sectorbytes;
    }
return SCPE_OK;
}

static t_stat sim_os_disk_read (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *rbytesread, uint32 bytes)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
ssize_t bytesread;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_read(unit=%d, addr=0x%X, bytes=%u)\n", (int)(uptr - ctx->dptr->units), (uint32)addr, bytes);

bytesread = pread((int)((long)uptr->fileref), buf, bytes, (off_t)addr);
if (bytesread < 0) {
    if (rbytesread)
        *rbytesread = 0;
    return SCPE_IOERR;
    }
if (rbytesread)
    *rbytesread = bytesread;
return SCPE_OK;
}

static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
off_t addr = ((off_t)lba) * ctx->sector_size;
ssize_t byteswritten;
size_t bytestowrite = sects * ctx->sector_size;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

if (sectswritten)
    *sectswritten = 0;
while (bytestowrite) {
    size_t sectorbytes;

    byteswritten = pwrite((int)((long)uptr->fileref), buf, bytestowrite, addr);
    if (byteswritten < 0) {
        return SCPE_IOERR;
        }
    if (sectswritten)
        *sectswritten += byteswritten / ctx->sector_size;
    sectorbytes = (byteswritten / ctx->sector_size) * ctx->sector_size;
    bytestowrite -= sectorbytes;
    if (bytestowrite == 0)
        break;
    buf += sectorbytes;
    addr += sectorbytes;
    }
return SCPE_OK;
}

static t_stat sim_os_disk_write (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *rbyteswritten, uint32 bytes)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
ssize_t byteswritten;

sim_debug_unit (ctx->dbit, uptr, "sim_os_disk_write(unit=%d, addr=0x%X, bytes=%u)\n", (int)(uptr - ctx->dptr->units), (uint32)addr, bytes);

if (rbyteswritten)
    *rbyteswritten = 0;
byteswritten = pwrite((int)((long)uptr->fileref), buf, bytes, (off_t)addr);
if (byteswritten < 0)
    return SCPE_IOERR;
if (rbyteswritten)
    *rbyteswritten = byteswritten;
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
*sectsread = 0;
return SCPE_NOFNC;
}

static t_stat sim_os_disk_read (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *bytesread, uint32 bytes)
{
*bytesread = 0;
return SCPE_NOFNC;
}

static t_stat sim_os_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
*sectswritten = 0;
return SCPE_NOFNC;
}

static t_stat sim_os_disk_write (UNIT *uptr, t_offset addr, uint8 *buf, uint32 *byteswritten, uint32 bytes)
{
*byteswritten = 0;
return SCPE_NOFNC;
}

static t_stat sim_os_disk_info_raw (FILE *f, uint32 *sector_size, uint32 *removable, uint32 *is_cdrom)
{
*sector_size = *removable = *is_cdrom = 0;
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
/*   This is only for systems which don't have 64 bit integer types           */
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

static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize, DRVTYP *drvtyp)
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

static uint32 sim_vhd_CHS (FILE *f)
{
return 0;
}

static FILE *sim_vhd_disk_parent_path (FILE *f)
{
return NULL;
}

static t_stat sim_vhd_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects)
{
*sectsread = 0;
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_clearerr (UNIT *uptr)
{
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
*sectswritten = 0;
return SCPE_IOERR;
}

static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype, uint32 SectorSize, uint32 xfer_encode_size, uint32 media_id, const char *device_name, uint32 data_width, DRVTYP *drvtyp)
{
return SCPE_NOFNC;
}

static const char *sim_vhd_disk_get_dtype (FILE *f, uint32 *SectorSize, uint32 *xfer_encode_size, char sim_name[64], time_t *creation_time, uint32 *media_id, char device_name[16], uint32 *data_width)
{
*SectorSize = *xfer_encode_size = 0;
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
    uint32 DriveSectorSize;
    uint32 DriveElementEncodingSize;
    uint8 CreatingSimulator[64];
    uint32 MediaId;
    uint32 DataWidth;
    uint8 DeviceName[16];
    /*
    This field contains zeroes. It is 304 bytes in size.
    */
    uint8 Reserved[304];
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

#define VHD_Internal_SectorSize     512

typedef struct VHD_IOData *VHDHANDLE;

static t_stat ReadFilePosition(FILE *File, void *buf, size_t bufsize, uint32 *bytesread, uint64 position)
{
uint32 err = sim_fseeko (File, (t_offset)position, SEEK_SET);
size_t i;

if (bytesread)
    *bytesread = 0;
if (!err) {
    i = fread (buf, 1, bufsize, File);
    if (bytesread)
        *bytesread = (uint32)i;
    err = ferror (File);
    }
return (err ? SCPE_IOERR : SCPE_OK);
}

static t_stat WriteFilePosition(FILE *File, void *buf, size_t bufsize, uint32 *byteswritten, uint64 position)
{
uint32 err = sim_fseeko (File, (t_offset)position, SEEK_SET);
size_t i;

if (byteswritten)
    *byteswritten = 0;
if (!err) {
    i = fwrite (buf, 1, bufsize, File);
    if (byteswritten)
        *byteswritten = (uint32)i;
    err = ferror (File);
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
static uint64
NtoHll(uint64 value)
{
uint8 *l = (uint8 *)&value;
uint64 highresult = (uint64)l[3] | ((uint64)l[2]<<8) | ((uint64)l[1]<<16) | ((uint64)l[0]<<24);
uint32 lowresult = (uint64)l[7] | ((uint64)l[6]<<8) | ((uint64)l[5]<<16) | ((uint64)l[4]<<24);
return (highresult << 32) | lowresult;
}
#elif  __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
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
        *aBAT = (uint32*) calloc(1, VHD_Internal_SectorSize * ((sizeof(**aBAT) * NtoHl(sDynamic->MaxTableEntries) + VHD_Internal_SectorSize - 1) / VHD_Internal_SectorSize));
        if (ReadFilePosition(File,
                             *aBAT,
                             sizeof (**aBAT) * NtoHl(sDynamic->MaxTableEntries),
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
                memset (CheckPath, 0, sizeof (CheckPath));
                if (0 == memcmp (sDynamic->ParentLocatorEntries[j].PlatformCode, "W2ku", 4))
                    strlcpy (CheckPath, ParentName, sizeof (CheckPath));
                else
                    if (0 == memcmp (sDynamic->ParentLocatorEntries[j].PlatformCode, "W2ru", 4)) {
                        const char *c;

                        if ((c = strrchr (szVHDPath, '\\'))) {
                            memcpy (CheckPath, szVHDPath, c-szVHDPath+1);
                            strlcat (CheckPath, ParentName, sizeof (CheckPath));
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
                         (sim_switches & SWMASK ('O')))) {
                         memset (szParentVHDPath, 0, ParentVHDPathSize);
                         strlcpy (szParentVHDPath, CheckPath, ParentVHDPathSize);
                        }
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
    int Writable;
    char VHDPath[512];
    char ParentVHDPath[512];
    struct VHD_IOData *Parent;
    };

static t_stat sim_vhd_disk_implemented (void)
{
return SCPE_OK;
}

static t_stat sim_vhd_disk_set_dtype (FILE *f, const char *dtype, uint32 SectorSize, uint32 xfer_encode_size, uint32 media_id, const char *device_name, uint32 data_width, DRVTYP *drvtyp)
{
VHDHANDLE hVHD  = (VHDHANDLE)f;
int Status = 0;
struct stat statb;

if (fstat (fileno (f), &statb))
    memset (&statb, 0, sizeof (statb));
memset (hVHD->Footer.DriveType, '\0', sizeof hVHD->Footer.DriveType);
memcpy (hVHD->Footer.DriveType, dtype, ((1+strlen (dtype)) < sizeof (hVHD->Footer.DriveType)) ? (1+strlen (dtype)) : sizeof (hVHD->Footer.DriveType));
hVHD->Footer.DriveSectorSize = NtoHl (SectorSize);
hVHD->Footer.DriveElementEncodingSize = NtoHl (xfer_encode_size);
if (hVHD->Footer.CreatingSimulator[0] == 0) {
    memset (hVHD->Footer.CreatingSimulator, 0, sizeof (hVHD->Footer.CreatingSimulator));
    strlcpy ((char *)hVHD->Footer.CreatingSimulator, sim_name, sizeof (hVHD->Footer.CreatingSimulator));
    }
hVHD->Footer.MediaId = NtoHl (media_id);
if (device_name) {
    memset (hVHD->Footer.DeviceName, 0, sizeof (hVHD->Footer.DeviceName));
    strlcpy ((char *)hVHD->Footer.DeviceName, device_name, sizeof (hVHD->Footer.DeviceName));
    }
hVHD->Footer.DataWidth = NtoHl (data_width);
hVHD->Footer.DiskGeometry = NtoHl (sim_disk_drvtype_geometry (drvtyp, (uint32)(NtoHll (hVHD->Footer.CurrentSize) / NtoHl (hVHD->Footer.DriveSectorSize))));
hVHD->Footer.Checksum = 0;
hVHD->Footer.Checksum = NtoHl (CalculateVhdFooterChecksum (&hVHD->Footer, sizeof(hVHD->Footer)));

if (hVHD->Writable == 0) {
    fclose (hVHD->File);
    hVHD->File = sim_fopen (hVHD->VHDPath, "rb+");
    }
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
else {
    fclose (hVHD->File);
    sim_set_file_times (hVHD->VHDPath, statb.st_atime, statb.st_mtime);
    hVHD->File = sim_fopen (hVHD->VHDPath, hVHD->Writable ? "rb+" : "rb");
    }
return SCPE_OK;
}

static const char *sim_vhd_disk_get_dtype (FILE *f, uint32 *SectorSize, uint32 *xfer_encode_size, char sim_name[64], time_t *creation_time, uint32 *media_id, char device_name[16], uint32 *data_width)
{
VHDHANDLE hVHD  = (VHDHANDLE)f;

if (SectorSize)
    *SectorSize = NtoHl (hVHD->Footer.DriveSectorSize);
if (xfer_encode_size)
    *xfer_encode_size = NtoHl (hVHD->Footer.DriveElementEncodingSize);
if (sim_name)
    memcpy (sim_name, hVHD->Footer.CreatingSimulator, 64);
if (creation_time)
    *creation_time = (time_t)NtoHl (hVHD->Footer.TimeStamp) + 946684800;
if (media_id)
    *media_id = NtoHl (hVHD->Footer.MediaId);
if (device_name)
    memcpy (device_name, hVHD->Footer.DeviceName, 16);
if (data_width)
    *data_width = NtoHl (hVHD->Footer.DataWidth);
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
    else {
        strlcpy (hVHD->VHDPath, szVHDPath, sizeof (hVHD->VHDPath));
        hVHD->Writable = (strchr (DesiredAccess, 'w') || strchr (DesiredAccess, '+'));
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
    uint32 BytesRead;
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
    else
        strlcpy (hVHD->VHDPath, szVHDPath, sizeof (hVHD->VHDPath));
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

static uint32 sim_vhd_CHS (FILE *f)
{
VHDHANDLE hVHD = (VHDHANDLE)f;

return NtoHl (hVHD->Footer.DiskGeometry);
}

static const char *sim_vhd_disk_parent_path (FILE *f)
{
VHDHANDLE hVHD = (VHDHANDLE)f;

return (const char *)(hVHD->ParentVHDPath);
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
#elif defined (SIM_HAVE_DLOPEN)
static void
uuid_gen (void *uuidaddr)
{
void (*uuid_generate_c) (void *) = NULL;
void *handle;

    handle = dlopen("libuuid." __STR(SIM_DLOPEN_EXTENSION), RTLD_NOW|RTLD_GLOBAL);
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

/* CHS Calculation */
static uint32 sim_SectorsToCHS (uint32 totalSectors)
{
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
    if (cylinderTimesHeads >= (heads * 1024) || heads > 16) {
        sectorsPerTrack = 31;
        heads = 16;
        cylinderTimesHeads = totalSectors / sectorsPerTrack;
        }
    if (cylinderTimesHeads >= (heads * 1024)) {
        sectorsPerTrack = 63;
        heads = 16;
        cylinderTimesHeads = totalSectors / sectorsPerTrack;
        }
    }
cylinders = (totalSectors + sectorsPerTrack * heads - 1) / (sectorsPerTrack * heads);
return (cylinders<<16) | (heads<<8) | sectorsPerTrack;
}

uint32 sim_disk_drvtype_geometry (DRVTYP *drvtyp, uint32 totalSectors)
{
if ((drvtyp == NULL) || (0xFFFF10FF == sim_SectorsToCHS (totalSectors)) ||
    ((drvtyp->sect * drvtyp->surf * drvtyp->cyl) < totalSectors))
    return sim_SectorsToCHS (totalSectors);
return ((drvtyp->cyl << 16) | (drvtyp->surf << 8) | drvtyp->sect);
}


static VHDHANDLE
sim_CreateVirtualDisk(const char *szVHDPath,
                      uint32 SizeInSectors,
                      uint32 BlockSize,
                      t_bool bFixedVHD,
                      VHD_Footer *ParentFooter,
                      DRVTYP *drvtyp)
{
VHD_Footer Footer;
VHD_DynamicDiskHeader Dynamic;
uint32 *BAT = NULL;
time_t now;
uint32 i;
FILE *File = NULL;
uint32 Status = 0;
uint32 BytesPerSector = 512;
uint64 SizeInBytes = ((uint64)SizeInSectors) * BytesPerSector;
uint64 TableOffset;
uint32 MaxTableEntries;
VHDHANDLE hVHD = NULL;

if (SizeInBytes > ((uint64)(1024 * 1024 * 1024)) * 2040) {
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
if (ParentFooter)                                               /* If provided */
    Footer = *ParentFooter;                                     /* Inherit Footer contents from Parent */
Footer.Checksum = 0;
memcpy (Footer.Cookie, "conectix", 8);
Footer.Features = NtoHl (0x00000002);;
Footer.FileFormatVersion = NtoHl (0x00010000);;
Footer.DataOffset = NtoHll (bFixedVHD ? ((long long)-1) : (long long)(sizeof(Footer)));
time (&now);
Footer.TimeStamp = NtoHl ((uint32)(now - 946684800));
memcpy (Footer.CreatorApplication, "simh", 4);
Footer.CreatorVersion = NtoHl (0x00040000);
memcpy (Footer.CreatorHostOS, "Wi2k", 4);
Footer.OriginalSize = NtoHll (SizeInBytes);
Footer.CurrentSize = NtoHll (SizeInBytes);
uuid_gen (Footer.UniqueID);
Footer.DiskType = NtoHl (bFixedVHD ? VHD_DT_Fixed : VHD_DT_Dynamic);
if (ParentFooter == NULL)
    Footer.DiskGeometry = NtoHl (sim_disk_drvtype_geometry (drvtyp, (uint32)(SizeInBytes / BytesPerSector)));
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
    strlcpy (szFullFileSpecBuffer, szFileSpec, BufferSize);
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

memset (szVhdPath, 0, VhdPathSize);
strlcpy (szVhdPath, szHostPath, VhdPathSize-1);
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

memmove (szHostPath, szVhdPath, HostPathSize);
szHostPath[HostPathSize-1] = '\0';
#if defined(VMS)
c = strchr (szHostPath, ':');
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
hVHD = sim_CreateVirtualDisk (szVHDPath,
                              (uint32)(NtoHll(ParentFooter.CurrentSize)/BytesPerSector),
                              NtoHl(ParentDynamic.BlockSize),
                              FALSE,
                              &ParentFooter,
                              NULL);
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
hVHD->Footer.DriveSectorSize = ParentFooter.DriveSectorSize;
hVHD->Footer.DriveElementEncodingSize = ParentFooter.DriveElementEncodingSize;
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

static FILE *sim_vhd_disk_create (const char *szVHDPath, t_offset desiredsize, DRVTYP *drvtyp)
{
return (FILE *)sim_CreateVirtualDisk (szVHDPath, (uint32)(desiredsize/512), 0, (sim_switches & SWMASK ('X')), NULL, drvtyp);
}

static FILE *sim_vhd_disk_create_diff (const char *szVHDPath, const char *szParentVHDPath)
{
return (FILE *)CreateDifferencingVirtualDisk (szVHDPath, szParentVHDPath);
}

static t_stat
ReadVirtualDisk(VHDHANDLE hVHD,
                uint8 *buf,
                uint32 BytesToRead,
                uint32 *BytesRead,
                uint64 Offset)
{
uint32 TotalBytesRead = 0;
uint32 BitMapBytes;
uint32 BitMapSectors;
uint32 DynamicBlockSize;
t_stat r = SCPE_OK;

if (BytesRead)
    *BytesRead = 0;
if (!hVHD || (hVHD->File == NULL)) {
    errno = EBADF;
    return SCPE_IOERR;
    }
if (BytesToRead == 0)
    return SCPE_OK;
if (Offset >= (uint64)NtoHll (hVHD->Footer.CurrentSize)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
if (NtoHl (hVHD->Footer.DiskType) == VHD_DT_Fixed) {
    if (ReadFilePosition(hVHD->File,
                         buf,
                         BytesToRead,
                         BytesRead,
                         Offset))
        r = SCPE_IOERR;
    return r;
    }
/* We are now dealing with a Dynamically expanding or differencing disk */
DynamicBlockSize = NtoHl (hVHD->Dynamic.BlockSize);
if ((DynamicBlockSize == 0) ||
    ((DynamicBlockSize & (DynamicBlockSize - 1)) != 0)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
BitMapBytes = (7+(DynamicBlockSize / VHD_Internal_SectorSize))/8;
BitMapSectors = (BitMapBytes+VHD_Internal_SectorSize-1)/VHD_Internal_SectorSize;
while (BytesToRead && (r == SCPE_OK)) {
    uint32 BlockNumber = (uint32)(Offset / DynamicBlockSize);
    uint32 BytesInRead = BytesToRead;
    uint32 BytesThisRead = 0;

    if (BlockNumber != (Offset + BytesToRead) / DynamicBlockSize)
        BytesInRead = (uint32)(((BlockNumber + 1) * DynamicBlockSize) - Offset);
    if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY) {
        if (!hVHD->Parent) {
            memset (buf, 0, BytesInRead);
            BytesThisRead = BytesInRead;
            }
        else {
            if (ReadVirtualDisk(hVHD->Parent,
                                buf,
                                BytesInRead,
                                &BytesThisRead,
                                Offset))
                r = SCPE_IOERR;
            }
        }
    else {
        uint64 BlockOffset = VHD_Internal_SectorSize * ((uint64)(NtoHl (hVHD->BAT[BlockNumber]) + BitMapSectors)) + (Offset % DynamicBlockSize);

        if (ReadFilePosition(hVHD->File,
                             buf,
                             BytesInRead,
                             &BytesThisRead,
                             BlockOffset))
            r = SCPE_IOERR;
        }
    BytesToRead -= BytesThisRead;
    buf = (uint8 *)(((char *)buf) + BytesThisRead);
    Offset += BytesThisRead;
    TotalBytesRead += BytesThisRead;
    }
if (BytesRead)
    *BytesRead = TotalBytesRead;
return SCPE_OK;
}

static t_stat
ReadVirtualDiskSectors(VHDHANDLE hVHD,
                       uint8 *buf,
                       t_seccnt sects,
                       t_seccnt *sectsread,
                       uint32 SectorSize,
                       t_lba lba)
{
uint32 BytesRead;
t_stat r = ReadVirtualDisk(hVHD,
                           buf,
                           sects * SectorSize,
                           &BytesRead,
                           SectorSize * (uint64)lba);
if (sectsread)
    *sectsread = BytesRead / SectorSize;
return r;
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
WriteVirtualDisk(VHDHANDLE hVHD,
                 uint8 *buf,
                 uint32 BytesToWrite,
                 uint32 *BytesWritten,
                 uint64 Offset)
{
uint32 TotalBytesWritten = 0;
uint32 BitMapBytes;
uint32 BitMapSectors;
uint32 DynamicBlockSize;
t_stat r = SCPE_OK;

if (BytesWritten)
    *BytesWritten = 0;
if (!hVHD || !hVHD->File) {
    errno = EBADF;
    return SCPE_IOERR;
    }
if (BytesToWrite == 0)
    return SCPE_OK;
if (Offset >= (uint64)NtoHll(hVHD->Footer.CurrentSize)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
if (NtoHl(hVHD->Footer.DiskType) == VHD_DT_Fixed) {
    if (WriteFilePosition(hVHD->File,
                          buf,
                          BytesToWrite,
                          BytesWritten,
                          Offset))
        r = SCPE_IOERR;
    return r;
    }
/* We are now dealing with a Dynamically expanding or differencing disk */
DynamicBlockSize = NtoHl (hVHD->Dynamic.BlockSize);
if ((DynamicBlockSize == 0) ||
    ((DynamicBlockSize & (DynamicBlockSize - 1)) != 0)) {
    errno = ERANGE;
    return SCPE_IOERR;
    }
BitMapBytes = (7 + (DynamicBlockSize / VHD_Internal_SectorSize)) / 8;
BitMapSectors = (BitMapBytes + VHD_Internal_SectorSize - 1) / VHD_Internal_SectorSize;
while (BytesToWrite && (r == SCPE_OK)) {
    uint32 BlockNumber = (uint32)(Offset / DynamicBlockSize);
    uint32 BytesInWrite = BytesToWrite;
    uint32 BytesThisWrite = 0;

    if (BlockNumber >= NtoHl(hVHD->Dynamic.MaxTableEntries)) {
        return SCPE_EOF;
        }
    if (BlockNumber != (Offset + BytesToWrite) / DynamicBlockSize)
        BytesInWrite = (uint32)(((BlockNumber + 1) * DynamicBlockSize) - Offset);
    if (hVHD->BAT[BlockNumber] == VHD_BAT_FREE_ENTRY) {
        uint8 *BitMap = NULL;
        uint32 BitMapBufferSize = VHD_DATA_BLOCK_ALIGNMENT;
        uint8 *BitMapBuffer = NULL;
        void *BlockData = NULL;
        uint8 *BATUpdateBufferAddress;
        uint32 BATUpdateBufferSize;
        uint64 BATUpdateStorageAddress;
        uint64 BlockOffset;

        if (!hVHD->Parent && BufferIsZeros(buf, BytesInWrite)) {
            BytesThisWrite = BytesInWrite;
            goto IO_Done;
            }
        /* Need to allocate a new Data Block. */
        BlockOffset = sim_fsize_ex (hVHD->File);
        if (((int64)BlockOffset) == -1)
            return SCPE_IOERR;
        if ((BitMapSectors * VHD_Internal_SectorSize) > BitMapBufferSize)
            BitMapBufferSize = BitMapSectors * VHD_Internal_SectorSize;
        BitMapBuffer = (uint8 *)calloc(1, BitMapBufferSize + DynamicBlockSize);
        if (BitMapBufferSize > BitMapSectors * VHD_Internal_SectorSize)
            BitMap = BitMapBuffer + BitMapBufferSize - BitMapBytes;
        else
            BitMap = BitMapBuffer;
        memset(BitMap, 0xFF, BitMapBytes);
        BlockOffset -= sizeof(hVHD->Footer);
        if (0 == (BlockOffset & (VHD_DATA_BLOCK_ALIGNMENT-1)))
            {  // Already aligned, so use padded BitMapBuffer
            if (WriteFilePosition(hVHD->File,
                                  BitMapBuffer,
                                  BitMapBufferSize + DynamicBlockSize,
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
            BlockOffset += BitMapSectors * VHD_Internal_SectorSize;
            // round up this address to the desired alignment
            BlockOffset += VHD_DATA_BLOCK_ALIGNMENT-1;
            BlockOffset &= ~(VHD_DATA_BLOCK_ALIGNMENT - 1);
            BlockOffset -= BitMapSectors * VHD_Internal_SectorSize;
            if (WriteFilePosition(hVHD->File,
                                  BitMap,
                                  (BitMapSectors * VHD_Internal_SectorSize) + DynamicBlockSize,
                                  NULL,
                                  BlockOffset)) {
                free (BitMapBuffer);
                return SCPE_IOERR;
                }
            BlockOffset += BitMapSectors * VHD_Internal_SectorSize;
            }
        free(BitMapBuffer);
        BitMapBuffer = BitMap = NULL;
        /* the BAT block address is the beginning of the block bitmap */
        BlockOffset -= BitMapSectors * VHD_Internal_SectorSize;
        hVHD->BAT[BlockNumber] = NtoHl((uint32)(BlockOffset / VHD_Internal_SectorSize));
        BlockOffset += (BitMapSectors * VHD_Internal_SectorSize) + DynamicBlockSize;
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
        if ((size_t)(BATUpdateBufferAddress - (uint8 *)hVHD->BAT + BATUpdateBufferSize) > VHD_Internal_SectorSize * ((sizeof(*hVHD->BAT)*NtoHl(hVHD->Dynamic.MaxTableEntries) + VHD_Internal_SectorSize - 1)/VHD_Internal_SectorSize))
            BATUpdateBufferSize = (uint32)(VHD_Internal_SectorSize * ((sizeof(*hVHD->BAT) * NtoHl(hVHD->Dynamic.MaxTableEntries) + VHD_Internal_SectorSize - 1)/VHD_Internal_SectorSize) - (BATUpdateBufferAddress - ((uint8 *)hVHD->BAT)));
        if (WriteFilePosition(hVHD->File,
                              BATUpdateBufferAddress,
                              BATUpdateBufferSize,
                              NULL,
                              BATUpdateStorageAddress))
            goto Fatal_IO_Error;
        if (hVHD->Parent)
            { /* Need to populate data block contents from parent VHD */
            BlockData = malloc (NtoHl (hVHD->Dynamic.BlockSize));

            if (ReadVirtualDisk(hVHD->Parent,
                                (uint8*) BlockData,
                                NtoHl (hVHD->Dynamic.BlockSize),
                                NULL,
                                (Offset / NtoHl (hVHD->Dynamic.BlockSize)) * NtoHl (hVHD->Dynamic.BlockSize)))
                goto Fatal_IO_Error;
            if (WriteVirtualDisk(hVHD,
                                 (uint8*) BlockData,
                                 NtoHl (hVHD->Dynamic.BlockSize),
                                 NULL,
                                 (Offset / NtoHl (hVHD->Dynamic.BlockSize)) * NtoHl (hVHD->Dynamic.BlockSize)))
                goto Fatal_IO_Error;
            free(BlockData);
            }
        continue;
Fatal_IO_Error:
        free (BitMap);
        free (BlockData);
        r = SCPE_IOERR;
        }
    else {
        uint64 BlockOffset = VHD_Internal_SectorSize * ((uint64)(NtoHl(hVHD->BAT[BlockNumber]) + BitMapSectors)) + (Offset % DynamicBlockSize);

        if (WriteFilePosition(hVHD->File,
                              buf,
                              BytesInWrite,
                              &BytesThisWrite,
                              BlockOffset))
            r = SCPE_IOERR;
        }
IO_Done:
    BytesToWrite -= BytesThisWrite;
    buf = (uint8 *)(((char *)buf) + BytesThisWrite);
    Offset += BytesThisWrite;
    TotalBytesWritten += BytesThisWrite;
    }
if (BytesWritten)
    *BytesWritten = TotalBytesWritten;
return r;
}

static t_stat
WriteVirtualDiskSectors(VHDHANDLE hVHD,
                        uint8 *buf,
                        t_seccnt sects,
                        t_seccnt *sectswritten,
                        uint32 SectorSize,
                        t_lba lba)
{
uint32 BytesWritten;
t_stat r = WriteVirtualDisk(hVHD,
                            buf,
                            sects * SectorSize,
                            &BytesWritten,
                            SectorSize * (uint64)lba);

if (sectswritten)
    *sectswritten = BytesWritten / SectorSize;
return r;
}

static t_stat sim_vhd_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects)
{
VHDHANDLE hVHD = (VHDHANDLE)uptr->fileref;
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

return WriteVirtualDiskSectors(hVHD, buf, sects, sectswritten, ctx->sector_size, lba);
}
#endif

/* Used when sorting a drive type list: */
/* - Disks come first ordered by drive size */
/* - Tapes come last ordered by drive name */
static int _drive_type_compare (const void *pa, const void *pb)
{
const DRVTYP *a = (const DRVTYP *)pa;
const DRVTYP *b = (const DRVTYP *)pb;
int32 size_cmp;

if (a->devtype == SCSI_TAPE) {
    if (b->devtype == SCSI_TAPE)
        return strcmp (a->name, b->name);
    else
        return 1;
    }
else {
    if (b->devtype == SCSI_TAPE)
        return -1;
    else {
        size_cmp = ((int32)a->size - (int32)b->size);
        if (size_cmp == 0)
            size_cmp = strcmp (a->name, b->name);
        return size_cmp;
        }
    }
}

static void deb_MTAB (MTAB *mptr)
{
char mask[CBUFSIZE] = "";
char pstring[CBUFSIZE];
char mstring[CBUFSIZE];
char desc[CBUFSIZE];
char help[CBUFSIZE];

if ((sim_scp_dev.dctrl & SIM_DBG_INIT) && sim_deb) {
    if ((mptr->mask & MTAB_XTD)) {
        if (MODMASK(mptr, MTAB_VDV))
            strlcat (mask, "MTAB_VDV|", sizeof (mask));
        if (MODMASK(mptr, MTAB_VUN))
            strlcat (mask, "MTAB_VUN|", sizeof (mask));
        if (MODMASK(mptr, MTAB_VALR))
            strlcat (mask, "MTAB_VALR|", sizeof (mask));
        if (MODMASK(mptr, MTAB_VALO))
            strlcat (mask, "MTAB_VALO|", sizeof (mask));
        if (MODMASK(mptr, MTAB_NMO))
            strlcat (mask, "MTAB_NMO|", sizeof (mask));
        if (MODMASK(mptr, MTAB_NC))
            strlcat (mask, "MTAB_NC|", sizeof (mask));
        if (MODMASK(mptr, MTAB_QUOTE))
            strlcat (mask, "MTAB_QUOTE|", sizeof (mask));
        if (MODMASK(mptr, MTAB_SHP))
            strlcat (mask, "MTAB_SHP|", sizeof (mask));
        if (strlen (mask) > 0)
            mask[strlen (mask) - 1] = '\0';
        }
    else
        snprintf (mask, sizeof (mask), "0x%X", mptr->mask);
    if (mptr->pstring)
        snprintf (pstring, sizeof (pstring), "\"%s\"", mptr->pstring);
    else
        strlcpy (pstring, "NULL", sizeof (pstring));
    if (mptr->mstring)
        snprintf (mstring, sizeof (mstring), "\"%s\"", mptr->mstring);
    else
        strlcpy (mstring, "NULL", sizeof (mstring));
    if (mptr->desc)
        snprintf (desc, sizeof (desc), "\"%s\"", (char *)mptr->desc);
    else
        strlcpy (desc, "NULL", sizeof (desc));
    if (mptr->help)
        snprintf (help, sizeof (help), "\"%s\"", mptr->help);
    else
        strlcpy (help, "NULL", sizeof (help));
    }
sim_debug (SIM_DBG_INIT, &sim_scp_dev, "{%s, 0x%X, %s, %s,\n", mask, mptr->match,
           pstring, mstring);
sim_debug (SIM_DBG_INIT, &sim_scp_dev, "    %s, %s,\n",
           mptr->valid ? "Validator-Routine" : "NULL", mptr->disp ? "Display-Routine" : "NULL");
sim_debug (SIM_DBG_INIT, &sim_scp_dev, "    %s, %s}\n",
           desc, help);
}

t_stat sim_disk_init (void)
{
int32 saved_sim_show_message = sim_show_message;
DEVICE *dptr;
uint32 i, j, k, l;
t_stat stat = SCPE_OK;

sim_debug (SIM_DBG_INIT, &sim_scp_dev, "sim_disk_init()\n");
for (i = 0; NULL != (dptr = sim_devices[i]); i++) {
    DRVTYP *drive;
    static MTAB autos[] = {
        { MTAB_XTD|MTAB_VUN,        1,  NULL, "AUTOSIZE",
            &sim_disk_set_autosize,  NULL, NULL, "Enable disk autosize on attach" },
        { MTAB_XTD|MTAB_VUN,        0,  NULL, "NOAUTOSIZE",
            &sim_disk_set_autosize,  NULL, NULL, "Disable disk autosize on attach"  },
        { MTAB_XTD|MTAB_VUN,        0,  "AUTOSIZE", NULL,
            NULL, &sim_disk_show_autosize, NULL, "Display disk autosize on attach setting" }};
    static MTAB autoz[] = {
        { MTAB_XTD|MTAB_VUN,        1,  NULL, "AUTOZAP",
            &sim_disk_set_autozap,  NULL, NULL, "Enable disk metadata removal on detach" },
        { MTAB_XTD|MTAB_VUN,        0,  NULL, "NOAUTOZAP",
            &sim_disk_set_autozap,  NULL, NULL, "Disable disk metadata removal on detach"  },
        { MTAB_XTD|MTAB_VUN,        0,  "AUTOZAP", NULL,
            NULL, &sim_disk_show_autozap, NULL, "Display disk autozap on detach setting" }};
    MTAB *mtab = dptr->modifiers;
    MTAB *nmtab = NULL;
    t_stat (*validator)(UNIT *up, int32 v, CONST char *cp, void *dp) = NULL;
    uint32 modifiers = 0;
    uint32 setters = 0;
    uint32 dumb_autosizers = 0;
    uint32 smart_autosizers = 0;
    uint32 smart_autosizer = 0xFFFFFFFF;
    uint32 drives = 0;
    uint32 aliases = 0;
    int32 show_type_entry = -1;

    if (((DEV_TYPE (dptr) != DEV_DISK) && (DEV_TYPE (dptr) != DEV_SCSI)) ||
        (dptr->type_ctx == NULL))
        continue;
    sim_debug (SIM_DBG_INIT, &sim_scp_dev, "Device: %s\n", dptr->name);
    drive = (DRVTYP *)dptr->type_ctx;
    /* First prepare/fill-in the drive type list */
    for (drives = aliases = 0; drive[drives].name != NULL; drives++) {
        if (drive[drives].MediaId == 0)
            drive[drives].MediaId = sim_disk_drive_type_to_mediaid (drive[drives].name, drive[drives].driver_name);
        if (drive[drives].name_alias != NULL)
            ++aliases;
        /* Validate Geometry parameters */
        if (((drive[drives].flags & DRVFL_SETSIZE) == 0) &&
            ((DRVFL_GET_IFTYPE(&drive[drives]) != DRVFL_TYPE_SCSI) ||
             (drive[drives].devtype != SCSI_TAPE)) &&
            ((drive[drives].flags & DRVFL_QICTAPE) == 0) &&
            (drive[drives].size > (drive[drives].sect * drive[drives].surf * drive[drives].cyl))) {
            stat = sim_messagef (SCPE_IERR, "Device %s drive type %s has unreasonable geometry values:\n",
                                            dptr->name, drive[drives].name);
            stat = sim_messagef (SCPE_IERR, "Total Sectors: %u > (%u Cyls * %u Heads * %u sectors)\n",
                                            drive[drives].size, drive[drives].cyl, drive[drives].surf, drive[drives].sect);
            }
        }
    sim_debug (SIM_DBG_INIT, &sim_scp_dev, "%d Drive Types, %d aliases\n", drives, aliases);
    qsort (drive, drives, sizeof (*drive), _drive_type_compare);
    /* find device type modifier entries */
    for (j = 0; mtab[j].mask != 0; j++) {
        ++modifiers;
        if (((mtab[j].pstring != NULL) &&
             ((strcasecmp (mtab[j].pstring, "AUTOSIZE") == 0)   ||
              (strcasecmp (mtab[j].pstring, "NOAUTOSIZE") == 0))) ||
            ((mtab[j].mstring != NULL) &&
             ((strcasecmp (mtab[j].mstring, "AUTOSIZE") == 0)   ||
              (strcasecmp (mtab[j].mstring, "NOAUTOSIZE") == 0)))) {
             if ((mtab[j].mask & (MTAB_XTD|MTAB_VUN)) == 0)
                 ++dumb_autosizers;             /* Autosize set in unit flags */
             else {
                 ++smart_autosizers;            /* Autosize set by modifier */
                 smart_autosizer = j;
                 }
            }
        if ((mtab[j].disp == &sim_disk_show_drive_type) &&
            (mtab[j].mask == (MTAB_XTD|MTAB_VUN)))
            show_type_entry = j;
        for (k = 0; drive[k].name != NULL; k++) {
            if ((mtab[j].mstring == NULL) ||
                (strncasecmp (mtab[j].mstring, drive[k].name, strlen (drive[k].name))))
                continue;
            validator = mtab[j].valid;
            break;
            }
        if ((k == drives) &&
            ((mtab[j].mask != (MTAB_XTD|MTAB_VUN)) ||
             (mtab[j].pstring == NULL)             ||
             (strcasecmp (mtab[j].pstring, "TYPE") != 0)))
            continue;
        ++setters;
        }
    sim_debug (SIM_DBG_INIT, &sim_scp_dev, "%d Smart Autosizers, %d Modifiers, %d Setters, %d Dumb Autosizers\n",
                                           smart_autosizers, modifiers, setters, dumb_autosizers);
    nmtab = (MTAB *)calloc (2 + ((smart_autosizers == 0) * (sizeof (autos)/sizeof (autos[0]))) + (1 + (sizeof (autos)/sizeof (autos[0]))) * (drives + aliases + (modifiers - (setters + dumb_autosizers))), sizeof (MTAB));
    l = 0;
    for (j = 0; mtab[j].mask != 0; j++) {
        if ((((mtab[j].pstring != NULL) &&
              ((strcasecmp (mtab[j].pstring, "AUTOSIZE") == 0)   ||
               (strcasecmp (mtab[j].pstring, "NOAUTOSIZE") == 0))) ||
             ((mtab[j].mstring != NULL) &&
              ((strcasecmp (mtab[j].mstring, "AUTOSIZE") == 0)   ||
               (strcasecmp (mtab[j].mstring, "NOAUTOSIZE") == 0)))) &&
            ((mtab[j].mask & (MTAB_XTD|MTAB_VUN)) == 0))
             continue;          /* skip dumb autosizers */
        for (k = 0; drive[k].name != NULL; k++) {
            if ((mtab[j].mstring == NULL) ||
                (strncasecmp (mtab[j].mstring, drive[k].name, strlen (drive[k].name))))
                continue;
            break;
            }
        if ((k == drives) &&
            ((validator == NULL) || (validator != mtab[j].valid)) &&
            ((mtab[j].mask != (MTAB_XTD|MTAB_VUN)) ||
             (mtab[j].pstring == NULL)             ||
             (strcasecmp (mtab[j].pstring, "TYPE") != 0))) {
            deb_MTAB (&mtab[j]);
            nmtab[l++] = mtab[j];
            if (smart_autosizer == j) {
                for (k = 0; k < (sizeof (autoz)/sizeof (autoz[0])); k++) {
                    deb_MTAB (&autoz[k]);
                    nmtab[l++] = autoz[k];
                    }
                }
            continue;
            }
        }
    if (smart_autosizers == 0) {
        for (k = 0; k < (sizeof (autos)/sizeof (autos[0])); k++) {
            deb_MTAB (&autos[k]);
            nmtab[l++] = autos[k];
            }
        for (k = 0; k < (sizeof (autoz)/sizeof (autoz[0])); k++) {
            deb_MTAB (&autoz[k]);
            nmtab[l++] = autoz[k];
            }
        }
    for (k = 0; k < drives; k++) {
        char *hlp = (char *)malloc (CBUFSIZE);
        char *mstring = (char *)malloc (CBUFSIZE);

        nmtab[l].mask = MTAB_XTD|MTAB_VUN;
        nmtab[l].match = k;
        nmtab[l].pstring = NULL;
        nmtab[l].mstring = mstring;
        nmtab[l].valid = &sim_disk_set_drive_type;
        nmtab[l].disp = NULL;
        nmtab[l].desc = NULL;
        nmtab[l].help = hlp;
        ++l;
        if (drive[k].flags & DRVFL_SETSIZE) {
            snprintf (mstring, CBUFSIZE, "%s=SizeInMB", drive[k].name);
            snprintf (hlp, CBUFSIZE, "Set %s Disk Type and its size", drive[k].name);
            }
        else {
            strlcpy (mstring, drive[k].name, CBUFSIZE);
            if (drive[k].name_desc == NULL) {
                if (drive[k].devtype == SCSI_TAPE)
                    snprintf (hlp, CBUFSIZE, "Set %s%s%s Tape Type", drive[k].manufacturer ? drive[k].manufacturer : "", drive[k].manufacturer ? " " : "", drive[k].name);
                else
                    snprintf (hlp, CBUFSIZE, "Set %s%s%s Disk Type", drive[k].manufacturer ? drive[k].manufacturer : "", drive[k].manufacturer ? " " : "", drive[k].name);
                }
            else
                strlcpy (hlp, drive[k].name_desc, CBUFSIZE);
            }
        deb_MTAB (&nmtab[l-1]);
        if (drive[k].name_alias != NULL) {
            hlp = (char *)malloc (CBUFSIZE);
            mstring = (char *)malloc (CBUFSIZE);
            nmtab[l].mask = MTAB_XTD|MTAB_VUN;
            nmtab[l].match = k;
            nmtab[l].pstring = NULL;
            nmtab[l].mstring = mstring;
            nmtab[l].valid = &sim_disk_set_drive_type;
            nmtab[l].disp = NULL;
            nmtab[l].desc = NULL;
            nmtab[l].help = hlp;
            ++l;
            if (drive[k].flags & DRVFL_SETSIZE) {
                snprintf (mstring, CBUFSIZE, "%s=SizeInMB", drive[k].name_alias);
                snprintf (hlp, CBUFSIZE, "Set %s Disk Type and its size", drive[k].name_alias);
                }
            else {
                strlcpy (mstring, drive[k].name_alias, CBUFSIZE);
                if (drive[k].name_desc == NULL)
                    snprintf (hlp, CBUFSIZE, "Set %s Disk Type", drive[k].name_alias);
                else
                    strlcpy (hlp, drive[k].name_desc, CBUFSIZE);
                }
            deb_MTAB (&nmtab[l-1]);
            }
        }
    if (show_type_entry == -1) {
        nmtab[l].mask = MTAB_XTD|MTAB_VUN;
        nmtab[l].match = k;
        nmtab[l].pstring = "TYPE";
        nmtab[l].mstring = NULL;
        nmtab[l].valid = NULL;
        nmtab[l].disp = &sim_disk_show_drive_type;
        nmtab[l].desc = NULL;
        nmtab[l].help = "Display device type";
        deb_MTAB (&nmtab[l]);
        }
    /* replace the original modifier table with the revised one */
    dptr->modifiers = nmtab;
    sim_debug (SIM_DBG_INIT, &sim_scp_dev, "Updated MTAB table with %d entries\n", l);
    }
sim_show_message = FALSE;
sim_disk_set_all_noautosize (FALSE, NULL);
sim_show_message = saved_sim_show_message;
return stat;
}

/*
 * Zap Type command to remove incorrectly autosize information that
 * may have been recorded at the end of a disk container file
 */

typedef struct {
    t_stat stat;
    int32 flag;
    } DISK_INFO_CTX;

static t_bool sim_disk_check_attached_container (const char *filename, UNIT **auptr)
{
DEVICE *dptr;
UNIT *uptr;
uint32 i, j;
struct stat filestat;
char *fullname;

errno = 0;
if (auptr != NULL)
    *auptr = NULL;
if (sim_stat (filename, &filestat))
    return FALSE;
fullname = sim_filepath_parts (filename, "f");
if (fullname == NULL)
    return FALSE;                                        /* can't expand path assume attached */

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    if (0 == (dptr->flags & DEV_DISK))
        continue;                                       /* Only interested in disk devices */
    for (j = 0; j < dptr->numunits; j++) {              /* loop thru units */
        uptr = (dptr->units) + j;
        if (uptr->flags & UNIT_ATT) {                   /* attached? */
            struct stat statb;
            char *fullpath;

            errno = 0;
            fullpath = sim_filepath_parts (uptr->filename, "f");
            if (fullpath == NULL)
                continue;
            if (0 != strcasecmp (fullname, fullpath)) {
                free (fullpath);
                continue;
                }
            if (sim_stat (fullpath, &statb)) {
                free (fullpath);
                free (fullname);
                if (auptr != NULL)
                    *auptr = uptr;
                return TRUE;                            /* can't stat assume attached */
                }
            free (fullpath);
            if ((statb.st_dev   != filestat.st_dev)   ||
                (statb.st_ino   != filestat.st_ino)   ||
                (statb.st_mode  != filestat.st_mode)  ||
                (statb.st_nlink != filestat.st_nlink) ||
                (statb.st_uid   != filestat.st_uid)   ||
                (statb.st_gid   != filestat.st_gid)   ||
                (statb.st_atime != filestat.st_atime) ||
                (statb.st_mtime != filestat.st_mtime) ||
                (statb.st_ctime != filestat.st_ctime))
                continue;
            free (fullname);
            if (auptr != NULL)
                *auptr = uptr;
            return TRUE;                                /* file currently attached */
            }
        }
    }
free (fullname);
return FALSE;                                           /* Not attached */
}

static void sim_disk_info_entry (const char *directory,
                                 const char *filename,
                                 t_offset FileSize,
                                 const struct stat *filestat,
                                 void *context)
{
DISK_INFO_CTX *info = (DISK_INFO_CTX *)context;
char FullPath[PATH_MAX + 1];
struct simh_disk_footer footer;
struct simh_disk_footer *f = &footer;
FILE *container;
t_offset container_size;

snprintf (FullPath, sizeof (FullPath), "%s%s", directory, filename);

if (info->flag) {        /* zap disk type */
    struct stat statb;

    if (sim_disk_check_attached_container (FullPath, NULL)) {
        info->stat = sim_messagef (SCPE_ALATT, "Cannot ZAP an attached disk container: %s\n", FullPath);
        return;
        }
    container = sim_vhd_disk_open (FullPath, "r");
    if (container != NULL) {
        sim_vhd_disk_close (container);
        info->stat = sim_messagef (SCPE_OPENERR, "Cannot change the disk type of a VHD container file: %s\n", FullPath);
        return;
        }
    if (sim_stat (FullPath, &statb)) {
        info->stat = sim_messagef (SCPE_OPENERR, "Cannot stat file: '%s' - %s\n", FullPath, strerror (errno));
        return;
        }
    container = sim_fopen (FullPath, "rb+");
    if (container == NULL) {
        info->stat = sim_messagef (SCPE_OPENERR, "Cannot open container file '%s' - %s\n", FullPath, strerror (errno));
        return;
        }
    container_size = sim_fsize_ex (container);
    if ((container_size != (t_offset)-1) && (container_size > (t_offset)sizeof (*f)) &&
        (sim_fseeko (container, container_size - sizeof (*f), SEEK_SET) == 0) &&
        (sizeof (*f) == sim_fread (f, 1, sizeof (*f), container))) {
        if ((memcmp (f->Signature, "simh", 4) == 0) &&
            (f->Checksum == NtoHl (eth_crc32 (0, f, sizeof (*f) - sizeof (f->Checksum))))) {
            uint8 *sector_data;
            uint8 *zero_sector;
            t_offset initial_container_size;
            size_t sector_size = NtoHl (f->SectorSize);
            t_offset highwater = (((t_offset)NtoHl (f->Highwater[0])) << 32) | ((t_offset)NtoHl (f->Highwater[1]));

            if (sector_size > 16384)        /* arbitrary upper limit */
                sector_size = 16384;
            /* determine whole sectors in original container size */
            /* By default we chop off the disk footer and trailing */
            /* zero sectors added since the footer was appended that */
            /* hadn't been written by normal disk operations. */
            highwater = (highwater + (sector_size - 1)) & (~(t_offset)(sector_size - 1));
            if (sim_switches & SWMASK ('Z'))    /* Unless -Z switch specified */
                highwater = 0;                  /* then removes all trailing zero sectors */
            container_size -= sizeof (*f);
            initial_container_size = container_size;
            stop_cpu = FALSE;
            if (container_size > highwater) {
                sector_data = (uint8 *)malloc (sector_size * sizeof (*sector_data));
                zero_sector = (uint8 *)calloc (sector_size, sizeof (*sector_data));
                sim_messagef (SCPE_OK, "Trimming trailing zero containing blocks back to lbn: %u\n", (uint32)(highwater / sector_size));
                while ((container_size > highwater) &&
                       (!stop_cpu)) {
                    if ((sim_fseeko (container, container_size - sector_size, SEEK_SET) != 0) ||
                        (sector_size != sim_fread (sector_data, 1, sector_size, container))   ||
                        (0 != memcmp (sector_data, zero_sector, sector_size)))
                        break;
                    if ((0 == (container_size % 1024*1024)))
                        sim_messagef (SCPE_OK, "Trimming trailing zero containing blocks at lbn: %u         \r", (uint32)(container_size / sector_size));
                    container_size -= sector_size;
                    }
                free (sector_data);
                free (zero_sector);
                }
            if (!stop_cpu) {
                if (container_size == initial_container_size) {
                    sim_messagef (SCPE_OK, "The container was previously completely written with user data\n");
                    }
                else {
                    sim_messagef (SCPE_OK, "Last zero containing block found at lbn: %u          \n", (uint32)(container_size / sector_size));
                    sim_messagef (SCPE_OK, "Trimmed %u zero containing sectors\n", (uint32)((initial_container_size - container_size) / sector_size));
                    }
                (void)sim_set_fsize (container, (t_addr)container_size);
                fclose (container);
                sim_set_file_times (FullPath, statb.st_atime, statb.st_mtime);
                info->stat = sim_messagef (SCPE_OK, "Disk Type Info Removed from container: '%s'\n", sim_relative_path (FullPath));
                }
            else {
                fclose (container);
                info->stat = sim_messagef (SCPE_ARG, "Canceled Disk Type Info Removal from container: '%s'\n", sim_relative_path (FullPath));
                }
            stop_cpu = FALSE;
            return;
            }
        }
    fclose (container);
    info->stat = sim_messagef (SCPE_ARG, "No footer found on disk container '%s'.\n", FullPath);
    stop_cpu = FALSE;
    return;
    }
if (info->flag == 0) {  /* DISKINFO */
    DEVICE device, *dptr = &device;
    UNIT unit, *uptr = &unit;
    struct disk_context disk_ctx;
    struct disk_context *ctx = &disk_ctx;
    t_offset (*size_function)(FILE *file);
    int (*close_function)(FILE *f);
    const char *(*parent_path_function)(FILE *f);
    t_offset container_size;
    int32 saved_switches = sim_switches;
    char indent[CBUFSIZE] = "";

    memset (&device, 0, sizeof (device));
    memset (&unit, 0, sizeof (unit));
    memset (&disk_ctx, 0, sizeof (disk_ctx));
    sim_switches |= SWMASK ('E') | SWMASK ('R');   /* Must exist, Read Only */
    uptr->flags |= UNIT_ATTABLE;
    uptr->disk_ctx = &disk_ctx;
    disk_ctx.capac_factor = 1;
    disk_ctx.dptr = uptr->dptr = dptr;
    sim_disk_set_fmt (uptr, 0, "VHD", NULL);
    container = sim_vhd_disk_open (FullPath, "rb");
    if (container == NULL) {
        sim_disk_set_fmt (uptr, 0, "SIMH", NULL);
        container = sim_fopen (FullPath, "rb");
        close_function = fclose;
        size_function = sim_fsize_ex;
        parent_path_function = NULL;
        }
    else {
        close_function = sim_vhd_disk_close;
        size_function = sim_vhd_disk_size;
        parent_path_function = sim_vhd_disk_parent_path;
        }
    if (container != NULL) {
        while (container != NULL) {
            container_size = size_function (container);
            uptr->filename = strdup (FullPath);
            uptr->fileref = container;
            uptr->flags |= UNIT_ATT | UNIT_RO;
            get_disk_footer (uptr, NULL);
            f = ctx->footer;
            if (f) {
                t_offset highwater_sector = (f->SectorSize == 0) ? (t_offset)-1 : ((((t_offset)NtoHl (f->Highwater[0])) << 32) | ((t_offset)NtoHl (f->Highwater[1]))) / NtoHl(f->SectorSize);

                sim_printf ("%sContainer:              %s\n"
                            "%s   Simulator:           %s\n"
                            "%s   DriveType:           %s\n"
                            "%s   SectorSize:          %u\n"
                            "%s   SectorCount:         %u\n"
                            "%s   ElementEncodingSize: %s\n"
                            "%s   AccessFormat:        %s%s\n"
                            "%s   CreationTime:        %s",
                            indent, sim_relative_path (uptr->filename),
                            indent, f->CreatingSimulator,
                            indent, f->DriveType,
                            indent, NtoHl(f->SectorSize),
                            indent, NtoHl (f->SectorCount),
                            indent, _disk_tranfer_encoding (NtoHl (f->ElementEncodingSize)),
                            indent, fmts[f->AccessFormat].name,
                            ((parent_path_function == NULL) || (*parent_path_function (container) == '\0')) ? "" : " - Differencing Disk",
                            indent, f->CreationTime);
                if (ctime (&filestat->st_mtime))
                    sim_printf ("%s   ModifyTime:          %s", indent, ctime (&filestat->st_mtime));
                if (f->DeviceName[0] != '\0')
                    sim_printf ("%s   DeviceName:          %s\n", indent, (char *)f->DeviceName);
                if (f->DataWidth != 0)
                    sim_printf ("%s   DataWidth:           %d bits\n", indent, NtoHl(f->DataWidth));
                if (f->MediaID != 0)
                    sim_printf ("%s   MediaID:             0x%08X (%s)\n", indent, NtoHl(f->MediaID), sim_disk_decode_mediaid (NtoHl(f->MediaID)));
                if (f->Geometry != 0) {
                    uint32 CHS = NtoHl (f->Geometry);

                    sim_printf ("%s   Geometry:            %u Cylinders, %u Heads, %u Sectors\n", indent, CHS >> 16, (CHS >> 8) & 0xFF, CHS & 0xFF);
                    }
                if (highwater_sector > 0)
                    sim_printf ("%s   HighwaterSector:     %u\n", indent, (uint32)highwater_sector);
                sim_printf ("%sContainer Size: %s bytes\n", indent, sim_fmt_numeric ((double)ctx->container_size));
                ctx->sector_size = NtoHl(f->SectorSize);
                ctx->xfer_encode_size = NtoHl (f->ElementEncodingSize);
                }
            else {
                sim_printf ("%sContainer Info metadata for '%s' unavailable\n", indent, sim_relative_path (uptr->filename));
                sim_printf ("%sContainer Size: %s bytes\n", indent, sim_fmt_numeric ((double)container_size));
                info->stat = SCPE_ARG|SCPE_NOMESSAGE;
                ctx->sector_size = 512;
                ctx->xfer_encode_size = 1;
                }
            sim_set_uname (uptr, "FILE");
            get_filesystem_size (uptr, NULL);
            free (uptr->filename);
            free (ctx->footer);
            ctx->footer = NULL;
            free (uptr->uname);
            uptr->uname = NULL;
            if (parent_path_function != NULL)
                strlcpy (FullPath, parent_path_function (container), sizeof (FullPath));
            close_function (container);
            container = NULL;
            if (parent_path_function != NULL) {
                container = sim_vhd_disk_open (FullPath, "rb");
                if (container != NULL) {
                    sim_printf ("%sDifferencing Disk Parent:\n", indent);
                    strlcat (indent, "    ", sizeof (indent));
                    }
                }
            }
        }
    else
        info->stat = sim_messagef (SCPE_OPENERR, "Cannot open container file '%s' - %s\n", FullPath, strerror (errno));
    sim_switches = saved_switches;
    return;
    }
}

t_stat sim_disk_info_cmd (int32 flag, CONST char *cptr)
{
DISK_INFO_CTX disk_info_state;
t_stat stat;

if ((!cptr) || (*cptr == 0))
    return SCPE_2FARG;
GET_SWITCHES (cptr);                                    /* get switches */
memset (&disk_info_state, 0, sizeof (disk_info_state));
disk_info_state.flag = flag;
stat = sim_dir_scan (cptr, sim_disk_info_entry, &disk_info_state);
if (stat == SCPE_OK)
    return disk_info_state.stat;
return sim_messagef (SCPE_OK, "No such file or directory: %s\n", cptr);
}

/*

MediaId

Is defined in the MSCP Basic Disk Functions Manual, page 4-37 to 4-38:

The media type identifier is a 32-bit number, and it's coded like this:
The high 25 bits are 5 characters, each coded with 5 bits. The low 7
bits is a binary coded 2 digits.

Looking at it, you have:
D0,D1,A0,A1,A2,N

For an RA81, it would be:

D0,D1 is the preferred device type name for the unit. In our case,
that would be "DU".
A0,A1,A2 is the name of the media used on the unit. In our case "RA".
N is the value of the two decimal digits, so 81 for this example.

And for letters, the coding is that A=1, B=2 and so on. 0 means the
character is not used.

So, again, for an RA81, we would get:

Decimal Values:        4,    21,    18,     1,     0,      81
Hex Values:            4,    15,    12,     1,     0,      51
Binary Values:     00100, 10101, 10010, 00001, 00000, 1010001
Hex 4 bit Nibbles:    2     5     6     4   1     0     5   1

The 32bit value of RA81_MED is 0x25641051

 */
const char *sim_disk_decode_mediaid (uint32 MediaId)
{
static char text[16];
char D0[2] = "";
char D1[2] = "";
char A0[2] = "";
char A1[2] = "";
char A2[2] = "";
uint32 byte;
char num[4];

byte = (MediaId >> 27) & 0x1F;
if (byte)
    snprintf (D0, sizeof (D0), "%c", ('A' - 1) + byte);
byte = (MediaId >> 22) & 0x1F;
if (byte)
    snprintf (D1, sizeof (D1), "%c", ('A' - 1) + byte);
byte = (MediaId >> 17) & 0x1F;
if (byte)
    snprintf (A0, sizeof (A0), "%c", ('A' - 1) + byte);
byte = (MediaId >> 12) & 0x1F;
if (byte)
    snprintf (A1, sizeof (A1), "%c", ('A' - 1) + byte);
byte = (MediaId >> 7) & 0x1F;
if (byte)
    snprintf (A2, sizeof (A2), "%c", ('A' - 1) + byte);
snprintf (num, sizeof (num), "%02d", MediaId & 0x7F);
snprintf (text, sizeof (text), "%s%s - %s%s%s%s", D0, D1, A0, A1, num, A2);
return text;
}

uint32 sim_disk_drive_type_to_mediaid (const char *drive_type, const char *device_type)
{
uint32 D0 = 0;
uint32 D1 = 0;
uint32 num = 0;
uint32 A0 = 0;
uint32 A1 = 0;
uint32 A2 = 0;

if (device_type == NULL)
    return 0;
if (isalpha (device_type[0]))
    D0 = toupper (device_type[0]) - 'A' + 1;
if (isalpha (device_type[1]))
    D1 = toupper (device_type[1]) - 'A' + 1;
if (isalpha (drive_type[0]))
    A0 = toupper (drive_type[0]) - 'A' + 1;
if (isalpha (drive_type[1]))
    A1 = toupper (drive_type[1]) - 'A' + 1;
if (isalpha (drive_type[strlen (drive_type) - 1]))
    A2 = toupper (drive_type[strlen (drive_type) - 1]) - 'A' + 1;
while (isalpha (*drive_type))
    ++drive_type;
if (isdigit (*drive_type))
    num = strtoul (drive_type, NULL, 10);
return (D0 << 27) | (D1 << 22) | (A0 << 17) | (A1 << 12) | (A2 << 7) | num;
}

uint32 sim_disk_get_mediaid (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
uint32 result = uptr->drvtyp ? uptr->drvtyp->MediaId : 0;

if ((ctx != NULL) && (ctx->media_id != 0))
    result = ctx->media_id;
return result;
}

t_stat sim_disk_set_drive_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
DEVICE *dptr = find_dev_from_unit (uptr);
DRVTYP *drives = (DRVTYP *)dptr->type_ctx;
uint32 cap;
uint32 max = sim_toffset_64? DRV_EMAXC: DRV_MAXC;
uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */
t_stat r;

if (drives == NULL)
    return SCPE_IERR;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (uptr->drvtyp != NULL) {
    if (((uptr->drvtyp->flags & DRVFL_NOCHNG) != 0) &&
        (strcasecmp (uptr->drvtyp->name, drives[val].name) != 0))
        return sim_messagef (SCPE_ARG, "%s: Can't change drive type\n", sim_uname (uptr));
    if (((uptr->drvtyp->flags & DRVFL_NORMV) != 0) &&
        ((drives[val].flags & DRVFL_RMV) != 0))
        return sim_messagef (SCPE_ARG, "%s: Can't change unit with a %s to a removable drive type: %s\n", sim_uname (uptr), uptr->drvtyp->name, drives[val].name);
    }
cap = (t_addr)drives[val].size;
if (((drives[val].flags & DRVFL_SETSIZE) != 0) && ((cptr == NULL) || (*cptr == '\0')))
    return sim_messagef (SCPE_ARG, "%s: Missing Drive size specifier: %s=nnn\n", sim_uname (uptr), (drives[val].name_alias != NULL) ? drives[val].name_alias : drives[val].name);
if (cptr) {
    if ((drives[val].flags & DRVFL_SETSIZE) == 0)
        return sim_messagef (SCPE_ARG, "%s: Unexpected argument: %s\n", sim_uname (uptr), cptr);
    cap = (uint32) get_uint (cptr, 10, 0xFFFFFFFF, &r);
    if ((sim_switches & SWMASK ('L')) == 0)
        cap = cap * ((sim_switches & SWMASK ('B')) ? 2048 : 1954);
    if ((r != SCPE_OK) || (cap < DRV_MINC) || (cap > max))
        return sim_messagef (SCPE_ARG, "%s: Unreasonable capacity: %u\n", sim_uname (uptr), cap);
    }
if ((uptr->drvtyp != NULL) &&
    (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI) &&
    (uptr->drvtyp->devtype != drives[val].devtype)) {
    sim_tape_set_fmt (uptr, 0, "SIMH", NULL);
    sim_disk_set_fmt (uptr, 0, "AUTO", NULL);
    sim_tape_set_chunk_mode (uptr, ((drives[val].devtype == SCSI_TAPE) &&
                                    (drives[val].flags & DRVFL_QICTAPE)) ? drives[val].sectsize : 0);
    }
uptr->drvtyp = &drives[val];
set_writelock (uptr, ((uptr->drvtyp->flags & DRVFL_RO) != 0), NULL, NULL);
if ((dptr->flags & DEV_SECTORS) == 0)
    cap *= uptr->drvtyp->sectsize / capac_factor;
uptr->capac = (t_addr)cap;
return SCPE_OK;
}

t_stat sim_disk_show_drive_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int toks = 0;

#define SEP (0 == ((toks++) % 4)) ? ",\n\t" : ", "
fprintf (st, "%s", uptr->drvtyp->name);
if ((uptr->flags & UNIT_ATT) != 0) {
    if (sim_disk_get_mediaid (uptr))
        fprintf (st, ", MediaID=(%s)", sim_disk_decode_mediaid (sim_disk_get_mediaid (uptr)));
    fprintf (st, "%ssectors=%u, heads=%u, cylinders=%u, sectorsize=%u", SEP,
                uptr->drvtyp->sect, uptr->drvtyp->surf, uptr->drvtyp->cyl, uptr->drvtyp->sectsize);
    toks += 3;
    if (sim_switches & SWMASK ('D')) {
        if (uptr->drvtyp->model)
            fprintf (st, "%smodel=%u", SEP, uptr->drvtyp->model);
        if (uptr->drvtyp->tpg)
            fprintf (st, "%stpg=%u", SEP, uptr->drvtyp->tpg);
        if (uptr->drvtyp->gpc)
            fprintf (st, "%sgpc=%u", SEP, uptr->drvtyp->gpc);
        if (uptr->drvtyp->xbn)
            fprintf (st, "%sxbn=%u", SEP, uptr->drvtyp->xbn);
        if (uptr->drvtyp->dbn)
            fprintf (st, "%sdbn=%u", SEP, uptr->drvtyp->dbn);
        if (uptr->drvtyp->rcts)
            fprintf (st, "%srcts=%u", SEP, uptr->drvtyp->rcts);
        if (uptr->drvtyp->rctc)
            fprintf (st, "%srctc=%u", SEP, uptr->drvtyp->rctc);
        if (uptr->drvtyp->rbn)
            fprintf (st, "%srbn=%u", SEP, uptr->drvtyp->rbn);
        if (uptr->drvtyp->rctc)
            fprintf (st, "%srctc=%u", SEP, uptr->drvtyp->rctc);
        if (uptr->drvtyp->rbn)
            fprintf (st, "%srbn=%u", SEP, uptr->drvtyp->rbn);
        if (uptr->drvtyp->cylp)
            fprintf (st, "%scylp=%u", SEP, uptr->drvtyp->cylp);
        if (uptr->drvtyp->cylr)
            fprintf (st, "%scylr=%u", SEP, uptr->drvtyp->cylr);
        if (uptr->drvtyp->ccs)
            fprintf (st, "%sccs=%u", SEP, uptr->drvtyp->ccs);
        if (DRVFL_GET_IFTYPE(uptr->drvtyp) == DRVFL_TYPE_SCSI)
            fprintf (st, "%sdevtype=%u", SEP, uptr->drvtyp->devtype);
        if (uptr->drvtyp->pqual)
            fprintf (st, "%spqual=%u", SEP, uptr->drvtyp->pqual);
        if (uptr->drvtyp->scsiver)
            fprintf (st, "%sscsiver=%u", SEP, uptr->drvtyp->scsiver);
        if (uptr->drvtyp->manufacturer)
            fprintf (st, "%smanufacturer=%s", SEP, uptr->drvtyp->manufacturer);
        if (uptr->drvtyp->product)
            fprintf (st, "%sproduct=%s", SEP, uptr->drvtyp->product);
        if (uptr->drvtyp->rev)
            fprintf (st, "%srev=%s", SEP, uptr->drvtyp->rev);
        if (uptr->drvtyp->gaplen)
            fprintf (st, "%sgaplen=%u", SEP, uptr->drvtyp->gaplen);
        }
    }
return SCPE_OK;
}

const char *sim_disk_drive_type_set_string (UNIT *uptr)
{
static char typestr[80];

if (uptr->drvtyp) {
    if ((uptr->drvtyp->flags & DRVFL_SETSIZE) != 0) {
        uint32 totsectors = (uint32)(((uptr->dptr->flags & DEV_SECTORS) == 0)
                                                  ? (uptr->capac / uptr->drvtyp->sectsize)
                                                  : uptr->capac);
        snprintf (typestr, sizeof (typestr), "-L %s=%u", uptr->drvtyp->name, totsectors);
        }
    else
        snprintf (typestr, sizeof (typestr), "%s", uptr->drvtyp->name);
    return typestr;
    }
return NULL;
}


t_stat sim_disk_set_drive_type_by_name (UNIT *uptr, const char *drive_type)
{
DEVICE *dptr;
char cmd[CBUFSIZE];
t_bool dev_disabled, unit_disabled;
t_stat r;

if (uptr == NULL)
    return SCPE_IERR;
if ((dptr = find_dev_from_unit (uptr)) == NULL)
    return SCPE_IERR;
dev_disabled = (dptr->flags & DEV_DIS);
unit_disabled = uptr->flags & UNIT_DIS;
dptr->flags &= ~DEV_DIS;                    /* Assure that the DEVICE and UNIT */
uptr->flags &= ~UNIT_DIS;                   /* are enabled so the SET command works */
snprintf (cmd, sizeof (cmd), "%s %s", sim_uname (uptr), drive_type);
r = set_cmd (0, cmd);
if (dev_disabled)                           /* restore DEVICE enabled state */
    dptr->flags |= DEV_DIS;
if (unit_disabled)                          /* restore UNIT enabled state */
    uptr->flags |= UNIT_DIS;
return r;
}

static DRVTYP *sim_disk_find_type (UNIT *uptr, const char *dtype)
{
int i, j;
DEVICE *dptr;

if ((uptr->drvtyp != NULL) && (strcasecmp (uptr->drvtyp->name, dtype) == 0))
    return uptr->drvtyp;
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    DRVTYP *drvtypes = (DRVTYP *)dptr->type_ctx;

    if (((DEV_TYPE(dptr) != DEV_DISK) && (DEV_TYPE(dptr) != DEV_SCSI)) ||
        (drvtypes == NULL))
        continue;
    for (j = 0; drvtypes[j].name; j++) {
        if (strcasecmp (drvtypes[j].name, dtype) == 0)
            return &drvtypes[j];
        }
    }
return NULL;
}


/* disk testing */

struct disk_test_coverage {
    t_lba total_sectors;
    uint32 max_xfer_size;
    t_seccnt max_xfer_sectors;
    uint32 wsetbits;
    uint32 *wbitmap;
    uint32 *data;
    };

static t_stat sim_disk_test_exercise (UNIT *uptr)
{
struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
struct disk_test_coverage *c = (struct disk_test_coverage *)calloc (1, sizeof (*c));
t_stat r = SCPE_OK;
uint32 uint32s_per_sector = (ctx->sector_size / sizeof (*c->data));
DEVICE *dptr = find_dev_from_unit (uptr);
uint32 capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* capacity units (quadword: 8, word: 2, byte: 1) */
uint32 tries = 0;
t_bool unexpected_data = FALSE;

if (!(uptr->flags & UNIT_RO)) { /* Only test drives open Read/Write - Read Only due to container larger than drive */
    c->max_xfer_size = 1024*1024;
    c->max_xfer_sectors = c->max_xfer_size / ctx->sector_size;
    c->total_sectors = (t_lba)((uptr->capac*capac_factor)/(ctx->sector_size/((dptr->flags & DEV_SECTORS) ? ((ctx->sector_size >=  512) ? 512 : ctx->sector_size): 1)));
    c->data = (uint32 *)malloc (c->max_xfer_size);
    c->wbitmap = (uint32 *)calloc ((c->total_sectors + 32)/32, sizeof (*c->wbitmap));
#define BITMAP_IS_SET(n) (c->wbitmap[(n) >> 5] & (1 << ((n) & 0x1f)))
#define SET_BITMAP(n) c->wbitmap[(n) >> 5] |= (1 << ((n) & 0x1f))
    /* Randomly populate the whole drive container with known data (sector # in each sector) */
    srand (0);
    while (c->wsetbits < c->total_sectors) {
        t_lba start_lba = (rand () % c->total_sectors);
        t_lba end_lba = start_lba + 1 + (rand () % (c->max_xfer_sectors - 1));
        t_lba lba;
        t_seccnt i, sectors_to_write, sectors_written;

        if (end_lba > c->total_sectors)
            end_lba = c->total_sectors;
        if (BITMAP_IS_SET(start_lba)) {
            ++tries;
            if (tries < 30)
                continue;
            while (BITMAP_IS_SET(start_lba))
                start_lba = (1 + start_lba) % c->total_sectors;
            end_lba = start_lba + 1;
            }
        tries = 0;
        for (lba = start_lba; lba < end_lba; lba++) {
            if (BITMAP_IS_SET(lba)) {
                end_lba = lba;
                break;
                }
            SET_BITMAP(lba);
            ++c->wsetbits;
            }
        sectors_to_write = end_lba - start_lba;
        for (i=0; i < sectors_to_write * uint32s_per_sector; i++)
            c->data[i] = start_lba + i / uint32s_per_sector;
        r = sim_disk_wrsect (uptr, start_lba, (uint8 *)c->data, &sectors_written, sectors_to_write);
        if (r != SCPE_OK) {
            sim_printf ("Error writing sectors %u thru %u: %s\n", start_lba, end_lba - 1, sim_error_text (r));
            break;
            }
        else {
            if (sectors_to_write != sectors_written) {
                sim_printf ("Unexpectedly wrote %u sectors instead of %u sectors starting at lba %u\n", sectors_written, sectors_to_write, start_lba);
                break;
                }
            }
        }
    if (r == SCPE_OK) {
        t_seccnt sectors_read, sectors_to_read, sector_to_check;
        t_lba lba;

        sim_printf("Writing OK\n");
        for (lba = 0; (lba < c->total_sectors) && (r == SCPE_OK); lba += sectors_read) {
            sectors_to_read = 1 + (rand () % (c->max_xfer_sectors - 1));
            if (lba + sectors_to_read > c->total_sectors)
                sectors_to_read = c->total_sectors - lba;
            r = sim_disk_rdsect (uptr, lba, (uint8 *)c->data, &sectors_read, sectors_to_read);
            if (r == SCPE_OK) {
                if (sectors_read != sectors_to_read) {
                    sim_printf ("Only returned %u sectors when reading %u sectors from lba %u\n", sectors_read, sectors_to_read, lba);
                    r = SCPE_INCOMP;
                    }
                }
            else
                sim_printf ("Error reading %u sectors at lba %u, %u read - %s\n", sectors_to_read, lba, sectors_read, sim_error_text (r));
            for (sector_to_check = 0; sector_to_check < sectors_read; ++sector_to_check) {
                uint32 i;

                for (i = 0; i < uint32s_per_sector; i++)
                    if (c->data[i + sector_to_check * uint32s_per_sector] != (lba + sector_to_check)) {
                        sim_printf ("Sector %u(0x%X) has unexpected data at offset 0x%X: 0x%08X\n",
                                    lba + sector_to_check, lba + sector_to_check, i, c->data[i + sector_to_check * uint32s_per_sector]);
                        unexpected_data = TRUE;
                        break;
                        }
                }
            }
        if ((r == SCPE_OK) && !unexpected_data)
            sim_printf("Reading OK\n");
        else {
            sim_printf("Reading BAD\n");
            r = SCPE_IERR;
            }
        }
    if (r == SCPE_OK) { /* If still good, then do EOF and beyond boundary test */
        t_offset current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? ctx->sector_size : 1);
        t_seccnt sectors_read, sectors_to_read;
        t_lba lba = (t_lba)(current_unit_size / ctx->sector_size) - 2;
        int i;

        sectors_to_read = 1;
        for (i = 0; (i < 4) && (r == SCPE_OK); i++) {
            r = sim_disk_rdsect (uptr, lba + i, (uint8 *)c->data, &sectors_read, sectors_to_read);
            if ((r != SCPE_OK) || (sectors_read != sectors_to_read))
                r = SCPE_IERR;
            }
        }
    }
free (c->data);
free (c->wbitmap);
free (c);
if (r == SCPE_OK) {
    char *filename = strdup (uptr->filename);

    sim_disk_detach (uptr);
    (void)remove (filename);
    free (filename);
    }
return r;
}

static t_stat _sim_disk_test_create (const char *container, size_t size)
{
FILE *f = fopen (container, "w");
t_stat r = SCPE_OPENERR;

if (NULL == f)
    return SCPE_OPENERR;
if (0 == sim_set_fsize (f, (t_addr)size))
    r = SCPE_OK;
fclose (f);
return r;
}

/* Autosizing and Meta data testing support. */
/* Only operate on specific disk cases: */
/* Device: */
/*   RL  - 2 different disk sizes to autosize between */
/*   RQ  - Arbitrary disk size change supported */
/*   RK  - 1 sized disk with reserved cylinders */
t_stat sim_disk_sizing_test (DEVICE *dptr, const char *cptr)
{
char filename[256] = "TestFile.dsk";
UNIT *uptr = &dptr->units[0];
t_stat r = SCPE_OK;
int specific_test = -1;
static struct {
    int32       switches;
    t_offset    container_size;
    t_bool      autosize_attach;
    t_offset    pseudo_fs_size;
    t_stat      exp_attach_status;
    t_stat      unit_ro_attach;
    t_bool      has_footer;
    const char *drive_type;
    const char *device_name;
    int         testid;
    } tests[] = {
#define DMB 1024*1024
#define DKB 1024
      /* switches   containersize        autosize      fs_size          status    ro_after    footer   type        device   testid*/
        {0,              4800*512,          FALSE,    4800*512,        SCPE_OK,      FALSE,     TRUE,    NULL,       "RK",    25},
        {0,              2436*DKB,          FALSE,    4800*512,        SCPE_OK,      FALSE,     TRUE,    NULL,       "RK",    24},
        {SWMASK ('R'),   2436*DKB,          FALSE,    4800*512,        SCPE_OK,       TRUE,    FALSE,    NULL,       "RK",    23},
        {0,                     0,          FALSE,    4800*512,        SCPE_OK,      FALSE,     TRUE,    NULL,       "RK",    22},
        {0,                 7*DMB,           TRUE,     500*DMB,        SCPE_OK,       TRUE,    FALSE, "RRD40",       "RQ",    21},
        {0,                 7*DMB,          FALSE,     500*DMB,        SCPE_OK,       TRUE,    FALSE, "RRD40",       "RQ",    20},
        {0,                 7*DMB,           TRUE,      20*DMB,        SCPE_OK,      FALSE,     TRUE,  "RD51",       "RQ",    19},
        {0,                 4*DMB,          FALSE,      20*DMB,    SCPE_FSSIZE,      FALSE,    FALSE,  "RD51",       "RQ",    18},
        {0,                30*DMB,          FALSE,           0,        SCPE_OK,       TRUE,    FALSE,  "RD51",       "RQ",    17},
        {0,                     0,          FALSE,           0,        SCPE_OK,      FALSE,     TRUE,  "RD51",       "RQ",    16},
        {SWMASK ('R'),     20*DKB,          FALSE,           0,        SCPE_OK,       TRUE,    FALSE,  "RD51",       "RQ",    15},
        {SWMASK ('R'),     20*DKB,          FALSE,           0,        SCPE_OK,       TRUE,    FALSE,  "RL01",       "RL",    14},
        {0,                20*DKB,          FALSE,      10*DMB,    SCPE_FSSIZE,      FALSE,    FALSE,  "RL01",       "RL",    13},
        {0,                20*DKB,           TRUE,      13*DMB,    SCPE_FSSIZE,      FALSE,    FALSE,  "RL01",       "RL",    12},
        {SWMASK ('Y'),          0,           TRUE,           0,        SCPE_OK,      FALSE,     TRUE,  "RL01",       "RL",    11},
        {0,                20*DMB,           TRUE,           0,    SCPE_FSSIZE,      FALSE,    FALSE,  "RL01",       "RL",    10},
        {0,                20*DKB,           TRUE,           0,        SCPE_OK,      FALSE,    FALSE,  "RL01",       "RL",     9},
        {0,                20*DKB,           TRUE,       5*DMB,        SCPE_OK,      FALSE,     TRUE,  "RL01",       "RL",     8},
        {0,                20*DMB,          FALSE,           0,        SCPE_OK,       TRUE,    FALSE,  "RL01",       "RL",     7},
        {0,                20*DKB,          FALSE,           0,        SCPE_OK,      FALSE,    FALSE,  "RL01",       "RL",     6},
        {0,                20*DKB,          FALSE,       5*DMB,        SCPE_OK,      FALSE,     TRUE,  "RL01",       "RL",     5},
        {SWMASK ('Y'),          0,          FALSE,           0,        SCPE_OK,      FALSE,     TRUE,  "RL02",       "RL",     4},
        {0,                20*DMB,          FALSE,           0,        SCPE_OK,       TRUE,    FALSE,  "RL02",       "RL",     3},
        {0,                20*DKB,          FALSE,           0,        SCPE_OK,      FALSE,    FALSE,  "RL02",       "RL",     2},
        {0,                20*DKB,          FALSE,       5*DMB,        SCPE_OK,      FALSE,    FALSE,  "RL02",       "RL",     1},
        {0,0,0,0,0,0}
    };

sim_printf ("\n*** Container sizing behavior tests\n");
if ((cptr != NULL) && (isdigit (*cptr)))
    specific_test = atoi (cptr);
if ((0 == strcmp ("RL", dptr->name)) ||
    (0 == strcmp ("RQ", dptr->name)) ||
    (0 == strcmp ("RK", dptr->name))) {
    int i;

    (void)remove (filename);
    for (i=0; tests[i].device_name != NULL; i++) {
        char cmd[32];

        if (0 != strcmp (tests[i].device_name, dptr->name))
            continue;
        if ((specific_test != -1) && (tests[i].testid != specific_test))
            continue;
        sim_printf ("%d : Attaching %s with a %s byte container\n", tests[i].testid, sim_uname (uptr), sim_fmt_numeric ((double)tests[i].container_size));
        sim_printf ("%d : Device Type: %s, %sAutoSize\n", tests[i].testid, tests[i].drive_type ? tests[i].drive_type : tests[i].device_name, tests[i].autosize_attach ? "" : "No");
        if (tests[i].pseudo_fs_size)
            sim_printf ("%d : File System Size: %s\n", tests[i].testid, sim_fmt_numeric ((double)tests[i].pseudo_fs_size));
        else
            sim_printf ("%d : No File System\n", tests[i].testid);
        sim_switches = tests[i].switches;
        sprintf(cmd, "%s %sAUTOSIZE", sim_uname (uptr), tests[i].autosize_attach ? "" : "NO");
        set_cmd (0, cmd);
        if (tests[i].drive_type != NULL) {
            sprintf(cmd, "%s %s", sim_uname (uptr), tests[i].drive_type);
            set_cmd (0, cmd);
            }
        if (tests[i].container_size)
            _sim_disk_test_create (filename, (size_t)tests[i].container_size);
        pseudo_filesystem_size = tests[i].pseudo_fs_size;
        r = dptr->attach (uptr, filename);
        sim_printf ("%d: %s attach status. Expected: %s, Got: %s\n", tests[i].testid, (SCPE_BARE_STATUS (r) == SCPE_OK) ? "Success" : "Failure", sim_error_text (tests[i].exp_attach_status), sim_error_text (r));
        if (SCPE_BARE_STATUS (r) == SCPE_OK)
            show_cmd (0, sim_uname (uptr));
        if (SCPE_BARE_STATUS (r) != tests[i].exp_attach_status) {
            if (SCPE_BARE_STATUS (r) == SCPE_OK)
                sim_printf ("%d: UNEXPECTED Success attach status. Expected: %s\n", tests[i].testid, sim_error_text (tests[i].exp_attach_status));
            else
                sim_printf ("%d: UNEXPECTED Failure attach status: %s\n", tests[i].testid, sim_error_text (SCPE_BARE_STATUS (r)));
            return SCPE_INCOMP;
            }
        if ((SCPE_BARE_STATUS (r) == SCPE_OK) && (((uptr->flags & UNIT_RO) != 0) != tests[i].unit_ro_attach))
            r = SCPE_OK; /* Error */
        sim_disk_detach (uptr);
        if ((SCPE_OK == sim_disk_info_cmd (0, filename)) != tests[i].has_footer) {
            if (tests[i].has_footer)
                sim_printf ("%d: Expected metadata missing\n", tests[i].testid);
            else
                sim_printf ("%d: Unexpected metadata found\n", tests[i].testid);
            return SCPE_INCOMP;
            }
        pseudo_filesystem_size = 0;
        (void)remove (filename);
        sim_printf ("\n");
        }
    }
return r;
}

t_stat sim_disk_meta_attach_test (DEVICE *dptr, const char *cptr)
{
char **tarfiles = sim_get_filelist ("../Test-Disks/*.tar.gz");
char cmd[CBUFSIZE];

sim_printf ("\n*** Containers with meta data tests\n");
if (tarfiles == NULL)
    tarfiles = sim_get_filelist ("./Test-Disks/*.tar.gz");
if (tarfiles == NULL)
    tarfiles = sim_get_filelist ("./Visual Studio Projects/Test-Disks/*.tar.gz");
sim_printf ("Tar File containing test disk images: ");
sim_print_filelist (tarfiles);
if (tarfiles) {
    char **dskfiles = sim_get_filelist ("*.dsk.meta");

    if (dskfiles != NULL) {
        sim_printf ("Existing test disk containers:\n");
        sim_print_filelist (dskfiles);
        }
    else {
        sim_printf ("Extracting test disk containers from: %s\n", tarfiles[0]);
        if (strchr (tarfiles[0], ' '))
            snprintf (cmd, sizeof (cmd), "xf \"%s\"", tarfiles[0]);
        else
            snprintf (cmd, sizeof (cmd), "xf %s", tarfiles[0]);
        tar_cmd (0, cmd);
        dskfiles = sim_get_filelist ("*.dsk.meta");
        sim_printf ("Extracted test disk containers:\n");
        sim_print_filelist (dskfiles);
        }
    if (dskfiles == NULL) {
        sim_printf ("No test disk meta data containers were found.\n");
        }
    else {
        int i;
        UNIT *uptr = &dptr->units[0];
        t_stat r;

        for (i = 0; dskfiles[i] != NULL; i++) {
            char *drive_type = sim_filepath_parts (dskfiles[i], "n");
            const char *specific_file = ((cptr != NULL) && (*cptr != '\0') && (!isdigit (*cptr))) ? cptr : NULL;
            char *file_name = sim_filepath_parts (dskfiles[i], "nx");

            r = SCPE_OK;
            if ((specific_file == NULL) || (0 == strcasecmp (file_name, specific_file))) {
                if (strchr (drive_type, '.'))
                    *(strchr (drive_type, '.')) = '\0';
                sim_printf ("\n");
                sim_printf ("Attaching %s disk image '%s' to %s...\n", drive_type, dskfiles[i], sim_uname (uptr));
                sim_disk_info_cmd (0, dskfiles[i]);
                snprintf (cmd, sizeof (cmd), "%s RD51", sim_uname (uptr));
                set_cmd (0, "NOMESSAGE");
                set_cmd (0, cmd);
                set_cmd (0, "MESSAGE");
                snprintf (cmd, sizeof (cmd), "%s %s", sim_uname (uptr), drive_type);
                sim_switches = 0;
                set_cmd (0, cmd);
                r = dptr->attach (uptr, dskfiles[i]);
                sim_disk_detach (uptr);
                sim_printf ("%s: %s read/write attach status: %s\n", drive_type, (SCPE_BARE_STATUS (r) == SCPE_OK) ? "Success" : "Failure", sim_error_text (r));
                if ((SCPE_BARE_STATUS (r) != SCPE_OK)      &&
                    (SCPE_BARE_STATUS (r) != SCPE_OPENERR) &&
                    (SCPE_BARE_STATUS (r) != SCPE_INCOMPDSK)) {
                    free (drive_type);
                    free (file_name);
                    return r;
                    }
                sim_switches = SWMASK ('R');
                r = dptr->attach (uptr, dskfiles[i]);
                sim_disk_detach (uptr);
                sim_printf ("%s: %s read only attach status: %s\n", drive_type, (SCPE_BARE_STATUS (r) == SCPE_OK) ? "Success" : "Failure", sim_error_text (r));
                if ((SCPE_BARE_STATUS (r) != SCPE_OK)      &&
                    (SCPE_BARE_STATUS (r) != SCPE_OPENERR) &&
                    (SCPE_BARE_STATUS (r) != SCPE_INCOMPDSK)) {
                    free (drive_type);
                    free (file_name);
                    return r;
                    }
                }
            free (drive_type);
            free (file_name);
            }
        }
    sim_free_filelist (&dskfiles);
    }
sim_free_filelist (&tarfiles);
return SCPE_OK;
}

t_stat sim_disk_test (DEVICE *dptr, const char *cptr)
{
const char *fmt[] = {"RAW", "VHD", "VHD", "SIMH", NULL};
uint32 sect_size[] = {576, 4096, 1024, 512, 256, 128, 64, 0};
uint32 xfr_size[] = {1, 2, 4, 8, 0};
int x, s, f;
UNIT *uptr = &dptr->units[0];
char filename[256];
t_stat r;
int32 saved_switches = sim_switches & ~SWMASK('T');
SIM_TEST_INIT;

if (sim_switches & SWMASK ('M')) { /* Do meta first? */
    sim_switches = saved_switches &= ~SWMASK ('M');
    SIM_TEST (sim_disk_meta_attach_test (dptr, cptr));
    SIM_TEST (sim_disk_sizing_test (dptr, cptr));
    }
else {
    SIM_TEST (sim_disk_sizing_test (dptr, cptr));
    SIM_TEST (sim_disk_meta_attach_test (dptr, cptr));
    }
sim_printf ("\n*** Disk Format combination behavior tests\n");
for (x = 0; xfr_size[x] != 0; x++) {
    for (f = 0; fmt[f] != 0; f++) {
        for (s = 0; sect_size[s] != 0; s++) {
            snprintf (filename, sizeof (filename), "Test-%u-%u.%s", sect_size[s], xfr_size[x], fmt[f]);
            if ((f > 0) && (strcmp (fmt[f], "VHD") == 0) && (strcmp (fmt[f - 1], "VHD") == 0)) { /* Second VHD is Fixed */
                sim_switches |= SWMASK('X');
                snprintf (filename, sizeof (filename), "Test-%u-%u-Fixed.%s", sect_size[s], xfr_size[x], fmt[f]);
                }
            else
                sim_switches = saved_switches;
            (void)remove (filename);        /* Remove any prior remnants */
            r = sim_disk_set_fmt (uptr, 0, fmt[f], NULL);
            if (r != SCPE_OK)
                break;
            sim_printf ("Testing %s (%s) using %s\n", sim_uname (uptr), sprint_capac (dptr, uptr), filename);
            if (strcmp (fmt[f], "RAW") == 0) {
                /* There is no innate creation of RAW containers, so create the empty container using SIMH format */
                sim_disk_set_fmt (uptr, 0, "SIMH", NULL);
                sim_disk_attach_ex (uptr, filename, sect_size[s], xfr_size[x], TRUE, 0, NULL, 0, 0, NULL);
                sim_disk_detach (uptr);
                sim_disk_set_fmt (uptr, 0, fmt[f], NULL);
                }
            r = sim_disk_attach_ex (uptr, filename, sect_size[s], xfr_size[x], TRUE, 0, NULL, 0, 0, NULL);
            if ((r != SCPE_OK) &&
                (SCPE_BARE_STATUS (r) != SCPE_INCOMPDSK))
                break;
            if (r == SCPE_OK)
                SIM_TEST (sim_disk_test_exercise (uptr));
            }
        }
    }
return SCPE_OK;
}
