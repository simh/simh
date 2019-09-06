/* sim_slirp.c:
  ------------------------------------------------------------------------------
   Copyright (c) 2015, Mark Pizzolato

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  ------------------------------------------------------------------------------

   This module provides the interface needed between sim_ether and SLiRP to
   provide NAT network functionality.

*/

/* Actual slirp API interface support, some code taken from slirpvde.c */

#define DEFAULT_IP_ADDR "10.0.2.2"

#include "glib.h"
#include "qemu/timer.h"
#include "libslirp.h"
#include "sim_defs.h"
#include "sim_slirp.h"
#include "sim_sock.h"
#include "libslirp.h"

#if !defined (USE_READER_THREAD)
#define pthread_mutex_init(mtx, val)
#define pthread_mutex_destroy(mtx)
#define pthread_mutex_lock(mtx)
#define pthread_mutex_unlock(mtx)
#define pthread_mutex_t int
#endif

#define IS_TCP 0
#define IS_UDP 1
static const char *tcpudp[] = {
    "TCP",
    "UDP"
    };

struct redir_tcp_udp {
    struct in_addr inaddr;
    int is_udp;
    int port;
    int lport;
    struct redir_tcp_udp *next;
    };

static int
_parse_redirect_port (struct redir_tcp_udp **head, const char *buff, int is_udp)
{
char gbuf[4*CBUFSIZE];
uint32 inaddr = 0;
int port = 0;
int lport = 0;
char *ipaddrstr = NULL;
char *portstr = NULL;
struct redir_tcp_udp *newp;

gbuf[sizeof(gbuf)-1] = '\0';
strncpy (gbuf, buff, sizeof(gbuf)-1);
if (((ipaddrstr = strchr(gbuf, ':')) == NULL) || (*(ipaddrstr+1) == 0)) {
    sim_printf ("redir %s syntax error\n", tcpudp[is_udp]);
    return -1;
    }
*ipaddrstr++ = 0;

if ((ipaddrstr) && 
    (((portstr = strchr (ipaddrstr, ':')) == NULL) || (*(portstr+1) == 0))) {
    sim_printf ("redir %s syntax error\n", tcpudp[is_udp]);
    return -1;
    }
*portstr++ = 0;

sscanf (gbuf, "%d", &lport);
sscanf (portstr, "%d", &port);
if (ipaddrstr) 
    inaddr = inet_addr (ipaddrstr);

if (!inaddr) {
    sim_printf ("%s redirection error: an IP address must be specified\n", tcpudp[is_udp]);
    return -1;
    }

if ((newp = (struct redir_tcp_udp *)g_malloc (sizeof(struct redir_tcp_udp))) == NULL)
    return -1;
else {
    inet_aton (ipaddrstr, &newp->inaddr);
    newp->is_udp = is_udp;
    newp->port = port;
    newp->lport = lport;
    newp->next = *head;
    *head = newp;
    return 0;
    }
}

static int _do_redirects (Slirp *slirp, struct redir_tcp_udp *head)
{
struct in_addr host_addr;
int ret = 0;

host_addr.s_addr = htonl(INADDR_ANY);
if (head) {
    ret = _do_redirects (slirp, head->next);
    if (slirp_add_hostfwd (slirp, head->is_udp, host_addr, head->lport, head->inaddr, head->port) < 0) {
        sim_printf("Can't establish redirector for: redir %s   =%d:%s:%d\n", 
                   tcpudp[head->is_udp], head->lport, inet_ntoa(head->inaddr), head->port);
        ++ret;
        }
    }
return ret;
}

struct slirp_write_request {
    struct slirp_write_request *next;
    char msg[1518];
    size_t len;
    };

