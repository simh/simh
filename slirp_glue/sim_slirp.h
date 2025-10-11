#ifndef SIM_SLIRP_H
#define SIM_SLIRP_H

#if defined(HAVE_SLIRP_NETWORK) || defined(HAVE_VMNET_NETWORK)

#include "sim_defs.h"

struct redir_tcp_udp {
    struct in_addr inaddr;
    int is_udp;
    int port;
    int lport;
    struct redir_tcp_udp *next;
    };

struct sim_net_attributes {
    char *args;
    int nat_type;
    struct in_addr vnetwork;
    struct in_addr vnetmask;
    int maskbits;
    struct in_addr vgateway;
    int dhcpmgmt;
    struct in_addr vdhcp_start;
    struct in_addr vdhcp_end;
    struct in_addr vnameserver;
    char *boot_file;
    char *tftp_path;
    char *dns_search;
    char **dns_search_domains;
    struct redir_tcp_udp *rtcp;
    };


typedef struct sim_net_attributes NAT;
typedef struct sim_slirp SLIRP;

typedef void (*packet_callback)(void *opaque, const unsigned char *buf, int len);

SLIRP *sim_slirp_open (const char *args, void *opaque, packet_callback callback, DEVICE *dptr, uint32 dbit, char *errbuf, size_t errbuf_size);
void sim_slirp_close (SLIRP *slirp);
int sim_slirp_send (SLIRP *slirp, const char *msg, size_t len, int flags);
int sim_slirp_select (SLIRP *slirp, int ms_timeout);
void sim_slirp_dispatch (SLIRP *slirp);
void sim_slirp_show (SLIRP *slirp, FILE *st);
int sim_nat_parse_args (NAT *nat, const char *args, int nat_type, char *errbuf, size_t errbuf_size);
const char *sim_nat_attach_scp_help(DEVICE *dptr);
void sim_nat_show (NAT *nat, FILE *st);

#endif /* HAVE_SLIRP_NETWORK */

#endif
