/* kl10_NIA.c: NIA 20 Network interface.

   Copyright (c) 2020, Richard Cornwell.

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

#if NUM_DEVS_NIA > 0
#define NIA_DEVNUM  (0540 + (5 * 4))

/* NIA Bits */

/* CONI */
#define NIA_PPT     0400000000000LL            /* Port present */
#define NIA_DCC     0100000000000LL            /* Diag CSR */
#define NIA_CPE     0004000000000LL            /* CRAM Parity error */
#define NIA_MBE     0002000000000LL            /* MBUS error */
#define NIA_ILD     0000100000000LL            /* Idle */
#define NIA_DCP     0000040000000LL            /* Disable complete */
#define NIA_ECP     0000020000000LL            /* Enable complete */
#define NIA_PID     0000007000000LL            /* Port ID */

/* CONO/ CONI */
#define NIA_CPT     0000000400000LL            /* Clear Port */
#define NIA_SEB     0000000200000LL            /* Diag Select EBUF */
#define NIA_GEB     0000000100000LL            /* Diag Gen Ebus PE */
#define NIA_LAR     0000000040000LL            /* Diag select LAR */
#define NIA_SSC     0000000020000LL            /* Diag Single Cycle */
#define NIA_EPE     0000000004000LL            /* Ebus parity error */
#define NIA_FQE     0000000002000LL            /* Free Queue Error */
#define NIA_DME     0000000001000LL            /* Data mover error */
#define NIA_CQA     0000000000400LL            /* Command Queue Available */
#define NIA_RQA     0000000000200LL            /* Response Queue Available */
#define NIA_DIS     0000000000040LL            /* Disable */
#define NIA_ENB     0000000000020LL            /* Enable */
#define NIA_MRN     0000000000010LL            /* RUN */
#define NIA_PIA     0000000000007LL            /* PIA */

#define NIA_LRA     0400000000000LL            /* Load Ram address */
#define NIA_RAR     0377760000000LL            /* Microcode address mask */
#define NIA_MSB     0000020000000LL            /* Half word select */

/* PCB Offsets */
#define PCB_CQI     0                          /* Command queue interlock */
#define PCB_CQF     1                          /* Command queue flink */
#define PCB_CQB     2                          /* Command queue blink */
#define PCB_RS0     3                          /* Reserved */
#define PCB_RSI     4                          /* Response queue interlock */
#define PCB_RSF     5                          /* Response queue flink */
#define PCB_RSB     6                          /* Response queue blink */
#define PCB_RS1     7                          /* Reserved */
#define PCB_UPI     010                        /* Unknown protocol queue interlock */
#define PCB_UPF     011                        /* Unknown protocol queue flink */
#define PCB_UPB     012                        /* Unknown protocol queue blink */
#define PCB_UPL     013                        /* Unknown protocol queue length */
#define PCB_RS2     014                        /* Reserved */
#define PCB_PTT     015                        /* Protocol Type Table */
#define PCB_MCT     016                        /* Multicast Table */
#define PCB_RS3     017                        /* Reserved */
#define PCB_ER0     020                        /* Error Log out 0 */
#define PCB_ER1     021                        /* Error Log out 1 */
#define PCB_EPA     022                        /* EPT Channel logout word 1 address */
#define PCB_EPW     023                        /* EPT Channel logout word 1 contents */
#define PCB_PCB     024                        /* PCB Base Address */
#define PCB_PIA     025                        /* PIA */
#define PCB_RS4     026                        /* Reserved */
#define PCB_CCW     027                        /* Channel command word */
#define PCB_RCB     030                        /* Counters base address */

#define CHNERR      07762
#define SLFTST      07751
#define INTERR      07750
#define EBSERR      07752

/* 12 Bit Shift */
#define NIA_CMD_SND   0001                     /* Send a datagram */
#define NIA_CMD_LMAC  0002                     /* Load Multicast table */
#define NIA_CMD_LPTT  0003                     /* Load Protocol Type table */
#define NIA_CMD_RCNT  0004                     /* Read counts */
#define NIA_CMD_RCV   0005                     /* Received datagram */
#define NIA_CMD_WPLI  0006                     /* Write PLI */
#define NIA_CMD_RPLI  0007                     /* Read PLI */
#define NIA_CMD_RNSA  0010                     /* Read Station Address */
#define NIA_CMD_WNSA  0011                     /* Write Station Address */

/* 20 Bit shift */
#define NIA_FLG_RESP  0001                     /* Command wants a response */
#define NIA_FLG_CLRC  0002                     /* Clear counters (Read counters) */
#define NIA_FLG_BSD   0010                     /* Send BSD packet */
#define NIA_FLG_PAD   0040                     /* Send pad */
#define NIA_FLG_ICRC  0100                     /* Send use host CRC */
#define NIA_FLG_PACK  0200                     /* Send Pack */
#define NIA_STS_CPE   0200                     /* CRAM PE */
#define NIA_STS_SR    0100                     /* Send receive */
#define NIA_STS_ERR   0001                     /* Error bits valid */

/* 28 bit shift ERR +1 */
#define NIA_ERR_ECL   000                      /* Excessive collisions */
#define NIA_ERR_CAR   001                      /* Carrier check failed */
#define NIA_ERR_COL   002                      /* Collision detect failed */
#define NIA_ERR_SHT   003                      /* Short circuit */
#define NIA_ERR_OPN   004                      /* Open circuit */
#define NIA_ERR_LNG   005                      /* Frame to long */
#define NIA_ERR_RMT   006                      /* Remote failure */
#define NIA_ERR_BLK   007                      /* Block check error */
#define NIA_ERR_FRM   010                      /* Framing error */
#define NIA_ERR_OVR   011                      /* Data Overrun */
#define NIA_ERR_PRO   012                      /* Unrecongized protocol */
#define NIA_ERR_RUN   013                      /* Frame too short */
#define NIA_ERR_WCZ   030                      /* Word count not zero */
#define NIA_ERR_QLV   031                      /* Queue length violation */
#define NIA_ERR_PLI   032                      /* Illegal PLI function */
#define NIA_ERR_UNK   033                      /* Unknown command */
#define NIA_ERR_BLV   034                      /* Buffer length violation */
#define NIA_ERR_PAR   036                      /* Parity error */
#define NIA_ERR_INT   037                      /* Internal error */

/* Counters */
#define NIA_CNT_BR    000           /* Bytes received */
#define NIA_CNT_BX    001           /* Bytes transmitted */
#define NIA_CNT_FR    002           /* Frames received */
#define NIA_CNT_FX    003           /* Frames transmitted */
#define NIA_CNT_MCB   004           /* Multicast bytes received */
#define NIA_CNT_MCF   005           /* Multicast frames received */
#define NIA_CNT_FXD   006           /* Frames xmitted, initially deferred */
#define NIA_CNT_FXS   007           /* Frames xmitted, single collision */
#define NIA_CNT_FXM   010           /* Frames xmitted, multiple collisions */
#define NIA_CNT_XF    011           /* Transmit failures */
#define NIA_CNT_XFM   012           /* Transmit failure bit mask */
#define NIA_XFM_LOC   04000         /* B24 - Loss of carrier */
#define NIA_XFM_XBP   02000         /* B25 - Xmit buffer parity error */
#define NIA_XFM_RFD   01000         /* B26 - Remote failure to defer */
#define NIA_XFM_XFL   00400         /* B27 - Xmitted frame too long */
#define NIA_XFM_OC    00200         /* B28 - Open circuit */
#define NIA_XFM_SC    00100         /* B29 - Short circuit */
#define NIA_XFM_CCF   00040         /* B30 - Collision detect check failed */
#define NIA_XFM_EXC   00020         /* B31 - Excessive collisions */

