#define  INTERFACE "we0"	/* The interface to read from */
#define  PRINT_HDR 0	/* 0 if you don't wanna print the ip header */
#define  D_FILTER 1     /* 1=on 0=off, (filter for doubled packets on
                           the loopback) */

/* They (below) are lots of headers we won't need now, but I included them
 * anyway
 */
#include <sys/types.h>
#include <tcpip$examples/in.h>
#include <tcpip$examples/netdb.h>
#include <string.h>
#include <tcpip$examples/inet.h>
#include <tcpip$examples/socket.h>
#include <tcpip$examples/in.h>
#include <tcpip$examples/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include <tcpip$examples/tcp.h>
//#include "ip.h"

//#include <tcpip$examples/if_ether.h>


/* arg1: the ip in net byte order
 * return: the ip address in dot number form
 */
char *get_ip( unsigned long int net_byte_order )
{
   struct in_addr bytestr;

   static char dot_ip[50];

   bytestr.s_addr = net_byte_order;
   strncpy(dot_ip, inet_ntoa(bytestr), 50);
   return(dot_ip);
}


main()
{
   struct ifreq ifr;
   int sock;
   char buf[1596];
   struct iphdr  *ip;	    /* Pointer to the ip header */
   struct tcphdr *tcp;	    /* Pointer to the tcp header */

/*********** PART 1 ***********/

   /* Create the socket, we need a socket of type SOCK_PACKET in order
    * to read given frames from an interface
    */
   if( (sock = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ALL)))== -1)
   {
      printf("Can't create socket\n");
      exit(0);
   }

   printf("ifr.ifr_flags:\n");
   printf("Before we gets the interface flag: %d\n",ifr.ifr_flags);

   /* Get the interface flags
    */
   strcpy(ifr.ifr_name, INTERFACE);
   if(ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
   {
      printf("error: ioctl SIOCGIFFLAGS");
      exit(0);
   }

   printf("After we got the flag: %d\n",ifr.ifr_flags);


   ifr.ifr_flags |= IFF_PROMISC;

   /* Set the interface flags. We set our IFF_PROMISC flag
    */
   if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
   {
      printf("error: ioctl SIOCSIFFLAGS\n");
      exit(0);
   }
   printf("After a new flag is set: %d\n",ifr.ifr_flags);



/*********** PART 2 ***********/
   for(;;)
   {
      int n=0;
      static int c=0;
      for(;n<1;)
      {
         n = read(sock, buf, sizeof(buf));
      }

      /* Loopback double filter */
      c++;
      if(c==D_FILTER)continue;
      c=0;
      ip = (struct iphdr *)(buf + 14); /* buf + eth */ 

      /* Since we got a pointer to the ip head from above, we can use
       * ip->ihl to get the lenght of the ip header.. cool :-).
       * ihl means Internet Header Length
       */
      tcp = (struct tcphdr*)(buf + 14 + (4 * ip->ihl));


      /* Continue if the protocol is other then tcp
       * check /etc/protocols
       */
      if(ip->protocol!=6)continue;

      /* Get all flags on the packet */
      if(tcp->th_flags & TH_SYN)printf("syn ");
      if(tcp->th_flags & TH_ACK)printf("ack ");
      if(tcp->th_flags & TH_RST)printf("rst ");
      if(tcp->th_flags & TH_PUSH)printf("push ");
      if(tcp->th_flags & TH_FIN)printf("fin ");
      if(tcp->th_flags & TH_URG)printf("urg ");


      /* Print the ip address and port number
       */
      printf("S=%s -%d- ",get_ip(ip->saddr),ntohs(tcp->th_sport));
      printf("D=%s -%d-\n", get_ip(ip->daddr),ntohs(tcp->th_dport));


      /* Config stuff at the top of the source
       */
      /* Continue if we do not wanna print the ip header
       */
      if(PRINT_HDR == 0) continue;


      /* Print IP header information
       */
      printf("IPv%d ihl:%d tos:%d tot_len:%d id:%d frag_off %d ttl:%d "
             "proto:%d chksum %d\n",
      ip->version,
      ip->ihl,
      ip->tos,
      ip->tot_len,
      ip->id,
      ip->frag_off,
      ip->ttl,
      ip->protocol,
      ntohs(ip->check)
      );

   }

return 0;
}

