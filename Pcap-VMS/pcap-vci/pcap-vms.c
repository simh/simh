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
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * X-5      MB			Matt Burke			10-Jun-2011
 *	    Changed protocol type from 'IP' to 'DEC Customer Protocol'
 *
 * X-4      Mark Pizzolato	mark@infocomm.com		30-Oct-2003
 *	    Changed the interface names returned by pcap_platform_finddevs
 *	    to be upper case to conform to the form provided by UCX. This
 *	    avoids duplicate interface names from being found.
 *	    Fixed error paths in pcap_open_live to release aquired resources.
 *
 * X-3	    Mark Pizzolato	mark@infocomm.com		20-Oct-2003
 *	    filled in pcap_platform_finddevs.  This has the consequence of
 *	    letting pcap functionality work if there is no IP stack installed
 *	    or if the ip stack installed is not UCX.  Additionally pcap 
 *	    functionality can be achieved on an interface which isn't 
 *	    associated with an IP stack.
 *	    Fixed pcap_read to allow the complete frame contents to be read
 *	    instead of merely the first 1500 bytes.
 *	    Fixed pcap_read result data length to not include the CRC in the 
 *	    returned frame size.
 *
 * X-2	    Ankan		Anders Ahgren			30-Mar-2003
 *	    Almost a complete rewrite. We're now interfacing with an
 *	    execlet, which gives us access to the VCI interface.
 *
 * X1.0	    Ankan		Anders Ahgren			29-Nov-2002
 *	    Initial version.
 */
// VMS Includes
#include <errno.h>
#include <ctype.h>                        /* Character type classification macros/routines */ 
#include <descrip.h>                      /* For VMS descriptor manipulation */ 
#include <iodef.h>                        /* I/O function code definitions */ 
#include <ssdef.h>                        /* System service return status code definitions */ 
#include <starlet.h>                      /* System library routine prototypes */ 
#include <stdio.h>                        /* ANSI C Standard Input/Output */ 
#include <stdlib.h>                       /* General utilities */ 
#include <string.h>                       /* String handling */ 
#include <stsdef.h>                       /* VMS status code definitions */ 
#include <dvidef.h>
#include <dcdef.h>
#include <efndef.h>
#include <lib$routines.h>
#include <unistd.h>
#include <socket.h>
#include <in.h>
#include <if.h>
#include <ldrimgdef.h>
#include <ldr_routines.h>
#include <nmadef.h>			  /* NMA stuff */
 
#define LANSIZE 256

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pcapvci.h"
#include "pcap-vms.h"

#include <string.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "pcap-int.h"

#define $SUCCESS(status) (((status) & STS$M_SUCCESS) == SS$_NORMAL) 
#define $FAIL(status) (((status) & STS$M_SUCCESS) != SS$_NORMAL) 
#define init_desc(name, len, addr) \
    { \
    name.dsc$b_dtype = DSC$K_DTYPE_T; \
    name.dsc$b_class = DSC$K_CLASS_S; \
    name.dsc$a_pointer = addr; \
    name.dsc$w_length = len; \
    }


typedef struct _iosb                             
{ 
    short cond_val;                        /* Completion Status */ 
    short size;                  /* Transfer Size */ 
    short addl;                       /* Additional status */ 
    short misc;                       /* Miscellaneous */ 
} IOSB;

typedef struct _interface {
    char interface[4];
    char device[6];
} INTERFACE;



/*
** Timeout AST routine
*/
void timer_ast(pcap_t *p)
{
    p->timedout = 1;
}



