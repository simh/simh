/* headers to use the BSD sockets */
#ifndef QEMU_SOCKET_H
#define QEMU_SOCKET_H

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define socket_error() WSAGetLastError()

extern char *socket_strerror(int errcode);
#define strerror socket_strerror

int inet_aton(const char *cp, struct in_addr *ia);

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#define socket_error() errno

#endif /* !_WIN32 */

/* misc helpers */
int qemu_socket(int domain, int type, int protocol);
int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int socket_set_cork(int fd, int v);
int socket_set_nodelay(int fd);
void qemu_set_block(int fd);
void qemu_set_nonblock(int fd);
int socket_set_fast_reuse(int fd);

#ifdef _WIN32
/* MinGW needs type casts for the 'buf' and 'optval' arguments. */
#define qemu_sendto(sockfd, buf, len, flags, destaddr, addrlen) \
    sendto(sockfd, (const void *)buf, len, flags, destaddr, addrlen)

/* Windows has different names for the same constants with the same values */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif


#endif /* QEMU_SOCKET_H */
