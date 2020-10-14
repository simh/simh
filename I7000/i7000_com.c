/* i7090_com.c: IBM 7094 7750 communications interface simulator
   Derived from Bob Supnik's i7094_com.c 


   Copyright (c) 2005-2009, Robert M Supnik
   Copyright (c) 2010-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert Supnik or Richard Cornwell
   shall not be used in advertising or otherwise to promote the sale, use or other
   dealings in this Software without prior written authorization from Robert Supnik
   or Richard Cornwell

   com          7750 controller
   coml         7750 lines

   This module implements an abstract simulator for the IBM 7750 communications
   computer as used by the CTSS system.  The 7750 supports up to 112 lines;
   the simulator supports 33.  The 7750 can handle both high-speed lines, in
   6b and 12b mode, and normal terminals, in 12b mode only; the simulator
   supports only terminals.  The 7750 can handle many different kinds of
   terminals; the simulator supports only a limited subset.

   Input is asynchronous.  The 7750 sets ATN1 to signal availability of input.
   When the 7094 issues a CTLRN, the 7750 gathers available input characters
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

   Sense word based on 7074 Principles of Operation.

   1   A   Reserved.
   3   4   Program Check          Summary byte
   4   2   Exceptional Condtion   Summary byte
   5   1   Data Check             Summary byte
   7   A   Reserved
   9   4   Message Length Check   Program Check
   10  2   Channel Hold           Program Check
   11  1   Channel Queue Full     Program Check
   13  A   Reserved
   15  4   Reserved
   16  2   Reserved
   17  1   Interface Timeout      Data Check
   19  A   Reserved
   21  4   Data Message Ready     Exceptional Condition
   22  2   Input space available  Exceptional Condition
   23  1   Service Message Ready  Exceptional Condition

*/

#include "i7000_defs.h"
#include "sim_timer.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#ifdef NUM_DEVS_COM
#define COM_MLINES      32      /* mux lines */
#define COM_TLINES      (COM_MLINES)
#define COM_BUFSIZ      120     /* max chan transfer */
#define COM_PKTSIZ      16384   /* character buffer */

#define UNIT_V_2741     (UNIT_V_UF + 0) /* 2741 - ni */
#define UNIT_V_K35      (UNIT_V_UF + 1) /* KSR-35 */
#define UNIT_2741       (1 << UNIT_V_2741)
#define UNIT_K35        (1 << UNIT_V_K35)

#define TMR_COM         2

#define CONN            u3      /* line is connected */
#define NEEDID          u4      /* need to send ID */
#define ECHO            u5      /* echoing output */

#define COM_INIT_POLL   8000    /* polling interval */
#define COMC_WAIT       2       /* channel delay time */
#define COML_WAIT       500     /* char delay time */
#define COM_LBASE       4       /* start of lines */

/* Input threads */

#define COM_PLU         0       /* multiplexor poll */
#define COM_CIU         1       /* console input */
#define COM_CHU         2       /* console output */

/* Communications input */

#define COMI_LCOMP      002000  /* line complete */
#define COMI_DIALUP     002001  /* dialup */
#define COMI_ENDID      002002  /* end ID */
#define COMI_INTR       002003  /* interrupt */
#define COMI_QUIT       002004  /* quit */
#define COMI_HANGUP     002005  /* hangup */
#define COMI_EOM        013777  /* end of medium */
#define COMI_COMP(x)    ((uint16) (03000 + ((x) & COMI_CMAX)))
#define COMI_K35        6       /* KSR-35 ID */
#define COMI_K37        7       /* KSR-37 ID */
#define COMI_2741       8       /* 2741 ID */
#define COMI_CMAX       31      /* max chars returned */
#define COMI_PARITY     00200   /* parity bit */
#define COMI_BMAX       50      /* buffer max, words */
#define COMI_12BMAX     ((3 * COMI_BMAX) - 1)   /* last 12b char */

/* Communications output - characters */

#define COMO_LIN12B     02000   /* line is 12b */
#define COMO_LINCTL     01000   /* control msg */
#define COMO_GETLN(x)   ((x) & 0777)
#define COMO_CTLRST     07777   /* control reset */
#define COMO_BITRPT     03777   /* bit repeat */
#define COMO_EOM12B     07777   /* end of medium */
#define COMO_EOM6B      077     /* end of medium */
#define COMO_BMAX       94      /* buffer max, words */
#define COMO_12BMAX     ((3 * COMO_BMAX) - 1)

/* Report variables */

#define COMR_FQ         1       /* free queue */
#define COMR_IQ         2       /* input queue */
#define COMR_OQ         4       /* output queue */

/* Sense word flags */
#define EXPT_SRVRDY     0x1001  /* Service message available */
#define EXPT_INAVAIL    0x1002  /* Input available */
#define EXPT_DATRDY     0x1004  /* Data ready. */
#define DATA_TIMEOUT    0x2010  /* Timeout */
#define PROG_FULL       0x4100  /* No more space to send message */
#define PROG_HOLD       0x4200  /* Channel hold */
#define PROG_MSGLEN     0x4400  /* Invalid message length */

/* Input and output ring buffers */
uint16          in_buff[256];
int             in_head;
int             in_tail;
int             in_count;    /* Number of entries in queue */
int             in_delay = 5000;


typedef struct
{
    uint16              link;
    uint16              data;
} OLIST;

