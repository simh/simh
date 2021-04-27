/* 3b2_ports.h: AT&T 3B2 Model 400 "PORTS" feature card

   Copyright (c) 2018, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
 * PORTS is an intelligent feature card for the 3B2 that supports four
 * serial lines and one Centronics parallel port.
 *
 * The PORTS card is based on the Common I/O (CIO) platform. It uses
 * two SCN2681A DUARTs to supply the four serial lines, and uses the
 * SCN2681A parallel I/O pins for the Centronics parallel port.
 *
 * This file implements the required logic for the PORTS CIO
 * interface.  The SCN2681A functionality is implemented in the file
 * 3b2_duart.c, and is used by both this feature card and the System
 * Board console/contty functionality.
 */

#ifndef _3B2_PORTS_H_
#define _3B2_PORTS_H_

#include "sim_defs.h"

#define PORTS_ID        0x0003
#define PORTS_IPL       10
#define PORTS_VERSION   1

#define MAX_PORTS_CARDS 12
#define PORTS_LINES     4
#define PORTS_RCV_QUEUE 5

/*
 * Sub-field values for the PPC_DEVICE request entry; these are placed
 * in app_data.bt[0] in the PPC_DEVICE application field. The prefix
 * DR indicates that this is a code for use in "device" request
 * entries only.
*/

#define DR_ENA      1       /* enable a device */
#define DR_DIS      2       /* disable a device */
#define DR_ABR      3       /* abort reception on a device */
#define DR_ABX      4       /* abort transmission on a device */
#define DR_BRK      5       /* transmit "break" on a device */
#define DR_SUS      6       /* suspend xmit on a device */
#define DR_RES      7       /* resume xmit on a device */
#define DR_BLK      8       /* transmit STOP character */
#define DR_UNB      9       /* transmit START character */

/*
 * Sub-field values for the PPC_DEVICE completion entry; these appear
 * in app_data.bt[0] in the PPC_DEVICE application field. These are
 * mutually exclusive and cannot be combined. The prefix DC indicates
 * that this is a code for use in "device" completion entries only.
 */

#define DC_NORM 0x00    /* command executed as requested */
#define DC_DEV  0x01    /* bad device number */
#define DC_NON  0x02    /* bad sub-code on request */
#define DC_FAIL 0x03    /* failed to read express entry */

/*
 * Sub-field values for the PPC_RECV completion entry; these appear in
 * app_data.bt[0] in the PPC_RECV application field. These are NOT
 * mutually exclusive and may appear in combination. The prefix RC
 * indicates that this is a code for use in "read" completion entries
 * only.
*/

#define RC_DSR  0x01    /* disruption of service */
#define RC_FLU  0x02    /* buffer flushed */
#define RC_TMR  0x04    /* inter-character timer expired */
#define RC_BQO  0x08    /* PPC buffer queue overflow */
#define RC_UAO  0x10    /* uart overrun */
#define RC_PAR  0x20    /* parity error */
#define RC_FRA  0x40    /* framing error */
#define RC_BRK  0x80    /* break received */

/*
 * The following codes are included on the DISC (disconnect) command.
 * They are "or"ed into the app_data.bt[1] application field in a
 * request. These codes are NOT mutually exclusive and can be used in
 * any combination.
 */

#define GR_DTR      0x01
#define GR_CREAD    0x02

/*
 * Sub-field values for the PPC_XMIT and PPC_OPTIONS completion
 * entries; these appear in app_data.bt[0] in the application fields.
 * These are NOT mutually exclusive and may appear in combination.
 * The prefix GC indicates that this is a code for use in "general"
 * completion entries only.
*/

#define GC_DSR  0x01    /* disruption of service */
#define GC_FLU  0x02    /* buffer flushed */

/*
 * Sub-field values for the PPC_ASYNC completion entry; these appear
 * in app_data.bt[0] in the PPC_ASYNC application field. These are
 * mutually exclusive and cannot be combined. The prefix AC indicates
 * that this is a code for use in "asynchronous" completion entries
 * only.
*/

