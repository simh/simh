/* kx10_imp.c: IMP, interface message processor.

   Copyright (c) 2018-2020, Richard Cornwell based on code provided by
         Lars Brinkhoff and Danny Gasparovski.

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This emulates the MIT-AI/ML/MC Host/IMP interface.
*/


#include "kx10_defs.h"
#include "sim_ether.h"

#if NUM_DEVS_IMP > 0
#define IMP_DEVNUM  0460
#define WA_IMP_DEVNUM  0400

#define DEVNUM imp_dib.dev_num

#define UNIT_V_DHCP     (UNIT_V_UF + 0)                 /* DHCP enable flag */
#define UNIT_DHCP       (1 << UNIT_V_DHCP)
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* Type of IMP interface */
#define UNIT_M_DTYPE    3
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

#define TYPE_MIT        0              /* MIT Style KAIMP ITS */
#define TYPE_BBN        1              /* BBN style interface TENEX */
#define TYPE_WAITS      2              /* IMP connected to waits system. */

/* ITS IMP Bits */

/* CONI */
#define IMPID       010 /* Input done. */
#define IMPI32      020 /* Input in 32 bit mode. */
#define IMPIB       040 /* Input busy. */
#define IMPOD      0100 /* Output done. */
#define IMPO32     0200 /* Output in 32-bit mode. */
#define IMPOB      0400 /* Output busy. */
#define IMPERR    01000 /* IMP error. */
#define IMPR      02000 /* IMP ready. */
#define IMPIC     04000 /* IMP interrupt condition. */
#define IMPHER   010000 /* Host error. */
#define IMPHR    020000 /* Host ready. */
#define IMPIHE   040000 /* Inhibit interrupt on host error. */
#define IMPLW   0100000 /* Last IMP word. */

/* CONO */
#define IMPIDC      010 /* Clear input done */
#define IMI32S      020 /* Set 32-bit output */
#define IMI32C      040 /* Clear 32-bit output */
#define IMPODC     0100 /* Clear output done */
#define IMO32S     0200 /* Set 32-bit input */
#define IMO32C     0400 /* Clear 32-bit input */
#define IMPODS    01000 /* Set output done */
#define IMPIR     04000 /* Enable interrupt on IMP ready */
#define IMPHEC   010000 /* Clear host error */
#define IMIIHE   040000 /* Inhibit interrupt on host error */
#define IMPLHW  0200000 /* Set last host word. */

/* BBN IMP BITS */

/* CONO bits */
#define IMP_EN_IN   00000010  /* Enable input PIA channel */
#define IMP_EN_OUT  00000200  /* Enable output PIA channel */
#define IMP_EN_END  00004000  /* Enable end PIA channel */
#define IMP_END_IN  00010000  /* End of input */
#define IMP_END_OUT 00020000  /* End of output */
#define IMP_STOP    00040000  /* Stop the imp */
#define IMP_PDP_DN  00100000  /* PDP-10 is down */
#define IMP_CLR     00200000  /* Clear imp down flag */
#define IMP_RST     00400000  /* Reset IMP */

/* CONI bits */
#define IMP_IFULL   00000010  /* Input full */
#define IMP_OEMPY   00000200  /* Output empty */
#define IMP_ENDIN   00014000  /* End of input */
#define IMP_DN      00020000  /* IMP down */
#define IMP_WAS_DN  00040000  /* IMP was down */
#define IMP_PWR     00200000  /* IMP Rdy */

/* WAITS IMP BITS */

/* CONO bits  */
#define IMP_ODPIEN   0000010  /* Enable change of output done PIA, also set byte size */
#define IMP_IDPIEN   0000020  /* Enable change of input done PIA, also set byte size */
#define IMP_IEPIEN   0000040  /* Change end of input PIA */
#define IMP_FINO     0000100  /* Last bit of output */
#define IMP_STROUT   0000200  /* Start output */
#define IMP_CLRWT    0002000  /* Clear waiting to input bit */
#define IMP_CLRST    0004000  /* Clear stop after input bit */
#define IMP_O32      0010000  /* Set output to 32bit */
#define IMP_I32      0020000  /* Set input to 32bit */
#define IMP_STRIN    0040000  /* Start input */
#define IMP_TEST     0100000  /* Test mode */

/* CONI bits */
#define IMP_ODONE    0004000  /* Output done */
#define IMP_IEND     0010000  /* Input end. */
#define IMP_IDONE    0020000  /* Input done */
#define IMP_ERR      0040000  /* Imp error */
#define IMP_RDY      0200000  /* Imp ready */
#define IMP_OCHN     0000007
#define IMP_ICHN     0000070
#define IMP_ECHN     0000700

/* CONI timeout.  If no CONI instruction is executed for 3-5 seconds,
   the interface will raise the host error signal. */
#define CONI_TIMEOUT 3000000

#define STATUS     u3
#define OPOS       u4    /* Output bit position */
#define IPOS       u5    /* Input bit position */
#define ILEN       u6    /* Size of input buffer in bits */


#define IMP_ARPTAB_SIZE        64
#define IMP_ARP_MAX_AGE        100

uint32 mask[] = {
     0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFC, 0xFFFFFFF8,
     0xFFFFFFF0, 0xFFFFFFE0, 0xFFFFFFC0, 0xFFFFFF80,
     0xFFFFFF00, 0xFFFFFE00, 0xFFFFFC00, 0xFFFFF800,
     0xFFFFF000, 0xFFFFE000, 0xFFFFC000, 0xFFFF8000,
     0xFFFF0000, 0xFFFE0000, 0xFFFC0000, 0xFFF80000,
     0xFFF00000, 0xFFE00000, 0xFFC00000, 0xFF800000,
     0xFF000000, 0xFE000000, 0xFC000000, 0xF8000000,
     0xF0000000, 0xE0000000, 0xC0000000, 0x80000000,
     0x00000000};

typedef uint32 in_addr_T;

PACKED_BEGIN
struct imp_eth_hdr {
    ETH_MAC    dest;
    ETH_MAC    src;
    uint16     type;
} PACKED_END;

#define ETHTYPE_ARP 0x0806
#define ETHTYPE_IP  0x0800

/*
 * Structure of an internet header, naked of options.
 */
PACKED_BEGIN
struct ip {
    uint8           ip_v_hl;            /* version,header length */
    uint8           ip_tos;             /* type of service */
    uint16          ip_len;             /* total length */
    uint16          ip_id;              /* identification */
    uint16          ip_off;             /* fragment offset field */
#define IP_DF 0x4000                    /* don't fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
    uint8           ip_ttl;             /* time to live */
    uint8           ip_p;               /* protocol */
    uint16          ip_sum;             /* checksum */
    in_addr_T       ip_src;
    in_addr_T       ip_dst;             /* source and dest address */
} PACKED_END;

#define TCP_PROTO  6
PACKED_BEGIN
struct tcp {
    uint16          tcp_sport;          /* Source port */
    uint16          tcp_dport;          /* Destination port */
    uint32          seq;                /* Sequence number */
    uint32          ack;                /* Ack number */
    uint16          flags;              /* Flags */
#define TCP_FL_FIN  0x01
#define TCP_FL_SYN  0x02
#define TCP_FL_RST  0x04
#define TCP_FL_PSH  0x08
#define TCP_FL_ACK  0x10
#define TCP_FL_URG  0x20
    uint16          window;             /* Window size */
    uint16          chksum;             /* packet checksum */
    uint16          urgent;             /* Urgent pointer */
} PACKED_END;

#define UDP_PROTO 17
PACKED_BEGIN
struct udp {
    uint16          udp_sport;          /* Source port */
    uint16          udp_dport;          /* Destination port */
    uint16          len;                /* Length */
    uint16          chksum;             /* packet checksum */
} PACKED_END;

PACKED_BEGIN
struct udp_hdr {
    in_addr_T       ip_src;
    in_addr_T       ip_dst;             /* source and dest address */
    uint8           zero;
    uint8           proto;              /* Protocol */
    uint16          hlen;               /* Length of header and data */
} PACKED_END;

#define ICMP_PROTO 1
PACKED_BEGIN
struct icmp {
    uint8           type;               /* Type of packet */
    uint8           code;               /* Code */
    uint16          chksum;             /* packet checksum */
} PACKED_END;

PACKED_BEGIN
struct ip_hdr {
    struct imp_eth_hdr  ethhdr;
    struct ip           iphdr;
} PACKED_END;

#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_HWTYPE_ETH  1

PACKED_BEGIN
struct arp_hdr {
    struct imp_eth_hdr  ethhdr;
    uint16              hwtype;
    uint16              protocol;
    uint8               hwlen;
    uint8               protolen;
    uint16              opcode;
    ETH_MAC             shwaddr;
    in_addr_T           sipaddr;
    ETH_MAC             dhwaddr;
    in_addr_T           dipaddr;
    uint8               padding[18];
} PACKED_END;

struct arp_entry {
    in_addr_T  ipaddr;
    ETH_MAC    ethaddr;
    int16      age;
#define ARP_DONT_AGE -1
};

/* DHCP client states */
#define DHCP_STATE_OFF              0
#define DHCP_STATE_REQUESTING       1
#define DHCP_STATE_INIT             2
#define DHCP_STATE_REBOOTING        3
#define DHCP_STATE_REBINDING        4
#define DHCP_STATE_RENEWING         5
#define DHCP_STATE_SELECTING        6
#define DHCP_STATE_INFORMING        7
#define DHCP_STATE_CHECKING         8
#define DHCP_STATE_PERMANENT        9   /* not yet implemented */
#define DHCP_STATE_BOUND            10
#define DHCP_STATE_RELEASING        11  /* not yet implemented */
#define DHCP_STATE_BACKING_OFF      12

/* DHCP op codes */
#define DHCP_BOOTREQUEST            1
#define DHCP_BOOTREPLY              2

/* DHCP message types */
#define DHCP_DISCOVER               1
#define DHCP_OFFER                  2
#define DHCP_REQUEST                3
#define DHCP_DECLINE                4
#define DHCP_ACK                    5
#define DHCP_NAK                    6
#define DHCP_RELEASE                7
#define DHCP_INFORM                 8

/** DHCP hardware type, currently only ethernet is supported */
#define DHCP_HTYPE_ETH              1

#define DHCP_MAGIC_COOKIE           0x63825363UL

#define DHCP_INFINITE_LEASE         0xFFFFFFFF

#define DHCP_UDP_PORT_CLIENT        68
#define DHCP_UDP_PORT_SERVER        67

/* This is a list of options for BOOTP and DHCP, see RFC 2132 for descriptions */

/* BootP options */
#define DHCP_OPTION_PAD             0
#define DHCP_OPTION_SUBNET_MASK     1 /* RFC 2132 3.3 */
#define DHCP_OPTION_ROUTER          3
#define DHCP_OPTION_DNS_SERVER      6
#define DHCP_OPTION_HOSTNAME        12
#define DHCP_OPTION_IP_TTL          23
#define DHCP_OPTION_MTU             26
#define DHCP_OPTION_BROADCAST       28
#define DHCP_OPTION_TCP_TTL         37
#define DHCP_OPTION_NTP             42
#define DHCP_OPTION_END             255

/* DHCP options */
#define DHCP_OPTION_REQUESTED_IP    50 /* RFC 2132 9.1, requested IP address */
#define DHCP_OPTION_LEASE_TIME      51 /* RFC 2132 9.2, time in seconds, in 4 bytes */
#define DHCP_OPTION_OVERLOAD        52 /* RFC 2132 9.3, use file and/or sname field for options */

#define DHCP_OPTION_MESSAGE_TYPE    53 /* RFC 2132 9.6, important for DHCP */
#define DHCP_OPTION_MESSAGE_TYPE_LEN 1

#define DHCP_OPTION_SERVER_ID       54 /* RFC 2132 9.7, server IP address */
#define DHCP_OPTION_PARAMETER_REQUEST_LIST  55 /* RFC 2132 9.8, requested option types */

#define DHCP_OPTION_MAX_MSG_SIZE    57 /* RFC 2132 9.10, message size accepted >= 576 */
#define DHCP_OPTION_MAX_MSG_SIZE_LEN 2

#define DHCP_OPTION_T1              58 /* T1 renewal time */
#define DHCP_OPTION_T2              59 /* T2 rebinding time */
#define DHCP_OPTION_US              60
#define DHCP_OPTION_CLIENT_ID       61
#define DHCP_OPTION_TFTP_SERVERNAME 66
#define DHCP_OPTION_BOOTFILE        67

/* possible combinations of overloading the file and sname fields with options */
#define DHCP_OVERLOAD_NONE          0
#define DHCP_OVERLOAD_FILE          1
#define DHCP_OVERLOAD_SNAME         2
#define DHCP_OVERLOAD_SNAME_FILE    3

#define DHCP_CHADDR_LEN             16
#define DHCP_SNAME_LEN              64
#define DHCP_FILE_LEN               128

PACKED_BEGIN
struct dhcp {
    uint8             op;                      /* Operation */
    uint8             htype;                   /* Header type */
    uint8             hlen;                    /* Ether Header len */
    uint8             hops;                    /*  ops? */
    uint32            xid;                     /* id number */
    uint16            secs;
    uint16            flags;
    in_addr_T         ciaddr;                  /* Client IP address */
    in_addr_T         yiaddr;                  /* Your IP address */
    in_addr_T         siaddr;                  /* Server IP address */
    in_addr_T         giaddr;                  /* Gateway IP address */
    uint8             chaddr[DHCP_CHADDR_LEN];
    uint8             sname[DHCP_SNAME_LEN];
    uint8             file[DHCP_FILE_LEN];
    uint32            cookie;                  /* magic cookie */
    uint8             options[100];            /* Space for options */
} PACKED_END;

struct imp_packet {
    struct imp_packet *next;                   /* Link to packets */
    ETH_PACK          packet;
    in_addr_T         dest;                    /* Destination IP address */
    uint16            msg_id;                  /* Message ID */
    int               life;                    /* How many ticks to wait */
} imp_buffer[8];

struct imp_map {
    uint16            sport;                   /* Port to fix */
    uint16            dport;                   /* Port to fix */
    uint16            cls_tim;                 /* Close timer */
    uint32            adj;                     /* Amount to adjust */
    uint32            lseq;                    /* Sequence number last adjusted */
};


