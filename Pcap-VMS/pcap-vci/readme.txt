This is a second port of the pcap library for VMS. It interfaces with
a VMS execlet to capture the packets. This results in better performance
than the $QIO version of the pcap library. This version also includes
a routine to send packets. Many pcap implementations include such a
routine, however it is not part of the pcap standard and as such has no
specified name or calling signature. In this implementation it is
defined as:

int pcap_sendpacket(pcap_t *p, u_char *buf, int size);


where:
 p - pcap handle, as returned by pcap_open_live
 buf - pointer to the packet to send, including header
 size - size of the packet, including header


To build the pcap library, just do a:

$ @VMS_PCAP

This will build PCAP.OLB, to use it do something like:

$ cc/debug/noopt pcaptest/incl=sys$disk:[]/name=(as_is,short)
$ link/debug pcaptest

For information regarding PCAP just check the WWW.

You will notice that there are still some compilation warnings.

For filtering syntax have a look at grammar.y, provided you know yacc
that is.

Let me know if you enhance this package by sending mail to ankan@hp.com



