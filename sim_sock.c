/* sim_sock.c: OS-dependent socket routines

   Copyright (c) 2001-2010, Robert M Supnik

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

   15-Oct-12    MP      Added definitions needed to detect possible tcp 
                        connect failures
   25-Sep-12    MP      Reworked for RFC3493 interfaces supporting IPv6 and IPv4
   22-Jun-10    RMS     Fixed types in sim_accept_conn (from Mark Pizzolato)
   19-Nov-05    RMS     Added conditional for OpenBSD (from Federico G. Schwindt)
   16-Aug-05    RMS     Fixed spurious SIGPIPE signal error in Unix
   14-Apr-05    RMS     Added WSAEINPROGRESS test (from Tim Riker)
   09-Jan-04    RMS     Fixed typing problem in Alpha Unix (found by Tim Chapman)
   17-Apr-03    RMS     Fixed non-implemented version of sim_close_sock
                        (found by Mark Pizzolato)
   17-Dec-02    RMS     Added sim_connect_socket, sim_create_socket
   08-Oct-02    RMS     Revised for .NET compatibility
   22-Aug-02    RMS     Changed calling sequence for sim_accept_conn
   22-May-02    RMS     Added OS2 EMX support from Holger Veit
   06-Feb-02    RMS     Added VMS support from Robert Alan Byer
   16-Sep-01    RMS     Added Macintosh support from Peter Schorn
   02-Sep-01    RMS     Fixed UNIX bugs found by Mirian Lennox and Tom Markson
*/

