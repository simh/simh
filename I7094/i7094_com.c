/* i7094_com.c: IBM 7094 7750 communications interface simulator

   Copyright (c) 2005-2010, Robert M Supnik

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

   com          7750 controller
   coml         7750 lines

   12-Aug-10    RMS     Major rewrite for CTSS (Dave Pitts)
   19-Nov-08    RMS     Revised for common TMXR show routines

   This module implements an abstract simulator for the IBM 7750 communications
   computer as used by the CTSS system.  The 7750 supports up to 112 lines;
   the simulator supports 33.  The 7750 can handle both high-speed lines, in
   6b and 12b mode, and normal terminals, in 12b mode only; the simulator 
   supports only terminals.  The 7750 can handle many different kinds of
   terminals; the simulator supports only a limited subset.

   Input is asynchronous and line buffered.  When valid input (a line or a
   control message) is available, the 7750 sets ATN1 to signal availability of
   input.  When the 7094 issues a CTLRN, the 7750 gathers available input characters
   into a message.  The message has a 12b sequence number, followed by 12b line
   number/character pairs, followed by end-of-medium (03777).  Input characters
   can either be control characters (bit 02000 set) or data characters.  Data
   characters are 1's complemented and are 8b wide: 7 data bits and 1 parity
   bit (which may be 0).

   Output is synchronous.  When the 7094 issues a CTLWN, the 7750 interprets
   the channel output as a message.  The message has a 12b line number, followed
   by a 12b character count, followed by characters, followed by end-of-medium.
   If bit 02000 of the line number is set, the characters are 12b wide.  If
   bit 01000 is set, the message is a control message.  12b characters consist
   of 7 data bits, 1 parity bit, and 1 start bit.  Data characters are 1's
   complemented.  Data character 03777 is special and causes the 7750 to
   repeat the previous bit for the number of bit times specified in the next
   character.  This is used to generate delays for positioning characters.

   The 7750 supports flow control for output.  To help the 7094 account for
   usage of 7750 buffer memory, the 7750 sends 'character output completion'
   messages for every 'n' characters output on a line, where n <= 31.

   Note that the simulator console is mapped in as line n+1.
*/

#include "i7094_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#define COM_MLINES      31                              /* mux lines */
#define COM_TLINES      (COM_MLINES + 1)                /* total lines */
#define COM_BUFSIZ      120                             /* max chan transfer */
#define COM_PKTSIZ      16384                           /* character buffer */

#define UNIT_V_2741     (TTUF_V_UF + 0)                 /* 2741 - ni */
#define UNIT_V_K35      (TTUF_V_UF + 1)                 /* KSR-35 */
#define UNIT_2741       (1 << UNIT_V_2741)
#define UNIT_K35        (1 << UNIT_V_K35)

#define CONN            u3                              /* line is connected */
#define NEEDID          u4                              /* need to send ID */
#define NOECHO          u5                              /* no echo */
#define INPP            u6                              /* input pending */

#define COM_INIT_POLL   8000                            /* polling interval */
#define COMC_WAIT       2                               /* channel delay time */
#define COML_WAIT       1000                            /* char delay time */
#define COM_LBASE       4                               /* start of lines */

/* Input threads */

#define COM_PLU         0                               /* multiplexor poll */
#define COM_CIU         1                               /* console input */
#define COM_CHU         2                               /* channel transfer */
#define COM_SNS         3                               /* sense transfer */

/* Communications input */

#define COMI_VALIDL     02000                           /* valid line flag */
#define COMI_PARITY     00200                           /* parity bit */
#define COMI_DIALUP     02001                           /* dialup */
#define COMI_ENDID      02002                           /* end ID */
#define COMI_INTR       02003                           /* interrupt */
#define COMI_QUIT       02004                           /* quit */
#define COMI_HANGUP     02005                           /* hangup */
#define COMI_EOM        03777                           /* end of medium */
#define COMI_COMP(x)    ((uint16) (03000 + ((x) & COMI_CMAX)))
#define COMI_K35        1                               /* KSR-35 ID */
#define COMI_K37        7                               /* KSR-37 ID */
#define COMI_2741       8                               /* 2741 ID */
#define COMI_CMAX       31                              /* max chars returned */
#define COMI_BMAX       50                              /* buffer max, words */
#define COMI_12BMAX     ((3 * COMI_BMAX) - 1)           /* last 12b char */

/* Communications output */

#define COMO_LIN12B     INT64_C(0200000000000)          /* line is 12b */
#define COMO_LINCTL     INT64_C(0100000000000)          /* control msg */
#define COMO_GETLN(x)   (((uint32) ((x) >> 24)) & 0777)
#define COMO_CTLRST     00000                           /* control reset */
#define COMO_BITRPT     03777                           /* bit repeat */
#define COMO_EOM12B     07777                           /* end of medium */
#define COMO_BMAX       94                              /* buffer max, words */
#define COMO_12BMAX     ((3 * COMO_BMAX) - 1)

/* Status word (60b) */

