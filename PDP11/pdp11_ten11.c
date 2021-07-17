/* pdp11_ten11.c - Rubin 10-11 Memory Access facility

   Copyright (c) 2018, Mark Pizzolato

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

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   TEN11    Rubin 10-11 Memory Access facility

   Information necessary to create this simulation was gathered from
   Lar's Brinhoff:

   I/O Page Registers: none

   Vector: none

   Priority: none

   The Rubin 10-11 provides a means to allow the PDP-10 to read or 
   write individual words in PDP-11 memory using Unibus. The PDP-11s 
   can only access its own memory, not the PDP-10 memory.

*/

#if !defined (VM_PDP11)
#error "TEN11 is not supported!"
#endif
#include "pdp11_defs.h"

#include "sim_tmxr.h"

static t_stat ten11_svc (UNIT *uptr);
static t_stat ten11_conn_svc (UNIT *uptr);
static t_stat ten11_setmode (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat ten11_showmode (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat ten11_setpeer (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
static t_stat ten11_showpeer (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
static t_stat ten11_reset (DEVICE *dptr);
static t_stat ten11_attach (UNIT *uptr, CONST char *ptr);
static t_stat ten11_detach (UNIT *uptr);
static t_stat ten11_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *ten11_description (DEVICE *dptr);

#define UNIT_SHMEM (1u << UNIT_V_UF)
#define TEN11_POLL  100

static UNIT ten11_unit[2] = {
    { UDATA (&ten11_svc,        UNIT_IDLE|UNIT_ATTABLE, 0), TEN11_POLL },
    { UDATA (&ten11_conn_svc,   UNIT_DIS,               0) }
};

static UNIT *ten11_action_unit = &ten11_unit[0];
static UNIT *ten11_connection_unit = &ten11_unit[1];
#define PEERSIZE 512
static char ten11_peer[PEERSIZE];

static REG ten11_reg[] = {
    { DRDATAD  (POLL, ten11_unit[0].wait,   24,    "poll interval"), PV_LEFT },
    { BRDATA   (PEER, ten11_peer,    8,       8, PEERSIZE),          REG_HRO },
    { NULL }
};

static MTAB ten11_mod[] = {
    { MTAB_XTD|MTAB_VDV,          0, "MODE", "MODE={SHMEM|NETWORK}",
        &ten11_setmode, &ten11_showmode, NULL, "Display access mode" },
    { MTAB_XTD|MTAB_VDV,          0, "PEER", "PEER=address:port",
        &ten11_setpeer, &ten11_showpeer, NULL, "Display destination/source" },
    { 0 }
};


/* External Unibus interface. */
#define BUSNO           0
#define DATO            1
#define DATI            2
#define ACK             3
#define ERR             4

#define DBG_TRC         1
#define DBG_CMD         2

static DEBTAB ten11_debug[] = {
    {"TRACE",   DBG_TRC,    "Routine trace"},
    {"CMD",     DBG_CMD,    "Command Processing"},
    {0},
};

DEVICE ten11_dev = {
    "TEN11", ten11_unit, ten11_reg, ten11_mod,
    1, 8, 16, 2, 8, 16,
    NULL,                                               /* examine */
    NULL,                                               /* deposit */
    &ten11_reset,                                       /* reset */
    NULL,                                               /* boot */
    ten11_attach,                                       /* attach */
    ten11_detach,                                       /* detach */
    NULL,                                               /* context */
    DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_MUX,
    0,                                                  /* debug control */
    ten11_debug,                                        /* debug flags */
    NULL,                                               /* memory size chage */
    NULL,                                               /* logical name */
    NULL,                                               /* help */
    &ten11_attach_help,                                 /* attach help */
    NULL,                                               /* help context */
    &ten11_description,                                 /* description */
};

static TMLN ten11_ldsc;                                 /* line descriptor */
static TMXR ten11_desc = { 1, 0, 0, &ten11_ldsc };      /* mux descriptor */
#define PEERSIZE 512
static char ten11_peer[PEERSIZE];
SHMEM *pdp11_shmem = NULL;                              /* PDP11 shared memory info */


extern DEVICE cpu_dev;                                  /* access to examind and deposit routines */

static t_stat ten11_reset (DEVICE *dptr)
{
sim_debug(DBG_TRC, dptr, "ten11_reset()\n");

ten11_action_unit->flags |= UNIT_ATTABLE;
ten11_action_unit->action = ten11_svc;
ten11_connection_unit->flags |= UNIT_DIS | UNIT_IDLE;
ten11_connection_unit->action = ten11_conn_svc;
ten11_desc.packet = TRUE;
ten11_desc.notelnet = TRUE;
ten11_desc.buffered = 2048;

return SCPE_OK;
}

t_stat ten11_showpeer (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
if (ten11_peer[0])
    fprintf(st, "peer=%s", ten11_peer);
else
    fprintf(st, "peer=unspecified");
return SCPE_OK;
}

t_stat ten11_setmode (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
char gbuf[CBUFSIZE];

if ((!cptr) || (!*cptr))
    return SCPE_ARG;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
cptr = get_glyph (cptr, gbuf, 0);
if (0 == strcmp ("SHMEM", gbuf))
    uptr->flags |= UNIT_SHMEM;
else {
    if (0 == strcmp ("NETWORK", gbuf))
        uptr->flags &= ~UNIT_SHMEM;
    else
        return sim_messagef (SCPE_ARG, "Unknown mode: %s\n", gbuf);
    }
return SCPE_OK;
}

t_stat ten11_showmode (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
fprintf(st, "mode=%s", (uptr->flags & UNIT_SHMEM) ? "SHMEM" : "NETWORK");
return SCPE_OK;
}

t_stat ten11_setpeer (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
char host[PEERSIZE], port[PEERSIZE];

if ((!cptr) || (!*cptr))
    return SCPE_ARG;
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
if (uptr->flags & UNIT_SHMEM)
    return sim_messagef (SCPE_ARG, "Peer can't be specified in Shared Memory Mode\n");
if (sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL))
    return sim_messagef (SCPE_ARG, "Invalid Peer Specification: %s\n", cptr);
if (host[0] == '\0')
    return sim_messagef (SCPE_ARG, "Invalid/Missing host in Peer Specification: %s\n", cptr);
strncpy(ten11_peer, cptr, PEERSIZE-1);
return SCPE_OK;
}

static t_stat ten11_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;
char attach_string[512];

if (!cptr || !*cptr)
    return SCPE_ARG;
if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
if (uptr->flags & UNIT_SHMEM) {
    void *basead;

    r = sim_shmem_open (cptr, MAXMEMSIZE, &pdp11_shmem, &basead);
    if (r != SCPE_OK)
        return r;
    memcpy (basead, M, cpu_dev.units->capac);           /* copy normal memory */
    free (M);                                           /* release normal memory */
    M = (uint16 *)basead;
    }
else {
    if (ten11_peer[0] == '\0')
        return sim_messagef (SCPE_ARG, "Must specify peer before attach\n");
    sprintf (attach_string, "%s,Connect=%s", cptr, ten11_peer);
    r = tmxr_attach_ex (&ten11_desc, uptr, attach_string, FALSE);
    if (r != SCPE_OK)                                       /* error? */
        return r;
    sim_activate_after (ten11_connection_unit, 1000000);    /* start poll */
    }
uptr->flags |= UNIT_ATT;
return SCPE_OK;
}

static t_stat ten11_detach (UNIT *uptr)
{
t_stat r;

if (!(uptr->flags & UNIT_ATT))                      /* attached? */
    return SCPE_OK;
if (uptr->flags & UNIT_SHMEM) {
    uint16 *new_M = malloc (cpu_dev.units->capac);
    memcpy (new_M, M, cpu_dev.units->capac);
    M = new_M;
    sim_shmem_close (pdp11_shmem);
    r = SCPE_OK;
    }
else {
    sim_cancel (uptr);
    sim_cancel (ten11_connection_unit);             /* stop connection poll as well */
    r = tmxr_detach (&ten11_desc, uptr);
    }
uptr->flags &= ~UNIT_ATT;
free (uptr->filename);
uptr->filename = NULL;
return r;
}

static void build (uint8 *request, uint8 octet)
{
request[0]++;
request[request[0]] = octet;
}


static t_stat ten11_svc (UNIT *uptr)
{
const uint8 *ten11_request;
uint8 ten11_response[10];
size_t size;
t_stat stat;
t_addr addr;
t_value data;

sim_debug(DBG_TRC, &ten11_dev, "ten11_svc()\n");

stat = tmxr_get_packet_ln (&ten11_ldsc, &ten11_request, &size);

if (stat == SCPE_OK) {
    if (ten11_request != NULL) {
        switch (ten11_request[1]) {
            case DATO:
                if (ten11_request[0] != 6)
                    return sim_messagef (SCPE_IERR, "Protocol error - unexpected DATO request length: %d", ten11_request[0]);
                addr = ten11_request[2];
                addr |= ten11_request[3] << 8;
                addr |= ten11_request[4] << 16;
                data = ten11_request[5] | (ten11_request[6] << 8);
                stat = cpu_dev.deposit (ten11_request[5] + (ten11_request[6] << 8), addr, 0, 0);
                sim_debug (DBG_CMD, &ten11_dev, "Write: %06o <- %06o - %d - %s\n", (int)addr, (int)data, stat, sim_error_text (stat));
                if (stat == SCPE_OK)
                    build (ten11_response, ACK);
                else {
                    sim_printf ("TEN11: DATO error: %06o - %d - %s\n", addr, stat, sim_error_text (stat));
                    build (ten11_response, ERR);
                    build (ten11_response, stat & 0377);
                    build (ten11_response, (stat >> 8) & 0377);
                    build (ten11_response, (stat >> 16) & 0377);
                    build (ten11_response, (stat >> 24) & 0377);
                    }
                break;
            case DATI:
                if (ten11_request[0] != 4)
                    return sim_messagef (SCPE_IERR, "Protocol error - unexpected DATI request length: %d", ten11_request[0]);
                addr = ten11_request[2];
                addr |= ten11_request[3] << 8;
                addr |= ten11_request[4] << 16;
                stat = cpu_dev.examine (&data, addr, 0, 0);
                sim_debug (DBG_CMD, &ten11_dev, "Read: %06o = %06o - %d - %s\n", (int)addr, (int)data, stat, sim_error_text (stat));
                if (stat == SCPE_OK) {
                    build (ten11_response, ACK);
                    build (ten11_response, data & 0377);
                    build (ten11_response, (data >> 8) & 0377);
                } else {
                    sim_printf ("TEN11: DATI error: %06o - %d - %s\n", addr, stat, sim_error_text (stat));
                    build (ten11_response, ERR);
                    build (ten11_response, stat & 0377);
                    build (ten11_response, (stat >> 8) & 0377);
                    build (ten11_response, (stat >> 16) & 0377);
                    build (ten11_response, (stat >> 24) & 0377);
                }
                break;
            default:
                return sim_messagef (SCPE_IERR, "Protocol error - unexpected DATO request type: %d", ten11_request[0]);
            }
            stat = tmxr_put_packet_ln (&ten11_ldsc, ten11_response, (size_t)ten11_response[0] + 1);
        }
    sim_activate (uptr, uptr->wait);    /* Reschedule */
    }
return SCPE_OK;
}

static t_stat ten11_conn_svc (UNIT *uptr)
{
int32 newconn;

sim_debug(DBG_TRC, &ten11_dev, "ten11_conn_svc()\n");

newconn = tmxr_poll_conn(&ten11_desc);      /* poll for a connection */
if (newconn >= 0) {                         /* got a live one? */
    sim_activate (ten11_action_unit, ten11_action_unit->wait);  /* Start activity poll */
    }
sim_activate_after (uptr, 1000000);
return SCPE_OK;
}


static t_stat ten11_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The %D device is an implementation of the Rubin 10-11 PDP-10 to PDP-11\n"
    " Memory Access facility.  This allows a PDP 10 system to reach into a\n"
    " PDP-11 simulator and modify or access the contents of the PDP-11 memory.\n"
    "1 Configuration\n"
    " A %D device is configured with various simh SET and ATTACH commands\n"
    "2 $Set commands\n"
    "3 Mode\n"
    " To memory access mode.  Options are SHMEM for Shared Memory access and\n"
    " NETWORK for network access.  This can be configured with the\n"
    " following command:\n"
    "\n"
    "+sim> SET %U MODE=SHMEM\n"
    "+OR\n"
    "+sim> SET %U MODE=NETWORK\n"
    "3 Peer\n"
    " When the memory access mode is specified as NETWORK mode, the peer system's\n"
    " host and port to that data is to be transmitted across is specified by\n"
    " using the following command:\n"
    "\n"
    "+sim> SET %U PEER=host:port\n"
    "2 Attach\n"
    " When in SHMEM shared memory access mode, the device must be attached\n"
    " using an attach command which specifies the shared object name that\n"
    " the peer system will be using:\n"
    "\n"
    "+sim> ATTACH %U SharedObjectName\n"
    "\n"
    " When in NETWORK memory access mode, the device must be attached to a\n"
    " receive port, this is done by using the ATTACH command to specify\n"
    " the receive port number.\n"
    "\n"
    "+sim> ATTACH %U port\n"
    "\n"
    " The Peer host:port value must be specified before the attach command.\n"
    " The default connection uses TCP transport between the local system and\n"
    " the peer.  Alternatively, UDP can be used by specifying UDP on the\n"
    " ATTACH command:\n"
    "\n"
    "+sim> ATTACH %U port,UDP\n"
    "\n"
    "2 Examples\n"
    " To configure two simulators to talk to each other using in Network memory\n"
    " access mode, follow this example:\n"
    " \n"
    " Machine 1\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %U PEER=LOCALHOST:2222\n"
    "+sim> ATTACH %U 1111\n"
    " \n"
    " Machine 2\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %U PEER=LOCALHOST:1111\n"
    "+sim> ATTACH %U 2222\n"
    "\n"
    " To configure two simulators to talk to each other using SHMEM shared memory\n"
    " access mode, follow this example:\n"
    " \n"
    " Machine 1\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %D MODE=SHMEM\n"
    "+sim> ATTACH %U PDP11-1-Core\n"
    " \n"
    " Machine 2\n"
    "+sim> SET %D ENABLE\n"
    "+sim> SET %D MODE=SHMEM\n"
    "+sim> ATTACH %U PDP11-1-Core\n"
    "\n"
    "\n"
    ;

return scp_help (st, dptr, uptr, flag, helpString, cptr);
return SCPE_OK;
}


static const char *ten11_description (DEVICE *dptr)
{
return "Rubin 10-11 PDP-10 to PDP-11 Memory Access";
}