#ifdef  __cplusplus
extern "C" {
#endif

#include "sim_sock.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(AF_INET6) && defined(_WIN32)
#include <ws2tcpip.h>
#endif

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif

#ifndef WSAAPI
#define WSAAPI
#endif

#if defined(SHUT_RDWR) && !defined(SD_BOTH)
#define SD_BOTH SHUT_RDWR
#endif

#ifndef   NI_MAXHOST
#define   NI_MAXHOST 1025
#endif

/* OS dependent routines

   sim_master_sock      create master socket
   sim_connect_sock     connect a socket to a remote destination
   sim_connect_sock_ex  connect a socket to a remote destination
   sim_accept_conn      accept connection
   sim_read_sock        read from socket
   sim_write_sock       write from socket
   sim_close_sock       close socket
   sim_setnonblock      set socket non-blocking
*/

/* First, all the non-implemented versions */

#if defined (__OS2__) && !defined (__EMX__)

void sim_init_sock (void)
{
}

void sim_cleanup_sock (void)
{
}

SOCKET sim_master_sock_ex (const char *hostport, int *parse_status, int opt_flags)
{
return INVALID_SOCKET;
}

SOCKET sim_connect_sock_ex (const char *sourcehostport, const char *hostport, const char *default_host, const char *default_port, int opt_flags)
{
return INVALID_SOCKET;
}

SOCKET sim_accept_conn (SOCKET master, char **connectaddr);
{
return INVALID_SOCKET;
}

int sim_read_sock (SOCKET sock, char *buf, int nbytes)
{
return -1;
}

int sim_write_sock (SOCKET sock, char *msg, int nbytes)
{
return 0;
}

void sim_close_sock (SOCKET sock)
{
return;
}

#else                                                   /* endif unimpl */

/* UNIX, Win32, Macintosh, VMS, OS2 (Berkeley socket) routines */

static struct sock_errors {
    int value;
    const char *text;
    } sock_errors[] = {
        {WSAEWOULDBLOCK,  "Operation would block"},
        {WSAENAMETOOLONG, "File name too long"},
        {WSAEINPROGRESS,  "Operation now in progress "},
        {WSAETIMEDOUT,    "Connection timed out"},
        {WSAEISCONN,      "Transport endpoint is already connected"},
        {WSAECONNRESET,   "Connection reset by peer"},
        {WSAECONNREFUSED, "Connection refused"},
        {WSAECONNABORTED, "Connection aborted"},
        {WSAEHOSTUNREACH, "No route to host"},
        {WSAEADDRINUSE,   "Address already in use"},
#if defined (WSAEAFNOSUPPORT)
        {WSAEAFNOSUPPORT, "Address family not supported by protocol"},
#endif
        {WSAEACCES,       "Permission denied"},
        {0, NULL}
    };


const char *sim_get_err_sock (const char *emsg)
{
int err = WSAGetLastError ();
int i;
static char err_buf[512];

for (i=0; (sock_errors[i].text) && (sock_errors[i].value != err); i++)
    ;
if (sock_errors[i].value == err)
    sprintf (err_buf, "Sockets: %s error %d - %s\n", emsg, err, sock_errors[i].text);
else
#if defined(_WIN32)
    sprintf (err_buf, "Sockets: %s error %d\n", emsg, err);
#else
    sprintf (err_buf, "Sockets: %s error %d - %s\n", emsg, err, strerror(err));
#endif
return err_buf;
}

SOCKET sim_err_sock (SOCKET s, const char *emsg)
{
sim_printf ("%s", sim_get_err_sock (emsg));
if (s != INVALID_SOCKET) {
    int err = WSAGetLastError ();
    sim_close_sock (s);
    WSASetLastError (err);      /* Retain Original socket error value */
    }
return INVALID_SOCKET;
}

typedef void    (WSAAPI *freeaddrinfo_func) (struct addrinfo *ai);
static freeaddrinfo_func p_freeaddrinfo;

typedef int     (WSAAPI *getaddrinfo_func) (const char *hostname,
                                 const char *service,
                                 const struct addrinfo *hints,
                                 struct addrinfo **res);
static getaddrinfo_func p_getaddrinfo;

#if defined(VMS)
typedef size_t socklen_t;
#if !defined(EAI_OVERFLOW)
#define EAI_OVERFLOW EAI_FAIL
#endif
#endif

#if defined(__hpux)
#if !defined(EAI_OVERFLOW)
#define EAI_OVERFLOW EAI_FAIL
#endif
#endif

typedef int (WSAAPI *getnameinfo_func) (const struct sockaddr *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags);
static getnameinfo_func p_getnameinfo;

static void    WSAAPI s_freeaddrinfo (struct addrinfo *ai)
{
struct addrinfo *a, *an;

for (a=ai; a != NULL; a=an) {
    an = a->ai_next;
    free (a->ai_canonname);
    free (a->ai_addr);
    free (a);
    }
}

static int     WSAAPI s_getaddrinfo (const char *hostname,
                                     const char *service,
                                     const struct addrinfo *hints,
                                     struct addrinfo **res)
{
struct hostent *he;
struct servent *se = NULL;
struct sockaddr_in *sin;
struct addrinfo *result = NULL;
struct addrinfo *ai, *lai = NULL;
struct addrinfo dhints;
struct in_addr ipaddr;
struct in_addr *fixed[2];
struct in_addr **ips = NULL;
struct in_addr **ip;
const char *cname = NULL;
int port = 0;

// Validate parameters
if ((hostname == NULL) && (service == NULL))
    return EAI_NONAME;

if (hints) {
    if ((hints->ai_family != PF_INET) && (hints->ai_family != PF_UNSPEC))
        return EAI_FAMILY;
    switch (hints->ai_socktype)
        {
        default:
            return EAI_SOCKTYPE;
        case SOCK_DGRAM:
        case SOCK_STREAM:
        case 0:
            break;
        }
    }
else {
    hints = &dhints;
    memset(&dhints, 0, sizeof(dhints));
    dhints.ai_family = PF_UNSPEC;
    }
if (service) {
    char *c;

    port = strtoul(service, &c, 10);
    if ((port == 0) || (*c != '\0')) {
        switch (hints->ai_socktype)
            {
            case SOCK_DGRAM:
                se = getservbyname(service, "udp");
                break;
            case SOCK_STREAM:
            case 0:
                se = getservbyname(service, "tcp");
                break;
            }
        if (NULL == se)
            return EAI_SERVICE;
        port = se->s_port;
        }
    }

if (hostname) {
    if ((0xffffffff != (ipaddr.s_addr = inet_addr(hostname))) || 
        (0 == strcmp("255.255.255.255", hostname))) {
        fixed[0] = &ipaddr;
        fixed[1] = NULL;
        if ((hints->ai_flags & AI_CANONNAME) && !(hints->ai_flags & AI_NUMERICHOST)) {
            he = gethostbyaddr((char *)&ipaddr, 4, AF_INET);
            if (NULL != he)
                cname = he->h_name;
            else
                cname = hostname;
            }
        ips = fixed;
        }
    else {
        if (hints->ai_flags & AI_NUMERICHOST)
            return EAI_NONAME;
        he = gethostbyname(hostname);
        if (he) {
            ips = (struct in_addr **)he->h_addr_list;
            if (hints->ai_flags & AI_CANONNAME)
                cname = he->h_name;
            }
        else {
            switch (h_errno)
                {
                case HOST_NOT_FOUND:
                case NO_DATA:
                    return EAI_NONAME;
                case TRY_AGAIN:
                    return EAI_AGAIN;
                default:
                    return EAI_FAIL;
                }
            }
        }
    }
else {
    if (hints->ai_flags & AI_PASSIVE)
        ipaddr.s_addr = htonl(INADDR_ANY);
    else
        ipaddr.s_addr = htonl(INADDR_LOOPBACK);
    fixed[0] = &ipaddr;
    fixed[1] = NULL;
    ips = fixed;
    }
for (ip=ips; (ip != NULL) && (*ip != NULL); ++ip) {
    ai = (struct addrinfo *)calloc(1, sizeof(*ai));
    if (NULL == ai) {
        s_freeaddrinfo(result);
        return EAI_MEMORY;
        }
    ai->ai_family = PF_INET;
    ai->ai_socktype = hints->ai_socktype;
    ai->ai_protocol = hints->ai_protocol;
    ai->ai_addr = NULL;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_canonname = NULL;
    ai->ai_next = NULL;
    ai->ai_addr = (struct sockaddr *)calloc(1, sizeof(struct sockaddr_in));
    if (NULL == ai->ai_addr) {
        free(ai);
        s_freeaddrinfo(result);
        return EAI_MEMORY;
        }
    sin = (struct sockaddr_in *)ai->ai_addr;
    sin->sin_family = PF_INET;
    sin->sin_port = (unsigned short)port;
    memcpy(&sin->sin_addr, *ip, sizeof(sin->sin_addr));
    if (NULL == result)
        result = ai;
    else
        lai->ai_next = ai;
    lai = ai;
    }
if (cname) {
    result->ai_canonname = (char *)calloc(1, strlen(cname)+1);
    if (NULL == result->ai_canonname) {
        s_freeaddrinfo(result);
        return EAI_MEMORY;
        }
    strcpy(result->ai_canonname, cname);
    }
*res = result;
return 0;
}

#ifndef EAI_OVERFLOW
#define EAI_OVERFLOW WSAENAMETOOLONG
#endif

static int     WSAAPI s_getnameinfo (const struct sockaddr *sa, socklen_t salen,
                                     char *host, size_t hostlen,
                                     char *serv, size_t servlen,
                                     int flags)
{
struct hostent *he;
struct servent *se = NULL;
const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;

if (sin->sin_family != PF_INET)
    return EAI_FAMILY;
if ((NULL == host) && (NULL == serv))
    return EAI_NONAME;
if ((serv) && (servlen > 0)) {
    if (flags & NI_NUMERICSERV)
        se = NULL;
    else
        if (flags & NI_DGRAM)
            se = getservbyport(sin->sin_port, "udp");
        else
            se = getservbyport(sin->sin_port, "tcp");
    if (se) {
        if (servlen <= strlen(se->s_name))
            return EAI_OVERFLOW;
        strcpy(serv, se->s_name);
        }
    else {
        char buf[16];

        sprintf(buf, "%d", ntohs(sin->sin_port));
        if (servlen <= strlen(buf))
            return EAI_OVERFLOW;
        strcpy(serv, buf);
        }
    }
if ((host) && (hostlen > 0)) {
    if (flags & NI_NUMERICHOST)
        he = NULL;
    else
        he = gethostbyaddr((const char *)&sin->sin_addr, 4, AF_INET);
    if (he) {
        if (hostlen < strlen(he->h_name)+1)
            return EAI_OVERFLOW;
        strcpy(host, he->h_name);
        }
    else {
        if (flags & NI_NAMEREQD)
            return EAI_NONAME;
        if (hostlen < strlen(inet_ntoa(sin->sin_addr))+1)
            return EAI_OVERFLOW;
        strcpy(host, inet_ntoa(sin->sin_addr));
        }
    }
return 0;
}

#if defined(_WIN32) || defined(__CYGWIN__)

#if !defined(IPV6_V6ONLY)           /* Older XP environments may not define IPV6_V6ONLY */
#define IPV6_V6ONLY           27    /* Treat wildcard bind as AF_INET6-only. */
#endif
/* Dynamic DLL load variables */
#ifdef _WIN32
static HINSTANCE hLib = 0;                      /* handle to DLL */
#else
static void *hLib = NULL;                       /* handle to Library */
#endif
static int lib_loaded = 0;                      /* 0=not loaded, 1=loaded, 2=library load failed, 3=Func load failed */
static const char* lib_name = "Ws2_32.dll";

/* load function pointer from DLL */
typedef int (*_func)();

static void load_function(const char* function, _func* func_ptr) {
#ifdef _WIN32
    *func_ptr = (_func)GetProcAddress(hLib, function);
#else
    *func_ptr = (_func)dlsym(hLib, function);
#endif
    if (*func_ptr == 0) {
    sim_printf ("Sockets: Failed to find function '%s' in %s\r\n", function, lib_name);
    lib_loaded = 3;
  }
}

/* load Ws2_32.dll as required */
int load_ws2(void) {
  switch(lib_loaded) {
    case 0:                  /* not loaded */
            /* attempt to load DLL */
#ifdef _WIN32
      hLib = LoadLibraryA(lib_name);
#else
      hLib = dlopen(lib_name, RTLD_NOW);
#endif
      if (hLib == 0) {
        /* failed to load DLL */
        sim_printf ("Sockets: Failed to load %s\r\n", lib_name);
        lib_loaded = 2;
        break;
      } else {
        /* library loaded OK */
        lib_loaded = 1;
      }

      /* load required functions; sets dll_load=3 on error */
      load_function("getaddrinfo",       (_func *) &p_getaddrinfo);
      load_function("getnameinfo",       (_func *) &p_getnameinfo);
      load_function("freeaddrinfo",      (_func *) &p_freeaddrinfo);

      if (lib_loaded != 1) {
        /* unsuccessful load, connect stubs */
        p_getaddrinfo = (getaddrinfo_func)s_getaddrinfo;
        p_getnameinfo = (getnameinfo_func)s_getnameinfo;
        p_freeaddrinfo = (freeaddrinfo_func)s_freeaddrinfo;
      }
      break;
    default:                /* loaded or failed */
      break;
  }
  return (lib_loaded == 1) ? 1 : 0;
}
#endif

/* OS independent routines

   sim_parse_addr       parse a hostname/ipaddress from port and apply defaults and 
                        optionally validate an address match
*/

/* sim_parse_addr       host:port

   Presumption is that the input, if it doesn't contain a ':' character is a port specifier.
   If the host field contains one or more colon characters (i.e. it is an IPv6 address), 
   the IPv6 address MUST be enclosed in square bracket characters (i.e. Domain Literal format)

   Inputs:
        cptr    =       pointer to input string
        default_host
                =       optional pointer to default host if none specified
        host_len =      length of host buffer
        default_port
                =       optional pointer to default port if none specified
        port_len =      length of port buffer
        validate_addr = optional name/addr which is checked to be equivalent
                        to the host result of parsing the other input.  This
                        address would usually be returned by sim_accept_conn.
   Outputs:
        host    =       pointer to buffer for IP address (may be NULL), 0 = none
        port    =       pointer to buffer for IP port (may be NULL), 0 = none
        result  =       status (0 on complete success or -1 if 
                        parsing can't happen due to bad syntax, a value is 
                        out of range, a result can't fit into a result buffer, 
                        a service name doesn't exist, or a validation name 
                        doesn't match the parsed host)
*/

int sim_parse_addr (const char *cptr, char *host, size_t host_len, const char *default_host, char *port, size_t port_len, const char *default_port, const char *validate_addr)
{
char gbuf[CBUFSIZE], default_pbuf[CBUFSIZE];
const char *hostp;
char *portp;
char *endc;
unsigned long portval;

if ((host != NULL) && (host_len != 0))
    memset (host, 0, host_len);
if ((port != NULL) && (port_len != 0))
    memset (port, 0, port_len);
if ((cptr == NULL) || (*cptr == 0)) {
    if (((default_host == NULL) || (*default_host == 0)) || ((default_port == NULL) || (*default_port == 0)))
        return -1;
    if ((host == NULL) || (port == NULL))
        return -1;                                  /* no place */
    if ((strlen(default_host) >= host_len) || (strlen(default_port) >= port_len))
        return -1;                                  /* no room */
    strcpy (host, default_host);
    strcpy (port, default_port);
    return 0;
    }
memset (default_pbuf, 0, sizeof(default_pbuf));
if (default_port)
    strncpy (default_pbuf, default_port, sizeof(default_pbuf)-1);
gbuf[sizeof(gbuf)-1] = '\0';
strncpy (gbuf, cptr, sizeof(gbuf)-1);
hostp = gbuf;                                           /* default addr */
portp = NULL;
if ((portp = strrchr (gbuf, ':')) &&                    /* x:y? split */
    (NULL == strchr (portp, ']'))) {
    *portp++ = 0;
    if (*portp == '\0')
        portp = default_pbuf;
    }
else {                                                  /* No colon in input */
    portp = gbuf;                                       /* Input is the port specifier */
    hostp = (const char *)default_host;                 /* host is defaulted if provided */
    }
if (portp != NULL) {
    portval = strtoul(portp, &endc, 10);
    if ((*endc == '\0') && ((portval == 0) || (portval > 65535)))
        return -1;                                      /* numeric value too big */
    if (*endc != '\0') {
        struct servent *se = getservbyname(portp, "tcp");

        if (se == NULL)
            return -1;                                  /* invalid service name */
        }
    }
if (port)                                               /* port wanted? */
    if (portp != NULL) {
        if (strlen(portp) >= port_len)
            return -1;                                  /* no room */
        else
            strcpy (port, portp);
        }
if (hostp != NULL) {
    if (']' == hostp[strlen(hostp)-1]) {
        if ('[' != hostp[0])
            return -1;                                  /* invalid domain literal */
        /* host may be the const default_host so move to temp buffer before modifying */
        strncpy(gbuf, hostp+1, sizeof(gbuf)-1);         /* remove brackets from domain literal host */
        gbuf[strlen(gbuf)-1] = '\0';
        hostp = gbuf;
        }
    }
if (host) {                                             /* host wanted? */
    if (hostp != NULL) {
        if (strlen(hostp) >= host_len)
            return -1;                                  /* no room */
        else
            if (('\0' != hostp[0]) || (default_host == NULL))
                strcpy (host, hostp);
            else
                if (strlen(default_host) >= host_len)
                    return -1;                          /* no room */
                else
                    strcpy (host, default_host);
        }
    else {
        if (default_host) {
            if (strlen(default_host) >= host_len)
                return -1;                              /* no room */
            else
                strcpy (host, default_host);
            }
        }
    }
if (validate_addr) {
    struct addrinfo *ai_host, *ai_validate, *ai, *aiv;
    int status;

    if (hostp == NULL)
        return -1;
    if (p_getaddrinfo(hostp, NULL, NULL, &ai_host))
        return -1;
    if (p_getaddrinfo(validate_addr, NULL, NULL, &ai_validate)) {
        p_freeaddrinfo (ai_host);
        return -1;
        }
    status = -1;
    for (ai = ai_host; ai != NULL; ai = ai->ai_next) {
        for (aiv = ai_validate; aiv != NULL; aiv = aiv->ai_next) {
            if ((ai->ai_addrlen == aiv->ai_addrlen) &&
                (ai->ai_family == aiv->ai_family) &&
                (0 == memcmp (ai->ai_addr, aiv->ai_addr, ai->ai_addrlen))) {
                status = 0;
                break;
                }
            }
        }
    if (status != 0) {
        /* be generous and allow successful validations against variations of localhost addresses */
        if (((0 == strcmp("127.0.0.1", hostp)) && 
             (0 == strcmp("::1", validate_addr))) ||
            ((0 == strcmp("127.0.0.1", validate_addr)) && 
             (0 == strcmp("::1", hostp))))
            status = 0;
        }
    p_freeaddrinfo (ai_host);
    p_freeaddrinfo (ai_validate);
    return status;
    }
return 0;
}

/* sim_parse_addr_ex    localport:host:port

   Presumption is that the input, if it doesn't contain a ':' character is a port specifier.
   If the host field contains one or more colon characters (i.e. it is an IPv6 address), 
   the IPv6 address MUST be enclosed in square bracket characters (i.e. Domain Literal format)

        llll:w.x.y.z:rrrr
        llll:name.domain.com:rrrr
        llll::rrrr
        rrrr
        w.x.y.z:rrrr
        [w.x.y.z]:rrrr
        name.domain.com:rrrr

   Inputs:
        cptr    =       pointer to input string
        default_host
                =       optional pointer to default host if none specified
        host_len =      length of host buffer
        default_port
                =       optional pointer to default port if none specified
        port_len =      length of port buffer

   Outputs:
        host    =       pointer to buffer for IP address (may be NULL), 0 = none
        port    =       pointer to buffer for IP port (may be NULL), 0 = none
        localport
                =       pointer to buffer for local IP port (may be NULL), 0 = none
        result  =       status (SCPE_OK on complete success or SCPE_ARG if 
                        parsing can't happen due to bad syntax, a value is 
                        out of range, a result can't fit into a result buffer, 
                        a service name doesn't exist, or a validation name 
                        doesn't match the parsed host)
*/
int sim_parse_addr_ex (const char *cptr, char *host, size_t hostlen, const char *default_host, char *port, size_t port_len, char *localport, size_t localport_len, const char *default_port)
{
const char *hostp;

if ((localport != NULL) && (localport_len != 0))
    memset (localport, 0, localport_len);
hostp = strchr (cptr, ':');
if ((hostp != NULL) && ((hostp[1] == '[') || (NULL != strchr (hostp+1, ':')))) {
    if ((localport != NULL) && (localport_len != 0)) {
        localport_len -= 1;
        if (localport_len > (size_t)(hostp-cptr))
            localport_len = (size_t)(hostp-cptr);
        memcpy (localport, cptr, localport_len);
        }
    return sim_parse_addr (hostp+1, host, hostlen, default_host, port, port_len, default_port, NULL);
    }
return sim_parse_addr (cptr, host, hostlen, default_host, port, port_len, default_port, NULL);
}


void sim_init_sock (void)
{
#if defined (_WIN32)
int err;
WORD wVersionRequested; 
WSADATA wsaData; 
wVersionRequested = MAKEWORD (2, 2);

err = WSAStartup (wVersionRequested, &wsaData);         /* start Winsock */ 
if (err != 0)
    sim_printf ("Winsock: startup error %d\n", err);
#if defined(AF_INET6)
load_ws2 ();
#endif                                                  /* endif AF_INET6 */
#else                                                   /* Use native addrinfo APIs */
#if defined(AF_INET6)
    p_getaddrinfo = (getaddrinfo_func)getaddrinfo;
    p_getnameinfo = (getnameinfo_func)getnameinfo;
    p_freeaddrinfo = (freeaddrinfo_func)freeaddrinfo;
#else
    /* Native APIs not available, connect stubs */
    p_getaddrinfo = (getaddrinfo_func)s_getaddrinfo;
    p_getnameinfo = (getnameinfo_func)s_getnameinfo;
    p_freeaddrinfo = (freeaddrinfo_func)s_freeaddrinfo;
#endif                                                  /* endif AF_INET6 */
#endif                                                  /* endif _WIN32 */
#if defined (SIGPIPE)
signal (SIGPIPE, SIG_IGN);                              /* no pipe signals */
#endif
}

void sim_cleanup_sock (void)
{
#if defined (_WIN32)
WSACleanup ();
#endif
}

#if defined (_WIN32)                                    /* Windows */
static int sim_setnonblock (SOCKET sock)
{
unsigned long non_block = 1;

return ioctlsocket (sock, FIONBIO, &non_block);         /* set nonblocking */
}

#elif defined (VMS)                                     /* VMS */
static int sim_setnonblock (SOCKET sock)
{
int non_block = 1;

return ioctl (sock, FIONBIO, &non_block);               /* set nonblocking */
}

#else                                                   /* Mac, Unix, OS/2 */
static int sim_setnonblock (SOCKET sock)
{
int fl, sta;

fl = fcntl (sock, F_GETFL,0);                           /* get flags */
if (fl == -1)
    return SOCKET_ERROR;
sta = fcntl (sock, F_SETFL, fl | O_NONBLOCK);           /* set nonblock */
if (sta == -1)
    return SOCKET_ERROR;
#if !defined (macintosh) && !defined (__EMX__) && \
    !defined (__HAIKU__)                                /* Unix only */
sta = fcntl (sock, F_SETOWN, getpid());                 /* set ownership */
if (sta == -1)
    return SOCKET_ERROR;
#endif
return 0;
}

#endif                                                  /* endif !Win32 && !VMS */

static int sim_setnodelay (SOCKET sock)
{
int nodelay = 1;
int sta;

/* disable Nagle algorithm */
sta = setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof(nodelay));
if (sta == -1)
    return SOCKET_ERROR;

#if defined(TCP_NODELAYACK)
/* disable delayed ack algorithm */
sta = setsockopt (sock, IPPROTO_TCP, TCP_NODELAYACK, (char *)&nodelay, sizeof(nodelay));
if (sta == -1)
    return SOCKET_ERROR;
#endif

#if defined(TCP_QUICKACK)
/* disable delayed ack algorithm */
sta = setsockopt (sock, IPPROTO_TCP, TCP_QUICKACK, (char *)&nodelay, sizeof(nodelay));
if (sta == -1)
    return SOCKET_ERROR;
#endif

return sta;
}