struct sim_slirp {
    Slirp *slirp;
    char *args;
    struct in_addr vnetwork;
    struct in_addr vnetmask;
    int maskbits;
    struct in_addr vgateway;
    int dhcpmgmt;
    struct in_addr vdhcp_start;
    struct in_addr vnameserver;
    char *boot_file;
    char *tftp_path;
    char *dns_search;
    char **dns_search_domains;
    struct redir_tcp_udp *rtcp;
    GArray *gpollfds;
    SOCKET db_chime;            /* write packet doorbell */
    struct slirp_write_request *write_requests;
    struct slirp_write_request *write_buffers;
    pthread_mutex_t write_buffer_lock;
    void *opaque;               /* opaque value passed during packet delivery */
    packet_callback callback;   /* slirp arriving packet delivery callback */
    DEVICE *dptr;
    uint32 dbit;
    };

#if defined(__cplusplus)
extern "C" {
#endif
DEVICE *slirp_dptr;
uint32 slirp_dbit;
#if defined(__cplusplus)
}
#endif

SLIRP *sim_slirp_open (const char *args, void *opaque, packet_callback callback, DEVICE *dptr, uint32 dbit, char *errbuf, size_t errbuf_size)
{
SLIRP *slirp = (SLIRP *)g_malloc0(sizeof(*slirp));
char *targs = g_strdup (args);
const char *tptr = targs;
const char *cptr;
char tbuf[CBUFSIZE], gbuf[CBUFSIZE], abuf[CBUFSIZE];
int err;

slirp_dptr = dptr;
slirp_dbit = dbit;
slirp->args = (char *)g_malloc0(1 + strlen(args));
strcpy (slirp->args, args);
slirp->opaque = opaque;
slirp->callback = callback;
slirp->maskbits = 24;
slirp->dhcpmgmt = 1;
slirp->db_chime = INVALID_SOCKET;
inet_aton(DEFAULT_IP_ADDR,&slirp->vgateway);
pthread_mutex_init (&slirp->write_buffer_lock, NULL);

err = 0;
while (*tptr && !err) {
    tptr = get_glyph_nc (tptr, tbuf, ',');
    if (!tbuf[0])
        break;
    cptr = tbuf;
    cptr = get_glyph (cptr, gbuf, '=');
    if (0 == MATCH_CMD (gbuf, "DHCP")) {
        slirp->dhcpmgmt = 1;
        if (cptr && *cptr)
            inet_aton (cptr, &slirp->vdhcp_start);
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "TFTP")) {
        if (cptr && *cptr)
            slirp->tftp_path = g_strdup (cptr);
        else {
            strlcpy (errbuf, "Missing TFTP Path", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "BOOTFILE")) {
        if (cptr && *cptr)
            slirp->boot_file = g_strdup (cptr);
        else {
            strlcpy (errbuf, "Missing DHCP Boot file name", errbuf_size);
            err = 1;
            }
        continue;
        }
    if ((0 == MATCH_CMD (gbuf, "NAMESERVER")) ||
        (0 == MATCH_CMD (gbuf, "DNS"))) {
        if (cptr && *cptr)
            inet_aton (cptr, &slirp->vnameserver);
        else {
            strlcpy (errbuf, "Missing nameserver", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "DNSSEARCH")) {
        if (cptr && *cptr) {
            int count = 0;
            char *name;
           
            slirp->dns_search = g_strdup (cptr);
            name = slirp->dns_search;
            do {
                ++count;
                slirp->dns_search_domains = (char **)realloc (slirp->dns_search_domains, (count + 1)*sizeof(char *));
                slirp->dns_search_domains[count] = NULL;
                slirp->dns_search_domains[count-1] = name;
                name = strchr (name, ':');
                if (name) {
                    *name = '\0';
                    ++name;
                    }
                } while (name && *name);
            }
        else {
            strlcpy (errbuf, "Missing DNS search list", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "GATEWAY")) {
        if (cptr && *cptr) {
            cptr = get_glyph (cptr, abuf, '/');
            if (cptr && *cptr)
                slirp->maskbits = atoi (cptr);
            inet_aton (abuf, &slirp->vgateway);
            }
        else {
            strlcpy (errbuf, "Missing host", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "NETWORK")) {
        if (cptr && *cptr) {
            cptr = get_glyph (cptr, abuf, '/');
            if (cptr && *cptr)
                slirp->maskbits = atoi (cptr);
            inet_aton (abuf, &slirp->vnetwork);
            }
        else {
            strlcpy (errbuf, "Missing network", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "NODHCP")) {
        slirp->dhcpmgmt = 0;
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "UDP")) {
        if (cptr && *cptr)
            err = _parse_redirect_port (&slirp->rtcp, cptr, IS_UDP);
        else {
            strlcpy (errbuf, "Missing UDP port mapping", errbuf_size);
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "TCP")) {
        if (cptr && *cptr)
            err = _parse_redirect_port (&slirp->rtcp, cptr, IS_TCP);
        else {
            strlcpy (errbuf, "Missing TCP port mapping", errbuf_size);
            err = 1;
            }
        continue;
        }
    snprintf (errbuf, errbuf_size - 1, "Unexpected NAT argument: %s", gbuf);
    err = 1;
    }
if (err) {
    sim_slirp_close (slirp);
    g_free (targs);
    return NULL;
    }

slirp->vnetmask.s_addr = slirp->maskbits ? htonl(~((1 << (32-slirp->maskbits)) - 1)) : 0xFFFFFFFF;
slirp->vnetwork.s_addr = slirp->vgateway.s_addr & slirp->vnetmask.s_addr;
if ((slirp->vgateway.s_addr & ~slirp->vnetmask.s_addr) == 0)
    slirp->vgateway.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 2);
if ((slirp->vdhcp_start.s_addr == 0) && slirp->dhcpmgmt)
    slirp->vdhcp_start.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 15);
if (slirp->vnameserver.s_addr == 0)
    slirp->vnameserver.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 3);
slirp->slirp = slirp_init (0, slirp->vnetwork, slirp->vnetmask, slirp->vgateway, 
                           NULL, slirp->tftp_path, slirp->boot_file, 
                           slirp->vdhcp_start, slirp->vnameserver, 
                           (const char **)(slirp->dns_search_domains), (void *)slirp);

if (_do_redirects (slirp->slirp, slirp->rtcp)) {
    sim_slirp_close (slirp);
    slirp = NULL;
    }
else {
    char db_host[32];
    GPollFD pfd;
    int64_t rnd_val = qemu_clock_get_ns ((QEMUClockType)0) / 1000000;

    slirp->gpollfds = g_array_new(FALSE, FALSE, sizeof(GPollFD));
    /* setup transmit packet wakeup doorbell */
    do {
        if ((rnd_val & 0xFFFF) == 0)
            ++rnd_val;
        sprintf (db_host, "localhost:%d", (int)(rnd_val & 0xFFFF));
        slirp->db_chime  = sim_connect_sock_ex (db_host, db_host, NULL, NULL, SIM_SOCK_OPT_DATAGRAM);
        } while (slirp->db_chime == INVALID_SOCKET);
    memset (&pfd, 0, sizeof (pfd));
    pfd.fd = slirp->db_chime;
    pfd.events = G_IO_IN;
    g_array_append_val(slirp->gpollfds, pfd);
    slirp->dbit = dbit;
    slirp->dptr = dptr;
    
    sim_slirp_show(slirp, stdout);
    if (sim_log && (sim_log != stdout))
        sim_slirp_show(slirp, sim_log);
    if (sim_deb && (sim_deb != stdout) && (sim_deb != sim_log))
        sim_slirp_show(slirp, sim_deb);
    }
g_free (targs);
return slirp;
}

