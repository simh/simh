/* hp2100_ipl.c: HP 2000 interprocessor link simulator

   Copyright (c) 2002-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   IPLI, IPLO   12875A interprocessor link

   07-Sep-08    JDB     Changed Telnet poll to connect immediately after reset or attach
   15-Jul-08    JDB     Revised EDT handler to refine completion delay conditions
   09-Jul-08    JDB     Revised ipl_boot to use ibl_copy
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   01-Mar-07    JDB     IPLI EDT delays DMA completion interrupt for TSB
                        Added debug printouts
   28-Dec-06    JDB     Added ioCRS state to I/O decoders
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Oct-04    JDB     Fixed enable/disable from either device
   26-Apr-04    RMS     Fixed SFS x,C and SFC x,C
                        Implemented DMA SRQ (follows FLG)
   21-Dec-03    RMS     Adjusted ipl_ptime for TSB (from Mike Gemeny)
   09-May-03    RMS     Added network device flag
   31-Jan-03    RMS     Links are full duplex (found by Mike Gemeny)

   Reference:
   - 12875A Processor Interconnect Kit Operating and Service Manual
            (12875-90002, Jan-1974)


   The 12875A Processor Interconnect Kit consists four 12566A Microcircuit
   Interface cards.  Two are used in each processor.  One card in each system is
   used to initiate transmissions to the other, and the second card is used to
   receive transmissions from the other.  Each pair of cards forms a
   bidirectional link, as the sixteen data lines are cross-connected, so that
   data sent and status returned are supported.  In each processor, data is sent
   on the lower priority card and received on the higher priority card.  Two
   sets of cards are used to support simultaneous transmission in both
   directions.
*/


#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#define UNIT_V_DIAG     (UNIT_V_UF + 0)                 /* diagnostic mode */
#define UNIT_V_ACTV     (UNIT_V_UF + 1)                 /* making connection */
#define UNIT_V_ESTB     (UNIT_V_UF + 2)                 /* connection established */
#define UNIT_V_HOLD     (UNIT_V_UF + 3)                 /* character holding */
#define UNIT_DIAG       (1 << UNIT_V_DIAG)
#define UNIT_ACTV       (1 << UNIT_V_ACTV)
#define UNIT_ESTB       (1 << UNIT_V_ESTB)
#define UNIT_HOLD       (1 << UNIT_V_HOLD)
#define IBUF            buf                             /* input buffer */
#define OBUF            wait                            /* output buffer */
#define DSOCKET         u3                              /* data socket */
#define LSOCKET         u4                              /* listening socket */

/* Debug flags */

#define DEB_CMDS        (1 << 0)                        /* Command initiation and completion */
#define DEB_CPU         (1 << 1)                        /* CPU I/O */
#define DEB_XFER        (1 << 2)                        /* Socket receive and transmit */

extern DIB ptr_dib;                                     /* need PTR select code for boot */

typedef enum { CIN, COUT } CARD;                        /* ipli/iplo selector */

int32 ipl_edtdelay = 1;                                 /* EDT delay (msec) */
int32 ipl_ptime = 31;                                   /* polling interval */
int32 ipl_stopioe = 0;                                  /* stop on error */
int32 ipl_hold[2] = { 0 };                              /* holding character */

FLIP_FLOP ipl_control [2] = { CLEAR, CLEAR };
FLIP_FLOP ipl_flag [2] = { CLEAR, CLEAR };
FLIP_FLOP ipl_flagbuf [2] = { CLEAR, CLEAR };

DEVICE ipli_dev, iplo_dev;
uint32 iplio (uint32 select_code, IOSIG signal, uint32 data);
t_stat ipl_svc (UNIT *uptr);
t_stat ipl_reset (DEVICE *dptr);
t_stat ipl_attach (UNIT *uptr, char *cptr);
t_stat ipl_detach (UNIT *uptr);
t_stat ipl_boot (int32 unitno, DEVICE *dptr);
t_stat ipl_dscln (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat ipl_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc);
t_bool ipl_check_conn (UNIT *uptr);

/* Debug flags table */

DEBTAB ipl_deb[] = {
    { "CMDS", DEB_CMDS },
    { "CPU",  DEB_CPU },
    { "XFER", DEB_XFER },
    { NULL, 0 }  };