static SOCKET sim_create_sock (int af, int opt_flags)
{
SOCKET newsock;
int err;

newsock = socket (af, ((opt_flags & SIM_SOCK_OPT_DATAGRAM) ? SOCK_DGRAM : SOCK_STREAM), 0);/* create socket */
if (newsock == INVALID_SOCKET) {                        /* socket error? */
    err = WSAGetLastError ();
#if defined(WSAEAFNOSUPPORT)
    if (err == WSAEAFNOSUPPORT)                         /* expected error, just return */
        return newsock;
#endif
    return sim_err_sock (newsock, "socket");            /* report error and return */
    }
return newsock;
}

/*
   Some platforms and/or network stacks have varying support for listening on 
   an IPv6 socket and receiving connections from both IPv4 and IPv6 client 
   connections.  This is known as IPv4-Mapped.  Some platforms claim such 
   support (i.e. some Windows versions), but it doesn't work in all cases.
*/

SOCKET sim_master_sock_ex (const char *hostport, int *parse_status, int opt_flags)
{
SOCKET newsock = INVALID_SOCKET;
int sta;
char host[CBUFSIZE], port[CBUFSIZE];
int r;
struct addrinfo hints;
struct addrinfo *result = NULL, *preferred;

r = sim_parse_addr (hostport, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL);
if (parse_status)
    *parse_status = r;
if (r)
    return newsock;

memset(&hints, 0, sizeof(hints));
hints.ai_flags = AI_PASSIVE;
hints.ai_family = AF_UNSPEC;
hints.ai_protocol = IPPROTO_TCP;
hints.ai_socktype = SOCK_STREAM;
if (p_getaddrinfo(host[0] ? host : NULL, port[0] ? port : NULL, &hints, &result)) {
    if (parse_status)
        *parse_status = -1;
    return newsock;
    }
preferred = result;
#ifdef IPV6_V6ONLY
/*
    When we can create a dual stack socket, be sure to find the IPv6 addrinfo 
    to bind to.
*/
for (; preferred != NULL; preferred = preferred->ai_next) {
    if (preferred->ai_family == AF_INET6)
        break;
    }
if (preferred == NULL)
    preferred = result;
#endif
retry:
newsock = sim_create_sock (preferred->ai_family, 0);    /* create socket */
if (newsock == INVALID_SOCKET) {                        /* socket error? */
#ifndef IPV6_V6ONLY
    if (preferred->ai_next) {
        preferred = preferred->ai_next;
        goto retry;
        }
#else
    if ((preferred->ai_family == AF_INET6) &&
        (preferred != result)) {
        preferred = result;
        goto retry;
        }
#endif
    p_freeaddrinfo(result);
    return newsock;
    }
#ifdef IPV6_V6ONLY
if (preferred->ai_family == AF_INET6) {
    int off = 0;
    sta = setsockopt (newsock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off));
    }
