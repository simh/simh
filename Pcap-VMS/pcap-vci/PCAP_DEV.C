#include <stdio.h>
#include <stsdef.h>
#include <ssdef.h>
#include <ldrimgdef.h>
#include <ldr_routines.h>
#include "pcapvci.h"

int main(int argc, char *argv[])
{
    int status;
    char devnam[128];
    VCMCTX *vcmctx;

    // Make sure execlet is loaded
    status = pcapvci_load_execlet();
    if ($VMS_STATUS_SUCCESS(status)) {
        // Get us a port
        status = pcapvci_alloc_port(&vcmctx);
        if ($VMS_STATUS_SUCCESS(status)) {
            while ($VMS_STATUS_SUCCESS(status)) {
                status = pcapvci_get_device(vcmctx, devnam);
                if ($VMS_STATUS_SUCCESS(status)) {
                    printf("device: %s\n", devnam);
                }
            }
            status = pcapvci_free_port(vcmctx);
        }
    }

    return 0;
}