#define NIA_CNT_CDF   013           /* Carrier detect check failed */
#define NIA_CNT_RF    014           /* Receive failures */
#define NIA_CNT_RFM   015           /* Receive failure bit mask */
#define NIA_RFM_FLE   0400          /* B27 - free list parity error */
#define NIA_RFM_NFB   0200          /* B28 - no free buffers */
#define NIA_RFM_FTL   0100          /* B29 - frame too long */
#define NIA_RFM_FER   0040          /* B30 - framing error */
#define NIA_RFM_BCE   0020          /* B31 - block check error */

#define NIA_CNT_DUN   016           /* Discarded unknown */
#define NIA_CNT_D01   017           /* Discarded position 1 */
#define NIA_CNT_D02   020           /* Discarded position 2 */
#define NIA_CNT_D03   021           /* Discarded position 3 */
#define NIA_CNT_D04   022           /* Discarded position 4 */
#define NIA_CNT_D05   023           /* Discarded position 5 */
#define NIA_CNT_D06   024           /* Discarded position 6 */
#define NIA_CNT_D07   025           /* Discarded position 7 */
#define NIA_CNT_D08   026           /* Discarded position 8 */
#define NIA_CNT_D09   027           /* Discarded position 9 */
#define NIA_CNT_D10   030           /* Discarded position 10 */
#define NIA_CNT_D11   031           /* Discarded position 11 */
#define NIA_CNT_D12   032           /* Discarded position 12 */
#define NIA_CNT_D13   033           /* Discarded position 13 */
#define NIA_CNT_D14   034           /* Discarded position 14 */
#define NIA_CNT_D15   035           /* Discarded position 15 */
#define NIA_CNT_D16   036           /* Discarded position 16 */
#define NIA_CNT_UFD   037           /* Unrecognized frame dest */
#define NIA_CNT_DOV   040           /* Data overrun */
#define NIA_CNT_SBU   041           /* System buffer unavailable */
#define NIA_CNT_UBU   042           /* User buffer unavailable */
#define NIA_CNT_RS0   043           /* Pli reg rd par error,,pli parity error */
#define NIA_CNT_RS1   044           /* Mover parity error,,cbus parity error */
#define NIA_CNT_RS2   045           /* Ebus parity error,,ebus que parity error */
#define NIA_CNT_RS3   046           /* Channel error,,spur channel error */
#define NIA_CNT_RS4   047           /* Spur xmit attn error,,cbus req timout error */
#define NIA_CNT_RS5   050           /* Ebus req timeout error,,csr grnt timeout error */
#define NIA_CNT_RS6   051           /* Used buff parity error,,xmit buff parity error */
#define NIA_CNT_RS7   052           /* Reserved for ucode */
#define NIA_CNT_RS8   053           /* Reserved for ucode */
#define NIA_CNT_LEN   054           /* # of counters */

typedef uint32 in_addr_T;

#define ETHTYPE_ARP 0x0806
#define ETHTYPE_IP  0x0800

PACKED_BEGIN
struct nia_eth_hdr {
    ETH_MAC    dest;
    ETH_MAC    src;
    uint16     type;
} PACKED_END;

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
    struct nia_eth_hdr  ethhdr;
    struct ip           iphdr;
} PACKED_END;

#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_HWTYPE_ETH  1

PACKED_BEGIN
struct arp_hdr {
    struct nia_eth_hdr  ethhdr;
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

struct nia_device {
    ETH_PCALLBACK     rcallback;               /* read callback routine */
    ETH_PCALLBACK     wcallback;               /* write callback routine */
    ETH_MAC           mac;                     /* Hardware MAC addresses */
    ETH_DEV           etherface;
    ETH_QUE           ReadQ;
    ETH_PACK          rec_buff;                /* Buffer for recieved packet */
    ETH_PACK          snd_buff;                /* Buffer for sending packet */
    t_addr            cmd_entry;               /* Pointer to current command entry */
    t_addr            cmd_rply;                /* Pointer to reply entry */
    uint8             cmd_status;              /* Status feild of current command */
    t_addr            rec_entry;               /* Pointer to current recieve entry */
    t_addr            pcb;                     /* Address of PCB */
    t_addr            rcb;                     /* Read count buffer address */
#define cmd_hdr pcb                            /* Command queue is at PCB */
    t_addr            resp_hdr;                /* Head of response queue */
    t_addr            unk_hdr;                 /* Unknown protocol free queue */
    int               unk_len;                 /* Length of Unknown entries */
    t_addr            ptt_addr;                /* Address of Protocol table */
    t_addr            mcast_addr;              /* Address of Multicast table */
    int               pia;                     /* Interrupt channel */
    t_addr            cnt_addr;                /* Address of counters */
    uint64            pcnt[NIA_CNT_LEN];       /* Counters */