#define COMS_PCHK       INT64_C(004000000000000000000)  /* prog check */
#define COMS_DCHK       INT64_C(002000000000000000000)  /* data check */
#define COMS_EXCC       INT64_C(001000000000000000000)  /* exc cond */
#define COMS_MLNT       INT64_C(000040000000000000000)  /* message length check */
#define COMS_CHNH       INT64_C(000020000000000000000)  /* channel hold */
#define COMS_CHNQ       INT64_C(000010000000000000000)  /* channel queue full */
#define COMS_ITMO       INT64_C(000000100000000000000)  /* interface timeout */
#define COMS_DATR       INT64_C(000000004000000000000)  /* data message ready */
#define COMS_INBF       INT64_C(000000002000000000000)  /* input buffer free */
#define COMS_SVCR       INT64_C(000000001000000000000)  /* service message ready */
#define COMS_PALL       INT64_C(000000000000000000000)
#define COMS_DALL       INT64_C(000000000000000000000)
#define COMS_EALL       INT64_C(000000000000000000000)
#define COMS_DYN        INT64_C(000000007000000000000)

/* Report variables */

#define COMR_FQ         1                               /* free queue */
#define COMR_IQ         2                               /* input queue */
#define COMR_OQ         4                               /* output queue */

/* List heads and entries */

typedef struct {
    uint16              head;
    uint16              tail;
    } LISTHD;

typedef struct {
    uint16              next;
    uint16              data;
    } LISTENT;

/* The 7750 character buffer is maintained as linked lists.  The lists are:

   free                 free list
   inpq[ln]             input queue for line n
   outq[ln]             output queue for line ln

   Links are done as subscripts in array com_pkt.  This allows the list
   headers and the queues themselves to be saved and restored. */

uint32 com_ch = CH_E;                                   /* saved channel */
uint32 com_enab = 0;                                    /* 7750 enabled */
uint32 com_msgn = 0;                                    /* next input msg num */
uint32 com_sta = 0;                                     /* 7750 state */
uint32 com_stop = 0;                                    /* channel stop */
uint32 com_quit = 003;                                  /* quit code */
uint32 com_intr = 0;                                    /* interrupt code */
uint32 com_bptr = 0;                                    /* buffer pointer */
uint32 com_blim = 0;                                    /* buffer count */
uint32 com_tps = 50;                                    /* polls/second */
t_uint64 com_sns = 0;                                   /* sense word */
t_uint64 com_chob = 0;                                  /* chan output buf */
uint32 com_chob_v = 0;                                  /* valid flag */
t_uint64 com_buf[COM_BUFSIZ];                           /* channel buffer */
LISTHD com_free;                                        /* free list */
uint32 com_not_ret[COM_TLINES] = { 0 };                 /* chars not returned */
LISTHD com_inpq[COM_TLINES] = { {0} };                  /* input queues */
LISTHD com_outq[COM_TLINES] = { {0} };                  /* output queues */
LISTENT com_pkt[COM_PKTSIZ];                            /* character packets */
TMLN com_ldsc[COM_MLINES] = { {0} };                      /* line descriptors */
TMXR com_desc = { COM_MLINES, 0, 0, com_ldsc };         /* mux descriptor */

/* Even parity truth table */

static const uint8 com_epar[128] = {
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1
    };

extern uint32 ch_req;

t_stat com_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat com_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_stat comi_svc (UNIT *uptr);
t_stat comc_svc (UNIT *uptr);
t_stat como_svc (UNIT *uptr);
t_stat coms_svc (UNIT *uptr);
t_stat comti_svc (UNIT *uptr);
t_stat comto_svc (UNIT *uptr);
t_stat com_reset (DEVICE *dptr);
t_stat com_attach (UNIT *uptr, CONST char *cptr);
t_stat com_detach (UNIT *uptr);
t_stat com_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat com_show_freeq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat com_show_allq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat com_show_oneq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void com_reset_ln (uint32 i);
uint16 com_get_nexti (uint32 *ln);
uint16 com_gethd_free (LISTHD *lh);
uint16 com_gethd (LISTHD *lh);
uint16 com_gettl_free (LISTHD *lh);
uint16 com_gettl (LISTHD *lh);
t_bool com_new_puttl (LISTHD *lh, uint16 val);
void com_puttl (LISTHD *lh, uint16 ent);
t_bool com_test_inp (void);
void com_set_inpp (uint32 ln);
t_uint64 com_getob (uint32 ch);
t_bool com_qdone (uint32 ch);
void com_end (uint32 ch, uint32 fl, uint32 st);
t_stat com_send_id (uint32 ln);
uint32 com_gen_ccmp (uint32 ln);
t_bool com_queue_in (uint32 ln, uint32 ch);
uint32 com_queue_out (uint32 ln, uint32 *c1);
void com_set_sns (t_uint64 stat);

/* COM data structures

   com_dev      COM device descriptor
   com_unit     COM unit descriptor
   com_reg      COM register list
   com_mod      COM modifiers list
*/

DIB com_dib = { &com_chsel, &com_chwr };

UNIT com_unit[] = {
    { UDATA (&comi_svc, UNIT_ATTABLE, 0), COM_INIT_POLL },
    { UDATA (&comti_svc, UNIT_DIS, 0), KBD_POLL_WAIT },
    { UDATA (&comc_svc, UNIT_DIS, 0), COMC_WAIT },
    { UDATA (&coms_svc, UNIT_DIS, 0), COMC_WAIT }
    };

UNIT coml_unit[] = {
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&como_svc, 0, 0), COML_WAIT },
    { UDATA (&comto_svc, 0, 0), COML_WAIT },
    };