uint32              com_posti = 0;      /* Posted a IRQ */
uint32              com_active = 0;     /* Channel active */
uint32              com_ocnt = 0;       /* Number of characters to output */
uint32              com_oln = 0;        /* Output line number */
uint32              com_o12b = 0;       /* Outputing 12 bit */
uint32              com_enab = 0;       /* 7750 enabled */
uint32              com_msgn = 0;       /* next input msg num */
uint32              com_sta = 0;        /* 7750 state */
uint32              com_quit = 3;       /* quit code */
uint32              com_intr = 4;       /* interrupt code */
uint32              com_tps = 50;       /* polls/second */
uint8               com_out_inesc[COM_TLINES];
uint16              com_out_head[COM_TLINES];
uint16              com_out_tail[COM_TLINES];
uint16              com_comp_cnt[COM_TLINES];
int                 com_line;           /* Current line */
uint16              com_free;           /* free list */
OLIST               com_buf[10240];
TMLN                com_ldsc[COM_TLINES];       /* line descriptors */
TMXR                com_desc = { COM_TLINES, 0, 0, com_ldsc };  /* mux descriptor */
uint32              com_sense = 0;      /* Sense word */
uint16              com_data;
uint8               com_dflg = 0;


/* 2741 convertion table */
static const uint8 com_2741_out[256] = {
/* Upper case */
/*   0    1    2    3    4    5    6    7 */
    ' ', '-', '2', '+', '*', 'Q', 'Y', 'H',             /* 000 */
    ':', 'M', 'U', 'D', '_', '_', '_', '_',             /* 010 */
    '@', 'K', 'S', 'B', ')', '_', '_', '_',             /* 020 */
   '\'', 'O', 'W', 'F','\n','\b', ' ', '_',             /* 030 */
    '=', 'J', '?', 'A', '(', 'R', 'Z', 'I',             /* 040 */
    '%', 'N', 'V', 'E', '_','\n','\r', '\t',            /* 050 */
    ';', 'L', 'T', 'C', '#', '$', ',', '.',             /* 060 */
    '"', 'P', 'X', 'G', '_','\t', '<', '\0',            /* 070 */
    ' ', '-', '@', '&', '8', 'q', 'y', 'h',             /* 100 */
    '4', 'm', 'u', 'd', '_', '_', '_', '_',             /* 110 */
    '2', 'k', 's', 'b', '0', '_', '_', '_',             /* 120 */
    '6', 'o', 'w', 'f', '_','\b', ' ', '_',             /* 130 */
    '1', 'j', '/', 'a', '9', 'r', 'z', 'i',             /* 140 */
    '5', 'n', 'v', 'e','\n','\n','\r', '\t',            /* 150 */
    '3', 'l', 't', 'c', '_', '!', ',', '.',             /* 160 */
    '7', 'p', 'x', 'g', '_','\t', '_','\0',             /* 170 */
};

/* 76 43 177 15 */
/* 76 23 177 16 */
static const uint8 com_2741_in[128] = {
   /* Control                              */
    0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,     /*0-37*/
   /*Control*/
    0135, 0057, 0155, 0000, 0000, 0155, 0000, 0000,
   /*Control*/
    0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,
   /*Control*/
    0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,
   /*  sp    !    "     #     $     %     &     ' */
    0100, 0365, 0070, 0264, 0165, 0150, 0303, 0130,     /* 40 - 77 */
   /*  (     )    *     +     ,     -     .     / */
    0144, 0124, 0004, 0203, 0166, 0001, 0067, 0342,
   /*  0    1     2     3     4     5     6     7 */
    0324, 0240, 0220, 0360, 0210, 0350, 0330, 0270,
   /*  8    9     :     ;     <     =     >     ? */
    0204, 0344, 0010, 0160, 0000, 0040, 0000, 0142,
   /*  @    A     B     C     D     E     F     G */
    0202, 0043, 0023, 0163, 0013, 0153, 0133, 0073,     /* 100 - 137 */
   /*  H    I     J     K     L     M     N     O */
    0007, 0147, 0141, 0121, 0061, 0111, 0051, 0031,
   /*  P    Q     R     S     T     U     V     W */
    0171, 0105, 0045, 0122, 0062, 0112, 0052, 0032,
   /*  X    Y     Z     [     \     ]     ^     _ */
    0172, 0106, 0046, 0000, 0000, 0000, 0000, 0000,
   /*  `    a     b     c     d     e     f     g */
    0000, 0243, 0223, 0363, 0213, 0353, 0333, 0273,     /* 140 - 177 */
   /*  h    i     j     k     l     m     n     o */
    0207, 0347, 0341, 0321, 0261, 0311, 0251, 0231,
   /*  p    q     r     s     t     u     v     w */
    0371, 0305, 0245, 0322, 0262, 0312, 0252, 0232,
   /*  x    y     z     {     |     }     ~   del*/
    0372, 0306, 0246, 0000, 0000, 0000, 0000, 0177
};



uint32              com_cmd(UNIT * uptr, uint16 cmd, uint16 dev);
t_stat              com_svc(UNIT * uptr);
t_stat              comi_svc(UNIT * uptr);
t_stat              como_svc(UNIT * uptr);
t_stat              comti_svc(UNIT * uptr);
t_stat              comto_svc(UNIT * uptr);
t_stat              com_reset(DEVICE * dptr);
t_stat              com_attach(UNIT * uptr, CONST char *cptr);
t_stat              com_detach(UNIT * uptr);
t_stat              com_summ(FILE * st, UNIT * uptr, int32 val, CONST void *desc);
t_stat              com_show(FILE * st, UNIT * uptr, int32 val, CONST void *desc);
void                com_reset_ln(uint32 i);
t_stat              com_queue_in(uint32 ln, uint16 ch);
uint32              com_queue_out(uint32 ln, uint16 * c1);
t_stat              com_send_id(uint32 ln);
t_stat              com_send_ccmp(uint32 ln);
void                com_skip_outc(uint32 ln);
t_bool              com_get(int ln, uint16 *ch);
t_bool              com_put(int ln, uint16 ch);
void                com_post_eom();
t_bool              com_inp_msg(uint32 ln, uint16 msg);
const char          *com_description(DEVICE *dptr);
t_stat              com_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char          *coml_description(DEVICE *dptr);
t_stat              coml_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);


/* COM data structures

   com_dev      COM device descriptor
   com_unit     COM unit descriptor
   com_reg      COM register list
   com_mod      COM modifiers list
*/
#ifdef I7010
#define COM_CHAN        4
#else
#define COM_CHAN        5
#endif

