#ifndef SIM_SLIRP_H
#define SIM_SLIRP_H

#if defined(HAVE_SLIRP_NETWORK)

#include "sim_defs.h"
typedef struct sim_slirp SLIRP;

typedef void (*packet_callback)(void *opaque, const unsigned char *buf, int len);

SLIRP *sim_slirp_open (const char *args, void *opaque, packet_callback callback, DEVICE *dptr, uint32 dbit);
void sim_slirp_close (SLIRP *slirp);
int sim_slirp_send (SLIRP *slirp, const char *msg, size_t len, int flags);
int sim_slirp_select (SLIRP *slirp, int ms_timeout);
void sim_slirp_dispatch (SLIRP *slirp);
t_stat sim_slirp_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
void sim_slirp_show (SLIRP *slirp, FILE *st);

#endif /* HAVE_SLIRP_NETWORK */

#endif