struct imp_stats {
    int               recv;                    /* received packets */
    int               dropped;                 /* received packets dropped */
    int               xmit;                    /* transmitted packets */
    int               fail;                    /* transmit failed */
    int               runt;                    /* runts */
    int               reset;                   /* reset count */
    int               giant;                   /* oversize packets */
    int               setup;                   /* setup packets */
    int               loop;                    /* loopback packets */
    int               recv_overrun;            /* receiver overruns */
};


struct imp_device {
    ETH_PCALLBACK     rcallback;               /* read callback routine */
    ETH_PCALLBACK     wcallback;               /* write callback routine */
    ETH_MAC           macs[2];                 /* Hardware MAC addresses */
#define mac macs[0]
#define bcast macs[1]
    struct imp_packet *sendq;                  /* Send queue */
    struct imp_packet *freeq;                  /* Free queue */
    in_addr_T         ip;                      /* Local IP address */
    in_addr_T         ip_mask;                 /* Local IP mask */
    in_addr_T         hostip;                  /* IP address of local host */
    in_addr_T         gwip;                    /* Gateway IP address */
    int               maskbits;                /* Mask length */
    struct imp_map    port_map[64];            /* Ports to adjust */
    in_addr_T         dhcpip;                  /* DHCP server address */
    uint8             dhcp_state;              /* State of DHCP */
    uint32            dhcp_lease;              /* DHCP lease time */
    int               dhcp_renew;              /* DHCP renew time */
    int               dhcp_rebind;             /* DHCP rebind time */
    int               dhcp_wait_time;          /* seconds waiting for response */
    int               dhcp_retry_after;        /* timeout time */
    int               init_state;              /* Initialization state */
    uint32            dhcp_xid;                /* Transaction ID */
    int               padding;                 /* Type zero padding */
    uint64            obuf;                    /* Output buffer */
    uint64            ibuf;                    /* Input buffer */
    int               obits;                   /* Output bits */
    int               ibits;                   /* Input bits */
    struct imp_stats  stats;
    uint8             sbuffer[ETH_FRAME_SIZE]; /* Temp send buffer */
    uint8             rbuffer[ETH_FRAME_SIZE]; /* Temp receive buffer */
    ETH_DEV           etherface;
    ETH_QUE           ReadQ;
    int               imp_error;
    int               host_error;
    int               rfnm_count;              /* Number of pending RFNM packets */
    int               pia;                     /* PIA channels */
    struct arp_entry  arp_table[IMP_ARPTAB_SIZE];
} imp_data;

extern int32 tmxr_poll;

static CONST ETH_MAC broadcast_ethaddr = {0xff,0xff,0xff,0xff,0xff,0xff};

static CONST in_addr_T broadcast_ipaddr = {0xffffffff};

t_stat         imp_devio(uint32 dev, uint64 *data);
t_addr         imp_devirq(uint32 dev, t_addr addr);
t_stat         imp_srv(UNIT *);
t_stat         imp_eth_srv(UNIT *);
t_stat         imp_tim_srv(UNIT *);
t_stat         imp_reset (DEVICE *dptr);
t_stat         imp_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         imp_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         imp_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_ip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_ip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_gwip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_gwip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_hostip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_hostip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_dhcpip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_show_arp (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_arp (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void           imp_timer_task(struct imp_device *imp);
void           imp_send_rfmn(struct imp_device *imp);
void           imp_packet_in(struct imp_device *imp);
void           imp_send_packet (struct imp_device *imp_data, int len);
void           imp_free_packet(struct imp_device *imp, struct imp_packet *p);
struct imp_packet * imp_get_packet(struct imp_device *imp);
void           imp_arp_update(struct imp_device *imp, in_addr_T ipaddr, ETH_MAC *ethaddr, int age);
void           imp_arp_arpin(struct imp_device *imp, ETH_PACK *packet);
void           imp_arp_arpout(struct imp_device *imp, in_addr_T ipaddr);
struct arp_entry * imp_arp_lookup(struct imp_device *imp, in_addr_T ipaddr);
void           imp_packet_out(struct imp_device *imp, ETH_PACK *packet);
void           imp_packet_debug(struct imp_device *imp, const char *action, ETH_PACK *packet);
void           imp_write(struct imp_device *imp, ETH_PACK *packet);
void           imp_do_dhcp_client(struct imp_device *imp, ETH_PACK *packet);
void           imp_dhcp_timer(struct imp_device *imp);
void           imp_dhcp_discover(struct imp_device *imp);
void           imp_dhcp_request(struct imp_device *imp, in_addr_T dhcpip);
void           imp_dhcp_release(struct imp_device *imp);
void           imp_arp_age(struct imp_device *imp);
t_stat         imp_attach (UNIT * uptr, CONST char * cptr);
t_stat         imp_detach (UNIT * uptr);
t_stat         imp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char     *imp_description (DEVICE *dptr);
static char    *ipv4_inet_ntoa(struct in_addr ip);
static int     ipv4_inet_aton(const char *str, struct in_addr *inp);


int       imp_mpx_lvl = 0;
double    last_coni;

UNIT imp_unit[] = {
    {UDATA(imp_srv,     UNIT_IDLE+UNIT_ATTABLE+UNIT_DHCP, 0)},  /* 0 */
    {UDATA(imp_eth_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 0 */
    {UDATA(imp_tim_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 0 */
};
DIB imp_dib = {IMP_DEVNUM, 1, &imp_devio, 
#if KL
        &imp_devirq,
#else
        NULL
#endif
};

MTAB imp_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &imp_set_mac, &imp_show_mac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MPX", "MPX",
      &imp_set_mpx, &imp_show_mpx, NULL, "ITS Interrupt Channel #"},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "IP", "IP=ddd.ddd.ddd.ddd/dd",
      &imp_set_ip, &imp_show_ip, NULL, "IP address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "GW", "GW=ddd.ddd.ddd.ddd",
      &imp_set_gwip, &imp_show_gwip, NULL, "Gateway address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "HOST", "HOST=ddd.ddd.ddd.ddd",
      &imp_set_hostip, &imp_show_hostip, NULL, "HOST IP address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ETH", NULL, NULL,
      &eth_show, NULL, "Display attachedable devices" },
    { UNIT_DHCP, 0, NULL, "NODHCP", NULL, NULL, NULL,
           "Don't aquire address from DHCP"},
    { UNIT_DHCP, UNIT_DHCP, "DHCP", "DHCP", NULL, NULL, NULL,
           "Use DHCP to set IP address"},
    { MTAB_XTD|MTAB_VDV, 0, "DHCPIP", NULL,
      NULL, &imp_show_dhcpip, NULL, "DHCP info" },
    { UNIT_DTYPE, (TYPE_MIT << UNIT_V_DTYPE), "MIT", "MIT", NULL, NULL,  NULL,
           "ITS/MIT style interface"},
    { UNIT_DTYPE, (TYPE_BBN << UNIT_V_DTYPE), "BBN", "BBN", NULL, NULL,  NULL,
           "Tenex/BBN style interface"},
    { UNIT_DTYPE, (TYPE_WAITS << UNIT_V_DTYPE), "WAITS", "WAITS", NULL, NULL,  NULL,
           "WAITS style interface"},
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ARP", NULL,
      NULL, &imp_show_arp, NULL, "ARP IP address->MAC address table" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "ARP=ddd.ddd.ddd.ddd=XX:XX:XX:XX:XX:XX",
      &imp_set_arp, NULL, NULL, "Create a static ARP Entry" },
    { 0 }
    };

/* Simulator debug controls */
DEBTAB              imp_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show cono instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {"IRQ", DEBUG_IRQ, "Show IRQ requests"},
#define DEBUG_DHCP (DEBUG_IRQ<<1)
    {"DHCP", DEBUG_DHCP, "Show DHCP activities"},
#define DEBUG_ARP (DEBUG_DHCP<<1)
    {"ARP", DEBUG_ARP, "Show ARP activities"},
#define DEBUG_TCP (DEBUG_ARP<<1)
    {"TCP", DEBUG_TCP, "Show TCP packet activities"},
#define DEBUG_UDP (DEBUG_TCP<<1)
    {"UDP", DEBUG_UDP, "Show UDP packet activities"},
#define DEBUG_ICMP (DEBUG_UDP<<1)
    {"ICMP", DEBUG_ICMP, "Show ICMP packet activities"},
#define DEBUG_ETHER (DEBUG_ICMP<<1)
    {"ETHER", DEBUG_ETHER, "Show ETHER activities"},
    {0, 0}
};

REG                 imp_reg[] = {
    {SAVEDATA(DATA, imp_data) },
    {0}
};

DEVICE imp_dev = {
    "IMP", imp_unit, imp_reg, imp_mod,
    3, 8, 0, 1, 8, 36,
    NULL, NULL, &imp_reset, NULL, &imp_attach, &imp_detach,
    &imp_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_ETHER, 0, imp_debug,
    NULL, NULL, &imp_help, NULL, NULL, &imp_description
};
#define IMP_OCHN     0000007
#define IMP_ICHN     0000070
#define IMP_ECHN     0000700

static void check_interrupts (UNIT *uptr)
{
    clr_interrupt (DEVNUM);

    if ((uptr->STATUS & (IMPERR | IMPIC)) == IMPERR)
        set_interrupt(DEVNUM, imp_data.pia >> 6);
    if ((uptr->STATUS & (IMPR | IMPIC)) == (IMPR | IMPIC))
        set_interrupt(DEVNUM, imp_data.pia >> 6);
    if ((uptr->STATUS & (IMPHER | IMPIHE)) == IMPHER)
        set_interrupt(DEVNUM, imp_data.pia >> 6);
    if (uptr->STATUS & IMPID) {
        if (uptr->STATUS & IMPLW)
            set_interrupt(DEVNUM, imp_data.pia);
        else
            set_interrupt_mpx(DEVNUM, imp_data.pia, imp_mpx_lvl);
    }
    if (uptr->STATUS & IMPOD)
       set_interrupt_mpx(DEVNUM, imp_data.pia >> 3, imp_mpx_lvl + 1);
}

t_stat imp_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &imp_dev;
    UNIT   *uptr = imp_unit;

    switch(dev & 07) {
    case CONO:
        sim_debug(DEBUG_CONO, dptr, "IMP %03o CONO %06o PC=%o\n", dev,
                 (uint32)*data, PC);
        switch (GET_DTYPE(uptr->flags)) {
        case TYPE_MIT:
             imp_data.pia = *data & 7;
             imp_data.pia = (imp_data.pia << 6) | (imp_data.pia << 3) | imp_data.pia;
             if (*data & IMPIDC) /* Clear input done. */
                 uptr->STATUS &= ~IMPID;
             if (*data & IMI32S) /* Set 32-bit input. */
                 uptr->STATUS |= IMPI32;
             if (*data & IMI32C) /* Clear 32-bit input */
                 uptr->STATUS &= ~IMPI32;
             if (*data & IMPODC) /* Clear output done. */
                 uptr->STATUS &= ~IMPOD;
             if (*data & IMO32C) /* Clear 32-bit output. */
                 uptr->STATUS &= ~IMPO32;
             if (*data & IMO32S) /* Set 32-bit output. */
                 uptr->STATUS |= IMPO32;
             if (*data & IMPODS) /* Set output done. */
                 uptr->STATUS |= IMPOD;
             if (*data & IMPIR) { /* Enable interrupt on IMP ready. */
                 uptr->STATUS |= IMPIC;
                 uptr->STATUS &= ~IMPERR;
             }
             if (*data & IMPHEC) { /* Clear host error. */
                 /* Only if there has been a CONI lately. */
                 if (last_coni - sim_gtime() < CONI_TIMEOUT)
                     uptr->STATUS &= ~IMPHER;
             }
             if (*data & IMIIHE) /* Inhibit interrupt on host error. */
                 uptr->STATUS |= IMPIHE;
             if (*data & IMPLHW) /* Last host word. */
                 uptr->STATUS |= IMPLHW;
             check_interrupts(uptr);
             break;
        case TYPE_BBN:
             break;
        case TYPE_WAITS:
             if (*data & IMP_ODPIEN) {
                 imp_data.pia &= ~07;
                 imp_data.pia |= *data & 07;
                 uptr->STATUS &= ~(IMPO32|IMPLHW|IMPOD);
                 if (*data & IMP_O32)
                     uptr->STATUS |= IMPO32;
             }
             if (*data & IMP_IDPIEN) {
                 imp_data.pia &= ~070;
                 imp_data.pia |= (*data & 07) << 3;
                 uptr->STATUS &= ~(IMPI32|IMPID);
                 if (*data & IMP_I32)
                     uptr->STATUS |= IMPI32;
             }
             if (*data & IMP_IEPIEN) {
                 imp_data.pia &= ~0700;
                 imp_data.pia |= (*data & 07) << 6;
             }
             if (*data & IMP_FINO) {
                 if (uptr->STATUS & IMPOD) {
                     imp_send_packet (&imp_data, uptr->OPOS >> 3);
                     /* Allow room for ethernet header for later */
                     memset(imp_data.sbuffer, 0, ETH_FRAME_SIZE);
                     uptr->OPOS = 0;
                     uptr->STATUS &= ~(IMPLHW);
                 } else 
                     uptr->STATUS |= IMPLHW;
             }
             if (*data & IMP_STROUT)
                 uptr->STATUS &= ~(IMPOD|IMPLHW);
             if (*data & IMP_CLRWT) { /* Not sure about this yet. */
                 uptr->STATUS &= ~IMPID;
             }
             if (*data & IMP_CLRST)   /* Not sure about this yet. */
                 uptr->STATUS &= ~IMPID;
             if (*data & IMP_STRIN) {
                 uptr->STATUS &= ~IMPID;
                 uptr->ILEN = 0;
             }
             check_interrupts(uptr);
             break;
        }
        break;
    case CONI:
        switch (GET_DTYPE(uptr->flags)) {
        case TYPE_MIT:
             last_coni = sim_gtime();
             *data = (uint64)(uptr->STATUS | (imp_data.pia & 07));
             break;
        case TYPE_BBN:
             break;
        case TYPE_WAITS:
             *data = (uint64)(imp_data.pia & 0777);
             if (uptr->STATUS & IMPOD)
                 *data |= IMP_ODONE;
             if (uptr->STATUS & IMPID)
                 *data |= IMP_IDONE;
             if (uptr->STATUS & IMPR)
                 *data |= IMP_RDY;
             if (uptr->STATUS & IMPLW)
                 *data |= IMP_IEND;
             if (uptr->STATUS & (IMPERR|IMPHER))
                 *data |= IMP_ERR;
             break;
        }
        sim_debug(DEBUG_CONI, dptr, "IMP %03o CONI %012llo PC=%o\n", dev,
                           *data, PC);
        break;
    case DATAO:
        uptr->STATUS |= IMPOB;
        uptr->STATUS &= ~IMPOD;
        imp_data.obuf = *data;
        imp_data.obits = (uptr->STATUS & IMPO32) ? 32 : 36;
        sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATO %012llo %d %08x PC=%o\n",
                 dev, *data, imp_data.obits, (uint32)(*data >> 4), PC);
        sim_activate(uptr, 100);
        break;
    case DATAI:
        *data = imp_data.ibuf;
        uptr->STATUS &= ~(IMPID|IMPLW);
        sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATI %012llo %08x PC=%o\n",
                 dev, *data, (uint32)(*data >> 4), PC);
        if (uptr->ILEN != 0)
            uptr->STATUS |= IMPIB;
        sim_activate(uptr, 100);
        break;
    }

    check_interrupts (uptr);
    return SCPE_OK;
}

