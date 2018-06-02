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

   02-Feb-11    MP      Added sim_fsize_ex and sim_fsize_name_ex returning t_addr
                        Added export of sim_buf_copy_swapped and sim_buf_swap_data
   15-May-06    RMS     Added sim_fsize_name
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   02-Jan-04    RMS     Split out from SCP
*/

#ifndef SIM_FIO_H_
#define SIM_FIO_H_     0

#ifdef  __cplusplus
extern "C" {
#endif

#define FLIP_SIZE       (1 << 16)                       /* flip buf size */
#define fxread(a,b,c,d)         sim_fread (a, b, c, d)
#define fxwrite(a,b,c,d)        sim_fwrite (a, b, c, d)

#if ((defined (__linux) || defined (__linux__)) && (defined (__ANDROID_API__) && (__ANDROID_API__ < 24)))
#define DONT_DO_LARGEFILE 1
#endif
int32 sim_finit (void);
#if (defined (__linux) || defined (__linux__) || defined (__hpux) || defined (_AIX) ||         \
     (defined (VMS) && (defined (__ALPHA) || defined (__ia64)) && (__DECC_VER >= 60590001)) || \
     ((defined(__sun) || defined(__sun__)) && defined(_LARGEFILE_SOURCE)) ||                   \
     defined (_WIN32) || defined (__APPLE__) || defined (__CYGWIN__) ||                        \
     defined (__FreeBSD__) || defined(__NetBSD__) || defined (__OpenBSD__)) && !defined (DONT_DO_LARGEFILE)
typedef t_int64        t_offset;
#else
typedef int32        t_offset;
#if !defined (DONT_DO_LARGEFILE)
#define DONT_DO_LARGEFILE 1
#endif
#endif
FILE *sim_fopen (const char *file, const char *mode);
int sim_fseek (FILE *st, t_addr offset, int whence);
int sim_fseeko (FILE *st, t_offset offset, int whence);
int sim_set_fsize (FILE *fptr, t_addr size);
int sim_set_fifo_nonblock (FILE *fptr);
size_t sim_fread (void *bptr, size_t size, size_t count, FILE *fptr);
size_t sim_fwrite (const void *bptr, size_t size, size_t count, FILE *fptr);
uint32 sim_fsize (FILE *fptr);
uint32 sim_fsize_name (const char *fname);
t_offset sim_ftell (FILE *st);
t_offset sim_fsize_ex (FILE *fptr);
t_offset sim_fsize_name_ex (const char *fname);
t_stat sim_copyfile (const char *source_file, const char *dest_file, t_bool overwrite_existing);
void sim_buf_swap_data (void *bptr, size_t size, size_t count);
void sim_buf_copy_swapped (void *dptr, const void *bptr, size_t size, size_t count);
const char *sim_get_os_error_text (int error);
typedef struct SHMEM SHMEM;
t_stat sim_shmem_open (const char *name, size_t size, SHMEM **shmem, void **addr);
void sim_shmem_close (SHMEM *shmem);
int32 sim_shmem_atomic_add (int32 *ptr, int32 val);
t_bool sim_shmem_atomic_cas (int32 *ptr, int32 oldv, int32 newv);

extern t_bool sim_taddr_64;         /* t_addr is > 32b and Large File Support available */
extern t_bool sim_toffset_64;       /* Large File (>2GB) file I/O support */
extern t_bool sim_end;              /* TRUE = little endian, FALSE = big endian */

#ifdef  __cplusplus
}
#endif

#endif
