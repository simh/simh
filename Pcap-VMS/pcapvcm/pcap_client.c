#pragma module pcap_client "X-1"
/*
 *****************************************************************************
 *
 * Copyright © 1996 Digital Equipment Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Digital Equipment Corporation.  The name of the
 * Corporation may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *****************************************************************************
 *
 * Important Note:
 *
 *     This coding example uses privileged OpenVMS interfaces.
 *     OpenVMS does not guarantee that these interfaces will
 *     be supported indefinitely, and may change these interfaces
 *     without prior notice.
 *
 *****************************************************************************

*/



/* Define data structures and constants */

#include 	<descrip.h>		/* Descriptors			*/
#include 	<ldrimgdef.h>		/* Loaded image data block	*/
#include 	<lnmdef.h>		/* Logical names		*/
#include 	<pdscdef.h>		/* Linkage pairs		*/
#include	<starlet.h>		/* System service proto-types	*/
#include 	<ssdef.h>		/* System service status codes	*/
#include  <stsdef.h>
#include 	<stdio.h>		/* CRTL I/O			*/
#include 	<string.h>		/* CTRL strings			*/
#include <ldrdef.h>
#include <nmadef.h>

#include "pcapvcm.h" 

typedef 	struct lkpdef LKP;

#pragma member_alignment __save
#pragma nomember_alignment
typedef struct _eth_header {
    unsigned char da[6];
    unsigned char sa[6];
    unsigned char proto[2];
    unsigned char data[2048];
} ETH_HEADER;

#pragma member_alignment __restore

typedef struct _ref_handle		/* Dynamic loader reference handle */
{
   void		*base_addr;
   LDRIMG	*ldrimg_ptr;
   int		seq_num;
} REF_HANDLE;


/*  Define special proto-types for the loader routines we need 		*/

extern int ldr$load_image (struct dsc$descriptor_s *execlet_name, int flags,
                           REF_HANDLE *reference_handle);
extern int ldr$ref_info (struct dsc$descriptor_s *execlet_name,
			 REF_HANDLE *reference_handle);
extern int ldr$unload_image (struct dsc$descriptor_s *execlet_name,
			 REF_HANDLE *reference_handle);


/* Define proto-types for Kernel routines				*/
int	load_execlet();
int	unload_execlet();

/* Global data								*/
PCAPVCM *pcapvcm;
REF_HANDLE reference_handle;		/* Dynamic loader ref handle	*/
void *rtnptr = 0;
$DESCRIPTOR(execlet_name, "PCAPVCM");
int is_loaded = 0;


#pragma __required_pointer_size __save
#pragma __required_pointer_size __long  

int get_packet(VCRPLANDEF *vcrp, ETH_HEADER **hdrp)
{
    VCRPDEF *vcrpbase;
    uint64 boff;
    int len;

    vcrpbase = (VCRPDEF *) vcrp;
    boff = vcrpbase->vcrp$l_boff;
    len = vcrpbase->vcrp$l_bcnt; 
    *hdrp = (ETH_HEADER *) (((char *) vcrp) + boff);

    return len;
}


int parse_vcrp(VCRPLANDEF *vcrp)
{
    int status;
    VCRPDEF *vcrpbase;
    ETH_HEADER *hdr;
    uint64 boff;
    uint64 len;
    uint64 haddr;
    uint64 rstat;
    uint32 size;
    char *data;
    uint64 format;
    uint64 dest;

    vcrpbase = (VCRPDEF *) vcrp;
    size = vcrpbase->vcrp$w_size;
    rstat = vcrpbase->vcrp$q_request_status;
    haddr = vcrp->vcrp$a_lan_r_header;
    boff = vcrpbase->vcrp$l_boff;
    format = vcrp->vcrp$l_lan_pkformat;
    dest = vcrp->vcrp$q_lan_t_dest;
    len = vcrpbase->vcrp$l_bcnt; 
    hdr = (ETH_HEADER *) (((char *) vcrp) + boff);
//    data = vcrp->vcrp$b_lan_filler4;
    return status;

}
int build_vcrp(VCRPLANDEF *vcrp, int hdrlen, int len, char *packet)
{
    int status;
    VCRPDEF *base;
    char *packptr;
    char *rethdr;
    int pdulen;

    base = (VCRPDEF *) vcrp;
//    status = init_transmit_vcrp(vcrp);
    packptr = (char *) vcrp + VCRP$T_LAN_DATA + LAN$C_MAX_HDR_SIZE;

    pdulen = (len-hdrlen) + 16;
    base->vcrp$l_boff = (char *) packptr - (char *) base;
    base->vcrp$l_bcnt = pdulen;
    memcpy(&vcrp->vcrp$q_lan_t_dest, packet, 6);
    packptr = (char *) packptr + 16;
    memcpy(packptr, (packet+14), len-14);

    return status;

}