#endif
if (opt_flags & SIM_SOCK_OPT_REUSEADDR) {
    int on = 1;

    sta = setsockopt (newsock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    }
#if defined (SO_EXCLUSIVEADDRUSE)
else {
    int on = 1;

    sta = setsockopt (newsock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&on, sizeof(on));
    }
#endif
sta = bind (newsock, preferred->ai_addr, preferred->ai_addrlen);
p_freeaddrinfo(result);
if (sta == SOCKET_ERROR)                                /* bind error? */
    return sim_err_sock (newsock, "bind");
if (!(opt_flags & SIM_SOCK_OPT_BLOCKING)) {
    sta = sim_setnonblock (newsock);                    /* set nonblocking */
    if (sta == SOCKET_ERROR)                            /* fcntl error? */
        return sim_err_sock (newsock, "fcntl");
    }
sta = listen (newsock, 1);                              /* listen on socket */
if (sta == SOCKET_ERROR)                                /* listen error? */
    return sim_err_sock (newsock, "listen");
return newsock;                                         /* got it! */
}

SOCKET sim_connect_sock_ex (const char *sourcehostport, const char *hostport, const char *default_host, const char *default_port, int opt_flags)
{
SOCKET newsock = INVALID_SOCKET;
int sta;
char host[CBUFSIZE], port[CBUFSIZE];
struct addrinfo hints;
struct addrinfo *result = NULL, *source = NULL;

if (sim_parse_addr (hostport, host, sizeof(host), default_host, port, sizeof(port), default_port, NULL))
    return INVALID_SOCKET;

memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_UNSPEC;
hints.ai_protocol = ((opt_flags & SIM_SOCK_OPT_DATAGRAM) ? IPPROTO_UDP : IPPROTO_TCP);
hints.ai_socktype = ((opt_flags & SIM_SOCK_OPT_DATAGRAM) ? SOCK_DGRAM : SOCK_STREAM);
if (p_getaddrinfo(host[0] ? host : NULL, port[0] ? port : NULL, &hints, &result))
    return INVALID_SOCKET;

if (sourcehostport) {

    /* Validate the local/source side address which we'll bind to */
    if (sim_parse_addr (sourcehostport, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL)) {
        p_freeaddrinfo (result);
        return INVALID_SOCKET;
        }

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = result->ai_family;                /* Same family as connect destination */
    hints.ai_protocol = ((opt_flags & SIM_SOCK_OPT_DATAGRAM) ? IPPROTO_UDP : IPPROTO_TCP);
    hints.ai_socktype = ((opt_flags & SIM_SOCK_OPT_DATAGRAM) ? SOCK_DGRAM : SOCK_STREAM);
    if (p_getaddrinfo(host[0] ? host : NULL, port[0] ? port : NULL, &hints, &source)) {
        p_freeaddrinfo (result);
        return INVALID_SOCKET;
        }

    newsock = sim_create_sock (result->ai_family, opt_flags & SIM_SOCK_OPT_DATAGRAM);/* create socket */
    if (newsock == INVALID_SOCKET) {                    /* socket error? */
        p_freeaddrinfo (result);
        p_freeaddrinfo (source);
        return newsock;
        }

    sta = bind (newsock, source->ai_addr, source->ai_addrlen);
    p_freeaddrinfo(source);
    source = NULL;
    if (sta == SOCKET_ERROR) {                          /* bind error? */
        p_freeaddrinfo (result);
        return sim_err_sock (newsock, "bind");
        }
    }

if (newsock == INVALID_SOCKET) {                        /* socket error? */
    newsock = sim_create_sock (result->ai_family, opt_flags & SIM_SOCK_OPT_DATAGRAM);/* create socket */
    if (newsock == INVALID_SOCKET) {                    /* socket error? */
        p_freeaddrinfo (result);
        return newsock;
        }
    }

if (!(opt_flags & SIM_SOCK_OPT_BLOCKING)) {
    sta = sim_setnonblock (newsock);                    /* set nonblocking */
    if (sta == SOCKET_ERROR) {                          /* fcntl error? */
        p_freeaddrinfo (result);
        return sim_err_sock (newsock, "fcntl");
        }
    }
if ((!(opt_flags & SIM_SOCK_OPT_DATAGRAM)) && (opt_flags & SIM_SOCK_OPT_NODELAY)) {
    sta = sim_setnodelay (newsock);                     /* set nodelay */
    if (sta == SOCKET_ERROR) {                          /* setsock error? */
        p_freeaddrinfo (result);
        return sim_err_sock (newsock, "setnodelay");
        }
    }
if (!(opt_flags & SIM_SOCK_OPT_DATAGRAM)) {
    int keepalive = 1;

    /* enable TCP Keep Alives */
    sta = setsockopt (newsock, SOL_SOCKET, SO_KEEPALIVE, (char *)&keepalive, sizeof(keepalive));
    if (sta == -1) 
        return sim_err_sock (newsock, "setsockopt KEEPALIVE");
    }
sta = connect (newsock, result->ai_addr, result->ai_addrlen);
p_freeaddrinfo (result);
if (sta == SOCKET_ERROR) {
    if (opt_flags & SIM_SOCK_OPT_BLOCKING) {
        if ((WSAGetLastError () == WSAETIMEDOUT)    ||                        /* expected errors after a connect failure */
            (WSAGetLastError () == WSAEHOSTUNREACH) ||
            (WSAGetLastError () == WSAECONNREFUSED) ||
            (WSAGetLastError () == WSAECONNABORTED) ||
            (WSAGetLastError () == WSAECONNRESET)) {
            sim_close_sock (newsock);
            newsock = INVALID_SOCKET;
            }
        else
            return sim_err_sock (newsock, "connect");
        }
    else    /* Non Blocking case won't return errors until some future read */
        if ((WSAGetLastError () != WSAEWOULDBLOCK) &&
            (WSAGetLastError () != WSAEINPROGRESS))
            return sim_err_sock (newsock, "connect");
    }
return newsock;                                         /* got it! */
}

