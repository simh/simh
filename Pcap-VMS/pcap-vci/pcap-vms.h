/*
 * Copyright (c) 2002 Compaq Computer Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the Politecnico
 * di Torino, and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * X-2	    Ankan		Anders Ahgren			30-Mar-2003
 *	    Add VCI support. This is done by calling the PCAP VCM
 *	    execlet, rather than the LAN driver. This should speed
 *	    up performance.
 *
 * X-1	    Ankan		Anders Ahgren			29-Nov-2002
 *	    Initial version. 
 *
 */

#ifndef __PCAP_VMS__H
#define __PCAP_VMS__H

#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4

#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <socket.h>
#include <in.h>
#include "snprintf.h"
#include "pcapvcm.h"

//#include "bittypes.h"
#include <time.h>
//#include <io.h>

//
// Do not align
//
#pragma nomember_alignment
typedef unsigned long u_int32;
typedef struct _promisc_header {
    unsigned char da[6];
    unsigned char sa[6];
    char proto[2];
    char misc[6];
} promisc_header;

typedef struct _packet_header {
    unsigned char da[6];
    unsigned char sa[6];
    char proto[2];
} packet_header;

typedef struct _send_header {
    unsigned char da[6];
    char proto[2];
} send_header;

typedef struct _packet {
    packet_header hdr;
    unsigned char data[2048];
} packet;

#endif /* __PCAP_VMS__H */
