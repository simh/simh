#pragma module	PCAPVCM "X-3"
#pragma code_psect "EXEC$NONPAGED_CODE"
#pragma linkage_psect "EXEC$NONPAGED_LINKAGE"

/* pcapvcm.c - packet capturing execlet

   Copyright (c) 2003, Anders "ankan" Ahgren

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
//***************************************************************************
//
//
// FACILITY:
//
//      Dynamic Loadable Execlet for PCAP
//
//
// ABSTRACT:
//
//	This module implements a VCM for the LAN driver
// 
//
// AUTHOR:
//
//	Ankan
//
//  CREATION DATE:  21-Mar-2003
//
//  DESIGN ISSUES:
//
//      All data passed to this execlet is assumed to be correct.
//	No probes/range checks are made. Any failliure to read/write
//	data passed into the execlet will cause a system crash.
//
// *****************************************************************************
// *
// * Important Note:
// *
// *     This code uses privileged OpenVMS interfaces.
// *     OpenVMS does not guarantee that these interfaces will
// *     be supported indefinitely, and may change these interfaces
// *     without prior notice.
// *
// *****************************************************************************
//
//
// REVISION HISTORY:
//
//      X-3     MB			Matt Burke		07-Jul-2011
//              Missing fork_unlock for return path in
//              pcap$vcm_read_packet when queue is empty.
//
//      X-2     MB			Matt Burke		10-Jun-2011
//           03 IOLOCK8 must be held whilst calling any LAN
//              functions to ensure proper synchronization
//              on SMP systems.
//           02 Changes to allow compilation on IA64.
//           01 Fix register corruption in pcap_event on ALPHA.
//
//      X-1     Ankan			Anders Ahgren		21-Mar-2003
//              Initial version.
//

//
// Imported definitions
//
#define __NEW_STARLET 1
#include <starlet.h>
#include <string.h>
#include <stdlib.h>
#include <lanudef.h>
#include <ldcdef.h>
#include <vcibdef.h>
#include <vcibdlldef.h>
#include <vcrpdef.h>
#include <vcrplandef.h>
#include <lildef.h>
#include <dyndef.h>
#include <ints.h>
#include <ssdef.h>
#include <vms_macros.h>
#include <vms_drivers.h>
#include <builtins.h>
#include <nmadef.h>
#include <boostatedef.h>
#include <chfdef.h>
#include <exe_routines.h>
#include <ldr_routines.h>
#include <ldrimgdef.h>
#include <pdscdef.h>
#include <psldef.h>
#include <inirtndef.h>
#include <gen64def.h>

#include "pcapvcm.h"

//
// Load/unload stuff...
//
int pcap$vcm_init(  LDRIMG *ini_image_block,
		    INIRTN *ini_flags_addr,
		    const char *ini_user_buffer );
int pcap$vcm_unload();

//
// Execlet interface routine declarations
//
int pcap$vcm_get_context(PCAPVCM_64PP vcm);
int pcap$vcm_alloc_port(VCMCTX_64PP vcmctx);
int pcap$vcm_free_port(VCMCTX_64P vcmctx);
int pcap$vcm_getdevice(VCMCTX_64P vcmctx, CHAR_64P devnam);
int pcap$vcm_create_port(VCMCTX_64P vcmctx, CHAR_64P device);
int pcap$vcm_delete_port(VCMCTX_64P vcmctx);
int pcap$vcm_enable_port(VCMCTX_64P vcmctx, int p2len, CHAR_64P p2buf);
int pcap$vcm_disable_port(VCMCTX_64P vcmctx);
int pcap$vcm_get_mgm_error(VCMCTX_64P vcmctx, CHAR_64P error);
int pcap$vcm_get_last_error(VCMCTX_64P vcmctx);
int pcap$vcm_read_packet(VCMCTX_64P vcmctx, int len, CHAR_64P packet);
int pcap$vcm_send_packet(VCMCTX_64P vcmctx, int hdrlen, int len,
    CHAR_64P rawpacket);
int pcap$vcm_build_header(VCMCTX_64P vcmctx, int len, CHAR_64P header);
int pcap$vcm_get_statistics(VCMCTX_64P vcmctx, CHAR_64P stats);

//
// Special linkage
//
#define	INIT001_ROUTINE pcap$vcm_init
#include		init_rtn_setup

//
// VCI callback routines. Not to funny, thse are needed because these routines
// are called via JSBs from the LAN driver (LAN.MAR).
//
/* Transmit Complete */
#ifdef __ia64
#pragma linkage_ia64 pcap_txLnkg = (parameters(r4,r3), preserved(r28,r4,r5), nopreserve(r8,r9))
#else
#pragma linkage pcap_txLnkg = (parameters(r4,r3), preserved(r2,r4,r5), nopreserve(r0,r1))
#endif
#pragma use_linkage pcap_txLnkg (pcap_txCompl)
void pcap_txCompl( VCIBDLLDEF *vcib, VCRPDEF *request );