SOCKET sim_accept_conn_ex (SOCKET master, char **connectaddr, int opt_flags)
{
int sta = 0, err;
int keepalive = 1;
#if defined (macintosh) || defined (__linux) || defined (__linux__) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__) || \
    (defined(__hpux) && defined(_XOPEN_SOURCE_EXTENDED)) || \
    defined (__HAIKU__) || defined(__CYGWIN__)
socklen_t size;
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__)) || \
     defined (__hpux)
int size;
#else 
size_t size; 
#endif
SOCKET newsock;
struct sockaddr_storage clientname;

if (master == 0)                                        /* not attached? */
    return INVALID_SOCKET;
size = sizeof (clientname);
memset (&clientname, 0, sizeof(clientname));
newsock = accept (master, (struct sockaddr *) &clientname, &size);
if (newsock == INVALID_SOCKET) {                        /* error? */
    err = WSAGetLastError ();
    if (err != WSAEWOULDBLOCK)
        sim_err_sock(newsock, "accept");
    return INVALID_SOCKET;
    }
if (connectaddr != NULL) {
    *connectaddr = (char *)calloc(1, NI_MAXHOST+1);
#ifdef AF_INET6
    p_getnameinfo((struct sockaddr *)&clientname, size, *connectaddr, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
    if (0 == memcmp("::ffff:", *connectaddr, 7))        /* is this a IPv4-mapped IPv6 address? */
        memmove(*connectaddr, 7+*connectaddr,           /* prefer bare IPv4 address */
                strlen(*connectaddr) - 7 + 1);          /* length to include terminating \0 */
#else
    strcpy(*connectaddr, inet_ntoa(((struct sockaddr_in *)&connectaddr)->s_addr));
#endif
    }

if (!(opt_flags & SIM_SOCK_OPT_BLOCKING)) {
    sta = sim_setnonblock (newsock);                    /* set nonblocking */
    if (sta == SOCKET_ERROR)                            /* fcntl error? */
        return sim_err_sock (newsock, "fcntl");
    }

if ((opt_flags & SIM_SOCK_OPT_NODELAY)) {
    sta = sim_setnodelay (newsock);                     /* set nonblocking */
    if (sta == SOCKET_ERROR)                            /* setsockopt error? */
        return sim_err_sock (newsock, "setnodelay");
    }

/* enable TCP Keep Alives */
sta = setsockopt (newsock, SOL_SOCKET, SO_KEEPALIVE, (char *)&keepalive, sizeof(keepalive));
if (sta == -1) 
    return sim_err_sock (newsock, "setsockopt KEEPALIVE");

return newsock;
}

