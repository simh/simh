/* pcapvcm.h - packet capturing execlet

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
//      {@tbs@}
//
// REVISION HISTORY:
//
//      X-1     Ankan			Anders Ahgren		21-Mar-2003
//              Initial version.
//
#ifndef __PCAPVCM_H_
#define __PCAPVCM_H_
#include <lanudef.h>
#include <ldcdef.h>
#include <vcibdef.h>
#include <vcibdlldef.h>
#include <vcrpdef.h>
#include <vcrplandef.h>
#include <lildef.h>
#include <dyndef.h>
#include <ints.h>

typedef struct vcrpdef VCRPDEF;
typedef struct vcibdef VCIBDEF;

#define PCAPVCM$K_REVISION 1
#define PCAPVCM$K_RECV_QUEUE_SIZE 32
#define PCAPVCM$K_RECV_MIN_QUEUE_SIZE 1
#define PCAPVCM$K_RECV_MAX_QUEUE_SIZE 255
#define PCAPVCM$K_RECV_QUEUE_RETRY 16

// LIL stuff
#define PCAP_LIL_SIZE 512
typedef struct _lil_item {
    short len;
    short tag;
    char  val;
} LILITEM;

#define INIT_LIL(lil, len) \
    { \
    (lil)->lil$l_listlen = 0; \
    (lil)->lil$a_listadr = (char *) (lil) + LIL$T_DATA; \
    (lil)->lil$w_size = len + sizeof(LILDEF); \
    (lil)->lil$b_type = DYN$C_DECNET; \
    (lil)->lil$b_subtype = DYN$C_NET_ITEM; \
    }

#define ADD_LIL_ITEM(lil, tag, len, val) \
    add_lil_item(lil, len, tag, val);

#define ADD_LIL_ADDR_VAL(lil, len, tag, val) \
    add_lil_addr_value(lil, tag, len, val);

// LIL prototypes for the above
void add_lil_item(LILDEF *lil, int len, int tag, char *value);
void add_lil_addr_value(LILDEF *lil, int len, int tag, char *value);

// LAN P2 buffer stuff
#define ADD_INT_VAL(buf, code, val) \
    buf = add_int_value(buf, code, val);

#define ADD_CNT_VAL(buf, code, len, val) \
    buf = add_counted_value(buf, code, len, val);

// LAN P2 helper prototypes
char *add_int_value(char *buf, short code, int value);
char *add_counted_value(char *buf, short code, short len, char *value);
int find_value(int buflen, char *buf, short code, char *retbuf);

// VCIB helpers
int init_vcib(VCIBDLLDEF *vcib, LILDEF *lil);

// VCRP helpers
int init_mgmt_vcrp(VCRPLANDEF *vcrplan, int func, int p2len, char **p2buf);
int init_transmit_vcrp(VCRPLANDEF *vcrplan);

// Pointer tricks, since we give and take from user mode...
#pragma __required_pointer_size __save
#pragma __required_pointer_size __long  
typedef char * CHAR_64P;
typedef void * VOID_64P;
typedef LILDEF * LILDEF_64P;
typedef VCRPDEF * VCRPDEF_64P;
#pragma __required_pointer_size __short
typedef LILDEF * LILDEF_32P;
typedef char * CHAR_32P;
#pragma __required_pointer_size __restore

// Statistics...
typedef struct _pcapstat {
    long recv_packets;
    long recv_packets_dropped;
    long recv_queue_size;
    long tr_packets;
    long tr_failed;
} PCAPSTAT;

// Shared structure
typedef struct _pcapvcm {
    unsigned short int mbo;
    unsigned char type;
    unsigned char subtype;
    int size;
    int revision;
    int recv_queue_size;
    int curr_recv_queue_size;
    int retry_count;
    int last_mgm_event;		    // Last management event
    int (*get_context)();
    int (*unload_execlet)();
    int (*get_device)();
    int (*alloc_port)();
    int (*free_port)();
    int (*create_port)();
    int (*delete_port)();
    int (*enable_port)();
    int (*disable_port)();
    int (*get_mgm_error)();
    int (*get_last_error)();
    int (*read_packet)();
    int (*send_packet)();
    int (*build_header)();
    int (*get_statistics)();
} PCAPVCM;

// Our private VCIB definition, we need a context block in there
typedef struct __pcapvcib {
    VCIBDLLDEF vcib;
    struct _vcmctx *vcmctx;
} PCAPVCIB;

#pragma member_alignment __save
#pragma nomember_alignment __quadword
// Per client context
typedef struct _vcmctx {
    PCAPVCIB vcib;
    VCRPLANDEF vcrp;
    VCRPDEF *transmit_vcrp;
    int transmit_vcrp_size;
    int recv_queue_size;
    uint32 flags;
    uint32 transmit_pending;
    uint64 size;
    uint64 last_error;
    struct _ldcdef ldc;
    uint32 ldcid;
    char devbuf[128];
    LILDEF_64P lil;
    char lilbuf[sizeof(LILDEF)+PCAP_LIL_SIZE];
    char *hdrptr;
    char hdr[128];
    int p2len;
    char *p2ptr;
    char p2_buf[128];				// P2 buffer
    char vcrpbuf[4096];				// VCRP buffer (for transmit)
    PCAPSTAT stat;				// Statistics
} VCMCTX;
#pragma member_alignment __restore

//...and more pointer tricks
#pragma __required_pointer_size __save
#pragma __required_pointer_size __long  
typedef PCAPVCM * PCAPVCM_64P;
typedef PCAPVCM ** PCAPVCM_64PP;
typedef VCMCTX * VCMCTX_64P;
typedef VCMCTX ** VCMCTX_64PP;
#pragma __required_pointer_size __restore


// VCI jacket routines. These are written in MACRO, due to JSBs
extern int vci_get_device(uint32 *, LDCDEF **);
extern int vci_create_port(VCIBDLLDEF *vcib);
extern int vci_delete_port(VCIBDLLDEF *vcib);
extern int vci_delete_vcrp(VCRPDEF *vcrp);
extern int vci_mgmt_port(VCRPLANDEF *vcrp, VCIBDLLDEF *vcib);
extern int vci_transmit_frame(VCRPLANDEF *vcrp, VCIBDLLDEF *vcib);
extern int vci_build_header(char *header, char **reshdr, int *x802,
    int *r802, VCRPLANDEF *vcrp);
#endif /* __PCAPVCM_H_ */