/* PortMgmt Complete */
#ifdef __ia64
#pragma linkage_ia64 pcap_mgmLnkg = (parameters(r4,r3), preserved(r28,r4,r5), nopreserve(r8,r9))
#else
#pragma linkage pcap_mgmLnkg = (parameters(r4,r3), preserved(r2,r4,r5), nopreserve(r0,r1))
#endif
#pragma use_linkage pcap_mgmLnkg (pcap_mgmCompl)
void pcap_mgmCompl( VCIBDLLDEF *vcib, VCRPDEF *request );

/* Receive */
#ifdef __ia64
#pragma linkage_ia64 pcap_rxLnkg = (parameters(r4,r3), preserved(r28,r4,r5), nopreserve(r8,r9))
#else
#pragma linkage pcap_rxLnkg = (parameters(r4,r3), preserved(r2,r4,r5), nopreserve(r0,r1))
#endif
#pragma use_linkage pcap_rxLnkg (pcap_rxCompl)
void pcap_rxCompl( VCIBDEF *vcib, VCRPDEF *request );

/* Events */
#ifdef __ia64
#pragma linkage_ia64 pcap_evtLnkg = (parameters(r4,r9,r28), preserved(r4,r5), nopreserve(r8,r9))
#else
#pragma linkage pcap_evtLnkg = (parameters(r4,r1,r2), preserved(r4,r5), nopreserve(r0,r1))
#endif
#pragma use_linkage pcap_evtLnkg (pcap_event)
void pcap_event( VCIBDLLDEF *vcib, int event, int reason );


//
// External variables
//
extern uint64 exe$gq_systime;

//
// Global variables
//
#pragma extern_model save
#pragma extern_model strict_refdef "EXEC$NONPAGED_DATA"
static int unlvec[4];
#pragma __required_pointer_size __save
#pragma __required_pointer_size __long  
static	PCAPVCM	*pcapvcm = 0;
#pragma __required_pointer_size __restore  
#pragma extern_model restore


//
// Note: we use the VCIB as a queue for VCRBs and to keep track of the
// size of the queue we use the size field to hold the number of elements
// in the queue.
//
int init_vcib(VCIBDLLDEF *vcib, LILDEF *lil)
{
    int status = SS$_NORMAL;
    VCIBDEF *vcib_base;

    vcib_base = (VCIBDEF *) vcib;
    memset(vcib, 0, sizeof(VCIBDLLDEF));

    vcib_base->vcib$a_portmgmt_complete = (void *) pcap_mgmCompl;
    vcib_base->vcib$a_receive_complete = (void *) pcap_rxCompl;
    vcib_base->vcib$a_report_event = (void *) pcap_event;
    vcib_base->vcib$a_transmit_complete = (void *) pcap_txCompl;
    vcib_base->vcib$b_type = DYN$C_DECNET;
    vcib_base->vcib$b_sub_type = DYN$C_NET_VCI_VCIB;
    vcib_base->vcib$l_vci_id = 0x0101;
    vcib_base->vcib$w_version_upper = 1;
    vcib->vcib$a_dll_input_list = lil;
    vcib->vcib$w_dll_hdr_size = LAN$C_MAX_HDR_SIZE; // Max out...
    vcib->vcib$v_lan_ftc = 1;	// Always call completion routine
    return status;
}

