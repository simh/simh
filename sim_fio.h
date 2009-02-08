/* sim_fio.h: simulator file I/O library headers

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
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   15-May-06    RMS     Added sim_fsize_name
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-Jan-04    RMS     Split out from SCP
*/

#ifndef _SIM_FIO_H_
#define _SIM_FIO_H_     0

#define FLIP_SIZE       (1 << 16)                       /* flip buf size */
#define fxread(a,b,c,d)         sim_fread (a, b, c, d)
#define fxwrite(a,b,c,d)        sim_fwrite (a, b, c, d)

int32 sim_finit (void);
FILE *sim_fopen (const char *file, const char *mode);
int sim_fseek (FILE *st, t_addr offset, int whence);
size_t sim_fread (void *bptr, size_t size, size_t count, FILE *fptr);
size_t sim_fwrite (void *bptr, size_t size, size_t count, FILE *fptr);
uint32 sim_fsize (FILE *fptr);
uint32 sim_fsize_name (char *fname);

#endif