    int               ptt_n;                   /* Number of Protocol entries */
    uint16            ptt_proto[17];           /* Protocol for entry */
    t_addr            ptt_head[17];            /* Head of protocol queue */
    int               macs_n;                  /* Number of multi-cast addresses */
    ETH_MAC           macs[20];                /* Watched Multi-cast addresses */
    int               amc;                     /* Recieve all multicast packets */
    int               prmsc;                   /* Recieve all packets */
    int               h4000;                   /* Heart beat detection */
    int               rar;
    uint64            ebuf;
    uint64            status;                  /* Status of device. */
    uint32            uver[4];                 /* Version information */
    int               r_pkt;                   /* Packet pending */
    int               poll;                    /* Need to poll receiver */
} nia_data;


extern int32 tmxr_poll;

static CONST ETH_MAC broadcast_ethaddr = {0xff,0xff,0xff,0xff,0xff,0xff};

t_stat         nia_devio(uint32 dev, uint64 *data);
void           nia_start();
void           nia_stop();
void           nia_enable();
void           nia_disable();
void           nia_load_ptt();
void           nia_load_mcast();
void           nia_error(int);
t_stat         nia_eth_srv(UNIT *);
t_stat         nia_rec_srv(UNIT *);
t_stat         nia_cmd_srv(UNIT *);
t_stat         nia_reset (DEVICE *dptr);
t_stat         nia_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         nia_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         nia_attach (UNIT * uptr, CONST char * cptr);
t_stat         nia_detach (UNIT * uptr);
t_stat         nia_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char     *nia_description (DEVICE *dptr);

struct rh_if   nia_rh = { NULL, NULL, NULL};

UNIT nia_unit[] = {
    {UDATA(nia_eth_srv, UNIT_IDLE+UNIT_ATTABLE, 0)},  /* 0 */
    {UDATA(nia_rec_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 1 */
    {UDATA(nia_cmd_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 2 */
};

REG                 nia_reg[] = {
    {SAVEDATA(DATA, nia_data) },
    {0}
};


#define nia_cmd_uptr  (&nia_unit[2])  /* Unit for processing commands */
#define nia_recv_uptr (&nia_unit[0])  /* Unit doing receive digestion */
#define nia_proc_uptr (&nia_unit[1])  /* Unit doing receive dispatching */

DIB nia_dib = {NIA_DEVNUM | RH20_DEV, 1, &nia_devio, NULL, &nia_rh };

MTAB nia_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &nia_set_mac, &nia_show_mac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ETH", NULL, NULL,
      &eth_show, NULL, "Display attachedable devices" },
    { 0 }
    };

/* Simulator debug controls */
DEBTAB              nia_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {"IRQ", DEBUG_IRQ, "Show IRQ requests"},
#define DEBUG_ARP (DEBUG_IRQ<<1)
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

DEVICE nia_dev = {
    "NI", nia_unit, nia_reg, nia_mod,
    3, 8, 0, 1, 8, 36,
    NULL, NULL, &nia_reset, NULL, &nia_attach, &nia_detach,
    &nia_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_ETHER, 0, nia_debug,
    NULL, NULL, &nia_help, NULL, NULL, &nia_description
};

t_stat nia_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &nia_dev;
    UNIT   *uptr = nia_cmd_uptr;

    switch(dev & 03) {
    case CONO:
        if (*data & NIA_CPT)
            nia_reset(dptr);

        nia_data.status &= ~(NIA_SEB|NIA_LAR|NIA_SSC|NIA_DIS|NIA_ENB|NIA_PIA);
        nia_data.status |= *data & (NIA_SEB|NIA_LAR|NIA_SSC|NIA_DIS|NIA_ENB|NIA_PIA);
        nia_data.status &= ~(*data & (NIA_EPE|NIA_FQE|NIA_DME|NIA_RQA));
        clr_interrupt(NIA_DEVNUM);
        if (*data & NIA_MRN) {
           if ((nia_data.status & NIA_MRN) == 0)
              nia_start();
        } else {
           if ((nia_data.status & NIA_MRN) != 0)
              nia_stop();
        }
        if (*data & NIA_ENB) {
           if ((nia_data.status & NIA_MRN) != 0 &&
                   (nia_data.status & NIA_ECP) == 0)
               nia_enable();
           else
               nia_data.status |= NIA_ECP;
        } else
           nia_data.status &= ~NIA_ECP;
        if (*data & NIA_DIS) {
           if ((nia_data.status & NIA_MRN) != 0 &&
                   (nia_data.status & NIA_DCP) == 0)
               nia_disable();
           else
               nia_data.status |= NIA_DCP;
        } else
           nia_data.status &= ~NIA_DCP;
        if (*data & NIA_CQA &&
            (nia_data.status & NIA_CQA) == 0 &&
            (nia_data.status & NIA_MRN) != 0) {
           nia_data.status |= NIA_CQA;
           sim_activate(uptr, 200);
        }
        if (nia_data.status & (NIA_CPE|NIA_RQA))
            set_interrupt(NIA_DEVNUM, nia_data.status & NIA_PIA);
        sim_debug(DEBUG_CONO, dptr, "NIA %03o CONO %06o PC=%06o %012llo\n", dev,
                 (uint32)(*data & RMASK), PC, nia_data.status);
        break;
    case CONI:
        *data = nia_data.status|NIA_PPT|NIA_PID;
        sim_debug(DEBUG_CONI, dptr, "NIA %03o CONI %012llo PC=%o\n", dev,
                           *data, PC);
        break;
    case DATAO:
        if (nia_data.status & NIA_SEB) {
            nia_data.ebuf = *data;
        } else {
            if (*data & NIA_LRA) {
                nia_data.rar = (uint32)((*data & NIA_RAR) >> 22);
                sim_debug(DEBUG_DETAIL, dptr, "NIA %03o set RAR=%o\n",
                 dev, nia_data.rar);
            } else {
                if (nia_data.rar >= 0274 && nia_data.rar <= 0277)
                   nia_data.uver[nia_data.rar - 0274] = (uint32)(*data & RMASK);
                sim_debug(DEBUG_DETAIL, dptr, "NIA %03o set data=%o %06o\n",
                 dev, nia_data.rar, (uint32)(*data & RMASK));
            }
        }
        sim_debug(DEBUG_DATAIO, dptr, "NIA %03o DATO %012llo PC=%o\n",
                 dev, *data, PC);
        break;
    case DATAI:
        if (nia_data.status & NIA_SEB) {
            *data = nia_data.ebuf;
        } else {
            if (nia_data.status & NIA_LAR) {
                *data = ((uint64)nia_data.rar) << 20;
                *data &= ~NIA_MSB;
                *data |= NIA_LRA;
            } else if (nia_data.rar >= 0274 && nia_data.rar <= 0277) {
                   *data = (uint64)nia_data.uver[nia_data.rar - 0274];
            }
        }
        sim_debug(DEBUG_DATAIO, dptr, "NIA %03o DATI %012llo PC=%o\n",
                 dev, *data, PC);
        break;
    }

    return SCPE_OK;
}

static char *
ipv4_inet_ntoa(struct in_addr ip)
{
   static char str[20];

   if (sim_end)
       sprintf (str, "%d.%d.%d.%d", ip.s_addr & 0xFF,
                            (ip.s_addr >> 8) & 0xFF,
                            (ip.s_addr >> 16) & 0xFF,
                            (ip.s_addr >> 24) & 0xFF);
   else
       sprintf (str, "%d.%d.%d.%d", (ip.s_addr >> 24) & 0xFF,
                              (ip.s_addr >> 16) & 0xFF,
                              (ip.s_addr >> 8) & 0xFF,
                               ip.s_addr & 0xFF);
   return str;
}

/*
 * Set error code and stop.
 */
void nia_error(int err)
{
    nia_data.rar = err;
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA error %03o\n", err);
    nia_data.status |= NIA_CPE;
    set_interrupt(NIA_DEVNUM, nia_data.status & NIA_PIA);
    nia_stop();
}

/*
 * Start NIA device, load in 2 words using RH20 mode.
 */
void nia_start()
{
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA start\n");
    /* Set up RH20 to read 2 words */
    nia_rh.stcr = BIT7;
    nia_rh.imode = 2;
    rh20_setup(&nia_rh);
    /* Read PCB address */
    if (!rh_read(&nia_rh)) {
          nia_error(CHNERR);
          return;
    }
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA PCB %012llo %o\n", nia_rh.buf,
                                                              nia_rh.wcr);
    nia_data.pcb = (t_addr)(nia_rh.buf & AMASK);
    nia_data.resp_hdr = (t_addr)((nia_rh.buf + 4) & AMASK);
    nia_data.unk_hdr = (t_addr)((nia_rh.buf + 8) & AMASK);
    /* Read PIA value */
    if (!rh_read(&nia_rh)) {
          nia_error(CHNERR);
          return;
    }
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA PIA %012llo %o\n", nia_rh.buf,
                                                              nia_rh.wcr);
    nia_data.pia = (int)(nia_rh.buf & 7);
    nia_data.status |= NIA_MRN;
    memcpy(&nia_data.macs[0], &nia_data.mac, sizeof (ETH_MAC));
    memcpy(&nia_data.macs[1], &broadcast_ethaddr, sizeof(ETH_MAC));
}