UNIT                com_unit[] = {
    {UDATA(&comi_svc, UNIT_S_CHAN(COM_CHAN) | UNIT_ATTABLE, 0), COM_INIT_POLL},
    {UDATA(&comti_svc, UNIT_S_CHAN(COM_CHAN) | UNIT_DIS, 0), KBD_POLL_WAIT},
    {UDATA(&com_svc, UNIT_S_CHAN(COM_CHAN) | UNIT_DIS, 0), COMC_WAIT},
};

REG                 com_reg[] = {
    {FLDATA(ENABLE, com_enab, 0)},
    {ORDATA(STATE, com_sta, 6)},
    {ORDATA(MSGNUM, com_msgn, 12)},
    {NULL}
};

MTAB                com_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN",
     &set_chan, &get_chan, NULL, "Set channel"},
#ifndef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "SELECT", "SELECT",
     &chan9_set_select, &chan9_get_select, NULL, "Set selection channel"},
#endif
    {UNIT_ATT, UNIT_ATT, "connections", NULL, NULL, &com_summ},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
     NULL, &com_show, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
     NULL, &com_show, NULL},
    {0}
};

DEVICE              com_dev = {
    "COM", com_unit, com_reg, com_mod,
    3, 10, 31, 1, 16, 8,
    &tmxr_ex, &tmxr_dep, &com_reset,
    NULL, &com_attach, &com_detach,
    &com_dib, DEV_DISABLE| DEV_DEBUG|DEV_NET, 0, dev_debug,
    NULL, NULL, &com_help, NULL, NULL, &com_description
};

/* COMLL data structures

   coml_dev     COML device descriptor
   coml_unit    COML unit descriptor
   coml_reg     COML register list
   coml_mod     COML modifiers list
*/

UNIT                coml_unit[] = {
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 0 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 1 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 2 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 3 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 4 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 5 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 6 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 7 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 8 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 9 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 0 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 1 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 2 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 3 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 4 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 5 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 6 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 7 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 8 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 9 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 0 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 1 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 2 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 3 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 4 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 5 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 6 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 7 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 8 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 9 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 0 */
    {UDATA(&como_svc, 0, 0), COML_WAIT}, /* 1 */
};

MTAB                coml_mod[] = {
    {UNIT_K35 + UNIT_2741, 0, "KSR-37", "KSR-37", NULL, NULL, NULL, 
               "Standard KSR"},
    {UNIT_K35 + UNIT_2741, UNIT_K35, "KSR-35", "KSR-35", NULL, NULL, NULL, 
               "Upper case only KSR"},
    {UNIT_K35 + UNIT_2741, UNIT_2741, "2741", "2741", NULL, NULL, NULL, 
               "IBM 2741 terminal"},
    {MTAB_XTD | MTAB_VUN, 0, NULL, "DISCONNECT",
     &tmxr_dscln, NULL, &com_desc, "Disconnect line"},
    {MTAB_XTD | MTAB_VUN | MTAB_NC, 0, "LOG", "LOG",
     &tmxr_set_log, &tmxr_show_log, &com_desc},
    {MTAB_XTD | MTAB_VUN | MTAB_NC, 0, NULL, "NOLOG",
     &tmxr_set_nolog, NULL, &com_desc},
    {0}
};

REG                 coml_reg[] = {
    {URDATA(TIME, coml_unit[0].wait, 16, 24, 0,
            COM_TLINES, REG_NZ + PV_LEFT)},
    {NULL}
};

DEVICE              coml_dev = {
    "COML", coml_unit, coml_reg, coml_mod,
    COM_TLINES, 10, 31, 1, 16, 8,
    NULL, NULL, &com_reset, NULL, NULL, NULL,
    NULL, DEV_DISABLE, 0, NULL, NULL, 
    NULL, &coml_help, NULL, NULL, &coml_description
};

/* COM: channel select */
uint32 com_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    /* Activate the com device */
    sim_activate(&com_unit[COM_CHU], 10);
#if 0 /* Commented out until I can detect hangup signal */
    if (!sim_is_active(&com_unit[COM_CIU]))      /* console */
        sim_activate(&com_unit[COM_CIU], com_unit[COM_CIU].wait);
    if (!sim_is_active(&com_unit[COM_PLU])) {
        if (com_unit[COM_PLU].flags & UNIT_ATT) {       /* master att? */
            int32               t =
                sim_rtcn_init(com_unit[COM_PLU].wait, TMR_COM);
            sim_activate(&com_unit[COM_PLU], t);
        }
    }
#endif
    com_sta = 1;
    com_dflg = 0;
    com_active = 1;
    return SCPE_OK;
}

