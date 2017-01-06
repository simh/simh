/* hp2100_ipl.c: HP 2000 interprocessor link simulator

   Copyright (c) 2002-2016, Robert M Supnik

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

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   14-Sep-15    JDB     Exposed "ipl_edtdelay" via a REG_HIDDEN to allow user tuning
                        Corrected typos in comments and strings
   05-Jun-15    JDB     Merged 3.x and 4.x versions using conditionals
   11-Feb-15    MP      Revised ipl_detach and ipl_dscln for new sim_close_sock API
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   12-Dec-12    MP      Revised ipl_attach for new socket API
   25-Oct-12    JDB     Removed DEV_NET to allow restoration of listening ports
   09-May-12    JDB     Separated assignments from conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added CARD_INDEX casts to dib.card_index
   07-Apr-11    JDB     A failed STC may now be retried
   28-Mar-11    JDB     Tidied up signal handling
   27-Mar-11    JDB     Consolidated reporting of consecutive CRS signals
   29-Oct-10    JDB     Revised for new multi-card paradigm
   26-Oct-10    JDB     Changed I/O signal handler for revised signal model
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
#include "sim_rev.h"


#if (SIM_MAJOR >= 4)
  #define sim_close_sock(socket,master)     sim_close_sock (socket)
#endif


typedef enum { ipli, iplo } CARD_INDEX;                 /* card index number */

#define CARD_COUNT      2                               /* count of cards supported */

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

int32 ipl_edtdelay = 1;                                 /* EDT delay (msec) */
int32 ipl_ptime = 31;                                   /* polling interval */
int32 ipl_stopioe = 0;                                  /* stop on error */

typedef struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    int32     hold;                                     /* holding character */
    } CARD_STATE;

CARD_STATE ipl [CARD_COUNT];                            /* per-card state */

IOHANDLER iplio;

t_stat ipl_svc (UNIT *uptr);
t_stat ipl_reset (DEVICE *dptr);
t_stat ipl_attach (UNIT *uptr, CONST char *cptr);
t_stat ipl_detach (UNIT *uptr);
t_stat ipl_boot (int32 unitno, DEVICE *dptr);
t_stat ipl_dscln (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ipl_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_bool ipl_check_conn (UNIT *uptr);

/* Debug flags table */

DEBTAB ipl_deb [] = {
    { "CMDS", DEB_CMDS },
    { "CPU",  DEB_CPU },
    { "XFER", DEB_XFER },
    { NULL, 0 }
    };

/* Common structures */

DEVICE ipli_dev, iplo_dev;

static DEVICE *dptrs [] = { &ipli_dev, &iplo_dev };


UNIT ipl_unit [] = {
    { UDATA (&ipl_svc, UNIT_ATTABLE, 0) },
    { UDATA (&ipl_svc, UNIT_ATTABLE, 0) }
    };

#define ipli_unit ipl_unit [ipli]
#define iplo_unit ipl_unit [iplo]


DIB ipl_dib [] = {
    { &iplio, IPLI, 0 },
    { &iplio, IPLO, 1 }
    };

#define ipli_dib ipl_dib [ipli]
#define iplo_dib ipl_dib [iplo]


/* IPLI data structures

   ipli_dev     IPLI device descriptor
   ipli_unit    IPLI unit descriptor
   ipli_reg     IPLI register list
*/

REG ipli_reg [] = {
    { ORDATA (IBUF, ipli_unit.IBUF, 16) },
    { ORDATA (OBUF, ipli_unit.OBUF, 16) },
    { FLDATA (CTL, ipl [ipli].control, 0) },
    { FLDATA (FLG, ipl [ipli].flag,    0) },
    { FLDATA (FBF, ipl [ipli].flagbuf, 0) },
    { ORDATA (HOLD, ipl [ipli].hold, 8) },
    { DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
    { FLDATA (STOP_IOE, ipl_stopioe, 0) },
    { DRDATA (EDTDELAY, ipl_edtdelay, 32), REG_HIDDEN | PV_LEFT },
    { ORDATA (SC, ipli_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, ipli_dib.select_code, 6), REG_HRO },
    { NULL }
    };

MTAB ipl_mod [] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", &ipl_setdiag },
    { UNIT_DIAG, 0, "link mode", "LINK", &ipl_setdiag },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "DISCONNECT",
      &ipl_dscln, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,            1, "SC",    "SC",    &hp_setsc,  &hp_showsc,  &ipli_dev },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DEVNO", "DEVNO", &hp_setdev, &hp_showdev, &ipli_dev },
    { 0 }
    };

