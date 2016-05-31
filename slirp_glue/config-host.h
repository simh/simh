#ifndef CONFIG_HOST_H
#define CONFIG_HOST_H

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#else
typedef int SOCKET;
#endif

#ifndef  __cplusplus
typedef int bool;
#endif
#ifdef _MSC_VER
#include <win32/stdint.h>
#else
#include <stdint.h>
#endif
#include <sockets.h>
#define qemu_add_child_watch(pid)
int qemu_setsockopt (int s, int level, int optname, void *optval, int optlen);
int qemu_recv (int s, void *buf, size_t len, int flags);
#ifdef _MSC_VER
#define snprintf _snprintf
#define strcasecmp stricmp
#define inline
#else
#ifndef _WIN32
#define CONFIG_IOVEC 1
#endif
#endif
#define register_savevm(p1, p2, p3, p4, p5, p6, p7)
#define unregister_savevm(p1, p2, p3)
#define qemu_put_be16(p1, p2)
#define qemu_put_sbe16(p1, p2)
#define qemu_put_be32(p1, p2)
#define qemu_put_sbe32(p1, p2)
#define qemu_put_byte(p1, p2)
#define qemu_put_sbyte(p1, p2)
#define qemu_put_buffer(p1, p2, p3)

#define qemu_get_be16(p1) 0
#define qemu_get_sbe16(p1) 0
#define qemu_get_be32(p1) 0
#define qemu_get_sbe32(p1) 0
#define qemu_get_byte(p1) 0
#define qemu_get_sbyte(p1) 0
#define qemu_get_buffer(p1, p2, p3)
#define error_report(...) fprintf(stderr, __VA_ARGS__)

#endif