void sim_slirp_close (SLIRP *slirp)
{
struct redir_tcp_udp *rtmp;

if (slirp) {
    g_free (slirp->args);
    g_free (slirp->tftp_path);
    g_free (slirp->boot_file);
    g_free (slirp->dns_search);
    g_free (slirp->dns_search_domains);
    while ((rtmp = slirp->rtcp)) {
        slirp_remove_hostfwd(slirp->slirp, rtmp->is_udp, rtmp->inaddr, rtmp->lport);
        slirp->rtcp = rtmp->next;
        g_free (rtmp);
        }
    g_array_free(slirp->gpollfds, true);
    if (slirp->db_chime != INVALID_SOCKET)
        closesocket (slirp->db_chime);
    if (1) {
        struct slirp_write_request *buffer;

        while (NULL != (buffer = slirp->write_buffers)) {
            slirp->write_buffers = buffer->next;
            free(buffer);
            }
        while (NULL != (buffer = slirp->write_requests)) {
            slirp->write_requests = buffer->next;
            free(buffer);
            }
        }
    pthread_mutex_destroy (&slirp->write_buffer_lock);
    if (slirp->slirp)
        slirp_cleanup(slirp->slirp);
    }
g_free (slirp);
}

t_stat sim_slirp_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "%s", 
"NAT options:\n"
"    DHCP{=dhcp_start_address}           Enables DHCP server and specifies\n"
"                                        guest LAN DHCP start IP address\n"
"    BOOTFILE=bootfilename               specifies DHCP returned Boot Filename\n"
"    TFTP=tftp-base-path                 Enables TFTP server and specifies\n"
"                                        base file path\n"
"    NAMESERVER=nameserver_ipaddres      specifies DHCP nameserver IP address\n"
"    DNS=nameserver_ipaddres             specifies DHCP nameserver IP address\n"
"    DNSSEARCH=domain{:domain{:domain}}  specifies DNS Domains search suffixes\n"
"    GATEWAY=host_ipaddress{/masklen}    specifies LAN gateway IP address\n"
"    NETWORK=network_ipaddress{/masklen} specifies LAN network address\n"
"    UDP=port:address:address's-port     maps host UDP port to guest port\n"
"    TCP=port:address:address's-port     maps host TCP port to guest port\n"
"    NODHCP                              disables DHCP server\n\n"
"Default NAT Options: GATEWAY=10.0.2.2, masklen=24(netmask is 255.255.255.0)\n"
"                     DHCP=10.0.2.15, NAMESERVER=10.0.2.3\n"
"    Nameserver defaults to proxy traffic to host system's active nameserver\n\n"
"The 'address' field in the UDP and TCP port mappings are the simulated\n"
"(guest) system's IP address which, if DHCP allocated would default to\n"
"10.0.2.15 or could be statically configured to any address including\n"
"10.0.2.4 thru 10.0.2.14.\n\n"
"NAT limitations\n\n"
"There are four limitations of NAT mode which users should be aware of:\n\n"
" 1) ICMP protocol limitations:\n"
"    Some frequently used network debugging tools (e.g. ping or tracerouting)\n"
"    rely on the ICMP protocol for sending/receiving messages. While some\n"
"    ICMP support is available on some hosts (ping may or may not work),\n"
"    some other tools may not work reliably.\n\n"
" 2) Receiving of UDP broadcasts is not reliable:\n"
"    The guest does not reliably receive broadcasts, since, in order to save\n"
"    resources, it only listens for a certain amount of time after the guest\n"
"    has sent UDP data on a particular port.\n\n"
" 3) Protocols such as GRE, DECnet, LAT and Clustering are unsupported:\n"
"    Protocols other than TCP and UDP are not supported.\n\n"
" 4) Forwarding host ports < 1024 impossible:\n"
"    On Unix-based hosts (e.g. Linux, Solaris, Mac OS X) it is not possible\n"
"    to bind to ports below 1024 from applications that are not run by root.\n"
"    As a result, if you try to configure such a port forwarding, the attach\n"
"    will fail.\n\n"
"These limitations normally don't affect standard network use. But the\n"
"presence of NAT has also subtle effects that may interfere with protocols\n"
"that are normally working. One example is NFS, where the server is often\n"
"configured to refuse connections from non-privileged ports (i.e. ports not\n"
" below 1024).\n"
);
return SCPE_OK;
}

