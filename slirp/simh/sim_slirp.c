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
_parse_redirect_port (struct redir_tcp_udp **head, char *buff, int is_udp)
{
u_int32_t inaddr = 0;
int port = 0;
int lport = 0;
char *ipaddrstr = NULL;
char *portstr = NULL;
struct redir_tcp_udp *new;
        
if (((ipaddrstr = strchr(buff, ':')) == NULL) || (*(ipaddrstr+1) == 0)) {
    sim_printf ("redir %s syntax error\n", tcpudp[is_udp]);
    return -1;
    }
*ipaddrstr++ = 0;

if (((portstr = strchr (ipaddrstr, ':')) == NULL) || (*(portstr+1) == 0)) {
    sim_printf ("redir %s syntax error\n", tcpudp[is_udp]);
    return -1;
    }
*portstr++ = 0;

sscanf (buff, "%d", &lport);
sscanf (portstr, "%d", &port);
if (ipaddrstr) 
    inaddr = inet_addr (ipaddrstr);

if (!inaddr) {
    sim_printf ("%s redirection error: an IP address must be specified\n", tcpudp[is_udp]);
    return -1;
    }

if ((new = g_malloc (sizeof(struct redir_tcp_udp))) == NULL)
    return -1;
else {
    inet_aton (ipaddrstr, &new->inaddr);
    new->is_udp = is_udp;
    new->port = port;
    new->lport = lport;
    new->next = *head;
    *head = new;
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
    char *tftp_path;
    struct redir_tcp_udp *rtcp;
    GArray *gpollfds;
    SOCKET db_chime;            /* write packet doorbell */
    struct sockaddr_in db_addr; /* doorbell address */
    struct write_request {
        struct write_request *next;
        char msg[1518];
        size_t len;
        } *write_requests;
    struct write_request *write_buffers;
    pthread_mutex_t write_buffer_lock;
    void *opaque;               /* opaque value passed during packet delivery */
    packet_callback callback;   /* slirp arriving packet delivery callback */
    };

SLIRP *sim_slirp_open (const char *args, void *opaque, packet_callback callback)
{
SLIRP *slirp = (SLIRP *)g_malloc0(sizeof(*slirp));
char *targs = g_strdup (args);
char *tptr = targs;
char *cptr;
char tbuf[CBUFSIZE], gbuf[CBUFSIZE];
int err;

slirp->args = (char *)g_malloc0(1 + strlen(args));
strcpy (slirp->args, args);
slirp->opaque = opaque;
slirp->callback = callback;
slirp->maskbits = 24;
slirp->dhcpmgmt = 1;
inet_aton(DEFAULT_IP_ADDR,&slirp->vgateway);

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
            sim_printf ("Missing TFTP Path\n");
            err = 1;
            }
        continue;
        }
    if ((0 == MATCH_CMD (gbuf, "NAMESERVER")) ||
        (0 == MATCH_CMD (gbuf, "DNS"))) {
        if (cptr && *cptr)
            inet_aton (cptr, &slirp->vnameserver);
        else {
            sim_printf ("Missing nameserver\n");
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "GATEWAY")) {
        if (cptr && *cptr) {
            char *slash = strchr (cptr, '/');
            if (slash) {
                slirp->maskbits = atoi (slash+1);
                *slash = '\0';
                }
            inet_aton (cptr, &slirp->vgateway);
            }
        else {
            sim_printf ("Missing host\n");
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "NETWORK")) {
        if (cptr && *cptr) {
            char *slash = strchr (cptr, '/');
            if (slash) {
                slirp->maskbits = atoi (slash+1);
                *slash = '\0';
                }
            inet_aton (cptr, &slirp->vnetwork);
            }
        else {
            sim_printf ("Missing network\n");
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
            sim_printf ("Missing UDP port mapping\n");
            err = 1;
            }
        continue;
        }
    if (0 == MATCH_CMD (gbuf, "TCP")) {
        if (cptr && *cptr)
            err = _parse_redirect_port (&slirp->rtcp, cptr, IS_TCP);
        else {
            sim_printf ("Missing TCP port mapping\n");
            err = 1;
            }
        continue;
        }
    sim_printf ("Unexpected NAT argument: %s\n", gbuf);
    err = 1;
    }
if (err) {
    sim_slirp_close (slirp);
    g_free (targs);
    return NULL;
    }

slirp->vnetmask.s_addr = htonl(~((1 << (32-slirp->maskbits)) - 1));
slirp->vnetwork.s_addr = slirp->vgateway.s_addr & slirp->vnetmask.s_addr;
if ((slirp->vgateway.s_addr & ~slirp->vnetmask.s_addr) == 0)
    slirp->vgateway.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 2);
if ((slirp->vdhcp_start.s_addr == 0) && slirp->dhcpmgmt)
    slirp->vdhcp_start.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 15);
if (slirp->vnameserver.s_addr == 0)
    slirp->vnameserver.s_addr = htonl(ntohl(slirp->vnetwork.s_addr) | 3);
slirp->slirp = slirp_init (0, slirp->vnetwork, slirp->vnetmask, slirp->vgateway, 
                           NULL, slirp->tftp_path, NULL, slirp->vdhcp_start, 
                           slirp->vnameserver, NULL, (void *)slirp);

if (_do_redirects (slirp->slirp, slirp->rtcp)) {
    sim_slirp_close (slirp);
    slirp = NULL;
    }