/* Unit service - channel program */
t_stat com_svc(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 sel = (uptr->flags & UNIT_SELECT) ? 1 : 0;
    uint8               ch;

    if (sel != chan_test(chan, CTL_SEL))
        return SCPE_OK;

    /* Handle disconnect */
    if (com_sta != 0 && chan_stat(chan, DEV_DISCO)) {
        chan_clear(chan, DEV_WEOR|DEV_SEL);
        com_sta = 0;
        com_active = 0;
        return SCPE_OK;
    }

    if (chan_test(chan, CTL_SNS)) {
        int     eor = (com_sta == 4)?DEV_REOR:0;

        ch = (com_sense >> ((4 - com_sta) * 4)) & 0xf;
        if (ch & 010)   /* Move A bit over one */
           ch ^= 030;
        sim_debug(DEBUG_SNS, &com_dev, "sense unit=%02x\n", ch);
        switch(chan_write_char(chan, &ch, eor)) {
        case TIME_ERROR:
        case END_RECORD:
              com_sta = -1;
              com_sense = 0;
              break;
        case DATA_OK:
              com_sta++;
        }
        sim_activate(uptr, 50);
        return SCPE_OK;
    }

    /* Start a command, only do read/write */
    if (chan_test(chan, CTL_CNTL)) {
        chan_clear(chan, DEV_FULL);
        chan_set(chan, DEV_REOR|DEV_SEL);
        sim_activate(uptr, 50);
        return SCPE_OK;
    }

    /* Send down next buffer word */
    if (chan_test(chan, CTL_READ)) {
        /* Send low order character if one */
        if (com_dflg) {
            ch = com_data & 0377;
        sim_debug(DEBUG_DATA, &com_dev, "sent=%02o\n", ch);
            switch (chan_write_char(chan, &ch, (com_sta == 3)?DEV_REOR:0)) {
            case DATA_OK:
            case END_RECORD:
                com_dflg = 0;
                break;
            case TIME_ERROR:
                com_sense |= DATA_TIMEOUT;
            }
            sim_activate(uptr, 50);
            return SCPE_OK;
        }

        switch (com_sta) {
        case 1:
            com_data = com_msgn;        /* 1st char is msg num */
            com_msgn = (com_msgn + 1) & 03777;  /* incr msg num */
            com_sta++;
            com_posti = 0;
            chan9_clear_error(chan, sel);
            break;
        case 2:
            /* Check if queue empty. */
            if (in_head == in_tail) {
                com_data = COMI_EOM;
                com_sta++;
            } else {
                /* Grab next entry. */
                in_head++;
                /* Wrap around end of ring */
                if (in_head >= (int)((sizeof(in_buff)/sizeof(uint16))))
                    in_head = 0;
                com_data = in_buff[in_head];
                /* Check if end of current transfer */
                if (com_data == COMI_EOM)
                    com_sta++;
                in_count--;
            }
            break;
        case 3:
            chan_set(chan, DEV_REOR|CTL_END);
            sim_activate(uptr, 50);
            com_posti = 0;
            com_sta++;
            return SCPE_OK;      /* q empty, done */
        }
        sim_debug(DEBUG_DATA, &com_dev, "send data=%04o\n", com_data);
        ch = (com_data >> 6) & 077;
        com_data &= 077;
        switch (chan_write_char(chan, &ch, 0)) {
        case DATA_OK:
        case END_RECORD:
            com_dflg = 1;
            break;
        case TIME_ERROR:
            com_sense |= DATA_TIMEOUT;
        }
        sim_activate(uptr, 50);
        return SCPE_OK;
    }

    if (chan_test(chan, CTL_WRITE)) {
        uint32              ln;

        /* Read in two characters */
        if (com_dflg == 0) {
            switch(chan_read_char(chan, &ch, 0)) {
            case DATA_OK:
                com_dflg = 1;
                com_data = (ch & 077) << 6;
                break;
            case END_RECORD:
            case TIME_ERROR:
                com_sense |= DATA_TIMEOUT;
            }
            sim_activate(uptr, 50);
            return SCPE_OK;
        } else {
            switch(chan_read_char(chan, &ch, 0)) {
            case DATA_OK:
                com_dflg = 0;
                com_data |= (ch & 077);
                break;
            case END_RECORD:
            case TIME_ERROR:
                com_sense |= DATA_TIMEOUT;
                sim_activate(uptr, 50);
                return SCPE_OK;
            }
        }
        sim_debug(DEBUG_DATA, &com_dev, "recieved=%04o\n", com_data);
        switch (com_sta) {
        case 1:
            com_oln = com_data;
            if (com_data == 07777) {    /* turn on? */
                sim_debug(DEBUG_DETAIL, &com_dev, "enable\n");
                com_enab = 1;   /* enable 7750 */
                in_delay = 200;
                com_msgn = 0;   /* init message # */
                com_sta = 4;
                chan_set(chan, DEV_REOR|CTL_END);
            } else if (com_data & COMO_LINCTL) {        /* control message? */
                ln = COMO_GETLN(com_data);      /* line number */
                sim_debug(DEBUG_DETAIL, &com_dev, "line %d\n", ln);
                if (ln >= (COM_TLINES + COM_LBASE))      /* invalid line? */
                    return STOP_INVLIN;
                if (ln > COM_LBASE)             /* valid line? */
                     com_reset_ln(ln - COM_LBASE);
                com_sta = 4;
                chan_set(chan, DEV_REOR|CTL_END);
            } else              /* data message */
                com_sta++;
            break;
        case 2:
            com_ocnt =  (com_data & 07777) + 1; /* char count plus EOM */
            if (com_oln & COMO_LIN12B) {
                com_ocnt = com_ocnt << 1;       /* 12b double */
                com_o12b = 1;
            } else
                com_o12b = 0;
            com_oln = COMO_GETLN(com_oln);      /* line number */
            sim_debug(DEBUG_DETAIL, &com_dev, "output line %d\n", com_oln);
            com_sta++;  /* next state */
            break;
        case 3:         /* other words */
            ln = com_oln;       /* effective line */
            /* unpack chars */
            if (com_o12b) {
                com_ocnt -= 2;
                if (com_data == COMO_EOM12B) {
                    com_sta++;
                    if (com_ocnt != 0) {
                        chan9_set_error(chan, SNS_UEND);
                        com_sense |= PROG_MSGLEN;
                    }
                    chan_set(chan, DEV_REOR|CTL_END);   /* end, last state */
                    break;      /* EOM? */
                }
            } else {
                com_ocnt--;
                if (((com_data >> 6) & 077) == COMO_EOM6B) {
                    com_sta++;
                    if (com_ocnt != 0) {
                        sim_debug(DEBUG_EXP, &com_dev, "messge length error %d\n", com_ocnt);
                        chan9_set_error(chan, SNS_UEND);
                        com_sense |= PROG_MSGLEN;
                    }
                    chan_set(chan, DEV_REOR|CTL_END);   /* end, last state */
                    break;      /* EOM? */
                }
                sim_debug(DEBUG_DETAIL, &com_dev, "queing %o %d\n", (com_data >> 6) & 077, com_ocnt);
                if (com_put(ln, (com_data >> 6) & 077)) {
                    sim_debug(DEBUG_EXP, &com_dev, "Insert error\n");
                    chan9_set_error(chan, SNS_UEND);
                    com_sense |= PROG_FULL;
                }
                com_ocnt--;
                com_data &= 077;
                if (com_data == COMO_EOM6B) {
                    com_sta++;
                    if (com_ocnt != 0) {
                        sim_debug(DEBUG_EXP, &com_dev, "messge length error %d\n", com_ocnt);
                        chan9_set_error(chan, SNS_UEND);
                        com_sense |= PROG_MSGLEN;
                    }
                    chan_set(chan, DEV_REOR|CTL_END);   /* end, last state */
                    break;      /* EOM? */
                }
            }
            sim_debug(DEBUG_DETAIL, &com_dev, "queing %o %d\n", com_data, com_ocnt);
            if (com_put(ln, com_data)) {
                sim_debug(DEBUG_EXP, &com_dev, "Insert error\n");
                chan9_set_error(chan, SNS_UEND);
                com_sense |= PROG_FULL;
            }
            break;
        }
        sim_activate(uptr, 50);
    }
    return SCPE_OK;
}

