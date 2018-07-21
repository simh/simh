/* sim_shmem.c: simulator shared memory library

   Copyright (c) 2015-2016, Robert M Supnik

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

   This library includes:

   sim_shmem_open           create or attach to a shared memory region
   sim_shmem_close          close a shared memory region
   sim_shmem_atomic_add     interlocked add to an atomic variable
   sim_shmem_atomic_cas     interlocked compare and swap to an atomic variable
*/

#include "sim_defs.h"
#include "sim_shmem.h"

#if defined(_WIN32)
#include <windows.h>

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
    return sim_messagef (SCPE_OPENERR, "Can't CreateFileMapping of a %u byte shared memory segment '%s' - LastError=0x%X\n", size, name, LastError);
    }
AlreadyExists = (GetLastError () == ERROR_ALREADY_EXISTS);
(*shmem)->shm_base = MapViewOfFile ((*shmem)->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
if ((*shmem)->shm_base == NULL) {
    DWORD LastError = GetLastError();

    sim_shmem_close (*shmem);
    *shmem = NULL;
    return sim_messagef (SCPE_OPENERR, "Can't MapViewOfFile() of a %u byte shared memory segment '%s' - LastError=0x%X\n", size, name, LastError);
    }
if (AlreadyExists) {
    if (*((DWORD *)((*shmem)->shm_base)) == 0)
        Sleep (50);
    if (*((DWORD *)((*shmem)->shm_base)) != (DWORD)size) {
        DWORD SizeFound = *((DWORD *)((*shmem)->shm_base));
        sim_shmem_close (*shmem);
        *shmem = NULL;
        return sim_messagef (SCPE_OPENERR, "Shared Memory segment '%s' is %u bytes instead of %d\n", name, SizeFound, (int)size);
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

#elif defined (__linux__) || defined (__APPLE__)
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
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

#else

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
#endif
