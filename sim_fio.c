/* sim_fio.c: simulator file I/O library

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

   03-Jun-11    MP      Simplified VMS 64b support and made more portable
   02-Feb-11    MP      Added sim_fsize_ex and sim_fsize_name_ex returning t_addr
                        Added export of sim_buf_copy_swapped and sim_buf_swap_data
   28-Jun-07    RMS     Added VMS IA64 support (from Norm Lastovica)
   10-Jul-06    RMS     Fixed linux conditionalization (from Chaskiel Grundman)
   15-May-06    RMS     Added sim_fsize_name
   21-Apr-06    RMS     Added FreeBSD large file support (from Mark Martinec)
   19-Nov-05    RMS     Added OS/X large file support (from Peter Schorn)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   17-Jul-04    RMS     Fixed bug in optimized sim_fread (reported by Scott Bailey)
   26-May-04    RMS     Optimized sim_fread (suggested by John Dundas)
   02-Jan-04    RMS     Split out from SCP

   This library includes:

   sim_finit         -       initialize package
   sim_fopen         -       open file
   sim_fread         -       endian independent read (formerly fxread)
   sim_fwrite        -       endian independent write (formerly fxwrite)
   sim_fseek         -       conditionally extended (>32b) seek (
   sim_fseeko        -       extended seek (>32b if available)
   sim_fsize         -       get file size
   sim_fsize_name    -       get file size of named file
   sim_fsize_ex      -       get file size as a t_offset
   sim_fsize_name_ex -       get file size as a t_offset of named file
   sim_buf_copy_swapped -    copy data swapping elements along the way
   sim_buf_swap_data -       swap data elements inplace in buffer
   sim_shmem_open            create or attach to a shared memory region
   sim_shmem_close           close a shared memory region


   sim_fopen and sim_fseek are OS-dependent.  The other routines are not.
   sim_fsize is always a 32b routine (it is used only with small capacity random
   access devices like fixed head disks and DECtapes).
*/

#include "sim_defs.h"

t_bool sim_end;                     /* TRUE = little endian, FALSE = big endian */
t_bool sim_taddr_64;                /* t_addr is > 32b and Large File Support available */
t_bool sim_toffset_64;              /* Large File (>2GB) file I/O Support available */

#if defined(fprintf)                /* Make sure to only use the C rtl stream I/O routines */
#undef fprintf
#undef fputs
#undef fputc
#endif

#ifndef MAX
#define MAX(a,b)  (((a) >= (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#endif

/* OS-independent, endian independent binary I/O package

   For consistency, all binary data read and written by the simulator
   is stored in little endian data order.  That is, in a multi-byte
   data item, the bytes are written out right to left, low order byte
   to high order byte.  On a big endian host, data is read and written
   from high byte to low byte.  Consequently, data written on a little
   endian system must be byte reversed to be usable on a big endian
   system, and vice versa.

   These routines are analogs of the standard C runtime routines
   fread and fwrite.  If the host is little endian, or the data items
   are size char, then the calls are passed directly to fread or
   fwrite.  Otherwise, these routines perform the necessary byte swaps.
   Sim_fread swaps in place, sim_fwrite uses an intermediate buffer.
*/

int32 sim_finit (void)
{
union {int32 i; char c[sizeof (int32)]; } end_test;

end_test.i = 1;                                         /* test endian-ness */
sim_end = (end_test.c[0] != 0);
sim_toffset_64 = (sizeof(t_offset) > sizeof(int32));    /* Large File (>2GB) support */
sim_taddr_64 = sim_toffset_64 && (sizeof(t_addr) > sizeof(int32));
return sim_end;
}

void sim_buf_swap_data (void *bptr, size_t size, size_t count)
{
uint32 j;
int32 k;
unsigned char by, *sptr, *dptr;

if (sim_end || (count == 0) || (size == sizeof (char)))
    return;
for (j = 0, dptr = sptr = (unsigned char *) bptr;       /* loop on items */
     j < count; j++) { 
    for (k = (int32)(size - 1); k >= (((int32) size + 1) / 2); k--) {
        by = *sptr;                                     /* swap end-for-end */
        *sptr++ = *(dptr + k);
        *(dptr + k) = by;
        }
    sptr = dptr = dptr + size;                          /* next item */
    }
}

