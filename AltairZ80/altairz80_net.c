/*  altairz80_net.c: networking capability

    Copyright (c) 2002-2014, Peter Schorn

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.
*/

#include "altairz80_defs.h"
#include "sim_sock.h"

/* Debug flags */
#define ACCEPT_MSG  (1 << 0)
#define DROP_MSG    (1 << 1)
#define IN_MSG      (1 << 2)
#define OUT_MSG     (1 << 3)

extern uint32 PCX;

#define UNIT_V_SERVER   (UNIT_V_UF + 0) /* define machine as a server   */
#define UNIT_SERVER     (1 << UNIT_V_SERVER)
#define NET_INIT_POLL_SERVER  16000
#define NET_INIT_POLL_CLIENT  15000

static t_stat net_attach    (UNIT *uptr, char *cptr);
static t_stat net_detach    (UNIT *uptr);
static t_stat net_reset     (DEVICE *dptr);
static t_stat net_svc       (UNIT *uptr);
static t_stat set_net       (UNIT *uptr, int32 value, char *cptr, void *desc);
int32 netStatus             (const int32 port, const int32 io, const int32 data);
int32 netData               (const int32 port, const int32 io, const int32 data);

extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32), uint8 unmap);

#define MAX_CONNECTIONS 2   /* maximal number of server connections */
#define BUFFER_LENGTH   512 /* length of input and output buffer    */

static struct {
    int32   Z80StatusPort;              /* Z80 status port associated with this ioSocket, read only             */
    int32   Z80DataPort;                /* Z80 data port associated with this ioSocket, read only               */
    SOCKET  masterSocket;               /* server master socket, only defined at [1]                            */
    SOCKET  ioSocket;                   /* accepted server socket or connected client socket, 0 iff free        */
    char    inputBuffer[BUFFER_LENGTH]; /* buffer for input characters read from ioSocket                       */
    int32   inputPosRead;               /* position of next character to read from buffer                       */
    int32   inputPosWrite;              /* position of next character to append to input buffer from ioSocket   */
    int32   inputSize;                  /* number of characters in circular input buffer                        */
    char    outputBuffer[BUFFER_LENGTH];/* buffer for output characters to be written to ioSocket               */
    int32   outputPosRead;              /* position of next character to write to ioSocket                      */
    int32   outputPosWrite;             /* position of next character to append to output buffer                */
    int32   outputSize;                 /* number of characters in circular output buffer                       */
} serviceDescriptor[MAX_CONNECTIONS + 1] = {    /* serviceDescriptor[0] holds the information for a client      */
/*  stat    dat ms  ios in      inPR    inPW    inS out     outPR   outPW   outS */
    {0x32,  0x33, 0,  0,  {0},    0,      0,      0,  {0},    0,      0,      0}, /* client Z80 port 50 and 51  */
    {0x28,  0x29, 0,  0,  {0},    0,      0,      0,  {0},    0,      0,      0}, /* server Z80 port 40 and 41  */
    {0x2a,  0x2b, 0,  0,  {0},    0,      0,      0,  {0},    0,      0,      0}  /* server Z80 port 42 and 43  */
};

static UNIT net_unit = {
    UDATA (&net_svc, UNIT_ATTABLE, 0),
    0,  /* wait, set in attach  */
    0,  /* u3, unused			*/
    0,  /* u4, unused			*/
    0,  /* u5, unused           */
    0,  /* u6, unused           */
};

static REG net_reg[] = {
    { DRDATA (POLL,         net_unit.wait,  32)             },
    { NULL }
};

static MTAB net_mod[] = {
    { UNIT_SERVER, 0,           "CLIENT", "CLIENT", &set_net}, /* machine is a client   */
    { UNIT_SERVER, UNIT_SERVER, "SERVER", "SERVER", &set_net}, /* machine is a server   */
    { 0 }
};

/* Debug Flags */
static DEBTAB net_dt[] = {
    { "ACCEPT", ACCEPT_MSG  },
    { "DROP",   DROP_MSG    },
    { "IN",     IN_MSG      },
    { "OUT",    OUT_MSG     },
    { NULL,     0 }
};

DEVICE net_dev = {
    "NET", &net_unit, net_reg, net_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &net_reset,
    NULL, &net_attach, &net_detach,
    NULL, (DEV_DISABLE | DEV_DEBUG), 0,
    net_dt, NULL, "Network NET"
};

static t_stat set_net(UNIT *uptr, int32 value, char *cptr, void *desc) {
    char temp[CBUFSIZE];
    if ((net_unit.flags & UNIT_ATT) && ((net_unit.flags & UNIT_SERVER) != (uint32)value)) {
        strncpy(temp, net_unit.filename, CBUFSIZE); /* save name for later attach */
        net_detach(&net_unit);
        net_unit.flags ^= UNIT_SERVER; /* now switch from client to server and vice versa */
        net_attach(uptr, temp);
        return SCPE_OK;
    }
    return SCPE_OK;
}