REG com_reg[] = {
    { FLDATA (ENABLE, com_enab, 0) },
    { ORDATA (STATE, com_sta, 6) },
    { ORDATA (MSGNUM, com_msgn, 12) },
    { ORDATA (SNS, com_sns, 60) },
    { ORDATA (CHOB, com_chob, 36) },
    { FLDATA (CHOBV, com_chob_v, 0) },
    { FLDATA (STOP, com_stop, 0) },
    { ORDATA (QUIT, com_quit, 7) },
    { ORDATA (INTR, com_intr, 7) },
    { BRDATA (BUF, com_buf, 8, 36, COM_BUFSIZ) },
    { DRDATA (BPTR, com_bptr, 7), REG_RO },
    { DRDATA (BLIM, com_blim, 7), REG_RO },
    { BRDATA (NRET, com_not_ret, 10, 32, COM_TLINES), REG_RO + PV_LEFT },
    { URDATA (NEEDID, coml_unit[0].NEEDID, 8, 1, 0, COM_TLINES, 0) },
    { URDATA (NOECHO, coml_unit[0].NOECHO, 8, 1, 0, COM_TLINES, 0) },
    { URDATA (INPP, coml_unit[0].INPP, 8, 1, 0, COM_TLINES, 0) },
    { BRDATA (FREEQ, &com_free, 10, 16, 2) },
    { BRDATA (INPQ, com_inpq, 10, 16, 2 * COM_TLINES) },
    { BRDATA (OUTQ, com_outq, 10, 16, 2 * COM_TLINES) },
    { BRDATA (PKTB, com_pkt, 10, 16, 2 * COM_PKTSIZ) },
    { DRDATA (TTIME, com_unit[COM_CIU].wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (WTIME, com_unit[COM_CHU].wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (CHAN, com_ch, 3), REG_HRO },
    { NULL }
    };

MTAB com_mod[] = {
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &com_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &com_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &com_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, COMR_FQ, "FREEQ", NULL,
      NULL, &com_show_ctrl, 0 },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, COMR_IQ, "INPQ", NULL,
      NULL, &com_show_ctrl, 0 },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, COMR_OQ, "OUTQ", NULL,
      NULL, &com_show_ctrl, 0 },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0xFFFFFFFF, "ALL", NULL,
      NULL, &com_show_ctrl, 0 },
    { 0 }
    };

DEVICE com_dev = {
    "COM", com_unit, com_reg, com_mod,
    3, 10, 31, 1, 16, 8,
    &tmxr_ex, &tmxr_dep, &com_reset,
    NULL, &com_attach, &com_detach,
    &com_dib, DEV_MUX | DEV_DIS
    };

/* COML data structures

   coml_dev     COML device descriptor
   coml_unit    COML unit descriptor
   coml_reg     COML register list
   coml_mod     COML modifiers list
*/

MTAB coml_mod[] = {
    { UNIT_K35+UNIT_2741, 0        , "KSR-37", "KSR-37", NULL },
    { UNIT_K35+UNIT_2741, UNIT_K35 , "KSR-35", "KSR-35", NULL },
//  { UNIT_K35+UNIT_2741, UNIT_2741, "2741",   "2741", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, (void*) &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, (void *) &com_desc },
    { MTAB_XTD | MTAB_VUN | MTAB_NMO, 0, "INPQ", NULL,
      NULL, &com_show_oneq, 0 },
    { MTAB_XTD | MTAB_VUN | MTAB_NMO, 1, "OUTQ", NULL,
      NULL, &com_show_oneq, 0 },
    { 0 }
    };

REG coml_reg[] = {
    { URDATA (TIME, coml_unit[0].wait, 10, 24, 0,
              COM_TLINES, REG_NZ + PV_LEFT) },
    { NULL }
    };

DEVICE coml_dev = {
    "COML", coml_unit, coml_reg, coml_mod,
    COM_TLINES, 10, 31, 1, 16, 8,
    NULL, NULL, &com_reset,
    NULL, NULL, NULL,
    NULL, DEV_DIS
    };

/* COM: channel select */

t_stat com_chsel (uint32 ch, uint32 sel, uint32 unit)
{
com_ch = ch;                                            /* save channel */
if (sim_is_active (&com_unit[COM_CHU]) ||               /* not idle? */
    sim_is_active (&com_unit[COM_SNS])) {
    com_end (ch, CHINT_SEQC, 0);                        /* end, seq check */
    return SCPE_OK;
    }

switch (sel) {                                          /* case on select */

    case CHSL_RDS:                                      /* read */
    case CHSL_WRS:                                      /* write */
        com_sns = 0;                                    /* clear status */
        sim_activate (&com_unit[COM_CHU], com_unit[COM_CHU].wait);
        break;

    case CHSL_SNS:                                      /* sense */
        sim_activate (&com_unit[COM_SNS], com_unit[COM_SNS].wait);
        break;

    case CHSL_CTL:                                      /* control */
    default:                                            /* other */
        return STOP_ILLIOP;
        }

com_stop = 0;                                           /* clear stop */
com_sta = sel;                                          /* set initial state */
return SCPE_OK;
}

/* Channel write, from 7909 channel program */

t_stat com_chwr (uint32 ch, t_uint64 val, uint32 stopf)
{
if (stopf)
    com_stop = 1;
else {
    com_chob = val;                                     /* store data */
    com_chob_v = 1;                                     /* set valid */
    }
return SCPE_OK;
}

/* Unit service - SNS */