DEVICE ipli_dev = {
    "IPLI", &ipli_unit, ipli_reg, ipl_mod,
    1, 10, 31, 1, 16, 16,
    &tmxr_ex, &tmxr_dep, &ipl_reset,
    &ipl_boot, &ipl_attach, &ipl_detach,
    &ipli_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, ipl_deb, NULL, NULL
    };

/* IPLO data structures

   iplo_dev     IPLO device descriptor
   iplo_unit    IPLO unit descriptor
   iplo_reg     IPLO register list
*/

REG iplo_reg [] = {
    { ORDATA (IBUF, iplo_unit.IBUF, 16) },
    { ORDATA (OBUF, iplo_unit.OBUF, 16) },
    { FLDATA (CTL, ipl [iplo].control, 0) },
    { FLDATA (FLG, ipl [iplo].flag,    0) },
    { FLDATA (FBF, ipl [iplo].flagbuf, 0) },
    { ORDATA (HOLD, ipl [iplo].hold, 8) },
    { DRDATA (TIME, ipl_ptime, 24), PV_LEFT },
    { ORDATA (SC, iplo_dib.select_code, 6), REG_HRO },
    { ORDATA (DEVNO, iplo_dib.select_code, 6), REG_HRO },
    { NULL }
    };

DEVICE iplo_dev = {
    "IPLO", &iplo_unit, iplo_reg, ipl_mod,
    1, 10, 31, 1, 16, 16,
    &tmxr_ex, &tmxr_dep, &ipl_reset,
    &ipl_boot, &ipl_attach, &ipl_detach,
    &iplo_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0, ipl_deb, NULL, NULL
    };


/* I/O signal handler for the IPLI and IPLO devices.

   In the link mode, the IPLI and IPLO devices are linked via network
   connections to the corresponding cards in another CPU instance.  In the
   diagnostic mode, we simulate the attachment of the interprocessor cable
   between IPLI and IPLO in this machine.

   Implementation notes:

    1. 2000 Access has a race condition that manifests itself by an apparently
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
       IPL interrupt, and the completion interrupt routine "DMCMP" ends with a
       STC,C that clears the IPL flag.

       In hardware, the two CPUs are essentially interlocked by the DMA
       transfer, and DMA completion interrupts occur almost simultaneously.
       Therefore, the STC/CLC in the SP is guaranteed to occur before the STC,C
       in the IOP. Under simulation, and especially on multiprocessor hosts,
       that guarantee does not hold.  If the STC/CLC occurs after the STC,C,
       then the IOP starts a second device table DMA transfer, which the SP is
       not expecting.  The IOP never processes the subsequent "start
       timesharing" command, and the multiplexer is non-responsive.

       We employ a workaround that decreases the incidence of the problem: DMA
       output completion interrupts are delayed to allow the other SIMH instance
       a chance to process its own DMA completion.  We do this by processing the
       EDT (End Data Transfer) I/O backplane signal and "sleep"ing for a short
       time if the transfer was an output transfer to the input channel, i.e.,
       a data response to the SP.  This improves the race condition by delaying
       the IOP until the SP has a chance to receive the last word, recognize its
       own DMA input completion, drop out of the SFS loop, and execute the
       STC/CLC.  The delay, "ipl_edtdelay", is initialized to one millisecond
       but is exposed via a hidden IPLI register, "EDTDELAY", that allows the
       user to lengthen the delay if necessary.

       The condition is only improved, and not solved, because "sleep"ing the
       IOP doesn't guarantee that the SP will actually execute.  It's possible
       that a higher-priority host process will preempt the SP, and that at the
       sleep expiration, the SP still has not executed the STC/CLC.  Still, in
       testing, the incidence dropped dramatically, so the problem is much less
       intrusive.

    2. The operating manual for the 12920A Terminal Multiplexer says that "at
       least 100 milliseconds of CLC 0s must be programmed" by systems employing
       the multiplexer to ensure that the multiplexer resets.  In practice, such
       systems issue 128K CLC 0 instructions.  As we provide debug logging of
       IPL resets, a CRS counter is used to ensure that only one debug line is
       printed in response to these 128K CRS invocations.

    3. The STC handler may return "Unit not attached", "I/O error", or "No
       connection on interprocessor link" status if the link fails or is
       improperly configured.  If the error is corrected, the operation may be
       retried by resuming simulated execution.
*/