static void serviceDescriptor_reset(const uint32 i) {
    serviceDescriptor[i].inputPosRead   = 0;
    serviceDescriptor[i].inputPosWrite  = 0;
    serviceDescriptor[i].inputSize      = 0;
    serviceDescriptor[i].outputPosRead  = 0;
    serviceDescriptor[i].outputPosWrite = 0;
    serviceDescriptor[i].outputSize     = 0;
}

static t_stat net_reset(DEVICE *dptr) {
    uint32 i;
    if (net_unit.flags & UNIT_ATT)
        sim_activate(&net_unit, net_unit.wait); /* start poll */
    for (i = 0; i <= MAX_CONNECTIONS; i++) {
        serviceDescriptor_reset(i);
        sim_map_resource(serviceDescriptor[i].Z80StatusPort, 1,
                         RESOURCE_TYPE_IO, &netStatus, dptr->flags & DEV_DIS);
        sim_map_resource(serviceDescriptor[i].Z80DataPort, 1,
                         RESOURCE_TYPE_IO, &netData, dptr->flags & DEV_DIS);
    }
    return SCPE_OK;
}

static t_stat net_attach(UNIT *uptr, char *cptr) {
    uint32 i;
    char host[CBUFSIZE], port[CBUFSIZE];
    t_stat r;

    r = sim_parse_addr (cptr, host, sizeof(host), "localhost", port, sizeof(port), "3000", NULL);
    if (r != SCPE_OK)
        return SCPE_ARG;
    net_reset(&net_dev);
    for (i = 0; i <= MAX_CONNECTIONS; i++)
        serviceDescriptor[i].ioSocket = 0;
    if (net_unit.flags & UNIT_SERVER) {
        net_unit.wait = NET_INIT_POLL_SERVER;
        serviceDescriptor[1].masterSocket = sim_master_sock(cptr, NULL);
        if (serviceDescriptor[1].masterSocket == INVALID_SOCKET)
            return SCPE_IOERR;
    }
    else {
        net_unit.wait = NET_INIT_POLL_CLIENT;
        serviceDescriptor[0].ioSocket = sim_connect_sock(cptr, "localhost", "3000");
        if (serviceDescriptor[0].ioSocket == INVALID_SOCKET)
            return SCPE_IOERR;
    }
    net_unit.flags |= UNIT_ATT;
    net_unit.filename = (char *) calloc(1, strlen(cptr)+1);         /* alloc name buf */
    if (net_unit.filename == NULL)
        return SCPE_MEM;
    strcpy(net_unit.filename, cptr);                                /* save name */
    return SCPE_OK;
}

static t_stat net_detach(UNIT *uptr) {
    uint32 i;
    if (!(net_unit.flags & UNIT_ATT))
        return SCPE_OK;       /* if not attached simply return */
    if (net_unit.flags & UNIT_SERVER)
        sim_close_sock(serviceDescriptor[1].masterSocket, TRUE);
    for (i = 0; i <= MAX_CONNECTIONS; i++)
        if (serviceDescriptor[i].ioSocket)
            sim_close_sock(serviceDescriptor[i].ioSocket, FALSE);
    free(net_unit.filename);                                /* free port string */
    net_unit.filename = NULL;
    net_unit.flags &= ~UNIT_ATT;                            /* not attached */
    return SCPE_OK;
}

