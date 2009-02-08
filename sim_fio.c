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

   sim_finit    -       initialize package
   sim_fopen    -       open file
   sim_fread    -       endian independent read (formerly fxread)
   sim_write    -       endian independent write (formerly fxwrite)
   sim_fseek    -       extended (>32b) seek (formerly fseek_ext)
   sim_fsize    -       get file size

   sim_fopen and sim_fseek are OS-dependent.  The other routines are not.
   sim_fsize is always a 32b routine (it is used only with small capacity random
   access devices like fixed head disks and DECtapes).
*/

#include "sim_defs.h"

static unsigned char sim_flip[FLIP_SIZE];
int32 sim_end = 1;                                      /* 1 = little */

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
sim_end = end_test.c[0];
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

uint32 sim_fsize_name (char *fname)
{
FILE *fp;
uint32 sz;

if ((fp = sim_fopen (fname, "rb")) == NULL)
    return 0;
sz = sim_fsize (fp);
fclose (fp);
return sz;
}

uint32 sim_fsize (FILE *fp)
{
uint32 pos, sz;

if (fp == NULL)
    return 0;
pos = ftell (fp);
fseek (fp, 0, SEEK_END);
sz = ftell (fp);
fseek (fp, pos, SEEK_SET);
return sz;
}

/* OS-dependent routines */

/* Optimized file open */

FILE *sim_fopen (const char *file, const char *mode)
{
#if defined (VMS)
return fopen (file, mode, "ALQ=32", "DEQ=4096",
        "MBF=6", "MBC=127", "FOP=cbt,tef", "ROP=rah,wbh", "CTX=stm");
#elif defined (USE_INT64) && defined (USE_ADDR64) && defined (__linux)
return fopen64 (file, mode);
#else
return fopen (file, mode);
#endif
}

/* Long seek */

#if defined (USE_INT64) && defined (USE_ADDR64)

/* 64b VMS */

#if (defined (__ALPHA) || defined (__ia64)) && defined (VMS) /* 64b VMS */
#define _SIM_IO_FSEEK_EXT_      1

static t_int64 fpos_t_to_int64 (fpos_t *pos)
{
unsigned short *w = (unsigned short *) pos;             /* endian dep! */
t_int64 result;

result = w[1];
result <<= 16;
result += w[0];
result <<= 9;
result += w[2];
return result;
}

static void int64_to_fpos_t (t_int64 ipos, fpos_t *pos, size_t mbc)
{
unsigned short *w = (unsigned short *) pos;
int bufsize = mbc << 9;

w[3] = 0;
w[2] = (unsigned short) (ipos % bufsize);
ipos -= w[2];
ipos >>= 9;
w[0] = (unsigned short) ipos;
ipos >>= 16;
w[1] = (unsigned short) ipos;
if ((w[2] == 0) && (w[0] || w[1])) {
    w[2] = bufsize;
    w[0] -= mbc;
    }
return;
}

int sim_fseek (FILE *st, t_addr offset, int whence)
{
t_addr fileaddr;
fpos_t filepos;

switch (whence) {

    case SEEK_SET:
        fileaddr = offset;
        break;

    case SEEK_CUR:
        if (fgetpos (st, &filepos))
            return (-1);
        fileaddr = fpos_t_to_int64 (&filepos);
        fileaddr = fileaddr + offset;
        break;

    default:
        errno = EINVAL;
        return (-1);
        }

int64_to_fpos_t (fileaddr, &filepos, 127);
return fsetpos (st, &filepos);
}

#endif

/* Alpha UNIX - natively 64b */

#if defined (__ALPHA) && defined (__unix__)             /* Alpha UNIX */
#define _SIM_IO_FSEEK_EXT_      1

int sim_fseek (FILE *st, t_addr offset, int whence)
{
return fseek (st, offset, whence);
}

#endif

/* Windows */

#if defined (_WIN32)
#define _SIM_IO_FSEEK_EXT_      1

int sim_fseek (FILE *st, t_addr offset, int whence)
{
fpos_t fileaddr;

switch (whence) {

    case SEEK_SET:
        fileaddr = offset;
        break;

    case SEEK_CUR:
        if (fgetpos (st, &fileaddr))
            return (-1);
        fileaddr = fileaddr + offset;
        break;

    default:
        errno = EINVAL;
        return (-1);
        }

return fsetpos (st, &fileaddr);
}

#endif                                                  /* end Windows */

/* Linux */

#if defined (__linux)
#define _SIM_IO_FSEEK_EXT_      1

int sim_fseek (FILE *st, t_addr xpos, int origin)
{
return fseeko64 (st, xpos, origin);
}

#endif                                                  /* end Linux with LFS */

/* Apple OS/X */

#if defined (__APPLE__) || defined (__FreeBSD__)
#define _SIM_IO_FSEEK_EXT_      1

int sim_fseek (FILE *st, t_addr xpos, int origin) 
{
return fseeko (st, xpos, origin);
}

#endif  /* end Apple OS/X */

#endif                                                  /* end 64b seek defs */

/* Default: no OS-specific routine has been defined */

#if !defined (_SIM_IO_FSEEK_EXT_)
#define _SIM_IO_FSEEK_EXT_      0

int sim_fseek (FILE *st, t_addr xpos, int origin)
{
return fseek (st, (int32) xpos, origin);
}

#endif

uint32 sim_taddr_64 = _SIM_IO_FSEEK_EXT_;
