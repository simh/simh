/* sim_fio.c: simulator file I/O library

   Copyright (c) 1993-2018, Robert M Supnik

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

   28-Dec-18    JDB     Modify sim_fseeko, sim_ftell for mingwrt 5.2 compatibility
   02-Apr-15    RMS     Backported from GitHub master
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

   sim_finit            initialize package
   sim_fopen            open file
   sim_fread            endian independent read (formerly fxread)
   sim_write            endian independent write (formerly fxwrite)
   sim_fseek            (now a macro using fseeko)
   sim_fseeko           extended seek (>32b if available)
   sim_ftell            extended tell (>32b if available)
   sim_fsize            (now a macro using sim_fsize_ex)
   sim_fsize_name       (now a macro using sim_fsize_ex)
   sim_fsize_ex         get file size as a t_offset
   sim_fsize_name       get file size as a t_offset of named file

   sim_fopen, sim_fseeko, sim_ftell are OS-dependent. The other routines are not.
*/

#include "sim_defs.h"

static unsigned char sim_flip[FLIP_SIZE];
t_bool sim_end;                     /* TRUE = little endian, FALSE = big endian */
t_bool sim_taddr_64;                /* t_addr is > 32b and large file support available */
t_bool sim_toffset_64;              /* large file (>2GB) support available */

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
sim_toffset_64 = (sizeof(t_offset) > sizeof(int32));    /* large file (>2GB) support */
sim_taddr_64 = sim_toffset_64 && (sizeof(t_addr) > sizeof(int32));
return sim_end;
}

size_t sim_fread (void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c, j;
int32 k;
unsigned char by, *sptr, *dptr;

if ((size == 0) || (count == 0))                        /* check arguments */
    return 0;
c = fread (bptr, size, count, fptr);                    /* read buffer */
if (sim_end || (size == sizeof (char)) || (c == 0))     /* le, byte, or err? */
    return c;                                           /* done */
for (j = 0, dptr = sptr = (unsigned char *) bptr; j < c; j++) { /* loop on items */
    for (k = size - 1; k >= (((int32) size + 1) / 2); k--) {
        by = *sptr;                                     /* swap end-for-end */
        *sptr++ = *(dptr + k);
        *(dptr + k) = by;
        }
    sptr = dptr = dptr + size;                          /* next item */
    }
return c;
}

size_t sim_fwrite (void *bptr, size_t size, size_t count, FILE *fptr)
{
size_t c, j, nelem, nbuf, lcnt, total;
int32 i, k;
unsigned char *sptr, *dptr;

if ((size == 0) || (count == 0))                        /* check arguments */
    return 0;
if (sim_end || (size == sizeof (char)))                 /* le or byte? */
    return fwrite (bptr, size, count, fptr);            /* done */
nelem = FLIP_SIZE / size;                               /* elements in buffer */
nbuf = count / nelem;                                   /* number buffers */
lcnt = count % nelem;                                   /* count in last buf */
if (lcnt) nbuf = nbuf + 1;
else lcnt = nelem;
total = 0;
sptr = (unsigned char *) bptr;                          /* init input ptr */
for (i = nbuf; i > 0; i--) {                            /* loop on buffers */
    c = (i == 1)? lcnt: nelem;
    for (j = 0, dptr = sim_flip; j < c; j++) {          /* loop on items */
        for (k = size - 1; k >= 0; k--)
            *(dptr + k) = *sptr++;
        dptr = dptr + size;
        }
    c = fwrite (sim_flip, size, c, fptr);
    if (c == 0)
        return total;
    total = total + c;
    }
return total;
}

/* Get file size */

t_offset sim_fsize_ex (FILE *fp)
{
t_offset pos, sz;

if (fp == NULL)
    return 0;
pos = sim_ftell (fp);
sim_fseek (fp, 0, SEEK_END);
sz = sim_ftell (fp);
sim_fseeko (fp, pos, SEEK_SET);
return sz;
}

t_offset sim_fsize_name_ex (char *fname)
{
FILE *fp;
t_offset sz;

if ((fp = sim_fopen (fname, "rb")) == NULL)
    return 0;
sz = sim_fsize_ex (fp);
fclose (fp);
return sz;
}


/* OS-dependent routines */

/* Optimized file open

   VMS - specify extra goodies
   Linux, HP/UX, AIX - use a 64b open if necessary
   Others - ordinary open works for 32b or 64b */

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

/* Now define sim_fseeko and sim_ftell */

#if !defined (DONT_DO_LARGEFILE)

/* 64b VMS or Solaris */

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

/* [JDB] The previous Win32 versions of sim_fseeko and sim_ftell attempted to
   use fsetpos and fgetpos by manipulating an fpos_t value as though it were a
   64-bit integer.  This worked with version 5.0 of the mingw runtime library,
   which declared an fpos_t to be a long long int.  With version 5.2, fpos_t is
   now a union containing a long long int, so that it cannot be manipulated.
   The manipulation was always suspect, as the MSVC++ 2008 documentation says,
   "The [fpos_t] value is stored in an internal format and is intended for use
   only by fgetpos and fsetpos."  It worked, but only because VC++ declared it
   as an __int64 value.  If that changes, the original code would break, as it
   now does for mingw.

   Therefore, we now simply call _fseeki64 and _ftelli64, which are provided by
   both mingw and VC++ and work as expected without manipulation.
*/

int sim_fseeko (FILE *st, t_offset offset, int whence)
{
return _fseeki64 (st, offset, whence);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset) _ftelli64 (st);
}

#endif                                                  /* end Windows */

/* Linux, HP/UX, and AIX */

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

/* Apple OS/X and the BSD family */

#if defined (__APPLE__) || defined (__FreeBSD__) || defined(__NetBSD__) || defined (__OpenBSD__) 
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