/* cannot use sim_check_conn to check whether read will return an error */
static t_stat net_svc(UNIT *uptr) {
    int32 i, j, k, r;
    SOCKET s;
    static char svcBuffer[BUFFER_LENGTH];
    if (net_unit.flags & UNIT_ATT) { /* cannot remove due to following else */
        sim_activate(&net_unit, net_unit.wait);             /* continue poll */
        if (net_unit.flags & UNIT_SERVER) {
            for (i = 1; i <= MAX_CONNECTIONS; i++)
                if (serviceDescriptor[i].ioSocket == 0) {
                    s = sim_accept_conn(serviceDescriptor[1].masterSocket, NULL);
                    if (s != INVALID_SOCKET) {
                        serviceDescriptor[i].ioSocket = s;
                        sim_debug(ACCEPT_MSG, &net_dev, "NET: " ADDRESS_FORMAT " Accepted connection %i with socket %i.\n", PCX, i, s);
                    }
                }
        }
        else if (serviceDescriptor[0].ioSocket == 0) {
            serviceDescriptor[0].ioSocket = sim_connect_sock(net_unit.filename, "localhost", "3000");
            if (serviceDescriptor[0].ioSocket == INVALID_SOCKET)
                return SCPE_IOERR;
            printf("\rWaiting for server ... Type g<return> (possibly twice) when ready" NLP);
            return SCPE_STOP;
        }
        for (i = 0; i <= MAX_CONNECTIONS; i++)
            if (serviceDescriptor[i].ioSocket) {
                if (serviceDescriptor[i].inputSize < BUFFER_LENGTH) { /* there is space left in inputBuffer */
                    r = sim_read_sock(serviceDescriptor[i].ioSocket, svcBuffer,
                        BUFFER_LENGTH - serviceDescriptor[i].inputSize);
                    if (r == -1) {
                        sim_debug(DROP_MSG, &net_dev, "NET: " ADDRESS_FORMAT " Drop connection %i with socket %i.\n", PCX, i, serviceDescriptor[i].ioSocket);
                        sim_close_sock(serviceDescriptor[i].ioSocket, FALSE);
                        serviceDescriptor[i].ioSocket = 0;
                        serviceDescriptor_reset(i);
                        continue;
                    }
                    else {
                        for (j = 0; j < r; j++) {
                            serviceDescriptor[i].inputBuffer[serviceDescriptor[i].inputPosWrite++] = svcBuffer[j];
                            if (serviceDescriptor[i].inputPosWrite == BUFFER_LENGTH)
                                serviceDescriptor[i].inputPosWrite = 0;
                        }
                        serviceDescriptor[i].inputSize += r;
                    }
                }
                if (serviceDescriptor[i].outputSize > 0) { /* there is something to write in outputBuffer */
                    k = serviceDescriptor[i].outputPosRead;
                    for (j = 0; j < serviceDescriptor[i].outputSize; j++) {
                        svcBuffer[j] = serviceDescriptor[i].outputBuffer[k++];
                        if (k == BUFFER_LENGTH)
                            k = 0;
                    }
                    r = sim_write_sock(serviceDescriptor[i].ioSocket, svcBuffer, serviceDescriptor[i].outputSize);
                    if (r >= 0) {
                        serviceDescriptor[i].outputSize -= r;
                        serviceDescriptor[i].outputPosRead += r;
                        if (serviceDescriptor[i].outputPosRead >= BUFFER_LENGTH)
                            serviceDescriptor[i].outputPosRead -= BUFFER_LENGTH;
                    }
                    else
                        printf("write %i" NLP, r);
                }
            }
    }
    return SCPE_OK;
}

int32 netStatus(const int32 port, const int32 io, const int32 data) {
    uint32 i;
    if ((net_unit.flags & UNIT_ATT) == 0)
        return 0;
    net_svc(&net_unit);
    if (io == 0)    /* IN   */
        for (i = 0; i <= MAX_CONNECTIONS; i++)
            if (serviceDescriptor[i].Z80StatusPort == port)
                return (serviceDescriptor[i].inputSize > 0 ? 1 : 0) |
                    (serviceDescriptor[i].outputSize < BUFFER_LENGTH ? 2 : 0);
    return 0;
}

int32 netData(const int32 port, const int32 io, const int32 data) {
    uint32 i;
    char result;
    if ((net_unit.flags & UNIT_ATT) == 0)
        return 0;
    net_svc(&net_unit);
    for (i = 0; i <= MAX_CONNECTIONS; i++)
        if (serviceDescriptor[i].Z80DataPort == port) {
            if (io == 0) {  /* IN   */
                if (serviceDescriptor[i].inputSize == 0) {
                    printf("re-read from %i" NLP, port);
                    result = serviceDescriptor[i].inputBuffer[serviceDescriptor[i].inputPosRead > 0 ?
                        serviceDescriptor[i].inputPosRead - 1 : BUFFER_LENGTH - 1];
                }
                else {
                    result = serviceDescriptor[i].inputBuffer[serviceDescriptor[i].inputPosRead++];
                    if (serviceDescriptor[i].inputPosRead == BUFFER_LENGTH)
                        serviceDescriptor[i].inputPosRead = 0;
                    serviceDescriptor[i].inputSize--;
                }
                sim_debug(IN_MSG, &net_dev, "NET: " ADDRESS_FORMAT "  IN(%i)=%03xh (%c)\n", PCX, port, (result & 0xff), (32 <= (result & 0xff)) && ((result & 0xff) <= 127) ? (result & 0xff) : '?');
                return result;
            }
            else {          /* OUT  */
                if (serviceDescriptor[i].outputSize == BUFFER_LENGTH) {
                    printf("over-write %i to %i" NLP, data, port);
                    serviceDescriptor[i].outputBuffer[serviceDescriptor[i].outputPosWrite > 0 ?
                        serviceDescriptor[i].outputPosWrite - 1 : BUFFER_LENGTH - 1] = data;
                }
                else {
                    serviceDescriptor[i].outputBuffer[serviceDescriptor[i].outputPosWrite++] = data;
                    if (serviceDescriptor[i].outputPosWrite== BUFFER_LENGTH)
                        serviceDescriptor[i].outputPosWrite = 0;
                    serviceDescriptor[i].outputSize++;
                }
                sim_debug(OUT_MSG, &net_dev, "NET: " ADDRESS_FORMAT " OUT(%i)=%03xh (%c)\n", PCX, port, data, (32 <= data) && (data <= 127) ? data : '?');
                return 0;
            }
        }
    return 0;
}