/* IPLI data structures

   ipli_dev     IPLI device descriptor
   ipli_unit    IPLI unit descriptor
   ipli_reg     IPLI register list
*/

DIB ipl_dib[] = {
    { IPLI, &iplio },
    { IPLO, &iplio }
    };

#define ipli_dib ipl_dib[0]
#define iplo_dib ipl_dib[1]

UNIT ipl_unit[] = {
    { UDATA (&ipl_svc, UNIT_ATTABLE, 0) },
    { UDATA (&ipl_svc, UNIT_ATTABLE, 0) }
    };

#define ipli_unit ipl_unit[0]
#define iplo_unit ipl_unit[1]

REG ipli_reg[] = {
    { ORDATA (IBUF, ipli_unit.IBUF, 16) },
    { ORDATA (OBUF, ipli_unit.OBUF, 16) },
    { FLDATA (CTL, ipl_control [CIN], 0) },
    { FLDATA (FLG, ipl_flag [CIN],    0) },
    { FLDATA (FBF, ipl_flagbuf [CIN], 0) },
    { ORDATA (HOLD, ipl_hold[CIN], 8) },
    { DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ipl_stopioe, 0) },
    { ORDATA (DEVNO, ipli_dib.devno, 6), REG_HRO },
    { NULL }
    };

MTAB ipl_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", &ipl_setdiag },
    { UNIT_DIAG, 0, "link mode", "LINK", &ipl_setdiag },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "DISCONNECT",
      &ipl_dscln, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 1, "DEVNO", "DEVNO",
      &hp_setdev, &hp_showdev, &ipli_dev },
    { 0 }
    };

DEVICE ipli_dev = {
    "IPLI", &ipli_unit, ipli_reg, ipl_mod,
    1, 10, 31, 1, 16, 16,
    &tmxr_ex, &tmxr_dep, &ipl_reset,
    &ipl_boot, &ipl_attach, &ipl_detach,
    &ipli_dib, DEV_NET | DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, ipl_deb, NULL, NULL
    };

/* IPLO data structures

   iplo_dev     IPLO device descriptor
   iplo_unit    IPLO unit descriptor
   iplo_reg     IPLO register list
*/