/*
** Interface to device conversion routine. Converts a TCP/IP interface
** to a ASCIC VMS device for use by VCI.
**
** Example:
**
** WE0 becomes EWA
** SE1 becomes ESB
** XE0 becomes EXA
**
** For now we do not worry about pseudo interfaces, e.g WEA0
*/
int convert_interface_device(char *inter_name, INTERFACE *inter ) {
    char num_conv[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int status;
    char tmpdev[5];
    int tmpval;

    tmpdev[0] = 3;
    tmpdev[1] = toupper(inter_name[1]);
    tmpdev[2] = toupper(inter_name[0]);
    tmpval = (int) (char) inter_name[2] - (char) '0';
    if (tmpval < 0 || tmpval > sizeof(num_conv)) {
	return -1;
    }
    tmpdev[3] = num_conv[tmpval];
    tmpdev[4] = '\0';
    strcpy(inter->interface, inter_name);
    memcpy(inter->device, tmpdev, 5);
    return 0;
}
    
/*
** Device name to interface name Interface conversion routine. Converts a 
** VMS device name string of the form _ddc0: to a TCP/IP interface name
** for later use by VCI.
**
** Example:
**
** _EWA0: becomes WE0
** _ESB0: becomes SE1
** _EXA0: becomes XE0
**
** For now we do not worry about pseudo interfaces, e.g WEA0
*/
int convert_device_interface(char *device, char *inter_name ) {
    char num_conv[] = "ABCDEFGHIJ";
    int tmpval;

    if ((device[0] != '_') ||
	(device[4] != '0') ||
	(device[5] != ':'))
	return -1;
    tmpval = toupper(device[3]) - 'A';
    if (tmpval < 0 || tmpval > sizeof(num_conv))
	return -1;
    inter_name[0] = toupper(device[2]);
    inter_name[1] = toupper(device[1]);
    inter_name[2] = '0' + tmpval;
    inter_name[3] = '\0';
    return 0;
}
    


//
// This is kept in the execlet, so go get it.
//
int pcap_stats(pcap_t *p, struct pcap_stat *ps)
{
    int status;
    PCAPSTAT vci_stat;

    status = pcapvci_get_statistics(p->vcmctx, &vci_stat);

    if $FAIL(status) {
	return (-1);
    }

    ps->ps_recv = vci_stat.recv_packets;
    ps->ps_drop = vci_stat.recv_packets_dropped;

    return 0;
}



/*
** Read a packet
*/
int pcap_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
    int status;
    struct pcap_pkthdr pcap_header;
    promisc_header promhdr;
    int timeout[2];
    int msec_mul = -10000;
    int once = 1;
    int packlen;
     
    //
    // If we're to timeout, set up timeout.
    //
    if (p->check_timeout && p->timeout > 0) {
	p->timedout = 0;
	status = lib$emul(&p->timeout, &msec_mul, &0, timeout);
	if $FAIL(status) {
	    return -1;
	}
	status = sys$setimr(0, timeout, &timer_ast, p, 0);
	if $FAIL(status) {
	    return -1;
	}
    }

    while (once || (p->check_timeout && !p->timedout)) {
	if (p->timeout != -1) {
	    once = 0;
	}
	// Read the packet
	packlen = pcapvci_read_packet(p->vcmctx, sizeof(p->lan_pkt), (char *)&p->lan_pkt);	

	if (packlen < 0) {
	    p->check_timeout = 0;
	    return -1;
	}

	if (packlen == 0) {
	    p->check_timeout = 0;
	    return 0;
	}
	// Remove the CRC from consideration in the captured packet
	packlen -= 4;
	if (p->fcode.bf_insns == NULL ||
	    bpf_filter(p->fcode.bf_insns, (unsigned char *)&p->lan_pkt, 
		packlen, packlen)) {

	    ++p->md.stat.ps_recv;
	    pcap_header.len = packlen;
	    pcap_header.caplen = packlen;
	    gettimeofday(&pcap_header.ts, NULL);
	    (*callback)(user, &pcap_header, (unsigned char *)&p->lan_pkt);
	    p->check_timeout = 0;
	    return 1;
	}
    }
    
    p->check_timeout = 0;
    return 0;
}



/*
** Send a packet. 
** One problem, the send it asynchronous, so we can't check for status.
*/
int pcap_sendpacket(pcap_t *p, u_char *buf, int size)
{
    int status;

    status = pcapvci_send_packet(p->vcmctx, 14, size, (char *)buf);

    return 0;
}