#if KL
/* Handle KL style interrupt vectors for ITS */
t_addr
imp_devirq(uint32 dev, t_addr addr) {
    if ((cpu_unit[0].flags & UNIT_ITSPAGE) != 0 && (imp_data.pia & 7) == 1) {
        if (imp_unit[0].STATUS & IMPID && (imp_unit[0].STATUS & IMPLW) == 0)
            return 070|RSIGN;
        if (imp_unit[0].STATUS & IMPOD)
            return 072|RSIGN;
    }
    return  addr;
}
#endif

t_stat imp_srv(UNIT * uptr)
{
    int     i;
    int     l;

    if (uptr->STATUS & IMPOB && imp_data.sendq == NULL) {
        if (imp_data.obits == 32)
           imp_data.obuf >>= 4;
        for (i = imp_data.obits - 1; i >= 0; i--) {
            imp_data.sbuffer[uptr->OPOS>>3] |=
                  ((imp_data.obuf >> i) & 1) << (7-(uptr->OPOS & 7));
            uptr->OPOS++;
        }
        if (uptr->STATUS & IMPLHW) {
            imp_send_packet (&imp_data, uptr->OPOS >> 3);
            /* Allow room for ethernet header for later */
            memset(imp_data.sbuffer, 0, ETH_FRAME_SIZE);
            uptr->OPOS = 0;
            uptr->STATUS &= ~IMPLHW;
        }
        uptr->STATUS &= ~IMPOB;
        uptr->STATUS |= IMPOD;
        check_interrupts (uptr);
    }
    if (uptr->STATUS & IMPIB) {
        uptr->STATUS &= ~(IMPIB|IMPLW);
        imp_data.ibuf = 0;
        l = (uptr->STATUS & IMPI32) ? 4 : 0;
        for (i = 35; i >= l; i--) {
             if ((imp_data.rbuffer[uptr->IPOS>>3] >> (7-(uptr->IPOS & 7))) & 1)
                 imp_data.ibuf |= ((uint64)1) << i;
             uptr->IPOS++;
             if (uptr->IPOS > uptr->ILEN) {
                uptr->STATUS |= IMPLW;
                uptr->ILEN = 0;
                break;
             }
        }
        uptr->STATUS |= IMPID;
        check_interrupts (uptr);
    }
    if (uptr->ILEN == 0 && (uptr->STATUS & (IMPIB|IMPID)) == 0)
        imp_packet_in(&imp_data);
    return SCPE_OK;
}

void
ip_checksum(uint8 *chksum, uint8 *ptr, int len)
{
   /*
    * Compute Internet Checksum for "count" bytes
    *         beginning at location "addr".
    */
   int32  sum = 0;

   while( len > 1 )  {
      /*  This is the inner loop */
      sum += (ptr[0]<<8)+ptr[1];
      ptr+=2;
      len -= 2;
   }

   /*  Add left-over byte, if any */
   if( len > 0 )
      sum += ptr[0]<<8;

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
       sum = (sum & 0xffff) + (sum >> 16);
    sum=(~sum & 0xffff);
    chksum[0]=(sum>>8) & 0xff;
    chksum[1]=sum & 0xff;
}


/*
 * Update the checksum based on code from RFC1631
 */
void
checksumadjust(uint8 *chksum, uint8 *optr,
   int olen, uint8 *nptr, int nlen)
   /* assuming: unsigned char is 8 bits, long is 32 bits.
     - chksum points to the chksum in the packet
     - optr points to the old data in the packet
     - nptr points to the new data in the packet
     - even number of octets updated.
   */
{
    int32 sum, old, new_sum;
    sum=(chksum[0]<<8)+chksum[1];
    sum=(~sum & 0xffff);
    while (olen > 1) {
        old=(optr[0]<<8)+optr[1];
        optr+=2;
        sum-=old & 0xffff;
        if (sum<=0) { sum--; sum&=0xffff; }
        olen-=2;
    }
    if (olen > 0) {
        old=optr[0]<<8;
        sum-=old & 0xffff;
        if (sum<=0) { sum--; sum&=0xffff; }
    }
    while (nlen > 1) {
        new_sum=(nptr[0]<<8)+nptr[1];
        nptr+=2;
        sum+=new_sum & 0xffff;
        if (sum & 0x10000) { sum++; sum&=0xffff; }
        nlen-=2;
    }
    if (nlen > 0) {
        new_sum=(nptr[0]<<8);
        sum+=new_sum & 0xffff;
        if (sum & 0x10000) { sum++; sum&=0xffff; }
    }
    sum=(~sum & 0xffff);
    chksum[0]=sum>>8;
    chksum[1]=sum & 0xff;
}

t_stat imp_eth_srv(UNIT * uptr)
{
    sim_clock_coschedule(uptr, 1000);              /* continue poll */

    imp_timer_task(&imp_data);
    if (uptr->ILEN == 0 && (uptr->STATUS & (IMPIB|IMPID)) == 0)
        imp_packet_in(&imp_data);

    if (imp_data.init_state >= 3 && imp_data.init_state < 6) {
       if (imp_unit[0].flags & UNIT_DHCP && 
           imp_data.dhcp_state != DHCP_STATE_BOUND && 
           imp_data.dhcp_state != DHCP_STATE_REBINDING &&
           imp_data.dhcp_state != DHCP_STATE_RENEWING)
           return SCPE_OK;
       sim_debug(DEBUG_DETAIL, &imp_dev, "IMP init Nop %d\n",
                 imp_data.init_state);
       if (imp_unit[0].ILEN == 0) {
              /* Queue up a nop packet */
              imp_data.rbuffer[0] = 0x4;
#if 0
              imp_data.rbuffer[0] = 0xf;
              imp_data.rbuffer[3] = 4;
#endif
              imp_unit[0].STATUS |= IMPIB;
              imp_unit[0].IPOS = 0;
              imp_unit[0].ILEN = 12*8;
              imp_data.init_state++;
              sim_debug(DEBUG_DETAIL, &imp_dev, "IMP Send Nop %d\n",
                       imp_data.init_state);
              check_interrupts (&imp_unit[0]);
              sim_activate(&imp_unit[0], 100);
        }
    }
    return SCPE_OK;
}

void
imp_timer_task(struct imp_device *imp)
{
    struct imp_packet  *nq = NULL;                /* New send queue */
    int                 n;

    /* Scan through adjusted ports and remove old ones */
    for (n = 0; n < 64; n++) {
        if (imp->port_map[n].cls_tim > 0) {
            if (--imp->port_map[n].cls_tim == 0) {
                imp->port_map[n].dport = 0;
                imp->port_map[n].sport = 0;
                imp->port_map[n].adj = 0;
            }
        }
    }

    /* Scan the send queue and see if any packets have timed out */
    while (imp->sendq != NULL) {
         struct imp_packet *temp = imp->sendq;
         imp->sendq = temp->next;

         if (--temp->life == 0) {
            imp_free_packet(imp, temp);
            sim_debug(DEBUG_DETAIL, &imp_dev,
                        "IMP packet timed out %08x\n", temp->dest);
         } else {
            /* Not yet, put back on queue */
            temp->next = nq;
            nq = temp;
         }
     }
     imp->sendq = nq;
}

t_stat imp_tim_srv(UNIT * uptr)
{
    sim_activate_after(uptr, 1000000);              /* come back once per second */

    imp_dhcp_timer(&imp_data);
    imp_arp_age(&imp_data);
    return SCPE_OK;
}

void
imp_packet_in(struct imp_device *imp)
{
   ETH_PACK                read_buffer;
   struct imp_eth_hdr     *hdr;
   int                     type;
   int                     n;
   int                     pad;

   if (eth_read (&imp_data.etherface, &read_buffer, NULL) <= 0) {
       /* Any pending packet notifications? */
       if (imp->rfnm_count != 0) {
           /* Create RFNM packet */
           memset(&imp->rbuffer[0], 0, 256);
           imp->rbuffer[0] = 0xf;
           imp->rbuffer[3] = 4;
           imp_unit[0].STATUS |= IMPIB;
           imp_unit[0].IPOS = 0;
           imp_unit[0].ILEN = 12*8;
           if (!sim_is_active(&imp_unit[0]))
               sim_activate(&imp_unit[0], 100);
           imp->rfnm_count--;
       }
       return;
   }
   imp_packet_debug(imp, "Received", &read_buffer);
   hdr = (struct imp_eth_hdr *)(&read_buffer.msg[0]);
   type = ntohs(hdr->type);
   if (type == ETHTYPE_ARP) {
       imp_arp_arpin(imp, &read_buffer);
   } else if (type == ETHTYPE_IP) {
       struct ip           *ip_hdr =
              (struct ip *)(&read_buffer.msg[sizeof(struct imp_eth_hdr)]);
       /* Process DHCP is this is IP broadcast */
       if (ip_hdr->ip_dst == broadcast_ipaddr ||
                memcmp(&hdr->dest, &imp->mac, 6) == 0) {
           uint8   *payload = (uint8 *)(&read_buffer.msg[sizeof(struct imp_eth_hdr) +
                                       (ip_hdr->ip_v_hl & 0xf) * 4]);
           struct udp *udp_hdr = (struct udp *)payload;
           /* Check for DHCP traffic */
           if (ip_hdr->ip_p == UDP_PROTO && 
               ntohs(udp_hdr->udp_dport) == DHCP_UDP_PORT_CLIENT &&
               ntohs(udp_hdr->udp_sport) == DHCP_UDP_PORT_SERVER) {
              imp_do_dhcp_client(imp, &read_buffer);
              return;
           }
       }
       /* Process as IP if it is for us */
       if (ip_hdr->ip_dst == imp_data.ip || ip_hdr->ip_dst == 0) {
           /* Add mac address since we will probably need it later */
           imp_arp_update(imp, ip_hdr->ip_src, &hdr->src, 0);
           /* Clear beginning of message */
           memset(&imp->rbuffer[0], 0, 256);
           imp->rbuffer[0] = 0xf;
           imp->rbuffer[3] = 0;
           imp->rbuffer[5] = (ntohl(ip_hdr->ip_src) >> 16) & 0xff;
           imp->rbuffer[7] = 14;
           imp->rbuffer[8] = 0233;
           imp->rbuffer[18] = 0;
           imp->rbuffer[19] = 0x80;
           imp->rbuffer[21] = 0x30;

           /* Copy message over */
           pad = 12 + (imp->padding / 8);
           n = read_buffer.len;
           memcpy(&imp->rbuffer[pad], ip_hdr ,n);
           ip_hdr = (struct ip *)(&imp->rbuffer[pad]);
            /* Re-point IP header to copy packet */
            /*
             * If local IP defined, change destination to ip,
             * and update checksum
             */
           if (ip_hdr->ip_dst == imp_data.ip && imp_data.hostip != 0) {
               uint8   *payload = (uint8 *)(&imp->rbuffer[pad +
                                           (ip_hdr->ip_v_hl & 0xf) * 4]);
               /* If TCP packet update the TCP checksum */
               if (ip_hdr->ip_p == TCP_PROTO) {
                   struct tcp *tcp_hdr = (struct tcp *)payload;
                   uint16       dport = ntohs(tcp_hdr->tcp_dport);
                   uint16       sport = ntohs(tcp_hdr->tcp_sport);
                   int          thl = ((ntohs(tcp_hdr->flags) >> 12) & 0xf) * 4;
                   int          hl = (ip_hdr->ip_v_hl & 0xf) * 4;
                   uint8       *tcp_payload = &imp->rbuffer[
                            sizeof(struct imp_eth_hdr) + hl + thl];
                   checksumadjust((uint8 *)&tcp_hdr->chksum,
                              (uint8 *)(&ip_hdr->ip_dst), sizeof(in_addr_T),
                              (uint8 *)(&imp_data.hostip), sizeof(in_addr_T));
                   if ((ntohs(tcp_hdr->flags) & 0x10) != 0) {
                       for (n = 0; n < 64; n++) {
                           if (imp->port_map[n].sport == sport &&
                               imp->port_map[n].dport == dport) {
                               /* Check if SYN */
                               if (ntohs(tcp_hdr->flags) & 02) {
                                   imp->port_map[n].sport = 0;
                                   imp->port_map[n].dport = 0;
                                   imp->port_map[n].adj = 0;
                               } else {
                                   uint32   new_seq = ntohl(tcp_hdr->ack);
                                   if (new_seq > imp->port_map[n].lseq) {
                                       new_seq = htonl(new_seq - imp->port_map[n].adj);
                                       checksumadjust((uint8 *)&tcp_hdr->chksum,
                                               (uint8 *)(&tcp_hdr->ack), 4,
                                               (uint8 *)(&new_seq), 4);
                                       tcp_hdr->ack = new_seq;
                                   }
                               }
                               if (ntohs(tcp_hdr->flags) & 01)
                                   imp->port_map[n].cls_tim = 100;
                               break;
                           }
                       }
                    }
                    /* Check if recieving to FTP */
                    if (sport == 21 && strncmp((CONST char *)&tcp_payload[0], "PORT ", 5) == 0) {
                        /* We need to translate the IP address to new port number. */
                        int     l = ntohs(ip_hdr->ip_len) - thl - hl;
                        uint32  nip = ntohl(imp->hostip);
                        int     nlen;
                        int     i;
                        char    port_buffer[100];
                        struct udp_hdr     udp_hdr;
                       /* Count out 4 commas */
                       for (i = nlen = 0; i < l && nlen < 4; i++) {
                          if (tcp_payload[i] == ',')
                             nlen++;
                       }
                       nlen = sprintf(port_buffer, "PORT %d,%d,%d,%d,",
                            (nip >> 24) & 0xFF, (nip >> 16) & 0xFF,
                            (nip >> 8) & 0xFF, nip & 0xff);
                       /* Copy over rest of string */
                       while(i < l) {
                           port_buffer[nlen++] = tcp_payload[i++];
                       }
                       port_buffer[nlen] = '\0';
                       memcpy(tcp_payload, port_buffer, nlen);
                       /* Check if we need to update the sequence numbers */
                       if (nlen != l && (ntohs(tcp_hdr->flags) & 02) == 0) {
                           int n = -1;
                           /* See if we need to change the sequence number */
                           for (i = 0; i < 64; i++) {
                               if (imp->port_map[i].sport == sport &&
                                   imp->port_map[i].dport == dport) {
                                   n = i;
                                   break;
                               }
                               if (n < 0 && imp->port_map[i].dport == 0)
                                   n = 0;
                           }
                           if (n >= 0) {
                               imp->port_map[n].dport = dport;
                               imp->port_map[n].sport = sport;
                               imp->port_map[n].adj += nlen - l;
                               imp->port_map[n].cls_tim = 0;
                               imp->port_map[n].lseq = ntohl(tcp_hdr->seq);
                           }
                       }
                       /* Now we need to update the checksums */
                       tcp_hdr->chksum = 0;
                       ip_hdr->ip_len = htons(nlen + thl + hl);
                       ip_checksum((uint8 *)&tcp_hdr->chksum, (uint8 *)tcp_hdr,
                               nlen + thl);
                       udp_hdr.ip_src = ip_hdr->ip_src;
                       udp_hdr.ip_dst = imp->hostip;
                       udp_hdr.zero = 0;
                       udp_hdr.proto = TCP_PROTO;
                       udp_hdr.hlen = htons(nlen + thl);
                       checksumadjust((uint8 *)&tcp_hdr->chksum, (uint8 *)(&udp_hdr), 0,
                            (uint8 *)(&udp_hdr), sizeof(udp_hdr));
                       ip_hdr->ip_sum = 0;
                       ip_checksum((uint8 *)(&ip_hdr->ip_sum), (uint8 *)ip_hdr, 20);
                   }
               /* Check if UDP */
               } else if (ip_hdr->ip_p == UDP_PROTO) {
                    struct udp *udp_hdr = (struct udp *)payload;
                    if (ip_hdr->ip_p == UDP_PROTO &&
                        htons(udp_hdr->udp_dport) == DHCP_UDP_PORT_CLIENT &&
                        htons(udp_hdr->udp_sport) == DHCP_UDP_PORT_SERVER) {
                        imp_do_dhcp_client(imp, &read_buffer);
                        return;
                    }
                    checksumadjust((uint8 *)&udp_hdr->chksum,
                              (uint8 *)(&ip_hdr->ip_src), sizeof(in_addr_T),
                              (uint8 *)(&imp_data.hostip), sizeof(in_addr_T));
               /* Lastly check if ICMP */
               } else if (ip_hdr->ip_p == ICMP_PROTO) {
                    struct icmp *icmp_hdr = (struct icmp *)payload;
                    if ((icmp_hdr->type != 0) &&   /*     Not Echo Reply */
                        (icmp_hdr->type != 8))     /* and Not Echo */
                        checksumadjust((uint8 *)&icmp_hdr->chksum,
                                  (uint8 *)(&ip_hdr->ip_src), sizeof(in_addr_T),
                                  (uint8 *)(&imp_data.hostip), sizeof(in_addr_T));
               }
               checksumadjust((uint8 *)&ip_hdr->ip_sum,
                            (uint8 *)(&ip_hdr->ip_dst), sizeof(in_addr_T),
                            (uint8 *)(&imp_data.hostip), sizeof(in_addr_T));
               ip_hdr->ip_dst = imp_data.hostip;
           }
           /* If we are not initializing queue it up for host */
           if (imp_data.init_state >= 6) {
               n = pad + ntohs(ip_hdr->ip_len);
               imp_unit[0].STATUS |= IMPIB;
               imp_unit[0].IPOS = 0;
               imp_unit[0].ILEN = n*8;
           }
           if (!sim_is_active(&imp_unit[0]))
               sim_activate(&imp_unit[0], 100);
       }
         /* Otherwise just ignore it */
   }
}