uint32 iplio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
CARD_INDEX card = (CARD_INDEX) dibptr->card_index;      /* set card selector */
UNIT *const uptr = &(ipl_unit [card]);                  /* associated unit pointer */
const char *iotype [] = { "Status", "Command" };
int32 sta;
char msg [2];
static uint32 crs_count [CARD_COUNT] = { 0, 0 };        /* per-card cntrs for ioCRS repeat */
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

if (crs_count [card] && !(signal_set & ioCRS)) {        /* counting CRSes and not present? */
    if (DEBUG_PRJ (dptrs [card], DEB_CMDS))             /* report reset count */
        fprintf (sim_deb, ">>%s cmds: [CRS] Control cleared %d times\n",
                 dptrs [card]->name, crs_count [card]);

    crs_count [card] = 0;                               /* clear counter */
    }

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            ipl [card].flag = ipl [card].flagbuf = CLEAR;
            break;


        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            ipl [card].flag = ipl [card].flagbuf = SET;
            break;


        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (ipl [card]);
            break;


        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (ipl [card]);
            break;


        case ioIOI:                                     /* I/O data input */
            stat_data = IORETURN (SCPE_OK, uptr->IBUF); /* get return data */

            if (DEBUG_PRJ (dptrs [card], DEB_CPU))
                fprintf (sim_deb, ">>%s cpu:  [LIx] %s = %06o\n", dptrs [card]->name, iotype [card ^ 1], uptr->IBUF);
            break;


        case ioIOO:                                     /* I/O data output */
            uptr->OBUF = IODATA (stat_data);            /* clear supplied status */

            if (DEBUG_PRJ (dptrs [card], DEB_CPU))
                fprintf (sim_deb, ">>%s cpu:  [OTx] %s = %06o\n", dptrs [card]->name, iotype [card], uptr->OBUF);
            break;


        case ioPOPIO:                                   /* power-on preset to I/O */
            ipl [card].flag = ipl [card].flagbuf = SET; /* set flag buffer and flag */
            uptr->OBUF = 0;                             /* clear output buffer */
            break;


        case ioCRS:                                     /* control reset */
            if (crs_count [card] == 0)                  /* first reset? */
                ipl [card].control = CLEAR;             /* clear control */

            crs_count [card] = crs_count [card] + 1;    /* increment count */
            break;


        case ioCLC:                                     /* clear control flip-flop */
            ipl [card].control = CLEAR;                 /* clear ctl */

            if (DEBUG_PRJ (dptrs [card], DEB_CMDS))
                fprintf (sim_deb, ">>%s cmds: [CLC] Control cleared\n", dptrs [card]->name);
            break;


        case ioSTC:                                                 /* set control flip-flop */
            if (DEBUG_PRJ (dptrs [card], DEB_CMDS))
                fprintf (sim_deb, ">>%s cmds: [STC] Control set\n", dptrs [card]->name);

            if (uptr->flags & UNIT_ATT) {                           /* attached? */
                if (!ipl_check_conn (uptr))                         /* not established? */
                    return IORETURN (STOP_NOCONN, 0);               /* lose */

                msg [0] = (uptr->OBUF >> 8) & 0377;
                msg [1] = uptr->OBUF & 0377;
                sta = sim_write_sock (uptr->DSOCKET, msg, 2);

                if (DEBUG_PRJ (dptrs [card], DEB_XFER))
                    fprintf (sim_deb,
                        ">>%s xfer: [STC] Socket write = %06o, status = %d\n",
                        dptrs [card]->name, uptr->OBUF, sta);

                if (sta == SOCKET_ERROR) {
                    printf ("IPL socket write error\n");
                    return IORETURN (SCPE_IOERR, 0);
                    }

                ipl [card].control = SET;                           /* set ctl */

                sim_os_sleep (0);
                }

            else if (uptr->flags & UNIT_DIAG) {                     /* diagnostic mode? */
                ipl [card].control = SET;                           /* set ctl */
                ipl_unit [card ^ 1].IBUF = uptr->OBUF;              /* output to other */
                iplio ((DIB *) dptrs [card ^ 1]->ctxt, ioENF, 0);   /* set other flag */
                }

            else
                return IORETURN (SCPE_UNATT, 0);                    /* lose */
            break;


        case ioEDT:                                     /* end data transfer */
            if ((cpu_unit.flags & UNIT_IOP) &&          /* are we the IOP? */
                (signal_set & ioIOO) &&                 /*   and doing output? */
                (card == ipli)) {                       /*   on the input card? */

                if (DEBUG_PRJ (dptrs [card], DEB_CMDS))
                    fprintf (sim_deb,
                        ">>%s cmds: [EDT] Delaying DMA completion interrupt for %d msec\n",
                        dptrs [card]->name, ipl_edtdelay);

                sim_os_ms_sleep (ipl_edtdelay);         /* delay completion */
                }
            break;


        case ioSIR:                                     /* set interrupt request */
            setstdPRL (ipl [card]);
            setstdIRQ (ipl [card]);
            setstdSRQ (ipl [card]);
            break;


        case ioIAK:                                     /* interrupt acknowledge */
            ipl [card].flagbuf = CLEAR;
            break;


        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unit service - poll for input */