size_t sim_fread (void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c;

if ((size == 0) || (count == 0))                        /* check arguments */
    return 0;
c = fread (bptr, size, count, fptr);                    /* read buffer */
if (sim_end || (size == sizeof (char)) || (c == 0))     /* le, byte, or err? */
    return c;                                           /* done */
sim_buf_swap_data (bptr, size, count);
return c;
}

void sim_buf_copy_swapped (void *dbuf, const void *sbuf, size_t size, size_t count)
{
size_t j;
int32 k;
const unsigned char *sptr = (const unsigned char *)sbuf;
unsigned char *dptr = (unsigned char *)dbuf;

if (sim_end || (size == sizeof (char))) {
    memcpy (dptr, sptr, size * count);
    return;
    }
for (j = 0; j < count; j++) {                           /* loop on items */
    for (k = (int32)(size - 1); k >= 0; k--)
        *(dptr + k) = *sptr++;
    dptr = dptr + size;
    }
}

size_t sim_fwrite (const void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c, nelem, nbuf, lcnt, total;
int32 i;
const unsigned char *sptr;
unsigned char *sim_flip;

if ((size == 0) || (count == 0))                        /* check arguments */
    return 0;
if (sim_end || (size == sizeof (char)))                 /* le or byte? */
    return fwrite (bptr, size, count, fptr);            /* done */
sim_flip = (unsigned char *)malloc(FLIP_SIZE);
if (!sim_flip)
    return 0;
nelem = FLIP_SIZE / size;                               /* elements in buffer */
nbuf = count / nelem;                                   /* number buffers */
lcnt = count % nelem;                                   /* count in last buf */
if (lcnt) nbuf = nbuf + 1;
else lcnt = nelem;
total = 0;
sptr = (const unsigned char *) bptr;                    /* init input ptr */
for (i = (int32)nbuf; i > 0; i--) {                     /* loop on buffers */
    c = (i == 1)? lcnt: nelem;
    sim_buf_copy_swapped (sim_flip, sptr, size, c);
    sptr = sptr + size * count;
    c = fwrite (sim_flip, size, c, fptr);
    if (c == 0) {
        free(sim_flip);
        return total;
        }
    total = total + c;
    }
free(sim_flip);
return total;
}

/* Forward Declaration */

t_offset sim_ftell (FILE *st);

/* Get file size */

t_offset sim_fsize_ex (FILE *fp)
{
t_offset pos, sz;

if (fp == NULL)
    return 0;
pos = sim_ftell (fp);
if (sim_fseeko (fp, 0, SEEK_END))
    return 0;
sz = sim_ftell (fp);
if (sim_fseeko (fp, pos, SEEK_SET))
    return 0;
return sz;
}

t_offset sim_fsize_name_ex (const char *fname)
{
FILE *fp;
t_offset sz;

if ((fp = sim_fopen (fname, "rb")) == NULL)
    return 0;
sz = sim_fsize_ex (fp);
fclose (fp);
return sz;
}

uint32 sim_fsize_name (const char *fname)
{
return (uint32)(sim_fsize_name_ex (fname));
}

uint32 sim_fsize (FILE *fp)
{
return (uint32)(sim_fsize_ex (fp));
}

/* OS-dependent routines */

/* Optimized file open */

FILE *sim_fopen (const char *file, const char *mode)
{
#if defined (VMS)
return fopen (file, mode, "ALQ=32", "DEQ=4096",
        "MBF=6", "MBC=127", "FOP=cbt,tef", "ROP=rah,wbh", "CTX=stm");
#elif (defined (__linux) || defined (__linux__) || defined (__hpux) || defined (_AIX)) && !defined (DONT_DO_LARGEFILE)
return fopen64 (file, mode);
#else
return fopen (file, mode);
#endif
}

#if !defined (DONT_DO_LARGEFILE)
/* 64b VMS */

#if ((defined (__ALPHA) || defined (__ia64)) && defined (VMS) && (__DECC_VER >= 60590001)) || \
    ((defined(__sun) || defined(__sun__)) && defined(_LARGEFILE_SOURCE))
#define S_SIM_IO_FSEEK_EXT_ 1
int sim_fseeko (FILE *st, t_offset offset, int whence)
{
return fseeko (st, (off_t)offset, whence);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftello (st));
}

#endif

/* Alpha UNIX - natively 64b */