void
imp_send_packet (struct imp_device *imp, int len)
{
    ETH_PACK   write_buffer;
    int        i;
    UNIT      *uptr = &imp_unit[1];
    int        n;
    int        st;
    int        lk;
    int        mt;

    lk = 0;
    n = len;
    switch (imp->sbuffer[0] & 0xF) {
    default:
       /* Send back invalid leader message */
       sim_printf("Invalid header\n");
       return;
    case 0x0:
       mt = 0;
       st = imp->sbuffer[3] & 0xf;
       lk = 0233;
       break;
    case 0x4:
       mt = 4;
       st = imp->sbuffer[3] & 0xf;
       break;
    case 0xf:
       st = imp->sbuffer[9] & 0xf;
       lk = imp->sbuffer[8];
       mt = imp->sbuffer[3];
       n = (imp->sbuffer[10] << 8) + (imp->sbuffer[11]);
       break;
    }
    sim_debug(DEBUG_DETAIL, &imp_dev,
        "IMP packet Type=%d ht=%d dh=%d imp=%d lk=%d %d st=%d Len=%d\n",
         imp->sbuffer[3], imp->sbuffer[4], imp->sbuffer[5],
         (imp->sbuffer[6] * 256) + imp->sbuffer[7],
         lk, imp->sbuffer[9] >> 4, st, n);
    switch(mt) {
    case 0:      /* Regular packet */
           switch(st) {
           case 0: /* Regular */
           case 1: /* Refusable */
                  if (lk == 0233) {
                     i = 12 + (imp->padding / 8);
                     n = len - i;
                     memcpy(&write_buffer.msg[sizeof(struct imp_eth_hdr)],
                            &imp->sbuffer[i], n);
                     write_buffer.len = n+sizeof(struct imp_eth_hdr);
                     imp_packet_out(imp, &write_buffer);
                  }
                  break;
           case 2: /* Getting ready */
           case 3: /* Uncontrolled */
           default:
                  break;
           }
           break;
    case 1:      /* Error */
           break;
    case 2:      /* Host going down */
fprintf(stderr, "IMP: Host shutdown\n\r");
           break;
    case 4:      /* Nop */
           if (imp->init_state < 3)
              imp->init_state++;
           imp->padding = st * 16;
           sim_debug(DEBUG_DETAIL, &imp_dev,
                        "IMP recieve Nop %d padding= %d\n",
                         imp->init_state, imp->padding);
           sim_activate(uptr, tmxr_poll); /* Start reciever task */
           break;
    case 8:      /* Error with Message */
           break;
    default:
           break;
    }
    return;
}

/*
 * Check if this packet can be sent to given IP.
 * If it can we fill in the mac address and return false.
 * If we can't we need to queue up and send a ARP packet and return true.
 */
void
imp_packet_out(struct imp_device *imp, ETH_PACK *packet) {
    struct ip_hdr     *pkt = (struct ip_hdr *)(&packet->msg[0]);
    struct imp_packet *send;
    struct arp_entry  *tabptr;
    in_addr_T          ipaddr;
    int                i;

    /* If local IP defined, change source to ip, and update checksum */
    if (imp->hostip != 0) {
       int          hl = (pkt->iphdr.ip_v_hl & 0xf) * 4;
       uint8        *payload = (uint8 *)(&packet->msg[
                            sizeof(struct imp_eth_hdr) + hl]);
       /* If TCP packet update the TCP checksum */
       if (pkt->iphdr.ip_p == TCP_PROTO) {
           struct tcp  *tcp_hdr = (struct tcp *)payload;
           int          thl = ((ntohs(tcp_hdr->flags) >> 12) & 0xf) * 4;
           uint16       sport = ntohs(tcp_hdr->tcp_sport);
           uint16       dport = ntohs(tcp_hdr->tcp_dport);
           uint8       *tcp_payload = &packet->msg[
                    sizeof(struct imp_eth_hdr) + hl + thl];
           /* Update pseudo header checksum */
           checksumadjust((uint8 *)&tcp_hdr->chksum,
                       (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_T),
                       (uint8 *)(&imp->ip), sizeof(in_addr_T));
           /* See if we need to change the sequence number */
           for (i = 0; i < 64; i++) {
               if (imp->port_map[i].sport == sport &&
                   imp->port_map[i].dport == dport) {
                   /* Check if SYN */
                   if (ntohs(tcp_hdr->flags) & 02) {
                       imp->port_map[i].sport = 0;
                       imp->port_map[i].dport = 0;
                       imp->port_map[i].adj = 0;
                   } else {
                       uint32   new_seq = ntohl(tcp_hdr->seq);
                       if (new_seq > imp->port_map[i].lseq) {
                           new_seq = htonl(new_seq + imp->port_map[i].adj);
                           checksumadjust((uint8 *)&tcp_hdr->chksum,
                                   (uint8 *)(&tcp_hdr->seq), 4,
                                   (uint8 *)(&new_seq), 4);
                           tcp_hdr->seq = new_seq;
                       }
                   }
                   if (ntohs(tcp_hdr->flags) & 01)
                       imp->port_map[i].cls_tim = 100;
                   break;
               }
           }
           /* Check if sending to FTP */
           if (dport == 21 && strncmp((CONST char *)&tcp_payload[0], "PORT ", 5) == 0) {
               /* We need to translate the IP address to new port number. */
               int     l = ntohs(pkt->iphdr.ip_len) - thl - hl;
               uint32  nip = ntohl(imp->ip);
               int     nlen;
               char    port_buffer[100];
               struct udp_hdr     udp_hdr;
               /* Count out 4 commas */
               for (i = nlen = 0; i < l && nlen < 4; i++) {
                  if (tcp_payload[i] == ',')
                     nlen++;
               }
               nlen = sprintf(port_buffer, "PORT %d,%d,%d,%d,",
                    (nip >> 24) & 0xFF, (nip >> 16) & 0xFF,
                    (nip >> 8) & 0xFF, nip&0xff);
               /* Copy over rest of string */
               while(i < l) {
                   port_buffer[nlen++] = tcp_payload[i++];
               }
               port_buffer[nlen] = '\0';
               memcpy(tcp_payload, port_buffer, nlen);
               /* Check if we need to update the sequence numbers */
               if (nlen != l && (ntohs(tcp_hdr->flags) & 02) == 0) {
                   int n = -1;
                   /* See if we need to change the sequence number */
                   for (i = 0; i < 64; i++) {
                       if (imp->port_map[i].sport == sport &&
                           imp->port_map[i].dport == dport) {
                           n = i;
                           break;
                       }
                       if (n < 0 && imp->port_map[i].dport == 0)
                           n = 0;
                   }
                   if (n >= 0) {
                       imp->port_map[n].dport = dport;
                       imp->port_map[n].sport = sport;
                       imp->port_map[n].adj += nlen - l;
                       imp->port_map[n].cls_tim = 0;
                       imp->port_map[n].lseq = ntohl(tcp_hdr->seq);
                   }
               }
               /* Now we need to update the checksums */
               tcp_hdr->chksum = 0;
               pkt->iphdr.ip_len = htons(nlen + thl + hl);
               ip_checksum((uint8 *)&tcp_hdr->chksum, (uint8 *)tcp_hdr,
                       nlen + thl);
               udp_hdr.ip_src = imp->ip;
               udp_hdr.ip_dst = pkt->iphdr.ip_dst;
               udp_hdr.zero = 0;
               udp_hdr.proto = TCP_PROTO;
               udp_hdr.hlen = htons(nlen + thl);
               checksumadjust((uint8 *)&tcp_hdr->chksum, (uint8 *)(&udp_hdr), 0,
                    (uint8 *)(&udp_hdr), sizeof(udp_hdr));
               pkt->iphdr.ip_sum = 0;
               ip_checksum((uint8 *)(&pkt->iphdr.ip_sum), (uint8 *)&pkt->iphdr, 20);
               packet->len = nlen + thl + hl +  sizeof(struct imp_eth_hdr);
           }
       /* Check if UDP */
       } else if (pkt->iphdr.ip_p == UDP_PROTO) {
             struct udp *udp_hdr = (struct udp *)payload;
             checksumadjust((uint8 *)&udp_hdr->chksum,
                  (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_T),
                  (uint8 *)(&imp->ip), sizeof(in_addr_T));
        /* Lastly check if ICMP */
       } else if (pkt->iphdr.ip_p == ICMP_PROTO) {
             struct icmp *icmp_hdr = (struct icmp *)payload;
             if ((icmp_hdr->type != 0) &&   /*     Not Echo Reply */
                 (icmp_hdr->type != 8))     /* and Not Echo */
                 checksumadjust((uint8 *)&icmp_hdr->chksum,
                      (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_T),
                      (uint8 *)(&imp->ip), sizeof(in_addr_T));
       }
       /* Lastly update the header and IP address */
       checksumadjust((uint8 *)&pkt->iphdr.ip_sum,
                 (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_T),
                 (uint8 *)(&imp->ip), sizeof(in_addr_T));
       pkt->iphdr.ip_src = imp->ip;
    }

    /* Try to send the packed */
    ipaddr = pkt->iphdr.ip_dst;
    packet->len = sizeof(struct imp_eth_hdr) + ntohs(pkt->iphdr.ip_len);
    /* Enforce minimum packet size */
    while (packet->len < 60)
       packet->msg[packet->len++] = 0;

    /* Check if on our subnet */
    if ((imp->ip & imp->ip_mask) != (ipaddr & imp->ip_mask))
        ipaddr = imp->gwip;

    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &imp->arp_table[i];
        if (ipaddr == tabptr->ipaddr) {
            memcpy(&pkt->ethhdr.dest, &tabptr->ethaddr, 6);
            memcpy(&pkt->ethhdr.src, &imp->mac, 6);
            pkt->ethhdr.type = htons(ETHTYPE_IP);
            imp_write(imp, packet);
            imp->rfnm_count++;
            return;
         }
    }

    /* Queue packet for later send */
    send = imp_get_packet(imp);
    send->next = imp->sendq;
    imp->sendq = send;
    send->packet.len = packet->len;
    send->life = 1000;
    send->dest = pkt->iphdr.ip_dst;
    memcpy(&send->packet.msg[0], pkt, send->packet.len);

    /* We did not find it, so construct and send an ARP packet */
    imp_arp_arpout(imp, ipaddr);
}