int init_mgmt_vcrp(VCRPLANDEF *vcrplan, int func, int p2len, char **p2buf)
{
    int status = 1;
    VCRPDEF *vcrp;

    vcrp = (VCRPDEF *) vcrplan;
    vcrp->vcrp$b_type = DYN$C_VCRP;
    vcrp->vcrp$v_cmn_mgmt = 1;
    vcrp->vcrp$l_function = func;
    vcrplan->vcrp$a_lan_p2buff = p2buf;
    vcrplan->vcrp$l_lan_p2buff_size = p2len;

    return status;
}


int init_transmit_vcrp(VCRPLANDEF *vcrplan)
{
    int status = 1;
    VCRPDEF *vcrp;
    
    vcrp = (VCRPDEF *) vcrplan;
    vcrp->vcrp$b_type = DYN$C_VCRP;
    vcrp->vcrp$v_cmn_mgmt = 0;
    vcrp->vcrp$l_function = VCRP$K_FC_TRANSMIT;

    return status;
}


//
//	This is the execlet initialization routine, which is called upon
//	loading of this image.
//
int pcap$vcm_init (LDRIMG *ini_image_block,
		    INIRTN *ini_flags_addr,
		    const char *ini_user_buffer )
{
    int	status;
    uint64	alloc_size;

    //
    // Make sure we are not called again
    //
    ini_flags_addr->inirtn$v_no_recall = 1;

    //
    // We need to do some cleanup if we ever get unloaded, so declare an
    // unload vector and pass our unload routine
    //
    unlvec[0] = (int) pcap$vcm_unload;
    unlvec[1] = 0;
    unlvec[2] = 0;
    unlvec[3] = 0;
    ini_image_block->ldrimg$l_unlvec = unlvec;

    //
    // Allocate 2 pages for our shared data structure
    // with our companion, the PCAP library.
    //
#if __VMS_VER >= 80200000
    status = mmg$allocate_sva_and_pfns (
		    2,					// number of pages
		    0,
		    0,
		    1,					// S1 space
		    PTE$C_UW | PTE$M_ASM,		// User mode RW
		    1,					// nonpaged
		    (VOID_PPQ) &pcapvcm );
#else
    status = mmg_std$alloc_system_va_map (
		    PTE$C_UW | PTE$M_ASM,		// User mode RW
		    2,					// number of pages
		    1,					// nonpaged
		    1,					// S1 space
		    (VOID_PPQ) &pcapvcm );
#endif
    if (!$VMS_STATUS_SUCCESS(status))
      bug_check (CUSTOMER, FATAL, COLD);

    //
    // Initialize the data structure...
    //
    memset (pcapvcm, 0, mmg$gl_page_size);
    pcapvcm->mbo = 1;
    pcapvcm->type = DYN$C_MISC;
    pcapvcm->subtype = DYN$C_MISC;
    pcapvcm->size = sizeof(PCAPVCM);
    pcapvcm->revision = PCAPVCM$K_REVISION;
    pcapvcm->recv_queue_size = PCAPVCM$K_RECV_QUEUE_SIZE;
    pcapvcm->retry_count = PCAPVCM$K_RECV_QUEUE_RETRY;
    pcapvcm->get_context = pcap$vcm_get_context;
    pcapvcm->alloc_port = pcap$vcm_alloc_port;
    pcapvcm->free_port = pcap$vcm_free_port;
    pcapvcm->get_device = pcap$vcm_getdevice;
    pcapvcm->create_port = pcap$vcm_create_port;
    pcapvcm->delete_port = pcap$vcm_delete_port;
    pcapvcm->enable_port = pcap$vcm_enable_port;
    pcapvcm->disable_port = pcap$vcm_disable_port;
    pcapvcm->get_mgm_error = pcap$vcm_get_mgm_error;
    pcapvcm->get_last_error = pcap$vcm_get_last_error;
    pcapvcm->read_packet = pcap$vcm_read_packet;
    pcapvcm->send_packet = pcap$vcm_send_packet;
    pcapvcm->build_header = pcap$vcm_build_header;
    pcapvcm->get_statistics = pcap$vcm_get_statistics;

    return SS$_NORMAL;
}