REG iplo_reg[] = {
    { ORDATA (IBUF, iplo_unit.IBUF, 16) },
    { ORDATA (OBUF, iplo_unit.OBUF, 16) },
    { FLDATA (CTL, ipl_control [COUT], 0) },
    { FLDATA (FLG, ipl_flag [COUT],    0) },
    { FLDATA (FBF, ipl_flagbuf [COUT], 0) },
    { ORDATA (HOLD, ipl_hold[COUT], 8) },
    { DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
    { ORDATA (DEVNO, iplo_dib.devno, 6), REG_HRO },
    { NULL }
    };

DEVICE iplo_dev = {
    "IPLO", &iplo_unit, iplo_reg, ipl_mod,
    1, 10, 31, 1, 16, 16,
    &tmxr_ex, &tmxr_dep, &ipl_reset,
    &ipl_boot, &ipl_attach, &ipl_detach,
    &iplo_dib, DEV_NET | DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, ipl_deb, NULL, NULL
    };


/* I/O signal handler for the IPLI and IPLO devices.

   In the link mode, the IPLI and IPLO devices are linked via network
   connections to the corresponding cards in another CPU instance.  In the
   diagnostic mode, we simulate the attachment of the interprocessor cable
   between IPLI and IPLO in this machine.

   Implementation notes:

    1. Because this routine is written to handle two devices, the flip-flops are
       stored in arrays, preventing the use of the "setstd" macros for PRL, IRQ,
       and SRQ signals.  The logic for all three is standard, however.

    2. 2000 Access has a race condition that manifests itself by an apparently
       normal boot and operational system console but no PLEASE LOG IN response
       to terminals connected to the multiplexer.  The frequency of occurrence
       is higher on multiprocessor host systems, where the SP and IOP instances
       may execute concurrently.

       The cause is this code in the SP disc loader source (2883.asm, 7900.asm,
       790X.asm, 79X3.asm, and 79XX.asm):

         LDA SDVTR     REQUEST
         JSB IOPMA,I     DEVICE TABLE
         [...]
         STC DMAHS,C   TURN ON DMA
         SFS DMAHS     WAIT FOR
         JMP *-1         DEVICE TABLE
         STC CH2,C     SET CORRECT
         CLC CH2         FLAG DIRECTION

       The STC/CLC normally would cause a second "request device table" command
       to be recognized by the IOP, except that the IOP DMA setup routine
       "DMAXF" (in D61.asm) has specified an end-of-block CLC that holds off the
       IPL interrupt, and the completion interrupt routine "DMACP" ends with a
       STC,C that clears the IPL flag.

       In hardware, the two CPUs are essentially interlocked by the DMA
       transfer, and DMA completion interrupts occur almost simultaneously.
       Therefore, the STC/CLC in the SP is guaranteed to occur before the STC,C
       in the IOP. Under simulation, and especially on multiprocessor hosts,
       that guarantee does not hold.  If the STC/CLC occurs after the STC,C,
       then the IOP starts a second device table DMA transfer, which the SP is
       not expecting.  The IOP never processes the subsequent "start
       timesharing" command, and the muxtiplexer is non-reponsive.

       We employ a workaround that decreases the incidence of the problem: DMA
       output completion interrupts are delayed to allow the other SIMH instance
       a chance to process its own DMA completion.  We do this by processing the
       EDT (End Data Transfer) I/O backplane signal and "sleep"ing for a short
       time if the transfer was an output transfer to the input channel, i.e.,
       a data response to the SP.  This improves the race condition by delaying
       the IOP until the SP has a chance to receive the last word, recognize its
       own DMA input completion, drop out of the SFS loop, and execute the
       STC/CLC.

       The condition is only improved, and not solved, because "sleep"ing the
       IOP doesn't guarantee that the SP will actually execute.  It's possible
       that a higher-priority host process will preempt the SP, and that at the
       sleep expiration, the SP still has not executed the STC/CLC.  Still, in
       testing, the incidence dropped dramatically, so the problem is much less
       intrusive.
*/

uint32 iplio (uint32 select_code, IOSIG signal, uint32 data)
{
const CARD card = (select_code == iplo_dib.devno);              /* set card selector */
UNIT *const uptr = &(ipl_unit [card]);                          /* associated unit pointer */
const char uc = (card == CIN) ? 'I' : 'O';                      /* identify unit for debug */
const DEVICE *dbdev = (card == CIN) ? &ipli_dev : &iplo_dev;    /* identify device for debug */
const char *iotype[] = { "Status", "Command" };
const IOSIG base_signal = IOBASE (signal);                      /* derive base signal */
int32 sta;
char msg[2];

switch (base_signal) {                                  /* dispatch base I/O signal */

    case ioCLF:                                         /* clear flag flip-flop */
        ipl_flag [card] = ipl_flagbuf [card] = CLEAR;
        break;


    case ioSTF:                                         /* set flag flip-flop */
    case ioENF:                                         /* enable flag */
        ipl_flag [card] = ipl_flagbuf [card] = SET;
        break;


    case ioSFC:                                         /* skip if flag is clear */
        setSKF (!ipl_flag [card]);
        break;


    case ioSFS:                                         /* skip if flag is set */
        setSKF (ipl_flag [card]);
        break;


    case ioIOI:                                         /* I/O data input */
        data = uptr->IBUF;                              /* get return data */

        if (DEBUG_PRJ (dbdev, DEB_CPU))
            fprintf (sim_deb, ">>IPL%c LIx: %s = %06o\n", uc, iotype [card ^ 1], data);
        break;


    case ioIOO:                                         /* I/O data output */
        uptr->OBUF = data;

        if (DEBUG_PRJ (dbdev, DEB_CPU))
            fprintf (sim_deb, ">>IPL%c OTx: %s = %06o\n", uc, iotype [card], data);
        break;


    case ioPOPIO:                                       /* power-on preset to I/O */
        ipl_flag [card] = ipl_flagbuf [card] = SET;     /* set flag buffer and flag */
        uptr->OBUF = 0;                                 /* clear output buffer */
                                                        /* fall into CRS handler */

    case ioCRS:                                         /* control reset */
        ipl_control [card] = CLEAR;                     /* clear control */

        if (DEBUG_PRJ (dbdev, DEB_CMDS))
            fprintf (sim_deb, ">>IPL%c CRS: Control cleared\n", uc);
        break;


    case ioCLC:                                         /* clear control flip-flop */
        ipl_control [card] = CLEAR;                     /* clear ctl */

        if (DEBUG_PRJ (dbdev, DEB_CMDS))
            fprintf (sim_deb, ">>IPL%c CLC: Control cleared\n", uc);
        break;


    case ioSTC:                                         /* set control flip-flop */
        ipl_control [card] = SET;                       /* set ctl */

        if (DEBUG_PRJ (dbdev, DEB_CMDS))
            fprintf (sim_deb, ">>IPL%c STC: Control set\n", uc);

        if (uptr->flags & UNIT_ATT) {                   /* attached? */
            if ((uptr->flags & UNIT_ESTB) == 0)         /* established? */
                if (!ipl_check_conn (uptr)) {           /* not established? */
                    data = STOP_NOCONN << IOT_V_REASON; /* lose */
                    break;
                    }

            msg[0] = (uptr->OBUF >> 8) & 0377;
            msg[1] = uptr->OBUF & 0377;
            sta = sim_write_sock (uptr->DSOCKET, msg, 2);

            if (DEBUG_PRJ (dbdev, DEB_XFER))
                fprintf (sim_deb,
                    ">>IPL%c STC: Socket write = %06o, status = %d\n",
                    uc, uptr->OBUF, sta);

            if (sta == SOCKET_ERROR) {
                printf ("IPL: socket write error\n");
                data = SCPE_IOERR << IOT_V_REASON;
                break;
                }

            sim_os_sleep (0);
            }

        else if (uptr->flags & UNIT_DIAG) {             /* diagnostic mode? */
            ipl_unit [card ^ 1].IBUF = uptr->OBUF;      /* output to other */
            iplio (ipl_dib [card ^ 1].devno, ioENF, 0); /* set other flag */
            }

        else
            data = SCPE_UNATT << IOT_V_REASON;          /* lose */
        break;


    case ioEDT:                                         /* end data transfer */
        if ((cpu_unit.flags & UNIT_IOP) &&              /* are we the IOP? */
            ((IOSIG) data == ioIOO) && (card == CIN)) { /*   and doing output on input card? */

            if (DEBUG_PRJ (dbdev, DEB_CMDS))
                fprintf (sim_deb,
                    ">>IPL%c EDT: Delaying DMA completion interrupt for %d msec\n",
                    uc, ipl_edtdelay);

            sim_os_ms_sleep (ipl_edtdelay);             /* delay completion */
            }
        break;


    case ioSIR:                                         /* set interrupt request */
        setPRL (select_code, !(ipl_control [card] & ipl_flag [card]));
        setIRQ (select_code, ipl_control [card] & ipl_flag [card] & ipl_flagbuf [card]);
        setSRQ (select_code, ipl_flag [card]);
        break;


    case ioIAK:                                         /* interrupt acknowledge */
        ipl_flagbuf [card] = CLEAR;
        break;


    default:                                            /* all other signals */
        break;                                          /*   are ignored */
    }


if (signal > ioCLF)                                     /* multiple signals? */
    iplio (select_code, ioCLF, 0);                      /* issue CLF */
else if (signal > ioSIR)                                /* signal affected interrupt status? */
    iplio (select_code, ioSIR, 0);                      /* set interrupt request */

return data;
}


/* Unit service - poll for input */

t_stat ipl_svc (UNIT *uptr)
{
CARD card;
int32 nb;
char msg[2], uc;
DEVICE *dbdev;                                          /* device ptr for debug */

if ((uptr->flags & UNIT_ATT) == 0) return SCPE_OK;      /* not attached? */
sim_activate (uptr, ipl_ptime);                         /* reactivate */

if ((uptr->flags & UNIT_ESTB) == 0)                     /* not established? */
    if (!ipl_check_conn (uptr))                         /* check for conn */
        return SCPE_OK;                                 /* cot connected */

nb = sim_read_sock (uptr->DSOCKET, msg, ((uptr->flags & UNIT_HOLD)? 1: 2));
if (nb < 0) {                                           /* connection closed? */
    printf ("IPL: socket read error\n");
    return SCPE_IOERR;
    }
if (nb == 0) return SCPE_OK;                            /* no data? */

card = (uptr == &iplo_unit);                            /* set card selector */

if (uptr->flags & UNIT_HOLD) {                          /* holdover byte? */
    uptr->IBUF = (ipl_hold[card] << 8) | (((int32) msg[0]) & 0377);
    uptr->flags = uptr->flags & ~UNIT_HOLD;
    }
else if (nb == 1) {
    ipl_hold[card] = ((int32) msg[0]) & 0377;
    uptr->flags = uptr->flags | UNIT_HOLD;
    }
else uptr->IBUF = ((((int32) msg[0]) & 0377) << 8) |
    (((int32) msg[1]) & 0377);

iplio (ipl_dib [card].devno, ioENF, 0);                 /* set flag */

uc = (card == CIN) ? 'I' : 'O';                         /* identify unit for debug */
dbdev = (card == CIN) ? &ipli_dev : &iplo_dev;          /* identify device for debug */

if (DEBUG_PRJ (dbdev, DEB_XFER))
    fprintf (sim_deb, ">>IPL%c svc: Socket read = %06o, status = %d\n",
        uc, uptr->IBUF, nb);

return SCPE_OK;
}


t_bool ipl_check_conn (UNIT *uptr)
{
SOCKET sock;

if (uptr->flags & UNIT_ESTB) return TRUE;               /* established? */
if (uptr->flags & UNIT_ACTV) {                          /* active connect? */
    if (sim_check_conn (uptr->DSOCKET, 0) <= 0) return FALSE;
    }
else {
    sock = sim_accept_conn (uptr->LSOCKET, NULL);       /* poll connect */
    if (sock == INVALID_SOCKET) return FALSE;           /* got a live one? */
    uptr->DSOCKET = sock;                               /* save data socket */
    }
uptr->flags = uptr->flags | UNIT_ESTB;                  /* conn established */
return TRUE;
}


/* Reset routine.

   Implementation notes:

    1. We set up the first poll for socket connections to occur "immediately"
       upon execution, so that clients will be connected before execution
       begins.  Otherwise, a fast program may access the IPL before the poll
       service routine activates.
*/

t_stat ipl_reset (DEVICE *dptr)
{
UNIT *uptr = dptr->units;

hp_enbdis_pair (dptr,                                   /* make pair cons */
    (dptr == &ipli_dev)? &iplo_dev: &ipli_dev);

if (sim_switches & SWMASK ('P'))                        /* PON reset? */
    uptr->IBUF = uptr->OBUF = 0;                        /* clr buffers */

if (dptr == &ipli_dev)                                  /* input channel reset? */
    iplio (ipli_dib.devno, ioPOPIO, 0);                 /* send POPIO signal */
else                                                    /* output channel reset */
    iplio (iplo_dib.devno, ioPOPIO, 0);                 /* send POPIO signal */

if (uptr->flags & UNIT_ATT)                             /* socket attached? */
    sim_activate (uptr, POLL_FIRST);                    /* activate first poll "immediately" */
else
    sim_cancel (uptr);                                  /* deactivate unit */

uptr->flags = uptr->flags & ~UNIT_HOLD;                 /* clear holding flag */
return SCPE_OK;
}


/* Attach routine

   attach -l - listen for connection on port
   attach -c - connect to ip address and port
*/

t_stat ipl_attach (UNIT *uptr, char *cptr)
{
SOCKET newsock;
uint32 i, t, ipa, ipp, oldf;
char *tptr;
t_stat r;

r = get_ipaddr (cptr, &ipa, &ipp);
if ((r != SCPE_OK) || (ipp == 0)) return SCPE_ARG;
oldf = uptr->flags;
if (oldf & UNIT_ATT) ipl_detach (uptr);
if ((sim_switches & SWMASK ('C')) ||
    ((sim_switches & SIM_SW_REST) && (oldf & UNIT_ACTV))) {
        if (ipa == 0) ipa = 0x7F000001;
        newsock = sim_connect_sock (ipa, ipp);
        if (newsock == INVALID_SOCKET) return SCPE_IOERR;
        printf ("Connecting to IP address %d.%d.%d.%d, port %d\n",
            (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
            (ipa >> 8) & 0xff, ipa & 0xff, ipp);
        if (sim_log) fprintf (sim_log,
            "Connecting to IP address %d.%d.%d.%d, port %d\n",
            (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
            (ipa >> 8) & 0xff, ipa & 0xff, ipp);
        uptr->flags = uptr->flags | UNIT_ACTV;
        uptr->LSOCKET = 0;
        uptr->DSOCKET = newsock;
        }
else {
    if (ipa != 0) return SCPE_ARG;
    newsock = sim_master_sock (ipp);
    if (newsock == INVALID_SOCKET) return SCPE_IOERR;
    printf ("Listening on port %d\n", ipp);
    if (sim_log) fprintf (sim_log, "Listening on port %d\n", ipp);
    uptr->flags = uptr->flags & ~UNIT_ACTV;
    uptr->LSOCKET = newsock;
    uptr->DSOCKET = 0;
    }
uptr->IBUF = uptr->OBUF = 0;
uptr->flags = (uptr->flags | UNIT_ATT) & ~(UNIT_ESTB | UNIT_HOLD);
tptr = (char *) malloc (strlen (cptr) + 1);             /* get string buf */
if (tptr == NULL) {                                     /* no memory? */
    ipl_detach (uptr);                                  /* close sockets */
    return SCPE_MEM;
    }
strcpy (tptr, cptr);                                    /* copy ipaddr:port */
uptr->filename = tptr;                                  /* save */
sim_activate (uptr, POLL_FIRST);                        /* activate first poll "immediately" */
if (sim_switches & SWMASK ('W')) {                      /* wait? */
    for (i = 0; i < 30; i++) {                          /* check for 30 sec */
        if (t = ipl_check_conn (uptr)) break;           /* established? */
        if ((i % 10) == 0)                              /* status every 10 sec */
            printf ("Waiting for connnection\n");
        sim_os_sleep (1);                               /* sleep 1 sec */
        }
    if (t) printf ("Connection established\n");
    }
return SCPE_OK;
}

/* Detach routine */

t_stat ipl_detach (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT)) return SCPE_OK;          /* attached? */
if (uptr->flags & UNIT_ACTV) sim_close_sock (uptr->DSOCKET, 1);
else {
    if (uptr->flags & UNIT_ESTB)                        /* if established, */
        sim_close_sock (uptr->DSOCKET, 0);              /* close data socket */
    sim_close_sock (uptr->LSOCKET, 1);                  /* closen listen socket */
    }
free (uptr->filename);                                  /* free string */
uptr->filename = NULL;
uptr->LSOCKET = 0;
uptr->DSOCKET = 0;
uptr->flags = uptr->flags & ~(UNIT_ATT | UNIT_ACTV | UNIT_ESTB);
sim_cancel (uptr);                                      /* don't poll */
return SCPE_OK;
}

/* Disconnect routine */

t_stat ipl_dscln (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (cptr) return SCPE_ARG;
if (((uptr->flags & UNIT_ATT) == 0) || (uptr->flags & UNIT_ACTV) ||
    ((uptr->flags & UNIT_ESTB) == 0)) return SCPE_NOFNC;
sim_close_sock (uptr->DSOCKET, 0);
uptr->DSOCKET = 0;
uptr->flags = uptr->flags & ~UNIT_ESTB;
return SCPE_OK;
}

/* Diagnostic/normal mode routine */

t_stat ipl_setdiag (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val) {
    ipli_unit.flags = ipli_unit.flags | UNIT_DIAG;
    iplo_unit.flags = iplo_unit.flags | UNIT_DIAG;
    }
else {
    ipli_unit.flags = ipli_unit.flags & ~UNIT_DIAG;
    iplo_unit.flags = iplo_unit.flags & ~UNIT_DIAG;
    }
return SCPE_OK;
}