int sim_slirp_send (SLIRP *slirp, const char *msg, size_t len, int flags)
{
struct slirp_write_request *request;
int wake_needed = 0;

if (!slirp) {
    errno = EBADF;
    return 0;
    }
/* Get a buffer */
pthread_mutex_lock (&slirp->write_buffer_lock);
if (NULL != (request = slirp->write_buffers))
    slirp->write_buffers = request->next;
pthread_mutex_unlock (&slirp->write_buffer_lock);
if (NULL == request)
    request = (struct slirp_write_request *)g_malloc(sizeof(*request));

/* Copy buffer contents */
request->len = len;
memcpy(request->msg, msg, len);

/* Insert buffer at the end of the write list (to make sure that */
/* packets make it to the wire in the order they were presented here) */
pthread_mutex_lock (&slirp->write_buffer_lock);
request->next = NULL;
if (slirp->write_requests) {
    struct slirp_write_request *last_request = slirp->write_requests;

    while (last_request->next) {
        last_request = last_request->next;
        }
    last_request->next = request;
    }
else {
    slirp->write_requests = request;
    wake_needed = 1;
    }
pthread_mutex_unlock (&slirp->write_buffer_lock);

if (wake_needed)
    sim_write_sock (slirp->db_chime, msg, 0);
return len;
}