//
//	This unload routine will be automagically called during execlet
//	unloading to perform the cleanup steps.
// NOTE - since I'm useless at building execlets this currently will
// cause the system to crash, because I have pageable psects.
int pcap$vcm_unload ()
{
    int status;
    int pages;
    int cpuidx;
    uint64 delta;
    GENERIC_64 delta_time;

    //
    // Is the pointer good to the shared data?
    //
    if ( pcapvcm != 0 ) {

	//
	// Finally get rid of the shared data block
	//
	mmg_std$dealloc_sva ( 2, pcapvcm );
	pcapvcm = 0;
    }

    return SS$_NORMAL;
}

//
// VCI callback routies
//

//
// Transmit done. We only allow one transmit at the time, this
// routine simply clears the trasmit in progress flag.
//
void pcap_txCompl( VCIBDLLDEF *vcib, VCRPDEF *request )
{
    VCMCTX *vcmctx;
    VCRPLANDEF *vcrp;

    // Get context
    vcmctx = request->vcrp$a_creator;
    vcmctx->transmit_pending = 0;
    vcmctx->last_error = request->vcrp$l_request_status;
    vcmctx->stat.tr_packets++;
}


//
// Do nothing. We own the management VCRP and have a method for getting
// the status below.
//
void pcap_mgmCompl( VCIBDLLDEF *vcib, VCRPDEF *request )
{
    uint64 status;
    status = request->vcrp$l_request_status;
}


//
// Receive complete routine. Notice that the VCIB contains our
// context, so that we can fiddle... we assume we're going to
// ignore this packet.
//
void pcap_rxCompl( VCIBDEF *vcib, VCRPDEF *request )
{
    int retry_count = 1;
    int status = SS$_NORMAL;
    int trash_it = 0;
    PCAPVCIB *pcapvcib;
    VCRPDEF *vcrpout;
    VCMCTX *vcmctx;
    int saved_ipl;


    //
    // Get our port context
    //
    pcapvcib = (PCAPVCIB *) vcib;
    vcmctx = (VCMCTX *) pcapvcib->vcmctx;
    
    //
    // Put into global receive queue, so no copy.
    // Notice, at this time we're supposed to hold
    // IOLOCK 8, so be as fast as possible
    //
    retry_count += pcapvcm->retry_count;
    do {
	status = __PAL_INSQHIL(vcib, request);
    } while (status < 0 && retry_count-- > 0);

    //
    // If we failed to insert this item, drop it
    //
    if (status < 0) {
	fork_lock(SPL$C_IOLOCK8, &saved_ipl);
	status = vci_delete_vcrp(request);
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
	vcmctx->stat.recv_packets_dropped++;
	return;
    }

    //
    // Increase counter
    //
    vcmctx->stat.recv_packets++;

    //
    // Is the queue full?
    //
    vcib->vcib$w_size++;
    if (vcib->vcib$w_size > vcmctx->recv_queue_size) {
	status = __PAL_REMQTIL(vcib, (void *)&vcrpout);
	vcib->vcib$w_size--;
	if (status != 0) {
	    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
	    status = vci_delete_vcrp(vcrpout);
	    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
	    vcmctx->stat.recv_packets_dropped++;
	}
    }

    //
    // Update statistics
    //
    pcapvcm->curr_recv_queue_size = vcib->vcib$w_size;
    vcmctx->stat.recv_queue_size = vcib->vcib$w_size;    
}


//
// We reveived an event, just save it for now...
//
void pcap_event( VCIBDLLDEF *vcib, int event, int reason )
{

    pcapvcm->last_mgm_event = event;
}


//
// Get the PCAP context
//
int pcap$vcm_get_context(PCAPVCM_64PP vcm)
{
    int status;

    if (pcapvcm != NULL) {
	*vcm = pcapvcm;
	status = SS$_NORMAL;
    } else {
	status = SS$_ACCVIO;
    }

    return status;
}