t_stat ipl_svc (UNIT *uptr)
{
CARD_INDEX card;
int32 nb;
char msg [2];

if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return SCPE_OK;

sim_activate (uptr, ipl_ptime);                         /* reactivate */

if (!ipl_check_conn (uptr))                             /* check for conn */
    return SCPE_OK;                                     /* not connected */

nb = sim_read_sock (uptr->DSOCKET, msg, ((uptr->flags & UNIT_HOLD)? 1: 2));

if (nb < 0) {                                           /* connection closed? */
    printf ("IPL socket read error\n");
    return SCPE_IOERR;
    }

else if (nb == 0)                                       /* no data? */
    return SCPE_OK;

card = (CARD_INDEX) (uptr == &iplo_unit);               /* set card selector */

if (uptr->flags & UNIT_HOLD) {                          /* holdover byte? */
    uptr->IBUF = (ipl [card].hold << 8) | (((int32) msg [0]) & 0377);
    uptr->flags = uptr->flags & ~UNIT_HOLD;
    }
else if (nb == 1) {
    ipl [card].hold = ((int32) msg [0]) & 0377;
    uptr->flags = uptr->flags | UNIT_HOLD;
    }
else
    uptr->IBUF = ((((int32) msg [0]) & 0377) << 8) | (((int32) msg [1]) & 0377);

iplio ((DIB *) dptrs [card]->ctxt, ioENF, 0);           /* set flag */

if (DEBUG_PRJ (dptrs [card], DEB_XFER))
    fprintf (sim_deb, ">>%s xfer: Socket read = %06o, status = %d\n",
        dptrs [card]->name, uptr->IBUF, nb);

return SCPE_OK;
}


