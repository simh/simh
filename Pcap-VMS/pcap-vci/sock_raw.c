#include <ctype.h>                        /* Character type classification macros/routines */ 
#include <descrip.h>                      /* For VMS descriptor manipulation */ 
#include <iodef.h>                        /* I/O function code definitions */ 
#include <ssdef.h>                        /* System service return status code definitions */ 
#include <starlet.h>                      /* System library routine prototypes */ 
#include <stdio.h>                        /* ANSI C Standard Input/Output */ 
#include <stdlib.h>                       /* General utilities */ 
#include <string.h>                       /* String handling */ 
#include <stsdef.h>                       /* VMS status code definitions */ 
#include <unistd.h>
#include <ioctl.h>
#include <socket.h>
#include <in.h>
#include <if.h>

int main(void)
{
    int status;
    int one = 1;
    char buf[2048];    
    int cc;
    struct sockaddr_in rsock;
    int fd;
    struct ifreq ifr;
    int len;
	
    fd = socket(AF_DLI, SOCK_RAW, IPPROTO_RAW);

    status = setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));

    strncpy(ifr.ifr_name, "WE0", sizeof(ifr.ifr_name));
    status = ioctl(fd, SIOCGIFFLAGS, &ifr);
    ifr.ifr_ifru.ifru_flags |= IFF_PROMISC;
    status = ioctl(fd, SIOCSIFFLAGS, &ifr);

    memset(&rsock, 0, sizeof(rsock));
    rsock.sin_family = AF_INET;
    rsock.sin_port = 0;
    rsock.sin_addr.s_addr = INADDR_ANY;
//    strncpy(rsock.sa_data, "WE0", sizeof(rsock.sa_data));
    status = bind(fd, &rsock, sizeof(rsock));
    
    len = sizeof(rsock);
    cc = recvfrom(fd, buf, 1500,0, &rsock, &len);

    return 1;
}
    