void slirp_output (void *opaque, const uint8_t *pkt, int pkt_len)
{
SLIRP *slirp = (SLIRP *)opaque;

slirp->callback (slirp->opaque, pkt, pkt_len);
}

void sim_slirp_show (SLIRP *slirp, FILE *st)
{
struct redir_tcp_udp *rtmp;

if ((slirp == NULL) || (slirp->slirp == NULL))
    return;
fprintf (st, "NAT args: %s\n", slirp->args);
fprintf (st, "NAT network setup:\n");
fprintf (st, "        gateway       =%s/%d", inet_ntoa(slirp->vgateway), slirp->maskbits);
fprintf (st, "(%s)\n", inet_ntoa(slirp->vnetmask));
fprintf (st, "        DNS           =%s\n", inet_ntoa(slirp->vnameserver));
if (slirp->vdhcp_start.s_addr != 0)
    fprintf (st, "        dhcp_start    =%s\n", inet_ntoa(slirp->vdhcp_start));
if (slirp->boot_file)
    fprintf (st, "        dhcp bootfile =%s\n", slirp->boot_file);
if (slirp->dns_search_domains) {
    char **domains = slirp->dns_search_domains;
    
    fprintf (st, "        DNS domains   =");
    while (*domains) {
        fprintf (st, "%s%s", (domains != slirp->dns_search_domains) ? ", " : "", *domains);
        ++domains;
        }
    fprintf (st, "\n");
    }
if (slirp->tftp_path)
    fprintf (st, "        tftp prefix   =%s\n", slirp->tftp_path);
rtmp = slirp->rtcp;
while (rtmp) {
    fprintf (st, "        redir %3s     =%d:%s:%d\n", tcpudp[rtmp->is_udp], rtmp->lport, inet_ntoa(rtmp->inaddr), rtmp->port);
    rtmp = rtmp->next;
    }
slirp_connection_info (slirp->slirp, (Monitor *)st);
}

#if !defined(MAX)
#define MAX(a,b) (((a)>(b)) ? (a) : (b))
#endif

static int pollfds_fill (GArray *pollfds, fd_set *rfds, fd_set *wfds,
                         fd_set *xfds)
{
int nfds = -1;
guint i;

for (i = 0; i < pollfds->len; i++) {
    GPollFD *pfd = &g_array_index(pollfds, GPollFD, i);
    int fd = pfd->fd;
    int events = pfd->events;
    if (events & G_IO_IN) {
        FD_SET(fd, rfds);
        nfds = MAX(nfds, fd);
        }
    if (events & G_IO_OUT) {
        FD_SET(fd, wfds);
        nfds = MAX(nfds, fd);
        }
    if (events & (G_IO_PRI | G_IO_HUP | G_IO_ERR)) {
        FD_SET(fd, xfds);
        nfds = MAX(nfds, fd);
        }
    }
return nfds;
}