t_stat coms_svc (UNIT *uptr)
{
t_uint64 dat;

switch (com_sta) {                                      /* case on state */

    case CHSL_SNS:                                      /* prepare data */
        com_sns &= ~COMS_DYN;                           /* clear dynamic flags */
        if (com_free.head)                              /* free space? */
            com_set_sns (COMS_INBF);
        if (com_test_inp ())                            /* pending input? */
            com_set_sns (COMS_DATR);
        com_buf[0] = (com_sns >> 24) & DMASK;           /* buffer is 2 words */
        com_buf[1] = (com_sns << 12) & DMASK;
        com_bptr = 0;
        com_blim = 2;
        com_sta = CHSL_SNS|CHSL_2ND;                    /* 2nd state */
        break;

    case CHSL_SNS|CHSL_2ND:                             /* second state */
        if (com_bptr >= com_blim) {                     /* end of buffer? */
            ch9_set_end (com_ch, 0);                    /* set end */
            ch_req |= REQ_CH (com_ch);                  /* request channel */
            com_sta = CHSL_SNS|CHSL_3RD;                /* 3rd state */
            sim_activate (uptr, 10 * uptr->wait);       /* longer wait */
            return SCPE_OK;
            }
        dat = com_buf[com_bptr++];                      /* get word */
        if (!com_stop)                                  /* send wd to chan */
            ch9_req_rd (com_ch, dat);
        break;

    case CHSL_SNS|CHSL_3RD:                             /* 3rd state */
        if (com_qdone (com_ch))                         /* done? exit */
            return SCPE_OK;
        com_sta = CHSL_SNS;                             /* repeat sequence */
        break;
        }

sim_activate (uptr, uptr->wait);                        /* sched next */
return SCPE_OK;
}

/* Unit service - channel program */

t_stat comc_svc (UNIT *uptr)
{
uint32 i, j, k, ccnt, ln, uln;
uint16 chr, ent;
t_uint64 dat;

switch (com_sta) {                                      /* case on state */

    case CHSL_RDS:                                      /* read start */
        for (i = 0; i < COM_BUFSIZ; i++)                /* clear chan buf */
            com_buf[i] = 0;
        com_buf[0] = com_msgn;                          /* 1st char is msg num */
        com_msgn = (com_msgn + 1) & 03777;              /* incr msg num */
        for (i = 1, j = 0, ln = 0;                      /* check all lines */
            (ln < COM_TLINES) && (i < COMI_12BMAX);     /* until buffer full */
            ln++) {
            chr = (uint16) com_gen_ccmp (ln);           /* completion msg? */
            if ((chr == 0) && coml_unit[ln].INPP) {     /* no, line input? */
                ent = com_gethd_free (&com_inpq[ln]);   /* get first char */
                if (ent != 0)                           /* any input? */
                    chr = com_pkt[ent].data;            /* return char */
                else coml_unit[i].INPP = 0;             /* this line is idle */
                }                                       /* end if input pending */
            if (chr != 0) {                             /* got something? */
                if ((i++ % 3) == 0)                     /* next word? */
                    j++;
                com_buf[j] = (com_buf[j] << 12) |       /* pack line number */
                    ((t_uint64) ((ln + COM_LBASE) | COMI_VALIDL));
                if ((i++ % 3) == 0)                     /* next word? */
                    j++;
                com_buf[j] = (com_buf[j] << 12) |       /* pack data */
                    ((t_uint64) (chr & 07777));
                }                                       /* end if char */
            }                                           /* end for buffer */
        for (k = i % 3; k < 3; k++) {                   /* fill with EOM */
            if (k == 0)                                 /* next word? */
                j++;
            com_buf[j] = (com_buf[j] << 12) | COMI_EOM;
            }
        com_bptr = 0;                                   /* init buf ptr */
        com_blim = j + 1;                               /* save buf size */
        com_sta = CHSL_RDS|CHSL_2ND;                    /* next state */
        break;

    case CHSL_RDS|CHSL_2ND:                             /* read xmit word */
        if (com_bptr >= com_blim)                       /* transfer done? */
            com_end (com_ch, 0, CHSL_RDS|CHSL_3RD);     /* end, next state */
        else {                                          /* more to do */
            dat = com_buf[com_bptr++];                  /* get word */
            if (!com_stop)                              /* give to channel */
                ch9_req_rd (com_ch, dat);
            }
        break;

    case CHSL_RDS|CHSL_3RD:                             /* read end */
        if (com_qdone (com_ch)) {                       /* done? */
            if (com_test_inp ())                        /* more data waiting? */
                ch9_set_atn (com_ch);
            return SCPE_OK;                             /* exit */
            }
        com_sta = CHSL_RDS;                             /* repeat sequence */
        break;

    case CHSL_WRS:                                      /* write start */
        for (i = 0; i < COM_BUFSIZ; i++)                /* clear chan buf */
            com_buf[i] = 0;
        com_bptr = 0;                                   /* init buf ptr */
        com_sta = CHSL_WRS|CHSL_2ND;                    /* next state */
        ch_req |= REQ_CH (com_ch);                      /* request channel */
        com_chob = 0;                                   /* clr, inval buf */
        com_chob_v = 0;
        break;

    case CHSL_WRS|CHSL_2ND:                             /* write first word */
        dat = com_getob (com_ch);                       /* get word? */
        if (dat == INT64_C(0777777777777)) {            /* turn on? */
            com_enab = 1;                               /* enable 7750 */
            com_msgn = 0;                               /* init message # */
            com_end (com_ch, 0, CHSL_WRS|CHSL_4TH);     /* end, last state */
            }
        else if (dat & COMO_LINCTL) {                   /* control message? */
            ln = COMO_GETLN (dat);                      /* line number */
            if (ln >= (COM_TLINES + COM_LBASE))         /* invalid line? */
                return STOP_INVLIN;
            chr = (uint16) ((dat >> 12) & 07777);       /* control message */
            if (chr != COMO_CTLRST)                     /* char must be 0 */
                return STOP_INVMSG;
            if (ln >= COM_LBASE)
                com_reset_ln (ln - COM_LBASE);
            com_end (com_ch, 0, CHSL_WRS|CHSL_4TH);     /* end, last state */
            }
        else {                                          /* data message */
            ccnt = (((uint32) dat >> 12) & 07777) + 1;  /* char count plus EOM */
            if (dat & COMO_LIN12B)                      /* 12b? double */
                ccnt = ccnt << 1;
            com_blim = (ccnt + 6 + 5) / 6;              /* buffer limit */
            if ((com_blim == 1) || (com_blim >= COMO_BMAX))
                return STOP_INVMSG;
            com_buf[com_bptr++] = dat;                  /* store word */
            com_sta = CHSL_WRS|CHSL_3RD;                /* next state */
            ch_req |= REQ_CH (com_ch);                  /* request channel */
            }
        break;

    case CHSL_WRS|CHSL_3RD:                             /* other words */
        dat = com_getob (com_ch);                       /* get word */
        com_buf[com_bptr++] = dat;                      /* store word */
        if (com_bptr >= com_blim) {                     /* transfer done? */
            ln = COMO_GETLN (com_buf[0]);               /* line number */
            if (ln >= (COM_TLINES + COM_LBASE))         /* invalid line? */
                return STOP_INVLIN;
            if ((com_buf[0] & COMO_LIN12B) &&           /* 12b message? */
                (ln >= COM_LBASE)) {
                uln = ln - COM_LBASE;                   /* unit number */
                for (i = 2, j = 0; i < COMO_12BMAX; i++) { /* unpack 12b char */
                    if ((i % 3) == 0)
                        j++;
                    chr = (uint16) (com_buf[j] >> ((2 - (i % 3)) * 12)) & 07777;
                    if (chr == COMO_EOM12B)             /* EOM? */
                        break;
                    if (!com_new_puttl (&com_outq[uln], chr))
                        return STOP_NOOFREE;            /* append to outq */
                    }
                sim_activate (&coml_unit[uln], coml_unit[uln].wait);
                }
            com_end (com_ch, 0, CHSL_WRS|CHSL_4TH);     /* end, last state */
            }
        else if (!com_stop)                             /* request channel */
            ch_req |= REQ_CH (com_ch);
        break;

    case CHSL_WRS|CHSL_4TH:                             /* buffer done */
        if (com_qdone (com_ch))                         /* done? */
            return SCPE_OK;                             /* exit */
        com_sta = CHSL_WRS;                             /* repeat sequence */
        break;

    default:
        return SCPE_IERR;
        }

sim_activate (uptr, uptr->wait);
return SCPE_OK;
}       