/*
** Open a connection. This is a bit tricky. We could use the LAN driver to listen
** to packets. However, we'd also like to be able to modify packets and send
** them off to the wire. In many cases when sending a packet we want to modify
** the sources address. While it is possible to modify the physical address
** using the LAN driver it affects the controller, not the device we created
** from the LAN driver template device. For this reason we have to use a
** VMS execlet that uses the undocumented VCI interface. The code for that
** part is a separate image, which we interface to via a buffer in the
** non paged pool, real cool.
*/
pcap_t *pcap_open_live(char *device, int snaplen, int promisc, int to_ms, char *ebuf)
{
    int status;
    pcap_t *pcap_handle;
    char *ctlptr;
    char proto[5] = {8,0,0x2b,0x80,0x00};
    char pty[2] = {0x60,0x06};
    unsigned long ctldesc[2];
    INTERFACE interface;

    // Load the PCAP VCM execlet, if not already loaded
    status = pcapvci_load_execlet();
    if $FAIL(status) {
	return NULL;
    }

    // Convert interface to device
    status = convert_interface_device(device, &interface);
    if (status < 0) {
	return NULL;
    }

    pcap_handle = malloc(sizeof(pcap_t));
    if (!pcap_handle) {
	return NULL;
    }
    memset(pcap_handle, 0, sizeof(pcap_t));

    // Allocate a VCI port
    status = pcapvci_alloc_port(&pcap_handle->vcmctx);
    if $FAIL(status) {
	free(pcap_handle);
	return NULL;
    }

    // Create a VCI port
    status = pcapvci_create_port(pcap_handle->vcmctx, interface.device);
    if $FAIL(status) {
	pcapvci_free_port(pcap_handle->vcmctx);
	free(pcap_handle);
	return NULL;
    }

    pcap_handle->lan_ctl = malloc(LANSIZE);
    if (!pcap_handle->lan_ctl) {
	free(pcap_handle);
	return NULL;
    }

    pcap_handle->bufsize = 64*1024;
    pcap_handle->buffer = malloc(pcap_handle->bufsize);

    //
    // Save timeout value
    //
    pcap_handle->timeout = to_ms;
    pcap_handle->check_timeout = 0;

    //
    // Link type ethernet
    //
    pcap_handle->linktype = DLT_EN10MB;

    //
    // Save snapshot length
    //
    pcap_handle->snapshot = snaplen;

    //
    // Use standard ethernet package type
    //
    ctlptr = pcap_handle->lan_ctl;
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_FMT, NMA$C_LINFM_ETH);
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_PAD, NMA$C_STATE_OFF);
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_MLT, NMA$C_STATE_ON);
     
    
    //
    // Have device buffer 255 packets
    //
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_BFN, 255);
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_BUS, 2048);

    //
    // If promiscious mode, enable it
    //
    if (promisc) {
	ADD_INT_VAL(ctlptr, NMA$C_PCLI_PRM, NMA$C_STATE_ON);
    }

    //
    // All ethernet packets
    //
    ADD_INT_VAL(ctlptr, NMA$C_PCLI_PTY, *(int *)pty);

    //
    // Calculate length
    //
    ctldesc[0] = ctlptr - pcap_handle->lan_ctl;
    ctldesc[1] = (unsigned long)pcap_handle->lan_ctl;

    //
    // Enable the VCI port
    //
    status = pcapvci_enable_port(pcap_handle->vcmctx, ctldesc[0], (char *)ctldesc[1]);
    if $FAIL(status) {
	pcapvci_delete_port(pcap_handle->vcmctx);
	pcapvci_free_port(pcap_handle->vcmctx);
	free(pcap_handle->lan_ctl);
	free(pcap_handle);
	return NULL;
    }

    return pcap_handle;
}



void pcap_close_vms(pcap_t *p)
{
    int status;

    //
    // Disable the port
    //
    status = pcapvci_disable_port(p->vcmctx);

    //
    // Delete the port
    //
    status = pcapvci_delete_port(p->vcmctx);

    //
    // Get rid of VCM context
    //
    status = pcapvci_free_port(p->vcmctx);

    //
    // Free up memory
    //
    if (p->lan_ctl) {
	free(p->lan_ctl);
    }
}



int pcap_platform_finddevs(pcap_if_t **alldevsp, char *errbuf)
{
    int ctx[2] = {0,0};
    char devnam[65];
    char interfacename[8];
    char description[100];
    long devclass = DC$_SCOM;
    long devunit, devclassval;
    int unititem = DVI$_UNIT;
    int classitem = DVI$_DEVCLASS;
    $DESCRIPTOR(retdev,devnam);
    $DESCRIPTOR(searchdev, "*0:");
    int status;

    while (1)
	{
	retdev.dsc$w_length = sizeof(devnam)-1;
	status = sys$device_scan( &retdev, &retdev.dsc$w_length, &searchdev, NULL, ctx);
	if $FAIL(status)
	    break;
	status = lib$getdvi(&unititem, 0, &retdev, &devunit, NULL, NULL);
	if $FAIL(status)
	    break;
	if (0 != devunit)
	    continue;    
	status = lib$getdvi(&classitem, 0, &retdev, &devclassval, NULL, NULL);
	if $FAIL(status)
	    break;
	if (DC$_SCOM != devclassval)
	    continue;    
	devnam[retdev.dsc$w_length] = '\0';
	if (0 != convert_device_interface(devnam, interfacename))
	    continue;
	/*
	 * Add information for this address to the list.
	 */
	sprintf(description, "VMS Device: %s", devnam);
	if (pcap_add_if(alldevsp, interfacename, 0, description, errbuf) < 0)
	    continue;
	}
    return (0);
}



int pcap_setfilter(pcap_t *p, struct bpf_program *fp)
{
    if (install_bpf_program(p, fp) < 0)
	    return (-1);
    return (0);
}