//
// Allocate a VCM port context. This must be done as the first thing
//
int pcap$vcm_alloc_port(VCMCTX_64PP vcmctx)
{
    int status = SS$_NORMAL;
    int pages;
    VCMCTX_64P _align(QUADWORD) tmpctx;
    uint64 real_size;

    //
    // Number of pages required for CPU context
    //
    pages = (sizeof(VCMCTX) + (mmg$gl_page_size - 1))/mmg$gl_page_size;

    //
    // Allocate a VCM context from NNP...
    //
    status = exe$allocate_pool(
		sizeof(VCMCTX),
		MMG$K_POOLTYPE_NPP,
		6,
		&real_size,
		(VOID_PPQ) &tmpctx);
    if ( !$VMS_STATUS_SUCCESS(status) ) {
	return status;
    }

    memset(tmpctx, 0, sizeof(VCMCTX));
    tmpctx->size = real_size;
    tmpctx->lil = (LILDEF *)tmpctx->lilbuf;
    tmpctx->recv_queue_size = pcapvcm->recv_queue_size;
    INIT_LIL(tmpctx->lil, PCAP_LIL_SIZE);
    *vcmctx = tmpctx;

    return status;
}

//
// Deallocate a port block
//
int pcap$vcm_free_port(VCMCTX_64P vcmctx)
{
    int status = SS$_NORMAL;

    //
    // TBD - Check state, to ensure we can do this...
    //

    //
    // Dellocate our context...
    //
    exe$deallocate_pool(vcmctx, MMG$K_POOLTYPE_NPP, vcmctx->size);

    return status;
}

//
// Get devices. We must copy, since LAN return a kernel only readable address
// Notice that this routine can be called multiple times until no more
// devices are found.
//
int pcap$vcm_getdevice(VCMCTX_64P vcmctx, CHAR_64P devnam)
{
    int status = SS$_NORMAL;
    uint32 id;
    LDCDEF *ldc;
    unsigned char *tmpname;
    int len = 0;
    int saved_ipl;

    id = vcmctx->ldcid;
    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    status = vci_get_device(&id, &ldc);
    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    if ($VMS_STATUS_SUCCESS(status)) {
	vcmctx->ldcid = id;
	tmpname = (unsigned char *) ldc->ldc$a_name;
	len = (int) tmpname[0];
	vcmctx->ldc.ldc$a_name = (void *) &vcmctx->devbuf[0];
	memcpy(vcmctx->ldc.ldc$a_name, ldc->ldc$a_name, len+1);
	memcpy(devnam, ldc->ldc$a_name, len+1);
	devnam[len+1] = (char) 0;
	vcmctx->ldc.ldc$l_type = ldc->ldc$l_type;
	vcmctx->ldc.ldc$l_rcvsize = ldc->ldc$l_rcvsize;
	vcmctx->ldc.ldc$l_devtype = ldc->ldc$l_devtype;
    } else {
	vcmctx->ldcid = 0;
    }
    return status;
}

//
// Create a port...
//
int pcap$vcm_create_port(VCMCTX_64P vcmctx, CHAR_64P device)
{
    int status;
    char tmpdev[128];
    int len;
    PCAPVCIB *pcapvcib;
    int saved_ipl;

    len = (int) device[0];
    memcpy(tmpdev, device, len+1);
        
    //
    // Add device to LIL
    //
    if (device) {
	ADD_LIL_ADDR_VAL((LILDEF *)vcmctx->lil, DLL$K_LAN_DEVICE,
	    len+1, tmpdev);
    }
    
    //
    // Initialize VCIB
    //
    status = init_vcib((VCIBDLLDEF *)&vcmctx->vcib, (LILDEF *) vcmctx->lil);

    //
    // Save context in our VCIB
    //
    pcapvcib = (PCAPVCIB *) &vcmctx->vcib;
    pcapvcib->vcmctx = (VCMCTX *) vcmctx;

    if $VMS_STATUS_SUCCESS(status) {
	fork_lock(SPL$C_IOLOCK8, &saved_ipl);
	status = vci_create_port((VCIBDLLDEF *)&vcmctx->vcib);
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    }

    return status;
}

//
// Delete a port
//
int pcap$vcm_delete_port(VCMCTX_64P vcmctx)
{
    int status;
    int saved_ipl;

    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    status = vci_delete_port((VCIBDLLDEF *)&vcmctx->vcib);
    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);

    return status;
}