/* Unit service - console receive - always running, even if device is not */

t_stat comti_svc (UNIT *uptr)
{
int32 c, ln = COM_MLINES;
uint16 ent;

sim_activate (uptr, uptr->wait);                        /* continue poll */
c = sim_poll_kbd ();                                    /* get character */
if (c && !(c & (SCPE_BREAK|SCPE_KFLAG)))                /* error? */
    return c;
if (!com_enab || (c & SCPE_BREAK))                      /* !enab, break? done */
    return SCPE_OK;
if (coml_unit[ln].NEEDID)                               /* ID needed? */
    return com_send_id (ln);
if ((c & SCPE_KFLAG) && ((c = c & 0177) != 0)) {        /* char input? */
    if ((c == 0177) || (c == '\b')) {                   /* delete? */
        ent = com_gettl_free (&com_inpq[ln]);           /* remove last char */
        if (!coml_unit[ln].NOECHO)
            sim_putchar (ent? '\b': '\a');
        return SCPE_OK;
        }
    if (!com_queue_in (ln, c))                          /* add to inp queue */
        return STOP_NOIFREE;
    if (!coml_unit[ln].NOECHO) {                        /* echo enabled? */
        if (sim_tt_outcvt (c, TT_MODE_7P) >= 0)         /* printable? */
            sim_putchar (c);
        if (c == '\r')                                  /* line end? */
            sim_putchar ('\n');
        }
    }
return SCPE_OK;                                         /* set ATN if input */
}

/* Unit service - receive side

   Poll all active lines for input
   Poll for new connections */