#if defined (__ALPHA) && defined (__unix__)             /* Alpha UNIX */
#define S_SIM_IO_FSEEK_EXT_ 1
int sim_fseeko (FILE *st, t_offset offset, int whence)
{
return fseek (st, offset, whence);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftell (st));
}

#endif

/* Windows */

#if defined (_WIN32)
#define S_SIM_IO_FSEEK_EXT_ 1
#include <sys/stat.h>

int sim_fseeko (FILE *st, t_offset offset, int whence)
{
return _fseeki64 (st, (__int64)offset, whence);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)_ftelli64 (st);
}

#endif                                                  /* end Windows */

/* Linux */

#if defined (__linux) || defined (__linux__) || defined (__hpux) || defined (_AIX)
#define S_SIM_IO_FSEEK_EXT_ 1
int sim_fseeko (FILE *st, t_offset xpos, int origin)
{
return fseeko64 (st, (off64_t)xpos, origin);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftello64 (st));
}

#endif                                                  /* end Linux with LFS */

/* Apple OS/X */

#if defined (__APPLE__) || defined (__FreeBSD__) || defined(__NetBSD__) || defined (__OpenBSD__) || defined (__CYGWIN__) 
#define S_SIM_IO_FSEEK_EXT_ 1
int sim_fseeko (FILE *st, t_offset xpos, int origin) 
{
return fseeko (st, (off_t)xpos, origin);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftello (st));
}

#endif  /* end Apple OS/X */
#endif /* !DONT_DO_LARGEFILE */

/* Default: no OS-specific routine has been defined */

#if !defined (S_SIM_IO_FSEEK_EXT_)
int sim_fseeko (FILE *st, t_offset xpos, int origin)
{
return fseek (st, (long) xpos, origin);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftell (st));
}
#endif

int sim_fseek (FILE *st, t_addr offset, int whence)
{
return sim_fseeko (st, (t_offset)offset, whence);
}

#if defined(_WIN32)
const char *
sim_get_os_error_text (int Error)
{
static char szMsgBuffer[2048];
DWORD dwStatus;

dwStatus = FormatMessageA (FORMAT_MESSAGE_FROM_SYSTEM|
                           FORMAT_MESSAGE_IGNORE_INSERTS,     //  __in      DWORD dwFlags,
                           NULL,                              //  __in_opt  LPCVOID lpSource,
                           Error,                             //  __in      DWORD dwMessageId,
                           0,                                 //  __in      DWORD dwLanguageId,
                           szMsgBuffer,                       //  __out     LPTSTR lpBuffer,
                           sizeof (szMsgBuffer) -1,           //  __in      DWORD nSize,
                           NULL);                             //  __in_opt  va_list *Arguments
if (0 == dwStatus)
    snprintf(szMsgBuffer, sizeof(szMsgBuffer) - 1, "Error Code: 0x%X", Error);
while (sim_isspace (szMsgBuffer[strlen (szMsgBuffer)-1]))
    szMsgBuffer[strlen (szMsgBuffer) - 1] = '\0';
return szMsgBuffer;
}

t_stat sim_copyfile (const char *source_file, const char *dest_file, t_bool overwrite_existing)
{
if (CopyFileA (source_file, dest_file, !overwrite_existing))
    return SCPE_OK;
return sim_messagef (SCPE_ARG, "Error Copying '%s' to '%s': %s\n", source_file, dest_file, sim_get_os_error_text (GetLastError ()));
}

#include <io.h>
#include <direct.h>
int sim_set_fsize (FILE *fptr, t_addr size)
{
return _chsize(_fileno(fptr), (long)size);
}

int sim_set_fifo_nonblock (FILE *fptr)
{
return -1;
}

struct SHMEM {
    HANDLE hMapping;
    size_t shm_size;
    void *shm_base;
    char *shm_name;
    };