void imp_packet_debug(struct imp_device *imp, const char *action, ETH_PACK *packet) {
    struct imp_eth_hdr *eth = (struct imp_eth_hdr *)&packet->msg[0];
    struct arp_hdr     *arp = (struct arp_hdr *)eth;
    struct ip          *ip = (struct ip *)&packet->msg[sizeof(struct imp_eth_hdr)];
    struct udp         *udp;
    struct tcp         *tcp;
    struct icmp        *icmp;
    uint8              *payload;
    struct in_addr     ipaddr;
    size_t             len;
    int                flag;
    char               src_ip[20];
    char               dst_ip[20];
    char               src_port[8];
    char               dst_port[8];
    char               mac_buf[20];
    char               flags[64];
    static struct tcp_flag_bits {
        const char *name;
        uint16      bitmask;
        } bits[] = {
            {"FIN", TCP_FL_FIN},
            {"SYN", TCP_FL_SYN},
            {"RST", TCP_FL_RST},
            {"PSH", TCP_FL_PSH},
            {"ACK", TCP_FL_ACK},
            {"URG", TCP_FL_URG},
            {NULL, 0}
        };
    static const char *icmp_types[] = {
        "Echo Reply",                                   // Type 0
        "Type 1 - Unassigned",
        "Type 2 - Unassigned",
        "Destination Unreachable",                      // Type 3
        "Source Quench (Deprecated)",                   // Type 4
        "Redirect",                                     // Type 5
        "Type 6 - Alternate Host Address (Deprecated)",
        "Type 7 - Unassigned",
        "Echo Request",                                 // Type 8
        "Router Advertisement",                         // Type 9
        "Router Selection",                             // Type 10
        "Time Exceeded",                                // Type 11
        "Type 12 - Parameter Problem",
        "Type 13 - Timestamp",
        "Type 14 - Timestamp Reply",
        "Type 15 - Information Request (Deprecated)",
        "Type 16 - Information Reply (Deprecated)",
        "Type 17 - Address Mask Request (Deprecated)",
        "Type 18 - Address Mask Reply (Deprecated)",
        "Type 19 - Reserved (for Security)",
        "Type 20 - Reserved (for Robustness Experiment)",
        "Type 21 - Reserved (for Robustness Experiment)",
        "Type 22 - Reserved (for Robustness Experiment)",
        "Type 23 - Reserved (for Robustness Experiment)",
        "Type 24 - Reserved (for Robustness Experiment)",
        "Type 25 - Reserved (for Robustness Experiment)",
        "Type 26 - Reserved (for Robustness Experiment)",
        "Type 27 - Reserved (for Robustness Experiment)",
        "Type 28 - Reserved (for Robustness Experiment)",
        "Type 29 - Reserved (for Robustness Experiment)",
        "Type 30 - Traceroute (Deprecated)",
        "Type 31 - Datagram Conversion Error (Deprecated)",
        "Type 32 - Mobile Host Redirect (Deprecated)",
        "Type 33 - IPv6 Where-Are-You (Deprecated)",
        "Type 34 - IPv6 I-Am-Here (Deprecated)",
        "Type 35 - Mobile Registration Request (Deprecated)",
        "Type 36 - Mobile Registration Reply (Deprecated)",
        "Type 37 - Domain Name Request (Deprecated)",
        "Type 38 - Domain Name Reply (Deprecated)",
        "Type 39 - SKIP (Deprecated)",
        "Type 40 - Photuris",
        "Type 41 - ICMP messages utilized by experimental mobility protocols such as Seamoby",
        "Type 42 - Extended Echo Request",
        "Type 43 - Extended Echo Reply"
    };

    if (ntohs(eth->type) == ETHTYPE_ARP) {
        struct in_addr in_addr;
        const char *arp_op = (ARP_REQUEST == ntohs(arp->opcode)) ? "REQUEST" : ((ARP_REPLY == ntohs(arp->opcode)) ? "REPLY" : "Unknown");
        char eth_src[20], eth_dst[20];
        char arp_shwaddr[20], arp_dhwaddr[20];
        char arp_sipaddr[20], arp_dipaddr[20];

        if (!(imp_dev.dctrl & DEBUG_ARP))
            return;
        eth_mac_fmt(&arp->ethhdr.src, eth_src);
        eth_mac_fmt(&arp->ethhdr.dest, eth_dst);
        eth_mac_fmt(&arp->shwaddr, arp_shwaddr);
        memcpy(&in_addr, &arp->sipaddr, sizeof(in_addr));
        strlcpy(arp_sipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_sipaddr));
        eth_mac_fmt(&arp->dhwaddr, arp_dhwaddr);
        memcpy(&in_addr, &arp->dipaddr, sizeof(in_addr));
        strlcpy(arp_dipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_dipaddr));
        sim_debug(DEBUG_ARP, &imp_dev,
            "%s %s EthDst=%s EthSrc=%s shwaddr=%s sipaddr=%s dhwaddr=%s dipaddr=%s\n", 
            action, arp_op, eth_dst, eth_src, arp_shwaddr, arp_sipaddr, arp_dhwaddr, arp_dipaddr);
        return;
    }
    if (ntohs(eth->type) != ETHTYPE_IP)
        return;
    if (!(imp_dev.dctrl & (DEBUG_TCP|DEBUG_UDP|DEBUG_ICMP)))
        return;
    memcpy(&ipaddr, &ip->ip_src, sizeof(ipaddr));
    strlcpy(src_ip, ipv4_inet_ntoa(ipaddr), sizeof(src_ip));
    memcpy(&ipaddr, &ip->ip_dst, sizeof(ipaddr));
    strlcpy(dst_ip, ipv4_inet_ntoa(ipaddr), sizeof(dst_ip));
    payload = (uint8 *)&packet->msg[sizeof(struct imp_eth_hdr) + (ip->ip_v_hl & 0xf) * 4];
    switch (ip->ip_p) {
        case UDP_PROTO:
            udp = (struct udp *)payload;
            snprintf(src_port, sizeof(src_port), "%d", ntohs(udp->udp_sport));
            snprintf(dst_port, sizeof(dst_port), "%d", ntohs(udp->udp_dport));
            sim_debug(DEBUG_UDP, &imp_dev, "%s %d byte packet from %s:%s to %s:%s\n", action,
                ntohs(udp->len), src_ip, src_port, dst_ip, dst_port);
            if ((imp_dev.dctrl & DEBUG_DHCP) && 
                (((ntohs(udp->udp_sport) == DHCP_UDP_PORT_CLIENT) &&
                  (ntohs(udp->udp_dport) == DHCP_UDP_PORT_SERVER)) || 
                 ((ntohs(udp->udp_dport) == DHCP_UDP_PORT_CLIENT) &&
                  (ntohs(udp->udp_sport) == DHCP_UDP_PORT_SERVER)))) {
                struct dhcp         *dhcp = (struct dhcp *)(payload + sizeof(struct udp));
                uint8               *opt = &dhcp->options[0];

                sim_debug(DEBUG_DHCP, &imp_dev, "%s XID=%08X", 
                    (dhcp->op == DHCP_BOOTREQUEST) ? "REQUEST" : 
                                                     (dhcp->op == DHCP_BOOTREPLY) ? "REPLY" : 
                                                                                    "??????",
                    dhcp->xid);
                if (dhcp->ciaddr) {
                    memcpy(&ipaddr, &dhcp->ciaddr, sizeof(ipaddr));
                    sim_debug(DEBUG_DHCP, &imp_dev, ", ciaddr=%s", ipv4_inet_ntoa(ipaddr));
                    }
                if (dhcp->yiaddr) {
                    memcpy(&ipaddr, &dhcp->yiaddr, sizeof(ipaddr));
                    sim_debug(DEBUG_DHCP, &imp_dev, ", yiaddr=%s", ipv4_inet_ntoa(ipaddr));
                    }
                if (dhcp->siaddr) {
                    memcpy(&ipaddr, &dhcp->siaddr, sizeof(ipaddr));
                    sim_debug(DEBUG_DHCP, &imp_dev, ", siaddr=%s", ipv4_inet_ntoa(ipaddr));
                    }
                if (dhcp->giaddr) {
                    memcpy(&ipaddr, &dhcp->giaddr, sizeof(ipaddr));
                    sim_debug(DEBUG_DHCP, &imp_dev, ", giaddr=%s", ipv4_inet_ntoa(ipaddr));
                    }
                eth_mac_fmt((ETH_MAC*)&dhcp->chaddr, mac_buf);
                sim_debug(DEBUG_DHCP, &imp_dev, ", chaddr=%s Options: ", mac_buf);
                while (*opt != DHCP_OPTION_END) {
                    int opt_len;
                    u_long numeric;
                    static const char  *opr_names[] = {
                                        "",             "DISCOVER",     "OFFER",        "REQUEST",
                                        "DECLINE",      "ACK",          "NAK",          "RELEASE",
                                        "INFORM"
                                        };


                    switch(*opt++) {
                    case DHCP_OPTION_PAD:
                          break;
                    default:
                          opt_len = *opt++;
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_SUBNET_MASK:
                          opt_len = *opt++;
                          memcpy(&ipaddr, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", mask=%s", ipv4_inet_ntoa(ipaddr));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_ROUTER:
                          opt_len = *opt++;
                          memcpy(&ipaddr, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", router=%s", ipv4_inet_ntoa(ipaddr));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_REQUESTED_IP:
                          opt_len = *opt++;
                          memcpy(&ipaddr, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", requested-ip=%s", ipv4_inet_ntoa(ipaddr));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_LEASE_TIME:
                          opt_len = *opt++;
                          memcpy(&numeric, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", lease-time=%d", ntohl(numeric));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_T1:
                          opt_len = *opt++;
                          memcpy(&numeric, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", renew-time=%d", ntohl(numeric));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_T2:
                          opt_len = *opt++;
                          memcpy(&numeric, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", rebind-time=%d", ntohl(numeric));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_SERVER_ID:
                          opt_len = *opt++;
                          memcpy(&ipaddr, opt, 4);
                          sim_debug(DEBUG_DHCP, &imp_dev, ", server-ip=%s", ipv4_inet_ntoa(ipaddr));
                          opt += opt_len;
                          break;
                    case DHCP_OPTION_MESSAGE_TYPE:
                          opt_len = *opt++;
                          sim_debug(DEBUG_DHCP, &imp_dev, "MessageType=%s", opr_names[*opt]);
                          opt += opt_len;
                          break;
                        }
                    }
                sim_debug(DEBUG_DHCP, &imp_dev, "\n");
            } else {
                if (udp->len && (imp_dev.dctrl & DEBUG_UDP))
                    sim_data_trace(&imp_dev, imp_unit, payload + sizeof(struct udp), "", 
                                                       ntohs(udp->len), "", DEBUG_DATA);
                }
            break;
        case TCP_PROTO:
            tcp = (struct tcp *)payload;
            snprintf(src_port, sizeof(src_port), "%d", ntohs(tcp->tcp_sport));
            snprintf(dst_port, sizeof(dst_port), "%d", ntohs(tcp->tcp_dport));
            strlcpy(flags, "", sizeof(flags));
            for (flag=0; bits[flag].name; flag++) {
                if (ntohs(tcp->flags) & bits[flag].bitmask) {
                    if (*flags)
                        strlcat(flags, ",", sizeof(flags));
                    strlcat(flags, bits[flag].name, sizeof(flags));
                }

            }
            len = ntohs(ip->ip_len) - ((ip->ip_v_hl & 0xf) * 4 + (ntohs(tcp->flags) >> 12) * 4);
            sim_debug(DEBUG_TCP, &imp_dev, "%s %s%s %d byte packet from %s:%s to %s:%s\n", action,
                        flags, *flags ? ":" : "", (int)len, src_ip, src_port, dst_ip, dst_port);
            if (len && (imp_dev.dctrl & DEBUG_TCP))
                sim_data_trace(&imp_dev, imp_unit, payload + 4 * (ntohs(tcp->flags) >> 12), "", len, "", DEBUG_DATA);
            break;
        case ICMP_PROTO:
            icmp = (struct icmp *)payload;
            len = ntohs(ip->ip_len) - (ip->ip_v_hl & 0xf) * 4;
            sim_debug(DEBUG_ICMP, &imp_dev, "%s %s %d byte packet from %s to %s\n", action,
                (icmp->type < sizeof(icmp_types)/sizeof(icmp_types[0])) ? icmp_types[icmp->type] : "", (int)len, src_ip, dst_ip);
            if (len && (imp_dev.dctrl & DEBUG_ICMP))
                sim_data_trace(&imp_dev, imp_unit, payload + sizeof(struct icmp), "", len, "", DEBUG_DATA);
            break;
    }
}

void imp_write(struct imp_device *imp, ETH_PACK *packet) {
    imp_packet_debug(imp, "Sending", packet);
    eth_write(&imp->etherface, packet, NULL);
}

/*
 * Update the ARP table, first use free entry, else use oldest.
 */