t_stat comi_svc (UNIT *uptr)
{
int32 c, ln, t;
uint16 ent;

if ((uptr->flags & UNIT_ATT) == 0)                      /* attached? */
    return SCPE_OK;
t = sim_rtcn_calb (com_tps, TMR_COM);                   /* calibrate */
sim_activate (uptr, t);                                 /* continue poll */
if (!com_enab)                                          /* not enabled? exit */
    return SCPE_OK;
ln = tmxr_poll_conn (&com_desc);                        /* look for connect */
if (ln >= 0) {                                          /* got one? */
    com_ldsc[ln].rcve = 1;                              /* rcv enabled */ 
    coml_unit[ln].CONN = 1;                             /* flag connected */
    coml_unit[ln].NEEDID = 1;                           /* need ID */
    coml_unit[ln].NOECHO = 0;                           /* echo enabled */
    coml_unit[ln].INPP = 0;                             /* no input pending */
    }
tmxr_poll_rx (&com_desc);                               /* poll for input */
for (ln = 0; ln < COM_MLINES; ln++) {                   /* loop thru mux */
    if (com_ldsc[ln].conn) {                            /* connected? */
        if (coml_unit[ln].NEEDID)
            return com_send_id (ln);
        c = tmxr_getc_ln (&com_ldsc[ln]);               /* get char */
        if (c) {                                        /* any char? */
            c = c & 0177;                               /* mask to 7b */
            if ((c == 0177) || (c == '\b')) {           /* delete? */
                ent = com_gettl_free (&com_inpq[ln]);   /* remove last char */
                if (!coml_unit[ln].NOECHO)
                    tmxr_putc_ln (&com_ldsc[ln], ent? '\b': '\a');
                return SCPE_OK;
                }
            if (!com_queue_in (ln, c))                  /* queue char, err? */
                return STOP_NOIFREE;
            if (com_ldsc[ln].xmte) {                    /* output enabled? */
                if (!coml_unit[ln].NOECHO) {            /* echo enabled? */
                    if (sim_tt_outcvt (c, TT_MODE_7P) >= 0)
                        tmxr_putc_ln (&com_ldsc[ln], c);
                    if (c == '\r')                      /* add LF after CR */
                        tmxr_putc_ln (&com_ldsc[ln], '\n');
                    }
                tmxr_poll_tx (&com_desc);               /* poll xmt */
                }                                       /* end if enabled */
            }                                           /* end if char */
        }                                               /* end if conn */
    else if (coml_unit[ln].CONN) {                      /* not conn, was conn? */
        coml_unit[ln].CONN = 0;                         /* clear connected */
        coml_unit[ln].NEEDID = 0;                       /* clear need id */
        com_set_inpp (ln);                              /* input pending, ATN1 */
        if (!com_new_puttl (&com_inpq[ln], COMI_HANGUP))/* hangup message */
            return STOP_NOIFREE;
        }
    }                                                   /* end for */
return SCPE_OK;
}

/* Unit service - console transmit */

t_stat comto_svc (UNIT *uptr)
{
uint32 ln = COM_MLINES;
uint32 c, c1;

c = com_queue_out (ln, &c1);                            /* get character, cvt */
if (c)                                                  /* printable? output */
    sim_putchar (c);
if (c1)                                                 /* second char? output */
    sim_putchar (c1);
if (com_outq[ln].head == 0)                             /* line idle? */
    ch9_set_atn (com_ch);                               /* set ATN1 */
else sim_activate (uptr, uptr->wait);                   /* next char */
return SCPE_OK;
}

/* Unit service - transmit side */

t_stat como_svc (UNIT *uptr)
{
uint32 c, c1;
int32 ln = uptr - coml_unit;                            /* line # */

if (com_ldsc[ln].conn) {                                /* connected? */
    if (com_ldsc[ln].xmte) {                            /* output enabled? */
        c = com_queue_out (ln, &c1);                    /* get character, cvt */
        if (c)                                          /* printable? output */
            tmxr_putc_ln (&com_ldsc[ln], c);
        if (c1)                                         /* print second */
            tmxr_putc_ln (&com_ldsc[ln], c1);
        }                                               /* end if */
    tmxr_poll_tx (&com_desc);                           /* poll xmt */
    if (com_outq[ln].head == 0)                         /* line idle? */
        ch9_set_atn (com_ch);                           /* set ATN1 */
    else sim_activate (uptr, uptr->wait);               /* next char */
    }                                                   /* end if conn */
return SCPE_OK;
}

/* Send ID sequence on input */

t_stat com_send_id (uint32 ln)
{
com_new_puttl (&com_inpq[ln], COMI_DIALUP);             /* input message: */
if (coml_unit[ln].flags & UNIT_K35)                     /* dialup, ID, endID */
    com_new_puttl (&com_inpq[ln], COMI_K35);
else com_new_puttl (&com_inpq[ln], COMI_K37);
com_new_puttl (&com_inpq[ln], 0);
com_new_puttl (&com_inpq[ln], 0);
com_new_puttl (&com_inpq[ln], 0);
com_new_puttl (&com_inpq[ln], 0);
com_new_puttl (&com_inpq[ln], (uint16) (ln + COM_LBASE));
if (!com_new_puttl (&com_inpq[ln], COMI_ENDID))         /* make sure there */
    return STOP_NOIFREE;                                /* was room for msg */
coml_unit[ln].NEEDID = 0;
com_set_inpp (ln);                                      /* input pending, ATN1 */
return SCPE_OK;
}

/* Translate and queue input character */

t_bool com_queue_in (uint32 ln, uint32 c)
{
uint16 out;

if (c == com_intr) {
    out = COMI_INTR;
    com_set_inpp (ln);
    }
else if (c == com_quit) {
    out = COMI_QUIT;
    com_set_inpp (ln);
    }
else {
    if (c == '\r')
        com_set_inpp (ln);
    if (coml_unit[ln].flags & UNIT_K35) {               /* KSR-35? */
        if (islower (c))                                /* convert LC to UC */
            c = toupper (c);
        }
    else c |= (com_epar[c]? COMI_PARITY: 0);            /* add even parity */
    out = (~c) & 0377;                                  /* 1's complement */
    }
return com_new_puttl (&com_inpq[ln], out);              /* input message */
}

/* Retrieve and translate output character */