/* Unit service - console receive - always running, even if device is not */

t_stat
comti_svc(UNIT * uptr)
{
    int32               c;
    t_stat              r;

    sim_activate(uptr, uptr->wait);     /* continue poll */
    c = sim_poll_kbd();         /* get character */
    if (c && (c < SCPE_KFLAG))
        return c;               /* error? */
    if (((com_unit[COM_PLU].flags & UNIT_ATT) == 0) ||  /* not att, not enab, */
        !com_enab || (c & SCPE_BREAK))
        return SCPE_OK;         /* break? done */
    c = c & 0177;
    if (c) {
        r = com_queue_in(0, c);
        if (r != SCPE_OK)
            return r;
        sim_putchar(c);
        if (c == '\r')
            sim_putchar('\n');
    }
    return SCPE_OK;
}

/* Unit service - receive side

   Poll all active lines for input
   Poll for new connections */

t_stat
comi_svc(UNIT * uptr)
{
    int32               c, ln, t;
    t_stat              r;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_OK;         /* attached? */
    if (in_delay-- <= 0) {      /* Check for any inputs to send over. */
        in_delay = 50;  /* Time to wait for polling again. */
        if (!com_active && in_count > 0)
           com_post_eom();
    }
    t = sim_rtcn_calb(com_tps, TMR_COM);        /* calibrate */
    sim_activate(uptr, t);      /* continue poll */
    ln = tmxr_poll_conn(&com_desc);     /* look for connect */
    if (ln >= 0) {              /* got one? */
        com_ldsc[ln].rcve = 1;  /* rcv enabled */
        coml_unit[ln].CONN = 1; /* flag connected */
        coml_unit[ln].NEEDID = 1;       /* need ID */
        coml_unit[ln].ECHO = 1;         /* echoing output */
    }
    if (!com_enab)
        return SCPE_OK;         /* not enabled? exit */
    tmxr_poll_rx(&com_desc);    /* poll for input */
    for (ln = 0; ln < COM_TLINES; ln++) {       /* loop thru mux */
        if (com_ldsc[ln].conn) {        /* connected? */
            if (coml_unit[ln].NEEDID)
                com_send_id(ln);
            c = tmxr_getc_ln(&com_ldsc[ln]);    /* get char */
            if (c) {            /* any char? */
                c = c & 0177;   /* mask to 7b */
                r = com_queue_in(ln, c);
                if (r != SCPE_OK)
                    return r;   /* queue char, err? */
                if (coml_unit[ln].ECHO && com_ldsc[ln].xmte) {  /* output enabled? */
                    if (coml_unit[ln].flags & UNIT_K35) {       /* KSR-35? */
                        if (islower(c))
                            c = toupper(c);     /* convert LC to UC */
                    }
                    tmxr_putc_ln(&com_ldsc[ln], c);     /* echo char */
                    if (c == '\r')      /* add LF after CR */
                        tmxr_putc_ln(&com_ldsc[ln], '\n');
                }               /* end if enabled */
            }                   /* end if char */
        } /* end if conn */
        else if (coml_unit[ln].CONN) {  /* not conn, was conn? */
            coml_unit[ln].CONN = 0;     /* clear connected */
            coml_unit[ln].NEEDID = 0;   /* clear need id */
            if (com_inp_msg(ln, COMI_HANGUP))   /* hangup message */
                return STOP_NOIFREE;
        }
    }                           /* end for */
    tmxr_poll_tx(&com_desc);    /* poll xmt */
    return SCPE_OK;
}

/* Unit service - console transmit */

t_stat
comto_svc(UNIT * uptr)
{
    uint16              c, c1;

    if (com_out_head[0] == 0)
        return com_send_ccmp(0);  /* Send out a completion code */
    c = com_queue_out(0, &c1);  /* get character, cvt */
    if (c)
        sim_putchar(c);         /* printable? output */
    if (c1)
        sim_putchar(c1);        /* second char? output */
    sim_activate(uptr, uptr->wait);     /* next char */
    if (com_comp_cnt[0] >= COMI_CMAX)   /* completion needed? */
        return com_send_ccmp(0);        /* generate msg */
    return SCPE_OK;
}

/* Unit service - transmit side */