int sim_check_conn (SOCKET sock, int rd)
{
fd_set rw_set, er_set;
fd_set *rw_p = &rw_set;
fd_set *er_p = &er_set;
struct timeval zero;
struct sockaddr_storage peername;
#if defined (macintosh) || defined (__linux) || defined (__linux__) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__) || \
    (defined(__hpux) && defined(_XOPEN_SOURCE_EXTENDED)) || \
    defined (__HAIKU__) || defined(__CYGWIN__)
socklen_t peernamesize = (socklen_t)sizeof(peername);
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__)) || \
     defined (__hpux)
int peernamesize = (int)sizeof(peername);
#else 
size_t peernamesize = sizeof(peername); 
#endif

memset (&zero, 0, sizeof(zero));
FD_ZERO (rw_p);
FD_ZERO (er_p);
FD_SET (sock, rw_p);
FD_SET (sock, er_p);
if (rd)
    (void)select ((int) sock + 1, rw_p, NULL, er_p, &zero);
else
    (void)select ((int) sock + 1, NULL, rw_p, er_p, &zero);
if (FD_ISSET (sock, er_p))
    return -1;
if (FD_ISSET (sock, rw_p)) {
    if (0 == getpeername (sock, (struct sockaddr *)&peername, &peernamesize))
        return 1;
    else
        return -1;
    }
