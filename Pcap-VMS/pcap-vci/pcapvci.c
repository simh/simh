#pragma module PCAPVCI "X-1"
/*
**++
**  FACILITY:  PCAP
**
**  MODULE DESCRIPTION:
**
**      Interface routines to the PCAP VCM execlet.
**
**  AUTHORS:
**
**      Ankan
**
**  CREATION DATE:  30-Mar-2003
**
**  DESIGN ISSUES:
**
** Important Note:
**
**     This code module uses privileged OpenVMS interfaces.
**     OpenVMS does not guarantee that these interfaces will
**     be supported indefinitely, and may change these interfaces
**     without prior notice.
**
**
**  MODIFICATION HISTORY:
**
**      X-1	Ankan					    25-Oct-2003
**		Lock/unlock data prior to passing it to execlet.
**		Return proper status from enable/disable port.
**--
*/
#include <descrip.h>
#include <starlet.h>
#include <ssdef.h>
#include <stsdef.h>
#include <ldrdef.h>
#include <ldrimgdef.h>
#include <pdscdef.h>
#include <nmadef.h>
#include <string.h>

#include "pcapvci.h"

typedef struct lkpdef LKP;


//
// Loader routines, used to dynamically load or locate the execlet.
//
extern int LDR$LOAD_IMAGE (struct dsc$descriptor_s *execlet_name, int flags,
                           LDRHANDLE *reference_handle);
extern int LDR$REF_INFO (struct dsc$descriptor_s *execlet_name,
			 LDRHANDLE *reference_handle);
extern int LDR$UNLOAD_IMAGE (struct dsc$descriptor_s *execlet_name,
			 LDRHANDLE *reference_handle);

int	load_execlet();
int	unload_execlet();

//
// Global data
//
static PCAPVCM *pcapvcm = 0;
LDRHANDLE reference_handle;	
void *rtnptr = 0;
$DESCRIPTOR(execlet_name, "PCAPVCM");
int is_loaded = 0;

//
// Return packet portion of VCRP
//
int get_packet(VCRPLANDEF *vcrp, char **hdrp)
{
    VCRPDEF *vcrpbase;
    uint64 boff;
    int len;

    vcrpbase = (VCRPDEF *) vcrp;
    boff = vcrpbase->vcrp$l_boff;
    len = vcrpbase->vcrp$l_bcnt; 
    *hdrp = (char *) (((char *) vcrp) + boff);

    return len;
}

//
// Load or locate the execelet
//
int pcapvci_load_execlet()
{
    int status;
    int arglist[2];

    arglist[0] = 0;
    status = sys$cmkrnl(load_execlet,arglist);

    return status;
}


//
// Allocate a VCI port
//
int pcapvci_alloc_port(VCMCTX **ctx)
{
    int status;
    int status2;
    uint64 arglist[2];
    int64 retlen;
    void *retaddr;

    status = sys$lckpag_64(ctx, 1, 3, &retaddr, &retlen);

    arglist[0] = 1;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->alloc_port, arglist);

    status2 = sys$ulkpag_64(retaddr, retlen, 3, &retaddr, &retlen);

    return status;
}

//
// Free a port
//
int pcapvci_free_port(VCMCTX *ctx)
{
    int status;
    uint64 arglist[2];

    arglist[0] = 1;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->free_port, arglist);

    return status;
}


//
// Get VCI devices
//
int pcapvci_get_device(VCMCTX *ctx, char *device)
{
    int status;
    int status2;
    uint64 arglist[3];
    int64 retlen;
    void *retaddr;

    status = sys$lckpag_64(device, 1, 0, &retaddr, &retlen);

    arglist[0] = 2;
    arglist[1] = (uint64) ctx;
    arglist[2] = (uint64) device;
    status = sys$cmkrnl_64(pcapvcm->get_device, arglist);

    status2 = sys$ulkpag_64(retaddr, retlen, 0, &retaddr, &retlen);

    return status;
}


//
// Create a VCI port
//
int pcapvci_create_port(VCMCTX *ctx, char *device)
{
    int status;
    int status2;
    uint64 arglist[3];
    int64 retlen;
    void *retaddr;

    status = sys$lckpag_64(device, 1, 0, &retaddr, &retlen);
    arglist[0] = 2;
    arglist[1] = (uint64) ctx;
    arglist[2] = (uint64) device;
    status = sys$cmkrnl_64(pcapvcm->create_port, arglist);

    status2 = sys$ulkpag_64(retaddr, retlen, 0, &retaddr, &retlen);
    return status;
}

//
// Delete a port
//
int pcapvci_delete_port(VCMCTX *ctx)
{
    int status;
    uint64 arglist[2];

    arglist[0] = 1;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->delete_port, arglist);

    return status;
}


//
// Get port management error
//
int pcapvci_get_mgm_error(VCMCTX *ctx, uint64 *error)
{
    int status;
    int status2;
    uint64 arglist[3];
    int64 retlen;
    void *retaddr;

    status = sys$lckpag_64(error, 1, 0, &retaddr, &retlen);

    arglist[0] = 2;
    arglist[1] = (uint64) ctx;
    arglist[2] = (uint64) error;
    status = sys$cmkrnl_64(pcapvcm->get_mgm_error, arglist);

    status2 = sys$ulkpag_64(retaddr, retlen, 0, &retaddr, &retlen);

    return status;
}