t_stat
como_svc(UNIT * uptr)
{
    uint16              c, c1;
    int32               ln = uptr - coml_unit;  /* line # */

    if (com_out_head[ln] == 0)  /* no more characters? */
        return com_send_ccmp(ln);       /* free any remaining */
    if (com_ldsc[ln].conn) {    /* connected? */
        if (com_ldsc[ln].xmte) {        /* output enabled? */
            c = com_queue_out(ln, &c1); /* get character, cvt */
            if (c)
                tmxr_putc_ln(&com_ldsc[ln], c); /* printable? output */
            if (c1)
                tmxr_putc_ln(&com_ldsc[ln], c1);        /* print second */
        }                       /* end if */
        tmxr_poll_tx(&com_desc);        /* poll xmt */
        sim_activate(uptr, uptr->wait); /* next char */
        if (com_comp_cnt[ln] >= COMI_CMAX)      /* completion needed? */
            return com_send_ccmp(ln);   /* generate msg */
    }                           /* end if conn */
    return SCPE_OK;
}

/* Send ID sequence on input */

t_stat
com_send_id(uint32 ln)
{
    com_inp_msg(ln, COMI_DIALUP);       /* input message: */
    if (coml_unit[ln].flags & UNIT_2741)        /* dialup, ID, endID */
        com_inp_msg(ln, COMI_2741);
    else if (coml_unit[ln].flags & UNIT_K35)
        com_inp_msg(ln, COMI_K35);
    else
        com_inp_msg(ln, COMI_K37);
    com_inp_msg(ln, 0);
    com_inp_msg(ln, 0);
    com_inp_msg(ln, 0);
    com_queue_in(ln, 'T');
    com_queue_in(ln, '0' + ((ln+1) / 10));
    com_queue_in(ln, '0' + ((ln+1) % 10));
    if (com_inp_msg(ln, COMI_ENDID))    /* make sure there */
        return STOP_NOIFREE;    /* was room for msg */
    coml_unit[ln].NEEDID = 0;
    com_sense |= EXPT_SRVRDY;
    return SCPE_OK;
}
/* Translate and queue input character */

t_stat
com_queue_in(uint32 ln, uint16 c)
{
    uint16              out;
    uint16              parity;

    if (c == com_intr)
        out = COMI_INTR;
    else if (c == com_quit)
        out = COMI_QUIT;
    else {
        if (coml_unit[ln].flags & UNIT_K35) {   /* KSR-35? */
            if (islower(c))
                c = toupper(c); /* convert LC to UC */
        }
        if ((coml_unit[ln].flags & UNIT_K35) == 0) { /* KSR-37 or 2741 */
            if (c == '\r')
                c = '\n';
        }
        if (coml_unit[ln].flags & UNIT_2741) {
            c = com_2741_in[c];
            if (c & 0200) {             /* Check if lower case */
                 if ((com_out_inesc[ln] & 2) == 0) {    /* In upper case? */
                    uint8       c2;
                    c2 = com_2741_out[c & 077]; /* Check if no need to change */
                    if (c2 != com_2741_out[(c&077)|0100]) {
                        com_inp_msg(ln, 0034);  /* Switch case */
                        com_out_inesc[ln] &= 1;
                    }
                }
            } else {
                 if (com_out_inesc[ln] & 2) {   /* In lower case? */
                    uint8       c2;
                    c2 = com_2741_out[c & 077]; /* Check if no need to change */
                    if (c2 != com_2741_out[(c&077)|0100]) {
                        com_inp_msg(ln, 0037);  /* Switch case */
                        com_out_inesc[ln] |= 2;
                    }
                 }
            }
            c &= 0177;
        }

        out = (~c) & 0177;      /* 1's complement */
        parity = out ^ (out >> 4);
        parity = parity ^ (parity >> 2);
        parity = parity ^ (parity >> 1);
        if (parity & 1)
            out |= COMI_PARITY; /* add even parity */
    }
    if (com_inp_msg(ln, out))
        return STOP_NOIFREE;    /* input message */
    com_sense |= EXPT_DATRDY;
    return SCPE_OK;
}

/* Retrieve and translate output character */