uint32 com_queue_out (uint32 ln, uint32 *c1)
{
uint32 c, ent, raw;

*c1 = 0;                                                /* assume non-printing */
if ((ent = com_gethd_free (&com_outq[ln])) == 0)        /* get character */
    return 0;                                           /* nothing, exit */
raw = com_pkt[ent].data;                                /* get 12b character */
com_not_ret[ln]++;
if (raw == COMO_BITRPT) {                               /* insert delay? */
    if (com_gethd_free (&com_outq[ln]))                 /* skip next char */
         com_not_ret[ln]++;                             /* and count it */
    return 0;
    }
c = (~raw >> 1) & 0177;                                 /* remove start, parity */
if ((c >= 040) && (c != 0177)) {                        /* printable? */
    if ((coml_unit[ln].flags & UNIT_K35) && islower (c))/* KSR-35 LC? */
        c = toupper (c);                                /* cvt to UC */
    return c;
    }
switch (c) {

    case '\t': case '\f': case '\b': case '\a':         /* valid ctrls */
        return c;

    case '\r':                                          /* carriage return? */
        *c1 = '\n';                                     /* lf after cr */
        return c;

    case '\n':                                          /* line feed? */
        *c1 = '\n';                                     /* lf after cr */
        return '\r';

    case 022:                                           /* DC2 */
        coml_unit[ln].NOECHO = 1;
        return 0; 

    case 024:                                           /* DC4 */
        coml_unit[ln].NOECHO = 0;
        return 0;
        }

return 0;                                               /* ignore others */
}

/* Generate completion message, if needed */

uint32 com_gen_ccmp (uint32 ln)
{
uint32 t;

if ((t = com_not_ret[ln]) != 0) {                       /* chars not returned? */
    if (t > COMI_CMAX)                                  /* limit to max */
        t = COMI_CMAX;
    com_not_ret[ln] -= t;                               /* keep count */
    return COMI_COMP (t);                               /* gen completion msg */
    }
return 0;
}

/* Read and validate output buffer */

t_uint64 com_getob (uint32 ch)
{
if (com_chob_v)                                         /* valid? clear */
    com_chob_v = 0;
else if (!com_stop) {                                   /* not stopped? */
    ch9_set_ioc (com_ch);                               /* IO check */
    com_set_sns (COMS_ITMO);                            /* set sense bit */
    }
return com_chob;
}

/* Test whether input pending */

t_bool com_test_inp (void)
{
uint32 i;

for (i = 0; i < COM_TLINES; i++) {
    if ((com_not_ret[i] != 0) || coml_unit[i].INPP)
        return TRUE;
    }
return FALSE;
}

/* Set input pending and attention */

void com_set_inpp (uint32 ln)
{
coml_unit[ln].INPP = 1;
ch9_set_atn (com_ch);
return;
}

/* Test for done */

t_bool com_qdone (uint32 ch)
{
if (com_stop || !ch9_qconn (ch)) {                      /* stop or err disc? */
    com_sta = 0;                                        /* ctrl is idle */
    return TRUE;
    }
return FALSE;
}

/* Channel end */

void com_end (uint32 ch, uint32 fl, uint32 st)
{
ch9_set_end (ch, fl);                                   /* set end */
ch_req |= REQ_CH (ch);
com_sta = st;                                           /* next state */
return;
}

/* List routines - remove from head and free */

uint16 com_gethd_free (LISTHD *lh)
{
uint16 ent;

if ((ent = com_gethd (lh)) != 0)
    com_puttl (&com_free, ent);
return ent;
}

/* Remove from tail and free */

uint16 com_gettl_free (LISTHD *lh)
{
uint16 ent;

if ((ent = com_gethd (lh)) != 0)
    com_puttl (&com_free, ent);
return ent;
}

/* Get free entry and insert at tail */

t_bool com_new_puttl (LISTHD *lh, uint16 val)
{
uint16 ent;

if ((ent = com_gethd (&com_free)) != 0) {
    com_pkt[ent].data = val;
    com_puttl (lh, ent);
    return TRUE;
    }
return FALSE;
}

/* Remove from head */

uint16 com_gethd (LISTHD *lh)
{
uint16 ent;

if ((ent = lh->head) != 0) {
    lh->head = com_pkt[ent].next;
    if (lh->head == 0)
        lh->tail = 0;
    }
else lh->tail = 0;
return ent;
}

/* Remove from tail */

uint16 com_gettl (LISTHD *lh)
{
uint16 ent, next;
uint32 i;

ent = lh->tail;
if (lh->head == lh->tail) {
    lh->head = lh->tail = 0;
    return ent;
    }
next = lh->head;
for (i = 0; i < COM_PKTSIZ; i++) {
    if (com_pkt[next].next == ent) {
        com_pkt[next].next = 0;
        lh->tail = next;
        return ent;
        }
    }
return 0;
}

/* Insert at tail */

void com_puttl (LISTHD *lh, uint16 ent)
{
if (lh->tail == 0)
    lh->head = ent;
else com_pkt[lh->tail].next = ent;
com_pkt[ent].next = 0;
lh->tail = ent;
return;
}

/* Set flag in sense */

void com_set_sns (t_uint64 stat)
{
com_sns |= stat;
com_sns &= ~(COMS_PCHK|COMS_DCHK|COMS_EXCC);
if (com_sns & COMS_PALL)
    com_sns |= COMS_PCHK;
if (com_sns & COMS_DALL)
    com_sns |= COMS_DCHK;
if (com_sns & COMS_EALL)
    com_sns |= COMS_EXCC;
return;
}

/* Reset routine */