/* Interprocessor link bootstrap routine (HP Access Manual) */

#define MAX_BASE        073
#define IPL_PNTR        074
#define PTR_PNTR        075
#define IPL_DEVA        076
#define PTR_DEVA        077

static const BOOT_ROM ipl_rom = {
    0163774,                    /*BBL LDA ICK,I         ; IPL sel code */
    0027751,                    /*    JMP CFG           ; go configure */
    0107700,                    /*ST  CLC 0,C           ; intr off */
    0002702,                    /*    CLA,CCE,SZA       ; skip in */
    0063772,                    /*CN  LDA M26           ; feed frame */
    0002307,                    /*EOC CCE,INA,SZA,RSS   ; end of file? */
    0027760,                    /*    JMP EOT           ; yes */
    0017736,                    /*    JSB READ          ; get #char */
    0007307,                    /*    CMB,CCE,INB,SZB,RSS ; 2's comp; null? */
    0027705,                    /*    JMP EOC           ; read next */
    0077770,                    /*    STB WC            ; word in rec */
    0017736,                    /*    JSB READ          ; get feed frame */
    0017736,                    /*    JSB READ          ; get address */
    0074000,                    /*    STB 0             ; init csum */
    0077771,                    /*    STB AD            ; save addr */
    0067771,                    /*CK  LDB AD            ; check addr */
    0047773,                    /*    ADB MAXAD         ; below loader */
    0002040,                    /*    SEZ               ; E =0 => OK */
    0102055,                    /*    HLT 55 */
    0017736,                    /*    JSB READ          ; get word */
    0040001,                    /*    ADA 1             ; cont checksum */
    0177771,                    /*    STB AD,I          ; store word */
    0037771,                    /*    ISZ AD */
    0000040,                    /*    CLE               ; force wd read */
    0037770,                    /*    ISZ WC            ; block done? */
    0027717,                    /*    JMP CK            ; no */
    0017736,                    /*    JSB READ          ; get checksum */
    0054000,                    /*    CPB 0             ; ok? */
    0027704,                    /*    JMP CN            ; next block */
    0102011,                    /*    HLT 11            ; bad csum */
    0000000,                    /*RD  0 */
    0006600,                    /*    CLB,CME           ; E reg byte ptr */
    0103700,                    /*IO1 STC RDR,C         ; start reader */
    0102300,                    /*IO2 SFS RDR           ; wait */
    0027741,                    /*    JMP *-1 */
    0106400,                    /*IO3 MIB RDR           ; get byte */
    0002041,                    /*    SEZ,RSS           ; E set? */
    0127736,                    /*    JMP RD,I          ; no, done */
    0005767,                    /*    BLF,CLE,BLF       ; shift byte */
    0027740,                    /*    JMP IO1           ; again */
    0163775,                    /*    LDA PTR,I         ; get ptr code */
    0043765,                    /*CFG ADA SFS           ; config IO */
    0073741,                    /*    STA IO2 */
    0043766,                    /*    ADA STC */
    0073740,                    /*    STA IO1 */
    0043767,                    /*    ADA MIB */
    0073743,                    /*    STA IO3 */
    0027702,                    /*    JMP ST */
    0063777,                    /*EOT LDA PSC           ; put select codes */
    0067776,                    /*    LDB ISC           ; where xloader wants */
    0102077,                    /*    HLT 77 */
    0027702,                    /*    JMP ST */
    0000000,                    /*    NOP */
    0102300,                    /*SFS SFS 0 */
    0001400,                    /*STC 1400 */
    0002500,                    /*MIB 2500 */
    0000000,                    /*WC  0 */
    0000000,                    /*AD  0 */
    0177746,                    /*M26 -26 */
    0000000,                    /*MAX -BBL */
    0007776,                    /*ICK ISC */
    0007777,                    /*PTR IPT */
    0000000,                    /*ISC 0 */
    0000000                     /*IPT 0 */
    };

t_stat ipl_boot (int32 unitno, DEVICE *dptr)
{
const int32 devi = ipli_dib.devno;
const int32 devp = ptr_dib.devno;

ibl_copy (ipl_rom, devi);                               /* copy bootstrap to memory */
SR = (devi << IBL_V_DEV) | devp;                        /* set SR */
WritePW (PC + MAX_BASE, (~PC + 1) & DMASK);             /* fix ups */
WritePW (PC + IPL_PNTR, ipl_rom[IPL_PNTR] | PC);
WritePW (PC + PTR_PNTR, ipl_rom[PTR_PNTR] | PC);
WritePW (PC + IPL_DEVA, devi);
WritePW (PC + PTR_DEVA, devp);
return SCPE_OK;
}