void
imp_arp_update(struct imp_device *imp, in_addr_T ipaddr, ETH_MAC *ethaddr, int age)
{
    struct arp_entry  *tabptr;
    int                i;
    char               mac_buf[20];

    /* Check if entry already in the table. */
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &imp->arp_table[i];

        if (tabptr->ipaddr != 0) {
            if (tabptr->ipaddr == ipaddr) {
                if (0 != memcmp(&tabptr->ethaddr, ethaddr, sizeof(ETH_MAC))) {
                    memcpy(&tabptr->ethaddr, ethaddr, sizeof(ETH_MAC));
                    eth_mac_fmt(ethaddr, mac_buf);
                    sim_debug(DEBUG_ARP, &imp_dev,
                              "updating entry for IP %s to %s\n", 
                              ipv4_inet_ntoa(*((struct in_addr *)&ipaddr)), mac_buf);
                    }
                if (tabptr->age != ARP_DONT_AGE)
                    tabptr->age = age;
                return;
            }
        }
     }

    /* See if we can find an unused entry. */
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &imp->arp_table[i];

        if (tabptr->ipaddr == 0)
            break;
    }

    /* If no empty entry search for oldest one. */
    if (tabptr->ipaddr != 0) {
        int       fnd = 0;
        int16     tmpage = 0;
        for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
            tabptr = &imp->arp_table[i];
            if (tabptr->age > tmpage) {
                tmpage = tabptr->age;
                fnd = i;
            }
        }
        tabptr = &imp->arp_table[fnd];
    }

    /* Now save the entry */
    memcpy(&tabptr->ethaddr, ethaddr, sizeof(ETH_MAC));
    tabptr->ipaddr = ipaddr;
    tabptr->age = age;
    eth_mac_fmt(ethaddr, mac_buf);
    sim_debug(DEBUG_ARP, &imp_dev,
              "creating entry for IP %s to %s, initial age=%d\n", 
              ipv4_inet_ntoa(*((struct in_addr *)&ipaddr)), mac_buf, age);
}

/*
 * Age arp entries and free up old ones.
 */
void imp_arp_age(struct imp_device *imp)
{
    struct arp_entry  *tabptr;
    int                i;

    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &imp->arp_table[i];
        if (tabptr->ipaddr != 0) {      /* active entry? */
            if (tabptr->age != ARP_DONT_AGE)
                tabptr->age++;          /* Age it */
            /* expire too old entries */
            if (tabptr->age > IMP_ARP_MAX_AGE) {
                char mac_buf[20];

                eth_mac_fmt(&tabptr->ethaddr, mac_buf);
                sim_debug(DEBUG_ARP, &imp_dev, 
                          "discarding ARP entry for IP %s %s after %d seconds\n", 
                          ipv4_inet_ntoa(*((struct in_addr *)&tabptr->ipaddr)), mac_buf, IMP_ARP_MAX_AGE);
                memset(tabptr, 0, sizeof(*tabptr));
            }
        }
    }
}


/*
 *  Process incomming ARP packet.
 */

void
imp_arp_arpin(struct imp_device *imp, ETH_PACK *packet)
{
    struct arp_hdr *arp;
    struct in_addr in_addr;
    int             op;
    char            mac_buf[20];

    /* Ignore packet if too short */
    if (packet->len < sizeof(struct arp_hdr))
       return;
    arp = (struct arp_hdr *)(&packet->msg[0]);
    op = ntohs(arp->opcode);
    imp_arp_update(imp, arp->sipaddr, &arp->shwaddr, 0);

    switch (op) {
    case ARP_REQUEST:
        if (arp->dipaddr == imp->ip) {

           arp->opcode = htons(ARP_REPLY);
           memcpy(&arp->dhwaddr, &arp->shwaddr, 6);
           memcpy(&arp->shwaddr, &imp->mac, 6);
           memcpy(&arp->ethhdr.src, &imp->mac, 6);
           memcpy(&arp->ethhdr.dest, &arp->dhwaddr, 6);

           arp->dipaddr = arp->sipaddr;
           arp->sipaddr = imp->ip;
           arp->ethhdr.type = htons(ETHTYPE_ARP);
           packet->len = sizeof(struct arp_hdr);
           imp_write(imp, packet);
           eth_mac_fmt(&arp->dhwaddr, mac_buf);
           sim_debug(DEBUG_ARP, &imp_dev, "replied to received request for IP %s from %s\n",
               ipv4_inet_ntoa(*((struct in_addr *)&imp->ip)), mac_buf);
         }
         break;

    case ARP_REPLY:
        /* Check if this is our address */
        if (arp->dipaddr == imp->ip) {
            struct imp_packet  *nq = NULL;                /* New send queue */

            eth_mac_fmt(&arp->shwaddr, mac_buf);
            memcpy(&in_addr, &arp->sipaddr, sizeof(in_addr));
            sim_debug(DEBUG_ARP, &imp_dev, "received reply for IP %s as %s\n",
               ipv4_inet_ntoa(in_addr), mac_buf);
            /* Scan send queue, and send all packets for this host */
            while (imp->sendq != NULL) {
                struct imp_packet *temp = imp->sendq;
                imp->sendq = temp->next;

                if (temp->dest == arp->sipaddr) {
                    struct ip_hdr     *pkt = (struct ip_hdr *)
                                                     (&temp->packet.msg[0]);
                    memcpy(&pkt->ethhdr.dest, &arp->shwaddr, 6);
                    memcpy(&pkt->ethhdr.src, &imp->mac, 6);
                    pkt->ethhdr.type = htons(ETHTYPE_IP);
                    imp_write(imp, &temp->packet);
                    imp->rfnm_count++;
                    imp_free_packet(imp, temp);
                } else {
                    temp->next = nq;
                    nq = temp;
                }
            }
            imp->sendq = nq;
        }
        break;
    }
    return;
}

/*
 *  Send a ARP_REQUEST packet.
 */

void
imp_arp_arpout(struct imp_device *imp, in_addr_T ipaddr)
{
    struct arp_hdr    *arp;
    ETH_PACK           arp_pkt;

    memset(&arp_pkt, 0, sizeof(ETH_PACK));
    arp = (struct arp_hdr *)(&arp_pkt.msg[0]);
    memcpy(&arp->ethhdr.dest, &broadcast_ethaddr, 6);
    memcpy(&arp->ethhdr.src, &imp->mac, 6);
    arp->ethhdr.type = htons(ETHTYPE_ARP);
    memset(&arp->dhwaddr, 0x00, 6);
    memcpy(&arp->shwaddr, &imp->mac, 6);
    arp->dipaddr = ipaddr;
    arp->sipaddr = imp->ip;
    arp->opcode = htons(ARP_REQUEST);
    arp->hwtype = htons(ARP_HWTYPE_ETH);
    arp->protocol = htons(ETHTYPE_IP);
    arp->hwlen = 6;
    arp->protolen = 4;

    arp_pkt.len = sizeof(struct arp_hdr);
    imp_write(imp, &arp_pkt);
    sim_debug(DEBUG_ARP, &imp_dev, "sending request for IP %s\n", ipv4_inet_ntoa(*((struct in_addr *)&ipaddr)));
}

/*
 *  Lookup an IP's ARP entry.
 */

struct arp_entry *imp_arp_lookup(struct imp_device *imp, in_addr_T ipaddr)
{
    struct arp_entry  *tabptr;
    int                i;

    /* Check if entry already in the table. */
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &imp->arp_table[i];

        if (tabptr->ipaddr != 0) {
            if (tabptr->ipaddr == ipaddr)
                return tabptr;
        }
    }
    return NULL;
}

t_stat imp_set_arp (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    char abuf[CBUFSIZE];
    in_addr_T ip;
    ETH_MAC mac_addr;

    if (!cptr) return SCPE_IERR;

    cptr = get_glyph (cptr, abuf, '=');
    if (cptr && *cptr) {
        if (SCPE_OK != eth_mac_scan (&mac_addr, cptr)) /* scan string for mac, put in mac */
            return sim_messagef(SCPE_ARG, "Invalid MAC address: %s", cptr);
    } else
        return sim_messagef(SCPE_ARG, "Invalid MAC address: %s", cptr);
    if (ipv4_inet_aton (abuf, (struct in_addr *)&ip)) {
        imp_arp_update(&imp_data, ip, &mac_addr, ARP_DONT_AGE);
        return SCPE_OK;
    }
    return sim_messagef(SCPE_ARG, "Invalid IP Address: %s", abuf);
}

t_stat imp_show_arp (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    struct arp_entry  *tabptr;
    int                i;

    fprintf (st, "%-17s%-19s%s\n", "IP Address:", "MAC:", "Age:");

    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        char buf[32];

        tabptr = &imp_data.arp_table[i];

        if (tabptr->ipaddr == 0)
            continue;

        eth_mac_fmt(&tabptr->ethaddr, buf);     /* format ethernet mac address */
        if (tabptr->age == ARP_DONT_AGE)
            fprintf (st, "%-17s%-19s%s\n",
                          ipv4_inet_ntoa(*((struct in_addr *)&tabptr->ipaddr)),
                          buf, "static");
        else
            fprintf (st, "%-17s%-19s%d\n",
                          ipv4_inet_ntoa(*((struct in_addr *)&tabptr->ipaddr)),
                          buf, tabptr->age);
        }
    return SCPE_OK;
}


static int sent_flag = 0;
void sent(int status) {
    sent_flag = 1;
}

static const char  *dhcp_state_names[] = {
                    "OFF",          "REQUESTING",   "INIT",         "REBOOTING",
                    "REBINDING",    "RENEWING",     "SELECTING",    "INFORMING",
                    "CHECKING",     "PERMANENT",    "BOUND",        "RELEASING",
                    "BACKING_OFF"
                    };

static const char  *dhcp_opr_names[16] = {
                    "",             "DISCOVER",     "OFFER",        "REQUEST",
                    "DECLINE",      "ACK",          "NAK",          "RELEASE",
                    "INFORM"
                    };

/* Send out a DHCP packet, fill in IP and Ethernet data. */
void
imp_do_send_dhcp(struct imp_device *imp,
                 const char *op,
                 ETH_PACK *packet,
                 uint8 *last,
                  const struct arp_entry *destarp)
{
    struct ip_hdr     *pkt;
    struct udp        *udp;
    struct udp_hdr     udp_hdr;
    struct in_addr     in_addr;
    int                len;
    char               mac_buf[20];

    pkt = (struct ip_hdr *)(&packet->msg[0]);
    udp = (struct udp *)(&packet->msg[sizeof(struct imp_eth_hdr) +
                          sizeof(struct ip)]);
    len = last - (uint8 *)udp;
    /* Fill in ethernet and IP packet */
    if (destarp)
        memcpy(&pkt->ethhdr.dest, &destarp->ethaddr, 6);
    else
        memcpy(&pkt->ethhdr.dest, &broadcast_ethaddr, 6);
    memcpy(&pkt->ethhdr.src, &imp->mac, 6);
    pkt->ethhdr.type = htons(ETHTYPE_IP);
    pkt->iphdr.ip_v_hl = 0x45;
    pkt->iphdr.ip_id = 1;
    pkt->iphdr.ip_ttl = 128;
    pkt->iphdr.ip_p = UDP_PROTO;
    pkt->iphdr.ip_dst = broadcast_ipaddr;
    udp->udp_sport = htons(DHCP_UDP_PORT_CLIENT);
    udp->udp_dport = htons(DHCP_UDP_PORT_SERVER);
    udp->len = htons(len);
    pkt->iphdr.ip_len = htons(len + sizeof(struct ip));
    ip_checksum((uint8 *)(&pkt->iphdr.ip_sum), (uint8 *)&pkt->iphdr, 20);
    ip_checksum((uint8 *)(&udp->chksum), (uint8 *)(udp), len);
    /* Compute checksum of psuedo header and data */
    udp_hdr.ip_src = pkt->iphdr.ip_src;
    udp_hdr.ip_dst = pkt->iphdr.ip_dst;
    udp_hdr.zero = 0;
    udp_hdr.proto = UDP_PROTO;
    udp_hdr.hlen = udp->len;
    checksumadjust((uint8 *)&udp->chksum, (uint8 *)(&udp_hdr), 0,
               (uint8 *)(&udp_hdr), sizeof(udp_hdr));
    packet->len = len + sizeof(struct ip_hdr);
    imp_write(imp, packet);

    eth_mac_fmt (&pkt->ethhdr.dest, mac_buf);
    memcpy(&in_addr, &udp_hdr.ip_dst, sizeof(in_addr));
    sim_debug(DEBUG_DHCP, &imp_dev,
        "client sent %s packet to: %s:%d(%s)\n", 
        op, ipv4_inet_ntoa(in_addr), ntohs(udp->udp_dport), mac_buf);
}