t_stat sim_shmem_open (const char *name, size_t size, SHMEM **shmem, void **addr)
{
SYSTEM_INFO SysInfo;
t_bool AlreadyExists;

GetSystemInfo (&SysInfo);
*shmem = (SHMEM *)calloc (1, sizeof(**shmem));
if (*shmem == NULL)
    return SCPE_MEM;
(*shmem)->shm_name = (char *)calloc (1, 1 + strlen (name));
if ((*shmem)->shm_name == NULL) {
    free (*shmem);
    *shmem = NULL;
    return SCPE_MEM;
    }
strcpy ((*shmem)->shm_name, name);
(*shmem)->hMapping = INVALID_HANDLE_VALUE;
(*shmem)->shm_size = size;
(*shmem)->shm_base = NULL;
(*shmem)->hMapping = CreateFileMappingA (INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE|SEC_COMMIT, 0, (DWORD)(size+SysInfo.dwPageSize), name);
if ((*shmem)->hMapping == INVALID_HANDLE_VALUE) {
    DWORD LastError = GetLastError();

    sim_shmem_close (*shmem);
    *shmem = NULL;
    return sim_messagef (SCPE_OPENERR, "Can't CreateFileMapping of a %u byte shared memory segment '%s' - LastError=0x%X\n", (unsigned int)size, name, (unsigned int)LastError);
    }
AlreadyExists = (GetLastError () == ERROR_ALREADY_EXISTS);
(*shmem)->shm_base = MapViewOfFile ((*shmem)->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
if ((*shmem)->shm_base == NULL) {
    DWORD LastError = GetLastError();

    sim_shmem_close (*shmem);
    *shmem = NULL;
    return sim_messagef (SCPE_OPENERR, "Can't MapViewOfFile() of a %u byte shared memory segment '%s' - LastError=0x%X\n", (unsigned int)size, name, (unsigned int)LastError);
    }
if (AlreadyExists) {
    if (*((DWORD *)((*shmem)->shm_base)) == 0)
        Sleep (50);
    if (*((DWORD *)((*shmem)->shm_base)) != (DWORD)size) {
        DWORD SizeFound = *((DWORD *)((*shmem)->shm_base));
        sim_shmem_close (*shmem);
        *shmem = NULL;
        return sim_messagef (SCPE_OPENERR, "Shared Memory segment '%s' is %u bytes instead of %d\n", name, (unsigned int)SizeFound, (int)size);
        }
    }
else
    *((DWORD *)((*shmem)->shm_base)) = (DWORD)size;     /* Save Size in first page */

*addr = ((char *)(*shmem)->shm_base + SysInfo.dwPageSize);      /* Point to the second paget for data */
return SCPE_OK;
}

void sim_shmem_close (SHMEM *shmem)
{
if (shmem == NULL)
    return;
if (shmem->shm_base != NULL)
    UnmapViewOfFile (shmem->shm_base);
if (shmem->hMapping != INVALID_HANDLE_VALUE)
    CloseHandle (shmem->hMapping);
free (shmem->shm_name);
free (shmem);
}

int32 sim_shmem_atomic_add (int32 *p, int32 v)
{
return InterlockedExchangeAdd ((volatile long *) p,v) + (v);
}

t_bool sim_shmem_atomic_cas (int32 *ptr, int32 oldv, int32 newv)
{
return (InterlockedCompareExchange ((LONG volatile *) ptr, newv, oldv) == oldv);
}

#else /* !defined(_WIN32) */
#include <unistd.h>
int sim_set_fsize (FILE *fptr, t_addr size)
{
return ftruncate(fileno(fptr), (off_t)size);
}

#include <sys/stat.h>
#include <fcntl.h>
#if HAVE_UTIME
#include <utime.h>
#endif

const char *
sim_get_os_error_text (int Error)
{
return strerror (Error);
}

t_stat sim_copyfile (const char *source_file, const char *dest_file, t_bool overwrite_existing)
{
FILE *fIn = NULL, *fOut = NULL;
t_stat st = SCPE_OK;
char *buf = NULL;
size_t bytes;

fIn = sim_fopen (source_file, "rb");
if (!fIn) {
    st = sim_messagef (SCPE_ARG, "Can't open '%s' for input: %s\n", source_file, strerror (errno));
    goto Cleanup_Return;
    }
fOut = sim_fopen (dest_file, "wb");
if (!fOut) {
    st = sim_messagef (SCPE_ARG, "Can't open '%s' for output: %s\n", dest_file, strerror (errno));
    goto Cleanup_Return;
    }
buf = (char *)malloc (BUFSIZ);
while ((bytes = fread (buf, 1, BUFSIZ, fIn)))
    fwrite (buf, 1, bytes, fOut);
Cleanup_Return:
free (buf);
if (fIn)
    fclose (fIn);
if (fOut)
    fclose (fOut);
#if defined(HAVE_UTIME)
if (st == SCPE_OK) {
    struct stat statb;

    if (!stat (source_file, &statb)) {
        struct utimbuf utim;

        utim.actime = statb.st_atime;
        utim.modtime = statb.st_mtime;
        if (utime (dest_file, &utim))
            st = SCPE_IOERR;
        }
    else
        st = SCPE_IOERR;
    }
#endif
return st;
}

int sim_set_fifo_nonblock (FILE *fptr)
{
struct stat stbuf;

if (!fptr || fstat (fileno(fptr), &stbuf))
    return -1;
#if defined(S_IFIFO) && defined(O_NONBLOCK)
if ((stbuf.st_mode & S_IFIFO)) {
    int flags = fcntl(fileno(fptr), F_GETFL, 0);
    return fcntl(fileno(fptr), F_SETFL, flags | O_NONBLOCK);
    }
#endif
return -1;
}

#if defined (__linux__) || defined (__APPLE__)
#include <sys/mman.h>

struct SHMEM {
    int shm_fd;
    size_t shm_size;
    void *shm_base;
    char *shm_name;
    };

t_stat sim_shmem_open (const char *name, size_t size, SHMEM **shmem, void **addr)
{
#ifdef HAVE_SHM_OPEN
*shmem = (SHMEM *)calloc (1, sizeof(**shmem));
mode_t orig_mask;

*addr = NULL;
if (*shmem == NULL)
    return SCPE_MEM;
(*shmem)->shm_name = (char *)calloc (1, 1 + strlen (name) + ((*name != '/') ? 1 : 0));
if ((*shmem)->shm_name == NULL) {
    free (*shmem);
    *shmem = NULL;
    return SCPE_MEM;
    }

sprintf ((*shmem)->shm_name, "%s%s", ((*name != '/') ? "/" : ""), name);
(*shmem)->shm_base = MAP_FAILED;
(*shmem)->shm_size = size;
(*shmem)->shm_fd = shm_open ((*shmem)->shm_name, O_RDWR, 0);
if ((*shmem)->shm_fd == -1) {
    int last_errno;

    orig_mask = umask (0000);
    (*shmem)->shm_fd = shm_open ((*shmem)->shm_name, O_CREAT | O_RDWR, 0660);
    last_errno = errno;
    umask (orig_mask);                  /* Restore original mask */
    if ((*shmem)->shm_fd == -1) {
        sim_shmem_close (*shmem);
        *shmem = NULL;
        return sim_messagef (SCPE_OPENERR, "Can't shm_open() a %d byte shared memory segment '%s' - errno=%d - %s\n", (int)size, name, last_errno, strerror (last_errno));
        }
    if (ftruncate((*shmem)->shm_fd, size)) {
        sim_shmem_close (*shmem);
        *shmem = NULL;
        return SCPE_OPENERR;
        }
    }
else {
    struct stat statb;

    if ((fstat ((*shmem)->shm_fd, &statb)) ||
        (statb.st_size != (*shmem)->shm_size)) {
        sim_shmem_close (*shmem);
        *shmem = NULL;
        return sim_messagef (SCPE_OPENERR, "Shared Memory segment '%s' is %d bytes instead of %d\n", name, (int)(statb.st_size), (int)size);
        }
    }
(*shmem)->shm_base = mmap(NULL, (*shmem)->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, (*shmem)->shm_fd, 0);
if ((*shmem)->shm_base == MAP_FAILED) {
    int last_errno = errno;

    sim_shmem_close (*shmem);
    *shmem = NULL;
    return sim_messagef (SCPE_OPENERR, "Shared Memory '%s' mmap() failed. errno=%d - %s\n", name, last_errno, strerror (last_errno));
    }
*addr = (*shmem)->shm_base;
return SCPE_OK;
#else
return SCPE_NOFNC;
#endif
}

void sim_shmem_close (SHMEM *shmem)
{
if (shmem == NULL)
    return;
if (shmem->shm_base != MAP_FAILED)
    munmap (shmem->shm_base, shmem->shm_size);
if (shmem->shm_fd != -1)
    close (shmem->shm_fd);
free (shmem->shm_name);
free (shmem);
}

int32 sim_shmem_atomic_add (int32 *p, int32 v)
{
#if defined (HAVE_GCC_SYNC_BUILTINS)
return __sync_add_and_fetch((int *) p, v);
#else
return *p + v;
#endif
}

t_bool sim_shmem_atomic_cas (int32 *ptr, int32 oldv, int32 newv)
{
#if defined (HAVE_GCC_SYNC_BUILTINS)
return __sync_bool_compare_and_swap (ptr, oldv, newv);
#else
if (*ptr == oldv) {
    *ptr = newv;
    return 1;
    }
else
    return 0;
#endif
}

#else /* !(defined (__linux__) || defined (__APPLE__)) */

t_stat sim_shmem_open (const char *name, size_t size, SHMEM **shmem, void **addr)
{
return SCPE_NOFNC;
}

void sim_shmem_close (SHMEM *shmem)
{
}

int32 sim_shmem_atomic_add (int32 *p, int32 v)
{
return -1;
}

t_bool sim_shmem_atomic_cas (int32 *ptr, int32 oldv, int32 newv)
{
return FALSE;
}

#endif /* defined (__linux__) || defined (__APPLE__) */
#endif /* defined (_WIN32) */

#if defined(__VAX)
/* 
 * We privide a 'basic' snprintf, which 'might' overrun a buffer, but
 * the actual use cases don't on other platforms and none of the callers
 * care about the function return value.
 */
int sim_vax_snprintf(char *buf, size_t buf_size, const char *fmt, ...)
{
va_list arglist;

va_start (arglist, fmt);
vsprintf (buf, fmt, arglist);
va_end (arglist);
return 0;
}
#endif

char *sim_getcwd (char *buf, size_t buf_size)
{
#if defined (VMS)
return getcwd (buf, buf_size, 0);
#else
return getcwd (buf, buf_size);
#endif
}

/*
 * Parsing and expansion of file names.
 *
 *    %~I%        - expands filepath value removing any surrounding quotes (" or ')
 *    %~fI%       - expands filepath value to a fully qualified path name
 *    %~pI%       - expands filepath value to a path only
 *    %~nI%       - expands filepath value to a file name only
 *    %~xI%       - expands filepath value to a file extension only
 *
 * The modifiers can be combined to get compound results:
 *
 *    %~pnI%      - expands filepath value to a path and name only
 *    %~nxI%      - expands filepath value to a file name and extension only
 *
 * In the above example above %I% can be replaced by other 
 * environment variables or numeric parameters to a DO command
 * invokation.
 */

char *sim_filepath_parts (const char *filepath, const char *parts)
{
size_t tot_len = 0, tot_size = 0;
char *tempfilepath = NULL;
char *fullpath = NULL, *result = NULL;
char *c, *name, *ext;
char chr;
const char *p;
char filesizebuf[32] = "";
char filedatetimebuf[32] = "";

if (((*filepath == '\'') || (*filepath == '"')) &&
    (filepath[strlen (filepath) - 1] == *filepath)) {
    size_t temp_size = 1 + strlen (filepath);

    tempfilepath = (char *)malloc (temp_size);
    if (tempfilepath == NULL)
        return NULL;
    strlcpy (tempfilepath, 1 + filepath, temp_size);
    tempfilepath[strlen (tempfilepath) - 1] = '\0';
    filepath = tempfilepath;
    }
if ((filepath[1] == ':')  ||
    (filepath[0] == '/')  || 
    (filepath[0] == '\\')){
        tot_len = 1 + strlen (filepath);
        fullpath = (char *)malloc (tot_len);
        if (fullpath == NULL) {
            free (tempfilepath);
            return NULL;
            }
        strcpy (fullpath, filepath);
    }
else {
    char dir[PATH_MAX+1] = "";
    char *wd = sim_getcwd(dir, sizeof (dir));

    if (wd == NULL) {
        free (tempfilepath);
        return NULL;
        }
    tot_len = 1 + strlen (filepath) + 1 + strlen (dir);
    fullpath = (char *)malloc (tot_len);
    if (fullpath == NULL) {
        free (tempfilepath);
        return NULL;
        }
    strlcpy (fullpath, dir, tot_len);
    if ((dir[strlen (dir) - 1] != '/') &&       /* if missing a trailing directory separator? */
        (dir[strlen (dir) - 1] != '\\'))
        strlcat (fullpath, "/", tot_len);       /*  then add one */
    strlcat (fullpath, filepath, tot_len);
    }
while ((c = strchr (fullpath, '\\')))           /* standardize on / directory separator */
       *c = '/';
if ((fullpath[1] == ':') && islower (fullpath[0]))
    fullpath[0] = toupper (fullpath[0]);
while ((c = strstr (fullpath + 1, "//")))       /* strip out redundant / characters (leaving the option for a leading //) */
       memmove (c, c + 1, 1 + strlen (c + 1));
while ((c = strstr (fullpath, "/./")))          /* strip out irrelevant /./ sequences */
       memmove (c, c + 2, 1 + strlen (c + 2));
while ((c = strstr (fullpath, "/../"))) {       /* process up directory climbing */
    char *cl = c - 1;

    while ((*cl != '/') && (cl > fullpath))
        --cl;
    if ((cl <= fullpath) ||                      /* Digest Leading /../ sequences */
        ((fullpath[1] == ':') && (c == fullpath + 2)))
        memmove (c, c + 3, 1 + strlen (c + 3)); /* and removing intervening elements */
    else
        if (*cl == '/')
            memmove (cl, c + 3, 1 + strlen (c + 3));/* and removing intervening elements */
        else
            break;
    }
name = 1 + strrchr (fullpath, '/');
ext = strrchr (name, '.');
if (ext == NULL)
    ext = name + strlen (name);
tot_size = 0;
if (*parts == '\0')             /* empty part specifier means strip only quotes */
    tot_size = strlen (tempfilepath);
if (strchr (parts, 't') || strchr (parts, 'z')) {
    struct stat filestat;
    struct tm *tm;

    memset (&filestat, 0, sizeof (filestat));
    (void)stat (fullpath, &filestat);
    if (sizeof (filestat.st_size) == 4)
        sprintf (filesizebuf, "%ld ", (long)filestat.st_size);
    else
        sprintf (filesizebuf, "%" LL_FMT "d ", (LL_TYPE)filestat.st_size);
    tm = localtime (&filestat.st_mtime);
    sprintf (filedatetimebuf, "%02d/%02d/%04d %02d:%02d %cM ", 1 + tm->tm_mon, tm->tm_mday, 1900 + tm->tm_year,
                                                              tm->tm_hour % 12, tm->tm_min, (0 == (tm->tm_hour % 12)) ? 'A' : 'P');
    }
for (p = parts; *p; p++) {
    switch (*p) {
        case 'f':
            tot_size += strlen (fullpath);
            break;
        case 'p':
            tot_size += name - fullpath;
            break;
        case 'n':
            tot_size += ext - name;
            break;
        case 'x':
            tot_size += strlen (ext);
            break;
        case 't':
            tot_size += strlen (filedatetimebuf);
            break;
        case 'z':
            tot_size += strlen (filesizebuf);
            break;
        }
    }
result = (char *)malloc (1 + tot_size);
*result = '\0';
if (*parts == '\0')             /* empty part specifier means strip only quotes */
    strlcat (result, filepath, 1 + tot_size);
for (p = parts; *p; p++) {
    switch (*p) {
        case 'f':
            strlcat (result, fullpath, 1 + tot_size);
            break;
        case 'p':
            chr = *name;
            *name = '\0';
            strlcat (result, fullpath, 1 + tot_size);
            *name = chr;
            break;
        case 'n':
            chr = *ext;
            *ext = '\0';
            strlcat (result, name, 1 + tot_size);
            *ext = chr;
            break;
        case 'x':
            strlcat (result, ext, 1 + tot_size);
            break;
        case 't':
            strlcat (result, filedatetimebuf, 1 + tot_size);
            break;
        case 'z':
            strlcat (result, filesizebuf, 1 + tot_size);
            break;
        }
    }
free (fullpath);
free (tempfilepath);
return result;
}

#if defined (_WIN32)

t_stat sim_dir_scan (const char *cptr, DIR_ENTRY_CALLBACK entry, void *context)
{
HANDLE hFind;
WIN32_FIND_DATAA File;
struct stat filestat;
char WildName[PATH_MAX + 1];

strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
if ((hFind =  FindFirstFileA (cptr, &File)) != INVALID_HANDLE_VALUE) {
    t_int64 FileSize;
    char DirName[PATH_MAX + 1], FileName[PATH_MAX + 1];
    char *c;
    const char *backslash = strchr (cptr, '\\');
    const char *slash = strchr (cptr, '/');
    const char *pathsep = (backslash && slash) ? MIN (backslash, slash) : (backslash ? backslash : slash);

    GetFullPathNameA(cptr, sizeof(DirName), DirName, (char **)&c);
    c = strrchr (DirName, '\\');
    *c = '\0';                                  /* Truncate to just directory path */
    if (!pathsep ||                             /* Separator wasn't mentioned? */
        (slash && (0 == strcmp (slash, "/*")))) 
        pathsep = "\\";                         /* Default to Windows backslash */
    if (*pathsep == '/') {                      /* If slash separator? */
        while ((c = strchr (DirName, '\\')))
            *c = '/';                           /* Convert backslash to slash */
        }
    sprintf (&DirName[strlen (DirName)], "%c", *pathsep);
    do {
        FileSize = (((t_int64)(File.nFileSizeHigh)) << 32) | File.nFileSizeLow;
        strlcpy (FileName, DirName, sizeof (FileName));
        strlcat (FileName, File.cFileName, sizeof (FileName));
        stat (FileName, &filestat);
        entry (DirName, File.cFileName, FileSize, &filestat, context);
        } while (FindNextFile (hFind, &File));
    FindClose (hFind);
    }
else
    return SCPE_ARG;
return SCPE_OK;
}

#else /* !defined (_WIN32) */

#include <sys/stat.h>
#if defined (HAVE_GLOB)
#include <glob.h>
#else /* !defined (HAVE_GLOB) */
#include <dirent.h>
#if defined (HAVE_FNMATCH)
#include <fnmatch.h>
#endif
#endif /* defined (HAVE_GLOB) */

t_stat sim_dir_scan (const char *cptr, DIR_ENTRY_CALLBACK entry, void *context)
{
#if defined (HAVE_GLOB)
glob_t  paths;
#else
DIR *dir;
#endif
struct stat filestat;
char *c;
char DirName[PATH_MAX + 1], WholeName[PATH_MAX + 1], WildName[PATH_MAX + 1];

memset (DirName, 0, sizeof(DirName));
memset (WholeName, 0, sizeof(WholeName));
strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
c = sim_filepath_parts (cptr, "f");
strlcpy (WholeName, c, sizeof (WholeName));
free (c);
c = strrchr (WholeName, '/');
if (c) {
    memmove (DirName, WholeName, 1+c-WholeName);
    DirName[1+c-WholeName] = '\0';
    }
else
    DirName[0] = '\0';
cptr = WholeName;
#if defined (HAVE_GLOB)
memset (&paths, 0, sizeof (paths));
if (0 == glob (cptr, 0, NULL, &paths)) {
#else
dir = opendir(DirName[0] ? DirName : "/.");
if (dir) {
    struct dirent *ent;
#endif
    t_offset FileSize;
    char FileName[PATH_MAX + 1];
    const char *MatchName = 1 + strrchr (cptr, '/');
    char *p_name;
    struct tm *local;
#if defined (HAVE_GLOB)
    size_t i;
#endif

#if defined (HAVE_GLOB)
    for (i=0; i<paths.gl_pathc; i++) {
        sprintf (FileName, "%s", paths.gl_pathv[i]);
#else
    while ((ent = readdir (dir))) {
#if defined (HAVE_FNMATCH)
        if (fnmatch(MatchName, ent->d_name, 0))
            continue;
#else
        /* only match exact name without fnmatch support */
        if (strcmp(MatchName, ent->d_name) != 0)
            continue;
#endif
        sprintf (FileName, "%s%s", DirName, ent->d_name);
#endif
        p_name = FileName + strlen (DirName);
        memset (&filestat, 0, sizeof (filestat));
        (void)stat (FileName, &filestat);
        FileSize = (t_offset)((filestat.st_mode & S_IFDIR) ? 0 : sim_fsize_name_ex (FileName));
        entry (DirName, p_name, FileSize, &filestat, context);
        }
#if defined (HAVE_GLOB)
    globfree (&paths);
#else
    closedir (dir);
#endif
    }
else
    return SCPE_ARG;
return SCPE_OK;
}
#endif /* !defined(_WIN32) */