//
// Enable a port
//
int pcap$vcm_enable_port(VCMCTX_64P vcmctx, int p2len, CHAR_64P p2buf)
{
    int status;
    int saved_ipl;

    if (p2len > 0 && p2buf != NULL) {
	memcpy(&vcmctx->p2_buf[0], p2buf, p2len);
	vcmctx->p2ptr = (char *)&vcmctx->p2_buf[0];
	vcmctx->p2len = p2len;
	status = init_mgmt_vcrp((VCRPLANDEF *) &vcmctx->vcrp, 
	    VCRP$K_FC_ENABLE_PORT, vcmctx->p2len, (char **)&vcmctx->p2ptr);
    } else {
	status = init_mgmt_vcrp((VCRPLANDEF *)&vcmctx->vcrp, 
	    VCRP$K_FC_ENABLE_PORT, 0, 0);
    }

    if $VMS_STATUS_SUCCESS(status) {
	fork_lock(SPL$C_IOLOCK8, &saved_ipl);
	status = vci_mgmt_port((VCRPLANDEF *)&vcmctx->vcrp,
	    (VCIBDLLDEF *)&vcmctx->vcib);
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    }

    return status;
}

//
// Disable a port
//
int pcap$vcm_disable_port(VCMCTX_64P vcmctx)
{
    int status = SS$_NORMAL;
    int saved_ipl;

    status = init_mgmt_vcrp((VCRPLANDEF *)&vcmctx->vcrp, 
		VCRP$K_FC_DISABLE_PORT, 0, 0);

    if $VMS_STATUS_SUCCESS(status) {
	fork_lock(SPL$C_IOLOCK8, &saved_ipl);
	status = vci_mgmt_port((VCRPLANDEF *)&vcmctx->vcrp, 
		    (VCIBDLLDEF *)&vcmctx->vcib);
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    }

    return status;
}


//
// Get last management error.
//
int pcap$vcm_get_mgm_error(VCMCTX_64P vcmctx, CHAR_64P error)
{
    int status = SS$_NORMAL;
    VCRPDEF_64P vcrpptr;

    vcrpptr = (VCRPDEF_64P) &vcmctx->vcrp;
//    memcpy(error, &vcrpptr->vcrp$q_request_status, 8);

    status = vcrpptr->vcrp$l_request_status;

    return status;
}    


//
// Get last error.
//
int pcap$vcm_get_last_error(VCMCTX_64P vcmctx)
{
    int status;

    status = (int) vcmctx->last_error;

    return status;
}    


//
// Read a packet. This is as simple as removing a VCRP from the
// queue in the VCIB. 
//
int pcap$vcm_read_packet(VCMCTX_64P vcmctx, int len, CHAR_64P packet)
{
    int retry_count = 10;
    int status = SS$_NORMAL;
    int status2;
    int saved_ipl;
    int vcrpsize;
    VCRPDEF *vcrp;
    VCIBDEF *vcib;

    retry_count += pcapvcm->retry_count; 
    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    //
    // Remove from tail (FIFO style)
    //
    vcib = (VCIBDEF *)&vcmctx->vcib;
    do {
	status = __PAL_REMQTIL(vcib, (void *)&vcrp);
    } while (status <= 0 && retry_count-- > 0);

    //
    // If we couldn't remove entry from queue, say so
    //
    if (status < 0) {
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    	return SS$_NOTQUEUED;
    }

    //
    // If queue is empty, give up
    //
    if (status <= 0) {
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
	return SS$_NOSUCHOBJECT;
    }

    //
    // If this is the last entry in the queue, then indicate success
    //
    if (status == 2) {
	status = SS$_NORMAL;
    }

    if (vcib->vcib$w_size > 0) {
	vcib->vcib$w_size--;
    }

    vcrpsize = vcrp->vcrp$w_size;
    if (vcrpsize > len) {
	status = vci_delete_vcrp(vcrp);
	fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
	return SS$_INSFMEM;
    }

    //
    // Copy the entire VCRP
    //
    memcpy(packet, vcrp, vcrpsize);

    //
    // Get rid of VCRP
    //
    status2 = vci_delete_vcrp(vcrp);

    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);

    return status;
}