/* Handle incoming DCHP offer and other requests */
void
imp_do_dhcp_client(struct imp_device *imp, ETH_PACK *read_buffer)
{
    struct ip           *ip_hdr = (struct ip *)
                           (&read_buffer->msg[sizeof(struct imp_eth_hdr)]);
    struct imp_eth_hdr  *eth = (struct imp_eth_hdr  *)read_buffer->msg;
    int                 hl = (ip_hdr->ip_v_hl & 0xf) * 4;
    struct dhcp         *dhcp;
    struct udp          *upkt;
    struct udp_hdr      udp_hdr;
    struct in_addr      in_addr;
    uint8               *opt;
    uint16              sum;
    int                 len;
    in_addr_T           my_ip = 0;               /* Local IP address */
    in_addr_T           my_mask = 0;             /* Local IP mask */
    in_addr_T           my_gw = 0;               /* Gateway IP address */
    int                 lease_time = 0;          /* Lease time */
    int                 renew_time = 0;          /* Renewal time */
    int                 rebind_time = 0;         /* Rebind time */
    in_addr_T           dhcpip = 0;              /* DHCP server address */
    int                 opr = -1;                /* DHCP operation */
    char                mac_buf[20];             /* Source MAC address */

    upkt = (struct udp *)(&((uint8 *)(ip_hdr))[hl]);
    dhcp = (struct dhcp *)(&((uint8 *)(upkt))[sizeof(struct udp)]);

    ip_checksum((uint8 *)&sum, (uint8 *)ip_hdr, hl);
    if (sum != 0) {
       sim_printf("IP checksum error %x\n\r", sum);
       return;
    }
    ip_checksum((uint8 *)(&sum), (uint8 *)(upkt), ntohs(upkt->len));
    udp_hdr.ip_src = ip_hdr->ip_src;
    udp_hdr.ip_dst = ip_hdr->ip_dst;
    udp_hdr.zero = 0;
    udp_hdr.proto = UDP_PROTO;
    udp_hdr.hlen = upkt->len;
    checksumadjust((uint8 *)&sum, 0, 0, (uint8 *)(&udp_hdr), sizeof(udp_hdr));
    if (sum != 0) {
       sim_printf("UDP checksum error %x\n\r", sum);
       return;
    }

    if (memcmp(&dhcp->chaddr, &imp->mac, 6) != 0 || dhcp->xid != imp->dhcp_xid)
       return;

    if (dhcp->op != DHCP_BOOTREPLY)
       return;

    opt = &dhcp->options[0];

    my_ip = dhcp->yiaddr;

    /* Scan and collect the options we care about */
    while (*opt != DHCP_OPTION_END) {
        switch(*opt++) {
        case DHCP_OPTION_PAD:
                              break;
        default:
                              len = *opt++;
                              opt += len;
                              break;
        case DHCP_OPTION_SUBNET_MASK:
                              len = *opt++;
                              memcpy(&my_mask, opt, 4);
                              opt += len;
                              break;
        case DHCP_OPTION_ROUTER:
                              len = *opt++;
                              memcpy(&my_gw, opt, 4);
                              opt += len;
                              break;
        case DHCP_OPTION_REQUESTED_IP:
                              len = *opt++;
                              memcpy(&my_ip, opt, 4);
                              opt += len;
                              break;
        case DHCP_OPTION_LEASE_TIME:
                              len = *opt++;
                              memcpy(&lease_time, opt, 4);
                              lease_time = ntohl(lease_time);
                              opt += len;
                              break;
        case DHCP_OPTION_T1:
                              len = *opt++;
                              memcpy(&renew_time, opt, 4);
                              renew_time = ntohl(renew_time);
                              opt += len;
                              break;
        case DHCP_OPTION_T2:
                              len = *opt++;
                              memcpy(&rebind_time, opt, 4);
                              rebind_time = ntohl(rebind_time);
                              opt += len;
                              break;
        case DHCP_OPTION_SERVER_ID:
                              len = *opt++;
                              memcpy(&dhcpip, opt, 4);
                              opt += len;
                              break;
        case DHCP_OPTION_MESSAGE_TYPE:
                              len = *opt++;
                              opr = *opt;
                              opt += len;
                              break;
        }
    }

    eth_mac_fmt(&eth->src, mac_buf);
    memcpy(&in_addr, &udp_hdr.ip_src, sizeof(in_addr));
    sim_debug(DEBUG_DHCP, &imp_dev,
        "client incoming %s packet: dhcp_state=%s - wait_time=%d from: %s:%d(%s)\n", 
                   (opr == -1) ? "" : dhcp_opr_names[opr], 
                    dhcp_state_names[imp->dhcp_state], imp->dhcp_wait_time,
                    ipv4_inet_ntoa(in_addr), ntohs(upkt->udp_sport), mac_buf);

    /* Process an offer message */
    if (opr == DHCP_OFFER && imp->dhcp_state == DHCP_STATE_SELECTING) {
        /* Create a REQUEST Packet with IP address given */
        imp->dhcp_state = DHCP_STATE_REQUESTING;
        imp->ip = my_ip;
        imp->dhcp_wait_time = 0;
        imp->dhcp_retry_after = 4;
        imp_dhcp_request(imp, dhcpip);
    }
    if (opr == DHCP_ACK && imp->dhcp_xid == dhcp->xid &&
             (imp->dhcp_state == DHCP_STATE_REQUESTING ||
              imp->dhcp_state == DHCP_STATE_REBINDING ||
              imp->dhcp_state == DHCP_STATE_RENEWING)) {
        ETH_PACK            arp_pkt;
        struct arp_hdr      *arp;

        /* Set my IP address to one offered */
        imp->ip = dhcp->yiaddr;
        imp->ip_mask = my_mask;
        imp->gwip = my_gw;
        imp->dhcpip = dhcpip;
        imp->dhcp_state = DHCP_STATE_BOUND;
        imp->dhcp_lease = lease_time;
        imp->dhcp_renew = imp->dhcp_lease / 2;
        if (renew_time)
            imp->dhcp_renew = renew_time;
        imp->dhcp_rebind = (7 * imp->dhcp_lease) / 8;
        if (rebind_time)
            imp->dhcp_rebind = rebind_time;
        for (len = 0; len < 33; len++) {
            if (htonl(mask[len]) == my_mask) {
                imp->maskbits = 32 - len;
                break;
            }
        /* Record a static ARP for the DHCP server */
        imp_arp_update(imp, dhcpip, &eth->src, ARP_DONT_AGE);

        /* Broadcast an ARP Reply with the assigned IP Address */
        memset(&arp_pkt, 0, sizeof(ETH_PACK));
        arp = (struct arp_hdr *)(&arp_pkt.msg[0]);
        memcpy(&arp->ethhdr.dest, &broadcast_ethaddr, 6);
        memcpy(&arp->ethhdr.src, &imp->mac, 6);
        arp->ethhdr.type = htons(ETHTYPE_ARP);
        memcpy(&arp->dhwaddr, &imp->mac, 6);
        memcpy(&arp->shwaddr, &imp->mac, 6);
        arp->dipaddr = imp->ip;
        arp->sipaddr = imp->ip;
        arp->opcode = htons(ARP_REPLY);
        arp->hwtype = htons(ARP_HWTYPE_ETH);
        arp->protocol = htons(ETHTYPE_IP);
        arp->hwlen = 6;
        arp->protolen = 4;
        arp_pkt.len = sizeof(struct arp_hdr);
        imp_write(imp, &arp_pkt);
        }
    }
    if (opr == DHCP_NAK && (imp->dhcp_state == DHCP_STATE_REQUESTING ||
              imp->dhcp_state == DHCP_STATE_REBINDING ||
              imp->dhcp_state == DHCP_STATE_RENEWING)) {
        imp->ip = imp->gwip = imp->dhcpip = imp->maskbits = 0;
        imp->dhcp_state = DHCP_STATE_OFF;

    }
}

void
imp_dhcp_timer(struct imp_device *imp)
{
    if (!(imp_unit[0].flags & UNIT_DHCP))
        return;

    if (imp->dhcp_state == DHCP_STATE_BOUND)
        sim_debug(DEBUG_DETAIL, &imp_dev,
            "DHCP timer: dhcp_state=BOUND, Lease remaining time=%d\n", imp->dhcp_lease);
    else
        sim_debug(DEBUG_DETAIL, &imp_dev,
            "DHCP timer: dhcp_state=%s, wait_time=%d\n", dhcp_state_names[imp->dhcp_state],
                      imp->dhcp_wait_time);

    if ((imp->dhcp_state == DHCP_STATE_BOUND) ||
        (imp->dhcp_state == DHCP_STATE_REBINDING) ||
        (imp->dhcp_state == DHCP_STATE_RENEWING)) {
        if (imp->dhcp_lease != DHCP_INFINITE_LEASE) {
            if (imp->dhcp_lease-- == 0) {
                imp->dhcp_state = DHCP_STATE_OFF;
            } else {
                if (imp->dhcp_rebind-- == 0) {
                    imp->dhcp_state = DHCP_STATE_REBINDING;
                    imp->dhcpip = 0;
                    imp->dhcp_wait_time = 0;
                    imp->dhcp_retry_after = 4;
                } else {
                    if (imp->dhcp_renew-- == 0) {
                        imp->dhcp_state = DHCP_STATE_RENEWING;
                        imp->dhcp_wait_time = 0;
                        imp->dhcp_retry_after = 4;
                    }
                }
            }
        }
    }

    switch (imp->dhcp_state) {
    case DHCP_STATE_REBINDING:
    case DHCP_STATE_RENEWING:
         /* Create a REQUEST Packet with IP address previously given */
         imp_dhcp_request(imp, (imp->dhcp_state == DHCP_STATE_RENEWING) ? imp->dhcpip : 0);
         break;

    case DHCP_STATE_OFF:
         imp->dhcp_wait_time = 0;
         imp->dhcp_retry_after = 4;
         imp_dhcp_discover(imp);
         break;

    case DHCP_STATE_SELECTING:
    case DHCP_STATE_REQUESTING:
         imp->dhcp_wait_time++;
         if (imp->dhcp_wait_time % imp->dhcp_retry_after == 0) {
             if (imp->dhcp_retry_after < 64)
                 imp->dhcp_retry_after *= 2;
             imp->dhcp_wait_time = 0;
             if (imp->dhcp_state == DHCP_STATE_SELECTING)
                 imp_dhcp_discover(imp);
             else
                 imp_dhcp_request(imp, imp->dhcpip);
         }
         break;

    default:
         break;
    }
}

void
imp_dhcp_discover(struct imp_device *imp)
{
    ETH_PACK          dhcp_pkt;
    struct dhcp       *dhcp;
    uint8             *opt;
    int               len;

    /* Fill in Discover packet */
    memset(&dhcp_pkt.msg[0], 0, ETH_FRAME_SIZE);
    dhcp = (struct dhcp *)(&dhcp_pkt.msg[sizeof(struct imp_eth_hdr) +
                          sizeof(struct ip) + sizeof(struct udp)]);

    dhcp->op = DHCP_BOOTREQUEST;
    dhcp->htype = DHCP_HTYPE_ETH;
    dhcp->hlen = 6;
    dhcp->xid = ++imp->dhcp_xid;
    dhcp->secs = imp->dhcp_wait_time;
    dhcp->cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(&dhcp->chaddr, &imp->mac, 6);
    opt = &dhcp->options[0];
    *opt++ = DHCP_OPTION_MESSAGE_TYPE;
    *opt++ = 1;
    *opt++ = DHCP_DISCOVER;
    *opt++ = DHCP_OPTION_CLIENT_ID;
    *opt++ = 7;
    *opt++ = DHCP_HTYPE_ETH;
    for (len = 0; len < 6; len++)
        *opt++ = imp->mac[len];
    if (imp->ip != 0) {
        *opt++ = DHCP_OPTION_REQUESTED_IP;
        *opt++ = 0x04;
        memcpy(opt, &imp->ip, 4);
        opt += 4;
    }
    *opt++= DHCP_OPTION_PARAMETER_REQUEST_LIST;
    *opt++= 2;     /* Number */
    *opt++= DHCP_OPTION_SUBNET_MASK;     /* Subnet mask */
    *opt++= DHCP_OPTION_ROUTER;         /* Routers */
    *opt++= DHCP_OPTION_END;  /* Last option */
    /* Fill in ethernet and IP packet */
    imp_do_send_dhcp(imp, "DISCOVER", &dhcp_pkt, opt, NULL);
    imp->dhcp_state = DHCP_STATE_SELECTING;
}

void
imp_dhcp_request(struct imp_device *imp, in_addr_T dhcpip)
{
    ETH_PACK          dhcp_pkt;
    struct dhcp       *dhcp;
    uint8             *opt;
    int               len;

    /* Fill in Request packet */
    memset(&dhcp_pkt.msg[0], 0, ETH_FRAME_SIZE);
    dhcp = (struct dhcp *)(&dhcp_pkt.msg[sizeof(struct imp_eth_hdr) +
                          sizeof(struct ip) + sizeof(struct udp)]);

    dhcp->op = DHCP_BOOTREQUEST;
    dhcp->htype = DHCP_HTYPE_ETH;
    dhcp->hlen = 6;
    dhcp->xid = imp->dhcp_xid;
    dhcp->cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(&dhcp->chaddr, &imp->mac, 6);
    opt = &dhcp->options[0];
    *opt++ = DHCP_OPTION_MESSAGE_TYPE;
    *opt++ = 1;
    *opt++ = DHCP_REQUEST;
    *opt++ = DHCP_OPTION_REQUESTED_IP;
    *opt++ = 4;
    memcpy(opt, &imp->ip, 4);
    opt += 4;
    if (dhcpip) {
        *opt++ = DHCP_OPTION_SERVER_ID;
        *opt++ = 4;
        memcpy(opt, &dhcpip, 4);
        opt += 4;
    }
    *opt++ = DHCP_OPTION_CLIENT_ID;
    *opt++ = 7;
    *opt++ = DHCP_HTYPE_ETH;
    for (len = 0; len < 6; len++)
        *opt++ = imp->mac[len];
    *opt++ = DHCP_OPTION_PARAMETER_REQUEST_LIST;
    *opt++ = 4;     /* Number */
    *opt++ = DHCP_OPTION_SUBNET_MASK;     /* Subnet mask */
    *opt++ = DHCP_OPTION_ROUTER;         /* Routers */
    *opt++ = DHCP_OPTION_T1;            /* Renewal time */
    *opt++ = DHCP_OPTION_T2;            /* Rebinding time */
    *opt++ = DHCP_OPTION_END;  /* Last option */
    imp_do_send_dhcp(imp, "REQUEST", &dhcp_pkt, opt, imp_arp_lookup(imp, dhcpip));
}