//
// Enable a port, this requires a P2 buffer as described in
// the I/O User's Reference Manual.
//
int pcapvci_enable_port(VCMCTX *ctx, int p2len, char *p2buf)
{
    int status;
    int status2;
    uint64 arglist[4];
    int64 retlen;
    void *retaddr;
    uint64 vcierr;

    // Lock P2 buffer
    status = sys$lkwset_64(p2buf, p2len, 0, &retaddr, &retlen);
    if (!$VMS_STATUS_SUCCESS(status)) {
	return 0;
    }
    
    arglist[0] = 3;
    arglist[1] = (uint64) ctx;
    arglist[2] = p2len;
    arglist[3] = (uint64) p2buf;
    status = sys$cmkrnl_64(pcapvcm->enable_port, arglist);
    status2 = sys$ulwset_64(retaddr, retlen, 0, &retaddr, &retlen);
    status = pcapvci_get_mgm_error(ctx, &vcierr);
    return status;
}

//
// Disable a port
//
int pcapvci_disable_port(VCMCTX *ctx)
{
    int status;
    uint64 arglist[2];
    uint64 vcierr;

    arglist[0] = 1;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->disable_port, arglist);

    status = pcapvci_get_mgm_error(ctx, &vcierr);

    return status;
}


//
// Read a packet, notice we only return the packet, not the entire VCRP
//
int pcapvci_read_packet(VCMCTX *ctx, int packlen, char *packet)
{
    int status;
    int status2;
    uint64 arglist[4];
    char vcrp[4096];
    int len;
    char *ptr;
    int64 retlen;
    void *retaddr;

    // Lock return buffer
    status = sys$lkwset_64(vcrp, sizeof(vcrp), 0, &retaddr, &retlen);
    if (!$VMS_STATUS_SUCCESS(status)) {
	return 0;
    }

    arglist[0] = 3;
    arglist[1] = (uint64) ctx;
    arglist[2] = 4096;
    arglist[3] = (uint64) vcrp;
    status = sys$cmkrnl_64(pcapvcm->read_packet, arglist);

    // Unlock return buffer
    status2 = sys$ulwset_64(retaddr, retlen, 0, &retaddr, &retlen);

    if (!$VMS_STATUS_SUCCESS(status)) {
	return 0;
    }

    //
    // Locate packet in VCRP
    //
    len = get_packet((VCRPLANDEF *)vcrp, &ptr);

    //
    // Copy packet in users buffer, check if we need to truncate.
    //
    if (len > packlen) {
	memcpy(packet, ptr, packlen);
    } else {
	memcpy(packet, ptr, len);
    }

    return len;
}


//
// Send a packet. Lock packet prior to sending
//
int pcapvci_send_packet(VCMCTX *ctx, int hdrlen, int totlen, char *packet)
{
    int status;
    int status2;
    uint64 arglist[5];
    int64 retlen;
    void *retaddr;

    // Lock packet
    status = sys$lkwset_64(packet, totlen, 0, &retaddr, &retlen);
    if (!$VMS_STATUS_SUCCESS(status)) {
	return status;
    }

    arglist[0] = 4;
    arglist[1] = (uint64) ctx;
    arglist[2] = hdrlen;
    arglist[3] = totlen;
    arglist[4] = (uint64) packet;
    status = sys$cmkrnl_64(pcapvcm->send_packet, arglist);

    // Unlock packet
    status2 = sys$ulwset_64(retaddr, retlen, 0, &retaddr, &retlen);

    return status;
}


//
// Get last transmit error
//
int pcapvci_get_trasmit_error(VCMCTX *ctx)
{
    int status;
    uint64 arglist[2];

    arglist[0] = 2;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->get_last_error, arglist);

    return status;
}


//
// Get statistics from execlet
//
int pcapvci_get_statistics(VCMCTX *vcmctx, PCAPSTAT *statptr)
{
    int status;
    int status2;
    uint64 arglist[3];
    int64 retlen;
    void *retaddr;

    status = sys$lkwset_64(statptr, sizeof(PCAPSTAT), 0, &retaddr, &retlen);
    if (!$VMS_STATUS_SUCCESS(status)) {
	return status;
    }

    arglist[0] = 2;
    arglist[1] = (uint64) vcmctx;
    arglist[2] = (uint64) statptr;
    status = sys$cmkrnl_64(pcapvcm->get_statistics, arglist);

    status2 = sys$ulwset_64(retaddr, retlen, 0, &retaddr, &retlen);

    return status;
}


//
// Set the size of the receive queue
//
int pcapvci_set_recv_queue_size(int entries)
{
    int status;

    if (entries >= PCAPVCM$K_RECV_MIN_QUEUE_SIZE && 
	entries <= PCAPVCM$K_RECV_MAX_QUEUE_SIZE) {
	pcapvcm->recv_queue_size = entries;
	return SS$_NORMAL;
    } else {
	return SS$_BADPARAM;
    }
}


//
// Get receive queue size
//
int pcapvci_get_recv_queue_size(void)
{
    return pcapvcm->recv_queue_size;
}



//
// Load the execlet and get the execlet context block
//
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