#pragma __required_pointer_size __restore


/*
**++
**	Main - Get us started
**
** Functional description:
**
**	This routine exists to simply get us into Kernel mode to load the
**  execlet.
**
** Calling convention:
**
**	main()
**
** Input parameters:
**
**	None
**
** Output parameters:
**
**	None
**
** Return value:
**
**	Various SS$_xxxx
**
** Environment:
**
**	User mode.
**
**--
*/
int 	main()
{
    int status;
    uint64 arglist[10];
    uint64 lstat;
    int p2len;
    int packlen;
    int hdrlen;
    ETH_HEADER *packet;
    char sa[6] = {0xaa,0x00,0x2b,0x99,0x99,0x99};
    char pty[2] = {0x08,0x00};
    VCRPLANDEF *vcrp;
    char vcrpbuff[4096];
#pragma __required_pointer_size __save
#pragma __required_pointer_size __long  
    char vcrpbuf[4096];
    VCRPLANDEF *vcrpptr;
    VCMCTX *vcmctx;
    char *rawpackptr;
    char devnam[128];
    char p2buf[1024];
    char hdr[128];
    char *p2ptr;
    char *tmpptr;
    int q_stat[4];
#pragma __required_pointer_size __restore

    /* Call kernel mode routine to load execlet and read symbol vector */
    arglist[0] = 0;
    status = sys$cmkrnl(load_execlet,arglist);

    if (status & SS$_NORMAL)
    {
       printf("Execlet loaded\n");
    }
    else
    {
       printf("Status from load_execlet = %x.\n",status);
    }
    // Allocate a port
    arglist[0] = 1;
    arglist[1] = &vcmctx;
    status = sys$cmkrnl_64(pcapvcm->alloc_port, arglist);

    // Now get the devices
    tmpptr = &devnam[0];
    arglist[0] = 2;
    arglist[1] = vcmctx;
    arglist[2] = tmpptr;
    status = sys$cmkrnl_64(pcapvcm->get_device, arglist);

    // Create a port with this device
    arglist[0] = 2;
    arglist[1] = vcmctx;
    arglist[2] = tmpptr;
    status = sys$cmkrnl_64(pcapvcm->create_port, arglist);

    // Populate P2 buffer
    p2ptr = &p2buf[0];
    memset(p2ptr, 0, 1024);
    ADD_INT_VAL(p2ptr, NMA$C_PCLI_FMT, NMA$C_LINFM_ETH);
    ADD_INT_VAL(p2ptr, NMA$C_PCLI_PTY, *(int *)pty);
    ADD_INT_VAL(p2ptr, NMA$C_PCLI_PAD, NMA$C_STATE_OFF);
    ADD_INT_VAL(p2ptr, NMA$C_PCLI_PRM, NMA$C_STATE_ON);
    ADD_INT_VAL(p2ptr, NMA$C_PCLI_CCA, NMA$C_STATE_ON);
    p2len = p2ptr - &p2buf[0];
    arglist[0] = 3;
    arglist[1] = vcmctx;
    arglist[2] = p2len;
    arglist[3] = &p2buf[0];
    status = sys$cmkrnl_64(pcapvcm->enable_port, arglist);

    if (!$VMS_STATUS_SUCCESS(status)) {
	arglist[0] = 2;
	arglist[1] = vcmctx;
	arglist[2] = q_stat;
	status = sys$cmkrnl_64(pcapvcm->get_mgm_error, arglist);
    } else {
	arglist[0] = 1;
	arglist[1] = vcmctx;
//	status = sys$cmkrnl_64(pcapvcm->disable_port, arglist);
    }

    // Read a packet
    vcrpptr = &vcrpbuf[0];
    memset(vcrpptr,0, 4096);    
    arglist[0] = 3;
    arglist[1] = vcmctx;
    arglist[2] = 4096;
    arglist[3] = vcrpptr;
    status = sys$cmkrnl_64(pcapvcm->read_packet, arglist);

    status = parse_vcrp(vcrpptr);
    packlen = get_packet(vcrpptr, &packet);
    rawpackptr = &packet->da[0];

    // Build us a header
    arglist[0] = 3;
    arglist[1] = vcmctx;
    arglist[2] = 128;
    arglist[3] = &hdr[0];
    hdrlen = sys$cmkrnl_64(pcapvcm->build_header, arglist);
    
    // Put in a SA in packet
    memcpy(&packet->sa[0], sa, 6);

    vcrp = &vcrpbuff[0];
    status = build_vcrp(vcrp, 14, packlen, rawpackptr);

    arglist[0] = 4;
    arglist[1] = vcmctx;
    arglist[2] = 14;
    arglist[3] = packlen;
    arglist[4] = rawpackptr;
    status = sys$cmkrnl_64(pcapvcm->send_packet, arglist);

    arglist[0] = 1;
    arglist[1] = vcmctx;
    status = sys$cmkrnl_64(pcapvcm->disable_port, arglist);

    arglist[0] = 4;
    arglist[1] = vcmctx;
    arglist[2] = 14;
    arglist[3] = packlen;
    arglist[4] = packet;
    status = sys$cmkrnl_64(pcapvcm->send_packet, arglist);

    // Read another packet
    arglist[0] = 3;
    arglist[1] = vcmctx;
    arglist[2] = 4098;
    arglist[3] = vcrpptr;
    status = sys$cmkrnl_64(pcapvcm->read_packet, arglist);

    status = parse_vcrp(vcrpptr);

    arglist[0] = 1;
    arglist[1] = vcmctx;
    status = sys$cmkrnl_64(pcapvcm->delete_port, arglist);

    arglist[0] = 1;
    arglist[1] = vcmctx;
    status = sys$cmkrnl_64(pcapvcm->free_port, arglist);

//    arglist[0] = 0;
//    status = sys$cmkrnl(unload_execlet, arglist);

    return (status);

}