//
// Send of a packet. The packet is assumed to be formatted correctly, so we'll
// just put it in a VCRP and send it on its way...
//
int pcap$vcm_send_packet(VCMCTX_64P vcmctx, int hdrlen, int len,
    CHAR_64P rawpacket)
{
    int status;
    int saved_ipl;
    int vcrpsize;
    char *packptr;
    VCRPDEF *base;
    VCRPLANDEF *vcrp;
    char *reshdr;
    int built_hdrlen;
    int pdulen;

    //
    // If we have an outstanding transmit, give up.
    //
    if (vcmctx->transmit_pending) {
	return SS$_NOTHINGDONE;
    }
    vcmctx->transmit_pending = 1;
    vcmctx->transmit_vcrp = (VCRPDEF *) &vcmctx->vcrpbuf[0];
    vcrp = (VCRPLANDEF *) vcmctx->transmit_vcrp;
    memset(vcrp, 0, 4096);
    status = init_transmit_vcrp(vcrp);
    base = (VCRPDEF *) vcrp;

    //
    // Point to where we're going to put the packet
    //
    packptr = (char *) vcrp + VCRP$T_LAN_DATA + LAN$C_MAX_HDR_SIZE + 8;
    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    built_hdrlen = vci_build_header(packptr, &reshdr, 0, 0, (VCRPLANDEF *)&vcmctx->vcib);
    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);

    //
    // Save the port context address
    //
    base->vcrp$a_creator = (VCMCTX *)vcmctx;

    //
    // Build a frame
    //
    pdulen = (len-hdrlen) + built_hdrlen;
    base->vcrp$l_boff = (char *) reshdr - (char *) base;
    base->vcrp$l_bcnt = pdulen;
    base->vcrp$l_total_pdu_size = pdulen; 
    base->vcrp$w_size = VCRP$T_LAN_DATA + len;
    memcpy(&vcrp->vcrp$q_lan_t_dest, rawpacket, 6);
    //
    // Fiddle header
    //
    memcpy(reshdr, rawpacket, 6);	    // DA
    memcpy(&reshdr[6], &rawpacket[6], 6);   // SA
    memcpy(&reshdr[12], &rawpacket[12], 2); // PTY
    packptr = (char *) reshdr + built_hdrlen;
    memcpy(packptr, (rawpacket+hdrlen), len-hdrlen); 

    //
    // Send the frame
    //
    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    status = vci_transmit_frame((VCRPLANDEF *)vcmctx->transmit_vcrp, 
	(VCIBDLLDEF *)&vcmctx->vcib);
    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);
    
    //
    // If, by any chance, the transmit is complete, return the status
    //
    if (vcmctx->transmit_pending == 0) {
	status = (int) base->vcrp$q_request_status;
    } else {
	status = SS$_OPINPROG;
    }
    
    return status;
}


//
// Build a header and return it to the user
//
int pcap$vcm_build_header(VCMCTX_64P vcmctx, int len, CHAR_64P header)
{
    int status = SS$_NORMAL;
    char *reshdr;
    char *hdrptr;
    int hdrlen;
    int saved_ipl;

    //
    // Set up pointer to (the middle of) header
    //
    hdrptr = (char *)&vcmctx->hdr[64];

    //
    // Build the header
    //
    fork_lock(SPL$C_IOLOCK8, &saved_ipl);
    hdrlen = vci_build_header(hdrptr, &reshdr, 0, 0, (VCRPLANDEF *)&vcmctx->vcib);
    fork_unlock(SPL$C_IOLOCK8, saved_ipl, SMP_RESTORE);

    //
    // Copy header just built
    //
    if (hdrlen > len) {
	hdrlen = len;
    }

    memcpy(header, reshdr, hdrlen);

    return hdrlen;
}

//
// Retreive statistics for this VCI port
//
int pcap$vcm_get_statistics(VCMCTX_64P vcmctx, CHAR_64P stats)
{
    int status = SS$_NORMAL;
    PCAPSTAT *statptr;

    statptr = (PCAPSTAT *) stats;

    statptr->recv_packets = vcmctx->stat.recv_packets;
    statptr->recv_packets_dropped = vcmctx->stat.recv_packets_dropped;
    statptr->recv_queue_size = vcmctx->stat.recv_queue_size;
    statptr->tr_packets = vcmctx->stat.tr_packets;
    statptr->tr_failed = vcmctx->stat.tr_failed;

    return status;
}

