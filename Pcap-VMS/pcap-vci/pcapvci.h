#ifndef _PCAPVCI__H_
#define _PCAPVCI__H_
#include "pcapvcm.h"

typedef struct _ldr_handle
{
   void		*base_addr;
   LDRIMG	*ldrimg_ptr;
   int		seq_num;
} LDRHANDLE;


typedef struct _vcmhandle {
    PCAPVCM *pcapvcm;
    LDRHANDLE refhand;
} VCMHANDLE;


// VCI interface routines
int pcapvci_load_execlet();
//int pcapvci_unload_execlet();
int pcapvci_alloc_port(VCMCTX **ctx);
int pcapvci_free_port(VCMCTX *ctx);
int pcapvci_get_device(VCMCTX *ctx, char *device);
int pcapvci_create_port(VCMCTX *ctx, char *device);
int pcapvci_delete_port(VCMCTX *ctx);
int pcapvci_enable_port(VCMCTX *ctx, int p2len, char *p2buf);
int pcapvci_disable_port(VCMCTX *ctx);
int pcapvci_get_mgm_error(VCMCTX *ctx, uint64 *error);
int pcapvci_read_packet(VCMCTX *ctx, int packlen, char *packet);
int pcapvci_send_packet(VCMCTX *ctx, int hdrlen, int totlen, char *packet);
int pcapvci_get_trasmit_error(VCMCTX *ctx);
int pcapvci_get_statistics(VCMCTX *vcmctx, PCAPSTAT *statptr);
int pcapvci_set_recv_queue_size(int entries);
int pcapvci_get_recv_queue_size(void);

#endif /* _PCAPVCI__H_ */