else {
    char db_host[32];
    GPollFD pfd;
    int ret;
    int64_t rnd_val = qemu_clock_get_ns (0) / 1000000;

    pthread_mutex_init (&slirp->write_buffer_lock, NULL);
    slirp->gpollfds = g_array_new(FALSE, FALSE, sizeof(GPollFD));
    /* setup transmit packet wakeup doorbell */
    slirp->db_chime  = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ret = socket_set_fast_reuse(slirp->db_chime);
    memset (&slirp->db_addr, 0, sizeof (slirp->db_addr));
    slirp->db_addr.sin_family = AF_INET;
    sprintf (db_host, "127.%d.%d.%d", (int)((rnd_val>>16) & 0xFF), (int)((rnd_val>>8) & 0xFF), (int)(rnd_val & 0xFF));
    slirp->db_addr.sin_port = (rnd_val >> 24) & 0xFFFF;
    inet_aton (db_host, &slirp->db_addr.sin_addr);
    ret = bind(slirp->db_chime, (struct sockaddr *)&slirp->db_addr, sizeof(slirp->db_addr));
    qemu_set_nonblock(slirp->db_chime);
    memset (&pfd, 0, sizeof (pfd));
    pfd.fd = slirp->db_chime;
    pfd.events = G_IO_IN;
    g_array_append_val(slirp->gpollfds, pfd);
    
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
    while ((rtmp = slirp->rtcp)) {
        slirp_remove_hostfwd(slirp->slirp, rtmp->is_udp, rtmp->inaddr, rtmp->lport);
        slirp->rtcp = rtmp->next;
        g_free (rtmp);
        }
    g_array_free(slirp->gpollfds, true);
    closesocket (slirp->db_chime);
    if (1) {
        struct write_request *buffer;

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
"    TFTP=tftp-base-path                 Enables TFTP server and specifies\n"
"                                        base file path\n"
"    NAMESERVER=nameserver_ipaddres      specifies DHCP nameserver IP address\n"
"    DNS=nameserver_ipaddres             specifies DHCP nameserver IP address\n"
"    GATEWAY=host_ipaddress{/masklen}    specifies LAN gateway IP address\n"
"    NETWORK=network_ipaddress{/masklen} specifies LAN network address\n"
"    UDP=port:address:internal-port      maps host UDP port to guest port\n"
"    TCP=port:address:internal-port      maps host TCP port to guest port\n"
"    NODHCP                              disables DHCP server\n"
"Default NAT Options: GATEWAY=10.0.2.2, masklen=24(netmask is 255.255.255.0)\n"
"                     DHCP=10.0.2.15, NAMESERVER=10.0.2.3\n"
"    Nameserver defaults to proxy traffic to host system's active nameserver\n"
);
return SCPE_OK;
}

int sim_slirp_send (SLIRP *slirp, const char *msg, size_t len, int flags)
{
struct write_request *request;
int wake_needed = 0;

/* Get a buffer */
pthread_mutex_lock (&slirp->write_buffer_lock);
if (NULL != (request = slirp->write_buffers))
    slirp->write_buffers = request->next;
pthread_mutex_unlock (&slirp->write_buffer_lock);
if (NULL == request)
    request = (struct write_request *)g_malloc(sizeof(*request));

/* Copy buffer contents */
request->len = len;
memcpy(request->msg, msg, len);

/* Insert buffer at the end of the write list (to make sure that */
/* packets make it to the wire in the order they were presented here) */
pthread_mutex_lock (&slirp->write_buffer_lock);
request->next = NULL;
if (slirp->write_requests) {
    struct write_request *last_request = slirp->write_requests;

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
    sendto (slirp->db_chime, msg, 0, 0, (struct sockaddr *)&slirp->db_addr, sizeof(slirp->db_addr));
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
fprintf (st, "        gateway     =%s/%d\n", inet_ntoa(slirp->vgateway), slirp->maskbits);
fprintf (st, "        DNS         =%s\n", inet_ntoa(slirp->vnameserver));
if (slirp->vdhcp_start.s_addr != 0)
    fprintf (st, "        dhcp_start  =%s\n", inet_ntoa(slirp->vdhcp_start));
if (slirp->tftp_path)
    fprintf (st, "        tftp prefix =%s\n", slirp->tftp_path);
rtmp = slirp->rtcp;
while (rtmp) {
    fprintf (st, "        redir %3s   =%d:%s:%d\n", tcpudp[rtmp->is_udp], rtmp->lport, inet_ntoa(rtmp->inaddr), rtmp->port);
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
    if (events & G_IO_PRI) {
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
uint32_t slirp_timeout = ms_timeout;
struct timeval timeout;
fd_set rfds, wfds, xfds;
fd_set save_rfds, save_wfds, save_xfds;
int nfds;

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
        recv (slirp->db_chime, buf, sizeof (buf), 0);
        }
    fprintf (stderr, "Select returned %d\r\n", select_ret);
    for (i=0; i<nfds+1; i++) {
        if (FD_ISSET(i, &rfds) || FD_ISSET(i, &save_rfds))
            fprintf (stderr, "%d: save_rfd=%d, rfd=%d\r\n", i, FD_ISSET(i, &save_rfds), FD_ISSET(i, &rfds));
        if (FD_ISSET(i, &wfds) || FD_ISSET(i, &save_wfds))
            fprintf (stderr, "%d: save_wfd=%d, wfd=%d\r\n", i, FD_ISSET(i, &save_wfds), FD_ISSET(i, &wfds));
        if (FD_ISSET(i, &xfds) || FD_ISSET(i, &save_xfds))
            fprintf (stderr, "%d: save_xfd=%d, xfd=%d\r\n", i, FD_ISSET(i, &save_xfds), FD_ISSET(i, &xfds));
            }
    }
return select_ret + 1;  /* Force dispatch even on timeout */
}

void sim_slirp_dispatch (SLIRP *slirp)
{
struct write_request *request;

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