t_bool ipl_check_conn (UNIT *uptr)
{
SOCKET sock;

if (uptr->flags & UNIT_ESTB)                            /* established? */
    return TRUE;

if (uptr->flags & UNIT_ACTV) {                          /* active connect? */
    if (sim_check_conn (uptr->DSOCKET, 0) <= 0)
        return FALSE;
    }

else {
    sock = sim_accept_conn (uptr->LSOCKET, NULL);       /* poll connect */

    if (sock == INVALID_SOCKET)                         /* got a live one? */
        return FALSE;

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
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */
CARD_INDEX card = (CARD_INDEX) dibptr->card_index;      /* card number */

hp_enbdis_pair (dptr, dptrs [card ^ 1]);                /* make pair cons */

if (sim_switches & SWMASK ('P'))                        /* initialization reset? */
    uptr->IBUF = uptr->OBUF = 0;                        /* clr buffers */

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

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

t_stat ipl_attach (UNIT *uptr, CONST char *cptr)
{
SOCKET newsock;
char *tptr = NULL;
t_stat r;

#if (SIM_MAJOR >= 4)

uint32 i, t;
char host [CBUFSIZE], port [CBUFSIZE], hostport [2 * CBUFSIZE + 3];

t_bool is_active;

is_active = (uptr->flags & UNIT_ACTV) == UNIT_ACTV;     /* is the connection active? */

if (uptr->flags & UNIT_ATT)                             /* if IPL is currently attached, */
    ipl_detach (uptr);                                  /*   detach it first */

if ((sim_switches & SWMASK ('C')) ||                    /* connecting? */
  ((sim_switches & SIM_SW_REST) && is_active)) {        /*   or restoring an active connection? */
    r = sim_parse_addr (cptr, host,                     /* parse the host and port */
                        sizeof (host), "localhost",     /*   from the parameter string */
                        port, sizeof (port),
                        NULL, NULL);

    if ((r != SCPE_OK) || (port [0] == '\0'))           /* parse error or missing port number? */
        return SCPE_ARG;                                /* complain to the user */

    sprintf(hostport, "%s%s%s%s%s", strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "", host [0] ? ":" : "", port);

    newsock = sim_connect_sock (hostport, NULL, NULL);

    if (newsock == INVALID_SOCKET)
        return SCPE_IOERR;

    printf ("Connecting to %s\n", hostport);

    if (sim_log)
        fprintf (sim_log, "Connecting to %s\n", hostport);

    uptr->flags = uptr->flags | UNIT_ACTV;
    uptr->LSOCKET = 0;
    uptr->DSOCKET = newsock;
    }

else {
    if (sim_parse_addr (cptr, host, sizeof(host), NULL, port, sizeof(port), NULL, NULL))
        return SCPE_ARG;
    sprintf(hostport, "%s%s%s%s%s", strchr(host, ':') ? "[" : "", host, strchr(host, ':') ? "]" : "", host [0] ? ":" : "", port);
    newsock = sim_master_sock (hostport, &r);
    if (r != SCPE_OK)
        return r;
    if (newsock == INVALID_SOCKET)
        return SCPE_IOERR;
    printf ("Listening on port %s\n", hostport);
    if (sim_log)
        fprintf (sim_log, "Listening on port %s\n", hostport);
    uptr->flags = uptr->flags & ~UNIT_ACTV;
    uptr->LSOCKET = newsock;
    uptr->DSOCKET = 0;
    }
uptr->IBUF = uptr->OBUF = 0;
uptr->flags = (uptr->flags | UNIT_ATT) & ~(UNIT_ESTB | UNIT_HOLD);
tptr = (char *) malloc (strlen (hostport) + 1);         /* get string buf */
if (tptr == NULL) {                                     /* no memory? */
    ipl_detach (uptr);                                  /* close sockets */
    return SCPE_MEM;
    }
strcpy (tptr, hostport);                                /* copy ipaddr:port */

#else

uint32 i, t, ipa, ipp, oldf;

r = get_ipaddr (cptr, &ipa, &ipp);
if ((r != SCPE_OK) || (ipp == 0))
    return SCPE_ARG;
oldf = uptr->flags;
if (oldf & UNIT_ATT)
    ipl_detach (uptr);
if ((sim_switches & SWMASK ('C')) ||
    ((sim_switches & SIM_SW_REST) && (oldf & UNIT_ACTV))) {
        if (ipa == 0)
            ipa = 0x7F000001;
        newsock = sim_connect_sock (ipa, ipp);
        if (newsock == INVALID_SOCKET)
            return SCPE_IOERR;
        printf ("Connecting to IP address %d.%d.%d.%d, port %d\n",
            (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
            (ipa >> 8) & 0xff, ipa & 0xff, ipp);
        if (sim_log)
            fprintf (sim_log,
                "Connecting to IP address %d.%d.%d.%d, port %d\n",
                (ipa >> 24) & 0xff, (ipa >> 16) & 0xff,
                (ipa >> 8) & 0xff, ipa & 0xff, ipp);
        uptr->flags = uptr->flags | UNIT_ACTV;
        uptr->LSOCKET = 0;
        uptr->DSOCKET = newsock;
        }
else {
    if (ipa != 0)
        return SCPE_ARG;
    newsock = sim_master_sock (ipp);
    if (newsock == INVALID_SOCKET)
        return SCPE_IOERR;
    printf ("Listening on port %d\n", ipp);
    if (sim_log)
        fprintf (sim_log, "Listening on port %d\n", ipp);
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

#endif

uptr->filename = tptr;                                  /* save */
sim_activate (uptr, POLL_FIRST);                        /* activate first poll "immediately" */
if (sim_switches & SWMASK ('W')) {                      /* wait? */
    for (i = 0; i < 30; i++) {                          /* check for 30 sec */
        t = ipl_check_conn (uptr);
        if (t)                                          /* established? */
            break;
        if ((i % 10) == 0)                              /* status every 10 sec */
            printf ("Waiting for connection\n");
        sim_os_sleep (1);                               /* sleep 1 sec */
        }
    if (t)                                              /* if connected (set by "ipl_check_conn" above) */
        printf ("Connection established\n");            /*   then report */
    }
return SCPE_OK;
}

/* Detach routine */

t_stat ipl_detach (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;

if (uptr->flags & UNIT_ACTV)
    sim_close_sock (uptr->DSOCKET, 1);

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

t_stat ipl_dscln (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if (((uptr->flags & UNIT_ATT) == 0) ||
    (uptr->flags & UNIT_ACTV) ||
    ((uptr->flags & UNIT_ESTB) == 0))
    return SCPE_NOFNC;
sim_close_sock (uptr->DSOCKET, 0);
uptr->DSOCKET = 0;
uptr->flags = uptr->flags & ~UNIT_ESTB;
return SCPE_OK;
}

/* Diagnostic/normal mode routine */

t_stat ipl_setdiag (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
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
const int32 devi = ipli_dib.select_code;
const int32 devp = ptr_dib.select_code;

if (ibl_copy (ipl_rom, devi, IBL_S_CLR,                 /* copy the boot ROM to memory and configure */
              IBL_SET_SC (devi) | devp))                /*   the S register accordingly */
    return SCPE_IERR;                                   /* return an internal error if the copy failed */

WritePW (PR + MAX_BASE, (~PR + 1) & DMASK);             /* fix ups */
WritePW (PR + IPL_PNTR, ipl_rom [IPL_PNTR] | PR);
WritePW (PR + PTR_PNTR, ipl_rom [PTR_PNTR] | PR);
WritePW (PR + IPL_DEVA, devi);
WritePW (PR + PTR_DEVA, devp);
return SCPE_OK;
}