t_stat com_reset (DEVICE *dptr)
{
uint32 i;

if (dptr->flags & DEV_DIS) {                            /* disabled? */
    com_dev.flags = com_dev.flags | DEV_DIS;            /* disable lines */
    coml_dev.flags = coml_dev.flags | DEV_DIS;
    }
else {
    com_dev.flags = com_dev.flags & ~DEV_DIS;           /* enable lines */
    coml_dev.flags = coml_dev.flags & ~DEV_DIS;
    }
sim_activate (&com_unit[COM_CIU], com_unit[COM_CIU].wait); /* console */
sim_cancel (&com_unit[COM_PLU]);
sim_cancel (&com_unit[COM_CHU]);
if (com_unit[COM_PLU].flags & UNIT_ATT) {               /* master att? */
    int32 t = sim_rtcn_init (com_unit[COM_PLU].wait, TMR_COM);
    sim_activate (&com_unit[COM_PLU], t);
    }
com_enab = 0;
com_sns = 0;
com_msgn = 0;
com_sta = 0;
com_chob = 0;
com_chob_v = 0;
com_stop = 0;
com_bptr = 0;
com_blim = 0;
for (i = 0; i < COM_BUFSIZ; i++)
    com_buf[i] = 0;
for (i = 0; i < COM_TLINES; i++) {                      /* init lines */
    com_inpq[i].head = 0;
    com_inpq[i].tail = 0;
    com_outq[i].head = 0;
    com_outq[i].tail = 0;
    com_reset_ln (i);
    }
com_pkt[0].next = 0;                                    /* init free list */
for (i = 1; i < COM_PKTSIZ; i++) {
    com_pkt[i].next = i + 1;
    com_pkt[i].data = 0;
    }
com_pkt[COM_PKTSIZ - 1].next = 0;                       /* end of free list */
com_free.head = 1;
com_free.tail = COM_PKTSIZ - 1;
coml_unit[COM_MLINES].CONN = 1;                         /* console always conn */
coml_unit[COM_MLINES].NEEDID = 1;
return SCPE_OK;
}

/* Attach master unit */

t_stat com_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = tmxr_attach (&com_desc, uptr, cptr);                /* attach */
if (r != SCPE_OK)                                       /* error */
    return r;
sim_rtcn_init (uptr->wait, TMR_COM);
sim_activate (uptr, 100);                               /* quick poll */
return SCPE_OK;
}

/* Detach master unit */

t_stat com_detach (UNIT *uptr)
{
uint32 i;
t_stat r;

r = tmxr_detach (&com_desc, uptr);                      /* detach */
for (i = 0; i < COM_MLINES; i++)                        /* disable rcv */
    com_ldsc[i].rcve = 0;
sim_cancel (uptr);                                      /* stop poll */
return r;
}

/* Reset an individual line */

void com_reset_ln (uint32 ln)
{
while (com_gethd_free (&com_inpq[ln])) ;
while (com_gethd_free (&com_outq[ln])) ;
com_not_ret[ln] = 0;
coml_unit[ln].NEEDID = 0;
coml_unit[ln].NOECHO = 0;
coml_unit[ln].INPP = 0;
sim_cancel (&coml_unit[ln]);
if ((ln < COM_MLINES) && (com_ldsc[ln].conn == 0))
    coml_unit[ln].CONN = 0;
return;
}

/* Special show commands */

uint32 com_show_qsumm (FILE *st, LISTHD *lh, const char *name)
{
uint32 i, next;

next = lh->head;
for (i = 0; i < COM_PKTSIZ; i++) {
    if (next == 0) {
        if (i == 0)
            fprintf (st, "%s is empty\n", name);
        else if (i == 1)
            fprintf (st, "%s has 1 entry\n", name);
        else fprintf (st, "%s has %d entries\n", name, i);
        return i;
        }
    next = com_pkt[next].next;
    }
fprintf (st, "%s is corrupt\n", name);
return 0;
}

void com_show_char (FILE *st, uint32 ch)
{
uint32 c;

fprintf (st, "%03o", ch);
c = (~ch) & 0177;
if (((ch & 07400) == 0) && (c >= 040) && (c != 0177))
    fprintf (st, "[%c]", c);
return;
}

t_stat com_show_freeq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
com_show_qsumm (st, &com_free, "Free queue");
return SCPE_OK;
}

t_stat com_show_oneq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 entc, ln, i, next;
LISTHD *lh;
char name[20];

ln = uptr - coml_dev.units;
sprintf (name, val? "Output queue %d": "Input queue %d", ln);
lh = val? &com_outq[ln]: &com_inpq[ln];
if ((entc = com_show_qsumm (st, lh, name))) {
    for (i = 0, next = lh->head; next != 0;
         i++, next = com_pkt[next].next) {
        if ((i % 8) == 0)
            fprintf (st, "%d:\t", i);
        com_show_char (st, com_pkt[next].data >> (val? 1: 0));
        fputc ((((i % 8) == 7)? '\n': '\t'), st);
        }
    if (i % 8)
        fputc ('\n', st);
    }
return SCPE_OK;
}

t_stat com_show_allq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 i;

for (i = 0; i < COM_TLINES; i++)
    com_show_oneq (st, coml_dev.units + i, val, desc);
return SCPE_OK;
}

t_stat com_show_ctrl (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
if (!com_enab)
    fprintf (st, "Controller is not initialized\n");
if (val & COMR_FQ)
    com_show_freeq (st, uptr, 0, desc);
if (val & COMR_IQ)
    com_show_allq (st, uptr, 0, desc);
if (val & COMR_OQ)
    com_show_allq (st, uptr, 1, desc);
return SCPE_OK;
}