void nia_stop()
{
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA stop\n");
    nia_data.status &= ~NIA_MRN;
}

/*
 * Enable NIA 20.
 *
 * Read in PTT and MACS table.
 */
void nia_enable()
{
    uint64   word;
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA enable\n");
    /* Load pointers to various table */
    if (Mem_read_word(nia_data.unk_hdr + PCB_UPL, &word, 0)) {
        nia_error(EBSERR);
        return;
    }
    nia_data.unk_len = (int)(word & AMASK);
    /* Load PTT */
    if (Mem_read_word(nia_data.pcb + PCB_PTT, &word, 0)) {
        nia_error(EBSERR);
        return;
    }
    nia_data.ptt_addr = (t_addr)(word & AMASK);
    nia_load_ptt();
    /* Load MCT */
    if (Mem_read_word(nia_data.pcb + PCB_MCT, &word, 0)) {
        nia_error(EBSERR);
        return;
    }
    nia_data.mcast_addr = (t_addr)(word & AMASK);
    nia_load_mcast();
    /* Load read count buffer address */
    if (Mem_read_word(nia_data.pcb + PCB_RCB, &word, 0)) {
        nia_error(EBSERR);
        return;
    }
    nia_data.rcb = (t_addr)(word & AMASK);
    nia_data.status |= NIA_ECP;
    nia_data.status &= ~NIA_DCP;
}

/*
 * Disable NIA 20.
 */
void nia_disable()
{
    nia_data.status |= NIA_DCP;
    nia_data.status &= ~NIA_ECP;
}

/*
 * Copy a MAC address from string to memory word.
 */
void nia_cpy_mac(uint64 word1, uint64 word2, ETH_MAC *mac)
{
    ETH_MAC  m;
    m[0] = (unsigned char)((word1 >> 28) & 0xff);
    m[1] = (unsigned char)((word1 >> 20) & 0xff);
    m[2] = (unsigned char)((word1 >> 12) & 0xff);
    m[3] = (unsigned char)((word1 >> 4) & 0xff);
    m[4] = (unsigned char)((word2 >> 28) & 0xff);
    m[5] = (unsigned char)((word2 >> 20) & 0xff);
    memcpy(mac, &m, sizeof(ETH_MAC));
}

/*
 * Copy memory to a packet.
 */
uint8 *nia_cpy_to(t_addr addr, uint8 *data, int len)
{
    uint64    word;
    /* Copy full words */
    while (len > 3) {
        word = M[addr++];
        *data++ = (uint8)((word >> 28) & 0xff);
        *data++ = (uint8)((word >> 20) & 0xff);
        *data++ = (uint8)((word >> 12) & 0xff);
        *data++ = (uint8)((word >> 4) & 0xff);
         len -= 4;
    }
    /* Grab last partial word */
    if (len) {
        word = M[addr++];
        switch (len) {
        case 3:
                *data++ = (uint8)((word >> 28) & 0xff);
                *data++ = (uint8)((word >> 20) & 0xff);
                *data++ = (uint8)((word >> 12) & 0xff);
                break;
        case 2:
                *data++ = (uint8)((word >> 28) & 0xff);
                *data++ = (uint8)((word >> 20) & 0xff);
                break;
        case 1:
                *data++ = (uint8)((word >> 28) & 0xff);
                break;
        }
    }
    return data;
}

/*
 * Copy a packet to memory.
 */
uint8 *nia_cpy_from(t_addr addr, uint8 *data, int len)
{
    uint64    word;

    /* Copy full words */
    while (len > 3) {
        word =  (uint64)(*data++) << 28;
        word |= (uint64)(*data++) << 20;
        word |= (uint64)(*data++) << 12;
        word |= (uint64)(*data++) << 4;
        M[addr++] = word;
        len -= 4;
    }
    /* Copy last partial word */
    if (len) {
        switch (len) {
        case 3:
                word =  (uint64)(*data++) << 28;
                word |= (uint64)(*data++) << 20;
                word |= (uint64)(*data++) << 12;
                break;
        case 2:
                word =  (uint64)(*data++) << 28;
                word |= (uint64)(*data++) << 20;
                break;
        case 1:
                word =  (uint64)(*data++) << 28;
                break;
        default:
                word = 0;
                break;
        }
        M[addr++] = word;
    }
    return data;
}

/*
 * Get next entry off a queue.
 * Return entry in *entry and 1 if successfull.
 * Returns 0 if fail, queue locked or memory out of bounds.
 */