/*
**++
**	load_execlet - Load the specified execlet
**
** Functional description:
**
**	This routine is called in Kernel Mode and will load the specified
**  execlet.
**
** Calling convention:
**
**	load_execlet()
**
** Input parameters:
**
**	None
**
** Output parameters:
**
**	None
**
** Return value:
**
**	Various SS$_xxx
**
** Environment:
**
**	Kernel mode
**
**--
*/
int load_execlet ()
{
    int 	status;
    LKP 	*symvec;                            /* Pointer to symbol vector	*/
    int (*getContext)(PCAPVCM **);

    /* Try referencing execlet first, in case it is already loaded */
    status = LDR$REF_INFO (&execlet_name, &reference_handle);

    /* If error, must not be loaded yet */
    if (status != SS$_NORMAL)
    {
       /* Load execlet */
       status = LDR$LOAD_IMAGE (&execlet_name, LDR$M_UNL, &reference_handle);
    }

    if ($VMS_STATUS_SUCCESS(status)) {
	// Indicate that we've loaded the execlet
	is_loaded = 1;

	// Get the shared context. We built the execlet so that the address
	// of the routine that does this is at home base...
	rtnptr = *(void **)reference_handle.ldrimg_ptr->ldrimg$l_nonpag_w_base;
	if (rtnptr) {
	    getContext = (int (*)())rtnptr;
	    status = (*getContext)(&pcapvcm);
	}
    }
    return(status);

}


//
// Unload the execlet
//
int unload_execlet ()
{
    int status;
    
    if (is_loaded) {
	status = LDR$REF_INFO (&execlet_name, &reference_handle);
	if ($VMS_STATUS_SUCCESS(status)) {
	    status = LDR$UNLOAD_IMAGE(&execlet_name, &reference_handle);
	}
	is_loaded = 0;
    } else {
	status = SS$_ACCVIO;
    }
    return status;
}