return 0;
}

static int _sim_getaddrname (struct sockaddr *addr, size_t addrsize, char *hostnamebuf, char *portnamebuf)
{
#if defined (macintosh) || defined (__linux) || defined (__linux__) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__) || \
    (defined(__hpux) && defined(_XOPEN_SOURCE_EXTENDED)) || \
    defined (__HAIKU__) || defined(__CYGWIN__)
socklen_t size = (socklen_t)addrsize;
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__)) || \
     defined (__hpux)
int size = (int)addrsize;
#else 
size_t size = addrsize; 
#endif
int ret = 0;

#ifdef AF_INET6
*hostnamebuf = '\0';
*portnamebuf = '\0';
ret = p_getnameinfo(addr, size, hostnamebuf, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
if (0 == memcmp("::ffff:", hostnamebuf, 7))        /* is this a IPv4-mapped IPv6 address? */
    memmove(hostnamebuf, 7+hostnamebuf,            /* prefer bare IPv4 address */
            strlen(hostnamebuf) + 7 - 1);          /* length to include terminating \0 */
if (!ret)
    ret = p_getnameinfo(addr, size, NULL, 0, portnamebuf, NI_MAXSERV, NI_NUMERICSERV);
#else
strcpy(hostnamebuf, inet_ntoa(((struct sockaddr_in *)addr)->s_addr));
sprintf(portnamebuf, "%d", (int)ntohs(((struct sockaddr_in *)addr)->s_port)));
#endif
return ret;
}