int nia_getq(t_addr head, t_addr *entry)
{
    uint64    temp;
    t_addr    flink;
    t_addr    nlink;
    *entry = 0;  /* For safty */

    /* Try and get lock */
    if (Mem_read_word(head, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    /* Check if entry locked */
    if ((temp & SMASK) == 0)
        return 0;

    /* Increment lock here */

    /* Get head of queue */
    if (Mem_read_word(head + 1, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    flink = (t_addr)(temp & AMASK);
    /* Check if queue empty */
    if (flink == (head+1)) {
        sim_debug(DEBUG_DETAIL, &nia_dev, "NIA empty %08o\n", head);
        /* Decrement lock here */
        return 1;
    }
    /* Get link to next entry */
    if (Mem_read_word(flink + 1, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    nlink = (t_addr)(temp & AMASK);
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA head: q=%08o f=%08o n=%08o\n",
             head, flink, nlink);
    /* Set Head Flink to point to next */
    temp = (uint64)nlink;
    if (Mem_write_word(head+1, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    /* Set Next Blink to head */
    temp = (uint64)(head + 1);
    if (Mem_write_word(nlink+1, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    /* Return entry */
    *entry = flink;

    /* Decrement lock here */
    return 1;
}


/*
 * Put entry on head of queue.
 * Return entry in *entry and 1 if successfull.
 * Returns 0 if fail, queue locked or memory out of bounds.
 */
int nia_putq(t_addr head, t_addr *entry)
{
    uint64    temp;
    t_addr    blink;

    /* Try and get lock */
    if (Mem_read_word(head, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    /* Check if entry locked */
    if ((temp & SMASK) == 0)
        return 0;

    /* Increment lock here */

    /* Hook entry into tail of queue */
    if (Mem_read_word(head + 2, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }

    blink = (t_addr)(temp & AMASK);  /* Get back link */
    /* Get link to previous entry */
    temp = (uint64)*entry;
    if (Mem_write_word(blink, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    /* Old forward is new */
    if (Mem_write_word(head+2, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }

    /* Flink is head of queue */
    temp = (uint64)(head+1);
    if (Mem_write_word(*entry, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }

    /* Back link points to previous */
    temp = (uint64)blink;
    if (Mem_write_word(*entry+1, &temp, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA put: q=%08o i=%08o b=%08o\n",
                head, *entry, blink);
    *entry = 0;
    /* Decement lock here */

    /* Check if Queue was empty, and response queue */
    if (blink == (head+1) && head == nia_data.resp_hdr) {
        nia_data.status |= NIA_RQA;
        set_interrupt(NIA_DEVNUM, nia_data.pia);
        sim_debug(DEBUG_DETAIL, &nia_dev, "NIA set response\n");
    }
    return 1;
}


/*
 * Load in the protocol type table
 */
void nia_load_ptt()
{
    int      i;
    int      n = 0;
    t_addr   addr = nia_data.ptt_addr;

    for (i = 0; i < 17; i++) {
        uint64     word1, word2;

        /* Get entry */
        if (Mem_read_word(addr++, &word1, 0)) {
            nia_error(EBSERR);
            return;
        }
        if (Mem_read_word(addr++, &word2, 0)) {
            nia_error(EBSERR);
            return;
        }
        sim_debug(DEBUG_DETAIL, &nia_dev, "NIA load ptt%d: %012llo %012llo\n\r",
              n,  word1, word2);
        if (word1 & SMASK) {
           uint16 type;
           type = (uint16)(word1 >> 12) & 0xff;
           type |= (uint16)(word1 << 4) & 0xff00;
           nia_data.ptt_proto[n] = type;
           nia_data.ptt_head[n] = (t_addr)(word2 &AMASK) - 1;
           n++;
        }
        addr++;
    }
    for (i = 0; i < n; i++)
       sim_debug(DEBUG_DETAIL, &nia_dev, "NIA load ptt%d: %04x %010o\n\r",
              n,  nia_data.ptt_proto[i], nia_data.ptt_head[i]);
    nia_data.ptt_n = n;
}

/*
 * Load in the multi-cast table
 */
void nia_load_mcast()
{
    int      i;
    int      n = 0;
    char     buffer[20];
    t_addr   addr = nia_data.mcast_addr;

    /* Start with our own address. */
    memcpy(&nia_data.macs[n], &nia_data.mac, sizeof (ETH_MAC));
    n++;
    memcpy(&nia_data.macs[n], &broadcast_ethaddr, sizeof (ETH_MAC));
    n++;
    for (i = 0; i < 17; i++) {
        uint64 word1, word2;

        if (Mem_read_word(addr++, &word1, 0)) {
            nia_error(EBSERR);
            return;
        }
        if (Mem_read_word(addr++, &word2, 0)) {
            nia_error(EBSERR);
            return;
        }
        if (word2 & 1) {
            nia_cpy_mac(word1, word2, &nia_data.macs[n]);
            n++;
        }
     }
     for(i = 0; i< n; i++) {
         eth_mac_fmt(&nia_data.macs[i], buffer);
         sim_debug(DEBUG_DETAIL, &nia_dev, "NIA load mcast%d: %s\n\r",i,buffer);
     }
     nia_data.macs_n = n - 2;
     if (nia_recv_uptr->flags & UNIT_ATT)
         eth_filter (&nia_data.etherface, n, nia_data.macs, nia_data.amc,
                     nia_data.prmsc);
}

/*
 * Pretty print a packet for debugging.
 */
void nia_packet_debug(struct nia_device *nia, const char *action,
     ETH_PACK *packet) {
    struct nia_eth_hdr *eth = (struct nia_eth_hdr *)&packet->msg[0];
    struct arp_hdr     *arp = (struct arp_hdr *)eth;
    struct ip          *ip = (struct ip *)&packet->msg[sizeof(struct nia_eth_hdr)];
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

        if (!(nia_dev.dctrl & DEBUG_ARP))
            return;
        eth_mac_fmt(&arp->ethhdr.src, eth_src);
        eth_mac_fmt(&arp->ethhdr.dest, eth_dst);
        eth_mac_fmt(&arp->shwaddr, arp_shwaddr);
        memcpy(&in_addr, &arp->sipaddr, sizeof(in_addr));
        strlcpy(arp_sipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_sipaddr));
        eth_mac_fmt(&arp->dhwaddr, arp_dhwaddr);
        memcpy(&in_addr, &arp->dipaddr, sizeof(in_addr));
        strlcpy(arp_dipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_dipaddr));
        sim_debug(DEBUG_ARP, &nia_dev,
            "%s %s EthDst=%s EthSrc=%s shwaddr=%s sipaddr=%s dhwaddr=%s dipaddr=%s\n",
            action, arp_op, eth_dst, eth_src, arp_shwaddr, arp_sipaddr, arp_dhwaddr, arp_dipaddr);
        return;
    }
    if (ntohs(eth->type) != ETHTYPE_IP) {
        payload = (uint8 *)&packet->msg[sizeof(struct nia_eth_hdr)];
        len = packet->len - sizeof(struct nia_eth_hdr);
        sim_data_trace(&nia_dev, nia_unit, payload, "", len, "", DEBUG_DATA);
        return;
    }
    if (!(nia_dev.dctrl & (DEBUG_TCP|DEBUG_UDP|DEBUG_ICMP)))
        return;
    memcpy(&ipaddr, &ip->ip_src, sizeof(ipaddr));
    strlcpy(src_ip, ipv4_inet_ntoa(ipaddr), sizeof(src_ip));
    memcpy(&ipaddr, &ip->ip_dst, sizeof(ipaddr));
    strlcpy(dst_ip, ipv4_inet_ntoa(ipaddr), sizeof(dst_ip));
    payload = (uint8 *)&packet->msg[sizeof(struct nia_eth_hdr) + (ip->ip_v_hl & 0xf) * 4];
    switch (ip->ip_p) {
        case UDP_PROTO:
            udp = (struct udp *)payload;
            snprintf(src_port, sizeof(src_port), "%d", ntohs(udp->udp_sport));
            snprintf(dst_port, sizeof(dst_port), "%d", ntohs(udp->udp_dport));
            sim_debug(DEBUG_UDP, &nia_dev, "%s %d byte packet from %s:%s to %s:%s\n", action,
                ntohs(udp->len), src_ip, src_port, dst_ip, dst_port);
                if (udp->len && (nia_dev.dctrl & DEBUG_UDP))
                    sim_data_trace(&nia_dev, nia_unit, payload + sizeof(struct udp), "",
                                                       ntohs(udp->len), "", DEBUG_DATA);
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
            sim_debug(DEBUG_TCP, &nia_dev, "%s %s%s %d byte packet from %s:%s to %s:%s\n", action,
                        flags, *flags ? ":" : "", (int)len, src_ip, src_port, dst_ip, dst_port);
            if (len && (nia_dev.dctrl & DEBUG_TCP))
                sim_data_trace(&nia_dev, nia_unit, payload + 4 * (ntohs(tcp->flags) >> 12), "", len, "", DEBUG_DATA);
            break;
        case ICMP_PROTO:
            icmp = (struct icmp *)payload;
            len = ntohs(ip->ip_len) - (ip->ip_v_hl & 0xf) * 4;
            sim_debug(DEBUG_ICMP, &nia_dev, "%s %s %d byte packet from %s to %s\n", action,
                (icmp->type < sizeof(icmp_types)/sizeof(icmp_types[0])) ? icmp_types[icmp->type] : "", (int)len, src_ip, dst_ip);
            if (len && (nia_dev.dctrl & DEBUG_ICMP))
                sim_data_trace(&nia_dev, nia_unit, payload + sizeof(struct icmp), "", len, "", DEBUG_DATA);
            break;
    }
}


/*
 * Send out a packet.
 */
int nia_send_pkt(uint64 cmd)
{
    uint64    word1, word2;
    struct    nia_eth_hdr  *hdr = (struct nia_eth_hdr *)(&nia_data.snd_buff.msg[0]);
    uint8     *data = &nia_data.snd_buff.msg[sizeof(struct nia_eth_hdr)];
    ETH_MAC   dest;
    uint16    type;
    int       len;
    int       blen;

    /* Read packet length */
    if (Mem_read_word(nia_data.cmd_entry + 4, &word1, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    len = (int)(word1 & 0177777);
    blen = len + sizeof(struct nia_eth_hdr);
    /* Check for runt packet */
    if (blen < ETH_MIN_PACKET && (cmd & (NIA_FLG_PAD << 8)) == 0) {
        return NIA_ERR_RUN;
    }
    /* Check for too long of a packet */
    if (blen > ETH_MAX_PACKET) {
        nia_data.pcnt[NIA_CNT_XF]++;
        nia_data.pcnt[NIA_CNT_XFM] |= NIA_XFM_XFL;
        return NIA_ERR_LNG;
    }
    /* If unit not attached, nothing more we can do */
    if ((nia_recv_uptr->flags & UNIT_ATT) == 0)
        return 0;
    /* Read protocol type */
    if (Mem_read_word(nia_data.cmd_entry + 5, &word1, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    type = (uint16)(word1 >> 12) & 0xff;
    type |= (uint16)(word1 << 4) & 0xff00;
    hdr->type = htons(type);

    /* Load destination address */
    if (Mem_read_word(nia_data.cmd_entry + 7, &word1, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    if (Mem_read_word(nia_data.cmd_entry + 8, &word2, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    nia_cpy_mac(word1, word2, &dest);
    memcpy(hdr->dest, dest, sizeof(ETH_MAC));
    /* Copy our address over */
    memcpy(hdr->src, nia_data.mac, sizeof(ETH_MAC));
    /* Set packet length */
    nia_data.snd_buff.len = len + sizeof(struct nia_eth_hdr);
    /* Preappend length if asking for pad */
    if ((cmd & (NIA_FLG_PAD << 8)) != 0) {
        *data++ = len & 0377;
        *data++ = (len >> 8) & 0377;
        nia_data.snd_buff.len += 2;
    }
    /* Copy over rest of packet */
    if (cmd & (NIA_FLG_BSD << 8)) {
        if (Mem_read_word(nia_data.cmd_entry + 9, &word1, 0)) {
            nia_error(EBSERR);
            return 0;
        }
        while (len > 0) {
            uint64  tlen;
            /* Read pointer to buffer */
            if (Mem_read_word((t_addr)(word1 & AMASK), &word2, 0)) {
                nia_error(EBSERR);
                return 0;
            }
            /* Read length */
            if (Mem_read_word((t_addr)((word1+2) & AMASK), &tlen, 0)) {
                nia_error(EBSERR);
                return 0;
            }
            blen = (int)(tlen & 0177777);
            data = nia_cpy_to((t_addr)(word2 & AMASK), data, blen);
            len -= blen;
            if (Mem_read_word((t_addr)((word1 + 1) & AMASK), &word1, 0)) {
                nia_error(EBSERR);
                return 0;
            }
        }
    } else {
        data = nia_cpy_to(nia_data.cmd_entry + 9, data, len);
    }
    if (((cmd & (NIA_FLG_PAD << 8)) != 0) &&
               nia_data.snd_buff.len < ETH_MIN_PACKET) {
        while (nia_data.snd_buff.len < ETH_MIN_PACKET) {
           *data++ = 0;
           nia_data.snd_buff.len++;
        }
    }
    nia_packet_debug(&nia_data, "send", &nia_data.snd_buff);
    if (eth_write(&nia_data.etherface, &nia_data.snd_buff, NULL) != SCPE_OK) {
        nia_data.pcnt[NIA_CNT_XF]++;
        nia_data.pcnt[NIA_CNT_XFM] |= NIA_XFM_LOC;
    }
    nia_data.pcnt[NIA_CNT_BX] += nia_data.snd_buff.len;
    nia_data.pcnt[NIA_CNT_FX] ++;
    return 0;
}

/*
 * Process commands.
 */
t_stat nia_cmd_srv(UNIT * uptr)
{
    uint64    word1, word2;
    uint32    cmd;
    int       len, i;
    int       err;

    /* See if we have command that we could not respond too */
    if (nia_data.cmd_entry != 0) {
       /* Have to put this either on response queue or free queue */
       if (nia_putq(nia_data.cmd_rply, &nia_data.cmd_entry) == 0){
           sim_activate(uptr, 200); /* Reschedule ourselves to deal with it */
           return SCPE_OK;
       }
       nia_data.cmd_rply = 0;
    }

    /* Check if we are running */
    if ((nia_data.status & NIA_MRN) == 0 || (nia_data.status & NIA_CQA) == 0) {
        return SCPE_OK;
    }

    /* or no commands pending, just idle out */
    /* Try to get command off queue */
    if (nia_getq(nia_data.cmd_hdr, &nia_data.cmd_entry) == 0) {
       sim_activate(uptr, 200); /* Reschedule ourselves to deal with it */
       return SCPE_OK;
    }

    /* Check if we got one */
    if (nia_data.cmd_entry == 0) {
       /* Nothing to do */
       nia_data.status &= ~NIA_CQA;
       return SCPE_OK;
    }

    /* Get command */
    if (Mem_read_word(nia_data.cmd_entry + 3, &word1, 0)) {
        nia_error(EBSERR);
        return SCPE_OK;
    }
    cmd = (uint32)(word1 >> 12);
    /* Save initial status */
    nia_data.cmd_status = ((uint8)(cmd >> 16)) & 0xff;
    sim_debug(DEBUG_DETAIL, &nia_dev, "NIA cmd: %08x\n", cmd);
    cmd &= 0xffff;
    len = 5;
    /* Preform function of command */
    switch(cmd & 0xff) {
    case NIA_CMD_SND:  /* Send a datagram */
         err = nia_send_pkt(cmd);
         if (err != 0)
            cmd |= ((err<<1)|1) << 16;
         cmd |= NIA_STS_SR << 16;
         len = 10;
         break;
    case NIA_CMD_LPTT: /* Load Protocol Type table */
         nia_load_ptt();
         break;
    case NIA_CMD_LMAC: /* Load Multicast table */
         nia_load_mcast();
         break;
    case NIA_CMD_RCNT: /* Read counts */
         for (i = 0; i < NIA_CNT_LEN; i++) {
             word1 = nia_data.pcnt[i];
             if (Mem_write_word(nia_data.cnt_addr + i, &word1, 0)) {
                 nia_error(EBSERR);
                 return SCPE_OK;
             }
             if ((cmd & (NIA_FLG_CLRC << 20)) != 0)
                nia_data.pcnt[i] = 0;
         }
         break;
    case NIA_CMD_WPLI: /* Write PLI */
         break;
    case NIA_CMD_RPLI: /* Read PLI */
         break;
    case NIA_CMD_RNSA: /* Read Station Address */
         len = 8;
         word1 = ((uint64)nia_data.mac[0]) << 28;
         word1 |= ((uint64)nia_data.mac[1]) << 20;
         word1 |= ((uint64)nia_data.mac[2]) << 12;
         word1 |= ((uint64)nia_data.mac[3]) << 4;
         word2 = ((uint64)nia_data.mac[4]) << 28;
         word2 |= ((uint64)nia_data.mac[5]) << 20;
         if (Mem_write_word(nia_data.cmd_entry + 4, &word1, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         if (Mem_write_word(nia_data.cmd_entry + 5, &word2, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         word1 = (uint64)((nia_data.amc << 2)| (nia_data.h4000 << 1)
                                             | nia_data.prmsc);
                                     /* Micro code version, PTT len MACS len */
         word2 = (nia_data.uver[3] << 12) |(0xF << 6)|0xF;
         if (Mem_write_word(nia_data.cmd_entry + 6, &word1, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         if (Mem_write_word(nia_data.cmd_entry + 7, &word2, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         break;
    case NIA_CMD_WNSA: /* Write Station Address */
         len = 8;
         if (Mem_read_word(nia_data.cmd_entry + 4, &word1, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         if (Mem_read_word(nia_data.cmd_entry + 5, &word2, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         nia_cpy_mac(word1, word2, &nia_data.mac);
         if (Mem_read_word(nia_data.cmd_entry + 6, &word1, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         if (Mem_read_word(nia_data.cmd_entry + 7, &word2, 0)) {
             nia_error(EBSERR);
             return SCPE_OK;
         }
         nia_data.prmsc = (int)(word1 & 1);
         nia_data.h4000 = (int)((word1 & 2) != 0);
         nia_data.amc = (int)((word1 & 4) != 0);
         memcpy(&nia_data.macs[0], &nia_data.mac, sizeof (ETH_MAC));
         if (nia_recv_uptr->flags & UNIT_ATT)
             eth_filter (&nia_data.etherface, nia_data.macs_n + 2,
                         nia_data.macs, 0, 0);
         break;

    case NIA_CMD_RCV:  /* Received datagram */
    default:           /* Invalid command */
         cmd |= ((NIA_ERR_UNK<<1)|1) << 16;
         break;
    }

    nia_data.cmd_rply = nia_data.unk_hdr;
    word1 = ((uint64)cmd) << 12;
    if (Mem_write_word(nia_data.cmd_entry + 3, &word1, 0)) {
        nia_error(EBSERR);
        return SCPE_OK;
    }
    if (((cmd >> 16) & 1) != 0 || (cmd & (NIA_FLG_RESP << 8)) != 0) {
       nia_data.cmd_rply = nia_data.resp_hdr;
    } else if ((cmd & 0xff) == NIA_CMD_SND) {
       if (Mem_read_word(nia_data.cmd_entry + 5, &word1, 0)) {
           nia_error(EBSERR);
           return SCPE_OK;
       }
       nia_data.cmd_rply = (t_addr)(word1 & AMASK);
    }
    for(i = 0; i < len; i++)
        sim_debug(DEBUG_DETAIL, &nia_dev, "NIA rcmd: %d %09llx %012llo\n",
                i, M[nia_data.cmd_entry + i], M[nia_data.cmd_entry + i]);
    (void)nia_putq(nia_data.cmd_rply, &nia_data.cmd_entry);
    sim_activate(uptr, 500);
    return SCPE_OK;
}


int
nia_rec_pkt()
{
    struct nia_eth_hdr  *hdr;
    uint16              type;
    int                 i;
    int                 len;
    t_addr              queue;
    t_addr              bsd;
    uint64              word;
    uint8               *data;

    /* See if we have received packet to process */
    if (nia_data.rec_entry != 0) {
       /* Have to put this response queue */
       if (nia_putq(nia_data.resp_hdr, &nia_data.rec_entry) == 0)
           return 0;
    }

    /* If no pending packet, return success */
    if (nia_data.r_pkt == 0)
       return 1;

    /* Determine which queue to get free packet from */
    hdr = (struct nia_eth_hdr *)(&nia_data.rec_buff.msg[0]);
    type = ntohs(hdr->type);

    queue = nia_data.unk_hdr;
    for (i = 0; i < nia_data.ptt_n; i++) {
        if (nia_data.ptt_proto[i] == type) {
            queue = nia_data.ptt_head[i];
            break;
        }
    }

    /* Try to grab place to save packet */
    if (nia_getq(queue, &nia_data.rec_entry) == 0)
       return 0;   /* Indicate packet not processed */

    /* If we queue empty, just drop packet */
    if (nia_data.rec_entry == 0) {
       sim_debug(DEBUG_DETAIL, &nia_dev, "NIA drop packet\n");
       nia_data.r_pkt = 0;  /* Drop packet it queue empty */
       if (queue == nia_data.unk_hdr)
           nia_data.pcnt[NIA_CNT_DUN]++;
       else
           nia_data.pcnt[NIA_CNT_D01 + i]++;
       nia_data.pcnt[NIA_CNT_UBU] += nia_data.rec_buff.len;
       nia_data.status |= NIA_FQE;
       set_interrupt(NIA_DEVNUM, nia_data.status & NIA_PIA);
       return 1;  /* We did what we could with it. */
    }

    /* Get some information about packet */
    len = nia_data.rec_buff.len - sizeof(struct nia_eth_hdr);
    data = &nia_data.rec_buff.msg[sizeof(struct nia_eth_hdr)];

    /* Got one, now fill in data */
    word = (uint64)(NIA_CMD_RCV << 12);
    if (Mem_write_word(nia_data.rec_entry + 3, &word, 0)) {
        nia_error(EBSERR);
        return SCPE_OK;
    }
    word = (uint64)len;
    if (Mem_write_word(nia_data.rec_entry + 4, &word, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    (void)nia_cpy_from(nia_data.rec_entry + 5,
                         (uint8 *)&hdr->dest, sizeof(ETH_MAC));
    (void)nia_cpy_from(nia_data.rec_entry + 7,
                         (uint8 *)&hdr->src, sizeof(ETH_MAC));
    word = (uint64)(((type & 0xff00) >> 4) |
                           ((type & 0xff) << 12));
    if (Mem_write_word(nia_data.rec_entry + 9, &word, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    if (Mem_read_word(nia_data.rec_entry + 10, &word, 0)) {
        nia_error(EBSERR);
        return 0;
    }
    bsd = (t_addr)(word & AMASK);
    while (len > 0) {
        int  blen;
        /* Get length of segment */
        if (Mem_read_word(bsd+2, &word, 0)) {
            nia_error(EBSERR);
            return 0;
        }
        blen = (int)(word & 0177777);
        if (blen > len)
           blen = len;
        /* Get address of where to put data */
        if (Mem_read_word(bsd, &word, 0)) {
            nia_error(EBSERR);
            return 0;
        }
        data = nia_cpy_from((t_addr)(word & AMASK), data, blen);
        len -= blen;
        /* Get pointer to next segment */
        if (Mem_read_word(bsd+1, &word, 0)) {
            nia_error(EBSERR);
            return 0;
        }
        bsd = (t_addr)(word & AMASK);
    }

    for(i = 0; i < 10; i++)
         sim_debug(DEBUG_DETAIL, &nia_dev, "NIA recv: %d %09llx %012llo\n",
                 i, M[nia_data.rec_entry + i], M[nia_data.rec_entry + i]);
    /* All done with packet */
    nia_data.r_pkt = 0;
    /* Put on response queue */
    return nia_putq(nia_data.resp_hdr, &nia_data.rec_entry);
}

/*
 * Receive ether net packets.
 */
t_stat nia_eth_srv(UNIT * uptr)
{
    struct nia_eth_hdr  *hdr;
    uint16              type;

    if (nia_data.poll)
        sim_clock_coschedule(uptr, 1000);             /* continue poll */
    /* Check if we need to get a packet */
    while (nia_data.r_pkt == 0) {
        if (eth_read(&nia_data.etherface, &nia_data.rec_buff, NULL) <= 0)
            return SCPE_OK;

        nia_packet_debug(&nia_data, "recv", &nia_data.rec_buff);
        hdr = (struct nia_eth_hdr *)(&nia_data.rec_buff.msg[0]);
        type = ntohs(hdr->type);
        /* Check if we are running */
        if ((nia_data.status & NIA_MRN) == 0) {
            sim_debug(DEBUG_DETAIL, &nia_dev,
                "NIA read packet - not running: %d %04x\n",
                 nia_data.rec_buff.len, type);
            return SCPE_OK;
        }

        sim_debug(DEBUG_DETAIL, &nia_dev, "NIA read packet: %d %04x\n",
                nia_data.rec_buff.len, type);
        nia_data.r_pkt = 1;   /* Mark packet buffer full */
        nia_data.pcnt[NIA_CNT_BR] += nia_data.rec_buff.len;
        nia_data.pcnt[NIA_CNT_FR] ++;
        if (hdr->dest[0] & 1) {
            nia_data.pcnt[NIA_CNT_MCB] += nia_data.rec_buff.len;
            nia_data.pcnt[NIA_CNT_MCF] ++;
        }

        /* Try to process the packet */
        if (nia_rec_pkt() == 0) {
            sim_activate(nia_proc_uptr, 100);
            return SCPE_OK;
        }
    }
    return SCPE_OK;
}

/*
 * Handle delayed packets.
 */
t_stat nia_rec_srv(UNIT * uptr)
{

    /* Process what we have */
    if (nia_rec_pkt() == 0) {
        sim_activate(uptr, 100);
        return SCPE_OK;
    }
    /* See if we can pick up any more packets */
    return nia_eth_srv(nia_recv_uptr);
}



t_stat nia_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];
    eth_mac_fmt(&nia_data.mac, buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat nia_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    status = eth_mac_scan_ex(&nia_data.mac, cptr, uptr);
    if (status != SCPE_OK)
      return status;

    return SCPE_OK;
}

t_stat nia_reset (DEVICE *dptr)
{
    int  i;

    for (i = 0; i < 6; i++) {
        if (nia_data.mac[i] != 0)
            break;
    }
    if (i == 6) {   /* First call to reset? */
   /* Set a default MAC address in a BBN assigned OID range no longer in use */
        nia_set_mac (dptr->units, 0, "00:00:02:00:00:00/24", NULL);
    }
    return SCPE_OK;
}

/* attach device: */
t_stat nia_attach(UNIT* uptr, CONST char* cptr)
{
    t_stat status;
    char* tptr;
    char buf[32];

    tptr = (char *) malloc(strlen(cptr) + 1);
    if (tptr == NULL) return SCPE_MEM;
    strcpy(tptr, cptr);

    memcpy(&nia_data.macs[0], &nia_data.mac, sizeof (ETH_MAC));
    memcpy(&nia_data.macs[1], &broadcast_ethaddr, 6);
    status = eth_open(&nia_data.etherface, cptr, &nia_dev, DEBUG_ETHER);
    if (status != SCPE_OK) {
        free(tptr);
        return status;
    }
    eth_mac_fmt(&nia_data.mac, buf);     /* format ethernet mac address */
    if (SCPE_OK != eth_check_address_conflict (&nia_data.etherface,
                                                 &nia_data.mac)) {
        eth_close(&nia_data.etherface);
        free(tptr);
        return sim_messagef (SCPE_NOATT,
                  "%s: MAC Address Conflict on LAN for address %s\n",
                      nia_dev.name, buf);
    }
    if (SCPE_OK != eth_filter(&nia_data.etherface, 2, nia_data.macs, 0, 0)) {
        eth_close(&nia_data.etherface);
        free(tptr);
        return sim_messagef (SCPE_NOATT,
                "%s: Can't set packet filter for MAC Address %s\n",
                       nia_dev.name, buf);
    }

    uptr->filename = tptr;
    uptr->flags |= UNIT_ATT;
    eth_setcrc(&nia_data.etherface, 1);     /* Enable CRC */

    /* init read queue (first time only) */
    status = ethq_init(&nia_data.ReadQ, 8);
    if (status != SCPE_OK) {
        eth_close(&nia_data.etherface);
        uptr->filename = NULL;
        free(tptr);
        return sim_messagef (status, "%s: Can't initialize receive queue\n",
                             nia_dev.name);
    }


    /* Allow Asynchronous inbound packets */
    if (eth_set_async (&nia_data.etherface, 0) == SCPE_OK)
        nia_data.poll = 0;
    else {
        nia_data.poll = 1;
        sim_activate (&nia_unit[0], 100);
    }
    return SCPE_OK;
}

/* detach device: */

t_stat nia_detach(UNIT* uptr)
{

    if (uptr->flags & UNIT_ATT) {
        sim_cancel(&nia_unit[1]);
        sim_cancel(&nia_unit[2]);
        eth_close (&nia_data.etherface);
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
    }
    return SCPE_OK;
}

t_stat nia_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "NIA interface\n\n");
fprintf (st, "The NIA interfaces to the network. Setting MAC defines default MAC address\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
eth_attach_help(st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *nia_description (DEVICE *dptr)
{
    return "KL NIA interface";
}
#endif