static void pollfds_poll (GArray *pollfds, int nfds, fd_set *rfds,
                          fd_set *wfds, fd_set *xfds)
{
guint i;

for (i = 0; i < pollfds->len; i++) {
    GPollFD *pfd = &g_array_index(pollfds, GPollFD, i);
    int fd = pfd->fd;
    int revents = 0;

    if (FD_ISSET(fd, rfds)) {
        revents |= G_IO_IN;
        }
    if (FD_ISSET(fd, wfds)) {
        revents |= G_IO_OUT;
        }
    if (FD_ISSET(fd, xfds)) {
        revents |= G_IO_PRI;
        }
    pfd->revents = revents & pfd->events;
    }
}

int sim_slirp_select (SLIRP *slirp, int ms_timeout)
{
int select_ret = 0;
uint32 slirp_timeout = ms_timeout;
struct timeval timeout;
fd_set rfds, wfds, xfds;
fd_set save_rfds, save_wfds, save_xfds;
int nfds;

if (!slirp)                         /* Not active? */
    return -1;                      /* That's an error */
/* Populate the GPollFDs from slirp */
g_array_set_size (slirp->gpollfds, 1);  /* Leave the doorbell chime alone */
slirp_pollfds_fill(slirp->gpollfds, &slirp_timeout);
timeout.tv_sec  = slirp_timeout / 1000;
timeout.tv_usec = (slirp_timeout % 1000) * 1000;

FD_ZERO(&rfds);
FD_ZERO(&wfds);
FD_ZERO(&xfds);
/* Extract the GPollFDs interest */
nfds = pollfds_fill (slirp->gpollfds, &rfds, &wfds, &xfds);
save_rfds = rfds;
save_wfds = wfds;
save_xfds = xfds;
select_ret = select(nfds + 1, &rfds, &wfds, &xfds, &timeout);
if (select_ret) {
    int i;
    /* Update the GPollFDs results */
    pollfds_poll (slirp->gpollfds, nfds, &rfds, &wfds, &xfds);
    if (FD_ISSET (slirp->db_chime, &rfds)) {
        char buf[32];
        /* consume the doorbell wakeup ring */
        (void)recv (slirp->db_chime, buf, sizeof (buf), 0);
        }
    sim_debug (slirp->dbit, slirp->dptr, "Select returned %d\r\n", select_ret);
    for (i=0; i<nfds+1; i++) {
        if (FD_ISSET(i, &rfds) || FD_ISSET(i, &save_rfds))
            sim_debug (slirp->dbit, slirp->dptr, "%d: save_rfd=%d, rfd=%d\r\n", i, FD_ISSET(i, &save_rfds), FD_ISSET(i, &rfds));
        if (FD_ISSET(i, &wfds) || FD_ISSET(i, &save_wfds))
            sim_debug (slirp->dbit, slirp->dptr, "%d: save_wfd=%d, wfd=%d\r\n", i, FD_ISSET(i, &save_wfds), FD_ISSET(i, &wfds));
        if (FD_ISSET(i, &xfds) || FD_ISSET(i, &save_xfds))
            sim_debug (slirp->dbit, slirp->dptr, "%d: save_xfd=%d, xfd=%d\r\n", i, FD_ISSET(i, &save_xfds), FD_ISSET(i, &xfds));
            }
    }
return select_ret + 1;  /* Force dispatch even on timeout */
}

void sim_slirp_dispatch (SLIRP *slirp)
{
struct slirp_write_request *request;

/* first deliver any transmit packets which are pending */

pthread_mutex_lock (&slirp->write_buffer_lock);
while (NULL != (request = slirp->write_requests)) {
    /* Pull buffer off request list */
    slirp->write_requests = request->next;
    pthread_mutex_unlock (&slirp->write_buffer_lock);

    slirp_input (slirp->slirp, (const uint8_t *)request->msg, (int)request->len);

    pthread_mutex_lock (&slirp->write_buffer_lock);
    /* Put buffer on free buffer list */
    request->next = slirp->write_buffers;
    slirp->write_buffers = request;
    }
pthread_mutex_unlock (&slirp->write_buffer_lock);

slirp_pollfds_poll(slirp->gpollfds, 0);

}