#define AC_CON  0x01    /* connection detected */
#define AC_DIS  0x02    /* disconnection detected */
#define AC_BRK  0x03    /* asynchronous "break" */
#define AC_FLU  0x04    /* xmit flush complete */

/* Line Discipline flags (input and output) */

#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100
#define IUCLC   0x0200
#define IXON    0x0400
#define IXANY   0x0800

#define OPOST   0x0001
#define OLCUC   0x0002
#define ONLCR   0x0004
#define OCRNL   0x0008
#define ONOCR   0x0010
#define ONLRET  0x0020
#define OFILL   0x0040
#define OFDEL   0x0080
#define ONLDLY  0x0100
#define OCRDLY  0x0600
#define OTABDLY 0x1800
#define OBSDLY  0x2000
#define OVTDLY  0x4000
#define OFFDLY  0x8000

/* Opcodes for PORTS card */

#define PPC_OPTIONS     32      /* GEN, COMP queues: set PPC options */
#define PPC_XMIT        33      /* GEN, COMP queues: transmit a buffer */
#define PPC_CONN        34      /* GEN, COMP queues: connect a device */
#define PPC_DISC        35      /* GEN, COMP queues: disconnect a device */
#define PPC_BRK         36      /* GEN, COMP queues: ioctl break */
#define PPC_DEVICE      40      /* EXP, ECOMP entries: device control command */
#define PPC_CLR         41      /* EXP, ECOMP entries: board clear */
#define PPC_RECV        50      /* RECV, COMP queues: receive request */
#define PPC_ASYNC       60      /* Asynchronous request */
#define CFW_CONFIG      70      /* GEN, COMP queues: set PPC port 0 hardware options */
#define CFW_IREAD       71      /* GEN, COMP queues: read immediate one to four bytes */
#define CFW_IWRITE      72      /* GEN, COMP queues: write immediate one to four bytes */
#define CFW_WRITE       73      /* GEN, COMP queues: write  */
#define PPC_VERS        80      /* EXP, COMP queues: Version */

typedef struct {
    uint32 tx_addr;      /* Address to next read from */
    uint32 tx_req_addr;  /* Original request address */
    uint32 tx_chars;     /* Number of chars left to transfer */
    uint32 tx_req_chars; /* Original number of chars */
    uint8  rlp;          /* Last known load pointer */
    uint16 iflag;        /* Line Discipline: Input flags */
    uint16 oflag;        /* Line Discipline: Output flags */
    t_bool crlf;         /* Indicates we are in a CRLF output transform */
    t_bool conn;         /* TRUE if connected, FALSE otherwise */
} PORTS_LINE_STATE;

typedef struct {
    uint16 line;    /* line discipline */
    uint16 pad1;
    uint16 iflag;   /* input options word */
    uint16 oflag;   /* output options word */
    uint16 cflag;   /* hardware options */
    uint16 lflag;   /* line discipline options */
    uint8  cerase;  /* "erase" character */
    uint8  ckill;   /* "kill" character */
    uint8  cinter;  /* "interrupt" character */
    uint8  cquit;   /* "quit" character */
    uint8  ceof;    /* "end of file" character */
    uint8  ceol;    /* "end of line" character */
    uint8  itime;   /* inter character timer multiplier */
    uint8  vtime;   /* user-specified inter char timer */
    uint8  vcount;  /* user-specified maximum buffer char count */
    uint8  pad2;
    uint16 pad3;
} PORTS_OPTIONS;

t_stat ports_reset(DEVICE *dptr);
t_stat ports_setnl(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ports_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ports_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ports_rcv_svc(UNIT *uptr);
t_stat ports_xmt_svc(UNIT *uptr);
t_stat ports_cio_svc(UNIT *uptr);
t_stat ports_attach(UNIT *uptr, CONST char *cptr);
t_stat ports_detach(UNIT *uptr);
void ports_sysgen(uint8 cid);
void ports_express(uint8 cid);
void ports_full(uint8 cid);

#endif /* _3B2_PORTS_H_ */