uint32
com_queue_out(uint32 ln, uint16 * c1)
{
    uint16              c, raw;

    *c1 = 0;                    /* assume non-printing */
    if (com_get(ln, &raw))      /* Try to get character */
        return 0;
    if (raw == COMO_BITRPT) {   /* insert delay? */
        com_skip_outc(ln);
        return 0;
    }
    c = (~raw >> 1) & 0177;     /* remove start, parity */
    if (coml_unit[ln].flags & UNIT_2741) {
        uint8           c2;
        c2 = c & 077;
        if (com_out_inesc[ln] & 4) {
            com_out_inesc[ln] &= 3;
            switch (c) {
            case '\043':                /* Red */
               tmxr_putc_ln(&com_ldsc[ln], '\033');
               tmxr_putc_ln(&com_ldsc[ln], '[');
               tmxr_putc_ln(&com_ldsc[ln], '3');
               tmxr_putc_ln(&com_ldsc[ln], '1');
               tmxr_putc_ln(&com_ldsc[ln], 'm');
               return 0;
            case '\023':                /* Black */
               tmxr_putc_ln(&com_ldsc[ln], '\033');
               tmxr_putc_ln(&com_ldsc[ln], '[');
               tmxr_putc_ln(&com_ldsc[ln], '0');
               tmxr_putc_ln(&com_ldsc[ln], 'm');
               return 0;
            }
            *c1 = c;
            return '\033';
        }
        switch (c2) {
        case '\034':    com_out_inesc[ln] &= 2; return 0;    /* UC */
        case '\037':    com_out_inesc[ln] |= 1; return 0;    /* LC */
        case '\076':    com_out_inesc[ln] |= 4; return 0;   /* Esc */
        case '\016':    coml_unit[ln].ECHO = FALSE; return 0; /* Poff */
        case '\015':    coml_unit[ln].ECHO = TRUE; return 0; /* Pon */
        }
        c2 = com_2741_out[(com_out_inesc[ln]&1)? (0100|c2): c2];
        sim_debug(DEBUG_DETAIL, &com_dev, "printing %d %04o '%c' %o\n",
                   ln, c, (c2>= ' ')?c2: 0, com_out_inesc[ln]&1);
        if (c2 == '\n')
           *c1 = '\r';
        return c2;
    }
    if (com_out_inesc[ln]) {
        com_out_inesc[ln] = 0;
        switch (c) {
        case '3':               /* Red */
               tmxr_putc_ln(&com_ldsc[ln], '\033');
               tmxr_putc_ln(&com_ldsc[ln], '[');
               tmxr_putc_ln(&com_ldsc[ln], '3');
               tmxr_putc_ln(&com_ldsc[ln], '1');
               tmxr_putc_ln(&com_ldsc[ln], 'm');
               return 0;
        case '4':               /* Black */
               tmxr_putc_ln(&com_ldsc[ln], '\033');
               tmxr_putc_ln(&com_ldsc[ln], '[');
               tmxr_putc_ln(&com_ldsc[ln], '0');
               tmxr_putc_ln(&com_ldsc[ln], 'm');
               return 0;
        case ':':               /* Poff */
               coml_unit[ln].ECHO = FALSE;
               return 0;
        case ';':               /* Pon */
               coml_unit[ln].ECHO = TRUE;
               return 0;
        }
        *c1 = c;
        return '\033';
    }
    sim_debug(DEBUG_DETAIL, &com_dev, "printing %d %04o '%c'\n", ln, c, (c >= ' ')?c:0);
    if (c >= 040) {             /* printable? */
        if (c == 0177)
            return 0;           /* DEL? ignore */
        if ((coml_unit[ln].flags & UNIT_K35) && islower(c))     /* KSR-35 LC? */
            c = toupper(c);     /* cvt to UC */
        return c;
    }
    switch (c) {
    case '\033':                /* Escape character */
         com_out_inesc[ln] = 1;
         return 0;

    case '\t':
    case '\f':
    case '\b':
    case '\a':                  /* valid ctrls */
        return c;

    case '\r':                  /* carriage return? */
        if (coml_unit[ln].flags & UNIT_K35)     /* KSR-35? */
            *c1 = '\n';         /* lf after cr */
        return c;

    case '\n':                  /* line feed? */
        if (!(coml_unit[ln].flags & UNIT_K35)) {        /* KSR-37? */
            *c1 = '\n';         /* lf after cr */
            return '\r';
        }
        return c;               /* print lf */

#if 0
    case 022:                   /* DC2 */
        if (!(com_unit[ln].flags & UNIT_K35)) { /* KSR-37? */
            com_skip_outc(ln);  /* skip next */
            return '\n';        /* print lf */
        }
        break;

    case 024:                   /* DC4 */
        if (!(com_unit[ln].flags & UNIT_K35))   /* KSR-37? */
            com_skip_outc(ln);  /* skip next */
        break;
#endif
    }

    return 0;                   /* ignore others */
}

/* Generate completion message, if needed */

t_stat
com_send_ccmp(uint32 ln)
{
    uint32              t;

    t = com_comp_cnt[ln];
    if (t != 0) {               /* chars not returned? */
        if (t > COMI_CMAX)
            t = COMI_CMAX;      /* limit to max */
        com_comp_cnt[ln] -= t;  /* keep count */
        if (com_inp_msg(ln, COMI_COMP(t)))      /* gen completion msg */
            return STOP_NOIFREE;
    }
    return SCPE_OK;
}

/* Skip next char in output queue */

void
com_skip_outc(uint32 ln)
{
    uint16      tmp;
    if (com_get(ln, &tmp))
        com_comp_cnt[ln]++;     /* count it */
    return;
}

/* List routines - remove from head and free */

t_bool
com_get(int ln, uint16 *ch)
{
    uint16              ent;

    ent = com_out_head[ln];
    if (ent == 0)               /* Check if anything to send. */
        return TRUE;
    *ch = com_buf[ent].data;
    com_comp_cnt[ln]++;
    com_out_head[ln] = com_buf[ent].link;       /* Get next char */
    com_buf[ent].link = com_free;               /* Put back on free list */
    com_free = ent;
    if (com_out_head[ln] == 0) { /* done with queue? */
        com_out_tail[ln] = 0;
    }
    return FALSE;
}

/* Put a character onto output queue for a line */
t_bool
com_put(int ln, uint16 ch)
{
    uint16              ent;

    ln -= COM_LBASE;
    ent = com_free;             /* Get a character spot */
    if (ent == 0)
        return TRUE;            /* No room */
    com_free = com_buf[ent].link;       /* Next free character */
    com_buf[ent].data = ch;
    com_buf[ent].link = 0;
    if (com_out_tail[ln] == 0) {        /* Idle line */
        com_out_head[ln] = ent;
    } else {                            /* Active line */
        com_buf[com_out_tail[ln]].link = ent;
    }
    com_out_tail[ln] = ent;
    /* Activate line if not already running */
    if (!sim_is_active(&coml_unit[ln]))
        sim_activate(&coml_unit[ln], coml_unit[ln].wait);
    return FALSE;
}

/* Put EOM on input queue and post interupt to wake up CPU */
void
com_post_eom()
{
     int          ent;
     int          chan = UNIT_G_CHAN(com_unit[0].flags);
     int          sel = (com_unit[0].flags & UNIT_SELECT) ? 1 : 0;
    /* See if we can insert a EOM message */
    if (in_buff[in_tail] != COMI_EOM) {
         sim_debug(DEBUG_EXP, &com_dev, "inserting eom %d %d %d\n",
                in_head, in_tail, in_count);
         ent = in_tail + 1;
         if (ent >= (int)((sizeof(in_buff)/sizeof(uint16)))) /* Wrap around */
             ent = 0;
         if (ent != in_head) { /* If next element would be head, queue is full */
            /* If we can't put one on, handler will do it for us */
             in_buff[ent] = COMI_EOM;
             in_tail = ent;
             in_count++;
         }
     }
     chan9_set_attn(chan, sel);
     com_posti = 1;
}

/* Insert line and message into input queue */

