#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "pcap.h"
#include "pcap-int.h"

static int fd;

void dump_packet(int fd, int len, unsigned char *data)
{
    int idx;
    char buf[128];
    char packhead[256];
    char nl[] = "\n";

    sprintf(packhead, "\nPacket length %d", len);
    write (fd, packhead,strlen(packhead));
    for (idx=0; idx<len; idx++) {
	if (!(idx % 40)) {
	    write(fd, nl, 1);
	}
	sprintf (buf, "%02x", data[idx]);
	write (fd, buf, strlen(buf));
    }
}

void read_callback(char *info, struct pcap_pkthdr *hdr, unsigned char *data)
{

    int me;
    unsigned char meaddr[6] = {0xaa,0x00,0x04,0x00,0x37,0x4c};
    packet *pkt;

    pkt = (packet *) data;
    me = memcmp(data, meaddr, 6);
    if (me == 0) {
	dump_packet(fd, hdr->len, data);

	printf("Received packet, len: %d\n", hdr->len);
	printf("From %02x-%02x-%02x-%02x-%02x-%02x\n", data[0],data[1],
	    data[2],data[3],data[4],data[5]);
	printf("To %02x-%02x-%02x-%02x-%02x-%02x\n",data[6],data[7],
	    data[8],data[9],data[10],data[11]);
    }
}
int main(void)
{
    int status;
    pcap_t *pcap_handle;
    struct pcap_pkthdr hdr;
    char *packet;
    char ebuff[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;
    bpf_u_int32 maskp;
    bpf_u_int32 netp;
    int bufsiz = 2048;
    char *dev;

    dev = pcap_lookupdev(ebuff);

    pcap_handle = pcap_open_live(dev, 2048, 1, 5000, ebuff);
    fd = open ("packet.dmp", O_RDWR|O_CREAT);

    status = pcap_lookupnet(dev, &netp, &maskp, ebuff);
    status = pcap_compile(pcap_handle, &fp, "port 23",0, 
	netp);
//    status = pcap_setfilter(pcap_handle, &fp);
    status = pcap_loop(pcap_handle, 2000, &read_callback, 0);
    close(fd);
    return 1;
}