int sim_getnames_sock (SOCKET sock, char **socknamebuf, char **peernamebuf)
{
struct sockaddr_storage sockname, peername;
#if defined (macintosh) || defined (__linux) || defined (__linux__) || \
    defined (__APPLE__) || defined (__OpenBSD__) || \
    defined(__NetBSD__) || defined(__FreeBSD__) || \
    (defined(__hpux) && defined(_XOPEN_SOURCE_EXTENDED)) || \
    defined (__HAIKU__) || defined(__CYGWIN__)
socklen_t socknamesize = (socklen_t)sizeof(sockname);
socklen_t peernamesize = (socklen_t)sizeof(peername);
#elif defined (_WIN32) || defined (__EMX__) || \
     (defined (__ALPHA) && defined (__unix__)) || \
     defined (__hpux)
int socknamesize = (int)sizeof(sockname);
int peernamesize = (int)sizeof(peername);
#else 
size_t socknamesize = sizeof(sockname); 
size_t peernamesize = sizeof(peername); 
#endif
char hostbuf[NI_MAXHOST+1];
char portbuf[NI_MAXSERV+1];

if (socknamebuf)
    *socknamebuf = (char *)calloc(1, NI_MAXHOST+NI_MAXSERV+4);
if (peernamebuf)
    *peernamebuf = (char *)calloc(1, NI_MAXHOST+NI_MAXSERV+4);
(void)getsockname (sock, (struct sockaddr *)&sockname, &socknamesize);
(void)getpeername (sock, (struct sockaddr *)&peername, &peernamesize);
if (socknamebuf != NULL) {
    _sim_getaddrname ((struct sockaddr *)&sockname, (size_t)socknamesize, hostbuf, portbuf);
    sprintf(*socknamebuf, "[%s]:%s", hostbuf, portbuf);
    }
if (peernamebuf != NULL) {
    _sim_getaddrname ((struct sockaddr *)&peername, (size_t)peernamesize, hostbuf, portbuf);
    sprintf(*peernamebuf, "[%s]:%s", hostbuf, portbuf);
    }
return 0;
}


int sim_read_sock (SOCKET sock, char *buf, int nbytes)
{
int rbytes, err;

rbytes = recv (sock, buf, nbytes, 0);
if (rbytes == 0)                                        /* disconnect */
    return -1;
if (rbytes == SOCKET_ERROR) {
    err = WSAGetLastError ();
    if (err == WSAEWOULDBLOCK)                          /* no data */
        return 0;
#if defined(EAGAIN)
    if (err == EAGAIN)                                  /* no data */
        return 0;
#endif
    if ((err != WSAETIMEDOUT) &&                        /* expected errors after a connect failure */
        (err != WSAEHOSTUNREACH) &&
        (err != WSAECONNREFUSED) &&
        (err != WSAECONNABORTED) &&
        (err != WSAECONNRESET) &&
        (err != WSAEINTR))                              /* or a close of a blocking read */
        sim_err_sock (INVALID_SOCKET, "read");
    return -1;
    }
return rbytes;
}

int sim_write_sock (SOCKET sock, const char *msg, int nbytes)
{
int err, sbytes = send (sock, msg, nbytes, 0);

if (sbytes == SOCKET_ERROR) {
    err = WSAGetLastError ();
    if (err == WSAEWOULDBLOCK)                          /* no data */
        return 0;
#if defined(EAGAIN)
    if (err == EAGAIN)                                  /* no data */
        return 0;
#endif
    }
return sbytes;
}

void sim_close_sock (SOCKET sock)
{
shutdown(sock, SD_BOTH);
closesocket (sock);
}

#endif                                                  /* end else !implemented */

#ifdef  __cplusplus
}
#endif