t_bool
com_inp_msg(uint32 ln, uint16 msg)
{
    int          ent1, ent2;

    sim_debug(DEBUG_EXP, &com_dev, "inserting %d %04o %d %d %d\n", ln, msg,
                in_head, in_tail, in_count);
    ent1 = in_tail + 1;
    if (ent1 >= (int)((sizeof(in_buff)/sizeof(uint16)))) /* Wrap around */
        ent1 = 0;
    if (ent1 == in_head) /* If next element would be head, queue is full */
        return TRUE;
    ent2 = ent1 + 1;
    if (ent2 >= (int)((sizeof(in_buff)/sizeof(uint16)))) /* Wrap around */
        ent2 = 0;
    if (ent2 == in_head) /* If next element would be head, queue is full */
        return TRUE;
    /* Ok we have room to put this message in. */
    ln += COM_LBASE;
    in_buff[ent1] = 02000 + ln;
    in_buff[ent2] = msg;
    in_count += 2;
    in_tail = ent2;
    /* Check if over limit */
    if (!com_active && in_count > 150) {
         com_post_eom();
    }
    return FALSE;
}

/* Reset routine */

t_stat
com_reset(DEVICE * dptr)
{
    uint32              i;

    if (dptr->flags & DEV_DIS) {        /* disabled? */
        com_dev.flags = com_dev.flags | DEV_DIS;        /* disable lines */
        coml_dev.flags = coml_dev.flags | DEV_DIS;
    } else {
        com_dev.flags = com_dev.flags & ~DEV_DIS;       /* enable lines */
        coml_dev.flags = coml_dev.flags & ~DEV_DIS;
    }
#if 0   /* Commented out until I can detect hangup signal */
    sim_activate(&com_unit[COM_CIU], com_unit[COM_CIU].wait);   /* console */
    sim_cancel(&com_unit[COM_CHU]);
#endif
    sim_cancel(&com_unit[COM_PLU]);
    if (com_unit[COM_PLU].flags & UNIT_ATT) {   /* master att? */
        int32               t =

            sim_rtcn_init(com_unit[COM_PLU].wait, TMR_COM);
        sim_activate(&com_unit[COM_PLU], t);
    }
    com_enab = 0;
    com_msgn = 0;
    com_sta = 0;
    com_sense = 0;
    /* Put dummy message on Queue before first login */
    in_head = 0;
    in_tail = 0;
    in_count = 0;
    for (i = 0; i < COM_TLINES; i++) {
        com_out_tail[i] = 0;
        com_out_head[i] = 0;
        com_reset_ln(i);
    }
    com_free = sizeof(com_buf)/(sizeof(OLIST));
    for (i = 1; i < com_free; i++) {
        com_buf[i].link = i + 1;
        com_buf[i].data = 0;
    }
    com_buf[com_free - 1].link = 0;     /* end of free list */
    com_free = 1;

    return SCPE_OK;
}

/* Attach master unit */

t_stat
com_attach(UNIT * uptr, CONST char *cptr)
{
    t_stat              r;

    r = tmxr_attach(&com_desc, uptr, cptr);     /* attach */
    if (r != SCPE_OK)
        return r;               /* error */
    sim_rtcn_init(uptr->wait, TMR_COM);
    sim_activate(uptr, 100);    /* quick poll */
    return SCPE_OK;
}

/* Detach master unit */

t_stat
com_detach(UNIT * uptr)
{
    uint32              i;
    t_stat              r;

    r = tmxr_detach(&com_desc, uptr);   /* detach */
    for (i = 0; i < COM_MLINES; i++)
        com_ldsc[i].rcve = 0;   /* disable rcv */
    sim_cancel(uptr);           /* stop poll */
    return r;
}

/* Show summary processor */

t_stat
com_summ(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    uint32              i, t;

    t = 0;
    for (i = 0; i < COM_TLINES; i++)
        t = t + (com_ldsc[i].conn != 0);
    if (t == 1)
        fprintf(st, "1 connection");
    else
        fprintf(st, "%d connections", t);
    return SCPE_OK;
}

/* SHOW CONN/STAT processor */

t_stat
com_show(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    int32               i, cc;

    for (cc = 0; (cc < COM_MLINES) && com_ldsc[cc].conn; cc++) ;
    if (cc) {
        for (i = 0; i < COM_MLINES; i++) {
            if (com_ldsc[i].conn) {
                if (val)
                    tmxr_fconns(st, &com_ldsc[i], i);
                else
                    tmxr_fstats(st, &com_ldsc[i], i);
            }
        }
    } else
        fprintf(st, "all disconnected\n");
    return SCPE_OK;
}

/* Reset an individual line */

void
com_reset_ln(uint32 ln)
{
    uint16      ch;

    while (!com_get(ln, &ch)) ;
    com_comp_cnt[ln] = 0;
    com_out_inesc[ln] = 0;
    sim_cancel(&coml_unit[ln]);
    if ((ln < COM_MLINES) && (com_ldsc[ln].conn == 0))
        coml_unit[ln].CONN = 0;
    return;
}
const char *
coml_description(DEVICE *dptr)
{
    return "IBM 7750 terminal";
}

t_stat
coml_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Each COM line can be set to a given type of terminal\n\n");
fprintf (st, "   sim> SET COMLn KSR-37     Standard connection\n");
fprintf (st, "   sim> SET COMLn KSR-35     Allows only upper case\n");
fprintf (st, "   sim> SET COMLn 2741       Set to look like a 2741\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *
com_description(DEVICE *dptr)
{
    return "IBM 7750 terminal controller";
}

t_stat
com_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "IBM 7750 terminal controller, this is required for CTSS to run.\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
help_set_chan_type(st, dptr, "IBM 7750");
#ifndef I7010
fprintf (st, "Each device on the channel can be at either select 0 or 1, \n");
fprintf (st, "this is set with the\n\n");
fprintf (st, "   sim> SET COM SELECT=n\n\n");
#endif
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

#endif


