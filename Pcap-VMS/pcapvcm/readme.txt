This is a VMS execlet which implements a VMS Communications Module (VCM)
for the VMS Communications Interface (VCI)

In order to use the execlet it must be placed in SYS$LOADABLE_IMAGES and
must be loaded with the dynamic loader in order to get the address of
the context block used to communicate with the execlet. This is
accomplished as follows:

...
static PCAPVCM *pcapvcm = 0;
LDRHANDLE reference_handle;	
void *rtnptr = 0;
$DESCRIPTOR(execlet_name, "PCAPVCM");
int is_loaded = 0;
....
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

The execlet is callable by means of using sys$cmkrl_64, for instance the
following allocates a VCI port:

//
// Tell the PCAPVCM execlet to allocate a VCI port
//
int pcapvci_alloc_port(VCMCTX **ctx)
{
    int status;
    uint64 arglist[2];

    arglist[0] = 1;
    arglist[1] = (uint64) ctx;
    status = sys$cmkrnl_64(pcapvcm->alloc_port, arglist);

    return status;
}

For more information on how to use the execlet, see the pcap_client.c module.

There is also a set of C jacket routines for the PCAPVCM execlet in the
PCAP-VCI directory all in the PCAPVCI.C module.

Notice that the execlet does *not* check for invalid data in anyway. If
invalid data, such as a bad pointer, is passed to the execlet the system
will crash.

It is also not posible to unload the execlet once it is built, due to the
somewhat incorrect way in which it is built. If you don't like it fix it.

If you enhance this execlet please let me know at ankan@hp.com, thanks.