void
imp_dhcp_release(struct imp_device *imp)
{
    ETH_PACK          dhcp_pkt;
    struct dhcp       *dhcp;
    uint8             *opt;
    int               len;


    /* Nothing to send if we are not bound */
    if (imp->dhcp_state == DHCP_STATE_OFF)
        return;
    /* Fill in DHCP RELEASE PACKET */
    memset(&dhcp_pkt.msg[0], 0, ETH_FRAME_SIZE);
    dhcp = (struct dhcp *)(&dhcp_pkt.msg[sizeof(struct imp_eth_hdr) +
                          sizeof(struct ip) + sizeof(struct udp)]);

    dhcp->op = DHCP_BOOTREQUEST;
    dhcp->htype = DHCP_HTYPE_ETH;
    dhcp->hlen = 6;
    dhcp->xid = ++imp->dhcp_xid;
    dhcp->ciaddr = imp->ip;
    dhcp->cookie = htonl(DHCP_MAGIC_COOKIE);
    memcpy(&dhcp->chaddr, &imp->mac, 6);
    opt = &dhcp->options[0];
    *opt++ = DHCP_OPTION_SERVER_ID;
    *opt++ = 4;
    memcpy(opt, &imp->dhcpip, 4);
    opt += 4;
    *opt++ = DHCP_OPTION_MESSAGE_TYPE;
    *opt++ = 1;
    *opt++ = DHCP_RELEASE;
    if (imp->ip != 0) {
        *opt++ = DHCP_OPTION_REQUESTED_IP;
        *opt++ = 0x04;
        memcpy(opt, &imp->ip, 4);
        opt += 4;
    }
    *opt++ = DHCP_OPTION_CLIENT_ID;
    *opt++ = 7;
    *opt++ = DHCP_HTYPE_ETH;
    for (len = 0; len < 6; len++)
        *opt++ = imp->mac[len];
    *opt++= DHCP_OPTION_END;  /* Last option */
    imp_do_send_dhcp(imp, "RELEASE", &dhcp_pkt, opt, imp_arp_lookup(imp, imp->dhcpip));
    imp->dhcp_state = DHCP_STATE_OFF;
    imp->ip = imp->gwip = imp->dhcpip = imp->maskbits = 0;
}



static char *
ipv4_inet_ntoa(struct in_addr ip)
{
   static char str[20];

   if (sim_end)
       sprintf (str, "%d.%d.%d.%d", ip.s_addr & 0xFF, (ip.s_addr >> 8) & 0xFF,
                              (ip.s_addr >> 16) & 0xFF, (ip.s_addr >> 24) & 0xFF);
   else
       sprintf (str, "%d.%d.%d.%d", (ip.s_addr >> 24) & 0xFF, (ip.s_addr >> 16) & 0xFF,
                              (ip.s_addr >> 8) & 0xFF, ip.s_addr & 0xFF);
   return str;
}

static
int ipv4_inet_aton(const char *str, struct in_addr *inp)
{
    unsigned long bytes[4];
    int i = 0;
    char *end;
    in_addr_T val;

    for (i=0; (i < 4) && isdigit (*str); i++) {
        bytes[i] = strtoul (str, &end, 0);
        if (str == end)
            return 0;
        str = end;
        if (*str == '.')
            ++str;
        }
    if (*str && (*str != '/'))
        return 0;
    switch (i) {
    case 1:
            val = bytes[0];
            break;
    case 2:
            if ((bytes[0] > 0xFF) || (bytes[1] > 0xFFFFFF))
                return 0;
            val = (bytes[0] << 24) | bytes[1];
            break;
    case 3:
            if ((bytes[0] > 0xFF) || (bytes[1] > 0xFF) || (bytes[2] > 0xFFFF))
                return 0;
            val = (bytes[0] << 24) | (bytes[1] << 16) | bytes[2];
            break;
    case 4:
            if ((bytes[0] > 0xFF) || (bytes[1] > 0xFF) || (bytes[2] > 0xFF)
                  || (bytes[3] > 0xFF))
                return 0;
            val = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            break;
    default:
            return 0;
    }
    if (inp)
        *(in_addr_T *)inp = htonl (val);
    return 1;
}

t_stat imp_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mpx;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    mpx = (int32) get_uint (cptr, 8, 8, &r);
    if (r != SCPE_OK)
        return r;
    imp_mpx_lvl = mpx;
    return SCPE_OK;
}

t_stat imp_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "MPX=%o", imp_mpx_lvl);
   return SCPE_OK;
}

t_stat imp_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];
    eth_mac_fmt(&imp_data.mac, buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat imp_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    status = eth_mac_scan_ex(&imp_data.mac, cptr, uptr);
    if (status != SCPE_OK)
      return status;

    memcpy(&imp_data.bcast, &broadcast_ethaddr, 6);
    return SCPE_OK;
}

t_stat imp_show_ip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;
   ip.s_addr = imp_data.ip;
   fprintf (st, "IP=%s/%d", ipv4_inet_ntoa(ip), imp_data.maskbits);
   return SCPE_OK;
}

t_stat imp_set_ip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    char abuf[CBUFSIZE];
    struct in_addr  ip;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    cptr = get_glyph (cptr, abuf, '/');
    if (cptr && *cptr) {
          imp_data.maskbits = atoi (cptr);
          if (imp_data.maskbits < 0)
              imp_data.maskbits = 0;
          if (imp_data.maskbits > 32)
              imp_data.maskbits = 32;
    } else
          imp_data.maskbits = 32;
    if (ipv4_inet_aton (abuf, &ip)) {
        imp_data.ip = ip.s_addr;
        imp_data.ip_mask =  htonl(mask[imp_data.maskbits]);
        if (uptr->flags & UNIT_DHCP) {
            uptr->flags &= ~UNIT_DHCP;
            return sim_messagef (SCPE_OK, "IMP DHCP disabled\n");
        }
        return SCPE_OK;
    }
    return SCPE_ARG;
}

t_stat imp_show_gwip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;

   ip.s_addr = imp_data.gwip;
   fprintf (st, "GW=%s", ipv4_inet_ntoa(ip));
   return SCPE_OK;
}

t_stat imp_set_gwip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    struct in_addr  ip;
    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    if (ipv4_inet_aton (cptr, &ip)) {
        imp_data.gwip = ip.s_addr;
        if (uptr->flags & UNIT_DHCP) {
            uptr->flags &= ~UNIT_DHCP;
            return sim_messagef (SCPE_OK, "IMP DHCP disabled\n");
        }
       return SCPE_OK;
    }
    return SCPE_ARG;
}

t_stat imp_show_dhcpip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    if (!(uptr->flags & UNIT_DHCP)) {
        fprintf (st, "DHCP disabled");
    } else {
        struct in_addr ip;
        ip.s_addr = imp_data.dhcpip;
        fprintf (st, "DHCP Server IP=%s", ipv4_inet_ntoa(ip));
        if (imp_data.dhcp_state == DHCP_STATE_BOUND) {
            fprintf (st, ", Lease Expires in %d seconds", imp_data.dhcp_lease);
        } else {
            fprintf (st, ", State:%s, Waited %d seconds", dhcp_state_names[imp_data.dhcp_state],
                  imp_data.dhcp_wait_time);            
        }
    }
   return SCPE_OK;
}

t_stat imp_show_hostip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;

   ip.s_addr = imp_data.hostip;
   fprintf (st, "HOST=%s", ipv4_inet_ntoa(ip));
   return SCPE_OK;
}

t_stat imp_set_hostip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
   struct in_addr ip;
    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    if (ipv4_inet_aton (cptr, &ip)) {
       imp_data.hostip = ip.s_addr;
       return SCPE_OK;
    }
    return SCPE_ARG;
}

struct imp_packet *
imp_get_packet(struct imp_device *imp) {
    struct imp_packet *ret;
    /* Check if list empty */
    if ((ret = imp->freeq) != NULL) {
        imp->freeq = ret->next;
        ret->next = NULL;
    }
    return ret;
}

void
imp_free_packet(struct imp_device *imp, struct imp_packet *p) {
    p->next = imp->freeq;
    imp->freeq = p;
}

t_stat imp_reset (DEVICE *dptr)
{
    int  i;
    struct imp_packet *p;

    for (i = 0; i < 6; i++) {
        if (imp_data.mac[i] != 0)
            break;
    }
    if (i == 6) {   /* First call to reset? */
        /* Set a default MAC address in a BBN assigned OID range no longer in use */
        imp_set_mac (dptr->units, 0, "00:00:02:00:00:00/24", NULL);
        /* Clear ARP table. */
        memset(imp_data.arp_table, 0, sizeof(imp_data.arp_table));
        imp_data.dhcp_state = DHCP_STATE_OFF;
    }
    /* Clear queues. */
    imp_data.sendq = NULL;
    /* Set up free queue */
    p = NULL;
    for (i = 0; i < (int)(sizeof(imp_buffer)/sizeof(struct imp_packet)); i++) {
        imp_buffer[i].next = p;
        p = &imp_buffer[i];
    }
    /* Fix last entry */
    imp_data.freeq = p;
    imp_data.init_state = 0;
    last_coni = sim_gtime();
    if (imp_unit[0].flags & UNIT_ATT)
        sim_activate_after(&imp_unit[2], 1000000);    /* Start Timer service */
    return SCPE_OK;
}

/* attach device: */
t_stat imp_attach(UNIT* uptr, CONST char* cptr)
{
    t_stat status;
    char* tptr;
    char buf[32];

    /* Set to correct device number */
    switch(GET_DTYPE(imp_unit[0].flags)) {
    case TYPE_MIT:
    case TYPE_BBN:
                   imp_dib.dev_num = IMP_DEVNUM;
                   break;
    case TYPE_WAITS:
                   imp_dib.dev_num = WA_IMP_DEVNUM;
                   break;
    }
    if (!(uptr->flags & UNIT_DHCP) && imp_data.ip == 0)
      return sim_messagef (SCPE_NOATT, "%s: An IP Address must be specified when DHCP is disabled\n", 
               imp_dev.name);

    tptr = (char *) malloc(strlen(cptr) + 1);
    if (tptr == NULL) return SCPE_MEM;
    strcpy(tptr, cptr);

    memset(&imp_data.ReadQ, 0, sizeof(ETH_QUE));
    status = eth_open(&imp_data.etherface, cptr, &imp_dev, DEBUG_ETHER);
    if (status != SCPE_OK) {
      free(tptr);
      return status;
    }
    eth_mac_fmt(&imp_data.mac, buf);     /* format ethernet mac address */
    if (SCPE_OK != eth_check_address_conflict (&imp_data.etherface, &imp_data.mac)) {
      eth_close(&imp_data.etherface);
      free(tptr);
      return sim_messagef (SCPE_NOATT, "%s: MAC Address Conflict on LAN for address %s\n",
                      imp_dev.name, buf);
    }
    if (SCPE_OK != eth_filter(&imp_data.etherface, 2, &imp_data.mac, 0, 0)) {
      eth_close(&imp_data.etherface);
      free(tptr);
      return sim_messagef (SCPE_NOATT, "%s: Can't set packet filter for MAC Address %s\n",
                       imp_dev.name, buf);
    }

    uptr->filename = tptr;
    uptr->flags |= UNIT_ATT;
    eth_setcrc(&imp_data.etherface, 0);     /* Don't need CRC */

    /* init read queue (first time only) */
    status = ethq_init(&imp_data.ReadQ, 8);
    if (status != SCPE_OK) {
      eth_close(&imp_data.etherface);
      uptr->filename = NULL;
      free(tptr);
      return sim_messagef (status, "%s: Can't initialize receive queue\n", imp_dev.name);
    }

    /* Pick a relatively unique starting xid, presuming that the 
       MAC address has been verified unique on the LAN by eth_open */
    imp_data.dhcp_xid = (imp_data.mac[0] | (imp_data.mac[1] << 8) |
                        (imp_data.mac[2] << 16) | (imp_data.mac[3] << 24)) + (uint32)time(NULL);
    imp_data.dhcp_state = DHCP_STATE_OFF;
    memset(imp_data.arp_table, 0, sizeof(imp_data.arp_table));

    /* If we're not doing DHCP and a gateway is defined on the network
       then define a static APR entry for the gateway to facilitate 
       outbound routing */
    if (!(uptr->flags & UNIT_DHCP) && imp_data.gwip) {
      ETH_PACK                read_buffer;
      struct imp_eth_hdr      *hdr = (struct imp_eth_hdr *)(&read_buffer.msg[0]);
      int                     type;
      struct arp_entry        *arp;

      imp_arp_arpout(&imp_data, imp_data.gwip);
      sim_os_ms_sleep (300);   /* wait for an ARP response to arrive */

      /* empty the read queue and find ARP_REPLYs */
      do {
        memset (&read_buffer, 0, sizeof(read_buffer));
        status = eth_read (&imp_data.etherface, &read_buffer, NULL);
        type = ntohs(hdr->type);
        if (type == ETHTYPE_ARP)
          imp_arp_arpin(&imp_data, &read_buffer);
      } while (read_buffer.len > 0);
      if ((arp = imp_arp_lookup(&imp_data, imp_data.gwip)))
        imp_arp_update(&imp_data, imp_data.gwip, &arp->ethaddr, ARP_DONT_AGE);
    }

    eth_set_async (&imp_data.etherface, 0); /* Allow Asynchronous inbound packets */
    sim_activate_after(&imp_unit[2], 1000000);    /* Start Timer service */
    return SCPE_OK;
}

/* detach device: */

t_stat imp_detach(UNIT* uptr)
{

    if (uptr->flags & UNIT_ATT) {
        /* If DHCP, release our IP address */
        if (uptr->flags & UNIT_DHCP) {
          imp_dhcp_release(&imp_data);
        }
        sim_cancel (uptr+1);                /* stop the packet timing services */
        sim_cancel (uptr+2);                /* stop the clock timer services */
        eth_close (&imp_data.etherface);
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
    }
    return SCPE_OK;
}

t_stat imp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "IMP interface\n\n");
fprintf (st, "The IMP acted as an interface to the early internet. ");
fprintf (st, "This interface operated\nat the TCP/IP level rather than the ");
fprintf (st, "Ethernet level. This interface allows for\nITS or Tenex to be ");
fprintf (st, "placed on the internet. The interface connects up to a TAP\n");
fprintf (st, "or direct ethernet connection. If the host is to be run at an ");
fprintf (st, "arbitrary IP\naddress, then the HOST should be set to the IP ");
fprintf (st, "of ITS. The network interface\nwill translate this IP address ");
fprintf (st, "to the one set in IP. If HOST is set to 0.0.0.0,\nno ");
fprintf (st, "translation will take place. IP should be set to the external ");
fprintf (st, "address of\nthe IMP, along the number of bits in the net mask. ");
fprintf (st, "GW points to the default\nrouter. If DHCP is enabled these ");
fprintf (st, "will be set from DHCP when the IMP is attached.\nIf IP is set ");
fprintf (st, "and DHCP is enabled, when the IMP is attached it will inform\n");
fprintf (st, "the local DHCP server of it's address.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
eth_attach_help(st, dptr, uptr, flag, cptr);
return SCPE_OK;
}


const char *imp_description (DEVICE *dptr)
{
    return "KA Host/IMP interface";
}
#endif
